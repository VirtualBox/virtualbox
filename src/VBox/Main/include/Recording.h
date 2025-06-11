/* $Id: Recording.h 109818 2025-06-11 08:31:50Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording code header.
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

#ifndef MAIN_INCLUDED_Recording_h
#define MAIN_INCLUDED_Recording_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/err.h>
#include <VBox/settings.h>

#include "RecordingStream.h"

class Console;
class Progress;

/** No flags specified. */
#define VBOX_RECORDING_CURSOR_F_NONE       0
/** Cursor is visible. */
#define VBOX_RECORDING_CURSOR_F_VISIBLE    RT_BIT(0)
/** Cursor shape contains an alpha mask. */
#define VBOX_RECORDING_CURSOR_F_ALPHA      RT_BIT(1)
/** Cursor state flags valid mask. */
#define VBOX_RECORDING_CURSOR_F_VALID_MASK 0x3

/**
 * Class for keeping a recording cursor state.
 */
class RecordingCursorState
{
public:

    RecordingCursorState();
    virtual ~RecordingCursorState();

    void Destroy();

    int CreateOrUpdate(bool fAlpha, uint32_t uWidth, uint32_t uHeight, const uint8_t *pu8Shape, size_t cbShape);

    int Move(int32_t iX, int32_t iY);

    /** Cursor state flags. */
    uint32_t            m_fFlags;
    /** The current cursor shape. */
    RECORDINGVIDEOFRAME m_Shape;
};

/**
 * Enumeration for a recording context state.
 */
enum RECORDINGSTS
{
    /** Recording not initialized. */
    RECORDINGSTS_UNINITIALIZED = 0,
    /** Recording was created. */
    RECORDINGSTS_CREATED       = 1,
    /** Recording was started. */
    RECORDINGSTS_STARTED       = 2,
    /** Recording was stopped. */
    RECORDINGSTS_STOPPED       = 3,
    /** Limit has been reached. */
    RECORDINGSTS_LIMIT_REACHED = 4,
    /** Recording experienced an error. */
    RECORDINGSTS_FAILURE       = 5,
    /** The usual 32-bit hack. */
    RECORDINGSTS_32BIT_HACK    = 0x7fffffff
};

/**
 * Class for managing a recording context.
 */
class RecordingContext
{
    friend RecordingStream;

public:

    /** Recording context callback table. */
    struct CALLBACKS
    {
       /**
        * Recording state got changed. Optional.
        *
        * @param   pCtx                 Recording context.
        * @param   enmSts               New status.
        * @param   uScreen              Screen ID.
        *                               Set to UINT32_MAX if the limit of all streams was reached.
        * @param   vrc                  Result code of state change.
        * @param   pvUser               User-supplied pointer. Might be NULL.
        */
        DECLCALLBACKMEMBER(void, pfnStateChanged, (RecordingContext *pCtx, RECORDINGSTS enmSts, uint32_t uScreen, int vrc, void *pvUser));

        /** User-supplied pointer. Might be NULL. */
        void *pvUser;
    };

public:

    RecordingContext();

    virtual ~RecordingContext(void);

public:

    const settings::Recording &GetConfig(void) const;
    RecordingStream *GetStream(unsigned uScreen) const;
    size_t GetStreamCount(void) const;
#ifdef VBOX_WITH_AUDIO_RECORDING
    PRECORDINGCODEC GetCodecAudio(void) { return &this->m_CodecAudio; }
#endif

    int Create(Console *pConsole, const settings::Recording &Settings, ComPtr<IProgress> &pProgress);
    void Destroy(void);

    int Start(void);
    int Stop(void);

    int SetError(int rc, const com::Utf8Str &strText);

    int SendAudioFrame(const void *pvData, size_t cbData, uint64_t uTimestampMs);
    int SendVideoFrame(uint32_t uScreen, PRECORDINGVIDEOFRAME pFrame, uint64_t msTimestamp);
    int SendCursorPositionChange(uint32_t uScreen, int32_t x, int32_t y, uint64_t msTimestamp);
    int SendCursorShapeChange(bool fVisible, bool fAlpha, uint32_t xHot, uint32_t yHot, uint32_t uWidth, uint32_t uHeight, const uint8_t *pu8Shape, size_t cbShape, uint64_t msTimestamp);
    int SendScreenChange(uint32_t uScreen, PRECORDINGSURFACEINFO pInfo, uint64_t uTimestampMs);

public:

    uint64_t GetCurrentPTS(void) const;
    bool IsFeatureEnabled(RecordingFeature_T enmFeature);
    bool IsFeatureEnabled(uint32_t uScreen, RecordingFeature_T enmFeature);
    bool IsReady(void);
    bool IsStarted(void);
    bool IsLimitReached(void);
    bool IsLimitReached(uint32_t uScreen, uint64_t msTimestamp);
    bool NeedsUpdate(uint32_t uScreen, uint64_t msTimestamp);
    void SetCallbacks(RecordingContext::CALLBACKS *pCallbacks, void *pvUser);

    /** The state mouse cursor state.
     *  We currently only support one mouse cursor at a time. */
    RecordingCursorState         m_Cursor;


protected:

    int createInternal(Console *ptrConsole, const settings::Recording &Settings, ComPtr<IProgress> &pProgress);
    void reset(void);
    int startInternal(void);
    int stopInternal(void);

    void destroyInternal(void);

    RecordingStream *getStreamInternal(unsigned uScreen) const;

    int processCommonData(RecordingBlockMap &mapCommon, RTMSINTERVAL msTimeout);
    int writeCommonData(RecordingBlockMap &mapCommon, PRECORDINGCODEC pCodec, const void *pvData, size_t cbData, uint64_t msAbsPTS, uint32_t uFlags);

    int lock(void);
    int unlock(void);

    int onLimitReached(uint32_t uScreen, int vrc);

    bool progressIsCanceled(void) const;
    bool progressIsCompleted(void) const;
    int progressCreate(const settings::Recording &Settings, ComObjPtr<Progress> &pProgress);
    int progressNotifyComplete(HRESULT hrc = S_OK, IVirtualBoxErrorInfo *pErrorInfo = NULL);
    int progressSet(uint32_t uOp, const com::Bstr &strDesc);
    int progressSet(uint64_t msTimestamp);

    static DECLCALLBACK(int) threadMain(RTTHREAD hThreadSelf, void *pvUser);

    int threadNotify(void);

protected:

    int audioInit(const settings::RecordingScreen &screenSettings);

protected:

    static void               s_progressCancelCallback(void *pvUser);

    static DECLCALLBACK(void) s_recordingStateChangedCallback(RecordingContext *pCtx, RECORDINGSTS enmSts, uint32_t uScreen, int vrc, void *pvUser);

    static DECLCALLBACK(int)  s_audioCodecWriteDataCallback(PRECORDINGCODEC pCodec, const void *pvData, size_t cbData, uint64_t msAbsPTS, uint32_t uFlags, void *pvUser);

protected:

    /** Pointer to the console object. */
    Console                     *m_pConsole;
    /** Used recording settings. */
    settings::Recording          m_Settings;
    /** The current state. */
    RECORDINGSTS                 m_enmState;
    /** Callback table. */
    CALLBACKS                    m_Callbacks;
    /** Critical section to serialize access. */
    RTCRITSECT                   m_CritSect;
    /** Semaphore to signal the encoding worker thread. */
    RTSEMEVENT                   m_WaitEvent;
    /** Current operation of progress. Set to 0 if not started yet, >= 1 if started. */
    ULONG                        m_ulCurOp;
    /** Number of progress operations. Always >= 1. */
    ULONG                        m_cOps;
    /** The progress object assigned to this context.
     *  Might be NULL if not being used. */
    const ComObjPtr<Progress>    m_pProgress;
    /** Shutdown indicator. */
    bool                         m_fShutdown;
    /** Encoding worker thread. */
    RTTHREAD                     m_Thread;
    /** Vector of current recording streams.
     *  Per VM screen (display) one recording stream is being used. */
    RecordingStreams             m_vecStreams;
    /** Number of streams in vecStreams which currently are enabled for recording. */
    uint16_t                     m_cStreamsEnabled;
    /** Timestamp (in ms) of when recording has been started.
     *  Set to 0 if not started (yet). */
    uint64_t                     m_tsStartMs;
#ifdef VBOX_WITH_AUDIO_RECORDING
    /** Audio codec to use.
     *
     *  We multiplex audio data from this recording context to all streams,
     *  to avoid encoding the same audio data for each stream. We ASSUME that
     *  all audio data of a VM will be the same for each stream at a given
     *  point in time. */
    RECORDINGCODEC               m_CodecAudio;
#endif /* VBOX_WITH_AUDIO_RECORDING */
#ifdef VBOX_WITH_STATISTICS
    /** STAM values. */
    struct
    {
        STAMPROFILE              profileDataCommon;
        STAMPROFILE              profileDataStreams;
    } m_STAM;
#endif /* VBOX_WITH_STATISTICS */
    /** Block map of raw common data blocks which need to get encoded first. */
    RecordingBlockMap            m_mapBlocksRaw;
    /** Block map of encoded common blocks.
     *
     *  Only do the encoding of common data blocks only once and then multiplex
     *  the encoded data to all affected recording streams.
     *
     *  This avoids doing the (expensive) encoding + multiplexing work in other
     *  threads like EMT / audio async I/O.
     *
     *  For now this only affects audio, e.g. all recording streams
     *  need to have the same audio data at a specific point in time. */
    RecordingBlockMap            m_mapBlocksEncoded;
};
#endif /* !MAIN_INCLUDED_Recording_h */

