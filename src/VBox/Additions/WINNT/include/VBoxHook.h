/* $Id: VBoxHook.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxHook -- Global windows hook dll.
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

#ifndef GA_INCLUDED_WINNT_VBoxHook_h
#define GA_INCLUDED_WINNT_VBoxHook_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* custom messages as we must install the hook from the main thread */
/** @todo r=andy Use WM_APP + n offsets here! */
#define WM_VBOX_SEAMLESS_ENABLE                     0x2001
#define WM_VBOX_SEAMLESS_DISABLE                    0x2002
#define WM_VBOX_SEAMLESS_UPDATE                     0x2003
#define WM_VBOX_GRAPHICS_SUPPORTED                  0x2004
#define WM_VBOX_GRAPHICS_UNSUPPORTED                0x2005


#define VBOXHOOK_DLL_NAME              "VBoxHook.dll"
#define VBOXHOOK_GLOBAL_DT_EVENT_NAME  "Local\\VBoxHookDtNotifyEvent"
#define VBOXHOOK_GLOBAL_WT_EVENT_NAME  "Local\\VBoxHookWtNotifyEvent"

BOOL VBoxHookInstallActiveDesktopTracker(HMODULE hDll);
BOOL VBoxHookRemoveActiveDesktopTracker();

BOOL VBoxHookInstallWindowTracker(HMODULE hDll);
BOOL VBoxHookRemoveWindowTracker();

#endif /* !GA_INCLUDED_WINNT_VBoxHook_h */

