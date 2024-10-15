/* $Id: VBoxSysTables.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSysTables.c - VirtualBox system tables
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/DevicePathToText.h>

#include <IndustryStandard/Acpi10.h>
#include <IndustryStandard/Acpi20.h>
#include <IndustryStandard/SmBios.h>

#include <Guid/SmBios.h>
#include <Guid/Acpi.h>
#include <Guid/Mps.h>

#include "VBoxPkg.h"
#include "DevEFI.h"
#include "iprt/asm.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


EFI_STATUS EFIAPI
ConvertSystemTable (
    IN     EFI_GUID        *TableGuid,
    IN OUT VOID            **Table
                    );

#define MPS_PTR           SIGNATURE_32('_','M','P','_')
#define SMBIOS_PTR        SIGNATURE_32('_','S','M','_')

#define EBDA_BASE (0x9FC0 << 4)

VOID *
FindSMBIOSPtr (
  VOID
  )
{
  UINTN                           Address;

  //
  // First Search 0x0e0000 - 0x0fffff for SMBIOS Ptr
  //
  for (Address = 0xe0000; Address < 0xfffff; Address += 0x10) {
    if (*(UINT32 *)(Address) == SMBIOS_PTR) {
      return (VOID *)Address;
    }
  }
  return NULL;
}

VOID *
FindMPSPtr (
  VOID
  )
{
  UINTN                           Address;
  UINTN                           Index;

  //
  // First Search 0x0e0000 - 0x0fffff for MPS Ptr
  //
  for (Address = 0xe0000; Address < 0xfffff; Address += 0x10) {
    if (*(UINT32 *)(Address) == MPS_PTR) {
      return (VOID *)Address;
    }
  }

  //
  // Search EBDA
  //

  Address = EBDA_BASE;
  for (Index = 0; Index < 0x400 ; Index += 16) {
    if (*(UINT32 *)(Address + Index) == MPS_PTR) {
      return (VOID *)(Address + Index);
    }
  }
  return NULL;
}

EFI_STATUS EFIAPI
ConvertAndInstallTable(EFI_GUID* Guid, VOID* Ptr)
{
    EFI_STATUS  rc = EFI_SUCCESS;

    rc = ConvertSystemTable(Guid, &Ptr);
    //ASSERT_EFI_ERROR (rc);

    rc = gBS->InstallConfigurationTable(Guid, Ptr);
    ASSERT_EFI_ERROR (rc);

    return rc;
}


/**
 * VBoxSysTablesDxe entry point.
 *
 * @returns EFI status code.
 *
 * @param   ImageHandle     The image handle.
 * @param   SystemTable     The system table pointer.
 */
EFI_STATUS EFIAPI
DxeInitializeVBoxSysTables(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS  rc;
    VOID*       Ptr;

    DEBUG((DEBUG_INFO, "DxeInitializeVBoxSysTables\n"));

    Ptr = FindSMBIOSPtr();
    DEBUG((DEBUG_INFO, "SMBIOS=%p\n", Ptr));
    ASSERT(Ptr != NULL);
    if (Ptr)
    {
        rc = ConvertAndInstallTable(&gEfiSmbiosTableGuid, Ptr);
        ASSERT_EFI_ERROR (rc);
    }

    Ptr = FindMPSPtr();
    DEBUG((DEBUG_INFO, "MPS=%p\n", Ptr));
    // MPS can be null in non IO-APIC configs
    if (Ptr)
        rc = ConvertAndInstallTable(&gEfiMpsTableGuid, Ptr);

    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
DxeUninitializeVBoxSysTables(IN EFI_HANDLE         ImageHandle)
{
    return EFI_SUCCESS;
}
