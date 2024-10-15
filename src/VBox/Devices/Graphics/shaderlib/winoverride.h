/* $Id: winoverride.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevVMWare/Shaderlib - Wine Function Portability Overrides
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_shaderlib_winoverride_h
#define VBOX_INCLUDED_SRC_Graphics_shaderlib_winoverride_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define GetProcessHeap()    ((HANDLE)0)
#define HeapAlloc           VBoxHeapAlloc
#define HeapFree            VBoxHeapFree
#define HeapReAlloc         VBoxHeapReAlloc
#define DebugBreak          VBoxDebugBreak

LPVOID      WINAPI VBoxHeapAlloc(HANDLE hHeap, DWORD heaptype, SIZE_T size);
BOOL        WINAPI VBoxHeapFree(HANDLE hHeap, DWORD heaptype, LPVOID ptr);
LPVOID      WINAPI VBoxHeapReAlloc(HANDLE hHeap,DWORD heaptype, LPVOID ptr, SIZE_T size);
void VBoxDebugBreak(void);

#endif /* !VBOX_INCLUDED_SRC_Graphics_shaderlib_winoverride_h */

