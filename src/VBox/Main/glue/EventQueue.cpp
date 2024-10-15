/* $Id: EventQueue.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Event queue class declaration.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

/** @todo Adapt / update documentation! */

#include "VBox/com/EventQueue.h"

#include <iprt/asm.h>
#include <new> /* For bad_alloc. */

#include <iprt/err.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/log.h>

namespace com
{

// EventQueue class
////////////////////////////////////////////////////////////////////////////////

EventQueue::EventQueue(void)
    : mUserCnt(0),
      mShutdown(false)
{
    int vrc = RTCritSectInit(&mCritSect);
    AssertRC(vrc);

    vrc = RTSemEventCreate(&mSemEvent);
    AssertRC(vrc);
}

EventQueue::~EventQueue(void)
{
    int vrc = RTCritSectDelete(&mCritSect);
    AssertRC(vrc);

    vrc = RTSemEventDestroy(mSemEvent);
    AssertRC(vrc);

    EventQueueListIterator it  = mEvents.begin();
    while (it != mEvents.end())
    {
        (*it)->Release();
        it = mEvents.erase(it);
    }
}

/**
 * Process events pending on this event queue, and wait up to given timeout, if
 * nothing is available.
 *
 * Must be called on same thread this event queue was created on.
 *
 * @param   cMsTimeout  The timeout specified as milliseconds.  Use
 *                      RT_INDEFINITE_WAIT to wait till an event is posted on the
 *                      queue.
 *
 * @returns VBox status code
 * @retval  VINF_SUCCESS if one or more messages was processed.
 * @retval  VERR_TIMEOUT if cMsTimeout expired.
 * @retval  VERR_INVALID_CONTEXT if called on the wrong thread.
 * @retval  VERR_INTERRUPTED if interruptEventQueueProcessing was called.
 *          On Windows will also be returned when WM_QUIT is encountered.
 *          On Darwin this may also be returned when the native queue is
 *          stopped or destroyed/finished.
 * @retval  VINF_INTERRUPTED if the native system call was interrupted by a
 *          an asynchronous event delivery (signal) or just felt like returning
 *          out of bounds.  On darwin it will also be returned if the queue is
 *          stopped.
 */
int EventQueue::processEventQueue(RTMSINTERVAL cMsTimeout)
{
    size_t cNumEvents;
    int vrc = RTCritSectEnter(&mCritSect);
    if (RT_SUCCESS(vrc))
    {
        if (mUserCnt == 0) /* No concurrent access allowed. */
        {
            mUserCnt++;

            cNumEvents = mEvents.size();
            if (!cNumEvents)
            {
                int vrc2 = RTCritSectLeave(&mCritSect);
                AssertRC(vrc2);

                vrc = RTSemEventWaitNoResume(mSemEvent, cMsTimeout);

                vrc2 = RTCritSectEnter(&mCritSect);
                AssertRC(vrc2);

                if (RT_SUCCESS(vrc))
                {
                    if (mShutdown)
                        vrc = VERR_INTERRUPTED;
                    cNumEvents = mEvents.size();
                }
            }

            if (RT_SUCCESS(vrc))
                vrc = processPendingEvents(cNumEvents);

            Assert(mUserCnt);
            mUserCnt--;
        }
        else
            vrc = VERR_WRONG_ORDER;

        int vrc2 = RTCritSectLeave(&mCritSect);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;
    }

    Assert(vrc != VERR_TIMEOUT || cMsTimeout != RT_INDEFINITE_WAIT);
    return vrc;
}

/**
 * Processes all pending events in the queue at the time of
 * calling. Note: Does no initial locking, must be done by the
 * caller!
 *
 * @return  IPRT status code.
 */
int EventQueue::processPendingEvents(size_t cNumEvents)
{
    if (!cNumEvents) /* Nothing to process? Bail out early. */
        return VINF_SUCCESS;

    int vrc = VINF_SUCCESS;

    EventQueueListIterator it = mEvents.begin();
    for (size_t i = 0;
            i   < cNumEvents
         && it != mEvents.end(); i++)
    {
        Event *pEvent = *it;
        AssertPtr(pEvent);

        mEvents.erase(it);

        int vrc2 = RTCritSectLeave(&mCritSect);
        AssertRC(vrc2);

        pEvent->handler();
        pEvent->Release();

        vrc2 = RTCritSectEnter(&mCritSect);
        AssertRC(vrc2);

        it = mEvents.begin();
        if (mShutdown)
        {
            vrc = VERR_INTERRUPTED;
            break;
        }
    }

    return vrc;
}

/**
 * Interrupt thread waiting on event queue processing.
 *
 * Can be called on any thread.
 *
 * @returns VBox status code.
 */
int EventQueue::interruptEventQueueProcessing(void)
{
    ASMAtomicWriteBool(&mShutdown, true);

    return RTSemEventSignal(mSemEvent);
}

/**
 *  Posts an event to this event loop asynchronously.
 *
 *  @param pEvent   the event to post, must be allocated using |new|
 *  @return         TRUE if successful and false otherwise
 */
BOOL EventQueue::postEvent(Event *pEvent)
{
    int vrc = RTCritSectEnter(&mCritSect);
    if (RT_SUCCESS(vrc))
    {
        try
        {
            if (pEvent)
            {
                pEvent->AddRef();
                mEvents.push_back(pEvent);
            }
            else /* No locking, since we're already in our crit sect. */
                mShutdown = true;

            size_t cEvents = mEvents.size();
            if (cEvents > _1K) /** @todo Make value configurable? */
            {
                static int s_cBitchedAboutLotEvents = 0;
                if (s_cBitchedAboutLotEvents < 10)
                    LogRel(("Warning: Event queue received lots of events (%zu), expect delayed event handling (%d/10)\n",
                            cEvents, ++s_cBitchedAboutLotEvents));
            }

            /* Leave critical section before signalling event. */
            vrc = RTCritSectLeave(&mCritSect);
            if (RT_SUCCESS(vrc))
            {
                int vrc2 = RTSemEventSignal(mSemEvent);
                AssertRC(vrc2);
            }
        }
        catch (std::bad_alloc &ba)
        {
            NOREF(ba);
            vrc = VERR_NO_MEMORY;
        }

        if (RT_FAILURE(vrc))
        {
            int vrc2 = RTCritSectLeave(&mCritSect);
            AssertRC(vrc2);
        }
    }

    return RT_SUCCESS(vrc) ? TRUE : FALSE;
}

}
/* namespace com */
