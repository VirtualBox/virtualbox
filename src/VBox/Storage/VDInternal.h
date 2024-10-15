/* $Id: VDInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VD - Virtual Disk container implementation, internal header file.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/

#ifndef VBOX_INCLUDED_SRC_Storage_VDInternal_h
#define VBOX_INCLUDED_SRC_Storage_VDInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif
#include <VBox/vd.h>
#include <VBox/vd-plugin.h>

#include <iprt/avl.h>
#include <iprt/list.h>
#include <iprt/memcache.h>

#if 0 /* bird: this is nonsense */
/** Disable dynamic backends on non x86 architectures. This feature
 * requires the SUPR3 library which is not available there.
 */
#if !defined(VBOX_HDD_NO_DYNAMIC_BACKENDS) && !defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)
# define VBOX_HDD_NO_DYNAMIC_BACKENDS
#endif
#endif

/** Magic number contained in the VDISK instance data, used for checking that the passed
 * pointer contains a valid instance in debug builds. */
#define VDISK_SIGNATURE 0x6f0e2a7d

/**
 * Structure containing everything I/O related
 * for the image and cache descriptors.
 */
typedef struct VDIO
{
    /** I/O interface to the upper layer. */
    PVDINTERFACEIO      pInterfaceIo;

    /** Per image internal I/O interface. */
    VDINTERFACEIOINT    VDIfIoInt;

    /** Fallback I/O interface, only used if the caller doesn't provide it. */
    VDINTERFACEIO       VDIfIo;

    /** Opaque backend data. */
    void               *pBackendData;
    /** Disk this image is part of */
    PVDISK              pDisk;
    /** Flag whether to ignore flush requests. */
    bool                fIgnoreFlush;
} VDIO, *PVDIO;

/** Forward declaration of an I/O task */
typedef struct VDIOTASK *PVDIOTASK;

/**
 * Virtual disk container image descriptor.
 */
typedef struct VDIMAGE
{
    /** Link to parent image descriptor, if any. */
    struct VDIMAGE     *pPrev;
    /** Link to child image descriptor, if any. */
    struct VDIMAGE     *pNext;
    /** Cached image size. */
    uint64_t           cbImage;
    /** Container base filename. (UTF-8) */
    char               *pszFilename;
    /** Data managed by the backend which keeps the actual info. */
    void               *pBackendData;
    /** Cached sanitized image flags. */
    unsigned            uImageFlags;
    /** Image open flags (only those handled generically in this code and which
     * the backends will never ever see). */
    unsigned            uOpenFlags;

    /** Function pointers for the various backend methods. */
    PCVDIMAGEBACKEND    Backend;
    /** Pointer to list of VD interfaces, per-image. */
    PVDINTERFACE        pVDIfsImage;
    /** I/O related things. */
    VDIO                VDIo;
} VDIMAGE, *PVDIMAGE;

/** The special uninitialized size value for he image. */
#define VD_IMAGE_SIZE_UNINITIALIZED UINT64_C(0)

/**
 * Virtual disk cache image descriptor.
 */
typedef struct VDCACHE
{
    /** Cache base filename. (UTF-8) */
    char               *pszFilename;
    /** Data managed by the backend which keeps the actual info. */
    void               *pBackendData;
    /** Cached sanitized image flags. */
    unsigned            uImageFlags;
    /** Image open flags (only those handled generically in this code and which
     * the backends will never ever see). */
    unsigned            uOpenFlags;

    /** Function pointers for the various backend methods. */
    PCVDCACHEBACKEND    Backend;

    /** Pointer to list of VD interfaces, per-cache. */
    PVDINTERFACE        pVDIfsCache;
    /** I/O related things. */
    VDIO                VDIo;
} VDCACHE, *PVDCACHE;

/**
 * A block waiting for a discard.
 */
typedef struct VDDISCARDBLOCK
{
    /** AVL core. */
    AVLRU64NODECORE    Core;
    /** LRU list node. */
    RTLISTNODE         NodeLru;
    /** Number of bytes to discard. */
    size_t             cbDiscard;
    /** Bitmap of allocated sectors. */
    void              *pbmAllocated;
} VDDISCARDBLOCK, *PVDDISCARDBLOCK;

/**
 * VD discard state.
 */
typedef struct VDDISCARDSTATE
{
    /** Number of bytes waiting for a discard. */
    size_t              cbDiscarding;
    /** AVL tree with blocks waiting for a discard.
     * The uOffset + cbDiscard range is the search key. */
    PAVLRU64TREE        pTreeBlocks;
    /** LRU list of the least frequently discarded blocks.
     * If there are to many blocks waiting the least frequently used
     * will be removed and the range will be set to 0.
     */
    RTLISTNODE          ListLru;
} VDDISCARDSTATE, *PVDDISCARDSTATE;

/**
 * VD filter instance.
 */
typedef struct VDFILTER
{
    /** List node for the read filter chain. */
    RTLISTNODE         ListNodeChainRead;
    /** List node for the write filter chain. */
    RTLISTNODE         ListNodeChainWrite;
    /** Number of references to this filter. */
    uint32_t           cRefs;
    /** Opaque VD filter backend instance data. */
    void              *pvBackendData;
    /** Pointer to the filter backend interface. */
    PCVDFILTERBACKEND  pBackend;
    /** Pointer to list of VD interfaces, per-filter. */
    PVDINTERFACE        pVDIfsFilter;
    /** I/O related things. */
    VDIO                VDIo;
} VDFILTER;
/** Pointer to a VD filter instance. */
typedef VDFILTER *PVDFILTER;

/**
 * Virtual disk container main structure, private part.
 */
struct VDISK
{
    /** Structure signature (VDISK_SIGNATURE). */
    uint32_t               u32Signature;

    /** Image type. */
    VDTYPE                 enmType;

    /** Number of opened images. */
    unsigned               cImages;

    /** Base image. */
    PVDIMAGE               pBase;

    /** Last opened image in the chain.
     * The same as pBase if only one image is used. */
    PVDIMAGE               pLast;

    /** If a merge to one of the parents is running this may be non-NULL
     * to indicate to what image the writes should be additionally relayed. */
    PVDIMAGE               pImageRelay;

    /** Flags representing the modification state. */
    unsigned               uModified;

    /** Cached size of this disk. */
    uint64_t               cbSize;
    /** Cached PCHS geometry for this disk. */
    VDGEOMETRY             PCHSGeometry;
    /** Cached LCHS geometry for this disk. */
    VDGEOMETRY             LCHSGeometry;

    /** Pointer to list of VD interfaces, per-disk. */
    PVDINTERFACE           pVDIfsDisk;
    /** Pointer to the common interface structure for error reporting. */
    PVDINTERFACEERROR      pInterfaceError;
    /** Pointer to the optional thread synchronization callbacks. */
    PVDINTERFACETHREADSYNC pInterfaceThreadSync;

    /** Memory cache for I/O contexts */
    RTMEMCACHE             hMemCacheIoCtx;
    /** Memory cache for I/O tasks. */
    RTMEMCACHE             hMemCacheIoTask;
    /** An I/O context is currently using the disk structures
     * Every I/O context must be placed on one of the lists below. */
    volatile bool          fLocked;
    /** Head of pending I/O tasks waiting for completion - LIFO order. */
    volatile PVDIOTASK     pIoTasksPendingHead;
    /** Head of newly queued I/O contexts - LIFO order. */
    volatile PVDIOCTX      pIoCtxHead;
    /** Head of halted I/O contexts which are given back to generic
     * disk framework by the backend. - LIFO order. */
    volatile PVDIOCTX      pIoCtxHaltedHead;

    /** Head of blocked I/O contexts, processed only
     * after pIoCtxLockOwner was freed - LIFO order. */
    volatile PVDIOCTX      pIoCtxBlockedHead;
    /** I/O context which locked the disk for a growing write or flush request.
     * Other flush or growing write requests need to wait until
     * the current one completes. - NIL_VDIOCTX if unlocked. */
    volatile PVDIOCTX      pIoCtxLockOwner;
    /** If the disk was locked by a growing write, flush or discard request this
     * contains the start offset to check for interfering I/O while it is in progress. */
    uint64_t               uOffsetStartLocked;
    /** If the disk was locked by a growing write, flush or discard request this contains
     * the first non affected offset to check for interfering I/O while it is in progress. */
    uint64_t               uOffsetEndLocked;

    /** Pointer to the L2 disk cache if any. */
    PVDCACHE               pCache;
    /** Pointer to the discard state if any. */
    PVDDISCARDSTATE        pDiscard;

    /** Read filter chain - PVDFILTER. */
    RTLISTANCHOR           ListFilterChainRead;
    /** Write filter chain - PVDFILTER. */
    RTLISTANCHOR           ListFilterChainWrite;
};


DECLHIDDEN(int)      vdPluginInit(void);
DECLHIDDEN(int)      vdPluginTerm(void);
DECLHIDDEN(bool)     vdPluginIsInitialized(void);
DECLHIDDEN(int)      vdPluginUnloadFromPath(const char *pszPath);
DECLHIDDEN(int)      vdPluginUnloadFromFilename(const char *pszFilename);
DECLHIDDEN(int)      vdPluginLoadFromPath(const char *pszPath);
DECLHIDDEN(int)      vdPluginLoadFromFilename(const char *pszFilename);

DECLHIDDEN(uint32_t) vdGetImageBackendCount(void);
DECLHIDDEN(int)      vdQueryImageBackend(uint32_t idx, PCVDIMAGEBACKEND *ppBackend);
DECLHIDDEN(int)      vdFindImageBackend(const char *pszBackend, PCVDIMAGEBACKEND *ppBackend);
DECLHIDDEN(uint32_t) vdGetCacheBackendCount(void);
DECLHIDDEN(int)      vdQueryCacheBackend(uint32_t idx, PCVDCACHEBACKEND *ppBackend);
DECLHIDDEN(int)      vdFindCacheBackend(const char *pszBackend, PCVDCACHEBACKEND *ppBackend);
DECLHIDDEN(uint32_t) vdGetFilterBackendCount(void);
DECLHIDDEN(int)      vdQueryFilterBackend(uint32_t idx, PCVDFILTERBACKEND *ppBackend);
DECLHIDDEN(int)      vdFindFilterBackend(const char *pszFilter, PCVDFILTERBACKEND *ppBackend);

DECLHIDDEN(int)      vdIoIterQueryStartNext(VDIOITER hVdIoIter, uint64_t *pu64Start);
DECLHIDDEN(int)      vdIoIterQuerySegSizeByStart(VDIOITER hVdIoIter, uint64_t u64Start, size_t *pcRegSize);
DECLHIDDEN(int)      vdIoIterAdvance(VDIOITER hVdIoIter, uint64_t cBlocksOrBytes);

#endif /* !VBOX_INCLUDED_SRC_Storage_VDInternal_h */

