/* $Id: dbgmoddbghelp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Info Reader Using DbgHelp.dll if Present.
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
#define LOG_GROUP   RTLOGGROUP_DBG
#include <iprt/dbg.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include "internal/dbgmod.h"

#include <iprt/win/windows.h>
#include <iprt/win/dbghelp.h>
#include <iprt/win/lazy-dbghelp.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** For passing arguments to DbgHelp.dll callback. */
typedef struct RTDBGMODBGHELPARGS
{
    RTDBGMOD        hCnt;
    PRTDBGMODINT    pMod;
    uint64_t        uModAddr;
    RTLDRADDR       uNextRva;

    /** UTF-8 version of the previous file name. */
    char           *pszPrev;
    /** Copy of the previous file name. */
    PRTUTF16        pwszPrev;
    /** Number of bytes pwszPrev points to. */
    size_t          cbPrevUtf16Alloc;
} RTDBGMODBGHELPARGS;


/** @interface_method_impl{RTDBGMODVTDBG,pfnUnwindFrame} */
static DECLCALLBACK(int) rtDbgModDbgHelp_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    RT_NOREF(pMod, iSeg, off, pState);
    return VERR_DBG_NO_UNWIND_INFO;
}



/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByAddr} */
static DECLCALLBACK(int) rtDbgModDbgHelp_LineByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                                  PRTINTPTR poffDisp, PRTDBGLINE pLineInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModLineByAddr(hCnt, iSeg, off, poffDisp, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByOrdinal} */
static DECLCALLBACK(int) rtDbgModDbgHelp_LineByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModLineByOrdinal(hCnt, iOrdinal, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineCount} */
static DECLCALLBACK(uint32_t) rtDbgModDbgHelp_LineCount(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModLineCount(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineAdd} */
static DECLCALLBACK(int) rtDbgModDbgHelp_LineAdd(PRTDBGMODINT pMod, const char *pszFile, size_t cchFile, uint32_t uLineNo,
                                                 uint32_t iSeg, RTUINTPTR off, uint32_t *piOrdinal)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszFile[cchFile]); NOREF(cchFile);
    return RTDbgModLineAdd(hCnt, pszFile, uLineNo, iSeg, off, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByAddr} */
static DECLCALLBACK(int) rtDbgModDbgHelp_SymbolByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                                      PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSymbolByAddr(hCnt, iSeg, off, fFlags, poffDisp, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByName} */
static DECLCALLBACK(int) rtDbgModDbgHelp_SymbolByName(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                      PRTDBGSYMBOL pSymInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); RT_NOREF_PV(cchSymbol);
    return RTDbgModSymbolByName(hCnt, pszSymbol/*, cchSymbol*/, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByOrdinal} */
static DECLCALLBACK(int) rtDbgModDbgHelp_SymbolByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSymbolByOrdinal(hCnt, iOrdinal, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolCount} */
static DECLCALLBACK(uint32_t) rtDbgModDbgHelp_SymbolCount(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSymbolCount(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolAdd} */
static DECLCALLBACK(int) rtDbgModDbgHelp_SymbolAdd(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                   RTDBGSEGIDX iSeg, RTUINTPTR off, RTUINTPTR cb, uint32_t fFlags,
                                                   uint32_t *piOrdinal)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); NOREF(cchSymbol);
    return RTDbgModSymbolAdd(hCnt, pszSymbol, iSeg, off, cb, fFlags, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentByIndex} */
static DECLCALLBACK(int) rtDbgModDbgHelp_SegmentByIndex(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSegmentByIndex(hCnt, iSeg, pSegInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentCount} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModDbgHelp_SegmentCount(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSegmentCount(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentAdd} */
static DECLCALLBACK(int) rtDbgModDbgHelp_SegmentAdd(PRTDBGMODINT pMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName, size_t cchName,
                                                    uint32_t fFlags, PRTDBGSEGIDX piSeg)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszName[cchName]); NOREF(cchName);
    return RTDbgModSegmentAdd(hCnt, uRva, cb, pszName, fFlags, piSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnImageSize} */
static DECLCALLBACK(RTUINTPTR) rtDbgModDbgHelp_ImageSize(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    RTUINTPTR cb1 = RTDbgModImageSize(hCnt);
    RTUINTPTR cb2 = pMod->pImgVt->pfnImageSize(pMod);
    return RT_MAX(cb1, cb2);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnRvaToSegOff} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModDbgHelp_RvaToSegOff(PRTDBGMODINT pMod, RTUINTPTR uRva, PRTUINTPTR poffSeg)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModRvaToSegOff(hCnt, uRva, poffSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnClose} */
static DECLCALLBACK(int) rtDbgModDbgHelp_Close(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;;
    RTDbgModRelease(hCnt);
    pMod->pvDbgPriv = NULL;
    return VINF_SUCCESS;
}


/**
 * SymEnumLinesW callback that adds a line number to the container.
 *
 * @returns TRUE, FALSE if we're out of memory.
 * @param   pLineInfo           Line number information.
 * @param   pvUser              Pointer to a RTDBGMODBGHELPARGS structure.
 */
static BOOL CALLBACK rtDbgModDbgHelpCopyLineNumberCallback(PSRCCODEINFOW pLineInfo, PVOID pvUser)
{
    RTDBGMODBGHELPARGS *pArgs  = (RTDBGMODBGHELPARGS *)pvUser;

    if (pLineInfo->Address < pArgs->uModAddr)
    {
        Log((" %#018RX64 %05u  %s  [SKIPPED - INVALID ADDRESS!]\n", pLineInfo->Address, pLineInfo->LineNumber));
        return TRUE;
    }

    /*
     * To save having to call RTUtf16ToUtf8 every time, we keep a copy of the
     * previous file name both as UTF-8 and UTF-16.
     */
    /** @todo we could combine RTUtf16Len and memcmp... */
    size_t cbLen  = (RTUtf16Len(pLineInfo->FileName) + 1) * sizeof(RTUTF16);
    if (   !pArgs->pwszPrev
        || pArgs->cbPrevUtf16Alloc < cbLen
        || memcmp(pArgs->pwszPrev, pLineInfo->FileName, cbLen) )
    {
        if (pArgs->cbPrevUtf16Alloc >= cbLen)
            memcpy(pArgs->pwszPrev, pLineInfo->FileName, cbLen);
        else
        {
            RTMemFree(pArgs->pwszPrev);
            pArgs->cbPrevUtf16Alloc = cbLen;
            pArgs->pwszPrev = (PRTUTF16)RTMemDupEx(pLineInfo->FileName, cbLen, pArgs->cbPrevUtf16Alloc - cbLen);
            if (!pArgs->pwszPrev)
                pArgs->cbPrevUtf16Alloc = 0;
        }

        RTStrFree(pArgs->pszPrev);
        pArgs->pszPrev = NULL;
        int rc = RTUtf16ToUtf8(pLineInfo->FileName, &pArgs->pszPrev);
        if (RT_FAILURE(rc))
        {
            SetLastError(ERROR_OUTOFMEMORY);
            Log(("rtDbgModDbgHelpCopyLineNumberCallback: Out of memory\n"));
            return FALSE;
        }
    }

    /*
     * Add the line number to the container.
     */
    int rc = RTDbgModLineAdd(pArgs->hCnt, pArgs->pszPrev, pLineInfo->LineNumber,
                             RTDBGSEGIDX_RVA, pLineInfo->Address - pArgs->uModAddr, NULL);
    Log((" %#018RX64 %05u  %s  [%Rrc]\n", pLineInfo->Address, pLineInfo->LineNumber, pArgs->pszPrev, rc));
    NOREF(rc);

    return TRUE;
}


/**
 * Copies the line numbers into the container.
 *
 * @returns IPRT status code.
 * @param   pMod                The debug module.
 * @param   hCnt                The container that will keep the symbols.
 * @param   hFake               The fake process handle.
 * @param   uModAddr            The module load address.
 */
static int rtDbgModDbgHelpCopyLineNumbers(PRTDBGMODINT pMod, RTDBGMOD hCnt, HANDLE hFake, uint64_t uModAddr)
{
    RTDBGMODBGHELPARGS Args;
    Args.hCnt             = hCnt;
    Args.pMod             = pMod;
    Args.uModAddr         = uModAddr;
    Args.pszPrev          = NULL;
    Args.pwszPrev         = NULL;
    Args.cbPrevUtf16Alloc = 0;

    int rc;
    if (SymEnumLinesW(hFake, uModAddr, NULL /*pszObj*/, NULL /*pszFile*/, rtDbgModDbgHelpCopyLineNumberCallback, &Args))
        rc = VINF_SUCCESS;
    else
    {
        rc = RTErrConvertFromWin32(GetLastError());
        Log(("Line number enum: %Rrc (%u)\n", rc, GetLastError()));
        if (rc == VERR_NOT_SUPPORTED)
            rc = VINF_SUCCESS;
    }

    RTStrFree(Args.pszPrev);
    RTMemFree(Args.pwszPrev);
    return rc;
}


/**
 * SymEnumSymbols callback that adds a symbol to the container.
 *
 * @returns TRUE
 * @param   pSymInfo            The symbol information.
 * @param   cbSymbol            The symbol size (estimated).
 * @param   pvUser              Pointer to a RTDBGMODBGHELPARGS structure.
 */
static BOOL CALLBACK rtDbgModDbgHelpCopySymbolsCallback(PSYMBOL_INFO pSymInfo, ULONG cbSymbol, PVOID pvUser)
{
    RTDBGMODBGHELPARGS *pArgs = (RTDBGMODBGHELPARGS *)pvUser;
    if (pSymInfo->Address < pArgs->uModAddr) /* NT4 SP1 ntfs.dbg */
    {
        Log(("  %#018RX64 LB %#07x  %s  [SKIPPED - INVALID ADDRESS!]\n", pSymInfo->Address, cbSymbol, pSymInfo->Name));
        return TRUE;
    }
    if (pSymInfo->NameLen >= RTDBG_SYMBOL_NAME_LENGTH)
    {
        Log(("  %#018RX64 LB %#07x  %s  [SKIPPED - TOO LONG (%u > %u)!]\n", pSymInfo->Address, cbSymbol, pSymInfo->Name,
             pSymInfo->NameLen, RTDBG_SYMBOL_NAME_LENGTH));
        return TRUE;
    }

    /* ASSUMES the symbol name is ASCII. */
    int rc = RTDbgModSymbolAdd(pArgs->hCnt, pSymInfo->Name, RTDBGSEGIDX_RVA,
                               pSymInfo->Address - pArgs->uModAddr, cbSymbol, 0, NULL);
    Log(("  %#018RX64 LB %#07x  %s  [%Rrc]\n", pSymInfo->Address, cbSymbol, pSymInfo->Name, rc));
    NOREF(rc);

    return TRUE;
}


/**
 * Copies the symbols into the container.
 *
 * @returns IPRT status code.
 * @param   pMod                The debug module.
 * @param   hCnt                The container that will keep the symbols.
 * @param   hFake               The fake process handle.
 * @param   uModAddr            The module load address.
 */
static int rtDbgModDbgHelpCopySymbols(PRTDBGMODINT pMod, RTDBGMOD hCnt, HANDLE hFake, uint64_t uModAddr)
{
    RTDBGMODBGHELPARGS Args;
    Args.hCnt     = hCnt;
    Args.pMod     = pMod;
    Args.uModAddr = uModAddr;
    int rc;
    if (SymEnumSymbols(hFake, uModAddr, NULL, rtDbgModDbgHelpCopySymbolsCallback, &Args))
        rc = VINF_SUCCESS;
    else
    {
        rc = RTErrConvertFromWin32(GetLastError());
        Log(("SymEnumSymbols: %Rrc (%u)\n", rc, GetLastError()));
    }
    return rc;
}


/** @callback_method_impl{FNRTLDRENUMSEGS,
 * Copies the PE segments over into the container.} */
static DECLCALLBACK(int) rtDbgModDbgHelpAddSegmentsCallback(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    RTDBGMODBGHELPARGS *pArgs = (RTDBGMODBGHELPARGS *)pvUser;
    RT_NOREF_PV(hLdrMod);

    Log(("Segment %.*s: LinkAddress=%#llx RVA=%#llx cb=%#llx\n",
         pSeg->cchName, pSeg->pszName, (uint64_t)pSeg->LinkAddress, (uint64_t)pSeg->RVA, pSeg->cb));

    Assert(pSeg->cchName > 0);
    Assert(!pSeg->pszName[pSeg->cchName]);

    RTLDRADDR cb   = RT_MAX(pSeg->cb, pSeg->cbMapped);
    RTLDRADDR uRva = pSeg->RVA;
    if (!uRva)
        pArgs->uModAddr = pSeg->LinkAddress;
    else if (uRva == NIL_RTLDRADDR)
    {
        cb   = 0;
        uRva = pArgs->uNextRva;
    }
    pArgs->uNextRva = uRva + cb;

    return RTDbgModSegmentAdd(pArgs->hCnt, uRva, cb, pSeg->pszName, 0 /*fFlags*/, NULL);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnTryOpen} */
static DECLCALLBACK(int) rtDbgModDbgHelp_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    NOREF(enmArch); RT_NOREF_PV(hDbgCfg);

    /*
     * Currently only support external files with a executable already present.
     */
    if (!pMod->pszDbgFile)
        return VERR_DBG_NO_MATCHING_INTERPRETER;
    if (!pMod->pImgVt)
        return VERR_DBG_NO_MATCHING_INTERPRETER;

    /*
     * Create a container for copying the information into.  We do this early
     * so we can determine the image base address.
     */
    RTDBGMOD hCnt;
    int rc = RTDbgModCreate(&hCnt, pMod->pszName, 0 /*cbSeg*/, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        RTDBGMODBGHELPARGS Args;
        RT_ZERO(Args);
        Args.hCnt = hCnt;
        rc = pMod->pImgVt->pfnEnumSegments(pMod, rtDbgModDbgHelpAddSegmentsCallback, &Args);
        if (RT_SUCCESS(rc))
        {
            uint32_t cbImage    = pMod->pImgVt->pfnImageSize(pMod);
            uint64_t uImageBase = Args.uModAddr ? Args.uModAddr : 0x4000000;

            /*
             * Try load the module into an empty address space.
             */
            static uint32_t volatile s_uFakeHandle = 0x3940000;
            HANDLE hFake;
            do
                hFake = (HANDLE)(uintptr_t)ASMAtomicIncU32(&s_uFakeHandle);
            while (hFake == NULL || hFake == INVALID_HANDLE_VALUE);

            LogFlow(("rtDbgModDbgHelp_TryOpen: \n"));
            if (SymInitialize(hFake, NULL /*SearchPath*/, FALSE /*fInvalidProcess*/))
            {
                SymSetOptions(SYMOPT_LOAD_LINES | SymGetOptions());

                PRTUTF16 pwszDbgFile;
                rc = RTStrToUtf16(pMod->pszDbgFile, &pwszDbgFile);
                if (RT_SUCCESS(rc))
                {
                    uint64_t uModAddr = SymLoadModuleExW(hFake, NULL /*hFile*/, pwszDbgFile, NULL /*pszModName*/,
                                                         uImageBase, cbImage, NULL /*pModData*/, 0 /*fFlags*/);
                    if (uModAddr != 0)
                    {
                        rc = rtDbgModDbgHelpCopySymbols(pMod, hCnt, hFake, uModAddr);
                        if (RT_SUCCESS(rc))
                            rc = rtDbgModDbgHelpCopyLineNumbers(pMod, hCnt, hFake, uModAddr);
                        if (RT_SUCCESS(rc))
                        {
                            pMod->pvDbgPriv = hCnt;
                            pMod->pDbgVt    = &g_rtDbgModVtDbgDbgHelp;
                            hCnt = NIL_RTDBGMOD;
                            LogFlow(("rtDbgModDbgHelp_TryOpen: Successfully loaded '%s' at %#llx\n",
                                     pMod->pszDbgFile, (uint64_t)uImageBase));
                        }

                        SymUnloadModule64(hFake, uModAddr);
                    }
                    else
                    {
                        rc = RTErrConvertFromWin32(GetLastError());
                        if (RT_SUCCESS_NP(rc))
                            rc = VERR_DBG_NO_MATCHING_INTERPRETER;
                        LogFlow(("rtDbgModDbgHelp_TryOpen: Error loading the module '%s' at %#llx: %Rrc (%u)\n",
                                 pMod->pszDbgFile, (uint64_t)uImageBase, rc, GetLastError()));
                    }
                    RTUtf16Free(pwszDbgFile);
                }
                else
                    LogFlow(("rtDbgModDbgHelp_TryOpen: Unicode version issue: %Rrc\n", rc));

                BOOL fRc2 = SymCleanup(hFake); Assert(fRc2); NOREF(fRc2);
            }
            else
            {
                rc = RTErrConvertFromWin32(GetLastError());
                if (RT_SUCCESS_NP(rc))
                    rc = VERR_DBG_NO_MATCHING_INTERPRETER;
                LogFlow(("rtDbgModDbgHelp_TryOpen: SymInitialize failed: %Rrc (%u)\n", rc, GetLastError()));
            }
        }
        RTDbgModRelease(hCnt);
    }
    return rc;
}



/** Virtual function table for the DBGHELP debug info reader. */
DECL_HIDDEN_CONST(RTDBGMODVTDBG) const g_rtDbgModVtDbgDbgHelp =
{
    /*.u32Magic = */            RTDBGMODVTDBG_MAGIC,
    /*.fSupports = */           RT_DBGTYPE_CODEVIEW,
    /*.pszName = */             "dbghelp",
    /*.pfnTryOpen = */          rtDbgModDbgHelp_TryOpen,
    /*.pfnClose = */            rtDbgModDbgHelp_Close,

    /*.pfnRvaToSegOff = */      rtDbgModDbgHelp_RvaToSegOff,
    /*.pfnImageSize = */        rtDbgModDbgHelp_ImageSize,

    /*.pfnSegmentAdd = */       rtDbgModDbgHelp_SegmentAdd,
    /*.pfnSegmentCount = */     rtDbgModDbgHelp_SegmentCount,
    /*.pfnSegmentByIndex = */   rtDbgModDbgHelp_SegmentByIndex,

    /*.pfnSymbolAdd = */        rtDbgModDbgHelp_SymbolAdd,
    /*.pfnSymbolCount = */      rtDbgModDbgHelp_SymbolCount,
    /*.pfnSymbolByOrdinal = */  rtDbgModDbgHelp_SymbolByOrdinal,
    /*.pfnSymbolByName = */     rtDbgModDbgHelp_SymbolByName,
    /*.pfnSymbolByAddr = */     rtDbgModDbgHelp_SymbolByAddr,

    /*.pfnLineAdd = */          rtDbgModDbgHelp_LineAdd,
    /*.pfnLineCount = */        rtDbgModDbgHelp_LineCount,
    /*.pfnLineByOrdinal = */    rtDbgModDbgHelp_LineByOrdinal,
    /*.pfnLineByAddr = */       rtDbgModDbgHelp_LineByAddr,

    /*.pfnUnwindFrame = */      rtDbgModDbgHelp_UnwindFrame,

    /*.u32EndMagic = */         RTDBGMODVTDBG_MAGIC
};

