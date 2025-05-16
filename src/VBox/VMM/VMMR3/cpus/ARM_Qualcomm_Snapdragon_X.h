/* $Id: ARM_Qualcomm_Snapdragon_X.h 168912 2025-05-16 17:58:52Z knut.osmundsen@oracle.com $ */
/** @file
 * CPU database entry "Qualcomm Snapdragon X".
 * Generated at 2025-04-21T11:23:38Z by VBoxCpuReport v7.1.97r168566 on win.arm64.
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

#ifndef VBOX_CPUDB_ARM_Qualcomm_Snapdragon_X_h
#define VBOX_CPUDB_ARM_Qualcomm_Snapdragon_X_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/**
 * Common system register values for Snapdragon(R) X Elite - X1E80100 - Qualcomm(R) Oryon(TM) CPU.
 */
static SUPARMSYSREGVAL const g_aCmnSysRegVals_ARM_Qualcomm_Snapdragon_X[] =
{
    { UINT64_C(0x0000000080000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 5), 0x1 }, /* MPIDR_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 6), 0x0 }, /* REVIDR_EL1 */
    { UINT64_C(0x1101201021111111), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 0), 0x0 }, /* ID_AA64PFR0_EL1 */
    { UINT64_C(0x0000000000000020), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 1), 0x0 }, /* ID_AA64PFR1_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 2), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 3), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 4), 0x0 }, /* ID_AA64ZFR0_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 5), 0x0 }, /* ID_AA64SMFR0_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 6), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 7), 0x0 },
    { UINT64_C(0x000000f010305709), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 0), 0x0 }, /* ID_AA64DFR0_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 1), 0x0 }, /* ID_AA64DFR1_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 2), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 3), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 4), 0x0 }, /* ID_AA64AFR0_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 5), 0x0 }, /* ID_AA64AFR1_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 6), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 7), 0x0 },
    { UINT64_C(0x1221111110212120), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 0), 0x0 }, /* ID_AA64ISAR0_EL1 */
    { UINT64_C(0x0010111101211052), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 1), 0x0 }, /* ID_AA64ISAR1_EL1 */
    { UINT64_C(0x0000000010000010), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 2), 0x0 }, /* ID_AA64ISAR2_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 3), 0x0 }, /* ID_AA64ISAR3_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 4), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 5), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 6), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 7), 0x0 },
    { UINT64_C(0x0000111100001024), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 0), 0x0 }, /* ID_AA64MMFR0_EL1 */
    { UINT64_C(0x0000101001201000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 1), 0x0 }, /* ID_AA64MMFR1_EL1 */
    { UINT64_C(0x1000001100101011), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 2), 0x0 }, /* ID_AA64MMFR2_EL1 */
    { UINT64_C(0x1000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 3), 0x0 }, /* ID_AA64MMFR3_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 4), 0x0 }, /* ID_AA64MMFR4_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 5), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 6), 0x0 },
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 7), 0x0 },
    { UINT64_C(0x0000000000000003), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 5, 3, 0), 0x0 }, /* ERRIDR_EL1 */
    { UINT64_C(0x00003fff0000005a), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1, 0, 0, 0), 0x0 }, /* CCSIDR_EL1 */
    { UINT64_C(0x0000000002000023), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1, 0, 0, 1), 0x0 }, /* CLIDR_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1, 0, 0, 2), 0x0 }, /* CCSIDR2_EL1 */
    { UINT64_C(0x0000000000000000), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1, 0, 0, 7), 0x0 }, /* AIDR_EL1 */
    { UINT64_C(0x0000000000000004), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 0, 0, 7), 0x0 }, /* DCZID_EL0 */
    { UINT64_C(0x000000000124f800), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3,14, 0, 0), 0x0 }, /* CNTFRQ_EL0 */
};


/**
 * System register values for Qualcomm Snapdragon X (Oryon var 2), variation #0.
 * 4 CPUs shares this variant: 0, 1, 2, 3
 */
static SUPARMSYSREGVAL const g_aVar0SysRegVals_ARM_Qualcomm_Snapdragon_X[] =
{
    { UINT64_C(0x00000000512f0011), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 0), 0x0 }, /* MIDR_EL1 */
};


/**
 * System register values for Qualcomm Snapdragon X (Oryon var 1), variation #1.
 * 8 CPUs shares this variant: 4, 5, 6, 7, 8, 9, 10, 11
 */
static SUPARMSYSREGVAL const g_aVar1SysRegVals_ARM_Qualcomm_Snapdragon_X[] =
{
    { UINT64_C(0x00000000511f0011), ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 0), 0x0 }, /* MIDR_EL1 */
};


/**
 * Database entry for Snapdragon(R) X Elite - X1E80100 - Qualcomm(R) Oryon(TM) CPU.
 */
static CPUMDBENTRYARM const g_Entry_ARM_Qualcomm_Snapdragon_X =
{
    {
        /*.pszName      = */ "Qualcomm Snapdragon X",
        /*.pszFullName  = */ "Snapdragon(R) X Elite - X1E80100 - Qualcomm(R) Oryon(TM) CPU",
        /*.enmVendor    = */ CPUMCPUVENDOR_QUALCOMM,
        /*.enmMicroarch = */ kCpumMicroarch_Qualcomm_Oryon,
        /*.fFlags       = */ 0,
        /*.enmEntryType = */ CPUMDBENTRYTYPE_ARM,
    },
    /*.paSysRegCmnVals  = */ NULL_ALONE(g_aCmnSysRegVals_ARM_Qualcomm_Snapdragon_X),
    /*.cSysRegCmnVals   = */ ZERO_ALONE(RT_ELEMENTS(g_aCmnSysRegVals_ARM_Qualcomm_Snapdragon_X)),
    /*.cVariants        = */ 2,
    /*.aVariants        = */
    {
        /*.Variants[0] = */
        {
            /*.pszName      = */ "Qualcomm Snapdragon X (Oryon var 2)",
            /*.Midr         = */
            {
                /*Midr.s = */
                {
                    /*.u4Revision    = */ 0x1,
                    /*.u12PartNum    = */ 0x001,
                    /*.u4Arch        = */ 0xf,
                    /*.u4Variant     = */ 0x2,
                    /*.u4Implementer = */ 0x51,
                }
            },
            /*.enmCoreType  = */ kCpumCoreType_Unknown,
            /*.cSysRegVals  = */ ZERO_ALONE(RT_ELEMENTS(g_aVar0SysRegVals_ARM_Qualcomm_Snapdragon_X)),
            /*.paSysRegVals = */ NULL_ALONE(g_aVar0SysRegVals_ARM_Qualcomm_Snapdragon_X)
        },
        /*.Variants[1] = */
        {
            /*.pszName      = */ "Qualcomm Snapdragon X (Oryon var 1)",
            /*.Midr         = */
            {
                /*Midr.s = */
                {
                    /*.u4Revision    = */ 0x1,
                    /*.u12PartNum    = */ 0x001,
                    /*.u4Arch        = */ 0xf,
                    /*.u4Variant     = */ 0x1,
                    /*.u4Implementer = */ 0x51,
                }
            },
            /*.enmCoreType  = */ kCpumCoreType_Unknown,
            /*.cSysRegVals  = */ ZERO_ALONE(RT_ELEMENTS(g_aVar1SysRegVals_ARM_Qualcomm_Snapdragon_X)),
            /*.paSysRegVals = */ NULL_ALONE(g_aVar1SysRegVals_ARM_Qualcomm_Snapdragon_X)
        },
    }
};

#endif /* !VBOX_CPUDB_ARM_Qualcomm_Snapdragon_X_h */

