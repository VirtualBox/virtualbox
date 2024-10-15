/* $Id: WebMWriter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * WebMWriter.cpp - WebM container handling.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

/**
 * For more information, see:
 * - https://w3c.github.io/media-source/webm-byte-stream-format.html
 * - https://www.webmproject.org/docs/container/#muxer-guidelines
 */

#define LOG_GROUP LOG_GROUP_RECORDING
#include "LoggingNew.h"

#include <iprt/buildconfig.h>
#include <iprt/errcore.h>

#include <VBox/version.h>

#include "RecordingInternals.h"
#include "WebMWriter.h"


WebMWriter::WebMWriter(void)
{
    /* Size (in bytes) of time code to write. We use 2 bytes (16 bit) by default. */
    m_cbTimecode   = 2;
    m_uTimecodeMax = UINT16_MAX;

    m_fInTracksSection = false;
}

WebMWriter::~WebMWriter(void)
{
    Close();
}

/**
 * Opens (creates) an output file using an already open file handle.
 *
 * @returns VBox status code.
 * @param   a_pszFilename   Name of the file the file handle points at.
 * @param   a_phFile        Pointer to open file handle to use.
 * @param   a_enmAudioCodec Audio codec to use.
 * @param   a_enmVideoCodec Video codec to use.
 */
int WebMWriter::OpenEx(const char *a_pszFilename, PRTFILE a_phFile,
                       RecordingAudioCodec_T a_enmAudioCodec, RecordingVideoCodec_T a_enmVideoCodec)
{
    try
    {
        LogFunc(("Creating '%s'\n", a_pszFilename));

        int vrc = createEx(a_pszFilename, a_phFile);
        if (RT_SUCCESS(vrc))
        {
            vrc = init(a_enmAudioCodec, a_enmVideoCodec);
            if (RT_SUCCESS(vrc))
                vrc = writeHeader();
        }
    }
    catch(int vrc)
    {
        return vrc;
    }
    return VINF_SUCCESS;
}

/**
 * Opens an output file.
 *
 * @returns VBox status code.
 * @param   a_pszFilename   Name of the file to create.
 * @param   a_fOpen         File open mode of type RTFILE_O_.
 * @param   a_enmAudioCodec Audio codec to use.
 * @param   a_enmVideoCodec Video codec to use.
 */
int WebMWriter::Open(const char *a_pszFilename, uint64_t a_fOpen,
                     RecordingAudioCodec_T a_enmAudioCodec, RecordingVideoCodec_T a_enmVideoCodec)
{
    try
    {
        LogFunc(("Creating '%s'\n", a_pszFilename));

        int vrc = create(a_pszFilename, a_fOpen);
        if (RT_SUCCESS(vrc))
        {
            vrc = init(a_enmAudioCodec, a_enmVideoCodec);
            if (RT_SUCCESS(vrc))
                vrc = writeHeader();
        }
    }
    catch(int vrc)
    {
        return vrc;
    }
    return VINF_SUCCESS;
}

/**
 * Closes the WebM file and drains all queues.
 *
 * @returns VBox status code.
 */
int WebMWriter::Close(void)
{
    LogFlowFuncEnter();

    if (!isOpen())
        return VINF_SUCCESS;

    /* Make sure to drain all queues. */
    processQueue(&m_CurSeg.m_queueBlocks, true /* fForce */);

    writeFooter();

    WebMTracks::iterator itTrack = m_CurSeg.m_mapTracks.begin();
    while (itTrack != m_CurSeg.m_mapTracks.end())
    {
        WebMTrack *pTrack = itTrack->second;
        if (pTrack) /* Paranoia. */
            delete pTrack;

        m_CurSeg.m_mapTracks.erase(itTrack);

        itTrack = m_CurSeg.m_mapTracks.begin();
    }

    Assert(m_CurSeg.m_queueBlocks.Map.size() == 0);
    Assert(m_CurSeg.m_mapTracks.size() == 0);

    com::Utf8Str strFileName = getFileName().c_str();

    close();

    return VINF_SUCCESS;
}

/**
 * Adds an audio track.
 *
 * @returns VBox status code.
 * @param   pCodec          Audio codec to use.
 * @param   uHz             Input sampling rate.
 *                          Must be supported by the selected audio codec.
 * @param   cChannels       Number of input audio channels.
 * @param   cBits           Number of input bits per channel.
 * @param   puTrack         Track number on successful creation. Optional.
 */
int WebMWriter::AddAudioTrack(PRECORDINGCODEC pCodec, uint16_t uHz, uint8_t cChannels, uint8_t cBits, uint8_t *puTrack)
{
    AssertReturn(uHz,       VERR_INVALID_PARAMETER);
    AssertReturn(cBits,     VERR_INVALID_PARAMETER);
    AssertReturn(cChannels, VERR_INVALID_PARAMETER);

    /* Some players (e.g. Firefox with Nestegg) rely on track numbers starting at 1.
     * Using a track number 0 will show those files as being corrupted. */
    const uint8_t uTrack = (uint8_t)m_CurSeg.m_mapTracks.size() + 1;

    subStart(MkvElem_TrackEntry);

    serializeUnsignedInteger(MkvElem_TrackNumber, (uint8_t)uTrack);
    serializeString         (MkvElem_Language,    "und" /* "Undefined"; see ISO-639-2. */);
    serializeUnsignedInteger(MkvElem_FlagLacing,  (uint8_t)0);

    int vrc = VINF_SUCCESS;

    WebMTrack *pTrack = NULL;
    try
    {
        pTrack = new WebMTrack(WebMTrackType_Audio, pCodec, uTrack, RTFileTell(getFile()));
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(vrc))
    {
        serializeUnsignedInteger(MkvElem_TrackUID,     pTrack->uUUID, 4)
              .serializeUnsignedInteger(MkvElem_TrackType,    2 /* Audio */);

        switch (m_enmAudioCodec)
        {
# ifdef VBOX_WITH_LIBVORBIS
            case RecordingAudioCodec_OggVorbis:
            {
                pTrack->Audio.msPerBlock = 0; /** @todo */
                if (!pTrack->Audio.msPerBlock) /* No ms per frame defined? Use default. */
                    pTrack->Audio.msPerBlock = VBOX_RECORDING_VORBIS_FRAME_MS_DEFAULT;

                vorbis_comment vc;
                vorbis_comment_init(&vc);
                vorbis_comment_add_tag(&vc,"ENCODER", vorbis_version_string());

                ogg_packet pkt_ident;
                ogg_packet pkt_comments;
                ogg_packet pkt_setup;
                vorbis_analysis_headerout(&pCodec->Audio.Vorbis.dsp_state, &vc, &pkt_ident, &pkt_comments, &pkt_setup);
                AssertMsgBreakStmt(pkt_ident.bytes <= 255 && pkt_comments.bytes <= 255,
                                   ("Too long header / comment packets\n"), vrc = VERR_INVALID_PARAMETER);

                WEBMOGGVORBISPRIVDATA vorbisPrivData(pkt_ident.bytes, pkt_comments.bytes, pkt_setup.bytes);

                uint8_t *pabHdr = &vorbisPrivData.abHdr[0];
                memcpy(pabHdr, pkt_ident.packet, pkt_ident.bytes);
                pabHdr += pkt_ident.bytes;
                memcpy(pabHdr, pkt_comments.packet, pkt_comments.bytes);
                pabHdr += pkt_comments.bytes;
                memcpy(pabHdr, pkt_setup.packet, pkt_setup.bytes);
                pabHdr += pkt_setup.bytes;

                vorbis_comment_clear(&vc);

                size_t const offHeaders = RT_OFFSETOF(WEBMOGGVORBISPRIVDATA, abHdr);

                serializeString(MkvElem_CodecID,    "A_VORBIS");
                serializeData(MkvElem_CodecPrivate, &vorbisPrivData,
                              offHeaders + pkt_ident.bytes + pkt_comments.bytes + pkt_setup.bytes);
                break;
            }
# endif /* VBOX_WITH_LIBVORBIS */
            default:
                AssertFailedStmt(vrc = VERR_NOT_SUPPORTED); /* Shouldn't ever happen (tm). */
                break;
        }

        if (RT_SUCCESS(vrc))
        {
            serializeUnsignedInteger(MkvElem_CodecDelay,   0)
           .serializeUnsignedInteger(MkvElem_SeekPreRoll,  80 * 1000000) /* 80ms in ns. */
                  .subStart(MkvElem_Audio)
                      .serializeFloat(MkvElem_SamplingFrequency,  (float)uHz)
                      .serializeUnsignedInteger(MkvElem_Channels, cChannels)
                      .serializeUnsignedInteger(MkvElem_BitDepth, cBits)
                  .subEnd(MkvElem_Audio)
                  .subEnd(MkvElem_TrackEntry);

            pTrack->Audio.uHz            = uHz;
            pTrack->Audio.framesPerBlock = uHz / (1000 /* s in ms */ / pTrack->Audio.msPerBlock);

            LogRel2(("Recording: WebM track #%RU8: Audio codec @ %RU16Hz (%RU16ms, %RU16 frames per block)\n",
                     pTrack->uTrack, pTrack->Audio.uHz, pTrack->Audio.msPerBlock, pTrack->Audio.framesPerBlock));

            m_CurSeg.m_mapTracks[uTrack] = pTrack;

            if (puTrack)
                *puTrack = uTrack;

            return VINF_SUCCESS;
        }
    }

    if (pTrack)
        delete pTrack;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Adds a video track.
 *
 * @returns VBox status code.
 * @param   pCodec              Codec data to use.
 * @param   uWidth              Width (in pixels) of the video track.
 * @param   uHeight             Height (in pixels) of the video track.
 * @param   uFPS                FPS (Frames Per Second) of the video track.
 * @param   puTrack             Track number of the added video track on success. Optional.
 */
int WebMWriter::AddVideoTrack(PRECORDINGCODEC pCodec, uint16_t uWidth, uint16_t uHeight, uint32_t uFPS, uint8_t *puTrack)
{
#ifdef VBOX_WITH_LIBVPX
    /* Some players (e.g. Firefox with Nestegg) rely on track numbers starting at 1.
     * Using a track number 0 will show those files as being corrupted. */
    const uint8_t uTrack = (uint8_t)m_CurSeg.m_mapTracks.size() + 1;

    subStart(MkvElem_TrackEntry);

    serializeUnsignedInteger(MkvElem_TrackNumber, (uint8_t)uTrack);
    serializeString         (MkvElem_Language,    "und" /* "Undefined"; see ISO-639-2. */);
    serializeUnsignedInteger(MkvElem_FlagLacing,  (uint8_t)0);

    WebMTrack *pTrack = new WebMTrack(WebMTrackType_Video, pCodec, uTrack, RTFileTell(getFile()));

    /** @todo Resolve codec type. */
    serializeUnsignedInteger(MkvElem_TrackUID,    pTrack->uUUID /* UID */, 4)
          .serializeUnsignedInteger(MkvElem_TrackType,   1 /* Video */)
          .serializeString(MkvElem_CodecID,              "V_VP8")
          .subStart(MkvElem_Video)
              .serializeUnsignedInteger(MkvElem_PixelWidth,  uWidth)
              .serializeUnsignedInteger(MkvElem_PixelHeight, uHeight)
              /* Some players rely on the FPS rate for timing calculations.
               * So make sure to *always* include that. */
              .serializeFloat          (MkvElem_FrameRate,   (float)uFPS)
          .subEnd(MkvElem_Video);

    subEnd(MkvElem_TrackEntry);

    LogRel2(("Recording: WebM track #%RU8: Video\n", pTrack->uTrack));

    m_CurSeg.m_mapTracks[uTrack] = pTrack;

    if (puTrack)
        *puTrack = uTrack;

    return VINF_SUCCESS;
#else
    RT_NOREF(pCodec, uWidth, uHeight, uFPS, puTrack);
    return VERR_NOT_SUPPORTED;
#endif
}

/**
 * Gets file name.
 *
 * @returns File name as UTF-8 string.
 */
const com::Utf8Str& WebMWriter::GetFileName(void)
{
    return getFileName();
}

/**
 * Gets current output file size.
 *
 * @returns File size in bytes.
 */
uint64_t WebMWriter::GetFileSize(void)
{
    return getFileSize();
}

/**
 * Gets current free storage space available for the file.
 *
 * @returns Available storage free space.
 */
uint64_t WebMWriter::GetAvailableSpace(void)
{
    return getAvailableSpace();
}

/**
 * Takes care of the initialization of the instance.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if a given codec is not supported.
 * @param   enmAudioCodec       Audio codec to use.
 * @param   enmVideoCodec       Video codec to use.
 */
int WebMWriter::init(RecordingAudioCodec_T enmAudioCodec, RecordingVideoCodec_T enmVideoCodec)
{
#ifndef VBOX_WITH_LIBVORBIS
    AssertReturn(enmAudioCodec != RecordingAudioCodec_OggVorbis, VERR_NOT_SUPPORTED);
#endif
    AssertReturn(   enmVideoCodec == RecordingVideoCodec_None
                 || enmVideoCodec == RecordingVideoCodec_VP8, VERR_NOT_SUPPORTED);

    m_enmAudioCodec = enmAudioCodec;
    m_enmVideoCodec = enmVideoCodec;

    return m_CurSeg.init();
}

/**
 * Takes care of the destruction of the instance.
 */
void WebMWriter::destroy(void)
{
    m_CurSeg.uninit();
}

/**
 * Writes the WebM file header.
 *
 * @returns VBox status code.
 */
int WebMWriter::writeHeader(void)
{
    LogFunc(("Header @ %RU64\n", RTFileTell(getFile())));

    subStart(MkvElem_EBML)
          .serializeUnsignedInteger(MkvElem_EBMLVersion, 1)
          .serializeUnsignedInteger(MkvElem_EBMLReadVersion, 1)
          .serializeUnsignedInteger(MkvElem_EBMLMaxIDLength, 4)
          .serializeUnsignedInteger(MkvElem_EBMLMaxSizeLength, 8)
          .serializeString(MkvElem_DocType, "webm")
          .serializeUnsignedInteger(MkvElem_DocTypeVersion, 2)
          .serializeUnsignedInteger(MkvElem_DocTypeReadVersion, 2)
          .subEnd(MkvElem_EBML);

    subStart(MkvElem_Segment);

    /* Save offset of current segment. */
    m_CurSeg.m_offStart = RTFileTell(getFile());

    writeSeekHeader();

    /* Save offset of upcoming tracks segment. */
    m_CurSeg.m_offTracks = RTFileTell(getFile());

    /* The tracks segment starts right after this header. */
    subStart(MkvElem_Tracks);
    m_fInTracksSection = true;

    return VINF_SUCCESS;
}

/**
 * Writes a simple block into the EBML structure.
 *
 * @returns VBox status code.
 * @param   a_pTrack        Track the simple block is assigned to.
 * @param   a_pBlock        Simple block to write.
 */
int WebMWriter::writeSimpleBlockEBML(WebMTrack *a_pTrack, WebMSimpleBlock *a_pBlock)
{
#ifdef LOG_ENABLED
    WebMCluster &Cluster = m_CurSeg.m_CurCluster;

    Log3Func(("[T%RU8C%RU64] Off=%RU64, AbsPTSMs=%RU64, RelToClusterMs=%RU16, %zu bytes\n",
              a_pTrack->uTrack, Cluster.uID, RTFileTell(getFile()),
              a_pBlock->Data.tcAbsPTSMs, a_pBlock->Data.tcRelToClusterMs, a_pBlock->Data.cb));
#endif
    /*
     * Write a "Simple Block".
     */
    writeClassId(MkvElem_SimpleBlock);
    /* Block size. */
    writeUnsignedInteger(0x10000000u | (  1                 /* Track number size. */
                                        + m_cbTimecode      /* Timecode size .*/
                                        + 1                 /* Flags size. */
                                        + a_pBlock->Data.cb /* Actual frame data size. */),  4);
    /* Track number. */
    writeSize(a_pTrack->uTrack);
    /* Timecode (relative to cluster opening timecode). */
    writeUnsignedInteger(a_pBlock->Data.tcRelToClusterMs, m_cbTimecode);
    /* Flags. */
    writeUnsignedInteger(a_pBlock->Data.fFlags, 1);
    /* Frame data. */
    write(a_pBlock->Data.pv, a_pBlock->Data.cb);

    return VINF_SUCCESS;
}

/**
 * Writes a simple block and enqueues it into the segment's render queue.
 *
 * @returns VBox status code.
 * @param   a_pTrack        Track the simple block is assigned to.
 * @param   a_pBlock        Simple block to write and enqueue.
 */
int WebMWriter::writeSimpleBlockQueued(WebMTrack *a_pTrack, WebMSimpleBlock *a_pBlock)
{
    RT_NOREF(a_pTrack);

    int vrc = VINF_SUCCESS;

    try
    {
        const WebMTimecodeAbs tcAbsPTS = a_pBlock->Data.tcAbsPTSMs;

        /* See if we already have an entry for the specified timecode in our queue. */
        WebMBlockMap::iterator itQueue = m_CurSeg.m_queueBlocks.Map.find(tcAbsPTS);
        if (itQueue != m_CurSeg.m_queueBlocks.Map.end()) /* Use existing queue. */
        {
            WebMTimecodeBlocks &Blocks = itQueue->second;
            Blocks.Enqueue(a_pBlock);
        }
        else /* Create a new timecode entry. */
        {
            WebMTimecodeBlocks Blocks;
            Blocks.Enqueue(a_pBlock);

            m_CurSeg.m_queueBlocks.Map[tcAbsPTS] = Blocks;
        }

        vrc = processQueue(&m_CurSeg.m_queueBlocks, false /* fForce */);
    }
    catch(...)
    {
        delete a_pBlock;
        a_pBlock = NULL;

        vrc = VERR_NO_MEMORY;
    }

    return vrc;
}

/**
 * Writes a data block to the specified track.
 *
 * @returns VBox status code.
 * @param   uTrack          Track ID to write data to.
 * @param   pvData          Pointer to data block to write.
 * @param   cbData          Size (in bytes) of data block to write.
 * @param   tcAbsPTSMs      Absolute PTS of simple data block.
 * @param   uFlags          WebM block flags to use for this block.
 */
int WebMWriter::WriteBlock(uint8_t uTrack, const void *pvData, size_t cbData, WebMTimecodeAbs tcAbsPTSMs, WebMBlockFlags uFlags)
{
    int vrc = RTCritSectEnter(&m_CurSeg.m_CritSect);
    AssertRC(vrc);

    WebMTracks::iterator itTrack = m_CurSeg.m_mapTracks.find(uTrack);
    if (itTrack == m_CurSeg.m_mapTracks.end())
    {
        RTCritSectLeave(&m_CurSeg.m_CritSect);
        return VERR_NOT_FOUND;
    }

    WebMTrack *pTrack = itTrack->second;
    AssertPtr(pTrack);

    if (m_fInTracksSection)
    {
        subEnd(MkvElem_Tracks);
        m_fInTracksSection = false;
    }

    try
    {
        vrc = writeSimpleBlockQueued(pTrack,
                                     new WebMSimpleBlock(pTrack,
                                                         tcAbsPTSMs, pvData, cbData, uFlags));
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    int vrc2 = RTCritSectLeave(&m_CurSeg.m_CritSect);
    AssertRC(vrc2);

    return vrc;
}

/**
 * Processes a render queue.
 *
 * @returns VBox status code.
 * @param   pQueue          Queue to process.
 * @param   fForce          Whether forcing to process the render queue or not.
 *                          Needed to drain the queues when terminating.
 */
int WebMWriter::processQueue(WebMQueue *pQueue, bool fForce)
{
    if (pQueue->tsLastProcessedMs == 0)
        pQueue->tsLastProcessedMs = RTTimeMilliTS();

    if (!fForce)
    {
        /* Only process when we reached a certain threshold. */
        if (RTTimeMilliTS() - pQueue->tsLastProcessedMs < 5000 /* ms */ /** @todo Make this configurable */)
            return VINF_SUCCESS;
    }

    WebMCluster &Cluster = m_CurSeg.m_CurCluster;

    /* Iterate through the block map. */
    WebMBlockMap::iterator it = pQueue->Map.begin();
    while (it != m_CurSeg.m_queueBlocks.Map.end())
    {
        WebMTimecodeAbs    mapAbsPTSMs = it->first;
        WebMTimecodeBlocks mapBlocks   = it->second;

        /* Whether to start a new cluster or not. */
        bool fClusterStart = false;

        /* If the current segment does not have any clusters (yet),
         * take the first absolute PTS as the starting point for that segment. */
        if (m_CurSeg.m_cClusters == 0)
        {
            m_CurSeg.m_tcAbsStartMs = mapAbsPTSMs;
            fClusterStart = true;
        }

        /* Determine if we need to start a new cluster. */
            /* No blocks written yet? Start a new cluster. */
        if (   Cluster.cBlocks == 0
            /* Did we reach the maximum a cluster can hold? Use a new cluster then. */
            || mapAbsPTSMs - Cluster.tcAbsStartMs > VBOX_WEBM_CLUSTER_MAX_LEN_MS
            /* If the block map indicates that a cluster is needed for this timecode, create one. */
            || mapBlocks.fClusterNeeded)
        {
            fClusterStart = true;
        }

        if (   fClusterStart
            && !mapBlocks.fClusterStarted)
        {
            /* Last written timecode of the current cluster. */
            uint64_t tcAbsClusterLastWrittenMs;

            if (Cluster.fOpen) /* Close current cluster first. */
            {
                Log2Func(("[C%RU64] End @ %RU64ms (duration = %RU64ms)\n",
                          Cluster.uID, Cluster.tcAbsLastWrittenMs, Cluster.tcAbsLastWrittenMs - Cluster.tcAbsStartMs));

                /* Make sure that the current cluster contained some data. */
                Assert(Cluster.offStart);
                Assert(Cluster.cBlocks);

                /* Save the last written timecode of the current cluster before closing it. */
                tcAbsClusterLastWrittenMs = Cluster.tcAbsLastWrittenMs;

                subEnd(MkvElem_Cluster);
                Cluster.fOpen = false;
            }
            else /* First cluster ever? Use the segment's starting timecode. */
                tcAbsClusterLastWrittenMs = m_CurSeg.m_tcAbsStartMs;

            Cluster.fOpen              = true;
            Cluster.uID                = m_CurSeg.m_cClusters;
            /* Use the block map's currently processed TC as the cluster's starting TC. */
            Cluster.tcAbsStartMs       = mapAbsPTSMs;
            Cluster.tcAbsLastWrittenMs = Cluster.tcAbsStartMs;
            Cluster.offStart           = RTFileTell(getFile());
            Cluster.cBlocks            = 0;

            AssertMsg(Cluster.tcAbsStartMs <= mapAbsPTSMs,
                      ("Cluster #%RU64 start TC (%RU64) must not bigger than the block map's currently processed TC (%RU64)\n",
                       Cluster.uID, Cluster.tcAbsStartMs, mapAbsPTSMs));

            Log2Func(("[C%RU64] Start @ %RU64ms (map TC is %RU64) / %RU64 bytes\n",
                      Cluster.uID, Cluster.tcAbsStartMs, mapAbsPTSMs, Cluster.offStart));

            /* Insert cue points for all tracks if a new cluster has been started. */
            WebMCuePoint *pCuePoint = new WebMCuePoint(Cluster.tcAbsStartMs);

            WebMTracks::iterator itTrack = m_CurSeg.m_mapTracks.begin();
            while (itTrack != m_CurSeg.m_mapTracks.end())
            {
                pCuePoint->Pos[itTrack->first] = new WebMCueTrackPosEntry(Cluster.offStart);
                ++itTrack;
            }

            m_CurSeg.m_lstCuePoints.push_back(pCuePoint);

            subStart(MkvElem_Cluster)
                .serializeUnsignedInteger(MkvElem_Timecode, Cluster.tcAbsStartMs - m_CurSeg.m_tcAbsStartMs);

            m_CurSeg.m_cClusters++;

            mapBlocks.fClusterStarted = true;
        }

        Log2Func(("[C%RU64] SegTcAbsStartMs=%RU64, ClusterTcAbsStartMs=%RU64, ClusterTcAbsLastWrittenMs=%RU64, mapAbsPTSMs=%RU64\n",
                   Cluster.uID, m_CurSeg.m_tcAbsStartMs, Cluster.tcAbsStartMs, Cluster.tcAbsLastWrittenMs, mapAbsPTSMs));

        /* Iterate through all blocks related to the current timecode. */
        while (!mapBlocks.Queue.empty())
        {
            WebMSimpleBlock *pBlock = mapBlocks.Queue.front();
            AssertPtr(pBlock);

            WebMTrack       *pTrack = pBlock->pTrack;
            AssertPtr(pTrack);

            /* Calculate the block's relative time code to the current cluster's starting time code. */
            Assert(pBlock->Data.tcAbsPTSMs >= Cluster.tcAbsStartMs);
            pBlock->Data.tcRelToClusterMs = pBlock->Data.tcAbsPTSMs - Cluster.tcAbsStartMs;

            int vrc2 = writeSimpleBlockEBML(pTrack, pBlock);
            AssertRC(vrc2);

            Cluster.cBlocks++;
            Cluster.tcAbsLastWrittenMs = pBlock->Data.tcAbsPTSMs;

            pTrack->cTotalBlocks++;
            pTrack->tcAbsLastWrittenMs = Cluster.tcAbsLastWrittenMs;

            if (m_CurSeg.m_tcAbsLastWrittenMs < pTrack->tcAbsLastWrittenMs)
                m_CurSeg.m_tcAbsLastWrittenMs = pTrack->tcAbsLastWrittenMs;

            /* Save a cue point if this is a keyframe (if no new cluster has been started,
             * as this implies that a cue point already is present. */
            if (   !fClusterStart
                && (pBlock->Data.fFlags & VBOX_WEBM_BLOCK_FLAG_KEY_FRAME))
            {
                /* Insert cue points for all tracks if a new cluster has been started. */
                WebMCuePoint *pCuePoint = new WebMCuePoint(Cluster.tcAbsLastWrittenMs);

                WebMTracks::iterator itTrack = m_CurSeg.m_mapTracks.begin();
                while (itTrack != m_CurSeg.m_mapTracks.end())
                {
                    pCuePoint->Pos[itTrack->first] = new WebMCueTrackPosEntry(Cluster.offStart);
                    ++itTrack;
                }

                m_CurSeg.m_lstCuePoints.push_back(pCuePoint);
            }

            delete pBlock;
            pBlock = NULL;

            mapBlocks.Queue.pop();
        }

        Assert(mapBlocks.Queue.empty());

        m_CurSeg.m_queueBlocks.Map.erase(it);

        it = m_CurSeg.m_queueBlocks.Map.begin();
    }

    Assert(m_CurSeg.m_queueBlocks.Map.empty());

    pQueue->tsLastProcessedMs = RTTimeMilliTS();

    return VINF_SUCCESS;
}

/**
 * Writes the WebM footer.
 *
 * @returns VBox status code.
 */
int WebMWriter::writeFooter(void)
{
    AssertReturn(isOpen(), VERR_WRONG_ORDER);

    if (m_fInTracksSection)
    {
        subEnd(MkvElem_Tracks);
        m_fInTracksSection = false;
    }

    if (m_CurSeg.m_CurCluster.fOpen)
    {
        subEnd(MkvElem_Cluster);
        m_CurSeg.m_CurCluster.fOpen = false;
    }

    /*
     * Write Cues element.
     */
    m_CurSeg.m_offCues = RTFileTell(getFile());
    LogFunc(("Cues @ %RU64\n", m_CurSeg.m_offCues));

    subStart(MkvElem_Cues);

    WebMCuePointList::iterator itCuePoint = m_CurSeg.m_lstCuePoints.begin();
    while (itCuePoint != m_CurSeg.m_lstCuePoints.end())
    {
        WebMCuePoint *pCuePoint = (*itCuePoint);
        AssertPtr(pCuePoint);

        LogFunc(("CuePoint @ %RU64: %zu tracks, tcAbs=%RU64)\n",
                 RTFileTell(getFile()), pCuePoint->Pos.size(), pCuePoint->tcAbs));

        subStart(MkvElem_CuePoint)
            .serializeUnsignedInteger(MkvElem_CueTime, pCuePoint->tcAbs);

            WebMCueTrackPosMap::iterator itTrackPos = pCuePoint->Pos.begin();
            while (itTrackPos != pCuePoint->Pos.end())
            {
                WebMCueTrackPosEntry *pTrackPos = itTrackPos->second;
                AssertPtr(pTrackPos);

                LogFunc(("TrackPos (track #%RU32) @ %RU64, offCluster=%RU64)\n",
                         itTrackPos->first, RTFileTell(getFile()), pTrackPos->offCluster));

                subStart(MkvElem_CueTrackPositions)
                    .serializeUnsignedInteger(MkvElem_CueTrack,           itTrackPos->first)
                    .serializeUnsignedInteger(MkvElem_CueClusterPosition, pTrackPos->offCluster - m_CurSeg.m_offStart, 8)
                    .subEnd(MkvElem_CueTrackPositions);

                ++itTrackPos;
            }

        subEnd(MkvElem_CuePoint);

        ++itCuePoint;
    }

    subEnd(MkvElem_Cues);
    subEnd(MkvElem_Segment);

    /*
     * Re-Update seek header with final information.
     */

    writeSeekHeader();

    return RTFileSeek(getFile(), 0, RTFILE_SEEK_END, NULL);
}

/**
 * Writes the segment's seek header.
 */
void WebMWriter::writeSeekHeader(void)
{
    if (m_CurSeg.m_offSeekInfo)
        RTFileSeek(getFile(), m_CurSeg.m_offSeekInfo, RTFILE_SEEK_BEGIN, NULL);
    else
        m_CurSeg.m_offSeekInfo = RTFileTell(getFile());

    LogFunc(("Seek Header @ %RU64\n", m_CurSeg.m_offSeekInfo));

    subStart(MkvElem_SeekHead);

    subStart(MkvElem_Seek)
          .serializeUnsignedInteger(MkvElem_SeekID, MkvElem_Tracks)
          .serializeUnsignedInteger(MkvElem_SeekPosition, m_CurSeg.m_offTracks - m_CurSeg.m_offStart, 8)
          .subEnd(MkvElem_Seek);

    if (m_CurSeg.m_offCues)
        LogFunc(("Updating Cues @ %RU64\n", m_CurSeg.m_offCues));

    subStart(MkvElem_Seek)
          .serializeUnsignedInteger(MkvElem_SeekID, MkvElem_Cues)
          .serializeUnsignedInteger(MkvElem_SeekPosition, m_CurSeg.m_offCues - m_CurSeg.m_offStart, 8)
          .subEnd(MkvElem_Seek);

    subStart(MkvElem_Seek)
          .serializeUnsignedInteger(MkvElem_SeekID, MkvElem_Info)
          .serializeUnsignedInteger(MkvElem_SeekPosition, m_CurSeg.m_offInfo - m_CurSeg.m_offStart, 8)
          .subEnd(MkvElem_Seek);

    subEnd(MkvElem_SeekHead);

    /*
     * Write the segment's info element.
     */

    /* Save offset of the segment's info element. */
    m_CurSeg.m_offInfo = RTFileTell(getFile());

    LogFunc(("Info @ %RU64\n", m_CurSeg.m_offInfo));

    char szMux[64];
    RTStrPrintf(szMux, sizeof(szMux),
#ifdef VBOX_WITH_LIBVPX
                 "vpxenc%s", vpx_codec_version_str()
#else
                 "unknown"
#endif
                );
    char szApp[64];
    RTStrPrintf(szApp, sizeof(szApp), VBOX_PRODUCT " %sr%u", VBOX_VERSION_STRING, RTBldCfgRevision());

    const WebMTimecodeAbs tcAbsDurationMs = m_CurSeg.m_tcAbsLastWrittenMs - m_CurSeg.m_tcAbsStartMs;

    if (!m_CurSeg.m_lstCuePoints.empty())
    {
        LogFunc(("tcAbsDurationMs=%RU64\n", tcAbsDurationMs));
        AssertMsg(tcAbsDurationMs, ("Segment seems to be empty (duration is 0)\n"));
    }

    subStart(MkvElem_Info)
        .serializeUnsignedInteger(MkvElem_TimecodeScale, m_CurSeg.m_uTimecodeScaleFactor)
        .serializeFloat(MkvElem_Segment_Duration, tcAbsDurationMs)
        .serializeString(MkvElem_MuxingApp, szMux)
        .serializeString(MkvElem_WritingApp, szApp)
        .subEnd(MkvElem_Info);
}
