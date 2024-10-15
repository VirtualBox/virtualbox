/* $Id: runtime_service_table.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * runtime_service_table.h - runtime service table declaration.
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
TBL_ENTRY(EFI_GET_TIME, GetTime, NVOID, EFI_STATUS, 2, (PTR(EFI_TIME), PTR(EFI_TIME_CAPABILITIES)))
TBL_ENTRY(EFI_SET_TIME, SetTime, NVOID, EFI_STATUS, 1, (PTR(EFI_TIME)))
TBL_ENTRY(EFI_GET_WAKEUP_TIME, GetWakeupTime, NVOID, EFI_STATUS, 3, (PTR(BOOLEAN), PTR(BOOLEAN), PTR(EFI_TIME)))
TBL_ENTRY(EFI_SET_WAKEUP_TIME, SetWakeupTime, NVOID, EFI_STATUS, 2, (SCL(BOOLEAN), PTR(EFI_TIME)))

//
// Virtual Memory Services
//
TBL_ENTRY(EFI_SET_VIRTUAL_ADDRESS_MAP, SetVirtualAddressMap, NVOID, EFI_STATUS, 4, (SCL(UINTN), SCL(UINTN), SCL(UINT32), PTR(EFI_MEMORY_DESCRIPTOR)))
TBL_ENTRY(EFI_CONVERT_POINTER, ConvertPointer, NVOID, EFI_STATUS, 2, (SCL(UINTN), PTR2(VOID)))

//
// Variable Services
//
TBL_ENTRY(EFI_GET_VARIABLE, GetVariable, NVOID, EFI_STATUS, 5, (PTR(CHAR16), PTR(EFI_GUID), PTR(UINT32), PTR(UINTN), PTR(VOID)))
TBL_ENTRY(EFI_GET_NEXT_VARIABLE_NAME, GetNextVariableName, NVOID, EFI_STATUS, 3, (PTR(UINTN), PTR(CHAR16), PTR(EFI_GUID)))
TBL_ENTRY(EFI_SET_VARIABLE, SetVariable, NVOID, EFI_STATUS, 5, (PTR(CHAR16), PTR(EFI_GUID), SCL(UINT32), SCL(UINTN), PTR(VOID)))

//
// Miscellaneous Services
//
TBL_ENTRY(EFI_GET_NEXT_HIGH_MONO_COUNT, GetNextHighMonotonicCount, NVOID, EFI_STATUS, 1, (PTR(UINT32)))
TBL_ENTRY(EFI_RESET_SYSTEM, ResetSystem, RVOID, VOID, 4, (SCL(EFI_RESET_TYPE), SCL(EFI_STATUS), SCL(UINTN), PTR(VOID)))

//
// UEFI 2.0 Capsule Services
//
TBL_ENTRY(EFI_UPDATE_CAPSULE, UpdateCapsule, NVOID, EFI_STATUS, 3, (PTR2(EFI_CAPSULE_HEADER), SCL(UINTN), SCL(EFI_PHYSICAL_ADDRESS)))
TBL_ENTRY(EFI_QUERY_CAPSULE_CAPABILITIES, QueryCapsuleCapabilities, NVOID, EFI_STATUS, 4, (PTR2(EFI_CAPSULE_HEADER), SCL(UINTN), PTR(UINT64), PTR(EFI_RESET_TYPE)))

//
// Miscellaneous UEFI 2.0 Service
//
TBL_ENTRY(EFI_QUERY_VARIABLE_INFO, QueryVariableInfo, NVOID, EFI_STATUS, 4, (SCL(UINT32), PTR(UINT64), PTR(UINT64), PTR(UINT64)))
