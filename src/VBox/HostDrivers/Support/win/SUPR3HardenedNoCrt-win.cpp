/* $Id: SUPR3HardenedNoCrt-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Hardened main(), windows bits.
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
#include <iprt/nt/nt-and-windows.h>
#include <AccCtrl.h>
#include <AclApi.h>
#ifndef PROCESS_SET_LIMITED_INFORMATION
# define PROCESS_SET_LIMITED_INFORMATION 0x2000
#endif

#include <VBox/sup.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/heap.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/mem.h>
#include <iprt/utf16.h>

#include "SUPLibInternal.h"
#include "win/SUPHardenedVerify-win.h"


/*
 * assert.cpp
 */

RTDATADECL(char)                     g_szRTAssertMsg1[1024];
RTDATADECL(char)                     g_szRTAssertMsg2[4096];
RTDATADECL(const char * volatile)    g_pszRTAssertExpr;
RTDATADECL(const char * volatile)    g_pszRTAssertFile;
RTDATADECL(uint32_t volatile)        g_u32RTAssertLine;
RTDATADECL(const char * volatile)    g_pszRTAssertFunction;


RTDECL(bool) RTAssertMayPanic(void)
{
    return true;
}


RTDECL(void) RTAssertMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    /*
     * Fill in the globals.
     */
    g_pszRTAssertExpr       = pszExpr;
    g_pszRTAssertFile       = pszFile;
    g_pszRTAssertFunction   = pszFunction;
    g_u32RTAssertLine       = uLine;
    RTStrPrintf(g_szRTAssertMsg1, sizeof(g_szRTAssertMsg1),
                "\n!!Assertion Failed!!\n"
                "Expression: %s\n"
                "Location  : %s(%d) %s\n",
                pszExpr, pszFile, uLine, pszFunction);
}


RTDECL(void) RTAssertMsg2V(const char *pszFormat, va_list va)
{
    RTStrPrintfV(g_szRTAssertMsg2, sizeof(g_szRTAssertMsg2), pszFormat, va);
    if (g_enmSupR3HardenedMainState < SUPR3HARDENEDMAINSTATE_CALLED_TRUSTED_MAIN)
        supR3HardenedFatalMsg(g_pszRTAssertExpr, kSupInitOp_Misc, VERR_INTERNAL_ERROR,
                              "%s%s", g_szRTAssertMsg1,  g_szRTAssertMsg2);
    else
        supR3HardenedError(VERR_INTERNAL_ERROR, false/*fFatal*/, "%s%s", g_szRTAssertMsg1,  g_szRTAssertMsg2);
}


/*
 * Memory allocator.
 */

/** The handle of the heap we're using. */
static HANDLE       g_hSupR3HardenedHeap = NULL;
/** Number of heaps used during early process init. */
static uint32_t     g_cSupR3HardenedEarlyHeaps = 0;
/** Early process init heaps. */
static struct
{
    /** The heap handle. */
    RTHEAPSIMPLE    hHeap;
    /** The heap block pointer. */
    void           *pvBlock;
    /** The size of the heap block. */
    size_t          cbBlock;
    /** Number of active allocations on this heap. */
    size_t          cAllocations;
} g_aSupR3HardenedEarlyHeaps[8];


static uint32_t supR3HardenedEarlyFind(void *pv) RT_NOTHROW_DEF
{
    uint32_t iHeap = g_cSupR3HardenedEarlyHeaps;
    while (iHeap-- > 0)
        if ((uintptr_t)pv - (uintptr_t)g_aSupR3HardenedEarlyHeaps[iHeap].pvBlock < g_aSupR3HardenedEarlyHeaps[iHeap].cbBlock)
            return iHeap;
    return UINT32_MAX;
}


static void supR3HardenedEarlyCompact(void) RT_NOTHROW_DEF
{
    uint32_t iHeap = g_cSupR3HardenedEarlyHeaps;
    while (iHeap-- > 0)
        if (g_aSupR3HardenedEarlyHeaps[iHeap].cAllocations == 0)
        {
            PVOID  pvMem = g_aSupR3HardenedEarlyHeaps[iHeap].pvBlock;
            SIZE_T cbMem = g_aSupR3HardenedEarlyHeaps[iHeap].cbBlock;
            if (iHeap + 1 < g_cSupR3HardenedEarlyHeaps)
                g_aSupR3HardenedEarlyHeaps[iHeap] = g_aSupR3HardenedEarlyHeaps[g_cSupR3HardenedEarlyHeaps - 1];
            g_cSupR3HardenedEarlyHeaps--;

            NTSTATUS rcNt = NtFreeVirtualMemory(NtCurrentProcess(), &pvMem, &cbMem, MEM_RELEASE);
            Assert(NT_SUCCESS(rcNt)); RT_NOREF_PV(rcNt);
            SUP_DPRINTF(("supR3HardenedEarlyCompact: Removed heap %#u (%#p LB %#zx)\n", iHeap, pvMem, cbMem));
        }
}


static void *supR3HardenedEarlyAlloc(size_t cb, bool fZero) RT_NOTHROW_DEF
{
    /*
     * Try allocate on existing heaps.
     */
    void    *pv;
    uint32_t iHeap = 0;
    while (iHeap < g_cSupR3HardenedEarlyHeaps)
    {
        if (fZero)
            pv = RTHeapSimpleAllocZ(g_aSupR3HardenedEarlyHeaps[iHeap].hHeap, cb, 0);
        else
            pv = RTHeapSimpleAlloc(g_aSupR3HardenedEarlyHeaps[iHeap].hHeap, cb, 0);
        if (pv)
        {
            g_aSupR3HardenedEarlyHeaps[iHeap].cAllocations++;
#ifdef SUPR3HARDENED_EARLY_HEAP_TRACE
            SUP_DPRINTF(("Early heap: %p LB %#zx - alloc\n", pv, cb));
#endif
            return pv;
        }
        iHeap++;
    }

    /*
     * Add another heap.
     */
    if (iHeap == RT_ELEMENTS(g_aSupR3HardenedEarlyHeaps))
        supR3HardenedFatal("Early heap table is full (cb=%#zx).\n", cb);
    SIZE_T cbBlock = iHeap == 0 ? _1M : g_aSupR3HardenedEarlyHeaps[iHeap - 1].cbBlock * 2;
    while (cbBlock <= cb * 2)
        cbBlock *= 2;

    PVOID pvBlock = NULL;
    NTSTATUS rcNt = NtAllocateVirtualMemory(NtCurrentProcess(), &pvBlock, 0 /*ZeroBits*/, &cbBlock, MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(rcNt))
        supR3HardenedFatal("NtAllocateVirtualMemory(,,,%#zx,,) failed: rcNt=%#x\n", cbBlock, rcNt);
    SUP_DPRINTF(("New simple heap: #%u %p LB %#zx (for %zu allocation)\n", iHeap, pvBlock, cbBlock, cb));

    RTHEAPSIMPLE hHeap;
    int rc = RTHeapSimpleInit(&hHeap, pvBlock, cbBlock);
    if (RT_FAILURE(rc))
        supR3HardenedFatal("RTHeapSimpleInit(,%p,%#zx) failed: rc=%#x\n", pvBlock, cbBlock, rc);

    if (fZero)
        pv = RTHeapSimpleAllocZ(hHeap, cb, 0);
    else
        pv = RTHeapSimpleAlloc(hHeap, cb, 0);
    if (!pv)
        supR3HardenedFatal("RTHeapSimpleAlloc[Z] failed allocating %#zx bytes on a %#zu heap.\n", cb, cbBlock);

    g_aSupR3HardenedEarlyHeaps[iHeap].pvBlock      = pvBlock;
    g_aSupR3HardenedEarlyHeaps[iHeap].cbBlock      = cbBlock;
    g_aSupR3HardenedEarlyHeaps[iHeap].cAllocations = 1;
    g_aSupR3HardenedEarlyHeaps[iHeap].hHeap        = hHeap;

    Assert(g_cSupR3HardenedEarlyHeaps == iHeap);
    g_cSupR3HardenedEarlyHeaps                     = iHeap + 1;

#ifdef SUPR3HARDENED_EARLY_HEAP_TRACE
    SUP_DPRINTF(("Early heap: %p LB %#zx - alloc\n", pv, cb));
#endif
    return pv;
}


/**
 * Lazy heap initialization function.
 *
 * @returns Heap handle.
 */
static HANDLE supR3HardenedHeapInit(void) RT_NOTHROW_DEF
{
    Assert(g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_EP_CALLED);
    HANDLE hHeap = RtlCreateHeap(HEAP_GROWABLE | HEAP_CLASS_PRIVATE, NULL /*HeapBase*/,
                                 0 /*ReserveSize*/, 0 /*CommitSize*/,  NULL /*Lock*/, NULL /*Parameters*/);
    if (hHeap)
    {
        g_hSupR3HardenedHeap = hHeap;
        return hHeap;
    }

    supR3HardenedFatal("RtlCreateHeap failed.\n");
    /* not reached */
}


/**
 * Compacts the heaps before enter wait for parent/child.
 */
DECLHIDDEN(void) supR3HardenedWinCompactHeaps(void)
{
    if (g_hSupR3HardenedHeap)
        RtlCompactHeap(g_hSupR3HardenedHeap, 0 /*dwFlags*/);
    RtlCompactHeap(GetProcessHeap(), 0 /*dwFlags*/);
    supR3HardenedEarlyCompact();
}



#undef RTMemTmpAllocTag
RTDECL(void *) RTMemTmpAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return RTMemAllocTag(cb, pszTag);
}


#undef RTMemTmpAllocZTag
RTDECL(void *) RTMemTmpAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return RTMemAllocZTag(cb, pszTag);
}


#undef RTMemTmpFree
RTDECL(void) RTMemTmpFree(void *pv) RT_NO_THROW_DEF
{
    RTMemFree(pv);
}


#undef RTMemAllocTag
RTDECL(void *) RTMemAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    RT_NOREF1(pszTag);
    HANDLE hHeap = g_hSupR3HardenedHeap;
    if (!hHeap)
    {
        if (   g_fSupEarlyProcessInit
            && g_enmSupR3HardenedMainState <= SUPR3HARDENEDMAINSTATE_WIN_EP_CALLED)
            return supR3HardenedEarlyAlloc(cb, false /*fZero*/);
        hHeap = supR3HardenedHeapInit();
    }

    void *pv = RtlAllocateHeap(hHeap, 0 /*fFlags*/, cb);
    if (!pv)
        supR3HardenedFatal("RtlAllocateHeap failed to allocate %zu bytes.\n", cb);
    return pv;
}


#undef RTMemAllocZTag
RTDECL(void *) RTMemAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    RT_NOREF1(pszTag);
    HANDLE hHeap = g_hSupR3HardenedHeap;
    if (!hHeap)
    {
        if (   g_fSupEarlyProcessInit
            && g_enmSupR3HardenedMainState <= SUPR3HARDENEDMAINSTATE_WIN_EP_CALLED)
            return supR3HardenedEarlyAlloc(cb, true /*fZero*/);
        hHeap = supR3HardenedHeapInit();
    }

    void *pv = RtlAllocateHeap(hHeap, HEAP_ZERO_MEMORY, cb);
    if (!pv)
        supR3HardenedFatal("RtlAllocateHeap failed to allocate %zu bytes.\n", cb);
    return pv;
}


#undef RTMemAllocVarTag
RTDECL(void *) RTMemAllocVarTag(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return RTMemAllocTag(cbAligned, pszTag);
}


#undef RTMemAllocZVarTag
RTDECL(void *) RTMemAllocZVarTag(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_DEF
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return RTMemAllocZTag(cbAligned, pszTag);
}


#undef RTMemReallocTag
RTDECL(void *) RTMemReallocTag(void *pvOld, size_t cbNew, const char *pszTag) RT_NO_THROW_DEF
{
    if (!pvOld)
        return RTMemAllocZTag(cbNew, pszTag);

    void *pv;
    if (g_fSupEarlyProcessInit)
    {
        uint32_t iHeap = supR3HardenedEarlyFind(pvOld);
        if (iHeap != UINT32_MAX)
        {
#if 0 /* RTHeapSimpleRealloc is not implemented */
            /* If this is before we can use a regular heap, we try resize
               within the simple heap.  (There are a lot of array growing in
               the ASN.1 code.) */
            if (g_enmSupR3HardenedMainState < SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED)
            {
                pv = RTHeapSimpleRealloc(g_aSupR3HardenedEarlyHeaps[iHeap].hHeap, pvOld, cbNew, 0);
                if (pv)
                {
# ifdef SUPR3HARDENED_EARLY_HEAP_TRACE
                    SUP_DPRINTF(("Early heap: %p LB %#zx, was %p - realloc\n", pvNew, cbNew, pvOld));
# endif
                    return pv;
                }
            }
#endif

            /* Either we can't reallocate it on the same simple heap, or we're
               past hardened main and wish to migrate everything over on the
               real heap. */
            size_t cbOld = RTHeapSimpleSize(g_aSupR3HardenedEarlyHeaps[iHeap].hHeap, pvOld);
            pv = RTMemAllocTag(cbNew, pszTag);
            if (pv)
            {
                memcpy(pv, pvOld, RT_MIN(cbOld, cbNew));
                RTHeapSimpleFree(g_aSupR3HardenedEarlyHeaps[iHeap].hHeap, pvOld);
                if (g_aSupR3HardenedEarlyHeaps[iHeap].cAllocations)
                    g_aSupR3HardenedEarlyHeaps[iHeap].cAllocations--;
                if (   !g_aSupR3HardenedEarlyHeaps[iHeap].cAllocations
                    && g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED)
                    supR3HardenedEarlyCompact();
            }
# ifdef SUPR3HARDENED_EARLY_HEAP_TRACE
            SUP_DPRINTF(("Early heap: %p LB %#zx, was %p %LB %#zx - realloc\n", pv, cbNew, pvOld, cbOld));
# endif
            return pv;
        }
        Assert(g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED);
    }

    /* Allocate from the regular heap. */
    HANDLE hHeap = g_hSupR3HardenedHeap;
    Assert(hHeap != NULL);
    pv = RtlReAllocateHeap(hHeap, 0 /*dwFlags*/, pvOld, cbNew);
    if (!pv)
        supR3HardenedFatal("RtlReAllocateHeap failed to allocate %zu bytes.\n", cbNew);
    return pv;
}


#undef RTMemFree
RTDECL(void) RTMemFree(void *pv) RT_NO_THROW_DEF
{
    if (pv)
    {
        if (g_fSupEarlyProcessInit)
        {
            uint32_t iHeap = supR3HardenedEarlyFind(pv);
            if (iHeap != UINT32_MAX)
            {
#ifdef SUPR3HARDENED_EARLY_HEAP_TRACE
                SUP_DPRINTF(("Early heap: %p - free\n", pv));
#endif
                RTHeapSimpleFree(g_aSupR3HardenedEarlyHeaps[iHeap].hHeap, pv);
                if (g_aSupR3HardenedEarlyHeaps[iHeap].cAllocations)
                    g_aSupR3HardenedEarlyHeaps[iHeap].cAllocations--;
                if (   !g_aSupR3HardenedEarlyHeaps[iHeap].cAllocations
                    && g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED)
                    supR3HardenedEarlyCompact();
                return;
            }
            Assert(g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED);
        }

        HANDLE hHeap = g_hSupR3HardenedHeap;
        Assert(hHeap != NULL);
        RtlFreeHeap(hHeap, 0 /* dwFlags*/, pv);
    }
}


/*
 * Simplified version of RTMemWipeThoroughly that avoids dragging in the
 * random number code.
 */

RTDECL(void) RTMemWipeThoroughly(void *pv, size_t cb, size_t cMinPasses) RT_NO_THROW_DEF
{
    size_t cPasses = RT_MIN(cMinPasses, 6);
    static const uint32_t s_aPatterns[] = { 0x00, 0xaa, 0x55, 0xff, 0xf0, 0x0f, 0xcc, 0x3c, 0xc3 };
    uint32_t iPattern = 0;
    do
    {
        memset(pv, s_aPatterns[iPattern], cb);
        iPattern = (iPattern + 1) % RT_ELEMENTS(s_aPatterns);
        ASMMemoryFence();

        memset(pv, s_aPatterns[iPattern], cb);
        iPattern = (iPattern + 1) % RT_ELEMENTS(s_aPatterns);
        ASMMemoryFence();

        memset(pv, s_aPatterns[iPattern], cb);
        iPattern = (iPattern + 1) % RT_ELEMENTS(s_aPatterns);
        ASMMemoryFence();
    } while (cPasses-- > 0);

    memset(pv, 0xff, cb);
    ASMMemoryFence();
}



/*
 * path-win.cpp
 */

RTDECL(int) RTPathGetCurrent(char *pszPath, size_t cbPath)
{
    int rc;
    if (g_enmSupR3HardenedMainState < SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED)
/** @todo Rainy day: improve this by checking the process parameter block
 *        (needs to be normalized). */
        rc = RTStrCopy(pszPath, cbPath, "C:\\");
    else
    {
        /*
         * GetCurrentDirectory may in some cases omit the drive letter, according
         * to MSDN, thus the GetFullPathName call.
         */
        RTUTF16 wszCurPath[RTPATH_MAX];
        if (GetCurrentDirectoryW(RTPATH_MAX, wszCurPath))
        {
            RTUTF16 wszFullPath[RTPATH_MAX];
            if (GetFullPathNameW(wszCurPath, RTPATH_MAX, wszFullPath, NULL))
                rc = RTUtf16ToUtf8Ex(&wszFullPath[0], RTSTR_MAX, &pszPath, cbPath, NULL);
            else
                rc = RTErrConvertFromWin32(RtlGetLastWin32Error());
        }
        else
            rc = RTErrConvertFromWin32(RtlGetLastWin32Error());
    }
    return rc;
}

