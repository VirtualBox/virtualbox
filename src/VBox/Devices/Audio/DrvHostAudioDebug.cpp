/* $Id: DrvHostAudioDebug.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Host audio driver - Debug - For dumping and injecting audio data from/to the device emulation.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <VBox/log.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

#include <iprt/uuid.h> /* For PDMIBASE_2_PDMDRV. */

#include "AudioHlp.h"
#include "AudioTest.h"
#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Debug host audio stream.
 */
typedef struct DRVHSTAUDDEBUGSTREAM
{
    /** Common part. */
    PDMAUDIOBACKENDSTREAM   Core;
    /** The stream's acquired configuration. */
    PDMAUDIOSTREAMCFG       Cfg;
    /** Audio file to dump output to or read input from. */
    PAUDIOHLPFILE           pFile;
    union
    {
        AUDIOTESTTONE       In;
    };
} DRVHSTAUDDEBUGSTREAM;
/** Pointer to a debug host audio stream. */
typedef DRVHSTAUDDEBUGSTREAM *PDRVHSTAUDDEBUGSTREAM;

/**
 * Debug audio driver instance data.
 * @implements PDMIAUDIOCONNECTOR
 */
typedef struct DRVHSTAUDDEBUG
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS      pDrvIns;
    /** Pointer to host audio interface. */
    PDMIHOSTAUDIO   IHostAudio;
} DRVHSTAUDDEBUG;
/** Pointer to a debug host audio driver. */
typedef DRVHSTAUDDEBUG *PDRVHSTAUDDEBUG;


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);

    /*
     * Fill in the config structure.
     */
    RTStrCopy(pBackendCfg->szName, sizeof(pBackendCfg->szName), "DebugAudio");
    pBackendCfg->cbStream       = sizeof(DRVHSTAUDDEBUGSTREAM);
    pBackendCfg->fFlags         = 0;
    pBackendCfg->cMaxStreamsOut = 1; /* Output; writing to a file. */
    pBackendCfg->cMaxStreamsIn  = 1; /* Input; generates a sine wave. */

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvHstAudDebugHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(pInterface, enmDir);
    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                       PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    PDRVHSTAUDDEBUG         pThis      = RT_FROM_MEMBER(pInterface, DRVHSTAUDDEBUG, IHostAudio);
    PDRVHSTAUDDEBUGSTREAM   pStreamDbg = (PDRVHSTAUDDEBUGSTREAM)pStream;
    AssertPtrReturn(pStreamDbg, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq, VERR_INVALID_POINTER);

    PDMAudioStrmCfgCopy(&pStreamDbg->Cfg, pCfgAcq);

    if (pCfgReq->enmDir == PDMAUDIODIR_IN)
        AudioTestToneInitRandom(&pStreamDbg->In, &pStreamDbg->Cfg.Props);

    int rc = AudioHlpFileCreateAndOpenEx(&pStreamDbg->pFile, AUDIOHLPFILETYPE_WAV, NULL /*use temp dir*/,
                                         pThis->pDrvIns->iInstance, AUDIOHLPFILENAME_FLAGS_NONE, AUDIOHLPFILE_FLAGS_NONE,
                                         &pCfgReq->Props, RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE,
                                         pCfgReq->enmDir == PDMAUDIODIR_IN ? "DebugAudioIn" : "DebugAudioOut");
    if (RT_FAILURE(rc))
        LogRel(("DebugAudio: Failed to creating debug file for %s stream '%s' in the temp directory: %Rrc\n",
                pCfgReq->enmDir == PDMAUDIODIR_IN ? "input" : "output", pCfgReq->szName, rc));

    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                        bool fImmediate)
{
    RT_NOREF(pInterface, fImmediate);
    PDRVHSTAUDDEBUGSTREAM pStreamDbg = (PDRVHSTAUDDEBUGSTREAM)pStream;
    AssertPtrReturn(pStreamDbg, VERR_INVALID_POINTER);

    if (pStreamDbg->pFile)
    {
        AudioHlpFileDestroy(pStreamDbg->pFile);
        pStreamDbg->pFile = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamEnable}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_StreamEnable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDisable}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_StreamDisable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPause}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_StreamPause(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamResume}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_StreamResume(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDrain}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_StreamDrain(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetState}
 */
static DECLCALLBACK(PDMHOSTAUDIOSTREAMSTATE) drvHstAudDebugHA_StreamGetState(PPDMIHOSTAUDIO pInterface,
                                                                             PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pStream, PDMHOSTAUDIOSTREAMSTATE_INVALID);
    return PDMHOSTAUDIOSTREAMSTATE_OKAY;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetPending}
 */
static DECLCALLBACK(uint32_t) drvHstAudDebugHA_StreamGetPending(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return 0;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvHstAudDebugHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return UINT32_MAX;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                        const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDDEBUGSTREAM pStreamDbg = (PDRVHSTAUDDEBUGSTREAM)pStream;

    int rc = AudioHlpFileWrite(pStreamDbg->pFile, pvBuf, cbBuf);
    if (RT_SUCCESS(rc))
        *pcbWritten = cbBuf;
    else
        LogRelMax(32, ("DebugAudio: Writing output failed with %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvHstAudDebugHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDDEBUGSTREAM pStreamDbg = (PDRVHSTAUDDEBUGSTREAM)pStream;

    return PDMAudioPropsMilliToBytes(&pStreamDbg->Cfg.Props, 10 /*ms*/);
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvHstAudDebugHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                        void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDDEBUGSTREAM pStreamDbg = (PDRVHSTAUDDEBUGSTREAM)pStream;
/** @todo rate limit this?  */

    uint32_t cbWritten;
    int rc = AudioTestToneGenerate(&pStreamDbg->In, pvBuf, cbBuf, &cbWritten);
    if (RT_SUCCESS(rc))
    {
        /*
         * Write it.
         */
        rc = AudioHlpFileWrite(pStreamDbg->pFile, pvBuf, cbWritten);
        if (RT_SUCCESS(rc))
            *pcbRead = cbWritten;
    }

    if (RT_FAILURE(rc))
        LogRelMax(32, ("DebugAudio: Writing input failed with %Rrc\n", rc));

    return rc;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHstAudDebugQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS      pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHSTAUDDEBUG pThis   = PDMINS_2_DATA(pDrvIns, PDRVHSTAUDDEBUG);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, &pThis->IHostAudio);
    return NULL;
}


/**
 * Constructs a Null audio driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvHstAudDebugConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(pCfg, fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVHSTAUDDEBUG pThis = PDMINS_2_DATA(pDrvIns, PDRVHSTAUDDEBUG);
    LogRel(("Audio: Initializing DEBUG driver\n"));

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                   = pDrvIns;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvHstAudDebugQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnGetConfig                  = drvHstAudDebugHA_GetConfig;
    pThis->IHostAudio.pfnGetDevices                 = NULL;
    pThis->IHostAudio.pfnSetDevice                  = NULL;
    pThis->IHostAudio.pfnGetStatus                  = drvHstAudDebugHA_GetStatus;
    pThis->IHostAudio.pfnDoOnWorkerThread           = NULL;
    pThis->IHostAudio.pfnStreamConfigHint           = NULL;
    pThis->IHostAudio.pfnStreamCreate               = drvHstAudDebugHA_StreamCreate;
    pThis->IHostAudio.pfnStreamInitAsync            = NULL;
    pThis->IHostAudio.pfnStreamDestroy              = drvHstAudDebugHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamNotifyDeviceChanged  = NULL;
    pThis->IHostAudio.pfnStreamEnable               = drvHstAudDebugHA_StreamEnable;
    pThis->IHostAudio.pfnStreamDisable              = drvHstAudDebugHA_StreamDisable;
    pThis->IHostAudio.pfnStreamPause                = drvHstAudDebugHA_StreamPause;
    pThis->IHostAudio.pfnStreamResume               = drvHstAudDebugHA_StreamResume;
    pThis->IHostAudio.pfnStreamDrain                = drvHstAudDebugHA_StreamDrain;
    pThis->IHostAudio.pfnStreamGetState             = drvHstAudDebugHA_StreamGetState;
    pThis->IHostAudio.pfnStreamGetPending           = drvHstAudDebugHA_StreamGetPending;
    pThis->IHostAudio.pfnStreamGetWritable          = drvHstAudDebugHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamPlay                 = drvHstAudDebugHA_StreamPlay;
    pThis->IHostAudio.pfnStreamGetReadable          = drvHstAudDebugHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamCapture              = drvHstAudDebugHA_StreamCapture;

    return VINF_SUCCESS;
}

/**
 * Char driver registration record.
 */
const PDMDRVREG g_DrvHostDebugAudio =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "DebugAudio",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Debug audio host driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHSTAUDDEBUG),
    /* pfnConstruct */
    drvHstAudDebugConstruct,
    /* pfnDestruct */
    NULL,
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
