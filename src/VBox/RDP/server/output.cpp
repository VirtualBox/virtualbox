/* $Id: output.cpp 114912 2026-08-10 12:03:20Z vitali.pelenjow@oracle.com $ */
/** @file
 * VBox Remote Desktop Protocol.
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

#include "vrdpserv.h"
#include "utils.h"
#include "bmpcache.h"
#include "textcache.h"

#include "bmpcomp.h"

/* There is one queue for output updates. Updates go there always.
 * When there is no target for an output update, the update is
 * dropped. Updates therefore must tolerate dropping, they are
 * only something that can be resend or can be ignored:
 *  - pointer shape,
 *  - bitmap updates,
 *  - graphical orders.
 *
 *
 */
OutputQueue::OutputQueue (void)
    :
    m_pHead (NULL),
    m_pTail (NULL)
{
    VRDPLock::Create("OutputQueue", &m_pOutputQueueLock);
}

OutputQueue::~OutputQueue ()
{
    VRDPLock::Delete(&m_pOutputQueueLock);
}

OutputUpdate *OutputQueue::CreateUpdate (int iCode, uint32_t u32TargetClientId, const void *pvData, unsigned cbData)
{
    OutputUpdate *p = (OutputUpdate *)VRDPMemAllocZ (sizeof (OutputUpdate) + cbData);

    if (p)
    {
        p->u32TargetClientId = u32TargetClientId;
        p->iCode = iCode;

        if (pvData)
        {
            p->pvData = &p[1];
            p->cbData = cbData;

            memcpy (p->pvData, pvData, cbData);
        }
    }

    return p;
}

void OutputQueue::DeleteUpdate (OutputUpdate *pUpdate)
{
    if (pUpdate)
    {
        Assert (pUpdate->pNext == NULL && pUpdate->pPrev == NULL);

        /* Free the update data. */
        if (pUpdate->iCode == VRDP_OUTPUT_CLIPBOARD_DATA)
        {
            VRDPClipboardData *pClipboardData = (VRDPClipboardData *)pUpdate->pvData;

            if (pClipboardData->pvData)
            {
                VRDPMemFree (pClipboardData->pvData);
            }
        }

        VRDPMemFree (pUpdate);
    }
}

void OutputQueue::InsertUpdate (OutputUpdate *pUpdate)
{
    Assert (pUpdate->pNext == NULL && pUpdate->pPrev == NULL);

    if (RT_SUCCESS(VRDPLock::Lock(m_pOutputQueueLock)))
    {
        pUpdate->pPrev = m_pTail;

        if (m_pTail)
        {
            m_pTail->pNext = pUpdate;
        }
        else
        {
            m_pHead = pUpdate;
        }

        m_pTail = pUpdate;

        VRDPLock::Unlock(m_pOutputQueueLock);
    }
    else
    {
        DeleteUpdate (pUpdate);
    }
}

OutputUpdate *OutputQueue::Read (void)
{
    OutputUpdate *pUpdate = NULL;

    if (RT_SUCCESS(VRDPLock::Lock(m_pOutputQueueLock)))
    {
        pUpdate = m_pHead;

        if (pUpdate)
        {
            m_pHead = pUpdate->pNext;

            if (m_pHead)
            {
               m_pHead->pPrev = NULL;
            }
            else
            {
               m_pTail = NULL;
            }

            pUpdate->pNext = NULL;
            Assert (pUpdate->pPrev == NULL);
        }

        VRDPLock::Unlock(m_pOutputQueueLock);
    }

    return pUpdate;
}

/* The OUTPUT thread provides timers for the server.
 * There are currently 2 timers: flush and audio.
 */

typedef struct _VRDPOutputTimer
{
    DECLCALLBACKMEMBER(uint64_t, pfnCallback,(VRDPServer *pServer, uint64_t u64EventTS));
    uint64_t u64NextEventTS;
    int      cTimerOnTime;
    int      cTimerLate;
    int64_t  i64TimerLateMax;
} VRDPOutputTimer;

static uint64_t vrdpOutputTimersNextEvent (VRDPOutputTimer *paTimers, int cTimers)
{
    Assert (cTimers > 0);

    int i;

    uint64_t u64NextEventTS = paTimers[0].u64NextEventTS;

    for (i = 1; i < cTimers; i++)
    {
        if (u64NextEventTS > paTimers[i].u64NextEventTS)
        {
            u64NextEventTS = paTimers[i].u64NextEventTS;
        }
    }

    return u64NextEventTS;
}

static uint64_t vrdpOutputTimersInit (VRDPServer *pServer, VRDPOutputTimer *paTimers, int cTimers)
{
    Assert (cTimers > 0);

    int i;

    for (i = 0; i < cTimers; i++)
    {
        Assert (paTimers[i].pfnCallback != NULL);

        paTimers[i].u64NextEventTS  = paTimers[i].pfnCallback (pServer, 0);
        paTimers[i].cTimerOnTime    = 0;
        paTimers[i].cTimerLate      = 0;
        paTimers[i].i64TimerLateMax = 0;
    }

    return vrdpOutputTimersNextEvent (paTimers, cTimers);
}

static uint64_t vrdpOutputTimersProcess (VRDPServer *pServer, VRDPOutputTimer *paTimers, int cTimers)
{
    Assert (cTimers > 0);

    int i;

    uint64_t u64NowTS = VRDPTimeMilliTS ();

    for (i = 0; i < cTimers; i++)
    {
        if (paTimers[i].pfnCallback == NULL)
        {
            continue;
        }

        int64_t i64DeltaTS = u64NowTS - paTimers[i].u64NextEventTS;

        if (i64DeltaTS >= 0)
        {
            /* The timer expired, process it. */

            /* Statistics. */
            if (i64DeltaTS < 5)
            {
                paTimers[i].cTimerOnTime++;
            }
            else
            {
                paTimers[i].cTimerLate++;

                if (paTimers[i].i64TimerLateMax < i64DeltaTS)
                {
                    paTimers[i].i64TimerLateMax = i64DeltaTS;
                }
            }

            paTimers[i].u64NextEventTS = paTimers[i].pfnCallback (pServer, paTimers[i].u64NextEventTS);
        }
    }

    return vrdpOutputTimersNextEvent (paTimers, cTimers);
}

static void vrdpOutputTimersEnd (VRDPServer *pServer, VRDPOutputTimer *paTimers, int cTimers)
{
    RT_NOREF3(pServer, paTimers, cTimers);
    Assert (cTimers > 0);

    int i;

    for (i = 0; i < cTimers; i++)
    {
        SERVERLOG(("VRDPTimer %d\n"
                   "    OnTime  %d\n"
                   "    Late    %d\n"
                   "    LateMax %lld\n",
                   i,
                   paTimers[i].cTimerOnTime,
                   paTimers[i].cTimerLate,
                   paTimers[i].i64TimerLateMax
                 ));
    }
}

#define VRDP_OUTPUT_REFRESH_PERIOD_MS 100
#define VRDP_OUTPUT_SHORT_REFRESH_PERIOD_MS 20

bool VRDPServer::isInputActivity (uint64_t u64NowTS)
{
    uint64_t u64LastInputTS;

    u64LastInputTS = ASMAtomicReadU64 (&m_u64LastInputTS);

    if (u64NowTS >= u64LastInputTS + VRDP_OUTPUT_REFRESH_PERIOD_MS)
    {
        return false;
    }

    return true;
}

/** @thread INPUT */
void VRDPServer::notifyInputActivity (void)
{
    /* Atomically update the timestamp. */
    ASMAtomicXchgU64(&m_u64LastInputTS, VRDPTimeMilliTS ());

    /* Ping the output thread to get a higher refresh rate. */
    RaiseOutputEvent ();
}

/* static */ DECLCALLBACK(uint64_t) VRDPServer::vrdpOutputCallbackFlush (VRDPServer *pServer, uint64_t u64EventTS)
{
    uint64_t u64NowTS = VRDPTimeMilliTS ();

    if (u64EventTS == 0)
    {
        return u64NowTS + VRDP_OUTPUT_REFRESH_PERIOD_MS;
    }

    VRDPClient *pClient = NULL;
    uint32_t u32ClientId = 0;

    while ((pClient = pServer->m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
    {
        pClient->Stream ()->Flush ();

        pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
    }

    return u64NowTS +
           (pServer->isInputActivity (u64NowTS)? VRDP_OUTPUT_SHORT_REFRESH_PERIOD_MS: VRDP_OUTPUT_REFRESH_PERIOD_MS);
}

/* static */ DECLCALLBACK(uint64_t) VRDPServer::vrdpOutputCallbackAudio (VRDPServer *pServer, uint64_t u64EventTS)
{
    return pServer->audioTimerHandler (u64EventTS);
}

/* static */ DECLCALLBACK(uint64_t) VRDPServer::vrdpOutputCallbackVideo(VRDPServer *pServer, uint64_t u64EventTS)
{
    return pServer->VideoHandlerOutput(u64EventTS);
}

int VRDPServer::OutputThread (RTTHREAD self, VRDPServerThreadStartCtx *pCtx)
{
    /* Set it immediately so any called code can check the context. */
    m_outputThread = self;

    Assert (m_ptc == NULL);

    m_ptc = TCCreate ();

    if (m_ptc == NULL)
    {
        notifyThreadStarted (self, pCtx, VERR_NO_MEMORY);
        return VERR_NO_MEMORY;
    }

    audioInit ();

    VRDPOutputTimer timers[3];
    uint64_t u64NextEventTS;

    /* Audio callback must have higher priority than the flush one. */
    timers[0].pfnCallback = vrdpOutputCallbackAudio;
    timers[1].pfnCallback = vrdpOutputCallbackFlush;
    timers[2].pfnCallback = vrdpOutputCallbackVideo;

    u64NextEventTS = vrdpOutputTimersInit (this, timers, RT_ELEMENTS (timers));

    notifyThreadStarted (self, pCtx, VINF_SUCCESS);

    OUTPUTLOG(("Output thread started: m_fShutdownThreads = %d\n", m_fShutdownThreads));

    VrdpSbAct action;
    RT_ZERO(action); /* (MSC understandably thinks this (uScreenId, u.region.prgn) is used uninitialized) */
    action.code = VRDP_SB_ACT_NOP;

    OutputUpdate *pUpdate = NULL;

    m_u32OrderFallbackStatus = VRDP_OF_STATUS_NONE;

    while (!m_fShutdownThreads)
    {
        if (action.code == VRDP_SB_ACT_NOP)
        {
            shadowBufferGetAction (&action);

            if (action.code == VRDP_SB_ACT_NOP)
            {
                /* No actions from shadow buffer. If pending redraw have been processed,
                 * clear the fallback status. This will enable normal processing for graphics orders.
                 */
                ASMAtomicCmpXchgU32(&m_u32OrderFallbackStatus, VRDP_OF_STATUS_NONE, VRDP_OF_STATUS_REDRAW_COMPLETED);
            }
        }

        pUpdate = m_outputQueue.Read ();

        /* Check if the thread should wait for a next event. */
        uint64_t u64NowTS = VRDPTimeMilliTS ();
        int64_t i64DeltaTS = u64NextEventTS - u64NowTS;

        if (   pUpdate == NULL
            && action.code == VRDP_SB_ACT_NOP
            && i64DeltaTS > 0)
        {
            RTSemEventMultiWait(m_outputsem, (unsigned)i64DeltaTS);
            RTSemEventMultiReset(m_outputsem);
        }

        /* Always process timers, the function takes care of calling handlers only when necessary. */
        u64NextEventTS = vrdpOutputTimersProcess (this, timers, RT_ELEMENTS (timers));

#ifdef DEBUG
        if (action.code != 0 || pUpdate != NULL)
        {
            OUTPUTLOG(("Output thread: action.code = %d, pUpdate %p, screenid = %d\n", action.code, pUpdate, action.uScreenId));
        }
#endif /* DEBUG */

        VRDPClient *pClient = NULL;
        uint32_t u32ClientId = 0;

        /** @todo updates must be delivered regardless of resize processing. USB updates are lost. */
        if (pUpdate)
        {
            /* Server output events. */
            if (VRDP_OUTPUT_SERVER_FIRST <= pUpdate->iCode && pUpdate->iCode <= VRDP_OUTPUT_SERVER_LAST)
            {
                ProcessOutputEvent(pUpdate);

                m_outputQueue.DeleteUpdate (pUpdate);

                /* Restart the output loop as there could be another update pending. */
                continue;
            }

            if (pUpdate->u32TargetClientId != 0)
            {
                /* Update for a specific client. */
                pClient = m_clientArray.ThreadContextGetClient (pUpdate->u32TargetClientId, VRDP_CONTEXT_OUTPUT);

                if (pClient)
                {
                    pClient->Output (pUpdate);

                    pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                }
            }
            else
            {
                /* Broadcast update. Deliver to all ready clients. */
                u32ClientId = 0;

                while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                {
                    pClient->Output (pUpdate);

                    pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                }
            }

            m_outputQueue.DeleteUpdate (pUpdate);

            /* Restart the output loop as there could be another update pending. */
            continue;
        }

        /* Check whether there is something from shadow buffer. */
        if (action.code != VRDP_SB_ACT_NOP)
        {
            if (action.code == VRDP_SB_ACT_REGION)
            {
                const RGNRECT *prect;

                VRDPSBKEY sbStartKey = shadowBufferBeginEnumRgnRect (action.u.region.prgn);

                int rc = VINF_SUCCESS;

                while (RT_SUCCESS(rc) && (prect = shadowBufferQueryNextRgnRect (action.u.region.prgn)) != NULL)
                {
                    OUTPUTLOG(("Going to send update %d,%d %d,%d\n", prect->x, prect->y, prect->w, prect->h));

                    if (prect->w > 0 && prect->h > 0)
                    {
                        VRDPBitmapCompressed bmp;

                        OUTPUTLOG(("Sending bitmap update: %d,%d %dx%d\n",
                                    prect->x, prect->y, prect->w, prect->h));

                        u32ClientId = 0;

                        while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                        {
                            if (   pClient->IsScreenIdMatched (action.uScreenId)
                                && !pClient->IsDisplayDisabled ())
                            {
                                if (pClient->TP()->IsBitmapCompressionSupported ())
                                {
                                    /* There is at least one client that needs the compressed bitmap data. */
                                    if (bmp.IsEmpty ())
                                    {
                                        VRDPBITSRECT bitsRect;
                                        VRDPSBKEY key = shadowBufferLock (sbStartKey, &bitsRect, action.uScreenId, prect);

                                        if (key == VRDP_SB_NULL_ACCESS_KEY)
                                        {
                                            OUTPUTLOG(("Failed to obtain shadow buffer lock!!!\n"));

                                            rc = VERR_NOT_SUPPORTED;

                                            pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);

                                            break;
                                        }

                                        bmp.Compress (bitsRect.pu8Bits, bitsRect.cbLine, prect->w, prect->h, bitsRect.cBitsPerPixel, 0);

                                        shadowBufferUnlock (action.uScreenId);
                                    }

                                    pClient->OutputBitmap2 (action.uScreenId, &bmp, prect->x, prect->y);
                                }
                                else
                                {
                                    /* Unoptimised code path for clients which do not support bitmap compression.
                                     * Split bitmap to tiles and always generate the bitmap.
                                     */
                                    VRDPBitmapCompressed bmpUncompressed;

                                    OUTPUTLOG(("A client without bitmap compression.\n"));

                                    VRDPBITSRECT bitsRect;
                                    VRDPSBKEY key = shadowBufferLock (sbStartKey, &bitsRect, action.uScreenId, prect);

                                    if (key == VRDP_SB_NULL_ACCESS_KEY)
                                    {
                                        OUTPUTLOG(("Failed to obtain shadow buffer lock!!!\n"));

                                        rc = VERR_NOT_SUPPORTED;

                                        pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);

                                        break;
                                    }

                                    bmpUncompressed.Compress (bitsRect.pu8Bits, bitsRect.cbLine, prect->w, prect->h, bitsRect.cBitsPerPixel, VRDP_F_BMPCOMP_TILE);

                                    shadowBufferUnlock (action.uScreenId);

                                    pClient->OutputBitmap2 (action.uScreenId, &bmpUncompressed, prect->x, prect->y);
                                }
                            }

                            pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                        }
                    }
                }

                shadowBufferCancelAction (&action);
            }
            else if (action.code == VRDP_SB_ACT_ORDER)
            {
                /* Special processing for MSB limit in shadow buffer.
                 * Last processed order was a VRDE_ORDER_BOUNDS, and now the shadow buffer
                 * is about to reenable MSBs and have to reset stale bounds.
                 */
                if (action.u.order.i32Op == VRDP_ORDER_INTERNALCANCEL)
                {
                    u32ClientId = 0;

                    while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                    {
                        if (pClient->IsScreenIdMatched (action.uScreenId))
                        {
                            pClient->OutputOrder (action.uScreenId, VRDP_ORDER_INTERNALCANCEL, NULL, 0);
                        }

                        pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                    }
                    shadowBufferCancelAction (&action);
                    continue;
                }

                /* If an order fallback is active, filter orders, which are incompatible. */
                if (ASMAtomicReadU32(&m_u32OrderFallbackStatus) != VRDP_OF_STATUS_NONE)
                {
                    if (   action.u.order.i32Op == VRDE_ORDER_SCREENBLT
                        || action.u.order.i32Op == VRDE_ORDER_DSTBLT
                       )
                    {
                        u32ClientId = 0;

                        while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                        {
                            if (pClient->IsScreenIdMatched (action.uScreenId))
                            {
                                pClient->AddRedraw (&action.u.order.rectAffected, true, action.uScreenId);
                                pClient->OutputOrder (action.uScreenId, VRDP_ORDER_INTERNALCANCEL, NULL, 0);
                            }

                            pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                        }

                        ASMAtomicWriteU32(&m_u32OrderFallbackStatus, VRDP_OF_STATUS_REDRAW_PENDING);

                        shadowBufferCancelAction (&action);
                        continue;
                    }
                }

                switch (action.u.order.i32Op)
                {
                    case VRDE_ORDER_MEMBLT:
                    {
                        VRDEORDERMEMBLT *pOrder = (VRDEORDERMEMBLT *)action.u.order.pvOrder;

                        SERVERLOG(("OutputThread: VRDE_ORDER_MEMBLT %d,%d %dx%d from %d,%d rop 0x%02X\n",
                                   pOrder->x, pOrder->y, pOrder->w, pOrder->h, pOrder->xSrc, pOrder->ySrc, pOrder->rop));

                        if (!shadowBufferAreOrderCoordsValid(action.uScreenId, pOrder->x, pOrder->y, pOrder->w, pOrder->h))
                        {
                            break;
                        }

                        /* Locate the bitmap in the cache. */
                        PBMPCACHEENTRY pbce = BCFindBitmap (m_pbc, &pOrder->hash);

                        if (!pbce)
                        {
                            SERVERLOG(("OutputThread: VRDE_ORDER_MEMBLT could not find in cache!!!\n"));

                            u32ClientId = 0;

                            while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                            {
                                if (pClient->IsScreenIdMatched (action.uScreenId))
                                {
                                    pClient->AddRedraw (&action.u.order.rectAffected, true, action.uScreenId);
                                    pClient->OutputOrder (action.uScreenId, VRDP_ORDER_INTERNALCANCEL, NULL, 0);
                                }

                                pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                            }

                            ASMAtomicWriteU32(&m_u32OrderFallbackStatus, VRDP_OF_STATUS_REDRAW_PENDING);

                            break;
                        }

                        VRDPBitmapCompressed *pBmp = BCQueryBitmapCompressed (pbce);

                        if (!pBmp)
                        {
                            SERVERLOG(("OutputThread: VRDE_ORDER_MEMBLT failed to create compressed bitmap object!!!\n"));
                            break;
                        }

                        /* Adjust the order's coordinates. */
                        RGNRECT rectDst;

                        rectDst.x = pOrder->x;
                        rectDst.y = pOrder->y;
                        rectDst.w = pOrder->w;
                        rectDst.h = pOrder->h;

                        shadowBufferTransformRect (action.uScreenId, &rectDst);

                        RGNRECT rectSrc;

                        rectSrc.x = pOrder->xSrc;
                        rectSrc.y = pOrder->ySrc;
                        rectSrc.w = pOrder->w;
                        rectSrc.h = pOrder->h;

                        RGNRECT rectBmp;

                        pBmp->GetBitmapRect (&rectBmp);

                        TESTLOG(("rectDst %d,%d %dx%d rectBmp %d,%d %dx%d rectSrc(o) %d,%d %dx%d\n",
                                 rectDst.x, rectDst.y, rectDst.w, rectDst.h,
                                 rectBmp.x, rectBmp.y, rectBmp.w, rectBmp.h,
                                 rectSrc.x, rectSrc.y, rectSrc.w, rectSrc.h
                               ));

                        shadowBufferTransformRectGeneric (action.uScreenId, &rectSrc, rectBmp.w, rectBmp.h);

                        TESTLOG(("rectSrc %d,%d %dx%d\n",
                                 rectSrc.x, rectSrc.y, rectSrc.w, rectSrc.h
                               ));

                        /* Find out which tiles of the cached bitmap are used. And tell all clients
                         * to cache these tiles. Clients may already have the tile in the remote cache,
                         * in that case they just do nothing.
                         *
                         * Need to compute which tiles in the bitmap are intersected with the
                         * orders xSrc, ySrc, w, h rectangle.
                         */

                        /* Scan all tiles. */
                        uint16_t cTilesX = pBmp->GetTilesColumns();

                        /* The order rectangle in the source bitmap coordinates. */
                        RGNRECT rectOrder = rectSrc;

                        TESTLOG(("rectOrder: %d,%d, %dx%d\n", rectOrder.x, rectOrder.y, rectOrder.w, rectOrder.h));

                        uint16_t u16TileX, u16TileY;

                        uint16_t u16DstX, u16DstY;

                        u16DstX = rectDst.x;

                        for (u16TileX = 0; u16TileX < cTilesX; u16TileX++)
                        {
                            u16DstY = rectDst.y;

                            uint16_t u16DstXShift = 0;

                            uint16_t cTilesY = pBmp->GetTilesRows(u16TileX);

                            for (u16TileY = 0; u16TileY < cTilesY; u16TileY++)
                            {
                                /* The tile rectangle in the bitmap coordinates. */
                                RGNRECT rectTile;
                                RGNRECT rect;

                                uint8_t u8TileSizeClass = pBmp->GetTileRect (&rectTile, u16TileX, u16TileY);

                                TESTLOG(("rectTile: %d,%d, %dx%d\n", rectTile.x, rectTile.y, rectTile.w, rectTile.h));

                                if (rgnIntersectRects(&rect, &rectTile, &rectOrder))
                                {
                                    TESTLOG(("rect: %d,%d, %dx%d\n", rect.x, rect.y, rect.w, rect.h));

                                    u32ClientId = 0;

                                    while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                                    {
                                        if (pClient->IsScreenIdMatched (action.uScreenId))
                                        {
                                            if (pClient->IsOrderSupported (action.u.order.i32Op))
                                            {
                                                pClient->OutputMemblt (action.uScreenId, pBmp, u16TileX, u16TileY,
                                                                       u16DstX,             /* Dst x. */
                                                                       u16DstY,             /* Dst y. */
                                                                       rect.x - rectTile.x, /* Src x. */
                                                                       rect.y - rectTile.y, /* Src y. */
                                                                       rect.w, rect.h,      /* Src w,h. */
                                                                       pOrder->rop,
                                                                       &rectTile,
                                                                       u8TileSizeClass);
                                            }
                                            else
                                            {
                                                pClient->AddRedraw (&action.u.order.rectAffected, true, action.uScreenId);
                                                pClient->OutputOrder (action.uScreenId, VRDP_ORDER_INTERNALCANCEL, NULL, 0);

                                                ASMAtomicWriteU32(&m_u32OrderFallbackStatus, VRDP_OF_STATUS_REDRAW_PENDING);
                                            }
                                        }

                                        pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                                    }

                                    u16DstY += rect.h;
                                    u16DstXShift = rect.w;
                                }
                            }

                            u16DstX += u16DstXShift;
                        }


                    } break;

                    case VRDE_ORDER_CACHED_BITMAP:
                    {
                        BCHEAPHANDLE h;
                        memcpy(&h, action.u.order.pvOrder, sizeof(BCHEAPHANDLE));

                        SERVERLOG(("OutputThread: VRDE_ORDER_CACHED_BITMAP\n"));

                        BCCacheBitmap (m_pbc, &h, action.uScreenId);
                    } break;

                    case VRDE_ORDER_DELETED_BITMAP:
                    {
                        VRDEORDERDELETEDBITMAP *pOrder = (VRDEORDERDELETEDBITMAP *)action.u.order.pvOrder;

                        SERVERLOG(("OutputThread: VRDE_ORDER_DELETED_BITMAP\n"));

                        BCDeleteBitmap (m_pbc, &pOrder->hash);
                    } break;

                    case VRDP_ORDER_INTERNAL_SAVESCREEN_RESTORE:
                    {
                        BCHEAPHANDLE h;
                        memcpy(&h, action.u.order.pvOrder, sizeof(BCHEAPHANDLE));

                        uint32_t cbOrder = 0;

                        VRDEORDERSAVESCREEN *pOrder = (VRDEORDERSAVESCREEN *)BCBitmapHeapBlockQuery(m_pbc, &h, action.u.order.i32Op, &cbOrder);

                        u32ClientId = 0;

                        while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                        {
                            if (pClient->IsScreenIdMatched (action.uScreenId))
                            {
                                if (pOrder && pClient->IsOrderSupported (action.u.order.i32Op))
                                {
                                    pClient->OutputOrder (action.uScreenId, VRDE_ORDER_SAVESCREEN, pOrder, cbOrder);
                                }
                                else
                                {
                                    pClient->AddRedraw (&action.u.order.rectAffected, true, action.uScreenId);
                                    pClient->OutputOrder (action.uScreenId, VRDP_ORDER_INTERNALCANCEL, NULL, 0);

                                    ASMAtomicWriteU32(&m_u32OrderFallbackStatus, VRDP_OF_STATUS_REDRAW_PENDING);
                                }
                            }

                            pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                        }

                        if (pOrder)
                        {
                            BCBitmapHeapBlockRelease(m_pbc, &h);
                        }

                        BCBitmapHeapBlockFree(m_pbc, &h);
                    } break;

                    case VRDE_ORDER_TEXT:
                    {
                        VRDEORDERTEXT *pOrder = (VRDEORDERTEXT *)action.u.order.pvOrder;

                        SERVERLOG(("OutputThread: VRDE_ORDER_TEXT\n"));

                        /* Text order generations consists of:
                         * 1) glyphs caching;
                         * 2) finding a string match;
                         * 3) preparing text2 order, which references cached glyphs
                         *    and a possible string match.
                         *
                         * The cache is divided to the server and client parts.
                         * The server part holds glyph information, provides the font/index
                         * assigment and fragment matching.
                         * The client specific part tracks which glyphs, fragments were cached
                         * by the client.
                         *
                         * First the server part generates a generic FONTCACHE/TEXT2 sequence
                         * based on information from the cache (which glyphs are already known,
                         * which fragments can be resent, whether the server cache has to
                         * update the font with new glyphs).
                         *
                         * Then each affected client generates and sends required orders.
                         * The generic sequence can be extended with required FONTCACHE
                         * and fragment TEXT2 orders, if relevant glyphs/fragments are not
                         * yet on the client.
                         *
                         */

                        /* Generate a FONTCACHE/TEXT2 sequence that would work if all clients would
                         * be synchronized with the server cache.
                         */
                        TCFONTTEXT2 *pFontText2 = NULL;

                        bool fSuccess = TCCacheGlyphs (m_ptc, pOrder, action.u.order.cbOrder, &pFontText2);

                        if (!fSuccess)
                        {
                            /* Normally that should not happen. If it does, then there is no fallback. */
                            SERVERLOG(("OutputThread: VRDE_ORDER_TEXT could not cache glyphs!!!\n"));
                            break;
                        }

                        if (pFontText2)
                        {
                            u32ClientId = 0;

                            while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                            {
                                if (pClient->IsScreenIdMatched (action.uScreenId))
                                {
                                    /* Generate and send actual FONTCACHE/TEXT2 orders. */
                                    if (pClient->IsOrderSupported (action.u.order.i32Op))
                                    {
                                        pClient->OutputText2 (action.uScreenId, pFontText2);
                                    }
                                    else
                                    {
                                        pClient->AddRedraw (&action.u.order.rectAffected, true, action.uScreenId);
                                        pClient->OutputOrder (action.uScreenId, VRDP_ORDER_INTERNALCANCEL, NULL, 0);

                                        ASMAtomicWriteU32(&m_u32OrderFallbackStatus, VRDP_OF_STATUS_REDRAW_PENDING);
                                    }
                                }

                                pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                            }

                            TCFreeFontText2 (pFontText2);
                        }
                    } break;

                    case VRDP_ORDER_INTERNALREDRAW:
                    {

                        /* This order causes a fallback if the client has no active stream. */
                        u32ClientId = 0;

                        while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                        {
                            if (pClient->IsScreenIdMatched (action.uScreenId))
                            {
                                pClient->AddRedraw (&action.u.order.rectAffected, true, action.uScreenId);
                            }

                            pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                        }
                    } break;

                    default:
                        /* Forward the order to clients. */
                        u32ClientId = 0;

                        while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                        {
                            if (pClient->IsScreenIdMatched (action.uScreenId))
                            {
                                if (pClient->IsOrderSupported (action.u.order.i32Op))
                                {
                                    pClient->OutputOrder (action.uScreenId, action.u.order.i32Op, action.u.order.pvOrder, action.u.order.cbOrder);
                                }
                                else
                                {
                                    pClient->AddRedraw (&action.u.order.rectAffected, true, action.uScreenId);
                                    pClient->OutputOrder (action.uScreenId, VRDP_ORDER_INTERNALCANCEL, NULL, 0);

                                    ASMAtomicWriteU32(&m_u32OrderFallbackStatus, VRDP_OF_STATUS_REDRAW_PENDING);
                                }
                            }

                            pClient->ThreadContextRelease (VRDP_CONTEXT_OUTPUT);
                        }
                }

                shadowBufferCancelAction (&action);
            }
            else if (action.code == VRDP_SB_ACT_RESIZE)
            {
                RESIZELOG(("VRDP_SB_ACT_RESIZE.\n"));

                /* A resize makes all current video streams obsolete. */
                videoHandlerResetOutput(VideoHandler(), this);

                u32ClientId = 0;

                while ((pClient = m_clientArray.ThreadContextGetNextClient (&u32ClientId, VRDP_CONTEXT_OUTPUT)) != NULL)
                {
                    /* The method changes the client's state accordingly. So a 'ThreadContextRelease' call is not needed. */
                    pClient->NotifyResizeAndThreadContextDisableBoth ();
                }

                shadowBufferCancelAction (&action);
            }
            else
            {
                AssertFailed();
            }

            continue;
        }
    }

    while ((pUpdate = m_outputQueue.Read ()) != NULL)
    {
        /* Cleanup pending updates. */
        m_outputQueue.DeleteUpdate (pUpdate);
    }

    if (action.code != VRDP_SB_ACT_NOP)
    {
        shadowBufferCancelAction (&action);
    }

    vrdpOutputTimersEnd (this, timers, RT_ELEMENTS (timers));

    audioUninit ();

    TCDelete (m_ptc);

    m_outputThread = NIL_RTTHREAD;

    return VINF_SUCCESS;
}
