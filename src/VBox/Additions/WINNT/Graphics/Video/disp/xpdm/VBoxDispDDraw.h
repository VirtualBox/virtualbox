/* $Id: VBoxDispDDraw.h 108837 2025-03-20 12:48:42Z andreas.loeffler@oracle.com $ */
/** @file
 * VBox XPDM Display driver, direct draw callbacks
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispDDraw_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispDDraw_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <winddi.h>

DWORD APIENTRY VBoxDispDDCanCreateSurface(PDD_CANCREATESURFACEDATA lpCanCreateSurface);
DWORD APIENTRY VBoxDispDDCreateSurface(PDD_CREATESURFACEDATA lpCreateSurface);
DWORD APIENTRY VBoxDispDDDestroySurface(PDD_DESTROYSURFACEDATA lpDestroySurface);
DWORD APIENTRY VBoxDispDDLock(PDD_LOCKDATA lpLock);
DWORD APIENTRY VBoxDispDDUnlock(PDD_UNLOCKDATA lpUnlock);
DWORD APIENTRY VBoxDispDDMapMemory(PDD_MAPMEMORYDATA lpMapMemory);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispDDraw_h */
