/* $Id: RTFsMountpointsEnum-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File System, RTFsMountpointsEnum, POSIX.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#if defined(RT_OS_SOLARIS)
# include <sys/mnttab.h>
#endif

#include <iprt/fs.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include "internal/fs.h"
#include "internal/path.h"


RTR3DECL(int) RTFsMountpointsEnum(PFNRTFSMOUNTPOINTENUM pfnCallback, void *pvUser)
{
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    /* pvUser is optional. */

    int rc = VINF_SUCCESS;

#if defined(RT_OS_LINUX)
    FILE *pFile = setmntent("/proc/mounts", "r");
    if (!pFile)
        pFile = setmntent(_PATH_MOUNTED, "r");
    if (pFile)
    {
        struct mntent *pEntry;
        while ((pEntry = getmntent(pFile)) != NULL)
        {
            rc = pfnCallback(pEntry->mnt_dir, pvUser);
            if (RT_FAILURE(rc))
                break;
        }
        endmntent(pFile);
    }
    else
        rc = VERR_ACCESS_DENIED;
#elif defined(RT_OS_SOLARIS)
    FILE *pFile = fopen("/etc/mnttab", "r");
    if (pFile)
    {
        struct mnttab Entry;
        while (getmntent(pFile, &Entry) == 0)
        {
            rc = pfnCallback(Entry.mnt_mountp, pvUser);
            if (RT_FAILURE(rc))
                break;
        }
        fclose(pFile);
    }
    else
        rc = VERR_ACCESS_DENIED;
#elif defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
    struct statfs *pEntry;
    errno = 0;
    int cEntries = getmntinfo(&pEntry, MNT_NOWAIT);
    if (errno == 0)
    {
        for (; cEntries-- > 0; pEntry++)
        {
            rc = pfnCallback(pEntry->f_mntonname, pvUser);
            if (RT_FAILURE(rc))
                break;
        }
    }
    else
        rc = RTErrConvertFromErrno(errno);
#else
    rc = VERR_NOT_IMPLEMENTED;
#endif

    return rc;
}

