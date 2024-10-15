/* $Id: DisplayImplLegacy.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox IDisplay implementation, helpers for legacy GAs.
 *
 * Methods and helpers to support old Guest Additions 3.x or older.
 * This is not used by the current Guest Additions.
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

#define LOG_GROUP LOG_GROUP_MAIN_DISPLAY
#include "LoggingNew.h"

#include "DisplayImpl.h"
#include "ConsoleImpl.h"
#include "ConsoleVRDPServer.h"
#include "VMMDev.h"
#include <VBox/VMMDev.h>

/* generated header */
#include "VBoxEvents.h"


int videoAccelConstruct(VIDEOACCEL *pVideoAccel)
{
    pVideoAccel->pVbvaMemory = NULL;
    pVideoAccel->fVideoAccelEnabled = false;

    pVideoAccel->pu8VbvaPartial = NULL;
    pVideoAccel->cbVbvaPartial = 0;

    pVideoAccel->hXRoadsVideoAccel = NIL_RTSEMXROADS;
    int vrc = RTSemXRoadsCreate(&pVideoAccel->hXRoadsVideoAccel);
    AssertRC(vrc);

    return vrc;
}

void videoAccelDestroy(VIDEOACCEL *pVideoAccel)
{
    RTSemXRoadsDestroy(pVideoAccel->hXRoadsVideoAccel);
    RT_ZERO(*pVideoAccel);
}

static unsigned mapCoordsToScreen(DISPLAYFBINFO *pInfos, unsigned cInfos, int *px, int *py, int *pw, int *ph)
{
    RT_NOREF(pw, ph);

    DISPLAYFBINFO *pInfo = pInfos;
    unsigned uScreenId;
    Log9(("mapCoordsToScreen: %d,%d %dx%d\n", *px, *py, *pw, *ph));
    for (uScreenId = 0; uScreenId < cInfos; uScreenId++, pInfo++)
    {
        Log9(("    [%d] %d,%d %dx%d\n", uScreenId, pInfo->xOrigin, pInfo->yOrigin, pInfo->w, pInfo->h));
        if (   (pInfo->xOrigin <= *px && *px < pInfo->xOrigin + (int)pInfo->w)
            && (pInfo->yOrigin <= *py && *py < pInfo->yOrigin + (int)pInfo->h))
        {
            /* The rectangle belongs to the screen. Correct coordinates. */
            *px -= pInfo->xOrigin;
            *py -= pInfo->yOrigin;
            Log9(("    -> %d,%d", *px, *py));
            break;
        }
    }
    if (uScreenId == cInfos)
    {
        /* Map to primary screen. */
        uScreenId = 0;
    }
    Log9((" scr %d\n", uScreenId));
    return uScreenId;
}


typedef struct _VBVADIRTYREGION
{
    /* Copies of object's pointers used by vbvaRgn functions. */
    DISPLAYFBINFO    *paFramebuffers;
    unsigned          cMonitors;
    Display          *pDisplay;
    PPDMIDISPLAYPORT  pPort;

    /* The rectangle that includes all dirty rectangles. */
    RTRECT aDirtyRects[SchemaDefs::MaxGuestMonitors];

} VBVADIRTYREGION;

static void vbvaRgnInit(VBVADIRTYREGION *prgn, DISPLAYFBINFO *paFramebuffers, unsigned cMonitors,
                        Display *pd, PPDMIDISPLAYPORT pp)
{
    prgn->paFramebuffers = paFramebuffers;
    prgn->cMonitors = cMonitors;
    prgn->pDisplay = pd;
    prgn->pPort = pp;

    RT_ZERO(prgn->aDirtyRects);
}

static void vbvaRgnDirtyRect(VBVADIRTYREGION *prgn, unsigned uScreenId, VBVACMDHDR *phdr)
{
    Log9(("x = %d, y = %d, w = %d, h = %d\n", phdr->x, phdr->y, phdr->w, phdr->h));

    /*
     * Here update rectangles are accumulated to form an update area.
     */
    /** @todo
     * Now the simplest method is used which builds one rectangle that
     * includes all update areas. A bit more advanced method can be
     * employed here. The method should be fast however.
     */
    if (phdr->w == 0 || phdr->h == 0)
    {
        /* Empty rectangle. */
        return;
    }

    int32_t xRight  = phdr->x + phdr->w;
    int32_t yBottom = phdr->y + phdr->h;

    RTRECT *pDirtyRect = &prgn->aDirtyRects[uScreenId];
    DISPLAYFBINFO *pFBInfo = &prgn->paFramebuffers[uScreenId];

    if (pDirtyRect->xRight == 0)
    {
        /* This is the first rectangle to be added. */
        pDirtyRect->xLeft   = phdr->x;
        pDirtyRect->yTop    = phdr->y;
        pDirtyRect->xRight  = xRight;
        pDirtyRect->yBottom = yBottom;
    }
    else
    {
        /* Adjust region coordinates. */
        if (pDirtyRect->xLeft > phdr->x)
        {
            pDirtyRect->xLeft = phdr->x;
        }

        if (pDirtyRect->yTop > phdr->y)
        {
            pDirtyRect->yTop = phdr->y;
        }

        if (pDirtyRect->xRight < xRight)
        {
            pDirtyRect->xRight = xRight;
        }

        if (pDirtyRect->yBottom < yBottom)
        {
            pDirtyRect->yBottom = yBottom;
        }
    }

    if (pFBInfo->fDefaultFormat)
    {
        /// @todo pfnUpdateDisplayRect must take the vram offset parameter for the framebuffer
        prgn->pPort->pfnUpdateDisplayRect(prgn->pPort, phdr->x, phdr->y, phdr->w, phdr->h);
        prgn->pDisplay->i_handleDisplayUpdate(uScreenId, phdr->x, phdr->y, phdr->w, phdr->h);
    }

    return;
}

static void vbvaRgnUpdateFramebuffer(VBVADIRTYREGION *prgn, unsigned uScreenId)
{
    RTRECT *pDirtyRect = &prgn->aDirtyRects[uScreenId];
    DISPLAYFBINFO *pFBInfo = &prgn->paFramebuffers[uScreenId];

    uint32_t w = pDirtyRect->xRight - pDirtyRect->xLeft;
    uint32_t h = pDirtyRect->yBottom - pDirtyRect->yTop;

    if (!pFBInfo->fDefaultFormat && w != 0 && h != 0)
    {
        /// @todo pfnUpdateDisplayRect must take the vram offset parameter for the framebuffer
        prgn->pPort->pfnUpdateDisplayRect(prgn->pPort, pDirtyRect->xLeft, pDirtyRect->yTop, w, h);
        prgn->pDisplay->i_handleDisplayUpdate(uScreenId, pDirtyRect->xLeft, pDirtyRect->yTop, w, h);
    }
}

void i_vbvaSetMemoryFlags(VBVAMEMORY *pVbvaMemory,
                          bool fVideoAccelEnabled,
                          bool fVideoAccelVRDP,
                          uint32_t fu32SupportedOrders,
                          DISPLAYFBINFO *paFBInfos,
                          unsigned cFBInfos)
{
    if (pVbvaMemory)
    {
        /* This called only on changes in mode. So reset VRDP always. */
        uint32_t fu32Flags = VBVA_F_MODE_VRDP_RESET;

        if (fVideoAccelEnabled)
        {
            fu32Flags |= VBVA_F_MODE_ENABLED;

            if (fVideoAccelVRDP)
            {
                fu32Flags |= VBVA_F_MODE_VRDP | VBVA_F_MODE_VRDP_ORDER_MASK;

                pVbvaMemory->fu32SupportedOrders = fu32SupportedOrders;
            }
        }

        pVbvaMemory->fu32ModeFlags = fu32Flags;
    }

    unsigned uScreenId;
    for (uScreenId = 0; uScreenId < cFBInfos; uScreenId++)
    {
        if (paFBInfos[uScreenId].pHostEvents)
        {
            paFBInfos[uScreenId].pHostEvents->fu32Events |= VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET;
        }
    }
}

bool Display::i_VideoAccelAllowed(void)
{
    return true;
}

int videoAccelEnterVGA(VIDEOACCEL *pVideoAccel)
{
    return RTSemXRoadsNSEnter(pVideoAccel->hXRoadsVideoAccel);
}

void videoAccelLeaveVGA(VIDEOACCEL *pVideoAccel)
{
    RTSemXRoadsNSLeave(pVideoAccel->hXRoadsVideoAccel);
}

int videoAccelEnterVMMDev(VIDEOACCEL *pVideoAccel)
{
    return RTSemXRoadsEWEnter(pVideoAccel->hXRoadsVideoAccel);
}

void videoAccelLeaveVMMDev(VIDEOACCEL *pVideoAccel)
{
    RTSemXRoadsEWLeave(pVideoAccel->hXRoadsVideoAccel);
}

/**
 * @thread EMT
 */
int Display::i_VideoAccelEnable(bool fEnable, VBVAMEMORY *pVbvaMemory, PPDMIDISPLAYPORT pUpPort)
{
    LogRelFlowFunc(("fEnable = %d\n", fEnable));

    int vrc = i_videoAccelEnable(fEnable, pVbvaMemory, pUpPort);

    LogRelFlowFunc(("%Rrc.\n", vrc));
    return vrc;
}

int Display::i_videoAccelEnable(bool fEnable, VBVAMEMORY *pVbvaMemory, PPDMIDISPLAYPORT pUpPort)
{
    VIDEOACCEL *pVideoAccel = &mVideoAccelLegacy;

    /* Called each time the guest wants to use acceleration,
     * or when the VGA device disables acceleration,
     * or when restoring the saved state with accel enabled.
     *
     * VGA device disables acceleration on each video mode change
     * and on reset.
     *
     * Guest enabled acceleration at will. And it has to enable
     * acceleration after a mode change.
     */
    LogRelFlowFunc(("mfVideoAccelEnabled = %d, fEnable = %d, pVbvaMemory = %p\n",
                  pVideoAccel->fVideoAccelEnabled, fEnable, pVbvaMemory));

    /* Strictly check parameters. Callers must not pass anything in the case. */
    Assert((fEnable && pVbvaMemory) || (!fEnable && pVbvaMemory == NULL));

    if (!i_VideoAccelAllowed ())
        return VERR_NOT_SUPPORTED;

    /* Check that current status is not being changed */
    if (pVideoAccel->fVideoAccelEnabled == fEnable)
        return VINF_SUCCESS;

    if (pVideoAccel->fVideoAccelEnabled)
    {
        /* Process any pending orders and empty the VBVA ring buffer. */
        i_videoAccelFlush (pUpPort);
    }

    if (!fEnable && pVideoAccel->pVbvaMemory)
        pVideoAccel->pVbvaMemory->fu32ModeFlags &= ~VBVA_F_MODE_ENABLED;

    if (fEnable)
    {
        /* Process any pending VGA device changes, resize. */
        pUpPort->pfnUpdateDisplayAll(pUpPort, /* fFailOnResize = */ false);
    }

    /* Protect the videoaccel state transition. */
    RTCritSectEnter(&mVideoAccelLock);

    if (fEnable)
    {
        /* Initialize the hardware memory. */
        i_vbvaSetMemoryFlags(pVbvaMemory, true, mfVideoAccelVRDP,
                             mfu32SupportedOrders, maFramebuffers, mcMonitors);
        pVbvaMemory->off32Data = 0;
        pVbvaMemory->off32Free = 0;

        memset(pVbvaMemory->aRecords, 0, sizeof(pVbvaMemory->aRecords));
        pVbvaMemory->indexRecordFirst = 0;
        pVbvaMemory->indexRecordFree = 0;

        pVideoAccel->pVbvaMemory = pVbvaMemory;
        pVideoAccel->fVideoAccelEnabled = true;

        LogRel(("VBVA: Enabled.\n"));
    }
    else
    {
        pVideoAccel->pVbvaMemory = NULL;
        pVideoAccel->fVideoAccelEnabled = false;

        LogRel(("VBVA: Disabled.\n"));
    }

    RTCritSectLeave(&mVideoAccelLock);

    if (!fEnable)
    {
        pUpPort->pfnUpdateDisplayAll(pUpPort, /* fFailOnResize = */ false);
    }

    /* Notify the VMMDev, which saves VBVA status in the saved state,
     * and needs to know current status.
     */
    VMMDev *pVMMDev = mParent->i_getVMMDev();
    if (pVMMDev)
    {
        PPDMIVMMDEVPORT pVMMDevPort = pVMMDev->getVMMDevPort();
        if (pVMMDevPort)
            pVMMDevPort->pfnVBVAChange(pVMMDevPort, fEnable);
    }

    LogRelFlowFunc(("VINF_SUCCESS.\n"));
    return VINF_SUCCESS;
}

static bool i_vbvaVerifyRingBuffer(VBVAMEMORY *pVbvaMemory)
{
    RT_NOREF(pVbvaMemory);
    return true;
}

static void i_vbvaFetchBytes(VBVAMEMORY *pVbvaMemory, uint8_t *pu8Dst, uint32_t cbDst)
{
    if (cbDst >= VBVA_RING_BUFFER_SIZE)
    {
        AssertMsgFailed(("cbDst = 0x%08X, ring buffer size 0x%08X\n", cbDst, VBVA_RING_BUFFER_SIZE));
        return;
    }

    uint32_t u32BytesTillBoundary = VBVA_RING_BUFFER_SIZE - pVbvaMemory->off32Data;
    uint8_t  *src                 = &pVbvaMemory->au8RingBuffer[pVbvaMemory->off32Data];
    int32_t i32Diff               = cbDst - u32BytesTillBoundary;

    if (i32Diff <= 0)
    {
        /* Chunk will not cross buffer boundary. */
        memcpy (pu8Dst, src, cbDst);
    }
    else
    {
        /* Chunk crosses buffer boundary. */
        memcpy(pu8Dst, src, u32BytesTillBoundary);
        memcpy(pu8Dst + u32BytesTillBoundary, &pVbvaMemory->au8RingBuffer[0], i32Diff);
    }

    /* Advance data offset. */
    pVbvaMemory->off32Data = (pVbvaMemory->off32Data + cbDst) % VBVA_RING_BUFFER_SIZE;

    return;
}


static bool i_vbvaPartialRead(uint8_t **ppu8, uint32_t *pcb, uint32_t cbRecord, VBVAMEMORY *pVbvaMemory)
{
    uint8_t *pu8New;

    LogFlow(("MAIN::DisplayImpl::vbvaPartialRead: p = %p, cb = %d, cbRecord 0x%08X\n",
             *ppu8, *pcb, cbRecord));

    if (*ppu8)
    {
        Assert (*pcb);
        pu8New = (uint8_t *)RTMemRealloc(*ppu8, cbRecord);
    }
    else
    {
        Assert (!*pcb);
        pu8New = (uint8_t *)RTMemAlloc(cbRecord);
    }

    if (!pu8New)
    {
        /* Memory allocation failed, fail the function. */
        Log(("MAIN::vbvaPartialRead: failed to (re)alocate memory for partial record!!! cbRecord 0x%08X\n",
             cbRecord));

        if (*ppu8)
        {
            RTMemFree(*ppu8);
        }

        *ppu8 = NULL;
        *pcb = 0;

        return false;
    }

    /* Fetch data from the ring buffer. */
    i_vbvaFetchBytes(pVbvaMemory, pu8New + *pcb, cbRecord - *pcb);

    *ppu8 = pu8New;
    *pcb = cbRecord;

    return true;
}

/* For contiguous chunks just return the address in the buffer.
 * For crossing boundary - allocate a buffer from heap.
 */
static bool i_vbvaFetchCmd(VIDEOACCEL *pVideoAccel, VBVACMDHDR **ppHdr, uint32_t *pcbCmd)
{
    VBVAMEMORY *pVbvaMemory = pVideoAccel->pVbvaMemory;

    uint32_t indexRecordFirst = pVbvaMemory->indexRecordFirst;
    uint32_t indexRecordFree = pVbvaMemory->indexRecordFree;

#ifdef DEBUG_sunlover
    LogFlowFunc(("first = %d, free = %d\n",
                 indexRecordFirst, indexRecordFree));
#endif /* DEBUG_sunlover */

    if (!i_vbvaVerifyRingBuffer(pVbvaMemory))
    {
        return false;
    }

    if (indexRecordFirst == indexRecordFree)
    {
        /* No records to process. Return without assigning output variables. */
        return true;
    }

    uint32_t cbRecordCurrent = ASMAtomicReadU32(&pVbvaMemory->aRecords[indexRecordFirst].cbRecord);

#ifdef DEBUG_sunlover
    LogFlowFunc(("cbRecord = 0x%08X\n", cbRecordCurrent));
#endif /* DEBUG_sunlover */

    uint32_t cbRecord = cbRecordCurrent & ~VBVA_F_RECORD_PARTIAL;

    if (pVideoAccel->cbVbvaPartial)
    {
        /* There is a partial read in process. Continue with it. */

        Assert(pVideoAccel->pu8VbvaPartial);

        LogFlowFunc(("continue partial record cbVbvaPartial = %d cbRecord 0x%08X, first = %d, free = %d\n",
                      pVideoAccel->cbVbvaPartial, cbRecordCurrent, indexRecordFirst, indexRecordFree));

        if (cbRecord > pVideoAccel->cbVbvaPartial)
        {
            /* New data has been added to the record. */
            if (!i_vbvaPartialRead(&pVideoAccel->pu8VbvaPartial, &pVideoAccel->cbVbvaPartial, cbRecord, pVbvaMemory))
            {
                return false;
            }
        }

        if (!(cbRecordCurrent & VBVA_F_RECORD_PARTIAL))
        {
            /* The record is completed by guest. Return it to the caller. */
            *ppHdr = (VBVACMDHDR *)pVideoAccel->pu8VbvaPartial;
            *pcbCmd = pVideoAccel->cbVbvaPartial;

            pVideoAccel->pu8VbvaPartial = NULL;
            pVideoAccel->cbVbvaPartial = 0;

            /* Advance the record index. */
            pVbvaMemory->indexRecordFirst = (indexRecordFirst + 1) % VBVA_MAX_RECORDS;

#ifdef DEBUG_sunlover
            LogFlowFunc(("partial done ok, data = %d, free = %d\n",
                         pVbvaMemory->off32Data, pVbvaMemory->off32Free));
#endif /* DEBUG_sunlover */
        }

        return true;
    }

    /* A new record need to be processed. */
    if (cbRecordCurrent & VBVA_F_RECORD_PARTIAL)
    {
        /* Current record is being written by guest. '=' is important here. */
        if (cbRecord >= VBVA_RING_BUFFER_SIZE - VBVA_RING_BUFFER_THRESHOLD)
        {
            /* Partial read must be started. */
            if (!i_vbvaPartialRead(&pVideoAccel->pu8VbvaPartial, &pVideoAccel->cbVbvaPartial, cbRecord, pVbvaMemory))
            {
                return false;
            }

            LogFlowFunc(("started partial record cbVbvaPartial = 0x%08X cbRecord 0x%08X, first = %d, free = %d\n",
                          pVideoAccel->cbVbvaPartial, cbRecordCurrent, indexRecordFirst, indexRecordFree));
        }

        return true;
    }

    /* Current record is complete. If it is not empty, process it. */
    if (cbRecord)
    {
        /* The size of largest contiguous chunk in the ring biffer. */
        uint32_t u32BytesTillBoundary = VBVA_RING_BUFFER_SIZE - pVbvaMemory->off32Data;

        /* The ring buffer pointer. */
        uint8_t *au8RingBuffer = &pVbvaMemory->au8RingBuffer[0];

        /* The pointer to data in the ring buffer. */
        uint8_t *src = &au8RingBuffer[pVbvaMemory->off32Data];

        /* Fetch or point the data. */
        if (u32BytesTillBoundary >= cbRecord)
        {
            /* The command does not cross buffer boundary. Return address in the buffer. */
            *ppHdr = (VBVACMDHDR *)src;

            /* Advance data offset. */
            pVbvaMemory->off32Data = (pVbvaMemory->off32Data + cbRecord) % VBVA_RING_BUFFER_SIZE;
        }
        else
        {
            /* The command crosses buffer boundary. Rare case, so not optimized. */
            uint8_t *dst = (uint8_t *)RTMemAlloc(cbRecord);

            if (!dst)
            {
                LogRelFlowFunc(("could not allocate %d bytes from heap!!!\n", cbRecord));
                pVbvaMemory->off32Data = (pVbvaMemory->off32Data + cbRecord) % VBVA_RING_BUFFER_SIZE;
                return false;
            }

            i_vbvaFetchBytes(pVbvaMemory, dst, cbRecord);

            *ppHdr = (VBVACMDHDR *)dst;

#ifdef DEBUG_sunlover
            LogFlowFunc(("Allocated from heap %p\n", dst));
#endif /* DEBUG_sunlover */
        }
    }

    *pcbCmd = cbRecord;

    /* Advance the record index. */
    pVbvaMemory->indexRecordFirst = (indexRecordFirst + 1) % VBVA_MAX_RECORDS;

#ifdef DEBUG_sunlover
    LogFlowFunc(("done ok, data = %d, free = %d\n",
                 pVbvaMemory->off32Data, pVbvaMemory->off32Free));
#endif /* DEBUG_sunlover */

    return true;
}

static void i_vbvaReleaseCmd(VIDEOACCEL *pVideoAccel, VBVACMDHDR *pHdr, int32_t cbCmd)
{
    RT_NOREF(cbCmd);
    uint8_t *au8RingBuffer = pVideoAccel->pVbvaMemory->au8RingBuffer;

    if (   (uint8_t *)pHdr >= au8RingBuffer
        && (uint8_t *)pHdr < &au8RingBuffer[VBVA_RING_BUFFER_SIZE])
    {
        /* The pointer is inside ring buffer. Must be continuous chunk. */
        Assert(VBVA_RING_BUFFER_SIZE - ((uint8_t *)pHdr - au8RingBuffer) >= cbCmd);

        /* Do nothing. */

        Assert(!pVideoAccel->pu8VbvaPartial && pVideoAccel->cbVbvaPartial == 0);
    }
    else
    {
        /* The pointer is outside. It is then an allocated copy. */

#ifdef DEBUG_sunlover
        LogFlowFunc(("Free heap %p\n", pHdr));
#endif /* DEBUG_sunlover */

        if ((uint8_t *)pHdr == pVideoAccel->pu8VbvaPartial)
        {
            pVideoAccel->pu8VbvaPartial = NULL;
            pVideoAccel->cbVbvaPartial = 0;
        }
        else
        {
            Assert(!pVideoAccel->pu8VbvaPartial && pVideoAccel->cbVbvaPartial == 0);
        }

        RTMemFree(pHdr);
    }

    return;
}


/**
 * Called regularly on the DisplayRefresh timer.
 * Also on behalf of guest, when the ring buffer is full.
 *
 * @thread EMT
 */
void Display::i_VideoAccelFlush(PPDMIDISPLAYPORT pUpPort)
{
    int vrc = i_videoAccelFlush(pUpPort);
    if (RT_FAILURE(vrc))
    {
        /* Disable on errors. */
        i_videoAccelEnable(false, NULL, pUpPort);
    }
}

int Display::i_videoAccelFlush(PPDMIDISPLAYPORT pUpPort)
{
    VIDEOACCEL *pVideoAccel = &mVideoAccelLegacy;
    VBVAMEMORY *pVbvaMemory = pVideoAccel->pVbvaMemory;

#ifdef DEBUG_sunlover_2
    LogFlowFunc(("fVideoAccelEnabled = %d\n", pVideoAccel->fVideoAccelEnabled));
#endif /* DEBUG_sunlover_2 */

    if (!pVideoAccel->fVideoAccelEnabled)
    {
        Log(("Display::VideoAccelFlush: called with disabled VBVA!!! Ignoring.\n"));
        return VINF_SUCCESS;
    }

    /* Here VBVA is enabled and we have the accelerator memory pointer. */
    Assert(pVbvaMemory);

#ifdef DEBUG_sunlover_2
    LogFlowFunc(("indexRecordFirst = %d, indexRecordFree = %d, off32Data = %d, off32Free = %d\n",
                  pVbvaMemory->indexRecordFirst, pVbvaMemory->indexRecordFree,
                  pVbvaMemory->off32Data, pVbvaMemory->off32Free));
#endif /* DEBUG_sunlover_2 */

    /* Quick check for "nothing to update" case. */
    if (pVbvaMemory->indexRecordFirst == pVbvaMemory->indexRecordFree)
    {
        return VINF_SUCCESS;
    }

    /* Process the ring buffer */
    unsigned uScreenId;

    /* Initialize dirty rectangles accumulator. */
    VBVADIRTYREGION rgn;
    vbvaRgnInit(&rgn, maFramebuffers, mcMonitors, this, pUpPort);

    for (;;)
    {
        VBVACMDHDR *phdr = NULL;
        uint32_t cbCmd = UINT32_MAX;

        /* Fetch the command data. */
        if (!i_vbvaFetchCmd(pVideoAccel, &phdr, &cbCmd))
        {
            Log(("Display::VideoAccelFlush: unable to fetch command. off32Data = %d, off32Free = %d. Disabling VBVA!!!\n",
                  pVbvaMemory->off32Data, pVbvaMemory->off32Free));
            return VERR_INVALID_STATE;
        }

        if (cbCmd == uint32_t(~0))
        {
            /* No more commands yet in the queue. */
#ifdef DEBUG_sunlover
            LogFlowFunc(("no command\n"));
#endif /* DEBUG_sunlover */
            break;
        }

        if (cbCmd != 0)
        {
#ifdef DEBUG_sunlover
            LogFlowFunc(("hdr: cbCmd = %d, x=%d, y=%d, w=%d, h=%d\n",
                         cbCmd, phdr->x, phdr->y, phdr->w, phdr->h));
#endif /* DEBUG_sunlover */

            VBVACMDHDR hdrSaved = *phdr;

            int x = phdr->x;
            int y = phdr->y;
            int w = phdr->w;
            int h = phdr->h;

            uScreenId = mapCoordsToScreen(maFramebuffers, mcMonitors, &x, &y, &w, &h);

            phdr->x = (int16_t)x;
            phdr->y = (int16_t)y;
            phdr->w = (uint16_t)w;
            phdr->h = (uint16_t)h;

            /* Handle the command.
             *
             * Guest is responsible for updating the guest video memory.
             * The Windows guest does all drawing using Eng*.
             *
             * For local output, only dirty rectangle information is used
             * to update changed areas.
             *
             * Dirty rectangles are accumulated to exclude overlapping updates and
             * group small updates to a larger one.
             */

            /* Accumulate the update. */
            vbvaRgnDirtyRect(&rgn, uScreenId, phdr);

            /* Forward the command to VRDP server. */
            mParent->i_consoleVRDPServer()->SendUpdate(uScreenId, phdr, cbCmd);

            *phdr = hdrSaved;
        }

        i_vbvaReleaseCmd(pVideoAccel, phdr, cbCmd);
    }

    for (uScreenId = 0; uScreenId < mcMonitors; uScreenId++)
    {
        /* Draw the framebuffer. */
        vbvaRgnUpdateFramebuffer(&rgn, uScreenId);
    }
    return VINF_SUCCESS;
}

int Display::i_videoAccelRefreshProcess(PPDMIDISPLAYPORT pUpPort)
{
    int vrc = VWRN_INVALID_STATE; /* Default is to do a display update in VGA device. */

    VIDEOACCEL *pVideoAccel = &mVideoAccelLegacy;

    videoAccelEnterVGA(pVideoAccel);

    if (pVideoAccel->fVideoAccelEnabled)
    {
        Assert(pVideoAccel->pVbvaMemory);
        vrc = i_videoAccelFlush(pUpPort);
        if (RT_FAILURE(vrc))
        {
            /* Disable on errors. */
            i_videoAccelEnable(false, NULL, pUpPort);
            vrc = VWRN_INVALID_STATE; /* Do a display update in VGA device. */
        }
        else
        {
            vrc = VINF_SUCCESS;
        }
    }

    videoAccelLeaveVGA(pVideoAccel);

    return vrc;
}

void Display::processAdapterData(void *pvVRAM, uint32_t u32VRAMSize)
{
    RT_NOREF(u32VRAMSize);
    if (pvVRAM == NULL)
    {
        unsigned i;
        for (i = 0; i < mcMonitors; i++)
        {
            DISPLAYFBINFO *pFBInfo = &maFramebuffers[i];

            pFBInfo->u32Offset = 0;
            pFBInfo->u32MaxFramebufferSize = 0;
            pFBInfo->u32InformationSize = 0;
        }
    }
#ifndef VBOX_WITH_HGSMI
    else
    {
         uint8_t *pu8 = (uint8_t *)pvVRAM;
         pu8 += u32VRAMSize - VBOX_VIDEO_ADAPTER_INFORMATION_SIZE;

         /// @todo
         uint8_t *pu8End = pu8 + VBOX_VIDEO_ADAPTER_INFORMATION_SIZE;

         VBOXVIDEOINFOHDR *pHdr;

         for (;;)
         {
             pHdr = (VBOXVIDEOINFOHDR *)pu8;
             pu8 += sizeof(VBOXVIDEOINFOHDR);

             if (pu8 >= pu8End)
             {
                 LogRel(("VBoxVideo: Guest adapter information overflow!!!\n"));
                 break;
             }

             if (pHdr->u8Type == VBOX_VIDEO_INFO_TYPE_DISPLAY)
             {
                 if (pHdr->u16Length != sizeof(VBOXVIDEOINFODISPLAY))
                 {
                     LogRel(("VBoxVideo: Guest adapter information %s invalid length %d!!!\n", "DISPLAY", pHdr->u16Length));
                     break;
                 }

                 VBOXVIDEOINFODISPLAY *pDisplay = (VBOXVIDEOINFODISPLAY *)pu8;

                 if (pDisplay->u32Index >= mcMonitors)
                 {
                     LogRel(("VBoxVideo: Guest adapter information invalid display index %d!!!\n", pDisplay->u32Index));
                     break;
                 }

                 DISPLAYFBINFO *pFBInfo = &maFramebuffers[pDisplay->u32Index];

                 pFBInfo->u32Offset = pDisplay->u32Offset;
                 pFBInfo->u32MaxFramebufferSize = pDisplay->u32FramebufferSize;
                 pFBInfo->u32InformationSize = pDisplay->u32InformationSize;

                 LogRelFlow(("VBOX_VIDEO_INFO_TYPE_DISPLAY: %d: at 0x%08X, size 0x%08X, info 0x%08X\n", pDisplay->u32Index,
                             pDisplay->u32Offset, pDisplay->u32FramebufferSize, pDisplay->u32InformationSize));
             }
             else if (pHdr->u8Type == VBOX_VIDEO_INFO_TYPE_QUERY_CONF32)
             {
                 if (pHdr->u16Length != sizeof(VBOXVIDEOINFOQUERYCONF32))
                 {
                     LogRel(("VBoxVideo: Guest adapter information %s invalid length %d!!!\n", "CONF32", pHdr->u16Length));
                     break;
                 }

                 VBOXVIDEOINFOQUERYCONF32 *pConf32 = (VBOXVIDEOINFOQUERYCONF32 *)pu8;

                 switch (pConf32->u32Index)
                 {
                     case VBOX_VIDEO_QCI32_MONITOR_COUNT:
                     {
                         pConf32->u32Value = mcMonitors;
                     } break;

                     case VBOX_VIDEO_QCI32_OFFSCREEN_HEAP_SIZE:
                     {
                         /** @todo make configurable. */
                         pConf32->u32Value = _1M;
                     } break;

                     default:
                         LogRel(("VBoxVideo: CONF32 %d not supported!!! Skipping.\n", pConf32->u32Index));
                 }
             }
             else if (pHdr->u8Type == VBOX_VIDEO_INFO_TYPE_END)
             {
                 if (pHdr->u16Length != 0)
                 {
                     LogRel(("VBoxVideo: Guest adapter information %s invalid length %d!!!\n", "END", pHdr->u16Length));
                     break;
                 }

                 break;
             }
             else if (pHdr->u8Type != VBOX_VIDEO_INFO_TYPE_NV_HEAP)
             {
                 /** @todo why is Additions/WINNT/Graphics/Miniport/VBoxVideo. cpp pushing this to us? */
                 LogRel(("Guest adapter information contains unsupported type %d. The block has been skipped.\n", pHdr->u8Type));
             }

             pu8 += pHdr->u16Length;
         }
    }
#endif /* !VBOX_WITH_HGSMI */
}

void Display::processDisplayData(void *pvVRAM, unsigned uScreenId)
{
    if (uScreenId >= mcMonitors)
    {
        LogRel(("VBoxVideo: Guest display information invalid display index %d!!!\n", uScreenId));
        return;
    }

    /* Get the display information structure. */
    DISPLAYFBINFO *pFBInfo = &maFramebuffers[uScreenId];

    uint8_t *pu8 = (uint8_t *)pvVRAM;
    pu8 += pFBInfo->u32Offset + pFBInfo->u32MaxFramebufferSize;

    /// @todo
    uint8_t *pu8End = pu8 + pFBInfo->u32InformationSize;

    VBOXVIDEOINFOHDR *pHdr;

    for (;;)
    {
        pHdr = (VBOXVIDEOINFOHDR *)pu8;
        pu8 += sizeof(VBOXVIDEOINFOHDR);

        if (pu8 >= pu8End)
        {
            LogRel(("VBoxVideo: Guest display information overflow!!!\n"));
            break;
        }

        if (pHdr->u8Type == VBOX_VIDEO_INFO_TYPE_SCREEN)
        {
            if (pHdr->u16Length != sizeof(VBOXVIDEOINFOSCREEN))
            {
                LogRel(("VBoxVideo: Guest display information %s invalid length %d!!!\n", "SCREEN", pHdr->u16Length));
                break;
            }

            VBOXVIDEOINFOSCREEN *pScreen = (VBOXVIDEOINFOSCREEN *)pu8;

            pFBInfo->xOrigin = pScreen->xOrigin;
            pFBInfo->yOrigin = pScreen->yOrigin;

            pFBInfo->w = pScreen->u16Width;
            pFBInfo->h = pScreen->u16Height;

            LogRelFlow(("VBOX_VIDEO_INFO_TYPE_SCREEN: (%p) %d: at %d,%d, linesize 0x%X, size %dx%d, bpp %d, flags 0x%02X\n",
                     pHdr, uScreenId, pScreen->xOrigin, pScreen->yOrigin, pScreen->u32LineSize, pScreen->u16Width,
                     pScreen->u16Height, pScreen->bitsPerPixel, pScreen->u8Flags));

            if (uScreenId != VBOX_VIDEO_PRIMARY_SCREEN)
            {
                /* Primary screen resize is eeeeeeeee by the VGA device. */
                if (pFBInfo->fDisabled)
                {
                    pFBInfo->fDisabled = false;
                    ::FireGuestMonitorChangedEvent(mParent->i_getEventSource(), GuestMonitorChangedEventType_Enabled, uScreenId,
                                                   pFBInfo->xOrigin, pFBInfo->yOrigin, pFBInfo->w, pFBInfo->h);
                }

                i_handleDisplayResize(uScreenId, pScreen->bitsPerPixel,
                                      (uint8_t *)pvVRAM + pFBInfo->u32Offset,
                                      pScreen->u32LineSize,
                                      pScreen->u16Width, pScreen->u16Height,
                                      VBVA_SCREEN_F_ACTIVE,
                                      pScreen->xOrigin, pScreen->yOrigin, false);
            }
        }
        else if (pHdr->u8Type == VBOX_VIDEO_INFO_TYPE_END)
        {
            if (pHdr->u16Length != 0)
            {
                LogRel(("VBoxVideo: Guest adapter information %s invalid length %d!!!\n", "END", pHdr->u16Length));
                break;
            }

            break;
        }
        else if (pHdr->u8Type == VBOX_VIDEO_INFO_TYPE_HOST_EVENTS)
        {
            if (pHdr->u16Length != sizeof(VBOXVIDEOINFOHOSTEVENTS))
            {
                LogRel(("VBoxVideo: Guest display information %s invalid length %d!!!\n", "HOST_EVENTS", pHdr->u16Length));
                break;
            }

            VBOXVIDEOINFOHOSTEVENTS *pHostEvents = (VBOXVIDEOINFOHOSTEVENTS *)pu8;

            pFBInfo->pHostEvents = pHostEvents;

            LogFlow(("VBOX_VIDEO_INFO_TYPE_HOSTEVENTS: (%p)\n",
                     pHostEvents));
        }
        else if (pHdr->u8Type == VBOX_VIDEO_INFO_TYPE_LINK)
        {
            if (pHdr->u16Length != sizeof(VBOXVIDEOINFOLINK))
            {
                LogRel(("VBoxVideo: Guest adapter information %s invalid length %d!!!\n", "LINK", pHdr->u16Length));
                break;
            }

            VBOXVIDEOINFOLINK *pLink = (VBOXVIDEOINFOLINK *)pu8;
            pu8 += pLink->i32Offset;
        }
        else
        {
            LogRel(("Guest display information contains unsupported type %d\n", pHdr->u8Type));
        }

        pu8 += pHdr->u16Length;
    }
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
