/* $Id: memtracker.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Tracker & Leak Detector.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/memtracker.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/critsect.h>
#ifdef IN_RING3
# include <iprt/file.h>
#endif
#include <iprt/errcore.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include "internal/file.h"
#include "internal/magics.h"
#include "internal/strhash.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a memory tracker instance */
typedef struct RTMEMTRACKERINT *PRTMEMTRACKERINT;

/**
 * Memory tracker statistics.
 */
typedef struct RTMEMTRACKERSTATS
{
    /** Array of method calls. */
    uint64_t volatile   acMethodCalls[RTMEMTRACKERMETHOD_END];
    /** The number of times this user freed or reallocated a memory block
     * orignally allocated by someone else. */
    uint64_t volatile   cUserChanges;
    /** The total number of bytes allocated ever. */
    uint64_t volatile   cbTotalAllocated;
    /** The total number of blocks allocated ever. */
    uint64_t volatile   cTotalAllocatedBlocks;
    /** The number of bytes currently allocated. */
    size_t volatile     cbAllocated;
    /** The number of blocks currently allocated. */
    size_t volatile     cAllocatedBlocks;
} RTMEMTRACKERSTATS;
/** Pointer to memory tracker statistics. */
typedef RTMEMTRACKERSTATS *PRTMEMTRACKERSTATS;


/**
 * Memory tracker user data.
 */
typedef struct RTMEMTRACKERUSER
{
    /** Entry in the user list (RTMEMTRACKERINT::UserList). */
    RTLISTNODE          ListEntry;
    /** Pointer to the tracker. */
    PRTMEMTRACKERINT    pTracker;
    /** Critical section protecting the memory list. */
    RTCRITSECT          CritSect;
    /** The list of memory allocated by this user (RTMEMTRACKERHDR). */
    RTLISTANCHOR        MemoryList;
    /** Positive numbers indicates recursion.
     * Negative numbers are used for the global user since that is shared by
     * more than one thread. */
    int32_t volatile    cInTracker;
    /** The user identifier. */
    uint32_t            idUser;
    /** The statistics for this user. */
    RTMEMTRACKERSTATS   Stats;
    /** The user (thread) name. */
    char                szName[32];
} RTMEMTRACKERUSER;
/** Pointer to memory tracker per user data. */
typedef RTMEMTRACKERUSER *PRTMEMTRACKERUSER;


/**
 * Memory tracker per tag statistics.
 */
typedef struct RTMEMTRACKERTAG
{
    /** AVL node core for lookup by hash.  */
    AVLU32NODECORE      Core;
    /** Tag list entry for flat traversal while dumping. */
    RTLISTNODE          ListEntry;
    /** Pointer to the next tag with the same hash (collisions). */
    PRTMEMTRACKERTAG    pNext;
    /** The tag statistics. */
    RTMEMTRACKERSTATS   Stats;
    /** The tag name length.  */
    size_t              cchTag;
    /** The tag string. */
    char                szTag[1];
} RTMEMTRACKERTAG;


/**
 * The memory tracker instance.
 */
typedef struct RTMEMTRACKERINT
{
    /** Cross roads semaphore separating dumping and normal operation.
     *  - NS - normal tracking.
     *  - EW - dumping tracking data. */
    RTSEMXROADS         hXRoads;

    /** Critical section protecting the user list and tag database. */
    RTCRITSECT          CritSect;
    /** List of RTMEMTRACKERUSER records. */
    RTLISTANCHOR        UserList;
    /** The next user identifier number.  */
    uint32_t            idUserNext;
    /** The TLS index used for the per thread user records. */
    RTTLS               iTls;
    /** Cross roads semaphore used to protect the tag database.
     *  - NS - lookup.
     *  - EW + critsect - insertion.
     * @todo Replaced this by a read-write semaphore. */
    RTSEMXROADS         hXRoadsTagDb;
    /** The root of the tag lookup database. */
    AVLU32TREE          TagDbRoot;
    /** List of RTMEMTRACKERTAG records. */
    RTLISTANCHOR        TagList;
#if ARCH_BITS == 32
    /** Alignment padding. */
    uint32_t            u32Alignment;
#endif
    /** The global user record (fallback). */
    RTMEMTRACKERUSER    FallbackUser;
    /** The global statistics. */
    RTMEMTRACKERSTATS   GlobalStats;
    /** The number of busy (recursive) allocations. */
    uint64_t volatile   cBusyAllocs;
    /** The number of busy (recursive) frees. */
    uint64_t volatile   cBusyFrees;
    /** The number of tags. */
    uint32_t            cTags;
    /** The number of users. */
    uint32_t            cUsers;
} RTMEMTRACKERINT;
AssertCompileMemberAlignment(RTMEMTRACKERINT, FallbackUser, 8);


/**
 * Output callback structure.
 */
typedef struct RTMEMTRACKEROUTPUT
{
    /** The printf like callback. */
    DECLCALLBACKMEMBER(void, pfnPrintf,(struct RTMEMTRACKEROUTPUT *pThis, const char *pszFormat, ...));

    /** The data. */
    union
    {
        RTFILE  hFile;
    } uData;
} RTMEMTRACKEROUTPUT;
/** Pointer to a memory tracker output callback structure. */
typedef RTMEMTRACKEROUTPUT *PRTMEMTRACKEROUTPUT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to the default memory tracker. */
static PRTMEMTRACKERINT g_pDefaultTracker = NULL;


/**
 * Creates a memory tracker.
 *
 * @returns IRPT status code.
 * @param   ppTracker           Where to return the tracker instance.
 */
static int rtMemTrackerCreate(PRTMEMTRACKERINT *ppTracker)
{
    PRTMEMTRACKERINT pTracker = (PRTMEMTRACKERINT)RTMemAllocZ(sizeof(*pTracker));
    if (!pTracker)
        return VERR_NO_MEMORY;

    /*
     * Create locks and stuff.
     */
    int rc = RTCritSectInitEx(&pTracker->CritSect,
                              RTCRITSECT_FLAGS_NO_LOCK_VAL | RTCRITSECT_FLAGS_NO_NESTING | RTCRITSECT_FLAGS_BOOTSTRAP_HACK,
                              NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = RTSemXRoadsCreate(&pTracker->hXRoads);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemXRoadsCreate(&pTracker->hXRoadsTagDb);
            if (RT_SUCCESS(rc))
            {
                rc = RTTlsAllocEx(&pTracker->iTls, NULL);
                if (RT_SUCCESS(rc))
                {
                    rc = RTCritSectInitEx(&pTracker->FallbackUser.CritSect,
                                          RTCRITSECT_FLAGS_NO_LOCK_VAL | RTCRITSECT_FLAGS_NO_NESTING | RTCRITSECT_FLAGS_BOOTSTRAP_HACK,
                                          NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Initialize the rest of the structure.
                         */
                        RTListInit(&pTracker->UserList);
                        RTListInit(&pTracker->TagList);
                        RTListInit(&pTracker->FallbackUser.ListEntry);
                        RTListInit(&pTracker->FallbackUser.MemoryList);
                        pTracker->FallbackUser.pTracker   = pTracker;
                        pTracker->FallbackUser.cInTracker = INT32_MIN / 2;
                        pTracker->FallbackUser.idUser     = pTracker->idUserNext++;
                        strcpy(pTracker->FallbackUser.szName, "fallback");

                        *ppTracker = pTracker;
                        return VINF_SUCCESS;
                    }

                    RTTlsFree(pTracker->iTls);
                }
                RTSemXRoadsDestroy(pTracker->hXRoadsTagDb);
            }
            RTSemXRoadsDestroy(pTracker->hXRoads);
        }
        RTCritSectDelete(&pTracker->CritSect);
    }
    return rc;
}


/**
 * Gets the user record to use.
 *
 * @returns Pointer to a user record.
 * @param   pTracker            The tracker instance.
 */
static PRTMEMTRACKERUSER rtMemTrackerGetUser(PRTMEMTRACKERINT pTracker)
{
    /* ASSUMES that RTTlsGet and RTTlsSet will not reenter. */
    PRTMEMTRACKERUSER pUser = (PRTMEMTRACKERUSER)RTTlsGet(pTracker->iTls);
    if (RT_UNLIKELY(!pUser))
    {
        /*
         * Is the thread currently initializing or terminating?
         * If so, don't try add any user record for it as RTThread may barf or
         * we might not get the thread name.
         */
        if (!RTThreadIsSelfAlive())
            return &pTracker->FallbackUser;

        /*
         * Allocate and initialize a new user record for this thread.
         *
         * We install the fallback user record while doing the allocation and
         * locking so that we can deal with recursions.
         */
        int rc = RTTlsSet(pTracker->iTls, &pTracker->FallbackUser);
        if (RT_SUCCESS(rc))
        {
            pUser = (PRTMEMTRACKERUSER)RTMemAllocZ(sizeof(*pUser));
            if (pUser)
            {
                rc = RTCritSectInitEx(&pUser->CritSect,
                                      RTCRITSECT_FLAGS_NO_LOCK_VAL | RTCRITSECT_FLAGS_NO_NESTING | RTCRITSECT_FLAGS_BOOTSTRAP_HACK,
                                      NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
                if (RT_SUCCESS(rc))
                {
                    RTListInit(&pUser->ListEntry);
                    RTListInit(&pUser->MemoryList);
                    pUser->pTracker   = pTracker;
                    pUser->cInTracker = 1;

                    const char *pszName = RTThreadSelfName();
                    if (pszName)
                        RTStrCopy(pUser->szName, sizeof(pUser->szName), pszName);

                    /*
                     * Register the new user record.
                     */
                    rc = RTTlsSet(pTracker->iTls, pUser);
                    if (RT_SUCCESS(rc))
                    {
                        RTCritSectEnter(&pTracker->CritSect);

                        pUser->idUser = pTracker->idUserNext++;
                        RTListAppend(&pTracker->UserList, &pUser->ListEntry);
                        pTracker->cUsers++;

                        RTCritSectLeave(&pTracker->CritSect);
                        return pUser;
                    }

                    RTCritSectDelete(&pUser->CritSect);
                }
                RTMemFree(pUser);
            }
            else
                rc = VERR_NO_MEMORY;
        }

        /* Failed, user the fallback. */
        pUser = &pTracker->FallbackUser;
    }

    ASMAtomicIncS32(&pUser->cInTracker);
    return pUser;
}


/**
 * Counterpart to rtMemTrackerGetUser.
 *
 * @param   pUser               The user record to 'put' back.
 */
DECLINLINE(void) rtMemTrackerPutUser(PRTMEMTRACKERUSER pUser)
{
    ASMAtomicDecS32(&pUser->cInTracker);
}


/**
 * Get the tag record corresponding to @a pszTag.
 *
 * @returns The tag record.  This may be NULL if we're out of memory or
 *          if something goes wrong.
 *
 * @param   pTracker            The tracker instance.
 * @param   pUser               The user record of the caller.  Must NOT be
 *                              NULL.  This is used to prevent infinite
 *                              recursions when allocating a new tag record.
 * @param   pszTag              The tag string.  Can be NULL.
 */
DECLINLINE(PRTMEMTRACKERTAG) rtMemTrackerGetTag(PRTMEMTRACKERINT pTracker, PRTMEMTRACKERUSER pUser, const char *pszTag)
{
    AssertPtr(pTracker);
    AssertPtr(pUser);
    if (pUser->cInTracker <= 0)
        return NULL;

    /*
     * Hash tag string.
     */
    size_t   cchTag;
    uint32_t uHash;
    if (pszTag)
        uHash = sdbmN(pszTag, 260, &cchTag);
    else
    {
        pszTag = "";
        cchTag = 0;
        uHash  = 0;
    }

    /*
     * Look up the tag.
     */
    RTSemXRoadsNSEnter(pTracker->hXRoadsTagDb);
    PRTMEMTRACKERTAG pTag = (PRTMEMTRACKERTAG)RTAvlU32Get(&pTracker->TagDbRoot, uHash);
    while (   pTag
           && (   pTag->cchTag != cchTag
               || memcmp(pTag->szTag, pszTag, cchTag)) )
        pTag = pTag->pNext;
    RTSemXRoadsNSLeave(pTracker->hXRoadsTagDb);

    /*
     * Create a new tag record if not found.
     */
    if (RT_UNLIKELY(!pTag))
    {
        pTag = (PRTMEMTRACKERTAG)RTMemAllocZVar(RT_UOFFSETOF_DYN(RTMEMTRACKERTAG, szTag[cchTag + 1]));
        if (pTag)
        {
            pTag->Core.Key = uHash;
            pTag->cchTag   = cchTag;
            memcpy(pTag->szTag, pszTag, cchTag + 1);

            RTSemXRoadsEWEnter(pTracker->hXRoadsTagDb);
            RTCritSectEnter(&pTracker->CritSect);

            void *pvFreeMe = NULL;
            PRTMEMTRACKERTAG pHeadTag = (PRTMEMTRACKERTAG)RTAvlU32Get(&pTracker->TagDbRoot, uHash);
            if (!pHeadTag)
            {
                RTAvlU32Insert(&pTracker->TagDbRoot, &pTag->Core);
                RTListAppend(&pTracker->TagList, &pTag->ListEntry);
                pTracker->cTags++;
            }
            else
            {
                PRTMEMTRACKERTAG pTag2 = pHeadTag;
                while (   pTag2
                       && (   pTag2->cchTag != cchTag
                           || memcmp(pTag2->szTag, pszTag, cchTag)) )
                    pTag2 = pTag2->pNext;
                if (RT_LIKELY(!pTag2))
                {
                    pTag->pNext     = pHeadTag->pNext;
                    pHeadTag->pNext = pTag;
                    RTListAppend(&pTracker->TagList, &pTag->ListEntry);
                    pTracker->cTags++;
                }
                else
                {
                    pvFreeMe = pTag;
                    pTag = pTag2;
                }
            }

            RTCritSectLeave(&pTracker->CritSect);
            RTSemXRoadsEWLeave(pTracker->hXRoadsTagDb);

            if (RT_LIKELY(pvFreeMe))
                RTMemFree(pvFreeMe);
        }
    }

    return pTag;
}


/**
 * Counterpart to rtMemTrackerGetTag.
 *
 * @param   pTag                The tag record to 'put' back.
 */
DECLINLINE(void) rtMemTrackerPutTag(PRTMEMTRACKERTAG pTag)
{
    NOREF(pTag);
}


/**
 * Record an allocation call.
 *
 * @param   pStats              The statistics record.
 * @param   cbUser              The size of the allocation.
 * @param   enmMethod           The allocation method.
 */
DECLINLINE(void) rtMemTrackerStateRecordAlloc(PRTMEMTRACKERSTATS pStats, size_t cbUser, RTMEMTRACKERMETHOD enmMethod)
{
    ASMAtomicAddU64(&pStats->cbTotalAllocated, cbUser);
    ASMAtomicIncU64(&pStats->cTotalAllocatedBlocks);
    ASMAtomicAddZ(&pStats->cbAllocated, cbUser);
    ASMAtomicIncZ(&pStats->cAllocatedBlocks);
    ASMAtomicIncU64(&pStats->acMethodCalls[enmMethod]);
}


/**
 * Record a free call.
 *
 * @param   pStats              The statistics record.
 * @param   cbUser              The size of the allocation.
 * @param   enmMethod           The free method.
 */
DECLINLINE(void) rtMemTrackerStateRecordFree(PRTMEMTRACKERSTATS pStats, size_t cbUser, RTMEMTRACKERMETHOD enmMethod)
{
    ASMAtomicSubZ(&pStats->cbAllocated, cbUser);
    ASMAtomicDecZ(&pStats->cAllocatedBlocks);
    ASMAtomicIncU64(&pStats->acMethodCalls[enmMethod]);
}


/**
 * Internal RTMemTrackerHdrAlloc and RTMemTrackerHdrAllocEx worker.
 *
 * @returns Pointer to the user data allocation.
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   pv                  The pointer to the allocated memory. This
 *                              includes room for the header.
 * @param   cbUser              The size requested by the user.
 * @param   pszTag              The tag string.
 * @param   pvCaller            The return address.
 * @param   enmMethod           The allocation method.
 */
static void *rtMemTrackerHdrAllocEx(PRTMEMTRACKERINT pTracker, void *pv, size_t cbUser,
                                    const char *pszTag, void *pvCaller, RTMEMTRACKERMETHOD enmMethod)
{
    /*
     * Check input.
     */
    if (!pv)
        return NULL;
    AssertReturn(enmMethod > RTMEMTRACKERMETHOD_INVALID && enmMethod < RTMEMTRACKERMETHOD_END, NULL);

    /*
     * Initialize the header.
     */
    PRTMEMTRACKERHDR pHdr = (PRTMEMTRACKERHDR)pv;

    pHdr->uMagic            = RTMEMTRACKERHDR_MAGIC;
    pHdr->cbUser            = cbUser;
    RTListInit(&pHdr->ListEntry);
    pHdr->pUser             = NULL;
    pHdr->pszTag            = pszTag;
    pHdr->pTag              = NULL;
    pHdr->pvCaller          = pvCaller;
    pHdr->pvUser            = pHdr + 1;
    pHdr->uReserved         = 0;

    /*
     * Add it to the tracker if we've got one.
     */
    if (pTracker)
    {
        PRTMEMTRACKERUSER pUser = rtMemTrackerGetUser(pTracker);
        if (pUser->cInTracker == 1)
        {
            RTSemXRoadsNSEnter(pTracker->hXRoads);

            /* Get the tag and update it's statistics.  */
            PRTMEMTRACKERTAG pTag = rtMemTrackerGetTag(pTracker, pUser, pszTag);
            if (pTag)
            {
                pHdr->pTag = pTag;
                rtMemTrackerStateRecordAlloc(&pTag->Stats, cbUser, enmMethod);
                rtMemTrackerPutTag(pTag);
            }

            /* Link the header and update the user statistics. */
            RTCritSectEnter(&pUser->CritSect);
            RTListAppend(&pUser->MemoryList, &pHdr->ListEntry);
            RTCritSectLeave(&pUser->CritSect);

            pHdr->pUser = pUser;
            rtMemTrackerStateRecordAlloc(&pUser->Stats, cbUser, enmMethod);

            /* Update the global statistics. */
            rtMemTrackerStateRecordAlloc(&pTracker->GlobalStats, cbUser, enmMethod);

            RTSemXRoadsNSLeave(pTracker->hXRoads);
        }
        else
            ASMAtomicIncU64(&pTracker->cBusyAllocs);
        rtMemTrackerPutUser(pUser);
    }

    return pHdr + 1;
}


/**
 * Internal worker for rtMemTrackerHdrFreeEx and rtMemTrackerHdrReallocPrep.
 *
 * @returns Pointer to the original block.
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   pvUser              Pointer to the user memory.
 * @param   cbUser              The size of the user memory or 0.
 * @param   pszTag              The tag to associate the free with.
 * @param   pvCaller            The return address.
 * @param   enmMethod           The free method.
 * @param   uDeadMagic          The dead magic value to use.
 */
static void *rtMemTrackerHdrFreeCommon(PRTMEMTRACKERINT pTracker, void *pvUser, size_t cbUser,
                                       const char *pszTag, void *pvCaller, RTMEMTRACKERMETHOD enmMethod,
                                       size_t uDeadMagic)
{
    PRTMEMTRACKERHDR pHdr = (PRTMEMTRACKERHDR)pvUser - 1;
    AssertReturn(pHdr->uMagic == RTMEMTRACKERHDR_MAGIC, NULL);
    Assert(pHdr->cbUser == cbUser || !cbUser); NOREF(cbUser);
    Assert(pHdr->pvUser == pvUser);

    AssertReturn(enmMethod > RTMEMTRACKERMETHOD_INVALID && enmMethod < RTMEMTRACKERMETHOD_END, NULL);

    /*
     * First mark it as free.
     */
    pHdr->uMagic = uDeadMagic;

    /*
     * If there is a association with a user, we need to unlink it and update
     * the statistics.
     *
     * A note on the locking here.  We don't take the crossroads semaphore when
     * reentering the memory tracker on the same thread because we may be
     * holding it in a different direction and would therefore deadlock.
     */
    PRTMEMTRACKERUSER pMemUser = pHdr->pUser;
    if (pMemUser)
    {
        Assert(pMemUser->pTracker == pTracker); Assert(pTracker);
        PRTMEMTRACKERUSER   pCallingUser    = rtMemTrackerGetUser(pTracker);
        bool const          fTakeXRoadsLock = pCallingUser->cInTracker <= 1;
        if (fTakeXRoadsLock)
            RTSemXRoadsNSEnter(pTracker->hXRoads);

        RTCritSectEnter(&pMemUser->CritSect);
        RTListNodeRemove(&pHdr->ListEntry);
        RTCritSectLeave(&pMemUser->CritSect);

        if (pCallingUser == pMemUser)
            rtMemTrackerStateRecordFree(&pCallingUser->Stats, pHdr->cbUser, enmMethod);
        else
        {
            ASMAtomicIncU64(&pCallingUser->Stats.cUserChanges);
            ASMAtomicIncU64(&pCallingUser->Stats.acMethodCalls[enmMethod]);

            ASMAtomicSubU64(&pMemUser->Stats.cbTotalAllocated, cbUser);
            ASMAtomicSubZ(&pMemUser->Stats.cbAllocated, cbUser);
        }

        rtMemTrackerStateRecordFree(&pTracker->GlobalStats, pHdr->cbUser, enmMethod);

        /** @todo we're currently ignoring pszTag, consider how to correctly
         *        attribute the free operation if the tags differ - if it
         *        makes sense at all... */
        NOREF(pszTag);
        if (pHdr->pTag)
            rtMemTrackerStateRecordFree(&pHdr->pTag->Stats, pHdr->cbUser, enmMethod);


        if (fTakeXRoadsLock)
            RTSemXRoadsNSLeave(pTracker->hXRoads);
        rtMemTrackerPutUser(pCallingUser);
    }
    else
    {
        /*
         * No tracked.  This may happen even when pTracker != NULL when the same
         * thread reenters the tracker when allocating tracker structures or memory
         * in some subroutine like threading and locking.
         */
        Assert(!pHdr->pTag);
        if (pTracker)
            ASMAtomicIncU64(&pTracker->cBusyFrees);
    }

    NOREF(pvCaller);  /* Intended for We may later do some use-after-free tracking. */
    return pHdr;
}


/**
 * Internal worker for RTMemTrackerHdrReallocPrep and
 * RTMemTrackerHdrReallocPrepEx.
 *
 * @returns Pointer to the actual allocation.
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   pvOldUser           The user memory.
 * @param   cbOldUser           The size of the user memory, 0 if unknown.
 * @param   pszTag              The tag string.
 * @param   pvCaller            The return address.
 */
static void *rtMemTrackerHdrReallocPrepEx(PRTMEMTRACKERINT pTracker, void *pvOldUser, size_t cbOldUser,
                                          const char *pszTag, void *pvCaller)
{
    if (!pvOldUser)
        return NULL;
    return rtMemTrackerHdrFreeCommon(pTracker, pvOldUser, cbOldUser, pszTag, pvCaller,
                                     RTMEMTRACKERMETHOD_REALLOC_PREP, RTMEMTRACKERHDR_MAGIC_REALLOC);
}


/**
 * Internal worker for RTMemTrackerHdrReallocDone and
 * RTMemTrackerHdrReallocDoneEx.
 *
 * @returns Pointer to the actual allocation.
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   pvNew               The new memory chunk.  Can be NULL.
 * @param   cbNewUser           The size of the new memory chunk.
 * @param   pvOldUser           Pointer to the old user memory.
 * @param   pszTag              The tag string.
 * @param   pvCaller            The return address.
 */
static void *rtMemTrackerHdrReallocDoneEx(PRTMEMTRACKERINT pTracker, void *pvNew, size_t cbNewUser,
                                          void *pvOldUser, const char *pszTag, void *pvCaller)
{
    /* Succeeded? */
    if (pvNew)
        return rtMemTrackerHdrAllocEx(pTracker, pvNew, cbNewUser, pszTag, pvCaller, RTMEMTRACKERMETHOD_REALLOC_DONE);

    /* Failed or just realloc to zero? */
    if (cbNewUser)
    {
        PRTMEMTRACKERHDR pHdr = (PRTMEMTRACKERHDR)pvOldUser - 1;
        AssertReturn(pHdr->uMagic == RTMEMTRACKERHDR_MAGIC_REALLOC, NULL);

        return rtMemTrackerHdrAllocEx(pTracker, pHdr, pHdr->cbUser, pszTag, pvCaller, RTMEMTRACKERMETHOD_REALLOC_FAILED);
    }

    /* Tealloc to zero bytes, i.e. free. */
    return NULL;
}


/**
 * Internal worker for RTMemTrackerHdrFree and RTMemTrackerHdrFreeEx.
 *
 * @returns Pointer to the actual allocation.
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   pvUser              The user memory.
 * @param   cbUser              The size of the user memory, 0 if unknown.
 * @param   pszTag              The tag string.
 * @param   pvCaller            The return address.
 * @param   enmMethod           The free method.
 */
static void *rtMemTrackerHdrFreeEx(PRTMEMTRACKERINT pTracker, void *pvUser, size_t cbUser,
                                   const char *pszTag, void *pvCaller, RTMEMTRACKERMETHOD enmMethod)
{
    if (!pvUser)
        return NULL;
    return rtMemTrackerHdrFreeCommon(pTracker, pvUser, cbUser, pszTag, pvCaller, enmMethod, RTMEMTRACKERHDR_MAGIC_FREE);
}


/**
 * Prints a statistics record.
 *
 * @param   pStats              The record.
 * @param   pOutput             The output callback table.
 * @param   fVerbose            Whether to print in terse or verbose form.
 */
DECLINLINE(void) rtMemTrackerDumpOneStatRecord(PRTMEMTRACKERSTATS pStats, PRTMEMTRACKEROUTPUT pOutput, bool fVerbose)
{
    if (fVerbose)
    {
        pOutput->pfnPrintf(pOutput,
                           "     Currently allocated: %7zu blocks, %8zu bytes\n"
                           "    Total allocation sum: %7RU64 blocks, %8RU64 bytes\n"
                           ,
                           pStats->cAllocatedBlocks,
                           pStats->cbAllocated,
                           pStats->cTotalAllocatedBlocks,
                           pStats->cbTotalAllocated);
        pOutput->pfnPrintf(pOutput,
                           "  Alloc: %7RU64  AllocZ: %7RU64    Free: %7RU64  User Chg: %7RU64\n"
                           "  RPrep: %7RU64   RDone: %7RU64   RFail: %7RU64\n"
                           "    New: %7RU64   New[]: %7RU64  Delete: %7RU64  Delete[]: %7RU64\n"
                           ,
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_ALLOC],
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_ALLOCZ],
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_FREE],
                           pStats->cUserChanges,
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_REALLOC_PREP],
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_REALLOC_DONE],
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_REALLOC_FAILED],
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_NEW],
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_NEW_ARRAY],
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_DELETE],
                           pStats->acMethodCalls[RTMEMTRACKERMETHOD_DELETE_ARRAY]);
    }
    else
    {
        pOutput->pfnPrintf(pOutput, "  %zu bytes in %zu blocks\n",
                           pStats->cbAllocated, pStats->cAllocatedBlocks);
    }
}


/**
 * Internal worker that dumps all the memory tracking data.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   pOutput             The output callback table.
 */
static void rtMemTrackerDumpAllWorker(PRTMEMTRACKERINT pTracker, PRTMEMTRACKEROUTPUT pOutput)
{
    if (!pTracker)
        return;

    /*
     * We use the EW direction to make sure the lists, trees and statistics
     * does not change while we're working.
     */
    PRTMEMTRACKERUSER pUser = rtMemTrackerGetUser(pTracker);
    RTSemXRoadsEWEnter(pTracker->hXRoads);

    /* Global statistics.*/
    pOutput->pfnPrintf(pOutput, "*** Global statistics ***\n");
    rtMemTrackerDumpOneStatRecord(&pTracker->GlobalStats, pOutput, true);
    pOutput->pfnPrintf(pOutput, "  Busy Allocs: %4RU64  Busy Frees: %4RU64  Tags: %3u  Users: %3u\n",
                       pTracker->cBusyAllocs, pTracker->cBusyFrees, pTracker->cTags, pTracker->cUsers);

    /* Per tag statistics. */
    pOutput->pfnPrintf(pOutput, "\n*** Tag statistics ***\n");
    PRTMEMTRACKERTAG pTag, pNextTag;
    RTListForEachSafe(&pTracker->TagList, pTag, pNextTag, RTMEMTRACKERTAG, ListEntry)
    {
        pOutput->pfnPrintf(pOutput, "Tag: %s\n", pTag->szTag);
        rtMemTrackerDumpOneStatRecord(&pTag->Stats, pOutput, true);
        pOutput->pfnPrintf(pOutput, "\n", pTag->szTag);
    }

    /* Per user statistics & blocks. */
    pOutput->pfnPrintf(pOutput, "\n*** User statistics ***\n");
    PRTMEMTRACKERUSER pCurUser, pNextUser;
    RTListForEachSafe(&pTracker->UserList, pCurUser, pNextUser, RTMEMTRACKERUSER, ListEntry)
    {
        pOutput->pfnPrintf(pOutput, "User #%u: %s%s (cInTracker=%d)\n",
                           pCurUser->idUser,
                           pCurUser->szName,
                           pUser == pCurUser ? " (me)" : "",
                           pCurUser->cInTracker);
        rtMemTrackerDumpOneStatRecord(&pCurUser->Stats, pOutput, true);

        PRTMEMTRACKERHDR pCurHdr, pNextHdr;
        RTListForEachSafe(&pCurUser->MemoryList, pCurHdr, pNextHdr, RTMEMTRACKERHDR, ListEntry)
        {
            if (pCurHdr->pTag)
                pOutput->pfnPrintf(pOutput,
                                   "    %zu bytes at %p by %p with tag %s\n"
                                   "%.*Rhxd\n"
                                   "\n",
                                   pCurHdr->cbUser, pCurHdr->pvUser, pCurHdr->pvCaller, pCurHdr->pTag->szTag,
                                   RT_MIN(pCurHdr->cbUser, 16*3), pCurHdr->pvUser);
            else
                pOutput->pfnPrintf(pOutput,
                                   "    %zu bytes at %p by %p without a tag\n"
                                   "%.*Rhxd\n"
                                   "\n",
                                   pCurHdr->cbUser, pCurHdr->pvUser, pCurHdr->pvCaller,
                                   RT_MIN(pCurHdr->cbUser, 16*3), pCurHdr->pvUser);
        }
        pOutput->pfnPrintf(pOutput, "\n", pTag->szTag);
    }

    /* Repeat the global statistics. */
    pOutput->pfnPrintf(pOutput, "*** Global statistics (reprise) ***\n");
    rtMemTrackerDumpOneStatRecord(&pTracker->GlobalStats, pOutput, true);
    pOutput->pfnPrintf(pOutput, "  Busy Allocs: %4RU64  Busy Frees: %4RU64  Tags: %3u  Users: %3u\n",
                       pTracker->cBusyAllocs, pTracker->cBusyFrees, pTracker->cTags, pTracker->cUsers);

    RTSemXRoadsEWLeave(pTracker->hXRoads);
    rtMemTrackerPutUser(pUser);
}


/**
 * Internal worker that dumps the memory tracking statistics.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   pOutput             The output callback table.
 * @param   fVerbose            Whether to the verbose or quiet.
 */
static void rtMemTrackerDumpStatsWorker(PRTMEMTRACKERINT pTracker, PRTMEMTRACKEROUTPUT pOutput, bool fVerbose)
{
    if (!pTracker)
        return;

    /*
     * We use the EW direction to make sure the lists, trees and statistics
     * does not change while we're working.
     */
    PRTMEMTRACKERUSER pUser = rtMemTrackerGetUser(pTracker);
    RTSemXRoadsEWEnter(pTracker->hXRoads);

    /* Global statistics.*/
    pOutput->pfnPrintf(pOutput, "*** Global statistics ***\n");
    rtMemTrackerDumpOneStatRecord(&pTracker->GlobalStats, pOutput, fVerbose);
    if (fVerbose)
        pOutput->pfnPrintf(pOutput, "  Busy Allocs: %4RU64  Busy Frees: %4RU64  Tags: %3u  Users: %3u\n",
                           pTracker->cBusyAllocs, pTracker->cBusyFrees, pTracker->cTags, pTracker->cUsers);

    /* Per tag statistics. */
    pOutput->pfnPrintf(pOutput, "\n*** Tag statistics ***\n");
    PRTMEMTRACKERTAG pTag, pNextTag;
    RTListForEachSafe(&pTracker->TagList, pTag, pNextTag, RTMEMTRACKERTAG, ListEntry)
    {
        if (   fVerbose
            || pTag->Stats.cbAllocated)
        {
            pOutput->pfnPrintf(pOutput, "Tag: %s\n", pTag->szTag);
            rtMemTrackerDumpOneStatRecord(&pTag->Stats, pOutput, fVerbose);
            if (fVerbose)
                pOutput->pfnPrintf(pOutput, "\n", pTag->szTag);
        }
    }

    /* Per user statistics. */
    pOutput->pfnPrintf(pOutput, "\n*** User statistics ***\n");
    PRTMEMTRACKERUSER pCurUser, pNextUser;
    RTListForEachSafe(&pTracker->UserList, pCurUser, pNextUser, RTMEMTRACKERUSER, ListEntry)
    {
        if (   fVerbose
            || pCurUser->Stats.cbAllocated
            || pCurUser == pUser)
        {
            pOutput->pfnPrintf(pOutput, "User #%u: %s%s (cInTracker=%d)\n",
                               pCurUser->idUser,
                               pCurUser->szName,
                               pUser == pCurUser ? " (me)" : "",
                               pCurUser->cInTracker);
            rtMemTrackerDumpOneStatRecord(&pCurUser->Stats, pOutput, fVerbose);
            if (fVerbose)
                pOutput->pfnPrintf(pOutput, "\n", pTag->szTag);
        }
    }

    if (fVerbose)
    {
        /* Repeat the global statistics. */
        pOutput->pfnPrintf(pOutput, "*** Global statistics (reprise) ***\n");
        rtMemTrackerDumpOneStatRecord(&pTracker->GlobalStats, pOutput, fVerbose);
        pOutput->pfnPrintf(pOutput, "  Busy Allocs: %4RU64  Busy Frees: %4RU64  Tags: %3u  Users: %3u\n",
                           pTracker->cBusyAllocs, pTracker->cBusyFrees, pTracker->cTags, pTracker->cUsers);
    }

    RTSemXRoadsEWLeave(pTracker->hXRoads);
    rtMemTrackerPutUser(pUser);
}


/**
 * @callback_method_impl{RTMEMTRACKEROUTPUT::pfnPrintf, Outputting to the release log}
 */
static DECLCALLBACK(void) rtMemTrackerDumpLogOutput(PRTMEMTRACKEROUTPUT pThis, const char *pszFormat, ...)
{
    NOREF(pThis);
    va_list va;
    va_start(va, pszFormat);
    RTLogPrintfV(pszFormat, va);
    va_end(va);
}


/**
 * Internal worker for RTMemTrackerDumpAllToLog and RTMemTrackerDumpAllToLogEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 */
static void rtMemTrackerDumpAllToLogEx(PRTMEMTRACKERINT pTracker)
{
    RTMEMTRACKEROUTPUT Output;
    Output.pfnPrintf = rtMemTrackerDumpLogOutput;
    rtMemTrackerDumpAllWorker(pTracker, &Output);
}


/**
 * Internal worker for RTMemTrackerDumpStatsToLog and
 * RTMemTrackerDumpStatsToLogEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   fVerbose            Whether to print all the stats or just the ones
 *                              relevant to hunting leaks.
 */
static void rtMemTrackerDumpStatsToLogEx(PRTMEMTRACKERINT pTracker, bool fVerbose)
{
    RTMEMTRACKEROUTPUT Output;
    Output.pfnPrintf = rtMemTrackerDumpLogOutput;
    rtMemTrackerDumpStatsWorker(pTracker, &Output, fVerbose);
}


/**
 * @callback_method_impl{RTMEMTRACKEROUTPUT::pfnPrintf, Outputting to the release log}
 */
static DECLCALLBACK(void) rtMemTrackerDumpLogRelOutput(PRTMEMTRACKEROUTPUT pThis, const char *pszFormat, ...)
{
    NOREF(pThis);
    va_list va;
    va_start(va, pszFormat);
    RTLogRelPrintfV(pszFormat, va);
    va_end(va);
}


/**
 * Internal worker for RTMemTrackerDumpStatsToLog and
 * RTMemTrackerDumpStatsToLogEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 */
static void rtMemTrackerDumpAllToLogRelEx(PRTMEMTRACKERINT pTracker)
{
    RTMEMTRACKEROUTPUT Output;
    Output.pfnPrintf = rtMemTrackerDumpLogRelOutput;
    rtMemTrackerDumpAllWorker(pTracker, &Output);
}


/**
 * Internal worker for RTMemTrackerDumpStatsToLogRel and
 * RTMemTrackerDumpStatsToLogRelEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   fVerbose            Whether to print all the stats or just the ones
 *                              relevant to hunting leaks.
 */
static void rtMemTrackerDumpStatsToLogRelEx(PRTMEMTRACKERINT pTracker, bool fVerbose)
{
    RTMEMTRACKEROUTPUT Output;
    Output.pfnPrintf = rtMemTrackerDumpLogRelOutput;
    rtMemTrackerDumpStatsWorker(pTracker, &Output, fVerbose);
}

#ifdef IN_RING3

/**
 * @callback_method_impl{RTMEMTRACKEROUTPUT::pfnPrintf, Outputting to file}
 */
static DECLCALLBACK(void) rtMemTrackerDumpFileOutput(PRTMEMTRACKEROUTPUT pThis, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    char szOutput[_4K];
    size_t cchOutput = RTStrPrintfV(szOutput, sizeof(szOutput), pszFormat, va);
    va_end(va);
    RTFileWrite(pThis->uData.hFile, szOutput, cchOutput, NULL);
}


/**
 * Internal work that dumps the memory tracking statistics to a file handle.
 *
 * @param   pTracker        The tracker instance.  Can be NULL.
 * @param   fVerbose        Whether to print all the stats or just the ones
 *                          relevant to hunting leaks.
 * @param   hFile           The file handle.  Can be NIL_RTFILE.
 */
static void rtMemTrackerDumpStatsToFileHandle(PRTMEMTRACKERINT pTracker, bool fVerbose, RTFILE hFile)
{
    if (hFile == NIL_RTFILE)
        return;
    RTMEMTRACKEROUTPUT Output;
    Output.pfnPrintf   = rtMemTrackerDumpFileOutput;
    Output.uData.hFile = hFile;
    rtMemTrackerDumpStatsWorker(pTracker, &Output, fVerbose);
}


/**
 * Internal work that dumps all the memory tracking information to a file
 * handle.
 *
 * @param   pTracker        The tracker instance.  Can be NULL.
 * @param   hFile           The file handle.  Can be NIL_RTFILE.
 */
static void rtMemTrackerDumpAllToFileHandle(PRTMEMTRACKERINT pTracker, RTFILE hFile)
{
    if (hFile == NIL_RTFILE)
        return;
    RTMEMTRACKEROUTPUT Output;
    Output.pfnPrintf   = rtMemTrackerDumpFileOutput;
    Output.uData.hFile = hFile;
    rtMemTrackerDumpAllWorker(pTracker, &Output);
}


/**
 * Internal worker for RTMemTrackerDumpStatsToStdOut and
 * RTMemTrackerDumpStatsToStdOutEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   fVerbose            Whether to print all the stats or just the ones
 *                              relevant to hunting leaks.
 */
static void rtMemTrackerDumpStatsToStdOutEx(PRTMEMTRACKERINT pTracker, bool fVerbose)
{
    rtMemTrackerDumpStatsToFileHandle(pTracker, fVerbose, rtFileGetStandard(RTHANDLESTD_OUTPUT));
}


/**
 * Internal worker for RTMemTrackerDumpAllToStdOut and
 * RTMemTrackerDumpAllToStdOutEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 */
static void rtMemTrackerDumpAllToStdOutEx(PRTMEMTRACKERINT pTracker)
{
    rtMemTrackerDumpAllToFileHandle(pTracker, rtFileGetStandard(RTHANDLESTD_OUTPUT));
}


/**
 * Internal worker for RTMemTrackerDumpStatsToStdErr and
 * RTMemTrackerDumpStatsToStdErrEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   fVerbose            Whether to print all the stats or just the ones
 *                              relevant to hunting leaks.
 */
static void rtMemTrackerDumpStatsToStdErrEx(PRTMEMTRACKERINT pTracker, bool fVerbose)
{
    rtMemTrackerDumpStatsToFileHandle(pTracker, fVerbose, rtFileGetStandard(RTHANDLESTD_ERROR));
}


/**
 * Internal worker for RTMemTrackerDumpAllToStdErr and
 * RTMemTrackerDumpAllToStdErrEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 */
static void rtMemTrackerDumpAllToStdErrEx(PRTMEMTRACKERINT pTracker)
{
    rtMemTrackerDumpAllToFileHandle(pTracker, rtFileGetStandard(RTHANDLESTD_ERROR));
}


/**
 * Internal worker for RTMemTrackerDumpStatsToFile and
 * RTMemTrackerDumpStatsToFileEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   fVerbose            Whether to print all the stats or just the ones
 *                              relevant to hunting leaks.
 * @param   pszFilename         The name of the output file.
 */
static void rtMemTrackerDumpStatsToFileEx(PRTMEMTRACKERINT pTracker, bool fVerbose, const char *pszFilename)
{
    if (!pTracker)
        return;

    /** @todo this is borked. */
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszFilename,
                        RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE
                        | (0600 << RTFILE_O_CREATE_MODE_SHIFT));
    if (RT_FAILURE(rc))
        return;
    rtMemTrackerDumpStatsToFileHandle(pTracker, fVerbose, hFile);
    RTFileClose(hFile);
}


/**
 * Internal worker for RTMemTrackerDumpAllToFile and
 * RTMemTrackerDumpAllToFileEx.
 *
 * @param   pTracker            The tracker instance.  Can be NULL.
 * @param   pszFilename         The name of the output file.
 */
static void rtMemTrackerDumpAllToFileEx(PRTMEMTRACKERINT pTracker, const char *pszFilename)
{
    if (!pTracker)
        return;

    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszFilename,
                        RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE
                        | (0600 << RTFILE_O_CREATE_MODE_SHIFT));
    if (RT_FAILURE(rc))
        return;
    rtMemTrackerDumpAllToFileHandle(pTracker, hFile);
    RTFileClose(hFile);
}

#endif /* IN_RING3 */



/*
 *
 *
 * Default tracker.
 * Default tracker.
 * Default tracker.
 * Default tracker.
 * Default tracker.
 *
 *
 */


/**
 * Handles the lazy initialization when g_pDefaultTracker is NULL.
 *
 * @returns The newly created default tracker or NULL.
 */
static PRTMEMTRACKERINT rtMemTrackerLazyInitDefaultTracker(void)
{
    /*
     * Don't attempt initialize before RTThread has been initialized.
     */
    if (!RTThreadIsInitialized())
        return NULL;

    /*
     * Only one initialization at a time.  For now we'll ASSUME that there
     * won't be thread ending up here at the same time, only the same
     * reentering from the allocator when creating the tracker.
     */
    static volatile bool s_fInitialized = false;
    if (ASMAtomicXchgBool(&s_fInitialized, true))
        return g_pDefaultTracker;

    PRTMEMTRACKERINT pTracker = NULL; /* gcc sucks. */
    int rc = rtMemTrackerCreate(&pTracker);
    if (RT_FAILURE(rc))
        return NULL;

    g_pDefaultTracker = pTracker;
    return pTracker;
}



RTDECL(void *) RTMemTrackerHdrAlloc(void *pv, size_t cb, const char *pszTag, void *pvCaller, RTMEMTRACKERMETHOD enmMethod)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerHdrAllocEx(pTracker, pv, cb, pszTag, pvCaller, enmMethod);
}


RTDECL(void *) RTMemTrackerHdrReallocPrep(void *pvOldUser, size_t cbOldUser, const char *pszTag, void *pvCaller)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerHdrReallocPrepEx(pTracker, pvOldUser, cbOldUser, pszTag, pvCaller);
}


RTDECL(void *) RTMemTrackerHdrReallocDone(void *pvNew, size_t cbNewUser, void *pvOld, const char *pszTag, void *pvCaller)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerHdrReallocDoneEx(pTracker, pvNew, cbNewUser, pvOld, pszTag, pvCaller);
}


RTDECL(void *) RTMemTrackerHdrFree(void *pvUser, size_t cbUser, const char *pszTag, void *pvCaller, RTMEMTRACKERMETHOD enmMethod)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerHdrFreeEx(pTracker, pvUser, cbUser, pszTag, pvCaller, enmMethod);
}


RTDECL(void) RTMemTrackerDumpAllToLog(void)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpAllToLogEx(pTracker);
}


RTDECL(void) RTMemTrackerDumpAllToLogRel(void)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpAllToLogRelEx(pTracker);
}


RTDECL(void) RTMemTrackerDumpAllToStdOut(void)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpAllToStdOutEx(pTracker);
}


RTDECL(void) RTMemTrackerDumpAllToStdErr(void)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpAllToStdErrEx(pTracker);
}


RTDECL(void) RTMemTrackerDumpAllToFile(const char *pszFilename)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpAllToFileEx(pTracker, pszFilename);
}


RTDECL(void) RTMemTrackerDumpStatsToLog(bool fVerbose)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpStatsToLogEx(pTracker, fVerbose);
}


RTDECL(void) RTMemTrackerDumpStatsToLogRel(bool fVerbose)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpStatsToLogRelEx(pTracker, fVerbose);
}


RTDECL(void) RTMemTrackerDumpStatsToStdOut(bool fVerbose)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpStatsToStdOutEx(pTracker, fVerbose);
}


RTDECL(void) RTMemTrackerDumpStatsToStdErr(bool fVerbose)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpStatsToStdErrEx(pTracker, fVerbose);
}


RTDECL(void) RTMemTrackerDumpStatsToFile(bool fVerbose, const char *pszFilename)
{
    PRTMEMTRACKERINT pTracker = g_pDefaultTracker;
    if (RT_UNLIKELY(!pTracker))
        pTracker = rtMemTrackerLazyInitDefaultTracker();
    return rtMemTrackerDumpStatsToFileEx(pTracker, fVerbose, pszFilename);
}

