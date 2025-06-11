/* $Id: RecordingStream.h 109818 2025-06-11 08:31:50Z andreas.loeffler@oracle.com $ */
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

#include <map>
#include <vector>

#include <iprt/critsect.h>
#include <iprt/req.h>

#include "RecordingInternals.h"

class Console;
class WebMWriter;
class RecordingContext;

/** Structure for queuing all blocks bound to a single timecode.
 *  This can happen if multiple tracks are being involved. */
struct RecordingBlocks
{
    virtual ~RecordingBlocks()
    {
        Clear();
    }

    /**
     * Resets a recording block list by removing (destroying)
     * all current elements.
     */
    void Clear()
    {
        while (!List.empty())
        {
            RecordingBlock *pBlock = List.front();
            if (pBlock->GetRefs() == 0)
            {
                List.pop_front();
                delete pBlock;
            }
        }

        Assert(List.size() == 0);
    }

    /** The actual block list for this timecode. */
    RecordingBlockList List;
};

/** A block map containing all currently queued blocks.
 *  The key specifies a unique timecode, whereas the value
 *  is a list of blocks which all correlate to the same key (timecode). */
typedef std::map<uint64_t, RecordingBlocks *> RecordingBlockMap;

/**
 * Structure for holding a set of recording (data) blocks.
 */
struct RecordingBlockSet
{
    /**
     * Constructor.
     *
     * Will throw rc on failure.
     */
    RecordingBlockSet()
        : tsLastProcessedMs(0)
    {
        int const vrc = RTCritSectInit(&CritSect);
        if (RT_FAILURE(vrc))
            throw vrc;
    }

    virtual ~RecordingBlockSet()
    {
        Clear();

        RTCritSectDelete(&CritSect);
    }

    /**
     * Inserts a block list within the given PTS.
     */
    int Insert(uint64_t uPTS, RecordingBlocks *pBlocks)
    {
        int vrc = RTCritSectEnter(&CritSect);
        if (RT_SUCCESS(vrc))
        {
            try
            {
                Map.insert(std::make_pair(uPTS, pBlocks));
            }
            catch (std::bad_alloc &)
            {
                vrc = VERR_NO_MEMORY;
            }
            RTCritSectLeave(&CritSect);
        }

        return vrc;
    }

    /**
     * Resets a recording block set by removing (destroying)
     * all current elements.
     */
    void Clear(void)
    {
        int vrc = RTCritSectEnter(&CritSect);
        if (RT_SUCCESS(vrc))
        {
            RecordingBlockMap::iterator it = Map.begin();
            while (it != Map.end())
            {
                it->second->Clear();
                delete it->second;
                Map.erase(it);
                it = Map.begin();
            }

            Assert(Map.size() == 0);

            RTCritSectLeave(&CritSect);
        }
    }

    /** Critical section for protecting the set. */
    RTCRITSECT        CritSect;
    /** Timestamp (in ms) when this set was last processed.
     *  Set to 0 if not processed yet. */
    uint64_t          tsLastProcessedMs;
    /** All blocks related to this block set. */
    RecordingBlockMap Map;
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

    RecordingStream(Console *pConsole, RecordingContext *pCtx, uint32_t uScreen, const settings::RecordingScreen &Settings);

    virtual ~RecordingStream(void);

public:

    int Init(RecordingContext *pCtx, uint32_t uScreen, const settings::RecordingScreen &Settings);
    int Uninit(void);

    int ThreadMain(int rcWait, uint64_t msTimestamp, RecordingBlockMap &commonBlocks);
    int SendAudioFrame(const void *pvData, size_t cbData, uint64_t msTimestamp);
    int SendCursorPos(uint8_t idCursor, PRECORDINGPOS pPos, uint64_t msTimestamp);
    int SendCursorShape(uint8_t idCursor, PRECORDINGVIDEOFRAME pShape, uint64_t msTimestamp);
    int SendVideoFrame(PRECORDINGVIDEOFRAME pFrame, uint64_t msTimestamp);
    int SendScreenChange(PRECORDINGSURFACEINFO pInfo, uint64_t msTimestamp, bool fForce = false);

    const settings::RecordingScreen &GetConfig(void) const;
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

    int open(const settings::RecordingScreen &screenSettings);
    int close(void);

    int initInternal(RecordingContext *pCtx, uint32_t uScreen, const settings::RecordingScreen &screenSettings);
    int uninitInternal(void);

    int initVideo(const settings::RecordingScreen &screenSettings);
    int unitVideo(void);

    bool isLimitReachedInternal(uint64_t msTimestamp) const;
    int iterateInternal(uint64_t msTimestamp);

    int addFrame(PRECORDINGFRAME pFrame, uint64_t msTimestamp);
    int process(RecordingBlockSet &streamBlocks, RecordingBlockMap &commonBlocks);
    int codecWriteToWebM(PRECORDINGCODEC pCodec, const void *pvData, size_t cbData, uint64_t msAbsPTS, uint32_t uFlags);

    void lock(void);
    void unlock(void);

protected:

    static DECLCALLBACK(void) doHousekeepingCallback(RecordingStream *pThis, RecordingBlockSet *pSet);

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
    bool                m_fEnabled;
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
        STAMCOUNTER     cFramesAdded;
        STAMCOUNTER     cFramesEncoded;
        STAMPROFILE     profileFrameEncode;
        STAMPROFILE     profileFnProcessTotal;
        STAMPROFILE     profileFnProcessVideo;
        STAMPROFILE     profileFnProcessAudio;
        STAMPROFILE     profileFnHouekeeping;
    } m_STAM;
#endif /* VBOX_WITH_STATISTICS */
    /** Video codec instance data to use. */
    RECORDINGCODEC      m_CodecVideo;
    /** Screen settings to use. */
    settings::RecordingScreen
                        m_ScreenSettings;
    /** Request pool for async tasks. */
    RTREQPOOL           m_hReqPool;
    /** Set of unprocessed recording (data) blocks for this stream. */
    RecordingBlockSet   m_BlockSet;
    /** Set of recording (data) blocks for this stream done processing. */
    RecordingBlockSet   m_BlockSetHousekeeping;
};

/** Vector of recording streams. */
typedef std::vector <RecordingStream *> RecordingStreams;

#endif /* !MAIN_INCLUDED_RecordingStream_h */

