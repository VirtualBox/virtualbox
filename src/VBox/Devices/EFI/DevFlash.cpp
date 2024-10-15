/* $Id: DevFlash.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevFlash - A simple Flash device
 *
 * A simple non-volatile byte-wide (x8) memory device modeled after Intel 28F008
 * FlashFile. See 28F008SA datasheet, Intel order number 290429-007.
 *
 * Implemented as an MMIO device attached directly to the CPU, not behind any
 * bus. Typically mapped as part of the firmware image.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_FLASH
#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/file.h>

#include "VBoxDD.h"
#include "FlashCore.h"



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The flash device, shared state.
 */
typedef struct DEVFLASH
{
    /** The flash core device instance.*/
    FLASHCORE           Core;
    /** The guest physical memory base address. */
    RTGCPHYS            GCPhysFlashBase;
    /** The handle to the MMIO region. */
    IOMMMIOHANDLE       hMmio;
} DEVFLASH;
/** Pointer to the Flash device state. */
typedef DEVFLASH *PDEVFLASH;

/**
 * The flash device, ring-3 state.
 */
typedef struct DEVFLASHR3
{
    /** The file conaining the flash content. */
    char               *pszFlashFile;
} DEVFLASHR3;
/** Pointer to the ring-3 Flash device state. */
typedef DEVFLASHR3 *PDEVFLASHR3;

/**
 * The flash device, ring-0 state.
 */
typedef struct DEVFLASHR0
{
    /** Dummy, no R0 only component so far. */
    uint8_t            bDummy;
} DEVFLASHR0;
/** Pointer to the ring-3 Flash device state. */
typedef DEVFLASHR0 *PDEVFLASHR0;

/** The serial device state for the current context. */
typedef CTX_SUFF(DEVFLASH) DEVFLASHCC;
/** Pointer to the serial device state for the current context. */
typedef CTX_SUFF(PDEVFLASH) PDEVFLASHCC;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE, Flash memory write}
 */
static DECLCALLBACK(VBOXSTRICTRC) flashMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDEVFLASH pThis = PDMDEVINS_2_DATA(pDevIns, PDEVFLASH);
    RT_NOREF1(pvUser);
    return flashWrite(&pThis->Core, off, pv, cb);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD, Flash memory read}
 */
static DECLCALLBACK(VBOXSTRICTRC) flashMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDEVFLASH pThis = PDMDEVINS_2_DATA(pDevIns, PDEVFLASH);
    RT_NOREF1(pvUser);
    return flashRead(&pThis->Core, off, pv, cb);
}

#ifdef IN_RING3

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) flashSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVFLASH pThis = PDMDEVINS_2_DATA(pDevIns, PDEVFLASH);
    return flashR3SaveExec(&pThis->Core, pDevIns, pSSM);
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) flashLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVFLASH pThis = PDMDEVINS_2_DATA(pDevIns, PDEVFLASH);
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    /* Fend off unsupported versions. */
    if (uVersion != FLASH_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    return flashR3LoadExec(&pThis->Core, pDevIns, pSSM);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) flashReset(PPDMDEVINS pDevIns)
{
    PDEVFLASH pThis = PDMDEVINS_2_DATA(pDevIns, PDEVFLASH);
    flashR3Reset(&pThis->Core);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) flashDestruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PDEVFLASH   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVFLASH);
    PDEVFLASHR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PDEVFLASHR3);

    if (pThisR3->pszFlashFile)
    {
        int rc = flashR3SaveToFile(&pThis->Core, pDevIns, pThisR3->pszFlashFile);
        if (RT_FAILURE(rc))
            LogRel(("Flash: Failed to save flash file: %Rrc\n", rc));

        PDMDevHlpMMHeapFree(pDevIns, pThisR3->pszFlashFile);
        pThisR3->pszFlashFile = NULL;
    }

    flashR3Destruct(&pThis->Core, pDevIns);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) flashConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVFLASH       pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVFLASH);
    PDEVFLASHR3     pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PDEVFLASHR3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    Assert(iInstance == 0); RT_NOREF1(iInstance);

    /*
     * Validate configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "DeviceId|BaseAddress|Size|BlockSize|FlashFile", "");

    /*
     * Read configuration.
     */

    /* The default device ID is Intel 28F800SA. */
    uint16_t u16FlashId = 0;
    int rc = pHlp->pfnCFGMQueryU16Def(pCfg, "DeviceId", &u16FlashId, 0xA289);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"DeviceId\" as an integer failed"));

    /* The default base address is 2MB below 4GB. */
    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "BaseAddress", &pThis->GCPhysFlashBase, 0xFFE00000);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"BaseAddress\" as an integer failed"));

    /* The default flash device size is 128K. */
    uint32_t cbFlash = 0;
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "Size", &cbFlash, 128 * _1K);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"Size\" as an integer failed"));

    /* The default flash device block size is 4K. */
    uint16_t cbBlock = 0;
    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "BlockSize", &cbBlock, _4K);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"BlockSize\" as an integer failed"));

    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "FlashFile", &pThisR3->pszFlashFile);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"FlashFile\" as a string failed"));

    /*
     * Initialize the flash core.
     */
    rc = flashR3Init(&pThis->Core, pDevIns, u16FlashId, cbFlash, cbBlock);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Flash: Failed to initialize core flash device"));

    /* Try to load the flash content from file. */
    rc = flashR3LoadFromFile(&pThis->Core, pDevIns, pThisR3->pszFlashFile);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Flash: Failed to load flash content from given file"));

    /*
     * Register MMIO region.
     */
    rc = PDMDevHlpMmioCreateExAndMap(pDevIns, pThis->GCPhysFlashBase, cbFlash,
                                     IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU, NULL, UINT32_MAX,
                                     flashMMIOWrite, flashMMIORead, NULL, NULL, "Flash Memory", &pThis->hMmio);
    AssertRCReturn(rc, rc);
    LogRel(("Registered %uKB flash at %RGp\n", pThis->Core.cbFlashSize / _1K, pThis->GCPhysFlashBase));

    /*
     * Register saved state.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, FLASH_SAVED_STATE_VERSION, sizeof(*pThis), flashSaveExec, flashLoadExec);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) flashRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVFLASH pThis = PDMDEVINS_2_DATA(pDevIns, PDEVFLASH);

# if 1
    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, flashMMIOWrite, flashMMIORead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
# else
    RT_NOREF(pDevIns, pThis); (void)&flashMMIOWrite; (void)&flashMMIORead;
# endif

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceFlash =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "flash",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_ARCH,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVFLASH),
    /* .cbInstanceCC = */           sizeof(DEVFLASHCC),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Flash Memory Device",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           flashConstruct,
    /* .pfnDestruct = */            flashDestruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               flashReset,
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
    /* .pfnConstruct = */           flashRZConstruct,
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
    /* .pfnConstruct = */           NULL,
    /* .pfnReserved0 = */           flashRZConstruct,
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

#endif /* VBOX_DEVICE_STRUCT_TESTCASE */
