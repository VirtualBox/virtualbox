/* $Id: CPUMR3-armv8.cpp 168915 2025-05-16 21:41:53Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU Monitor / Manager (ARMv8 variant).
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

/** @page pg_cpum CPUM - CPU Monitor / Manager
 *
 * The CPU Monitor / Manager keeps track of all the CPU registers.
 * This is the ARMv8 variant which is doing much less than its x86/AMD6464
 * counterpart due to the fact that we currently only support the NEM backends
 * for running ARM guests. It might become complex iff we decide to implement our
 * own hypervisor.
 *
 * @section sec_cpum_logging_armv8      Logging Level Assignments.
 *
 * Following log level assignments:
 *      - @todo
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_CPUM
#define CPUM_WITH_NONCONST_HOST_FEATURES
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/ssm.h>
#include "CPUMInternal-armv8.h"
#include <VBox/vmm/vm.h>

#include <VBox/param.h>
#include <VBox/dis.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/cpuset.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/string.h>
#include <iprt/armv8.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Internal form used by the macros. */
#ifdef VBOX_WITH_STATISTICS
# define RINT(a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName) \
    { a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, 0, 0, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName, \
      { 0 }, { 0 }, { 0 }, { 0 } }
#else
# define RINT(a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName) \
    { a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, 0, 0, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName }
#endif

/** Function handlers, extended version. */
#define MFX(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_uValue, a_fWrIgnMask, a_fWrGpMask) \
    RINT(a_uMsr, a_uMsr, kCpumSysRegRdFn_##a_enmRdFnSuff, kCpumSysRegWrFn_##a_enmWrFnSuff, 0, a_uValue, a_fWrIgnMask, a_fWrGpMask, a_szName)
/** Function handlers, read-only. */
#define MFO(a_uMsr, a_szName, a_enmRdFnSuff) \
    RINT(a_uMsr, a_uMsr, kCpumSysRegRdFn_##a_enmRdFnSuff, kCpumSysRegWrFn_ReadOnly, 0, 0, 0, UINT64_MAX, a_szName)
/** Read-only fixed value, ignores all writes. */
#define MVI(a_uMsr, a_szName, a_uValue) \
    RINT(a_uMsr, a_uMsr, kCpumSysRegRdFn_FixedValue, kCpumSysRegWrFn_IgnoreWrite, 0, a_uValue, UINT64_MAX, 0, a_szName)
/** Read/Write value from/to CPUMCTX. */
#define MVRW(a_uMsr, a_szName, a_offCpum) \
    RINT(a_uMsr, a_uMsr, kCpumSysRegRdFn_ReadCpumOff, kCpumSysRegWrFn_WriteCpumOff, (uint32_t)a_offCpum, 0, UINT64_MAX, 0, a_szName)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * System register ranges.
 */
static CPUMSYSREGRANGE const g_aSysRegRanges[] =
{
    MFX(ARMV8_AARCH64_SYSREG_OSLAR_EL1, "OSLAR_EL1", WriteOnly, OslarEl1, 0, UINT64_C(0xfffffffffffffffe), UINT64_C(0xfffffffffffffffe)),
    MFO(ARMV8_AARCH64_SYSREG_OSLSR_EL1, "OSLSR_EL1", OslsrEl1),
    MVI(ARMV8_AARCH64_SYSREG_OSDLR_EL1, "OSDLR_EL1", 0),
    MVRW(ARMV8_AARCH64_SYSREG_MDSCR_EL1,       "MDSCR_EL1",    RT_UOFFSETOF(CPUMCTX, Mdscr)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(0),  "DBGBVR0_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[0].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(1),  "DBGBVR1_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[1].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(2),  "DBGBVR2_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[2].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(3),  "DBGBVR3_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[3].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(4),  "DBGBVR4_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[4].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(5),  "DBGBVR5_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[5].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(6),  "DBGBVR6_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[6].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(7),  "DBGBVR7_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[7].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(8),  "DBGBVR8_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[8].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(9),  "DBGBVR9_EL9",  RT_UOFFSETOF(CPUMCTX, aBp[9].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(10), "DBGBVR10_EL1", RT_UOFFSETOF(CPUMCTX, aBp[10].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(11), "DBGBVR11_EL1", RT_UOFFSETOF(CPUMCTX, aBp[11].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(12), "DBGBVR12_EL1", RT_UOFFSETOF(CPUMCTX, aBp[12].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(13), "DBGBVR13_EL1", RT_UOFFSETOF(CPUMCTX, aBp[13].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(14), "DBGBVR14_EL1", RT_UOFFSETOF(CPUMCTX, aBp[14].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(15), "DBGBVR15_EL1", RT_UOFFSETOF(CPUMCTX, aBp[15].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(0),  "DBGBCR0_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[0].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(1),  "DBGBCR1_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[1].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(2),  "DBGBCR2_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[2].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(3),  "DBGBCR3_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[3].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(4),  "DBGBCR4_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[4].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(5),  "DBGBCR5_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[5].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(6),  "DBGBCR6_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[6].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(7),  "DBGBCR7_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[7].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(8),  "DBGBCR8_EL1",  RT_UOFFSETOF(CPUMCTX, aBp[8].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(9),  "DBGBCR9_EL9",  RT_UOFFSETOF(CPUMCTX, aBp[9].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(10), "DBGBCR10_EL1", RT_UOFFSETOF(CPUMCTX, aBp[10].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(11), "DBGBCR11_EL1", RT_UOFFSETOF(CPUMCTX, aBp[11].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(12), "DBGBCR12_EL1", RT_UOFFSETOF(CPUMCTX, aBp[12].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(13), "DBGBCR13_EL1", RT_UOFFSETOF(CPUMCTX, aBp[13].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(14), "DBGBCR14_EL1", RT_UOFFSETOF(CPUMCTX, aBp[14].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(15), "DBGBCR15_EL1", RT_UOFFSETOF(CPUMCTX, aBp[15].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(0),  "DBGWVR0_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[0].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(1),  "DBGWVR1_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[1].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(2),  "DBGWVR2_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[2].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(3),  "DBGWVR3_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[3].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(4),  "DBGWVR4_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[4].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(5),  "DBGWVR5_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[5].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(6),  "DBGWVR6_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[6].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(7),  "DBGWVR7_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[7].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(8),  "DBGWVR8_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[8].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(9),  "DBGWVR9_EL9",  RT_UOFFSETOF(CPUMCTX, aWp[9].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(10), "DBGWVR10_EL1", RT_UOFFSETOF(CPUMCTX, aWp[10].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(11), "DBGWVR11_EL1", RT_UOFFSETOF(CPUMCTX, aWp[11].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(12), "DBGWVR12_EL1", RT_UOFFSETOF(CPUMCTX, aWp[12].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(13), "DBGWVR13_EL1", RT_UOFFSETOF(CPUMCTX, aWp[13].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(14), "DBGWVR14_EL1", RT_UOFFSETOF(CPUMCTX, aWp[14].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(15), "DBGWVR15_EL1", RT_UOFFSETOF(CPUMCTX, aWp[15].Value)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(0),  "DBGWCR0_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[0].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(1),  "DBGWCR1_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[1].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(2),  "DBGWCR2_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[2].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(3),  "DBGWCR3_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[3].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(4),  "DBGWCR4_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[4].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(5),  "DBGWCR5_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[5].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(6),  "DBGWCR6_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[6].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(7),  "DBGWCR7_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[7].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(8),  "DBGWCR8_EL1",  RT_UOFFSETOF(CPUMCTX, aWp[8].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(9),  "DBGWCR9_EL9",  RT_UOFFSETOF(CPUMCTX, aWp[9].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(10), "DBGWCR10_EL1", RT_UOFFSETOF(CPUMCTX, aWp[10].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(11), "DBGWCR11_EL1", RT_UOFFSETOF(CPUMCTX, aWp[11].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(12), "DBGWCR12_EL1", RT_UOFFSETOF(CPUMCTX, aWp[12].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(13), "DBGWCR13_EL1", RT_UOFFSETOF(CPUMCTX, aWp[13].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(14), "DBGWCR14_EL1", RT_UOFFSETOF(CPUMCTX, aWp[14].Ctrl)),
    MVRW(ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(15), "DBGWCR15_EL1", RT_UOFFSETOF(CPUMCTX, aWp[15].Ctrl)),
};


/** Saved state field descriptors for CPUMCTX. */
static const SSMFIELD g_aCpumCtxFields[] =
{
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[0].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[1].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[2].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[3].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[4].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[5].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[6].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[7].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[8].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[9].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[10].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[11].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[12].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[13].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[14].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[15].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[16].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[17].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[18].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[19].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[20].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[21].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[22].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[23].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[24].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[25].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[26].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[27].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[28].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[29].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[30].x),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[0].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[1].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[2].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[3].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[4].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[5].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[6].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[7].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[8].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[9].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[10].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[11].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[12].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[13].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[14].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[15].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[16].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[17].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[18].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[19].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[20].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[21].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[22].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[23].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[24].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[25].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[26].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[27].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[28].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[29].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[30].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[31].v),
    SSMFIELD_ENTRY(         CPUMCTX, aSpReg[0].u64),
    SSMFIELD_ENTRY(         CPUMCTX, aSpReg[1].u64),
    SSMFIELD_ENTRY(         CPUMCTX, Pc.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Spsr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Elr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Sctlr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Tcr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Ttbr0.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Ttbr1.u64),
    SSMFIELD_ENTRY(         CPUMCTX, VBar.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[0].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[0].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[1].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[1].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[2].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[2].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[3].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[3].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[4].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[4].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[5].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[5].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[6].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[6].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[7].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[7].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[8].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[8].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[9].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[9].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[10].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[10].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[11].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[11].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[12].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[12].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[13].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[13].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[14].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[14].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[15].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[15].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[0].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[0].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[1].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[1].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[2].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[2].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[3].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[3].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[4].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[4].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[5].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[5].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[6].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[6].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[7].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[7].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[8].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[8].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[9].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[9].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[10].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[10].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[11].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[11].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[12].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[12].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[13].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[13].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[14].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[14].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[15].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[15].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Mdscr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apda.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apda.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apdb.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apdb.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apga.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apga.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apia.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apia.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apib.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apib.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Afsr0.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Afsr1.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Amair.u64),
    SSMFIELD_ENTRY(         CPUMCTX, CntKCtl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, ContextIdr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Cpacr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Csselr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Esr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Far.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Mair.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Par.u64),
    SSMFIELD_ENTRY(         CPUMCTX, TpIdrRoEl0.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aTpIdr[0].u64),
    SSMFIELD_ENTRY(         CPUMCTX, aTpIdr[1].u64),
    SSMFIELD_ENTRY(         CPUMCTX, MDccInt.u64),
    SSMFIELD_ENTRY(         CPUMCTX, fpcr),
    SSMFIELD_ENTRY(         CPUMCTX, fpsr),
    SSMFIELD_ENTRY(         CPUMCTX, fPState),
    SSMFIELD_ENTRY(         CPUMCTX, fOsLck),
    SSMFIELD_ENTRY(         CPUMCTX, CntvCtlEl0),
    SSMFIELD_ENTRY(         CPUMCTX, CntvCValEl0),
    /** @name EL2 support:
     * @{ */
    SSMFIELD_ENTRY(         CPUMCTX, CntHCtlEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CntHpCtlEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CntHpCValEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CntHpTValEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CntVOffEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CptrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, ElrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, EsrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, FarEl2),
    SSMFIELD_ENTRY(         CPUMCTX, HcrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, HpFarEl2),
    SSMFIELD_ENTRY(         CPUMCTX, MairEl2),
    SSMFIELD_ENTRY(         CPUMCTX, MdcrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, SctlrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, SpsrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, SpEl2),
    SSMFIELD_ENTRY(         CPUMCTX, TcrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, TpidrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, Ttbr0El2),
    SSMFIELD_ENTRY(         CPUMCTX, Ttbr1El2),
    SSMFIELD_ENTRY(         CPUMCTX, VBarEl2),
    SSMFIELD_ENTRY(         CPUMCTX, VMpidrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, VPidrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, VTcrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, VTtbrEl2),
    /** @} */

    SSMFIELD_ENTRY_TERM()
};

/**
 * Additional fields for v2
 */
static const SSMFIELD g_aCpumCtxFieldsV2[] =
{
    SSMFIELD_ENTRY(         CPUMCTX, Actlr.u64),
    SSMFIELD_ENTRY_TERM()
};



/*********************************************************************************************************************************
*   Initialization, Reset & Termination                                                                                          *
*********************************************************************************************************************************/


/**
 * Initializes the guest system register states.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
static int cpumR3InitSysRegs(PVM pVM)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aSysRegRanges); i++)
    {
        int rc = CPUMR3SysRegRangesInsert(pVM, &g_aSysRegRanges[i]);
        AssertLogRelRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}


DECLHIDDEN(int) cpumR3InitTarget(PVM pVM)
{
    LogFlow(("cpumR3InitTarget\n"));

    pVM->cpum.s.GuestInfo.paSysRegRangesR3 = &pVM->cpum.s.GuestInfo.aSysRegRanges[0];
    pVM->cpum.s.bResetEl                   = ARMV8_AARCH64_EL_1;

    PCFGMNODE   pCpumCfg = CFGMR3GetChild(CFGMR3GetRoot(pVM), "CPUM");

    /** @cfgm{/CPUM/ResetPcValue, string}
     * Program counter value after a reset, sets the address of the first instruction to execute. */
    int rc = CFGMR3QueryU64Def(pCpumCfg, "ResetPcValue", &pVM->cpum.s.u64ResetPc, 0);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/NestedHWVirt, bool, false}
     * Whether to expose the hardware virtualization (EL2) feature to the guest.
     * The default is false, and when enabled requires a 64-bit CPU and a NEM backend
     * supporting it.
     */
    bool fNestedHWVirt = false;
    rc = CFGMR3QueryBoolDef(pCpumCfg, "NestedHWVirt", &fNestedHWVirt, false);
    AssertLogRelRCReturn(rc, rc);
    if (fNestedHWVirt)
        pVM->cpum.s.bResetEl = ARMV8_AARCH64_EL_2;

    /*
     * Initialize the Guest system register states.
     */
    rc = cpumR3InitSysRegs(pVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register ARM specific info items
     */
    DBGFR3InfoRegisterInternal(pVM, "cpufeat", "Displays the guest features.", &cpumR3CpuFeatInfo);

    return VINF_SUCCESS;
}


DECLHIDDEN(int) cpumR3InitCompletedRing3Target(PVM pVM)
{
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) cpumR3TermTarget(PVM pVM)
{
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Resets a virtual CPU.
 *
 * Used by CPUMR3Reset and CPU hot plugging.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure of the CPU that is
 *                  being reset.  This may differ from the current EMT.
 */
VMMR3DECL(void) CPUMR3ResetCpu(PVM pVM, PVMCPU pVCpu)
{
    RT_NOREF(pVM);

    /** @todo anything different for VCPU > 0? */
    PCPUMCTX pCtx = &pVCpu->cpum.s.Guest;

    /*
     * Initialize everything to ZERO first.
     */
    RT_BZERO(pCtx, sizeof(*pCtx));

    /* Start in Supervisor mode. */
    /** @todo Differentiate between Aarch64 and Aarch32 configuation. */
    pCtx->fPState =   ARMV8_SPSR_EL2_AARCH64_SET_EL(pVM->cpum.s.bResetEl)
                    | ARMV8_SPSR_EL2_AARCH64_SP
                    | ARMV8_SPSR_EL2_AARCH64_D
                    | ARMV8_SPSR_EL2_AARCH64_A
                    | ARMV8_SPSR_EL2_AARCH64_I
                    | ARMV8_SPSR_EL2_AARCH64_F;

    pCtx->Pc.u64 = pVM->cpum.s.u64ResetPc;
    /** @todo */
}



/*********************************************************************************************************************************
*   Saved State                                                                                                                  *
*********************************************************************************************************************************/

DECLCALLBACK(int) cpumR3LiveExecTarget(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass)
{
    AssertReturn(uPass == 0, VERR_SSM_UNEXPECTED_PASS);
    cpumR3SaveCpuId(pVM, pSSM);
    return VINF_SSM_DONT_CALL_AGAIN;
}


DECLCALLBACK(int) cpumR3SaveExecTarget(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Save.
     */
    SSMR3PutU32(pSSM, pVM->cCpus);
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU const   pVCpu   = pVM->apCpusR3[idCpu];
        PCPUMCTX const pGstCtx = &pVCpu->cpum.s.Guest;

        SSMR3PutStructEx(pSSM, pGstCtx, sizeof(*pGstCtx), 0, g_aCpumCtxFields, NULL);
        SSMR3PutStructEx(pSSM, pGstCtx, sizeof(*pGstCtx), 0, g_aCpumCtxFieldsV2, NULL);

        SSMR3PutU32(pSSM, pVCpu->cpum.s.fChanged);
    }

    cpumR3SaveCpuId(pVM, pSSM);
    return VINF_SUCCESS;
}


DECLCALLBACK(int) cpumR3LoadExecTarget(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    /*
     * Validate version.
     */
    if (   uVersion != CPUM_SAVED_STATE_VERSION
        && uVersion != CPUM_SAVED_STATE_VERSION_ARMV8_V1)
    {
        AssertMsgFailed(("cpumR3LoadExec: Invalid version uVersion=%d!\n", uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    if (uPass == SSM_PASS_FINAL)
    {
        uint32_t cCpus;
        int rc = SSMR3GetU32(pSSM, &cCpus); AssertRCReturn(rc, rc);
        AssertLogRelMsgReturn(cCpus == pVM->cCpus, ("Mismatching CPU counts: saved: %u; configured: %u \n", cCpus, pVM->cCpus),
                              VERR_SSM_UNEXPECTED_DATA);

        /*
         * Do the per-CPU restoring.
         */
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU   pVCpu   = pVM->apCpusR3[idCpu];
            PCPUMCTX pGstCtx = &pVCpu->cpum.s.Guest;

            /*
             * Restore the CPUMCTX structure.
             */
            rc = SSMR3GetStructEx(pSSM, pGstCtx, sizeof(*pGstCtx), 0, g_aCpumCtxFields, NULL);
            AssertRCReturn(rc, rc);

            if (uVersion == CPUM_SAVED_STATE_VERSION_ARMV8_V2)
            {
                rc = SSMR3GetStructEx(pSSM, pGstCtx, sizeof(*pGstCtx), 0, g_aCpumCtxFieldsV2, NULL);
                AssertRCReturn(rc, rc);
            }

            /*
             * Restore a couple of flags.
             */
            SSMR3GetU32(pSSM, &pVCpu->cpum.s.fChanged);
        }
    }

    pVM->cpum.s.fPendingRestore = false;

    /* Load CPUID and explode guest features. */
    return cpumR3LoadCpuIdArmV8(pVM, pSSM, uVersion);
}


/**
 * @callback_method_impl{FNSSMINTLOADDONE}
 */
DECLHIDDEN(int) cpumR3LoadDoneTarget(PVM pVM, PSSMHANDLE pSSM)
{
    RT_NOREF(pVM, pSSM);
    /** @todo */
    return VINF_SUCCESS;
}

/**
 * Formats the PSTATE value into mnemonics.
 *
 * @param   pszPState   Where to write the mnemonics. (Assumes sufficient buffer space.)
 * @param   fPState     The PSTATE value with both guest hardware and VBox
 *                      internal bits included.
 */
static void cpumR3InfoFormatPState(char *pszPState, uint32_t fPState)
{
    /*
     * Format the flags.
     */
    static const struct
    {
        const char *pszSet; const char *pszClear; uint32_t fFlag;
    }   s_aFlags[] =
    {
        { "SP", "nSP", ARMV8_SPSR_EL2_AARCH64_SP },
        { "M4", "nM4", ARMV8_SPSR_EL2_AARCH64_M4 },
        { "T",  "nT",  ARMV8_SPSR_EL2_AARCH64_T  },
        { "nF", "F",   ARMV8_SPSR_EL2_AARCH64_F  },
        { "nI", "I",   ARMV8_SPSR_EL2_AARCH64_I  },
        { "nA", "A",   ARMV8_SPSR_EL2_AARCH64_A  },
        { "nD", "D",   ARMV8_SPSR_EL2_AARCH64_D  },
        { "V",  "nV",  ARMV8_SPSR_EL2_AARCH64_V  },
        { "C",  "nC",  ARMV8_SPSR_EL2_AARCH64_C  },
        { "Z",  "nZ",  ARMV8_SPSR_EL2_AARCH64_Z  },
        { "N",  "nN",  ARMV8_SPSR_EL2_AARCH64_N  },
    };
    char *psz = pszPState;
    for (unsigned i = 0; i < RT_ELEMENTS(s_aFlags); i++)
    {
        const char *pszAdd = s_aFlags[i].fFlag & fPState ? s_aFlags[i].pszSet : s_aFlags[i].pszClear;
        if (pszAdd)
        {
            strcpy(psz, pszAdd);
            psz += strlen(pszAdd);
            *psz++ = ' ';
        }
    }
    psz[-1] = '\0';
}


DECLHIDDEN(void) cpumR3InfoOneTarget(PVM pVM, PCVMCPU pVCpu, PCDBGFINFOHLP pHlp, CPUMDUMPTYPE enmType)
{
    PCCPUMCTX const pCtx = &pVCpu->cpum.s.Guest;
    RT_NOREF(pVM);

    /*
     * Format the PSTATE.
     */
    char szPState[80];
    cpumR3InfoFormatPState(&szPState[0], pCtx->fPState);

    /*
     * Format the registers.
     */
    switch (enmType)
    {
        case CPUMDUMPTYPE_TERSE:
            if (CPUMIsGuestIn64BitCodeEx(pCtx))
                pHlp->pfnPrintf(pHlp,
                    " x0=%016RX64  x1=%016RX64  x2=%016RX64  x3=%016RX64\n"
                    " x4=%016RX64  x5=%016RX64  x6=%016RX64  x7=%016RX64\n"
                    " x8=%016RX64  x9=%016RX64 x10=%016RX64 x11=%016RX64\n"
                    "x12=%016RX64 x13=%016RX64 x14=%016RX64 x15=%016RX64\n"
                    "x16=%016RX64 x17=%016RX64 x18=%016RX64 x19=%016RX64\n"
                    "x20=%016RX64 x21=%016RX64 x22=%016RX64 x23=%016RX64\n"
                    "x24=%016RX64 x25=%016RX64 x26=%016RX64 x27=%016RX64\n"
                    "x28=%016RX64 x29=%016RX64 x30=%016RX64\n"
                    " pc=%016RX64 psr=%016RX64 %s\n"
                    "sp_el0=%016RX64 sp_el1=%016RX64\n",
                    pCtx->aGRegs[0],  pCtx->aGRegs[1],  pCtx->aGRegs[2],  pCtx->aGRegs[3],
                    pCtx->aGRegs[4],  pCtx->aGRegs[5],  pCtx->aGRegs[6],  pCtx->aGRegs[7],
                    pCtx->aGRegs[8],  pCtx->aGRegs[9],  pCtx->aGRegs[10], pCtx->aGRegs[11],
                    pCtx->aGRegs[12], pCtx->aGRegs[13], pCtx->aGRegs[14], pCtx->aGRegs[15],
                    pCtx->aGRegs[16], pCtx->aGRegs[17], pCtx->aGRegs[18], pCtx->aGRegs[19],
                    pCtx->aGRegs[20], pCtx->aGRegs[21], pCtx->aGRegs[22], pCtx->aGRegs[23],
                    pCtx->aGRegs[24], pCtx->aGRegs[25], pCtx->aGRegs[26], pCtx->aGRegs[27],
                    pCtx->aGRegs[28], pCtx->aGRegs[29], pCtx->aGRegs[30],
                    pCtx->Pc.u64, pCtx->fPState, szPState,
                    pCtx->aSpReg[0].u64, pCtx->aSpReg[1].u64);
            else
                AssertFailed();
            break;

        case CPUMDUMPTYPE_DEFAULT:
            if (CPUMIsGuestIn64BitCodeEx(pCtx))
                pHlp->pfnPrintf(pHlp,
                    " x0=%016RX64  x1=%016RX64  x2=%016RX64  x3=%016RX64\n"
                    " x4=%016RX64  x5=%016RX64  x6=%016RX64  x7=%016RX64\n"
                    " x8=%016RX64  x9=%016RX64 x10=%016RX64 x11=%016RX64\n"
                    "x12=%016RX64 x13=%016RX64 x14=%016RX64 x15=%016RX64\n"
                    "x16=%016RX64 x17=%016RX64 x18=%016RX64 x19=%016RX64\n"
                    "x20=%016RX64 x21=%016RX64 x22=%016RX64 x23=%016RX64\n"
                    "x24=%016RX64 x25=%016RX64 x26=%016RX64 x27=%016RX64\n"
                    "x28=%016RX64 x29=%016RX64 x30=%016RX64\n"
                    " pc=%016RX64 psr=%016RX64 %s\n"
                    "  sp_el0=%016RX64    sp_el1=%016RX64 sctlr_el1=%016RX64\n"
                    " tcr_el1=%016RX64 ttbr0_el1=%016RX64 ttbr1_el1=%016RX64\n"
                    "vbar_el1=%016RX64   elr_el1=%016RX64   esr_el1=%016RX64\n",
                    pCtx->aGRegs[0],  pCtx->aGRegs[1],  pCtx->aGRegs[2],  pCtx->aGRegs[3],
                    pCtx->aGRegs[4],  pCtx->aGRegs[5],  pCtx->aGRegs[6],  pCtx->aGRegs[7],
                    pCtx->aGRegs[8],  pCtx->aGRegs[9],  pCtx->aGRegs[10], pCtx->aGRegs[11],
                    pCtx->aGRegs[12], pCtx->aGRegs[13], pCtx->aGRegs[14], pCtx->aGRegs[15],
                    pCtx->aGRegs[16], pCtx->aGRegs[17], pCtx->aGRegs[18], pCtx->aGRegs[19],
                    pCtx->aGRegs[20], pCtx->aGRegs[21], pCtx->aGRegs[22], pCtx->aGRegs[23],
                    pCtx->aGRegs[24], pCtx->aGRegs[25], pCtx->aGRegs[26], pCtx->aGRegs[27],
                    pCtx->aGRegs[28], pCtx->aGRegs[29], pCtx->aGRegs[30],
                    pCtx->Pc.u64, pCtx->fPState, szPState,
                    pCtx->aSpReg[0].u64, pCtx->aSpReg[1].u64, pCtx->Sctlr.u64,
                    pCtx->Tcr.u64, pCtx->Ttbr0.u64, pCtx->Ttbr1.u64,
                    pCtx->VBar.u64, pCtx->Elr.u64, pCtx->Esr.u64);
            else
                AssertFailed();
            break;

        case CPUMDUMPTYPE_VERBOSE:
            if (CPUMIsGuestIn64BitCodeEx(pCtx))
                pHlp->pfnPrintf(pHlp,
                    " x0=%016RX64  x1=%016RX64  x2=%016RX64  x3=%016RX64\n"
                    " x4=%016RX64  x5=%016RX64  x6=%016RX64  x7=%016RX64\n"
                    " x8=%016RX64  x9=%016RX64 x10=%016RX64 x11=%016RX64\n"
                    "x12=%016RX64 x13=%016RX64 x14=%016RX64 x15=%016RX64\n"
                    "x16=%016RX64 x17=%016RX64 x18=%016RX64 x19=%016RX64\n"
                    "x20=%016RX64 x21=%016RX64 x22=%016RX64 x23=%016RX64\n"
                    "x24=%016RX64 x25=%016RX64 x26=%016RX64 x27=%016RX64\n"
                    "x28=%016RX64 x29=%016RX64 x30=%016RX64\n"
                    " pc=%016RX64 psr=%016RX64 %s\n"
                    "      sp_el0=%016RX64    sp_el1=%016RX64  sctlr_el1=%016RX64\n"
                    "     tcr_el1=%016RX64 ttbr0_el1=%016RX64  ttbr1_el1=%016RX64\n"
                    "    vbar_el1=%016RX64   elr_el1=%016RX64    esr_el1=%016RX64\n"
                    " tpidrr0_el0=%016RX64                        contextidr_el1=%016RX64\n"
                    "   tpidr_el0=%016RX64 tpidr_el1=%016RX64\n"
                    "     far_el1=%016RX64  mair_el1=%016RX64    par_el1=%016RX64\n"
                    "cntv_ctl_el0=%016RX64                          cntv_val_el0=%016RX64\n"
                    "   afsr0_el1=%016RX64 afsr0_el1=%016RX64  amair_el1=%016RX64\n"
                    " cntkctl_el1=%016RX64 cpacr_el1=%016RX64 csselr_el1=%016RX64\n"
                    " mdccint_el1=%016RX64\n",
                    pCtx->aGRegs[0],  pCtx->aGRegs[1],  pCtx->aGRegs[2],  pCtx->aGRegs[3],
                    pCtx->aGRegs[4],  pCtx->aGRegs[5],  pCtx->aGRegs[6],  pCtx->aGRegs[7],
                    pCtx->aGRegs[8],  pCtx->aGRegs[9],  pCtx->aGRegs[10], pCtx->aGRegs[11],
                    pCtx->aGRegs[12], pCtx->aGRegs[13], pCtx->aGRegs[14], pCtx->aGRegs[15],
                    pCtx->aGRegs[16], pCtx->aGRegs[17], pCtx->aGRegs[18], pCtx->aGRegs[19],
                    pCtx->aGRegs[20], pCtx->aGRegs[21], pCtx->aGRegs[22], pCtx->aGRegs[23],
                    pCtx->aGRegs[24], pCtx->aGRegs[25], pCtx->aGRegs[26], pCtx->aGRegs[27],
                    pCtx->aGRegs[28], pCtx->aGRegs[29], pCtx->aGRegs[30],
                    pCtx->Pc.u64, pCtx->fPState, szPState,
                    pCtx->aSpReg[0].u64, pCtx->aSpReg[1].u64, pCtx->Sctlr.u64,
                    pCtx->Tcr.u64, pCtx->Ttbr0.u64, pCtx->Ttbr1.u64,
                    pCtx->VBar.u64, pCtx->Elr.u64, pCtx->Esr.u64,
                    pCtx->TpIdrRoEl0.u64, pCtx->ContextIdr.u64,
                    pCtx->aTpIdr[0].u64, pCtx->aTpIdr[1].u64,
                    pCtx->Far.u64, pCtx->Mair.u64, pCtx->Par.u64,
                    pCtx->CntvCtlEl0, pCtx->CntvCValEl0,
                    pCtx->Afsr0.u64, pCtx->Afsr1.u64, pCtx->Amair.u64,
                    pCtx->CntKCtl.u64, pCtx->Cpacr.u64, pCtx->Csselr.u64,
                    pCtx->MDccInt.u64);
            else
                AssertFailed();

            pHlp->pfnPrintf(pHlp, "fpcr=%016RX64 fpsr=%016RX64\n", pCtx->fpcr, pCtx->fpsr);
            for (unsigned i = 0; i < RT_ELEMENTS(pCtx->aVRegs); i++)
                pHlp->pfnPrintf(pHlp,
                                i & 1
                                ? "q%u%s=%08RX32'%08RX32'%08RX32'%08RX32\n"
                                : "q%u%s=%08RX32'%08RX32'%08RX32'%08RX32  ",
                                i, i < 10 ? " " : "",
                                pCtx->aVRegs[i].au32[3],
                                pCtx->aVRegs[i].au32[2],
                                pCtx->aVRegs[i].au32[1],
                                pCtx->aVRegs[i].au32[0]);

            pHlp->pfnPrintf(pHlp, "mdscr_el1=%016RX64\n", pCtx->Mdscr.u64);
            for (unsigned i = 0; i < RT_ELEMENTS(pCtx->aBp); i++)
                pHlp->pfnPrintf(pHlp, "DbgBp%u%s: Control=%016RX64 Value=%016RX64\n",
                                i, i < 10 ? " " : "",
                                pCtx->aBp[i].Ctrl, pCtx->aBp[i].Value);

            for (unsigned i = 0; i < RT_ELEMENTS(pCtx->aWp); i++)
                pHlp->pfnPrintf(pHlp, "DbgWp%u%s: Control=%016RX64 Value=%016RX64\n",
                                i, i < 10 ? " " : "",
                                pCtx->aWp[i].Ctrl, pCtx->aWp[i].Value);

            pHlp->pfnPrintf(pHlp, "APDAKey=%016RX64'%016RX64\n", pCtx->Apda.High.u64, pCtx->Apda.Low.u64);
            pHlp->pfnPrintf(pHlp, "APDBKey=%016RX64'%016RX64\n", pCtx->Apdb.High.u64, pCtx->Apdb.Low.u64);
            pHlp->pfnPrintf(pHlp, "APGAKey=%016RX64'%016RX64\n", pCtx->Apga.High.u64, pCtx->Apga.Low.u64);
            pHlp->pfnPrintf(pHlp, "APIAKey=%016RX64'%016RX64\n", pCtx->Apia.High.u64, pCtx->Apia.Low.u64);
            pHlp->pfnPrintf(pHlp, "APIBKey=%016RX64'%016RX64\n", pCtx->Apib.High.u64, pCtx->Apib.Low.u64);
            break;
    }
}


DECLHIDDEN(void) cpumR3LogCpuIdAndMsrFeaturesTarget(PVM pVM)
{
    LogRel(("******************** CPU feature dump ***********************\n"));
    DBGFR3Info(pVM->pUVM, "cpufeat", "verbose", DBGFR3InfoLogRelHlp());
    LogRel(("\n"));
    DBGFR3_INFO_LOG_SAFE(pVM, "cpufeat", "verbose"); /* macro */
    LogRel(("***************** End of CPU feature dump *******************\n"));
}

