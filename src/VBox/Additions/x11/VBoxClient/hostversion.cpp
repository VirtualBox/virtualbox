/* $Id: hostversion.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * X11 guest client - Host version check.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <stdio.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/ldr.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>
#ifdef VBOX_OSE
# include <VBox/version.h>
#endif

#include "VBoxClient.h"


/**
 * @interface_method_impl{VBCLSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vbclHostVerWorker(bool volatile *pfShutdown)
{
    /** @todo Move this part in VbglR3 and just provide a callback for the platform-specific
              notification stuff, since this is very similar to the VBoxTray code. */

    RT_NOREF(pfShutdown);

    LogFlowFuncEnter();

    int rc;
#ifdef VBOX_WITH_GUEST_PROPS
    uint32_t uGuestPropSvcClientID;
    rc = VbglR3GuestPropConnect(&uGuestPropSvcClientID);
    if (RT_FAILURE(rc))
    {
        VBClLogError("Cannot connect to guest property service while chcking for host version, rc = %Rrc\n", rc);
        return rc;
    }

    /* Let the main thread know that it can continue spawning services. */
    RTThreadUserSignal(RTThreadSelf());

    /* Because we need desktop notifications to be displayed, wait
     * some time to make the desktop environment load (as a work around). */
    if (g_fDaemonized)
        RTThreadSleep(RT_MS_30SEC);

    char *pszHostVersion;
    char *pszGuestVersion;
    bool  fUpdate;

    rc = VbglR3HostVersionCheckForUpdate(uGuestPropSvcClientID, &fUpdate, &pszHostVersion, &pszGuestVersion);
    if (RT_SUCCESS(rc))
    {
        if (fUpdate)
        {
            char szMsg[1024];
            char szTitle[64];

            /** @todo add some translation macros here */
            RTStrPrintf(szTitle, sizeof(szTitle), "VirtualBox Guest Additions update available!");
# ifndef VBOX_OSE
            RTStrPrintf(szMsg, sizeof(szMsg), "Your guest is currently running the Guest Additions version %s. "
                                              "We recommend updating to the latest version (%s) by choosing the "
                                              "install option from the Devices menu.", pszGuestVersion, pszHostVersion);
# else
/* This is the message which appears for non-Oracle builds of the
* Guest Additions.  Distributors are encouraged to customise this. */
            RTStrPrintf(szMsg, sizeof(szMsg), "Your virtual machine is currently running the Guest Additions version %s. Since you are running a version of the Guest Additions provided by the operating system you installed in the virtual machine we recommend that you update it to at least version %s using that system's update features, or alternatively that you remove this version and then install the " VBOX_VENDOR_SHORT " Guest Additions package using the install option from the Devices menu. Please consult the documentation for the operating system you are running to find out how to update or remove the current Guest Additions package.", pszGuestVersion, pszHostVersion);
# endif /* VBOX_OSE */
            rc = VBClShowNotify(szTitle, szMsg);
        }

        /* Store host version to not notify again */
        int rc2 = VbglR3HostVersionLastCheckedStore(uGuestPropSvcClientID, pszHostVersion);
        if (RT_SUCCESS(rc))
            rc = rc2;

        VbglR3GuestPropReadValueFree(pszHostVersion);
        VbglR3GuestPropReadValueFree(pszGuestVersion);
    }

    VbglR3GuestPropDisconnect(uGuestPropSvcClientID);
#else  /* !VBOX_WITH_GUEST_PROPS */
    rc = VERR_NOT_SUPPORTED;
#endif /* VBOX_WITH_GUEST_PROPS */

    return rc;
}

VBCLSERVICE g_SvcHostVersion =
{
    "hostversion",                   /* szName */
    "VirtualBox host version check", /* pszDescription */
    ".vboxclient-hostversion",       /* pszPidFilePathTemplate */
    NULL,                            /* pszUsage */
    NULL,                            /* pszOptions */
    NULL,                            /* pfnOption */
    NULL,                            /* pfnInit */
    vbclHostVerWorker,               /* pfnWorker */
    NULL,                            /* pfnStop*/
    NULL                             /* pfnTerm */
};

