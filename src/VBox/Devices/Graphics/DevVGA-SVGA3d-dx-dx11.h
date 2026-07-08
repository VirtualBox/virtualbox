/* $Id: DevVGA-SVGA3d-dx-dx11.h 114253 2026-06-03 16:55:48Z vitali.pelenjow@oracle.com $ */
/** @file
 * DevSVGA - Internal DX11 backend utilities.
 */

/*
 * Copyright (C) 2020-2026 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_dx_dx11_h
#define VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_dx_dx11_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdmdev.h>

#include "DevVGA-SVGA3d-internal.h"

/* d3d11_1.h has a structure field named 'Status' but Status is defined as int on Linux host */
#if defined(Status)
# undef Status
#endif
#ifndef RT_OS_WINDOWS
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <d3d11_1.h>
#ifndef RT_OS_WINDOWS
# pragma GCC diagnostic pop
#endif

int dxHwOutputTargetCreate(VMSVGAOUTPUTTARGET *pOutputTarget,
                           ID3D11Device1 *pDevice);
void dxHwOutputTargetDestroy(VMSVGAOUTPUTTARGET *pOutputTarget);
int dxHwOutputTargetConvert(VMSVGAOUTPUTTARGET *pOutputTarget,
                            ID3D11DeviceContext1 *pDeviceContext,
                            ID3D11ShaderResourceView *pSrcSrv,
                            UINT srcW, UINT srcH,
                            SVGASignedRect const &dirtyRect);
int dxHwOutputTargetReadback(VMSVGAOUTPUTTARGET *pOutputTarget,
                             ID3D11DeviceContext1 *pDeviceContext,
                             SVGASignedRect const &updateRect,
                             bool *pfChanged);

/* A buffer which accumulates data. If the buffer is full (offFree == cbBuffer),
 * then a query is issued. The buffer can be reused when the query finishes.
 */
typedef struct DXUPLOADBUFFER
{
    RTLISTNODE    nodeUploadBuffer;
    ID3D11Buffer *pUploadBuffer;
    uint32_t      offFree;
    uint32_t      cbBuffer;
    ID3D11Query  *pUploadQuery;
} DXUPLOADBUFFER;

int dxUploadBufferCreate(ID3D11Device1 *pDevice, UINT cbBuffer, UINT BindFlags, DXUPLOADBUFFER **ppUploadBuffer);
void dxUploadBufferDestroy(DXUPLOADBUFFER *pUploadBuffer);
void dxUploadBufferEndQuery(DXUPLOADBUFFER *pUploadBuffer, ID3D11DeviceContext1 *pImmediateContext);
bool dxUploadBufferIsFinished(DXUPLOADBUFFER *pUploadBuffer, ID3D11DeviceContext1 *pImmediateContext);

typedef struct DXUPLOADBUFFERMANAGER
{
    /* DXUPLOADBUFFER */
    RTLISTANCHOR  listUploadBuffers;
    RTLISTANCHOR  listFullUploadBuffers;
    RTLISTANCHOR  listInFlightUploadBuffers;

    UINT          cbMaxData;
    UINT          cbBuffer;
    UINT          BindFlags;

#ifdef DEBUG
    uint32_t cUploadBuffers;
#endif
} DXUPLOADBUFFERMANAGER;

DXUPLOADBUFFER *dxUploadBufferManagerGetBuffer(DXUPLOADBUFFERMANAGER *pMgr, ID3D11Device1 *pDevice,
                                               ID3D11DeviceContext1 *pImmediateContext, uint32_t cbData);
void dxUploadBufferManagerProcessFull(DXUPLOADBUFFERMANAGER *pMgr, ID3D11DeviceContext1 *pImmediateContext);
void dxUploadBufferManagerInit(DXUPLOADBUFFERMANAGER *pMgr, UINT cbMaxData, UINT cbBuffer, UINT BindFlags);
void dxUploadBufferManagerUninit(DXUPLOADBUFFERMANAGER *pMgr);

#endif /* !VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA3d_dx_dx11_h */
