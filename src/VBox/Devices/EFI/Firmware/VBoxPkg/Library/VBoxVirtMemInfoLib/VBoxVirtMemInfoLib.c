/* $Id: VBoxVirtMemInfoLib.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVirtMemInfoLib.c - Providing the address map for setting up the MMU based on the platform settings.
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

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>

#include <Library/VBoxArmPlatformLib.h>

// Number of Virtual Memory Map Descriptors
#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS  7

/**
  Default library constructur that obtains the memory size from a PCD.

  @return  Always returns RETURN_SUCCESS

**/
RETURN_STATUS
EFIAPI
VBoxVirtMemInfoLibConstructor (
  VOID
  )
{
  UINT64  Size;
  VOID    *Hob;

  Size = VBoxArmPlatformRamBaseSizeGet();
  Hob  = BuildGuidDataHob (&gArmVirtSystemMemorySizeGuid, &Size, sizeof Size);
  ASSERT (Hob != NULL);

  return RETURN_SUCCESS;
}

/**
  Return the Virtual Memory Map of your platform

  This Virtual Memory Map is used by MemoryInitPei Module to initialize the MMU
  on your platform.

  @param[out]   VirtualMemoryMap    Array of ARM_MEMORY_REGION_DESCRIPTOR
                                    describing a Physical-to-Virtual Memory
                                    mapping. This array must be ended by a
                                    zero-filled entry. The allocated memory
                                    will not be freed.

**/
VOID
ArmVirtGetMemoryMap (
  OUT ARM_MEMORY_REGION_DESCRIPTOR  **VirtualMemoryMap
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR  *VirtualMemoryTable;

  ASSERT (VirtualMemoryMap != NULL);

  VirtualMemoryTable = AllocatePool (
                         sizeof (ARM_MEMORY_REGION_DESCRIPTOR) *
                         MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS
                         );

  if (VirtualMemoryTable == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Error: Failed AllocatePool()\n", __func__));
    return;
  }

  // System DRAM
  VirtualMemoryTable[0].PhysicalBase = VBoxArmPlatformRamBaseStartGetPhysAddr();
  VirtualMemoryTable[0].VirtualBase  = VirtualMemoryTable[0].PhysicalBase;
  VirtualMemoryTable[0].Length       = VBoxArmPlatformRamBaseSizeGet();
  VirtualMemoryTable[0].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;

  // Memory mapped peripherals
  VirtualMemoryTable[1].PhysicalBase = VBoxArmPlatformMmioStartGetPhysAddr();
  VirtualMemoryTable[1].VirtualBase  = VirtualMemoryTable[1].PhysicalBase;
  VirtualMemoryTable[1].Length       = VBoxArmPlatformMmioSizeGet();
  VirtualMemoryTable[1].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  // Map the FV region as normal executable memory
  VirtualMemoryTable[2].PhysicalBase = VBoxArmPlatformUefiRomStartGetPhysAddr();
  VirtualMemoryTable[2].VirtualBase  = VirtualMemoryTable[2].PhysicalBase;
  VirtualMemoryTable[2].Length       = FixedPcdGet32 (PcdFvSize);
  VirtualMemoryTable[2].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_RO;

  // Map the FDT region readonnly.
  VirtualMemoryTable[3].PhysicalBase = VBoxArmPlatformFdtGet();
  VirtualMemoryTable[3].VirtualBase  = VirtualMemoryTable[3].PhysicalBase;
  VirtualMemoryTable[3].Length       = VBoxArmPlatformFdtSizeGet();
  VirtualMemoryTable[3].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_RO;

  // Map the VBox descriptor region readonly.
  VirtualMemoryTable[4].PhysicalBase = VBoxArmPlatformDescGetPhysAddr();
  VirtualMemoryTable[4].VirtualBase  = VirtualMemoryTable[4].PhysicalBase;
  VirtualMemoryTable[4].Length       = VBoxArmPlatformDescSizeGet();
  VirtualMemoryTable[4].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_RO;

  // Map the MMIO32 region if it exists.
  if (VBoxArmPlatformMmio32SizeGet() != 0)
  {
    VirtualMemoryTable[5].PhysicalBase = VBoxArmPlatformMmio32StartGetPhysAddr();
    VirtualMemoryTable[5].VirtualBase  = VirtualMemoryTable[5].PhysicalBase;
    VirtualMemoryTable[5].Length       = VBoxArmPlatformMmio32SizeGet();
    VirtualMemoryTable[5].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
  }

  // End of Table
  ZeroMem (&VirtualMemoryTable[6], sizeof (ARM_MEMORY_REGION_DESCRIPTOR));

  for (UINTN i = 0; i < MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS; i++)
  {
    DEBUG ((
      DEBUG_INFO,
      "%a: Memory Table Entry %u:\n"
      "\tPhysicalBase: 0x%lX\n"
      "\tVirtualBase: 0x%lX\n"
      "\tLength: 0x%lX\n",
      __func__, i,
      VirtualMemoryTable[i].PhysicalBase,
      VirtualMemoryTable[i].VirtualBase,
      VirtualMemoryTable[i].Length
      ));
  }

  *VirtualMemoryMap = VirtualMemoryTable;
}
