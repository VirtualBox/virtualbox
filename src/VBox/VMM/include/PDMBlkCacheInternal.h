/* $Id: PDMBlkCacheInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Block Cache.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VMM_INCLUDED_SRC_include_PDMBlkCacheInternal_h
#define VMM_INCLUDED_SRC_include_PDMBlkCacheInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/pdmblkcache.h>
#include <iprt/types.h>
#include <iprt/file.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>
#include <iprt/avl.h>
#include <iprt/list.h>
#include <iprt/spinlock.h>
#include <iprt/memcache.h>

RT_C_DECLS_BEGIN

/**
 * A few forward declarations.
 */
/** Pointer to a cache LRU list. */
typedef struct PDMBLKLRULIST *PPDMBLKLRULIST;
/** Pointer to the global cache structure. */
typedef struct PDMBLKCACHEGLOBAL *PPDMBLKCACHEGLOBAL;
/** Pointer to a cache entry waiter structure. */
typedef struct PDMBLKCACHEWAITER *PPDMBLKCACHEWAITER;

/**
 * A cache entry
 */
typedef struct PDMBLKCACHEENTRY
{
    /** The AVL entry data. */
    AVLRU64NODECORE                 Core;
    /** Pointer to the previous element. Used in one of the LRU lists.*/
    struct PDMBLKCACHEENTRY        *pPrev;
    /** Pointer to the next element. Used in one of the LRU lists.*/
    struct PDMBLKCACHEENTRY        *pNext;
    /** Pointer to the list the entry is in. */
    PPDMBLKLRULIST                  pList;
    /** Cache the entry belongs to. */
    PPDMBLKCACHE                    pBlkCache;
    /** Flags for this entry. Combinations of PDMACFILECACHE_* \#defines */
    volatile uint32_t               fFlags;
    /** Reference counter. Prevents eviction of the entry if > 0. */
    volatile uint32_t               cRefs;
    /** Size of the entry. */
    uint32_t                        cbData;
    /** Pointer to the memory containing the data. */
    uint8_t                        *pbData;
    /** Head of list of tasks waiting for this one to finish. */
    PPDMBLKCACHEWAITER              pWaitingHead;
    /** Tail of list of tasks waiting for this one to finish. */
    PPDMBLKCACHEWAITER              pWaitingTail;
    /** Node for dirty but not yet committed entries list per endpoint. */
    RTLISTNODE                      NodeNotCommitted;
} PDMBLKCACHEENTRY, *PPDMBLKCACHEENTRY;
/** I/O is still in progress for this entry. This entry is not evictable. */
#define PDMBLKCACHE_ENTRY_IO_IN_PROGRESS RT_BIT(0)
/** Entry is locked and thus not evictable. */
#define PDMBLKCACHE_ENTRY_LOCKED         RT_BIT(1)
/** Entry is dirty */
#define PDMBLKCACHE_ENTRY_IS_DIRTY       RT_BIT(2)
/** Entry is not evictable. */
#define PDMBLKCACHE_NOT_EVICTABLE  (PDMBLKCACHE_ENTRY_LOCKED | PDMBLKCACHE_ENTRY_IO_IN_PROGRESS | PDMBLKCACHE_ENTRY_IS_DIRTY)

/**
 * LRU list data
 */
typedef struct PDMBLKLRULIST
{
    /** Head of the list. */
    PPDMBLKCACHEENTRY pHead;
    /** Tail of the list. */
    PPDMBLKCACHEENTRY pTail;
    /** Number of bytes cached in the list. */
    uint32_t          cbCached;
} PDMBLKLRULIST;

/**
 * Global cache data.
 */
typedef struct PDMBLKCACHEGLOBAL
{
    /** Pointer to the owning VM instance. */
    PVM                 pVM;
    /** Maximum size of the cache in bytes. */
    uint32_t            cbMax;
    /** Current size of the cache in bytes. */
    uint32_t            cbCached;
    /** Critical section protecting the cache. */
    RTCRITSECT          CritSect;
    /** Maximum number of bytes cached. */
    uint32_t            cbRecentlyUsedInMax;
    /** Maximum number of bytes in the paged out list .*/
    uint32_t            cbRecentlyUsedOutMax;
    /** Recently used cache entries list */
    PDMBLKLRULIST       LruRecentlyUsedIn;
    /** Scorecard cache entry list. */
    PDMBLKLRULIST       LruRecentlyUsedOut;
    /** List of frequently used cache entries */
    PDMBLKLRULIST       LruFrequentlyUsed;
    /** Commit timeout in milli seconds */
    uint32_t            u32CommitTimeoutMs;
    /** Number of dirty bytes needed to start a commit of the data to the disk. */
    uint32_t            cbCommitDirtyThreshold;
    /** Current number of dirty bytes in the cache. */
    volatile uint32_t   cbDirty;
    /** Flag whether the VM was suspended becaus of an I/O error. */
    volatile bool       fIoErrorVmSuspended;
    /** Flag whether a commit is currently in progress. */
    volatile bool       fCommitInProgress;
    /** Commit interval timer */
    TMTIMERHANDLE       hTimerCommit;
    /** Number of endpoints using the cache. */
    uint32_t            cRefs;
    /** List of all users of this cache. */
    RTLISTANCHOR        ListUsers;
#ifdef VBOX_WITH_STATISTICS
    /** Hit counter. */
    STAMCOUNTER         cHits;
    /** Partial hit counter. */
    STAMCOUNTER         cPartialHits;
    /** Miss counter. */
    STAMCOUNTER         cMisses;
    /** Bytes read from cache. */
    STAMCOUNTER         StatRead;
    /** Bytes written to the cache. */
    STAMCOUNTER         StatWritten;
    /** Time spend to get an entry in the AVL tree. */
    STAMPROFILEADV      StatTreeGet;
    /** Time spend to insert an entry in the AVL tree. */
    STAMPROFILEADV      StatTreeInsert;
    /** Time spend to remove an entry in the AVL tree. */
    STAMPROFILEADV      StatTreeRemove;
    /** Number of times a buffer could be reused. */
    STAMCOUNTER         StatBuffersReused;
#endif
} PDMBLKCACHEGLOBAL;
#ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(PDMBLKCACHEGLOBAL, cHits, sizeof(uint64_t));
#endif

/**
 * Block cache type.
 */
typedef enum PDMBLKCACHETYPE
{
    /** Device . */
    PDMBLKCACHETYPE_DEV = 1,
    /** Driver consumer. */
    PDMBLKCACHETYPE_DRV,
    /** Internal consumer. */
    PDMBLKCACHETYPE_INTERNAL,
    /** Usb consumer. */
    PDMBLKCACHETYPE_USB
} PDMBLKCACHETYPE;

/**
 * Per user cache data.
 */
typedef struct PDMBLKCACHE
{
    /** Pointer to the id for the cache. */
    char                         *pszId;
    /** AVL tree managing cache entries. */
    PAVLRU64TREE                  pTree;
    /** R/W semaphore protecting cached entries for this endpoint. */
    RTSEMRW                       SemRWEntries;
    /** Pointer to the gobal cache data */
    PPDMBLKCACHEGLOBAL            pCache;
    /** Lock protecting the dirty entries list. */
    RTSPINLOCK                    LockList;
    /** List of dirty but not committed entries for this endpoint. */
    RTLISTANCHOR                  ListDirtyNotCommitted;
    /** Node of the cache user list. */
    RTLISTNODE                    NodeCacheUser;
    /** Block cache type. */
    PDMBLKCACHETYPE               enmType;
    /** Type specific data. */
    union
    {
        /** PDMASYNCCOMPLETIONTEMPLATETYPE_DEV */
        struct
        {
            /** Pointer to the device instance owning the block cache. */
            R3PTRTYPE(PPDMDEVINS)                            pDevIns;
            /** Complete callback to the user. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERCOMPLETEDEV)         pfnXferComplete;
            /** I/O enqueue callback. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERENQUEUEDEV)          pfnXferEnqueue;
            /** Discard enqueue callback. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERENQUEUEDISCARDDEV)   pfnXferEnqueueDiscard;
        } Dev;
        /** PDMASYNCCOMPLETIONTEMPLATETYPE_DRV */
        struct
        {
            /** Pointer to the driver instance owning the block cache. */
            R3PTRTYPE(PPDMDRVINS)                            pDrvIns;
            /** Complete callback to the user. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERCOMPLETEDRV)         pfnXferComplete;
            /** I/O enqueue callback. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERENQUEUEDRV)          pfnXferEnqueue;
            /** Discard enqueue callback. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERENQUEUEDISCARDDRV)   pfnXferEnqueueDiscard;
        } Drv;
        /** PDMASYNCCOMPLETIONTEMPLATETYPE_INTERNAL */
        struct
        {
            /** Pointer to user data. */
            R3PTRTYPE(void *)                                pvUser;
            /** Complete callback to the user. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERCOMPLETEINT)         pfnXferComplete;
            /** I/O enqueue callback. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERENQUEUEINT)          pfnXferEnqueue;
            /** Discard enqueue callback. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERENQUEUEDISCARDINT)   pfnXferEnqueueDiscard;
        } Int;
        /** PDMASYNCCOMPLETIONTEMPLATETYPE_USB */
        struct
        {
            /** Pointer to the usb instance owning the template. */
            R3PTRTYPE(PPDMUSBINS)                            pUsbIns;
            /** Complete callback to the user. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERCOMPLETEUSB)         pfnXferComplete;
            /** I/O enqueue callback. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERENQUEUEUSB)          pfnXferEnqueue;
            /** Discard enqueue callback. */
            R3PTRTYPE(PFNPDMBLKCACHEXFERENQUEUEDISCARDUSB)   pfnXferEnqueueDiscard;
        } Usb;
    } u;

#ifdef VBOX_WITH_STATISTICS

#if HC_ARCH_BITS == 64
    uint32_t                      u32Alignment;
#endif
    /** Number of times a write was deferred because the cache entry was still in progress */
    STAMCOUNTER                   StatWriteDeferred;
    /** Number appended cache entries. */
    STAMCOUNTER                   StatAppendedWrites;
#endif

    /** Flag whether the cache was suspended. */
    volatile bool                 fSuspended;
    /** Number of outstanding I/O transfers. */
    volatile uint32_t             cIoXfersActive;

} PDMBLKCACHE, *PPDMBLKCACHE;
#ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(PDMBLKCACHE, StatWriteDeferred, sizeof(uint64_t));
#endif

/**
 * I/O task.
 */
typedef struct PDMBLKCACHEREQ
{
    /** Opaque user data returned on completion. */
    void             *pvUser;
    /** Number of pending transfers (waiting for a cache entry and passed through). */
    volatile uint32_t cXfersPending;
    /** Status code. */
    volatile int      rcReq;
} PDMBLKCACHEREQ, *PPDMBLKCACHEREQ;

/**
 * I/O transfer from the cache to the underlying medium.
 */
typedef struct PDMBLKCACHEIOXFER
{
    /** Flag whether the I/O xfer updates a cache entry or updates the request directly. */
    bool                  fIoCache;
    /** Type dependent data. */
    union
    {
        /** Pointer to the entry the transfer updates. */
        PPDMBLKCACHEENTRY pEntry;
        /** Pointer to the request the transfer updates. */
        PPDMBLKCACHEREQ   pReq;
    };
    /** Transfer direction. */
    PDMBLKCACHEXFERDIR    enmXferDir;
    /** Segment used if a cache entry is updated. */
    RTSGSEG               SgSeg;
    /** S/G buffer. */
    RTSGBUF               SgBuf;
} PDMBLKCACHEIOXFER;

/**
 * Cache waiter
 */
typedef struct PDMBLKCACHEWAITER
{
    /* Next waiter in the list. */
    struct PDMBLKCACHEWAITER *pNext;
    /** S/G buffer holding or receiving data. */
    RTSGBUF                   SgBuf;
    /** Offset into the cache entry to start the transfer. */
    uint32_t                  offCacheEntry;
    /** How many bytes to transfer. */
    size_t                    cbTransfer;
    /** Flag whether the task wants to read or write into the entry. */
    bool                      fWrite;
    /** Task the waiter is for. */
    PPDMBLKCACHEREQ           pReq;
} PDMBLKCACHEWAITER;

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_PDMBlkCacheInternal_h */

