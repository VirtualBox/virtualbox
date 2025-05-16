/* $Id: ARM_Apple_M3_Max.h 168912 2025-05-16 17:58:52Z knut.osmundsen@oracle.com $ */
/** @file
 * CPU database entry "Apple M3 Max".
 * Generated at 2025-05-02T20:19:41Z by VBoxCpuReport v7.1.97r168686 on darwin.arm64.
 */

/*
 * Copyright (C) 2013-2025 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VBOX_CPUDB_ARM_Apple_M3_Max_h
#define VBOX_CPUDB_ARM_Apple_M3_Max_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/**
 * Common system register values for Apple M3 Max.
 */
static SUPARMSYSREGVAL const g_aCmnSysRegVals_ARM_Apple_M3_Max[] =
{
    { UINT64_C(0x0000000080000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 5), 0x1 }, /* MPIDR_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 6), 0x0 }, /* REVIDR_EL1 */
    { UINT64_C(0x1101000010110111), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 0), 0x0 }, /* ID_AA64PFR0_EL1 */
    { UINT64_C(0x0000000100000021), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 1), 0x0 }, /* ID_AA64PFR1_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 2), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 3), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 4), 0x0 }, /* ID_AA64ZFR0_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 5), 0x0 }, /* ID_AA64SMFR0_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 6), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 7), 0x0 },
    { UINT64_C(0x0000000010305f09), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 0), 0x0 }, /* ID_AA64DFR0_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 1), 0x0 }, /* ID_AA64DFR1_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 2), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 3), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 4), 0x0 }, /* ID_AA64AFR0_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 5), 0x0 }, /* ID_AA64AFR1_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 6), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 7), 0x0 },
    { UINT64_C(0x0221100110212120), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 0), 0x0 }, /* ID_AA64ISAR0_EL1 */
    { UINT64_C(0x0010111110211402), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 1), 0x0 }, /* ID_AA64ISAR1_EL1 */
    { UINT64_C(0x0000000000000010), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 2), 0x0 }, /* ID_AA64ISAR2_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 3), 0x0 }, /* ID_AA64ISAR3_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 4), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 5), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 6), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 7), 0x0 },
    { UINT64_C(0x110012120f100003), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 0), 0x0 }, /* ID_AA64MMFR0_EL1 */
    { UINT64_C(0x0000111111312100), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 1), 0x0 }, /* ID_AA64MMFR1_EL1 */
    { UINT64_C(0x1201011102001011), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 2), 0x0 }, /* ID_AA64MMFR2_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 3), 0x0 }, /* ID_AA64MMFR3_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 4), 0x0 }, /* ID_AA64MMFR4_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 5), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 6), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 7), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 5, 3, 0), 0x0 }, /* ERRIDR_EL1 */
    { UINT64_C(0x0000000081000023), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1, 0, 0, 1), 0x0 }, /* CLIDR_EL1 */
    { UINT64_C(0x000000016a695797), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1, 0, 0, 7), 0x0 }, /* AIDR_EL1 */
    { UINT64_C(0x0000000000000004), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 0, 0, 7), 0x0 }, /* DCZID_EL0 */
    { UINT64_C(0x00000000016e3600), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3,14, 0, 0), 0x0 }, /* CNTFRQ_EL0 */
};


/**
 * System register values for Apple M3 Max (Sawtooth), variation #0.
 * 4 CPUs shares this variant: 0, 1, 2, 3
 */
static SUPARMSYSREGVAL const g_aVar0SysRegVals_ARM_Apple_M3_Max[] =
{
    { UINT64_C(0x00000000611f0482), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 0), 0x0 }, /* MIDR_EL1 */
    { UINT64_C(0x00000000700fe03a), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1, 0, 0, 0), 0x0 }, /* CCSIDR_EL1 */
};


/**
 * System register values for Apple M3 Max (Everest), variation #1.
 * 12 CPUs shares this variant: 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
 */
static SUPARMSYSREGVAL const g_aVar1SysRegVals_ARM_Apple_M3_Max[] =
{
    { UINT64_C(0x00000000611f0492), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 0), 0x0 }, /* MIDR_EL1 */
    { UINT64_C(0x00000000701fe03a), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1, 0, 0, 0), 0x0 }, /* CCSIDR_EL1 */
};


/**
 * Database entry for Apple M3 Max.
 */
static CPUMDBENTRYARM const g_Entry_ARM_Apple_M3_Max =
{
    {
        /*.pszName      = */ "Apple M3 Max",
        /*.pszFullName  = */ "Apple M3 Max",
        /*.enmVendor    = */ CPUMCPUVENDOR_APPLE,
        /*.enmMicroarch = */ kCpumMicroarch_Apple_M3,
        /*.fFlags       = */ 0,
        /*.enmEntryType = */ CPUMDBENTRYTYPE_ARM,
    },
    /*.paSysRegCmnVals  = */ NULL_ALONE(g_aCmnSysRegVals_ARM_Apple_M3_Max),
    /*.cSysRegCmnVals   = */ ZERO_ALONE(RT_ELEMENTS(g_aCmnSysRegVals_ARM_Apple_M3_Max)),
    /*.cVariants        = */ 2,
    /*.aVariants        = */
    {
        /*.Variants[0] = */
        {
            /*.pszName      = */ "Apple M3 Max (Sawtooth)",
            /*.Midr         = */
            {
                /*Midr.s = */
                {
                    /*.u4Revision    = */ 0x2,
                    /*.u12PartNum    = */ 0x048,
                    /*.u4Arch        = */ 0xf,
                    /*.u4Variant     = */ 0x1,
                    /*.u4Implementer = */ 0x61,
                }
            },
            /*.enmCoreType  = */ kCpumCoreType_Efficiency,
            /*.cSysRegVals  = */ ZERO_ALONE(RT_ELEMENTS(g_aVar0SysRegVals_ARM_Apple_M3_Max)),
            /*.paSysRegVals = */ NULL_ALONE(g_aVar0SysRegVals_ARM_Apple_M3_Max)
        },
        /*.Variants[1] = */
        {
            /*.pszName      = */ "Apple M3 Max (Everest)",
            /*.Midr         = */
            {
                /*Midr.s = */
                {
                    /*.u4Revision    = */ 0x2,
                    /*.u12PartNum    = */ 0x049,
                    /*.u4Arch        = */ 0xf,
                    /*.u4Variant     = */ 0x1,
                    /*.u4Implementer = */ 0x61,
                }
            },
            /*.enmCoreType  = */ kCpumCoreType_Performance,
            /*.cSysRegVals  = */ ZERO_ALONE(RT_ELEMENTS(g_aVar1SysRegVals_ARM_Apple_M3_Max)),
            /*.paSysRegVals = */ NULL_ALONE(g_aVar1SysRegVals_ARM_Apple_M3_Max)
        },
    }
};

#endif /* !VBOX_CPUDB_ARM_Apple_M3_Max_h */

