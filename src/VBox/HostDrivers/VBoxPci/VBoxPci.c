/* $Id: VBoxPci.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxPci - PCI card passthrough support (Host), Common Code.
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

/** @page pg_rawpci     VBoxPci - host PCI support
 *
 * This is a kernel module that works as host proxy between guest and
 * PCI hardware.
 *
 */

#define LOG_GROUP LOG_GROUP_DEV_PCI_RAW
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/sup.h>
#include <VBox/version.h>

#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/spinlock.h>
#include <iprt/uuid.h>
#include <iprt/asm.h>
#include <iprt/mem.h>

#include "VBoxPciInternal.h"


#define DEVPORT_2_VBOXRAWPCIINS(pPort) \
    ( (PVBOXRAWPCIINS)((uint8_t *)pPort - RT_OFFSETOF(VBOXRAWPCIINS, DevPort)) )


/**
 * Implements the SUPDRV component factor interface query method.
 *
 * @returns Pointer to an interface. NULL if not supported.
 *
 * @param   pSupDrvFactory      Pointer to the component factory registration structure.
 * @param   pSession            The session - unused.
 * @param   pszInterfaceUuid    The factory interface id.
 */
static DECLCALLBACK(void *) vboxPciQueryFactoryInterface(PCSUPDRVFACTORY pSupDrvFactory, PSUPDRVSESSION pSession, const char *pszInterfaceUuid)
{
    PVBOXRAWPCIGLOBALS pGlobals = (PVBOXRAWPCIGLOBALS)((uint8_t *)pSupDrvFactory - RT_OFFSETOF(VBOXRAWPCIGLOBALS, SupDrvFactory));

    /*
     * Convert the UUID strings and compare them.
     */
    RTUUID UuidReq;
    int rc = RTUuidFromStr(&UuidReq, pszInterfaceUuid);
    if (RT_SUCCESS(rc))
    {
        if (!RTUuidCompareStr(&UuidReq, RAWPCIFACTORY_UUID_STR))
        {
            ASMAtomicIncS32(&pGlobals->cFactoryRefs);
            return &pGlobals->RawPciFactory;
        }
    }
    else
        Log(("VBoxRawPci: rc=%Rrc, uuid=%s\n", rc, pszInterfaceUuid));

    return NULL;
}
DECLINLINE(int) vboxPciDevLock(PVBOXRAWPCIINS pThis)
{
#ifdef VBOX_WITH_SHARED_PCI_INTERRUPTS
    RTSpinlockAcquire(pThis->hSpinlock);
    return VINF_SUCCESS;
#else
    int rc = RTSemFastMutexRequest(pThis->hFastMtx);

    AssertRC(rc);
    return rc;
#endif
}

DECLINLINE(void) vboxPciDevUnlock(PVBOXRAWPCIINS pThis)
{
#ifdef VBOX_WITH_SHARED_PCI_INTERRUPTS
    RTSpinlockRelease(pThis->hSpinlock);
#else
    RTSemFastMutexRelease(pThis->hFastMtx);
#endif
}

DECLINLINE(int) vboxPciVmLock(PVBOXRAWPCIDRVVM pThis)
{
    int rc = RTSemFastMutexRequest(pThis->hFastMtx);
    AssertRC(rc);
    return rc;
}

DECLINLINE(void) vboxPciVmUnlock(PVBOXRAWPCIDRVVM pThis)
{
    RTSemFastMutexRelease(pThis->hFastMtx);
}

DECLINLINE(int) vboxPciGlobalsLock(PVBOXRAWPCIGLOBALS pGlobals)
{
    int rc = RTSemFastMutexRequest(pGlobals->hFastMtx);
    AssertRC(rc);
    return rc;
}

DECLINLINE(void) vboxPciGlobalsUnlock(PVBOXRAWPCIGLOBALS pGlobals)
{
    RTSemFastMutexRelease(pGlobals->hFastMtx);
}

static PVBOXRAWPCIINS vboxPciFindInstanceLocked(PVBOXRAWPCIGLOBALS pGlobals, uint32_t iHostAddress)
{
    PVBOXRAWPCIINS pCur;
    for (pCur = pGlobals->pInstanceHead; pCur != NULL; pCur = pCur->pNext)
    {
        if (iHostAddress == pCur->HostPciAddress)
            return pCur;
    }
    return NULL;
}

static void vboxPciUnlinkInstanceLocked(PVBOXRAWPCIGLOBALS pGlobals, PVBOXRAWPCIINS pToUnlink)
{
    if (pGlobals->pInstanceHead == pToUnlink)
        pGlobals->pInstanceHead = pToUnlink->pNext;
    else
    {
        PVBOXRAWPCIINS pCur;
        for (pCur = pGlobals->pInstanceHead; pCur != NULL; pCur = pCur->pNext)
        {
            if (pCur->pNext == pToUnlink)
            {
                pCur->pNext = pToUnlink->pNext;
                break;
            }
        }
    }
    pToUnlink->pNext = NULL;
}


#if 0 /** @todo r=bird: Who the heck is supposed to call this?!?   */
DECLHIDDEN(void) vboxPciDevCleanup(PVBOXRAWPCIINS pThis)
{
    pThis->DevPort.pfnDeinit(&pThis->DevPort, 0);

    if (pThis->hFastMtx)
    {
        RTSemFastMutexDestroy(pThis->hFastMtx);
        pThis->hFastMtx = NIL_RTSEMFASTMUTEX;
    }

    if (pThis->hSpinlock)
    {
        RTSpinlockDestroy(pThis->hSpinlock);
        pThis->hSpinlock = NIL_RTSPINLOCK;
    }

    vboxPciGlobalsLock(pThis->pGlobals);
    vboxPciUnlinkInstanceLocked(pThis->pGlobals, pThis);
    vboxPciGlobalsUnlock(pThis->pGlobals);
}
#endif


/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnInit}
 */
static DECLCALLBACK(int) vboxPciDevInit(PRAWPCIDEVPORT pPort, uint32_t fFlags)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int rc;

    vboxPciDevLock(pThis);

    rc = vboxPciOsDevInit(pThis, fFlags);

    vboxPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnDeinit}
 */
static DECLCALLBACK(int) vboxPciDevDeinit(PRAWPCIDEVPORT pPort, uint32_t fFlags)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int            rc;

    vboxPciDevLock(pThis);

    if (pThis->IrqHandler.pfnIrqHandler)
    {
        vboxPciOsDevUnregisterIrqHandler(pThis, pThis->IrqHandler.iHostIrq);
        pThis->IrqHandler.iHostIrq = 0;
        pThis->IrqHandler.pfnIrqHandler = NULL;
    }

    rc = vboxPciOsDevDeinit(pThis, fFlags);

    vboxPciDevUnlock(pThis);

    return rc;
}


/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnDestroy}
 */
static DECLCALLBACK(int) vboxPciDevDestroy(PRAWPCIDEVPORT pPort)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int rc;

    rc = vboxPciOsDevDestroy(pThis);
    if (rc == VINF_SUCCESS)
    {
        if (pThis->hFastMtx)
        {
            RTSemFastMutexDestroy(pThis->hFastMtx);
            pThis->hFastMtx = NIL_RTSEMFASTMUTEX;
        }

        if (pThis->hSpinlock)
        {
            RTSpinlockDestroy(pThis->hSpinlock);
            pThis->hSpinlock = NIL_RTSPINLOCK;
        }

        vboxPciGlobalsLock(pThis->pGlobals);
        vboxPciUnlinkInstanceLocked(pThis->pGlobals, pThis);
        vboxPciGlobalsUnlock(pThis->pGlobals);

        RTMemFree(pThis);
    }

    return rc;
}
/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnGetRegionInfo}
 */
static DECLCALLBACK(int) vboxPciDevGetRegionInfo(PRAWPCIDEVPORT pPort,
                                                 int32_t        iRegion,
                                                 RTHCPHYS       *pRegionStart,
                                                 uint64_t       *pu64RegionSize,
                                                 bool           *pfPresent,
                                                 uint32_t        *pfFlags)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int            rc;

    vboxPciDevLock(pThis);

    rc = vboxPciOsDevGetRegionInfo(pThis, iRegion,
                                   pRegionStart, pu64RegionSize,
                                   pfPresent, pfFlags);
    vboxPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnMapRegion}
 */
static DECLCALLBACK(int) vboxPciDevMapRegion(PRAWPCIDEVPORT pPort,
                                             int32_t        iRegion,
                                             RTHCPHYS       RegionStart,
                                             uint64_t       u64RegionSize,
                                             int32_t        fFlags,
                                             RTR0PTR        *pRegionBaseR0)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int            rc;

    vboxPciDevLock(pThis);

    rc = vboxPciOsDevMapRegion(pThis, iRegion, RegionStart, u64RegionSize, fFlags, pRegionBaseR0);

    vboxPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnUnmapRegion}
 */
static DECLCALLBACK(int) vboxPciDevUnmapRegion(PRAWPCIDEVPORT pPort,
                                               int32_t        iRegion,
                                               RTHCPHYS       RegionStart,
                                               uint64_t       u64RegionSize,
                                               RTR0PTR        RegionBase)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int            rc;

    vboxPciDevLock(pThis);

    rc = vboxPciOsDevUnmapRegion(pThis, iRegion, RegionStart, u64RegionSize, RegionBase);

    vboxPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnPciCfgRead}
 */
static DECLCALLBACK(int) vboxPciDevPciCfgRead(PRAWPCIDEVPORT pPort,
                                              uint32_t       Register,
                                              PCIRAWMEMLOC   *pValue)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int            rc;

    vboxPciDevLock(pThis);

    rc = vboxPciOsDevPciCfgRead(pThis, Register, pValue);

    vboxPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnPciCfgWrite}
 */
static DECLCALLBACK(int) vboxPciDevPciCfgWrite(PRAWPCIDEVPORT pPort,
                                               uint32_t       Register,
                                               PCIRAWMEMLOC   *pValue)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int            rc;

    vboxPciDevLock(pThis);

    rc = vboxPciOsDevPciCfgWrite(pThis, Register, pValue);

    vboxPciDevUnlock(pThis);

    return rc;
}

static DECLCALLBACK(int) vboxPciDevRegisterIrqHandler(PRAWPCIDEVPORT  pPort,
                                                      PFNRAWPCIISR    pfnHandler,
                                                      void*           pIrqContext,
                                                      PCIRAWISRHANDLE *phIsr)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int            rc;
    int32_t        iHostIrq = 0;

    if (pfnHandler == NULL)
        return VERR_INVALID_PARAMETER;

    vboxPciDevLock(pThis);

    if (pThis->IrqHandler.pfnIrqHandler)
    {
        rc = VERR_ALREADY_EXISTS;
    }
    else
    {
        rc = vboxPciOsDevRegisterIrqHandler(pThis, pfnHandler, pIrqContext, &iHostIrq);
        if (RT_SUCCESS(rc))
        {
            *phIsr = 0xcafe0000;
            pThis->IrqHandler.iHostIrq      = iHostIrq;
            pThis->IrqHandler.pfnIrqHandler = pfnHandler;
            pThis->IrqHandler.pIrqContext   = pIrqContext;
        }
    }

    vboxPciDevUnlock(pThis);

    return rc;
}

static DECLCALLBACK(int) vboxPciDevUnregisterIrqHandler(PRAWPCIDEVPORT  pPort,
                                                        PCIRAWISRHANDLE hIsr)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int            rc;

    if (hIsr != 0xcafe0000)
        return VERR_INVALID_PARAMETER;

    vboxPciDevLock(pThis);

    rc = vboxPciOsDevUnregisterIrqHandler(pThis, pThis->IrqHandler.iHostIrq);
    if (RT_SUCCESS(rc))
    {
        pThis->IrqHandler.pfnIrqHandler = NULL;
        pThis->IrqHandler.pIrqContext   = NULL;
        pThis->IrqHandler.iHostIrq = 0;
    }
    vboxPciDevUnlock(pThis);

    return rc;
}

static DECLCALLBACK(int) vboxPciDevPowerStateChange(PRAWPCIDEVPORT    pPort,
                                                    PCIRAWPOWERSTATE  aState,
                                                    uint64_t          *pu64Param)
{
    PVBOXRAWPCIINS pThis = DEVPORT_2_VBOXRAWPCIINS(pPort);
    int            rc;

    vboxPciDevLock(pThis);

    rc = vboxPciOsDevPowerStateChange(pThis, aState);

    switch (aState)
    {
        case PCIRAW_POWER_ON:
            /*
             * Let virtual device know about VM caps.
             */
            *pu64Param = VBOX_DRV_VMDATA(pThis)->pPerVmData->fVmCaps;
            break;
        default:
            pu64Param = 0;
            break;
    }


    vboxPciDevUnlock(pThis);

    return rc;
}

/**
 * Creates a new instance.
 *
 * @returns VBox status code.
 * @param   pGlobals            The globals.
 * @param   u32HostAddress      Host address.
 * @param   fFlags              Flags.
 * @param   pVmCtx              VM context.
 * @param   ppDevPort           Where to store the pointer to our port interface.
 * @param   pfDevFlags          The device flags.
 */
static int vboxPciNewInstance(PVBOXRAWPCIGLOBALS pGlobals,
                              uint32_t           u32HostAddress,
                              uint32_t           fFlags,
                              PRAWPCIPERVM       pVmCtx,
                              PRAWPCIDEVPORT     *ppDevPort,
                              uint32_t           *pfDevFlags)
{
    int             rc;
    PVBOXRAWPCIINS  pNew = (PVBOXRAWPCIINS)RTMemAllocZ(sizeof(*pNew));
    if (!pNew)
        return VERR_NO_MEMORY;

    pNew->pGlobals                      = pGlobals;
    pNew->hSpinlock                     = NIL_RTSPINLOCK;
    pNew->cRefs                         = 1;
    pNew->pNext                         = NULL;
    pNew->HostPciAddress                = u32HostAddress;
    pNew->pVmCtx                        = pVmCtx;

    pNew->DevPort.u32Version            = RAWPCIDEVPORT_VERSION;

    pNew->DevPort.pfnInit               = vboxPciDevInit;
    pNew->DevPort.pfnDeinit             = vboxPciDevDeinit;
    pNew->DevPort.pfnDestroy            = vboxPciDevDestroy;
    pNew->DevPort.pfnGetRegionInfo      = vboxPciDevGetRegionInfo;
    pNew->DevPort.pfnMapRegion          = vboxPciDevMapRegion;
    pNew->DevPort.pfnUnmapRegion        = vboxPciDevUnmapRegion;
    pNew->DevPort.pfnPciCfgRead         = vboxPciDevPciCfgRead;
    pNew->DevPort.pfnPciCfgWrite        = vboxPciDevPciCfgWrite;
    pNew->DevPort.pfnPciCfgRead         = vboxPciDevPciCfgRead;
    pNew->DevPort.pfnPciCfgWrite        = vboxPciDevPciCfgWrite;
    pNew->DevPort.pfnRegisterIrqHandler = vboxPciDevRegisterIrqHandler;
    pNew->DevPort.pfnUnregisterIrqHandler = vboxPciDevUnregisterIrqHandler;
    pNew->DevPort.pfnPowerStateChange   = vboxPciDevPowerStateChange;
    pNew->DevPort.u32VersionEnd         = RAWPCIDEVPORT_VERSION;

    rc = RTSpinlockCreate(&pNew->hSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VBoxPCI");
    if (RT_SUCCESS(rc))
    {
        rc = RTSemFastMutexCreate(&pNew->hFastMtx);
        if (RT_SUCCESS(rc))
        {
            rc = pNew->DevPort.pfnInit(&pNew->DevPort, fFlags);
            if (RT_SUCCESS(rc))
            {
                *ppDevPort = &pNew->DevPort;

                pNew->pNext = pGlobals->pInstanceHead;
                pGlobals->pInstanceHead = pNew;
            }
            else
            {
                RTSemFastMutexDestroy(pNew->hFastMtx);
                RTSpinlockDestroy(pNew->hSpinlock);
                RTMemFree(pNew);
            }
        }
    }

    return rc;
}

/**
 * @interface_method_impl{RAWPCIFACTORY,pfnCreateAndConnect}
 */
static DECLCALLBACK(int) vboxPciFactoryCreateAndConnect(PRAWPCIFACTORY       pFactory,
                                                        uint32_t             u32HostAddress,
                                                        uint32_t             fFlags,
                                                        PRAWPCIPERVM         pVmCtx,
                                                        PRAWPCIDEVPORT       *ppDevPort,
                                                        uint32_t             *pfDevFlags)
{
    PVBOXRAWPCIGLOBALS pGlobals = (PVBOXRAWPCIGLOBALS)((uint8_t *)pFactory - RT_OFFSETOF(VBOXRAWPCIGLOBALS, RawPciFactory));
    int rc;

    LogFlow(("vboxPciFactoryCreateAndConnect: PCI=%x fFlags=%#x\n", u32HostAddress, fFlags));
    Assert(pGlobals->cFactoryRefs > 0);
    rc = vboxPciGlobalsLock(pGlobals);
    AssertRCReturn(rc, rc);

    /* First search if there's no existing instance with same host device
     * address - if so - we cannot continue.
     */
    if (vboxPciFindInstanceLocked(pGlobals, u32HostAddress) != NULL)
    {
        rc = VERR_RESOURCE_BUSY;
        goto unlock;
    }

    rc = vboxPciNewInstance(pGlobals, u32HostAddress, fFlags, pVmCtx, ppDevPort, pfDevFlags);

unlock:
    vboxPciGlobalsUnlock(pGlobals);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIFACTORY,pfnRelease}
 */
static DECLCALLBACK(void) vboxPciFactoryRelease(PRAWPCIFACTORY pFactory)
{
    PVBOXRAWPCIGLOBALS pGlobals = (PVBOXRAWPCIGLOBALS)((uint8_t *)pFactory - RT_OFFSETOF(VBOXRAWPCIGLOBALS, RawPciFactory));

    int32_t cRefs = ASMAtomicDecS32(&pGlobals->cFactoryRefs);
    Assert(cRefs >= 0); NOREF(cRefs);
    LogFlow(("vboxPciFactoryRelease: cRefs=%d (new)\n", cRefs));
}

/**
 * @interface_method_impl{RAWPCIFACTORY,pfnInitVm}
 */
static DECLCALLBACK(int)  vboxPciFactoryInitVm(PRAWPCIFACTORY       pFactory,
                                               PVM                  pVM,
                                               PRAWPCIPERVM         pVmData)
{
    PVBOXRAWPCIDRVVM pThis = (PVBOXRAWPCIDRVVM)RTMemAllocZ(sizeof(VBOXRAWPCIDRVVM));
    int rc;

    if (!pThis)
         return VERR_NO_MEMORY;

    rc = RTSemFastMutexCreate(&pThis->hFastMtx);
    if (RT_SUCCESS(rc))
    {
        rc = vboxPciOsInitVm(pThis, pVM, pVmData);

        if (RT_SUCCESS(rc))
        {
#ifdef VBOX_WITH_IOMMU
            /* If IOMMU notification routine in pVmData->pfnContigMemInfo
               is set - we have functional IOMMU hardware. */
            if (pVmData->pfnContigMemInfo)
                pVmData->fVmCaps |= PCIRAW_VMFLAGS_HAS_IOMMU;
#endif
            pThis->pPerVmData = pVmData;
            pVmData->pDriverData = pThis;
            return VINF_SUCCESS;
        }

        RTSemFastMutexDestroy(pThis->hFastMtx);
        pThis->hFastMtx = NIL_RTSEMFASTMUTEX;
        RTMemFree(pThis);
    }

    return rc;
}

/**
 * @interface_method_impl{RAWPCIFACTORY,pfnDeinitVm}
 */
static DECLCALLBACK(void)  vboxPciFactoryDeinitVm(PRAWPCIFACTORY       pFactory,
                                                  PVM                  pVM,
                                                  PRAWPCIPERVM         pVmData)
{
    if (pVmData->pDriverData)
    {
        PVBOXRAWPCIDRVVM pThis = (PVBOXRAWPCIDRVVM)pVmData->pDriverData;

#ifdef VBOX_WITH_IOMMU
        /* If we have IOMMU, need to unmap all guest's physical pages from IOMMU on VM termination. */
#endif

        vboxPciOsDeinitVm(pThis, pVM);

        if (pThis->hFastMtx)
        {
            RTSemFastMutexDestroy(pThis->hFastMtx);
            pThis->hFastMtx = NIL_RTSEMFASTMUTEX;
        }

        RTMemFree(pThis);
        pVmData->pDriverData = NULL;
    }
}


static bool vboxPciCanUnload(PVBOXRAWPCIGLOBALS pGlobals)
{
    int rc = vboxPciGlobalsLock(pGlobals);
    bool fRc = !pGlobals->pInstanceHead
            && pGlobals->cFactoryRefs <= 0;
    vboxPciGlobalsUnlock(pGlobals);
    AssertRC(rc);
    return fRc;
}


static int vboxPciInitIdc(PVBOXRAWPCIGLOBALS pGlobals)
{
    int rc;
    Assert(!pGlobals->fIDCOpen);

    /*
     * Establish a connection to SUPDRV and register our component factory.
     */
    rc = SUPR0IdcOpen(&pGlobals->SupDrvIDC, 0 /* iReqVersion = default */, 0 /* iMinVersion = default */, NULL, NULL, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = SUPR0IdcComponentRegisterFactory(&pGlobals->SupDrvIDC, &pGlobals->SupDrvFactory);
        if (RT_SUCCESS(rc))
        {
            pGlobals->fIDCOpen = true;
            Log(("VBoxRawPci: pSession=%p\n", SUPR0IdcGetSession(&pGlobals->SupDrvIDC)));
            return rc;
        }

        /* bail out. */
        LogRel(("VBoxRawPci: Failed to register component factory, rc=%Rrc\n", rc));
        SUPR0IdcClose(&pGlobals->SupDrvIDC);
    }

    return rc;
}


/**
 * Try to close the IDC connection to SUPDRV if established.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_WRONG_ORDER if we're busy.
 *
 * @param   pGlobals        Pointer to the globals.
 */
static int vboxPciDeleteIdc(PVBOXRAWPCIGLOBALS pGlobals)
{
    int rc;

    Assert(pGlobals->hFastMtx != NIL_RTSEMFASTMUTEX);

    /*
     * Check before trying to deregister the factory.
     */
    if (!vboxPciCanUnload(pGlobals))
        return VERR_WRONG_ORDER;

    if (!pGlobals->fIDCOpen)
        rc = VINF_SUCCESS;
    else
    {
        /*
         * Disconnect from SUPDRV.
         */
        rc = SUPR0IdcComponentDeregisterFactory(&pGlobals->SupDrvIDC, &pGlobals->SupDrvFactory);
        AssertRC(rc);
        SUPR0IdcClose(&pGlobals->SupDrvIDC);
        pGlobals->fIDCOpen = false;
    }

    return rc;
}


/**
 * Initializes the globals.
 *
 * @returns VBox status code.
 * @param   pGlobals        Pointer to the globals.
 */
static int vboxPciInitGlobals(PVBOXRAWPCIGLOBALS pGlobals)
{
    /*
     * Initialize the common portions of the structure.
     */
    int rc = RTSemFastMutexCreate(&pGlobals->hFastMtx);
    if (RT_SUCCESS(rc))
    {
        pGlobals->pInstanceHead = NULL;
        pGlobals->RawPciFactory.pfnRelease = vboxPciFactoryRelease;
        pGlobals->RawPciFactory.pfnCreateAndConnect = vboxPciFactoryCreateAndConnect;
        pGlobals->RawPciFactory.pfnInitVm = vboxPciFactoryInitVm;
        pGlobals->RawPciFactory.pfnDeinitVm = vboxPciFactoryDeinitVm;
        memcpy(pGlobals->SupDrvFactory.szName, "VBoxRawPci", sizeof("VBoxRawPci"));
        pGlobals->SupDrvFactory.pfnQueryFactoryInterface = vboxPciQueryFactoryInterface;
        pGlobals->fIDCOpen = false;
    }
    return rc;
}


/**
 * Deletes the globals.
 *
 * @param   pGlobals        Pointer to the globals.
 */
static void vboxPciDeleteGlobals(PVBOXRAWPCIGLOBALS pGlobals)
{
    Assert(!pGlobals->fIDCOpen);

    /*
     * Release resources.
     */
    if (pGlobals->hFastMtx)
    {
        RTSemFastMutexDestroy(pGlobals->hFastMtx);
        pGlobals->hFastMtx = NIL_RTSEMFASTMUTEX;
    }
}


int  vboxPciInit(PVBOXRAWPCIGLOBALS pGlobals)
{

    /*
     * Initialize the common portions of the structure.
     */
    int rc = vboxPciInitGlobals(pGlobals);
    if (RT_SUCCESS(rc))
    {
        rc = vboxPciInitIdc(pGlobals);
        if (RT_SUCCESS(rc))
            return rc;

        /* bail out. */
        vboxPciDeleteGlobals(pGlobals);
    }

    return rc;
}

void vboxPciShutdown(PVBOXRAWPCIGLOBALS pGlobals)
{
    int rc = vboxPciDeleteIdc(pGlobals);
    if (RT_SUCCESS(rc))
        vboxPciDeleteGlobals(pGlobals);
}

