/* $Id: SrvPciRawR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PCI passthrough - The ring 0 service.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_PCI_RAW
#include <VBox/log.h>
#include <VBox/sup.h>
#include <VBox/rawpci.h>
#include <VBox/vmm/pdmpci.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/gvm.h>
#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/vmcc.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/handletable.h>
#include <iprt/mp.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/asm-amd64-x86.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct PCIRAWSRVSTATE
{
    /** Structure lock. */
    RTSPINLOCK      hSpinlock;

    /** Handle table for devices. */
    RTHANDLETABLE   hHtDevs;

} PCIRAWSRVSTATE;
typedef PCIRAWSRVSTATE *PPCIRAWSRVSTATE;

typedef struct PCIRAWDEV
{
    /* Port pointer. */
    PRAWPCIDEVPORT     pPort;

    /* Handle used by everybody else. */
    PCIRAWDEVHANDLE    hHandle;

      /** The session this device is associated with. */
    PSUPDRVSESSION     pSession;

    /** Structure lock. */
    RTSPINLOCK         hSpinlock;

    /** Event for IRQ updates. */
    RTSEMEVENT         hIrqEvent;

    /** Current pending IRQ for the device. */
    int32_t            iPendingIrq;

    /** ISR handle. */
    PCIRAWISRHANDLE    hIsr;

    /* If object is being destroyed. */
    bool               fTerminate;

    /** The SUPR0 object. */
    void               *pvObj;
} PCIRAWDEV;
typedef PCIRAWDEV *PPCIRAWDEV;

static PCIRAWSRVSTATE g_State;


/** Interrupt handler. Could be called in the interrupt context,
 * depending on host OS implmenetation. */
static DECLCALLBACK(bool) pcirawr0Isr(void* pContext, int32_t iHostIrq)
{
    PPCIRAWDEV    pThis = (PPCIRAWDEV)pContext;

#ifdef VBOX_WITH_SHARED_PCI_INTERRUPTS
    uint16_t      uStatus;
    PCIRAWMEMLOC  Loc;
    int           rc;

    Loc.cb = 2;
    rc = pThis->pPort->pfnPciCfgRead(pThis->pPort, VBOX_PCI_STATUS, &Loc);
    /* Cannot read, assume non-shared. */
    if (RT_FAILURE(rc))
        return false;

    /* Check interrupt status bit. */
    if ((Loc.u.u16 & (1 << 3)) == 0)
        return false;
#endif

    RTSpinlockAcquire(pThis->hSpinlock);
    pThis->iPendingIrq = iHostIrq;
    RTSpinlockRelease(pThis->hSpinlock);

    /**
     * @todo RTSemEventSignal() docs claims that it's platform-dependent
     * if RTSemEventSignal() could be called from the ISR, but it seems IPRT
     * doesn't provide primitives that guaranteed to work this way.
     */
    RTSemEventSignal(pThis->hIrqEvent);

    return true;
}

static DECLCALLBACK(int) pcirawr0DevRetainHandle(RTHANDLETABLE hHandleTable, void *pvObj, void *pvCtx, void *pvUser)
{
    NOREF(pvUser);
    NOREF(hHandleTable);
    PPCIRAWDEV pDev = (PPCIRAWDEV)pvObj;
    if (pDev->hHandle != 0)
        return SUPR0ObjAddRefEx(pDev->pvObj, (PSUPDRVSESSION)pvCtx, true /* fNoBlocking */);

    return VINF_SUCCESS;
}


/**
 * Initializes the raw PCI ring-0 service.
 *
 * @returns VBox status code.
 */
PCIRAWR0DECL(int) PciRawR0Init(void)
{
    LogFlow(("PciRawR0Init:\n"));
    int rc = VINF_SUCCESS;

    rc = RTHandleTableCreateEx(&g_State.hHtDevs, RTHANDLETABLE_FLAGS_LOCKED | RTHANDLETABLE_FLAGS_CONTEXT,
                               UINT32_C(0xfefe0000), 4096, pcirawr0DevRetainHandle, NULL);

    LogFlow(("PciRawR0Init: returns %Rrc\n", rc));
    return rc;
}

/**
 * Destroys raw PCI ring-0 service.
 */
PCIRAWR0DECL(void) PciRawR0Term(void)
{
    LogFlow(("PciRawR0Term:\n"));
    RTHandleTableDestroy(g_State.hHtDevs, NULL, NULL);
    g_State.hHtDevs = NIL_RTHANDLETABLE;
}


/**
 * Per-VM R0 module init.
 */
PCIRAWR0DECL(int)  PciRawR0InitVM(PGVM pGVM)
{
    PRAWPCIFACTORY pFactory = NULL;
    int rc = SUPR0ComponentQueryFactory(pGVM->pSession, "VBoxRawPci", RAWPCIFACTORY_UUID_STR, (void **)&pFactory);
    if (RT_SUCCESS(rc))
    {
        if (pFactory)
        {
            rc = pFactory->pfnInitVm(pFactory, pGVM, &pGVM->rawpci.s);
            pFactory->pfnRelease(pFactory);
        }
    }
    return VINF_SUCCESS;
}

/**
 * Per-VM R0 module termination routine.
 */
PCIRAWR0DECL(void)  PciRawR0TermVM(PGVM pGVM)
{
    PRAWPCIFACTORY pFactory = NULL;
    int rc = SUPR0ComponentQueryFactory(pGVM->pSession, "VBoxRawPci", RAWPCIFACTORY_UUID_STR, (void **)&pFactory);
    if (RT_SUCCESS(rc))
    {
        if (pFactory)
        {
            pFactory->pfnDeinitVm(pFactory, pGVM, &pGVM->rawpci.s);
            pFactory->pfnRelease(pFactory);
        }
    }
}

static int pcirawr0DevTerm(PPCIRAWDEV pThis, int32_t fFlags)
{
    ASMAtomicWriteBool(&pThis->fTerminate, true);

    if (pThis->hIrqEvent)
        RTSemEventSignal(pThis->hIrqEvent);

    /* Enable that, once figure our how to make sure
       IRQ getter thread notified and woke up. */
#if 0
    if (pThis->hIrqEvent)
    {
        RTSemEventDestroy(pThis->hIrqEvent);
        pThis->hIrqEvent = NIL_RTSEMEVENT;
    }
#endif

    if (pThis->hSpinlock)
    {
        RTSpinlockDestroy(pThis->hSpinlock);
        pThis->hSpinlock = NIL_RTSPINLOCK;
    }

    /* Forcefully deinit. */
    return pThis->pPort->pfnDeinit(pThis->pPort, fFlags);
}

#define GET_PORT(hDev)                                                  \
    PPCIRAWDEV pDev = (PPCIRAWDEV)RTHandleTableLookupWithCtx(g_State.hHtDevs, hDev, pSession); \
    if (!pDev)                                                          \
        return VERR_INVALID_HANDLE;                                     \
    PRAWPCIDEVPORT pDevPort = pDev->pPort;                              \
    AssertReturn(pDevPort != NULL, VERR_INVALID_PARAMETER);             \
    AssertReturn(pDevPort->u32Version    == RAWPCIDEVPORT_VERSION, VERR_INVALID_PARAMETER); \
    AssertReturn(pDevPort->u32VersionEnd == RAWPCIDEVPORT_VERSION, VERR_INVALID_PARAMETER);

#define PUT_PORT() if (pDev->pvObj) SUPR0ObjRelease(pDev->pvObj, pSession)

#ifdef DEBUG_nike

/* Code to perform debugging without host driver. */
typedef struct DUMMYRAWPCIINS
{
    /* Host PCI address of this device. */
    uint32_t           HostPciAddress;
    /* Padding */
    uint32_t           pad0;

    uint8_t            aPciCfg[256];

    /** Port, given to the outside world. */
    RAWPCIDEVPORT      DevPort;
} DUMMYRAWPCIINS;
typedef struct DUMMYRAWPCIINS *PDUMMYRAWPCIINS;

#define DEVPORT_2_DUMMYRAWPCIINS(pPort) \
    ( (PDUMMYRAWPCIINS)((uint8_t *)pPort - RT_UOFFSETOF(DUMMYRAWPCIINS, DevPort)) )

static uint8_t dummyPciGetByte(PDUMMYRAWPCIINS pThis, uint32_t iRegister)
{
    return pThis->aPciCfg[iRegister];
}

static void dummyPciSetByte(PDUMMYRAWPCIINS pThis, uint32_t iRegister, uint8_t u8)
{
    pThis->aPciCfg[iRegister] = u8;
}

static uint16_t dummyPciGetWord(PDUMMYRAWPCIINS pThis, uint32_t iRegister)
{
    uint16_t u16Value = *(uint16_t*)&pThis->aPciCfg[iRegister];
    return RT_H2LE_U16(u16Value);
}

static void dummyPciSetWord(PDUMMYRAWPCIINS pThis, uint32_t iRegister, uint16_t u16)
{
    *(uint16_t*)&pThis->aPciCfg[iRegister] = RT_H2LE_U16(u16);
}

static uint32_t dummyPciGetDWord(PDUMMYRAWPCIINS pThis, uint32_t iRegister)
{
    uint32_t u32Value = *(uint32_t*)&pThis->aPciCfg[iRegister];
    return RT_H2LE_U32(u32Value);
}

static void dummyPciSetDWord(PDUMMYRAWPCIINS pThis, uint32_t iRegister, uint32_t u32)
{
    *(uint32_t*)&pThis->aPciCfg[iRegister] = RT_H2LE_U32(u32);
}

/**
 * @copydoc RAWPCIDEVPORT:: pfnInit
 */
static DECLCALLBACK(int) dummyPciDevInit(PRAWPCIDEVPORT pPort, uint32_t fFlags)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);

    dummyPciSetWord(pThis, VBOX_PCI_VENDOR_ID, 0xccdd);
    dummyPciSetWord(pThis, VBOX_PCI_DEVICE_ID, 0xeeff);
    dummyPciSetWord(pThis, VBOX_PCI_COMMAND,   PCI_COMMAND_IOACCESS | PCI_COMMAND_MEMACCESS | PCI_COMMAND_BUSMASTER);
    dummyPciSetByte(pThis, VBOX_PCI_INTERRUPT_PIN, 1);

    return VINF_SUCCESS;
}

/**
 * @copydoc RAWPCIDEVPORT:: pfnDeinit
 */
static DECLCALLBACK(int) dummyPciDevDeinit(PRAWPCIDEVPORT pPort, uint32_t fFlags)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);

    return VINF_SUCCESS;
}

/**
 * @copydoc RAWPCIDEVPORT:: pfnDestroy
 */
static DECLCALLBACK(int) dummyPciDevDestroy(PRAWPCIDEVPORT pPort)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);

    RTMemFree(pThis);

    return VINF_SUCCESS;
}


/**
 * @copydoc RAWPCIDEVPORT:: pfnGetRegionInfo
 */
static DECLCALLBACK(int) dummyPciDevGetRegionInfo(PRAWPCIDEVPORT pPort,
                                                  int32_t        iRegion,
                                                  RTHCPHYS       *pRegionStart,
                                                  uint64_t       *pu64RegionSize,
                                                  bool           *pfPresent,
                                                  uint32_t       *pfFlags)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);

    if (iRegion == 0)
    {
        *pfPresent = true;
        *pRegionStart = 0xfef0;
        *pu64RegionSize = 0x10;
        *pfFlags = PCIRAW_ADDRESS_SPACE_IO;
    }
    else if (iRegion == 2)
    {
        *pfPresent = true;
        *pRegionStart = 0xffff0000;
        *pu64RegionSize = 0x1000;
        *pfFlags = PCIRAW_ADDRESS_SPACE_BAR64 | PCIRAW_ADDRESS_SPACE_MEM;
    }
    else
        *pfPresent = false;

    return VINF_SUCCESS;
}

/**
 * @copydoc RAWPCIDEVPORT:: pfnMapRegion
 */
static DECLCALLBACK(int) dummyPciDevMapRegion(PRAWPCIDEVPORT pPort,
                                              int32_t        iRegion,
                                              RTHCPHYS       HCRegionStart,
                                              uint64_t       u64RegionSize,
                                              int32_t        fFlags,
                                              RTR0PTR        *pRegionBase)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);
    return VINF_SUCCESS;
}

/**
 * @copydoc RAWPCIDEVPORT:: pfnUnapRegion
 */
static DECLCALLBACK(int) dummyPciDevUnmapRegion(PRAWPCIDEVPORT pPort,
                                                int32_t        iRegion,
                                                RTHCPHYS       HCRegionStart,
                                                uint64_t       u64RegionSize,
                                                RTR0PTR        RegionBase)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);
    return VINF_SUCCESS;
}

/**
 * @copydoc RAWPCIDEVPORT:: pfnPciCfgRead
 */
static DECLCALLBACK(int) dummyPciDevPciCfgRead(PRAWPCIDEVPORT     pPort,
                                               uint32_t           Register,
                                               PCIRAWMEMLOC      *pValue)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);

    switch (pValue->cb)
    {
        case 1:
            pValue->u.u8  = dummyPciGetByte(pThis, Register);
            break;
        case 2:
            pValue->u.u16 = dummyPciGetWord(pThis, Register);
            break;
        case 4:
            pValue->u.u32 = dummyPciGetDWord(pThis, Register);
            break;
    }

    return VINF_SUCCESS;
}

/**
 * @copydoc RAWPCIDEVPORT:: pfnPciCfgWrite
 */
static DECLCALLBACK(int) dummyPciDevPciCfgWrite(PRAWPCIDEVPORT pPort,
                                                uint32_t       Register,
                                                PCIRAWMEMLOC   *pValue)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);

    switch (pValue->cb)
    {
        case 1:
            dummyPciSetByte(pThis, Register, pValue->u.u8);
            break;
        case 2:
            dummyPciSetWord(pThis, Register, pValue->u.u16);
            break;
        case 4:
            dummyPciSetDWord(pThis, Register, pValue->u.u32);
            break;
    }

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) dummyPciDevRegisterIrqHandler(PRAWPCIDEVPORT   pPort,
                                                       PFNRAWPCIISR     pfnHandler,
                                                       void*            pIrqContext,
                                                       PCIRAWISRHANDLE  *phIsr)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) dummyPciDevUnregisterIrqHandler(PRAWPCIDEVPORT   pPort,
                                                         PCIRAWISRHANDLE  hIsr)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) dummyPciDevPowerStateChange(PRAWPCIDEVPORT    pPort,
                                                     PCIRAWPOWERSTATE  aState,
                                                     uint64_t          *pu64Param)
{
    PDUMMYRAWPCIINS pThis = DEVPORT_2_DUMMYRAWPCIINS(pPort);
    return VINF_SUCCESS;
}

static PRAWPCIDEVPORT pcirawr0CreateDummyDevice(uint32_t HostDevice, uint32_t fFlags)
{
    PDUMMYRAWPCIINS pNew = (PDUMMYRAWPCIINS)RTMemAllocZ(sizeof(*pNew));
    if (!pNew)
        return NULL;

    pNew->HostPciAddress                  = HostDevice;

    pNew->DevPort.u32Version              = RAWPCIDEVPORT_VERSION;
    pNew->DevPort.pfnInit                 = dummyPciDevInit;
    pNew->DevPort.pfnDeinit               = dummyPciDevDeinit;
    pNew->DevPort.pfnDestroy              = dummyPciDevDestroy;
    pNew->DevPort.pfnGetRegionInfo        = dummyPciDevGetRegionInfo;
    pNew->DevPort.pfnMapRegion            = dummyPciDevMapRegion;
    pNew->DevPort.pfnUnmapRegion          = dummyPciDevUnmapRegion;
    pNew->DevPort.pfnPciCfgRead           = dummyPciDevPciCfgRead;
    pNew->DevPort.pfnPciCfgWrite          = dummyPciDevPciCfgWrite;
    pNew->DevPort.pfnRegisterIrqHandler   = dummyPciDevRegisterIrqHandler;
    pNew->DevPort.pfnUnregisterIrqHandler = dummyPciDevUnregisterIrqHandler;
    pNew->DevPort.pfnPowerStateChange     = dummyPciDevPowerStateChange;

    pNew->DevPort.u32VersionEnd           = RAWPCIDEVPORT_VERSION;

    return &pNew->DevPort;
}

#endif /* DEBUG_nike */

static DECLCALLBACK(void) pcirawr0DevObjDestructor(void *pvObj, void *pvIns, void *pvUnused)
{
    PPCIRAWDEV  pThis = (PPCIRAWDEV)pvIns;
    NOREF(pvObj); NOREF(pvUnused);

    /* Forcefully deinit. */
    pcirawr0DevTerm(pThis, 0);

    /* And destroy. */
    pThis->pPort->pfnDestroy(pThis->pPort);

    RTMemFree(pThis);
}


static int pcirawr0OpenDevice(PGVM pGVM, PSUPDRVSESSION pSession,
                              uint32_t         HostDevice,
                              uint32_t         fFlags,
                              PCIRAWDEVHANDLE *pHandle,
                              uint32_t        *pfDevFlags)
{

    int rc = GVMMR0ValidateGVMandEMT(pGVM, 0 /*idCpu*/);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Query the factory we want, then use it create and connect the host device.
     */
    PPCIRAWDEV pNew = (PPCIRAWDEV)RTMemAllocZ(sizeof(*pNew));
    if (!pNew)
        return VERR_NO_MEMORY;

    PRAWPCIFACTORY pFactory = NULL;
    rc = SUPR0ComponentQueryFactory(pSession, "VBoxRawPci", RAWPCIFACTORY_UUID_STR, (void **)&pFactory);
    /* No host driver registered, provide some fake implementation
       for debugging purposes. */
    PRAWPCIDEVPORT pDevPort = NULL;
#ifdef DEBUG_nike
    if (rc == VERR_SUPDRV_COMPONENT_NOT_FOUND)
    {
        pDevPort = pcirawr0CreateDummyDevice(HostDevice, fFlags);
        if (pDevPort)
        {
            pDevPort->pfnInit(pDevPort, fFlags);
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;
    }
#endif

    if (RT_SUCCESS(rc))
    {
        if (pFactory)
        {
            rc = pFactory->pfnCreateAndConnect(pFactory,
                                               HostDevice,
                                               fFlags,
                                               &pGVM->rawpci.s,
                                               &pDevPort,
                                               pfDevFlags);
            pFactory->pfnRelease(pFactory);
        }

        if (RT_SUCCESS(rc))
        {
            rc = RTSpinlockCreate(&pNew->hSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "PciRaw");
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                rc = RTSemEventCreate(&pNew->hIrqEvent);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    pNew->pSession = pSession;
                    pNew->pPort    = pDevPort;
                    pNew->pvObj    = SUPR0ObjRegister(pSession, SUPDRVOBJTYPE_RAW_PCI_DEVICE,
                                                      pcirawr0DevObjDestructor, pNew, NULL);
                    if (pNew->pvObj)
                    {

                        uint32_t hHandle = 0;
                        rc = RTHandleTableAllocWithCtx(g_State.hHtDevs, pNew, pSession, &hHandle);
                        if (RT_SUCCESS(rc))
                        {
                            pNew->hHandle = (PCIRAWDEVHANDLE)hHandle;
                            *pHandle = pNew->hHandle;
                            return rc;
                        }
                        SUPR0ObjRelease(pNew->pvObj, pSession);
                    }
                    RTSemEventDestroy(pNew->hIrqEvent);
                }
                RTSpinlockDestroy(pNew->hSpinlock);
            }
        }
    }

    if (RT_FAILURE(rc))
        RTMemFree(pNew);

    return rc;
}

static int pcirawr0CloseDevice(PSUPDRVSESSION   pSession,
                               PCIRAWDEVHANDLE  TargetDevice,
                               uint32_t         fFlags)
{
    GET_PORT(TargetDevice);
    int rc;

    pDevPort->pfnUnregisterIrqHandler(pDevPort, pDev->hIsr);
    pDev->hIsr = 0;

    rc = pcirawr0DevTerm(pDev, fFlags);

    RTHandleTableFreeWithCtx(g_State.hHtDevs, TargetDevice, pSession);

    PUT_PORT();

    return rc;
}

/* We may want to call many functions here directly, so no static */
static int pcirawr0GetRegionInfo(PSUPDRVSESSION   pSession,
                                 PCIRAWDEVHANDLE  TargetDevice,
                                 int32_t          iRegion,
                                 RTHCPHYS         *pRegionStart,
                                 uint64_t         *pu64RegionSize,
                                 bool             *pfPresent,
                                 uint32_t         *pfFlags)
{
    LogFlow(("pcirawr0GetRegionInfo: %d\n", iRegion));
    GET_PORT(TargetDevice);

    int rc = pDevPort->pfnGetRegionInfo(pDevPort, iRegion, pRegionStart, pu64RegionSize, pfPresent, pfFlags);

    PUT_PORT();

    return rc;
}

static int pcirawr0MapRegion(PSUPDRVSESSION   pSession,
                             PCIRAWDEVHANDLE  TargetDevice,
                             int32_t          iRegion,
                             RTHCPHYS         HCRegionStart,
                             uint64_t         u64RegionSize,
                             uint32_t         fFlags,
                             RTR3PTR          *ppvAddressR3,
                             RTR0PTR          *ppvAddressR0)
{
    LogFlow(("pcirawr0MapRegion\n"));
    GET_PORT(TargetDevice);
    int rc;

    rc = pDevPort->pfnMapRegion(pDevPort, iRegion, HCRegionStart, u64RegionSize, fFlags, ppvAddressR0);
    if (RT_SUCCESS(rc))
    {
        Assert(*ppvAddressR0 != NULL);

        /* Do we need to do something to help with R3 mapping, if ((fFlags & PCIRAWRFLAG_ALLOW_R3MAP) != 0) */
    }

    *ppvAddressR3 = 0;

    PUT_PORT();

    return rc;
}

static int pcirawr0UnmapRegion(PSUPDRVSESSION   pSession,
                               PCIRAWDEVHANDLE  TargetDevice,
                               int32_t          iRegion,
                               RTHCPHYS         HCRegionStart,
                               uint64_t         u64RegionSize,
                               RTR3PTR          pvAddressR3,
                               RTR0PTR          pvAddressR0)
{
    LogFlow(("pcirawr0UnmapRegion\n"));
    int rc;
    NOREF(pSession); NOREF(pvAddressR3);

    GET_PORT(TargetDevice);

    rc = pDevPort->pfnUnmapRegion(pDevPort, iRegion, HCRegionStart, u64RegionSize, pvAddressR0);

    PUT_PORT();

    return rc;
}

static int pcirawr0PioWrite(PSUPDRVSESSION  pSession,
                            PCIRAWDEVHANDLE TargetDevice,
                            uint16_t        Port,
                            uint32_t        u32,
                            unsigned        cb)
{
    NOREF(pSession); NOREF(TargetDevice);
    /// @todo add check that port fits into device range
    switch (cb)
    {
        case 1:
            ASMOutU8 (Port, u32);
            break;
        case 2:
            ASMOutU16(Port, u32);
            break;
        case 4:
            ASMOutU32(Port, u32);
            break;
        default:
            AssertMsgFailed(("Unhandled port write: %d\n", cb));
    }

    return VINF_SUCCESS;
}


static int pcirawr0PioRead(PSUPDRVSESSION    pSession,
                           PCIRAWDEVHANDLE   TargetDevice,
                           uint16_t          Port,
                           uint32_t          *pu32,
                           unsigned          cb)
{
    NOREF(pSession); NOREF(TargetDevice);
    /// @todo add check that port fits into device range
    switch (cb)
    {
        case 1:
            *pu32 = ASMInU8 (Port);
            break;
        case 2:
            *pu32 = ASMInU16(Port);
            break;
        case 4:
            *pu32 = ASMInU32(Port);
            break;
        default:
            AssertMsgFailed(("Unhandled port read: %d\n", cb));
    }

    return VINF_SUCCESS;
}


static int pcirawr0MmioRead(PSUPDRVSESSION    pSession,
                            PCIRAWDEVHANDLE   TargetDevice,
                            RTR0PTR           Address,
                            PCIRAWMEMLOC      *pValue)
{
    NOREF(pSession); NOREF(TargetDevice);
    /// @todo add check that address fits into device range
#if 1
    switch (pValue->cb)
    {
        case 1:
            pValue->u.u8 = *(uint8_t*)Address;
            break;
        case 2:
            pValue->u.u16 = *(uint16_t*)Address;
            break;
        case 4:
            pValue->u.u32 = *(uint32_t*)Address;
            break;
        case 8:
            pValue->u.u64 = *(uint64_t*)Address;
            break;
    }
#else
    memset(&pValue->u.u64, 0, 8);
#endif
    return VINF_SUCCESS;
}

static int pcirawr0MmioWrite(PSUPDRVSESSION    pSession,
                             PCIRAWDEVHANDLE   TargetDevice,
                             RTR0PTR           Address,
                             PCIRAWMEMLOC      *pValue)
{
    NOREF(pSession); NOREF(TargetDevice);
    /// @todo add check that address fits into device range
#if 1
    switch (pValue->cb)
    {
        case 1:
            *(uint8_t*)Address  = pValue->u.u8;
            break;
        case 2:
            *(uint16_t*)Address  = pValue->u.u16;
            break;
        case 4:
            *(uint32_t*)Address  = pValue->u.u32;
            break;
        case 8:
            *(uint64_t*)Address  = pValue->u.u64;
            break;
    }
#endif
    return VINF_SUCCESS;
}

static int pcirawr0PciCfgRead(PSUPDRVSESSION    pSession,
                              PCIRAWDEVHANDLE   TargetDevice,
                              uint32_t          Register,
                              PCIRAWMEMLOC      *pValue)
{
    GET_PORT(TargetDevice);

    return pDevPort->pfnPciCfgRead(pDevPort, Register, pValue);
}

static int pcirawr0PciCfgWrite(PSUPDRVSESSION    pSession,
                               PCIRAWDEVHANDLE   TargetDevice,
                               uint32_t          Register,
                               PCIRAWMEMLOC      *pValue)
{
    int rc;

    GET_PORT(TargetDevice);

    rc = pDevPort->pfnPciCfgWrite(pDevPort, Register, pValue);

    PUT_PORT();

    return rc;
}

static int pcirawr0EnableIrq(PSUPDRVSESSION    pSession,
                             PCIRAWDEVHANDLE   TargetDevice)
{
    int            rc = VINF_SUCCESS;
    GET_PORT(TargetDevice);

    rc = pDevPort->pfnRegisterIrqHandler(pDevPort, pcirawr0Isr, pDev,
                                         &pDev->hIsr);

    PUT_PORT();
    return rc;
}

static int pcirawr0DisableIrq(PSUPDRVSESSION    pSession,
                              PCIRAWDEVHANDLE   TargetDevice)
{
    int            rc = VINF_SUCCESS;
    GET_PORT(TargetDevice);

    rc = pDevPort->pfnUnregisterIrqHandler(pDevPort, pDev->hIsr);
    pDev->hIsr = 0;

    PUT_PORT();
    return rc;
}

static int pcirawr0GetIrq(PSUPDRVSESSION    pSession,
                          PCIRAWDEVHANDLE   TargetDevice,
                          int64_t           iTimeout,
                          int32_t          *piIrq)
{
    int            rc = VINF_SUCCESS;
    bool           fTerminate = false;
    int32_t        iPendingIrq = 0;

    LogFlow(("pcirawr0GetIrq\n"));

    GET_PORT(TargetDevice);

    RTSpinlockAcquire(pDev->hSpinlock);
    iPendingIrq = pDev->iPendingIrq;
    pDev->iPendingIrq = 0;
    fTerminate = pDev->fTerminate;
    RTSpinlockRelease(pDev->hSpinlock);

    /* Block until new IRQs arrives */
    if (!fTerminate)
    {
        if (iPendingIrq == 0)
        {
            rc = RTSemEventWaitNoResume(pDev->hIrqEvent, iTimeout);
            if (RT_SUCCESS(rc))
            {
                /** @todo racy */
                if (!ASMAtomicReadBool(&pDev->fTerminate))
                {
                    RTSpinlockAcquire(pDev->hSpinlock);
                    iPendingIrq = pDev->iPendingIrq;
                    pDev->iPendingIrq = 0;
                    RTSpinlockRelease(pDev->hSpinlock);
                }
                else
                    rc = VERR_INTERRUPTED;
            }
        }

        if (RT_SUCCESS(rc))
            *piIrq = iPendingIrq;
    }
    else
        rc = VERR_INTERRUPTED;

    PUT_PORT();

    return rc;
}

static int pcirawr0PowerStateChange(PSUPDRVSESSION    pSession,
                                    PCIRAWDEVHANDLE   TargetDevice,
                                    PCIRAWPOWERSTATE  aState,
                                    uint64_t          *pu64Param)
{
    LogFlow(("pcirawr0PowerStateChange\n"));
    GET_PORT(TargetDevice);

    int rc = pDevPort->pfnPowerStateChange(pDevPort, aState, pu64Param);

    PUT_PORT();

    return rc;
}

/**
 * Process PCI raw request
 *
 * @returns VBox status code.
 */
PCIRAWR0DECL(int) PciRawR0ProcessReq(PGVM pGVM, PSUPDRVSESSION pSession, PPCIRAWSENDREQ pReq)
{
    LogFlow(("PciRawR0ProcessReq: %d for %x\n", pReq->iRequest, pReq->TargetDevice));
    int rc = VINF_SUCCESS;

    /* Route request to the host driver */
    switch (pReq->iRequest)
    {
        case PCIRAWR0_DO_OPEN_DEVICE:
            rc = pcirawr0OpenDevice(pGVM, pSession,
                                    pReq->u.aOpenDevice.PciAddress,
                                    pReq->u.aOpenDevice.fFlags,
                                    &pReq->u.aOpenDevice.Device,
                                    &pReq->u.aOpenDevice.fDevFlags);
            break;
        case PCIRAWR0_DO_CLOSE_DEVICE:
            rc = pcirawr0CloseDevice(pSession,
                                     pReq->TargetDevice,
                                     pReq->u.aCloseDevice.fFlags);
            break;
        case PCIRAWR0_DO_GET_REGION_INFO:
            rc = pcirawr0GetRegionInfo(pSession,
                                       pReq->TargetDevice,
                                       pReq->u.aGetRegionInfo.iRegion,
                                       &pReq->u.aGetRegionInfo.RegionStart,
                                       &pReq->u.aGetRegionInfo.u64RegionSize,
                                       &pReq->u.aGetRegionInfo.fPresent,
                                       &pReq->u.aGetRegionInfo.fFlags);
            break;
        case PCIRAWR0_DO_MAP_REGION:
            rc = pcirawr0MapRegion(pSession,
                                   pReq->TargetDevice,
                                   pReq->u.aMapRegion.iRegion,
                                   pReq->u.aMapRegion.StartAddress,
                                   pReq->u.aMapRegion.iRegionSize,
                                   pReq->u.aMapRegion.fFlags,
                                   &pReq->u.aMapRegion.pvAddressR3,
                                   &pReq->u.aMapRegion.pvAddressR0);
            break;
        case PCIRAWR0_DO_UNMAP_REGION:
            rc = pcirawr0UnmapRegion(pSession,
                                     pReq->TargetDevice,
                                     pReq->u.aUnmapRegion.iRegion,
                                     pReq->u.aUnmapRegion.StartAddress,
                                     pReq->u.aUnmapRegion.iRegionSize,
                                     pReq->u.aUnmapRegion.pvAddressR3,
                                     pReq->u.aUnmapRegion.pvAddressR0);
            break;
        case PCIRAWR0_DO_PIO_WRITE:
            rc = pcirawr0PioWrite(pSession,
                                  pReq->TargetDevice,
                                  pReq->u.aPioWrite.iPort,
                                  pReq->u.aPioWrite.iValue,
                                  pReq->u.aPioWrite.cb);
            break;
        case PCIRAWR0_DO_PIO_READ:
            rc = pcirawr0PioRead(pSession,
                                 pReq->TargetDevice,
                                 pReq->u.aPioRead.iPort,
                                 &pReq->u.aPioWrite.iValue,
                                 pReq->u.aPioRead.cb);
            break;
        case PCIRAWR0_DO_MMIO_WRITE:
            rc = pcirawr0MmioWrite(pSession,
                                   pReq->TargetDevice,
                                   pReq->u.aMmioWrite.Address,
                                   &pReq->u.aMmioWrite.Value);
            break;
        case PCIRAWR0_DO_MMIO_READ:
            rc = pcirawr0MmioRead(pSession,
                                  pReq->TargetDevice,
                                  pReq->u.aMmioRead.Address,
                                  &pReq->u.aMmioRead.Value);
            break;
        case PCIRAWR0_DO_PCICFG_WRITE:
            rc = pcirawr0PciCfgWrite(pSession,
                                     pReq->TargetDevice,
                                     pReq->u.aPciCfgWrite.iOffset,
                                     &pReq->u.aPciCfgWrite.Value);
            break;
        case PCIRAWR0_DO_PCICFG_READ:
            rc = pcirawr0PciCfgRead(pSession,
                                    pReq->TargetDevice,
                                    pReq->u.aPciCfgRead.iOffset,
                                   &pReq->u.aPciCfgRead.Value);
            break;
        case  PCIRAWR0_DO_ENABLE_IRQ:
            rc = pcirawr0EnableIrq(pSession,
                                   pReq->TargetDevice);
            break;
        case  PCIRAWR0_DO_DISABLE_IRQ:
            rc = pcirawr0DisableIrq(pSession,
                                   pReq->TargetDevice);
            break;
        case PCIRAWR0_DO_GET_IRQ:
            rc = pcirawr0GetIrq(pSession,
                                pReq->TargetDevice,
                                pReq->u.aGetIrq.iTimeout,
                                &pReq->u.aGetIrq.iIrq);
            break;
       case PCIRAWR0_DO_POWER_STATE_CHANGE:
            rc = pcirawr0PowerStateChange(pSession,
                                          pReq->TargetDevice,
                                          (PCIRAWPOWERSTATE)pReq->u.aPowerStateChange.iState,
                                          &pReq->u.aPowerStateChange.u64Param);
            break;
        default:
            rc = VERR_NOT_SUPPORTED;
    }

    LogFlow(("PciRawR0ProcessReq: returns %Rrc\n", rc));
    return rc;
}

