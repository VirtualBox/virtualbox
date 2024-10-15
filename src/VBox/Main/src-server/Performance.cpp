/* $Id: Performance.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Performance Classes implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

/**
 * @todo list:
 *
 * 1) Detection of erroneous metric names
 */

#define LOG_GROUP LOG_GROUP_MAIN_PERFORMANCECOLLECTOR
#ifndef VBOX_COLLECTOR_TEST_CASE
# include "VirtualBoxImpl.h"
# include "MachineImpl.h"
# include "MediumImpl.h"
# include "AutoCaller.h"
#endif
#include "Performance.h"
#include "HostNetworkInterfaceImpl.h"
#include "netif.h"

#include <VBox/com/array.h>
#include <VBox/com/ptr.h>
#include <VBox/com/string.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/cpuset.h>

#include <algorithm>

#include "LoggingNew.h"

using namespace pm;

// Stubs for non-pure virtual methods

int CollectorHAL::getHostCpuLoad(ULONG * /* user */, ULONG * /* kernel */, ULONG * /* idle */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getProcessCpuLoad(RTPROCESS  /* process */, ULONG * /* user */, ULONG * /* kernel */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getRawHostCpuLoad(uint64_t * /* user */, uint64_t * /* kernel */, uint64_t * /* idle */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getRawHostNetworkLoad(const char * /* name */, uint64_t * /* rx */, uint64_t * /* tx */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getRawHostDiskLoad(const char * /* name */, uint64_t * /* disk_ms */, uint64_t * /* total_ms */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getRawProcessCpuLoad(RTPROCESS  /* process */, uint64_t * /* user */,
                                       uint64_t * /* kernel */, uint64_t * /* total */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getHostMemoryUsage(ULONG * /* total */, ULONG * /* used */, ULONG * /* available */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getHostFilesystemUsage(const char * /* name */, ULONG * /* total */, ULONG * /* used */,
                                         ULONG * /* available */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getHostDiskSize(const char * /* name */, uint64_t * /* size */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getProcessMemoryUsage(RTPROCESS /* process */, ULONG * /* used */)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorHAL::getDiskListByFs(const char * /* name */, DiskList& /* listUsage */, DiskList& /* listLoad */)
{
    return VERR_NOT_IMPLEMENTED;
}

/* Generic implementations */

int CollectorHAL::getHostCpuMHz(ULONG *mhz)
{
    unsigned cCpus = 0;
    uint64_t u64TotalMHz = 0;
    RTCPUSET OnlineSet;
    RTMpGetOnlineSet(&OnlineSet);
    for (int iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
    {
        Log7Func(("{%p}: Checking if CPU %d is member of online set...\n", this, (int)iCpu));
        if (RTCpuSetIsMemberByIndex(&OnlineSet, iCpu))
        {
            Log7Func(("{%p}: Getting frequency for CPU %d...\n", this, (int)iCpu));
            uint32_t uMHz = RTMpGetCurFrequency(RTMpCpuIdFromSetIndex(iCpu));
            if (uMHz != 0)
            {
                Log7Func(("{%p}: CPU %d %u MHz\n", this, (int)iCpu, uMHz));
                u64TotalMHz += uMHz;
                cCpus++;
            }
        }
    }

    if (cCpus)
    {
        *mhz = (ULONG)(u64TotalMHz / cCpus);
        return VINF_SUCCESS;
    }

    /* This is always the case on darwin, so don't assert there. */
#ifndef RT_OS_DARWIN
    AssertFailed();
#endif
    *mhz = 0;
    return VERR_NOT_IMPLEMENTED;
}

#ifndef VBOX_COLLECTOR_TEST_CASE

CollectorGuestQueue::CollectorGuestQueue()
{
    mEvent = NIL_RTSEMEVENT;
    RTSemEventCreate(&mEvent);
}

CollectorGuestQueue::~CollectorGuestQueue()
{
    RTSemEventDestroy(mEvent);
}

void CollectorGuestQueue::push(CollectorGuestRequest* rq)
{
    RTCLock lock(mLockMtx);

    mQueue.push(rq);
    RTSemEventSignal(mEvent);
}

CollectorGuestRequest* CollectorGuestQueue::pop()
{
    int vrc = VINF_SUCCESS;
    CollectorGuestRequest *rq = NULL;

    do
    {
        {
            RTCLock lock(mLockMtx);

            if (!mQueue.empty())
            {
                rq = mQueue.front();
                mQueue.pop();
            }
        }

        if (rq)
            return rq;
        vrc = RTSemEventWaitNoResume(mEvent, RT_INDEFINITE_WAIT);
    } while (RT_SUCCESS(vrc));

    return NULL;
}

HRESULT CGRQEnable::execute()
{
    Assert(mCGuest);
    return mCGuest->enableInternal(mMask);
}

void CGRQEnable::debugPrint(void *aObject, const char *aFunction, const char *aText)
{
    NOREF(aObject);
    NOREF(aFunction);
    NOREF(aText);
    Log7((LOG_FN_FMT ": {%p}: CGRQEnable(mask=0x%x) %s\n", aObject, aFunction, mMask, aText));
}

HRESULT CGRQDisable::execute()
{
    Assert(mCGuest);
    return mCGuest->disableInternal(mMask);
}

void CGRQDisable::debugPrint(void *aObject, const char *aFunction, const char *aText)
{
    NOREF(aObject);
    NOREF(aFunction);
    NOREF(aText);
    Log7((LOG_FN_FMT ": {%p}: CGRQDisable(mask=0x%x) %s\n", aObject, aFunction, mMask, aText));
}

HRESULT CGRQAbort::execute()
{
    return E_ABORT;
}

void CGRQAbort::debugPrint(void *aObject, const char *aFunction, const char *aText)
{
    NOREF(aObject);
    NOREF(aFunction);
    NOREF(aText);
    Log7((LOG_FN_FMT ": {%p}: CGRQAbort %s\n", aObject, aFunction, aText));
}

CollectorGuest::CollectorGuest(Machine *machine, RTPROCESS process) :
    mUnregistered(false), mEnabled(false), mValid(false), mMachine(machine), mProcess(process),
    mCpuUser(0), mCpuKernel(0), mCpuIdle(0),
    mMemTotal(0), mMemFree(0), mMemBalloon(0), mMemShared(0), mMemCache(0), mPageTotal(0),
    mAllocVMM(0), mFreeVMM(0), mBalloonedVMM(0), mSharedVMM(0), mVmNetRx(0), mVmNetTx(0)
{
    Assert(mMachine);
    /* cannot use ComObjPtr<Machine> in Performance.h, do it manually */
    mMachine->AddRef();
}

CollectorGuest::~CollectorGuest()
{
    /* cannot use ComObjPtr<Machine> in Performance.h, do it manually */
    mMachine->Release();
    // Assert(!cEnabled); why?
}

HRESULT CollectorGuest::enableVMMStats(bool mCollectVMMStats)
{
    HRESULT hrc = S_OK;

    if (mGuest)
    {
        /** @todo replace this with a direct call to mGuest in trunk! */
        AutoCaller autoCaller(mMachine);
        if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

        ComPtr<IInternalSessionControl> directControl;

        hrc = mMachine->i_getDirectControl(&directControl);
        if (hrc != S_OK)
            return hrc;

        /* enable statistics collection; this is a remote call (!) */
        hrc = directControl->EnableVMMStatistics(mCollectVMMStats);
        Log7Func(("{%p}: %sable VMM stats (%s)\n",
                  this, mCollectVMMStats ? "En" : "Dis", SUCCEEDED(hrc) ? "success" : "failed"));
    }

    return hrc;
}

HRESULT CollectorGuest::enable(ULONG mask)
{
    return enqueueRequest(new CGRQEnable(mask));
}

HRESULT CollectorGuest::disable(ULONG mask)
{
    return enqueueRequest(new CGRQDisable(mask));
}

HRESULT CollectorGuest::enableInternal(ULONG mask)
{
    HRESULT ret = S_OK;

    if ((mEnabled & mask) == mask)
        return E_UNEXPECTED;

    if (!mEnabled)
    {
        /* Must make sure that the machine object does not get uninitialized
         * in the middle of enabling this collector. Causes timing-related
         * behavior otherwise, which we don't want. In particular the
         * GetRemoteConsole call below can hang if the VM didn't completely
         * terminate (the VM processes stop processing events shortly before
         * closing the session). This avoids the hang. */
        AutoCaller autoCaller(mMachine);
        if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

        mMachineName = mMachine->i_getName();

        ComPtr<IInternalSessionControl> directControl;

        ret = mMachine->i_getDirectControl(&directControl);
        if (ret != S_OK)
            return ret;

        /* get the associated console; this is a remote call (!) */
        ret = directControl->COMGETTER(RemoteConsole)(mConsole.asOutParam());
        if (ret != S_OK)
            return ret;

        ret = mConsole->COMGETTER(Guest)(mGuest.asOutParam());
        if (ret == S_OK)
        {
            ret = mGuest->COMSETTER(StatisticsUpdateInterval)(1 /* 1 sec */);
            Log7Func(("{%p}: Set guest statistics update interval to 1 sec (%s)\n",
                  this, SUCCEEDED(ret) ? "success" : "failed"));
        }
    }
    if ((mask & VMSTATS_VMM_RAM) == VMSTATS_VMM_RAM)
        enableVMMStats(true);
    mEnabled |= mask;

    return ret;
}

HRESULT CollectorGuest::disableInternal(ULONG mask)
{
    if (!(mEnabled & mask))
        return E_UNEXPECTED;

    if ((mask & VMSTATS_VMM_RAM) == VMSTATS_VMM_RAM)
        enableVMMStats(false);
    mEnabled &= ~mask;
    if (!mEnabled)
    {
        Assert(mGuest && mConsole);
        HRESULT ret = mGuest->COMSETTER(StatisticsUpdateInterval)(0 /* off */);
        NOREF(ret);
        Log7Func(("{%p}: Set guest statistics update interval to 0 sec (%s)\n",
              this, SUCCEEDED(ret) ? "success" : "failed"));
        invalidate(VMSTATS_ALL);
    }

    return S_OK;
}

HRESULT CollectorGuest::enqueueRequest(CollectorGuestRequest *aRequest)
{
    if (mManager)
    {
        aRequest->setGuest(this);
        return mManager->enqueueRequest(aRequest);
    }

    Log7Func(("{%p}: Attempted enqueue guest request when mManager is null\n", this));
    return E_POINTER;
}

void CollectorGuest::updateStats(ULONG aValidStats, ULONG aCpuUser,
                                 ULONG aCpuKernel, ULONG aCpuIdle,
                                 ULONG aMemTotal, ULONG aMemFree,
                                 ULONG aMemBalloon, ULONG aMemShared,
                                 ULONG aMemCache, ULONG aPageTotal,
                                 ULONG aAllocVMM, ULONG aFreeVMM,
                                 ULONG aBalloonedVMM, ULONG aSharedVMM,
                                 ULONG aVmNetRx, ULONG aVmNetTx)
{
    if ((aValidStats & VMSTATS_GUEST_CPULOAD) == VMSTATS_GUEST_CPULOAD)
    {
        mCpuUser   = aCpuUser;
        mCpuKernel = aCpuKernel,
        mCpuIdle   = aCpuIdle;
    }
    if ((aValidStats & VMSTATS_GUEST_RAMUSAGE) == VMSTATS_GUEST_RAMUSAGE)
    {
        mMemTotal   = aMemTotal;
        mMemFree    = aMemFree;
        mMemBalloon = aMemBalloon;
        mMemShared  = aMemShared;
        mMemCache   = aMemCache;
        mPageTotal  = aPageTotal;
    }
    if ((aValidStats & VMSTATS_VMM_RAM) == VMSTATS_VMM_RAM)
    {
        mAllocVMM     = aAllocVMM;
        mFreeVMM      = aFreeVMM;
        mBalloonedVMM = aBalloonedVMM;
        mSharedVMM    = aSharedVMM;
    }
    if ((aValidStats & VMSTATS_NET_RATE) == VMSTATS_NET_RATE)
    {
        mVmNetRx = aVmNetRx;
        mVmNetTx = aVmNetTx;
    }
    mValid = aValidStats;
}

CollectorGuestManager::CollectorGuestManager()
  : mVMMStatsProvider(NULL), mGuestBeingCalled(NULL)
{
    int vrc = RTThreadCreate(&mThread, CollectorGuestManager::requestProcessingThread,
                            this, 0, RTTHREADTYPE_MAIN_WORKER, RTTHREADFLAGS_WAITABLE,
                            "CGMgr");
    NOREF(vrc);
    Log7Func(("{%p}: RTThreadCreate returned %Rrc (mThread=%p)\n", this, vrc, mThread));
}

CollectorGuestManager::~CollectorGuestManager()
{
    Assert(mGuests.size() == 0);
    HRESULT hrc = enqueueRequest(new CGRQAbort());
    if (SUCCEEDED(hrc))
    {
        /* We wait only if we were able to put the abort request to a queue */
        Log7Func(("{%p}: Waiting for CGM request processing thread to stop...\n", this));
        int vrcThread = VINF_SUCCESS;
        int vrc = RTThreadWait(mThread, 1000 /* 1 sec */, &vrcThread);
        Log7Func(("{%p}: RTThreadWait returned %Rrc (thread exit code: %Rrc)\n", this, vrc, vrcThread));
        RT_NOREF(vrc);
    }
}

void CollectorGuestManager::registerGuest(CollectorGuest* pGuest)
{
    pGuest->setManager(this);
    mGuests.push_back(pGuest);
    /*
     * If no VMM stats provider was elected previously than this is our
     * candidate.
     */
    if (!mVMMStatsProvider)
        mVMMStatsProvider = pGuest;
    Log7Func(("{%p}: Registered guest=%p provider=%p\n", this, pGuest, mVMMStatsProvider));
}

void CollectorGuestManager::unregisterGuest(CollectorGuest* pGuest)
{
    Log7Func(("{%p}: About to unregister guest=%p provider=%p\n", this, pGuest, mVMMStatsProvider));
    //mGuests.remove(pGuest); => destroyUnregistered()
    pGuest->unregister();
    if (pGuest == mVMMStatsProvider)
    {
        /* This was our VMM stats provider, it is time to re-elect */
        CollectorGuestList::iterator it;
        /* Assume that nobody can provide VMM stats */
        mVMMStatsProvider = NULL;

        for (it = mGuests.begin(); it != mGuests.end(); ++it)
        {
            /* Skip unregistered as they are about to be destroyed */
            if ((*it)->isUnregistered())
                continue;

            if ((*it)->isEnabled())
            {
                /* Found the guest already collecting stats, elect it */
                mVMMStatsProvider = *it;
                HRESULT hrc = mVMMStatsProvider->enqueueRequest(new CGRQEnable(VMSTATS_VMM_RAM));
                if (FAILED(hrc))
                {
                    /* This is not a good candidate -- try to find another */
                    mVMMStatsProvider = NULL;
                    continue;
                }
                break;
            }
        }
        if (!mVMMStatsProvider)
        {
            /* If nobody collects stats, take the first registered */
            for (it = mGuests.begin(); it != mGuests.end(); ++it)
            {
                /* Skip unregistered as they are about to be destroyed */
                if ((*it)->isUnregistered())
                    continue;

                mVMMStatsProvider = *it;
                //mVMMStatsProvider->enable(VMSTATS_VMM_RAM);
                HRESULT hrc = mVMMStatsProvider->enqueueRequest(new CGRQEnable(VMSTATS_VMM_RAM));
                if (SUCCEEDED(hrc))
                    break;
                /* This was not a good candidate -- try to find another */
                mVMMStatsProvider = NULL;
            }
        }
    }
    Log7Func(("[%p}: LEAVE new provider=%p\n", this, mVMMStatsProvider));
}

void CollectorGuestManager::destroyUnregistered()
{
    CollectorGuestList::iterator it;

    for (it = mGuests.begin(); it != mGuests.end();)
        if ((*it)->isUnregistered())
        {
            delete *it;
            it = mGuests.erase(it);
            Log7Func(("{%p}: Number of guests after erasing unregistered is %d\n",
                     this, mGuests.size()));
        }
        else
            ++it;
}

HRESULT CollectorGuestManager::enqueueRequest(CollectorGuestRequest *aRequest)
{
#ifdef DEBUG
    aRequest->debugPrint(this, __PRETTY_FUNCTION__, "added to CGM queue");
#endif /* DEBUG */
    /*
     * It is very unlikely that we will get high frequency calls to configure
     * guest metrics collection, so we rely on this fact to detect blocked
     * guests. If the guest has not finished processing the previous request
     * after half a second we consider it blocked.
     */
    if (aRequest->getGuest() && aRequest->getGuest() == mGuestBeingCalled)
    {
        /*
         * Before we can declare a guest blocked we need to wait for a while
         * and then check again as it may never had a chance to process
         * the previous request. Half a second is an eternity for processes
         * and is barely noticable by humans.
         */
        Log7Func(("{%p}: Suspecting %s is stalled. Waiting for .5 sec...\n",
              this, aRequest->getGuest()->getVMName().c_str()));
        RTThreadSleep(500 /* ms */);
        if (aRequest->getGuest() == mGuestBeingCalled) {
            Log7Func(("{%p}: Request processing stalled for %s\n",
                     this, aRequest->getGuest()->getVMName().c_str()));
            /* Request execution got stalled for this guest -- report an error */
            return E_FAIL;
        }
    }
    mQueue.push(aRequest);
    return S_OK;
}

/* static */
DECLCALLBACK(int) CollectorGuestManager::requestProcessingThread(RTTHREAD /* aThread */, void *pvUser)
{
    CollectorGuestRequest *pReq;
    CollectorGuestManager *mgr = static_cast<CollectorGuestManager*>(pvUser);

    HRESULT hrc = S_OK;

    Log7Func(("{%p}: Starting request processing loop...\n", mgr));
    while ((pReq = mgr->mQueue.pop()) != NULL)
    {
#ifdef DEBUG
        pReq->debugPrint(mgr, __PRETTY_FUNCTION__, "is being executed...");
#endif /* DEBUG */
        mgr->mGuestBeingCalled = pReq->getGuest();
        hrc = pReq->execute();
        mgr->mGuestBeingCalled = NULL;
        delete pReq;
        if (hrc == E_ABORT)
            break;
        if (FAILED(hrc))
            Log7Func(("{%p}: request::execute returned %Rhrc\n", mgr, hrc));
    }
    Log7Func(("{%p}: Exiting request processing loop... hrc=%Rhrc\n", mgr, hrc));

    return VINF_SUCCESS;
}


#endif /* !VBOX_COLLECTOR_TEST_CASE */

bool BaseMetric::collectorBeat(uint64_t nowAt)
{
    if (isEnabled())
    {
        if (mLastSampleTaken == 0)
        {
            mLastSampleTaken = nowAt;
            Log4Func(("{%p}: Collecting %s for obj(%p)...\n",
                      this, getName(), (void *)mObject));
            return true;
        }
        /*
         * We use low resolution timers which may fire just a little bit early.
         * We compensate for that by jumping into the future by several
         * milliseconds (see @bugref{6345}).
         */
        if (nowAt - mLastSampleTaken + PM_SAMPLER_PRECISION_MS >= mPeriod * 1000)
        {
            /*
             * We don't want the beat to drift. This is why the timestamp of
             * the last taken sample is not the actual time but the time we
             * should have taken the measurement at.
             */
            mLastSampleTaken += mPeriod * 1000;
            Log4Func(("{%p}: Collecting %s for obj(%p)...\n",
                        this, getName(), (void *)mObject));
            return true;
        }
        Log4Func(("{%p}: Enabled but too early to collect %s for obj(%p)\n",
                 this, getName(), (void *)mObject));
    }
    return false;
}

void HostCpuLoad::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mUser->init(mLength);
    mKernel->init(mLength);
    mIdle->init(mLength);
}

void HostCpuLoad::collect()
{
    ULONG user, kernel, idle;
    int vrc = mHAL->getHostCpuLoad(&user, &kernel, &idle);
    if (RT_SUCCESS(vrc))
    {
        mUser->put(user);
        mKernel->put(kernel);
        mIdle->put(idle);
    }
}

void HostCpuLoadRaw::init(ULONG period, ULONG length)
{
    HostCpuLoad::init(period, length);
    mHAL->getRawHostCpuLoad(&mUserPrev, &mKernelPrev, &mIdlePrev);
}

void HostCpuLoadRaw::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectHostCpuLoad();
}

void HostCpuLoadRaw::collect()
{
    uint64_t user, kernel, idle;
    uint64_t userDiff, kernelDiff, idleDiff, totalDiff;

    int vrc = mHAL->getRawHostCpuLoad(&user, &kernel, &idle);
    if (RT_SUCCESS(vrc))
    {
        userDiff   = user   - mUserPrev;
        kernelDiff = kernel - mKernelPrev;
        idleDiff   = idle   - mIdlePrev;
        totalDiff  = userDiff + kernelDiff + idleDiff;

        if (totalDiff == 0)
        {
            /* This is only possible if none of counters has changed! */
            LogFlowThisFunc(("Impossible! User, kernel and idle raw "
                "counters has not changed since last sample.\n" ));
            mUser->put(0);
            mKernel->put(0);
            mIdle->put(0);
        }
        else
        {
            mUser->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * userDiff / totalDiff));
            mKernel->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * kernelDiff / totalDiff));
            mIdle->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * idleDiff / totalDiff));
        }

        mUserPrev   = user;
        mKernelPrev = kernel;
        mIdlePrev   = idle;
    }
}

#ifndef VBOX_COLLECTOR_TEST_CASE
static bool getLinkSpeed(const char *szShortName, uint32_t *pSpeed)
{
# ifdef VBOX_WITH_HOSTNETIF_API
    NETIFSTATUS enmState = NETIF_S_UNKNOWN;
    int vrc = NetIfGetState(szShortName, &enmState);
    if (RT_FAILURE(vrc))
        return false;
    if (enmState != NETIF_S_UP)
        *pSpeed = 0;
    else
    {
        vrc = NetIfGetLinkSpeed(szShortName, pSpeed);
        if (RT_FAILURE(vrc))
            return false;
    }
    return true;
# else  /* !VBOX_WITH_HOSTNETIF_API */
    RT_NOREF(szShortName, pSpeed);
    return false;
# endif /* VBOX_WITH_HOSTNETIF_API */
}

void HostNetworkSpeed::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mLinkSpeed->init(length);
    /*
     * Retrieve the link speed now as it may be wrong if the metric was
     * registered at boot (see @bugref{6613}).
     */
    getLinkSpeed(mShortName.c_str(), &mSpeed);
}

void HostNetworkLoadRaw::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mRx->init(mLength);
    mTx->init(mLength);
    /*
     * Retrieve the link speed now as it may be wrong if the metric was
     * registered at boot (see @bugref{6613}).
     */
    uint32_t uSpeedMbit = 65535;
    if (getLinkSpeed(mShortName.c_str(), &uSpeedMbit))
        mSpeed = (uint64_t)uSpeedMbit * (1000000/8); /* Convert to bytes/sec */
    /*int vrc =*/ mHAL->getRawHostNetworkLoad(mShortName.c_str(), &mRxPrev, &mTxPrev);
    //AssertRC(vrc);
}

void HostNetworkLoadRaw::preCollect(CollectorHints& /* hints */, uint64_t /* iTick */)
{
    if (RT_FAILURE(mRc))
    {
        ComPtr<IHostNetworkInterface> networkInterface;
        ComPtr<IHost> host = getObject();
        HRESULT hrc = host->FindHostNetworkInterfaceByName(com::Bstr(mInterfaceName).raw(), networkInterface.asOutParam());
        if (SUCCEEDED(hrc))
        {
            static uint32_t s_tsLogRelLast;
            uint32_t tsNow = RTTimeProgramSecTS();
            if (   tsNow < RT_SEC_1HOUR
                || (tsNow - s_tsLogRelLast >= 60))
            {
                s_tsLogRelLast = tsNow;
                LogRel(("Failed to collect network metrics for %s: %Rrc (%d). Max one msg/min.\n", mInterfaceName.c_str(), mRc, mRc));
            }
            mRc = VINF_SUCCESS;
        }
    }
}

void HostNetworkLoadRaw::collect()
{
    uint64_t rx = mRxPrev;
    uint64_t tx = mTxPrev;

    if (RT_UNLIKELY(mSpeed * getPeriod() == 0))
    {
        LogFlowThisFunc(("Check cable for %s! speed=%llu period=%d.\n", mShortName.c_str(), mSpeed, getPeriod()));
        /* We do not collect host network metrics for unplugged interfaces! */
        return;
    }
    mRc = mHAL->getRawHostNetworkLoad(mShortName.c_str(), &rx, &tx);
    if (RT_SUCCESS(mRc))
    {
        uint64_t rxDiff = rx - mRxPrev;
        uint64_t txDiff = tx - mTxPrev;

        mRx->put((ULONG)(PM_NETWORK_LOAD_MULTIPLIER * rxDiff / (mSpeed * getPeriod())));
        mTx->put((ULONG)(PM_NETWORK_LOAD_MULTIPLIER * txDiff / (mSpeed * getPeriod())));

        mRxPrev = rx;
        mTxPrev = tx;
    }
    else
        LogFlowThisFunc(("Failed to collect data: %Rrc (%d)."
                         " Will update the list of interfaces...\n", mRc,mRc));
}
#endif /* !VBOX_COLLECTOR_TEST_CASE */

void HostDiskLoadRaw::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mUtil->init(mLength);
    int vrc = mHAL->getRawHostDiskLoad(mDiskName.c_str(), &mDiskPrev, &mTotalPrev);
    AssertRC(vrc);
}

void HostDiskLoadRaw::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectHostCpuLoad();
}

void HostDiskLoadRaw::collect()
{
    uint64_t disk, total;

    int vrc = mHAL->getRawHostDiskLoad(mDiskName.c_str(), &disk, &total);
    if (RT_SUCCESS(vrc))
    {
        uint64_t diskDiff = disk - mDiskPrev;
        uint64_t totalDiff = total - mTotalPrev;

        if (RT_UNLIKELY(totalDiff == 0))
        {
            Assert(totalDiff);
            LogFlowThisFunc(("Improbable! Less than millisecond passed! Disk=%s\n", mDiskName.c_str()));
            mUtil->put(0);
        }
        else if (diskDiff > totalDiff)
        {
            /*
             * It is possible that the disk spent more time than CPU because
             * CPU measurements are taken during the pre-collect phase. We try
             * to compensate for than by adding the extra to the next round of
             * measurements.
             */
            mUtil->put(PM_NETWORK_LOAD_MULTIPLIER);
            Assert((diskDiff - totalDiff) < mPeriod * 1000);
            if ((diskDiff - totalDiff) > mPeriod * 1000)
            {
                LogRel(("Disk utilization time exceeds CPU time by more"
                        " than the collection period (%llu ms)\n", diskDiff - totalDiff));
            }
            else
            {
                disk = mDiskPrev + totalDiff;
                LogFlowThisFunc(("Moved %u milliseconds to the next period.\n", (unsigned)(diskDiff - totalDiff)));
            }
        }
        else
        {
            mUtil->put((ULONG)(PM_NETWORK_LOAD_MULTIPLIER * diskDiff / totalDiff));
        }

        mDiskPrev = disk;
        mTotalPrev = total;
    }
    else
        LogFlowThisFunc(("Failed to collect data: %Rrc (%d)\n", vrc, vrc));
}

void HostCpuMhz::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mMHz->init(mLength);
}

void HostCpuMhz::collect()
{
    ULONG mhz;
    int vrc = mHAL->getHostCpuMHz(&mhz);
    if (RT_SUCCESS(vrc))
        mMHz->put(mhz);
}

void HostRamUsage::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mTotal->init(mLength);
    mUsed->init(mLength);
    mAvailable->init(mLength);
}

void HostRamUsage::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectHostRamUsage();
}

void HostRamUsage::collect()
{
    ULONG total, used, available;
    int vrc = mHAL->getHostMemoryUsage(&total, &used, &available);
    if (RT_SUCCESS(vrc))
    {
        mTotal->put(total);
        mUsed->put(used);
        mAvailable->put(available);
    }
}

void HostFilesystemUsage::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mTotal->init(mLength);
    mUsed->init(mLength);
    mAvailable->init(mLength);
}

void HostFilesystemUsage::preCollect(CollectorHints& /* hints */, uint64_t /* iTick */)
{
}

void HostFilesystemUsage::collect()
{
    ULONG total, used, available;
    int vrc = mHAL->getHostFilesystemUsage(mFsName.c_str(), &total, &used, &available);
    if (RT_SUCCESS(vrc))
    {
        mTotal->put(total);
        mUsed->put(used);
        mAvailable->put(available);
    }
}

void HostDiskUsage::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mTotal->init(mLength);
}

void HostDiskUsage::preCollect(CollectorHints& /* hints */, uint64_t /* iTick */)
{
}

void HostDiskUsage::collect()
{
    uint64_t total;
    int vrc = mHAL->getHostDiskSize(mDiskName.c_str(), &total);
    if (RT_SUCCESS(vrc))
        mTotal->put((ULONG)(total / _1M));
}

#ifndef VBOX_COLLECTOR_TEST_CASE

void HostRamVmm::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mAllocVMM->init(mLength);
    mFreeVMM->init(mLength);
    mBalloonVMM->init(mLength);
    mSharedVMM->init(mLength);
}

HRESULT HostRamVmm::enable()
{
    HRESULT hrc = S_OK;
    CollectorGuest *provider = mCollectorGuestManager->getVMMStatsProvider();
    if (provider)
        hrc = provider->enable(VMSTATS_VMM_RAM);
    BaseMetric::enable();
    return hrc;
}

HRESULT HostRamVmm::disable()
{
    HRESULT hrc = S_OK;
    BaseMetric::disable();
    CollectorGuest *provider = mCollectorGuestManager->getVMMStatsProvider();
    if (provider)
        hrc = provider->disable(VMSTATS_VMM_RAM);
    return hrc;
}

void HostRamVmm::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectHostRamVmm();
}

void HostRamVmm::collect()
{
    CollectorGuest *provider = mCollectorGuestManager->getVMMStatsProvider();
    if (provider)
    {
        Log7Func(("{%p}: provider=%p enabled=%RTbool valid=%RTbool...\n",
              this, provider, provider->isEnabled(), provider->isValid(VMSTATS_VMM_RAM) ));
        if (provider->isValid(VMSTATS_VMM_RAM))
        {
            /* Provider is ready, get updated stats */
            mAllocCurrent     = provider->getAllocVMM();
            mFreeCurrent      = provider->getFreeVMM();
            mBalloonedCurrent = provider->getBalloonedVMM();
            mSharedCurrent    = provider->getSharedVMM();
            provider->invalidate(VMSTATS_VMM_RAM);
        }
        /*
         * Note that if there are no new values from the provider we will use
         * the ones most recently provided instead of zeros, which is probably
         * a desirable behavior.
         */
    }
    else
    {
        mAllocCurrent     = 0;
        mFreeCurrent      = 0;
        mBalloonedCurrent = 0;
        mSharedCurrent    = 0;
    }
    Log7Func(("{%p}: mAllocCurrent=%u mFreeCurrent=%u mBalloonedCurrent=%u mSharedCurrent=%u\n",
             this, mAllocCurrent, mFreeCurrent, mBalloonedCurrent, mSharedCurrent));
    mAllocVMM->put(mAllocCurrent);
    mFreeVMM->put(mFreeCurrent);
    mBalloonVMM->put(mBalloonedCurrent);
    mSharedVMM->put(mSharedCurrent);
}

#endif /* !VBOX_COLLECTOR_TEST_CASE */



void MachineCpuLoad::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mUser->init(mLength);
    mKernel->init(mLength);
}

void MachineCpuLoad::collect()
{
    ULONG user, kernel;
    int vrc = mHAL->getProcessCpuLoad(mProcess, &user, &kernel);
    if (RT_SUCCESS(vrc))
    {
        mUser->put(user);
        mKernel->put(kernel);
    }
}

void MachineCpuLoadRaw::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectProcessCpuLoad(mProcess);
}

void MachineCpuLoadRaw::collect()
{
    uint64_t processUser, processKernel, hostTotal;

    int vrc = mHAL->getRawProcessCpuLoad(mProcess, &processUser, &processKernel, &hostTotal);
    if (RT_SUCCESS(vrc))
    {
        if (hostTotal == mHostTotalPrev)
        {
            /* Nearly impossible, but... */
            mUser->put(0);
            mKernel->put(0);
        }
        else
        {
            mUser->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * (processUser - mProcessUserPrev) / (hostTotal - mHostTotalPrev)));
            mKernel->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * (processKernel - mProcessKernelPrev ) / (hostTotal - mHostTotalPrev)));
        }

        mHostTotalPrev     = hostTotal;
        mProcessUserPrev   = processUser;
        mProcessKernelPrev = processKernel;
    }
}

void MachineRamUsage::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mUsed->init(mLength);
}

void MachineRamUsage::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectProcessRamUsage(mProcess);
}

void MachineRamUsage::collect()
{
    ULONG used;
    int vrc = mHAL->getProcessMemoryUsage(mProcess, &used);
    if (RT_SUCCESS(vrc))
        mUsed->put(used);
}


#ifndef VBOX_COLLECTOR_TEST_CASE

void MachineDiskUsage::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;
    mUsed->init(mLength);
}

void MachineDiskUsage::preCollect(CollectorHints& /* hints */, uint64_t /* iTick */)
{
}

void MachineDiskUsage::collect()
{
    ULONG used = 0;

    for (MediaList::iterator it = mDisks.begin(); it != mDisks.end(); ++it)
    {
        ComObjPtr<Medium> pMedium = *it;

        /* just in case */
        AssertContinue(!pMedium.isNull());

        AutoCaller localAutoCaller(pMedium);
        if (FAILED(localAutoCaller.hrc())) continue;

        AutoReadLock local_alock(pMedium COMMA_LOCKVAL_SRC_POS);

        used += (ULONG)(pMedium->i_getSize() / _1M);
    }

    mUsed->put(used);
}

void MachineNetRate::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;

    mRx->init(mLength);
    mTx->init(mLength);
}

void MachineNetRate::collect()
{
    if (mCGuest->isValid(VMSTATS_NET_RATE))
    {
        mRx->put(mCGuest->getVmNetRx());
        mTx->put(mCGuest->getVmNetTx());
        mCGuest->invalidate(VMSTATS_NET_RATE);
    }
}

HRESULT MachineNetRate::enable()
{
    HRESULT hrc = mCGuest->enable(VMSTATS_NET_RATE);
    BaseMetric::enable();
    return hrc;
}

HRESULT MachineNetRate::disable()
{
    BaseMetric::disable();
    return mCGuest->disable(VMSTATS_NET_RATE);
}

void MachineNetRate::preCollect(CollectorHints& hints,  uint64_t /* iTick */)
{
    hints.collectGuestStats(mCGuest->getProcess());
}

void GuestCpuLoad::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;

    mUser->init(mLength);
    mKernel->init(mLength);
    mIdle->init(mLength);
}

void GuestCpuLoad::preCollect(CollectorHints& hints, uint64_t /* iTick */)
{
    hints.collectGuestStats(mCGuest->getProcess());
}

void GuestCpuLoad::collect()
{
    if (mCGuest->isValid(VMSTATS_GUEST_CPULOAD))
    {
        mUser->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * mCGuest->getCpuUser()) / 100);
        mKernel->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * mCGuest->getCpuKernel()) / 100);
        mIdle->put((ULONG)(PM_CPU_LOAD_MULTIPLIER * mCGuest->getCpuIdle()) / 100);
        mCGuest->invalidate(VMSTATS_GUEST_CPULOAD);
    }
}

HRESULT GuestCpuLoad::enable()
{
    HRESULT hrc = mCGuest->enable(VMSTATS_GUEST_CPULOAD);
    BaseMetric::enable();
    return hrc;
}

HRESULT GuestCpuLoad::disable()
{
    BaseMetric::disable();
    return mCGuest->disable(VMSTATS_GUEST_CPULOAD);
}

void GuestRamUsage::init(ULONG period, ULONG length)
{
    mPeriod = period;
    mLength = length;

    mTotal->init(mLength);
    mFree->init(mLength);
    mBallooned->init(mLength);
    mShared->init(mLength);
    mCache->init(mLength);
    mPagedTotal->init(mLength);
}

void GuestRamUsage::collect()
{
    if (mCGuest->isValid(VMSTATS_GUEST_RAMUSAGE))
    {
        mTotal->put(mCGuest->getMemTotal());
        mFree->put(mCGuest->getMemFree());
        mBallooned->put(mCGuest->getMemBalloon());
        mShared->put(mCGuest->getMemShared());
        mCache->put(mCGuest->getMemCache());
        mPagedTotal->put(mCGuest->getPageTotal());
        mCGuest->invalidate(VMSTATS_GUEST_RAMUSAGE);
    }
}

HRESULT GuestRamUsage::enable()
{
    HRESULT hrc = mCGuest->enable(VMSTATS_GUEST_RAMUSAGE);
    BaseMetric::enable();
    return hrc;
}

HRESULT GuestRamUsage::disable()
{
    BaseMetric::disable();
    return mCGuest->disable(VMSTATS_GUEST_RAMUSAGE);
}

void GuestRamUsage::preCollect(CollectorHints& hints,  uint64_t /* iTick */)
{
    hints.collectGuestStats(mCGuest->getProcess());
}

#endif /* !VBOX_COLLECTOR_TEST_CASE */

void CircularBuffer::init(ULONG ulLength)
{
    if (mData)
        RTMemFree(mData);
    mLength = ulLength;
    if (mLength)
        mData = (ULONG*)RTMemAllocZ(ulLength * sizeof(ULONG));
    else
        mData = NULL;
    mWrapped = false;
    mEnd = 0;
    mSequenceNumber = 0;
}

ULONG CircularBuffer::length()
{
    return mWrapped ? mLength : mEnd;
}

void CircularBuffer::put(ULONG value)
{
    if (mData)
    {
        mData[mEnd++] = value;
        if (mEnd >= mLength)
        {
            mEnd = 0;
            mWrapped = true;
        }
        ++mSequenceNumber;
    }
}

void CircularBuffer::copyTo(ULONG *data)
{
    if (mWrapped)
    {
        memcpy(data, mData + mEnd, (mLength - mEnd) * sizeof(ULONG));
        // Copy the wrapped part
        if (mEnd)
            memcpy(data + (mLength - mEnd), mData, mEnd * sizeof(ULONG));
    }
    else
        memcpy(data, mData, mEnd * sizeof(ULONG));
}

void SubMetric::query(ULONG *data)
{
    copyTo(data);
}

void Metric::query(ULONG **data, ULONG *count, ULONG *sequenceNumber)
{
    ULONG length;
    ULONG *tmpData;

    length = mSubMetric->length();
    *sequenceNumber = mSubMetric->getSequenceNumber() - length;
    if (length)
    {
        tmpData = (ULONG*)RTMemAlloc(sizeof(*tmpData)*length);
        mSubMetric->query(tmpData);
        if (mAggregate)
        {
            *count = 1;
            *data  = (ULONG*)RTMemAlloc(sizeof(**data));
            **data = mAggregate->compute(tmpData, length);
            RTMemFree(tmpData);
        }
        else
        {
            *count = length;
            *data  = tmpData;
        }
    }
    else
    {
        *count = 0;
        *data  = 0;
    }
}

ULONG AggregateAvg::compute(ULONG *data, ULONG length)
{
    uint64_t tmp = 0;
    for (ULONG i = 0; i < length; ++i)
        tmp += data[i];
    return (ULONG)(tmp / length);
}

const char * AggregateAvg::getName()
{
    return "avg";
}

ULONG AggregateMin::compute(ULONG *data, ULONG length)
{
    ULONG tmp = *data;
    for (ULONG i = 0; i < length; ++i)
        if (data[i] < tmp)
            tmp = data[i];
    return tmp;
}

const char * AggregateMin::getName()
{
    return "min";
}

ULONG AggregateMax::compute(ULONG *data, ULONG length)
{
    ULONG tmp = *data;
    for (ULONG i = 0; i < length; ++i)
        if (data[i] > tmp)
            tmp = data[i];
    return tmp;
}

const char * AggregateMax::getName()
{
    return "max";
}

Filter::Filter(const std::vector<com::Utf8Str> &metricNames,
               const std::vector<ComPtr<IUnknown> > &objects)
{
    if (!objects.size())
    {
        if (metricNames.size())
        {
            for (size_t i = 0; i < metricNames.size(); ++i)
                processMetricList(metricNames[i], ComPtr<IUnknown>());
        }
        else
            processMetricList("*", ComPtr<IUnknown>());
    }
    else
    {
        for (size_t i = 0; i < objects.size(); ++i)
            switch (metricNames.size())
            {
                case 0:
                    processMetricList("*", objects[i]);
                    break;
                case 1:
                    processMetricList(metricNames[0], objects[i]);
                    break;
                default:
                    processMetricList(metricNames[i], objects[i]);
                    break;
            }
    }
}

Filter::Filter(const com::Utf8Str &name, const ComPtr<IUnknown> &aObject)
{
    processMetricList(name, aObject);
}

void Filter::processMetricList(const com::Utf8Str &name, const ComPtr<IUnknown> object)
{
    size_t startPos = 0;

    for (size_t pos = name.find(",");
         pos != com::Utf8Str::npos;
         pos = name.find(",", startPos))
    {
        mElements.push_back(std::make_pair(object, RTCString(name.substr(startPos, pos - startPos).c_str())));
        startPos = pos + 1;
    }
    mElements.push_back(std::make_pair(object, RTCString(name.substr(startPos).c_str())));
}

/**
 * The following method was borrowed from stamR3Match (VMM/STAM.cpp) and
 * modified to handle the special case of trailing colon in the pattern.
 *
 * @returns True if matches, false if not.
 * @param   pszPat      Pattern.
 * @param   pszName     Name to match against the pattern.
 * @param   fSeenColon  Seen colon (':').
 */
bool Filter::patternMatch(const char *pszPat, const char *pszName,
                          bool fSeenColon)
{
    /* ASSUMES ASCII */
    for (;;)
    {
        char chPat = *pszPat;
        switch (chPat)
        {
            default:
                if (*pszName != chPat)
                    return false;
                break;

            case '*':
            {
                while ((chPat = *++pszPat) == '*' || chPat == '?')
                    /* nothing */;

                /* Handle a special case, the mask terminating with a colon. */
                if (chPat == ':')
                {
                    if (!fSeenColon && !pszPat[1])
                        return !strchr(pszName, ':');
                    fSeenColon = true;
                }

                for (;;)
                {
                    char ch = *pszName++;
                    if (    ch == chPat
                        &&  (   !chPat
                             || patternMatch(pszPat + 1, pszName, fSeenColon)))
                        return true;
                    if (!ch)
                        return false;
                }
                /* won't ever get here */
                break;
            }

            case '?':
                if (!*pszName)
                    return false;
                break;

            /* Handle a special case, the mask terminating with a colon. */
            case ':':
                if (!fSeenColon && !pszPat[1])
                    return !*pszName;
                if (*pszName != ':')
                    return false;
                fSeenColon = true;
                break;

            case '\0':
                return !*pszName;
        }
        pszName++;
        pszPat++;
    }
    /* not reached */
}

bool Filter::match(const ComPtr<IUnknown> object, const RTCString &name) const
{
    ElementList::const_iterator it;

    //Log7(("Filter::match(%p, %s)\n", static_cast<const IUnknown*> (object), name.c_str()));
    for (it = mElements.begin(); it != mElements.end(); ++it)
    {
        //Log7(("...matching against(%p, %s)\n", static_cast<const IUnknown*> ((*it).first), (*it).second.c_str()));
        if ((*it).first.isNull() || (*it).first == object)
        {
            // Objects match, compare names
            if (patternMatch((*it).second.c_str(), name.c_str()))
            {
                //LogFlowThisFunc(("...found!\n"));
                return true;
            }
        }
    }
    //Log7(("...no matches!\n"));
    return false;
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
