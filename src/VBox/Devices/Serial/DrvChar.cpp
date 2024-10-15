/* $Id: DrvChar.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Driver that adapts PDMISTREAM into PDMISERIALCONNECTOR / PDMISERIALPORT.
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
#define LOG_GROUP LOG_GROUP_DRV_CHAR
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmserialifs.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/poll.h>
#include <iprt/stream.h>
#include <iprt/critsect.h>
#include <iprt/semaphore.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Char driver instance data.
 *
 * @implements PDMISERIALCONNECTOR
 */
typedef struct DRVCHAR
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the char port interface of the driver/device above us. */
    PPDMISERIALPORT             pDrvSerialPort;
    /** Pointer to the stream interface of the driver below us. */
    PPDMISTREAM                 pDrvStream;
    /** Our serial interface. */
    PDMISERIALCONNECTOR         ISerialConnector;
    /** Flag to notify the receive thread it should terminate. */
    volatile bool               fShutdown;
    /** Flag whether data is available from the device/driver above as notified by the driver. */
    volatile bool               fAvailWrExt;
    /** Internal copy of the flag which gets reset when there is no data anymore. */
    bool                        fAvailWrInt;
    /** I/O thread. */
    PPDMTHREAD                  pThrdIo;

    /** Small send buffer. */
    uint8_t                     abTxBuf[16];
    /** Amount of data in the buffer. */
    size_t                      cbTxUsed;

    /** Receive buffer. */
    uint8_t                     abBuffer[256];
    /** Number of bytes remaining in the receive buffer. */
    volatile size_t             cbRemaining;
    /** Current position into the read buffer. */
    uint8_t                     *pbBuf;

#if HC_ARCH_BITS == 32
    uint32_t                    uAlignment0;
#endif

    /** Read/write statistics */
    STAMCOUNTER                 StatBytesRead;
    STAMCOUNTER                 StatBytesWritten;
} DRVCHAR, *PDRVCHAR;
AssertCompileMemberAlignment(DRVCHAR, StatBytesRead, 8);




/* -=-=-=-=- IBase -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvCharQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVCHAR    pThis = PDMINS_2_DATA(pDrvIns, PDRVCHAR);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMISERIALCONNECTOR, &pThis->ISerialConnector);
    return NULL;
}


/* -=-=-=-=- ISerialConnector -=-=-=-=- */


/**
 * @interface_method_impl{PDMISERIALCONNECTOR,pfnDataAvailWrNotify}
 */
static DECLCALLBACK(int) drvCharDataAvailWrNotify(PPDMISERIALCONNECTOR pInterface)
{
    LogFlowFunc(("pInterface=%#p\n", pInterface));
    PDRVCHAR pThis = RT_FROM_MEMBER(pInterface, DRVCHAR, ISerialConnector);

    int rc = VINF_SUCCESS;
    bool fAvailOld = ASMAtomicXchgBool(&pThis->fAvailWrExt, true);
    if (!fAvailOld)
        rc = pThis->pDrvStream->pfnPollInterrupt(pThis->pDrvStream);

    return rc;
}


/**
 * @interface_method_impl{PDMISERIALCONNECTOR,pfnReadRdr}
 */
static DECLCALLBACK(int) drvCharReadRdr(PPDMISERIALCONNECTOR pInterface, void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    LogFlowFunc(("pInterface=%#p pvBuf=%#p cbRead=%zu pcbRead=%#p\n", pInterface, pvBuf, cbRead, pcbRead));
    PDRVCHAR pThis = RT_FROM_MEMBER(pInterface, DRVCHAR, ISerialConnector);
    int rc = VINF_SUCCESS;

    AssertReturn(pThis->cbRemaining, VERR_INVALID_STATE);
    size_t cbToRead = RT_MIN(cbRead, pThis->cbRemaining);
    memcpy(pvBuf, pThis->pbBuf, cbToRead);

    pThis->pbBuf += cbToRead;
    *pcbRead = cbToRead;
    size_t cbOld = ASMAtomicSubZ(&pThis->cbRemaining, cbToRead);
    if (!(cbOld - cbToRead)) /* Kick the I/O thread to fetch new data. */
        rc = pThis->pDrvStream->pfnPollInterrupt(pThis->pDrvStream);
    STAM_COUNTER_ADD(&pThis->StatBytesRead, cbToRead);

    LogFlowFunc(("-> %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMISERIALCONNECTOR,pfnChgParams}
 */
static DECLCALLBACK(int) drvCharChgParams(PPDMISERIALCONNECTOR pInterface, uint32_t uBps,
                                          PDMSERIALPARITY enmParity, unsigned cDataBits,
                                          PDMSERIALSTOPBITS enmStopBits)
{
    /* Nothing to do here. */
    RT_NOREF(pInterface, uBps, enmParity, cDataBits, enmStopBits);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{PDMISERIALCONNECTOR,pfnChgModemLines}
 */
static DECLCALLBACK(int) drvCharChgModemLines(PPDMISERIALCONNECTOR pInterface, bool fRts, bool fDtr)
{
    /* Nothing to do here. */
    RT_NOREF(pInterface, fRts, fDtr);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{PDMISERIALCONNECTOR,pfnChgBrk}
 */
static DECLCALLBACK(int) drvCharChgBrk(PPDMISERIALCONNECTOR pInterface, bool fBrk)
{
    /* Nothing to do here. */
    RT_NOREF(pInterface, fBrk);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{PDMISERIALCONNECTOR,pfnQueryStsLines}
 */
static DECLCALLBACK(int) drvCharQueryStsLines(PPDMISERIALCONNECTOR pInterface, uint32_t *pfStsLines)
{
    /* Always carrier detect, data set read and clear to send. */
    *pfStsLines = PDMISERIALPORT_STS_LINE_DCD | PDMISERIALPORT_STS_LINE_DSR | PDMISERIALPORT_STS_LINE_CTS;
    RT_NOREF(pInterface);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{PDMISERIALCONNECTOR,pfnQueuesFlush}
 */
static DECLCALLBACK(int) drvCharQueuesFlush(PPDMISERIALCONNECTOR pInterface, bool fQueueRecv, bool fQueueXmit)
{
    RT_NOREF(fQueueXmit);
    LogFlowFunc(("pInterface=%#p fQueueRecv=%RTbool fQueueXmit=%RTbool\n", pInterface, fQueueRecv, fQueueXmit));
    int rc = VINF_SUCCESS;
    PDRVCHAR pThis = RT_FROM_MEMBER(pInterface, DRVCHAR, ISerialConnector);

    if (fQueueRecv)
    {
        size_t cbOld = 0;
        cbOld = ASMAtomicXchgZ(&pThis->cbRemaining, 0);
        if (cbOld) /* Kick the I/O thread to fetch new data. */
            rc = pThis->pDrvStream->pfnPollInterrupt(pThis->pDrvStream);
    }

    LogFlowFunc(("-> %Rrc\n", rc));
    return rc;
}


/* -=-=-=-=- I/O thread -=-=-=-=- */

/**
 * Send thread loop - pushes data down thru the driver chain.
 *
 * @returns VBox status code.
 * @param   pDrvIns     The char driver instance.
 * @param   pThread     The worker thread.
 */
static DECLCALLBACK(int) drvCharIoLoop(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pDrvIns);
    PDRVCHAR pThis = (PDRVCHAR)pThread->pvUser;

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        uint32_t fEvts = 0;

        if (!pThis->fAvailWrInt)
            pThis->fAvailWrInt = ASMAtomicXchgBool(&pThis->fAvailWrExt, false);

        if (   !pThis->cbRemaining
            && pThis->pDrvStream->pfnRead)
            fEvts |= RTPOLL_EVT_READ;
        if (   pThis->fAvailWrInt
            || pThis->cbTxUsed)
            fEvts |= RTPOLL_EVT_WRITE;

        uint32_t fEvtsRecv = 0;
        int rc = pThis->pDrvStream->pfnPoll(pThis->pDrvStream, fEvts, &fEvtsRecv, RT_INDEFINITE_WAIT);
        if (RT_SUCCESS(rc))
        {
            if (fEvtsRecv & RTPOLL_EVT_WRITE)
            {
                if (   pThis->fAvailWrInt
                    && pThis->cbTxUsed < RT_ELEMENTS(pThis->abTxBuf))
                {
                    /* Stuff as much data into the TX buffer as we can. */
                    size_t cbToFetch = RT_ELEMENTS(pThis->abTxBuf) - pThis->cbTxUsed;
                    size_t cbFetched = 0;
                    rc = pThis->pDrvSerialPort->pfnReadWr(pThis->pDrvSerialPort, &pThis->abTxBuf[pThis->cbTxUsed], cbToFetch,
                                                          &cbFetched);
                    AssertRC(rc);

                    if (cbFetched > 0)
                        pThis->cbTxUsed  += cbFetched;
                    else
                    {
                        /* There is no data available anymore. */
                        pThis->fAvailWrInt = false;
                    }
                }

                if (pThis->cbTxUsed)
                {
                    size_t cbProcessed = pThis->cbTxUsed;
                    rc = pThis->pDrvStream->pfnWrite(pThis->pDrvStream, &pThis->abTxBuf[0], &cbProcessed);
                    if (RT_SUCCESS(rc))
                    {
                        pThis->cbTxUsed -= cbProcessed;
                        if (   pThis->cbTxUsed
                            && cbProcessed)
                        {
                            /* Move the data in the TX buffer to the front to fill the end again. */
                            memmove(&pThis->abTxBuf[0], &pThis->abTxBuf[cbProcessed], pThis->cbTxUsed);
                        }
                        else
                            pThis->pDrvSerialPort->pfnDataSentNotify(pThis->pDrvSerialPort);
                        STAM_COUNTER_ADD(&pThis->StatBytesWritten, cbProcessed);
                    }
                    else if (rc != VERR_TIMEOUT)
                    {
                        LogRel(("Char#%d: Write failed with %Rrc; skipping\n", pDrvIns->iInstance, rc));
                        break;
                    }
                }
            }

            if (fEvtsRecv & RTPOLL_EVT_READ)
            {
                AssertPtr(pThis->pDrvStream->pfnRead);
                Assert(!pThis->cbRemaining);

                size_t cbRead = sizeof(pThis->abBuffer);
                rc = pThis->pDrvStream->pfnRead(pThis->pDrvStream, &pThis->abBuffer[0], &cbRead);
                if (RT_FAILURE(rc))
                {
                    LogFlow(("Read failed with %Rrc\n", rc));
                    break;
                }

                if (cbRead)
                {
                    pThis->pbBuf = &pThis->abBuffer[0];
                    ASMAtomicWriteZ(&pThis->cbRemaining, cbRead);
                    /* Notify the upper device/driver. */
                    rc = pThis->pDrvSerialPort->pfnDataAvailRdrNotify(pThis->pDrvSerialPort, cbRead);
                    if (RT_FAILURE(rc))
                    {
                        LogRel(("Char#%d: Notifying upper driver about available data failed with %Rrc\n",
                                pDrvIns->iInstance, rc));
                        break;
                    }
                }
            }
        }
        else if (rc != VERR_INTERRUPTED)
            LogRelMax(10, ("Char#%d: Polling failed with %Rrc\n", pDrvIns->iInstance, rc));
    }

    return VINF_SUCCESS;
}


/**
 * Unblock the send worker thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDrvIns     The char driver instance.
 * @param   pThread     The worker thread.
 */
static DECLCALLBACK(int) drvCharIoLoopWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVCHAR pThis = (PDRVCHAR)pThread->pvUser;

    RT_NOREF(pDrvIns);
    return pThis->pDrvStream->pfnPollInterrupt(pThis->pDrvStream);
}


/* -=-=-=-=- driver interface -=-=-=-=- */

/**
 * @interface_method_impl{PDMDRVREG,pfnReset}
 */
static DECLCALLBACK(void) drvCharReset(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVCHAR pThis = PDMINS_2_DATA(pDrvIns, PDRVCHAR);

    /* Reset TX and RX buffers. */
    pThis->fAvailWrExt = false;
    pThis->fAvailWrInt = false;
    pThis->cbTxUsed    = 0;
    pThis->cbRemaining = 0;
}


/**
 * Construct a char driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvCharConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(pCfg);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVCHAR pThis = PDMINS_2_DATA(pDrvIns, PDRVCHAR);
    LogFlow(("%s: iInstance=%d\n", __FUNCTION__, pDrvIns->iInstance));

    /*
     * Init basic data members and interfaces.
     */
    pThis->pDrvIns                               = pDrvIns;
    pThis->pThrdIo                               = NIL_RTTHREAD;
    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface             = drvCharQueryInterface;
    /* ISerialConnector. */
    pThis->ISerialConnector.pfnDataAvailWrNotify = drvCharDataAvailWrNotify;
    pThis->ISerialConnector.pfnReadRdr           = drvCharReadRdr;
    pThis->ISerialConnector.pfnChgParams         = drvCharChgParams;
    pThis->ISerialConnector.pfnChgModemLines     = drvCharChgModemLines;
    pThis->ISerialConnector.pfnChgBrk            = drvCharChgBrk;
    pThis->ISerialConnector.pfnQueryStsLines     = drvCharQueryStsLines;
    pThis->ISerialConnector.pfnQueuesFlush       = drvCharQueuesFlush;

    /*
     * Get the ISerialPort interface of the above driver/device.
     */
    pThis->pDrvSerialPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMISERIALPORT);
    if (!pThis->pDrvSerialPort)
        return PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE, RT_SRC_POS,
                                   N_("Char#%d has no serial port interface above"), pDrvIns->iInstance);

    /*
     * Attach driver below and query its stream interface.
     */
    PPDMIBASE pBase;
    int rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pBase);
    if (RT_FAILURE(rc))
        return rc; /* Don't call PDMDrvHlpVMSetError here as we assume that the driver already set an appropriate error */
    pThis->pDrvStream = PDMIBASE_QUERY_INTERFACE(pBase, PDMISTREAM);
    if (!pThis->pDrvStream)
        return PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_MISSING_INTERFACE_BELOW, RT_SRC_POS,
                                   N_("Char#%d has no stream interface below"), pDrvIns->iInstance);

    rc = PDMDrvHlpThreadCreate(pThis->pDrvIns, &pThis->pThrdIo, pThis, drvCharIoLoop,
                               drvCharIoLoopWakeup, 0, RTTHREADTYPE_IO, "CharIo");
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("Char#%d cannot create I/O thread"), pDrvIns->iInstance);


    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatBytesWritten, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                           "Nr of bytes written",         "/Devices/Char%d/Written", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatBytesRead,    STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                           "Nr of bytes read",            "/Devices/Char%d/Read", pDrvIns->iInstance);

    return VINF_SUCCESS;
}


/**
 * Char driver registration record.
 */
const PDMDRVREG g_DrvChar =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "Char",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Generic char driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_CHAR,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVCHAR),
    /* pfnConstruct */
    drvCharConstruct,
    /* pfnDestruct */
    NULL,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    drvCharReset,
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
