/* $Id: VBoxHostVersion.cpp 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxHostVersion - Checks the host's VirtualBox version and notifies
 *                   the user in case of an update.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>

#include "VBoxHostVersion.h"
#include "VBoxTray.h"
#include "VBoxTrayInternal.h"
#include "VBoxHelpers.h"


/** @todo Move this part in VbglR3 and just provide a callback for the platform-specific
          notification stuff, since this is very similar to the VBoxClient code. */
int VBoxCheckHostVersion(void)
{
    int rc;
    uint32_t uGuestPropSvcClientID;

    rc = VbglR3GuestPropConnect(&uGuestPropSvcClientID);
    if (RT_SUCCESS(rc))
    {
        char *pszHostVersion;
        char *pszGuestVersion;
        bool fUpdate;
        rc = VbglR3HostVersionCheckForUpdate(uGuestPropSvcClientID, &fUpdate, &pszHostVersion, &pszGuestVersion);
        if (RT_SUCCESS(rc))
        {
            if (fUpdate)
            {
                char szMsg[256]; /* Sizes according to MSDN. */
                char szTitle[64];

                /** @todo Add some translation macros here. */
                RTStrPrintf(szTitle, sizeof(szTitle), "VirtualBox Guest Additions update available!");
                RTStrPrintf(szMsg, sizeof(szMsg),
                            "Your guest is currently running the Guest Additions version %s. "
                            "We recommend updating to the latest version (%s) by choosing the "
                            "install option from the Devices menu.", pszGuestVersion, pszHostVersion);

                rc = VBoxTrayHlpShowBalloonTip(szMsg, szTitle, RT_MS_5SEC);

                VBoxTrayInfo("Guest Additions update found: %s -> %s\n", pszGuestVersion, pszHostVersion);
            }

            /* Store host version to not notify again. */
            rc = VbglR3HostVersionLastCheckedStore(uGuestPropSvcClientID, pszHostVersion);

            VbglR3GuestPropReadValueFree(pszHostVersion);
            VbglR3GuestPropReadValueFree(pszGuestVersion);
        }
        VbglR3GuestPropDisconnect(uGuestPropSvcClientID);
    }
    return rc;
}

