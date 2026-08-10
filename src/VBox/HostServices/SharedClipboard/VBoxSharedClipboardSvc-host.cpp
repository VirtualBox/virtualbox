/* $Id: VBoxSharedClipboardSvc-host.cpp 114971 2026-08-10 17:29:14Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Host-controlled service handling.
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
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/HostServices/Service.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/assert.h>
#include <iprt/critsect.h>

#include "VBoxSharedClipboardSvc-internal.h"
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include "VBoxSharedClipboardSvc-transfers.h"
#endif

using namespace HGCM;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void shClSvcHostReset(void);


int shClSvcHostModeSet(uint32_t uMode)
{
    int rc = VERR_NOT_SUPPORTED;

    switch (uMode)
    {
        case VBOX_SHCL_MODE_OFF:
            RT_FALL_THROUGH();
        case VBOX_SHCL_MODE_HOST_TO_GUEST:
            RT_FALL_THROUGH();
        case VBOX_SHCL_MODE_GUEST_TO_HOST:
            RT_FALL_THROUGH();
        case VBOX_SHCL_MODE_BIDIRECTIONAL:
        {
            g_uMode = uMode;

            rc = VINF_SUCCESS;
            break;
        }

        default:
        {
            g_uMode = VBOX_SHCL_MODE_OFF;
            break;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Resets host-side live Shared Clipboard state.
 */
static void shClSvcHostReset(void)
{
    int rc = RTCritSectEnter(&g_CritSect);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        return;

    for (ClipboardClientMap::iterator itClient = g_mapClients.begin(); itClient != g_mapClients.end(); ++itClient)
        if (itClient->second)
            shClSvcClientReset(itClient->second);

    g_ExtState.fReadingData = false;
    g_ExtState.fDelayedAnnouncement = false;
    g_ExtState.fDelayedFormats = 0;

    RTCritSectLeave(&g_CritSect);
}


/*
 * We differentiate between a function handler for the guest and one for the host.
 */
DECLCALLBACK(int) shClSvcHostCall(void *, uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("u32Function=%RU32 (%s), cParms=%RU32, paParms=%p\n",
                 u32Function, ShClHostFunctionToStr(u32Function), cParms, paParms));

    switch (u32Function)
    {
        case VBOX_SHCL_HOST_FN_SET_MODE:
        {
            if (cParms != 1)
                rc = VERR_INVALID_PARAMETER;
            else
            {
                uint32_t u32Mode = VBOX_SHCL_MODE_OFF;

                rc = HGCMSvcGetU32(&paParms[0], &u32Mode);
                if (RT_SUCCESS(rc))
                    rc = shClSvcHostModeSet(u32Mode);
            }
            break;
        }

        case VBOX_SHCL_HOST_FN_SET_HEADLESS:
            rc = VERR_NOT_IMPLEMENTED;
            break;

        case VBOX_SHCL_HOST_FN_CANCEL:
        {
            if (cParms == 0)
            {
                shClSvcHostReset();
                rc = VINF_SUCCESS;
            }
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            else
                rc = ShClSvcTransferMsgHostHandler(u32Function, cParms, paParms);
#else
            else
                rc = VERR_INVALID_PARAMETER;
#endif
            break;
        }

        default:
        {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            rc = ShClSvcTransferMsgHostHandler(u32Function, cParms, paParms);
#else
            rc = VERR_NOT_IMPLEMENTED;
#endif
            break;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}
