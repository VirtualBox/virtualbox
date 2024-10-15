/* $Id: handletable.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Handle Tables, internal header.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_common_misc_handletable_h
#define IPRT_INCLUDED_SRC_common_misc_handletable_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** The number of entries in the 2nd level lookup table. */
#define RTHT_LEVEL2_ENTRIES             2048

/** The number of (max) 1st level entries requiring dynamic allocation of the
 * 1st level table. If the max number is below this threshold, the 1st level
 * table will be allocated as part of the handle table structure. */
#define RTHT_LEVEL1_DYN_ALLOC_THRESHOLD 256

/** Checks whether a object pointer is really a free entry or not. */
#define RTHT_IS_FREE(pvObj)             ( ((uintptr_t)(pvObj) & 3) == 3 )

/** Sets RTHTENTRYFREE::iNext. */
#define RTHT_SET_FREE_IDX(pFree, idx) \
    do { \
        (pFree)->iNext = ((uintptr_t)((uint32_t)(idx)) << 2) | 3U; \
    } while (0)

/** Gets the index part of RTHTENTRYFREE::iNext. */
#define RTHT_GET_FREE_IDX(pFree)        ( (uint32_t)((pFree)->iNext >> 2) )

/** @def NIL_RTHT_INDEX
 * The NIL handle index for use in the free list. (The difference between
 * 32-bit and 64-bit hosts here comes down to the shifting performed for
 * RTHTENTRYFREE::iNext.) */
#if ARCH_BITS == 32
# define NIL_RTHT_INDEX                 ( UINT32_C(0x3fffffff) )
#elif ARCH_BITS >= 34
# define NIL_RTHT_INDEX                 ( UINT32_C(0xffffffff) )
#else
# error "Missing or unsupported ARCH_BITS."
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/

/**
 * Handle table entry, simple variant.
 */
typedef struct RTHTENTRY
{
    /** The object. */
    void *pvObj;
} RTHTENTRY;
/** Pointer to a handle table entry, simple variant. */
typedef RTHTENTRY *PRTHTENTRY;


/**
 * Handle table entry, context variant.
 */
typedef struct RTHTENTRYCTX
{
    /** The object. */
    void *pvObj;
    /** The context. */
    void *pvCtx;
} RTHTENTRYCTX;
/** Pointer to a handle table entry, context variant. */
typedef RTHTENTRYCTX *PRTHTENTRYCTX;


/**
 * Free handle table entry, shared by all variants.
 */
typedef struct RTHTENTRYFREE
{
    /** The index of the next handle, special format.
     * In order to distinguish free and used handle table entries we exploit
     * the heap alignment and use the lower two bits to do this. Used entries
     * will have these bits set to 0, while free entries will have tem set
     * to 3. Use the RTHT_GET_FREE_IDX and RTHT_SET_FREE_IDX macros to access
     * this field. */
    uintptr_t iNext;
} RTHTENTRYFREE;
/** Pointer to a free handle table entry. */
typedef RTHTENTRYFREE *PRTHTENTRYFREE;

AssertCompile(sizeof(RTHTENTRYFREE) <= sizeof(RTHTENTRY));
AssertCompile(sizeof(RTHTENTRYFREE) <= sizeof(RTHTENTRYCTX));
AssertCompileMemberOffset(RTHTENTRYFREE, iNext, 0);
AssertCompileMemberOffset(RTHTENTRY,     pvObj, 0);
AssertCompileMemberOffset(RTHTENTRYCTX,  pvObj, 0);


/**
 * Internal handle table structure.
 */
typedef struct RTHANDLETABLEINT
{
    /** Magic value (RTHANDLETABLE_MAGIC). */
    uint32_t u32Magic;
    /** The handle table flags specified to RTHandleTableCreateEx. */
    uint32_t fFlags;
    /** The base handle value (i.e. the first handle). */
    uint32_t uBase;
    /** The current number of handle table entries. */
    uint32_t cCur;
    /** The spinlock handle (NIL if RTHANDLETABLE_FLAGS_LOCKED wasn't used). */
    RTSPINLOCK hSpinlock;
    /** The level one lookup table. */
    void **papvLevel1;
    /** The retainer callback. Can be NULL. */
    PFNRTHANDLETABLERETAIN pfnRetain;
    /** The user argument to the retainer. */
    void *pvRetainUser;
    /** The max number of handles. */
    uint32_t cMax;
    /** The number of handles currently allocated. (for optimizing destruction) */
    uint32_t cCurAllocated;
    /** The current number of 1st level entries. */
    uint32_t cLevel1;
    /** Head of the list of free handle entires (index). */
    uint32_t iFreeHead;
    /** Tail of the list of free handle entires (index). */
    uint32_t iFreeTail;
} RTHANDLETABLEINT;
/** Pointer to an handle table structure. */
typedef RTHANDLETABLEINT *PRTHANDLETABLEINT;


/**
 * Looks up a simple index.
 *
 * @returns Pointer to the handle table entry on success, NULL on failure.
 * @param   pThis           The handle table structure.
 * @param   i               The index to look up.
 */
DECLINLINE(PRTHTENTRY) rtHandleTableLookupSimpleIdx(PRTHANDLETABLEINT pThis, uint32_t i)
{
    if (i < pThis->cCur)
    {
        PRTHTENTRY paTable = (PRTHTENTRY)pThis->papvLevel1[i / RTHT_LEVEL2_ENTRIES];
        if (paTable)
            return &paTable[i % RTHT_LEVEL2_ENTRIES];
    }
    return NULL;
}


/**
 * Looks up a simple handle.
 *
 * @returns Pointer to the handle table entry on success, NULL on failure.
 * @param   pThis           The handle table structure.
 * @param   h               The handle to look up.
 */
DECLINLINE(PRTHTENTRY) rtHandleTableLookupSimple(PRTHANDLETABLEINT pThis, uint32_t h)
{
    return rtHandleTableLookupSimpleIdx(pThis, h - pThis->uBase);
}


/**
 * Looks up a context index.
 *
 * @returns Pointer to the handle table entry on success, NULL on failure.
 * @param   pThis           The handle table structure.
 * @param   i               The index to look up.
 */
DECLINLINE(PRTHTENTRYCTX) rtHandleTableLookupWithCtxIdx(PRTHANDLETABLEINT pThis, uint32_t i)
{
    if (i < pThis->cCur)
    {
        PRTHTENTRYCTX paTable = (PRTHTENTRYCTX)pThis->papvLevel1[i / RTHT_LEVEL2_ENTRIES];
        if (paTable)
            return &paTable[i % RTHT_LEVEL2_ENTRIES];
    }
    return NULL;
}


/**
 * Looks up a context handle.
 *
 * @returns Pointer to the handle table entry on success, NULL on failure.
 * @param   pThis           The handle table structure.
 * @param   h               The handle to look up.
 */
DECLINLINE(PRTHTENTRYCTX) rtHandleTableLookupWithCtx(PRTHANDLETABLEINT pThis, uint32_t h)
{
    return rtHandleTableLookupWithCtxIdx(pThis, h - pThis->uBase);
}


/**
 * Locks the handle table.
 *
 * @param   pThis           The handle table structure.
 */
DECLINLINE(void) rtHandleTableLock(PRTHANDLETABLEINT pThis)
{
    if (pThis->hSpinlock != NIL_RTSPINLOCK)
        RTSpinlockAcquire(pThis->hSpinlock);
}


/**
 * Locks the handle table.
 *
 * @param   pThis           The handle table structure.
 */
DECLINLINE(void) rtHandleTableUnlock(PRTHANDLETABLEINT pThis)
{
    if (pThis->hSpinlock != NIL_RTSPINLOCK)
        RTSpinlockRelease(pThis->hSpinlock);
}

#endif /* !IPRT_INCLUDED_SRC_common_misc_handletable_h */

