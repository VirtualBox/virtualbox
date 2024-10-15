/* $Id: DevVirtualKD.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualKD - Device stub/loader for fast Windows kernel-mode debugging.
 *
 * Contributed by: Ivan Shcherbakov
 * Heavily modified after the contribution.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV // LOG_GROUP_DEV_VIRTUALKD
#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/path.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define IKDClient_InterfaceVersion 3


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct VKDREQUESTHDR
{
    uint32_t cbData;
    uint32_t cbReplyMax;
} VKDREQUESTHDR;
AssertCompileSize(VKDREQUESTHDR, 8);

#pragma pack(1)
typedef struct VKDREPLYHDR
{
    uint32_t cbData;
    char chOne;
    char chSpace;
} VKDREPLYHDR;
#pragma pack()
AssertCompileSize(VKDREPLYHDR, 6);

class IKDClient
{
public:
    virtual unsigned OnRequest(const char *pRequestIncludingRpcHeader, unsigned RequestSizeWithRpcHeader, char **ppReply) = 0;
    virtual ~IKDClient() {}
};

typedef IKDClient *(*PFNCreateVBoxKDClientEx)(unsigned version);

typedef struct VIRTUALKD
{
    bool fOpenChannelDetected;
    bool fChannelDetectSuccessful;
    RTLDRMOD hLib;
    IKDClient *pKDClient;
    char *pbCmdBody;
    bool fFencedCmdBody;    /**< Set if pbCmdBody was allocated using RTMemPageAlloc rather than RTMemAlloc. */
} VIRTUALKD;

#define VIRTUALKB_CMDBODY_SIZE          _256K                /**< Size of buffer pointed to by VIRTUALKB::pbCmdBody */
#define VIRTUALKB_CMDBODY_PRE_FENCE     (HOST_PAGE_SIZE * 4) /**< Size of the eletrict fence before the command body. */
#define VIRTUALKB_CMDBODY_POST_FENCE    (HOST_PAGE_SIZE * 8) /**< Size of the eletrict fence after the command body. */




static DECLCALLBACK(VBOXSTRICTRC) vkdPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser, offPort, cb);
    VIRTUALKD *pThis = PDMDEVINS_2_DATA(pDevIns, VIRTUALKD *);

    if (pThis->fOpenChannelDetected)
    {
        *pu32 = RT_MAKE_U32_FROM_U8('V', 'B', 'O', 'X');    /* 'XOBV', checked in VMWRPC.H */
        pThis->fOpenChannelDetected = false;
        pThis->fChannelDetectSuccessful = true;
    }
    else
        *pu32 = UINT32_MAX;

    return VINF_SUCCESS;
}

static DECLCALLBACK(VBOXSTRICTRC) vkdPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pvUser, cb);
    VIRTUALKD *pThis = PDMDEVINS_2_DATA(pDevIns, VIRTUALKD *);

    if (offPort == 1)
    {
        /*
         * Read the request and request body.  Ignore empty requests.
         */
        RTGCPHYS GCPhys = u32;
        VKDREQUESTHDR RequestHeader = { 0, 0 };
        int rc = PDMDevHlpPhysRead(pDevIns, GCPhys, &RequestHeader, sizeof(RequestHeader));
        if (   RT_SUCCESS(rc)
            && RequestHeader.cbData > 0)
        {
            uint32_t cbData = RT_MIN(RequestHeader.cbData, VIRTUALKB_CMDBODY_SIZE);
            rc = PDMDevHlpPhysRead(pDevIns, GCPhys + sizeof(RequestHeader), pThis->pbCmdBody, cbData);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Call the plugin module.
                 */
                char    *pbReply = NULL;
                unsigned cbReply;
                try
                {
                    cbReply = pThis->pKDClient->OnRequest(pThis->pbCmdBody, cbData, &pbReply);
                    if (!pbReply)
                        cbReply = 0;
                }
                catch (...)
                {
                    LogRel(("DevVirtualKB: OnRequest threw exception. sigh.\n"));
                    cbReply = 0;
                    pbReply = NULL;
                }

                /*
                 * Write the reply to guest memory (overwriting the request):
                 */
                cbReply = RT_MIN(cbReply + 2, RequestHeader.cbReplyMax);
                VKDREPLYHDR ReplyHeader;
                ReplyHeader.cbData = cbReply; /* The '1' and ' ' bytes count towards reply size. */
                ReplyHeader.chOne = '1';
                ReplyHeader.chSpace = ' ';
                rc = PDMDevHlpPhysWrite(pDevIns, GCPhys, &ReplyHeader, sizeof(ReplyHeader.cbData) + RT_MIN(cbReply, 2));
                if (cbReply > 2 && RT_SUCCESS(rc))
                    rc = PDMDevHlpPhysWrite(pDevIns, GCPhys + sizeof(ReplyHeader), pbReply, cbReply - 2);
            }
        }
    }
    else
    {
        Assert(offPort == 0);
        if (u32 == UINT32_C(0x564D5868) /* 'VMXh' */)
            pThis->fOpenChannelDetected = true;
        else
            pThis->fOpenChannelDetected = false;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) vkdDestruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    VIRTUALKD *pThis = PDMDEVINS_2_DATA(pDevIns, VIRTUALKD *);

    if (pThis->pKDClient)
    {
        /** @todo r=bird: This interface is not safe as the object doesn't overload the
         *        delete operator, thus making our runtime free it rather than that of
         *        the plug-in module IIRC. */
        delete pThis->pKDClient;
        pThis->pKDClient = NULL;
    }

    if (pThis->hLib != NIL_RTLDRMOD)
    {
        RTLdrClose(pThis->hLib);
        pThis->hLib = NIL_RTLDRMOD;
    }

    if (pThis->pbCmdBody)
    {
        if (pThis->fFencedCmdBody)
            RTMemPageFree((uint8_t *)pThis->pbCmdBody - RT_ALIGN_Z(VIRTUALKB_CMDBODY_PRE_FENCE, HOST_PAGE_SIZE),
                            RT_ALIGN_Z(VIRTUALKB_CMDBODY_PRE_FENCE,  HOST_PAGE_SIZE)
                          + RT_ALIGN_Z(VIRTUALKB_CMDBODY_SIZE,       HOST_PAGE_SIZE)
                          + RT_ALIGN_Z(VIRTUALKB_CMDBODY_POST_FENCE, HOST_PAGE_SIZE));
        else
            RTMemFree(pThis->pbCmdBody);
        pThis->pbCmdBody = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) vkdConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    VIRTUALKD       *pThis = PDMDEVINS_2_DATA(pDevIns, VIRTUALKD *);
    PCPDMDEVHLPR3   pHlp   = pDevIns->pHlpR3;
    RT_NOREF(iInstance);

    pThis->fOpenChannelDetected = false;
    pThis->fChannelDetectSuccessful = false;
    pThis->hLib = NIL_RTLDRMOD;
    pThis->pKDClient = NULL;
    pThis->pbCmdBody = NULL;
    pThis->fFencedCmdBody = false;

    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Path", "");

    /* This device is a bit unusual, after this point it will not fail to be
     * constructed, but there will be a warning and it will not work. */

    char szPath[RTPATH_MAX];
    int rc = pHlp->pfnCFGMQueryStringDef(pCfg, "Path", szPath, sizeof(szPath) - sizeof("kdclient64.dll"), "");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"Path\" value"));

    rc = RTPathAppend(szPath, sizeof(szPath), HC_ARCH_BITS == 64 ? "kdclient64.dll" : "kdclient.dll");
    AssertRCReturn(rc, rc);
    rc = RTLdrLoad(szPath, &pThis->hLib);
    if (RT_SUCCESS(rc))
    {
        PFNCreateVBoxKDClientEx pfnInit;
        rc = RTLdrGetSymbol(pThis->hLib, "CreateVBoxKDClientEx", (void **)&pfnInit);
        if (RT_SUCCESS(rc))
        {
            pThis->pKDClient = pfnInit(IKDClient_InterfaceVersion);
            if (pThis->pKDClient)
            {
                /* We allocate a fenced buffer for reasons of paranoia. */
                uint8_t *pbCmdBody = (uint8_t *)RTMemPageAlloc(  RT_ALIGN_Z(VIRTUALKB_CMDBODY_PRE_FENCE,  HOST_PAGE_SIZE)
                                                               + RT_ALIGN_Z(VIRTUALKB_CMDBODY_SIZE,       HOST_PAGE_SIZE)
                                                               + RT_ALIGN_Z(VIRTUALKB_CMDBODY_POST_FENCE, HOST_PAGE_SIZE));
                if (pbCmdBody)
                {
                    rc = RTMemProtect(pbCmdBody, RT_ALIGN_Z(VIRTUALKB_CMDBODY_PRE_FENCE, HOST_PAGE_SIZE), RTMEM_PROT_NONE);
                    pbCmdBody += RT_ALIGN_Z(VIRTUALKB_CMDBODY_PRE_FENCE, HOST_PAGE_SIZE);

                    pThis->fFencedCmdBody = true;
                    pThis->pbCmdBody = (char *)pbCmdBody;
                    rc = RTMemProtect(pbCmdBody, RT_ALIGN_Z(VIRTUALKB_CMDBODY_SIZE, HOST_PAGE_SIZE),
                                      RTMEM_PROT_READ | RTMEM_PROT_WRITE);
                    AssertLogRelRC(rc);
                    pbCmdBody += RT_ALIGN_Z(VIRTUALKB_CMDBODY_SIZE, HOST_PAGE_SIZE);

                    rc = RTMemProtect(pbCmdBody, RT_ALIGN_Z(VIRTUALKB_CMDBODY_PRE_FENCE, HOST_PAGE_SIZE),
                                      RTMEM_PROT_NONE);
                    AssertLogRelRC(rc);
                }
                else
                {
                    LogRel(("VirtualKB: RTMemPageAlloc failed, falling back on regular alloc.\n"));
                    pThis->pbCmdBody = (char *)RTMemAllocZ(VIRTUALKB_CMDBODY_SIZE);
                    AssertLogRelReturn(pThis->pbCmdBody, VERR_NO_MEMORY);
                }

                IOMIOPORTHANDLE hIoPorts;
                rc = PDMDevHlpIoPortCreateAndMap(pDevIns, 0x5658 /*uPort*/, 2 /*cPorts*/, vkdPortWrite, vkdPortRead,
                                                 "VirtualKD",  NULL /*paExtDescs*/, &hIoPorts);
                AssertRCReturn(rc, rc);
            }
            else
                PDMDevHlpVMSetRuntimeError(pDevIns, 0 /* fFlags */, "VirtualKD_INIT",
                                           N_("Failed to initialize VirtualKD library '%s'. Fast kernel-mode debugging will not work"), szPath);
        }
        else
            PDMDevHlpVMSetRuntimeError(pDevIns, 0 /* fFlags */, "VirtualKD_SYMBOL",
                                       N_("Failed to find entry point for VirtualKD library '%s'. Fast kernel-mode debugging will not work"), szPath);
    }
    else
        PDMDevHlpVMSetRuntimeError(pDevIns, 0 /* fFlags */, "VirtualKD_LOAD",
                                   N_("Failed to load VirtualKD library '%s'. Fast kernel-mode debugging will not work"), szPath);
    return VINF_SUCCESS;
}


/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceVirtualKD =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "VirtualKD",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_MISC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(VIRTUALKD),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Provides fast debugging interface when debugging Windows kernel",
#if defined(IN_RING3)
    /* .pszRCMod = */               "",
    /* .pszR0Mod = */               "",
    /* .pfnConstruct = */           vkdConstruct,
    /* .pfnDestruct = */            vkdDestruct,
    /* .pfnRelocate = */            NULL,
    /* pfnIOCtl */    NULL,
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

