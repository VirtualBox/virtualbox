/* $Id: VBoxSharedClipboardSvc-transfers.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard Service - Internal header for transfer (list) handling.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_transfers_h
#define VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_transfers_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

int shClSvcTransferModeSet(uint32_t fMode);
int ShClSvcTransferCreate(PSHCLCLIENT pClient, SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource, SHCLTRANSFERID idTransfer, PSHCLTRANSFER *ppTransfer);
void ShClSvcTransferDestroy(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer);
int ShClSvcTransferInit(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer);
int ShClSvcTransferStart(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer);
int ShClSvcTransferStop(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, bool fWaitForGuest);
int ShClSvcTransferSendStatusAsync(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, SHCLTRANSFERSTATUS uStatus, int rcTransfer, PSHCLEVENT *ppEvent);
int ShClSvcTransferRootListReadFromGuest(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer);
bool shClSvcTransferMsgIsAllowed(uint32_t uMode, uint32_t uMsg);
void shClSvcTransferDestroyAll(PSHCLCLIENT pClient);

#endif /* !VBOX_INCLUDED_SRC_SharedClipboard_VBoxSharedClipboardSvc_transfers_h */

