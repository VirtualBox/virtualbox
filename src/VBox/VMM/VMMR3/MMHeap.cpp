/* $Id: MMHeap.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MM - Memory Manager - Heap.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MM_HEAP
#include <VBox/vmm/mm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/pgm.h>
#include "MMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <iprt/errcore.h>
#include <VBox/param.h>
#include <VBox/log.h>

#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void *mmR3HeapAlloc(PMMHEAP pHeap, MMTAG enmTag, size_t cbSize, bool fZero);



/**
 * Allocate and initialize a heap structure and it's associated substructures.
 *
 * @returns VBox status code.
 * @param   pUVM    Pointer to the user mode VM structure.
 * @param   ppHeap  Where to store the heap pointer.
 */
int mmR3HeapCreateU(PUVM pUVM, PMMHEAP *ppHeap)
{
    PMMHEAP pHeap = (PMMHEAP)RTMemAllocZ(sizeof(MMHEAP) + sizeof(MMHEAPSTAT));
    if (pHeap)
    {
        int rc = RTCritSectInit(&pHeap->Lock);
        if (RT_SUCCESS(rc))
        {
            /*
             * Initialize the global stat record.
             */
            pHeap->pUVM = pUVM;
            pHeap->Stat.pHeap = pHeap;
#ifdef MMR3HEAP_WITH_STATISTICS
            PMMHEAPSTAT pStat = &pHeap->Stat;
            STAMR3RegisterU(pUVM, &pStat->cAllocations,   STAMTYPE_U64, STAMVISIBILITY_ALWAYS, "/MM/R3Heap/cAllocations",     STAMUNIT_CALLS, "Number or MMR3HeapAlloc() calls.");
            STAMR3RegisterU(pUVM, &pStat->cReallocations, STAMTYPE_U64, STAMVISIBILITY_ALWAYS, "/MM/R3Heap/cReallocations",   STAMUNIT_CALLS, "Number of MMR3HeapRealloc() calls.");
            STAMR3RegisterU(pUVM, &pStat->cFrees,         STAMTYPE_U64, STAMVISIBILITY_ALWAYS, "/MM/R3Heap/cFrees",           STAMUNIT_CALLS, "Number of MMR3HeapFree() calls.");
            STAMR3RegisterU(pUVM, &pStat->cFailures,      STAMTYPE_U64, STAMVISIBILITY_ALWAYS, "/MM/R3Heap/cFailures",        STAMUNIT_COUNT, "Number of failures.");
            STAMR3RegisterU(pUVM, &pStat->cbCurAllocated, sizeof(pStat->cbCurAllocated) == sizeof(uint32_t) ? STAMTYPE_U32 : STAMTYPE_U64,
                                                                        STAMVISIBILITY_ALWAYS, "/MM/R3Heap/cbCurAllocated",   STAMUNIT_BYTES, "Number of bytes currently allocated.");
            STAMR3RegisterU(pUVM, &pStat->cbAllocated,    STAMTYPE_U64, STAMVISIBILITY_ALWAYS, "/MM/R3Heap/cbAllocated",      STAMUNIT_BYTES, "Total number of bytes allocated.");
            STAMR3RegisterU(pUVM, &pStat->cbFreed,        STAMTYPE_U64, STAMVISIBILITY_ALWAYS, "/MM/R3Heap/cbFreed",          STAMUNIT_BYTES, "Total number of bytes freed.");
#endif
            *ppHeap = pHeap;
            return VINF_SUCCESS;
        }
        AssertRC(rc);
        RTMemFree(pHeap);
    }
    AssertMsgFailed(("failed to allocate heap structure\n"));
    return VERR_NO_MEMORY;
}


/**
 * MM heap statistics tree destroy callback.
 */
static DECLCALLBACK(int) mmR3HeapStatTreeDestroy(PAVLULNODECORE pCore, void *pvParam)
{
    RT_NOREF(pvParam);

    /* Don't bother deregistering the stat samples as they get destroyed by STAM. */
    RTMemFree(pCore);
    return VINF_SUCCESS;
}


/**
 * Destroy a heap.
 *
 * @param   pHeap   Heap handle.
 */
void mmR3HeapDestroy(PMMHEAP pHeap)
{
    /*
     * Start by deleting the lock, that'll trap anyone
     * attempting to use the heap.
     */
    RTCritSectDelete(&pHeap->Lock);

    /*
     * Walk the node list and free all the memory.
     */
    PMMHEAPHDR  pHdr = pHeap->pHead;
    while (pHdr)
    {
        void *pv = pHdr;
        pHdr = pHdr->pNext;
        RTMemFree(pv);
    }

    /*
     * Free the stat nodes.
     */
    RTAvlULDestroy(&pHeap->pStatTree, mmR3HeapStatTreeDestroy, NULL);
    RTMemFree(pHeap);
}


/**
 * Allocate memory associating it with the VM for collective cleanup.
 *
 * The memory will be allocated from the default heap but a header
 * is added in which we keep track of which VM it belongs to and chain
 * all the allocations together so they can be freed in one go.
 *
 * This interface is typically used for memory block which will not be
 * freed during the life of the VM.
 *
 * @returns Pointer to allocated memory.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   cbSize      Size of the block.
 */
VMMR3DECL(void *) MMR3HeapAllocU(PUVM pUVM, MMTAG enmTag, size_t cbSize)
{
    Assert(pUVM->mm.s.pHeap);
    return mmR3HeapAlloc(pUVM->mm.s.pHeap, enmTag, cbSize, false);
}


/**
 * Allocate memory associating it with the VM for collective cleanup.
 *
 * The memory will be allocated from the default heap but a header
 * is added in which we keep track of which VM it belongs to and chain
 * all the allocations together so they can be freed in one go.
 *
 * This interface is typically used for memory block which will not be
 * freed during the life of the VM.
 *
 * @returns Pointer to allocated memory.
 * @param   pVM         The cross context VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   cbSize      Size of the block.
 */
VMMR3DECL(void *) MMR3HeapAlloc(PVM pVM, MMTAG enmTag, size_t cbSize)
{
    return mmR3HeapAlloc(pVM->pUVM->mm.s.pHeap, enmTag, cbSize, false);
}


/**
 * Same as MMR3HeapAllocU().
 *
 * @returns Pointer to allocated memory.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   cbSize      Size of the block.
 * @param   ppv         Where to store the pointer to the allocated memory on success.
 */
VMMR3DECL(int) MMR3HeapAllocExU(PUVM pUVM, MMTAG enmTag, size_t cbSize, void **ppv)
{
    Assert(pUVM->mm.s.pHeap);
    void *pv = mmR3HeapAlloc(pUVM->mm.s.pHeap, enmTag, cbSize, false);
    if (pv)
    {
        *ppv = pv;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/**
 * Same as MMR3HeapAlloc().
 *
 * @returns Pointer to allocated memory.
 * @param   pVM         The cross context VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   cbSize      Size of the block.
 * @param   ppv         Where to store the pointer to the allocated memory on success.
 */
VMMR3DECL(int) MMR3HeapAllocEx(PVM pVM, MMTAG enmTag, size_t cbSize, void **ppv)
{
    void *pv = mmR3HeapAlloc(pVM->pUVM->mm.s.pHeap, enmTag, cbSize, false);
    if (pv)
    {
        *ppv = pv;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/**
 * Same as MMR3HeapAlloc() only the memory is zeroed.
 *
 * @returns Pointer to allocated memory.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   cbSize      Size of the block.
 */
VMMR3DECL(void *) MMR3HeapAllocZU(PUVM pUVM, MMTAG enmTag, size_t cbSize)
{
    return mmR3HeapAlloc(pUVM->mm.s.pHeap, enmTag, cbSize, true);
}


/**
 * Same as MMR3HeapAlloc() only the memory is zeroed.
 *
 * @returns Pointer to allocated memory.
 * @param   pVM         The cross context VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   cbSize      Size of the block.
 */
VMMR3DECL(void *) MMR3HeapAllocZ(PVM pVM, MMTAG enmTag, size_t cbSize)
{
    return mmR3HeapAlloc(pVM->pUVM->mm.s.pHeap, enmTag, cbSize, true);
}


/**
 * Same as MMR3HeapAllocZ().
 *
 * @returns Pointer to allocated memory.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   cbSize      Size of the block.
 * @param   ppv         Where to store the pointer to the allocated memory on success.
 */
VMMR3DECL(int) MMR3HeapAllocZExU(PUVM pUVM, MMTAG enmTag, size_t cbSize, void **ppv)
{
    Assert(pUVM->mm.s.pHeap);
    void *pv = mmR3HeapAlloc(pUVM->mm.s.pHeap, enmTag, cbSize, true);
    if (pv)
    {
        *ppv = pv;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/**
 * Same as MMR3HeapAllocZ().
 *
 * @returns Pointer to allocated memory.
 * @param   pVM         The cross context VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   cbSize      Size of the block.
 * @param   ppv         Where to store the pointer to the allocated memory on success.
 */
VMMR3DECL(int) MMR3HeapAllocZEx(PVM pVM, MMTAG enmTag, size_t cbSize, void **ppv)
{
    void *pv = mmR3HeapAlloc(pVM->pUVM->mm.s.pHeap, enmTag, cbSize, true);
    if (pv)
    {
        *ppv = pv;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/**
 * Links @a pHdr into the heap block list (tail).
 *
 * @param   pHeap       Heap handle.
 * @param   pHdr        The block to link.
 *
 * @note    Caller has locked the heap!
 */
DECLINLINE(void) mmR3HeapLink(PMMHEAP pHeap, PMMHEAPHDR pHdr)
{
    /* Tail insertion: */
    pHdr->pNext = NULL;
    PMMHEAPHDR pTail = pHeap->pTail;
    pHdr->pPrev = pTail;
    if (pTail)
    {
        Assert(!pTail->pNext);
        pTail->pNext = pHdr;
    }
    else
    {
        Assert(!pHeap->pHead);
        pHeap->pHead = pHdr;
    }
    pHeap->pTail = pHdr;
}


/**
 * Unlinks @a pHdr from the heal block list.
 *
 * @param   pHeap       Heap handle.
 * @param   pHdr        The block to unlink.
 *
 * @note    Caller has locked the heap!
 */
DECLINLINE(void) mmR3HeapUnlink(PMMHEAP pHeap, PMMHEAPHDR pHdr)
{
    PMMHEAPHDR const pPrev = pHdr->pPrev;
    PMMHEAPHDR const pNext = pHdr->pNext;
    if (pPrev)
        pPrev->pNext = pNext;
    else
        pHeap->pHead = pNext;

    if (pNext)
        pNext->pPrev = pPrev;
    else
        pHeap->pTail = pHdr->pPrev;
}


/**
 * Allocate memory from the heap.
 *
 * @returns Pointer to allocated memory.
 * @param   pHeap       Heap handle.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   cbSize      Size of the block.
 * @param   fZero       Whether or not to zero the memory block.
 */
void *mmR3HeapAlloc(PMMHEAP pHeap, MMTAG enmTag, size_t cbSize, bool fZero)
{
#ifdef MMR3HEAP_WITH_STATISTICS
    RTCritSectEnter(&pHeap->Lock);

    /*
     * Find/alloc statistics nodes.
     */
    pHeap->Stat.cAllocations++;
    PMMHEAPSTAT pStat = (PMMHEAPSTAT)RTAvlULGet(&pHeap->pStatTree, (AVLULKEY)enmTag);
    if (pStat)
    {
        pStat->cAllocations++;

        RTCritSectLeave(&pHeap->Lock);
    }
    else
    {
        pStat = (PMMHEAPSTAT)RTMemAllocZ(sizeof(MMHEAPSTAT));
        if (!pStat)
        {
            pHeap->Stat.cFailures++;
            AssertMsgFailed(("Failed to allocate heap stat record.\n"));
            RTCritSectLeave(&pHeap->Lock);
            return NULL;
        }
        pStat->Core.Key = (AVLULKEY)enmTag;
        pStat->pHeap    = pHeap;
        RTAvlULInsert(&pHeap->pStatTree, &pStat->Core);

        pStat->cAllocations++;
        RTCritSectLeave(&pHeap->Lock);

        /* register the statistics */
        PUVM pUVM = pHeap->pUVM;
        const char *pszTag = mmGetTagName(enmTag);
        STAMR3RegisterFU(pUVM, &pStat->cbCurAllocated, STAMTYPE_U32, STAMVISIBILITY_ALWAYS,  STAMUNIT_BYTES, "Number of bytes currently allocated.",    "/MM/R3Heap/%s", pszTag);
        STAMR3RegisterFU(pUVM, &pStat->cAllocations,   STAMTYPE_U64, STAMVISIBILITY_ALWAYS,  STAMUNIT_CALLS, "Number or MMR3HeapAlloc() calls.",        "/MM/R3Heap/%s/cAllocations", pszTag);
        STAMR3RegisterFU(pUVM, &pStat->cReallocations, STAMTYPE_U64, STAMVISIBILITY_ALWAYS,  STAMUNIT_CALLS, "Number of MMR3HeapRealloc() calls.",      "/MM/R3Heap/%s/cReallocations", pszTag);
        STAMR3RegisterFU(pUVM, &pStat->cFrees,         STAMTYPE_U64, STAMVISIBILITY_ALWAYS,  STAMUNIT_CALLS, "Number of MMR3HeapFree() calls.",         "/MM/R3Heap/%s/cFrees", pszTag);
        STAMR3RegisterFU(pUVM, &pStat->cFailures,      STAMTYPE_U64, STAMVISIBILITY_ALWAYS,  STAMUNIT_COUNT, "Number of failures.",                     "/MM/R3Heap/%s/cFailures", pszTag);
        STAMR3RegisterFU(pUVM, &pStat->cbAllocated,    STAMTYPE_U64, STAMVISIBILITY_ALWAYS,  STAMUNIT_BYTES, "Total number of bytes allocated.",        "/MM/R3Heap/%s/cbAllocated", pszTag);
        STAMR3RegisterFU(pUVM, &pStat->cbFreed,        STAMTYPE_U64, STAMVISIBILITY_ALWAYS,  STAMUNIT_BYTES, "Total number of bytes freed.",            "/MM/R3Heap/%s/cbFreed", pszTag);
    }
#else
    RT_NOREF_PV(enmTag);
#endif

    /*
     * Validate input.
     */
    if (cbSize == 0)
    {
#ifdef MMR3HEAP_WITH_STATISTICS
        RTCritSectEnter(&pHeap->Lock);
        pStat->cFailures++;
        pHeap->Stat.cFailures++;
        RTCritSectLeave(&pHeap->Lock);
#endif
        AssertFailed();
        return NULL;
    }

    /*
     * Allocate heap block.
     */
    cbSize = RT_ALIGN_Z(cbSize, MMR3HEAP_SIZE_ALIGNMENT) + sizeof(MMHEAPHDR);
    PMMHEAPHDR const pHdr = (PMMHEAPHDR)(fZero ? RTMemAllocZ(cbSize) : RTMemAlloc(cbSize));
    if (pHdr)
    { /* likely */ }
    else
    {
        AssertMsgFailed(("Failed to allocate heap block %d, enmTag=%x(%.4s).\n", cbSize, enmTag, &enmTag));
#ifdef MMR3HEAP_WITH_STATISTICS
        RTCritSectEnter(&pHeap->Lock);
        pStat->cFailures++;
        pHeap->Stat.cFailures++;
        RTCritSectLeave(&pHeap->Lock);
#endif
        return NULL;
    }
    Assert(!((uintptr_t)pHdr & (RTMEM_ALIGNMENT - 1)));

    /*
     * Init and link in the header.
     */
#ifdef MMR3HEAP_WITH_STATISTICS
    pHdr->pStat  = pStat;
#else
    pHdr->pStat  = &pHeap->Stat;
#endif
    pHdr->cbSize = cbSize;

    RTCritSectEnter(&pHeap->Lock);

    mmR3HeapLink(pHeap, pHdr);

    /*
     * Update statistics
     */
#ifdef MMR3HEAP_WITH_STATISTICS
    pStat->cbAllocated          += cbSize;
    pStat->cbCurAllocated       += cbSize;
    pHeap->Stat.cbAllocated     += cbSize;
    pHeap->Stat.cbCurAllocated  += cbSize;
#endif

    RTCritSectLeave(&pHeap->Lock);

    return pHdr + 1;
}


/**
 * Reallocate memory allocated with MMR3HeapAlloc(), MMR3HeapAllocZ() or
 * MMR3HeapRealloc().
 *
 * Any additional memory is zeroed (only reliable if the initial allocation was
 * also of the zeroing kind).
 *
 * @returns Pointer to reallocated memory.
 * @param   pv          Pointer to the memory block to reallocate.
 *                      Must not be NULL!
 * @param   cbNewSize   New block size.
 */
VMMR3DECL(void *) MMR3HeapRealloc(void *pv, size_t cbNewSize)
{
    AssertMsg(pv, ("Invalid pointer pv=%p\n", pv));
    if (!pv)
        return NULL;

    /*
     * If newsize is zero then this is a free.
     */
    if (!cbNewSize)
    {
        MMR3HeapFree(pv);
        return NULL;
    }

    /*
     * Validate header.
     */
    PMMHEAPHDR const pHdr      = (PMMHEAPHDR)pv - 1;
    size_t const     cbOldSize = pHdr->cbSize;
    AssertMsgReturn(   !(cbOldSize & (MMR3HEAP_SIZE_ALIGNMENT - 1))
                    && !((uintptr_t)pHdr & (RTMEM_ALIGNMENT - 1)),
                    ("Invalid heap header! pv=%p, size=%#x\n", pv, cbOldSize),
                    NULL);
    Assert(pHdr->pStat != NULL);
    Assert(!((uintptr_t)pHdr->pNext & (RTMEM_ALIGNMENT - 1)));
    Assert(!((uintptr_t)pHdr->pPrev & (RTMEM_ALIGNMENT - 1)));

    PMMHEAP pHeap = pHdr->pStat->pHeap;

    /*
     * Unlink the header before we reallocate the block.
     */
    RTCritSectEnter(&pHeap->Lock);
#ifdef MMR3HEAP_WITH_STATISTICS
    pHdr->pStat->cReallocations++;
    pHeap->Stat.cReallocations++;
#endif
    mmR3HeapUnlink(pHeap, pHdr);
    RTCritSectLeave(&pHeap->Lock);

    /*
     * Reallocate the block.  Clear added space.
     */
    cbNewSize = RT_ALIGN_Z(cbNewSize, MMR3HEAP_SIZE_ALIGNMENT) + sizeof(MMHEAPHDR);
    PMMHEAPHDR pHdrNew = (PMMHEAPHDR)RTMemReallocZ(pHdr, cbOldSize, cbNewSize);
    if (pHdrNew)
        pHdrNew->cbSize = cbNewSize;
    else
    {
        RTCritSectEnter(&pHeap->Lock);
        mmR3HeapLink(pHeap, pHdr);
#ifdef MMR3HEAP_WITH_STATISTICS
        pHdr->pStat->cFailures++;
        pHeap->Stat.cFailures++;
#endif
        RTCritSectLeave(&pHeap->Lock);
        return NULL;
    }

    RTCritSectEnter(&pHeap->Lock);

    /*
     * Relink the header.
     */
    mmR3HeapLink(pHeap, pHdrNew);

    /*
     * Update statistics.
     */
#ifdef MMR3HEAP_WITH_STATISTICS
    pHdrNew->pStat->cbAllocated += cbNewSize - pHdrNew->cbSize;
    pHeap->Stat.cbAllocated += cbNewSize - pHdrNew->cbSize;
#endif

    RTCritSectLeave(&pHeap->Lock);

    return pHdrNew + 1;
}


/**
 * Duplicates the specified string.
 *
 * @returns Pointer to the duplicate.
 * @returns NULL on failure or when input NULL.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   psz         The string to duplicate. NULL is allowed.
 */
VMMR3DECL(char *) MMR3HeapStrDupU(PUVM pUVM, MMTAG enmTag, const char *psz)
{
    if (!psz)
        return NULL;
    AssertPtr(psz);

    size_t cch = strlen(psz) + 1;
    char *pszDup = (char *)MMR3HeapAllocU(pUVM, enmTag, cch);
    if (pszDup)
        memcpy(pszDup, psz, cch);
    return pszDup;
}


/**
 * Duplicates the specified string.
 *
 * @returns Pointer to the duplicate.
 * @returns NULL on failure or when input NULL.
 * @param   pVM         The cross context VM structure.
 * @param   enmTag      Statistics tag. Statistics are collected on a per tag
 *                      basis in addition to a global one. Thus we can easily
 *                      identify how memory is used by the VM. See MM_TAG_*.
 * @param   psz         The string to duplicate. NULL is allowed.
 */
VMMR3DECL(char *) MMR3HeapStrDup(PVM pVM, MMTAG enmTag, const char *psz)
{
    return MMR3HeapStrDupU(pVM->pUVM, enmTag, psz);
}


/**
 * Allocating string printf.
 *
 * @returns Pointer to the string.
 * @param   pVM         The cross context VM structure.
 * @param   enmTag      The statistics tag.
 * @param   pszFormat   The format string.
 * @param   ...         Format arguments.
 */
VMMR3DECL(char *)    MMR3HeapAPrintf(PVM pVM, MMTAG enmTag, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    char *psz = MMR3HeapAPrintfVU(pVM->pUVM, enmTag, pszFormat, va);
    va_end(va);
    return psz;
}


/**
 * Allocating string printf.
 *
 * @returns Pointer to the string.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   enmTag      The statistics tag.
 * @param   pszFormat   The format string.
 * @param   ...         Format arguments.
 */
VMMR3DECL(char *)    MMR3HeapAPrintfU(PUVM pUVM, MMTAG enmTag, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    char *psz = MMR3HeapAPrintfVU(pUVM, enmTag, pszFormat, va);
    va_end(va);
    return psz;
}


/**
 * Allocating string printf.
 *
 * @returns Pointer to the string.
 * @param   pVM         The cross context VM structure.
 * @param   enmTag      The statistics tag.
 * @param   pszFormat   The format string.
 * @param   va          Format arguments.
 */
VMMR3DECL(char *)    MMR3HeapAPrintfV(PVM pVM, MMTAG enmTag, const char *pszFormat, va_list va)
{
    return MMR3HeapAPrintfVU(pVM->pUVM, enmTag, pszFormat, va);
}


/**
 * Allocating string printf.
 *
 * @returns Pointer to the string.
 * @param   pUVM        Pointer to the user mode VM structure.
 * @param   enmTag      The statistics tag.
 * @param   pszFormat   The format string.
 * @param   va          Format arguments.
 */
VMMR3DECL(char *)    MMR3HeapAPrintfVU(PUVM pUVM, MMTAG enmTag, const char *pszFormat, va_list va)
{
    /*
     * The lazy bird way.
     */
    char *psz;
    int cch = RTStrAPrintfV(&psz, pszFormat, va);
    if (cch < 0)
        return NULL;
    Assert(psz[cch] == '\0');
    char *pszRet = (char *)MMR3HeapAllocU(pUVM, enmTag, cch + 1);
    if (pszRet)
        memcpy(pszRet, psz, cch + 1);
    RTStrFree(psz);
    return pszRet;
}


/**
 * Releases memory allocated with MMR3HeapAlloc() or MMR3HeapRealloc().
 *
 * The memory is cleared/filled before freeing to prevent heap spraying, info
 * leaks, and help detect use after free trouble.
 *
 * @param   pv          Pointer to the memory block to free.
 */
VMMR3DECL(void) MMR3HeapFree(void *pv)
{
    /* Ignore NULL pointers. */
    if (!pv)
        return;

    /*
     * Validate header.
     */
    PMMHEAPHDR const pHdr         = (PMMHEAPHDR)pv - 1;
    size_t const     cbAllocation = pHdr->cbSize;
    AssertMsgReturnVoid(   !(pHdr->cbSize & (MMR3HEAP_SIZE_ALIGNMENT - 1))
                        && !((uintptr_t)pHdr & (RTMEM_ALIGNMENT - 1)),
                        ("Invalid heap header! pv=%p, size=%#x\n", pv, pHdr->cbSize));
    AssertPtr(pHdr->pStat);
    Assert(!((uintptr_t)pHdr->pNext & (RTMEM_ALIGNMENT - 1)));
    Assert(!((uintptr_t)pHdr->pPrev & (RTMEM_ALIGNMENT - 1)));

    /*
     * Update statistics
     */
    PMMHEAP pHeap = pHdr->pStat->pHeap;
    RTCritSectEnter(&pHeap->Lock);

#ifdef MMR3HEAP_WITH_STATISTICS
    pHdr->pStat->cFrees++;
    pHeap->Stat.cFrees++;
    pHdr->pStat->cbFreed            += cbAllocation;
    pHeap->Stat.cbFreed             += cbAllocation;
    pHdr->pStat->cbCurAllocated     -= cbAllocation;
    pHeap->Stat.cbCurAllocated      -= cbAllocation;
#endif

    /*
     * Unlink it.
     */
    mmR3HeapUnlink(pHeap, pHdr);

    RTCritSectLeave(&pHeap->Lock);

    /*
     * Free the memory.  We clear just to be on the safe size wrt
     * heap spraying and leaking sensitive info (also helps detecting
     * double freeing).
     */
    RTMemFreeZ(pHdr, cbAllocation);
}

