/* $Id: alloc-ef.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, electric fence.
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
#include "alloc-ef.h"
#include <iprt/mem.h>
#include <iprt/log.h>
#include <iprt/asm.h>
#include <iprt/asm-mem.h>
#include <iprt/thread.h>
#include <VBox/sup.h>
#include <iprt/errcore.h>
#ifndef IPRT_NO_CRT
# include <errno.h>
# include <stdio.h>
# include <stdlib.h>
#endif

#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/system.h>

#ifdef RTALLOC_REPLACE_MALLOC
# include <VBox/dis.h>
# include <VBox/disopcode.h>
# include <dlfcn.h>
# ifdef RT_OS_DARWIN
#  include <malloc/malloc.h>
# endif
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef RTALLOC_REPLACE_MALLOC
# define RTMEM_REPLACMENT_ALIGN(a_cb) ((a_cb) >= 16 ? RT_ALIGN_Z(a_cb, 16) \
                                       : (a_cb) >= sizeof(uintptr_t) ? RT_ALIGN_Z(a_cb,  sizeof(uintptr_t)) : (a_cb))
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef RTALLOC_EFENCE_TRACE
/** Spinlock protecting the all the block's globals. */
static volatile uint32_t    g_BlocksLock;
/** Tree tracking the allocations. */
static AVLPVTREE            g_BlocksTree;
# ifdef RTALLOC_EFENCE_FREE_DELAYED
/** Tail of the delayed blocks. */
static volatile PRTMEMBLOCK g_pBlocksDelayHead;
/** Tail of the delayed blocks. */
static volatile PRTMEMBLOCK g_pBlocksDelayTail;
/** Number of bytes in the delay list (includes fences). */
static volatile size_t      g_cbBlocksDelay;
# endif /* RTALLOC_EFENCE_FREE_DELAYED */
# ifdef RTALLOC_REPLACE_MALLOC
/** @name For calling the real allocation API we've replaced.
 * @{ */
void * (*g_pfnOrgMalloc)(size_t);
void * (*g_pfnOrgCalloc)(size_t, size_t);
void * (*g_pfnOrgRealloc)(void *, size_t);
void   (*g_pfnOrgFree)(void *);
size_t (*g_pfnOrgMallocSize)(void *);
/** @} */
# endif
#endif /* RTALLOC_EFENCE_TRACE */
/** Array of pointers free watches for. */
void   *gapvRTMemFreeWatch[4] = {NULL, NULL, NULL, NULL};
/** Enable logging of all freed memory. */
bool    gfRTMemFreeLog = false;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef RTALLOC_REPLACE_MALLOC
static void rtMemReplaceMallocAndFriends(void);
#endif


/**
 * Complains about something.
 */
static void rtmemComplain(const char *pszOp, const char *pszFormat, ...)
{
    va_list args;
    fprintf(stderr, "RTMem error: %s: ", pszOp);
    va_start(args, pszFormat);
    vfprintf(stderr, pszFormat, args);
    va_end(args);
    RTAssertDoPanic();
}

/**
 * Log an event.
 */
DECLINLINE(void) rtmemLog(const char *pszOp, const char *pszFormat, ...)
{
#if 0
    va_list args;
    fprintf(stderr, "RTMem info: %s: ", pszOp);
    va_start(args, pszFormat);
    vfprintf(stderr, pszFormat, args);
    va_end(args);
#else
    NOREF(pszOp); NOREF(pszFormat);
#endif
}


#ifdef RTALLOC_EFENCE_TRACE

/**
 * Acquires the lock.
 */
DECLINLINE(void) rtmemBlockLock(void)
{
    unsigned c = 0;
    while (!ASMAtomicCmpXchgU32(&g_BlocksLock, 1, 0))
        RTThreadSleepNoLog(((++c) >> 2) & 31);
}


/**
 * Releases the lock.
 */
DECLINLINE(void) rtmemBlockUnlock(void)
{
    Assert(g_BlocksLock == 1);
    ASMAtomicXchgU32(&g_BlocksLock, 0);
}


/**
 * Creates a block.
 */
DECLINLINE(PRTMEMBLOCK) rtmemBlockCreate(RTMEMTYPE enmType, size_t cbUnaligned, size_t cbAligned,
                                         const char *pszTag, void *pvCaller, RT_SRC_POS_DECL)
{
# ifdef RTALLOC_REPLACE_MALLOC
    if (!g_pfnOrgMalloc)
        rtMemReplaceMallocAndFriends();
    PRTMEMBLOCK pBlock = (PRTMEMBLOCK)g_pfnOrgMalloc(sizeof(*pBlock));
# else
    PRTMEMBLOCK pBlock = (PRTMEMBLOCK)malloc(sizeof(*pBlock));
# endif
    if (pBlock)
    {
        pBlock->enmType     = enmType;
        pBlock->cbUnaligned = cbUnaligned;
        pBlock->cbAligned   = cbAligned;
        pBlock->pszTag      = pszTag;
        pBlock->pvCaller    = pvCaller;
        pBlock->iLine       = iLine;
        pBlock->pszFile     = pszFile;
        pBlock->pszFunction = pszFunction;
    }
    return pBlock;
}


/**
 * Frees a block.
 */
DECLINLINE(void) rtmemBlockFree(PRTMEMBLOCK pBlock)
{
# ifdef RTALLOC_REPLACE_MALLOC
    g_pfnOrgFree(pBlock);
# else
    free(pBlock);
# endif
}


/**
 * Insert a block from the tree.
 */
DECLINLINE(void) rtmemBlockInsert(PRTMEMBLOCK pBlock, void *pv)
{
    pBlock->Core.Key = pv;
    rtmemBlockLock();
    bool fRc = RTAvlPVInsert(&g_BlocksTree, &pBlock->Core);
    rtmemBlockUnlock();
    AssertRelease(fRc);
}


/**
 * Remove a block from the tree and returns it to the caller.
 */
DECLINLINE(PRTMEMBLOCK) rtmemBlockRemove(void *pv)
{
    rtmemBlockLock();
    PRTMEMBLOCK pBlock = (PRTMEMBLOCK)RTAvlPVRemove(&g_BlocksTree, pv);
    rtmemBlockUnlock();
    return pBlock;
}

/**
 * Gets a block.
 */
DECLINLINE(PRTMEMBLOCK) rtmemBlockGet(void *pv)
{
    rtmemBlockLock();
    PRTMEMBLOCK pBlock = (PRTMEMBLOCK)RTAvlPVGet(&g_BlocksTree, pv);
    rtmemBlockUnlock();
    return pBlock;
}

/**
 * Dumps one allocation.
 */
static DECLCALLBACK(int) RTMemDumpOne(PAVLPVNODECORE pNode, void *pvUser)
{
    PRTMEMBLOCK pBlock = (PRTMEMBLOCK)pNode;
    fprintf(stderr, "%p %08lx(+%02lx) %p\n",
            pBlock->Core.Key,
            (unsigned long)pBlock->cbUnaligned,
            (unsigned long)(pBlock->cbAligned - pBlock->cbUnaligned),
            pBlock->pvCaller);
    NOREF(pvUser);
    return 0;
}

/**
 * Dumps the allocated blocks.
 * This is something which you should call from gdb.
 */
extern "C" void RTMemDump(void);
void RTMemDump(void)
{
    fprintf(stderr, "address  size(alg)     caller\n");
    RTAvlPVDoWithAll(&g_BlocksTree, true, RTMemDumpOne, NULL);
}

# ifdef RTALLOC_EFENCE_FREE_DELAYED

/**
 * Insert a delayed block.
 */
DECLINLINE(void) rtmemBlockDelayInsert(PRTMEMBLOCK pBlock)
{
    size_t const cbFence = RTALLOC_EFENCE_SIZE_FACTOR * RTSystemGetPageSize();
    size_t const cbBlock = RTSystemPageAlignSize(pBlock->cbAligned) + cbFence;
    pBlock->Core.pRight = NULL;
    pBlock->Core.pLeft = NULL;
    rtmemBlockLock();
    if (g_pBlocksDelayHead)
    {
        g_pBlocksDelayHead->Core.pLeft = (PAVLPVNODECORE)pBlock;
        pBlock->Core.pRight = (PAVLPVNODECORE)g_pBlocksDelayHead;
        g_pBlocksDelayHead = pBlock;
    }
    else
    {
        g_pBlocksDelayTail = pBlock;
        g_pBlocksDelayHead = pBlock;
    }
    g_cbBlocksDelay += cbBlock;
    rtmemBlockUnlock();
}

/**
 * Removes a delayed block.
 */
DECLINLINE(PRTMEMBLOCK) rtmemBlockDelayRemove(void)
{
    PRTMEMBLOCK pBlock = NULL;
    rtmemBlockLock();
    if (g_cbBlocksDelay > RTALLOC_EFENCE_FREE_DELAYED)
    {
        pBlock = g_pBlocksDelayTail;
        if (pBlock)
        {
            g_pBlocksDelayTail = (PRTMEMBLOCK)pBlock->Core.pLeft;
            if (pBlock->Core.pLeft)
                pBlock->Core.pLeft->pRight = NULL;
            else
                g_pBlocksDelayHead = NULL;

            size_t const cbFence = RTALLOC_EFENCE_SIZE_FACTOR * RTSystemGetPageSize();
            g_cbBlocksDelay -= RTSystemPageAlignSize(pBlock->cbAligned) + cbFence;
        }
    }
    rtmemBlockUnlock();
    return pBlock;
}


/**
 * Dumps the freed blocks.
 * This is something which you should call from gdb.
 */
extern "C" void RTMemDumpFreed(void);
void RTMemDumpFreed(void)
{
    fprintf(stderr, "address  size(alg)     caller\n");
    for (PRTMEMBLOCK pCur = g_pBlocksDelayHead; pCur; pCur = (PRTMEMBLOCK)pCur->Core.pRight)
        RTMemDumpOne(&pCur->Core, NULL);

}

# endif  /* RTALLOC_EFENCE_FREE_DELAYED */

#endif /* RTALLOC_EFENCE_TRACE */


#if defined(RTALLOC_REPLACE_MALLOC) && defined(RTALLOC_EFENCE_TRACE)
/*
 *
 * Replacing malloc, calloc, realloc, & free.
 *
 */

/** Replacement for malloc. */
static void *rtMemReplacementMalloc(size_t cb)
{
    size_t cbAligned = RTMEM_REPLACMENT_ALIGN(cb);
    void *pv = rtR3MemAlloc("r-malloc", RTMEMTYPE_RTMEMALLOC, cb, cbAligned, "heap", ASMReturnAddress(), RT_SRC_POS);
    if (!pv)
        pv = g_pfnOrgMalloc(cb);
    return pv;
}

/** Replacement for calloc. */
static void *rtMemReplacementCalloc(size_t cbItem, size_t cItems)
{
    size_t cb = cbItem * cItems;
    size_t cbAligned = RTMEM_REPLACMENT_ALIGN(cb);
    void *pv = rtR3MemAlloc("r-calloc", RTMEMTYPE_RTMEMALLOCZ, cb, cbAligned, "heap", ASMReturnAddress(), RT_SRC_POS);
    if (!pv)
        pv = g_pfnOrgCalloc(cbItem, cItems);
    return pv;
}

/** Replacement for realloc. */
static void *rtMemReplacementRealloc(void *pvOld, size_t cbNew)
{
    if (pvOld)
    {
        /* We're not strict about where the memory was allocated. */
        PRTMEMBLOCK pBlock = rtmemBlockGet(pvOld);
        if (pBlock)
        {
            size_t cbAligned = RTMEM_REPLACMENT_ALIGN(cbNew);
            return rtR3MemRealloc("r-realloc", RTMEMTYPE_RTMEMREALLOC, pvOld, cbAligned, "heap", ASMReturnAddress(), RT_SRC_POS);
        }
        return g_pfnOrgRealloc(pvOld, cbNew);
    }
    return rtMemReplacementMalloc(cbNew);
}

/** Replacement for free(). */
static void rtMemReplacementFree(void *pv)
{
    if (pv)
    {
        /* We're not strict about where the memory was allocated. */
        PRTMEMBLOCK pBlock = rtmemBlockGet(pv);
        if (pBlock)
            rtR3MemFree("r-free", RTMEMTYPE_RTMEMFREE, pv, 0, ASMReturnAddress(), RT_SRC_POS);
        else
            g_pfnOrgFree(pv);
    }
}

# ifdef RT_OS_DARWIN
/** Replacement for malloc. */
static size_t rtMemReplacementMallocSize(void *pv)
{
    size_t cb;
    if (pv)
    {
        /* We're not strict about where the memory was allocated. */
        PRTMEMBLOCK pBlock = rtmemBlockGet(pv);
        if (pBlock)
            cb = pBlock->cbUnaligned;
        else
            cb = g_pfnOrgMallocSize(pv);
    }
    else
        cb = 0;
    return cb;
}
# endif


static void rtMemReplaceMallocAndFriends(void)
{
    struct
    {
        const char *pszName;
        PFNRT       pfnReplacement;
        PFNRT       pfnOrg;
        PFNRT      *ppfnJumpBack;
    } aApis[] =
    {
        { "free",    (PFNRT)rtMemReplacementFree,    (PFNRT)free,    (PFNRT *)&g_pfnOrgFree },
        { "realloc", (PFNRT)rtMemReplacementRealloc, (PFNRT)realloc, (PFNRT *)&g_pfnOrgRealloc },
        { "calloc",  (PFNRT)rtMemReplacementCalloc,  (PFNRT)calloc,  (PFNRT *)&g_pfnOrgCalloc },
        { "malloc",  (PFNRT)rtMemReplacementMalloc,  (PFNRT)malloc,  (PFNRT *)&g_pfnOrgMalloc },
#ifdef RT_OS_DARWIN
        { "malloc_size", (PFNRT)rtMemReplacementMallocSize,  (PFNRT)malloc_size,  (PFNRT *)&g_pfnOrgMallocSize },
#endif
    };

    /*
     * Initialize the jump backs to avoid recursivly entering this function.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(aApis); i++)
        *aApis[i].ppfnJumpBack = aApis[i].pfnOrg;

    /*
     * Give the user an option to skip replacing malloc.
     */
    if (getenv("IPRT_DONT_REPLACE_MALLOC"))
        return;

    /*
     * Allocate a page for jump back code (we leak it).
     */
    size_t const cbPage = RTSystemGetPageSize();
    uint8_t *pbExecPage = (uint8_t *)RTMemPageAlloc(cbPage); AssertFatal(pbExecPage);
    int rc = RTMemProtect(pbExecPage, cbPage, RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC); AssertFatalRC(rc);

    /*
     * Do the ground work.
     */
    uint8_t *pb = pbExecPage;
    for (unsigned i = 0; i < RT_ELEMENTS(aApis); i++)
    {
        /* Resolve it. */
        PFNRT pfnOrg = (PFNRT)(uintptr_t)dlsym(RTLD_DEFAULT, aApis[i].pszName);
        if (pfnOrg)
            aApis[i].pfnOrg = pfnOrg;
        else
            pfnOrg = aApis[i].pfnOrg;

        /* Figure what we can replace and how much to duplicate in the jump back code. */
# ifdef RT_ARCH_AMD64
        uint32_t         cbNeeded   = 12;
        DISCPUMODE const enmCpuMode = DISCPUMODE_64BIT;
# elif defined(RT_ARCH_X86)
        uint32_t   const cbNeeded   = 5;
        DISCPUMODE const enmCpuMode = DISCPUMODE_32BIT;
# else
#  error "Port me"
# endif
        uint32_t offJmpBack = 0;
        uint32_t cbCopy = 0;
        while (offJmpBack < cbNeeded)
        {
            DISCPUSTATE Dis;
            uint32_t cbInstr = 1;
            rc = DISInstr((void *)((uintptr_t)pfnOrg + offJmpBack), enmCpuMode, &Dis, &cbInstr); AssertFatalRC(rc);
            AssertFatal(!(Dis.pCurInstr->fOpType & (DISOPTYPE_CONTROLFLOW)));
# ifdef RT_ARCH_AMD64
#  ifdef RT_OS_DARWIN
            /* Kludge for: cmp [malloc_def_zone_state], 1; jg 2; call _malloc_initialize; 2: */
            if (   Dis.ModRM.Bits.Mod == 0
                && Dis.ModRM.Bits.Rm == 5 /* wrt RIP */
                && (Dis.Param2.fUse & (DISUSE_IMMEDIATE16_SX8 | DISUSE_IMMEDIATE32_SX8 | DISUSE_IMMEDIATE64_SX8))
                && Dis.Param2.uValue == 1
                && Dis.pCurInstr->uOpcode == OP_CMP)
            {
                cbCopy = offJmpBack;

                offJmpBack += cbInstr;
                rc = DISInstr((void *)((uintptr_t)pfnOrg + offJmpBack), enmCpuMode, &Dis, &cbInstr); AssertFatalRC(rc);
                if (   Dis.pCurInstr->uOpcode == OP_JNBE
                    && Dis.Param1.uDisp.i8 == 5)
                {
                    offJmpBack += cbInstr + 5;
                    AssertFatal(offJmpBack >= cbNeeded);
                    break;
                }
            }
#  endif
            AssertFatal(!(Dis.ModRM.Bits.Mod == 0 && Dis.ModRM.Bits.Rm == 5 /* wrt RIP */));
# endif
            offJmpBack += cbInstr;
        }
        if (!cbCopy)
            cbCopy = offJmpBack;

        /* Assemble the jump back. */
        memcpy(pb, (void *)(uintptr_t)pfnOrg, cbCopy);
        uint32_t off = cbCopy;
# ifdef RT_ARCH_AMD64
        pb[off++] = 0xff; /* jmp qword [$+8 wrt RIP] */
        pb[off++] = 0x25;
        *(uint32_t *)&pb[off] = 0;
        off += 4;
        *(uint64_t *)&pb[off] = (uintptr_t)pfnOrg + offJmpBack;
        off += 8;
        off = RT_ALIGN_32(off, 16);
# elif defined(RT_ARCH_X86)
        pb[off++] = 0xe9; /* jmp rel32 */
        *(uint32_t *)&pb[off] = (uintptr_t)pfnOrg + offJmpBack - (uintptr_t)&pb[4];
        off += 4;
        off = RT_ALIGN_32(off, 8);
# else
#  error "Port me"
# endif
        *aApis[i].ppfnJumpBack = (PFNRT)(uintptr_t)pb;
        pb += off;
    }

    /*
     * Modify the APIs.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(aApis); i++)
    {
        pb = (uint8_t *)(uintptr_t)aApis[i].pfnOrg;
        rc = RTMemProtect(pb, 16, RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC); AssertFatalRC(rc);

# ifdef RT_ARCH_AMD64
        /* Assemble the LdrLoadDll patch. */
        *pb++ = 0x48; /* mov rax, qword */
        *pb++ = 0xb8;
        *(uint64_t *)pb = (uintptr_t)aApis[i].pfnReplacement;
        pb += 8;
        *pb++ = 0xff; /* jmp rax */
        *pb++ = 0xe0;
# elif defined(RT_ARCH_X86)
        *pb++ = 0xe9; /* jmp rel32 */
        *(uint32_t *)pb = (uintptr_t)aApis[i].pfnReplacement - (uintptr_t)&pb[4];
# else
#  error "Port me"
# endif
    }
}

#endif /* RTALLOC_REPLACE_MALLOC && RTALLOC_EFENCE_TRACE */


/**
 * Internal allocator.
 */
RTDECL(void *) rtR3MemAlloc(const char *pszOp, RTMEMTYPE enmType, size_t cbUnaligned, size_t cbAligned,
                            const char *pszTag, void *pvCaller, RT_SRC_POS_DECL)
{
    /*
     * Sanity.
     */
    size_t const cbFence = RTALLOC_EFENCE_SIZE_FACTOR * RTSystemGetPageSize();
    size_t const cbPage = RTSystemGetPageSize();
    if (RTALLOC_EFENCE_SIZE_FACTOR <= 0)
    {
        rtmemComplain(pszOp, "Invalid E-fence size! %#x\n", RTALLOC_EFENCE_SIZE_FACTOR);
        return NULL;
    }
    if (!cbUnaligned)
    {
#if 0
        rtmemComplain(pszOp, "Request of ZERO bytes allocation!\n");
        return NULL;
#else
        cbAligned = cbUnaligned = 1;
#endif
    }

#ifndef RTALLOC_EFENCE_IN_FRONT
    /* Alignment decreases fence accuracy, but this is at least partially
     * counteracted by filling and checking the alignment padding. When the
     * fence is in front then then no extra alignment is needed. */
    cbAligned = RT_ALIGN_Z(cbAligned, RTALLOC_EFENCE_ALIGNMENT);
#endif

#ifdef RTALLOC_EFENCE_TRACE
    /*
     * Allocate the trace block.
     */
    PRTMEMBLOCK pBlock = rtmemBlockCreate(enmType, cbUnaligned, cbAligned, pszTag, pvCaller, RT_SRC_POS_ARGS);
    if (!pBlock)
    {
        rtmemComplain(pszOp, "Failed to allocate trace block!\n");
        return NULL;
    }
#endif

    /*
     * Allocate a block with page alignment space + the size of the E-fence.
     */
    size_t  cbBlock = RT_ALIGN_Z(cbAligned, cbPage) + cbFence;
    void   *pvBlock = RTMemPageAlloc(cbBlock);
    if (pvBlock)
    {
        /*
         * Calc the start of the fence and the user block
         * and then change the page protection of the fence.
         */
#ifdef RTALLOC_EFENCE_IN_FRONT
        void *pvEFence = pvBlock;
        void *pv       = (char *)pvEFence + cbFence;
# ifdef RTALLOC_EFENCE_NOMAN_FILLER
        memset((char *)pv + cbUnaligned, RTALLOC_EFENCE_NOMAN_FILLER, cbBlock - cbFence - cbUnaligned);
# endif
#else
        void *pvEFence = (char *)pvBlock + (cbBlock - cbFence);
        void *pv       = (char *)pvEFence - cbAligned;
# ifdef RTALLOC_EFENCE_NOMAN_FILLER
        memset(pvBlock, RTALLOC_EFENCE_NOMAN_FILLER, cbBlock - cbFence - cbAligned);
        memset((char *)pv + cbUnaligned, RTALLOC_EFENCE_NOMAN_FILLER, cbAligned - cbUnaligned);
# endif
#endif

#ifdef RTALLOC_EFENCE_FENCE_FILLER
        memset(pvEFence, RTALLOC_EFENCE_FENCE_FILLER, cbFence);
#endif
        int rc = RTMemProtect(pvEFence, cbFence, RTMEM_PROT_NONE);
        if (!rc)
        {
#ifdef RTALLOC_EFENCE_TRACE
            rtmemBlockInsert(pBlock, pv);
#endif
            if (enmType == RTMEMTYPE_RTMEMALLOCZ)
                memset(pv, 0, cbUnaligned);
#ifdef RTALLOC_EFENCE_FILLER
            else
                memset(pv, RTALLOC_EFENCE_FILLER, cbUnaligned);
#endif

            rtmemLog(pszOp, "returns %p (pvBlock=%p cbBlock=%#x pvEFence=%p cbUnaligned=%#x)\n", pv, pvBlock, cbBlock, pvEFence, cbUnaligned);
            return pv;
        }
        rtmemComplain(pszOp, "RTMemProtect failed, pvEFence=%p size %d, rc=%d\n", pvEFence, cbFence, rc);
        RTMemPageFree(pvBlock, cbBlock);
    }
    else
        rtmemComplain(pszOp, "Failed to allocated %lu (%lu) bytes.\n", (unsigned long)cbBlock, (unsigned long)cbUnaligned);

#ifdef RTALLOC_EFENCE_TRACE
    rtmemBlockFree(pBlock);
#endif
    return NULL;
}


/**
 * Internal free.
 */
RTDECL(void) rtR3MemFree(const char *pszOp, RTMEMTYPE enmType, void *pv, size_t cbUser, void *pvCaller, RT_SRC_POS_DECL)
{
    NOREF(enmType); RT_SRC_POS_NOREF();

    /*
     * Simple case.
     */
    if (!pv)
        return;

    /*
     * Check watch points.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(gapvRTMemFreeWatch); i++)
        if (gapvRTMemFreeWatch[i] == pv)
            RTAssertDoPanic();

    size_t cbPage = RTSystemGetPageSize();
#ifdef RTALLOC_EFENCE_TRACE
    /*
     * Find the block.
     */
    PRTMEMBLOCK pBlock = rtmemBlockRemove(pv);
    if (pBlock)
    {
        if (gfRTMemFreeLog)
            RTLogPrintf("RTMem %s: pv=%p pvCaller=%p cbUnaligned=%#x\n", pszOp, pv, pvCaller, pBlock->cbUnaligned);

# ifdef RTALLOC_EFENCE_NOMAN_FILLER
        /*
         * Check whether the no man's land is untouched.
         */
#  ifdef RTALLOC_EFENCE_IN_FRONT
        void *pvWrong = ASMMemFirstMismatchingU8((char *)pv + pBlock->cbUnaligned,
                                                 RT_ALIGN_Z(pBlock->cbAligned, cbPage) - pBlock->cbUnaligned,
                                                 RTALLOC_EFENCE_NOMAN_FILLER);
#  else
        /* Alignment must match allocation alignment in rtMemAlloc(). */
        void  *pvWrong = ASMMemFirstMismatchingU8((char *)pv + pBlock->cbUnaligned,
                                                  pBlock->cbAligned - pBlock->cbUnaligned,
                                                  RTALLOC_EFENCE_NOMAN_FILLER);
        if (pvWrong)
            RTAssertDoPanic();
        pvWrong = ASMMemFirstMismatchingU8((void *)((uintptr_t)pv & ~RTSystemGetPageOffsetMask()),
                                           RT_ALIGN_Z(pBlock->cbAligned, cbPage) - pBlock->cbAligned,
                                           RTALLOC_EFENCE_NOMAN_FILLER);
#  endif
        if (pvWrong)
            RTAssertDoPanic();
# endif

        /*
         * Fill the user part of the block.
         */
        AssertMsg(enmType != RTMEMTYPE_RTMEMFREEZ || cbUser == pBlock->cbUnaligned,
                  ("cbUser=%#zx cbUnaligned=%#zx\n", cbUser, pBlock->cbUnaligned));
        RT_NOREF(cbUser);
        if (enmType == RTMEMTYPE_RTMEMFREEZ)
            RT_BZERO(pv, pBlock->cbUnaligned);
# ifdef RTALLOC_EFENCE_FREE_FILL
        else
            memset(pv, RTALLOC_EFENCE_FREE_FILL, pBlock->cbUnaligned);
# endif

        size_t const cbFence = RTALLOC_EFENCE_SIZE_FACTOR * RTSystemGetPageSize();
# if defined(RTALLOC_EFENCE_FREE_DELAYED) && RTALLOC_EFENCE_FREE_DELAYED > 0
        /*
         * We're doing delayed freeing.
         * That means we'll expand the E-fence to cover the entire block.
         */
        int rc = RTMemProtect(pv, pBlock->cbAligned, RTMEM_PROT_NONE);
        if (RT_SUCCESS(rc))
        {
            /*
             * Insert it into the free list and process pending frees.
             */
            rtmemBlockDelayInsert(pBlock);
            while ((pBlock = rtmemBlockDelayRemove()) != NULL)
            {
                pv = pBlock->Core.Key;
#  ifdef RTALLOC_EFENCE_IN_FRONT
                void  *pvBlock = (char *)pv - cbFence;
#  else
                void  *pvBlock = (void *)((uintptr_t)pv & ~RTSystemGetPageOffsetMask());
#  endif
                size_t cbBlock = RT_ALIGN_Z(pBlock->cbAligned, cbPage) + cbFence;
                rc = RTMemProtect(pvBlock, cbBlock, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
                if (RT_SUCCESS(rc))
                    RTMemPageFree(pvBlock, RT_ALIGN_Z(pBlock->cbAligned, cbPage) + cbFence);
                else
                    rtmemComplain(pszOp, "RTMemProtect(%p, %#x, RTMEM_PROT_READ | RTMEM_PROT_WRITE) -> %d\n", pvBlock, cbBlock, rc);
                rtmemBlockFree(pBlock);
            }
        }
        else
            rtmemComplain(pszOp, "Failed to expand the efence of pv=%p cb=%d, rc=%d.\n", pv, pBlock, rc);

# else /* !RTALLOC_EFENCE_FREE_DELAYED */

        /*
         * Turn of the E-fence and free it.
         */
#  ifdef RTALLOC_EFENCE_IN_FRONT
        void *pvBlock = (char *)pv - cbFence;
        void *pvEFence = pvBlock;
#  else
        void *pvBlock = (void *)((uintptr_t)pv & ~RTSystemGetPageOffsetMask());
        void *pvEFence = (char *)pv + pBlock->cb;
#  endif
        int rc = RTMemProtect(pvEFence, cbFence, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
        if (RT_SUCCESS(rc))
            RTMemPageFree(pvBlock, RT_ALIGN_Z(pBlock->cbAligned, cbPage) + cbFence);
        else
            rtmemComplain(pszOp, "RTMemProtect(%p, %#x, RTMEM_PROT_READ | RTMEM_PROT_WRITE) -> %d\n", pvEFence, cbFence, rc);
        rtmemBlockFree(pBlock);

# endif /* !RTALLOC_EFENCE_FREE_DELAYED */
    }
    else
        rtmemComplain(pszOp, "pv=%p not found! Incorrect free!\n", pv);

#else /* !RTALLOC_EFENCE_TRACE */

    /*
     * We have no size tracking, so we're not doing any freeing because
     * we cannot if the E-fence is after the block.
     * Let's just expand the E-fence to the first page of the user bit
     * since we know that it's around.
     */
    if (enmType == RTMEMTYPE_RTMEMFREEZ)
        RT_BZERO(pv, cbUser);
    int rc = RTMemProtect((void *)((uintptr_t)pv & ~RTSystemGetPageOffsetMask()), cbPage, RTMEM_PROT_NONE);
    if (RT_FAILURE(rc))
        rtmemComplain(pszOp, "RTMemProtect(%p, cbPage, RTMEM_PROT_NONE) -> %d\n", (void *)((uintptr_t)pv & ~RTSystemGetPageOffsetMask()), rc);
#endif /* !RTALLOC_EFENCE_TRACE */
}


/**
 * Internal realloc.
 */
RTDECL(void *) rtR3MemRealloc(const char *pszOp, RTMEMTYPE enmType, void *pvOld, size_t cbNew,
                              const char *pszTag, void *pvCaller, RT_SRC_POS_DECL)
{
    /*
     * Allocate new and copy.
     */
    if (!pvOld)
        return rtR3MemAlloc(pszOp, enmType, cbNew, cbNew, pszTag, pvCaller, RT_SRC_POS_ARGS);
    if (!cbNew)
    {
        rtR3MemFree(pszOp, RTMEMTYPE_RTMEMREALLOC, pvOld, 0, pvCaller, RT_SRC_POS_ARGS);
        return NULL;
    }

#ifdef RTALLOC_EFENCE_TRACE

    /*
     * Get the block, allocate the new, copy the data, free the old one.
     */
    PRTMEMBLOCK pBlock = rtmemBlockGet(pvOld);
    if (pBlock)
    {
        void *pvRet = rtR3MemAlloc(pszOp, enmType, cbNew, cbNew, pszTag, pvCaller, RT_SRC_POS_ARGS);
        if (pvRet)
        {
            memcpy(pvRet, pvOld, RT_MIN(cbNew, pBlock->cbUnaligned));
            rtR3MemFree(pszOp, RTMEMTYPE_RTMEMREALLOC, pvOld, 0, pvCaller, RT_SRC_POS_ARGS);
        }
        return pvRet;
    }
    else
        rtmemComplain(pszOp, "pvOld=%p was not found!\n", pvOld);
    return NULL;

#else /* !RTALLOC_EFENCE_TRACE */

    rtmemComplain(pszOp, "Not supported if RTALLOC_EFENCE_TRACE isn't defined!\n");
    return NULL;

#endif /* !RTALLOC_EFENCE_TRACE */
}




RTDECL(void *)  RTMemEfTmpAlloc(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR3MemAlloc("TmpAlloc", RTMEMTYPE_RTMEMALLOC, cb, cb, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfTmpAllocZ(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR3MemAlloc("TmpAlloc", RTMEMTYPE_RTMEMALLOCZ, cb, cb, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void)    RTMemEfTmpFree(void *pv, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    if (pv)
        rtR3MemFree("Free", RTMEMTYPE_RTMEMFREE, pv, 0, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void)    RTMemEfTmpFreeZ(void *pv, size_t cb, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    if (pv)
        rtR3MemFree("FreeZ", RTMEMTYPE_RTMEMFREEZ, pv, cb, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfAlloc(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR3MemAlloc("Alloc", RTMEMTYPE_RTMEMALLOC, cb, cb, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfAllocZ(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR3MemAlloc("AllocZ", RTMEMTYPE_RTMEMALLOCZ, cb, cb, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfAllocVar(size_t cbUnaligned, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return rtR3MemAlloc("Alloc", RTMEMTYPE_RTMEMALLOC, cbUnaligned, cbAligned, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfAllocZVar(size_t cbUnaligned, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return rtR3MemAlloc("AllocZ", RTMEMTYPE_RTMEMALLOCZ, cbUnaligned, cbAligned, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfRealloc(void *pvOld, size_t cbNew, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR3MemRealloc("Realloc", RTMEMTYPE_RTMEMREALLOC, pvOld, cbNew, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfReallocZ(void *pvOld, size_t cbOld, size_t cbNew, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    void *pvDst = rtR3MemRealloc("ReallocZ", RTMEMTYPE_RTMEMREALLOC, pvOld, cbNew, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
    if (pvDst && cbNew > cbOld)
        memset((uint8_t *)pvDst + cbOld, 0, cbNew - cbOld);
    return pvDst;
}


RTDECL(void)    RTMemEfFree(void *pv, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    if (pv)
        rtR3MemFree("Free", RTMEMTYPE_RTMEMFREE, pv, 0, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void)    RTMemEfFreeZ(void *pv, size_t cb, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    if (pv)
        rtR3MemFree("FreeZ", RTMEMTYPE_RTMEMFREEZ, pv, cb, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *) RTMemEfDup(const void *pvSrc, size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    void *pvDst = RTMemEfAlloc(cb, pszTag, RT_SRC_POS_ARGS);
    if (pvDst)
        memcpy(pvDst, pvSrc, cb);
    return pvDst;
}


RTDECL(void *) RTMemEfDupEx(const void *pvSrc, size_t cbSrc, size_t cbExtra, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    void *pvDst = RTMemEfAlloc(cbSrc + cbExtra, pszTag, RT_SRC_POS_ARGS);
    if (pvDst)
    {
        memcpy(pvDst, pvSrc, cbSrc);
        memset((uint8_t *)pvDst + cbSrc, 0, cbExtra);
    }
    return pvDst;
}




/*
 *
 * The NP (no position) versions.
 *
 */



RTDECL(void *)  RTMemEfTmpAllocNP(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtR3MemAlloc("TmpAlloc", RTMEMTYPE_RTMEMALLOC, cb, cb, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfTmpAllocZNP(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtR3MemAlloc("TmpAllocZ", RTMEMTYPE_RTMEMALLOCZ, cb, cb, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void)    RTMemEfTmpFreeNP(void *pv) RT_NO_THROW_DEF
{
    if (pv)
        rtR3MemFree("Free", RTMEMTYPE_RTMEMFREE, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void)    RTMemEfTmpFreeZNP(void *pv, size_t cb) RT_NO_THROW_DEF
{
    if (pv)
        rtR3MemFree("FreeZ", RTMEMTYPE_RTMEMFREEZ, pv, cb, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfAllocNP(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtR3MemAlloc("Alloc", RTMEMTYPE_RTMEMALLOC, cb, cb, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfAllocZNP(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtR3MemAlloc("AllocZ", RTMEMTYPE_RTMEMALLOCZ, cb, cb, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfAllocVarNP(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return rtR3MemAlloc("Alloc", RTMEMTYPE_RTMEMALLOC, cbUnaligned, cbAligned, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfAllocZVarNP(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return rtR3MemAlloc("AllocZ", RTMEMTYPE_RTMEMALLOCZ, cbUnaligned, cbAligned, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfReallocNP(void *pvOld, size_t cbNew, const char *pszTag) RT_NO_THROW_DEF
{
    return rtR3MemRealloc("Realloc", RTMEMTYPE_RTMEMREALLOC, pvOld, cbNew, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfReallocZNP(void *pvOld, size_t cbOld, size_t cbNew, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvDst = rtR3MemRealloc("ReallocZ", RTMEMTYPE_RTMEMREALLOC, pvOld, cbNew, pszTag, ASMReturnAddress(), NULL, 0, NULL);
    if (pvDst && cbNew > cbOld)
        memset((uint8_t *)pvDst + cbOld, 0, cbNew - cbOld);
    return pvDst;
}


RTDECL(void)    RTMemEfFreeNP(void *pv) RT_NO_THROW_DEF
{
    if (pv)
        rtR3MemFree("Free", RTMEMTYPE_RTMEMFREE, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void)    RTMemEfFreeZNP(void *pv, size_t cb) RT_NO_THROW_DEF
{
    if (pv)
        rtR3MemFree("Free", RTMEMTYPE_RTMEMFREEZ, pv, cb, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *) RTMemEfDupNP(const void *pvSrc, size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvDst = RTMemEfAlloc(cb, pszTag, NULL, 0, NULL);
    if (pvDst)
        memcpy(pvDst, pvSrc, cb);
    return pvDst;
}


RTDECL(void *) RTMemEfDupExNP(const void *pvSrc, size_t cbSrc, size_t cbExtra, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvDst = RTMemEfAlloc(cbSrc + cbExtra, pszTag, NULL, 0, NULL);
    if (pvDst)
    {
        memcpy(pvDst, pvSrc, cbSrc);
        memset((uint8_t *)pvDst + cbSrc, 0, cbExtra);
    }
    return pvDst;
}

