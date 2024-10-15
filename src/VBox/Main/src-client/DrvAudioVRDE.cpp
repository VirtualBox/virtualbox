/* $Id: DrvAudioVRDE.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VRDE audio backend for Main.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include "LoggingNew.h"

#include <VBox/log.h>
#include "DrvAudioVRDE.h"
#include "ConsoleImpl.h"
#include "ConsoleVRDPServer.h"

#include <iprt/mem.h>
#include <iprt/cdefs.h>
#include <iprt/circbuf.h>

#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/RemoteDesktop/VRDE.h>
#include <VBox/err.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * VRDE stream.
 */
typedef struct VRDESTREAM
{
    /** Common part. */
    PDMAUDIOBACKENDSTREAM   Core;
    /** The stream's acquired configuration. */
    PDMAUDIOSTREAMCFG       Cfg;
    union
    {
        struct
        {
            /** Circular buffer for holding the recorded audio frames from the host. */
            PRTCIRCBUF      pCircBuf;
        } In;
    };
} VRDESTREAM;
/** Pointer to a VRDE stream. */
typedef VRDESTREAM *PVRDESTREAM;

/**
 * VRDE (host) audio driver instance data.
 */
typedef struct DRVAUDIOVRDE
{
    /** Pointer to audio VRDE object. */
    AudioVRDE           *pAudioVRDE;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS           pDrvIns;
    /** Pointer to the VRDP's console object. */
    ConsoleVRDPServer   *pConsoleVRDPServer;
    /** Number of connected clients to this VRDE instance. */
    uint32_t             cClients;
    /** Interface to the driver above us (DrvAudio).   */
    PDMIHOSTAUDIOPORT   *pIHostAudioPort;
    /** Pointer to host audio interface. */
    PDMIHOSTAUDIO        IHostAudio;
} DRVAUDIOVRDE;
/** Pointer to the instance data for an VRDE audio driver. */
typedef DRVAUDIOVRDE *PDRVAUDIOVRDE;


/*********************************************************************************************************************************
*   Class AudioVRDE                                                                                                              *
*********************************************************************************************************************************/

AudioVRDE::AudioVRDE(Console *pConsole)
    : AudioDriver(pConsole)
    , mpDrv(NULL)
{
    RTCritSectInit(&mCritSect);
}


AudioVRDE::~AudioVRDE(void)
{
    RTCritSectEnter(&mCritSect);
    if (mpDrv)
    {
        mpDrv->pAudioVRDE = NULL;
        mpDrv = NULL;
    }
    RTCritSectLeave(&mCritSect);
    RTCritSectDelete(&mCritSect);
}


int AudioVRDE::configureDriver(PCFGMNODE pLunCfg, PCVMMR3VTABLE pVMM)
{
    return AudioDriver::configureDriver(pLunCfg, pVMM);
}


void AudioVRDE::onVRDEClientConnect(uint32_t uClientID)
{
    RT_NOREF(uClientID);

    RTCritSectEnter(&mCritSect);
    if (mpDrv)
    {
        mpDrv->cClients++;
        LogRel2(("Audio: VRDE client connected (#%u)\n", mpDrv->cClients));

#if 0 /* later, maybe */
        /*
         * The first client triggers a device change event in both directions
         * so that can start talking to the audio device.
         *
         * Note! Should be okay to stay in the critical section here, as it's only
         *       used at construction and destruction time.
         */
        if (mpDrv->cClients == 1)
        {
            VMSTATE enmState = PDMDrvHlpVMState(mpDrv->pDrvIns);
            if (enmState <= VMSTATE_POWERING_OFF)
            {
                PDMIHOSTAUDIOPORT *pIHostAudioPort = mpDrv->pIHostAudioPort;
                AssertPtr(pIHostAudioPort);
                pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, PDMAUDIODIR_OUT, NULL /*pvUser*/);
                pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, PDMAUDIODIR_IN,  NULL /*pvUser*/);
            }
        }
#endif
    }
    RTCritSectLeave(&mCritSect);
}


void AudioVRDE::onVRDEClientDisconnect(uint32_t uClientID)
{
    RT_NOREF(uClientID);

    RTCritSectEnter(&mCritSect);
    if (mpDrv)
    {
        Assert(mpDrv->cClients > 0);
        mpDrv->cClients--;
        LogRel2(("Audio: VRDE client disconnected (%u left)\n", mpDrv->cClients));
#if 0 /* later maybe */
        /*
         * The last client leaving triggers a device change event in both
         * directions so the audio devices can stop wasting time trying to
         * talk to us.  (There is an additional safeguard in
         * drvAudioVrdeHA_StreamGetStatus.)
         */
        if (mpDrv->cClients == 0)
        {
            VMSTATE enmState = PDMDrvHlpVMState(mpDrv->pDrvIns);
            if (enmState <= VMSTATE_POWERING_OFF)
            {
                PDMIHOSTAUDIOPORT *pIHostAudioPort = mpDrv->pIHostAudioPort;
                AssertPtr(pIHostAudioPort);
                pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, PDMAUDIODIR_OUT, NULL /*pvUser*/);
                pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, PDMAUDIODIR_IN,  NULL /*pvUser*/);
            }
        }
#endif
    }
    RTCritSectLeave(&mCritSect);
}


int AudioVRDE::onVRDEControl(bool fEnable, uint32_t uFlags)
{
    RT_NOREF(fEnable, uFlags);
    LogFlowThisFunc(("fEnable=%RTbool, uFlags=0x%x\n", fEnable, uFlags));

    if (mpDrv == NULL)
        return VERR_INVALID_STATE;

    return VINF_SUCCESS; /* Never veto. */
}


/**
 * Marks the beginning of sending captured audio data from a connected
 * RDP client.
 *
 * @returns VBox status code.
 * @param   pvContext               The context; in this case a pointer to a
 *                                  VRDESTREAMIN structure.
 * @param   pVRDEAudioBegin         Pointer to a VRDEAUDIOINBEGIN structure.
 */
int AudioVRDE::onVRDEInputBegin(void *pvContext, PVRDEAUDIOINBEGIN pVRDEAudioBegin)
{
    AssertPtrReturn(pvContext, VERR_INVALID_POINTER);
    AssertPtrReturn(pVRDEAudioBegin, VERR_INVALID_POINTER);
    PVRDESTREAM pVRDEStrmIn = (PVRDESTREAM)pvContext;
    AssertPtrReturn(pVRDEStrmIn, VERR_INVALID_POINTER);

#ifdef LOG_ENABLED
    VRDEAUDIOFORMAT const audioFmt = pVRDEAudioBegin->fmt;
    LogFlowFunc(("cbSample=%RU32, iSampleHz=%d, cChannels=%d, cBits=%d, fUnsigned=%RTbool\n",
                 VRDE_AUDIO_FMT_BYTES_PER_SAMPLE(audioFmt), VRDE_AUDIO_FMT_SAMPLE_FREQ(audioFmt),
                 VRDE_AUDIO_FMT_CHANNELS(audioFmt), VRDE_AUDIO_FMT_BITS_PER_SAMPLE(audioFmt), VRDE_AUDIO_FMT_SIGNED(audioFmt)));
#endif

    return VINF_SUCCESS;
}


int AudioVRDE::onVRDEInputData(void *pvContext, const void *pvData, uint32_t cbData)
{
    PVRDESTREAM pStreamVRDE = (PVRDESTREAM)pvContext;
    AssertPtrReturn(pStreamVRDE, VERR_INVALID_POINTER);
    LogFlowFunc(("cbData=%#x\n", cbData));

    void  *pvBuf = NULL;
    size_t cbBuf = 0;
    RTCircBufAcquireWriteBlock(pStreamVRDE->In.pCircBuf, cbData, &pvBuf, &cbBuf);

    if (cbBuf)
        memcpy(pvBuf, pvData, cbBuf);

    RTCircBufReleaseWriteBlock(pStreamVRDE->In.pCircBuf, cbBuf);

    if (cbBuf < cbData)
        LogRelMax(999, ("VRDE: Capturing audio data lost %zu bytes\n", cbData - cbBuf)); /** @todo Use an error counter. */

    return VINF_SUCCESS; /** @todo r=andy How to tell the caller if we were not able to handle *all* input data? */
}


int AudioVRDE::onVRDEInputEnd(void *pvContext)
{
    RT_NOREF(pvContext);
    return VINF_SUCCESS;
}


int AudioVRDE::onVRDEInputIntercept(bool fEnabled)
{
    RT_NOREF(fEnabled);
    return VINF_SUCCESS; /* Never veto. */
}



/*********************************************************************************************************************************
*   PDMIHOSTAUDIO                                                                                                                *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);

    RTStrCopy(pBackendCfg->szName, sizeof(pBackendCfg->szName), "VRDE");
    pBackendCfg->cbStream       = sizeof(VRDESTREAM);
    pBackendCfg->fFlags         = 0;
    pBackendCfg->cMaxStreamsIn  = UINT32_MAX;
    pBackendCfg->cMaxStreamsOut = UINT32_MAX;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvAudioVrdeHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(pInterface, enmDir);
    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                     PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    PDRVAUDIOVRDE pThis       = RT_FROM_MEMBER(pInterface, DRVAUDIOVRDE, IHostAudio);
    PVRDESTREAM   pStreamVRDE = (PVRDESTREAM)pStream;
    AssertPtrReturn(pStreamVRDE, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq, VERR_INVALID_POINTER);

    /*
     * Only create a stream if we have clients.
     */
    int vrc;
    NOREF(pThis);
#if 0 /* later maybe */
    if (pThis->cClients == 0)
    {
        LogFunc(("No clients, failing with VERR_AUDIO_STREAM_COULD_NOT_CREATE.\n"));
        vrc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    }
    else
#endif
    {
        /*
         * The VRDP server does its own mixing and resampling because it may be
         * sending the audio to any number of different clients all with different
         * formats (including clients which hasn't yet connected).  So, it desires
         * the raw data from the mixer (somewhat akind to stereo signed 64-bit,
         * see st_sample_t and PDMAUDIOFRAME).
         */
        PDMAudioPropsInitEx(&pCfgAcq->Props, 8 /*64-bit*/, true /*fSigned*/, 2 /*stereo*/,
                            22050 /*Hz - VRDP_AUDIO_CHUNK_INTERNAL_FREQ_HZ*/,
                            true /*fLittleEndian*/, true /*fRaw*/);

        /* According to the VRDP docs (VRDP_AUDIO_CHUNK_TIME_MS), the VRDP server
           stores audio in 200ms chunks. */
        const uint32_t cFramesVrdpServer = PDMAudioPropsMilliToFrames(&pCfgAcq->Props, 200 /*ms*/);

        if (pCfgReq->enmDir == PDMAUDIODIR_IN)
        {
            pCfgAcq->Backend.cFramesBufferSize      = cFramesVrdpServer;
            pCfgAcq->Backend.cFramesPeriod          = cFramesVrdpServer / 4; /* This is utter non-sense, but whatever. */
            pCfgAcq->Backend.cFramesPreBuffering    = pCfgReq->Backend.cFramesPreBuffering * cFramesVrdpServer
                                                    / RT_MAX(pCfgReq->Backend.cFramesBufferSize, 1);

            vrc = RTCircBufCreate(&pStreamVRDE->In.pCircBuf, PDMAudioPropsFramesToBytes(&pCfgAcq->Props, cFramesVrdpServer));
        }
        else
        {
            /** @todo r=bird: So, if VRDP does 200ms chunks, why do we report 100ms
             *        buffer and 20ms period?  How does these parameters at all correlate
             *        with the above comment?!? */
            pCfgAcq->Backend.cFramesPeriod       = PDMAudioPropsMilliToFrames(&pCfgAcq->Props, 20  /*ms*/);
            pCfgAcq->Backend.cFramesBufferSize   = PDMAudioPropsMilliToFrames(&pCfgAcq->Props, 100 /*ms*/);
            pCfgAcq->Backend.cFramesPreBuffering = pCfgAcq->Backend.cFramesPeriod * 2;
            vrc = VINF_SUCCESS;
        }

        PDMAudioStrmCfgCopy(&pStreamVRDE->Cfg, pCfgAcq);
    }
    return vrc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                      bool fImmediate)
{
    PDRVAUDIOVRDE pDrv        = RT_FROM_MEMBER(pInterface, DRVAUDIOVRDE, IHostAudio);
    PVRDESTREAM   pStreamVRDE = (PVRDESTREAM)pStream;
    AssertPtrReturn(pStreamVRDE, VERR_INVALID_POINTER);
    RT_NOREF(fImmediate);

    if (pStreamVRDE->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        LogFlowFunc(("Calling SendAudioInputEnd\n"));
        if (pDrv->pConsoleVRDPServer)
            pDrv->pConsoleVRDPServer->SendAudioInputEnd(NULL);

        if (pStreamVRDE->In.pCircBuf)
        {
            RTCircBufDestroy(pStreamVRDE->In.pCircBuf);
            pStreamVRDE->In.pCircBuf = NULL;
        }
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamEnable}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_StreamEnable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVAUDIOVRDE pDrv        = RT_FROM_MEMBER(pInterface, DRVAUDIOVRDE, IHostAudio);
    PVRDESTREAM   pStreamVRDE = (PVRDESTREAM)pStream;

    int vrc;
    if (!pDrv->pConsoleVRDPServer)
    {
        LogRelMax(32, ("Audio: VRDP console not ready (enable)\n"));
        vrc = VERR_AUDIO_STREAM_NOT_READY;
    }
    else if (pStreamVRDE->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        vrc = pDrv->pConsoleVRDPServer->SendAudioInputBegin(NULL, pStreamVRDE,
                                                            PDMAudioPropsMilliToFrames(&pStreamVRDE->Cfg.Props, 200 /*ms*/),
                                                            PDMAudioPropsHz(&pStreamVRDE->Cfg.Props),
                                                            PDMAudioPropsChannels(&pStreamVRDE->Cfg.Props),
                                                            PDMAudioPropsSampleBits(&pStreamVRDE->Cfg.Props));
        LogFlowFunc(("SendAudioInputBegin returns %Rrc\n", vrc));
        if (vrc == VERR_NOT_SUPPORTED)
        {
            LogRelMax(64, ("Audio: No VRDE client connected, so no input recording available\n"));
            vrc = VERR_AUDIO_STREAM_NOT_READY;
        }
    }
    else
        vrc = VINF_SUCCESS;
    LogFlowFunc(("returns %Rrc\n", vrc));
    return vrc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDisable}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_StreamDisable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVAUDIOVRDE pDrv        = RT_FROM_MEMBER(pInterface, DRVAUDIOVRDE, IHostAudio);
    PVRDESTREAM   pStreamVRDE = (PVRDESTREAM)pStream;

    int vrc;
    if (!pDrv->pConsoleVRDPServer)
    {
        LogRelMax(32, ("Audio: VRDP console not ready (disable)\n"));
        vrc = VERR_AUDIO_STREAM_NOT_READY;
    }
    else if (pStreamVRDE->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        LogFlowFunc(("Calling SendAudioInputEnd\n"));
        pDrv->pConsoleVRDPServer->SendAudioInputEnd(NULL /* pvUserCtx */);
        vrc = VINF_SUCCESS;
    }
    else
        vrc = VINF_SUCCESS;
    LogFlowFunc(("returns %Rrc\n", vrc));
    return vrc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPause}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_StreamPause(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVAUDIOVRDE pDrv = RT_FROM_MEMBER(pInterface, DRVAUDIOVRDE, IHostAudio);
    RT_NOREF(pStream);

    if (!pDrv->pConsoleVRDPServer)
    {
        LogRelMax(32, ("Audio: VRDP console not ready (pause)\n"));
        return VERR_AUDIO_STREAM_NOT_READY;
    }
    LogFlowFunc(("returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamResume}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_StreamResume(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVAUDIOVRDE pDrv = RT_FROM_MEMBER(pInterface, DRVAUDIOVRDE, IHostAudio);
    RT_NOREF(pStream);

    if (!pDrv->pConsoleVRDPServer)
    {
        LogRelMax(32, ("Audio: VRDP console not ready (resume)\n"));
        return VERR_AUDIO_STREAM_NOT_READY;
    }
    LogFlowFunc(("returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDrain}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_StreamDrain(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    LogFlowFunc(("returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetState}
 */
static DECLCALLBACK(PDMHOSTAUDIOSTREAMSTATE) drvAudioVrdeHA_StreamGetState(PPDMIHOSTAUDIO pInterface,
                                                                           PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVAUDIOVRDE pDrv = RT_FROM_MEMBER(pInterface, DRVAUDIOVRDE, IHostAudio);
    AssertPtrReturn(pStream, PDMHOSTAUDIOSTREAMSTATE_INVALID);

    return pDrv->cClients > 0 ? PDMHOSTAUDIOSTREAMSTATE_OKAY : PDMHOSTAUDIOSTREAMSTATE_INACTIVE;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvAudioVrdeHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVAUDIOVRDE pDrv        = RT_FROM_MEMBER(pInterface, DRVAUDIOVRDE, IHostAudio);
    PVRDESTREAM   pStreamVRDE = (PVRDESTREAM)pStream;

    /** @todo Find some sane value here. We probably need a VRDE API VRDE to specify this. */
    if (pDrv->cClients)
        return PDMAudioPropsFramesToBytes(&pStreamVRDE->Cfg.Props, pStreamVRDE->Cfg.Backend.cFramesBufferSize);
    return 0;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                   const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    PDRVAUDIOVRDE pDrv = RT_FROM_MEMBER(pInterface, DRVAUDIOVRDE, IHostAudio);
    AssertPtr(pDrv);
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);
    PVRDESTREAM pStreamVRDE = (PVRDESTREAM)pStream;
    if (cbBuf)
        AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbWritten, VERR_INVALID_POINTER);

    if (!pDrv->pConsoleVRDPServer)
        return VERR_NOT_AVAILABLE;

    /* Prepate the format. */
    PPDMAUDIOPCMPROPS pProps = &pStreamVRDE->Cfg.Props;
    VRDEAUDIOFORMAT const uVrdpFormat = VRDE_AUDIO_FMT_MAKE(PDMAudioPropsHz(pProps),
                                                            PDMAudioPropsChannels(pProps),
                                                            PDMAudioPropsSampleBits(pProps),
                                                            pProps->fSigned);
    Assert(uVrdpFormat == VRDE_AUDIO_FMT_MAKE(PDMAudioPropsHz(pProps), 2, 64, true));

    /** @todo r=bird: there was some incoherent mumbling about "using the
     *        internal counter to track if we (still) can write to the VRDP
     *        server or if need to wait another round (time slot)".  However it
     *        wasn't accessing any internal counter nor doing anything else
     *        sensible, so I've removed it. */

    uint32_t cFrames = PDMAudioPropsBytesToFrames(&pStream->pStream->Cfg.Props, cbBuf);
    Assert(cFrames == cbBuf / (sizeof(uint64_t) * 2));
    pDrv->pConsoleVRDPServer->SendAudioSamples(pvBuf, cFrames, uVrdpFormat);

    Log3Func(("cFramesWritten=%RU32\n", cFrames));
    *pcbWritten = PDMAudioPropsFramesToBytes(&pStream->pStream->Cfg.Props, cFrames);
    Assert(*pcbWritten == cbBuf);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvAudioVrdeHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PVRDESTREAM pStreamVRDE = (PVRDESTREAM)pStream;

    AssertReturn(pStreamVRDE->Cfg.enmDir == PDMAUDIODIR_IN, 0);
    uint32_t cbRet = (uint32_t)RTCircBufUsed(pStreamVRDE->In.pCircBuf);
    Log4Func(("returns %#x\n", cbRet));
    return cbRet;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvAudioVrdeHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                      void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    RT_NOREF(pInterface);
    PVRDESTREAM pStreamVRDE = (PVRDESTREAM)pStream;
    AssertPtrReturn(pStreamVRDE, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbRead, VERR_INVALID_PARAMETER);

    *pcbRead = 0;
    while (cbBuf > 0 && RTCircBufUsed(pStreamVRDE->In.pCircBuf) > 0)
    {
        size_t cbData = 0;
        void  *pvData = NULL;
        RTCircBufAcquireReadBlock(pStreamVRDE->In.pCircBuf, cbBuf, &pvData, &cbData);

        memcpy(pvBuf, pvData, cbData);

        RTCircBufReleaseReadBlock(pStreamVRDE->In.pCircBuf, cbData);

        *pcbRead += (uint32_t)cbData;
        cbBuf    -= (uint32_t)cbData;
        pvData    = (uint8_t *)pvData + cbData;
    }

    LogFlowFunc(("returns %#x bytes\n", *pcbRead));
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   PDMIBASE                                                                                                                     *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvAudioVrdeQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVAUDIOVRDE pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIOVRDE);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, &pThis->IHostAudio);
    return NULL;
}


/*********************************************************************************************************************************
*   PDMDRVREG                                                                                                                    *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMDRVREG,pfnPowerOff}
 */
/*static*/ DECLCALLBACK(void) AudioVRDE::drvPowerOff(PPDMDRVINS pDrvIns)
{
    PDRVAUDIOVRDE pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIOVRDE);
    LogFlowFuncEnter();

    if (pThis->pConsoleVRDPServer)
        pThis->pConsoleVRDPServer->SendAudioInputEnd(NULL);
}


/**
 * @interface_method_impl{PDMDRVREG,pfnDestruct}
 */
/*static*/ DECLCALLBACK(void) AudioVRDE::drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVAUDIOVRDE pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIOVRDE);
    LogFlowFuncEnter();

    /** @todo For runtime detach maybe:
    if (pThis->pConsoleVRDPServer)
        pThis->pConsoleVRDPServer->SendAudioInputEnd(NULL); */

    /*
     * If the AudioVRDE object is still alive, we must clear it's reference to
     * us since we'll be invalid when we return from this method.
     */
    AudioVRDE *pAudioVRDE = pThis->pAudioVRDE;
    if (pAudioVRDE)
    {
        RTCritSectEnter(&pAudioVRDE->mCritSect);
        pAudioVRDE->mpDrv = NULL;
        pThis->pAudioVRDE = NULL;
        RTCritSectLeave(&pAudioVRDE->mCritSect);
    }
}


/**
 * Construct a VRDE audio driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
/* static */
DECLCALLBACK(int) AudioVRDE::drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVAUDIOVRDE pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIOVRDE);
    RT_NOREF(fFlags);

    AssertPtrReturn(pDrvIns, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg, VERR_INVALID_POINTER);

    LogRel(("Audio: Initializing VRDE driver\n"));
    LogFlowFunc(("fFlags=0x%x\n", fFlags));

    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                   = pDrvIns;
    pThis->cClients                  = 0;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvAudioVrdeQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnGetConfig                  = drvAudioVrdeHA_GetConfig;
    pThis->IHostAudio.pfnGetDevices                 = NULL;
    pThis->IHostAudio.pfnSetDevice                  = NULL;
    pThis->IHostAudio.pfnGetStatus                  = drvAudioVrdeHA_GetStatus;
    pThis->IHostAudio.pfnDoOnWorkerThread           = NULL;
    pThis->IHostAudio.pfnStreamConfigHint           = NULL;
    pThis->IHostAudio.pfnStreamCreate               = drvAudioVrdeHA_StreamCreate;
    pThis->IHostAudio.pfnStreamInitAsync            = NULL;
    pThis->IHostAudio.pfnStreamDestroy              = drvAudioVrdeHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamNotifyDeviceChanged  = NULL;
    pThis->IHostAudio.pfnStreamEnable               = drvAudioVrdeHA_StreamEnable;
    pThis->IHostAudio.pfnStreamDisable              = drvAudioVrdeHA_StreamDisable;
    pThis->IHostAudio.pfnStreamPause                = drvAudioVrdeHA_StreamPause;
    pThis->IHostAudio.pfnStreamResume               = drvAudioVrdeHA_StreamResume;
    pThis->IHostAudio.pfnStreamDrain                = drvAudioVrdeHA_StreamDrain;
    pThis->IHostAudio.pfnStreamGetState             = drvAudioVrdeHA_StreamGetState;
    pThis->IHostAudio.pfnStreamGetPending           = NULL;
    pThis->IHostAudio.pfnStreamGetWritable          = drvAudioVrdeHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamPlay                 = drvAudioVrdeHA_StreamPlay;
    pThis->IHostAudio.pfnStreamGetReadable          = drvAudioVrdeHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamCapture              = drvAudioVrdeHA_StreamCapture;

    /*
     * Resolve the interface to the driver above us.
     */
    pThis->pIHostAudioPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIHOSTAUDIOPORT);
    AssertPtrReturn(pThis->pIHostAudioPort, VERR_PDM_MISSING_INTERFACE_ABOVE);

    /* Get the Console object pointer. */
    com::Guid ConsoleUuid(COM_IIDOF(IConsole));
    IConsole *pIConsole = (IConsole *)PDMDrvHlpQueryGenericUserObject(pDrvIns, ConsoleUuid.raw());
    AssertLogRelReturn(pIConsole, VERR_INTERNAL_ERROR_3);
    Console *pConsole = static_cast<Console *>(pIConsole);
    AssertLogRelReturn(pConsole, VERR_INTERNAL_ERROR_3);

    /* Get the console VRDP object pointer. */
    pThis->pConsoleVRDPServer = pConsole->i_consoleVRDPServer();
    AssertLogRelMsgReturn(RT_VALID_PTR(pThis->pConsoleVRDPServer) || !pThis->pConsoleVRDPServer,
                          ("pConsoleVRDPServer=%p\n", pThis->pConsoleVRDPServer), VERR_INVALID_POINTER);

    /* Get the AudioVRDE object pointer. */
    pThis->pAudioVRDE = pConsole->i_getAudioVRDE();
    AssertLogRelMsgReturn(RT_VALID_PTR(pThis->pAudioVRDE), ("pAudioVRDE=%p\n", pThis->pAudioVRDE), VERR_INVALID_POINTER);
    RTCritSectEnter(&pThis->pAudioVRDE->mCritSect);
    pThis->pAudioVRDE->mpDrv = pThis;
    RTCritSectLeave(&pThis->pAudioVRDE->mCritSect);

    return VINF_SUCCESS;
}


/**
 * VRDE audio driver registration record.
 */
const PDMDRVREG AudioVRDE::DrvReg =
{
    PDM_DRVREG_VERSION,
    /* szName */
    "AudioVRDE",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Audio driver for VRDE backend",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVAUDIOVRDE),
    /* pfnConstruct */
    AudioVRDE::drvConstruct,
    /* pfnDestruct */
    AudioVRDE::drvDestruct,
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
    AudioVRDE::drvPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

