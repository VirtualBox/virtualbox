/* $Id: TableConversion.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TableConversion.c
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

/*++

  This code is baed on:

  Copyright (c) 2006 - 2007, Intel Corporation
  All rights reserved. This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
  --*/

#include <IndustryStandard/Pci.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/SmBios.h>
#include "LegacyBiosMpTable.h"

#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>

#include <Guid/Acpi.h>
#include <Guid/SmBios.h>
#include <Guid/Mps.h>
#include <Guid/HobList.h>
#include <Guid/GlobalVariable.h>

#define SYS_TABLE_PAD(ptr) (((~ptr) +1) & 0x07 )
#define EFI_SYSTEM_TABLE_MAX_ADDRESS 0xFFFFFFFF

EFI_STATUS
ConvertAcpiTable (
    IN     UINTN                       TableLen,
    IN OUT VOID                        **Table
                  )
/*++

  Routine Description:
  Convert RSDP of ACPI Table if its location is lower than Address:0x100000
  Assumption here:
  As in legacy Bios, ACPI table is required to place in E/F Seg,
  So here we just check if the range is E/F seg,
  and if Not, assume the Memory type is EfiACPIReclaimMemory/EfiACPIMemoryNVS

  Arguments:
  TableLen  - Acpi RSDP length
  Table     - pointer to the table

  Returns:
  EFI_SUCCESS - Convert Table successfully
  Other       - Failed

  --*/
{
    VOID                  *AcpiTableOri;
    VOID                  *AcpiTableNew;
    EFI_STATUS            Status;
    EFI_PHYSICAL_ADDRESS  BufferPtr;


    AcpiTableOri    =  (VOID *)(UINTN)((*Table));

    BufferPtr = EFI_SYSTEM_TABLE_MAX_ADDRESS;
    Status = gBS->AllocatePages (
                                 AllocateMaxAddress,
                                 EfiACPIMemoryNVS,
                                 EFI_SIZE_TO_PAGES(TableLen),
                                 &BufferPtr
                                 );
    ASSERT_EFI_ERROR (Status);
    AcpiTableNew = (VOID *)(UINTN)BufferPtr;
    CopyMem (AcpiTableNew, AcpiTableOri, TableLen);

    //
    // Change configuration table Pointer
    //
    *Table = AcpiTableNew;

    return EFI_SUCCESS;
}

EFI_STATUS
ConvertSmbiosTable (
    IN OUT VOID        **Table
                    )
/*++

  Routine Description:

  Convert Smbios Table if the Location of the SMBios Table is lower than Address 0x100000
  Assumption here:
  As in legacy Bios, Smbios table is required to place in E/F Seg,
  So here we just check if the range is F seg,
  and if Not, assume the Memory type is EfiACPIMemoryNVS/EfiRuntimeServicesData
  Arguments:
  Table     - pointer to the table

  Returns:
  EFI_SUCCESS - Convert Table successfully
  Other       - Failed

  --*/
{
    SMBIOS_TABLE_ENTRY_POINT *SmbiosTableNew;
    SMBIOS_TABLE_ENTRY_POINT *SmbiosTableOri;
    EFI_STATUS               Status;
    UINT32                   SmbiosEntryLen;
    UINT32                   BufferLen;
    EFI_PHYSICAL_ADDRESS     BufferPtr;

    SmbiosTableNew  = NULL;
    SmbiosTableOri  = NULL;

    //
    // Get Smibos configuration Table
    //
    SmbiosTableOri =  (SMBIOS_TABLE_ENTRY_POINT *)(UINTN)((*Table));


    ASSERT(CalculateSum8((UINT8*)SmbiosTableOri, sizeof(SMBIOS_TABLE_ENTRY_POINT)) == 0);
    //
    // Relocate the Smibos memory
    //
    BufferPtr = EFI_SYSTEM_TABLE_MAX_ADDRESS;
    if (SmbiosTableOri->SmbiosBcdRevision != 0x21) {
        SmbiosEntryLen  = SmbiosTableOri->EntryPointLength;
    } else {
        //
        // According to Smbios Spec 2.4, we should set entry point length as 0x1F if version is 2.1
        //
        SmbiosEntryLen = 0x1F;
    }
    BufferLen = SmbiosEntryLen + SYS_TABLE_PAD(SmbiosEntryLen) + SmbiosTableOri->TableLength;
    Status = gBS->AllocatePages (
        AllocateMaxAddress,
        EfiACPIMemoryNVS,
        EFI_SIZE_TO_PAGES(BufferLen),
        &BufferPtr
                                 );
    ASSERT_EFI_ERROR (Status);
    SmbiosTableNew = (SMBIOS_TABLE_ENTRY_POINT *)(UINTN)BufferPtr;
    CopyMem (
        SmbiosTableNew,
        SmbiosTableOri,
        SmbiosEntryLen
             );
    //
    // Get Smbios Structure table address, and make sure the start address is 32-bit align
    //
    BufferPtr += SmbiosEntryLen + SYS_TABLE_PAD(SmbiosEntryLen);
    CopyMem (
        (VOID *)(UINTN)BufferPtr,
        (VOID *)(UINTN)(SmbiosTableOri->TableAddress),
        SmbiosTableOri->TableLength
             );
    SmbiosTableNew->TableAddress = (UINT32)BufferPtr;
    SmbiosTableNew->IntermediateChecksum = 0;
    SmbiosTableNew->IntermediateChecksum =
            CalculateCheckSum8 ((UINT8*)SmbiosTableNew + 0x10, SmbiosEntryLen -0x10);
    //
    // Change the SMBIOS pointer
    //
    *Table = SmbiosTableNew;

    return EFI_SUCCESS;
}

EFI_STATUS
ConvertMpsTable (
    IN OUT VOID          **Table
                 )
/*++

  Routine Description:

  Convert MP Table if the Location of the SMBios Table is lower than Address 0x100000
  Assumption here:
  As in legacy Bios, MP table is required to place in E/F Seg,
  So here we just check if the range is E/F seg,
  and if Not, assume the Memory type is EfiACPIMemoryNVS/EfiRuntimeServicesData
  Arguments:
  Table     - pointer to the table

  Returns:
  EFI_SUCCESS - Convert Table successfully
  Other       - Failed

  --*/
{
    UINT32                                       Data32;
    UINT32                                       FPLength;
    EFI_LEGACY_MP_TABLE_FLOATING_POINTER         *MpsFloatingPointerOri;
    EFI_LEGACY_MP_TABLE_FLOATING_POINTER         *MpsFloatingPointerNew;
    EFI_LEGACY_MP_TABLE_HEADER                   *MpsTableOri;
    EFI_LEGACY_MP_TABLE_HEADER                   *MpsTableNew;
    VOID                                         *OemTableOri;
    VOID                                         *OemTableNew;
    EFI_STATUS                                   Status;
    EFI_PHYSICAL_ADDRESS                         BufferPtr;

    //
    // Get MP configuration Table
    //
    MpsFloatingPointerOri = (EFI_LEGACY_MP_TABLE_FLOATING_POINTER *)(UINTN)(*Table);
    //
    // Get Floating pointer structure length
    //
    FPLength = MpsFloatingPointerOri->Length * 16;
    ASSERT(CalculateSum8((UINT8*)MpsFloatingPointerOri, FPLength) == 0);
    Data32   = FPLength + SYS_TABLE_PAD (FPLength);
    MpsTableOri = (EFI_LEGACY_MP_TABLE_HEADER *)(UINTN)(MpsFloatingPointerOri->PhysicalAddress);
    ASSERT(MpsTableOri != NULL);
    ASSERT(CalculateSum8((UINT8*)MpsTableOri, MpsTableOri->BaseTableLength) == 0);

    Data32 += MpsTableOri->BaseTableLength;
    Data32 += MpsTableOri->ExtendedTableLength;
    if (MpsTableOri->OemTablePointer != 0x00) {
        Data32 += SYS_TABLE_PAD (Data32);
        Data32 += MpsTableOri->OemTableSize;
    }

    //
    // Relocate memory
    //
    BufferPtr = EFI_SYSTEM_TABLE_MAX_ADDRESS;
    Status = gBS->AllocatePages (
        AllocateMaxAddress,
        EfiACPIMemoryNVS,
        EFI_SIZE_TO_PAGES(Data32),
        &BufferPtr
                                 );
    ASSERT_EFI_ERROR (Status);
    MpsFloatingPointerNew = (EFI_LEGACY_MP_TABLE_FLOATING_POINTER *)(UINTN)BufferPtr;
    CopyMem (MpsFloatingPointerNew, MpsFloatingPointerOri, FPLength);
    //
    // If Mp Table exists
    //
    if (MpsTableOri != NULL) {
        //
        // Get Mps table length, including Ext table
        //
        BufferPtr = BufferPtr + FPLength + SYS_TABLE_PAD (FPLength);
        MpsTableNew = (EFI_LEGACY_MP_TABLE_HEADER *)(UINTN)BufferPtr;
        CopyMem (MpsTableNew, MpsTableOri, MpsTableOri->BaseTableLength + MpsTableOri->ExtendedTableLength);

        if ((MpsTableOri->OemTableSize != 0x0000) && (MpsTableOri->OemTablePointer != 0x0000)){
            BufferPtr += MpsTableOri->BaseTableLength + MpsTableOri->ExtendedTableLength;
            BufferPtr += SYS_TABLE_PAD (BufferPtr);
            OemTableNew = (VOID *)(UINTN)BufferPtr;
            OemTableOri = (VOID *)(UINTN)MpsTableOri->OemTablePointer;
            CopyMem (OemTableNew, OemTableOri, MpsTableOri->OemTableSize);
            MpsTableNew->OemTablePointer = (UINT32)(UINTN)OemTableNew;
        }
        MpsTableNew->Checksum = 0;
        MpsTableNew->Checksum = CalculateCheckSum8 ((UINT8*)MpsTableNew, MpsTableOri->BaseTableLength);
        MpsFloatingPointerNew->PhysicalAddress = (UINT32)(UINTN)MpsTableNew;
        MpsFloatingPointerNew->Checksum = 0;
        MpsFloatingPointerNew->Checksum = CalculateCheckSum8 ((UINT8*)MpsFloatingPointerNew, FPLength);
    }
    //
    // Change the pointer
    //
    *Table = MpsFloatingPointerNew;

    return EFI_SUCCESS;
}

EFI_STATUS
ConvertSystemTable (
    IN     EFI_GUID        *TableGuid,
    IN OUT VOID            **Table
                    )
/*++

  Routine Description:
  Convert ACPI Table /Smbios Table /MP Table if its location is lower than Address:0x100000
  Assumption here:
  As in legacy Bios, ACPI/Smbios/MP table is required to place in E/F Seg,
  So here we just check if the range is E/F seg,
  and if Not, assume the Memory type is EfiACPIReclaimMemory/EfiACPIMemoryNVS

  Arguments:
  TableGuid - Guid of the table
  Table     - pointer to the table

  Returns:
  EFI_SUCCESS - Convert Table successfully
  Other       - Failed

  --*/
{
    EFI_STATUS      Status = EFI_SUCCESS;
    VOID            *AcpiHeader;
    UINTN           AcpiTableLen;

    //
    // If match acpi guid (1.0, 2.0, or later), Convert ACPI table according to version.
    //

    if (CompareGuid(TableGuid, &gEfiAcpiTableGuid) || CompareGuid(TableGuid, &gEfiAcpi20TableGuid)){
        AcpiHeader = (VOID*)(UINTN)(*Table);

        if (((EFI_ACPI_1_0_ROOT_SYSTEM_DESCRIPTION_POINTER *)AcpiHeader)->Reserved == 0x00){
            //
            // If Acpi 1.0 Table, then RSDP structure doesn't contain Length field, use structure size
            //
            AcpiTableLen = sizeof (EFI_ACPI_1_0_ROOT_SYSTEM_DESCRIPTION_POINTER);
        } else if (((EFI_ACPI_1_0_ROOT_SYSTEM_DESCRIPTION_POINTER *)AcpiHeader)->Reserved >= 0x02){
            //
            // If Acpi 2.0 or later, use RSDP Length fied.
            //
            AcpiTableLen = ((EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *)AcpiHeader)->Length;
        } else {
            //
            // Invalid Acpi Version, return
            //
            return EFI_UNSUPPORTED;
        }
        Status = ConvertAcpiTable (AcpiTableLen, Table);
        return Status;
    }

    //
    // If matches smbios guid, convert Smbios table.
    //
    if (CompareGuid(TableGuid, &gEfiSmbiosTableGuid)){
        Status = ConvertSmbiosTable (Table);
        return Status;
    }

    //
    // If the table is MP table?
    //
    if (CompareGuid(TableGuid, &gEfiMpsTableGuid)){
        Status = ConvertMpsTable (Table);
        return Status;
    }

    return EFI_UNSUPPORTED;
}
