/* $Id: dbgmodldr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Module Image Interpretation by RTLdr.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include "internal/dbgmod.h"
#include "internal/ldr.h"
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The instance data of the RTLdr based image reader.
 */
typedef struct RTDBGMODLDR
{
    /** Magic value (RTDBGMODLDR_MAGIC). */
    uint32_t        u32Magic;
    /** The loader handle. */
    RTLDRMOD        hLdrMod;
} RTDBGMODLDR;
/** Pointer to instance data NM map reader. */
typedef RTDBGMODLDR *PRTDBGMODLDR;



/** @interface_method_impl{RTDBGMODVTDBG,pfnUnwindFrame} */
static DECLCALLBACK(int) rtDbgModLdr_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrUnwindFrame(pThis->hLdrMod, NULL, iSeg, off, pState);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnQueryProp} */
static DECLCALLBACK(int) rtDbgModLdr_QueryProp(PRTDBGMODINT pMod, RTLDRPROP enmProp, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrQueryPropEx(pThis->hLdrMod, enmProp, NULL /*pvBits*/, pvBuf, cbBuf, pcbRet);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnGetArch} */
static DECLCALLBACK(RTLDRARCH) rtDbgModLdr_GetArch(PRTDBGMODINT pMod)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrGetArch(pThis->hLdrMod);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnGetFormat} */
static DECLCALLBACK(RTLDRFMT) rtDbgModLdr_GetFormat(PRTDBGMODINT pMod)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrGetFormat(pThis->hLdrMod);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnReadAt} */
static DECLCALLBACK(int) rtDbgModLdr_ReadAt(PRTDBGMODINT pMod, uint32_t iDbgInfoHint, RTFOFF off, void *pvBuf, size_t cb)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    RT_NOREF_PV(iDbgInfoHint);
    return rtLdrReadAt(pThis->hLdrMod, pvBuf, UINT32_MAX /** @todo iDbgInfo*/, off, cb);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnUnmapPart} */
static DECLCALLBACK(int) rtDbgModLdr_UnmapPart(PRTDBGMODINT pMod, size_t cb, void const **ppvMap)
{
    Assert(((PRTDBGMODLDR)pMod->pvImgPriv)->u32Magic == RTDBGMODLDR_MAGIC);
    NOREF(pMod); NOREF(cb);
    RTMemFree((void *)*ppvMap);
    *ppvMap = NULL;
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnMapPart} */
static DECLCALLBACK(int) rtDbgModLdr_MapPart(PRTDBGMODINT pMod, uint32_t iDbgInfo, RTFOFF off, size_t cb, void const **ppvMap)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);

    void *pvMap = RTMemAlloc(cb);
    if (!pvMap)
        return VERR_NO_MEMORY;

    int rc = rtLdrReadAt(pThis->hLdrMod, pvMap, iDbgInfo, off, cb);
    if (RT_SUCCESS(rc))
        *ppvMap = pvMap;
    else
    {
        RTMemFree(pvMap);
        *ppvMap = NULL;
    }
    return rc;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnImageSize} */
static DECLCALLBACK(RTUINTPTR) rtDbgModLdr_ImageSize(PRTDBGMODINT pMod)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrSize(pThis->hLdrMod);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnRvaToSegOffset} */
static DECLCALLBACK(int) rtDbgModLdr_RvaToSegOffset(PRTDBGMODINT pMod, RTLDRADDR Rva, PRTDBGSEGIDX piSeg, PRTLDRADDR poffSeg)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrRvaToSegOffset(pThis->hLdrMod, Rva, piSeg, poffSeg);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnLinkAddressToSegOffset} */
static DECLCALLBACK(int) rtDbgModLdr_LinkAddressToSegOffset(PRTDBGMODINT pMod, RTLDRADDR LinkAddress,
                                                            PRTDBGSEGIDX piSeg, PRTLDRADDR poffSeg)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrLinkAddressToSegOffset(pThis->hLdrMod, LinkAddress, piSeg, poffSeg);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnEnumSymbols} */
static DECLCALLBACK(int) rtDbgModLdr_EnumSymbols(PRTDBGMODINT pMod, uint32_t fFlags, RTLDRADDR BaseAddress,
                                                 PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrEnumSymbols(pThis->hLdrMod, fFlags, NULL /*pvBits*/, BaseAddress, pfnCallback, pvUser);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnEnumSegments} */
static DECLCALLBACK(int) rtDbgModLdr_EnumSegments(PRTDBGMODINT pMod, PFNRTLDRENUMSEGS pfnCallback, void *pvUser)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrEnumSegments(pThis->hLdrMod, pfnCallback, pvUser);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnEnumDbgInfo} */
static DECLCALLBACK(int) rtDbgModLdr_EnumDbgInfo(PRTDBGMODINT pMod, PFNRTLDRENUMDBG pfnCallback, void *pvUser)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);
    return RTLdrEnumDbgInfo(pThis->hLdrMod, NULL, pfnCallback, pvUser);
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnClose} */
static DECLCALLBACK(int) rtDbgModLdr_Close(PRTDBGMODINT pMod)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)pMod->pvImgPriv;
    AssertPtr(pThis);
    Assert(pThis->u32Magic == RTDBGMODLDR_MAGIC);

    int rc = RTLdrClose(pThis->hLdrMod); AssertRC(rc);
    pThis->hLdrMod  = NIL_RTLDRMOD;
    pThis->u32Magic = RTDBGMODLDR_MAGIC_DEAD;

    RTMemFree(pThis);

    return VINF_SUCCESS;
}


/** @interface_method_impl{RTDBGMODVTIMG,pfnTryOpen} */
static DECLCALLBACK(int) rtDbgModLdr_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, uint32_t fLdrFlags)
{
    RTLDRMOD hLdrMod;
    int rc = RTLdrOpen(pMod->pszImgFile, RTLDR_O_FOR_DEBUG | fLdrFlags, enmArch, &hLdrMod);
    if (RT_SUCCESS(rc))
    {
        rc = rtDbgModLdrOpenFromHandle(pMod, hLdrMod);
        if (RT_FAILURE(rc))
            RTLdrClose(hLdrMod);
    }
    return rc;
}


/** Virtual function table for the RTLdr based image reader. */
DECL_HIDDEN_CONST(RTDBGMODVTIMG) const g_rtDbgModVtImgLdr =
{
    /*.u32Magic = */                    RTDBGMODVTIMG_MAGIC,
    /*.fReserved = */                   0,
    /*.pszName = */                     "RTLdr",
    /*.pfnTryOpen = */                  rtDbgModLdr_TryOpen,
    /*.pfnClose = */                    rtDbgModLdr_Close,
    /*.pfnEnumDbgInfo = */              rtDbgModLdr_EnumDbgInfo,
    /*.pfnEnumSegments = */             rtDbgModLdr_EnumSegments,
    /*.pfnEnumSymbols = */              rtDbgModLdr_EnumSymbols,
    /*.pfnImageSize = */                rtDbgModLdr_ImageSize,
    /*.pfnLinkAddressToSegOffset = */   rtDbgModLdr_LinkAddressToSegOffset,
    /*.pfnRvaToSegOffset= */            rtDbgModLdr_RvaToSegOffset,
    /*.pfnMapPart = */                  rtDbgModLdr_MapPart,
    /*.pfnUnmapPart = */                rtDbgModLdr_UnmapPart,
    /*.pfnReadAt = */                   rtDbgModLdr_ReadAt,
    /*.pfnGetFormat = */                rtDbgModLdr_GetFormat,
    /*.pfnGetArch = */                  rtDbgModLdr_GetArch,
    /*.pfnQueryProp = */                rtDbgModLdr_QueryProp,
    /*.pfnUnwindFrame = */              rtDbgModLdr_UnwindFrame,

    /*.u32EndMagic = */                 RTDBGMODVTIMG_MAGIC
};


/**
 * Open PE-image trick.
 *
 * @returns IPRT status code
 * @param   pDbgMod             The debug module instance.
 * @param   hLdrMod             The module to open a image debug backend for.
 */
DECLHIDDEN(int) rtDbgModLdrOpenFromHandle(PRTDBGMODINT pDbgMod, RTLDRMOD hLdrMod)
{
    PRTDBGMODLDR pThis = (PRTDBGMODLDR)RTMemAllocZ(sizeof(RTDBGMODLDR));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic    = RTDBGMODLDR_MAGIC;
    pThis->hLdrMod     = hLdrMod;
    pDbgMod->pvImgPriv = pThis;
    return VINF_SUCCESS;
}

