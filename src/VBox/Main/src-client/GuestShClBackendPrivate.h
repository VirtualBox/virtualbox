/* $Id: GuestShClBackendPrivate.h 115050 2026-08-17 15:20:35Z andreas.loeffler@oracle.com $ */
/** @file
 * Main Shared Clipboard - Internal native backend operation table.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_SRC_src_client_GuestShClBackendPrivate_h
#define MAIN_INCLUDED_SRC_src_client_GuestShClBackendPrivate_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestShClBackend.h"

/** Native platform backend operations used by ShClBackend. */
typedef struct SHCLBACKENDOPS
{
    /** Initializes the platform backend. */
    int  (*pfnInit)(void);
    /** Destroys the platform backend. */
    void (*pfnDestroy)(void);
    /** Replaces the native callback table for test purposes. Optional. */
    void (*pfnSetCallbacks)(PSHCLCALLBACKS pCallbacks);
    /** Connects a Main service connection and returns its platform context. */
    int  (*pfnConnect)(GuestShClConn *pConn, PSHCLCONTEXT *ppCtx);
    /** Disconnects and destroys a platform connection context. */
    int  (*pfnDisconnect)(PSHCLCONTEXT pCtx);
    /** Reports guest formats to the native clipboard. */
    int  (*pfnReportFormats)(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats);
    /** Reads native clipboard data for the guest. */
    int  (*pfnReadData)(PSHCLCONTEXT pCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual);
    /** Writes guest clipboard data to the native clipboard. */
    int  (*pfnWriteData)(PSHCLCONTEXT pCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData);
    /** Synchronizes native clipboard state with the guest. */
    int  (*pfnSync)(PSHCLCONTEXT pCtx);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Returns callbacks for a new transfer. */
    void (*pfnTransferGetCallbacks)(PSHCLCONTEXT pCtx, PSHCLTRANSFERCALLBACKS pCallbacks);
    /** Handles a transfer status reply. */
    int  (*pfnTransferHandleStatusReply)(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer, SHCLSOURCE enmSource,
                                         SHCLTRANSFERSTATUS enmStatus, int rcStatus);
#endif
} SHCLBACKENDOPS;

/**
 * Returns the native Shared Clipboard backend operations for this host.
 *
 * @returns Immutable native backend operation table.  Never NULL.
 */
PCSHCLBACKENDOPS ShClBackendGetOps(void);

#endif /* !MAIN_INCLUDED_SRC_src_client_GuestShClBackendPrivate_h */
