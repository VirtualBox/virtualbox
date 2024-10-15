/* $Id: dbgmodcontainer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Info Container.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_DBG
#include <iprt/dbg.h>
#include "internal/iprt.h"

#include <iprt/avl.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#define RTDBGMODCNT_WITH_MEM_CACHE
#ifdef RTDBGMODCNT_WITH_MEM_CACHE
# include <iprt/memcache.h>
#endif
#include <iprt/string.h>
#include <iprt/strcache.h>
#include "internal/dbgmod.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Symbol entry.
 */
typedef struct RTDBGMODCTNSYMBOL
{
    /** The address core. */
    AVLRUINTPTRNODECORE         AddrCore;
    /** The name space core. */
    RTSTRSPACECORE              NameCore;
    /** The ordinal number core. */
    AVLU32NODECORE              OrdinalCore;
    /** The segment index. */
    RTDBGSEGIDX                 iSeg;
    /** The symbol flags. */
    uint32_t                    fFlags;
    /** The symbol size.
     * This may be zero while the range in AddrCore indicates 0. */
    RTUINTPTR                   cb;
} RTDBGMODCTNSYMBOL;
/** Pointer to a symbol entry in the debug info container. */
typedef RTDBGMODCTNSYMBOL *PRTDBGMODCTNSYMBOL;
/** Pointer to a const symbol entry in the debug info container. */
typedef RTDBGMODCTNSYMBOL const *PCRTDBGMODCTNSYMBOL;

/**
 * Line number entry.
 */
typedef struct RTDBGMODCTNLINE
{
    /** The address core.
     * The Key is the address of the line number. */
    AVLUINTPTRNODECORE          AddrCore;
    /** The ordinal number core. */
    AVLU32NODECORE              OrdinalCore;
    /** Pointer to the file name (in string cache). */
    const char                 *pszFile;
    /** The line number. */
    uint32_t                    uLineNo;
    /** The segment index. */
    RTDBGSEGIDX                 iSeg;
} RTDBGMODCTNLINE;
/** Pointer to a line number entry. */
typedef RTDBGMODCTNLINE *PRTDBGMODCTNLINE;
/** Pointer to const a line number entry. */
typedef RTDBGMODCTNLINE const *PCRTDBGMODCTNLINE;

/**
 * Segment entry.
 */
typedef struct RTDBGMODCTNSEGMENT
{
    /** The symbol address space tree. */
    AVLRUINTPTRTREE             SymAddrTree;
    /** The line number address space tree. */
    AVLUINTPTRTREE              LineAddrTree;
    /** The segment offset. */
    RTUINTPTR                   off;
    /** The segment size. */
    RTUINTPTR                   cb;
    /** The segment flags. */
    uint32_t                    fFlags;
    /** The segment name. */
    const char                 *pszName;
} RTDBGMODCTNSEGMENT;
/** Pointer to a segment entry in the debug info container. */
typedef RTDBGMODCTNSEGMENT *PRTDBGMODCTNSEGMENT;
/** Pointer to a const segment entry in the debug info container. */
typedef RTDBGMODCTNSEGMENT const *PCRTDBGMODCTNSEGMENT;

/**
 * Instance data.
 */
typedef struct RTDBGMODCTN
{
    /** The name space. */
    RTSTRSPACE                  Names;
    /** Tree containing any absolute addresses. */
    AVLRUINTPTRTREE             AbsAddrTree;
    /** Tree organizing the symbols by ordinal number. */
    AVLU32TREE                  SymbolOrdinalTree;
     /** Tree organizing the line numbers by ordinal number. */
    AVLU32TREE                  LineOrdinalTree;
    /** Segment table. */
    PRTDBGMODCTNSEGMENT         paSegs;
    /** The number of segments in the segment table. */
    RTDBGSEGIDX                 cSegs;
    /** The image size. 0 means unlimited. */
    RTUINTPTR                   cb;
    /** The next symbol ordinal. */
    uint32_t                    iNextSymbolOrdinal;
    /** The next line number ordinal. */
    uint32_t                    iNextLineOrdinal;
#ifdef RTDBGMODCNT_WITH_MEM_CACHE
    /** Line number allocator.
     * Using a cache is a bit overkill since we normally won't free them, but
     * it's a construct that exists and does the job relatively efficiently. */
    RTMEMCACHE                  hLineNumAllocator;
#endif
} RTDBGMODCTN;
/** Pointer to instance data for the debug info container. */
typedef RTDBGMODCTN *PRTDBGMODCTN;



/** @interface_method_impl{RTDBGMODVTDBG,pfnUnwindFrame} */
static DECLCALLBACK(int)
rtDbgModContainer_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    RT_NOREF(pMod, iSeg, off, pState);
    return VERR_DBG_NO_UNWIND_INFO;
}


/**
 * Fills in a RTDBGSYMBOL structure.
 *
 * @returns VINF_SUCCESS.
 * @param   pMySym          Our internal symbol representation.
 * @param   pExtSym         The external symbol representation.
 */
DECLINLINE(int) rtDbgModContainerReturnSymbol(PCRTDBGMODCTNSYMBOL pMySym, PRTDBGSYMBOL pExtSym)
{
    pExtSym->Value    = pMySym->AddrCore.Key;
    pExtSym->offSeg   = pMySym->AddrCore.Key;
    pExtSym->iSeg     = pMySym->iSeg;
    pExtSym->fFlags   = pMySym->fFlags;
    pExtSym->cb       = pMySym->cb;
    pExtSym->iOrdinal = pMySym->OrdinalCore.Key;
    Assert(pMySym->NameCore.cchString < sizeof(pExtSym->szName));
    memcpy(pExtSym->szName, pMySym->NameCore.pszString, pMySym->NameCore.cchString + 1);
    return VINF_SUCCESS;
}



/** @copydoc RTDBGMODVTDBG::pfnLineByAddr */
static DECLCALLBACK(int) rtDbgModContainer_LineByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                                      PRTINTPTR poffDisp, PRTDBGLINE pLineInfo)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /*
     * Validate the input address.
     */
    AssertMsgReturn(iSeg < pThis->cSegs,
                    ("iSeg=%#x cSegs=%#x\n", iSeg, pThis->cSegs),
                    VERR_DBG_INVALID_SEGMENT_INDEX);
    AssertMsgReturn(off < pThis->paSegs[iSeg].cb,
                    ("off=%RTptr cbSeg=%RTptr\n", off, pThis->paSegs[iSeg].cb),
                    VERR_DBG_INVALID_SEGMENT_OFFSET);

    /*
     * Lookup the nearest line number with an address less or equal to the specified address.
     */
    PAVLUINTPTRNODECORE pAvlCore = RTAvlUIntPtrGetBestFit(&pThis->paSegs[iSeg].LineAddrTree, off, false /*fAbove*/);
    if (!pAvlCore)
        return pThis->iNextLineOrdinal
             ? VERR_DBG_LINE_NOT_FOUND
             : VERR_DBG_NO_LINE_NUMBERS;
    PCRTDBGMODCTNLINE pMyLine = RT_FROM_MEMBER(pAvlCore, RTDBGMODCTNLINE const, AddrCore);
    pLineInfo->Address = pMyLine->AddrCore.Key;
    pLineInfo->offSeg  = pMyLine->AddrCore.Key;
    pLineInfo->iSeg    = iSeg;
    pLineInfo->uLineNo = pMyLine->uLineNo;
    pLineInfo->iOrdinal = pMyLine->OrdinalCore.Key;
    strcpy(pLineInfo->szFilename, pMyLine->pszFile);
    if (poffDisp)
        *poffDisp = off - pMyLine->AddrCore.Key;
    return VINF_SUCCESS;
}


/** @copydoc RTDBGMODVTDBG::pfnLineByOrdinal */
static DECLCALLBACK(int) rtDbgModContainer_LineByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /*
     * Look it up.
     */
    if (iOrdinal >= pThis->iNextLineOrdinal)
        return pThis->iNextLineOrdinal
             ? VERR_DBG_LINE_NOT_FOUND
             : VERR_DBG_NO_LINE_NUMBERS;
    PAVLU32NODECORE pAvlCore = RTAvlU32Get(&pThis->LineOrdinalTree, iOrdinal);
    AssertReturn(pAvlCore, VERR_DBG_LINE_NOT_FOUND);
    PCRTDBGMODCTNLINE pMyLine = RT_FROM_MEMBER(pAvlCore, RTDBGMODCTNLINE const, OrdinalCore);
    pLineInfo->Address  = pMyLine->AddrCore.Key;
    pLineInfo->offSeg   = pMyLine->AddrCore.Key;
    pLineInfo->iSeg     = pMyLine->iSeg;
    pLineInfo->uLineNo  = pMyLine->uLineNo;
    pLineInfo->iOrdinal = pMyLine->OrdinalCore.Key;
    strcpy(pLineInfo->szFilename, pMyLine->pszFile);
    return VINF_SUCCESS;
}


/** @copydoc RTDBGMODVTDBG::pfnLineCount */
static DECLCALLBACK(uint32_t) rtDbgModContainer_LineCount(PRTDBGMODINT pMod)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /* Note! The ordinal numbers are 0-based. */
    return pThis->iNextLineOrdinal;
}


/** @copydoc RTDBGMODVTDBG::pfnLineAdd */
static DECLCALLBACK(int) rtDbgModContainer_LineAdd(PRTDBGMODINT pMod, const char *pszFile, size_t cchFile, uint32_t uLineNo,
                                                   uint32_t iSeg, RTUINTPTR off, uint32_t *piOrdinal)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /*
     * Validate the input address.
     */
    AssertMsgReturn(iSeg < pThis->cSegs,          ("iSeg=%#x cSegs=%#x\n", iSeg, pThis->cSegs),
                    VERR_DBG_INVALID_SEGMENT_INDEX);
    AssertMsgReturn(off <= pThis->paSegs[iSeg].cb, ("off=%RTptr cbSeg=%RTptr\n", off, pThis->paSegs[iSeg].cb),
                    VERR_DBG_INVALID_SEGMENT_OFFSET);

    /*
     * Create a new entry.
     */
#ifdef RTDBGMODCNT_WITH_MEM_CACHE
    PRTDBGMODCTNLINE pLine = (PRTDBGMODCTNLINE)RTMemCacheAlloc(pThis->hLineNumAllocator);
#else
    PRTDBGMODCTNLINE pLine = (PRTDBGMODCTNLINE)RTMemAllocZ(sizeof(*pLine));
#endif
    if (!pLine)
        return VERR_NO_MEMORY;
    pLine->AddrCore.Key     = off;
    pLine->OrdinalCore.Key  = pThis->iNextLineOrdinal;
    pLine->uLineNo          = uLineNo;
    pLine->iSeg             = iSeg;
    pLine->pszFile          = RTStrCacheEnterN(g_hDbgModStrCache, pszFile, cchFile);
    int rc;
    if (pLine->pszFile)
    {
        if (RTAvlUIntPtrInsert(&pThis->paSegs[iSeg].LineAddrTree, &pLine->AddrCore))
        {
            if (RTAvlU32Insert(&pThis->LineOrdinalTree, &pLine->OrdinalCore))
            {
                if (piOrdinal)
                    *piOrdinal = pThis->iNextLineOrdinal;
                pThis->iNextLineOrdinal++;
                return VINF_SUCCESS;
            }

            rc = VERR_INTERNAL_ERROR_5;
            RTAvlUIntPtrRemove(&pThis->paSegs[iSeg].LineAddrTree, pLine->AddrCore.Key);
        }

        /* bail out */
        rc = VERR_DBG_ADDRESS_CONFLICT;
        RTStrCacheRelease(g_hDbgModStrCache, pLine->pszFile);
    }
    else
        rc = VERR_NO_MEMORY;
#ifdef RTDBGMODCNT_WITH_MEM_CACHE
    RTMemCacheFree(pThis->hLineNumAllocator, pLine);
#else
    RTMemFree(pLine);
#endif
    return rc;
}


/** @copydoc RTDBGMODVTDBG::pfnSymbolByAddr */
static DECLCALLBACK(int) rtDbgModContainer_SymbolByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                                        PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /*
     * Validate the input address.
     */
    AssertMsgReturn(    iSeg == RTDBGSEGIDX_ABS
                    ||  iSeg < pThis->cSegs,
                    ("iSeg=%#x cSegs=%#x\n", iSeg, pThis->cSegs),
                    VERR_DBG_INVALID_SEGMENT_INDEX);
    AssertMsgReturn(    iSeg >= RTDBGSEGIDX_SPECIAL_FIRST
                    ||  off <= pThis->paSegs[iSeg].cb,
                    ("off=%RTptr cbSeg=%RTptr\n", off, pThis->paSegs[iSeg].cb),
                    VERR_DBG_INVALID_SEGMENT_OFFSET);

    /*
     * Lookup the nearest symbol with an address less or equal to the specified address.
     */
    PAVLRUINTPTRNODECORE pAvlCore = RTAvlrUIntPtrGetBestFit(  iSeg == RTDBGSEGIDX_ABS
                                                            ? &pThis->AbsAddrTree
                                                            : &pThis->paSegs[iSeg].SymAddrTree,
                                                            off,
                                                            fFlags == RTDBGSYMADDR_FLAGS_GREATER_OR_EQUAL /*fAbove*/);
    if (!pAvlCore)
        return VERR_SYMBOL_NOT_FOUND;
    PCRTDBGMODCTNSYMBOL pMySym = RT_FROM_MEMBER(pAvlCore, RTDBGMODCTNSYMBOL const, AddrCore);
    if (poffDisp)
        *poffDisp = off - pMySym->AddrCore.Key;
    return rtDbgModContainerReturnSymbol(pMySym, pSymInfo);
}


/** @copydoc RTDBGMODVTDBG::pfnSymbolByName */
static DECLCALLBACK(int) rtDbgModContainer_SymbolByName(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;
    NOREF(cchSymbol);

    /*
     * Look it up in the name space.
     */
    PRTSTRSPACECORE pStrCore = RTStrSpaceGet(&pThis->Names, pszSymbol);
    if (!pStrCore)
        return VERR_SYMBOL_NOT_FOUND;
    PCRTDBGMODCTNSYMBOL pMySym = RT_FROM_MEMBER(pStrCore, RTDBGMODCTNSYMBOL const, NameCore);
    return rtDbgModContainerReturnSymbol(pMySym, pSymInfo);
}


/** @copydoc RTDBGMODVTDBG::pfnSymbolByOrdinal */
static DECLCALLBACK(int) rtDbgModContainer_SymbolByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /*
     * Look it up in the ordinal tree.
     */
    if (iOrdinal >= pThis->iNextSymbolOrdinal)
        return pThis->iNextSymbolOrdinal
             ? VERR_DBG_NO_SYMBOLS
             : VERR_SYMBOL_NOT_FOUND;
    PAVLU32NODECORE pAvlCore = RTAvlU32Get(&pThis->SymbolOrdinalTree, iOrdinal);
    AssertReturn(pAvlCore, VERR_SYMBOL_NOT_FOUND);
    PCRTDBGMODCTNSYMBOL pMySym = RT_FROM_MEMBER(pAvlCore, RTDBGMODCTNSYMBOL const, OrdinalCore);
    return rtDbgModContainerReturnSymbol(pMySym, pSymInfo);
}


/** @copydoc RTDBGMODVTDBG::pfnSymbolCount */
static DECLCALLBACK(uint32_t) rtDbgModContainer_SymbolCount(PRTDBGMODINT pMod)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /* Note! The ordinal numbers are 0-based. */
    return pThis->iNextSymbolOrdinal;
}


/**
 * Worker for rtDbgModContainer_SymbolAdd that removes a symbol to resolve
 * address conflicts.
 *
 * We don't shift ordinals up as that could be very expensive, instead we move
 * the last one up to take the place of the one we're removing.  Caller must
 * take this into account.
 *
 * @param   pThis               The container.
 * @param   pAddrTree           The address tree to remove from.
 * @param   pToRemove           The conflicting symbol to be removed.
 */
static void rtDbgModContainer_SymbolReplace(PRTDBGMODCTN pThis, PAVLRUINTPTRTREE pAddrTree, PRTDBGMODCTNSYMBOL pToRemove)
{
    Log(("rtDbgModContainer_SymbolReplace: pToRemove=%p ordinal=%u %04x:%08RX64 %s\n",
         pToRemove, pToRemove->OrdinalCore.Key, pToRemove->iSeg, pToRemove->AddrCore.Key, pToRemove->NameCore.pszString));

    /* Unlink it. */
    PRTSTRSPACECORE pRemovedName = RTStrSpaceRemove(&pThis->Names, pToRemove->NameCore.pszString);
    Assert(pRemovedName); RT_NOREF_PV(pRemovedName);
    pToRemove->NameCore.pszString = NULL;

    PAVLRUINTPTRNODECORE pRemovedAddr = RTAvlrUIntPtrRemove(pAddrTree, pToRemove->AddrCore.Key);
    Assert(pRemovedAddr); RT_NOREF_PV(pRemovedAddr);
    pToRemove->AddrCore.Key = 0;
    pToRemove->AddrCore.KeyLast = 0;

    uint32_t const iOrdinal = pToRemove->OrdinalCore.Key;
    PAVLU32NODECORE pRemovedOrdinal = RTAvlU32Remove(&pThis->SymbolOrdinalTree, iOrdinal);
    Assert(pRemovedOrdinal); RT_NOREF_PV(pRemovedOrdinal);

    RTMemFree(pToRemove);

    /* Jump the last symbol ordinal to take its place, unless pToRemove is the last one. */
    if (iOrdinal >= pThis->iNextSymbolOrdinal - 1)
        pThis->iNextSymbolOrdinal -= 1;
    else
    {
        PAVLU32NODECORE pLastOrdinal = RTAvlU32Remove(&pThis->SymbolOrdinalTree, pThis->iNextSymbolOrdinal - 1);
        AssertReturnVoid(pLastOrdinal);

        pThis->iNextSymbolOrdinal -= 1;
        pLastOrdinal->Key = iOrdinal;
        bool fInsert = RTAvlU32Insert(&pThis->SymbolOrdinalTree, pLastOrdinal);
        Assert(fInsert); RT_NOREF_PV(fInsert);
    }
}


/** @copydoc RTDBGMODVTDBG::pfnSymbolAdd */
static DECLCALLBACK(int) rtDbgModContainer_SymbolAdd(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                     RTDBGSEGIDX iSeg, RTUINTPTR off, RTUINTPTR cb, uint32_t fFlags,
                                                     uint32_t *piOrdinal)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /*
     * Address validation. The other arguments have already been validated.
     */
    AssertMsgReturn(    iSeg == RTDBGSEGIDX_ABS
                    ||  iSeg < pThis->cSegs,
                    ("iSeg=%#x cSegs=%#x\n", iSeg, pThis->cSegs),
                    VERR_DBG_INVALID_SEGMENT_INDEX);
    AssertMsgReturn(    iSeg >= RTDBGSEGIDX_SPECIAL_FIRST
                    ||  off <= pThis->paSegs[iSeg].cb,
                    ("off=%RTptr cb=%RTptr cbSeg=%RTptr\n", off, cb, pThis->paSegs[iSeg].cb),
                    VERR_DBG_INVALID_SEGMENT_OFFSET);

    /* Be a little relaxed wrt to the symbol size. */
    int rc = VINF_SUCCESS;
    if (iSeg != RTDBGSEGIDX_ABS && off + cb > pThis->paSegs[iSeg].cb)
    {
        cb = pThis->paSegs[iSeg].cb - off;
        rc = VINF_DBG_ADJUSTED_SYM_SIZE;
    }

    /*
     * Create a new entry.
     */
    PRTDBGMODCTNSYMBOL pSymbol = (PRTDBGMODCTNSYMBOL)RTMemAllocZ(sizeof(*pSymbol));
    if (!pSymbol)
        return VERR_NO_MEMORY;

    pSymbol->AddrCore.Key       = off;
    pSymbol->AddrCore.KeyLast   = off + (cb ? cb - 1 : 0);
    pSymbol->OrdinalCore.Key    = pThis->iNextSymbolOrdinal;
    pSymbol->iSeg               = iSeg;
    pSymbol->cb                 = cb;
    pSymbol->fFlags             = fFlags;
    pSymbol->NameCore.pszString = RTStrCacheEnterN(g_hDbgModStrCache, pszSymbol, cchSymbol);
    if (pSymbol->NameCore.pszString)
    {
        if (RTStrSpaceInsert(&pThis->Names, &pSymbol->NameCore))
        {
            PAVLRUINTPTRTREE pAddrTree = iSeg == RTDBGSEGIDX_ABS
                                       ? &pThis->AbsAddrTree
                                       : &pThis->paSegs[iSeg].SymAddrTree;
            if (RTAvlrUIntPtrInsert(pAddrTree, &pSymbol->AddrCore))
            {
                if (RTAvlU32Insert(&pThis->SymbolOrdinalTree, &pSymbol->OrdinalCore))
                {
                    /*
                     * Success.
                     */
                    if (piOrdinal)
                        *piOrdinal = pThis->iNextSymbolOrdinal;
                    Log12(("rtDbgModContainer_SymbolAdd: ordinal=%u %04x:%08RX64 LB %#RX64 %s\n",
                           pThis->iNextSymbolOrdinal, iSeg, off, cb, pSymbol->NameCore.pszString));
                    pThis->iNextSymbolOrdinal++;
                    return rc;
                }

                /* bail out */
                rc = VERR_INTERNAL_ERROR_5;
                RTAvlrUIntPtrRemove(pAddrTree, pSymbol->AddrCore.Key);
            }
            /*
             * Did the caller specify a conflict resolution method?
             */
            else if (fFlags & (  RTDBGSYMBOLADD_F_REPLACE_SAME_ADDR
                               | RTDBGSYMBOLADD_F_REPLACE_ANY
                               | RTDBGSYMBOLADD_F_ADJUST_SIZES_ON_CONFLICT))
            {
                /*
                 * Handle anything at or before the start address first:
                 */
                AssertCompileMemberOffset(RTDBGMODCTNSYMBOL, AddrCore, 0);
                PRTDBGMODCTNSYMBOL pConflict = (PRTDBGMODCTNSYMBOL)RTAvlrUIntPtrRangeGet(pAddrTree, pSymbol->AddrCore.Key);
                if (pConflict)
                {
                    if (pConflict->AddrCore.Key == pSymbol->AddrCore.Key)
                    {
                        /* Same address, only option is replacing it. */
                        if (fFlags & (RTDBGSYMBOLADD_F_REPLACE_SAME_ADDR | RTDBGSYMBOLADD_F_REPLACE_ANY))
                            rtDbgModContainer_SymbolReplace(pThis, pAddrTree, pConflict);
                        else
                            rc = VERR_DBG_ADDRESS_CONFLICT;
                    }
                    else if (fFlags & RTDBGSYMBOLADD_F_ADJUST_SIZES_ON_CONFLICT)
                    {
                        /* Reduce the size of the symbol before us, adopting the size if we've got none. */
                        Assert(pConflict->AddrCore.Key < pSymbol->AddrCore.Key);
                        if (!pSymbol->cb)
                        {
                            pSymbol->AddrCore.KeyLast = pSymbol->AddrCore.KeyLast;
                            pSymbol->cb               = pSymbol->AddrCore.KeyLast - pConflict->AddrCore.Key + 1;
                            rc = VINF_DBG_ADJUSTED_SYM_SIZE;
                        }
                        pConflict->AddrCore.KeyLast = pSymbol->AddrCore.Key - 1;
                        pConflict->cb               = pSymbol->AddrCore.Key - pConflict->AddrCore.Key;
                    }
                    else if (fFlags & RTDBGSYMBOLADD_F_REPLACE_ANY)
                        rtDbgModContainer_SymbolReplace(pThis, pAddrTree, pConflict);
                    else
                        rc = VERR_DBG_ADDRESS_CONFLICT;
                }

                /*
                 * Try insert again and deal with symbols in the range.
                 */
                while (RT_SUCCESS(rc))
                {
                    if (RTAvlrUIntPtrInsert(pAddrTree, &pSymbol->AddrCore))
                    {
                        pSymbol->OrdinalCore.Key = pThis->iNextSymbolOrdinal;
                        if (RTAvlU32Insert(&pThis->SymbolOrdinalTree, &pSymbol->OrdinalCore))
                        {
                            /*
                             * Success.
                             */
                            if (piOrdinal)
                                *piOrdinal = pThis->iNextSymbolOrdinal;
                            pThis->iNextSymbolOrdinal++;
                            Log12(("rtDbgModContainer_SymbolAdd: ordinal=%u %04x:%08RX64 LB %#RX64 %s [replace codepath]\n",
                                   pThis->iNextSymbolOrdinal, iSeg, off, cb, pSymbol->NameCore.pszString));
                            return rc;
                        }

                        rc = VERR_INTERNAL_ERROR_5;
                        RTAvlrUIntPtrRemove(pAddrTree, pSymbol->AddrCore.Key);
                        break;
                    }

                    /* Get the first symbol above us and see if we can do anything about it (or ourselves). */
                    AssertCompileMemberOffset(RTDBGMODCTNSYMBOL, AddrCore, 0);
                    pConflict = (PRTDBGMODCTNSYMBOL)RTAvlrUIntPtrGetBestFit(pAddrTree, pSymbol->AddrCore.Key, true /*fAbove*/);
                    AssertBreakStmt(pConflict, rc = VERR_DBG_ADDRESS_CONFLICT);
                    Assert(pSymbol->AddrCore.Key     != pConflict->AddrCore.Key);
                    Assert(pSymbol->AddrCore.KeyLast >= pConflict->AddrCore.Key);

                    if (fFlags & RTDBGSYMBOLADD_F_ADJUST_SIZES_ON_CONFLICT)
                    {
                        Assert(pSymbol->cb > 0);
                        pSymbol->AddrCore.Key = pConflict->AddrCore.Key - 1;
                        pSymbol->cb           = pConflict->AddrCore.Key - pSymbol->AddrCore.Key;
                        rc = VINF_DBG_ADJUSTED_SYM_SIZE;
                    }
                    else if (fFlags & RTDBGSYMBOLADD_F_REPLACE_ANY)
                        rtDbgModContainer_SymbolReplace(pThis, pAddrTree, pConflict);
                    else
                        rc = VERR_DBG_ADDRESS_CONFLICT;
                }
            }
            else
                rc = VERR_DBG_ADDRESS_CONFLICT;
            RTStrSpaceRemove(&pThis->Names, pSymbol->NameCore.pszString);
        }
        else
            rc = VERR_DBG_DUPLICATE_SYMBOL;
        RTStrCacheRelease(g_hDbgModStrCache, pSymbol->NameCore.pszString);
    }
    else
        rc = VERR_NO_MEMORY;
    RTMemFree(pSymbol);
    return rc;
}


/** @copydoc RTDBGMODVTDBG::pfnSegmentByIndex */
static DECLCALLBACK(int) rtDbgModContainer_SegmentByIndex(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;
    if (iSeg >= pThis->cSegs)
        return VERR_DBG_INVALID_SEGMENT_INDEX;
    pSegInfo->Address = RTUINTPTR_MAX;
    pSegInfo->uRva    = pThis->paSegs[iSeg].off;
    pSegInfo->cb      = pThis->paSegs[iSeg].cb;
    pSegInfo->fFlags  = pThis->paSegs[iSeg].fFlags;
    pSegInfo->iSeg    = iSeg;
    strcpy(pSegInfo->szName, pThis->paSegs[iSeg].pszName);
    return VINF_SUCCESS;
}


/** @copydoc RTDBGMODVTDBG::pfnSegmentCount */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModContainer_SegmentCount(PRTDBGMODINT pMod)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;
    return pThis->cSegs;
}


/** @copydoc RTDBGMODVTDBG::pfnSegmentAdd */
static DECLCALLBACK(int) rtDbgModContainer_SegmentAdd(PRTDBGMODINT pMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName, size_t cchName,
                                                      uint32_t fFlags, PRTDBGSEGIDX piSeg)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /*
     * Input validation (the bits the caller cannot do).
     */
    /* Overlapping segments are not yet supported. Will use flags to deal with it if it becomes necessary. */
    RTUINTPTR   uRvaLast    = uRva + RT_MAX(cb, 1) - 1;
    RTUINTPTR   uRvaLastMax = uRvaLast;
    RTDBGSEGIDX iSeg        = pThis->cSegs;
    while (iSeg-- > 0)
    {
        RTUINTPTR uCurRva     = pThis->paSegs[iSeg].off;
        RTUINTPTR uCurRvaLast = uCurRva + RT_MAX(pThis->paSegs[iSeg].cb, 1) - 1;
        if (   uRva      <= uCurRvaLast
            && uRvaLast  >= uCurRva
            && (   /* HACK ALERT! Allow empty segments to share space (bios/watcom, elf). */
                   (cb != 0 && pThis->paSegs[iSeg].cb != 0)
                || (   cb == 0
                    && uRva != uCurRva
                    && uRva != uCurRvaLast)
                || (    pThis->paSegs[iSeg].cb == 0
                    && uCurRva != uRva
                    && uCurRva != uRvaLast)
               )
           )
            AssertMsgFailedReturn(("uRva=%RTptr uRvaLast=%RTptr (cb=%RTptr) \"%s\";\n"
                                   "uRva=%RTptr uRvaLast=%RTptr (cb=%RTptr) \"%s\" iSeg=%#x\n",
                                   uRva, uRvaLast, cb, pszName,
                                   uCurRva, uCurRvaLast, pThis->paSegs[iSeg].cb, pThis->paSegs[iSeg].pszName, iSeg),
                                  VERR_DBG_SEGMENT_INDEX_CONFLICT);
        if (uRvaLastMax < uCurRvaLast)
            uRvaLastMax = uCurRvaLast;
    }
    /* Strict ordered segment addition at the moment. */
    iSeg = pThis->cSegs;
    AssertMsgReturn(!piSeg || *piSeg == NIL_RTDBGSEGIDX || *piSeg == iSeg,
                    ("iSeg=%#x *piSeg=%#x\n", iSeg, *piSeg),
                    VERR_DBG_INVALID_SEGMENT_INDEX);

    /*
     * Add an entry to the segment table, extending it if necessary.
     */
    if (!(iSeg % 8))
    {
        void *pvSegs = RTMemRealloc(pThis->paSegs, sizeof(RTDBGMODCTNSEGMENT) * (iSeg + 8));
        if (!pvSegs)
            return VERR_NO_MEMORY;
        pThis->paSegs = (PRTDBGMODCTNSEGMENT)pvSegs;
    }

    pThis->paSegs[iSeg].SymAddrTree     = NULL;
    pThis->paSegs[iSeg].LineAddrTree    = NULL;
    pThis->paSegs[iSeg].off             = uRva;
    pThis->paSegs[iSeg].cb              = cb;
    pThis->paSegs[iSeg].fFlags          = fFlags;
    pThis->paSegs[iSeg].pszName         = RTStrCacheEnterN(g_hDbgModStrCache, pszName, cchName);
    if (pThis->paSegs[iSeg].pszName)
    {
        if (piSeg)
            *piSeg = iSeg;
        pThis->cSegs++;
        pThis->cb = uRvaLastMax + 1;
        if (!pThis->cb)
            pThis->cb = RTUINTPTR_MAX;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/** @copydoc RTDBGMODVTDBG::pfnImageSize */
static DECLCALLBACK(RTUINTPTR) rtDbgModContainer_ImageSize(PRTDBGMODINT pMod)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;
    return pThis->cb;
}


/** @copydoc RTDBGMODVTDBG::pfnRvaToSegOff */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModContainer_RvaToSegOff(PRTDBGMODINT pMod, RTUINTPTR uRva, PRTUINTPTR poffSeg)
{
    PRTDBGMODCTN          pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;
    PCRTDBGMODCTNSEGMENT  paSeg = pThis->paSegs;
    uint32_t const        cSegs = pThis->cSegs;
#if 0
    if (cSegs <= 7)
#endif
    {
        /*
         * Linear search.
         */
        for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
        {
            RTUINTPTR offSeg = uRva - paSeg[iSeg].off;
            if (offSeg < paSeg[iSeg].cb)
            {
                if (poffSeg)
                    *poffSeg = offSeg;
                return iSeg;
            }
        }
    }
#if 0 /** @todo binary search doesn't work if we've got empty segments in the list */
    else
    {
        /*
         * Binary search.
         */
        uint32_t iFirst = 0;
        uint32_t iLast  = cSegs - 1;
        for (;;)
        {
            uint32_t    iSeg   = iFirst + (iLast - iFirst) / 2;
            RTUINTPTR   offSeg = uRva - paSeg[iSeg].off;
            if (offSeg < paSeg[iSeg].cb)
            {
#if 0 /* Enable if we change the above test. */
                if (offSeg == 0 && paSeg[iSeg].cb == 0)
                    while (   iSeg > 0
                           && paSeg[iSeg - 1].cb  == 0
                           && paSeg[iSeg - 1].off == uRva)
                        iSeg--;
#endif

                if (poffSeg)
                    *poffSeg = offSeg;
                return iSeg;
            }

            /* advance */
            if (uRva < paSeg[iSeg].off)
            {
                /* between iFirst and iSeg. */
                if (iSeg == iFirst)
                    break;
                iLast = iSeg - 1;
            }
            else
            {
                /* between iSeg and iLast. paSeg[iSeg].cb == 0 ends up here too. */
                if (iSeg == iLast)
                    break;
                iFirst = iSeg + 1;
            }
        }
    }
#endif

    /* Invalid. */
    return NIL_RTDBGSEGIDX;
}


/** Destroy a line number node. */
static DECLCALLBACK(int)  rtDbgModContainer_DestroyTreeLineNode(PAVLU32NODECORE pNode, void *pvUser)
{
    PRTDBGMODCTN     pThis = (PRTDBGMODCTN)pvUser;
    PRTDBGMODCTNLINE pLine = RT_FROM_MEMBER(pNode, RTDBGMODCTNLINE, OrdinalCore);
    RTStrCacheRelease(g_hDbgModStrCache, pLine->pszFile);
    pLine->pszFile = NULL;
#ifdef RTDBGMODCNT_WITH_MEM_CACHE
    RTMemCacheFree(pThis->hLineNumAllocator, pLine);
#else
    RTMemFree(pLine); NOREF(pThis);
#endif
    return 0;
}


/** Destroy a symbol node. */
static DECLCALLBACK(int)  rtDbgModContainer_DestroyTreeNode(PAVLRUINTPTRNODECORE pNode, void *pvUser)
{
    PRTDBGMODCTNSYMBOL pSym = RT_FROM_MEMBER(pNode, RTDBGMODCTNSYMBOL, AddrCore);
    RTStrCacheRelease(g_hDbgModStrCache, pSym->NameCore.pszString);
    pSym->NameCore.pszString = NULL;

#if 0
    //PRTDBGMODCTN pThis = (PRTDBGMODCTN)pvUser;
    //PAVLU32NODECORE pRemoved = RTAvlU32Remove(&pThis->SymbolOrdinalTree, pSym->OrdinalCore.Key);
    //Assert(pRemoved == &pSym->OrdinalCore); RT_NOREF_PV(pRemoved);
#else
    RT_NOREF_PV(pvUser);
#endif

    RTMemFree(pSym);
    return 0;
}


/** @copydoc RTDBGMODVTDBG::pfnClose */
static DECLCALLBACK(int) rtDbgModContainer_Close(PRTDBGMODINT pMod)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    /*
     * Destroy the symbols and instance data.
     */
    for (uint32_t iSeg = 0; iSeg < pThis->cSegs; iSeg++)
    {
        RTAvlrUIntPtrDestroy(&pThis->paSegs[iSeg].SymAddrTree, rtDbgModContainer_DestroyTreeNode, pThis);
        RTStrCacheRelease(g_hDbgModStrCache, pThis->paSegs[iSeg].pszName);
        pThis->paSegs[iSeg].pszName = NULL;
    }

    RTAvlrUIntPtrDestroy(&pThis->AbsAddrTree, rtDbgModContainer_DestroyTreeNode, pThis);

    pThis->Names = NULL;

#ifdef RTDBGMODCNT_WITH_MEM_CACHE
    RTMemCacheDestroy(pThis->hLineNumAllocator);
    pThis->hLineNumAllocator = NIL_RTMEMCACHE;
#else
    RTAvlU32Destroy(&pThis->LineOrdinalTree, rtDbgModContainer_DestroyTreeLineNode, pThis);
#endif

    RTMemFree(pThis->paSegs);
    pThis->paSegs = NULL;

    RTMemFree(pThis);

    return VINF_SUCCESS;
}


/** @copydoc RTDBGMODVTDBG::pfnTryOpen */
static DECLCALLBACK(int) rtDbgModContainer_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    NOREF(pMod); NOREF(enmArch); RT_NOREF_PV(hDbgCfg);
    return VERR_INTERNAL_ERROR_5;
}



/** Virtual function table for the debug info container. */
DECL_HIDDEN_CONST(RTDBGMODVTDBG) const g_rtDbgModVtDbgContainer =
{
    /*.u32Magic = */            RTDBGMODVTDBG_MAGIC,
    /*.fSupports = */           0, /* (Don't call my TryOpen, please.) */
    /*.pszName = */             "container",
    /*.pfnTryOpen = */          rtDbgModContainer_TryOpen,
    /*.pfnClose = */            rtDbgModContainer_Close,

    /*.pfnRvaToSegOff = */      rtDbgModContainer_RvaToSegOff,
    /*.pfnImageSize = */        rtDbgModContainer_ImageSize,

    /*.pfnSegmentAdd = */       rtDbgModContainer_SegmentAdd,
    /*.pfnSegmentCount = */     rtDbgModContainer_SegmentCount,
    /*.pfnSegmentByIndex = */   rtDbgModContainer_SegmentByIndex,

    /*.pfnSymbolAdd = */        rtDbgModContainer_SymbolAdd,
    /*.pfnSymbolCount = */      rtDbgModContainer_SymbolCount,
    /*.pfnSymbolByOrdinal = */  rtDbgModContainer_SymbolByOrdinal,
    /*.pfnSymbolByName = */     rtDbgModContainer_SymbolByName,
    /*.pfnSymbolByAddr = */     rtDbgModContainer_SymbolByAddr,

    /*.pfnLineAdd = */          rtDbgModContainer_LineAdd,
    /*.pfnLineCount = */        rtDbgModContainer_LineCount,
    /*.pfnLineByOrdinal = */    rtDbgModContainer_LineByOrdinal,
    /*.pfnLineByAddr = */       rtDbgModContainer_LineByAddr,

    /*.pfnUnwindFrame = */      rtDbgModContainer_UnwindFrame,

    /*.u32EndMagic = */         RTDBGMODVTDBG_MAGIC
};



/**
 * Special container operation for removing all symbols.
 *
 * @returns IPRT status code.
 * @param   pMod        The module instance.
 */
DECLHIDDEN(int) rtDbgModContainer_SymbolRemoveAll(PRTDBGMODINT pMod)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    for (uint32_t iSeg = 0; iSeg < pThis->cSegs; iSeg++)
    {
        RTAvlrUIntPtrDestroy(&pThis->paSegs[iSeg].SymAddrTree, rtDbgModContainer_DestroyTreeNode, NULL);
        Assert(pThis->paSegs[iSeg].SymAddrTree == NULL);
    }

    RTAvlrUIntPtrDestroy(&pThis->AbsAddrTree, rtDbgModContainer_DestroyTreeNode, NULL);
    Assert(pThis->AbsAddrTree == NULL);

    pThis->Names = NULL;
    pThis->iNextSymbolOrdinal = 0;

    return VINF_SUCCESS;
}


/**
 * Special container operation for removing all line numbers.
 *
 * @returns IPRT status code.
 * @param   pMod        The module instance.
 */
DECLHIDDEN(int) rtDbgModContainer_LineRemoveAll(PRTDBGMODINT pMod)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    for (uint32_t iSeg = 0; iSeg < pThis->cSegs; iSeg++)
        pThis->paSegs[iSeg].LineAddrTree = NULL;

    RTAvlU32Destroy(&pThis->LineOrdinalTree, rtDbgModContainer_DestroyTreeLineNode, pThis);
    Assert(pThis->LineOrdinalTree == NULL);

    pThis->iNextLineOrdinal = 0;

    return VINF_SUCCESS;
}


/**
 * Special container operation for removing everything.
 *
 * @returns IPRT status code.
 * @param   pMod        The module instance.
 */
DECLHIDDEN(int) rtDbgModContainer_RemoveAll(PRTDBGMODINT pMod)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)pMod->pvDbgPriv;

    rtDbgModContainer_LineRemoveAll(pMod);
    rtDbgModContainer_SymbolRemoveAll(pMod);

    for (uint32_t iSeg = 0; iSeg < pThis->cSegs; iSeg++)
    {
        RTStrCacheRelease(g_hDbgModStrCache, pThis->paSegs[iSeg].pszName);
        pThis->paSegs[iSeg].pszName = NULL;
    }

    pThis->cSegs = 0;
    pThis->cb = 0;

    return VINF_SUCCESS;
}


/**
 * Creates a generic debug info container and associates it with the module.
 *
 * @returns IPRT status code.
 * @param   pMod        The module instance.
 * @param   cbSeg       The size of the initial segment. 0 if segments are to be
 *                      created manually later on.
 */
DECLHIDDEN(int) rtDbgModContainerCreate(PRTDBGMODINT pMod, RTUINTPTR cbSeg)
{
    PRTDBGMODCTN pThis = (PRTDBGMODCTN)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->Names = NULL;
    pThis->AbsAddrTree = NULL;
    pThis->SymbolOrdinalTree = NULL;
    pThis->LineOrdinalTree = NULL;
    pThis->paSegs = NULL;
    pThis->cSegs = 0;
    pThis->cb = 0;
    pThis->iNextSymbolOrdinal = 0;
    pThis->iNextLineOrdinal = 0;

    pMod->pDbgVt = &g_rtDbgModVtDbgContainer;
    pMod->pvDbgPriv = pThis;

#ifdef RTDBGMODCNT_WITH_MEM_CACHE
    int rc = RTMemCacheCreate(&pThis->hLineNumAllocator, sizeof(RTDBGMODCTNLINE), sizeof(void *), UINT32_MAX,
                              NULL /*pfnCtor*/, NULL /*pfnDtor*/, NULL /*pvUser*/, 0 /*fFlags*/);
#else
    int rc = VINF_SUCCESS;
#endif
    if (RT_SUCCESS(rc))
    {
        /*
         * Add the initial segment.
         */
        if (cbSeg)
            rc = rtDbgModContainer_SegmentAdd(pMod, 0, cbSeg, "default", sizeof("default") - 1, 0, NULL);
        if (RT_SUCCESS(rc))
            return rc;

#ifdef RTDBGMODCNT_WITH_MEM_CACHE
        RTMemCacheDestroy(pThis->hLineNumAllocator);
#endif
    }

    RTMemFree(pThis);
    pMod->pDbgVt = NULL;
    pMod->pvDbgPriv = NULL;
    return rc;
}

