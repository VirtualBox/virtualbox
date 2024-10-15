/* $Id: RecordingStream.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Recording stream code.
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

#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_RECORDING
#include "LoggingNew.h"

#include <iprt/path.h>

#ifdef VBOX_RECORDING_DUMP
# include <iprt/formats/bmp.h>
#endif

#ifdef VBOX_WITH_AUDIO_RECORDING
# include <VBox/vmm/pdmaudioinline.h>
#endif

#include "Recording.h"
#include "RecordingUtils.h"
#include "WebMWriter.h"


RecordingStream::RecordingStream(RecordingContext *a_pCtx, uint32_t uScreen, const settings::RecordingScreen &Settings)
    : m_enmState(RECORDINGSTREAMSTATE_UNINITIALIZED)
{
    int vrc2 = initInternal(a_pCtx, uScreen, Settings);
    if (RT_FAILURE(vrc2))
        throw vrc2;
}

RecordingStream::~RecordingStream(void)
{
    int vrc2 = uninitInternal();
    AssertRC(vrc2);
}

/**
 * Opens a recording stream.
 *
 * @returns VBox status code.
 * @param   screenSettings      Recording settings to use.
 */
int RecordingStream::open(const settings::RecordingScreen &screenSettings)
{
    /* Sanity. */
    Assert(screenSettings.enmDest != RecordingDestination_None);

    int vrc;

    switch (screenSettings.enmDest)
    {
        case RecordingDestination_File:
        {
            Assert(screenSettings.File.strName.isNotEmpty());

            const char *pszFile = screenSettings.File.strName.c_str();

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
                {
                    this->File.m_hFile = hFile;
                    m_ScreenSettings.File.strName = pszFile;
                }
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
const settings::RecordingScreen &RecordingStream::GetConfig(void) const
{
    return m_ScreenSettings;
}

/**
 * Checks if a specified limit for a recording stream has been reached, internal version.
 *
 * @returns @c true if any limit has been reached, @c false if not.
 * @param   msTimestamp         Timestamp (PTS, in ms) to check for.
 */
bool RecordingStream::isLimitReachedInternal(uint64_t msTimestamp) const
{
    LogFlowThisFunc(("msTimestamp=%RU64, ulMaxTimeS=%RU32, tsStartMs=%RU64\n",
                     msTimestamp, m_ScreenSettings.ulMaxTimeS, m_tsStartMs));

    if (   m_ScreenSettings.ulMaxTimeS
        && msTimestamp >= m_ScreenSettings.ulMaxTimeS * RT_MS_1SEC)
    {
        LogRel(("Recording: Time limit for stream #%RU16 has been reached (%RU32s)\n",
                m_uScreenID, m_ScreenSettings.ulMaxTimeS));
        return true;
    }

    if (m_ScreenSettings.enmDest == RecordingDestination_File)
    {
        if (m_ScreenSettings.File.ulMaxSizeMB)
        {
            uint64_t sizeInMB = this->File.m_pWEBM->GetFileSize() / _1M;
            if(sizeInMB >= m_ScreenSettings.File.ulMaxSizeMB)
            {
                LogRel(("Recording: File size limit for stream #%RU16 has been reached (%RU64MB)\n",
                        m_uScreenID, m_ScreenSettings.File.ulMaxSizeMB));
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
    AssertReturn(!RTCritSectIsOwner(&m_CritSect), VERR_WRONG_ORDER);

    if (!m_fEnabled)
        return VINF_SUCCESS;

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
            m_fEnabled = false;

            int vrc2 = m_pCtx->onLimitReached(m_uScreenID, VINF_SUCCESS /* vrc */);
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
    if (!m_fEnabled)
        return true;

    return isLimitReachedInternal(msTimestamp);
}

/**
 * Returns whether a feature for a recording stream is enabled or not.
 *
 * @returns @c true if ready, @c false if not.
 * @param   enmFeature          Feature of stream to check enabled status for.
 */
bool RecordingStream::IsFeatureEnabled(RecordingFeature_T enmFeature) const
{
    return m_fEnabled && m_ScreenSettings.isFeatureEnabled(enmFeature);
}

/**
 * Returns if a recording stream needs to be fed with an update or not.
 *
 * @returns @c true if an update is needed, @c false if not.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 */
bool RecordingStream::NeedsUpdate(uint64_t msTimestamp) const
{
    return recordingCodecGetWritable((const PRECORDINGCODEC)&m_CodecVideo, msTimestamp) > 0;
}

/**
 * Processes a recording stream.
 *
 * This function takes care of the actual encoding and writing of a certain stream.
 * As this can be very CPU intensive, this function usually is called from a separate thread.
 *
 * @returns VBox status code.
 * @param   streamBlocks        Block set of stream to process.
 * @param   commonBlocks        Block set of common blocks to process for this stream.
 *
 * @note    Runs in recording thread.
 */
int RecordingStream::process(const RecordingBlockSet &streamBlocks, RecordingBlockMap &commonBlocks)
{
    LogFlowFuncEnter();

    lock();

    if (!m_ScreenSettings.fEnabled)
    {
        unlock();
        return VINF_SUCCESS;
    }

    int vrc = VINF_SUCCESS;

    RecordingBlockMap::const_iterator itStreamBlock = streamBlocks.Map.begin();
    while (itStreamBlock != streamBlocks.Map.end())
    {
        uint64_t const   msTimestamp = itStreamBlock->first; RT_NOREF(msTimestamp);
        RecordingBlocks *pBlocks     = itStreamBlock->second;

        AssertPtr(pBlocks);

        RecordingBlockList::const_iterator itBlockInList = pBlocks->List.cbegin();
        while (itBlockInList != pBlocks->List.cend())
        {
            PRECORDINGFRAME pFrame = (PRECORDINGFRAME)(*itBlockInList)->pvData;
            AssertPtr(pFrame);
            Assert(pFrame->msTimestamp == msTimestamp);

            switch (pFrame->enmType)
            {
                case RECORDINGFRAME_TYPE_VIDEO:
                    RT_FALL_THROUGH();
                case RECORDINGFRAME_TYPE_CURSOR_SHAPE:
                    RT_FALL_THROUGH();
                case RECORDINGFRAME_TYPE_CURSOR_POS:
                {
                    int vrc2 = recordingCodecEncodeFrame(&m_CodecVideo, pFrame, pFrame->msTimestamp, m_pCtx /* pvUser */);
                    AssertRC(vrc2);
                    if (RT_SUCCESS(vrc))
                        vrc = vrc2;
                    break;
                }

                case RECORDINGFRAME_TYPE_SCREEN_CHANGE:
                {
                    /* ignore rc */ recordingCodecScreenChange(&m_CodecVideo, &pFrame->u.ScreenInfo);
                    break;
                }

                default:
                    break;
            }

            ++itBlockInList;
        }

        ++itStreamBlock;
    }

#ifdef VBOX_WITH_AUDIO_RECORDING
    /* Do we need to multiplex the common audio data to this stream? */
    if (m_ScreenSettings.isFeatureEnabled(RecordingFeature_Audio))
    {
        /* As each (enabled) screen has to get the same audio data, look for common (audio) data which needs to be
         * written to the screen's assigned recording stream. */
        RecordingBlockMap::const_iterator itBlockMap = commonBlocks.begin();
        while (itBlockMap != commonBlocks.end())
        {
            RecordingBlockList &blockList = itBlockMap->second->List;

            RecordingBlockList::iterator itBlockList = blockList.begin();
            while (itBlockList != blockList.end())
            {
                RecordingBlock *pBlock = (RecordingBlock *)(*itBlockList);

                PRECORDINGFRAME      pFrame      = (PRECORDINGFRAME)pBlock->pvData;
                Assert(pFrame->enmType == RECORDINGFRAME_TYPE_AUDIO);
                PRECORDINGAUDIOFRAME pAudioFrame = &pFrame->u.Audio;

                int vrc2 = this->File.m_pWEBM->WriteBlock(m_uTrackAudio, pAudioFrame->pvBuf, pAudioFrame->cbBuf, pBlock->msTimestamp, pBlock->uFlags);
                if (RT_SUCCESS(vrc))
                    vrc = vrc2;

                Log3Func(("RECORDINGFRAME_TYPE_AUDIO: %zu bytes -> %Rrc\n", pAudioFrame->cbBuf, vrc2));

                Assert(pBlock->cRefs);
                pBlock->cRefs--;
                if (pBlock->cRefs == 0)
                {
                    blockList.erase(itBlockList);
                    delete pBlock;
                    itBlockList = blockList.begin();
                }
                else
                    ++itBlockList;
            }

            /* If no entries are left over in the block list, remove it altogether. */
            if (blockList.empty())
            {
                delete itBlockMap->second;
                commonBlocks.erase(itBlockMap);
                itBlockMap = commonBlocks.begin();
            }
            else
                ++itBlockMap;
        }
    }
#else
    RT_NOREF(commonBlocks);
#endif /* VBOX_WITH_AUDIO_RECORDING */

    unlock();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * The stream's main routine called from the encoding thread.
 *
 * @returns VBox status code.
 * @retval  VINF_RECORDING_LIMIT_REACHED if the stream's recording limit has been reached.
 * @param   rcWait              Result of the encoding thread's wait operation.
 *                              Can be used for figuring out if the encoder has to perform some
 *                              worked based on that result.
 * @param   msTimestamp         Timestamp to use for PTS calculation (absolute).
 * @param   commonBlocks        Common blocks multiplexed to all recording streams.
 *
 * @note    Runs in encoding thread.
 */
int RecordingStream::ThreadMain(int rcWait, uint64_t msTimestamp, RecordingBlockMap &commonBlocks)
{
    Log3Func(("uScreenID=%RU16, msTimestamp=%RU64, rcWait=%Rrc\n", m_uScreenID, msTimestamp, rcWait));

    /* No new data arrived within time? Feed the encoder with the last frame we built.
     *
     * This is necessary in order to render a video which has a consistent time line,
     * as we only encode data when something has changed ("dirty areas"). */
    if (   rcWait == VERR_TIMEOUT
        && m_ScreenSettings.isFeatureEnabled(RecordingFeature_Video))
    {
        return recordingCodecEncodeCurrent(&m_CodecVideo, msTimestamp);
    }

    int vrc = process(m_Blocks, commonBlocks);

    /*
     * Housekeeping.
     *
     * Here we delete all processed stream blocks of this stream.
     * The common blocks will be deleted by the recording context (which owns those).
     */
    lock();

    RecordingBlockMap::iterator itStreamBlocks = m_Blocks.Map.begin();
    while (itStreamBlocks != m_Blocks.Map.end())
    {
        RecordingBlocks *pBlocks = itStreamBlocks->second;
        AssertPtr(pBlocks);
        pBlocks->Clear();
        Assert(pBlocks->List.empty());
        delete pBlocks;

        m_Blocks.Map.erase(itStreamBlocks);
        itStreamBlocks = m_Blocks.Map.begin();
    }
    Assert(m_Blocks.Map.empty());

    unlock();

    return vrc;
}

/**
 * Adds a recording frame to be fed to the encoder.
 *
 * @returns VBox status code.
 * @param   pFrame              Recording frame to add.
 *                              Ownership of the frame will be transferred to the encoder on success then.
 *                              Must be free'd by the caller on failure.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 *
 * @note    Caller needs to take the stream's lock.
 */
int RecordingStream::addFrame(PRECORDINGFRAME pFrame, uint64_t msTimestamp)
{
    int vrc;

    Assert(pFrame->msTimestamp == msTimestamp); /* Sanity. */

    try
    {
        RecordingBlock *pBlock = new RecordingBlock();

        pBlock->pvData = pFrame;
        pBlock->cbData = sizeof(RECORDINGFRAME);

        try
        {
            RecordingBlocks *pRecordingBlocks;
            RecordingBlockMap::const_iterator it = m_Blocks.Map.find(msTimestamp);
            if (it == m_Blocks.Map.end())
            {
                pRecordingBlocks = new RecordingBlocks();
                pRecordingBlocks->List.push_back(pBlock);
                m_Blocks.Map.insert(std::make_pair(msTimestamp, pRecordingBlocks));
            }
            else
            {
                pRecordingBlocks = it->second;
                pRecordingBlocks->List.push_back(pBlock);
            }

            vrc = VINF_SUCCESS;
        }
        catch (const std::exception &)
        {
            delete pBlock;
            vrc = VERR_NO_MEMORY;
        }
    }
    catch (const std::exception &)
    {
        vrc = VERR_NO_MEMORY;
    }

    return vrc;
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
    AssertPtrReturn(m_pCtx, VERR_WRONG_ORDER);

    /* As audio data is common across all streams, re-route this to the recording context, where
     * the data is being encoded and stored in the common blocks queue. */
    return m_pCtx->SendAudioFrame(pvData, cbData, msTimestamp);
}

/**
 * Sends a cursor position change to the recording stream.
 *
 * @returns VBox status code.
 * @param   idCursor            Cursor ID. Currently unused and always set to 0.
 * @param   pPos                Cursor information to send.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 */
int RecordingStream::SendCursorPos(uint8_t idCursor, PRECORDINGPOS pPos, uint64_t msTimestamp)
{
    RT_NOREF(idCursor);
    AssertPtrReturn(pPos, VERR_INVALID_POINTER);

    int vrc = iterateInternal(msTimestamp);
    if (vrc != VINF_SUCCESS) /* Can return VINF_RECORDING_LIMIT_REACHED. */
        return vrc;

    PRECORDINGFRAME pFrame = (PRECORDINGFRAME)RTMemAlloc(sizeof(RECORDINGFRAME));
    AssertPtrReturn(pFrame, VERR_NO_MEMORY);
    pFrame->enmType     = RECORDINGFRAME_TYPE_CURSOR_POS;
    pFrame->msTimestamp = msTimestamp;

    pFrame->u.Cursor.Pos = *pPos;

    lock();

    vrc = addFrame(pFrame, msTimestamp);

    unlock();

    return vrc;
}

/**
 * Sends a cursor shape change to the recording stream.
 *
 * @returns VBox status code.
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
    AssertPtrReturn(pShape, VERR_INVALID_POINTER);
    AssertPtrReturn(m_pCtx, VERR_WRONG_ORDER);

    int vrc = iterateInternal(msTimestamp);
    if (vrc != VINF_SUCCESS) /* Can return VINF_RECORDING_LIMIT_REACHED. */
        return vrc;

    PRECORDINGFRAME pFrame = (PRECORDINGFRAME)RTMemAlloc(sizeof(RECORDINGFRAME));
    AssertPtrReturn(pFrame, VERR_NO_MEMORY);

    pFrame->u.Video = *pShape;
    /* Make a deep copy of the pixel data. */
    pFrame->u.Video.pau8Buf = (uint8_t *)RTMemDup(pShape->pau8Buf, pShape->cbBuf);
    AssertPtrReturnStmt(pFrame->u.Video.pau8Buf, RTMemFree(pFrame), VERR_NO_MEMORY);
    pFrame->u.Video.cbBuf   = pShape->cbBuf;

    pFrame->enmType     = RECORDINGFRAME_TYPE_CURSOR_SHAPE;
    pFrame->msTimestamp = msTimestamp;

    lock();

    vrc = addFrame(pFrame, msTimestamp);

    unlock();

    if (RT_FAILURE(vrc))
    {
        RecordingVideoFrameDestroy(&pFrame->u.Video);
        RecordingFrameFree(pFrame);
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Sends a raw (e.g. not yet encoded) video frame to the recording stream.
 *
 * @returns VBox status code.
 * @retval  VINF_RECORDING_LIMIT_REACHED if the stream's recording limit has been reached.
 * @retval  VINF_RECORDING_THROTTLED if the frame is too early for the current FPS setting.
 * @param   pVideoFrame         Video frame to send.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 *
 * @note    Keep it as simple as possible, as this function might run on EMT.
 * @thread  EMT
 */
int RecordingStream::SendVideoFrame(PRECORDINGVIDEOFRAME pVideoFrame, uint64_t msTimestamp)
{
    AssertPtrReturn(pVideoFrame, VERR_INVALID_POINTER);
    AssertPtrReturn(m_pCtx, VERR_WRONG_ORDER);

    int vrc = iterateInternal(msTimestamp);
    if (vrc != VINF_SUCCESS) /* Can return VINF_RECORDING_LIMIT_REACHED. */
        return vrc;

    PRECORDINGFRAME pFrame = (PRECORDINGFRAME)RTMemAlloc(sizeof(RECORDINGFRAME));
    AssertPtrReturn(pFrame, VERR_NO_MEMORY);

    pFrame->u.Video = *pVideoFrame;

    /* Make a deep copy of the pixel data. */
    pFrame->u.Video.pau8Buf = (uint8_t *)RTMemAlloc(pVideoFrame->cbBuf);
    AssertPtrReturnStmt(pFrame->u.Video.pau8Buf, RTMemFree(pFrame), VERR_NO_MEMORY);
    size_t       offDst            = 0;
    size_t       offSrc            = 0;
    size_t const cbDstBytesPerLine = pVideoFrame->Info.uWidth * (pVideoFrame->Info.uBPP / 8);
    for (uint32_t h = 0; h < pFrame->u.Video.Info.uHeight; h++)
    {
        memcpy(pFrame->u.Video.pau8Buf + offDst, pVideoFrame->pau8Buf + offSrc, cbDstBytesPerLine);
        offDst += cbDstBytesPerLine;
        offSrc += pVideoFrame->Info.uBytesPerLine;
    }
    pFrame->u.Video.Info.uBytesPerLine = (uint32_t)cbDstBytesPerLine;

    pFrame->enmType     = RECORDINGFRAME_TYPE_VIDEO;
    pFrame->msTimestamp = msTimestamp;

    lock();

    vrc = addFrame(pFrame, msTimestamp);

    unlock();

    if (RT_FAILURE(vrc))
    {
        RecordingVideoFrameDestroy(&pFrame->u.Video);
        RecordingFrameFree(pFrame);
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Sends a screen size change to a recording stream.
 *
 * @returns VBox status code.
 * @param   pInfo               Recording screen info to use.
 * @param   msTimestamp         Timestamp (PTS, in ms).
 * @param   fForce              Set to \c true to force a change, otherwise to \c false.
 */
int RecordingStream::SendScreenChange(PRECORDINGSURFACEINFO pInfo, uint64_t msTimestamp, bool fForce /* = false */)
{
    AssertPtrReturn(pInfo, VERR_INVALID_POINTER);

    if (   !pInfo->uWidth
        || !pInfo->uHeight)
        return VINF_SUCCESS;

    RT_NOREF(fForce);

    LogRel(("Recording: Size of screen #%RU32 changed to %RU32x%RU32 (%RU8 BPP)\n",
            m_uScreenID, pInfo->uWidth, pInfo->uHeight, pInfo->uBPP));

    lock();

    PRECORDINGFRAME pFrame = (PRECORDINGFRAME)RTMemAlloc(sizeof(RECORDINGFRAME));
    AssertPtrReturn(pFrame, VERR_NO_MEMORY);
    pFrame->enmType      = RECORDINGFRAME_TYPE_SCREEN_CHANGE;
    pFrame->msTimestamp  = msTimestamp;

    pFrame->u.ScreenInfo = *pInfo;

    int vrc = addFrame(pFrame, msTimestamp);

    unlock();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Initializes a recording stream.
 *
 * @returns VBox status code.
 * @param   pCtx                Pointer to recording context.
 * @param   uScreen             Screen number to use for this recording stream.
 * @param   Settings            Recording screen configuration to use for initialization.
 */
int RecordingStream::Init(RecordingContext *pCtx, uint32_t uScreen, const settings::RecordingScreen &Settings)
{
    return initInternal(pCtx, uScreen, Settings);
}

/**
 * Initializes a recording stream, internal version.
 *
 * @returns VBox status code.
 * @param   pCtx                Pointer to recording context.
 * @param   uScreen             Screen number to use for this recording stream.
 * @param   screenSettings      Recording screen configuration to use for initialization.
 */
int RecordingStream::initInternal(RecordingContext *pCtx, uint32_t uScreen,
                                  const settings::RecordingScreen &screenSettings)
{
    AssertReturn(m_enmState == RECORDINGSTREAMSTATE_UNINITIALIZED, VERR_WRONG_ORDER);

    m_pCtx         = pCtx;
    m_uTrackAudio    = UINT8_MAX;
    m_uTrackVideo    = UINT8_MAX;
    m_tsStartMs      = 0;
    m_uScreenID      = uScreen;
#ifdef VBOX_WITH_AUDIO_RECORDING
    /* We use the codec from the recording context, as this stream only receives multiplexed data (same audio for all streams). */
    m_pCodecAudio    = m_pCtx->GetCodecAudio();
#endif
    m_ScreenSettings = screenSettings;

    settings::RecordingScreen *pSettings = &m_ScreenSettings;

    int vrc = RTCritSectInit(&m_CritSect);
    if (RT_FAILURE(vrc))
        return vrc;

    this->File.m_pWEBM = NULL;
    this->File.m_hFile = NIL_RTFILE;

    vrc = open(*pSettings);
    if (RT_FAILURE(vrc))
        return vrc;

    const bool fVideoEnabled = pSettings->isFeatureEnabled(RecordingFeature_Video);
    const bool fAudioEnabled = pSettings->isFeatureEnabled(RecordingFeature_Audio);

    if (fVideoEnabled)
    {
        vrc = initVideo(*pSettings);
        if (RT_FAILURE(vrc))
            return vrc;
    }

    switch (pSettings->enmDest)
    {
        case RecordingDestination_File:
        {
            Assert(pSettings->File.strName.isNotEmpty());
            const char *pszFile = pSettings->File.strName.c_str();

            AssertPtr(File.m_pWEBM);
            vrc = File.m_pWEBM->OpenEx(pszFile, &this->File.m_hFile,
                                     fAudioEnabled ? pSettings->Audio.enmCodec : RecordingAudioCodec_None,
                                     fVideoEnabled ? pSettings->Video.enmCodec : RecordingVideoCodec_None);
            if (RT_FAILURE(vrc))
            {
                LogRel(("Recording: Failed to create output file '%s' (%Rrc)\n", pszFile, vrc));
                break;
            }

            if (fVideoEnabled)
            {
                vrc = this->File.m_pWEBM->AddVideoTrack(&m_CodecVideo,
                                                      pSettings->Video.ulWidth, pSettings->Video.ulHeight, pSettings->Video.ulFPS,
                                                      &m_uTrackVideo);
                if (RT_FAILURE(vrc))
                {
                    LogRel(("Recording: Failed to add video track to output file '%s' (%Rrc)\n", pszFile, vrc));
                    break;
                }

                LogRel(("Recording: Recording video of screen #%u with %RU32x%RU32 @ %RU32 kbps, %RU32 FPS (track #%RU8)\n",
                        m_uScreenID, pSettings->Video.ulWidth, pSettings->Video.ulHeight,
                        pSettings->Video.ulRate, pSettings->Video.ulFPS, m_uTrackVideo));
            }

#ifdef VBOX_WITH_AUDIO_RECORDING
            if (fAudioEnabled)
            {
                AssertPtr(m_pCodecAudio);
                vrc = this->File.m_pWEBM->AddAudioTrack(m_pCodecAudio,
                                                      pSettings->Audio.uHz, pSettings->Audio.cChannels, pSettings->Audio.cBits,
                                                      &m_uTrackAudio);
                if (RT_FAILURE(vrc))
                {
                    LogRel(("Recording: Failed to add audio track to output file '%s' (%Rrc)\n", pszFile, vrc));
                    break;
                }

                LogRel(("Recording: Recording audio of screen #%u in %RU16Hz, %RU8 bit, %RU8 %s (track #%RU8)\n",
                        m_uScreenID, pSettings->Audio.uHz, pSettings->Audio.cBits, pSettings->Audio.cChannels,
                        pSettings->Audio.cChannels ? "channels" : "channel", m_uTrackAudio));
            }
#endif

            if (   fVideoEnabled
#ifdef VBOX_WITH_AUDIO_RECORDING
                || fAudioEnabled
#endif
               )
            {
                char szWhat[32] = { 0 };
                if (fVideoEnabled)
                    RTStrCat(szWhat, sizeof(szWhat), "video");
#ifdef VBOX_WITH_AUDIO_RECORDING
                if (fAudioEnabled)
                {
                    if (fVideoEnabled)
                        RTStrCat(szWhat, sizeof(szWhat), " + ");
                    RTStrCat(szWhat, sizeof(szWhat), "audio");
                }
#endif
                LogRel(("Recording: Recording %s of screen #%u to '%s'\n", szWhat, m_uScreenID, pszFile));
            }

            break;
        }

        default:
            AssertFailed(); /* Should never happen. */
            vrc = VERR_NOT_IMPLEMENTED;
            break;
    }

    if (RT_SUCCESS(vrc))
    {
        m_enmState  = RECORDINGSTREAMSTATE_INITIALIZED;
        m_fEnabled  = true;
        m_tsStartMs = RTTimeMilliTS();

        return VINF_SUCCESS;
    }

    int vrc2 = uninitInternal();
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

    /* ignore rc */ recordingCodecFinalize(&m_CodecVideo);

    switch (m_ScreenSettings.enmDest)
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

    m_Blocks.Clear();

    LogRel(("Recording: Recording screen #%u stopped\n", m_uScreenID));

    if (RT_FAILURE(vrc))
    {
        LogRel(("Recording: Error stopping recording screen #%u, vrc=%Rrc\n", m_uScreenID, vrc));
        return vrc;
    }

    switch (m_ScreenSettings.enmDest)
    {
        case RecordingDestination_File:
        {
            if (RTFileIsValid(this->File.m_hFile))
            {
                vrc = RTFileClose(this->File.m_hFile);
                if (RT_SUCCESS(vrc))
                {
                    LogRel(("Recording: Closed file '%s'\n", m_ScreenSettings.File.strName.c_str()));
                }
                else
                {
                    LogRel(("Recording: Error closing file '%s', vrc=%Rrc\n", m_ScreenSettings.File.strName.c_str(), vrc));
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
                    int vrc2 = RTFileDelete(m_ScreenSettings.File.strName.c_str());
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
 * Uninitializes a recording stream.
 *
 * @returns VBox status code.
 */
int RecordingStream::Uninit(void)
{
    return uninitInternal();
}

/**
 * Uninitializes a recording stream, internal version.
 *
 * @returns VBox status code.
 */
int RecordingStream::uninitInternal(void)
{
    if (m_enmState != RECORDINGSTREAMSTATE_INITIALIZED)
        return VINF_SUCCESS;

    int vrc = close();
    if (RT_FAILURE(vrc))
        return vrc;

#ifdef VBOX_WITH_AUDIO_RECORDING
    m_pCodecAudio = NULL;
#endif

    if (m_ScreenSettings.isFeatureEnabled(RecordingFeature_Video))
        vrc = recordingCodecDestroy(&m_CodecVideo);

    if (RT_SUCCESS(vrc))
    {
        RTCritSectDelete(&m_CritSect);

        m_enmState = RECORDINGSTREAMSTATE_UNINITIALIZED;
        m_fEnabled = false;
    }

    return vrc;
}

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

    return this->File.m_pWEBM->WriteBlock(  pCodec->Parms.enmType == RECORDINGCODECTYPE_AUDIO
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
 * @param   screenSettings      Screen settings to use.
 */
int RecordingStream::initVideo(const settings::RecordingScreen &screenSettings)
{
    /* Sanity. */
    AssertReturn(screenSettings.Video.ulRate,   VERR_INVALID_PARAMETER);
    AssertReturn(screenSettings.Video.ulWidth,  VERR_INVALID_PARAMETER);
    AssertReturn(screenSettings.Video.ulHeight, VERR_INVALID_PARAMETER);
    AssertReturn(screenSettings.Video.ulFPS,    VERR_INVALID_PARAMETER);

    PRECORDINGCODEC pCodec = &m_CodecVideo;

    RECORDINGCODECCALLBACKS Callbacks;
    Callbacks.pvUser       = this;
    Callbacks.pfnWriteData = RecordingStream::codecWriteDataCallback;

    RECORDINGSURFACEINFO ScreenInfo;
    ScreenInfo.uWidth  = screenSettings.Video.ulWidth;
    ScreenInfo.uHeight = screenSettings.Video.ulHeight;
    ScreenInfo.uBPP    = 32; /* We always start with 32 bit. */

    int vrc = SendScreenChange(&ScreenInfo, true /* fForce */);
    if (RT_SUCCESS(vrc))
    {
        vrc = recordingCodecCreateVideo(pCodec, screenSettings.Video.enmCodec);
        if (RT_SUCCESS(vrc))
            vrc = recordingCodecInit(pCodec, &Callbacks, screenSettings);
    }

    if (RT_FAILURE(vrc))
        LogRel(("Recording: Initializing video codec failed with %Rrc\n", vrc));

    return vrc;
}

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

