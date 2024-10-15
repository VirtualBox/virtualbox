/* $Id: VBoxSharedClipboardSvc-x11-stubs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $*/
/** @file
 * Shared Clipboard Service - Linux host, a stub version with no functionality for use on headless hosts.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/alloc.h>
#include <iprt/asm.h>        /* For atomic operations */
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "VBoxSharedClipboardSvc-internal.h"


/*
 * Initialise the host side of the shared clipboard - called by the hgcm layer.
 */
int ShClBackendInit(PSHCLBACKEND pBackend, VBOXHGCMSVCFNTABLE *pTable)
{
    RT_NOREF(pTable);
    LogFlowFunc(("called, returning VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}

/*
 * Terminate the host side of the shared clipboard - called by the hgcm layer.
 */
void ShClBackendDestroy(PSHCLBACKEND pBackend)
{
    RT_NOREF(pBackend);
    LogFlowFunc(("called, returning\n"));
}

int ShClBackendConnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, bool fHeadless)
{
    RT_NOREF(pBackend, pClient, fHeadless);
    LogFlowFunc(("called, returning VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}

/*
 * Synchronise the contents of the host clipboard with the guest, called by the HGCM layer
 * after a save and restore of the guest.
 */
int ShClBackendSync(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend, pClient);
    LogFlowFunc(("called, returning VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}

int ShClBackendDisconnect(PSHCLBACKEND pBackend, PSHCLCLIENT pClient)
{
    RT_NOREF(pBackend, pClient);
    return VINF_SUCCESS;
}

/*
 * The guest is taking possession of the shared clipboard.
 * Called by the HGCM clipboard subsystem.
 */
int ShClBackendReportFormats(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    RT_NOREF(pBackend, pClient, fFormats);
    return VINF_SUCCESS;
}

/*
 * Called by the HGCM clipboard subsystem when the guest wants to read the host clipboard.
 */
int ShClBackendReadData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx,
                        SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    RT_NOREF(pBackend, pClient, pCmdCtx, uFormat, pvData, cbData);

    /* No data available. */
    *pcbActual = 0;

    return VINF_SUCCESS;
}

int ShClBackendWriteData(PSHCLBACKEND pBackend, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx,
                         SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    RT_NOREF(pBackend, pClient, pCmdCtx, uFormat, pvData, cbData);
    return VERR_NOT_IMPLEMENTED;
}

