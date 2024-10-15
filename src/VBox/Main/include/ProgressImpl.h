/* $Id: ProgressImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_ProgressImpl_h
#define MAIN_INCLUDED_ProgressImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ProgressWrap.h"
#include "VirtualBoxBase.h"
#include "EventImpl.h"

#include <iprt/semaphore.h>

////////////////////////////////////////////////////////////////////////////////

/**
 * Class for progress objects.
 */
class ATL_NO_VTABLE Progress :
    public ProgressWrap
{
public:
    DECLARE_NOT_AGGREGATABLE(Progress)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only

    /**
     * Simplified constructor for progress objects that have only one
     * operation as a task.
     * @param aParent
     * @param aInitiator
     * @param aDescription
     * @param aCancelable
     * @return
     */
    HRESULT init(
#if !defined(VBOX_COM_INPROC)
                  VirtualBox *aParent,
#endif
                  IUnknown *aInitiator,
                  const Utf8Str &aDescription,
                  BOOL aCancelable)
    {
        return init(
#if !defined(VBOX_COM_INPROC)
            aParent,
#endif
            aInitiator,
            aDescription,
            aCancelable,
            1,      // cOperations
            1,      // ulTotalOperationsWeight
            aDescription, // aFirstOperationDescription
            1);     // ulFirstOperationWeight
    }

    /**
     * Not quite so simplified constructor for progress objects that have
     * more than one operation, but all sub-operations are weighed the same.
     * @param aParent
     * @param aInitiator
     * @param aDescription
     * @param aCancelable
     * @param cOperations
     * @param aFirstOperationDescription
     * @return
     */
    HRESULT init(
#if !defined(VBOX_COM_INPROC)
                  VirtualBox *aParent,
#endif
                  IUnknown *aInitiator,
                  const Utf8Str &aDescription, BOOL aCancelable,
                  ULONG cOperations,
                  const Utf8Str &aFirstOperationDescription)
    {
        return init(
#if !defined(VBOX_COM_INPROC)
            aParent,
#endif
            aInitiator,
            aDescription,
            aCancelable,
            cOperations,      // cOperations
            cOperations,      // ulTotalOperationsWeight = cOperations
            aFirstOperationDescription, // aFirstOperationDescription
            1);     // ulFirstOperationWeight: weigh them all the same
    }

    HRESULT init(
#if !defined(VBOX_COM_INPROC)
                  VirtualBox *aParent,
#endif
                  IUnknown *aInitiator,
                  const Utf8Str &aDescription,
                  BOOL aCancelable,
                  ULONG cOperations,
                  ULONG ulTotalOperationsWeight,
                  const Utf8Str &aFirstOperationDescription,
                  ULONG ulFirstOperationWeight);

    HRESULT init(BOOL aCancelable,
                 ULONG aOperationCount,
                 const Utf8Str &aOperationDescription);

    void uninit();


    // public methods only for internal purposes
    HRESULT i_notifyComplete(HRESULT aResultCode);
    HRESULT i_notifyComplete(HRESULT aResultCode,
                             const GUID &aIID,
                             const char *pcszComponent,
                             const char *aText,
                             ...);
    HRESULT i_notifyCompleteV(HRESULT aResultCode,
                              const GUID &aIID,
                              const char *pcszComponent,
                              const char *aText,
                              va_list va);
    HRESULT i_notifyCompleteBoth(HRESULT aResultCode,
                                 int vrc,
                                 const GUID &aIID,
                                 const char *pcszComponent,
                                 const char *aText,
                                 ...);
    HRESULT i_notifyCompleteBothV(HRESULT aResultCode,
                                  int vrc,
                                  const GUID &aIID,
                                  const char *pcszComponent,
                                  const char *aText,
                                  va_list va);

    bool i_setCancelCallback(void (*pfnCallback)(void *), void *pvUser);

    static DECLCALLBACK(int) i_iprtProgressCallback(unsigned uPercentage, void *pvUser);
    static DECLCALLBACK(int) i_vdProgressCallback(void *pvUser, unsigned uPercentage);

protected:
    DECLARE_COMMON_CLASS_METHODS(Progress)

#if !defined(VBOX_COM_INPROC)
    /** Weak parent. */
    VirtualBox * const      mParent;
#endif
    const ComObjPtr<EventSource> pEventSource;
    const ComPtr<IUnknown>  mInitiator;

    const Guid mId;
    const com::Utf8Str mDescription;

    uint64_t m_ullTimestamp;                        // progress object creation timestamp, for ETA computation

    void (*m_pfnCancelCallback)(void *);
    void *m_pvCancelUserArg;

    /* The fields below are to be properly initialized by subclasses */

    BOOL mCompleted;
    BOOL mCancelable;
    BOOL mCanceled;
    HRESULT mResultCode;
    ComPtr<IVirtualBoxErrorInfo> mErrorInfo;

    ULONG m_cOperations;                            // number of operations (so that progress dialog can
                                                    // display something like 1/3)
    ULONG m_ulTotalOperationsWeight;                // sum of weights of all operations, given to constructor

    ULONG m_ulOperationsCompletedWeight;            // summed-up weight of operations that have been completed; initially 0

    ULONG m_ulCurrentOperation;                     // operations counter, incremented with
                                                    // each setNextOperation()
    com::Utf8Str m_operationDescription;            // name of current operation; initially
                                                    // from constructor, changed with setNextOperation()
    ULONG m_ulCurrentOperationWeight;               // weight of current operation, given to setNextOperation()
    ULONG m_ulOperationPercent;                     // percentage of current operation, set with setCurrentOperationProgress()
    ULONG m_cMsTimeout;                             /**< Automatic timeout value. 0 means none. */

private:
    // wrapped IProgress properties
    HRESULT getId(com::Guid &aId);
    HRESULT getDescription(com::Utf8Str &aDescription);
    HRESULT getInitiator(ComPtr<IUnknown> &aInitiator);
    HRESULT getCancelable(BOOL *aCancelable);
    HRESULT getPercent(ULONG *aPercent);
    HRESULT getTimeRemaining(LONG *aTimeRemaining);
    HRESULT getCompleted(BOOL *aCompleted);
    HRESULT getCanceled(BOOL *aCanceled);
    HRESULT getResultCode(LONG *aResultCode);
    HRESULT getErrorInfo(ComPtr<IVirtualBoxErrorInfo> &aErrorInfo);
    HRESULT getOperationCount(ULONG *aOperationCount);
    HRESULT getOperation(ULONG *aOperation);
    HRESULT getOperationDescription(com::Utf8Str &aOperationDescription);
    HRESULT getOperationPercent(ULONG *aOperationPercent);
    HRESULT getOperationWeight(ULONG *aOperationWeight);
    HRESULT getTimeout(ULONG *aTimeout);
    HRESULT setTimeout(ULONG aTimeout);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);

    // wrapped IProgress methods
    HRESULT waitForCompletion(LONG aTimeout);
    HRESULT waitForOperationCompletion(ULONG aOperation,
                                       LONG aTimeout);
    HRESULT cancel();

    // wrapped IInternalProgressControl methods
    HRESULT setCurrentOperationProgress(ULONG aPercent);
    HRESULT waitForOtherProgressCompletion(const ComPtr<IProgress> &aProgressOther,
                                           ULONG aTimeoutMS);
    HRESULT setNextOperation(const com::Utf8Str &aNextOperationDescription,
                             ULONG aNextOperationsWeight);
    HRESULT notifyPointOfNoReturn();
    HRESULT notifyComplete(LONG aResultCode,
                           const ComPtr<IVirtualBoxErrorInfo> &aErrorInfo);

    // internal helper methods
    HRESULT i_notifyCompleteWorker(HRESULT aResultCode, const ComPtr<IVirtualBoxErrorInfo> &aErrorInfo);
    double i_calcTotalPercent();
    void i_checkForAutomaticTimeout(void);

    RTSEMEVENTMULTI mCompletedSem;
    ULONG mWaitersCount;

private:
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(Progress); /* Shuts up MSC warning C4625. */
};

#endif /* !MAIN_INCLUDED_ProgressImpl_h */

