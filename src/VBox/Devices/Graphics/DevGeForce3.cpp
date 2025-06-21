/* $Id$ */
/** @file
 * DevGeForce3 - NVIDIA GeForce3 Ti 500 device implementation for VirtualBox.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_VGA
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pgm.h>
#include <VBox/AssertGuest.h>
#include <VBox/log.h>
#include <VBox/err.h>

#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/mem.h>

#include "DevGeForce3.h"
#include <VBox/Graphics/GeForce3.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Saved state version. */
#define GEFORCE3_SAVED_STATE_VERSION       1

/** Log2 of the ring buffer size. */
#define GEFORCE3_FIFO_SIZE_LOG2             12


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Update the IRQ status.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared GeForce3 state.
 */
static void geforce3UpdateIrq(PPDMDEVINS pDevIns, PGEFORCE3STATE pThis)
{
    bool fAssert = false;
    
    if (pThis->fInterruptsEnabled)
    {
        /* Check various interrupt conditions here */
        if (pThis->aRegisters[GEFORCE3_REG_INTR / 4] & 0x1)
            fAssert = true;
    }

    LogFlow(("GeForce3: IRQ %s\n", fAssert ? "asserted" : "deasserted"));
    PDMDevHlpPCISetIrq(pDevIns, 0, fAssert ? 1 : 0);
}

/**
 * Process a command from the FIFO.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared GeForce3 state.
 * @param   pThisCC         The ring-3 GeForce3 state.
 * @param   pCmd            The command to process.
 */
static void geforce3ProcessCommand(PPDMDEVINS pDevIns, PGEFORCE3STATE pThis, PGEFORCE3STATER3 pThisCC, PGEFORCE3FIFOCMD pCmd)
{
    RT_NOREF(pDevIns, pThisCC);
    
    LogFlow(("GeForce3: Processing command 0x%08X\n", pCmd->uCommand));

    switch (pCmd->uCommand)
    {
        case GEFORCE3_CMD_NOP:
            /* No operation */
            break;

        case GEFORCE3_CMD_SET_MODE:
        {
            /* Set display mode */
            pThis->DisplayMode.uWidth = pCmd->uParam1;
            pThis->DisplayMode.uHeight = pCmd->uParam2;
            pThis->DisplayMode.uBpp = pCmd->uParam3 & 0xFF;
            pThis->DisplayMode.uPitch = (pThis->DisplayMode.uWidth * pThis->DisplayMode.uBpp + 7) / 8;
            
            LogRel(("GeForce3: Set mode %ux%u @ %u bpp\n", 
                    pThis->DisplayMode.uWidth, pThis->DisplayMode.uHeight, pThis->DisplayMode.uBpp));
            break;
        }

        case GEFORCE3_CMD_FILL_RECT:
        {
            /* Basic rectangle fill - implementation would go here */
            LogFlow(("GeForce3: Fill rect command\n"));
            break;
        }

        case GEFORCE3_CMD_BITBLT:
        {
            /* Basic BitBlt operation - implementation would go here */
            LogFlow(("GeForce3: BitBlt command\n"));
            break;
        }

        default:
            LogRel(("GeForce3: Unknown command 0x%08X\n", pCmd->uCommand));
            break;
    }
}

/**
 * Process pending FIFO commands.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared GeForce3 state.
 * @param   pThisCC         The ring-3 GeForce3 state.
 */
static void geforce3ProcessFifo(PPDMDEVINS pDevIns, PGEFORCE3STATE pThis, PGEFORCE3STATER3 pThisCC)
{
    uint32_t uFifoSize = RT_ELEMENTS(pThis->aFifo);
    
    while (pThis->uFifoRead != pThis->uFifoWrite)
    {
        PGEFORCE3FIFOCMD pCmd = &pThis->aFifo[pThis->uFifoRead];
        geforce3ProcessCommand(pDevIns, pThis, pThisCC, pCmd);
        
        pThis->uFifoRead = (pThis->uFifoRead + 1) % uFifoSize;
    }
}


/*********************************************************************************************************************************
*   MMIO Handlers                                                                                                                *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) geforce3MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PGEFORCE3STATE pThis = PDMDEVINS_2_DATA(pDevIns, PGEFORCE3STATE);
    RT_NOREF(pvUser);

    LogFlow(("GeForce3: MMIO read at offset 0x%RGp, size %u\n", off, cb));

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VINF_IOM_R3_MMIO_READ);
    if (rc == VINF_SUCCESS)
    {
        uint32_t *pu32 = (uint32_t *)pv;
        
        if (cb == 4 && (off & 3) == 0 && off < GEFORCE3_REGISTERS_SIZE)
        {
            uint32_t uRegIdx = (uint32_t)off / 4;
            *pu32 = pThis->aRegisters[uRegIdx];
            LogFlow(("GeForce3: Read register[0x%RGp] = 0x%08X\n", off, *pu32));
        }
        else
        {
            /* Handle other read sizes if needed */
            LogRel(("GeForce3: Unsupported MMIO read size %u at offset 0x%RGp\n", cb, off));
            memset(pv, 0xFF, cb);
        }

        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    }

    return rc;
}

/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) geforce3MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PGEFORCE3STATE    pThis   = PDMDEVINS_2_DATA(pDevIns, PGEFORCE3STATE);
    PGEFORCE3STATER3  pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PGEFORCE3STATER3);
    RT_NOREF(pvUser);

    LogFlow(("GeForce3: MMIO write at offset 0x%RGp, size %u\n", off, cb));

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VINF_IOM_R3_MMIO_WRITE);
    if (rc == VINF_SUCCESS)
    {
        const uint32_t *pu32 = (const uint32_t *)pv;
        
        if (cb == 4 && (off & 3) == 0 && off < GEFORCE3_REGISTERS_SIZE)
        {
            uint32_t uRegIdx = (uint32_t)off / 4;
            uint32_t uValue = *pu32;
            
            LogFlow(("GeForce3: Write register[0x%RGp] = 0x%08X\n", off, uValue));
            
            switch (off)
            {
                case GEFORCE3_REG_INTR_EN:
                    pThis->fInterruptsEnabled = (uValue & 1) != 0;
                    LogFlow(("GeForce3: Interrupts %s\n", pThis->fInterruptsEnabled ? "enabled" : "disabled"));
                    break;

                case GEFORCE3_REG_CONFIG:
                    pThis->fEnabled = (uValue & 1) != 0;
                    LogFlow(("GeForce3: Device %s\n", pThis->fEnabled ? "enabled" : "disabled"));
                    break;

                case GEFORCE3_REG_FIFO_PTR:
                    /* FIFO write - add command to FIFO */
                    if (pThis->fEnabled)
                    {
                        uint32_t uFifoSize = RT_ELEMENTS(pThis->aFifo);
                        uint32_t uNextWrite = (pThis->uFifoWrite + 1) % uFifoSize;
                        
                        if (uNextWrite != pThis->uFifoRead)
                        {
                            /* TODO: This is simplified - actual implementation would read command data */
                            pThis->aFifo[pThis->uFifoWrite].uCommand = uValue;
                            pThis->uFifoWrite = uNextWrite;
                            
                            /* Process FIFO commands */
                            geforce3ProcessFifo(pDevIns, pThis, pThisCC);
                        }
                        else
                        {
                            LogRel(("GeForce3: FIFO overflow\n"));
                        }
                    }
                    break;

                default:
                    pThis->aRegisters[uRegIdx] = uValue;
                    break;
            }
            
            geforce3UpdateIrq(pDevIns, pThis);
        }
        else
        {
            LogRel(("GeForce3: Unsupported MMIO write size %u at offset 0x%RGp\n", cb, off));
        }

        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    }

    return rc;
}


/*********************************************************************************************************************************
*   Saved State                                                                                                                  *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) geforce3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PGEFORCE3STATE   pThis = PDMDEVINS_2_DATA(pDevIns, PGEFORCE3STATE);
    PCPDMDEVHLPR3    pHlp  = pDevIns->pHlpR3;

    /* Save the PCI device state - simplified for now */
    RT_NOREF(pThis, pHlp, pSSM);

    /* Save device state */
    pHlp->pfnSSMPutU32(pSSM, pThis->cbVram);
    pHlp->pfnSSMPutBool(pSSM, pThis->fEnabled);
    pHlp->pfnSSMPutBool(pSSM, pThis->fInterruptsEnabled);

    /* Save device state */
    pHlp->pfnSSMPutU32(pSSM, pThis->cbVram);
    pHlp->pfnSSMPutBool(pSSM, pThis->fEnabled);
    pHlp->pfnSSMPutBool(pSSM, pThis->fInterruptsEnabled);
    
    /* Save registers */
    pHlp->pfnSSMPutMem(pSSM, pThis->aRegisters, sizeof(pThis->aRegisters));
    
    /* Save FIFO state */
    pHlp->pfnSSMPutU32(pSSM, pThis->uFifoRead);
    pHlp->pfnSSMPutU32(pSSM, pThis->uFifoWrite);
    pHlp->pfnSSMPutMem(pSSM, pThis->aFifo, sizeof(pThis->aFifo));
    
    /* Save display mode */
    pHlp->pfnSSMPutU32(pSSM, pThis->DisplayMode.uWidth);
    pHlp->pfnSSMPutU32(pSSM, pThis->DisplayMode.uHeight);
    pHlp->pfnSSMPutU32(pSSM, pThis->DisplayMode.uBpp);
    pHlp->pfnSSMPutU32(pSSM, pThis->DisplayMode.uPitch);
    pHlp->pfnSSMPutU32(pSSM, pThis->DisplayMode.uStartAddr);

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) geforce3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PGEFORCE3STATE   pThis = PDMDEVINS_2_DATA(pDevIns, PGEFORCE3STATE);
    PCPDMDEVHLPR3    pHlp  = pDevIns->pHlpR3;

    if (uVersion != GEFORCE3_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    /* Load PCI device state - simplified for now */
    RT_NOREF(pThis);
    int rc = VINF_SUCCESS;

    /* Load device state */
    pHlp->pfnSSMGetU32(pSSM, &pThis->cbVram);
    pHlp->pfnSSMGetBool(pSSM, &pThis->fEnabled);
    pHlp->pfnSSMGetBool(pSSM, &pThis->fInterruptsEnabled);

    /* Load device state */
    pHlp->pfnSSMGetU32(pSSM, &pThis->cbVram);
    pHlp->pfnSSMGetBool(pSSM, &pThis->fEnabled);
    pHlp->pfnSSMGetBool(pSSM, &pThis->fInterruptsEnabled);
    
    /* Load registers */
    pHlp->pfnSSMGetMem(pSSM, pThis->aRegisters, sizeof(pThis->aRegisters));
    
    /* Load FIFO state */
    pHlp->pfnSSMGetU32(pSSM, &pThis->uFifoRead);
    pHlp->pfnSSMGetU32(pSSM, &pThis->uFifoWrite);
    pHlp->pfnSSMGetMem(pSSM, pThis->aFifo, sizeof(pThis->aFifo));
    
    /* Load display mode */
    pHlp->pfnSSMGetU32(pSSM, &pThis->DisplayMode.uWidth);
    pHlp->pfnSSMGetU32(pSSM, &pThis->DisplayMode.uHeight);
    pHlp->pfnSSMGetU32(pSSM, &pThis->DisplayMode.uBpp);
    pHlp->pfnSSMGetU32(pSSM, &pThis->DisplayMode.uPitch);
    pHlp->pfnSSMGetU32(pSSM, &pThis->DisplayMode.uStartAddr);

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Device Interface                                                                                                             *
*********************************************************************************************************************************/

#ifdef IN_RING3

/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) geforce3R3Reset(PPDMDEVINS pDevIns)
{
    PGEFORCE3STATE pThis = PDMDEVINS_2_DATA(pDevIns, PGEFORCE3STATE);

    LogFlow(("GeForce3: Reset\n"));

    /* Reset device state */
    pThis->fEnabled = false;
    pThis->fInterruptsEnabled = false;
    pThis->uFifoRead = 0;
    pThis->uFifoWrite = 0;
    
    /* Clear registers */
    RT_ZERO(pThis->aRegisters);
    RT_ZERO(pThis->aFifo);
    
    /* Reset display mode */
    pThis->DisplayMode.uWidth = 640;
    pThis->DisplayMode.uHeight = 480;
    pThis->DisplayMode.uBpp = 32;
    pThis->DisplayMode.uPitch = 640 * 4;
    pThis->DisplayMode.uStartAddr = 0;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) geforce3R3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PGEFORCE3STATE pThis = PDMDEVINS_2_DATA(pDevIns, PGEFORCE3STATE);

    LogFlow(("GeForce3: Destruct\n"));

    /*
     * Clean up the critical section.
     */
    if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->CritSect))
        PDMDevHlpCritSectDelete(pDevIns, &pThis->CritSect);

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) geforce3R3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PGEFORCE3STATE    pThis   = PDMDEVINS_2_DATA(pDevIns, PGEFORCE3STATE);
    PGEFORCE3STATER3  pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PGEFORCE3STATER3);
    PCPDMDEVHLPR3     pHlp    = pDevIns->pHlpR3;
    int               rc;

    Assert(iInstance == 0); RT_NOREF(iInstance);
    
    /*
     * Initialize the instance data.
     */
    pThis->pDevIns = pDevIns;
    pThisCC->pDevIns = pDevIns;

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "VRamSize", "");

    uint32_t cbVram;
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "VRamSize", &cbVram, GEFORCE3_VRAM_SIZE_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"VRamSize\" value"));
    
    if (cbVram < GEFORCE3_VRAM_SIZE_MIN || cbVram > GEFORCE3_VRAM_SIZE_MAX)
        return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER, 
                                N_("Configuration error: \"VRamSize\" is out of range"));

    pThis->cbVram = cbVram;
    LogRel(("GeForce3: VRAM size: %u MB\n", cbVram / _1M));

    /*
     * Initialize the critical section.
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "GeForce3");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("GeForce3 cannot initialize critical section"));

    /*
     * Register the PCI device.
     */
    PDMPCIDEV_ASSERT_VALID(pDevIns, &pThis->PciDev);
    rc = PDMDevHlpPCIRegister(pDevIns, &pThis->PciDev);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Set up the PCI configuration space.
     */
    PDMPciDevSetVendorId(&pThis->PciDev,   GEFORCE3_VENDOR_ID);
    PDMPciDevSetDeviceId(&pThis->PciDev,   GEFORCE3_DEVICE_ID);
    PDMPciDevSetRevisionId(&pThis->PciDev, 0x10);
    PDMPciDevSetClassProg(&pThis->PciDev,  0x00);
    PDMPciDevSetClassSub(&pThis->PciDev,   0x00); /* VGA compatible controller */
    PDMPciDevSetClassBase(&pThis->PciDev,  0x03); /* Display controller */
    PDMPciDevSetHeaderType(&pThis->PciDev, 0x00); /* Single-function device */
    PDMPciDevSetInterruptLine(&pThis->PciDev, 0x00);
    PDMPciDevSetInterruptPin(&pThis->PciDev,  0x01);

    /*
     * Register MMIO regions.
     */
    
    /* BAR0: Registers */
    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, GEFORCE3_PCI_BAR_REGISTERS, GEFORCE3_REGISTERS_SIZE,
                                        PCI_ADDRESS_SPACE_MEM, geforce3MmioWrite, geforce3MmioRead, NULL /*pvUser*/,
                                        IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED,
                                        "GeForce3 Registers", &pThis->hMmioRegisters);
    AssertRCReturn(rc, rc);

    /* BAR1: Frame buffer (VRAM) */
    rc = PDMDevHlpPCIIORegionCreateMmio2(pDevIns, GEFORCE3_PCI_BAR_FRAMEBUFFER, pThis->cbVram,
                                         (PCIADDRESSSPACE)(PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_BAR64), "GeForce3 VRAM",
                                         (void **)&pThisCC->pbVram, &pThis->hMmio2Vram);
    AssertRCReturn(rc, rc);

    /*
     * Saved state registration.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, GEFORCE3_SAVED_STATE_VERSION, sizeof(GEFORCE3STATE), geforce3SaveExec, geforce3LoadExec);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Reset the device state.
     */
    geforce3R3Reset(pDevIns);

    LogRel(("GeForce3: Device initialized successfully\n"));
    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) geforce3RZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PGEFORCE3STATE pThis = PDMDEVINS_2_DATA(pDevIns, PGEFORCE3STATE);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, &pThis->CritSect);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmioRegisters, geforce3MmioWrite, geforce3MmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */


/*********************************************************************************************************************************
*   Device Registration                                                                                                          *
*********************************************************************************************************************************/

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceGeForce3 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "geforce3",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_GRAPHICS,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(GEFORCE3STATE),
    /* .cbInstanceCC = */           sizeof(GEFORCE3STATER3),
    /* .cbInstanceRC = */           sizeof(GEFORCE3STATER0),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "NVIDIA GeForce3 Ti 500 graphics adapter",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           geforce3R3Construct,
    /* .pfnDestruct = */            geforce3R3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               geforce3R3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
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
    /* .pfnConstruct = */           geforce3RZConstruct,
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
    /* .pfnConstruct = */           geforce3RZConstruct,
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

#ifdef VBOX_DEVICE_STRUCT_TESTCASE

typedef struct GEFORCE3TESTSTRUCT
{
    GEFORCE3STATE DeviceState;
} GEFORCE3TESTSTRUCT;

#endif /* VBOX_DEVICE_STRUCT_TESTCASE */