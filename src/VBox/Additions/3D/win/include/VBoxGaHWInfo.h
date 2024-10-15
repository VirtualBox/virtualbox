/* $Id: VBoxGaHWInfo.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_3D_WIN_VBoxGaHWInfo_h
#define GA_INCLUDED_3D_WIN_VBoxGaHWInfo_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>

#include <VBoxGaHwSVGA.h>

/* Gallium virtual hardware supported by the miniport. */
#define VBOX_GA_HW_TYPE_UNKNOWN 0
#define VBOX_GA_HW_TYPE_VMSVGA  1

/*
 * VBOXGAHWINFO contains information about the virtual hardware, which is passed
 * to the user mode Gallium driver. The driver can not query the info at the initialization time,
 * therefore we send the complete info to the driver.
 *
 * VBOXGAHWINFO struct goes both to 32 and 64 bit user mode binaries, take care of alignment.
 */
#pragma pack(1)
typedef struct VBOXGAHWINFO
{
    uint32_t u32HwType; /* VBOX_GA_HW_TYPE_* */
    uint32_t u32Reserved;
    union
    {
        VBOXGAHWINFOSVGA svga;
        uint8_t au8Raw[65536];
    } u;
} VBOXGAHWINFO;
#pragma pack()

AssertCompile(RT_SIZEOFMEMB(VBOXGAHWINFO, u) <= RT_SIZEOFMEMB(VBOXGAHWINFO, u.au8Raw));

#endif /* !GA_INCLUDED_3D_WIN_VBoxGaHWInfo_h */
