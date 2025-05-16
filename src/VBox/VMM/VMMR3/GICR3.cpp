/* $Id: GICR3.cpp 168898 2025-05-16 08:13:16Z ramshankar.venkataraman@oracle.com $ */
/** @file
 * GIC - Generic Interrupt Controller Architecture (GIC).
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_GIC
#include <VBox/log.h>
#include "GICInternal.h"
#include <VBox/vmm/pdmgic.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vm.h>

#include <iprt/armv8.h>
#include <iprt/mem.h>


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** GIC saved state version. */
#define GIC_SAVED_STATE_VERSION                     9

# define GIC_SYSREGRANGE(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumSysRegRdFn_GicIcc, kCpumSysRegWrFn_GicIcc, 0, 0, 0, 0, 0, 0, a_szName, { 0 }, { 0 }, { 0 }, { 0 } }


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * System register ranges for the GIC.
 */
static CPUMSYSREGRANGE const g_aSysRegRanges_GIC[] =
{
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_PMR_EL1,   ARMV8_AARCH64_SYSREG_ICC_PMR_EL1,     "ICC_PMR_EL1"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_IAR0_EL1,  ARMV8_AARCH64_SYSREG_ICC_AP0R3_EL1,   "ICC_IAR0_EL1 - ICC_AP0R3_EL1"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_AP1R0_EL1, ARMV8_AARCH64_SYSREG_ICC_NMIAR1_EL1,  "ICC_AP1R0_EL1 - ICC_NMIAR1_EL1"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_DIR_EL1,   ARMV8_AARCH64_SYSREG_ICC_SGI0R_EL1,   "ICC_DIR_EL1 - ICC_SGI0R_EL1"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_IAR1_EL1,  ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1, "ICC_IAR1_EL1 - ICC_IGRPEN1_EL1"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_SRE_EL2,   ARMV8_AARCH64_SYSREG_ICC_SRE_EL2,     "ICC_SRE_EL2"),
    GIC_SYSREGRANGE(ARMV8_AARCH64_SYSREG_ICC_SRE_EL3,   ARMV8_AARCH64_SYSREG_ICC_SRE_EL3,     "ICC_SRE_EL3")
};


/**
 * Dumps basic GIC state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) gicR3DbgInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PCGIC      pGic    = VM_TO_GIC(pVM);
    PPDMDEVINS pDevIns = pGic->CTX_SUFF(pDevIns);
    PCGICDEV   pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);

    pHlp->pfnPrintf(pHlp, "GIC:\n");
    pHlp->pfnPrintf(pHlp, "  uArchRev         = %u\n",      pGicDev->uArchRev);
    pHlp->pfnPrintf(pHlp, "  uArchRevMinor    = %u\n",      pGicDev->uArchRevMinor);
    pHlp->pfnPrintf(pHlp, "  uMaxSpi          = %u (upto IntId %u)\n", pGicDev->uMaxSpi, 32 * (pGicDev->uMaxSpi + 1));
    pHlp->pfnPrintf(pHlp, "  fExtSpi          = %RTbool\n", pGicDev->fExtSpi);
    pHlp->pfnPrintf(pHlp, "  uMaxExtSpi       = %u (upto IntId %u)\n", pGicDev->uMaxExtSpi,
                    GIC_INTID_RANGE_EXT_SPI_START - 1 + 32 * (pGicDev->uMaxExtSpi + 1));
    pHlp->pfnPrintf(pHlp, "  fExtPpi          = %RTbool\n", pGicDev->fExtPpi);
    pHlp->pfnPrintf(pHlp, "  uMaxExtPpi       = %u (upto IntId %u)\n", pGicDev->uMaxExtPpi,
                    pGicDev->uMaxExtPpi == GIC_REDIST_REG_TYPER_PPI_NUM_MAX_1087 ? 1087 : GIC_INTID_RANGE_EXT_PPI_LAST);
    pHlp->pfnPrintf(pHlp, "  fRangeSelSupport = %RTbool\n", pGicDev->fRangeSel);
    pHlp->pfnPrintf(pHlp, "  fNmi             = %RTbool\n", pGicDev->fNmi);
    pHlp->pfnPrintf(pHlp, "  fMbi             = %RTbool\n", pGicDev->fMbi);
    pHlp->pfnPrintf(pHlp, "  fAff3Levels      = %RTbool\n", pGicDev->fAff3Levels);
    pHlp->pfnPrintf(pHlp, "  fLpi             = %RTbool\n", pGicDev->fLpi);
}


/**
 * Gets alternate bits (starting at and including bit 1 to bit 63) from a 64-bit
 * source into a 32-bit value.
 *
 * @returns The 32-bit result with alternate bits from the 64-bit source.
 * @param uLo    The low 32-bits of the source value.
 * @param uHi    The high 32-bits of the source value.
 */
DECL_FORCE_INLINE(uint32_t) gicGetAltBits(uint32_t uLo, uint32_t uHi)
{
    uint64_t const uVal = RT_MAKE_U64(uLo, uHi);
    uint32_t uConfig = 0;
    for (unsigned i = 1; i < sizeof(uint64_t) * 8; i += 2)
        uConfig |= ((uVal >> i) & 1) << (i / 2);
    return uConfig;
}


/**
 * Dumps GIC Distributor information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) gicR3DbgInfoDist(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);

    PGIC pGic = VM_TO_GIC(pVM);
    PPDMDEVINS pDevIns = pGic->CTX_SUFF(pDevIns);
    PCGICDEV   pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);

    pHlp->pfnPrintf(pHlp, "GIC Distributor:\n");
    pHlp->pfnPrintf(pHlp, "  fIntrGroup0Enabled = %RTbool\n", pGicDev->fIntrGroup0Enabled);
    pHlp->pfnPrintf(pHlp, "  fIntrGroup1Enabled = %RTbool\n", pGicDev->fIntrGroup1Enabled);
    pHlp->pfnPrintf(pHlp, "  fAffRoutingEnabled = %RTbool\n", pGicDev->fAffRoutingEnabled);

#define GIC_DBGFINFO_DIST_INTR_BITMAP(a_Name, a_bmIntr) \
    do \
    { \
        pHlp->pfnPrintf(pHlp, "  " a_Name " =\n"); \
        for (uint32_t i = 0; i < RT_ELEMENTS(a_bmIntr); i += 8) \
            pHlp->pfnPrintf(pHlp, "    [%2u..%-2u] %#010x %#010x %#010x %#010x %#010x %#010x %#010x %#010x\n", i, i + 7, \
                            (a_bmIntr)[i],   (a_bmIntr)[i+1], (a_bmIntr)[i+2], (a_bmIntr)[i+3],  \
                            (a_bmIntr)[i+4], (a_bmIntr)[i+5], (a_bmIntr)[i+6], (a_bmIntr)[i+7]); \
    } while (0)
    GIC_DBGFINFO_DIST_INTR_BITMAP("bmIntrGroup",   pGicDev->bmIntrGroup);
    GIC_DBGFINFO_DIST_INTR_BITMAP("bmIntrEnabled", pGicDev->bmIntrEnabled);
    GIC_DBGFINFO_DIST_INTR_BITMAP("bmIntrPending", pGicDev->bmIntrPending);
    GIC_DBGFINFO_DIST_INTR_BITMAP("bmIntrActive",  pGicDev->bmIntrActive);
#undef GIC_DBGFINFO_DIST_INTR_BITMAP

    /* Interrupt config (edge-triggered or level-sensitive). */
    {
        pHlp->pfnPrintf(pHlp, "  Interrupt configs:\n");
        for (uint32_t i = 0; i < RT_ELEMENTS(pGicDev->bmIntrConfig); i += 16)
        {
            uint32_t const bmCfg0 = gicGetAltBits(pGicDev->bmIntrConfig[i],      pGicDev->bmIntrConfig[i + 1]);
            uint32_t const bmCfg1 = gicGetAltBits(pGicDev->bmIntrConfig[i + 2],  pGicDev->bmIntrConfig[i + 3]);
            uint32_t const bmCfg2 = gicGetAltBits(pGicDev->bmIntrConfig[i + 4],  pGicDev->bmIntrConfig[i + 5]);
            uint32_t const bmCfg3 = gicGetAltBits(pGicDev->bmIntrConfig[i + 6],  pGicDev->bmIntrConfig[i + 7]);
            uint32_t const bmCfg4 = gicGetAltBits(pGicDev->bmIntrConfig[i + 8],  pGicDev->bmIntrConfig[i + 9]);
            uint32_t const bmCfg5 = gicGetAltBits(pGicDev->bmIntrConfig[i + 10], pGicDev->bmIntrConfig[i + 11]);
            uint32_t const bmCfg6 = gicGetAltBits(pGicDev->bmIntrConfig[i + 12], pGicDev->bmIntrConfig[i + 13]);
            uint32_t const bmCfg7 = gicGetAltBits(pGicDev->bmIntrConfig[i + 14], pGicDev->bmIntrConfig[i + 15]);
            pHlp->pfnPrintf(pHlp, "    [%2u..%-2u] %#010x %#010x %#010x %#010x %#010x %#010x %#010x %#010x\n", i / 2, i / 2 + 7,
                                  bmCfg0, bmCfg1, bmCfg2, bmCfg3, bmCfg4, bmCfg5, bmCfg6, bmCfg7);
        }
    }

    /* Interrupt priorities.*/
    {
        uint32_t const cPriorities = RT_ELEMENTS(pGicDev->abIntrPriority);
        AssertCompile(!(cPriorities % 16));
        pHlp->pfnPrintf(pHlp, "  Interrupt priorities:\n");
        for (uint32_t i = 0; i < cPriorities; i += 16)
            pHlp->pfnPrintf(pHlp, "    IntId[%4u..%-4u] = %3u %3u %3u %3u %3u %3u %3u %3u"
                                  "    IntId[%4u..%-4u] = %3u %3u %3u %3u %3u %3u %3u %3u\n",
                                  gicDistGetIntIdFromIndex(i),     gicDistGetIntIdFromIndex(i + 7),
                                  pGicDev->abIntrPriority[i],      pGicDev->abIntrPriority[i + 1],
                                  pGicDev->abIntrPriority[i + 2],  pGicDev->abIntrPriority[i + 3],
                                  pGicDev->abIntrPriority[i + 4],  pGicDev->abIntrPriority[i + 5],
                                  pGicDev->abIntrPriority[i + 6],  pGicDev->abIntrPriority[i + 7],
                                  gicDistGetIntIdFromIndex(i + 8), gicDistGetIntIdFromIndex(i + 15),
                                  pGicDev->abIntrPriority[i + 8],  pGicDev->abIntrPriority[i + 9],
                                  pGicDev->abIntrPriority[i + 10], pGicDev->abIntrPriority[i + 11],
                                  pGicDev->abIntrPriority[i + 12], pGicDev->abIntrPriority[i + 13],
                                  pGicDev->abIntrPriority[i + 14], pGicDev->abIntrPriority[i + 15]);
    }

    /* Interrupt routing.*/
    {
        uint32_t const cRouting = RT_ELEMENTS(pGicDev->au32IntrRouting);
        AssertCompile(!(cRouting % 16));
        pHlp->pfnPrintf(pHlp, "  Interrupt routing:\n");
        for (uint32_t i = 0; i < cRouting; i += 16)
        {
            uint8_t const cBits  = sizeof(pGicDev->bmIntrRoutingMode[0]) * 8;
            uint8_t const idxIrm = i / cBits;
            uint8_t const iBit   = i % cBits;
            Assert(idxIrm < RT_ELEMENTS(pGicDev->bmIntrRoutingMode));   /* Paranoia. */
            pHlp->pfnPrintf(pHlp, "    IntId[%4u..%-4u] = %u:%u %u:%u %u:%u %u:%u %u:%u %u:%u %u:%u %u:%u"
                                  "    IntId[%4u..%-4u] = %u:%u %u:%u %u:%u %u:%u %u:%u %u:%u %u:%u %u:%u\n",
                                  gicDistGetIntIdFromIndex(i),                            gicDistGetIntIdFromIndex(i + 7),
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 0),  pGicDev->au32IntrRouting[i],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 1),  pGicDev->au32IntrRouting[i + 1],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 2),  pGicDev->au32IntrRouting[i + 2],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 3),  pGicDev->au32IntrRouting[i + 3],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 4),  pGicDev->au32IntrRouting[i + 4],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 5),  pGicDev->au32IntrRouting[i + 5],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 6),  pGicDev->au32IntrRouting[i + 6],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 7),  pGicDev->au32IntrRouting[i + 7],
                                  gicDistGetIntIdFromIndex(i + 8),                        gicDistGetIntIdFromIndex(i + 15),
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 8),  pGicDev->au32IntrRouting[i + 8],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 9),  pGicDev->au32IntrRouting[i + 9],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 10), pGicDev->au32IntrRouting[i + 10],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 11), pGicDev->au32IntrRouting[i + 11],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 12), pGicDev->au32IntrRouting[i + 12],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 13), pGicDev->au32IntrRouting[i + 13],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 14), pGicDev->au32IntrRouting[i + 14],
                                  pGicDev->bmIntrRoutingMode[idxIrm] & RT_BIT(iBit + 15), pGicDev->au32IntrRouting[i + 15]);
        }
    }
}


/**
 * Dumps the GIC Redistributor information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) gicR3DbgInfoReDist(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    PCGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);

    pHlp->pfnPrintf(pHlp, "VCPU[%u] Redistributor:\n", pVCpu->idCpu);
    AssertCompile(RT_ELEMENTS(pGicCpu->bmIntrGroup)   >= 3);
    AssertCompile(RT_ELEMENTS(pGicCpu->bmIntrEnabled) >= 3);
    AssertCompile(RT_ELEMENTS(pGicCpu->bmIntrPending) >= 3);
    AssertCompile(RT_ELEMENTS(pGicCpu->bmIntrActive)  >= 3);

#define GIC_DBGFINFO_REDIST_INTR_BITMAPS_3(a_bmIntr) pGicCpu->a_bmIntr[0], pGicCpu->a_bmIntr[1], pGicCpu->a_bmIntr[2]
    pHlp->pfnPrintf(pHlp, "  bmIntrGroup[0..2]   = %#010x %#010x %#010x\n", GIC_DBGFINFO_REDIST_INTR_BITMAPS_3(bmIntrGroup));
    pHlp->pfnPrintf(pHlp, "  bmIntrEnabled[0..2] = %#010x %#010x %#010x\n", GIC_DBGFINFO_REDIST_INTR_BITMAPS_3(bmIntrEnabled));
    pHlp->pfnPrintf(pHlp, "  bmIntrPending[0..2] = %#010x %#010x %#010x\n", GIC_DBGFINFO_REDIST_INTR_BITMAPS_3(bmIntrPending));
    pHlp->pfnPrintf(pHlp, "  bmIntrActive[0..2]  = %#010x %#010x %#010x\n", GIC_DBGFINFO_REDIST_INTR_BITMAPS_3(bmIntrActive));
#undef GIC_DBGFINFO_REDIST_INTR_BITMAPS

    /* Interrupt config (edge-triggered or level-sensitive). */
    {
        uint32_t const bmCfg0 = gicGetAltBits(pGicCpu->bmIntrConfig[0], pGicCpu->bmIntrConfig[1]);
        uint32_t const bmCfg1 = gicGetAltBits(pGicCpu->bmIntrConfig[2], pGicCpu->bmIntrConfig[3]);
        uint32_t const bmCfg2 = gicGetAltBits(pGicCpu->bmIntrConfig[4], pGicCpu->bmIntrConfig[5]);
        pHlp->pfnPrintf(pHlp, "  Interrupt configs   = %#010x %#010x %#010x\n", bmCfg0, bmCfg1, bmCfg2);
    }

    /* Interrupt priorities. */
    {
        uint32_t const cPriorities = RT_ELEMENTS(pGicCpu->abIntrPriority);
        AssertCompile(!(cPriorities % 16));
        pHlp->pfnPrintf(pHlp, "  Interrupt priorities:\n");
        for (uint32_t i = 0; i < cPriorities; i += 16)
            pHlp->pfnPrintf(pHlp, "    IntId[%4u..%-4u] = %3u %3u %3u %3u %3u %3u %3u %3u"
                                  "    IntId[%4u..%-4u] = %3u %3u %3u %3u %3u %3u %3u %3u\n",
                                  gicReDistGetIntIdFromIndex(i),     gicReDistGetIntIdFromIndex(i + 7),
                                  pGicCpu->abIntrPriority[i],        pGicCpu->abIntrPriority[i + 1],
                                  pGicCpu->abIntrPriority[i + 2],    pGicCpu->abIntrPriority[i + 3],
                                  pGicCpu->abIntrPriority[i + 4],    pGicCpu->abIntrPriority[i + 5],
                                  pGicCpu->abIntrPriority[i + 6],    pGicCpu->abIntrPriority[i + 7],
                                  gicReDistGetIntIdFromIndex(i + 8), gicReDistGetIntIdFromIndex(i + 15),
                                  pGicCpu->abIntrPriority[i + 8],    pGicCpu->abIntrPriority[i + 9],
                                  pGicCpu->abIntrPriority[i + 10],   pGicCpu->abIntrPriority[i + 11],
                                  pGicCpu->abIntrPriority[i + 12],   pGicCpu->abIntrPriority[i + 13],
                                  pGicCpu->abIntrPriority[i + 14],   pGicCpu->abIntrPriority[i + 15]);
    }

    pHlp->pfnPrintf(pHlp, "\nVCPU[%u] ICC system register state:\n", pVCpu->idCpu);
    pHlp->pfnPrintf(pHlp, "  uIccCtlr            = %#RX64\n",  pGicCpu->uIccCtlr);
    pHlp->pfnPrintf(pHlp, "  fIntrGroup0Enabled  = %RTbool\n", pGicCpu->fIntrGroup0Enabled);
    pHlp->pfnPrintf(pHlp, "  fIntrGroup1Enabled  = %RTbool\n", pGicCpu->fIntrGroup1Enabled);
    pHlp->pfnPrintf(pHlp, "  bBinaryPtGroup0     = %#x\n",     pGicCpu->bBinaryPtGroup0);
    pHlp->pfnPrintf(pHlp, "  bBinaryPtGroup1     = %#x\n",     pGicCpu->bBinaryPtGroup1);
    pHlp->pfnPrintf(pHlp, "  idxRunningPriority  = %u\n",      pGicCpu->idxRunningPriority);
    pHlp->pfnPrintf(pHlp, "  Running priority    = %u\n",      pGicCpu->abRunningPriorities[pGicCpu->idxRunningPriority]);
    pHlp->pfnPrintf(pHlp, "  Priority mask       = %u\n",      pGicCpu->bIntrPriorityMask);

    /* Running interrupt priorities. */
    {
        uint32_t const cPriorities = RT_ELEMENTS(pGicCpu->abRunningPriorities);
        AssertCompile(!(cPriorities % 16));
        pHlp->pfnPrintf(pHlp, "  Running-interrupt priorities:\n");
        for (uint32_t i = 0; i < cPriorities; i += 16)
            pHlp->pfnPrintf(pHlp, "    [%3u..%-3u] = %3u %3u %3u %3u %3u %3u %3u %3u"
                                  "    [%3u..%-3u] = %3u %3u %3u %3u %3u %3u %3u %3u\n",
                                  i,                                    i + 7,
                                  pGicCpu->abRunningPriorities[i],      pGicCpu->abRunningPriorities[i + 1],
                                  pGicCpu->abRunningPriorities[i + 2],  pGicCpu->abRunningPriorities[i + 3],
                                  pGicCpu->abRunningPriorities[i + 4],  pGicCpu->abRunningPriorities[i + 5],
                                  pGicCpu->abRunningPriorities[i + 6],  pGicCpu->abRunningPriorities[i + 7],
                                  i + 8,                                i + 15,
                                  pGicCpu->abRunningPriorities[i + 8],  pGicCpu->abRunningPriorities[i + 9],
                                  pGicCpu->abRunningPriorities[i + 10], pGicCpu->abRunningPriorities[i + 11],
                                  pGicCpu->abRunningPriorities[i + 12], pGicCpu->abRunningPriorities[i + 13],
                                  pGicCpu->abRunningPriorities[i + 14], pGicCpu->abRunningPriorities[i + 15]);
    }

    AssertCompile(RT_ELEMENTS(pGicCpu->bmActivePriorityGroup0) >= 4);
    pHlp->pfnPrintf(pHlp, "  Active-interrupt priorities Group 0:\n");
    pHlp->pfnPrintf(pHlp, "    [0..3] = %#010x %#010x %#010x %#010x\n",
                          pGicCpu->bmActivePriorityGroup0[0], pGicCpu->bmActivePriorityGroup0[1],
                          pGicCpu->bmActivePriorityGroup0[2], pGicCpu->bmActivePriorityGroup0[3]);
    AssertCompile(RT_ELEMENTS(pGicCpu->bmActivePriorityGroup1) >= 4);
    pHlp->pfnPrintf(pHlp, "  Active-interrupt priorities Group 1:\n");
    pHlp->pfnPrintf(pHlp, "    [0..3] = %#010x %#010x %#010x %#010x\n",
                          pGicCpu->bmActivePriorityGroup1[0], pGicCpu->bmActivePriorityGroup1[1],
                          pGicCpu->bmActivePriorityGroup1[2], pGicCpu->bmActivePriorityGroup1[3]);
}


/**
 * Dumps the GIC ITS information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) gicR3DbgInfoIts(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PGIC       pGic    = VM_TO_GIC(pVM);
    PPDMDEVINS pDevIns = pGic->CTX_SUFF(pDevIns);
    PCGICDEV   pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);
    if (pGicDev->hMmioGits != NIL_IOMMMIOHANDLE)
        gitsR3DbgInfo(&pGicDev->Gits, pHlp);
    else
        pHlp->pfnPrintf(pHlp, "GIC ITS is not mapped/configured for the VM\n");
}


/**
 * Dumps the GIC LPI information.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) gicR3DbgInfoLpi(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PGIC       pGic    = VM_TO_GIC(pVM);
    PPDMDEVINS pDevIns = pGic->CTX_SUFF(pDevIns);
    PCGICDEV   pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);
    if (!pGicDev->fLpi)
    {
        pHlp->pfnPrintf(pHlp, "GIC LPI support is not enabled for the VM\n");
        return;
    }

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    PCGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);

    pHlp->pfnPrintf(pHlp, "GIC LPIs:\n");
    pHlp->pfnPrintf(pHlp, "  Enabled            = %RTbool\n", pGicDev->fEnableLpis);

    /* GICR_PENDBASER. */
    {
        uint64_t const uReg = pGicDev->uLpiPendingBaseReg.u;
        pHlp->pfnPrintf(pHlp, "  uLpiPendingBaseReg = %#RX64\n", uReg);
        pHlp->pfnPrintf(pHlp, "    Inner cache        = %#x\n",     RT_BF_GET(uReg, GIC_BF_REDIST_REG_PENDBASER_INNER_CACHE));
        pHlp->pfnPrintf(pHlp, "    Shareability       = %#x\n",     RT_BF_GET(uReg, GIC_BF_REDIST_REG_PENDBASER_SHAREABILITY));
        pHlp->pfnPrintf(pHlp, "    Phys addr          = %#RX64\n",  uReg & GIC_BF_REDIST_REG_PENDBASER_PHYS_ADDR_MASK);
        pHlp->pfnPrintf(pHlp, "    Outer cache        = %#x\n",     RT_BF_GET(uReg, GIC_BF_REDIST_REG_PENDBASER_OUTER_CACHE));
        pHlp->pfnPrintf(pHlp, "    Pending Table Zero = %RTbool\n", RT_BF_GET(uReg, GIC_BF_REDIST_REG_PENDBASER_PTZ));
    }

    /* GICR_PROPBASER. */
    {
        uint64_t const uReg   = pGicDev->uLpiConfigBaseReg.u;
        uint8_t const cIdBits = RT_BF_GET(uReg, GIC_BF_REDIST_REG_PROPBASER_ID_BITS);
        pHlp->pfnPrintf(pHlp, "  uLpiConfigBaseReg  = %#RX64\n", uReg);
        pHlp->pfnPrintf(pHlp, "    ID bits            = %#x (%u bits)\n", cIdBits, cIdBits > 0 ? cIdBits + 1 : 0);
        pHlp->pfnPrintf(pHlp, "    Inner cache        = %#x\n",    RT_BF_GET(uReg, GIC_BF_REDIST_REG_PROPBASER_INNER_CACHE));
        pHlp->pfnPrintf(pHlp, "    Shareability       = %#x\n",    RT_BF_GET(uReg, GIC_BF_REDIST_REG_PROPBASER_SHAREABILITY));
        pHlp->pfnPrintf(pHlp, "    Phys addr          = %#RX64\n", uReg & GIC_BF_REDIST_REG_PROPBASER_PHYS_ADDR_MASK);
        pHlp->pfnPrintf(pHlp, "    Outer cache        = %#x\n",    RT_BF_GET(uReg, GIC_BF_REDIST_REG_PROPBASER_OUTER_CACHE));
    }

    /* LPI CTE (Configuration Table Entries). */
    {
        uint32_t const cLpiCtes   = RT_ELEMENTS(pGicDev->abLpiConfig);
        uint32_t       cLpiCtesEn = 0;
        for (uint32_t i = 0; i < cLpiCtes; i++)
            if (RT_BF_GET(pGicDev->abLpiConfig[i], GIC_BF_LPI_CTE_ENABLE))
                ++cLpiCtesEn;

        pHlp->pfnPrintf(pHlp, "  LPI config table (capacity=%u entries, enabled=%u entries)%s\n", cLpiCtes, cLpiCtesEn,
                        cLpiCtesEn > 0 ? ":" : "");
        for (uint32_t i = 0; i < cLpiCtesEn; i++)
        {
            uint8_t const uLpiCte   = pGicDev->abLpiConfig[i];
            uint8_t const uPriority = RT_BF_GET(uLpiCte, GIC_BF_LPI_CTE_PRIORITY);
            pHlp->pfnPrintf(pHlp, "    [%4u]               = %#x (priority=%u)\n", i, uLpiCte, uPriority);
        }
    }

    /* Pending LPI registers. */
    pHlp->pfnPrintf(pHlp, "  LPI pending bitmap:\n");
    for (uint32_t i = 0; i < RT_ELEMENTS(pGicCpu->bmLpiPending); i += 8)
    {
        pHlp->pfnPrintf(pHlp, "    [%3u..%-3u] = %08RX64 %08RX64 %08RX64 %08RX64 %08RX64 %08RX64 %08RX64 %08RX64\n",
                              i,                             i + 7,
                              pGicCpu->bmLpiPending[i],      pGicCpu->bmLpiPending[i + 1],
                              pGicCpu->bmLpiPending[i + 2],  pGicCpu->bmLpiPending[i + 3],
                              pGicCpu->bmLpiPending[i + 4],  pGicCpu->bmLpiPending[i + 5],
                              pGicCpu->bmLpiPending[i + 6],  pGicCpu->bmLpiPending[i + 7]);
    }
}


/**
 * The GIC ITS command-queue thread.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThread     The command thread.
 */
static DECLCALLBACK(int) gicItsR3CmdQueueThread(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    PGICDEV  pGicDev  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    PGITSDEV pGitsDev = &pGicDev->Gits;
    AssertPtrReturn(pGicDev, VERR_INVALID_PARAMETER);
    LogFlowFunc(("Command-queue thread spawned and initialized\n"));

    /*
     * Pre-allocate the maximum size of the command queue allowed by the ARM GIC spec.
     * This prevents trashing the heap as well as dealing with out-of-memory situations
     * up-front while starting the VM. It also simplifies the code from having to
     * dynamically grow/shrink the allocation based on how software sizes the queue.
     * Guests normally don't alter the queue size all the time, but that's not an
     * assumption we can make. Another benefit is that we can avoid releasing and
     * re-acquiring the device critical section if/when guests modifies the command
     * queue size.
     */
    uint16_t const cMaxPages = GITS_BF_CTRL_REG_CBASER_SIZE_MASK + 1;
    size_t const   cbCmds    = cMaxPages << GITS_CMD_QUEUE_PAGE_SHIFT;
    void *pvCmds = RTMemAllocZ(cbCmds);
    AssertLogRelMsgReturn(pvCmds, ("Failed to alloc %.Rhcb (%zu bytes) for the GITS command queue\n", cbCmds, cbCmds),
                          VERR_NO_MEMORY);

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        /* Sleep until we are woken up. */
        {
            int const rcLock = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pGitsDev->hEvtCmdQueue, RT_INDEFINITE_WAIT);
            AssertLogRelMsgReturnStmt(RT_SUCCESS(rcLock) || rcLock == VERR_INTERRUPTED, ("%Rrc\n", rcLock),
                                      RTMemFree(pvCmds), rcLock);
            if (pThread->enmState != PDMTHREADSTATE_RUNNING)
                break;
        }

        /* Process the command queue. */
        int const rc = gitsR3CmdQueueProcess(pDevIns, pGitsDev, pvCmds, cbCmds);
        if (RT_FAILURE(rc))
            break;
    }

    RTMemFree(pvCmds);

    LogFlowFunc(("Command-queue thread terminating\n"));
    return VINF_SUCCESS;
}


/**
 * Wakes up the command-queue thread so it can respond to a state change.
 *
 * @return VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThread     The command-queue thread.
 *
 * @thread EMT.
 */
static DECLCALLBACK(int) gicItsR3CmdQueueThreadWakeUp(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    LogFlowFunc(("\n"));
    PGICDEV  pGicDev  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    PGITSDEV pGitsDev = &pGicDev->Gits;
    return PDMDevHlpSUPSemEventSignal(pDevIns, pGitsDev->hEvtCmdQueue);
}


/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) gicR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PCVM          pVM     = PDMDevHlpGetVM(pDevIns);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;
    PCGICDEV      pGicDev = PDMDEVINS_2_DATA(pDevIns, PCGICDEV);
    AssertPtrReturn(pVM, VERR_INVALID_VM_HANDLE);
    LogFlowFunc(("\n"));

    /*
     * Save per-VM data.
     */
    pHlp->pfnSSMPutU32(pSSM,  pVM->cCpus);
    pHlp->pfnSSMPutU8(pSSM,   pGicDev->uArchRev);
    pHlp->pfnSSMPutU8(pSSM,   pGicDev->uArchRevMinor);
    pHlp->pfnSSMPutU8(pSSM,   pGicDev->uMaxSpi);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fExtSpi);
    pHlp->pfnSSMPutU8(pSSM,   pGicDev->uMaxExtSpi);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fExtPpi);
    pHlp->pfnSSMPutU8(pSSM,   pGicDev->uMaxExtPpi);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fRangeSel);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fNmi);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fMbi);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fAff3Levels);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fLpi);

    /* Distributor state. */
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fIntrGroup0Enabled);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fIntrGroup1Enabled);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fAffRoutingEnabled);
    pHlp->pfnSSMPutMem(pSSM,  &pGicDev->bmIntrGroup[0],       sizeof(pGicDev->bmIntrGroup));
    pHlp->pfnSSMPutMem(pSSM,  &pGicDev->bmIntrConfig[0],      sizeof(pGicDev->bmIntrConfig));
    pHlp->pfnSSMPutMem(pSSM,  &pGicDev->bmIntrEnabled[0],     sizeof(pGicDev->bmIntrEnabled));
    pHlp->pfnSSMPutMem(pSSM,  &pGicDev->bmIntrPending[0],     sizeof(pGicDev->bmIntrPending));
    pHlp->pfnSSMPutMem(pSSM,  &pGicDev->bmIntrActive[0],      sizeof(pGicDev->bmIntrActive));
    pHlp->pfnSSMPutMem(pSSM,  &pGicDev->abIntrPriority[0],    sizeof(pGicDev->abIntrPriority));
    pHlp->pfnSSMPutMem(pSSM,  &pGicDev->au32IntrRouting[0],   sizeof(pGicDev->au32IntrRouting));
    pHlp->pfnSSMPutMem(pSSM,  &pGicDev->bmIntrRoutingMode[0], sizeof(pGicDev->bmIntrRoutingMode));

    /* LPI state. */
    /* We store the size followed by the data because we currently do not support the full LPI range. */
    pHlp->pfnSSMPutU32(pSSM,  RT_SIZEOFMEMB(GICCPU, bmLpiPending));
    pHlp->pfnSSMPutU32(pSSM,  sizeof(pGicDev->abLpiConfig));
    pHlp->pfnSSMPutMem(pSSM,  &pGicDev->abLpiConfig[0],       sizeof(pGicDev->abLpiConfig));
    pHlp->pfnSSMPutU64(pSSM,  pGicDev->uLpiConfigBaseReg.u);
    pHlp->pfnSSMPutU64(pSSM,  pGicDev->uLpiPendingBaseReg.u);
    pHlp->pfnSSMPutBool(pSSM, pGicDev->fEnableLpis);

    /** @todo GITS data. */

    /*
     * Save per-VCPU data.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PCGICCPU pGicCpu = VMCPU_TO_GICCPU(pVM->apCpusR3[idCpu]);
        Assert(pGicCpu);

        /* Redistributor state. */
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->bmIntrGroup[0],    sizeof(pGicCpu->bmIntrGroup));
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->bmIntrConfig[0],   sizeof(pGicCpu->bmIntrConfig));
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->bmIntrEnabled[0],  sizeof(pGicCpu->bmIntrEnabled));
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->bmIntrPending[0],  sizeof(pGicCpu->bmIntrPending));
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->bmIntrActive[0],   sizeof(pGicCpu->bmIntrActive));
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->abIntrPriority[0], sizeof(pGicCpu->abIntrPriority));

        /* ICC system register state. */
        pHlp->pfnSSMPutU64(pSSM,  pGicCpu->uIccCtlr);
        pHlp->pfnSSMPutU8(pSSM,   pGicCpu->bIntrPriorityMask);
        pHlp->pfnSSMPutU8(pSSM,   pGicCpu->idxRunningPriority);
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->abRunningPriorities[0],    sizeof(pGicCpu->abRunningPriorities));
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->bmActivePriorityGroup0[0], sizeof(pGicCpu->bmActivePriorityGroup0));
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->bmActivePriorityGroup1[0], sizeof(pGicCpu->bmActivePriorityGroup1));
        pHlp->pfnSSMPutU8(pSSM,   pGicCpu->bBinaryPtGroup0);
        pHlp->pfnSSMPutU8(pSSM,   pGicCpu->bBinaryPtGroup1);
        pHlp->pfnSSMPutBool(pSSM, pGicCpu->fIntrGroup0Enabled);
        pHlp->pfnSSMPutBool(pSSM, pGicCpu->fIntrGroup1Enabled);

        /* LPI state. */
        pHlp->pfnSSMPutMem(pSSM, &pGicCpu->bmLpiPending[0], sizeof(pGicCpu->bmLpiPending));
    }

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX);
}


/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) gicR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PVM           pVM  = PDMDevHlpGetVM(pDevIns);
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    AssertPtrReturn(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(uPass == SSM_PASS_FINAL, VERR_WRONG_ORDER);
    LogFlowFunc(("uVersion=%u uPass=%#x\n", uVersion, uPass));

    /*
     * Validate supported saved-state versions.
     */
    if (uVersion != GIC_SAVED_STATE_VERSION)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Invalid saved-state version %u"), uVersion);

    /*
     * Load per-VM data.
     */
    uint32_t cCpus;
    pHlp->pfnSSMGetU32(pSSM,  &cCpus);
    if (cCpus != pVM->cCpus)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: cCpus: got=%u expected=%u"), cCpus, pVM->cCpus);

    PGICDEV pGicDev = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    pHlp->pfnSSMGetU8(pSSM,   &pGicDev->uArchRev);
    pHlp->pfnSSMGetU8(pSSM,   &pGicDev->uArchRevMinor);
    pHlp->pfnSSMGetU8(pSSM,   &pGicDev->uMaxSpi);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fExtSpi);
    pHlp->pfnSSMGetU8(pSSM,   &pGicDev->uMaxExtSpi);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fExtPpi);
    pHlp->pfnSSMGetU8(pSSM,   &pGicDev->uMaxExtPpi);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fRangeSel);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fNmi);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fMbi);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fAff3Levels);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fLpi);

    /* Distributor state. */
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fIntrGroup0Enabled);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fIntrGroup1Enabled);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fAffRoutingEnabled);
    pHlp->pfnSSMGetMem(pSSM,  &pGicDev->bmIntrGroup[0],       sizeof(pGicDev->bmIntrGroup));
    pHlp->pfnSSMGetMem(pSSM,  &pGicDev->bmIntrConfig[0],      sizeof(pGicDev->bmIntrConfig));
    pHlp->pfnSSMGetMem(pSSM,  &pGicDev->bmIntrEnabled[0],     sizeof(pGicDev->bmIntrEnabled));
    pHlp->pfnSSMGetMem(pSSM,  &pGicDev->bmIntrPending[0],     sizeof(pGicDev->bmIntrPending));
    pHlp->pfnSSMGetMem(pSSM,  &pGicDev->bmIntrActive[0],      sizeof(pGicDev->bmIntrActive));
    pHlp->pfnSSMGetMem(pSSM,  &pGicDev->abIntrPriority[0],    sizeof(pGicDev->abIntrPriority));
    pHlp->pfnSSMGetMem(pSSM,  &pGicDev->au32IntrRouting[0],   sizeof(pGicDev->au32IntrRouting));
    pHlp->pfnSSMGetMem(pSSM,  &pGicDev->bmIntrRoutingMode[0], sizeof(pGicDev->bmIntrRoutingMode));

    /* LPI state. */
    /* LPI pending bitmap size. */
    {
        uint32_t cbData = 0;
        int const rc = pHlp->pfnSSMGetU32(pSSM, &cbData);
        AssertRCReturn(rc, rc);
        if (cbData != RT_SIZEOFMEMB(GICCPU, bmLpiPending))
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: LPI pending bitmap size: got=%u expected=%u"),
                                           cbData, RT_SIZEOFMEMB(GICCPU, bmLpiPending));
    }
    /* LPI config table. */
    {
        uint32_t cbLpiConfig = 0;
        int const rc = pHlp->pfnSSMGetU32(pSSM, &cbLpiConfig);
        AssertRCReturn(rc, rc);
        if (cbLpiConfig != sizeof(pGicDev->abLpiConfig))
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: LPI config table size: got=%u expected=%u"),
                                           cbLpiConfig, sizeof(pGicDev->abLpiConfig));
        pHlp->pfnSSMGetMem(pSSM, &pGicDev->abLpiConfig[0], cbLpiConfig);
    }
    pHlp->pfnSSMGetU64(pSSM,  &pGicDev->uLpiConfigBaseReg.u);
    pHlp->pfnSSMGetU64(pSSM,  &pGicDev->uLpiPendingBaseReg.u);
    pHlp->pfnSSMGetBool(pSSM, &pGicDev->fEnableLpis);

    /** @todo GITS data. */

    /*
     * Load per-VCPU data.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVM->apCpusR3[idCpu]);
        Assert(pGicCpu);

        /* Redistributor state. */
        pHlp->pfnSSMGetMem(pSSM, &pGicCpu->bmIntrGroup[0],    sizeof(pGicCpu->bmIntrGroup));
        pHlp->pfnSSMGetMem(pSSM, &pGicCpu->bmIntrConfig[0],   sizeof(pGicCpu->bmIntrConfig));
        pHlp->pfnSSMGetMem(pSSM, &pGicCpu->bmIntrEnabled[0],  sizeof(pGicCpu->bmIntrEnabled));
        pHlp->pfnSSMGetMem(pSSM, &pGicCpu->bmIntrPending[0],  sizeof(pGicCpu->bmIntrPending));
        pHlp->pfnSSMGetMem(pSSM, &pGicCpu->bmIntrActive[0],   sizeof(pGicCpu->bmIntrActive));
        pHlp->pfnSSMGetMem(pSSM, &pGicCpu->abIntrPriority[0], sizeof(pGicCpu->abIntrPriority));

        /* ICC system register state. */
        pHlp->pfnSSMGetU64(pSSM,  &pGicCpu->uIccCtlr);
        pHlp->pfnSSMGetU8(pSSM,   &pGicCpu->bIntrPriorityMask);
        pHlp->pfnSSMGetU8(pSSM,   &pGicCpu->idxRunningPriority);
        pHlp->pfnSSMGetMem(pSSM,  &pGicCpu->abRunningPriorities[0],    sizeof(pGicCpu->abRunningPriorities));
        pHlp->pfnSSMGetMem(pSSM,  &pGicCpu->bmActivePriorityGroup0[0], sizeof(pGicCpu->bmActivePriorityGroup0));
        pHlp->pfnSSMGetMem(pSSM,  &pGicCpu->bmActivePriorityGroup1[0], sizeof(pGicCpu->bmActivePriorityGroup1));
        pHlp->pfnSSMGetU8(pSSM,   &pGicCpu->bBinaryPtGroup0);
        pHlp->pfnSSMGetU8(pSSM,   &pGicCpu->bBinaryPtGroup1);
        pHlp->pfnSSMGetBool(pSSM, &pGicCpu->fIntrGroup0Enabled);
        pHlp->pfnSSMGetBool(pSSM, &pGicCpu->fIntrGroup1Enabled);

        /* LPI pending. */
        pHlp->pfnSSMGetMem(pSSM, &pGicCpu->bmLpiPending[0], sizeof(pGicCpu->bmLpiPending));
    }

    /*
     * Check that we're still good wrt restored data.
     */
    int rc = pHlp->pfnSSMHandleGetStatus(pSSM);
    AssertRCReturn(rc, rc);

    uint32_t uMarker = 0;
    rc = pHlp->pfnSSMGetU32(pSSM, &uMarker);
    AssertRCReturn(rc, rc);
    if (uMarker == UINT32_MAX)
    { /* likely */ }
    else
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: Marker: got=%u expected=%u"), uMarker, UINT32_MAX);

    /*
     * Finally, perform sanity checks.
     */
    if (pGicDev->uArchRev <= GIC_DIST_REG_PIDR2_ARCHREV_GICV4)
    { /* likely */ }
    else
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Invalid uArchRev, got %u expected range [1,31]"), pGicDev->uArchRev,
                                       GIC_DIST_REG_PIDR2_ARCHREV_GICV1, GIC_DIST_REG_PIDR2_ARCHREV_GICV4);
    if (pGicDev->uArchRevMinor == 1)
    { /* likely */ }
    else
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Invalid uArchRevMinor, got %u expected 1"), pGicDev->uArchRevMinor);
    if (pGicDev->uMaxSpi - 1 < 31)
    { /* likely */ }
    else
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Invalid MaxSpi, got %u expected range [1,31]"), pGicDev->uMaxSpi);
    if (pGicDev->uMaxExtSpi <= 31)
    { /* likely */ }
    else
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Invalid MaxExtSpi, got %u expected range [0,31]"), pGicDev->uMaxExtSpi);
    if (   pGicDev->uMaxExtPpi == GIC_REDIST_REG_TYPER_PPI_NUM_MAX_1087
        || pGicDev->uMaxExtPpi == GIC_REDIST_REG_TYPER_PPI_NUM_MAX_1119)
    { /* likely */ }
    else
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Invalid MaxExtPpi, got %u expected range [1,2]"), pGicDev->uMaxExtPpi);
    bool const fIsGitsEnabled = RT_BOOL(pGicDev->hMmioGits != NIL_IOMMMIOHANDLE);
    if (fIsGitsEnabled == pGicDev->fLpi)
    { /* likely */ }
    else
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: LPIs are %s when ITS is %s"),
                                       fIsGitsEnabled ? "enabled" : "disabled", pGicDev->fLpi ? "enabled" : "disabled");
    return rc;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
DECLCALLBACK(void) gicR3Reset(PPDMDEVINS pDevIns)
{
    PVM pVM = PDMDevHlpGetVM(pDevIns);
    VM_ASSERT_EMT0(pVM);
    VM_ASSERT_IS_NOT_RUNNING(pVM);

    LogFlow(("GIC: gicR3Reset\n"));

    gicReset(pDevIns);
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpuDest = pVM->apCpusR3[idCpu];
        gicResetCpu(pDevIns, pVCpuDest);
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
DECLCALLBACK(int) gicR3Destruct(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("pDevIns=%p\n", pDevIns));
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    PGICDEV  pGicDev  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    PGITSDEV pGitsDev = &pGicDev->Gits;
    if (pGitsDev->hEvtCmdQueue != NIL_SUPSEMEVENT)
    {
        PDMDevHlpSUPSemEventClose(pDevIns, pGitsDev->hEvtCmdQueue);
        pGitsDev->hEvtCmdQueue = NIL_SUPSEMEVENT;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
DECLCALLBACK(int) gicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PGICDEV         pGicDev  = PDMDEVINS_2_DATA(pDevIns, PGICDEV);
    PCPDMDEVHLPR3   pHlp     = pDevIns->pHlpR3;
    PVM             pVM      = PDMDevHlpGetVM(pDevIns);
    PGIC            pGic     = VM_TO_GIC(pVM);
    Assert(iInstance == 0);

    /*
     * Init the data.
     */
    pGic->pDevInsR3 = pDevIns;

    /*
     * Validate GIC settings.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "DistributorMmioBase|RedistributorMmioBase|ItsMmioBase"
                                           "|ArchRev"
                                           "|ArchRevMinor"
                                           "|MaxSpi"
                                           "|ExtSpi"
                                           "|MaxExtSpi"
                                           "|ExtPpi"
                                           "|MaxExtPpi"
                                           "|RangeSel"
                                           "|Nmi"
                                           "|Mbi"
                                           "|Aff3Levels"
                                           "|Lpi"
                                           "|MaxLpi", "");

#if 0
    /*
     * Disable automatic PDM locking for this device.
     */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);
#endif

    /** @devcfgm{gic, ArchRev, uint8_t, 3}
     * Configures the GIC architecture revision (GICD_PIDR2.ArchRev, GICR_PIDR2.ArchRev
     * and GITS_PIDR2.ArchRev).
     *
     * Currently we only support GICv3 and the architecture revision reported is the
     * same for both the GIC and the ITS. */
    int rc = pHlp->pfnCFGMQueryU8Def(pCfg, "ArchRev", &pGicDev->uArchRev, 3);
    AssertLogRelRCReturn(rc, rc);
    if (pGicDev->uArchRev == GIC_DIST_REG_PIDR2_ARCHREV_GICV3)
    {
        AssertCompile(GIC_DIST_REG_PIDR2_ARCHREV_GICV3 == GITS_CTRL_REG_PIDR2_ARCHREV_GICV3);
        pGicDev->Gits.uArchRev = pGicDev->uArchRev;
    }
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("Configuration error: \"ArchRev\" must be %u, other revisions not supported"),
                                   GIC_DIST_REG_PIDR2_ARCHREV_GICV3);

    /** @devcfgm{gic, ArchRevMinor, uint8_t, 1}
     * Configures the GIC architecture revision minor version.
     *
     * Currently we support GICv3.1 only. GICv3.1's only addition to GICv3 is supported
     * for extended INTID ranges which we currently always support. */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "ArchRevMinor", &pGicDev->uArchRevMinor, 1);
    AssertLogRelRCReturn(rc, rc);
    if (pGicDev->uArchRevMinor == 1)
    { /* likely */ }
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("Configuration error: \"ArchRevMinor\" must be 1, other minor revisions not supported"));

    /** @devcfgm{gic, MaxSpi, uint8_t, 31}
     * Configures GICD_TYPER.ItLinesNumber.
     *
     * For the IntId range [32,1023], configures the maximum SPI supported. Valid values
     * are [1,31] which equates to interrupt IDs [63,1023]. A value of 0 implies SPIs
     * are not supported. We don't allow configuring this value as it's expected that
     * most guests would assume support for SPIs. */
    AssertCompile(GIC_DIST_REG_TYPER_NUM_ITLINES == 31);
    /** @todo This currently isn't implemented and the full range is always
     *        reported to the guest. */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "MaxSpi", &pGicDev->uMaxSpi, 31 /* Upto and incl. IntId 1023 */);
    AssertLogRelRCReturn(rc, rc);
    if (pGicDev->uMaxSpi - 1 < 31)
    { /* likely */ }
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("Configuration error: \"MaxSpi\" must be in the range [1,%u]"),
                                   GIC_DIST_REG_TYPER_NUM_ITLINES);

    /** @devcfgm{gic, ExtSpi, bool, false}
     * Configures whether extended SPIs supported is enabled (GICD_TYPER.ESPI). */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ExtSpi", &pGicDev->fExtSpi, true);
    AssertLogRelRCReturn(rc, rc);

    /** @devcfgm{gic, MaxExtSpi, uint8_t, 31}
     * Configures GICD_TYPER.ESPI_range.
     *
     * For the extended SPI range [4096,5119], configures the maximum extended SPI
     * supported. Valid values are [0,31] which equates to extended SPI IntIds
     * [4127,5119]. This is ignored (set to 0 in the register) when extended SPIs are
     * disabled. */
    AssertCompile(GIC_DIST_REG_TYPER_ESPI_RANGE >> GIC_DIST_REG_TYPER_ESPI_RANGE_BIT == 31);
    /** @todo This currently isn't implemented and the full range is always
     *        reported to the guest. */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "MaxExtSpi", &pGicDev->uMaxExtSpi, 31);
    AssertLogRelRCReturn(rc, rc);
    if (pGicDev->uMaxExtSpi <= 31)
    { /* likely */ }
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("Configuration error: \"MaxExtSpi\" must be in the range [0,31]"));

    /** @devcfgm{gic, ExtPpi, bool, true}
     * Configures whether extended PPIs support is enabled. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ExtPpi", &pGicDev->fExtPpi, true);
    AssertLogRelRCReturn(rc, rc);

    /** @devcfgm{gic, MaxExtPpi, uint8_t, 2}
     * Configures GICR_TYPER.PPInum.
     *
     * For the extended PPI range [1056,5119], configures the maximum extended PPI
     * supported. Valid values are [1,2] which equates to extended PPI IntIds
     * [1087,1119]. This is unused when extended PPIs are disabled. */
    /** @todo This currently isn't implemented and the full range is always
     *        reported to the guest. */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "MaxExtPpi", &pGicDev->uMaxExtPpi, 2);
    AssertLogRelRCReturn(rc, rc);
    if (   pGicDev->uMaxExtPpi == GIC_REDIST_REG_TYPER_PPI_NUM_MAX_1087
        || pGicDev->uMaxExtPpi == GIC_REDIST_REG_TYPER_PPI_NUM_MAX_1119)
    { /* likely */ }
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("Configuration error: \"MaxExtPpi\" must be in the range [0,2]"));

    /** @devcfgm{gic, RangeSel, bool, true}
     * Configures whether range-selector support is enabled (GICD_TYPER.RSS and
     * ICC_CTLR_EL1.RSS). */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "RangeSel", &pGicDev->fRangeSel, true);
    AssertLogRelRCReturn(rc, rc);

    /** @devcfgm{gic, Nmi, bool, false}
     * Configures whether non-maskable interrupts (NMIs) are supported
     * (GICD_TYPER.NMI). */
    /** @todo NMIs are currently not implemented. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Nmi", &pGicDev->fNmi, false);
    AssertLogRelRCReturn(rc, rc);

    /** @devcfgm{gic, Mbi, bool, false}
     * Configures whether message-based interrupts (MBIs) are supported
     * (GICD_TYPER.MBIS).
     *
     * Guests typically can't use MBIs without an ITS. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Mbi", &pGicDev->fMbi, false);
    AssertLogRelRCReturn(rc, rc);

    /** @devcfgm{gic, Aff3Levels, bool, true}
     * Configures whether non-zero affinity 3 levels (A3V) are supported
     * (GICD_TYPER.A3V and ICC_CTLR.A3V). */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Aff3Levels", &pGicDev->fAff3Levels, true);
    AssertLogRelRCReturn(rc, rc);

    /** @devcfgm{gic, Lpi, bool, false}
     * Configures whether physical LPIs are supported (GICD_TYPER.LPIS and
     * GICR_TYPER.PLPIS).
     *
     * This currently requires an ITS as we do not support direction injection of
     * LPIs as most guests do not use them anyway. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Lpi", &pGicDev->fLpi, false);
    AssertLogRelRCReturn(rc, rc);

    /** @devcfgm{gic, MaxLpi, uint8_t, 14}
     * Configures GICD_TYPER.num_LPIs.
     *
     * For the physical LPI range [8192,65535], configures the number of physical LPI
     * supported. Valid values are [3,14] which equates to LPI IntIds 8192 to
     * [8207,40959]. A value of 15 or higher would exceed the maximum INTID size of
     * 16-bits since 8192 + 2^(NumLpi+1) is >= 73727. A value of 2 or lower support
     * fewer than 15 LPIs which seem pointless and is hence disallowed. This value is
     * ignored (set to 0 in the register) when LPIs are disabled. */
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "MaxLpi", &pGicDev->uMaxLpi, 11);
    AssertLogRelRCReturn(rc, rc);

    /* We currently support 4096 LPIs until we need to support more. */
    if (pGicDev->uMaxLpi == 11)
    { /* likely */ }
    else
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("Configuration error: \"MaxLpi\" must be in the range [3,14]"));
    AssertRelease(UINT32_C(2) << pGicDev->uMaxLpi <= RT_ELEMENTS(pGicDev->abLpiConfig));

    /*
     * Register the GIC with PDM.
     */
    rc = PDMDevHlpIcRegister(pDevIns);
    AssertLogRelRCReturn(rc, rc);

    rc = PDMGicRegisterBackend(pVM, PDMGICBACKENDTYPE_VBOX, &g_GicBackend);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Insert the GIC system registers.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aSysRegRanges_GIC); i++)
    {
        rc = CPUMR3SysRegRangesInsert(pVM, &g_aSysRegRanges_GIC[i]);
        AssertLogRelRCReturn(rc, rc);
    }

    /*
     * Register the MMIO ranges.
     */
    /* Distributor. */
    {
        RTGCPHYS GCPhysMmioBase = 0;
        rc = pHlp->pfnCFGMQueryU64(pCfg, "DistributorMmioBase", &GCPhysMmioBase);
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc,
                                    N_("Configuration error: Failed to get the \"DistributorMmioBase\" value"));

        rc = PDMDevHlpMmioCreateAndMap(pDevIns, GCPhysMmioBase, GIC_DIST_REG_FRAME_SIZE, gicDistMmioWrite, gicDistMmioRead,
                                       IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED, "GIC Distributor",
                                       &pGicDev->hMmioDist);
        AssertRCReturn(rc, rc);
    }

    /* Redistributor. */
    {
        RTGCPHYS GCPhysMmioBase = 0;
        rc = pHlp->pfnCFGMQueryU64(pCfg, "RedistributorMmioBase", &GCPhysMmioBase);
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc,
                                    N_("Configuration error: Failed to get the \"RedistributorMmioBase\" value"));

        RTGCPHYS const cbRegion = (RTGCPHYS)pVM->cCpus
                                * (GIC_REDIST_REG_FRAME_SIZE + GIC_REDIST_SGI_PPI_REG_FRAME_SIZE); /* Adjacent and per vCPU. */
        rc = PDMDevHlpMmioCreateAndMap(pDevIns, GCPhysMmioBase, cbRegion, gicReDistMmioWrite, gicReDistMmioRead,
                                       IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED, "GIC Redistributor",
                                       &pGicDev->hMmioReDist);
        AssertRCReturn(rc, rc);
    }

    /* ITS. */
    {
        rc = pHlp->pfnCFGMQueryU64(pCfg, "ItsMmioBase", &pGicDev->GCPhysGits);
        if (RT_SUCCESS(rc))
        {
            Assert(pGicDev->hMmioGits != NIL_IOMMMIOHANDLE);    /* paranoia, as this would be 0 here not NIL_IOMMMIOHANDLE. */
            RTGCPHYS const cbRegion = 2 * GITS_REG_FRAME_SIZE;  /* 2 frames for GICv3. */
            rc = PDMDevHlpMmioCreateAndMap(pDevIns, pGicDev->GCPhysGits, cbRegion, gicItsMmioWrite, gicItsMmioRead,
                                             IOMMMIO_FLAGS_READ_DWORD_QWORD
                                           | IOMMMIO_FLAGS_WRITE_DWORD_QWORD_ZEROED
                                           | IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_READ
                                           | IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_WRITE,
                                           "GIC ITS", &pGicDev->hMmioGits);
            AssertLogRelRCReturn(rc, rc);
            Assert(pGicDev->hMmioGits != NIL_IOMMMIOHANDLE);
            Assert(pGicDev->GCPhysGits != NIL_RTGCPHYS);

            /* When the ITS is enabled we must support LPIs. */
            if (!pGicDev->fLpi)
                return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                           N_("Configuration error: \"Lpi\" must be enabled when ITS is enabled\n"));

            /* Create ITS command-queue thread and semaphore. */
            PGITSDEV pGitsDev = &pGicDev->Gits;
            char szCmdQueueThread[32];
            RT_ZERO(szCmdQueueThread);
            RTStrPrintf(szCmdQueueThread, sizeof(szCmdQueueThread), "Gits-CmdQ-%u", iInstance);
            rc = PDMDevHlpThreadCreate(pDevIns, &pGitsDev->pCmdQueueThread, &pGicDev, gicItsR3CmdQueueThread,
                                       gicItsR3CmdQueueThreadWakeUp, 0 /* cbStack */, RTTHREADTYPE_IO, szCmdQueueThread);
            AssertLogRelRCReturn(rc, rc);

            rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pGitsDev->hEvtCmdQueue);
            AssertLogRelRCReturn(rc, rc);
        }
        else
        {
            pGicDev->hMmioGits  = NIL_IOMMMIOHANDLE;
            pGicDev->GCPhysGits = NIL_RTGCPHYS;

            /* When the ITS is disabled we don't support LPIs as we do not support direct LPI injection (guests don't use it). */
            if (pGicDev->fLpi)
                return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                           N_("Configuration error: \"Lpi\" must be disabled when ITS is disabled\n"));
        }
    }

    /*
     * Register saved state callbacks.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, GIC_SAVED_STATE_VERSION, 0, gicR3SaveExec, gicR3LoadExec);
    AssertRCReturn(rc, rc);

    /*
     * Register debugger info callbacks.
     *
     * We use separate callbacks rather than arguments so they can also be
     * dumped in an automated fashion while collecting crash diagnostics and
     * not just used during live debugging via the VM debugger.
     */
    DBGFR3InfoRegisterInternalEx(pVM, "gic",       "Dumps GIC basic information.",         gicR3DbgInfo,       DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "gicdist",   "Dumps GIC distributor information.",   gicR3DbgInfoDist,   DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "gicredist", "Dumps GIC redistributor information.", gicR3DbgInfoReDist, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "gicits",    "Dumps GIC ITS information.",           gicR3DbgInfoIts,    DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "giclpi",    "Dumps GIC LPI information.",           gicR3DbgInfoLpi,    DBGFINFO_FLAGS_ALL_EMTS);

    /*
     * Statistics.
     */
#ifdef VBOX_WITH_STATISTICS
# define GICCPU_REG_COUNTER(a_pvReg, a_pszNameFmt, a_pszDesc) \
         PDMDevHlpSTAMRegisterF(pDevIns, a_pvReg, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, \
                                a_pszDesc, a_pszNameFmt, idCpu)
# define GICCPU_PROF_COUNTER(a_pvReg, a_pszNameFmt, a_pszDesc) \
         PDMDevHlpSTAMRegisterF(pDevIns, a_pvReg, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, \
                                a_pszDesc, a_pszNameFmt, idCpu)
# define GIC_REG_COUNTER(a_pvReg, a_pszNameFmt, a_pszDesc) \
         PDMDevHlpSTAMRegisterF(pDevIns, a_pvReg, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, \
                                a_pszDesc, a_pszNameFmt)

    /* Redistributor. */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU  pVCpu   = pVM->apCpusR3[idCpu];
        PGICCPU pGicCpu = VMCPU_TO_GICCPU(pVCpu);

        GICCPU_REG_COUNTER(&pGicCpu->StatMmioRead,    "%u/MmioRead",     "Number of MMIO reads.");
        GICCPU_REG_COUNTER(&pGicCpu->StatMmioWrite,   "%u/MmioWrite",    "Number of MMIO writes.");
        GICCPU_REG_COUNTER(&pGicCpu->StatSysRegRead,  "%u/SysRegRead",   "Number of system register reads.");
        GICCPU_REG_COUNTER(&pGicCpu->StatSysRegWrite, "%u/SysRegWrite",  "Number of system register writes.");
        GICCPU_REG_COUNTER(&pGicCpu->StatSetSpi,      "%u/SetSpi",       "Number of set SPI callbacks.");
        GICCPU_REG_COUNTER(&pGicCpu->StatSetPpi,      "%u/SetPpi",       "Number of set PPI callbacks.");
        GICCPU_REG_COUNTER(&pGicCpu->StatSetSgi,      "%u/SetSgi",       "Number of SGIs generated.");

        GICCPU_PROF_COUNTER(&pGicCpu->StatProfIntrAck, "%u/Prof/IntrAck", "Profiling of interrupt acknowledge (IAR).");
        GICCPU_PROF_COUNTER(&pGicCpu->StatProfSetSpi,  "%u/Prof/SetSpi",  "Profiling of set SPI callback.");
        GICCPU_PROF_COUNTER(&pGicCpu->StatProfSetPpi,  "%u/Prof/SetPpi",  "Profiling of set PPI callback.");
        GICCPU_PROF_COUNTER(&pGicCpu->StatProfSetSgi,  "%u/Prof/SetSgi",  "Profiling of SGIs generated.");
    }

    /* ITS. */
    PGITSDEV pGitsDev = &pGicDev->Gits;
    GIC_REG_COUNTER(&pGitsDev->StatCmdMapd,   "ITS/Commands/MAPD",   "Number of MAPD commands executed.");
    GIC_REG_COUNTER(&pGitsDev->StatCmdMapc,   "ITS/Commands/MAPC",   "Number of MAPC commands executed.");
    GIC_REG_COUNTER(&pGitsDev->StatCmdMapi,   "ITS/Commands/MAPI",   "Number of MAPI commands executed.");
    GIC_REG_COUNTER(&pGitsDev->StatCmdMapti,  "ITS/Commands/MAPTI",  "Number of MAPTI commands executed.");
    GIC_REG_COUNTER(&pGitsDev->StatCmdSync,   "ITS/Commands/SYNC",   "Number of SYNC commands executed.");
    GIC_REG_COUNTER(&pGitsDev->StatCmdInvall, "ITS/Commands/INVALL", "Number of INVALL commands executed.");

# undef GICCPU_REG_COUNTER
# undef GICCPU_PROF_COUNTER
# undef GIC_REG_COUNTER
#endif  /* VBOX_WITH_STATISTICS */

    gicR3Reset(pDevIns);

    /*
     * Log some of the features exposed to software.
     */
    uint8_t const uArchRev      = pGicDev->uArchRev;
    uint8_t const uArchRevMinor = pGicDev->uArchRevMinor;
    uint8_t const uMaxSpi       = pGicDev->uMaxSpi;
    bool const    fExtSpi       = pGicDev->fExtSpi;
    uint8_t const uMaxExtSpi    = pGicDev->uMaxExtSpi;
    bool const    fExtPpi       = pGicDev->fExtPpi;
    uint8_t const uMaxExtPpi    = pGicDev->uMaxExtPpi;
    bool const fRangeSel        = pGicDev->fRangeSel;
    bool const fNmi             = pGicDev->fNmi;
    bool const fMbi             = pGicDev->fMbi;
    bool const fAff3Levels      = pGicDev->fAff3Levels;
    bool const fLpi             = pGicDev->fLpi;
    uint32_t const uMaxLpi      = pGicDev->uMaxLpi;
    uint16_t const uExtPpiLast  = uMaxExtPpi == GIC_REDIST_REG_TYPER_PPI_NUM_MAX_1087 ? 1087 : GIC_INTID_RANGE_EXT_PPI_LAST;
    LogRel(("GIC: ArchRev=%u.%u RangeSel=%RTbool Nmi=%RTbool Mbi=%RTbool Aff3Levels=%RTbool\n",
            uArchRev, uArchRevMinor, fRangeSel, fNmi, fMbi, fAff3Levels));
    LogRel(("GIC: SPIs=true (%u:32..%u) ExtSPIs=%RTbool (%u:4095..%u) ExtPPIs=%RTbool (%u:1056..%u)\n",
            uMaxSpi, 32 * (uMaxSpi + 1),
            fExtSpi, uMaxExtSpi, GIC_INTID_RANGE_EXT_SPI_START - 1 + 32 * (uMaxExtSpi + 1),
            fExtPpi, uMaxExtPpi, uExtPpiLast));
    LogRel(("GIC: ITS=%s LPIs=%s (%u:%u..%u)\n",
            pGicDev->hMmioGits != NIL_IOMMMIOHANDLE ? "enabled" : "disabled", fLpi ? "enabled" : "disabled",
            uMaxLpi, GIC_INTID_RANGE_LPI_START, GIC_INTID_RANGE_LPI_START - 1 + (UINT32_C(2) << uMaxLpi)));
    return VINF_SUCCESS;
}

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

