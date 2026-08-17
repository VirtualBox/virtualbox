/* $Id: clipboard-transfers-http.h 115048 2026-08-17 15:07:54Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard - HTTP transfer functions.
 */

/*
 * Copyright (C) 2020-2026 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_GuestHost_clipboard_transfers_http_h
#define VBOX_INCLUDED_GuestHost_clipboard_transfers_http_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/GuestHost/SharedClipboard-transfers.h>

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
/** Namespace used as a prefix for HTTP(S) transfer URLs. */
# define SHCL_HTTPT_URL_NAMESPACE "vbcl"

/** @name Shared Clipboard HTTP context API.
 *  @{
 */
int ShClTransferHttpServerMaybeStart(PSHCLHTTPCONTEXT pCtx);
int ShClTransferHttpServerMaybeStop(PSHCLHTTPCONTEXT pCtx);
/** @} */

/** @name Shared Clipboard HTTP server API.
 *  @{
 */
int ShClTransferHttpServerInit(PSHCLHTTPSERVER pSrv);
int ShClTransferHttpServerDestroy(PSHCLHTTPSERVER pSrv);
int ShClTransferHttpServerStart(PSHCLHTTPSERVER pSrv, unsigned cMaxAttempts, uint16_t *puPort);
int ShClTransferHttpServerStartEx(PSHCLHTTPSERVER pSrv, uint16_t uPort);
int ShClTransferHttpServerStop(PSHCLHTTPSERVER pSrv);
int ShClTransferHttpServerRegisterTransfer(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer);
int ShClTransferHttpServerUnregisterTransfer(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer);
bool ShClTransferHttpServerGetTransfer(PSHCLHTTPSERVER pSrv, SHCLTRANSFERID idTransfer);
uint32_t ShClTransferHttpServerGetTransferCount(PSHCLHTTPSERVER pSrv);
char *ShClTransferHttpServerGetAddressA(PSHCLHTTPSERVER pSrv);
char *ShClTransferHttpServerGetUrlA(PSHCLHTTPSERVER pSrv, SHCLTRANSFERID idTransfer, uint64_t idxEntry);
int ShClTransferHttpConvertToStringList(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer, char **ppszData, size_t *pcbData);
bool ShClTransferHttpServerIsInitialized(PSHCLHTTPSERVER pSrv);
bool ShClTransferHttpServerIsRunning(PSHCLHTTPSERVER pSrv);
/** @} */
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */

#endif /* !VBOX_INCLUDED_GuestHost_clipboard_transfers_http_h */
