/* $Id: path2-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Path Manipulation, POSIX, Part 2 - RTPathQueryInfo.
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
#define LOG_GROUP RTLOGGROUP_PATH
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/types.h>

#include <iprt/path.h>
#include <iprt/env.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/path.h"
#include "internal/process.h"
#include "internal/fs.h"


RTR3DECL(int) RTPathQueryInfo(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs)
{
    return RTPathQueryInfoEx(pszPath, pObjInfo, enmAdditionalAttribs, RTPATH_F_ON_LINK);
}


RTR3DECL(int) RTPathQueryInfoEx(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
    AssertMsgReturn(    enmAdditionalAttribs >= RTFSOBJATTRADD_NOTHING
                    &&  enmAdditionalAttribs <= RTFSOBJATTRADD_LAST,
                    ("Invalid enmAdditionalAttribs=%p\n", enmAdditionalAttribs),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Convert the filename.
     */
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat Stat;
        if (fFlags & RTPATH_F_FOLLOW_LINK)
            rc = stat(pszNativePath, &Stat);
        else
            rc = lstat(pszNativePath, &Stat); /** @todo how doesn't have lstat again? */
        if (!rc)
        {
            rtFsConvertStatToObjInfo(pObjInfo, &Stat, pszPath, 0);
            switch (enmAdditionalAttribs)
            {
                case RTFSOBJATTRADD_NOTHING:
                case RTFSOBJATTRADD_UNIX:
                    Assert(pObjInfo->Attr.enmAdditional == RTFSOBJATTRADD_UNIX);
                    break;

                case RTFSOBJATTRADD_UNIX_OWNER:
                    rtFsObjInfoAttrSetUnixOwner(pObjInfo, Stat.st_uid);
                    break;

                case RTFSOBJATTRADD_UNIX_GROUP:
                    rtFsObjInfoAttrSetUnixGroup(pObjInfo, Stat.st_gid);
                    break;

                case RTFSOBJATTRADD_EASIZE:
                    /** @todo Use SGI extended attribute interface to query EA info. */
                    pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_EASIZE;
                    pObjInfo->Attr.u.EASize.cb            = 0;
                    break;

                default:
                    AssertMsgFailed(("Impossible!\n"));
                    return VERR_INTERNAL_ERROR;
            }
        }
        else
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTPathQueryInfoEx(%p:{%s}, pObjInfo=%p, %d): returns %Rrc\n",
             pszPath, pszPath, pObjInfo, enmAdditionalAttribs, rc));
    return rc;
}


RTR3DECL(int) RTPathSetTimes(const char *pszPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    return RTPathSetTimesEx(pszPath, pAccessTime, pModificationTime, pChangeTime, pBirthTime, RTPATH_F_ON_LINK);
}


RTR3DECL(int) RTPathSetTimesEx(const char *pszPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                               PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pAccessTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pModificationTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pChangeTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pBirthTime, VERR_INVALID_POINTER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Convert the paths.
     */
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO ObjInfo;

        /*
         * If it's a no-op, we'll only verify the existance of the file.
         */
        if (!pAccessTime && !pModificationTime)
            rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, fFlags);
        else
        {
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
                rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_UNIX, fFlags);
                if (RT_SUCCESS(rc))
                {
                    RTTimeSpecGetTimeval(pAccessTime        ? pAccessTime       : &ObjInfo.AccessTime,       &aTimevals[0]);
                    RTTimeSpecGetTimeval(pModificationTime  ? pModificationTime : &ObjInfo.ModificationTime, &aTimevals[1]);
                }
                else
                    Log(("RTPathSetTimes('%s',%p,%p,,): RTPathQueryInfo failed with %Rrc\n",
                         pszPath, pAccessTime, pModificationTime, rc));
            }
            if (RT_SUCCESS(rc))
            {
                if (fFlags & RTPATH_F_FOLLOW_LINK)
                {
                    if (utimes(pszNativePath, aTimevals))
                        rc = RTErrConvertFromErrno(errno);
                }
#if (defined(RT_OS_DARWIN) && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050) \
 || defined(RT_OS_FREEBSD) \
 || defined(RT_OS_LINUX) \
 || defined(RT_OS_OS2) /** @todo who really has lutimes? */
                else
                {
                    if (lutimes(pszNativePath, aTimevals))
                    {
                        /* If lutimes is not supported (e.g. linux < 2.6.22), try fall back on utimes: */
                        if (errno != ENOSYS)
                            rc = RTErrConvertFromErrno(errno);
                        else
                        {
                            if (pAccessTime && pModificationTime)
                                rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_UNIX, fFlags);
                            if (RT_SUCCESS(rc) && !RTFS_IS_SYMLINK(ObjInfo.Attr.fMode))
                            {
                                if (utimes(pszNativePath, aTimevals))
                                    rc = RTErrConvertFromErrno(errno);
                            }
                            else
                                rc = VERR_NOT_SUPPORTED;
                        }
                    }
                }
#else
                else
                {
                    if (pAccessTime && pModificationTime)
                        rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_UNIX, fFlags);
                    if (RT_SUCCESS(rc) && RTFS_IS_SYMLINK(ObjInfo.Attr.fMode))
                        rc = VERR_NS_SYMLINK_SET_TIME;
                    else if (RT_SUCCESS(rc))
                    {
                        if (utimes(pszNativePath, aTimevals))
                            rc = RTErrConvertFromErrno(errno);
                    }
                }
#endif
                if (RT_FAILURE(rc))
                    Log(("RTPathSetTimes('%s',%p,%p,,): failed with %Rrc and errno=%d\n",
                         pszPath, pAccessTime, pModificationTime, rc, errno));
            }
        }
        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTPathSetTimes(%p:{%s}, %p:{%RDtimespec}, %p:{%RDtimespec}, %p:{%RDtimespec}, %p:{%RDtimespec}): return %Rrc\n",
             pszPath, pszPath, pAccessTime, pAccessTime, pModificationTime, pModificationTime,
             pChangeTime, pChangeTime, pBirthTime, pBirthTime, rc));
    return rc;
}


RTR3DECL(int) RTPathSetOwner(const char *pszPath, uint32_t uid, uint32_t gid)
{
    return RTPathSetOwnerEx(pszPath, uid, gid, RTPATH_F_ON_LINK);
}


RTR3DECL(int) RTPathSetOwnerEx(const char *pszPath, uint32_t uid, uint32_t gid, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    uid_t uidNative = uid != NIL_RTUID ? (uid_t)uid : (uid_t)-1;
    AssertReturn(uid == uidNative, VERR_INVALID_PARAMETER);
    gid_t gidNative = gid != NIL_RTGID ? (gid_t)gid : (uid_t)-1;
    AssertReturn(gid == gidNative, VERR_INVALID_PARAMETER);

    /*
     * Convert the path.
     */
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        if (fFlags & RTPATH_F_FOLLOW_LINK)
        {
            if (chown(pszNativePath, uidNative, gidNative))
                rc = RTErrConvertFromErrno(errno);
        }
#if 1
        else
        {
            if (lchown(pszNativePath, uidNative, gidNative))
                rc = RTErrConvertFromErrno(errno);
        }
#else
        else
        {
            RTFSOBJINFO ObjInfo;
            rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_UNIX, fFlags);
            if (RT_SUCCESS(rc) && RTFS_IS_SYMLINK(ObjInfo.Attr.fMode))
                rc = VERR_NS_SYMLINK_CHANGE_OWNER;
            else if (RT_SUCCESS(rc))
            {
                if (lchown(pszNativePath, uidNative, gidNative))
                    rc = RTErrConvertFromErrno(errno);
            }
        }
#endif
        if (RT_FAILURE(rc))
            Log(("RTPathSetOwnerEx('%s',%d,%d): failed with %Rrc and errno=%d\n",
                 pszPath, uid, gid, rc, errno));

        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTPathSetOwnerEx(%p:{%s}, uid=%d, gid=%d): return %Rrc\n",
             pszPath, pszPath, uid, gid, rc));
    return rc;
}

