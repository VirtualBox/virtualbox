/* $Id: GaDrvEnvWddm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface to the WDDM miniport driver.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#include "GaDrvEnvWddm.h"

#include "svga3d_reg.h"

#include <common/wddm/VBoxMPIf.h>

#include <iprt/alloc.h>
#include <iprt/log.h>
#include <iprt/param.h>

typedef struct GAWDDMCONTEXTINFO
{
    AVLU32NODECORE            Core;
    HANDLE                    hContext;
    VOID                     *pCommandBuffer;
    UINT                      CommandBufferSize;
    D3DDDI_ALLOCATIONLIST    *pAllocationList;
    UINT                      AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT                      PatchLocationListSize;
} GAWDDMCONTEXTINFO;

static HRESULT
vboxDdiContextGetId(GaWddmCallbacks *pWddmCallbacks,
                    HANDLE hContext,
                    uint32_t *pu32Cid)
{
    HRESULT                   hr;
    D3DDDICB_ESCAPE           ddiEscape;
    VBOXDISPIFESCAPE_GAGETCID data;

    memset(&data, 0, sizeof(data));
    data.EscapeHdr.escapeCode  = VBOXESC_GAGETCID;
    // data.EscapeHdr.cmdSpecific = 0;
    // data.u32Cid                = 0;

    /* If the user-mode display driver sets hContext to a non-NULL value, the driver must
     * have also set hDevice to a non-NULL value...
     */
    ddiEscape.hDevice               = pWddmCallbacks->hDevice;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.pPrivateDriverData    = &data;
    ddiEscape.PrivateDriverDataSize = sizeof(data);
    ddiEscape.hContext              = hContext;

    hr = pWddmCallbacks->DeviceCallbacks.pfnEscapeCb(pWddmCallbacks->hAdapter, &ddiEscape);
    if (SUCCEEDED(hr))
    {
        *pu32Cid = data.u32Cid;
    }
    return hr;
}

static void
vboxDdiContextDestroy(GaWddmCallbacks *pWddmCallbacks,
                      GAWDDMCONTEXTINFO *pContextInfo)
{
    if (pContextInfo->hContext)
    {
        D3DDDICB_DESTROYCONTEXT ddiDestroyContext;
        memset(&ddiDestroyContext, 0, sizeof(ddiDestroyContext));
        ddiDestroyContext.hContext = pContextInfo->hContext;
        pWddmCallbacks->DeviceCallbacks.pfnDestroyContextCb(pWddmCallbacks->hDevice, &ddiDestroyContext);
    }
}

static HRESULT
vboxDdiContextCreate(GaWddmCallbacks *pWddmCallbacks,
                     void *pvPrivateData, uint32_t cbPrivateData,
                     GAWDDMCONTEXTINFO *pContextInfo)
{
    HRESULT hr;
    D3DDDICB_CREATECONTEXT ddiCreateContext;

    memset(&ddiCreateContext, 0, sizeof(ddiCreateContext));
    // ddiCreateContext.NodeOrdinal = 0;
    // ddiCreateContext.EngineAffinity = 0;
    // ddiCreateContext.Flags.Value = 0;
    ddiCreateContext.pPrivateDriverData = pvPrivateData;
    ddiCreateContext.PrivateDriverDataSize = cbPrivateData;

    hr = pWddmCallbacks->DeviceCallbacks.pfnCreateContextCb(pWddmCallbacks->hDevice, &ddiCreateContext);
    if (hr == S_OK)
    {
        /* Query cid. */
        uint32_t u32Cid = 0;
        hr = vboxDdiContextGetId(pWddmCallbacks, ddiCreateContext.hContext, &u32Cid);
        if (SUCCEEDED(hr))
        {
            pContextInfo->Core.Key              = u32Cid;
            pContextInfo->hContext              = ddiCreateContext.hContext;
            pContextInfo->pCommandBuffer        = ddiCreateContext.pCommandBuffer;
            pContextInfo->CommandBufferSize     = ddiCreateContext.CommandBufferSize;
            pContextInfo->pAllocationList       = ddiCreateContext.pAllocationList;
            pContextInfo->AllocationListSize    = ddiCreateContext.AllocationListSize;
            pContextInfo->pPatchLocationList    = ddiCreateContext.pPatchLocationList;
            pContextInfo->PatchLocationListSize = ddiCreateContext.PatchLocationListSize;
        }
        else
        {
            vboxDdiContextDestroy(pWddmCallbacks, pContextInfo);
        }
    }

    return hr;
}

/* static */ DECLCALLBACK(void)
GaDrvEnvWddm::gaEnvWddmContextDestroy(void *pvEnv,
                                      uint32_t u32Cid)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    GAWDDMCONTEXTINFO *pContextInfo = (GAWDDMCONTEXTINFO *)RTAvlU32Remove(&pThis->mContextTree, u32Cid);
    if (pContextInfo)
    {
        vboxDdiContextDestroy(&pThis->mWddmCallbacks, pContextInfo);
        memset(pContextInfo, 0, sizeof(*pContextInfo));
        RTMemFree(pContextInfo);
    }
}

HANDLE GaDrvEnvWddm::GaDrvEnvWddmContextHandle(uint32_t u32Cid)
{
    GAWDDMCONTEXTINFO *pContextInfo = (GAWDDMCONTEXTINFO *)RTAvlU32Get(&mContextTree, u32Cid);
    return pContextInfo ? pContextInfo->hContext : NULL;
}

/* static */ DECLCALLBACK(uint32_t)
GaDrvEnvWddm::gaEnvWddmContextCreate(void *pvEnv,
                                     boolean extended,
                                     boolean vgpu10)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    VBOXWDDM_CREATECONTEXT_INFO privateData;
    GAWDDMCONTEXTINFO *pContextInfo;
    HRESULT hr;

    pContextInfo = (GAWDDMCONTEXTINFO *)RTMemAlloc(sizeof(GAWDDMCONTEXTINFO));
    if (!pContextInfo)
        return (uint32_t)-1;

    memset(&privateData, 0, sizeof(privateData));
    privateData.u32IfVersion   = 9;
    privateData.enmType        = VBOXWDDM_CONTEXT_TYPE_GA_3D;
    privateData.u.vmsvga.u32Flags  = extended? VBOXWDDM_F_GA_CONTEXT_EXTENDED: 0;
    privateData.u.vmsvga.u32Flags |= vgpu10? VBOXWDDM_F_GA_CONTEXT_VGPU10: 0;

    hr = vboxDdiContextCreate(&pThis->mWddmCallbacks,
                              &privateData, sizeof(privateData), pContextInfo);
    if (SUCCEEDED(hr))
    {
        if (RTAvlU32Insert(&pThis->mContextTree, &pContextInfo->Core))
        {
            return pContextInfo->Core.Key;
        }

        vboxDdiContextDestroy(&pThis->mWddmCallbacks,
                              pContextInfo);
    }

    RTMemFree(pContextInfo);
    return (uint32_t)-1;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvWddm::gaEnvWddmSurfaceDefine(void *pvEnv,
                                     GASURFCREATE *pCreateParms,
                                     GASURFSIZE *paSizes,
                                     uint32_t cSizes,
                                     uint32_t *pu32Sid)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    HRESULT                           hr;
    D3DDDICB_ESCAPE                   ddiEscape;
    VBOXDISPIFESCAPE_GASURFACEDEFINE *pData;
    uint32_t                          cbAlloc;
    uint8_t                          *pu8Req;
    uint32_t                          cbReq;

    /* Size of the SVGA request data */
    cbReq = sizeof(GASURFCREATE) + cSizes * sizeof(GASURFSIZE);
    /* How much to allocate for WDDM escape data. */
    cbAlloc =   sizeof(VBOXDISPIFESCAPE_GASURFACEDEFINE)
              + cbReq;

    pData = (VBOXDISPIFESCAPE_GASURFACEDEFINE *)RTMemAllocZ(cbAlloc);
    if (!pData)
        return -1;

    pData->EscapeHdr.escapeCode  = VBOXESC_GASURFACEDEFINE;
    // pData->EscapeHdr.cmdSpecific = 0;
    // pData->u32Sid                = 0;
    pData->cbReq                 = cbReq;
    pData->cSizes                = cSizes;

    pu8Req = (uint8_t *)&pData[1];
    memcpy(pu8Req, pCreateParms, sizeof(GASURFCREATE));
    memcpy(&pu8Req[sizeof(GASURFCREATE)], paSizes, cSizes * sizeof(GASURFSIZE));

    ddiEscape.hDevice               = 0; // pThis->mWddmCallbacks.hDevice;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.Flags.HardwareAccess  = 1; // Required, otherwise graphics corruption can happen. No idea why.
                                         // Eventually we probably have to create allocations for surfaces,
                                         // as a WDDM driver should do. Then the Escape hack will be removed.
    ddiEscape.pPrivateDriverData    = pData;
    ddiEscape.PrivateDriverDataSize = cbAlloc;
    ddiEscape.hContext              = 0;

    hr = pThis->mWddmCallbacks.DeviceCallbacks.pfnEscapeCb(pThis->mWddmCallbacks.hAdapter, &ddiEscape);
    if (FAILED(hr))
    {
        RTMemFree(pData);
        return -1;
    }

    *pu32Sid = pData->u32Sid;
    RTMemFree(pData);
    return 0;
}

/* static */ DECLCALLBACK(void)
GaDrvEnvWddm::gaEnvWddmSurfaceDestroy(void *pvEnv,
                                      uint32_t u32Sid)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    HRESULT                           hr;
    D3DDDICB_ESCAPE                   ddiEscape;
    VBOXDISPIFESCAPE_GASURFACEDESTROY data;

    memset(&data, 0, sizeof(data));
    data.EscapeHdr.escapeCode  = VBOXESC_GASURFACEDESTROY;
    // data.EscapeHdr.cmdSpecific = 0;
    data.u32Sid                = u32Sid;

    ddiEscape.hDevice               = 0; // pThis->mWddmCallbacks.hDevice;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.Flags.HardwareAccess  = 1; // Required, otherwise graphics corruption can happen. No idea why.
                                         // Eventually we probably have to create allocations for surfaces,
                                         // as a WDDM driver should do. Then the Escape hack will be removed.
    ddiEscape.pPrivateDriverData    = &data;
    ddiEscape.PrivateDriverDataSize = sizeof(data);
    ddiEscape.hContext              = 0;

    hr = pThis->mWddmCallbacks.DeviceCallbacks.pfnEscapeCb(pThis->mWddmCallbacks.hAdapter, &ddiEscape);
    Assert(SUCCEEDED(hr));
}

static HRESULT
vboxDdiFenceCreate(GaWddmCallbacks *pWddmCallbacks,
                   GAWDDMCONTEXTINFO *pContextInfo,
                   uint32_t *pu32FenceHandle)
{
    HRESULT                        hr;
    D3DDDICB_ESCAPE                ddiEscape;
    VBOXDISPIFESCAPE_GAFENCECREATE fenceCreate;

    memset(&fenceCreate, 0, sizeof(fenceCreate));

    fenceCreate.EscapeHdr.escapeCode  = VBOXESC_GAFENCECREATE;
    // fenceCreate.EscapeHdr.cmdSpecific = 0;

    /* If the user-mode display driver sets hContext to a non-NULL value, the driver must
     * have also set hDevice to a non-NULL value...
     */
    ddiEscape.hDevice               = pWddmCallbacks->hDevice;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.pPrivateDriverData    = &fenceCreate;
    ddiEscape.PrivateDriverDataSize = sizeof(fenceCreate);
    ddiEscape.hContext              = pContextInfo->hContext;

    hr = pWddmCallbacks->DeviceCallbacks.pfnEscapeCb(pWddmCallbacks->hAdapter, &ddiEscape);
    if (SUCCEEDED(hr))
    {
        *pu32FenceHandle = fenceCreate.u32FenceHandle;
    }

    return hr;
}

static HRESULT
vboxDdiFenceQuery(GaWddmCallbacks *pWddmCallbacks,
                  uint32_t u32FenceHandle,
                  GAFENCEQUERY *pFenceQuery)
{
    HRESULT                   hr;
    D3DDDICB_ESCAPE           ddiEscape;
    VBOXDISPIFESCAPE_GAFENCEQUERY fenceQuery;

    RT_ZERO(fenceQuery);
    fenceQuery.EscapeHdr.escapeCode  = VBOXESC_GAFENCEQUERY;
    // fenceQuery.EscapeHdr.cmdSpecific = 0;
    fenceQuery.u32FenceHandle = u32FenceHandle;

    ddiEscape.hDevice               = pWddmCallbacks->hDevice;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.pPrivateDriverData    = &fenceQuery;
    ddiEscape.PrivateDriverDataSize = sizeof(fenceQuery);
    ddiEscape.hContext              = 0;

    hr = pWddmCallbacks->DeviceCallbacks.pfnEscapeCb(pWddmCallbacks->hAdapter, &ddiEscape);
    if (SUCCEEDED(hr))
    {
        pFenceQuery->u32FenceHandle    = fenceQuery.u32FenceHandle;
        pFenceQuery->u32SubmittedSeqNo = fenceQuery.u32SubmittedSeqNo;
        pFenceQuery->u32ProcessedSeqNo = fenceQuery.u32ProcessedSeqNo;
        pFenceQuery->u32FenceStatus    = fenceQuery.u32FenceStatus;
    }
    return hr;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvWddm::gaEnvWddmFenceQuery(void *pvEnv,
                                  uint32_t u32FenceHandle,
                                  GAFENCEQUERY *pFenceQuery)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    if (!pThis->mWddmCallbacks.hDevice)
    {
        pFenceQuery->u32FenceStatus = GA_FENCE_STATUS_NULL;
        return 0;
    }

    HRESULT hr = vboxDdiFenceQuery(&pThis->mWddmCallbacks, u32FenceHandle, pFenceQuery);
    if (FAILED(hr))
        return -1;

    return 0;
}

static HRESULT
vboxDdiFenceWait(GaWddmCallbacks *pWddmCallbacks,
                 uint32_t u32FenceHandle,
                 uint32_t u32TimeoutUS)
{
    HRESULT                      hr;
    D3DDDICB_ESCAPE              ddiEscape;
    VBOXDISPIFESCAPE_GAFENCEWAIT fenceWait;

    memset(&fenceWait, 0, sizeof(fenceWait));

    fenceWait.EscapeHdr.escapeCode  = VBOXESC_GAFENCEWAIT;
    // pFenceWait->EscapeHdr.cmdSpecific = 0;
    fenceWait.u32FenceHandle = u32FenceHandle;
    fenceWait.u32TimeoutUS = u32TimeoutUS;

    ddiEscape.hDevice               = pWddmCallbacks->hDevice;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.pPrivateDriverData    = &fenceWait;
    ddiEscape.PrivateDriverDataSize = sizeof(fenceWait);
    ddiEscape.hContext              = 0;

    hr = pWddmCallbacks->DeviceCallbacks.pfnEscapeCb(pWddmCallbacks->hAdapter, &ddiEscape);
    return hr;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvWddm::gaEnvWddmFenceWait(void *pvEnv,
                                 uint32_t u32FenceHandle,
                                 uint32_t u32TimeoutUS)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    if (!pThis->mWddmCallbacks.hDevice)
        return 0;

    HRESULT hr = vboxDdiFenceWait(&pThis->mWddmCallbacks, u32FenceHandle, u32TimeoutUS);
    return SUCCEEDED(hr) ? 0 : -1;
}

static HRESULT
vboxDdiFenceUnref(GaWddmCallbacks *pWddmCallbacks,
                  uint32_t u32FenceHandle)
{
    HRESULT                       hr;
    D3DDDICB_ESCAPE               ddiEscape;
    VBOXDISPIFESCAPE_GAFENCEUNREF fenceUnref;

    memset(&fenceUnref, 0, sizeof(fenceUnref));

    fenceUnref.EscapeHdr.escapeCode  = VBOXESC_GAFENCEUNREF;
    // pFenceUnref->EscapeHdr.cmdSpecific = 0;
    fenceUnref.u32FenceHandle = u32FenceHandle;

    ddiEscape.hDevice               = pWddmCallbacks->hDevice;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.pPrivateDriverData    = &fenceUnref;
    ddiEscape.PrivateDriverDataSize = sizeof(fenceUnref);
    ddiEscape.hContext              = 0;

    hr = pWddmCallbacks->DeviceCallbacks.pfnEscapeCb(pWddmCallbacks->hAdapter, &ddiEscape);
    return hr;
}

/* static */ DECLCALLBACK(void)
GaDrvEnvWddm::gaEnvWddmFenceUnref(void *pvEnv,
                                  uint32_t u32FenceHandle)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    if (!pThis->mWddmCallbacks.hDevice)
        return;

    vboxDdiFenceUnref(&pThis->mWddmCallbacks, u32FenceHandle);
}

/** Calculate how many commands will fit in the buffer.
 *
 * @param pu8Commands Command buffer.
 * @param cbCommands  Size of command buffer.
 * @param cbAvail     Available buffer size..
 * @param pu32Length  Size of commands which will fit in cbAvail bytes.
 */
static HRESULT
vboxCalcCommandLength(const uint8_t *pu8Commands, uint32_t cbCommands, uint32_t cbAvail, uint32_t *pu32Length)
{
    HRESULT hr = S_OK;

    uint32_t u32Length = 0;
    const uint8_t *pu8Src = pu8Commands;
    const uint8_t *pu8SrcEnd = pu8Commands + cbCommands;

    while (pu8SrcEnd > pu8Src)
    {
        const uint32_t cbSrcLeft = pu8SrcEnd - pu8Src;
        AssertBreakStmt(cbSrcLeft >= sizeof(uint32_t), hr = E_INVALIDARG);

        /* Get the command id and command length. */
        const uint32_t u32CmdId = *(uint32_t *)pu8Src;
        uint32_t cbCmd = 0;

        if (SVGA_3D_CMD_BASE <= u32CmdId && u32CmdId < SVGA_3D_CMD_MAX)
        {
            AssertBreakStmt(cbSrcLeft >= sizeof(SVGA3dCmdHeader), hr = E_INVALIDARG);

            const SVGA3dCmdHeader *pHeader = (SVGA3dCmdHeader *)pu8Src;
            cbCmd = sizeof(SVGA3dCmdHeader) + pHeader->size;
            AssertBreakStmt(cbCmd % sizeof(uint32_t) == 0, hr = E_INVALIDARG);
            AssertBreakStmt(cbSrcLeft >= cbCmd, hr = E_INVALIDARG);
        }
        else
        {
            /* It is not expected that any of common SVGA commands will be in the command buffer
             * because the SVGA gallium driver does not use them.
             */
            AssertBreakStmt(0, hr = E_INVALIDARG);
        }

        if (u32Length + cbCmd > cbAvail)
        {
            if (u32Length == 0)
            {
               /* No commands fit into the buffer. */
               hr = E_FAIL;
            }
            break;
        }

        pu8Src += cbCmd;
        u32Length += cbCmd;
    }

    *pu32Length = u32Length;
    return hr;
}

static HRESULT
vboxDdiRender(GaWddmCallbacks *pWddmCallbacks,
              GAWDDMCONTEXTINFO *pContextInfo, uint32_t u32FenceHandle, void *pvCommands, uint32_t cbCommands)
{
    HRESULT hr = S_OK;
    D3DDDICB_RENDER ddiRender;
    uint32_t cbLeft;
    const uint8_t *pu8Src;

    LogRel(("vboxDdiRender: cbCommands = %d, u32FenceHandle = %d\n", cbCommands, u32FenceHandle));

    cbLeft = cbCommands;
    pu8Src = (uint8_t *)pvCommands;
    /* Even when cbCommands is 0, submit the fence. The following code deals with this. */
    do
    {
        /* Actually available space. */
        const uint32_t cbAvail = pContextInfo->CommandBufferSize;
        AssertBreakStmt(cbAvail > sizeof(u32FenceHandle), hr = E_FAIL);

        /* How many bytes of command data still to copy. */
        uint32_t cbCommandChunk = cbLeft;

        /* How many bytes still to copy. */
        uint32_t cbToCopy = sizeof(u32FenceHandle) + cbCommandChunk;

        /* Copy the buffer identifier. */
        if (cbToCopy <= cbAvail)
        {
            /* Command buffer is big enough. */
            *(uint32_t *)pContextInfo->pCommandBuffer = u32FenceHandle;
        }
        else
        {
            /* Split. Write zero as buffer identifier. */
            *(uint32_t *)pContextInfo->pCommandBuffer = 0;

            /* Get how much commands data will fit in the buffer. */
            hr = vboxCalcCommandLength(pu8Src, cbCommandChunk, cbAvail - sizeof(u32FenceHandle), &cbCommandChunk);
            AssertBreak(SUCCEEDED(hr));

            cbToCopy = sizeof(u32FenceHandle) + cbCommandChunk;
        }

        if (cbCommandChunk)
        {
            /* Copy the command data. */
            memcpy((uint8_t *)pContextInfo->pCommandBuffer + sizeof(u32FenceHandle), pu8Src, cbCommandChunk);
        }

        /* Advance the command position. */
        pu8Src += cbCommandChunk;
        cbLeft -= cbCommandChunk;

        memset(&ddiRender, 0, sizeof(ddiRender));
        ddiRender.CommandLength = cbToCopy;
        // ddiRender.CommandOffset = 0;
        // ddiRender.NumAllocations = 0;
        // ddiRender.NumPatchLocations = 0;
        ddiRender.hContext = pContextInfo->hContext;

        hr = pWddmCallbacks->DeviceCallbacks.pfnRenderCb(pWddmCallbacks->hDevice, &ddiRender);
        AssertBreak(SUCCEEDED(hr));

        pContextInfo->pCommandBuffer = ddiRender.pNewCommandBuffer;
        pContextInfo->CommandBufferSize = ddiRender.NewCommandBufferSize;
    } while (cbLeft);

    return hr;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvWddm::gaEnvWddmRender(void *pvEnv,
                              uint32_t u32Cid, void *pvCommands, uint32_t cbCommands,
                              GAFENCEQUERY *pFenceQuery)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    HRESULT hr = S_OK;
    uint32_t u32FenceHandle;
    GAWDDMCONTEXTINFO *pContextInfo = (GAWDDMCONTEXTINFO *)RTAvlU32Get(&pThis->mContextTree, u32Cid);
    if (!pContextInfo)
        return -1;

    u32FenceHandle = 0;
    if (pFenceQuery)
    {
        hr = vboxDdiFenceCreate(&pThis->mWddmCallbacks, pContextInfo, &u32FenceHandle);
    }

    if (SUCCEEDED(hr))
    {
        hr = vboxDdiRender(&pThis->mWddmCallbacks, pContextInfo, u32FenceHandle, pvCommands, cbCommands);
        if (SUCCEEDED(hr))
        {
            if (pFenceQuery)
            {
                HRESULT hr2 = vboxDdiFenceQuery(&pThis->mWddmCallbacks, u32FenceHandle, pFenceQuery);
                if (hr2 != S_OK)
                {
                    pFenceQuery->u32FenceStatus = GA_FENCE_STATUS_NULL;
                }
            }
        }
    }
    return SUCCEEDED(hr)? 0: 1;
}

static HRESULT
vboxDdiRegionCreate(GaWddmCallbacks *pWddmCallbacks,
                    uint32_t u32RegionSize,
                    uint32_t *pu32GmrId,
                    void **ppvMap)
{
    HRESULT                   hr;
    D3DDDICB_ESCAPE           ddiEscape;
    VBOXDISPIFESCAPE_GAREGION data;

    memset(&data, 0, sizeof(data));
    data.EscapeHdr.escapeCode  = VBOXESC_GAREGION;
    // data.EscapeHdr.cmdSpecific = 0;
    data.u32Command               = GA_REGION_CMD_CREATE;
    data.u32NumPages              = (u32RegionSize + PAGE_SIZE - 1) / PAGE_SIZE;
    // data.u32GmrId              = 0;
    // data.u64UserAddress        = 0;

    ddiEscape.hDevice               = pWddmCallbacks->hDevice;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.pPrivateDriverData    = &data;
    ddiEscape.PrivateDriverDataSize = sizeof(data);
    ddiEscape.hContext              = 0;

    hr = pWddmCallbacks->DeviceCallbacks.pfnEscapeCb(pWddmCallbacks->hAdapter, &ddiEscape);
    if (SUCCEEDED(hr))
    {
        *pu32GmrId = data.u32GmrId;
        *ppvMap = (void *)(uintptr_t)data.u64UserAddress;
    }
    return hr;
}

/* static */ DECLCALLBACK(int)
GaDrvEnvWddm::gaEnvWddmRegionCreate(void *pvEnv,
                                    uint32_t u32RegionSize,
                                    uint32_t *pu32GmrId,
                                    void **ppvMap)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    int ret;

    if (pThis->mWddmCallbacks.hDevice)
    {
        /* That is a real device */
        HRESULT hr = vboxDdiRegionCreate(&pThis->mWddmCallbacks, u32RegionSize, pu32GmrId, ppvMap);
        ret = SUCCEEDED(hr)? 0: -1;
    }
    else
    {
        /* That is a fake device, created when WDDM adapter is initialized. */
        *ppvMap = RTMemAlloc(u32RegionSize);
        if (*ppvMap != NULL)
        {
            *pu32GmrId = 0;
            ret = 0;
        }
        else
            ret = -1;
    }

    return ret;
}

static HRESULT
vboxDdiRegionDestroy(GaWddmCallbacks *pWddmCallbacks,
                     uint32_t u32GmrId)
{
    HRESULT                   hr;
    D3DDDICB_ESCAPE           ddiEscape;
    VBOXDISPIFESCAPE_GAREGION data;

    memset(&data, 0, sizeof(data));
    data.EscapeHdr.escapeCode  = VBOXESC_GAREGION;
    // data.EscapeHdr.cmdSpecific = 0;
    data.u32Command               = GA_REGION_CMD_DESTROY;
    // data.u32NumPages           = 0;
    data.u32GmrId                 = u32GmrId;
    // data.u64UserAddress        = 0;

    ddiEscape.hDevice               = 0;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.pPrivateDriverData    = &data;
    ddiEscape.PrivateDriverDataSize = sizeof(data);
    ddiEscape.hContext              = 0;

    hr = pWddmCallbacks->DeviceCallbacks.pfnEscapeCb(pWddmCallbacks->hAdapter, &ddiEscape);
    return hr;
}

/* static */ DECLCALLBACK(void)
GaDrvEnvWddm::gaEnvWddmRegionDestroy(void *pvEnv,
                                     uint32_t u32GmrId,
                                     void *pvMap)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    if (pThis->mWddmCallbacks.hDevice)
    {
        vboxDdiRegionDestroy(&pThis->mWddmCallbacks, u32GmrId);
    }
    else
    {
        RTMemFree(pvMap);
    }
}


/* static */ DECLCALLBACK(int)
GaDrvEnvWddm::gaEnvWddmGBSurfaceDefine(void *pvEnv,
                                       SVGAGBSURFCREATE *pCreateParms)
{
    GaDrvEnvWddm *pThis = (GaDrvEnvWddm *)pvEnv;

    HRESULT                           hr;
    D3DDDICB_ESCAPE                   ddiEscape;
    VBOXDISPIFESCAPE_SVGAGBSURFACEDEFINE data;
    data.EscapeHdr.escapeCode     = VBOXESC_SVGAGBSURFACEDEFINE;
    data.EscapeHdr.u32CmdSpecific = 0;
    data.CreateParms              = *pCreateParms;

    ddiEscape.hDevice               = 0; // pThis->mWddmCallbacks.hDevice;
    ddiEscape.Flags.Value           = 0;
    ddiEscape.Flags.HardwareAccess  = 1; // Required, otherwise graphics corruption can happen. No idea why.
                                         // Eventually we probably have to create allocations for surfaces,
                                         // as a WDDM driver should do. Then the Escape hack will be removed.
    ddiEscape.pPrivateDriverData    = &data;
    ddiEscape.PrivateDriverDataSize = sizeof(data);
    ddiEscape.hContext              = 0;

    hr = pThis->mWddmCallbacks.DeviceCallbacks.pfnEscapeCb(pThis->mWddmCallbacks.hAdapter, &ddiEscape);
    if (FAILED(hr))
        return -1;

    pCreateParms->gmrid = data.CreateParms.gmrid;
    pCreateParms->cbGB = data.CreateParms.cbGB;
    pCreateParms->u64UserAddress = data.CreateParms.u64UserAddress;
    pCreateParms->u32Sid = data.CreateParms.u32Sid;
    return 0;
}


GaDrvEnvWddm::GaDrvEnvWddm()
    :
    mContextTree(0)
{
    RT_ZERO(mWddmCallbacks);
    RT_ZERO(mHWInfo);
    RT_ZERO(mEnv);
}

GaDrvEnvWddm::~GaDrvEnvWddm()
{
}

HRESULT GaDrvEnvWddm::Init(HANDLE hAdapter,
                           HANDLE hDevice,
                           const D3DDDI_DEVICECALLBACKS *pDeviceCallbacks,
                           const VBOXGAHWINFO *pHWInfo)
{
    mWddmCallbacks.hAdapter = hAdapter;
    mWddmCallbacks.hDevice  = hDevice;
    if (pDeviceCallbacks)
    {
        mWddmCallbacks.DeviceCallbacks = *pDeviceCallbacks;
    }
    mHWInfo                 = *pHWInfo;
    return VINF_SUCCESS;
}

const WDDMGalliumDriverEnv *GaDrvEnvWddm::Env()
{
    if (mEnv.cb == 0)
    {
        mEnv.cb                = sizeof(WDDMGalliumDriverEnv);
        mEnv.pvEnv             = this;
        mEnv.pfnContextCreate  = gaEnvWddmContextCreate;
        mEnv.pfnContextDestroy = gaEnvWddmContextDestroy;
        mEnv.pfnSurfaceDefine  = gaEnvWddmSurfaceDefine;
        mEnv.pfnSurfaceDestroy = gaEnvWddmSurfaceDestroy;
        mEnv.pfnRender         = gaEnvWddmRender;
        mEnv.pfnFenceUnref     = gaEnvWddmFenceUnref;
        mEnv.pfnFenceQuery     = gaEnvWddmFenceQuery;
        mEnv.pfnFenceWait      = gaEnvWddmFenceWait;
        mEnv.pfnRegionCreate   = gaEnvWddmRegionCreate;
        mEnv.pfnRegionDestroy  = gaEnvWddmRegionDestroy;
        mEnv.pHWInfo           = &mHWInfo;
        /* VGPU10 */
        mEnv.pfnGBSurfaceDefine  = gaEnvWddmGBSurfaceDefine;
    }

    return &mEnv;
}
