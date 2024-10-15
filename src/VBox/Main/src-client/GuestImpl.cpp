/* $Id: GuestImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation: Guest features.
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

#define LOG_GROUP LOG_GROUP_MAIN_GUEST
#include "LoggingNew.h"

#include "GuestImpl.h"
#ifdef VBOX_WITH_GUEST_CONTROL
# include "GuestSessionImpl.h"
#endif
#include "Global.h"
#include "ConsoleImpl.h"
#include "ProgressImpl.h"
#ifdef VBOX_WITH_DRAG_AND_DROP
# include "GuestDnDPrivate.h"
#endif
#include "VMMDev.h"

#include "AutoCaller.h"
#include "Performance.h"
#include "VBoxEvents.h"

#include <VBox/VMMDev.h>
#include <iprt/cpp/utils.h>
#include <iprt/ctype.h>
#include <iprt/stream.h>
#include <iprt/timer.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/version.h>

// defines
/////////////////////////////////////////////////////////////////////////////

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(Guest)

HRESULT Guest::FinalConstruct()
{
    return BaseFinalConstruct();
}

void Guest::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the guest object.
 */
HRESULT Guest::init(Console *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    ULONG aMemoryBalloonSize = 0;
    HRESULT hr = mParent->i_machine()->COMGETTER(MemoryBalloonSize)(&aMemoryBalloonSize);
    if (SUCCEEDED(hr))
        mMemoryBalloonSize = aMemoryBalloonSize;
    else
        mMemoryBalloonSize = 0; /* Default is no ballooning */

    BOOL fPageFusionEnabled = FALSE;
    hr = mParent->i_machine()->COMGETTER(PageFusionEnabled)(&fPageFusionEnabled);
    if (SUCCEEDED(hr))
        mfPageFusionEnabled = fPageFusionEnabled;
    else
        mfPageFusionEnabled = false; /* Default is no page fusion*/

    mStatUpdateInterval = 0; /* Default is not to report guest statistics at all */
    mCollectVMMStats = false;

    /* Clear statistics. */
    mNetStatRx = mNetStatTx = 0;
    mNetStatLastTs = RTTimeNanoTS();
    for (unsigned i = 0 ; i < GUESTSTATTYPE_MAX; i++)
        mCurrentGuestStat[i] = 0;
    mVmValidStats = pm::VMSTATMASK_NONE;
    RT_ZERO(mCurrentGuestCpuUserStat);
    RT_ZERO(mCurrentGuestCpuKernelStat);
    RT_ZERO(mCurrentGuestCpuIdleStat);

    mMagic = GUEST_MAGIC;
    mStatTimer = NIL_RTTIMERLR;

    hr = unconst(mEventSource).createObject();
    if (SUCCEEDED(hr))
        hr = mEventSource->init();

    mCpus = 1;

#ifdef VBOX_WITH_DRAG_AND_DROP
    if (SUCCEEDED(hr))
    {
        try
        {
            GuestDnD::createInstance(this /* pGuest */);
            hr = unconst(mDnDSource).createObject();
            if (SUCCEEDED(hr))
                hr = mDnDSource->init(this /* pGuest */);
            if (SUCCEEDED(hr))
            {
                hr = unconst(mDnDTarget).createObject();
                if (SUCCEEDED(hr))
                    hr = mDnDTarget->init(this /* pGuest */);
            }

            LogFlowFunc(("Drag and drop initializied with hr=%Rhrc\n", hr));
        }
        catch (std::bad_alloc &)
        {
            hr = E_OUTOFMEMORY;
        }
    }
#endif

    /* Confirm a successful initialization when it's the case: */
    if (SUCCEEDED(hr))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed();

    LogFlowFunc(("hr=%Rhrc\n", hr));
    return hr;
}

/**
 * Uninitializes the instance and sets the ready flag to FALSE.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void Guest::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    /* Destroy stat update timer */
    int vrc = RTTimerLRDestroy(mStatTimer);
    AssertMsgRC(vrc, ("Failed to create guest statistics update timer(%Rra)\n", vrc));
    mStatTimer = NIL_RTTIMERLR;
    mMagic     = 0;

#ifdef VBOX_WITH_GUEST_CONTROL
    LogFlowThisFunc(("Closing sessions (%RU64 total)\n",
                     mData.mGuestSessions.size()));
    GuestSessions::iterator itSessions = mData.mGuestSessions.begin();
    while (itSessions != mData.mGuestSessions.end())
    {
# ifdef DEBUG
        ULONG cRefs = itSessions->second->AddRef();
        LogFlowThisFunc(("sessionID=%RU32, cRefs=%RU32\n", itSessions->first, cRefs > 1 ? cRefs - 1 : 0));
        itSessions->second->Release();
# endif
        itSessions->second->uninit();
        ++itSessions;
    }
    mData.mGuestSessions.clear();
#endif

#ifdef VBOX_WITH_DRAG_AND_DROP
    GuestDnD::destroyInstance();
    unconst(mDnDSource).setNull();
    unconst(mDnDTarget).setNull();
#endif

    unconst(mEventSource).setNull();
    unconst(mParent) = NULL;

    LogFlowFuncLeave();
}

/* static */
DECLCALLBACK(void) Guest::i_staticUpdateStats(RTTIMERLR hTimerLR, void *pvUser, uint64_t iTick)
{
    AssertReturnVoid(pvUser != NULL);
    Guest *guest = static_cast<Guest *>(pvUser);
    Assert(guest->mMagic == GUEST_MAGIC);
    if (guest->mMagic == GUEST_MAGIC)
        guest->i_updateStats(iTick);

    NOREF(hTimerLR);
}

/* static */
DECLCALLBACK(int)  Guest::i_staticEnumStatsCallback(const char *pszName, STAMTYPE enmType, void *pvSample,
                                                    STAMUNIT enmUnit, const char *pszUnit, STAMVISIBILITY enmVisiblity,
                                                    const char *pszDesc, void *pvUser)
{
    RT_NOREF(enmVisiblity, pszDesc, pszUnit);
    AssertLogRelMsgReturn(enmType == STAMTYPE_COUNTER, ("Unexpected sample type %d ('%s')\n", enmType, pszName), VINF_SUCCESS);
    AssertLogRelMsgReturn(enmUnit == STAMUNIT_BYTES, ("Unexpected sample unit %d ('%s')\n", enmUnit, pszName), VINF_SUCCESS);

    /* Get the base name w/ slash. */
    const char *pszLastSlash = strrchr(pszName, '/');
    AssertLogRelMsgReturn(pszLastSlash, ("Unexpected sample '%s'\n", pszName), VINF_SUCCESS);

    /* Receive or transmit? */
    bool fRx;
    if (!strcmp(pszLastSlash, "/BytesReceived"))
        fRx = true;
    else if (!strcmp(pszLastSlash, "/BytesTransmitted"))
        fRx = false;
    else
        AssertLogRelMsgFailedReturn(("Unexpected sample '%s'\n", pszName), VINF_SUCCESS);

#if 0 /* not used for anything, so don't bother parsing it. */
    /* Find start of instance number. ASSUMES '/Public/Net/Name<Instance digits>/Bytes...' */
    do
        --pszLastSlash;
    while (pszLastSlash > pszName && RT_C_IS_DIGIT(*pszLastSlash));
    pszLastSlash++;

    uint8_t uInstance;
    int vrc = RTStrToUInt8Ex(pszLastSlash, NULL, 10, &uInstance);
    AssertLogRelMsgReturn(RT_SUCCESS(vrc) && vrc != VWRN_NUMBER_TOO_BIG && vrc != VWRN_NEGATIVE_UNSIGNED,
                          ("%Rrc '%s'\n", vrc, pszName), VINF_SUCCESS)
#endif

    /* Add the bytes to our counters. */
    PSTAMCOUNTER pCnt   = (PSTAMCOUNTER)pvSample;
    Guest       *pGuest = (Guest *)pvUser;
    uint64_t     cb     = pCnt->c;
#if 0
    LogFlowFunc(("%s i=%u d=%s %llu bytes\n", pszName, uInstance, fRx ? "RX" : "TX", cb));
#else
    LogFlowFunc(("%s d=%s %llu bytes\n", pszName, fRx ? "RX" : "TX", cb));
#endif
    if (fRx)
        pGuest->mNetStatRx += cb;
    else
        pGuest->mNetStatTx += cb;

    return VINF_SUCCESS;
}

void Guest::i_updateStats(uint64_t iTick)
{
    RT_NOREF(iTick);

    uint64_t cbFreeTotal      = 0;
    uint64_t cbAllocTotal     = 0;
    uint64_t cbBalloonedTotal = 0;
    uint64_t cbSharedTotal    = 0;
    uint64_t cbSharedMem      = 0;
    ULONG    uNetStatRx       = 0;
    ULONG    uNetStatTx       = 0;
    ULONG    aGuestStats[GUESTSTATTYPE_MAX];
    RT_ZERO(aGuestStats);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    ULONG validStats = mVmValidStats;
    /* Check if we have anything to report */
    if (validStats)
    {
        mVmValidStats = pm::VMSTATMASK_NONE;
        memcpy(aGuestStats, mCurrentGuestStat, sizeof(aGuestStats));
    }
    alock.release();

    /*
     * Calling SessionMachine may take time as the object resides in VBoxSVC
     * process. This is why we took a snapshot of currently collected stats
     * and released the lock.
     */
    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk())
    {
        int vrc;

        /*
         * There is no point in collecting VM shared memory if other memory
         * statistics are not available yet. Or is there?
         */
        if (validStats)
        {
            /* Query the missing per-VM memory statistics. */
            uint64_t cbTotalMemIgn, cbPrivateMemIgn, cbZeroMemIgn;
            vrc = ptrVM.vtable()->pfnPGMR3QueryMemoryStats(ptrVM.rawUVM(), &cbTotalMemIgn, &cbPrivateMemIgn,
                                                           &cbSharedMem, &cbZeroMemIgn);
            if (vrc == VINF_SUCCESS)
                validStats |= pm::VMSTATMASK_GUEST_MEMSHARED;
        }

        if (mCollectVMMStats)
        {
            vrc = ptrVM.vtable()->pfnPGMR3QueryGlobalMemoryStats(ptrVM.rawUVM(), &cbAllocTotal, &cbFreeTotal,
                                                                 &cbBalloonedTotal, &cbSharedTotal);
            AssertRC(vrc);
            if (vrc == VINF_SUCCESS)
                validStats |= pm::VMSTATMASK_VMM_ALLOC  | pm::VMSTATMASK_VMM_FREE
                           |  pm::VMSTATMASK_VMM_BALOON | pm::VMSTATMASK_VMM_SHARED;
        }

        uint64_t uRxPrev = mNetStatRx;
        uint64_t uTxPrev = mNetStatTx;
        mNetStatRx = mNetStatTx = 0;
        vrc = ptrVM.vtable()->pfnSTAMR3Enum(ptrVM.rawUVM(), "/Public/Net/*/Bytes*", i_staticEnumStatsCallback, this);
        AssertRC(vrc);

        uint64_t uTsNow = RTTimeNanoTS();
        uint64_t cNsPassed = uTsNow - mNetStatLastTs;
        if (cNsPassed >= 1000)
        {
            mNetStatLastTs = uTsNow;

            uNetStatRx = (ULONG)((mNetStatRx - uRxPrev) * 1000000 / (cNsPassed / 1000)); /* in bytes per second */
            uNetStatTx = (ULONG)((mNetStatTx - uTxPrev) * 1000000 / (cNsPassed / 1000)); /* in bytes per second */
            validStats |= pm::VMSTATMASK_NET_RX | pm::VMSTATMASK_NET_TX;
            LogFlowThisFunc(("Net Rx=%llu Tx=%llu Ts=%llu Delta=%llu\n", mNetStatRx, mNetStatTx, uTsNow, cNsPassed));
        }
        else
        {
            /* Can happen on resume or if we're using a non-monotonic clock
               source for the timer and the time is adjusted. */
            mNetStatRx = uRxPrev;
            mNetStatTx = uTxPrev;
            LogThisFunc(("Net Ts=%llu cNsPassed=%llu - too small interval\n", uTsNow, cNsPassed));
        }
    }

    mParent->i_reportVmStatistics(validStats,
                                  aGuestStats[GUESTSTATTYPE_CPUUSER],
                                  aGuestStats[GUESTSTATTYPE_CPUKERNEL],
                                  aGuestStats[GUESTSTATTYPE_CPUIDLE],
                                  /* Convert the units for RAM usage stats: page (4K) -> 1KB units */
                                  mCurrentGuestStat[GUESTSTATTYPE_MEMTOTAL] * (_4K/_1K),
                                  mCurrentGuestStat[GUESTSTATTYPE_MEMFREE] * (_4K/_1K),
                                  mCurrentGuestStat[GUESTSTATTYPE_MEMBALLOON] * (_4K/_1K),
                                  (ULONG)(cbSharedMem / _1K), /* bytes -> KB */
                                  mCurrentGuestStat[GUESTSTATTYPE_MEMCACHE] * (_4K/_1K),
                                  mCurrentGuestStat[GUESTSTATTYPE_PAGETOTAL] * (_4K/_1K),
                                  (ULONG)(cbAllocTotal / _1K), /* bytes -> KB */
                                  (ULONG)(cbFreeTotal / _1K),
                                  (ULONG)(cbBalloonedTotal / _1K),
                                  (ULONG)(cbSharedTotal / _1K),
                                  uNetStatRx,
                                  uNetStatTx);
}

// IGuest properties
/////////////////////////////////////////////////////////////////////////////

HRESULT Guest::getOSTypeId(com::Utf8Str &aOSTypeId)
{
    HRESULT hrc = S_OK;
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (!mData.mInterfaceVersion.isEmpty())
        aOSTypeId = mData.mOSTypeId;
    else
    {
        /* Redirect the call to IMachine if no additions are installed. */
        ComPtr<IMachine> ptrMachine(mParent->i_machine());
        alock.release();
        Bstr bstr;
        hrc = ptrMachine->COMGETTER(OSTypeId)(bstr.asOutParam());
        aOSTypeId = bstr;
    }
    return hrc;
}

HRESULT Guest::getAdditionsRunLevel(AdditionsRunLevelType_T *aAdditionsRunLevel)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAdditionsRunLevel = mData.mAdditionsRunLevel;

    return S_OK;
}

HRESULT Guest::getAdditionsVersion(com::Utf8Str &aAdditionsVersion)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = S_OK;

    /*
     * Return the ReportGuestInfo2 version info if available.
     */
    if (   !mData.mAdditionsVersionNew.isEmpty()
         || mData.mAdditionsRunLevel <= AdditionsRunLevelType_None)
        aAdditionsVersion = mData.mAdditionsVersionNew;
    else
    {
        /*
         * If we're running older Guest Additions (< 3.2.0) try get it from
         * the guest properties.  Detected switched around Version and
         * Revision in early 3.1.x releases (see r57115).
         */
        ComPtr<IMachine> ptrMachine = mParent->i_machine();
        alock.release(); /* No need to hold this during the IPC fun. */

        Bstr bstr;
        hrc = ptrMachine->GetGuestPropertyValue(Bstr("/VirtualBox/GuestAdd/Version").raw(), bstr.asOutParam());
        if (   SUCCEEDED(hrc)
            && !bstr.isEmpty())
        {
            Utf8Str str(bstr);
            if (str.count('.') == 0)
                hrc = ptrMachine->GetGuestPropertyValue(Bstr("/VirtualBox/GuestAdd/Revision").raw(), bstr.asOutParam());
            str = bstr;
            if (str.count('.') != 2)
                hrc = E_FAIL;
        }

        if (SUCCEEDED(hrc))
            aAdditionsVersion = bstr;
        else
        {
            /* Returning 1.4 is better than nothing. */
            alock.acquire();
            aAdditionsVersion = mData.mInterfaceVersion;
            hrc = S_OK;
        }
    }
    return hrc;
}

HRESULT Guest::getAdditionsRevision(ULONG *aAdditionsRevision)
{
    HRESULT hrc = S_OK;
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Return the ReportGuestInfo2 version info if available.
     */
    if (   !mData.mAdditionsVersionNew.isEmpty()
        || mData.mAdditionsRunLevel <= AdditionsRunLevelType_None)
        *aAdditionsRevision = mData.mAdditionsRevision;
    else
    {
        /*
         * If we're running older Guest Additions (< 3.2.0) try get it from
         * the guest properties. Detected switched around Version and
         * Revision in early 3.1.x releases (see r57115).
         */
        ComPtr<IMachine> ptrMachine = mParent->i_machine();
        alock.release(); /* No need to hold this during the IPC fun. */

        Bstr bstr;
        hrc = ptrMachine->GetGuestPropertyValue(Bstr("/VirtualBox/GuestAdd/Revision").raw(), bstr.asOutParam());
        if (SUCCEEDED(hrc))
        {
            Utf8Str str(bstr);
            uint32_t uRevision;
            int vrc = RTStrToUInt32Full(str.c_str(), 0, &uRevision);
            if (vrc != VINF_SUCCESS && str.count('.') == 2)
            {
                hrc = ptrMachine->GetGuestPropertyValue(Bstr("/VirtualBox/GuestAdd/Version").raw(), bstr.asOutParam());
                if (SUCCEEDED(hrc))
                {
                    str = bstr;
                    vrc = RTStrToUInt32Full(str.c_str(), 0, &uRevision);
                }
            }
            if (vrc == VINF_SUCCESS)
                *aAdditionsRevision = uRevision;
            else
                hrc = VBOX_E_IPRT_ERROR;
        }
        if (FAILED(hrc))
        {
            /* Return 0 if we don't know. */
            *aAdditionsRevision = 0;
            hrc = S_OK;
        }
    }
    return hrc;
}

HRESULT Guest::getDnDSource(ComPtr<IGuestDnDSource> &aDnDSource)
{
#ifndef VBOX_WITH_DRAG_AND_DROP
    RT_NOREF(aDnDSource);
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    /* No need to lock - lifetime constant. */
    HRESULT hr = mDnDSource.queryInterfaceTo(aDnDSource.asOutParam());

    LogFlowFuncLeaveRC(hr);
    return hr;
#endif /* VBOX_WITH_DRAG_AND_DROP */
}

HRESULT Guest::getDnDTarget(ComPtr<IGuestDnDTarget> &aDnDTarget)
{
#ifndef VBOX_WITH_DRAG_AND_DROP
    RT_NOREF(aDnDTarget);
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    /* No need to lock - lifetime constant. */
    HRESULT hr = mDnDTarget.queryInterfaceTo(aDnDTarget.asOutParam());

    LogFlowFuncLeaveRC(hr);
    return hr;
#endif /* VBOX_WITH_DRAG_AND_DROP */
}

HRESULT Guest::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    LogFlowThisFuncEnter();

    /* No need to lock - lifetime constant. */
    mEventSource.queryInterfaceTo(aEventSource.asOutParam());

    LogFlowFuncLeaveRC(S_OK);
    return S_OK;
}

HRESULT Guest::getFacilities(std::vector<ComPtr<IAdditionsFacility> > &aFacilities)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aFacilities.resize(mData.mFacilityMap.size());
    size_t i = 0;
    for (FacilityMapIter it = mData.mFacilityMap.begin(); it != mData.mFacilityMap.end(); ++it, ++i)
        it->second.queryInterfaceTo(aFacilities[i].asOutParam());

    return S_OK;
}

HRESULT Guest::getSessions(std::vector<ComPtr<IGuestSession> > &aSessions)
{
#ifdef VBOX_WITH_GUEST_CONTROL
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aSessions.resize(mData.mGuestSessions.size());
    size_t i = 0;
    for (GuestSessions::iterator it = mData.mGuestSessions.begin(); it != mData.mGuestSessions.end(); ++it, ++i)
        it->second.queryInterfaceTo(aSessions[i].asOutParam());

    return S_OK;
#else
    ReturnComNotImplemented();
#endif
}

BOOL Guest::i_isPageFusionEnabled()
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    return mfPageFusionEnabled;
}

HRESULT Guest::getMemoryBalloonSize(ULONG *aMemoryBalloonSize)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aMemoryBalloonSize = mMemoryBalloonSize;

    return S_OK;
}

HRESULT Guest::setMemoryBalloonSize(ULONG aMemoryBalloonSize)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* We must be 100% sure that IMachine::COMSETTER(MemoryBalloonSize)
     * does not call us back in any way! */
    HRESULT ret = mParent->i_machine()->COMSETTER(MemoryBalloonSize)(aMemoryBalloonSize);
    if (ret == S_OK)
    {
        mMemoryBalloonSize = aMemoryBalloonSize;
        /* forward the information to the VMM device */
        VMMDev *pVMMDev = mParent->i_getVMMDev();
        /* MUST release all locks before calling VMM device as its critsect
         * has higher lock order than anything in Main. */
        alock.release();
        if (pVMMDev)
        {
            PPDMIVMMDEVPORT pVMMDevPort = pVMMDev->getVMMDevPort();
            if (pVMMDevPort)
                pVMMDevPort->pfnSetMemoryBalloon(pVMMDevPort, aMemoryBalloonSize);
        }
    }

    return ret;
}

HRESULT Guest::getStatisticsUpdateInterval(ULONG *aStatisticsUpdateInterval)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aStatisticsUpdateInterval = mStatUpdateInterval;
    return S_OK;
}

HRESULT Guest::setStatisticsUpdateInterval(ULONG aStatisticsUpdateInterval)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Update the timer, creating it the first time we're called with a non-zero value. */
    int vrc;
    HRESULT hrc = S_OK;
    if (aStatisticsUpdateInterval > 0)
    {
        if (mStatTimer == NIL_RTTIMERLR)
        {
            vrc = RTTimerLRCreate(&mStatTimer, aStatisticsUpdateInterval * RT_MS_1SEC, &Guest::i_staticUpdateStats, this);
            AssertRCStmt(vrc, hrc = setErrorVrc(vrc, tr("Failed to create guest statistics update timer (%Rrc)"), vrc));
        }
        else if (aStatisticsUpdateInterval != mStatUpdateInterval)
        {
            vrc = RTTimerLRChangeInterval(mStatTimer, aStatisticsUpdateInterval * RT_NS_1SEC_64);
            AssertRCStmt(vrc, hrc = setErrorVrc(vrc, tr("Failed to change guest statistics update timer interval from %u to %u failed (%Rrc)"),
                                                mStatUpdateInterval, aStatisticsUpdateInterval, vrc));
            if (mStatUpdateInterval == 0)
            {
                vrc = RTTimerLRStart(mStatTimer, 0);
                AssertRCStmt(vrc, hrc = setErrorVrc(vrc, tr("Failed to start the guest statistics update timer (%Rrc)"), vrc));
            }
        }
    }
    /* Setting interval to zero - stop the update timer if needed: */
    else if (mStatUpdateInterval > 0 && mStatTimer != NIL_RTTIMERLR)
    {
        vrc = RTTimerLRStop(mStatTimer);
        AssertRCStmt(vrc, hrc = setErrorVrc(vrc, tr("Failed to stop the guest statistics update timer (%Rrc)"), vrc));
    }

    /* Update the interval now that the timer is in sync. */
    mStatUpdateInterval = aStatisticsUpdateInterval;

    /* Forward the information to the VMM device.
       MUST release all locks before calling VMM device as its critsect
       has higher lock order than anything in Main. */
    VMMDev *pVMMDev = mParent->i_getVMMDev();
    alock.release();
    if (pVMMDev)
    {
        PPDMIVMMDEVPORT pVMMDevPort = pVMMDev->getVMMDevPort();
        if (pVMMDevPort)
            pVMMDevPort->pfnSetStatisticsInterval(pVMMDevPort, aStatisticsUpdateInterval);
    }

    return hrc;
}


HRESULT Guest::internalGetStatistics(ULONG *aCpuUser, ULONG *aCpuKernel, ULONG *aCpuIdle,
                                     ULONG *aMemTotal, ULONG *aMemFree, ULONG *aMemBalloon,
                                     ULONG *aMemShared, ULONG *aMemCache, ULONG *aPageTotal,
                                     ULONG *aMemAllocTotal, ULONG *aMemFreeTotal,
                                     ULONG *aMemBalloonTotal, ULONG *aMemSharedTotal)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aCpuUser    = mCurrentGuestStat[GUESTSTATTYPE_CPUUSER];
    *aCpuKernel  = mCurrentGuestStat[GUESTSTATTYPE_CPUKERNEL];
    *aCpuIdle    = mCurrentGuestStat[GUESTSTATTYPE_CPUIDLE];
    *aMemTotal   = mCurrentGuestStat[GUESTSTATTYPE_MEMTOTAL]   * (_4K/_1K); /* page (4K) -> 1KB units */
    *aMemFree    = mCurrentGuestStat[GUESTSTATTYPE_MEMFREE]    * (_4K/_1K); /* page (4K) -> 1KB units */
    *aMemBalloon = mCurrentGuestStat[GUESTSTATTYPE_MEMBALLOON] * (_4K/_1K); /* page (4K) -> 1KB units */
    *aMemCache   = mCurrentGuestStat[GUESTSTATTYPE_MEMCACHE]   * (_4K/_1K); /* page (4K) -> 1KB units */
    *aPageTotal  = mCurrentGuestStat[GUESTSTATTYPE_PAGETOTAL]  * (_4K/_1K); /* page (4K) -> 1KB units */

    /* Play safe or smth? */
    *aMemAllocTotal   = 0;
    *aMemFreeTotal    = 0;
    *aMemBalloonTotal = 0;
    *aMemSharedTotal  = 0;
    *aMemShared       = 0;

    /* MUST release all locks before calling any PGM statistics queries,
     * as they are executed by EMT and that might deadlock us by VMM device
     * activity which waits for the Guest object lock. */
    alock.release();
    Console::SafeVMPtr ptrVM(mParent);
    if (!ptrVM.isOk())
        return E_FAIL;

    uint64_t cbFreeTotal, cbAllocTotal, cbBalloonedTotal, cbSharedTotal;
    int vrc = ptrVM.vtable()->pfnPGMR3QueryGlobalMemoryStats(ptrVM.rawUVM(), &cbAllocTotal, &cbFreeTotal,
                                                             &cbBalloonedTotal, &cbSharedTotal);
    AssertRCReturn(vrc, E_FAIL);

    *aMemAllocTotal   = (ULONG)(cbAllocTotal / _1K);  /* bytes -> KB */
    *aMemFreeTotal    = (ULONG)(cbFreeTotal / _1K);
    *aMemBalloonTotal = (ULONG)(cbBalloonedTotal / _1K);
    *aMemSharedTotal  = (ULONG)(cbSharedTotal / _1K);

    /* Query the missing per-VM memory statistics. */
    uint64_t cbTotalMemIgn, cbPrivateMemIgn, cbSharedMem, cbZeroMemIgn;
    vrc = ptrVM.vtable()->pfnPGMR3QueryMemoryStats(ptrVM.rawUVM(), &cbTotalMemIgn, &cbPrivateMemIgn, &cbSharedMem, &cbZeroMemIgn);
    AssertRCReturn(vrc, E_FAIL);
    *aMemShared = (ULONG)(cbSharedMem / _1K);

    return S_OK;
}

HRESULT Guest::i_setStatistic(ULONG aCpuId, GUESTSTATTYPE enmType, ULONG aVal)
{
    static ULONG indexToPerfMask[] =
    {
        pm::VMSTATMASK_GUEST_CPUUSER,
        pm::VMSTATMASK_GUEST_CPUKERNEL,
        pm::VMSTATMASK_GUEST_CPUIDLE,
        pm::VMSTATMASK_GUEST_MEMTOTAL,
        pm::VMSTATMASK_GUEST_MEMFREE,
        pm::VMSTATMASK_GUEST_MEMBALLOON,
        pm::VMSTATMASK_GUEST_MEMCACHE,
        pm::VMSTATMASK_GUEST_PAGETOTAL,
        pm::VMSTATMASK_NONE
    };
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (enmType >= GUESTSTATTYPE_MAX)
        return E_INVALIDARG;

    if (aCpuId < VMM_MAX_CPU_COUNT)
    {
        ULONG *paCpuStats;
        switch (enmType)
        {
            case GUESTSTATTYPE_CPUUSER:   paCpuStats = mCurrentGuestCpuUserStat;   break;
            case GUESTSTATTYPE_CPUKERNEL: paCpuStats = mCurrentGuestCpuKernelStat; break;
            case GUESTSTATTYPE_CPUIDLE:   paCpuStats = mCurrentGuestCpuIdleStat;   break;
            default:                      paCpuStats = NULL;                       break;
        }
        if (paCpuStats)
        {
            paCpuStats[aCpuId] = aVal;
            aVal = 0;
            for (uint32_t i = 0; i < mCpus && i < VMM_MAX_CPU_COUNT; i++)
                aVal += paCpuStats[i];
            aVal /= mCpus;
        }
    }

    mCurrentGuestStat[enmType] = aVal;
    mVmValidStats |= indexToPerfMask[enmType];
    return S_OK;
}

/**
 * Returns the status of a specified Guest Additions facility.
 *
 * @return  COM status code
 * @param   aFacility       Facility to get the status from.
 * @param   aTimestamp      Timestamp of last facility status update in ms (optional).
 * @param   aStatus         Current status of the specified facility.
 */
HRESULT Guest::getFacilityStatus(AdditionsFacilityType_T aFacility, LONG64 *aTimestamp, AdditionsFacilityStatus_T *aStatus)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Not checking for aTimestamp is intentional; it's optional. */
    FacilityMapIterConst it = mData.mFacilityMap.find(aFacility);
    if (it != mData.mFacilityMap.end())
    {
        AdditionsFacility *pFacility = it->second;
        ComAssert(pFacility);
        *aStatus = pFacility->i_getStatus();
        if (aTimestamp)
            *aTimestamp = pFacility->i_getLastUpdated();
    }
    else
    {
        /*
         * Do not fail here -- could be that the facility never has been brought up (yet) but
         * the host wants to have its status anyway. So just tell we don't know at this point.
         */
        *aStatus = AdditionsFacilityStatus_Unknown;
        if (aTimestamp)
            *aTimestamp = RTTimeMilliTS();
    }
    return S_OK;
}

HRESULT Guest::getAdditionsStatus(AdditionsRunLevelType_T aLevel, BOOL *aActive)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;
    switch (aLevel)
    {
        case AdditionsRunLevelType_System:
            *aActive = (mData.mAdditionsRunLevel > AdditionsRunLevelType_None);
            break;

        case AdditionsRunLevelType_Userland:
            *aActive = (mData.mAdditionsRunLevel >= AdditionsRunLevelType_Userland);
            break;

        case AdditionsRunLevelType_Desktop:
            *aActive = (mData.mAdditionsRunLevel >= AdditionsRunLevelType_Desktop);
            break;

        default:
            hrc = setError(VBOX_E_NOT_SUPPORTED,
                           tr("Invalid status level defined: %u"), aLevel);
            break;
    }

    return hrc;
}

HRESULT Guest::setCredentials(const com::Utf8Str &aUserName, const com::Utf8Str &aPassword,
                              const com::Utf8Str &aDomain, BOOL aAllowInteractiveLogon)
{
    /* Check for magic domain names which are used to pass encryption keys to the disk. */
    if (Utf8Str(aDomain) == "@@disk")
        return mParent->i_setDiskEncryptionKeys(aPassword);
    if (Utf8Str(aDomain) == "@@mem")
    {
        /** @todo */
        return E_NOTIMPL;
    }

    /* forward the information to the VMM device */
    VMMDev *pVMMDev = mParent->i_getVMMDev();
    if (pVMMDev)
    {
        PPDMIVMMDEVPORT pVMMDevPort = pVMMDev->getVMMDevPort();
        if (pVMMDevPort)
        {
            uint32_t u32Flags = VMMDEV_SETCREDENTIALS_GUESTLOGON;
            if (!aAllowInteractiveLogon)
                u32Flags = VMMDEV_SETCREDENTIALS_NOLOCALLOGON;

            pVMMDevPort->pfnSetCredentials(pVMMDevPort,
                                           aUserName.c_str(),
                                           aPassword.c_str(),
                                           aDomain.c_str(),
                                           u32Flags);
            return S_OK;
        }
    }

    return setError(VBOX_E_VM_ERROR, tr("VMM device is not available (is the VM running?)"));
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 * Sets the general Guest Additions information like
 * API (interface) version and OS type.  Gets called by
 * vmmdevUpdateGuestInfo.
 *
 * @param aInterfaceVersion
 * @param aOsType
 */
void Guest::i_setAdditionsInfo(const com::Utf8Str &aInterfaceVersion, VBOXOSTYPE aOsType)
{
    RTTIMESPEC TimeSpecTS;
    RTTimeNow(&TimeSpecTS);

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Note: The Guest Additions API (interface) version is deprecated
     * and will not be used anymore!  We might need it to at least report
     * something as version number if *really* ancient Guest Additions are
     * installed (without the guest version + revision properties having set).
     */
    mData.mInterfaceVersion = aInterfaceVersion;

    /*
     * Older Additions rely on the Additions API version whether they
     * are assumed to be active or not.  Since newer Additions do report
     * the Additions version *before* calling this function (by calling
     * VMMDevReportGuestInfo2, VMMDevReportGuestStatus, VMMDevReportGuestInfo,
     * in that order) we can tell apart old and new Additions here. Old
     * Additions never would set VMMDevReportGuestInfo2 (which set mData.mAdditionsVersion)
     * so they just rely on the aInterfaceVersion string (which gets set by
     * VMMDevReportGuestInfo).
     *
     * So only mark the Additions as being active (run level = system) when we
     * don't have the Additions version set.
     */
    if (mData.mAdditionsVersionNew.isEmpty())
    {
        if (aInterfaceVersion.isEmpty())
            mData.mAdditionsRunLevel = AdditionsRunLevelType_None;
        else
        {
            mData.mAdditionsRunLevel = AdditionsRunLevelType_System;

            /*
             * To keep it compatible with the old Guest Additions behavior we need to set the
             * "graphics" (feature) facility to active as soon as we got the Guest Additions
             * interface version.
             */
            i_facilityUpdate(VBoxGuestFacilityType_Graphics, VBoxGuestFacilityStatus_Active, 0 /*fFlags*/, &TimeSpecTS);
        }
    }

    /*
     * Older Additions didn't have this finer grained capability bit,
     * so enable it by default. Newer Additions will not enable this here
     * and use the setSupportedFeatures function instead.
     */
    /** @todo r=bird: I don't get the above comment nor the code below...
     * One talks about capability bits, the one always does something to a facility.
     * Then there is the comment below it all, which is placed like it addresses the
     * mOSTypeId, but talks about something which doesn't remotely like mOSTypeId...
     *
     * Andy, could you please try clarify and make the comments shorter and more
     * coherent! Also, explain why this is important and what depends on it.
     *
     * PS. There is the VMMDEV_GUEST_SUPPORTS_GRAPHICS capability* report... It
     * should come in pretty quickly after this update, normally.
     */
    i_facilityUpdate(VBoxGuestFacilityType_Graphics,
                     i_facilityIsActive(VBoxGuestFacilityType_VBoxGuestDriver)
                     ? VBoxGuestFacilityStatus_Active : VBoxGuestFacilityStatus_Inactive,
                     0 /*fFlags*/, &TimeSpecTS); /** @todo the timestamp isn't gonna be right here on saved state restore. */

    /*
     * Note! There is a race going on between setting mAdditionsRunLevel and
     * mSupportsGraphics here and disabling/enabling it later according to
     * its real status when using new(er) Guest Additions.
     */
    mData.mOSType = aOsType;
    mData.mOSTypeId = Global::OSTypeId(aOsType);

    /*
     * Always fire an event here.
     */
    AdditionsRunLevelType_T const enmRunLevel = mData.mAdditionsRunLevel;
    alock.release();
    ::FireGuestAdditionsStatusChangedEvent(mEventSource, AdditionsFacilityType_None, AdditionsFacilityStatus_Active,
                                           enmRunLevel, RTTimeSpecGetMilli(&TimeSpecTS));
}

/**
 * Sets the Guest Additions version information details.
 *
 * Gets called by vmmdevUpdateGuestInfo2 and vmmdevUpdateGuestInfo (to clear the
 * state).
 *
 * @param   a_uFullVersion          VBoxGuestInfo2::additionsMajor,
 *                                  VBoxGuestInfo2::additionsMinor and
 *                                  VBoxGuestInfo2::additionsBuild combined into
 *                                  one value by VBOX_FULL_VERSION_MAKE.
 *
 *                                  When this is 0, it's vmmdevUpdateGuestInfo
 *                                  calling to reset the state.
 *
 * @param   a_pszName               Build type tag and/or publisher tag, empty
 *                                  string if neiter of those are present.
 * @param   a_uRevision             See VBoxGuestInfo2::additionsRevision.
 * @param   a_fFeatures             See VBoxGuestInfo2::additionsFeatures.
 */
void Guest::i_setAdditionsInfo2(uint32_t a_uFullVersion, const char *a_pszName, uint32_t a_uRevision, uint32_t a_fFeatures)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (a_uFullVersion)
    {
        mData.mAdditionsVersionNew  = Utf8StrFmt(*a_pszName ? "%u.%u.%u_%s" : "%u.%u.%u",
                                                 VBOX_FULL_VERSION_GET_MAJOR(a_uFullVersion),
                                                 VBOX_FULL_VERSION_GET_MINOR(a_uFullVersion),
                                                 VBOX_FULL_VERSION_GET_BUILD(a_uFullVersion),
                                                 a_pszName);
        mData.mAdditionsVersionFull = a_uFullVersion;
        mData.mAdditionsRevision    = a_uRevision;
        mData.mAdditionsFeatures    = a_fFeatures;
    }
    else
    {
        Assert(!a_fFeatures && !a_uRevision && !*a_pszName);
        mData.mAdditionsVersionNew.setNull();
        mData.mAdditionsVersionFull = 0;
        mData.mAdditionsRevision    = 0;
        mData.mAdditionsFeatures    = 0;
    }
}

bool Guest::i_facilityIsActive(VBoxGuestFacilityType enmFacility)
{
    Assert(enmFacility < INT32_MAX);
    FacilityMapIterConst it = mData.mFacilityMap.find((AdditionsFacilityType_T)enmFacility);
    if (it != mData.mFacilityMap.end())
    {
        AdditionsFacility *pFac = it->second;
        return (pFac->i_getStatus() == AdditionsFacilityStatus_Active);
    }
    return false;
}

bool Guest::i_facilityUpdate(VBoxGuestFacilityType a_enmFacility, VBoxGuestFacilityStatus a_enmStatus,
                             uint32_t a_fFlags, PCRTTIMESPEC a_pTimeSpecTS)
{
    AssertReturn(   a_enmFacility < VBoxGuestFacilityType_All
                 && a_enmFacility > VBoxGuestFacilityType_Unknown, false);

    bool fChanged;
    FacilityMapIter it = mData.mFacilityMap.find((AdditionsFacilityType_T)a_enmFacility);
    if (it != mData.mFacilityMap.end())
    {
        AdditionsFacility *pFac = it->second;
        fChanged = pFac->i_update((AdditionsFacilityStatus_T)a_enmStatus, a_fFlags, a_pTimeSpecTS);
    }
    else
    {
        if (mData.mFacilityMap.size() > 64)
        {
            /* The easy way out for now. We could automatically destroy
               inactive facilities like VMMDev does if we like... */
            AssertFailedReturn(false);
        }

        ComObjPtr<AdditionsFacility> ptrFac;
        HRESULT hrc = ptrFac.createObject();
        AssertComRCReturn(hrc, false);
        Assert(ptrFac);

        hrc = ptrFac->init(this, (AdditionsFacilityType_T)a_enmFacility, (AdditionsFacilityStatus_T)a_enmStatus,
                           a_fFlags, a_pTimeSpecTS);
        AssertComRCReturn(hrc, false);
        try
        {
            mData.mFacilityMap.insert(std::make_pair((AdditionsFacilityType_T)a_enmFacility, ptrFac));
            fChanged = true;
        }
        catch (std::bad_alloc &)
        {
            fChanged = false;
        }
    }
    return fChanged;
}

/**
 * Issued by the guest when a guest user changed its state.
 *
 * @param   aUser               Guest user name.
 * @param   aDomain             Domain of guest user account. Optional.
 * @param   enmState            New state to indicate.
 * @param   pbDetails           Pointer to state details. Optional.
 * @param   cbDetails           Size (in bytes) of state details. Pass 0 if not used.
 */
void Guest::i_onUserStateChanged(const Utf8Str &aUser, const Utf8Str &aDomain, VBoxGuestUserState enmState,
                                 const uint8_t *pbDetails, uint32_t cbDetails)
{
    RT_NOREF(pbDetails, cbDetails);
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    Utf8Str strDetails; /** @todo Implement state details here. */

    ::FireGuestUserStateChangedEvent(mEventSource, aUser, aDomain, (GuestUserState_T)enmState, strDetails);
    LogFlowFuncLeave();
}

/**
 * Sets the status of a certain Guest Additions facility.
 *
 * Gets called by vmmdevUpdateGuestStatus, which just passes the report along.
 *
 * @param   a_enmFacility       The facility.
 * @param   a_enmStatus         The status.
 * @param   a_fFlags            Flags assoicated with the update. Currently
 *                              reserved and should be ignored.
 * @param   a_pTimeSpecTS       Pointer to the timestamp of this report.
 * @sa      PDMIVMMDEVCONNECTOR::pfnUpdateGuestStatus, vmmdevUpdateGuestStatus
 * @thread  The emulation thread.
 */
void Guest::i_setAdditionsStatus(VBoxGuestFacilityType a_enmFacility, VBoxGuestFacilityStatus a_enmStatus,
                                 uint32_t a_fFlags, PCRTTIMESPEC a_pTimeSpecTS)
{
    Assert(   a_enmFacility > VBoxGuestFacilityType_Unknown
           && a_enmFacility <= VBoxGuestFacilityType_All); /* Paranoia, VMMDev checks for this. */

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Set a specific facility status.
     */
    bool fFireEvent = false;
    if (a_enmFacility == VBoxGuestFacilityType_All)
        for (FacilityMapIter it = mData.mFacilityMap.begin(); it != mData.mFacilityMap.end(); ++it)
            fFireEvent |= i_facilityUpdate((VBoxGuestFacilityType)it->first, a_enmStatus, a_fFlags, a_pTimeSpecTS);
    else /* Update one facility only. */
        fFireEvent = i_facilityUpdate(a_enmFacility, a_enmStatus, a_fFlags, a_pTimeSpecTS);

    /*
     * Recalc the runlevel.
     */
    AdditionsRunLevelType_T const enmOldRunLevel = mData.mAdditionsRunLevel;
    if (i_facilityIsActive(VBoxGuestFacilityType_VBoxTrayClient))
        mData.mAdditionsRunLevel = AdditionsRunLevelType_Desktop;
    else if (i_facilityIsActive(VBoxGuestFacilityType_VBoxService))
        mData.mAdditionsRunLevel = AdditionsRunLevelType_Userland;
    else if (i_facilityIsActive(VBoxGuestFacilityType_VBoxGuestDriver))
        mData.mAdditionsRunLevel = AdditionsRunLevelType_System;
    else
        mData.mAdditionsRunLevel = AdditionsRunLevelType_None;

    /*
     * Fire event if something actually changed.
     */
    AdditionsRunLevelType_T const enmNewRunLevel = mData.mAdditionsRunLevel;
    if (fFireEvent || enmNewRunLevel != enmOldRunLevel)
    {
        alock.release();
        ::FireGuestAdditionsStatusChangedEvent(mEventSource, (AdditionsFacilityType_T)a_enmFacility,
                                               (AdditionsFacilityStatus_T)a_enmStatus, enmNewRunLevel,
                                               RTTimeSpecGetMilli(a_pTimeSpecTS));
    }
}

/**
 * Sets the supported features (and whether they are active or not).
 *
 * @param   aCaps   Guest capability bit mask (VMMDEV_GUEST_SUPPORTS_XXX).
 */
void Guest::i_setSupportedFeatures(uint32_t aCaps)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /** @todo A nit: The timestamp is wrong on saved state restore. Would be better
     *  to move the graphics and seamless capability -> facility translation to
     *  VMMDev so this could be saved.  */
    RTTIMESPEC TimeSpecTS;
    RTTimeNow(&TimeSpecTS);

    bool fFireEvent = i_facilityUpdate(VBoxGuestFacilityType_Seamless,
                                       aCaps & VMMDEV_GUEST_SUPPORTS_SEAMLESS
                                       ? VBoxGuestFacilityStatus_Active : VBoxGuestFacilityStatus_Inactive,
                                       0 /*fFlags*/, &TimeSpecTS);
    /** @todo Add VMMDEV_GUEST_SUPPORTS_GUEST_HOST_WINDOW_MAPPING */

    /*
     * Fire event if the state actually changed.
     */
    if (fFireEvent)
    {
        AdditionsRunLevelType_T const enmRunLevel = mData.mAdditionsRunLevel;
        alock.release();
        ::FireGuestAdditionsStatusChangedEvent(mEventSource, AdditionsFacilityType_Seamless,
                                               aCaps & VMMDEV_GUEST_SUPPORTS_SEAMLESS
                                               ? AdditionsFacilityStatus_Active : AdditionsFacilityStatus_Inactive,
                                               enmRunLevel, RTTimeSpecGetMilli(&TimeSpecTS));
    }
}
