/* $Id: DevOxPcie958.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevOxPcie958 - Oxford Semiconductor OXPCIe958 PCI Express bridge to octal serial port emulation
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

/** @page pg_dev_oxpcie958   OXPCIe958 - Oxford Semiconductor OXPCIe958 PCI Express bridge to octal serial port emulation.
 *  @todo Write something
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_SERIAL
#include <VBox/pci.h>
#include <VBox/msi.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pdmpci.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/list.h>
#include <iprt/asm.h>

#include "VBoxDD.h"
#include "UartCore.h"


/** @name PCI device related constants.
 * @{ */
/** The PCI device ID. */
#define OX958_PCI_DEVICE_ID             0xc308
/** The PCI vendor ID. */
#define OX958_PCI_VENDOR_ID             0x1415
/** Where the MSI capability starts. */
#define OX958_PCI_MSI_CAP_OFS           0x80
/** Where the MSI-X capability starts. */
#define OX958_PCI_MSIX_CAP_OFS          (OX958_PCI_MSI_CAP_OFS + VBOX_MSI_CAP_SIZE_64)
/** The BAR for the MSI-X related functionality. */
#define OX958_PCI_MSIX_BAR              1
/** @} */

/** Maximum number of UARTs supported by the device. */
#define OX958_UARTS_MAX 16

/** Offset op the class code and revision ID register. */
#define OX958_REG_CC_REV_ID              0x00
/** Offset fof the UART count register. */
#define OX958_REG_UART_CNT               0x04
/** Offset of the global UART IRQ status register. */
#define OX958_REG_UART_IRQ_STS           0x08
/** Offset of the global UART IRQ enable register. */
#define OX958_REG_UART_IRQ_ENABLE        0x0c
/** Offset of the global UART IRQ disable register. */
#define OX958_REG_UART_IRQ_DISABLE       0x10
/** Offset of the global UART wake IRQ enable register. */
#define OX958_REG_UART_WAKE_IRQ_ENABLE   0x14
/** Offset of the global UART wake IRQ disable register. */
#define OX958_REG_UART_WAKE_IRQ_DISABLE  0x18
/** Offset of the region in MMIO space where the UARTs actually start. */
#define OX958_REG_UART_REGION_OFFSET     0x1000
/** Register region size for each UART. */
#define OX958_REG_UART_REGION_SIZE       0x200
/** Offset where the DMA channels registers start for each UART. */
#define OX958_REG_UART_DMA_REGION_OFFSET 0x100


/**
 * Shared OXPCIe958 UART core.
 */
typedef struct OX958UART
{
    /** The UART core. */
    UARTCORE                        UartCore;
    /** DMA address configured. */
    RTGCPHYS                        GCPhysDmaAddr;
    /** The DMA transfer length configured. */
    uint32_t                        cbDmaXfer;
    /** The DMA status registers. */
    uint32_t                        u32RegDmaSts;
} OX958UART;
/** Pointer to a shared OXPCIe958 UART core. */
typedef OX958UART *POX958UART;

/**
 * Ring-3 OXPCIe958 UART core.
 */
typedef struct OX958UARTR3
{
    /** The ring-3 UART core. */
    UARTCORER3                      UartCore;
} OX958UARTR3;
/** Pointer to a ring-3 OXPCIe958 UART core. */
typedef OX958UARTR3 *POX958UARTR3;

/**
 * Ring-0 OXPCIe958 UART core.
 */
typedef struct OX958UARTR0
{
    /** The ring-0 UART core. */
    UARTCORER0                      UartCore;
} OX958UARTR0;
/** Pointer to a ring-0 OXPCIe958 UART core. */
typedef OX958UARTR0 *POX958UARTR0;


/**
 * Raw-mode OXPCIe958 UART core.
 */
typedef struct OX958UARTRC
{
    /** The raw-mode UART core. */
    UARTCORERC                      UartCore;
} OX958UARTRC;
/** Pointer to a raw-mode OXPCIe958 UART core. */
typedef OX958UARTRC *POX958UARTRC;

/** Current context OXPCIe958 UART core. */
typedef CTX_SUFF(OX958UART) OX958UARTCC;
/** Pointer to a current context OXPCIe958 UART core. */
typedef CTX_SUFF(POX958UART) POX958UARTCC;


/**
 * Shared OXPCIe958 device instance data.
 */
typedef struct DEVOX958
{
    /** UART global IRQ status. */
    volatile uint32_t               u32RegIrqStsGlob;
    /** UART global IRQ enable mask. */
    volatile uint32_t               u32RegIrqEnGlob;
    /** UART wake IRQ enable mask. */
    volatile uint32_t               u32RegIrqEnWake;
    /** Number of UARTs configured. */
    uint32_t                        cUarts;
    /** Handle to the MMIO region (PCI region \#0). */
    IOMMMIOHANDLE                   hMmio;
    /** The UARTs. */
    OX958UART                       aUarts[OX958_UARTS_MAX];
} DEVOX958;
/** Pointer to shared OXPCIe958 device instance data. */
typedef DEVOX958 *PDEVOX958;

/**
 * Ring-3 OXPCIe958 device instance data.
 */
typedef struct DEVOX958R3
{
    /** The UARTs. */
    OX958UARTR3                     aUarts[OX958_UARTS_MAX];
} DEVOX958R3;
/** Pointer to ring-3 OXPCIe958 device instance data. */
typedef DEVOX958R3 *PDEVOX958R3;

/**
 * Ring-0 OXPCIe958 device instance data.
 */
typedef struct DEVOX958R0
{
    /** The UARTs. */
    OX958UARTR0                     aUarts[OX958_UARTS_MAX];
} DEVOX958R0;
/** Pointer to ring-0 OXPCIe958 device instance data. */
typedef DEVOX958R0 *PDEVOX958R0;

/**
 * Raw-mode OXPCIe958 device instance data.
 */
typedef struct DEVOX958RC
{
    /** The UARTs. */
    OX958UARTRC                     aUarts[OX958_UARTS_MAX];
} DEVOX958RC;
/** Pointer to raw-mode OXPCIe958 device instance data. */
typedef DEVOX958RC *PDEVOX958RC;

/** Current context OXPCIe958 device instance data. */
typedef CTX_SUFF(DEVOX958) DEVOX958CC;
/** Pointer to current context OXPCIe958 device instance data. */
typedef CTX_SUFF(PDEVOX958) PDEVOX958CC;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE



/**
 * Update IRQ status of the device.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared OXPCIe958 device instance data.
 */
static void ox958IrqUpdate(PPDMDEVINS pDevIns, PDEVOX958 pThis)
{
    uint32_t u32IrqSts = ASMAtomicReadU32(&pThis->u32RegIrqStsGlob);
    uint32_t u32IrqEn  = ASMAtomicReadU32(&pThis->u32RegIrqEnGlob);

    if (u32IrqSts & u32IrqEn)
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_HIGH);
    else
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_LOW);
}


/**
 * Performs a register read from the given UART.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared OXPCIe958 device instance data.
 * @param   pUart               The UART accessed, shared bits.
 * @param   pUartCC             The UART accessed, current context bits.
 * @param   offUartReg          Offset of the register being read.
 * @param   pv                  Where to store the read data.
 * @param   cb                  Number of bytes to read.
 */
static VBOXSTRICTRC ox958UartRegRead(PPDMDEVINS pDevIns, PDEVOX958 pThis, POX958UART pUart, POX958UARTCC pUartCC,
                                     uint32_t offUartReg, void *pv, unsigned cb)
{
    VBOXSTRICTRC rc;
    RT_NOREF(pThis);

    if (offUartReg >= OX958_REG_UART_DMA_REGION_OFFSET)
    {
        /* Access to the DMA registers. */
        rc = VINF_IOM_MMIO_UNUSED_00; /** @todo Misses implementation, just pretend there is nothing there yet. */
    }
    else /* Access UART registers. */
    {
        AssertReturn(cb == 1, VINF_IOM_MMIO_UNUSED_00);

        uint32_t u32 = 0;
        rc = uartRegRead(pDevIns, &pUart->UartCore, &pUartCC->UartCore, offUartReg, &u32, 1);
        Assert(rc == VINF_SUCCESS || rc == VINF_IOM_R3_IOPORT_READ);

        if (rc == VINF_SUCCESS)
            *(uint8_t *)pv = (uint8_t)u32;
        else if (rc == VERR_IOM_IOPORT_UNUSED)
            rc = VINF_IOM_MMIO_UNUSED_00;
        else if (rc == VINF_IOM_R3_IOPORT_READ)
            rc = VINF_IOM_R3_MMIO_READ;
    }

    return rc;
}


/**
 * Performs a register write to the given UART.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared OXPCIe958 device instance data.
 * @param   pUart               The UART accessed, shared bits.
 * @param   pUartCC             The UART accessed, current context bits.
 * @param   offUartReg          Offset of the register being written.
 * @param   pv                  The data to write.
 * @param   cb                  Number of bytes to write.
 */
static VBOXSTRICTRC ox958UartRegWrite(PPDMDEVINS pDevIns, PDEVOX958 pThis, POX958UART pUart, POX958UARTCC pUartCC,
                                      uint32_t offUartReg, const void *pv, unsigned cb)
{
    VBOXSTRICTRC rc;
    RT_NOREF(pThis);

    if (offUartReg >= OX958_REG_UART_DMA_REGION_OFFSET)
    {
        /* Access to the DMA registers. */
        rc = VINF_SUCCESS;
    }
    else /* Access UART registers. */
    {
        AssertReturn(cb == 1, VINF_SUCCESS);

        rc = uartRegWrite(pDevIns, &pUart->UartCore, &pUartCC->UartCore, offUartReg, *(const uint8_t *)pv, 1);
        Assert(rc == VINF_SUCCESS || rc == VINF_IOM_R3_IOPORT_WRITE);

        if (rc == VINF_IOM_R3_IOPORT_WRITE)
            rc = VINF_IOM_R3_MMIO_WRITE;
    }

    return rc;
}


/**
 * UART core IRQ request callback.
 *
 * @param   pDevIns     The device instance.
 * @param   pUart       The UART requesting an IRQ update.
 * @param   iLUN        The UART index.
 * @param   iLvl        IRQ level requested.
 */
static DECLCALLBACK(void) ox958IrqReq(PPDMDEVINS pDevIns, PUARTCORE pUart, unsigned iLUN, int iLvl)
{
    RT_NOREF(pUart);
    PDEVOX958 pThis = PDMDEVINS_2_DATA(pDevIns, PDEVOX958);

    if (iLvl)
        ASMAtomicOrU32(&pThis->u32RegIrqStsGlob, RT_BIT_32(iLUN));
    else
        ASMAtomicAndU32(&pThis->u32RegIrqStsGlob, ~RT_BIT_32(iLUN));
    ox958IrqUpdate(pDevIns, pThis);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) ox958MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDEVOX958    pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVOX958);
    PDEVOX958CC  pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVOX958CC);
    VBOXSTRICTRC rc      = VINF_SUCCESS;
    RT_NOREF(pvUser);

    if (off < OX958_REG_UART_REGION_OFFSET)
    {
        AssertReturn(cb == 4, VINF_IOM_MMIO_UNUSED_00);
        uint32_t *pu32 = (uint32_t *)pv;

        switch ((uint32_t)off)
        {
            case OX958_REG_CC_REV_ID:
                *pu32 = 0x00070002;
                break;
            case OX958_REG_UART_CNT:
                *pu32 = pThis->cUarts;
                break;
            case OX958_REG_UART_IRQ_STS:
                *pu32 = ASMAtomicReadU32(&pThis->u32RegIrqStsGlob);
                break;
            case OX958_REG_UART_IRQ_ENABLE:
                *pu32 = ASMAtomicReadU32(&pThis->u32RegIrqEnGlob);
                break;
            case OX958_REG_UART_IRQ_DISABLE:
                *pu32 = ~ASMAtomicReadU32(&pThis->u32RegIrqEnGlob);
                break;
            case OX958_REG_UART_WAKE_IRQ_ENABLE:
                *pu32 = ASMAtomicReadU32(&pThis->u32RegIrqEnWake);
                break;
            case OX958_REG_UART_WAKE_IRQ_DISABLE:
                *pu32 = ~ASMAtomicReadU32(&pThis->u32RegIrqEnWake);
                break;
            default:
                rc = VINF_IOM_MMIO_UNUSED_00;
        }
    }
    else
    {
        /* Figure out the UART accessed from the offset. */
        off -= OX958_REG_UART_REGION_OFFSET;
        uint32_t iUart      = (uint32_t)off / OX958_REG_UART_REGION_SIZE;
        uint32_t offUartReg = (uint32_t)off % OX958_REG_UART_REGION_SIZE;
        if (iUart < RT_MIN(pThis->cUarts, RT_ELEMENTS(pThis->aUarts)))
        {
            POX958UART   pUart   = &pThis->aUarts[iUart];
            POX958UARTCC pUartCC = &pThisCC->aUarts[iUart];
            rc = ox958UartRegRead(pDevIns, pThis, pUart, pUartCC, offUartReg, pv, cb);
        }
        else
            rc = VINF_IOM_MMIO_UNUSED_00;
    }

    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) ox958MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDEVOX958    pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVOX958);
    PDEVOX958CC  pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVOX958CC);
    VBOXSTRICTRC rc      = VINF_SUCCESS;
    RT_NOREF1(pvUser);

    if (off < OX958_REG_UART_REGION_OFFSET)
    {
        AssertReturn(cb == 4, VINF_SUCCESS);
        const uint32_t u32 = *(const uint32_t *)pv;

        switch ((uint32_t)off)
        {
            case OX958_REG_UART_IRQ_ENABLE:
                ASMAtomicOrU32(&pThis->u32RegIrqEnGlob, u32);
                ox958IrqUpdate(pDevIns, pThis);
                break;
            case OX958_REG_UART_IRQ_DISABLE:
                ASMAtomicAndU32(&pThis->u32RegIrqEnGlob, ~u32);
                ox958IrqUpdate(pDevIns, pThis);
                break;
            case OX958_REG_UART_WAKE_IRQ_ENABLE:
                ASMAtomicOrU32(&pThis->u32RegIrqEnWake, u32);
                break;
            case OX958_REG_UART_WAKE_IRQ_DISABLE:
                ASMAtomicAndU32(&pThis->u32RegIrqEnWake, ~u32);
                break;
            case OX958_REG_UART_IRQ_STS: /* Readonly */
            case OX958_REG_CC_REV_ID:    /* Readonly */
            case OX958_REG_UART_CNT:     /* Readonly */
            default:
                break;
        }
    }
    else
    {
        /* Figure out the UART accessed from the offset. */
        off -= OX958_REG_UART_REGION_OFFSET;
        uint32_t iUart      = (uint32_t)off / OX958_REG_UART_REGION_SIZE;
        uint32_t offUartReg = (uint32_t)off % OX958_REG_UART_REGION_SIZE;
        if (iUart < RT_MIN(pThis->cUarts, RT_ELEMENTS(pThis->aUarts)))
        {
            POX958UART   pUart   = &pThis->aUarts[iUart];
            POX958UARTCC pUartCC = &pThisCC->aUarts[iUart];
            rc = ox958UartRegWrite(pDevIns, pThis, pUart, pUartCC, offUartReg, pv, cb);
        }
    }

    return rc;
}


#ifdef IN_RING3

/** @interface_method_impl{PDMDEVREG,pfnDetach} */
static DECLCALLBACK(void) ox958R3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PDEVOX958   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVOX958);
    PDEVOX958CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVOX958CC);
    AssertReturnVoid(iLUN >= pThis->cUarts);

    RT_NOREF(fFlags);

    return uartR3Detach(pDevIns, &pThis->aUarts[iLUN].UartCore, &pThisCC->aUarts[iLUN].UartCore);
}


/** @interface_method_impl{PDMDEVREG,pfnAttach} */
static DECLCALLBACK(int) ox958R3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PDEVOX958   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVOX958);
    PDEVOX958CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVOX958CC);

    RT_NOREF(fFlags);

    if (iLUN >= RT_MIN(pThis->cUarts, RT_ELEMENTS(pThis->aUarts)))
        return VERR_PDM_LUN_NOT_FOUND;

    return uartR3Attach(pDevIns, &pThis->aUarts[iLUN].UartCore, &pThisCC->aUarts[iLUN].UartCore, iLUN);
}


/** @interface_method_impl{PDMDEVREG,pfnReset} */
static DECLCALLBACK(void) ox958R3Reset(PPDMDEVINS pDevIns)
{
    PDEVOX958   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVOX958);
    PDEVOX958CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVOX958CC);

    pThis->u32RegIrqStsGlob = 0x00;
    pThis->u32RegIrqEnGlob  = 0x00;
    pThis->u32RegIrqEnWake  = 0x00;

    uint32_t const cUarts = RT_MIN(pThis->cUarts, RT_ELEMENTS(pThis->aUarts));
    for (uint32_t i = 0; i < cUarts; i++)
        uartR3Reset(pDevIns, &pThis->aUarts[i].UartCore, &pThisCC->aUarts[i].UartCore);
}


/** @interface_method_impl{PDMDEVREG,pfnDestruct} */
static DECLCALLBACK(int) ox958R3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PDEVOX958 pThis = PDMDEVINS_2_DATA(pDevIns, PDEVOX958);

    uint32_t const cUarts = RT_MIN(pThis->cUarts, RT_ELEMENTS(pThis->aUarts));
    for (uint32_t i = 0; i < cUarts; i++)
        uartR3Destruct(pDevIns, &pThis->aUarts[i].UartCore);

    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMDEVREG,pfnConstruct} */
static DECLCALLBACK(int) ox958R3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    RT_NOREF(iInstance);
    PDEVOX958       pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVOX958);
    PDEVOX958R3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVOX958CC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    bool            fMsiXSupported = false;
    int             rc;

    /*
     * Init instance data.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "MsiXSupported|UartCount", "");

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "MsiXSupported", &fMsiXSupported, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("OXPCIe958 configuration error: failed to read \"MsiXSupported\" as boolean"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "UartCount", &pThis->cUarts, OX958_UARTS_MAX);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("OXPCIe958 configuration error: failed to read \"UartCount\" as unsigned 32bit integer"));

    if (!pThis->cUarts || pThis->cUarts > OX958_UARTS_MAX)
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("OXPCIe958 configuration error: \"UartCount\" has invalid value %u (must be in range [1 .. %u]"),
                                   pThis->cUarts, OX958_UARTS_MAX);

    /*
     * Fill PCI config space.
     */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,           OX958_PCI_VENDOR_ID);
    PDMPciDevSetDeviceId(pPciDev,           OX958_PCI_DEVICE_ID);
    PDMPciDevSetCommand(pPciDev,            0x0000);
# ifdef VBOX_WITH_MSI_DEVICES
    PDMPciDevSetStatus(pPciDev,             VBOX_PCI_STATUS_CAP_LIST);
    PDMPciDevSetCapabilityList(pPciDev,     OX958_PCI_MSI_CAP_OFS);
# else
    PDMPciDevSetCapabilityList(pPciDev,     0x70);
# endif
    PDMPciDevSetRevisionId(pPciDev,         0x00);
    PDMPciDevSetClassBase(pPciDev,          0x07); /* Communication controller. */
    PDMPciDevSetClassSub(pPciDev,           0x00); /* Serial controller. */
    PDMPciDevSetClassProg(pPciDev,          0x02); /* 16550. */

    PDMPciDevSetRevisionId(pPciDev,         0x00);
    PDMPciDevSetSubSystemVendorId(pPciDev,  OX958_PCI_VENDOR_ID);
    PDMPciDevSetSubSystemId(pPciDev,        OX958_PCI_DEVICE_ID);

    PDMPciDevSetInterruptLine(pPciDev,      0x00);
    PDMPciDevSetInterruptPin(pPciDev,       0x01);
    /** @todo More Capabilities. */

    /*
     * Register PCI device and I/O region.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
        return rc;

# ifdef VBOX_WITH_MSI_DEVICES
    PDMMSIREG MsiReg;
    RT_ZERO(MsiReg);
    MsiReg.cMsiVectors     = 1;
    MsiReg.iMsiCapOffset   = OX958_PCI_MSI_CAP_OFS;
    MsiReg.iMsiNextOffset  = OX958_PCI_MSIX_CAP_OFS;
    MsiReg.fMsi64bit       = true;
    if (fMsiXSupported)
    {
        MsiReg.cMsixVectors    = VBOX_MSIX_MAX_ENTRIES;
        MsiReg.iMsixCapOffset  = OX958_PCI_MSIX_CAP_OFS;
        MsiReg.iMsixNextOffset = 0x00;
        MsiReg.iMsixBar        = OX958_PCI_MSIX_BAR;
    }
    rc = PDMDevHlpPCIRegisterMsi(pDevIns, &MsiReg);
    if (RT_FAILURE(rc))
    {
        PDMPciDevSetCapabilityList(pPciDev, 0x0);
        /* That's OK, we can work without MSI */
    }
# endif

    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 0 /*iPciRegion*/, _16K, PCI_ADDRESS_SPACE_MEM,
                                        ox958MmioWrite, ox958MmioRead, NULL /*pvUser*/,
                                        IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                                        "OxPCIe958", &pThis->hMmio);
    AssertRCReturn(rc, rc);


    /*
     * Initialize the UARTs.
     */
    for (uint32_t i = 0; i < pThis->cUarts; i++)
    {
        POX958UART   pUart   = &pThis->aUarts[i];
        POX958UARTCC pUartCC = &pThisCC->aUarts[i];
        rc = uartR3Init(pDevIns, &pUart->UartCore, &pUartCC->UartCore, UARTTYPE_16550A, i, 0, ox958IrqReq);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("OXPCIe958 configuration error: failed to initialize UART %u"), i);
    }

    ox958R3Reset(pDevIns);
    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) ox958RZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVOX958   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVOX958);
    PDEVOX958CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVOX958CC);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, ox958MmioWrite, ox958MmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    uint32_t const cUarts = RT_MIN(pThis->cUarts, RT_ELEMENTS(pThis->aUarts));
    for (uint32_t i = 0; i < cUarts; i++)
    {
        POX958UARTCC pUartCC = &pThisCC->aUarts[i];
        rc = uartRZInit(&pUartCC->UartCore, ox958IrqReq);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */


const PDMDEVREG g_DeviceOxPcie958 =
{
    /* .u32version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "oxpcie958uart",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_SERIAL,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVOX958),
    /* .cbInstanceCC = */           sizeof(DEVOX958CC),
    /* .cbInstanceRC = */           sizeof(DEVOX958RC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        VBOX_MSIX_MAX_ENTRIES,
    /* .pszDescription = */         "OXPCIe958 based UART controller.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           ox958R3Construct,
    /* .pfnDestruct = */            ox958R3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               ox958R3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              ox958R3Attach,
    /* .pfnDetach = */              ox958R3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           ox958RZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           ox958RZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

