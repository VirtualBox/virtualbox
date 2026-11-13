/* $Id$ */
/** @file
 * PLIC - Platform Level Interrupt Controller (PLIC).
 */

/*
 * Copyright (C) 2026 Alexander Eichner <github@aeichner.de>.
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

#ifndef VMM_INCLUDED_SRC_include_PLICInternal_h
#define VMM_INCLUDED_SRC_include_PLICInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmaia.h>
#include <VBox/vmm/stam.h>

/** @defgroup grp_plic_int       Internal
 * @ingroup grp_plic
 * @internal
 * @{
 */

#ifdef VBOX_INCLUDED_vmm_pdmaia_h
# if defined(RT_OS_LINUX)
/** The PLIC backend (using the AIA interface here). */
extern const PDMAIABACKEND g_AiaPlicBackend;
# endif
#endif

#define VMCPU_TO_PLICCPU(a_pVCpu)            (&(a_pVCpu)->plic.s)
#define VM_TO_PLIC(a_pVM)                    (&(a_pVM)->plic.s)
#ifdef IN_RING3
# define VMCPU_TO_DEVINS(a_pVCpu)           ((a_pVCpu)->pVMR3->plic.s.pDevInsR3)
#elif defined(IN_RING0)
# error "Not implemented!"
#endif


/**
 * PLIC PDM instance data (per-VM).
 */
typedef struct PLICDEV
{
    /** The MMIO handle. */
    IOMMMIOHANDLE               hMmio;
} PLICDEV;
/** Pointer to an PLIC device. */
typedef PLICDEV *PPLICDEV;
/** Pointer to a const PLIC device. */
typedef PLICDEV const *PCPLICDEV;


/**
 * PLIC VM Instance data.
 */
typedef struct PLIC
{
    /** The ring-3 device instance. */
    PPDMDEVINSR3                pDevInsR3;
    /** Interrupt pending bitmap. */
    volatile uint32_t           bmIrqPending[1024 / (8 * sizeof(uint32_t))];
    /** Interrupt source priority. */
    uint32_t                    au32IrqPriority[1024];
} PLIC;
/** Pointer to PLIC VM instance data. */
typedef PLIC *PPLIC;
/** Pointer to const PLIC VM instance data. */
typedef PLIC const *PCPLIC;
AssertCompileSizeAlignment(PLIC, 8);
AssertCompile(sizeof(PLIC) <= 4360);


/**
 * PLIC VMCPU Instance data.
 */
typedef struct PLICCPU
{
    /** Interrupt enabled bitmap for this vCPU. */
    volatile uint32_t           bmIrqEnabled[1024 / 8 / sizeof(uint32_t)];
    /** The priority threshold for this vCPU. */
    volatile uint32_t           u32PriThr;
} PLICCPU;
/** Pointer to PLIC VMCPU instance data. */
typedef PLICCPU *PPLICCPU;
/** Pointer to a const PLIC VMCPU instance data. */
typedef PLICCPU const *PCPLICCPU;
AssertCompile(sizeof(PLICCPU) <= 3840);

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_PLICInternal_h */

