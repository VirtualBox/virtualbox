/* $Id: RecordingInternals.h 109818 2025-06-11 08:31:50Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording internals header.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_RecordingInternals_h
#define MAIN_INCLUDED_RecordingInternals_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <list>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/types.h> /* drag in stdint.h before vpx does it. */

#include "VBox/com/string.h"
#include "VBox/com/VirtualBox.h"
#include "VBox/settings.h"
#include <VBox/vmm/pdmaudioifs.h>

#ifdef VBOX_WITH_LIBVPX
# define VPX_CODEC_DISABLE_COMPAT 1
# include "vpx/vp8cx.h"
# include "vpx/vpx_image.h"
# include "vpx/vpx_encoder.h"
#endif /* VBOX_WITH_LIBVPX */

#ifdef VBOX_WITH_LIBVORBIS
# include "vorbis/vorbisenc.h"
#endif


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/
#define VBOX_RECORDING_VORBIS_HZ_MAX             48000   /**< Maximum sample rate (in Hz) Vorbis can handle. */
#define VBOX_RECORDING_VORBIS_FRAME_MS_DEFAULT   20      /**< Default Vorbis frame size (in ms). */


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
struct RECORDINGCODEC;
typedef RECORDINGCODEC *PRECORDINGCODEC;

struct RECORDINGFRAME;
typedef RECORDINGFRAME *PRECORDINGFRAME;


/*********************************************************************************************************************************
*   Internal structures, defines and APIs                                                                                        *
*********************************************************************************************************************************/

/**
 * Enumeration for supported pixel formats.
 */
enum RECORDINGPIXELFMT
{
    /** Unknown pixel format. */
    RECORDINGPIXELFMT_UNKNOWN    = 0,
    /** BRGA 32. */
    RECORDINGPIXELFMT_BRGA32     = 1,
    /** The usual 32-bit hack. */
    RECORDINGPIXELFMT_32BIT_HACK = 0x7fffffff
};

/**
 * Structure for keeping recording surface information.
 */
typedef struct RECORDINGSURFACEINFO
{
    /** Width (in pixel). */
    uint32_t          uWidth;
    /** Height (in pixel). */
    uint32_t          uHeight;
    /** Bits per pixel. */
    uint8_t           uBPP;
    /** Pixel format. */
    RECORDINGPIXELFMT enmPixelFmt;
    /** Bytes per scan line (stride).
     *  Note: Does not necessarily match \a uWidth * (\a uBPP / 8). */
    uint32_t          uBytesPerLine;
} RECORDINGSURFACEINFO;
/** Pointer to recording surface information. */
typedef RECORDINGSURFACEINFO *PRECORDINGSURFACEINFO;

/**
 * Structure for keeping recording relative or absolute position information.
 */
typedef struct RECORDINGPOS
{
    uint32_t x;
    uint32_t y;
} RECORDINGPOS;
/** Pointer to a RECORDINGRECT. */
typedef RECORDINGPOS *PRECORDINGPOS;

/** No flags set. */
#define RECORDINGVIDEOFRAME_F_NONE       UINT32_C(0)
/** Frame is visible. */
#define RECORDINGVIDEOFRAME_F_VISIBLE    RT_BIT(0)
/** Use blitting with alpha blending. */
#define RECORDINGVIDEOFRAME_F_BLIT_ALPHA RT_BIT(1)
/** Validation mask. */
#define RECORDINGVIDEOFRAME_F_VALID_MASK 0x3

/**
 * Structure for keeping a single recording video frame.
 */
typedef struct RECORDINGVIDEOFRAME
{
    /** Surface information of this frame. */
    RECORDINGSURFACEINFO Info;
    /** Pixel data buffer. */
    uint8_t             *pau8Buf;
    /** Size (in bytes) of \a pau8Buf. */
    size_t               cbBuf;
    /** X / Y positioning hint (in pixel) of this frame.
     *  Note: This does *not* mean offset within \a pau8Buf! */
    RECORDINGPOS         Pos;
    /** Recording video frame flags of type RECORDINGVIDEOFRAME_F_XXX. */
    uint32_t             fFlags;
} RECORDINGVIDEOFRAME;
/** Pointer to a video recording frame. */
typedef RECORDINGVIDEOFRAME *PRECORDINGVIDEOFRAME;

/**
 * Enumeration for supported scaling methods.
 */
enum RECORDINGSCALINGMETHOD
{
    /** No scaling applied.
     *  Bigger frame buffers will be cropped (centered),
     *  smaller frame buffers will be centered. */
    RECORDINGSCALINGMETHOD_NONE       = 0,
    /** The usual 32-bit hack. */
    RECORDINGSCALINGMETHOD_32BIT_HACK = 0x7fffffff
};

/**
 * Structure for keeping recording scaling information.
 */
typedef struct RECORDINGSCALINGINFO
{
    /** The scaling method to use. */
    RECORDINGSCALINGMETHOD enmMethod;
    /** Union based on \a enmMethod. */
    union
    {
        /** Cropping information. */
        struct
        {
            /** X origin.
             *  If negative, the frame buffer will be cropped with centering applied,
             *  if positive, the frame buffer will be centered. */
            int32_t m_iOriginX;
            /** Y origin. *
             * If negative, the frame buffer will be cropped with centering applied,
             * if positive, the frame buffer will be centered. */
            int32_t m_iOriginY;
        } Crop;
    } u;
} RECORDINGSCALINGINFO;
/** Pointer to a recording scaling information. */
typedef RECORDINGSCALINGINFO *PRECORDINGSCALINGINFO;

/**
 * Enumeration for specifying a (generic) codec type.
 */
typedef enum RECORDINGCODECTYPE
{
    /** Invalid codec type. Do not use. */
    RECORDINGCODECTYPE_INVALID = 0,
    /** Video codec. */
    RECORDINGCODECTYPE_VIDEO,
    /** Audio codec. */
    RECORDINGCODECTYPE_AUDIO
} RECORDINGCODECTYPE;

/**
 * Structure for keeping a codec operations table.
 */
typedef struct RECORDINGCODECOPS
{
    /**
     * Initializes a codec.
     *
     * @returns VBox status code.
     * @param   pCodec              Codec instance to initialize.
     */
    DECLCALLBACKMEMBER(int, pfnInit,         (PRECORDINGCODEC pCodec));

    /**
     * Destroys a codec.
     *
     * @returns VBox status code.
     * @param   pCodec              Codec instance to destroy.
     */
    DECLCALLBACKMEMBER(int, pfnDestroy,      (PRECORDINGCODEC pCodec));

    /**
     * Parses an options string to configure advanced / hidden / experimental features of a recording stream.
     * Unknown values will be skipped. Optional.
     *
     * @returns VBox status code.
     * @param   pCodec              Codec instance to parse options for.
     * @param   strOptions          Options string to parse.
     */
    DECLCALLBACKMEMBER(int, pfnParseOptions,  (PRECORDINGCODEC pCodec, const com::Utf8Str &strOptions));

    /**
     * Feeds the codec encoder with data to encode.
     *
     * @returns VBox status code.
     * @param   pCodec              Codec instance to use.
     * @param   pFrame              Pointer to frame data to encode. Optional and can be NULL.
     * @param   msTimestamp         Timestamp (PTS) of frame to encode.
     * @param   pvUser              User data pointer. Optional and can be NULL.
     */
    DECLCALLBACKMEMBER(int, pfnEncode,       (PRECORDINGCODEC pCodec, const PRECORDINGFRAME pFrame, uint64_t msTimestamp, void *pvUser));

    /**
     * Tells the codec about a screen change. Optional.
     *
     * @returns VBox status code.
     * @param   pCodec              Codec instance to use.
     * @param   pInfo               Screen information to send.
     */
    DECLCALLBACKMEMBER(int, pfnScreenChange, (PRECORDINGCODEC pCodec, PRECORDINGSURFACEINFO pInfo));

    /**
     * Tells the codec to finalize the current stream. Optional.
     *
     * @returns VBox status code.
     * @param   pCodec              Codec instance to finalize stream for.
     */
    DECLCALLBACKMEMBER(int, pfnFinalize,     (PRECORDINGCODEC pCodec));
} RECORDINGCODECOPS, *PRECORDINGCODECOPS;

/** No encoding flags set. */
#define RECORDINGCODEC_ENC_F_NONE               UINT32_C(0)
/** Data block is a key block. */
#define RECORDINGCODEC_ENC_F_BLOCK_IS_KEY       RT_BIT_32(0)
/** Data block is invisible. */
#define RECORDINGCODEC_ENC_F_BLOCK_IS_INVISIBLE RT_BIT_32(1)
/** Encoding flags valid mask. */
#define RECORDINGCODEC_ENC_F_VALID_MASK         0x3

/**
 * Structure for keeping a codec callback table.
 */
typedef struct RECORDINGCODECCALLBACKS
{
    /**
     * Callback for notifying that encoded data has been written.
     *
     * @returns VBox status code.
     * @param   pCodec          Pointer to codec instance which has written the data.
     * @param   pvData          Pointer to written data (encoded).
     * @param   cbData          Size (in bytes) of \a pvData.
     * @param   msAbsPTS        Absolute PTS (in ms) of the written data.
     * @param   uFlags          Encoding flags of type RECORDINGCODEC_ENC_F_XXX.
     * @param   pvUser          User-supplied pointer.
     */
    DECLCALLBACKMEMBER(int, pfnWriteData, (PRECORDINGCODEC pCodec, const void *pvData, size_t cbData, uint64_t msAbsPTS, uint32_t uFlags, void *pvUser));
    /** User-supplied data pointer. */
    void                   *pvUser;
} RECORDINGCODECCALLBACKS, *PRECORDINGCODECCALLBACKS;

/**
 * Structure for keeping generic codec parameters.
 */
typedef struct RECORDINGCODECPARMS
{
    /** The generic codec type. */
    RECORDINGCODECTYPE          enmType;
    /** The specific codec type, based on \a enmType. */
    union
    {
        /** The container's video codec to use. */
        RecordingVideoCodec_T   enmVideoCodec;
        /** The container's audio codec to use. */
        RecordingAudioCodec_T   enmAudioCodec;
    };
    union
    {
        struct
        {
            /** Frames per second. */
            uint8_t              uFPS;
            /** Target width (in pixels) of encoded video image. */
            uint32_t             uWidth;
            /** Target height (in pixels) of encoded video image. */
            uint32_t             uHeight;
            /** Minimal delay (in ms) between two video frames.
             *  This value is based on the configured FPS rate. */
            uint32_t             uDelayMs;
            /** Scaling information. */
            RECORDINGSCALINGINFO Scaling;
        } Video;
        struct
        {
            /** The codec's used PCM properties. */
            PDMAUDIOPCMPROPS    PCMProps;
        } Audio;
    } u;
    /** Desired (average) bitrate (in kbps) to use, for codecs which support bitrate management.
     *  Set to 0 to use a variable bit rate (VBR) (if available, otherwise fall back to CBR). */
    uint32_t                    uBitrate;
    /** Time (in ms) the encoder expects us to send data to encode.
     *
     *  For Vorbis, valid frame sizes are powers of two from 64 to 8192 bytes.
     */
    uint32_t                    msFrame;
    /** The frame size in bytes (based on \a msFrame). */
    uint32_t                    cbFrame;
    /** The frame size in samples per frame (based on \a msFrame). */
    uint32_t                    csFrame;
} RECORDINGCODECPARMS, *PRECORDINGCODECPARMS;

#ifdef VBOX_WITH_LIBVPX
/**
 * VPX encoder state (needs libvpx).
 */
typedef struct RECORDINGCODECVPX
{
    /** VPX codec context. */
    vpx_codec_ctx_t     Ctx;
    /** VPX codec configuration. */
    vpx_codec_enc_cfg_t Cfg;
    /** VPX image context. */
    vpx_image_t         RawImage;
    /** Pointer to the codec's internal YUV buffer.
     *  VP8 works exclusively with an 8-bit YUV 4:2:0 image, so frame packed, where U and V are half resolution images. */
    uint8_t            *pu8YuvBuf;
    /** The encoder's deadline (in ms).
     *  The more time the encoder is allowed to spend encoding, the better the encoded
     *  result, in exchange for higher CPU usage and time spent encoding. */
    unsigned int        uEncoderDeadline;
    /** Front buffer which is going to be encoded.
     *  Matches Main's framebuffer pixel format for faster / easier conversion.
     *  Not necessarily the same size as the encoder buffer. In such a case a scaling / cropping
     *  operation has to be performed first. */
    RECORDINGVIDEOFRAME Front;
    /** Back buffer which holds the framebuffer data before we blit anything to it.
     *  Needed for mouse cursor handling, if the mouse cursor is not actually part of the framebuffer data.
     *  Always matches the front buffer (\a Front) in terms of size. */
    RECORDINGVIDEOFRAME Back;
    /** The current cursor shape to use.
     *  Set to NULL if not used (yet). */
    PRECORDINGVIDEOFRAME pCursorShape;
    /** Old cursor position since last cursor position message. */
    RECORDINGPOS        PosCursorOld;

} RECORDINGCODECVPX;
/** Pointer to a VPX encoder state. */
typedef RECORDINGCODECVPX *PRECORDINGCODECVPX;
#endif /* VBOX_WITH_LIBVPX */

#ifdef VBOX_WITH_LIBVORBIS
/**
 * Vorbis encoder state (needs libvorbis + libogg).
 */
typedef struct RECORDINGCODECVORBIS
{
    /** Basic information about the audio in a Vorbis bitstream. */
    vorbis_info      info;
    /** Encoder state. */
    vorbis_dsp_state dsp_state;
    /** Current block being worked on. */
    vorbis_block     block_cur;
} RECORDINGCODECVORBIS;
/** Pointer to a Vorbis encoder state. */
typedef RECORDINGCODECVORBIS *PRECORDINGCODECVORBIS;
#endif /* VBOX_WITH_LIBVORBIS */

/**
 * Structure for keeping a codec's internal state.
 */
typedef struct RECORDINGCODECSTATE
{
    /** Timestamp Timestamp (PTS, in ms) of the last frame was encoded. */
    uint64_t            tsLastWrittenMs;
    /** Number of encoding errors. */
    uint64_t            cEncErrors;
} RECORDINGCODECSTATE;
/** Pointer to an internal encoder state. */
typedef RECORDINGCODECSTATE *PRECORDINGCODECSTATE;

/**
 * Structure for keeping codec-specific data.
 */
typedef struct RECORDINGCODEC
{
    /** Callback table for codec operations. */
    RECORDINGCODECOPS           Ops;
    /** Table for user-supplied callbacks. */
    RECORDINGCODECCALLBACKS     Callbacks;
    /** Generic codec parameters. */
    RECORDINGCODECPARMS         Parms;
    /** Generic codec parameters. */
    RECORDINGCODECSTATE         State;
    /** Crtitical section. */
    RTCRITSECT                  CritSect;

#ifdef VBOX_WITH_LIBVPX
    union
    {
        RECORDINGCODECVPX       VPX;
    } Video;
#endif

#ifdef VBOX_WITH_AUDIO_RECORDING
    union
    {
# ifdef VBOX_WITH_LIBVORBIS
        RECORDINGCODECVORBIS    Vorbis;
# endif /* VBOX_WITH_LIBVORBIS */
    } Audio;
#endif /* VBOX_WITH_AUDIO_RECORDING */

    /** Internal scratch buffer for en-/decoding steps.
     *  Might be NULL if not being used. */
    void               *pvScratch;
    /** Size (in bytes) of \a pvScratch. */
    uint32_t            cbScratch;
} RECORDINGCODEC, *PRECORDINGCODEC;

/**
 * Enumeration for a recording frame type.
 */
enum RECORDINGFRAME_TYPE
{
    /** Invalid frame type; do not use. */
    RECORDINGFRAME_TYPE_INVALID       = 0,
    /** Frame is an audio frame. */
    RECORDINGFRAME_TYPE_AUDIO         = 1,
    /** Frame is an video frame. */
    RECORDINGFRAME_TYPE_VIDEO         = 2,
    /** Frame is a cursor shape frame.
     *  Send when a (mouse) cursor shape has changed. */
    RECORDINGFRAME_TYPE_CURSOR_SHAPE  = 3,
    /** Frame is a cursor position change request.
     *  Sent when a (mouse) cursor position has changed. */
    RECORDINGFRAME_TYPE_CURSOR_POS    = 4,
    /** Screen change information.
     *  Sent when the screen properties (resolution, BPP, ...) have changed. */
    RECORDINGFRAME_TYPE_SCREEN_CHANGE = 5
};

/**
 * Structure for keeping a single recording audio frame.
 */
typedef struct RECORDINGAUDIOFRAME
{
    /** Pointer to audio data. */
    uint8_t            *pvBuf;
    /** Size (in bytes) of audio data. */
    size_t              cbBuf;
} RECORDINGAUDIOFRAME, *PRECORDINGAUDIOFRAME;

/**
 * Structure for keeping cursor information.
 */
typedef struct RECORDINGCURSORINFO
{
    /** Cursor ID. Currently always 0. */
    uint8_t          Id;
    /** Current position. */
    RECORDINGPOS     Pos;
} RECORDINGCURSORINFO;
/** Pointer to a RECORDINGCURSORINFO. */
typedef RECORDINGCURSORINFO *PRECORDINGCURSORINFO;

/**
 * Structure for keeping a single recording frame.
 */
typedef struct RECORDINGFRAME
{
    /** List node. */
    RTLISTNODE              Node;
    /** Stream index (hint) where this frame should go to.
     *  Specify UINT16_MAX to broadcast to all streams. */
    uint16_t                idStream;
    /** The frame type. */
    RECORDINGFRAME_TYPE     enmType;
    /** Timestamp (PTS, in ms). */
    uint64_t                msTimestamp;
    /** Union holding different frame types depending on \a enmType. */
    union
    {
#ifdef VBOX_WITH_AUDIO_RECORDING
        /** Audio data.
         *  Used by RECORDINGFRAME_TYPE_AUDIO. */
        RECORDINGAUDIOFRAME  Audio;
#endif
        /** A (weak) pointer to a video frame. */
        RECORDINGVIDEOFRAME  Video;
        /** Recording screen information. */
        RECORDINGSURFACEINFO ScreenInfo;
        /** Cursor shape frame.
         *  Used by RECORDINGFRAME_TYPE_CURSOR_SHAPE. */
        RECORDINGVIDEOFRAME  CursorShape;
        /** Cursor information.
         *  Used by RECORDINGFRAME_TYPE_CURSOR_POS. */
        RECORDINGCURSORINFO  Cursor;
    } u;
} RECORDINGFRAME;
/** Pointer to a RECORDINGFRAME. */
typedef RECORDINGFRAME *PRECORDINGFRAME;

PRECORDINGVIDEOFRAME RecordingVideoFrameAlloc(void);
PRECORDINGVIDEOFRAME RecordingVideoFrameAllocEx(const void *pvData, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t uBPP, RECORDINGPIXELFMT enmFmt);
void RecordingVideoFrameFree(PRECORDINGVIDEOFRAME pFrame);
int RecordingVideoFrameInit(PRECORDINGVIDEOFRAME pFrame, uint32_t fFlags, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t uBPP, RECORDINGPIXELFMT enmFmt);
void RecordingVideoFrameDestroy(PRECORDINGVIDEOFRAME pFrame);
PRECORDINGVIDEOFRAME RecordingVideoFrameDup(PRECORDINGVIDEOFRAME pFrame);
void RecordingVideoFrameClear(PRECORDINGVIDEOFRAME pFrame);
int RecordingVideoFrameBlitRawAlpha(PRECORDINGVIDEOFRAME pDstFrame, uint32_t uDstX, uint32_t uDstY, const uint8_t *pu8Src, size_t cbSrc, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight, uint32_t uSrcBytesPerLine, uint8_t uSrcBPP, RECORDINGPIXELFMT enmFmt);
int RecordingVideoBlitRaw(uint8_t *pu8Dst, size_t cbDst, uint32_t uDstX, uint32_t uDstY, uint32_t uDstBytesPerLine, uint8_t uDstBPP, RECORDINGPIXELFMT enmDstFmt, const uint8_t *pu8Src, size_t cbSrc, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight, uint32_t uSrcBytesPerLine, uint8_t uSrcBPP, RECORDINGPIXELFMT enmSrcFmt);
int RecordingVideoFrameBlitRaw(PRECORDINGVIDEOFRAME pDstFrame, uint32_t uDstX, uint32_t uDstY, const uint8_t *pu8Src, size_t cbSrc, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight, uint32_t uSrcBytesPerLine, uint8_t uSrcBPP, RECORDINGPIXELFMT enmFmt);
int RecordingVideoFrameBlitFrame(PRECORDINGVIDEOFRAME pDstFrame, uint32_t uDstX, uint32_t uDstY, PRECORDINGVIDEOFRAME pSrcFrame, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight);

#ifdef VBOX_WITH_AUDIO_RECORDING
void RecordingAudioFrameFree(PRECORDINGAUDIOFRAME pFrame);
#endif

void RecordingFrameFree(PRECORDINGFRAME pFrame);

/**
 * Generic structure for keeping a single video recording (data) block.
 */
struct RecordingBlock
{
    RecordingBlock()
        : cRefs(0)
        , uFlags(RECORDINGCODEC_ENC_F_NONE)
        , pvData(NULL)
        , cbData(0) { }

    virtual ~RecordingBlock()
    {
        Destroy();
    }

    /**
     * Returns the current reference count.
     *
     * @returns  Number of current references.
     */
    uint64_t GetRefs(void)
    {
        Assert(cRefs <= 4); /* Helps finding refcounting bugs. Value chosen at random. */
        return ASMAtomicReadU64(&cRefs);
    }

    /**
     * Adds a reference to a recording block.
     *
     * @returns  Number of new references.
     */
    uint64_t AddRef(void)
    {
        Assert(cRefs <= 4); /* Helps finding refcounting bugs. Value chosen at random. */
        return ASMAtomicIncU64(&cRefs);
    }

    /**
     * Releases a reference to a recording block.
     *
     * @returns  Number of new references after release.
     */
    uint64_t Release(void)
    {
        Assert(cRefs);
        return ASMAtomicDecU64(&cRefs);
    }

    /**
     * Unlinks the stored data (giving up ownership).
     *
     * @note All references to this recording block must be released first.
     *
     * @returns Pointer to unlinked data. Can be NULL if no data stored.
     * @param   pcbData         Where to return the size (in bytes) of the returned data. Optional and can be NULL.
     */
    void *Unlink(size_t *pcbData)
    {
        AssertMsgReturn(cRefs == 0, ("Can't unlink data, as there are references to it (%RU64)\n", cRefs), NULL);

        void *pvDataUnlinked = pvData;
        if (pcbData)
            *pcbData = cbData;

        pvData  = NULL;
        cbData  = 0;

        return pvDataUnlinked;
    }

    /**
     * Destroys a recording block.
     *
     * @note Must be released via Release() first.
     */
    void Destroy(void)
    {
        AssertMsgReturnVoid(cRefs == 0, ("Recording block still holds references (%RU64)\n", cRefs));
        RecordingFrameFree((PRECORDINGFRAME)pvData);

        cRefs   = 0;
        pvData  = NULL;
        cbData  = 0;
    }

    /** Number of references held of this block. */
    uint64_t           cRefs;
    /** Block flags of type RECORDINGCODEC_ENC_F_XXX. */
    uint64_t           uFlags;
    /** The (absolute) timestamp (in ms, PTS) of this block. */
    uint64_t           msTimestamp;
    /** Opaque data block to the actual block data. Currently only used with PRECORDINGFRAME. */
    void              *pvData;
    /** Size (in bytes) of the (opaque) data block. */
    size_t             cbData;
};

/** List for keeping video recording (data) blocks. */
typedef std::list<RecordingBlock *> RecordingBlockList;

int recordingCodecCreateAudio(PRECORDINGCODEC pCodec, RecordingAudioCodec_T enmAudioCodec);
int recordingCodecCreateVideo(PRECORDINGCODEC pCodec, RecordingVideoCodec_T enmVideoCodec);
int recordingCodecInit(const PRECORDINGCODEC pCodec, const PRECORDINGCODECCALLBACKS pCallbacks, const settings::RecordingScreen &Settings);
int recordingCodecDestroy(PRECORDINGCODEC pCodec);
int recordingCodecEncodeFrame(PRECORDINGCODEC pCodec, const PRECORDINGFRAME pFrame, uint64_t msTimestamp, void *pvUser);
int recordingCodecEncodeCurrent(PRECORDINGCODEC pCodec, uint64_t msTimestamp);
int recordingCodecScreenChange(PRECORDINGCODEC pCodec, PRECORDINGSURFACEINFO pInfo);
int recordingCodecFinalize(PRECORDINGCODEC pCodec);
bool recordingCodecIsInitialized(const PRECORDINGCODEC pCodec);
uint32_t recordingCodecGetWritable(const PRECORDINGCODEC pCodec, uint64_t msTimestamp);
#endif /* !MAIN_INCLUDED_RecordingInternals_h */
