/* $Id: DrvHostAudioNull.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Host audio driver - NULL (bitbucket).
 *
 * This also acts as a fallback if no other backend is available.
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
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/uuid.h> /* For PDMIBASE_2_PDMDRV. */

#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <VBox/log.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Null audio stream. */
typedef struct DRVHSTAUDNULLSTREAM
{
    /** Common part. */
    PDMAUDIOBACKENDSTREAM   Core;
    /** The stream's acquired configuration. */
    PDMAUDIOSTREAMCFG       Cfg;
} DRVHSTAUDNULLSTREAM;
/** Pointer to a null audio stream.   */
typedef DRVHSTAUDNULLSTREAM *PDRVHSTAUDNULLSTREAM;



/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvHstAudNullHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    NOREF(pInterface);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);

    /*
     * Fill in the config structure.
     */
    RTStrCopy(pBackendCfg->szName, sizeof(pBackendCfg->szName), "NULL audio");
    pBackendCfg->cbStream       = sizeof(DRVHSTAUDNULLSTREAM);
    pBackendCfg->fFlags         = 0;
    pBackendCfg->cMaxStreamsOut = 1; /* Output */
    pBackendCfg->cMaxStreamsIn  = 2; /* Line input + microphone input. */

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvHstAudNullHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(pInterface, enmDir);
    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvHstAudNullHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                      PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDNULLSTREAM pStreamNull = (PDRVHSTAUDNULLSTREAM)pStream;
    AssertPtrReturn(pStreamNull, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq, VERR_INVALID_POINTER);

    PDMAudioStrmCfgCopy(&pStreamNull->Cfg, pCfgAcq);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvHstAudNullHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream, bool fImmediate)
{
    RT_NOREF(pInterface, pStream, fImmediate);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamEnable}
 */
static DECLCALLBACK(int) drvHstAudNullHA_StreamControlStub(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetState}
 */
static DECLCALLBACK(PDMHOSTAUDIOSTREAMSTATE) drvHstAudNullHA_StreamGetState(PPDMIHOSTAUDIO pInterface,
                                                                            PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pStream, PDMHOSTAUDIOSTREAMSTATE_INVALID);
#if 0
    /* Bit bucket appraoch where we ignore the output and silence the input buffers.  */
    return PDMHOSTAUDIOSTREAMSTATE_OKAY;
#else
    /* Approach where the mixer in the devices skips us and saves a few CPU cycles. */
    return PDMHOSTAUDIOSTREAMSTATE_INACTIVE;
#endif
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetPending}
 */
static DECLCALLBACK(uint32_t) drvHstAudNullHA_StreamGetPending(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return 0;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvHstAudNullHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return UINT32_MAX;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvHstAudNullHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                    const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    RT_NOREF(pInterface, pStream, pvBuf);

    /* The bitbucket never overflows. */
    *pcbWritten = cbBuf;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvHstAudNullHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    /** @todo rate limit this?   */
    return UINT32_MAX;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvHstAudNullHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                       void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    RT_NOREF(pInterface);
    PDRVHSTAUDNULLSTREAM pStreamNull = (PDRVHSTAUDNULLSTREAM)pStream;

    /** @todo rate limit this? */

    /* Return silence. */
    PDMAudioPropsClearBuffer(&pStreamNull->Cfg.Props, pvBuf, cbBuf,
                             PDMAudioPropsBytesToFrames(&pStreamNull->Cfg.Props, cbBuf));
    *pcbRead = cbBuf;
    return VINF_SUCCESS;
}


/**
 * This is used directly by DrvAudio when a backend fails to initialize in a
 * non-fatal manner.
 */
DECL_HIDDEN_CONST(PDMIHOSTAUDIO) const g_DrvHostAudioNull =
{
    /* .pfnGetConfig                 =*/ drvHstAudNullHA_GetConfig,
    /* .pfnGetDevices                =*/ NULL,
    /* .pfnSetDevice                 =*/ NULL,
    /* .pfnGetStatus                 =*/ drvHstAudNullHA_GetStatus,
    /* .pfnDoOnWorkerThread          =*/ NULL,
    /* .pfnStreamConfigHint          =*/ NULL,
    /* .pfnStreamCreate              =*/ drvHstAudNullHA_StreamCreate,
    /* .pfnStreamInitAsync           =*/ NULL,
    /* .pfnStreamDestroy             =*/ drvHstAudNullHA_StreamDestroy,
    /* .pfnStreamNotifyDeviceChanged =*/ NULL,
    /* .pfnStreamEnable              =*/ drvHstAudNullHA_StreamControlStub,
    /* .pfnStreamDisable             =*/ drvHstAudNullHA_StreamControlStub,
    /* .pfnStreamPause               =*/ drvHstAudNullHA_StreamControlStub,
    /* .pfnStreamResume              =*/ drvHstAudNullHA_StreamControlStub,
    /* .pfnStreamDrain               =*/ drvHstAudNullHA_StreamControlStub,
    /* .pfnStreamGetState            =*/ drvHstAudNullHA_StreamGetState,
    /* .pfnStreamGetPending          =*/ drvHstAudNullHA_StreamGetPending,
    /* .pfnStreamGetWritable         =*/ drvHstAudNullHA_StreamGetWritable,
    /* .pfnStreamPlay                =*/ drvHstAudNullHA_StreamPlay,
    /* .pfnStreamGetReadable         =*/ drvHstAudNullHA_StreamGetReadable,
    /* .pfnStreamCapture             =*/ drvHstAudNullHA_StreamCapture,
};


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHstAudNullQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS        pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PPDMIHOSTAUDIO    pThis   = PDMINS_2_DATA(pDrvIns, PPDMIHOSTAUDIO);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, pThis);
    return NULL;
}


/**
 * Constructs a Null audio driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvHstAudNullConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PPDMIHOSTAUDIO pThis = PDMINS_2_DATA(pDrvIns, PPDMIHOSTAUDIO);
    RT_NOREF(pCfg, fFlags);
    LogRel(("Audio: Initializing NULL driver\n"));

    /*
     * Init the static parts.
     */
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvHstAudNullQueryInterface;
    /* IHostAudio */
    *pThis = g_DrvHostAudioNull;

    return VINF_SUCCESS;
}


/**
 * Char driver registration record.
 */
const PDMDRVREG g_DrvHostNullAudio =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "NullAudio",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "NULL audio host driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(PDMIHOSTAUDIO),
    /* pfnConstruct */
    drvHstAudNullConstruct,
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

