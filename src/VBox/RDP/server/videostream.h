/* $Id: videostream.h 114912 2026-08-10 12:03:20Z vitali.pelenjow@oracle.com $ */
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

#ifndef VRDP_INCLUDED_SRC_videostream_h
#define VRDP_INCLUDED_SRC_videostream_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "vrdpdefs.h"
#include "regions.h"
#include "utils.h"

#include "videoencoder.h"
#include "vhstat.h"
#include "bmpscale.h"

#include <iprt/list.h>

class VRDPServer;

typedef struct VIDEOSTREAMCALLBACKS
{
    DECLR3CALLBACKMEMBER(bool, pfnVideoSourceStreamStart, (void *pvCallback, uint32_t u32SourceStreamId, const RGNRECT *prect, int64_t timeStart));
    DECLR3CALLBACKMEMBER(void, pfnVideoSourceStreamStop, (void *pvCallback, uint32_t u32SourceStreamId, const RGNRECT *prect));
    DECLR3CALLBACKMEMBER(bool, pfnIsValidRect, (void *pvCallback, const RGNRECT *prect));
} VIDEOSTREAMCALLBACKS;

typedef struct VDCONTEXT VDCONTEXT;
typedef struct VHCONTEXT VHCONTEXT;
typedef struct VHOUTPUTFRAME VHOUTPUTFRAME;

#define VH_MS_TIME_FROM_TIMELINE(t) ((t) / 10000)
#define VH_TIMELINE_TIME_FROM_MS(ms) ((int64_t)(ms) * 1000 * 10)
#define VH_TIMELINE_TIME_FROM_MCS(mcs) ((int64_t)(mcs) * 10)

enum StatusVideoStream
{
    /* The stream has been detected and negotiations with clients about video playback should be started. */
    StatusVideoStream_Start,

    /* Stream ready for data transfer. */
    StatusVideoStream_Ready,

    /* Stream should be stopped. */
    StatusVideoStream_Stop,

    /* The stream does not work. */
    StatusVideoStream_Error
};

typedef struct VHSTREAMCALLBACKDATA
{
    void *pvContext;
    DECLR3CALLBACKMEMBER(int, VHStreamNotify,(void *pvContext,
                                              uint32_t u32Id,
                                              void *pvData,
                                              uint32_t cbData));
} VHSTREAMCALLBACKDATA;

/* Public stream parameters. */
typedef struct VHSTREAMPARMS
{
    VHCONTEXT *pCtx; /* Backlink. */
    VHSTATSTREAM *pStatCtx; /** @todo should be referenced. */
    uint32_t u32VideoStreamId;
    unsigned uScreenId;
    RTUUID uuid;
    RGNRECT rectOriginal; /* Rectangle in the shadow buffer coordinates. */
    RGNRECT rectScaled;   /* { 0, 0, wScaled, hScaled } possibly downscaled image. */
    uint32_t cVisibleRects; /* Number of visible region rects. */
    RTRECT *paVisibleRects; /* The visible region. */
    VHSTREAMCALLBACKDATA callbackData;
    int64_t i64LipSyncDelay;
    bool fDirect;
} VHSTREAMPARMS;

typedef struct VHOUTPUTFRAME
{
    VRDPPKT pkt;
    RTLISTNODE NodeOutputFrame;
    int64_t i64Timestamp;
    uint8_t *pu8JPEG;
    uint32_t cbJPEG;
    int64_t i64SampleStartTime; /* When this sample has started in the stream. */
    int64_t i64SampleEndTime;   /* When this sample has ended in the stream. */
} VHOUTPUTFRAME;

typedef struct VHEVENTSIZELIMIT
{
    uint32_t u32ClientMaxArea;
    uint16_t u16ClientMinArea;
    uint16_t u16ClientMinWidth;
    uint16_t u16ClientMinHeight;
} VHEVENTSIZELIMIT;

/* A video stream handler holds video frames and maintains streams.
 *
 */

typedef void FNPOSTVIDEOEVENT(void *pvContext, int iCode, const void *pvData, uint32_t cbData);
typedef FNPOSTVIDEOEVENT *PFNPOSTVIDEOEVENT;

typedef struct VHCONTEXT
{
    int32_t volatile cClients;
    VRDPLock *pLock;              /* For source and output streams changes. @todo review locking */
    int64_t i64VideoPeriodMinMS;

    RTLISTANCHOR listStreams; /* VHSTREAMDATA structures */

    RTLISTANCHOR ListOutputStreams; /**< VHOUTPUTSTREAM structures */

    PFNPOSTVIDEOEVENT pfnPostVideoEvent;
    void *pvPostVideoEvent;

    uint32_t u32VideoRectSquareMax; /* If w*h of a stream is greater, then downscale. */
    uint32_t u32VideoRectSquareDownscale; /* The video will be downscaled to this square. */

    uint32_t u32VideoRectSquareMin; /* If w*h of a stream is lesser, then do not redirect. */
    uint32_t u32VideoRectWidthMin;  /* Do not redirect, if width is less than this. */
    uint32_t u32VideoRectHeightMin; /* Do not redirect, if height is less than this. */

    VRDPServer *pServer;

    uint32_t u32RequestedVideoQuality;

    int iDownscaleProtection;

    VRDPLock *pLockSFB; /* The lock is shared by all streams source frame buffers. */
} VHCONTEXT;

int videoHandlerCreate(VHCONTEXT **ppCtx, PFNPOSTVIDEOEVENT pfnPostVideoEvent, void *pvPostVideoEvent, VRDPServer *pVRDPServer);
void videoHandlerDelete(VHCONTEXT *pCtx);

void videoHandlerNotifyClientConnect(VHCONTEXT *pCtx);
void videoHandlerNotifyClientDisconnect(VHCONTEXT *pCtx);

typedef DECLCALLBACKTYPE(int, FNHANDLERBITMAPREAD,(void *pvCtx, uint8_t *pu8Buffer, uint32_t cbBuffer));
typedef FNHANDLERBITMAPREAD *PFNHANDLERBITMAPREAD;

bool videoHandlerSourceStreamStart(VHCONTEXT *pCtx,
                                   uint32_t u32SourceStreamId,
                                   const RGNRECT *prect,
                                   int64_t timeStart,
                                   unsigned uScreenId,
                                   bool fDirect,
                                   VHSTREAMCALLBACKDATA *pCallbackData);

bool videoHandlerSourceFrame(VHCONTEXT *pCtx,
                             uint32_t u32SourceStreamId,
                             int64_t i64Timestamp,
                             const RGNRECT *pRect,
                             PFNHANDLERBITMAPREAD pfnBitmapRead,
                             void *pvBitmapRead);

void videoHandlerSourceStreamStop(VHCONTEXT *pCtx,
                                  uint32_t u32SourceStreamId);

/* Direct source stream, which is not autodetected but directly supplied by the caller.
 * Such streams supports visble region, do not use covers and do not use delay.
 */
bool videoHandlerDirectStreamStart(uint32_t *pu32DirectStreamId,
                                   const RGNRECT *prect,
                                   unsigned uScreenId,
                                   VHSTREAMCALLBACKDATA *pCallbackData);

void videoHandlerDirectFrame(uint32_t u32DirectStreamId,
                             const RGNRECT *pRect,
                             PFNHANDLERBITMAPREAD pfnBitmapRead,
                             void *pvBitmapRead);

void videoHandlerDirectStreamStop(uint32_t u32DirectStreamId);

void videoHandlerDirectRegion(uint32_t u32DirectStreamId,
                              uint32_t cRects,
                              const RTRECT *paRects);
int videoHandlerDirectGeometry(uint32_t u32DirectStreamId,
                               const RTRECT *pRect);

void videoHandlerResetOutput(VHCONTEXT *pCtx, VRDPServer *pServer);

void videoHandlerInsertLatestFrame(const VHSTREAMPARMS *pStream, RTLISTANCHOR *pListFramesToSend);


/* A video detector context holds information about a rectangular screen.
 *
 */
int videoDetectorContextCreate(VDCONTEXT **ppCtx, VIDEOSTREAMCALLBACKS *pCallbacks, void *pvCallback, VRDPServer *pServer);
void videoDetectorContextDelete(VDCONTEXT *pCtx);

void videoDetectorReset(VDCONTEXT *pCtx, bool fStopOutputStreams);

void videoDetectorOrderUpdate(VDCONTEXT *pCtx, const RGNRECT *prectOrder);
bool videoDetectorBitmapUpdate(VDCONTEXT *pCtx, RGNRECT *prectUpdate, RGNRECT *prectVideo, uint64_t timeNow, uint32_t *pu32VideoStreamId);

#define VD_CMD_NOP 0
#define VD_CMD_DISABLE 1
#define VD_CMD_ENABLE 2
#define VD_CMD_RESET 3
void videoDetectorStoreCommand(VDCONTEXT *pCtx, uint8_t u8VDCmd);

/* Periodical stream maintenance. */
void videoDetectorUpdateComplete(VDCONTEXT *pCtx);

#endif /* !VRDP_INCLUDED_SRC_videostream_h */
