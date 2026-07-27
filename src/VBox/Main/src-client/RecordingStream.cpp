/* $Id: RecordingStream.cpp 114800 2026-07-27 16:53:06Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording stream code.
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

#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_RECORDING
#include "LoggingNew.h"

#include <iprt/asm.h>
#include <iprt/path.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/cpp/utils.h> /* For unconst() */

#ifdef VBOX_WITH_AUDIO_RECORDING
# include <VBox/vmm/pdmaudioinline.h>
#endif

#include "ConsoleImpl.h"
#include "RecordingStream.h"
#include "RecordingInternals.h"
#include "RecordingUtils.h"
#include "WebMWriter.h"

#ifdef VBOX_WITH_STATISTICS
# include <VBox/vmm/vmmr3vtable.h>
#endif


/** Video pool tile width (in pixel). */
#define VBOX_RECORDING_VIDEO_POOL_TILE_WIDTH   400
/** Video pool tile height (in pixel). */
#define VBOX_RECORDING_VIDEO_POOL_TILE_HEIGHT  300

#ifdef VBOX_WITH_RECORDING_STATS
/** Dirty-area byte thresholds for logging the pool sizes. */
static uint32_t const g_acbDirtyBucketLimits[] = { _16K, _64K, _256K, _1M, _4M };

/**
 * Updates per-frame dirty/pool statistics for queued video frames.
 *
 * @param   pVideoFrame         Queued video frame.
 */
void RecordingStream::statsVideoOnFrameQueued(PRECORDINGVIDEOFRAME pVideoFrame)
{
    m_Video.Stats.cbDirtyTotal += pVideoFrame->cbBuf;

    uint32_t idxDirtyBucket = RT_ELEMENTS(g_acbDirtyBucketLimits); /* Last bucket: > max threshold. */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_acbDirtyBucketLimits); i++)
        if (pVideoFrame->cbBuf <= g_acbDirtyBucketLimits[i])
        {
            idxDirtyBucket = i;
            break;
        }
    m_Video.Stats.acDirtyBuckets[idxDirtyBucket]++;

    /* Occupancy approximation:
     * - Video pool pressure via pending video frames vs configured video pool capacity.
     * - Command queue pressure via RTCircBuf used/capacity. */
    m_Video.Stats.cPendingVideo++;
    uint64_t const cPendingVideo = m_Video.Stats.cPendingVideo;
    size_t   const cVideoCap     = m_aFramePool[RECORDINGFRAME_TYPE_VIDEO].cFrames;
    RecordingStatsPoolPressureSample(&m_Video.Stats.PoolPressure, cPendingVideo, cVideoCap);

    size_t const cbCmdUsed = RTCircBufUsed(m_pCmdCircBuf);
    size_t const cbCmdSize = RTCircBufSize(m_pCmdCircBuf);
    if (cbCmdSize)
    {
        uint64_t const uPctCmd = RT_MIN((cbCmdUsed * 100) / cbCmdSize, 100);
        if (uPctCmd > m_Video.Stats.uCmdQPctMax)
            m_Video.Stats.uCmdQPctMax = uPctCmd;
    }
}

/**
 * Logs accumulated video dirty/pool bucket statistics.
 *
 * @param   pszPrefix           Log line prefix.
 */
void RecordingStream::statsVideoLogBuckets(const char *pszPrefix) const
{
    uint64_t const cDirtySamples =   m_Video.Stats.acDirtyBuckets[0] + m_Video.Stats.acDirtyBuckets[1]
                                   + m_Video.Stats.acDirtyBuckets[2] + m_Video.Stats.acDirtyBuckets[3]
                                   + m_Video.Stats.acDirtyBuckets[4] + m_Video.Stats.acDirtyBuckets[5];
    uint64_t const uPctB0 = cDirtySamples ? (m_Video.Stats.acDirtyBuckets[0] * 100) / cDirtySamples : 0;
    uint64_t const uPctB1 = cDirtySamples ? (m_Video.Stats.acDirtyBuckets[1] * 100) / cDirtySamples : 0;
    uint64_t const uPctB2 = cDirtySamples ? (m_Video.Stats.acDirtyBuckets[2] * 100) / cDirtySamples : 0;
    uint64_t const uPctB3 = cDirtySamples ? (m_Video.Stats.acDirtyBuckets[3] * 100) / cDirtySamples : 0;
    uint64_t const uPctB4 = cDirtySamples ? (m_Video.Stats.acDirtyBuckets[4] * 100) / cDirtySamples : 0;
    uint64_t const uPctB5 = cDirtySamples ? (m_Video.Stats.acDirtyBuckets[5] * 100) / cDirtySamples : 0;

    size_t const cVideoCap = m_aFramePool[RECORDINGFRAME_TYPE_VIDEO].cFrames;

    /* Note: We want this as release logging, for debugging potential customer issues. */
    LogRel(("Recording: %s: dirtyTotal=%RU64, <=16K=%RU64(%RU64%%) <=64K=%RU64(%RU64%%) <=256K=%RU64(%RU64%%) "
            "<=1M=%RU64(%RU64%%) <=4M=%RU64(%RU64%%) >4M=%RU64(%RU64%%), maxCmdQ=%RU64%%\n",
            pszPrefix,
            m_Video.Stats.cbDirtyTotal,
            m_Video.Stats.acDirtyBuckets[0], uPctB0,
            m_Video.Stats.acDirtyBuckets[1], uPctB1,
            m_Video.Stats.acDirtyBuckets[2], uPctB2,
            m_Video.Stats.acDirtyBuckets[3], uPctB3,
            m_Video.Stats.acDirtyBuckets[4], uPctB4,
            m_Video.Stats.acDirtyBuckets[5], uPctB5,
            m_Video.Stats.uCmdQPctMax));

    RecordingStatsPoolPressureLog(pszPrefix, "Video pool",
                                  &m_Video.Stats.PoolPressure,
                                  m_Video.Stats.cPendingVideo, cVideoCap);

    LogRel(("Recording: %s: Stream #%u:\n", pszPrefix, m_uScreenID));
    size_t cbSize = RTCircBufSize(m_pCmdCircBuf);
    size_t cbUsed = RTCircBufUsed(m_pCmdCircBuf);
    LogRel(("Recording: %s:   Command Buffer Usage: %u%% (%zu/%zu)\n",
            pszPrefix, cbUsed ? (100 * cbUsed) / cbSize : 0, cbUsed, cbSize));
    LogRel(("Recording: %s:   Frame Pool Usage: ", pszPrefix));
    for (size_t q = 1; q < RT_ELEMENTS(m_aFramePool); q++)
    {
        const PRECORDINGFRAMEPOOL pPool = (const PRECORDINGFRAMEPOOL)&m_aFramePool[q];
        if (RecordingFramePoolIsInitialized(pPool))
        {
            cbSize = RecordingFramePoolSize(pPool);
            cbUsed = RecordingFramePoolUsed(pPool);
            LogRel(("[%zu] %u%% (%zu/%zu) ", q, cbUsed ? (100 * cbUsed) / cbSize : 0, cbUsed, cbSize));
        }
    }
    LogRel(("\n"));
}
#endif /* VBOX_WITH_RECORDING_STATS */

/**
 * Recording stream constructor.
 */
RecordingStream::RecordingStream(RecordingContext *a_pCtx, uint32_t uScreen,
                                 const ComPtr<IRecordingScreenSettings> &ScreenSettings,
                                 PRECORDINGFRAMEPOOL paPoolsCommonEnc)
    : m_pConsole(NULL)
    , m_enmState(RECORDINGSTREAMSTATE_UNINITIALIZED)
    , m_WaitEvent(NIL_RTSEMEVENT)
    , m_Thread(NIL_RTTHREAD)
    , m_fShutdown(false)
{
    RT_ZERO(m_Renderer);
    RT_ZERO(m_CodecVideo);
    RT_ZERO(m_CodecInputFrame);

    int vrc2 = initCommon(a_pCtx, uScreen, ScreenSettings, paPoolsCommonEnc);
    if (RT_FAILURE(vrc2))
        throw vrc2;
}

/**
 * Recording stream destructor.
 */
RecordingStream::~RecordingStream(void)
{
    uninitVideo();

#ifdef VBOX_WITH_AUDIO_RECORDING
    uninitAudio();
#endif

    uninitCommon();
}

/**
 * Opens a recording stream.
 *
 * @returns VBox status code.
 * @param   ScreenSettings      Recording screen settings to use.
 */
int RecordingStream::open(const ComPtr<IRecordingScreenSettings> &ScreenSettings)
{
    /* Sanity. */
    Assert(m_SettingsCache.enmDestination != RecordingDestination_None);

    int vrc;

    switch (m_SettingsCache.enmDestination)
    {
        case RecordingDestination_File:
        {
            Bstr bstrFilename;
            HRESULT hrc = ScreenSettings->COMGETTER(Filename)(bstrFilename.asOutParam());
            AssertComRCBreak(hrc, vrc = VERR_INVALID_PARAMETER);
            AssertBreakStmt(bstrFilename.isNotEmpty(), vrc = VERR_INVALID_PARAMETER);

            Utf8Str strFilename(bstrFilename);
            const char *pszFile = strFilename.c_str();

            RTFILE hFile = NIL_RTFILE;
            vrc = RTFileOpen(&hFile, pszFile, RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE);
            if (RT_SUCCESS(vrc))
            {
                LogRel2(("Recording: Opened file '%s'\n", pszFile));

                try
                {
                    Assert(File.m_pWEBM == NULL);
                    File.m_pWEBM = new WebMWriter();
                }
                catch (std::bad_alloc &)
                {
                    vrc = VERR_NO_MEMORY;
                }

                if (RT_SUCCESS(vrc))
                    this->File.m_hFile = hFile;
            }
            else
                LogRel(("Recording: Failed to open file '%s' for screen %RU32, vrc=%Rrc\n",
                        pszFile ? pszFile : "<Unnamed>", m_uScreenID, vrc));

            if (RT_FAILURE(vrc))
            {
                if (hFile != NIL_RTFILE)
                    RTFileClose(hFile);
            }

            break;
        }

        default:
            vrc = VERR_NOT_IMPLEMENTED;
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Returns the recording stream's used configuration.
 *
 * @returns The recording stream's used configuration.
 */
const ComPtr<IRecordingScreenSettings> &RecordingStream::GetSettings(void) const
{
    return m_Settings;
}

/**
 * Sets the video output target description and switches the renderer
 * to the output target backend.
 *
 * @returns VBox status code.
 * @param   pDesc               Video output target description to set.
 */
int RecordingStream::SetVideoOutputTargetDesc(PDMDISPLAYOUTPUTTARGETDESC const *pDesc)
{
    int vrc2 = RTCritSectEnter(&m_RenderCritSect);
    AssertRC(vrc2);

    int vrc = VINF_SUCCESS;

    RecordingRenderDestroy(&m_Renderer);

    if (pDesc)
    {
        LogRel2(("Recording: Got output target description for screen #%RU32: %RU32x%RU32 (format %#x, seq %RU64)\n",
                 pDesc->idScreen, pDesc->cWidth, pDesc->cHeight, pDesc->enmFormat, *pDesc->pu64UpdateSequenceNumber));

        AssertReturnStmt(pDesc->idScreen == m_uScreenID, RTCritSectLeave(&m_RenderCritSect), VERR_INVALID_PARAMETER);
        AssertReturnStmt(   m_RenderParms.Info.uWidth  == pDesc->cWidth
                         && m_RenderParms.Info.uHeight == pDesc->cHeight
                         && pDesc->cbOutputBuffer
                         && RT_VALID_PTR(pDesc->pvOutputBuffer), RTCritSectLeave(&m_RenderCritSect), VERR_INVALID_PARAMETER);

        vrc = RecordingRenderInitEx(&m_Renderer, RECORDINGRENDERBACKEND_OUTTGT, pDesc);
        if (RT_SUCCESS(vrc))
            m_Video.OutputTargetDesc = *pDesc;
    }
    else
    {
        LogRel2(("Recording: Revoking output target description for screen #%RU32\n", m_uScreenID));
        RT_ZERO(m_Video.OutputTargetDesc);
    }

    if (   !pDesc
        || RT_FAILURE(vrc))
        vrc = RecordingRenderInit(&m_Renderer, RECORDINGRENDERBACKEND_AUTO);

    if (RT_SUCCESS(vrc))
    {
        vrc = RecordingRenderSetParms(&m_Renderer, &m_RenderParms);
        if (RT_SUCCESS(vrc))
            vrc = RecordingRenderScreenChange(&m_Renderer, &m_Video.ScreenInfo);
    }

    if (RT_FAILURE(vrc))
        LogRel(("Recording: Stream #%u renderer initialization failed with %Rrc\n", m_uScreenID, vrc));

    vrc2 = RTCritSectLeave(&m_RenderCritSect);
    AssertRC(vrc2);

    return VINF_SUCCESS;
}

/**
 * Returns the current video output target description.
 *
 * @return  Current video output target description.
 */
PDMDISPLAYOUTPUTTARGETDESC const * RecordingStream::GetVideoOutputTargetDesc(void)
{
    return &m_Video.OutputTargetDesc;
}

/**
 * Checks if a specified limit for a recording stream has been reached, internal version.
 *
 * @returns @c true if any limit has been reached, @c false if not.
 * @param   msTimestamp         Timestamp (PTS, in ms) to check for.
 */
inline bool RecordingStream::isLimitReachedInternal(uint64_t msTimestamp) const
{
    LogFlowThisFunc(("msTimestamp=%RU64, ulMaxTimeS=%RU32, tsStartMs=%RU64\n",
                     msTimestamp, m_SettingsCache.uMaxTime, m_tsStartMs));

    if (   m_SettingsCache.uMaxTime
        && msTimestamp >= m_SettingsCache.uMaxTime * RT_MS_1SEC)
    {
        LogRel(("Recording: Time limit for stream #%RU16 has been reached (%RU32s)\n",
                m_uScreenID, m_SettingsCache.uMaxTime));
        return true;
    }

    if (m_SettingsCache.enmDestination == RecordingDestination_File)
    {
        if (m_SettingsCache.uMaxFileSize)
        {
            uint64_t const sizeInMB = this->File.m_pWEBM->GetFileSize() / _1M;
            if (sizeInMB >= m_SettingsCache.uMaxFileSize)
            {
                LogRel(("Recording: File size limit for stream #%RU16 has been reached (%RU64MB)\n",
                        m_uScreenID, m_SettingsCache.uMaxFileSize));
                return true;
            }
        }

        /* Check for available free disk space */
        if (   this->File.m_pWEBM
            && this->File.m_pWEBM->GetAvailableSpace() < 0x100000) /** @todo r=andy WTF? Fix this. */
        {
            LogRel(("Recording: Not enough free storage space available, stopping recording\n"));
            return true;
        }
    }

    return false;
}

/**
 * Returns whether video recording for this stream is enabled or not.
 *
 * @returns @c true if video recording is enabled, or @c false if not.
 */
inline bool RecordingStream::isVideoEnabled(void) const
{
    return m_SettingsCache.mapFeatures.find(RecordingFeature_Video) != m_SettingsCache.mapFeatures.end();
}

/**
 * Returns whether audio recording for this stream is enabled or not.
 *
 * @returns @c true if audio recording is enabled, or @c false if not.
 */
inline bool RecordingStream::isAudioEnabled(void) const
{
    return m_SettingsCache.mapFeatures.find(RecordingFeature_Audio) != m_SettingsCache.mapFeatures.end();
}

/**
 * Internal iteration main loop.
 * Does housekeeping and recording context notification.
 *
 * @returns VBox status code.
 * @retval  VINF_RECORDING_LIMIT_REACHED if the stream's recording limit has been reached.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 *
 * @note    Caller must *not* have the stream's lock (callbacks involved).
 */
int RecordingStream::iterateInternal(uint64_t msTimestamp)
{
#ifdef DEBUG
    AssertReturn(!RTCritSectIsOwner(&m_CritSect), VERR_WRONG_ORDER);
    lock();
    AssertReturnStmt(   m_enmState == RECORDINGSTREAMSTATE_STARTED
                     || m_enmState == RECORDINGSTREAMSTATE_STOPPING, unlock(), VINF_RECORDING_LIMIT_REACHED);
    unlock();
#endif

    if (   m_enmState != RECORDINGSTREAMSTATE_STARTED
        && m_enmState != RECORDINGSTREAMSTATE_STOPPING)
        return VINF_RECORDING_LIMIT_REACHED;

    int vrc;

    if (isLimitReachedInternal(msTimestamp))
    {
        vrc = VINF_RECORDING_LIMIT_REACHED;
    }
    else
        vrc = VINF_SUCCESS;

    AssertPtr(m_pCtx);

    switch (vrc)
    {
        case VINF_RECORDING_LIMIT_REACHED:
        {
            m_enmState = RECORDINGSTREAMSTATE_STOPPED;

            int vrc2 = m_pCtx->OnLimitReached(m_uScreenID, VINF_SUCCESS /* vrc */);
            AssertRC(vrc2);
            break;
        }

        default:
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Checks if a specified limit for a recording stream has been reached.
 *
 * @returns @c true if any limit has been reached, @c false if not.
 * @param   msTimestamp         Timestamp (PTS, in ms) to check for.
 */
bool RecordingStream::IsLimitReached(uint64_t msTimestamp) const
{
    if (m_enmState != RECORDINGSTREAMSTATE_STARTED)
        return true;

    return isLimitReachedInternal(msTimestamp);
}

/**
 * Returns whether a feature for a recording stream is enabled or not.
 *
 * @returns @c true if enabled, @c false if not.
 * @param   enmFeature          Feature of stream to check enabled status for.
 */
bool RecordingStream::IsFeatureEnabled(RecordingFeature_T enmFeature) const
{
    RecordingFeatureMap::const_iterator itFeat = m_SettingsCache.mapFeatures.find(enmFeature);
    if (itFeat != m_SettingsCache.mapFeatures.end())
        return RT_BOOL(itFeat->second);

    return false;
}

/**
 * Returns if a recording stream needs to be fed with an update or not.
 *
 * @returns @c true if an update is needed, @c false if not.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 */
bool RecordingStream::NeedsUpdate(uint64_t msTimestamp) const
{
    return    RecordingCodecGetWritable((const PRECORDINGCODEC)&m_CodecVideo, msTimestamp) > 0
           && !shouldDropFrame(RECORDINGFRAME_TYPE_VIDEO);
}

/**
 * Returns command queue pressure in percent.
 */
inline uint8_t RecordingStream::cmdQueueGetPressure(void) const
{
    if (!m_pCmdCircBuf)
        return 0;

    size_t const cbCmdSize = RTCircBufSize(m_pCmdCircBuf);
    if (!cbCmdSize)
        return 0;

    size_t const cbCmdUsed = RTCircBufUsed(m_pCmdCircBuf);
    return (uint8_t)RT_MIN((cbCmdUsed * 100) / cbCmdSize, (size_t)100);
}

/**
 * Returns whether a frame should be dropped due to queue / pool pressure.
 */
inline bool RecordingStream::shouldDropFrame(RECORDINGFRAME_TYPE enmType) const
{
    uint8_t const uCmdPressure  = cmdQueueGetPressure();
    uint8_t const uPoolPressure = RecordingFramePoolGetPressure(&m_aFramePool[enmType], 0 /* idRdr */);

    switch (enmType)
    {
        case RECORDINGFRAME_TYPE_CURSOR_POS:
            return uCmdPressure >= 70 || uPoolPressure >= 85;

        case RECORDINGFRAME_TYPE_CURSOR_SHAPE:
            return uCmdPressure >= 80 || uPoolPressure >= 90;

        case RECORDINGFRAME_TYPE_VIDEO:
            return uCmdPressure >= 90 || uPoolPressure >= 90;

        case RECORDINGFRAME_TYPE_SCREEN_CHANGE:
            return false; /* Always keep structural frames  needed for recording. */

        default:
            break;
    }

    return false;
}

/**
 * Processes a recording stream.
 *
 * This function takes care of the actual encoding and writing of a certain stream.
 * As this can be very CPU intensive, this function usually is called from a separate thread.
 *
 * @returns VBox status code.
 * @param   msTimeout           Timeout for processing.
 *                              If set to 0, run in non-timed drain mode.
 *
 * @note    Runs in stream thread.
 */
int RecordingStream::process(RTMSINTERVAL msTimeout)
{
    LogFlowFuncEnter();

    lock();

    if (!m_SettingsCache.fEnabled)
    {
        unlock();
        return VINF_SUCCESS;
    }

    unlock();

    int vrc = VINF_SUCCESS;

    uint64_t const tsStartMs    = RTTimeMilliTS();
    bool     const fTimedBudget = msTimeout > 0;

    size_t   cPktsProcessed         = 0;
    size_t   cPktsDroppedForBacklog = 0;
    size_t   cCmdProcessed          = 0;

    STAM_PROFILE_START(&m_STAM.profileFnProcessTotal, total);

    int vrc2 = RTCritSectEnter(&m_RenderCritSect);
    AssertRC(vrc2);

    if (IsFeatureEnabled(RecordingFeature_Video))
    {
        STAM_PROFILE_START(&m_STAM.profileFnProcessVideo, video);

        for (;;)
        {
            /* Get next command. */
            RECORDINGCMD *pCmd;
            size_t        cbCmd;
            RTCircBufAcquireReadBlock(m_pCmdCircBuf, sizeof(RECORDINGCMD), (void **)&pCmd, &cbCmd);
            if (!cbCmd)
                break;
            Assert(cbCmd >= sizeof(RECORDINGCMD));

            RECORDINGCMD Cmd = *pCmd;

            Log3Func(("Cmd: ts=%RU64, idxPool=%u\n", Cmd.msTimestamp, Cmd.idxPool));

            RTCircBufReleaseReadBlock(m_pCmdCircBuf, sizeof(RECORDINGCMD));

            /* Resolve packet for this timestamp (if any). */
            PRECORDINGPKT pPkt = NULL;
            std::map<uint64_t, RECORDINGPKT>::iterator itPkt = m_mapPkts.find(Cmd.msTimestamp);
            if (itPkt != m_mapPkts.end())
            {
                pPkt = &itPkt->second;
                if (pPkt->cSeen == 0) /* New packet? */
                {
                    RecordingRenderComposeBegin(&m_Renderer);

                    Assert(pPkt->msTimestamp == Cmd.msTimestamp);
                    pPkt->tsFirstSeenMs = RTTimeMilliTS();
                }

                Log3Func(("Packet %RU64: ts=%RU64, seen=%zu/%zu\n",
                          pPkt->idPkt, pPkt->msTimestamp, pPkt->cSeen, pPkt->cExpected));
            }

            /* Get the next frame to process. */
            Assert(Cmd.idxPool && Cmd.idxPool < RECORDINGFRAME_TYPE_MAX);
            PRECORDINGFRAMEPOOL pPool  = &m_aFramePool[Cmd.idxPool];
            PRECORDINGFRAME     pFrame = RecordingFramePoolAcquireRead(&m_aFramePool[Cmd.idxPool], m_uScreenID);
            AssertPtr(pFrame);

            Log3Func(("Frame %s: ts=%RU64\n",
                      RecordingUtilsFrameTypeToStr(pFrame->enmType), pFrame->msTimestamp));

            PRECORDINGCODEC pCodec = &m_CodecVideo; /* Only codec used right now. */

            switch (pFrame->enmType)
            {
                case RECORDINGFRAME_TYPE_CURSOR_POS:
                case RECORDINGFRAME_TYPE_CURSOR_SHAPE:
                case RECORDINGFRAME_TYPE_VIDEO:
                {
                    if (!pPkt) /* No packet but one frame only? Start a new compose cycley. */
                        RecordingRenderComposeBegin(&m_Renderer);

                    /* Cursor frames must first be composed into the current image and then encoded. */
                    vrc2 = RecordingRenderComposeAddFrame(&m_Renderer, pFrame);
                    AssertRC(vrc2);
                    if (RT_SUCCESS(vrc))
                        vrc = vrc2;

                    if (!pPkt) /* No packet but one frame only? Start a new compose cycley. */
                        RecordingRenderComposeEnd(&m_Renderer);
                    break;
                }

                case RECORDINGFRAME_TYPE_SCREEN_CHANGE:
                {
                    vrc2 = RecordingRenderScreenChange(&m_Renderer,
                                                       &pFrame->u.ScreenInfo);
                    if (RT_SUCCESS(vrc2))
                        vrc2 = RecordingCodecScreenChange(pCodec, &pFrame->u.ScreenInfo);
                    if (RT_SUCCESS(vrc))
                        vrc = vrc2;
                    break;
                }

                default:
                    AssertFailed();
                    break;
            }

            RecordingFramePoolReleaseRead(pPool, m_uScreenID);

            cCmdProcessed++;

            /* Command queue pressure too high? Drop the entire packet. */
            bool          fCmdPressureTooHigh = false;
            uint8_t const uCmdPress           = cmdQueueGetPressure();
            if (uCmdPress >= 80)
            {
                uint8_t const uPoolPress = RecordingFramePoolGetPressure(&m_aFramePool[RECORDINGFRAME_TYPE_VIDEO],
                                                                         m_uScreenID);
                if (   uCmdPress  >= 90  /** @todo Make this configurable? */
                    || uPoolPress >= 90)
                {
                    /* Keep every 4th packet to preserve temporal progress while dropping stale backlog. */
                    cPktsDroppedForBacklog++;
                    fCmdPressureTooHigh = (cPktsDroppedForBacklog % 4) != 0;
                }
            }

            bool fEncode = false;

            if (pPkt)
            {
                pPkt->cSeen++;

                Assert(pPkt->cSeen <= pPkt->cExpected);
                bool const fPktComplete = pPkt->cSeen >= pPkt->cExpected;
                bool const fPktTimedOut = RTTimeMilliTS() - pPkt->tsFirstSeenMs > m_pCtx->GetSchedulingHintMs();
                bool const fPktDrop     =    fPktTimedOut
                                          || fPktComplete
                                          || fCmdPressureTooHigh;

                if (RT_LIKELY(fPktComplete))
                {
                    RecordingRenderComposeEnd(&m_Renderer);
                }
                else if (   fPktTimedOut
                         || fCmdPressureTooHigh)
                {
                    RecordingRenderComposeDrop(&m_Renderer);

                    STAM_COUNTER_INC(&m_STAM.cVideoFramesSkipped);

                    if (fPktTimedOut)
                        LogRelMax2(64,("Recording: Warning: Stream #%u dropping packet %RU64 because of timeout\n",
                                       m_uScreenID, pPkt->idPkt));
                    else if (fCmdPressureTooHigh)
                        LogRelMax2(64, ("Recording: Warning: Stream #%u dropping packet %RU64 under backlog pressure\n",
                                        m_uScreenID, pPkt->idPkt));
                }

                fEncode = fPktComplete;

                Log3Func(("Packet %RU64: %zu/%zu -> complete=%RTbool, timed out=%RTbool -> fEncode = %RTbool\n",
                          pPkt->idPkt, pPkt->cSeen, pPkt->cExpected, fPktComplete, fPktTimedOut, fEncode));

                if (fPktDrop)
                {
                    itPkt = m_mapPkts.erase(itPkt);
                    cPktsProcessed++; /* Count for fairness guard. */
                }
            }
            else if (pFrame->enmType != RECORDINGFRAME_TYPE_SCREEN_CHANGE)
                fEncode = true;

            if (fEncode)
            {
                vrc2 = RecordingRenderPerform(&m_Renderer);
                AssertRC(vrc2);
                if (RT_SUCCESS(vrc))
                    vrc = vrc2;

                RECORDINGFRAME FrameRendered;
                RT_ZERO(FrameRendered);
                FrameRendered.idStream    = m_uScreenID;
                FrameRendered.enmType     = RECORDINGFRAME_TYPE_VIDEO;
                FrameRendered.msTimestamp = pFrame->msTimestamp;

                vrc2 = RecordingRenderQueryFrame(&m_Renderer, &FrameRendered.u.Video);
                AssertRC(vrc2);
                if (RT_SUCCESS(vrc))
                    vrc = vrc2;

                vrc2 = RecordingCodecEncode(pCodec, &FrameRendered, FrameRendered.msTimestamp, m_pCtx /* pvUser */);
                AssertRC(vrc2);
                if (RT_SUCCESS(vrc))
                    vrc = vrc2;

                RecordingFrameDestroy(&FrameRendered);
            }

            if (   (fTimedBudget && RTTimeMilliTS() - tsStartMs >= msTimeout) /* Timeout reached? */
                || (!fTimedBudget && cPktsProcessed >= 512 /* Fairness guard for non-timed drain mode. */))
                break;

        } /* for */

        STAM_PROFILE_STOP(&m_STAM.profileFnProcessVideo, video);
    }

    vrc2 = RTCritSectLeave(&m_RenderCritSect);
    AssertRC(vrc2);

#ifdef VBOX_WITH_AUDIO_RECORDING
    STAM_PROFILE_START(&m_STAM.profileFnProcessAudio, audio);

    /* Do we need to multiplex the common audio data to this stream? */
    if (   IsFeatureEnabled(RecordingFeature_Audio)
        && (!fTimedBudget || RTTimeMilliTS() - tsStartMs < msTimeout))
    {
        PRECORDINGFRAMEPOOL pPool = &m_paPoolsCommonEnc[RECORDINGFRAME_TYPE_AUDIO];
        size_t cAudioFramesProcessed = 0;
        for (;;)
        {
            PRECORDINGFRAME pFrame = RecordingFramePoolAcquireRead(pPool, m_uScreenID);
            if (!pFrame)
                break;

            Assert(pFrame->enmType == RECORDINGFRAME_TYPE_AUDIO);
            PRECORDINGAUDIOFRAME pAudioFrame = &pFrame->u.Audio;

            vrc2 = this->File.m_pWEBM->WriteBlock(m_uTrackAudio, pAudioFrame->pvBuf, pAudioFrame->cbBuf,
                                                  pFrame->msTimestamp,
                                                    pFrame->Enc.uFlags == RECORDINGCODEC_ENC_F_BLOCK_IS_KEY
                                                  ? VBOX_WEBM_BLOCK_FLAG_KEY_FRAME : VBOX_WEBM_BLOCK_FLAG_NONE);
            if (RT_SUCCESS(vrc))
                vrc = vrc2;

            Log3Func(("RECORDINGFRAME_TYPE_AUDIO: %zu bytes -> %Rrc\n", pAudioFrame->cbBuf, vrc2));

            RecordingFramePoolReleaseRead(pPool, m_uScreenID);

            cAudioFramesProcessed++;
            if (   (fTimedBudget && RTTimeMilliTS() - tsStartMs > msTimeout) /* Timeout reached? */
                || (!fTimedBudget && cAudioFramesProcessed >= 256))
                break;
        }
    }

    STAM_PROFILE_STOP(&m_STAM.profileFnProcessAudio, audio);
#endif /* VBOX_WITH_AUDIO_RECORDING */

    STAM_PROFILE_STOP(&m_STAM.profileFnProcessTotal, total);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * The stream's thread main routine.
 *
 * @returns VBox status code.
 * @retval  VINF_RECORDING_LIMIT_REACHED if the stream's recording limit has been reached.
 * @param   rcWait              Result of the encoding thread's wait operation.
 *                              Can be used for figuring out if the encoder has to perform some
 *                              worked based on that result.
 * @param   msTimeout           Timeout (in ms) for processing.
 *
 */
int RecordingStream::ThreadWorker(int rcWait, RTMSINTERVAL msTimeout)
{
    Log3Func(("uScreenID=%RU16, msTimeout=%RU32, rcWait=%Rrc\n", m_uScreenID, msTimeout, rcWait));

    /* Idle timeout: don't re-encode the last composed video frame.
     *
     * We intentionally keep video recording sparse / variable-frame-rate and only encode
     * on actual dirty updates, which avoids burning CPU on duplicate frames.
     *
     * However, a zero-time wait can also timeout while queues still contain work, so keep
     * draining pending stream/common-audio queues in that case. */
    if (rcWait == VERR_TIMEOUT)
    {
        bool fHavePendingWork = RTCircBufUsed(m_pCmdCircBuf) > 0;
#ifdef VBOX_WITH_AUDIO_RECORDING
        if (   !fHavePendingWork
            && IsFeatureEnabled(RecordingFeature_Audio)
            && m_paPoolsCommonEnc)
            fHavePendingWork = RecordingFramePoolReadable(&m_paPoolsCommonEnc[RECORDINGFRAME_TYPE_AUDIO],
                                                          m_uScreenID) > 0;
#endif
        if (!fHavePendingWork)
            return VINF_SUCCESS;
    }

    return process(msTimeout);
}

/**
 * Worker thread for one recording stream.
 */
/* static */
DECLCALLBACK(int) RecordingStream::Thread(RTTHREAD hThreadSelf, void *pvUser)
{
    RecordingStream *pThis = (RecordingStream *)pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    RTThreadUserSignal(hThreadSelf);

    bool fDrainMode = false;

    for (;;)
    {
        RTMSINTERVAL msTimeout = pThis->m_pCtx->GetSchedulingHintMs();

        uint8_t const uCmdPressure       = pThis->cmdQueueGetPressure();
        uint8_t const uVideoPoolPressure = RecordingFramePoolGetPressure(&pThis->m_aFramePool[RECORDINGFRAME_TYPE_VIDEO],
                                                                         pThis->m_uScreenID);

        /* Backlog hysteresis (enter high, leave low) to avoid mode thrashing. */
        if (fDrainMode)
        {
            if (uCmdPressure <= 40 && uVideoPoolPressure <= 40)
                fDrainMode = false;
        }
        else if (uCmdPressure >= 80 || uVideoPoolPressure >= 80)
            fDrainMode = true;

        size_t const cbCmdQueueUsed = RTCircBufUsed(pThis->m_pCmdCircBuf);
        bool   const fHaveVideoWork = cbCmdQueueUsed > 0;
        if (fHaveVideoWork || fDrainMode)
            msTimeout = 0;

#ifdef VBOX_WITH_AUDIO_RECORDING
        size_t cPoolAudioUsed = 0;
        if (pThis->IsFeatureEnabled(RecordingFeature_Audio))
        {
            cPoolAudioUsed = RecordingFramePoolReadable(&pThis->m_paPoolsCommonEnc[RECORDINGFRAME_TYPE_AUDIO],
                                                        pThis->m_uScreenID);
            if (cPoolAudioUsed)
                msTimeout = 0;
        }
#endif
        int vrcWait = VINF_SUCCESS;
        if (msTimeout)
            vrcWait = RTSemEventWait(pThis->m_WaitEvent, msTimeout);

        if (ASMAtomicReadBool(&pThis->m_fShutdown))
            break;

        int const vrc = pThis->ThreadWorker(vrcWait, msTimeout);
        if (RT_FAILURE(vrc))
            LogRel(("Recording: Stream thread #%RU16 failed (%Rrc)\n", pThis->m_uScreenID, vrc));

        /* Keep going. */
    }

    return VINF_SUCCESS;
}

/**
 * Notifies a recording stream worker thread.
 *
 * @returns VBox status code.
 */
int RecordingStream::Notify(void)
{
    return RTSemEventSignal(m_WaitEvent);
}

/**
 * Locks the command queue.
 */
inline void RecordingStream::cmdQueueLock(void)
{
    int vrc2 = RTCritSectEnter(&m_CmdCritSect);
    AssertRC(vrc2); RT_NOREF(vrc2);
}

/**
 * Unlocks the command queue.
 */
inline void RecordingStream::cmdQueueUnlock(void)
{
    int vrc2 = RTCritSectLeave(&m_CmdCritSect);
    AssertRC(vrc2); RT_NOREF(vrc2);
}

/**
 * Adds a recording frame to the command queue.
 *
 * @returns VBox status code.
 * @retval  VERR_RECORDING_THROTTLED if the command buffer is full.
 * @param   pFrame              Recording frame to add.
 *                              Does not change ownership (caller, stored in frame pool).
 * @param   pPool               Pool to containing the recording frame.
 * @param   pCmd                Command to enqueue.
 **/
int RecordingStream::cmdQueueAddFrame(PRECORDINGFRAME pFrame, PRECORDINGFRAMEPOOL pPool, RECORDINGCMD *pCmd)
{
    RT_NOREF(pPool);

    LogFlowFunc(("type=%s, ts=%RU64\n",
                 RecordingUtilsFrameTypeToStr(pFrame->enmType), pFrame->msTimestamp));

    /* Sanity. */
    Assert(pCmd->idxPool != RECORDINGFRAME_TYPE_INVALID);
    Assert(pFrame->msTimestamp == pCmd->msTimestamp);

    RECORDINGCMD *pCmdWr;
    size_t        cbCmdWr;
    RTCircBufAcquireWriteBlock(m_pCmdCircBuf, sizeof(RECORDINGCMD), (void **)&pCmdWr, &cbCmdWr);
    if (!cbCmdWr)
    {
        LogRelMax(64, ("Recording: Warning: Stream #%u command pool is full, dropping %s\n",
                       m_uScreenID, RecordingUtilsFrameTypeToStr(pFrame->enmType)));
        return VERR_RECORDING_THROTTLED;
    }
    Assert(cbCmdWr >= sizeof(RECORDINGCMD));
    AssertPtr(pCmdWr);
    Assert(pPool->uId);

    *pCmdWr = *pCmd;

    RTCircBufReleaseWriteBlock(m_pCmdCircBuf, sizeof(RECORDINGCMD));

    return VINF_SUCCESS;
}

/**
 * Adds a recording frame to the command queue.
 *
 * @returns VBox status code.
 * @retval  VERR_RECORDING_THROTTLED if the command buffer is full.
 * @param   pFrame              Recording frame to add.
 *                              Does not change ownership (caller, stored in frame pool).
 * @param   pPool               Pool to containing the recording frame.
 **/
int RecordingStream::cmdQueueAddFrame(PRECORDINGFRAME pFrame, PRECORDINGFRAMEPOOL pPool)
{
    RECORDINGCMD Cmd;
    RT_ZERO(Cmd);

    Cmd.idxPool     = pPool->uId;
    Cmd.msTimestamp = pFrame->msTimestamp;

    return cmdQueueAddFrame(pFrame, pPool, &Cmd);
}

/**
 * Sends a raw (e.g. not yet encoded) audio frame to the recording stream.
 *
 * @returns VBox status code.
 * @param   pvData              Pointer to audio data.
 * @param   cbData              Size (in bytes) of \a pvData.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 */
int RecordingStream::SendAudioFrame(const void *pvData, size_t cbData, uint64_t msTimestamp)
{
#ifdef DEBUG
    lock();

    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturnStmt(m_enmState == RECORDINGSTREAMSTATE_STARTED, unlock(), VERR_WRONG_ORDER);

    unlock();
#endif

    if (!isAudioEnabled())
        return VINF_SUCCESS;

#ifdef VBOX_WITH_AUDIO_RECORDING
    STAM_COUNTER_INC(&m_STAM.cAudioFramesAdded);
#endif

    /* As audio data is common across all streams, re-route this to the recording context, where
     * the data is being encoded and stored in the common frame pool. */
    return m_pCtx->SendAudioFrame(pvData, cbData, msTimestamp);
}

/**
 * Sends a cursor position change to the recording stream.
 *
 * @returns VBox status code.
 * @retval  VWRN_RECORDING_ENCODING_SKIPPED if the encoding was skipped.
 * @retval  VERR_RECORDING_THROTTLED if the frame is too early for the current FPS setting.
 * @param   idCursor            Cursor ID. Currently unused and always set to 0.
 * @param   pPos                Cursor information to send.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 *
 * @thread  EMT
 */
int RecordingStream::SendCursorPos(uint8_t idCursor, PRECORDINGPOS pPos, uint64_t msTimestamp)
{
    RT_NOREF(idCursor);

    if (!isVideoEnabled())
        return VINF_SUCCESS;

    int vrc = iterateInternal(msTimestamp);
    if (vrc != VINF_SUCCESS) /* Can return VINF_RECORDING_LIMIT_REACHED. */
        return vrc;

    /* Ignore non-screen-change frames until we got the first screen change frame. */
    if (!m_tsStartMs)
        return VWRN_RECORDING_ENCODING_SKIPPED;

    if (shouldDropFrame(RECORDINGFRAME_TYPE_CURSOR_POS))
        return VERR_RECORDING_THROTTLED;

    PRECORDINGFRAMEPOOL pPool = &this->m_aFramePool[RECORDINGFRAME_TYPE_CURSOR_POS];

    PRECORDINGFRAME pFrame = RecordingFramePoolAcquireWrite(pPool);
    if (!pFrame)
        return VERR_RECORDING_THROTTLED;

    pFrame->enmType     = RECORDINGFRAME_TYPE_CURSOR_POS;
    pFrame->msTimestamp = msTimestamp;
    pFrame->idStream    = this->m_uScreenID;

    pFrame->u.Cursor.Pos = *pPos;

    RecordingFramePoolReleaseWrite(pPool);

    cmdQueueLock();

    vrc = cmdQueueAddFrame(pFrame, pPool);

    cmdQueueUnlock();
    return vrc;
}

/**
 * Sends a cursor shape change to the recording stream.
 *
 * @returns VBox status code.
 * @retval  VERR_RECORDING_THROTTLED if the frame is too early for the current FPS setting.
 * @param   idCursor            Cursor ID. Currently unused and always set to 0.
 * @param   pShape              Cursor shape to send.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 *
 * @note    Keep it as simple as possible, as this function might run on EMT.
 * @thread  EMT
 */
int RecordingStream::SendCursorShape(uint8_t idCursor, PRECORDINGVIDEOFRAME pShape, uint64_t msTimestamp)
{
    RT_NOREF(idCursor);

    if (!isVideoEnabled())
        return VINF_SUCCESS;

    int vrc = iterateInternal(msTimestamp);
    if (vrc != VINF_SUCCESS) /* Can return VINF_RECORDING_LIMIT_REACHED. */
        return vrc;

    if (shouldDropFrame(RECORDINGFRAME_TYPE_CURSOR_SHAPE))
        return VERR_RECORDING_THROTTLED;

    PRECORDINGFRAMEPOOL pPool = &this->m_aFramePool[RECORDINGFRAME_TYPE_CURSOR_SHAPE];

    PRECORDINGFRAME pFrame = RecordingFramePoolAcquireWrite(pPool);
    if (!pFrame)
        return VERR_RECORDING_THROTTLED;

    pFrame->enmType     = RECORDINGFRAME_TYPE_CURSOR_SHAPE;
    pFrame->msTimestamp = msTimestamp;
    pFrame->idStream    = this->m_uScreenID;

    Assert(pShape->cbBuf <= pPool->cbFrame - sizeof(RECORDINGFRAME));

    pFrame->u.Video = *pShape;
    pFrame->u.Video.pau8Buf = &pFrame->abData[0];
    memcpy(pFrame->u.Video.pau8Buf, pShape->pau8Buf, pShape->cbBuf); /* Make a deep copy of the pixel data. */
    pFrame->u.Video.cbBuf   = pShape->cbBuf;

    RecordingFramePoolReleaseWrite(pPool);

    cmdQueueLock();

    vrc = cmdQueueAddFrame(pFrame, pPool);

    cmdQueueUnlock();
    return vrc;
}

/**
 * Looks up (or creates) a deferred packet bucket for a timestamp.
 *
 * @returns Reference to packet entry for @a msTimestamp.
 * @param   msTimestamp         Packet timestamp (PTS, in ms).
 * @param   cFramesExpected     Expected number of frames for this packet.
 */
inline RecordingStream::RECORDINGPKT &RecordingStream::packetLookupOrCreate(uint64_t msTimestamp, size_t cFramesExpected)
{
    Assert(cFramesExpected > 1); /* Packets only make sense for >= 2 frames. */

    std::map<uint64_t, RECORDINGPKT>::iterator itPkt = m_mapPkts.find(msTimestamp);
    if (itPkt == m_mapPkts.end())
    {
        RECORDINGPKT Pkt;
        Pkt.idPkt       = ASMAtomicIncU64(&m_uPacketNext);
        Pkt.msTimestamp = msTimestamp;
        Pkt.cSeen       = 0;
        Pkt.cExpected   = cFramesExpected;
        m_mapPkts[msTimestamp] = Pkt;
        return m_mapPkts[msTimestamp];
    }

    return itPkt->second;
}

/**
 * Enqueues a raw (e.g. not yet encoded) video frame.
 *
 * @returns VBox status code.
 * @retval  VINF_RECORDING_LIMIT_REACHED if the stream's recording limit has been reached.
 * @retval  VERR_RECORDING_THROTTLED if the frame is too early for the current FPS setting.
 * @param   pVideoFrame         Video frame to send.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 *
 * @note    Keep it as simple as possible, as this function might run on EMT.
 * @thread  EMT or VMSVGA FIFO
 */
int RecordingStream::videoEnqueueFrame(PRECORDINGVIDEOFRAME pVideoFrame, uint64_t msTimestamp)
{
    LogFlowFuncEnter();

    PRECORDINGFRAMEPOOL pPool = &this->m_aFramePool[RECORDINGFRAME_TYPE_VIDEO];

#define VIDEO_LOG_SKIP_FRAME_RET(a_Reason) \
{ \
    STAM_COUNTER_INC(&m_STAM.cVideoFramesSkipped); \
    LogRelMax2(256, ("Recording: Stream #%u skipping video frame: %s\n", \
                     m_uScreenID, #a_Reason)); \
    LogFlowFuncLeaveRC(VERR_RECORDING_THROTTLED); \
    return VERR_RECORDING_THROTTLED; \
}

    if (shouldDropFrame(RECORDINGFRAME_TYPE_VIDEO))
        VIDEO_LOG_SKIP_FRAME_RET("Dropping while enqueuing");

    uint32_t const cbSrcBytesPerPixel = pVideoFrame->Info.uBPP / 8;
    uint32_t const cbSrcStride        = pVideoFrame->Info.uBytesPerLine;

    size_t   const cbDstMax = pPool->cbFrame - sizeof(RECORDINGFRAME);

    uint32_t uTileWidth  = RT_MIN(pVideoFrame->Info.uWidth,  VBOX_RECORDING_VIDEO_POOL_TILE_WIDTH);
    uint32_t uTileHeight = RT_MIN(pVideoFrame->Info.uHeight, VBOX_RECORDING_VIDEO_POOL_TILE_HEIGHT);
    while (   uTileWidth
           && uTileHeight
           && (uint64_t)uTileWidth * uTileHeight * cbSrcBytesPerPixel > cbDstMax)
    {
        if (uTileHeight > 1)
            uTileHeight = RT_MAX(uTileHeight / 2, 1 /* To prevent division by zero  below */);
        else if (uTileWidth > 1)
            uTileWidth = RT_MAX(uTileWidth / 2, 1);
        else
            break;
    }

    uint32_t const cTilesX = (pVideoFrame->Info.uWidth  + uTileWidth  - 1) / uTileWidth;  /* Round up. */
    uint32_t const cTilesY = (pVideoFrame->Info.uHeight + uTileHeight - 1) / uTileHeight; /* Ditto. */
    size_t   const cTiles  = cTilesX * cTilesY;

#ifdef VBOX_STRICT /* Sanity (skip in release builds for speed reasons). */
    AssertReturn(pVideoFrame->Info.uBPP % 8 == 0, VERR_INVALID_PARAMETER);
    AssertReturn(pVideoFrame->Info.uWidth, VERR_INVALID_PARAMETER);
    AssertReturn(pVideoFrame->Info.uHeight, VERR_INVALID_PARAMETER);
    AssertReturn(uTileWidth && uTileHeight, VERR_BUFFER_OVERFLOW);
    AssertReturn((uint64_t)uTileWidth * uTileHeight * cbSrcBytesPerPixel <= cbDstMax, VERR_BUFFER_OVERFLOW);
    AssertReturn(cTiles, VERR_INVALID_PARAMETER);
#endif

    /* Tiled updates can explode queue work under heavy update storms.
     * If we're already under notable pressure, drop whole oversized updates early.
     * This prevents stream worker threads from running hot while preserving pacing. */
    if (cTiles > 1)
    {
        uint8_t const uCmdPressure       = cmdQueueGetPressure();
        uint8_t const uVideoPoolPressure = RecordingFramePoolGetPressure(pPool, m_uScreenID);

        if (uCmdPressure >= 80 || uVideoPoolPressure >= 80)
            VIDEO_LOG_SKIP_FRAME_RET("Tiled update blocked by high cmd/pool pressure");
    }

    /* Avoid partially queuing tiled frame updates. */
    size_t const cPoolFree = RecordingFramePoolFree(pPool);
    size_t const cCmdFree  = RTCircBufFree(m_pCmdCircBuf) / sizeof(RECORDINGCMD);
    if (   cPoolFree < cTiles
        || cCmdFree  < cTiles)
        VIDEO_LOG_SKIP_FRAME_RET("Insufficient pool / command capacity for tiled update");

    LogFlowFunc(("%u * %u tiles (%zu total)\n", cTilesX, cTilesY, cTiles));

    if (cTiles >= 2)
        packetLookupOrCreate(msTimestamp, cTiles);

    /* Init command. */
    RECORDINGCMD Cmd;
    RT_ZERO(Cmd);
    Cmd.idxPool     = RECORDINGFRAME_TYPE_VIDEO;
    Cmd.msTimestamp = msTimestamp; /* All tiles have to have the same timestamp. */

    uint16_t idxTile = 0;
    for (uint32_t idxY = 0; idxY < cTilesY; idxY++)
    {
        uint32_t const offCurY    = idxY * uTileHeight;
        uint32_t const uCurHeight = RT_MIN(uTileHeight, pVideoFrame->Info.uHeight - offCurY);

        for (uint32_t idxX = 0; idxX < cTilesX; idxX++)
        {
            uint32_t const offCurX      = idxX * uTileWidth;
            uint32_t const uCurWidth    = RT_MIN(uTileWidth, pVideoFrame->Info.uWidth - offCurX);
            size_t   const cbCurStride  = uCurWidth * cbSrcBytesPerPixel;
            size_t   const cbCur        = uCurWidth * uCurHeight * cbSrcBytesPerPixel;

            Assert(cbCur <= cbDstMax);

#ifdef VBOX_WITH_RECORDING_STATS
            RECORDINGVIDEOFRAME TileStats = *pVideoFrame;
            TileStats.cbBuf               = cbCur;
            statsVideoOnFrameQueued(&TileStats);

            if (    m_Video.Stats.PoolPressure.cSamples
                && (m_Video.Stats.PoolPressure.cSamples % 25 /* FPS */) == 0)
                statsVideoLogBuckets("Stats");
#endif /* VBOX_WITH_RECORDING_STATS */

            PRECORDINGFRAME pFrame = RecordingFramePoolAcquireWrite(pPool);
            if (!pFrame)
                VIDEO_LOG_SKIP_FRAME_RET("Video frame pool acquire failed");

            pFrame->enmType     = RECORDINGFRAME_TYPE_VIDEO;
            pFrame->msTimestamp = msTimestamp;
            pFrame->idStream    = this->m_uScreenID;

            pFrame->u.Video                    = *pVideoFrame;
            pFrame->u.Video.Info.uWidth        = uCurWidth;
            pFrame->u.Video.Info.uHeight       = uCurHeight;
            pFrame->u.Video.Info.uBytesPerLine = (uint32_t)cbCurStride;
            pFrame->u.Video.Pos.x              = pVideoFrame->Pos.x + offCurX;
            pFrame->u.Video.Pos.y              = pVideoFrame->Pos.y + offCurY;
            pFrame->u.Video.cbBuf              = cbCur;
            pFrame->u.Video.pau8Buf = (uint8_t *)((PRTUINTPTR)pFrame + RT_OFFSETOF(RECORDINGFRAME, abData));

            Log3Func(("  Tile %02u/%02u: offX=%u, offY=%u, w=%u, h=%u, %zu bytes\n",
                      idxX, idxY, offCurX, offCurY, uCurWidth, uCurHeight, cbCur));
            Log3Func(("              srcStride=%u, curStride=%u\n", cbSrcStride, cbCurStride));

            uint8_t *pu8Src = pVideoFrame->pau8Buf + (offCurY * cbSrcStride) + (offCurX * cbSrcBytesPerPixel);
            size_t   offSrc = 0;
            size_t   offDst = 0;

            for (uint32_t y = 0; y < uCurHeight; y++)
            {
                memcpy(pFrame->u.Video.pau8Buf + offDst, pu8Src + offSrc, cbCurStride);
                offSrc += cbSrcStride;
                offDst += cbCurStride;
            }

#ifdef VBOX_RECORDING_DEBUG_TILES
            char szTile[64];
            RTStrPrintf2(szTile, sizeof(szTile), "tile%u%u", idxX, idxY);
            RecordingDbgDumpVideoFrame(&pFrame->u.Video, szTile, msTimestamp);
            RecordingDbgAddVideoFrameBorder(&pFrame->u.Video);
#endif /* VBOX_RECORDING_DEBUG_TILES */
            RecordingFramePoolReleaseWrite(pPool);

            int const vrc = cmdQueueAddFrame(pFrame, pPool, &Cmd);
            if (RT_FAILURE(vrc))
                VIDEO_LOG_SKIP_FRAME_RET("Adding frame failed");

            STAM_COUNTER_INC(&m_STAM.cVideoFramesAdded);
            STAM_COUNTER_INC(&m_STAM.cVideoFramesToEncode);

            idxTile++;
        }
    }

#undef VIDEO_LOG_SKIP_FRAME_RET

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * Sends a raw (e.g. not yet encoded) video frame to the recording stream.
 *
 * @returns VBox status code.
 * @retval  VINF_RECORDING_LIMIT_REACHED if the stream's recording limit has been reached.
 * @retval  VERR_RECORDING_THROTTLED if the frame is too early for the current FPS setting.
 * @retval  VWRN_RECORDING_ENCODING_SKIPPED if the encoding was skipped.
 * @param   pVideoFrame         Video frame to send.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 *
 * @note    Keep it as simple as possible, as this function might run on EMT.
 * @thread  EMT or VMSVGA FIFO
 */
int RecordingStream::SendVideoFrame(PRECORDINGVIDEOFRAME pVideoFrame, uint64_t msTimestamp)
{
    AssertPtrReturn(pVideoFrame, VERR_INVALID_POINTER);
    AssertPtrReturn(pVideoFrame->pau8Buf, VERR_INVALID_POINTER);

    if (!isVideoEnabled())
        return VINF_SUCCESS;

    int vrc = iterateInternal(msTimestamp);
    if (vrc != VINF_SUCCESS) /* Can return VINF_RECORDING_LIMIT_REACHED. */
        return vrc;

    /* Ignore non-screen-change frames until we got the first screen change frame. */
    if (!m_tsStartMs)
        return VWRN_RECORDING_ENCODING_SKIPPED;

    cmdQueueLock();

    vrc = videoEnqueueFrame(pVideoFrame, msTimestamp);

    cmdQueueUnlock();

    return vrc;
}

/**
 * Sends a screen size change to a recording stream.
 *
 * @returns VBox status code.
 * @retval  VERR_RECORDING_THROTTLED if the frame is too early for the current FPS setting.
 * @param   pInfo               Recording screen info to use.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 * @param   fForce              Set to \c true to force a change, otherwise to \c false.
 *
 * @thread  EMT
 */
int RecordingStream::SendScreenChange(PRECORDINGSURFACEINFO pInfo, uint64_t msTimestamp, bool fForce /* = false */)
{
#ifdef DEBUG
    lock();
    AssertPtrReturn(pInfo, VERR_INVALID_POINTER);
    AssertReturn(m_enmState == RECORDINGSTREAMSTATE_STARTED, VERR_WRONG_ORDER);
    unlock();
#endif

    RT_NOREF(fForce);

    if (!isVideoEnabled())
        return VINF_SUCCESS;

    lock();

    /* Fend off screen change requests which match the current screen info we already have. */
    if (   m_Video.ScreenInfo.uWidth  == pInfo->uWidth
        && m_Video.ScreenInfo.uHeight == pInfo->uHeight
        && m_Video.ScreenInfo.uBPP    == pInfo->uBPP)
    {
        unlock();
        return VINF_SUCCESS;
    }

    m_Video.ScreenInfo = *pInfo;

    unlock();

    LogRel(("Recording: Screen size of stream #%RU32 changed to %RU32x%RU32 (%RU8 BPP)\n",
            m_uScreenID, m_Video.ScreenInfo.uWidth, m_Video.ScreenInfo.uHeight, m_Video.ScreenInfo.uBPP));

    PRECORDINGFRAMEPOOL pPool = &this->m_aFramePool[RECORDINGFRAME_TYPE_SCREEN_CHANGE];

    PRECORDINGFRAME pFrame = RecordingFramePoolAcquireWrite(pPool);
    if (!pFrame)
        return VERR_RECORDING_THROTTLED;

    pFrame->enmType     = RECORDINGFRAME_TYPE_SCREEN_CHANGE;
    pFrame->msTimestamp = msTimestamp;
    pFrame->idStream    = this->m_uScreenID;

    pFrame->u.ScreenInfo = m_Video.ScreenInfo;

    RecordingFramePoolReleaseWrite(pPool);

    /* First screen change marks the recording start point. */
    if (!m_tsStartMs)
        m_tsStartMs = RTTimeMilliTS();

    cmdQueueLock();

    int const vrc = cmdQueueAddFrame(pFrame, pPool);

    cmdQueueUnlock();
    return vrc;
}

/**
 * Starts an initialized recording stream.
 *
 * @returns VBox status code.
 *
 * @thread  EMT
 */
int RecordingStream::Start(void)
{
    lock();

    AssertReturnStmt(m_enmState == RECORDINGSTREAMSTATE_INITIALIZED, unlock(), VERR_WRONG_ORDER);

    ASMAtomicWriteBool(&m_fShutdown, false);

    char szThreadName[16];
    RTStrPrintf(szThreadName, sizeof(szThreadName), "RecS%02RU16", m_uScreenID);

    int vrc = RTThreadCreate(&m_Thread, RecordingStream::Thread, (void *)this, 0,
                             RTTHREADTYPE_MAIN_WORKER, RTTHREADFLAGS_WAITABLE, szThreadName);
    if (RT_SUCCESS(vrc))
        vrc = RTThreadUserWait(m_Thread, RT_MS_30SEC /* Timeout */);

    if (RT_SUCCESS(vrc))
    {
        LogRel(("Recording: Starting to record stream #%RU32\n", m_uScreenID));
        m_enmState = RECORDINGSTREAMSTATE_STARTED;
    }
    else
        LogRel(("Recording: Failed to start thread for stream #%RU32 (%Rrc)\n", m_uScreenID, vrc));

    unlock();

    return vrc;
}

/**
 * Stops an started or paused recording stream.
 *
 * @returns VBox status code.
 *
 * @thread  EMT
 */
int RecordingStream::Stop(void)
{
    RTTHREAD hThread = NIL_RTTHREAD;

    lock();

    AssertReturnStmt(   m_enmState == RECORDINGSTREAMSTATE_STARTED
                     || m_enmState == RECORDINGSTREAMSTATE_PAUSED, unlock(), VERR_WRONG_ORDER);

    int vrc = VINF_SUCCESS;

    LogRel(("Recording: Stopping to record stream #%RU32\n", m_uScreenID));
    m_enmState = RECORDINGSTREAMSTATE_STOPPING;
    hThread = m_Thread;

    ASMAtomicWriteBool(&m_fShutdown, true);

    unlock();

    if (hThread != NIL_RTTHREAD)
    {
        int vrc2 = Notify();
        AssertRC(vrc2);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;

        /* Timeout, can take a while to encode + write the remaining stuff. */
        vrc2 = RTThreadWait(hThread, RT_MS_5MIN, NULL);
        AssertRC(vrc2);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;
    }

    lock();
    m_Thread   = NIL_RTTHREAD;
    m_enmState = RECORDINGSTREAMSTATE_STOPPED;
    unlock();

    return vrc;
}

/**
 * Initializes a recording stream, internal version.
 *
 * @returns VBox status code.
 * @param   pCtx                        Pointer to recording context.
 * @param   uScreen                     Screen number to use for this recording stream.
 * @param   ScreenSettings              Recording screen settings to use for initialization.
 * @param   paPoolsCommonEnc            Pointer to commonly encoded frame pool.
 */
int RecordingStream::initCommon(RecordingContext *pCtx, uint32_t uScreen,
                                const ComPtr<IRecordingScreenSettings> &ScreenSettings,
                                PRECORDINGFRAMEPOOL paPoolsCommonEnc)
{
    AssertReturn(m_enmState == RECORDINGSTREAMSTATE_UNINITIALIZED, VERR_WRONG_ORDER);

    unconst(m_pConsole) = pCtx->GetConsole();

    m_pCtx             = pCtx;
    m_uTrackAudio      = UINT8_MAX;
    m_uTrackVideo      = UINT8_MAX;
    m_tsStartMs        = 0;
    m_uScreenID        = uScreen;
#ifndef VBOX_WITH_AUDIO_RECORDING
    RT_NOREF(paPoolsCommonEnc);
#else
    m_paPoolsCommonEnc = paPoolsCommonEnc;
#endif

    m_Settings = ScreenSettings;

    /*
     * Populate cached settings.
     */
    HRESULT hrc = ScreenSettings->COMGETTER(Enabled)(&m_SettingsCache.fEnabled);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    com::SafeArray<RecordingFeature_T> aFeatures;
    hrc = m_Settings->COMGETTER(Features)(ComSafeArrayAsOutParam(aFeatures));
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    for (size_t i = 0; i < aFeatures.size(); i++)
        m_SettingsCache.mapFeatures[aFeatures[i]] = true;
    hrc = ScreenSettings->COMGETTER(Destination)(&m_SettingsCache.enmDestination);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    hrc = ScreenSettings->COMGETTER(MaxTime)(&m_SettingsCache.uMaxTime);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    hrc = ScreenSettings->COMGETTER(MaxFileSize)(&m_SettingsCache.uMaxFileSize);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);

    int vrc = RTCritSectInit(&m_CritSect);
    if (RT_FAILURE(vrc))
        return vrc;

    vrc = RTSemEventCreate(&m_WaitEvent);
    if (RT_FAILURE(vrc))
    {
        RTCritSectDelete(&m_CritSect);
        return vrc;
    }

    this->File.m_pWEBM = NULL;
    this->File.m_hFile = NIL_RTFILE;

    RT_ZERO(m_aFramePool);

    vrc = open(ScreenSettings);
    if (RT_FAILURE(vrc))
        return vrc;

    bool const fVideoEnabled = IsFeatureEnabled(RecordingFeature_Video);
    bool const fAudioEnabled = IsFeatureEnabled(RecordingFeature_Audio);

    switch (m_SettingsCache.enmDestination)
    {
        case RecordingDestination_File:
        {
            Bstr bstrFilename;
            hrc = ScreenSettings->COMGETTER(Filename)(bstrFilename.asOutParam());
            AssertComRCBreak(hrc, vrc = VERR_INVALID_PARAMETER);
            AssertBreakStmt(bstrFilename.isNotEmpty(), vrc = VERR_INVALID_PARAMETER);

            Utf8Str strFilename(bstrFilename);
            const char *pszFile = strFilename.c_str();

            RecordingAudioCodec_T enmAudioCodec;
            hrc = ScreenSettings->COMGETTER(AudioCodec)(&enmAudioCodec);
            AssertComRCBreak(hrc, vrc = VERR_INVALID_PARAMETER);

            RecordingVideoCodec_T enmVideoCodec;
            hrc = ScreenSettings->COMGETTER(VideoCodec)(&enmVideoCodec);
            AssertComRCBreak(hrc, vrc = VERR_INVALID_PARAMETER);

            AssertPtr(File.m_pWEBM);
            vrc = File.m_pWEBM->OpenEx(pszFile, &this->File.m_hFile,
                                     fAudioEnabled ? enmAudioCodec : RecordingAudioCodec_None,
                                     fVideoEnabled ? enmVideoCodec : RecordingVideoCodec_None);
            if (RT_FAILURE(vrc))
            {
                LogRel(("Recording: Failed to create output file '%s' (%Rrc)\n", pszFile, vrc));
                break;
            }

            if (   fVideoEnabled
                || fAudioEnabled
               )
            {
                char szWhat[32] = { 0 };
                if (fVideoEnabled)
                    RTStrCat(szWhat, sizeof(szWhat), "video");
                if (fAudioEnabled)
                {
                    if (fVideoEnabled)
                        RTStrCat(szWhat, sizeof(szWhat), " + ");
                    RTStrCat(szWhat, sizeof(szWhat), "audio");
                }

                LogRel(("Recording: Recording %s of screen #%u to '%s'\n", szWhat, m_uScreenID, pszFile));
            }

            break;
        }

        default:
            AssertFailed(); /* Should never happen. */
            vrc = VERR_NOT_IMPLEMENTED;
            break;
    }

#ifdef VBOX_WITH_STATISTICS
    Console::SafeVMPtrQuiet ptrVM(m_pCtx->GetConsole());
    if (ptrVM.isOk())
    {
         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.profileFnProcessTotal,
                                             STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL,
                                             "Profiling the processing function (audio + video).", "/Main/Recording/Stream%RU32/ProfileFnProcessTotal", uScreen);
    }
#endif

    if (RT_SUCCESS(vrc))
    {
        ULONG uFPS;
        hrc = ScreenSettings->COMGETTER(VideoFPS)(&uFPS);
        AssertComRC(hrc);

        size_t const cCmdEntries = RecordingUtilsCalcCapacityFromFpsAndLatency(uFPS, m_pCtx->GetSchedulingHintMs(),
                                                                               2048 /* cMinFPS */, 16384 /* cMaxFPS */);

        vrc = RTCircBufCreate(&m_pCmdCircBuf, sizeof(RECORDINGCMD) * cCmdEntries);
        if (RT_SUCCESS(vrc))
            vrc = RTCritSectInit(&m_CmdCritSect);
    }

    if (RT_SUCCESS(vrc))
    {
        m_uPacketNext = 0;
        m_enmState    = RECORDINGSTREAMSTATE_INITIALIZED;
        return VINF_SUCCESS;
    }

    int vrc2 = uninitCommon();
    AssertRC(vrc2);

    LogRel(("Recording: Stream #%RU32 initialization failed with %Rrc\n", uScreen, vrc));
    return vrc;
}

/**
 * Closes a recording stream.
 * Depending on the stream's recording destination, this function closes all associated handles
 * and finalizes recording.
 *
 * @returns VBox status code.
 */
int RecordingStream::close(void)
{
    int vrc = VINF_SUCCESS;

    switch (m_SettingsCache.enmDestination)
    {
        case RecordingDestination_File:
        {
            if (this->File.m_pWEBM)
                vrc = this->File.m_pWEBM->Close();
            break;
        }

        default:
            AssertFailed(); /* Should never happen. */
            break;
    }

    LogRel(("Recording: Recording screen #%u stopped\n", m_uScreenID));

    if (RT_FAILURE(vrc))
    {
        LogRel(("Recording: Error stopping recording screen #%u, vrc=%Rrc\n", m_uScreenID, vrc));
        return vrc;
    }

    switch (m_SettingsCache.enmDestination)
    {
        case RecordingDestination_File:
        {
            Bstr bstrFilename;
            HRESULT hrc = m_Settings->COMGETTER(Filename)(bstrFilename.asOutParam());
            AssertComRCBreak(hrc, vrc = VERR_INVALID_PARAMETER);

            Utf8Str strFilename(bstrFilename);

            if (RTFileIsValid(this->File.m_hFile))
            {
                vrc = RTFileClose(this->File.m_hFile);
                if (RT_SUCCESS(vrc))
                {
                    LogRel(("Recording: Closed file '%s'\n", strFilename.c_str()));
                }
                else
                {
                    LogRel(("Recording: Error closing file '%s', vrc=%Rrc\n", strFilename.c_str(), vrc));
                    break;
                }
            }

            WebMWriter *pWebMWriter = this->File.m_pWEBM;
            AssertPtr(pWebMWriter);

            if (pWebMWriter)
            {
                /* If no clusters (= data) was written, delete the file again. */
                if (pWebMWriter->GetClusters() == 0)
                {
                    int vrc2 = RTFileDelete(strFilename.c_str());
                    AssertRC(vrc2); /* Ignore vrc on non-debug builds. */
                }

                delete pWebMWriter;
                pWebMWriter = NULL;

                this->File.m_pWEBM = NULL;
            }
            break;
        }

        default:
            vrc = VERR_NOT_IMPLEMENTED;
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Uninitializes a recording stream, internal version.
 *
 * @returns VBox status code.
 */
int RecordingStream::uninitCommon(void)
{
    if (m_enmState == RECORDINGSTREAMSTATE_UNINITIALIZED)
        return VINF_SUCCESS;

    lock();

    int vrc = close();
    if (RT_FAILURE(vrc))
        return vrc;

    for (size_t i = 0; i < RT_ELEMENTS(m_aFramePool); i++)
        RecordingFramePoolDestroy(&m_aFramePool[i]);

    vrc = RTCritSectDelete(&m_CmdCritSect);
    AssertRC(vrc);

    RTCircBufDestroy(m_pCmdCircBuf);
    m_pCmdCircBuf = NULL;

#ifdef VBOX_WITH_AUDIO_RECORDING
    m_paPoolsCommonEnc = NULL;
#endif

    if (RT_SUCCESS(vrc))
    {
        m_enmState = RECORDINGSTREAMSTATE_UNINITIALIZED;

        int const vrc2 = RTSemEventDestroy(m_WaitEvent);
        AssertRC(vrc2);
        m_WaitEvent = NIL_RTSEMEVENT;

        unlock();

        RTCritSectDelete(&m_CritSect);

#ifdef VBOX_WITH_STATISTICS
        Console::SafeVMPtrQuiet ptrVM(m_pCtx->GetConsole());
        if (ptrVM.isOk())
            ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/ProfileFnProcessTotal", m_uScreenID);
#endif

        return VINF_SUCCESS;
    }

    unlock();
    return vrc;
}

/**
 * Uninitializes the video part.
 *
 * @returns VBox status code.
 */
int RecordingStream::uninitVideo(void)
{
#ifdef VBOX_WITH_RECORDING_STATS
    statsVideoLogBuckets("Stats (final)");
#endif

    if (!IsFeatureEnabled(RecordingFeature_Video))
        return VINF_SUCCESS;

    int vrc = RecordingCodecFinalize(&m_CodecVideo);
    if (RT_SUCCESS(vrc))
    {
        vrc = RecordingCodecDestroy(&m_CodecVideo);
        if (RT_SUCCESS(vrc))
            RecordingVideoFrameDestroy(&m_CodecInputFrame);
    }

    if (RT_SUCCESS(vrc))
    {
        RecordingRenderDestroy(&m_Renderer);
        vrc = RTCritSectDelete(&m_RenderCritSect);
    }

    AssertRC(vrc);

#ifdef VBOX_WITH_STATISTICS
    Console::SafeVMPtrQuiet ptrVM(m_pCtx->GetConsole());
    if (ptrVM.isOk())
    {
        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/VideoFramesAdded", m_uScreenID);
        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/VideoFramesToEncode", m_uScreenID);
        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/VideoFramesEncoded", m_uScreenID);
        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/VideoFramesSkipped", m_uScreenID);

        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/ProfileFnProcessVideo", m_uScreenID);
    }
#endif

    return vrc;
}

#ifdef VBOX_WITH_AUDIO_RECORDING
/**
 * Uninitializes the audio part.
 *
 * @returns VBox status code.
 */
int RecordingStream::uninitAudio(void)
{
    if (!IsFeatureEnabled(RecordingFeature_Audio))
        return VINF_SUCCESS;

#ifdef VBOX_WITH_STATISTICS
    Console::SafeVMPtrQuiet ptrVM(m_pCtx->GetConsole());
    if (ptrVM.isOk())
    {
        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/AudioFramesAdded", m_uScreenID);
        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/AudioFramesToEncode", m_uScreenID);
        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/AudioFramesEncoded", m_uScreenID);

        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Recording/Stream%RU32/ProfileFnProcessAudio", m_uScreenID);
    }
# endif

    /* Note: The audio codec is owned by the recording context and will be uninitialized there. */
    m_pCodecAudio = NULL;

    return VINF_SUCCESS;
}
#endif

/**
 * Writes encoded data to a WebM file instance.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec which has encoded the data.
 * @param   pvData              Encoded data to write.
 * @param   cbData              Size (in bytes) of \a pvData.
 * @param   msAbsPTS            Absolute PTS (in ms) of written data.
 * @param   uFlags              Encoding flags of type RECORDINGCODEC_ENC_F_XXX.
 */
int RecordingStream::codecWriteToWebM(PRECORDINGCODEC pCodec, const void *pvData, size_t cbData,
                                      uint64_t msAbsPTS, uint32_t uFlags)
{
    AssertPtr(this->File.m_pWEBM);
    AssertPtr(pvData);
    Assert   (cbData);

    WebMWriter::WebMBlockFlags blockFlags = VBOX_WEBM_BLOCK_FLAG_NONE;
    if (RT_LIKELY(uFlags == RECORDINGCODEC_ENC_F_NONE))
    {
        /* All set. */
    }
    else
    {
        if (uFlags & RECORDINGCODEC_ENC_F_BLOCK_IS_KEY)
            blockFlags |= VBOX_WEBM_BLOCK_FLAG_KEY_FRAME;
        if (uFlags & RECORDINGCODEC_ENC_F_BLOCK_IS_INVISIBLE)
            blockFlags |= VBOX_WEBM_BLOCK_FLAG_INVISIBLE;
    }

    return this->File.m_pWEBM->WriteBlock(  pCodec->Parms.Common.enmType == RECORDINGCODECTYPE_AUDIO
                                          ? m_uTrackAudio : m_uTrackVideo,
                                          pvData, cbData, msAbsPTS, blockFlags);
}

/**
 * Codec callback for writing encoded data to a recording stream.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec which has encoded the data.
 * @param   pvData              Encoded data to write.
 * @param   cbData              Size (in bytes) of \a pvData.
 * @param   msAbsPTS            Absolute PTS (in ms) of written data.
 * @param   uFlags              Encoding flags of type RECORDINGCODEC_ENC_F_XXX.
 * @param   pvUser              User-supplied pointer.
 */
/* static */
DECLCALLBACK(int) RecordingStream::codecWriteDataCallback(PRECORDINGCODEC pCodec, const void *pvData, size_t cbData,
                                                          uint64_t msAbsPTS, uint32_t uFlags, void *pvUser)
{
    RecordingStream *pThis = (RecordingStream *)pvUser;
    AssertPtr(pThis);

    /** @todo For now this is hardcoded to always write to a WebM file. Add other stuff later. */
    return pThis->codecWriteToWebM(pCodec, pvData, cbData, msAbsPTS, uFlags);
}

/**
 * Initializes the video recording for a recording stream.
 *
 * @returns VBox status code.
 * @param   ScreenSettings      Screen settings to use.
 */
int RecordingStream::InitVideo(const ComPtr<IRecordingScreenSettings> &ScreenSettings)
{
    if (!IsFeatureEnabled(RecordingFeature_Video))
        return VINF_SUCCESS;

    RT_ZERO(m_Video.ScreenInfo);
    RT_ZERO(m_Video.OutputTargetDesc);
#ifdef VBOX_WITH_RECORDING_STATS
    RT_ZERO(m_Video.Stats);
#endif

    RecordingVideoFrameDestroy(&m_CodecInputFrame);

    PRECORDINGCODEC pCodec = &m_CodecVideo;

    RECORDINGCODECCALLBACKS Callbacks;
    Callbacks.pvUser       = this;
    Callbacks.pfnWriteData = RecordingStream::codecWriteDataCallback;

    RecordingVideoCodec_T enmVideoCodec;
    HRESULT hrc = ScreenSettings->COMGETTER(VideoCodec)(&enmVideoCodec);
    AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);

    RECORDINGCODECCREATEPARMS Parms;
    Parms.enmType         = RECORDINGCODECTYPE_VIDEO;
    Parms.u.enmVideoCodec = enmVideoCodec;

    int vrc = RecordingCodecCreate(pCodec, &Parms);
    if (RT_SUCCESS(vrc))
        vrc = RecordingCodecInit(pCodec, &Callbacks, ScreenSettings);

    if (RT_SUCCESS(vrc))
    {
        vrc = RTCritSectInit(&m_RenderCritSect);
        if (RT_SUCCESS(vrc))
        {
            vrc = RecordingRenderInit(&m_Renderer, RECORDINGRENDERBACKEND_AUTO);
            if (RT_FAILURE(vrc))
                LogRel(("Recording: Initializing renderer failed with %Rrc\n", vrc));
        }
    }

    if (RT_SUCCESS(vrc))
    {
        ULONG uWidth;
        hrc = ScreenSettings->COMGETTER(VideoWidth)(&uWidth);
        AssertComRC(hrc);
        ULONG uHeight;
        hrc = ScreenSettings->COMGETTER(VideoHeight)(&uHeight);
        AssertComRC(hrc);
        ULONG uRate;
        hrc = ScreenSettings->COMGETTER(VideoRate)(&uRate);
        AssertComRC(hrc);
        ULONG uFPS;
        hrc = ScreenSettings->COMGETTER(VideoFPS)(&uFPS);
        AssertComRC(hrc);

        RT_ZERO(m_RenderParms);
        m_RenderParms.Info.uWidth      = uWidth;
        m_RenderParms.Info.uHeight     = uHeight;
        m_RenderParms.Info.uBPP        = 32;
        m_RenderParms.Info.enmPixelFmt = RECORDINGPIXELFMT_YUVI420; /* Needed for VP8, the only codec we support right now. */

        hrc = ScreenSettings->COMGETTER(VideoScalingMode)(&m_RenderParms.enmScalingMode);
        AssertComRC(hrc);

        /* Fend off non-supported modes. */
        if (   m_RenderParms.enmScalingMode != RecordingVideoScalingMode_None
            && m_RenderParms.enmScalingMode != RecordingVideoScalingMode_NearestNeighbor
            && m_RenderParms.enmScalingMode != RecordingVideoScalingMode_Bilinear
            && m_RenderParms.enmScalingMode != RecordingVideoScalingMode_Bicubic)
        {
            LogRel(("Recording: Warning: Video scaling mode '%s' is not supported\n",
                    RecordingUtilsVideoScalingModeToStr(m_RenderParms.enmScalingMode)));
            m_RenderParms.enmScalingMode = RecordingVideoScalingMode_None;
        }

        LogRel(("Recording: Video scaling mode is set to '%s'\n",
                RecordingUtilsVideoScalingModeToStr(m_RenderParms.enmScalingMode)));

        vrc = RecordingRenderSetParms(&m_Renderer, &m_RenderParms);
        if (RT_SUCCESS(vrc))
        {
            /* Trigger an initial screen change so that necessary textures can be initialized. */
            RECORDINGSURFACEINFO ScreenInfo;
            ScreenInfo             = m_RenderParms.Info;
            ScreenInfo.enmPixelFmt = RECORDINGPIXELFMT_BRGA32;
            vrc = RecordingRenderScreenChange(&m_Renderer, &ScreenInfo);
        }

        if (RT_SUCCESS(vrc))
        {
            vrc = this->File.m_pWEBM->AddVideoTrack(&m_CodecVideo, uWidth, uHeight, uFPS, &m_uTrackVideo);
            if (RT_FAILURE(vrc))
                LogRel(("Recording: Failed to add video track to output file (%Rrc)\n", vrc));
        }

        if (RT_SUCCESS(vrc))
        {
            LogRel2(("Recording: Stream #%u video frame pools:\n", m_uScreenID));

            RTMSINTERVAL const msSched = m_pCtx->GetSchedulingHintMs();

            for (size_t i = 1; i < RT_ELEMENTS(m_aFramePool); i++)
            {
                size_t cFrames = 0; /* Shut up MSVC. */

                if (!RecordingFramePoolIsInitialized(&m_aFramePool[i])) /* Check if initialized already. */
                {
                    vrc = VINF_SUCCESS; /* Reset state. */

                    size_t cbFrame = sizeof(RECORDINGFRAME);

                    /* Only handle the pools video recording needs (i.e. skip audio pool(s)). */
                    switch (i)
                    {
                        /* For the video frame pool we use a quarter of the output video resolution.
                         * Keep slot size tile-based and split larger updates into multiple frame entries.
                         * This keeps RAM usage low while still allowing full-screen updates without changing scheduling. */
                        case RECORDINGFRAME_TYPE_VIDEO:
                        {
                            cFrames  = RecordingUtilsCalcCapacityFromFpsAndLatency(uFPS, msSched,
                                                                                   8 /* cMinFPS */, 512 /* cMaxFPS */);

                            size_t const cxTile = VBOX_RECORDING_VIDEO_POOL_TILE_WIDTH;
                            size_t const cyTile = VBOX_RECORDING_VIDEO_POOL_TILE_HEIGHT + 1;
                            cbFrame += cxTile * cyTile * 4 /* 32 BPP */;

                            /* Ensure that one full-screen update can be queued in tiles plus a tiny safety margin. */
                            size_t const cTilesFullX = ((size_t)uWidth  + cxTile - 1) / cxTile;
                            size_t const cTilesFullY = ((size_t)uHeight + cyTile - 1) / cyTile;
                            size_t const cTilesFull  = cTilesFullX * cTilesFullY;
                            cFrames = RT_MAX(cFrames, cTilesFull + 4);
                            cFrames = RT_MIN(cFrames, (size_t)512);

                        #ifdef DEBUG_andy
                            cFrames *= 4;
                        #endif
                            break;
                        }

                        case RECORDINGFRAME_TYPE_CURSOR_SHAPE:
                        {
                            cFrames  = RecordingUtilsCalcCapacityFromFpsAndLatency(uFPS, msSched,
                                                                                   4 /* cMinFPS */, 128 /* cMaxFPS */);
                            cbFrame += 64 * 64 * (32 /* BPP */ / 8);
                            break;
                        }

                        case RECORDINGFRAME_TYPE_CURSOR_POS:
                            cFrames  = RecordingUtilsCalcCapacityFromFpsAndLatency(uFPS, msSched,
                                                                                   16 /* cMinFPS */, 512 /* cMaxFPS */);
                            break;

                        case RECORDINGFRAME_TYPE_SCREEN_CHANGE:
                            cFrames  = 16;
                            break;
                        default:
                            vrc = VINF_NOT_SUPPORTED;
                            break;
                    }

                    if (vrc == VINF_SUCCESS)
                    {
                        Assert(cFrames);
                        Assert(cbFrame);

                        vrc = RecordingFramePoolInit(&m_aFramePool[i], (RECORDINGFRAME_TYPE)i, cbFrame, cFrames);
                        AssertRCBreak(vrc);
                        uint32_t idRdr;
                        vrc = RecordingFramePoolAddReader(&m_aFramePool[i], &idRdr);
                        AssertRCBreak(vrc);
                        Assert(idRdr == 0); /* Only the stream itself is allowed as a reader. */

                        LogRel2(("Recording:   %-34s: %zu bytes x %zu -> %zu bytes\n",
                                 RecordingUtilsFrameTypeToStr((RECORDINGFRAME_TYPE)i), cbFrame, cFrames, cbFrame * cFrames));
                    }
                }
            }
        }

        if (RT_SUCCESS(vrc))
            LogRel(("Recording: Recording video of screen #%u with %RU32x%RU32 @ %RU32 kbps, %RU32 FPS (track #%RU8)\n",
                    m_uScreenID, uWidth, uHeight, uRate, uFPS, m_uTrackVideo));
    }

    if (RT_FAILURE(vrc))
        LogRel(("Recording: Initializing video codec failed with %Rrc\n", vrc));

#ifdef VBOX_WITH_STATISTICS
    Console::SafeVMPtrQuiet ptrVM(m_pCtx->GetConsole());
    if (ptrVM.isOk())
    {
         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.profileFnProcessVideo,
                                             STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL,
                                             "Profiling the processing function (video).", "/Main/Recording/Stream%RU32/ProfileFnProcessVideo", m_uScreenID);
         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.cVideoFramesAdded,
                                             STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                                             "Total video frames added.", "/Main/Recording/Stream%RU32/VideoFramesAdded", m_uScreenID);
         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.cVideoFramesToEncode,
                                             STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                                             "Current video frames (pending) to encode.", "/Main/Recording/Stream%RU32/VideoFramesToEncode", m_uScreenID);
         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.cVideoFramesEncoded,
                                             STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                                             "Total video frames encoded.", "/Main/Recording/Stream%RU32/VideoFramesEncoded", m_uScreenID);
         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.cVideoFramesSkipped,
                                             STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                                             "Total video frames skipped to encode.", "/Main/Recording/Stream%RU32/VideoFramesSkipped", m_uScreenID);
    }
#endif
    return vrc;
}

#ifdef VBOX_WITH_AUDIO_RECORDING
/**
 * Initializes audio for a recording stream.
 *
 * @returns VBox status code.
 * @param   ScreenSettings              Recording screen settings to use for initialization.
 * @param   pCodecAudio                 Pointer to audio codec instance to use.
 */
int RecordingStream::InitAudio(const ComPtr<IRecordingScreenSettings> &ScreenSettings, PRECORDINGCODEC pCodecAudio)
{
    if (!IsFeatureEnabled(RecordingFeature_Audio))
        return VINF_SUCCESS;

    m_pCodecAudio = pCodecAudio;

    int vrc;

    ULONG uBits;
    HRESULT hrc = ScreenSettings->COMGETTER(AudioBits)(&uBits);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    ULONG cChannels;
    hrc = ScreenSettings->COMGETTER(AudioChannels)(&cChannels);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    AssertReturn(cChannels, VERR_RECORDING_INIT_FAILED);
    ULONG uHz;
    hrc = ScreenSettings->COMGETTER(AudioHz)(&uHz);
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);
    com::Bstr bstrOpts;
    hrc = ScreenSettings->COMGETTER(Options)(bstrOpts.asOutParam());
    AssertComRCReturn(hrc, VERR_RECORDING_INIT_FAILED);

    LogRel(("Recording: Recording audio of screen #%u in %RU16Hz, %RU8 bit, %RU8 %s (track #%RU8)\n",
            m_uScreenID, uHz, uBits, cChannels, cChannels >= 2 ? "channels" : "channel", m_uTrackAudio));

    AssertPtr(m_pCodecAudio);
    vrc = this->File.m_pWEBM->AddAudioTrack(m_pCodecAudio, uHz, cChannels, uBits, &m_uTrackAudio);
    if (RT_FAILURE(vrc))
    {
        LogRel(("Recording: Failed to add audio track to output file (%Rrc)\n", vrc));
        return VERR_RECORDING_INIT_FAILED;
    }

    /* Note: The audio frame pool is part of the recording context, as we
     *       multiplex audio data to all streams. */

# ifdef VBOX_WITH_STATISTICS
    Console::SafeVMPtrQuiet ptrVM(m_pCtx->GetConsole());
    if (ptrVM.isOk())
    {
         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.cVideoFramesAdded,
                                              STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                                              "Total audio frames added.", "/Main/Recording/Stream%RU32/AudioFramesAdded", m_uScreenID);
         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.cAudioFramesToEncode,
                                              STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                                              "Current audio frames (pending) to encode.", "/Main/Recording/Stream%RU32/AudioFramesToEncode", m_uScreenID);
         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.cAudioFramesEncoded,
                                              STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                                              "Total audio frames encoded.", "/Main/Recording/Stream%RU32/AudioFramesEncoded", m_uScreenID);

         ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &m_STAM.profileFnProcessAudio,
                                             STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_CALL,
                                             "Profiling the processing function (audio).", "/Main/Recording/Stream%RU32/ProfileFnProcessAudio", m_uScreenID);
    }
# endif

    return vrc;
}
#endif /* VBOX_WITH_AUDIO_RECORDING */

/**
 * Locks a recording stream.
 */
void RecordingStream::lock(void)
{
    int vrc = RTCritSectEnter(&m_CritSect);
    AssertRC(vrc);
}

/**
 * Unlocks a locked recording stream.
 */
void RecordingStream::unlock(void)
{
    int vrc = RTCritSectLeave(&m_CritSect);
    AssertRC(vrc);
}
