/* $Id: acpi-aml.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, ACPI (Advanced Configuration and Power Interface) Machine Language (AML) format.
 *
 * Spec taken from: https://uefi.org/sites/default/files/resources/ACPI_Spec_6_5_Aug29.pdf (2024-07-25)
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_acpi_aml_h
#define IPRT_INCLUDED_formats_acpi_aml_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/cdefs.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_acpi_aml    Advanced Configuration and Power Interface (ACPI) Machine Language (AML) structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */

/** @name AML Bytecode values (see https://uefi.org/specs/ACPI/6.5/20_AML_Specification.html#aml-byte-stream-byte-values).
 * @{ */
/** Encoding Name: ZeroOp,              Encoding Group: Data Object */
#define ACPI_AML_BYTE_CODE_OP_ZERO               0x00
/** Encoding Name: OneOp,               Encoding Group: Data Object */
#define ACPI_AML_BYTE_CODE_OP_ONE                0x01
/** Encoding Name: AliasOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_ALIAS              0x06
/** Encoding Name: NameOp,              Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_NAME               0x08
/** Encoding Name: BytePrefix,          Encoding Group: Data Object */
#define ACPI_AML_BYTE_CODE_PREFIX_BYTE           0x0a
/** Encoding Name: WordPrefix,          Encoding Group: Data Object */
#define ACPI_AML_BYTE_CODE_PREFIX_WORD           0x0b
/** Encoding Name: DWordPrefix,         Encoding Group: Data Object */
#define ACPI_AML_BYTE_CODE_PREFIX_DWORD          0x0c
/** Encoding Name: StringPrefix,        Encoding Group: Data Object */
#define ACPI_AML_BYTE_CODE_PREFIX_STRING         0x0d
/** Encoding Name: QWordPrefix,         Encoding Group: Data Object */
#define ACPI_AML_BYTE_CODE_PREFIX_QWORD          0x0e
/** Encoding Name: ScopeOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_SCOPE              0x10
/** Encoding Name: BufferOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_BUFFER             0x11
/** Encoding Name: PackageOp,           Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_PACKAGE            0x12
/** Encoding Name: VarPackageOp,        Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_VAR_PACKAGE        0x13
/** Encoding Name: MethodOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_METHOD             0x14
/** Encoding Name: ExternalOp,          Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_EXTERNAL           0x15
/** Encoding Name: DualNamePrefix,      Encoding Group: Name Object */
#define ACPI_AML_BYTE_CODE_PREFIX_DUAL_NAME      0x2e
/** Encoding Name: MultiNamePrefix,     Encoding Group: Name Object */
#define ACPI_AML_BYTE_CODE_PREFIX_MULTI_NAME     0x2f
/** Encoding Name: ExtOpPrefix,         Encoding Group: - */
#define ACPI_AML_BYTE_CODE_PREFIX_EXT_OP         0x5b
/** Encoding Name: MutexOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_MUTEX          0x01
/** Encoding Name: EventOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_EVENT          0x02
/** Encoding Name: CondRefOfOp,         Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_COND_REF_OF    0x12
/** Encoding Name: CreateFieldOp,       Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_CREATE_FIELD   0x13
/** Encoding Name: LoadTableOp,         Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_LOAD_TABLE     0x1f
/** Encoding Name: LoadOp,              Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_LOAD           0x20
/** Encoding Name: StallOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_STALL          0x21
/** Encoding Name: SleepOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_SLEEP          0x22
/** Encoding Name: AcquireOp,           Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_ACQUIRE        0x23
/** Encoding Name: SignalOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_SIGNAL         0x24
/** Encoding Name: SignalOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_WAIT           0x25
/** Encoding Name: ResetOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_RESET          0x26
/** Encoding Name: ReleaseOp,           Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_RELEASE        0x27
/** Encoding Name: FromBCDOp,           Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_FROM_BCD       0x28
/** Encoding Name: ToBCDOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_TO_BCD         0x29
/** Encoding Name: RevisionOp,          Encoding Group: Data Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_REVISION       0x30
/** Encoding Name: DebugOp,             Encoding Group: Debug Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_DEBUG          0x31
/** Encoding Name: FatalOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_FATAL          0x32
/** Encoding Name: FatalOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_TIMER          0x33
/** Encoding Name: OpRegionOp,          Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_OP_REGION      0x80
/** Encoding Name: FieldOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_FIELD          0x81
/** Encoding Name: DeviceOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_DEVICE         0x82
/** Encoding Name: ProcessorOp,         Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_PROCESSOR      0x83
/** Encoding Name: PowerResOp,          Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_POWER_RES      0x84
/** Encoding Name: ThermalZoneOp,       Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_THERMAL_ZONE   0x85
/** Encoding Name: IndexFieldOp,        Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_INDEX_FIELD    0x86
/** Encoding Name: BankFieldOp,         Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_BANK_FIELD     0x87
/** Encoding Name: DataRegionOp,        Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_EXT_OP_DATA_REGION    0x88
/** Encoding Name: RootChar,            Encoding Group: Name Object */
#define ACPI_AML_BYTE_CODE_ROOT_CHAR             0x5c
/** Encoding Name: ParentPrefixChar,    Encoding Group: Name Object */
#define ACPI_AML_BYTE_CODE_PREFIX_PARENT_CHAR    0x5e
/** Encoding Name: NameChar,            Encoding Group: Name Object */
#define ACPI_AML_BYTE_CODE_NAME_CHAR             0x5f
/** Encoding Name: Local0Op,            Encoding Group: Local Object */
#define ACPI_AML_BYTE_CODE_OP_LOCAL_0            0x60
/** Encoding Name: Local1Op,            Encoding Group: Local Object */
#define ACPI_AML_BYTE_CODE_OP_LOCAL_1            0x61
/** Encoding Name: Local2Op,            Encoding Group: Local Object */
#define ACPI_AML_BYTE_CODE_OP_LOCAL_2            0x62
/** Encoding Name: Local3Op,            Encoding Group: Local Object */
#define ACPI_AML_BYTE_CODE_OP_LOCAL_3            0x63
/** Encoding Name: Local4Op,            Encoding Group: Local Object */
#define ACPI_AML_BYTE_CODE_OP_LOCAL_4            0x64
/** Encoding Name: Local5Op,            Encoding Group: Local Object */
#define ACPI_AML_BYTE_CODE_OP_LOCAL_5            0x65
/** Encoding Name: Local6Op,            Encoding Group: Local Object */
#define ACPI_AML_BYTE_CODE_OP_LOCAL_6            0x66
/** Encoding Name: Local7Op,            Encoding Group: Local Object */
#define ACPI_AML_BYTE_CODE_OP_LOCAL_7            0x67
/** Encoding Name: Arg0Op,              Encoding Group: Arg Object */
#define ACPI_AML_BYTE_CODE_OP_ARG_0              0x68
/** Encoding Name: Arg1Op,              Encoding Group: Arg Object */
#define ACPI_AML_BYTE_CODE_OP_ARG_1              0x69
/** Encoding Name: Arg2Op,              Encoding Group: Arg Object */
#define ACPI_AML_BYTE_CODE_OP_ARG_2              0x6a
/** Encoding Name: Arg3Op,              Encoding Group: Arg Object */
#define ACPI_AML_BYTE_CODE_OP_ARG_3              0x6b
/** Encoding Name: Arg4Op,              Encoding Group: Arg Object */
#define ACPI_AML_BYTE_CODE_OP_ARG_4              0x6c
/** Encoding Name: Arg5Op,              Encoding Group: Arg Object */
#define ACPI_AML_BYTE_CODE_OP_ARG_5              0x6d
/** Encoding Name: Arg6Op,              Encoding Group: Arg Object */
#define ACPI_AML_BYTE_CODE_OP_ARG_6              0x6e
/** Encoding Name: StoreOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_STORE              0x70
/** Encoding Name: RefOfOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_REF_OF             0x71
/** Encoding Name: AddOp,               Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_ADD                0x72
/** Encoding Name: ConcatOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_CONCAT             0x73
/** Encoding Name: SubtractOp,          Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_SUBTRACT           0x74
/** Encoding Name: IncrementOp,         Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_INCREMENT          0x75
/** Encoding Name: DecrementOp,         Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_DECREMENT          0x76
/** Encoding Name: MultiplyOp,          Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_MULTIPLY           0x77
/** Encoding Name: DivideOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_DIVIDE             0x78
/** Encoding Name: ShiftLeftOp,         Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_SHIFT_LEFT         0x79
/** Encoding Name: ShiftRightOp,        Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_SHIFT_RIGHT        0x7a
/** Encoding Name: AndOp,               Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_AND                0x7b
/** Encoding Name: NandOp,              Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_NAND               0x7c
/** Encoding Name: OrOp,                Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_OR                 0x7d
/** Encoding Name: NorOp,               Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_NOR                0x7e
/** Encoding Name: XorOp,               Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_XOR                0x7f
/** Encoding Name: NotOp,               Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_NOT                0x80
/** Encoding Name: FindSetLeftBitOp,    Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_FIND_SET_LEFT_BIT  0x81
/** Encoding Name: FindSetRightBitOp,   Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_FIND_SET_RIGHT_BIT 0x82
/** Encoding Name: DerefOfOp,           Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_DEREF_OF           0x83
/** Encoding Name: ConcatResOp,         Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_CONCAT_RES         0x84
/** Encoding Name: ModOp,               Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_MOD                0x85
/** Encoding Name: NotifyOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_NOTIFY             0x86
/** Encoding Name: SizeOfOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_SIZE_OF            0x87
/** Encoding Name: IndexOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_INDEX              0x88
/** Encoding Name: MatchOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_MATCH              0x89
/** Encoding Name: CreateDWordFieldOp,  Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_CREATE_DWORD_FIELD 0x8a
/** Encoding Name: CreateWordFieldOp,   Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_CREATE_WORD_FIELD  0x8b
/** Encoding Name: CreateByteFieldOp,   Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_CREATE_BYTE_FIELD  0x8c
/** Encoding Name: CreateBitFieldOp,    Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_CREATE_BIT_FIELD   0x8d
/** Encoding Name: ObjectTypeOp,        Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_OBJECT_TYPE        0x8e
/** Encoding Name: CreateQWordFieldOp,  Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_CREATE_QWORD_FIELD 0x8f
/** Encoding Name: LandOp,              Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_LAND               0x90
/** Encoding Name: LorOp,               Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_LOR                0x91
/** Encoding Name: LnotOp,              Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_LNOT               0x92
/** Encoding Name: LEqualOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_LEQUAL             0x93
/** Encoding Name: LGreaterOp,          Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_LGREATER           0x94
/** Encoding Name: LLessOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_LLESS              0x95
/** Encoding Name: ToBufferOp,          Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_TO_BUFFER          0x96
/** Encoding Name: ToDecimalStringOp,   Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_TO_DECIMAL_STRING  0x97
/** Encoding Name: ToHexStringOp,       Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_TO_HEX_STRING      0x98
/** Encoding Name: ToIntegerOp,         Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_TO_INTEGER         0x99
/** Encoding Name: ToStringOp,          Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_TO_STRING          0x9c
/** Encoding Name: CopyObjectOp,        Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_COPY_OBJECT        0x9d
/** Encoding Name: MidOp,               Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_MID                0x9e
/** Encoding Name: ContinueOp,          Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_CONTINUE           0x9f
/** Encoding Name: IfOp,                Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_IF                 0xa0
/** Encoding Name: ElseOp,              Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_ELSE               0xa1
/** Encoding Name: WhileOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_WHILE              0xa2
/** Encoding Name: NoopOp,              Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_NOOP               0xa3
/** Encoding Name: ReturnOp,            Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_RETURN             0xa4
/** Encoding Name: BreakOp,             Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_BREAK              0xa5
/** Encoding Name: BreakPointOp,        Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_BREAK_POINT        0xcc
/** Encoding Name: OnesOp,              Encoding Group: Term Object */
#define ACPI_AML_BYTE_CODE_OP_ONES               0xff
/** @} */

/** @} */

#endif /* !IPRT_INCLUDED_formats_acpi_aml_h */

