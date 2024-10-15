/* $Id: WebMWriter.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * WebMWriter.h - WebM container handling.
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

#ifndef MAIN_INCLUDED_WebMWriter_h
#define MAIN_INCLUDED_WebMWriter_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/buildconfig.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/string.h>

#include "VBox/com/VirtualBox.h"
#include <VBox/version.h>

#include "EBMLWriter.h"
#include "EBML_MKV.h"

#include <queue>
#include <map>
#include <list>

#ifdef VBOX_WITH_LIBVPX
# ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable: 4668) /* vpx_codec.h(64) : warning C4668: '__GNUC__' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif' */
#  include <vpx/vpx_encoder.h>
#  pragma warning(pop)
# else
#  include <vpx/vpx_encoder.h>
# endif
#endif /* VBOX_WITH_LIBVPX */

/** No flags specified. */
#define VBOX_WEBM_BLOCK_FLAG_NONE           0
/** Invisible block which can be skipped. */
#define VBOX_WEBM_BLOCK_FLAG_INVISIBLE      0x08
/** The block marks a key frame. */
#define VBOX_WEBM_BLOCK_FLAG_KEY_FRAME      0x80

/** The default timecode scale factor for WebM -- all timecodes in the segments are expressed in ms.
 *  This allows every cluster to have blocks with positive values up to 32.767 seconds. */
#define VBOX_WEBM_TIMECODESCALE_FACTOR_MS   1000000

/** Maximum time (in ms) a cluster can store. */
#define VBOX_WEBM_CLUSTER_MAX_LEN_MS        INT16_MAX

/** Maximum time a block can store.
 *  With signed 16-bit timecodes and a default timecode scale of 1ms per unit this makes 65536ms. */
#define VBOX_WEBM_BLOCK_MAX_LEN_MS          UINT16_MAX

#ifdef VBOX_WITH_LIBVORBIS
# pragma pack(push)
# pragma pack(1)
    /** Ogg Vorbis codec private data within the MKV (WEBM) container.
     *  Taken from: https://www.matroska.org/technical/codec_specs.html */
    typedef struct WEBMOGGVORBISPRIVDATA
    {
        WEBMOGGVORBISPRIVDATA(uint32_t a_cbHdrIdent, uint32_t a_cbHdrComments, uint32_t a_cbHdrSetup)
            : cbHdrIdent(a_cbHdrIdent)
            , cbHdrComments(a_cbHdrComments)
        {
            RT_NOREF(a_cbHdrSetup);

            /* We supply 3 headers total: The "real" header, comments header + setup header. */
            cHeaders = 3 /* Headers */ - 1; /* Note: Always "minus one" here. */

            Assert(a_cbHdrIdent    <= UINT8_MAX);
            Assert(a_cbHdrComments <= UINT8_MAX);
            Assert(a_cbHdrSetup    <= _8K);
            Assert(a_cbHdrIdent + a_cbHdrComments + a_cbHdrSetup <= sizeof(abHdr));
        }

        /** Number of private headers - 1. */
        uint8_t  cHeaders;
        /** Size of identification header (in bytes). */
        uint8_t  cbHdrIdent;
        /** < Size of comments header (in bytes). */
        uint8_t  cbHdrComments;
        /** < Header code area. */
        uint8_t  abHdr[UINT8_MAX /* Header */ + UINT8_MAX /* Comments header */ + _8K /* Setup header */];

    } WEBMOGGVORBISPRIVDATA, *PWEBMOGGVORBISPRIVDATA;
# pragma pack(pop)
#endif

class WebMWriter : public EBMLWriter
{

public:

    /** Defines an absolute WebM timecode (Block + Cluster). */
    typedef uint64_t WebMTimecodeAbs;

    /** Defines a relative WebM timecode (Block). */
    typedef uint16_t WebMTimecodeRel;

    /** Defines the WebM block flags data type. */
    typedef uint8_t  WebMBlockFlags;

    /**
     * Track type enumeration.
     */
    enum WebMTrackType
    {
        /** Unknown / invalid type. */
        WebMTrackType_Invalid     = 0,
        /** Only writes audio. */
        WebMTrackType_Audio       = 1,
        /** Only writes video. */
        WebMTrackType_Video       = 2
    };

    struct WebMTrack;

    /**
     * Structure for defining a WebM simple block.
     */
    struct WebMSimpleBlock
    {
        WebMSimpleBlock(WebMTrack *a_pTrack,
                        WebMTimecodeAbs a_tcAbsPTSMs, const void *a_pvData, size_t a_cbData, WebMBlockFlags a_fFlags)
            : pTrack(a_pTrack)
        {
            Data.tcAbsPTSMs = a_tcAbsPTSMs;
            Data.cb         = a_cbData;
            Data.fFlags     = a_fFlags;

            if (Data.cb)
            {
                Data.pv = RTMemDup(a_pvData, a_cbData);
                if (!Data.pv)
                    throw;
            }
        }

        virtual ~WebMSimpleBlock()
        {
            if (Data.pv)
            {
                Assert(Data.cb);
                RTMemFree(Data.pv);
            }
        }

        WebMTrack    *pTrack;

        /** Actual simple block data. */
        struct
        {
            WebMTimecodeAbs tcAbsPTSMs;
            WebMTimecodeRel tcRelToClusterMs;
            void          *pv;
            size_t         cb;
            WebMBlockFlags fFlags;
        } Data;
    };

    /** A simple block queue.*/
    typedef std::queue<WebMSimpleBlock *> WebMSimpleBlockQueue;

    /** Structure for queuing all simple blocks bound to a single timecode.
     *  This can happen if multiple tracks are being involved. */
    struct WebMTimecodeBlocks
    {
        WebMTimecodeBlocks(void)
            : fClusterNeeded(false)
            , fClusterStarted(false) { }

        /** The actual block queue for this timecode. */
        WebMSimpleBlockQueue Queue;
        /** Whether a new cluster is needed for this timecode or not. */
        bool                 fClusterNeeded;
        /** Whether a new cluster already has been started for this timecode or not. */
        bool                 fClusterStarted;

        /**
         * Enqueues a simple block into the internal queue.
         *
         * @param   a_pBlock    Block to enqueue and take ownership of.
         */
        void Enqueue(WebMSimpleBlock *a_pBlock)
        {
            Queue.push(a_pBlock);

            if (a_pBlock->Data.fFlags & VBOX_WEBM_BLOCK_FLAG_KEY_FRAME)
                fClusterNeeded = true;
        }
    };

    /** A block map containing all currently queued blocks.
     *  The key specifies a unique timecode, whereas the value
     *  is a queue of blocks which all correlate to the key (timecode). */
    typedef std::map<WebMTimecodeAbs, WebMTimecodeBlocks> WebMBlockMap;

    /**
     * Structure for defining a WebM (encoding) queue.
     */
    struct WebMQueue
    {
        WebMQueue(void)
            : tcAbsLastBlockWrittenMs(0)
            , tsLastProcessedMs(0) { }

        /** Blocks as FIFO (queue). */
        WebMBlockMap    Map;
        /** Absolute timecode (in ms) of last written block to queue. */
        WebMTimecodeAbs tcAbsLastBlockWrittenMs;
        /** Time stamp (in ms) of when the queue was processed last. */
        uint64_t        tsLastProcessedMs;
    };

    /**
     * Structure for keeping a WebM track entry.
     */
    struct WebMTrack
    {
        WebMTrack(WebMTrackType a_enmType, PRECORDINGCODEC pTheCodec, uint8_t a_uTrack, uint64_t a_offID)
            : enmType(a_enmType)
            , pCodec(pTheCodec)
            , uTrack(a_uTrack)
            , offUUID(a_offID)
            , cTotalBlocks(0)
            , tcAbsLastWrittenMs(0)
        {
            uUUID = RTRandU32();
        }

        /** The type of this track. */
        WebMTrackType   enmType;
        /** Pointer to codec data to use. */
        PRECORDINGCODEC pCodec;
        /** Track parameters. */
        union
        {
            struct
            {
                /** Sample rate of input data. */
                uint32_t uHz;
                /** Duration of the frame in samples (per channel).
                 *  Valid frame size are:
                 *
                 *  ms           Frame size
                 *  2.5          120
                 *  5            240
                 *  10           480
                 *  20 (Default) 960
                 *  40           1920
                 *  60           2880
                 */
                uint16_t framesPerBlock;
                /** How many milliseconds (ms) one written (simple) block represents. */
                uint16_t msPerBlock;
            } Audio;
        };
        /** This track's track number. Also used as key in track map. */
        uint8_t             uTrack;
        /** The track's "UUID".
         *  Needed in case this track gets mux'ed with tracks from other files. Not really unique though. */
        uint32_t            uUUID;
        /** Absolute offset in file of track UUID.
         *  Needed to write the hash sum within the footer. */
        uint64_t            offUUID;
        /** Total number of blocks. */
        uint64_t            cTotalBlocks;
        /** Absoute timecode (in ms) of last write. */
        WebMTimecodeAbs     tcAbsLastWrittenMs;
    };

    /**
     * Structure for a single cue point track position entry.
     */
    struct WebMCueTrackPosEntry
    {
        WebMCueTrackPosEntry(uint64_t a_offCluster)
            : offCluster(a_offCluster) { }

        /** Offset (in bytes) of the related cluster containing the given position. */
        uint64_t offCluster;
    };

    /** Map for keeping track position entries for a single cue point.
     *  The key is the track number (*not* UUID!). */
    typedef std::map<uint8_t, WebMCueTrackPosEntry *> WebMCueTrackPosMap;

    /**
     * Structure for keeping a cue point.
     */
    struct WebMCuePoint
    {
        WebMCuePoint(WebMTimecodeAbs a_tcAbs)
            : tcAbs(a_tcAbs) { }

        virtual ~WebMCuePoint()
        {
            Clear();
        }

        void Clear(void)
        {
            WebMCueTrackPosMap::iterator itTrackPos = Pos.begin();
            while (itTrackPos != Pos.end())
            {
                WebMCueTrackPosEntry *pTrackPos = itTrackPos->second;
                AssertPtr(pTrackPos);
                delete pTrackPos;

                Pos.erase(itTrackPos);
                itTrackPos = Pos.begin();
            }

            Assert(Pos.empty());
        }

        /** Map containing all track positions for this specific cue point. */
        WebMCueTrackPosMap Pos;
        /** Absolute time code according to the segment time base. */
        WebMTimecodeAbs    tcAbs;
    };

    /** List of cue points. */
    typedef std::list<WebMCuePoint *> WebMCuePointList;

    /**
     * Structure for keeping a WebM cluster entry.
     */
    struct WebMCluster
    {
        WebMCluster(void)
            : uID(0)
            , offStart(0)
            , fOpen(false)
            , tcAbsStartMs(0)
            , cBlocks(0) { }

        /** This cluster's ID. */
        uint64_t        uID;
        /** Absolute offset (in bytes) of this cluster.
         *  Needed for seeking info table. */
        uint64_t        offStart;
        /** Whether this cluster element is opened currently. */
        bool            fOpen;
        /** Absolute timecode (in ms) when this cluster starts. */
        WebMTimecodeAbs tcAbsStartMs;
        /** Absolute timecode (in ms) of when last written to this cluster. */
        WebMTimecodeAbs tcAbsLastWrittenMs;
        /** Number of (simple) blocks in this cluster. */
        uint64_t        cBlocks;
    };

    /**
     * Structure for keeping a WebM segment entry.
     *
     * Current we're only using one segment.
     */
    struct WebMSegment
    {
        WebMSegment(void)
            : m_tcAbsStartMs(0)
            , m_tcAbsLastWrittenMs(0)
            , m_offStart(0)
            , m_offInfo(0)
            , m_offSeekInfo(0)
            , m_offTracks(0)
            , m_offCues(0)
            , m_cClusters(0)
        {
            m_uTimecodeScaleFactor = VBOX_WEBM_TIMECODESCALE_FACTOR_MS;

            LogFunc(("Default timecode scale is: %RU64ns\n", m_uTimecodeScaleFactor));
        }

        virtual ~WebMSegment()
        {
            uninit();
        }

        /**
         * Initializes a segment.
         *
         * @returns VBox status code.
         */
        int init(void)
        {
            return RTCritSectInit(&m_CritSect);
        }

        /**
         * Uninitializes a segment.
         */
        void uninit(void)
        {
            clear();

            RTCritSectDelete(&m_CritSect);
        }

        /**
         * Clear the segment's data by removing (and freeing) all data.
         */
        void clear(void)
        {
            WebMCuePointList::iterator itCuePoint = m_lstCuePoints.begin();
            while (itCuePoint != m_lstCuePoints.end())
            {
                WebMCuePoint *pCuePoint = (*itCuePoint);
                AssertPtr(pCuePoint);
                delete pCuePoint;

                m_lstCuePoints.erase(itCuePoint);
                itCuePoint = m_lstCuePoints.begin();
            }

            Assert(m_lstCuePoints.empty());
        }

        /** Critical section for serializing access to this segment. */
        RTCRITSECT                      m_CritSect;

        /** The timecode scale factor of this segment. */
        uint64_t                        m_uTimecodeScaleFactor;

        /** Absolute timecode (in ms) when starting this segment. */
        WebMTimecodeAbs                 m_tcAbsStartMs;
        /** Absolute timecode (in ms) of last write. */
        WebMTimecodeAbs                 m_tcAbsLastWrittenMs;

        /** Absolute offset (in bytes) of CurSeg. */
        uint64_t                        m_offStart;
        /** Absolute offset (in bytes) of general info. */
        uint64_t                        m_offInfo;
        /** Absolute offset (in bytes) of seeking info. */
        uint64_t                        m_offSeekInfo;
        /** Absolute offset (in bytes) of tracks. */
        uint64_t                        m_offTracks;
        /** Absolute offset (in bytes) of cues table. */
        uint64_t                        m_offCues;
        /** List of cue points. Needed for seeking table. */
        WebMCuePointList                m_lstCuePoints;

        /** Total number of clusters. */
        uint64_t                        m_cClusters;

        /** Map of tracks.
         *  The key marks the track number (*not* the UUID!). */
        std::map <uint8_t, WebMTrack *> m_mapTracks;

        /** Current cluster which is being handled.
         *
         *  Note that we don't need (and shouldn't need, as this can be a *lot* of data!) a
         *  list of all clusters. */
        WebMCluster                     m_CurCluster;

        WebMQueue                       m_queueBlocks;

    } m_CurSeg;

    /** Audio codec to use. */
    RecordingAudioCodec_T       m_enmAudioCodec;
    /** Video codec to use. */
    RecordingVideoCodec_T       m_enmVideoCodec;

    /** Whether we're currently in the tracks section. */
    bool                        m_fInTracksSection;

    /** Size of timecodes (in bytes). */
    size_t                      m_cbTimecode;
    /** Maximum value a timecode can have. */
    uint32_t                    m_uTimecodeMax;

#ifdef VBOX_WITH_LIBVPX
    /**
     * Block data for VP8-encoded video data.
     */
    struct BlockData_VP8
    {
        const vpx_codec_enc_cfg_t *pCfg;
        const vpx_codec_cx_pkt_t  *pPkt;
    };
#endif /* VBOX_WITH_LIBVPX */

    /**
     * Block data for encoded audio data.
     */
    struct BlockData_Audio
    {
        /** Pointer to encoded audio data. */
        const void *pvData;
        /** Size (in bytes) of encoded audio data. */
        size_t      cbData;
        /** PTS (in ms) of encoded audio data. */
        uint64_t    uPTSMs;
    };

public:

    WebMWriter();

    virtual ~WebMWriter();

public:

    int OpenEx(const char *a_pszFilename, PRTFILE a_phFile,
               RecordingAudioCodec_T a_enmAudioCodec, RecordingVideoCodec_T a_enmVideoCodec);

    int Open(const char *a_pszFilename, uint64_t a_fOpen,
             RecordingAudioCodec_T a_enmAudioCodec, RecordingVideoCodec_T a_enmVideoCodec);

    int Close(void);

    int AddAudioTrack(PRECORDINGCODEC pCodec, uint16_t uHz, uint8_t cChannels, uint8_t cBits, uint8_t *puTrack);

    int AddVideoTrack(PRECORDINGCODEC pCodec, uint16_t uWidth, uint16_t uHeight, uint32_t uFPS, uint8_t *puTrack);

    int WriteBlock(uint8_t uTrack, const void *pvData, size_t cbData, WebMTimecodeAbs tcAbsPTSMs, WebMBlockFlags uFlags);

    const com::Utf8Str& GetFileName(void);

    uint64_t GetFileSize(void);

    uint64_t GetAvailableSpace(void);

    /**
     * Returns the number of written WebM clusters.
     *
     * @returns Number of written WebM clusters; 0 when no clusters written (empty file).
     */
    uint64_t GetClusters(void) const { return m_CurSeg.m_cClusters; }

protected:

    int init(RecordingAudioCodec_T a_enmAudioCodec, RecordingVideoCodec_T a_enmVideoCodec);

    void destroy(void);

    int writeHeader(void);

    void writeSeekHeader(void);

    int writeFooter(void);

    int writeSimpleBlockEBML(WebMTrack *a_pTrack, WebMSimpleBlock *a_pBlock);

    int writeSimpleBlockQueued(WebMTrack *a_pTrack, WebMSimpleBlock *a_pBlock);

    int processQueue(WebMQueue *pQueue, bool fForce);

protected:

    typedef std::map <uint8_t, WebMTrack *> WebMTracks;
};

#endif /* !MAIN_INCLUDED_WebMWriter_h */
