/* $Id: AudioMixBuffer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Audio Mixing bufer convert audio samples to/from different rates / formats.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Audio_AudioMixBuffer_h
#define VBOX_INCLUDED_SRC_Audio_AudioMixBuffer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <VBox/vmm/pdmaudioifs.h>

/** @defgroup grp_pdm_ifs_audio_mixing_buffers   Audio Mixing Buffers
 * @ingroup grp_pdm_ifs_audio_mixing
 *
 * @note This is currently placed under PDM Audio Interface as that seemed like
 *       the best place for it.
 *
 * @{
 */


/**
 * Rate processing information of a source & destination audio stream.
 *
 * This is needed because both streams can differ regarding their rates and
 * therefore need to be treated accordingly.
 */
typedef struct AUDIOSTREAMRATE
{
    /** Current (absolute) offset in the output (destination) stream.
     * @todo r=bird: Please reveal which unit these members are given in. */
    uint64_t        offDst;
    /** Increment for moving offDst for the destination stream.
     * This is needed because the source <-> destination rate might be different. */
    uint64_t        uDstInc;
    /** Current (absolute) offset in the input stream. */
    uint32_t        offSrc;
    /** Set if no conversion is necessary. */
    bool            fNoConversionNeeded;
    bool            afPadding[3];

    /** Last processed frame of the input stream.
     *  Needed for interpolation. */
    union
    {
        int32_t     ai32Samples[PDMAUDIO_MAX_CHANNELS];
    } SrcLast;

    /**
     * Resampling function.
     * @returns Number of destination frames written.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnResample, (int32_t *pi32Dst, uint32_t cDstFrames,
                                                 int32_t const *pi32Src, uint32_t cSrcFrames, uint32_t *pcSrcFramesRead,
                                                 struct AUDIOSTREAMRATE *pRate));

} AUDIOSTREAMRATE;
/** Pointer to rate processing information of a stream. */
typedef AUDIOSTREAMRATE *PAUDIOSTREAMRATE;

/**
 * Mixing buffer volume parameters.
 *
 * The volume values are in fixed point style and must be converted to/from
 * before using with e.g. PDMAUDIOVOLUME.
 */
typedef struct AUDMIXBUFVOL
{
    /** Set to @c true if this stream is muted, @c false if not. */
    bool            fMuted;
    /** Set if all (relevant) channels are at max. */
    bool            fAllMax;
    /** The per-channels values. */
    uint32_t        auChannels[PDMAUDIO_MAX_CHANNELS];
} AUDMIXBUFVOL;
/** Pointer to mixing buffer volument parameters. */
typedef AUDMIXBUFVOL *PAUDMIXBUFVOL;


/** Pointer to audio mixing buffer.  */
typedef struct AUDIOMIXBUF *PAUDIOMIXBUF;
/** Pointer to a const audio mixing buffer.  */
typedef struct AUDIOMIXBUF const *PCAUDIOMIXBUF;


/**
 * State & config for AudioMixBufPeek created by AudioMixBufInitPeekState.
 */
typedef struct AUDIOMIXBUFPEEKSTATE
{
    /** Encodes @a cFrames from @a paSrc to @a pvDst. */
    DECLR3CALLBACKMEMBER(void,  pfnEncode,(void *pvDst, int32_t const *paSrc, uint32_t cFrames, struct AUDIOMIXBUFPEEKSTATE *pState));
    /** Sample rate conversion state (only used when needed). */
    AUDIOSTREAMRATE             Rate;
    /** Source (mixer) channels. */
    uint8_t                     cSrcChannels;
    /** Destination channels. */
    uint8_t                     cDstChannels;
    /** Destination frame size. */
    uint8_t                     cbDstFrame;
    /** The destination frame layout described as indexes into the source frame.
     * This ASSUMES that all channels uses the same sample size, so one sample per
     * channel if you like.
     * Negative values are special: -1 for zero, -2 for silence.
     * @note Blending stereo into mono is not really expressible here. */
    int8_t                      aidxChannelMap[PDMAUDIO_MAX_CHANNELS];
} AUDIOMIXBUFPEEKSTATE;
/** Pointer to peek state & config. */
typedef AUDIOMIXBUFPEEKSTATE *PAUDIOMIXBUFPEEKSTATE;


/**
 * State & config for AudioMixBufWrite, AudioMixBufSilence, AudioMixBufBlend and
 * AudioMixBufBlendGap, created by AudioMixBufInitWriteState.
 */
typedef struct AUDIOMIXBUFWRITESTATE
{
    /** Encodes @a cFrames from @a pvSrc to @a paDst. */
    DECLR3CALLBACKMEMBER(void,  pfnDecode,(int32_t *paDst, const void *pvSrc, uint32_t cFrames, struct AUDIOMIXBUFWRITESTATE *pState));
    /** Encodes @a cFrames from @a pvSrc blending into @a paDst. */
    DECLR3CALLBACKMEMBER(void,  pfnDecodeBlend,(int32_t *paDst, const void *pvSrc, uint32_t cFrames, struct AUDIOMIXBUFWRITESTATE *pState));
    /** Sample rate conversion state (only used when needed). */
    AUDIOSTREAMRATE             Rate;
    /** Destination (mixer) channels. */
    uint8_t                     cDstChannels;
    /** Source hannels. */
    uint8_t                     cSrcChannels;
    /** Source frame size. */
    uint8_t                     cbSrcFrame;
    /** The destination frame layout described as indexes into the source frame.
     * This ASSUMES that all channels uses the same sample size, so one sample per
     * channel if you like.
     * Negative values are special: -1 for zero, -2 for silence.
     * @note Blending stereo into mono is not really expressible here. */
    int8_t                      aidxChannelMap[PDMAUDIO_MAX_CHANNELS];
} AUDIOMIXBUFWRITESTATE;
/** Pointer to write state & config. */
typedef AUDIOMIXBUFWRITESTATE *PAUDIOMIXBUFWRITESTATE;


/**
 * Audio mixing buffer.
 */
typedef struct AUDIOMIXBUF
{
    /** Magic value (AUDIOMIXBUF_MAGIC). */
    uint32_t                    uMagic;
    /** Size of the frame buffer (in audio frames). */
    uint32_t                    cFrames;
    /** The frame buffer.
     * This is a two dimensional array consisting of cFrames rows and
     * cChannels columns. */
    int32_t                    *pi32Samples;
    /** The number of channels. */
    uint8_t                     cChannels;
    /** The frame size (row size if you like). */
    uint8_t                     cbFrame;
    uint8_t                     abPadding[2];
    /** The current read position (in frames). */
    uint32_t                    offRead;
    /** The current write position (in frames). */
    uint32_t                    offWrite;
    /** How much audio frames are currently being used in this buffer.
     * @note This also is known as the distance in ring buffer terms. */
    uint32_t                    cUsed;
    /** Audio properties for the buffer content - for frequency and channel count.
     * (This is the guest side PCM properties.) */
    PDMAUDIOPCMPROPS            Props;
    /** Internal representation of current volume used for mixing. */
    AUDMIXBUFVOL                Volume;
    /** Name of the buffer. */
    char                       *pszName;
} AUDIOMIXBUF;

/** Magic value for AUDIOMIXBUF (Antonio Lucio Vivaldi). */
#define AUDIOMIXBUF_MAGIC           UINT32_C(0x16780304)
/** Dead mixer buffer magic. */
#define AUDIOMIXBUF_MAGIC_DEAD      UINT32_C(0x17410728)

/** Converts (audio) frames to bytes. */
#define AUDIOMIXBUF_F2B(a_pMixBuf, a_cFrames)   PDMAUDIOPCMPROPS_F2B(&(a_pMixBuf)->Props, a_cFrames)
/** Converts bytes to (audio) frames.
 * @note Does *not* take the conversion ratio into account. */
#define AUDIOMIXBUF_B2F(a_pMixBuf, a_cb)        PDMAUDIOPCMPROPS_B2F(&(a_pMixBuf)->Props, a_cb)


int         AudioMixBufInit(PAUDIOMIXBUF pMixBuf, const char *pszName, PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames);
void        AudioMixBufTerm(PAUDIOMIXBUF pMixBuf);
void        AudioMixBufDrop(PAUDIOMIXBUF pMixBuf);
void        AudioMixBufSetVolume(PAUDIOMIXBUF pMixBuf, PCPDMAUDIOVOLUME pVol);

/** @name Mixer buffer getters
 * @{ */
uint32_t    AudioMixBufSize(PCAUDIOMIXBUF pMixBuf);
uint32_t    AudioMixBufSizeBytes(PCAUDIOMIXBUF pMixBuf);
uint32_t    AudioMixBufUsed(PCAUDIOMIXBUF pMixBuf);
uint32_t    AudioMixBufUsedBytes(PCAUDIOMIXBUF pMixBuf);
uint32_t    AudioMixBufFree(PCAUDIOMIXBUF pMixBuf);
uint32_t    AudioMixBufFreeBytes(PCAUDIOMIXBUF pMixBuf);
bool        AudioMixBufIsEmpty(PCAUDIOMIXBUF pMixBuf);
uint32_t    AudioMixBufReadPos(PCAUDIOMIXBUF pMixBuf);
uint32_t    AudioMixBufWritePos(PCAUDIOMIXBUF pMixBuf);
/** @} */

/** @name Mixer buffer reading
 *  @{ */
int         AudioMixBufInitPeekState(PCAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFPEEKSTATE pState, PCPDMAUDIOPCMPROPS pDstProps);
void        AudioMixBufPeek(PCAUDIOMIXBUF pMixBuf, uint32_t offSrcFrame, uint32_t cMaxSrcFrames, uint32_t *pcSrcFramesPeeked,
                            PAUDIOMIXBUFPEEKSTATE pState, void *pvDst, uint32_t cbDst, uint32_t *pcbDstPeeked);
void        AudioMixBufAdvance(PAUDIOMIXBUF pMixBuf, uint32_t cFrames);
/** @} */

/** @name Mixer buffer writing
 * @{ */
int         AudioMixBufInitWriteState(PCAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, PCPDMAUDIOPCMPROPS pSrcProps);
void        AudioMixBufWrite(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, const void *pvSrcBuf, uint32_t cbSrcBuf,
                             uint32_t offDstFrame, uint32_t cMaxDstFrames, uint32_t *pcDstFramesWritten);
void        AudioMixBufSilence(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, uint32_t offFrame, uint32_t cFrames);
void        AudioMixBufBlend(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, const void *pvSrcBuf, uint32_t cbSrcBuf,
                             uint32_t offDstFrame, uint32_t cMaxDstFrames, uint32_t *pcDstFramesBlended);
void        AudioMixBufBlendGap(PAUDIOMIXBUF pMixBuf, PAUDIOMIXBUFWRITESTATE pState, uint32_t cFrames);
void        AudioMixBufCommit(PAUDIOMIXBUF pMixBuf, uint32_t cFrames);
/** @} */

/** @} */
#endif /* !VBOX_INCLUDED_SRC_Audio_AudioMixBuffer_h */

