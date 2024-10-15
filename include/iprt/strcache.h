/* $Id: strcache.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - String Cache, stub implementation.
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

#ifndef IPRT_INCLUDED_strcache_h
#define IPRT_INCLUDED_strcache_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

RT_C_DECLS_BEGIN


/**
 * Create a new string cache.
 *
 * @returns IPRT status code
 *
 * @param   phStrCache          Where to return the string cache handle.
 * @param   pszName             The name of the cache (for debug purposes).
 */
RTDECL(int) RTStrCacheCreate(PRTSTRCACHE phStrCache, const char *pszName);


/**
 * Destroys a string cache.
 *
 * This will cause all strings in the cache to be released and thus become
 * invalid.
 *
 * @returns IPRT status.
 *
 * @param   hStrCache           Handle to the string cache. The nil and default
 *                              handles are ignored quietly (VINF_SUCCESS).
 */
RTDECL(int) RTStrCacheDestroy(RTSTRCACHE hStrCache);


/**
 * Enters a string into the cache.
 *
 * @returns Pointer to a read-only copy of the string.
 *
 * @param   hStrCache           Handle to the string cache.
 * @param   pchString           Pointer to a string. This does not need to be
 *                              zero terminated, but must not contain any zero
 *                              characters.
 * @param   cchString           The number of characters (bytes) to enter.
 *
 * @remarks It is implementation dependent whether the returned string pointer
 *          differs when entering the same string twice.
 */
RTDECL(const char *) RTStrCacheEnterN(RTSTRCACHE hStrCache, const char *pchString, size_t cchString);

/**
 * Enters a string into the cache.
 *
 * @returns Pointer to a read-only copy of the string.
 *
 * @param   hStrCache           Handle to the string cache.
 * @param   psz                 Pointer to a zero terminated string.
 *
 * @remarks See RTStrCacheEnterN.
 */
RTDECL(const char *) RTStrCacheEnter(RTSTRCACHE hStrCache, const char *psz);


/**
 * Enters a string into the cache in lower cased form.
 *
 * @returns Pointer to a read-only lower cased copy of the string.
 *
 * @param   hStrCache           Handle to the string cache.
 * @param   pchString           Pointer to a string. This does not need to be
 *                              zero terminated, but must not contain any zero
 *                              characters.
 * @param   cchString           The number of characters (bytes) to enter.
 *
 * @remarks It is implementation dependent whether the returned string pointer
 *          differs when entering the same string twice.
 */
RTDECL(const char *) RTStrCacheEnterLowerN(RTSTRCACHE hStrCache, const char *pchString, size_t cchString);

/**
 * Enters a string into the cache in lower cased form.
 *
 * @returns Pointer to a read-only lower cased copy of the string.
 *
 * @param   hStrCache           Handle to the string cache.
 * @param   psz                 Pointer to a zero terminated string.
 *
 * @remarks See RTStrCacheEnterN.
 */
RTDECL(const char *) RTStrCacheEnterLower(RTSTRCACHE hStrCache, const char *psz);


/**
 * Retains a reference to a string.
 *
 * @returns The new reference count. UINT32_MAX is returned if the string
 *          pointer is invalid.
 */
RTDECL(uint32_t) RTStrCacheRetain(const char *psz);

/**
 * Releases a reference to a string.
 *
 * @returns The new reference count.
 *          UINT32_MAX is returned if the string pointer is invalid.
 *
 * @param   hStrCache           Handle to the string cache. NIL is NOT allowed.
 * @param   psz                 Pointer to a cached string.
 */
RTDECL(uint32_t) RTStrCacheRelease(RTSTRCACHE hStrCache, const char *psz);

/**
 * Gets the string length of a cache entry.
 *
 * @returns The string length. 0 if the string is invalid (asserted).
 *
 * @param   psz             Pointer to a cached string.
 */
RTDECL(size_t) RTStrCacheLength(const char *psz);


/**
 * Gets cache statistics.
 *
 * All parameters, except @a hStrCache, are optional and can be NULL.
 *
 * @returns Number of strings, UINT32_MAX on failure (or not supported).
 * @param   hStrCache           Handle to the string cache.
 * @param   pcbStrings          The number of string bytes (including
 *                              terminators) .
 * @param   pcbChunks           Amount of memory we've allocated for the
 *                              internal allocator.
 * @param   pcbBigEntries       Amount of memory we've allocated off the heap
 *                              for really long strings that doesn't fit in the
 *                              internal allocator.
 * @param   pcHashCollisions    Number of hash table insert collisions.
 * @param   pcHashCollisions2   Number of hash table secondary insert
 *                              collisions.
 * @param   pcHashInserts       Number of hash table inserts.
 * @param   pcRehashes          The number of rehashes.
 *
 * @remarks This is not a stable interface as it needs to reflect the cache
 *          implementation.
 */
RTDECL(uint32_t) RTStrCacheGetStats(RTSTRCACHE hStrCache, size_t *pcbStrings, size_t *pcbChunks, size_t *pcbBigEntries,
                                    uint32_t *pcHashCollisions, uint32_t *pcHashCollisions2, uint32_t *pcHashInserts,
                                    uint32_t *pcRehashes);

/**
 * Indicates whether this a real string cache or a cheap place holder.
 *
 * A real string cache will return the same address when a string is added
 * multiple times.
 *
 * @returns true / false.
 */
RTDECL(bool) RTStrCacheIsRealImpl(void);


RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_strcache_h */

