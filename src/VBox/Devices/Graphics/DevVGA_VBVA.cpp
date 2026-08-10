/* $Id: DevVGA_VBVA.cpp 114932 2026-08-10 12:38:24Z vitali.pelenjow@oracle.com $ */
/** @file
 * VirtualBox Video Acceleration (VBVA).
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_VGA
#include <VBox/vmm/pdmifs.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/VMMDev.h>
#include <VBox/AssertGuest.h>
#include <VBox/version.h>
#include <VBoxVideo.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/param.h>

#include "DevVGA.h"

/* A very detailed logging. */
#if 0 // def DEBUG_sunlover
#define LOGVBVABUFFER(a) LogFlow(a)
#else
#define LOGVBVABUFFER(a) do {} while (0)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct VBVAPARTIALRECORD
{
    uint8_t *pu8;
    uint32_t cb;
} VBVAPARTIALRECORD;

typedef struct VBVADATA
{
    struct
    {
        VBVABUFFER RT_UNTRUSTED_VOLATILE_GUEST *pVBVA;   /**< Pointer to the guest memory with the VBVABUFFER. */
        uint8_t RT_UNTRUSTED_VOLATILE_GUEST    *pu8Data; /**< For convenience, pointer to the guest ring buffer (VBVABUFFER::au8Data). */
    } guest;
    uint32_t u32VBVAOffset;           /**< VBVABUFFER offset in the guest VRAM. */
    VBVAPARTIALRECORD partialRecord;  /**< Partial record temporary storage. */
    uint32_t off32Data;               /**< The offset where the data starts in the VBVABUFFER.
                                       * The host code uses it instead of VBVABUFFER::off32Data. */
    uint32_t indexRecordFirst;        /**< Index of the first filled record in VBVABUFFER::aRecords. */
    uint32_t cbPartialWriteThreshold; /**< Copy of VBVABUFFER::cbPartialWriteThreshold used by host code. */
    uint32_t cbData;                  /**< Copy of VBVABUFFER::cbData used by host code. */
} VBVADATA;

typedef struct VBVAVIEW
{
    VBVAINFOVIEW    view;
    VBVAINFOSCREEN  screen;
    VBVADATA        vbva;
} VBVAVIEW;

typedef struct VBVAMOUSESHAPEINFO
{
    bool fSet;
    bool fVisible;
    bool fAlpha;
    uint32_t u32HotX;
    uint32_t u32HotY;
    uint32_t u32Width;
    uint32_t u32Height;
    uint32_t cbShape;
    uint32_t cbAllocated;
    uint8_t *pu8Shape;
} VBVAMOUSESHAPEINFO;

/** @todo saved state: save and restore VBVACONTEXT */
typedef struct VBVACONTEXT
{
    uint32_t cViews;
    VBVAVIEW aViews[VBOX_VIDEO_MAX_SCREENS];
    VBVAMOUSESHAPEINFO mouseShapeInfo;
    bool fPaused;
    VBVAMODEHINT aModeHints[VBOX_VIDEO_MAX_SCREENS];
} VBVACONTEXT;


static void vbvaDataCleanup(VBVADATA *pVBVAData)
{
    if (pVBVAData->guest.pVBVA)
    {
        pVBVAData->guest.pVBVA->hostFlags.u32HostEvents      = 0;
        pVBVAData->guest.pVBVA->hostFlags.u32SupportedOrders = 0;
    }

    RTMemFreeZ(pVBVAData->partialRecord.pu8, pVBVAData->partialRecord.cb);

    RT_ZERO(*pVBVAData);
    pVBVAData->u32VBVAOffset = HGSMIOFFSET_VOID;
}

/** Copies @a cb bytes from the VBVA ring buffer to the @a pbDst.
 * Used for partial records or for records which cross the ring boundary.
 */
static bool vbvaFetchBytes(VBVADATA *pVBVAData, uint8_t *pbDst, uint32_t cb)
{
    if (cb >= pVBVAData->cbData)
    {
        AssertMsgFailed(("cb = 0x%08X, ring buffer size 0x%08X", cb, pVBVAData->cbData));
        return false;
    }

    const uint8_t RT_UNTRUSTED_VOLATILE_GUEST *pbSrc = &pVBVAData->guest.pu8Data[pVBVAData->off32Data];
    const uint32_t u32BytesTillBoundary = pVBVAData->cbData - pVBVAData->off32Data;

    if (cb <= u32BytesTillBoundary)
    {
        /* Chunk will not cross buffer boundary. */
        RT_BCOPY_VOLATILE(pbDst, pbSrc, cb);
    }
    else
    {
        /* Chunk crosses buffer boundary. */
        RT_BCOPY_VOLATILE(pbDst, pbSrc, u32BytesTillBoundary);
        RT_BCOPY_VOLATILE(pbDst + u32BytesTillBoundary, &pVBVAData->guest.pu8Data[0], cb - u32BytesTillBoundary);
    }

    /* Advance data offset and sync with guest. */
    pVBVAData->off32Data = (pVBVAData->off32Data + cb) % pVBVAData->cbData;
    pVBVAData->guest.pVBVA->off32Data = pVBVAData->off32Data;
    return true;
}


static bool vbvaPartialRead(uint32_t cbRecord, VBVADATA *pVBVAData)
{
    VBVAPARTIALRECORD *pPartialRecord = &pVBVAData->partialRecord;
    uint8_t *pu8New;

    LOGVBVABUFFER(("vbvaPartialRead: p = %p, cb = %d, cbRecord 0x%08X\n",
                   pPartialRecord->pu8, pPartialRecord->cb, cbRecord));

    Assert(cbRecord > pPartialRecord->cb); /* Caller ensures this. */

    const uint32_t cbChunk = cbRecord - pPartialRecord->cb;
    if (cbChunk >= pVBVAData->cbData)
    {
        return false;
    }

    if (pPartialRecord->pu8)
    {
        Assert(pPartialRecord->cb);
        pu8New = (uint8_t *)RTMemRealloc(pPartialRecord->pu8, cbRecord);
    }
    else
    {
        Assert(!pPartialRecord->cb);
        pu8New = (uint8_t *)RTMemAlloc(cbRecord);
    }

    if (!pu8New)
    {
        /* Memory allocation failed, fail the function. */
        Log(("vbvaPartialRead: failed to (re)alocate memory for partial record!!! cbRecord 0x%08X\n",
             cbRecord));

        RTMemFree(pPartialRecord->pu8);
        pPartialRecord->pu8 = NULL;
        pPartialRecord->cb = 0;
        return false;
    }

    /* Fetch data from the ring buffer. */
    if (!vbvaFetchBytes(pVBVAData, pu8New + pPartialRecord->cb, cbChunk))
    {
        RTMemFree(pu8New);
        pPartialRecord->pu8 = NULL;
        pPartialRecord->cb = 0;
        return false;
    }

    pPartialRecord->pu8 = pu8New;
    pPartialRecord->cb = cbRecord;

    return true;
}

/**
 * Always allocate a bounce buffer from heap.
 */
static bool vbvaFetchCmd(VBVADATA *pVBVAData, VBVACMDHDR RT_UNTRUSTED_VOLATILE_GUEST **ppHdr, uint32_t *pcbCmd)
{
    VBVAPARTIALRECORD *pPartialRecord = &pVBVAData->partialRecord;
    uint32_t indexRecordFirst = pVBVAData->indexRecordFirst;
    const uint32_t indexRecordFree = ASMAtomicReadU32(&pVBVAData->guest.pVBVA->indexRecordFree);

    LOGVBVABUFFER(("first = %d, free = %d\n",
                   indexRecordFirst, indexRecordFree));

    if (indexRecordFree >= RT_ELEMENTS(pVBVAData->guest.pVBVA->aRecords))
    {
        return false;
    }

    if (indexRecordFirst == indexRecordFree)
    {
        /* No records to process. Return without assigning output variables. */
        return true;
    }

    uint32_t cbRecordCurrent = ASMAtomicReadU32(&pVBVAData->guest.pVBVA->aRecords[indexRecordFirst].cbRecord);

    LOGVBVABUFFER(("cbRecord = 0x%08X, pPartialRecord->cb = 0x%08X\n", cbRecordCurrent, pPartialRecord->cb));

    uint32_t cbRecord = cbRecordCurrent & ~VBVA_F_RECORD_PARTIAL;

    if (cbRecord > VBVA_MAX_RECORD_SIZE)
    {
        return false;
    }

    if (pPartialRecord->cb)
    {
        /* There is a partial read in process. Continue with it. */
        Assert (pPartialRecord->pu8);

        LOGVBVABUFFER(("continue partial record cb = %d cbRecord 0x%08X, first = %d, free = %d\n",
                      pPartialRecord->cb, cbRecordCurrent, indexRecordFirst, indexRecordFree));

        if (cbRecord > pPartialRecord->cb)
        {
            /* New data has been added to the record. */
            if (!vbvaPartialRead(cbRecord, pVBVAData))
            {
                return false;
            }
        }

        if (!(cbRecordCurrent & VBVA_F_RECORD_PARTIAL))
        {
            /* The record is completed by guest. Return it to the caller. */
            *ppHdr = (VBVACMDHDR *)pPartialRecord->pu8;
            *pcbCmd = pPartialRecord->cb;

            pPartialRecord->pu8 = NULL;
            pPartialRecord->cb = 0;

            /* Advance the record index and sync with guest. */
            pVBVAData->indexRecordFirst = (indexRecordFirst + 1) % RT_ELEMENTS(pVBVAData->guest.pVBVA->aRecords);
            pVBVAData->guest.pVBVA->indexRecordFirst = pVBVAData->indexRecordFirst;

            LOGVBVABUFFER(("partial done ok, data = %d, free = %d\n",
                          pVBVAData->off32Data, pVBVAData->guest.pVBVA->off32Free));
        }

        return true;
    }

    /* A new record need to be processed. */
    if (cbRecordCurrent & VBVA_F_RECORD_PARTIAL)
    {
        /* Current record is being written by guest. '=' is important here,
         * because the guest will do a FLUSH at this condition.
         * This partial record is too large for the ring buffer and must
         * be accumulated in an allocated buffer.
         */
        if (cbRecord >= pVBVAData->cbData - pVBVAData->cbPartialWriteThreshold)
        {
            /* Partial read must be started. */
            if (!vbvaPartialRead(cbRecord, pVBVAData))
            {
                return false;
            }

            LOGVBVABUFFER(("started partial record cb = 0x%08X cbRecord 0x%08X, first = %d, free = %d\n",
                          pPartialRecord->cb, cbRecordCurrent, indexRecordFirst, indexRecordFree));
        }

        return true;
    }

    /* Current record is complete. If it is not empty, process it. */
    if (cbRecord >= pVBVAData->cbData)
    {
        return false;
    }

    if (cbRecord)
    {
        /* Make a copy of the data. */
        uint8_t *pbDst = (uint8_t *)RTMemAlloc(cbRecord);
        if (!pbDst)
        {
            LogFlowFunc (("could not allocate %d bytes from heap!!!\n", cbRecord));
            return false;
        }

        if (!vbvaFetchBytes(pVBVAData, pbDst, cbRecord))
            return false;

        *ppHdr = (VBVACMDHDR *)pbDst;
    }

    *pcbCmd = cbRecord;

    /* Advance the record index and sync with guest. */
    pVBVAData->indexRecordFirst = (indexRecordFirst + 1) % RT_ELEMENTS(pVBVAData->guest.pVBVA->aRecords);
    pVBVAData->guest.pVBVA->indexRecordFirst = pVBVAData->indexRecordFirst;

    LOGVBVABUFFER(("done ok, data = %d, free = %d\n",
                  pVBVAData->off32Data, pVBVAData->guest.pVBVA->off32Free));

    return true;
}

static void vbvaReleaseCmd(VBVADATA *pVBVAData, VBVACMDHDR RT_UNTRUSTED_VOLATILE_GUEST *pHdr, uint32_t cbCmd)
{
    RT_NOREF(pVBVAData, cbCmd);

    LOGVBVABUFFER(("Free heap %p\n", pHdr));

    RTMemFree((void *)pHdr);
}

static int vbvaFlushProcess(PVGASTATECC pThisCC, VBVADATA *pVBVAData, unsigned uScreenId)
{
    LOGVBVABUFFER(("uScreenId %d, indexRecordFirst = %d, indexRecordFree = %d, off32Data = %d, off32Free = %d\n",
                   uScreenId, pVBVAData->indexRecordFirst, pVBVAData->guest.pVBVA->indexRecordFree,
                   pVBVAData->off32Data, pVBVAData->guest.pVBVA->off32Free));
    struct
    {
        /* The rectangle that includes all dirty rectangles. */
        int32_t xLeft;
        int32_t xRight;
        int32_t yTop;
        int32_t yBottom;
    } dirtyRect;
    RT_ZERO(dirtyRect);

    bool fUpdate = false; /* Whether there were any updates. */
    bool fDirtyEmpty = true;

    for (;;)
    {
        /* Fetch the command data. */
        VBVACMDHDR RT_UNTRUSTED_VOLATILE_GUEST *pHdr  = NULL;
        uint32_t                                cbCmd = UINT32_MAX;
        if (!vbvaFetchCmd(pVBVAData, &pHdr, &cbCmd))
        {
            LogFunc(("unable to fetch command. off32Data = %d, off32Free = %d!!!\n",
                     pVBVAData->off32Data, pVBVAData->guest.pVBVA->off32Free));
            return VERR_NOT_SUPPORTED;
        }

        if (cbCmd == UINT32_MAX)
        {
            /* No more commands yet in the queue. */
            break;
        }

        if (cbCmd < sizeof(VBVACMDHDR))
        {
            LogFunc(("command length: off32Data = %d, off32Free = %d, cbCmd %d!!!\n",
                  pVBVAData->off32Data, pVBVAData->guest.pVBVA->off32Free, cbCmd));

            vbvaReleaseCmd(pVBVAData, pHdr, cbCmd);
            return VERR_NOT_SUPPORTED;
        }

        if (!fUpdate)
        {
            pThisCC->pDrv->pfnVBVAUpdateBegin(pThisCC->pDrv, uScreenId);
            fUpdate = true;
        }

        /* Updates the rectangle and sends the command to the VRDP server. */
        pThisCC->pDrv->pfnVBVAUpdateProcess(pThisCC->pDrv, uScreenId, pHdr, cbCmd);

        int32_t xRight  = pHdr->x + pHdr->w;
        int32_t yBottom = pHdr->y + pHdr->h;

        /* These are global coords, relative to the primary screen. */

        LOGVBVABUFFER(("cbCmd = %d, x=%d, y=%d, w=%d, h=%d\n", cbCmd, pHdr->x, pHdr->y, pHdr->w, pHdr->h));
        LogRel3(("%s: update command cbCmd = %d, x=%d, y=%d, w=%d, h=%d\n",
                 __FUNCTION__, cbCmd, pHdr->x, pHdr->y, pHdr->w, pHdr->h));

        /* Collect all rects into one. */
        if (fDirtyEmpty)
        {
            /* This is the first rectangle to be added. */
            dirtyRect.xLeft   = pHdr->x;
            dirtyRect.yTop    = pHdr->y;
            dirtyRect.xRight  = xRight;
            dirtyRect.yBottom = yBottom;
            fDirtyEmpty       = false;
        }
        else
        {
            /* Adjust region coordinates. */
            if (dirtyRect.xLeft > pHdr->x)
            {
                dirtyRect.xLeft = pHdr->x;
            }

            if (dirtyRect.yTop > pHdr->y)
            {
                dirtyRect.yTop = pHdr->y;
            }

            if (dirtyRect.xRight < xRight)
            {
                dirtyRect.xRight = xRight;
            }

            if (dirtyRect.yBottom < yBottom)
            {
                dirtyRect.yBottom = yBottom;
            }
        }

        vbvaReleaseCmd(pVBVAData, pHdr, cbCmd);
    }

    if (fUpdate)
    {
        if (dirtyRect.xRight - dirtyRect.xLeft)
        {
            LogRel3(("%s: sending update screen=%d, x=%d, y=%d, w=%d, h=%d\n",
                     __FUNCTION__, uScreenId, dirtyRect.xLeft,
                     dirtyRect.yTop, dirtyRect.xRight - dirtyRect.xLeft,
                     dirtyRect.yBottom - dirtyRect.yTop));
            pThisCC->pDrv->pfnVBVAUpdateEnd(pThisCC->pDrv, uScreenId, dirtyRect.xLeft, dirtyRect.yTop,
                                              dirtyRect.xRight - dirtyRect.xLeft, dirtyRect.yBottom - dirtyRect.yTop);
        }
        else
        {
            pThisCC->pDrv->pfnVBVAUpdateEnd(pThisCC->pDrv, uScreenId, 0, 0, 0, 0);
        }
    }

    return VINF_SUCCESS;
}

static int vbvaFlush(PVGASTATE pThis, PVGASTATECC pThisCC, VBVACONTEXT *pCtx)
{
    int rc = VINF_SUCCESS;

    unsigned uScreenId;
    for (uScreenId = 0; uScreenId < pCtx->cViews; uScreenId++)
    {
        VBVADATA *pVBVAData = &pCtx->aViews[uScreenId].vbva;
        if (pVBVAData->guest.pVBVA)
        {
            rc = vbvaFlushProcess(pThisCC, pVBVAData, uScreenId);
            if (RT_FAILURE(rc))
                break;
        }
    }

    if (RT_FAILURE(rc))
    {
        /* Turn off VBVA processing. */
        LogRel(("VBVA: Disabling (%Rrc)\n", rc));
        pThis->fGuestCaps = 0;
        pThisCC->pDrv->pfnVBVAGuestCapabilityUpdate(pThisCC->pDrv, pThis->fGuestCaps);
        for (uScreenId = 0; uScreenId < pCtx->cViews; uScreenId++)
        {
            VBVADATA *pVBVAData = &pCtx->aViews[uScreenId].vbva;
            if (pVBVAData->guest.pVBVA)
            {
                vbvaDataCleanup(pVBVAData);
                pThisCC->pDrv->pfnVBVADisable(pThisCC->pDrv, uScreenId);
            }
        }
    }

    return rc;
}

static int vbvaResize(PVGASTATECC pThisCC, VBVAVIEW *pView, const VBVAINFOSCREEN *pNewScreen, bool fResetInputMapping)
{
    /* Callers ensure that pNewScreen contains valid data. */

    /* Apply these changes. */
    pView->screen = *pNewScreen;

    uint8_t *pbVRam = pThisCC->pbVRam + pView->view.u32ViewOffset;
    return pThisCC->pDrv->pfnVBVAResize(pThisCC->pDrv, &pView->view, &pView->screen, pbVRam, fResetInputMapping);
}

static int vbvaEnable(PVGASTATE pThis, PVGASTATECC pThisCC, VBVACONTEXT *pCtx, unsigned uScreenId,
                      VBVABUFFER RT_UNTRUSTED_VOLATILE_GUEST *pVBVA, uint32_t u32Offset, bool fRestored)
{
    /*
     * Copy into non-volatile memory and validate its content.
     */
    VBVABUFFER VbgaSafe;
    RT_COPY_VOLATILE(VbgaSafe, *pVBVA);
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

    uint32_t const cbVBVABuffer = RT_UOFFSETOF(VBVABUFFER, au8Data) + VbgaSafe.cbData;
    ASSERT_GUEST_RETURN(   VbgaSafe.cbData <= UINT32_MAX - RT_UOFFSETOF(VBVABUFFER, au8Data)
                        && cbVBVABuffer <= pThis->vram_size
                        && u32Offset <= pThis->vram_size - cbVBVABuffer,
                        VERR_INVALID_PARAMETER);
    if (!fRestored)
    {
        ASSERT_GUEST_RETURN(VbgaSafe.off32Data        == 0, VERR_INVALID_PARAMETER);
        ASSERT_GUEST_RETURN(VbgaSafe.off32Free        == 0, VERR_INVALID_PARAMETER);
        ASSERT_GUEST_RETURN(VbgaSafe.indexRecordFirst == 0, VERR_INVALID_PARAMETER);
        ASSERT_GUEST_RETURN(VbgaSafe.indexRecordFree  == 0, VERR_INVALID_PARAMETER);
    }
    ASSERT_GUEST_RETURN(   VbgaSafe.cbPartialWriteThreshold < VbgaSafe.cbData
                        && VbgaSafe.cbPartialWriteThreshold != 0,
                        VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /*
     * Okay, try do the job.
     */
    int rc;
    if (pThisCC->pDrv->pfnVBVAEnable)
    {
        pVBVA->hostFlags.u32HostEvents      = 0;
        pVBVA->hostFlags.u32SupportedOrders = 0;
        rc = pThisCC->pDrv->pfnVBVAEnable(pThisCC->pDrv, uScreenId, &pVBVA->hostFlags);
        if (RT_SUCCESS(rc))
        {
            /* pVBVA->hostFlags has been set up by pfnVBVAEnable. */
            LogFlowFunc(("u32HostEvents=0x%08x  u32SupportedOrders=0x%08x\n",
                         pVBVA->hostFlags.u32HostEvents, pVBVA->hostFlags.u32SupportedOrders));

            VBVADATA *pVBVAData = &pCtx->aViews[uScreenId].vbva;
            pVBVAData->guest.pVBVA             = pVBVA;
            pVBVAData->guest.pu8Data           = &pVBVA->au8Data[0];
            pVBVAData->u32VBVAOffset           = u32Offset;
            pVBVAData->off32Data               = VbgaSafe.off32Data;
            pVBVAData->indexRecordFirst        = VbgaSafe.indexRecordFirst;
            pVBVAData->cbPartialWriteThreshold = VbgaSafe.cbPartialWriteThreshold;
            pVBVAData->cbData                  = VbgaSafe.cbData;

            if (!fRestored)
            {
                /** @todo Actually this function must not touch the partialRecord structure at all,
                 * because initially it is a zero and when VBVA is disabled this should be set to zero.
                 * But I'm not sure that no code depends on zeroing partialRecord here.
                 * So for now (a quick fix for 4.1) just do not do this if the VM was restored,
                 * when partialRecord might be loaded already from the saved state.
                 */
                pVBVAData->partialRecord.pu8 = NULL;
                pVBVAData->partialRecord.cb = 0;
            }

            /* VBVA is working so disable the pause. */
            pCtx->fPaused = false;
        }
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}

static int vbvaDisable(PVGASTATE pThis, PVGASTATECC pThisCC, VBVACONTEXT *pCtx, unsigned idScreen)
{
    /* Process any pending orders and empty the VBVA ring buffer. */
    vbvaFlush(pThis, pThisCC, pCtx);

    AssertReturn(idScreen < RT_ELEMENTS(pCtx->aViews), VERR_OUT_OF_RANGE);
    VBVADATA *pVBVAData = &pCtx->aViews[idScreen].vbva;
    vbvaDataCleanup(pVBVAData);

    if (idScreen == 0)
    {
        pThis->fGuestCaps = 0;
        pThisCC->pDrv->pfnVBVAGuestCapabilityUpdate(pThisCC->pDrv, pThis->fGuestCaps);
    }
    pThisCC->pDrv->pfnVBVADisable(pThisCC->pDrv, idScreen);
    return VINF_SUCCESS;
}

#ifdef UNUSED_FUNCTION
bool VBVAIsEnabled(PVGASTATECC pThisCC)
{
    PHGSMIINSTANCE pHGSMI = pThisCC->pHGSMI;
    if (pHGSMI)
    {
        VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pHGSMI);
        if (pCtx)
        {
            if (pCtx->cViews)
            {
                VBVAVIEW * pView = &pCtx->aViews[0];
                if (pView->vbva.guest.pVBVA)
                    return true;
            }
        }
    }
    return false;
}
#endif

#ifdef DEBUG_sunlover
void dumpMouseShapeInfo(const VBVAMOUSESHAPEINFO *pMouseShapeInfo)
{
    LogFlow(("fSet = %d, fVisible %d, fAlpha %d, @%d,%d %dx%d (%p, %d/%d)\n",
             pMouseShapeInfo->fSet,
             pMouseShapeInfo->fVisible,
             pMouseShapeInfo->fAlpha,
             pMouseShapeInfo->u32HotX,
             pMouseShapeInfo->u32HotY,
             pMouseShapeInfo->u32Width,
             pMouseShapeInfo->u32Height,
             pMouseShapeInfo->pu8Shape,
             pMouseShapeInfo->cbShape,
             pMouseShapeInfo->cbAllocated
             ));
}
#endif

static int vbvaUpdateMousePointerShape(PVGASTATECC pThisCC, VBVAMOUSESHAPEINFO *pMouseShapeInfo, bool fShape)
{
    LogFlowFunc(("pThisCC %p, pMouseShapeInfo %p, fShape %d\n", pThisCC, pMouseShapeInfo, fShape));
#ifdef DEBUG_sunlover
    dumpMouseShapeInfo(pMouseShapeInfo);
#endif

    if (pThisCC->pDrv->pfnVBVAMousePointerShape == NULL)
        return VERR_NOT_SUPPORTED;

    int rc;
    if (fShape && pMouseShapeInfo->pu8Shape != NULL)
        rc = pThisCC->pDrv->pfnVBVAMousePointerShape(pThisCC->pDrv,
                                                     pMouseShapeInfo->fVisible,
                                                     pMouseShapeInfo->fAlpha,
                                                     pMouseShapeInfo->u32HotX,
                                                     pMouseShapeInfo->u32HotY,
                                                     pMouseShapeInfo->u32Width,
                                                     pMouseShapeInfo->u32Height,
                                                     pMouseShapeInfo->pu8Shape);
    else
        rc = pThisCC->pDrv->pfnVBVAMousePointerShape(pThisCC->pDrv,
                                                     pMouseShapeInfo->fVisible,
                                                     false,
                                                     0, 0,
                                                     0, 0,
                                                     NULL);
    return rc;
}

static int vbvaMousePointerShape(PVGASTATECC pThisCC, VBVACONTEXT *pCtx,
                                 const VBVAMOUSEPOINTERSHAPE RT_UNTRUSTED_VOLATILE_GUEST *pShape, HGSMISIZE cbShape)
{
    /*
     * Make non-volatile copy of the shape header and validate it.
     */
    VBVAMOUSEPOINTERSHAPE SafeShape;
    RT_COPY_VOLATILE(SafeShape, *pShape);
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

    LogFlowFunc(("VBVA_MOUSE_POINTER_SHAPE: i32Result 0x%x, fu32Flags 0x%x, hot spot %d,%d, size %dx%d\n",
                 SafeShape.i32Result, SafeShape.fu32Flags, SafeShape.u32HotX, SafeShape.u32HotY, SafeShape.u32Width, SafeShape.u32Height));

    const bool fVisible = RT_BOOL(SafeShape.fu32Flags & VBOX_MOUSE_POINTER_VISIBLE);
    const bool fAlpha =   RT_BOOL(SafeShape.fu32Flags & VBOX_MOUSE_POINTER_ALPHA);
    const bool fShape =   RT_BOOL(SafeShape.fu32Flags & VBOX_MOUSE_POINTER_SHAPE);

    HGSMISIZE cbPointerData = 0;
    if (fShape)
    {
        static const uint32_t s_cxMax = 2048; //used to be: 8192;
        static const uint32_t s_cyMax = 2048; //used to be: 8192;
        ASSERT_GUEST_MSG_RETURN(   SafeShape.u32Width  <= s_cxMax
                                && SafeShape.u32Height <= s_cyMax,
                                ("Too large: %ux%u, max %ux%x\n", SafeShape.u32Width, SafeShape.u32Height, s_cxMax, s_cyMax),
                                VERR_INVALID_PARAMETER);

         cbPointerData = ((((SafeShape.u32Width + 7) / 8) * SafeShape.u32Height + 3) & ~3)
                       + SafeShape.u32Width * 4 * SafeShape.u32Height;

         ASSERT_GUEST_MSG_RETURN(cbPointerData <= cbShape - RT_UOFFSETOF(VBVAMOUSEPOINTERSHAPE, au8Data),
                                 ("Insufficent pointer data: Expected %#x, got %#x\n",
                                  cbPointerData, cbShape - RT_UOFFSETOF(VBVAMOUSEPOINTERSHAPE, au8Data) ),
                                 VERR_INVALID_PARAMETER);
    }
    RT_UNTRUSTED_VALIDATED_FENCE();

    /*
     * Do the job.
     */
    /* Save mouse info it will be used to restore mouse pointer after restoring saved state. */
    pCtx->mouseShapeInfo.fSet = true;
    pCtx->mouseShapeInfo.fVisible = fVisible;
    if (fShape)
    {
        /* Data related to shape. */
        pCtx->mouseShapeInfo.u32HotX = SafeShape.u32HotX;
        pCtx->mouseShapeInfo.u32HotY = SafeShape.u32HotY;
        pCtx->mouseShapeInfo.u32Width = SafeShape.u32Width;
        pCtx->mouseShapeInfo.u32Height = SafeShape.u32Height;
        pCtx->mouseShapeInfo.fAlpha = fAlpha;

        /* Reallocate memory buffer if necessary. */
        if (cbPointerData > pCtx->mouseShapeInfo.cbAllocated)
        {
            RTMemFreeZ(pCtx->mouseShapeInfo.pu8Shape, pCtx->mouseShapeInfo.cbAllocated);
            pCtx->mouseShapeInfo.pu8Shape = NULL;
            pCtx->mouseShapeInfo.cbShape = 0;

            uint8_t *pu8Shape = (uint8_t *)RTMemAlloc(cbPointerData);
            if (pu8Shape)
            {
                pCtx->mouseShapeInfo.pu8Shape = pu8Shape;
                pCtx->mouseShapeInfo.cbAllocated = cbPointerData;
            }
        }

        /* Copy shape bitmaps. */
        if (pCtx->mouseShapeInfo.pu8Shape)
        {
            RT_BCOPY_VOLATILE(pCtx->mouseShapeInfo.pu8Shape, &pShape->au8Data[0], cbPointerData);
            pCtx->mouseShapeInfo.cbShape = cbPointerData;
        }
    }

    return vbvaUpdateMousePointerShape(pThisCC, &pCtx->mouseShapeInfo, fShape);
}

static uint32_t vbvaViewFromBufferPtr(PHGSMIINSTANCE pIns, const VBVACONTEXT *pCtx,
                                      const void RT_UNTRUSTED_VOLATILE_GUEST *pvBuffer)
{
    /* Check which view contains the buffer. */
    HGSMIOFFSET offBuffer = HGSMIPointerToOffsetHost(pIns, pvBuffer);
    if (offBuffer != HGSMIOFFSET_VOID)
    {
        unsigned uScreenId;
        for (uScreenId = 0; uScreenId < pCtx->cViews; uScreenId++)
        {
            const VBVAINFOVIEW *pView = &pCtx->aViews[uScreenId].view;
            if ((uint32_t)(offBuffer - pView->u32ViewOffset) < pView->u32ViewSize)
                return pView->u32ViewIndex;
        }
    }
    return UINT32_MAX;
}

#ifdef DEBUG_sunlover
static void dumpctx(const VBVACONTEXT *pCtx)
{
    Log(("VBVACONTEXT dump: cViews %d\n", pCtx->cViews));

    uint32_t iView;
    for (iView = 0; iView < pCtx->cViews; iView++)
    {
        const VBVAVIEW *pView = &pCtx->aViews[iView];

        Log(("                  view %d o 0x%x s 0x%x m 0x%x\n",
              pView->view.u32ViewIndex,
              pView->view.u32ViewOffset,
              pView->view.u32ViewSize,
              pView->view.u32MaxScreenSize));

        Log(("                  screen %d @%d,%d s 0x%x l 0x%x %dx%d bpp %d f 0x%x\n",
              pView->screen.u32ViewIndex,
              pView->screen.i32OriginX,
              pView->screen.i32OriginY,
              pView->screen.u32StartOffset,
              pView->screen.u32LineSize,
              pView->screen.u32Width,
              pView->screen.u32Height,
              pView->screen.u16BitsPerPixel,
              pView->screen.u16Flags));

        Log(("                  VBVA o 0x%x p %p\n",
              pView->vbva.u32VBVAOffset,
              pView->vbva.guest.pVBVA));

        Log(("                  PR cb 0x%x p %p\n",
              pView->vbva.partialRecord.cb,
              pView->vbva.partialRecord.pu8));
    }

    dumpMouseShapeInfo(&pCtx->mouseShapeInfo);
}
#endif /* DEBUG_sunlover */

#define VBOXVBVASAVEDSTATE_VHWAAVAILABLE_MAGIC   0x12345678
#define VBOXVBVASAVEDSTATE_VHWAUNAVAILABLE_MAGIC 0x9abcdef0

static int vboxVBVASaveDevStateExec(PCPDMDEVHLPR3 pHlp, PVGASTATE pThis, PVGASTATECC pThisCC, PSSMHANDLE pSSM)
{
    PHGSMIINSTANCE pIns = pThisCC->pHGSMI;
    int rc = HGSMIHostSaveStateExec(pHlp, pIns, pSSM);
    if (RT_SUCCESS(rc))
    {
        VGA_SAVED_STATE_PUT_MARKER(pSSM, 2);

        /* Save VBVACONTEXT. */
        VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext (pIns);

        if (!pCtx)
        {
            AssertFailed();

            /* Still write a valid value to the SSM. */
            rc = pHlp->pfnSSMPutU32 (pSSM, 0);
            AssertRCReturn(rc, rc);
        }
        else
        {
#ifdef DEBUG_sunlover
            dumpctx(pCtx);
#endif

            rc = pHlp->pfnSSMPutU32 (pSSM, pCtx->cViews);
            AssertRCReturn(rc, rc);

            uint32_t iView;
            for (iView = 0; iView < pCtx->cViews; iView++)
            {
                VBVAVIEW *pView = &pCtx->aViews[iView];

                rc = pHlp->pfnSSMPutU32(pSSM, pView->view.u32ViewIndex);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutU32(pSSM, pView->view.u32ViewOffset);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutU32(pSSM, pView->view.u32ViewSize);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutU32(pSSM, pView->view.u32MaxScreenSize);
                AssertRCReturn(rc, rc);

                rc = pHlp->pfnSSMPutU32(pSSM, pView->screen.u32ViewIndex);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutS32(pSSM, pView->screen.i32OriginX);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutS32(pSSM, pView->screen.i32OriginY);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutU32(pSSM, pView->screen.u32StartOffset);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutU32(pSSM, pView->screen.u32LineSize);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutU32(pSSM, pView->screen.u32Width);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutU32(pSSM, pView->screen.u32Height);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutU16(pSSM, pView->screen.u16BitsPerPixel);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMPutU16(pSSM, pView->screen.u16Flags);
                AssertRCReturn(rc, rc);

                rc = pHlp->pfnSSMPutU32(pSSM, pView->vbva.guest.pVBVA? pView->vbva.u32VBVAOffset: HGSMIOFFSET_VOID);
                AssertRCReturn(rc, rc);

                rc = pHlp->pfnSSMPutU32(pSSM, pView->vbva.partialRecord.cb);
                AssertRCReturn(rc, rc);

                if (pView->vbva.partialRecord.cb > 0)
                {
                    rc = pHlp->pfnSSMPutMem(pSSM, pView->vbva.partialRecord.pu8, pView->vbva.partialRecord.cb);
                    AssertRCReturn(rc, rc);
                }
            }

            /* Save mouse pointer shape information. */
            rc = pHlp->pfnSSMPutBool(pSSM, pCtx->mouseShapeInfo.fSet);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMPutBool(pSSM, pCtx->mouseShapeInfo.fVisible);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMPutBool(pSSM, pCtx->mouseShapeInfo.fAlpha);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMPutU32(pSSM, pCtx->mouseShapeInfo.u32HotX);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMPutU32(pSSM, pCtx->mouseShapeInfo.u32HotY);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMPutU32(pSSM, pCtx->mouseShapeInfo.u32Width);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMPutU32(pSSM, pCtx->mouseShapeInfo.u32Height);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMPutU32(pSSM, pCtx->mouseShapeInfo.cbShape);
            AssertRCReturn(rc, rc);
            if (pCtx->mouseShapeInfo.cbShape)
            {
                rc = pHlp->pfnSSMPutMem(pSSM, pCtx->mouseShapeInfo.pu8Shape, pCtx->mouseShapeInfo.cbShape);
                AssertRCReturn(rc, rc);
            }

#ifdef VBOX_WITH_WDDM
            /* Size of some additional data. For future extensions. */
            rc = pHlp->pfnSSMPutU32(pSSM, 4);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMPutU32(pSSM, pThis->fGuestCaps);
            AssertRCReturn(rc, rc);
#else
            /* Size of some additional data. For future extensions. */
            rc = pHlp->pfnSSMPutU32(pSSM, 0);
            AssertRCReturn(rc, rc);
#endif
            rc = pHlp->pfnSSMPutU32(pSSM, RT_ELEMENTS(pCtx->aModeHints));
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMPutU32(pSSM, sizeof(VBVAMODEHINT));
            AssertRCReturn(rc, rc);
            for (unsigned i = 0; i < RT_ELEMENTS(pCtx->aModeHints); ++i)
            {
                rc = pHlp->pfnSSMPutMem(pSSM, &pCtx->aModeHints[i], sizeof(VBVAMODEHINT));
                AssertRCReturn(rc, rc);
            }
        }
    }

    return rc;
}

int vboxVBVASaveStateExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    return vboxVBVASaveDevStateExec(pHlp, pThis, pThisCC, pSSM);
}

int vboxVBVALoadStateExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion)
{
    if (uVersion < VGA_SAVEDSTATE_VERSION_HGSMI)
    {
        /* Nothing was saved. */
        return VINF_SUCCESS;
    }

    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PHGSMIINSTANCE  pIns    = pThisCC->pHGSMI;
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int rc = HGSMIHostLoadStateExec(pHlp, pIns, pSSM, uVersion);
    if (RT_SUCCESS(rc))
    {
        VGA_SAVED_STATE_GET_MARKER_RETURN_ON_MISMATCH(pSSM, uVersion, 2);

        /* Load VBVACONTEXT. */
        VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext (pIns);

        if (!pCtx)
        {
            /* This should not happen. */
            AssertFailed();
            rc = VERR_INVALID_PARAMETER;
        }
        else
        {
            uint32_t cViews = 0;
            rc = pHlp->pfnSSMGetU32 (pSSM, &cViews);
            AssertRCReturn(rc, rc);

            uint32_t iView;
            for (iView = 0; iView < cViews; iView++)
            {
                VBVAVIEW *pView = &pCtx->aViews[iView];

                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->view.u32ViewIndex);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->view.u32ViewOffset);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->view.u32ViewSize);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->view.u32MaxScreenSize);
                AssertRCReturn(rc, rc);

                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->screen.u32ViewIndex);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetS32 (pSSM, &pView->screen.i32OriginX);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetS32 (pSSM, &pView->screen.i32OriginY);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->screen.u32StartOffset);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->screen.u32LineSize);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->screen.u32Width);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->screen.u32Height);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU16 (pSSM, &pView->screen.u16BitsPerPixel);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU16 (pSSM, &pView->screen.u16Flags);
                AssertRCReturn(rc, rc);

                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->vbva.u32VBVAOffset);
                AssertRCReturn(rc, rc);

                rc = pHlp->pfnSSMGetU32 (pSSM, &pView->vbva.partialRecord.cb);
                AssertRCReturn(rc, rc);

                if (pView->vbva.partialRecord.cb == 0)
                {
                    pView->vbva.partialRecord.pu8 = NULL;
                }
                else
                {
                    Assert(pView->vbva.partialRecord.pu8 == NULL); /* Should be it. */

                    uint8_t *pu8 = (uint8_t *)RTMemAlloc(pView->vbva.partialRecord.cb);

                    if (!pu8)
                    {
                        return VERR_NO_MEMORY;
                    }

                    pView->vbva.partialRecord.pu8 = pu8;

                    rc = pHlp->pfnSSMGetMem (pSSM, pView->vbva.partialRecord.pu8, pView->vbva.partialRecord.cb);
                    AssertRCReturn(rc, rc);
                }

                if (pView->vbva.u32VBVAOffset == HGSMIOFFSET_VOID)
                {
                    pView->vbva.guest.pVBVA = NULL;
                }
                else
                {
                    pView->vbva.guest.pVBVA = (VBVABUFFER *)HGSMIOffsetToPointerHost(pIns, pView->vbva.u32VBVAOffset);
                }
            }

            if (uVersion > VGA_SAVEDSTATE_VERSION_WITH_CONFIG)
            {
                /* Read mouse pointer shape information. */
                rc = pHlp->pfnSSMGetBool (pSSM, &pCtx->mouseShapeInfo.fSet);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetBool (pSSM, &pCtx->mouseShapeInfo.fVisible);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetBool (pSSM, &pCtx->mouseShapeInfo.fAlpha);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pCtx->mouseShapeInfo.u32HotX);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pCtx->mouseShapeInfo.u32HotY);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pCtx->mouseShapeInfo.u32Width);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pCtx->mouseShapeInfo.u32Height);
                AssertRCReturn(rc, rc);
                rc = pHlp->pfnSSMGetU32 (pSSM, &pCtx->mouseShapeInfo.cbShape);
                AssertRCReturn(rc, rc);
                if (pCtx->mouseShapeInfo.cbShape)
                {
                    pCtx->mouseShapeInfo.pu8Shape = (uint8_t *)RTMemAlloc(pCtx->mouseShapeInfo.cbShape);
                    if (pCtx->mouseShapeInfo.pu8Shape == NULL)
                    {
                        return VERR_NO_MEMORY;
                    }
                    pCtx->mouseShapeInfo.cbAllocated = pCtx->mouseShapeInfo.cbShape;
                    rc = pHlp->pfnSSMGetMem (pSSM, pCtx->mouseShapeInfo.pu8Shape, pCtx->mouseShapeInfo.cbShape);
                    AssertRCReturn(rc, rc);
                }
                else
                {
                    pCtx->mouseShapeInfo.pu8Shape = NULL;
                }

                /* Size of some additional data. For future extensions. */
                uint32_t cbExtra = 0;
                rc = pHlp->pfnSSMGetU32 (pSSM, &cbExtra);
                AssertRCReturn(rc, rc);
#ifdef VBOX_WITH_WDDM
                if (cbExtra >= 4)
                {
                    rc = pHlp->pfnSSMGetU32 (pSSM, &pThis->fGuestCaps);
                    AssertRCReturn(rc, rc);
                    pThisCC->pDrv->pfnVBVAGuestCapabilityUpdate(pThisCC->pDrv, pThis->fGuestCaps);
                    cbExtra -= 4;
                }
#endif
                if (cbExtra > 0)
                {
                    rc = pHlp->pfnSSMSkip(pSSM, cbExtra);
                    AssertRCReturn(rc, rc);
                }

                if (uVersion >= VGA_SAVEDSTATE_VERSION_MODE_HINTS)
                {
                    uint32_t cModeHints, cbModeHints;
                    rc = pHlp->pfnSSMGetU32 (pSSM, &cModeHints);
                    AssertRCReturn(rc, rc);
                    rc = pHlp->pfnSSMGetU32 (pSSM, &cbModeHints);
                    AssertRCReturn(rc, rc);
                    memset(&pCtx->aModeHints, ~0, sizeof(pCtx->aModeHints));
                    unsigned iHint;
                    for (iHint = 0; iHint < cModeHints; ++iHint)
                    {
                        if (   cbModeHints <= sizeof(VBVAMODEHINT)
                            && iHint < RT_ELEMENTS(pCtx->aModeHints))
                            rc = pHlp->pfnSSMGetMem(pSSM, &pCtx->aModeHints[iHint],
                                             cbModeHints);
                        else
                            rc = pHlp->pfnSSMSkip(pSSM, cbModeHints);
                        AssertRCReturn(rc, rc);
                    }
                }
            }

            pCtx->cViews = iView;
            LogFlowFunc(("%d views loaded\n", pCtx->cViews));

            if (uVersion > VGA_SAVEDSTATE_VERSION_WDDM)
            {
                /* Skip loading VHWA (2D Video Hardware Acceleration) state from older saved states. */
                if (   uVersion < VGA_SAVEDSTATE_VERSION_VHWA_REMOVED
                    || pHlp->pfnSSMHandleVersion(pSSM) < VBOX_FULL_VERSION_MAKE(7,2,0))
                {
                    bool fLoadCommands;
                    if (uVersion < VGA_SAVEDSTATE_VERSION_FIXED_PENDVHWA)
                    {
                        const char *pcszOsArch = pHlp->pfnSSMHandleHostOSAndArch(pSSM);
                        AssertPtr(pcszOsArch);
                        fLoadCommands = !pcszOsArch || RTStrNCmp(pcszOsArch, RT_STR_TUPLE("solaris"));
                    }
                    else
                        fLoadCommands = true;

                    uint32_t u32;
                    for (uint32_t i = 0; i < pThis->cMonitors; ++i)
                    {
                        rc = pHlp->pfnSSMGetU32(pSSM, &u32); /* VHWA [un]available magic. */
                        AssertRCReturn(rc, rc);
                    }

                    /* Pending VHWA commands. */
                    if (fLoadCommands)
                    {
                        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
                        AssertRCReturn(rc, rc);
                        for (uint32_t i = 0; i < u32; ++i)
                        {
                            rc = pHlp->pfnSSMSkip(pSSM, sizeof(uint32_t));
                            AssertRCReturn(rc, rc);
                        }
                    }
                }
            }

#ifdef DEBUG_sunlover
            dumpctx(pCtx);
#endif
        }
    }

    return rc;
}

int vboxVBVALoadStateDone(PPDMDEVINS pDevIns)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);
    if (pCtx)
    {
        uint32_t iView;
        for (iView = 0; iView < pCtx->cViews; iView++)
        {
            VBVAVIEW *pView = &pCtx->aViews[iView];
            if (pView->vbva.guest.pVBVA)
            {
                int rc = vbvaEnable(pThis, pThisCC, pCtx, iView, pView->vbva.guest.pVBVA,
                                    pView->vbva.u32VBVAOffset, true /* fRestored */);
                if (RT_SUCCESS(rc))
                    vbvaResize(pThisCC, pView, &pView->screen, false);
                else
                    LogRel(("VBVA: can not restore: %Rrc\n", rc));
            }
        }

        if (pCtx->mouseShapeInfo.fSet)
            vbvaUpdateMousePointerShape(pThisCC, &pCtx->mouseShapeInfo, true);
    }

    return VINF_SUCCESS;
}

void VBVARaiseIrq(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t fFlags)
{
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSectIRQ, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSectIRQ, rcLock);

    const uint32_t fu32CurrentGuestFlags = HGSMIGetHostGuestFlags(pThisCC->pHGSMI);
    if ((fu32CurrentGuestFlags & HGSMIHOSTFLAGS_IRQ) == 0)
    {
        /* No IRQ set yet. */
        Assert(pThis->fu32PendingGuestFlags == 0);

        HGSMISetHostGuestFlags(pThisCC->pHGSMI, HGSMIHOSTFLAGS_IRQ | fFlags);

        /* If VM is not running, the IRQ will be set in VBVAOnResume. */
        const VMSTATE enmVMState = PDMDevHlpVMState(pDevIns);
        if (   enmVMState == VMSTATE_RUNNING
            || enmVMState == VMSTATE_RUNNING_LS)
            PDMDevHlpPCISetIrqNoWait(pDevIns, 0, PDM_IRQ_LEVEL_HIGH);
    }
    else
    {
        /* IRQ already set, remember the new flags. */
        pThis->fu32PendingGuestFlags |= HGSMIHOSTFLAGS_IRQ | fFlags;
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSectIRQ);
}

void VBVAOnResume(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSectIRQ, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSectIRQ, rcLock);

    if (HGSMIGetHostGuestFlags(pThisCC->pHGSMI) & HGSMIHOSTFLAGS_IRQ)
        PDMDevHlpPCISetIrqNoWait(pDevIns, 0, PDM_IRQ_LEVEL_HIGH);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSectIRQ);
}

static int vbvaHandleQueryConf32(PVGASTATECC pThisCC, VBVACONF32 RT_UNTRUSTED_VOLATILE_GUEST *pConf32)
{
    uint32_t const idxQuery = pConf32->u32Index;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    LogFlowFunc(("VBVA_QUERY_CONF32: u32Index %d, u32Value 0x%x\n", idxQuery, pConf32->u32Value));

    VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);
    uint32_t     uValue;
    if (idxQuery == VBOX_VBVA_CONF32_MONITOR_COUNT)
        uValue = pCtx->cViews;
    else if (idxQuery == VBOX_VBVA_CONF32_HOST_HEAP_SIZE)
        uValue = _64K; /** @todo a value calculated from the vram size */
    else if (   idxQuery == VBOX_VBVA_CONF32_MODE_HINT_REPORTING
             || idxQuery == VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING)
        uValue = VINF_SUCCESS;
    else if (idxQuery == VBOX_VBVA_CONF32_CURSOR_CAPABILITIES)
        uValue = VBOX_VBVA_CURSOR_CAPABILITY_HARDWARE;
    else if (idxQuery == VBOX_VBVA_CONF32_SCREEN_FLAGS)
        uValue = VBVA_SCREEN_F_ACTIVE
               | VBVA_SCREEN_F_DISABLED
               | VBVA_SCREEN_F_BLANK
               | VBVA_SCREEN_F_BLANK2;
    else if (idxQuery == VBOX_VBVA_CONF32_MAX_RECORD_SIZE)
        uValue = VBVA_MAX_RECORD_SIZE;
    else if (idxQuery == UINT32_MAX)
        uValue = UINT32_MAX; /* Older GA uses this for sanity checking. See testQueryConf in HGSMIBase.cpp on branches. */
    else
        ASSERT_GUEST_MSG_FAILED_RETURN(("Invalid index %#x\n", idxQuery), VERR_INVALID_PARAMETER);

    pConf32->u32Value = uValue;
    return VINF_SUCCESS;
}

static int vbvaHandleSetConf32(VBVACONF32 RT_UNTRUSTED_VOLATILE_GUEST *pConf32)
{
    uint32_t const idxQuery = pConf32->u32Index;
    uint32_t const uValue   = pConf32->u32Value;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    LogFlowFunc(("VBVA_SET_CONF32: u32Index %d, u32Value 0x%x\n", idxQuery, uValue));

    if (idxQuery == VBOX_VBVA_CONF32_MONITOR_COUNT)
    { /* do nothing. this is a const. */ }
    else if (idxQuery == VBOX_VBVA_CONF32_HOST_HEAP_SIZE)
    { /* do nothing. this is a const. */ }
    else
        ASSERT_GUEST_MSG_FAILED_RETURN(("Invalid index %#x (value=%u)\n", idxQuery, uValue), VERR_INVALID_PARAMETER);

    RT_NOREF_PV(uValue);
    return VINF_SUCCESS;
}

static int vbvaHandleInfoHeap(PVGASTATECC pThisCC, const VBVAINFOHEAP RT_UNTRUSTED_VOLATILE_GUEST *pInfoHeap)
{
    uint32_t const offHeap = pInfoHeap->u32HeapOffset;
    uint32_t const cbHeap  = pInfoHeap->u32HeapSize;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    LogFlowFunc(("VBVA_INFO_HEAP: offset 0x%x, size 0x%x\n", offHeap, cbHeap));

    return HGSMIHostHeapSetup(pThisCC->pHGSMI, offHeap, cbHeap);
}

static int vbvaInfoView(PVGASTATE pThis, PVGASTATER3 pThisCC, const VBVAINFOVIEW RT_UNTRUSTED_VOLATILE_GUEST *pView)
{
    VBVAINFOVIEW view;
    RT_COPY_VOLATILE(view, *pView);
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

    LogFlowFunc(("VBVA_INFO_VIEW: u32ViewIndex %d, u32ViewOffset 0x%x, u32ViewSize 0x%x, u32MaxScreenSize 0x%x\n",
                 view.u32ViewIndex, view.u32ViewOffset, view.u32ViewSize, view.u32MaxScreenSize));

    VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);
    ASSERT_GUEST_LOGREL_MSG_RETURN(   view.u32ViewIndex     < pCtx->cViews
                                   && view.u32ViewOffset    <= pThis->vram_size
                                   && view.u32ViewSize      <= pThis->vram_size
                                   && view.u32ViewOffset    <= pThis->vram_size - view.u32ViewSize
                                   && view.u32MaxScreenSize <= view.u32ViewSize,
                                   ("index %d(%d), offset 0x%x, size 0x%x, max 0x%x, vram size 0x%x\n",
                                    view.u32ViewIndex, pCtx->cViews, view.u32ViewOffset, view.u32ViewSize,
                                    view.u32MaxScreenSize, pThis->vram_size),
                                   VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    pCtx->aViews[view.u32ViewIndex].view = view;
    return VINF_SUCCESS;
}

static int vbvaInfoScreen(PVGASTATECC pThisCC, const VBVAINFOSCREEN RT_UNTRUSTED_VOLATILE_GUEST *pScreen)
{
    /*
     * Copy input into non-volatile buffer.
     */
    VBVAINFOSCREEN screen;
    RT_COPY_VOLATILE(screen, *pScreen);
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    LogRel2(("VBVA: InfoScreen: [%d] @%d,%d %dx%d, line 0x%x, BPP %d, flags 0x%x\n",
             screen.u32ViewIndex, screen.i32OriginX, screen.i32OriginY,
             screen.u32Width, screen.u32Height,
             screen.u32LineSize, screen.u16BitsPerPixel, screen.u16Flags));

    /*
     * Validate input.
     */
    /* Allow screen.u16BitsPerPixel == 0 because legacy guest code used it for screen blanking. */
    VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);
    ASSERT_GUEST_LOGREL_MSG_RETURN(screen.u32ViewIndex <  pCtx->cViews,
                                   ("Screen index %#x is out of bound (cViews=%#x)\n", screen.u32ViewIndex, pCtx->cViews),
                                    VERR_INVALID_PARAMETER);
    ASSERT_GUEST_LOGREL_MSG_RETURN(   screen.u16BitsPerPixel <= 32
                                   && screen.u32Width        <= UINT16_MAX
                                   && screen.u32Height       <= UINT16_MAX
                                   && screen.u32LineSize     <= UINT16_MAX * UINT32_C(4),
                                   ("One or more values out of range: u16BitsPerPixel=%#x u32Width=%#x u32Height=%#x u32LineSize=%#x\n",
                                    screen.u16BitsPerPixel, screen.u32Width, screen.u32Height, screen.u32LineSize),
                                   VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    const VBVAINFOVIEW *pView = &pCtx->aViews[screen.u32ViewIndex].view;
    const uint32_t      cbPerPixel = (screen.u16BitsPerPixel + 7) / 8;
    ASSERT_GUEST_LOGREL_MSG_RETURN(screen.u32Width <= screen.u32LineSize / (cbPerPixel ? cbPerPixel : 1),
                                   ("u32Width=%#x u32LineSize=%3x cbPerPixel=%#x\n",
                                    screen.u32Width, screen.u32LineSize, cbPerPixel),
                                   VERR_INVALID_PARAMETER);

    const uint64_t u64ScreenSize = (uint64_t)screen.u32LineSize * screen.u32Height;

    ASSERT_GUEST_LOGREL_MSG_RETURN(   screen.u32StartOffset <= pView->u32ViewSize
                                   && u64ScreenSize         <= pView->u32MaxScreenSize
                                   && screen.u32StartOffset <= pView->u32ViewSize - (uint32_t)u64ScreenSize,
                                   ("u32StartOffset=%#x u32ViewSize=%#x u64ScreenSize=%#RX64 u32MaxScreenSize=%#x\n",
                                    screen.u32StartOffset, pView->u32ViewSize, u64ScreenSize, pView->u32MaxScreenSize),
                                   VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /*
     * Do the job.
     */
    vbvaResize(pThisCC, &pCtx->aViews[screen.u32ViewIndex], &screen, true);
    return VINF_SUCCESS;
}

#ifdef UNUSED_FUNCTION
int VBVAGetInfoViewAndScreen(PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t u32ViewIndex, VBVAINFOVIEW *pView, VBVAINFOSCREEN *pScreen)
{
    if (u32ViewIndex >= pThis->cMonitors)
        return VERR_INVALID_PARAMETER;

    PHGSMIINSTANCE pIns = pThisCC->pHGSMI;
    VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext (pIns);

    if (pView)
        *pView = pCtx->aViews[u32ViewIndex].view;

    if (pScreen)
        *pScreen = pCtx->aViews[u32ViewIndex].screen;

    return VINF_SUCCESS;
}
#endif

static int vbvaHandleEnable(PVGASTATE pThis, PVGASTATER3 pThisCC, uint32_t fEnableFlags, uint32_t offEnable, uint32_t idScreen)
{
    LogFlowFunc(("VBVA_ENABLE[%u]: fEnableFlags=0x%x offEnable=%#x\n", idScreen, fEnableFlags, offEnable));
    PHGSMIINSTANCE pIns = pThisCC->pHGSMI;
    VBVACONTEXT   *pCtx = (VBVACONTEXT *)HGSMIContext(pIns);

    /*
     * Validate input.
     */
    ASSERT_GUEST_LOGREL_MSG_RETURN(idScreen < pCtx->cViews, ("idScreen=%#x cViews=%#x\n", idScreen, pCtx->cViews), VERR_INVALID_PARAMETER);
    ASSERT_GUEST_LOGREL_MSG_RETURN(   (fEnableFlags & (VBVA_F_ENABLE | VBVA_F_DISABLE)) == VBVA_F_ENABLE
                                    || (fEnableFlags & (VBVA_F_ENABLE | VBVA_F_DISABLE)) == VBVA_F_DISABLE,
                                   ("fEnableFlags=%#x\n", fEnableFlags),
                                   VERR_INVALID_PARAMETER);
    if (fEnableFlags & VBVA_F_ENABLE)
    {
        ASSERT_GUEST_LOGREL_MSG_RETURN(offEnable < pThis->vram_size,
                                       ("offEnable=%#x vram_size=%#x\n", offEnable, pThis->vram_size),
                                       VERR_INVALID_PARAMETER);
        if (fEnableFlags & VBVA_F_ABSOFFSET)
            /* Offset from VRAM start. */
            ASSERT_GUEST_LOGREL_MSG_RETURN(   pThis->vram_size >= RT_UOFFSETOF(VBVABUFFER, au8Data)
                                           && offEnable <= pThis->vram_size - RT_UOFFSETOF(VBVABUFFER, au8Data),
                                           ("offEnable=%#x vram_size=%#x\n", offEnable, pThis->vram_size),
                                           VERR_INVALID_PARAMETER);
        else
        {
            /* Offset from the view start.  We'd be using idScreen here to fence required. */
            RT_UNTRUSTED_VALIDATED_FENCE();
            const VBVAINFOVIEW *pView = &pCtx->aViews[idScreen].view;
            ASSERT_GUEST_LOGREL_MSG_RETURN(   pThis->vram_size - offEnable >= pView->u32ViewOffset
                                           && pView->u32ViewSize >= RT_UOFFSETOF(VBVABUFFER, au8Data)
                                           && offEnable <= pView->u32ViewSize - RT_UOFFSETOF(VBVABUFFER, au8Data),
                                           ("offEnable=%#x vram_size=%#x view: %#x LB %#x\n",
                                            offEnable, pThis->vram_size, pView->u32ViewOffset, pView->u32ViewSize),
                                           VERR_INVALID_PARAMETER);
            offEnable += pView->u32ViewOffset;
        }
        ASSERT_GUEST_LOGREL_MSG_RETURN(HGSMIIsOffsetValid(pIns, offEnable),
                                       ("offEnable=%#x area %#x LB %#x\n",
                                        offEnable, HGSMIGetAreaOffset(pIns), HGSMIGetAreaSize(pIns)),
                                       VERR_INVALID_PARAMETER);
    }
    RT_UNTRUSTED_VALIDATED_FENCE();

    /*
     * Execute.
     */
    int rc = VINF_SUCCESS;
    if (fEnableFlags & VBVA_F_ENABLE)
    {
        VBVABUFFER RT_UNTRUSTED_VOLATILE_GUEST *pVBVA
            = (VBVABUFFER RT_UNTRUSTED_VOLATILE_GUEST *)HGSMIOffsetToPointerHost(pIns, offEnable);
        ASSERT_GUEST_LOGREL_RETURN(pVBVA, VERR_INVALID_PARAMETER); /* already check above, but let's be careful. */

        /* Process any pending orders and empty the VBVA ring buffer. */
        vbvaFlush(pThis, pThisCC, pCtx);

        rc = vbvaEnable(pThis, pThisCC, pCtx, idScreen, pVBVA, offEnable, false /* fRestored */);
        if (RT_FAILURE(rc))
            LogRelMax(8, ("VBVA: can not enable: %Rrc\n", rc));
    }
    else
        rc = vbvaDisable(pThis, pThisCC, pCtx, idScreen);
    return rc;
}

static int vbvaHandleQueryModeHints(PVGASTATECC pThisCC, VBVAQUERYMODEHINTS volatile *pQueryModeHints, HGSMISIZE cbBuffer)
{
    PHGSMIINSTANCE pIns = pThisCC->pHGSMI;
    VBVACONTEXT   *pCtx = (VBVACONTEXT *)HGSMIContext(pIns);

    /*
     * Copy and validate the request.
     */
    uint16_t const cHintsQueried         = pQueryModeHints->cHintsQueried;
    uint16_t const cbHintStructureGuest  = pQueryModeHints->cbHintStructureGuest;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

    LogRelFlowFunc(("VBVA: HandleQueryModeHints: cHintsQueried=%RU16, cbHintStructureGuest=%RU16\n",
                    cHintsQueried, cbHintStructureGuest));
    ASSERT_GUEST_RETURN(cbBuffer >= sizeof(VBVAQUERYMODEHINTS) + (uint32_t)cHintsQueried * cbHintStructureGuest,
                        VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    /*
     * Produce the requested data.
     */
    uint8_t *pbHint = (uint8_t *)(pQueryModeHints + 1);
    memset(pbHint, ~0, cbBuffer - sizeof(VBVAQUERYMODEHINTS));

    for (unsigned iHint = 0; iHint < cHintsQueried && iHint < VBOX_VIDEO_MAX_SCREENS; ++iHint)
    {
        memcpy(pbHint, &pCtx->aModeHints[iHint], RT_MIN(cbHintStructureGuest, sizeof(VBVAMODEHINT)));
        pbHint += cbHintStructureGuest;
        Assert((uintptr_t)(pbHint - (uint8_t *)pQueryModeHints) <= cbBuffer);
    }

    return VINF_SUCCESS;
}

/*
 *
 * New VBVA uses a new interface id: #define VBE_DISPI_ID_VBOX_VIDEO         0xBE01
 *
 * VBVA uses two 32 bits IO ports to write VRAM offsets of shared memory blocks for commands.
 *                                 Read                        Write
 * Host port 0x3b0                 to process                  completed
 * Guest port 0x3d0                control value?              to process
 *
 */

static DECLCALLBACK(void) vbvaNotifyGuest(void *pvCallback)
{
#if defined(VBOX_WITH_HGSMI) && (defined(VBOX_WITH_VDMA) || defined(VBOX_WITH_WDDM))
    PPDMDEVINS pDevIns = (PPDMDEVINS)pvCallback;
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    VBVARaiseIrq(pDevIns, pThis, pThisCC, 0);
#else
    NOREF(pvCallback);
    /* Do nothing. Later the VMMDev/VGA IRQ can be used for the notification. */
#endif
}

/**
 * The guest submitted a command buffer (hit VGA_PORT_HGSMI_GUEST).
 *
 * Verify the buffer size and invoke corresponding handler.
 *
 * @return VBox status code.
 * @param pvHandler      The VBVA channel context.
 * @param u16ChannelInfo Command code.
 * @param pvBuffer       HGSMI buffer with command data.  Considered volatile!
 * @param cbBuffer       Size of command data.
 *
 * @thread EMT
 */
static DECLCALLBACK(int) vbvaChannelHandler(void *pvHandler, uint16_t u16ChannelInfo,
                                            void RT_UNTRUSTED_VOLATILE_GUEST *pvBuffer, HGSMISIZE cbBuffer)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pvHandler %p, u16ChannelInfo %d, pvBuffer %p, cbBuffer %u\n", pvHandler, u16ChannelInfo, pvBuffer, cbBuffer));

    PPDMDEVINS      pDevIns = (PPDMDEVINS)pvHandler;
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PHGSMIINSTANCE  pIns    = pThisCC->pHGSMI;
    VBVACONTEXT    *pCtx    = (VBVACONTEXT *)HGSMIContext(pIns);

    if (pThis->fVMSVGAEnabled)
    {
        if (   u16ChannelInfo == VBVA_QUERY_CONF32
            || u16ChannelInfo == VBVA_INFO_HEAP
            || u16ChannelInfo == VBVA_MOUSE_POINTER_SHAPE
            || u16ChannelInfo == VBVA_INFO_CAPS
            || u16ChannelInfo == VBVA_CURSOR_POSITION
           )
        { /* allowed */ }
        else
        {
           /* NOPs for VMSVGA mode. */
           return VINF_SUCCESS;
        }
    }

    switch (u16ChannelInfo)
    {
#ifdef VBOX_WITH_VDMA
        case VBVA_VDMA_CMD:
            if (cbBuffer >= VBoxSHGSMIBufferHeaderSize() + sizeof(VBOXVDMACBUF_DR))
            {
                VBOXVDMACBUF_DR RT_UNTRUSTED_VOLATILE_GUEST *pCmd
                    = (VBOXVDMACBUF_DR RT_UNTRUSTED_VOLATILE_GUEST *)VBoxSHGSMIBufferData((VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer);
                vboxVDMACommand(pThisCC->pVdma, pCmd, cbBuffer - VBoxSHGSMIBufferHeaderSize());
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VBVA_VDMA_CTL:
            if (cbBuffer >= VBoxSHGSMIBufferHeaderSize() + sizeof(VBOXVDMA_CTL))
            {
                VBOXVDMA_CTL RT_UNTRUSTED_VOLATILE_GUEST *pCmd
                    = (VBOXVDMA_CTL RT_UNTRUSTED_VOLATILE_GUEST *)VBoxSHGSMIBufferData((VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer);
                vboxVDMAControl(pThisCC->pVdma, pCmd, cbBuffer - VBoxSHGSMIBufferHeaderSize());
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;
#endif /* VBOX_WITH_VDMA */

        case VBVA_QUERY_CONF32:
            if (cbBuffer >= sizeof(VBVACONF32))
                rc = vbvaHandleQueryConf32(pThisCC, (VBVACONF32 RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VBVA_SET_CONF32:
            if (cbBuffer >= sizeof(VBVACONF32))
                rc = vbvaHandleSetConf32((VBVACONF32 RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VBVA_INFO_VIEW:
            /* Expect at least one VBVAINFOVIEW structure. */
            rc = VERR_INVALID_PARAMETER;
            if (cbBuffer >= sizeof(VBVAINFOVIEW))
            {
                /* Guest submits an array of VBVAINFOVIEW structures. */
                const VBVAINFOVIEW RT_UNTRUSTED_VOLATILE_GUEST *pView = (VBVAINFOVIEW RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer;
                for (;
                     cbBuffer >= sizeof(VBVAINFOVIEW);
                     ++pView, cbBuffer -= sizeof(VBVAINFOVIEW))
                {
                    rc = vbvaInfoView(pThis, pThisCC, pView);
                    if (RT_FAILURE(rc))
                        break;
                }
            }
            break;

        case VBVA_INFO_HEAP:
            if (cbBuffer >= sizeof(VBVAINFOHEAP))
                rc = vbvaHandleInfoHeap(pThisCC, (VBVAINFOHEAP RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VBVA_FLUSH:
            if (cbBuffer >= sizeof(VBVAFLUSH))
                rc = vbvaFlush(pThis, pThisCC, pCtx);
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VBVA_INFO_SCREEN:
            rc = VERR_INVALID_PARAMETER;
            if (cbBuffer >= sizeof(VBVAINFOSCREEN))
                rc = vbvaInfoScreen(pThisCC, (VBVAINFOSCREEN RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer);
            break;

        case VBVA_ENABLE:
            rc = VERR_INVALID_PARAMETER;
            if (cbBuffer >= sizeof(VBVAENABLE))
            {
                VBVAENABLE RT_UNTRUSTED_VOLATILE_GUEST *pVbvaEnable = (VBVAENABLE RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer;
                uint32_t const fEnableFlags = pVbvaEnable->u32Flags;
                uint32_t const offEnable    = pVbvaEnable->u32Offset;
                RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

                uint32_t idScreen;
                if (fEnableFlags & VBVA_F_EXTENDED)
                {
                    ASSERT_GUEST_STMT_BREAK(cbBuffer >= sizeof(VBVAENABLE_EX), rc = VERR_INVALID_PARAMETER);
                    idScreen = ((VBVAENABLE_EX RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer)->u32ScreenId;
                    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
                }
                else
                    idScreen = vbvaViewFromBufferPtr(pIns, pCtx, pvBuffer);

                rc = vbvaHandleEnable(pThis, pThisCC, fEnableFlags, offEnable, idScreen);
                pVbvaEnable->i32Result = rc;
            }
            break;

        case VBVA_MOUSE_POINTER_SHAPE:
            if (cbBuffer >= sizeof(VBVAMOUSEPOINTERSHAPE))
            {
                VBVAMOUSEPOINTERSHAPE RT_UNTRUSTED_VOLATILE_GUEST *pShape
                    = (VBVAMOUSEPOINTERSHAPE RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer;
                rc = vbvaMousePointerShape(pThisCC, pCtx, pShape, cbBuffer);
                pShape->i32Result = rc;
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;

#ifdef VBOX_WITH_WDDM
        case VBVA_INFO_CAPS:
            if (cbBuffer >= sizeof(VBVACAPS))
            {
                VBVACAPS RT_UNTRUSTED_VOLATILE_GUEST *pCaps = (VBVACAPS RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer;
                pThis->fGuestCaps = pCaps->fCaps;
                RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

                pThisCC->pDrv->pfnVBVAGuestCapabilityUpdate(pThisCC->pDrv, pThis->fGuestCaps);
                pCaps->rc = rc = VINF_SUCCESS;
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;
#endif

        case VBVA_SCANLINE_CFG:
            if (cbBuffer >= sizeof(VBVASCANLINECFG))
            {
                VBVASCANLINECFG RT_UNTRUSTED_VOLATILE_GUEST *pCfg = (VBVASCANLINECFG RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer;
                pThis->fScanLineCfg = pCfg->fFlags;
                RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

                pCfg->rc = rc = VINF_SUCCESS;
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VBVA_QUERY_MODE_HINTS:
            if (cbBuffer >= sizeof(VBVAQUERYMODEHINTS))
            {
                VBVAQUERYMODEHINTS RT_UNTRUSTED_VOLATILE_GUEST *pQueryModeHints
                    = (VBVAQUERYMODEHINTS RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer;
                rc = vbvaHandleQueryModeHints(pThisCC, pQueryModeHints, cbBuffer);
                pQueryModeHints->rc = rc;
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VBVA_REPORT_INPUT_MAPPING:
            if (cbBuffer >= sizeof(VBVAREPORTINPUTMAPPING))
            {
                VBVAREPORTINPUTMAPPING inputMapping;
                {
                    VBVAREPORTINPUTMAPPING RT_UNTRUSTED_VOLATILE_GUEST *pInputMapping
                        = (VBVAREPORTINPUTMAPPING RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer;
                    inputMapping.x  = pInputMapping->x;
                    inputMapping.y  = pInputMapping->y;
                    inputMapping.cx = pInputMapping->cx;
                    inputMapping.cy = pInputMapping->cy;
                }
                RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

                LogRelFlowFunc(("VBVA: ChannelHandler: VBVA_REPORT_INPUT_MAPPING: x=%RI32, y=%RI32, cx=%RU32, cy=%RU32\n",
                                inputMapping.x, inputMapping.y, inputMapping.cx, inputMapping.cy));
                pThisCC->pDrv->pfnVBVAInputMappingUpdate(pThisCC->pDrv,
                                                           inputMapping.x, inputMapping.y,
                                                           inputMapping.cx, inputMapping.cy);
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        case VBVA_CURSOR_POSITION:
            if (cbBuffer >= sizeof(VBVACURSORPOSITION))
            {
                VBVACURSORPOSITION RT_UNTRUSTED_VOLATILE_GUEST *pReport = (VBVACURSORPOSITION RT_UNTRUSTED_VOLATILE_GUEST *)pvBuffer;
                VBVACURSORPOSITION Report;
                Report.fReportPosition = pReport->fReportPosition;
                Report.x               = pReport->x;
                Report.y               = pReport->y;
                RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

                LogRelFlowFunc(("VBVA: ChannelHandler: VBVA_CURSOR_POSITION: fReportPosition=%RTbool, Id=%RU32, x=%RU32, y=%RU32\n",
                                RT_BOOL(Report.fReportPosition), vbvaViewFromBufferPtr(pIns, pCtx, pvBuffer), Report.x, Report.y));

                pThisCC->pDrv->pfnVBVAReportCursorPosition(pThisCC->pDrv, RT_BOOL(Report.fReportPosition), vbvaViewFromBufferPtr(pIns, pCtx, pvBuffer), Report.x, Report.y);
                /* This was only ever briefly used by the guest, and a value
                 * of zero in both was taken to mean "ignore". */
                pReport->x = 0;
                pReport->y = 0;
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;

        default:
            Log(("Unsupported VBVA guest command %d (%#x)!!!\n", u16ChannelInfo, u16ChannelInfo));
            break;
    }

    return rc;
}

/** When VBVA is paused, the VGA device is allowed to work but
 * no HGSMI etc state is changed.
 */
static void vbvaPause(PVGASTATECC pThisCC, bool fPause)
{
    if (!pThisCC || !pThisCC->pHGSMI)
        return;

    VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);
    if (pCtx)
        pCtx->fPaused = fPause;
}

bool VBVAIsPaused(PVGASTATECC pThisCC)
{
    if (pThisCC && pThisCC->pHGSMI)
    {
        const VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);
        if (pCtx && pCtx->cViews)
        {
            /* If VBVA is enabled at all. */
            const VBVAVIEW *pView = &pCtx->aViews[0];
            if (pView->vbva.guest.pVBVA)
                return pCtx->fPaused;
        }
    }
    /* VBVA is disabled. */
    return true;
}

void VBVAOnVBEChanged(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    /* The guest does not depend on host handling the VBE registers. */
    if (pThis->fGuestCaps & VBVACAPS_USE_VBVA_ONLY)
        return;

    vbvaPause(pThisCC, (pThis->vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED) == 0);
}

void VBVAReset(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
    if (!pThis || !pThisCC->pHGSMI)
        return;

    VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);

    HGSMIReset(pThisCC->pHGSMI);
    /* Make sure the IRQ is reset. */
    PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_LOW);
    pThis->fu32PendingGuestFlags = 0;

    if (pCtx)
    {
        vbvaFlush(pThis, pThisCC, pCtx);

        for (unsigned idScreen = 0; idScreen < pCtx->cViews; idScreen++)
            vbvaDisable(pThis, pThisCC, pCtx, idScreen);

        pCtx->mouseShapeInfo.fSet = false;
        RTMemFreeZ(pCtx->mouseShapeInfo.pu8Shape, pCtx->mouseShapeInfo.cbAllocated);
        pCtx->mouseShapeInfo.pu8Shape = NULL;
        pCtx->mouseShapeInfo.cbAllocated = 0;
        pCtx->mouseShapeInfo.cbShape = 0;
    }

}

int VBVAUpdateDisplay(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    int rc = VERR_NOT_SUPPORTED; /* Assuming that the VGA device will have to do updates. */

    VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);
    if (pCtx)
    {
        if (!pCtx->fPaused)
        {
            rc = vbvaFlush(pThis, pThisCC, pCtx);
            if (RT_SUCCESS(rc))
            {
                if (!pCtx->aViews[0].vbva.guest.pVBVA)
                {
                    /* VBVA is not enabled for the first view, so VGA device must do updates. */
                    rc = VERR_NOT_SUPPORTED;
                }
            }
        }
    }

    return rc;
}

static int vbvaSendModeHintWorker(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC,
                                  uint32_t cx, uint32_t cy, uint32_t cBPP, uint32_t iDisplay, uint32_t dx,
                                  uint32_t dy, uint32_t fEnabled,
                                  uint32_t fNotifyGuest)
{
    VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);
    /** @note See Display::setVideoModeHint: "It is up to the guest to decide
     *  whether the hint is valid. Therefore don't do any VRAM sanity checks
     *  here! */
    if (iDisplay >= RT_MIN(pThis->cMonitors, RT_ELEMENTS(pCtx->aModeHints)))
        return VERR_OUT_OF_RANGE;
    pCtx->aModeHints[iDisplay].magic    = VBVAMODEHINT_MAGIC;
    pCtx->aModeHints[iDisplay].cx       = cx;
    pCtx->aModeHints[iDisplay].cy       = cy;
    pCtx->aModeHints[iDisplay].cBPP     = cBPP;
    pCtx->aModeHints[iDisplay].dx       = dx;
    pCtx->aModeHints[iDisplay].dy       = dy;
    pCtx->aModeHints[iDisplay].fEnabled = fEnabled;
    if (fNotifyGuest && pThis->fGuestCaps & VBVACAPS_IRQ && pThis->fGuestCaps & VBVACAPS_VIDEO_MODE_HINTS)
        VBVARaiseIrq(pDevIns, pThis, pThisCC, HGSMIHOSTFLAGS_HOTPLUG);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnSendModeHint}
 */
DECLCALLBACK(int) vbvaR3PortSendModeHint(PPDMIDISPLAYPORT pInterface,  uint32_t cx, uint32_t cy, uint32_t cBPP,
                                         uint32_t iDisplay, uint32_t dx, uint32_t dy, uint32_t fEnabled, uint32_t fNotifyGuest)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_SEM_BUSY);
    AssertRCReturn(rc, rc);

    rc = vbvaSendModeHintWorker(pDevIns, pThis, pThisCC, cx, cy, cBPP, iDisplay, dx, dy, fEnabled, fNotifyGuest);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}

int VBVAInit(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
    int rc = HGSMICreate(&pThisCC->pHGSMI,
                         pDevIns,
                         "VBVA",
                         0,
                         pThisCC->pbVRam,
                         pThis->vram_size,
                         vbvaNotifyGuest,
                         pDevIns,
                         sizeof(VBVACONTEXT));
     if (RT_SUCCESS(rc))
     {
         rc = HGSMIHostChannelRegister(pThisCC->pHGSMI,
                                       HGSMI_CH_VBVA,
                                       vbvaChannelHandler,
                                       pDevIns);
         if (RT_SUCCESS(rc))
         {
             VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pThisCC->pHGSMI);
             pCtx->cViews = pThis->cMonitors;
             pCtx->fPaused = true;
             memset(pCtx->aModeHints, ~0, sizeof(pCtx->aModeHints));
         }
     }

     return rc;

}

void VBVADestroy(PVGASTATECC pThisCC)
{
    PHGSMIINSTANCE pHgsmi = pThisCC->pHGSMI;
    if (pHgsmi)
    {
        VBVACONTEXT *pCtx = (VBVACONTEXT *)HGSMIContext(pHgsmi);
        pCtx->mouseShapeInfo.fSet = false;
        RTMemFreeZ(pCtx->mouseShapeInfo.pu8Shape, pCtx->mouseShapeInfo.cbAllocated);
        pCtx->mouseShapeInfo.pu8Shape = NULL;
        pCtx->mouseShapeInfo.cbAllocated = 0;
        pCtx->mouseShapeInfo.cbShape = 0;

        HGSMIDestroy(pHgsmi);
        pThisCC->pHGSMI = NULL;
    }
}

