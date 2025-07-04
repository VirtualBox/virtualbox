/* $Id: LegacyBiosMpTable.h 109526 2025-05-14 10:34:03Z alexander.eichner@oracle.com $ */
/** @file
 * LegacyBiosMpTable.h
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

This code is based on:

Copyright (c) 2007, Intel Corporation
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

Module Name:

  LegacyBiosMpTable.h

Abstract:
  Defines data structures per Multi Processor Specification Ver 1.4.

--*/

#ifndef LEGACY_BIOS_MPTABLE_H_
#define LEGACY_BIOS_MPTABLE_H_

#define EFI_LEGACY_MP_TABLE_REV_1_4 0x04

//
// Define MP table structures. All are packed.
//
#pragma pack(push, 1)

#define EFI_LEGACY_MP_TABLE_FLOATING_POINTER_SIGNATURE  SIGNATURE_32 ('_', 'M', 'P', '_')
typedef struct {
  UINT32  Signature;
  UINT32  PhysicalAddress;
  UINT8   Length;
  UINT8   SpecRev;
  UINT8   Checksum;
  UINT8   FeatureByte1;
  struct {
    UINT32  Reserved1 : 6;
    UINT32  MutipleClk : 1;
    UINT32  Imcr : 1;
    UINT32  Reserved2 : 24;
  } FeatureByte2_5;
} EFI_LEGACY_MP_TABLE_FLOATING_POINTER;

#define EFI_LEGACY_MP_TABLE_HEADER_SIGNATURE  SIGNATURE_32 ('P', 'C', 'M', 'P')
typedef struct {
  UINT32  Signature;
  UINT16  BaseTableLength;
  UINT8   SpecRev;
  UINT8   Checksum;
  CHAR8   OemId[8];
  CHAR8   OemProductId[12];
  UINT32  OemTablePointer;
  UINT16  OemTableSize;
  UINT16  EntryCount;
  UINT32  LocalApicAddress;
  UINT16  ExtendedTableLength;
  UINT8   ExtendedChecksum;
  UINT8   Reserved;
} EFI_LEGACY_MP_TABLE_HEADER;

typedef struct {
  UINT8 EntryType;
} EFI_LEGACY_MP_TABLE_ENTRY_TYPE;

//
// Entry Type 0: Processor.
//
#define EFI_LEGACY_MP_TABLE_ENTRY_TYPE_PROCESSOR  0x00
typedef struct {
  UINT8 EntryType;
  UINT8 Id;
  UINT8 Ver;
  struct {
    UINT8 Enabled : 1;
    UINT8 Bsp : 1;
    UINT8 Reserved : 6;
  } Flags;
  struct {
    UINT32  Stepping : 4;
    UINT32  Model : 4;
    UINT32  Family : 4;
    UINT32  Reserved : 20;
  } Signature;
  struct {
    UINT32  Fpu : 1;
    UINT32  Reserved1 : 6;
    UINT32  Mce : 1;
    UINT32  Cx8 : 1;
    UINT32  Apic : 1;
    UINT32  Reserved2 : 22;
  } Features;
  UINT32  Reserved1;
  UINT32  Reserved2;
} EFI_LEGACY_MP_TABLE_ENTRY_PROCESSOR;

//
// Entry Type 1: Bus.
//
#define EFI_LEGACY_MP_TABLE_ENTRY_TYPE_BUS  0x01
typedef struct {
  UINT8 EntryType;
  UINT8 Id;
  CHAR8 TypeString[6];
} EFI_LEGACY_MP_TABLE_ENTRY_BUS;

#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_CBUS   "CBUS  "  // Corollary CBus
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_CBUSII "CBUSII"  // Corollary CBUS II
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_EISA   "EISA  "  // Extended ISA
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_FUTURE "FUTURE"  // IEEE FutureBus
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_INTERN "INTERN"  // Internal bus
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_ISA    "ISA   "  // Industry Standard Architecture
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_MBI    "MBI   "  // Multibus I
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_MBII   "MBII  "  // Multibus II
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_MCA    "MCA   "  // Micro Channel Architecture
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_MPI    "MPI   "  // MPI
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_MPSA   "MPSA  "  // MPSA
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_NUBUS  "NUBUS "  // Apple Macintosh NuBus
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_PCI    "PCI   "  // Peripheral Component Interconnect
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_PCMCIA "PCMCIA"  // PC Memory Card International Assoc.
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_TC     "TC    "  // DEC TurboChannel
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_VL     "VL    "  // VESA Local Bus
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_VME    "VME   "  // VMEbus
#define EFI_LEGACY_MP_TABLE_ENTRY_BUS_STRING_XPRESS "XPRESS"  // Express System Bus
//
// Entry Type 2: I/O APIC.
//
#define EFI_LEGACY_MP_TABLE_ENTRY_TYPE_IOAPIC 0x02
typedef struct {
  UINT8 EntryType;
  UINT8 Id;
  UINT8 Ver;
  struct {
    UINT8 Enabled : 1;
    UINT8 Reserved : 7;
  } Flags;
  UINT32  Address;
} EFI_LEGACY_MP_TABLE_ENTRY_IOAPIC;

//
// Entry Type 3: I/O Interrupt Assignment.
//
#define EFI_LEGACY_MP_TABLE_ENTRY_TYPE_IO_INT 0x03
typedef struct {
  UINT8 EntryType;
  UINT8 IntType;
  struct {
    UINT16  Polarity : 2;
    UINT16  Trigger : 2;
    UINT16  Reserved : 12;
  } Flags;
  UINT8 SourceBusId;
  union {
    struct {
      UINT8 IntNo : 2;
      UINT8 Dev : 5;
      UINT8 Reserved : 1;
    } fields;
    UINT8 byte;
  } SourceBusIrq;
  UINT8 DestApicId;
  UINT8 DestApicIntIn;
} EFI_LEGACY_MP_TABLE_ENTRY_IO_INT;

typedef enum {
  EfiLegacyMpTableEntryIoIntTypeInt   = 0,
  EfiLegacyMpTableEntryIoIntTypeNmi   = 1,
  EfiLegacyMpTableEntryIoIntTypeSmi   = 2,
  EfiLegacyMpTableEntryIoIntTypeExtInt= 3,
} EFI_LEGACY_MP_TABLE_ENTRY_IO_INT_TYPE;

typedef enum {
  EfiLegacyMpTableEntryIoIntFlagsPolaritySpec       = 0x0,
  EfiLegacyMpTableEntryIoIntFlagsPolarityActiveHigh = 0x1,
  EfiLegacyMpTableEntryIoIntFlagsPolarityReserved   = 0x2,
  EfiLegacyMpTableEntryIoIntFlagsPolarityActiveLow  = 0x3,
} EFI_LEGACY_MP_TABLE_ENTRY_IO_INT_FLAGS_POLARITY;

typedef enum {
  EfiLegacyMpTableEntryIoIntFlagsTriggerSpec        = 0x0,
  EfiLegacyMpTableEntryIoIntFlagsTriggerEdge        = 0x1,
  EfiLegacyMpTableEntryIoIntFlagsTriggerReserved    = 0x2,
  EfiLegacyMpTableEntryIoIntFlagsTriggerLevel       = 0x3,
} EFI_LEGACY_MP_TABLE_ENTRY_IO_INT_FLAGS_TRIGGER;

//
// Entry Type 4: Local Interrupt Assignment.
//
#define EFI_LEGACY_MP_TABLE_ENTRY_TYPE_LOCAL_INT  0x04
typedef struct {
  UINT8 EntryType;
  UINT8 IntType;
  struct {
    UINT16  Polarity : 2;
    UINT16  Trigger : 2;
    UINT16  Reserved : 12;
  } Flags;
  UINT8 SourceBusId;
  UINT8 SourceBusIrq;
  UINT8 DestApicId;
  UINT8 DestApicIntIn;
} EFI_LEGACY_MP_TABLE_ENTRY_LOCAL_INT;

typedef enum {
  EfiLegacyMpTableEntryLocalIntTypeInt              = 0,
  EfiLegacyMpTableEntryLocalIntTypeNmi              = 1,
  EfiLegacyMpTableEntryLocalIntTypeSmi              = 2,
  EfiLegacyMpTableEntryLocalIntTypeExtInt           = 3,
} EFI_LEGACY_MP_TABLE_ENTRY_LOCAL_INT_TYPE;

typedef enum {
  EfiLegacyMpTableEntryLocalIntFlagsPolaritySpec      = 0x0,
  EfiLegacyMpTableEntryLocalIntFlagsPolarityActiveHigh= 0x1,
  EfiLegacyMpTableEntryLocalIntFlagsPolarityReserved  = 0x2,
  EfiLegacyMpTableEntryLocalIntFlagsPolarityActiveLow = 0x3,
} EFI_LEGACY_MP_TABLE_ENTRY_LOCAL_INT_FLAGS_POLARITY;

typedef enum {
  EfiLegacyMpTableEntryLocalIntFlagsTriggerSpec       = 0x0,
  EfiLegacyMpTableEntryLocalIntFlagsTriggerEdge       = 0x1,
  EfiLegacyMpTableEntryLocalIntFlagsTriggerReserved   = 0x2,
  EfiLegacyMpTableEntryLocalIntFlagsTriggerLevel      = 0x3,
} EFI_LEGACY_MP_TABLE_ENTRY_LOCAL_INT_FLAGS_TRIGGER;

//
// Entry Type 128: System Address Space Mapping.
//
#define EFI_LEGACY_MP_TABLE_ENTRY_EXT_TYPE_SYS_ADDR_SPACE_MAPPING 0x80
typedef struct {
  UINT8   EntryType;
  UINT8   Length;
  UINT8   BusId;
  UINT8   AddressType;
  UINT64  AddressBase;
  UINT64  AddressLength;
} EFI_LEGACY_MP_TABLE_ENTRY_EXT_SYS_ADDR_SPACE_MAPPING;

typedef enum {
  EfiLegacyMpTableEntryExtSysAddrSpaceMappingIo       = 0,
  EfiLegacyMpTableEntryExtSysAddrSpaceMappingMemory   = 1,
  EfiLegacyMpTableEntryExtSysAddrSpaceMappingPrefetch = 2,
} EFI_LEGACY_MP_TABLE_ENTRY_EXT_SYS_ADDR_SPACE_MAPPING_TYPE;

//
// Entry Type 129: Bus Hierarchy.
//
#define EFI_LEGACY_MP_TABLE_ENTRY_EXT_TYPE_BUS_HIERARCHY  0x81
typedef struct {
  UINT8 EntryType;
  UINT8 Length;
  UINT8 BusId;
  struct {
    UINT8 SubtractiveDecode : 1;
    UINT8 Reserved : 7;
  } BusInfo;
  UINT8 ParentBus;
  UINT8 Reserved1;
  UINT8 Reserved2;
  UINT8 Reserved3;
} EFI_LEGACY_MP_TABLE_ENTRY_EXT_BUS_HIERARCHY;

//
// Entry Type 130: Compatibility Bus Address Space Modifier.
//
#define EFI_LEGACY_MP_TABLE_ENTRY_EXT_TYPE_COMPAT_BUS_ADDR_SPACE_MODIFIER 0x82
typedef struct {
  UINT8 EntryType;
  UINT8 Length;
  UINT8 BusId;
  struct {
    UINT8 RangeMode : 1;
    UINT8 Reserved : 7;
  } AddrMode;
  UINT32  PredefinedRangeList;
} EFI_LEGACY_MP_TABLE_ENTRY_EXT_COMPAT_BUS_ADDR_SPACE_MODIFIER;

#pragma pack(pop)

#endif
