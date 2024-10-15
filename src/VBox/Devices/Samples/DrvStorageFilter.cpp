/* $Id: DrvStorageFilter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage filter driver sample.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MISC
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/version.h>

#include <iprt/uuid.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Storage Filter Driver Instance Data.
 */
typedef struct DRVSTORAGEFILTER
{
    /** @name Interfaces exposed by this driver.
     * @{ */
    PDMIMEDIA           IMedia;
    PDMIMEDIAPORT       IMediaPort;
    PDMIMEDIAEX         IMediaEx;
    PDMIMEDIAEXPORT     IMediaExPort;
    /** @}  */

    /** @name Interfaces exposed by the driver below us.
     * @{ */
    PPDMIMEDIA          pIMediaBelow;
    PPDMIMEDIAEX        pIMediaExBelow;
    /** @} */

    /** @name Interfaces exposed by the driver/device above us.
     * @{ */
    PPDMIMEDIAPORT      pIMediaPortAbove;
    PPDMIMEDIAEXPORT    pIMediaExPortAbove;
    /** @} */

    /** If clear, then suppress Async support. */
    bool                fAsyncIOSupported;

    /** @todo implement memfrob. */
} DRVSTORAGEFILTER;
/** Pointer to a storage filter driver instance. */
typedef DRVSTORAGEFILTER *PDRVSTORAGEFILTER;



/*
 *
 * IMediaPort Implementation.
 *
 */

/** @interface_method_impl{PDMIMEDIAPORT,pfnQueryDeviceLocation} */
static DECLCALLBACK(int) drvStorageFltIMediaPort_QueryDeviceLocation(PPDMIMEDIAPORT pInterface, const char **ppcszController,
                                                                     uint32_t *piInstance, uint32_t *piLUN)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaPort);
    int rc = pThis->pIMediaPortAbove->pfnQueryDeviceLocation(pThis->pIMediaPortAbove, ppcszController, piInstance, piLUN);
    return rc;
}


/*
 *
 * IMedia Implementation.
 *
 */

/** @interface_method_impl{PDMIMEDIA,pfnRead} */
static DECLCALLBACK(int) drvStorageFltIMedia_Read(PPDMIMEDIA pInterface, uint64_t off, void *pvBuf, size_t cbRead)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnRead(pThis->pIMediaBelow, off, pvBuf, cbRead);
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnWrite} */
static DECLCALLBACK(int) drvStorageFltIMedia_Write(PPDMIMEDIA pInterface, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnWrite(pThis->pIMediaBelow, off, pvBuf, cbWrite);
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnFlush} */
static DECLCALLBACK(int) drvStorageFltIMedia_Flush(PPDMIMEDIA pInterface)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnFlush(pThis->pIMediaBelow);
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnMerge} */
static DECLCALLBACK(int) drvStorageFltIMedia_Merge(PPDMIMEDIA pInterface, PFNSIMPLEPROGRESS pfnProgress, void *pvUser)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnMerge(pThis->pIMediaBelow, pfnProgress, pvUser);
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnGetSize} */
static DECLCALLBACK(uint64_t) drvStorageFltIMedia_GetSize(PPDMIMEDIA pInterface)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    uint64_t cb = pThis->pIMediaBelow->pfnGetSize(pThis->pIMediaBelow);
    return cb;
}

/** @interface_method_impl{PDMIMEDIA,pfnIsReadOnly} */
static DECLCALLBACK(bool) drvStorageFltIMedia_IsReadOnly(PPDMIMEDIA pInterface)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    bool fRc = pThis->pIMediaBelow->pfnIsReadOnly(pThis->pIMediaBelow);
    return fRc;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosGetPCHSGeometry} */
static DECLCALLBACK(int) drvStorageFltIMedia_BiosGetPCHSGeometry(PPDMIMEDIA pInterface, PPDMMEDIAGEOMETRY pPCHSGeometry)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnBiosGetPCHSGeometry(pThis->pIMediaBelow, pPCHSGeometry);
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosSetPCHSGeometry} */
static DECLCALLBACK(int) drvStorageFltIMedia_BiosSetPCHSGeometry(PPDMIMEDIA pInterface, PCPDMMEDIAGEOMETRY pPCHSGeometry)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnBiosSetPCHSGeometry(pThis->pIMediaBelow, pPCHSGeometry);
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosGetLCHSGeometry} */
static DECLCALLBACK(int) drvStorageFltIMedia_BiosGetLCHSGeometry(PPDMIMEDIA pInterface, PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnBiosGetLCHSGeometry(pThis->pIMediaBelow, pLCHSGeometry);
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosSetLCHSGeometry} */
static DECLCALLBACK(int) drvStorageFltIMedia_BiosSetLCHSGeometry(PPDMIMEDIA pInterface, PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnBiosSetLCHSGeometry(pThis->pIMediaBelow, pLCHSGeometry);
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnGetUuid} */
static DECLCALLBACK(int) drvStorageFltIMedia_GetUuid(PPDMIMEDIA pInterface, PRTUUID pUuid)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnGetUuid(pThis->pIMediaBelow, pUuid);
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnDiscard} */
static DECLCALLBACK(int) drvStorageFltIMedia_Discard(PPDMIMEDIA pInterface, PCRTRANGE paRanges, unsigned cRanges)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMedia);
    int rc = pThis->pIMediaBelow->pfnDiscard(pThis->pIMediaBelow, paRanges, cRanges);
    return rc;
}


/*
 *
 * IMediaExPort Implementation.
 *
 */

/** @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCompleteNotify} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqCompleteNotify(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                                 void *pvIoReqAlloc, int rcReq)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaExPort);
    return pThis->pIMediaExPortAbove->pfnIoReqCompleteNotify(pThis->pIMediaExPortAbove, hIoReq, pvIoReqAlloc, rcReq);
}


/** @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyFromBuf} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqCopyFromBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                              void *pvIoReqAlloc, uint32_t offDst, PRTSGBUF pSgBuf,
                                                              size_t cbCopy)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaExPort);
    return pThis->pIMediaExPortAbove->pfnIoReqCopyFromBuf(pThis->pIMediaExPortAbove, hIoReq, pvIoReqAlloc,
                                                       offDst, pSgBuf, cbCopy);
}

/** @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyToBuf} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqCopyToBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                              void *pvIoReqAlloc, uint32_t offSrc, PRTSGBUF pSgBuf,
                                              size_t cbCopy)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaExPort);
    return pThis->pIMediaExPortAbove->pfnIoReqCopyToBuf(pThis->pIMediaExPortAbove, hIoReq, pvIoReqAlloc,
                                                     offSrc, pSgBuf, cbCopy);
}

/** @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqQueryDiscardRanges} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqQueryDiscardRanges(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                       void *pvIoReqAlloc, uint32_t idxRangeStart,
                                                       uint32_t cRanges, PRTRANGE paRanges,
                                                       uint32_t *pcRanges)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaExPort);
    return pThis->pIMediaExPortAbove->pfnIoReqQueryDiscardRanges(pThis->pIMediaExPortAbove, hIoReq, pvIoReqAlloc,
                                                              idxRangeStart, cRanges, paRanges, pcRanges);
}

/** @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqStateChanged} */
static DECLCALLBACK(void) drvStorageFltIMedia_IoReqStateChanged(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                                void *pvIoReqAlloc, PDMMEDIAEXIOREQSTATE enmState)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaExPort);
    return pThis->pIMediaExPortAbove->pfnIoReqStateChanged(pThis->pIMediaExPortAbove, hIoReq, pvIoReqAlloc, enmState);
}


/*
 *
 * IMediaEx Implementation.
 *
 */

/** @interface_method_impl{PDMIMEDIAEX,pfnQueryFeatures} */
static DECLCALLBACK(int) drvStorageFltIMedia_QueryFeatures(PPDMIMEDIAEX pInterface, uint32_t *pfFeatures)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);

    int rc = pThis->pIMediaExBelow->pfnQueryFeatures(pThis->pIMediaExBelow, pfFeatures);
    if (RT_SUCCESS(rc) && !pThis->fAsyncIOSupported)
        *pfFeatures &= ~PDMIMEDIAEX_FEATURE_F_ASYNC;

    return rc;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqAllocSizeSet} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqAllocSizeSet(PPDMIMEDIAEX pInterface, size_t cbIoReqAlloc)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqAllocSizeSet(pThis->pIMediaExBelow, cbIoReqAlloc);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqAlloc} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqAlloc(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq, void **ppvIoReqAlloc,
                                                        PDMMEDIAEXIOREQID uIoReqId, uint32_t fFlags)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);

    if (!pThis->fAsyncIOSupported)
        fFlags |= PDMIMEDIAEX_F_SYNC;
    return pThis->pIMediaExBelow->pfnIoReqAlloc(pThis->pIMediaExBelow, phIoReq, ppvIoReqAlloc, uIoReqId, fFlags);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqFree} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqFree(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqFree(pThis->pIMediaExBelow, hIoReq);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqQueryResidual} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqQueryResidual(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, size_t *pcbResidual)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqQueryResidual(pThis->pIMediaExBelow, hIoReq, pcbResidual);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancelAll} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqCancelAll(PPDMIMEDIAEX pInterface)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqCancelAll(pThis->pIMediaExBelow);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancel} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqCancel(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQID uIoReqId)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqCancel(pThis->pIMediaExBelow, uIoReqId);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqRead} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqRead(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbRead)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqRead(pThis->pIMediaExBelow, hIoReq, off, cbRead);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqWrite}
 */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqWrite(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbWrite)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqWrite(pThis->pIMediaExBelow, hIoReq, off, cbWrite);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqFlush} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqFlush(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqFlush(pThis->pIMediaExBelow, hIoReq);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqDiscard} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqDiscard(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, unsigned cRangesMax)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqDiscard(pThis->pIMediaExBelow, hIoReq, cRangesMax);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqSendScsiCmd} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqSendScsiCmd(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                              uint32_t uLun, const uint8_t *pbCdb, size_t cbCdb,
                                                              PDMMEDIAEXIOREQSCSITXDIR enmTxDir, PDMMEDIAEXIOREQSCSITXDIR *penmTxDirRet,
                                                              size_t cbBuf, uint8_t *pabSense, size_t cbSense, size_t *pcbSenseRet,
                                                              uint8_t *pu8ScsiSts, uint32_t cTimeoutMillies)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqSendScsiCmd(pThis->pIMediaExBelow, hIoReq, uLun, pbCdb, cbCdb,
                                                      enmTxDir, penmTxDirRet, cbBuf, pabSense, cbSense, pcbSenseRet,
                                                      pu8ScsiSts, cTimeoutMillies);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetActiveCount} */
static DECLCALLBACK(uint32_t) drvStorageFltIMedia_IoReqGetActiveCount(PPDMIMEDIAEX pInterface)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqGetActiveCount(pThis->pIMediaExBelow);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetSuspendedCount} */
static DECLCALLBACK(uint32_t) drvStorageFltIMedia_IoReqGetSuspendedCount(PPDMIMEDIAEX pInterface)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqGetSuspendedCount(pThis->pIMediaExBelow);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedFirst} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqQuerySuspendedStart(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq,
                                                                      void **ppvIoReqAlloc)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqQuerySuspendedStart(pThis->pIMediaExBelow, phIoReq, ppvIoReqAlloc);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedNext} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqQuerySuspendedNext(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                                     PPDMMEDIAEXIOREQ phIoReqNext, void **ppvIoReqAllocNext)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqQuerySuspendedNext(pThis->pIMediaExBelow, hIoReq, phIoReqNext, ppvIoReqAllocNext);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedSave} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqSuspendedSave(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqSuspendedSave(pThis->pIMediaExBelow, pSSM, hIoReq);
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedLoad} */
static DECLCALLBACK(int) drvStorageFltIMedia_IoReqSuspendedLoad(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    PDRVSTORAGEFILTER pThis = RT_FROM_MEMBER(pInterface, DRVSTORAGEFILTER, IMediaEx);
    return pThis->pIMediaExBelow->pfnIoReqSuspendedLoad(pThis->pIMediaExBelow, pSSM, hIoReq);
}


/*
 *
 * IBase Implementation.
 *
 */


static DECLCALLBACK(void *) drvStorageFltIBase_QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS          pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVSTORAGEFILTER   pThis   = PDMINS_2_DATA(pDrvIns, PDRVSTORAGEFILTER);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    if (pThis->pIMediaBelow)
        PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIA, &pThis->IMedia);
    if (pThis->pIMediaPortAbove)
        PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAPORT, &pThis->IMediaPort);
    if (pThis->pIMediaExBelow)
        PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEX, &pThis->IMediaEx);
    if (pThis->pIMediaExPortAbove)
        PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEXPORT, &pThis->IMediaExPort);

    return NULL;
}


/*
 *
 * PDMDRVREG Methods
 *
 */


/**
 * Construct a storage filter driver.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvStorageFlt_Construct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVSTORAGEFILTER   pThis = PDMINS_2_DATA(pDrvIns, PDRVSTORAGEFILTER);
    PCPDMDRVHLPR3       pHlp  = pDrvIns->pHlpR3;

    /*
     * Initialize the instance data.
     */
    pDrvIns->IBase.pfnQueryInterface     = drvStorageFltIBase_QueryInterface;

    pThis->IMedia.pfnRead                = drvStorageFltIMedia_Read;
    pThis->IMedia.pfnWrite               = drvStorageFltIMedia_Write;
    pThis->IMedia.pfnFlush               = drvStorageFltIMedia_Flush;
    pThis->IMedia.pfnMerge               = drvStorageFltIMedia_Merge;
    pThis->IMedia.pfnGetSize             = drvStorageFltIMedia_GetSize;
    pThis->IMedia.pfnIsReadOnly          = drvStorageFltIMedia_IsReadOnly;
    pThis->IMedia.pfnBiosGetPCHSGeometry = drvStorageFltIMedia_BiosGetPCHSGeometry;
    pThis->IMedia.pfnBiosSetPCHSGeometry = drvStorageFltIMedia_BiosSetPCHSGeometry;
    pThis->IMedia.pfnBiosGetLCHSGeometry = drvStorageFltIMedia_BiosGetLCHSGeometry;
    pThis->IMedia.pfnBiosSetLCHSGeometry = drvStorageFltIMedia_BiosSetLCHSGeometry;
    pThis->IMedia.pfnGetUuid             = drvStorageFltIMedia_GetUuid;
    pThis->IMedia.pfnDiscard             = drvStorageFltIMedia_Discard;

    pThis->IMediaEx.pfnQueryFeatures            = drvStorageFltIMedia_QueryFeatures;
    pThis->IMediaEx.pfnIoReqAllocSizeSet        = drvStorageFltIMedia_IoReqAllocSizeSet;
    pThis->IMediaEx.pfnIoReqAlloc               = drvStorageFltIMedia_IoReqAlloc;
    pThis->IMediaEx.pfnIoReqFree                = drvStorageFltIMedia_IoReqFree;
    pThis->IMediaEx.pfnIoReqQueryResidual       = drvStorageFltIMedia_IoReqQueryResidual;
    pThis->IMediaEx.pfnIoReqCancelAll           = drvStorageFltIMedia_IoReqCancelAll;
    pThis->IMediaEx.pfnIoReqCancel              = drvStorageFltIMedia_IoReqCancel;
    pThis->IMediaEx.pfnIoReqRead                = drvStorageFltIMedia_IoReqRead;
    pThis->IMediaEx.pfnIoReqWrite               = drvStorageFltIMedia_IoReqWrite;
    pThis->IMediaEx.pfnIoReqFlush               = drvStorageFltIMedia_IoReqFlush;
    pThis->IMediaEx.pfnIoReqDiscard             = drvStorageFltIMedia_IoReqDiscard;
    pThis->IMediaEx.pfnIoReqSendScsiCmd         = drvStorageFltIMedia_IoReqSendScsiCmd;
    pThis->IMediaEx.pfnIoReqGetActiveCount      = drvStorageFltIMedia_IoReqGetActiveCount;
    pThis->IMediaEx.pfnIoReqGetSuspendedCount   = drvStorageFltIMedia_IoReqGetSuspendedCount;
    pThis->IMediaEx.pfnIoReqQuerySuspendedStart = drvStorageFltIMedia_IoReqQuerySuspendedStart;
    pThis->IMediaEx.pfnIoReqQuerySuspendedNext  = drvStorageFltIMedia_IoReqQuerySuspendedNext;
    pThis->IMediaEx.pfnIoReqSuspendedSave       = drvStorageFltIMedia_IoReqSuspendedSave;
    pThis->IMediaEx.pfnIoReqSuspendedLoad       = drvStorageFltIMedia_IoReqSuspendedLoad;

    pThis->IMediaPort.pfnQueryDeviceLocation = drvStorageFltIMediaPort_QueryDeviceLocation;

    pThis->IMediaExPort.pfnIoReqCompleteNotify     = drvStorageFltIMedia_IoReqCompleteNotify;
    pThis->IMediaExPort.pfnIoReqCopyFromBuf        = drvStorageFltIMedia_IoReqCopyFromBuf;
    pThis->IMediaExPort.pfnIoReqCopyToBuf          = drvStorageFltIMedia_IoReqCopyToBuf;
    pThis->IMediaExPort.pfnIoReqQueryDiscardRanges = drvStorageFltIMedia_IoReqQueryDiscardRanges;
    pThis->IMediaExPort.pfnIoReqStateChanged       = drvStorageFltIMedia_IoReqStateChanged;

    /*
     * Validate and read config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "AsyncIOSupported|", "");

    int rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "AsyncIOSupported", &pThis->fAsyncIOSupported, true);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Query interfaces from the driver/device above us.
     */
    pThis->pIMediaPortAbove   = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAPORT);
    pThis->pIMediaExPortAbove = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAEXPORT);

    /*
     * Attach driver below us.
     */
    PPDMIBASE pIBaseBelow;
    rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pIBaseBelow);
    AssertLogRelRCReturn(rc, rc);

    pThis->pIMediaBelow   = PDMIBASE_QUERY_INTERFACE(pIBaseBelow, PDMIMEDIA);
    pThis->pIMediaExBelow = PDMIBASE_QUERY_INTERFACE(pIBaseBelow, PDMIMEDIAEX);

    AssertLogRelReturn(pThis->pIMediaBelow, VERR_PDM_MISSING_INTERFACE_BELOW);

    if (!pThis->pIMediaBelow->pfnDiscard)
        pThis->IMedia.pfnDiscard = NULL;

    return VINF_SUCCESS;
}


/**
 * Storage filter driver registration record.
 */
static const PDMDRVREG g_DrvStorageFilter =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "StorageFilter",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Storage Filter Driver Sample",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_MEDIA,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVSTORAGEFILTER),
    /* pfnConstruct */
    drvStorageFlt_Construct,
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


/**
 * Register builtin devices.
 *
 * @returns VBox status code.
 * @param   pCallbacks      Pointer to the callback table.
 * @param   u32Version      VBox version number.
 */
extern "C" DECLEXPORT(int) VBoxDriversRegister(PPDMDRVREGCB pCallbacks, uint32_t u32Version)
{
    LogFlow(("VBoxSampleDriver::VBoxDriversRegister: u32Version=%#x pCallbacks->u32Version=%#x\n",
             u32Version, pCallbacks->u32Version));

    AssertLogRelMsgReturn(u32Version >= VBOX_VERSION,
                          ("VirtualBox version %#x, expected %#x or higher\n", u32Version, VBOX_VERSION),
                          VERR_VERSION_MISMATCH);
    AssertLogRelMsgReturn(pCallbacks->u32Version == PDM_DRVREG_CB_VERSION,
                          ("callback version %#x, expected %#x\n", pCallbacks->u32Version, PDM_DRVREG_CB_VERSION),
                          VERR_VERSION_MISMATCH);

    return pCallbacks->pfnRegister(pCallbacks, &g_DrvStorageFilter);
}

