/* $Id: RecordingInternals.h 114800 2026-07-27 16:53:06Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording internals header.
 */

/*
 * Copyright (C) 2012-2026 Oracle and/or its affiliates.
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

#ifdef IN_VBOXSVC
# error "Using RecordingInternals.h is prohibited in VBoxSVC!"
#endif

#include <list>
#include <map>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/circbuf.h>
#include <iprt/types.h> /* drag in stdint.h before vpx does it. */

#include "VBox/com/string.h"
#include "VBox/com/VirtualBox.h"
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

#include "RecordingContext.h" /* For RECORDINGPIXELFMT. */


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/
#define VBOX_RECORDING_VORBIS_HZ_MAX             48000   /**< Maximum sample rate (in Hz) Vorbis can handle. */
#define VBOX_RECORDING_VORBIS_FRAME_MS_DEFAULT   20      /**< Default Vorbis frame size (in ms). */

#ifdef DEBUG_andy_disabled
# define VBOX_RECORDING_DEBUG_DUMP_FRAMES
# define VBOX_WITH_RECORDING_STATS
# define VBOX_RECORDING_DEBUG_TILES
#endif

#ifdef DEBUG_DISABLED
 /* Enable the following to get some recording statistics in the log. */
 #define VBOX_WITH_RECORDING_STATS
 /* Dumps video frame data as uncompressed bitmaps to the temporary directory.
  * Warning! This generates *a lot* of data! */
 #define VBOX_RECORDING_DEBUG_DUMP_FRAMES
 /* Dumps tiles as bitmaps to temporary dir and draws a red border around the tiles. */
 #define VBOX_RECORDING_DEBUG_TILES
#endif


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
struct RECORDINGCODEC;
typedef RECORDINGCODEC *PRECORDINGCODEC;

struct RECORDINGFRAME;
typedef RECORDINGFRAME *PRECORDINGFRAME;

struct RECORDINGCODECPARMS;


/*********************************************************************************************************************************
*   Internal structures, defines and APIs                                                                                        *
*********************************************************************************************************************************/

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
/** Pointer to a position information. */
typedef RECORDINGPOS *PRECORDINGPOS;

/** No flags set. */
#define RECORDINGVIDEOFRAME_F_NONE       UINT32_C(0)
/** Frame is visible. */
#define RECORDINGVIDEOFRAME_F_VISIBLE    RT_BIT(0)
/** Use blitting with alpha blending. */
#define RECORDINGVIDEOFRAME_F_BLIT_ALPHA RT_BIT(1)
/** Do not destroy the pixel data.
 *  Might be used if the pixel data is not owned by this frame. Use with care! */
#define RECORDINGVIDEOFRAME_F_NO_DESTROY RT_BIT(2)
/** Validation mask. */
#define RECORDINGVIDEOFRAME_F_VALID_MASK 0x7

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
 * Resize parameters.
 */
typedef struct RECORDINGRENDERRESIZEPARMS
{
    /** Resize operation mode. */
    RecordingVideoScalingMode_T enmMode;
    /** Source rect. */
    RTRECT                      srcRect;
    /** Destination rect. */
    RTRECT                      dstRect;
} RECORDINGRENDERRESIZEPARMS;
/** Pointer to resize parameters. */
typedef RECORDINGRENDERRESIZEPARMS *PRECORDINGRENDERRESIZEPARMS;

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
     * Resets a codec.
     *
     * @param   pCodec              Codec instance to reset.
     */
    DECLCALLBACKMEMBER(int, pfnReset,        (PRECORDINGCODEC pCodec));

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
     * Triggers encoding the currently built frame.
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

typedef struct RECORDINGCODECCREATEPARMS
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
    } u;
} RECORDINGCODECCREATEPARMS;
/** Pointer to codec creation parameters. */
typedef RECORDINGCODECCREATEPARMS *PRECORDINGCODECCREATEPARMS;

/**
 * Structure for keeping generic codec parameters.
 */
typedef struct RECORDINGCODECPARMS
{
    RECORDINGCODECCREATEPARMS    Common;
    union
    {
        struct
        {
            /** Frames per second. */
            uint32_t             uFPS;
            /** Rate control mode. */
            RecordingRateControlMode_T
                                 enmRateCtlMode;
            /** Target width (in pixels) of encoded video image. */
            uint32_t             uWidth;
            /** Target height (in pixels) of encoded video image. */
            uint32_t             uHeight;
            /** Minimal delay (in ms) between two video frames.
             *  This value is based on the configured FPS rate. */
            uint32_t             uDelayMs;
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
} RECORDINGCODECPARMS;
/** Pointer to codec parameters. */
typedef RECORDINGCODECPARMS *PRECORDINGCODECPARMS;

/**
 * Read-only codec configuration exposed to callers.
 */
typedef struct RECORDINGCODECCFG
{
    /** Expected input pixel format for the encoder path. */
    RECORDINGPIXELFMT enmPixelFmt;
} RECORDINGCODECCFG;
/** Pointer to codec configuration. */
typedef RECORDINGCODECCFG *PRECORDINGCODECCFG;
/** Pointer to const codec configuration. */
typedef RECORDINGCODECCFG const *PCRECORDINGCODECCFG;

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
    /** Indicates whether at least one frame has been written already. */
    bool                fHaveWrittenFrame;
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
    /** Public codec configuration. */
    RECORDINGCODECCFG           Cfg;
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
 *
 * Note: For frame pool use, don't leave any (numbering) gaps here.
 *       Also, don't change the order without proper testing first.
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
    RECORDINGFRAME_TYPE_SCREEN_CHANGE = 5,
    /** End marker. Must come last. */
    RECORDINGFRAME_TYPE_MAX
};

/** Maximum number of circular buffer readers.
 *  This must match the maximum recording streams. */
#define RECORDINGCIRCBUF_MAX_READERS 64

/**
 * Structure for maintaining a single recording circular buffer reader.
 */
typedef struct RECORDINGCIRCBUFRDR
{
    /** Current reading position.
     *  Set to UINT64_MAX if not active. */
    volatile uint64_t uReadPos;
} RECORDINGCIRCBUFRDR;
/** Pointer to RECORDINGCIRCBUFRDR. */
typedef RECORDINGCIRCBUFRDR *PRECORDINGCIRCBUFRDR;

/**
 * Circular buffer for supporting multiple readers (consumers)
 * of recording frames. Needed for common (across streams) frame data.
 */
typedef struct RECORDINGCIRCBUF
{
    /** The circular buffer where to keep the data. */
    PRTCIRCBUF          pCircBuf;
    /** Monotonic logical position (in bytes) at underlying RTCircBuf read pointer (writer advances). */
    volatile uint64_t   uBasePos;
    /** Monotonic logical position (in bytes) at end of committed writes (writer publishes). */
    volatile uint64_t   uWritePos;
    /** Reader count and ID generator. */
    volatile uint32_t   cRdr;
    /** Reader list. */
    RECORDINGCIRCBUFRDR aRdr[RECORDINGCIRCBUF_MAX_READERS];
} RECORDINGCIRCBUF;
/** Pointer to RECORDINGCIRCBUF. */
typedef RECORDINGCIRCBUF *PRECORDINGCIRCBUF;

int  RecordingCircBufCreate(PRECORDINGCIRCBUF pBuf, size_t cbSize);
void RecordingCircBufDestroy(PRECORDINGCIRCBUF pCircBuf);
int  RecordingCircBufAddReader(PRECORDINGCIRCBUF pCircBuf, uint32_t *pIdRdr);
int  RecordingCircBufRemoveReader(PRECORDINGCIRCBUF pCircBuf, uint32_t idRdr);
int  RecordingCircBufAcquireWrite(RECORDINGCIRCBUF *pBuf, size_t cbReq, void **ppv, size_t *pcbAcquired);
int  RecordingCircBufReleaseWrite(RECORDINGCIRCBUF *pBuf, size_t cbToCommit);
int  RecordingCircBufAcquireRead(RECORDINGCIRCBUF *pBuf, uint32_t idRdr, size_t cbReq, const void **ppv, size_t *pcbAcquired);
int  RecordingCircBufReleaseRead(RECORDINGCIRCBUF *pBuf, uint32_t idRdr, size_t cbToConsume);
size_t RecordingCircBufReadable(const PRECORDINGCIRCBUF pBuf, uint32_t idRdr);
size_t RecordingCircBufUsed(const PRECORDINGCIRCBUF pBuf);
size_t RecordingCircBufSize(const PRECORDINGCIRCBUF pBuf);

/**
 * Structure for maintaining a recording frame pool.
 *
 * A frame pool consists of an internal ring buffer to avoid (re-)allocation
 * when sending frames to (a) recording stream(s).
 */
typedef struct RECORDINGFRAMEPOOL
{
    /** Pool identifier (currently mirrors @a enmType for logging/debugging). */
    uint8_t             uId;
    /** Backing circular buffer storing fixed-size frame slots. */
    RECORDINGCIRCBUF    CircBuf;
    /** Frame type stored in this pool.
     *  Set to RECORDINGFRAME_TYPE_INVALID when uninitialized. */
    RECORDINGFRAME_TYPE enmType;
    /** Size (in bytes) of one frame slot in @a CircBuf. */
    size_t              cbFrame;
    /** Number of frame slots configured for this pool. */
    size_t              cFrames;
} RECORDINGFRAMEPOOL;
/** Pointer to a RECORDINGFRAMEPOOL. */
typedef RECORDINGFRAMEPOOL *PRECORDINGFRAMEPOOL;

/** Use all readers / raw used occupancy for RecordingFramePoolGetPressure(). */
#define RECORDINGFRAMEPOOL_PRESSURE_READER_ALL UINT32_MAX

int RecordingFramePoolInit(PRECORDINGFRAMEPOOL pPool, RECORDINGFRAME_TYPE enmType, size_t cbFrame, size_t cCapacity);
void RecordingFramePoolDestroy(PRECORDINGFRAMEPOOL pPool);
bool RecordingFramePoolIsInitialized(PRECORDINGFRAMEPOOL pPool);
int  RecordingFramePoolAddReader(PRECORDINGFRAMEPOOL pPool, uint32_t *pIdRdr);
int  RecordingFramePoolRemoveReader(PRECORDINGFRAMEPOOL pPool, uint32_t idRdr);
PRECORDINGFRAME RecordingFramePoolAcquireRead(PRECORDINGFRAMEPOOL pPool, uint32_t idRdr);
void RecordingFramePoolReleaseRead(PRECORDINGFRAMEPOOL pPool, uint32_t idRdr);
PRECORDINGFRAME RecordingFramePoolAcquireWrite(PRECORDINGFRAMEPOOL pPool);
void RecordingFramePoolReleaseWrite(PRECORDINGFRAMEPOOL pPool);
int  RecordingFramePoolReclaim(PRECORDINGFRAMEPOOL pPool);
size_t RecordingFramePoolFrameSize(const PRECORDINGFRAMEPOOL pPool);
size_t RecordingFramePoolFree(const PRECORDINGFRAMEPOOL pPool);
size_t RecordingFramePoolReadable(const PRECORDINGFRAMEPOOL pPool, uint32_t idRdr);
size_t RecordingFramePoolUsed(const PRECORDINGFRAMEPOOL pPool);
size_t RecordingFramePoolSize(const PRECORDINGFRAMEPOOL pPool);
uint8_t RecordingFramePoolGetPressure(const RECORDINGFRAMEPOOL *pPool, uint32_t idRdr);

#ifdef VBOX_WITH_RECORDING_STATS
/**
 * Generic pressure statistics for queue / pool occupancy in percent.
 */
typedef struct RECORDINGPOOLPRESSURESTATS
{
    /** Number of occupancy samples taken. */
    uint64_t cSamples;
    /** Number of samples at >= 50% occupancy. */
    uint64_t cGe50;
    /** Number of samples at >= 75% occupancy. */
    uint64_t cGe75;
    /** Number of samples at >= 90% occupancy. */
    uint64_t cGe90;
    /** Number of samples at >= 100% occupancy. */
    uint64_t cGe100;
    /** Maximum observed occupancy percentage [0..100]. */
    uint64_t uPctMax;
    /** Number of frames dropped because the associated pool/queue was full. */
    uint64_t cDrops;
} RECORDINGPOOLPRESSURESTATS;
/** Pointer to RECORDINGPOOLPRESSURESTATS. */
typedef RECORDINGPOOLPRESSURESTATS *PRECORDINGPOOLPRESSURESTATS;

void RecordingStatsPoolPressureSample(PRECORDINGPOOLPRESSURESTATS pStats, size_t cUsed, size_t cCapacity);
void RecordingStatsPoolPressureLog(const char *pszPrefix, const char *pszPoolName,
                                   const RECORDINGPOOLPRESSURESTATS *pStats,
                                   size_t cUsed, size_t cCapacity);
#endif /* VBOX_WITH_RECORDING_STATS */

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
    /** Number of references held of to this frame. */
    uint64_t                cRefs;
    /** Stream index (hint) where this frame should go to.
     *  Specify UINT16_MAX to broadcast to all streams. */
    uint16_t                idStream;
    /** The frame type. */
    RECORDINGFRAME_TYPE     enmType;
    /** Timestamp (PTS, in ms). */
    uint64_t                msTimestamp;
    /** Encoder parameters.
     *  Only valid if this frame is processed by an encoder. */
    struct
    {
        /** Codec flags of type RECORDINGCODEC_ENC_F_XXX. */
        uint32_t            uFlags;
    } Enc;
    /** Union holding different frame types depending on \a enmType. */
    union
    {
#ifdef VBOX_WITH_AUDIO_RECORDING
        /** Audio data.
         *  Used by RECORDINGFRAME_TYPE_AUDIO. */
        RECORDINGAUDIOFRAME  Audio;
#endif
        /** Video frame.
         *  Used by RECORDINGFRAME_TYPE_VIDEO. */
        RECORDINGVIDEOFRAME  Video;
        /** Recording screen information.
         *  Used by RECORDINGFRAME_TYPE_SCREEN_CHANGE. */
        RECORDINGSURFACEINFO ScreenInfo;
        /** Cursor shape frame.
         *  Used by RECORDINGFRAME_TYPE_CURSOR_SHAPE. */
        RECORDINGVIDEOFRAME  CursorShape;
        /** Cursor information.
         *  Used by RECORDINGFRAME_TYPE_CURSOR_POS. */
        RECORDINGCURSORINFO  Cursor;
    } u;
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t                  abData[RT_FLEXIBLE_ARRAY];
    /** Following there can be allocated data, such as pixel data. */
} RECORDINGFRAME;
/** Pointer to a RECORDINGFRAME. */
typedef RECORDINGFRAME *PRECORDINGFRAME;

PRECORDINGVIDEOFRAME RecordingVideoFrameAlloc(void);
PRECORDINGVIDEOFRAME RecordingVideoFrameAllocEx(const void *pvData, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t uBPP, RECORDINGPIXELFMT enmFmt);
void RecordingVideoFrameFree(PRECORDINGVIDEOFRAME pFrame);
int RecordingVideoFrameInit(PRECORDINGVIDEOFRAME pFrame, uint32_t fFlags, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t uBPP, RECORDINGPIXELFMT enmFmt);
void RecordingVideoFrameDestroy(PRECORDINGVIDEOFRAME pFrame);
uint64_t RecordingFrameRefs(PRECORDINGFRAME pFrame);
uint64_t RecordingFrameAcquire(PRECORDINGFRAME pFrame);
uint64_t RecordingFrameRelease(PRECORDINGFRAME pFrame);
int RecordingVideoFrameCopy(PRECORDINGVIDEOFRAME pDstFrame, PRECORDINGVIDEOFRAME pSrcFrame);
PRECORDINGVIDEOFRAME RecordingVideoFrameDup(PRECORDINGVIDEOFRAME pFrame);
void RecordingVideoFrameClear(PRECORDINGVIDEOFRAME pFrame);

#ifdef VBOX_WITH_AUDIO_RECORDING
void RecordingAudioFrameFree(PRECORDINGAUDIOFRAME pFrame);
#endif

void RecordingFrameDestroy(PRECORDINGFRAME pFrame);
void RecordingFrameFree(PRECORDINGFRAME pFrame);

int RecordingCodecCreate(PRECORDINGCODEC pCodec, PRECORDINGCODECCREATEPARMS pParms);
int RecordingCodecInit(const PRECORDINGCODEC pCodec, const PRECORDINGCODECCALLBACKS pCallbacks, const ComPtr<IRecordingScreenSettings> &ScreenSettings);
int RecordingCodecDestroy(PRECORDINGCODEC pCodec);
PCRECORDINGCODECCFG RecordingCodecGetConfig(const PRECORDINGCODEC pCodec);
int RecordingCodecEncode(PRECORDINGCODEC pCodec, const PRECORDINGFRAME pFrame, uint64_t msTimestamp, void *pvUser);
int RecordingCodecScreenChange(PRECORDINGCODEC pCodec, PRECORDINGSURFACEINFO pInfo);
int RecordingCodecFinalize(PRECORDINGCODEC pCodec);
bool RecordingCodecIsInitialized(const PRECORDINGCODEC pCodec);
uint32_t RecordingCodecGetWritable(const PRECORDINGCODEC pCodec, uint64_t msTimestamp);
RTMSINTERVAL RecordingCodecGetDeadlineMs(const PRECORDINGCODEC pCodec, uint64_t msTimestamp);
#endif /* !MAIN_INCLUDED_RecordingInternals_h */
