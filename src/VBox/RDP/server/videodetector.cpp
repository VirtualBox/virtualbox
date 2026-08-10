/* $Id: videodetector.cpp 114912 2026-08-10 12:03:20Z vitali.pelenjow@oracle.com $ */
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

#include "videostream.h"
#include "videotsmf.h"
#include "vrdpserv.h"
#include "videoencoder.h"
#include "videodetector.h"

/* Prototype video stream detection.
 *
 * Bitmap update rectangles are saved in the "history" list.
 *
 * A new update is compared with rectangles in the list.
 * Same rectangles, which follow after a short period of time,
 * are considered as a video stream.
 * Updates are removed from the "history" list when they are older than 2 seconds or
 * when a new update overlaps them.
 *
 * Detected video streams are saved in the "video" list. If no updates happen for the video
 * for a couple of seconds, the video area is dropped.
 *
 * New bitmap updates which are within the video area are ignored (rendered together with video).
 * New updates which partially overlap the video should be clipped.
 *
 * @todo
 *     * intersecting order -> drop video?
 *     * bitmap update after dropping video
 *     * partially overlapped bitmap updates
 *     * Handler and Stream reset on resize.
 *
 * Implementation
 *
 * VideoDetector (VD)
 * completely in the APP thread, no locks. Detects frames per source stream and informs shadow buffer, which forwards
 * the notification to the video handler. Generates source stream ids. Assigns timestamps for detected frames.
 *
 * VideoHandler (VH)
 * works partially on APP thread, INPUT thread and OUTPUT thread.
 * Detected frames are added to a per stream "source" frames list, which is protected
 * by the source lock. VH also replaces a previous video frame if a new frame comes too early.
 * Maybe 25 FPS max.
 * Output thread part consumes source frames lists and compresses them.
 *
 * Timing:
 *
 * VH maintains timing data for each detected video stream based on frames timestamps and required lipsync delay.
 * This information is per output stream.
 *
 * Each TSMF presentation has a StartTime which is equal to the SampleStartTime of the first frame, which is to
 * be played for the presentation. This StartTime is used for calculations of per presentation Sample times:
 * TSMFSampleStartTime,i = SampleStartTime,i - StartTime.
 *
 * Compressed frames are sent to the client as soon as possible. Stream sample times are calculated as:
 *
 * for the first frame:
 *     SampleStartTime,0 = t,0 + LipSyncDelay
 *     SampleEndTime,0 = t,1 + LipSyncDelay
 *
 * other frames:
 *    SampleStartTime,i = SampleEndTime,i-1;
 *    SampleEndTime,i = t,i + s + LipSyncDelay
 *
 *    where t,i is the timestamp of frame i,
 *          s is the average frame period for a few latest frames.
 *
 * This means that in order to calculate sample times, it is necessary to know timestamps of 2 first frames.
 * After that only the timestamp of current frame is required.
 *
 * Note that detected source frames timestamps are not exact and are delayed for 0..20ms, because of async
 * display update timer. This will be partially addressed by the average frame period method.
 *
 *
 * Scaling
 * Too large video areas are downsized and then JPEG compressed, because a large bitmap needs too much CPU
 * time to be compressed.
 *
 */

#if 0 // def DEBUG_sunlover
#undef VIDEOLOG
#define VIDEOLOG LogRel
#define VIDEOASSERT AssertLogRel
#else
#define VIDEOASSERT Assert
#endif

/*
 * VideoDetector
 */

typedef struct RECTITEM
{
    struct RECTITEM *next;
    RGNRECT rect;
    int64_t timeStart;
    int64_t timeLast;
    int cFrames;
} RECTITEM;

typedef struct VDSOURCESTREAM
{
    RTLISTNODE NodeSourceStream;

    uint32_t u32StreamId;
    RGNRECT rect;
    int64_t timeStart;
    int64_t timeLast;
} VDSOURCESTREAM;

#define VD_STATUS_DISABLED 0
#define VD_STATUS_ENABLED  1

typedef struct VDCONTEXT
{
    VIDEOSTREAMCALLBACKS *pCallbacks;
    void *pvCallback;

    int64_t i64VideoDetectionFrames;
    int64_t i64VideoDetectionMinMS;
    int64_t i64VideoDetectionMaxMS;

    int64_t i64HistoryDecayMS;
    int64_t i64VideoDecayMS;

    RECTITEM *listHistory;

    RTLISTANCHOR ListSourceStreams; /**< VDSOURCESTREAM structures */

    int scItems;
    int scItemsMax;

    VRDPLock *pLockVD; /* Protects command queue. */

    /* OUTPUT thread can post commands to disable and enable video detection,
     * for example when all streams must be restarted.
     * Commands are stored in the queue and polled by VD.
     */
    uint8_t au8Commands[64];
    uint8_t u8CmdFirst;
    uint8_t u8CmdLast;

    uint32_t u32Status; /* Whether videos are detected. */
} VDCONTEXT;

static uint32_t volatile gsu32IdSource = 0;

DECLHIDDEN(uint32_t) idCreate(void)
{
    uint32_t id = ASMAtomicIncU32(&gsu32IdSource);
    while (id == 0)
    {
        id = ASMAtomicIncU32(&gsu32IdSource);
    }
    return id;
}

typedef struct RECTLISTITER
{
    RECTITEM **ppHead;
    RECTITEM *pPrev;
    RECTITEM *pCurrent;
} RECTLISTITER;

/* Initializes an iterator for the list. */
static void rectListIterInit(RECTLISTITER *pIter, RECTITEM **ppHead)
{
    pIter->ppHead = ppHead;
    pIter->pPrev = NULL;
    pIter->pCurrent = *ppHead;
}

/* Returns current element in the list. */
static RECTITEM *rectListIterCurrent(RECTLISTITER *pIter)
{
    return pIter->pCurrent;
}

#ifdef RT_STRICT
/* Returns head element in the list. */
static RECTITEM *rectListIterHead(RECTLISTITER *pIter)
{
    return *pIter->ppHead;
}
#endif

/* Advances the iterator to the next element.
 */
static void rectListIterNext(RECTLISTITER *pIter)
{
    pIter->pPrev = pIter->pCurrent;
    if (pIter->pCurrent)
    {
        pIter->pCurrent = pIter->pCurrent->next;
    }
}

/* Excludes the current element. New current is set to the element, which followed the excluded one.
 * Returns the excluded element.
 */
static RECTITEM *rectListIterCurrentExclude(RECTLISTITER *pIter)
{
    RECTITEM *pExcluded = pIter->pCurrent;

    if (pExcluded)
    {
        RECTITEM *pNext = pExcluded->next;
        if (pIter->pPrev)
        {
            pIter->pPrev->next = pNext;
        }
        else
        {
            *pIter->ppHead = pNext;
        }
        pIter->pCurrent = pNext;

        pExcluded->next = NULL;
    }

    return pExcluded;
}

/* Moves current element to the head of the list.
 * New current is set to the element, which followed the excluded one.
 */
static void rectListIterCurrentMoveToHead(RECTLISTITER *pIter)
{
    VIDEOASSERT(pIter->pCurrent);

    if (pIter->pCurrent != *pIter->ppHead)
    {
        RECTITEM *pCurrent = rectListIterCurrentExclude(pIter);

        if (pCurrent)
        {
            pCurrent->next = *pIter->ppHead;
        }
        *pIter->ppHead = pCurrent;
    }
    else
    {
        /* The current element is already the head. Advance the list position as required by the function spec. */
        rectListIterNext(pIter);
    }
}

static bool vdLock(VDCONTEXT *pCtx)
{
    if (pCtx)
    {
        return RT_SUCCESS(VRDPLock::Lock(pCtx->pLockVD));
    }

    return false;
}

static void vdUnlock(VDCONTEXT *pCtx)
{
    if (pCtx)
    {
        VRDPLock::Unlock(pCtx->pLockVD);
    }
}

static int g_leak_scSourceStreamAlloc;
static VDSOURCESTREAM *vdSourceStreamAlloc(VDCONTEXT *pCtx, const RGNRECT *prect, int64_t timeStart)
{
    RT_NOREF1(pCtx);
    VDSOURCESTREAM *pSourceStream = (VDSOURCESTREAM *)VRDPMemAllocZ(sizeof (VDSOURCESTREAM));
    if (pSourceStream)
    {
        pSourceStream->u32StreamId = idCreate();
        pSourceStream->rect        = *prect;
        pSourceStream->timeStart   = timeStart;
        pSourceStream->timeLast    = timeStart;
g_leak_scSourceStreamAlloc++;
    }
    return pSourceStream;
}

static void vdSourceStreamFree(VDSOURCESTREAM *pSourceStream)
{
g_leak_scSourceStreamAlloc--;
    VRDPMemFree(pSourceStream);
}

#define VD_MIN_WIDTH 64
#define VD_MIN_HEIGHT 64
#define VD_MIN_SQUARE (64*64)
#define VD_ASPECT_RATIO_SCALE 100
#define VD_MIN_ASPECT_RATIO ((1 * VD_ASPECT_RATIO_SCALE) / 2)
#define VD_MAX_ASPECT_RATIO ((3 * VD_ASPECT_RATIO_SCALE) / 1)

/*
 * @return false The bitmap update should be processed normally.
 *         true  The update has been processed by the video detector.
 *               The caller has to generate VRDP_ORDER_VIDEOFRAME.
 */
bool videoDetectorBitmapUpdate(VDCONTEXT *pCtx,
                               RGNRECT *prectUpdate,
                               RGNRECT *prectVideo,
                               uint64_t timeNow,
                               uint32_t *pu32VideoStreamId)
{
    RT_NOREF1(prectVideo);

    /*
     * Check the input rect. Skip rects, which do not look like a video:
     *  * too small rects;
     *  * rects with aspect ratio greater than 3/1 (16:9 video is ~1.8) or less than 1/2.
     */
    if (prectUpdate->w < VD_MIN_WIDTH || prectUpdate->h < VD_MIN_HEIGHT)
    {
        return false;
    }

    if (prectUpdate->w * prectUpdate->h < VD_MIN_SQUARE)
    {
        return false;
    }

    uint32_t u32AspectRatio = (prectUpdate->w * VD_ASPECT_RATIO_SCALE) / prectUpdate->h;

    if (u32AspectRatio > VD_MAX_ASPECT_RATIO || u32AspectRatio < VD_MIN_ASPECT_RATIO)
    {
        return false;
    }

    if (pCtx->u32Status != VD_STATUS_ENABLED)
    {
        return false;
    }

    if (!pCtx->pCallbacks->pfnIsValidRect(pCtx->pvCallback, prectUpdate))
    {
        return false;
    }

#ifdef DEBUG_sunlover
    VIDEOLOG(("@%d,%d %dx%d\n", prectUpdate->x, prectUpdate->y, prectUpdate->w, prectUpdate->h));
#endif /* DEBUG_sunlover */

    /*
     * Check if the update belongs to one of existing videos,
     * that is it has exactly the same size as one of videos.
     */
    VDSOURCESTREAM *pSourceStream = RTListGetFirst(&pCtx->ListSourceStreams,
                                                   VDSOURCESTREAM,
                                                   NodeSourceStream);

    while (pSourceStream)
    {
        VDSOURCESTREAM *pNext = RTListNodeIsLast(&pCtx->ListSourceStreams, &pSourceStream->NodeSourceStream)?
                                    NULL:
                                    RTListNodeGetNext(&pSourceStream->NodeSourceStream,
                                                      VDSOURCESTREAM, NodeSourceStream);

        if (   prectUpdate->x == pSourceStream->rect.x
            && prectUpdate->y == pSourceStream->rect.y
            && prectUpdate->w == pSourceStream->rect.w
            && prectUpdate->h == pSourceStream->rect.h
           )
        {
            /* Check that the frame does not come too early. */
            int64_t i64DeltaT = (int64_t)(timeNow - pSourceStream->timeLast);

            if (i64DeltaT > pCtx->i64VideoDetectionMinMS)
            {
                /* Report the video frame to the handler. */
                pSourceStream->timeLast = timeNow;
                *pu32VideoStreamId = pSourceStream->u32StreamId;
                return true;
            }
            else
            {
                /* Ignore this frame. */
                VHSTATLOG(("VHSTAT: VD: ignoring frame at id%u, i64DeltaT %lld\n",
                           pSourceStream->u32StreamId, i64DeltaT));
                *pu32VideoStreamId = 0;
                return true;
            }
        }

        pSourceStream = pNext;
    }

    /*
     * History list contains all bitmap updates within 2 seconds.
     * Scan it to check the new update.
     */
    RECTLISTITER iterator;
    rectListIterInit(&iterator, &pCtx->listHistory);

    bool fVideoFrame = false;

    RECTITEM *item;
    while ((item = rectListIterCurrent(&iterator)) != NULL)
    {
        if (   prectUpdate->x == item->rect.x
            && prectUpdate->y == item->rect.y
            && prectUpdate->w == item->rect.w
            && prectUpdate->h == item->rect.h
           )
        {
            fVideoFrame = true;

            int64_t i64DeltaT = (int64_t)(timeNow - item->timeLast);

            VHSTATLOG(("VHSTAT: VD: i64DeltaT %lld, %d frames (%d,%d %dx%d)\n",
                       i64DeltaT, item->cFrames, prectUpdate->x, prectUpdate->y, prectUpdate->w, prectUpdate->h));

            if (i64DeltaT < pCtx->i64VideoDetectionMinMS)
            {
                /* A new frame comes too fast, ignore. */
                return false;
            }

            if (i64DeltaT < pCtx->i64VideoDetectionMaxMS)
            {
                item->timeLast = timeNow;
                item->cFrames++;

                if (item->cFrames >= pCtx->i64VideoDetectionFrames)
                {
                    /* This is a video. Remove from history and add to video list,
                     * if accepted by video handler. Return to the caller.
                     */

                    RECTITEM *pVideo = rectListIterCurrentExclude(&iterator);
                    VIDEOASSERT(pVideo == item);
                    VIDEOASSERT(pCtx->listHistory == rectListIterHead(&iterator));

                    pCtx->scItems--;

                    /* Allocate a new source stream. */
                    pSourceStream = vdSourceStreamAlloc(pCtx, &pVideo->rect, timeNow);
                    if (pSourceStream)
                    {
                        VIDEOASSERT(pCtx->pCallbacks->pfnVideoSourceStreamStart);
                        bool fAccepted = pCtx->pCallbacks->pfnVideoSourceStreamStart(pCtx->pvCallback,
                                                                                     pSourceStream->u32StreamId,
                                                                                     &pSourceStream->rect,
                                                                                     timeNow);

                        if (fAccepted)
                        {
                            VHSTATLOG(("VHSTAT: VD: Found id%u @%d,%d %dx%d after %lldms, %d Frames. %d items\n",
                                       pSourceStream->u32StreamId, pVideo->rect.x, pVideo->rect.y, pVideo->rect.w, pVideo->rect.h,
                                       timeNow - pVideo->timeLast, pVideo->cFrames, pCtx->scItems));

                            /* Add to the head of video list. */
                            RTListPrepend(&pCtx->ListSourceStreams, &pSourceStream->NodeSourceStream);

                            *pu32VideoStreamId = pSourceStream->u32StreamId;
                        }
                        else
                        {
                            /* Video handler has not accepted the video stream. */
                            vdSourceStreamFree(pSourceStream);
                            VHSTATLOG(("VHSTAT: VD: stream not accepted!!!\n"));
                        }
                    }
                    else
                    {
                        VIDEOLOG(("VD: failed to allocate stream!!!"));
                    }

                    VRDPMemFree(pVideo);

                    /* Still tell the caller to do a regilar bitmap update.
                     * Only the next frame will be processed as video.
                     */
                    return false;
                }

                /* Move the item with updated cFrames and timeLast to the history head. */
                rectListIterCurrentMoveToHead(&iterator);
                VIDEOASSERT(pCtx->listHistory == rectListIterHead(&iterator));
            }
            else
            {
                /* Remove the item as the new update hides it. */
                VIDEOLOG(("VIDEOVRDP: Hiding %d,%d %dx%d after %lldms, %d Frames. %d items\n",
                           item->rect.x, item->rect.y, item->rect.w, item->rect.h, timeNow - item->timeLast, item->cFrames, pCtx->scItems));

                RECTITEM *pExcluded = rectListIterCurrentExclude(&iterator);
                VIDEOASSERT(pExcluded == item);
                VIDEOASSERT(pCtx->listHistory == rectListIterHead(&iterator));

                pCtx->scItems--;

                VRDPMemFree(pExcluded);
            }

            /* Check other items, may be the new update hides them.
             * Iterator already advanced by either rectListIterCurrentMoveToHead or
             * rectListIterCurrentExclude above.
             */
            continue;
        }

        if (   rgnIsRectWithin(prectUpdate, &item->rect)
            && prectUpdate->w != item->rect.w
            && prectUpdate->h != item->rect.h)
        {
            /* Remove the item as the new update completely hides it. */
            VIDEOLOG(("VIDEOVRDP: Hiding %d,%d %dx%d after %lldms, %d Frames. %d(%d) items\n",
                       item->rect.x, item->rect.y, item->rect.w, item->rect.h, timeNow - item->timeLast, item->cFrames, pCtx->scItems, pCtx->scItemsMax));

            RECTITEM *pExcluded = rectListIterCurrentExclude(&iterator);
            VIDEOASSERT(pExcluded == item);
            VIDEOASSERT(pCtx->listHistory == rectListIterHead(&iterator));

            pCtx->scItems--;

            VRDPMemFree(pExcluded);

            /* Iterator already advanced by rectListIterCurrentExclude above.
             */
            continue;
        }

        /* Advance the iterator. */
        rectListIterNext(&iterator);
    }

    /* Do not add this update, because it already exists in the list. */
    if (fVideoFrame)
    {
        return false;
    }

    /* Update is not a part of existing video sequence. Add new history item to the head. */
    item = (RECTITEM *)VRDPMemAllocZ(sizeof (RECTITEM));
    if (item)
    {
        item->rect = *prectUpdate;
        item->timeStart = timeNow;
        item->timeLast = timeNow;
        item->cFrames = 0;

        item->next = pCtx->listHistory;
        pCtx->listHistory = item;

        /* Statistics. */
        pCtx->scItems++;
        if (pCtx->scItems > pCtx->scItemsMax)
        {
            pCtx->scItemsMax = pCtx->scItems;
        }
    }

    return false;
}

/* Periodic maintenance at the end of VRDP update sequence. */
void videoDetectorUpdateComplete(VDCONTEXT *pCtx)
{
    uint64_t timeNow = VRDPTimeMilliTS();

    /*
     * Check if there are outdated updates.
     */
    VDSOURCESTREAM *pSourceStream = RTListGetFirst(&pCtx->ListSourceStreams,
                                                   VDSOURCESTREAM,
                                                   NodeSourceStream);

    while (pSourceStream)
    {
        VDSOURCESTREAM *pNext = RTListNodeIsLast(&pCtx->ListSourceStreams, &pSourceStream->NodeSourceStream)?
                                    NULL:
                                    RTListNodeGetNext(&pSourceStream->NodeSourceStream,
                                                      VDSOURCESTREAM, NodeSourceStream);

        int64_t i64DeltaT = (int64_t)(timeNow - pSourceStream->timeLast);

        if (i64DeltaT > pCtx->i64VideoDecayMS)
        {
            /* No updates for some time, drop the video. */
            VHSTATLOG(("VHSTAT: VD: videoDetectorUpdateComplete: Dropping id%u @%d,%d %dx%d after %lldms, %lldms total\n",
                       pSourceStream->u32StreamId, pSourceStream->rect.x, pSourceStream->rect.y, pSourceStream->rect.w, pSourceStream->rect.h, timeNow - pSourceStream->timeLast, pSourceStream->timeLast - pSourceStream->timeStart));

            RTListNodeRemove(&pSourceStream->NodeSourceStream);

            /* Inform that the video has been dropped. */
            VIDEOASSERT(pCtx->pCallbacks->pfnVideoSourceStreamStop);
            pCtx->pCallbacks->pfnVideoSourceStreamStop(pCtx->pvCallback, pSourceStream->u32StreamId, &pSourceStream->rect);

            vdSourceStreamFree(pSourceStream);
        }

        pSourceStream = pNext;
    }

    /*
     * History list contains all bitmap updates within 2 seconds.
     * Remove older items.
     */
    RECTLISTITER iterator;
    RECTITEM *item;
    rectListIterInit(&iterator, &pCtx->listHistory);

    while ((item = rectListIterCurrent(&iterator)) != NULL)
    {
        int64_t i64DeltaT = (int64_t)(timeNow - item->timeLast);

        if (i64DeltaT > pCtx->i64HistoryDecayMS)
        {
            /* Remove the obsolete item. */
            VIDEOLOG(("VIDEOVRDP: videoDetectorUpdateComplete: Hiding %d,%d %dx%d after %lldms, %d Frames. %d(%d) items\n",
                       item->rect.x, item->rect.y, item->rect.w, item->rect.h, timeNow - item->timeLast, item->cFrames, pCtx->scItems, pCtx->scItemsMax));

            RECTITEM *pExcluded = rectListIterCurrentExclude(&iterator);
            VIDEOASSERT(pExcluded == item);
            VIDEOASSERT(pCtx->listHistory == rectListIterHead(&iterator));

            VRDPMemFree(pExcluded);

            pCtx->scItems--;

            continue; /* The iterator is already points to the element, which followed the excluded one. */
        }

        rectListIterNext(&iterator);
    }

    /* Poll OUTPUT thread requests, one command at a time is good enough. */
    uint8_t u8VDCmd = VD_CMD_NOP;
    if (vdLock(pCtx))
    {
        if (pCtx->u8CmdLast != pCtx->u8CmdFirst)
        {
            u8VDCmd = pCtx->au8Commands[pCtx->u8CmdFirst];
            pCtx->u8CmdFirst = (pCtx->u8CmdFirst + 1) % RT_ELEMENTS(pCtx->au8Commands);
        }
        vdUnlock(pCtx);
    }

    if (u8VDCmd == VD_CMD_DISABLE)
    {
        if (pCtx->u32Status == VD_STATUS_ENABLED)
        {
            pCtx->u32Status = VD_STATUS_DISABLED;
            videoDetectorReset(pCtx, true);
        }
    }
    else if (u8VDCmd == VD_CMD_ENABLE)
    {
        pCtx->u32Status = VD_STATUS_ENABLED;
    }
    else if (u8VDCmd == VD_CMD_RESET)
    {
        videoDetectorReset(pCtx, true);
    }

    return;
}

/* Check if the order is intersecting with any history items or streams and drop them. */
void videoDetectorOrderUpdate(VDCONTEXT *pCtx, const RGNRECT *prectOrder)
{
    /*
     * Check if there are streams which intersects with the order.
     */
    VDSOURCESTREAM *pSourceStream = RTListGetFirst(&pCtx->ListSourceStreams,
                                                   VDSOURCESTREAM,
                                                   NodeSourceStream);

    while (pSourceStream)
    {
        VDSOURCESTREAM *pNext = RTListNodeIsLast(&pCtx->ListSourceStreams, &pSourceStream->NodeSourceStream)?
                                    NULL:
                                    RTListNodeGetNext(&pSourceStream->NodeSourceStream,
                                                      VDSOURCESTREAM, NodeSourceStream);

        RGNRECT rectResult;

        if (rgnIntersectRects(&rectResult,
                              prectOrder,
                              &pSourceStream->rect))
        {
            /* The order intersects the stream, drop the video. */
            VHSTATLOG(("VHSTAT: VD: videoDetectorOrderUpdate: Dropping id%u @%d,%d %dx%d after %lldms, %lldms total\n",
                       pSourceStream->u32StreamId, pSourceStream->rect.x, pSourceStream->rect.y, pSourceStream->rect.w, pSourceStream->rect.h, VRDPTimeMilliTS() - pSourceStream->timeLast, pSourceStream->timeLast - pSourceStream->timeStart));

            RTListNodeRemove(&pSourceStream->NodeSourceStream);

            /* Inform that the video has been dropped. */
            VIDEOASSERT(pCtx->pCallbacks->pfnVideoSourceStreamStop);
            pCtx->pCallbacks->pfnVideoSourceStreamStop(pCtx->pvCallback, pSourceStream->u32StreamId, &pSourceStream->rect);

            vdSourceStreamFree(pSourceStream);
        }

        pSourceStream = pNext;
    }

    /*
     * History list contains all bitmap updates within 2 seconds.
     * Remove items which intersect with the order.
     */
    RECTLISTITER iterator;
    RECTITEM *item;
    rectListIterInit(&iterator, &pCtx->listHistory);

    while ((item = rectListIterCurrent(&iterator)) != NULL)
    {
        RGNRECT rectResult;

        if (rgnIntersectRects(&rectResult,
                              prectOrder,
                              &item->rect))
        {
            /* Remove the item. */
            VIDEOLOG(("VIDEOVRDP: videoDetectorOrderUpdate: Hiding %d,%d %dx%d after %lldms, %d Frames. %d(%d) items\n",
                       item->rect.x, item->rect.y, item->rect.w, item->rect.h, VRDPTimeMilliTS() - item->timeLast, item->cFrames, pCtx->scItems, pCtx->scItemsMax));

            RECTITEM *pExcluded = rectListIterCurrentExclude(&iterator);
            VIDEOASSERT(pExcluded == item);
            VIDEOASSERT(pCtx->listHistory == rectListIterHead(&iterator));

            VRDPMemFree(pExcluded);

            pCtx->scItems--;

            continue; /* The iterator is already points to the element, which followed the excluded one. */
        }

        rectListIterNext(&iterator);
    }

    return;
}

DECLHIDDEN(int) queryPropertyInt64Def(VRDPServer *pServer,
                                      const char *pszProperty,
                                      int64_t *pi64Val,
                                      int64_t  i64Def)
 {
    int rc = VINF_SUCCESS;
    char *pszValue = NULL;
    uint32_t cbOut = 0;

    rc = appFeature (pServer->ApplicationCallbacks (), pServer->ApplicationCallbackPointer (),
                     pszProperty, &pszValue, &cbOut);

    /* Set to default if no value. */
    if (   !pszValue
        || *pszValue == '\0')
        *pi64Val = i64Def;
    else
        rc = RTStrToInt64Full(pszValue, 10, pi64Val);

    if (pszValue)
        VRDPMemFree(pszValue);

    return rc;
}

#if 0 /* unused */
static int queryPropertyBoolDef(VRDPServer *pServer,
                                const char *pszProperty,
                                bool *pfVal,
                                bool fDef)
 {
    int rc = VINF_SUCCESS;
    char *pszValue = NULL;
    uint32_t cbOut = 0;

    rc = appFeature (pServer->ApplicationCallbacks (), pServer->ApplicationCallbackPointer (),
                     pszProperty, &pszValue, &cbOut);

    /* Set to default if no value. */
    if (   !pszValue
        || *pszValue == '\0')
        *pfVal = fDef;
    else
    {
        if (   RTStrCmp(pszValue, "1") == 0
            || RTStrICmp(pszValue, "true") == 0)
        {
            *pfVal = true;
        }
    }

    if (pszValue)
        VRDPMemFree(pszValue);

    return rc;
}
#endif

int videoDetectorContextCreate(VDCONTEXT **ppCtx, VIDEOSTREAMCALLBACKS *pCallbacks, void *pvCallback, VRDPServer *pServer)
{
    AssertReturn(pCallbacks != NULL, VERR_INVALID_PARAMETER);

    VDCONTEXT *pCtx = (VDCONTEXT *)VRDPMemAlloc(sizeof (VDCONTEXT));
    if (!pCtx)
    {
        return VERR_NO_MEMORY;
    }

    int rc = VRDPLock::Create("VD", &pCtx->pLockVD);
    if (RT_SUCCESS (rc))
    {
        pCtx->pCallbacks = pCallbacks;
        pCtx->pvCallback = pvCallback;

        /* How many frames must be detected before starting a video. */
        rc = queryPropertyInt64Def(pServer, "Property/VideoDetector/VideoDetectionFrames",
                                   &pCtx->i64VideoDetectionFrames, 10);

        /* Default: 15ms=1.5*10ms, where 10ms is minimal VRDP update period. */
        if (RT_SUCCESS(rc))
            rc = queryPropertyInt64Def(pServer, "Property/VideoDetector/VideoDetectionMinMS",
                                       &pCtx->i64VideoDetectionMinMS, 15);

        /* Default: 300ms=1.5*200ms, to reliably detect videos > 5FPS. */
        if (RT_SUCCESS(rc))
            rc = queryPropertyInt64Def(pServer, "Property/VideoDetector/VideoDetectionMaxMS",
                                       &pCtx->i64VideoDetectionMaxMS, 300);

        /* How long to keep updates in the history. */
        if (RT_SUCCESS(rc))
            rc = queryPropertyInt64Def(pServer, "Property/VideoDetector/HistoryDecayMS",
                                       &pCtx->i64HistoryDecayMS, 2000);

        /* Video is dropped if _no_ updates within this period come. */
        if (RT_SUCCESS(rc))
            rc = queryPropertyInt64Def(pServer, "Property/VideoDetector/VideoDecayMS",
                                       &pCtx->i64VideoDecayMS, 300);

        pCtx->listHistory = NULL;

        pCtx->scItems = 0;
        pCtx->scItemsMax = 0;

        RTListInit(&pCtx->ListSourceStreams);

        RT_ZERO(pCtx->au8Commands);
        pCtx->u8CmdFirst = 0;
        pCtx->u8CmdLast = 0;

        pCtx->u32Status = VD_STATUS_ENABLED;

        VRDPLOGREL(("VRDP: VD: Frames=%llu MinMS=%llu MaxMS=%llu HistoryMS=%llu VideoMS=%llu\n",
                    pCtx->i64VideoDetectionFrames,
                    pCtx->i64VideoDetectionMinMS,
                    pCtx->i64VideoDetectionMaxMS,
                    pCtx->i64HistoryDecayMS,
                    pCtx->i64VideoDecayMS));

        *ppCtx = pCtx;
    }
    else
    {
        videoDetectorContextDelete(pCtx);
    }


    return VINF_SUCCESS;
}

void videoDetectorContextDelete(VDCONTEXT *pCtx)
{
    if (pCtx)
    {
        videoDetectorReset(pCtx, false);

        VRDPLock::Delete(&pCtx->pLockVD);

        VRDPMemFree(pCtx);
    }
}

void videoDetectorStoreCommand(VDCONTEXT *pCtx, uint8_t u8VDCmd)
{
    if (vdLock(pCtx))
    {
        /* Write at the current position. */
        pCtx->au8Commands[pCtx->u8CmdLast] = u8VDCmd;
        pCtx->u8CmdLast = (pCtx->u8CmdLast + 1) % RT_ELEMENTS(pCtx->au8Commands);

        /* If the queue is full, drop the oldest command. */
        if (pCtx->u8CmdLast == pCtx->u8CmdFirst)
        {
            pCtx->u8CmdFirst = (pCtx->u8CmdFirst + 1) % RT_ELEMENTS(pCtx->au8Commands);
        }

        vdUnlock(pCtx);
    }
}

void videoDetectorReset(VDCONTEXT *pCtx, bool fStopOutputStreams)
{
    if (!pCtx)
    {
        return;
    }

    /*
     * Clear detected videos.
     */
    VDSOURCESTREAM *pSourceStream = RTListGetFirst(&pCtx->ListSourceStreams,
                                                   VDSOURCESTREAM,
                                                   NodeSourceStream);

    while (pSourceStream)
    {
        VDSOURCESTREAM *pNext = RTListNodeIsLast(&pCtx->ListSourceStreams, &pSourceStream->NodeSourceStream)?
                                    NULL:
                                    RTListNodeGetNext(&pSourceStream->NodeSourceStream,
                                                      VDSOURCESTREAM, NodeSourceStream);

        VIDEOLOG(("VIDEOVRDP: videoDetectorReset: Dropping id%u @%d,%d %dx%d\n",
                   pSourceStream->u32StreamId, pSourceStream->rect.x, pSourceStream->rect.y, pSourceStream->rect.w, pSourceStream->rect.h));

        RTListNodeRemove(&pSourceStream->NodeSourceStream);

        vdSourceStreamFree(pSourceStream);

        if (fStopOutputStreams)
        {
            /* Inform that the video has been dropped. */
            VIDEOASSERT(pCtx->pCallbacks->pfnVideoSourceStreamStop);
            pCtx->pCallbacks->pfnVideoSourceStreamStop(pCtx->pvCallback, pSourceStream->u32StreamId, &pSourceStream->rect);
        }

        pSourceStream = pNext;
    }

    RTListInit(&pCtx->ListSourceStreams);

    /*
     * Remove history list items.
     */
    RECTLISTITER iterator;
    RECTITEM *item;
    rectListIterInit(&iterator, &pCtx->listHistory);

    while ((item = rectListIterCurrent(&iterator)) != NULL)
    {
        VIDEOLOG(("VIDEOVRDP: videoDetectorReset: Hiding %d,%d %dx%d. %d(%d) items\n",
                   item->rect.x, item->rect.y, item->rect.w, item->rect.h, pCtx->scItems, pCtx->scItemsMax));

        RECTITEM *pExcluded = rectListIterCurrentExclude(&iterator);
        VIDEOASSERT(pExcluded == item);
        VIDEOASSERT(pCtx->listHistory == rectListIterHead(&iterator));

        VRDPMemFree(pExcluded);

        pCtx->scItems--;

        /* The iterator is already points to the element, which followed the excluded one. */
    }
}
