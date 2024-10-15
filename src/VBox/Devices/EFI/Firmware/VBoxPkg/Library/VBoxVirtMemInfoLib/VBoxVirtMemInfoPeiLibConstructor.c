/* $Id: VBoxVirtMemInfoPeiLibConstructor.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <Library/DebugLib.h>
#include <Library/HobLib.h>

#include <Library/VBoxArmPlatformLib.h>

RETURN_STATUS
EFIAPI
VBoxVirtMemInfoPeiLibConstructor (
  VOID
  )
{
  UINT64 NewBase;
  UINT64 NewSize;
  VOID   *Hob;

  NewBase = VBoxArmPlatformRamBaseStartGetPhysAddr();
  NewSize = VBoxArmPlatformRamBaseSizeGet();

  /** @todo This will go away soon when we change the other places to deal with dynamic RAM ranges. */
  Hob = BuildGuidDataHob (
          &gArmVirtSystemMemorySizeGuid,
          &NewSize,
          sizeof NewSize
          );
  ASSERT (Hob != NULL);

  //
  // We need to make sure that the machine we are running on has at least
  // 128 MB of memory configured, and is currently executing this binary from
  // NOR flash. This prevents a device tree image in DRAM from getting
  // clobbered when our caller installs permanent PEI RAM, before we have a
  // chance of marking its location as reserved or copy it to a freshly
  // allocated block in the permanent PEI RAM in the platform PEIM.
  //
  ASSERT (NewSize >= SIZE_128MB);
  ASSERT (
    (((UINT64)PcdGet64 (PcdFdBaseAddress) +
      (UINT64)PcdGet32 (PcdFdSize)) <= NewBase) ||
    ((UINT64)PcdGet64 (PcdFdBaseAddress) >= (NewBase + NewSize))
    );

  return RETURN_SUCCESS;
}
