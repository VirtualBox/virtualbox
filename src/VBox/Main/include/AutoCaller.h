/* $Id: AutoCaller.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VirtualBox object caller handling definitions
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

#ifndef MAIN_INCLUDED_AutoCaller_h
#define MAIN_INCLUDED_AutoCaller_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ObjectState.h"

#include "VBox/com/AutoLock.h"

// Forward declaration needed, but nothing more.
class VirtualBoxBase;


////////////////////////////////////////////////////////////////////////////////
//
// AutoCaller* classes
//
////////////////////////////////////////////////////////////////////////////////


/**
 * Smart class that automatically increases the number of normal (non-limited)
 * callers of the given VirtualBoxBase object when an instance is constructed
 * and decreases it back when the created instance goes out of scope (i.e. gets
 * destroyed).
 *
 * If #hrc() returns a failure after the instance creation, it means that
 * the managed VirtualBoxBase object is not Ready, or in any other invalid
 * state, so that the caller must not use the object and can return this
 * failed result code to the upper level.
 *
 * See ObjectState::addCaller() and ObjectState::releaseCaller() for more
 * details about object callers.
 *
 * A typical usage pattern to declare a normal method of some object (i.e. a
 * method that is valid only when the object provides its full
 * functionality) is:
 * <code>
 * STDMETHODIMP Component::Foo()
 * {
 *     AutoCaller autoCaller(this);
 *     HRESULT hrc = autoCaller.hrc();
 *     if (SUCCEEDED(hrc))
 *     {
 *         ...
 *     }
 *     return hrc;
 * }
 * </code>
 */
class AutoCaller
{
public:
    /**
     * Default constructor. Not terribly useful, but it's valid to create
     * an instance without associating it with an object. It's a no-op,
     * like the more useful constructor below when NULL is passed to it.
     */
    AutoCaller()
    {
        init(NULL, false);
    }

    /**
     * Increases the number of callers of the given object by calling
     * ObjectState::addCaller() for the corresponding member instance.
     *
     * @param aObj      Object to add a normal caller to. If NULL, this
     *                  instance is effectively turned to no-op (where
     *                  hrc() will return S_OK).
     */
    AutoCaller(VirtualBoxBase *aObj)
    {
        init(aObj, false);
    }

    /**
     * If the number of callers was successfully increased, decreases it
     * using ObjectState::releaseCaller(), otherwise does nothing.
     */
    ~AutoCaller()
    {
        if (mObj && SUCCEEDED(mRC))
            mObj->getObjectState().releaseCaller();
    }

    /**
     * Returns the stored result code returned by ObjectState::addCaller() after
     * instance creation or after the last #add() call.
     *
     * A successful result code means the number of callers was successfully
     * increased.
     */
    HRESULT hrc() const { return mRC; }

    /**
     * Returns |true| if |SUCCEEDED(hrc())| is |true|, for convenience. |true| means
     * the number of callers was successfully increased.
     */
    bool isOk() const { return SUCCEEDED(mRC); }

    /**
     * Returns |true| if |FAILED(hrc())| is |true|, for convenience. |true| means
     * the number of callers was _not_ successfully increased.
     */
    bool isNotOk() const { return FAILED(mRC); }

    /**
     * Temporarily decreases the number of callers of the managed object.
     * May only be called if #isOk() returns |true|. Note that #hrc() will return
     * E_FAIL after this method succeeds.
     */
    void release()
    {
        Assert(SUCCEEDED(mRC));
        if (SUCCEEDED(mRC))
        {
            if (mObj)
                mObj->getObjectState().releaseCaller();
            mRC = E_FAIL;
        }
    }

    /**
     * Restores the number of callers decreased by #release(). May only be
     * called after #release().
     */
    void add()
    {
        Assert(!SUCCEEDED(mRC));
        if (mObj && !SUCCEEDED(mRC))
            mRC = mObj->getObjectState().addCaller(mLimited);
    }

    /**
     * Attaches another object to this caller instance.
     * The previous object's caller is released before the new one is added.
     *
     * @param aObj  New object to attach, may be @c NULL.
     */
    void attach(VirtualBoxBase *aObj)
    {
        /* detect simple self-reattachment */
        if (mObj != aObj)
        {
            if (mObj && SUCCEEDED(mRC))
                release();
            else if (!mObj)
            {
                /* Fix up the success state when nothing is attached. Otherwise
                 * there are a couple of assertion which would trigger. */
                mRC = E_FAIL;
            }
            mObj = aObj;
            add();
        }
    }

    /** Verbose equivalent to <tt>attach(NULL)</tt>. */
    void detach() { attach(NULL); }

protected:
    /**
     * Internal constructor: Increases the number of callers of the given
     * object (either normal or limited variant) by calling
     * ObjectState::addCaller() for the corresponding member instance.
     *
     * @param aObj      Object to add a caller to. If NULL, this
     *                  instance is effectively turned to no-op (where hrc() will
     *                  return S_OK).
     * @param aLimited  If |false|, then it's a regular caller, otherwise a
     *                  limited caller.
     */
    void init(VirtualBoxBase *aObj, bool aLimited)
    {
        mObj = aObj;
        mRC = S_OK;
        mLimited = aLimited;
        if (mObj)
            mRC = mObj->getObjectState().addCaller(mLimited);
    }

private:
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(AutoCaller);
    DECLARE_CLS_NEW_DELETE_NOOP(AutoCaller);

    VirtualBoxBase *mObj;
    HRESULT mRC;
    bool mLimited;
};

/**
 * Smart class that automatically increases the number of limited callers of
 * the given VirtualBoxBase object when an instance is constructed and
 * decreases it back when the created instance goes out of scope (i.e. gets
 * destroyed).
 *
 * A typical usage pattern to declare a limited method of some object (i.e.
 * a method that is valid even if the object doesn't provide its full
 * functionality) is:
 * <code>
 * STDMETHODIMP Component::Bar()
 * {
 *     AutoLimitedCaller autoCaller(this);
 *     HRESULT hrc = autoCaller.hrc();
 *     if (SUCCEEDED(hrc))
 *     {
 *         ...
 *     }
 *     return hrc;
 * </code>
 *
 * See AutoCaller for more information about auto caller functionality.
 */
class AutoLimitedCaller : public AutoCaller
{
public:
    /**
     * Default constructor. Not terribly useful, but it's valid to create
     * an instance without associating it with an object. It's a no-op,
     * like the more useful constructor below when NULL is passed to it.
     */
    AutoLimitedCaller()
    {
        AutoCaller::init(NULL, true);
    }

    /**
     * Increases the number of callers of the given object by calling
     * ObjectState::addCaller() for the corresponding member instance.
     *
     * @param aObj      Object to add a limited caller to. If NULL, this
     *                  instance is effectively turned to no-op (where hrc() will
     *                  return S_OK).
     */
    AutoLimitedCaller(VirtualBoxBase *aObj)
    {
        AutoCaller::init(aObj, true);
    }

private:
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(AutoLimitedCaller); /* Shuts up MSC warning C4625. */
};

/**
 * Smart class to enclose the state transition NotReady->InInit->Ready.
 *
 * The purpose of this span is to protect object initialization.
 *
 * Instances must be created as a stack-based variable taking |this| pointer
 * as the argument at the beginning of init() methods of VirtualBoxBase
 * subclasses. When this variable is created it automatically places the
 * object to the InInit state.
 *
 * When the created variable goes out of scope (i.e. gets destroyed) then,
 * depending on the result status of this initialization span, it either
 * places the object to Ready or Limited state or calls the object's
 * VirtualBoxBase::uninit() method which is supposed to place the object
 * back to the NotReady state using the AutoUninitSpan class.
 *
 * The initial result status of the initialization span is determined by the
 * @a aResult argument of the AutoInitSpan constructor (Result::Failed by
 * default). Inside the initialization span, the success status can be set
 * to Result::Succeeded using #setSucceeded(), to to Result::Limited using
 * #setLimited() or to Result::Failed using #setFailed(). Please don't
 * forget to set the correct success status before getting the AutoInitSpan
 * variable destroyed (for example, by performing an early return from
 * the init() method)!
 *
 * Note that if an instance of this class gets constructed when the object
 * is in the state other than NotReady, #isOk() returns |false| and methods
 * of this class do nothing: the state transition is not performed.
 *
 * A typical usage pattern is:
 * <code>
 * HRESULT Component::init()
 * {
 *     AutoInitSpan autoInitSpan(this);
 *     AssertReturn(autoInitSpan.isOk(), E_FAIL);
 *     ...
 *     if (FAILED(rc))
 *         return rc;
 *     ...
 *     if (SUCCEEDED(rc))
 *         autoInitSpan.setSucceeded();
 *     return rc;
 * }
 * </code>
 *
 * @note Never create instances of this class outside init() methods of
 *       VirtualBoxBase subclasses and never pass anything other than |this|
 *       as the argument to the constructor!
 */
class AutoInitSpan
{
public:

    enum Result { Failed = 0x0, Succeeded = 0x1, Limited = 0x2 };

    AutoInitSpan(VirtualBoxBase *aObj, Result aResult = Failed);
    ~AutoInitSpan();

    /**
     * Returns |true| if this instance has been created at the right moment
     * (when the object was in the NotReady state) and |false| otherwise.
     */
    bool isOk() const { return mOk; }

    /**
     * Sets the initialization status to Succeeded to indicates successful
     * initialization. The AutoInitSpan destructor will place the managed
     * VirtualBoxBase object to the Ready state.
     */
    void setSucceeded() { mResult = Succeeded; }

    /**
     * Sets the initialization status to Succeeded to indicate limited
     * (partly successful) initialization. The AutoInitSpan destructor will
     * place the managed VirtualBoxBase object to the Limited state.
     */
    void setLimited() { mResult = Limited; }

    /**
     * Sets the initialization status to Succeeded to indicate limited
     * (partly successful) initialization but also adds the initialization
     * error if required for further reporting. The AutoInitSpan destructor
     * will place the managed VirtualBoxBase object to the Limited state.
     */
    void setLimited(HRESULT rc)
    {
        mResult = Limited;
        mFailedRC = rc;
        mpFailedEI = new ErrorInfo();
    }

    /**
     * Sets the initialization status to Failure to indicates failed
     * initialization. The AutoInitSpan destructor will place the managed
     * VirtualBoxBase object to the InitFailed state and will automatically
     * call its uninit() method which is supposed to place the object back
     * to the NotReady state using AutoUninitSpan.
     */
    void setFailed(HRESULT rc = E_ACCESSDENIED)
    {
        mResult = Failed;
        mFailedRC = rc;
        mpFailedEI = new ErrorInfo();
    }

    /** Returns the current initialization result. */
    Result result() { return mResult; }

private:

    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(AutoInitSpan);
    DECLARE_CLS_NEW_DELETE_NOOP(AutoInitSpan);

    VirtualBoxBase *mObj;
    Result mResult : 3; // must be at least total number of bits + 1 (sign)
    bool mOk : 1;
    HRESULT mFailedRC;
    ErrorInfo *mpFailedEI;
};

/**
 * Smart class to enclose the state transition Limited->InInit->Ready.
 *
 * The purpose of this span is to protect object re-initialization.
 *
 * Instances must be created as a stack-based variable taking |this| pointer
 * as the argument at the beginning of methods of VirtualBoxBase
 * subclasses that try to re-initialize the object to bring it to the Ready
 * state (full functionality) after partial initialization (limited
 * functionality). When this variable is created, it automatically places
 * the object to the InInit state.
 *
 * When the created variable goes out of scope (i.e. gets destroyed),
 * depending on the success status of this initialization span, it either
 * places the object to the Ready state or brings it back to the Limited
 * state.
 *
 * The initial success status of the re-initialization span is |false|. In
 * order to make it successful, #setSucceeded() must be called before the
 * instance is destroyed.
 *
 * Note that if an instance of this class gets constructed when the object
 * is in the state other than Limited, #isOk() returns |false| and methods
 * of this class do nothing: the state transition is not performed.
 *
 * A typical usage pattern is:
 * <code>
 * HRESULT Component::reinit()
 * {
 *     AutoReinitSpan autoReinitSpan(this);
 *     AssertReturn(autoReinitSpan.isOk(), E_FAIL);
 *     ...
 *     if (FAILED(rc))
 *         return rc;
 *     ...
 *     if (SUCCEEDED(rc))
 *         autoReinitSpan.setSucceeded();
 *     return rc;
 * }
 * </code>
 *
 * @note Never create instances of this class outside re-initialization
 * methods of VirtualBoxBase subclasses and never pass anything other than
 * |this| as the argument to the constructor!
 */
class AutoReinitSpan
{
public:

    AutoReinitSpan(VirtualBoxBase *aObj);
    ~AutoReinitSpan();

    /**
     * Returns |true| if this instance has been created at the right moment
     * (when the object was in the Limited state) and |false| otherwise.
     */
    bool isOk() const { return mOk; }

    /**
     * Sets the re-initialization status to Succeeded to indicates
     * successful re-initialization. The AutoReinitSpan destructor will place
     * the managed VirtualBoxBase object to the Ready state.
     */
    void setSucceeded() { mSucceeded = true; }

private:

    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(AutoReinitSpan);
    DECLARE_CLS_NEW_DELETE_NOOP(AutoReinitSpan);

    VirtualBoxBase *mObj;
    bool mSucceeded : 1;
    bool mOk : 1;
};

/**
 * Smart class to enclose the state transition Ready->InUninit->NotReady,
 * InitFailed->InUninit->NotReady.
 *
 * The purpose of this span is to protect object uninitialization.
 *
 * Instances must be created as a stack-based variable taking |this| pointer
 * as the argument at the beginning of uninit() methods of VirtualBoxBase
 * subclasses. When this variable is created it automatically places the
 * object to the InUninit state, unless it is already in the NotReady state
 * as indicated by #uninitDone() returning |true|. In the latter case, the
 * uninit() method must immediately return because there should be nothing
 * to uninitialize.
 *
 * When this variable goes out of scope (i.e. gets destroyed), it places the
 * object to NotReady state.
 *
 * A typical usage pattern is:
 * <code>
 * void Component::uninit()
 * {
 *     AutoUninitSpan autoUninitSpan(this);
 *     if (autoUninitSpan.uninitDone())
 *         return;
 *     ...
 * }
 * </code>
 *
 * @note The constructor of this class blocks the current thread execution
 *       until the number of callers added to the object using
 *       ObjectState::addCaller() or AutoCaller drops to zero. For this reason,
 *       it is forbidden to create instances of this class (or call uninit())
 *       within the AutoCaller or ObjectState::addCaller() scope because it is
 *       a guaranteed deadlock.
 *
 * @note Never create instances of this class outside uninit() methods and
 *       never pass anything other than |this| as the argument to the
 *       constructor!
 */
class AutoUninitSpan
{
public:

    AutoUninitSpan(VirtualBoxBase *aObj, bool fTry = false);
    ~AutoUninitSpan();

    /** |true| when uninit() is called as a result of init() failure */
    bool initFailed() { return mInitFailed; }

    /** |true| when uninit() has already been called (so the object is NotReady) */
    bool uninitDone() { return mUninitDone; }

    /** |true| when uninit() has failed, relevant only if it was a "try uninit" */
    bool uninitFailed() { return mUninitFailed; }

    void setSucceeded();

private:

    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(AutoUninitSpan);
    DECLARE_CLS_NEW_DELETE_NOOP(AutoUninitSpan);

    VirtualBoxBase *mObj;
    bool mInitFailed : 1;
    bool mUninitDone : 1;
    bool mUninitFailed : 1;
};

#endif /* !MAIN_INCLUDED_AutoCaller_h */
