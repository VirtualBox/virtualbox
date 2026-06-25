/* $Id: DevVGA-SVGA3d-dx-dx11-utils.cpp 114523 2026-06-25 10:15:20Z vitali.pelenjow@oracle.com $ */
/** @file
 * DevSVGA - D3D11 backend utilities
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_DEV_VMSVGA

#include <VBox/log.h>

#include <iprt/mem.h>

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif

#include "DevVGA-SVGA3d-dx-dx11.h"


int dxUploadBufferCreate(ID3D11Device1 *pDevice, UINT cbBuffer, UINT BindFlags, DXUPLOADBUFFER **ppUploadBuffer)
{
    *ppUploadBuffer = NULL;

    DXUPLOADBUFFER *pUploadBuffer = (DXUPLOADBUFFER *)RTMemAllocZ(sizeof(DXUPLOADBUFFER));
    AssertReturn(pUploadBuffer, VERR_NO_MEMORY);

    D3D11_BUFFER_DESC bd;
    RT_ZERO(bd);
    bd.ByteWidth           = cbBuffer;
    bd.Usage               = D3D11_USAGE_DYNAMIC;
    bd.BindFlags           = BindFlags;
    bd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
    //bd.MiscFlags           = 0;
    //bd.StructureByteStride = 0;

    HRESULT hr = pDevice->CreateBuffer(&bd, NULL, &pUploadBuffer->pUploadBuffer);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);

    D3D11_QUERY_DESC qd;
    RT_ZERO(qd);
    qd.Query = D3D11_QUERY_EVENT;

    hr = pDevice->CreateQuery(&qd, &pUploadBuffer->pUploadQuery);
    AssertReturn(SUCCEEDED(hr), VERR_NO_MEMORY);

    //RT_ZERO(pUploadBuffer->nodeUploadBuffer);
    //pUploadBuffer->offFree = 0;
    pUploadBuffer->cbBuffer = bd.ByteWidth;

    *ppUploadBuffer = pUploadBuffer;
    return VINF_SUCCESS;
}


void dxUploadBufferDestroy(DXUPLOADBUFFER *pUploadBuffer)
{
    if (pUploadBuffer)
    {
        D3D_RELEASE(pUploadBuffer->pUploadQuery);
        D3D_RELEASE(pUploadBuffer->pUploadBuffer);
        RTMemFree(pUploadBuffer);
    }
}


void dxUploadBufferEndQuery(DXUPLOADBUFFER *pUploadBuffer, ID3D11DeviceContext1 *pImmediateContext)
{
    pImmediateContext->End(pUploadBuffer->pUploadQuery);
}


bool dxUploadBufferIsFinished(DXUPLOADBUFFER *pUploadBuffer, ID3D11DeviceContext1 *pImmediateContext)
{
    BOOL queryData;
    HRESULT hr = pImmediateContext->GetData(pUploadBuffer->pUploadQuery, &queryData, sizeof(queryData),
                                            D3D11_ASYNC_GETDATA_DONOTFLUSH);
    AssertReturn(SUCCEEDED(hr), false);
    return hr == S_OK;
}


DXUPLOADBUFFER *dxUploadBufferManagerGetBuffer(DXUPLOADBUFFERMANAGER *pMgr, ID3D11Device1 *pDevice,
                                               ID3D11DeviceContext1 *pImmediateContext, uint32_t cbData)
{
    AssertReturn(cbData <= pMgr->cbMaxData, NULL);

    /* Shortcut for the usual case when there is space in the current buffer. */
    DXUPLOADBUFFER *pUploadBuffer = RTListGetFirst(&pMgr->listUploadBuffers, DXUPLOADBUFFER, nodeUploadBuffer);
    if (RT_LIKELY(pUploadBuffer && cbData <= pUploadBuffer->cbBuffer - pUploadBuffer->offFree))
        return pUploadBuffer;

    if (pUploadBuffer)
    {
        /* The current upload buffer is full.
         * Mark it for a query submission after a Draw which last uses it.
         */
        RTListNodeRemove(&pUploadBuffer->nodeUploadBuffer);
        RTListAppend(&pMgr->listFullUploadBuffers, &pUploadBuffer->nodeUploadBuffer);

        /* Get the next buffer in the list of available buffers. */
        pUploadBuffer = RTListGetFirst(&pMgr->listUploadBuffers, DXUPLOADBUFFER, nodeUploadBuffer);
        if (pUploadBuffer)
        {
            /* The new buffer must be empty and have enough space. */
            Assert(pUploadBuffer->offFree == 0);
            return pUploadBuffer;
        }
    }

    /* Check if any "in flight" buffers are free for use. */
    DXUPLOADBUFFER *pIter, *pNext;
    RTListForEachSafe(&pMgr->listInFlightUploadBuffers, pIter, pNext, DXUPLOADBUFFER, nodeUploadBuffer)
    {
        if (dxUploadBufferIsFinished(pIter, pImmediateContext))
        {
            RTListNodeRemove(&pIter->nodeUploadBuffer);
            pIter->offFree = 0;

            RTListAppend(&pMgr->listUploadBuffers, &pIter->nodeUploadBuffer);
            return pIter;
        }
    }

    /* Allocate a new buffer.  */
    int rc = dxUploadBufferCreate(pDevice, pMgr->cbBuffer, pMgr->BindFlags, &pUploadBuffer);
    AssertRCReturn(rc, NULL);

#ifdef DEBUG
    ++pMgr->cUploadBuffers;
#endif
    RTListAppend(&pMgr->listUploadBuffers, &pUploadBuffer->nodeUploadBuffer);
    return pUploadBuffer;
}


void dxUploadBufferManagerProcessFull(DXUPLOADBUFFERMANAGER *pMgr, ID3D11DeviceContext1 *pImmediateContext)
{
    DXUPLOADBUFFER *pIter, *pNext;

    /* Submit queries for full buffers. */
    RTListForEachSafe(&pMgr->listFullUploadBuffers, pIter, pNext, DXUPLOADBUFFER, nodeUploadBuffer)
    {
        RTListNodeRemove(&pIter->nodeUploadBuffer);
        dxUploadBufferEndQuery(pIter, pImmediateContext);
        RTListAppend(&pMgr->listInFlightUploadBuffers, &pIter->nodeUploadBuffer);
    }
}


void dxUploadBufferManagerInit(DXUPLOADBUFFERMANAGER *pMgr, UINT cbMaxData, UINT cbBuffer, UINT BindFlags)
{
    RT_ZERO(*pMgr);
    RTListInit(&pMgr->listUploadBuffers);
    RTListInit(&pMgr->listFullUploadBuffers);
    RTListInit(&pMgr->listInFlightUploadBuffers);
    pMgr->cbMaxData = cbMaxData;
    pMgr->cbBuffer = cbBuffer;
    pMgr->BindFlags = BindFlags;
}


void dxUploadBufferManagerUninit(DXUPLOADBUFFERMANAGER *pMgr)
{
    DXUPLOADBUFFER *pIter, *pNext;
    RTListForEachSafe(&pMgr->listUploadBuffers, pIter, pNext, DXUPLOADBUFFER, nodeUploadBuffer)
    {
        dxUploadBufferDestroy(pIter);
    }
    RTListForEachSafe(&pMgr->listFullUploadBuffers, pIter, pNext, DXUPLOADBUFFER, nodeUploadBuffer)
    {
        dxUploadBufferDestroy(pIter);
    }
    RTListForEachSafe(&pMgr->listInFlightUploadBuffers, pIter, pNext, DXUPLOADBUFFER, nodeUploadBuffer)
    {
        dxUploadBufferDestroy(pIter);
    }
#ifdef DEBUG
    LogFunc(("cUploadBuffers %u\n", pMgr->cUploadBuffers));
#endif
    RT_ZERO(*pMgr);
}
