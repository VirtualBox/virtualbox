/* $Id: RecordingStream.h 110491 2025-07-31 09:28:23Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording stream code header.
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

#ifndef MAIN_INCLUDED_RecordingStream_h
#define MAIN_INCLUDED_RecordingStream_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef IN_VBOXSVC
# error "Using RecordingStream.h is prohibited in VBoxSVC!"
#endif

#include <map>
#include <vector>

#include <iprt/critsect.h>
#include <iprt/req.h>
#include <iprt/semaphore.h>

#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_RECORDING
#include "LoggingNew.h"

#include "RecordingInternals.h"

class Console;
class WebMWriter;
class RecordingContext;

/**
 * Virtual block worker base class.
 *
 * Can be used for keeping and working on a block set.
 */
class RecordingBlockWorker
{
protected:

    /** Constructor -- will throw rc on errors. */
    RecordingBlockWorker(const char *pszName,
                         uint32_t msTimeout = RT_INDEFINITE_WAIT, uint32_t msWait = 0, void *pvUser = NULL)
        : m_fShutdown(false)
        , m_tsLastRunMs(0)
        , m_msTimeout(msTimeout)
        , m_msWait(msWait)
        , m_pvUser(pvUser)
    {
        RTStrPrintf(m_szName, sizeof(m_szName), "%s", pszName);

        if (m_msWait)
        {
            int vrc = RTSemEventCreate(&m_hSemEvent);
            if (RT_FAILURE(vrc))
                throw vrc;

            /** @todo Handle threading creation here. */
        }
    }

    virtual ~RecordingBlockWorker(void)
    {
        /** @todo Handle threading shutdown here. */

        if (m_msWait)
            RTSemEventDestroy(m_hSemEvent);
    }

public:

    int Run(void);
    int Notify(void);

protected:

    /**
     * Pure virtual worker function. Must be implemented by the derived class.
     * Will be called by the Run() function.
     * Keep the implementation short so that this class can handle timeouts in a proper manner.
     *
     * @return VBox status code. Returning an error will abort the current worker run.
     * @retval VINF_CALLBACK_RETURN if the worker wants to signal that there is nothing more to do in the current iteration.
     * @param  msTimeout    Timeout (in ms) hint.
     *                      This gives the worker a hint for how long it can run.
     * @param  fShutdown    Shutdown flag.
     * @param  pvUser       Handed-in user supplied pointer. Might be NULL. */
    virtual DECLCALLBACK(int) Worker(uint64_t msTimeout, bool fShutdown, void *pvUser) = 0;

protected:

    /**
     * Returns whether the stream's processing timeout has been reached.
     *
     * @returns \c true if timeout has been reached, or \c false if not.
     */
    bool timeoutReached(void) const
    {
        return timeoutRemaining() == 0 ? true : false;
    }

    /**
     * Returns the remaining processing timeout (in ms).
     *
     * @returns Timeout remaining (in ms),
     * @retval  UINT64_MAX if no timeout was configured.
     * @retval  0 if timeout has been reached.
     */
    uint64_t timeoutRemaining(void) const
    {
        if (m_msTimeout == RT_INDEFINITE_WAIT)
            return UINT64_MAX;
        uint64_t const tsDiff = RTTimeMilliTS() - m_tsLastRunMs;
        return tsDiff < m_msTimeout ? m_msTimeout - tsDiff : 0;
    }

    /**
     * Resets the processing timeout for a worker iteration.
     */
    void timeoutReset(void)
    {
        m_tsLastRunMs = RTTimeMilliTS();
    }

    /**
     * Sets a new processing timeout.
     */
    void timeoutSet(uint32_t msTimeout)
    {
        m_msTimeout = msTimeout;
    }

protected:

    /** Worker name. Used for STAM paths.
     *  @todo */
    char                m_szName[8];
    /** Shutdown indicator. */
    bool                m_fShutdown;
    /** Last run timestamp (in ms).
     *  Set to 0 if never run (yet). */
    uint64_t            m_tsLastRunMs;
    /** Timeout (in ms) to use.
     *  This specifies the maximum time the worker is allowed to run per iteration.
     *  Specify RT_INDEFINITE_WAIT for running until all data is processed. */
    uint32_t            m_msTimeout;
    /** Event semaphore for triggering the worker. */
    RTSEMEVENT          m_hSemEvent;
    /** Waiting time (in ms) to use before calling the worker again.
     *  If set to 0 (default), the worker wil be called immediately.
     *  If set to RT_INDEFINITE_WAIT, the worker needs to be triggered via Notify() from another thread.
     *  Any other value will wait for the time (in ms) specified and calls the worker again. */
    uint32_t            m_msWait;
    /** User-supplied pointer for the worker function. */
    void               *m_pvUser;
};

/**
 * Implementation of the block worker class for the stream's housekeeping.
 *
 ** @todo Use this in a separate thread?
 */
class RecordingBlockWorkerHousekeeping : public RecordingBlockWorker
{
public:

    RecordingBlockWorkerHousekeeping(void)
        : RecordingBlockWorker("Housekeeping", RT_MS_1SEC /* ms timeout */) { }

    virtual ~RecordingBlockWorkerHousekeeping()
    {
        /* Invoke the worker one last time, to indicate shutdown. */
        m_fShutdown = true;
        Run();
    }

    int Insert(uint64_t uPTS, RecordingBlocks *pBlocks);

    DECLCALLBACK(int) Worker(uint64_t msTimeout, bool fShutdown, void *pvUser);

protected:

    /** Set of recording (data) blocks for this worker to process. */
    RecordingBlockSet   m_BlockSet;
};

/**
 * Class for managing a recording stream.
 *
 * A recording stream represents one entity to record (e.g. on screen / monitor),
 * so there is a 1:1 mapping (stream <-> monitors).
 */
class RecordingStream
{
public:

    RecordingStream(RecordingContext *pCtx, uint32_t uScreen, const ComPtr<IRecordingScreenSettings> &ScreenSettings, PRECORDINGCODEC pCodecAudio);

    virtual ~RecordingStream(void);

public:

    int Init(RecordingContext *pCtx, uint32_t uScreen, const ComPtr<IRecordingScreenSettings> &ScreenSettings, PRECORDINGCODEC pCodecAudio);
    int Uninit(void);

    int ThreadMain(int rcWait, uint64_t msTimestamp, RecordingBlockMap &commonBlocks);
    int SendAudioFrame(const void *pvData, size_t cbData, uint64_t msTimestamp);
    int SendCursorPos(uint8_t idCursor, PRECORDINGPOS pPos, uint64_t msTimestamp);
    int SendCursorShape(uint8_t idCursor, PRECORDINGVIDEOFRAME pShape, uint64_t msTimestamp);
    int SendVideoFrame(PRECORDINGVIDEOFRAME pFrame, uint64_t msTimestamp);
    int SendScreenChange(PRECORDINGSURFACEINFO pInfo, uint64_t msTimestamp, bool fForce = false);

    int Start(void);
    int Stop(void);

    const ComPtr<IRecordingScreenSettings> &GetSettings(void) const;
    uint16_t GetID(void) const { return this->m_uScreenID; };
#ifdef VBOX_WITH_AUDIO_RECORDING
    PRECORDINGCODEC GetAudioCodec(void) { return this->m_pCodecAudio; };
#endif
    PRECORDINGCODEC GetVideoCodec(void) { return &this->m_CodecVideo; };

    bool IsLimitReached(uint64_t msTimestamp) const;
    bool IsFeatureEnabled(RecordingFeature_T enmFeature) const;
    bool NeedsUpdate(uint64_t msTimestamp) const;

public:

    static DECLCALLBACK(int) codecWriteDataCallback(PRECORDINGCODEC pCodec, const void *pvData, size_t cbData, uint64_t msAbsPTS, uint32_t uFlags, void *pvUser);

protected:

    int open(const ComPtr<IRecordingScreenSettings> &ScreenSettings);
    int close(void);

    int initInternal(RecordingContext *pCtx, uint32_t uScreen, const ComPtr<IRecordingScreenSettings> &ScreenSettings, PRECORDINGCODEC pCodecAudio);
    int uninitInternal(void);

    int initVideo(const ComPtr<IRecordingScreenSettings> &ScreenSettings);
    int unitVideo(void);

    void housekeepingWorker(void);

    bool isLimitReachedInternal(uint64_t msTimestamp) const;
    int iterateInternal(uint64_t msTimestamp);

    int addFrame(PRECORDINGFRAME pFrame, uint64_t msTimestamp);
    int process(RecordingBlockSet &streamBlocks, RecordingBlockMap &commonBlocks);
    int codecWriteToWebM(PRECORDINGCODEC pCodec, const void *pvData, size_t cbData, uint64_t msAbsPTS, uint32_t uFlags);

    void lock(void);
    void unlock(void);

protected:

    /**
     * Enumeration for a recording stream state.
     */
    enum RECORDINGSTREAMSTATE
    {
        /** Stream not initialized. */
        RECORDINGSTREAMSTATE_UNINITIALIZED = 0,
        /** Stream was initialized. */
        RECORDINGSTREAMSTATE_INITIALIZED   = 1,
        /** Stream was started (recording active). */
        RECORDINGSTREAMSTATE_STARTED       = 2,
        /** Stream is in paused state. */
        RECORDINGSTREAMSTATE_PAUSED        = 3,
        /** Stream is stopping. */
        RECORDINGSTREAMSTATE_STOPPING      = 4,
        /** Stream has been stopped (non-continuable). */
        RECORDINGSTREAMSTATE_STOPPED       = 5,
        /** The usual 32-bit hack. */
        RECORDINGSTREAMSTATE_32BIT_HACK    = 0x7fffffff
    };

    /** Pointer (weak) to console object.
     *  Needed for STAM. */
    Console * const         m_pConsole;
    /** Recording context this stream is associated to. */
    RecordingContext       *m_pCtx;
    /** The current state. */
    RECORDINGSTREAMSTATE    m_enmState;
    struct
    {
        /** File handle to use for writing. */
        RTFILE              m_hFile;
        /** Pointer to WebM writer instance being used. */
        WebMWriter         *m_pWEBM;
    } File;
    /** Track number of audio stream.
     *  Set to UINT8_MAX if not being used. */
    uint8_t             m_uTrackAudio;
    /** Track number of video stream.
     *  Set to UINT8_MAX if not being used. */
    uint8_t             m_uTrackVideo;
    /** Screen ID. */
    uint16_t            m_uScreenID;
    /** Critical section to serialize access. */
    RTCRITSECT          m_CritSect;
    /** Timestamp (in ms) of when recording has been started. */
    uint64_t            m_tsStartMs;
#ifdef VBOX_WITH_AUDIO_RECORDING
    /** Pointer to audio codec instance data to use.
     *
     *  We multiplex audio data from the recording context to all streams,
     *  to avoid encoding the same audio data for each stream. We ASSUME that
     *  all audio data of a VM will be the same for each stream at a given
     *  point in time.
     *
     *  Might be NULL if not being used. */
    PRECORDINGCODEC     m_pCodecAudio;
#endif /* VBOX_WITH_AUDIO_RECORDING */
#ifdef VBOX_WITH_STATISTICS
    /** STAM values. */
    struct
    {
        STAMCOUNTER     cVideoFramesAdded;
        STAMCOUNTER     cVideoFramesToEncode;
        STAMCOUNTER     cVideoFramesEncoded;
        STAMCOUNTER     cVideoFramesHousekeeping;
# ifdef VBOX_WITH_AUDIO_RECORDING
        STAMCOUNTER     cAudioFramesAdded;
        /* Note: STAM values for frames to encode / encoded / housekeeping
                 will be handled in the recording context, as this is common data
                 which needs to be multiplexed for now. */
        STAMCOUNTER     cAudioFramesToEncode;
        STAMCOUNTER     cAudioFramesEncoded;
        STAMCOUNTER     cAudioFramesHousekeeping;
# endif
        STAMPROFILE     profileFnProcessTotal;
        STAMPROFILE     profileFnProcessVideo;
        STAMPROFILE     profileFnProcessAudio;
    } m_STAM;
#endif /* VBOX_WITH_STATISTICS */
    /** Video codec instance data to use. */
    RECORDINGCODEC      m_CodecVideo;
    /** Screen settings to use. */
    ComPtr<IRecordingScreenSettings>
                        m_Settings;
    /** Video-specific runtime data. */
    struct
    {
        /** Current surface screen info being used.
         *  Can be changed by a SendScreenChange() call. */
        RECORDINGSURFACEINFO ScreenInfo;
    } m_Video;
    /** Cached (const) settings from IRecordingScreen. Same naming (minus notation).
     *  Kept around for speed reasons during runtime. */
    struct
    {
        BOOL                   fEnabled;
        RecordingFeatureMap    mapFeatures;
        RecordingDestination_T enmDestination;
        ULONG                  uMaxTime;
        ULONG                  uMaxFileSize;
    } m_SettingsCache;
    /** Set of unprocessed recording (data) blocks for this stream. */
    RecordingBlockSet   m_BlockSet;
    /** Housekeeping block worker. */
    RecordingBlockWorkerHousekeeping
                        m_Housekeeping;
};

/** Vector of recording streams. */
typedef std::vector <RecordingStream *> RecordingStreams;

#endif /* !MAIN_INCLUDED_RecordingStream_h */

