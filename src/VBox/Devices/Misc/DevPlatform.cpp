/* $Id: DevPlatform.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPlatform - Guest platform <-> VirtualBox Integration Framework.
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
#define LOG_GROUP LOG_GROUP_DEV_PLATFORM

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
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include "VBoxDD.h"
#include "VBoxDD2.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Platform resource entry.
 */
typedef struct DEVPLATFORMRESOURCE
{
    /** Node in the list of resources added to the VM. */
    RTLISTNODE              NdLst;
    /** The ID in the resource store or filename to load. */
    const char              *pszResourceIdOrFilename;
    /** The physical load address of the resource. */
    RTGCPHYS                GCPhysResource;
    /** The resource data. */
    uint8_t const          *pu8Resource;
    /** The resource data pointer to be passed to RTFileReadAllFree (only valid when DEVPLATFORMRESOURCE::fResourceId is false). */
    uint8_t                *pu8ResourceFree;
    /** The size of the resource. */
    size_t                  cbResource;
    /** Flag whether the resource is loaded from the resource store of from a file. */
    bool                    fResourceId;
    /** Name of the resource. */
    char                    szName[32];
} DEVPLATFORMRESOURCE;
/** Pointer to a platform resource entry. */
typedef DEVPLATFORMRESOURCE *PDEVPLATFORMRESOURCE;
/** Pointer to a constant platform resource entry. */
typedef const DEVPLATFORMRESOURCE *PCDEVPLATFORMRESOURCE;


/**
 * The plaatform device state structure, shared state.
 */
typedef struct DEVPLATFORM
{
    /** Pointer back to the device instance. */
    PPDMDEVINS              pDevIns;

    /** The resource namespace. */
    char                   *pszResourceNamespace;

    /** List head for resources added in the memory setup phase
     * (written to guest memory). */
    RTLISTANCHOR            LstResourcesMem;
    /** List head for resources added as a ROM file. */
    RTLISTANCHOR            LstResourcesRom;

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
} DEVPLATFORM;
/** Pointer to the platform device state. */
typedef DEVPLATFORM *PDEVPLATFORM;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
# ifdef VBOX_WITH_EFI_IN_DD2
/** Special file name value for indicating the 32-bit built-in EFI firmware. */
static const char g_szEfiBuiltinAArch32[] = "VBoxEFIAArch32.fd";
/** Special file name value for indicating the 64-bit built-in EFI firmware. */
static const char g_szEfiBuiltinAArch64[] = "VBoxEFIAArch64.fd";
# endif


/**
 * Resolves the given resource content.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance data.
 * @param   pThis               The device state for the current context.
 * @param   pRes                The resource to resolve.
 * @param   ppvFree             Where to store pointer to the function freeing the resource data
 *                              (RTFileReadAllFree() or PDMDevHlpMMHeapFree()).
 * @param   ppv                 Where to store the pointer to the reoucer data content.
 * @param   pcb                 Where to store the size of the content in bytes.
 */
static int platformR3ResourceResolveContent(PPDMDEVINS pDevIns, PDEVPLATFORM pThis,
                                            PCDEVPLATFORMRESOURCE pRes,
                                            void **ppvFree, void const **ppv, size_t *pcb)
{
    if (pRes->fResourceId)
    {
#ifdef VBOX_WITH_EFI_IN_DD2
        if (RTStrCmp(pRes->pszResourceIdOrFilename, g_szEfiBuiltinAArch32) == 0)
        {
            *ppvFree = NULL;
            *ppv     = g_abEfiFirmwareAArch32;
            *pcb     = g_cbEfiFirmwareAArch32;
        }
        else if (RTStrCmp(pRes->pszResourceIdOrFilename, g_szEfiBuiltinAArch64) == 0)
        {
            *ppvFree = NULL;
            *ppv     = g_abEfiFirmwareAArch64;
            *pcb     = g_cbEfiFirmwareAArch64;
        }
        else
#endif
        {
            AssertPtrReturn(pThis->Lun0.pDrvVfs, VERR_INVALID_STATE);

            uint64_t cbResource = 0;
            int rc = pThis->Lun0.pDrvVfs->pfnQuerySize(pThis->Lun0.pDrvVfs, pThis->pszResourceNamespace, pRes->pszResourceIdOrFilename, &cbResource);
            if (RT_SUCCESS(rc))
            {
                void *pv = PDMDevHlpMMHeapAlloc(pDevIns, cbResource);
                if (pv)
                {
                    rc = pThis->Lun0.pDrvVfs->pfnReadAll(pThis->Lun0.pDrvVfs, pThis->pszResourceNamespace, pRes->pszResourceIdOrFilename, pv, cbResource);
                    if (RT_FAILURE(rc))
                    {
                        PDMDevHlpMMHeapFree(pDevIns, pv);
                        return rc;
                    }

                    *ppvFree = pv;
                    *ppv     = pv;
                    *pcb     = cbResource;
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            AssertLogRelRCReturn(rc, rc);
        }
    }
    else
    {
        void   *pvFile;
        size_t  cbFile;
        int rc = RTFileReadAllEx(pRes->pszResourceIdOrFilename,
                                 0 /*off*/,
                                 RTFOFF_MAX /*cbMax*/,
                                 RTFILE_RDALL_O_DENY_WRITE,
                                 &pvFile,
                                 &cbFile);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Loading the resource '%s' failed with rc=%Rrc"),
                                       pRes->pszResourceIdOrFilename, rc);
        *ppvFree = (uint8_t *)pvFile;
        *ppv     = (uint8_t *)pvFile;
        *pcb     = cbFile;
    }

    return VINF_SUCCESS;
}


/**
 * Returns the load address of the given resource.
 *
 * @returns Guest physical load address of the resource.
 * @param   pDevIns             The device instance data.
 * @param   pRes                The resource to get the load address from.
 */
DECLINLINE(RTGCPHYS) platformR3ResourceGetLoadAddress(PPDMDEVINS pDevIns, PCDEVPLATFORMRESOURCE pRes)
{
    /*
     * Maximum means determine the guest physical address width and place at the end
     * (which requires the size of the resource to be known (assuming we won't ever have
     * one byte large contents to be placed at the end).
     */
    if (pRes->GCPhysResource == RTGCPHYS_MAX)
    {
        uint8_t cPhysAddrBits, cLinearAddrBits;
        PDMDevHlpCpuGetGuestAddrWidths(pDevIns, &cPhysAddrBits, &cLinearAddrBits);

        return RT_BIT_64(cPhysAddrBits) - pRes->cbResource;
    }

    return pRes->GCPhysResource;
}


/**
 * Destroys the given resource list.
 *
 * @param   pDevIns             The device instance data.
 * @param   pLst                The resource list to destroy.
 */
static void platformR3DestructResourceList(PPDMDEVINS pDevIns, PRTLISTANCHOR pLst)
{
    PDEVPLATFORMRESOURCE pIt, pItNext;

    RTListForEachSafe(pLst, pIt, pItNext, DEVPLATFORMRESOURCE, NdLst)
    {
        RTListNodeRemove(&pIt->NdLst);

        if (pIt->pu8ResourceFree)
        {
            if (!pIt->fResourceId)
                RTFileReadAllFree(pIt->pu8ResourceFree, (size_t)pIt->cbResource);
            else
                PDMDevHlpMMHeapFree(pDevIns, pIt->pu8ResourceFree);
        }

        PDMDevHlpMMHeapFree(pDevIns, (void *)pIt->pszResourceIdOrFilename);

        pIt->pszResourceIdOrFilename = NULL;
        pIt->pu8ResourceFree         = NULL;
        pIt->pu8Resource             = NULL;
        pIt->cbResource              = 0;

        PDMDevHlpMMHeapFree(pDevIns, pIt);
    }
}


/**
 * @copydoc(PDMIBASE::pfnQueryInterface)
 */
static DECLCALLBACK(void *) platformR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    LogFlowFunc(("ENTER: pIBase=%p pszIID=%p\n", pInterface, pszIID));
    PDEVPLATFORM pThis = RT_FROM_MEMBER(pInterface, DEVPLATFORM, Lun0.IBase);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThis->Lun0.IBase);
    return NULL;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnMemSetup}
 */
static DECLCALLBACK(void) platformR3MemSetup(PPDMDEVINS pDevIns, PDMDEVMEMSETUPCTX enmCtx)
{
    PDEVPLATFORM pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPLATFORM);
    LogFlow(("platformR3MemSetup\n"));

    RT_NOREF(enmCtx);

    /* Iterate over the memory resource list and write everything there. */
    PDEVPLATFORMRESOURCE pIt;
    RTListForEach(&pThis->LstResourcesMem, pIt, DEVPLATFORMRESOURCE, NdLst)
    {
        int rc = platformR3ResourceResolveContent(pDevIns, pThis, pIt, (void **)&pIt->pu8ResourceFree, (const void **)&pIt->pu8Resource, &pIt->cbResource);
        if (RT_SUCCESS(rc))
        {
            rc = PDMDevHlpPhysWrite(pDevIns, platformR3ResourceGetLoadAddress(pDevIns, pIt), pIt->pu8Resource, pIt->cbResource);

            /* Don't need to keep it around, will be queried the next time the VM is reset. */
            if (pIt->pu8ResourceFree)
            {
                if (!pIt->fResourceId)
                    RTFileReadAllFree(pIt->pu8ResourceFree, (size_t)pIt->cbResource);
                else
                    PDMDevHlpMMHeapFree(pDevIns, pIt->pu8ResourceFree);
            }

            pIt->pu8ResourceFree = NULL;
            pIt->pu8Resource     = NULL;
            pIt->cbResource      = 0;
        }
        AssertLogRelRCReturnVoid(rc);
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
static DECLCALLBACK(int) platformR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PDEVPLATFORM pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPLATFORM);

    /*
     * Walk the resource lists and free everything.
     */
    platformR3DestructResourceList(pDevIns, &pThis->LstResourcesMem);
    platformR3DestructResourceList(pDevIns, &pThis->LstResourcesRom);

    RTListInit(&pThis->LstResourcesMem);
    RTListInit(&pThis->LstResourcesRom);

    /*
     * Free MM heap pointers (waste of time, but whatever).
     */
    if (pThis->pszResourceNamespace)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThis->pszResourceNamespace);
        pThis->pszResourceNamespace = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * Load the ROM resources into the guest physical address space.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The device state for the current context.
 */
static int platformR3LoadRoms(PPDMDEVINS pDevIns, PDEVPLATFORM pThis)
{
    int rc = VINF_SUCCESS;

    PDEVPLATFORMRESOURCE pIt;
    RTListForEach(&pThis->LstResourcesRom, pIt, DEVPLATFORMRESOURCE, NdLst)
    {
        rc = platformR3ResourceResolveContent(pDevIns, pThis, pIt, (void **)&pIt->pu8ResourceFree, (void const **)&pIt->pu8Resource, &pIt->cbResource);
        if (RT_SUCCESS(rc))
            rc = PDMDevHlpROMRegister(pDevIns, platformR3ResourceGetLoadAddress(pDevIns, pIt), pIt->cbResource, pIt->pu8Resource, pIt->cbResource,
                                      PGMPHYS_ROM_FLAGS_SHADOWED | PGMPHYS_ROM_FLAGS_PERMANENT_BINARY, pIt->szName);
        AssertRCReturn(rc, rc);
    }

    return rc;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)  platformR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPLATFORM    pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPLATFORM);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    int             rc;

    RT_NOREF(iInstance);
    Assert(iInstance == 0);

    /*
     * Initalize the basic variables so that the destructor always works.
     */
    pThis->pDevIns                        = pDevIns;
    pThis->pszResourceNamespace           = NULL;
    pThis->Lun0.IBase.pfnQueryInterface   = platformR3QueryInterface;
    RTListInit(&pThis->LstResourcesMem);
    RTListInit(&pThis->LstResourcesRom);

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "ResourceNamespace", "Resources");

    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "ResourceNamespace", &pThis->pszResourceNamespace);
    if (RT_FAILURE(rc) && rc != VERR_CFGM_VALUE_NOT_FOUND)
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"ResourceNamespace\" as a string failed"));

    /*
     * Resource storage.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThis->Lun0.IBase, &pThis->Lun0.pDrvBase, "ResourceStorage");
    if (RT_SUCCESS(rc))
    {
        pThis->Lun0.pDrvVfs = PDMIBASE_QUERY_INTERFACE(pThis->Lun0.pDrvBase, PDMIVFSCONNECTOR);
        if (!pThis->Lun0.pDrvVfs)
            return PDMDevHlpVMSetError(pDevIns, VERR_PDM_MISSING_INTERFACE_BELOW, RT_SRC_POS, N_("Resource storage driver is missing VFS interface below"));
    }
    else
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("Can't attach resource Storage driver"));

    /*
     * Load the resources.
     */
    PCFGMNODE pCfgRes = pHlp->pfnCFGMGetChild(pCfg, "Resources");
    if (pCfgRes)
    {
        pCfgRes = pHlp->pfnCFGMGetFirstChild(pCfgRes);
        while (pCfgRes)
        {
            rc = pHlp->pfnCFGMValidateConfig(pCfgRes, "/",
                                             "RegisterAsRom"
                                             "|Filename"
                                             "|ResourceId"
                                             "|GCPhysLoadAddress",
                                             "",
                                             pDevIns->pReg->szName, pDevIns->iInstance);
            if (RT_FAILURE(rc))
                return rc;

            bool fRegisterAsRom;
            rc = pHlp->pfnCFGMQueryBool(pCfgRes, "RegisterAsRom", &fRegisterAsRom);
            if (RT_FAILURE(rc))
                return PDMDEV_SET_ERROR(pDevIns, rc,
                                        N_("Configuration error: Querying \"RegisterAsRom\" as boolean failed"));

            PDEVPLATFORMRESOURCE pRes = (PDEVPLATFORMRESOURCE)PDMDevHlpMMHeapAlloc(pDevIns, sizeof(*pRes));
            if (!pRes)
                return PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY,
                                        N_("Configuration error: Failed to allocate resource node"));

            rc = pHlp->pfnCFGMGetName(pCfgRes, &pRes->szName[0], sizeof(pRes->szName));
            if (RT_FAILURE(rc))
                return PDMDEV_SET_ERROR(pDevIns, rc,
                                        N_("Configuration error: Querying resource name as a string failed"));

            rc = pHlp->pfnCFGMQueryU64Def(pCfgRes, "GCPhysLoadAddress", &pRes->GCPhysResource, RTGCPHYS_MAX);
            if (RT_FAILURE(rc))
                return PDMDEV_SET_ERROR(pDevIns, rc,
                                        N_("Configuration error: Querying \"GCPhysLoadAddress\" as integer failed"));

            /* Setting a filename overrides the resource store (think of CFGM extradata from the user). */
            pRes->fResourceId = false;
            rc = pHlp->pfnCFGMQueryStringAlloc(pCfgRes, "Filename", (char **)&pRes->pszResourceIdOrFilename);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND)
            {
                /* There must be an existing resource ID. */
                rc = pHlp->pfnCFGMQueryStringAlloc(pCfgRes, "ResourceId", (char **)&pRes->pszResourceIdOrFilename);
                if (RT_SUCCESS(rc))
                    pRes->fResourceId = true;
            }
            if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Configuration error: Querying \"Filename\" or \"ResourceId\" as a string failed"));

            /* Add it to the appropriate list. */
            RTListAppend(fRegisterAsRom ? &pThis->LstResourcesRom : &pThis->LstResourcesMem, &pRes->NdLst);

            /* Next one please. */
            pCfgRes = pHlp->pfnCFGMGetNextChild(pCfgRes);
        }
    }

    /* Load and register the ROM resources. */
    rc = platformR3LoadRoms(pDevIns, pThis);
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}


/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePlatform =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "platform",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_ARCH_BIOS,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVPLATFORM),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Generic platform device for registering ROMs and loading resources into guest RAM.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "",
    /* .pszR0Mod = */               "",
    /* .pfnConstruct = */           platformR3Construct,
    /* .pfnDestruct = */            platformR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            platformR3MemSetup,
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
