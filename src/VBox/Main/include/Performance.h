/* $Id: Performance.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Performance Classes declaration.
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

#ifndef MAIN_INCLUDED_Performance_h
#define MAIN_INCLUDED_Performance_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/defs.h>
#include <VBox/com/ptr.h>
#include <VBox/com/string.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/types.h>
#include <iprt/err.h>
#include <iprt/cpp/lock.h>

#include <algorithm>
#include <functional> /* For std::fun_ptr in testcase */
#include <list>
#include <vector>
#include <queue>

#include "MediumImpl.h"

/* Forward decl. */
class Machine;

namespace pm
{
    /* CPU load is measured in 1/1000 of per cent. */
    const uint64_t PM_CPU_LOAD_MULTIPLIER = UINT64_C(100000);
    /* Network load is measured in 1/1000 of per cent. */
    const uint64_t PM_NETWORK_LOAD_MULTIPLIER = UINT64_C(100000);
    /* Disk load is measured in 1/1000 of per cent. */
    const uint64_t PM_DISK_LOAD_MULTIPLIER = UINT64_C(100000);
    /* Sampler precision in milliseconds. */
    const uint64_t PM_SAMPLER_PRECISION_MS = 50;

    /* Sub Metrics **********************************************************/
    class CircularBuffer
    {
    public:
        CircularBuffer() : mData(0), mLength(0), mEnd(0), mWrapped(false) {};
        ~CircularBuffer() { if (mData) RTMemFree(mData); };
        void init(ULONG length);
        ULONG length();
        ULONG getSequenceNumber() { return mSequenceNumber; }
        void put(ULONG value);
        void copyTo(ULONG *data);
    private:
        ULONG *mData;
        ULONG  mLength;
        ULONG  mEnd;
        ULONG  mSequenceNumber;
        bool   mWrapped;
    };

    class SubMetric : public CircularBuffer
    {
    public:
        SubMetric(com::Utf8Str name, const char *description)
        : mName(name), mDescription(description) {};
        void query(ULONG *data);
        const char *getName() { return mName.c_str(); };
        const char *getDescription() { return mDescription; };
    private:
        const com::Utf8Str mName;
        const char *mDescription;
    };


    enum {
        COLLECT_NONE        = 0x0,
        COLLECT_CPU_LOAD    = 0x1,
        COLLECT_RAM_USAGE   = 0x2,
        COLLECT_GUEST_STATS = 0x4
    };
    typedef int HintFlags;
    typedef std::pair<RTPROCESS, HintFlags> ProcessFlagsPair;

    class CollectorHints
    {
    public:
        typedef std::list<ProcessFlagsPair> ProcessList;

        CollectorHints() : mHostFlags(COLLECT_NONE) {}
        void collectHostCpuLoad()
            { mHostFlags |= COLLECT_CPU_LOAD; }
        void collectHostRamUsage()
            { mHostFlags |= COLLECT_RAM_USAGE; }
        void collectHostRamVmm()
            { mHostFlags |= COLLECT_GUEST_STATS; }
        void collectProcessCpuLoad(RTPROCESS process)
            { findProcess(process).second |= COLLECT_CPU_LOAD; }
        void collectProcessRamUsage(RTPROCESS process)
            { findProcess(process).second |= COLLECT_RAM_USAGE; }
        void collectGuestStats(RTPROCESS process)
            { findProcess(process).second |= COLLECT_GUEST_STATS; }
        bool isHostCpuLoadCollected() const
            { return (mHostFlags & COLLECT_CPU_LOAD) != 0; }
        bool isHostRamUsageCollected() const
            { return (mHostFlags & COLLECT_RAM_USAGE) != 0; }
        bool isHostRamVmmCollected() const
            { return (mHostFlags & COLLECT_GUEST_STATS) != 0; }
        bool isProcessCpuLoadCollected(RTPROCESS process)
            { return (findProcess(process).second & COLLECT_CPU_LOAD) != 0; }
        bool isProcessRamUsageCollected(RTPROCESS process)
            { return (findProcess(process).second & COLLECT_RAM_USAGE) != 0; }
        bool isGuestStatsCollected(RTPROCESS process)
            { return (findProcess(process).second & COLLECT_GUEST_STATS) != 0; }
        void getProcesses(std::vector<RTPROCESS>& processes) const
        {
            processes.clear();
            processes.reserve(mProcesses.size());
            for (ProcessList::const_iterator it = mProcesses.begin(); it != mProcesses.end(); ++it)
                processes.push_back(it->first);
        }
        const ProcessList& getProcessFlags() const
        {
            return mProcesses;
        }
    private:
        HintFlags   mHostFlags;
        ProcessList mProcesses;

        ProcessFlagsPair& findProcess(RTPROCESS process)
        {
            ProcessList::iterator it;
            for (it = mProcesses.begin(); it != mProcesses.end(); ++it)
                if (it->first == process)
                    return *it;

            /* Not found -- add new */
            mProcesses.push_back(ProcessFlagsPair(process, COLLECT_NONE));
            return mProcesses.back();
        }
    };

    /* Guest Collector Classes  *********************************/
    /*
     * WARNING! The bits in the following masks must correspond to parameters
     * of CollectorGuest::updateStats().
     */
    typedef enum
    {
        VMSTATMASK_NONE             = 0x00000000,
        VMSTATMASK_GUEST_CPUUSER    = 0x00000001,
        VMSTATMASK_GUEST_CPUKERNEL  = 0x00000002,
        VMSTATMASK_GUEST_CPUIDLE    = 0x00000004,
        VMSTATMASK_GUEST_MEMTOTAL   = 0x00000008,
        VMSTATMASK_GUEST_MEMFREE    = 0x00000010,
        VMSTATMASK_GUEST_MEMBALLOON = 0x00000020,
        VMSTATMASK_GUEST_MEMSHARED  = 0x00000040,
        VMSTATMASK_GUEST_MEMCACHE   = 0x00000080,
        VMSTATMASK_GUEST_PAGETOTAL  = 0x00000100,
        VMSTATMASK_VMM_ALLOC        = 0x00010000,
        VMSTATMASK_VMM_FREE         = 0x00020000,
        VMSTATMASK_VMM_BALOON       = 0x00040000,
        VMSTATMASK_VMM_SHARED       = 0x00080000,
        VMSTATMASK_NET_RX           = 0x01000000,
        VMSTATMASK_NET_TX           = 0x02000000
    } VMSTATMASK;

    const ULONG VMSTATS_GUEST_CPULOAD =
        VMSTATMASK_GUEST_CPUUSER    | VMSTATMASK_GUEST_CPUKERNEL |
        VMSTATMASK_GUEST_CPUIDLE;
    const ULONG VMSTATS_GUEST_RAMUSAGE =
        VMSTATMASK_GUEST_MEMTOTAL   | VMSTATMASK_GUEST_MEMFREE |
        VMSTATMASK_GUEST_MEMBALLOON | VMSTATMASK_GUEST_MEMSHARED |
        VMSTATMASK_GUEST_MEMCACHE   | VMSTATMASK_GUEST_PAGETOTAL;
    const ULONG VMSTATS_VMM_RAM =
        VMSTATMASK_VMM_ALLOC        | VMSTATMASK_VMM_FREE|
        VMSTATMASK_VMM_BALOON       | VMSTATMASK_VMM_SHARED;
    const ULONG VMSTATS_NET_RATE =
        VMSTATMASK_NET_RX           | VMSTATMASK_NET_TX;
    const ULONG VMSTATS_ALL =
        VMSTATS_GUEST_CPULOAD       | VMSTATS_GUEST_RAMUSAGE |
        VMSTATS_VMM_RAM             | VMSTATS_NET_RATE;
    class CollectorGuest;

    class CollectorGuestRequest
    {
    public:
        CollectorGuestRequest()
            : mCGuest(0) {};
        virtual ~CollectorGuestRequest() {};
        void setGuest(CollectorGuest *aGuest) { mCGuest = aGuest; };
        CollectorGuest *getGuest() { return mCGuest; };
        virtual HRESULT execute() = 0;

        virtual void debugPrint(void *aObject, const char *aFunction, const char *aText) = 0;
    protected:
        CollectorGuest *mCGuest;
        const char *mDebugName;
    };

    class CGRQEnable : public CollectorGuestRequest
    {
    public:
        CGRQEnable(ULONG aMask)
            : mMask(aMask) {};
        HRESULT execute();

        void debugPrint(void *aObject, const char *aFunction, const char *aText);
    private:
        ULONG mMask;
    };

    class CGRQDisable : public CollectorGuestRequest
    {
    public:
        CGRQDisable(ULONG aMask)
            : mMask(aMask) {};
        HRESULT execute();

        void debugPrint(void *aObject, const char *aFunction, const char *aText);
    private:
        ULONG mMask;
    };

    class CGRQAbort : public CollectorGuestRequest
    {
    public:
        CGRQAbort() {};
        HRESULT execute();

        void debugPrint(void *aObject, const char *aFunction, const char *aText);
    };

    class CollectorGuestQueue
    {
    public:
        CollectorGuestQueue();
        ~CollectorGuestQueue();
        void push(CollectorGuestRequest* rq);
        CollectorGuestRequest* pop();
    private:
        RTCLockMtx mLockMtx;
        RTSEMEVENT mEvent;
        std::queue<CollectorGuestRequest*> mQueue;
    };

    class CollectorGuestManager;

    class CollectorGuest
    {
    public:
        CollectorGuest(Machine *machine, RTPROCESS process);
        ~CollectorGuest();

        void setManager(CollectorGuestManager *aManager)
                                    { mManager = aManager; };
        bool isUnregistered()       { return mUnregistered; };
        bool isEnabled()            { return mEnabled != 0; };
        bool isValid(ULONG mask)    { return (mValid & mask) == mask; };
        void invalidate(ULONG mask) { mValid &= ~mask; };
        void unregister()           { mUnregistered = true; };
        void updateStats(ULONG aValidStats, ULONG aCpuUser,
                         ULONG aCpuKernel, ULONG aCpuIdle,
                         ULONG aMemTotal, ULONG aMemFree,
                         ULONG aMemBalloon, ULONG aMemShared,
                         ULONG aMemCache, ULONG aPageTotal,
                         ULONG aAllocVMM, ULONG aFreeVMM,
                         ULONG aBalloonedVMM, ULONG aSharedVMM,
                         ULONG aVmNetRx, ULONG aVmNetTx);
        HRESULT enable(ULONG mask);
        HRESULT disable(ULONG mask);

        HRESULT enqueueRequest(CollectorGuestRequest *aRequest);
        HRESULT enableInternal(ULONG mask);
        HRESULT disableInternal(ULONG mask);

        const com::Utf8Str& getVMName() const { return mMachineName; };

        RTPROCESS getProcess()  { return mProcess; };
        ULONG getCpuUser()      { return mCpuUser; };
        ULONG getCpuKernel()    { return mCpuKernel; };
        ULONG getCpuIdle()      { return mCpuIdle; };
        ULONG getMemTotal()     { return mMemTotal; };
        ULONG getMemFree()      { return mMemFree; };
        ULONG getMemBalloon()   { return mMemBalloon; };
        ULONG getMemShared()    { return mMemShared; };
        ULONG getMemCache()     { return mMemCache; };
        ULONG getPageTotal()    { return mPageTotal; };
        ULONG getAllocVMM()     { return mAllocVMM; };
        ULONG getFreeVMM()      { return mFreeVMM; };
        ULONG getBalloonedVMM() { return mBalloonedVMM; };
        ULONG getSharedVMM()    { return mSharedVMM; };
        ULONG getVmNetRx()      { return mVmNetRx; };
        ULONG getVmNetTx()      { return mVmNetTx; };

    private:
        HRESULT enableVMMStats(bool mCollectVMMStats);

        CollectorGuestManager *mManager;

        bool                 mUnregistered;
        ULONG                mEnabled;
        ULONG                mValid;
        Machine             *mMachine;
        com::Utf8Str         mMachineName;
        RTPROCESS            mProcess;
        ComPtr<IConsole>     mConsole;
        ComPtr<IGuest>       mGuest;
        ULONG                mCpuUser;
        ULONG                mCpuKernel;
        ULONG                mCpuIdle;
        ULONG                mMemTotal;
        ULONG                mMemFree;
        ULONG                mMemBalloon;
        ULONG                mMemShared;
        ULONG                mMemCache;
        ULONG                mPageTotal;
        ULONG                mAllocVMM;
        ULONG                mFreeVMM;
        ULONG                mBalloonedVMM;
        ULONG                mSharedVMM;
        ULONG                mVmNetRx;
        ULONG                mVmNetTx;
    };

    typedef std::list<CollectorGuest*> CollectorGuestList;
    class CollectorGuestManager
    {
    public:
        CollectorGuestManager();
        ~CollectorGuestManager();
        void registerGuest(CollectorGuest* pGuest);
        void unregisterGuest(CollectorGuest* pGuest);
        CollectorGuest *getVMMStatsProvider() { return mVMMStatsProvider; };
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void destroyUnregistered();
        HRESULT enqueueRequest(CollectorGuestRequest *aRequest);

        CollectorGuest *getBlockedGuest() { return mGuestBeingCalled; };

        static DECLCALLBACK(int) requestProcessingThread(RTTHREAD aThread, void *pvUser);
    private:
        RTTHREAD            mThread;
        CollectorGuestList  mGuests;
        CollectorGuest     *mVMMStatsProvider;
        CollectorGuestQueue mQueue;
        CollectorGuest     *mGuestBeingCalled;
    };

    /* Collector Hardware Abstraction Layer *********************************/
    typedef std::list<RTCString> DiskList;

    class CollectorHAL
    {
    public:
                 CollectorHAL() {};
        virtual ~CollectorHAL() { };
        virtual int preCollect(const CollectorHints& /* hints */, uint64_t /* iTick */) { return VINF_SUCCESS; }
        /** Returns averaged CPU usage in 1/1000th per cent across all host's CPUs. */
        virtual int getHostCpuLoad(ULONG *user, ULONG *kernel, ULONG *idle);
        /** Returns the average frequency in MHz across all host's CPUs. */
        virtual int getHostCpuMHz(ULONG *mhz);
        /** Returns the amount of physical memory in kilobytes. */
        virtual int getHostMemoryUsage(ULONG *total, ULONG *used, ULONG *available);
        /** Returns file system counters in megabytes. */
        virtual int getHostFilesystemUsage(const char *name, ULONG *total, ULONG *used, ULONG *available);
        /** Returns disk size in bytes. */
        virtual int getHostDiskSize(const char *name, uint64_t *size);
        /** Returns CPU usage in 1/1000th per cent by a particular process. */
        virtual int getProcessCpuLoad(RTPROCESS process, ULONG *user, ULONG *kernel);
        /** Returns the amount of memory used by a process in kilobytes. */
        virtual int getProcessMemoryUsage(RTPROCESS process, ULONG *used);

        /** Returns CPU usage counters in platform-specific units. */
        virtual int getRawHostCpuLoad(uint64_t *user, uint64_t *kernel, uint64_t *idle);
        /** Returns received and transmitted bytes. */
        virtual int getRawHostNetworkLoad(const char *name, uint64_t *rx, uint64_t *tx);
        /** Returns disk usage counters in platform-specific units. */
        virtual int getRawHostDiskLoad(const char *name, uint64_t *disk_ms, uint64_t *total_ms);
        /** Returns process' CPU usage counter in platform-specific units. */
        virtual int getRawProcessCpuLoad(RTPROCESS process, uint64_t *user, uint64_t *kernel, uint64_t *total);

        /** Returns the lists of disks (aggregate and physical) used by the specified file system. */
        virtual int getDiskListByFs(const char *name, DiskList& listUsage, DiskList& listLoad);
    };

    extern CollectorHAL *createHAL();

    /* Base Metrics *********************************************************/
    class BaseMetric
    {
    public:
        BaseMetric(CollectorHAL *hal, const com::Utf8Str name, ComPtr<IUnknown> object)
            : mPeriod(0), mLength(0), mHAL(hal), mName(name), mObject(object),
              mLastSampleTaken(0), mEnabled(false), mUnregistered(false) {};
        virtual ~BaseMetric() {};

        virtual void init(ULONG period, ULONG length) = 0;
        virtual void preCollect(CollectorHints& hints, uint64_t iTick) = 0;
        virtual void collect() = 0;
        virtual const char *getUnit() = 0;
        virtual ULONG getMinValue() = 0;
        virtual ULONG getMaxValue() = 0;
        virtual ULONG getScale() = 0;

        bool collectorBeat(uint64_t nowAt);

        virtual HRESULT enable()  { mEnabled = true; return S_OK; };
        virtual HRESULT disable() { mEnabled = false; return S_OK; };
        void unregister() { mUnregistered = true; };

        bool isUnregistered() { return mUnregistered; };
        bool isEnabled() { return mEnabled; };
        ULONG getPeriod() { return mPeriod; };
        ULONG getLength() { return mLength; };
        const char *getName() { return mName.c_str(); };
        ComPtr<IUnknown> getObject() { return mObject; };
        bool associatedWith(ComPtr<IUnknown> object) { return mObject == object; };

    protected:
        ULONG           mPeriod;
        ULONG           mLength;
        CollectorHAL    *mHAL;
        const com::Utf8Str mName;
        ComPtr<IUnknown> mObject;
        uint64_t         mLastSampleTaken;
        bool             mEnabled;
        bool             mUnregistered;
    };

    class BaseGuestMetric : public BaseMetric
    {
    public:
        BaseGuestMetric(CollectorGuest *cguest, const char *name, ComPtr<IUnknown> object)
            : BaseMetric(NULL, name, object), mCGuest(cguest) {};
    protected:
        CollectorGuest *mCGuest;
    };

    class HostCpuLoad : public BaseMetric
    {
    public:
        HostCpuLoad(CollectorHAL *hal, ComPtr<IUnknown> object, SubMetric *user, SubMetric *kernel, SubMetric *idle)
        : BaseMetric(hal, "CPU/Load", object), mUser(user), mKernel(kernel), mIdle(idle) {};
        ~HostCpuLoad() { delete mUser; delete mKernel; delete mIdle; };

        void init(ULONG period, ULONG length);

        void collect();
        const char *getUnit() { return "%"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return PM_CPU_LOAD_MULTIPLIER; };
        ULONG getScale() { return PM_CPU_LOAD_MULTIPLIER / 100; }

    protected:
        SubMetric *mUser;
        SubMetric *mKernel;
        SubMetric *mIdle;
    };

    class HostCpuLoadRaw : public HostCpuLoad
    {
    public:
        HostCpuLoadRaw(CollectorHAL *hal, ComPtr<IUnknown> object, SubMetric *user, SubMetric *kernel, SubMetric *idle)
        : HostCpuLoad(hal, object, user, kernel, idle), mUserPrev(0), mKernelPrev(0), mIdlePrev(0) {};

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
    private:
        uint64_t mUserPrev;
        uint64_t mKernelPrev;
        uint64_t mIdlePrev;
    };

    class HostCpuMhz : public BaseMetric
    {
    public:
        HostCpuMhz(CollectorHAL *hal, ComPtr<IUnknown> object, SubMetric *mhz)
        : BaseMetric(hal, "CPU/MHz", object), mMHz(mhz) {};
        ~HostCpuMhz() { delete mMHz; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& /* hints */, uint64_t /* iTick */) {}
        void collect();
        const char *getUnit() { return "MHz"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }
    private:
        SubMetric *mMHz;
    };

    class HostRamUsage : public BaseMetric
    {
    public:
        HostRamUsage(CollectorHAL *hal, ComPtr<IUnknown> object, SubMetric *total, SubMetric *used, SubMetric *available)
        : BaseMetric(hal, "RAM/Usage", object), mTotal(total), mUsed(used), mAvailable(available) {};
        ~HostRamUsage() { delete mTotal; delete mUsed; delete mAvailable; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        const char *getUnit() { return "kB"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }
    private:
        SubMetric *mTotal;
        SubMetric *mUsed;
        SubMetric *mAvailable;
    };

    class HostNetworkSpeed : public BaseMetric
    {
    public:
        HostNetworkSpeed(CollectorHAL *hal, ComPtr<IUnknown> object, com::Utf8Str name, com::Utf8Str shortname, com::Utf8Str /* ifname */, uint32_t speed, SubMetric *linkspeed)
        : BaseMetric(hal, name, object), mShortName(shortname), mSpeed(speed), mLinkSpeed(linkspeed) {};
        ~HostNetworkSpeed() { delete mLinkSpeed; };

        void init(ULONG period, ULONG length);

        void preCollect(CollectorHints& /* hints */, uint64_t /* iTick */) {};
        void collect() { if (mSpeed) mLinkSpeed->put(mSpeed); };
        const char *getUnit() { return "mbit/s"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }
    private:
        com::Utf8Str mShortName;
        uint32_t     mSpeed;
        SubMetric   *mLinkSpeed;
    };

    class HostNetworkLoadRaw : public BaseMetric
    {
    public:
        HostNetworkLoadRaw(CollectorHAL *hal, ComPtr<IUnknown> object, com::Utf8Str name, com::Utf8Str shortname, com::Utf8Str ifname, uint32_t speed, SubMetric *rx, SubMetric *tx)
            : BaseMetric(hal, name, object), mShortName(shortname), mInterfaceName(ifname), mRx(rx), mTx(tx), mRxPrev(0), mTxPrev(0), mRc(VINF_SUCCESS) { mSpeed = (uint64_t)speed * (1000000/8); /* Convert to bytes/sec */ };
        ~HostNetworkLoadRaw() { delete mRx; delete mTx; };

        void init(ULONG period, ULONG length);

        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        const char *getUnit() { return "%"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return PM_NETWORK_LOAD_MULTIPLIER; };
        ULONG getScale() { return PM_NETWORK_LOAD_MULTIPLIER / 100; }

    private:
        com::Utf8Str  mShortName;
        com::Utf8Str  mInterfaceName;
        SubMetric    *mRx;
        SubMetric    *mTx;
        uint64_t      mRxPrev;
        uint64_t      mTxPrev;
        uint64_t      mSpeed;
        int           mRc;
    };

    class HostFilesystemUsage : public BaseMetric
    {
    public:
        HostFilesystemUsage(CollectorHAL *hal, ComPtr<IUnknown> object, com::Utf8Str name, com::Utf8Str fsname, SubMetric *total, SubMetric *used, SubMetric *available)
            : BaseMetric(hal, name, object), mFsName(fsname), mTotal(total), mUsed(used), mAvailable(available) {};
        ~HostFilesystemUsage() { delete mTotal; delete mUsed; delete mAvailable; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        const char *getUnit() { return "MB"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }
    private:
        com::Utf8Str mFsName;
        SubMetric   *mTotal;
        SubMetric   *mUsed;
        SubMetric   *mAvailable;
    };

    class HostDiskUsage : public BaseMetric
    {
    public:
        HostDiskUsage(CollectorHAL *hal, ComPtr<IUnknown> object, com::Utf8Str name, com::Utf8Str diskname, SubMetric *total)
            : BaseMetric(hal, name, object), mDiskName(diskname), mTotal(total) {};
        ~HostDiskUsage() { delete mTotal; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        const char *getUnit() { return "MB"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }
    private:
        com::Utf8Str mDiskName;
        SubMetric   *mTotal;
    };

    class HostDiskLoadRaw : public BaseMetric
    {
    public:
        HostDiskLoadRaw(CollectorHAL *hal, ComPtr<IUnknown> object, com::Utf8Str name, com::Utf8Str diskname, SubMetric *util)
            : BaseMetric(hal, name, object), mDiskName(diskname), mUtil(util), mDiskPrev(0), mTotalPrev(0) {};
        ~HostDiskLoadRaw() { delete mUtil; };

        void init(ULONG period, ULONG length);

        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        const char *getUnit() { return "%"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return PM_DISK_LOAD_MULTIPLIER; };
        ULONG getScale() { return PM_DISK_LOAD_MULTIPLIER / 100; }

    private:
        com::Utf8Str  mDiskName;
        SubMetric    *mUtil;
        uint64_t      mDiskPrev;
        uint64_t      mTotalPrev;
    };


#ifndef VBOX_COLLECTOR_TEST_CASE
    class HostRamVmm : public BaseMetric
    {
    public:
        HostRamVmm(CollectorGuestManager *gm, ComPtr<IUnknown> object, SubMetric *allocVMM, SubMetric *freeVMM, SubMetric *balloonVMM, SubMetric *sharedVMM)
            : BaseMetric(NULL, "RAM/VMM", object), mCollectorGuestManager(gm),
            mAllocVMM(allocVMM), mFreeVMM(freeVMM), mBalloonVMM(balloonVMM), mSharedVMM(sharedVMM),
            mAllocCurrent(0), mFreeCurrent(0), mBalloonedCurrent(0), mSharedCurrent(0) {};
        ~HostRamVmm() { delete mAllocVMM; delete mFreeVMM; delete mBalloonVMM; delete mSharedVMM; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        HRESULT enable();
        HRESULT disable();
        const char *getUnit() { return "kB"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }

    private:
        CollectorGuestManager *mCollectorGuestManager;
        SubMetric             *mAllocVMM;
        SubMetric             *mFreeVMM;
        SubMetric             *mBalloonVMM;
        SubMetric             *mSharedVMM;
        ULONG                  mAllocCurrent;
        ULONG                  mFreeCurrent;
        ULONG                  mBalloonedCurrent;
        ULONG                  mSharedCurrent;
    };
#endif /* VBOX_COLLECTOR_TEST_CASE */

    class MachineCpuLoad : public BaseMetric
    {
    public:
        MachineCpuLoad(CollectorHAL *hal, ComPtr<IUnknown> object, RTPROCESS process, SubMetric *user, SubMetric *kernel)
        : BaseMetric(hal, "CPU/Load", object), mProcess(process), mUser(user), mKernel(kernel) {};
        ~MachineCpuLoad() { delete mUser; delete mKernel; };

        void init(ULONG period, ULONG length);
        void collect();
        const char *getUnit() { return "%"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return PM_CPU_LOAD_MULTIPLIER; };
        ULONG getScale() { return PM_CPU_LOAD_MULTIPLIER / 100; }
    protected:
        RTPROCESS  mProcess;
        SubMetric *mUser;
        SubMetric *mKernel;
    };

    class MachineCpuLoadRaw : public MachineCpuLoad
    {
    public:
        MachineCpuLoadRaw(CollectorHAL *hal, ComPtr<IUnknown> object, RTPROCESS process, SubMetric *user, SubMetric *kernel)
        : MachineCpuLoad(hal, object, process, user, kernel), mHostTotalPrev(0), mProcessUserPrev(0), mProcessKernelPrev(0) {};

        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
    private:
        uint64_t mHostTotalPrev;
        uint64_t mProcessUserPrev;
        uint64_t mProcessKernelPrev;
    };

    class MachineRamUsage : public BaseMetric
    {
    public:
        MachineRamUsage(CollectorHAL *hal, ComPtr<IUnknown> object, RTPROCESS process, SubMetric *used)
        : BaseMetric(hal, "RAM/Usage", object), mProcess(process), mUsed(used) {};
        ~MachineRamUsage() { delete mUsed; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        const char *getUnit() { return "kB"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }
    private:
        RTPROCESS  mProcess;
        SubMetric *mUsed;
    };


#ifndef VBOX_COLLECTOR_TEST_CASE
    typedef std::list<ComObjPtr<Medium> > MediaList;
    class MachineDiskUsage : public BaseMetric
    {
    public:
        MachineDiskUsage(CollectorHAL *hal, ComPtr<IUnknown> object, MediaList &disks, SubMetric *used)
        : BaseMetric(hal, "Disk/Usage", object), mDisks(disks), mUsed(used) {};
        ~MachineDiskUsage() { delete mUsed; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        const char *getUnit() { return "MB"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }
    private:
        MediaList   mDisks;
        SubMetric *mUsed;
    };

    /*
     * Although MachineNetRate is measured for VM, not for the guest, it is
     * derived from BaseGuestMetric since it uses the same mechanism for
     * data collection -- values get pushed by Guest class along with other
     * guest statistics.
     */
    class MachineNetRate : public BaseGuestMetric
    {
    public:
        MachineNetRate(CollectorGuest *cguest, ComPtr<IUnknown> object, SubMetric *rx, SubMetric *tx)
            : BaseGuestMetric(cguest, "Net/Rate", object), mRx(rx), mTx(tx) {};
        ~MachineNetRate() { delete mRx; delete mTx; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        HRESULT enable();
        HRESULT disable();
        const char *getUnit() { return "B/s"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }
    private:
        SubMetric *mRx, *mTx;
    };

    class GuestCpuLoad : public BaseGuestMetric
    {
    public:
        GuestCpuLoad(CollectorGuest *cguest, ComPtr<IUnknown> object, SubMetric *user, SubMetric *kernel, SubMetric *idle)
            : BaseGuestMetric(cguest, "Guest/CPU/Load", object), mUser(user), mKernel(kernel), mIdle(idle) {};
        ~GuestCpuLoad() { delete mUser; delete mKernel; delete mIdle; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        HRESULT enable();
        HRESULT disable();
        const char *getUnit() { return "%"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return PM_CPU_LOAD_MULTIPLIER; };
        ULONG getScale() { return PM_CPU_LOAD_MULTIPLIER / 100; }
    protected:
        SubMetric *mUser;
        SubMetric *mKernel;
        SubMetric *mIdle;
    };

    class GuestRamUsage : public BaseGuestMetric
    {
    public:
        GuestRamUsage(CollectorGuest *cguest, ComPtr<IUnknown> object, SubMetric *total, SubMetric *free, SubMetric *balloon, SubMetric *shared, SubMetric *cache, SubMetric *pagedtotal)
            : BaseGuestMetric(cguest, "Guest/RAM/Usage", object), mTotal(total), mFree(free), mBallooned(balloon), mCache(cache), mPagedTotal(pagedtotal), mShared(shared) {};
        ~GuestRamUsage() { delete mTotal; delete mFree; delete mBallooned; delete mShared; delete mCache; delete mPagedTotal; };

        void init(ULONG period, ULONG length);
        void preCollect(CollectorHints& hints, uint64_t iTick);
        void collect();
        HRESULT enable();
        HRESULT disable();
        const char *getUnit() { return "kB"; };
        ULONG getMinValue() { return 0; };
        ULONG getMaxValue() { return INT32_MAX; };
        ULONG getScale() { return 1; }
    private:
        SubMetric *mTotal, *mFree, *mBallooned, *mCache, *mPagedTotal, *mShared;
    };
#endif /* VBOX_COLLECTOR_TEST_CASE */

    /* Aggregate Functions **************************************************/
    class Aggregate
    {
    public:
        virtual ULONG compute(ULONG *data, ULONG length) = 0;
        virtual const char *getName() = 0;
        virtual ~Aggregate() {}
    };

    class AggregateAvg : public Aggregate
    {
    public:
        virtual ULONG compute(ULONG *data, ULONG length);
        virtual const char *getName();
    };

    class AggregateMin : public Aggregate
    {
    public:
        virtual ULONG compute(ULONG *data, ULONG length);
        virtual const char *getName();
    };

    class AggregateMax : public Aggregate
    {
    public:
        virtual ULONG compute(ULONG *data, ULONG length);
        virtual const char *getName();
    };

    /* Metric Class *********************************************************/
    class Metric
    {
    public:
        Metric(BaseMetric *baseMetric, SubMetric *subMetric, Aggregate *aggregate) :
            mName(subMetric->getName()), mBaseMetric(baseMetric), mSubMetric(subMetric), mAggregate(aggregate)
        {
            if (mAggregate)
            {
                mName.append(":");
                mName.append(mAggregate->getName());
            }
        }

        ~Metric()
        {
            delete mAggregate;
        }
        bool associatedWith(ComPtr<IUnknown> object) { return getObject() == object; };

        const char *getName() { return mName.c_str(); };
        ComPtr<IUnknown> getObject() { return mBaseMetric->getObject(); };
        const char *getDescription()
            { return mAggregate ? "" : mSubMetric->getDescription(); };
        const char *getUnit() { return mBaseMetric->getUnit(); };
        ULONG getMinValue() { return mBaseMetric->getMinValue(); };
        ULONG getMaxValue() { return mBaseMetric->getMaxValue(); };
        ULONG getPeriod() { return mBaseMetric->getPeriod(); };
        ULONG getLength()
            { return mAggregate ? 1 : mBaseMetric->getLength(); };
        ULONG getScale() { return mBaseMetric->getScale(); }
        void query(ULONG **data, ULONG *count, ULONG *sequenceNumber);

    private:
        RTCString mName;
        BaseMetric *mBaseMetric;
        SubMetric  *mSubMetric;
        Aggregate  *mAggregate;
    };

    /* Filter Class *********************************************************/

    class Filter
    {
    public:
        Filter(const std::vector<com::Utf8Str> &metricNames,
               const std::vector<ComPtr<IUnknown> > &objects);
        Filter(const com::Utf8Str &name, const ComPtr<IUnknown> &aObject);
        static bool patternMatch(const char *pszPat, const char *pszName,
                                 bool fSeenColon = false);
        bool match(const ComPtr<IUnknown> object, const RTCString &name) const;
    private:
        typedef std::pair<const ComPtr<IUnknown>, const RTCString> FilterElement;
        typedef std::list<FilterElement> ElementList;

        ElementList mElements;

        void processMetricList(const com::Utf8Str &name, const ComPtr<IUnknown> object);
    };
}
#endif /* !MAIN_INCLUDED_Performance_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
