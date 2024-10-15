/* $Id: dbgmoddeferred.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Module Deferred Loading Stub.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <iprt/dbg.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include "internal/dbgmod.h"
#include "internal/magics.h"



/**
 * Releases the instance data.
 *
 * @param   pThis               The instance data.
 */
static void rtDbgModDeferredReleaseInstanceData(PRTDBGMODDEFERRED pThis)
{
    AssertPtr(pThis);
    Assert(pThis->u32Magic == RTDBGMODDEFERRED_MAGIC);
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs); Assert(cRefs < 8);
    if (!cRefs)
    {
        RTDbgCfgRelease(pThis->hDbgCfg);
        pThis->hDbgCfg = NIL_RTDBGCFG;
        pThis->u32Magic = RTDBGMODDEFERRED_MAGIC_DEAD;
        RTMemFree(pThis);
    }
}


/**
 * Does the deferred loading of the real data (image and/or debug info).
 *
 * @returns VINF_SUCCESS or VERR_DBG_DEFERRED_LOAD_FAILED.
 * @param   pMod                The generic module instance data.
 * @param   fForcedRetry        Whether it's a forced retry by one of the
 *                              pfnTryOpen methods.
 */
static int rtDbgModDeferredDoIt(PRTDBGMODINT pMod, bool fForcedRetry)
{
    RTCritSectEnter(&pMod->CritSect);

    int rc;
    if (!pMod->fDeferredFailed || fForcedRetry)
    {
        bool const fDbgVt = pMod->pDbgVt == &g_rtDbgModVtDbgDeferred;
        bool const fImgVt = pMod->pImgVt == &g_rtDbgModVtImgDeferred;
        AssertReturnStmt(fDbgVt || fImgVt, RTCritSectLeave(&pMod->CritSect), VERR_INTERNAL_ERROR_5);

        PRTDBGMODDEFERRED pThis = (PRTDBGMODDEFERRED)(fDbgVt ? pMod->pvDbgPriv : pMod->pvImgPriv);

        /* Reset the method tables and private data pointes so the deferred loading
           procedure can figure out what to do and won't get confused. */
        if (fDbgVt)
        {
            pMod->pvDbgPriv = NULL;
            pMod->pDbgVt    = NULL;
        }

        if (fImgVt)
        {
            pMod->pvImgPriv = NULL;
            pMod->pImgVt    = NULL;
        }

        /* Do the deferred loading. */
        rc = pThis->pfnDeferred(pMod, pThis);
        if (RT_SUCCESS(rc))
        {
            Assert(!fDbgVt || pMod->pDbgVt != NULL);
            Assert(!fImgVt || pMod->pImgVt != NULL);

            pMod->fDeferred       = false;
            pMod->fDeferredFailed = false;

            rtDbgModDeferredReleaseInstanceData(pThis);
            if (fImgVt && fDbgVt)
                rtDbgModDeferredReleaseInstanceData(pThis);
        }
        else
        {
            /* Failed, bail out and restore the deferred setup. */
            pMod->fDeferredFailed = true;

            if (fDbgVt)
            {
                Assert(!pMod->pDbgVt);
                pMod->pDbgVt    = &g_rtDbgModVtDbgDeferred;
                pMod->pvDbgPriv = pThis;
            }

            if (fImgVt)
            {
                Assert(!pMod->pImgVt);
                pMod->pImgVt    = &g_rtDbgModVtImgDeferred;
                pMod->pvImgPriv = pThis;
            }
        }
    }
    else
        rc = VERR_DBG_DEFERRED_LOAD_FAILED;

    RTCritSectLeave(&pMod->CritSect);
    return rc;
}




/*
 *
 * D e b u g   I n f o   M e t h o d s
 * D e b u g   I n f o   M e t h o d s
 * D e b u g   I n f o   M e t h o d s
 *
 */

/** @interface_method_impl{RTDBGMODVTDBG,pfnUnwindFrame} */
static DECLCALLBACK(int)
rtDbgModDeferredDbg_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnUnwindFrame(pMod, iSeg, off, pState);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByAddr} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_LineByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                                        PRTINTPTR poffDisp, PRTDBGLINE pLineInfo)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnLineByAddr(pMod, iSeg, off, poffDisp, pLineInfo);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByOrdinal} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_LineByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnLineByOrdinal(pMod, iOrdinal, pLineInfo);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineCount} */
static DECLCALLBACK(uint32_t) rtDbgModDeferredDbg_LineCount(PRTDBGMODINT pMod)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        return pMod->pDbgVt->pfnLineCount(pMod);
    return 0;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineAdd} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_LineAdd(PRTDBGMODINT pMod, const char *pszFile, size_t cchFile, uint32_t uLineNo,
                                                     uint32_t iSeg, RTUINTPTR off, uint32_t *piOrdinal)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnLineAdd(pMod, pszFile, cchFile, uLineNo, iSeg, off, piOrdinal);
    return rc;
}


/**
 * Fill in symbol info for the fake start symbol.
 *
 * @returns VINF_SUCCESS
 * @param   pThis               The deferred load data.
 * @param   pSymInfo            The output structure.
 */
static int rtDbgModDeferredDbgSymInfo_Start(PRTDBGMODDEFERRED pThis, PRTDBGSYMBOL pSymInfo)
{
    pSymInfo->Value     = 0;
    pSymInfo->cb        = pThis->cbImage;
    pSymInfo->offSeg    = 0;
    pSymInfo->iSeg      = 0;
    pSymInfo->fFlags    = 0;
    pSymInfo->iOrdinal  = 0;
    strcpy(pSymInfo->szName, "DeferredStart");
    return VINF_SUCCESS;
}


/**
 * Fill in symbol info for the fake last symbol.
 *
 * @returns VINF_SUCCESS
 * @param   pThis               The deferred load data.
 * @param   pSymInfo            The output structure.
 */
static int rtDbgModDeferredDbgSymInfo_Last(PRTDBGMODDEFERRED pThis, PRTDBGSYMBOL pSymInfo)
{
    pSymInfo->Value     = pThis->cbImage - 1;
    pSymInfo->cb        = 0;
    pSymInfo->offSeg    = pThis->cbImage - 1;
    pSymInfo->iSeg      = 0;
    pSymInfo->fFlags    = 0;
    pSymInfo->iOrdinal  = 1;
    strcpy(pSymInfo->szName, "DeferredLast");
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByAddr} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_SymbolByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                                          PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    if (   (fFlags & RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED)
        && iSeg == RTDBGSEGIDX_ABS)
        return VERR_SYMBOL_NOT_FOUND;

    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnSymbolByAddr(pMod, iSeg, off, fFlags, poffDisp, pSymInfo);
    else
    {
        PRTDBGMODDEFERRED pThis = (PRTDBGMODDEFERRED)pMod->pvDbgPriv;
        if (off == 0)
            rc = rtDbgModDeferredDbgSymInfo_Start(pThis, pSymInfo);
        else if (off >= pThis->cbImage - 1 || (fFlags & RTDBGSYMADDR_FLAGS_GREATER_OR_EQUAL))
            rc = rtDbgModDeferredDbgSymInfo_Last(pThis, pSymInfo);
        else
            rc = rtDbgModDeferredDbgSymInfo_Start(pThis, pSymInfo);
        if (poffDisp)
            *poffDisp = off - pSymInfo->offSeg;
    }
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByName} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_SymbolByName(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                          PRTDBGSYMBOL pSymInfo)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnSymbolByName(pMod, pszSymbol, cchSymbol, pSymInfo);
    else
    {
        PRTDBGMODDEFERRED pThis = (PRTDBGMODDEFERRED)pMod->pvDbgPriv;
        if (   cchSymbol             == sizeof("DeferredStart") - 1
             && !memcmp(pszSymbol, RT_STR_TUPLE("DeferredStart")))
            rc = rtDbgModDeferredDbgSymInfo_Start(pThis, pSymInfo);
        else if (   cchSymbol             == sizeof("DeferredLast") - 1
                 && !memcmp(pszSymbol, RT_STR_TUPLE("DeferredLast")))
            rc = rtDbgModDeferredDbgSymInfo_Last(pThis, pSymInfo);
        else
            rc = VERR_SYMBOL_NOT_FOUND;
    }
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByOrdinal} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_SymbolByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnSymbolByOrdinal(pMod, iOrdinal, pSymInfo);
    else
    {
        PRTDBGMODDEFERRED pThis = (PRTDBGMODDEFERRED)pMod->pvDbgPriv;
        if (iOrdinal == 0)
            rc = rtDbgModDeferredDbgSymInfo_Start(pThis, pSymInfo);
        else if (iOrdinal == 1)
            rc = rtDbgModDeferredDbgSymInfo_Last(pThis, pSymInfo);
        else
            rc = VERR_SYMBOL_NOT_FOUND;
    }
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolCount} */
static DECLCALLBACK(uint32_t) rtDbgModDeferredDbg_SymbolCount(PRTDBGMODINT pMod)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        return pMod->pDbgVt->pfnSymbolCount(pMod);
    return 2;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolAdd} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_SymbolAdd(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                       RTDBGSEGIDX iSeg, RTUINTPTR off, RTUINTPTR cb, uint32_t fFlags,
                                                       uint32_t *piOrdinal)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnSymbolAdd(pMod, pszSymbol, cchSymbol, iSeg, off, cb, fFlags, piOrdinal);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentByIndex} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_SegmentByIndex(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnSegmentByIndex(pMod, iSeg, pSegInfo);
    else if (iSeg == 0)
    {
        PRTDBGMODDEFERRED pThis = (PRTDBGMODDEFERRED)pMod->pvDbgPriv;
        pSegInfo->Address   = 0;
        pSegInfo->uRva      = 0;
        pSegInfo->cb        = pThis->cbImage;
        pSegInfo->fFlags    = 0;
        pSegInfo->iSeg      = 0;
        memcpy(pSegInfo->szName, RT_STR_TUPLE("LATER"));
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_DBG_INVALID_SEGMENT_INDEX;
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentCount} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModDeferredDbg_SegmentCount(PRTDBGMODINT pMod)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        return pMod->pDbgVt->pfnSegmentCount(pMod);
    return 1;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentAdd} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_SegmentAdd(PRTDBGMODINT pMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName,
                                                        size_t cchName, uint32_t fFlags, PRTDBGSEGIDX piSeg)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pDbgVt->pfnSegmentAdd(pMod, uRva, cb, pszName, cchName, fFlags, piSeg);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnImageSize} */
static DECLCALLBACK(RTUINTPTR) rtDbgModDeferredDbg_ImageSize(PRTDBGMODINT pMod)
{
    PRTDBGMODDEFERRED pThis = (PRTDBGMODDEFERRED)pMod->pvDbgPriv;
    Assert(pThis->u32Magic == RTDBGMODDEFERRED_MAGIC);
    return pThis->cbImage;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnRvaToSegOff} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModDeferredDbg_RvaToSegOff(PRTDBGMODINT pMod, RTUINTPTR uRva, PRTUINTPTR poffSeg)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvDbgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        return pMod->pDbgVt->pfnRvaToSegOff(pMod, uRva, poffSeg);
    return 0;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnClose} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_Close(PRTDBGMODINT pMod)
{
    rtDbgModDeferredReleaseInstanceData((PRTDBGMODDEFERRED)pMod->pvDbgPriv);
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnTryOpen} */
static DECLCALLBACK(int) rtDbgModDeferredDbg_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    NOREF(enmArch); RT_NOREF_PV(hDbgCfg);
    return rtDbgModDeferredDoIt(pMod, true /*fForceRetry*/);
}



/** Virtual function table for the deferred debug info reader. */
DECL_HIDDEN_CONST(RTDBGMODVTDBG) const g_rtDbgModVtDbgDeferred =
{
    /*.u32Magic = */            RTDBGMODVTDBG_MAGIC,
    /*.fSupports = */           RT_DBGTYPE_MAP,
    /*.pszName = */             "deferred",
    /*.pfnTryOpen = */          rtDbgModDeferredDbg_TryOpen,
    /*.pfnClose = */            rtDbgModDeferredDbg_Close,

    /*.pfnRvaToSegOff = */      rtDbgModDeferredDbg_RvaToSegOff,
    /*.pfnImageSize = */        rtDbgModDeferredDbg_ImageSize,

    /*.pfnSegmentAdd = */       rtDbgModDeferredDbg_SegmentAdd,
    /*.pfnSegmentCount = */     rtDbgModDeferredDbg_SegmentCount,
    /*.pfnSegmentByIndex = */   rtDbgModDeferredDbg_SegmentByIndex,

    /*.pfnSymbolAdd = */        rtDbgModDeferredDbg_SymbolAdd,
    /*.pfnSymbolCount = */      rtDbgModDeferredDbg_SymbolCount,
    /*.pfnSymbolByOrdinal = */  rtDbgModDeferredDbg_SymbolByOrdinal,
    /*.pfnSymbolByName = */     rtDbgModDeferredDbg_SymbolByName,
    /*.pfnSymbolByAddr = */     rtDbgModDeferredDbg_SymbolByAddr,

    /*.pfnLineAdd = */          rtDbgModDeferredDbg_LineAdd,
    /*.pfnLineCount = */        rtDbgModDeferredDbg_LineCount,
    /*.pfnLineByOrdinal = */    rtDbgModDeferredDbg_LineByOrdinal,
    /*.pfnLineByAddr = */       rtDbgModDeferredDbg_LineByAddr,

    /*.pfnUnwindFrame = */      rtDbgModDeferredDbg_UnwindFrame,

    /*.u32EndMagic = */         RTDBGMODVTDBG_MAGIC
};




/*
 *
 * I m a g e   M e t h o d s
 * I m a g e   M e t h o d s
 * I m a g e   M e t h o d s
 *
 */

/** @interface_method_impl{RTDBGMODVTIMG,pfnUnwindFrame} */
static DECLCALLBACK(int)
rtDbgModDeferredImg_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnUnwindFrame(pMod, iSeg, off, pState);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnQueryProp} */
static DECLCALLBACK(int)
rtDbgModDeferredImg_QueryProp(PRTDBGMODINT pMod, RTLDRPROP enmProp, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnQueryProp(pMod, enmProp, pvBuf, cbBuf, pcbRet);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnGetArch} */
static DECLCALLBACK(RTLDRARCH) rtDbgModDeferredImg_GetArch(PRTDBGMODINT pMod)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);

    RTLDRARCH enmArch;
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        enmArch = pMod->pImgVt->pfnGetArch(pMod);
    else
        enmArch = RTLDRARCH_WHATEVER;
    return enmArch;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnGetFormat} */
static DECLCALLBACK(RTLDRFMT) rtDbgModDeferredImg_GetFormat(PRTDBGMODINT pMod)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);

    RTLDRFMT enmFmt;
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        enmFmt = pMod->pImgVt->pfnGetFormat(pMod);
    else
        enmFmt = RTLDRFMT_INVALID;
    return enmFmt;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnReadAt} */
static DECLCALLBACK(int) rtDbgModDeferredImg_ReadAt(PRTDBGMODINT pMod, uint32_t iDbgInfoHint, RTFOFF off, void *pvBuf, size_t cb)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnReadAt(pMod, iDbgInfoHint, off, pvBuf, cb);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnUnmapPart} */
static DECLCALLBACK(int) rtDbgModDeferredImg_UnmapPart(PRTDBGMODINT pMod, size_t cb, void const **ppvMap)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnUnmapPart(pMod, cb, ppvMap);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnMapPart} */
static DECLCALLBACK(int) rtDbgModDeferredImg_MapPart(PRTDBGMODINT pMod, uint32_t iDbgInfo, RTFOFF off, size_t cb, void const **ppvMap)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnMapPart(pMod, iDbgInfo, off, cb, ppvMap);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnImageSize} */
static DECLCALLBACK(RTUINTPTR) rtDbgModDeferredImg_ImageSize(PRTDBGMODINT pMod)
{
    PRTDBGMODDEFERRED pThis = (PRTDBGMODDEFERRED)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODDEFERRED_MAGIC);
    return pThis->cbImage;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnRvaToSegOffset} */
static DECLCALLBACK(int) rtDbgModDeferredImg_RvaToSegOffset(PRTDBGMODINT pMod, RTLDRADDR Rva,
                                                            PRTDBGSEGIDX piSeg, PRTLDRADDR poffSeg)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnRvaToSegOffset(pMod, Rva, piSeg, poffSeg);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnLinkAddressToSegOffset} */
static DECLCALLBACK(int) rtDbgModDeferredImg_LinkAddressToSegOffset(PRTDBGMODINT pMod, RTLDRADDR LinkAddress,
                                                                    PRTDBGSEGIDX piSeg, PRTLDRADDR poffSeg)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnLinkAddressToSegOffset(pMod, LinkAddress, piSeg, poffSeg);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnEnumSymbols} */
static DECLCALLBACK(int) rtDbgModDeferredImg_EnumSymbols(PRTDBGMODINT pMod, uint32_t fFlags, RTLDRADDR BaseAddress,
                                                         PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnEnumSymbols(pMod, fFlags, BaseAddress, pfnCallback, pvUser);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnEnumSegments} */
static DECLCALLBACK(int) rtDbgModDeferredImg_EnumSegments(PRTDBGMODINT pMod, PFNRTLDRENUMSEGS pfnCallback, void *pvUser)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnEnumSegments(pMod, pfnCallback, pvUser);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnEnumDbgInfo} */
static DECLCALLBACK(int) rtDbgModDeferredImg_EnumDbgInfo(PRTDBGMODINT pMod, PFNRTLDRENUMDBG pfnCallback, void *pvUser)
{
    Assert(((PRTDBGMODDEFERRED)pMod->pvImgPriv)->u32Magic == RTDBGMODDEFERRED_MAGIC);
    int rc = rtDbgModDeferredDoIt(pMod, false /*fForceRetry*/);
    if (RT_SUCCESS(rc))
        rc = pMod->pImgVt->pfnEnumDbgInfo(pMod, pfnCallback, pvUser);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnClose} */
static DECLCALLBACK(int) rtDbgModDeferredImg_Close(PRTDBGMODINT pMod)
{
    rtDbgModDeferredReleaseInstanceData((PRTDBGMODDEFERRED)pMod->pvImgPriv);
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnTryOpen} */
static DECLCALLBACK(int) rtDbgModDeferredImg_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, uint32_t fLdrFlags)
{
    RT_NOREF(enmArch, fLdrFlags);
    return rtDbgModDeferredDoIt(pMod, true /*fForceRetry*/);
}


/** Virtual function table for the RTLdr based image reader. */
DECL_HIDDEN_CONST(RTDBGMODVTIMG) const g_rtDbgModVtImgDeferred =
{
    /*.u32Magic = */                    RTDBGMODVTIMG_MAGIC,
    /*.fReserved = */                   0,
    /*.pszName = */                     "deferred",
    /*.pfnTryOpen = */                  rtDbgModDeferredImg_TryOpen,
    /*.pfnClose = */                    rtDbgModDeferredImg_Close,
    /*.pfnEnumDbgInfo = */              rtDbgModDeferredImg_EnumDbgInfo,
    /*.pfnEnumSegments = */             rtDbgModDeferredImg_EnumSegments,
    /*.pfnEnumSymbols = */              rtDbgModDeferredImg_EnumSymbols,
    /*.pfnGetLoadedSize = */            rtDbgModDeferredImg_ImageSize,
    /*.pfnLinkAddressToSegOffset = */   rtDbgModDeferredImg_LinkAddressToSegOffset,
    /*.pfnRvaToSegOffset = */           rtDbgModDeferredImg_RvaToSegOffset,
    /*.pfnMapPart = */                  rtDbgModDeferredImg_MapPart,
    /*.pfnUnmapPart = */                rtDbgModDeferredImg_UnmapPart,
    /*.pfnReadAt = */                   rtDbgModDeferredImg_ReadAt,
    /*.pfnGetFormat = */                rtDbgModDeferredImg_GetFormat,
    /*.pfnGetArch = */                  rtDbgModDeferredImg_GetArch,
    /*.pfnQueryProp = */                rtDbgModDeferredImg_QueryProp,
    /*.pfnUnwindFrame = */              rtDbgModDeferredImg_UnwindFrame,

    /*.u32EndMagic = */                 RTDBGMODVTIMG_MAGIC
};


/**
 * Creates a deferred loading stub for both image and debug info.
 *
 * @returns IPRT status code.
 * @param   pDbgMod             The debug module.
 * @param   pfnDeferred         The callback that will try load the image and
 *                              debug info.
 * @param   cbImage             The size of the image.
 * @param   hDbgCfg             The debug config handle.  Can be NIL.  A
 *                              reference will be retained.
 * @param   cbDeferred          The size of the deferred instance data, 0 if the
 *                              default structure is good enough.
 * @param   fFlags              RTDBGMOD_F_XXX.
 * @param   ppDeferred          Where to return the instance data. Can be NULL.
 */
DECLHIDDEN(int) rtDbgModDeferredCreate(PRTDBGMODINT pDbgMod, PFNRTDBGMODDEFERRED pfnDeferred, RTUINTPTR cbImage,
                                       RTDBGCFG hDbgCfg, size_t cbDeferred, uint32_t fFlags, PRTDBGMODDEFERRED *ppDeferred)
{
    AssertReturn(!pDbgMod->pDbgVt, VERR_DBG_MOD_IPE);

    if (cbDeferred < sizeof(RTDBGMODDEFERRED))
        cbDeferred = sizeof(RTDBGMODDEFERRED);
    PRTDBGMODDEFERRED pDeferred = (PRTDBGMODDEFERRED)RTMemAllocZ(cbDeferred);
    if (!pDeferred)
        return VERR_NO_MEMORY;

    pDeferred->u32Magic    = RTDBGMODDEFERRED_MAGIC;
    pDeferred->cRefs       = 1 + (pDbgMod->pImgVt == NULL);
    pDeferred->cbImage     = cbImage;
    if (hDbgCfg != NIL_RTDBGCFG)
        RTDbgCfgRetain(hDbgCfg);
    pDeferred->hDbgCfg     = hDbgCfg;
    pDeferred->pfnDeferred = pfnDeferred;
    pDeferred->fFlags      = fFlags;

    pDbgMod->pDbgVt             = &g_rtDbgModVtDbgDeferred;
    pDbgMod->pvDbgPriv          = pDeferred;
    if (!pDbgMod->pImgVt)
    {
        pDbgMod->pImgVt         = &g_rtDbgModVtImgDeferred;
        pDbgMod->pvImgPriv      = pDeferred;
    }
    pDbgMod->fDeferred          = true;
    pDbgMod->fDeferredFailed    = false;

    if (ppDeferred)
        *ppDeferred = pDeferred;
    return VINF_SUCCESS;
}

