/* $Id: fs-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File System, Linux.
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
#define LOG_GROUP RTLOGGROUP_FS
#include <sys/statvfs.h>
#include <errno.h>
#include <stdio.h>
#ifdef RT_OS_LINUX
# include <mntent.h>
#endif
#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
# include <sys/mount.h>
#endif

#include <iprt/fs.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include "internal/fs.h"
#include "internal/path.h"



RTR3DECL(int) RTFsQuerySizes(const char *pszFsPath, RTFOFF *pcbTotal, RTFOFF *pcbFree,
                             uint32_t *pcbBlock, uint32_t *pcbSector)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertReturn(*pszFsPath != '\0', VERR_INVALID_PARAMETER);

    /*
     * Convert the path and query the information.
     */
    char const *pszNativeFsPath;
    int rc = rtPathToNative(&pszNativeFsPath, pszFsPath, NULL);
    if (RT_SUCCESS(rc))
    {
        /** @todo I'm not quite sure if statvfs was properly specified by SuS, I have to check my own
         * implementation and FreeBSD before this can eventually be promoted to posix. */
        struct statvfs StatVFS;
        RT_ZERO(StatVFS);
        if (!statvfs(pszNativeFsPath, &StatVFS))
        {
            /*
             * Calc the returned values.
             */
            if (pcbTotal)
                *pcbTotal = (RTFOFF)StatVFS.f_blocks * StatVFS.f_frsize;
            if (pcbFree)
                *pcbFree = (RTFOFF)StatVFS.f_bavail * StatVFS.f_frsize;
            if (pcbBlock)
                *pcbBlock = StatVFS.f_frsize;
            /* no idea how to get the sector... */
            if (pcbSector)
                *pcbSector = 512;
        }
        else
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativeFsPath, pszFsPath);
    }

    LogFlow(("RTFsQuerySizes(%p:{%s}, %p:{%RTfoff}, %p:{%RTfoff}, %p:{%RX32}, %p:{%RX32}): returns %Rrc\n",
             pszFsPath, pszFsPath, pcbTotal, pcbTotal ? *pcbTotal : 0, pcbFree, pcbFree ? *pcbFree : 0,
             pcbBlock, pcbBlock ? *pcbBlock : 0, pcbSector, pcbSector ? *pcbSector : 0, rc));
    return rc;
}


RTR3DECL(int) RTFsQuerySerial(const char *pszFsPath, uint32_t *pu32Serial)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertReturn(*pszFsPath != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pu32Serial, VERR_INVALID_POINTER);

    /*
     * Convert the path and query the stats.
     * We're simply return the device id.
     */
    char const *pszNativeFsPath;
    int rc = rtPathToNative(&pszNativeFsPath, pszFsPath, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat Stat;
        if (!stat(pszNativeFsPath, &Stat))
        {
            if (pu32Serial)
                *pu32Serial = (uint32_t)Stat.st_dev;
        }
        else
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativeFsPath, pszFsPath);
    }
    LogFlow(("RTFsQuerySerial(%p:{%s}, %p:{%RX32}: returns %Rrc\n",
             pszFsPath, pszFsPath, pu32Serial, pu32Serial ? *pu32Serial : 0, rc));
    return rc;
}


RTR3DECL(int) RTFsQueryProperties(const char *pszFsPath, PRTFSPROPERTIES pProperties)
{
    /*
     * Validate.
     */
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertReturn(*pszFsPath != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pProperties, VERR_INVALID_POINTER);

    /*
     * Convert the path and query the information.
     */
    char const *pszNativeFsPath;
    int rc = rtPathToNative(&pszNativeFsPath, pszFsPath, NULL);
    if (RT_SUCCESS(rc))
    {
        struct statvfs StatVFS;
        RT_ZERO(StatVFS);
        if (!statvfs(pszNativeFsPath, &StatVFS))
        {
            /*
             * Calc/fake the returned values.
             */
            pProperties->cbMaxComponent = StatVFS.f_namemax;
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
            pProperties->fCaseSensitive = false;
#else
            pProperties->fCaseSensitive = true;
#endif
            pProperties->fCompressed = false;
            pProperties->fFileCompression = false;
            pProperties->fReadOnly = !!(StatVFS.f_flag & ST_RDONLY);
            pProperties->fRemote = false;
            pProperties->fSupportsUnicode = true;
        }
        else
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativeFsPath, pszFsPath);
    }

    LogFlow(("RTFsQueryProperties(%p:{%s}, %p:{.cbMaxComponent=%u, .fReadOnly=%RTbool}): returns %Rrc\n",
             pszFsPath, pszFsPath, pProperties, pProperties->cbMaxComponent, pProperties->fReadOnly, rc));
    return rc;
}


RTR3DECL(bool) RTFsIsCaseSensitive(const char *pszFsPath)
{
    RT_NOREF_PV(pszFsPath);
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
    return false;
#else
    return true;
#endif
}


RTR3DECL(int) RTFsQueryType(const char *pszFsPath, PRTFSTYPE penmType)
{
    *penmType = RTFSTYPE_UNKNOWN;

    /*
     * Validate input.
     */
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertReturn(*pszFsPath, VERR_INVALID_PARAMETER);

    /*
     * Convert the path and query the stats.
     * We're simply return the device id.
     */
    char const *pszNativeFsPath;
    int rc = rtPathToNative(&pszNativeFsPath, pszFsPath, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat Stat;
        if (!stat(pszNativeFsPath, &Stat))
        {
#if defined(RT_OS_LINUX)
            FILE *mounted = setmntent("/proc/mounts", "r");
            if (!mounted)
                mounted = setmntent("/etc/mtab", "r");
            if (mounted)
            {
                char            szBuf[1024];
                struct stat     mntStat;
                struct mntent   mntEnt;
                while (getmntent_r(mounted, &mntEnt, szBuf, sizeof(szBuf)))
                {
                    if (!stat(mntEnt.mnt_dir, &mntStat))
                    {
                        if (mntStat.st_dev == Stat.st_dev)
                        {
                            if (!strcmp("ext4", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_EXT4;
                            else if (!strcmp("ext3", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_EXT3;
                            else if (!strcmp("ext2", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_EXT2;
                            else if (!strcmp("jfs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_JFS;
                            else if (!strcmp("xfs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_XFS;
                            else if (!strcmp("btrfs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_BTRFS;
                            else if (   !strcmp("vfat", mntEnt.mnt_type)
                                     || !strcmp("msdos", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_FAT;
                            else if (!strcmp("ntfs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_NTFS;
                            else if (!strcmp("hpfs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_HPFS;
                            else if (!strcmp("ufs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_UFS;
                            else if (!strcmp("tmpfs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_TMPFS;
                            else if (!strcmp("hfsplus", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_HFS;
                            else if (!strcmp("udf", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_UDF;
                            else if (!strcmp("iso9660", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_ISO9660;
                            else if (!strcmp("smbfs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_SMBFS;
                            else if (!strcmp("cifs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_CIFS;
                            else if (!strcmp("nfs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_NFS;
                            else if (!strcmp("nfs4", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_NFS;
                            else if (!strcmp("ocfs2", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_OCFS2;
                            else if (!strcmp("sysfs", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_SYSFS;
                            else if (!strcmp("proc", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_PROC;
                            else if (   !strcmp("fuse", mntEnt.mnt_type)
                                     || !strncmp("fuse.", mntEnt.mnt_type, 5)
                                     || !strcmp("fuseblk", mntEnt.mnt_type))
                                *penmType = RTFSTYPE_FUSE;
                            else
                            {
                                /* sometimes there are more than one entry for the same partition */
                                continue;
                            }
                            break;
                        }
                    }
                }
                endmntent(mounted);
            }

#elif defined(RT_OS_SOLARIS)
            /*
             * Home directories are normally loopback mounted in Solaris 11 (st_fstype=="lofs")
             * so statvfs(2) is needed to get the underlying file system information.
             */
            struct statvfs statvfsBuf;
            if (!statvfs(pszNativeFsPath, &statvfsBuf))
            {
                if (!strcmp("zfs", statvfsBuf.f_basetype))
                    *penmType = RTFSTYPE_ZFS;
                else if (!strcmp("ufs", statvfsBuf.f_basetype))
                    *penmType = RTFSTYPE_UFS;
                else if (!strcmp("nfs", statvfsBuf.f_basetype))
                    *penmType = RTFSTYPE_NFS;
            }

#elif defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
            struct statfs statfsBuf;
            if (!statfs(pszNativeFsPath, &statfsBuf))
            {
                if (!strcmp("hfs", statfsBuf.f_fstypename))
                    *penmType = RTFSTYPE_HFS;
                else if (!strcmp("apfs", statfsBuf.f_fstypename)) /** @todo verify apfs signature. */
                    *penmType = RTFSTYPE_APFS;
                else if (   !strcmp("fat", statfsBuf.f_fstypename)
                         || !strcmp("msdos", statfsBuf.f_fstypename))
                    *penmType = RTFSTYPE_FAT;
                else if (!strcmp("ntfs", statfsBuf.f_fstypename))
                    *penmType = RTFSTYPE_NTFS;
                else if (!strcmp("autofs", statfsBuf.f_fstypename))
                    *penmType = RTFSTYPE_AUTOFS;
                else if (!strcmp("devfs", statfsBuf.f_fstypename))
                    *penmType = RTFSTYPE_DEVFS;
                else if (!strcmp("nfs", statfsBuf.f_fstypename))
                    *penmType = RTFSTYPE_NFS;
                else if (!strcmp("ufs", statfsBuf.f_fstypename))
                    *penmType = RTFSTYPE_UFS;
                else if (!strcmp("zfs", statfsBuf.f_fstypename))
                    *penmType = RTFSTYPE_ZFS;
            }
            else
                rc = RTErrConvertFromErrno(errno);
#endif
        }
        else
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativeFsPath, pszFsPath);
    }

    return rc;
}
