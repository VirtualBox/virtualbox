/* $Id: SvgaHw.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver VMSVGA hardware access helpers.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_SvgaHw_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_SvgaHw_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "Svga.h"

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>

DECLINLINE(RTIOPORT) SVGAPort(PVBOXWDDM_EXT_VMSVGA pSvga, uint16_t u16Offset)
{
    return pSvga->ioportBase + u16Offset;
}

DECLINLINE(void) SVGAPortWrite(PVBOXWDDM_EXT_VMSVGA pSvga, uint16_t u16Offset, uint32_t u32Value)
{
    ASMOutU32(SVGAPort(pSvga, u16Offset), u32Value);
}

DECLINLINE(uint32_t) SVGAPortRead(PVBOXWDDM_EXT_VMSVGA pSvga, uint16_t u16Offset)
{
    const uint32_t u32Value = ASMInU32(SVGAPort(pSvga, u16Offset));
    return u32Value;
}

DECLINLINE(void) SVGARegWrite(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t u32Offset, uint32_t u32Value)
{
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSvga->HwSpinLock, &OldIrql);

    ASMOutU32(SVGAPort(pSvga, SVGA_INDEX_PORT), u32Offset);
    ASMOutU32(SVGAPort(pSvga, SVGA_VALUE_PORT), u32Value);

    KeReleaseSpinLock(&pSvga->HwSpinLock, OldIrql);
}

DECLINLINE(uint32_t) SVGARegRead(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t u32Offset)
{
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSvga->HwSpinLock, &OldIrql);

    ASMOutU32(SVGAPort(pSvga, SVGA_INDEX_PORT), u32Offset);
    const uint32_t u32Value = ASMInU32(SVGAPort(pSvga, SVGA_VALUE_PORT));

    KeReleaseSpinLock(&pSvga->HwSpinLock, OldIrql);
    return u32Value;
}

DECLINLINE(uint32_t) SVGADevCapRead(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t idx)
{
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSvga->HwSpinLock, &OldIrql);

    ASMOutU32(SVGAPort(pSvga, SVGA_INDEX_PORT), SVGA_REG_DEV_CAP);
    ASMOutU32(SVGAPort(pSvga, SVGA_VALUE_PORT), idx);
    uint32_t const u32Value = ASMInU32(SVGAPort(pSvga, SVGA_VALUE_PORT));

    KeReleaseSpinLock(&pSvga->HwSpinLock, OldIrql);
    return u32Value;
}

DECLINLINE(volatile void *) SVGAFifoPtrFromOffset(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t u32Offset)
{
    return (volatile uint8_t *)pSvga->pu32FIFO + u32Offset;
}

DECLINLINE(volatile void *) SVGAFifoPtrFromIndex(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t u32Index)
{
    return pSvga->pu32FIFO + u32Index;
}

DECLINLINE(uint32_t) SVGAFifoRead(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t u32Index)
{
    volatile uint32_t *pu32 = &pSvga->pu32FIFO[u32Index];
    return ASMAtomicReadU32(pu32);
}

DECLINLINE(void) SVGAFifoWrite(PVBOXWDDM_EXT_VMSVGA pSvga, uint32_t u32Index, uint32_t u32Value)
{
    volatile uint32_t *pu32 = &pSvga->pu32FIFO[u32Index];
    ASMAtomicWriteU32(pu32, u32Value);
    ASMCompilerBarrier();
}

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_SvgaHw_h */
