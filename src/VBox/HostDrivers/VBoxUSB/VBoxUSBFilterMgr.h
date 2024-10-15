/* $Id: VBoxUSBFilterMgr.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Ring-0 USB Filter Manager.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VBoxUSB_VBoxUSBFilterMgr_h
#define VBOX_INCLUDED_SRC_VBoxUSB_VBoxUSBFilterMgr_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/usbfilter.h>

RT_C_DECLS_BEGIN

/** @todo r=bird: VBOXUSBFILTER_CONTEXT isn't following the coding
 *        guildlines. Don't know which clueless dude did this...  */
#if defined(RT_OS_WINDOWS)
typedef struct VBOXUSBFLTCTX *VBOXUSBFILTER_CONTEXT;
#define VBOXUSBFILTER_CONTEXT_NIL NULL
#else
typedef RTPROCESS VBOXUSBFILTER_CONTEXT;
#define VBOXUSBFILTER_CONTEXT_NIL NIL_RTPROCESS
#endif

int     VBoxUSBFilterInit(void);
void    VBoxUSBFilterTerm(void);
void    VBoxUSBFilterRemoveOwner(VBOXUSBFILTER_CONTEXT Owner);
int     VBoxUSBFilterAdd(PCUSBFILTER pFilter, VBOXUSBFILTER_CONTEXT Owner, uintptr_t *puId);
int     VBoxUSBFilterRemove(VBOXUSBFILTER_CONTEXT Owner, uintptr_t uId);
VBOXUSBFILTER_CONTEXT VBoxUSBFilterMatch(PCUSBFILTER pDevice, uintptr_t *puId);
VBOXUSBFILTER_CONTEXT VBoxUSBFilterMatchEx(PCUSBFILTER pDevice, uintptr_t *puId, bool fRemoveFltIfOneShot, bool *pfFilter, bool *pfIsOneShot);
VBOXUSBFILTER_CONTEXT VBoxUSBFilterGetOwner(uintptr_t uId);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_VBoxUSB_VBoxUSBFilterMgr_h */
