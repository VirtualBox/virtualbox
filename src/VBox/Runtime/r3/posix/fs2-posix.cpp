/* $Id: fs2-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File System Helpers, POSIX, Part 2.
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
#define RTTIME_INCL_TIMESPEC
#include <sys/time.h>
#include <sys/param.h>
#ifndef DEV_BSIZE
# include <sys/stat.h>
# if defined(RT_OS_HAIKU) && !defined(S_BLKSIZE)
#  define S_BLKSIZE 512
# endif
# define DEV_BSIZE S_BLKSIZE /** @todo bird: add DEV_BSIZE to sys/param.h on OS/2. */
#endif

#include <iprt/fs.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/time.h>
#include "internal/fs.h"


/**
 * Internal worker function which setups RTFSOBJINFO based on a UNIX stat struct.
 *
 * @param   pObjInfo        The file system object info structure to setup.
 * @param   pStat           The stat structure to use.
 * @param   pszName         The filename which this applies to (exe/hidden check).
 * @param   cbName          The length of that filename. (optional, set 0)
 */
void rtFsConvertStatToObjInfo(PRTFSOBJINFO pObjInfo, const struct stat *pStat, const char *pszName, unsigned cbName)
{
    pObjInfo->cbObject    = pStat->st_size;
    pObjInfo->cbAllocated = pStat->st_blocks * DEV_BSIZE;

#ifdef HAVE_STAT_NSEC
    RTTimeSpecAddNano(RTTimeSpecSetSeconds(&pObjInfo->AccessTime,       pStat->st_atime),     pStat->st_atimensec);
    RTTimeSpecAddNano(RTTimeSpecSetSeconds(&pObjInfo->ModificationTime, pStat->st_mtime),     pStat->st_mtimensec);
    RTTimeSpecAddNano(RTTimeSpecSetSeconds(&pObjInfo->ChangeTime,       pStat->st_ctime),     pStat->st_ctimensec);
# ifdef HAVE_STAT_BIRTHTIME
    RTTimeSpecAddNano(RTTimeSpecSetSeconds(&pObjInfo->BirthTime,        pStat->st_birthtime), pStat->st_birthtimensec);
# endif

#elif defined(HAVE_STAT_TIMESPEC_BRIEF)
    RTTimeSpecSetTimespec(&pObjInfo->AccessTime,       &pStat->st_atim);
    RTTimeSpecSetTimespec(&pObjInfo->ModificationTime, &pStat->st_mtim);
    RTTimeSpecSetTimespec(&pObjInfo->ChangeTime,       &pStat->st_ctim);
# ifdef HAVE_STAT_BIRTHTIME
    RTTimeSpecSetTimespec(&pObjInfo->BirthTime,        &pStat->st_birthtim);
# endif

#elif defined(HAVE_STAT_TIMESPEC)
    RTTimeSpecSetTimespec(&pObjInfo->AccessTime,       pStat->st_atimespec);
    RTTimeSpecSetTimespec(&pObjInfo->ModificationTime, pStat->st_mtimespec);
    RTTimeSpecSetTimespec(&pObjInfo->ChangeTime,       pStat->st_ctimespec);
# ifdef HAVE_STAT_BIRTHTIME
    RTTimeSpecSetTimespec(&pObjInfo->BirthTime,        pStat->st_birthtimespec);
# endif

#else /* just the normal stuff */
    RTTimeSpecSetSeconds(&pObjInfo->AccessTime,       pStat->st_atime);
    RTTimeSpecSetSeconds(&pObjInfo->ModificationTime, pStat->st_mtime);
    RTTimeSpecSetSeconds(&pObjInfo->ChangeTime,       pStat->st_ctime);
# ifdef HAVE_STAT_BIRTHTIME
    RTTimeSpecSetSeconds(&pObjInfo->BirthTime,        pStat->st_birthtime);
# endif
#endif
#ifndef HAVE_STAT_BIRTHTIME
    pObjInfo->BirthTime = pObjInfo->ChangeTime;
#endif

    /* the file mode */
    RTFMODE fMode = pStat->st_mode & RTFS_UNIX_MASK;
    Assert(RTFS_UNIX_ISUID == S_ISUID);
    Assert(RTFS_UNIX_ISGID == S_ISGID);
#ifdef S_ISTXT
    Assert(RTFS_UNIX_ISTXT == S_ISTXT);
#elif defined(S_ISVTX)
    Assert(RTFS_UNIX_ISTXT == S_ISVTX);
#else
#error "S_ISVTX / S_ISTXT isn't defined"
#endif
    Assert(RTFS_UNIX_IRWXU == S_IRWXU);
    Assert(RTFS_UNIX_IRUSR == S_IRUSR);
    Assert(RTFS_UNIX_IWUSR == S_IWUSR);
    Assert(RTFS_UNIX_IXUSR == S_IXUSR);
    Assert(RTFS_UNIX_IRWXG == S_IRWXG);
    Assert(RTFS_UNIX_IRGRP == S_IRGRP);
    Assert(RTFS_UNIX_IWGRP == S_IWGRP);
    Assert(RTFS_UNIX_IXGRP == S_IXGRP);
    Assert(RTFS_UNIX_IRWXO == S_IRWXO);
    Assert(RTFS_UNIX_IROTH == S_IROTH);
    Assert(RTFS_UNIX_IWOTH == S_IWOTH);
    Assert(RTFS_UNIX_IXOTH == S_IXOTH);
    Assert(RTFS_TYPE_FIFO == S_IFIFO);
    Assert(RTFS_TYPE_DEV_CHAR == S_IFCHR);
    Assert(RTFS_TYPE_DIRECTORY == S_IFDIR);
    Assert(RTFS_TYPE_DEV_BLOCK == S_IFBLK);
    Assert(RTFS_TYPE_FILE == S_IFREG);
    Assert(RTFS_TYPE_SYMLINK == S_IFLNK);
    Assert(RTFS_TYPE_SOCKET == S_IFSOCK);
#ifdef S_IFWHT
    Assert(RTFS_TYPE_WHITEOUT == S_IFWHT);
#endif
    Assert(RTFS_TYPE_MASK == S_IFMT);

    pObjInfo->Attr.fMode  = rtFsModeFromUnix(fMode, pszName, cbName, 0);

    /* additional unix attribs */
    pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX;
    pObjInfo->Attr.u.Unix.uid             = pStat->st_uid;
    pObjInfo->Attr.u.Unix.gid             = pStat->st_gid;
    pObjInfo->Attr.u.Unix.cHardlinks      = pStat->st_nlink;
    pObjInfo->Attr.u.Unix.INodeIdDevice   = pStat->st_dev;
    pObjInfo->Attr.u.Unix.INodeId         = pStat->st_ino;
#ifdef HAVE_STAT_FLAGS
    pObjInfo->Attr.u.Unix.fFlags          = pStat->st_flags;
#else
    pObjInfo->Attr.u.Unix.fFlags          = 0;
#endif
#ifdef HAVE_STAT_GEN
    pObjInfo->Attr.u.Unix.GenerationId    = pStat->st_gen;
#else
    pObjInfo->Attr.u.Unix.GenerationId    = 0;
#endif
    pObjInfo->Attr.u.Unix.Device          = pStat->st_rdev;
}

