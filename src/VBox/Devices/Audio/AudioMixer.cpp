/* $Id: AudioMixer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Audio mixing routines for multiplexing audio sources in device emulations.
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

/** @page pg_audio_mixer    Audio Mixer
 *
 * @section sec_audio_mixer_overview    Overview
 *
 * This mixer acts as a layer between the audio connector interface and the
 * actual device emulation, providing mechanisms for audio input sinks (sometime
 * referred to as audio sources) and audio output sinks.
 *
 * Think of this mixer as kind of a higher level interface for the audio device
 * to use in steado of PDMIAUDIOCONNECTOR, where it works with sinks rather than
 * individual PDMAUDIOSTREAM instances.
 *
 * How and which audio streams are connected to the sinks depends on how the
 * audio mixer has been set up by the device.  Though, generally, each driver
 * chain (LUN) has a mixer stream for each sink.
 *
 * An output sink can connect multiple output streams together, whereas an input
 * sink (source) does this with input streams.  Each of these mixer stream will
 * in turn point to actual PDMAUDIOSTREAM instances.
 *
 * A mixing sink employs an own audio mixing buffer in a standard format (32-bit
 * signed) with the virtual device's rate and channel configuration.  The mixer
 * streams will convert to/from this as they write and read from it.
 *
 *
 * @section sec_audio_mixer_playback    Playback
 *
 * For output sinks there can be one or more mixing stream attached.
 *
 * The backends are the consumers here and if they don't get samples when then
 * need them we'll be having cracles, distortion and/or bits of silence in the
 * actual output.  The guest runs independently at it's on speed (see @ref
 * sec_pdm_audio_timing for more details) and we're just inbetween trying to
 * shuffle the data along as best as we can.  If one or more of the backends
 * for some reason isn't able to process data at a nominal speed (as defined by
 * the others), we'll try detect this, mark it as bad and disregard it when
 * calculating how much we can write to the backends in a buffer update call.
 *
 * This is called synchronous multiplexing.
 *
 *
 * @section sec_audio_mixer_recording   Recording
 *
 * For input sinks (sources) we blend the samples of all mixing streams
 * together, however ignoring silent ones to avoid too much of a hit on the
 * volume level.  It is otherwise very similar to playback, only the direction
 * is different and we don't multicast but blend.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_AUDIO_MIXER
#include <VBox/log.h>
#include "AudioMixer.h"
#include "AudioMixBuffer.h"
#include "AudioHlp.h"

#include <VBox/vmm/pdm.h>
#include <VBox/err.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

#include <iprt/alloc.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#ifdef VBOX_WITH_DTRACE
# include "dtrace/VBoxDD.h"
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int audioMixerRemoveSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink);

static void audioMixerSinkDestroyInternal(PAUDMIXSINK pSink, PPDMDEVINS pDevIns);
static int audioMixerSinkUpdateVolume(PAUDMIXSINK pSink, PCPDMAUDIOVOLUME pVolMaster);
static int audioMixerSinkRemoveStreamInternal(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream);
static void audioMixerSinkResetInternal(PAUDMIXSINK pSink);

static int audioMixerStreamCtlInternal(PAUDMIXSTREAM pMixStream, PDMAUDIOSTREAMCMD enmCmd);
static void audioMixerStreamDestroyInternal(PAUDMIXSTREAM pStream, PPDMDEVINS pDevIns, bool fImmediate);
static int audioMixerStreamUpdateStatus(PAUDMIXSTREAM pMixStream);


/** size of output buffer for dbgAudioMixerSinkStatusToStr.   */
#define AUDIOMIXERSINK_STATUS_STR_MAX sizeof("RUNNING DRAINING DRAINED_DMA DRAINED_MIXBUF DIRTY 0x12345678")

/**
 * Converts a mixer sink status to a string.
 *
 * @returns pszDst
 * @param   fStatus     The mixer sink status.
 * @param   pszDst      The output buffer.  Must be at least
 *                      AUDIOMIXERSINK_STATUS_STR_MAX in length.
 */
static const char *dbgAudioMixerSinkStatusToStr(uint32_t fStatus, char pszDst[AUDIOMIXERSINK_STATUS_STR_MAX])
{
    if (!fStatus)
        return strcpy(pszDst, "NONE");
    static const struct
    {
        const char *pszMnemonic;
        uint32_t    cchMnemonic;
        uint32_t    fStatus;
    } s_aFlags[] =
    {
        { RT_STR_TUPLE("RUNNING "),         AUDMIXSINK_STS_RUNNING },
        { RT_STR_TUPLE("DRAINING "),        AUDMIXSINK_STS_DRAINING },
        { RT_STR_TUPLE("DRAINED_DMA "),     AUDMIXSINK_STS_DRAINED_DMA },
        { RT_STR_TUPLE("DRAINED_MIXBUF "),  AUDMIXSINK_STS_DRAINED_MIXBUF },
        { RT_STR_TUPLE("DIRTY "),           AUDMIXSINK_STS_DIRTY },
    };
    char *psz = pszDst;
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (fStatus & s_aFlags[i].fStatus)
        {
            memcpy(psz, s_aFlags[i].pszMnemonic, s_aFlags[i].cchMnemonic);
            psz += s_aFlags[i].cchMnemonic;
            fStatus &= ~s_aFlags[i].fStatus;
            if (!fStatus)
            {
                psz[-1] = '\0';
                return pszDst;
            }
        }
    RTStrPrintf(psz, AUDIOMIXERSINK_STATUS_STR_MAX - (psz - pszDst), "%#x", fStatus);
    return pszDst;
}


/**
 * Creates an audio mixer.
 *
 * @returns VBox status code.
 * @param   pszName     Name of the audio mixer.
 * @param   fFlags      Creation flags - AUDMIXER_FLAGS_XXX.
 * @param   ppMixer     Pointer which returns the created mixer object.
 */
int AudioMixerCreate(const char *pszName, uint32_t fFlags, PAUDIOMIXER *ppMixer)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    size_t const cchName = strlen(pszName);
    AssertReturn(cchName > 0 && cchName < 128, VERR_INVALID_NAME);
    AssertReturn  (!(fFlags & ~AUDMIXER_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(ppMixer, VERR_INVALID_POINTER);

    int         rc;
    PAUDIOMIXER pMixer = (PAUDIOMIXER)RTMemAllocZVar(sizeof(AUDIOMIXER) + cchName + 1);
    if (pMixer)
    {
        rc = RTCritSectInit(&pMixer->CritSect);
        if (RT_SUCCESS(rc))
        {
            pMixer->pszName = (const char *)memcpy(pMixer + 1, pszName, cchName + 1);

            pMixer->cSinks = 0;
            RTListInit(&pMixer->lstSinks);

            pMixer->fFlags = fFlags;
            pMixer->uMagic = AUDIOMIXER_MAGIC;

            if (pMixer->fFlags & AUDMIXER_FLAGS_DEBUG)
                LogRel(("Audio Mixer: Debug mode enabled\n"));

            /* Set master volume to the max. */
            PDMAudioVolumeInitMax(&pMixer->VolMaster);

            LogFlowFunc(("Created mixer '%s'\n", pMixer->pszName));
            *ppMixer = pMixer;
            return VINF_SUCCESS;
        }
        RTMemFree(pMixer);
    }
    else
        rc = VERR_NO_MEMORY;
    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Destroys an audio mixer.
 *
 * @param   pMixer      Audio mixer to destroy.  NULL is ignored.
 * @param   pDevIns     The device instance the statistics are associated with.
 */
void AudioMixerDestroy(PAUDIOMIXER pMixer, PPDMDEVINS pDevIns)
{
    if (!pMixer)
        return;
    AssertPtrReturnVoid(pMixer);
    AssertReturnVoid(pMixer->uMagic == AUDIOMIXER_MAGIC);

    int rc2 = RTCritSectEnter(&pMixer->CritSect);
    AssertRCReturnVoid(rc2);
    Assert(pMixer->uMagic == AUDIOMIXER_MAGIC);

    LogFlowFunc(("Destroying %s ...\n", pMixer->pszName));
    pMixer->uMagic = AUDIOMIXER_MAGIC_DEAD;

    PAUDMIXSINK pSink, pSinkNext;
    RTListForEachSafe(&pMixer->lstSinks, pSink, pSinkNext, AUDMIXSINK, Node)
    {
        audioMixerRemoveSinkInternal(pMixer, pSink);
        audioMixerSinkDestroyInternal(pSink, pDevIns);
    }
    Assert(pMixer->cSinks == 0);

    rc2 = RTCritSectLeave(&pMixer->CritSect);
    AssertRC(rc2);

    RTCritSectDelete(&pMixer->CritSect);
    RTMemFree(pMixer);
}


/**
 * Helper function for the internal debugger to print the mixer's current
 * state, along with the attached sinks.
 *
 * @param   pMixer              Mixer to print debug output for.
 * @param   pHlp                Debug info helper to use.
 * @param   pszArgs             Optional arguments. Not being used at the moment.
 */
void AudioMixerDebug(PAUDIOMIXER pMixer, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    AssertReturnVoid(pMixer->uMagic == AUDIOMIXER_MAGIC);

    int rc = RTCritSectEnter(&pMixer->CritSect);
    AssertRCReturnVoid(rc);

    /* Determin max sink name length for pretty formatting: */
    size_t cchMaxName = strlen(pMixer->pszName);
    PAUDMIXSINK pSink;
    RTListForEach(&pMixer->lstSinks, pSink, AUDMIXSINK, Node)
    {
        size_t const cchMixer = strlen(pSink->pszName);
        cchMaxName = RT_MAX(cchMixer, cchMaxName);
    }

    /* Do the displaying. */
    pHlp->pfnPrintf(pHlp, "[Master] %*s: fMuted=%#RTbool auChannels=%.*Rhxs\n", cchMaxName, pMixer->pszName,
                    pMixer->VolMaster.fMuted, sizeof(pMixer->VolMaster.auChannels), pMixer->VolMaster.auChannels);
    unsigned iSink = 0;
    RTListForEach(&pMixer->lstSinks, pSink, AUDMIXSINK, Node)
    {
        pHlp->pfnPrintf(pHlp, "[Sink %u] %*s: fMuted=%#RTbool auChannels=%.*Rhxs\n", iSink, cchMaxName, pSink->pszName,
                        pSink->Volume.fMuted, sizeof(pSink->Volume.auChannels), pSink->Volume.auChannels);
        ++iSink;
    }

    RTCritSectLeave(&pMixer->CritSect);
}


/**
 * Sets the mixer's master volume.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to set master volume for.
 * @param   pVol                Volume to set.
 */
int AudioMixerSetMasterVolume(PAUDIOMIXER pMixer, PCPDMAUDIOVOLUME pVol)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    AssertReturn(pMixer->uMagic == AUDIOMIXER_MAGIC, VERR_INVALID_MAGIC);
    AssertPtrReturn(pVol, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pMixer->CritSect);
    AssertRCReturn(rc, rc);

    /*
     * Make a copy.
     */
    LogFlowFunc(("[%s] fMuted=%RTbool auChannels=%.*Rhxs => fMuted=%RTbool auChannels=%.*Rhxs\n", pMixer->pszName,
                 pMixer->VolMaster.fMuted, sizeof(pMixer->VolMaster.auChannels), pMixer->VolMaster.auChannels,
                 pVol->fMuted, sizeof(pVol->auChannels), pVol->auChannels));
    memcpy(&pMixer->VolMaster, pVol, sizeof(PDMAUDIOVOLUME));

    LogRelMax(256, ("Audio Mixer: %s master volume of '%s' -- channel volumes: %.*Rhxs\n",
                    pMixer->VolMaster.fMuted ? "MUTING" : "Setting",
                    pMixer->pszName, sizeof(pMixer->VolMaster.auChannels), pMixer->VolMaster.auChannels));

    /*
     * Propagate new master volume to all sinks.
     */
    PAUDMIXSINK pSink;
    RTListForEach(&pMixer->lstSinks, pSink, AUDMIXSINK, Node)
    {
        int rc2 = audioMixerSinkUpdateVolume(pSink, &pMixer->VolMaster);
        AssertRC(rc2);
    }

    RTCritSectLeave(&pMixer->CritSect);
    return rc;
}


/**
 * Removes an audio sink from the given audio mixer, internal version.
 *
 * Used by AudioMixerDestroy and AudioMixerSinkDestroy.
 *
 * Caller must hold the mixer lock.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to remove sink from.
 * @param   pSink               Sink to remove.
 */
static int audioMixerRemoveSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink)
{
    LogFlowFunc(("[%s] pSink=%s, cSinks=%RU8\n", pMixer->pszName, pSink->pszName, pMixer->cSinks));
    Assert(RTCritSectIsOwner(&pMixer->CritSect));
    AssertMsgReturn(pSink->pParent == pMixer,
                    ("%s: Is not part of mixer '%s'\n", pSink->pszName, pMixer->pszName), VERR_INTERNAL_ERROR_4);

    /* Remove sink from mixer. */
    RTListNodeRemove(&pSink->Node);

    Assert(pMixer->cSinks);
    pMixer->cSinks--;

    /* Set mixer to NULL so that we know we're not part of any mixer anymore. */
    pSink->pParent = NULL;

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Mixer Sink implementation.                                                                                                   *
*********************************************************************************************************************************/

/**
 * Creates an audio sink and attaches it to the given mixer.
 *
 * @returns VBox status code.
 * @param   pMixer      Mixer to attach created sink to.
 * @param   pszName     Name of the sink to create.
 * @param   enmDir      Direction of the sink to create.
 * @param   pDevIns     The device instance to register statistics under.
 * @param   ppSink      Pointer which returns the created sink on success.
 */
int AudioMixerCreateSink(PAUDIOMIXER pMixer, const char *pszName, PDMAUDIODIR enmDir, PPDMDEVINS pDevIns, PAUDMIXSINK *ppSink)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    size_t const cchName = strlen(pszName);
    AssertReturn(cchName > 0 && cchName < 64, VERR_INVALID_NAME);
    AssertPtrNullReturn(ppSink, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pMixer->CritSect);
    AssertRCReturn(rc, rc);

    /** @todo limit the number of sinks? */

    /*
     * Allocate the data and initialize the critsect.
     */
    PAUDMIXSINK pSink = (PAUDMIXSINK)RTMemAllocZVar(sizeof(AUDMIXSINK) + cchName + 1);
    if (pSink)
    {
        rc = RTCritSectInit(&pSink->CritSect);
        if (RT_SUCCESS(rc))
        {
            /*
             * Initialize it.
             */
            pSink->uMagic   = AUDMIXSINK_MAGIC;
            pSink->pParent  = NULL;
            pSink->enmDir   = enmDir;
            pSink->pszName  = (const char *)memcpy(pSink + 1, pszName, cchName + 1);
            RTListInit(&pSink->lstStreams);

            /* Set initial volume to max. */
            PDMAudioVolumeInitMax(&pSink->Volume);

            /* Ditto for the combined volume. */
            PDMAudioVolumeInitMax(&pSink->VolumeCombined);

            /* AIO */
            AssertPtr(pDevIns);
            pSink->AIO.pDevIns     = pDevIns;
            pSink->AIO.hThread     = NIL_RTTHREAD;
            pSink->AIO.hEvent      = NIL_RTSEMEVENT;
            pSink->AIO.fStarted    = false;
            pSink->AIO.fShutdown   = false;
            pSink->AIO.cUpdateJobs = 0;

            /*
             * Add it to the mixer.
             */
            RTListAppend(&pMixer->lstSinks, &pSink->Node);
            pMixer->cSinks++;
            pSink->pParent = pMixer;

            RTCritSectLeave(&pMixer->CritSect);

            /*
             * Register stats and return.
             */
            char szPrefix[128];
            RTStrPrintf(szPrefix, sizeof(szPrefix), "MixerSink-%s/", pSink->pszName);
            PDMDevHlpSTAMRegisterF(pDevIns, &pSink->MixBuf.cFrames, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                                   "Sink mixer buffer size in frames.",         "%sMixBufSize", szPrefix);
            PDMDevHlpSTAMRegisterF(pDevIns, &pSink->MixBuf.cUsed, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                                   "Sink mixer buffer fill size in frames.",    "%sMixBufUsed", szPrefix);
            PDMDevHlpSTAMRegisterF(pDevIns, &pSink->cStreams, STAMTYPE_U8, STAMVISIBILITY_USED, STAMUNIT_NONE,
                                   "Number of streams attached to the sink.",   "%sStreams", szPrefix);

            if (ppSink)
                *ppSink = pSink;
            return VINF_SUCCESS;
        }

        RTMemFree(pSink);
    }
    else
        rc = VERR_NO_MEMORY;

    RTCritSectLeave(&pMixer->CritSect);
    if (ppSink)
        *ppSink = NULL;
    return rc;
}


/**
 * Starts playback/capturing on the mixer sink.
 *
 * @returns VBox status code.  Generally always VINF_SUCCESS unless the input
 *          is invalid.  Individual driver errors are suppressed and ignored.
 * @param   pSink       Mixer sink to control.
 */
int AudioMixerSinkStart(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);
    char szStatus[AUDIOMIXERSINK_STATUS_STR_MAX];
    LogFunc(("Starting '%s'. Old status: %s\n", pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));

    AssertReturnStmt(pSink->enmDir == PDMAUDIODIR_IN || pSink->enmDir == PDMAUDIODIR_OUT,
                     RTCritSectLeave(&pSink->CritSect), VERR_INTERNAL_ERROR_3);

    /*
     * Make sure the sink and its streams are all stopped.
     */
    if (!(pSink->fStatus & AUDMIXSINK_STS_RUNNING))
        Assert(pSink->fStatus == AUDMIXSINK_STS_NONE);
    else
    {
        LogFunc(("%s: This sink is still running!! Stop it before starting it again.\n", pSink->pszName));

        PAUDMIXSTREAM pStream;
        RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
        {
            /** @todo PDMAUDIOSTREAMCMD_STOP_NOW   */
            audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_DISABLE);
        }
        audioMixerSinkResetInternal(pSink);
    }

    /*
     * Send the command to the streams.
     */
    PAUDMIXSTREAM pStream;
    RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
    {
        audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_ENABLE);
    }

    /*
     * Update the sink status.
     */
    pSink->fStatus = AUDMIXSINK_STS_RUNNING;

    LogRel2(("Audio Mixer: Started sink '%s': %s\n", pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));

    RTCritSectLeave(&pSink->CritSect);
    return VINF_SUCCESS;
}


/**
 * Helper for AudioMixerSinkDrainAndStop that calculates the max length a drain
 * operation should take.
 *
 * @returns The drain deadline (relative to RTTimeNanoTS).
 * @param   pSink               The sink.
 * @param   cbDmaLeftToDrain    The number of bytes in the DMA buffer left to
 *                              transfer into the mixbuf.
 */
static uint64_t audioMixerSinkDrainDeadline(PAUDMIXSINK pSink, uint32_t cbDmaLeftToDrain)
{
    /*
     * Calculate the max backend buffer size in mixbuf frames.
     * (This is somewhat similar to audioMixerSinkUpdateOutputCalcFramesToRead.)
     */
    uint32_t      cFramesStreamMax = 0;
    PAUDMIXSTREAM pMixStream;
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
        /*LogFunc(("Stream '%s': %#x (%u frames)\n", pMixStream->pszName, pMixStream->fStatus, pMixStream->cFramesBackendBuffer));*/
        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_WRITE)
        {
            uint32_t cFrames = pMixStream->cFramesBackendBuffer;
            if (PDMAudioPropsHz(&pMixStream->pStream->Cfg.Props) == PDMAudioPropsHz(&pSink->MixBuf.Props))
            { /* likely */ }
            else
                cFrames = cFrames * PDMAudioPropsHz(&pSink->MixBuf.Props) / PDMAudioPropsHz(&pMixStream->pStream->Cfg.Props);
            if (cFrames > cFramesStreamMax)
            {
                Log4Func(("%s: cFramesStreamMax %u -> %u; %s\n", pSink->pszName, cFramesStreamMax, cFrames, pMixStream->pszName));
                cFramesStreamMax = cFrames;
            }
        }
    }

    /*
     * Combine that with the pending DMA and mixbuf content, then convert
     * to nanoseconds and apply a fudge factor to get a generous deadline.
     */
    uint32_t const cFramesDmaAndMixBuf = PDMAudioPropsBytesToFrames(&pSink->MixBuf.Props, cbDmaLeftToDrain)
                                       + AudioMixBufUsed(&pSink->MixBuf);
    uint64_t const cNsToDrainMax       = PDMAudioPropsFramesToNano(&pSink->MixBuf.Props, cFramesDmaAndMixBuf + cFramesStreamMax);
    uint64_t const nsDeadline          = cNsToDrainMax * 2;
    LogFlowFunc(("%s: cFramesStreamMax=%#x cFramesDmaAndMixBuf=%#x -> cNsToDrainMax=%RU64 -> %RU64\n",
                 pSink->pszName, cFramesStreamMax, cFramesDmaAndMixBuf, cNsToDrainMax, nsDeadline));
    return nsDeadline;
}


/**
 * Kicks off the draining and stopping playback/capture on the mixer sink.
 *
 * For input streams this causes an immediate stop, as draining only makes sense
 * to output stream in the VBox device context.
 *
 * @returns VBox status code.  Generally always VINF_SUCCESS unless the input
 *          is invalid.  Individual driver errors are suppressed and ignored.
 * @param   pSink       Mixer sink to control.
 * @param   cbComming   The number of bytes still left in the device's DMA
 *                      buffers that the update job has yet to transfer.  This
 *                      is ignored for input streams.
 */
int AudioMixerSinkDrainAndStop(PAUDMIXSINK pSink, uint32_t cbComming)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);
    char szStatus[AUDIOMIXERSINK_STATUS_STR_MAX];
    LogFunc(("Draining '%s' with %#x bytes left. Old status: %s\n",
             pSink->pszName, cbComming, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus) ));

    AssertReturnStmt(pSink->enmDir == PDMAUDIODIR_IN || pSink->enmDir == PDMAUDIODIR_OUT,
                     RTCritSectLeave(&pSink->CritSect), VERR_INTERNAL_ERROR_3);

    if (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
    {
        /*
         * Output streams will be drained then stopped (all by the AIO thread).
         *
         * For streams we define that they shouldn't not be written to after we start draining,
         * so we have to hold back sending the command to them till we've processed all the
         * cbComming remaining bytes in the DMA buffer.
         */
        if (pSink->enmDir == PDMAUDIODIR_OUT)
        {
            if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
            {
                Assert(!(pSink->fStatus & (AUDMIXSINK_STS_DRAINED_DMA | AUDMIXSINK_STS_DRAINED_MIXBUF)));

                /* Update the status and draining member. */
                pSink->cbDmaLeftToDrain = cbComming;
                pSink->nsDrainDeadline  = audioMixerSinkDrainDeadline(pSink, cbComming);
                if (pSink->nsDrainDeadline > 0)
                {
                    pSink->nsDrainStarted   = RTTimeNanoTS();
                    pSink->nsDrainDeadline += pSink->nsDrainStarted;
                    pSink->fStatus         |= AUDMIXSINK_STS_DRAINING;

                    /* Kick the AIO thread so it can keep pushing data till we're out of this
                       status. (The device's DMA timer won't kick it any more, so we must.) */
                    AudioMixerSinkSignalUpdateJob(pSink);
                }
                else
                {
                    LogFunc(("%s: No active streams, doing an immediate stop.\n", pSink->pszName));
                    PAUDMIXSTREAM pStream;
                    RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
                    {
                        audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_DISABLE);
                    }
                    audioMixerSinkResetInternal(pSink);
                }
            }
            else
                AssertMsgFailed(("Already draining '%s': %s\n",
                                 pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));
        }
        /*
         * Input sinks are stopped immediately.
         *
         * It's the guest giving order here and we can't force it to accept data that's
         * already in the buffer pipeline or anything.  So, there can be no draining here.
         */
        else
        {
            PAUDMIXSTREAM pStream;
            RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
            {
                audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_DISABLE);
            }
            audioMixerSinkResetInternal(pSink);
        }
    }
    else
        LogFunc(("%s: Not running\n", pSink->pszName));

    LogRel2(("Audio Mixer: Started draining sink '%s': %s\n", pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));
    RTCritSectLeave(&pSink->CritSect);
    return rc;
}


/**
 * Destroys and frees a mixer sink.
 *
 * Worker for AudioMixerSinkDestroy(), AudioMixerCreateSink() and
 * AudioMixerDestroy().
 *
 * @param   pSink       Mixer sink to destroy.
 * @param   pDevIns     The device instance statistics are registered with.
 */
static void audioMixerSinkDestroyInternal(PAUDMIXSINK pSink, PPDMDEVINS pDevIns)
{
    AssertPtrReturnVoid(pSink);

    LogFunc(("%s\n", pSink->pszName));

    /*
     * Invalidate the sink instance.
     */
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    pSink->uMagic = AUDMIXSINK_MAGIC_DEAD;

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturnVoid(rc);

    /*
     * Destroy all streams.
     */
    PAUDMIXSTREAM pStream, pStreamNext;
    RTListForEachSafe(&pSink->lstStreams, pStream, pStreamNext, AUDMIXSTREAM, Node)
    {
        audioMixerSinkRemoveStreamInternal(pSink, pStream);
        audioMixerStreamDestroyInternal(pStream, pDevIns, true /*fImmediate*/);
    }

    rc = RTCritSectLeave(&pSink->CritSect);
    AssertRCReturnVoid(rc);

    /*
     * Destroy debug file and statistics.
     */
    if (!pSink->Dbg.pFile)
    { /* likely */ }
    else
    {
        AudioHlpFileDestroy(pSink->Dbg.pFile);
        pSink->Dbg.pFile = NULL;
    }

    char szPrefix[128];
    RTStrPrintf(szPrefix, sizeof(szPrefix), "MixerSink-%s/", pSink->pszName);
    PDMDevHlpSTAMDeregisterByPrefix(pDevIns, szPrefix);

    /*
     * Shutdown the AIO thread if started:
     */
    ASMAtomicWriteBool(&pSink->AIO.fShutdown, true);
    if (pSink->AIO.hEvent != NIL_RTSEMEVENT)
    {
        int rc2 = RTSemEventSignal(pSink->AIO.hEvent);
        AssertRC(rc2);
    }
    if (pSink->AIO.hThread != NIL_RTTHREAD)
    {
        LogFlowFunc(("Waiting for AIO thread for %s...\n", pSink->pszName));
        int rc2 = RTThreadWait(pSink->AIO.hThread, RT_MS_30SEC, NULL);
        AssertRC(rc2);
        pSink->AIO.hThread = NIL_RTTHREAD;
    }
    if (pSink->AIO.hEvent != NIL_RTSEMEVENT)
    {
        int rc2 = RTSemEventDestroy(pSink->AIO.hEvent);
        AssertRC(rc2);
        pSink->AIO.hEvent = NIL_RTSEMEVENT;
    }

    /*
     * Mixing buffer, critsect and the structure itself.
     */
    AudioMixBufTerm(&pSink->MixBuf);
    RTCritSectDelete(&pSink->CritSect);
    RTMemFree(pSink);
}


/**
 * Destroys a mixer sink and removes it from the attached mixer (if any).
 *
 * @param   pSink       Mixer sink to destroy.  NULL is ignored.
 * @param   pDevIns     The device instance that statistics are registered with.
 */
void AudioMixerSinkDestroy(PAUDMIXSINK pSink, PPDMDEVINS pDevIns)
{
    if (!pSink)
        return;
    AssertReturnVoid(pSink->uMagic == AUDMIXSINK_MAGIC);

    /*
     * Serializing paranoia.
     */
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturnVoid(rc);
    RTCritSectLeave(&pSink->CritSect);

    /*
     * Unlink from parent.
     */
    PAUDIOMIXER pMixer = pSink->pParent;
    if (   RT_VALID_PTR(pMixer)
        && pMixer->uMagic == AUDIOMIXER_MAGIC)
    {
        RTCritSectEnter(&pMixer->CritSect);
        audioMixerRemoveSinkInternal(pMixer, pSink);
        RTCritSectLeave(&pMixer->CritSect);
    }
    else if (pMixer)
        AssertFailed();

    /*
     * Actually destroy it.
     */
    audioMixerSinkDestroyInternal(pSink, pDevIns);
}


/**
 * Get the number of bytes that can be read from the sink.
 *
 * @returns Number of bytes.
 * @param   pSink   The mixer sink.
 *
 * @note    Only applicable to input sinks, will assert and return zero for
 *          other sink directions.
 */
uint32_t AudioMixerSinkGetReadable(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, 0);
    AssertReturn(pSink->uMagic == AUDMIXSINK_MAGIC, 0);
    AssertMsgReturn(pSink->enmDir == PDMAUDIODIR_IN, ("%s: Can't read from a non-input sink\n", pSink->pszName), 0);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, 0);

    uint32_t cbReadable = 0;
    if (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
        cbReadable = AudioMixBufUsedBytes(&pSink->MixBuf);

    RTCritSectLeave(&pSink->CritSect);
    Log3Func(("[%s] cbReadable=%#x\n", pSink->pszName, cbReadable));
    return cbReadable;
}


/**
 * Get the number of bytes that can be written to be sink.
 *
 * @returns Number of bytes.
 * @param   pSink   The mixer sink.
 *
 * @note    Only applicable to output sinks, will assert and return zero for
 *          other sink directions.
 */
uint32_t AudioMixerSinkGetWritable(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, 0);
    AssertReturn(pSink->uMagic == AUDMIXSINK_MAGIC, 0);
    AssertMsgReturn(pSink->enmDir == PDMAUDIODIR_OUT, ("%s: Can't write to a non-output sink\n", pSink->pszName), 0);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, 0);

    uint32_t cbWritable = 0;
    if ((pSink->fStatus & (AUDMIXSINK_STS_RUNNING | AUDMIXSINK_STS_DRAINING)) == AUDMIXSINK_STS_RUNNING)
        cbWritable = AudioMixBufFreeBytes(&pSink->MixBuf);

    RTCritSectLeave(&pSink->CritSect);
    Log3Func(("[%s] cbWritable=%#x (%RU64ms)\n", pSink->pszName, cbWritable,
              PDMAudioPropsBytesToMilli(&pSink->PCMProps, cbWritable) ));
    return cbWritable;
}


/**
 * Get the sink's mixing direction.
 *
 * @returns Mixing direction.
 * @param   pSink   The mixer sink.
 */
PDMAUDIODIR AudioMixerSinkGetDir(PCAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, PDMAUDIODIR_INVALID);
    AssertReturn(pSink->uMagic == AUDMIXSINK_MAGIC, PDMAUDIODIR_INVALID);

    /* The sink direction cannot be changed after creation, so no need for locking here.  */
    return pSink->enmDir;
}


/**
 * Get the sink status.
 *
 * @returns AUDMIXSINK_STS_XXX
 * @param   pSink   The mixer sink.
 */
uint32_t AudioMixerSinkGetStatus(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, AUDMIXSINK_STS_NONE);
    AssertReturn(pSink->uMagic == AUDMIXSINK_MAGIC, AUDMIXSINK_STS_NONE);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, AUDMIXSINK_STS_NONE);

    uint32_t const fStsSink = pSink->fStatus;

    RTCritSectLeave(&pSink->CritSect);
    return fStsSink;
}


/**
 * Checks if the sink is active not.
 *
 * @note    The pending disable state also counts as active.
 *
 * @retval  true if active.
 * @retval  false if not active.
 * @param   pSink   The mixer sink.  NULL is okay (returns false).
 */
bool AudioMixerSinkIsActive(PAUDMIXSINK pSink)
{
    if (!pSink)
        return false;
    AssertPtr(pSink);
    AssertReturn(pSink->uMagic == AUDMIXSINK_MAGIC, false);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, false);

    bool const fIsActive = RT_BOOL(pSink->fStatus & AUDMIXSINK_STS_RUNNING);

    RTCritSectLeave(&pSink->CritSect);
    Log3Func(("[%s] returns %RTbool\n", pSink->pszName, fIsActive));
    return fIsActive;
}


/**
 * Resets the sink's state.
 *
 * @param   pSink       The sink to reset.
 * @note    Must own sink lock.
 */
static void audioMixerSinkResetInternal(PAUDMIXSINK pSink)
{
    Assert(RTCritSectIsOwner(&pSink->CritSect));
    LogFunc(("[%s]\n", pSink->pszName));

    /* Drop mixing buffer content. */
    AudioMixBufDrop(&pSink->MixBuf);

    /* Reset status. */
    pSink->fStatus         = AUDMIXSINK_STS_NONE;
    pSink->tsLastUpdatedMs = 0;
}


/**
 * Resets a sink. This will immediately stop all processing.
 *
 * @param pSink                 Sink to reset.
 */
void AudioMixerSinkReset(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;
    AssertReturnVoid(pSink->uMagic == AUDMIXSINK_MAGIC);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturnVoid(rc);

    LogFlowFunc(("[%s]\n", pSink->pszName));

    /*
     * Stop any stream that's enabled before resetting the state.
     */
    PAUDMIXSTREAM pStream;
    RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
    {
        if (pStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_DISABLE);
    }

    /*
     * Reset the state.
     */
    audioMixerSinkResetInternal(pSink);

    RTCritSectLeave(&pSink->CritSect);
}


/**
 * Sets the audio format of a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               The sink to set audio format for.
 * @param   pProps              The properties of the new audio format (guest side).
 * @param   cMsSchedulingHint   Scheduling hint for mixer buffer sizing.
 */
int AudioMixerSinkSetFormat(PAUDMIXSINK pSink, PCPDMAUDIOPCMPROPS pProps, uint32_t cMsSchedulingHint)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    AssertReturn(pSink->uMagic == AUDMIXSINK_MAGIC, VERR_INVALID_MAGIC);
    AssertPtrReturn(pProps, VERR_INVALID_POINTER);
    AssertReturn(AudioHlpPcmPropsAreValidAndSupported(pProps), VERR_INVALID_PARAMETER);

    /*
     * Calculate the mixer buffer size so we can force a recreation if it changes.
     *
     * This used to be fixed at 100ms, however that's usually too generous and can
     * in theory be too small.  Generally, we size the buffer at 3 DMA periods as
     * that seems reasonable.  Now, since the we don't quite trust the scheduling
     * hint we're getting, make sure we're got a minimum of 30ms buffer space, but
     * no more than 500ms.
     */
    if (cMsSchedulingHint <= 10)
        cMsSchedulingHint = 30;
    else
    {
        cMsSchedulingHint *= 3;
        if (cMsSchedulingHint > 500)
            cMsSchedulingHint = 500;
    }
    uint32_t const cBufferFrames = PDMAudioPropsMilliToFrames(pProps, cMsSchedulingHint);
     /** @todo configuration override on the buffer size? */

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    /*
     * Do nothing unless the format actually changed.
     * The buffer size must not match exactly, within +/- 2% is okay.
     */
    uint32_t cOldBufferFrames;
    if (   !PDMAudioPropsAreEqual(&pSink->PCMProps, pProps)
        || (   cBufferFrames != (cOldBufferFrames = AudioMixBufSize(&pSink->MixBuf))
            && (uint32_t)RT_ABS((int32_t)(cBufferFrames - cOldBufferFrames)) > cBufferFrames / 50) )
    {
#ifdef LOG_ENABLED
        char szTmp[PDMAUDIOPROPSTOSTRING_MAX];
#endif
        if (PDMAudioPropsHz(&pSink->PCMProps) != 0)
            LogFlowFunc(("[%s] Old format: %s; buffer: %u frames\n", pSink->pszName,
                         PDMAudioPropsToString(&pSink->PCMProps, szTmp, sizeof(szTmp)), AudioMixBufSize(&pSink->MixBuf) ));
        pSink->PCMProps = *pProps;
        LogFlowFunc(("[%s] New format: %s; buffer: %u frames\n", pSink->pszName,
                     PDMAudioPropsToString(&pSink->PCMProps, szTmp, sizeof(szTmp)), cBufferFrames ));

        /*
         * Also update the sink's mixing buffer format.
         */
        AudioMixBufTerm(&pSink->MixBuf);

        rc = AudioMixBufInit(&pSink->MixBuf, pSink->pszName, &pSink->PCMProps, cBufferFrames);
        if (RT_SUCCESS(rc))
        {
            /*
             * Input sinks must init their (mostly dummy) peek state.
             */
            if (pSink->enmDir == PDMAUDIODIR_IN)
                rc = AudioMixBufInitPeekState(&pSink->MixBuf, &pSink->In.State, &pSink->PCMProps);
            else
                rc = AudioMixBufInitWriteState(&pSink->MixBuf, &pSink->Out.State, &pSink->PCMProps);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Re-initialize the peek/write states as the frequency, channel count
                 * and other things may have changed now.
                 */
                PAUDMIXSTREAM pMixStream;
                if (pSink->enmDir == PDMAUDIODIR_IN)
                {
                    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
                    {
                        int rc2 = AudioMixBufInitWriteState(&pSink->MixBuf, &pMixStream->WriteState, &pMixStream->pStream->Cfg.Props);
                        /** @todo remember this. */
                        AssertLogRelRC(rc2);
                    }
                }
                else
                {
                    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
                    {
                        int rc2 = AudioMixBufInitPeekState(&pSink->MixBuf, &pMixStream->PeekState, &pMixStream->pStream->Cfg.Props);
                        /** @todo remember this. */
                        AssertLogRelRC(rc2);
                    }
                }

                /*
                 * Debug.
                 */
                if (!(pSink->pParent->fFlags & AUDMIXER_FLAGS_DEBUG))
                { /* likely */ }
                else
                {
                    AudioHlpFileClose(pSink->Dbg.pFile);

                    char szName[64];
                    RTStrPrintf(szName, sizeof(szName), "MixerSink-%s", pSink->pszName);
                    AudioHlpFileCreateAndOpen(&pSink->Dbg.pFile, NULL /*pszDir - use temp dir*/, szName,
                                              0 /*iInstance*/, &pSink->PCMProps);
                }
            }
            else
                LogFunc(("%s failed: %Rrc\n",
                         pSink->enmDir == PDMAUDIODIR_IN ? "AudioMixBufInitPeekState" : "AudioMixBufInitWriteState", rc));
        }
        else
            LogFunc(("AudioMixBufInit failed: %Rrc\n", rc));
    }

    RTCritSectLeave(&pSink->CritSect);
    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Updates the combined volume (sink + mixer) of a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink to update volume for (valid).
 * @param   pVolMaster  The master (mixer) volume (valid).
 */
static int audioMixerSinkUpdateVolume(PAUDMIXSINK pSink, PCPDMAUDIOVOLUME pVolMaster)
{
    AssertPtr(pSink);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertPtr(pVolMaster);
    LogFlowFunc(("[%s] Master fMuted=%RTbool auChannels=%.*Rhxs\n",
                  pSink->pszName, pVolMaster->fMuted, sizeof(pVolMaster->auChannels), pVolMaster->auChannels));

    PDMAudioVolumeCombine(&pSink->VolumeCombined, &pSink->Volume, pVolMaster);

    LogFlowFunc(("[%s] fMuted=%RTbool auChannels=%.*Rhxs -> fMuted=%RTbool auChannels=%.*Rhxs\n", pSink->pszName,
                 pSink->Volume.fMuted, sizeof(pSink->Volume.auChannels), pSink->Volume.auChannels,
                 pSink->VolumeCombined.fMuted, sizeof(pSink->VolumeCombined.auChannels), pSink->VolumeCombined.auChannels ));

    LogRelMax(256, ("Audio Mixer: %s sink '%s/%s' -- channel volumes: %.*Rhxs\n",
                    pSink->VolumeCombined.fMuted ? "MUTING" : "Setting",
                    pSink->pParent->pszName, pSink->pszName,
                    sizeof(pSink->VolumeCombined.auChannels), pSink->VolumeCombined.auChannels));

    AudioMixBufSetVolume(&pSink->MixBuf, &pSink->VolumeCombined);
    return VINF_SUCCESS;
}


/**
 * Sets the volume a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink       The sink to set volume for.
 * @param   pVol        New volume settings.
 */
int AudioMixerSinkSetVolume(PAUDMIXSINK pSink, PCPDMAUDIOVOLUME pVol)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    AssertReturn(pSink->uMagic == AUDMIXSINK_MAGIC, VERR_INVALID_MAGIC);
    AssertPtrReturn(pVol, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    memcpy(&pSink->Volume, pVol, sizeof(PDMAUDIOVOLUME));

    Assert(pSink->pParent);
    if (pSink->pParent)
        rc = audioMixerSinkUpdateVolume(pSink, &pSink->pParent->VolMaster);

    RTCritSectLeave(&pSink->CritSect);

    return rc;
}


/**
 * Helper for audioMixerSinkUpdateInput that determins now many frames it can
 * transfer from the drivers and into the sink's mixer buffer.
 *
 * This also updates the mixer stream status, which may involve stream re-inits.
 *
 * @returns Number of frames.
 * @param   pSink               The sink.
 * @param   pcReadableStreams   Where to return the number of readable streams.
 */
static uint32_t audioMixerSinkUpdateInputCalcFramesToTransfer(PAUDMIXSINK pSink, uint32_t *pcReadableStreams)
{
    uint32_t      cFramesToRead    = AudioMixBufFree(&pSink->MixBuf);
    uint32_t      cReadableStreams = 0;
    PAUDMIXSTREAM pMixStream;
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
        int rc2 = audioMixerStreamUpdateStatus(pMixStream);
        AssertRC(rc2);

        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_READ)
        {
            PPDMIAUDIOCONNECTOR const pIConnector = pMixStream->pConn;
            PPDMAUDIOSTREAM const     pStream     = pMixStream->pStream;
            pIConnector->pfnStreamIterate(pIConnector, pStream);

            uint32_t const cbReadable = pIConnector->pfnStreamGetReadable(pIConnector, pStream);
            uint32_t cFrames = PDMAudioPropsBytesToFrames(&pStream->Cfg.Props, cbReadable);
            pMixStream->cFramesLastAvail = cFrames;
            if (PDMAudioPropsHz(&pStream->Cfg.Props) == PDMAudioPropsHz(&pSink->MixBuf.Props))
            { /* likely */ }
            else
            {
                cFrames = cFrames * PDMAudioPropsHz(&pSink->MixBuf.Props) / PDMAudioPropsHz(&pStream->Cfg.Props);
                cFrames = cFrames > 2 ? cFrames - 2 : 0; /* rounding safety fudge */
            }
            if (cFramesToRead > cFrames && !pMixStream->fUnreliable)
            {
                Log4Func(("%s: cFramesToRead %u -> %u; %s (%u bytes readable)\n",
                          pSink->pszName, cFramesToRead, cFrames, pMixStream->pszName, cbReadable));
                cFramesToRead = cFrames;
            }
            cReadableStreams++;
        }
    }

    *pcReadableStreams = cReadableStreams;
    return cFramesToRead;
}


/**
 * Updates an input mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink       Mixer sink to update.
 * @param   cbDmaBuf    The number of bytes in the DMA buffer.  For detecting
 *                      underruns.  Zero if we don't know.
 * @param   cbDmaPeriod The minimum number of bytes required for reliable DMA
 *                      operation.  Zero if we don't know.
 */
static int audioMixerSinkUpdateInput(PAUDMIXSINK pSink, uint32_t cbDmaBuf, uint32_t cbDmaPeriod)
{
    PAUDMIXSTREAM pMixStream;
    Assert(!(pSink->fStatus & AUDMIXSINK_STS_DRAINED_MIXBUF)); /* (can't drain input sink) */

    /*
     * Iterate, update status and check each mixing sink stream for how much
     * we can transfer.
     *
     * We're currently using the minimum size of all streams, however this
     * isn't a smart approach as it means one disfunctional stream can block
     * working ones.  So, if we end up with zero frames and a full mixer
     * buffer we'll disregard the stream that accept the smallest amount and
     * try again.
     */
    uint32_t cReadableStreams  = 0;
    uint32_t cFramesToXfer     = audioMixerSinkUpdateInputCalcFramesToTransfer(pSink, &cReadableStreams);
    if (   cFramesToXfer != 0
        || cReadableStreams <= 1
        || cbDmaPeriod == 0 /* Insufficient info to decide. The update function will call us again, at least for HDA. */
        || cbDmaBuf + PDMAudioPropsFramesToBytes(&pSink->PCMProps, AudioMixBufUsed(&pSink->MixBuf)) >= cbDmaPeriod)
        Log3Func(("%s: cFreeFrames=%#x cFramesToXfer=%#x cReadableStreams=%#x\n", pSink->pszName,
                  AudioMixBufFree(&pSink->MixBuf), cFramesToXfer, cReadableStreams));
    else
    {
        Log3Func(("%s: MixBuf is underrunning but one or more streams only provides zero frames.  Try disregarding those...\n", pSink->pszName));
        uint32_t        cReliableStreams  = 0;
        uint32_t        cMarkedUnreliable = 0;
        PAUDMIXSTREAM   pMixStreamMin     = NULL;
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_READ)
            {
                if (!pMixStream->fUnreliable)
                {
                    if (pMixStream->cFramesLastAvail == 0)
                    {
                        cMarkedUnreliable++;
                        pMixStream->fUnreliable = true;
                        Log3Func(("%s: Marked '%s' as unreliable.\n", pSink->pszName, pMixStream->pszName));
                        pMixStreamMin = pMixStream;
                    }
                    else
                    {
                        if (!pMixStreamMin || pMixStream->cFramesLastAvail < pMixStreamMin->cFramesLastAvail)
                            pMixStreamMin = pMixStream;
                        cReliableStreams++;
                    }
                }
            }
        }

        if (cMarkedUnreliable == 0 && cReliableStreams > 1 && pMixStreamMin != NULL)
        {
            cReliableStreams--;
            cMarkedUnreliable++;
            pMixStreamMin->fUnreliable = true;
            Log3Func(("%s: Marked '%s' as unreliable (%u frames).\n",
                      pSink->pszName, pMixStreamMin->pszName, pMixStreamMin->cFramesLastAvail));
        }

        if (cMarkedUnreliable > 0)
        {
            cReadableStreams = 0;
            cFramesToXfer = audioMixerSinkUpdateInputCalcFramesToTransfer(pSink, &cReadableStreams);
        }

        Log3Func(("%s: cFreeFrames=%#x cFramesToXfer=%#x cReadableStreams=%#x cMarkedUnreliable=%#x cReliableStreams=%#x\n",
                  pSink->pszName, AudioMixBufFree(&pSink->MixBuf), cFramesToXfer,
                  cReadableStreams, cMarkedUnreliable, cReliableStreams));
    }

    if (cReadableStreams > 0)
    {
        if (cFramesToXfer > 0)
        {
/*#define ELECTRIC_INPUT_BUFFER*/ /* if buffer code is misbehaving, enable this to catch overflows. */
#ifndef ELECTRIC_INPUT_BUFFER
            union
            {
                uint8_t  ab[8192];
                uint64_t au64[8192 / sizeof(uint64_t)]; /* Use uint64_t to ensure good alignment. */
            } Buf;
            void * const   pvBuf = &Buf;
            uint32_t const cbBuf = sizeof(Buf);
#else
            uint32_t const cbBuf = 0x2000 - 16;
            void * const   pvBuf = RTMemEfAlloc(cbBuf, RTMEM_TAG, RT_SRC_POS);
#endif

            /*
             * For each of the enabled streams, read cFramesToXfer frames worth
             * of samples from them and merge that into the mixing buffer.
             */
            bool fAssign = true;
            RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
            {
                if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_READ)
                {
                    PPDMIAUDIOCONNECTOR const pIConnector = pMixStream->pConn;
                    PPDMAUDIOSTREAM const     pStream     = pMixStream->pStream;

                    /* Calculate how many bytes we should read from this stream. */
                    bool const     fResampleSrc = PDMAudioPropsHz(&pStream->Cfg.Props) != PDMAudioPropsHz(&pSink->MixBuf.Props);
                    uint32_t const cbSrcToXfer  = !fResampleSrc
                                                ? PDMAudioPropsFramesToBytes(&pStream->Cfg.Props, cFramesToXfer)
                                                : PDMAudioPropsFramesToBytes(&pStream->Cfg.Props, /** @todo check rounding errors here... */
                                                                             cFramesToXfer * PDMAudioPropsHz(&pSink->MixBuf.Props)
                                                                             / PDMAudioPropsHz(&pStream->Cfg.Props));

                    /* Do the reading. */
                    uint32_t offSrc      = 0;
                    uint32_t offDstFrame = 0;
                    do
                    {
                        /*
                         * Read a chunk from the backend.
                         */
                        uint32_t const cbSrcToRead = RT_MIN(cbBuf, cbSrcToXfer - offSrc);
                        uint32_t       cbSrcRead   = 0;
                        if (cbSrcToRead > 0)
                        {
                            int rc2 = pIConnector->pfnStreamCapture(pIConnector, pStream, pvBuf, cbSrcToRead, &cbSrcRead);
                            Log3Func(("%s: %#x L %#x => %#x bytes; rc2=%Rrc %s\n",
                                      pSink->pszName, offSrc, cbSrcToRead, cbSrcRead, rc2, pMixStream->pszName));

                            if (RT_SUCCESS(rc2))
                                AssertLogRelMsg(cbSrcRead == cbSrcToRead || pMixStream->fUnreliable,
                                                ("cbSrcRead=%#x cbSrcToRead=%#x - (sink '%s')\n",
                                                 cbSrcRead, cbSrcToRead, pSink->pszName));
                            else if (rc2 == VERR_AUDIO_STREAM_NOT_READY)
                            {
                                LogRel2(("Audio Mixer: '%s' (sink '%s'): Stream not ready - skipping.\n",
                                         pMixStream->pszName, pSink->pszName)); /* must've changed status, stop processing */
                                break;
                            }
                            else
                            {
                                Assert(rc2 != VERR_BUFFER_OVERFLOW);
                                LogRel2(("Audio Mixer: Reading from mixer stream '%s' (sink '%s') failed, rc=%Rrc\n",
                                         pMixStream->pszName, pSink->pszName, rc2));
                                break;
                            }
                            offSrc += cbSrcRead;
                        }
                        else
                            Assert(fResampleSrc); /** @todo test this case */

                        /*
                         * Assign or blend it into the mixer buffer.
                         */
                        uint32_t cFramesDstTransferred = 0;
                        if (fAssign)
                        {
                            /** @todo could complicate this by detecting silence here too and stay in
                             *        assign mode till we get a stream with non-silence... */
                            AudioMixBufWrite(&pSink->MixBuf, &pMixStream->WriteState, pvBuf, cbSrcRead,
                                             offDstFrame, cFramesToXfer - offDstFrame, &cFramesDstTransferred);
                        }
                        /* We don't need to blend silence buffers.  For simplicity, always blend
                           when we're resampling (for rounding). */
                        else if (fResampleSrc || !PDMAudioPropsIsBufferSilence(&pStream->Cfg.Props, pvBuf, cbSrcRead))
                        {
                            AudioMixBufBlend(&pSink->MixBuf, &pMixStream->WriteState, pvBuf, cbSrcRead,
                                             offDstFrame, cFramesToXfer - offDstFrame, &cFramesDstTransferred);
                        }
                        else
                        {
                            cFramesDstTransferred = PDMAudioPropsBytesToFrames(&pStream->Cfg.Props, cbSrcRead);
                            AudioMixBufBlendGap(&pSink->MixBuf, &pMixStream->WriteState, cFramesDstTransferred);
                        }
                        AssertBreak(cFramesDstTransferred > 0);

                        /* Advance. */
                        offDstFrame += cFramesDstTransferred;
                    } while (offDstFrame < cFramesToXfer);

                    /*
                     * In case the first stream is misbehaving, make sure we written the entire area.
                     */
                    if (offDstFrame >= cFramesToXfer)
                    { /* likely */ }
                    else if (fAssign)
                        AudioMixBufSilence(&pSink->MixBuf, &pMixStream->WriteState, offDstFrame, cFramesToXfer - offDstFrame);
                    else
                        AudioMixBufBlendGap(&pSink->MixBuf, &pMixStream->WriteState, cFramesToXfer - offDstFrame);
                    fAssign = false;
                }
            }

            /*
             * Commit the buffer area we've written and blended into.
             */
            AudioMixBufCommit(&pSink->MixBuf, cFramesToXfer);

#ifdef ELECTRIC_INPUT_BUFFER
            RTMemEfFree(pvBuf, RT_SRC_POS);
#endif
        }

        /*
         * Set the dirty flag for what it's worth.
         */
        pSink->fStatus |= AUDMIXSINK_STS_DIRTY;
    }
    else
    {
        /*
         * No readable stream. Clear the dirty flag if empty (pointless flag).
         */
        if (!AudioMixBufUsed(&pSink->MixBuf))
            pSink->fStatus &= ~AUDMIXSINK_STS_DIRTY;
    }

    /* Update last updated timestamp. */
    pSink->tsLastUpdatedMs = RTTimeMilliTS();

    return VINF_SUCCESS;
}


/**
 * Helper for audioMixerSinkUpdateOutput that determins now many frames it
 * can transfer from the sink's mixer buffer and to the drivers.
 *
 * This also updates the mixer stream status, which may involve stream re-inits.
 *
 * @returns Number of frames.
 * @param   pSink               The sink.
 * @param   pcWritableStreams   Where to return the number of writable streams.
 */
static uint32_t audioMixerSinkUpdateOutputCalcFramesToRead(PAUDMIXSINK pSink, uint32_t *pcWritableStreams)
{
    uint32_t      cFramesToRead    = AudioMixBufUsed(&pSink->MixBuf); /* (to read from the mixing buffer) */
    uint32_t      cWritableStreams = 0;
    PAUDMIXSTREAM pMixStream;
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
#if 0 /** @todo this conceptually makes sense, but may mess up the pending-disable logic ... */
        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            pConn->pfnStreamIterate(pConn, pStream);
#endif

        int rc2 = audioMixerStreamUpdateStatus(pMixStream);
        AssertRC(rc2);

        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_WRITE)
        {
            uint32_t const cbWritable = pMixStream->pConn->pfnStreamGetWritable(pMixStream->pConn, pMixStream->pStream);
            uint32_t cFrames = PDMAudioPropsBytesToFrames(&pMixStream->pStream->Cfg.Props, cbWritable);
            pMixStream->cFramesLastAvail = cFrames;
            if (PDMAudioPropsHz(&pMixStream->pStream->Cfg.Props) == PDMAudioPropsHz(&pSink->MixBuf.Props))
            { /* likely */ }
            else
            {
                cFrames = cFrames * PDMAudioPropsHz(&pSink->MixBuf.Props) / PDMAudioPropsHz(&pMixStream->pStream->Cfg.Props);
                cFrames = cFrames > 2 ? cFrames - 2 : 0; /* rounding safety fudge */
            }
            if (cFramesToRead > cFrames && !pMixStream->fUnreliable)
            {
                Log4Func(("%s: cFramesToRead %u -> %u; %s (%u bytes writable)\n",
                          pSink->pszName, cFramesToRead, cFrames, pMixStream->pszName, cbWritable));
                cFramesToRead = cFrames;
            }
            cWritableStreams++;
        }
    }

    *pcWritableStreams = cWritableStreams;
    return cFramesToRead;
}


/**
 * Updates an output mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Mixer sink to update.
 */
static int audioMixerSinkUpdateOutput(PAUDMIXSINK pSink)
{
    PAUDMIXSTREAM pMixStream;
    Assert(!(pSink->fStatus & AUDMIXSINK_STS_DRAINED_MIXBUF) || AudioMixBufUsed(&pSink->MixBuf) == 0);

    /*
     * Update each mixing sink stream's status and check how much we can
     * write into them.
     *
     * We're currently using the minimum size of all streams, however this
     * isn't a smart approach as it means one disfunctional stream can block
     * working ones.  So, if we end up with zero frames and a full mixer
     * buffer we'll disregard the stream that accept the smallest amount and
     * try again.
     */
    uint32_t cWritableStreams  = 0;
    uint32_t cFramesToRead     = audioMixerSinkUpdateOutputCalcFramesToRead(pSink, &cWritableStreams);
    if (   cFramesToRead != 0
        || cWritableStreams <= 1
        || AudioMixBufFree(&pSink->MixBuf) > 2)
        Log3Func(("%s: cLiveFrames=%#x cFramesToRead=%#x cWritableStreams=%#x\n", pSink->pszName,
                  AudioMixBufUsed(&pSink->MixBuf), cFramesToRead, cWritableStreams));
    else
    {
        Log3Func(("%s: MixBuf is full but one or more streams only want zero frames.  Try disregarding those...\n", pSink->pszName));
        uint32_t        cReliableStreams  = 0;
        uint32_t        cMarkedUnreliable = 0;
        PAUDMIXSTREAM   pMixStreamMin     = NULL;
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_WRITE)
            {
                if (!pMixStream->fUnreliable)
                {
                    if (pMixStream->cFramesLastAvail == 0)
                    {
                        cMarkedUnreliable++;
                        pMixStream->fUnreliable = true;
                        Log3Func(("%s: Marked '%s' as unreliable.\n", pSink->pszName, pMixStream->pszName));
                        pMixStreamMin = pMixStream;
                    }
                    else
                    {
                        if (!pMixStreamMin || pMixStream->cFramesLastAvail < pMixStreamMin->cFramesLastAvail)
                            pMixStreamMin = pMixStream;
                        cReliableStreams++;
                    }
                }
            }
        }

        if (cMarkedUnreliable == 0 && cReliableStreams > 1 && pMixStreamMin != NULL)
        {
            cReliableStreams--;
            cMarkedUnreliable++;
            pMixStreamMin->fUnreliable = true;
            Log3Func(("%s: Marked '%s' as unreliable (%u frames).\n",
                      pSink->pszName, pMixStreamMin->pszName, pMixStreamMin->cFramesLastAvail));
        }

        if (cMarkedUnreliable > 0)
        {
            cWritableStreams = 0;
            cFramesToRead = audioMixerSinkUpdateOutputCalcFramesToRead(pSink, &cWritableStreams);
        }

        Log3Func(("%s: cLiveFrames=%#x cFramesToRead=%#x cWritableStreams=%#x cMarkedUnreliable=%#x cReliableStreams=%#x\n",
                  pSink->pszName, AudioMixBufUsed(&pSink->MixBuf), cFramesToRead,
                  cWritableStreams, cMarkedUnreliable, cReliableStreams));
    }

    if (cWritableStreams > 0)
    {
        if (cFramesToRead > 0)
        {
            PAUDIOHLPFILE pFile = pSink->Dbg.pFile; /* Beacon for writing multiplexed data only once. */

            /*
             * For each of the enabled streams, convert cFramesToRead frames from
             * the mixing buffer and write that to the downstream driver.
             */
            RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
            {
                if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_WRITE)
                {
                    uint32_t offSrcFrame = 0;
                    do
                    {
                        /* Convert a chunk from the mixer buffer.  */
/*#define ELECTRIC_PEEK_BUFFER*/ /* if buffer code is misbehaving, enable this to catch overflows. */
#ifndef ELECTRIC_PEEK_BUFFER
                        union
                        {
                            uint8_t  ab[8192];
                            uint64_t au64[8192 / sizeof(uint64_t)]; /* Use uint64_t to ensure good alignment. */
                        } Buf;
                        void * const   pvBuf = &Buf;
                        uint32_t const cbBuf = sizeof(Buf);
#else
                        uint32_t const cbBuf = 0x2000 - 16;
                        void * const   pvBuf = RTMemEfAlloc(cbBuf, RTMEM_TAG, RT_SRC_POS);
#endif
                        uint32_t cbDstPeeked      = cbBuf;
                        uint32_t cSrcFramesPeeked = cFramesToRead - offSrcFrame;
                        AudioMixBufPeek(&pSink->MixBuf, offSrcFrame, cSrcFramesPeeked, &cSrcFramesPeeked,
                                        &pMixStream->PeekState, pvBuf, cbBuf, &cbDstPeeked);
                        offSrcFrame += cSrcFramesPeeked;

                        if (RT_UNLIKELY(pFile))
                        {
                            AudioHlpFileWrite(pFile, pvBuf, cbDstPeeked);
                            pFile = NULL;
                        }

                        /* Write it to the backend.  Since've checked that there is buffer
                           space available, this should always write the whole buffer unless
                           it's an unreliable stream. */
                        uint32_t cbDstWritten = 0;
                        int rc2 = pMixStream->pConn->pfnStreamPlay(pMixStream->pConn, pMixStream->pStream,
                                                                   pvBuf, cbDstPeeked, &cbDstWritten);
                        Log3Func(("%s: %#x L %#x => %#x bytes; wrote %#x rc2=%Rrc %s\n", pSink->pszName, offSrcFrame,
                                  cSrcFramesPeeked - cSrcFramesPeeked, cbDstPeeked, cbDstWritten, rc2, pMixStream->pszName));
#ifdef ELECTRIC_PEEK_BUFFER
                        RTMemEfFree(pvBuf, RT_SRC_POS);
#endif
                        if (RT_SUCCESS(rc2))
                            AssertLogRelMsg(cbDstWritten == cbDstPeeked || pMixStream->fUnreliable,
                                            ("cbDstWritten=%#x cbDstPeeked=%#x - (sink '%s')\n",
                                             cbDstWritten, cbDstPeeked, pSink->pszName));
                        else if (rc2 == VERR_AUDIO_STREAM_NOT_READY)
                        {
                            LogRel2(("Audio Mixer: '%s' (sink '%s'): Stream not ready - skipping.\n",
                                     pMixStream->pszName, pSink->pszName));
                            break; /* must've changed status, stop processing */
                        }
                        else
                        {
                            Assert(rc2 != VERR_BUFFER_OVERFLOW);
                            LogRel2(("Audio Mixer: Writing to mixer stream '%s' (sink '%s') failed, rc=%Rrc\n",
                                     pMixStream->pszName, pSink->pszName, rc2));
                            break;
                        }
                    } while (offSrcFrame < cFramesToRead);
                }
            }

            AudioMixBufAdvance(&pSink->MixBuf, cFramesToRead);
        }

        /*
         * Update the dirty flag for what it's worth.
         */
        if (AudioMixBufUsed(&pSink->MixBuf) > 0)
            pSink->fStatus |= AUDMIXSINK_STS_DIRTY;
        else
            pSink->fStatus &= ~AUDMIXSINK_STS_DIRTY;
    }
    else
    {
        /*
         * If no writable streams, just drop the mixer buffer content.
         */
        AudioMixBufDrop(&pSink->MixBuf);
        pSink->fStatus &= ~AUDMIXSINK_STS_DIRTY;
    }

    /*
     * Iterate buffers.
     */
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            pMixStream->pConn->pfnStreamIterate(pMixStream->pConn, pMixStream->pStream);
    }

    /* Update last updated timestamp. */
    uint64_t const nsNow   = RTTimeNanoTS();
    pSink->tsLastUpdatedMs = nsNow / RT_NS_1MS;

    /*
     * Deal with pending disable.
     * We reset the sink when all streams have been disabled.
     */
    if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
    { /* likely, till we get to the end */ }
    else if (nsNow <= pSink->nsDrainDeadline)
    {
        /* Have we drained the mixbuf now?  If so, update status and send drain
           command to streams.  (As mentioned elsewhere we don't want to confuse
           driver code by sending drain command while there is still data to write.) */
        Assert((pSink->fStatus & AUDMIXSINK_STS_DIRTY) == (AudioMixBufUsed(&pSink->MixBuf) > 0 ? AUDMIXSINK_STS_DIRTY : 0));
        if ((pSink->fStatus & (AUDMIXSINK_STS_DRAINED_MIXBUF | AUDMIXSINK_STS_DIRTY)) == 0)
        {
            LogFunc(("Sink '%s': Setting AUDMIXSINK_STS_DRAINED_MIXBUF and sending drain command to streams (after %RU64 ns).\n",
                     pSink->pszName, nsNow - pSink->nsDrainStarted));
            pSink->fStatus |= AUDMIXSINK_STS_DRAINED_MIXBUF;

            RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
            {
                pMixStream->pConn->pfnStreamControl(pMixStream->pConn, pMixStream->pStream, PDMAUDIOSTREAMCMD_DRAIN);
            }
        }

        /* Check if all streams has stopped, and if so we stop the sink. */
        uint32_t const  cStreams         = pSink->cStreams;
        uint32_t        cStreamsDisabled = pSink->cStreams;
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            {
                PDMAUDIOSTREAMSTATE const enmState = pMixStream->pConn->pfnStreamGetState(pMixStream->pConn, pMixStream->pStream);
                if (enmState >= PDMAUDIOSTREAMSTATE_ENABLED)
                    cStreamsDisabled--;
            }
        }

        if (cStreamsDisabled != cStreams)
            Log3Func(("Sink '%s': %u out of %u streams disabled (after %RU64 ns).\n",
                      pSink->pszName, cStreamsDisabled, cStreams, nsNow - pSink->nsDrainStarted));
        else
        {
            LogFunc(("Sink '%s': All %u streams disabled. Drain done after %RU64 ns.\n",
                     pSink->pszName, cStreamsDisabled, nsNow - pSink->nsDrainStarted));
            audioMixerSinkResetInternal(pSink); /* clears the status */
        }
    }
    else
    {
        /* Draining timed out. Just do an instant stop. */
        LogFunc(("Sink '%s': pending disable timed out after %RU64 ns!\n", pSink->pszName, nsNow - pSink->nsDrainStarted));
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            pMixStream->pConn->pfnStreamControl(pMixStream->pConn, pMixStream->pStream, PDMAUDIOSTREAMCMD_DISABLE);
        }
        audioMixerSinkResetInternal(pSink); /* clears the status */
    }

    return VINF_SUCCESS;
}

/**
 * Updates (invalidates) a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink           Mixer sink to update.
 * @param   cbDmaUsed       The DMA buffer fill for input stream, ignored for
 *                          output sinks.
 * @param   cbDmaPeriod     The DMA period in bytes for input stream, ignored
 *                          for output sinks.
 */
int AudioMixerSinkUpdate(PAUDMIXSINK pSink, uint32_t cbDmaUsed, uint32_t cbDmaPeriod)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

#ifdef LOG_ENABLED
    char szStatus[AUDIOMIXERSINK_STATUS_STR_MAX];
#endif
    Log3Func(("[%s] fStatus=%s\n", pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));

    /* Only process running sinks. */
    if (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
    {
        /* Do separate processing for input and output sinks. */
        if (pSink->enmDir == PDMAUDIODIR_OUT)
            rc = audioMixerSinkUpdateOutput(pSink);
        else if (pSink->enmDir == PDMAUDIODIR_IN)
            rc = audioMixerSinkUpdateInput(pSink, cbDmaUsed, cbDmaPeriod);
        else
            AssertFailedStmt(rc = VERR_INTERNAL_ERROR_3);
    }
    else
        rc = VINF_SUCCESS; /* disabled */

    RTCritSectLeave(&pSink->CritSect);
    return rc;
}


/**
 * @callback_method_impl{FNRTTHREAD, Audio Mixer Sink asynchronous I/O thread}
 */
static DECLCALLBACK(int) audioMixerSinkAsyncIoThread(RTTHREAD hThreadSelf, void *pvUser)
{
    PAUDMIXSINK pSink = (PAUDMIXSINK)pvUser;
    AssertPtr(pSink);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    RT_NOREF(hThreadSelf);

    /*
     * The run loop.
     */
    LogFlowFunc(("%s: Entering run loop...\n", pSink->pszName));
    while (!pSink->AIO.fShutdown)
    {
        RTMSINTERVAL cMsSleep = RT_INDEFINITE_WAIT;

        RTCritSectEnter(&pSink->CritSect);
        if (pSink->fStatus & (AUDMIXSINK_STS_RUNNING | AUDMIXSINK_STS_DRAINING))
        {
            /*
             * Before doing jobs, always update input sinks.
             */
            if (pSink->enmDir == PDMAUDIODIR_IN)
                audioMixerSinkUpdateInput(pSink, 0 /*cbDmaUsed*/, 0 /*cbDmaPeriod*/);

            /*
             * Do the device specific updating.
             */
            uintptr_t const cUpdateJobs = RT_MIN(pSink->AIO.cUpdateJobs, RT_ELEMENTS(pSink->AIO.aUpdateJobs));
            for (uintptr_t iJob = 0; iJob < cUpdateJobs; iJob++)
                pSink->AIO.aUpdateJobs[iJob].pfnUpdate(pSink->AIO.pDevIns, pSink, pSink->AIO.aUpdateJobs[iJob].pvUser);

            /*
             * Update output sinks after the updating.
             */
            if (pSink->enmDir == PDMAUDIODIR_OUT)
                audioMixerSinkUpdateOutput(pSink);

            /*
             * If we're in draining mode, we use the smallest typical interval of the
             * jobs for the next wait as we're unlikly to be woken up again by any
             * DMA timer as it has normally stopped running at this point.
             */
            if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
            { /* likely */ }
            else
            {
                /** @todo Also do some kind of timeout here and do a forced stream disable w/o
                 *        any draining if we exceed it. */
                cMsSleep = pSink->AIO.cMsMinTypicalInterval;
            }

        }
        RTCritSectLeave(&pSink->CritSect);

        /*
         * Now block till we're signalled or
         */
        if (!pSink->AIO.fShutdown)
        {
            int rc = RTSemEventWait(pSink->AIO.hEvent, cMsSleep);
            AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_TIMEOUT, ("%s: RTSemEventWait -> %Rrc\n", pSink->pszName, rc), rc);
        }
    }

    LogFlowFunc(("%s: returnining normally.\n", pSink->pszName));
    return VINF_SUCCESS;
}


/**
 * Adds an AIO update job to the sink.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_EXISTS if already registered job with same @a pvUser
 *          and @a pfnUpdate.
 *
 * @param   pSink               The mixer sink to remove the AIO job from.
 * @param   pfnUpdate           The update callback for the job.
 * @param   pvUser              The user parameter to pass to @a pfnUpdate.  This should
 *                              identify the job unique together with @a pfnUpdate.
 * @param   cMsTypicalInterval  A typical interval between jobs in milliseconds.
 *                              This is used when draining.
 */
int AudioMixerSinkAddUpdateJob(PAUDMIXSINK pSink, PFNAUDMIXSINKUPDATE pfnUpdate, void *pvUser, uint32_t cMsTypicalInterval)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    /*
     * Check that the job hasn't already been added.
     */
    uintptr_t const iEnd = pSink->AIO.cUpdateJobs;
    for (uintptr_t i = 0; i < iEnd; i++)
        AssertReturnStmt(   pvUser    != pSink->AIO.aUpdateJobs[i].pvUser
                         || pfnUpdate != pSink->AIO.aUpdateJobs[i].pfnUpdate,
                         RTCritSectLeave(&pSink->CritSect),
                         VERR_ALREADY_EXISTS);

    AssertReturnStmt(iEnd < RT_ELEMENTS(pSink->AIO.aUpdateJobs),
                     RTCritSectLeave(&pSink->CritSect),
                     VERR_ALREADY_EXISTS);

    /*
     * Create the thread if not already running or if it stopped.
     */
/** @todo move this to the sink "enable" code */
    if (pSink->AIO.hThread != NIL_RTTHREAD)
    {
        int rcThread = VINF_SUCCESS;
        rc = RTThreadWait(pSink->AIO.hThread, 0, &rcThread);
        if (RT_FAILURE_NP(rc))
        { /* likely */ }
        else
        {
            LogRel(("Audio: AIO thread for '%s' died? rcThread=%Rrc\n", pSink->pszName, rcThread));
            pSink->AIO.hThread = NIL_RTTHREAD;
        }
    }
    if (pSink->AIO.hThread == NIL_RTTHREAD)
    {
        LogFlowFunc(("%s: Starting AIO thread...\n", pSink->pszName));
        if (pSink->AIO.hEvent == NIL_RTSEMEVENT)
        {
            rc = RTSemEventCreate(&pSink->AIO.hEvent);
            AssertRCReturnStmt(rc, RTCritSectLeave(&pSink->CritSect), rc);
        }
        static uint32_t volatile s_idxThread = 0;
        uint32_t idxThread = ASMAtomicIncU32(&s_idxThread);
        rc = RTThreadCreateF(&pSink->AIO.hThread, audioMixerSinkAsyncIoThread, pSink, 0 /*cbStack*/, RTTHREADTYPE_IO,
                             RTTHREADFLAGS_WAITABLE | RTTHREADFLAGS_COM_MTA, "MixAIO-%u", idxThread);
        AssertRCReturnStmt(rc, RTCritSectLeave(&pSink->CritSect), rc);
    }

    /*
     * Finally, actually add the job.
     */
    pSink->AIO.aUpdateJobs[iEnd].pfnUpdate          = pfnUpdate;
    pSink->AIO.aUpdateJobs[iEnd].pvUser             = pvUser;
    pSink->AIO.aUpdateJobs[iEnd].cMsTypicalInterval = cMsTypicalInterval;
    pSink->AIO.cUpdateJobs = (uint8_t)(iEnd + 1);
    if (cMsTypicalInterval < pSink->AIO.cMsMinTypicalInterval)
        pSink->AIO.cMsMinTypicalInterval = cMsTypicalInterval;
    LogFlowFunc(("%s: [#%zu]: Added pfnUpdate=%p pvUser=%p typically every %u ms (min %u ms)\n",
                 pSink->pszName, iEnd, pfnUpdate, pvUser, cMsTypicalInterval, pSink->AIO.cMsMinTypicalInterval));

    RTCritSectLeave(&pSink->CritSect);
    return VINF_SUCCESS;

}


/**
 * Removes an update job previously registered via AudioMixerSinkAddUpdateJob().
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if not found.
 *
 * @param   pSink       The mixer sink to remove the AIO job from.
 * @param   pfnUpdate   The update callback of the job.
 * @param   pvUser      The user parameter identifying the job.
 */
int AudioMixerSinkRemoveUpdateJob(PAUDMIXSINK pSink, PFNAUDMIXSINKUPDATE pfnUpdate, void *pvUser)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    rc = VERR_NOT_FOUND;
    for (uintptr_t iJob = 0; iJob < pSink->AIO.cUpdateJobs; iJob++)
        if (   pvUser    == pSink->AIO.aUpdateJobs[iJob].pvUser
            && pfnUpdate == pSink->AIO.aUpdateJobs[iJob].pfnUpdate)
        {
            pSink->AIO.cUpdateJobs--;
            if (iJob != pSink->AIO.cUpdateJobs)
                memmove(&pSink->AIO.aUpdateJobs[iJob], &pSink->AIO.aUpdateJobs[iJob + 1],
                        (pSink->AIO.cUpdateJobs - iJob) * sizeof(pSink->AIO.aUpdateJobs[0]));
            LogFlowFunc(("%s: [#%zu]: Removed pfnUpdate=%p pvUser=%p => cUpdateJobs=%u\n",
                         pSink->pszName, iJob, pfnUpdate, pvUser, pSink->AIO.cUpdateJobs));
            rc = VINF_SUCCESS;
            break;
        }
    AssertRC(rc);

    /* Recalc the minimum sleep interval (do it always). */
    pSink->AIO.cMsMinTypicalInterval = RT_MS_1SEC / 2;
    for (uintptr_t iJob = 0; iJob < pSink->AIO.cUpdateJobs; iJob++)
        if (pSink->AIO.aUpdateJobs[iJob].cMsTypicalInterval < pSink->AIO.cMsMinTypicalInterval)
            pSink->AIO.cMsMinTypicalInterval = pSink->AIO.aUpdateJobs[iJob].cMsTypicalInterval;


    RTCritSectLeave(&pSink->CritSect);
    return rc;
}


/**
 * Writes data to a mixer output sink.
 *
 * @param   pSink       The sink to write data to.
 * @param   pvBuf       Buffer containing the audio data to write.
 * @param   cbBuf       How many bytes to write.
 * @param   pcbWritten  Number of bytes written.
 *
 * @todo merge with caller.
 */
static void audioMixerSinkWrite(PAUDMIXSINK pSink, const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    uint32_t cFrames   = AudioMixBufFree(&pSink->MixBuf);
    uint32_t cbToWrite = PDMAudioPropsFramesToBytes(&pSink->PCMProps, cFrames);
    cbToWrite = RT_MIN(cbToWrite, cbBuf);
    AudioMixBufWrite(&pSink->MixBuf, &pSink->Out.State, pvBuf, cbToWrite, 0 /*offDstFrame*/, cFrames, &cFrames);
    Assert(cbToWrite == PDMAudioPropsFramesToBytes(&pSink->PCMProps, cFrames));
    AudioMixBufCommit(&pSink->MixBuf, cFrames);
    *pcbWritten = cbToWrite;

    /* Update the sink's last written time stamp. */
    pSink->tsLastReadWrittenNs = RTTimeNanoTS();

    Log3Func(("[%s] cbBuf=%#x -> cbWritten=%#x\n", pSink->pszName, cbBuf, cbToWrite));
}


/**
 * Transfer data from the device's DMA buffer and into the sink.
 *
 * The caller is already holding the mixer sink's critical section, either by
 * way of being the AIO thread doing update jobs or by explicit locking calls.
 *
 * @returns The new stream offset.
 * @param   pSink       The mixer sink to transfer samples to.
 * @param   pCircBuf    The internal DMA buffer to move samples from.
 * @param   offStream   The stream current offset (logging, dtrace, return).
 * @param   idStream    Device specific audio stream identifier (logging, dtrace).
 * @param   pDbgFile    Debug file, NULL if disabled.
 */
uint64_t AudioMixerSinkTransferFromCircBuf(PAUDMIXSINK pSink, PRTCIRCBUF pCircBuf, uint64_t offStream,
                                           uint32_t idStream, PAUDIOHLPFILE pDbgFile)
{
    /*
     * Sanity.
     */
    AssertReturn(pSink, offStream);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertReturn(pCircBuf, offStream);
    Assert(RTCritSectIsOwner(&pSink->CritSect));
    Assert(pSink->enmDir == PDMAUDIODIR_OUT);
    RT_NOREF(idStream);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, offStream);

    /*
     * Figure how much that we can push down.
     */
    uint32_t const cbSinkWritable     = AudioMixerSinkGetWritable(pSink);
    uint32_t const cbCircBufReadable  = (uint32_t)RTCircBufUsed(pCircBuf);
    uint32_t       cbToTransfer       = RT_MIN(cbCircBufReadable, cbSinkWritable);
    /* Make sure that we always align the number of bytes when reading to the stream's PCM properties. */
    uint32_t const cbToTransfer2      = cbToTransfer = PDMAudioPropsFloorBytesToFrame(&pSink->PCMProps, cbToTransfer);

    Log3Func(("idStream=%u: cbSinkWritable=%#RX32 cbCircBufReadable=%#RX32 -> cbToTransfer=%#RX32 @%#RX64\n",
              idStream, cbSinkWritable, cbCircBufReadable, cbToTransfer, offStream));
    /* Note: There now can be more data in the DMA buffer than initially announced.  See @bugref{10354}. */
    AssertMsg(!(pSink->fStatus & AUDMIXSINK_STS_DRAINING) || cbCircBufReadable >= pSink->cbDmaLeftToDrain,
              ("cbCircBufReadable=%#x cbDmaLeftToDrain=%#x\n", cbCircBufReadable, pSink->cbDmaLeftToDrain));

    /*
     * Do the pushing.
     */
    while (cbToTransfer > 0)
    {
        void /*const*/ *pvSrcBuf;
        size_t          cbSrcBuf;
        RTCircBufAcquireReadBlock(pCircBuf, cbToTransfer, &pvSrcBuf, &cbSrcBuf);

        uint32_t cbWritten = 0;
        audioMixerSinkWrite(pSink, pvSrcBuf, (uint32_t)cbSrcBuf, &cbWritten);
        Assert(cbWritten <= cbSrcBuf);

        Log2Func(("idStream=%u: %#RX32/%#zx bytes read @%#RX64\n", idStream, cbWritten, cbSrcBuf, offStream));
#ifdef VBOX_WITH_DTRACE
        VBOXDD_AUDIO_MIXER_SINK_AIO_OUT(idStream, cbWritten, offStream);
#endif
        offStream += cbWritten;

        if (!pDbgFile)
        { /* likely */ }
        else
            AudioHlpFileWrite(pDbgFile, pvSrcBuf, cbSrcBuf);


        RTCircBufReleaseReadBlock(pCircBuf, cbWritten);

        /* advance */
        cbToTransfer -= cbWritten;
    }

    /*
     * Advance drain status.
     */
    if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
    { /* likely for most of the playback time ... */ }
    else if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINED_DMA))
    {
        if (cbToTransfer2 >= pSink->cbDmaLeftToDrain)
        {
            Assert(cbToTransfer2 == pSink->cbDmaLeftToDrain);
            Log3Func(("idStream=%u/'%s': Setting AUDMIXSINK_STS_DRAINED_DMA.\n", idStream, pSink->pszName));
            pSink->cbDmaLeftToDrain = 0;
            pSink->fStatus         |= AUDMIXSINK_STS_DRAINED_DMA;
        }
        else
        {
            pSink->cbDmaLeftToDrain -= cbToTransfer2;
            Log3Func(("idStream=%u/'%s': still %#x bytes left in the DMA buffer\n",
                      idStream, pSink->pszName, pSink->cbDmaLeftToDrain));
        }
    }
    else
        Assert(cbToTransfer2 == 0);

    Log3Func(("idStream=%u: cbCircBufUsed=%RX32 left\n", idStream, (uint32_t)RTCircBufUsed(pCircBuf)));

    RTCritSectLeave(&pSink->CritSect);

    LogFlowFuncLeave();
    return offStream;
}


/**
 * Transfer data to the device's DMA buffer from the sink.
 *
 * The caller is already holding the mixer sink's critical section, either by
 * way of being the AIO thread doing update jobs or by explicit locking calls.
 *
 * @returns The new stream offset.
 * @param   pSink       The mixer sink to transfer samples from.
 * @param   pCircBuf    The internal DMA buffer to move samples to.
 * @param   offStream   The stream current offset (logging, dtrace, return).
 * @param   idStream    Device specific audio stream identifier (logging, dtrace).
 * @param   pDbgFile    Debug file, NULL if disabled.
 */
uint64_t AudioMixerSinkTransferToCircBuf(PAUDMIXSINK pSink, PRTCIRCBUF pCircBuf, uint64_t offStream,
                                         uint32_t idStream, PAUDIOHLPFILE pDbgFile)
{
    /*
     * Sanity.
     */
    AssertReturn(pSink, offStream);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertReturn(pCircBuf, offStream);
    Assert(RTCritSectIsOwner(&pSink->CritSect));

    /*
     * Figure out how much we can transfer.
     */
    const uint32_t cbSinkReadable    = AudioMixerSinkGetReadable(pSink);
    const uint32_t cbCircBufWritable = (uint32_t)RTCircBufFree(pCircBuf);
    uint32_t       cbToTransfer      = RT_MIN(cbCircBufWritable, cbSinkReadable);
    uint32_t       cFramesToTransfer = PDMAudioPropsBytesToFrames(&pSink->PCMProps, cbToTransfer);
    cbToTransfer = PDMAudioPropsFramesToBytes(&pSink->PCMProps, cFramesToTransfer);

    Log3Func(("idStream=%u: cbSinkReadable=%#RX32 cbCircBufWritable=%#RX32 -> cbToTransfer=%#RX32 (%RU32 frames) @%#RX64\n",
              idStream, cbSinkReadable, cbCircBufWritable, cbToTransfer, cFramesToTransfer, offStream));
    RT_NOREF(idStream);

    /** @todo should we throttle (read less) this if we're far ahead? */

    /*
     * Copy loop.
     */
    while (cbToTransfer > 0)
    {
/** @todo We should be able to read straight into the circular buffer here
 *        as it should have a frame aligned size. */

        /* Read a chunk of data. */
        uint8_t  abBuf[4096];
        uint32_t cbRead = 0;
        uint32_t cFramesRead = 0;
        AudioMixBufPeek(&pSink->MixBuf, 0, cFramesToTransfer, &cFramesRead,
                        &pSink->In.State, abBuf, RT_MIN(cbToTransfer, sizeof(abBuf)), &cbRead);
        AssertBreak(cFramesRead > 0);
        Assert(cbRead > 0);

        cFramesToTransfer -= cFramesRead;
        AudioMixBufAdvance(&pSink->MixBuf, cFramesRead);

        /* Write it to the internal DMA buffer. */
        uint32_t off = 0;
        while (off < cbRead)
        {
            void  *pvDstBuf;
            size_t cbDstBuf;
            RTCircBufAcquireWriteBlock(pCircBuf, cbRead - off, &pvDstBuf, &cbDstBuf);

            memcpy(pvDstBuf, &abBuf[off], cbDstBuf);

#ifdef VBOX_WITH_DTRACE
            VBOXDD_AUDIO_MIXER_SINK_AIO_IN(idStream, (uint32_t)cbDstBuf, offStream);
#endif
            offStream += cbDstBuf;

            RTCircBufReleaseWriteBlock(pCircBuf, cbDstBuf);

            off += (uint32_t)cbDstBuf;
        }
        Assert(off == cbRead);

        /* Write to debug file? */
        if (RT_LIKELY(!pDbgFile))
        { /* likely */ }
        else
            AudioHlpFileWrite(pDbgFile, abBuf, cbRead);

        /* Advance. */
        Assert(cbRead <= cbToTransfer);
        cbToTransfer -= cbRead;
    }

    return offStream;
}


/**
 * Signals the AIO thread to perform updates.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink which AIO thread needs to do chores.
 */
int AudioMixerSinkSignalUpdateJob(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    return RTSemEventSignal(pSink->AIO.hEvent);
}


/**
 * Checks if the caller is the owner of the mixer sink's critical section.
 *
 * @returns \c true if the caller is the lock owner, \c false if not.
 * @param   pSink       The mixer sink to check.
 */
bool AudioMixerSinkLockIsOwner(PAUDMIXSINK pSink)
{
    return RTCritSectIsOwner(&pSink->CritSect);
}


/**
 * Locks the mixer sink for purposes of serializing with the AIO thread.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink to lock.
 */
int AudioMixerSinkLock(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    return RTCritSectEnter(&pSink->CritSect);
}


/**
 * Try to lock the mixer sink for purposes of serializing with the AIO thread.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink to lock.
 */
int AudioMixerSinkTryLock(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    return RTCritSectTryEnter(&pSink->CritSect);
}


/**
 * Unlocks the sink.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink to unlock.
 */
int AudioMixerSinkUnlock(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    return RTCritSectLeave(&pSink->CritSect);
}


/**
 * Creates an audio mixer stream.
 *
 * @returns VBox status code.
 * @param   pSink       Sink to use for creating the stream.
 * @param   pConn       Audio connector interface to use.
 * @param   pCfg        Audio stream configuration to use.  This may be modified
 *                      in some unspecified way (see
 *                      PDMIAUDIOCONNECTOR::pfnStreamCreate).
 * @param   pDevIns     The device instance to register statistics with.
 * @param   ppStream    Pointer which receives the newly created audio stream.
 */
int AudioMixerSinkCreateStream(PAUDMIXSINK pSink, PPDMIAUDIOCONNECTOR pConn, PCPDMAUDIOSTREAMCFG pCfg,
                               PPDMDEVINS pDevIns, PAUDMIXSTREAM *ppStream)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertPtrReturn(pConn, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg, VERR_INVALID_POINTER);
    AssertPtrNullReturn(ppStream, VERR_INVALID_POINTER);
    Assert(pSink->AIO.pDevIns == pDevIns); RT_NOREF(pDevIns); /* we'll probably be adding more statistics */
    AssertReturn(pCfg->enmDir == pSink->enmDir, VERR_MISMATCH);

    /*
     * Check status and get the host driver config.
     */
    if (pConn->pfnGetStatus(pConn, PDMAUDIODIR_DUPLEX) == PDMAUDIOBACKENDSTS_NOT_ATTACHED)
        return VERR_AUDIO_BACKEND_NOT_ATTACHED;

    PDMAUDIOBACKENDCFG BackendCfg;
    int rc = pConn->pfnGetConfig(pConn, &BackendCfg);
    AssertRCReturn(rc, rc);

    /*
     * Allocate the instance.
     */
    PAUDMIXSTREAM pMixStream = (PAUDMIXSTREAM)RTMemAllocZ(sizeof(AUDMIXSTREAM));
    AssertReturn(pMixStream, VERR_NO_MEMORY);

    /* Assign the backend's name to the mixer stream's name for easier identification in the (release) log. */
    pMixStream->pszName = RTStrAPrintf2("[%s] %s", pCfg->szName, BackendCfg.szName);
    pMixStream->pszStatPrefix = RTStrAPrintf2("MixerSink-%s/%s/", pSink->pszName, BackendCfg.szName);
    if (pMixStream->pszName && pMixStream->pszStatPrefix)
    {
        rc = RTCritSectInit(&pMixStream->CritSect);
        if (RT_SUCCESS(rc))
        {
            /*
             * Lock the sink so we can safely get it's properties and call
             * down into the audio driver to create that end of the stream.
             */
            rc = RTCritSectEnter(&pSink->CritSect);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                LogFlowFunc(("[%s] (enmDir=%ld, %u bits, %RU8 channels, %RU32Hz)\n", pSink->pszName, pCfg->enmDir,
                             PDMAudioPropsSampleBits(&pCfg->Props), PDMAudioPropsChannels(&pCfg->Props), pCfg->Props.uHz));

                /*
                 * Initialize the host-side configuration for the stream to be created,
                 * this is the sink format & direction with the src/dir, layout, name
                 * and device specific config copied from the guest side config (pCfg).
                 * We disregard any Backend settings here.
                 *
                 * (Note! pfnStreamCreate used to get both CfgHost and pCfg (aka pCfgGuest)
                 *        passed in, but that became unnecessary with DrvAudio stoppping
                 *        mixing.  The mixing is done here and we bridge guest & host configs.)
                 */
                AssertMsg(AudioHlpPcmPropsAreValidAndSupported(&pSink->PCMProps),
                          ("%s: Does not (yet) have a (valid and supported) format set when it must\n", pSink->pszName));

                PDMAUDIOSTREAMCFG CfgHost;
                rc = PDMAudioStrmCfgInitWithProps(&CfgHost, &pSink->PCMProps);
                AssertRC(rc); /* cannot fail */
                CfgHost.enmDir    = pSink->enmDir;
                CfgHost.enmPath   = pCfg->enmPath;
                CfgHost.Device    = pCfg->Device;
                RTStrCopy(CfgHost.szName, sizeof(CfgHost.szName), pCfg->szName);

                /*
                 * Create the stream.
                 *
                 * Output streams are not using any mixing buffers in DrvAudio.  This will
                 * become the norm after we move the input mixing here and convert DevSB16
                 * to use this mixer code too.
                 */
                PPDMAUDIOSTREAM pStream;
                rc = pConn->pfnStreamCreate(pConn, 0 /*fFlags*/, &CfgHost, &pStream);
                if (RT_SUCCESS(rc))
                {
                    pMixStream->cFramesBackendBuffer = pStream->Cfg.Backend.cFramesBufferSize;

                    /* Set up the mixing buffer conversion state. */
                    if (pSink->enmDir == PDMAUDIODIR_IN)
                        rc = AudioMixBufInitWriteState(&pSink->MixBuf, &pMixStream->WriteState, &pStream->Cfg.Props);
                    else
                        rc = AudioMixBufInitPeekState(&pSink->MixBuf, &pMixStream->PeekState, &pStream->Cfg.Props);
                    if (RT_SUCCESS(rc))
                    {
                        /* Save the audio stream pointer to this mixing stream. */
                        pMixStream->pStream = pStream;

                        /* Increase the stream's reference count to let others know
                         * we're relying on it to be around now. */
                        pConn->pfnStreamRetain(pConn, pStream);
                        pMixStream->pConn  = pConn;
                        pMixStream->uMagic = AUDMIXSTREAM_MAGIC;

                        RTCritSectLeave(&pSink->CritSect);

                        if (ppStream)
                            *ppStream = pMixStream;
                        return VINF_SUCCESS;
                    }

                    rc = pConn->pfnStreamDestroy(pConn, pStream, true /*fImmediate*/);
                }

                /*
                 * Failed.  Tear down the stream.
                 */
                int rc2 = RTCritSectLeave(&pSink->CritSect);
                AssertRC(rc2);
            }
            RTCritSectDelete(&pMixStream->CritSect);
        }
    }
    else
        rc = VERR_NO_STR_MEMORY;

    RTStrFree(pMixStream->pszStatPrefix);
    pMixStream->pszStatPrefix = NULL;
    RTStrFree(pMixStream->pszName);
    pMixStream->pszName = NULL;
    RTMemFree(pMixStream);
    return rc;
}


/**
 * Adds an audio stream to a specific audio sink.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to add audio stream to.
 * @param   pStream             Stream to add.
 */
int AudioMixerSinkAddStream(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    LogFlowFuncEnter();
    AssertPtrReturn(pSink,   VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);
    Assert(pStream->uMagic == AUDMIXSTREAM_MAGIC);
    AssertPtrReturn(pStream->pConn, VERR_AUDIO_STREAM_NOT_READY);
    AssertReturn(pStream->pSink == NULL, VERR_ALREADY_EXISTS);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    AssertLogRelMsgReturnStmt(pSink->cStreams < UINT8_MAX, ("too many streams!\n"), RTCritSectLeave(&pSink->CritSect),
                              VERR_TOO_MANY_OPEN_FILES);

    /*
     * If the sink is running and not in pending disable mode, make sure that
     * the added stream also is enabled.   Ignore any failure to enable it.
     */
    if (    (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
        && !(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
    {
        audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_ENABLE);
    }

    /* Save pointer to sink the stream is attached to. */
    pStream->pSink = pSink;

    /* Append stream to sink's list. */
    RTListAppend(&pSink->lstStreams, &pStream->Node);
    pSink->cStreams++;

    LogFlowFunc(("[%s] cStreams=%RU8, rc=%Rrc\n", pSink->pszName, pSink->cStreams, rc));
    RTCritSectLeave(&pSink->CritSect);
    return rc;
}


/**
 * Removes a mixer stream from a mixer sink, internal version.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink (valid).
 * @param   pStream     The stream to remove (valid).
 *
 * @note    Caller must own the sink lock.
 */
static int audioMixerSinkRemoveStreamInternal(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    AssertPtr(pSink);
    AssertPtr(pStream);
    AssertMsgReturn(pStream->pSink == pSink, ("Stream '%s' is not part of sink '%s'\n",
                                              pStream->pszName, pSink->pszName), VERR_NOT_FOUND);
    Assert(RTCritSectIsOwner(&pSink->CritSect));
    LogFlowFunc(("[%s] (Stream = %s), cStreams=%RU8\n", pSink->pszName, pStream->pStream->Cfg.szName, pSink->cStreams));

    /*
     * Remove stream from sink, update the count and set the pSink member to NULL.
     */
    RTListNodeRemove(&pStream->Node);

    Assert(pSink->cStreams > 0);
    pSink->cStreams--;

    pStream->pSink = NULL;

    return VINF_SUCCESS;
}


/**
 * Removes a mixer stream from a mixer sink.
 *
 * @param   pSink       The mixer sink.
 * @param   pStream     The stream to remove.
 */
void AudioMixerSinkRemoveStream(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    AssertPtrReturnVoid(pSink);
    AssertReturnVoid(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertPtrReturnVoid(pStream);
    AssertReturnVoid(pStream->uMagic == AUDMIXSTREAM_MAGIC);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturnVoid(rc);

    audioMixerSinkRemoveStreamInternal(pSink, pStream);

    RTCritSectLeave(&pSink->CritSect);
}


/**
 * Removes all streams from a given sink.
 *
 * @param   pSink       The mixer sink. NULL is ignored.
 */
void AudioMixerSinkRemoveAllStreams(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;
    AssertReturnVoid(pSink->uMagic == AUDMIXSINK_MAGIC);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturnVoid(rc);

    LogFunc(("%s\n", pSink->pszName));

    PAUDMIXSTREAM pStream, pStreamNext;
    RTListForEachSafe(&pSink->lstStreams, pStream, pStreamNext, AUDMIXSTREAM, Node)
    {
        audioMixerSinkRemoveStreamInternal(pSink, pStream);
    }
    AssertStmt(pSink->cStreams == 0, pSink->cStreams = 0);

    RTCritSectLeave(&pSink->CritSect);
}



/*********************************************************************************************************************************
 * Mixer Stream implementation.
 ********************************************************************************************************************************/

/**
 * Controls a mixer stream, internal version.
 *
 * @returns VBox status code (generally ignored).
 * @param   pMixStream          Mixer stream to control.
 * @param   enmCmd              Mixer stream command to use.
 */
static int audioMixerStreamCtlInternal(PAUDMIXSTREAM pMixStream, PDMAUDIOSTREAMCMD enmCmd)
{
    Assert(pMixStream->uMagic == AUDMIXSTREAM_MAGIC);
    AssertPtrReturn(pMixStream->pConn, VERR_AUDIO_STREAM_NOT_READY);
    AssertPtrReturn(pMixStream->pStream, VERR_AUDIO_STREAM_NOT_READY);

    int rc = pMixStream->pConn->pfnStreamControl(pMixStream->pConn, pMixStream->pStream, enmCmd);

    LogFlowFunc(("[%s] enmCmd=%d, rc=%Rrc\n", pMixStream->pszName, enmCmd, rc));

    return rc;
}

/**
 * Updates a mixer stream's internal status.
 *
 * This may perform a stream re-init if the driver requests it, in which case
 * this may take a little while longer than usual...
 *
 * @returns VBox status code.
 * @param   pMixStream          Mixer stream to to update internal status for.
 */
static int audioMixerStreamUpdateStatus(PAUDMIXSTREAM pMixStream)
{
    Assert(pMixStream->uMagic == AUDMIXSTREAM_MAGIC);

    /*
     * Reset the mixer status to start with.
     */
    pMixStream->fStatus = AUDMIXSTREAM_STATUS_NONE;

    PPDMIAUDIOCONNECTOR const pConn = pMixStream->pConn;
    if (pConn) /* Audio connector available? */
    {
        PPDMAUDIOSTREAM const pStream = pMixStream->pStream;

        /*
         * Get the stream status.
         * Do re-init if needed and fetch the status again afterwards.
         */
        PDMAUDIOSTREAMSTATE enmState = pConn->pfnStreamGetState(pConn, pStream);
        if (enmState != PDMAUDIOSTREAMSTATE_NEED_REINIT)
        { /* likely */ }
        else
        {
            LogFunc(("[%s] needs re-init...\n", pMixStream->pszName));
            int rc = pConn->pfnStreamReInit(pConn, pStream);
            enmState = pConn->pfnStreamGetState(pConn, pStream);
            LogFunc(("[%s] re-init returns %Rrc and %s.\n", pMixStream->pszName, rc, PDMAudioStreamStateGetName(enmState)));

            PAUDMIXSINK const pSink = pMixStream->pSink;
            AssertPtr(pSink);
            if (pSink->enmDir == PDMAUDIODIR_OUT)
            {
                rc = AudioMixBufInitPeekState(&pSink->MixBuf, &pMixStream->PeekState, &pStream->Cfg.Props);
                /** @todo we need to remember this, don't we? */
                AssertLogRelRCReturn(rc, VINF_SUCCESS);
            }
            else
            {
                rc = AudioMixBufInitWriteState(&pSink->MixBuf, &pMixStream->WriteState, &pStream->Cfg.Props);
                /** @todo we need to remember this, don't we? */
                AssertLogRelRCReturn(rc, VINF_SUCCESS);
            }
        }

        /*
         * Translate the status to mixer speak.
         */
        AssertMsg(enmState > PDMAUDIOSTREAMSTATE_INVALID && enmState < PDMAUDIOSTREAMSTATE_END, ("%d\n", enmState));
        switch (enmState)
        {
            case PDMAUDIOSTREAMSTATE_NOT_WORKING:
            case PDMAUDIOSTREAMSTATE_NEED_REINIT:
            case PDMAUDIOSTREAMSTATE_INACTIVE:
                pMixStream->fStatus = AUDMIXSTREAM_STATUS_NONE;
                break;
            case PDMAUDIOSTREAMSTATE_ENABLED:
                pMixStream->fStatus = AUDMIXSTREAM_STATUS_ENABLED;
                break;
            case PDMAUDIOSTREAMSTATE_ENABLED_READABLE:
                Assert(pMixStream->pSink->enmDir == PDMAUDIODIR_IN);
                pMixStream->fStatus = AUDMIXSTREAM_STATUS_ENABLED | AUDMIXSTREAM_STATUS_CAN_READ;
                break;
            case PDMAUDIOSTREAMSTATE_ENABLED_WRITABLE:
                Assert(pMixStream->pSink->enmDir == PDMAUDIODIR_OUT);
                pMixStream->fStatus = AUDMIXSTREAM_STATUS_ENABLED | AUDMIXSTREAM_STATUS_CAN_WRITE;
                break;
            /* no default */
            case PDMAUDIOSTREAMSTATE_INVALID:
            case PDMAUDIOSTREAMSTATE_END:
            case PDMAUDIOSTREAMSTATE_32BIT_HACK:
                break;
        }
    }

    LogFlowFunc(("[%s] -> 0x%x\n", pMixStream->pszName, pMixStream->fStatus));
    return VINF_SUCCESS;
}


/**
 * Destroys & frees a mixer stream, internal version.
 *
 * Worker for audioMixerSinkDestroyInternal and AudioMixerStreamDestroy.
 *
 * @param   pMixStream  Mixer stream to destroy.
 * @param   pDevIns     The device instance the statistics are registered with.
 * @param   fImmediate  How to handle still draining streams, whether to let
 *                      them complete (@c false) or destroy them immediately (@c
 *                      true).
 */
static void audioMixerStreamDestroyInternal(PAUDMIXSTREAM pMixStream, PPDMDEVINS pDevIns, bool fImmediate)
{
    AssertPtr(pMixStream);
    LogFunc(("%s\n", pMixStream->pszName));
    Assert(pMixStream->uMagic == AUDMIXSTREAM_MAGIC);

    /*
     * Invalidate it.
     */
    pMixStream->uMagic = AUDMIXSTREAM_MAGIC_DEAD;

    /*
     * Destroy the driver stream (if any).
     */
    if (pMixStream->pConn) /* Stream has a connector interface present? */
    {
        if (pMixStream->pStream)
        {
            pMixStream->pConn->pfnStreamRelease(pMixStream->pConn, pMixStream->pStream);
            pMixStream->pConn->pfnStreamDestroy(pMixStream->pConn, pMixStream->pStream, fImmediate);

            pMixStream->pStream = NULL;
        }

        pMixStream->pConn = NULL;
    }

    /*
     * Stats.  Doing it by prefix is soo much faster than individually, btw.
     */
    if (pMixStream->pszStatPrefix)
    {
        PDMDevHlpSTAMDeregisterByPrefix(pDevIns, pMixStream->pszStatPrefix);
        RTStrFree(pMixStream->pszStatPrefix);
        pMixStream->pszStatPrefix = NULL;
    }

    /*
     * Delete the critsect and free the memory.
     */
    int rc2 = RTCritSectDelete(&pMixStream->CritSect);
    AssertRC(rc2);

    RTStrFree(pMixStream->pszName);
    pMixStream->pszName = NULL;

    RTMemFree(pMixStream);
}


/**
 * Destroys a mixer stream.
 *
 * @param   pMixStream  Mixer stream to destroy.
 * @param   pDevIns     The device instance statistics are registered with.
 * @param   fImmediate  How to handle still draining streams, whether to let
 *                      them complete (@c false) or destroy them immediately (@c
 *                      true).
 */
void AudioMixerStreamDestroy(PAUDMIXSTREAM pMixStream, PPDMDEVINS pDevIns, bool fImmediate)
{
    if (!pMixStream)
        return;
    AssertReturnVoid(pMixStream->uMagic == AUDMIXSTREAM_MAGIC);
    LogFunc(("%s\n", pMixStream->pszName));

    /*
     * Serializing paranoia.
     */
    int rc = RTCritSectEnter(&pMixStream->CritSect);
    AssertRCReturnVoid(rc);
    RTCritSectLeave(&pMixStream->CritSect);

    /*
     * Unlink from sink if associated with one.
     */
    PAUDMIXSINK pSink = pMixStream->pSink;
    if (   RT_VALID_PTR(pSink)
        && pSink->uMagic == AUDMIXSINK_MAGIC)
    {
        RTCritSectEnter(&pSink->CritSect);
        audioMixerSinkRemoveStreamInternal(pMixStream->pSink, pMixStream);
        RTCritSectLeave(&pSink->CritSect);
    }
    else if (pSink)
        AssertFailed();

    /*
     * Do the actual stream destruction.
     */
    audioMixerStreamDestroyInternal(pMixStream, pDevIns, fImmediate);
    LogFlowFunc(("returns\n"));
}

