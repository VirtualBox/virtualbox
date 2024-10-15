/* $Id: tstCollector.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Performance collector classes test cases.
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

#ifdef RT_OS_DARWIN
# include "../src-server/darwin/PerformanceDarwin.cpp"
#endif
#ifdef RT_OS_FREEBSD
# include "../src-server/freebsd/PerformanceFreeBSD.cpp"
#endif
#ifdef RT_OS_LINUX
# include "../src-server/linux/PerformanceLinux.cpp"
#endif
#ifdef RT_OS_OS2
# include "../src-server/os2/PerformanceOS2.cpp"
#endif
#ifdef RT_OS_SOLARIS
# include "../src-server/solaris/PerformanceSolaris.cpp"
#endif
#ifdef RT_OS_WINDOWS
# define _WIN32_DCOM
# include <iprt/win/objidl.h>
# include <iprt/win/objbase.h>
# include "../src-server/win/PerformanceWin.cpp"
#endif

#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/process.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#define RUN_TIME_MS        1000

#define N_CALLS(n, fn) \
  do {\
    for (int call = 0; call < n; ++call) \
        rc = collector->fn; \
    if (RT_FAILURE(rc)) \
        RTPrintf("tstCollector: "#fn" -> %Rrc\n", rc); \
  } while (0)

#define CALLS_PER_SECOND(fn, args) \
  do { \
    nCalls = 0; \
    start = RTTimeMilliTS(); \
    do { \
        rc = collector->fn args; \
        if (RT_FAILURE(rc)) \
            break; \
        ++nCalls; \
    } while (RTTimeMilliTS() - start < RUN_TIME_MS); \
    if (RT_FAILURE(rc)) \
        RTPrintf("tstCollector: "#fn" -> %Rrc\n", rc); \
    else \
        RTPrintf("%70s -- %u calls per second\n", #fn, nCalls); \
  } while (0)

static void shutdownProcessList(std::vector<RTPROCESS> const &rProcesses)
{
    for (size_t i = 0; i < rProcesses.size(); i++)
        RTProcTerminate(rProcesses[i]);
}

static void measurePerformance(pm::CollectorHAL *collector, const char *pszName, int cVMs)
{

    const char * const args[] = { pszName, "-child", NULL };
    pm::CollectorHints hints;
    std::vector<RTPROCESS> processes;

    hints.collectHostCpuLoad();
    hints.collectHostRamUsage();
    /* Start fake VMs */
    for (int i = 0; i < cVMs; ++i)
    {
        RTPROCESS pid;
        int rc = RTProcCreate(pszName, args, RTENV_DEFAULT, 0, &pid);
        if (RT_FAILURE(rc))
        {
            hints.getProcesses(processes);
            shutdownProcessList(processes);

            RTPrintf("tstCollector: RTProcCreate() -> %Rrc\n", rc);
            return;
        }
        hints.collectProcessCpuLoad(pid);
        hints.collectProcessRamUsage(pid);
    }

    hints.getProcesses(processes);
    RTThreadSleep(30000); // Let children settle for half a minute

    int rc;
    ULONG tmp;
    uint64_t tmp64;
    uint64_t start;
    unsigned int nCalls;
    /* Pre-collect */
    CALLS_PER_SECOND(preCollect, (hints, 0));
    /* Host CPU load */
    CALLS_PER_SECOND(getRawHostCpuLoad, (&tmp64, &tmp64, &tmp64));
    /* Process CPU load */
    CALLS_PER_SECOND(getRawProcessCpuLoad, (processes[nCalls % cVMs], &tmp64, &tmp64, &tmp64));
    /* Host CPU speed */
    CALLS_PER_SECOND(getHostCpuMHz, (&tmp));
    /* Host RAM usage */
    CALLS_PER_SECOND(getHostMemoryUsage, (&tmp, &tmp, &tmp));
    /* Process RAM usage */
    CALLS_PER_SECOND(getProcessMemoryUsage, (processes[nCalls % cVMs], &tmp));

    start = RTTimeNanoTS();

    int times;
    for (times = 0; times < 100; times++)
    {
        /* Pre-collect */
        N_CALLS(1, preCollect(hints, 0));
        /* Host CPU load */
        N_CALLS(1, getRawHostCpuLoad(&tmp64, &tmp64, &tmp64));
        /* Host CPU speed */
        N_CALLS(1, getHostCpuMHz(&tmp));
        /* Host RAM usage */
        N_CALLS(1, getHostMemoryUsage(&tmp, &tmp, &tmp));
        /* Process CPU load */
        N_CALLS(cVMs, getRawProcessCpuLoad(processes[call], &tmp64, &tmp64, &tmp64));
        /* Process RAM usage */
        N_CALLS(cVMs, getProcessMemoryUsage(processes[call], &tmp));
    }
    RTPrintf("\n%d VMs -- %u%% of CPU time\n", cVMs, (unsigned)((double)(RTTimeNanoTS() - start) / 10000000.0 / times));

    /* Shut down fake VMs */
    shutdownProcessList(processes);
}

#ifdef RT_OS_SOLARIS
#define NETIFNAME "net0"
#else
#define NETIFNAME "eth0"
#endif
static int testNetwork(pm::CollectorHAL *collector)
{
    pm::CollectorHints hints;
    uint64_t hostRxStart, hostTxStart;
    uint64_t hostRxStop, hostTxStop, speed = 125000000; /* Assume 1Gbit/s */

    RTPrintf("tstCollector: TESTING - Network load, sleeping for 5 s...\n");

    hostRxStart = hostTxStart = 0;
    int rc = collector->preCollect(hints, 0);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getRawHostNetworkLoad(NETIFNAME, &hostRxStart, &hostTxStart);
    if (rc == VERR_NOT_IMPLEMENTED)
        RTPrintf("tstCollector: getRawHostNetworkLoad() not implemented, skipping\n");
    else
    {
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawHostNetworkLoad() -> %Rrc\n", rc);
            return 1;
        }

        RTThreadSleep(5000); // Sleep for five seconds

        rc = collector->preCollect(hints, 0);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
            return 1;
        }
        hostRxStop = hostRxStart;
        hostTxStop = hostTxStart;
        rc = collector->getRawHostNetworkLoad(NETIFNAME, &hostRxStop, &hostTxStop);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawHostNetworkLoad() -> %Rrc\n", rc);
            return 1;
        }
        RTPrintf("tstCollector: host network speed = %llu bytes/sec (%llu mbit/sec)\n",
                speed, speed/(1000000/8));
        RTPrintf("tstCollector: host network rx    = %llu bytes/sec (%llu mbit/sec, %u.%u %%)\n",
                (hostRxStop - hostRxStart)/5, (hostRxStop - hostRxStart)/(5000000/8),
                (hostRxStop - hostRxStart) * 100 / (speed * 5),
                (hostRxStop - hostRxStart) * 10000 / (speed * 5) % 100);
        RTPrintf("tstCollector: host network tx    = %llu bytes/sec (%llu mbit/sec, %u.%u %%)\n\n",
                (hostTxStop - hostTxStart)/5, (hostTxStop - hostTxStart)/(5000000/8),
                (hostTxStop - hostTxStart) * 100 / (speed * 5),
                (hostTxStop - hostTxStart) * 10000 / (speed * 5) % 100);
    }

    return 0;
}

#define FSNAME "/"
static int testFsUsage(pm::CollectorHAL *collector)
{
    RTPrintf("tstCollector: TESTING - File system usage\n");

    ULONG total, used, available;

    int rc = collector->getHostFilesystemUsage(FSNAME, &total, &used, &available);
    if (rc == VERR_NOT_IMPLEMENTED)
        RTPrintf("tstCollector: getHostFilesystemUsage() not implemented, skipping\n");
    else
    {
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getHostFilesystemUsage() -> %Rrc\n", rc);
            return 1;
        }
        RTPrintf("tstCollector: host root fs total     = %lu MB\n", total);
        RTPrintf("tstCollector: host root fs used      = %lu MB\n", used);
        RTPrintf("tstCollector: host root fs available = %lu MB\n\n", available);
    }
    return 0;
}

static int testDisk(pm::CollectorHAL *collector)
{
    pm::CollectorHints hints;
    uint64_t diskMsStart, totalMsStart;
    uint64_t diskMsStop, totalMsStop;

    pm::DiskList disksUsage, disksLoad;
    int rc = collector->getDiskListByFs(FSNAME, disksUsage, disksLoad);
    if (rc == VERR_NOT_IMPLEMENTED)
        RTPrintf("tstCollector: getDiskListByFs() not implemented, skipping\n");
    else
    {
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getDiskListByFs(%s) -> %Rrc\n", FSNAME, rc);
            return 1;
        }
        if (disksUsage.empty())
        {
            RTPrintf("tstCollector: getDiskListByFs(%s) returned empty usage list\n", FSNAME);
            return 0;
        }
        if (disksLoad.empty())
        {
            RTPrintf("tstCollector: getDiskListByFs(%s) returned empty usage list\n", FSNAME);
            return 0;
        }

        pm::DiskList::iterator it;
        for (it = disksUsage.begin(); it != disksUsage.end(); ++it)
        {
            uint64_t diskSize = 0;
            rc = collector->getHostDiskSize(it->c_str(), &diskSize);
            RTPrintf("tstCollector: TESTING - Disk size (%s) = %llu\n", it->c_str(), diskSize);
            if (rc == VERR_FILE_NOT_FOUND)
                RTPrintf("tstCollector: getHostDiskSize(%s) returned VERR_FILE_NOT_FOUND\n", it->c_str());
            else if (RT_FAILURE(rc))
            {
                RTPrintf("tstCollector: getHostDiskSize() -> %Rrc\n", rc);
                return 1;
            }
        }

        for (it = disksLoad.begin(); it != disksLoad.end(); ++it)
        {
            RTPrintf("tstCollector: TESTING - Disk utilization (%s), sleeping for 5 s...\n", it->c_str());

            hints.collectHostCpuLoad();
            rc = collector->preCollect(hints, 0);
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
                return 1;
            }
            rc = collector->getRawHostDiskLoad(it->c_str(), &diskMsStart, &totalMsStart);
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstCollector: getRawHostDiskLoad() -> %Rrc\n", rc);
                return 1;
            }

            RTThreadSleep(5000); // Sleep for five seconds

            rc = collector->preCollect(hints, 0);
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
                return 1;
            }
            rc = collector->getRawHostDiskLoad(it->c_str(), &diskMsStop, &totalMsStop);
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstCollector: getRawHostDiskLoad() -> %Rrc\n", rc);
                return 1;
            }
            RTPrintf("tstCollector: host disk util    = %llu msec (%u.%u %%), total = %llu msec\n\n",
                     (diskMsStop - diskMsStart),
                     (unsigned)((diskMsStop - diskMsStart) * 100 / (totalMsStop - totalMsStart)),
                     (unsigned)((diskMsStop - diskMsStart) * 10000 / (totalMsStop - totalMsStart) % 100),
                     totalMsStop - totalMsStart);
        }
    }

    return 0;
}



int main(int argc, char *argv[])
{
    bool cpuTest, ramTest, netTest, diskTest, fsTest, perfTest;
    cpuTest = ramTest = netTest = diskTest = fsTest = perfTest = false;
    /*
     * Initialize the VBox runtime without loading
     * the support driver.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: RTR3InitExe() -> %d\n", rc);
        return 1;
    }
    if (argc > 1)
    {
        if (!strcmp(argv[1], "-child"))
        {
            /* We have spawned ourselves as a child process -- scratch the leg */
            RTThreadSleep(1000000);
            return 1;
        }
        for (int i = 1; i < argc; i++)
        {
            if (!strcmp(argv[i], "-cpu"))
                cpuTest = true;
            else if (!strcmp(argv[i], "-ram"))
                ramTest = true;
            else if (!strcmp(argv[i], "-net"))
                netTest = true;
            else if (!strcmp(argv[i], "-disk"))
                diskTest = true;
            else if (!strcmp(argv[i], "-fs"))
                fsTest = true;
            else if (!strcmp(argv[i], "-perf"))
                perfTest = true;
            else
            {
                RTPrintf("tstCollector: Unknown option: %s\n", argv[i]);
                return 2;
            }
        }
    }
    else
        cpuTest = ramTest = netTest = diskTest = fsTest = perfTest = true;

#ifdef RT_OS_WINDOWS
    HRESULT hRes = CoInitialize(NULL);
    /*
     * Need to initialize security to access performance enumerators.
     */
    hRes = CoInitializeSecurity(
        NULL,
        -1,
        NULL,
        NULL,
        RPC_C_AUTHN_LEVEL_NONE,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, 0);
#endif

    pm::CollectorHAL *collector = pm::createHAL();
    if (!collector)
    {
        RTPrintf("tstCollector: createMetricFactory() failed\n");
        return 1;
    }

    pm::CollectorHints hints;
    if (cpuTest)
    {
        hints.collectHostCpuLoad();
        hints.collectProcessCpuLoad(RTProcSelf());
    }
    if (ramTest)
    {
        hints.collectHostRamUsage();
        hints.collectProcessRamUsage(RTProcSelf());
    }

    uint64_t start;

    uint64_t hostUserStart, hostKernelStart, hostIdleStart;
    uint64_t hostUserStop, hostKernelStop, hostIdleStop, hostTotal;

    uint64_t processUserStart, processKernelStart, processTotalStart;
    uint64_t processUserStop, processKernelStop, processTotalStop;

    rc = collector->preCollect(hints, 0);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
        return 1;
    }
    if (cpuTest)
    {
        RTPrintf("tstCollector: TESTING - CPU load, sleeping for 5 s...\n");

        rc = collector->getRawHostCpuLoad(&hostUserStart, &hostKernelStart, &hostIdleStart);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawHostCpuLoad() -> %Rrc\n", rc);
            return 1;
        }
        rc = collector->getRawProcessCpuLoad(RTProcSelf(), &processUserStart, &processKernelStart, &processTotalStart);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawProcessCpuLoad() -> %Rrc\n", rc);
            return 1;
        }

        RTThreadSleep(5000); // Sleep for 5 seconds

        rc = collector->preCollect(hints, 0);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
            return 1;
        }
        rc = collector->getRawHostCpuLoad(&hostUserStop, &hostKernelStop, &hostIdleStop);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawHostCpuLoad() -> %Rrc\n", rc);
            return 1;
        }
        rc = collector->getRawProcessCpuLoad(RTProcSelf(), &processUserStop, &processKernelStop, &processTotalStop);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawProcessCpuLoad() -> %Rrc\n", rc);
            return 1;
        }
        hostTotal = hostUserStop - hostUserStart
            + hostKernelStop - hostKernelStart
            + hostIdleStop - hostIdleStart;
        RTPrintf("tstCollector: host cpu user      = %u.%u %%\n",
                 (unsigned)((hostUserStop - hostUserStart) * 100 / hostTotal),
                 (unsigned)((hostUserStop - hostUserStart) * 10000 / hostTotal % 100));
        RTPrintf("tstCollector: host cpu kernel    = %u.%u %%\n",
                 (unsigned)((hostKernelStop - hostKernelStart) * 100 / hostTotal),
                 (unsigned)((hostKernelStop - hostKernelStart) * 10000 / hostTotal % 100));
        RTPrintf("tstCollector: host cpu idle      = %u.%u %%\n",
                 (unsigned)((hostIdleStop - hostIdleStart) * 100 / hostTotal),
                 (unsigned)((hostIdleStop - hostIdleStart) * 10000 / hostTotal % 100));
        RTPrintf("tstCollector: process cpu user   = %u.%u %%\n",
                 (unsigned)((processUserStop - processUserStart) * 100 / (processTotalStop - processTotalStart)),
                 (unsigned)((processUserStop - processUserStart) * 10000 / (processTotalStop - processTotalStart) % 100));
        RTPrintf("tstCollector: process cpu kernel = %u.%u %%\n\n",
                 (unsigned)((processKernelStop - processKernelStart) * 100 / (processTotalStop - processTotalStart)),
                 (unsigned)((processKernelStop - processKernelStart) * 10000 / (processTotalStop - processTotalStart) % 100));

        RTPrintf("tstCollector: TESTING - CPU load, looping for 5 s...\n");
        rc = collector->preCollect(hints, 0);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
            return 1;
        }
        rc = collector->getRawHostCpuLoad(&hostUserStart, &hostKernelStart, &hostIdleStart);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawHostCpuLoad() -> %Rrc\n", rc);
            return 1;
        }
        rc = collector->getRawProcessCpuLoad(RTProcSelf(), &processUserStart, &processKernelStart, &processTotalStart);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawProcessCpuLoad() -> %Rrc\n", rc);
            return 1;
        }
        start = RTTimeMilliTS();
        while (RTTimeMilliTS() - start < 5000)
            ; // Loop for 5 seconds
        rc = collector->preCollect(hints, 0);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
            return 1;
        }
        rc = collector->getRawHostCpuLoad(&hostUserStop, &hostKernelStop, &hostIdleStop);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawHostCpuLoad() -> %Rrc\n", rc);
            return 1;
        }
        rc = collector->getRawProcessCpuLoad(RTProcSelf(), &processUserStop, &processKernelStop, &processTotalStop);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getRawProcessCpuLoad() -> %Rrc\n", rc);
            return 1;
        }
        hostTotal = hostUserStop - hostUserStart
            + hostKernelStop - hostKernelStart
            + hostIdleStop - hostIdleStart;
        RTPrintf("tstCollector: host cpu user      = %u.%u %%\n",
                 (unsigned)((hostUserStop - hostUserStart) * 100 / hostTotal),
                 (unsigned)((hostUserStop - hostUserStart) * 10000 / hostTotal % 100));
        RTPrintf("tstCollector: host cpu kernel    = %u.%u %%\n",
                 (unsigned)((hostKernelStop - hostKernelStart) * 100 / hostTotal),
                 (unsigned)((hostKernelStop - hostKernelStart) * 10000 / hostTotal % 100));
        RTPrintf("tstCollector: host cpu idle      = %u.%u %%\n",
                 (unsigned)((hostIdleStop - hostIdleStart) * 100 / hostTotal),
                 (unsigned)((hostIdleStop - hostIdleStart) * 10000 / hostTotal % 100));
        RTPrintf("tstCollector: process cpu user   = %u.%u %%\n",
                 (unsigned)((processUserStop - processUserStart) * 100 / (processTotalStop - processTotalStart)),
                 (unsigned)((processUserStop - processUserStart) * 10000 / (processTotalStop - processTotalStart) % 100));
        RTPrintf("tstCollector: process cpu kernel = %u.%u %%\n\n",
                 (unsigned)((processKernelStop - processKernelStart) * 100 / (processTotalStop - processTotalStart)),
                 (unsigned)((processKernelStop - processKernelStart) * 10000 / (processTotalStop - processTotalStart) % 100));
    }

    if (ramTest)
    {
        RTPrintf("tstCollector: TESTING - Memory usage\n");

        ULONG total, used, available, processUsed;

        rc = collector->getHostMemoryUsage(&total, &used, &available);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getHostMemoryUsage() -> %Rrc\n", rc);
            return 1;
        }
        rc = collector->getProcessMemoryUsage(RTProcSelf(), &processUsed);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstCollector: getProcessMemoryUsage() -> %Rrc\n", rc);
            return 1;
        }
        RTPrintf("tstCollector: host mem total     = %lu kB\n", total);
        RTPrintf("tstCollector: host mem used      = %lu kB\n", used);
        RTPrintf("tstCollector: host mem available = %lu kB\n", available);
        RTPrintf("tstCollector: process mem used   = %lu kB\n\n", processUsed);
    }

    if (netTest)
        rc = testNetwork(collector);
    if (fsTest)
    rc = testFsUsage(collector);
    if (diskTest)
        rc = testDisk(collector);
    if (perfTest)
    {
        RTPrintf("tstCollector: TESTING - Performance\n\n");

        measurePerformance(collector, argv[0], 100);
    }

    delete collector;

    RTPrintf("\ntstCollector FINISHED.\n");

    return RTEXITCODE_SUCCESS;
}

