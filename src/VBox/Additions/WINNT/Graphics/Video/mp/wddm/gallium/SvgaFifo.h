/* $Id: SvgaFifo.h 110023 2025-06-27 16:06:42Z vitali.pelenjow@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver VMSVGA FIFO operations.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_SvgaFifo_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_SvgaFifo_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "Svga.h"

NTSTATUS SvgaFifoInit(PVBOXWDDM_EXT_VMSVGA pSvga);

void *SvgaFifoReserve(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbReserve);
void SvgaFifoCommit(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbActual);

NTSTATUS SvgaCmdBufInit(PVBOXWDDM_EXT_VMSVGA pSvga);
NTSTATUS SvgaCmdBufDestroy(PVBOXWDDM_EXT_VMSVGA pSvga);

NTSTATUS SvgaCmdBufDeviceCommand(PVBOXWDDM_EXT_VMSVGA pSvga, void const *pvCmd, uint32_t cbCmd);
NTSTATUS SvgaCmdBufSubmitMiniportCommand(PVBOXWDDM_EXT_VMSVGA pSvga, void const *pvCmd, uint32_t cbCmd);

void *SvgaCmdBuf3dCmdReserve(PVBOXWDDM_EXT_VMSVGA pSvga, SVGAFifo3dCmdId enmCmd, uint32_t cbReserve, uint32_t idDXContext);
void *SvgaCmdBufFifoCmdReserve(PVBOXWDDM_EXT_VMSVGA pSvga, SVGAFifoCmdId enmCmd, uint32_t cbReserve);
void *SvgaCmdBufReserve(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbReserve, uint32_t idDXContext);
void  SvgaCmdBufCommit(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbActual);
void  SvgaCmdBufCommitWithCompletionCallback(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbActual, PFNCBCOMPLETION pfn, void const *pv, uint32_t cb);
void  SvgaCmdBufFlush(PVBOXWDDM_EXT_VMSVGA pSvga);
void  SvgaCmdBufProcess(PVBOXWDDM_EXT_VMSVGA pSvga);
bool SvgaCmdBufIsIdle(PVBOXWDDM_EXT_VMSVGA pSvga);

NTSTATUS SvgaCmdBufAllocUMD(PVBOXWDDM_EXT_VMSVGA pSvga, PHYSICAL_ADDRESS DmaBufferPhysicalAddress,
                            uint32_t cbBuffer, uint32_t cbCommands, uint32_t idDXContext, PVMSVGACB *ppCB);
NTSTATUS SvgaCmdBufSubmitUMD(PVBOXWDDM_EXT_VMSVGA pSvga, PVMSVGACB pCB);

DECLINLINE(void *) SvgaReserve(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbReserve, uint32_t idDXContext = SVGA3D_INVALID_ID)
{
    if (pSvga->pCBState)
        return SvgaCmdBufReserve(pSvga, cbReserve, idDXContext);
    return SvgaFifoReserve(pSvga, cbReserve);
}

DECLINLINE(void) SvgaCommit(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t cbActual)
{
    if (pSvga->pCBState)
    {
        SvgaCmdBufCommit(pSvga, cbActual);
        return;
    }
    SvgaFifoCommit(pSvga, cbActual);
}

DECLINLINE(void) SvgaFlush(PVBOXWDDM_EXT_VMSVGA pSvga)
{
    if (pSvga->pCBState)
        SvgaCmdBufFlush(pSvga);
}


#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_SvgaFifo_h */
