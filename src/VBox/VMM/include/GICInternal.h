/* $Id: GICInternal.h 110423 2025-07-28 07:16:43Z ramshankar.venkataraman@oracle.com $ */
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

#ifndef VMM_INCLUDED_SRC_include_GICInternal_h
#define VMM_INCLUDED_SRC_include_GICInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/gic.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmgic.h>
#include <VBox/vmm/stam.h>

#include "GITSInternal.h"

/** @defgroup grp_gic_int       Internal
 * @ingroup grp_gic
 * @internal
 * @{
 */

#ifdef VBOX_INCLUDED_vmm_pdmgic_h
/** The VirtualBox GIC backend. */
extern const PDMGICBACKEND g_GicBackend;
# ifdef RT_OS_DARWIN
/** The Hypervisor.Framework GIC backend. */
extern const PDMGICBACKEND g_GicHvfBackend;
# elif defined(RT_OS_WINDOWS)
/** The Hyper-V GIC backend. */
extern const PDMGICBACKEND g_GicHvBackend;
# elif defined(RT_OS_LINUX)
/** The KVM GIC backend. */
extern const PDMGICBACKEND g_GicKvmBackend;
# endif
#endif

#define VMCPU_TO_GICCPU(a_pVCpu)            (&(a_pVCpu)->gic.s)
#define VM_TO_GIC(a_pVM)                    (&(a_pVM)->gic.s)
#define GICDEV_TO_GITSDEV(a_GicDev)         (&(a_GicDev)->Gits)
#ifdef IN_RING3
# define VMCPU_TO_DEVINS(a_pVCpu)           ((a_pVCpu)->pVMR3->gic.s.pDevInsR3)
#elif defined(IN_RING0)
# error "Not implemented!"
#endif

/** Acquire the device critical section. */
#define GIC_CRIT_SECT_ENTER(a_pDevIns) \
    do \
    { \
        int const rcLock_ = PDMDevHlpCritSectEnter((a_pDevIns), (a_pDevIns)->pCritSectRoR3, VINF_SUCCESS); \
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV((a_pDevIns), (a_pDevIns)->pCritSectRoR3, rcLock_); \
    } while(0)

/** Release the device critical section. */
#define GIC_CRIT_SECT_LEAVE(a_pDevIns)          PDMDevHlpCritSectLeave((a_pDevIns), (a_pDevIns)->CTX_SUFF(pCritSectRo))

/** Returns whether the critical section is held. */
#define GIC_CRIT_SECT_IS_OWNER(a_pDevIns)       PDMDevHlpCritSectIsOwner((a_pDevIns), (a_pDevIns)->CTX_SUFF(pCritSectRo))

/** Returns whether the given register offset is within the specified range. */
#define GIC_IS_REG_IN_RANGE(a_offReg, a_offFirst, a_cbRegion)    ((uint32_t)(a_offReg) - (a_offFirst) < (uint32_t)(a_cbRegion))

/** @def GIC_SET_REG_U64_FULL
 * Sets a 64-bit GIC register.
 * @param   a_uReg      The 64-bit register to set.
 * @param   a_uValue    The 64-bit value being written.
 * @param   a_fRwMask   The 64-bit mask of valid read-write bits.
 */
#define GIC_SET_REG_U64_FULL(a_uReg, a_uValue, a_fRwMask) \
    do \
    { \
        AssertCompile(sizeof(a_uReg) == sizeof(uint64_t)); \
        AssertCompile(sizeof(a_fRwMask) == sizeof(uint64_t)); \
        (a_uReg) = ((a_uReg) & ~(a_fRwMask)) | ((a_uValue) & (a_fRwMask)); \
    } while (0)

/** @def GIC_SET_REG_U64_LO
 * Sets the lower half of a 64-bit GIC register.
 * @param   a_uReg      The lower half of a 64-bit register to set.
 * @param   a_uValue    The value being written (only lower 32-bits are used).
 * @param   a_fRwMask   The 64-bit mask of valid read-write bits.
 */
#define GIC_SET_REG_U64_LO(a_uReg, a_uValue, a_fRwMask) \
    do \
    { \
        AssertCompile(sizeof(a_uReg) == sizeof(uint32_t)); \
        AssertCompile(sizeof(a_fRwMask) == sizeof(uint64_t)); \
        (a_uReg) = ((a_uReg) & ~(RT_LO_U32(a_fRwMask))) | ((uint32_t)(a_uValue) & (RT_LO_U32(a_fRwMask))); \
    } while (0)

/** @def GIC_SET_REG_U64_HI
 * Sets the upper half of a 64-bit GIC register.
 * @param   a_uReg      The upper half of the 64-bit register to set.
 * @param   a_uValue    The value being written (only lower 32-bits are used).
 * @param   a_fRwMask   The 64-bit mask of valid read-write bits.
 */
#define GIC_SET_REG_U64_HI(a_uReg, a_uValue, a_fRwMask) \
    do \
    { \
        AssertCompile(sizeof(a_uReg) == sizeof(uint32_t)); \
        AssertCompile(sizeof(a_fRwMask) == sizeof(uint64_t)); \
        (a_uReg) = ((a_uReg) & ~(RT_HI_U32(a_fRwMask))) | ((uint32_t)(a_uValue) & (RT_HI_U32(a_fRwMask))); \
    } while (0)

/** @def GIC_SET_REG_U32
 * Sets a 32-bit GIC register.
 * @param   a_uReg      The 32-bit register to set.
 * @param   a_uValue    The 32-bit value being written (only lower 32-bits are
 *                      used).
 * @param   a_fRwMask   The mask of valid read-write bits (only lower 32-bits are
 *                      used).
 */
#define GIC_SET_REG_U32(a_uReg, a_uValue, a_fRwMask) \
    do \
    { \
        AssertCompile(sizeof(a_uReg) == sizeof(uint32_t)); \
        (a_uReg) = ((a_uReg) & ~(a_fRwMask)) | ((uint32_t)(a_uValue) & (uint32_t)(a_fRwMask)); \
    } while (0)

/** @name GIC interrupt ID range checks macros.
 * @{ */
#define GIC_IS_INTR_SGI(a_uIntId)        ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_SGI_START < (uint32_t)GIC_INTID_SGI_RANGE_SIZE)
#define GIC_IS_INTR_PPI(a_uIntId)        ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_PPI_START < (uint32_t)GIC_INTID_PPI_RANGE_SIZE)
#define GIC_IS_INTR_SGI_OR_PPI(a_uIntId) ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_SGI_START < (uint32_t)(GIC_INTID_SGI_RANGE_SIZE + GIC_INTID_PPI_RANGE_SIZE))
#define GIC_IS_INTR_SPI(a_uIntId)        ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_SPI_START < (uint32_t)GIC_INTID_SPI_RANGE_SIZE)
#define GIC_IS_INTR_SPECIAL(a_uIntId)    ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_SPECIAL_START < (uint32_t)GIC_INTID_SPECIAL_RANGE_SIZE)
#define GIC_IS_INTR_EXT_PPI(a_uIntId)    ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_EXT_PPI_START < (uint32_t)GIC_INTID_EXT_PPI_RANGE_SIZE)
#define GIC_IS_INTR_EXT_SPI(a_uIntId)    ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_EXT_SPI_START < (uint32_t)GIC_INTID_EXT_SPI_RANGE_SIZE)
#define GIC_IS_INTR_LPI(a_uIntId)        ((uint32_t)(a_uIntId) - (uint32_t)GIC_INTID_RANGE_LPI_START < (uint32_t)RT_ELEMENTS(GICDEV::abLpiConfig))
/** @} */

/** @name GIC interrupt groups.
 * @{ */
/** Interrupt Group 0. */
#define GIC_INTR_GROUP_0                          RT_BIT_32(0)
/** Interrupt Group 1 (Secure). */
#define GIC_INTR_GROUP_1S                         RT_BIT_32(1)
/** Interrupt Group 1 (Non-secure). */
#define GIC_INTR_GROUP_1NS                        RT_BIT_32(2)
/** @} */

/**
 * GIC distributor interrupt bitmap.
 */
typedef union GICDISTINTRBMP
{
    /** The 64-bit view. */
    uint64_t        au64[32];
    /** The 32-bit view. */
    uint32_t        au32[64];
} GICDISTINTRBMP;
AssertCompileSize(GICDISTINTRBMP, 256);
AssertCompileMembersSameSize(GICDISTINTRBMP, au64, GICDISTINTRBMP, au32);
AssertCompileMemberAlignment(GICDISTINTRBMP, au32, 4);

/**
 * GIC PDM instance data (per-VM).
 */
typedef struct GICDEV
{
    /** @name Distributor register state.
     * @{
     */
    /** Interrupt group bitmap. */
    GICDISTINTRBMP              IntrGroup;
    /** Interrupt config bitmap (edge-triggered vs level-sensitive). */
    GICDISTINTRBMP              IntrConfig;
    /** Interrupt enabled bitmap. */
    GICDISTINTRBMP              IntrEnabled;
    /** Interrupt pending bitmap. */
    GICDISTINTRBMP              IntrPending;
    /** Interrupt active bitmap. */
    GICDISTINTRBMP              IntrActive;
    /** Interrupt line-level bitmap. */
    GICDISTINTRBMP              IntrLevel;
    /** Interrupt priorities. */
    uint8_t                     abIntrPriority[2048];
    /** Interrupt routine mode bitmap. */
    GICDISTINTRBMP              IntrRoutingMode;
    /** Interrupt routing info. */
    uint32_t                    au32IntrRouting[2048];
    /** Mask of enabled interrupt groups (see GIC_INTR_GROUP_XXX). */
    uint32_t                    fIntrGroupMask;
    /** @} */

    /** @name Configurables.
     * @{ */
    /** The GIC architecture revision (GICD_PIDR2.ArchRev and GICR_PIDR2.ArchRev). */
    uint8_t                     uArchRev;
    /** The GIC architecture minor revision (currently 1 as we only support GICv3.1). */
    uint8_t                     uArchRevMinor;
    /** Maximum SPIs supported (GICD_TYPER.ItLinesNumber). */
    uint8_t                     uMaxSpi;
    /** Whether extended SPIs are supported (GICD_ESPI). */
    bool                        fExtSpi;
    /** Maximum extended SPIs supported (GICD_TYPER.ESPI_range).  */
    uint8_t                     uMaxExtSpi;
    /** Whether extended PPIs are supported. */
    bool                        fExtPpi;
    /** Maximum extended PPI supported (GICR_TYPER.PPInum). */
    uint8_t                     uMaxExtPpi;
    /** Whether range-selector is supported (GICD_TYPER.RSS and ICC_CTLR_EL1.RSS). */
    bool                        fRangeSel;
    /** Whether NMIs are supported (GICD_TYPER.NMI). */
    bool                        fNmi;
    /** Whether message-based interrupts are supported (GICD_TYPER.MBIS). */
    bool                        fMbi;
    /** Flag whether affinity routing is enabled. */
    bool                        fAffRouting;
    /** Whether non-zero affinity 3 levels are supported (GICD_TYPER.A3V) and
     *  (ICC_CTLR.A3V). */
    bool                        fAff3Levels;
    /** Whether LPIs are supported (GICD_TYPER.PLPIS). */
    bool                        fLpi;
    /** Maximum LPI supported (GICD_TYPER.num_LPI). */
    uint8_t                     uMaxLpi;
    /** @} */

    /** @name GITS device data and LPIs.
     * @{ */
    /** Whether LPIs are enabled (GICR_CTLR.EnableLpis of all redistributors). */
    bool                        fEnableLpis;
    /** Padding. */
    bool                        afPadding0[5];
    /** LPI config table. */
    uint8_t                     abLpiConfig[8192];
    /** LPI config table base register (GICR_PROPBASER). */
    RTUINT64U                   uLpiConfigBaseReg;
    /** LPI pending table base register (GICR_PENDBASER). */
    RTUINT64U                   uLpiPendingBaseReg;
    /** ITS device state. */
    GITSDEV                     Gits;
    /** @} */

    /** @name MMIO data.
     * @{ */
    /** Distributor MMIO handle. */
    IOMMMIOHANDLE               hMmioDist;
    /** Redistributor MMIO handle. */
    IOMMMIOHANDLE               hMmioReDist;
    /** Interrupt translation service MMIO handle. */
    IOMMMIOHANDLE               hMmioGits;
    /** Physical address of the ITS. */
    RTGCPHYS                    GCPhysGits;
    /** @} */

#ifdef VBOX_WITH_STATISTICS
    /** @name Statistics.
     * @{ */
    /** Number of set SPI callbacks. */
    STAMCOUNTER                 StatSetSpi;
    /** Number of set LPI callbacks. */
    STAMCOUNTER                 StatSetLpi;
    /** Profiling of set SPI callback. */
    STAMPROFILE                 StatProfSetSpi;
    /** @} */
#endif
} GICDEV;
/** Pointer to a GIC device. */
typedef GICDEV *PGICDEV;
/** Pointer to a const GIC device. */
typedef GICDEV const *PCGICDEV;
AssertCompileMemberAlignment(GICDEV, abIntrPriority,  8);
AssertCompileMemberAlignment(GICDEV, au32IntrRouting, 8);
AssertCompileMemberAlignment(GICDEV, fIntrGroupMask,  8);
AssertCompileMemberAlignment(GICDEV, abLpiConfig,     8);
AssertCompileMemberAlignment(GICDEV, Gits,            8);
AssertCompileMemberAlignment(GICDEV, hMmioDist,       8);

/**
 * GIC VM Instance data.
 */
typedef struct GIC
{
    /** The ring-3 device instance. */
    PPDMDEVINSR3                pDevInsR3;
} GIC;
/** Pointer to GIC VM instance data. */
typedef GIC *PGIC;
/** Pointer to const GIC VM instance data. */
typedef GIC const *PCGIC;
AssertCompileSizeAlignment(GIC, 8);

/**
 * GIC LPI pending bitmap.
 * This contains the same number of LPIs to match GICDEV::abLpiConfig.
 */
typedef union GICLPIBMP
{
    /** The 64-bit view. */
    uint64_t        au64[128];
    /** The 32-bit view. */
    uint32_t        au32[256];
} GICLPIBMP;
AssertCompileSize(GICLPIBMP, RT_ELEMENTS(GICDEV::abLpiConfig) / 8);
AssertCompileMembersSameSize(GICLPIBMP, au64, GICLPIBMP, au32);
AssertCompileMemberAlignment(GICLPIBMP, au32, 4);

/**
 * GIC VMCPU Instance data.
 */
typedef struct GICCPU
{
    /** @name Redistributor register state.
     * @{ */
    /** Interrupt group bitmap. */
    uint32_t                    bmIntrGroup[3];
    /** Interrupt config bitmap (edge-triggered vs level-sensitive). */
    uint32_t                    bmIntrConfig[3];
    /** Interrupt enabled bitmap. */
    uint32_t                    bmIntrEnabled[3];
    /** Interrupt pending bitmap. */
    uint32_t                    bmIntrPending[3];
    /** Interrupt active bitmap. */
    uint32_t                    bmIntrActive[3];
    /** Interrupt line-level bitmap. */
    uint32_t                    bmIntrLevel[3];
    /** Interrupt priorities. */
    uint8_t                     abIntrPriority[96];
    /** @} */

    /** @name ICC system register state.
     * @{ */
    /** Control register (ICC_CTLR_EL1). */
    uint64_t                    uIccCtlr;
    /** Mask of enabled interrupt groups (see GIC_INTR_GROUP_XXX). */
    uint32_t                    fIntrGroupMask;
    /** Interrupt priority mask of the CPU interface (ICC_PMR_EL1). */
    uint8_t                     bIntrPriorityMask;
    /** Index to the current running priority. */
    uint8_t                     idxRunningPriority;
    /** Binary point register for group 0 interrupts. */
    uint8_t                     bBinaryPtGroup0;
    /** Binary point register for group 1 interrupts. */
    uint8_t                     bBinaryPtGroup1;
    /** Running priorities caused by preemption. */
    uint8_t                     abRunningPriorities[256];
    /** Active priorities group 0 bitmap. */
    uint32_t                    bmActivePriorityGroup0[4];
    /** Active priorities group 1 bitmap. */
    uint32_t                    bmActivePriorityGroup1[4];
    /** INTID of the running interrupts (for debugging). */
    uint16_t                    abRunningIntId[256];
    /** @} */

    /** @name LPIs.
     * @{ */
    /** LPI pending bitmap. */
    GICLPIBMP                   LpiPending;
    /** @} */

#ifdef VBOX_WITH_STATISTICS
    /** @name Statistics.
     * @{ */
    /** Number of MMIO reads. */
    STAMCOUNTER                 StatMmioRead;
    /** Number of MMIO writes. */
    STAMCOUNTER                 StatMmioWrite;
    /** Number of MSR reads. */
    STAMCOUNTER                 StatSysRegRead;
    /** Number of MSR writes. */
    STAMCOUNTER                 StatSysRegWrite;
    /** Number of set PPI callbacks. */
    STAMCOUNTER                 StatSetPpi;
    /** Number of SGIs generated. */
    STAMCOUNTER                 StatSetSgi;
    /** Number of interrupts acknowledged. */
    STAMCOUNTER                 StatIntrAck;
    /** Number of interrupts EOI'd. */
    STAMCOUNTER                 StatIntrEoi;

    /** Profiling of interrupt acknowledge (IAR). */
    STAMPROFILE                 StatProfIntrAck;
    /** Profiling of set PPI callback. */
    STAMPROFILE                 StatProfSetPpi;
    /** Profiling of set SGI function. */
    STAMPROFILE                 StatProfSetSgi;
    /** @} */
#endif
} GICCPU;
/** Pointer to GIC VMCPU instance data. */
typedef GICCPU *PGICCPU;
/** Pointer to a const GIC VMCPU instance data. */
typedef GICCPU const *PCGICCPU;
/* Ensure the LPI pending bitmap's capacity is sufficient for the number of LPIs we support. */
AssertCompileMemberSize(GICCPU, LpiPending, RT_ELEMENTS(GICDEV::abLpiConfig) / 8);
AssertCompileMemberAlignment(GICCPU, abIntrPriority,      8);
AssertCompileMemberAlignment(GICCPU, uIccCtlr,            8);
AssertCompileMemberAlignment(GICCPU, abRunningPriorities, 8);
AssertCompileMemberAlignment(GICCPU, LpiPending,          8);
AssertCompileMemberSize(GICCPU, bmIntrGroup,     12);
AssertCompileMemberSize(GICCPU, bmIntrConfig,    12);
AssertCompileMemberSize(GICCPU, bmIntrEnabled,   12);
AssertCompileMemberSize(GICCPU, bmIntrPending,   12);
AssertCompileMemberSize(GICCPU, bmIntrActive,    12);
AssertCompileMemberSize(GICCPU, bmIntrLevel,     12);
AssertCompileMemberSize(GICCPU, abIntrPriority,  RT_SIZEOFMEMB(GICCPU, bmIntrGroup) * 8);
AssertCompileMemberAlignment(GICCPU, bmIntrGroup, 8);

DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicReDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicReDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicItsMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC) gicItsMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);

DECLHIDDEN(void)                   gicReDistSetLpi(PPDMDEVINS pDevIns, PVMCPUCC pVCpu, uint16_t uIntId, bool fAsserted);
DECLHIDDEN(void)                   gicDistReadLpiConfigTableFromMem(PPDMDEVINS pDevIns);

DECLHIDDEN(void)                   gicResetCpu(PPDMDEVINS pDevIns, PVMCPUCC pVCpu);
DECLHIDDEN(void)                   gicReset(PPDMDEVINS pDevIns);
DECLHIDDEN(uint16_t)               gicReDistGetIntIdFromIndex(uint16_t idxIntr);
DECLHIDDEN(uint16_t)               gicDistGetIntIdFromIndex(uint16_t idxIntr);

DECLCALLBACK(int)                  gicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg);
DECLCALLBACK(int)                  gicR3Destruct(PPDMDEVINS pDevIns);
DECLCALLBACK(void)                 gicR3Reset(PPDMDEVINS pDevIns);

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_GICInternal_h */

