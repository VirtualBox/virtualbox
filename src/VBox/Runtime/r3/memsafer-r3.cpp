/* $Id: memsafer-r3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocate for Sensitive Data, generic heap-based implementation.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/memsafer.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/rand.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/system.h>
#ifdef IN_SUP_R3
# include <VBox/sup.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Allocation size alignment (power of two). */
#define RTMEMSAFER_ALIGN        16


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Allocators.
 */
typedef enum RTMEMSAFERALLOCATOR
{
    /** Invalid method. */
    RTMEMSAFERALLOCATOR_INVALID = 0,
    /** RTMemPageAlloc. */
    RTMEMSAFERALLOCATOR_RTMEMPAGE,
    /** SUPR3PageAllocEx. */
    RTMEMSAFERALLOCATOR_SUPR3
} RTMEMSAFERALLOCATOR;

/**
 * Tracking node (lives on normal heap).
 */
typedef struct RTMEMSAFERNODE
{
    /** Node core.
     * The core key is a scrambled pointer the user memory. */
    AVLPVNODECORE           Core;
    /** The allocation flags. */
    uint32_t                fFlags;
    /** The offset into the allocation of the user memory. */
    uint32_t                offUser;
    /** The requested allocation size. */
    size_t                  cbUser;
    /** The allocation size in pages, this includes the two guard pages. */
    uint32_t                cPages;
    /** The allocator used for this node. */
    RTMEMSAFERALLOCATOR     enmAllocator;
    /** XOR scrambler value for memory. */
    uintptr_t               uScramblerXor;
} RTMEMSAFERNODE;
/** Pointer to an allocation tracking node. */
typedef RTMEMSAFERNODE *PRTMEMSAFERNODE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init once structure for this module. */
static RTONCE       g_MemSaferOnce = RTONCE_INITIALIZER;
/** Critical section protecting the allocation tree. */
static RTCRITSECTRW g_MemSaferCritSect;
/** Tree of allocation nodes. */
static AVLPVTREE    g_pMemSaferTree;
/** XOR scrambler value pointers. */
static uintptr_t    g_uMemSaferPtrScramblerXor;
/** Pointer rotate shift count.*/
static uintptr_t    g_cMemSaferPtrScramblerRotate;


/**
 * @callback_method_impl{FNRTONCE, Inits globals.}
 */
static DECLCALLBACK(int32_t) rtMemSaferOnceInit(void *pvUserIgnore)
{
    RT_NOREF_PV(pvUserIgnore);

    g_uMemSaferPtrScramblerXor = (uintptr_t)RTRandU64();
    g_cMemSaferPtrScramblerRotate = RTRandU32Ex(0, ARCH_BITS - 1);
    return RTCritSectRwInit(&g_MemSaferCritSect);
}


/**
 * @callback_method_impl{PFNRTONCECLEANUP, Cleans up globals.}
 */
static DECLCALLBACK(void) rtMemSaferOnceTerm(void *pvUser, bool fLazyCleanUpOk)
{
    RT_NOREF_PV(pvUser);

    if (!fLazyCleanUpOk)
    {
        RTCritSectRwDelete(&g_MemSaferCritSect);
        Assert(!g_pMemSaferTree);
    }
}



DECLINLINE(void *) rtMemSaferScramblePointer(void *pvUser)
{
    uintptr_t uPtr = (uintptr_t)pvUser;
    uPtr ^= g_uMemSaferPtrScramblerXor;
#if ARCH_BITS == 64
    uPtr = ASMRotateRightU64(uPtr, g_cMemSaferPtrScramblerRotate);
#elif ARCH_BITS == 32
    uPtr = ASMRotateRightU32(uPtr, g_cMemSaferPtrScramblerRotate);
#else
# error "Unsupported/missing ARCH_BITS."
#endif
    return (void *)uPtr;
}


/**
 * Inserts a tracking node into the tree.
 *
 * @param   pThis               The allocation tracking node to insert.
 */
static void rtMemSaferNodeInsert(PRTMEMSAFERNODE pThis)
{
    RTCritSectRwEnterExcl(&g_MemSaferCritSect);
    pThis->Core.Key = rtMemSaferScramblePointer(pThis->Core.Key);
    bool fRc = RTAvlPVInsert(&g_pMemSaferTree, &pThis->Core);
    RTCritSectRwLeaveExcl(&g_MemSaferCritSect);
    Assert(fRc); NOREF(fRc);
}


/**
 * Finds a tracking node into the tree.
 *
 * @returns The allocation tracking node for @a pvUser. NULL if not found.
 * @param   pvUser              The user pointer to the allocation.
 */
static PRTMEMSAFERNODE rtMemSaferNodeLookup(void *pvUser)
{
    void *pvKey = rtMemSaferScramblePointer(pvUser);
    RTCritSectRwEnterShared(&g_MemSaferCritSect);
    PRTMEMSAFERNODE pThis = (PRTMEMSAFERNODE)RTAvlPVGet(&g_pMemSaferTree, pvKey);
    RTCritSectRwLeaveShared(&g_MemSaferCritSect);
    return pThis;
}


/**
 * Removes a tracking node from the tree.
 *
 * @returns The allocation tracking node for @a pvUser. NULL if not found.
 * @param   pvUser              The user pointer to the allocation.
 */
static PRTMEMSAFERNODE rtMemSaferNodeRemove(void *pvUser)
{
    void *pvKey = rtMemSaferScramblePointer(pvUser);
    RTCritSectRwEnterExcl(&g_MemSaferCritSect);
    PRTMEMSAFERNODE pThis = (PRTMEMSAFERNODE)RTAvlPVRemove(&g_pMemSaferTree, pvKey);
    RTCritSectRwLeaveExcl(&g_MemSaferCritSect);
    return pThis;
}


RTDECL(int) RTMemSaferScramble(void *pv, size_t cb)
{
    PRTMEMSAFERNODE pThis = rtMemSaferNodeLookup(pv);
    AssertReturn(pThis, VERR_INVALID_POINTER);
    AssertMsgReturn(cb == pThis->cbUser, ("cb=%#zx != %#zx\n", cb, pThis->cbUser), VERR_INVALID_PARAMETER);

    /* First time we get a new xor value. */
    if (!pThis->uScramblerXor)
        pThis->uScramblerXor = (uintptr_t)RTRandU64();

    /* Note! This isn't supposed to be safe, just less obvious. */
    uintptr_t *pu = (uintptr_t *)pv;
    cb = RT_ALIGN_Z(cb, RTMEMSAFER_ALIGN);
    while (cb > 0)
    {
        *pu ^= pThis->uScramblerXor;
        pu++;
        cb -= sizeof(*pu);
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTMemSaferScramble);


RTDECL(int) RTMemSaferUnscramble(void *pv, size_t cb)
{
    PRTMEMSAFERNODE pThis = rtMemSaferNodeLookup(pv);
    AssertReturn(pThis, VERR_INVALID_POINTER);
    AssertMsgReturn(cb == pThis->cbUser, ("cb=%#zx != %#zx\n", cb, pThis->cbUser), VERR_INVALID_PARAMETER);

    /* Note! This isn't supposed to be safe, just less obvious. */
    uintptr_t *pu = (uintptr_t *)pv;
    cb = RT_ALIGN_Z(cb, RTMEMSAFER_ALIGN);
    while (cb > 0)
    {
        *pu ^= pThis->uScramblerXor;
        pu++;
        cb -= sizeof(*pu);
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTMemSaferUnscramble);


/**
 * Initializes the pages.
 *
 * Fills the memory with random bytes in order to make it less obvious where the
 * secret data starts and ends.  We also zero the user memory in case the
 * allocator does not do this.
 *
 * @param   pThis               The allocation tracer node.  The Core.Key member
 *                              will be set.
 * @param   pvPages             The pages to initialize.
 */
static void rtMemSaferInitializePages(PRTMEMSAFERNODE pThis, void *pvPages)
{
    uint32_t const cbPage = RTSystemGetPageSize();
    RTRandBytes(pvPages, cbPage + pThis->offUser);

    uint8_t *pbUser = (uint8_t *)pvPages + cbPage + pThis->offUser;
    pThis->Core.Key = pbUser;
    RT_BZERO(pbUser, pThis->cbUser); /* paranoia */

    RTRandBytes(pbUser + pThis->cbUser, (size_t)pThis->cPages * cbPage - cbPage - pThis->offUser - pThis->cbUser);
}


/**
 * Allocates and initializes pages from the support driver and initializes it.
 *
 * @returns VBox status code.
 * @param   pThis       The allocator node. Core.Key will be set on successful
 *                      return (unscrambled).
 */
static int rtMemSaferSupR3AllocPages(PRTMEMSAFERNODE pThis)
{
#ifdef IN_SUP_R3
    /*
     * Try allocate the memory.
     */
    void *pvPages;
    int rc = SUPR3PageAllocEx(pThis->cPages, 0 /* fFlags */, &pvPages, NULL /* pR0Ptr */, NULL /* paPages */);
    if (RT_SUCCESS(rc))
    {
        rtMemSaferInitializePages(pThis, pvPages);

        /*
         * On darwin we cannot allocate pages without an R0 mapping and
         * SUPR3PageAllocEx falls back to another method which is incompatible with
         * the way SUPR3PageProtect works. Ignore changing the protection of the guard
         * pages.
         */
#ifdef RT_OS_DARWIN
        return VINF_SUCCESS;
#else
        /*
         * Configure the guard pages.
         * SUPR3PageProtect isn't supported on all hosts, we ignore that.
         */
        uint32_t const cbPage = RTSystemGetPageSize();
        rc = SUPR3PageProtect(pvPages, NIL_RTR0PTR, 0, cbPage, RTMEM_PROT_NONE);
        if (RT_SUCCESS(rc))
        {
            rc = SUPR3PageProtect(pvPages, NIL_RTR0PTR, (pThis->cPages - 1) * cbPage, cbPage, RTMEM_PROT_NONE);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
            SUPR3PageProtect(pvPages, NIL_RTR0PTR, 0, cbPage, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
        }
        else if (rc == VERR_NOT_SUPPORTED)
            return VINF_SUCCESS;

        /* failed. */
        int rc2 = SUPR3PageFreeEx(pvPages, pThis->cPages); AssertRC(rc2);
#endif
    }
    return rc;

#else  /* !IN_SUP_R3 */
    RT_NOREF_PV(pThis);
    return VERR_NOT_SUPPORTED;
#endif /* !IN_SUP_R3 */
}


/**
 * Allocates and initializes pages using the IPRT page allocator API.
 *
 * @returns VBox status code.
 * @param   pThis       The allocator node. Core.Key will be set on successful
 *                      return (unscrambled).
 */
static int rtMemSaferMemAllocPages(PRTMEMSAFERNODE pThis)
{
    /*
     * Try allocate the memory.
     */
    uint32_t const cbPage = RTSystemGetPageSize();
    int rc = VINF_SUCCESS;
    void *pvPages = RTMemPageAllocEx((size_t)pThis->cPages * cbPage,
                                     RTMEMPAGEALLOC_F_ADVISE_LOCKED | RTMEMPAGEALLOC_F_ADVISE_NO_DUMP | RTMEMPAGEALLOC_F_ZERO);
    if (pvPages)
    {
        rtMemSaferInitializePages(pThis, pvPages);

        /*
         * Configure the guard pages.
         */
        rc = RTMemProtect(pvPages, cbPage, RTMEM_PROT_NONE);
        if (RT_SUCCESS(rc))
        {
            rc = RTMemProtect((uint8_t *)pvPages + (size_t)(pThis->cPages - 1U) * cbPage, cbPage, RTMEM_PROT_NONE);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
            rc = RTMemProtect(pvPages, cbPage, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
        }

        /* failed. */
        RTMemPageFree(pvPages, (size_t)pThis->cPages * cbPage);
    }
    else
        rc = VERR_NO_PAGE_MEMORY;

    return rc;
}


RTDECL(int) RTMemSaferAllocZExTag(void **ppvNew, size_t cb, uint32_t fFlags, const char *pszTag) RT_NO_THROW_DEF
{
    RT_NOREF_PV(pszTag);

    /*
     * Validate input.
     */
    AssertPtrReturn(ppvNew, VERR_INVALID_PARAMETER);
    *ppvNew = NULL;
    AssertReturn(cb, VERR_INVALID_PARAMETER);
    uint32_t const cbPage = RTSystemGetPageSize();
    AssertReturn(cb <= 32U*_1M - cbPage * 3U, VERR_ALLOCATION_TOO_BIG); /* Max 32 MB minus padding and guard pages. */
    AssertReturn(!(fFlags & ~RTMEMSAFER_F_VALID_MASK), VERR_INVALID_FLAGS);

    /*
     * Initialize globals.
     */
    int rc = RTOnceEx(&g_MemSaferOnce, rtMemSaferOnceInit, rtMemSaferOnceTerm, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate a tracker node first.
         */
        PRTMEMSAFERNODE pThis = (PRTMEMSAFERNODE)RTMemAllocZ(sizeof(RTMEMSAFERNODE));
        if (pThis)
        {
            /*
             * Prepare the allocation.
             */
            pThis->cbUser  = cb;
            pThis->offUser = (RTRandU32Ex(0, 128) * RTMEMSAFER_ALIGN) & RTSystemGetPageOffsetMask();

            size_t cbNeeded = pThis->offUser + pThis->cbUser;
            cbNeeded = RT_ALIGN_Z(cbNeeded, cbPage);

            pThis->cPages = (uint32_t)(cbNeeded / cbPage) + 2; /* +2 for guard pages */

            /*
             * Try allocate the memory, using the best allocator by default and
             * falling back on the less safe one.
             */
            if (!(fFlags & RTMEMSAFER_F_NO_SUPLIB_ALLOC))
                rc = rtMemSaferSupR3AllocPages(pThis);
            else
                rc = VERR_NO_PAGE_MEMORY;
            if (RT_SUCCESS(rc))
                pThis->enmAllocator = RTMEMSAFERALLOCATOR_SUPR3;
            else if (!(fFlags & RTMEMSAFER_F_REQUIRE_NOT_PAGABLE))
            {
                rc = rtMemSaferMemAllocPages(pThis);
                if (RT_SUCCESS(rc))
                    pThis->enmAllocator = RTMEMSAFERALLOCATOR_RTMEMPAGE;
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Insert the node.
                 */
                *ppvNew = pThis->Core.Key;
                rtMemSaferNodeInsert(pThis); /* (Scrambles Core.Key) */
                return VINF_SUCCESS;
            }

            RTMemFree(pThis);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTMemSaferAllocZExTag);


RTDECL(void) RTMemSaferFree(void *pv, size_t cb) RT_NO_THROW_DEF
{
    if (pv)
    {
        PRTMEMSAFERNODE pThis = rtMemSaferNodeRemove(pv);
        AssertReturnVoid(pThis);
        if (cb == 0) /* for openssl use */
            cb = pThis->cbUser;
        else
            AssertMsg(cb == pThis->cbUser, ("cb=%#zx != %#zx\n", cb, pThis->cbUser));

        /*
         * Wipe the user memory first.
         */
        RTMemWipeThoroughly(pv, RT_ALIGN_Z(cb, RTMEMSAFER_ALIGN), 3);

        /*
         * Free the pages.
         */
        uint32_t const cbPage = RTSystemGetPageSize();
        uint8_t *pbPages = (uint8_t *)pv - pThis->offUser - cbPage;
        size_t   cbPages = (size_t)pThis->cPages * cbPage;
        switch (pThis->enmAllocator)
        {
#ifdef IN_SUP_R3
            case RTMEMSAFERALLOCATOR_SUPR3:
                SUPR3PageProtect(pbPages, NIL_RTR0PTR, 0, cbPage, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
                SUPR3PageProtect(pbPages, NIL_RTR0PTR, (uint32_t)(cbPages - cbPage), cbPage, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
                SUPR3PageFreeEx(pbPages, pThis->cPages);
                break;
#endif
            case RTMEMSAFERALLOCATOR_RTMEMPAGE:
                RTMemProtect(pbPages, cbPage, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
                RTMemProtect(pbPages + cbPages - cbPage, cbPage, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
                RTMemPageFree(pbPages, cbPages);
                break;

            default:
                AssertFailed();
        }

        /*
         * Free the tracking node.
         */
        pThis->Core.Key = NULL;
        pThis->offUser = 0;
        pThis->cbUser = 0;
        RTMemFree(pThis);
    }
    else
        Assert(cb == 0);
}
RT_EXPORT_SYMBOL(RTMemSaferFree);


RTDECL(size_t) RTMemSaferGetSize(void *pv) RT_NO_THROW_DEF
{
    size_t cbRet = 0;
    if (pv)
    {
        /*
         * We use this API for testing whether pv is a safer allocation or not,
         * so we may be called before the allocators.  Thus, it's prudent to
         * make sure initialization has taken place before attempting to enter
         * the critical section and such.
         */
        int rc = RTOnceEx(&g_MemSaferOnce, rtMemSaferOnceInit, rtMemSaferOnceTerm, NULL);
        if (RT_SUCCESS(rc))
        {
            void *pvKey = rtMemSaferScramblePointer(pv);
            RTCritSectRwEnterShared(&g_MemSaferCritSect);
            PRTMEMSAFERNODE pThis = (PRTMEMSAFERNODE)RTAvlPVGet(&g_pMemSaferTree, pvKey);
            if (pThis)
                cbRet = pThis->cbUser;
            RTCritSectRwLeaveShared(&g_MemSaferCritSect);
        }
    }
    return cbRet;
}
RT_EXPORT_SYMBOL(RTMemSaferGetSize);


/**
 * The simplest reallocation method: allocate new block, copy over the data,
 * free old block.
 */
static int rtMemSaferReallocSimpler(size_t cbOld, void *pvOld, size_t cbNew, void **ppvNew, uint32_t fFlags, const char *pszTag)
{
    void *pvNew;
    int rc = RTMemSaferAllocZExTag(&pvNew, cbNew, fFlags, pszTag);
    if (RT_SUCCESS(rc))
    {
        memcpy(pvNew, pvOld, RT_MIN(cbNew, cbOld));
        RTMemSaferFree(pvOld, cbOld);
        *ppvNew = pvNew;
    }
    return rc;
}


RTDECL(int) RTMemSaferReallocZExTag(size_t cbOld, void *pvOld, size_t cbNew, void **ppvNew, uint32_t fFlags, const char *pszTag) RT_NO_THROW_DEF
{
    int rc;
    /* Real realloc. */
    if (cbNew && cbOld)
    {
        PRTMEMSAFERNODE pThis = rtMemSaferNodeLookup(pvOld);
        AssertReturn(pThis, VERR_INVALID_POINTER);
        AssertMsgStmt(cbOld == pThis->cbUser, ("cbOld=%#zx != %#zx\n", cbOld, pThis->cbUser), cbOld = pThis->cbUser);

        if (pThis->fFlags == fFlags)
        {
            if (cbNew > cbOld)
            {
                /*
                 * Is the enough room for us to grow?
                 */
                size_t cbMax = (size_t)(pThis->cPages - 2) * RTSystemGetPageSize();
                if (cbNew <= cbMax)
                {
                    size_t const cbAdded = (cbNew - cbOld);
                    size_t const cbAfter = cbMax - pThis->offUser - cbOld;
                    if (cbAfter >= cbAdded)
                    {
                        /*
                         * Sufficient space after the current allocation.
                         */
                        uint8_t *pbNewSpace = (uint8_t *)pvOld + cbOld;
                        RT_BZERO(pbNewSpace, cbAdded);
                        *ppvNew = pvOld;
                    }
                    else
                    {
                        /*
                         * Have to move the allocation to make enough room at the
                         * end.  In order to make it a little less predictable and
                         * maybe avoid a relocation or two in the next call, divide
                         * the page offset by four until it it fits.
                         */
                        AssertReturn(rtMemSaferNodeRemove(pvOld) == pThis, VERR_INTERNAL_ERROR_3);
                        uint32_t offNewUser = pThis->offUser;
                        do
                            offNewUser = offNewUser / 2;
                        while ((pThis->offUser - offNewUser) + cbAfter < cbAdded);
                        offNewUser &= ~(RTMEMSAFER_ALIGN - 1U);

                        uint32_t const cbMove = pThis->offUser - offNewUser;
                        uint8_t *pbNew = (uint8_t *)pvOld - cbMove;
                        memmove(pbNew, pvOld, cbOld);

                        RT_BZERO(pbNew + cbOld, cbAdded);
                        if (cbMove > cbAdded)
                            RTMemWipeThoroughly(pbNew + cbNew, cbMove - cbAdded, 3);

                        pThis->offUser  = offNewUser;
                        pThis->Core.Key = pbNew;
                        *ppvNew = pbNew;

                        rtMemSaferNodeInsert(pThis);
                    }
                    Assert(((uintptr_t)*ppvNew & RTSystemGetPageOffsetMask()) == pThis->offUser);
                    pThis->cbUser = cbNew;
                    rc = VINF_SUCCESS;
                }
                else
                {
                    /*
                     * Not enough space, allocate a new block and copy over the data.
                     */
                    rc = rtMemSaferReallocSimpler(cbOld, pvOld, cbNew, ppvNew, fFlags, pszTag);
                }
            }
            else
            {
                /*
                 * Shrinking the allocation, just wipe the memory that is no longer
                 * being used.
                 */
                if (cbNew != cbOld)
                {
                    uint8_t *pbAbandond = (uint8_t *)pvOld + cbNew;
                    RTMemWipeThoroughly(pbAbandond, cbOld - cbNew, 3);
                }
                pThis->cbUser = cbNew;
                *ppvNew = pvOld;
                rc = VINF_SUCCESS;
            }
        }
        else if (!pThis->fFlags)
        {
            /*
             * New flags added. Allocate a new block and copy over the old one.
             */
            rc = rtMemSaferReallocSimpler(cbOld, pvOld, cbNew, ppvNew, fFlags, pszTag);
        }
        else
        {
            /* Compatible flags. */
            AssertMsgFailed(("fFlags=%#x old=%#x\n", fFlags, pThis->fFlags));
            rc = VERR_INVALID_FLAGS;
        }
    }
    /*
     * First allocation. Pass it on.
     */
    else if (!cbOld)
    {
        Assert(pvOld == NULL);
        rc = RTMemSaferAllocZExTag(ppvNew, cbNew, fFlags, pszTag);
    }
    /*
     * Free operation. Pass it on.
     */
    else
    {
        RTMemSaferFree(pvOld, cbOld);
        *ppvNew = NULL;
        rc = VINF_SUCCESS;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTMemSaferReallocZExTag);


RTDECL(void *) RTMemSaferAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvNew = NULL;
    int rc = RTMemSaferAllocZExTag(&pvNew, cb, 0 /*fFlags*/, pszTag);
    if (RT_SUCCESS(rc))
        return pvNew;
    return NULL;
}
RT_EXPORT_SYMBOL(RTMemSaferAllocZTag);


RTDECL(void *) RTMemSaferReallocZTag(size_t cbOld, void *pvOld, size_t cbNew, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvNew = NULL;
    int rc = RTMemSaferReallocZExTag(cbOld, pvOld, cbNew, &pvNew, 0 /*fFlags*/, pszTag);
    if (RT_SUCCESS(rc))
        return pvNew;
    return NULL;
}
RT_EXPORT_SYMBOL(RTMemSaferReallocZTag);

