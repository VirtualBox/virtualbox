/* $Id: DevSB16.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevSB16 - VBox SB16 Audio Controller.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
 * --------------------------------------------------------------------
 *
 * This code is based on: sb16.c from QEMU AUDIO subsystem (r3917).
 * QEMU Soundblaster 16 emulation
 *
 * Copyright (c) 2003-2005 Vassili Karpov (malc)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_SB16
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#ifdef IN_RING3
# include <iprt/mem.h>
# include <iprt/string.h>
# include <iprt/uuid.h>
#endif

#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/AssertGuest.h>

#include "VBoxDD.h"

#include "AudioMixBuffer.h"
#include "AudioMixer.h"
#include "AudioHlp.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Default timer frequency (in Hz). */
#define SB16_TIMER_HZ_DEFAULT           100
/** The maximum number of separate streams we currently implement.
 *  Currently we only support one stream only, namely the output stream. */
#define SB16_MAX_STREAMS                1
/** The (zero-based) index of the output stream in \a aStreams. */
#define SB16_IDX_OUT                    0

/** Current saved state version. */
#define SB16_SAVE_STATE_VERSION         2
/** The version used in VirtualBox version 3.0 and earlier. This didn't include the config dump. */
#define SB16_SAVE_STATE_VERSION_VBOX_30 1


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char e3[] = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the SB16 state. */
typedef struct SB16STATE *PSB16STATE;

/**
 * The internal state of a SB16 stream.
 */
typedef struct SB16STREAMSTATE
{
    /** Critical section for this stream. */
    RTCRITSECT              CritSect;
    /** Flag indicating whether this stream is in enabled state or not. */
    bool                    fEnabled;
    /** Set if we've registered the asynchronous update job. */
    bool                    fRegisteredAsyncUpdateJob;
    /** DMA cache to read data from / write data to. */
    PRTCIRCBUF              pCircBuf;
    /** Current circular buffer read offset (for tracing & logging). */
    uint64_t                offRead;
    /** Current circular buffer write offset (for tracing & logging). */
    uint64_t                offWrite;

    /** Size of the DMA buffer (pCircBuf) in bytes. */
    uint32_t                StatDmaBufSize;
    /** Number of used bytes in the DMA buffer (pCircBuf). */
    uint32_t                StatDmaBufUsed;
} SB16STREAMSTATE;
/** Pointer to internal state of an SB16 stream. */
typedef SB16STREAMSTATE *PSB16STREAMSTATE;

/**
 * Structure defining a (host backend) driver stream.
 * Each driver has its own instances of audio mixer streams, which then
 * can go into the same (or even different) audio mixer sinks.
 */
typedef struct SB16DRIVERSTREAM
{
    /** Associated mixer stream handle. */
    R3PTRTYPE(PAUDMIXSTREAM)        pMixStrm;
    /** The stream's current configuration. */
} SB16DRIVERSTREAM, *PSB16DRIVERSTREAM;

/**
 * Struct for tracking a host backend driver, i.e. our per-LUN data.
 */
typedef struct SB16DRIVER
{
    /** Node for storing this driver in our device driver list of SB16STATE. */
    RTLISTNODER3                    Node;
    /** Pointer to SB16 controller (state). */
    R3PTRTYPE(PSB16STATE)           pSB16State;
    /** Pointer to attached driver base interface. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** Audio connector interface to the underlying host backend. */
    R3PTRTYPE(PPDMIAUDIOCONNECTOR)  pConnector;
    /** Stream for output. */
    SB16DRIVERSTREAM                Out;
    /** LUN # to which this driver has been assigned. */
    uint8_t                         uLUN;
    /** Whether this driver is in an attached state or not. */
    bool                            fAttached;
    /** The LUN description. */
    char                            szDesc[48 - 2];
} SB16DRIVER;
/** Pointer to the per-LUN data. */
typedef SB16DRIVER *PSB16DRIVER;

/**
 * Runtime configurable debug stuff for a SB16 stream.
 */
typedef struct SB16STREAMDEBUGRT
{
    /** Whether debugging is enabled or not. */
    bool                            fEnabled;
    uint8_t                         Padding[7];
    /** File for dumping DMA reads / writes.
     *  For input streams, this dumps data being written to the device DMA,
     *  whereas for output streams this dumps data being read from the device DMA. */
    R3PTRTYPE(PAUDIOHLPFILE)        pFileDMA;
} SB16STREAMDEBUGRT;

/**
 * Debug stuff for a SB16 stream.
 */
typedef struct SB16STREAMDEBUG
{
    /** Runtime debug stuff. */
    SB16STREAMDEBUGRT               Runtime;
} SB16STREAMDEBUG;

/**
 * Structure for keeping a SB16 hardware stream configuration.
 */
typedef struct SB16STREAMHWCFG
{
    /** IRQ # to use. */
    uint8_t                         uIrq;
    /** Low DMA channel to use. */
    uint8_t                         uDmaChanLow;
    /** High DMA channel to use. */
    uint8_t                         uDmaChanHigh;
    /** IO port to use. */
    RTIOPORT                        uPort;
    /** DSP version to expose. */
    uint16_t                        uVer;
} SB16STREAMHWCFG;

/**
 * Structure for a SB16 stream.
 */
typedef struct SB16STREAM
{
    /** The stream's own index in \a aStreams of SB16STATE.
     *  Set to UINT8_MAX if not set (yet). */
    uint8_t                 uIdx;
    uint16_t                uTimerHz;
    /** The timer for pumping data thru the attached LUN drivers. */
    TMTIMERHANDLE           hTimerIO;
    /** The timer interval for pumping data thru the LUN drivers in timer ticks. */
    uint64_t                cTicksTimerIOInterval;
    /** Timestamp of the last timer callback (sb16TimerIO).
     * Used to calculate thetime actually elapsed between two timer callbacks.
     * This currently ASSMUMES that we only have one single (output) stream. */
    uint64_t                tsTimerIO; /** @todo Make this a per-stream value. */
    /** The stream's currentconfiguration. */
    PDMAUDIOSTREAMCFG       Cfg;
    /** The stream's defaulthardware configuration, mostly done by jumper settings back then. */
    SB16STREAMHWCFG         HwCfgDefault;
    /** The stream's hardware configuration set at runtime.
     *  Might differ from the default configuration above and is needed for live migration. */
    SB16STREAMHWCFG         HwCfgRuntime;

    int                     fifo;
    int                     dma_auto;
    /** Whether to use the high (\c true) or the low (\c false) DMA channel. */
    int                     fDmaUseHigh;
    int                     can_write; /** @todo r=andy BUGBUG Value never gets set to 0! */
    int                     time_const;
    /** The DMA transfer (block)size in bytes. */
    int32_t                 cbDmaBlockSize;
    int32_t                 cbDmaLeft; /** Note: Can be < 0. Needs to 32-bit for backwards compatibility. */
    /** Internal state of this stream. */
    SB16STREAMSTATE         State;
    /** Debug stuff. */
    SB16STREAMDEBUG         Dbg;
} SB16STREAM;
/** Pointer to a SB16 stream */
typedef SB16STREAM *PSB16STREAM;

/**
 * SB16 debug settings.
 */
typedef struct SB16STATEDEBUG
{
    /** Whether debugging is enabled or not. */
    bool                    fEnabled;
    bool                    afAlignment[7];
    /** Path where to dump the debug output to.
     *  Can be NULL, in which the system's temporary directory will be used then. */
    R3PTRTYPE(char *)       pszOutPath;
} SB16STATEDEBUG;

/**
 * The SB16 state.
 */
typedef struct SB16STATE
{
    /** Pointer to the device instance. */
    PPDMDEVINSR3           pDevInsR3;
    /** Pointer to the connector of the attached audio driver. */
    PPDMIAUDIOCONNECTOR    pDrv;

    int                    dsp_in_idx;
    int                    dsp_out_data_len;
    int                    dsp_in_needed_bytes;
    int                    cmd;
    int                    highspeed;

    int                    v2x6;

    uint8_t                csp_param;
    uint8_t                csp_value;
    uint8_t                csp_mode;
    uint8_t                csp_index;
    uint8_t                csp_regs[256];
    uint8_t                csp_reg83[4];
    int                    csp_reg83r;
    int                    csp_reg83w;

    uint8_t                dsp_in_data[10];
    uint8_t                dsp_out_data[50];
    uint8_t                test_reg;
    uint8_t                last_read_byte;
    int                    nzero;

    RTLISTANCHOR           lstDrv;
    /** IRQ timer   */
    TMTIMERHANDLE          hTimerIRQ;
    /** The base interface for LUN\#0. */
    PDMIBASE               IBase;

    /** Array of all SB16 hardware audio stream. */
    SB16STREAM             aStreams[SB16_MAX_STREAMS];
    /** The device's software mixer. */
    R3PTRTYPE(PAUDIOMIXER) pMixer;
    /** Audio sink for PCM output. */
    R3PTRTYPE(PAUDMIXSINK) pSinkOut;

    /** The two mixer I/O ports (port + 4). */
    IOMIOPORTHANDLE        hIoPortsMixer;
    /** The 10 DSP I/O ports (port + 6). */
    IOMIOPORTHANDLE        hIoPortsDsp;

    /** Debug settings. */
    SB16STATEDEBUG         Dbg;

    /* mixer state */
    uint8_t                mixer_nreg;
    uint8_t                mixer_regs[256];

#ifdef VBOX_WITH_STATISTICS
    STAMPROFILE            StatTimerIO;
    STAMCOUNTER            StatBytesRead;
#endif
} SB16STATE;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(PDMAUDIODIR) sb16GetDirFromIndex(uint8_t uIdx);

static int  sb16StreamEnable(PSB16STATE pThis, PSB16STREAM pStream, bool fEnable, bool fForce);
static void sb16StreamReset(PSB16STATE pThis, PSB16STREAM pStream);
static int  sb16StreamOpen(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream);
static void sb16StreamClose(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream);
DECLINLINE(void) sb16StreamLock(PSB16STREAM pStream);
DECLINLINE(void) sb16StreamUnlock(PSB16STREAM pStream);
DECLINLINE(PAUDMIXSINK) sb16StreamIndexToSink(PSB16STATE pThis, uint8_t uIdx);
static void sb16StreamTransferScheduleNext(PSB16STATE pThis, PSB16STREAM pStream, uint32_t cSamples);
static int  sb16StreamDoDmaOutput(PSB16STATE pThis, PSB16STREAM pStream, int uDmaChan, uint32_t offDma, uint32_t cbDma, uint32_t cbToRead, uint32_t *pcbRead);
static DECLCALLBACK(void) sb16StreamUpdateAsyncIoJob(PPDMDEVINS pDevIns, PAUDMIXSINK pSink, void *pvUser);

static DECLCALLBACK(void) sb16TimerIO(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser);
static DECLCALLBACK(void) sb16TimerIRQ(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser);
DECLINLINE(void) sb16TimerSet(PPDMDEVINS pDevIns, PSB16STREAM pStream, uint64_t cTicksToDeadline);

static void sb16SpeakerControl(PSB16STATE pThis, bool fOn);
static void sb16UpdateVolume(PSB16STATE pThis);



static void sb16SpeakerControl(PSB16STATE pThis, bool fOn)
{
    RT_NOREF(pThis, fOn);

    /** @todo This currently does nothing. */
}

static void sb16StreamControl(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream, bool fRun)
{
    unsigned uDmaChan = pStream->fDmaUseHigh ? pStream->HwCfgRuntime.uDmaChanHigh : pStream->HwCfgRuntime.uDmaChanLow;

    LogFunc(("fRun=%RTbool, fDmaUseHigh=%RTbool, uDmaChan=%u\n", fRun, pStream->fDmaUseHigh, uDmaChan));

    PDMDevHlpDMASetDREQ(pThis->pDevInsR3, uDmaChan, fRun ? 1 : 0);

    if (fRun != pStream->State.fEnabled)
    {
        if (fRun)
        {
            int rc = VINF_SUCCESS;

            if (pStream->Cfg.Props.uHz > 0)
            {
                rc = sb16StreamOpen(pDevIns, pThis, pStream);
                if (RT_SUCCESS(rc))
                    sb16UpdateVolume(pThis);
            }
            else
                AssertFailed(); /** @todo Buggy code? */

            if (RT_SUCCESS(rc))
            {
                rc = sb16StreamEnable(pThis, pStream, true /* fEnable */, false /* fForce */);
                if (RT_SUCCESS(rc))
                {
                    sb16TimerSet(pDevIns, pStream, pStream->cTicksTimerIOInterval);

                    PDMDevHlpDMASchedule(pThis->pDevInsR3);
                }
            }
        }
        else
        {
            sb16StreamEnable(pThis, pStream, false /* fEnable */, false /* fForce */);
        }
    }
}

#define DMA8_AUTO 1
#define DMA8_HIGH 2

static void sb16DmaCmdContinue8(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream)
{
    sb16StreamControl(pDevIns, pThis, pStream, true /* fRun */);
}

static void sb16DmaCmd8(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream,
                        int mask, int dma_len)
{
    pStream->fDmaUseHigh = 0;

    if (-1 == pStream->time_const)
    {
        if (pStream->Cfg.Props.uHz == 0)
            pStream->Cfg.Props.uHz = 11025;
    }
    else
    {
        int tmp = (256 - pStream->time_const);
        pStream->Cfg.Props.uHz = (1000000 + (tmp / 2)) / tmp;
    }

    /** @todo r=bird: Use '(pThis->mixer_regs[0x0e] & 2) == 0 ? 1 : 2' like below? */
    unsigned cShiftChannels = PDMAudioPropsChannels(&pStream->Cfg.Props) >= 2 ? 1 : 0;

    if (dma_len != -1)
    {
        pStream->cbDmaBlockSize = dma_len << cShiftChannels;
    }
    else
    {
        /* This is apparently the only way to make both Act1/PL
           and SecondReality/FC work

           r=andy Wow, actually someone who remembers Future Crew :-)

           Act1 sets block size via command 0x48 and it's an odd number
           SR does the same with even number
           Both use stereo, and Creatives own documentation states that
           0x48 sets block size in bytes less one.. go figure */
        pStream->cbDmaBlockSize &= ~cShiftChannels;
    }

    pStream->Cfg.Props.uHz >>= cShiftChannels;
    pStream->cbDmaLeft = pStream->cbDmaBlockSize;
    /* pThis->highspeed = (mask & DMA8_HIGH) != 0; */
    pStream->dma_auto = (mask & DMA8_AUTO) != 0;

    PDMAudioPropsInit(&pStream->Cfg.Props, 1                     /* 8-bit */,
                      false                                      /* fSigned */,
                      (pThis->mixer_regs[0x0e] & 2) == 0 ? 1 : 2 /* Mono/Stereo */,
                      pStream->Cfg.Props.uHz);

    /** @todo Check if stream's DMA block size is properly aligned to the set PCM props. */

    sb16DmaCmdContinue8(pDevIns, pThis, pStream);
    sb16SpeakerControl(pThis, true /* fOn */);
}

static void sb16DmaCmd(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream,
                       uint8_t cmd, uint8_t d0, int dma_len)
{
    pStream->fDmaUseHigh   = cmd < 0xc0;
    pStream->fifo       = (cmd >> 1) & 1;
    pStream->dma_auto   = (cmd >> 2) & 1;

    pStream->Cfg.Props.fSigned = RT_BOOL(d0 & RT_BIT_32(4));
    PDMAudioPropsSetChannels(&pStream->Cfg.Props, 1 + ((d0 >> 5) & 1));

    switch (cmd >> 4)
    {
        case 11:
            PDMAudioPropsSetSampleSize(&pStream->Cfg.Props, 2 /*16-bit*/);
            break;

        case 12:
            PDMAudioPropsSetSampleSize(&pStream->Cfg.Props, 1 /*8-bit*/);
            break;

        default:
            AssertFailed();
            break;
    }

    if (-1 != pStream->time_const)
    {
#if 1
        int tmp = 256 - pStream->time_const;
        pStream->Cfg.Props.uHz = (1000000 + (tmp / 2)) / tmp;
#else
        /* pThis->freq = 1000000 / ((255 - pStream->time_const) << pThis->fmt_stereo); */
        pThis->freq = 1000000 / ((255 - pStream->time_const));
#endif
        pStream->time_const = -1;
    }

    pStream->cbDmaBlockSize = dma_len + 1;
    pStream->cbDmaBlockSize <<= PDMAudioPropsSampleSize(&pStream->Cfg.Props) == 2 ? 1 : 0;
    if (!pStream->dma_auto)
    {
        /*
         * It is clear that for DOOM and auto-init this value
         * shouldn't take stereo into account, while Miles Sound Systems
         * setsound.exe with single transfer mode wouldn't work without it
         * wonders of SB16 yet again.
         */
        pStream->cbDmaBlockSize <<= PDMAudioPropsSampleSize(&pStream->Cfg.Props) == 2 ? 1 : 0;
    }

    pStream->cbDmaLeft = pStream->cbDmaBlockSize;

    pThis->highspeed = 0;

    /** @todo Check if stream's DMA block size is properly aligned to the set PCM props. */

    sb16StreamControl(pDevIns, pThis, pStream, true /* fRun */);
    sb16SpeakerControl(pThis, true /* fOn */);
}

static inline void sb16DspSeData(PSB16STATE pThis, uint8_t val)
{
    LogFlowFunc(("%#x\n", val));
    if ((size_t) pThis->dsp_out_data_len < sizeof (pThis->dsp_out_data))
        pThis->dsp_out_data[pThis->dsp_out_data_len++] = val;
}

static inline uint8_t sb16DspGetData(PSB16STATE pThis)
{
    if (pThis->dsp_in_idx)
        return pThis->dsp_in_data[--pThis->dsp_in_idx];
    AssertMsgFailed(("DSP input buffer underflow\n"));
    return 0;
}

static void sb16DspCmdLookup(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream, uint8_t cmd)
{
    LogFlowFunc(("command %#x\n", cmd));

    if (cmd > 0xaf && cmd < 0xd0)
    {
        if (cmd & 8) /** @todo Handle recording. */
            LogFlowFunc(("ADC not yet supported (command %#x)\n", cmd));

        switch (cmd >> 4)
        {
            case 11:
            case 12:
                break;
            default:
                LogFlowFunc(("%#x wrong bits\n", cmd));
        }

        pThis->dsp_in_needed_bytes = 3;
    }
    else
    {
        pThis->dsp_in_needed_bytes = 0;

        /** @todo Use a mapping table with
         *        - a command verb (binary search)
         *        - required bytes
         *        - function callback handler
         */

        switch (cmd)
        {
            case 0x03:              /* ASP Status */
                sb16DspSeData(pThis, 0x10); /* pThis->csp_param); */
                goto warn;

            case 0x04:              /* DSP Status (Obsolete) / ASP ??? */
                pThis->dsp_in_needed_bytes = 1;
                goto warn;

            case 0x05:              /* ASP ??? */
                pThis->dsp_in_needed_bytes = 2;
                goto warn;

            case 0x08:              /* ??? */
                /* __asm__ ("int3"); */
                goto warn;

            case 0x09:              /* ??? */
                sb16DspSeData(pThis, 0xf8);
                goto warn;

            case 0x0e:              /* ??? */
                pThis->dsp_in_needed_bytes = 2;
                goto warn;

            case 0x0f:              /* ??? */
                pThis->dsp_in_needed_bytes = 1;
                goto warn;

            case 0x10:              /* Direct mode DAC */
                pThis->dsp_in_needed_bytes = 1;
                goto warn;

            case 0x14:              /* DAC DMA, 8-bit, uncompressed */
                pThis->dsp_in_needed_bytes = 2;
                pStream->cbDmaBlockSize = 0;
                break;

            case 0x1c:              /* Auto-Initialize DMA DAC, 8-bit */
                sb16DmaCmd8(pDevIns, pThis, pStream, DMA8_AUTO, -1);
                break;

            case 0x20:              /* Direct ADC, Juice/PL */
                sb16DspSeData(pThis, 0xff);
                goto warn;

            case 0x35:              /* MIDI Read Interrupt + Write Poll (UART) */
                LogRelMax2(32, ("SB16: MIDI support not implemented yet\n"));
                break;

            case 0x40:              /* Set Time Constant */
                pStream->time_const = -1;
                pThis->dsp_in_needed_bytes = 1;
                break;

            case 0x41:              /* Set sample rate for input */
                pStream->Cfg.Props.uHz = 0; /** @todo r=andy Why do we reset output stuff here? */
                pStream->time_const = -1;
                pThis->dsp_in_needed_bytes = 2;
                break;

            case 0x42:             /* Set sample rate for output */
                pStream->Cfg.Props.uHz = 0;
                pStream->time_const = -1;
                pThis->dsp_in_needed_bytes = 2;
                goto warn;

            case 0x45:             /* Continue Auto-Initialize DMA, 8-bit */
                sb16DspSeData(pThis, 0xaa);
                goto warn;

            case 0x47:             /* Continue Auto-Initialize DMA, 16-bit */
                break;

            case 0x48:             /* Set DMA Block Size */
                pThis->dsp_in_needed_bytes = 2;
                break;

            case 0x74:             /* DMA DAC, 4-bit ADPCM */
                pThis->dsp_in_needed_bytes = 2;
                LogFlowFunc(("4-bit ADPCM not implemented yet\n"));
                break;

            case 0x75:              /* DMA DAC, 4-bit ADPCM Reference */
                pThis->dsp_in_needed_bytes = 2;
                LogFlowFunc(("DMA DAC, 4-bit ADPCM Reference not implemented\n"));
                break;

            case 0x76:              /* DMA DAC, 2.6-bit ADPCM */
                pThis->dsp_in_needed_bytes = 2;
                LogFlowFunc(("DMA DAC, 2.6-bit ADPCM not implemented yet\n"));
                break;

            case 0x77:              /* DMA DAC, 2.6-bit ADPCM Reference */
                pThis->dsp_in_needed_bytes = 2;
                LogFlowFunc(("ADPCM reference not implemented yet\n"));
                break;

            case 0x7d:              /* Auto-Initialize DMA DAC, 4-bit ADPCM Reference */
                LogFlowFunc(("Autio-Initialize DMA DAC, 4-bit ADPCM reference not implemented yet\n"));
                break;

            case 0x7f:              /* Auto-Initialize DMA DAC, 16-bit ADPCM Reference */
                LogFlowFunc(("Autio-Initialize DMA DAC, 2.6-bit ADPCM Reference not implemented yet\n"));
                break;

            case 0x80:              /* Silence DAC */
                pThis->dsp_in_needed_bytes = 2;
                break;

            case 0x90:              /* Auto-Initialize DMA DAC, 8-bit (High Speed) */
                RT_FALL_THROUGH();
            case 0x91:              /* Normal DMA DAC, 8-bit (High Speed) */
                sb16DmaCmd8(pDevIns, pThis, pStream, (((cmd & 1) == 0) ? 1 : 0) | DMA8_HIGH, -1);
                break;

            case 0xd0:              /* Halt DMA operation. 8bit */
                sb16StreamControl(pDevIns, pThis, pStream, false /* fRun */);
                break;

            case 0xd1:              /* Speaker on */
                sb16SpeakerControl(pThis, true /* fOn */);
                break;

            case 0xd3:              /* Speaker off */
                sb16SpeakerControl(pThis, false /* fOn */);
                break;

            case 0xd4:              /* Continue DMA operation, 8-bit */
                /* KQ6 (or maybe Sierras audblst.drv in general) resets
                   the frequency between halt/continue */
                sb16DmaCmdContinue8(pDevIns, pThis, pStream);
                break;

            case 0xd5:              /* Halt DMA operation, 16-bit */
                sb16StreamControl(pDevIns, pThis, pStream, false /* fRun */);
                break;

            case 0xd6:              /* Continue DMA operation, 16-bit */
                sb16StreamControl(pDevIns, pThis, pStream, true /* fRun */);
                break;

            case 0xd9:              /* Exit auto-init DMA after this block, 16-bit */
                pStream->dma_auto = 0;
                break;

            case 0xda:              /* Exit auto-init DMA after this block, 8-bit */
                pStream->dma_auto = 0;
                break;

            case 0xe0:              /* DSP identification */
                pThis->dsp_in_needed_bytes = 1;
                break;

            case 0xe1:              /* DSP version */
                sb16DspSeData(pThis, RT_LO_U8(pStream->HwCfgRuntime.uVer));
                sb16DspSeData(pThis, RT_HI_U8(pStream->HwCfgRuntime.uVer));
                break;

            case 0xe2:              /* ??? */
                pThis->dsp_in_needed_bytes = 1;
                goto warn;

            case 0xe3:              /* DSP copyright */
            {
                for (int i = sizeof(e3) - 1; i >= 0; --i)
                    sb16DspSeData(pThis, e3[i]);
                break;
            }

            case 0xe4:              /* Write test register */
                pThis->dsp_in_needed_bytes = 1;
                break;

            case 0xe7:              /* ??? */
                LogFlowFunc(("Attempt to probe for ESS (0xe7)?\n"));
                break;

            case 0xe8:              /* Read test register */
                sb16DspSeData(pThis, pThis->test_reg);
                break;

            case 0xf2:              /* IRQ Request, 8-bit */
                RT_FALL_THROUGH();
            case 0xf3:              /* IRQ Request, 16-bit */
            {
                sb16DspSeData(pThis, 0xaa);
                pThis->mixer_regs[0x82] |= (cmd == 0xf2) ? 1 : 2;
                PDMDevHlpISASetIrq(pThis->pDevInsR3, pStream->HwCfgRuntime.uIrq, 1);
                break;
            }

            case 0xf8:              /* Undocumented, used by old Creative diagnostic programs */
                sb16DspSeData(pThis, 0);
                goto warn;

            case 0xf9:              /* ??? */
                pThis->dsp_in_needed_bytes = 1;
                goto warn;

            case 0xfa:              /* ??? */
                sb16DspSeData(pThis, 0);
                goto warn;

            case 0xfc:              /* ??? */
                sb16DspSeData(pThis, 0);
                goto warn;

            default:
                LogFunc(("Unrecognized DSP command %#x, ignored\n", cmd));
                break;
        }
    }

exit:

     if (!pThis->dsp_in_needed_bytes)
        pThis->cmd = -1;
     else
        pThis->cmd = cmd;

    return;

warn:
    LogFunc(("warning: command %#x,%d is not truly understood yet\n", cmd, pThis->dsp_in_needed_bytes));
    goto exit;
}

DECLINLINE(uint16_t) sb16DspGetLoHi(PSB16STATE pThis)
{
    const uint8_t hi = sb16DspGetData(pThis);
    const uint8_t lo = sb16DspGetData(pThis);
    return RT_MAKE_U16(lo, hi);
}

DECLINLINE(uint16_t) sb16DspGetHiLo(PSB16STATE pThis)
{
    const uint8_t lo = sb16DspGetData(pThis);
    const uint8_t hi = sb16DspGetData(pThis);
    return RT_MAKE_U16(lo, hi);
}

static void sb16DspCmdComplete(PPDMDEVINS pDevIns, PSB16STATE pThis)
{
    LogFlowFunc(("Command %#x, in_index %d, needed_bytes %d\n", pThis->cmd, pThis->dsp_in_idx, pThis->dsp_in_needed_bytes));

    int v0, v1, v2;

    PSB16STREAM pStream = &pThis->aStreams[SB16_IDX_OUT]; /** @ŧodo Improve this. */

    if (pThis->cmd > 0xaf && pThis->cmd < 0xd0)
    {
        v2 = sb16DspGetData(pThis);
        v1 = sb16DspGetData(pThis);
        v0 = sb16DspGetData(pThis);

        if (pThis->cmd & 8)
            LogFlowFunc(("ADC params cmd = %#x d0 = %d, d1 = %d, d2 = %d\n", pThis->cmd, v0, v1, v2));
        else
        {
            LogFlowFunc(("cmd = %#x d0 = %d, d1 = %d, d2 = %d\n", pThis->cmd, v0, v1, v2));
            sb16DmaCmd(pDevIns, pThis, pStream, pThis->cmd, v0, v1 + (v2 << 8));
        }
    }
    else
    {
        switch (pThis->cmd)
        {
            case 0x04:
                pThis->csp_mode = sb16DspGetData(pThis);
                pThis->csp_reg83r = 0;
                pThis->csp_reg83w = 0;
                LogFlowFunc(("CSP command 0x04: mode=%#x\n", pThis->csp_mode));
                break;

            case 0x05:
                pThis->csp_param = sb16DspGetData(pThis);
                pThis->csp_value = sb16DspGetData(pThis);
                LogFlowFunc(("CSP command 0x05: param=%#x value=%#x\n", pThis->csp_param, pThis->csp_value));
                break;

            case 0x0e:
                v0 = sb16DspGetData(pThis);
                v1 = sb16DspGetData(pThis);
                LogFlowFunc(("write CSP register %d <- %#x\n", v1, v0));
                if (v1 == 0x83)
                {
                    LogFlowFunc(("0x83[%d] <- %#x\n", pThis->csp_reg83r, v0));
                    pThis->csp_reg83[pThis->csp_reg83r % 4] = v0;
                    pThis->csp_reg83r += 1;
                }
                else
                    pThis->csp_regs[v1] = v0;
                break;

            case 0x0f:
                v0 = sb16DspGetData(pThis);
                LogFlowFunc(("read CSP register %#x -> %#x, mode=%#x\n", v0, pThis->csp_regs[v0], pThis->csp_mode));
                if (v0 == 0x83)
                {
                    LogFlowFunc(("0x83[%d] -> %#x\n", pThis->csp_reg83w, pThis->csp_reg83[pThis->csp_reg83w % 4]));
                    sb16DspSeData(pThis, pThis->csp_reg83[pThis->csp_reg83w % 4]);
                    pThis->csp_reg83w += 1;
                }
                else
                    sb16DspSeData(pThis, pThis->csp_regs[v0]);
                break;

            case 0x10:
                v0 = sb16DspGetData(pThis);
                LogFlowFunc(("cmd 0x10 d0=%#x\n", v0));
                break;

            case 0x14:
                sb16DmaCmd8(pDevIns, pThis, pStream, 0, sb16DspGetLoHi(pThis) + 1);
                break;

            case 0x22: /* Sets the master volume. */
                /** @todo Setting the master volume is not implemented yet. */
                break;

            case 0x40: /* Sets the timer constant; SB16 is able to use sample rates via 0x41 instead. */
                pStream->time_const = sb16DspGetData(pThis);
                LogFlowFunc(("set time const %d\n", pStream->time_const));
                break;

            case 0x42: /* Sets the input rate (in Hz). */
#if 0
                LogFlowFunc(("cmd 0x42 might not do what it think it should\n"));
#endif
                RT_FALL_THROUGH(); /** @todo BUGBUG FT2 sets output freq with this, go figure. */

            case 0x41: /* Sets the output rate (in Hz). */
                pStream->Cfg.Props.uHz = sb16DspGetHiLo(pThis);
                LogFlowFunc(("set freq to %RU16Hz\n", pStream->Cfg.Props.uHz));
                break;

            case 0x48:
                pStream->cbDmaBlockSize = sb16DspGetLoHi(pThis) + 1;
                LogFlowFunc(("set dma block len %d\n", pStream->cbDmaBlockSize));
                break;

            case 0x74:
            case 0x75:
            case 0x76:
            case 0x77:
                /* ADPCM stuff, ignore. */
                break;

            case 0x80: /* Sets the IRQ. */
                sb16StreamTransferScheduleNext(pThis, pStream, sb16DspGetLoHi(pThis) + 1);
                break;

            case 0xe0:
                v0 = sb16DspGetData(pThis);
                pThis->dsp_out_data_len = 0;
                LogFlowFunc(("E0=%#x\n", v0));
                sb16DspSeData(pThis, ~v0);
                break;

            case 0xe2:
                v0 = sb16DspGetData(pThis);
                LogFlowFunc(("E2=%#x\n", v0));
                break;

            case 0xe4:
                pThis->test_reg = sb16DspGetData(pThis);
                break;

            case 0xf9:
                v0 = sb16DspGetData(pThis);
                switch (v0)
                {
                    case 0x0e:
                        sb16DspSeData(pThis, 0xff);
                        break;

                    case 0x0f:
                        sb16DspSeData(pThis, 0x07);
                        break;

                    case 0x37:
                        sb16DspSeData(pThis, 0x38);
                        break;

                    default:
                        sb16DspSeData(pThis, 0x00);
                        break;
                }
                break;

            default:
                LogRel2(("SB16: Unrecognized command %#x, skipping\n", pThis->cmd));
                return;
        }
    }

    pThis->cmd = -1;
    return;
}

static void sb16DspCmdResetLegacy(PSB16STATE pThis)
{
    LogFlowFuncEnter();

    /* Disable speaker(s). */
    sb16SpeakerControl(pThis, false /* fOn */);

    /*
     * Reset all streams.
     */
    for (unsigned i = 0; i < SB16_MAX_STREAMS; i++)
        sb16StreamReset(pThis, &pThis->aStreams[i]);
}

static void sb16DspCmdReset(PSB16STATE pThis)
{
    pThis->mixer_regs[0x82] = 0;
    pThis->dsp_in_idx = 0;
    pThis->dsp_out_data_len = 0;
    pThis->dsp_in_needed_bytes = 0;
    pThis->nzero = 0;
    pThis->highspeed = 0;
    pThis->v2x6 = 0;
    pThis->cmd = -1;

    sb16DspSeData(pThis, 0xaa);

    sb16DspCmdResetLegacy(pThis);
}

/**
 * @callback_method_impl{PFNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) sb16IoPortDspWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    RT_NOREF(pvUser, cb);

    /** @todo Figure out how we can distinguish between streams. DSP port #, e.g. 0x220? */
    PSB16STREAM pStream = &pThis->aStreams[SB16_IDX_OUT];

    LogFlowFunc(("write %#x <- %#x\n", offPort, u32));
    switch (offPort)
    {
        case 0:
            switch (u32)
            {
                case 0x00:
                {
                    if (pThis->v2x6 == 1)
                    {
                        if (0 && pThis->highspeed)
                        {
                            pThis->highspeed = 0;
                            PDMDevHlpISASetIrq(pThis->pDevInsR3, pStream->HwCfgRuntime.uIrq, 0);
                            sb16StreamControl(pDevIns, pThis, pStream, false /* fRun */);
                        }
                        else
                            sb16DspCmdReset(pThis);
                    }
                    pThis->v2x6 = 0;
                    break;
                }

                case 0x01:
                case 0x03:              /* FreeBSD kludge */
                    pThis->v2x6 = 1;
                    break;

                case 0xc6:
                    pThis->v2x6 = 0;    /* Prince of Persia, csp.sys, diagnose.exe */
                    break;

                case 0xb8:              /* Panic */
                    sb16DspCmdReset(pThis);
                    break;

                case 0x39:
                    sb16DspSeData(pThis, 0x38);
                    sb16DspCmdReset(pThis);
                    pThis->v2x6 = 0x39;
                    break;

                default:
                    pThis->v2x6 = u32;
                    break;
            }
            break;

        case 6:                        /* Write data or command | write status */
#if 0
            if (pThis->highspeed)
                break;
#endif
            if (0 == pThis->dsp_in_needed_bytes)
            {
                sb16DspCmdLookup(pDevIns, pThis, pStream, u32);
            }
            else
            {
                if (pThis->dsp_in_idx == sizeof (pThis->dsp_in_data))
                {
                    AssertMsgFailed(("DSP input data overrun\n"));
                }
                else
                {
                    pThis->dsp_in_data[pThis->dsp_in_idx++] = u32;
                    if (pThis->dsp_in_idx == pThis->dsp_in_needed_bytes)
                    {
                        pThis->dsp_in_needed_bytes = 0;
                        sb16DspCmdComplete(pDevIns, pThis);
                    }
                }
            }
            break;

        default:
            LogFlowFunc(("offPort=%#x, u32=%#x)\n", offPort, u32));
            break;
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{PFNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) sb16IoPortDspRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser, cb);

    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);

    uint32_t retval;
    int ack = 0;

    /** @todo Figure out how we can distinguish between streams. */
    PSB16STREAM pStream = &pThis->aStreams[SB16_IDX_OUT];

    /** @todo reject non-byte access?
     *  The spec does not mention a non-byte access so we should check how real hardware behaves. */

    switch (offPort)
    {
        case 0:                     /* reset */
            retval = 0xff;
            break;

        case 4:                     /* read data */
            if (pThis->dsp_out_data_len)
            {
                retval = pThis->dsp_out_data[--pThis->dsp_out_data_len];
                pThis->last_read_byte = retval;
            }
            else
            {
                if (pThis->cmd != -1)
                    LogFlowFunc(("empty output buffer for command %#x\n", pThis->cmd));
                retval = pThis->last_read_byte;
                /* goto error; */
            }
            break;

        case 6:                     /* 0 can write */
            retval = pStream->can_write ? 0 : 0x80;
            break;

        case 7:                     /* timer interrupt clear */
            /* LogFlowFunc(("timer interrupt clear\n")); */
            retval = 0;
            break;

        case 8:                     /* data available status | irq 8 ack */
            retval = (!pThis->dsp_out_data_len || pThis->highspeed) ? 0 : 0x80;
            if (pThis->mixer_regs[0x82] & 1)
            {
                ack = 1;
                pThis->mixer_regs[0x82] &= ~1;
                PDMDevHlpISASetIrq(pThis->pDevInsR3, pStream->HwCfgRuntime.uIrq, 0);
            }
            break;

        case 9:                     /* irq 16 ack */
            retval = 0xff;
            if (pThis->mixer_regs[0x82] & 2)
            {
                ack = 1;
                pThis->mixer_regs[0x82] &= ~2;
                PDMDevHlpISASetIrq(pThis->pDevInsR3, pStream->HwCfgRuntime.uIrq, 0);
            }
            break;

        default:
            LogFlowFunc(("warning: sb16IoPortDspRead %#x error\n", offPort));
            return VERR_IOM_IOPORT_UNUSED;
    }

    if (!ack)
        LogFlowFunc(("read %#x -> %#x\n", offPort, retval));

    *pu32 = retval;
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Mixer functions                                                                                                              *
*********************************************************************************************************************************/

static uint8_t sb16MixRegToVol(PSB16STATE pThis, int reg)
{
    /* The SB16 mixer has a 0 to -62dB range in 32 levels (2dB each step).
     * We use a 0 to -96dB range in 256 levels (0.375dB each step).
     * Only the top 5 bits of a mixer register are used.
     */
    uint8_t steps = 31 - (pThis->mixer_regs[reg] >> 3);
    uint8_t vol   = 255 - steps * 16 / 3;   /* (2dB*8) / (0.375dB*8) */
    return vol;
}

/**
 * Returns the device's current master volume.
 *
 * @param   pThis               SB16 state.
 * @param   pVol                Where to store the master volume information.
 */
DECLINLINE(void) sb16GetMasterVolume(PSB16STATE pThis, PPDMAUDIOVOLUME pVol)
{
    /* There's no mute switch, only volume controls. */
    PDMAudioVolumeInitFromStereo(pVol, false /*fMuted*/, sb16MixRegToVol(pThis, 0x30), sb16MixRegToVol(pThis, 0x31));
}

/**
 * Returns the device's current output stream volume.
 *
 * @param   pThis               SB16 state.
 * @param   pVol                Where to store the output stream volume information.
 */
DECLINLINE(void) sb16GetPcmOutVolume(PSB16STATE pThis, PPDMAUDIOVOLUME pVol)
{
    /* There's no mute switch, only volume controls. */
    PDMAudioVolumeInitFromStereo(pVol, false /*fMuted*/, sb16MixRegToVol(pThis, 0x32), sb16MixRegToVol(pThis, 0x33));
}

static void sb16UpdateVolume(PSB16STATE pThis)
{
    PDMAUDIOVOLUME VolMaster;
    sb16GetMasterVolume(pThis, &VolMaster);

    PDMAUDIOVOLUME VolOut;
    sb16GetPcmOutVolume(pThis, &VolOut);

    /* Combine the master + output stream volume. */
    PDMAUDIOVOLUME VolCombined;
    PDMAudioVolumeCombine(&VolCombined, &VolMaster, &VolOut);

    int rc2 = AudioMixerSinkSetVolume(pThis->pSinkOut, &VolCombined);
    AssertRC(rc2);
}

static void sb16MixerReset(PSB16STATE pThis)
{
    memset(pThis->mixer_regs, 0xff, 0x7f);
    memset(pThis->mixer_regs + 0x83, 0xff, sizeof (pThis->mixer_regs) - 0x83);

    pThis->mixer_regs[0x02] = 4;    /* master volume 3bits */
    pThis->mixer_regs[0x06] = 4;    /* MIDI volume 3bits */
    pThis->mixer_regs[0x08] = 0;    /* CD volume 3bits */
    pThis->mixer_regs[0x0a] = 0;    /* voice volume 2bits */

    /* d5=input filt, d3=lowpass filt, d1,d2=input source */
    pThis->mixer_regs[0x0c] = 0;

    /* d5=output filt, d1=stereo switch */
    pThis->mixer_regs[0x0e] = 0;

    /* voice volume L d5,d7, R d1,d3 */
    pThis->mixer_regs[0x04] = (12 << 4) | 12;
    /* master ... */
    pThis->mixer_regs[0x22] = (12 << 4) | 12;
    /* MIDI ... */
    pThis->mixer_regs[0x26] = (12 << 4) | 12;

    /* master/voice/MIDI L/R volume */
    for (int i = 0x30; i < 0x36; i++)
        pThis->mixer_regs[i] = 24 << 3; /* -14 dB */

    /* treble/bass */
    for (int i = 0x44; i < 0x48; i++)
        pThis->mixer_regs[i] = 0x80;

    /* Update the master (mixer) and PCM out volumes. */
    sb16UpdateVolume(pThis);

    /*
     * Reset mixer sinks.
     *
     * Do the reset here instead of in sb16StreamReset();
     * the mixer sink(s) might still have data to be processed when an audio stream gets reset.
     */
    if (pThis->pSinkOut)
        AudioMixerSinkReset(pThis->pSinkOut);
}

static int magic_of_irq(int irq)
{
    switch (irq)
    {
        case 5:
            return 2;
        case 7:
            return 4;
        case 9:
            return 1;
        case 10:
            return 8;
        default:
            break;
    }

    LogFlowFunc(("bad irq %d\n", irq));
    return 2;
}

static int irq_of_magic(int magic)
{
    switch (magic)
    {
        case 1:
            return 9;
        case 2:
            return 5;
        case 4:
            return 7;
        case 8:
            return 10;
        default:
            break;
    }

    LogFlowFunc(("bad irq magic %d\n", magic));
    return -1;
}

static int sb16MixerWriteIndex(PSB16STATE pThis, PSB16STREAM pStream, uint8_t val)
{
    RT_NOREF(pStream);
    pThis->mixer_nreg = val;
    return VINF_SUCCESS;
}

#ifndef VBOX
static uint32_t popcount(uint32_t u)
{
    u = ((u&0x55555555) + ((u>>1)&0x55555555));
    u = ((u&0x33333333) + ((u>>2)&0x33333333));
    u = ((u&0x0f0f0f0f) + ((u>>4)&0x0f0f0f0f));
    u = ((u&0x00ff00ff) + ((u>>8)&0x00ff00ff));
    u = ( u&0x0000ffff) + (u>>16);
    return u;
}
#endif

static uint32_t lsbindex(uint32_t u)
{
#ifdef VBOX
    return u ? ASMBitFirstSetU32(u) - 1 : 32;
#else
    return popcount((u & -(int32_t)u) - 1);
#endif
}

/* Convert SB16 to SB Pro mixer volume (left). */
static inline void sb16ConvVolumeL(PSB16STATE pThis, unsigned reg, uint8_t val)
{
    /* High nibble in SBP mixer. */
    pThis->mixer_regs[reg] = (pThis->mixer_regs[reg] & 0x0f) | (val & 0xf0);
}

/* Convert SB16 to SB Pro mixer volume (right). */
static inline void sb16ConvVolumeR(PSB16STATE pThis, unsigned reg, uint8_t val)
{
    /* Low nibble in SBP mixer. */
    pThis->mixer_regs[reg] = (pThis->mixer_regs[reg] & 0xf0) | (val >> 4);
}

/* Convert SB Pro to SB16 mixer volume (left + right). */
static inline void sb16ConvVolumeOldToNew(PSB16STATE pThis, unsigned reg, uint8_t val)
{
    /* Left channel. */
    pThis->mixer_regs[reg + 0] = (val & 0xf0) | RT_BIT(3);
    /* Right channel (the register immediately following). */
    pThis->mixer_regs[reg + 1] = (val << 4)   | RT_BIT(3);
}


static int sb16MixerWriteData(PSB16STATE pThis, PSB16STREAM pStream, uint8_t val)
{
    bool        fUpdateMaster = false;
    bool        fUpdateStream = false;

    LogFlowFunc(("[%#x] <- %#x\n", pThis->mixer_nreg, val));

    switch (pThis->mixer_nreg)
    {
        case 0x00:
            sb16MixerReset(pThis);
            /* And update the actual volume, too. */
            fUpdateMaster = true;
            fUpdateStream = true;
            break;

        case 0x04:  /* Translate from old style voice volume (L/R). */
            sb16ConvVolumeOldToNew(pThis, 0x32, val);
            fUpdateStream = true;
            break;

        case 0x22:  /* Translate from old style master volume (L/R). */
            sb16ConvVolumeOldToNew(pThis, 0x30, val);
            fUpdateMaster = true;
            break;

        case 0x26:  /* Translate from old style MIDI volume (L/R). */
            sb16ConvVolumeOldToNew(pThis, 0x34, val);
            break;

        case 0x28:  /* Translate from old style CD volume (L/R). */
            sb16ConvVolumeOldToNew(pThis, 0x36, val);
            break;

        case 0x2E:  /* Translate from old style line volume (L/R). */
            sb16ConvVolumeOldToNew(pThis, 0x38, val);
            break;

        case 0x30:  /* Translate to old style master volume (L). */
            sb16ConvVolumeL(pThis, 0x22, val);
            fUpdateMaster = true;
            break;

        case 0x31:  /* Translate to old style master volume (R). */
            sb16ConvVolumeR(pThis, 0x22, val);
            fUpdateMaster = true;
            break;

        case 0x32:  /* Translate to old style voice volume (L). */
            sb16ConvVolumeL(pThis, 0x04, val);
            fUpdateStream = true;
            break;

        case 0x33:  /* Translate to old style voice volume (R). */
            sb16ConvVolumeR(pThis, 0x04, val);
            fUpdateStream = true;
            break;

        case 0x34:  /* Translate to old style MIDI volume (L). */
            sb16ConvVolumeL(pThis, 0x26, val);
            break;

        case 0x35:  /* Translate to old style MIDI volume (R). */
            sb16ConvVolumeR(pThis, 0x26, val);
            break;

        case 0x36:  /* Translate to old style CD volume (L). */
            sb16ConvVolumeL(pThis, 0x28, val);
            break;

        case 0x37:  /* Translate to old style CD volume (R). */
            sb16ConvVolumeR(pThis, 0x28, val);
            break;

        case 0x38:  /* Translate to old style line volume (L). */
            sb16ConvVolumeL(pThis, 0x2E, val);
            break;

        case 0x39:  /* Translate to old style line volume (R). */
            sb16ConvVolumeR(pThis, 0x2E, val);
            break;

        case 0x80:
        {
            int irq = irq_of_magic(val);
            LogRelMax2(64, ("SB16: Setting IRQ to %d\n", irq));
            if (irq > 0)
                pStream->HwCfgRuntime.uIrq = irq;
            break;
        }

        case 0x81:
        {
            int dma  = lsbindex(val & 0xf);
            int hdma = lsbindex(val & 0xf0);
            if (    dma != pStream->HwCfgRuntime.uDmaChanLow
                || hdma != pStream->HwCfgRuntime.uDmaChanHigh)
            {
                LogRelMax2(64, ("SB16: Attempt to change DMA 8bit %d(%d), 16bit %d(%d)\n",
                                dma, pStream->HwCfgRuntime.uDmaChanLow, hdma, pStream->HwCfgRuntime.uDmaChanHigh));
            }
#if 0
            pStream->dma = dma;
            pStream->hdma = hdma;
#endif
            break;
        }

        case 0x82:
            LogRelMax2(64, ("SB16: Attempt to write into IRQ status register to %#x\n", val));
            return VINF_SUCCESS;

        default:
            if (pThis->mixer_nreg >= 0x80)
                LogFlowFunc(("attempt to write mixer[%#x] <- %#x\n", pThis->mixer_nreg, val));
            break;
    }

    pThis->mixer_regs[pThis->mixer_nreg] = val;

    /* Update the master (mixer) volume. */
    if (   fUpdateMaster
        || fUpdateStream)
    {
        sb16UpdateVolume(pThis);
    }

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{PFNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) sb16IoPortMixerWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    RT_NOREF(pvUser);

    /** @todo Figure out how we can distinguish between streams. */
    PSB16STREAM pStream = &pThis->aStreams[SB16_IDX_OUT];

    switch (cb)
    {
        case 1:
            switch (offPort)
            {
                case 0:
                    sb16MixerWriteIndex(pThis, pStream, u32);
                    break;
                case 1:
                    sb16MixerWriteData(pThis, pStream, u32);
                    break;
                default:
                    AssertFailed();
            }
            break;
        case 2:
            sb16MixerWriteIndex(pThis, pStream, u32 & 0xff);
            sb16MixerWriteData(pThis, pStream, (u32 >> 8) & 0xff);
            break;
        default:
            ASSERT_GUEST_MSG_FAILED(("offPort=%#x cb=%d u32=%#x\n", offPort, cb, u32));
            break;
    }
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{PFNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) sb16IoPortMixerRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    RT_NOREF(pvUser, cb, offPort);

#ifndef DEBUG_SB16_MOST
    if (pThis->mixer_nreg != 0x82)
        LogFlowFunc(("sb16IoPortMixerRead[%#x] -> %#x\n", pThis->mixer_nreg, pThis->mixer_regs[pThis->mixer_nreg]));
#else
    LogFlowFunc(("sb16IoPortMixerRead[%#x] -> %#x\n", pThis->mixer_nreg, pThis->mixer_regs[pThis->mixer_nreg]));
#endif
    *pu32 = pThis->mixer_regs[pThis->mixer_nreg];
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   DMA handling                                                                                                                 *
*********************************************************************************************************************************/

/**
 * Worker for sb16DMARead.
 */

/**
 * @callback_method_impl{FNDMATRANSFERHANDLER,
 *      Worker callback for both DMA channels.}
 */
static DECLCALLBACK(uint32_t) sb16DMARead(PPDMDEVINS pDevIns, void *pvUser, unsigned uChannel, uint32_t off, uint32_t cb)

{
    PSB16STATE  pThis   = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    AssertPtr(pThis);
    PSB16STREAM pStream = (PSB16STREAM)pvUser;
    AssertPtr(pStream);

    int till, copy, free;

    if (pStream->cbDmaBlockSize <= 0)
    {
        LogFlowFunc(("invalid block size=%d uChannel=%d off=%d cb=%d\n", pStream->cbDmaBlockSize, uChannel, off, cb));
        return off;
    }

    if (pStream->cbDmaLeft < 0)
        pStream->cbDmaLeft = pStream->cbDmaBlockSize;

    free = cb;

    copy = free;
    till = pStream->cbDmaLeft;

    Log4Func(("pos=%d %d, till=%d, len=%d\n", off, free, till, cb));

    if (copy >= till)
    {
        if (0 == pStream->dma_auto)
        {
            copy = till;
        }
        else
        {
            if (copy >= till + pStream->cbDmaBlockSize)
                copy = till; /* Make sure we won't skip IRQs. */
        }
    }

    STAM_COUNTER_ADD(&pThis->StatBytesRead, copy);

    uint32_t written = 0; /* Shut up GCC. */
    int rc = sb16StreamDoDmaOutput(pThis, pStream, uChannel, off, cb, copy, &written);
    AssertRC(rc);

    /** @todo Convert the rest to uin32_t / size_t. */
    off = (off + (int)written) % cb;
    pStream->cbDmaLeft -= (int)written; /** @todo r=andy left_till_irq can be < 0. Correct? Revisit this. */

    Log3Func(("pos %d/%d, free=%d, till=%d, copy=%d, written=%RU32, block_size=%d\n",
              off, cb, free, pStream->cbDmaLeft, copy, copy, pStream->cbDmaBlockSize));

    if (pStream->cbDmaLeft <= 0)
    {
        pThis->mixer_regs[0x82] |= (uChannel & 4) ? 2 : 1;

        PDMDevHlpISASetIrq(pThis->pDevInsR3, pStream->HwCfgRuntime.uIrq, 1);

        if (0 == pStream->dma_auto) /** @todo r=andy BUGBUG Why do we first assert the IRQ if dma_auto is 0? Revisit this. */
        {
            sb16StreamControl(pDevIns, pThis, pStream, false /* fRun */);
            sb16SpeakerControl(pThis, false /* fOn */);
        }
    }

    while (pStream->cbDmaLeft <= 0)
        pStream->cbDmaLeft += pStream->cbDmaBlockSize;

    return off;
}


/*********************************************************************************************************************************
*   Timer-related code                                                                                                           *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{PFNTMTIMERDEV}
 */
static DECLCALLBACK(void) sb16TimerIRQ(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    RT_NOREF(hTimer, pThis);

    PSB16STREAM pStream = (PSB16STREAM)pvUser;
    AssertPtrReturnVoid(pStream);

    LogFlowFuncEnter();

    pStream->can_write = 1;
    PDMDevHlpISASetIrq(pDevIns, pStream->HwCfgRuntime.uIrq, 1);
}

/**
 * Sets the stream's I/O timer to a new expiration time.
 *
 * @param   pDevIns             The device instance.
 * @param   pStream             SB16 stream to set timer for.
 * @param   cTicksToDeadline    The number of ticks to the new deadline.
 */
DECLINLINE(void) sb16TimerSet(PPDMDEVINS pDevIns, PSB16STREAM pStream, uint64_t cTicksToDeadline)
{
    int rc = PDMDevHlpTimerSetRelative(pDevIns, pStream->hTimerIO, cTicksToDeadline, NULL /*pu64Now*/);
    AssertRC(rc);
}

/**
 * @callback_method_impl{FNTMTIMERDEV}
 */
static DECLCALLBACK(void) sb16TimerIO(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    STAM_PROFILE_START(&pThis->StatTimerIO, a);

    PSB16STREAM pStream = (PSB16STREAM)pvUser;
    AssertPtrReturnVoid(pStream);
    AssertReturnVoid(hTimer == pStream->hTimerIO);

    const uint64_t cTicksNow = PDMDevHlpTimerGet(pDevIns, pStream->hTimerIO);

    pStream->tsTimerIO = cTicksNow;

    PAUDMIXSINK pSink = sb16StreamIndexToSink(pThis, pStream->uIdx);
    AssertPtrReturnVoid(pSink);

    const bool fSinkActive = AudioMixerSinkIsActive(pSink);

    LogFlowFunc(("fSinkActive=%RTbool\n", fSinkActive));

    /* Schedule the next transfer. */
    PDMDevHlpDMASchedule(pDevIns);

    if (fSinkActive)
    {
        /** @todo adjust cTicks down by now much cbOutMin represents. */
        sb16TimerSet(pDevIns, pStream, pStream->cTicksTimerIOInterval);
    }

    AudioMixerSinkSignalUpdateJob(pSink);

    STAM_PROFILE_STOP(&pThis->StatTimerIO, a);
}


/*********************************************************************************************************************************
*   LUN (driver) management                                                                                                      *
*********************************************************************************************************************************/

/**
 * Retrieves a specific driver stream of a SB16 driver.
 *
 * @returns Pointer to driver stream if found, or NULL if not found.
 * @param   pDrv                Driver to retrieve driver stream for.
 * @param   enmDir              Stream direction to retrieve.
 * @param   enmPath             Stream destination / source to retrieve.
 */
static PSB16DRIVERSTREAM sb16GetDrvStream(PSB16DRIVER pDrv, PDMAUDIODIR enmDir, PDMAUDIOPATH enmPath)
{
    PSB16DRIVERSTREAM pDrvStream = NULL;

    if (enmDir == PDMAUDIODIR_OUT)
    {
        LogFunc(("enmPath=%d\n", enmPath));

        switch (enmPath)
        {
            case PDMAUDIOPATH_OUT_FRONT:
                pDrvStream = &pDrv->Out;
                break;
            default:
                AssertFailed();
                break;
        }
    }
    else
        Assert(enmDir == PDMAUDIODIR_IN /** @todo Recording not implemented yet. */);

    return pDrvStream;
}

/**
 * Adds a driver stream to a specific mixer sink.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pMixSink    Mixer sink to add driver stream to.
 * @param   pCfg        Stream configuration to use.
 * @param   pDrv        Driver stream to add.
 */
static int sb16AddDrvStream(PPDMDEVINS pDevIns, PAUDMIXSINK pMixSink, PCPDMAUDIOSTREAMCFG pCfg, PSB16DRIVER pDrv)
{
    AssertReturn(pCfg->enmDir == PDMAUDIODIR_OUT, VERR_NOT_IMPLEMENTED); /* We don't support recording for SB16 so far. */
    LogFunc(("[LUN#%RU8] %s\n", pDrv->uLUN, pCfg->szName));

    int rc;
    PSB16DRIVERSTREAM pDrvStream = sb16GetDrvStream(pDrv, pCfg->enmDir, pCfg->enmPath);
    if (pDrvStream)
    {
        AssertMsg(pDrvStream->pMixStrm == NULL, ("[LUN#%RU8] Driver stream already present when it must not\n", pDrv->uLUN));

        PAUDMIXSTREAM pMixStrm;
        rc = AudioMixerSinkCreateStream(pMixSink, pDrv->pConnector, pCfg, pDevIns, &pMixStrm);
        LogFlowFunc(("LUN#%RU8: Created stream \"%s\" for sink, rc=%Rrc\n", pDrv->uLUN, pCfg->szName, rc));
        if (RT_SUCCESS(rc))
        {
            rc = AudioMixerSinkAddStream(pMixSink, pMixStrm);
            LogFlowFunc(("LUN#%RU8: Added stream \"%s\" to sink, rc=%Rrc\n", pDrv->uLUN, pCfg->szName, rc));
            if (RT_SUCCESS(rc))
                pDrvStream->pMixStrm = pMixStrm;
            else
                AudioMixerStreamDestroy(pMixStrm, pDevIns, true /*fImmediate*/);
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Adds all current driver streams to a specific mixer sink.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The SB16 state.
 * @param   pMixSink    Mixer sink to add stream to.
 * @param   pCfg        Stream configuration to use.
 */
static int sb16AddDrvStreams(PPDMDEVINS pDevIns, PSB16STATE pThis, PAUDMIXSINK pMixSink, PCPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pMixSink, VERR_INVALID_POINTER);

    int rc;
    if (AudioHlpStreamCfgIsValid(pCfg))
    {
        rc = AudioMixerSinkSetFormat(pMixSink, &pCfg->Props, pCfg->Device.cMsSchedulingHint);
        if (RT_SUCCESS(rc))
        {
            PSB16DRIVER pDrv;
            RTListForEach(&pThis->lstDrv, pDrv, SB16DRIVER, Node)
            {
                int rc2 = sb16AddDrvStream(pDevIns, pMixSink, pCfg, pDrv);
                if (RT_FAILURE(rc2))
                    LogFunc(("Attaching stream failed with %Rrc\n", rc2));

                /* Do not pass failure to rc here, as there might be drivers which aren't
                 * configured / ready yet. */
            }
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Removes a driver stream from a specific mixer sink.
 *
 * @param   pDevIns     The device instance.
 * @param   pMixSink    Mixer sink to remove audio streams from.
 * @param   enmDir      Stream direction to remove.
 * @param   enmPath     Stream destination / source to remove.
 * @param   pDrv        Driver stream to remove.
 */
static void sb16RemoveDrvStream(PPDMDEVINS pDevIns, PAUDMIXSINK pMixSink, PDMAUDIODIR enmDir,
                                PDMAUDIOPATH enmPath, PSB16DRIVER pDrv)
{
    PSB16DRIVERSTREAM pDrvStream = sb16GetDrvStream(pDrv, enmDir, enmPath);
    if (pDrvStream)
    {
        if (pDrvStream->pMixStrm)
        {
            LogFlowFunc(("[LUN#%RU8]\n", pDrv->uLUN));

            AudioMixerSinkRemoveStream(pMixSink, pDrvStream->pMixStrm);

            AudioMixerStreamDestroy(pDrvStream->pMixStrm, pDevIns, false /*fImmediate*/);
            pDrvStream->pMixStrm = NULL;
        }
    }
}

/**
 * Removes all driver streams from a specific mixer sink.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The SB16 state.
 * @param   pMixSink    Mixer sink to remove audio streams from.
 * @param   enmDir      Stream direction to remove.
 * @param   enmPath     Stream destination / source to remove.
 */
static void sb16RemoveDrvStreams(PPDMDEVINS pDevIns, PSB16STATE pThis, PAUDMIXSINK pMixSink,
                                 PDMAUDIODIR enmDir, PDMAUDIOPATH enmPath)
{
    AssertPtrReturnVoid(pMixSink);

    PSB16DRIVER pDrv;
    RTListForEach(&pThis->lstDrv, pDrv, SB16DRIVER, Node)
    {
        sb16RemoveDrvStream(pDevIns, pMixSink, enmDir, enmPath, pDrv);
    }
}

/**
 * Adds a specific SB16 driver to the driver chain.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The SB16 device state.
 * @param   pDrv        The SB16 driver to add.
 */
static int sb16AddDrv(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16DRIVER pDrv)
{
    int rc = VINF_SUCCESS;

    for (unsigned i = 0; i < SB16_MAX_STREAMS; i++)
    {
        if (AudioHlpStreamCfgIsValid(&pThis->aStreams[i].Cfg))
        {
            int rc2 = sb16AddDrvStream(pDevIns, sb16StreamIndexToSink(pThis, pThis->aStreams[i].uIdx),
                                       &pThis->aStreams[i].Cfg, pDrv);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }

    return rc;
}

/**
 * Removes a specific SB16 driver from the driver chain and destroys its
 * associated streams.
 *
 * This is only used by sb16Detach.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The SB16 device state.
 * @param   pDrv        SB16 driver to remove.
 */
static void sb16RemoveDrv(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16DRIVER pDrv)
{
    RT_NOREF(pDevIns);

    /** @todo We only implement one single output (playback) stream at the moment. */

    if (pDrv->Out.pMixStrm)
    {
        AudioMixerSinkRemoveStream(pThis->pSinkOut, pDrv->Out.pMixStrm);
        AudioMixerStreamDestroy(pDrv->Out.pMixStrm, pDevIns, true /*fImmediate*/);
        pDrv->Out.pMixStrm = NULL;
    }

    RTListNodeRemove(&pDrv->Node);
}


/*********************************************************************************************************************************
*   Stream handling                                                                                                              *
*********************************************************************************************************************************/

/**
 * Reads DMA output data into the internal DMA buffer.
 *
 * @returns VBox status code.
 * @param   pThis       The SB16 state.
 * @param   pStream     The SB16 stream to read DMA output data from.
 * @param   uDmaChan    DMA channel to read from.
 * @param   offDma      DMA offset (in bytes) to start reading from.
 * @param   cbDma       DMA area size in bytes.
 * @param   cbToRead    How much bytes to read in total.
 * @param   pcbRead     Where to return the DMA bytes read.
 *
 * @thread  EMT
 */
static int sb16StreamDoDmaOutput(PSB16STATE pThis, PSB16STREAM pStream, int uDmaChan, uint32_t offDma, uint32_t cbDma,
                                 uint32_t cbToRead, uint32_t *pcbRead)
{
    uint32_t cbFree = (uint32_t)RTCircBufFree(pStream->State.pCircBuf);
    //Assert(cbToRead <= cbFree); /** @todo Add statistics for overflows. */
    cbToRead = RT_MIN(cbToRead, cbFree);

    sb16StreamLock(pStream);

    uint32_t cbReadTotal = 0;
    while (cbToRead)
    {
        void  *pv = NULL;
        size_t cb = 0;
        RTCircBufAcquireWriteBlock(pStream->State.pCircBuf, RT_MIN(cbDma - offDma, cbToRead), &pv, &cb);

        uint32_t cbRead = 0;
        int rc = PDMDevHlpDMAReadMemory(pThis->pDevInsR3, uDmaChan, pv, offDma, (uint32_t)cb, &cbRead);
        if (RT_SUCCESS(rc))
            Assert(cbRead == cb);
        else
        {
            AssertMsgFailed(("Reading from DMA failed: %Rrc (cbReadTotal=%#x)\n", rc, cbReadTotal));
            RTCircBufReleaseWriteBlock(pStream->State.pCircBuf, 0);
            if (cbReadTotal > 0)
                break;
            *pcbRead = 0;

            sb16StreamUnlock(pStream);
            return rc;
        }

        if (RT_LIKELY(!pStream->Dbg.Runtime.pFileDMA))
        { /* likely */ }
        else
            AudioHlpFileWrite(pStream->Dbg.Runtime.pFileDMA, pv, cbRead);

        Log3Func(("Writing %#RX32 bytes to DMA buffer\n", cbRead));

        RTCircBufReleaseWriteBlock(pStream->State.pCircBuf, cbRead);

        Log3Func(("Now %#RX32 bytes in buffer\n", RTCircBufUsed(pStream->State.pCircBuf)));

        Assert(cbToRead >= cbRead);
        pStream->State.offWrite += cbRead;
        offDma                   = (offDma + cbRead) % cbDma;
        cbReadTotal             += cbRead;
        cbToRead                -= cbRead;
    }

    *pcbRead = cbReadTotal;

    /* Update buffer stats. */
    pStream->State.StatDmaBufUsed = (uint32_t)RTCircBufUsed(pStream->State.pCircBuf);

    Log3Func(("[SD%RU8] cbCircBufUsed=%#RX64\n", pStream->uIdx, RTCircBufUsed(pStream->State.pCircBuf)));

    sb16StreamUnlock(pStream);
    return VINF_SUCCESS;
}

/**
 * Enables or disables a SB16 audio stream.
 *
 * @returns VBox status code.
 * @param   pThis       The SB16 state.
 * @param   pStream     The SB16 stream to enable or disable.
 * @param   fEnable     Whether to enable or disable the stream.
 * @param   fForce      Whether to force re-opening the stream or not.
 *                      Otherwise re-opening only will happen if the PCM properties have changed.
 */
static int sb16StreamEnable(PSB16STATE pThis, PSB16STREAM pStream, bool fEnable, bool fForce)
{
    if (   !fForce
        && fEnable == pStream->State.fEnabled)
        return VINF_SUCCESS;

    LogFlowFunc(("fEnable=%RTbool, fForce=%RTbool, fStreamEnabled=%RTbool\n", fEnable, fForce, pStream->State.fEnabled));

    PAUDMIXSINK pSink = sb16StreamIndexToSink(pThis, pStream->uIdx);
    AssertPtrReturn(pSink, VERR_INTERNAL_ERROR_2);

    /* We only need to register the AIO update job the first time around as the requence doesn't change. */
    int rc;
    if (fEnable && !pStream->State.fRegisteredAsyncUpdateJob)
    {
        rc = AudioMixerSinkAddUpdateJob(pSink, sb16StreamUpdateAsyncIoJob, pStream, RT_MS_1SEC / pStream->uTimerHz);
        AssertRC(rc);
        pStream->State.fRegisteredAsyncUpdateJob = RT_SUCCESS(rc) || rc == VERR_ALREADY_EXISTS;
    }

    /* Tell the mixer. */
    if (fEnable)
    {
        rc = AudioMixerSinkStart(pSink);
        AssertRCReturn(rc, rc);
    }
    else
    {
        /* We need to lock the stream here in order to synchronize the amount of bytes we have to drain
         * here with the mixer's async I/O thread.  See @bugref{10354}. */
        sb16StreamLock(pStream);

        uint32_t const cbToDrain = pStream->State.pCircBuf ? (uint32_t)RTCircBufUsed(pStream->State.pCircBuf) : 0;
        Log3Func(("cbToDrain=%#RX32\n", cbToDrain));

        sb16StreamUnlock(pStream);

        rc = AudioMixerSinkDrainAndStop(pSink, cbToDrain);
        AssertRCReturn(rc, rc);


    }

    pStream->State.fEnabled = fEnable;

    return rc;
}

/**
 * Retrieves the audio mixer sink of a corresponding SB16 stream.
 *
 * @returns Pointer to audio mixer sink if found, or NULL if not found / invalid.
 * @param   pThis               The SB16 state.
 * @param   uIdx                Stream index to get audio mixer sink for.
 */
DECLINLINE(PAUDMIXSINK) sb16StreamIndexToSink(PSB16STATE pThis, uint8_t uIdx)
{
    AssertReturn(uIdx <= SB16_MAX_STREAMS, NULL);

    /* Dead simple for now; make this more sophisticated if we have more stuff to cover. */
    if (uIdx == SB16_IDX_OUT)
        return pThis->pSinkOut; /* Can be NULL if not configured / set up yet. */

    AssertMsgFailed(("No sink attached (yet) for index %RU8\n", uIdx));
    return NULL;
}

/**
 * Returns the audio direction of a specified stream descriptor.
 *
 * @returns Audio direction.
 * @param   uIdx                Stream index to get audio direction for.
 */
DECLINLINE(PDMAUDIODIR) sb16GetDirFromIndex(uint8_t uIdx)
{
    AssertReturn(uIdx <= SB16_MAX_STREAMS, PDMAUDIODIR_INVALID);

    /* Dead simple for now; make this more sophisticated if we have more stuff to cover. */
    if (uIdx == SB16_IDX_OUT)
        return PDMAUDIODIR_OUT;

    return PDMAUDIODIR_INVALID;
}

/**
 * Creates a SB16 audio stream.
 *
 * @returns VBox status code.
 * @param   pThis               The SB16 state.
 * @param   pStream             The SB16 stream to create.
 * @param   uIdx                Stream index to assign.
 */
static int sb16StreamCreate(PSB16STATE pThis, PSB16STREAM pStream, uint8_t uIdx)
{
    LogFlowFuncEnter();

    int rc = RTCritSectInit(&pStream->State.CritSect);
    AssertRCReturn(rc, rc);

    pStream->Dbg.Runtime.fEnabled = pThis->Dbg.fEnabled;

    if (RT_LIKELY(!pStream->Dbg.Runtime.fEnabled))
    { /* likely */ }
    else
    {
        int rc2 = AudioHlpFileCreateF(&pStream->Dbg.Runtime.pFileDMA, AUDIOHLPFILE_FLAGS_NONE, AUDIOHLPFILETYPE_WAV,
                                      pThis->Dbg.pszOutPath, AUDIOHLPFILENAME_FLAGS_NONE, 0 /*uInstance*/,
                                      sb16GetDirFromIndex(pStream->uIdx) == PDMAUDIODIR_IN
                                      ? "sb16StreamWriteSD%RU8" : "sb16StreamReadSD%RU8", pStream->uIdx);
        AssertRC(rc2);

        /* Delete stale debugging files from a former run. */
        AudioHlpFileDelete(pStream->Dbg.Runtime.pFileDMA);
    }

    pStream->uIdx = uIdx;

    return VINF_SUCCESS;
}

/**
 * Destroys a SB16 audio stream.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The SB16 state.
 * @param   pStream             The SB16 stream to destroy.
 */
static int sb16StreamDestroy(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream)
{
    LogFlowFuncEnter();

    sb16StreamClose(pDevIns, pThis, pStream);

    if (pStream->State.fRegisteredAsyncUpdateJob)
    {
        PAUDMIXSINK pSink = sb16StreamIndexToSink(pThis, pStream->uIdx);
        if (pSink)
            AudioMixerSinkRemoveUpdateJob(pSink, sb16StreamUpdateAsyncIoJob, pStream);
        pStream->State.fRegisteredAsyncUpdateJob = false;
    }

    int rc2 = RTCritSectDelete(&pStream->State.CritSect);
    AssertRC(rc2);

    if (pStream->State.pCircBuf)
    {
        RTCircBufDestroy(pStream->State.pCircBuf);
        pStream->State.pCircBuf = NULL;
    }

    if (RT_LIKELY(!pStream->Dbg.Runtime.fEnabled))
    { /* likely */ }
    else
    {
        AudioHlpFileDestroy(pStream->Dbg.Runtime.pFileDMA);
        pStream->Dbg.Runtime.pFileDMA = NULL;
    }

    pStream->uIdx = UINT8_MAX;

    return VINF_SUCCESS;
}

/**
 * Resets a SB16 stream.
 *
 * @param   pThis               The SB16 state.
 * @param   pStream             The SB16 stream to reset.
 */
static void sb16StreamReset(PSB16STATE pThis, PSB16STREAM pStream)
{
    LogFlowFuncEnter();

    PDMDevHlpISASetIrq(pThis->pDevInsR3, pStream->HwCfgRuntime.uIrq, 0);
    if (pStream->dma_auto)
    {
        PDMDevHlpISASetIrq(pThis->pDevInsR3, pStream->HwCfgRuntime.uIrq, 1);
        PDMDevHlpISASetIrq(pThis->pDevInsR3, pStream->HwCfgRuntime.uIrq, 0);

        pStream->dma_auto = 0;
    }

    sb16StreamControl(pThis->pDevInsR3, pThis, pStream, false /* fRun */);
    sb16StreamEnable(pThis, pStream, false /* fEnable */, false /* fForce */);

    switch (pStream->uIdx)
    {
        case SB16_IDX_OUT:
        {
            pStream->Cfg.enmDir    = PDMAUDIODIR_OUT;
            pStream->Cfg.enmPath   = PDMAUDIOPATH_OUT_FRONT;

            PDMAudioPropsInit(&pStream->Cfg.Props, 1 /* 8-bit */, false /* fSigned */, 1 /* Mono */, 11025 /* uHz */);
            RTStrCopy(pStream->Cfg.szName, sizeof(pStream->Cfg.szName), "Output");
            break;
        }

        default:
            AssertFailed();
            break;
    }

    pStream->cbDmaLeft      = 0;
    pStream->cbDmaBlockSize = 0;
    pStream->can_write      = 1; /** @ŧodo r=andy BUGBUG Figure out why we (still) need this. */

    /** @todo Also reset corresponding DSP values here? */
}

/**
 * Locks a SB16 stream for serialized access.
 *
 * @param   pStream             The SB16 stream to lock.
 */
DECLINLINE(void) sb16StreamLock(PSB16STREAM pStream)
{
    int rc2 = RTCritSectEnter(&pStream->State.CritSect);
    AssertRC(rc2);
}

/**
 * Unlocks a formerly locked SB16 stream.
 *
 * @param   pStream             The SB16 stream to unlock.
 */
DECLINLINE(void) sb16StreamUnlock(PSB16STREAM pStream)
{
    int rc2 = RTCritSectLeave(&pStream->State.CritSect);
    AssertRC(rc2);
}

/**
 * Opens a SB16 stream with its current mixer settings.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The SB16 device state.
 * @param   pStream     The SB16 stream to open.
 *
 * @note    This currently only supports the one and only output stream.
 */
static int sb16StreamOpen(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream)
{
    LogFlowFuncEnter();
    AssertLogRelReturn(PDMAudioPropsAreValid(&pStream->Cfg.Props), VERR_INTERNAL_ERROR_5);

    switch (pStream->uIdx)
    {
        case SB16_IDX_OUT:
            pStream->Cfg.enmDir      = PDMAUDIODIR_OUT;
            pStream->Cfg.enmPath     = PDMAUDIOPATH_OUT_FRONT;

            RTStrCopy(pStream->Cfg.szName, sizeof(pStream->Cfg.szName), "Output");
            break;

        default:
            AssertFailed();
            break;
    }

    LogRel2(("SB16: (Re-)Opening stream '%s' (%RU32Hz, %RU8 channels, %s%RU8)\n", pStream->Cfg.szName, pStream->Cfg.Props.uHz,
             PDMAudioPropsChannels(&pStream->Cfg.Props), pStream->Cfg.Props.fSigned ? "S" : "U",
             PDMAudioPropsSampleBits(&pStream->Cfg.Props)));

    /** @todo r=bird: two DMA periods is probably too little. */
    const uint32_t cbCircBuf = PDMAudioPropsMilliToBytes(&pStream->Cfg.Props,
                                                         (RT_MS_1SEC / pStream->uTimerHz) * 2 /* Use double buffering here */);
    int rc = VINF_SUCCESS;

    /**
     * Note: Only re-create the DMA buffer if the size actually has changed.
     *
     *       Otherwise do *not* reset the stream's circular buffer here, as the audio mixer still relies on
     *       previously announced DMA data (via AudioMixerSinkDrainAndStop()) and processes it asynchronously.
     *       Resetting the buffer here will cause a race condition.  See @bugref{10354}. */
    if (pStream->State.StatDmaBufSize != cbCircBuf)
    {
        Log3Func(("Re-creating ring buffer (%#RX32 -> %#RX32)\n", pStream->State.StatDmaBufSize, cbCircBuf));

        /* (Re-)create the stream's internal ring buffer. */
        if (pStream->State.pCircBuf)
        {
            RTCircBufDestroy(pStream->State.pCircBuf);
            pStream->State.pCircBuf = NULL;
        }

        rc = RTCircBufCreate(&pStream->State.pCircBuf, cbCircBuf);
        AssertRCReturn(rc, rc);
        pStream->State.StatDmaBufSize = (uint32_t)RTCircBufSize(pStream->State.pCircBuf);

        /* Set scheduling hint. */
        pStream->Cfg.Device.cMsSchedulingHint = RT_MS_1SEC / RT_MIN(pStream->uTimerHz, 1);

        PAUDMIXSINK pMixerSink = sb16StreamIndexToSink(pThis, pStream->uIdx);
        AssertPtrReturn(pMixerSink, VERR_INVALID_POINTER);

        sb16RemoveDrvStreams(pDevIns, pThis,
                             sb16StreamIndexToSink(pThis, pStream->uIdx), pStream->Cfg.enmDir, pStream->Cfg.enmPath);

        rc = sb16AddDrvStreams(pDevIns, pThis, pMixerSink, &pStream->Cfg);
        if (RT_SUCCESS(rc))
        {
            if (RT_LIKELY(!pStream->Dbg.Runtime.fEnabled))
            { /* likely */ }
            else
            {
                /* Make sure to close + delete a former debug file, as the PCM format has changed (e.g. U8 -> S16). */
                if (AudioHlpFileIsOpen(pStream->Dbg.Runtime.pFileDMA))
                {
                    AudioHlpFileClose(pStream->Dbg.Runtime.pFileDMA);
                    AudioHlpFileDelete(pStream->Dbg.Runtime.pFileDMA);
                }

                int rc2 = AudioHlpFileOpen(pStream->Dbg.Runtime.pFileDMA, AUDIOHLPFILE_DEFAULT_OPEN_FLAGS,
                                           &pStream->Cfg.Props);
                AssertRC(rc2);
            }
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Closes a SB16 stream.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           SB16 state.
 * @param   pStream         The SB16 stream to close.
 */
static void sb16StreamClose(PPDMDEVINS pDevIns, PSB16STATE pThis, PSB16STREAM pStream)
{
    RT_NOREF(pDevIns, pThis, pStream);

    LogFlowFuncEnter();

    /* Nothing to do in here right now. */
}

static void sb16StreamTransferScheduleNext(PSB16STATE pThis, PSB16STREAM pStream, uint32_t cbBytes)
{
    RT_NOREF(pStream);

    uint64_t const uTimerHz = PDMDevHlpTimerGetFreq(pThis->pDevInsR3, pThis->hTimerIRQ);

    const uint64_t usBytes        = PDMAudioPropsBytesToMicro(&pStream->Cfg.Props, cbBytes);
    const uint64_t cTransferTicks = PDMDevHlpTimerFromMicro(pThis->pDevInsR3, pThis->hTimerIRQ, usBytes);

    LogFlowFunc(("%RU32 bytes -> %RU64 ticks\n", cbBytes, cTransferTicks));

    if (cTransferTicks < uTimerHz / 1024) /** @todo Explain this. */
    {
        LogFlowFunc(("IRQ\n"));
        PDMDevHlpISASetIrq(pThis->pDevInsR3, pStream->HwCfgRuntime.uIrq, 1);
    }
    else
    {
        LogFlowFunc(("Scheduled\n"));
        PDMDevHlpTimerSetRelative(pThis->pDevInsR3, pThis->hTimerIRQ, cTransferTicks, NULL);
    }
}


/**
 * Output streams: Pushes data to the mixer.
 *
 * @param   pStream         The SB16 stream.
 * @param   pSink           The mixer sink to push to.
 *
 * @thread  MixAIO-n
 *
 * @note    Takes the stream's lock.
 */
static void sb16StreamPushToMixer(PSB16STREAM pStream, PAUDMIXSINK pSink)
{
    sb16StreamLock(pStream);

#ifdef LOG_ENABLED
    uint64_t const offReadOld = pStream->State.offRead;
    Log3Func(("[SD%RU8] cbCircBufUsed=%#RX64\n", pStream->uIdx, RTCircBufUsed(pStream->State.pCircBuf)));
#endif
    pStream->State.offRead = AudioMixerSinkTransferFromCircBuf(pSink,
                                                               pStream->State.pCircBuf,
                                                               pStream->State.offRead,
                                                               pStream->uIdx,
                                                               /** @todo pStream->Dbg.Runtime.fEnabled
                                                               ? pStream->Dbg.Runtime.pFileStream :*/ NULL);

    Log3Func(("[SD%RU8] transferred=%#RX64 bytes -> @%#RX64 (cbCircBufUsed=%#RX64)\n", pStream->uIdx,
              pStream->State.offRead - offReadOld, pStream->State.offRead, RTCircBufUsed(pStream->State.pCircBuf)));

    /* Update buffer stats. */
    pStream->State.StatDmaBufUsed = (uint32_t)RTCircBufUsed(pStream->State.pCircBuf);

    sb16StreamUnlock(pStream);
}


/**
 * @callback_method_impl{FNAUDMIXSINKUPDATE}
 *
 * For output streams this moves data from the internal DMA buffer (in which
 * sb16StreamDoDmaOutput put it), thru the mixer and to the various backend
 * audio devices.
 *
 * @thread  MixAIO-n
 */
static DECLCALLBACK(void) sb16StreamUpdateAsyncIoJob(PPDMDEVINS pDevIns, PAUDMIXSINK pSink, void *pvUser)
{
    PSB16STATE const  pThis   = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    PSB16STREAM const pStream = (PSB16STREAM)pvUser;
    Assert(pStream->uIdx == (uintptr_t)(pStream - &pThis->aStreams[0]));
    Assert(pSink == sb16StreamIndexToSink(pThis, pStream->uIdx));
    RT_NOREF(pThis);

    /*
     * Output.
     */
    if (sb16GetDirFromIndex(pStream->uIdx) == PDMAUDIODIR_OUT)
        sb16StreamPushToMixer(pStream, pSink);
    /*
     * No input streams at present.
     */
    else
        AssertFailed();
}


/*********************************************************************************************************************************
*   Saved state handling                                                                                                         *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) sb16LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    RT_NOREF(uPass);

    PSB16STATE    pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;

    /** Currently the saved state only contains the one-and-only output stream. */
    PSB16STREAM pStream = &pThis->aStreams[SB16_IDX_OUT];

    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgDefault.uIrq);
    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgDefault.uDmaChanLow);
    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgDefault.uDmaChanHigh);
    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgDefault.uPort);
    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgDefault.uVer);
    return VINF_SSM_DONT_CALL_AGAIN;
}

/**
 * Worker for sb16SaveExec.
 */
static int sb16Save(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PSB16STATE pThis)
{
    /* The saved state only contains the one-and-only output stream. */
    PSB16STREAM pStream = &pThis->aStreams[SB16_IDX_OUT];

    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgRuntime.uIrq);
    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgRuntime.uDmaChanLow);
    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgRuntime.uDmaChanHigh);
    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgRuntime.uPort);
    pHlp->pfnSSMPutS32(pSSM, pStream->HwCfgRuntime.uVer);
    pHlp->pfnSSMPutS32(pSSM, pThis->dsp_in_idx);
    pHlp->pfnSSMPutS32(pSSM, pThis->dsp_out_data_len);

    pHlp->pfnSSMPutS32(pSSM, PDMAudioPropsChannels(&pStream->Cfg.Props) >= 2 ? 1 : 0);
    pHlp->pfnSSMPutS32(pSSM, PDMAudioPropsIsSigned(&pStream->Cfg.Props)      ? 1 : 0);
    pHlp->pfnSSMPutS32(pSSM, PDMAudioPropsSampleBits(&pStream->Cfg.Props));
    pHlp->pfnSSMPutU32(pSSM, 0); /* Legacy; was PDMAUDIOFMT, unused now. */

    pHlp->pfnSSMPutS32(pSSM, pStream->dma_auto);
    pHlp->pfnSSMPutS32(pSSM, pStream->cbDmaBlockSize);
    pHlp->pfnSSMPutS32(pSSM, pStream->fifo);
    pHlp->pfnSSMPutS32(pSSM, PDMAudioPropsHz(&pStream->Cfg.Props));
    pHlp->pfnSSMPutS32(pSSM, pStream->time_const);
    pHlp->pfnSSMPutS32(pSSM, 0); /* Legacy; was speaker control (on/off) for output stream. */
    pHlp->pfnSSMPutS32(pSSM, pThis->dsp_in_needed_bytes);
    pHlp->pfnSSMPutS32(pSSM, pThis->cmd);
    pHlp->pfnSSMPutS32(pSSM, pStream->fDmaUseHigh);
    pHlp->pfnSSMPutS32(pSSM, pThis->highspeed);
    pHlp->pfnSSMPutS32(pSSM, pStream->can_write);
    pHlp->pfnSSMPutS32(pSSM, pThis->v2x6);

    pHlp->pfnSSMPutU8 (pSSM, pThis->csp_param);
    pHlp->pfnSSMPutU8 (pSSM, pThis->csp_value);
    pHlp->pfnSSMPutU8 (pSSM, pThis->csp_mode);
    pHlp->pfnSSMPutU8 (pSSM, pThis->csp_param); /* Bug compatible! */
    pHlp->pfnSSMPutMem(pSSM, pThis->csp_regs, 256);
    pHlp->pfnSSMPutU8 (pSSM, pThis->csp_index);
    pHlp->pfnSSMPutMem(pSSM, pThis->csp_reg83, 4);
    pHlp->pfnSSMPutS32(pSSM, pThis->csp_reg83r);
    pHlp->pfnSSMPutS32(pSSM, pThis->csp_reg83w);

    pHlp->pfnSSMPutMem(pSSM, pThis->dsp_in_data, sizeof(pThis->dsp_in_data));
    pHlp->pfnSSMPutMem(pSSM, pThis->dsp_out_data, sizeof(pThis->dsp_out_data));
    pHlp->pfnSSMPutU8 (pSSM, pThis->test_reg);
    pHlp->pfnSSMPutU8 (pSSM, pThis->last_read_byte);

    pHlp->pfnSSMPutS32(pSSM, pThis->nzero);
    pHlp->pfnSSMPutS32(pSSM, pStream->cbDmaLeft);
    pHlp->pfnSSMPutS32(pSSM, pStream->State.fEnabled ? 1 : 0);
    /* The stream's bitrate. Needed for backwards (legacy) compatibility. */
    pHlp->pfnSSMPutS32(pSSM, AudioHlpCalcBitrate(PDMAudioPropsSampleBits(&pThis->aStreams[SB16_IDX_OUT].Cfg.Props),
                                                 PDMAudioPropsHz(&pThis->aStreams[SB16_IDX_OUT].Cfg.Props),
                                                 PDMAudioPropsChannels(&pThis->aStreams[SB16_IDX_OUT].Cfg.Props)));
    /* Block size alignment, superfluous and thus not saved anymore. Needed for backwards (legacy) compatibility. */
    pHlp->pfnSSMPutS32(pSSM, 0);

    pHlp->pfnSSMPutS32(pSSM, pThis->mixer_nreg);
    return pHlp->pfnSSMPutMem(pSSM, pThis->mixer_regs, 256);
}

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) sb16SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    sb16LiveExec(pDevIns, pSSM, 0);
    return sb16Save(pHlp, pSSM, pThis);
}

/**
 * Worker for sb16LoadExec.
 */
static int sb16Load(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, PSB16STATE pThis)
{
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;
    PSB16STREAM pStream = &pThis->aStreams[SB16_IDX_OUT]; /* The saved state only contains the one-and-only output stream. */
    int rc;

    int32_t i32Tmp;
    pHlp->pfnSSMGetS32(pSSM, &i32Tmp);
    pStream->HwCfgRuntime.uIrq = i32Tmp;                        /* IRQ. */
    pHlp->pfnSSMGetS32(pSSM, &i32Tmp);
    pStream->HwCfgRuntime.uDmaChanLow = i32Tmp;                 /* Low (8-bit) DMA channel. */
    pHlp->pfnSSMGetS32(pSSM, &i32Tmp);
    pStream->HwCfgRuntime.uDmaChanHigh = i32Tmp;                /* High (16-bit) DMA channel. */
    pHlp->pfnSSMGetS32(pSSM, &i32Tmp);                          /* Used I/O port. */
    pStream->HwCfgRuntime.uPort = i32Tmp;
    pHlp->pfnSSMGetS32(pSSM, &i32Tmp);                          /* DSP version running. */
    pStream->HwCfgRuntime.uVer  = i32Tmp;
    pHlp->pfnSSMGetS32(pSSM, &pThis->dsp_in_idx);
    pHlp->pfnSSMGetS32(pSSM, &pThis->dsp_out_data_len);

    rc = pHlp->pfnSSMGetS32(pSSM, &i32Tmp);                     /* Output stream: Number of channels. */
    AssertRCReturn(rc, rc);
    AssertReturn((uint32_t)i32Tmp <= 1, VERR_INVALID_PARAMETER); /* Paranoia. */
    if (i32Tmp) /* PDMAudioPropsSetChannels() will assert if channels are 0 (will be re-set on DMA run command). */
        PDMAudioPropsSetChannels(&pStream->Cfg.Props, (uint8_t)i32Tmp);
    pHlp->pfnSSMGetS32(pSSM, &i32Tmp);                          /* Output stream: Signed format bit. */
    pStream->Cfg.Props.fSigned = i32Tmp != 0;
    rc = pHlp->pfnSSMGetS32(pSSM, &i32Tmp);                     /* Output stream: Sample size in bits. */
    AssertRCReturn(rc, rc);
    if (i32Tmp) /* PDMAudioPropsSetSampleSize() will assert if sample size is 0 (will be re-set on DMA run command). */
        PDMAudioPropsSetSampleSize(&pStream->Cfg.Props, (uint8_t)(i32Tmp / 8));

    pHlp->pfnSSMSkip  (pSSM, sizeof(int32_t));                  /* Legacy; was PDMAUDIOFMT, unused now. */
    pHlp->pfnSSMGetS32(pSSM, &pStream->dma_auto);
    pHlp->pfnSSMGetS32(pSSM, &pStream->cbDmaBlockSize);
    pHlp->pfnSSMGetS32(pSSM, &pStream->fifo);
    pHlp->pfnSSMGetS32(pSSM, &i32Tmp); pStream->Cfg.Props.uHz = i32Tmp;
    pHlp->pfnSSMGetS32(pSSM, &pStream->time_const);
    pHlp->pfnSSMSkip  (pSSM, sizeof(int32_t));                  /* Legacy; was speaker (on / off) for output stream. */
    pHlp->pfnSSMGetS32(pSSM, &pThis->dsp_in_needed_bytes);
    pHlp->pfnSSMGetS32(pSSM, &pThis->cmd);
    pHlp->pfnSSMGetS32(pSSM, &pStream->fDmaUseHigh);            /* Output stream: Whether to use the high or low DMA channel. */
    pHlp->pfnSSMGetS32(pSSM, &pThis->highspeed);
    pHlp->pfnSSMGetS32(pSSM, &pStream->can_write);
    pHlp->pfnSSMGetS32(pSSM, &pThis->v2x6);

    pHlp->pfnSSMGetU8 (pSSM, &pThis->csp_param);
    pHlp->pfnSSMGetU8 (pSSM, &pThis->csp_value);
    pHlp->pfnSSMGetU8 (pSSM, &pThis->csp_mode);
    pHlp->pfnSSMGetU8 (pSSM, &pThis->csp_param);                /* Bug compatible! */
    pHlp->pfnSSMGetMem(pSSM, pThis->csp_regs, 256);
    pHlp->pfnSSMGetU8 (pSSM, &pThis->csp_index);
    pHlp->pfnSSMGetMem(pSSM, pThis->csp_reg83, 4);
    pHlp->pfnSSMGetS32(pSSM, &pThis->csp_reg83r);
    pHlp->pfnSSMGetS32(pSSM, &pThis->csp_reg83w);

    pHlp->pfnSSMGetMem(pSSM, pThis->dsp_in_data, sizeof(pThis->dsp_in_data));
    pHlp->pfnSSMGetMem(pSSM, pThis->dsp_out_data, sizeof(pThis->dsp_out_data));
    pHlp->pfnSSMGetU8 (pSSM, &pThis->test_reg);
    pHlp->pfnSSMGetU8 (pSSM, &pThis->last_read_byte);

    pHlp->pfnSSMGetS32(pSSM, &pThis->nzero);
    pHlp->pfnSSMGetS32(pSSM, &pStream->cbDmaLeft);
    pHlp->pfnSSMGetS32(pSSM, &i32Tmp);                          /* Output stream: DMA currently running bit. */
    const bool fStreamEnabled = i32Tmp != 0;
    pHlp->pfnSSMSkip  (pSSM, sizeof(int32_t));                  /* Legacy; was the output stream's current bitrate (in bytes). */
    pHlp->pfnSSMSkip  (pSSM, sizeof(int32_t));                  /* Legacy; was the output stream's DMA block alignment. */

    int32_t mixer_nreg = 0;
    rc = pHlp->pfnSSMGetS32(pSSM, &mixer_nreg);
    AssertRCReturn(rc, rc);
    pThis->mixer_nreg = (uint8_t)mixer_nreg;
    rc = pHlp->pfnSSMGetMem(pSSM, pThis->mixer_regs, 256);
    AssertRCReturn(rc, rc);

    if (fStreamEnabled)
    {
        /* Sanity: If stream is going be enabled, PCM props must be valid. Otherwise the saved state is borked somehow. */
        AssertMsgReturn(AudioHlpPcmPropsAreValidAndSupported(&pStream->Cfg.Props),
                        ("PCM properties for stream #%RU8 are invalid\n", pStream->uIdx), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
        sb16StreamControl(pDevIns, pThis, pStream, true /* fRun */);
    }

    /* Update the master (mixer) and PCM out volumes. */
    sb16UpdateVolume(pThis);

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) sb16LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PSB16STATE    pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;

    AssertMsgReturn(    uVersion == SB16_SAVE_STATE_VERSION
                    ||  uVersion == SB16_SAVE_STATE_VERSION_VBOX_30,
                    ("%u\n", uVersion),
                    VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);
    if (uVersion > SB16_SAVE_STATE_VERSION_VBOX_30)
    {
        /** Currently the saved state only contains the one-and-only output stream. */
        PSB16STREAM pStream = &pThis->aStreams[SB16_IDX_OUT];

        int32_t irq;
        pHlp->pfnSSMGetS32(pSSM, &irq);
        int32_t dma;
        pHlp->pfnSSMGetS32(pSSM, &dma);
        int32_t hdma;
        pHlp->pfnSSMGetS32(pSSM, &hdma);
        int32_t port;
        pHlp->pfnSSMGetS32(pSSM, &port);
        int32_t ver;
        int rc = pHlp->pfnSSMGetS32(pSSM, &ver);
        AssertRCReturn (rc, rc);

        if (   irq  != pStream->HwCfgDefault.uIrq
            || dma  != pStream->HwCfgDefault.uDmaChanLow
            || hdma != pStream->HwCfgDefault.uDmaChanHigh
            || port != pStream->HwCfgDefault.uPort
            || ver  != pStream->HwCfgDefault.uVer)
        {
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                           N_("config changed: irq=%x/%x dma=%x/%x hdma=%x/%x port=%x/%x ver=%x/%x (saved/config)"),
                                           irq,  pStream->HwCfgDefault.uIrq,
                                           dma,  pStream->HwCfgDefault.uDmaChanLow,
                                           hdma, pStream->HwCfgDefault.uDmaChanHigh,
                                           port, pStream->HwCfgDefault.uPort,
                                           ver,  pStream->HwCfgDefault.uVer);
        }
    }

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    return sb16Load(pDevIns, pSSM, pThis);
}


/*********************************************************************************************************************************
*   Debug Info Items                                                                                                             *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, sb16mixer}
 */
static DECLCALLBACK(void) sb16DbgInfoMixer(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    if (pThis->pMixer)
        AudioMixerDebug(pThis->pMixer, pHlp, pszArgs);
    else
        pHlp->pfnPrintf(pHlp, "Mixer not available\n");
}


/*********************************************************************************************************************************
*   IBase implementation                                                                                                         *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) sb16QueryInterface(struct PDMIBASE *pInterface, const char *pszIID)
{
    PSB16STATE pThis = RT_FROM_MEMBER(pInterface, SB16STATE, IBase);
    Assert(&pThis->IBase == pInterface);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThis->IBase);
    return NULL;
}


/*********************************************************************************************************************************
*   Device (PDM) handling                                                                                                        *
*********************************************************************************************************************************/

/**
 * Worker for sb16Construct() and sb16Attach().
 *
 * @returns VBox status code.
 * @param   pThis       SB16 state.
 * @param   uLUN        The logical unit which is being detached.
 * @param   ppDrv       Attached driver instance on success. Optional.
 */
static int sb16AttachInternal(PSB16STATE pThis, unsigned uLUN, PSB16DRIVER *ppDrv)
{
    /*
     * Allocate a new driver structure and try attach the driver.
     */
    PSB16DRIVER pDrv = (PSB16DRIVER)RTMemAllocZ(sizeof(SB16DRIVER));
    AssertPtrReturn(pDrv, VERR_NO_MEMORY);
    RTStrPrintf(pDrv->szDesc, sizeof(pDrv->szDesc), "Audio driver port (SB16) for LUN #%u", uLUN);

    PPDMIBASE pDrvBase;
    int rc = PDMDevHlpDriverAttach(pThis->pDevInsR3, uLUN, &pThis->IBase, &pDrvBase, pDrv->szDesc);
    if (RT_SUCCESS(rc))
    {
        pDrv->pConnector = PDMIBASE_QUERY_INTERFACE(pDrvBase, PDMIAUDIOCONNECTOR);
        AssertPtr(pDrv->pConnector);
        if (RT_VALID_PTR(pDrv->pConnector))
        {
            pDrv->pDrvBase   = pDrvBase;
            pDrv->pSB16State = pThis;
            pDrv->uLUN       = uLUN;

            /* Attach to driver list if not attached yet. */
            if (!pDrv->fAttached)
            {
                RTListAppend(&pThis->lstDrv, &pDrv->Node);
                pDrv->fAttached = true;
            }

            if (ppDrv)
                *ppDrv = pDrv;
            LogFunc(("LUN#%u: returns VINF_SUCCESS (pCon=%p)\n", uLUN, pDrv->pConnector));
            return VINF_SUCCESS;
        }
        rc = VERR_PDM_MISSING_INTERFACE_BELOW;
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        LogFunc(("No attached driver for LUN #%u\n", uLUN));
    else
        LogFunc(("Failed to attached driver for LUN #%u: %Rrc\n", uLUN, rc));
    RTMemFree(pDrv);

    LogFunc(("LUN#%u: rc=%Rrc\n", uLUN, rc));
    return rc;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnAttach}
 */
static DECLCALLBACK(int) sb16Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    RT_NOREF(fFlags);
    LogFunc(("iLUN=%u, fFlags=%#x\n", iLUN, fFlags));

    /** @todo r=andy Any locking required here? */

    PSB16DRIVER pDrv;
    int rc = sb16AttachInternal(pThis, iLUN, &pDrv);
    if (RT_SUCCESS(rc))
    {
        int rc2 = sb16AddDrv(pDevIns, pThis, pDrv);
        if (RT_FAILURE(rc2))
            LogFunc(("sb16AddDrv failed with %Rrc (ignored)\n", rc2));
    }

    return rc;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnDetach}
 */
static DECLCALLBACK(void) sb16Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    RT_NOREF(fFlags);

    LogFunc(("iLUN=%u, fFlags=0x%x\n", iLUN, fFlags));

    PSB16DRIVER pDrv;
    RTListForEach(&pThis->lstDrv, pDrv, SB16DRIVER, Node)
    {
        if (pDrv->uLUN == iLUN)
        {
            sb16RemoveDrv(pDevIns, pThis, pDrv);
            RTMemFree(pDrv);
            return;
        }
    }
    LogFunc(("LUN#%u was not found\n", iLUN));
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) sb16DevReset(PPDMDEVINS pDevIns)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);

    LogRel2(("SB16: Reset\n"));

    pThis->mixer_regs[0x82] = 0;
    pThis->csp_regs[5]      = 1;
    pThis->csp_regs[9]      = 0xf8;

    pThis->dsp_in_idx = 0;
    pThis->dsp_out_data_len = 0;
    pThis->dsp_in_needed_bytes = 0;
    pThis->nzero = 0;
    pThis->highspeed = 0;
    pThis->v2x6 = 0;
    pThis->cmd = -1;

    sb16MixerReset(pThis);
    sb16SpeakerControl(pThis, false /* fOn */);
    sb16DspCmdResetLegacy(pThis);
}

/**
 * Powers off the device.
 *
 * @param   pDevIns             Device instance to power off.
 */
static DECLCALLBACK(void) sb16PowerOff(PPDMDEVINS pDevIns)
{
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);

    LogRel2(("SB16: Powering off ...\n"));

    /*
     * Destroy all streams.
     */
    for (unsigned i = 0; i < SB16_MAX_STREAMS; i++)
        sb16StreamDestroy(pDevIns, pThis, &pThis->aStreams[i]);

    /*
     * Destroy all sinks.
     */
    if (pThis->pSinkOut)
    {
        AudioMixerSinkDestroy(pThis->pSinkOut, pDevIns);
        pThis->pSinkOut = NULL;
    }
    /** @todo Ditto for sinks. */

    /*
     * Note: Destroy the mixer while powering off and *not* in sb16Destruct,
     *       giving the mixer the chance to release any references held to
     *       PDM audio streams it maintains.
     */
    if (pThis->pMixer)
    {
        AudioMixerDestroy(pThis->pMixer, pDevIns);
        pThis->pMixer = NULL;
    }
}

/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) sb16Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns); /* this shall come first */
    PSB16STATE pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);

    LogFlowFuncEnter();

    PSB16DRIVER pDrv;
    while (!RTListIsEmpty(&pThis->lstDrv))
    {
        pDrv = RTListGetFirst(&pThis->lstDrv, SB16DRIVER, Node);

        RTListNodeRemove(&pDrv->Node);
        RTMemFree(pDrv);
    }

    /* We don't always go via PowerOff, so make sure the mixer is destroyed. */
    if (pThis->pMixer)
    {
        AudioMixerDestroy(pThis->pMixer, pDevIns);
        pThis->pMixer = NULL;
    }

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnConstruct}
 */
static DECLCALLBACK(int) sb16Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns); /* this shall come first */
    PSB16STATE      pThis = PDMDEVINS_2_DATA(pDevIns, PSB16STATE);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    RT_NOREF(iInstance);

    Assert(iInstance == 0);

    /*
     * Initialize the data so sb16Destruct runs without a hitch if we return early.
     */
    pThis->pDevInsR3               = pDevIns;
    pThis->IBase.pfnQueryInterface = sb16QueryInterface;
    pThis->cmd                     = -1;

    pThis->csp_regs[5]             = 1;
    pThis->csp_regs[9]             = 0xf8;

    RTListInit(&pThis->lstDrv);

    /*
     * Validate and read config data.
     */
    /* Note: For now we only support the one-and-only output stream. */
    PSB16STREAM pStream = &pThis->aStreams[SB16_IDX_OUT];

    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "IRQ|DMA|DMA16|Port|Version|TimerHz|DebugEnabled|DebugPathOut", "");
    int rc = pHlp->pfnCFGMQueryU8Def(pCfg, "IRQ", &pStream->HwCfgDefault.uIrq, 5);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Failed to get the \"IRQ\" value"));
    /* Sanity-check supported SB16 IRQs. */
    if (    2 != pStream->HwCfgDefault.uIrq
        &&  5 != pStream->HwCfgDefault.uIrq
        &&  7 != pStream->HwCfgDefault.uIrq
        && 10 != pStream->HwCfgDefault.uIrq)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Invalid \"IRQ\" value."));
    pStream->HwCfgRuntime.uIrq  = pStream->HwCfgDefault.uIrq;

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "DMA", &pStream->HwCfgDefault.uDmaChanLow, 1);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Failed to get the \"DMA\" value"));
    if (    0 != pStream->HwCfgDefault.uDmaChanLow
        &&  1 != pStream->HwCfgDefault.uDmaChanLow
        &&  3 != pStream->HwCfgDefault.uDmaChanLow)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Invalid \"DMA\" value."));
    pStream->HwCfgRuntime.uDmaChanLow  = pStream->HwCfgDefault.uDmaChanLow;

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "DMA16", &pStream->HwCfgDefault.uDmaChanHigh, 5);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Failed to get the \"DMA16\" value"));
    if (    5 != pStream->HwCfgDefault.uDmaChanHigh
        &&  6 != pStream->HwCfgDefault.uDmaChanHigh
        &&  7 != pStream->HwCfgDefault.uDmaChanHigh)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Invalid \"DMA16\" value."));
    pStream->HwCfgRuntime.uDmaChanHigh = pStream->HwCfgDefault.uDmaChanHigh;

    rc = pHlp->pfnCFGMQueryPortDef(pCfg, "Port", &pStream->HwCfgDefault.uPort, 0x220);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Failed to get the \"Port\" value"));
    /* Sanity-check supported SB16 ports. */
    if (   0x220 != pStream->HwCfgDefault.uPort
        && 0x240 != pStream->HwCfgDefault.uPort
        && 0x260 != pStream->HwCfgDefault.uPort
        && 0x280 != pStream->HwCfgDefault.uPort)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Invalid \"Port\" value. Did you specify it as a hex value (e.g. 0x220)?"));
    pStream->HwCfgRuntime.uPort = pStream->HwCfgDefault.uPort;

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "Version", &pStream->HwCfgDefault.uVer, 0x0405);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Failed to get the \"Version\" value"));
    pStream->HwCfgRuntime.uVer = pStream->HwCfgDefault.uVer;

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "TimerHz", &pStream->uTimerHz, SB16_TIMER_HZ_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: failed to read Hertz rate as unsigned integer"));
    if (pStream->uTimerHz == 0)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Hertz rate is invalid"));
    if (pStream->uTimerHz > 2048)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("SB16 configuration error: Maximum Hertz rate is 2048"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "DebugEnabled", &pThis->Dbg.fEnabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("SB16 configuration error: failed to read debugging enabled flag as boolean"));

    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "DebugPathOut", &pThis->Dbg.pszOutPath, NULL);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("SB16 configuration error: failed to read debugging output path flag as string"));

    if (pThis->Dbg.fEnabled)
        LogRel2(("SB16: Debug output will be saved to '%s'\n", pThis->Dbg.pszOutPath));

    /*
     * Create internal software mixer.
     * Must come before we do the device's mixer reset.
     */
    rc = AudioMixerCreate("SB16 Mixer", 0 /* uFlags */, &pThis->pMixer);
    AssertRCReturn(rc, rc);

    AssertRCReturn(rc, rc);
    rc = AudioMixerCreateSink(pThis->pMixer, "PCM Output",
                              PDMAUDIODIR_OUT, pDevIns, &pThis->pSinkOut);
    AssertRCReturn(rc, rc);

    /*
     * Create all hardware streams.
     * For now we have one stream only, namely the output (playback) stream.
     */
    AssertCompile(RT_ELEMENTS(pThis->aStreams) == SB16_MAX_STREAMS);
    for (unsigned i = 0; i < SB16_MAX_STREAMS; i++)
    {
        rc = sb16StreamCreate(pThis, &pThis->aStreams[i], i /* uIdx */);
        AssertRCReturn(rc, rc);
    }

    /*
     * Setup the mixer now that we've got the irq and dma channel numbers.
     */
    pThis->mixer_regs[0x80] = magic_of_irq(pStream->HwCfgRuntime.uIrq);
    pThis->mixer_regs[0x81] = (1 << pStream->HwCfgRuntime.uDmaChanLow) | (1 << pStream->HwCfgRuntime.uDmaChanHigh);
    pThis->mixer_regs[0x82] = 2 << 5;

    /*
     * Perform a device reset before we set up the mixer below,
     * to have a defined state. This includes the mixer reset + legacy reset.
     */
    sb16DevReset(pThis->pDevInsR3);

    /*
     * Make sure that the mixer sink(s) have a valid format set.
     *
     * This is needed in order to make the driver attaching logic working done by Main
     * for machine construction. Must come after sb16DevReset().
     */
    PSB16STREAM const pStreamOut = &pThis->aStreams[SB16_IDX_OUT];
    AudioMixerSinkSetFormat(pThis->pSinkOut, &pStreamOut->Cfg.Props, pStreamOut->Cfg.Device.cMsSchedulingHint);

    /*
     * Create timers.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, sb16TimerIRQ, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_NO_RING0, "SB16 IRQ", &pThis->hTimerIRQ);
    AssertRCReturn(rc, rc);

    static const char * const s_apszNames[] = { "SB16 OUT" };
    AssertCompile(RT_ELEMENTS(s_apszNames) == SB16_MAX_STREAMS);
    for (unsigned i = 0; i < SB16_MAX_STREAMS; i++)
    {
        rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, sb16TimerIO, &pThis->aStreams[i],
                                  TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_NO_RING0, s_apszNames[i], &pThis->aStreams[i].hTimerIO);
        AssertRCReturn(rc, rc);

        pThis->aStreams[i].cTicksTimerIOInterval = PDMDevHlpTimerGetFreq(pDevIns, pThis->aStreams[i].hTimerIO)
                                                 / pThis->aStreams[i].uTimerHz;
        pThis->aStreams[i].tsTimerIO             = PDMDevHlpTimerGet(pDevIns, pThis->aStreams[i].hTimerIO);
    }

    /*
     * Register I/O and DMA.
     */
    static const IOMIOPORTDESC s_aAllDescs[] =
    {
        { "FM Music Status Port",           "FM Music Register Address Port",           NULL, NULL },   // 00h
        { NULL,                             "FM Music Data Port",                       NULL, NULL },   // 01h
        { "Advanced FM Music Status Port",  "Advanced FM Music Register Address Port",  NULL, NULL },   // 02h
        { NULL,                             "Advanced FM Music Data Port",              NULL, NULL },   // 03h
        { NULL,                             "Mixer chip Register Address Port",         NULL, NULL },   // 04h
        { "Mixer chip Data Port",           NULL,                                       NULL, NULL },   // 05h
        { NULL,                             "DSP Reset",                                NULL, NULL },   // 06h
        { "Unused7",                        "Unused7",                                  NULL, NULL },   // 07h
        { "FM Music Status Port",           "FM Music Register Port",                   NULL, NULL },   // 08h
        { NULL,                             "FM Music Data Port",                       NULL, NULL },   // 09h
        { "DSP Read Data Port",             NULL,                                       NULL, NULL },   // 0Ah
        { "UnusedB",                        "UnusedB",                                  NULL, NULL },   // 0Bh
        { "DSP Write-Buffer Status",        "DSP Write Command/Data",                   NULL, NULL },   // 0Ch
        { "UnusedD",                        "UnusedD",                                  NULL, NULL },   // 0Dh
        { "DSP Read-Buffer Status",         NULL,                                       NULL, NULL },   // 0Eh
        { "IRQ16ACK",                       NULL,                                       NULL, NULL },   // 0Fh
        { "CD-ROM Data Register",           "CD-ROM Command Register",                  NULL, NULL },   // 10h
        { "CD-ROM Status Register",         NULL,                                       NULL, NULL },   // 11h
        { NULL,                             "CD-ROM Reset Register",                    NULL, NULL },   // 12h
        { NULL,                             "CD-ROM Enable Register",                   NULL, NULL },   // 13h
        { NULL,                             NULL,                                       NULL, NULL },
    };

    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pStream->HwCfgRuntime.uPort + 0x04 /*uPort*/, 2 /*cPorts*/,
                                     sb16IoPortMixerWrite, sb16IoPortMixerRead,
                                     "SB16 - Mixer", &s_aAllDescs[4], &pThis->hIoPortsMixer);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pStream->HwCfgRuntime.uPort + 0x06 /*uPort*/, 10 /*cPorts*/,
                                     sb16IoPortDspWrite, sb16IoPortDspRead,
                                     "SB16 - DSP", &s_aAllDescs[6], &pThis->hIoPortsDsp);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpDMARegister(pDevIns, pStream->HwCfgRuntime.uDmaChanHigh, sb16DMARead, &pThis->aStreams[SB16_IDX_OUT] /* pvUser */);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpDMARegister(pDevIns, pStream->HwCfgRuntime.uDmaChanLow,  sb16DMARead, &pThis->aStreams[SB16_IDX_OUT] /* pvUser */);
    AssertRCReturn(rc, rc);

    /*
     * Register Saved state.
     */
    rc = PDMDevHlpSSMRegister3(pDevIns, SB16_SAVE_STATE_VERSION, sizeof(SB16STATE), sb16LiveExec, sb16SaveExec, sb16LoadExec);
    AssertRCReturn(rc, rc);

    LogRel2(("SB16: Using port %#x, DMA%RU8, IRQ%RU8\n",
             pStream->HwCfgRuntime.uPort, pStream->HwCfgRuntime.uDmaChanLow, pStream->HwCfgRuntime.uIrq));

    /*
     * Attach drivers.  We ASSUME they are configured consecutively without any
     * gaps, so we stop when we hit the first LUN w/o a driver configured.
     */
    for (unsigned iLun = 0; ; iLun++)
    {
        AssertBreak(iLun < UINT8_MAX);
        LogFunc(("Trying to attach driver for LUN#%u ...\n", iLun));
        rc = sb16AttachInternal(pThis, iLun, NULL /* ppDrv */);
        if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        {
            LogFunc(("cLUNs=%u\n", iLun));
            break;
        }
        AssertLogRelMsgReturn(RT_SUCCESS(rc),  ("LUN#%u: rc=%Rrc\n", iLun, rc), rc);
    }

    /*
     * Register statistics.
     */
# ifdef VBOX_WITH_STATISTICS
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTimerIO,   STAMTYPE_PROFILE, "Timer",     STAMUNIT_TICKS_PER_CALL, "Profiling sb16TimerIO.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatBytesRead, STAMTYPE_COUNTER, "BytesRead", STAMUNIT_BYTES,          "Bytes read from SB16 emulation.");
# endif
    for (unsigned idxStream = 0; idxStream < RT_ELEMENTS(pThis->aStreams); idxStream++)
    {
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.offRead, STAMTYPE_U64, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Virtual internal buffer read position.",    "Stream%u/offRead", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.offWrite, STAMTYPE_U64, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Virtual internal buffer write position.",   "Stream%u/offWrite", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.StatDmaBufSize, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Size of the internal DMA buffer.",  "Stream%u/DMABufSize", idxStream);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aStreams[idxStream].State.StatDmaBufUsed, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Number of bytes used in the internal DMA buffer.",  "Stream%u/DMABufUsed", idxStream);
    }

    /*
     * Debug info items.
     */
    //PDMDevHlpDBGFInfoRegister(pDevIns, "sb16",         "SB16 registers. (sb16 [register case-insensitive])", sb16DbgInfo);
    //PDMDevHlpDBGFInfoRegister(pDevIns, "sb16stream",   "SB16 stream info. (sb16stream [stream number])", sb16DbgInfoStream);
    PDMDevHlpDBGFInfoRegister(pDevIns, "sb16mixer",    "SB16 mixer state.", sb16DbgInfoMixer);

    return VINF_SUCCESS;
}

const PDMDEVREG g_DeviceSB16 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "sb16",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_FIRST_POWEROFF_NOTIFICATION /* stream clearnup with working drivers */,
    /* .fClass = */                 PDM_DEVREG_CLASS_AUDIO,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(SB16STATE),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Sound Blaster 16 Controller",
#if defined(IN_RING3)
    /* .pszRCMod = */               "",
    /* .pszR0Mod = */               "",
    /* .pfnConstruct = */           sb16Construct,
    /* .pfnDestruct = */            sb16Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               sb16DevReset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              sb16Attach,
    /* .pfnDetach = */              sb16Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            sb16PowerOff,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           NULL,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

