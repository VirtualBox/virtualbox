/* $Id: VBoxMimicry.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxMimicry.h - Debug and logging routines implemented by VBoxDebugLib.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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


#ifndef __VBOXMIMICRY_H__
#define __VBOXMIMICRY_H__
#include <Uefi.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>

#define MIMICRY_INTERFACE_COUNT 1
#define DO_9_FAKE_DECL(name)        \
static EFI_STATUS name ## _fake_impl0();  \
static EFI_STATUS name ## _fake_impl1();  \
static EFI_STATUS name ## _fake_impl2();  \
static EFI_STATUS name ## _fake_impl3();  \
static EFI_STATUS name ## _fake_impl4();  \
static EFI_STATUS name ## _fake_impl5();  \
static EFI_STATUS name ## _fake_impl6();  \
static EFI_STATUS name ## _fake_impl7();  \
static EFI_STATUS name ## _fake_impl8();  \
static EFI_STATUS name ## _fake_impl9();

#define FAKE_IMPL(name, guid)                                       \
static EFI_STATUS name ()                                           \
{                                                                   \
    DEBUG((DEBUG_INFO, #name ": of %g called\n", &guid));           \
    return EFI_SUCCESS;                                             \
}

typedef struct
{
    EFI_HANDLE hImage;
} VBOXMIMICRY, *PVBOXMIMICRY;

PVBOXMIMICRY gThis;

EFI_STATUS install_mimic_interfaces();
EFI_STATUS uninstall_mimic_interfaces();
#endif
