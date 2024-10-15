/* $Id: EventImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM Event class implementation
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

/** @page pg_main_events    Events
 *
 * Theory of operations.
 *
 * This code implements easily extensible event mechanism, letting us
 * to make any VirtualBox object an event source (by aggregating an EventSource instance).
 * Another entity could subscribe to the event source for events it is interested in.
 * If an event is waitable, it's possible to wait until all listeners
 * registered at the moment of firing event as ones interested in this
 * event acknowledged that they finished event processing (thus allowing
 * vetoable events).
 *
 * Listeners can be registered as active or passive ones, defining policy of delivery.
 * For *active* listeners, their HandleEvent() method is invoked when event is fired by
 * the event source (pretty much callbacks).
 * For *passive* listeners, it's up to an event consumer to perform GetEvent() operation
 * with given listener, and then perform desired operation with returned event, if any.
 * For passive listeners case, listener instance serves as merely a key referring to
 * particular event consumer, thus HandleEvent() implementation isn't that important.
 * IEventSource's CreateListener() could be used to create such a listener.
 * Passive mode is designed for transports not allowing callbacks, such as webservices
 * running on top of HTTP, and for situations where consumer wants exact control on
 * context where event handler is executed (such as GUI thread for some toolkits).
 *
 * Internal EventSource data structures are optimized for fast event delivery, while
 * listener registration/unregistration operations are expected being pretty rare.
 * Passive mode listeners keep an internal event queue for all events they receive,
 * and all waitable events are added to the pending events map. This map keeps track
 * of how many listeners are still not acknowledged their event, and once this counter
 * reach zero, element is removed from pending events map, and event is marked as processed.
 * Thus if passive listener's user forgets to call IEventSource's EventProcessed()
 * waiters may never know that event processing finished.
 */

#define LOG_GROUP LOG_GROUP_MAIN_EVENT
#include <list>
#include <map>
#include <deque>

#include "EventImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"
#include "VBoxEvents.h"

#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/errcore.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>

#include <VBox/com/array.h>

class ListenerRecord;

struct VBoxEvent::Data
{
    Data()
        : mType(VBoxEventType_Invalid),
          mWaitEvent(NIL_RTSEMEVENT),
          mWaitable(FALSE),
          mProcessed(FALSE)
    {}

    VBoxEventType_T         mType;
    RTSEMEVENT              mWaitEvent;
    BOOL                    mWaitable;
    BOOL                    mProcessed;
    ComPtr<IEventSource>    mSource;
};

DEFINE_EMPTY_CTOR_DTOR(VBoxEvent)

HRESULT VBoxEvent::FinalConstruct()
{
    m = new Data;
    return BaseFinalConstruct();
}

void VBoxEvent::FinalRelease()
{
    if (m)
    {
        uninit();
        delete m;
        m = NULL;
    }
    BaseFinalRelease();
}

HRESULT VBoxEvent::init(IEventSource *aSource, VBoxEventType_T aType, BOOL aWaitable)
{
    AssertReturn(aSource != NULL, E_INVALIDARG);

    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m->mSource = aSource;
    m->mType = aType;
    m->mWaitable = aWaitable;
    m->mProcessed = !aWaitable;

    do
    {
        if (aWaitable)
        {
            int vrc = ::RTSemEventCreate(&m->mWaitEvent);

            if (RT_FAILURE(vrc))
            {
                AssertFailed();
                return setError(E_FAIL,
                                tr("Internal error (%Rrc)"), vrc);
            }
        }
    } while (0);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

void VBoxEvent::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    if (!m)
        return;

    m->mProcessed = TRUE;
    m->mType = VBoxEventType_Invalid;
    m->mSource.setNull();

    if (m->mWaitEvent != NIL_RTSEMEVENT)
    {
        Assert(m->mWaitable);
        ::RTSemEventDestroy(m->mWaitEvent);
        m->mWaitEvent = NIL_RTSEMEVENT;
    }
}

HRESULT VBoxEvent::getType(VBoxEventType_T *aType)
{
    // never changes while event alive, no locking
    *aType = m->mType;
    return S_OK;
}

HRESULT VBoxEvent::getSource(ComPtr<IEventSource> &aSource)
{
    m->mSource.queryInterfaceTo(aSource.asOutParam());
    return S_OK;
}

HRESULT VBoxEvent::getWaitable(BOOL *aWaitable)
{
    // never changes while event alive, no locking
    *aWaitable = m->mWaitable;
    return S_OK;
}

HRESULT VBoxEvent::setProcessed()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->mProcessed)
        return S_OK;

    m->mProcessed = TRUE;

    // notify waiters
    ::RTSemEventSignal(m->mWaitEvent);

    return S_OK;
}

HRESULT VBoxEvent::waitProcessed(LONG aTimeout, BOOL *aResult)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->mProcessed)
    {
        *aResult = TRUE;
        return S_OK;
    }

    if (aTimeout == 0)
    {
        *aResult = m->mProcessed;
        return S_OK;
    }

    // must drop lock while waiting, because setProcessed() needs synchronization.
    alock.release();
    /** @todo maybe while loop for spurious wakeups? */
    int vrc = ::RTSemEventWait(m->mWaitEvent, aTimeout < 0 ? RT_INDEFINITE_WAIT : (RTMSINTERVAL)aTimeout);
    AssertMsg(RT_SUCCESS(vrc) || vrc == VERR_TIMEOUT || vrc == VERR_INTERRUPTED,
              ("RTSemEventWait returned %Rrc\n", vrc));
    alock.acquire();

    if (RT_SUCCESS(vrc))
    {
        AssertMsg(m->mProcessed,
                  ("mProcessed must be set here\n"));
        *aResult = m->mProcessed;
    }
    else
    {
        *aResult = FALSE;
        /*
         * If we timed out then one or more passive listeners didn't process this event
         * within the time limit most likely due to the listener no longer being alive (e.g.
         * the VirtualBox GUI crashed) so we flag this to our caller so it can remove this
         * event from the list of events the passive listener is interested in.  This avoids
         * incurring this timeout every time the event is fired.
         */
        if (vrc == VERR_TIMEOUT)
            return E_ABORT;
    }

    return S_OK;
}

typedef std::list<Utf8Str> VetoList;
typedef std::list<Utf8Str> ApprovalList;
struct VBoxVetoEvent::Data
{
    Data() :
        mVetoed(FALSE)
    {}
    ComObjPtr<VBoxEvent>    mEvent;
    BOOL                    mVetoed;
    VetoList                mVetoList;
    ApprovalList            mApprovalList;
};

HRESULT VBoxVetoEvent::FinalConstruct()
{
    m = new Data;
    HRESULT hrc = m->mEvent.createObject();
    BaseFinalConstruct();
    return hrc;
}

void VBoxVetoEvent::FinalRelease()
{
    if (m)
    {
        uninit();
        delete m;
        m = NULL;
    }
    BaseFinalRelease();
}

DEFINE_EMPTY_CTOR_DTOR(VBoxVetoEvent)

HRESULT VBoxVetoEvent::init(IEventSource *aSource, VBoxEventType_T aType)
{
    // all veto events are waitable
    HRESULT hrc = m->mEvent->init(aSource, aType, TRUE);
    if (FAILED(hrc))
        return hrc;

    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m->mVetoed = FALSE;
    m->mVetoList.clear();
    m->mApprovalList.clear();

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

void VBoxVetoEvent::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    if (!m)
        return;

    m->mVetoed = FALSE;
    if (!m->mEvent.isNull())
    {
        m->mEvent->uninit();
        m->mEvent.setNull();
    }
}

HRESULT VBoxVetoEvent::getType(VBoxEventType_T *aType)
{
    return m->mEvent->COMGETTER(Type)(aType);
}

HRESULT VBoxVetoEvent::getSource(ComPtr<IEventSource> &aSource)
{
    return m->mEvent->COMGETTER(Source)(aSource.asOutParam());
}

HRESULT VBoxVetoEvent::getWaitable(BOOL *aWaitable)
{
    return m->mEvent->COMGETTER(Waitable)(aWaitable);
}

HRESULT VBoxVetoEvent::setProcessed()
{
    return m->mEvent->SetProcessed();
}

HRESULT VBoxVetoEvent::waitProcessed(LONG aTimeout, BOOL *aResult)
{
    return m->mEvent->WaitProcessed(aTimeout, aResult);
}

HRESULT VBoxVetoEvent::addVeto(const com::Utf8Str &aReason)
{
    // AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (aReason.length())
        m->mVetoList.push_back(aReason);

    m->mVetoed = TRUE;

    return S_OK;
}

HRESULT VBoxVetoEvent::isVetoed(BOOL *aResult)
{
    // AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aResult = m->mVetoed;

    return S_OK;
}

HRESULT VBoxVetoEvent::getVetos(std::vector<com::Utf8Str> &aResult)
{
    // AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aResult.resize(m->mVetoList.size());
    size_t i = 0;
    for (VetoList::const_iterator it = m->mVetoList.begin(); it != m->mVetoList.end(); ++it, ++i)
        aResult[i] = (*it);

    return S_OK;

}

HRESULT VBoxVetoEvent::addApproval(const com::Utf8Str &aReason)
{
    // AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->mApprovalList.push_back(aReason);
    return S_OK;
}

HRESULT VBoxVetoEvent::isApproved(BOOL *aResult)
{
    // AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aResult = !m->mApprovalList.empty();
    return S_OK;
}

HRESULT VBoxVetoEvent::getApprovals(std::vector<com::Utf8Str> &aResult)
{
    // AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aResult.resize(m->mApprovalList.size());
    size_t i = 0;
    for (ApprovalList::const_iterator it = m->mApprovalList.begin(); it != m->mApprovalList.end(); ++it, ++i)
        aResult[i] = (*it);
    return S_OK;
}

static const int FirstEvent = (int)VBoxEventType_LastWildcard + 1;
static const int LastEvent = (int)VBoxEventType_End;
static const int NumEvents = LastEvent - FirstEvent;

/**
 * Class replacing std::list and able to provide required stability
 * during iteration. It's acheived by delaying structural modifications
 * to the list till the moment particular element is no longer used by
 * current iterators.
 */
class EventMapRecord
{
public:
    /**
     * We have to be double linked, as structural modifications in list are delayed
     * till element removed, so we have to know our previous one to update its next
     */
    EventMapRecord *mNext;
    bool            mAlive;
private:
    EventMapRecord *mPrev;
    ListenerRecord *mRef; /* must be weak reference */
    int32_t         mRefCnt;

public:
    EventMapRecord(ListenerRecord *aRef) :
        mNext(0), mAlive(true), mPrev(0), mRef(aRef), mRefCnt(1)
    {}

    EventMapRecord(EventMapRecord &aOther)
    {
        mNext = aOther.mNext;
        mPrev = aOther.mPrev;
        mRef = aOther.mRef;
        mRefCnt = aOther.mRefCnt;
        mAlive = aOther.mAlive;
    }

    ~EventMapRecord()
    {
        if (mNext)
            mNext->mPrev = mPrev;
        if (mPrev)
            mPrev->mNext = mNext;
    }

    void addRef()
    {
        ASMAtomicIncS32(&mRefCnt);
    }

    void release()
    {
        if (ASMAtomicDecS32(&mRefCnt) <= 0)
            delete this;
    }

    // Called when an element is no longer needed
    void kill()
    {
        mAlive = false;
        release();
    }

    ListenerRecord *ref()
    {
        return mAlive ? mRef : 0;
    }

    friend class EventMapList;
};


class EventMapList
{
    EventMapRecord *mHead;
    uint32_t        mSize;
public:
    EventMapList()
        :
        mHead(0),
        mSize(0)
    {}
    ~EventMapList()
    {
        EventMapRecord *pCur = mHead;
        while (pCur)
        {
            EventMapRecord *pNext = pCur->mNext;
            pCur->release();
            pCur = pNext;
        }
    }

    /*
     * Elements have to be added to the front of the list, to make sure
     * that iterators doesn't see newly added listeners, and iteration
     * will always complete.
     */
    void add(ListenerRecord *aRec)
    {
        EventMapRecord *pNew = new EventMapRecord(aRec);
        pNew->mNext = mHead;
        if (mHead)
            mHead->mPrev = pNew;
        mHead = pNew;
        mSize++;
    }

    /*
     * Mark element as removed, actual removal could be delayed until
     * all consumers release it too. This helps to keep list stable
     * enough for iterators to allow long and probably intrusive callbacks.
     */
    void remove(ListenerRecord *aRec)
    {
        EventMapRecord *pCur = mHead;
        while (pCur)
        {
            EventMapRecord *aNext = pCur->mNext;
            if (pCur->ref() == aRec)
            {
                if (pCur == mHead)
                    mHead = aNext;
                pCur->kill();
                mSize--;
                // break?
            }
            pCur = aNext;
        }
    }

    uint32_t size() const
    {
        return mSize;
    }

    struct iterator
    {
        EventMapRecord *mCur;

        iterator() :
            mCur(0)
        {}

        explicit
        iterator(EventMapRecord *aCur) :
            mCur(aCur)
        {
            // Prevent element removal, till we're at it
            if (mCur)
                mCur->addRef();
        }

        ~iterator()
        {
            if (mCur)
                mCur->release();
        }

        ListenerRecord *
        operator*() const
        {
            return mCur->ref();
        }

        EventMapList::iterator &
        operator++()
        {
            EventMapRecord *pPrev = mCur;
            do {
                mCur = mCur->mNext;
            } while (mCur && !mCur->mAlive);

            // now we can safely release previous element
            pPrev->release();

            // And grab the new current
            if (mCur)
                mCur->addRef();

            return *this;
        }

        bool
        operator==(const EventMapList::iterator &aOther) const
        {
            return mCur == aOther.mCur;
        }

        bool
        operator!=(const EventMapList::iterator &aOther) const
        {
            return mCur != aOther.mCur;
        }
    };

    iterator begin()
    {
        return iterator(mHead);
    }

    iterator end()
    {
        return iterator(0);
    }
};

typedef EventMapList EventMap[NumEvents];
typedef std::map<IEvent *, int32_t> PendingEventsMap;
typedef std::deque<ComPtr<IEvent> > PassiveQueue;

class ListenerRecord
{
private:
    ComPtr<IEventListener>        mListener;
    BOOL const                    mActive;
    EventSource                  *mOwner;

    RTSEMEVENT                    mQEvent;
    int32_t volatile              mQEventBusyCnt;
    RTCRITSECT                    mcsQLock;
    PassiveQueue                  mQueue;
    int32_t volatile              mRefCnt;
    uint64_t                      mLastRead;

public:
    ListenerRecord(IEventListener *aListener,
                   com::SafeArray<VBoxEventType_T> &aInterested,
                   BOOL aActive,
                   EventSource *aOwner);
    ~ListenerRecord();

    HRESULT process(IEvent *aEvent, BOOL aWaitable, PendingEventsMap::iterator &pit, AutoLockBase &alock);
    HRESULT enqueue(IEvent *aEvent);
    HRESULT dequeue(IEvent **aEvent, LONG aTimeout, AutoLockBase &aAlock);
    HRESULT eventProcessed(IEvent *aEvent, PendingEventsMap::iterator &pit);
    void shutdown();

    void addRef()
    {
        ASMAtomicIncS32(&mRefCnt);
    }

    void release()
    {
        if (ASMAtomicDecS32(&mRefCnt) <= 0)
            delete this;
    }

    BOOL isActive()
    {
        return mActive;
    }

    friend class EventSource;
};

/* Handy class with semantics close to ComPtr, but for list records */
template<typename Held>
class RecordHolder
{
public:
    RecordHolder(Held *lr) :
        held(lr)
    {
        addref();
    }
    RecordHolder(const RecordHolder &that) :
        held(that.held)
    {
        addref();
    }
    RecordHolder()
    :
    held(0)
    {
    }
    ~RecordHolder()
    {
        release();
    }

    Held *obj()
    {
        return held;
    }

    RecordHolder &operator=(const RecordHolder &that)
    {
        safe_assign(that.held);
        return *this;
    }
private:
    Held *held;

    void addref()
    {
        if (held)
            held->addRef();
    }
    void release()
    {
        if (held)
            held->release();
    }
    void safe_assign(Held *that_p)
    {
        if (that_p)
            that_p->addRef();
        release();
        held = that_p;
    }
};

typedef std::map<IEventListener *, RecordHolder<ListenerRecord> > Listeners;

struct EventSource::Data
{
    Data() : fShutdown(false)
    {}

    Listeners                     mListeners;
    EventMap                      mEvMap;
    PendingEventsMap              mPendingMap;
    bool                          fShutdown;
};

/**
 * This function defines what wildcard expands to.
 */
static BOOL implies(VBoxEventType_T who, VBoxEventType_T what)
{
    switch (who)
    {
        case VBoxEventType_Any:
            return TRUE;
        case VBoxEventType_Vetoable:
            return    (what == VBoxEventType_OnExtraDataCanChange)
                   || (what == VBoxEventType_OnCanShowWindow);
        case VBoxEventType_MachineEvent:
            return    (what == VBoxEventType_OnMachineStateChanged)
                   || (what == VBoxEventType_OnMachineDataChanged)
                   || (what == VBoxEventType_OnMachineRegistered)
                   || (what == VBoxEventType_OnSessionStateChanged)
                   || (what == VBoxEventType_OnGuestPropertyChanged);
        case VBoxEventType_SnapshotEvent:
            return    (what == VBoxEventType_OnSnapshotTaken)
                   || (what == VBoxEventType_OnSnapshotDeleted)
                   || (what == VBoxEventType_OnSnapshotChanged) ;
        case VBoxEventType_InputEvent:
            return    (what == VBoxEventType_OnKeyboardLedsChanged)
                   || (what == VBoxEventType_OnMousePointerShapeChanged)
                   || (what == VBoxEventType_OnMouseCapabilityChanged);
        case VBoxEventType_Invalid:
            return FALSE;
        default:
            break;
    }

    return who == what;
}

ListenerRecord::ListenerRecord(IEventListener *aListener,
                               com::SafeArray<VBoxEventType_T> &aInterested,
                               BOOL aActive,
                               EventSource *aOwner) :
    mListener(aListener), mActive(aActive), mOwner(aOwner), mQEventBusyCnt(0), mRefCnt(0)
{
    EventMap *aEvMap = &aOwner->m->mEvMap;

    for (size_t i = 0; i < aInterested.size(); ++i)
    {
        VBoxEventType_T interested = aInterested[i];
        for (int j = FirstEvent; j < LastEvent; j++)
        {
            VBoxEventType_T candidate = (VBoxEventType_T)j;
            if (implies(interested, candidate))
            {
                (*aEvMap)[j - FirstEvent].add(this);
            }
        }
    }

    if (!mActive)
    {
        ::RTCritSectInit(&mcsQLock);
        ::RTSemEventCreate(&mQEvent);
        mLastRead = RTTimeMilliTS();
    }
    else
    {
        mQEvent = NIL_RTSEMEVENT;
        RT_ZERO(mcsQLock);
        mLastRead = 0;
    }
}

ListenerRecord::~ListenerRecord()
{
    /* Remove references to us from the event map */
    EventMap *aEvMap = &mOwner->m->mEvMap;
    for (int j = FirstEvent; j < LastEvent; j++)
    {
        (*aEvMap)[j - FirstEvent].remove(this);
    }

    if (!mActive)
    {
        // at this moment nobody could add elements to our queue, so we can safely
        // clean it up, otherwise there will be pending events map elements
        PendingEventsMap *aPem = &mOwner->m->mPendingMap;
        while (true)
        {
            ComPtr<IEvent> aEvent;

            if (mQueue.empty())
                break;

            mQueue.front().queryInterfaceTo(aEvent.asOutParam());
            mQueue.pop_front();

            BOOL fWaitable = FALSE;
            aEvent->COMGETTER(Waitable)(&fWaitable);
            if (fWaitable)
            {
                PendingEventsMap::iterator pit = aPem->find(aEvent);
                if (pit != aPem->end())
                    eventProcessed(aEvent, pit);
            }
        }

        ::RTCritSectDelete(&mcsQLock);
    }
    shutdown();
}

HRESULT ListenerRecord::process(IEvent *aEvent,
                                BOOL aWaitable,
                                PendingEventsMap::iterator &pit,
                                AutoLockBase &aAlock)
{
    if (mActive)
    {
        /*
         * We release lock here to allow modifying ops on EventSource inside callback.
         */
        HRESULT hrc = S_OK;
        if (mListener)
        {
            aAlock.release();
            hrc = mListener->HandleEvent(aEvent);
#ifdef RT_OS_WINDOWS
            Assert(hrc != RPC_E_WRONG_THREAD);
#endif
            aAlock.acquire();
        }
        if (aWaitable)
            eventProcessed(aEvent, pit);
        return hrc;
    }
    return enqueue(aEvent);
}


HRESULT ListenerRecord::enqueue(IEvent *aEvent)
{
    AssertMsg(!mActive, ("must be passive\n"));

    // put an event the queue
    ::RTCritSectEnter(&mcsQLock);

    // If there was no events reading from the listener for the long time,
    // and events keep coming, or queue is oversized we shall unregister this listener.
    uint64_t sinceRead = RTTimeMilliTS() - mLastRead;
    size_t queueSize = mQueue.size();
    if (queueSize > 1000 || (queueSize > 500 && sinceRead > 60 * 1000))
    {
        ::RTCritSectLeave(&mcsQLock);
        LogRel(("Event: forcefully unregistering passive event listener %p due to excessive queue size\n", this));
        return E_ABORT;
    }


    RTSEMEVENT hEvt = mQEvent;
    if (queueSize != 0 && mQueue.back() == aEvent)
        /* if same event is being pushed multiple times - it's reusable event and
           we don't really need multiple instances of it in the queue */
        hEvt = NIL_RTSEMEVENT;
    else if (hEvt != NIL_RTSEMEVENT) /* don't bother queuing after shutdown */
    {
        mQueue.push_back(aEvent);
        ASMAtomicIncS32(&mQEventBusyCnt);
    }

    ::RTCritSectLeave(&mcsQLock);

    // notify waiters unless we've been shut down.
    if (hEvt != NIL_RTSEMEVENT)
    {
        ::RTSemEventSignal(hEvt);
        ASMAtomicDecS32(&mQEventBusyCnt);
    }

    return S_OK;
}

HRESULT ListenerRecord::dequeue(IEvent **aEvent,
                                LONG aTimeout,
                                AutoLockBase &aAlock)
{
    if (mActive)
        return VBOX_E_INVALID_OBJECT_STATE;

    // retain listener record
    RecordHolder<ListenerRecord> holder(this);

    ::RTCritSectEnter(&mcsQLock);

    mLastRead = RTTimeMilliTS();

    /*
     * If waiting both desired and necessary, then try grab the event
     * semaphore and mark it busy.  If it's NIL we've been shut down already.
     */
    if (aTimeout != 0 && mQueue.empty())
    {
        RTSEMEVENT hEvt = mQEvent;
        if (hEvt != NIL_RTSEMEVENT)
        {
            ASMAtomicIncS32(&mQEventBusyCnt);
            ::RTCritSectLeave(&mcsQLock);

            // release lock while waiting, listener will not go away due to above holder
            aAlock.release();

            ::RTSemEventWait(hEvt, aTimeout < 0 ? RT_INDEFINITE_WAIT : (RTMSINTERVAL)aTimeout);
            ASMAtomicDecS32(&mQEventBusyCnt);

            // reacquire lock
            aAlock.acquire();
            ::RTCritSectEnter(&mcsQLock);
        }
    }

    if (mQueue.empty())
        *aEvent = NULL;
    else
    {
        mQueue.front().queryInterfaceTo(aEvent);
        mQueue.pop_front();
    }

    ::RTCritSectLeave(&mcsQLock);
    return S_OK;
}

HRESULT ListenerRecord::eventProcessed(IEvent *aEvent, PendingEventsMap::iterator &pit)
{
    if (--pit->second == 0)
    {
        Assert(pit->first == aEvent);
        aEvent->SetProcessed();
        mOwner->m->mPendingMap.erase(pit);
    }

    return S_OK;
}

void ListenerRecord::shutdown()
{
    if (mQEvent != NIL_RTSEMEVENT)
    {
        /* Grab the event semaphore.  Must do this while owning the CS or we'll
           be racing user wanting to use the handle. */
        ::RTCritSectEnter(&mcsQLock);
        RTSEMEVENT hEvt = mQEvent;
        mQEvent = NIL_RTSEMEVENT;
        ::RTCritSectLeave(&mcsQLock);

        /*
         * Signal waiters and wait for them and any other signallers to stop using the sempahore.
         *
         * Note! RTSemEventDestroy does not necessarily guarantee that waiting threads are
         *       out of RTSemEventWait or even woken up when it returns.  Darwin is (or was?)
         *       an example of this, the result was undesirable freezes on shutdown.
         */
        int32_t cBusy = ASMAtomicReadS32(&mQEventBusyCnt);
        if (cBusy > 0)
        {
            Log(("Wait for %d waiters+signalers to release.\n", cBusy));
            while (cBusy-- > 0)
                ::RTSemEventSignal(hEvt);

            for (uint32_t cLoops = 0;; cLoops++)
            {
                RTThreadSleep(RT_MIN(8, cLoops));
                if (ASMAtomicReadS32(&mQEventBusyCnt) <= 0)
                    break;
                ::RTSemEventSignal(hEvt); /* (Technically unnecessary, but just in case.) */
            }
            Log(("All waiters+signalers just released the lock.\n"));
        }

        ::RTSemEventDestroy(hEvt);
    }
}

EventSource::EventSource()
{}

EventSource::~EventSource()
{}

HRESULT EventSource::FinalConstruct()
{
    m = new Data;
    return BaseFinalConstruct();
}

void EventSource::FinalRelease()
{
    uninit();
    delete m;
    BaseFinalRelease();
}

HRESULT EventSource::init()
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();
    return S_OK;
}

void EventSource::uninit()
{
    {
        // First of all (before even thinking about entering the uninit span):
        // make sure that all listeners are are shut down (no pending events or
        // wait calls), because they cannot be alive without the associated
        // event source. Otherwise API clients which use long-term (or
        // indefinite) waits will block VBoxSVC termination (just one example)
        // for a long time or even infinitely long.
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!m->fShutdown)
        {
            m->fShutdown = true;
            for (Listeners::iterator it = m->mListeners.begin();
                 it != m->mListeners.end();
                 ++it)
            {
                it->second.obj()->shutdown();
            }
        }
    }

    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    m->mListeners.clear();
    // m->mEvMap shall be cleared at this point too by destructors, assert?
}

HRESULT EventSource::registerListener(const ComPtr<IEventListener> &aListener,
                                      const std::vector<VBoxEventType_T> &aInteresting,
                                      BOOL aActive)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->fShutdown)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("This event source is already shut down"));

    Listeners::const_iterator it = m->mListeners.find(aListener);
    if (it != m->mListeners.end())
        return setError(E_INVALIDARG,
                        tr("This listener already registered"));

    com::SafeArray<VBoxEventType_T> interested(aInteresting);
    RecordHolder<ListenerRecord> lrh(new ListenerRecord(aListener, interested, aActive, this));
    m->mListeners.insert(Listeners::value_type((IEventListener *)aListener, lrh));

    ::FireEventSourceChangedEvent(this, (IEventListener *)aListener, TRUE /*add*/);

    return S_OK;
}

HRESULT EventSource::unregisterListener(const ComPtr<IEventListener> &aListener)
{
    HRESULT hrc = S_OK;;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    Listeners::iterator it = m->mListeners.find(aListener);

    if (it != m->mListeners.end())
    {
        it->second.obj()->shutdown();
        m->mListeners.erase(it);
        // destructor removes refs from the event map
        ::FireEventSourceChangedEvent(this, (IEventListener *)aListener, FALSE /*add*/);
        hrc = S_OK;
    }
    else
        hrc = setError(VBOX_E_OBJECT_NOT_FOUND,
                       tr("Listener was never registered"));

    return hrc;
}

HRESULT EventSource::fireEvent(const ComPtr<IEvent> &aEvent,
                               LONG aTimeout,
                               BOOL *aResult)
{
    /* Get event attributes before take the source lock: */
    BOOL fWaitable = FALSE;
    HRESULT hrc = aEvent->COMGETTER(Waitable)(&fWaitable);
    AssertComRC(hrc);

    VBoxEventType_T evType;
    hrc = aEvent->COMGETTER(Type)(&evType);
    AssertComRCReturn(hrc, hrc);

    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (m->fShutdown)
            return setError(VBOX_E_INVALID_OBJECT_STATE,
                            tr("This event source is already shut down"));

        EventMapList &listeners = m->mEvMap[(int)evType - FirstEvent];

        /* Anyone interested in this event? */
        uint32_t cListeners = listeners.size();
        if (cListeners == 0)
        {
            aEvent->SetProcessed();
            // just leave the lock and update event object state
        }
        else
        {
            PendingEventsMap::iterator pit;
            if (fWaitable)
            {
                m->mPendingMap.insert(PendingEventsMap::value_type(aEvent, cListeners));
                // we keep iterator here to allow processing active listeners without
                // pending events lookup
                pit = m->mPendingMap.find(aEvent);
            }

            for (EventMapList::iterator it = listeners.begin();
                 it != listeners.end();
                 ++it)
            {
                // keep listener record reference, in case someone will remove it while in callback
                RecordHolder<ListenerRecord> record(*it);

                /*
                 * We pass lock here to allow modifying ops on EventSource inside callback
                 * in active mode. Note that we expect list iterator stability as 'alock'
                 * could be temporary released when calling event handler.
                 */
                HRESULT cbRc = record.obj()->process(aEvent, fWaitable, pit, alock);

                /* Note that E_ABORT is used above to signal that a passive
                 * listener was unregistered due to not picking up its event.
                 * This overlaps with XPCOM specific use of E_ABORT to signal
                 * death of an active listener, but that's irrelevant here. */
                if (FAILED_DEAD_INTERFACE(cbRc) || cbRc == E_ABORT)
                {
                    Listeners::iterator lit = m->mListeners.find(record.obj()->mListener);
                    if (lit != m->mListeners.end())
                    {
                        lit->second.obj()->shutdown();
                        m->mListeners.erase(lit);
                    }
                }
                // anything else to do with cbRc?
            }
        }
    }
    /* We leave the lock here */

    if (fWaitable)
    {
        hrc = aEvent->WaitProcessed(aTimeout, aResult);

        /*
         * If a passive listener times out without processing a vetoable event then we
         * remove that event from the list of events this listener is interested in.
         */
        if (!*aResult && hrc == E_ABORT && implies(VBoxEventType_Vetoable, evType))
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

            EventMapList &listeners = m->mEvMap[(int)evType - FirstEvent];
            for (EventMapList::iterator it = listeners.begin();
                 it != listeners.end();
                 ++it)
            {
                RecordHolder<ListenerRecord> record(*it);
                if (record.obj()->mQueue.size() != 0 && record.obj()->mQueue.back() == aEvent)
                    m->mEvMap[(int)evType - FirstEvent].remove(record.obj());
            }

            PendingEventsMap::iterator pit = m->mPendingMap.find(aEvent);
            if (pit != m->mPendingMap.end())
                m->mPendingMap.erase(pit);

            /*
             * VBoxEventDesc::fire() requires TRUE to be returned so it can handle
             * vetoable events.
             */
            return S_OK;
        }
    }
    else
        *aResult = TRUE;

    return hrc;
}

HRESULT EventSource::getEvent(const ComPtr<IEventListener> &aListener,
                              LONG aTimeout,
                              ComPtr<IEvent> &aEvent)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->fShutdown)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("This event source is already shut down"));

    Listeners::iterator it = m->mListeners.find(aListener);
    HRESULT hrc = S_OK;

    if (it != m->mListeners.end())
        hrc = it->second.obj()->dequeue(aEvent.asOutParam(), aTimeout, alock);
    else
        hrc = setError(VBOX_E_OBJECT_NOT_FOUND,
                       tr("Listener was never registered"));

    if (hrc == VBOX_E_INVALID_OBJECT_STATE)
        return setError(hrc, tr("Listener must be passive"));

    return hrc;
}

HRESULT EventSource::eventProcessed(const ComPtr<IEventListener> &aListener,
                                    const ComPtr<IEvent> &aEvent)
{
    BOOL fWaitable = FALSE;
    HRESULT hrc = aEvent->COMGETTER(Waitable)(&fWaitable);
    AssertComRC(hrc);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m->fShutdown)
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("This event source is already shut down"));

    Listeners::iterator it = m->mListeners.find(aListener);

    if (it != m->mListeners.end())
    {
        ListenerRecord *aRecord = it->second.obj();

        if (aRecord->isActive())
            return setError(E_INVALIDARG,
                            tr("Only applicable to passive listeners"));

        if (fWaitable)
        {
            PendingEventsMap::iterator pit = m->mPendingMap.find(aEvent);

            if (pit == m->mPendingMap.end())
            {
                AssertFailed();
                hrc = setError(VBOX_E_OBJECT_NOT_FOUND,
                               tr("Unknown event"));
            }
            else
                hrc = aRecord->eventProcessed(aEvent, pit);
        }
        else
        {
            // for non-waitable events we're done
            hrc = S_OK;
        }
    }
    else
        hrc = setError(VBOX_E_OBJECT_NOT_FOUND,
                       tr("Listener was never registered"));

    return hrc;
}

/**
 * This class serves as feasible listener implementation
 * which could be used by clients not able to create local
 * COM objects, but still willing to receive event
 * notifications in passive mode, such as webservices.
 */
class ATL_NO_VTABLE PassiveEventListener :
    public VirtualBoxBase,
    VBOX_SCRIPTABLE_IMPL(IEventListener)
{
public:

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(PassiveEventListener, IEventListener)

    DECLARE_NOT_AGGREGATABLE(PassiveEventListener)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(PassiveEventListener)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IEventListener)
        COM_INTERFACE_ENTRY2(IDispatch, IEventListener)
        VBOX_TWEAK_INTERFACE_ENTRY(IEventListener)
    END_COM_MAP()

    PassiveEventListener()
    {}
    ~PassiveEventListener()
    {}

    HRESULT FinalConstruct()
    {
        return BaseFinalConstruct();
    }
    void FinalRelease()
    {
        BaseFinalRelease();
    }

    // IEventListener methods
    STDMETHOD(HandleEvent)(IEvent *) RT_OVERRIDE
    {
        ComAssertMsgRet(false, (tr("HandleEvent() of wrapper shall never be called")),
                        E_FAIL);
    }
};

/* Proxy listener class, used to aggregate multiple event sources into one */
class ATL_NO_VTABLE ProxyEventListener :
    public VirtualBoxBase,
    VBOX_SCRIPTABLE_IMPL(IEventListener)
{
    ComPtr<IEventSource> mSource;
public:

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(ProxyEventListener, IEventListener)

    DECLARE_NOT_AGGREGATABLE(ProxyEventListener)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(ProxyEventListener)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IEventListener)
        COM_INTERFACE_ENTRY2(IDispatch, IEventListener)
        VBOX_TWEAK_INTERFACE_ENTRY(IEventListener)
    END_COM_MAP()

    ProxyEventListener()
    {}
    ~ProxyEventListener()
    {}

    HRESULT FinalConstruct()
    {
        return BaseFinalConstruct();
    }
    void FinalRelease()
    {
        BaseFinalRelease();
    }

    HRESULT init(IEventSource *aSource)
    {
        mSource = aSource;
        return S_OK;
    }

    // IEventListener methods
    STDMETHOD(HandleEvent)(IEvent *aEvent) RT_OVERRIDE
    {
        BOOL fProcessed = FALSE;
        if (mSource)
            return mSource->FireEvent(aEvent, 0, &fProcessed);
        else
            return S_OK;
    }
};

class ATL_NO_VTABLE EventSourceAggregator :
    public VirtualBoxBase,
    VBOX_SCRIPTABLE_IMPL(IEventSource)
{
    typedef std::list <ComPtr<IEventSource> > EventSourceList;
    /* key is weak reference */
    typedef std::map<IEventListener *, ComPtr<IEventListener> > ProxyListenerMap;

    EventSourceList           mEventSources;
    ProxyListenerMap          mListenerProxies;
    ComObjPtr<EventSource>    mSource;

public:

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(EventSourceAggregator, IEventSource)

    DECLARE_NOT_AGGREGATABLE(EventSourceAggregator)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(EventSourceAggregator)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IEventSource)
        COM_INTERFACE_ENTRY2(IDispatch, IEventSource)
        VBOX_TWEAK_INTERFACE_ENTRY(IEventSource)
    END_COM_MAP()

    EventSourceAggregator()
    {}
    ~EventSourceAggregator()
    {}

    HRESULT FinalConstruct()
    {
        return BaseFinalConstruct();
    }
    void FinalRelease()
    {
        mEventSources.clear();
        mListenerProxies.clear();
        mSource->uninit();
        BaseFinalRelease();
    }

    // internal public
    HRESULT init(const std::vector<ComPtr<IEventSource> >  aSourcesIn);

    // IEventSource methods
    STDMETHOD(CreateListener)(IEventListener **aListener) RT_OVERRIDE;
    STDMETHOD(CreateAggregator)(ComSafeArrayIn(IEventSource *, aSubordinates),
                                IEventSource **aAggregator) RT_OVERRIDE;
    STDMETHOD(RegisterListener)(IEventListener *aListener,
                                ComSafeArrayIn(VBoxEventType_T, aInterested),
                                BOOL aActive) RT_OVERRIDE;
    STDMETHOD(UnregisterListener)(IEventListener *aListener) RT_OVERRIDE;
    STDMETHOD(FireEvent)(IEvent *aEvent,
                         LONG aTimeout,
                         BOOL *aProcessed) RT_OVERRIDE;
    STDMETHOD(GetEvent)(IEventListener *aListener,
                        LONG aTimeout,
                        IEvent **aEvent) RT_OVERRIDE;
    STDMETHOD(EventProcessed)(IEventListener *aListener,
                              IEvent *aEvent) RT_OVERRIDE;

  protected:
    HRESULT createProxyListener(IEventListener *aListener,
                                IEventListener **aProxy);
    HRESULT getProxyListener(IEventListener *aListener,
                             IEventListener **aProxy);
    HRESULT removeProxyListener(IEventListener *aListener);
};

#ifdef VBOX_WITH_XPCOM
NS_DECL_CLASSINFO(ProxyEventListener)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(ProxyEventListener, IEventListener)
NS_DECL_CLASSINFO(PassiveEventListener)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(PassiveEventListener, IEventListener)
NS_DECL_CLASSINFO(EventSourceAggregator)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(EventSourceAggregator, IEventSource)
#endif


HRESULT EventSource::createListener(ComPtr<IEventListener> &aListener)
{
    ComObjPtr<PassiveEventListener> listener;

    HRESULT hrc = listener.createObject();
    ComAssertMsgRet(SUCCEEDED(hrc), (tr("Could not create wrapper object (%Rhrc)"), hrc),
                    E_FAIL);
    listener.queryInterfaceTo(aListener.asOutParam());
    return S_OK;
}

HRESULT EventSource::createAggregator(const std::vector<ComPtr<IEventSource> > &aSubordinates,
                                      ComPtr<IEventSource> &aResult)
{
    ComObjPtr<EventSourceAggregator> agg;

    HRESULT hrc = agg.createObject();
    ComAssertMsgRet(SUCCEEDED(hrc), (tr("Could not create aggregator (%Rhrc)"), hrc),
                    E_FAIL);

    hrc = agg->init(aSubordinates);
    if (FAILED(hrc))
        return hrc;

    agg.queryInterfaceTo(aResult.asOutParam());
    return S_OK;
}

HRESULT EventSourceAggregator::init(const std::vector<ComPtr<IEventSource> >  aSourcesIn)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT hrc = mSource.createObject();
    ComAssertMsgRet(SUCCEEDED(hrc), (tr("Could not create source (%Rhrc)"), hrc),
                    E_FAIL);
    hrc = mSource->init();
    ComAssertMsgRet(SUCCEEDED(hrc), (tr("Could not init source (%Rhrc)"), hrc),
                    E_FAIL);

    for (size_t i = 0; i < aSourcesIn.size(); i++)
    {
        if (aSourcesIn[i] != NULL)
            mEventSources.push_back(aSourcesIn[i]);
    }

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return hrc;
}

STDMETHODIMP EventSourceAggregator::CreateListener(IEventListener **aListener)
{
    return mSource->CreateListener(aListener);
}

STDMETHODIMP EventSourceAggregator::CreateAggregator(ComSafeArrayIn(IEventSource *, aSubordinates),
                                                     IEventSource **aResult)
{
    return mSource->CreateAggregator(ComSafeArrayInArg(aSubordinates), aResult);
}

STDMETHODIMP EventSourceAggregator::RegisterListener(IEventListener *aListener,
                                                     ComSafeArrayIn(VBoxEventType_T, aInterested),
                                                     BOOL aActive)
{
    CheckComArgNotNull(aListener);
    CheckComArgSafeArrayNotNull(aInterested);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    ComPtr<IEventListener> proxy;
    HRESULT hrc = createProxyListener(aListener, proxy.asOutParam());
    if (FAILED(hrc))
        return hrc;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    for (EventSourceList::const_iterator it = mEventSources.begin(); it != mEventSources.end();
         ++it)
    {
        ComPtr<IEventSource> es = *it;
        /* Register active proxy listener on real event source */
        hrc = es->RegisterListener(proxy, ComSafeArrayInArg(aInterested), TRUE);
    }
    /* And add real listener on our event source */
    hrc = mSource->RegisterListener(aListener, ComSafeArrayInArg(aInterested), aActive);

    return S_OK;
}

STDMETHODIMP EventSourceAggregator::UnregisterListener(IEventListener *aListener)
{
    CheckComArgNotNull(aListener);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComPtr<IEventListener> proxy;
    HRESULT hrc = getProxyListener(aListener, proxy.asOutParam());
    if (FAILED(hrc))
        return hrc;

    for (EventSourceList::const_iterator it = mEventSources.begin(); it != mEventSources.end();
         ++it)
    {
        ComPtr<IEventSource> es = *it;
        hrc = es->UnregisterListener(proxy);
    }
    hrc = mSource->UnregisterListener(aListener);

    return removeProxyListener(aListener);

}

STDMETHODIMP EventSourceAggregator::FireEvent(IEvent *aEvent,
                                              LONG aTimeout,
                                              BOOL *aProcessed)
{
    CheckComArgNotNull(aEvent);
    CheckComArgOutPointerValid(aProcessed);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    /* Aggregator event source shall not have direct event firing, but we may
       wish to support aggregation chains */
    for (EventSourceList::const_iterator it = mEventSources.begin(); it != mEventSources.end();
         ++it)
    {
        ComPtr<IEventSource> es = *it;
        HRESULT hrc = es->FireEvent(aEvent, aTimeout, aProcessed);
        /* Current behavior is that aggregator's FireEvent() always succeeds,
           so that multiple event sources don't affect each other. */
        NOREF(hrc);
    }

    return S_OK;
}

STDMETHODIMP EventSourceAggregator::GetEvent(IEventListener *aListener,
                                             LONG aTimeout,
                                             IEvent **aEvent)
{
    return mSource->GetEvent(aListener, aTimeout, aEvent);
}

STDMETHODIMP EventSourceAggregator::EventProcessed(IEventListener *aListener,
                                                   IEvent *aEvent)
{
    return mSource->EventProcessed(aListener, aEvent);
}

HRESULT EventSourceAggregator::createProxyListener(IEventListener *aListener,
                                                   IEventListener **aProxy)
{
    ComObjPtr<ProxyEventListener> proxy;

    HRESULT hrc = proxy.createObject();
    ComAssertMsgRet(SUCCEEDED(hrc), (tr("Could not create proxy (%Rhrc)"), hrc),
                    E_FAIL);

    hrc = proxy->init(mSource);
    if (FAILED(hrc))
        return hrc;

    ProxyListenerMap::const_iterator it = mListenerProxies.find(aListener);
    if (it != mListenerProxies.end())
        return setError(E_INVALIDARG,
                        tr("This listener already registered"));

    mListenerProxies.insert(ProxyListenerMap::value_type(aListener, proxy));

    proxy.queryInterfaceTo(aProxy);
    return S_OK;
}

HRESULT EventSourceAggregator::getProxyListener(IEventListener *aListener,
                                                IEventListener **aProxy)
{
    ProxyListenerMap::const_iterator it = mListenerProxies.find(aListener);
    if (it == mListenerProxies.end())
        return setError(E_INVALIDARG,
                        tr("This listener never registered"));

    (*it).second.queryInterfaceTo(aProxy);
    return S_OK;
}

HRESULT EventSourceAggregator::removeProxyListener(IEventListener *aListener)
{
    ProxyListenerMap::iterator it = mListenerProxies.find(aListener);
    if (it == mListenerProxies.end())
        return setError(E_INVALIDARG,
                        tr("This listener never registered"));

    mListenerProxies.erase(it);
    return S_OK;
}
