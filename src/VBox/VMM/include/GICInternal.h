/* $Id: GICInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIC - Generic Interrupt Controller Architecture (GICv3).
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


/** @defgroup grp_gic_int       Internal
 * @ingroup grp_gic
 * @internal
 * @{
 */

#define VMCPU_TO_GICCPU(a_pVCpu)             (&(a_pVCpu)->gic.s)
#define VM_TO_GIC(a_pVM)                     (&(a_pVM)->gic.s)
#define VM_TO_GICDEV(a_pVM)                  CTX_SUFF(VM_TO_GIC(a_pVM)->pGicDev)
#ifdef IN_RING3
# define VMCPU_TO_DEVINS(a_pVCpu)           ((a_pVCpu)->pVMR3->gic.s.pDevInsR3)
#elif defined(IN_RING0)
# error "Not implemented!"
#endif

/** Maximum number of SPI interrupts. */
#define GIC_SPI_MAX                         32

/**
 * GIC PDM instance data (per-VM).
 */
typedef struct GICDEV
{
    /** The distributor MMIO handle. */
    IOMMMIOHANDLE               hMmioDist;
    /** The redistributor MMIO handle. */
    IOMMMIOHANDLE               hMmioReDist;

    /** @name SPI distributor register state.
     * @{ */
    /** Interrupt Group 0 Register. */
    volatile uint32_t           u32RegIGrp0;
    /** Interrupt Configuration Register 0. */
    volatile uint32_t           u32RegICfg0;
    /** Interrupt Configuration Register 1. */
    volatile uint32_t           u32RegICfg1;
    /** Interrupt enabled bitmap. */
    volatile uint32_t           bmIntEnabled;
    /** Current interrupt pending state. */
    volatile uint32_t           bmIntPending;
    /** The current interrupt active state. */
    volatile uint32_t           bmIntActive;
    /** The interrupt priority for each of the SGI/PPIs */
    volatile uint8_t            abIntPriority[GIC_SPI_MAX];

    /** Flag whether group 0 interrupts are currently enabled. */
    volatile bool               fIrqGrp0Enabled;
    /** Flag whether group 1 interrupts are currently enabled. */
    volatile bool               fIrqGrp1Enabled;
    /** @} */

} GICDEV;
/** Pointer to a GIC device. */
typedef GICDEV *PGICDEV;
/** Pointer to a const GIC device. */
typedef GICDEV const *PCGICDEV;


/**
 * GIC VM Instance data.
 */
typedef struct GIC
{
    /** The ring-3 device instance. */
    PPDMDEVINSR3                pDevInsR3;
    /** Flag whether the GIC provided by NEM is used. */
    bool                        fNemGic;
} GIC;
/** Pointer to GIC VM instance data. */
typedef GIC *PGIC;
/** Pointer to const GIC VM instance data. */
typedef GIC const *PCGIC;
AssertCompileSizeAlignment(GIC, 8);

/**
 * GIC VMCPU Instance data.
 */
typedef struct GICCPU
{
    /** @name The per vCPU redistributor data is kept here.
     * @{ */

    /** @name Physical LPI register state.
     * @{ */
    /** @} */

    /** @name SGI and PPI redistributor register state.
     * @{ */
    /** Interrupt Group 0 Register. */
    volatile uint32_t           u32RegIGrp0;
    /** Interrupt Configuration Register 0. */
    volatile uint32_t           u32RegICfg0;
    /** Interrupt Configuration Register 1. */
    volatile uint32_t           u32RegICfg1;
    /** Interrupt enabled bitmap. */
    volatile uint32_t           bmIntEnabled;
    /** Current interrupt pending state. */
    volatile uint32_t           bmIntPending;
    /** The current interrupt active state. */
    volatile uint32_t           bmIntActive;
    /** The interrupt priority for each of the SGI/PPIs */
    volatile uint8_t            abIntPriority[GIC_INTID_RANGE_PPI_LAST + 1];
    /** @} */

    /** @name ICC system register state.
     * @{ */
    /** Flag whether group 0 interrupts are currently enabled. */
    volatile bool               fIrqGrp0Enabled;
    /** Flag whether group 1 interrupts are currently enabled. */
    volatile bool               fIrqGrp1Enabled;
    /** The current interrupt priority, only interrupts with a higher priority get signalled. */
    volatile uint8_t            bInterruptPriority;
    /** The interrupt controller Binary Point Register for Group 0 interrupts. */
    uint8_t                     bBinaryPointGrp0;
    /** The interrupt controller Binary Point Register for Group 1 interrupts. */
    uint8_t                     bBinaryPointGrp1;
    /** The running poriorities caused by preemption. */
    volatile uint8_t            abRunningPriorities[256];
    /** The index to the current running priority. */
    volatile uint8_t            idxRunningPriority;
    /** @} */

    /** @name Log Max counters
     * @{ */
    uint32_t                    cLogMaxAccessError;
    uint32_t                    cLogMaxSetApicBaseAddr;
    uint32_t                    cLogMaxGetApicBaseAddr;
    uint32_t                    uAlignment4;
    /** @} */

    /** @name APIC statistics.
     * @{ */
#ifdef VBOX_WITH_STATISTICS
    /** Number of MMIO reads in R3. */
    STAMCOUNTER                 StatMmioReadR3;
    /** Number of MMIO writes in R3. */
    STAMCOUNTER                 StatMmioWriteR3;
    /** Number of MSR reads in R3. */
    STAMCOUNTER                 StatSysRegReadR3;
    /** Number of MSR writes in R3. */
    STAMCOUNTER                 StatSysRegWriteR3;

# if 0 /* No R0 for now. */
    /** Number of MMIO reads in RZ. */
    STAMCOUNTER                 StatMmioReadRZ;
    /** Number of MMIO writes in RZ. */
    STAMCOUNTER                 StatMmioWriteRZ;
    /** Number of MSR reads in RZ. */
    STAMCOUNTER                 StatSysRegReadRZ;
    /** Number of MSR writes in RZ. */
    STAMCOUNTER                 StatSysRegWriteRZ;
# endif
#endif
    /** @} */
} GICCPU;
/** Pointer to GIC VMCPU instance data. */
typedef GICCPU *PGICCPU;
/** Pointer to a const GIC VMCPU instance data. */
typedef GICCPU const *PCGICCPU;

DECL_HIDDEN_CALLBACK(VBOXSTRICTRC)      gicDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC)      gicDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);

DECL_HIDDEN_CALLBACK(VBOXSTRICTRC)      gicReDistMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);
DECL_HIDDEN_CALLBACK(VBOXSTRICTRC)      gicReDistMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);

DECLHIDDEN(void)                        gicResetCpu(PVMCPUCC pVCpu);

DECLCALLBACK(int)                       gicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg);
DECLCALLBACK(int)                       gicR3Destruct(PPDMDEVINS pDevIns);
DECLCALLBACK(void)                      gicR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta);
DECLCALLBACK(void)                      gicR3Reset(PPDMDEVINS pDevIns);

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_GICInternal_h */

