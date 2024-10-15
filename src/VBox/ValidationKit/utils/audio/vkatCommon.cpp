/* $Id: vkatCommon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Validation Kit Audio Test (VKAT) - Common code.
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
#define LOG_GROUP LOG_GROUP_AUDIO_TEST
#include <iprt/log.h>

#ifdef VBOX_WITH_AUDIO_ALSA
# include "DrvHostAudioAlsaStubsMangling.h"
# include <alsa/asoundlib.h>
# include <alsa/control.h> /* For device enumeration. */
# include <alsa/version.h>
# include "DrvHostAudioAlsaStubs.h"
#endif
#ifdef VBOX_WITH_AUDIO_OSS
# include <errno.h>
# include <fcntl.h>
# include <sys/ioctl.h>
# include <sys/mman.h>
# include <sys/soundcard.h>
# include <unistd.h>
#endif
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
# include <iprt/win/audioclient.h>
# include <endpointvolume.h> /* For IAudioEndpointVolume. */
# include <audiopolicy.h> /* For IAudioSessionManager. */
# include <AudioSessionTypes.h>
# include <Mmdeviceapi.h>
#endif

#include <iprt/circbuf.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/rand.h>
#include <iprt/test.h>

#include "Audio/AudioHlp.h"
#include "Audio/AudioTest.h"
#include "Audio/AudioTestService.h"
#include "Audio/AudioTestServiceClient.h"

#include "vkatInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int audioTestStreamInit(PAUDIOTESTDRVSTACK pDrvStack, PAUDIOTESTSTREAM pStream, PDMAUDIODIR enmDir, PAUDIOTESTIOOPTS pPlayOpt);
static int audioTestStreamDestroy(PAUDIOTESTDRVSTACK pDrvStack, PAUDIOTESTSTREAM pStream);


/*********************************************************************************************************************************
*   Volume handling.                                                                                                             *
*********************************************************************************************************************************/

#ifdef VBOX_WITH_AUDIO_ALSA
/**
 * Sets the system's master volume via ALSA, if available.
 *
 * @returns VBox status code.
 * @param   uVolPercent         Volume (in percent) to set.
 */
static int audioTestSetMasterVolumeALSA(unsigned uVolPercent)
{
    int rc = audioLoadAlsaLib();
    if (RT_FAILURE(rc))
        return rc;

    int          err;
    snd_mixer_t *handle;

# define ALSA_CHECK_RET(a_Exp, a_Text) \
    if (!(a_Exp)) \
    { \
        AssertLogRelMsg(a_Exp, a_Text); \
        if (handle) \
            snd_mixer_close(handle); \
        return VERR_GENERAL_FAILURE; \
    }

# define ALSA_CHECK_ERR_RET(a_Text) \
    ALSA_CHECK_RET(err >= 0, a_Text)

    err = snd_mixer_open(&handle, 0 /* Index */);
    ALSA_CHECK_ERR_RET(("ALSA: Failed to open mixer: %s\n", snd_strerror(err)));
    err = snd_mixer_attach(handle, "default");
    ALSA_CHECK_ERR_RET(("ALSA: Failed to attach to default sink: %s\n", snd_strerror(err)));
    err = snd_mixer_selem_register(handle, NULL, NULL);
    ALSA_CHECK_ERR_RET(("ALSA: Failed to attach to default sink: %s\n", snd_strerror(err)));
    err = snd_mixer_load(handle);
    ALSA_CHECK_ERR_RET(("ALSA: Failed to load mixer: %s\n", snd_strerror(err)));

    snd_mixer_selem_id_t *sid = NULL;
    snd_mixer_selem_id_alloca(&sid);

    snd_mixer_selem_id_set_index(sid, 0 /* Index */);
    snd_mixer_selem_id_set_name(sid, "Master");

    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);
    ALSA_CHECK_RET(elem != NULL, ("ALSA: Failed to find mixer element: %s\n", snd_strerror(err)));

    long uVolMin, uVolMax;
    snd_mixer_selem_get_playback_volume_range(elem, &uVolMin, &uVolMax);
    ALSA_CHECK_ERR_RET(("ALSA: Failed to get playback volume range: %s\n", snd_strerror(err)));

    long const uVol = RT_MIN(uVolPercent, 100) * uVolMax / 100;

    err = snd_mixer_selem_set_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, uVol);
    ALSA_CHECK_ERR_RET(("ALSA: Failed to set playback volume left: %s\n", snd_strerror(err)));
    err = snd_mixer_selem_set_playback_volume(elem, SND_MIXER_SCHN_FRONT_RIGHT, uVol);
    ALSA_CHECK_ERR_RET(("ALSA: Failed to set playback volume right: %s\n", snd_strerror(err)));

    snd_mixer_close(handle);

    return VINF_SUCCESS;

# undef ALSA_CHECK_RET
# undef ALSA_CHECK_ERR_RET
}
#endif /* VBOX_WITH_AUDIO_ALSA */

#ifdef VBOX_WITH_AUDIO_OSS
/**
 * Sets the system's master volume via OSS, if available.
 *
 * @returns VBox status code.
 * @param   uVolPercent         Volume (in percent) to set.
 */
static int audioTestSetMasterVolumeOSS(unsigned uVolPercent)
{
    int hFile = open("/dev/dsp", O_WRONLY | O_NONBLOCK, 0);
    if (hFile == -1)
    {
        /* Try opening the mixing device instead. */
        hFile = open("/dev/mixer", O_RDONLY | O_NONBLOCK, 0);
    }

    if (hFile != -1)
    {
        /* OSS maps 0 (muted) - 100 (max), so just use uVolPercent unmodified here. */
        uint16_t uVol = RT_MAKE_U16(uVolPercent, uVolPercent);
        AssertLogRelMsgReturnStmt(ioctl(hFile, SOUND_MIXER_PCM /* SNDCTL_DSP_SETPLAYVOL */, &uVol) >= 0,
                                  ("OSS: Failed to set DSP playback volume: %s (%d)\n",
                                   strerror(errno), errno), close(hFile), RTErrConvertFromErrno(errno));
        return VINF_SUCCESS;
    }

    return VERR_NOT_SUPPORTED;
}
#endif /* VBOX_WITH_AUDIO_OSS */

#ifdef RT_OS_WINDOWS
static int audioTestSetMasterVolumeWASAPI(unsigned uVolPercent)
{
    HRESULT hr;

# define WASAPI_CHECK_HR_RET(a_Text) \
    if (FAILED(hr)) \
    { \
        AssertLogRelMsgFailed(a_Text); \
        return VERR_GENERAL_FAILURE; \
    }

    hr = CoInitialize(NULL);
    WASAPI_CHECK_HR_RET(("CoInitialize() failed, hr=%Rhrc", hr));
    IMMDeviceEnumerator* pIEnumerator = NULL;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&pIEnumerator);
    WASAPI_CHECK_HR_RET(("WASAPI: Unable to create IMMDeviceEnumerator, hr=%Rhrc", hr));

    IMMDevice *pIMMDevice = NULL;
    hr = pIEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eConsole, &pIMMDevice);
    WASAPI_CHECK_HR_RET(("WASAPI: Unable to get audio endpoint, hr=%Rhrc", hr));
    pIEnumerator->Release();

    IAudioEndpointVolume *pIAudioEndpointVolume = NULL;
    hr = pIMMDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void **)&pIAudioEndpointVolume);
    WASAPI_CHECK_HR_RET(("WASAPI: Unable to activate audio endpoint volume, hr=%Rhrc", hr));
    pIMMDevice->Release();

    float dbMin, dbMax, dbInc;
    hr = pIAudioEndpointVolume->GetVolumeRange(&dbMin, &dbMax, &dbInc);
    WASAPI_CHECK_HR_RET(("WASAPI: Unable to get volume range, hr=%Rhrc", hr));

    float const dbSteps           = (dbMax - dbMin) / dbInc;
    float const dbStepsPerPercent = (dbSteps * dbInc) / 100;
    float const dbVol             = dbMin + (dbStepsPerPercent * (float(RT_MIN(uVolPercent, 100.0))));

    hr = pIAudioEndpointVolume->SetMasterVolumeLevel(dbVol, NULL);
    WASAPI_CHECK_HR_RET(("WASAPI: Unable to set master volume level, hr=%Rhrc", hr));
    pIAudioEndpointVolume->Release();

    return VINF_SUCCESS;

# undef WASAPI_CHECK_HR_RET
}
#endif /* RT_OS_WINDOWS */

/**
 * Sets the system's master volume, if available.
 *
 * @returns VBox status code. VERR_NOT_SUPPORTED if not supported.
 * @param   uVolPercent         Volume (in percent) to set.
 */
static int audioTestSetMasterVolume(unsigned uVolPercent)
{
    int rc = VINF_SUCCESS;

#ifdef VBOX_WITH_AUDIO_ALSA
    rc = audioTestSetMasterVolumeALSA(uVolPercent);
    if (RT_SUCCESS(rc))
        return rc;
    /* else try OSS (if available) below. */
#endif /* VBOX_WITH_AUDIO_ALSA */

#ifdef VBOX_WITH_AUDIO_OSS
    rc = audioTestSetMasterVolumeOSS(uVolPercent);
    if (RT_SUCCESS(rc))
        return rc;
#endif /* VBOX_WITH_AUDIO_OSS */

#ifdef RT_OS_WINDOWS
    rc = audioTestSetMasterVolumeWASAPI(uVolPercent);
    if (RT_SUCCESS(rc))
        return rc;
#endif

   RT_NOREF(rc, uVolPercent);
    /** @todo Port other platforms. */
   return VERR_NOT_SUPPORTED;
}


/*********************************************************************************************************************************
*   Device enumeration + handling.                                                                                               *
*********************************************************************************************************************************/

/**
 * Enumerates audio devices and optionally searches for a specific device.
 *
 * @returns VBox status code.
 * @param   pDrvStack           Driver stack to use for enumeration.
 * @param   pszDev              Device name to search for. Can be NULL if the default device shall be used.
 * @param   ppDev               Where to return the pointer of the device enumeration of \a pTstEnv when a
 *                              specific device was found.
 */
int audioTestDevicesEnumerateAndCheck(PAUDIOTESTDRVSTACK pDrvStack, const char *pszDev, PPDMAUDIOHOSTDEV *ppDev)
{
    RTTestSubF(g_hTest, "Enumerating audio devices and checking for device '%s'", pszDev && *pszDev ? pszDev : "[Default]");

    if (!pDrvStack->pIHostAudio->pfnGetDevices)
    {
        RTTestSkipped(g_hTest, "Backend does not support device enumeration, skipping");
        return VINF_NOT_SUPPORTED;
    }

    Assert(pszDev == NULL || ppDev);

    if (ppDev)
        *ppDev = NULL;

    int rc = pDrvStack->pIHostAudio->pfnGetDevices(pDrvStack->pIHostAudio, &pDrvStack->DevEnum);
    if (RT_SUCCESS(rc))
    {
        PPDMAUDIOHOSTDEV pDev;
        RTListForEach(&pDrvStack->DevEnum.LstDevices, pDev, PDMAUDIOHOSTDEV, ListEntry)
        {
            char szFlags[PDMAUDIOHOSTDEV_MAX_FLAGS_STRING_LEN];
            if (pDev->pszId)
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Enum: Device '%s' (ID '%s'):\n", pDev->pszName, pDev->pszId);
            else
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Enum: Device '%s':\n", pDev->pszName);
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Enum:   Usage           = %s\n",   PDMAudioDirGetName(pDev->enmUsage));
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Enum:   Flags           = %s\n",   PDMAudioHostDevFlagsToString(szFlags, pDev->fFlags));
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Enum:   Input channels  = %RU8\n", pDev->cMaxInputChannels);
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Enum:   Output channels = %RU8\n", pDev->cMaxOutputChannels);

            if (   (pszDev && *pszDev)
                && !RTStrCmp(pDev->pszName, pszDev))
            {
                *ppDev = pDev;
            }
        }
    }
    else
        RTTestFailed(g_hTest, "Enumerating audio devices failed with %Rrc", rc);

    if (RT_SUCCESS(rc))
    {
        if (   (pszDev && *pszDev)
            && *ppDev == NULL)
        {
            RTTestFailed(g_hTest, "Audio device '%s' not found", pszDev);
            rc = VERR_NOT_FOUND;
        }
    }

    RTTestSubDone(g_hTest);
    return rc;
}

static int audioTestStreamInit(PAUDIOTESTDRVSTACK pDrvStack, PAUDIOTESTSTREAM pStream,
                               PDMAUDIODIR enmDir, PAUDIOTESTIOOPTS pIoOpts)
{
    int rc;

    if (enmDir == PDMAUDIODIR_IN)
        rc = audioTestDriverStackStreamCreateInput(pDrvStack, &pIoOpts->Props, pIoOpts->cMsBufferSize,
                                                   pIoOpts->cMsPreBuffer, pIoOpts->cMsSchedulingHint, &pStream->pStream, &pStream->Cfg);
    else if (enmDir == PDMAUDIODIR_OUT)
        rc = audioTestDriverStackStreamCreateOutput(pDrvStack, &pIoOpts->Props, pIoOpts->cMsBufferSize,
                                                    pIoOpts->cMsPreBuffer, pIoOpts->cMsSchedulingHint, &pStream->pStream, &pStream->Cfg);
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_SUCCESS(rc))
    {
        if (!pDrvStack->pIAudioConnector)
        {
            pStream->pBackend = &((PAUDIOTESTDRVSTACKSTREAM)pStream->pStream)->Backend;
        }
        else
            pStream->pBackend = NULL;

        /*
         * Automatically enable the mixer if the PCM properties don't match.
         */
        if (   !pIoOpts->fWithMixer
            && !PDMAudioPropsAreEqual(&pIoOpts->Props, &pStream->Cfg.Props))
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,  "Enabling stream mixer\n");
            pIoOpts->fWithMixer = true;
        }

        rc = AudioTestMixStreamInit(&pStream->Mix, pDrvStack, pStream->pStream,
                                    pIoOpts->fWithMixer ? &pIoOpts->Props : NULL, 100 /* ms */); /** @todo Configure mixer buffer? */
    }

    if (RT_FAILURE(rc))
        RTTestFailed(g_hTest, "Initializing %s stream failed with %Rrc", enmDir == PDMAUDIODIR_IN ? "input" : "output", rc);

    return rc;
}

/**
 * Destroys an audio test stream.
 *
 * @returns VBox status code.
 * @param   pDrvStack           Driver stack the stream belongs to.
 * @param   pStream             Audio stream to destroy.
 */
static int audioTestStreamDestroy(PAUDIOTESTDRVSTACK pDrvStack, PAUDIOTESTSTREAM pStream)
{
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);

    if (pStream->pStream)
    {
        /** @todo Anything else to do here, e.g. test if there are left over samples or some such? */

        audioTestDriverStackStreamDestroy(pDrvStack, pStream->pStream);
        pStream->pStream  = NULL;
        pStream->pBackend = NULL;
    }

    AudioTestMixStreamTerm(&pStream->Mix);

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Test Primitives                                                                                                              *
*********************************************************************************************************************************/

/**
 * Initializes test tone parameters (partly with random values).

 * @param   pToneParms          Test tone parameters to initialize.
 */
void audioTestToneParmsInit(PAUDIOTESTTONEPARMS pToneParms)
{
    RT_BZERO(pToneParms, sizeof(AUDIOTESTTONEPARMS));

    /**
     * Set default (randomized) test tone parameters if not set explicitly.
     */
    pToneParms->dbFreqHz       = AudioTestToneGetRandomFreq();
    pToneParms->msDuration     = RTRandU32Ex(200, RT_MS_30SEC);
    pToneParms->uVolumePercent = 100; /* We always go with maximum volume for now. */

    PDMAudioPropsInit(&pToneParms->Props,
                      2 /* 16-bit */, true /* fPcmSigned */, 2 /* cPcmChannels */, 44100 /* uPcmHz */);
}

/**
 * Initializes I/O options with some sane default values.
 *
 * @param   pIoOpts             I/O options to initialize.
 */
void audioTestIoOptsInitDefaults(PAUDIOTESTIOOPTS pIoOpts)
{
    RT_BZERO(pIoOpts, sizeof(AUDIOTESTIOOPTS));

    /* Initialize the PCM properties to some sane values. */
    PDMAudioPropsInit(&pIoOpts->Props,
                      2 /* 16-bit */, true /* fPcmSigned */, 2 /* cPcmChannels */, 44100 /* uPcmHz */);

    pIoOpts->cMsBufferSize     = UINT32_MAX;
    pIoOpts->cMsPreBuffer      = UINT32_MAX;
    pIoOpts->cMsSchedulingHint = UINT32_MAX;
    pIoOpts->uVolumePercent    = 100; /* Use maximum volume by default. */
}

#if 0 /* Unused */
/**
 * Returns a random scheduling hint (in ms).
 */
DECLINLINE(uint32_t) audioTestEnvGetRandomSchedulingHint(void)
{
    static const unsigned s_aSchedulingHintsMs[] =
    {
        10,
        25,
        50,
        100,
        200,
        250
    };

    return s_aSchedulingHintsMs[RTRandU32Ex(0, RT_ELEMENTS(s_aSchedulingHintsMs) - 1)];
}
#endif

/**
 * Plays a test tone on a specific audio test stream.
 *
 * @returns VBox status code.
 * @param   pIoOpts             I/O options to use.
 * @param   pTstEnv             Test environment to use for running the test.
 *                              Optional and can be NULL (for simple playback only).
 * @param   pStream             Stream to use for playing the tone.
 * @param   pParms              Tone parameters to use.
 *
 * @note    Blocking function.
 */
int audioTestPlayTone(PAUDIOTESTIOOPTS pIoOpts, PAUDIOTESTENV pTstEnv, PAUDIOTESTSTREAM pStream, PAUDIOTESTTONEPARMS pParms)
{
    uint32_t const idxTest = (uint8_t)pParms->Hdr.idxTest;

    AUDIOTESTTONE TstTone;
    AudioTestToneInit(&TstTone, &pStream->Cfg.Props, pParms->dbFreqHz);

    char const *pcszPathOut = NULL;
    if (pTstEnv)
        pcszPathOut = pTstEnv->Set.szPathAbs;

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Playing test tone (tone frequency is %RU16Hz, %RU32ms, %RU8%% volume)\n",
                 idxTest, (uint16_t)pParms->dbFreqHz, pParms->msDuration, pParms->uVolumePercent);
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Using %RU32ms stream scheduling hint\n",
                 idxTest, pStream->Cfg.Device.cMsSchedulingHint);
    if (pcszPathOut)
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Writing to '%s'\n", idxTest, pcszPathOut);

    int rc;

    /** @todo Use .WAV here? */
    AUDIOTESTOBJ Obj;
    RT_ZERO(Obj); /* Shut up MSVC. */
    if (pTstEnv)
    {
        rc = AudioTestSetObjCreateAndRegister(&pTstEnv->Set, "guest-tone-play.pcm", &Obj);
        AssertRCReturn(rc, rc);
    }

    uint8_t const uVolPercent = pIoOpts->uVolumePercent;
    int rc2 = audioTestSetMasterVolume(uVolPercent);
    if (RT_FAILURE(rc2))
    {
        if (rc2 == VERR_NOT_SUPPORTED)
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Setting system's master volume is not supported on this platform, skipping\n");
        else
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Setting system's master volume failed with %Rrc\n", rc2);
    }
    else
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Set system's master volume to %RU8%%\n", uVolPercent);

    rc = AudioTestMixStreamEnable(&pStream->Mix);
    if (   RT_SUCCESS(rc)
        && AudioTestMixStreamIsOkay(&pStream->Mix))
    {
        uint32_t cbToWriteTotal = PDMAudioPropsMilliToBytes(&pStream->Cfg.Props, pParms->msDuration);
        AssertStmt(cbToWriteTotal, rc = VERR_INVALID_PARAMETER);
        uint32_t cbWrittenTotal = 0;

        /* We play a pre + post beacon before + after the actual test tone.
         * We always start with the pre beacon. */
        AUDIOTESTTONEBEACON Beacon;
        AudioTestBeaconInit(&Beacon, (uint8_t)pParms->Hdr.idxTest, AUDIOTESTTONEBEACONTYPE_PLAY_PRE, &pStream->Cfg.Props);

        uint32_t const cbBeacon = AudioTestBeaconGetSize(&Beacon);
        if (cbBeacon)
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Playing 2 x %RU32 bytes pre/post beacons\n",
                        idxTest, cbBeacon);

            if (g_uVerbosity >= 2)
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Playing %s beacon ...\n",
                             idxTest, AudioTestBeaconTypeGetName(Beacon.enmType));
        }

        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Playing %RU32 bytes total (%RU32ms timeout)\n",
                     idxTest, cbToWriteTotal, pTstEnv->msTimeout);

        /* Failsafe if invalid timeout is set. */
        if (   pTstEnv->msTimeout == 0
            || pTstEnv->msTimeout == UINT32_MAX)
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Warning! Invalid timeout set (%RU32ms), setting default\n",
                         idxTest, pTstEnv->msTimeout);
            pTstEnv->msTimeout = AUDIOTEST_TIMEOUT_DEFAULT_MS;
        }

        AudioTestObjAddMetadataStr(Obj, "test_id=%04RU32\n", pParms->Hdr.idxTest);
        AudioTestObjAddMetadataStr(Obj, "beacon_type=%RU32\n", (uint32_t)AudioTestBeaconGetType(&Beacon));
        AudioTestObjAddMetadataStr(Obj, "beacon_pre_bytes=%RU32\n", cbBeacon);
        AudioTestObjAddMetadataStr(Obj, "beacon_post_bytes=%RU32\n", cbBeacon);
        AudioTestObjAddMetadataStr(Obj, "stream_to_write_total_bytes=%RU32\n",  cbToWriteTotal);
        AudioTestObjAddMetadataStr(Obj, "stream_period_size_frames=%RU32\n", pStream->Cfg.Backend.cFramesPeriod);
        AudioTestObjAddMetadataStr(Obj, "stream_buffer_size_frames=%RU32\n", pStream->Cfg.Backend.cFramesBufferSize);
        AudioTestObjAddMetadataStr(Obj, "stream_prebuf_size_frames=%RU32\n", pStream->Cfg.Backend.cFramesPreBuffering);
        /* Note: This mostly is provided by backend (e.g. PulseAudio / ALSA / ++) and
         *       has nothing to do with the device emulation scheduling hint. */
        AudioTestObjAddMetadataStr(Obj, "device_scheduling_hint_ms=%RU32\n", pStream->Cfg.Device.cMsSchedulingHint);

        PAUDIOTESTDRVMIXSTREAM pMix = &pStream->Mix;

        uint32_t const  cbPreBuffer        = PDMAudioPropsFramesToBytes(pMix->pProps, pStream->Cfg.Backend.cFramesPreBuffering);
        uint64_t const  nsStarted          = RTTimeNanoTS();
        uint64_t        nsDonePreBuffering = 0;

        uint64_t        offStream          = 0;
        uint64_t        nsTimeout          = uint64_t(pTstEnv->msTimeout) * RT_NS_1MS_64;
        uint64_t        nsLastMsgCantWrite = 0; /* Timestamp (in ns) when the last message of an unwritable stream was shown. */
        uint64_t        nsLastWrite        = 0;

        AUDIOTESTSTATE  enmState           = AUDIOTESTSTATE_PRE;
        uint8_t         abBuf[_16K];

        for (;;)
        {
            uint64_t const nsNow = RTTimeNanoTS();
            if (!nsLastWrite)
                nsLastWrite = nsNow;

            /* Pace ourselves a little. */
            if (offStream >= cbPreBuffer)
            {
                if (!nsDonePreBuffering)
                    nsDonePreBuffering = nsNow;
                uint64_t const cNsWritten = PDMAudioPropsBytesToNano64(pMix->pProps, offStream - cbPreBuffer);
                uint64_t const cNsElapsed = nsNow - nsStarted;
                if (cNsWritten > cNsElapsed + RT_NS_10MS)
                    RTThreadSleep(uint32_t(cNsWritten - cNsElapsed - RT_NS_10MS / 2) / RT_NS_1MS);
            }

            uint32_t       cbWritten  = 0;
            uint32_t const cbCanWrite = AudioTestMixStreamGetWritable(&pStream->Mix);
            if (cbCanWrite)
            {
                if (g_uVerbosity >= 4)
                    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Stream is writable with %RU64ms (%RU32 bytes)\n",
                                 idxTest, PDMAudioPropsBytesToMilli(pMix->pProps, cbCanWrite), cbCanWrite);

                switch (enmState)
                {
                    case AUDIOTESTSTATE_PRE:
                        RT_FALL_THROUGH();
                    case AUDIOTESTSTATE_POST:
                    {
                        if (g_uVerbosity >= 4)
                            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: %RU32 bytes (%RU64ms) beacon data remaining\n",
                                         idxTest, AudioTestBeaconGetRemaining(&Beacon),
                                         PDMAudioPropsBytesToMilli(&pStream->pStream->Cfg.Props, AudioTestBeaconGetRemaining(&Beacon)));

                        bool fGoToNextStage = false;

                        if (    AudioTestBeaconGetSize(&Beacon)
                            && !AudioTestBeaconIsComplete(&Beacon))
                        {
                            bool const fStarted = AudioTestBeaconGetRemaining(&Beacon) == AudioTestBeaconGetSize(&Beacon);

                            uint32_t const cbBeaconRemaining = AudioTestBeaconGetRemaining(&Beacon);
                            AssertBreakStmt(cbBeaconRemaining, rc = VERR_WRONG_ORDER);

                            /* Limit to exactly one beacon (pre or post). */
                            uint32_t const cbToWrite = RT_MIN(sizeof(abBuf), RT_MIN(cbCanWrite, cbBeaconRemaining));

                            rc = AudioTestBeaconWrite(&Beacon, abBuf, cbToWrite);
                            if (RT_SUCCESS(rc))
                            {
                                rc = AudioTestMixStreamPlay(&pStream->Mix, abBuf, cbToWrite, &cbWritten);
                                if (   RT_SUCCESS(rc)
                                    && pTstEnv)
                                {
                                    /* Also write the beacon data to the test object.
                                     * Note: We use cbPlayed here instead of cbToPlay to know if the data actually was
                                     *       reported as being played by the audio stack. */
                                    rc = AudioTestObjWrite(Obj, abBuf, cbWritten);
                                }
                            }

                            if (   fStarted
                                && g_uVerbosity >= 2)
                                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Writing %s beacon begin\n",
                                             idxTest, AudioTestBeaconTypeGetName(Beacon.enmType));
                            if (AudioTestBeaconIsComplete(&Beacon))
                            {
                                if (g_uVerbosity >= 2)
                                    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Writing %s beacon end\n",
                                                 idxTest, AudioTestBeaconTypeGetName(Beacon.enmType));
                                fGoToNextStage = true;
                            }
                        }
                        else
                            fGoToNextStage = true;

                        if (fGoToNextStage)
                        {
                            if (enmState == AUDIOTESTSTATE_PRE)
                                enmState = AUDIOTESTSTATE_RUN;
                            else if (enmState == AUDIOTESTSTATE_POST)
                                enmState = AUDIOTESTSTATE_DONE;
                        }
                        break;
                    }

                    case AUDIOTESTSTATE_RUN:
                    {
                        uint32_t cbToWrite = RT_MIN(sizeof(abBuf), cbCanWrite);
                                 cbToWrite = RT_MIN(cbToWrite, cbToWriteTotal - cbWrittenTotal);

                        if (g_uVerbosity >= 4)
                            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                                         "Test #%RU32: Playing back %RU32 bytes\n", idxTest, cbToWrite);

                        if (cbToWrite)
                        {
                            rc = AudioTestToneGenerate(&TstTone, abBuf, cbToWrite, &cbToWrite);
                            if (RT_SUCCESS(rc))
                            {
                                if (pTstEnv)
                                {
                                    /* Write stuff to disk before trying to play it. Helps analysis later. */
                                    rc = AudioTestObjWrite(Obj, abBuf, cbToWrite);
                                }

                                if (RT_SUCCESS(rc))
                                {
                                    rc = AudioTestMixStreamPlay(&pStream->Mix, abBuf, cbToWrite, &cbWritten);
                                    if (RT_SUCCESS(rc))
                                    {
                                        AssertBreakStmt(cbWritten <= cbToWrite, rc = VERR_TOO_MUCH_DATA);

                                        offStream += cbWritten;

                                        if (cbWritten != cbToWrite)
                                            RTTestFailed(g_hTest, "Test #%RU32: Only played %RU32/%RU32 bytes\n",
                                                         idxTest, cbWritten, cbToWrite);

                                        if (cbWritten)
                                            nsLastWrite = nsNow;

                                        if (g_uVerbosity >= 4)
                                            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                                                         "Test #%RU32: Played back %RU32 bytes\n", idxTest, cbWritten);

                                        cbWrittenTotal += cbWritten;
                                    }
                                }
                            }
                        }

                        if (RT_SUCCESS(rc))
                        {
                            const bool fComplete = cbWrittenTotal >= cbToWriteTotal;
                            if (fComplete)
                            {
                                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Playing back audio data ended\n", idxTest);

                                enmState = AUDIOTESTSTATE_POST;

                                /* Re-use the beacon object, but this time it's the post beacon. */
                                AudioTestBeaconInit(&Beacon, (uint8_t)idxTest, AUDIOTESTTONEBEACONTYPE_PLAY_POST,
                                                    &pStream->Cfg.Props);
                            }
                        }
                        else
                            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Playing back failed with %Rrc\n", idxTest, rc);
                        break;
                    }

                    case AUDIOTESTSTATE_DONE:
                    {
                        /* Handled below. */
                        break;
                    }

                    default:
                        AssertFailed();
                        break;
                }

                if (RT_FAILURE(rc))
                    break;

                if (enmState == AUDIOTESTSTATE_DONE)
                    break;

                nsLastMsgCantWrite = 0;
            }
            else if (AudioTestMixStreamIsOkay(&pStream->Mix))
            {
                RTMSINTERVAL const msSleep = RT_MIN(RT_MAX(1, pStream->Cfg.Device.cMsSchedulingHint), 256);

                if (   g_uVerbosity >= 3
                    && (   !nsLastMsgCantWrite
                        || (nsNow - nsLastMsgCantWrite) > RT_NS_10SEC)) /* Don't spam the output too much. */
                {
                    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Waiting %RU32ms for stream to be writable again (last write %RU64ns ago) ...\n",
                                 idxTest, msSleep, nsNow - nsLastWrite);
                    nsLastMsgCantWrite = nsNow;
                }

                RTThreadSleep(msSleep);
            }
            else
                AssertFailedBreakStmt(rc = VERR_AUDIO_STREAM_NOT_READY);

            /* Fail-safe in case something screwed up while playing back. */
            uint64_t const cNsElapsed = nsNow - nsStarted;
            if (cNsElapsed > nsTimeout)
            {
                RTTestFailed(g_hTest, "Test #%RU32: Playback took too long (running %RU64 vs. timeout %RU64), aborting\n",
                             idxTest, cNsElapsed, nsTimeout);
                rc = VERR_TIMEOUT;
            }

            if (RT_FAILURE(rc))
                break;
        } /* for */

        if (cbWrittenTotal != cbToWriteTotal)
        {
            RTTestFailed(g_hTest, "Test #%RU32: Playback ended unexpectedly (%RU32 played, expected %RU32)\n",
                         idxTest, cbWrittenTotal, cbToWriteTotal);
            rc = cbWrittenTotal > cbToWriteTotal ? VERR_BUFFER_OVERFLOW : VERR_BUFFER_UNDERFLOW;
        }

        if (RT_SUCCESS(rc))
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Draining stream ...\n", idxTest);
            rc = AudioTestMixStreamDrain(&pStream->Mix, true /*fSync*/);
        }
    }
    else
        rc = VERR_AUDIO_STREAM_NOT_READY;

    if (pTstEnv)
    {
        rc2 = AudioTestObjClose(Obj);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    if (RT_FAILURE(rc))
        RTTestFailed(g_hTest, "Test #%RU32: Playing tone failed with %Rrc\n", idxTest, rc);

    return rc;
}

/**
 * Records a test tone from a specific audio test stream.
 *
 * @returns VBox status code.
 * @param   pIoOpts             I/O options to use.
 * @param   pTstEnv             Test environment to use for running the test.
 * @param   pStream             Stream to use for recording the tone.
 * @param   pParms              Tone parameters to use.
 *
 * @note    Blocking function.
 */
static int audioTestRecordTone(PAUDIOTESTIOOPTS pIoOpts, PAUDIOTESTENV pTstEnv, PAUDIOTESTSTREAM pStream, PAUDIOTESTTONEPARMS pParms)
{
    uint32_t const idxTest = (uint8_t)pParms->Hdr.idxTest;

    const char *pcszPathOut = pTstEnv->Set.szPathAbs;

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Recording test tone (tone frequency is %RU16Hz, %RU32ms)\n",
                 idxTest, (uint16_t)pParms->dbFreqHz, pParms->msDuration);
    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG,  "Test #%RU32: Writing to '%s'\n", idxTest, pcszPathOut);

    /** @todo Use .WAV here? */
    AUDIOTESTOBJ Obj;
    int rc = AudioTestSetObjCreateAndRegister(&pTstEnv->Set, "guest-tone-rec.pcm", &Obj);
    AssertRCReturn(rc, rc);

    PAUDIOTESTDRVMIXSTREAM pMix = &pStream->Mix;

    rc = AudioTestMixStreamEnable(pMix);
    if (RT_SUCCESS(rc))
    {
        size_t cbToReadTotal = PDMAudioPropsMilliToBytes(&pStream->Cfg.Props, pParms->msDuration);
        AssertStmt(cbToReadTotal, rc = VERR_INVALID_PARAMETER);
        size_t cbReadTotal   = 0; /* Counts the read test tone data (w/o any beacons). */

        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Recording %RU32 bytes total (%RU32ms timeout)\n",
                     idxTest, cbToReadTotal, pTstEnv->msTimeout);

        /* Failsafe if invalid timeout is set. */
        if (   pTstEnv->msTimeout == 0
            || pTstEnv->msTimeout == UINT32_MAX)
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Warning! Invalid timeout set (%RU32ms), setting default\n",
                         idxTest, pTstEnv->msTimeout);
            pTstEnv->msTimeout = AUDIOTEST_TIMEOUT_DEFAULT_MS;
        }

        /* We expect a pre + post beacon before + after the actual test tone.
         * We always start with the pre beacon. */
        AUDIOTESTTONEBEACON Beacon;
        AudioTestBeaconInit(&Beacon, (uint8_t)pParms->Hdr.idxTest, AUDIOTESTTONEBEACONTYPE_PLAY_PRE, &pStream->Cfg.Props);

        uint32_t const cbBeacon = AudioTestBeaconGetSize(&Beacon);
        if (cbBeacon)
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Expecting 2 x %RU32 bytes pre/post beacons\n",
                         idxTest, cbBeacon);
            if (g_uVerbosity >= 2)
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Waiting for %s beacon ...\n",
                             idxTest, AudioTestBeaconTypeGetName(Beacon.enmType));
        }

        AudioTestObjAddMetadataStr(Obj, "test_id=%04RU32\n", pParms->Hdr.idxTest);
        AudioTestObjAddMetadataStr(Obj, "beacon_type=%RU32\n", (uint32_t)AudioTestBeaconGetType(&Beacon));
        AudioTestObjAddMetadataStr(Obj, "beacon_pre_bytes=%RU32\n", cbBeacon);
        AudioTestObjAddMetadataStr(Obj, "beacon_post_bytes=%RU32\n", cbBeacon);
        AudioTestObjAddMetadataStr(Obj, "stream_to_record_bytes=%RU32\n", cbToReadTotal);
        AudioTestObjAddMetadataStr(Obj, "stream_buffer_size_ms=%RU32\n", pIoOpts->cMsBufferSize);
        AudioTestObjAddMetadataStr(Obj, "stream_prebuf_size_ms=%RU32\n", pIoOpts->cMsPreBuffer);
        /* Note: This mostly is provided by backend (e.g. PulseAudio / ALSA / ++) and
         *       has nothing to do with the device emulation scheduling hint. */
        AudioTestObjAddMetadataStr(Obj, "device_scheduling_hint_ms=%RU32\n", pIoOpts->cMsSchedulingHint);

        PRTCIRCBUF pCircBuf;
        rc = RTCircBufCreate(&pCircBuf, _64K);
        AssertRCReturn(rc, rc);

        uint64_t const  nsStarted         = RTTimeNanoTS();

        uint64_t        nsTimeout         = uint64_t(pTstEnv->msTimeout) * RT_NS_1MS_64;
        uint64_t        nsLastMsgCantRead = 0; /* Timestamp (in ns) when the last message of an unreadable stream was shown. */

        AUDIOTESTSTATE  enmState          = AUDIOTESTSTATE_PRE;

        while (!g_fTerminate)
        {
            uint64_t const nsNow  = RTTimeNanoTS();

            void  *pvBlock;
            size_t cbBlock;

            /*
             * Anything we can capture from the stream?
             */
            uint32_t const cbCanRead = AudioTestMixStreamGetReadable(pMix);
            if (cbCanRead)
            {
                RTCircBufAcquireWriteBlock(pCircBuf, cbCanRead, &pvBlock, &cbBlock);

                uint32_t cbCaptured = 0;
                if (cbBlock)
                {
                    rc = AudioTestMixStreamCapture(pMix, pvBlock, (uint32_t)cbBlock, &cbCaptured);
                    if (RT_FAILURE(rc))
                       RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Reading from stream failed with %Rrc\n", idxTest, rc);

                    if (g_uVerbosity >= 2)
                        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Stream is readable with %RU64ms (%RU32 bytes), captured %RU64ms (%RU32 bytes)\n",
                                     idxTest,
                                     PDMAudioPropsBytesToMilli(pMix->pProps, cbCanRead), cbCanRead,
                                     PDMAudioPropsBytesToMilli(pMix->pProps, cbCaptured), cbCaptured);

                    if (RT_FAILURE(rc))
                        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Capturing failed with %Rrc\n",
                                     idxTest, rc);
                }

                RTCircBufReleaseWriteBlock(pCircBuf, cbCaptured);
            }
            else if (AudioTestMixStreamIsOkay(pMix))
            {
                RTMSINTERVAL const msSleep = RT_MIN(RT_MAX(1, pStream->Cfg.Device.cMsSchedulingHint), 256);

                if (   g_uVerbosity >= 3
                    && (   !nsLastMsgCantRead
                        || (nsNow - nsLastMsgCantRead) > RT_NS_10SEC)) /* Don't spam the output too much. */
                {
                    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Waiting %RU32ms for stream to be readable again ...\n",
                                 idxTest, msSleep);
                    nsLastMsgCantRead = nsNow;
                }

                RTThreadSleep(msSleep);
            }

            /*
             * Process our buffer.
             */
            size_t cbBlockToAcq;
            if (pTstEnv->fSelftest) /* For self-test mode we want to have a bit more randomness. */
            {
                size_t const u = (uint32_t)RTCircBufUsed(pCircBuf);
                size_t const r = RTRandU32Ex(1, (uint32_t)RTCircBufSize(pCircBuf));
                cbBlockToAcq   = PDMAudioPropsFloorBytesToFrame(pMix->pProps, RT_MIN((uint32_t)u, (uint32_t)r));
            }
            else
                cbBlockToAcq = PDMAudioPropsFloorBytesToFrame(pMix->pProps, (uint32_t)RTCircBufUsed(pCircBuf));

            RTCircBufAcquireReadBlock(pCircBuf, cbBlockToAcq, &pvBlock, &cbBlock);
            if (!cbBlock)
                continue;

            /* Flag indicating whether the whole block we've captured is silence or not. */
            bool const fIsAllSilence = PDMAudioPropsIsBufferSilence(&pStream->pStream->Cfg.Props, pvBlock, cbBlock);
            size_t     cbRead = 0;

            switch (enmState)
            {
                case AUDIOTESTSTATE_PRE:
                    RT_FALL_THROUGH();
                case AUDIOTESTSTATE_POST:
                {
                    bool fGoToNextStage = false;

                    if (    AudioTestBeaconGetSize(&Beacon)
                        && !AudioTestBeaconIsComplete(&Beacon))
                    {
                        bool const fStarted = AudioTestBeaconGetRemaining(&Beacon) == AudioTestBeaconGetSize(&Beacon);

                        size_t uOff;
                        int rc2 = AudioTestBeaconAddConsecutive(&Beacon, (uint8_t *)pvBlock, cbBlock, &uOff);
                        if (   RT_SUCCESS(rc2)
                            && uOff)
                            cbRead = uOff;

                        if (   fStarted
                            && g_uVerbosity >= 2)
                        {
                            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                                         "Test #%RU32: Detection of %s beacon started (%RU64ms recorded so far)\n",
                                         idxTest, AudioTestBeaconTypeGetName(Beacon.enmType),
                                         PDMAudioPropsBytesToMilli(&pStream->pStream->Cfg.Props, (uint32_t)cbReadTotal));
                        }

                        if (AudioTestBeaconIsComplete(&Beacon))
                        {
                            if (g_uVerbosity >= 2)
                                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Detection of %s beacon complete\n",
                                             idxTest, AudioTestBeaconTypeGetName(Beacon.enmType));
                            fGoToNextStage = true;
                        }
                    }
                    else
                        fGoToNextStage = true;

                    if (fGoToNextStage)
                    {
                        if (enmState == AUDIOTESTSTATE_PRE)
                            enmState = AUDIOTESTSTATE_RUN;
                        else if (enmState == AUDIOTESTSTATE_POST)
                            enmState = AUDIOTESTSTATE_DONE;
                    }
                    break;
                }

                case AUDIOTESTSTATE_RUN:
                {
                    /* Whether we count all silence as recorded data or not.
                     * Currently we don't, as otherwise consequtively played tones will be cut off in the end. */
                    bool const fRecord = !fIsAllSilence;
                    if (fRecord)
                    {
                        /* Don't read more than we're told to.
                         * After the actual test tone data there might come a post beacon which also
                         * needs to be handled in the AUDIOTESTSTATE_POST state then. */
                        if (cbReadTotal + cbBlock > cbToReadTotal)
                        {
                            AssertBreakStmt(cbToReadTotal >= cbReadTotal, rc = VERR_INTERNAL_ERROR);
                            cbRead = cbToReadTotal - cbReadTotal;
                        }
                        else
                            cbRead = cbBlock;

                        cbReadTotal += cbRead;
                        AssertBreakStmt(cbReadTotal <= cbToReadTotal, rc = VERR_INTERNAL_ERROR);
                    }

                    /* Done recording the test tone? */
                    if (cbReadTotal + cbRead == cbToReadTotal)
                    {
                        enmState = AUDIOTESTSTATE_POST;

                        if (g_uVerbosity >= 2)
                            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Recording tone data done\n", idxTest);

                        if (AudioTestBeaconGetSize(&Beacon))
                        {
                            /* Re-use the beacon object, but this time it's the post beacon. */
                            AudioTestBeaconInit(&Beacon, (uint8_t)pParms->Hdr.idxTest, AUDIOTESTTONEBEACONTYPE_PLAY_POST,
                                                &pStream->Cfg.Props);
                            if (g_uVerbosity >= 2)
                                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                                             "Test #%RU32: Waiting for %s beacon ...\n",
                                             idxTest, AudioTestBeaconTypeGetName(Beacon.enmType));
                        }
                    }
                    break;
                }

                case AUDIOTESTSTATE_DONE:
                {
                    /* Nothing to do here. */
                    break;
                }

                default:
                    AssertFailed();
                    break;
            }

            if (cbRead)
            {
                if (g_uVerbosity >= 3)
                    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Read data (%zu bytes):\n"
                                                            "%.*Rhxd\n",
                                 idxTest, cbRead, cbRead, pvBlock);

                /* Always write (record) everything, no matter if the current audio contains complete silence or not.
                 * Might be also become handy later if we want to have a look at start/stop timings and so on. */
                rc = AudioTestObjWrite(Obj, pvBlock, cbRead);
                AssertRCBreak(rc);
            }

            if (g_uVerbosity >= 2)
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Processed %RU64ms (%zu bytes)\n",
                             idxTest, PDMAudioPropsBytesToMilli(pMix->pProps, (uint32_t)cbRead), cbRead);

            RTCircBufReleaseReadBlock(pCircBuf, cbRead);

            if (enmState == AUDIOTESTSTATE_DONE) /* Bail out when in state "done". */
                break;

            /* Fail-safe in case something screwed up while playing back. */
            uint64_t const cNsElapsed = nsNow - nsStarted;
            if (cNsElapsed > nsTimeout)
            {
                RTTestFailed(g_hTest, "Test #%RU32: Recording took too long (running %RU64 vs. timeout %RU64), aborting\n",
                             idxTest, cNsElapsed, nsTimeout);
                rc = VERR_TIMEOUT;
            }

            if (RT_FAILURE(rc))
                break;
        } /* while */

        if (g_uVerbosity >= 2)
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test #%RU32: Recorded %zu bytes total\n", idxTest, cbReadTotal);
        if (cbReadTotal != cbToReadTotal)
        {
            RTTestFailed(g_hTest, "Test #%RU32: Recording ended unexpectedly (%zu read, expected %zu)\n",
                         idxTest, cbReadTotal, cbToReadTotal);
            int rc2 = cbReadTotal > cbToReadTotal ? VERR_BUFFER_OVERFLOW : VERR_BUFFER_UNDERFLOW;
            if (RT_SUCCESS(rc))
                rc = rc2;
        }

        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "Test #%RU32: Recording failed (state is '%s')\n", idxTest, AudioTestStateToStr(enmState));

        int rc2 = AudioTestMixStreamDisable(pMix);
        if (RT_SUCCESS(rc))
            rc = rc2;

        RTCircBufDestroy(pCircBuf);
    }

    int rc2 = AudioTestObjClose(Obj);
    if (RT_SUCCESS(rc))
        rc = rc2;

    if (RT_FAILURE(rc))
        RTTestFailed(g_hTest, "Test #%RU32: Recording tone done failed with %Rrc\n", idxTest, rc);

    return rc;
}


/*********************************************************************************************************************************
*   ATS Callback Implementations                                                                                                 *
*********************************************************************************************************************************/

/** @copydoc ATSCALLBACKS::pfnHowdy
 *
 *  @note Runs as part of the guest ATS.
 */
static DECLCALLBACK(int) audioTestGstAtsHowdyCallback(void const *pvUser)
{
    PATSCALLBACKCTX pCtx = (PATSCALLBACKCTX)pvUser;

    AssertReturn(pCtx->cClients <= UINT8_MAX - 1, VERR_BUFFER_OVERFLOW);

    pCtx->cClients++;

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "New client connected, now %RU8 total\n", pCtx->cClients);

    return VINF_SUCCESS;
}

/** @copydoc ATSCALLBACKS::pfnBye
 *
 *  @note Runs as part of the guest ATS.
 */
static DECLCALLBACK(int) audioTestGstAtsByeCallback(void const *pvUser)
{
    PATSCALLBACKCTX pCtx = (PATSCALLBACKCTX)pvUser;

    AssertReturn(pCtx->cClients, VERR_WRONG_ORDER);
    pCtx->cClients--;

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Client wants to disconnect, %RU8 remaining\n", pCtx->cClients);

    if (0 == pCtx->cClients) /* All clients disconnected? Tear things down. */
    {
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Last client disconnected, terminating server ...\n");
        ASMAtomicWriteBool(&g_fTerminate, true);
    }

    return VINF_SUCCESS;
}

/** @copydoc ATSCALLBACKS::pfnTestSetBegin
 *
 *  @note Runs as part of the guest ATS.
 */
static DECLCALLBACK(int) audioTestGstAtsTestSetBeginCallback(void const *pvUser, const char *pszTag)
{
    PATSCALLBACKCTX pCtx    = (PATSCALLBACKCTX)pvUser;
    PAUDIOTESTENV   pTstEnv = pCtx->pTstEnv;

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Got request for beginning test set '%s' in '%s'\n", pszTag, pTstEnv->szPathTemp);

    return AudioTestSetCreate(&pTstEnv->Set, pTstEnv->szPathTemp, pszTag);
}

/** @copydoc ATSCALLBACKS::pfnTestSetEnd
 *
 *  @note Runs as part of the guest ATS.
 */
static DECLCALLBACK(int) audioTestGstAtsTestSetEndCallback(void const *pvUser, const char *pszTag)
{
    PATSCALLBACKCTX pCtx    = (PATSCALLBACKCTX)pvUser;
    PAUDIOTESTENV   pTstEnv = pCtx->pTstEnv;

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Got request for ending test set '%s'\n", pszTag);

    /* Pack up everything to be ready for transmission. */
    return audioTestEnvPrologue(pTstEnv, true /* fPack */, pCtx->szTestSetArchive, sizeof(pCtx->szTestSetArchive));
}

/** @copydoc ATSCALLBACKS::pfnTonePlay
 *
 *  @note Runs as part of the guest ATS.
 */
static DECLCALLBACK(int) audioTestGstAtsTonePlayCallback(void const *pvUser, PAUDIOTESTTONEPARMS pToneParms)
{
    PATSCALLBACKCTX  pCtx    = (PATSCALLBACKCTX)pvUser;
    PAUDIOTESTENV    pTstEnv = pCtx->pTstEnv;
    PAUDIOTESTIOOPTS pIoOpts = &pTstEnv->IoOpts;

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Got request for playing test tone #%RU32 (%RU16Hz, %RU32ms) ...\n",
                 pToneParms->Hdr.idxTest, (uint16_t)pToneParms->dbFreqHz, pToneParms->msDuration);

    char szTimeCreated[RTTIME_STR_LEN];
    RTTimeToString(&pToneParms->Hdr.tsCreated, szTimeCreated, sizeof(szTimeCreated));
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test created (caller UTC): %s\n", szTimeCreated);

    const PAUDIOTESTSTREAM pTstStream = &pTstEnv->aStreams[0]; /** @todo Make this dynamic. */

    int rc = audioTestStreamInit(pTstEnv->pDrvStack, pTstStream, PDMAUDIODIR_OUT, pIoOpts);
    if (RT_SUCCESS(rc))
    {
        AUDIOTESTPARMS TstParms;
        RT_ZERO(TstParms);
        TstParms.enmType  = AUDIOTESTTYPE_TESTTONE_PLAY;
        TstParms.enmDir   = PDMAUDIODIR_OUT;
        TstParms.TestTone = *pToneParms;

        PAUDIOTESTENTRY pTst;
        rc = AudioTestSetTestBegin(&pTstEnv->Set, "Playing test tone", &TstParms, &pTst);
        if (RT_SUCCESS(rc))
        {
            rc = audioTestPlayTone(&pTstEnv->IoOpts, pTstEnv, pTstStream, pToneParms);
            if (RT_SUCCESS(rc))
            {
                AudioTestSetTestDone(pTst);
            }
            else
                AudioTestSetTestFailed(pTst, rc, "Playing tone failed");
        }

        int rc2 = audioTestStreamDestroy(pTstEnv->pDrvStack, pTstStream);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }
    else
        RTTestFailed(g_hTest, "Error creating output stream, rc=%Rrc\n", rc);

    return rc;
}

/** @copydoc ATSCALLBACKS::pfnToneRecord */
static DECLCALLBACK(int) audioTestGstAtsToneRecordCallback(void const *pvUser, PAUDIOTESTTONEPARMS pToneParms)
{
    PATSCALLBACKCTX  pCtx    = (PATSCALLBACKCTX)pvUser;
    PAUDIOTESTENV    pTstEnv = pCtx->pTstEnv;
    PAUDIOTESTIOOPTS pIoOpts = &pTstEnv->IoOpts;

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Got request for recording test tone #%RU32 (%RU32ms) ...\n",
                 pToneParms->Hdr.idxTest, pToneParms->msDuration);

    char szTimeCreated[RTTIME_STR_LEN];
    RTTimeToString(&pToneParms->Hdr.tsCreated, szTimeCreated, sizeof(szTimeCreated));
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test created (caller UTC): %s\n", szTimeCreated);

    const PAUDIOTESTSTREAM pTstStream = &pTstEnv->aStreams[0]; /** @todo Make this dynamic. */

    int rc = audioTestStreamInit(pTstEnv->pDrvStack, pTstStream, PDMAUDIODIR_IN, pIoOpts);
    if (RT_SUCCESS(rc))
    {
        AUDIOTESTPARMS TstParms;
        RT_ZERO(TstParms);
        TstParms.enmType  = AUDIOTESTTYPE_TESTTONE_RECORD;
        TstParms.enmDir   = PDMAUDIODIR_IN;
        TstParms.TestTone = *pToneParms;

        PAUDIOTESTENTRY pTst;
        rc = AudioTestSetTestBegin(&pTstEnv->Set, "Recording test tone from host", &TstParms, &pTst);
        if (RT_SUCCESS(rc))
        {
            rc = audioTestRecordTone(pIoOpts, pTstEnv, pTstStream, pToneParms);
            if (RT_SUCCESS(rc))
            {
                AudioTestSetTestDone(pTst);
            }
            else
                AudioTestSetTestFailed(pTst, rc, "Recording tone failed");
        }

        int rc2 = audioTestStreamDestroy(pTstEnv->pDrvStack, pTstStream);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }
    else
        RTTestFailed(g_hTest, "Error creating input stream, rc=%Rrc\n", rc);

    return rc;
}

/** @copydoc ATSCALLBACKS::pfnTestSetSendBegin */
static DECLCALLBACK(int) audioTestGstAtsTestSetSendBeginCallback(void const *pvUser, const char *pszTag)
{
    RT_NOREF(pszTag);

    PATSCALLBACKCTX pCtx = (PATSCALLBACKCTX)pvUser;

    if (!RTFileExists(pCtx->szTestSetArchive)) /* Has the archive successfully been created yet? */
        return VERR_WRONG_ORDER;

    int rc = RTFileOpen(&pCtx->hTestSetArchive, pCtx->szTestSetArchive, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        uint64_t uSize;
        rc = RTFileQuerySize(pCtx->hTestSetArchive, &uSize);
        if (RT_SUCCESS(rc))
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Sending test set '%s' (%zu bytes)\n", pCtx->szTestSetArchive, uSize);
    }

    return rc;
}

/** @copydoc ATSCALLBACKS::pfnTestSetSendRead */
static DECLCALLBACK(int) audioTestGstAtsTestSetSendReadCallback(void const *pvUser,
                                                                const char *pszTag, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    RT_NOREF(pszTag);

    PATSCALLBACKCTX pCtx = (PATSCALLBACKCTX)pvUser;

    return RTFileRead(pCtx->hTestSetArchive, pvBuf, cbBuf, pcbRead);
}

/** @copydoc ATSCALLBACKS::pfnTestSetSendEnd */
static DECLCALLBACK(int) audioTestGstAtsTestSetSendEndCallback(void const *pvUser, const char *pszTag)
{
    RT_NOREF(pszTag);

    PATSCALLBACKCTX pCtx = (PATSCALLBACKCTX)pvUser;

    int rc = RTFileClose(pCtx->hTestSetArchive);
    if (RT_SUCCESS(rc))
    {
        pCtx->hTestSetArchive = NIL_RTFILE;
    }

    return rc;
}


/*********************************************************************************************************************************
*   Implementation of audio test environment handling                                                                            *
*********************************************************************************************************************************/

/**
 * Connects an ATS client via TCP/IP to a peer.
 *
 * @returns VBox status code.
 * @param   pTstEnv             Test environment to use.
 * @param   pClient             Client to connect.
 * @param   pszWhat             Hint of what to connect to where.
 * @param   pTcpOpts            Pointer to TCP options to use.
 */
static int audioTestEnvConnectViaTcp(PAUDIOTESTENV pTstEnv, PATSCLIENT pClient, const char *pszWhat, PAUDIOTESTENVTCPOPTS pTcpOpts)
{
    RT_NOREF(pTstEnv);

    RTGETOPTUNION Val;
    RT_ZERO(Val);

    Val.u32 = pTcpOpts->enmConnMode;
    int rc = AudioTestSvcClientHandleOption(pClient, ATSTCPOPT_CONN_MODE, &Val);
    AssertRCReturn(rc, rc);

    if (   pTcpOpts->enmConnMode == ATSCONNMODE_BOTH
        || pTcpOpts->enmConnMode == ATSCONNMODE_SERVER)
    {
        Assert(pTcpOpts->uBindPort); /* Always set by the caller. */
        Val.u16 = pTcpOpts->uBindPort;
        rc = AudioTestSvcClientHandleOption(pClient, ATSTCPOPT_BIND_PORT, &Val);
        AssertRCReturn(rc, rc);

        if (pTcpOpts->szBindAddr[0])
        {
            Val.psz = pTcpOpts->szBindAddr;
            rc = AudioTestSvcClientHandleOption(pClient, ATSTCPOPT_BIND_ADDRESS, &Val);
            AssertRCReturn(rc, rc);
        }
        else
        {
            RTTestFailed(g_hTest, "No bind address specified!\n");
            return VERR_INVALID_PARAMETER;
        }

        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Connecting %s by listening as server at %s:%RU32 ...\n",
                     pszWhat, pTcpOpts->szBindAddr, pTcpOpts->uBindPort);
    }


    if (   pTcpOpts->enmConnMode == ATSCONNMODE_BOTH
        || pTcpOpts->enmConnMode == ATSCONNMODE_CLIENT)
    {
        Assert(pTcpOpts->uConnectPort); /* Always set by the caller. */
        Val.u16 = pTcpOpts->uConnectPort;
        rc = AudioTestSvcClientHandleOption(pClient, ATSTCPOPT_CONNECT_PORT, &Val);
        AssertRCReturn(rc, rc);

        if (pTcpOpts->szConnectAddr[0])
        {
            Val.psz = pTcpOpts->szConnectAddr;
            rc = AudioTestSvcClientHandleOption(pClient, ATSTCPOPT_CONNECT_ADDRESS, &Val);
            AssertRCReturn(rc, rc);
        }
        else
        {
            RTTestFailed(g_hTest, "No connect address specified!\n");
            return VERR_INVALID_PARAMETER;
        }

        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Connecting %s by connecting as client to %s:%RU32 ...\n",
                     pszWhat, pTcpOpts->szConnectAddr, pTcpOpts->uConnectPort);
    }

    rc = AudioTestSvcClientConnect(pClient);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Connecting %s failed with %Rrc\n", pszWhat, rc);
        return rc;
    }

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Successfully connected %s\n", pszWhat);
    return rc;
}

/**
 * Configures and starts an ATS TCP/IP server.
 *
 * @returns VBox status code.
 * @param   pSrv                ATS server instance to configure and start.
 * @param   pCallbacks          ATS callback table to use.
 * @param   pszDesc             Hint of server type which is being started.
 * @param   pTcpOpts            TCP options to use.
 */
static int audioTestEnvConfigureAndStartTcpServer(PATSSERVER pSrv, PCATSCALLBACKS pCallbacks, const char *pszDesc,
                                                  PAUDIOTESTENVTCPOPTS pTcpOpts)
{
    RTGETOPTUNION Val;
    RT_ZERO(Val);

    int rc = AudioTestSvcInit(pSrv, pCallbacks);
    if (RT_FAILURE(rc))
        return rc;

    Val.u32 = pTcpOpts->enmConnMode;
    rc = AudioTestSvcHandleOption(pSrv, ATSTCPOPT_CONN_MODE, &Val);
    AssertRCReturn(rc, rc);

    if (   pTcpOpts->enmConnMode == ATSCONNMODE_BOTH
        || pTcpOpts->enmConnMode == ATSCONNMODE_SERVER)
    {
        Assert(pTcpOpts->uBindPort); /* Always set by the caller. */
        Val.u16 = pTcpOpts->uBindPort;
        rc = AudioTestSvcHandleOption(pSrv, ATSTCPOPT_BIND_PORT, &Val);
        AssertRCReturn(rc, rc);

        if (pTcpOpts->szBindAddr[0])
        {
            Val.psz = pTcpOpts->szBindAddr;
            rc = AudioTestSvcHandleOption(pSrv, ATSTCPOPT_BIND_ADDRESS, &Val);
            AssertRCReturn(rc, rc);
        }
        else
        {
            RTTestFailed(g_hTest, "No bind address specified!\n");
            return VERR_INVALID_PARAMETER;
        }

        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Starting server for %s at %s:%RU32 ...\n",
                     pszDesc, pTcpOpts->szBindAddr, pTcpOpts->uBindPort);
    }


    if (   pTcpOpts->enmConnMode == ATSCONNMODE_BOTH
        || pTcpOpts->enmConnMode == ATSCONNMODE_CLIENT)
    {
        Assert(pTcpOpts->uConnectPort); /* Always set by the caller. */
        Val.u16 = pTcpOpts->uConnectPort;
        rc = AudioTestSvcHandleOption(pSrv, ATSTCPOPT_CONNECT_PORT, &Val);
        AssertRCReturn(rc, rc);

        if (pTcpOpts->szConnectAddr[0])
        {
            Val.psz = pTcpOpts->szConnectAddr;
            rc = AudioTestSvcHandleOption(pSrv, ATSTCPOPT_CONNECT_ADDRESS, &Val);
            AssertRCReturn(rc, rc);
        }
        else
        {
            RTTestFailed(g_hTest, "No connect address specified!\n");
            return VERR_INVALID_PARAMETER;
        }

        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Starting server for %s by connecting as client to %s:%RU32 ...\n",
                     pszDesc, pTcpOpts->szConnectAddr, pTcpOpts->uConnectPort);
    }

    if (RT_SUCCESS(rc))
    {
        rc = AudioTestSvcStart(pSrv);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "Starting server for %s failed with %Rrc\n", pszDesc, rc);
    }

    return rc;
}

/**
 * Initializes an audio test environment.
 *
 * @param   pTstEnv             Audio test environment to initialize.
 */
void audioTestEnvInit(PAUDIOTESTENV pTstEnv)
{
    RT_BZERO(pTstEnv, sizeof(AUDIOTESTENV));

    pTstEnv->msTimeout = AUDIOTEST_TIMEOUT_DEFAULT_MS;

    audioTestIoOptsInitDefaults(&pTstEnv->IoOpts);
    audioTestToneParmsInit(&pTstEnv->ToneParms);
}

/**
 * Creates an audio test environment.
 *
 * @returns VBox status code.
 * @param   pTstEnv             Audio test environment to create.
 * @param   pDrvStack           Driver stack to use.
 */
int audioTestEnvCreate(PAUDIOTESTENV pTstEnv, PAUDIOTESTDRVSTACK pDrvStack)
{
    AssertReturn(PDMAudioPropsAreValid(&pTstEnv->IoOpts.Props), VERR_WRONG_ORDER);

    int rc = VINF_SUCCESS;

    pTstEnv->pDrvStack = pDrvStack;

    /*
     * Set sane defaults if not already set.
     */
    if (!RTStrNLen(pTstEnv->szTag, sizeof(pTstEnv->szTag)))
        rc = AudioTestGenTag(pTstEnv->szTag, sizeof(pTstEnv->szTag));

    if (!RTStrNLen(pTstEnv->szPathTemp, sizeof(pTstEnv->szPathTemp)))
    {
        int rc2 = AudioTestPathGetTemp(pTstEnv->szPathTemp, sizeof(pTstEnv->szPathTemp));
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    if (!RTStrNLen(pTstEnv->szPathOut, sizeof(pTstEnv->szPathOut)))
    {
        int rc2 = RTPathJoin(pTstEnv->szPathOut, sizeof(pTstEnv->szPathOut), pTstEnv->szPathTemp, "vkat-temp");
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    char szPathTemp[RTPATH_MAX];
    if (   RT_SUCCESS(rc)
        && (   !strlen(pTstEnv->szPathTemp)
            || !strlen(pTstEnv->szPathOut)))
        rc = RTPathTemp(szPathTemp, sizeof(szPathTemp));

    if (   RT_SUCCESS(rc)
        && !strlen(pTstEnv->szPathTemp))
        rc = RTPathJoin(pTstEnv->szPathTemp, sizeof(pTstEnv->szPathTemp), szPathTemp, "vkat-temp");

    if (RT_SUCCESS(rc))
    {
        rc = RTDirCreate(pTstEnv->szPathTemp, RTFS_UNIX_IRWXU, 0 /* fFlags */);
        if (rc == VERR_ALREADY_EXISTS)
            rc = VINF_SUCCESS;
    }

    if (   RT_SUCCESS(rc)
        && !strlen(pTstEnv->szPathOut))
        rc = RTPathJoin(pTstEnv->szPathOut, sizeof(pTstEnv->szPathOut), szPathTemp, "vkat");

    if (RT_SUCCESS(rc))
    {
        rc = RTDirCreate(pTstEnv->szPathOut, RTFS_UNIX_IRWXU, 0 /* fFlags */);
        if (rc == VERR_ALREADY_EXISTS)
            rc = VINF_SUCCESS;
    }

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Initializing environment for mode '%s'\n", pTstEnv->enmMode == AUDIOTESTMODE_HOST ? "host" : "guest");
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Using tag '%s'\n", pTstEnv->szTag);
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Output directory is '%s'\n", pTstEnv->szPathOut);
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Temp directory is '%s'\n", pTstEnv->szPathTemp);

    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Initializing test directories failed with %Rrc\n", rc);
        return rc;
    }

    /**
     * For NAT'ed VMs we use (default):
     *     - client mode (uConnectAddr / uConnectPort) on the guest.
     *     - server mode (uBindAddr / uBindPort) on the host.
     */
    if (   !pTstEnv->TcpOpts.szConnectAddr[0]
        && !pTstEnv->TcpOpts.szBindAddr[0])
            RTStrCopy(pTstEnv->TcpOpts.szBindAddr, sizeof(pTstEnv->TcpOpts.szBindAddr), "0.0.0.0");

    /*
     * Determine connection mode based on set variables.
     */
    if (   pTstEnv->TcpOpts.szBindAddr[0]
        && pTstEnv->TcpOpts.szConnectAddr[0])
    {
        pTstEnv->TcpOpts.enmConnMode = ATSCONNMODE_BOTH;
    }
    else if (pTstEnv->TcpOpts.szBindAddr[0])
        pTstEnv->TcpOpts.enmConnMode = ATSCONNMODE_SERVER;
    else /* "Reversed mode", i.e. used for NATed VMs. */
        pTstEnv->TcpOpts.enmConnMode = ATSCONNMODE_CLIENT;

    /* Set a back reference to the test environment for the callback context. */
    pTstEnv->CallbackCtx.pTstEnv = pTstEnv;

    ATSCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Callbacks.pvUser = &pTstEnv->CallbackCtx;

    if (pTstEnv->enmMode == AUDIOTESTMODE_GUEST)
    {
        Callbacks.pfnHowdy            = audioTestGstAtsHowdyCallback;
        Callbacks.pfnBye              = audioTestGstAtsByeCallback;
        Callbacks.pfnTestSetBegin     = audioTestGstAtsTestSetBeginCallback;
        Callbacks.pfnTestSetEnd       = audioTestGstAtsTestSetEndCallback;
        Callbacks.pfnTonePlay         = audioTestGstAtsTonePlayCallback;
        Callbacks.pfnToneRecord       = audioTestGstAtsToneRecordCallback;
        Callbacks.pfnTestSetSendBegin = audioTestGstAtsTestSetSendBeginCallback;
        Callbacks.pfnTestSetSendRead  = audioTestGstAtsTestSetSendReadCallback;
        Callbacks.pfnTestSetSendEnd   = audioTestGstAtsTestSetSendEndCallback;

        if (!pTstEnv->TcpOpts.uBindPort)
            pTstEnv->TcpOpts.uBindPort = ATS_TCP_DEF_BIND_PORT_GUEST;

        if (!pTstEnv->TcpOpts.uConnectPort)
            pTstEnv->TcpOpts.uConnectPort = ATS_TCP_DEF_CONNECT_PORT_GUEST;

        pTstEnv->pSrv = (PATSSERVER)RTMemAlloc(sizeof(ATSSERVER));
        AssertPtrReturn(pTstEnv->pSrv, VERR_NO_MEMORY);

        /*
         * Start the ATS (Audio Test Service) on the guest side.
         * That service then will perform playback and recording operations on the guest, triggered from the host.
         *
         * When running this in self-test mode, that service also can be run on the host if nothing else is specified.
         * Note that we have to bind to "0.0.0.0" by default so that the host can connect to it.
         */
        rc = audioTestEnvConfigureAndStartTcpServer(pTstEnv->pSrv, &Callbacks, "guest", &pTstEnv->TcpOpts);
    }
    else /* Host mode */
    {
        if (!pTstEnv->TcpOpts.uBindPort)
            pTstEnv->TcpOpts.uBindPort = ATS_TCP_DEF_BIND_PORT_HOST;

        if (!pTstEnv->TcpOpts.uConnectPort)
            pTstEnv->TcpOpts.uConnectPort = ATS_TCP_DEF_CONNECT_PORT_HOST_PORT_FWD;

        /**
         * Note: Don't set pTstEnv->TcpOpts.szTcpConnectAddr by default here, as this specifies what connection mode
         *       (client / server / both) we use on the host.
         */

        /* We need to start a server on the host so that VMs configured with NAT networking
         * can connect to it as well. */
        rc = AudioTestSvcClientCreate(&pTstEnv->u.Host.AtsClGuest);
        if (RT_SUCCESS(rc))
            rc = audioTestEnvConnectViaTcp(pTstEnv, &pTstEnv->u.Host.AtsClGuest,
                                           "host -> guest", &pTstEnv->TcpOpts);
        if (RT_SUCCESS(rc))
        {
            AUDIOTESTENVTCPOPTS ValKitTcpOpts;
            RT_ZERO(ValKitTcpOpts);

            /* We only connect as client to the Validation Kit audio driver ATS. */
            ValKitTcpOpts.enmConnMode = ATSCONNMODE_CLIENT;

            /* For now we ASSUME that the Validation Kit audio driver ATS runs on the same host as VKAT (this binary) runs on. */
            ValKitTcpOpts.uConnectPort = ATS_TCP_DEF_CONNECT_PORT_VALKIT; /** @todo Make this dynamic. */
            RTStrCopy(ValKitTcpOpts.szConnectAddr, sizeof(ValKitTcpOpts.szConnectAddr), ATS_TCP_DEF_CONNECT_HOST_ADDR_STR); /** @todo Ditto. */

            rc = AudioTestSvcClientCreate(&pTstEnv->u.Host.AtsClValKit);
            if (RT_SUCCESS(rc))
            {
                rc = audioTestEnvConnectViaTcp(pTstEnv, &pTstEnv->u.Host.AtsClValKit,
                                               "host -> valkit", &ValKitTcpOpts);
                if (RT_FAILURE(rc))
                    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Unable to connect to the Validation Kit audio driver!\n"
                                                            "There could be multiple reasons:\n\n"
                                                            "    - Wrong host being used\n"
                                                            "    - VirtualBox host version is too old\n"
                                                            "    - Audio debug mode is not enabled\n"
                                                            "    - Support for Validation Kit audio driver is not included\n"
                                                            "    - Firewall / network configuration problem\n");
            }
        }
    }

    return rc;
}

/**
 * Destroys an audio test environment.
 *
 * @param   pTstEnv             Audio test environment to destroy.
 */
void audioTestEnvDestroy(PAUDIOTESTENV pTstEnv)
{
    if (!pTstEnv)
        return;

    /* When in host mode, we need to destroy our ATS clients in order to also let
     * the ATS server(s) know we're going to quit. */
    if (pTstEnv->enmMode == AUDIOTESTMODE_HOST)
    {
        AudioTestSvcClientDestroy(&pTstEnv->u.Host.AtsClValKit);
        AudioTestSvcClientDestroy(&pTstEnv->u.Host.AtsClGuest);
    }

    if (pTstEnv->pSrv)
    {
        int rc2 = AudioTestSvcDestroy(pTstEnv->pSrv);
        AssertRC(rc2);

        RTMemFree(pTstEnv->pSrv);
        pTstEnv->pSrv = NULL;
    }

    for (unsigned i = 0; i < RT_ELEMENTS(pTstEnv->aStreams); i++)
    {
        int rc2 = audioTestStreamDestroy(pTstEnv->pDrvStack, &pTstEnv->aStreams[i]);
        if (RT_FAILURE(rc2))
            RTTestFailed(g_hTest, "Stream destruction for stream #%u failed with %Rrc\n", i, rc2);
    }

    /* Try cleaning up a bit. */
    RTDirRemove(pTstEnv->szPathTemp);
    RTDirRemove(pTstEnv->szPathOut);

    pTstEnv->pDrvStack = NULL;
}

/**
 * Closes, packs up and destroys a test environment.
 *
 * @returns VBox status code.
 * @param   pTstEnv             Test environment to handle.
 * @param   fPack               Whether to pack the test set up before destroying / wiping it.
 * @param   pszPackFile         Where to store the packed test set file on success. Can be NULL if \a fPack is \c false.
 * @param   cbPackFile          Size (in bytes) of \a pszPackFile. Can be 0 if \a fPack is \c false.
 */
int audioTestEnvPrologue(PAUDIOTESTENV pTstEnv, bool fPack, char *pszPackFile, size_t cbPackFile)
{
    /* Close the test set first. */
    AudioTestSetClose(&pTstEnv->Set);

    int rc = VINF_SUCCESS;

    if (fPack)
    {
        /* Before destroying the test environment, pack up the test set so
         * that it's ready for transmission. */
        rc = AudioTestSetPack(&pTstEnv->Set, pTstEnv->szPathOut, pszPackFile, cbPackFile);
        if (RT_SUCCESS(rc))
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Test set packed up to '%s'\n", pszPackFile);
    }

    if (!g_fDrvAudioDebug) /* Don't wipe stuff when debugging. Can be useful for introspecting data. */
        /* ignore rc */ AudioTestSetWipe(&pTstEnv->Set);

    AudioTestSetDestroy(&pTstEnv->Set);

    if (RT_FAILURE(rc))
        RTTestFailed(g_hTest, "Test set prologue failed with %Rrc\n", rc);

    return rc;
}

/**
 * Initializes an audio test parameters set.
 *
 * @param   pTstParms           Test parameters set to initialize.
 */
void audioTestParmsInit(PAUDIOTESTPARMS pTstParms)
{
    RT_ZERO(*pTstParms);
}

/**
 * Destroys an audio test parameters set.
 *
 * @param   pTstParms           Test parameters set to destroy.
 */
void audioTestParmsDestroy(PAUDIOTESTPARMS pTstParms)
{
    if (!pTstParms)
        return;

    return;
}

