/* $Id: alloc-ef-r0drv.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, electric fence for ring-0 drivers.
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
#define RTMEM_NO_WRAP_TO_EF_APIS
#include "internal/iprt.h"
#include <iprt/mem.h>

#include <iprt/alloc.h>
#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/memobj.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include "internal/mem.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(DOXYGEN_RUNNING)
# define RTR0MEM_EF_IN_FRONT
#endif

/** @def RTR0MEM_EF_SIZE
 * The size of the fence. This must be page aligned.
 */
#define RTR0MEM_EF_SIZE             PAGE_SIZE

/** @def RTR0MEM_EF_ALIGNMENT
 * The allocation alignment, power of two of course.
 *
 * Use this for working around misaligned sizes, usually stemming from
 * allocating a string or something after the main structure.  When you
 * encounter this, please fix the allocation to RTMemAllocVar or RTMemAllocZVar.
 */
#if 0
# define RTR0MEM_EF_ALIGNMENT       (ARCH_BITS / 8)
#else
# define RTR0MEM_EF_ALIGNMENT       1
#endif

/** @def RTR0MEM_EF_IN_FRONT
 * Define this to put the fence up in front of the block.
 * The default (when this isn't defined) is to up it up after the block.
 */
//# define RTR0MEM_EF_IN_FRONT

/** @def RTR0MEM_EF_FREE_DELAYED
 * This define will enable free() delay and protection of the freed data
 * while it's being delayed. The value of RTR0MEM_EF_FREE_DELAYED defines
 * the threshold of the delayed blocks.
 * Delayed blocks does not consume any physical memory, only virtual address space.
 */
#define RTR0MEM_EF_FREE_DELAYED     (20 * _1M)

/** @def RTR0MEM_EF_FREE_FILL
 * This define will enable memset(,RTR0MEM_EF_FREE_FILL,)'ing the user memory
 * in the block before freeing/decommitting it. This is useful in GDB since GDB
 * appears to be able to read the content of the page even after it's been
 * decommitted.
 */
#define RTR0MEM_EF_FREE_FILL        'f'

/** @def RTR0MEM_EF_FILLER
 * This define will enable memset(,RTR0MEM_EF_FILLER,)'ing the allocated
 * memory when the API doesn't require it to be zero'd.
 */
#define RTR0MEM_EF_FILLER           0xef

/** @def RTR0MEM_EF_NOMAN_FILLER
 * This define will enable memset(,RTR0MEM_EF_NOMAN_FILLER,)'ing the
 * unprotected but not allocated area of memory, the so called no man's land.
 */
#define RTR0MEM_EF_NOMAN_FILLER     0xaa

/** @def RTR0MEM_EF_FENCE_FILLER
 * This define will enable memset(,RTR0MEM_EF_FENCE_FILLER,)'ing the
 * fence itself, as debuggers can usually read them.
 */
#define RTR0MEM_EF_FENCE_FILLER     0xcc


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#elif !defined(RT_OS_FREEBSD)
# include <sys/mman.h>
#endif
#include <iprt/avl.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Allocation types.
 */
typedef enum RTMEMTYPE
{
    RTMEMTYPE_RTMEMALLOC,
    RTMEMTYPE_RTMEMALLOCZ,
    RTMEMTYPE_RTMEMREALLOC,
    RTMEMTYPE_RTMEMFREE,
    RTMEMTYPE_RTMEMFREEZ,

    RTMEMTYPE_NEW,
    RTMEMTYPE_NEW_ARRAY,
    RTMEMTYPE_DELETE,
    RTMEMTYPE_DELETE_ARRAY
} RTMEMTYPE;

/**
 * Node tracking a memory allocation.
 */
typedef struct RTR0MEMEFBLOCK
{
    /** Avl node code, key is the user block pointer. */
    AVLPVNODECORE   Core;
    /** Allocation type. */
    RTMEMTYPE       enmType;
    /** The memory object. */
    RTR0MEMOBJ      hMemObj;
    /** The unaligned size of the block. */
    size_t          cbUnaligned;
    /** The aligned size of the block. */
    size_t          cbAligned;
    /** The allocation tag (read-only string). */
    const char     *pszTag;
    /** The return address of the allocator function. */
    void           *pvCaller;
    /** Line number of the alloc call. */
    unsigned        iLine;
    /** File from within the allocation was made. */
    const char     *pszFile;
    /** Function from within the allocation was made. */
    const char     *pszFunction;
} RTR0MEMEFBLOCK, *PRTR0MEMEFBLOCK;



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Spinlock protecting the all the block's globals. */
static volatile uint32_t        g_BlocksLock;
/** Tree tracking the allocations. */
static AVLPVTREE                g_BlocksTree;

#ifdef RTR0MEM_EF_FREE_DELAYED
/** Tail of the delayed blocks. */
static volatile PRTR0MEMEFBLOCK g_pBlocksDelayHead;
/** Tail of the delayed blocks. */
static volatile PRTR0MEMEFBLOCK g_pBlocksDelayTail;
/** Number of bytes in the delay list (includes fences). */
static volatile size_t          g_cbBlocksDelay;
#endif /* RTR0MEM_EF_FREE_DELAYED */

/** Array of pointers free watches for. */
void   *gapvRTMemFreeWatch[4] = {NULL, NULL, NULL, NULL};
/** Enable logging of all freed memory. */
bool    gfRTMemFreeLog = false;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * @callback_method_impl{FNRTSTROUTPUT}
 */
static DECLCALLBACK(size_t) rtR0MemEfWrite(void *pvArg, const char *pachChars, size_t cbChars)
{
    RT_NOREF1(pvArg);
    if (cbChars)
    {
        RTLogWriteDebugger(pachChars, cbChars);
        RTLogWriteStdOut(pachChars, cbChars);
        RTLogWriteUser(pachChars, cbChars);
    }
    return cbChars;
}


/**
 * Complains about something.
 */
static void rtR0MemComplain(const char *pszOp, const char *pszFormat, ...)
{
    va_list args;
    RTStrFormat(rtR0MemEfWrite, NULL, NULL, NULL, "RTMem error: %s: ", pszOp);
    va_start(args, pszFormat);
    RTStrFormatV(rtR0MemEfWrite, NULL, NULL, NULL, pszFormat, args);
    va_end(args);
    RTAssertDoPanic();
}

/**
 * Log an event.
 */
DECLINLINE(void) rtR0MemLog(const char *pszOp, const char *pszFormat, ...)
{
#if 0
    va_list args;
    RTStrFormat(rtR0MemEfWrite, NULL, NULL, NULL, "RTMem info: %s: ", pszOp);
    va_start(args, pszFormat);
    RTStrFormatV(rtR0MemEfWrite, NULL, NULL, NULL, pszFormat, args);
    va_end(args);
#else
    NOREF(pszOp); NOREF(pszFormat);
#endif
}



/**
 * Acquires the lock.
 */
DECLINLINE(RTCCUINTREG) rtR0MemBlockLock(void)
{
    RTCCUINTREG uRet;
    unsigned c = 0;
    if (RTThreadPreemptIsEnabled(NIL_RTTHREAD))
    {
        for (;;)
        {
            uRet = ASMIntDisableFlags();
            if (ASMAtomicCmpXchgU32(&g_BlocksLock, 1, 0))
                break;
            ASMSetFlags(uRet);
            RTThreadSleepNoLog(((++c) >> 2) & 31);
        }
    }
    else
    {
        for (;;)
        {
            uRet = ASMIntDisableFlags();
            if (ASMAtomicCmpXchgU32(&g_BlocksLock, 1, 0))
                break;
            ASMSetFlags(uRet);
            ASMNopPause();
            if (++c & 3)
                ASMNopPause();
        }
    }
    return uRet;
}


/**
 * Releases the lock.
 */
DECLINLINE(void) rtR0MemBlockUnlock(RTCCUINTREG fSavedIntFlags)
{
    Assert(g_BlocksLock == 1);
    ASMAtomicXchgU32(&g_BlocksLock, 0);
    ASMSetFlags(fSavedIntFlags);
}


/**
 * Creates a block.
 */
DECLINLINE(PRTR0MEMEFBLOCK) rtR0MemBlockCreate(RTMEMTYPE enmType, size_t cbUnaligned, size_t cbAligned,
                                               const char *pszTag, void *pvCaller, RT_SRC_POS_DECL)
{
    PRTR0MEMEFBLOCK pBlock = (PRTR0MEMEFBLOCK)RTMemAlloc(sizeof(*pBlock));
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
DECLINLINE(void) rtR0MemBlockFree(PRTR0MEMEFBLOCK pBlock)
{
    RTMemFree(pBlock);
}


/**
 * Insert a block from the tree.
 */
DECLINLINE(void) rtR0MemBlockInsert(PRTR0MEMEFBLOCK pBlock, void *pv, RTR0MEMOBJ hMemObj)
{
    pBlock->Core.Key = pv;
    pBlock->hMemObj  = hMemObj;
    RTCCUINTREG fSavedIntFlags = rtR0MemBlockLock();
    bool fRc = RTAvlPVInsert(&g_BlocksTree, &pBlock->Core);
    rtR0MemBlockUnlock(fSavedIntFlags);
    AssertRelease(fRc);
}


/**
 * Remove a block from the tree and returns it to the caller.
 */
DECLINLINE(PRTR0MEMEFBLOCK) rtR0MemBlockRemove(void *pv)
{
    RTCCUINTREG fSavedIntFlags = rtR0MemBlockLock();
    PRTR0MEMEFBLOCK pBlock = (PRTR0MEMEFBLOCK)RTAvlPVRemove(&g_BlocksTree, pv);
    rtR0MemBlockUnlock(fSavedIntFlags);
    return pBlock;
}


/**
 * Gets a block.
 */
DECLINLINE(PRTR0MEMEFBLOCK) rtR0MemBlockGet(void *pv)
{
    RTCCUINTREG fSavedIntFlags = rtR0MemBlockLock();
    PRTR0MEMEFBLOCK pBlock = (PRTR0MEMEFBLOCK)RTAvlPVGet(&g_BlocksTree, pv);
    rtR0MemBlockUnlock(fSavedIntFlags);
    return pBlock;
}


/**
 * Dumps one allocation.
 */
static DECLCALLBACK(int) RTMemDumpOne(PAVLPVNODECORE pNode, void *pvUser)
{
    PRTR0MEMEFBLOCK pBlock = (PRTR0MEMEFBLOCK)pNode;
    RTStrFormat(rtR0MemEfWrite, NULL, NULL, NULL, "%p %08lx(+%02lx) %p\n",
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
RT_C_DECLS_BEGIN
void RTMemDump(void);
RT_C_DECLS_END

void RTMemDump(void)
{
    RTStrFormat(rtR0MemEfWrite, NULL, NULL, NULL, "address  size(alg)     caller\n");
    RTAvlPVDoWithAll(&g_BlocksTree, true, RTMemDumpOne, NULL);
}

#ifdef RTR0MEM_EF_FREE_DELAYED

/**
 * Insert a delayed block.
 */
DECLINLINE(void) rtR0MemBlockDelayInsert(PRTR0MEMEFBLOCK pBlock)
{
    size_t cbBlock = RT_ALIGN_Z(pBlock->cbAligned, PAGE_SIZE) + RTR0MEM_EF_SIZE;
    pBlock->Core.pRight = NULL;
    pBlock->Core.pLeft = NULL;
    RTCCUINTREG fSavedIntFlags = rtR0MemBlockLock();
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
    rtR0MemBlockUnlock(fSavedIntFlags);
}

/**
 * Removes a delayed block.
 */
DECLINLINE(PRTR0MEMEFBLOCK) rtR0MemBlockDelayRemove(void)
{
    PRTR0MEMEFBLOCK pBlock = NULL;
    RTCCUINTREG fSavedIntFlags = rtR0MemBlockLock();
    if (g_cbBlocksDelay > RTR0MEM_EF_FREE_DELAYED)
    {
        pBlock = g_pBlocksDelayTail;
        if (pBlock)
        {
            g_pBlocksDelayTail = (PRTR0MEMEFBLOCK)pBlock->Core.pLeft;
            if (pBlock->Core.pLeft)
                pBlock->Core.pLeft->pRight = NULL;
            else
                g_pBlocksDelayHead = NULL;
            g_cbBlocksDelay -= RT_ALIGN_Z(pBlock->cbAligned, PAGE_SIZE) + RTR0MEM_EF_SIZE;
        }
    }
    rtR0MemBlockUnlock(fSavedIntFlags);
    return pBlock;
}

#endif  /* RTR0MEM_EF_FREE_DELAYED */


static void rtR0MemFreeBlock(PRTR0MEMEFBLOCK pBlock, const char *pszOp)
{
    void  *pv      = pBlock->Core.Key;
# ifdef RTR0MEM_EF_IN_FRONT
    void  *pvBlock = (char *)pv - RTR0MEM_EF_SIZE;
# else
    void  *pvBlock = (void *)((uintptr_t)pv & ~(uintptr_t)PAGE_OFFSET_MASK);
# endif
    size_t cbBlock = RT_ALIGN_Z(pBlock->cbAligned, PAGE_SIZE) + RTR0MEM_EF_SIZE;

    int rc = RTR0MemObjProtect(pBlock->hMemObj, 0 /*offSub*/, RT_ALIGN_Z(cbBlock, PAGE_SIZE), RTMEM_PROT_READ | RTMEM_PROT_WRITE);
    if (RT_FAILURE(rc))
        rtR0MemComplain(pszOp, "RTR0MemObjProtect([%p], 0, %#x, RTMEM_PROT_READ | RTMEM_PROT_WRITE) -> %Rrc\n",
                        pvBlock, cbBlock, rc);

    rc = RTR0MemObjFree(pBlock->hMemObj, true /*fFreeMappings*/);
    if (RT_FAILURE(rc))
        rtR0MemComplain(pszOp, "RTR0MemObjFree([%p LB %#x]) -> %Rrc\n", pvBlock, cbBlock, rc);
    pBlock->hMemObj = NIL_RTR0MEMOBJ;

    rtR0MemBlockFree(pBlock);
}


/**
 * Initialize call, we shouldn't fail here.
 */
void rtR0MemEfInit(void)
{

}

/**
 * @callback_method_impl{AVLPVCALLBACK}
 */
static DECLCALLBACK(int) rtR0MemEfDestroyBlock(PAVLPVNODECORE pNode, void *pvUser)
{
    PRTR0MEMEFBLOCK pBlock = (PRTR0MEMEFBLOCK)pNode;

    /* Note! pszFile and pszFunction may be invalid at this point. */
    rtR0MemComplain("rtR0MemEfDestroyBlock", "Leaking %zu bytes at %p (iLine=%u pvCaller=%p)\n",
                    pBlock->cbAligned, pBlock->Core.Key, pBlock->iLine, pBlock->pvCaller);

    rtR0MemFreeBlock(pBlock, "rtR0MemEfDestroyBlock");

    NOREF(pvUser);
    return VINF_SUCCESS;
}


/**
 * Termination call.
 *
 * Will check and free memory.
 */
void rtR0MemEfTerm(void)
{
#ifdef RTR0MEM_EF_FREE_DELAYED
    /*
     * Release delayed frees.
     */
    RTCCUINTREG fSavedIntFlags = rtR0MemBlockLock();
    for (;;)
    {
        PRTR0MEMEFBLOCK pBlock = g_pBlocksDelayTail;
        if (pBlock)
        {
            g_pBlocksDelayTail = (PRTR0MEMEFBLOCK)pBlock->Core.pLeft;
            if (pBlock->Core.pLeft)
                pBlock->Core.pLeft->pRight = NULL;
            else
                g_pBlocksDelayHead = NULL;
            rtR0MemBlockUnlock(fSavedIntFlags);

            rtR0MemFreeBlock(pBlock, "rtR0MemEfTerm");

            rtR0MemBlockLock();
        }
        else
            break;
    }
    g_cbBlocksDelay = 0;
    rtR0MemBlockUnlock(fSavedIntFlags);
#endif

    /*
     * Complain about leaks. Then release them.
     */
    RTAvlPVDestroy(&g_BlocksTree, rtR0MemEfDestroyBlock, NULL);
}


/**
 * Internal allocator.
 */
static void * rtR0MemAlloc(const char *pszOp, RTMEMTYPE enmType, size_t cbUnaligned, size_t cbAligned,
                           const char *pszTag, void *pvCaller, RT_SRC_POS_DECL)
{
    /*
     * Sanity.
     */
    if (    RT_ALIGN_Z(RTR0MEM_EF_SIZE, PAGE_SIZE) != RTR0MEM_EF_SIZE
        &&  RTR0MEM_EF_SIZE <= 0)
    {
        rtR0MemComplain(pszOp, "Invalid E-fence size! %#x\n", RTR0MEM_EF_SIZE);
        return NULL;
    }
    if (!cbUnaligned)
    {
#if 1
        rtR0MemComplain(pszOp, "Request of ZERO bytes allocation!\n");
        return NULL;
#else
        cbAligned = cbUnaligned = 1;
#endif
    }

#ifndef RTR0MEM_EF_IN_FRONT
    /* Alignment decreases fence accuracy, but this is at least partially
     * counteracted by filling and checking the alignment padding. When the
     * fence is in front then then no extra alignment is needed. */
    cbAligned = RT_ALIGN_Z(cbAligned, RTR0MEM_EF_ALIGNMENT);
#endif

    /*
     * Allocate the trace block.
     */
    PRTR0MEMEFBLOCK pBlock = rtR0MemBlockCreate(enmType, cbUnaligned, cbAligned, pszTag, pvCaller, RT_SRC_POS_ARGS);
    if (!pBlock)
    {
        rtR0MemComplain(pszOp, "Failed to allocate trace block!\n");
        return NULL;
    }

    /*
     * Allocate a block with page alignment space + the size of the E-fence.
     */
    void       *pvBlock = NULL;
    RTR0MEMOBJ  hMemObj;
    size_t      cbBlock = RT_ALIGN_Z(cbAligned, PAGE_SIZE) + RTR0MEM_EF_SIZE;
    int rc = RTR0MemObjAllocPage(&hMemObj, cbBlock, false /*fExecutable*/);
    if (RT_SUCCESS(rc))
        pvBlock = RTR0MemObjAddress(hMemObj);
    if (pvBlock)
    {
        /*
         * Calc the start of the fence and the user block
         * and then change the page protection of the fence.
         */
#ifdef RTR0MEM_EF_IN_FRONT
        void *pvEFence = pvBlock;
        void *pv       = (char *)pvEFence + RTR0MEM_EF_SIZE;
# ifdef RTR0MEM_EF_NOMAN_FILLER
        memset((char *)pv + cbUnaligned, RTR0MEM_EF_NOMAN_FILLER, cbBlock - RTR0MEM_EF_SIZE - cbUnaligned);
# endif
#else
        void *pvEFence = (char *)pvBlock + (cbBlock - RTR0MEM_EF_SIZE);
        void *pv       = (char *)pvEFence - cbAligned;
# ifdef RTR0MEM_EF_NOMAN_FILLER
        memset(pvBlock, RTR0MEM_EF_NOMAN_FILLER, cbBlock - RTR0MEM_EF_SIZE - cbAligned);
        memset((char *)pv + cbUnaligned, RTR0MEM_EF_NOMAN_FILLER, cbAligned - cbUnaligned);
# endif
#endif

#ifdef RTR0MEM_EF_FENCE_FILLER
        memset(pvEFence, RTR0MEM_EF_FENCE_FILLER, RTR0MEM_EF_SIZE);
#endif
        rc = RTR0MemObjProtect(hMemObj, (uint8_t *)pvEFence - (uint8_t *)pvBlock, RTR0MEM_EF_SIZE, RTMEM_PROT_NONE);
        if (!rc)
        {
            rtR0MemBlockInsert(pBlock, pv, hMemObj);
            if (enmType == RTMEMTYPE_RTMEMALLOCZ)
                memset(pv, 0, cbUnaligned);
#ifdef RTR0MEM_EF_FILLER
            else
                memset(pv, RTR0MEM_EF_FILLER, cbUnaligned);
#endif

            rtR0MemLog(pszOp, "returns %p (pvBlock=%p cbBlock=%#x pvEFence=%p cbUnaligned=%#x)\n", pv, pvBlock, cbBlock, pvEFence, cbUnaligned);
            return pv;
        }
        rtR0MemComplain(pszOp, "RTMemProtect failed, pvEFence=%p size %d, rc=%d\n", pvEFence, RTR0MEM_EF_SIZE, rc);
        RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
    }
    else
    {
        rtR0MemComplain(pszOp, "Failed to allocated %zu (%zu) bytes (rc=%Rrc).\n", cbBlock, cbUnaligned, rc);
        if (RT_SUCCESS(rc))
            RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
    }

    rtR0MemBlockFree(pBlock);
    return NULL;
}


/**
 * Internal free.
 */
static void rtR0MemFree(const char *pszOp, RTMEMTYPE enmType, void *pv, size_t cbUser, void *pvCaller, RT_SRC_POS_DECL)
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

    /*
     * Find the block.
     */
    PRTR0MEMEFBLOCK pBlock = rtR0MemBlockRemove(pv);
    if (pBlock)
    {
        if (gfRTMemFreeLog)
            RTLogPrintf("RTMem %s: pv=%p pvCaller=%p cbUnaligned=%#x\n", pszOp, pv, pvCaller, pBlock->cbUnaligned);

#ifdef RTR0MEM_EF_NOMAN_FILLER
        /*
         * Check whether the no man's land is untouched.
         */
# ifdef RTR0MEM_EF_IN_FRONT
        void *pvWrong = ASMMemFirstMismatchingU8((char *)pv + pBlock->cbUnaligned,
                                                 RT_ALIGN_Z(pBlock->cbAligned, PAGE_SIZE) - pBlock->cbUnaligned,
                                                 RTR0MEM_EF_NOMAN_FILLER);
# else
        /* Alignment must match allocation alignment in rtMemAlloc(). */
        void *pvWrong = ASMMemFirstMismatchingU8((char *)pv + pBlock->cbUnaligned,
                                                 pBlock->cbAligned - pBlock->cbUnaligned,
                                                 RTR0MEM_EF_NOMAN_FILLER);
        if (pvWrong)
            RTAssertDoPanic();
        pvWrong = ASMMemFirstMismatchingU8((void *)((uintptr_t)pv & ~(uintptr_t)PAGE_OFFSET_MASK),
                                           RT_ALIGN_Z(pBlock->cbAligned, PAGE_SIZE) - pBlock->cbAligned,
                                           RTR0MEM_EF_NOMAN_FILLER);
# endif
        if (pvWrong)
            RTAssertDoPanic();
#endif

        /*
         * Fill the user part of the block.
         */
        AssertMsg(enmType != RTMEMTYPE_RTMEMFREEZ || cbUser == pBlock->cbUnaligned,
                  ("cbUser=%#zx cbUnaligned=%#zx\n", cbUser, pBlock->cbUnaligned));
        RT_NOREF(cbUser);
        if (enmType == RTMEMTYPE_RTMEMFREEZ)
            RT_BZERO(pv, pBlock->cbUnaligned);
#ifdef RTR0MEM_EF_FREE_FILL
        else
            memset(pv, RTR0MEM_EF_FREE_FILL, pBlock->cbUnaligned);
#endif

#if defined(RTR0MEM_EF_FREE_DELAYED) && RTR0MEM_EF_FREE_DELAYED > 0
        /*
         * We're doing delayed freeing.
         * That means we'll expand the E-fence to cover the entire block.
         */
        int rc = RTR0MemObjProtect(pBlock->hMemObj,
# ifdef RTR0MEM_EF_IN_FRONT
                                   RTR0MEM_EF_SIZE,
# else
                                   0 /*offSub*/,
# endif
                                   RT_ALIGN_Z(pBlock->cbAligned, PAGE_SIZE),
                                   RTMEM_PROT_NONE);
        if (RT_SUCCESS(rc))
        {
            /*
             * Insert it into the free list and process pending frees.
             */
            rtR0MemBlockDelayInsert(pBlock);
            while ((pBlock = rtR0MemBlockDelayRemove()) != NULL)
                rtR0MemFreeBlock(pBlock, pszOp);
        }
        else
            rtR0MemComplain(pszOp, "Failed to expand the efence of pv=%p cb=%d, rc=%d.\n", pv, pBlock, rc);

#else  /* !RTR0MEM_EF_FREE_DELAYED */
        rtR0MemFreeBlock(pBlock, pszOp);
#endif /* !RTR0MEM_EF_FREE_DELAYED */
    }
    else
        rtR0MemComplain(pszOp, "pv=%p not found! Incorrect free!\n", pv);
}


/**
 * Internal realloc.
 */
static void *rtR0MemRealloc(const char *pszOp, RTMEMTYPE enmType, void *pvOld, size_t cbNew,
                            const char *pszTag, void *pvCaller, RT_SRC_POS_DECL)
{
    /*
     * Allocate new and copy.
     */
    if (!pvOld)
        return rtR0MemAlloc(pszOp, enmType, cbNew, cbNew, pszTag, pvCaller, RT_SRC_POS_ARGS);
    if (!cbNew)
    {
        rtR0MemFree(pszOp, RTMEMTYPE_RTMEMREALLOC, pvOld, 0, pvCaller, RT_SRC_POS_ARGS);
        return NULL;
    }

    /*
     * Get the block, allocate the new, copy the data, free the old one.
     */
    PRTR0MEMEFBLOCK pBlock = rtR0MemBlockGet(pvOld);
    if (pBlock)
    {
        void *pvRet = rtR0MemAlloc(pszOp, enmType, cbNew, cbNew, pszTag, pvCaller, RT_SRC_POS_ARGS);
        if (pvRet)
        {
            memcpy(pvRet, pvOld, RT_MIN(cbNew, pBlock->cbUnaligned));
            rtR0MemFree(pszOp, RTMEMTYPE_RTMEMREALLOC, pvOld, 0, pvCaller, RT_SRC_POS_ARGS);
        }
        return pvRet;
    }
    rtR0MemComplain(pszOp, "pvOld=%p was not found!\n", pvOld);
    return NULL;
}




RTDECL(void *)  RTMemEfTmpAlloc(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR0MemAlloc("TmpAlloc", RTMEMTYPE_RTMEMALLOC, cb, cb, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfTmpAllocZ(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR0MemAlloc("TmpAlloc", RTMEMTYPE_RTMEMALLOCZ, cb, cb, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void)    RTMemEfTmpFree(void *pv, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    if (pv)
        rtR0MemFree("Free", RTMEMTYPE_RTMEMFREE, pv, 0, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void)    RTMemEfTmpFreeZ(void *pv, size_t cb, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    if (pv)
        rtR0MemFree("FreeZ", RTMEMTYPE_RTMEMFREEZ, pv, cb, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfAlloc(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR0MemAlloc("Alloc", RTMEMTYPE_RTMEMALLOC, cb, cb, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfAllocZ(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR0MemAlloc("AllocZ", RTMEMTYPE_RTMEMALLOCZ, cb, cb, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfAllocVar(size_t cbUnaligned, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return rtR0MemAlloc("Alloc", RTMEMTYPE_RTMEMALLOC, cbUnaligned, cbAligned, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfAllocZVar(size_t cbUnaligned, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return rtR0MemAlloc("AllocZ", RTMEMTYPE_RTMEMALLOCZ, cbUnaligned, cbAligned, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void *)  RTMemEfRealloc(void *pvOld, size_t cbNew, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    return rtR0MemRealloc("Realloc", RTMEMTYPE_RTMEMREALLOC, pvOld, cbNew, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
}

RTDECL(void *)  RTMemEfReallocZ(void *pvOld, size_t cbOld, size_t cbNew, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    void *pvDst = rtR0MemRealloc("Realloc", RTMEMTYPE_RTMEMREALLOC, pvOld, cbNew, pszTag, ASMReturnAddress(), RT_SRC_POS_ARGS);
    if (pvDst && cbNew > cbOld)
        memset((uint8_t *)pvDst + cbOld, 0, cbNew - cbOld);
    return pvDst;
}


RTDECL(void)    RTMemEfFree(void *pv, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    if (pv)
        rtR0MemFree("Free", RTMEMTYPE_RTMEMFREE, pv, 0, ASMReturnAddress(), RT_SRC_POS_ARGS);
}


RTDECL(void)    RTMemEfFreeZ(void *pv, size_t cb, RT_SRC_POS_DECL) RT_NO_THROW_DEF
{
    if (pv)
        rtR0MemFree("Free", RTMEMTYPE_RTMEMFREEZ, pv, cb, ASMReturnAddress(), RT_SRC_POS_ARGS);
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
    return rtR0MemAlloc("TmpAlloc", RTMEMTYPE_RTMEMALLOC, cb, cb, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfTmpAllocZNP(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtR0MemAlloc("TmpAllocZ", RTMEMTYPE_RTMEMALLOCZ, cb, cb, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void)    RTMemEfTmpFreeNP(void *pv) RT_NO_THROW_DEF
{
    if (pv)
        rtR0MemFree("Free", RTMEMTYPE_RTMEMFREE, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void)    RTMemEfTmpFreeZNP(void *pv, size_t cb) RT_NO_THROW_DEF
{
    if (pv)
        rtR0MemFree("FreeZ", RTMEMTYPE_RTMEMFREEZ, pv, cb, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfAllocNP(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtR0MemAlloc("Alloc", RTMEMTYPE_RTMEMALLOC, cb, cb, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfAllocZNP(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtR0MemAlloc("AllocZ", RTMEMTYPE_RTMEMALLOCZ, cb, cb, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfAllocVarNP(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return rtR0MemAlloc("Alloc", RTMEMTYPE_RTMEMALLOC, cbUnaligned, cbAligned, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfAllocZVarNP(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return rtR0MemAlloc("AllocZ", RTMEMTYPE_RTMEMALLOCZ, cbUnaligned, cbAligned, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfReallocNP(void *pvOld, size_t cbNew, const char *pszTag) RT_NO_THROW_DEF
{
    return rtR0MemRealloc("Realloc", RTMEMTYPE_RTMEMREALLOC, pvOld, cbNew, pszTag, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void *)  RTMemEfReallocZNP(void *pvOld, size_t cbOld, size_t cbNew, const char *pszTag) RT_NO_THROW_DEF
{
    void *pvDst = rtR0MemRealloc("ReallocZ", RTMEMTYPE_RTMEMREALLOC, pvOld, cbNew, pszTag, ASMReturnAddress(), NULL, 0, NULL);
    if (pvDst && cbNew > cbOld)
        memset((uint8_t *)pvDst + cbOld, 0, cbNew - cbOld);
    return pvDst;
}


RTDECL(void)    RTMemEfFreeNP(void *pv) RT_NO_THROW_DEF
{
    if (pv)
        rtR0MemFree("Free", RTMEMTYPE_RTMEMFREE, pv, 0, ASMReturnAddress(), NULL, 0, NULL);
}


RTDECL(void)    RTMemEfFreeZNP(void *pv, size_t cb) RT_NO_THROW_DEF
{
    if (pv)
        rtR0MemFree("FreeZ", RTMEMTYPE_RTMEMFREEZ, pv, cb, ASMReturnAddress(), NULL, 0, NULL);
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

