/* $Id: vkatCmdGeneric.cpp 168974 2025-05-23 13:54:26Z andreas.loeffler@oracle.com $ */
/** @file
 * Validation Kit Audio Test (VKAT) utility for testing and validating the audio stack.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include <iprt/errcore.h>
#include <iprt/message.h>
#include <iprt/rand.h>
#include <iprt/test.h>

#include "vkatInternal.h"


/*********************************************************************************************************************************
*   Command: backends                                                                                                            *
*********************************************************************************************************************************/

/**
 * Options for 'backends'.
 */
static const RTGETOPTDEF g_aCmdBackendsOptions[] =
{
    { "--dummy", 'd',  RTGETOPT_REQ_NOTHING  }, /* just a placeholder */
};


/** The 'backends' command option help. */
static DECLCALLBACK(const char *) audioTestCmdBackendsHelp(PCRTGETOPTDEF pOpt)
{
    RT_NOREF(pOpt);
    return NULL;
}

/**
 * The 'backends' command handler.
 *
 * @returns Program exit code.
 * @param   pGetState   RTGetOpt state.
 */
static DECLCALLBACK(RTEXITCODE) audioTestCmdBackendsHandler(PRTGETOPTSTATE pGetState)
{
    /*
     * Parse options.
     */
    int           ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(pGetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            AUDIO_TEST_COMMON_OPTION_CASES(ValueUnion, &g_CmdBackends);

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    /*
     * List the backends.
     */
    RTPrintf("Backends (%u):\n", g_cBackends);
    for (size_t i = 0; i < g_cBackends; i++)
        RTPrintf(" %12s - %s\n", g_aBackends[i].pszName, g_aBackends[i].pDrvReg->pszDescription);

    return RTEXITCODE_SUCCESS;
}


/**
 * Command table entry for 'backends'.
 */
const VKATCMD g_CmdBackends =
{
    /* .pszCommand = */         "backends",
    /* .pfnHandler = */         audioTestCmdBackendsHandler,
    /* .pszDesc = */            "Lists the compiled in audio backends.",
    /* .paOptions = */          g_aCmdBackendsOptions,
    /* .cOptions = */           0 /*RT_ELEMENTS(g_aCmdBackendsOptions)*/,
    /* .pfnOptionHelp = */      audioTestCmdBackendsHelp,
    /* .fNeedsTransport = */    false
};


/*********************************************************************************************************************************
*   Command: enum                                                                                                                *
*********************************************************************************************************************************/



/**
 * Long option values for the 'enum' command.
 */
enum
{
    VKAT_ENUM_OPT_PROBE_BACKENDS = 900
};

/**
 * Options for 'enum'.
 */
static const RTGETOPTDEF g_aCmdEnumOptions[] =
{
    { "--backend",          'b',                                RTGETOPT_REQ_STRING  },
    { "--probe-backends",    VKAT_ENUM_OPT_PROBE_BACKENDS,      RTGETOPT_REQ_NOTHING }
};


/** The 'enum' command option help. */
static DECLCALLBACK(const char *) audioTestCmdEnumHelp(PCRTGETOPTDEF pOpt)
{
    switch (pOpt->iShort)
    {
        case 'b':                               return "The audio backend to use";
        case VKAT_ENUM_OPT_PROBE_BACKENDS:      return "Probes all (available) backends until a working one is found";
        default:  return NULL;
    }
}

/**
 * The 'enum' command handler.
 *
 * @returns Program exit code.
 * @param   pGetState   RTGetOpt state.
 */
static DECLCALLBACK(RTEXITCODE) audioTestCmdEnumHandler(PRTGETOPTSTATE pGetState)
{
    /*
     * Parse options.
     */
    /* Option values: */
    PCPDMDRVREG pDrvReg        = AudioTestGetDefaultBackend();
    bool        fProbeBackends = false;

    /* Argument processing loop: */
    int           ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(pGetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'b':
                pDrvReg = AudioTestFindBackendOpt(ValueUnion.psz);
                if (pDrvReg == NULL)
                    return RTEXITCODE_SYNTAX;
                break;

            case VKAT_ENUM_OPT_PROBE_BACKENDS:
                fProbeBackends = true;
                break;

            AUDIO_TEST_COMMON_OPTION_CASES(ValueUnion, &g_CmdEnum);

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    int rc;

    AUDIOTESTDRVSTACK DrvStack;
    if (fProbeBackends)
        rc = audioTestDriverStackProbe(&DrvStack,
                                       true /* fEnabledIn */, true /* fEnabledOut */, false /* fWithDrvAudio */);
    else
        rc = audioTestDriverStackInitEx(&DrvStack, pDrvReg,
                                        true /* fEnabledIn */, true /* fEnabledOut */, false /* fWithDrvAudio */);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unable to init driver stack: %Rrc\n", rc);

    /*
     * Do the enumeration.
     */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    if (DrvStack.pIHostAudio->pfnGetDevices)
    {
        PDMAUDIOHOSTENUM Enum;
        rc = DrvStack.pIHostAudio->pfnGetDevices(DrvStack.pIHostAudio, &Enum);
        if (RT_SUCCESS(rc))
        {
            RTPrintf("Found %u device%s\n", Enum.cDevices, Enum.cDevices != 1 ? "s" : "");

            PPDMAUDIOHOSTDEV pHostDev;
            RTListForEach(&Enum.LstDevices, pHostDev, PDMAUDIOHOSTDEV, ListEntry)
            {
                RTPrintf("\nDevice \"%s\":\n", pHostDev->pszName);

                char szFlags[PDMAUDIOHOSTDEV_MAX_FLAGS_STRING_LEN];
                if (pHostDev->cMaxInputChannels && !pHostDev->cMaxOutputChannels && pHostDev->enmUsage == PDMAUDIODIR_IN)
                    RTPrintf("    Input:  max %u channels (%s)\n",
                             pHostDev->cMaxInputChannels, PDMAudioHostDevFlagsToString(szFlags, pHostDev->fFlags));
                else if (!pHostDev->cMaxInputChannels && pHostDev->cMaxOutputChannels && pHostDev->enmUsage == PDMAUDIODIR_OUT)
                    RTPrintf("    Output: max %u channels (%s)\n",
                             pHostDev->cMaxOutputChannels, PDMAudioHostDevFlagsToString(szFlags, pHostDev->fFlags));
                else
                    RTPrintf("    %s: max %u output channels, max %u input channels (%s)\n",
                             PDMAudioDirGetName(pHostDev->enmUsage), pHostDev->cMaxOutputChannels,
                             pHostDev->cMaxInputChannels, PDMAudioHostDevFlagsToString(szFlags, pHostDev->fFlags));

                if (pHostDev->pszId && *pHostDev->pszId)
                    RTPrintf("    ID:     \"%s\"\n", pHostDev->pszId);
            }

            PDMAudioHostEnumDelete(&Enum);
        }
        else
            rcExit = RTMsgErrorExitFailure("Enumeration failed: %Rrc\n", rc);
    }
    else
        rcExit = RTMsgErrorExitFailure("Enumeration not supported by backend '%s'\n", pDrvReg->szName);
    audioTestDriverStackDelete(&DrvStack);

    return rcExit;
}


/**
 * Command table entry for 'enum'.
 */
const VKATCMD g_CmdEnum =
{
    "enum",
    audioTestCmdEnumHandler,
    "Enumerates audio devices.",
    g_aCmdEnumOptions,
    RT_ELEMENTS(g_aCmdEnumOptions),
    audioTestCmdEnumHelp,
    false /* fNeedsTransport */
};




/*********************************************************************************************************************************
*   Command: play                                                                                                                *
*********************************************************************************************************************************/

/**
 * Worker for audioTestPlayOne implementing the play loop.
 */
static RTEXITCODE audioTestPlayOneInner(PAUDIOTESTDRVMIXSTREAM pMix, PAUDIOTESTWAVEFILE pWaveFile,
                                        PCPDMAUDIOSTREAMCFG pCfgAcq, const char *pszFile)
{
    uint32_t const  cbPreBuffer        = PDMAudioPropsFramesToBytes(pMix->pProps, pCfgAcq->Backend.cFramesPreBuffering);
    uint64_t const  nsStarted          = RTTimeNanoTS();
    uint64_t        nsDonePreBuffering = 0;

    /*
     * Transfer data as quickly as we're allowed.
     */
    uint8_t         abSamples[16384];
    uint32_t const  cbSamplesAligned = PDMAudioPropsFloorBytesToFrame(pMix->pProps, sizeof(abSamples));
    uint64_t        offStream        = 0;
    while (!g_fTerminate)
    {
        /* Read a chunk from the wave file. */
        size_t      cbSamples = 0;
        int rc = AudioTestWaveFileRead(pWaveFile, abSamples, cbSamplesAligned, &cbSamples);
        if (RT_SUCCESS(rc) && cbSamples > 0)
        {
            /* Pace ourselves a little. */
            if (offStream >= cbPreBuffer)
            {
                if (!nsDonePreBuffering)
                    nsDonePreBuffering = RTTimeNanoTS();
                uint64_t const cNsWritten = PDMAudioPropsBytesToNano64(pMix->pProps, offStream - cbPreBuffer);
                uint64_t const cNsElapsed = RTTimeNanoTS() - nsStarted;
                if (cNsWritten > cNsElapsed + RT_NS_10MS)
                    RTThreadSleep(uint32_t(cNsWritten - cNsElapsed - RT_NS_10MS / 2) / RT_NS_1MS);
            }

            /* Transfer the data to the audio stream. */
            for (uint32_t offSamples = 0; offSamples < cbSamples;)
            {
                uint32_t const cbCanWrite = AudioTestMixStreamGetWritable(pMix);
                if (cbCanWrite > 0)
                {
                    uint32_t const cbToPlay = RT_MIN(cbCanWrite, (uint32_t)cbSamples - offSamples);
                    uint32_t       cbPlayed = 0;
                    rc = AudioTestMixStreamPlay(pMix, &abSamples[offSamples], cbToPlay, &cbPlayed);
                    if (RT_SUCCESS(rc))
                    {
                        if (cbPlayed)
                        {
                            offSamples += cbPlayed;
                            offStream  += cbPlayed;
                        }
                        else
                            return RTMsgErrorExitFailure("Played zero bytes - %#x bytes reported playable!\n", cbCanWrite);
                    }
                    else
                        return RTMsgErrorExitFailure("Failed to play %#x bytes: %Rrc\n", cbToPlay, rc);
                }
                else if (AudioTestMixStreamIsOkay(pMix))
                    RTThreadSleep(RT_MIN(RT_MAX(1, pCfgAcq->Device.cMsSchedulingHint), 256));
                else
                    return RTMsgErrorExitFailure("Stream is not okay!\n");
            }
        }
        else if (RT_SUCCESS(rc) && cbSamples == 0)
            break;
        else
            return RTMsgErrorExitFailure("Error reading wav file '%s': %Rrc", pszFile, rc);
    }

    /*
     * Drain the stream.
     */
    if (g_uVerbosity > 0)
        RTMsgInfo("%'RU64 ns: Draining...\n", RTTimeNanoTS() - nsStarted);
    int rc = AudioTestMixStreamDrain(pMix, true /*fSync*/);
    if (RT_SUCCESS(rc))
    {
        if (g_uVerbosity > 0)
            RTMsgInfo("%'RU64 ns: Done\n", RTTimeNanoTS() - nsStarted);
    }
    else
        return RTMsgErrorExitFailure("Draining failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}

/**
 * Worker for audioTestCmdPlayHandler that plays one file.
 */
static RTEXITCODE audioTestPlayOne(const char *pszFile, PCPDMDRVREG pDrvReg, const char *pszDevId,
                                   PAUDIOTESTIOOPTS pIoOpts)
{
    char szTmp[128];

    /*
     * First we must open the file and determin the format.
     */
    RTERRINFOSTATIC ErrInfo;
    AUDIOTESTWAVEFILE WaveFile;
    int rc = AudioTestWaveFileOpen(pszFile, &WaveFile, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Failed to open '%s': %Rrc%#RTeim", pszFile, rc, &ErrInfo.Core);

    if (g_uVerbosity > 0)
    {
        RTMsgInfo("Opened '%s' for playing\n", pszFile);
        RTMsgInfo("Format: %s\n", PDMAudioPropsToString(&WaveFile.Props, szTmp, sizeof(szTmp)));
        RTMsgInfo("Size:   %'RU32 bytes / %#RX32 / %'RU32 frames / %'RU64 ns\n",
                  WaveFile.cbSamples, WaveFile.cbSamples,
                  PDMAudioPropsBytesToFrames(&WaveFile.Props, WaveFile.cbSamples),
                  PDMAudioPropsBytesToNano(&WaveFile.Props, WaveFile.cbSamples));
    }

    /*
     * Construct the driver stack.
     */
    RTEXITCODE          rcExit = RTEXITCODE_FAILURE;
    AUDIOTESTDRVSTACK   DrvStack;
    rc = audioTestDriverStackInit(&DrvStack, pDrvReg, pIoOpts->fWithDrvAudio);
    if (RT_SUCCESS(rc))
    {
        /*
         * Set the output device if one is specified.
         */
        rc = audioTestDriverStackSetDevice(&DrvStack, PDMAUDIODIR_OUT, pszDevId);
        if (RT_SUCCESS(rc))
        {
            /*
             * Open a stream for the output.
             */
            uint8_t const cChannels = PDMAudioPropsChannels(&pIoOpts->Props);

            PDMAUDIOPCMPROPS ReqProps = WaveFile.Props;
            if (cChannels != 0 && PDMAudioPropsChannels(&ReqProps) != cChannels)
                PDMAudioPropsSetChannels(&ReqProps, cChannels);

            uint8_t const cbSample = PDMAudioPropsSampleSize(&pIoOpts->Props);
            if (cbSample != 0)
                PDMAudioPropsSetSampleSize(&ReqProps, cbSample);

            uint32_t const uHz = PDMAudioPropsHz(&pIoOpts->Props);
            if (uHz != 0)
                ReqProps.uHz = uHz;

            PDMAUDIOSTREAMCFG CfgAcq;
            PPDMAUDIOSTREAM   pStream  = NULL;
            rc = audioTestDriverStackStreamCreateOutput(&DrvStack, &ReqProps, pIoOpts->cMsBufferSize,
                                                        pIoOpts->cMsPreBuffer, pIoOpts->cMsSchedulingHint, &pStream, &CfgAcq);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Automatically enable the mixer if the wave file and the
                 * output parameters doesn't match.
                 */
                if (   !pIoOpts->fWithMixer
                    && (   !PDMAudioPropsAreEqual(&WaveFile.Props, &pStream->Cfg.Props)
                        || pIoOpts->uVolumePercent != 100)
                   )
                {
                    RTMsgInfo("Enabling the mixer buffer.\n");
                    pIoOpts->fWithMixer = true;
                }

                /*
                 * Create a mixer wrapper.  This is just a thin wrapper if fWithMixer
                 * is false, otherwise it's doing mixing, resampling and recoding.
                 */
                AUDIOTESTDRVMIXSTREAM Mix;
                rc = AudioTestMixStreamInit(&Mix, &DrvStack, pStream, pIoOpts->fWithMixer ? &WaveFile.Props : NULL, 100 /*ms*/);
                if (RT_SUCCESS(rc))
                {
                    if (g_uVerbosity > 0)
                        RTMsgInfo("Stream: %s cbBackend=%#RX32%s\n",
                                  PDMAudioPropsToString(&pStream->Cfg.Props, szTmp, sizeof(szTmp)),
                                  pStream->cbBackend, pIoOpts->fWithMixer ? " mixed" : "");

                    if (pIoOpts->fWithMixer)
                        AudioTestMixStreamSetVolume(&Mix, pIoOpts->uVolumePercent);

                    /*
                     * Enable the stream and start playing.
                     */
                    rc = AudioTestMixStreamEnable(&Mix);
                    if (RT_SUCCESS(rc))
                        rcExit = audioTestPlayOneInner(&Mix, &WaveFile, &CfgAcq, pszFile);
                    else
                        rcExit = RTMsgErrorExitFailure("Enabling the output stream failed: %Rrc", rc);

                    /*
                     * Clean up.
                     */
                    AudioTestMixStreamTerm(&Mix);
                }
                audioTestDriverStackStreamDestroy(&DrvStack, pStream);
                pStream = NULL;
            }
            else
                rcExit = RTMsgErrorExitFailure("Creating output stream failed: %Rrc", rc);
        }
        else
            rcExit = RTMsgErrorExitFailure("Failed to set output device to '%s': %Rrc", pszDevId, rc);
        audioTestDriverStackDelete(&DrvStack);
    }
    else
        rcExit = RTMsgErrorExitFailure("Driver stack construction failed: %Rrc", rc);
    AudioTestWaveFileClose(&WaveFile);
    return rcExit;
}

/**
 * Worker for audioTestCmdPlayHandler that plays one test tone.
 */
static RTEXITCODE audioTestPlayTestToneOne(PAUDIOTESTTONEPARMS pToneParms,
                                           PCPDMDRVREG pDrvReg, const char *pszDevId,
                                           PAUDIOTESTIOOPTS pIoOpts)
{
    char szTmp[128];

    AUDIOTESTSTREAM TstStream;
    RT_ZERO(TstStream);

    /*
     * Construct the driver stack.
     */
    RTEXITCODE          rcExit = RTEXITCODE_FAILURE;
    AUDIOTESTDRVSTACK   DrvStack;
    int rc = audioTestDriverStackInit(&DrvStack, pDrvReg, pIoOpts->fWithDrvAudio);
    if (RT_SUCCESS(rc))
    {
        /*
         * Set the output device if one is specified.
         */
        rc = audioTestDriverStackSetDevice(&DrvStack, PDMAUDIODIR_OUT, pszDevId);
        if (RT_SUCCESS(rc))
        {
            /*
             * Open a stream for the output.
             */
            uint8_t const cChannels = PDMAudioPropsChannels(&pIoOpts->Props);

            PDMAUDIOPCMPROPS ReqProps = pToneParms->Props;
            if (cChannels != 0 && PDMAudioPropsChannels(&ReqProps) != cChannels)
                PDMAudioPropsSetChannels(&ReqProps, cChannels);

            uint8_t const cbSample = PDMAudioPropsSampleSize(&pIoOpts->Props);
            if (cbSample != 0)
                PDMAudioPropsSetSampleSize(&ReqProps, cbSample);

            uint32_t const uHz = PDMAudioPropsHz(&pIoOpts->Props);
            if (uHz != 0)
                ReqProps.uHz = uHz;

            rc = audioTestDriverStackStreamCreateOutput(&DrvStack, &ReqProps, pIoOpts->cMsBufferSize,
                                                        pIoOpts->cMsPreBuffer, pIoOpts->cMsSchedulingHint, &TstStream.pStream, &TstStream.Cfg);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Automatically enable the mixer if the wave file and the
                 * output parameters doesn't match.
                 */
                if (   !pIoOpts->fWithMixer
                    && (   !PDMAudioPropsAreEqual(&pToneParms->Props, &TstStream.pStream->Cfg.Props)
                        || pToneParms->uVolumePercent != 100)
                    )
                {
                    RTMsgInfo("Enabling the mixer buffer.\n");
                    pIoOpts->fWithMixer = true;
                }

                /*
                 * Create a mixer wrapper.  This is just a thin wrapper if fWithMixer
                 * is false, otherwise it's doing mixing, resampling and recoding.
                 */
                rc = AudioTestMixStreamInit(&TstStream.Mix, &DrvStack, TstStream.pStream,
                                            pIoOpts->fWithMixer ? &pToneParms->Props : NULL, 100 /*ms*/);
                if (RT_SUCCESS(rc))
                {
                    if (g_uVerbosity > 0)
                        RTMsgInfo("Stream: %s cbBackend=%#RX32%s\n",
                                  PDMAudioPropsToString(&TstStream.pStream->Cfg.Props, szTmp, sizeof(szTmp)),
                                  TstStream.pStream->cbBackend, pIoOpts->fWithMixer ? " mixed" : "");

                    /*
                     * Enable the stream and start playing.
                     */
                    rc = AudioTestMixStreamEnable(&TstStream.Mix);
                    if (RT_SUCCESS(rc))
                    {
                        if (pIoOpts->fWithMixer)
                            AudioTestMixStreamSetVolume(&TstStream.Mix, pToneParms->uVolumePercent);

                        rc = audioTestPlayTone(pIoOpts, NULL /* pTstEnv */, &TstStream, pToneParms);
                        if (RT_SUCCESS(rc))
                            rcExit = RTEXITCODE_SUCCESS;
                    }
                    else
                        rcExit = RTMsgErrorExitFailure("Enabling the output stream failed: %Rrc", rc);

                    /*
                     * Clean up.
                     */
                    AudioTestMixStreamTerm(&TstStream.Mix);
                }
                audioTestDriverStackStreamDestroy(&DrvStack, TstStream.pStream);
                TstStream.pStream = NULL;
            }
            else
                rcExit = RTMsgErrorExitFailure("Creating output stream failed: %Rrc", rc);
        }
        else
            rcExit = RTMsgErrorExitFailure("Failed to set output device to '%s': %Rrc", pszDevId, rc);
        audioTestDriverStackDelete(&DrvStack);
    }
    else
        rcExit = RTMsgErrorExitFailure("Driver stack construction failed: %Rrc", rc);
    return rcExit;
}


/**
 * Long option values for the 'play' command.
 */
enum
{
    VKAT_PLAY_OPT_TONE_DUR = 900,
    VKAT_PLAY_OPT_TONE_FREQ,
    VKAT_PLAY_OPT_TONE_VOL,
    VKAT_PLAY_OPT_VOL
};


/**
 * Options for 'play'.
 */
static const RTGETOPTDEF g_aCmdPlayOptions[] =
{
    { "--backend",          'b',                          RTGETOPT_REQ_STRING  },
    { "--channels",         'c',                          RTGETOPT_REQ_UINT8 },
    { "--hz",               'f',                          RTGETOPT_REQ_UINT32 },
    { "--frequency",        'f',                          RTGETOPT_REQ_UINT32 },
    { "--sample-size",      'z',                          RTGETOPT_REQ_UINT8 },
    { "--test-tone",        't',                          RTGETOPT_REQ_NOTHING },
    { "--tone-dur",         VKAT_PLAY_OPT_TONE_DUR,       RTGETOPT_REQ_UINT32 },
    { "--tone-freq",        VKAT_PLAY_OPT_TONE_FREQ,      RTGETOPT_REQ_UINT32 },
    { "--tone-vol",         VKAT_PLAY_OPT_TONE_VOL,       RTGETOPT_REQ_UINT32 },
    { "--output-device",    'o',                          RTGETOPT_REQ_STRING  },
    { "--with-drv-audio",   'd',                          RTGETOPT_REQ_NOTHING },
    { "--with-mixer",       'm',                          RTGETOPT_REQ_NOTHING },
    { "--vol",              VKAT_PLAY_OPT_VOL,            RTGETOPT_REQ_UINT8 }
};


/** The 'play' command option help. */
static DECLCALLBACK(const char *) audioTestCmdPlayHelp(PCRTGETOPTDEF pOpt)
{
    switch (pOpt->iShort)
    {
        case 'b':                       return "The audio backend to use";
        case 'c':                       return "Number of backend output channels";
        case 'd':                       return "Go via DrvAudio instead of directly interfacing with the backend";
        case 'f':                       return "Output frequency (Hz)";
        case 'z':                       return "Output sample size (bits)";
        case 't':                       return "Plays a test tone. Can be specified multiple times";
        case 'm':                       return "Go via the mixer";
        case 'o':                       return "The ID of the output device to use";
        case VKAT_PLAY_OPT_TONE_DUR:    return "Test tone duration (ms)";
        case VKAT_PLAY_OPT_TONE_FREQ:   return "Test tone frequency (Hz)";
        case VKAT_PLAY_OPT_TONE_VOL:    return "Test tone volume (percent)";
        case VKAT_PLAY_OPT_VOL:         return "Playback volume (percent)";
        default:                        return NULL;
    }
}


/**
 * The 'play' command handler.
 *
 * @returns Program exit code.
 * @param   pGetState   RTGetOpt state.
 */
static DECLCALLBACK(RTEXITCODE) audioTestCmdPlayHandler(PRTGETOPTSTATE pGetState)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    /* Option values: */
    PCPDMDRVREG pDrvReg             = AudioTestGetDefaultBackend();
    const char *pszDevId            = NULL;
    uint32_t    cTestTones          = 0;
    uint32_t    cTestFiles          = 0;
    uint8_t     cbSample            = 0;
    uint8_t     cChannels           = 0;
    uint32_t    uHz                 = 0;

    AUDIOTESTIOOPTS IoOpts;
    audioTestIoOptsInitDefaults(&IoOpts);

    AUDIOTESTTONEPARMS ToneParms;
    audioTestToneParmsInit(&ToneParms);

    /* Argument processing loop: */
    int           ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(pGetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'b':
                pDrvReg = AudioTestFindBackendOpt(ValueUnion.psz);
                if (pDrvReg == NULL)
                    return RTEXITCODE_SYNTAX;
                break;

            case 'c':
                cChannels = ValueUnion.u8;
                break;

            case 'd':
                IoOpts.fWithDrvAudio = true;
                break;

            case 'f':
                uHz = ValueUnion.u32;
                break;

            case 'm':
                IoOpts.fWithMixer = true;
                break;

            case 'o':
                pszDevId = ValueUnion.psz;
                break;

            case 't':
                cTestTones++;
                break;

            case 'z':
                cbSample = ValueUnion.u8 / 8;
                break;

            case VKAT_PLAY_OPT_TONE_DUR:
                ToneParms.msDuration = ValueUnion.u32;
                break;

            case VKAT_PLAY_OPT_TONE_FREQ:
                ToneParms.dbFreqHz = ValueUnion.u32;
                break;

            case VKAT_PLAY_OPT_TONE_VOL:
                ToneParms.uVolumePercent = ValueUnion.u8;
                if (ToneParms.uVolumePercent > 100)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid tonevolume (0-100)");
                break;

            case VKAT_PLAY_OPT_VOL:
                IoOpts.uVolumePercent = ValueUnion.u8;
                if (IoOpts.uVolumePercent > 100)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid playback volume (0-100)");
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                /* Set new (override standard) I/O PCM properties if set by the user. */
                PDMAudioPropsInit(&IoOpts.Props,
                                  cbSample  ? cbSample  : 2 /* 16-bit */, true /* fSigned */,
                                  cChannels ? cChannels : 2 /* Stereo */, uHz ? uHz : 44100);

                RTEXITCODE const rcExit2 = audioTestPlayOne(ValueUnion.psz, pDrvReg, pszDevId, &IoOpts);
                if (rcExit == RTEXITCODE_SUCCESS)
                    rcExit = rcExit2;
                /* Keep going. */

                cTestFiles++;
                break;
            }

            AUDIO_TEST_COMMON_OPTION_CASES(ValueUnion, &g_CmdPlay);

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    if (   !cTestFiles
        && !cTestTones)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Must specify at least one test tone and/or file to play!");

    while (cTestTones)
    {
        /* Use some sane defaults if no PCM props are set by the user. */
        PDMAudioPropsInit(&ToneParms.Props,
                          cbSample  ? cbSample  : 2 /* 16-bit */, true /* fSigned */,
                          cChannels ? cChannels : 2 /* Stereo */, uHz ? uHz : 44100);

        RTEXITCODE const rcExit2 = audioTestPlayTestToneOne(&ToneParms, pDrvReg, pszDevId, &IoOpts);
        if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = rcExit2;

        cTestTones--;
    }

    RTMsgInfo("Played %RU32 file(s) / %RU32 test tone(s)\n", cTestFiles, cTestTones);
    return rcExit;
}


/**
 * Command table entry for 'play'.
 */
const VKATCMD g_CmdPlay =
{
    "play",
    audioTestCmdPlayHandler,
    "Plays one or more wave files.",
    g_aCmdPlayOptions,
    RT_ELEMENTS(g_aCmdPlayOptions),
    audioTestCmdPlayHelp,
    false /* fNeedsTransport */
};


/*********************************************************************************************************************************
*   Command: rec                                                                                                                 *
*********************************************************************************************************************************/

/**
 * Worker for audioTestRecOne implementing the recording loop.
 */
static RTEXITCODE audioTestRecOneInner(PAUDIOTESTDRVMIXSTREAM pMix, PAUDIOTESTWAVEFILE pWaveFile,
                                       PCPDMAUDIOSTREAMCFG pCfgAcq, uint64_t cMaxFrames, const char *pszFile)
{
    int             rc;
    uint64_t const  nsStarted   = RTTimeNanoTS();

    /*
     * Transfer data as quickly as we're allowed.
     */
    uint8_t         abSamples[16384];
    uint32_t const  cbSamplesAligned     = PDMAudioPropsFloorBytesToFrame(pMix->pProps, sizeof(abSamples));
    uint64_t        cFramesCapturedTotal = 0;
    while (!g_fTerminate && cFramesCapturedTotal < cMaxFrames)
    {
        /*
         * Anything we can read?
         */
        uint32_t const cbCanRead = AudioTestMixStreamGetReadable(pMix);
        if (cbCanRead)
        {
            uint32_t const cbToRead   = RT_MIN(cbCanRead, cbSamplesAligned);
            uint32_t       cbCaptured = 0;
            rc = AudioTestMixStreamCapture(pMix, abSamples, cbToRead, &cbCaptured);
            if (RT_SUCCESS(rc))
            {
                if (cbCaptured)
                {
                    uint32_t cFramesCaptured = PDMAudioPropsBytesToFrames(pMix->pProps, cbCaptured);
                    if (cFramesCaptured + cFramesCaptured < cMaxFrames)
                    { /* likely */ }
                    else
                    {
                        cFramesCaptured = (uint32_t)cMaxFrames - cFramesCaptured;
                        cbCaptured      = PDMAudioPropsFramesToBytes(pMix->pProps, cFramesCaptured);
                    }

                    rc = AudioTestWaveFileWrite(pWaveFile, abSamples, cbCaptured);
                    if (RT_SUCCESS(rc))
                        cFramesCapturedTotal += cFramesCaptured;
                    else
                        return RTMsgErrorExitFailure("Error writing to '%s': %Rrc", pszFile, rc);
                }
                else
                    return RTMsgErrorExitFailure("Captured zero bytes - %#x bytes reported readable!\n", cbCanRead);
            }
            else
                return RTMsgErrorExitFailure("Failed to capture %#x bytes: %Rrc (%#x available)\n", cbToRead, rc, cbCanRead);
        }
        else if (AudioTestMixStreamIsOkay(pMix))
            RTThreadSleep(RT_MIN(RT_MAX(1, pCfgAcq->Device.cMsSchedulingHint), 256));
        else
            return RTMsgErrorExitFailure("Stream is not okay!\n");
    }

    /*
     * Disable the stream.
     */
    rc = AudioTestMixStreamDisable(pMix);
    if (RT_SUCCESS(rc) && g_uVerbosity > 0)
        RTMsgInfo("%'RU64 ns: Stopped after recording %RU64 frames%s\n", RTTimeNanoTS() - nsStarted, cFramesCapturedTotal,
                  g_fTerminate ? " - Ctrl-C" : ".");
    else if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Disabling stream failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


/**
 * Worker for audioTestCmdRecHandler that recs one file.
 */
static RTEXITCODE audioTestRecOne(const char *pszFile, uint8_t cWaveChannels, uint8_t cbWaveSample, uint32_t uWaveHz,
                                  PCPDMDRVREG pDrvReg, const char *pszDevId, PAUDIOTESTIOOPTS pIoOpts,
                                  uint64_t cMaxFrames, uint64_t cNsMaxDuration)
{
    /*
     * Construct the driver stack.
     */
    RTEXITCODE          rcExit = RTEXITCODE_FAILURE;
    AUDIOTESTDRVSTACK   DrvStack;
    int rc = audioTestDriverStackInit(&DrvStack, pDrvReg, pIoOpts->fWithDrvAudio);
    if (RT_SUCCESS(rc))
    {
        /*
         * Set the input device if one is specified.
         */
        rc = audioTestDriverStackSetDevice(&DrvStack, PDMAUDIODIR_IN, pszDevId);
        if (RT_SUCCESS(rc))
        {
            /*
             * Create an input stream.
             */
            PDMAUDIOPCMPROPS  ReqProps;
            PDMAudioPropsInit(&ReqProps,
                              pIoOpts->Props.cbSampleX ? pIoOpts->Props.cbSampleX : cbWaveSample ? cbWaveSample : 2,
                              pIoOpts->Props.fSigned,
                              pIoOpts->Props.cChannelsX ? pIoOpts->Props.cChannelsX : cWaveChannels ? cWaveChannels : 2,
                              pIoOpts->Props.uHz ? pIoOpts->Props.uHz : uWaveHz ? uWaveHz : 44100);

            PDMAUDIOSTREAMCFG CfgAcq;
            PPDMAUDIOSTREAM   pStream  = NULL;
            rc = audioTestDriverStackStreamCreateInput(&DrvStack, &ReqProps, pIoOpts->cMsBufferSize,
                                                       pIoOpts->cMsPreBuffer, pIoOpts->cMsSchedulingHint, &pStream, &CfgAcq);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Determine the wave file properties.  If it differs from the stream
                 * properties, make sure the mixer is enabled.
                 */
                PDMAUDIOPCMPROPS WaveProps;
                PDMAudioPropsInit(&WaveProps,
                                  cbWaveSample ? cbWaveSample : PDMAudioPropsSampleSize(&CfgAcq.Props),
                                  true /*fSigned*/,
                                  cWaveChannels ? cWaveChannels : PDMAudioPropsChannels(&CfgAcq.Props),
                                  uWaveHz ? uWaveHz : PDMAudioPropsHz(&CfgAcq.Props));
                if (!pIoOpts->fWithMixer && !PDMAudioPropsAreEqual(&WaveProps, &CfgAcq.Props))
                {
                    RTMsgInfo("Enabling the mixer buffer.\n");
                    pIoOpts->fWithMixer = true;
                }

                /* Console the max duration into frames now that we've got the wave file format. */
                if (cMaxFrames != UINT64_MAX && cNsMaxDuration != UINT64_MAX)
                {
                    uint64_t cMaxFrames2 = PDMAudioPropsNanoToBytes64(&WaveProps, cNsMaxDuration);
                    cMaxFrames = RT_MAX(cMaxFrames, cMaxFrames2);
                }
                else if (cNsMaxDuration != UINT64_MAX)
                    cMaxFrames = PDMAudioPropsNanoToBytes64(&WaveProps, cNsMaxDuration);

                /*
                 * Create a mixer wrapper.  This is just a thin wrapper if fWithMixer
                 * is false, otherwise it's doing mixing, resampling and recoding.
                 */
                AUDIOTESTDRVMIXSTREAM Mix;
                rc = AudioTestMixStreamInit(&Mix, &DrvStack, pStream, pIoOpts->fWithMixer ? &WaveProps : NULL, 100 /*ms*/);
                if (RT_SUCCESS(rc))
                {
                    char szTmp[128];
                    if (g_uVerbosity > 0)
                        RTMsgInfo("Stream: %s cbBackend=%#RX32%s\n",
                                  PDMAudioPropsToString(&pStream->Cfg.Props, szTmp, sizeof(szTmp)),
                                  pStream->cbBackend, pIoOpts->fWithMixer ? " mixed" : "");

                    /*
                     * Open the wave output file.
                     */
                    AUDIOTESTWAVEFILE WaveFile;
                    RTERRINFOSTATIC ErrInfo;
                    rc = AudioTestWaveFileCreate(pszFile, &WaveProps, &WaveFile, RTErrInfoInitStatic(&ErrInfo));
                    if (RT_SUCCESS(rc))
                    {
                        if (g_uVerbosity > 0)
                        {
                            RTMsgInfo("Opened '%s' for playing\n", pszFile);
                            RTMsgInfo("Format: %s\n", PDMAudioPropsToString(&WaveFile.Props, szTmp, sizeof(szTmp)));
                        }

                        /*
                         * Enable the stream and start recording.
                         */
                        rc = AudioTestMixStreamEnable(&Mix);
                        if (RT_SUCCESS(rc))
                            rcExit = audioTestRecOneInner(&Mix, &WaveFile, &CfgAcq, cMaxFrames, pszFile);
                        else
                            rcExit = RTMsgErrorExitFailure("Enabling the input stream failed: %Rrc", rc);
                        if (rcExit != RTEXITCODE_SUCCESS)
                            AudioTestMixStreamDisable(&Mix);

                        /*
                         * Clean up.
                         */
                        rc = AudioTestWaveFileClose(&WaveFile);
                        if (RT_FAILURE(rc))
                            rcExit = RTMsgErrorExitFailure("Error closing '%s': %Rrc", pszFile, rc);
                    }
                    else
                        rcExit = RTMsgErrorExitFailure("Failed to open '%s': %Rrc%#RTeim", pszFile, rc, &ErrInfo.Core.pszMsg);

                    AudioTestMixStreamTerm(&Mix);
                }
                audioTestDriverStackStreamDestroy(&DrvStack, pStream);
                pStream = NULL;
            }
            else
                rcExit = RTMsgErrorExitFailure("Creating output stream failed: %Rrc", rc);
        }
        else
            rcExit = RTMsgErrorExitFailure("Failed to set output device to '%s': %Rrc", pszDevId, rc);
        audioTestDriverStackDelete(&DrvStack);
    }
    else
        rcExit = RTMsgErrorExitFailure("Driver stack construction failed: %Rrc", rc);
    return rcExit;
}


/**
 * Options for 'rec'.
 */
static const RTGETOPTDEF g_aCmdRecOptions[] =
{
    { "--backend",          'b',                          RTGETOPT_REQ_STRING  },
    { "--channels",         'c',                          RTGETOPT_REQ_UINT8 },
    { "--hz",               'f',                          RTGETOPT_REQ_UINT32 },
    { "--frequency",        'f',                          RTGETOPT_REQ_UINT32 },
    { "--sample-size",      'z',                          RTGETOPT_REQ_UINT8 },
    { "--input-device",     'i',                          RTGETOPT_REQ_STRING  },
    { "--wav-channels",     'C',                          RTGETOPT_REQ_UINT8 },
    { "--wav-hz",           'F',                          RTGETOPT_REQ_UINT32 },
    { "--wav-frequency",    'F',                          RTGETOPT_REQ_UINT32 },
    { "--wav-sample-size",  'Z',                          RTGETOPT_REQ_UINT8 },
    { "--with-drv-audio",   'd',                          RTGETOPT_REQ_NOTHING },
    { "--with-mixer",       'm',                          RTGETOPT_REQ_NOTHING },
    { "--max-frames",       'r',                          RTGETOPT_REQ_UINT64 },
    { "--max-sec",          's',                          RTGETOPT_REQ_UINT64 },
    { "--max-seconds",      's',                          RTGETOPT_REQ_UINT64 },
    { "--max-ms",           't',                          RTGETOPT_REQ_UINT64 },
    { "--max-milliseconds", 't',                          RTGETOPT_REQ_UINT64 },
    { "--max-ns",           'T',                          RTGETOPT_REQ_UINT64 },
    { "--max-nanoseconds",  'T',                          RTGETOPT_REQ_UINT64 },
};


/** The 'rec' command option help. */
static DECLCALLBACK(const char *) audioTestCmdRecHelp(PCRTGETOPTDEF pOpt)
{
    switch (pOpt->iShort)
    {
        case 'b': return "The audio backend to use.";
        case 'c': return "Number of backend input channels";
        case 'C': return "Number of wave-file channels";
        case 'd': return "Go via DrvAudio instead of directly interfacing with the backend.";
        case 'f': return "Input frequency (Hz)";
        case 'F': return "Wave-file frequency (Hz)";
        case 'z': return "Input sample size (bits)";
        case 'Z': return "Wave-file sample size (bits)";
        case 'm': return "Go via the mixer.";
        case 'i': return "The ID of the input device to use.";
        case 'r': return "Max recording duration in frames.";
        case 's': return "Max recording duration in seconds.";
        case 't': return "Max recording duration in milliseconds.";
        case 'T': return "Max recording duration in nanoseconds.";
        default:  return NULL;
    }
}


/**
 * The 'rec' command handler.
 *
 * @returns Program exit code.
 * @param   pGetState   RTGetOpt state.
 */
static DECLCALLBACK(RTEXITCODE) audioTestCmdRecHandler(PRTGETOPTSTATE pGetState)
{
    /* Option values: */
    PCPDMDRVREG pDrvReg             = AudioTestGetDefaultBackend();
    const char *pszDevId            = NULL;
    uint8_t     cbSample            = 0;
    uint8_t     cChannels           = 0;
    uint32_t    uHz                 = 0;
    uint8_t     cbWaveSample        = 0;
    uint8_t     cWaveChannels       = 0;
    uint32_t    uWaveHz             = 0;
    uint64_t    cMaxFrames          = UINT64_MAX;
    uint64_t    cNsMaxDuration      = UINT64_MAX;

    AUDIOTESTIOOPTS IoOpts;
    audioTestIoOptsInitDefaults(&IoOpts);

    /* Argument processing loop: */
    int           ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(pGetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'b':
                pDrvReg = AudioTestFindBackendOpt(ValueUnion.psz);
                if (pDrvReg == NULL)
                    return RTEXITCODE_SYNTAX;
                break;

            case 'c':
                cChannels = ValueUnion.u8;
                break;

            case 'C':
                cWaveChannels = ValueUnion.u8;
                break;

            case 'd':
                IoOpts.fWithDrvAudio = true;
                break;

            case 'f':
                uHz = ValueUnion.u32;
                break;

            case 'F':
                uWaveHz = ValueUnion.u32;
                break;

            case 'i':
                pszDevId = ValueUnion.psz;
                break;

            case 'm':
                IoOpts.fWithMixer = true;
                break;

            case 'r':
                cMaxFrames = ValueUnion.u64;
                break;

            case 's':
                cNsMaxDuration = ValueUnion.u64 >= UINT64_MAX / RT_NS_1SEC ? UINT64_MAX : ValueUnion.u64 * RT_NS_1SEC;
                break;

            case 't':
                cNsMaxDuration = ValueUnion.u64 >= UINT64_MAX / RT_NS_1MS  ? UINT64_MAX : ValueUnion.u64 * RT_NS_1MS;
                break;

            case 'T':
                cNsMaxDuration = ValueUnion.u64;
                break;

            case 'z':
                cbSample = ValueUnion.u8 / 8;
                break;

            case 'Z':
                cbWaveSample = ValueUnion.u8 / 8;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                if (   cbSample
                    || cChannels
                    || uHz)
                {
                    /* Set new (override standard) I/O PCM properties if set by the user. */
                    PDMAudioPropsInit(&IoOpts.Props,
                                      cbSample  ? cbSample  : 2 /* 16-bit */, true /* fSigned */,
                                      cChannels ? cChannels : 2 /* Stereo */, uHz ? uHz : 44100);
                }

                RTEXITCODE rcExit = audioTestRecOne(ValueUnion.psz, cWaveChannels, cbWaveSample, uWaveHz,
                                                    pDrvReg, pszDevId, &IoOpts,
                                                    cMaxFrames, cNsMaxDuration);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
                break;
            }

            AUDIO_TEST_COMMON_OPTION_CASES(ValueUnion, &g_CmdRec);

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    return RTEXITCODE_SUCCESS;
}


/**
 * Command table entry for 'rec'.
 */
const VKATCMD g_CmdRec =
{
    "rec",
    audioTestCmdRecHandler,
    "Records audio to a wave file.",
    g_aCmdRecOptions,
    RT_ELEMENTS(g_aCmdRecOptions),
    audioTestCmdRecHelp,
    false /* fNeedsTransport */
};

