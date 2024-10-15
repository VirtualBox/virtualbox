/* $Id: VBoxArmPlatformLib.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxArmPlatformLib.c - Helpers for the VirtualBox virtual platform descriptor parsing.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <Base.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

#define IN_RING0
#include <VBox/platforms/vbox-armv8.h>

EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformDescGetPhysAddr (VOID)
{
    return VBOXPLATFORMARMV8_PHYS_ADDR;
}


VOID * EFIAPI VBoxArmPlatformDescGet(VOID)
{
    /* Works because of identity mappings in UEFI. */
    return (VOID *)VBoxArmPlatformDescGetPhysAddr();
}


UINTN EFIAPI VBoxArmPlatformDescSizeGet(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    return pDesc->cbDesc;
}


EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformRamBaseStartGetPhysAddr(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    return pDesc->u64PhysAddrRamBase;
}


UINTN EFIAPI VBoxArmPlatformRamBaseSizeGet(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    return pDesc->cbRamBase;
}


VOID * EFIAPI VBoxArmPlatformFdtGet(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    if (!pDesc->cbFdt)
        return NULL;

    return (VOID *)((UINTN)pDesc + pDesc->i64OffFdt);
}


UINTN EFIAPI VBoxArmPlatformFdtSizeGet(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    return pDesc->cbFdt;
}


EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformUefiRomStartGetPhysAddr(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    if (!pDesc->cbUefiRom)
        return 0;

    return (EFI_PHYSICAL_ADDRESS)((UINTN)pDesc + pDesc->i64OffUefiRom);
}


UINTN EFIAPI VBoxArmPlatformUefiRomSizeGet(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    return pDesc->cbUefiRom;
}


EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformMmioStartGetPhysAddr(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    if (!pDesc->cbMmio)
        return 0;

    return (EFI_PHYSICAL_ADDRESS)((UINTN)pDesc + pDesc->i64OffMmio);
}


UINTN EFIAPI VBoxArmPlatformMmioSizeGet(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    return pDesc->cbMmio;
}


EFI_PHYSICAL_ADDRESS EFIAPI VBoxArmPlatformMmio32StartGetPhysAddr(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    if (!pDesc->cbMmio)
        return 0;

    return (EFI_PHYSICAL_ADDRESS)((UINTN)pDesc + pDesc->i64OffMmio32);
}


UINTN EFIAPI VBoxArmPlatformMmio32SizeGet(VOID)
{
    PCVBOXPLATFORMARMV8 pDesc = (PCVBOXPLATFORMARMV8)VBoxArmPlatformDescGet();
    ASSERT(pDesc->u32Magic == VBOXPLATFORMARMV8_MAGIC);

    return pDesc->cbMmio32;
}
