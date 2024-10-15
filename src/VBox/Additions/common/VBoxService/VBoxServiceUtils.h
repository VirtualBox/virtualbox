/* $Id: VBoxServiceUtils.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxServiceUtils - Guest Additions Services (Utilities).
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_common_VBoxService_VBoxServiceUtils_h
#define GA_INCLUDED_SRC_common_VBoxService_VBoxServiceUtils_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxServiceInternal.h"

/** ID cache entry. */
typedef struct VGSVCUIDENTRY
{
    /** The identifier name. */
    uint32_t    id;
    /** Set if UID, clear if GID. */
    bool        fIsUid;
    /** The name. */
    char        szName[128 - 4 - 1];
} VGSVCUIDENTRY;
typedef VGSVCUIDENTRY *PVGSVCUIDENTRY;


/** ID cache. */
typedef struct VGSVCIDCACHE
{
    /** Number of valid cache entries. */
    uint32_t                cEntries;
    /** The next entry to replace. */
    uint32_t                iNextReplace;
    /** The cache entries. */
    VGSVCUIDENTRY           aEntries[16];
} VGSVCIDCACHE;
typedef VGSVCIDCACHE *PVGSVCIDCACHE;

#ifdef VBOX_WITH_GUEST_PROPS
int VGSvcReadProp(uint32_t u32ClientId, const char *pszPropName, char **ppszValue, char **ppszFlags, uint64_t *puTimestamp);
int VGSvcReadPropUInt32(uint32_t u32ClientId, const char *pszPropName, uint32_t *pu32, uint32_t u32Min, uint32_t u32Max);
int VGSvcReadHostProp(uint32_t u32ClientId, const char *pszPropName, bool fReadOnly, char **ppszValue, char **ppszFlags,
                      uint64_t *puTimestamp);
int VGSvcWritePropF(uint32_t u32ClientId, const char *pszName, const char *pszValueFormat, ...);
#endif

#ifdef RT_OS_WINDOWS
int VGSvcUtilWinGetFileVersionString(const char *pszPath, const char *pszFileName, char *pszVersion, size_t cbVersion);
#endif

const char *VGSvcIdCacheGetUidName(PVGSVCIDCACHE pIdCache, RTUID uid, const char *pszEntry, const char *pszRelativeTo);
const char *VGSvcIdCacheGetGidName(PVGSVCIDCACHE pIdCache, RTGID gid, const char *pszEntry, const char *pszRelativeTo);

#endif /* !GA_INCLUDED_SRC_common_VBoxService_VBoxServiceUtils_h */

