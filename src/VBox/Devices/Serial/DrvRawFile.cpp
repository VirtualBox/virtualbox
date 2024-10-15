/* $Id: DrvRawFile.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox stream drivers - Raw file output.
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
#define LOG_GROUP LOG_GROUP_DEFAULT
#include <VBox/vmm/pdmdrv.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/poll.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Raw file output driver instance data.
 *
 * @implements  PDMISTREAM
 */
typedef struct DRVRAWFILE
{
    /** The stream interface. */
    PDMISTREAM          IStream;
    /** Pointer to the driver instance. */
    PPDMDRVINS          pDrvIns;
    /** Pointer to the file name. (Freed by MM) */
    char               *pszLocation;
    /** File handle to write the data to. */
    RTFILE              hOutputFile;
    /** Event semaphore for the poll interface. */
    RTSEMEVENT          hSemEvtPoll;
} DRVRAWFILE, *PDRVRAWFILE;



/* -=-=-=-=- PDMISTREAM -=-=-=-=- */

/** @interface_method_impl{PDMISTREAM,pfnPoll} */
static DECLCALLBACK(int) drvRawFilePoll(PPDMISTREAM pInterface, uint32_t fEvts, uint32_t *pfEvts, RTMSINTERVAL cMillies)
{
    PDRVRAWFILE pThis = RT_FROM_MEMBER(pInterface, DRVRAWFILE, IStream);

    Assert(!(fEvts & RTPOLL_EVT_READ)); /* Reading is not supported here. */

    /* Writing is always possible. */
    if (fEvts & RTPOLL_EVT_WRITE)
    {
        *pfEvts = RTPOLL_EVT_WRITE;
        return VINF_SUCCESS;
    }

    return RTSemEventWait(pThis->hSemEvtPoll, cMillies);
}


/** @interface_method_impl{PDMISTREAM,pfnPollInterrupt} */
static DECLCALLBACK(int) drvRawFilePollInterrupt(PPDMISTREAM pInterface)
{
    PDRVRAWFILE pThis = RT_FROM_MEMBER(pInterface, DRVRAWFILE, IStream);
    return RTSemEventSignal(pThis->hSemEvtPoll);
}


/** @interface_method_impl{PDMISTREAM,pfnWrite} */
static DECLCALLBACK(int) drvRawFileWrite(PPDMISTREAM pInterface, const void *pvBuf, size_t *pcbWrite)
{
    int rc = VINF_SUCCESS;
    PDRVRAWFILE pThis = RT_FROM_MEMBER(pInterface, DRVRAWFILE, IStream);
    LogFlow(("%s: pvBuf=%p *pcbWrite=%#x (%s)\n", __FUNCTION__, pvBuf, *pcbWrite, pThis->pszLocation));

    Assert(pvBuf);
    if (pThis->hOutputFile != NIL_RTFILE)
    {
        size_t cbWritten;
        rc = RTFileWrite(pThis->hOutputFile, pvBuf, *pcbWrite, &cbWritten);
#if 0
        /* don't flush here, takes too long and we will loose characters */
        if (RT_SUCCESS(rc))
            RTFileFlush(pThis->hOutputFile);
#endif
        *pcbWrite = cbWritten;
    }

    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}

/* -=-=-=-=- PDMIBASE -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvRawFileQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVRAWFILE pThis   = PDMINS_2_DATA(pDrvIns, PDRVRAWFILE);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMISTREAM, &pThis->IStream);
    return NULL;
}

/* -=-=-=-=- PDMDRVREG -=-=-=-=- */


/**
 * Power off a raw output stream driver instance.
 *
 * This does most of the destruction work, to avoid ordering dependencies.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvRawFilePowerOff(PPDMDRVINS pDrvIns)
{
    PDRVRAWFILE pThis = PDMINS_2_DATA(pDrvIns, PDRVRAWFILE);
    LogFlow(("%s: %s\n", __FUNCTION__, pThis->pszLocation));

    RTFileClose(pThis->hOutputFile);
    pThis->hOutputFile = NIL_RTFILE;
}


/**
 * Destruct a raw output stream driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that
 * any non-VM resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvRawFileDestruct(PPDMDRVINS pDrvIns)
{
    PDRVRAWFILE pThis = PDMINS_2_DATA(pDrvIns, PDRVRAWFILE);
    LogFlow(("%s: %s\n", __FUNCTION__, pThis->pszLocation));
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    if (pThis->pszLocation)
        PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszLocation);

    if (pThis->hOutputFile != NIL_RTFILE)
    {
        RTFileClose(pThis->hOutputFile);
        pThis->hOutputFile = NIL_RTFILE;
    }

    if (pThis->hSemEvtPoll != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThis->hSemEvtPoll);
        pThis->hSemEvtPoll = NIL_RTSEMEVENT;
    }
}


/**
 * Construct a raw output stream driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvRawFileConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVRAWFILE     pThis = PDMINS_2_DATA(pDrvIns, PDRVRAWFILE);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                      = pDrvIns;
    pThis->pszLocation                  = NULL;
    pThis->hOutputFile                  = NIL_RTFILE;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvRawFileQueryInterface;
    /* IStream */
    pThis->IStream.pfnPoll              = drvRawFilePoll;
    pThis->IStream.pfnPollInterrupt     = drvRawFilePollInterrupt;
    pThis->IStream.pfnRead              = NULL;
    pThis->IStream.pfnWrite             = drvRawFileWrite;

    /*
     * Read the configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "Location", "");

    int rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "Location", &pThis->pszLocation);
    if (RT_FAILURE(rc))
        AssertMsgFailedReturn(("Configuration error: query \"Location\" resulted in %Rrc.\n", rc), rc);

     rc = RTSemEventCreate(&pThis->hSemEvtPoll);
     AssertRCReturn(rc, rc);

    /*
     * Open the raw file.
     */
    rc = RTFileOpen(&pThis->hOutputFile, pThis->pszLocation, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
    {
        LogRel(("RawFile%d: CreateFile failed rc=%Rrc\n", pDrvIns->iInstance, rc));
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("RawFile#%d failed to create the raw output file %s"), pDrvIns->iInstance, pThis->pszLocation);
    }

    LogFlow(("drvRawFileConstruct: location %s\n", pThis->pszLocation));
    LogRel(("RawFile#%u: location %s\n", pDrvIns->iInstance, pThis->pszLocation));
    return VINF_SUCCESS;
}


/**
 * Raw file driver registration record.
 */
const PDMDRVREG g_DrvRawFile =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "RawFile",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "RawFile stream driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_STREAM,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVRAWFILE),
    /* pfnConstruct */
    drvRawFileConstruct,
    /* pfnDestruct */
    drvRawFileDestruct,
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
    drvRawFilePowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

