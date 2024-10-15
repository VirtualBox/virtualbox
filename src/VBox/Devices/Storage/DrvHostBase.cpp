/* $Id: DrvHostBase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvHostBase - Host base drive access driver.
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
#define LOG_GROUP LOG_GROUP_DRV_HOST_BASE

#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/uuid.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>

#include "DrvHostBase.h"




/* -=-=-=-=- IBlock -=-=-=-=- */

/** @interface_method_impl{PDMIMEDIA,pfnRead} */
static DECLCALLBACK(int) drvHostBaseRead(PPDMIMEDIA pInterface, uint64_t off, void *pvBuf, size_t cbRead)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    LogFlow(("%s-%d: drvHostBaseRead: off=%#llx pvBuf=%p cbRead=%#x (%s)\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, off, pvBuf, cbRead, pThis->pszDevice));
    RTCritSectEnter(&pThis->CritSect);

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsRead);

    /*
     * Check the state.
     */
    int rc;
    if (pThis->fMediaPresent)
    {
        /*
         * Seek and read.
         */
        rc = drvHostBaseReadOs(pThis, off, pvBuf, cbRead);
        if (RT_SUCCESS(rc))
        {
            Log2(("%s-%d: drvHostBaseReadOs: off=%#llx cbRead=%#x\n"
                  "%16.*Rhxd\n",
                  pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, off, cbRead, cbRead, pvBuf));
        }
        else
            Log(("%s-%d: drvHostBaseRead: drvHostBaseReadOs(%#llx, %p, %#x) -> %Rrc ('%s')\n",
                 pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance,
                 off, pvBuf, cbRead, rc, pThis->pszDevice));
    }
    else
        rc = VERR_MEDIA_NOT_PRESENT;

    if (RT_SUCCESS(rc))
    {
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
        STAM_REL_COUNTER_ADD(&pThis->StatBytesRead, cbRead);
    }
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseRead: returns %Rrc\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMIMEDIA,pfnWrite} */
static DECLCALLBACK(int) drvHostBaseWrite(PPDMIMEDIA pInterface, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    LogFlow(("%s-%d: drvHostBaseWrite: off=%#llx pvBuf=%p cbWrite=%#x (%s)\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, off, pvBuf, cbWrite, pThis->pszDevice));
    Log2(("%s-%d: drvHostBaseWrite: off=%#llx cbWrite=%#x\n"
          "%16.*Rhxd\n",
          pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, off, cbWrite, cbWrite, pvBuf));
    RTCritSectEnter(&pThis->CritSect);

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsWrite);

    /*
     * Check the state.
     */
    int rc;
    if (!pThis->fReadOnly)
    {
        if (pThis->fMediaPresent)
        {
            /*
             * Seek and write.
             */
            rc = drvHostBaseWriteOs(pThis, off, pvBuf, cbWrite);
            if (RT_FAILURE(rc))
                Log(("%s-%d: drvHostBaseWrite: drvHostBaseWriteOs(%#llx, %p, %#x) -> %Rrc ('%s')\n",
                     pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance,
                     off, pvBuf, cbWrite, rc, pThis->pszDevice));
        }
        else
            rc = VERR_MEDIA_NOT_PRESENT;
    }
    else
        rc = VERR_WRITE_PROTECT;

    if (RT_SUCCESS(rc))
    {
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
        STAM_REL_COUNTER_ADD(&pThis->StatBytesWritten, cbWrite);
    }
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseWrite: returns %Rrc\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMIMEDIA,pfnFlush} */
static DECLCALLBACK(int) drvHostBaseFlush(PPDMIMEDIA pInterface)
{
    int rc;
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    LogFlow(("%s-%d: drvHostBaseFlush: (%s)\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, pThis->pszDevice));
    RTCritSectEnter(&pThis->CritSect);

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsFlush);

    if (pThis->fMediaPresent)
        rc = drvHostBaseFlushOs(pThis);
    else
        rc = VERR_MEDIA_NOT_PRESENT;

    if (RT_SUCCESS(rc))
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseFlush: returns %Rrc\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMIMEDIA,pfnIsReadOnly} */
static DECLCALLBACK(bool) drvHostBaseIsReadOnly(PPDMIMEDIA pInterface)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    return pThis->fReadOnly;
}


/** @interface_method_impl{PDMIMEDIA,pfnIsNonRotational} */
static DECLCALLBACK(bool) drvHostBaseIsNonRotational(PPDMIMEDIA pInterface)
{
    RT_NOREF1(pInterface);
    return false;
}


/** @interface_method_impl{PDMIMEDIA,pfnGetSize} */
static DECLCALLBACK(uint64_t) drvHostBaseGetSize(PPDMIMEDIA pInterface)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    RTCritSectEnter(&pThis->CritSect);

    uint64_t cb = 0;
    if (pThis->fMediaPresent)
        cb = pThis->cbSize;

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseGetSize: returns %llu\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, cb));
    return cb;
}


/** @interface_method_impl{PDMIMEDIA,pfnGetType} */
static DECLCALLBACK(PDMMEDIATYPE) drvHostBaseGetType(PPDMIMEDIA pInterface)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    LogFlow(("%s-%d: drvHostBaseGetType: returns %d\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, pThis->enmType));
    return pThis->enmType;
}


/** @interface_method_impl{PDMIMEDIA,pfnGetUuid} */
static DECLCALLBACK(int) drvHostBaseGetUuid(PPDMIMEDIA pInterface, PRTUUID pUuid)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);

    *pUuid = pThis->Uuid;

    LogFlow(("%s-%d: drvHostBaseGetUuid: returns VINF_SUCCESS *pUuid=%RTuuid\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, pUuid));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMIMEDIA,pfnBiosGetPCHSGeometry} */
static DECLCALLBACK(int) drvHostBaseGetPCHSGeometry(PPDMIMEDIA pInterface, PPDMMEDIAGEOMETRY pPCHSGeometry)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fMediaPresent)
    {
        if (    pThis->PCHSGeometry.cCylinders > 0
            &&  pThis->PCHSGeometry.cHeads > 0
            &&  pThis->PCHSGeometry.cSectors > 0)
        {
            *pPCHSGeometry = pThis->PCHSGeometry;
        }
        else
            rc = VERR_PDM_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_PDM_MEDIA_NOT_MOUNTED;

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: %s: returns %Rrc CHS={%d,%d,%d}\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, __FUNCTION__, rc,
             pThis->PCHSGeometry.cCylinders, pThis->PCHSGeometry.cHeads, pThis->PCHSGeometry.cSectors));
    return rc;
}


/** @interface_method_impl{PDMIMEDIA,pfnBiosSetPCHSGeometry} */
static DECLCALLBACK(int) drvHostBaseSetPCHSGeometry(PPDMIMEDIA pInterface, PCPDMMEDIAGEOMETRY pPCHSGeometry)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    LogFlow(("%s-%d: %s: cCylinders=%d cHeads=%d cSectors=%d\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, __FUNCTION__,
             pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fMediaPresent)
    {
        pThis->PCHSGeometry = *pPCHSGeometry;
    }
    else
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        rc = VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/** @interface_method_impl{PDMIMEDIA,pfnBiosGetLCHSGeometry} */
static DECLCALLBACK(int) drvHostBaseGetLCHSGeometry(PPDMIMEDIA pInterface, PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fMediaPresent)
    {
        if (    pThis->LCHSGeometry.cCylinders > 0
            &&  pThis->LCHSGeometry.cHeads > 0
            &&  pThis->LCHSGeometry.cSectors > 0)
        {
            *pLCHSGeometry = pThis->LCHSGeometry;
        }
        else
            rc = VERR_PDM_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_PDM_MEDIA_NOT_MOUNTED;

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: %s: returns %Rrc CHS={%d,%d,%d}\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, __FUNCTION__, rc,
             pThis->LCHSGeometry.cCylinders, pThis->LCHSGeometry.cHeads, pThis->LCHSGeometry.cSectors));
    return rc;
}


/** @interface_method_impl{PDMIMEDIA,pfnBiosSetLCHSGeometry} */
static DECLCALLBACK(int) drvHostBaseSetLCHSGeometry(PPDMIMEDIA pInterface, PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    LogFlow(("%s-%d: %s: cCylinders=%d cHeads=%d cSectors=%d\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, __FUNCTION__,
             pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fMediaPresent)
    {
        pThis->LCHSGeometry = *pLCHSGeometry;
    }
    else
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        rc = VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/** @interface_method_impl{PDMIMEDIA,pfnBiosIsVisible} */
static DECLCALLBACK(bool) drvHostBaseIsVisible(PPDMIMEDIA pInterface)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    return pThis->fBiosVisible;
}


/** @interface_method_impl{PDMIMEDIA,pfnGetRegionCount} */
static DECLCALLBACK(uint32_t) drvHostBaseGetRegionCount(PPDMIMEDIA pInterface)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);

    LogFlowFunc(("\n"));
    uint32_t cRegions = pThis->fMediaPresent ? 1 : 0;

    /* For now just return one region for all devices. */
    /** @todo Handle CD/DVD passthrough properly. */

    LogFlowFunc(("returns %u\n", cRegions));
    return cRegions;
}

/** @interface_method_impl{PDMIMEDIA,pfnQueryRegionProperties} */
static DECLCALLBACK(int) drvHostBaseQueryRegionProperties(PPDMIMEDIA pInterface, uint32_t uRegion, uint64_t *pu64LbaStart,
                                                          uint64_t *pcBlocks, uint64_t *pcbBlock,
                                                          PVDREGIONDATAFORM penmDataForm)
{
    LogFlowFunc(("\n"));
    int rc = VINF_SUCCESS;
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);

    if (uRegion < 1 && pThis->fMediaPresent)
    {
        uint64_t cbMedia;
        rc = drvHostBaseGetMediaSizeOs(pThis, &cbMedia);
        if (RT_SUCCESS(rc))
        {
            uint64_t cbBlock = 0;

            if (pThis->enmType == PDMMEDIATYPE_DVD)
                cbBlock = 2048;
            else
                cbBlock = 512; /* Floppy. */

            if (pu64LbaStart)
                *pu64LbaStart = 0;
            if (pcBlocks)
                *pcBlocks = cbMedia / cbBlock;
            if (pcbBlock)
                *pcbBlock = cbBlock;
            if (penmDataForm)
                *penmDataForm = VDREGIONDATAFORM_RAW;
        }
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnQueryRegionPropertiesForLba} */
static DECLCALLBACK(int) drvHostBaseQueryRegionPropertiesForLba(PPDMIMEDIA pInterface, uint64_t u64LbaStart,
                                                                uint32_t *puRegion, uint64_t *pcBlocks,
                                                                uint64_t *pcbBlock, PVDREGIONDATAFORM penmDataForm)
{
    LogFlowFunc(("\n"));
    int rc = VINF_SUCCESS;
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    uint64_t cbMedia;
    uint64_t cbBlock = 0;

    if (pThis->enmType == PDMMEDIATYPE_DVD)
        cbBlock = 2048;
    else
        cbBlock = 512; /* Floppy. */

    rc = drvHostBaseGetMediaSizeOs(pThis, &cbMedia);
    if (   RT_SUCCESS(rc)
        && u64LbaStart < cbMedia / cbBlock)
    {
        if (puRegion)
            *puRegion = 0;
        if (pcBlocks)
            *pcBlocks = cbMedia / cbBlock;
        if (pcbBlock)
            *pcbBlock = cbBlock;
        if (penmDataForm)
            *penmDataForm = VDREGIONDATAFORM_RAW;
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}



/* -=-=-=-=- IMediaEx -=-=-=-=- */

DECLHIDDEN(int) drvHostBaseBufferRetain(PDRVHOSTBASE pThis, PDRVHOSTBASEREQ pReq, size_t cbBuf, bool fWrite, void **ppvBuf)
{
    int rc = VINF_SUCCESS;

    if (pThis->cbBuf < cbBuf)
    {
        RTMemFree(pThis->pvBuf);
        pThis->cbBuf = 0;
        pThis->pvBuf = RTMemAlloc(cbBuf);
        if (pThis->pvBuf)
            pThis->cbBuf = cbBuf;
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc) && fWrite)
    {
        RTSGSEG Seg;
        RTSGBUF SgBuf;

        Seg.pvSeg = pThis->pvBuf;
        Seg.cbSeg = cbBuf;
        RTSgBufInit(&SgBuf, &Seg, 1);
        rc = pThis->pDrvMediaExPort->pfnIoReqCopyToBuf(pThis->pDrvMediaExPort, (PDMMEDIAEXIOREQ)pReq,
                                                       &pReq->abAlloc[0], 0, &SgBuf, cbBuf);
    }

    if (RT_SUCCESS(rc))
        *ppvBuf = pThis->pvBuf;

    return rc;
}

DECLHIDDEN(int) drvHostBaseBufferRelease(PDRVHOSTBASE pThis, PDRVHOSTBASEREQ pReq, size_t cbBuf, bool fWrite, void *pvBuf)
{
    int rc = VINF_SUCCESS;

    if (!fWrite)
    {
        RTSGSEG Seg;
        RTSGBUF SgBuf;

        Seg.pvSeg = pvBuf;
        Seg.cbSeg = cbBuf;
        RTSgBufInit(&SgBuf, &Seg, 1);
        rc = pThis->pDrvMediaExPort->pfnIoReqCopyFromBuf(pThis->pDrvMediaExPort, (PDMMEDIAEXIOREQ)pReq,
                                                         &pReq->abAlloc[0], 0, &SgBuf, cbBuf);
    }

    return rc;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnQueryFeatures} */
static DECLCALLBACK(int) drvHostBaseQueryFeatures(PPDMIMEDIAEX pInterface, uint32_t *pfFeatures)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMediaEx);

    *pfFeatures = pThis->IMediaEx.pfnIoReqSendScsiCmd ? PDMIMEDIAEX_FEATURE_F_RAWSCSICMD : 0;
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnNotifySuspend} */
static DECLCALLBACK(void) drvHostBaseNotifySuspend(PPDMIMEDIAEX pInterface)
{
    RT_NOREF(pInterface); /* Nothing to do here. */
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqAllocSizeSet} */
static DECLCALLBACK(int) drvHostBaseIoReqAllocSizeSet(PPDMIMEDIAEX pInterface, size_t cbIoReqAlloc)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMediaEx);

    pThis->cbIoReqAlloc = RT_UOFFSETOF_DYN(DRVHOSTBASEREQ, abAlloc[cbIoReqAlloc]);
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqAlloc} */
static DECLCALLBACK(int) drvHostBaseIoReqAlloc(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq, void **ppvIoReqAlloc,
                                               PDMMEDIAEXIOREQID uIoReqId, uint32_t fFlags)
{
    RT_NOREF2(uIoReqId, fFlags);

    int rc = VINF_SUCCESS;
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMediaEx);
    PDRVHOSTBASEREQ pReq = (PDRVHOSTBASEREQ)RTMemAllocZ(pThis->cbIoReqAlloc);
    if (RT_LIKELY(pReq))
    {
        pReq->cbReq      = 0;
        pReq->cbResidual = 0;
        *phIoReq = (PDMMEDIAEXIOREQ)pReq;
        *ppvIoReqAlloc = &pReq->abAlloc[0];
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqFree} */
static DECLCALLBACK(int) drvHostBaseIoReqFree(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    RT_NOREF1(pInterface);
    PDRVHOSTBASEREQ pReq = (PDRVHOSTBASEREQ)hIoReq;

    RTMemFree(pReq);
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqQueryResidual} */
static DECLCALLBACK(int) drvHostBaseIoReqQueryResidual(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, size_t *pcbResidual)
{
    RT_NOREF1(pInterface);
    PDRVHOSTBASEREQ pReq = (PDRVHOSTBASEREQ)hIoReq;

    *pcbResidual = pReq->cbResidual;
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqQueryXferSize} */
static DECLCALLBACK(int) drvHostBaseIoReqQueryXferSize(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, size_t *pcbXfer)
{
    RT_NOREF1(pInterface);
    PDRVHOSTBASEREQ pReq = (PDRVHOSTBASEREQ)hIoReq;

    *pcbXfer = pReq->cbReq;
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancelAll} */
static DECLCALLBACK(int) drvHostBaseIoReqCancelAll(PPDMIMEDIAEX pInterface)
{
    RT_NOREF1(pInterface);
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancel} */
static DECLCALLBACK(int) drvHostBaseIoReqCancel(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQID uIoReqId)
{
    RT_NOREF2(pInterface, uIoReqId);
    return VERR_PDM_MEDIAEX_IOREQID_NOT_FOUND;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqRead} */
static DECLCALLBACK(int) drvHostBaseIoReqRead(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbRead)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMediaEx);
    PDRVHOSTBASEREQ pReq = (PDRVHOSTBASEREQ)hIoReq;
    LogFlow(("%s-%d: drvHostBaseIoReqRead: off=%#llx cbRead=%#x (%s)\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, off, cbRead, pThis->pszDevice));
    RTCritSectEnter(&pThis->CritSect);

    pReq->cbReq = cbRead;
    pReq->cbResidual = cbRead;

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsRead);

    /*
     * Check the state.
     */
    int rc;
    if (pThis->fMediaPresent)
    {
        void *pvBuf;
        rc = drvHostBaseBufferRetain(pThis, pReq, cbRead, false, &pvBuf);
        if (RT_SUCCESS(rc))
        {
            /*
             * Seek and read.
             */
            rc = drvHostBaseReadOs(pThis, off, pvBuf, cbRead);
            if (RT_SUCCESS(rc))
            {
                Log2(("%s-%d: drvHostBaseReadOs: off=%#llx cbRead=%#x\n"
                      "%16.*Rhxd\n",
                      pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, off, cbRead, cbRead, pvBuf));

                pReq->cbResidual = 0;
            }
            else
                Log(("%s-%d: drvHostBaseIoReqRead: drvHostBaseReadOs(%#llx, %p, %#x) -> %Rrc ('%s')\n",
                     pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance,
                     off, pvBuf, cbRead, rc, pThis->pszDevice));

            rc = drvHostBaseBufferRelease(pThis, pReq, cbRead, false, pvBuf);
        }
        else
            Log(("%s-%d: drvHostBaseIoReqRead: drvHostBaseBufferRetain(%#llx, %p, %#x) -> %Rrc ('%s')\n",
                 pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance,
                 off, pvBuf, cbRead, rc, pThis->pszDevice));
    }
    else
        rc = VERR_MEDIA_NOT_PRESENT;

    if (RT_SUCCESS(rc))
    {
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
        STAM_REL_COUNTER_INC(&pThis->StatBytesRead);
    }
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseIoReqRead: returns %Rrc\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqWrite} */
static DECLCALLBACK(int) drvHostBaseIoReqWrite(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbWrite)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMediaEx);
    PDRVHOSTBASEREQ pReq = (PDRVHOSTBASEREQ)hIoReq;
    LogFlow(("%s-%d: drvHostBaseIoReqWrite: off=%#llx cbWrite=%#x (%s)\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, off, cbWrite, pThis->pszDevice));
    RTCritSectEnter(&pThis->CritSect);

    pReq->cbReq = cbWrite;
    pReq->cbResidual = cbWrite;

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsWrite);

    /*
     * Check the state.
     */
    int rc;
    if (!pThis->fReadOnly)
    {
        if (pThis->fMediaPresent)
        {
            void *pvBuf;
            rc = drvHostBaseBufferRetain(pThis, pReq, cbWrite, true, &pvBuf);
            if (RT_SUCCESS(rc))
            {
                Log2(("%s-%d: drvHostBaseIoReqWrite: off=%#llx cbWrite=%#x\n"
                      "%16.*Rhxd\n",
                      pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, off, cbWrite, cbWrite, pvBuf));
                /*
                 * Seek and write.
                 */
                rc = drvHostBaseWriteOs(pThis, off, pvBuf, cbWrite);
                if (RT_FAILURE(rc))
                    Log(("%s-%d: drvHostBaseIoReqWrite: drvHostBaseWriteOs(%#llx, %p, %#x) -> %Rrc ('%s')\n",
                         pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance,
                         off, pvBuf, cbWrite, rc, pThis->pszDevice));
                else
                    pReq->cbResidual = 0;

                rc = drvHostBaseBufferRelease(pThis, pReq, cbWrite, true, pvBuf);
            }
        }
        else
            rc = VERR_MEDIA_NOT_PRESENT;
    }
    else
        rc = VERR_WRITE_PROTECT;

    if (RT_SUCCESS(rc))
    {
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
        STAM_REL_COUNTER_INC(&pThis->StatBytesWritten);
    }
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseIoReqWrite: returns %Rrc\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqFlush} */
static DECLCALLBACK(int) drvHostBaseIoReqFlush(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    RT_NOREF1(hIoReq);

    int rc;
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMediaEx);
    LogFlow(("%s-%d: drvHostBaseIoReqFlush: (%s)\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, pThis->pszDevice));
    RTCritSectEnter(&pThis->CritSect);

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsFlush);

    if (pThis->fMediaPresent)
        rc = drvHostBaseFlushOs(pThis);
    else
        rc = VERR_MEDIA_NOT_PRESENT;

    if (RT_SUCCESS(rc))
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseFlush: returns %Rrc\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqDiscard} */
static DECLCALLBACK(int) drvHostBaseIoReqDiscard(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, unsigned cRangesMax)
{
    RT_NOREF3(pInterface, hIoReq, cRangesMax);
    return VERR_NOT_SUPPORTED;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetActiveCount} */
static DECLCALLBACK(uint32_t) drvHostBaseIoReqGetActiveCount(PPDMIMEDIAEX pInterface)
{
    RT_NOREF1(pInterface);
    return 0;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetSuspendedCount} */
static DECLCALLBACK(uint32_t) drvHostBaseIoReqGetSuspendedCount(PPDMIMEDIAEX pInterface)
{
    RT_NOREF1(pInterface);
    return 0;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedStart} */
static DECLCALLBACK(int) drvHostBaseIoReqQuerySuspendedStart(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq, void **ppvIoReqAlloc)
{
    RT_NOREF3(pInterface, phIoReq, ppvIoReqAlloc);
    return VERR_NOT_IMPLEMENTED;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedNext} */
static DECLCALLBACK(int) drvHostBaseIoReqQuerySuspendedNext(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                            PPDMMEDIAEXIOREQ phIoReqNext, void **ppvIoReqAllocNext)
{
    RT_NOREF4(pInterface, hIoReq, phIoReqNext, ppvIoReqAllocNext);
    return VERR_NOT_IMPLEMENTED;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedSave} */
static DECLCALLBACK(int) drvHostBaseIoReqSuspendedSave(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    RT_NOREF3(pInterface, pSSM, hIoReq);
    return VERR_NOT_IMPLEMENTED;
}

/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedLoad} */
static DECLCALLBACK(int) drvHostBaseIoReqSuspendedLoad(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    RT_NOREF3(pInterface, pSSM, hIoReq);
    return VERR_NOT_IMPLEMENTED;
}



/* -=-=-=-=- IMount -=-=-=-=- */

/** @interface_method_impl{PDMIMOUNT,pfnUnmount} */
static DECLCALLBACK(int) drvHostBaseUnmount(PPDMIMOUNT pInterface, bool fForce, bool fEject)
{
    RT_NOREF(fEject);
    /* While we're not mountable (see drvHostBaseMount), we're unmountable. */
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMount);
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Validate state.
     */
    int rc = VINF_SUCCESS;
    if (!pThis->fLocked || fForce)
    {
        /* Unlock drive if necessary. */
        if (pThis->fLocked)
        {
            if (pThis->pfnDoLock)
                rc = pThis->pfnDoLock(pThis, false);
            if (RT_SUCCESS(rc))
                pThis->fLocked = false;
        }

        if (fEject)
        {
            /*
             * Eject the disc.
             */
            rc = drvHostBaseEjectOs(pThis);
        }

        /*
         * Media is no longer present.
         */
        DRVHostBaseMediaNotPresent(pThis);
    }
    else
    {
        Log(("drvHostBaseUnmount: Locked\n"));
        rc = VERR_PDM_MEDIA_LOCKED;
    }

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("drvHostBaseUnmount: returns %Rrc\n", rc));
    return rc;
}


/** @interface_method_impl{PDMIMOUNT,pfnIsMounted} */
static DECLCALLBACK(bool) drvHostBaseIsMounted(PPDMIMOUNT pInterface)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMount);
    RTCritSectEnter(&pThis->CritSect);

    bool fRc = pThis->fMediaPresent;

    RTCritSectLeave(&pThis->CritSect);
    return fRc;
}


/** @interface_method_impl{PDMIMOUNT,pfnIsLocked} */
static DECLCALLBACK(int) drvHostBaseLock(PPDMIMOUNT pInterface)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMount);
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (!pThis->fLocked)
    {
        if (pThis->pfnDoLock)
        {
            rc = pThis->pfnDoLock(pThis, true);
            if (RT_SUCCESS(rc))
                pThis->fLocked = true;
        }
    }
    else
        LogFlow(("%s-%d: drvHostBaseLock: already locked\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseLock: returns %Rrc\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMIMOUNT,pfnIsLocked} */
static DECLCALLBACK(int) drvHostBaseUnlock(PPDMIMOUNT pInterface)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMount);
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fLocked)
    {
        if (pThis->pfnDoLock)
            rc = pThis->pfnDoLock(pThis, false);
        if (RT_SUCCESS(rc))
            pThis->fLocked = false;
    }
    else
        LogFlow(("%s-%d: drvHostBaseUnlock: not locked\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseUnlock: returns %Rrc\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMIMOUNT,pfnIsLocked} */
static DECLCALLBACK(bool) drvHostBaseIsLocked(PPDMIMOUNT pInterface)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMount);
    RTCritSectEnter(&pThis->CritSect);

    bool fRc = pThis->fLocked;

    RTCritSectLeave(&pThis->CritSect);
    return fRc;
}


/* -=-=-=-=- IBase -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *)  drvHostBaseQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS   pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHOSTBASE pThis   = PDMINS_2_DATA(pDrvIns, PDRVHOSTBASE);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIA, &pThis->IMedia);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUNT, &pThis->IMount);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEX, pThis->pDrvMediaExPort ? &pThis->IMediaEx : NULL);
    return NULL;
}


/* -=-=-=-=- poller thread -=-=-=-=- */


/**
 * Media present.
 * Query the size and notify the above driver / device.
 *
 * @param   pThis   The instance data.
 */
DECLHIDDEN(int) DRVHostBaseMediaPresent(PDRVHOSTBASE pThis)
{
    /*
     * Open the drive.
     */
    int rc = drvHostBaseMediaRefreshOs(pThis);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Determine the size.
     */
    uint64_t cb;
    rc = drvHostBaseGetMediaSizeOs(pThis, &cb);
    if (RT_FAILURE(rc))
    {
        LogFlow(("%s-%d: failed to figure media size of %s, rc=%Rrc\n",
                 pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, pThis->pszDevice, rc));
        return rc;
    }

    /*
     * Update the data and inform the unit.
     */
    pThis->cbSize = cb;
    pThis->fMediaPresent = true;
    if (pThis->pDrvMountNotify)
        pThis->pDrvMountNotify->pfnMountNotify(pThis->pDrvMountNotify);
    LogFlow(("%s-%d: drvHostBaseMediaPresent: cbSize=%lld (%#llx)\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, pThis->cbSize, pThis->cbSize));
    return VINF_SUCCESS;
}


/**
 * Media no longer present.
 * @param   pThis   The instance data.
 */
DECLHIDDEN(void) DRVHostBaseMediaNotPresent(PDRVHOSTBASE pThis)
{
    pThis->fMediaPresent = false;
    pThis->fLocked = false;
    pThis->PCHSGeometry.cCylinders = 0;
    pThis->PCHSGeometry.cHeads = 0;
    pThis->PCHSGeometry.cSectors = 0;
    pThis->LCHSGeometry.cCylinders = 0;
    pThis->LCHSGeometry.cHeads = 0;
    pThis->LCHSGeometry.cSectors = 0;
    if (pThis->pDrvMountNotify)
        pThis->pDrvMountNotify->pfnUnmountNotify(pThis->pDrvMountNotify);
}


static int drvHostBaseMediaPoll(PDRVHOSTBASE pThis)
{
    /*
     * Poll for media change.
     */
    bool fMediaPresent = false;
    bool fMediaChanged = false;
    drvHostBaseQueryMediaStatusOs(pThis, &fMediaChanged, &fMediaPresent);

    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fMediaPresent != fMediaPresent)
    {
        LogFlow(("drvHostDvdPoll: %d -> %d\n", pThis->fMediaPresent, fMediaPresent));
        pThis->fMediaPresent = false;
        if (fMediaPresent)
            rc = DRVHostBaseMediaPresent(pThis);
        else
            DRVHostBaseMediaNotPresent(pThis);
    }
    else if (fMediaPresent)
    {
        /*
         * Poll for media change.
         */
        if (fMediaChanged)
        {
            LogFlow(("drvHostDVDMediaThread: Media changed!\n"));
            DRVHostBaseMediaNotPresent(pThis);
            rc = DRVHostBaseMediaPresent(pThis);
        }
    }

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/**
 * This thread will periodically poll the device for media presence.
 *
 * @returns Ignored.
 * @param   ThreadSelf  Handle of this thread. Ignored.
 * @param   pvUser      Pointer to the driver instance structure.
 */
static DECLCALLBACK(int) drvHostBaseMediaThread(RTTHREAD ThreadSelf, void *pvUser)
{
    PDRVHOSTBASE pThis = (PDRVHOSTBASE)pvUser;
    LogFlow(("%s-%d: drvHostBaseMediaThread: ThreadSelf=%p pvUser=%p\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, ThreadSelf, pvUser));
    bool        fFirst = true;
    int         cRetries = 10;
    while (!pThis->fShutdownPoller)
    {
        /*
         * Perform the polling (unless we've run out of 50ms retries).
         */
        if (cRetries-- > 0)
        {

            int rc = drvHostBaseMediaPoll(pThis);
            if (RT_FAILURE(rc))
            {
                RTSemEventWait(pThis->EventPoller, 50);
                continue;
            }
        }

        /*
         * Signal EMT after the first go.
         */
        if (fFirst)
        {
            RTThreadUserSignal(ThreadSelf);
            fFirst = false;
        }

        /*
         * Sleep.
         */
        int rc = RTSemEventWait(pThis->EventPoller, pThis->cMilliesPoller);
        if (    RT_FAILURE(rc)
            &&  rc != VERR_TIMEOUT)
        {
            AssertMsgFailed(("rc=%Rrc\n", rc));
            pThis->ThreadPoller = NIL_RTTHREAD;
            LogFlow(("drvHostBaseMediaThread: returns %Rrc\n", rc));
            return rc;
        }
        cRetries = 10;
    }

    /* (Don't clear the thread handle here, the destructor thread is using it to wait.) */
    LogFlow(("%s-%d: drvHostBaseMediaThread: returns VINF_SUCCESS\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));
    return VINF_SUCCESS;
}

/**
 * Registers statistics associated with the given media driver.
 *
 * @returns VBox status code.
 * @param   pThis      The media driver instance.
 */
static int drvHostBaseStatsRegister(PDRVHOSTBASE pThis)
{
    PPDMDRVINS pDrvIns = pThis->pDrvIns;
    uint32_t iInstance, iLUN;
    const char *pcszController;

    int rc = pThis->pDrvMediaPort->pfnQueryDeviceLocation(pThis->pDrvMediaPort, &pcszController,
                                                          &iInstance, &iLUN);
    if (RT_SUCCESS(rc))
    {
        char *pszCtrlUpper = RTStrDup(pcszController);
        if (pszCtrlUpper)
        {
            RTStrToUpper(pszCtrlUpper);

            PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatBytesRead, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                                   "Amount of data read.", "/Devices/%s%u/Port%u/ReadBytes", pszCtrlUpper, iInstance, iLUN);
            PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatBytesWritten, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                                   "Amount of data written.", "/Devices/%s%u/Port%u/WrittenBytes", pszCtrlUpper, iInstance, iLUN);

            PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsSubmitted, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                                   "Number of I/O requests submitted.", "/Devices/%s%u/Port%u/ReqsSubmitted", pszCtrlUpper, iInstance, iLUN);
            PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsFailed, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                                   "Number of I/O requests failed.", "/Devices/%s%u/Port%u/ReqsFailed", pszCtrlUpper, iInstance, iLUN);
            PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsSucceeded, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                                   "Number of I/O requests succeeded.", "/Devices/%s%u/Port%u/ReqsSucceeded", pszCtrlUpper, iInstance, iLUN);
            PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsFlush, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                                   "Number of flush I/O requests submitted.", "/Devices/%s%u/Port%u/ReqsFlush", pszCtrlUpper, iInstance, iLUN);
            PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsWrite, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                                   "Number of write I/O requests submitted.", "/Devices/%s%u/Port%u/ReqsWrite", pszCtrlUpper, iInstance, iLUN);
            PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsRead, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                                   "Number of read I/O requests submitted.", "/Devices/%s%u/Port%u/ReqsRead", pszCtrlUpper, iInstance, iLUN);

            RTStrFree(pszCtrlUpper);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }

    return rc;
}

/**
 * Deregisters statistics associated with the given media driver.
 *
 * @param   pThis      The media driver instance.
 */
static void drvhostBaseStatsDeregister(PDRVHOSTBASE pThis)
{
    PPDMDRVINS pDrvIns = pThis->pDrvIns;

    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatBytesRead);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatBytesWritten);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsSubmitted);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsFailed);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsSucceeded);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsFlush);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsWrite);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsRead);
}

/* -=-=-=-=- driver interface -=-=-=-=- */


/**
 * Done state load operation.
 *
 * @returns VBox load code.
 * @param   pDrvIns         Driver instance of the driver which registered the data unit.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) drvHostBaseLoadDone(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    PDRVHOSTBASE pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTBASE);
    LogFlow(("%s-%d: drvHostBaseMediaThread:\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Tell the device/driver above us that the media status is uncertain.
     */
    if (pThis->pDrvMountNotify)
    {
        pThis->pDrvMountNotify->pfnUnmountNotify(pThis->pDrvMountNotify);
        if (pThis->fMediaPresent)
            pThis->pDrvMountNotify->pfnMountNotify(pThis->pDrvMountNotify);
    }

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/** @copydoc FNPDMDRVDESTRUCT */
DECLCALLBACK(void) DRVHostBaseDestruct(PPDMDRVINS pDrvIns)
{
    PDRVHOSTBASE pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTBASE);
    LogFlow(("%s-%d: drvHostBaseDestruct: iInstance=%d\n", pDrvIns->pReg->szName, pDrvIns->iInstance, pDrvIns->iInstance));

    /*
     * Terminate the thread.
     */
    if (pThis->ThreadPoller != NIL_RTTHREAD)
    {
        pThis->fShutdownPoller = true;
        int rc;
        int cTimes = 50;
        do
        {
            RTSemEventSignal(pThis->EventPoller);
            rc = RTThreadWait(pThis->ThreadPoller, 100, NULL);
        } while (cTimes-- > 0 && rc == VERR_TIMEOUT);

        if (!rc)
            pThis->ThreadPoller = NIL_RTTHREAD;
    }

    /*
     * Cleanup the other resources.
     */
    drvHostBaseDestructOs(pThis);

    if (pThis->EventPoller != NULL)
    {
        RTSemEventDestroy(pThis->EventPoller);
        pThis->EventPoller = NULL;
    }

    if (pThis->pszDevice)
    {
        PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszDevice);
        pThis->pszDevice = NULL;
    }

    if (pThis->pszDeviceOpen)
    {
        RTStrFree(pThis->pszDeviceOpen);
        pThis->pszDeviceOpen = NULL;
    }

    if (pThis->pvBuf)
    {
        RTMemFree(pThis->pvBuf);
        pThis->pvBuf = NULL;
        pThis->cbBuf = 0;
    }

    /* Forget about the notifications. */
    pThis->pDrvMountNotify = NULL;

    drvhostBaseStatsDeregister(pThis);

    /* Leave the instance operational if this is just a cleanup of the state
     * after an attach error happened. So don't destroy the critsect then. */
    if (!pThis->fKeepInstance && RTCritSectIsInitialized(&pThis->CritSect))
        RTCritSectDelete(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseDestruct completed\n", pDrvIns->pReg->szName, pDrvIns->iInstance));
}


/**
 * Initializes the instance data .
 *
 * On failure call DRVHostBaseDestruct().
 *
 * @returns VBox status code.
 * @param   pDrvIns         Driver instance.
 * @param   pszCfgValid     Pointer to a string of valid CFGM options.
 * @param   pCfg            Configuration handle.
 * @param   enmType         Device type.
 */
DECLHIDDEN(int) DRVHostBaseInit(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, const char *pszCfgValid, PDMMEDIATYPE enmType)
{
    int src = VINF_SUCCESS;
    PDRVHOSTBASE    pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTBASE);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    LogFlow(("%s-%d: DRVHostBaseInit: iInstance=%d\n", pDrvIns->pReg->szName, pDrvIns->iInstance, pDrvIns->iInstance));

    /*
     * Initialize most of the data members.
     */
    pThis->pDrvIns                          = pDrvIns;
    pThis->fKeepInstance                    = false;
    pThis->ThreadPoller                     = NIL_RTTHREAD;
    pThis->enmType                          = enmType;
    pThis->fAttachFailError                 = true; /* It's an error until we've read the config. */

    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface        = drvHostBaseQueryInterface;

    /* IMedia. */
    pThis->IMedia.pfnRead                        = drvHostBaseRead;
    pThis->IMedia.pfnWrite                       = drvHostBaseWrite;
    pThis->IMedia.pfnFlush                       = drvHostBaseFlush;
    pThis->IMedia.pfnIsReadOnly                  = drvHostBaseIsReadOnly;
    pThis->IMedia.pfnIsNonRotational             = drvHostBaseIsNonRotational;
    pThis->IMedia.pfnGetSize                     = drvHostBaseGetSize;
    pThis->IMedia.pfnGetType                     = drvHostBaseGetType;
    pThis->IMedia.pfnGetUuid                     = drvHostBaseGetUuid;
    pThis->IMedia.pfnBiosGetPCHSGeometry         = drvHostBaseGetPCHSGeometry;
    pThis->IMedia.pfnBiosSetPCHSGeometry         = drvHostBaseSetPCHSGeometry;
    pThis->IMedia.pfnBiosGetLCHSGeometry         = drvHostBaseGetLCHSGeometry;
    pThis->IMedia.pfnBiosSetLCHSGeometry         = drvHostBaseSetLCHSGeometry;
    pThis->IMedia.pfnBiosIsVisible               = drvHostBaseIsVisible;
    pThis->IMedia.pfnGetRegionCount              = drvHostBaseGetRegionCount;
    pThis->IMedia.pfnQueryRegionProperties       = drvHostBaseQueryRegionProperties;
    pThis->IMedia.pfnQueryRegionPropertiesForLba = drvHostBaseQueryRegionPropertiesForLba;

    /* IMediaEx */
    pThis->IMediaEx.pfnQueryFeatures            = drvHostBaseQueryFeatures;
    pThis->IMediaEx.pfnNotifySuspend            = drvHostBaseNotifySuspend;
    pThis->IMediaEx.pfnIoReqAllocSizeSet        = drvHostBaseIoReqAllocSizeSet;
    pThis->IMediaEx.pfnIoReqAlloc               = drvHostBaseIoReqAlloc;
    pThis->IMediaEx.pfnIoReqFree                = drvHostBaseIoReqFree;
    pThis->IMediaEx.pfnIoReqQueryResidual       = drvHostBaseIoReqQueryResidual;
    pThis->IMediaEx.pfnIoReqQueryXferSize       = drvHostBaseIoReqQueryXferSize;
    pThis->IMediaEx.pfnIoReqCancelAll           = drvHostBaseIoReqCancelAll;
    pThis->IMediaEx.pfnIoReqCancel              = drvHostBaseIoReqCancel;
    pThis->IMediaEx.pfnIoReqRead                = drvHostBaseIoReqRead;
    pThis->IMediaEx.pfnIoReqWrite               = drvHostBaseIoReqWrite;
    pThis->IMediaEx.pfnIoReqFlush               = drvHostBaseIoReqFlush;
    pThis->IMediaEx.pfnIoReqDiscard             = drvHostBaseIoReqDiscard;
    pThis->IMediaEx.pfnIoReqGetActiveCount      = drvHostBaseIoReqGetActiveCount;
    pThis->IMediaEx.pfnIoReqGetSuspendedCount   = drvHostBaseIoReqGetSuspendedCount;
    pThis->IMediaEx.pfnIoReqQuerySuspendedStart = drvHostBaseIoReqQuerySuspendedStart;
    pThis->IMediaEx.pfnIoReqQuerySuspendedNext  = drvHostBaseIoReqQuerySuspendedNext;
    pThis->IMediaEx.pfnIoReqSuspendedSave       = drvHostBaseIoReqSuspendedSave;
    pThis->IMediaEx.pfnIoReqSuspendedLoad       = drvHostBaseIoReqSuspendedLoad;

    /* IMount. */
    pThis->IMount.pfnUnmount                = drvHostBaseUnmount;
    pThis->IMount.pfnIsMounted              = drvHostBaseIsMounted;
    pThis->IMount.pfnLock                   = drvHostBaseLock;
    pThis->IMount.pfnUnlock                 = drvHostBaseUnlock;
    pThis->IMount.pfnIsLocked               = drvHostBaseIsLocked;

    drvHostBaseInitOs(pThis);

    if (!pHlp->pfnCFGMAreValuesValid(pCfg, pszCfgValid))
    {
        pThis->fAttachFailError = true;
        return VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES;
    }

    /*
     * Get the IMediaPort & IMountNotify interfaces of the above driver/device.
     */
    pThis->pDrvMediaPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAPORT);
    if (!pThis->pDrvMediaPort)
    {
        AssertMsgFailed(("Configuration error: No media port interface above!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }
    pThis->pDrvMediaExPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAEXPORT);
    pThis->pDrvMountNotify = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMOUNTNOTIFY);

    /*
     * Query configuration.
     */
    /* Device */
    int rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "Path", &pThis->pszDevice);
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: query for \"Path\" string returned %Rra.\n", rc));
        return rc;
    }

    /* Mountable */
    uint32_t u32;
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "Interval", &u32, 1000);
    if (RT_SUCCESS(rc))
        pThis->cMilliesPoller = u32;
    else
    {
        AssertMsgFailed(("Configuration error: Query \"Mountable\" resulted in %Rrc.\n", rc));
        return rc;
    }

    /* ReadOnly - passthrough mode requires read/write access in any case. */
    if (   (pThis->enmType == PDMMEDIATYPE_CDROM || pThis->enmType == PDMMEDIATYPE_DVD)
        && pThis->IMedia.pfnSendCmd)
            pThis->fReadOnlyConfig = false;
    else
    {
        rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ReadOnly", &pThis->fReadOnlyConfig,
                                         enmType == PDMMEDIATYPE_DVD || enmType == PDMMEDIATYPE_CDROM
                                       ? true
                                       : false);
        if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("Configuration error: Query \"ReadOnly\" resulted in %Rrc.\n", rc));
            return rc;
        }
    }

    /* Locked */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Locked", &pThis->fLocked, false);
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: Query \"Locked\" resulted in %Rrc.\n", rc));
        return rc;
    }

    /* BIOS visible */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "BIOSVisible", &pThis->fBiosVisible, true);
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: Query \"BIOSVisible\" resulted in %Rrc.\n", rc));
        return rc;
    }

    /* Uuid */
    char *psz;
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "Uuid", &psz);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        RTUuidClear(&pThis->Uuid);
    else if (RT_SUCCESS(rc))
    {
        rc = RTUuidFromStr(&pThis->Uuid, psz);
        if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("Configuration error: Uuid from string failed on \"%s\", rc=%Rrc.\n", psz, rc));
            PDMDrvHlpMMHeapFree(pDrvIns, psz);
            return rc;
        }
        PDMDrvHlpMMHeapFree(pDrvIns, psz);
    }
    else
    {
        AssertMsgFailed(("Configuration error: Failed to obtain the uuid, rc=%Rrc.\n", rc));
        return rc;
    }

    /* Define whether attach failure is an error (default) or not. */
    bool fAttachFailError = true;
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "AttachFailError", &fAttachFailError, true);
    pThis->fAttachFailError = fAttachFailError;

    /* log config summary */
    Log(("%s-%d: pszDevice='%s' (%s) cMilliesPoller=%d fReadOnlyConfig=%d fLocked=%d fBIOSVisible=%d Uuid=%RTuuid\n",
         pDrvIns->pReg->szName, pDrvIns->iInstance, pThis->pszDevice, pThis->pszDeviceOpen, pThis->cMilliesPoller,
         pThis->fReadOnlyConfig, pThis->fLocked, pThis->fBiosVisible, &pThis->Uuid));

    /*
     * Check that there are no drivers below us.
     */
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Register saved state.
     */
    rc = PDMDrvHlpSSMRegisterLoadDone(pDrvIns, drvHostBaseLoadDone);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Initialize the critical section used for serializing the access to the media.
     */
    rc = RTCritSectInit(&pThis->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Open the device.
     */
    rc = drvHostBaseOpenOs(pThis, pThis->fReadOnlyConfig);
    if (RT_FAILURE(rc))
    {
        char *pszDevice = pThis->pszDevice;
#ifndef RT_OS_DARWIN
        char szPathReal[256];
        if (   RTPathExists(pszDevice)
            && RT_SUCCESS(RTPathReal(pszDevice, szPathReal, sizeof(szPathReal))))
            pszDevice = szPathReal;
#endif

        /*
         * Disable CD/DVD passthrough in case it was enabled. Would cause
         * weird failures later when the guest issues commands. These would
         * all fail because of the invalid file handle. So use the normal
         * virtual CD/DVD code, which deals more gracefully with unavailable
         * "media" - actually a complete drive in this case.
         */
        pThis->IMedia.pfnSendCmd = NULL;
        AssertMsgFailed(("Could not open host device %s, rc=%Rrc\n", pszDevice, rc));
        switch (rc)
        {
            case VERR_ACCESS_DENIED:
                return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
#ifdef RT_OS_LINUX
                        N_("Cannot open host device '%s' for %s access. Check the permissions "
                           "of that device ('/bin/ls -l %s'): Most probably you need to be member "
                           "of the device group. Make sure that you logout/login after changing "
                           "the group settings of the current user"),
#else
                        N_("Cannot open host device '%s' for %s access. Check the permissions "
                           "of that device"),
#endif
                       pszDevice, pThis->fReadOnlyConfig ? "readonly" : "read/write",
                       pszDevice);
            default:
            {
                if (pThis->fAttachFailError)
                    return rc;
                int erc = PDMDrvHlpVMSetRuntimeError(pDrvIns, 0 /*fFlags*/,
                                                     "DrvHost_MOUNTFAIL",
                                                     N_("Cannot attach to host device '%s'"), pszDevice);
                AssertRC(erc);
                src = rc;
            }
        }
    }

    /*
     * Lock the drive if that's required by the configuration.
     */
    if (pThis->fLocked)
    {
        if (pThis->pfnDoLock)
            rc = pThis->pfnDoLock(pThis, true);
        if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("Failed to lock the dvd drive. rc=%Rrc\n", rc));
            return rc;
        }
    }

    if (RT_SUCCESS(src) && drvHostBaseIsMediaPollingRequiredOs(pThis))
    {
        /*
         * Create the event semaphore which the poller thread will wait on.
         */
        rc = RTSemEventCreate(&pThis->EventPoller);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Start the thread which will poll for the media.
         */
        rc = RTThreadCreate(&pThis->ThreadPoller, drvHostBaseMediaThread, pThis, 0,
                            RTTHREADTYPE_INFREQUENT_POLLER, RTTHREADFLAGS_WAITABLE, "DVDMEDIA");
        if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("Failed to create poller thread. rc=%Rrc\n", rc));
            return rc;
        }

        /*
         * Wait for the thread to start up (!w32:) and do one detection loop.
         */
        rc = RTThreadUserWait(pThis->ThreadPoller, 10000);
        AssertRC(rc);
    }

    if (RT_SUCCESS(rc))
        drvHostBaseStatsRegister(pThis);

    if (RT_FAILURE(rc))
    {
        if (!pThis->fAttachFailError)
        {
            /* Suppressing the attach failure error must not affect the normal
             * DRVHostBaseDestruct, so reset this flag below before leaving. */
            pThis->fKeepInstance = true;
            rc = VINF_SUCCESS;
        }
        DRVHostBaseDestruct(pDrvIns);
        pThis->fKeepInstance = false;
    }

    if (RT_FAILURE(src))
        return src;

    return rc;
}

