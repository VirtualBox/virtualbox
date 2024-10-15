/* $Id: VBoxPciInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxPci - PCI driver (Host), Internal Header.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VBoxPci_VBoxPciInternal_h
#define VBOX_INCLUDED_SRC_VBoxPci_VBoxPciInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/sup.h>
#include <VBox/rawpci.h>
#include <iprt/semaphore.h>
#include <iprt/assert.h>

#ifdef RT_OS_LINUX

#if RTLNX_VER_MIN(2,6,35) && defined(CONFIG_IOMMU_API)
# define VBOX_WITH_IOMMU
#endif

#ifdef VBOX_WITH_IOMMU
#include <linux/errno.h>
#include <linux/iommu.h>
#endif

#endif

RT_C_DECLS_BEGIN

/* Forward declaration. */
typedef struct VBOXRAWPCIGLOBALS *PVBOXRAWPCIGLOBALS;
typedef struct VBOXRAWPCIDRVVM   *PVBOXRAWPCIDRVVM;
typedef struct VBOXRAWPCIINS     *PVBOXRAWPCIINS;

typedef struct VBOXRAWPCIISRDESC
{
    /** Handler function. */
    PFNRAWPCIISR       pfnIrqHandler;
    /** Handler context. */
    void              *pIrqContext;
    /** Host IRQ. */
    int32_t            iHostIrq;
} VBOXRAWPCIISRDESC;
typedef struct VBOXRAWPCIISRDESC     *PVBOXRAWPCIISRDESC;

/**
 * The per-instance data of the VBox raw PCI interface.
 *
 * This is data associated with a host PCI card attached to the VM.
 *
 */
typedef struct VBOXRAWPCIINS
{
    /** Pointer to the globals. */
    PVBOXRAWPCIGLOBALS pGlobals;

     /** Mutex protecting device access. */
    RTSEMFASTMUTEX     hFastMtx;
    /** The spinlock protecting the state variables and device access. */
    RTSPINLOCK         hSpinlock;
    /** Pointer to the next device in the list. */
    PVBOXRAWPCIINS     pNext;
    /** Reference count. */
    uint32_t volatile cRefs;

    /* Host PCI address of this device. */
    uint32_t           HostPciAddress;

#ifdef RT_OS_LINUX
    struct pci_dev  *  pPciDev;
    char               szPrevDriver[64];
#endif
    bool               fMsiUsed;
    bool               fMsixUsed;
    bool               fIommuUsed;
    bool               fPad0;

    /** Port, given to the outside world. */
    RAWPCIDEVPORT      DevPort;

    /** IRQ handler. */
    VBOXRAWPCIISRDESC  IrqHandler;

    /** Pointer to per-VM context in hypervisor data. */
    PRAWPCIPERVM       pVmCtx;

    RTR0PTR            aRegionR0Mapping[/* XXX: magic */ 7];
} VBOXRAWPCIINS;

/**
 * Per-VM data of the VBox PCI driver. Pointed to by pGVM->rawpci.s.pDriverData.
 *
 */
typedef struct VBOXRAWPCIDRVVM
{
    /** Mutex protecting state changes. */
    RTSEMFASTMUTEX hFastMtx;

#ifdef RT_OS_LINUX
# ifdef VBOX_WITH_IOMMU
    /* IOMMU domain. */
    struct iommu_domain* pIommuDomain;
# endif
#endif
    /* Back pointer to pGVM->rawpci.s. */
    PRAWPCIPERVM pPerVmData;
} VBOXRAWPCIDRVVM;

/**
 * The global data of the VBox PCI driver.
 *
 * This contains the bit required for communicating with support driver, VBoxDrv
 * (start out as SupDrv).
 */
typedef struct VBOXRAWPCIGLOBALS
{
    /** Mutex protecting the list of instances and state changes. */
    RTSEMFASTMUTEX hFastMtx;

    /** Pointer to a list of instance data. */
    PVBOXRAWPCIINS pInstanceHead;

    /** The raw PCI interface factory. */
    RAWPCIFACTORY RawPciFactory;
    /** The SUPDRV component factory registration. */
    SUPDRVFACTORY SupDrvFactory;
    /** The number of current factory references. */
    int32_t volatile cFactoryRefs;
    /** Whether the IDC connection is open or not.
     * This is only for cleaning up correctly after the separate IDC init on Windows. */
    bool fIDCOpen;
    /** The SUPDRV IDC handle (opaque struct). */
    SUPDRVIDCHANDLE SupDrvIDC;
#ifdef RT_OS_LINUX
    bool fPciStubModuleAvail;
    struct module    * pciStubModule;
#endif
} VBOXRAWPCIGLOBALS;

DECLHIDDEN(int)  vboxPciInit(PVBOXRAWPCIGLOBALS pGlobals);
DECLHIDDEN(void) vboxPciShutdown(PVBOXRAWPCIGLOBALS pGlobals);

DECLHIDDEN(int)  vboxPciOsInitVm(PVBOXRAWPCIDRVVM pThis,   PVM pVM, PRAWPCIPERVM pVmData);
DECLHIDDEN(void) vboxPciOsDeinitVm(PVBOXRAWPCIDRVVM pThis, PVM pVM);

DECLHIDDEN(int)  vboxPciOsDevInit  (PVBOXRAWPCIINS pIns, uint32_t fFlags);
DECLHIDDEN(int)  vboxPciOsDevDeinit(PVBOXRAWPCIINS pIns, uint32_t fFlags);
DECLHIDDEN(int)  vboxPciOsDevDestroy(PVBOXRAWPCIINS pIns);

DECLHIDDEN(int)  vboxPciOsDevGetRegionInfo(PVBOXRAWPCIINS pIns,
                                           int32_t        iRegion,
                                           RTHCPHYS       *pRegionStart,
                                           uint64_t       *pu64RegionSize,
                                           bool           *pfPresent,
                                           uint32_t       *pfFlags);
DECLHIDDEN(int)  vboxPciOsDevMapRegion(PVBOXRAWPCIINS pIns,
                                       int32_t        iRegion,
                                       RTHCPHYS       pRegionStart,
                                       uint64_t       u64RegionSize,
                                       uint32_t       fFlags,
                                       RTR0PTR        *pRegionBase);
DECLHIDDEN(int)  vboxPciOsDevUnmapRegion(PVBOXRAWPCIINS pIns,
                                         int32_t        iRegion,
                                         RTHCPHYS       RegionStart,
                                         uint64_t       u64RegionSize,
                                         RTR0PTR        RegionBase);

DECLHIDDEN(int)  vboxPciOsDevPciCfgWrite(PVBOXRAWPCIINS pIns, uint32_t Register, PCIRAWMEMLOC *pValue);
DECLHIDDEN(int)  vboxPciOsDevPciCfgRead (PVBOXRAWPCIINS pIns, uint32_t Register, PCIRAWMEMLOC *pValue);

DECLHIDDEN(int)  vboxPciOsDevRegisterIrqHandler  (PVBOXRAWPCIINS pIns, PFNRAWPCIISR pfnHandler, void* pIrqContext, int32_t *piHostIrq);
DECLHIDDEN(int)  vboxPciOsDevUnregisterIrqHandler(PVBOXRAWPCIINS pIns, int32_t iHostIrq);

DECLHIDDEN(int)  vboxPciOsDevPowerStateChange(PVBOXRAWPCIINS pIns, PCIRAWPOWERSTATE  aState);

#define VBOX_DRV_VMDATA(pIns) ((PVBOXRAWPCIDRVVM)(pIns->pVmCtx ? pIns->pVmCtx->pDriverData : NULL))

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_VBoxPci_VBoxPciInternal_h */
