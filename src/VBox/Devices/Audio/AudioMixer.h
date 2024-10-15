/* $Id: AudioMixer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox audio - Mixing routines.
 *
 * The mixing routines are mainly used by the various audio device emulations
 * to achieve proper multiplexing from/to attached devices LUNs.
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

#ifndef VBOX_INCLUDED_SRC_Audio_AudioMixer_h
#define VBOX_INCLUDED_SRC_Audio_AudioMixer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/critsect.h>

#include <VBox/vmm/pdmaudioifs.h>
#include "AudioMixBuffer.h"
#include "AudioHlp.h"


/** @defgroup grp_pdm_ifs_audio_mixing  Audio Mixing
 * @ingroup grp_pdm_ifs_audio
 *
 * @note This is currently placed under PDM Audio Interface as that seemed like
 *       the best place for it.
 *
 * @{
 */

/** Pointer to an audio mixer sink. */
typedef struct AUDMIXSINK *PAUDMIXSINK;
/** Pointer to a const audio mixer sink. */
typedef struct AUDMIXSINK const *PCAUDMIXSINK;


/**
 * Audio mixer instance.
 */
typedef struct AUDIOMIXER
{
    /** Magic value (AUDIOMIXER_MAGIC). */
    uintptr_t               uMagic;
    /** The mixer's name (allocated after this structure). */
    char const             *pszName;
    /** The master volume of this mixer. */
    PDMAUDIOVOLUME          VolMaster;
    /** List of audio mixer sinks (AUDMIXSINK). */
    RTLISTANCHOR            lstSinks;
    /** Number of used audio sinks. */
    uint8_t                 cSinks;
    /** Mixer flags. See AUDMIXER_FLAGS_XXX. */
    uint32_t                fFlags;
    /** The mixer's critical section. */
    RTCRITSECT              CritSect;
} AUDIOMIXER;
/** Pointer to an audio mixer instance. */
typedef AUDIOMIXER *PAUDIOMIXER;

/** Value for AUDIOMIXER::uMagic. (Attilio Joseph "Teo" Macero)  */
#define AUDIOMIXER_MAGIC                UINT32_C(0x19251030)
/** Value for AUDIOMIXER::uMagic after destruction. */
#define AUDIOMIXER_MAGIC_DEAD           UINT32_C(0x20080219)

/** @name AUDMIXER_FLAGS_XXX - For AudioMixerCreate().
 * @{ */
/** No mixer flags specified. */
#define AUDMIXER_FLAGS_NONE             0
/** Debug mode enabled.
 *  This writes .WAV file to the host, usually to the temporary directory. */
#define AUDMIXER_FLAGS_DEBUG            RT_BIT(0)
/** Validation mask. */
#define AUDMIXER_FLAGS_VALID_MASK       UINT32_C(0x00000001)
/** @} */


/**
 * Audio mixer stream.
 */
typedef struct AUDMIXSTREAM
{
    /** List entry on AUDMIXSINK::lstStreams. */
    RTLISTNODE              Node;
    /** Magic value (AUDMIXSTREAM_MAGIC). */
    uint32_t                uMagic;
    /** The backend buffer size in frames (for draining deadline calc). */
    uint32_t                cFramesBackendBuffer;
    /** Stream status of type AUDMIXSTREAM_STATUS_. */
    uint32_t                fStatus;
    /** Number of writable/readable frames the last time we checked. */
    uint32_t                cFramesLastAvail;
    /** Set if the stream has been found unreliable wrt. consuming/producing
     * samples, and that we shouldn't consider it when deciding how much to move
     * from the mixer buffer and to the drivers. */
    bool                    fUnreliable;
    /** Name of this stream. */
    char                   *pszName;
    /** The statistics prefix. */
    char                   *pszStatPrefix;
    /** Sink this stream is attached to. */
    PAUDMIXSINK             pSink;
    /** Pointer to audio connector being used. */
    PPDMIAUDIOCONNECTOR     pConn;
    /** Pointer to PDM audio stream this mixer stream handles. */
    PPDMAUDIOSTREAM         pStream;
    union
    {
        /** Output: Mixing buffer peeking state & config. */
        AUDIOMIXBUFPEEKSTATE    PeekState;
        /** Input:  Mixing buffer writing state & config. */
        AUDIOMIXBUFWRITESTATE   WriteState;
    };
    /** Last read (recording) / written (playback) timestamp (in ns). */
    uint64_t                tsLastReadWrittenNs;
    /** The streams's critical section. */
    RTCRITSECT              CritSect;
} AUDMIXSTREAM;
/** Pointer to an audio mixer stream. */
typedef AUDMIXSTREAM *PAUDMIXSTREAM;

/** Value for AUDMIXSTREAM::uMagic. (Jan Erik Kongshaug)  */
#define AUDMIXSTREAM_MAGIC                UINT32_C(0x19440704)
/** Value for AUDMIXSTREAM::uMagic after destruction. */
#define AUDMIXSTREAM_MAGIC_DEAD           UINT32_C(0x20191105)


/** @name AUDMIXSTREAM_STATUS_XXX - mixer stream status.
 * (This is a destilled version of PDMAUDIOSTREAM_STS_XXX.)
 * @{ */
/** No status set. */
#define AUDMIXSTREAM_STATUS_NONE                UINT32_C(0)
/** The mixing stream is enabled (active). */
#define AUDMIXSTREAM_STATUS_ENABLED             RT_BIT_32(0)
/** The mixing stream can be read from.
 * Always set together with AUDMIXSTREAM_STATUS_ENABLED. */
#define AUDMIXSTREAM_STATUS_CAN_READ            RT_BIT_32(1)
/** The mixing stream can be written to.
 * Always set together with AUDMIXSTREAM_STATUS_ENABLED. */
#define AUDMIXSTREAM_STATUS_CAN_WRITE           RT_BIT_32(2)
/** @} */


/** Callback for an asynchronous I/O update job.  */
typedef DECLCALLBACKTYPE(void, FNAUDMIXSINKUPDATE,(PPDMDEVINS pDevIns, PAUDMIXSINK pSink, void *pvUser));
/** Pointer to a callback for an asynchronous I/O update job.  */
typedef FNAUDMIXSINKUPDATE *PFNAUDMIXSINKUPDATE;

/**
 * Audio mixer sink.
 */
typedef struct AUDMIXSINK
{
    /** List entry on AUDIOMIXER::lstSinks. */
    RTLISTNODE              Node;
    /** Magic value (AUDMIXSINK_MAGIC). */
    uint32_t                uMagic;
    /** The sink direction (either PDMAUDIODIR_IN or PDMAUDIODIR_OUT). */
    PDMAUDIODIR             enmDir;
    /** Pointer to mixer object this sink is bound to. */
    PAUDIOMIXER             pParent;
    /** Name of this sink (allocated after this structure). */
    char const             *pszName;
    /** The sink's PCM format (i.e. the guest device side). */
    PDMAUDIOPCMPROPS        PCMProps;
    /** Sink status bits - AUDMIXSINK_STS_XXX. */
    uint32_t                fStatus;
    /** Number of bytes to be transferred from the device DMA buffer before the
     *  streams will be put into draining mode. */
    uint32_t                cbDmaLeftToDrain;
    /** The deadline for draining if it's pending. */
    uint64_t                nsDrainDeadline;
    /** When the draining startet (for logging). */
    uint64_t                nsDrainStarted;
    /** Number of streams assigned. */
    uint8_t                 cStreams;
    /** List of assigned streams (AUDMIXSTREAM).
     * @note All streams have the same PCM properties, so the mixer does not do
     *       any conversion.  bird: That is *NOT* true any more, the mixer has
     *       encoders/decoder states for each stream (well, input is still a todo).
     *
     * @todo Use something faster -- vector maybe?  bird: It won't be faster.  You
     *       will have a vector of stream pointers (because you cannot have a vector
     *       of full AUDMIXSTREAM structures since they'll move when the vector is
     *       reallocated and we need pointers to them to give out to devices), which
     *       is the same cost as going via Node.pNext/pPrev. */
    RTLISTANCHOR            lstStreams;
    /** The volume of this sink. The volume always will
     *  be combined with the mixer's master volume. */
    PDMAUDIOVOLUME          Volume;
    /** The volume of this sink, combined with the last set  master volume. */
    PDMAUDIOVOLUME          VolumeCombined;
    /** Timestamp since last update (in ms). */
    uint64_t                tsLastUpdatedMs;
    /** Last read (recording) / written (playback) timestamp (in ns). */
    uint64_t                tsLastReadWrittenNs;
    /** Union for input/output specifics. */
    union
    {
        struct
        {
            /** The sink's peek state. */
            AUDIOMIXBUFPEEKSTATE    State;
        } In;
        struct
        {
            /** The sink's write state. */
            AUDIOMIXBUFWRITESTATE   State;
        } Out;
    };
    struct
    {
        PAUDIOHLPFILE       pFile;
    } Dbg;
    /** This sink's mixing buffer. */
    AUDIOMIXBUF             MixBuf;
    /** Asynchronous I/O thread related stuff. */
    struct
    {
        /** The thread handle, NIL_RTTHREAD if not active. */
        RTTHREAD                hThread;
        /** Event for letting the thread know there is some data to process. */
        RTSEMEVENT              hEvent;
        /** The device instance (same for all update jobs). */
        PPDMDEVINS              pDevIns;
        /** Started indicator. */
        volatile bool           fStarted;
        /** Shutdown indicator. */
        volatile bool           fShutdown;
        /** Number of update jobs this sink has (usually zero or one). */
        uint8_t                 cUpdateJobs;
        /** The minimum typical interval for all jobs. */
        uint32_t                cMsMinTypicalInterval;
        /** Update jobs for this sink. */
        struct
        {
            /** User specific argument. */
            void               *pvUser;
            /** The callback. */
            PFNAUDMIXSINKUPDATE pfnUpdate;
            /** Typical interval in milliseconds. */
            uint32_t            cMsTypicalInterval;
        } aUpdateJobs[8];
    } AIO;
    /** The sink's critical section. */
    RTCRITSECT              CritSect;
} AUDMIXSINK;

/** Value for AUDMIXSINK::uMagic. (Sir George Martin)  */
#define AUDMIXSINK_MAGIC                UINT32_C(0x19260103)
/** Value for AUDMIXSINK::uMagic after destruction. */
#define AUDMIXSINK_MAGIC_DEAD           UINT32_C(0x20160308)


/** @name AUDMIXSINK_STS_XXX - Sink status bits.
 * @{ */
/** No status specified. */
#define AUDMIXSINK_STS_NONE                  0
/** The sink is active and running. */
#define AUDMIXSINK_STS_RUNNING               RT_BIT(0)
/** Draining the buffers and pending stop - output only. */
#define AUDMIXSINK_STS_DRAINING              RT_BIT(1)
/** Drained the DMA buffer. */
#define AUDMIXSINK_STS_DRAINED_DMA           RT_BIT(2)
/** Drained the mixer buffer, only waiting for streams (drivers) now. */
#define AUDMIXSINK_STS_DRAINED_MIXBUF        RT_BIT(3)
/** Dirty flag.
 * - For output sinks this means that there is data in the sink which has not
 *   been played yet.
 * - For input sinks this means that there is data in the sink which has been
 *   recorded but not transferred to the destination yet.
 * @todo This isn't used for *anything* at the moment. Remove? */
#define AUDMIXSINK_STS_DIRTY                 RT_BIT(4)
/** @} */


/** @name Audio mixer methods
 * @{ */
int         AudioMixerCreate(const char *pszName, uint32_t fFlags, PAUDIOMIXER *ppMixer);
void        AudioMixerDestroy(PAUDIOMIXER pMixer, PPDMDEVINS pDevIns);
void        AudioMixerDebug(PAUDIOMIXER pMixer, PCDBGFINFOHLP pHlp, const char *pszArgs);
int         AudioMixerSetMasterVolume(PAUDIOMIXER pMixer, PCPDMAUDIOVOLUME pVol);
int         AudioMixerCreateSink(PAUDIOMIXER pMixer, const char *pszName, PDMAUDIODIR enmDir, PPDMDEVINS pDevIns, PAUDMIXSINK *ppSink);
/** @} */

/** @name Audio mixer sink methods
 * @{ */
int         AudioMixerSinkStart(PAUDMIXSINK pSink);
int         AudioMixerSinkDrainAndStop(PAUDMIXSINK pSink, uint32_t cbComming);
void        AudioMixerSinkDestroy(PAUDMIXSINK pSink, PPDMDEVINS pDevIns);
uint32_t    AudioMixerSinkGetReadable(PAUDMIXSINK pSink);
uint32_t    AudioMixerSinkGetWritable(PAUDMIXSINK pSink);
PDMAUDIODIR AudioMixerSinkGetDir(PCAUDMIXSINK pSink);
uint32_t    AudioMixerSinkGetStatus(PAUDMIXSINK pSink);
bool        AudioMixerSinkIsActive(PAUDMIXSINK pSink);
void        AudioMixerSinkReset(PAUDMIXSINK pSink);
int         AudioMixerSinkSetFormat(PAUDMIXSINK pSink, PCPDMAUDIOPCMPROPS pPCMProps, uint32_t cMsSchedulingHint);
int         AudioMixerSinkSetVolume(PAUDMIXSINK pSink, PCPDMAUDIOVOLUME pVol);
int         AudioMixerSinkUpdate(PAUDMIXSINK pSink, uint32_t cbDmaUsed, uint32_t cbDmaPeriod);

int         AudioMixerSinkAddUpdateJob(PAUDMIXSINK pSink, PFNAUDMIXSINKUPDATE pfnUpdate, void *pvUser, uint32_t cMsTypicalInterval);
int         AudioMixerSinkRemoveUpdateJob(PAUDMIXSINK pSink, PFNAUDMIXSINKUPDATE pfnUpdate, void *pvUser);
int         AudioMixerSinkSignalUpdateJob(PAUDMIXSINK pSink);
uint64_t    AudioMixerSinkTransferFromCircBuf(PAUDMIXSINK pSink, PRTCIRCBUF pCircBuf, uint64_t offStream,
                                              uint32_t idStream, PAUDIOHLPFILE pDbgFile);
uint64_t    AudioMixerSinkTransferToCircBuf(PAUDMIXSINK pSink, PRTCIRCBUF pCircBuf, uint64_t offStream,
                                            uint32_t idStream, PAUDIOHLPFILE pDbgFile);
bool        AudioMixerSinkLockIsOwner(PAUDMIXSINK pSink);
int         AudioMixerSinkLock(PAUDMIXSINK pSink);
int         AudioMixerSinkTryLock(PAUDMIXSINK pSink);
int         AudioMixerSinkUnlock(PAUDMIXSINK pSink);

int         AudioMixerSinkCreateStream(PAUDMIXSINK pSink, PPDMIAUDIOCONNECTOR pConnector, PCPDMAUDIOSTREAMCFG pCfg,
                                       PPDMDEVINS pDevIns, PAUDMIXSTREAM *ppStream);
int         AudioMixerSinkAddStream(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream);
void        AudioMixerSinkRemoveStream(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream);
void        AudioMixerSinkRemoveAllStreams(PAUDMIXSINK pSink);
/** @} */

/** @name Audio mixer stream methods
 * @{ */
void        AudioMixerStreamDestroy(PAUDMIXSTREAM pStream, PPDMDEVINS pDevIns, bool fImmediate);
/** @} */

/** @} */

#endif /* !VBOX_INCLUDED_SRC_Audio_AudioMixer_h */

