/* $Id: RecordingContext.h 110435 2025-07-28 13:12:27Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording code header. Used by VBoxSVC + VBoxC.
 *
 * Note: Keep this header as abstract as possible, to not drag in
 *       any internal recording structs / definitions.
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

#ifndef MAIN_INCLUDED_RecordingContext_h
#define MAIN_INCLUDED_RecordingContext_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <map>

#include <VBox/err.h>

#include "VirtualBoxBase.h"

class Console;
class Progress;
class RecordingContextImpl;
class RecordingStream;

struct RECORDINGCODEC;
typedef RECORDINGCODEC *PRECORDINGCODEC;

/** List for keeping a recording feature list. */
typedef std::map<RecordingFeature_T, bool> RecordingFeatureMap;

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
    /** Recording was paused (to resume later). */
    RECORDINGSTS_PAUSED        = 3,
    /** Recording is stopping.
     *  Will happen when encoding / writing pending data. */
    RECORDINGSTS_STOPPING      = 4,
    /** Recording was stopped (non-continuable). */
    RECORDINGSTS_STOPPED       = 5,
    /** Limit has been reached and thus the recording was stopped. */
    RECORDINGSTS_LIMIT_REACHED = 6,
    /** Recording experienced an error. */
    RECORDINGSTS_FAILURE       = 7,
    /** The usual 32-bit hack. */
    RECORDINGSTS_32BIT_HACK    = 0x7fffffff
};

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
 * Class for managing a recording context.
 */
class RecordingContext
{
public:

    /** Recording context callback table. */
    struct CALLBACKS
    {
       /**
        * Recording state got changed. Optional.
        *
        * @param   enmSts               New status.
        * @param   uScreen              Screen ID.
        *                               Set to UINT32_MAX if the limit of all streams was reached.
        * @param   vrc                  Result code of state change.
        * @param   pvUser               User-supplied pointer. Might be NULL.
        */
        DECLCALLBACKMEMBER(void, pfnStateChanged, (RECORDINGSTS enmSts, uint32_t uScreen, int vrc, void *pvUser));

        /** User-supplied pointer. Might be NULL. */
        void *pvUser;
    };

public:

    RecordingContext();

    virtual ~RecordingContext(void);

public:

    Console *GetConsole(void) const;
    const ComPtr<IRecordingSettings> &GetSettings(void) const;
    RecordingStream *GetStream(unsigned uScreen) const;
    size_t GetStreamCount(void) const;

    int Create(Console *pConsole, ComPtr<IProgress> &pProgress);
    void Destroy(void);

    int Start(void);
    int Stop(void);

    int SetError(int rc, const com::Utf8Str &strText);

    int SendAudioFrame(const void *pvData, size_t cbData, uint64_t uTimestampMs);
    int SendVideoFrame(uint32_t uScreen, uint32_t uWidth, uint32_t uHeight, RECORDINGPIXELFMT enmPixelFmt, uint32_t uBytesPerLine, const void *pvData, size_t cbData, uint32_t uPosX, uint32_t uPosY, uint64_t msTimestamp);
    int SendCursorPositionChange(uint32_t uScreen, int32_t x, int32_t y, uint64_t msTimestamp);
    int SendCursorShapeChange(bool fVisible, bool fAlpha, uint32_t xHot, uint32_t yHot, uint32_t uWidth, uint32_t uHeight, const uint8_t *pu8Shape, size_t cbShape, uint64_t msTimestamp);
    int SendScreenChange(uint32_t uScreen, uint32_t uWidth, uint32_t uHeight, RECORDINGPIXELFMT enmPixelFmt, uint32_t uBytesPerLine, uint64_t uTimestampMs);

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

public:

    int OnLimitReached(uint32_t uScreen, int vrc);

protected:

    /** Pointer to the console object. */
    Console *m_pConsole;

    /** Protected internal data. */
    friend RecordingContextImpl;
    RecordingContextImpl *m;
};
#endif /* !MAIN_INCLUDED_RecordingContext_h */

