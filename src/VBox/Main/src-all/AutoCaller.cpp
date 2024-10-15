/* $Id: AutoCaller.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox object state implementation
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

#define LOG_GROUP LOG_GROUP_MAIN
#include <iprt/semaphore.h>

#include "VirtualBoxBase.h"
#include "AutoCaller.h"
#include "LoggingNew.h"

#include "VBoxNls.h"


DECLARE_TRANSLATION_CONTEXT(AutoCallerCtx);

////////////////////////////////////////////////////////////////////////////////
//
// ObjectState methods
//
////////////////////////////////////////////////////////////////////////////////


ObjectState::ObjectState() : mStateLock(LOCKCLASS_OBJECTSTATE)
{
    AssertFailed();
}

ObjectState::ObjectState(VirtualBoxBase *aObj) :
    mObj(aObj), mStateLock(LOCKCLASS_OBJECTSTATE)
{
    Assert(mObj);
    mState = NotReady;
    mStateChangeThread = NIL_RTTHREAD;
    mCallers = 0;
    mFailedRC = S_OK;
    mpFailedEI = NULL;
    mZeroCallersSem = NIL_RTSEMEVENT;
    mInitUninitSem = NIL_RTSEMEVENTMULTI;
    mInitUninitWaiters = 0;
}

ObjectState::~ObjectState()
{
    Assert(mInitUninitWaiters == 0);
    Assert(mInitUninitSem == NIL_RTSEMEVENTMULTI);
    if (mZeroCallersSem != NIL_RTSEMEVENT)
        RTSemEventDestroy(mZeroCallersSem);
    mCallers = 0;
    mStateChangeThread = NIL_RTTHREAD;
    mState = NotReady;
    mFailedRC = S_OK;
    if (mpFailedEI)
    {
        delete mpFailedEI;
        mpFailedEI = NULL;
    }
    mObj = NULL;
}

ObjectState::State ObjectState::getState()
{
    AutoReadLock stateLock(mStateLock COMMA_LOCKVAL_SRC_POS);
    return mState;
}

/**
 * Increments the number of calls to this object by one.
 *
 * After this method succeeds, it is guaranteed that the object will remain
 * in the Ready (or in the Limited) state at least until #releaseCaller() is
 * called.
 *
 * This method is intended to mark the beginning of sections of code within
 * methods of COM objects that depend on the readiness (Ready) state. The
 * Ready state is a primary "ready to serve" state. Usually all code that
 * works with component's data depends on it. On practice, this means that
 * almost every public method, setter or getter of the object should add
 * itself as an object's caller at the very beginning, to protect from an
 * unexpected uninitialization that may happen on a different thread.
 *
 * Besides the Ready state denoting that the object is fully functional,
 * there is a special Limited state. The Limited state means that the object
 * is still functional, but its functionality is limited to some degree, so
 * not all operations are possible. The @a aLimited argument to this method
 * determines whether the caller represents this limited functionality or
 * not.
 *
 * This method succeeds (and increments the number of callers) only if the
 * current object's state is Ready. Otherwise, it will return E_ACCESSDENIED
 * to indicate that the object is not operational. There are two exceptions
 * from this rule:
 * <ol>
 *   <li>If the @a aLimited argument is |true|, then this method will also
 *       succeed if the object's state is Limited (or Ready, of course).
 *   </li>
 *   <li>If this method is called from the same thread that placed
 *       the object to InInit or InUninit state (i.e. either from within the
 *       AutoInitSpan or AutoUninitSpan scope), it will succeed as well (but
 *       will not increase the number of callers).
 *   </li>
 * </ol>
 *
 * Normally, calling addCaller() never blocks. However, if this method is
 * called by a thread created from within the AutoInitSpan scope and this
 * scope is still active (i.e. the object state is InInit), it will block
 * until the AutoInitSpan destructor signals that it has finished
 * initialization.
 *
 * When this method returns a failure, the caller must not use the object
 * and should return the failed result code to its own caller.
 *
 * @param aLimited      |true| to add a limited caller.
 *
 * @return              S_OK on success or E_ACCESSDENIED on failure.
 *
 * @sa #releaseCaller()
 */
HRESULT ObjectState::addCaller(bool aLimited /* = false */)
{
    AutoWriteLock stateLock(mStateLock COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = E_ACCESSDENIED;

    if (mState == Ready || (aLimited && mState == Limited))
    {
        /* if Ready or allows Limited, increase the number of callers */
        ++mCallers;
        hrc = S_OK;
    }
    else
    if (mState == InInit || mState == InUninit)
    {
        if (mStateChangeThread == RTThreadSelf())
        {
            /* Called from the same thread that is doing AutoInitSpan or
             * AutoUninitSpan, just succeed */
            hrc = S_OK;
        }
        else if (mState == InInit)
        {
            /* addCaller() is called by a "child" thread while the "parent"
             * thread is still doing AutoInitSpan/AutoReinitSpan, so wait for
             * the state to become either Ready/Limited or InitFailed (in
             * case of init failure).
             *
             * Note that we increase the number of callers anyway -- to
             * prevent AutoUninitSpan from early completion if we are
             * still not scheduled to pick up the posted semaphore when
             * uninit() is called.
             */
            ++mCallers;

            /* lazy semaphore creation */
            if (mInitUninitSem == NIL_RTSEMEVENTMULTI)
            {
                RTSemEventMultiCreate(&mInitUninitSem);
                Assert(mInitUninitWaiters == 0);
            }

            ++mInitUninitWaiters;

            LogFlowThisFunc(("Waiting for AutoInitSpan/AutoReinitSpan to finish...\n"));

            stateLock.release();
            RTSemEventMultiWait(mInitUninitSem, RT_INDEFINITE_WAIT);
            stateLock.acquire();

            if (--mInitUninitWaiters == 0)
            {
                /* destroy the semaphore since no more necessary */
                RTSemEventMultiDestroy(mInitUninitSem);
                mInitUninitSem = NIL_RTSEMEVENTMULTI;
            }

            if (mState == Ready || (aLimited && mState == Limited))
                hrc = S_OK;
            else
            {
                Assert(mCallers != 0);
                --mCallers;
                if (mCallers == 0 && mState == InUninit)
                {
                    /* inform AutoUninitSpan ctor there are no more callers */
                    RTSemEventSignal(mZeroCallersSem);
                }
            }
        }
    }

    if (FAILED(hrc))
    {
        if (mState == Limited)
            hrc = mObj->setError(hrc, AutoCallerCtx::tr("The object functionality is limited"));
        else if (FAILED(mFailedRC) && mFailedRC != E_ACCESSDENIED)
        {
            /* replay recorded error information */
            if (mpFailedEI)
                ErrorInfoKeeper eik(*mpFailedEI);
            hrc = mFailedRC;
        }
        else
            hrc = mObj->setError(hrc, AutoCallerCtx::tr("The object is not ready"));
    }

    return hrc;
}

/**
 * Decreases the number of calls to this object by one.
 *
 * Must be called after every #addCaller() when protecting the object
 * from uninitialization is no more necessary.
 */
void ObjectState::releaseCaller()
{
    AutoWriteLock stateLock(mStateLock COMMA_LOCKVAL_SRC_POS);

    if (mState == Ready || mState == Limited)
    {
        /* if Ready or Limited, decrease the number of callers */
        AssertMsgReturn(mCallers != 0, ("mCallers is ZERO!"), (void) 0);
        --mCallers;

        return;
    }

    if (mState == InInit || mState == InUninit)
    {
        if (mStateChangeThread == RTThreadSelf())
        {
            /* Called from the same thread that is doing AutoInitSpan or
             * AutoUninitSpan: just succeed */
            return;
        }

        if (mState == InUninit)
        {
            /* the caller is being released after AutoUninitSpan has begun */
            AssertMsgReturn(mCallers != 0, ("mCallers is ZERO!"), (void) 0);
            --mCallers;

            if (mCallers == 0)
                /* inform the Auto*UninitSpan ctor there are no more callers */
                RTSemEventSignal(mZeroCallersSem);

            return;
        }
    }

    AssertMsgFailed(("mState = %d!", mState));
}

bool ObjectState::autoInitSpanConstructor(ObjectState::State aExpectedState)
{
    AutoWriteLock stateLock(mStateLock COMMA_LOCKVAL_SRC_POS);

    mFailedRC = S_OK;
    if (mpFailedEI)
    {
        delete mpFailedEI;
        mpFailedEI = NULL;
    }

    if (mState == aExpectedState)
    {
        setState(InInit);
        return true;
    }
    else
        return false;
}

void ObjectState::autoInitSpanDestructor(State aNewState, HRESULT aFailedRC, com::ErrorInfo *apFailedEI)
{
    AutoWriteLock stateLock(mStateLock COMMA_LOCKVAL_SRC_POS);

    Assert(mState == InInit);

    if (mCallers > 0 && mInitUninitWaiters > 0)
    {
        /* We have some pending addCaller() calls on other threads (created
         * during InInit), signal that InInit is finished and they may go on. */
        RTSemEventMultiSignal(mInitUninitSem);
    }

    if (aNewState == InitFailed || aNewState == Limited)
    {
        mFailedRC = aFailedRC;
        /* apFailedEI may be NULL, when there is no explicit setFailed() or
         * setLimited() call, which also implies that aFailedRC is S_OK.
         * This case is used by objects (the majority) which don't want
         * delayed error signalling. */
        mpFailedEI = apFailedEI;
    }
    else
    {
        Assert(SUCCEEDED(aFailedRC));
        Assert(apFailedEI == NULL);
        Assert(mpFailedEI == NULL);
    }

    setState(aNewState);
}

ObjectState::State ObjectState::autoUninitSpanConstructor(bool fTry)
{
    AutoWriteLock stateLock(mStateLock COMMA_LOCKVAL_SRC_POS);

    Assert(mState != InInit);

    if (mState == NotReady)
    {
        /* do nothing if already uninitialized */
        return mState;
    }
    else if (mState == InUninit)
    {
        /* Another thread has already started uninitialization, wait for its
         * completion. This is necessary to make sure that when this method
         * returns, the object state is well-defined (NotReady). */

        if (fTry)
            return Ready;

        /* lazy semaphore creation */
        if (mInitUninitSem == NIL_RTSEMEVENTMULTI)
        {
            RTSemEventMultiCreate(&mInitUninitSem);
            Assert(mInitUninitWaiters == 0);
        }
        ++mInitUninitWaiters;

        LogFlowFunc(("{%p}: Waiting for AutoUninitSpan to finish...\n", mObj));

        stateLock.release();
        RTSemEventMultiWait(mInitUninitSem, RT_INDEFINITE_WAIT);
        stateLock.acquire();

        if (--mInitUninitWaiters == 0)
        {
            /* destroy the semaphore since no more necessary */
            RTSemEventMultiDestroy(mInitUninitSem);
            mInitUninitSem = NIL_RTSEMEVENTMULTI;
        }

        /* the other thread set it to NotReady */
        return mState;
    }

    /* go to InUninit to prevent from adding new callers */
    setState(InUninit);

    /* wait for already existing callers to drop to zero */
    if (mCallers > 0)
    {
        if (fTry)
            return Ready;

        /* lazy creation */
        Assert(mZeroCallersSem == NIL_RTSEMEVENT);
        RTSemEventCreate(&mZeroCallersSem);

        /* wait until remaining callers release the object */
        LogFlowFunc(("{%p}: Waiting for callers (%d) to drop to zero...\n",
                     mObj, mCallers));

        stateLock.release();
        RTSemEventWait(mZeroCallersSem, RT_INDEFINITE_WAIT);
    }
    return mState;
}

void ObjectState::autoUninitSpanDestructor()
{
    AutoWriteLock stateLock(mStateLock COMMA_LOCKVAL_SRC_POS);

    Assert(mState == InUninit);

    if (mInitUninitWaiters > 0)
    {
        /* We have some concurrent uninit() calls on other threads (created
         * during InUninit), signal that InUninit is finished and they may go on. */
        RTSemEventMultiSignal(mInitUninitSem);
    }

    setState(NotReady);
}


void ObjectState::setState(ObjectState::State aState)
{
    Assert(mState != aState);
    mState = aState;
    mStateChangeThread = RTThreadSelf();
}


////////////////////////////////////////////////////////////////////////////////
//
// AutoInitSpan methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Creates a smart initialization span object that places the object to
 * InInit state.
 *
 * Please see the AutoInitSpan class description for more info.
 *
 * @param aObj      |this| pointer of the managed VirtualBoxBase object whose
 *                  init() method is being called.
 * @param aResult   Default initialization result.
 */
AutoInitSpan::AutoInitSpan(VirtualBoxBase *aObj,
                           Result aResult /* = Failed */)
    : mObj(aObj),
      mResult(aResult),
      mOk(false),
      mFailedRC(S_OK),
      mpFailedEI(NULL)
{
    Assert(mObj);
    mOk = mObj->getObjectState().autoInitSpanConstructor(ObjectState::NotReady);
    AssertReturnVoid(mOk);
}

/**
 * Places the managed VirtualBoxBase object to Ready/Limited state if the
 * initialization succeeded or partly succeeded, or places it to InitFailed
 * state and calls the object's uninit() method.
 *
 * Please see the AutoInitSpan class description for more info.
 */
AutoInitSpan::~AutoInitSpan()
{
    /* if the state was other than NotReady, do nothing */
    if (!mOk)
    {
        Assert(SUCCEEDED(mFailedRC));
        Assert(mpFailedEI == NULL);
        return;
    }

    ObjectState::State newState;
    if (mResult == Succeeded)
        newState = ObjectState::Ready;
    else if (mResult == Limited)
        newState = ObjectState::Limited;
    else
        newState = ObjectState::InitFailed;
    mObj->getObjectState().autoInitSpanDestructor(newState, mFailedRC, mpFailedEI);
    mFailedRC = S_OK;
    mpFailedEI = NULL; /* now owned by ObjectState instance */
    if (newState == ObjectState::InitFailed)
    {
        /* call uninit() to let the object uninit itself after failed init() */
        mObj->uninit();
    }
}

// AutoReinitSpan methods
////////////////////////////////////////////////////////////////////////////////

/**
 * Creates a smart re-initialization span object and places the object to
 * InInit state.
 *
 * Please see the AutoInitSpan class description for more info.
 *
 * @param aObj      |this| pointer of the managed VirtualBoxBase object whose
 *                  re-initialization method is being called.
 */
AutoReinitSpan::AutoReinitSpan(VirtualBoxBase *aObj)
    : mObj(aObj),
      mSucceeded(false),
      mOk(false)
{
    Assert(mObj);
    mOk = mObj->getObjectState().autoInitSpanConstructor(ObjectState::Limited);
    AssertReturnVoid(mOk);
}

/**
 * Places the managed VirtualBoxBase object to Ready state if the
 * re-initialization succeeded (i.e. #setSucceeded() has been called) or back to
 * Limited state otherwise.
 *
 * Please see the AutoInitSpan class description for more info.
 */
AutoReinitSpan::~AutoReinitSpan()
{
    /* if the state was other than Limited, do nothing */
    if (!mOk)
        return;

    ObjectState::State newState;
    if (mSucceeded)
        newState = ObjectState::Ready;
    else
        newState = ObjectState::Limited;
    mObj->getObjectState().autoInitSpanDestructor(newState, S_OK, NULL);
    /* If later AutoReinitSpan can truly fail (today there is no way) then
     * in this place there needs to be an mObj->uninit() call just like in
     * the AutoInitSpan destructor. In that case it might make sense to
     * let AutoReinitSpan inherit from AutoInitSpan, as the code can be
     * made (almost) identical. */
}

// AutoUninitSpan methods
////////////////////////////////////////////////////////////////////////////////

/**
 * Creates a smart uninitialization span object and places this object to
 * InUninit state.
 *
 * Please see the AutoInitSpan class description for more info.
 *
 * @note This method blocks the current thread execution until the number of
 *       callers of the managed VirtualBoxBase object drops to zero!
 *
 * @param aObj  |this| pointer of the VirtualBoxBase object whose uninit()
 *              method is being called.
 * @param fTry  @c true if the wait for other callers should be skipped,
 *              requiring checking if the uninit span is actually operational.
 */
AutoUninitSpan::AutoUninitSpan(VirtualBoxBase *aObj, bool fTry /* = false */)
    : mObj(aObj),
      mInitFailed(false),
      mUninitDone(false),
      mUninitFailed(false)
{
    Assert(mObj);
    ObjectState::State state;
    state = mObj->getObjectState().autoUninitSpanConstructor(fTry);
    if (state == ObjectState::InitFailed)
        mInitFailed = true;
    else if (state == ObjectState::NotReady)
        mUninitDone = true;
    else if (state == ObjectState::Ready)
        mUninitFailed = true;
}

/**
 *  Places the managed VirtualBoxBase object to the NotReady state.
 */
AutoUninitSpan::~AutoUninitSpan()
{
    /* do nothing if already uninitialized */
    if (mUninitDone || mUninitFailed)
        return;

    mObj->getObjectState().autoUninitSpanDestructor();
}

/**
 * Marks the uninitializion as succeeded.
 *
 * Same as the destructor, and makes the destructor do nothing.
 */
void AutoUninitSpan::setSucceeded()
{
    /* do nothing if already uninitialized */
    if (mUninitDone || mUninitFailed)
        return;

    mObj->getObjectState().autoUninitSpanDestructor();
    mUninitDone = true;
}
