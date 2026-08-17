/* $Id: VBoxSharedClipboardSvc-host.cpp 115054 2026-08-17 16:27:08Z andreas.loeffler@oracle.com $ */
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

#include "VBoxSharedClipboardSvc-internal.h"
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include "VBoxSharedClipboardSvc-transfers.h"
#endif

using namespace HGCM;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void shClSvcHostReset(void);


/**
 * Sets the host-controlled Shared Clipboard mode.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if @a uMode is not a VBOX_SHCL_MODE_XXX value.
 * @param   uMode               New VBOX_SHCL_MODE_XXX value.
 *
 * Invalid values fail closed by switching the effective mode to
 * VBOX_SHCL_MODE_OFF.
 */
int shClSvcHostModeSet(uint32_t uMode)
{
    int rc = VERR_NOT_SUPPORTED;
    uint32_t uModeNew = VBOX_SHCL_MODE_OFF;

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
            uModeNew = uMode;
            rc = VINF_SUCCESS;
            break;
        }

        default:
            break;
    }

    shClSvcLock();
    ASMAtomicWriteU32(&g_ShClSvc.uMode, uModeNew);
    if (g_ShClSvc.pActiveClient)
    {
        ShClSvcClientLock(g_ShClSvc.pActiveClient);
        ASMAtomicWriteU32(&g_ShClSvc.pActiveClient->State.uMode, uModeNew);
        ShClSvcClientUnlock(g_ShClSvc.pActiveClient);
    }
    shClSvcUnlock();

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Resets host-side live Shared Clipboard state.
 */
static void shClSvcHostReset(void)
{
    shClSvcLock();

    PSHCLCLIENT const pClient = g_ShClSvc.pActiveClient;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RTLISTANCHOR ListDestroy;
    RTListInit(&ListDestroy);

    /* Keep the client stable while claiming its transfers, but do not run
     * platform callbacks or wait for transfer users under the service lock. */
    if (pClient)
        shClSvcTransferDetachAll(pClient, &ListDestroy);

    shClSvcUnlock();

    shClSvcTransferDestroyDetachedAll(&ListDestroy);

    shClSvcLock();
#endif

    if (   pClient
        && g_ShClSvc.pActiveClient == pClient)
        shClSvcClientReset(pClient);

    shClSvcUnlock();
}


/*
 * We differentiate between a function handler for the guest and one for the host.
 */
DECLCALLBACK(int) shClSvcHostCall(void *, uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("u32Function=%RU32 (%s), cParms=%RU32, paParms=%p\n",
                 u32Function, ShClSvcHostFunctionToStr(u32Function), cParms, paParms));

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
