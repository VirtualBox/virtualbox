/* $Id: VBoxMPLegacy.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface for WDDM kernel mode driver.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPLegacy_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPLegacy_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "common/VBoxMPDevExt.h"

NTSTATUS APIENTRY DxgkDdiBuildPagingBufferLegacy(CONST HANDLE hAdapter,
                                                 DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer);
NTSTATUS APIENTRY DxgkDdiPresentLegacy(CONST HANDLE hContext,
                                       DXGKARG_PRESENT *pPresent);
NTSTATUS APIENTRY DxgkDdiRenderLegacy(CONST HANDLE hContext,
                                      DXGKARG_RENDER *pRender);
NTSTATUS APIENTRY DxgkDdiPatchLegacy(CONST HANDLE hAdapter,
                                     CONST DXGKARG_PATCH *pPatch);
NTSTATUS APIENTRY DxgkDdiSubmitCommandLegacy(CONST HANDLE hAdapter,
                                             CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand);
NTSTATUS APIENTRY DxgkDdiPreemptCommandLegacy(CONST HANDLE hAdapter,
                                              CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand);
NTSTATUS APIENTRY DxgkDdiQueryCurrentFenceLegacy(CONST HANDLE hAdapter,
                                                 DXGKARG_QUERYCURRENTFENCE *pCurrentFence);
BOOLEAN DxgkDdiInterruptRoutineLegacy(CONST PVOID MiniportDeviceContext,
                                      ULONG MessageNumber);
VOID DxgkDdiDpcRoutineLegacy(CONST PVOID MiniportDeviceContext);

VOID vboxVdmaDdiNodesInit(PVBOXMP_DEVEXT pDevExt);

NTSTATUS vboxVdmaGgDmaBltPerform(PVBOXMP_DEVEXT pDevExt, struct VBOXWDDM_ALLOC_DATA *pSrcAlloc, RECT *pSrcRect,
                                 struct VBOXWDDM_ALLOC_DATA *pDstAlloc, RECT *pDstRect);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPLegacy_h */
