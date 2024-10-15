/* $Id: dir.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal Header for RTDir.
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

#ifndef IPRT_INCLUDED_INTERNAL_dir_h
#define IPRT_INCLUDED_INTERNAL_dir_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include "internal/magics.h"


/** Pointer to the data behind an open directory handle. */
typedef struct RTDIRINTERNAL *PRTDIRINTERNAL;

/**
 * Filter a the filename in the against a filter.
 *
 * @returns true if the name matches the filter.
 * @returns false if the name doesn't match filter.
 * @param   pDir        The directory handle.
 * @param   pszName     The path to match to the filter.
 */
typedef DECLCALLBACKTYPE(bool, FNRTDIRFILTER,(PRTDIRINTERNAL pDir, const char *pszName));
/** Pointer to a filter function. */
typedef FNRTDIRFILTER *PFNRTDIRFILTER;


/**
 * Open directory.
 */
typedef struct RTDIRINTERNAL
{
    /** Magic value, RTDIR_MAGIC. */
    uint32_t            u32Magic;
    /** The type of filter that's to be applied to the directory listing. */
    RTDIRFILTER         enmFilter;
    /** The filter function. */
    PFNRTDIRFILTER      pfnFilter;
    /** The filter Code Point string.
     * This is allocated in the same block as this structure. */
    PRTUNICP            puszFilter;
    /** The number of Code Points in the filter string. */
    size_t              cucFilter;
    /** The filter string.
     * This is allocated in the same block as this structure, thus the const. */
    const char         *pszFilter;
    /** The length of the filter string. */
    size_t              cchFilter;
    /** Normalized path to the directory including a trailing slash.
     * We keep this around so we can query more information if required (posix).
     * This is allocated in the same block as this structure, thus the const. */
    const char         *pszPath;
    /** The length of the path. */
    size_t              cchPath;
    /** Pointer to the converted filename.
     * This can be NULL. */
#ifdef RT_OS_WINDOWS
    char               *pszName;
#else
    char const         *pszName;
#endif
    /** The length of the converted filename. */
    size_t              cchName;
    /** The size of this structure. */
    size_t              cbSelf;
    /** The RTDIR_F_XXX flags passed to RTDirOpenFiltered */
    uint32_t            fFlags;
    /** Set if the specified path included a directory slash or if enmFilter is not RTDIRFILTER_NONE.
     * This is relevant for how to interpret the RTDIR_F_NO_FOLLOW flag, as it won't
     * have any effect if the specified path ends with a slash on posix systems.  We
     * implement that on the other systems too, for consistency. */
    bool                fDirSlash;
    /** Set to indicate that the Data member contains unread data. */
    bool                fDataUnread;

#ifndef RTDIR_AGNOSTIC
# ifdef RT_OS_WINDOWS
    /** Set by RTDirRewind. */
    bool                fRestartScan;
    /** Handle to the opened directory search. */
    HANDLE              hDir;
#  ifndef RTNT_USE_NATIVE_NT
    /** Find data buffer.
     * fDataUnread indicates valid data. */
    WIN32_FIND_DATAW    Data;
#  else
    /** The size of the name buffer pszName points to. */
    size_t              cbNameAlloc;
    /** NT filter string. */
    UNICODE_STRING      NtFilterStr;
    /** Pointer to NtFilterStr if applicable, otherwise NULL. */
    PUNICODE_STRING     pNtFilterStr;
    /** The information class we're using. */
    FILE_INFORMATION_CLASS enmInfoClass;
    /** Object directory context data. */
    ULONG               uObjDirCtx;
    /** Pointer to the current data entry in the buffer. */
    union
    {
        /** Both file names, no file ID. */
        PFILE_BOTH_DIR_INFORMATION      pBoth;
        /** Both file names with file ID. */
        PFILE_ID_BOTH_DIR_INFORMATION   pBothId;
        /** Object directory info. */
        POBJECT_DIRECTORY_INFORMATION   pObjDir;
        /** Unsigned view. */
        uintptr_t                       u;
    }                   uCurData;
    /** The amount of valid data in the buffer. */
    uint32_t            cbBuffer;
    /** The allocate buffer size. */
    uint32_t            cbBufferAlloc;
    /** Find data buffer containing multiple directory entries.
     * fDataUnread indicates valid data. */
    uint8_t            *pabBuffer;
    /** The device number for the directory (serial number). */
    RTDEV               uDirDev;
#  endif
# else /* 'POSIX': */
    /** What opendir() returned. */
    DIR                *pDir;
    /** Find data buffer.
     * fDataUnread indicates valid data. */
    struct dirent       Data;
# endif
#endif
} RTDIRINTERNAL;



/**
 * Validates a directory handle.
 * @returns true if valid.
 * @returns false if valid after having bitched about it first.
 */
DECLINLINE(bool) rtDirValidHandle(PRTDIRINTERNAL pDir)
{
    AssertPtrReturn(pDir, false);
    AssertMsgReturn(pDir->u32Magic == RTDIR_MAGIC, ("%#RX32\n", pDir->u32Magic), false);
    return true;
}


/**
 * Initialize the OS specific part of the handle and open the directory.
 * Called by rtDirOpenCommon().
 *
 * @returns IPRT status code.
 * @param   pDir                The directory to open. The pszPath member contains the
 *                              path to the directory.
 * @param   hRelativeDir        The directory @a pvNativeRelative is relative,
 *                              ~(uintptr_t)0 if absolute.
 * @param   pvNativeRelative    The native relative path.  NULL if absolute or
 *                              we're to use (consume) hRelativeDir.
 */
int rtDirNativeOpen(PRTDIRINTERNAL pDir, uintptr_t hRelativeDir, void *pvNativeRelative);

/**
 * Returns the size of the directory structure.
 *
 * @returns The size in bytes.
 * @param   pszPath     The path to the directory we're about to open.
 */
size_t rtDirNativeGetStructSize(const char *pszPath);


DECLHIDDEN(int) rtDirOpenRelativeOrHandle(RTDIR *phDir, const char *pszRelativeAndFilter, RTDIRFILTER enmFilter,
                                          uint32_t fFlags, uintptr_t hRelativeDir, void *pvNativeRelative);

#endif /* !IPRT_INCLUDED_INTERNAL_dir_h */
