/* $Id: tstAudioMixBuffer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Audio testcase - Mixing buffer.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>

#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pdmaudioinline.h>

#include "../AudioMixBuffer.h"
#include "../AudioHlp.h"

#define _USE_MATH_DEFINES
#include <math.h> /* sin, M_PI */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef RT_LITTLE_ENDIAN
bool const g_fLittleEndian = true;
#else
bool const g_fLittleEndian = false;
#endif


static void tstBasics(RTTEST hTest)
{
    RTTestSub(hTest, "Basics");

    const PDMAUDIOPCMPROPS Cfg441StereoS16 = PDMAUDIOPCMPROPS_INITIALIZER(
        /* a_cb: */             2,
        /* a_fSigned: */        true,
        /* a_cChannels: */      2,
        /* a_uHz: */            44100,
        /* a_fSwapEndian: */    false
    );
    const PDMAUDIOPCMPROPS Cfg441StereoU16 = PDMAUDIOPCMPROPS_INITIALIZER(
        /* a_cb: */             2,
        /* a_fSigned: */        false,
        /* a_cChannels: */      2,
        /* a_uHz: */            44100,
        /* a_fSwapEndian: */    false
    );
    const PDMAUDIOPCMPROPS Cfg441StereoU32 = PDMAUDIOPCMPROPS_INITIALIZER(
        /* a_cb: */             4,
        /* a_fSigned: */        false,
        /* a_cChannels: */      2,
        /* a_uHz: */            44100,
        /* a_fSwapEndian: */    false
    );

    RTTESTI_CHECK(PDMAudioPropsGetBitrate(&Cfg441StereoS16) == 44100*4*8);
    RTTESTI_CHECK(PDMAudioPropsGetBitrate(&Cfg441StereoU16) == 44100*4*8);
    RTTESTI_CHECK(PDMAudioPropsGetBitrate(&Cfg441StereoU32) == 44100*8*8);

    RTTESTI_CHECK(AudioHlpPcmPropsAreValidAndSupported(&Cfg441StereoS16));
    RTTESTI_CHECK(AudioHlpPcmPropsAreValidAndSupported(&Cfg441StereoU16));
    RTTESTI_CHECK(AudioHlpPcmPropsAreValidAndSupported(&Cfg441StereoU32));


    RTTESTI_CHECK_MSG(PDMAUDIOPCMPROPS_F2B(&Cfg441StereoS16, 1) == 4,
                      ("got %x, expected 4\n", PDMAUDIOPCMPROPS_F2B(&Cfg441StereoS16, 1)));
    RTTESTI_CHECK_MSG(PDMAUDIOPCMPROPS_F2B(&Cfg441StereoU16, 1) == 4,
                      ("got %x, expected 4\n", PDMAUDIOPCMPROPS_F2B(&Cfg441StereoU16, 1)));
    RTTESTI_CHECK_MSG(PDMAUDIOPCMPROPS_F2B(&Cfg441StereoU32, 1) == 8,
                      ("got %x, expected 4\n", PDMAUDIOPCMPROPS_F2B(&Cfg441StereoU32, 1)));

    RTTESTI_CHECK_MSG(PDMAudioPropsBytesPerFrame(&Cfg441StereoS16) == 4,
                      ("got %x, expected 4\n", PDMAudioPropsBytesPerFrame(&Cfg441StereoS16)));
    RTTESTI_CHECK_MSG(PDMAudioPropsBytesPerFrame(&Cfg441StereoU16) == 4,
                      ("got %x, expected 4\n", PDMAudioPropsBytesPerFrame(&Cfg441StereoU16)));
    RTTESTI_CHECK_MSG(PDMAudioPropsBytesPerFrame(&Cfg441StereoU32) == 8,
                      ("got %x, expected 4\n", PDMAudioPropsBytesPerFrame(&Cfg441StereoU32)));

    uint32_t u32;
    for (uint32_t i = 0; i < 256; i += 8)
    {
        RTTESTI_CHECK(PDMAudioPropsIsSizeAligned(&Cfg441StereoU32, i) == true);
        for (uint32_t j = 1; j < 8; j++)
            RTTESTI_CHECK(PDMAudioPropsIsSizeAligned(&Cfg441StereoU32, i + j) == false);
        for (uint32_t j = 0; j < 8; j++)
            RTTESTI_CHECK(PDMAudioPropsFloorBytesToFrame(&Cfg441StereoU32, i + j) == i);
    }
    for (uint32_t i = 0; i < 4096; i += 4)
    {
        RTTESTI_CHECK(PDMAudioPropsIsSizeAligned(&Cfg441StereoS16, i) == true);
        for (uint32_t j = 1; j < 4; j++)
            RTTESTI_CHECK(PDMAudioPropsIsSizeAligned(&Cfg441StereoS16, i + j) == false);
        for (uint32_t j = 0; j < 4; j++)
            RTTESTI_CHECK(PDMAudioPropsFloorBytesToFrame(&Cfg441StereoS16, i + j) == i);
    }

    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsFramesToBytes(&Cfg441StereoS16, 44100)) == 44100 * 2 * 2,
                      ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsFramesToBytes(&Cfg441StereoS16, 2)) == 2 * 2 * 2,
                      ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsFramesToBytes(&Cfg441StereoS16, 1)) == 4,
                      ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsFramesToBytes(&Cfg441StereoU16, 1)) == 4,
                      ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsFramesToBytes(&Cfg441StereoU32, 1)) == 8,
                      ("cb=%RU32\n", u32));

    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsBytesToFrames(&Cfg441StereoS16, 4)) == 1, ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsBytesToFrames(&Cfg441StereoU16, 4)) == 1, ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsBytesToFrames(&Cfg441StereoU32, 8)) == 1, ("cb=%RU32\n", u32));

    uint64_t u64;
    RTTESTI_CHECK_MSG((u64 = PDMAudioPropsBytesToNano(&Cfg441StereoS16, 44100 * 2 * 2)) == RT_NS_1SEC,
                      ("ns=%RU64\n", u64));
    RTTESTI_CHECK_MSG((u64 = PDMAudioPropsBytesToMicro(&Cfg441StereoS16, 44100 * 2 * 2)) == RT_US_1SEC,
                      ("us=%RU64\n", u64));
    RTTESTI_CHECK_MSG((u64 = PDMAudioPropsBytesToMilli(&Cfg441StereoS16, 44100 * 2 * 2)) == RT_MS_1SEC,
                      ("ms=%RU64\n", u64));

    RTTESTI_CHECK_MSG((u64 = PDMAudioPropsFramesToNano(&Cfg441StereoS16, 44100)) == RT_NS_1SEC, ("ns=%RU64\n", u64));
    RTTESTI_CHECK_MSG((u64 = PDMAudioPropsFramesToNano(&Cfg441StereoS16,     1)) == 22675,      ("ns=%RU64\n", u64));
    RTTESTI_CHECK_MSG((u64 = PDMAudioPropsFramesToNano(&Cfg441StereoS16,    31)) == 702947,     ("ns=%RU64\n", u64));
    RTTESTI_CHECK_MSG((u64 = PDMAudioPropsFramesToNano(&Cfg441StereoS16,   255)) == 5782312,    ("ns=%RU64\n", u64));
    //RTTESTI_CHECK_MSG((u64 = DrvAudioHlpFramesToMicro(&Cfg441StereoS16, 44100)) == RT_US_1SEC,
    //                  ("us=%RU64\n", u64));
    RTTESTI_CHECK_MSG((u64 = PDMAudioPropsFramesToMilli(&Cfg441StereoS16, 44100)) == RT_MS_1SEC, ("ms=%RU64\n", u64));
    RTTESTI_CHECK_MSG((u64 = PDMAudioPropsFramesToMilli(&Cfg441StereoS16,   255)) == 5,          ("ms=%RU64\n", u64));

    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsNanoToFrames(&Cfg441StereoS16,  RT_NS_1SEC)) == 44100, ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsNanoToFrames(&Cfg441StereoS16,      215876)) == 10,    ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsMilliToFrames(&Cfg441StereoS16, RT_MS_1SEC)) == 44100, ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsMilliToFrames(&Cfg441StereoU32,          6)) == 265,   ("cb=%RU32\n", u32));

    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsNanoToBytes(&Cfg441StereoS16,  RT_NS_1SEC)) == 44100*2*2, ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsNanoToBytes(&Cfg441StereoS16,      702947)) == 31*2*2,    ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsMilliToBytes(&Cfg441StereoS16, RT_MS_1SEC)) == 44100*2*2, ("cb=%RU32\n", u32));
    RTTESTI_CHECK_MSG((u32 = PDMAudioPropsMilliToBytes(&Cfg441StereoS16,          5)) == 884,       ("cb=%RU32\n", u32));

    /* DrvAudioHlpClearBuf: */
    uint8_t *pbPage;
    int rc = RTTestGuardedAlloc(hTest, HOST_PAGE_SIZE, 0, false /*fHead*/, (void **)&pbPage);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    memset(pbPage, 0x42, HOST_PAGE_SIZE);
    PDMAudioPropsClearBuffer(&Cfg441StereoS16, pbPage, HOST_PAGE_SIZE, HOST_PAGE_SIZE / 4);
    RTTESTI_CHECK(ASMMemIsZero(pbPage, HOST_PAGE_SIZE));

    memset(pbPage, 0x42, HOST_PAGE_SIZE);
    PDMAudioPropsClearBuffer(&Cfg441StereoU16, pbPage, HOST_PAGE_SIZE, HOST_PAGE_SIZE / 4);
    for (uint32_t off = 0; off < HOST_PAGE_SIZE; off += 2)
        RTTESTI_CHECK_MSG(pbPage[off] == 0 && pbPage[off + 1] == 0x80, ("off=%#x: %#x %x\n", off, pbPage[off], pbPage[off + 1]));

    memset(pbPage, 0x42, HOST_PAGE_SIZE);
    PDMAudioPropsClearBuffer(&Cfg441StereoU32, pbPage, HOST_PAGE_SIZE, HOST_PAGE_SIZE / 8);
    for (uint32_t off = 0; off < HOST_PAGE_SIZE; off += 4)
        RTTESTI_CHECK(pbPage[off] == 0 && pbPage[off + 1] == 0 && pbPage[off + 2] == 0 && pbPage[off + 3] == 0x80);


    RTTestDisableAssertions(hTest);
    memset(pbPage, 0x42, HOST_PAGE_SIZE);
    PDMAudioPropsClearBuffer(&Cfg441StereoS16, pbPage, HOST_PAGE_SIZE, HOST_PAGE_SIZE); /* should adjust down the frame count. */
    RTTESTI_CHECK(ASMMemIsZero(pbPage, HOST_PAGE_SIZE));

    memset(pbPage, 0x42, HOST_PAGE_SIZE);
    PDMAudioPropsClearBuffer(&Cfg441StereoU16, pbPage, HOST_PAGE_SIZE, HOST_PAGE_SIZE); /* should adjust down the frame count. */
    for (uint32_t off = 0; off < HOST_PAGE_SIZE; off += 2)
        RTTESTI_CHECK_MSG(pbPage[off] == 0 && pbPage[off + 1] == 0x80, ("off=%#x: %#x %x\n", off, pbPage[off], pbPage[off + 1]));

    memset(pbPage, 0x42, HOST_PAGE_SIZE);
    PDMAudioPropsClearBuffer(&Cfg441StereoU32, pbPage, HOST_PAGE_SIZE, HOST_PAGE_SIZE); /* should adjust down the frame count. */
    for (uint32_t off = 0; off < HOST_PAGE_SIZE; off += 4)
        RTTESTI_CHECK(pbPage[off] == 0 && pbPage[off + 1] == 0 && pbPage[off + 2] == 0 && pbPage[off + 3] == 0x80);
    RTTestRestoreAssertions(hTest);

    RTTestGuardedFree(hTest, pbPage);
}


static void tstSimple(RTTEST hTest)
{
    RTTestSub(hTest, "Simple");

    /* 44100Hz, 2 Channels, S16 */
    PDMAUDIOPCMPROPS config = PDMAUDIOPCMPROPS_INITIALIZER(
        2,                                                                  /* Bytes */
        true,                                                               /* Signed */
        2,                                                                  /* Channels */
        44100,                                                              /* Hz */
        false                                                               /* Swap Endian */
    );

    RTTESTI_CHECK(AudioHlpPcmPropsAreValidAndSupported(&config));

    uint32_t cBufSize = _1K;

    /*
     * General stuff.
     */
    AUDIOMIXBUF mb;
    RTTESTI_CHECK_RC_OK_RETV(AudioMixBufInit(&mb, "Single", &config, cBufSize));
    RTTESTI_CHECK(AudioMixBufSize(&mb) == cBufSize);
    RTTESTI_CHECK(AUDIOMIXBUF_B2F(&mb, AudioMixBufSizeBytes(&mb)) == cBufSize);
    RTTESTI_CHECK(AUDIOMIXBUF_F2B(&mb, AudioMixBufSize(&mb)) == AudioMixBufSizeBytes(&mb));
    RTTESTI_CHECK(AudioMixBufFree(&mb) == cBufSize);
    RTTESTI_CHECK(AUDIOMIXBUF_F2B(&mb, AudioMixBufFree(&mb)) == AudioMixBufFreeBytes(&mb));

    AUDIOMIXBUFWRITESTATE WriteState;
    RTTESTI_CHECK_RC(AudioMixBufInitWriteState(&mb, &WriteState, &config), VINF_SUCCESS);

    AUDIOMIXBUFPEEKSTATE PeekState;
    RTTESTI_CHECK_RC(AudioMixBufInitPeekState(&mb, &PeekState, &config), VINF_SUCCESS);

    /*
     * A few writes (used to be the weird absolute writes).
     */
    uint32_t cFramesRead  = 0, cFramesWritten = 0, cFramesWrittenAbs = 0;
    int16_t aFrames16[2] = { 0xAA, 0xBB };
    int32_t aFrames32[2] = { 0xCC, 0xDD };

    RTTESTI_CHECK(AudioMixBufUsed(&mb) == 0);

    AudioMixBufWrite(&mb, &WriteState, &aFrames16, sizeof(aFrames16), 0 /*offDstFrame*/, cBufSize / 4, &cFramesWritten);
    RTTESTI_CHECK(cFramesWritten == 1 /* Frames */);
    RTTESTI_CHECK(AudioMixBufUsed(&mb) == 0);
    AudioMixBufCommit(&mb, cFramesWritten);
    RTTESTI_CHECK(AudioMixBufUsed(&mb) == 1);
    RTTESTI_CHECK(AudioMixBufReadPos(&mb) == 0);
    RTTESTI_CHECK(AudioMixBufWritePos(&mb) == 1);

    AudioMixBufWrite(&mb, &WriteState, &aFrames32, sizeof(aFrames32), 0 /*offDstFrame*/, cBufSize / 4, &cFramesWritten);
    RTTESTI_CHECK(cFramesWritten == 2 /* Frames */);
    AudioMixBufCommit(&mb, cFramesWritten);
    RTTESTI_CHECK(AudioMixBufUsed(&mb) == 3);
    RTTESTI_CHECK(AudioMixBufReadPos(&mb) == 0);
    RTTESTI_CHECK(AudioMixBufWritePos(&mb) == 3);

    /* Pretend we read the frames.*/
    AudioMixBufAdvance(&mb, 3);
    RTTESTI_CHECK(AudioMixBufUsed(&mb) == 0);
    RTTESTI_CHECK(AudioMixBufReadPos(&mb) == 3);
    RTTESTI_CHECK(AudioMixBufWritePos(&mb) == 3);

    /* Fill up the buffer completely and check wraps. */

    uint32_t  cbSamples = PDMAudioPropsFramesToBytes(&config, cBufSize);
    uint16_t *paSamples = (uint16_t *)RTMemAlloc(cbSamples);
    RTTESTI_CHECK_RETV(paSamples);
    AudioMixBufWrite(&mb, &WriteState, paSamples, cbSamples, 0 /*offDstFrame*/, cBufSize, &cFramesWritten);
    RTTESTI_CHECK(cFramesWritten == cBufSize);
    AudioMixBufCommit(&mb, cFramesWritten);
    RTTESTI_CHECK(AudioMixBufUsed(&mb) == cBufSize);
    RTTESTI_CHECK(AudioMixBufReadPos(&mb) == 3);
    RTTESTI_CHECK(AudioMixBufWritePos(&mb) == 3);
    RTMemFree(paSamples);
    cbSamples = 0;

    /*
     * Writes and reads (used to be circular).
     */
    AudioMixBufDrop(&mb);

    cFramesWrittenAbs = AudioMixBufUsed(&mb);

    uint32_t cToWrite = AudioMixBufSize(&mb) - cFramesWrittenAbs - 1; /* -1 as padding plus -2 frames for above. */
    for (uint32_t i = 0; i < cToWrite; i++)
    {
        AudioMixBufWrite(&mb, &WriteState, &aFrames16[0], sizeof(aFrames16), 0 /*offDstFrame*/, 1, &cFramesWritten);
        RTTESTI_CHECK(cFramesWritten == 1);
        AudioMixBufCommit(&mb, cFramesWritten);
    }
    RTTESTI_CHECK(!AudioMixBufIsEmpty(&mb));
    RTTESTI_CHECK(AudioMixBufFree(&mb) == 1);
    RTTESTI_CHECK(AudioMixBufFreeBytes(&mb) == AUDIOMIXBUF_F2B(&mb, 1U));
    RTTESTI_CHECK(AudioMixBufUsed(&mb) == cToWrite + cFramesWrittenAbs /* + last absolute write */);

    AudioMixBufWrite(&mb, &WriteState, &aFrames16[0], sizeof(aFrames16), 0 /*offDstFrame*/, 1, &cFramesWritten);
    RTTESTI_CHECK(cFramesWritten == 1);
    AudioMixBufCommit(&mb, cFramesWritten);
    RTTESTI_CHECK(AudioMixBufFree(&mb) == 0);
    RTTESTI_CHECK(AudioMixBufFreeBytes(&mb) == AUDIOMIXBUF_F2B(&mb, 0U));
    RTTESTI_CHECK(AudioMixBufUsed(&mb) == cBufSize);

    /* Reads. */
    RTTESTI_CHECK(AudioMixBufReadPos(&mb) == 0);
    uint32_t cbRead;
    uint16_t aFrames16Buf[RT_ELEMENTS(aFrames16)];
    uint32_t cToRead = AudioMixBufSize(&mb) - cFramesWrittenAbs - 1;
    for (uint32_t i = 0; i < cToRead; i++)
    {
        AudioMixBufPeek(&mb, 0 /*offSrcFrame*/, 1, &cFramesRead, &PeekState, aFrames16Buf, sizeof(aFrames16Buf), &cbRead);
        RTTESTI_CHECK(cFramesRead == 1);
        RTTESTI_CHECK(cbRead == sizeof(aFrames16Buf));
        AudioMixBufAdvance(&mb, cFramesRead);
        RTTESTI_CHECK(AudioMixBufReadPos(&mb) == i + 1);
    }
    RTTESTI_CHECK(!AudioMixBufIsEmpty(&mb));
    RTTESTI_CHECK(AudioMixBufFree(&mb) == AudioMixBufSize(&mb) - cFramesWrittenAbs - 1);
    RTTESTI_CHECK(AudioMixBufFreeBytes(&mb) == AUDIOMIXBUF_F2B(&mb, cBufSize - cFramesWrittenAbs - 1));
    RTTESTI_CHECK(AudioMixBufUsed(&mb) == cBufSize - cToRead);

    AudioMixBufPeek(&mb, 0 /*offSrcFrame*/, 1, &cFramesRead, &PeekState, aFrames16Buf, sizeof(aFrames16Buf), &cbRead);
    RTTESTI_CHECK(cFramesRead == 1);
    RTTESTI_CHECK(cbRead == sizeof(aFrames16Buf));
    AudioMixBufAdvance(&mb, cFramesRead);
    RTTESTI_CHECK(AudioMixBufFree(&mb) == cBufSize - cFramesWrittenAbs);
    RTTESTI_CHECK(AudioMixBufFreeBytes(&mb) == AUDIOMIXBUF_F2B(&mb, cBufSize - cFramesWrittenAbs));
    RTTESTI_CHECK(AudioMixBufUsed(&mb) == cFramesWrittenAbs);
    RTTESTI_CHECK(AudioMixBufReadPos(&mb) == 0);

    AudioMixBufTerm(&mb);
}

/** @name Eight test samples represented in all basic formats.
 * @{ */
static uint8_t const  g_au8TestSamples[8]  = {         0x1,        0x11,        0x32,       0x7f,        0x80,       0x81,       0xbe,       0xff };
static int8_t  const  g_ai8TestSamples[8]  = {        -127,        -111,         -78,         -1,           0,          1,         62,        127 };
static uint16_t const g_au16TestSamples[8] = {       0x100,      0x1100,      0x3200,     0x7f00,      0x8000,     0x8100,     0xbe00,     0xff00 };
static int16_t  const g_ai16TestSamples[8] = {      -32512,      -28416,      -19968,       -256,           0,        256,      15872,      32512 };
static uint32_t const g_au32TestSamples[8] = {   0x1000000,  0x11000000,  0x32000000, 0x7f000000,  0x80000000, 0x81000000, 0xbe000000, 0xff000000 };
static int32_t  const g_ai32TestSamples[8] = { -2130706432, -1862270976, -1308622848,  -16777216,           0,   16777216, 1040187392, 2130706432 };
static int64_t  const g_ai64TestSamples[8] = { -2130706432, -1862270976, -1308622848,  -16777216,           0,   16777216, 1040187392, 2130706432 };
static struct { void const *apv[2]; uint32_t cb; } g_aTestSamples[] =
{
    /* 0/0:  */ { { NULL, NULL }, 0 },
    /* 1/8:  */ { {  g_au8TestSamples,  g_ai8TestSamples }, sizeof( g_au8TestSamples) },
    /* 2/16: */ { { g_au16TestSamples, g_ai16TestSamples }, sizeof(g_au16TestSamples) },
    /* 3/24: */ { { NULL, NULL }, 0 },
    /* 4/32: */ { { g_au32TestSamples, g_ai32TestSamples }, sizeof(g_au32TestSamples) },
    /* 5: */    { { NULL, NULL }, 0 },
    /* 6: */    { { NULL, NULL }, 0 },
    /* 7: */    { { NULL, NULL }, 0 },
    /* 8:64 */  { {              NULL, g_ai64TestSamples }, sizeof(g_ai64TestSamples) }, /* raw */
};
/** @} */

/** Fills a buffer with samples from an g_aTestSamples entry. */
static uint32_t tstFillBuf(PCPDMAUDIOPCMPROPS pCfg, void const *pvTestSamples, uint32_t iTestSample,
                           uint8_t *pbBuf, uint32_t cFrames)
{
    uint8_t const cTestSamples = RT_ELEMENTS(g_au8TestSamples);

    cFrames *= PDMAudioPropsChannels(pCfg);
    switch (PDMAudioPropsSampleSize(pCfg))
    {
        case 1:
        {
            uint8_t const * const pau8TestSamples = (uint8_t const *)pvTestSamples;
            uint8_t              *pu8Dst          = (uint8_t *)pbBuf;
            while (cFrames-- > 0)
            {
                *pu8Dst++ = pau8TestSamples[iTestSample];
                iTestSample = (iTestSample + 1) % cTestSamples;
            }
            break;
        }

        case 2:
        {
            uint16_t const * const pau16TestSamples = (uint16_t const *)pvTestSamples;
            uint16_t              *pu16Dst          = (uint16_t *)pbBuf;
            while (cFrames-- > 0)
            {
                *pu16Dst++ = pau16TestSamples[iTestSample];
                iTestSample = (iTestSample + 1) % cTestSamples;
            }
            break;
        }

        case 4:
        {
            uint32_t const * const pau32TestSamples = (uint32_t const *)pvTestSamples;
            uint32_t              *pu32Dst          = (uint32_t *)pbBuf;
            while (cFrames-- > 0)
            {
                *pu32Dst++ = pau32TestSamples[iTestSample];
                iTestSample = (iTestSample + 1) % cTestSamples;
            }
            break;
        }

        case 8:
        {
            uint64_t const * const pau64TestSamples = (uint64_t const *)pvTestSamples;
            uint64_t              *pu64Dst          = (uint64_t *)pbBuf;
            while (cFrames-- > 0)
            {
                *pu64Dst++ = pau64TestSamples[iTestSample];
                iTestSample = (iTestSample + 1) % cTestSamples;
            }
            break;
        }

        default:
            AssertFailedBreak();
    }
    return iTestSample;
}


static void tstConversion(RTTEST hTest, uint8_t cSrcBits, bool fSrcSigned, uint8_t cSrcChs,
                          uint8_t cDstBits, bool fDstSigned, uint8_t cDstChs)
{
    RTTestSubF(hTest, "Conv %uch %c%u to %uch %c%u", cSrcChs, fSrcSigned ? 'S' : 'U', cSrcBits,
               cDstChs, fDstSigned ? 'S' : 'U', cDstBits);

    PDMAUDIOPCMPROPS       CfgSrc, CfgDst;
    PDMAudioPropsInitEx(&CfgSrc, cSrcBits / 8, fSrcSigned, cSrcChs, 44100, g_fLittleEndian, cSrcBits == 64 /*fRaw*/);
    PDMAudioPropsInitEx(&CfgDst, cDstBits / 8, fDstSigned, cDstChs, 44100, g_fLittleEndian, cDstBits == 64 /*fRaw*/);

    void const * const     pvSrcTestSamples = g_aTestSamples[cSrcBits / 8].apv[fSrcSigned];
    void const * const     pvDstTestSamples = g_aTestSamples[cDstBits / 8].apv[fDstSigned];
    uint32_t const         cMixBufFrames = RTRandU32Ex(128, 16384);
    uint32_t const         cIterations   = RTRandU32Ex(256, 1536);
    uint32_t const         cbSrcBuf      = PDMAudioPropsFramesToBytes(&CfgSrc, cMixBufFrames + 64);
    uint8_t * const        pbSrcBuf      = (uint8_t *)RTMemAllocZ(cbSrcBuf);
    uint32_t const         cbDstBuf      = PDMAudioPropsFramesToBytes(&CfgDst, cMixBufFrames + 64);
    uint8_t * const        pbDstBuf      = (uint8_t *)RTMemAllocZ(cbDstBuf);
    uint8_t * const        pbDstExpect   = (uint8_t *)RTMemAllocZ(cbDstBuf);
    RTTESTI_CHECK_RETV(pbSrcBuf);
    RTTESTI_CHECK_RETV(pbDstBuf);
    RTTESTI_CHECK_RETV(pbDstExpect);

    AUDIOMIXBUF             MixBuf;
    RTTESTI_CHECK_RC_RETV(AudioMixBufInit(&MixBuf, "FormatOutputConversion", &CfgSrc, cMixBufFrames), VINF_SUCCESS);
    AUDIOMIXBUFWRITESTATE   WriteState;
    RTTESTI_CHECK_RC_RETV(AudioMixBufInitWriteState(&MixBuf, &WriteState, &CfgSrc), VINF_SUCCESS);
    AUDIOMIXBUFWRITESTATE   WriteStateIgnZero = WriteState; RT_NOREF(WriteStateIgnZero);
    AUDIOMIXBUFPEEKSTATE    PeekState;
    RTTESTI_CHECK_RC_RETV(AudioMixBufInitPeekState(&MixBuf, &PeekState, &CfgDst), VINF_SUCCESS);

    uint32_t iSrcTestSample = 0;
    uint32_t iDstTestSample = 0;
    //RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "cIterations=%u\n", cIterations);
    for (uint32_t iIteration = 0; iIteration < cIterations; iIteration++)
    {
        /* Write some frames to the buffer. */
        uint32_t const cSrcFramesToWrite = iIteration < 16 ? iIteration + 1
                                         : AudioMixBufFree(&MixBuf) ? RTRandU32Ex(1, AudioMixBufFree(&MixBuf)) : 0;
        if (cSrcFramesToWrite > 0)
        {
            uint32_t const cbSrcToWrite = PDMAudioPropsFramesToBytes(&CfgSrc, cSrcFramesToWrite);
            uint32_t cFrames = RTRandU32();
            switch (RTRandU32Ex(0, 3))
            {
                default:
                    iSrcTestSample = tstFillBuf(&CfgSrc, pvSrcTestSamples, iSrcTestSample, pbSrcBuf, cSrcFramesToWrite);
                    AudioMixBufWrite(&MixBuf, &WriteState, pbSrcBuf, cbSrcToWrite, 0 /*offDstFrame*/, cSrcFramesToWrite, &cFrames);
                    RTTESTI_CHECK(cFrames == cSrcFramesToWrite);
                    break;

                case 1: /* zero & blend */
                    AudioMixBufSilence(&MixBuf, &WriteStateIgnZero, 0 /*offFrame*/, cSrcFramesToWrite);
                    iSrcTestSample = tstFillBuf(&CfgSrc, pvSrcTestSamples, iSrcTestSample, pbSrcBuf, cSrcFramesToWrite);
                    AudioMixBufBlend(&MixBuf, &WriteState, pbSrcBuf, cbSrcToWrite, 0 /*offDstFrame*/, cSrcFramesToWrite, &cFrames);
                    RTTESTI_CHECK(cFrames == cSrcFramesToWrite);
                    break;

                case 2: /* blend same equal data twice */
                {
                    AUDIOMIXBUFWRITESTATE WriteStateSame = WriteState;
                    iSrcTestSample = tstFillBuf(&CfgSrc, pvSrcTestSamples, iSrcTestSample, pbSrcBuf, cSrcFramesToWrite);
                    AudioMixBufWrite(&MixBuf, &WriteState, pbSrcBuf, cbSrcToWrite, 0 /*offDstFrame*/, cSrcFramesToWrite, &cFrames);
                    RTTESTI_CHECK(cFrames == cSrcFramesToWrite);
                    AudioMixBufBlend(&MixBuf, &WriteStateSame, pbSrcBuf, cbSrcToWrite, 0 /*offDstFrame*/, cSrcFramesToWrite, &cFrames);
                    RTTESTI_CHECK(cFrames == cSrcFramesToWrite);
                    break;
                }
                case 3: /* write & blend with zero */
                {
                    AUDIOMIXBUFWRITESTATE WriteStateSame = WriteState;
                    iSrcTestSample = tstFillBuf(&CfgSrc, pvSrcTestSamples, iSrcTestSample, pbSrcBuf, cSrcFramesToWrite);
                    AudioMixBufWrite(&MixBuf, &WriteState, pbSrcBuf, cbSrcToWrite, 0 /*offDstFrame*/, cSrcFramesToWrite, &cFrames);
                    RTTESTI_CHECK(cFrames == cSrcFramesToWrite);
                    PDMAudioPropsClearBuffer(&CfgSrc, pbSrcBuf, cbSrcToWrite, cSrcFramesToWrite);
                    AudioMixBufBlend(&MixBuf, &WriteStateSame, pbSrcBuf, cbSrcToWrite, 0 /*offDstFrame*/, cSrcFramesToWrite, &cFrames);
                    RTTESTI_CHECK(cFrames == cSrcFramesToWrite);
                    break;
                }
            }
            AudioMixBufCommit(&MixBuf, cSrcFramesToWrite);
        }

        /* Read some frames back. */
        uint32_t const cUsed            = AudioMixBufUsed(&MixBuf);
        uint32_t const cDstFramesToRead = iIteration < 16 ? iIteration + 1 : iIteration + 5 >= cIterations ? cUsed
                                        : cUsed ? RTRandU32Ex(1, cUsed) : 0;
        if (cDstFramesToRead > 0)
        {
            uint32_t const cbDstToRead = PDMAudioPropsFramesToBytes(&CfgDst, cDstFramesToRead);
            uint32_t       cbRead      = RTRandU32();
            uint32_t       cFrames     = RTRandU32();
            RTRandBytes(pbDstBuf, cbDstToRead);
            AudioMixBufPeek(&MixBuf, 0 /*offSrcFrame*/, (iIteration & 3) != 2 ? cDstFramesToRead : cUsed, &cFrames,
                            &PeekState, pbDstBuf,       (iIteration & 3) != 3 ? cbDstToRead      : cbDstBuf, &cbRead);
            RTTESTI_CHECK(cFrames == cDstFramesToRead);
            RTTESTI_CHECK(cbRead  == cbDstToRead);
            AudioMixBufAdvance(&MixBuf, cFrames);

            /* Verify if we can. */
            if (PDMAudioPropsChannels(&CfgSrc) == PDMAudioPropsChannels(&CfgDst))
            {
                iDstTestSample = tstFillBuf(&CfgDst, pvDstTestSamples, iDstTestSample, pbDstExpect, cFrames);
                if (memcmp(pbDstExpect, pbDstBuf, cbRead) == 0)
                { /* likely */ }
                else
                {
                    RTTestFailed(hTest,
                                 "mismatch: %.*Rhxs\n"
                                 "expected: %.*Rhxs\n"
                                 "iIteration=%u cDstFramesToRead=%u cbRead=%#x\n",
                                 RT_MIN(cbRead, 48), pbDstBuf,
                                 RT_MIN(cbRead, 48), pbDstExpect,
                                 iIteration, cDstFramesToRead, cbRead);
                    break;
                }
            }
        }
    }

    AudioMixBufTerm(&MixBuf);
    RTMemFree(pbSrcBuf);
    RTMemFree(pbDstBuf);
    RTMemFree(pbDstExpect);
}


#if 0 /** @todo rewrite to non-parent/child setup */
static void tstDownsampling(RTTEST hTest, uint32_t uFromHz, uint32_t uToHz)
{
    RTTestSubF(hTest, "Downsampling %u to %u Hz (S16)", uFromHz, uToHz);

    struct { int16_t l, r; }
        aSrcFrames[4096],
        aDstFrames[4096];

    /* Parent (destination) buffer is xxxHz 2ch S16 */
    uint32_t const         cFramesParent = RTRandU32Ex(16, RT_ELEMENTS(aDstFrames));
    PDMAUDIOPCMPROPS const CfgDst = PDMAUDIOPCMPROPS_INITIALIZER(2 /*cbSample*/, true /*fSigned*/, 2 /*ch*/, uToHz, false /*fSwap*/);
    RTTESTI_CHECK(AudioHlpPcmPropsAreValid(&CfgDst));
    AUDIOMIXBUF Parent;
    RTTESTI_CHECK_RC_OK_RETV(AudioMixBufInit(&Parent, "ParentDownsampling", &CfgDst, cFramesParent));

    /* Child (source) buffer is yyykHz 2ch S16 */
    PDMAUDIOPCMPROPS const CfgSrc = PDMAUDIOPCMPROPS_INITIALIZER(2 /*cbSample*/, true /*fSigned*/, 2 /*ch*/, uFromHz, false /*fSwap*/);
    RTTESTI_CHECK(AudioHlpPcmPropsAreValid(&CfgSrc));
    uint32_t const cFramesChild = RTRandU32Ex(32, RT_ELEMENTS(aSrcFrames));
    AUDIOMIXBUF Child;
    RTTESTI_CHECK_RC_OK_RETV(AudioMixBufInit(&Child, "ChildDownsampling", &CfgSrc, cFramesChild));
    RTTESTI_CHECK_RC_OK_RETV(AudioMixBufLinkTo(&Child, &Parent));

    /*
     * Test parameters.
     */
    uint32_t const cMaxSrcFrames = RT_MIN(cFramesParent * uFromHz / uToHz - 1, cFramesChild);
    uint32_t const cIterations   = RTRandU32Ex(4, 128);
    RTTestErrContext(hTest, "cFramesParent=%RU32 cFramesChild=%RU32 cMaxSrcFrames=%RU32 cIterations=%RU32",
                     cFramesParent, cFramesChild, cMaxSrcFrames, cIterations);
    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "cFramesParent=%RU32 cFramesChild=%RU32 cMaxSrcFrames=%RU32 cIterations=%RU32\n",
                 cFramesParent, cFramesChild, cMaxSrcFrames, cIterations);

    /*
     * We generate a simple "A" sine wave as input.
     */
    uint32_t iSrcFrame = 0;
    uint32_t iDstFrame = 0;
    double   rdFixed = 2.0 * M_PI * 440.0 /* A */ / PDMAudioPropsHz(&CfgSrc); /* Fixed sin() input. */
    for (uint32_t i = 0; i < cIterations; i++)
    {
        RTTestPrintf(hTest, RTTESTLVL_DEBUG, "i=%RU32\n", i);

        /*
         * Generate source frames and write them.
         */
        uint32_t const cSrcFrames = i < cIterations / 2
                                  ? RTRandU32Ex(2, cMaxSrcFrames) & ~(uint32_t)1
                                  : RTRandU32Ex(1, cMaxSrcFrames - 1) | 1;
        for (uint32_t j = 0; j < cSrcFrames; j++, iSrcFrame++)
            aSrcFrames[j].r = aSrcFrames[j].l = 32760 /*Amplitude*/ * sin(rdFixed * iSrcFrame);

        uint32_t cSrcFramesWritten = UINT32_MAX / 2;
        RTTESTI_CHECK_RC_OK_BREAK(AudioMixBufWriteAt(&Child, 0, &aSrcFrames, cSrcFrames * sizeof(aSrcFrames[0]),
                                                     &cSrcFramesWritten));
        RTTESTI_CHECK_MSG_BREAK(cSrcFrames == cSrcFramesWritten,
                                ("cSrcFrames=%RU32 vs cSrcFramesWritten=%RU32\n", cSrcFrames, cSrcFramesWritten));

        /*
         * Mix them.
         */
        uint32_t cSrcFramesMixed = UINT32_MAX / 2;
        RTTESTI_CHECK_RC_OK_BREAK(AudioMixBufMixToParent(&Child, cSrcFramesWritten, &cSrcFramesMixed));
        RTTESTI_CHECK_MSG(AudioMixBufUsed(&Child) == 0, ("%RU32\n", AudioMixBufUsed(&Child)));
        RTTESTI_CHECK_MSG_BREAK(cSrcFramesWritten == cSrcFramesMixed,
                                ("cSrcFramesWritten=%RU32 cSrcFramesMixed=%RU32\n", cSrcFramesWritten, cSrcFramesMixed));
        RTTESTI_CHECK_MSG_BREAK(AudioMixBufUsed(&Child) == 0, ("%RU32\n", AudioMixBufUsed(&Child)));

        /*
         * Read out the parent buffer.
         */
        uint32_t cDstFrames = AudioMixBufUsed(&Parent);
        while (cDstFrames > 0)
        {
            uint32_t cFramesRead = UINT32_MAX / 2;
            RTTESTI_CHECK_RC_OK_BREAK(AudioMixBufAcquireReadBlock(&Parent, aDstFrames, sizeof(aDstFrames), &cFramesRead));
            RTTESTI_CHECK_MSG(cFramesRead > 0 && cFramesRead <= cDstFrames,
                              ("cFramesRead=%RU32 cDstFrames=%RU32\n", cFramesRead, cDstFrames));

            AudioMixBufReleaseReadBlock(&Parent, cFramesRead);
            AudioMixBufFinish(&Parent, cFramesRead);

            iDstFrame  += cFramesRead;
            cDstFrames -= cFramesRead;
            RTTESTI_CHECK(AudioMixBufUsed(&Parent) == cDstFrames);
        }
    }

    RTTESTI_CHECK(AudioMixBufUsed(&Parent) == 0);
    RTTESTI_CHECK(AudioMixBufLive(&Child) == 0);
    uint32_t const cDstMinExpect =  (uint64_t)iSrcFrame * uToHz                / uFromHz;
    uint32_t const cDstMaxExpect = ((uint64_t)iSrcFrame * uToHz + uFromHz - 1) / uFromHz;
    RTTESTI_CHECK_MSG(iDstFrame == cDstMinExpect || iDstFrame == cDstMaxExpect,
                      ("iSrcFrame=%#x -> %#x,%#x; iDstFrame=%#x\n", iSrcFrame, cDstMinExpect, cDstMaxExpect, iDstFrame));

    AudioMixBufDestroy(&Parent);
    AudioMixBufDestroy(&Child);
}
#endif


static void tstNewPeek(RTTEST hTest, uint32_t uFromHz, uint32_t uToHz)
{
    RTTestSubF(hTest, "New peek %u to %u Hz (S16)", uFromHz, uToHz);

    struct { int16_t l, r; }
        aSrcFrames[4096],
        aDstFrames[4096];

    /* Mix buffer is uFromHz 2ch S16 */
    uint32_t const         cFrames = RTRandU32Ex(16, RT_ELEMENTS(aSrcFrames));
    PDMAUDIOPCMPROPS const CfgSrc  = PDMAUDIOPCMPROPS_INITIALIZER(2 /*cbSample*/, true /*fSigned*/, 2 /*ch*/, uFromHz, false /*fSwap*/);
    RTTESTI_CHECK(AudioHlpPcmPropsAreValidAndSupported(&CfgSrc));
    AUDIOMIXBUF MixBuf;
    RTTESTI_CHECK_RC_OK_RETV(AudioMixBufInit(&MixBuf, "NewPeekMixBuf", &CfgSrc, cFrames));

    /* Write state (source). */
    AUDIOMIXBUFWRITESTATE WriteState;
    RTTESTI_CHECK_RC_OK_RETV(AudioMixBufInitWriteState(&MixBuf, &WriteState, &CfgSrc));

    /* Peek state (destination) is uToHz 2ch S16 */
    PDMAUDIOPCMPROPS const CfgDst = PDMAUDIOPCMPROPS_INITIALIZER(2 /*cbSample*/, true /*fSigned*/, 2 /*ch*/, uToHz, false /*fSwap*/);
    RTTESTI_CHECK(AudioHlpPcmPropsAreValidAndSupported(&CfgDst));
    AUDIOMIXBUFPEEKSTATE PeekState;
    RTTESTI_CHECK_RC_OK_RETV(AudioMixBufInitPeekState(&MixBuf, &PeekState, &CfgDst));

    /*
     * Test parameters.
     */
    uint32_t const cMaxSrcFrames = RT_MIN(cFrames * uFromHz / uToHz - 1, cFrames);
    uint32_t const cIterations   = RTRandU32Ex(64, 1024);
    RTTestErrContext(hTest, "cFrames=%RU32 cMaxSrcFrames=%RU32 cIterations=%RU32", cFrames, cMaxSrcFrames, cIterations);
    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "cFrames=%RU32 cMaxSrcFrames=%RU32 cIterations=%RU32\n",
                 cFrames, cMaxSrcFrames, cIterations);

    /*
     * We generate a simple "A" sine wave as input.
     */
    uint32_t iSrcFrame = 0;
    uint32_t iDstFrame = 0;
    double   rdFixed = 2.0 * M_PI * 440.0 /* A */ / PDMAudioPropsHz(&CfgSrc); /* Fixed sin() input. */
    for (uint32_t i = 0; i < cIterations; i++)
    {
        RTTestPrintf(hTest, RTTESTLVL_DEBUG, "i=%RU32\n", i);

        /*
         * Generate source frames and write them.
         */
        uint32_t const cSrcFrames = i < cIterations / 2
                                  ? RTRandU32Ex(2, cMaxSrcFrames) & ~(uint32_t)1
                                  : RTRandU32Ex(1, cMaxSrcFrames - 1) | 1;
        for (uint32_t j = 0; j < cSrcFrames; j++, iSrcFrame++)
            aSrcFrames[j].r = aSrcFrames[j].l = 32760 /*Amplitude*/ * sin(rdFixed * iSrcFrame);

        uint32_t cSrcFramesWritten = UINT32_MAX / 2;
        AudioMixBufWrite(&MixBuf, &WriteState, &aSrcFrames[0], cSrcFrames * sizeof(aSrcFrames[0]),
                         0 /*offDstFrame*/, cSrcFrames, &cSrcFramesWritten);
        RTTESTI_CHECK_MSG_BREAK(cSrcFrames == cSrcFramesWritten,
                                ("cSrcFrames=%RU32 vs cSrcFramesWritten=%RU32 cLiveFrames=%RU32\n",
                                 cSrcFrames, cSrcFramesWritten, AudioMixBufUsed(&MixBuf)));
        AudioMixBufCommit(&MixBuf, cSrcFrames);

        /*
         * Read out all the frames using the peek function.
         */
        uint32_t offSrcFrame = 0;
        while (offSrcFrame < cSrcFramesWritten)
        {
            uint32_t cSrcFramesToRead = cSrcFramesWritten - offSrcFrame;
            uint32_t cTmp = (uint64_t)cSrcFramesToRead * uToHz / uFromHz;
            if (cTmp + 32 >= RT_ELEMENTS(aDstFrames))
                cSrcFramesToRead = ((uint64_t)RT_ELEMENTS(aDstFrames) - 32) * uFromHz / uToHz; /* kludge */

            uint32_t cSrcFramesPeeked = UINT32_MAX / 4;
            uint32_t cbDstPeeked      = UINT32_MAX / 2;
            RTRandBytes(aDstFrames, sizeof(aDstFrames));
            AudioMixBufPeek(&MixBuf, offSrcFrame, cSrcFramesToRead, &cSrcFramesPeeked,
                            &PeekState, aDstFrames, sizeof(aDstFrames), &cbDstPeeked);
            uint32_t cDstFramesPeeked = PDMAudioPropsBytesToFrames(&CfgDst, cbDstPeeked);
            RTTESTI_CHECK(cbDstPeeked > 0 || cSrcFramesPeeked > 0);

            if (uFromHz == uToHz)
            {
                for (uint32_t iDst = 0; iDst < cDstFramesPeeked; iDst++)
                    if (memcmp(&aDstFrames[iDst], &aSrcFrames[offSrcFrame + iDst], sizeof(aSrcFrames[0])) != 0)
                        RTTestFailed(hTest, "Frame #%u differs: %#x / %#x, expected %#x / %#x\n", iDstFrame + iDst,
                                     aDstFrames[iDst].l, aDstFrames[iDst].r,
                                     aSrcFrames[iDst + offSrcFrame].l, aSrcFrames[iDst + offSrcFrame].r);
            }

            offSrcFrame += cSrcFramesPeeked;
            iDstFrame   += cDstFramesPeeked;
        }

        /*
         * Then advance.
         */
        AudioMixBufAdvance(&MixBuf, cSrcFrames);
        RTTESTI_CHECK(AudioMixBufUsed(&MixBuf) == 0);
    }

    /** @todo this is a bit lax...   */
    uint32_t const cDstMinExpect = ((uint64_t)iSrcFrame * uToHz - uFromHz - 1) / uFromHz;
    uint32_t const cDstMaxExpect = ((uint64_t)iSrcFrame * uToHz + uFromHz - 1) / uFromHz;
    RTTESTI_CHECK_MSG(iDstFrame >= cDstMinExpect && iDstFrame <= cDstMaxExpect,
                      ("iSrcFrame=%#x -> %#x..%#x; iDstFrame=%#x (delta %d)\n",
                       iSrcFrame, cDstMinExpect, cDstMaxExpect, iDstFrame, (cDstMinExpect + cDstMaxExpect) / 2 - iDstFrame));

    AudioMixBufTerm(&MixBuf);
}

/* Test volume control. */
static void tstVolume(RTTEST hTest)
{
    RTTestSub(hTest, "Volume control (44.1kHz S16 2ch)");
    uint32_t const cBufSize = 256;

    /*
     * Configure a mixbuf where we read and write 44.1kHz S16 2ch.
     */
    PDMAUDIOPCMPROPS const Cfg = PDMAUDIOPCMPROPS_INITIALIZER(
        2,                                                                  /* Bytes */
        true,                                                               /* Signed */
        2,                                                                  /* Channels */
        44100,                                                              /* Hz */
        false                                                               /* Swap Endian */
    );
    AUDIOMIXBUF MixBuf;
    RTTESTI_CHECK_RC_RETV(AudioMixBufInit(&MixBuf, "Volume", &Cfg, cBufSize), VINF_SUCCESS);

    AUDIOMIXBUFWRITESTATE WriteState;
    RTTESTI_CHECK_RC_RETV(AudioMixBufInitWriteState(&MixBuf, &WriteState, &Cfg), VINF_SUCCESS);

    AUDIOMIXBUFPEEKSTATE PeekState;
    RTTESTI_CHECK_RC_RETV(AudioMixBufInitPeekState(&MixBuf, &PeekState, &Cfg), VINF_SUCCESS);

    /*
     * A few 16-bit signed test samples.
     */
    static int16_t const s_aFrames16S[16] =
    {
        INT16_MIN,  INT16_MIN + 1, -128,           -64,            -4,            -1,         0, 1,
                2,            255,  256, INT16_MAX / 2, INT16_MAX - 2, INT16_MAX - 1, INT16_MAX, 0,
    };

    /*
     * 1) Full volume/0dB attenuation (255).
     */
    PDMAUDIOVOLUME Vol = PDMAUDIOVOLUME_INITIALIZER_MAX;
    AudioMixBufSetVolume(&MixBuf, &Vol);

    /* Write all the test frames to the mixer buffer: */
    uint32_t cFramesWritten;
    AudioMixBufWrite(&MixBuf, &WriteState, &s_aFrames16S[0], sizeof(s_aFrames16S), 0 /*offDstFrame*/, cBufSize, &cFramesWritten);
    RTTESTI_CHECK(cFramesWritten == RT_ELEMENTS(s_aFrames16S) / 2);
    AudioMixBufCommit(&MixBuf, cFramesWritten);

    /* Read them back.  We should get them back just like we wrote them. */
    uint16_t au16Buf[cBufSize * 2];
    uint32_t cFramesPeeked;
    uint32_t cbPeeked;
    AudioMixBufPeek(&MixBuf, 0 /*offSrcFrame*/, cFramesWritten, &cFramesPeeked, &PeekState, au16Buf, sizeof(au16Buf), &cbPeeked);
    RTTESTI_CHECK(cFramesPeeked == cFramesWritten);
    RTTESTI_CHECK(cbPeeked == PDMAudioPropsFramesToBytes(&Cfg, cFramesPeeked));
    AudioMixBufAdvance(&MixBuf, cFramesPeeked);

    /* Check that at 0dB the frames came out unharmed. */
    if (memcmp(au16Buf, s_aFrames16S, sizeof(s_aFrames16S)) != 0)
        RTTestFailed(hTest,
                     "0dB test failed\n"
                     "mismatch: %.*Rhxs\n"
                     "expected: %.*Rhxs\n",
                     sizeof(s_aFrames16S), au16Buf, sizeof(s_aFrames16S), s_aFrames16S);

    /*
     * 2) Half volume/-6dB attenuation (16 steps down).
     */
    PDMAudioVolumeInitFromStereo(&Vol, false, 255 - 16, 255 - 16);
    AudioMixBufSetVolume(&MixBuf, &Vol);

    /* Write all the test frames to the mixer buffer: */
    AudioMixBufWrite(&MixBuf, &WriteState, &s_aFrames16S[0], sizeof(s_aFrames16S), 0 /*offDstFrame*/, cBufSize, &cFramesWritten);
    RTTESTI_CHECK(cFramesWritten == RT_ELEMENTS(s_aFrames16S) / 2);
    AudioMixBufCommit(&MixBuf, cFramesWritten);

    /* Read them back.  We should get them back just like we wrote them. */
    AudioMixBufPeek(&MixBuf, 0 /*offSrcFrame*/, cFramesWritten, &cFramesPeeked, &PeekState, au16Buf, sizeof(au16Buf), &cbPeeked);
    RTTESTI_CHECK(cFramesPeeked == cFramesWritten);
    RTTESTI_CHECK(cbPeeked == PDMAudioPropsFramesToBytes(&Cfg, cFramesPeeked));
    AudioMixBufAdvance(&MixBuf, cFramesPeeked);

    /* Check that at -6dB the sample values are halved. */
    int16_t ai16Expect[sizeof(s_aFrames16S) / 2];
    memcpy(ai16Expect, s_aFrames16S, sizeof(ai16Expect));
    for (uintptr_t i = 0; i < RT_ELEMENTS(ai16Expect); i++)
        ai16Expect[i] >>= 1; /* /= 2 - not the same for signed numbers; */
    if (memcmp(au16Buf, ai16Expect, sizeof(ai16Expect)) != 0)
        RTTestFailed(hTest,
                     "-6dB test failed\n"
                     "mismatch: %.*Rhxs\n"
                     "expected: %.*Rhxs\n"
                     "wrote:    %.*Rhxs\n",
                     sizeof(ai16Expect), au16Buf, sizeof(ai16Expect), ai16Expect, sizeof(s_aFrames16S), s_aFrames16S);

    AudioMixBufTerm(&MixBuf);
}


int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);

    /*
     * Initialize IPRT and create the test.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstAudioMixBuffer", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstBasics(hTest);
    tstSimple(hTest);

    /* Run tstConversion for all combinations we have test data. */
    for (unsigned iSrc = 0; iSrc < RT_ELEMENTS(g_aTestSamples); iSrc++)
    {
        for (unsigned iSrcSigned = 0; iSrcSigned < RT_ELEMENTS(g_aTestSamples[0].apv); iSrcSigned++)
            if (g_aTestSamples[iSrc].apv[iSrcSigned])
                for (unsigned cSrcChs = 1; cSrcChs <= 2; cSrcChs++)
                    for (unsigned iDst = 0; iDst < RT_ELEMENTS(g_aTestSamples); iDst++)
                        for (unsigned iDstSigned = 0; iDstSigned < RT_ELEMENTS(g_aTestSamples[0].apv); iDstSigned++)
                            if (g_aTestSamples[iDst].apv[iDstSigned])
                                for (unsigned cDstChs = 1; cDstChs <= 2; cDstChs++)
                                    tstConversion(hTest, iSrc * 8, iSrcSigned == 1, cSrcChs,
                                                  /*->*/ iDst * 8, iDstSigned == 1, cDstChs);
    }

#if 0 /** @todo rewrite to non-parent/child setup */
    tstDownsampling(hTest, 44100, 22050);
    tstDownsampling(hTest, 48000, 44100);
    tstDownsampling(hTest, 48000, 22050);
    tstDownsampling(hTest, 48000, 11000);
#endif
    tstNewPeek(hTest, 48000, 48000);
    tstNewPeek(hTest, 48000, 11000);
    tstNewPeek(hTest, 48000, 44100);
    tstNewPeek(hTest, 44100, 22050);
    tstNewPeek(hTest, 44100, 11000);
    //tstNewPeek(hTest, 11000, 48000);
    //tstNewPeek(hTest, 22050, 44100);

    tstVolume(hTest);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}
