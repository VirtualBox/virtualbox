/* $Id: VBoxVRDP.h 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxVRDP - VRDP notification
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

#ifndef GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxVRDP_h
#define GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxVRDP_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* The restore service prototypes. */
int                VBoxVRDPInit    (const VBOXTRAYSVCENV *pEnv, void **ppInstance, bool *pfStartThread);
unsigned __stdcall VBoxVRDPThread  (void *pInstance);
void               VBoxVRDPDestroy (const VBOXTRAYSVCENV *pEnv, void *pInstance);

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxVRDP_h */
