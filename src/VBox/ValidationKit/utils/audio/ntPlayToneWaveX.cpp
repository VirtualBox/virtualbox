/* $Id: ntPlayToneWaveX.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ????
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>

#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/errcore.h>

#define _USE_MATH_DEFINES
#include <math.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
uint32_t g_cSamplesPerSec    = 44100;
uint32_t g_cSamplesPerPeriod  = 100; // 441.0Hz for 44.1kHz
uint32_t g_cSamplesInBuffer = 4096;
double   g_rdSecDuration     = 5.0;

uint32_t g_cbSample; // assuming 16-bit stereo (for now)

HWAVEOUT g_hWaveOut;
HANDLE g_hWavEvent;


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--samples-per-sec",        's',  RTGETOPT_REQ_UINT32 },
        { "--period-in-samples",      'p',  RTGETOPT_REQ_UINT32 },
        { "--bufsize-in-samples",     'b',  RTGETOPT_REQ_UINT32 },
        { "--total-duration-in-secs", 'd',  RTGETOPT_REQ_UINT32 }
    };

    RTGETOPTSTATE State;
    RTGetOptInit(&State, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    RTGETOPTUNION ValueUnion;
    int chOpt;
    while ((chOpt = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (chOpt)
        {
            case 's': g_cSamplesPerSec    = ValueUnion.u32; break;
            case 'p': g_cSamplesPerPeriod = ValueUnion.u32; break;
            case 'b': g_cSamplesInBuffer  = ValueUnion.u32; break;
            case 'd': g_rdSecDuration     = ValueUnion.u32; break;
            case 'h':
                RTPrintf("usage: ntPlayToneWaveX.exe\n"
                "[-s|--samples-per-sec]\n"
                "[-p|--period-in-samples]\n"
                "[-b|--bufsize-in-samples]\n"
                "[-d|--total-duration-in-secs]\n"
                         "\n"
                         "Plays sine tone using ancient waveX API\n");
                return 0;

            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }


    WAVEFORMATEX waveFormatEx = { 0 };
    MMRESULT mmresult;

    waveFormatEx.wFormatTag = WAVE_FORMAT_PCM;
    waveFormatEx.nChannels = 2;
    waveFormatEx.nSamplesPerSec = g_cSamplesPerSec;
    waveFormatEx.wBitsPerSample = 16;
    waveFormatEx.nBlockAlign = g_cbSample = waveFormatEx.nChannels * waveFormatEx.wBitsPerSample / 8;
    waveFormatEx.nAvgBytesPerSec = waveFormatEx.nBlockAlign * waveFormatEx.nSamplesPerSec;
    waveFormatEx.cbSize = 0;

    g_hWavEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    mmresult = waveOutOpen(&g_hWaveOut, WAVE_MAPPER, &waveFormatEx, (DWORD_PTR)g_hWavEvent, NULL, CALLBACK_EVENT);

    if (mmresult != MMSYSERR_NOERROR)
    {
        RTMsgError("waveOutOpen failed with 0x%X\n", mmresult);
        return -1;
    }


    uint32_t ui32SamplesToPlayTotal = (uint32_t)(g_rdSecDuration * g_cSamplesPerSec);
    uint32_t ui32SamplesToPlay = ui32SamplesToPlayTotal;
    uint32_t ui32SamplesPlayed = 0;
    uint32_t ui32SamplesForWavBuf;

    WAVEHDR waveHdr1 = {0}, waveHdr2 = {0}, *pWaveHdr, *pWaveHdrPlaying, *pWaveHdrWaiting;
    uint32_t i, k;
    DWORD res;

    int16_t *i16Samples1 = (int16_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_cSamplesInBuffer * g_cbSample);
    int16_t *i16Samples2 = (int16_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_cSamplesInBuffer * g_cbSample);

    k = 0; // This is discrete time really!!!

    for (i = 0; i < g_cSamplesInBuffer; i++, k++)
    {
        i16Samples1[2 * i] = (uint16_t)(10000.0 * sin(2.0 * M_PI * k / g_cSamplesPerPeriod));
        i16Samples1[2 * i + 1] = i16Samples1[2 * i];
    }

    ui32SamplesForWavBuf = min(ui32SamplesToPlay, g_cSamplesInBuffer);

    waveHdr1.lpData = (LPSTR)i16Samples1;
    waveHdr1.dwBufferLength = ui32SamplesForWavBuf * g_cbSample;
    waveHdr1.dwFlags = 0;
    waveHdr1.dwLoops = 0;

    ui32SamplesToPlay -= ui32SamplesForWavBuf;
    ui32SamplesPlayed += ui32SamplesForWavBuf;

    pWaveHdrPlaying = &waveHdr1;

    mmresult = waveOutPrepareHeader(g_hWaveOut, pWaveHdrPlaying, sizeof(WAVEHDR));
    mmresult = waveOutWrite(g_hWaveOut, pWaveHdrPlaying, sizeof(WAVEHDR));
    //RTMsgInfo("waveOutWrite completes with %d\n", mmresult);

    res = WaitForSingleObject(g_hWavEvent, INFINITE);
    //RTMsgInfo("WaitForSingleObject completes with %d\n\n", res);

    waveHdr2.lpData = (LPSTR)i16Samples2;
    waveHdr2.dwBufferLength = 0;
    waveHdr2.dwFlags = 0;
    waveHdr2.dwLoops = 0;

    pWaveHdrWaiting = &waveHdr2;

    while (ui32SamplesToPlay > 0)
    {
        int16_t *i16Samples = (int16_t *)pWaveHdrWaiting->lpData;

        for (i = 0; i < g_cSamplesInBuffer; i++, k++)
        {
            i16Samples[2 * i] = (uint16_t)(10000.0 * sin(2.0 * M_PI * k / g_cSamplesPerPeriod));
            i16Samples[2 * i + 1] = i16Samples[2 * i];
        }

        ui32SamplesForWavBuf = min(ui32SamplesToPlay, g_cSamplesInBuffer);

        pWaveHdrWaiting->dwBufferLength = ui32SamplesForWavBuf * g_cbSample;
        pWaveHdrWaiting->dwFlags = 0;
        pWaveHdrWaiting->dwLoops = 0;


        ui32SamplesToPlay -= ui32SamplesForWavBuf;
        ui32SamplesPlayed += ui32SamplesForWavBuf;

        mmresult = waveOutPrepareHeader(g_hWaveOut, pWaveHdrWaiting, sizeof(WAVEHDR));
        mmresult = waveOutWrite(g_hWaveOut, pWaveHdrWaiting, sizeof(WAVEHDR));
        //RTMsgInfo("waveOutWrite completes with %d\n", mmresult);

        res = WaitForSingleObject(g_hWavEvent, INFINITE);
        //RTMsgInfo("WaitForSingleObject completes with %d\n\n", res);

        mmresult = waveOutUnprepareHeader(g_hWaveOut, pWaveHdrPlaying, sizeof(WAVEHDR));
        //RTMsgInfo("waveOutUnprepareHeader completes with %d\n", mmresult);

        pWaveHdr = pWaveHdrWaiting;
        pWaveHdrWaiting = pWaveHdrPlaying;
        pWaveHdrPlaying = pWaveHdr;
    }

    while (mmresult = waveOutUnprepareHeader(g_hWaveOut, pWaveHdrPlaying, sizeof(WAVEHDR)))
    {
        //Expecting WAVERR_STILLPLAYING
        //RTMsgInfo("waveOutUnprepareHeader failed with 0x%X\n", mmresult);
        Sleep(100);
    }

    if (mmresult == MMSYSERR_NOERROR)
    {
        waveOutClose(g_hWaveOut);
    }

    HeapFree(GetProcessHeap(), 0, i16Samples1);
    HeapFree(GetProcessHeap(), 0, i16Samples2);
}

