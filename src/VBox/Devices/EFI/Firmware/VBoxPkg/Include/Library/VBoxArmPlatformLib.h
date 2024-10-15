/* $Id: VBoxArmPlatformLib.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxArmPlatformLib.h - Helpers for the virtual ARM platform of VirtualBox.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef ___VBoxPkg_VBoxArmPlatformLib_h
#define ___VBoxPkg_VBoxArmPlatformLib_h

#include <Base.h>

/**
 * Returns the physical address of the VBox ARM platform descriptor region.
 *
 * @returns Physical address of the VirtualBox ARM platform descriptor region.
 */
EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformDescGetPhysAddr(VOID);


/**
 * Returns a pointer to the VBox ARM platform descriptor region.
 *
 * @returns Poitner to the VirtualBox ARM platform descriptor region.
 */
VOID *EFIAPI VBoxArmPlatformDescGet(VOID);


/**
 * Returns the size of the VBox ARM platform descriptor region in bytes.
 *
 * @returns Size of the VirtualBox ARM platform descriptor region in bytes.
 */
UINTN EFIAPI VBoxArmPlatformDescSizeGet(VOID);


/**
 * Returns the physical address of the base RAM region.
 *
 * @returns Physical address of the base RAM region.
 */
EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformRamBaseStartGetPhysAddr(VOID);


/**
 * Returns the size of the base RAM region.
 *
 * @returns Size of the base RAM region.
 */
UINTN EFIAPI VBoxArmPlatformRamBaseSizeGet(VOID);


/**
 * Returns the pointer to the FDT if available.
 *
 * @returns Pointer to the FDT or NULL if not available.
 */
VOID *EFIAPI VBoxArmPlatformFdtGet(VOID);


/**
 * Returns the size of the FDT region (not necessarily the size of the FDT itself).
 *
 * @returns Size of the FDT region.
 */
UINTN EFIAPI VBoxArmPlatformFdtSizeGet(VOID);


/**
 * Returns the physical address of the start of the UEFI ROM region.
 *
 * @returns Physical address of the UEFI ROM region start.
 */
EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformUefiRomStartGetPhysAddr(VOID);


/**
 * Returns the size of the UEFI ROM region (not necessarily the size of the ROM itself).
 *
 * @returns Size of the UEFI ROM region.
 */
UINTN EFIAPI VBoxArmPlatformUefiRomSizeGet(VOID);


/**
 * Returns the physical address of the start of the MMIO region.
 *
 * @returns Physical address of the MMIO region start.
 */
EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformMmioStartGetPhysAddr(VOID);


/**
 * Returns the size of the MMIO region.
 *
 * @returns Size of the MMIO region.
 */
UINTN EFIAPI VBoxArmPlatformMmioSizeGet(VOID);


/**
 * Returns the physical address of the start of the MMIO32 region.
 *
 * @returns Physical address of the MMIO32 region start.
 */
EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformMmio32StartGetPhysAddr(VOID);


/**
 * Returns the size of the MMIO32 region.
 *
 * @returns Size of the MMIO32 region.
 */
UINTN EFIAPI VBoxArmPlatformMmio32SizeGet(VOID);

#endif

