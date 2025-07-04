/* $Id: RecordingCodec.cpp 110118 2025-07-04 12:03:20Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording codec wrapper.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

/* This code makes use of Vorbis (libvorbis):
 *
 * Copyright (c) 2002-2020 Xiph.org Foundation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Xiph.org Foundation nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_GROUP LOG_GROUP_RECORDING
#include "LoggingNew.h"

#include <VBox/com/string.h>
#include <VBox/err.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

#include "Recording.h"
#include "RecordingInternals.h"
#include "RecordingUtils.h"
#include "WebMWriter.h"

#include <math.h>

#include <iprt/formats/bmp.h>


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_LIBVPX
static int recordingCodecVPXEncodeWorker(PRECORDINGCODEC pCodec, vpx_image_t *pImage, uint64_t msTimestamp);
#endif


/*********************************************************************************************************************************
*   Generic inline functions                                                                                                     *
*********************************************************************************************************************************/

#ifdef VBOX_WITH_LIBVPX /* Currently only used by VPX. */
DECLINLINE(void) recordingCodecLock(PRECORDINGCODEC pCodec)
{
    int vrc2 = RTCritSectEnter(&pCodec->CritSect);
    AssertRC(vrc2);
}

DECLINLINE(void) recordingCodecUnlock(PRECORDINGCODEC pCodec)
{
    int vrc2 = RTCritSectLeave(&pCodec->CritSect);
    AssertRC(vrc2);
}
#endif


/*********************************************************************************************************************************
*   VPX (VP8 / VP9) codec                                                                                                        *
*********************************************************************************************************************************/

#ifdef VBOX_WITH_LIBVPX
/** Prototypes. */
static DECLCALLBACK(int) recordingCodecVPXScreenChange(PRECORDINGCODEC pCodec, PRECORDINGSURFACEINFO pInfo);

/**
 * Clears (zeros) the VPX planes.
 */
DECLINLINE(void) recordingCodecVPXClearPlanes(PRECORDINGCODEC pCodec)
{
    size_t const cbYPlane  = pCodec->Parms.u.Video.uWidth * pCodec->Parms.u.Video.uHeight;
    memset(pCodec->Video.VPX.RawImage.planes[VPX_PLANE_Y], 0, cbYPlane);
    size_t const cbUVPlane = (pCodec->Parms.u.Video.uWidth / 2) * (pCodec->Parms.u.Video.uHeight / 2);
    memset(pCodec->Video.VPX.RawImage.planes[VPX_PLANE_U], 128, cbUVPlane);
    memset(pCodec->Video.VPX.RawImage.planes[VPX_PLANE_V], 128, cbUVPlane);
}

/** @copydoc RECORDINGCODECOPS::pfnInit */
static DECLCALLBACK(int) recordingCodecVPXInit(PRECORDINGCODEC pCodec)
{
    const unsigned uBPP = 32;

    pCodec->Parms.csFrame = 0;
    pCodec->Parms.cbFrame = pCodec->Parms.u.Video.uWidth * pCodec->Parms.u.Video.uHeight * (uBPP / 8);
    pCodec->Parms.msFrame = 1; /* 1ms per frame. */

# ifdef VBOX_WITH_LIBVPX_VP9
    vpx_codec_iface_t *pCodecIface = vpx_codec_vp9_cx();
# else /* Default is using VP8. */
    vpx_codec_iface_t *pCodecIface = vpx_codec_vp8_cx();
# endif
    PRECORDINGCODECVPX pVPX = &pCodec->Video.VPX;

    vpx_codec_err_t rcv = vpx_codec_enc_config_default(pCodecIface, &pVPX->Cfg, 0 /* Reserved */);
    if (rcv != VPX_CODEC_OK)
    {
        LogRel(("Recording: Failed to get default config for VPX encoder: %s\n", vpx_codec_err_to_string(rcv)));
        return VERR_RECORDING_CODEC_INIT_FAILED;
    }

    /* Target bitrate in kilobits per second. */
    pVPX->Cfg.rc_target_bitrate = pCodec->Parms.uBitrate;
    /* Frame width. */
    pVPX->Cfg.g_w = pCodec->Parms.u.Video.uWidth;
    /* Frame height. */
    pVPX->Cfg.g_h = pCodec->Parms.u.Video.uHeight;
    /* ms per frame. */
    pVPX->Cfg.g_timebase.num = pCodec->Parms.msFrame;
    pVPX->Cfg.g_timebase.den = 1000;
    /* Disable multithreading. */
    pVPX->Cfg.g_threads      = 0;

    /* Initialize codec. */
    rcv = vpx_codec_enc_init(&pVPX->Ctx, pCodecIface, &pVPX->Cfg, 0 /* Flags */);
    if (rcv != VPX_CODEC_OK)
    {
        LogRel(("Recording: Failed to initialize VPX encoder: %s\n", vpx_codec_err_to_string(rcv)));
        return VERR_RECORDING_CODEC_INIT_FAILED;
    }

    if (!vpx_img_alloc(&pVPX->RawImage, VPX_IMG_FMT_I420,
                       pCodec->Parms.u.Video.uWidth, pCodec->Parms.u.Video.uHeight, 1))
    {
        LogRel(("Recording: Failed to allocate image %RU32x%RU32\n", pCodec->Parms.u.Video.uWidth, pCodec->Parms.u.Video.uHeight));
        return VERR_RECORDING_CODEC_INIT_FAILED;
    }

    /* Save a pointer to the Y (Luminance) plane. */
    pVPX->pu8YuvBuf = pVPX->RawImage.planes[VPX_PLANE_Y];

    /* Initialize front + back buffers. */
    RT_ZERO(pCodec->Video.VPX.Front);
    RT_ZERO(pCodec->Video.VPX.Back);

    pCodec->Video.VPX.pCursorShape = NULL;

    RECORDINGSURFACEINFO ScreenInfo;
    ScreenInfo.uWidth      = pCodec->Parms.u.Video.uWidth;
    ScreenInfo.uHeight     = pCodec->Parms.u.Video.uHeight;
    ScreenInfo.uBPP        = uBPP;
    ScreenInfo.enmPixelFmt = RECORDINGPIXELFMT_BRGA32;

    RT_ZERO(pCodec->Video.VPX.PosCursorOld);

    int vrc = recordingCodecVPXScreenChange(pCodec, &ScreenInfo);
    if (RT_FAILURE(vrc))
        LogRel(("Recording: Failed to initialize codec: %Rrc\n", vrc));

    return vrc;
}

/** @copydoc RECORDINGCODECOPS::pfnDestroy */
static DECLCALLBACK(int) recordingCodecVPXDestroy(PRECORDINGCODEC pCodec)
{
    PRECORDINGCODECVPX pVPX = &pCodec->Video.VPX;

    vpx_img_free(&pVPX->RawImage);
    pVPX->pu8YuvBuf = NULL; /* Was pointing to VPX.RawImage. */

    vpx_codec_err_t rcv = vpx_codec_destroy(&pVPX->Ctx);
    Assert(rcv == VPX_CODEC_OK); RT_NOREF(rcv);

    RecordingVideoFrameDestroy(&pCodec->Video.VPX.Front);
    RecordingVideoFrameDestroy(&pCodec->Video.VPX.Back);

    RecordingVideoFrameFree(pCodec->Video.VPX.pCursorShape);
    pCodec->Video.VPX.pCursorShape = NULL;

    return VINF_SUCCESS;
}

/** @copydoc RECORDINGCODECOPS::pfnFinalize */
static DECLCALLBACK(int) recordingCodecVPXFinalize(PRECORDINGCODEC pCodec)
{
    recordingCodecLock(pCodec);

    int vrc = recordingCodecVPXEncodeWorker(pCodec, NULL /* pImage */, pCodec->State.tsLastWrittenMs + 1);

    recordingCodecUnlock(pCodec);

    return vrc;
}

/** @copydoc RECORDINGCODECOPS::pfnParseOptions */
static DECLCALLBACK(int) recordingCodecVPXParseOptions(PRECORDINGCODEC pCodec, const com::Utf8Str &strOptions)
{
    size_t pos = 0;
    com::Utf8Str key, value;
    while ((pos = strOptions.parseKeyValue(key, value, pos)) != com::Utf8Str::npos)
    {
        if (key.compare("vc_quality", com::Utf8Str::CaseInsensitive) == 0)
        {
            const PRECORDINGCODECVPX pVPX = &pCodec->Video.VPX;

            if (value.compare("realtime", com::Utf8Str::CaseInsensitive) == 0)
                pVPX->uEncoderDeadline = VPX_DL_REALTIME;
            else if (value.compare("good", com::Utf8Str::CaseInsensitive) == 0)
            {
                AssertStmt(pCodec->Parms.u.Video.uFPS, pCodec->Parms.u.Video.uFPS = 25);
                pVPX->uEncoderDeadline = 1000000 / pCodec->Parms.u.Video.uFPS;
            }
            else if (value.compare("best", com::Utf8Str::CaseInsensitive) == 0)
                pVPX->uEncoderDeadline = VPX_DL_BEST_QUALITY;
            else
                pVPX->uEncoderDeadline = value.toUInt32();
        }
        else
            LogRel2(("Recording: Unknown option '%s' (value '%s'), skipping\n", key.c_str(), value.c_str()));
    } /* while */

    return VINF_SUCCESS;
}

/**
 * Worker for encoding the last composed image.
 *
 * @returns VBox status code.
 * @param   pCodec              Pointer to codec instance.
 * @param   pImage              VPX image to encode.
 *                              Set to NULL to signal the encoder that it has to finish up stuff when ending encoding.
 * @param   msTimestamp         Timestamp (PTS) to use for encoding.
 *
 * @note    Caller must take encoder lock.
 */
static int recordingCodecVPXEncodeWorker(PRECORDINGCODEC pCodec, vpx_image_t *pImage, uint64_t msTimestamp)
{
    int vrc;

    PRECORDINGCODECVPX pVPX = &pCodec->Video.VPX;

    /* Presentation TimeStamp (PTS). */
    vpx_codec_pts_t const pts = msTimestamp;
    vpx_codec_err_t const rcv = vpx_codec_encode(&pVPX->Ctx,
                                                 pImage,
                                                 pts                            /* Timestamp */,
                                                 pCodec->Parms.u.Video.uDelayMs /* How long to show this frame */,
                                                 0                              /* Flags */,
                                                 pVPX->uEncoderDeadline         /* Quality setting */);
    if (rcv != VPX_CODEC_OK)
    {
        if (pCodec->State.cEncErrors++ < 64) /** @todo Make this configurable. */
            LogRel(("Recording: Failed to encode video frame: %s\n", vpx_codec_err_to_string(rcv)));
        return VERR_RECORDING_ENCODING_FAILED;
    }

    pCodec->State.cEncErrors = 0;

    vpx_codec_iter_t iter = NULL;
    vrc = VERR_NO_DATA;
    for (;;)
    {
        const vpx_codec_cx_pkt_t *pPkt = vpx_codec_get_cx_data(&pVPX->Ctx, &iter);
        if (!pPkt) /* End of list */
            break;

        switch (pPkt->kind)
        {
            case VPX_CODEC_CX_FRAME_PKT:
            {
                /* Calculate the absolute PTS of this frame (in ms). */
                uint64_t tsAbsPTSMs =   pPkt->data.frame.pts * 1000
                                      * (uint64_t)pCodec->Video.VPX.Cfg.g_timebase.num / pCodec->Video.VPX.Cfg.g_timebase.den;

                const bool fKeyframe = RT_BOOL(pPkt->data.frame.flags & VPX_FRAME_IS_KEY);

                uint32_t fFlags = RECORDINGCODEC_ENC_F_NONE;
                if (fKeyframe)
                    fFlags |= RECORDINGCODEC_ENC_F_BLOCK_IS_KEY;
                if (pPkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE)
                    fFlags |= RECORDINGCODEC_ENC_F_BLOCK_IS_INVISIBLE;

                Log3Func(("msTimestamp=%RU64, fFlags=%#x\n", msTimestamp, fFlags));

                vrc = pCodec->Callbacks.pfnWriteData(pCodec, pPkt->data.frame.buf, pPkt->data.frame.sz,
                                                     tsAbsPTSMs, fFlags, pCodec->Callbacks.pvUser);
                break;
            }

            default:
                AssertFailed();
                LogFunc(("Unexpected video packet type %ld\n", pPkt->kind));
                break;
        }
    }

    return vrc;
}

/** @copydoc RECORDINGCODECOPS::pfnEncode */
static DECLCALLBACK(int) recordingCodecVPXEncode(PRECORDINGCODEC pCodec, PRECORDINGFRAME pFrame,
                                                 uint64_t msTimestamp, void *pvUser)
{
    recordingCodecLock(pCodec);

    int vrc;

    /* If no frame is given, encode the last composed frame again with the given timestamp. */
    if (pFrame == NULL)
    {
        vrc = recordingCodecVPXEncodeWorker(pCodec, &pCodec->Video.VPX.RawImage, msTimestamp);
        recordingCodecUnlock(pCodec);
        return vrc;
    }

    RecordingContext *pCtx = (RecordingContext *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    Assert(pFrame->msTimestamp == msTimestamp);

    /* Note: We get BGRA 32 input here. */
    PRECORDINGVIDEOFRAME pFront = &pCodec->Video.VPX.Front;
    PRECORDINGVIDEOFRAME pBack  = &pCodec->Video.VPX.Back;

    int32_t sx = 0; /* X origin within the source frame. */
    int32_t sy = 0; /* Y origin within the source frame. */
    int32_t sw = 0; /* Width of the source frame (starting at X origin). */
    int32_t sh = 0; /* Height of the source frame (starting at Y origin). */
    int32_t dx = 0; /* X destination of the source frame within the destination frame. */
    int32_t dy = 0; /* Y destination of the source frame within the destination frame. */

    /*
     * Note!
     *
     * We don't implement any rendering graph or some such here, as we only have two things to render here, namely:
     *
     * - the actual framebuffer updates
     * - if available (through mouse integration via Guest Additions): the guest's mouse cursor via a (software) overlay
     *
     * So composing is done the following way:
     *
     *  - always store the plain framebuffer updates in our back buffer first
     *  - copy the framebuffer updates to our front buffer
     *  - restore the area of the old mouse cursor position by copying frame buffer area data from back -> front buffer
     *  - apply the mouse cursor updates to our front buffer
     */

    switch (pFrame->enmType)
    {
        case RECORDINGFRAME_TYPE_VIDEO:
        {
            PRECORDINGVIDEOFRAME pSrc = &pFrame->u.Video;

            vrc = RecordingVideoBlitRaw(pFront->pau8Buf, pFront->cbBuf, pSrc->Pos.x, pSrc->Pos.y,
                                        pFront->Info.uBytesPerLine, pFront->Info.uBPP, pFront->Info.enmPixelFmt,
                                        pSrc->pau8Buf, pSrc->cbBuf, 0 /* uSrcX */, 0 /* uSrcY */, pSrc->Info.uWidth, pSrc->Info.uHeight,
                                        pSrc->Info.uBytesPerLine, pSrc->Info.uBPP, pSrc->Info.enmPixelFmt);
#if 0
            RecordingUtilsDbgDumpVideoFrameEx(pFront, "/tmp/recording", "encode-front");
            RecordingUtilsDbgDumpImageData(pSrc->pau8Buf, pSrc->cbBuf,
                                           "/tmp/recording", "encode-src",
                                           pSrc->Info.uWidth, pSrc->Info.uHeight, pSrc->Info.uBytesPerLine, pSrc->Info.uBPP);
#endif
            vrc = RecordingVideoFrameBlitFrame(pBack, pSrc->Pos.x, pSrc->Pos.y,
                                               pSrc,  0 /* uSrcX */, 0 /* uSrcY */, pSrc->Info.uWidth, pSrc->Info.uHeight);
            pFront = pBack;

            sw = pSrc->Info.uWidth;
            sh = pSrc->Info.uHeight;
            sx = pSrc->Pos.x;
            sy = pSrc->Pos.y;

            Log3Func(("RECORDINGFRAME_TYPE_VIDEO: sx=%d, sy=%d, sw=%d, sh=%d\n", sx, sy, sw, sh));

            dx = pSrc->Pos.x;
            dy = pSrc->Pos.y;
            break;
        }

        case RECORDINGFRAME_TYPE_CURSOR_SHAPE:
        {
            Log3Func(("RECORDINGFRAME_TYPE_CURSOR_SHAPE: w=%d, h=%d\n",
                      pFrame->u.CursorShape.Info.uWidth, pFrame->u.CursorShape.Info.uHeight));

            RecordingVideoFrameFree(pCodec->Video.VPX.pCursorShape);
            pCodec->Video.VPX.pCursorShape = RecordingVideoFrameDup(&pFrame->u.CursorShape);
            AssertPtr(pCodec->Video.VPX.pCursorShape);

            RT_FALL_THROUGH(); /* Re-render cursor with new shape below. */
        }

        case RECORDINGFRAME_TYPE_CURSOR_POS:
        {
            const PRECORDINGVIDEOFRAME pCursor = pCodec->Video.VPX.pCursorShape;
            if (!pCursor) /* No cursor shape set yet. */
                break;

            PRECORDINGPOS pPosOld = &pCodec->Video.VPX.PosCursorOld;
            PRECORDINGPOS pPosNew = pFrame->enmType == RECORDINGFRAME_TYPE_CURSOR_POS
                                  ? &pFrame->u.Cursor.Pos
                                  : pPosOld;

            Log3Func(("RECORDINGFRAME_TYPE_CURSOR_POS: x=%d, y=%d, oldx=%d, oldy=%d, w=%d, h=%d\n",
                      pPosNew->x, pPosNew->y,
                      pPosOld->x, pPosOld->y, pCursor->Info.uWidth, pCursor->Info.uHeight));

            /* Calculate the merged area between the old and the new (current) cursor position
             * so that we update everything to not create any ghosting. */
            sx = RT_MIN(pPosNew->x, pPosOld->x);
            sy = RT_MIN(pPosNew->y, pPosOld->y);
            sw = (  pPosNew->x > pPosOld->x
                  ? pPosNew->x - pPosOld->x
                  : pPosOld->x - pPosNew->x) + pCursor->Info.uWidth;
            sh = (  pPosNew->y > pPosOld->y
                  ? pPosNew->y - pPosOld->y
                  : pPosOld->y - pPosNew->y) + pCursor->Info.uHeight;

            /* Limit the width / height to blit to the front buffer's size. */
            if (sx + sw >= (int32_t)pFront->Info.uWidth)
                sw = pFront->Info.uWidth - sx;
            if (sy + sh >= (int32_t)pFront->Info.uHeight)
                sh = pFront->Info.uHeight - sy;

            /* Save current cursor position for next iteration. */
            *pPosOld = *pPosNew;

            dx = sx;
            dy = sy;

            Log3Func(("RECORDINGFRAME_TYPE_CURSOR_POS: sx=%d, sy=%d, sw=%d, sh=%d\n", sx, sy, sw, sh));

            /* Nothing to encode? Bail out. */
            if (             sw <= 0
                ||           sh <= 0
                || (uint32_t)sx >  pBack->Info.uWidth
                || (uint32_t)sy >  pBack->Info.uHeight)
                break;

            /* Restore background of front buffer first. */
            vrc = RecordingVideoFrameBlitFrame(pFront, dx, dy,
                                               pBack,  sx, sy, sw, sh);

            /* Blit mouse cursor to front buffer. */
            if (RT_SUCCESS(vrc))
                RecordingVideoFrameBlitRawAlpha(pFront, pPosNew->x, pPosNew->y,
                                                pCursor->pau8Buf, pCursor->cbBuf,
                                                0 /* uSrcX */, 0 /* uSrcY */, pCursor->Info.uWidth, pCursor->Info.uHeight,
                                                pCursor->Info.uBytesPerLine, pCursor->Info.uBPP, pCursor->Info.enmPixelFmt);
#if 0
            RecordingUtilsDbgDumpVideoFrameEx(pFront, "/tmp/recording", "cursor-alpha-front");
#endif
            break;
        }

        default:
            AssertFailed();
            break;

    }

    /* Nothing to encode? Bail out. */
    if (   sw == 0
        || sh == 0)
    {
        recordingCodecUnlock(pCodec);
        return VINF_SUCCESS;
    }

    Log3Func(("Source: %RU32x%RU32 (Pos %RU32x%RU32)\n", sw, sh, sx, sy));
    Log3Func(("Front : %RU32x%RU32\n", pFront->Info.uWidth, pFront->Info.uHeight));
    Log3Func(("Codec : %RU32x%RU32\n", pCodec->Parms.u.Video.uWidth, pCodec->Parms.u.Video.uHeight));

    vrc = RecordingUtilsCoordsCropCenter(&pCodec->Parms, &sx, &sy, &sw, &sh, &dx, &dy);
    if (vrc == VINF_SUCCESS) /* vrc might be VWRN_RECORDING_ENCODING_SKIPPED to skip encoding. */
    {
        Log3Func(("Encoding source %RI32,%RI32 (%RI32x%RI32) to %RI32,%RI32 (%zu bytes)\n",
                  sx, sy, sw, sh, dx, dy, sw * sh * (pFront->Info.uBPP / 8)));
#if 0
        AssertReturn(sw + dx <= (int32_t)pCodec->Parms.u.Video.uWidth,  VERR_INVALID_PARAMETER);
        AssertReturn(sy + sh <= (int32_t)pCodec->Parms.u.Video.uHeight, VERR_INVALID_PARAMETER);
#endif
#if 0
        RecordingUtilsDbgDumpImageData(pFront->pau8Buf, pFront->cbBuf,
                                       NULL /* Use default temp dir */, "cropped", sx, sy, sw, sh,
                                       pFront->Info.uBytesPerLine, pFront->Info.uBPP);
#endif
        /* Blit (and convert from BGRA 32) the changed parts of the front buffer to the YUV 420 surface of the codec. */
        RecordingUtilsConvBGRA32ToYUVI420Ex(/* Destination */
                                            pCodec->Video.VPX.pu8YuvBuf, dx, dy, pCodec->Parms.u.Video.uWidth, pCodec->Parms.u.Video.uHeight,
                                            /* Source */
                                            pFront->pau8Buf, sx, sy, sw, sh, pFront->Info.uBytesPerLine, pFront->Info.uBPP);

        vrc = recordingCodecVPXEncodeWorker(pCodec, &pCodec->Video.VPX.RawImage, msTimestamp);
    }

    recordingCodecUnlock(pCodec);

    return vrc;
}

/** @copydoc RECORDINGCODECOPS::pfnScreenChange */
static DECLCALLBACK(int) recordingCodecVPXScreenChange(PRECORDINGCODEC pCodec, PRECORDINGSURFACEINFO pInfo)
{
    LogFunc(("ENTER: w=%RU32, h=%RU32, bpp=%RU8\n", pInfo->uWidth, pInfo->uHeight, pInfo->uBPP));

    PRECORDINGCODECVPX pVPX = &pCodec->Video.VPX;

    recordingCodecLock(pCodec);

    /* Tear down old stuff. */
    RecordingVideoFrameDestroy(&pVPX->Front);
    RecordingVideoFrameDestroy(&pVPX->Back);

    /* Initialize front + back buffers. */
    int vrc = RecordingVideoFrameInit(&pVPX->Front, RECORDINGVIDEOFRAME_F_VISIBLE,
                                      pInfo->uWidth, pInfo->uHeight, 0, 0,
                                      pInfo->uBPP, pInfo->enmPixelFmt);
    if (RT_SUCCESS(vrc))
        vrc = RecordingVideoFrameInit(&pVPX->Back, RECORDINGVIDEOFRAME_F_VISIBLE,
                                      pInfo->uWidth, pInfo->uHeight, 0, 0,
                                      pInfo->uBPP, pInfo->enmPixelFmt);
    if (RT_SUCCESS(vrc))
    {
        RecordingVideoFrameClear(&pVPX->Front);
        RecordingVideoFrameClear(&pVPX->Back);

        recordingCodecVPXClearPlanes(pCodec);

        /* Calculate the X/Y origins for cropping / centering.
         * This is needed as the codec's video output size not necessarily matches the VM's frame buffer size. */
        pCodec->Parms.u.Video.Scaling.u.Crop.m_iOriginX = int32_t(pCodec->Parms.u.Video.uWidth  - pInfo->uWidth) / 2;
        pCodec->Parms.u.Video.Scaling.u.Crop.m_iOriginY = int32_t(pCodec->Parms.u.Video.uHeight - pInfo->uHeight) / 2;
    }

    recordingCodecUnlock(pCodec);

    if (RT_FAILURE(vrc))
        LogRel(("Recording: Codec error handling screen change notification: %Rrc\n", vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;

}
#endif /* VBOX_WITH_LIBVPX */


/*********************************************************************************************************************************
*   Ogg Vorbis codec                                                                                                             *
*********************************************************************************************************************************/

#ifdef VBOX_WITH_LIBVORBIS
/** @copydoc RECORDINGCODECOPS::pfnInit */
static DECLCALLBACK(int) recordingCodecVorbisInit(PRECORDINGCODEC pCodec)
{
    pCodec->cbScratch = _4K;
    pCodec->pvScratch = RTMemAlloc(pCodec->cbScratch);
    AssertPtrReturn(pCodec->pvScratch, VERR_NO_MEMORY);

    const PPDMAUDIOPCMPROPS pPCMProps = &pCodec->Parms.u.Audio.PCMProps;

    /** @todo BUGBUG When left out this call, vorbis_block_init() does not find oggpack_writeinit and all goes belly up ... */
    oggpack_buffer b;
    oggpack_writeinit(&b);

    vorbis_info_init(&pCodec->Audio.Vorbis.info);

    int vorbis_rc;
    if (pCodec->Parms.uBitrate == 0) /* No bitrate management? Then go for ABR (Average Bit Rate) only. */
        vorbis_rc = vorbis_encode_init_vbr(&pCodec->Audio.Vorbis.info,
                                           PDMAudioPropsChannels(pPCMProps), PDMAudioPropsHz(pPCMProps),
                                           (float).4 /* Quality, from -.1 (lowest) to 1 (highest) */);
    else
        vorbis_rc = vorbis_encode_setup_managed(&pCodec->Audio.Vorbis.info, PDMAudioPropsChannels(pPCMProps), PDMAudioPropsHz(pPCMProps),
                                                -1 /* max bitrate (unset) */, pCodec->Parms.uBitrate /* kbps, nominal */, -1 /* min bitrate (unset) */);
    if (vorbis_rc)
    {
        LogRel(("Recording: Audio codec failed to setup %s mode (bitrate %RU32): %d\n",
                pCodec->Parms.uBitrate == 0 ? "VBR" : "bitrate management", pCodec->Parms.uBitrate, vorbis_rc));
        return VERR_RECORDING_CODEC_INIT_FAILED;
    }

    vorbis_rc = vorbis_encode_setup_init(&pCodec->Audio.Vorbis.info);
    if (vorbis_rc)
    {
        LogRel(("Recording: vorbis_encode_setup_init() failed (%d)\n", vorbis_rc));
        return VERR_RECORDING_CODEC_INIT_FAILED;
    }

    /* Initialize the analysis state and encoding storage. */
    vorbis_rc = vorbis_analysis_init(&pCodec->Audio.Vorbis.dsp_state, &pCodec->Audio.Vorbis.info);
    if (vorbis_rc)
    {
        vorbis_info_clear(&pCodec->Audio.Vorbis.info);
        LogRel(("Recording: vorbis_analysis_init() failed (%d)\n", vorbis_rc));
        return VERR_RECORDING_CODEC_INIT_FAILED;
    }

    vorbis_rc = vorbis_block_init(&pCodec->Audio.Vorbis.dsp_state, &pCodec->Audio.Vorbis.block_cur);
    if (vorbis_rc)
    {
        vorbis_info_clear(&pCodec->Audio.Vorbis.info);
        LogRel(("Recording: vorbis_block_init() failed (%d)\n", vorbis_rc));
        return VERR_RECORDING_CODEC_INIT_FAILED;
    }

    if (!pCodec->Parms.msFrame) /* No ms per frame defined? Use default. */
        pCodec->Parms.msFrame = VBOX_RECORDING_VORBIS_FRAME_MS_DEFAULT;

    return VINF_SUCCESS;
}

/** @copydoc RECORDINGCODECOPS::pfnDestroy */
static DECLCALLBACK(int) recordingCodecVorbisDestroy(PRECORDINGCODEC pCodec)
{
    PRECORDINGCODECVORBIS pVorbis = &pCodec->Audio.Vorbis;

    vorbis_block_clear(&pVorbis->block_cur);
    vorbis_dsp_clear  (&pVorbis->dsp_state);
    vorbis_info_clear (&pVorbis->info);

    return VINF_SUCCESS;
}

/** @copydoc RECORDINGCODECOPS::pfnEncode */
static DECLCALLBACK(int) recordingCodecVorbisEncode(PRECORDINGCODEC pCodec,
                                                    const PRECORDINGFRAME pFrame, uint64_t msTimestamp, void *pvUser)
{
    RT_NOREF(msTimestamp, pvUser);

    const PPDMAUDIOPCMPROPS pPCMProps = &pCodec->Parms.u.Audio.PCMProps;

    Assert      (pCodec->Parms.cbFrame);
    AssertReturn(pFrame->u.Audio.cbBuf % pCodec->Parms.cbFrame == 0, VERR_INVALID_PARAMETER);
    Assert      (pFrame->u.Audio.cbBuf);
    AssertReturn(pFrame->u.Audio.cbBuf % PDMAudioPropsFrameSize(pPCMProps) == 0, VERR_INVALID_PARAMETER);
    AssertReturn(pCodec->cbScratch >= pFrame->u.Audio.cbBuf, VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;

    int const cbFrame = PDMAudioPropsFrameSize(pPCMProps);
    int const cFrames = (int)(pFrame->u.Audio.cbBuf / cbFrame);

    /* Write non-interleaved frames. */
    float  **buffer = vorbis_analysis_buffer(&pCodec->Audio.Vorbis.dsp_state, cFrames);
    int16_t *puSrc  = (int16_t *)pFrame->u.Audio.pvBuf; RT_NOREF(puSrc);

    /* Convert samples into floating point. */
    /** @todo This is sloooooooooooow! Optimize this! */
    uint8_t const cChannels = PDMAudioPropsChannels(pPCMProps);
    AssertReturn(cChannels == 2, VERR_NOT_SUPPORTED);

    float const div = 1.0f / 32768.0f;

    for(int f = 0; f < cFrames; f++)
    {
        buffer[0][f] = (float)puSrc[0] * div;
        buffer[1][f] = (float)puSrc[1] * div;
        puSrc += cChannels;
    }

    int vorbis_rc = vorbis_analysis_wrote(&pCodec->Audio.Vorbis.dsp_state, cFrames);
    if (vorbis_rc)
    {
        LogRel(("Recording: vorbis_analysis_wrote() failed (%d)\n", vorbis_rc));
        return VERR_RECORDING_ENCODING_FAILED;
    }

    size_t cBlocksEncoded = 0;
    size_t cBytesEncoded  = 0;

    uint8_t *puDst = (uint8_t *)pCodec->pvScratch;

    while (vorbis_analysis_blockout(&pCodec->Audio.Vorbis.dsp_state, &pCodec->Audio.Vorbis.block_cur) == 1 /* More available? */)
    {
        vorbis_rc = vorbis_analysis(&pCodec->Audio.Vorbis.block_cur, NULL);
        if (vorbis_rc < 0)
        {
            LogRel(("Recording: vorbis_analysis() failed (%d)\n", vorbis_rc));
            vorbis_rc = 0; /* Reset */
            vrc = VERR_RECORDING_ENCODING_FAILED;
            break;
        }

        vorbis_rc = vorbis_bitrate_addblock(&pCodec->Audio.Vorbis.block_cur);
        if (vorbis_rc < 0)
        {
            LogRel(("Recording: vorbis_bitrate_addblock() failed (%d)\n", vorbis_rc));
            vorbis_rc = 0; /* Reset */
            vrc = VERR_RECORDING_ENCODING_FAILED;
            break;
        }

        /* Vorbis expects us to flush packets one at a time directly to the container.
         *
         * If we flush more than one packet in a row, players can't decode this then. */
        ogg_packet op;
        while ((vorbis_rc = vorbis_bitrate_flushpacket(&pCodec->Audio.Vorbis.dsp_state, &op)) > 0)
        {
            cBytesEncoded += op.bytes;
            AssertBreakStmt(cBytesEncoded <= pCodec->cbScratch, vrc = VERR_BUFFER_OVERFLOW);
            cBlocksEncoded++;

            vrc = pCodec->Callbacks.pfnWriteData(pCodec, op.packet, (size_t)op.bytes, pCodec->State.tsLastWrittenMs,
                                                 RECORDINGCODEC_ENC_F_BLOCK_IS_KEY /* Every Vorbis frame is a key frame */,
                                                 pCodec->Callbacks.pvUser);
        }

        RT_NOREF(puDst);

        /* Note: When vorbis_rc is 0, this marks the last packet, a negative values means error. */
        if (vorbis_rc < 0)
        {
            LogRel(("Recording: vorbis_bitrate_flushpacket() failed (%d)\n", vorbis_rc));
            vorbis_rc = 0; /* Reset */
            vrc = VERR_RECORDING_ENCODING_FAILED;
            break;
        }
    }

    if (vorbis_rc < 0)
    {
        LogRel(("Recording: vorbis_analysis_blockout() failed (%d)\n", vorbis_rc));
        return VERR_RECORDING_ENCODING_FAILED;
    }

    if (RT_FAILURE(vrc))
        LogRel(("Recording: Encoding Vorbis audio data failed, vrc=%Rrc\n", vrc));

    Log3Func(("cbSrc=%zu, cbDst=%zu, cEncoded=%zu, cbEncoded=%zu, vrc=%Rrc\n",
              pFrame->u.Audio.cbBuf, pCodec->cbScratch, cBlocksEncoded, cBytesEncoded, vrc));

    return vrc;
}

/** @copydoc RECORDINGCODECOPS::pfnFinalize */
static DECLCALLBACK(int) recordingCodecVorbisFinalize(PRECORDINGCODEC pCodec)
{
    int vorbis_rc = vorbis_analysis_wrote(&pCodec->Audio.Vorbis.dsp_state, 0 /* Means finalize */);
    if (vorbis_rc)
    {
        LogRel(("Recording: vorbis_analysis_wrote() failed for finalizing stream (%d)\n", vorbis_rc));
        return VERR_RECORDING_ENCODING_FAILED;
    }

    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_LIBVORBIS */


/*********************************************************************************************************************************
*   Codec API                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Initializes an audio codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec instance to initialize.
 * @param   pCallbacks          Codec callback table to use for the codec.
 * @param   Settings            Screen settings to use for initialization.
 */
static int recordingCodecInitAudio(const PRECORDINGCODEC pCodec,
                                   const PRECORDINGCODECCALLBACKS pCallbacks, const settings::RecordingScreen &Settings)
{
    AssertReturn(pCodec->Parms.enmType == RECORDINGCODECTYPE_AUDIO, VERR_INVALID_PARAMETER);

    com::Utf8Str strCodec;
    settings::RecordingScreen::audioCodecToString(pCodec->Parms.enmAudioCodec, strCodec);
    LogRel(("Recording: Initializing audio codec '%s'\n", strCodec.c_str()));

    const PPDMAUDIOPCMPROPS pPCMProps = &pCodec->Parms.u.Audio.PCMProps;

    PDMAudioPropsInit(pPCMProps,
                      Settings.Audio.cBits / 8,
                      true /* fSigned */, Settings.Audio.cChannels, Settings.Audio.uHz);
    pCodec->Parms.uBitrate = 0; /** @todo No bitrate management for audio yet. */

    if (pCallbacks)
        memcpy(&pCodec->Callbacks, pCallbacks, sizeof(RECORDINGCODECCALLBACKS));

    int vrc = VINF_SUCCESS;

    if (pCodec->Ops.pfnParseOptions)
        vrc = pCodec->Ops.pfnParseOptions(pCodec, Settings.strOptions);

    if (RT_SUCCESS(vrc))
        vrc = pCodec->Ops.pfnInit(pCodec);

    if (RT_SUCCESS(vrc))
    {
        Assert(PDMAudioPropsAreValid(pPCMProps));

        uint32_t uBitrate = pCodec->Parms.uBitrate; /* Bitrate management could have been changed by pfnInit(). */

        LogRel2(("Recording: Audio codec is initialized with %RU32Hz, %RU8 channel(s), %RU8 bits per sample\n",
                 PDMAudioPropsHz(pPCMProps), PDMAudioPropsChannels(pPCMProps), PDMAudioPropsSampleBits(pPCMProps)));
        LogRel2(("Recording: Audio codec's bitrate management is %s (%RU32 kbps)\n", uBitrate ? "enabled" : "disabled", uBitrate));

        if (!pCodec->Parms.msFrame || pCodec->Parms.msFrame >= RT_MS_1SEC) /* Not set yet by codec stuff above? */
            pCodec->Parms.msFrame = 20; /* 20ms by default should be a sensible value; to prevent division by zero. */

        pCodec->Parms.csFrame  = PDMAudioPropsHz(pPCMProps) / (RT_MS_1SEC / pCodec->Parms.msFrame);
        pCodec->Parms.cbFrame  = PDMAudioPropsFramesToBytes(pPCMProps, pCodec->Parms.csFrame);

        LogFlowFunc(("cbSample=%RU32, msFrame=%RU32 -> csFrame=%RU32, cbFrame=%RU32, uBitrate=%RU32\n",
                     PDMAudioPropsSampleSize(pPCMProps), pCodec->Parms.msFrame, pCodec->Parms.csFrame, pCodec->Parms.cbFrame, pCodec->Parms.uBitrate));
    }
    else
        LogRel(("Recording: Error initializing audio codec (%Rrc)\n", vrc));

    return vrc;
}

/**
 * Initializes a video codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec instance to initialize.
 * @param   pCallbacks          Codec callback table to use for the codec.
 * @param   Settings            Screen settings to use for initialization.
 */
static int recordingCodecInitVideo(const PRECORDINGCODEC pCodec,
                                   const PRECORDINGCODECCALLBACKS pCallbacks, const settings::RecordingScreen &Settings)
{
    com::Utf8Str strTemp;
    settings::RecordingScreen::videoCodecToString(pCodec->Parms.enmVideoCodec, strTemp);
    LogRel(("Recording: Initializing video codec '%s'\n", strTemp.c_str()));

    pCodec->Parms.uBitrate         = Settings.Video.ulRate;
    pCodec->Parms.u.Video.uFPS     = Settings.Video.ulFPS;
    pCodec->Parms.u.Video.uWidth   = Settings.Video.ulWidth;
    pCodec->Parms.u.Video.uHeight  = Settings.Video.ulHeight;
    pCodec->Parms.u.Video.uDelayMs = RT_MS_1SEC / pCodec->Parms.u.Video.uFPS;

    if (pCallbacks)
        memcpy(&pCodec->Callbacks, pCallbacks, sizeof(RECORDINGCODECCALLBACKS));

    AssertReturn(pCodec->Parms.uBitrate, VERR_INVALID_PARAMETER);        /* Bitrate must be set. */
    AssertStmt(pCodec->Parms.u.Video.uFPS, pCodec->Parms.u.Video.uFPS = 25); /* Prevent division by zero. */

    AssertReturn(pCodec->Parms.u.Video.uHeight, VERR_INVALID_PARAMETER);
    AssertReturn(pCodec->Parms.u.Video.uWidth, VERR_INVALID_PARAMETER);
    AssertReturn(pCodec->Parms.u.Video.uDelayMs, VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;

    if (pCodec->Ops.pfnParseOptions)
        vrc = pCodec->Ops.pfnParseOptions(pCodec, Settings.strOptions);

    if (   RT_SUCCESS(vrc)
        && pCodec->Ops.pfnInit)
        vrc = pCodec->Ops.pfnInit(pCodec);

    if (RT_SUCCESS(vrc))
    {
        pCodec->Parms.enmType       = RECORDINGCODECTYPE_VIDEO;
        pCodec->Parms.enmVideoCodec = RecordingVideoCodec_VP8; /** @todo No VP9 yet. */
    }
    else
        LogRel(("Recording: Error initializing video codec (%Rrc)\n", vrc));

    return vrc;
}

#ifdef VBOX_WITH_AUDIO_RECORDING
/**
 * Lets an audio codec parse advanced options given from a string.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec instance to parse options for.
 * @param   strOptions          Options string to parse.
 */
static DECLCALLBACK(int) recordingCodecAudioParseOptions(PRECORDINGCODEC pCodec, const com::Utf8Str &strOptions)
{
    AssertReturn(pCodec->Parms.enmType == RECORDINGCODECTYPE_AUDIO, VERR_INVALID_PARAMETER);

    size_t pos = 0;
    com::Utf8Str key, value;
    while ((pos = strOptions.parseKeyValue(key, value, pos)) != com::Utf8Str::npos)
    {
        if (key.compare("ac_profile", com::Utf8Str::CaseInsensitive) == 0)
        {
            if (value.compare("low", com::Utf8Str::CaseInsensitive) == 0)
            {
                PDMAudioPropsInit(&pCodec->Parms.u.Audio.PCMProps, 16, true /* fSigned */, 1 /* Channels */, 8000 /* Hz */);
            }
            else if (value.startsWith("med" /* "med[ium]" */, com::Utf8Str::CaseInsensitive) == 0)
            {
                /* Stay with the defaults. */
            }
            else if (value.compare("high", com::Utf8Str::CaseInsensitive) == 0)
            {
                PDMAudioPropsInit(&pCodec->Parms.u.Audio.PCMProps, 16, true /* fSigned */, 2 /* Channels */, 48000 /* Hz */);
            }
        }
        else
            LogRel(("Recording: Unknown option '%s' (value '%s'), skipping\n", key.c_str(), value.c_str()));

    } /* while */

    return VINF_SUCCESS;
}
#endif

static void recordingCodecReset(PRECORDINGCODEC pCodec)
{
    pCodec->State.tsLastWrittenMs = 0;
    pCodec->State.cEncErrors = 0;
}

/**
 * Common code for codec creation.
 *
 * @param   pCodec              Codec instance to create.
 */
static void recordingCodecCreateCommon(PRECORDINGCODEC pCodec)
{
    RT_ZERO(pCodec->Ops);
    RT_ZERO(pCodec->Callbacks);
}

/**
 * Creates an audio codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec instance to create.
 * @param   enmAudioCodec       Audio codec to create.
 */
int recordingCodecCreateAudio(PRECORDINGCODEC pCodec, RecordingAudioCodec_T enmAudioCodec)
{
    int vrc;

    recordingCodecCreateCommon(pCodec);

    switch (enmAudioCodec)
    {
# ifdef VBOX_WITH_LIBVORBIS
        case RecordingAudioCodec_OggVorbis:
        {
            pCodec->Ops.pfnInit         = recordingCodecVorbisInit;
            pCodec->Ops.pfnDestroy      = recordingCodecVorbisDestroy;
            pCodec->Ops.pfnParseOptions = recordingCodecAudioParseOptions;
            pCodec->Ops.pfnEncode       = recordingCodecVorbisEncode;
            pCodec->Ops.pfnFinalize     = recordingCodecVorbisFinalize;

            vrc = VINF_SUCCESS;
            break;
        }
# endif /* VBOX_WITH_LIBVORBIS */

        default:
            LogRel(("Recording: Selected codec is not supported!\n"));
            vrc = VERR_RECORDING_CODEC_NOT_SUPPORTED;
            break;
    }

    if (RT_SUCCESS(vrc))
    {
        pCodec->Parms.enmType       = RECORDINGCODECTYPE_AUDIO;
        pCodec->Parms.enmAudioCodec = enmAudioCodec;
    }

    return vrc;
}

/**
 * Creates a video codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec instance to create.
 * @param   enmVideoCodec       Video codec to create.
 */
int recordingCodecCreateVideo(PRECORDINGCODEC pCodec, RecordingVideoCodec_T enmVideoCodec)
{
    int vrc;

    recordingCodecCreateCommon(pCodec);

    switch (enmVideoCodec)
    {
# ifdef VBOX_WITH_LIBVPX
        case RecordingVideoCodec_VP8:
        {
            pCodec->Ops.pfnInit         = recordingCodecVPXInit;
            pCodec->Ops.pfnDestroy      = recordingCodecVPXDestroy;
            pCodec->Ops.pfnFinalize     = recordingCodecVPXFinalize;
            pCodec->Ops.pfnParseOptions = recordingCodecVPXParseOptions;
            pCodec->Ops.pfnEncode       = recordingCodecVPXEncode;
            pCodec->Ops.pfnScreenChange = recordingCodecVPXScreenChange;

            vrc = VINF_SUCCESS;
            break;
        }
# endif /* VBOX_WITH_LIBVPX */

        default:
            vrc = VERR_RECORDING_CODEC_NOT_SUPPORTED;
            break;
    }

    if (RT_SUCCESS(vrc))
    {
        pCodec->Parms.enmType       = RECORDINGCODECTYPE_VIDEO;
        pCodec->Parms.enmVideoCodec = enmVideoCodec;
    }

    return vrc;
}

/**
 * Initializes a codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to initialize.
 * @param   pCallbacks          Codec callback table to use. Optional and may be NULL.
 * @param   Settings            Settings to use for initializing the codec.
 */
int recordingCodecInit(const PRECORDINGCODEC pCodec, const PRECORDINGCODECCALLBACKS pCallbacks, const settings::RecordingScreen &Settings)
{
    int vrc = RTCritSectInit(&pCodec->CritSect);
    AssertRCReturn(vrc, vrc);

    pCodec->cbScratch = 0;
    pCodec->pvScratch = NULL;

    recordingCodecReset(pCodec);

    if (pCodec->Parms.enmType == RECORDINGCODECTYPE_AUDIO)
        vrc = recordingCodecInitAudio(pCodec, pCallbacks, Settings);
    else if (pCodec->Parms.enmType == RECORDINGCODECTYPE_VIDEO)
        vrc = recordingCodecInitVideo(pCodec, pCallbacks, Settings);
    else
        AssertFailedStmt(vrc = VERR_NOT_SUPPORTED);

    return vrc;
}

/**
 * Destroys an audio codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to destroy.
 */
static int recordingCodecDestroyAudio(PRECORDINGCODEC pCodec)
{
    AssertReturn(pCodec->Parms.enmType == RECORDINGCODECTYPE_AUDIO, VERR_INVALID_PARAMETER);

    return pCodec->Ops.pfnDestroy(pCodec);
}

/**
 * Destroys a video codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to destroy.
 */
static int recordingCodecDestroyVideo(PRECORDINGCODEC pCodec)
{
    AssertReturn(pCodec->Parms.enmType == RECORDINGCODECTYPE_VIDEO, VERR_INVALID_PARAMETER);

    return pCodec->Ops.pfnDestroy(pCodec);
}

/**
 * Destroys the codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to destroy.
 */
int recordingCodecDestroy(PRECORDINGCODEC pCodec)
{
    if (pCodec->Parms.enmType == RECORDINGCODECTYPE_INVALID)
        return VINF_SUCCESS;

    int vrc;

    if (pCodec->Parms.enmType == RECORDINGCODECTYPE_AUDIO)
        vrc = recordingCodecDestroyAudio(pCodec);
    else if (pCodec->Parms.enmType == RECORDINGCODECTYPE_VIDEO)
        vrc =recordingCodecDestroyVideo(pCodec);
    else
        AssertFailedReturn(VERR_NOT_SUPPORTED);

    if (RT_SUCCESS(vrc))
    {
        if (pCodec->pvScratch)
        {
            Assert(pCodec->cbScratch);
            RTMemFree(pCodec->pvScratch);
            pCodec->pvScratch = NULL;
            pCodec->cbScratch = 0;
        }

        pCodec->Parms.enmType       = RECORDINGCODECTYPE_INVALID;
        pCodec->Parms.enmVideoCodec = RecordingVideoCodec_None;

        int vrc2 = RTCritSectDelete(&pCodec->CritSect);
        AssertRC(vrc2);
    }

    return vrc;
}

/**
 * Feeds the codec encoder with frame data to encode.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to use.
 * @param   pFrame              Pointer to frame data to encode.
 * @param   msTimestamp         Timestamp (PTS) to use for encoding.
 * @param   pvUser              User data pointer. Optional and can be NULL.
 */
int recordingCodecEncodeFrame(PRECORDINGCODEC pCodec, const PRECORDINGFRAME pFrame, uint64_t msTimestamp, void *pvUser)
{
    AssertPtrReturn(pCodec->Ops.pfnEncode, VERR_NOT_SUPPORTED);

    int vrc = pCodec->Ops.pfnEncode(pCodec, pFrame, msTimestamp, pvUser);
    if (RT_SUCCESS(vrc))
        pCodec->State.tsLastWrittenMs = pFrame->msTimestamp;

    return vrc;
}

/**
 * Feeds the codec encoder with the current composed image.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to use.
 * @param   msTimestamp         Timestamp (PTS) to use for encoding.
 */
int recordingCodecEncodeCurrent(PRECORDINGCODEC pCodec, uint64_t msTimestamp)
{
    int vrc = pCodec->Ops.pfnEncode(pCodec, NULL /* pFrame */, msTimestamp, NULL /* pvUser */);
    if (RT_SUCCESS(vrc))
        pCodec->State.tsLastWrittenMs = msTimestamp;

    return vrc;
}

/**
 * Lets the codec know that a screen change has happened.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to use.
 * @param   pInfo               Screen info to send.
 */
int recordingCodecScreenChange(PRECORDINGCODEC pCodec, PRECORDINGSURFACEINFO pInfo)
{
    LogRel2(("Recording: Codec got screen change notification (%RU16x%RU16, %RU8 BPP)\n",
             pInfo->uWidth, pInfo->uHeight, pInfo->uBPP));

    if (!pCodec->Ops.pfnScreenChange)
        return VINF_SUCCESS;

    /* Fend-off bogus reports. */
    if (   !pInfo->uWidth
        || !pInfo->uHeight)
        return VERR_INVALID_PARAMETER;
    AssertReturn(pInfo->enmPixelFmt == RECORDINGPIXELFMT_BRGA32 /* Only format we support for now */, VERR_INVALID_PARAMETER);
    AssertReturn(pInfo->uBPP % 8 == 0, VERR_INVALID_PARAMETER);

    return pCodec->Ops.pfnScreenChange(pCodec, pInfo);
}

/**
 * Tells the codec that has to finalize the stream.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to finalize stream for.
 */
int recordingCodecFinalize(PRECORDINGCODEC pCodec)
{
    if (pCodec->Ops.pfnFinalize)
        return pCodec->Ops.pfnFinalize(pCodec);
    return VINF_SUCCESS;
}

/**
 * Returns whether the codec has been initialized or not.
 *
 * @returns @c true if initialized, or @c false if not.
 * @param   pCodec              Codec to return initialization status for.
 */
bool recordingCodecIsInitialized(const PRECORDINGCODEC pCodec)
{
    return pCodec->Ops.pfnInit != NULL; /* pfnInit acts as a beacon for initialization status. */
}

/**
 * Returns the number of writable bytes for a given timestamp.
 *
 * This basically is a helper function to respect the set frames per second (FPS).
 *
 * @returns Number of writable bytes.
 * @param   pCodec              Codec to return number of writable bytes for.
 * @param   msTimestamp         Timestamp (PTS, in ms) return number of writable bytes for.
 */
uint32_t recordingCodecGetWritable(const PRECORDINGCODEC pCodec, uint64_t msTimestamp)
{
    Log3Func(("%RU64 -- tsLastWrittenMs=%RU64 + uDelayMs=%RU32\n",
              msTimestamp, pCodec->State.tsLastWrittenMs,pCodec->Parms.u.Video.uDelayMs));

    if (msTimestamp < pCodec->State.tsLastWrittenMs + pCodec->Parms.u.Video.uDelayMs)
        return 0; /* Too early for writing (respect set FPS). */

    /* For now we just return the complete frame space. */
    AssertMsg(pCodec->Parms.cbFrame, ("Codec not initialized yet\n"));
    return pCodec->Parms.cbFrame;
}

