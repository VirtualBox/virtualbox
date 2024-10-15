/* $Id: DevPL061.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPL061 - ARM PL061 PrimeCell GPIO.
 *
 * The documentation for this device was taken from
 * https://developer.arm.com/documentation/ddi0190/b/programmer-s-model/summary-of-primecell-gpio-registers (2023-05-22).
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
#define LOG_GROUP LOG_GROUP_DEV_GPIO
#include <VBox/vmm/pdmdev.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The current serial code saved state version. */
#define PL061_SAVED_STATE_VERSION               1

/** PL061 MMIO region size in bytes. */
#define PL061_MMIO_SIZE                         _4K
/** PL061 number of GPIO pins. */
#define PL061_GPIO_NUM                          8

/** The offset of the GPIODATA (data) register from the beginning of the region. */
#define PL061_REG_GPIODATA_INDEX                0x0
/** The last offset of the GPIODATA (data) register from the beginning of the region. */
#define PL061_REG_GPIODATA_INDEX_END            0x3fc
/** The offset of the GPIODIR (data direction) register from the beginning of the region. */
#define PL061_REG_GPIODIR_INDEX                 0x400
/** The offset of the GPIOIS (interrupt sense) register from the beginning of the region. */
#define PL061_REG_GPIOIS_INDEX                  0x404
/** The offset of the GPIOIBE (interrupt both edges) register from the beginning of the region. */
#define PL061_REG_GPIOIBE_INDEX                 0x408
/** The offset of the GPIOIEV (interrupt event) register from the beginning of the region. */
#define PL061_REG_GPIOIEV_INDEX                 0x40c
/** The offset of the GPIOIE (interrupt mask) register from the beginning of the region. */
#define PL061_REG_GPIOIE_INDEX                  0x410
/** The offset of the GPIORIS (raw interrupt status) register from the beginning of the region. */
#define PL061_REG_GPIORIS_INDEX                 0x414
/** The offset of the GPIOMIS (masked interrupt status) register from the beginning of the region. */
#define PL061_REG_GPIOMIS_INDEX                 0x418
/** The offset of the GPIOIC (interrupt clear) register from the beginning of the region. */
#define PL061_REG_GPIOIC_INDEX                  0x41c
/** The offset of the GPIOAFSEL (mode control select) register from the beginning of the region. */
#define PL061_REG_GPIOAFSEL_INDEX               0x420

/** The offset of the GPIOPeriphID0 register from the beginning of the region. */
#define PL061_REG_GPIO_PERIPH_ID0_INDEX         0xfe0
/** The offset of the GPIOPeriphID1 register from the beginning of the region. */
#define PL061_REG_GPIO_PERIPH_ID1_INDEX         0xfe4
/** The offset of the GPIOPeriphID2 register from the beginning of the region. */
#define PL061_REG_GPIO_PERIPH_ID2_INDEX         0xfe8
/** The offset of the GPIOPeriphID3 register from the beginning of the region. */
#define PL061_REG_GPIO_PERIPH_ID3_INDEX         0xfec
/** The offset of the GPIOPCellID0 register from the beginning of the region. */
#define PL061_REG_GPIO_PCELL_ID0_INDEX          0xff0
/** The offset of the GPIOPCellID1 register from the beginning of the region. */
#define PL061_REG_GPIO_PCELL_ID1_INDEX          0xff4
/** The offset of the GPIOPCellID2 register from the beginning of the region. */
#define PL061_REG_GPIO_PCELL_ID2_INDEX          0xff8
/** The offset of the GPIOPCellID3 register from the beginning of the region. */
#define PL061_REG_GPIO_PCELL_ID3_INDEX          0xffc

/** Set the specified bits in the given register. */
#define PL061_REG_SET(a_Reg, a_Set)             ((a_Reg) |= (a_Set))
/** Clear the specified bits in the given register. */
#define PL061_REG_CLR(a_Reg, a_Clr)             ((a_Reg) &= ~(a_Clr))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Shared PL061 GPIO device state.
 */
typedef struct DEVPL061
{
    /** The MMIO handle. */
    IOMMMIOHANDLE                   hMmio;
    /** The base MMIO address the device is registered at. */
    RTGCPHYS                        GCPhysMmioBase;
    /** The IRQ value. */
    uint16_t                        u16Irq;

    /** @name Registers.
     * @{ */
    /** Data register. */
    uint8_t                         u8RegData;
    /** Direction register. */
    uint8_t                         u8RegDir;
    /** Interrupt sense register. */
    uint8_t                         u8RegIs;
    /** Interrupt both edges register. */
    uint8_t                         u8RegIbe;
    /** Interrupt event register. */
    uint8_t                         u8RegIev;
    /** Interrupt mask register. */
    uint8_t                         u8RegIe;
    /** Raw interrupt status register. */
    uint8_t                         u8RegRis;
    /** Mode control select register. */
    uint8_t                         u8RegAfsel;
    /** @} */
} DEVPL061;
/** Pointer to the shared serial device state. */
typedef DEVPL061 *PDEVPL061;


/**
 * PL061 device state for ring-3.
 */
typedef struct DEVPL061R3
{
    /** LUN\#0: The base interface. */
    PDMIBASE                        IBase;
    /** GPIO port interface. */
    PDMIGPIOPORT                    IGpioPort;
    /** Pointer to the attached base driver. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** Pointer to the attached GPIO connector interface. */
    R3PTRTYPE(PPDMIGPIOCONNECTOR)   pDrvGpio;
    /** Pointer to the device instance - only for getting our bearings in
     *  interface methods. */
    PPDMDEVINS                      pDevIns;
} DEVPL061R3;
/** Pointer to the PL061 device state for ring-3. */
typedef DEVPL061R3 *PDEVPL061R3;


/**
 * PL061 device state for ring-0.
 */
typedef struct DEVPL061R0
{
    /** Dummy. */
    uint8_t                         bDummy;
} DEVPL061R0;
/** Pointer to the PL061 device state for ring-0. */
typedef DEVPL061R0 *PDEVPL061R0;


/**
 * PL061 device state for raw-mode.
 */
typedef struct DEVPL061RC
{
    /** Dummy. */
    uint8_t                         bDummy;
} DEVPL061RC;
/** Pointer to the serial device state for raw-mode. */
typedef DEVPL061RC *PDEVPL061RC;

/** The PL061 device state for the current context. */
typedef CTX_SUFF(DEVPL061) DEVPL061CC;
/** Pointer to the PL016 device state for the current context. */
typedef CTX_SUFF(PDEVPL061) PDEVPL061CC;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/**
 * Updates the IRQ state based on the current device state.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared PL061 instance data.
 */
DECLINLINE(void) pl061IrqUpdate(PPDMDEVINS pDevIns, PDEVPL061 pThis)
{
    LogFlowFunc(("pThis=%#p u8RegRis=%#x u8RegIe=%#x -> %RTbool\n",
                 pThis, pThis->u8RegRis, pThis->u8RegIe,
                 RT_BOOL(pThis->u8RegRis & pThis->u8RegIe)));

    if (pThis->u8RegRis & pThis->u8RegIe)
        PDMDevHlpISASetIrqNoWait(pDevIns, pThis->u16Irq, 1);
    else
        PDMDevHlpISASetIrqNoWait(pDevIns, pThis->u16Irq, 0);
}


static void pl061InputUpdate(PPDMDEVINS pDevIns, PDEVPL061 pThis, uint8_t u8OldData)
{
    /* Edge interrupts. */
    uint8_t u8ChangedData = pThis->u8RegData ^ u8OldData;
    if (~pThis->u8RegIs & u8ChangedData)
    {
        /* Both edge interrupts can be treated easily. */
        pThis->u8RegRis |= u8ChangedData & pThis->u8RegIbe;

        /** @todo Single edge. */
    }

    /* Level interrupts. */
    pThis->u8RegRis |= (pThis->u8RegIs & pThis->u8RegData) & pThis->u8RegIev;
    pl061IrqUpdate(pDevIns, pThis);
}


/* -=-=-=-=-=- MMIO callbacks -=-=-=-=-=- */


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) pl061MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDEVPL061 pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);
    RT_NOREF(pvUser);
    Assert(cb == 4);
    Assert(!(off & (cb - 1))); RT_NOREF(cb);

    /*
     * From the spec:
     *     Similarly, the values read from this register are determined for each bit, by the mask bit derived from the
     *     address used to access the data register, PADDR[9:2]. Bits that are 1 in the address mask cause the corresponding
     *     bits in GPIODATA to be read, and bits that are 0 in the address mask cause the corresponding bits in GPIODATA
     *     to be read as 0, regardless of their value.
     */
    if (    off >= PL061_REG_GPIODATA_INDEX
        &&  off < PL061_REG_GPIODATA_INDEX_END + sizeof(uint32_t))
    {
        *(uint32_t *)pv = pThis->u8RegData & (uint8_t)(off >> 2);
        LogFlowFunc(("%RGp cb=%u u32=%RX32\n", off, cb, *(uint32_t *)pv));
        return VINF_SUCCESS;
    }

    uint32_t u32Val = 0;
    VBOXSTRICTRC rc = VINF_SUCCESS;
    switch (off)
    {
        case PL061_REG_GPIODIR_INDEX:
            u32Val = pThis->u8RegDir;
            break;
        case PL061_REG_GPIOIS_INDEX:
            u32Val = pThis->u8RegIs;
            break;
        case PL061_REG_GPIOIBE_INDEX:
            u32Val = pThis->u8RegIbe;
            break;
        case PL061_REG_GPIOIEV_INDEX:
            u32Val = pThis->u8RegIev;
            break;
        case PL061_REG_GPIOIE_INDEX:
            u32Val = pThis->u8RegIe;
            break;
        case PL061_REG_GPIORIS_INDEX:
            u32Val = pThis->u8RegRis;
            break;
        case PL061_REG_GPIOMIS_INDEX:
            u32Val = pThis->u8RegRis & pThis->u8RegIe;
            break;
        case PL061_REG_GPIOAFSEL_INDEX:
            u32Val = pThis->u8RegAfsel;
            break;
        case PL061_REG_GPIO_PERIPH_ID0_INDEX:
            u32Val = 0x61;
            break;
        case PL061_REG_GPIO_PERIPH_ID1_INDEX:
            u32Val = 0x10;
            break;
        case PL061_REG_GPIO_PERIPH_ID2_INDEX:
            u32Val = 0x04;
            break;
        case PL061_REG_GPIO_PERIPH_ID3_INDEX:
            u32Val = 0x00;
            break;
        case PL061_REG_GPIO_PCELL_ID0_INDEX:
            u32Val = 0x0d;
            break;
        case PL061_REG_GPIO_PCELL_ID1_INDEX:
            u32Val = 0xf0;
            break;
        case PL061_REG_GPIO_PCELL_ID2_INDEX:
            u32Val = 0x05;
            break;
        case PL061_REG_GPIO_PCELL_ID3_INDEX:
            u32Val = 0xb1;
            break;
        default:
            break;
    }

    if (rc == VINF_SUCCESS)
        *(uint32_t *)pv = u32Val;

    LogFlowFunc(("%RGp cb=%u u32=%RX32 -> %Rrc\n", off, cb, u32Val, rc));
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) pl061MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDEVPL061   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);
    LogFlowFunc(("cb=%u reg=%RGp val=%llx\n", cb, off, cb == 4 ? *(uint32_t *)pv : cb == 8 ? *(uint64_t *)pv : 0xdeadbeef));
    RT_NOREF(pvUser);
    Assert(cb == 4 || cb == 8); RT_NOREF(cb);
    Assert(!(off & (cb - 1)));

    /*
     * From the spec:
     *     In order to write to GPIODATA, the corresponding bits in the mask, resulting from the address bus, PADDR[9:2],
     *     must be HIGH. Otherwise the bit values remain unchanged by the write.
     */
    if (    off >= PL061_REG_GPIODATA_INDEX
        &&  off < PL061_REG_GPIODATA_INDEX_END + sizeof(uint32_t))
    {
        uint8_t uMask = (uint8_t)(off >> 2);
        uint8_t uNewValue = (*(const uint32_t *)pv & uMask) | (pThis->u8RegData & ~uMask);
        if (pThis->u8RegData ^ uNewValue)
        {
            /** @todo Reflect changes. */
        }

        pThis->u8RegData = uNewValue & pThis->u8RegDir; /* Filter out all pins configured as input. */
        return VINF_SUCCESS;
    }

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    uint8_t u8Val = (uint8_t)*(uint32_t *)pv;
    switch (off)
    {
        case PL061_REG_GPIODIR_INDEX:
            pThis->u8RegDir = u8Val;
            pl061IrqUpdate(pDevIns, pThis);
            break;
        case PL061_REG_GPIOIS_INDEX:
            pThis->u8RegIs = u8Val;
            pl061IrqUpdate(pDevIns, pThis);
            break;
        case PL061_REG_GPIOIBE_INDEX:
            pThis->u8RegIbe = u8Val;
            pl061IrqUpdate(pDevIns, pThis);
            break;
        case PL061_REG_GPIOIEV_INDEX:
            pThis->u8RegIev = u8Val;
            pl061IrqUpdate(pDevIns, pThis);
            break;
        case PL061_REG_GPIOIE_INDEX:
            pThis->u8RegIe = u8Val;
            pl061IrqUpdate(pDevIns, pThis);
            break;
        case PL061_REG_GPIOIC_INDEX:
            pThis->u8RegRis &= ~u8Val;
            pl061IrqUpdate(pDevIns, pThis);
            break;
        case PL061_REG_GPIOAFSEL_INDEX:
            pThis->u8RegAfsel = u8Val;
            break;
        default:
            break;

    }
    return rcStrict;
}


#ifdef IN_RING3

/* -=-=-=-=-=-=-=-=- PDMIBASE -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) pl061R3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PDEVPL061CC pThisCC = RT_FROM_MEMBER(pInterface, DEVPL061CC, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIGPIOPORT, &pThisCC->IGpioPort);
    return NULL;
}



/* -=-=-=-=-=-=-=-=- PDMIGPIOPORT -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIGPIOPORT,pfnGpioLineChange}
 */
static DECLCALLBACK(int) pl061R3GpioPort_GpioLineChange(PPDMIGPIOPORT pInterface, uint32_t idGpio, bool fVal)
{
    PDEVPL061CC pThisCC = RT_FROM_MEMBER(pInterface, DEVPL061CC, IGpioPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PDEVPL061   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);

    LogFlowFunc(("pInterface=%p idGpio=%u fVal=%RTbool\n", pInterface, idGpio, fVal));

    AssertReturn(idGpio < PL061_GPIO_NUM, VERR_INVALID_PARAMETER);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /* Only trigger an update on an actual change and if the GPIO line is configured as an input. */
    if (   RT_BOOL(pThis->u8RegData & RT_BIT(idGpio)) != fVal
        && !(RT_BIT(idGpio) & pThis->u8RegDir))
    {
        uint8_t u8OldData = pThis->u8RegData;

        if (fVal)
            PL061_REG_SET(pThis->u8RegData, RT_BIT(idGpio));
        else
            PL061_REG_CLR(pThis->u8RegData, RT_BIT(idGpio));

        pl061InputUpdate(pDevIns, pThis, u8OldData);
    }

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIGPIOPORT,pfnGpioLineIsInput}
 */
static DECLCALLBACK(bool) pl061R3GpioPort_GpioLineIsInput(PPDMIGPIOPORT pInterface, uint32_t idGpio)
{
    PDEVPL061CC pThisCC = RT_FROM_MEMBER(pInterface, DEVPL061CC, IGpioPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PDEVPL061   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);

    AssertReturn(idGpio < PL061_GPIO_NUM, VERR_INVALID_PARAMETER);

    return !RT_BOOL(pThis->u8RegDir & RT_BIT(idGpio)); /* Bit cleared means input. */
}



/* -=-=-=-=-=-=-=-=- Saved State -=-=-=-=-=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) pl061R3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PDEVPL061       pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    pHlp->pfnSSMPutU16(pSSM, pThis->u16Irq);
    pHlp->pfnSSMPutGCPhys(pSSM, pThis->GCPhysMmioBase);
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) pl061R3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPL061       pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    pHlp->pfnSSMPutU16(pSSM, pThis->u16Irq);
    pHlp->pfnSSMPutGCPhys(pSSM, pThis->GCPhysMmioBase);

    pHlp->pfnSSMPutU8(pSSM, pThis->u8RegData);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8RegDir);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8RegIs);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8RegIbe);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8RegIev);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8RegIe);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8RegRis);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8RegAfsel);

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) pl061R3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVPL061       pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    uint16_t        u16Irq;
    RTGCPHYS        GCPhysMmioBase;
    int rc;

    RT_NOREF(uVersion);

    pHlp->pfnSSMGetU16(   pSSM, &u16Irq);
    pHlp->pfnSSMGetGCPhys(pSSM, &GCPhysMmioBase);
    if (uPass == SSM_PASS_FINAL)
    {
        pHlp->pfnSSMGetU8(pSSM, &pThis->u8RegData);
        pHlp->pfnSSMGetU8(pSSM, &pThis->u8RegDir);
        pHlp->pfnSSMGetU8(pSSM, &pThis->u8RegIs);
        pHlp->pfnSSMGetU8(pSSM, &pThis->u8RegIbe);
        pHlp->pfnSSMGetU8(pSSM, &pThis->u8RegIev);
        pHlp->pfnSSMGetU8(pSSM, &pThis->u8RegIe);
        pHlp->pfnSSMGetU8(pSSM, &pThis->u8RegRis);
        rc = pHlp->pfnSSMGetU8(pSSM, &pThis->u8RegAfsel);
        AssertRCReturn(rc, rc);
    }

    if (uPass == SSM_PASS_FINAL)
    {
        /* The marker. */
        uint32_t u32;
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
        AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    /*
     * Check the config.
     */
    if (   pThis->u16Irq         != u16Irq
        || pThis->GCPhysMmioBase != GCPhysMmioBase)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved Irq=%#x GCPhysMmioBase=%#RGp; configured Irq=%#x GCPhysMmioBase=%#RGp"),
                                       u16Irq, GCPhysMmioBase, pThis->u16Irq, pThis->GCPhysMmioBase);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADDONE}
 */
static DECLCALLBACK(int) pl061R3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPL061   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);
    PDEVPL061CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL061CC);

    RT_NOREF(pThis, pThisCC, pSSM);
    return VINF_SUCCESS;
}


/* -=-=-=-=-=-=-=-=- PDMDEVREG -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) pl061R3Reset(PPDMDEVINS pDevIns)
{
    PDEVPL061   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);

    pThis->u8RegData  = 0;
    pThis->u8RegDir   = 0;
    pThis->u8RegIs    = 0;
    pThis->u8RegIbe   = 0;
    pThis->u8RegIev   = 0;
    pThis->u8RegIe    = 0;
    pThis->u8RegRis   = 0;
    pThis->u8RegAfsel = 0;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnAttach}
 */
static DECLCALLBACK(int) pl061R3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PDEVPL061CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL061CC);
    RT_NOREF(fFlags);
    AssertReturn(iLUN == 0, VERR_PDM_LUN_NOT_FOUND);

    int rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pThisCC->IBase, &pThisCC->pDrvBase, "PL016 Gpio");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvGpio = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIGPIOCONNECTOR);
        if (!pThisCC->pDrvGpio)
        {
            AssertLogRelMsgFailed(("PL061#%d: instance %d has no GPIO interface!\n", pDevIns->iInstance));
            return VERR_PDM_MISSING_INTERFACE;
        }
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        pThisCC->pDrvBase = NULL;
        LogRel(("PL061#%d: no unit\n", pDevIns->iInstance));
    }
    else /* Don't call VMSetError here as we assume that the driver already set an appropriate error */
        LogRel(("PL061#%d: Failed to attach to GPIO driver. rc=%Rrc\n", pDevIns->iInstance, rc));

   return rc;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDetach}
 */
static DECLCALLBACK(void) pl061R3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PDEVPL061CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL061CC);
    RT_NOREF(fFlags);
    AssertReturnVoid(iLUN == 0);

    /* Zero out important members. */
    pThisCC->pDrvBase   = NULL;
    pThisCC->pDrvGpio   = NULL;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) pl061R3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    /* Nothing to do (for now). */
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) pl061R3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPL061       pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);
    PDEVPL061CC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL061CC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    Assert(iInstance < 4);

    pThisCC->pDevIns                                = pDevIns;

    /* IBase */
    pThisCC->IBase.pfnQueryInterface                = pl061R3QueryInterface;
    /* IGpioPort */
    pThisCC->IGpioPort.pfnGpioLineChange            = pl061R3GpioPort_GpioLineChange;
    pThisCC->IGpioPort.pfnGpioLineIsInput           = pl061R3GpioPort_GpioLineIsInput;

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Irq|MmioBase", "");

    uint16_t u16Irq = 0;
    rc = pHlp->pfnCFGMQueryU16(pCfg, "Irq", &u16Irq);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"Irq\" value"));

    RTGCPHYS GCPhysMmioBase = 0;
    rc = pHlp->pfnCFGMQueryU64(pCfg, "MmioBase", &GCPhysMmioBase);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"IOBase\" value"));

    pThis->u16Irq         = u16Irq;
    pThis->GCPhysMmioBase = GCPhysMmioBase;

    /*
     * Register and map the MMIO region.
     */
    rc = PDMDevHlpMmioCreateAndMap(pDevIns, GCPhysMmioBase, PL061_MMIO_SIZE, pl061MmioWrite, pl061MmioRead,
                                   IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED, "PL061", &pThis->hMmio);
    AssertRCReturn(rc, rc);


    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, PL061_SAVED_STATE_VERSION, sizeof(*pThis), NULL,
                                NULL, pl061R3LiveExec, NULL,
                                NULL, pl061R3SaveExec, NULL,
                                NULL, pl061R3LoadExec, pl061R3LoadDone);
    AssertRCReturn(rc, rc);

    /*
     * Attach the GPIO driver and get the interfaces.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0 /*iLUN*/, &pThisCC->IBase, &pThisCC->pDrvBase, "GPIO");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvGpio = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIGPIOCONNECTOR);
        if (!pThisCC->pDrvGpio)
        {
            AssertLogRelMsgFailed(("Configuration error: instance %d has no GPIO interface!\n", iInstance));
            return VERR_PDM_MISSING_INTERFACE;
        }
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        pThisCC->pDrvBase   = NULL;
        pThisCC->pDrvGpio   = NULL;
        LogRel(("PL061#%d: no unit\n", iInstance));
    }
    else
    {
        AssertLogRelMsgFailed(("PL061#%d: Failed to attach to gpio driver. rc=%Rrc\n", iInstance, rc));
        /* Don't call VMSetError here as we assume that the driver already set an appropriate error */
        return rc;
    }

    pl061R3Reset(pDevIns);
    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) pl061RZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPL061   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL061);
    PDEVPL061CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL061CC);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, pl061MmioWrite, pl061MmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePl061Gpio =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "arm-pl061-gpio",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_GPIO,
    /* .cMaxInstances = */          UINT32_MAX,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVPL061),
    /* .cbInstanceCC = */           sizeof(DEVPL061CC),
    /* .cbInstanceRC = */           sizeof(DEVPL061RC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "ARM PL061 PrimeCell GPIO",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           pl061R3Construct,
    /* .pfnDestruct = */            pl061R3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               pl061R3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              pl061R3Attach,
    /* .pfnDetach = */              pl061R3Detach,
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
    /* .pfnConstruct = */           pl061RZConstruct,
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
    /* .pfnConstruct = */           pl061RZConstruct,
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

