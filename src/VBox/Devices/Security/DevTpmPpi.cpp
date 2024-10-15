/* $Id: DevTpmPpi.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevTpmPpi - Guest platform <-> VirtualBox TPM Physical Presence Interface Integration Framework.
 *
 * Based on: https://github.com/qemu/qemu/blob/master/docs/specs/tpm.rst (2024-06-26).
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
#define LOG_GROUP LOG_GROUP_DEV_TPM

#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The TPM saved state version. */
#define TPM_PPI_SAVED_STATE_VERSION                     1

/** The TPM PPI MMIO base default (compatible with qemu). */
#define TPM_PPI_MMIO_BASE_DEFAULT                       UINT64_C(0xfed45000)
/** The size of the TPM PPI MMIO area. */
#define TPM_PPI_MMIO_SIZE                               _4K

/** @name QEMU compatible PPI interface layout.
 * @{ */
/**
 * The PPI structure layout (yes, it really is misaligned).
 */
#pragma pack(1)
typedef union QEMUTPMPPI
{
    /** The byte view. */
    uint8_t         ab[0x400];
    /** The structured view. */
    struct
    {
        /** Supported operation flags set by the firmware for each operation. */
        uint8_t     abFunc[0x100];
        /** SMI interrupt to use, set by firmware. Not supported. */
        uint8_t     bPpin;
        /** ACPI function index to pass to SMM code, set by ACPI. Not supported. */
        uint32_t    u32Ppip;
        /** Result of last executed operation, set by firmware. */
        uint32_t    u32Pprp;
        /** Operation request number to execute, set by ACPI. */
        uint32_t    u32Pprq;
        /** Operation request optional parameter, set by ACPI. */
        uint32_t    u32Pprm;
        /** Last executed operation request number, copied from QEMUTPMPPI::u32PPrq field by firmware. */
        uint32_t    u32Lppr;
        /** Result code from SMM function, not supported. */
        uint32_t    u32Fret;
        /** Reserved for future use. */
        uint8_t     abRsvd[0x40];
        /** Operation to execute after reboot by firmware, used by firmware. */
        uint8_t     bNextStep;
        /** Memory overwrite variable. */
        uint8_t     bMovv;
    } s;
} QEMUTPMPPI;
#pragma pack()
AssertCompileSize(QEMUTPMPPI, 0x400);
/** Pointer to the QEMU PPI structure layout. */
typedef QEMUTPMPPI *PQEMUTPMPPI;
/** Pointer to a const QEMU PPI structure layout. */
typedef const QEMUTPMPPI *PCQEMUTPMPPI;
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Shared TPM PPI device state.
 */
typedef struct DEVTPMPPI
{
    /** Base MMIO address of the TPM PPI area. */
    RTGCPHYS                        GCPhysMmio;
    /** The handle of the MMIO region. */
    IOMMMIOHANDLE                   hMmio;
    /** The QEMU PPI state. */
    QEMUTPMPPI                      Ppi;
} DEVTPMPPI;
/** Pointer to the shared TPM PPI device state. */
typedef DEVTPMPPI *PDEVTPMPPI;

/**
 * TPM PPI device state for ring-3.
 */
typedef struct DEVTPMPPIR3
{
    /** Pointer to the device instance. */
    PPDMDEVINS                      pDevIns;
} DEVTPMPPIR3;
/** Pointer to the TPM device state for ring-3. */
typedef DEVTPMPPIR3 *PDEVTPMPPIR3;


/**
 * TPM PPI device state for ring-0.
 */
typedef struct DEVTPMPPIR0
{
    uint32_t                        u32Dummy;

} DEVTPMPPIR0;
/** Pointer to the TPM device state for ring-0. */
typedef DEVTPMPPIR0 *PDEVTPMPPIR0;


/**
 * TPM PPI device state for raw-mode.
 */
typedef struct DEVTPMPPIRC
{
    uint32_t                        u32Dummy;
} DEVTPMPPIRC;
/** Pointer to the TPM device state for raw-mode. */
typedef DEVTPMPPIRC *PDEVTPMPPIRC;

/** The TPM PI device state for the current context. */
typedef CTX_SUFF(DEVTPMPPI) DEVTPMPPICC;
/** Pointer to the TPM PPI device state for the current context. */
typedef CTX_SUFF(PDEVTPMPPI) PDEVTPMPPICC;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

#ifdef IN_RING3
/**
 * SSM descriptor table for the TPM PPI structure.
 */
static SSMFIELD const g_aTpmPpiFields[] =
{
    SSMFIELD_ENTRY(QEMUTPMPPI, s.abFunc),
    SSMFIELD_ENTRY(QEMUTPMPPI, s.bPpin),
    SSMFIELD_ENTRY(QEMUTPMPPI, s.u32Ppip),
    SSMFIELD_ENTRY(QEMUTPMPPI, s.u32Pprp),
    SSMFIELD_ENTRY(QEMUTPMPPI, s.u32Pprq),
    SSMFIELD_ENTRY(QEMUTPMPPI, s.u32Pprm),
    SSMFIELD_ENTRY(QEMUTPMPPI, s.u32Lppr),
    SSMFIELD_ENTRY(QEMUTPMPPI, s.u32Fret),
    SSMFIELD_ENTRY(QEMUTPMPPI, s.bNextStep),
    SSMFIELD_ENTRY(QEMUTPMPPI, s.bMovv),
    SSMFIELD_ENTRY_TERM()
};
#endif


/* -=-=-=-=-=- MMIO callbacks -=-=-=-=-=- */

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) tpmPpiMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDEVTPMPPI pThis  = PDMDEVINS_2_DATA(pDevIns, PDEVTPMPPI);
    RT_NOREF(pvUser);

    LogFlowFunc(("off=%RGp cb=%u\n", off, cb));
    if (off + cb > sizeof(pThis->Ppi))
        return VINF_IOM_MMIO_UNUSED_FF;

    memcpy(pv, &pThis->Ppi.ab[off], cb);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) tpmPpiMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDEVTPMPPI pThis  = PDMDEVINS_2_DATA(pDevIns, PDEVTPMPPI);
    RT_NOREF(pvUser);

    LogFlowFunc(("off=%RGp cb=%u\n", off, cb));
    if (off + cb > sizeof(pThis->Ppi))
        return VINF_SUCCESS;

    memcpy(&pThis->Ppi.ab[off], pv, cb);
    return VINF_SUCCESS;
}


#ifdef IN_RING3

/* -=-=-=-=-=-=-=-=- Saved State -=-=-=-=-=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) tpmPpiR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PDEVTPMPPI      pThis = PDMDEVINS_2_DATA(pDevIns, PDEVTPMPPI);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    /* Save the part of the config used for verification purposes when restoring. */
    pHlp->pfnSSMPutGCPhys(pSSM, pThis->GCPhysMmio);

    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) tpmPpiR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVTPMPPI      pThis = PDMDEVINS_2_DATA(pDevIns, PDEVTPMPPI);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    tpmPpiR3LiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    int rc = pHlp->pfnSSMPutStructEx(pSSM, &pThis->Ppi, sizeof(pThis->Ppi), 0 /*fFlags*/, &g_aTpmPpiFields[0], NULL);
    AssertRCReturn(rc, rc);

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) tpmPpiR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVTPMPPI      pThis = PDMDEVINS_2_DATA(pDevIns, PDEVTPMPPI);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    RTGCPHYS        GCPhysMmio;

    Assert(uPass == SSM_PASS_FINAL); RT_NOREF(uPass);
    AssertMsgReturn(uVersion == TPM_PPI_SAVED_STATE_VERSION, ("%d\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);

    /* Verify the config first. */
    int rc = pHlp->pfnSSMGetGCPhys(pSSM, &GCPhysMmio);
    AssertRCReturn(rc, rc);
    if (GCPhysMmio != pThis->GCPhysMmio)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                       N_("Config mismatch - saved GCPhysMmio=%#RGp; configured GCPhysMmio=%#RGp"),
                                       GCPhysMmio, pThis->GCPhysMmio);

    if (uPass == SSM_PASS_FINAL)
    {
        rc = pHlp->pfnSSMGetStructEx(pSSM, &pThis->Ppi, sizeof(pThis->Ppi), 0 /*fFlags*/, &g_aTpmPpiFields[0], NULL);
        AssertRCReturn(rc, rc);

        /* The marker. */
        uint32_t u32;
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
        AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) tpmPpiR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)  tpmPpiR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVTPMPPI      pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVTPMPPI);
    PDEVTPMPPICC    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVTPMPPICC);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    int             rc;

    RT_NOREF(iInstance);
    Assert(iInstance == 0);

    /*
     * Initalize the basic variables so that the destructor always works.
     */
    pThisCC->pDevIns = pDevIns;

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "MmioBase", "");

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "MmioBase", &pThis->GCPhysMmio, TPM_PPI_MMIO_BASE_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"MmioBase\" value"));

    /*
     * Register the MMIO range, PDM API requests page aligned
     * addresses and sizes.
     */
    rc = PDMDevHlpMmioCreateAndMap(pDevIns, pThis->GCPhysMmio, TPM_PPI_MMIO_SIZE, tpmPpiMmioWrite, tpmPpiMmioRead,
                                   IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                                   "TPM PPI MMIO", &pThis->hMmio);
    AssertRCReturn(rc, rc);

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegister3(pDevIns, TPM_PPI_SAVED_STATE_VERSION, sizeof(*pThis),
                               tpmPpiR3LiveExec, tpmPpiR3SaveExec, tpmPpiR3LoadExec);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) tpmPpiRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVTPMPPI pThis = PDMDEVINS_2_DATA(pDevIns, PDEVTPMPPI);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, tpmPpiMmioWrite, tpmPpiMmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */


/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceTpmPpi =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "tpm-ppi",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_ARCH_BIOS,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVTPMPPI),
    /* .cbInstanceCC = */           sizeof(DEVTPMPPICC),
    /* .cbInstanceRC = */           sizeof(DEVTPMPPIRC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Device implementing the TPM Physical Presence Interface (PPI)\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "",
    /* .pszR0Mod = */               "",
    /* .pfnConstruct = */           tpmPpiR3Construct,
    /* .pfnDestruct = */            tpmPpiR3Destruct,
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
    /* .pfnConstruct = */           tpmPpiRZConstruct,
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
    /* .pfnConstruct = */           tpmPpiRZConstruct,
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
