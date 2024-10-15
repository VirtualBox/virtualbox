/* $Id: VBoxGaHwSVGA.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface.
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

#ifndef GA_INCLUDED_3D_WIN_VBoxGaHwSVGA_h
#define GA_INCLUDED_3D_WIN_VBoxGaHwSVGA_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

/*
 * VBOXGAHWINFOSVGA contains information about the virtual hardware, which is neede dy the user mode
 * Gallium driver. The driver can not query the info at the initialization time, therefore
 * we send the complete info to the driver.
 *
 * Since both FIFO and SVGA_REG_ are expanded over time, we reserve some space.
 * The Gallium user mode driver can figure out which part of au32Regs and au32Fifo
 * is valid from the raw data.
 *
 * VBOXGAHWINFOSVGA struct goes both to 32 and 64 bit user mode binaries, take care of alignment.
 */
#pragma pack(1)
typedef struct VBOXGAHWINFOSVGA
{
    uint32_t cbInfoSVGA;

    /* Copy of SVGA_REG_*, up to 256, currently 58 are used. */
    uint32_t au32Regs[256];

    /* Copy of FIFO registers, up to 1024, currently 290 are used. */
    uint32_t au32Fifo[1024];

    /* Currently SVGA has 260 caps, 512 should be ok for near future.
     * This is a copy of SVGA3D_DEVCAP_* values returned by the host.
     * Only valid if SVGA_CAP_GBOBJECTS is set in SVGA_REG_CAPABILITIES.
     */
    uint32_t au32Caps[512];
} VBOXGAHWINFOSVGA;
#pragma pack()

#endif /* !GA_INCLUDED_3D_WIN_VBoxGaHwSVGA_h */
