/* $Id: VBoxMPVdma.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox WDDM Miniport driver
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPVdma_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPVdma_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/asm.h>
#include <VBoxVideo.h>
#include <HGSMI.h>

typedef struct _VBOXMP_DEVEXT *PVBOXMP_DEVEXT;

/* DMA commands are currently submitted over HGSMI */
typedef struct VBOXVDMAINFO
{
    BOOL      fEnabled;
} VBOXVDMAINFO, *PVBOXVDMAINFO;

int vboxVdmaCreate (PVBOXMP_DEVEXT pDevExt, VBOXVDMAINFO *pInfo
        );
int vboxVdmaDisable(PVBOXMP_DEVEXT pDevExt, PVBOXVDMAINFO pInfo);
int vboxVdmaEnable(PVBOXMP_DEVEXT pDevExt, PVBOXVDMAINFO pInfo);
int vboxVdmaDestroy(PVBOXMP_DEVEXT pDevExt, PVBOXVDMAINFO pInfo);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPVdma_h */
