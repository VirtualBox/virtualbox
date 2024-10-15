/* $Id: mp-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Darwin.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DEFAULT /*RTLOGGROUP_SYSTEM*/
#include <iprt/types.h>

#include <unistd.h>
#include <stdio.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <mach/mach.h>

#include <CoreFoundation/CFBase.h>
#include <IOKit/IOKitLib.h>
/*#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOCDMedia.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <mach/mach_error.h>
#include <sys/param.h>
#include <paths.h>*/

#include <iprt/mp.h>
#include <iprt/assert.h>
#include <iprt/cpuset.h>
#include <iprt/log.h>
#include <iprt/string.h>


/**
 * Internal worker that determines the max possible logical CPU count (hyperthreads).
 *
 * @returns Max cpus.
 */
static RTCPUID rtMpDarwinMaxLogicalCpus(void)
{
    int cCpus = -1;
    size_t cb = sizeof(cCpus);
    int rc = sysctlbyname("hw.logicalcpu_max", &cCpus, &cb, NULL, 0);
    if (rc != -1 && cCpus >= 1)
        return cCpus;
    AssertFailed();
    return 1;
}

/**
 * Internal worker that determines the max possible physical core count.
 *
 * @returns Max cpus.
 */
static RTCPUID rtMpDarwinMaxPhysicalCpus(void)
{
    int cCpus = -1;
    size_t cb = sizeof(cCpus);
    int rc = sysctlbyname("hw.physicalcpu_max", &cCpus, &cb, NULL, 0);
    if (rc != -1 && cCpus >= 1)
        return cCpus;
    AssertFailed();
    return 1;
}


#if 0 /* unused */
/**
 * Internal worker that determines the current number of logical CPUs (hyperthreads).
 *
 * @returns Max cpus.
 */
static RTCPUID rtMpDarwinOnlineLogicalCpus(void)
{
    int cCpus = -1;
    size_t cb = sizeof(cCpus);
    int rc = sysctlbyname("hw.logicalcpu", &cCpus, &cb, NULL, 0);
    if (rc != -1 && cCpus >= 1)
        return cCpus;
    AssertFailed();
    return 1;
}
#endif /* unused */


/**
 * Internal worker that determines the current number of physical CPUs.
 *
 * @returns Max cpus.
 */
static RTCPUID rtMpDarwinOnlinePhysicalCpus(void)
{
    int cCpus = -1;
    size_t cb = sizeof(cCpus);
    int rc = sysctlbyname("hw.physicalcpu", &cCpus, &cb, NULL, 0);
    if (rc != -1 && cCpus >= 1)
        return cCpus;
    AssertFailed();
    return 1;
}


#if defined(RT_ARCH_ARM64)
RTDECL(RTCPUID) RTMpCpuId(void)
{
    /* xnu-7195.50.7.100.1/osfmk/arm64/start.s and machine_routines.c sets TPIDRRO_EL0
       to the cpu_data_t::cpu_id value. */
    uint64_t u64Ret;
    __asm__ __volatile__("mrs %0,TPIDRRO_EL0\n\t" : "=r" (u64Ret));
    return (RTCPUID)u64Ret;
}
#elif defined(RT_ARCH_ARM32)
RTDECL(RTCPUID) RTMpCpuId(void)
{
    /* xnu-7195.50.7.100.1/osfmk/arm/start.s and machine_routines.c sets TPIDRURO
       to the cpu_data_t::cpu_id value. */
    uint32_t u32Ret;
    __asm__ __volatile__("mrs p15, 0, %0, c13, c0, 3\n\t" : "=r" (u32Ret));
    return (RTCPUID)u32Ret;
}
#endif


RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    return idCpu < RTCPUSET_MAX_CPUS && idCpu < rtMpDarwinMaxLogicalCpus() ? idCpu : -1;
}


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    return (unsigned)iCpu < rtMpDarwinMaxLogicalCpus() ? iCpu : NIL_RTCPUID;
}


RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    return rtMpDarwinMaxLogicalCpus() - 1;
}


RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
#if 0
    return RTMpIsCpuPossible(idCpu);
#else
    /** @todo proper ring-3 support on darwin, see @bugref{3014}. */
    natural_t              cCpus;
    processor_basic_info_t paInfo;
    mach_msg_type_number_t cInfo;
    kern_return_t krc = host_processor_info(mach_host_self(), PROCESSOR_BASIC_INFO,
                                            &cCpus, (processor_info_array_t*)&paInfo, &cInfo);
    AssertReturn(krc == KERN_SUCCESS, true);
    bool const fIsOnline = idCpu < cCpus ? paInfo[idCpu].running : false;
    vm_deallocate(mach_task_self(), (vm_address_t)paInfo, cInfo * sizeof(paInfo[0]));
    return fIsOnline;
#endif
}


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    return idCpu != NIL_RTCPUID
        && idCpu < rtMpDarwinMaxLogicalCpus();
}


RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet)
{
#if 0
    RTCPUID cCpus = rtMpDarwinMaxLogicalCpus();
    return RTCpuSetFromU64(RT_BIT_64(cCpus) - 1);

#else
    RTCpuSetEmpty(pSet);
    RTCPUID cMax = rtMpDarwinMaxLogicalCpus();
    for (RTCPUID idCpu = 0; idCpu < cMax; idCpu++)
        if (RTMpIsCpuPossible(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    return pSet;
#endif
}


RTDECL(RTCPUID) RTMpGetCount(void)
{
    return rtMpDarwinMaxLogicalCpus();
}


RTDECL(RTCPUID) RTMpGetCoreCount(void)
{
    return rtMpDarwinMaxPhysicalCpus();
}


RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
    RTCpuSetEmpty(pSet);
#if 0
    RTCPUID cMax = rtMpDarwinMaxLogicalCpus();
    for (RTCPUID idCpu = 0; idCpu < cMax; idCpu++)
        if (RTMpIsCpuOnline(idCpu))
            RTCpuSetAdd(pSet, idCpu);
#else
    natural_t              cCpus  = 0;
    processor_basic_info_t paInfo = NULL;
    mach_msg_type_number_t cInfo  = 0;
    kern_return_t krc = host_processor_info(mach_host_self(), PROCESSOR_BASIC_INFO,
                                            &cCpus, (processor_info_array_t *)&paInfo, &cInfo);
    AssertReturn(krc == KERN_SUCCESS, pSet);

    AssertStmt(cCpus <= RTCPUSET_MAX_CPUS, cCpus = RTCPUSET_MAX_CPUS);
    for (natural_t idCpu = 0; idCpu < cCpus; idCpu++)
        if (paInfo[idCpu].running)
            RTCpuSetAdd(pSet, idCpu);

    vm_deallocate(mach_task_self(), (vm_address_t)paInfo, cInfo * sizeof(paInfo[0]));
#endif
    return pSet;
}


RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
    RTCPUSET Set;
    RTMpGetOnlineSet(&Set);
    return RTCpuSetCount(&Set);
}


RTDECL(RTCPUID) RTMpGetOnlineCoreCount(void)
{
    return rtMpDarwinOnlinePhysicalCpus();
}


RTDECL(uint32_t) RTMpGetCurFrequency(RTCPUID idCpu)
{
    /** @todo figure out how to get the current cpu speed on darwin. Have to
     *  check what powermanagement does.  The powermetrics uses a private
     *  IOReportXxxx interface and *seems* (guessing) to calculate the frequency
     *  based on the frequency distribution over the last report period...  This
     *  means that it's not really an suitable API for here.  */
    NOREF(idCpu);
    return 0;
}


/**
 * Worker for RTMpGetMaxFrequency.
 * @returns Non-zero frequency in MHz on success, 0 on failure.
 */
static uint32_t rtMpDarwinGetMaxFrequencyFromIOService(io_service_t hCpu)
{
    io_struct_inband_t  Buf = {0};
    uint32_t            cbActual = sizeof(Buf);
    kern_return_t krc = IORegistryEntryGetProperty(hCpu, "clock-frequency", Buf, &cbActual);
    Log2(("rtMpDarwinGetMaxFrequencyFromIOService: krc=%d; cbActual=%#x %.16Rhxs\n", krc, cbActual, Buf));
    if (krc == kIOReturnSuccess)
    {
        switch (cbActual)
        {
            case sizeof(uint32_t):
                return RT_BE2H_U32(*(uint32_t *)Buf) / 1000;
            case sizeof(uint64_t):
                AssertFailed();
                return RT_BE2H_U64(*(uint64_t *)Buf) / 1000;
            default:
                AssertFailed();
        }
    }
    return 0;
}


RTDECL(uint32_t) RTMpGetMaxFrequency(RTCPUID idCpu)
{
    if (!RTMpIsCpuOnline(idCpu))
        return 0;

    /*
     * Try the 'hw.cpufrequency_max' one.
     */
    uint64_t CpuFrequencyMax = 0;
    size_t cb = sizeof(CpuFrequencyMax);
    int rc = sysctlbyname("hw.cpufrequency_max", &CpuFrequencyMax, &cb, NULL, 0);
    if (!rc)
        return (CpuFrequencyMax + 999999) / 1000000;

    /*
     * Use the deprecated one.
     */
    int aiMib[2];
    aiMib[0] = CTL_HW;
    aiMib[1] = HW_CPU_FREQ;
    int iDeprecatedFrequency = -1;
    cb = sizeof(iDeprecatedFrequency);
    rc = sysctl(aiMib, RT_ELEMENTS(aiMib), &iDeprecatedFrequency, &cb, NULL, 0);
    if (rc != -1 && iDeprecatedFrequency >= 1)
        return iDeprecatedFrequency;

    /*
     * The above does not work for Apple M1 / xnu 20.1.0, so go look at the I/O registry instead.
     *
     * A sample ARM layout:
     *  | +-o cpu1@1  <class IOPlatformDevice, id 0x100000110, registered, matched, active, busy 0 (182 ms), retain 8>
     *  | | +-o AppleARMCPU  <class AppleARMCPU, id 0x10000021b, registered, matched, active, busy 0 (1 ms), retain 6>
     *  | +-o cpu2@2  <class IOPlatformDevice, id 0x100000111, registered, matched, active, busy 0 (175 ms), retain 8>
     *  | | +-o AppleARMCPU  <class AppleARMCPU, id 0x10000021c, registered, matched, active, busy 0 (3 ms), retain 6>
     *  | +-o cpu3@3  <class IOPlatformDevice, id 0x100000112, registered, matched, active, busy 0 (171 ms), retain 8>
     *  | | +-o AppleARMCPU  <class AppleARMCPU, id 0x10000021d, registered, matched, active, busy 0 (1 ms), retain 6>
     *  | +-o cpu4@100  <class IOPlatformDevice, id 0x100000113, registered, matched, active, busy 0 (171 ms), retain 8>
     *  | | +-o AppleARMCPU  <class AppleARMCPU, id 0x10000021e, registered, matched, active, busy 0 (1 ms), retain 6>
     *  | +-o cpu5@101  <class IOPlatformDevice, id 0x100000114, registered, matched, active, busy 0 (179 ms), retain 8>
     *  | | +-o AppleARMCPU  <class AppleARMCPU, id 0x10000021f, registered, matched, active, busy 0 (9 ms), retain 6>
     *  | +-o cpu6@102  <class IOPlatformDevice, id 0x100000115, registered, matched, active, busy 0 (172 ms), retain 8>
     *  | | +-o AppleARMCPU  <class AppleARMCPU, id 0x100000220, registered, matched, active, busy 0 (1 ms), retain 6>
     *  | +-o cpu7@103  <class IOPlatformDevice, id 0x100000116, registered, matched, active, busy 0 (175 ms), retain 8>
     *  | | +-o AppleARMCPU  <class AppleARMCPU, id 0x100000221, registered, matched, active, busy 0 (5 ms), retain 6>
     *  | +-o cpus  <class IOPlatformDevice, id 0x10000010e, registered, matched, active, busy 0 (12 ms), retain 15>
     *
     */

#if 1 /* simpler way to get at it inspired by powermetrics, this is also used
         in the arm version of RTMpGetDescription. */
    /* Assume names on the form "cpu<N>" are only for CPUs. */
    char szCpuPath[64];
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    RTStrPrintf(szCpuPath, sizeof(szCpuPath), "IODeviceTree:/cpus/CPU%X", idCpu);
# else
    RTStrPrintf(szCpuPath, sizeof(szCpuPath), "IODeviceTree:/cpus/cpu%x", idCpu); /** @todo Hex? M1 Max only has 10 cores... */
# endif

    RT_GCC_NO_WARN_DEPRECATED_BEGIN
    io_registry_entry_t hIoRegEntry = IORegistryEntryFromPath(kIOMasterPortDefault, szCpuPath); /* kIOMasterPortDefault: Deprecated since 12.0. */
    RT_GCC_NO_WARN_DEPRECATED_END

    if (hIoRegEntry != MACH_PORT_NULL)
    {
        uint32_t uCpuFrequency = rtMpDarwinGetMaxFrequencyFromIOService(hIoRegEntry);
        IOObjectRelease(hIoRegEntry);
        if (uCpuFrequency)
            return uCpuFrequency;
    }

#else
    /* Assume names on the form "cpu<N>" are only for CPUs. */
    char szCpuName[32];
    RTStrPrintf(szCpuName, sizeof(szCpuName), "cpu%u", idCpu);
    CFMutableDictionaryRef hMatchingDict = IOServiceNameMatching(szCpuName);
    AssertReturn(hMatchingDict, 0);

    /* Just get the first one. */
    io_object_t hCpu = IOServiceGetMatchingService(kIOMasterPortDefault, hMatchingDict);
    if (hCpu != 0)
    {
        uint32_t uCpuFrequency = rtMpDarwinGetMaxFrequencyFromIOService(hCpu);
        IOObjectRelease(hCpu);
        if (uCpuFrequency)
            return uCpuFrequency;
    }

# if 1 /* Just in case... */
    /* Create a matching dictionary for searching for CPU services in the IOKit. */
#  if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
    hMatchingDict = IOServiceMatching("AppleARMCPU");
#  elif defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    hMatchingDict = IOServiceMatching("AppleACPICPU");
#  else
#   error "Port me!"
#  endif
    AssertReturn(hMatchingDict, 0);

    /* Perform the search and get a collection of Apple CPU services. */
    io_iterator_t hCpuServices = IO_OBJECT_NULL;
    IOReturn irc = IOServiceGetMatchingServices(kIOMasterPortDefault, hMatchingDict, &hCpuServices);
    AssertMsgReturn(irc == kIOReturnSuccess, ("irc=%d\n", irc), 0);
    hMatchingDict = NULL; /* the reference is consumed by IOServiceGetMatchingServices. */

    /* Enumerate the matching services. */
    uint32_t    uCpuFrequency = 0;
    io_object_t hCurCpu;
    while (uCpuFrequency == 0 && (hCurCpu = IOIteratorNext(hCpuServices)) != IO_OBJECT_NULL)
    {
        io_object_t hParent = (io_object_t)0;
        irc = IORegistryEntryGetParentEntry(hCurCpu, kIOServicePlane, &hParent);
        if (irc == kIOReturnSuccess && hParent)
        {
            uCpuFrequency = rtMpDarwinGetMaxFrequencyFromIOService(hParent);
            IOObjectRelease(hParent);
        }
        IOObjectRelease(hCurCpu);
    }
    IOObjectRelease(hCpuServices);
# endif
#endif
    AssertFailed();
    return 0;
}

