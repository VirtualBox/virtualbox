/* $Id: fs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal RTFs header.
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

#ifndef IPRT_INCLUDED_INTERNAL_fs_h
#define IPRT_INCLUDED_INTERNAL_fs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#ifndef RT_OS_WINDOWS
# include <sys/stat.h>
#endif
#ifdef RT_OS_FREEBSD
# include <osreldate.h>
#endif

RT_C_DECLS_BEGIN

/** IO_REPARSE_TAG_SYMLINK */
#define RTFSMODE_SYMLINK_REPARSE_TAG UINT32_C(0xa000000c)

RTFMODE rtFsModeFromDos(RTFMODE fMode, const char *pszName, size_t cbName, uint32_t uReparseTag, RTFMODE fType);
RTFMODE rtFsModeFromUnix(RTFMODE fMode, const char *pszName, size_t cbName, RTFMODE fType);
RTFMODE rtFsModeNormalize(RTFMODE fMode, const char *pszName, size_t cbName, RTFMODE fType);
bool    rtFsModeIsValid(RTFMODE fMode);
bool    rtFsModeIsValidPermissions(RTFMODE fMode);

#ifndef RT_OS_WINDOWS
void    rtFsConvertStatToObjInfo(PRTFSOBJINFO pObjInfo, const struct stat *pStat, const char *pszName, unsigned cbName);
void    rtFsObjInfoAttrSetUnixOwner(PRTFSOBJINFO pObjInfo, RTUID uid);
void    rtFsObjInfoAttrSetUnixGroup(PRTFSOBJINFO pObjInfo, RTUID gid);
#else  /* RT_OS_WINDOWS */
# ifdef DECLARE_HANDLE
int     rtNtQueryFsType(HANDLE hHandle, PRTFSTYPE penmType);
# endif
#endif /* RT_OS_WINDOWS */

#ifdef RT_OS_LINUX
# ifdef __USE_MISC
#  define HAVE_STAT_TIMESPEC_BRIEF
# else
#  define HAVE_STAT_NSEC
# endif
#endif

#ifdef RT_OS_FREEBSD
# if __FreeBSD_version >= 500000 /* 5.0 */
#  define HAVE_STAT_BIRTHTIME
# endif
# if __FreeBSD_version >= 900000 /* 9.0 */
#  define HAVE_STAT_TIMESPEC_BRIEF
# else
#  ifndef __BSD_VISIBLE
#   define __BSD_VISIBLE
#  endif
#  define HAVE_STAT_TIMESPEC
# endif
#endif

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_fs_h */
