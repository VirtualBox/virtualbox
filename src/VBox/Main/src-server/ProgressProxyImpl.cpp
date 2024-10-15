/* $Id: ProgressProxyImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IProgress implementation for Machine::openRemoteSession in VBoxSVC.
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

#define LOG_GROUP LOG_GROUP_MAIN_PROGRESS
#include <iprt/types.h>

#include "ProgressProxyImpl.h"

#include "VirtualBoxImpl.h"
#include "VirtualBoxErrorInfoImpl.h"

#include "LoggingNew.h"

#include <iprt/time.h>
#include <iprt/semaphore.h>
#include <iprt/errcore.h>

////////////////////////////////////////////////////////////////////////////////
// ProgressProxy class
////////////////////////////////////////////////////////////////////////////////

// constructor / destructor / uninitializer
////////////////////////////////////////////////////////////////////////////////


HRESULT ProgressProxy::FinalConstruct()
{
    mfMultiOperation = false;
    muOtherProgressStartWeight = 0;
    muOtherProgressWeight = 0;
    muOtherProgressStartOperation = 0;

    return Progress::FinalConstruct();
}

/**
 * Initialize it as a one operation Progress object.
 *
 * This is used by SessionMachine::OnSessionEnd.
 */
HRESULT ProgressProxy::init(
#if !defined (VBOX_COM_INPROC)
                            VirtualBox *pParent,
#endif
                            IUnknown *pInitiator,
                            Utf8Str strDescription,
                            BOOL fCancelable)
{
    mfMultiOperation = false;
    muOtherProgressStartWeight = 1;
    muOtherProgressWeight = 1;
    muOtherProgressStartOperation = 1;

    return Progress::init(
#if !defined (VBOX_COM_INPROC)
                          pParent,
#endif
                          pInitiator,
                          strDescription,
                          fCancelable,
                          1 /* cOperations */,
                          1 /* ulTotalOperationsWeight */,
                          strDescription /* strFirstOperationDescription */,
                          1 /* ulFirstOperationWeight */);
}

/**
 * Initialize for proxying one other progress object.
 *
 * This is tailored explicitly for the openRemoteSession code, so we start out
 * with one operation where we don't have any remote object (powerUp).  Then a
 * remote object is added and stays with us till the end.
 *
 * The user must do normal completion notification or risk leave the threads
 * waiting forever!
 */
HRESULT ProgressProxy::init(
#if !defined (VBOX_COM_INPROC)
                            VirtualBox *pParent,
#endif
                            IUnknown *pInitiator,
                            Utf8Str strDescription,
                            BOOL fCancelable,
                            ULONG uTotalOperationsWeight,
                            Utf8Str strFirstOperationDescription,
                            ULONG uFirstOperationWeight,
                            ULONG cOtherProgressObjectOperations)
{
    mfMultiOperation = false;
    muOtherProgressStartWeight    = uFirstOperationWeight;
    muOtherProgressWeight         = uTotalOperationsWeight - uFirstOperationWeight;
    muOtherProgressStartOperation = 1;

    return Progress::init(
#if !defined (VBOX_COM_INPROC)
                          pParent,
#endif
                          pInitiator,
                          strDescription,
                          fCancelable,
                          1 + cOtherProgressObjectOperations /* cOperations */,
                          uTotalOperationsWeight,
                          strFirstOperationDescription,
                          uFirstOperationWeight);
}

void ProgressProxy::FinalRelease()
{
    uninit();
    mfMultiOperation = false;
    muOtherProgressStartWeight    = 0;
    muOtherProgressWeight         = 0;
    muOtherProgressStartOperation = 0;

    BaseFinalRelease();
}

void ProgressProxy::uninit()
{
    LogFlowThisFunc(("\n"));

    mptrOtherProgress.setNull();
    Progress::uninit();
}

// Public methods
////////////////////////////////////////////////////////////////////////////////

/** Just a wrapper so we can automatically do the handover before setting
 *  the result locally. */
HRESULT ProgressProxy::notifyComplete(HRESULT aResultCode)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    clearOtherProgressObjectInternal(true /* fEarly */);
    HRESULT hrc = S_OK;
    if (!mCompleted)
         hrc = Progress::i_notifyComplete(aResultCode);
    return hrc;
}

/** Just a wrapper so we can automatically do the handover before setting
 *  the result locally. */
HRESULT ProgressProxy::notifyComplete(HRESULT aResultCode,
                                      const GUID &aIID,
                                      const char *pcszComponent,
                                      const char *aText,
                                      ...)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    clearOtherProgressObjectInternal(true /* fEarly */);

    HRESULT hrc = S_OK;
    if (!mCompleted)
    {
        va_list va;
        va_start(va, aText);
        hrc = Progress::i_notifyCompleteV(aResultCode, aIID, pcszComponent, aText, va);
        va_end(va);
    }
    return hrc;
}

/**
 * Sets the other progress object unless the operation has been completed /
 * canceled already.
 *
 * @returns false if failed/canceled, true if not.
 * @param   pOtherProgress      The other progress object. Must not be NULL.
 */
bool ProgressProxy::setOtherProgressObject(IProgress *pOtherProgress)
{
    LogFlowThisFunc(("setOtherProgressObject: %p\n", pOtherProgress));
    ComPtr<IProgress> ptrOtherProgress = pOtherProgress;

    /*
     * Query information from the other progress object before we grab the
     * lock.
     */
    ULONG cOperations;
    HRESULT hrc = pOtherProgress->COMGETTER(OperationCount)(&cOperations);
    if (FAILED(hrc))
        cOperations = 1;

    Bstr bstrOperationDescription;
    hrc = pOtherProgress->COMGETTER(Description)(bstrOperationDescription.asOutParam());
    if (FAILED(hrc))
        bstrOperationDescription = "oops";


    /*
     * Take the lock and check for cancelation, cancel the other object if
     * we've been canceled already.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    BOOL fCompletedOrCanceled = mCompleted || mCanceled;
    if (!fCompletedOrCanceled)
    {
        /*
         * Advance to the next object and operation. If the other object has
         * more operations than anticipated, adjust our internal count.
         */
        mptrOtherProgress = ptrOtherProgress;
        mfMultiOperation  = cOperations > 1;

        muOtherProgressStartWeight = m_ulOperationsCompletedWeight + m_ulCurrentOperationWeight;
        muOtherProgressWeight      = m_ulTotalOperationsWeight - muOtherProgressStartWeight;
        Progress::SetNextOperation(bstrOperationDescription.raw(), muOtherProgressWeight);

        muOtherProgressStartOperation = m_ulCurrentOperation;
        m_cOperations = cOperations + m_ulCurrentOperation;

        /*
         * Check for cancelation and completion.
         */
        BOOL f;
        hrc = ptrOtherProgress->COMGETTER(Completed)(&f);
        fCompletedOrCanceled = FAILED(hrc) || f;

        if (!fCompletedOrCanceled)
        {
            hrc = ptrOtherProgress->COMGETTER(Canceled)(&f);
            fCompletedOrCanceled = SUCCEEDED(hrc) && f;
        }

        if (fCompletedOrCanceled)
        {
            LogFlowThisFunc(("Other object completed or canceled, clearing...\n"));
            clearOtherProgressObjectInternal(false /*fEarly*/);
        }
        else
        {
            /*
             * Finally, mirror the cancelable property.
             * Note! Note necessary if we do passthru!
             */
            if (mCancelable)
            {
                hrc = ptrOtherProgress->COMGETTER(Cancelable)(&f);
                if (SUCCEEDED(hrc) && !f)
                {
                    LogFlowThisFunc(("The other progress object is not cancelable\n"));
                    mCancelable = FALSE;
                }
            }
        }
    }
    else
    {
        LogFlowThisFunc(("mCompleted=%RTbool mCanceled=%RTbool - Canceling the other progress object!\n",
                         mCompleted, mCanceled));
        hrc = ptrOtherProgress->Cancel();
        LogFlowThisFunc(("Cancel -> %Rhrc", hrc));
    }

    LogFlowThisFunc(("Returns %RTbool\n", !fCompletedOrCanceled));
    return !fCompletedOrCanceled;
}

// Internal methods.
////////////////////////////////////////////////////////////////////////////////


/**
 * Clear the other progress object reference, first copying over its state.
 *
 * This is used internally when completion is signalled one way or another.
 *
 * @param   fEarly          Early clearing or not.
 */
void ProgressProxy::clearOtherProgressObjectInternal(bool fEarly)
{
    if (!mptrOtherProgress.isNull())
    {
        ComPtr<IProgress> ptrOtherProgress = mptrOtherProgress;
        mptrOtherProgress.setNull();
        copyProgressInfo(ptrOtherProgress, fEarly);
    }
}

/**
 * Called to copy over the progress information from @a pOtherProgress.
 *
 * @param   pOtherProgress  The source of the information.
 * @param   fEarly          Early copy.
 *
 * @note    The caller owns the write lock and as cleared mptrOtherProgress
 *          already (or we might recurse forever)!
 */
void ProgressProxy::copyProgressInfo(IProgress *pOtherProgress, bool fEarly)
{
    HRESULT hrc;
    LogFlowThisFunc(("\n"));

    NOREF(fEarly);

    /*
     * No point in doing this if the progress object was canceled already.
     */
    if (!mCanceled)
    {
        /* Detect if the other progress object was canceled. */
        BOOL fCanceled;
        hrc = pOtherProgress->COMGETTER(Canceled)(&fCanceled);
        if (FAILED(hrc))
            fCanceled = FALSE;
        if (fCanceled)
        {
            LogFlowThisFunc(("Canceled\n"));
            mCanceled = TRUE;
            if (m_pfnCancelCallback)
                m_pfnCancelCallback(m_pvCancelUserArg);
        }
        else
        {
            /* Has it completed? */
            BOOL fCompleted;
            hrc = pOtherProgress->COMGETTER(Completed)(&fCompleted);
            if (FAILED(hrc))
                fCompleted = TRUE;
            Assert(fCompleted || fEarly);
            if (fCompleted)
            {
                /* Check the result. */
                LONG lResult;
                hrc = pOtherProgress->COMGETTER(ResultCode)(&lResult);
                if (FAILED(hrc))
                    lResult = (LONG)hrc;
                if (SUCCEEDED((HRESULT)lResult))
                    LogFlowThisFunc(("Succeeded\n"));
                else
                {
                    /* Get the error information. */
                    ComPtr<IVirtualBoxErrorInfo> ptrErrorInfo;
                    hrc = pOtherProgress->COMGETTER(ErrorInfo)(ptrErrorInfo.asOutParam());
                    if (SUCCEEDED(hrc) && !ptrErrorInfo.isNull())
                    {
                        Bstr bstrIID;
                        hrc = ptrErrorInfo->COMGETTER(InterfaceID)(bstrIID.asOutParam()); AssertComRC(hrc);
                        if (FAILED(hrc))
                            bstrIID.setNull();

                        Bstr bstrComponent;
                        hrc = ptrErrorInfo->COMGETTER(Component)(bstrComponent.asOutParam()); AssertComRC(hrc);
                        if (FAILED(hrc))
                            bstrComponent = "failed";

                        Bstr bstrText;
                        hrc = ptrErrorInfo->COMGETTER(Text)(bstrText.asOutParam()); AssertComRC(hrc);
                        if (FAILED(hrc))
                            bstrText = "<failed>";

                        Utf8Str strText(bstrText);
                        LogFlowThisFunc(("Got ErrorInfo(%s); hrcResult=%Rhrc\n", strText.c_str(), (HRESULT)lResult));
                        Progress::i_notifyComplete((HRESULT)lResult,
                                                   Guid(bstrIID).ref(),
                                                   Utf8Str(bstrComponent).c_str(),
                                                   "%s", strText.c_str());
                    }
                    else
                    {
                        LogFlowThisFunc(("ErrorInfo failed with hrc=%Rhrc; hrcResult=%Rhrc\n", hrc, (HRESULT)lResult));
                        Progress::i_notifyComplete((HRESULT)lResult,
                                                   COM_IIDOF(IProgress),
                                                   "ProgressProxy",
                                                   tr("No error info"));
                    }
                }
            }
            else
                LogFlowThisFunc(("Not completed\n"));
        }
    }
    else
        LogFlowThisFunc(("Already canceled\n"));

    /*
     * Did cancelable state change (point of no return)?
     */
    if (mCancelable && !mCompleted && !mCanceled)
    {
        BOOL fCancelable;
        hrc = pOtherProgress->COMGETTER(Cancelable)(&fCancelable); AssertComRC(hrc);
        if (SUCCEEDED(hrc) && !fCancelable)
        {
            LogFlowThisFunc(("point-of-no-return reached\n"));
            mCancelable = FALSE;
        }
    }
}


// IProgress properties
////////////////////////////////////////////////////////////////////////////////

STDMETHODIMP ProgressProxy::COMGETTER(Cancelable)(BOOL *aCancelable)
{
    CheckComArgOutPointerValid(aCancelable);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        /* ASSUME: The cancelable property can only change to FALSE. */
        if (!mCancelable || mptrOtherProgress.isNull())
            *aCancelable = mCancelable;
        else
        {
            hrc = mptrOtherProgress->COMGETTER(Cancelable)(aCancelable);
            if (SUCCEEDED(hrc) && !*aCancelable)
            {
                LogFlowThisFunc(("point-of-no-return reached\n"));
                mCancelable = FALSE;
            }
        }
    }
    return hrc;
}

STDMETHODIMP ProgressProxy::COMGETTER(Percent)(ULONG *aPercent)
{
    CheckComArgOutPointerValid(aPercent);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (mptrOtherProgress.isNull())
            hrc = Progress::COMGETTER(Percent)(aPercent);
        else
        {
            /*
             * Get the overall percent of the other object and adjust it with
             * the weighting given to the period before proxying started.
             */
            ULONG uPct;
            hrc = mptrOtherProgress->COMGETTER(Percent)(&uPct);
            if (SUCCEEDED(hrc))
            {
                double rdPercent = ((double)uPct / 100 * muOtherProgressWeight + muOtherProgressStartWeight)
                                 / m_ulTotalOperationsWeight * 100;
                *aPercent = RT_MIN((ULONG)rdPercent, 99); /* mptrOtherProgress is cleared when its completed,
                                                             so we can never return 100%. */
            }
        }
    }
    return hrc;
}

STDMETHODIMP ProgressProxy::COMGETTER(TimeRemaining)(LONG *aTimeRemaining)
{
    CheckComArgOutPointerValid(aTimeRemaining);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (mptrOtherProgress.isNull())
            hrc = Progress::COMGETTER(TimeRemaining)(aTimeRemaining);
        else
            hrc = mptrOtherProgress->COMGETTER(TimeRemaining)(aTimeRemaining);
    }
    return hrc;
}

STDMETHODIMP ProgressProxy::COMGETTER(Completed)(BOOL *aCompleted)
{
    /* Not proxied since we EXPECT a normal completion notification call. */
    return Progress::COMGETTER(Completed)(aCompleted);
}

STDMETHODIMP ProgressProxy::COMGETTER(Canceled)(BOOL *aCanceled)
{
    CheckComArgOutPointerValid(aCanceled);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        /* Check the local data first, then the other object. */
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        hrc = Progress::COMGETTER(Canceled)(aCanceled);
        if (   SUCCEEDED(hrc)
            && !*aCanceled
            && !mptrOtherProgress.isNull()
            && mCancelable)
        {
            hrc = mptrOtherProgress->COMGETTER(Canceled)(aCanceled);
            if (SUCCEEDED(hrc) && *aCanceled)
                /* This will not complete the object, only mark it as canceled. */
                clearOtherProgressObjectInternal(false /*fEarly*/);
        }
    }
    return hrc;
}

STDMETHODIMP ProgressProxy::COMGETTER(ResultCode)(LONG *aResultCode)
{
    /* Not proxied since we EXPECT a normal completion notification call. */
    return Progress::COMGETTER(ResultCode)(aResultCode);
}

STDMETHODIMP ProgressProxy::COMGETTER(ErrorInfo)(IVirtualBoxErrorInfo **aErrorInfo)
{
    /* Not proxied since we EXPECT a normal completion notification call. */
    return Progress::COMGETTER(ErrorInfo)(aErrorInfo);
}

STDMETHODIMP ProgressProxy::COMGETTER(Operation)(ULONG *aOperation)
{
    CheckComArgOutPointerValid(aOperation);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (mptrOtherProgress.isNull())
            hrc =  Progress::COMGETTER(Operation)(aOperation);
        else
        {
            ULONG uCurOtherOperation;
            hrc = mptrOtherProgress->COMGETTER(Operation)(&uCurOtherOperation);
            if (SUCCEEDED(hrc))
                *aOperation = uCurOtherOperation + muOtherProgressStartOperation;
        }
    }
    return hrc;
}

STDMETHODIMP ProgressProxy::COMGETTER(OperationDescription)(BSTR *aOperationDescription)
{
    CheckComArgOutPointerValid(aOperationDescription);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (mptrOtherProgress.isNull() || !mfMultiOperation)
            hrc = Progress::COMGETTER(OperationDescription)(aOperationDescription);
        else
            hrc = mptrOtherProgress->COMGETTER(OperationDescription)(aOperationDescription);
    }
    return hrc;
}

STDMETHODIMP ProgressProxy::COMGETTER(OperationPercent)(ULONG *aOperationPercent)
{
    CheckComArgOutPointerValid(aOperationPercent);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (mptrOtherProgress.isNull() || !mfMultiOperation)
            hrc = Progress::COMGETTER(OperationPercent)(aOperationPercent);
        else
            hrc = mptrOtherProgress->COMGETTER(OperationPercent)(aOperationPercent);
    }
    return hrc;
}

STDMETHODIMP ProgressProxy::COMSETTER(Timeout)(ULONG aTimeout)
{
    /* Not currently supported. */
    NOREF(aTimeout);
    AssertFailed();
    return E_NOTIMPL;
}

STDMETHODIMP ProgressProxy::COMGETTER(Timeout)(ULONG *aTimeout)
{
    /* Not currently supported. */
    CheckComArgOutPointerValid(aTimeout);

    AssertFailed();
    return E_NOTIMPL;
}

// IProgress methods
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP ProgressProxy::WaitForCompletion(LONG aTimeout)
{
    HRESULT hrc;
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aTimeout=%d\n", aTimeout));

    /* No need to wait on the proxied object for these since we'll get the
       normal completion notifications. */
    hrc = Progress::WaitForCompletion(aTimeout);

    LogFlowThisFuncLeave();
    return hrc;
}

STDMETHODIMP ProgressProxy::WaitForOperationCompletion(ULONG aOperation, LONG aTimeout)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aOperation=%d aTimeout=%d\n", aOperation, aTimeout));

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        CheckComArgExpr(aOperation, aOperation < m_cOperations);

        /*
         * Check if we can wait locally.
         */
        if (   aOperation + 1 == m_cOperations /* final operation */
            || mptrOtherProgress.isNull())
        {
            /* ASSUMES that Progress::WaitForOperationCompletion is using
               AutoWriteLock::leave() as it saves us from duplicating the code! */
            hrc = Progress::WaitForOperationCompletion(aOperation, aTimeout);
        }
        else
        {
            LogFlowThisFunc(("calling the other object...\n"));
            ComPtr<IProgress> ptrOtherProgress = mptrOtherProgress;
            alock.release();

            hrc = ptrOtherProgress->WaitForOperationCompletion(aOperation, aTimeout);
        }
    }

    LogFlowThisFuncLeave();
    return hrc;
}

STDMETHODIMP ProgressProxy::Cancel()
{
    LogFlowThisFunc(("\n"));
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (mptrOtherProgress.isNull() || !mCancelable)
            hrc = Progress::Cancel();
        else
        {
            hrc = mptrOtherProgress->Cancel();
            if (SUCCEEDED(hrc))
                clearOtherProgressObjectInternal(false /*fEarly*/);
        }
    }

    LogFlowThisFunc(("returns %Rhrc\n", hrc));
    return hrc;
}

STDMETHODIMP ProgressProxy::SetCurrentOperationProgress(ULONG aPercent)
{
    /* Not supported - why do we actually expose this? */
    NOREF(aPercent);
    return E_NOTIMPL;
}

STDMETHODIMP ProgressProxy::SetNextOperation(IN_BSTR bstrNextOperationDescription, ULONG ulNextOperationsWeight)
{
    /* Not supported - why do we actually expose this? */
    NOREF(bstrNextOperationDescription);
    NOREF(ulNextOperationsWeight);
    return E_NOTIMPL;
}

#ifdef VBOX_WITH_XPCOM
NS_DECL_CLASSINFO(ProgressProxy)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(ProgressProxy, IProgress)
#endif

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
