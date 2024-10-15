/* $Id: DevEFI-armv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevEFI - EFI <-> VirtualBox Integration Framework.
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
#define LOG_GROUP LOG_GROUP_DEV_EFI

#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/mm.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/vmm/dbgf.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include "DevEFI.h"
#include "VBoxDD.h"
#include "VBoxDD2.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The EFI device shared state structure.
 */
typedef struct DEVEFI
{
    uint32_t uEmpty;
} DEVEFI;
/** Pointer to the shared EFI state. */
typedef DEVEFI *PDEVEFI;


/**
 * The EFI device state structure for ring-3.
 */
typedef struct DEVEFIR3
{
    /** Pointer back to the device instance. */
    PPDMDEVINS              pDevIns;

    /** The system EFI ROM data. */
    uint8_t const          *pu8EfiRom;
    /** The system EFI ROM data pointer to be passed to RTFileReadAllFree. */
    uint8_t                *pu8EfiRomFree;
    /** The size of the system EFI ROM. */
    uint64_t                cbEfiRom;
    /** Offset into the actual ROM within EFI FW volume. */
    uint64_t                offEfiRom;
    /** The name of the EFI ROM file. */
    char                   *pszEfiRomFile;
    /** EFI firmware physical load address. */
    RTGCPHYS                GCPhysLoadAddress;
    /** The FDT load address, RTGCPHYS_MAX if not configured to be loaded. */
    RTGCPHYS                GCPhysFdtAddress;
    /** The FDT id used to load from the resource store driver below. */
    char                   *pszFdtId;

    /**
     * Resource port - LUN\#0.
     */
    struct
    {
        /** The base interface we provide the resource driver. */
        PDMIBASE            IBase;
        /** The resource driver base interface. */
        PPDMIBASE           pDrvBase;
        /** The VFS interface of the driver below for resource state loading and storing. */
        PPDMIVFSCONNECTOR   pDrvVfs;
    } Lun0;
} DEVEFIR3;
/** Pointer to the ring-3 EFI state. */
typedef DEVEFIR3 *PDEVEFIR3;


/**
 * The EFI device state structure for ring-0.
 */
typedef struct DEVEFIR0
{
    uint32_t uEmpty;
} DEVEFIR0;
/** Pointer to the ring-0 EFI state.  */
typedef DEVEFIR0 *PDEVEFIR0;


/**
 * The EFI device state structure for raw-mode.
 */
typedef struct DEVEFIRC
{
    uint32_t uEmpty;
} DEVEFIRC;
/** Pointer to the raw-mode EFI state.  */
typedef DEVEFIRC *PDEVEFIRC;


/** @typedef DEVEFICC
 * The instance data for the current context. */
/** @typedef PDEVEFICC
 * Pointer to the instance data for the current context. */
#ifdef IN_RING3
typedef  DEVEFIR3  DEVEFICC;
typedef PDEVEFIR3 PDEVEFICC;
#elif defined(IN_RING0)
typedef  DEVEFIR0  DEVEFICC;
typedef PDEVEFIR0 PDEVEFICC;
#elif defined(IN_RC)
typedef  DEVEFIRC  DEVEFICC;
typedef PDEVEFIRC PDEVEFICC;
#else
# error "Not IN_RING3, IN_RING0 or IN_RC"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The saved state version. */
#define EFI_SSM_VERSION                  1


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IN_RING3

# ifdef VBOX_WITH_EFI_IN_DD2
/** Special file name value for indicating the 32-bit built-in EFI firmware. */
static const char g_szEfiBuiltinAArch32[] = "VBoxEFIAArch32.fd";
/** Special file name value for indicating the 64-bit built-in EFI firmware. */
static const char g_szEfiBuiltinAArch64[] = "VBoxEFIAArch64.fd";
# endif
#endif /* IN_RING3 */


#ifdef IN_RING3

/**
 * @copydoc(PDMIBASE::pfnQueryInterface)
 */
static DECLCALLBACK(void *) devR3EfiQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    LogFlowFunc(("ENTER: pIBase=%p pszIID=%p\n", pInterface, pszIID));
    PDEVEFIR3 pThisCC = RT_FROM_MEMBER(pInterface, DEVEFIR3, Lun0.IBase);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->Lun0.IBase);
    return NULL;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnMemSetup}
 */
static DECLCALLBACK(void) efiR3MemSetup(PPDMDEVINS pDevIns, PDMDEVMEMSETUPCTX enmCtx)
{
    PDEVEFIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);
    LogFlow(("efiR3MemSetup\n"));

    RT_NOREF(enmCtx);
    if (pThisCC->GCPhysFdtAddress != RTGCPHYS_MAX)
    {
        AssertPtr(pThisCC->Lun0.pDrvVfs);

        uint64_t cbFdt = 0;
        int rc = pThisCC->Lun0.pDrvVfs->pfnQuerySize(pThisCC->Lun0.pDrvVfs, pThisCC->pszFdtId, pThisCC->pszFdtId, &cbFdt);
        if (RT_SUCCESS(rc))
        {
            /** @todo Need to add a proper read callback to avoid allocating temporary memory. */
            void *pvFdt = RTMemAllocZ(cbFdt);
            if (pvFdt)
            {
                rc = pThisCC->Lun0.pDrvVfs->pfnReadAll(pThisCC->Lun0.pDrvVfs, pThisCC->pszFdtId, pThisCC->pszFdtId, pvFdt, cbFdt);
                if (RT_SUCCESS(rc))
                    rc = PDMDevHlpPhysWrite(pDevIns, pThisCC->GCPhysFdtAddress, pvFdt, cbFdt);

                RTMemFree(pvFdt);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        AssertLogRelRC(rc);
    }
}


/**
 * Destruct a device instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(int) efiR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PDEVEFIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);

    if (pThisCC->pu8EfiRomFree)
    {
        RTFileReadAllFree(pThisCC->pu8EfiRomFree, (size_t)pThisCC->cbEfiRom + pThisCC->offEfiRom);
        pThisCC->pu8EfiRomFree = NULL;
    }

    /*
     * Free MM heap pointers (waste of time, but whatever).
     */
    if (pThisCC->pszEfiRomFile)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszEfiRomFile);
        pThisCC->pszEfiRomFile = NULL;
    }

    if (pThisCC->pszFdtId)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisCC->pszFdtId);
        pThisCC->pszFdtId = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * Load EFI ROM file into the memory.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The device state for the current context.
 * @param   pCfg        Configuration node handle for the device.
 */
static int efiR3LoadRom(PPDMDEVINS pDevIns, PDEVEFIR3 pThisCC, PCFGMNODE pCfg)
{
    RT_NOREF(pCfg);

    /*
     * Read the entire firmware volume into memory.
     */
    int     rc;
#ifdef VBOX_WITH_EFI_IN_DD2
    if (RTStrCmp(pThisCC->pszEfiRomFile, g_szEfiBuiltinAArch32) == 0)
    {
        pThisCC->pu8EfiRomFree = NULL;
        pThisCC->pu8EfiRom = g_abEfiFirmwareAArch32;
        pThisCC->cbEfiRom  = g_cbEfiFirmwareAArch32;
    }
    else if (RTStrCmp(pThisCC->pszEfiRomFile, g_szEfiBuiltinAArch64) == 0)
    {
        pThisCC->pu8EfiRomFree = NULL;
        pThisCC->pu8EfiRom = g_abEfiFirmwareAArch64;
        pThisCC->cbEfiRom  = g_cbEfiFirmwareAArch64;
    }
    else
#endif
    {
        void   *pvFile;
        size_t  cbFile;
        rc = RTFileReadAllEx(pThisCC->pszEfiRomFile,
                             0 /*off*/,
                             RTFOFF_MAX /*cbMax*/,
                             RTFILE_RDALL_O_DENY_WRITE,
                             &pvFile,
                             &cbFile);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Loading the EFI firmware volume '%s' failed with rc=%Rrc"),
                                       pThisCC->pszEfiRomFile, rc);
        pThisCC->pu8EfiRomFree = (uint8_t *)pvFile;
        pThisCC->pu8EfiRom = (uint8_t *)pvFile;
        pThisCC->cbEfiRom  = cbFile;
    }

    rc = PDMDevHlpROMRegister(pDevIns, pThisCC->GCPhysLoadAddress, pThisCC->cbEfiRom, pThisCC->pu8EfiRom, pThisCC->cbEfiRom,
                              PGMPHYS_ROM_FLAGS_SHADOWED | PGMPHYS_ROM_FLAGS_PERMANENT_BINARY, "EFI Firmware Volume");
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)  efiR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVEFIR3       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVEFIR3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    RT_NOREF(iInstance);
    Assert(iInstance == 0);

    /*
     * Initalize the basic variables so that the destructor always works.
     */
    pThisCC->pDevIns                        = pDevIns;
    pThisCC->pszFdtId                       = NULL;
    pThisCC->GCPhysFdtAddress               = RTGCPHYS_MAX;
    pThisCC->Lun0.IBase.pfnQueryInterface   = devR3EfiQueryInterface;

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "EfiRom|GCPhysLoadAddress|GCPhysFdtAddress|FdtId", "");

    rc = pHlp->pfnCFGMQueryU64(pCfg, "GCPhysLoadAddress", &pThisCC->GCPhysLoadAddress);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"GCPhysLoadAddress\" as integer failed"));

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "GCPhysFdtAddress", &pThisCC->GCPhysFdtAddress, RTGCPHYS_MAX);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"GCPhysFdtAddress\" as integer failed"));

    if (pThisCC->GCPhysFdtAddress != RTGCPHYS_MAX)
    {
        rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "FdtId", &pThisCC->pszFdtId);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Querying \"FdtId\" as a string failed"));
    }

    /*
     * Get the system EFI ROM file name.
     */
#ifdef VBOX_WITH_EFI_IN_DD2
    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "EfiRom", &pThisCC->pszEfiRomFile, g_szEfiBuiltinAArch32);
    if (RT_FAILURE(rc))
#else
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "EfiRom", &pThisCC->pszEfiRomFile);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        pThisCC->pszEfiRomFile = (char *)PDMDevHlpMMHeapAlloc(pDevIns, RTPATH_MAX);
        AssertReturn(pThisCC->pszEfiRomFile, VERR_NO_MEMORY);
        rc = RTPathAppPrivateArchTop(pThisCC->pszEfiRomFile, RTPATH_MAX);
        AssertRCReturn(rc, rc);
        rc = RTPathAppend(pThisCC->pszEfiRomFile, RTPATH_MAX, "VBoxEFIAArch32.fd");
        AssertRCReturn(rc, rc);
    }
    else if (RT_FAILURE(rc))
#endif
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"EfiRom\" as a string failed"));

    /*
     * Load firmware volume and thunk ROM.
     */
    rc = efiR3LoadRom(pDevIns, pThisCC, pCfg);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Resource storage.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->Lun0.IBase, &pThisCC->Lun0.pDrvBase, "ResourceStorage");
    if (RT_SUCCESS(rc))
    {
        pThisCC->Lun0.pDrvVfs = PDMIBASE_QUERY_INTERFACE(pThisCC->Lun0.pDrvBase, PDMIVFSCONNECTOR);
        if (!pThisCC->Lun0.pDrvVfs)
            return PDMDevHlpVMSetError(pDevIns, VERR_PDM_MISSING_INTERFACE_BELOW, RT_SRC_POS, N_("Resource storage driver is missing VFS interface below"));
    }
    else if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
             && pThisCC->GCPhysFdtAddress == RTGCPHYS_MAX)
        rc = VINF_SUCCESS; /* Missing driver is no error condition if no FDT is going to be loaded. */
    else
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("Can't attach resource Storage driver"));

    return VINF_SUCCESS;
}

#else  /* IN_RING3 */


/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int)  efiRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    RT_NOREF(pDevIns);

    return VINF_SUCCESS;
}


#endif /* IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceEfiArmV8 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "efi-armv8",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_ARCH_BIOS,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVEFI),
    /* .cbInstanceCC = */           sizeof(DEVEFICC),
    /* .cbInstanceRC = */           sizeof(DEVEFIRC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Extensible Firmware Interface Device for ARMv8 platforms.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           efiR3Construct,
    /* .pfnDestruct = */            efiR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            efiR3MemSetup,
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
    /* .pfnConstruct = */           efiRZConstruct,
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
    /* .pfnConstruct = */           efiRZConstruct,
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
