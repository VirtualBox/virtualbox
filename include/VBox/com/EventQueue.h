/* $Id: EventQueue.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MS COM / XPCOM Abstraction Layer - Event queue class declaration.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_com_EventQueue_h
#define VBOX_INCLUDED_com_EventQueue_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <list>

#include <iprt/asm.h>
#include <iprt/critsect.h>

#include <VBox/com/defs.h>
#include <VBox/com/assert.h>


/** @defgroup grp_com_evtqueue  Event Queue Classes
 * @ingroup grp_com
 * @{
 */

namespace com
{

class EventQueue;

/**
 *  Base class for all events. Intended to be subclassed to introduce new
 *  events and handlers for them.
 *
 *  Subclasses usually reimplement virtual #handler() (that does nothing by
 *  default) and add new data members describing the event.
 */
class Event
{
public:

    Event(void) :
        mRefCount(0) { }
    virtual ~Event(void) { AssertMsg(!mRefCount,
                                     ("Reference count of event=%p not 0 on destruction (is %RU32)\n",
                                      this, mRefCount)); }
public:

    uint32_t AddRef(void) { return ASMAtomicIncU32(&mRefCount); }
    void     Release(void)
    {
        Assert(mRefCount);
        uint32_t cRefs = ASMAtomicDecU32(&mRefCount);
        if (!cRefs)
            delete this;
    }

protected:

    /**
     *  Event handler. Called in the context of the event queue's thread.
     *  Always reimplemented by subclasses
     *
     *  @return reserved, should be NULL.
     */
    virtual void *handler(void) { return NULL; }

    friend class EventQueue;

protected:

    /** The event's reference count. */
    uint32_t mRefCount;
};

typedef std::list< Event* >                 EventQueueList;
typedef std::list< Event* >::iterator       EventQueueListIterator;
typedef std::list< Event* >::const_iterator EventQueueListIteratorConst;

/**
 *  Simple event queue.
 */
class EventQueue
{
public:

    EventQueue(void);
    virtual ~EventQueue(void);

public:

    BOOL postEvent(Event *event);
    int processEventQueue(RTMSINTERVAL cMsTimeout);
    int processPendingEvents(size_t cNumEvents);
    int interruptEventQueueProcessing();

private:

    /** Critical section for serializing access to this
     *  event queue. */
    RTCRITSECT         mCritSect;
    /** Number of concurrent users. At the moment we
     *  only support one concurrent user at a time when
        calling processEventQueue(). */
    uint32_t           mUserCnt;
    /** Event semaphore for getting notified on new
     *  events being handled. */
    RTSEMEVENT         mSemEvent;
    /** The actual event queue, implemented as a list. */
    EventQueueList     mEvents;
    /** Shutdown indicator. */
    bool               mShutdown;
};

} /* namespace com */

/** @} */

#endif /* !VBOX_INCLUDED_com_EventQueue_h */

