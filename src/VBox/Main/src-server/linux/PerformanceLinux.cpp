/* $Id: PerformanceLinux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Linux-specific Performance Classes implementation.
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
#include <stdio.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <mntent.h>
#include <iprt/alloc.h>
#include <iprt/cdefs.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/mp.h>
#include <iprt/linux/sysfs.h>

#include <map>
#include <vector>

#include "LoggingNew.h"
#include "Performance.h"

#define VBOXVOLINFO_EXE_NAME "VBoxVolInfo"

namespace pm {

class CollectorLinux : public CollectorHAL
{
public:
    CollectorLinux();
    virtual int preCollect(const CollectorHints& hints, uint64_t /* iTick */);
    virtual int getHostMemoryUsage(ULONG *total, ULONG *used, ULONG *available);
    virtual int getHostFilesystemUsage(const char *name, ULONG *total, ULONG *used, ULONG *available);
    virtual int getHostDiskSize(const char *name, uint64_t *size);
    virtual int getProcessMemoryUsage(RTPROCESS process, ULONG *used);

    virtual int getRawHostCpuLoad(uint64_t *user, uint64_t *kernel, uint64_t *idle);
    virtual int getRawHostNetworkLoad(const char *name, uint64_t *rx, uint64_t *tx);
    virtual int getRawHostDiskLoad(const char *name, uint64_t *disk_ms, uint64_t *total_ms);
    virtual int getRawProcessCpuLoad(RTPROCESS process, uint64_t *user, uint64_t *kernel, uint64_t *total);

    virtual int getDiskListByFs(const char *name, DiskList& listUsage, DiskList& listLoad);
private:
    virtual int _getRawHostCpuLoad();
    int getRawProcessStats(RTPROCESS process, uint64_t *cpuUser, uint64_t *cpuKernel, ULONG *memPagesUsed);
    void getDiskName(char *pszDiskName, size_t cbDiskName, const char *pszDevName, bool fTrimDigits);
    void addVolumeDependencies(const char *pcszVolume, DiskList& listDisks);
    void addRaidDisks(const char *pcszDevice, DiskList& listDisks);
    char *trimTrailingDigits(char *pszName);
    char *trimNewline(char *pszName);

    struct VMProcessStats
    {
        uint64_t cpuUser;
        uint64_t cpuKernel;
        ULONG    pagesUsed;
    };

    typedef std::map<RTPROCESS, VMProcessStats> VMProcessMap;

    VMProcessMap mProcessStats;
    uint64_t     mUser, mKernel, mIdle;
    uint64_t     mSingleUser, mSingleKernel, mSingleIdle;
    uint32_t     mHZ;
    ULONG        mTotalRAM;
};

CollectorHAL *createHAL()
{
    return new CollectorLinux();
}

// Collector HAL for Linux

CollectorLinux::CollectorLinux()
{
    long hz = sysconf(_SC_CLK_TCK);
    if (hz == -1)
    {
        LogRel(("CollectorLinux failed to obtain HZ from kernel, assuming 100.\n"));
        mHZ = 100;
    }
    else
        mHZ = (uint32_t)hz;
    LogFlowThisFunc(("mHZ=%u\n", mHZ));

    uint64_t cb;
    int vrc = RTSystemQueryTotalRam(&cb);
    if (RT_FAILURE(vrc))
        mTotalRAM = 0;
    else
        mTotalRAM = (ULONG)(cb / 1024);
}

int CollectorLinux::preCollect(const CollectorHints& hints, uint64_t /* iTick */)
{
    std::vector<RTPROCESS> processes;
    hints.getProcesses(processes);

    std::vector<RTPROCESS>::iterator it;
    for (it = processes.begin(); it != processes.end(); ++it)
    {
        VMProcessStats vmStats;
        int vrc = getRawProcessStats(*it, &vmStats.cpuUser, &vmStats.cpuKernel, &vmStats.pagesUsed);
        /* On failure, do NOT stop. Just skip the entry. Having the stats for
         * one (probably broken) process frozen/zero is a minor issue compared
         * to not updating many process stats and the host cpu stats. */
        if (RT_SUCCESS(vrc))
            mProcessStats[*it] = vmStats;
    }
    if (hints.isHostCpuLoadCollected() || !mProcessStats.empty())
    {
        _getRawHostCpuLoad();
    }
    return VINF_SUCCESS;
}

int CollectorLinux::_getRawHostCpuLoad()
{
    int vrc = VINF_SUCCESS;
    long long unsigned uUser, uNice, uKernel, uIdle, uIowait, uIrq, uSoftirq;
    FILE *f = fopen("/proc/stat", "r");

    if (f)
    {
        char szBuf[128];
        if (fgets(szBuf, sizeof(szBuf), f))
        {
            if (sscanf(szBuf, "cpu %llu %llu %llu %llu %llu %llu %llu",
                       &uUser, &uNice, &uKernel, &uIdle, &uIowait,
                       &uIrq, &uSoftirq) == 7)
            {
                mUser   = uUser + uNice;
                mKernel = uKernel + uIrq + uSoftirq;
                mIdle   = uIdle + uIowait;
            }
            /* Try to get single CPU stats. */
            if (fgets(szBuf, sizeof(szBuf), f))
            {
                if (sscanf(szBuf, "cpu0 %llu %llu %llu %llu %llu %llu %llu",
                           &uUser, &uNice, &uKernel, &uIdle, &uIowait,
                           &uIrq, &uSoftirq) == 7)
                {
                    mSingleUser   = uUser + uNice;
                    mSingleKernel = uKernel + uIrq + uSoftirq;
                    mSingleIdle   = uIdle + uIowait;
                }
                else
                {
                    /* Assume that this is not an SMP system. */
                    Assert(RTMpGetCount() == 1);
                    mSingleUser   = mUser;
                    mSingleKernel = mKernel;
                    mSingleIdle   = mIdle;
                }
            }
            else
                vrc = VERR_FILE_IO_ERROR;
        }
        else
            vrc = VERR_FILE_IO_ERROR;
        fclose(f);
    }
    else
        vrc = VERR_ACCESS_DENIED;

    return vrc;
}

int CollectorLinux::getRawHostCpuLoad(uint64_t *user, uint64_t *kernel, uint64_t *idle)
{
    *user   = mUser;
    *kernel = mKernel;
    *idle   = mIdle;
    return VINF_SUCCESS;
}

int CollectorLinux::getRawProcessCpuLoad(RTPROCESS process, uint64_t *user, uint64_t *kernel, uint64_t *total)
{
    VMProcessMap::const_iterator it = mProcessStats.find(process);

    if (it == mProcessStats.end())
    {
        Log (("No stats pre-collected for process %x\n", process));
        return VERR_INTERNAL_ERROR;
    }
    *user   = it->second.cpuUser;
    *kernel = it->second.cpuKernel;
    *total  = mUser + mKernel + mIdle;
    return VINF_SUCCESS;
}

int CollectorLinux::getHostMemoryUsage(ULONG *total, ULONG *used, ULONG *available)
{
    AssertReturn(mTotalRAM, VERR_INTERNAL_ERROR);
    uint64_t cb;
    int vrc = RTSystemQueryAvailableRam(&cb);
    if (RT_SUCCESS(vrc))
    {
        *total = mTotalRAM;
        *available = (ULONG)(cb / 1024);
        *used = *total - *available;
    }
    return vrc;
}

int CollectorLinux::getHostFilesystemUsage(const char *path, ULONG *total, ULONG *used, ULONG *available)
{
    struct statvfs stats;

    if (statvfs(path, &stats) == -1)
    {
        LogRel(("Failed to collect %s filesystem usage: errno=%d.\n", path, errno));
        return VERR_ACCESS_DENIED;
    }
    uint64_t cbBlock = stats.f_frsize ? stats.f_frsize : stats.f_bsize;
    *total = (ULONG)(cbBlock * stats.f_blocks / _1M);
    *used  = (ULONG)(cbBlock * (stats.f_blocks - stats.f_bfree) / _1M);
    *available = (ULONG)(cbBlock * stats.f_bavail / _1M);

    return VINF_SUCCESS;
}

int CollectorLinux::getHostDiskSize(const char *pszFile, uint64_t *size)
{
    char *pszPath = NULL;

    RTStrAPrintf(&pszPath, "/sys/block/%s/size", pszFile);
    Assert(pszPath);

    int vrc = VINF_SUCCESS;
    if (!RTLinuxSysFsExists(pszPath))
        vrc = VERR_FILE_NOT_FOUND;
    else
    {
        int64_t cSize = 0;
        vrc = RTLinuxSysFsReadIntFile(0, &cSize, pszPath);
        if (RT_SUCCESS(vrc))
            *size = cSize * 512;
    }
    RTStrFree(pszPath);
    return vrc;
}

int CollectorLinux::getProcessMemoryUsage(RTPROCESS process, ULONG *used)
{
    VMProcessMap::const_iterator it = mProcessStats.find(process);

    if (it == mProcessStats.end())
    {
        Log (("No stats pre-collected for process %x\n", process));
        return VERR_INTERNAL_ERROR;
    }
    *used = it->second.pagesUsed * (PAGE_SIZE / 1024);
    return VINF_SUCCESS;
}

int CollectorLinux::getRawProcessStats(RTPROCESS process, uint64_t *cpuUser, uint64_t *cpuKernel, ULONG *memPagesUsed)
{
    int vrc = VINF_SUCCESS;
    char *pszName;
    pid_t pid2;
    char c;
    int iTmp;
    long long unsigned int u64Tmp;
    unsigned uTmp;
    unsigned long ulTmp;
    signed long ilTmp;
    ULONG u32user, u32kernel;
    char buf[80]; /** @todo this should be tied to max allowed proc name. */

    RTStrAPrintf(&pszName, "/proc/%d/stat", process);
    FILE *f = fopen(pszName, "r");
    RTStrFree(pszName);

    if (f)
    {
        if (fscanf(f, "%d %79s %c %d %d %d %d %d %u %lu %lu %lu %lu %u %u "
                      "%ld %ld %ld %ld %ld %ld %llu %lu %u",
                   &pid2, buf, &c, &iTmp, &iTmp, &iTmp, &iTmp, &iTmp, &uTmp,
                   &ulTmp, &ulTmp, &ulTmp, &ulTmp, &u32user, &u32kernel,
                   &ilTmp, &ilTmp, &ilTmp, &ilTmp, &ilTmp, &ilTmp, &u64Tmp,
                   &ulTmp, memPagesUsed) == 24)
        {
            Assert((pid_t)process == pid2);
            *cpuUser   = u32user;
            *cpuKernel = u32kernel;
        }
        else
            vrc = VERR_FILE_IO_ERROR;
        fclose(f);
    }
    else
        vrc = VERR_ACCESS_DENIED;

    return vrc;
}

int CollectorLinux::getRawHostNetworkLoad(const char *pszFile, uint64_t *rx, uint64_t *tx)
{
    char szIfName[/*IFNAMSIZ*/ 16 + 36];

    RTStrPrintf(szIfName, sizeof(szIfName), "/sys/class/net/%s/statistics/rx_bytes", pszFile);
    if (!RTLinuxSysFsExists(szIfName))
        return VERR_FILE_NOT_FOUND;

    int64_t cSize = 0;
    int vrc = RTLinuxSysFsReadIntFile(0, &cSize, szIfName);
    if (RT_FAILURE(vrc))
        return vrc;

    *rx = cSize;

    RTStrPrintf(szIfName, sizeof(szIfName), "/sys/class/net/%s/statistics/tx_bytes", pszFile);
    if (!RTLinuxSysFsExists(szIfName))
        return VERR_FILE_NOT_FOUND;

    vrc = RTLinuxSysFsReadIntFile(0, &cSize, szIfName);
    if (RT_FAILURE(vrc))
        return vrc;

    *tx = cSize;
    return VINF_SUCCESS;
}

int CollectorLinux::getRawHostDiskLoad(const char *name, uint64_t *disk_ms, uint64_t *total_ms)
{
#if 0
    int vrc = VINF_SUCCESS;
    char szIfName[/*IFNAMSIZ*/ 16 + 36];
    long long unsigned int u64Busy, tmp;

    RTStrPrintf(szIfName, sizeof(szIfName), "/sys/class/block/%s/stat", name);
    FILE *f = fopen(szIfName, "r");
    if (f)
    {
        if (fscanf(f, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &u64Busy, &tmp) == 11)
        {
            *disk_ms   = u64Busy;
            *total_ms  = (uint64_t)(mSingleUser + mSingleKernel + mSingleIdle) * 1000 / mHZ;
        }
        else
            vrc = VERR_FILE_IO_ERROR;
        fclose(f);
    }
    else
        vrc = VERR_ACCESS_DENIED;
#else
    int vrc = VERR_MISSING;
    FILE *f = fopen("/proc/diskstats", "r");
    if (f)
    {
        char szBuf[128];
        while (fgets(szBuf, sizeof(szBuf), f))
        {
            char *pszBufName = szBuf;
            while (*pszBufName == ' ')         ++pszBufName; /* Skip spaces */
            while (RT_C_IS_DIGIT(*pszBufName)) ++pszBufName; /* Skip major */
            while (*pszBufName == ' ')         ++pszBufName; /* Skip spaces */
            while (RT_C_IS_DIGIT(*pszBufName)) ++pszBufName; /* Skip minor */
            while (*pszBufName == ' ')         ++pszBufName; /* Skip spaces */

            char *pszBufData = strchr(pszBufName, ' ');
            if (!pszBufData)
            {
                LogRel(("CollectorLinux::getRawHostDiskLoad() failed to parse disk stats: %s\n", szBuf));
                continue;
            }
            *pszBufData++ = '\0';
            if (!strcmp(name, pszBufName))
            {
                long long unsigned int u64Busy, tmp;

                if (sscanf(pszBufData, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                           &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &u64Busy, &tmp) == 11)
                {
                    *disk_ms   = u64Busy;
                    *total_ms  = (uint64_t)(mSingleUser + mSingleKernel + mSingleIdle) * 1000 / mHZ;
                    vrc = VINF_SUCCESS;
                }
                else
                    vrc = VERR_FILE_IO_ERROR;
                break;
            }
        }
        fclose(f);
    }
#endif

    return vrc;
}

char *CollectorLinux::trimNewline(char *pszName)
{
    size_t cbName = strlen(pszName);
    if (cbName == 0)
        return pszName;

    char *pszEnd = pszName + cbName - 1;
    while (pszEnd > pszName && *pszEnd == '\n')
        pszEnd--;
    pszEnd[1] = '\0';

    return pszName;
}

char *CollectorLinux::trimTrailingDigits(char *pszName)
{
    size_t cbName = strlen(pszName);
    if (cbName == 0)
        return pszName;

    char *pszEnd = pszName + cbName - 1;
    while (pszEnd > pszName && (RT_C_IS_DIGIT(*pszEnd) || *pszEnd == '\n'))
        pszEnd--;
    pszEnd[1] = '\0';

    return pszName;
}

/**
 * Use the partition name to get the name of the disk. Any path component is stripped.
 * if fTrimDigits is true, trailing digits are stripped as well, for example '/dev/sda5'
 * is converted to 'sda'.
 *
 * @param   pszDiskName     Where to store the name of the disk.
 * @param   cbDiskName      The size of the buffer pszDiskName points to.
 * @param   pszDevName      The device name used to get the disk name.
 * @param   fTrimDigits     Trim trailing digits (e.g. /dev/sda5)
 */
void CollectorLinux::getDiskName(char *pszDiskName, size_t cbDiskName, const char *pszDevName, bool fTrimDigits)
{
    unsigned cbName = 0;
    size_t cbDevName = strlen(pszDevName);
    const char *pszEnd = pszDevName + cbDevName - 1;
    if (fTrimDigits)
        while (pszEnd > pszDevName && RT_C_IS_DIGIT(*pszEnd))
            pszEnd--;
    while (pszEnd > pszDevName && *pszEnd != '/')
    {
        cbName++;
        pszEnd--;
    }
    RTStrCopy(pszDiskName, RT_MIN(cbName + 1, cbDiskName), pszEnd + 1);
}

void CollectorLinux::addRaidDisks(const char *pcszDevice, DiskList& listDisks)
{
    FILE *f = fopen("/proc/mdstat", "r");
    if (f)
    {
        char szBuf[128];
        while (fgets(szBuf, sizeof(szBuf), f))
        {
            char *pszBufName = szBuf;

            char *pszBufData = strchr(pszBufName, ' ');
            if (!pszBufData)
            {
                LogRel(("CollectorLinux::addRaidDisks() failed to parse disk stats: %s\n", szBuf));
                continue;
            }
            *pszBufData++ = '\0';
            if (!strcmp(pcszDevice, pszBufName))
            {
                while (*pszBufData == ':')         ++pszBufData; /* Skip delimiter */
                while (*pszBufData == ' ')         ++pszBufData; /* Skip spaces */
                while (RT_C_IS_ALNUM(*pszBufData)) ++pszBufData; /* Skip status */
                while (*pszBufData == ' ')         ++pszBufData; /* Skip spaces */
                while (RT_C_IS_ALNUM(*pszBufData)) ++pszBufData; /* Skip type */

                while (*pszBufData != '\0')
                {
                    while (*pszBufData == ' ') ++pszBufData; /* Skip spaces */
                    char *pszDisk = pszBufData;
                    while (RT_C_IS_ALPHA(*pszBufData))
                        ++pszBufData;
                    if (*pszBufData)
                    {
                        *pszBufData++ = '\0';
                        listDisks.push_back(RTCString(pszDisk));
                        while (*pszBufData != '\0' && *pszBufData != ' ')
                            ++pszBufData;
                    }
                    else
                        listDisks.push_back(RTCString(pszDisk));
                }
                break;
            }
        }
        fclose(f);
    }
}

void CollectorLinux::addVolumeDependencies(const char *pcszVolume, DiskList& listDisks)
{
    /** @todo r=bird: This is presumptive and will misbehave if someone puts VBox
     *        in directory which path contains spaces or other problematic
     *        characters.  This is one of the reasons to avoid popen(). */
    static const char s_szSlashExeNameSpace[] = "/" VBOXVOLINFO_EXE_NAME " ";
    size_t const cchVolume = strlen(pcszVolume);
    char szVolInfo[RTPATH_MAX];
    int vrc = RTPathAppPrivateArch(szVolInfo, sizeof(szVolInfo) - sizeof(s_szSlashExeNameSpace) - cchVolume);
    if (RT_FAILURE(vrc))
    {
        LogRel(("VolInfo: Failed to get program path, vrc=%Rrc\n", vrc));
        /** @todo r=bird: inconsistent failure behaviour. if popen fails, volume is
         *        pushed onto the list, while here it isn't. */
        return;
    }
    memcpy(mempcpy(strchr(szVolInfo, '\0'), s_szSlashExeNameSpace, sizeof(s_szSlashExeNameSpace) - 1),
           pcszVolume, cchVolume + 1);

    FILE *fp = popen(szVolInfo, "r");
    if (fp)
    {
        char szBuf[128];

        while (fgets(szBuf, sizeof(szBuf), fp))
            if (strncmp(szBuf, RT_STR_TUPLE("dm-")))
                listDisks.push_back(RTCString(trimTrailingDigits(szBuf)));
            else
                listDisks.push_back(RTCString(trimNewline(szBuf)));

        pclose(fp);
    }
    else
        listDisks.push_back(RTCString(pcszVolume));
}

int CollectorLinux::getDiskListByFs(const char *pszPath, DiskList& listUsage, DiskList& listLoad)
{
    FILE *mtab = setmntent("/etc/mtab", "r");
    if (mtab)
    {
        struct mntent *mntent;
        while ((mntent = getmntent(mtab)))
        {
            /* Skip rootfs entry, there must be another root mount. */
            if (strcmp(mntent->mnt_fsname, "rootfs") == 0)
                continue;
            if (strcmp(pszPath, mntent->mnt_dir) == 0)
            {
                char szDevName[128];
                char szFsName[1024];
                /* Try to resolve symbolic link if necessary. Yes, we access the file system here! */
                int vrc = RTPathReal(mntent->mnt_fsname, szFsName, sizeof(szFsName));
                if (RT_FAILURE(vrc))
                    continue; /* something got wrong, just ignore this path */
                /* check against the actual mtab entry, NOT the real path as /dev/mapper/xyz is
                 * often a symlink to something else */
                if (!strncmp(mntent->mnt_fsname, RT_STR_TUPLE("/dev/mapper")))
                {
                    /* LVM */
                    getDiskName(szDevName, sizeof(szDevName), mntent->mnt_fsname, false /*=fTrimDigits*/);
                    addVolumeDependencies(szDevName, listUsage);
                    listLoad = listUsage;
                }
                else if (!strncmp(szFsName, RT_STR_TUPLE("/dev/md")))
                {
                    /* Software RAID */
                    getDiskName(szDevName, sizeof(szDevName), szFsName, false /*=fTrimDigits*/);
                    listUsage.push_back(RTCString(szDevName));
                    addRaidDisks(szDevName, listLoad);
                }
                else
                {
                    /* Plain disk partition. Trim the trailing digits to get the drive name */
                    getDiskName(szDevName, sizeof(szDevName), szFsName, true /*=fTrimDigits*/);
                    listUsage.push_back(RTCString(szDevName));
                    listLoad.push_back(RTCString(szDevName));
                }
                if (listUsage.empty() || listLoad.empty())
                {
                    LogRel(("Failed to retrive disk info: getDiskName(%s) --> %s\n",
                           mntent->mnt_fsname, szDevName));
                }
                break;
            }
        }
        endmntent(mtab);
    }
    return VINF_SUCCESS;
}

}

