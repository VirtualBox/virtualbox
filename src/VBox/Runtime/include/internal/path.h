/* $Id: path.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPath Internal header.
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

#ifndef IPRT_INCLUDED_INTERNAL_path_h
#define IPRT_INCLUDED_INTERNAL_path_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/param.h>

RT_C_DECLS_BEGIN

#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# define HAVE_UNC 1
# define HAVE_DRIVE 1
#endif

/** The name of the environment variable that is used to override the default
 * codeset used when talking to the file systems.  This is only available on
 * Mac OS X and UNIX systems. */
#define RTPATH_CODESET_ENV_VAR          "IPRT_PATH_CODESET"


DECLHIDDEN(size_t)  rtPathRootSpecLen(const char *pszPath);
DECLHIDDEN(size_t)  rtPathVolumeSpecLen(const char *pszPath);
DECLHIDDEN(int)     rtPathPosixRename(const char *pszSrc, const char *pszDst, unsigned fRename, RTFMODE fFileType);
DECLHIDDEN(int)     rtPathWin32MoveRename(const char *pszSrc, const char *pszDst, uint32_t fFlags, RTFMODE fFileType);


/**
 * Converts a path from IPRT to native representation.
 *
 * This may involve querying filesystems what codeset they speak and so forth.
 *
 * @returns IPRT status code.
 * @param   ppszNativePath  Where to store the pointer to the native path.
 *                          Free by calling rtPathFreeHost(). NULL on failure.
 *                          Can be the same as pszPath.
 * @param   pszPath         The path to convert.
 * @param   pszBasePath     What pszPath is relative to.  NULL if current
 *                          directory.
 *
 * @remark  This function is not available on hosts using something else than
 *          byte sequences as names (eg win32).
 */
int rtPathToNative(char const **ppszNativePath, const char *pszPath, const char *pszBasePath);

/**
 * Frees a native path returned by rtPathToNative() or rtPathToNativeEx().
 *
 * @param   pszNativePath   The host path to free. NULL allowed.
 * @param   pszPath         The original path.  This is for checking if
 *                          rtPathToNative returned the pointer to the original.
 *
 * @remark  This function is not available on hosts using something else than
 *          byte sequences as names (eg win32).
 */
void rtPathFreeNative(char const *pszNativePath, const char *pszPath);

/**
 * Converts a path from the native to the IPRT representation.
 *
 * @returns IPRT status code.
 * @param   ppszPath        Where to store the pointer to the IPRT path.
 *                          Free by calling rtPathFreeIprt(). NULL on failure.
 * @param   pszNativePath   The native path to convert.
 * @param   pszBasePath     What pszNativePath is relative to - in IPRT
 *                          representation.  NULL if current directory.
 *
 * @remark  This function is not available on hosts using something else than
 *          byte sequences as names (eg win32).
 */
int rtPathFromNative(const char **ppszPath, const char *pszNativePath, const char *pszBasePath);

/**
 * Frees a path returned by rtPathFromNative or rtPathFromNativeEx.
 *
 * @param   pszPath         The returned path.
 * @param   pszNativePath   The original path.
 */
void rtPathFreeIprt(const char *pszPath, const char *pszNativePath);

/**
 * Convert a path from the native representation to the IPRT one, using the
 * specified path buffer.
 *
 * @returns VINF_SUCCESS, VERR_BUFFER_OVERFLOW, and recoding errors.
 *
 * @param   pszPath         The output buffer.
 * @param   cbPath          The size of the output buffer.
 * @param   pszNativePath   The path to convert.
 * @param   pszBasePath     What pszNativePath is relative to - in IPRT
 *                          representation.  NULL if current directory.
 */
int rtPathFromNativeCopy(char *pszPath, size_t cbPath, const char *pszNativePath, const char *pszBasePath);

/**
 * Convert a path from the native representation to the IPRT one, allocating a
 * string buffer for the result.
 *
 * @returns VINF_SUCCESS, VERR_NO_STR_MEMORY, and recoding errors.
 *
 * @param   ppszPath        Where to return the pointer to the IPRT path.  Must
 *                          be freed by calling RTStrFree.
 * @param   pszNativePath   The path to convert.
 * @param   pszBasePath     What pszNativePath is relative to - in IPRT
 *                          representation.  NULL if current directory.
 */
int rtPathFromNativeDup(char **ppszPath, const char *pszNativePath, const char *pszBasePath);


#if defined(RT_OS_WINDOWS) && defined(IPRT_INCLUDED_fs_h) && defined(UNICODE_NULL)
DECLHIDDEN(int) rtPathNtQueryInfoWorker(HANDLE hRootDir, struct _UNICODE_STRING *pNtName, PRTFSOBJINFO pObjInfo,
                                        RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags, const char *pszPath);
DECLHIDDEN(int) rtPathNtQueryInfoFromHandle(HANDLE hFile, void *pvBuf, size_t cbBuf, PRTFSOBJINFO pObjInfo,
                                            RTFSOBJATTRADD enmAddAttr, const char *pszPath, ULONG uReparseTag);
#endif


RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_path_h */

