/* $Id: vboxext.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox extension to Wine D3D
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_shaderlib_vboxext_h
#define VBOX_INCLUDED_SRC_Graphics_shaderlib_vboxext_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef VBOX_WINE_WITHOUT_LIBWINE
# ifdef _MSC_VER
#  include <iprt/win/windows.h>
# else
#  include <windows.h>
# endif
#endif

#include <iprt/list.h>

HRESULT VBoxExtCheckInit(void);
HRESULT VBoxExtCheckTerm(void);
#if defined(VBOX_WINE_WITH_SINGLE_CONTEXT) || defined(VBOX_WINE_WITH_SINGLE_SWAPCHAIN_CONTEXT)
# ifndef VBOX_WITH_WDDM
/* Windows destroys HDC created by a given thread when the thread is terminated
 * this leads to a mess-up in Wine & Chromium code in some situations, e.g.
 * D3D device is created in one thread, then the thread is terminated,
 * then device is started to be used in another thread */
HDC VBoxExtGetDC(HWND hWnd);
int VBoxExtReleaseDC(HWND hWnd, HDC hDC);
# endif
/* We need to do a VBoxTlsRefRelease for the current thread context on thread exit to avoid memory leaking
 * Calling VBoxTlsRefRelease may result in a call to context dtor callback, which is supposed to be run under wined3d lock.
 * We can not acquire a wined3d lock in DllMain since this would result in a lock order violation, which may result in a deadlock.
 * In other words, wined3d may internally call Win32 API functions which result in a DLL lock acquisition while holding wined3d lock.
 * So lock order should always be "wined3d lock" -> "dll lock".
 * To avoid possible deadlocks we make an asynchronous call to a worker thread to make a context release from there. */
struct wined3d_context;
void VBoxExtReleaseContextAsync(struct wined3d_context *context);
#endif

/* API for creating & destroying windows */
HRESULT VBoxExtWndDestroy(HWND hWnd, HDC hDC);
HRESULT VBoxExtWndCreate(DWORD width, DWORD height, HWND *phWnd, HDC *phDC);


/* hashmap */
typedef DECLCALLBACKTYPE(uint32_t, FNVBOXEXT_HASHMAP_HASH,(void *pvKey));
typedef FNVBOXEXT_HASHMAP_HASH *PFNVBOXEXT_HASHMAP_HASH;

typedef DECLCALLBACKTYPE(bool, FNVBOXEXT_HASHMAP_EQUAL,(void *pvKey1, void *pvKey2));
typedef FNVBOXEXT_HASHMAP_EQUAL *PFNVBOXEXT_HASHMAP_EQUAL;

struct VBOXEXT_HASHMAP;
struct VBOXEXT_HASHMAP_ENTRY;
typedef DECLCALLBACKTYPE(bool, FNVBOXEXT_HASHMAP_VISITOR,(struct VBOXEXT_HASHMAP *pMap, void *pvKey, struct VBOXEXT_HASHMAP_ENTRY *pValue, void *pvVisitor));
typedef FNVBOXEXT_HASHMAP_VISITOR *PFNVBOXEXT_HASHMAP_VISITOR;

typedef struct VBOXEXT_HASHMAP_ENTRY
{
    RTLISTNODE ListNode;
    void *pvKey;
    uint32_t u32Hash;
} VBOXEXT_HASHMAP_ENTRY, *PVBOXEXT_HASHMAP_ENTRY;

typedef struct VBOXEXT_HASHMAP_BUCKET
{
    RTLISTNODE EntryList;
} VBOXEXT_HASHMAP_BUCKET, *PVBOXEXT_HASHMAP_BUCKET;

#define VBOXEXT_HASHMAP_NUM_BUCKETS 29

typedef struct VBOXEXT_HASHMAP
{
    PFNVBOXEXT_HASHMAP_HASH pfnHash;
    PFNVBOXEXT_HASHMAP_EQUAL pfnEqual;
    uint32_t cEntries;
    VBOXEXT_HASHMAP_BUCKET aBuckets[VBOXEXT_HASHMAP_NUM_BUCKETS];
} VBOXEXT_HASHMAP, *PVBOXEXT_HASHMAP;

void VBoxExtHashInit(PVBOXEXT_HASHMAP pMap, PFNVBOXEXT_HASHMAP_HASH pfnHash, PFNVBOXEXT_HASHMAP_EQUAL pfnEqual);
PVBOXEXT_HASHMAP_ENTRY VBoxExtHashPut(PVBOXEXT_HASHMAP pMap, void *pvKey, PVBOXEXT_HASHMAP_ENTRY pEntry);
PVBOXEXT_HASHMAP_ENTRY VBoxExtHashGet(PVBOXEXT_HASHMAP pMap, void *pvKey);
PVBOXEXT_HASHMAP_ENTRY VBoxExtHashRemove(PVBOXEXT_HASHMAP pMap, void *pvKey);
void* VBoxExtHashRemoveEntry(PVBOXEXT_HASHMAP pMap, PVBOXEXT_HASHMAP_ENTRY pEntry);
void VBoxExtHashVisit(PVBOXEXT_HASHMAP pMap, PFNVBOXEXT_HASHMAP_VISITOR pfnVisitor, void *pvVisitor);
void VBoxExtHashCleanup(PVBOXEXT_HASHMAP pMap, PFNVBOXEXT_HASHMAP_VISITOR pfnVisitor, void *pvVisitor);

DECLINLINE(uint32_t) VBoxExtHashSize(PVBOXEXT_HASHMAP pMap)
{
    return pMap->cEntries;
}

DECLINLINE(void*) VBoxExtHashEntryKey(PVBOXEXT_HASHMAP_ENTRY pEntry)
{
    return pEntry->pvKey;
}

struct VBOXEXT_HASHCACHE_ENTRY;
typedef DECLCALLBACKTYPE(void, FNVBOXEXT_HASHCACHE_CLEANUP_ENTRY,(void *pvKey, struct VBOXEXT_HASHCACHE_ENTRY *pEntry));
typedef FNVBOXEXT_HASHCACHE_CLEANUP_ENTRY *PFNVBOXEXT_HASHCACHE_CLEANUP_ENTRY;

typedef struct VBOXEXT_HASHCACHE_ENTRY
{
    VBOXEXT_HASHMAP_ENTRY MapEntry;
    uint32_t u32Usage;
} VBOXEXT_HASHCACHE_ENTRY, *PVBOXEXT_HASHCACHE_ENTRY;

typedef struct VBOXEXT_HASHCACHE
{
    VBOXEXT_HASHMAP Map;
    uint32_t cMaxElements;
    PFNVBOXEXT_HASHCACHE_CLEANUP_ENTRY pfnCleanupEntry;
} VBOXEXT_HASHCACHE, *PVBOXEXT_HASHCACHE;

#define VBOXEXT_HASHCACHE_FROM_MAP(_pMap) RT_FROM_MEMBER((_pMap), VBOXEXT_HASHCACHE, Map)
#define VBOXEXT_HASHCACHE_ENTRY_FROM_MAP(_pEntry) RT_FROM_MEMBER((_pEntry), VBOXEXT_HASHCACHE_ENTRY, MapEntry)

DECLINLINE(void) VBoxExtCacheInit(PVBOXEXT_HASHCACHE pCache, uint32_t cMaxElements,
        PFNVBOXEXT_HASHMAP_HASH pfnHash,
        PFNVBOXEXT_HASHMAP_EQUAL pfnEqual,
        PFNVBOXEXT_HASHCACHE_CLEANUP_ENTRY pfnCleanupEntry)
{
    VBoxExtHashInit(&pCache->Map, pfnHash, pfnEqual);
    pCache->cMaxElements = cMaxElements;
    pCache->pfnCleanupEntry = pfnCleanupEntry;
}

DECLINLINE(PVBOXEXT_HASHCACHE_ENTRY) VBoxExtCacheGet(PVBOXEXT_HASHCACHE pCache, void *pvKey)
{
    PVBOXEXT_HASHMAP_ENTRY pEntry = VBoxExtHashRemove(&pCache->Map, pvKey);
    return VBOXEXT_HASHCACHE_ENTRY_FROM_MAP(pEntry);
}

DECLINLINE(void) VBoxExtCachePut(PVBOXEXT_HASHCACHE pCache, void *pvKey, PVBOXEXT_HASHCACHE_ENTRY pEntry)
{
    PVBOXEXT_HASHMAP_ENTRY pOldEntry = VBoxExtHashPut(&pCache->Map, pvKey, &pEntry->MapEntry);
    PVBOXEXT_HASHCACHE_ENTRY pOld;
    if (!pOldEntry)
        return;
    pOld = VBOXEXT_HASHCACHE_ENTRY_FROM_MAP(pOldEntry);
    if (pOld != pEntry)
        pCache->pfnCleanupEntry(pvKey, pOld);
}

void VBoxExtCacheCleanup(PVBOXEXT_HASHCACHE pCache);

DECLINLINE(void) VBoxExtCacheTerm(PVBOXEXT_HASHCACHE pCache)
{
    VBoxExtCacheCleanup(pCache);
}

#endif /* !VBOX_INCLUDED_SRC_Graphics_shaderlib_vboxext_h */

