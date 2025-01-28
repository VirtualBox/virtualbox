/* $Id: VBoxIPC.h 108105 2025-01-28 16:08:20Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxIPC - IPC thread, acts as a (purely) local IPC server.
 *           Multiple sessions are supported, whereas every session
 *           has its own thread for processing requests.
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

#ifndef GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxIPC_h
#define GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxIPC_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

int                vbtrIPCInit    (const VBOXTRAYSVCENV *pEnv, void **ppvInstance, bool *pfStartThread);
unsigned __stdcall vbtrIPCWorker  (void *pvInstance);
void               VBoxIPCStop    (const VBOXTRAYSVCENV *pEnv, void *pvInstance);
void               vbtrIPCDestroy (const VBOXTRAYSVCENV *pEnv, void *pvInstance);

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxIPC_h */
