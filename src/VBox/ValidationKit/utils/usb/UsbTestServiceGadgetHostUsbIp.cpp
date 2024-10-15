/* $Id: UsbTestServiceGadgetHostUsbIp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestServ - Remote USB test configuration and execution server, USB gadget host interface
 *               for USB/IP.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/env.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include "UsbTestServiceGadgetHostInternal.h"
#include "UsbTestServicePlatform.h"


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/**
 * Internal UTS gadget host instance data.
 */
typedef struct UTSGADGETHOSTTYPEINT
{
    /** Handle to the USB/IP daemon process. */
    RTPROCESS                 hProcUsbIp;
} UTSGADGETHOSTTYPEINT;

/** Default port of the USB/IP server. */
#define UTS_GADGET_HOST_USBIP_PORT_DEF 3240


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Worker for binding/unbinding the given gadget from the USB/IP server.
 *
 * @returns IPRT status code.
 * @param   pThis             The gadget host instance.
 * @param   hGadget           The gadget handle.
 * @param   fBind             Flag whether to do a bind or unbind.
 */
static int usbGadgetHostUsbIpBindUnbind(PUTSGADGETHOSTTYPEINT pThis, UTSGADGET hGadget, bool fBind)
{
    RT_NOREF1(pThis);
    uint32_t uBusId, uDevId;
    char aszBus[32];

    uBusId = utsGadgetGetBusId(hGadget);
    uDevId = utsGadgetGetDevId(hGadget);

    /* Create the busid argument string. */
    size_t cbRet = RTStrPrintf(&aszBus[0], RT_ELEMENTS(aszBus), "%u-%u", uBusId, uDevId);
    if (cbRet == RT_ELEMENTS(aszBus))
        return VERR_BUFFER_OVERFLOW;

    /* Bind to the USB/IP server. */
    RTPROCESS hProcUsbIp = NIL_RTPROCESS;
    const char *apszArgv[5];

    apszArgv[0] = "usbip";
    apszArgv[1] = fBind ? "bind" : "unbind";
    apszArgv[2] = "-b";
    apszArgv[3] = &aszBus[0];
    apszArgv[4] = NULL;

    int rc = RTProcCreate("usbip", apszArgv, RTENV_DEFAULT, RTPROC_FLAGS_SEARCH_PATH, &hProcUsbIp);
    if (RT_SUCCESS(rc))
    {
        RTPROCSTATUS ProcSts;
        rc = RTProcWait(hProcUsbIp, RTPROCWAIT_FLAGS_BLOCK, &ProcSts);
        if (RT_SUCCESS(rc))
        {
            /* Evaluate the process status. */
            if (   ProcSts.enmReason != RTPROCEXITREASON_NORMAL
                || ProcSts.iStatus != 0)
                rc = VERR_UNRESOLVED_ERROR; /** @todo Log and give finer grained status code. */
        }
    }

    return rc;
}

/**
 * @interface_method_impl{UTSGADGETHOSTIF,pfnInit}
 */
static DECLCALLBACK(int) utsGadgetHostUsbIpInit(PUTSGADGETHOSTTYPEINT pIf, PCUTSGADGETCFGITEM paCfg)
{
    int rc = VINF_SUCCESS;
    uint16_t uPort = 0;

    pIf->hProcUsbIp = NIL_RTPROCESS;

    rc = utsGadgetCfgQueryU16Def(paCfg, "UsbIp/Port", &uPort, UTS_GADGET_HOST_USBIP_PORT_DEF);
    if (RT_SUCCESS(rc))
    {
        /* Make sure the kernel drivers are loaded. */
        rc = utsPlatformModuleLoad("usbip-core", NULL, 0);
        if (RT_SUCCESS(rc))
        {
            rc = utsPlatformModuleLoad("usbip-host", NULL, 0);
            if (RT_SUCCESS(rc))
            {
                char aszPort[10];
                char aszPidFile[64];
                const char *apszArgv[6];

                RTStrPrintf(aszPort, RT_ELEMENTS(aszPort), "%u", uPort);
                RTStrPrintf(aszPidFile, RT_ELEMENTS(aszPidFile), "/var/run/usbipd-%u.pid", uPort);
                /* Start the USB/IP server process. */
                apszArgv[0] = "usbipd";
                apszArgv[1] = "--tcp-port";
                apszArgv[2] = aszPort;
                apszArgv[3] = "--pid";
                apszArgv[4] = aszPidFile;
                apszArgv[5] = NULL;
                rc = RTProcCreate("usbipd", apszArgv, RTENV_DEFAULT, RTPROC_FLAGS_SEARCH_PATH, &pIf->hProcUsbIp);
                if (RT_SUCCESS(rc))
                {
                    /* Wait for a bit to make sure the server started up successfully. */
                    uint64_t tsStart = RTTimeMilliTS();
                    do
                    {
                        RTPROCSTATUS ProcSts;
                        rc = RTProcWait(pIf->hProcUsbIp, RTPROCWAIT_FLAGS_NOBLOCK, &ProcSts);
                        if (rc != VERR_PROCESS_RUNNING)
                        {
                            rc = VERR_INVALID_HANDLE;
                            break;
                        }
                        RTThreadSleep(1);
                        rc = VINF_SUCCESS;
                    } while (RTTimeMilliTS() - tsStart < 2 * 1000); /* 2 seconds. */
                }
            }
        }
    }

    return rc;
}


/**
 * @interface_method_impl{UTSGADGETHOSTIF,pfnTerm}
 */
static DECLCALLBACK(void) utsGadgetHostUsbIpTerm(PUTSGADGETHOSTTYPEINT pIf)
{
    /* Kill the process and wait for it to terminate. */
    RTProcTerminate(pIf->hProcUsbIp);

    RTPROCSTATUS ProcSts;
    RTProcWait(pIf->hProcUsbIp, RTPROCWAIT_FLAGS_BLOCK, &ProcSts);
}


/**
 * @interface_method_impl{UTSGADGETHOSTIF,pfnGadgetAdd}
 */
static DECLCALLBACK(int) utsGadgetHostUsbIpGadgetAdd(PUTSGADGETHOSTTYPEINT pIf, UTSGADGET hGadget)
{
    /* Nothing to do so far. */
    RT_NOREF2(pIf, hGadget);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{UTSGADGETHOSTIF,pfnGadgetRemove}
 */
static DECLCALLBACK(int) utsGadgetHostUsbIpGadgetRemove(PUTSGADGETHOSTTYPEINT pIf, UTSGADGET hGadget)
{
    /* Nothing to do so far. */
    RT_NOREF2(pIf, hGadget);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{UTSGADGETHOSTIF,pfnGadgetConnect}
 */
static DECLCALLBACK(int) utsGadgetHostUsbIpGadgetConnect(PUTSGADGETHOSTTYPEINT pIf, UTSGADGET hGadget)
{
    return usbGadgetHostUsbIpBindUnbind(pIf, hGadget, true /* fBind */);
}


/**
 * @interface_method_impl{UTSGADGETHOSTIF,pfnGadgetDisconnect}
 */
static DECLCALLBACK(int) utsGadgetHostUsbIpGadgetDisconnect(PUTSGADGETHOSTTYPEINT pIf, UTSGADGET hGadget)
{
    return usbGadgetHostUsbIpBindUnbind(pIf, hGadget, false /* fBind */);
}



/**
 * The gadget host interface callback table.
 */
const UTSGADGETHOSTIF g_UtsGadgetHostIfUsbIp =
{
    /** enmType */
    UTSGADGETHOSTTYPE_USBIP,
    /** pszDesc */
    "UTS USB/IP gadget host",
    /** cbIf */
    sizeof(UTSGADGETHOSTTYPEINT),
    /** pfnInit */
    utsGadgetHostUsbIpInit,
    /** pfnTerm */
    utsGadgetHostUsbIpTerm,
    /** pfnGadgetAdd */
    utsGadgetHostUsbIpGadgetAdd,
    /** pfnGadgetRemove */
    utsGadgetHostUsbIpGadgetRemove,
    /** pfnGadgetConnect */
    utsGadgetHostUsbIpGadgetConnect,
    /** pfnGadgetDisconnect */
    utsGadgetHostUsbIpGadgetDisconnect
};
