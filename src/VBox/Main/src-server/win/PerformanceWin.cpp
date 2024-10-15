/* $Id: PerformanceWin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Windows-specific Performance Classes implementation.
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

#define LOG_GROUP LOG_GROUP_MAIN_PERFORMANCECOLLECTOR
#ifndef _WIN32_WINNT
# define _WIN32_WINNT 0x0500
#else /* !_WIN32_WINNT */
# if (_WIN32_WINNT < 0x0500)
#  error Win XP or later required!
# endif /* _WIN32_WINNT < 0x0500 */
#endif /* !_WIN32_WINNT */

#include <iprt/win/windows.h>
#include <winternl.h>
#include <psapi.h>
extern "C" {
#include <powrprof.h>
}

#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <iprt/mp.h>
#include <iprt/mem.h>
#include <iprt/system.h>

#include <map>

#include "LoggingNew.h"
#include "Performance.h"

#ifndef NT_ERROR
#define NT_ERROR(Status) ((ULONG)(Status) >> 30 == 3)
#endif

namespace pm {

class CollectorWin : public CollectorHAL
{
public:
    CollectorWin();
    virtual ~CollectorWin();
    virtual int preCollect(const CollectorHints& hints, uint64_t /* iTick */);
    virtual int getHostCpuLoad(ULONG *user, ULONG *kernel, ULONG *idle);
    virtual int getHostCpuMHz(ULONG *mhz);
    virtual int getHostMemoryUsage(ULONG *total, ULONG *used, ULONG *available);
    virtual int getProcessCpuLoad(RTPROCESS process, ULONG *user, ULONG *kernel);
    virtual int getProcessMemoryUsage(RTPROCESS process, ULONG *used);

    virtual int getRawHostCpuLoad(uint64_t *user, uint64_t *kernel, uint64_t *idle);
    virtual int getRawProcessCpuLoad(RTPROCESS process, uint64_t *user, uint64_t *kernel, uint64_t *total);

private:
    struct VMProcessStats
    {
        uint64_t cpuUser;
        uint64_t cpuKernel;
        uint64_t cpuTotal;
        uint64_t ramUsed;
    };

    typedef std::map<RTPROCESS, VMProcessStats> VMProcessMap;

    VMProcessMap mProcessStats;

    typedef BOOL (WINAPI *PFNGST)(LPFILETIME lpIdleTime,
                                  LPFILETIME lpKernelTime,
                                  LPFILETIME lpUserTime);
    typedef NTSTATUS (WINAPI *PFNNQSI)(SYSTEM_INFORMATION_CLASS SystemInformationClass,
                                       PVOID SystemInformation,
                                       ULONG SystemInformationLength,
                                       PULONG ReturnLength);

    PFNGST  mpfnGetSystemTimes;
    PFNNQSI mpfnNtQuerySystemInformation;

    ULONG   totalRAM;
};

CollectorHAL *createHAL()
{
    return new CollectorWin();
}

CollectorWin::CollectorWin() : CollectorHAL(), mpfnNtQuerySystemInformation(NULL)
{
    /* Note! Both kernel32.dll and ntdll.dll can be assumed to always be present. */
    mpfnGetSystemTimes = (PFNGST)RTLdrGetSystemSymbol("kernel32.dll", "GetSystemTimes");
    if (!mpfnGetSystemTimes)
    {
        /* Fall back to deprecated NtQuerySystemInformation */
        mpfnNtQuerySystemInformation = (PFNNQSI)RTLdrGetSystemSymbol("ntdll.dll", "NtQuerySystemInformation");
        if (!mpfnNtQuerySystemInformation)
            LogRel(("Warning! Neither GetSystemTimes() nor NtQuerySystemInformation() is not available.\n"
                    "         CPU and VM metrics will not be collected! (lasterr %u)\n", GetLastError()));
    }

    uint64_t cb;
    int vrc = RTSystemQueryTotalRam(&cb);
    if (RT_FAILURE(vrc))
        totalRAM = 0;
    else
        totalRAM = (ULONG)(cb / 1024);
}

CollectorWin::~CollectorWin()
{
}

#define FILETTIME_TO_100NS(ft) (((uint64_t)ft.dwHighDateTime << 32) + ft.dwLowDateTime)

int CollectorWin::preCollect(const CollectorHints& hints, uint64_t /* iTick */)
{
    LogFlowThisFuncEnter();

    uint64_t user, kernel, idle, total;
    int vrc = getRawHostCpuLoad(&user, &kernel, &idle);
    if (RT_FAILURE(vrc))
        return vrc;
    total = user + kernel + idle;

    DWORD dwError;
    const CollectorHints::ProcessList& processes = hints.getProcessFlags();
    CollectorHints::ProcessList::const_iterator it;

    mProcessStats.clear();

    for (it = processes.begin(); it != processes.end() && RT_SUCCESS(vrc); ++it)
    {
        RTPROCESS process = it->first;
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process);

        if (!h)
        {
            dwError = GetLastError();
            Log (("OpenProcess() -> 0x%x\n", dwError));
            vrc = RTErrConvertFromWin32(dwError);
            break;
        }

        VMProcessStats vmStats;
        RT_ZERO(vmStats);
        if ((it->second & COLLECT_CPU_LOAD) != 0)
        {
            FILETIME ftCreate, ftExit, ftKernel, ftUser;
            if (!GetProcessTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser))
            {
                dwError = GetLastError();
                Log (("GetProcessTimes() -> 0x%x\n", dwError));
                vrc = RTErrConvertFromWin32(dwError);
            }
            else
            {
                vmStats.cpuKernel = FILETTIME_TO_100NS(ftKernel);
                vmStats.cpuUser   = FILETTIME_TO_100NS(ftUser);
                vmStats.cpuTotal  = total;
            }
        }
        if (RT_SUCCESS(vrc) && (it->second & COLLECT_RAM_USAGE) != 0)
        {
            PROCESS_MEMORY_COUNTERS pmc;
            if (!GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
            {
                dwError = GetLastError();
                Log (("GetProcessMemoryInfo() -> 0x%x\n", dwError));
                vrc = RTErrConvertFromWin32(dwError);
            }
            else
                vmStats.ramUsed = pmc.WorkingSetSize;
        }
        CloseHandle(h);
        mProcessStats[process] = vmStats;
    }

    LogFlowThisFuncLeave();

    return vrc;
}

int CollectorWin::getHostCpuLoad(ULONG *user, ULONG *kernel, ULONG *idle)
{
    RT_NOREF(user, kernel, idle);
    return VERR_NOT_IMPLEMENTED;
}

typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION
{
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER Reserved1[2];
    ULONG Reserved2;
} SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

int CollectorWin::getRawHostCpuLoad(uint64_t *user, uint64_t *kernel, uint64_t *idle)
{
    LogFlowThisFuncEnter();

    FILETIME ftIdle, ftKernel, ftUser;

    if (mpfnGetSystemTimes)
    {
        if (!mpfnGetSystemTimes(&ftIdle, &ftKernel, &ftUser))
        {
            DWORD dwError = GetLastError();
            Log (("GetSystemTimes() -> 0x%x\n", dwError));
            return RTErrConvertFromWin32(dwError);
        }

        *user   = FILETTIME_TO_100NS(ftUser);
        *idle   = FILETTIME_TO_100NS(ftIdle);
        *kernel = FILETTIME_TO_100NS(ftKernel) - *idle;
    }
    else
    {
        /* GetSystemTimes is not available, fall back to NtQuerySystemInformation */
        if (!mpfnNtQuerySystemInformation)
            return VERR_NOT_IMPLEMENTED;

        SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION sppi[MAXIMUM_PROCESSORS];
        ULONG ulReturned;
        NTSTATUS status = mpfnNtQuerySystemInformation(
            SystemProcessorPerformanceInformation, &sppi, sizeof(sppi), &ulReturned);
        if (NT_ERROR(status))
        {
            Log(("NtQuerySystemInformation() -> 0x%x\n", status));
            return RTErrConvertFromNtStatus(status);
        }
        /* Sum up values across all processors */
        *user = *kernel = *idle = 0;
        for (unsigned i = 0; i < ulReturned / sizeof(sppi[0]); ++i)
        {
            *idle   += sppi[i].IdleTime.QuadPart;
            *kernel += sppi[i].KernelTime.QuadPart - sppi[i].IdleTime.QuadPart;
            *user   += sppi[i].UserTime.QuadPart;
        }
    }

    LogFlowThisFunc(("user=%lu kernel=%lu idle=%lu\n", *user, *kernel, *idle));
    LogFlowThisFuncLeave();

    return VINF_SUCCESS;
}

typedef struct _PROCESSOR_POWER_INFORMATION {
  ULONG  Number;
  ULONG  MaxMhz;
  ULONG  CurrentMhz;
  ULONG  MhzLimit;
  ULONG  MaxIdleState;
  ULONG  CurrentIdleState;
} PROCESSOR_POWER_INFORMATION , *PPROCESSOR_POWER_INFORMATION;

int CollectorWin::getHostCpuMHz(ULONG *mhz)
{
    uint64_t uTotalMhz   = 0;
    RTCPUID  nProcessors = RTMpGetCount();
    PPROCESSOR_POWER_INFORMATION ppi = (PPROCESSOR_POWER_INFORMATION)
                                       RTMemAllocZ(nProcessors * sizeof(PROCESSOR_POWER_INFORMATION));

    if (!ppi)
        return VERR_NO_MEMORY;

    LONG ns = CallNtPowerInformation(ProcessorInformation, NULL, 0, ppi,
                                     nProcessors * sizeof(PROCESSOR_POWER_INFORMATION));
    if (ns)
    {
        Log(("CallNtPowerInformation() -> %x\n", ns));
        RTMemFree(ppi);
        return VERR_INTERNAL_ERROR;
    }

    /* Compute an average over all CPUs */
    for (unsigned i = 0; i < nProcessors;  i++)
        uTotalMhz += ppi[i].CurrentMhz;
    *mhz = (ULONG)(uTotalMhz / nProcessors);

    RTMemFree(ppi);
    LogFlowThisFunc(("mhz=%u\n", *mhz));
    LogFlowThisFuncLeave();

    return VINF_SUCCESS;
}

int CollectorWin::getHostMemoryUsage(ULONG *total, ULONG *used, ULONG *available)
{
    AssertReturn(totalRAM, VERR_INTERNAL_ERROR);
    uint64_t cb;
    int vrc = RTSystemQueryAvailableRam(&cb);
    if (RT_SUCCESS(vrc))
    {
        *total = totalRAM;
        *available = (ULONG)(cb / 1024);
        *used = *total - *available;
    }
    return vrc;
}

int CollectorWin::getProcessCpuLoad(RTPROCESS process, ULONG *user, ULONG *kernel)
{
    RT_NOREF(process, user, kernel);
    return VERR_NOT_IMPLEMENTED;
}

int CollectorWin::getRawProcessCpuLoad(RTPROCESS process, uint64_t *user, uint64_t *kernel, uint64_t *total)
{
    VMProcessMap::const_iterator it = mProcessStats.find(process);

    if (it == mProcessStats.end())
    {
        Log (("No stats pre-collected for process %x\n", process));
        return VERR_INTERNAL_ERROR;
    }
    *user   = it->second.cpuUser;
    *kernel = it->second.cpuKernel;
    *total  = it->second.cpuTotal;
    return VINF_SUCCESS;
}

int CollectorWin::getProcessMemoryUsage(RTPROCESS process, ULONG *used)
{
    VMProcessMap::const_iterator it = mProcessStats.find(process);

    if (it == mProcessStats.end())
    {
        Log (("No stats pre-collected for process %x\n", process));
        return VERR_INTERNAL_ERROR;
    }
    *used = (ULONG)(it->second.ramUsed / 1024);
    return VINF_SUCCESS;
}

}
