/* $Id: VBoxIPC.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

int                VBoxIPCInit    (const VBOXSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread);
unsigned __stdcall VBoxIPCWorker  (void *pInstance);
void               VBoxIPCStop    (const VBOXSERVICEENV *pEnv, void *pInstance);
void               VBoxIPCDestroy (const VBOXSERVICEENV *pEnv, void *pInstance);

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxIPC_h */
