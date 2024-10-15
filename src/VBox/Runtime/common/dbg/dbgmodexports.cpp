/* $Id: dbgmodexports.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Module Using Image Exports.
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
#define LOG_GROUP RTLOGGROUP_DBG
#include <iprt/dbg.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include "internal/dbgmod.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTDBGMODEXPORTARGS
{
    PRTDBGMODINT    pDbgMod;
    RTLDRADDR       uImageBase;
    RTLDRADDR       uRvaNext;
    uint32_t        cSegs;
} RTDBGMODEXPORTARGS;
/** Pointer to an argument package. */
typedef RTDBGMODEXPORTARGS *PRTDBGMODEXPORTARGS;


/**
 * @callback_method_impl{FNRTLDRENUMSYMS,
 *      Copies the symbols over into the container} */
static DECLCALLBACK(int) rtDbgModExportsAddSymbolCallback(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol,
                                                          RTLDRADDR Value, void *pvUser)
{
    PRTDBGMODEXPORTARGS pArgs = (PRTDBGMODEXPORTARGS)pvUser;
    NOREF(hLdrMod);

    if (Value >= pArgs->uImageBase)
    {
        char szOrdinalNm[48];
        if (!pszSymbol || *pszSymbol == '\0')
        {
            RTStrPrintf(szOrdinalNm, sizeof(szOrdinalNm), "Ordinal%u", uSymbol);
            pszSymbol = szOrdinalNm;
        }

        int rc = RTDbgModSymbolAdd(pArgs->pDbgMod, pszSymbol, RTDBGSEGIDX_RVA, Value - pArgs->uImageBase,
                                   0 /*cb*/, 0 /* fFlags */, NULL /*piOrdinal*/);
        Log(("Symbol #%05u %#018RTptr %s [%Rrc]\n", uSymbol, Value, pszSymbol, rc)); NOREF(rc);
    }
    else
        Log(("Symbol #%05u %#018RTptr %s [SKIPPED - INVALID ADDRESS]\n", uSymbol, Value, pszSymbol));
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTLDRENUMSEGS,
 *      Copies the segments over into the container} */
static DECLCALLBACK(int) rtDbgModExportsAddSegmentsCallback(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    PRTDBGMODEXPORTARGS pArgs = (PRTDBGMODEXPORTARGS)pvUser;
    Log(("Segment %.*s: LinkAddress=%#llx RVA=%#llx cb=%#llx\n",
         pSeg->cchName, pSeg->pszName, (uint64_t)pSeg->LinkAddress, (uint64_t)pSeg->RVA, pSeg->cb));
    NOREF(hLdrMod);

    pArgs->cSegs++;

    /* Add dummy segments for segments that doesn't get mapped. */
    if (   pSeg->LinkAddress == NIL_RTLDRADDR
        || pSeg->RVA         == NIL_RTLDRADDR)
        return RTDbgModSegmentAdd(pArgs->pDbgMod, 0, 0, pSeg->pszName, 0 /*fFlags*/, NULL);

    RTLDRADDR uRva = pSeg->RVA;
#if 0 /* Kluge for the .data..percpu segment in 64-bit linux kernels and similar. (Moved to ELF.) */
    if (uRva < pArgs->uRvaNext)
        uRva = RT_ALIGN_T(pArgs->uRvaNext, pSeg->Alignment, RTLDRADDR);
#endif

    /* Find the best base address for the module. */
    if (   (  !pArgs->uImageBase
            || pArgs->uImageBase > pSeg->LinkAddress)
        && (   pSeg->LinkAddress != 0 /* .data..percpu again. */
            || pArgs->cSegs == 1))
        pArgs->uImageBase = pSeg->LinkAddress;

    /* Add it. */
    RTLDRADDR cb = RT_MAX(pSeg->cb, pSeg->cbMapped);
    pArgs->uRvaNext = uRva + cb;
    return RTDbgModSegmentAdd(pArgs->pDbgMod, uRva, cb, pSeg->pszName, 0 /*fFlags*/, NULL);
}


/**
 * Creates the debug info side of affairs based on exports and segments found in
 * the image part.
 *
 * The image part must be successfully initialized prior to the call, while the
 * debug bits must not be present of course.
 *
 * @returns IPRT status code
 * @param   pDbgMod             The debug module structure.
 */
DECLHIDDEN(int) rtDbgModCreateForExports(PRTDBGMODINT pDbgMod)
{
    AssertReturn(!pDbgMod->pDbgVt, VERR_DBG_MOD_IPE);
    AssertReturn(pDbgMod->pImgVt, VERR_DBG_MOD_IPE);
    RTUINTPTR cbImage = pDbgMod->pImgVt->pfnImageSize(pDbgMod);
    AssertReturn(cbImage > 0, VERR_DBG_MOD_IPE);

    /*
     * We simply use a container type for this work.
     */
    int rc = rtDbgModContainerCreate(pDbgMod, 0);
    if (RT_FAILURE(rc))
        return rc;
    pDbgMod->fExports = true;

    /*
     * Copy the segments and symbols.
     */
    RTDBGMODEXPORTARGS Args;
    Args.pDbgMod    = pDbgMod;
    Args.uImageBase = 0;
    Args.uRvaNext   = 0;
    Args.cSegs      = 0;
    rc = pDbgMod->pImgVt->pfnEnumSegments(pDbgMod, rtDbgModExportsAddSegmentsCallback, &Args);
    if (RT_SUCCESS(rc))
    {
        rc = pDbgMod->pImgVt->pfnEnumSymbols(pDbgMod, RTLDR_ENUM_SYMBOL_FLAGS_ALL | RTLDR_ENUM_SYMBOL_FLAGS_NO_FWD,
                                             Args.uImageBase ? Args.uImageBase : 0x10000,
                                             rtDbgModExportsAddSymbolCallback, &Args);
        if (RT_FAILURE(rc))
            Log(("rtDbgModCreateForExports: Error during symbol enum: %Rrc\n", rc));
    }
    else
        Log(("rtDbgModCreateForExports: Error during segment enum: %Rrc\n", rc));

    /* This won't fail. */
    if (RT_SUCCESS(rc))
        rc = VINF_SUCCESS;
    else
        rc = -rc; /* Change it into a warning. */
    return rc;
}

