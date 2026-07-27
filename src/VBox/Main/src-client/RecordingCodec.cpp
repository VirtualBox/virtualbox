/* $Id: RecordingCodec.cpp 114800 2026-07-27 16:53:06Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording codec wrapper.
 */

/*
 * Copyright (C) 2022-2026 Oracle and/or its affiliates.
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
/**
 * Enters a codec's critical section.
 *
 * @param   pCodec              Codec instance to lock.
 */
DECLINLINE(void) recordingCodecLock(PRECORDINGCODEC pCodec)
{
    int vrc2 = RTCritSectEnter(&pCodec->CritSect);
    AssertRC(vrc2);
}

/**
 * Leaves a codec's critical section.
 *
 * @param   pCodec              Codec instance to unlock.
 */
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
# if 0 /* Unused */
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
# endif

/**
 * Checks whether the VPX encoder is allowed to emit a frame at this timestamp.
 *
 * @returns @c true if encoding should proceed now, @c false if it should be deferred.
 * @param   pCodec              Codec instance to query.
 * @param   msTimestamp         Timestamp (PTS, in ms) to evaluate.
 */
DECLINLINE(bool) recordingCodecVPXShouldEncode(const PRECORDINGCODEC pCodec, uint64_t msTimestamp)
{
    if (pCodec->Parms.u.Video.uDelayMs == 0)
        return true;

    if (!pCodec->State.fHaveWrittenFrame)
        return true;

    if (msTimestamp <= pCodec->State.tsLastWrittenMs)
        return false;

    uint64_t const msNextAllowed = pCodec->State.tsLastWrittenMs + pCodec->Parms.u.Video.uDelayMs;
    return msTimestamp >= msNextAllowed;
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
    switch (pCodec->Parms.u.Video.enmRateCtlMode)
    {
        case RecordingRateControlMode_VBR:
            pVPX->Cfg.rc_end_usage = VPX_VBR;
            break;

        default:
            AssertMsgFailedReturn(("Unsupported VPX recording rate control mode %d\n",
                                   pCodec->Parms.u.Video.enmRateCtlMode), VERR_INVALID_PARAMETER);
    }
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

    return VINF_SUCCESS;
}

/** @copydoc RECORDINGCODECOPS::pfnDestroy */
static DECLCALLBACK(int) recordingCodecVPXDestroy(PRECORDINGCODEC pCodec)
{
    PRECORDINGCODECVPX pVPX = &pCodec->Video.VPX;

    vpx_img_free(&pVPX->RawImage);
    pVPX->pu8YuvBuf = NULL; /* Was pointing to VPX.RawImage. */

    vpx_codec_err_t rcv = vpx_codec_destroy(&pVPX->Ctx);
    Assert(rcv == VPX_CODEC_OK); RT_NOREF(rcv);

    return VINF_SUCCESS;
}

/** @copydoc RECORDINGCODECOPS::pfnFinalize */
static DECLCALLBACK(int) recordingCodecVPXFinalize(PRECORDINGCODEC pCodec)
{
    recordingCodecLock(pCodec);

    int vrc = recordingCodecVPXEncodeWorker(pCodec, NULL /* pImage */, pCodec->State.tsLastWrittenMs + 1);
    if (vrc == VERR_NO_DATA) /* No data was available anymore to process, don't propagate to caller. */
        vrc = VINF_SUCCESS;

    recordingCodecUnlock(pCodec);

    return vrc;
}

/**
 * Sets the libvpx encoding deadline.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec instance to configure.
 * @param   enmDeadline         Recording deadline setting to apply.
 */
static int recordingCodecVPXSetDeadline(PRECORDINGCODEC pCodec, RecordingCodecDeadline_T enmDeadline)
{
    PRECORDINGCODECVPX pVPX = &pCodec->Video.VPX;

    switch (enmDeadline)
    {
        case RecordingCodecDeadline_Default:
        case RecordingCodecDeadline_Realtime:
            pVPX->uEncoderDeadline = VPX_DL_REALTIME;
            break;

        case RecordingCodecDeadline_Good:
            AssertStmt(pCodec->Parms.u.Video.uFPS, pCodec->Parms.u.Video.uFPS = 25);
            pVPX->uEncoderDeadline = 1000000 / pCodec->Parms.u.Video.uFPS;
            break;

        case RecordingCodecDeadline_Best:
            pVPX->uEncoderDeadline = VPX_DL_BEST_QUALITY;
            break;

        default:
            AssertMsgFailedReturn(("Invalid recording codec deadline %d\n", enmDeadline), VERR_INVALID_PARAMETER);
    }

    return VINF_SUCCESS;
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
static DECLCALLBACK(int) recordingCodecVPXEncode(PRECORDINGCODEC pCodec, const PRECORDINGFRAME pFrame,
                                                 uint64_t msTimestamp, void *pvUser)
{
    RT_NOREF(pvUser);

    LogFlowFuncEnter();

    Assert(pFrame->enmType == RECORDINGFRAME_TYPE_VIDEO);
    Assert(pFrame->u.Video.Info.enmPixelFmt == RECORDINGPIXELFMT_YUVI420);

    recordingCodecLock(pCodec);

    PRECORDINGCODECVPX pVPX = &pCodec->Video.VPX;

    /* First things first: Do we need to encode anything at the given point in time? */
    bool fSkipEncoding = !recordingCodecVPXShouldEncode(pCodec, msTimestamp);
    if (fSkipEncoding)
    {
        /* Large updates can be split into multiple RECORDINGFRAME_TYPE_VIDEO chunks carrying
         * the same timestamp. If we drop all chunks after the first one, the composed frame can
         * end up incomplete (for example, missing bottom tiles in full-screen updates).
         *
         * Allow equal-timestamp video chunks to be encoded so tiled updates get fully committed. */
        if (   pCodec->State.fHaveWrittenFrame
            && msTimestamp == pCodec->State.tsLastWrittenMs)
            fSkipEncoding = false;
    }

    int vrc;
    if (!fSkipEncoding)
    {
        if (pFrame)
        {
            Assert(pFrame->enmType == RECORDINGFRAME_TYPE_VIDEO);
            Assert(pFrame->u.Video.Info.enmPixelFmt == RECORDINGPIXELFMT_YUVI420);
            AssertPtr(pFrame->u.Video.pau8Buf);

            size_t const cbReq = (size_t)pCodec->Parms.u.Video.uWidth * pCodec->Parms.u.Video.uHeight * 3 / 2;
            if (pFrame->u.Video.cbBuf < cbReq)
                vrc = VERR_BUFFER_OVERFLOW;
            else
            {
                memcpy(pVPX->pu8YuvBuf, pFrame->u.Video.pau8Buf, cbReq);
                vrc = VINF_SUCCESS;
            }
        }
        else /* Encode current raw image. */
            vrc = VINF_SUCCESS;

        if (vrc == VINF_SUCCESS)
            vrc = recordingCodecVPXEncodeWorker(pCodec, &pVPX->RawImage, msTimestamp);
    }
    else
        vrc = VWRN_RECORDING_ENCODING_SKIPPED;

    recordingCodecUnlock(pCodec);

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

#ifdef LOG_ENABLED
    size_t cBlocksEncoded = 0;
#endif
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
#ifdef LOG_ENABLED
            cBlocksEncoded++;
#endif
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
 * @param   ScreenSettings      Recording screen settings to use for initialization.
 */
static int recordingCodecInitAudio(const PRECORDINGCODEC pCodec, const PRECORDINGCODECCALLBACKS pCallbacks,
                                   const ComPtr<IRecordingScreenSettings> &ScreenSettings)
{
    LogRel(("Recording: Initializing audio codec '%s'\n", RecordingUtilsAudioCodecToStr(pCodec->Parms.Common.u.enmAudioCodec)));

    const PPDMAUDIOPCMPROPS pPCMProps = &pCodec->Parms.u.Audio.PCMProps;

    ULONG uBits;
    HRESULT hrc = ScreenSettings->COMGETTER(AudioBits)(&uBits);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    ULONG cChannels;
    hrc = ScreenSettings->COMGETTER(AudioChannels)(&cChannels);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    ULONG uHz;
    hrc = ScreenSettings->COMGETTER(AudioHz)(&uHz);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    com::Bstr bstrOpts;
    hrc = ScreenSettings->COMGETTER(Options)(bstrOpts.asOutParam());
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);

    PDMAudioPropsInit(pPCMProps, uBits / 8, true /* fSigned */, cChannels, uHz);
    pCodec->Parms.uBitrate = 0; /** @todo No bitrate management for audio yet. */

    if (pCallbacks)
        memcpy(&pCodec->Callbacks, pCallbacks, sizeof(RECORDINGCODECCALLBACKS));

    int vrc = VINF_SUCCESS;

    if (pCodec->Ops.pfnParseOptions)
        vrc = pCodec->Ops.pfnParseOptions(pCodec, com::Utf8Str(bstrOpts).c_str());

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
 * @param   ScreenSettings      Recording screen settings to use for initialization.
 */
static int recordingCodecInitVideo(const PRECORDINGCODEC pCodec, const PRECORDINGCODECCALLBACKS pCallbacks,
                                   const ComPtr<IRecordingScreenSettings> &ScreenSettings)
{
    LogRel(("Recording: Initializing video codec '%s'\n", RecordingUtilsVideoCodecToStr(pCodec->Parms.Common.u.enmVideoCodec)));

    ULONG uRate;
    HRESULT hrc = ScreenSettings->COMGETTER(VideoRate)(&uRate);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    ULONG uFPS;
    hrc = ScreenSettings->COMGETTER(VideoFPS)(&uFPS);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    ULONG uWidth;
    hrc = ScreenSettings->COMGETTER(VideoWidth)(&uWidth);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    ULONG uHeight;
    hrc = ScreenSettings->COMGETTER(VideoHeight)(&uHeight);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    RecordingRateControlMode_T enmRateCtlMode;
    hrc = ScreenSettings->COMGETTER(VideoRateControlMode)(&enmRateCtlMode);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    RecordingCodecDeadline_T enmDeadline;
    hrc = ScreenSettings->COMGETTER(VideoDeadline)(&enmDeadline);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);

    AssertReturn(uRate > 0 && uRate <= 1000000, VERR_INVALID_PARAMETER);
    AssertReturn(uFPS > 0 && uFPS <= RT_MS_1SEC, VERR_INVALID_PARAMETER);
    AssertReturn(uWidth >= 2 && uWidth <= VBOX_RECORDING_VIDEO_MAX_WIDTH && !(uWidth & 1), VERR_INVALID_PARAMETER);
    AssertReturn(uHeight >= 2 && uHeight <= VBOX_RECORDING_VIDEO_MAX_HEIGHT && !(uHeight & 1), VERR_INVALID_PARAMETER);

    pCodec->Parms.uBitrate         = uRate;
    pCodec->Parms.u.Video.uFPS     = uFPS;
    pCodec->Parms.u.Video.enmRateCtlMode = enmRateCtlMode;
    pCodec->Parms.u.Video.uWidth   = uWidth;
    pCodec->Parms.u.Video.uHeight  = uHeight;
    pCodec->Parms.u.Video.uDelayMs = RT_MS_1SEC / pCodec->Parms.u.Video.uFPS;

    if (pCallbacks)
        memcpy(&pCodec->Callbacks, pCallbacks, sizeof(RECORDINGCODECCALLBACKS));

    AssertReturn(pCodec->Parms.u.Video.uDelayMs, VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;

#ifdef VBOX_WITH_LIBVPX
    if (pCodec->Parms.Common.u.enmVideoCodec == RecordingVideoCodec_VP8)
        vrc = recordingCodecVPXSetDeadline(pCodec, enmDeadline);
#else
    RT_NOREF(enmDeadline);
#endif

    if (   RT_SUCCESS(vrc)
        && pCodec->Ops.pfnParseOptions)
    {
        com::Bstr bstrOptions;
        hrc = ScreenSettings->COMGETTER(Options)(bstrOptions.asOutParam());
        AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);

        vrc = pCodec->Ops.pfnParseOptions(pCodec, com::Utf8Str(bstrOptions).c_str());
    }

    if (   RT_SUCCESS(vrc)
        && pCodec->Ops.pfnInit)
        vrc = pCodec->Ops.pfnInit(pCodec);

    if (RT_FAILURE(vrc))
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
    AssertReturn(pCodec->Parms.Common.enmType == RECORDINGCODECTYPE_AUDIO, VERR_INVALID_PARAMETER);

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

/**
 * Resets codec runtime state.
 *
 * @param   pCodec              Codec instance to reset.
 */
static void recordingCodecReset(PRECORDINGCODEC pCodec)
{
    pCodec->State.tsLastWrittenMs = 0;
    pCodec->State.cEncErrors = 0;
    pCodec->State.fHaveWrittenFrame = false;

    if (pCodec->Ops.pfnReset)
        pCodec->Ops.pfnReset(pCodec);
}

/**
 * Creates an audio codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec instance to create.
 * @param   enmAudioCodec       Audio codec to create.
 */
static int recordingCodecCreateAudio(PRECORDINGCODEC pCodec, RecordingAudioCodec_T enmAudioCodec)
{
    int vrc;

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
            AssertFailedBreakStmt(vrc = VERR_RECORDING_CODEC_NOT_SUPPORTED);
            break;
    }

    if (RT_SUCCESS(vrc))
    {
        pCodec->Parms.Common.enmType         = RECORDINGCODECTYPE_AUDIO;
        pCodec->Parms.Common.u.enmAudioCodec = enmAudioCodec;
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
static int recordingCodecCreateVideo(PRECORDINGCODEC pCodec, RecordingVideoCodec_T enmVideoCodec)
{
    int vrc;

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

            vrc = VINF_SUCCESS;
            break;
        }
# endif /* VBOX_WITH_LIBVPX */

        default:
            AssertFailedBreakStmt(vrc = VERR_RECORDING_CODEC_NOT_SUPPORTED);
            break;
    }

    if (RT_SUCCESS(vrc))
    {
        pCodec->Parms.Common.enmType         = RECORDINGCODECTYPE_VIDEO;
        pCodec->Parms.Common.u.enmVideoCodec = RecordingVideoCodec_VP8; /** @todo No VP9 yet. */

        switch (enmVideoCodec)
        {
# ifdef VBOX_WITH_LIBVPX
            case RecordingVideoCodec_VP8:
                pCodec->Cfg.enmPixelFmt = RECORDINGPIXELFMT_YUVI420;
                break;
# endif
            default:
                pCodec->Cfg.enmPixelFmt = RECORDINGPIXELFMT_UNKNOWN;
                break;
        }
    }

    return vrc;
}

int RecordingCodecCreate(PRECORDINGCODEC pCodec, PRECORDINGCODECCREATEPARMS pParms)
{
    RT_ZERO(pCodec->Ops);
    RT_ZERO(pCodec->Callbacks);

    int vrc;

    if (pParms->enmType == RECORDINGCODECTYPE_VIDEO)
        vrc = recordingCodecCreateVideo(pCodec, pParms->u.enmVideoCodec);
    else if (pParms->enmType == RECORDINGCODECTYPE_AUDIO)
        vrc = recordingCodecCreateAudio(pCodec, pParms->u.enmAudioCodec);
    else
        AssertFailedReturn(VERR_NOT_IMPLEMENTED);

    return vrc;
}

/**
 * Initializes a codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to initialize.
 * @param   pCallbacks          Codec callback table to use. Optional and may be NULL.
 * @param   ScreenSettings      Screen settings to use for initializing the codec.
 */
int RecordingCodecInit(const PRECORDINGCODEC pCodec, const PRECORDINGCODECCALLBACKS pCallbacks,
                       const ComPtr<IRecordingScreenSettings> &ScreenSettings)
{
    int vrc = RTCritSectInit(&pCodec->CritSect);
    AssertRCReturn(vrc, vrc);

    pCodec->cbScratch = 0;
    pCodec->pvScratch = NULL;

    recordingCodecReset(pCodec);

    if (pCodec->Parms.Common.enmType == RECORDINGCODECTYPE_AUDIO)
        vrc = recordingCodecInitAudio(pCodec, pCallbacks, ScreenSettings);
    else if (pCodec->Parms.Common.enmType == RECORDINGCODECTYPE_VIDEO)
        vrc = recordingCodecInitVideo(pCodec, pCallbacks, ScreenSettings);
    else
        AssertFailedStmt(vrc = VERR_NOT_SUPPORTED);

    return vrc;
}

/**
 * Destroys the codec.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to destroy.
 */
int RecordingCodecDestroy(PRECORDINGCODEC pCodec)
{
    if (pCodec->Parms.Common.enmType == RECORDINGCODECTYPE_INVALID)
        return VINF_SUCCESS;

    int vrc = VINF_SUCCESS;

    if (pCodec->Ops.pfnDestroy)
        vrc = pCodec->Ops.pfnDestroy(pCodec);

    if (RT_SUCCESS(vrc))
    {
        if (pCodec->pvScratch)
        {
            Assert(pCodec->cbScratch);
            RTMemFree(pCodec->pvScratch);
            pCodec->pvScratch = NULL;
            pCodec->cbScratch = 0;
        }

        pCodec->Parms.Common.enmType         = RECORDINGCODECTYPE_INVALID;
        pCodec->Parms.Common.u.enmVideoCodec = RecordingVideoCodec_None;

        int vrc2 = RTCritSectDelete(&pCodec->CritSect);
        AssertRC(vrc2);
    }

    return vrc;
}

/**
 * Returns the public codec configuration.
 */
PCRECORDINGCODECCFG RecordingCodecGetConfig(const PRECORDINGCODEC pCodec)
{
    AssertPtrReturn(pCodec, NULL);
    return &pCodec->Cfg;
}

/**
 * Triggers encoding the currently built frame.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to use.
 * @param   pFrame              Pointer to frame data to encode.
 * @param   msTimestamp         Timestamp (PTS) to use for encoding.
 * @param   pvUser              User data pointer. Optional and can be NULL.
 */
int RecordingCodecEncode(PRECORDINGCODEC pCodec, const PRECORDINGFRAME pFrame, uint64_t msTimestamp, void *pvUser)
{
    int vrc = pCodec->Ops.pfnEncode(pCodec, pFrame, msTimestamp, pvUser);
    if (vrc == VINF_SUCCESS)
    {
        pCodec->State.tsLastWrittenMs   = msTimestamp;
        pCodec->State.fHaveWrittenFrame = true;
    }

    return vrc;
}

/**
 * Lets the codec know that a screen change has happened.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec to use.
 * @param   pInfo               Screen info to send.
 */
int RecordingCodecScreenChange(PRECORDINGCODEC pCodec, PRECORDINGSURFACEINFO pInfo)
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
int RecordingCodecFinalize(PRECORDINGCODEC pCodec)
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
bool RecordingCodecIsInitialized(const PRECORDINGCODEC pCodec)
{
    return pCodec && pCodec->Ops.pfnInit != NULL; /* pfnInit acts as a beacon for initialization status. */
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
uint32_t RecordingCodecGetWritable(const PRECORDINGCODEC pCodec, uint64_t msTimestamp)
{
    AssertPtrReturn(pCodec, 0);

    uint64_t msDelay = 0;
    if (pCodec->Parms.Common.enmType == RECORDINGCODECTYPE_VIDEO)
        msDelay = pCodec->Parms.u.Video.uDelayMs;
    else if (pCodec->Parms.Common.enmType == RECORDINGCODECTYPE_AUDIO)
        msDelay = pCodec->Parms.msFrame;
    else
        return 0;

    Log3Func(("%RU64 -- tsLastWrittenMs=%RU64 + msDelay=%RU64\n",
              msTimestamp, pCodec->State.tsLastWrittenMs, msDelay));

    if (   pCodec->State.fHaveWrittenFrame
        && msTimestamp < pCodec->State.tsLastWrittenMs + msDelay)
        return 0; /* Too early for writing (respect set pacing). */

    /* For now we just return the complete frame space. */
    AssertMsg(pCodec->Parms.cbFrame, ("Codec not initialized yet\n"));
    return pCodec->Parms.cbFrame;
}

/**
 * Returns the next writable deadline (time to wait in ms until writable).
 *
 * @returns Deadline in ms until writable. 0 means writable now.
 * @param   pCodec              Codec instance.
 * @param   msTimestamp         Current timestamp (PTS, in ms).
 */
RTMSINTERVAL RecordingCodecGetDeadlineMs(const PRECORDINGCODEC pCodec, uint64_t msTimestamp)
{
    AssertPtrReturn(pCodec, 0);

    uint64_t msDelay = 0;
    if (pCodec->Parms.Common.enmType == RECORDINGCODECTYPE_VIDEO)
        msDelay = pCodec->Parms.u.Video.uDelayMs;
    else if (pCodec->Parms.Common.enmType == RECORDINGCODECTYPE_AUDIO)
        msDelay = pCodec->Parms.msFrame;
    else
        return 0;

    if (   !pCodec->State.fHaveWrittenFrame
        || msDelay == 0)
        return 0;

    uint64_t const msNext = pCodec->State.tsLastWrittenMs + msDelay;
    if (msTimestamp >= msNext)
        return 0;

    uint64_t const msWait = msNext - msTimestamp;
    return (RTMSINTERVAL)RT_MIN(msWait, (uint64_t)UINT32_MAX);
}
