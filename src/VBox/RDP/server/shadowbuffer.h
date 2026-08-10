/* $Id: shadowbuffer.h 114912 2026-08-10 12:03:20Z vitali.pelenjow@oracle.com $ */
/** @file
 * VBox Remote Desktop Protocol.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

#ifndef VRDP_INCLUDED_SRC_shadowbuffer_h
#define VRDP_INCLUDED_SRC_shadowbuffer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "vrdpdefs.h"
#include "regions.h"
#include <VBox/RemoteDesktop/VRDEOrders.h>

#include "videostream.h"

typedef uint32_t VRDPSBKEY;

#define VRDP_TRANSFORM_ROTATE_0   0
#define VRDP_TRANSFORM_ROTATE_90  1
#define VRDP_TRANSFORM_ROTATE_180 2
#define VRDP_TRANSFORM_ROTATE_270 3

typedef struct _VRDPBITSRECT
{
    const uint8_t *pu8Bits;
    RGNRECT        rect;
    unsigned       cBitsPerPixel;
    unsigned       cbPixel;
    unsigned       cbLine;
} VRDPBITSRECT;

struct _VRDPSHADOWBUFFER;
typedef struct _VRDPSHADOWBUFFER VRDPSHADOWBUFFER;
typedef VRDPSHADOWBUFFER *VRDPSHADOWBUFFERHANDLE;

class VRDPServer;
int shadowBufferInit (VRDPServer *pServer, unsigned cScreens);

void shadowBufferUninit (void);

bool shadowBufferVerifyScreenId (unsigned uScreenId);
unsigned shadowBufferQueryScreenCount (void);

void shadowBufferMapMouse (unsigned uScreenId, int *px, int *py);

void shadowBufferResize (unsigned uScreenId, VRDPBITSRECT *pBitsRect, unsigned uTransform);
void shadowBufferBitmapUpdate (unsigned uScreenId, int32_t x, int32_t y, uint32_t w, uint32_t h);
void shadowBufferBitmapUpdateEx (unsigned uScreenId,
                                 int32_t x, int32_t y, uint32_t w, uint32_t h,
                                 const uint8_t *pu8Bits,
                                 int32_t iDeltaLine,
                                 bool fVideoDetection);

void shadowBufferRedrawUpdate (unsigned uScreenId, const RGNRECT *pRectScreen, const RGNRECT *pRectClient);

void shadowBufferUpdateComplete(void);
void shadowBufferRegisterVideoHandler(VHCONTEXT *pVideoHandler);
void shadowBufferUnregisterVideoHandler(VHCONTEXT *pVideoHandler);
// @todo a simpler architecture
VHCONTEXT *shadowBufferGetVideoHandler(void);

typedef uint64_t SBHANDLE;
#define SB_HANDLE_NULL UINT64_C(0)

SBHANDLE shadowBufferCoverAdd(unsigned uScreenId, const RGNRECT *pRect);
void shadowBufferCoverRemove(unsigned uScreenId, SBHANDLE handle);
void shadowBufferCoverResetAll(void);

void shadowBufferVideoDetectorCmd(uint8_t u8VDCmd);

void shadowBufferDestroyBuffers (void);

void shadowBufferOrder (unsigned uScreenId, void *pdata, uint32_t cbdata);

void shadowBufferQueryRect (unsigned uScreenId, RGNRECT *prect);

bool shadowBufferAreOrderCoordsValid(unsigned uScreenId, int32_t x, int32_t y, uint32_t w, uint32_t h);
void shadowBufferTransformRect (unsigned uScreenId, RGNRECT *prect);
void shadowBufferTransformRectGeneric (unsigned uScreenId, RGNRECT *prect, unsigned w, unsigned h);
void shadowBufferTransformWidthHeight(unsigned uScreenId, unsigned *pw, unsigned *ph);
void shadowBufferTransformPoint (unsigned uScreenId, int *px, int *py);
void shadowBufferTransformPointToFB (unsigned uScreenId, int *px, int *py);

bool shadowBufferTransformDataBits (unsigned uScreenId, VRDEDATABITS *pTransBitsHdr, const uint8_t **ppu8TransBits, const VRDEDATABITS *pBitsHdr, const uint8_t *pu8Bits);
void shadowBufferFreeTransformDataBits (unsigned uScreenId, VRDEDATABITS *pTransBitsHdr, const uint8_t *pu8TransBits, const VRDEDATABITS *pBitsHdr, const uint8_t *pu8Bits);

#define VRDP_SB_ACT_NOP    (0)
#define VRDP_SB_ACT_REGION (1)
#define VRDP_SB_ACT_ORDER  (2)
#define VRDP_SB_ACT_RESIZE (3)

typedef struct _VrdpSbActDirtyRegion
{
    REGION *prgn;
} VrdpSbActDirtyRegion;

typedef struct _VrdpSbActOrder
{
    void *pvOrder;
    int32_t i32Op;
    RGNRECT rectAffected;
    uint32_t cbOrder;
} VrdpSbActOrder;

typedef struct _VrdpSbAct
{
    int code;

    void *pvContext;

    unsigned uScreenId;

    union {
        VrdpSbActDirtyRegion  region;
        VrdpSbActOrder        order;
    } u;
} VrdpSbAct;

void shadowBufferGetAction (VrdpSbAct *pAction);
void shadowBufferCancelAction (VrdpSbAct *pAction);

VRDPSBKEY shadowBufferBeginEnumRgnRect (REGION *prgn);
const RGNRECT *shadowBufferQueryNextRgnRect (REGION *prgn);

#define VRDP_SB_NULL_ACCESS_KEY ((VRDPSBKEY)0)

VRDPSBKEY shadowBufferLock (VRDPSBKEY key, VRDPBITSRECT *pBitsRect, unsigned uScreenId, const RGNRECT *pRect);
void shadowBufferUnlock (unsigned uScreenId);

void shadowBufferSetAccessible(unsigned uScreenId, bool fAccessible);

#endif /* !VRDP_INCLUDED_SRC_shadowbuffer_h */
