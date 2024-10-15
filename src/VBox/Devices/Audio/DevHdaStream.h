/* $Id: DevHdaStream.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Intel HD Audio Controller Emulation - Streams.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Audio_DevHdaStream_h
#define VBOX_INCLUDED_SRC_Audio_DevHdaStream_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_INCLUDED_SRC_Audio_DevHda_h
# error "Only include DevHda.h!"
#endif


/**
 * Structure containing HDA stream debug stuff, configurable at runtime.
 */
typedef struct HDASTREAMDEBUGRT
{
    /** Whether debugging is enabled or not. */
    bool                     fEnabled;
    uint8_t                  Padding[7];
    /** File for dumping stream reads / writes.
     *  For input streams, this dumps data being written to the device FIFO,
     *  whereas for output streams this dumps data being read from the device FIFO. */
    R3PTRTYPE(PAUDIOHLPFILE) pFileStream;
    /** File for dumping raw DMA reads / writes.
     *  For input streams, this dumps data being written to the device DMA,
     *  whereas for output streams this dumps data being read from the device DMA. */
    R3PTRTYPE(PAUDIOHLPFILE) pFileDMARaw;
    /** File for dumping mapped (that is, extracted) DMA reads / writes. */
    R3PTRTYPE(PAUDIOHLPFILE) pFileDMAMapped;
} HDASTREAMDEBUGRT;

/**
 * Structure containing HDA stream debug information.
 */
typedef struct HDASTREAMDEBUG
{
    /** Runtime debug info. */
    HDASTREAMDEBUGRT        Runtime;
    uint64_t                au64Alignment[2];
} HDASTREAMDEBUG;

/**
 * Internal state of a HDA stream.
 */
typedef struct HDASTREAMSTATE
{
    /** Flag indicating whether this stream currently is
     *  in reset mode and therefore not acccessible by the guest. */
    volatile bool           fInReset;
    /** Flag indicating if the stream is in running state or not. */
    volatile bool           fRunning;
    /** How many interrupts are pending due to
     *  BDLE interrupt-on-completion (IOC) bits set. */
    uint8_t                 cTransferPendingInterrupts;
    /** Input streams only: Set when we switch from feeding the guest silence and
     *  commits to proving actual audio input bytes. */
    bool                    fInputPreBuffered;
    /** Input streams only: The number of bytes we need to prebuffer. */
    uint32_t                cbInputPreBuffer;
    /** Timestamp (absolute, in timer ticks) of the last DMA data transfer.
     * @note This is used for wall clock (WALCLK) calculations.  */
    uint64_t volatile       tsTransferLast;
    /** The stream's current configuration (matches SDnFMT). */
    PDMAUDIOSTREAMCFG       Cfg;
    /** Timestamp (real time, in ns) of last DMA transfer. */
    uint64_t                tsLastTransferNs;
    /** Timestamp (real time, in ns) of last stream read (to backends).
     *  When running in async I/O mode, this differs from \a tsLastTransferNs,
     *  because reading / processing will be done in a separate stream. */
    uint64_t                tsLastReadNs;

    /** The start time for the playback (on the timer clock). */
    uint64_t                tsStart;

    /** @name DMA engine
     * @{ */
    /** Timestamp (absolute, in timer ticks) of the next DMA data transfer.
     *  Next for determining the next scheduling window.
     *  Can be 0 if no next transfer is scheduled. */
    uint64_t                tsTransferNext;
    /** The size of the current DMA transfer period. */
    uint32_t                cbCurDmaPeriod;
    /** The size of an average transfer. */
    uint32_t                cbAvgTransfer;

    /** Current circular buffer read offset (for tracing & logging). */
    uint64_t                offRead;
    /** Current circular buffer write offset (for tracing & logging). */
    uint64_t                offWrite;

    /** The offset into the current BDLE. */
    uint32_t                offCurBdle;
    /** LVI + 1 */
    uint16_t                cBdles;
    /** The index of the current BDLE.
     * This is the entry which period is currently "running" on the DMA timer.  */
    uint8_t                 idxCurBdle;
    /** The number of prologue scheduling steps.
     * This is used when the tail BDLEs doesn't have IOC set.  */
    uint8_t                 cSchedulePrologue;
    /** Number of scheduling steps. */
    uint16_t                cSchedule;
    /** Current scheduling step. */
    uint16_t                idxSchedule;
    /** Current loop number within the current scheduling step.  */
    uint32_t                idxScheduleLoop;

    /** Buffer descriptors and additional timer scheduling state.
     * (Same as HDABDLEDESC, with more sensible naming.)  */
    struct
    {
        /** The buffer address. */
        uint64_t            GCPhys;
        /** The buffer size (guest bytes). */
        uint32_t            cb;
        /** The flags (only bit 0 is defined).    */
        uint32_t            fFlags;
    }                       aBdl[256];
    /** Scheduling steps. */
    struct
    {
        /** Number of timer ticks per period.
         * ASSUMES that we don't need a full second and that the timer resolution
         * isn't much higher than nanoseconds. */
        uint32_t            cPeriodTicks;
        /** The period length in host bytes. */
        uint32_t            cbPeriod;
        /** Number of times to repeat the period. */
        uint32_t            cLoops;
        /** The BDL index of the first entry.   */
        uint8_t             idxFirst;
        /** The number of BDL entries. */
        uint8_t             cEntries;
        uint8_t             abPadding[2];
    }                       aSchedule[512+8];

#ifdef VBOX_HDA_WITH_ON_REG_ACCESS_DMA
    /** Number of valid bytes in abDma.
     * @note Volatile to prevent the compiler from re-reading it after we've
     *       validated the value in ring-0. */
    uint32_t volatile       cbDma;
    /** Total number of bytes going via abDma this timer period. */
    uint32_t                cbDmaTotal;
    /** DMA bounce buffer for ring-0 register reads (LPIB). */
    uint8_t                 abDma[2048 - 8];
#endif
    /** @} */
} HDASTREAMSTATE;
AssertCompileSizeAlignment(HDASTREAMSTATE, 16);
AssertCompileMemberAlignment(HDASTREAMSTATE, aBdl, 8);
AssertCompileMemberAlignment(HDASTREAMSTATE, aBdl, 16);
AssertCompileMemberAlignment(HDASTREAMSTATE, aSchedule, 16);

/**
 * An HDA stream (SDI / SDO) - shared.
 *
 * @note This HDA stream has nothing to do with a regular audio stream handled
 *       by the audio connector or the audio mixer. This HDA stream is a serial
 *       data in/out stream (SDI/SDO) defined in hardware and can contain
 *       multiple audio streams in one single SDI/SDO (interleaving streams).
 *
 * Contains only register values which do *not* change until a stream reset
 * occurs.
 */
typedef struct HDASTREAM
{
    /** Internal state of this stream. */
    HDASTREAMSTATE              State;

    /** Stream descriptor number (SDn). */
    uint8_t                     u8SD;
    /** Current channel index.
     *  For a stereo stream, this is u8Channel + 1. */
    uint8_t                     u8Channel;
    /** FIFO Watermark (checked + translated in bytes, FIFOW).
     * This will be update from hdaRegWriteSDFIFOW() and also copied
     * hdaR3StreamInit() for some reason. */
    uint8_t                     u8FIFOW;

    /** @name Register values at stream setup.
     * These will all be copied in hdaR3StreamInit().
     * @{ */
    /** FIFO Size (checked + translated in bytes, FIFOS).
     * This is supposedly the max number of bytes we'll be DMA'ing in one chunk
     * and correspondingly the LPIB & wall clock update jumps.  However, we're
     * not at all being honest with the guest about this. */
    uint8_t                     u8FIFOS;
    /** Cyclic Buffer Length (SDnCBL) - Represents the size of the ring buffer. */
    uint32_t                    u32CBL;
    /** Last Valid Index (SDnLVI). */
    uint16_t                    u16LVI;
    /** Format (SDnFMT). */
    uint16_t                    u16FMT;
    uint8_t                     abPadding[4];
    /** DMA base address (SDnBDPU - SDnBDPL). */
    uint64_t                    u64BDLBase;
    /** @} */

    /** The timer for pumping data thru the attached LUN drivers. */
    TMTIMERHANDLE               hTimer;

    /** Pad the structure size to a 64 byte alignment. */
    uint64_t                    au64Padding1[2];
} HDASTREAM;
AssertCompileMemberAlignment(HDASTREAM, State.aBdl, 16);
AssertCompileMemberAlignment(HDASTREAM, State.aSchedule, 16);
AssertCompileSizeAlignment(HDASTREAM, 64);
/** Pointer to an HDA stream (SDI / SDO).  */
typedef HDASTREAM *PHDASTREAM;


/**
 * An HDA stream (SDI / SDO) - ring-3 bits.
 */
typedef struct HDASTREAMR3
{
    /** Stream descriptor number (SDn). */
    uint8_t                     u8SD;
    uint8_t                     abPadding[7];
    /** The shared state for the parent HDA device. */
    R3PTRTYPE(PHDASTATE)        pHDAStateShared;
    /** The ring-3 state for the parent HDA device. */
    R3PTRTYPE(PHDASTATER3)      pHDAStateR3;
    /** Pointer to HDA sink this stream is attached to. */
    R3PTRTYPE(PHDAMIXERSINK)    pMixSink;
    /** Internal state of this stream. */
    struct
    {
        /** Circular buffer (FIFO) for holding DMA'ed data. */
        R3PTRTYPE(PRTCIRCBUF)   pCircBuf;
        /** The mixer sink this stream has registered AIO update callback with.
         * This is NULL till we register it, typically in hdaR3StreamEnable.
         * (The problem with following the pMixSink assignment is that hdaR3StreamReset
         * sets it without updating the HDA sink structure, so things get out of
         * wack in hdaR3MixerControl later in the initial device reset.) */
        PAUDMIXSINK             pAioRegSink;

        /** Size of the DMA buffer (pCircBuf) in bytes. */
        uint32_t                StatDmaBufSize;
        /** Number of used bytes in the DMA buffer (pCircBuf). */
        uint32_t                StatDmaBufUsed;
        /** Counter for all under/overflows problems. */
        STAMCOUNTER             StatDmaFlowProblems;
        /** Counter for unresovled under/overflows problems. */
        STAMCOUNTER             StatDmaFlowErrors;
        /** Number of bytes involved in unresolved flow errors. */
        STAMCOUNTER             StatDmaFlowErrorBytes;
        /** DMA skipped because buffer interrupt pending. */
        STAMCOUNTER             StatDmaSkippedPendingBcis;

        STAMPROFILE             StatStart;
        STAMPROFILE             StatReset;
        STAMPROFILE             StatStop;
    } State;
    /** Debug bits. */
    HDASTREAMDEBUG              Dbg;
    uint64_t                    au64Alignment[3];
} HDASTREAMR3;
AssertCompileSizeAlignment(HDASTREAMR3, 64);
/** Pointer to an HDA stream (SDI / SDO).  */
typedef HDASTREAMR3 *PHDASTREAMR3;

/** @name Stream functions (all contexts).
 * @{
 */
VBOXSTRICTRC        hdaStreamDoOnAccessDmaOutput(PPDMDEVINS pDevIns, PHDASTATE pThis, PHDASTREAM pStreamShared,
                                                 uint64_t tsNow, uint32_t cbToTransfer);
VBOXSTRICTRC        hdaStreamMaybeDoOnAccessDmaOutput(PPDMDEVINS pDevIns, PHDASTATE pThis,
                                                      PHDASTREAM pStreamShared, uint64_t tsNow);
/** @} */

#ifdef IN_RING3

/** @name Stream functions (ring-3).
 * @{
 */
int                 hdaR3StreamConstruct(PHDASTREAM pStreamShared, PHDASTREAMR3 pStreamR3, PHDASTATE pThis,
                                         PHDASTATER3 pThisCC, uint8_t uSD);
void                hdaR3StreamDestroy(PHDASTREAMR3 pStreamR3);
int                 hdaR3StreamSetUp(PPDMDEVINS pDevIns, PHDASTATE pThis, PHDASTREAM pStreamShared,
                                     PHDASTREAMR3 pStreamR3, uint8_t uSD);
void                hdaR3StreamReset(PHDASTATE pThis, PHDASTATER3 pThisCC,
                                     PHDASTREAM pStreamShared, PHDASTREAMR3 pStreamR3, uint8_t uSD);
int                 hdaR3StreamEnable(PHDASTATE pThis, PHDASTREAM pStreamShared, PHDASTREAMR3 pStreamR3, bool fEnable);
void                hdaR3StreamMarkStarted(PPDMDEVINS pDevIns, PHDASTATE pThis, PHDASTREAM pStreamShared, uint64_t tsNow);
void                hdaR3StreamMarkStopped(PHDASTREAM pStreamShared);

uint64_t            hdaR3StreamTimerMain(PPDMDEVINS pDevIns, PHDASTATE pThis, PHDASTATER3 pThisCC,
                                         PHDASTREAM pStreamShared, PHDASTREAMR3 pStreamR3);
DECLCALLBACK(void)  hdaR3StreamUpdateAsyncIoJob(PPDMDEVINS pDevIns, PAUDMIXSINK pSink, void *pvUser);
/** @} */

/** @name Helper functions associated with the stream code.
 * @{ */
int                 hdaR3SDFMTToPCMProps(uint16_t u16SDFMT, PPDMAUDIOPCMPROPS pProps);
# ifdef LOG_ENABLED
void                hdaR3BDLEDumpAll(PPDMDEVINS pDevIns, PHDASTATE pThis, uint64_t u64BDLBase, uint16_t cBDLE);
# endif
/** @} */

#endif /* IN_RING3 */
#endif /* !VBOX_INCLUDED_SRC_Audio_DevHdaStream_h */

