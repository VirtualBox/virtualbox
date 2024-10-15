/* $Id: DrvHostAudioAlsa.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Host audio driver - Advanced Linux Sound Architecture (ALSA).
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
 * This code is based on: alsaaudio.c
 *
 * QEMU ALSA audio driver
 *
 * Copyright (c) 2005 Vassili Karpov (malc)
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
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/uuid.h> /* For PDMIBASE_2_PDMDRV. */
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/vmm/pdmaudiohostenuminline.h>

#include "DrvHostAudioAlsaStubsMangling.h"
#include <alsa/asoundlib.h>
#include <alsa/control.h> /* For device enumeration. */
#include <alsa/version.h>
#include "DrvHostAudioAlsaStubs.h"

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Maximum number of tries to recover a broken pipe. */
#define ALSA_RECOVERY_TRIES_MAX    5


/*********************************************************************************************************************************
*   Structures                                                                                                                   *
*********************************************************************************************************************************/
/**
 * ALSA host audio specific stream data.
 */
typedef struct DRVHSTAUDALSASTREAM
{
    /** Common part. */
    PDMAUDIOBACKENDSTREAM   Core;

    /** Handle to the ALSA PCM stream. */
    snd_pcm_t              *hPCM;
    /** Internal stream offset (for debugging). */
    uint64_t                offInternal;

    /** The stream's acquired configuration. */
    PDMAUDIOSTREAMCFG       Cfg;
} DRVHSTAUDALSASTREAM;
/** Pointer to the ALSA host audio specific stream data. */
typedef DRVHSTAUDALSASTREAM *PDRVHSTAUDALSASTREAM;


/**
 * Host Alsa audio driver instance data.
 * @implements PDMIAUDIOCONNECTOR
 */
typedef struct DRVHSTAUDALSA
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS          pDrvIns;
    /** Pointer to host audio interface. */
    PDMIHOSTAUDIO       IHostAudio;
    /** Error count for not flooding the release log.
     *  UINT32_MAX for unlimited logging. */
    uint32_t            cLogErrors;

    /** Critical section protecting the default device strings. */
    RTCRITSECT          CritSect;
    /** Default input device name.   */
    char                szInputDev[256];
    /** Default output device name. */
    char                szOutputDev[256];
    /** Upwards notification interface. */
    PPDMIHOSTAUDIOPORT  pIHostAudioPort;
} DRVHSTAUDALSA;
/** Pointer to the instance data of an ALSA host audio driver. */
typedef DRVHSTAUDALSA *PDRVHSTAUDALSA;



/**
 * Closes an ALSA stream
 *
 * @returns VBox status code.
 * @param   phPCM   Pointer to the ALSA stream handle to close.  Will be set to
 *                  NULL.
 */
static int drvHstAudAlsaStreamClose(snd_pcm_t **phPCM)
{
    if (!phPCM || !*phPCM)
        return VINF_SUCCESS;

    LogRelFlowFuncEnter();

    int rc;
    int rc2 = snd_pcm_close(*phPCM);
    if (rc2 == 0)
    {
        *phPCM = NULL;
        rc = VINF_SUCCESS;
    }
    else
    {
        rc = RTErrConvertFromErrno(-rc2);
        LogRel(("ALSA: Closing PCM descriptor failed: %s (%d, %Rrc)\n", snd_strerror(rc2), rc2, rc));
    }

    LogRelFlowFuncLeaveRC(rc);
    return rc;
}


#ifdef DEBUG
static void drvHstAudAlsaDbgErrorHandler(const char *file, int line, const char *function,
                                         int err, const char *fmt, ...)
{
    /** @todo Implement me! */
    RT_NOREF(file, line, function, err, fmt);
}
#endif


/**
 * Tries to recover an ALSA stream.
 *
 * @returns VBox status code.
 * @param   hPCM               ALSA stream handle.
 */
static int drvHstAudAlsaStreamRecover(snd_pcm_t *hPCM)
{
    AssertPtrReturn(hPCM, VERR_INVALID_POINTER);

    int rc = snd_pcm_prepare(hPCM);
    if (rc >= 0)
    {
        LogFlowFunc(("Successfully recovered %p.\n", hPCM));
        return VINF_SUCCESS;
    }
    LogFunc(("Failed to recover stream %p: %s (%d)\n", hPCM, snd_strerror(rc), rc));
    return RTErrConvertFromErrno(-rc);
}


/**
 * Resumes an ALSA stream.
 *
 * Used by drvHstAudAlsaHA_StreamPlay() and drvHstAudAlsaHA_StreamCapture().
 *
 * @returns VBox status code.
 * @param   hPCM               ALSA stream to resume.
 */
static int drvHstAudAlsaStreamResume(snd_pcm_t *hPCM)
{
    AssertPtrReturn(hPCM, VERR_INVALID_POINTER);

    int rc = snd_pcm_resume(hPCM);
    if (rc >= 0)
    {
        LogFlowFunc(("Successfuly resumed %p.\n", hPCM));
        return VINF_SUCCESS;
    }
    LogFunc(("Failed to resume stream %p: %s (%d)\n", hPCM, snd_strerror(rc), rc));
    return RTErrConvertFromErrno(-rc);
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);

    /*
     * Fill in the config structure.
     */
    RTStrCopy(pBackendCfg->szName, sizeof(pBackendCfg->szName), "ALSA");
    pBackendCfg->cbStream       = sizeof(DRVHSTAUDALSASTREAM);
    pBackendCfg->fFlags         = 0;
    /* ALSA allows exactly one input and one output used at a time for the selected device(s). */
    pBackendCfg->cMaxStreamsIn  = 1;
    pBackendCfg->cMaxStreamsOut = 1;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetDevices}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_GetDevices(PPDMIHOSTAUDIO pInterface, PPDMAUDIOHOSTENUM pDeviceEnum)
{
    RT_NOREF(pInterface);
    PDMAudioHostEnumInit(pDeviceEnum);

    char **papszHints = NULL;
    int rc = snd_device_name_hint(-1 /* All cards */, "pcm", (void***)&papszHints);
    if (rc == 0)
    {
        rc = VINF_SUCCESS;
        for (size_t iHint = 0; papszHints[iHint] != NULL && RT_SUCCESS(rc); iHint++)
        {
            /*
             * Retrieve the available info:
             */
            const char * const pszHint = papszHints[iHint];
            char * const pszDev     = snd_device_name_get_hint(pszHint, "NAME");
            char * const pszInOutId = snd_device_name_get_hint(pszHint, "IOID");
            char * const pszDesc    = snd_device_name_get_hint(pszHint, "DESC");

            if (pszDev && RTStrICmpAscii(pszDev, "null") != 0)
            {
                /* Detect and log presence of pulse audio plugin. */
                if (RTStrIStr("pulse", pszDev) != NULL)
                    LogRel(("ALSA: The ALSAAudio plugin for pulse audio is being used (%s).\n", pszDev));

                /*
                 * Add an entry to the enumeration result.
                 * We engage in some trickery here to deal with device names that
                 * are more than 63 characters long.
                 */
                size_t const     cbId   = pszDev ? strlen(pszDev) + 1 : 1;
                size_t const     cbName = pszDesc ? strlen(pszDesc) + 2 + 1 : cbId;
                PPDMAUDIOHOSTDEV pDev   = PDMAudioHostDevAlloc(sizeof(*pDev), cbName, cbId);
                if (pDev)
                {
                    RTStrCopy(pDev->pszId, cbId, pszDev);
                    if (pDev->pszId)
                    {
                        pDev->fFlags  = PDMAUDIOHOSTDEV_F_NONE;
                        pDev->enmType = PDMAUDIODEVICETYPE_UNKNOWN;

                        if (pszInOutId == NULL)
                        {
                            pDev->enmUsage           = PDMAUDIODIR_DUPLEX;
                            pDev->cMaxInputChannels  = 2;
                            pDev->cMaxOutputChannels = 2;
                        }
                        else if (RTStrICmpAscii(pszInOutId, "Input") == 0)
                        {
                            pDev->enmUsage           = PDMAUDIODIR_IN;
                            pDev->cMaxInputChannels  = 2;
                            pDev->cMaxOutputChannels = 0;
                        }
                        else
                        {
                            AssertMsg(RTStrICmpAscii(pszInOutId, "Output") == 0, ("%s (%s)\n", pszInOutId, pszHint));
                            pDev->enmUsage           = PDMAUDIODIR_OUT;
                            pDev->cMaxInputChannels  = 0;
                            pDev->cMaxOutputChannels = 2;
                        }

                        if (pszDesc && *pszDesc)
                        {
                            char *pszDesc2 = strchr(pszDesc, '\n');
                            if (!pszDesc2)
                                RTStrCopy(pDev->pszName, cbName, pszDesc);
                            else
                            {
                                *pszDesc2++ = '\0';
                                char *psz;
                                while ((psz = strchr(pszDesc2, '\n')) != NULL)
                                    *psz = ' ';
                                RTStrPrintf(pDev->pszName, cbName, "%s (%s)", pszDesc2, pszDesc);
                            }
                        }
                        else
                            RTStrCopy(pDev->pszName, cbName, pszDev);

                        PDMAudioHostEnumAppend(pDeviceEnum, pDev);

                        LogRel2(("ALSA: Device #%u: '%s' enmDir=%s: %s\n", iHint, pszDev,
                                 PDMAudioDirGetName(pDev->enmUsage), pszDesc));
                    }
                    else
                    {
                        PDMAudioHostDevFree(pDev);
                        rc = VERR_NO_STR_MEMORY;
                    }
                }
                else
                    rc = VERR_NO_MEMORY;
            }

            /*
             * Clean up.
             */
            if (pszInOutId)
                free(pszInOutId);
            if (pszDesc)
                free(pszDesc);
            if (pszDev)
                free(pszDev);
        }

        snd_device_name_free_hint((void **)papszHints);

        if (RT_FAILURE(rc))
        {
            PDMAudioHostEnumDelete(pDeviceEnum);
            PDMAudioHostEnumInit(pDeviceEnum);
        }
    }
    else
    {
        int rc2 = RTErrConvertFromErrno(-rc);
        LogRel2(("ALSA: Error enumerating PCM devices: %Rrc (%d)\n", rc2, rc));
        rc = rc2;
    }
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnSetDevice}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_SetDevice(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir, const char *pszId)
{
    PDRVHSTAUDALSA pThis = RT_FROM_MEMBER(pInterface, DRVHSTAUDALSA, IHostAudio);

    /*
     * Validate and normalize input.
     */
    AssertReturn(enmDir == PDMAUDIODIR_IN || enmDir == PDMAUDIODIR_OUT || enmDir == PDMAUDIODIR_DUPLEX, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszId, VERR_INVALID_POINTER);
    if (!pszId || !*pszId)
        pszId = "default";
    else
    {
        size_t cch = strlen(pszId);
        AssertReturn(cch < sizeof(pThis->szInputDev), VERR_INVALID_NAME);
    }
    LogFunc(("enmDir=%d pszId=%s\n", enmDir, pszId));

    /*
     * Update input.
     */
    if (enmDir == PDMAUDIODIR_IN || enmDir == PDMAUDIODIR_DUPLEX)
    {
        int rc = RTCritSectEnter(&pThis->CritSect);
        AssertRCReturn(rc, rc);
        if (strcmp(pThis->szInputDev, pszId) == 0)
            RTCritSectLeave(&pThis->CritSect);
        else
        {
            LogRel(("ALSA: Changing input device: '%s' -> '%s'\n", pThis->szInputDev, pszId));
            RTStrCopy(pThis->szInputDev, sizeof(pThis->szInputDev), pszId);
            PPDMIHOSTAUDIOPORT pIHostAudioPort = pThis->pIHostAudioPort;
            RTCritSectLeave(&pThis->CritSect);
            if (pIHostAudioPort)
            {
                LogFlowFunc(("Notifying parent driver about input device change...\n"));
                pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, PDMAUDIODIR_IN, NULL /*pvUser*/);
            }
        }
    }

    /*
     * Update output.
     */
    if (enmDir == PDMAUDIODIR_OUT || enmDir == PDMAUDIODIR_DUPLEX)
    {
        int rc = RTCritSectEnter(&pThis->CritSect);
        AssertRCReturn(rc, rc);
        if (strcmp(pThis->szOutputDev, pszId) == 0)
            RTCritSectLeave(&pThis->CritSect);
        else
        {
            LogRel(("ALSA: Changing output device: '%s' -> '%s'\n", pThis->szOutputDev, pszId));
            RTStrCopy(pThis->szOutputDev, sizeof(pThis->szOutputDev), pszId);
            PPDMIHOSTAUDIOPORT pIHostAudioPort = pThis->pIHostAudioPort;
            RTCritSectLeave(&pThis->CritSect);
            if (pIHostAudioPort)
            {
                LogFlowFunc(("Notifying parent driver about output device change...\n"));
                pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, PDMAUDIODIR_OUT, NULL /*pvUser*/);
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvHstAudAlsaHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(enmDir);
    AssertPtrReturn(pInterface, PDMAUDIOBACKENDSTS_UNKNOWN);

    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * Converts internal audio PCM properties to an ALSA PCM format.
 *
 * @returns Converted ALSA PCM format.
 * @param   pProps              Internal audio PCM configuration to convert.
 */
static snd_pcm_format_t alsaAudioPropsToALSA(PCPDMAUDIOPCMPROPS pProps)
{
    switch (PDMAudioPropsSampleSize(pProps))
    {
        case 1:
            return pProps->fSigned ? SND_PCM_FORMAT_S8 : SND_PCM_FORMAT_U8;

        case 2:
            if (PDMAudioPropsIsLittleEndian(pProps))
                return pProps->fSigned ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_U16_LE;
            return pProps->fSigned ? SND_PCM_FORMAT_S16_BE : SND_PCM_FORMAT_U16_BE;

        case 4:
            if (PDMAudioPropsIsLittleEndian(pProps))
                return pProps->fSigned ? SND_PCM_FORMAT_S32_LE : SND_PCM_FORMAT_U32_LE;
            return pProps->fSigned ? SND_PCM_FORMAT_S32_BE : SND_PCM_FORMAT_U32_BE;

        default:
            AssertLogRelMsgFailed(("%RU8 bytes not supported\n", PDMAudioPropsSampleSize(pProps)));
            return SND_PCM_FORMAT_UNKNOWN;
    }
}


/**
 * Sets the software parameters of an ALSA stream.
 *
 * @returns 0 on success, negative errno on failure.
 * @param   hPCM        ALSA stream to set software parameters for.
 * @param   pCfgReq     Requested stream configuration (PDM).
 * @param   pCfgAcq     The actual stream configuration (PDM). Updated as
 *                      needed.
 */
static int alsaStreamSetSWParams(snd_pcm_t *hPCM, PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    if (pCfgReq->enmDir == PDMAUDIODIR_IN) /* For input streams there's nothing to do in here right now. */
        return 0;

    snd_pcm_sw_params_t *pSWParms = NULL;
    snd_pcm_sw_params_alloca(&pSWParms);
    AssertReturn(pSWParms, -ENOMEM);

    int err = snd_pcm_sw_params_current(hPCM, pSWParms);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to get current software parameters: %s\n", snd_strerror(err)), err);

    /* Under normal circumstance, we don't need to set a playback threshold
       because DrvAudio will do the pre-buffering and hand us everything in
       one continuous chunk when we should start playing.  But since it is
       configurable, we'll set a reasonable minimum of two DMA periods or
       max 50 milliseconds (the pAlsaCfgReq->threshold value).

       Of course we also have to make sure the threshold is below the buffer
       size, or ALSA will never start playing. */
    unsigned long const cFramesMax       = PDMAudioPropsMilliToFrames(&pCfgAcq->Props, 50);
    unsigned long       cFramesThreshold = RT_MIN(pCfgAcq->Backend.cFramesPeriod * 2, cFramesMax);
    if (cFramesThreshold >= pCfgAcq->Backend.cFramesBufferSize - pCfgAcq->Backend.cFramesBufferSize / 16)
        cFramesThreshold  = pCfgAcq->Backend.cFramesBufferSize - pCfgAcq->Backend.cFramesBufferSize / 16;

    err = snd_pcm_sw_params_set_start_threshold(hPCM, pSWParms, cFramesThreshold);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set software threshold to %lu: %s\n", cFramesThreshold, snd_strerror(err)), err);

    err = snd_pcm_sw_params_set_avail_min(hPCM, pSWParms, pCfgReq->Backend.cFramesPeriod);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set available minimum to %u: %s\n",
                                     pCfgReq->Backend.cFramesPeriod, snd_strerror(err)), err);

    /* Commit the software parameters: */
    err = snd_pcm_sw_params(hPCM, pSWParms);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set new software parameters: %s\n", snd_strerror(err)), err);

    /* Get the actual parameters: */
    snd_pcm_uframes_t cFramesThresholdActual = cFramesThreshold;
    err = snd_pcm_sw_params_get_start_threshold(pSWParms, &cFramesThresholdActual);
    AssertLogRelMsgStmt(err >= 0, ("ALSA: Failed to get start threshold: %s\n", snd_strerror(err)),
                        cFramesThresholdActual = cFramesThreshold);

    LogRel2(("ALSA: SW params: %lu frames threshold, %u frames avail minimum\n",
             cFramesThresholdActual, pCfgAcq->Backend.cFramesPeriod));
    return 0;
}


/**
 * Maps a PDM channel ID to an ASLA channel map position.
 */
static unsigned int drvHstAudAlsaPdmChToAlsa(PDMAUDIOCHANNELID enmId, uint8_t cChannels)
{
    switch (enmId)
    {
        case PDMAUDIOCHANNELID_UNKNOWN:                 return SND_CHMAP_UNKNOWN;
        case PDMAUDIOCHANNELID_UNUSED_ZERO:             return SND_CHMAP_NA;
        case PDMAUDIOCHANNELID_UNUSED_SILENCE:          return SND_CHMAP_NA;

        case PDMAUDIOCHANNELID_FRONT_LEFT:              return SND_CHMAP_FL;
        case PDMAUDIOCHANNELID_FRONT_RIGHT:             return SND_CHMAP_FR;
        case PDMAUDIOCHANNELID_FRONT_CENTER:            return cChannels == 1 ? SND_CHMAP_MONO : SND_CHMAP_FC;
        case PDMAUDIOCHANNELID_LFE:                     return SND_CHMAP_LFE;
        case PDMAUDIOCHANNELID_REAR_LEFT:               return SND_CHMAP_RL;
        case PDMAUDIOCHANNELID_REAR_RIGHT:              return SND_CHMAP_RR;
        case PDMAUDIOCHANNELID_FRONT_LEFT_OF_CENTER:    return SND_CHMAP_FLC;
        case PDMAUDIOCHANNELID_FRONT_RIGHT_OF_CENTER:   return SND_CHMAP_FRC;
        case PDMAUDIOCHANNELID_REAR_CENTER:             return SND_CHMAP_RC;
        case PDMAUDIOCHANNELID_SIDE_LEFT:               return SND_CHMAP_SL;
        case PDMAUDIOCHANNELID_SIDE_RIGHT:              return SND_CHMAP_SR;
        case PDMAUDIOCHANNELID_TOP_CENTER:              return SND_CHMAP_TC;
        case PDMAUDIOCHANNELID_FRONT_LEFT_HEIGHT:       return SND_CHMAP_TFL;
        case PDMAUDIOCHANNELID_FRONT_CENTER_HEIGHT:     return SND_CHMAP_TFC;
        case PDMAUDIOCHANNELID_FRONT_RIGHT_HEIGHT:      return SND_CHMAP_TFR;
        case PDMAUDIOCHANNELID_REAR_LEFT_HEIGHT:        return SND_CHMAP_TRL;
        case PDMAUDIOCHANNELID_REAR_CENTER_HEIGHT:      return SND_CHMAP_TRC;
        case PDMAUDIOCHANNELID_REAR_RIGHT_HEIGHT:       return SND_CHMAP_TRR;

        case PDMAUDIOCHANNELID_INVALID:
        case PDMAUDIOCHANNELID_END:
        case PDMAUDIOCHANNELID_32BIT_HACK:
            break;
    }
    AssertFailed();
    return SND_CHMAP_NA;
}


/**
 * Sets the hardware parameters of an ALSA stream.
 *
 * @returns 0 on success, negative errno on failure.
 * @param   hPCM        ALSA stream to set software parameters for.
 * @param   enmAlsaFmt  The ALSA format to use.
 * @param   pCfgReq     Requested stream configuration (PDM).
 * @param   pCfgAcq     The actual stream configuration (PDM).  This is assumed
 *                      to be a copy of pCfgReq on input, at least for
 *                      properties handled here.  On output some of the
 *                      properties may be updated to match the actual stream
 *                      configuration.
 */
static int alsaStreamSetHwParams(snd_pcm_t *hPCM, snd_pcm_format_t enmAlsaFmt,
                                 PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    /*
     * Get the current hardware parameters.
     */
    snd_pcm_hw_params_t *pHWParms = NULL;
    snd_pcm_hw_params_alloca(&pHWParms);
    AssertReturn(pHWParms, -ENOMEM);

    int err = snd_pcm_hw_params_any(hPCM, pHWParms);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to initialize hardware parameters: %s\n", snd_strerror(err)), err);

    /*
     * Modify them according to pAlsaCfgReq.
     * We update pAlsaCfgObt as we go for parameters set by "near" methods.
     */
    /* We'll use snd_pcm_writei/snd_pcm_readi: */
    err = snd_pcm_hw_params_set_access(hPCM, pHWParms, SND_PCM_ACCESS_RW_INTERLEAVED);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set access type: %s\n", snd_strerror(err)), err);

    /* Set the format and frequency. */
    err = snd_pcm_hw_params_set_format(hPCM, pHWParms, enmAlsaFmt);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set audio format to %d: %s\n", enmAlsaFmt, snd_strerror(err)), err);

    unsigned int uFreq = PDMAudioPropsHz(&pCfgReq->Props);
    err = snd_pcm_hw_params_set_rate_near(hPCM, pHWParms, &uFreq, NULL /*dir*/);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set frequency to %uHz: %s\n",
                                     PDMAudioPropsHz(&pCfgReq->Props), snd_strerror(err)), err);
    pCfgAcq->Props.uHz = uFreq;

    /* Channel count currently does not change with the mapping translations,
       as ALSA can express both silent and unknown channel positions. */
    union
    {
        snd_pcm_chmap_t Map;
        unsigned int    padding[1 + PDMAUDIO_MAX_CHANNELS];
    } u;
    uint8_t       aidSrcChannels[PDMAUDIO_MAX_CHANNELS];
    unsigned int *aidDstChannels = u.Map.pos;
    unsigned int  cChannels      = u.Map.channels = PDMAudioPropsChannels(&pCfgReq->Props);
    unsigned int  iDst           = 0;
    for (unsigned int iSrc = 0; iSrc < cChannels; iSrc++)
    {
        uint8_t const idSrc  = pCfgReq->Props.aidChannels[iSrc];
        aidSrcChannels[iDst] = idSrc;
        aidDstChannels[iDst] = drvHstAudAlsaPdmChToAlsa((PDMAUDIOCHANNELID)idSrc, cChannels);
        iDst++;
    }
    u.Map.channels = cChannels = iDst;
    for (; iDst < PDMAUDIO_MAX_CHANNELS; iDst++)
    {
        aidSrcChannels[iDst] = PDMAUDIOCHANNELID_INVALID;
        aidDstChannels[iDst] = SND_CHMAP_NA;
    }

    err = snd_pcm_hw_params_set_channels_near(hPCM, pHWParms, &cChannels);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set number of channels to %d\n", PDMAudioPropsChannels(&pCfgReq->Props)),
                          err);
    if (cChannels == PDMAudioPropsChannels(&pCfgReq->Props))
        memcpy(pCfgAcq->Props.aidChannels, aidSrcChannels, sizeof(pCfgAcq->Props.aidChannels));
    else
    {
        LogRel2(("ALSA: Requested %u channels, got %u\n", u.Map.channels, cChannels));
        AssertLogRelMsgReturn(cChannels > 0 && cChannels <= PDMAUDIO_MAX_CHANNELS,
                              ("ALSA: Unsupported channel count: %u (requested %d)\n",
                               cChannels, PDMAudioPropsChannels(&pCfgReq->Props)), -ERANGE);
        PDMAudioPropsSetChannels(&pCfgAcq->Props, (uint8_t)cChannels);
        /** @todo Can we somehow guess channel IDs? snd_pcm_get_chmap? */
    }

    /* The period size (reportedly frame count per hw interrupt): */
    int               dir    = 0;
    snd_pcm_uframes_t minval = pCfgReq->Backend.cFramesPeriod;
    err = snd_pcm_hw_params_get_period_size_min(pHWParms, &minval, &dir);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Could not determine minimal period size: %s\n", snd_strerror(err)), err);

    snd_pcm_uframes_t period_size_f = pCfgReq->Backend.cFramesPeriod;
    if (period_size_f < minval)
        period_size_f = minval;
    err = snd_pcm_hw_params_set_period_size_near(hPCM, pHWParms, &period_size_f, 0);
    LogRel2(("ALSA: Period size is: %lu frames (min %lu, requested %u)\n", period_size_f, minval, pCfgReq->Backend.cFramesPeriod));
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set period size %d (%s)\n", period_size_f, snd_strerror(err)), err);

    /* The buffer size: */
    minval = pCfgReq->Backend.cFramesBufferSize;
    err = snd_pcm_hw_params_get_buffer_size_min(pHWParms, &minval);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Could not retrieve minimal buffer size: %s\n", snd_strerror(err)), err);

    snd_pcm_uframes_t buffer_size_f = pCfgReq->Backend.cFramesBufferSize;
    if (buffer_size_f < minval)
        buffer_size_f = minval;
    err = snd_pcm_hw_params_set_buffer_size_near(hPCM, pHWParms, &buffer_size_f);
    LogRel2(("ALSA: Buffer size is: %lu frames (min %lu, requested %u)\n", buffer_size_f, minval, pCfgReq->Backend.cFramesBufferSize));
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to set near buffer size %RU32: %s\n", buffer_size_f, snd_strerror(err)), err);

    /*
     * Set the hardware parameters.
     */
    err = snd_pcm_hw_params(hPCM, pHWParms);
    AssertLogRelMsgReturn(err >= 0, ("ALSA: Failed to apply audio parameters: %s\n", snd_strerror(err)), err);

    /*
     * Get relevant parameters and put them in the pAlsaCfgObt structure.
     */
    snd_pcm_uframes_t obt_buffer_size = buffer_size_f;
    err = snd_pcm_hw_params_get_buffer_size(pHWParms, &obt_buffer_size);
    AssertLogRelMsgStmt(err >= 0, ("ALSA: Failed to get buffer size: %s\n", snd_strerror(err)), obt_buffer_size = buffer_size_f);
    pCfgAcq->Backend.cFramesBufferSize = obt_buffer_size;

    snd_pcm_uframes_t obt_period_size = period_size_f;
    err = snd_pcm_hw_params_get_period_size(pHWParms, &obt_period_size, &dir);
    AssertLogRelMsgStmt(err >= 0, ("ALSA: Failed to get period size: %s\n", snd_strerror(err)), obt_period_size = period_size_f);
    pCfgAcq->Backend.cFramesPeriod = obt_period_size;

    LogRel2(("ALSA: HW params: %u Hz, %u frames period, %u frames buffer, %u channel(s), enmAlsaFmt=%d\n",
             PDMAudioPropsHz(&pCfgAcq->Props), pCfgAcq->Backend.cFramesPeriod, pCfgAcq->Backend.cFramesBufferSize,
             PDMAudioPropsChannels(&pCfgAcq->Props), enmAlsaFmt));

#if 0 /* Disabled in the hope to resolve testboxes not being able to drain + crashing when closing the PCM streams. */
    /*
     * Channel config (not fatal).
     */
    if (PDMAudioPropsChannels(&pCfgAcq->Props) == PDMAudioPropsChannels(&pCfgReq->Props))
    {
        err = snd_pcm_set_chmap(hPCM, &u.Map);
        if (err < 0)
        {
            if (err == -ENXIO)
                LogRel2(("ALSA: Audio device does not support channel maps, skipping\n"));
            else
                LogRel2(("ALSA: snd_pcm_set_chmap failed: %s (%d)\n", snd_strerror(err), err));
        }
    }
#endif

    return 0;
}


/**
 * Opens (creates) an ALSA stream.
 *
 * @returns VBox status code.
 * @param   pThis       The alsa driver instance data.
 * @param   enmAlsaFmt  The ALSA format to use.
 * @param   pCfgReq     Requested configuration to create stream with (PDM).
 * @param   pCfgAcq     The actual stream configuration (PDM).  This is assumed
 *                      to be a copy of pCfgReq on input, at least for
 *                      properties handled here.  On output some of the
 *                      properties may be updated to match the actual stream
 *                      configuration.
 * @param   phPCM       Where to store the ALSA stream handle on success.
 */
static int alsaStreamOpen(PDRVHSTAUDALSA pThis, snd_pcm_format_t enmAlsaFmt, PCPDMAUDIOSTREAMCFG pCfgReq,
                          PPDMAUDIOSTREAMCFG pCfgAcq, snd_pcm_t **phPCM)
{
    /*
     * Open the stream.
     */
    int                rc      = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    const char * const pszType = pCfgReq->enmDir == PDMAUDIODIR_IN ? "input" : "output";
    const char * const pszDev  = pCfgReq->enmDir == PDMAUDIODIR_IN ? pThis->szInputDev : pThis->szOutputDev;
    snd_pcm_stream_t   enmType = pCfgReq->enmDir == PDMAUDIODIR_IN ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;

    snd_pcm_t *hPCM = NULL;
    LogRel(("ALSA: Using %s device \"%s\"\n", pszType, pszDev));
    int err = snd_pcm_open(&hPCM, pszDev, enmType, SND_PCM_NONBLOCK);
    if (err >= 0)
    {
        err = snd_pcm_nonblock(hPCM, 1);
        if (err >= 0)
        {
            /*
             * Configure hardware stream parameters.
             */
            err = alsaStreamSetHwParams(hPCM, enmAlsaFmt, pCfgReq, pCfgAcq);
            if (err >= 0)
            {
                /*
                 * Prepare it.
                 */
                rc = VERR_AUDIO_BACKEND_INIT_FAILED;
                err = snd_pcm_prepare(hPCM);
                if (err >= 0)
                {
                    /*
                     * Configure software stream parameters.
                     */
                    rc = alsaStreamSetSWParams(hPCM, pCfgReq, pCfgAcq);
                    if (RT_SUCCESS(rc))
                    {
                        *phPCM = hPCM;
                        return VINF_SUCCESS;
                    }
                }
                else
                    LogRel(("ALSA: snd_pcm_prepare failed: %s\n", snd_strerror(err)));
            }
        }
        else
            LogRel(("ALSA: Error setting non-blocking mode for %s stream: %s\n", pszType, snd_strerror(err)));
        drvHstAudAlsaStreamClose(&hPCM);
    }
    else
        LogRel(("ALSA: Failed to open \"%s\" as %s device: %s\n", pszDev, pszType, snd_strerror(err)));
    *phPCM = NULL;
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                      PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    PDRVHSTAUDALSA pThis = RT_FROM_MEMBER(pInterface, DRVHSTAUDALSA, IHostAudio);
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream,    VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq,    VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq,    VERR_INVALID_POINTER);

    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;
    PDMAudioStrmCfgCopy(&pStreamALSA->Cfg, pCfgReq);

    int                     rc;
    snd_pcm_format_t const  enmFmt = alsaAudioPropsToALSA(&pCfgReq->Props);
    if (enmFmt != SND_PCM_FORMAT_UNKNOWN)
    {
        rc = alsaStreamOpen(pThis, enmFmt, pCfgReq, pCfgAcq, &pStreamALSA->hPCM);
        if (RT_SUCCESS(rc))
        {
            /* We have no objections to the pre-buffering that DrvAudio applies,
               only we need to adjust it relative to the actual buffer size. */
            pCfgAcq->Backend.cFramesPreBuffering    = (uint64_t)pCfgReq->Backend.cFramesPreBuffering
                                                    * pCfgAcq->Backend.cFramesBufferSize
                                                    / RT_MAX(pCfgReq->Backend.cFramesBufferSize, 1);

            PDMAudioStrmCfgCopy(&pStreamALSA->Cfg, pCfgAcq);
            LogFlowFunc(("returns success - hPCM=%p\n", pStreamALSA->hPCM));
            return rc;
        }
    }
    else
        rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    LogFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream, bool fImmediate)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;
    AssertPtrReturn(pStreamALSA, VERR_INVALID_POINTER);
    RT_NOREF(fImmediate);

    LogRelFlowFunc(("Stream '%s' state is '%s'\n", pStreamALSA->Cfg.szName, snd_pcm_state_name(snd_pcm_state(pStreamALSA->hPCM))));

    /** @todo r=bird: It's not like we can do much with a bad status... Check
     *        what the caller does... */
    int rc = drvHstAudAlsaStreamClose(&pStreamALSA->hPCM);

    LogRelFlowFunc(("returns %Rrc\n", rc));

    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamEnable}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_StreamEnable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;

    /*
     * Prepare the stream.
     */
    int rc = snd_pcm_prepare(pStreamALSA->hPCM);
    if (rc >= 0)
    {
        Assert(snd_pcm_state(pStreamALSA->hPCM) == SND_PCM_STATE_PREPARED);

        /*
         * Input streams should be started now, whereas output streams must
         * pre-buffer sufficent data before starting.
         */
        if (pStreamALSA->Cfg.enmDir == PDMAUDIODIR_IN)
        {
            rc = snd_pcm_start(pStreamALSA->hPCM);
            if (rc >= 0)
                rc = VINF_SUCCESS;
            else
            {
                LogRel(("ALSA: Error starting input stream '%s': %s (%d)\n", pStreamALSA->Cfg.szName, snd_strerror(rc), rc));
                rc = RTErrConvertFromErrno(-rc);
            }
        }
        else
            rc = VINF_SUCCESS;
    }
    else
    {
        LogRel(("ALSA: Error preparing stream '%s': %s (%d)\n", pStreamALSA->Cfg.szName, snd_strerror(rc), rc));
        rc = RTErrConvertFromErrno(-rc);
    }
    LogFlowFunc(("returns %Rrc (state %s)\n", rc, snd_pcm_state_name(snd_pcm_state(pStreamALSA->hPCM))));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDisable}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_StreamDisable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;

    int rc = snd_pcm_drop(pStreamALSA->hPCM);
    if (rc >= 0)
        rc = VINF_SUCCESS;
    else
    {
        LogRel(("ALSA: Error stopping stream '%s': %s (%d)\n", pStreamALSA->Cfg.szName, snd_strerror(rc), rc));
        rc = RTErrConvertFromErrno(-rc);
    }
    LogFlowFunc(("returns %Rrc (state %s)\n", rc, snd_pcm_state_name(snd_pcm_state(pStreamALSA->hPCM))));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPause}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_StreamPause(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    /* Same as disable. */
    /** @todo r=bird: Try use pause and fallback on disable/enable if it isn't
     *        supported or doesn't work. */
    return drvHstAudAlsaHA_StreamDisable(pInterface, pStream);
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamResume}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_StreamResume(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    /* Same as enable. */
    return drvHstAudAlsaHA_StreamEnable(pInterface, pStream);
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDrain}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_StreamDrain(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;

    snd_pcm_state_t const enmState = snd_pcm_state(pStreamALSA->hPCM);
    LogRelFlowFunc(("Stream '%s' input state: %s (%d)\n", pStreamALSA->Cfg.szName, snd_pcm_state_name(enmState), enmState));

    /* Only for output streams. */
    AssertReturn(pStreamALSA->Cfg.enmDir == PDMAUDIODIR_OUT, VERR_WRONG_ORDER);

    int rc;
    switch (enmState)
    {
        case SND_PCM_STATE_RUNNING:
        case SND_PCM_STATE_PREPARED: /* not yet started */
        {
            /* Do not change to blocking here! */
            rc = snd_pcm_drain(pStreamALSA->hPCM);
            if (rc >= 0 || rc == -EAGAIN)
                rc = VINF_SUCCESS;
            else
            {
                snd_pcm_state_t const enmState2 = snd_pcm_state(pStreamALSA->hPCM);
                if (rc == -EPIPE && enmState2 == enmState)
                {
                    /* Not entirely sure, but possibly an underrun, so just disable the stream. */
                    LogRel2(("ALSA: snd_pcm_drain failed with -EPIPE, stopping stream (%s)\n", pStreamALSA->Cfg.szName));
                    rc = snd_pcm_drop(pStreamALSA->hPCM);
                    if (rc >= 0)
                        rc = VINF_SUCCESS;
                    else
                    {
                        LogRel(("ALSA: Error draining/stopping stream '%s': %s (%d)\n", pStreamALSA->Cfg.szName, snd_strerror(rc), rc));
                        rc = RTErrConvertFromErrno(-rc);
                    }
                }
                else
                {
                    LogRel(("ALSA: Error draining output of '%s': %s (%d; %s -> %s)\n", pStreamALSA->Cfg.szName, snd_strerror(rc),
                            rc, snd_pcm_state_name(enmState), snd_pcm_state_name(enmState2)));
                    rc = RTErrConvertFromErrno(-rc);
                }
            }
            break;
        }

        default:
            rc = VINF_SUCCESS;
            break;
    }
    LogRelFlowFunc(("returns %Rrc (state %s)\n", rc, snd_pcm_state_name(snd_pcm_state(pStreamALSA->hPCM))));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetState}
 */
static DECLCALLBACK(PDMHOSTAUDIOSTREAMSTATE) drvHstAudAlsaHA_StreamGetState(PPDMIHOSTAUDIO pInterface,
                                                                            PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;
    AssertPtrReturn(pStreamALSA, PDMHOSTAUDIOSTREAMSTATE_INVALID);

    PDMHOSTAUDIOSTREAMSTATE enmStreamState = PDMHOSTAUDIOSTREAMSTATE_OKAY;
    snd_pcm_state_t         enmAlsaState   = snd_pcm_state(pStreamALSA->hPCM);
    if (enmAlsaState == SND_PCM_STATE_DRAINING)
    {
        /* We're operating in non-blocking mode, so we must (at least for a demux
           config) call snd_pcm_drain again to drive it forward.  Otherwise we
           might be stuck in the drain state forever. */
        Log5Func(("Calling snd_pcm_drain again...\n"));
        snd_pcm_drain(pStreamALSA->hPCM);
        enmAlsaState = snd_pcm_state(pStreamALSA->hPCM);
    }

    if (enmAlsaState == SND_PCM_STATE_DRAINING)
        enmStreamState = PDMHOSTAUDIOSTREAMSTATE_DRAINING;
#if (((SND_LIB_MAJOR) << 16) | ((SND_LIB_MAJOR) << 8) | (SND_LIB_SUBMINOR)) >= 0x10002 /* was added in 1.0.2 */
    else if (enmAlsaState == SND_PCM_STATE_DISCONNECTED)
        enmStreamState = PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING;
#endif

    Log5Func(("Stream '%s': ALSA state=%s -> %s\n",
              pStreamALSA->Cfg.szName, snd_pcm_state_name(enmAlsaState), PDMHostAudioStreamStateGetName(enmStreamState) ));
    return enmStreamState;
}


/**
 * Returns the available audio frames queued.
 *
 * @returns VBox status code.
 * @param   hPCM           ALSA stream handle.
 * @param   pcFramesAvail   Where to store the available frames.
 */
static int alsaStreamGetAvail(snd_pcm_t *hPCM, snd_pcm_sframes_t *pcFramesAvail)
{
    AssertPtr(hPCM);
    AssertPtr(pcFramesAvail);

    int rc;
    snd_pcm_sframes_t cFramesAvail = snd_pcm_avail_update(hPCM);
    if (cFramesAvail > 0)
    {
        LogFunc(("cFramesAvail=%ld\n", cFramesAvail));
        *pcFramesAvail = cFramesAvail;
        return VINF_SUCCESS;
    }

    /*
     * We can maybe recover from an EPIPE...
     */
    if (cFramesAvail == -EPIPE)
    {
        rc = drvHstAudAlsaStreamRecover(hPCM);
        if (RT_SUCCESS(rc))
        {
            cFramesAvail = snd_pcm_avail_update(hPCM);
            if (cFramesAvail >= 0)
            {
                LogFunc(("cFramesAvail=%ld\n", cFramesAvail));
                *pcFramesAvail = cFramesAvail;
                return VINF_SUCCESS;
            }
        }
        else
        {
            *pcFramesAvail = 0;
            return rc;
        }
    }

    rc = RTErrConvertFromErrno(-(int)cFramesAvail);
    LogFunc(("failed - cFramesAvail=%ld rc=%Rrc\n", cFramesAvail, rc));
    *pcFramesAvail = 0;
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetPending}
 */
static DECLCALLBACK(uint32_t) drvHstAudAlsaHA_StreamGetPending(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;
    AssertPtrReturn(pStreamALSA, 0);

    /*
     * This is only relevant to output streams (input streams can't have
     * any pending, unplayed data).
     */
    uint32_t cbPending = 0;
    if (pStreamALSA->Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        /*
         * Getting the delay (in audio frames) reports the time it will take
         * to hear a new sample after all queued samples have been played out.
         *
         * We use snd_pcm_avail_delay instead of snd_pcm_delay here as it will
         * update the buffer positions, and we can use the extra value against
         * the buffer size to double check since the delay value may include
         * fixed built-in delays in the processing chain and hardware.
         */
        snd_pcm_sframes_t cFramesAvail = 0;
        snd_pcm_sframes_t cFramesDelay = 0;
        int rc = snd_pcm_avail_delay(pStreamALSA->hPCM, &cFramesAvail, &cFramesDelay);

        /*
         * We now also get the state as the pending value should be zero when
         * we're not in a playing state.
         */
        snd_pcm_state_t enmState = snd_pcm_state(pStreamALSA->hPCM);
        switch (enmState)
        {
            case SND_PCM_STATE_RUNNING:
            case SND_PCM_STATE_DRAINING:
                if (rc >= 0)
                {
                    if ((uint32_t)cFramesAvail >= pStreamALSA->Cfg.Backend.cFramesBufferSize)
                        cbPending = 0;
                    else
                        cbPending = PDMAudioPropsFramesToBytes(&pStreamALSA->Cfg.Props, cFramesDelay);
                }
                break;

            default:
                break;
        }
        Log2Func(("returns %u (%#x) - cFramesBufferSize=%RU32 cFramesAvail=%ld cFramesDelay=%ld rc=%d; enmState=%s (%d) \n",
                  cbPending, cbPending, pStreamALSA->Cfg.Backend.cFramesBufferSize, cFramesAvail, cFramesDelay, rc,
                  snd_pcm_state_name(enmState), enmState));
    }
    return cbPending;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvHstAudAlsaHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;

    uint32_t          cbAvail      = 0;
    snd_pcm_sframes_t cFramesAvail = 0;
    int rc = alsaStreamGetAvail(pStreamALSA->hPCM, &cFramesAvail);
    if (RT_SUCCESS(rc))
        cbAvail = PDMAudioPropsFramesToBytes(&pStreamALSA->Cfg.Props, cFramesAvail);

    return cbAvail;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                    const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbWritten, VERR_INVALID_POINTER);
    Log4Func(("@%#RX64: pvBuf=%p cbBuf=%#x (%u) state=%s - %s\n", pStreamALSA->offInternal, pvBuf, cbBuf, cbBuf,
              snd_pcm_state_name(snd_pcm_state(pStreamALSA->hPCM)), pStreamALSA->Cfg.szName));
    if (cbBuf)
        AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    else
    {
        /* Fend off draining calls. */
        *pcbWritten = 0;
        return VINF_SUCCESS;
    }

    /*
     * Determine how much we can write (caller actually did this
     * already, but we repeat it just to be sure or something).
     */
    snd_pcm_sframes_t cFramesAvail;
    int rc = alsaStreamGetAvail(pStreamALSA->hPCM, &cFramesAvail);
    if (RT_SUCCESS(rc))
    {
        Assert(cFramesAvail);
        if (cFramesAvail)
        {
            PCPDMAUDIOPCMPROPS pProps    = &pStreamALSA->Cfg.Props;
            uint32_t           cbToWrite = PDMAudioPropsFramesToBytes(pProps, (uint32_t)cFramesAvail);
            if (cbToWrite)
            {
                if (cbToWrite > cbBuf)
                    cbToWrite = cbBuf;

                /*
                 * Try write the data.
                 */
                uint32_t cFramesToWrite = PDMAudioPropsBytesToFrames(pProps, cbToWrite);
                snd_pcm_sframes_t cFramesWritten = snd_pcm_writei(pStreamALSA->hPCM, pvBuf, cFramesToWrite);
                if (cFramesWritten > 0)
                {
                    Log4Func(("snd_pcm_writei w/ cbToWrite=%u -> %ld (frames) [cFramesAvail=%ld]\n",
                              cbToWrite, cFramesWritten, cFramesAvail));
                    *pcbWritten = PDMAudioPropsFramesToBytes(pProps, cFramesWritten);
                    pStreamALSA->offInternal += *pcbWritten;
                    return VINF_SUCCESS;
                }
                LogFunc(("snd_pcm_writei w/ cbToWrite=%u -> %ld [cFramesAvail=%ld]\n", cbToWrite, cFramesWritten, cFramesAvail));


                /*
                 * There are a couple of error we can recover from, try to do so.
                 * Only don't try too many times.
                 */
                for (unsigned iTry = 0;
                     (cFramesWritten == -EPIPE || cFramesWritten == -ESTRPIPE) && iTry < ALSA_RECOVERY_TRIES_MAX;
                     iTry++)
                {
                    if (cFramesWritten == -EPIPE)
                    {
                        /* Underrun occurred. */
                        rc = drvHstAudAlsaStreamRecover(pStreamALSA->hPCM);
                        if (RT_FAILURE(rc))
                            break;
                        LogFlowFunc(("Recovered from playback (iTry=%u)\n", iTry));
                    }
                    else
                    {
                        /* An suspended event occurred, needs resuming. */
                        rc = drvHstAudAlsaStreamResume(pStreamALSA->hPCM);
                        if (RT_FAILURE(rc))
                        {
                            LogRel(("ALSA: Failed to resume output stream (iTry=%u, rc=%Rrc)\n", iTry, rc));
                            break;
                        }
                        LogFlowFunc(("Resumed suspended output stream (iTry=%u)\n", iTry));
                    }

                    cFramesWritten = snd_pcm_writei(pStreamALSA->hPCM, pvBuf, cFramesToWrite);
                    if (cFramesWritten > 0)
                    {
                        Log4Func(("snd_pcm_writei w/ cbToWrite=%u -> %ld (frames) [cFramesAvail=%ld]\n",
                                  cbToWrite, cFramesWritten, cFramesAvail));
                        *pcbWritten = PDMAudioPropsFramesToBytes(pProps, cFramesWritten);
                        pStreamALSA->offInternal += *pcbWritten;
                        return VINF_SUCCESS;
                    }
                    LogFunc(("snd_pcm_writei w/ cbToWrite=%u -> %ld [cFramesAvail=%ld, iTry=%d]\n", cbToWrite, cFramesWritten, cFramesAvail, iTry));
                }

                /* Make sure we return with an error status. */
                if (RT_SUCCESS_NP(rc))
                {
                    if (cFramesWritten == 0)
                        rc = VERR_ACCESS_DENIED;
                    else
                    {
                        rc = RTErrConvertFromErrno(-(int)cFramesWritten);
                        LogFunc(("Failed to write %RU32 bytes: %ld (%Rrc)\n", cbToWrite, cFramesWritten, rc));
                    }
                }
            }
        }
    }
    else
        LogFunc(("Error getting number of playback frames, rc=%Rrc\n", rc));
    *pcbWritten = 0;
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvHstAudAlsaHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;

    uint32_t          cbAvail      = 0;
    snd_pcm_sframes_t cFramesAvail = 0;
    int rc = alsaStreamGetAvail(pStreamALSA->hPCM, &cFramesAvail);
    if (RT_SUCCESS(rc))
        cbAvail = PDMAudioPropsFramesToBytes(&pStreamALSA->Cfg.Props, cFramesAvail);

    return cbAvail;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvHstAudAlsaHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                       void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    RT_NOREF_PV(pInterface);
    PDRVHSTAUDALSASTREAM pStreamALSA = (PDRVHSTAUDALSASTREAM)pStream;
    AssertPtrReturn(pStreamALSA, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbRead, VERR_INVALID_POINTER);
    Log4Func(("@%#RX64: pvBuf=%p cbBuf=%#x (%u) state=%s - %s\n", pStreamALSA->offInternal, pvBuf, cbBuf, cbBuf,
              snd_pcm_state_name(snd_pcm_state(pStreamALSA->hPCM)), pStreamALSA->Cfg.szName));

    /*
     * Figure out how much we can read without trouble (we're doing
     * non-blocking reads, but whatever).
     */
    snd_pcm_sframes_t cAvail;
    int rc = alsaStreamGetAvail(pStreamALSA->hPCM, &cAvail);
    if (RT_SUCCESS(rc))
    {
        if (!cAvail) /* No data yet? */
        {
            snd_pcm_state_t enmState = snd_pcm_state(pStreamALSA->hPCM);
            switch (enmState)
            {
                case SND_PCM_STATE_PREPARED:
                    /** @todo r=bird: explain the logic here...    */
                    cAvail = PDMAudioPropsBytesToFrames(&pStreamALSA->Cfg.Props, cbBuf);
                    break;

                case SND_PCM_STATE_SUSPENDED:
                    rc = drvHstAudAlsaStreamResume(pStreamALSA->hPCM);
                    if (RT_SUCCESS(rc))
                    {
                        LogFlowFunc(("Resumed suspended input stream.\n"));
                        break;
                    }
                    LogFunc(("Failed resuming suspended input stream: %Rrc\n", rc));
                    return rc;

                default:
                    LogFlow(("No frames available: state=%s (%d)\n", snd_pcm_state_name(enmState), enmState));
                    break;
            }
            if (!cAvail)
            {
                *pcbRead = 0;
                return VINF_SUCCESS;
            }
        }
    }
    else
    {
        LogFunc(("Error getting number of captured frames, rc=%Rrc\n", rc));
        return rc;
    }

    size_t cbToRead = PDMAudioPropsFramesToBytes(&pStreamALSA->Cfg.Props, cAvail);
    cbToRead = RT_MIN(cbToRead, cbBuf);
    LogFlowFunc(("cbToRead=%zu, cAvail=%RI32\n", cbToRead, cAvail));

    /*
     * Read loop.
     */
    uint32_t cbReadTotal = 0;
    while (cbToRead > 0)
    {
        /*
         * Do the reading.
         */
        snd_pcm_uframes_t const cFramesToRead = PDMAudioPropsBytesToFrames(&pStreamALSA->Cfg.Props, cbToRead);
        AssertBreakStmt(cFramesToRead > 0, rc = VERR_NO_DATA);

        snd_pcm_sframes_t cFramesRead = snd_pcm_readi(pStreamALSA->hPCM, pvBuf, cFramesToRead);
        if (cFramesRead > 0)
        {
            /*
             * We should not run into a full mixer buffer or we lose samples and
             * run into an endless loop if ALSA keeps producing samples ("null"
             * capture device for example).
             */
            uint32_t const cbRead = PDMAudioPropsFramesToBytes(&pStreamALSA->Cfg.Props, cFramesRead);
            Assert(cbRead <= cbToRead);

            cbToRead    -= cbRead;
            cbReadTotal += cbRead;
            pvBuf        = (uint8_t *)pvBuf + cbRead;
            pStreamALSA->offInternal += cbRead;
        }
        else
        {
            /*
             * Try recover from overrun and re-try.
             * Other conditions/errors we cannot and will just quit the loop.
             */
            if (cFramesRead == -EPIPE)
            {
                rc = drvHstAudAlsaStreamRecover(pStreamALSA->hPCM);
                if (RT_SUCCESS(rc))
                {
                    LogFlowFunc(("Successfully recovered from overrun\n"));
                    continue;
                }
                LogFunc(("Failed to recover from overrun: %Rrc\n", rc));
            }
            else if (cFramesRead == -EAGAIN)
                LogFunc(("No input frames available (EAGAIN)\n"));
            else if (cFramesRead == 0)
                LogFunc(("No input frames available (0)\n"));
            else
            {
                rc = RTErrConvertFromErrno(-(int)cFramesRead);
                LogFunc(("Failed to read input frames: %s (%ld, %Rrc)\n", snd_strerror(cFramesRead), cFramesRead, rc));
            }

            /* If we've read anything, suppress the error. */
            if (RT_FAILURE(rc) && cbReadTotal > 0)
            {
                LogFunc(("Suppressing %Rrc because %#x bytes has been read already\n", rc, cbReadTotal));
                rc = VINF_SUCCESS;
            }
            break;
        }
    }

    LogFlowFunc(("returns %Rrc and %#x (%d) bytes (%u bytes left); state %s\n",
                 rc, cbReadTotal, cbReadTotal, cbToRead, snd_pcm_state_name(snd_pcm_state(pStreamALSA->hPCM))));
    *pcbRead = cbReadTotal;
    return rc;
}


/*********************************************************************************************************************************
*   PDMIBASE                                                                                                                     *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHstAudAlsaQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS     pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHSTAUDALSA pThis   = PDMINS_2_DATA(pDrvIns, PDRVHSTAUDALSA);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, &pThis->IHostAudio);
    return NULL;
}


/*********************************************************************************************************************************
*   PDMDRVREG                                                                                                                    *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMDRVREG,pfnDestruct,
 * Destructs an ALSA host audio driver instance.}
 */
static DECLCALLBACK(void) drvHstAudAlsaDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVHSTAUDALSA pThis = PDMINS_2_DATA(pDrvIns, PDRVHSTAUDALSA);
    LogFlowFuncEnter();

    if (RTCritSectIsInitialized(&pThis->CritSect))
    {
        RTCritSectEnter(&pThis->CritSect);
        pThis->pIHostAudioPort = NULL;
        RTCritSectLeave(&pThis->CritSect);
        RTCritSectDelete(&pThis->CritSect);
    }

    LogFlowFuncLeave();
}


/**
 * @interface_method_impl{PDMDRVREG,pfnConstruct,
 * Construct an ALSA host audio driver instance.}
 */
static DECLCALLBACK(int) drvHstAudAlsaConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVHSTAUDALSA  pThis = PDMINS_2_DATA(pDrvIns, PDRVHSTAUDALSA);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;
    LogRel(("Audio: Initializing ALSA driver\n"));

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                   = pDrvIns;
    int rc = RTCritSectInit(&pThis->CritSect);
    AssertRCReturn(rc, rc);
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvHstAudAlsaQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnGetConfig                  = drvHstAudAlsaHA_GetConfig;
    pThis->IHostAudio.pfnGetDevices                 = drvHstAudAlsaHA_GetDevices;
    pThis->IHostAudio.pfnSetDevice                  = drvHstAudAlsaHA_SetDevice;
    pThis->IHostAudio.pfnGetStatus                  = drvHstAudAlsaHA_GetStatus;
    pThis->IHostAudio.pfnDoOnWorkerThread           = NULL;
    pThis->IHostAudio.pfnStreamConfigHint           = NULL;
    pThis->IHostAudio.pfnStreamCreate               = drvHstAudAlsaHA_StreamCreate;
    pThis->IHostAudio.pfnStreamInitAsync            = NULL;
    pThis->IHostAudio.pfnStreamDestroy              = drvHstAudAlsaHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamNotifyDeviceChanged  = NULL;
    pThis->IHostAudio.pfnStreamEnable               = drvHstAudAlsaHA_StreamEnable;
    pThis->IHostAudio.pfnStreamDisable              = drvHstAudAlsaHA_StreamDisable;
    pThis->IHostAudio.pfnStreamPause                = drvHstAudAlsaHA_StreamPause;
    pThis->IHostAudio.pfnStreamResume               = drvHstAudAlsaHA_StreamResume;
    pThis->IHostAudio.pfnStreamDrain                = drvHstAudAlsaHA_StreamDrain;
    pThis->IHostAudio.pfnStreamGetPending           = drvHstAudAlsaHA_StreamGetPending;
    pThis->IHostAudio.pfnStreamGetState             = drvHstAudAlsaHA_StreamGetState;
    pThis->IHostAudio.pfnStreamGetWritable          = drvHstAudAlsaHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamPlay                 = drvHstAudAlsaHA_StreamPlay;
    pThis->IHostAudio.pfnStreamGetReadable          = drvHstAudAlsaHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamCapture              = drvHstAudAlsaHA_StreamCapture;

    /*
     * Read configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "OutputDeviceID|InputDeviceID", "");

    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "InputDeviceID", pThis->szInputDev, sizeof(pThis->szInputDev), "default");
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "OutputDeviceID", pThis->szOutputDev, sizeof(pThis->szOutputDev), "default");
    AssertRCReturn(rc, rc);

    /*
     * Init the alsa library.
     */
    rc = audioLoadAlsaLib();
    if (RT_FAILURE(rc))
    {
        LogRel(("ALSA: Failed to load the ALSA shared library: %Rrc\n", rc));
        return rc;
    }

    /*
     * Query the notification interface from the driver/device above us.
     */
    pThis->pIHostAudioPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIHOSTAUDIOPORT);
    AssertReturn(pThis->pIHostAudioPort, VERR_PDM_MISSING_INTERFACE_ABOVE);

#ifdef DEBUG
    /*
     * Some debug stuff we don't use for anything at all.
     */
    snd_lib_error_set_handler(drvHstAudAlsaDbgErrorHandler);
#endif
    return VINF_SUCCESS;
}


/**
 * ALSA audio driver registration record.
 */
const PDMDRVREG g_DrvHostALSAAudio =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "ALSAAudio",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "ALSA host audio driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHSTAUDALSA),
    /* pfnConstruct */
    drvHstAudAlsaConstruct,
    /* pfnDestruct */
    drvHstAudAlsaDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

