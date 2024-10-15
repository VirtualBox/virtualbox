/* $Id: fileio2-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O, POSIX, Part 2.
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
#define LOG_GROUP RTLOGGROUP_FILE

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _MSC_VER
# include <io.h>
# include <stdio.h>
#else
# include <unistd.h>
# include <sys/time.h>
#endif
#ifdef RT_OS_LINUX
# include <sys/file.h>
#endif
#if defined(RT_OS_OS2) && (!defined(__INNOTEK_LIBC__) || __INNOTEK_LIBC__ < 0x006)
# include <io.h>
#endif

#ifdef RT_OS_SOLARIS
# define futimes(filedes, timeval)   futimesat(filedes, NULL, timeval)
#endif

#ifdef RT_OS_HAIKU
# define USE_FUTIMENS
#endif

#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include "internal/file.h"
#include "internal/fs.h"
#include "internal/path.h"



RTR3DECL(int) RTFileQueryInfo(RTFILE hFile, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs)
{
    /*
     * Validate input.
     */
    AssertReturn(hFile != NIL_RTFILE, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pObjInfo, VERR_INVALID_PARAMETER);
    if (    enmAdditionalAttribs < RTFSOBJATTRADD_NOTHING
        ||  enmAdditionalAttribs > RTFSOBJATTRADD_LAST)
    {
        AssertMsgFailed(("Invalid enmAdditionalAttribs=%p\n", enmAdditionalAttribs));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Query file info.
     */
    struct stat Stat;
    if (fstat(RTFileToNative(hFile), &Stat))
    {
        int rc = RTErrConvertFromErrno(errno);
        Log(("RTFileQueryInfo(%RTfile,,%d): returns %Rrc\n", hFile, enmAdditionalAttribs, rc));
        return rc;
    }

    /*
     * Setup the returned data.
     */
    rtFsConvertStatToObjInfo(pObjInfo, &Stat, NULL, 0);

    /*
     * Requested attributes (we cannot provide anything actually).
     */
    switch (enmAdditionalAttribs)
    {
        case RTFSOBJATTRADD_NOTHING:
        case RTFSOBJATTRADD_UNIX:
            /* done */
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            rtFsObjInfoAttrSetUnixOwner(pObjInfo, Stat.st_uid);
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            rtFsObjInfoAttrSetUnixGroup(pObjInfo, Stat.st_gid);
            break;

        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_EASIZE;
            pObjInfo->Attr.u.EASize.cb            = 0;
            break;

        default:
            AssertMsgFailed(("Impossible!\n"));
            return VERR_INTERNAL_ERROR;
    }

    LogFlow(("RTFileQueryInfo(%RTfile,,%d): returns VINF_SUCCESS\n", hFile, enmAdditionalAttribs));
    return VINF_SUCCESS;
}


RTR3DECL(int) RTFileSetTimes(RTFILE hFile, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    NOREF(pChangeTime); NOREF(pBirthTime);

    /*
     * We can only set AccessTime and ModificationTime, so if neither
     * are specified we can return immediately.
     */
    if (!pAccessTime && !pModificationTime)
        return VINF_SUCCESS;

#ifdef USE_FUTIMENS
    struct timespec aTimespecs[2];
    if (pAccessTime && pModificationTime)
    {
        memcpy(&aTimespecs[0], pAccessTime, sizeof(struct timespec));
        memcpy(&aTimespecs[1], pModificationTime, sizeof(struct timespec));
    }
    else
    {
        RTFSOBJINFO ObjInfo;
        int rc = RTFileQueryInfo(hFile, &ObjInfo, RTFSOBJATTRADD_UNIX);
        if (RT_FAILURE(rc))
            return rc;
        memcpy(&aTimespecs[0], pAccessTime ? pAccessTime : &ObjInfo.AccessTime, sizeof(struct timespec));
        memcpy(&aTimespecs[1], pModificationTime ? pModificationTime : &ObjInfo.ModificationTime, sizeof(struct timespec));
    }

    if (futimens(RTFileToNative(hFile), aTimespecs))
    {
        int rc = RTErrConvertFromErrno(errno);
        Log(("RTFileSetTimes(%RTfile,%p,%p,,): returns %Rrc\n", hFile, pAccessTime, pModificationTime, rc));
        return rc;
    }
#else
    /*
     * Convert the input to timeval, getting the missing one if necessary,
     * and call the API which does the change.
     */
    struct timeval aTimevals[2];
    if (pAccessTime && pModificationTime)
    {
        RTTimeSpecGetTimeval(pAccessTime,       &aTimevals[0]);
        RTTimeSpecGetTimeval(pModificationTime, &aTimevals[1]);
    }
    else
    {
        RTFSOBJINFO ObjInfo;
        int rc = RTFileQueryInfo(hFile, &ObjInfo, RTFSOBJATTRADD_UNIX);
        if (RT_FAILURE(rc))
            return rc;
        RTTimeSpecGetTimeval(pAccessTime        ? pAccessTime       : &ObjInfo.AccessTime,       &aTimevals[0]);
        RTTimeSpecGetTimeval(pModificationTime  ? pModificationTime : &ObjInfo.ModificationTime, &aTimevals[1]);
    }

    /* XXX this falls back to utimes("/proc/self/fd/...",...) for older kernels/glibcs and this
     * will not work for hardened builds where this directory is owned by root.root and mode 0500 */
    if (futimes(RTFileToNative(hFile), aTimevals))
    {
        int rc = RTErrConvertFromErrno(errno);
        Log(("RTFileSetTimes(%RTfile,%p,%p,,): returns %Rrc\n", hFile, pAccessTime, pModificationTime, rc));
        return rc;
    }
#endif
    return VINF_SUCCESS;
}

