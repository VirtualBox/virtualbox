/* $Id: DevPlayground.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPlayground - Device for making PDM/PCI/... experiments.
 *
 * This device uses big PCI BAR64 resources, which needs the ICH9 chipset.
 * The device works without any PCI config (because the default setup with the
 * ICH9 chipset doesn't have anything at bus=0, device=0, function=0.
 *
 * To enable this device for a particular VM:
 * VBoxManage setextradata vmname VBoxInternal/PDM/Devices/playground/Path .../obj/VBoxPlaygroundDevice/VBoxPlaygroundDevice
 * VBoxManage setextradata vmname VBoxInternal/Devices/playground/0/Config/Whatever1 0
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MISC
#include <VBox/vmm/pdmdev.h>
#include <VBox/version.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <VBox/com/assert.h>
#include <VBox/com/defs.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/assert.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Playground device per function (sub-device) data.
 */
typedef struct VBOXPLAYGROUNDDEVICEFUNCTION
{
    /** The function number. */
    uint8_t         iFun;
    /** Device function name. */
    char            szName[31];
    /** MMIO region \#0 name. */
    char            szMmio0[32];
    /** MMIO region \#2 name. */
    char            szMmio2[32];
    /** The MMIO region \#0 handle. */
    IOMMMIOHANDLE   hMmio0;
    /** The MMIO region \#2 handle. */
    IOMMMIOHANDLE   hMmio2;
    /** Backing storage. */
    uint8_t         abBacking[4096];
} VBOXPLAYGROUNDDEVICEFUNCTION;
/** Pointer to a PCI function of the playground device. */
typedef VBOXPLAYGROUNDDEVICEFUNCTION *PVBOXPLAYGROUNDDEVICEFUNCTION;

/**
 * Playground device instance data.
 */
typedef struct VBOXPLAYGROUNDDEVICE
{
    /** PCI device functions. */
    VBOXPLAYGROUNDDEVICEFUNCTION aPciFuns[8];
} VBOXPLAYGROUNDDEVICE;
/** Pointer to the instance data of a playground device instance. */
typedef VBOXPLAYGROUNDDEVICE *PVBOXPLAYGROUNDDEVICE;


#define PLAYGROUND_SSM_VERSION 3


/*********************************************************************************************************************************
*   Device Functions                                                                                                             *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) devPlaygroundMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PVBOXPLAYGROUNDDEVICEFUNCTION pFun = (PVBOXPLAYGROUNDDEVICEFUNCTION)pvUser;
    NOREF(pDevIns);

#ifdef LOG_ENABLED
    unsigned const cbLog  = cb;
    RTGCPHYS       offLog = off;
#endif
    uint8_t *pbDst = (uint8_t *)pv;
    while (cb-- > 0)
    {
        *pbDst = pFun->abBacking[off % RT_ELEMENTS(pFun->abBacking)];
        pbDst++;
        off++;
    }

    Log(("DevPlayGr/[%u]: READ  off=%RGv cb=%u: %.*Rhxs\n", pFun->iFun, offLog, cbLog, cbLog, pv));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) devPlaygroundMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PVBOXPLAYGROUNDDEVICEFUNCTION pFun = (PVBOXPLAYGROUNDDEVICEFUNCTION)pvUser;
    NOREF(pDevIns);
    Log(("DevPlayGr/[%u]: WRITE off=%RGv cb=%u: %.*Rhxs\n", pFun->iFun, off, cb, cb, pv));

    uint8_t const *pbSrc = (uint8_t const *)pv;
    while (cb-- > 0)
    {
        pFun->abBacking[off % RT_ELEMENTS(pFun->abBacking)] = *pbSrc;
        pbSrc++;
        off++;
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) devPlaygroundSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVBOXPLAYGROUNDDEVICE pThis = PDMDEVINS_2_DATA(pDevIns, PVBOXPLAYGROUNDDEVICE);
    PCPDMDEVHLPR3         pHlp  = pDevIns->pHlpR3;

    /* dummy (real devices would need to save their state here) */
    RT_NOREF(pThis);

    /* Demo of some API stuff - very unusual, think twice if there's no better
     * solution which doesn't need API interaction. */
#if 0
    try
    {
        HRESULT hrc = S_OK;
        com::Bstr bstrSnapName;
        com::Guid uuid(COM_IIDOF(ISnapshot));
        ISnapshot *pSnap = (ISnapshot *)PDMDevHlpQueryGenericUserObject(pDevIns, uuid.raw());
        if (pSnap)
        {
            hrc = pSnap->COMGETTER(Name)(bstrSnapName.asOutParam());
            AssertComRCReturn(hrc, VERR_INVALID_STATE);
        }
        com::Utf8Str strSnapName(bstrSnapName);
        pHlp->pfnSSMPutStrZ(pSSM, strSnapName.c_str());
        LogRel(("Playground: saving state of snapshot '%s', hrc=%Rhrc\n", strSnapName.c_str(), hrc));
    }
    catch (...)
    {
        AssertLogRelFailed();
        return VERR_UNEXPECTED_EXCEPTION;
    }
#else
    pHlp->pfnSSMPutStrZ(pSSM, "playground");
#endif

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) devPlaygroundLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PVBOXPLAYGROUNDDEVICE pThis = PDMDEVINS_2_DATA(pDevIns, PVBOXPLAYGROUNDDEVICE);
    PCPDMDEVHLPR3         pHlp  = pDevIns->pHlpR3;

    if (uVersion > PLAYGROUND_SSM_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    /* dummy (real devices would need to load their state here) */
    RT_NOREF(pThis);

    /* Reading the stuff written to saved state, just a demo. */
    char szSnapName[256];
    int rc = pHlp->pfnSSMGetStrZ(pSSM, szSnapName, sizeof(szSnapName));
    AssertRCReturn(rc, rc);
    LogRel(("Playground: loading state of snapshot '%s'\n", szSnapName));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) devPlaygroundDestruct(PPDMDEVINS pDevIns)
{
    /*
     * Check the versions here as well since the destructor is *always* called.
     * THIS IS ALWAYS THE FIRST STATEMENT IN A DESTRUCTOR!
     */
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) devPlaygroundConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    /*
     * Check that the device instance and device helper structures are compatible.
     * THIS IS ALWAYS THE FIRST STATEMENT IN A CONSTRUCTOR!
     */
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns); /* This must come first. */
    Assert(iInstance == 0); RT_NOREF(iInstance);

    /*
     * Initialize the instance data so that the destructor won't mess up.
     */
    PVBOXPLAYGROUNDDEVICE pThis = PDMDEVINS_2_DATA(pDevIns, PVBOXPLAYGROUNDDEVICE);

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Whatever1|NumFunctions|BigBAR0MB|BigBAR0GB|BigBAR2MB|BigBAR2GB", "");

    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    uint8_t uNumFunctions;
    AssertCompile(RT_ELEMENTS(pThis->aPciFuns) <= RT_ELEMENTS(pDevIns->apPciDevs));
    int rc = pHlp->pfnCFGMQueryU8Def(pCfg, "NumFunctions", &uNumFunctions, RT_ELEMENTS(pThis->aPciFuns));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query integer value \"NumFunctions\""));
    if ((uNumFunctions < 1) || (uNumFunctions > RT_ELEMENTS(pThis->aPciFuns)))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Invalid \"NumFunctions\" value (must be between 1 and 8)"));

    RTGCPHYS cbFirstBAR;
    uint16_t uBigBAR0GB;
    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "BigBAR0GB", &uBigBAR0GB, 0);  /* Default to nothing. */
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query integer value \"BigBAR0GB\""));
    if (uBigBAR0GB > 512)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Invalid \"BigBAR0GB\" value (must be 512 or less)"));

    if (uBigBAR0GB)
        cbFirstBAR = uBigBAR0GB * _1G64;
    else
    {
        uint16_t uBigBAR0MB;
        rc = pHlp->pfnCFGMQueryU16Def(pCfg, "BigBAR0MB", &uBigBAR0MB, 8);  /* 8 MB default. */
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query integer value \"BigBAR0MB\""));
        if (uBigBAR0MB < 1 || uBigBAR0MB > 4095)
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Invalid \"BigBAR0MB\" value (must be between 1 and 4095)"));
        cbFirstBAR = uBigBAR0MB * _1M;
    }

    RTGCPHYS cbSecondBAR;
    uint16_t uBigBAR2GB;
    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "BigBAR2GB", &uBigBAR2GB, 0);  /* Default to nothing. */
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query integer value \"BigBAR2GB\""));
    if (uBigBAR2GB > 512)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Invalid \"BigBAR2GB\" value (must be 512 or less)"));

    if (uBigBAR2GB)
        cbSecondBAR = uBigBAR2GB * _1G64;
    else
    {
        uint16_t uBigBAR2MB;
        rc = pHlp->pfnCFGMQueryU16Def(pCfg, "BigBAR2MB", &uBigBAR2MB, 16); /* 16 MB default. */
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query integer value \"BigBAR2MB\""));
        if (uBigBAR2MB < 1 || uBigBAR2MB > 4095)
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Invalid \"BigBAR2MB\" value (must be between 1 and 4095)"));
        cbSecondBAR = uBigBAR2MB * _1M;
    }


    /*
     * PCI device setup.
     */
    uint32_t iPciDevNo = PDMPCIDEVREG_DEV_NO_FIRST_UNUSED;
    for (uint32_t iPciFun = 0; iPciFun < uNumFunctions; iPciFun++)
    {
        PPDMPCIDEV                    pPciDev = pDevIns->apPciDevs[iPciFun];
        PVBOXPLAYGROUNDDEVICEFUNCTION pFun    = &pThis->aPciFuns[iPciFun];
        RTStrPrintf(pFun->szName, sizeof(pFun->szName), "playground%u", iPciFun);
        pFun->iFun = iPciFun;

        PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

        PDMPciDevSetVendorId(pPciDev,       0x80ee);
        PDMPciDevSetDeviceId(pPciDev,       0xde4e);
        PDMPciDevSetClassBase(pPciDev,      0x07);  /* communications device */
        PDMPciDevSetClassSub(pPciDev,       0x80);  /* other communications device */
        if (iPciFun == 0) /* only for the primary function */
            PDMPciDevSetHeaderType(pPciDev, 0x80);  /* normal, multifunction device */

        rc = PDMDevHlpPCIRegisterEx(pDevIns, pPciDev, 0 /*fFlags*/, iPciDevNo, iPciFun, pThis->aPciFuns[iPciFun].szName);
        AssertLogRelRCReturn(rc, rc);

        /* First region. */
        RTGCPHYS const cbFirst = iPciFun == 0 ? cbFirstBAR : iPciFun * _4K;
        RTStrPrintf(pFun->szMmio0, sizeof(pFun->szMmio0), "PG-F%d-BAR0", iPciFun);
        rc = PDMDevHlpMmioCreate(pDevIns, cbFirst, pPciDev, 0 /*iPciRegion*/,
                                 devPlaygroundMMIOWrite, devPlaygroundMMIORead, pFun,
                                 IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU, pFun->szMmio0, &pFun->hMmio0);
        AssertLogRelRCReturn(rc, rc);

        rc = PDMDevHlpPCIIORegionRegisterMmioEx(pDevIns, pPciDev, 0, cbFirst,
                                                (PCIADDRESSSPACE)(  PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_BAR64
                                                                  | (iPciFun == 0 ? PCI_ADDRESS_SPACE_MEM_PREFETCH : 0)),
                                                pFun->hMmio0, NULL);
        AssertLogRelRCReturn(rc, rc);

        /* Second region. */
        RTGCPHYS const cbSecond = iPciFun == 0  ? cbSecondBAR : iPciFun * _32K;
        RTStrPrintf(pFun->szMmio2, sizeof(pFun->szMmio2), "PG-F%d-BAR2", iPciFun);
        rc = PDMDevHlpMmioCreate(pDevIns, cbSecond, pPciDev, 2 << 16 /*iPciRegion*/,
                                 devPlaygroundMMIOWrite, devPlaygroundMMIORead, pFun,
                                 IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU, pFun->szMmio2, &pFun->hMmio2);
        AssertLogRelRCReturn(rc, rc);

        rc = PDMDevHlpPCIIORegionRegisterMmioEx(pDevIns, pPciDev, 2, cbSecond,
                                                (PCIADDRESSSPACE)(  PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_BAR64
                                                                  | (iPciFun == 0 ? PCI_ADDRESS_SPACE_MEM_PREFETCH : 0)),
                                                pFun->hMmio2, NULL);
        AssertLogRelRCReturn(rc, rc);

        /* Subsequent function should use the same major as the previous one. */
        iPciDevNo = PDMPCIDEVREG_DEV_NO_SAME_AS_PREV;
    }

    /*
     * Save state handling.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, PLAYGROUND_SSM_VERSION, sizeof(*pThis), devPlaygroundSaveExec, devPlaygroundLoadExec);
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}


/**
 * The device registration structure.
 */
static const PDMDEVREG g_DevicePlayground =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "playground",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_MISC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(VBOXPLAYGROUNDDEVICE),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         8,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "VBox Playground Device.",
#if defined(IN_RING3)
    /* .pszRCMod = */               "",
    /* .pszR0Mod = */               "",
    /* .pfnConstruct = */           devPlaygroundConstruct,
    /* .pfnDestruct = */            devPlaygroundDestruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               NULL,
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
    /* .pfnConstruct = */           NULL,
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


/**
 * Register devices provided by the plugin.
 *
 * @returns VBox status code.
 * @param   pCallbacks      Pointer to the callback table.
 * @param   u32Version      VBox version number.
 */
extern "C" DECLEXPORT(int) VBoxDevicesRegister(PPDMDEVREGCB pCallbacks, uint32_t u32Version)
{
    LogFlow(("VBoxPlaygroundDevice::VBoxDevicesRegister: u32Version=%#x pCallbacks->u32Version=%#x\n", u32Version, pCallbacks->u32Version));

    AssertLogRelMsgReturn(u32Version >= VBOX_VERSION,
                          ("VirtualBox version %#x, expected %#x or higher\n", u32Version, VBOX_VERSION),
                          VERR_VERSION_MISMATCH);
    AssertLogRelMsgReturn(pCallbacks->u32Version == PDM_DEVREG_CB_VERSION,
                          ("callback version %#x, expected %#x\n", pCallbacks->u32Version, PDM_DEVREG_CB_VERSION),
                          VERR_VERSION_MISMATCH);

    return pCallbacks->pfnRegister(pCallbacks, &g_DevicePlayground);
}

