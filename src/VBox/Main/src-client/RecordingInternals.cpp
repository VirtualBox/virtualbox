/* $Id: RecordingInternals.cpp 169029 2025-05-28 08:48:14Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording internals code.
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

#include "RecordingInternals.h"
#include "RecordingUtils.h"

#include <iprt/assert.h>
#include <iprt/mem.h>

#ifdef DEBUG
# include <math.h>
# include <iprt/file.h>
# include <iprt/formats/bmp.h>
#endif


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
DECLINLINE(int) recordingVideoFrameInit(PRECORDINGVIDEOFRAME pFrame, uint32_t fFlags, uint32_t uWidth, uint32_t uHeight, uint32_t uPosX, uint32_t uPosY,
                                        uint8_t uBPP, RECORDINGPIXELFMT enmFmt);


/**
 * Allocates an empty video frame, inline version.
 *
 * @returns Allocated video frame on success, or NULL on failure.
 */
DECLINLINE(PRECORDINGVIDEOFRAME) recordingVideoFrameAlloc(void)
{
    return (PRECORDINGVIDEOFRAME)RTMemAlloc(sizeof(RECORDINGVIDEOFRAME));
}

/**
 * Allocates an empty video frame.
 *
 * @returns Allocated video frame on success, or NULL on failure.
 */
PRECORDINGVIDEOFRAME RecordingVideoFrameAlloc(void)
{
    PRECORDINGVIDEOFRAME pFrame = recordingVideoFrameAlloc();
    AssertPtrReturn(pFrame, NULL);
    RT_BZERO(pFrame, sizeof(RECORDINGVIDEOFRAME));
    return pFrame;
}

/**
 * Returns an allocated video frame from given image data.
 *
 * @returns Allocated video frame on success, or NULL on failure.
 * @param   pvData              Pointer to image data to use.
 * @param   x                   X location hint (in pixel) to use for allocated frame.
 *                              This is *not* the offset within \a pvData!
 * @param   y                   X location hint (in pixel) to use for allocated frame.
 *                              This is *not* the offset within \a pvData!
 * @param   w                   Width (in pixel) of \a pvData image data.
 * @param   h                   Height (in pixel) of \a pvData image data.
 * @param   uBPP                Bits per pixel) of \a pvData image data.
 * @param   enmFmt              Pixel format of \a pvData image data.
 */
PRECORDINGVIDEOFRAME RecordingVideoFrameAllocEx(const void *pvData, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                                uint8_t uBPP, RECORDINGPIXELFMT enmFmt)
{
    PRECORDINGVIDEOFRAME pFrame = recordingVideoFrameAlloc();
    AssertPtrReturn(pFrame, NULL);
    int vrc = recordingVideoFrameInit(pFrame, RECORDINGVIDEOFRAME_F_VISIBLE, w, h, x, y, uBPP, enmFmt);
    AssertRCReturn(vrc, NULL);
    memcpy(pFrame->pau8Buf, pvData, pFrame->cbBuf);

    return VINF_SUCCESS;
}

/**
 * Frees a recording video frame.
 *
 * @param   pFrame              Pointer to video frame to free. The pointer will be invalid after return.
 */
void RecordingVideoFrameFree(PRECORDINGVIDEOFRAME pFrame)
{
    if (!pFrame)
        return;

    RecordingVideoFrameDestroy(pFrame);

    RTMemFree(pFrame);
}

/**
 * Initializes a recording frame, inline version.
 *
 * @returns VBox status code.
 * @param   pFrame              Pointer to video frame to initialize.
 * @param   fFlags              Flags of type RECORDINGVIDEOFRAME_F_XXX.
 * @param   uWidth              Width (in pixel) of video frame.
 * @param   uHeight             Height (in pixel) of video frame.
 * @param   uPosX               X positioning hint.
 * @param   uPosY               Y positioning hint.
 * @param   uBPP                Bits per pixel (BPP).
 * @param   enmFmt              Pixel format to use.
 */
DECLINLINE(int) recordingVideoFrameInit(PRECORDINGVIDEOFRAME pFrame, uint32_t fFlags, uint32_t uWidth, uint32_t uHeight,
                                        uint32_t uPosX, uint32_t uPosY, uint8_t uBPP, RECORDINGPIXELFMT enmFmt)
{
    AssertPtrReturn(pFrame, VERR_INVALID_POINTER);
    AssertReturn(uWidth, VERR_INVALID_PARAMETER);
    AssertReturn(uHeight, VERR_INVALID_PARAMETER);
    AssertReturn(uBPP && uBPP % 8 == 0, VERR_INVALID_PARAMETER);

    /* Calculate bytes per pixel and set pixel format. */
    const unsigned uBytesPerPixel = uBPP / 8;

    /* Calculate bytes per pixel and set pixel format. */
    const size_t cbRGBBuf = uWidth * uHeight * uBytesPerPixel;
    AssertReturn(cbRGBBuf, VERR_INVALID_PARAMETER);

    pFrame->pau8Buf = (uint8_t *)RTMemAlloc(cbRGBBuf);
    AssertPtrReturn(pFrame->pau8Buf, VERR_NO_MEMORY);
    pFrame->cbBuf  = cbRGBBuf;

    pFrame->fFlags             = fFlags;
    pFrame->Info.uWidth        = uWidth;
    pFrame->Info.uHeight       = uHeight;
    pFrame->Info.uBPP          = uBPP;
    pFrame->Info.enmPixelFmt   = enmFmt;
    pFrame->Info.uBytesPerLine = uWidth * uBytesPerPixel;
    pFrame->Pos.x              = uPosX;
    pFrame->Pos.y              = uPosY;

    return VINF_SUCCESS;
}

/**
 * Initializes a recording frame.
 *
 * @param   pFrame              Pointer to video frame to initialize.
 * @param   fFlags              Flags of type RECORDINGVIDEOFRAME_F_XXX.
 * @param   uWidth              Width (in pixel) of video frame.
 * @param   uHeight             Height (in pixel) of video frame.
 * @param   uPosX               X positioning hint.
 * @param   uPosY               Y positioning hint.
 * @param   uBPP                Bits per pixel (BPP).
 * @param   enmFmt              Pixel format to use.
 */
int RecordingVideoFrameInit(PRECORDINGVIDEOFRAME pFrame, uint32_t fFlags, uint32_t uWidth, uint32_t uHeight, uint32_t uPosX, uint32_t uPosY,
                            uint8_t uBPP, RECORDINGPIXELFMT enmFmt)
{
    return recordingVideoFrameInit(pFrame, fFlags, uWidth, uHeight, uPosX, uPosY, uBPP, enmFmt);
}

/**
 * Destroys a recording video frame.
 *
 * @param   pFrame              Pointer to video frame to destroy.
 */
void RecordingVideoFrameDestroy(PRECORDINGVIDEOFRAME pFrame)
{
    if (!pFrame)
        return;

    if (pFrame->pau8Buf)
    {
        Assert(pFrame->cbBuf);
        RTMemFree(pFrame->pau8Buf);
        pFrame->pau8Buf = NULL;
        pFrame->cbBuf  = 0;
    }
}

/**
 * Duplicates a video frame.
 *
 * @returns Pointer to duplicated frame on success, or NULL on failure.
 * @param   pFrame              Video frame to duplicate.
 */
PRECORDINGVIDEOFRAME RecordingVideoFrameDup(PRECORDINGVIDEOFRAME pFrame)
{
    PRECORDINGVIDEOFRAME pFrameDup = (PRECORDINGVIDEOFRAME)RTMemDup(pFrame, sizeof(RECORDINGVIDEOFRAME));
    AssertPtrReturn(pFrameDup, NULL);
    pFrameDup->pau8Buf = (uint8_t *)RTMemDup(pFrame->pau8Buf, pFrame->cbBuf);
    AssertPtrReturnStmt(pFrameDup, RTMemFree(pFrameDup), NULL);

    return pFrameDup;
}

/**
 * Clears the content of a video recording frame, inlined version.
 *
 * @param   pFrame              Video recording frame to clear content for.
 */
DECLINLINE(void) recordingVideoFrameClear(PRECORDINGVIDEOFRAME pFrame)
{
    RT_BZERO(pFrame->pau8Buf, pFrame->cbBuf);
}

/**
 * Clears the content of a video recording frame.
 *
 * @param   pFrame              Video recording frame to clear content for.
 */
void RecordingVideoFrameClear(PRECORDINGVIDEOFRAME pFrame)
{
    recordingVideoFrameClear(pFrame);
}

/**
 * Simple blitting function for raw image data, inlined version.
 *
 * @returns VBox status code.
 * @param   pu8Dst              Destination buffer.
 * @param   cbDst               Size (in bytes) of \a pu8Dst.
 * @param   uDstX               X destination (in pixel) within destination frame.
 * @param   uDstY               Y destination (in pixel) within destination frame.
 * @param   uDstBytesPerLine    Bytes per line in destination buffer.
 * @param   uDstBPP             BPP of destination buffer.
 * @param   enmDstFmt           Pixel format of source data. Must match \a pFrame.
 * @param   pu8Src              Source data to blit. Must be in the same pixel format as \a pFrame.
 * @param   cbSrc               Size (in bytes) of \a pu8Src.
 * @param   uSrcX               X start (in pixel) within source data.
 * @param   uSrcY               Y start (in pixel) within source data.
 * @param   uSrcWidth           Width (in pixel) to blit from source data.
 * @param   uSrcHeight          Height (in pixel) to blit from data.
 * @param   uSrcBytesPerLine    Bytes per line in source data.
 * @param   uSrcBPP             BPP of source data. Must match \a pFrame.
 * @param   enmSrcFmt           Pixel format of source data. Must match \a pFrame.
 */
DECLINLINE(int) recordingVideoBlitRaw(uint8_t *pu8Dst, size_t cbDst, uint32_t uDstX, uint32_t uDstY,
                                      uint32_t uDstBytesPerLine, uint8_t uDstBPP, RECORDINGPIXELFMT enmDstFmt,
                                      const uint8_t *pu8Src, size_t cbSrc, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight,
                                      uint32_t uSrcBytesPerLine, uint8_t uSrcBPP, RECORDINGPIXELFMT enmSrcFmt)
{
    RT_NOREF(enmDstFmt, enmSrcFmt);

    uint8_t const uDstBytesPerPixel = uDstBPP / 8;
    uint8_t const uSrcBytesPerPixel = uSrcBPP / 8;

    size_t offSrc = RT_MIN(uSrcY * uSrcBytesPerLine + uSrcX * uSrcBytesPerPixel, cbSrc);
    size_t offDst = RT_MIN(uDstY * uDstBytesPerLine + uDstX * uDstBytesPerPixel, cbDst);

    for (uint32_t y = 0; y < uSrcHeight; y++)
    {
        size_t const cbToCopy = RT_MIN(cbDst - offDst,
                                       RT_MIN(uSrcWidth * uSrcBytesPerPixel, cbSrc - offSrc));
        if (!cbToCopy)
            break;
        memcpy(pu8Dst + offDst, (const uint8_t *)pu8Src + offSrc, cbToCopy);
        offDst = RT_MIN(offDst + uDstBytesPerLine, cbDst);
        Assert(offDst <= cbDst);
        offSrc = RT_MIN(offSrc + uSrcBytesPerLine, cbSrc);
        Assert(offSrc <= cbSrc);
    }

    return VINF_SUCCESS;
}

/**
 * Simple blitting function for raw image data.
 *
 * @returns VBox status code.
 * @param   pu8Dst              Destination buffer.
 * @param   cbDst               Size (in bytes) of \a pu8Dst.
 * @param   uDstX               X destination (in pixel) within destination frame.
 * @param   uDstY               Y destination (in pixel) within destination frame.
 * @param   uDstBytesPerLine    Bytes per line in destination buffer.
 * @param   uDstBPP             BPP of destination buffer.
 * @param   enmDstFmt           Pixel format of source data. Must match \a pFrame.
 * @param   pu8Src              Source data to blit. Must be in the same pixel format as \a pFrame.
 * @param   cbSrc               Size (in bytes) of \a pu8Src.
 * @param   uSrcX               X start (in pixel) within source data.
 * @param   uSrcY               Y start (in pixel) within source data.
 * @param   uSrcWidth           Width (in pixel) to blit from source data.
 * @param   uSrcHeight          Height (in pixel) to blit from data.
 * @param   uSrcBytesPerLine    Bytes per line in source data.
 * @param   uSrcBPP             BPP of source data. Must match \a pFrame.
 * @param   enmSrcFmt           Pixel format of source data. Must match \a pFrame.
 */
int RecordingVideoBlitRaw(uint8_t *pu8Dst, size_t cbDst, uint32_t uDstX, uint32_t uDstY,
                          uint32_t uDstBytesPerLine, uint8_t uDstBPP, RECORDINGPIXELFMT enmDstFmt,
                          const uint8_t *pu8Src, size_t cbSrc, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight,
                          uint32_t uSrcBytesPerLine, uint8_t uSrcBPP, RECORDINGPIXELFMT enmSrcFmt)
{
    return recordingVideoBlitRaw(pu8Dst, cbDst, uDstX, uDstY, uDstBytesPerLine,uDstBPP, enmDstFmt,
                                 pu8Src, cbSrc, uSrcX, uSrcY, uSrcWidth, uSrcHeight,uSrcBytesPerLine, uSrcBPP, enmSrcFmt);
}

/**
 * Simple blitting function for raw image data with alpha channel, inlined version.
 *
 * @returns VBox status code.
 * @param   pFrame              Destination frame.
 * @param   uDstX               X destination (in pixel) within destination frame.
 * @param   uDstY               Y destination (in pixel) within destination frame.
 * @param   pu8Src              Source data to blit. Must be in the same pixel format as \a pFrame.
 * @param   cbSrc               Size (in bytes) of \a pu8Src.
 * @param   uSrcX               X start (in pixel) within source data.
 * @param   uSrcY               Y start (in pixel) within source data.
 * @param   uSrcWidth           Width (in pixel) to blit from source data.
 * @param   uSrcHeight          Height (in pixel) to blit from data.
 * @param   uSrcBytesPerLine    Bytes per line in source data.
 * @param   uSrcBPP             BPP of source data. Must match \a pFrame.
 * @param   enmFmt              Pixel format of source data. Must match \a pFrame.
 */
DECLINLINE(int) recordingVideoFrameBlitRawAlpha(PRECORDINGVIDEOFRAME pFrame, uint32_t uDstX, uint32_t uDstY,
                                                const uint8_t *pu8Src, size_t cbSrc, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight,
                                                uint32_t uSrcBytesPerLine, uint8_t uSrcBPP, RECORDINGPIXELFMT enmFmt)
{
    AssertReturn(pFrame->Info.enmPixelFmt == enmFmt, VERR_NOT_SUPPORTED);
    AssertReturn(pFrame->Info.uBPP == uSrcBPP, VERR_NOT_SUPPORTED);

    RT_NOREF(uDstX, uDstY, cbSrc, uSrcX, uSrcY, uSrcBytesPerLine);
    uint8_t const uDstBytesPerPixel = pFrame->Info.uBPP / 8;
    uint8_t const uSrcBytesPerPixel = uSrcBPP / 8;

    for (uint32_t y = 0; y < uSrcHeight; y++)
    {
        size_t offSrc = RT_MIN((uSrcY + y) * uSrcBytesPerLine + uSrcX * uSrcBytesPerPixel, cbSrc);
        size_t offDst = RT_MIN((uDstY + y) * pFrame->Info.uBytesPerLine + uDstX * uDstBytesPerPixel, pFrame->cbBuf);

        for (uint32_t x = 0; x < uSrcWidth; x++)
        {
            /* BGRA */
            int const idx_b = 0;
            int const idx_g = 1;
            int const idx_r = 2;
            int const idx_a = 3;

            unsigned int const alpha = pu8Src[offSrc + idx_a] + 1;
            unsigned int const inv_alpha = 256 - pu8Src[offSrc + idx_a];
            if (pu8Src[offSrc + idx_a])
            {
                pFrame->pau8Buf[offDst + idx_r] = (unsigned char)((alpha * pu8Src[offSrc + idx_r] + inv_alpha * pFrame->pau8Buf[offDst + idx_r]) >> 8);
                pFrame->pau8Buf[offDst + idx_g] = (unsigned char)((alpha * pu8Src[offSrc + idx_g] + inv_alpha * pFrame->pau8Buf[offDst + idx_g]) >> 8);
                pFrame->pau8Buf[offDst + idx_b] = (unsigned char)((alpha * pu8Src[offSrc + idx_b] + inv_alpha * pFrame->pau8Buf[offDst + idx_b]) >> 8);
                pFrame->pau8Buf[offDst + idx_a] = 0xff;
            }

            offSrc = RT_MIN(offSrc + uSrcBytesPerPixel, cbSrc);
            if (offSrc >= cbSrc)
                break;
            offDst = RT_MIN(offDst + uDstBytesPerPixel, pFrame->cbBuf);
            if (offDst >= pFrame->cbBuf)
                break;
        }
    }

#if 0
    RecordingUtilsDbgDumpImageData(pu8Src, cbSrc, "/tmp", "cursor-src", uSrcWidth, uSrcHeight, uSrcBytesPerLine, 32);
    RecordingUtilsDbgDumpVideoFrameEx(pFrame, "/tmp", "cursor-dst");
#endif

    return VINF_SUCCESS;
}

/**
 * Simple blitting function for raw image data.
 *
 * @returns VBox status code.
 * @param   pDstFrame           Destination frame.
 * @param   uDstX               X destination (in pixel) within destination frame.
 * @param   uDstY               Y destination (in pixel) within destination frame.
 * @param   pu8Src              Source data to blit. Must be in the same pixel format as \a pFrame.
 * @param   cbSrc               Size (in bytes) of \a pu8Src.
 * @param   uSrcX               X start (in pixel) within source data.
 * @param   uSrcY               Y start (in pixel) within source data.
 * @param   uSrcWidth           Width (in pixel) to blit from source data.
 * @param   uSrcHeight          Height (in pixel) to blit from data.
 * @param   uSrcBytesPerLine    Bytes per line in source data.
 * @param   uSrcBPP             BPP of source data. Must match \a pFrame.
 * @param   enmFmt              Pixel format of source data. Must match \a pFrame.
 */
int RecordingVideoFrameBlitRaw(PRECORDINGVIDEOFRAME pDstFrame, uint32_t uDstX, uint32_t uDstY,
                               const uint8_t *pu8Src, size_t cbSrc, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight,
                               uint32_t uSrcBytesPerLine, uint8_t uSrcBPP, RECORDINGPIXELFMT enmFmt)
{
    return recordingVideoBlitRaw(/* Destination */
                                pDstFrame->pau8Buf, pDstFrame->cbBuf, uDstX, uDstY,
                                pDstFrame->Info.uBytesPerLine, pDstFrame->Info.uBPP, pDstFrame->Info.enmPixelFmt,
                                /* Source */
                                pu8Src, cbSrc, uSrcX, uSrcY, uSrcWidth, uSrcHeight, uSrcBytesPerLine, uSrcBPP, enmFmt);
}

/**
 * Simple blitting function for raw image data with alpha channel.
 *
 * @returns VBox status code.
 * @param   pDstFrame           Destination frame.
 * @param   uDstX               X destination (in pixel) within destination frame.
 * @param   uDstY               Y destination (in pixel) within destination frame.
 * @param   pu8Src              Source data to blit. Must be in the same pixel format as \a pFrame.
 * @param   cbSrc               Size (in bytes) of \a pu8Src.
 * @param   uSrcX               X start (in pixel) within source data.
 * @param   uSrcY               Y start (in pixel) within source data.
 * @param   uSrcWidth           Width (in pixel) to blit from source data.
 * @param   uSrcHeight          Height (in pixel) to blit from data.
 * @param   uSrcBytesPerLine    Bytes per line in source data.
 * @param   uSrcBPP             BPP of source data. Must match \a pFrame.
 * @param   enmFmt              Pixel format of source data. Must match \a pFrame.
 */
int RecordingVideoFrameBlitRawAlpha(PRECORDINGVIDEOFRAME pDstFrame, uint32_t uDstX, uint32_t uDstY,
                                    const uint8_t *pu8Src, size_t cbSrc, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight,
                                    uint32_t uSrcBytesPerLine, uint8_t uSrcBPP, RECORDINGPIXELFMT enmFmt)
{
    return recordingVideoFrameBlitRawAlpha(pDstFrame, uDstX, uDstY,
                                           pu8Src, cbSrc, uSrcX, uSrcY, uSrcWidth, uSrcHeight, uSrcBytesPerLine, uSrcBPP, enmFmt);
}

/**
 * Simple blitting function for video frames.
 *
 * @returns VBox status code.
 * @param   pDstFrame           Destination frame.
 * @param   uDstX               X destination (in pixel) within destination frame.
 * @param   uDstY               Y destination (in pixel) within destination frame.
 * @param   pSrcFrame           Source frame.
 * @param   uSrcX               X start (in pixel) within source frame.
 * @param   uSrcY               Y start (in pixel) within source frame.
 * @param   uSrcWidth           Width (in pixel) to blit from source frame.
 * @param   uSrcHeight          Height (in pixel) to blit from frame.
 *
 * @note    Does NOT check for limits, so use with care!
 */
int RecordingVideoFrameBlitFrame(PRECORDINGVIDEOFRAME pDstFrame, uint32_t uDstX, uint32_t uDstY,
                                 PRECORDINGVIDEOFRAME pSrcFrame, uint32_t uSrcX, uint32_t uSrcY, uint32_t uSrcWidth, uint32_t uSrcHeight)
{
    return recordingVideoBlitRaw(/* Dest */
                                 pDstFrame->pau8Buf, pDstFrame->cbBuf, uDstX, uDstY,
                                 pDstFrame->Info.uBytesPerLine, pDstFrame->Info.uBPP, pDstFrame->Info.enmPixelFmt,
                                 /* Source */
                                 pSrcFrame->pau8Buf, pSrcFrame->cbBuf, uSrcX, uSrcY, uSrcWidth, uSrcHeight,
                                 pSrcFrame->Info.uBytesPerLine, pSrcFrame->Info.uBPP, pSrcFrame->Info.enmPixelFmt);
}

#ifdef VBOX_WITH_AUDIO_RECORDING
/**
 * Destroys a recording audio frame.
 *
 * @param   pFrame              Pointer to audio frame to destroy.
 */
DECLINLINE(void) recordingAudioFrameDestroy(PRECORDINGAUDIOFRAME pFrame)
{
    if (!pFrame)
        return;

    if (pFrame->pvBuf)
    {
        Assert(pFrame->cbBuf);
        RTMemFree(pFrame->pvBuf);
        pFrame->pvBuf = NULL;
        pFrame->cbBuf = 0;
    }
}

/**
 * Frees a previously allocated recording audio frame.
 *
 * @param   pFrame              Audio frame to free. The pointer will be invalid after return.
 */
void RecordingAudioFrameFree(PRECORDINGAUDIOFRAME pFrame)
{
    if (!pFrame)
        return;

    recordingAudioFrameDestroy(pFrame);

    RTMemFree(pFrame);
    pFrame = NULL;
}
#endif /* VBOX_WITH_AUDIO_RECORDING */

/**
 * Frees a recording frame.
 *
 * @param   pFrame              Pointer to recording frame to free.
 *                              The pointer will be invalid after return.
 */
void RecordingFrameFree(PRECORDINGFRAME pFrame)
{
    if (!pFrame)
        return;

    switch (pFrame->enmType)
    {
#ifdef VBOX_WITH_AUDIO_RECORDING
        case RECORDINGFRAME_TYPE_AUDIO:
            recordingAudioFrameDestroy(&pFrame->u.Audio);
            break;
#endif
        case RECORDINGFRAME_TYPE_VIDEO:
            RecordingVideoFrameDestroy(&pFrame->u.Video);
            break;

        case RECORDINGFRAME_TYPE_CURSOR_SHAPE:
            RecordingVideoFrameDestroy(&pFrame->u.CursorShape);
            break;

        default:
            /* Nothing to do here. */
            break;
    }

    RTMemFree(pFrame);
    pFrame = NULL;
}

