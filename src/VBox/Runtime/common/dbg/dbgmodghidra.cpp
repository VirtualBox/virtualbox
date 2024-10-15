/* $Id: dbgmodghidra.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Info Reader for Ghidra XML files created with createPdbXmlFiles.bat/pdb.exe.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#include <iprt/err.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/sort.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/cpp/xml.h>
#include "internal/dbgmod.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Temporary segment data.
 */
typedef struct RTDBGMODGHIDRASEG
{
    const char *pszNumber;
    RTUINTPTR  uRva;
} RTDBGMODGHIDRASEG;
typedef RTDBGMODGHIDRASEG *PRTDBGMODGHIDRASEG;
typedef const RTDBGMODGHIDRASEG *PCRTDBGMODGHIDRASEG;


/** @interface_method_impl{RTDBGMODVTDBG,pfnUnwindFrame} */
static DECLCALLBACK(int) rtDbgModGhidra_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    RT_NOREF(pMod, iSeg, off, pState);
    return VERR_DBG_NO_UNWIND_INFO;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByAddr} */
static DECLCALLBACK(int) rtDbgModGhidra_LineByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                                   PRTINTPTR poffDisp, PRTDBGLINE pLineInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModLineByAddr(hCnt, iSeg, off, poffDisp, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByOrdinal} */
static DECLCALLBACK(int) rtDbgModGhidra_LineByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModLineByOrdinal(hCnt, iOrdinal, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineCount} */
static DECLCALLBACK(uint32_t) rtDbgModGhidra_LineCount(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModLineCount(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineAdd} */
static DECLCALLBACK(int) rtDbgModGhidra_LineAdd(PRTDBGMODINT pMod, const char *pszFile, size_t cchFile, uint32_t uLineNo,
                                                uint32_t iSeg, RTUINTPTR off, uint32_t *piOrdinal)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszFile[cchFile]); NOREF(cchFile);
    return RTDbgModLineAdd(hCnt, pszFile, uLineNo, iSeg, off, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByAddr} */
static DECLCALLBACK(int) rtDbgModGhidra_SymbolByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                                     PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSymbolByAddr(hCnt, iSeg, off, fFlags, poffDisp, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByName} */
static DECLCALLBACK(int) rtDbgModGhidra_SymbolByName(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                 PRTDBGSYMBOL pSymInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); NOREF(cchSymbol);
    return RTDbgModSymbolByName(hCnt, pszSymbol, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByOrdinal} */
static DECLCALLBACK(int) rtDbgModGhidra_SymbolByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSymbolByOrdinal(hCnt, iOrdinal, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolCount} */
static DECLCALLBACK(uint32_t) rtDbgModGhidra_SymbolCount(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSymbolCount(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolAdd} */
static DECLCALLBACK(int) rtDbgModGhidra_SymbolAdd(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                  RTDBGSEGIDX iSeg, RTUINTPTR off, RTUINTPTR cb, uint32_t fFlags,
                                                  uint32_t *piOrdinal)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); NOREF(cchSymbol);
    return RTDbgModSymbolAdd(hCnt, pszSymbol, iSeg, off, cb, fFlags, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentByIndex} */
static DECLCALLBACK(int) rtDbgModGhidra_SegmentByIndex(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSegmentByIndex(hCnt, iSeg, pSegInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentCount} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModGhidra_SegmentCount(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSegmentCount(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentAdd} */
static DECLCALLBACK(int) rtDbgModGhidra_SegmentAdd(PRTDBGMODINT pMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName,
                                                   size_t cchName, uint32_t fFlags, PRTDBGSEGIDX piSeg)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszName[cchName]); NOREF(cchName);
    return RTDbgModSegmentAdd(hCnt, uRva, cb, pszName, fFlags, piSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnImageSize} */
static DECLCALLBACK(RTUINTPTR) rtDbgModGhidra_ImageSize(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModImageSize(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnRvaToSegOff} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModGhidra_RvaToSegOff(PRTDBGMODINT pMod, RTUINTPTR uRva, PRTUINTPTR poffSeg)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModRvaToSegOff(hCnt, uRva, poffSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnClose} */
static DECLCALLBACK(int) rtDbgModGhidra_Close(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    RTDbgModRelease(hCnt);
    pMod->pvDbgPriv = NULL;
    return VINF_SUCCESS;
}


/**
 * Returns the table with the given name from the given table list.
 *
 * @returns Pointer to the XML element node containing the given table or NULL if not found.
 * @param   pelmTables          Pointer to the node containing the tables.
 * @param   pszName             The table name to look for.
 */
static const xml::ElementNode *rtDbgModGhidraGetTableByName(const xml::ElementNode *pelmTables, const char *pszName)
{
    xml::NodesLoop nl1(*pelmTables, "table");
    const xml::ElementNode *pelmTbl;
    while ((pelmTbl = nl1.forAllNodes()))
    {
        const char *pszTblName = NULL;

        if (   pelmTbl->getAttributeValue("name", &pszTblName)
            && !strcmp(pszTblName, pszName))
            return pelmTbl;
    }

    return NULL;
}


/**
 * Adds the symbols from the given \"Symbols\" table.
 *
 * @returns IPRT status code.
 * @param   hCnt                Debug module container handle.
 * @param   elmTbl              Reference to the XML node containing the symbols.
 */
static int rtDbgModGhidraXmlParseSymbols(RTDBGMOD hCnt, const xml::ElementNode &elmTbl)
{
    xml::NodesLoop nlSym(elmTbl, "symbol");
    const xml::ElementNode *pelmSym;
    while ((pelmSym = nlSym.forAllNodes()))
    {
        /* Only parse Function and PublicSymbol tags. */
        const char *pszTag = NULL;
        if (   pelmSym->getAttributeValue("tag", &pszTag)
            && (   !strcmp(pszTag, "PublicSymbol")
                || !strcmp(pszTag, "Function")))
        {
            const char *pszSymName = NULL;
            if (   !pelmSym->getAttributeValue("undecorated", &pszSymName)
                || *pszSymName == '\0')
                pelmSym->getAttributeValue("name", &pszSymName);

            if (   pszSymName
                && strlen(pszSymName) < RTDBG_SYMBOL_NAME_LENGTH)
            {
                uint64_t u64Addr = 0;
                uint64_t u64Length = 0;
                if (   pelmSym->getAttributeValue("address", &u64Addr)
                    && pelmSym->getAttributeValue("length", &u64Length))
                {
                    int rc = RTDbgModSymbolAdd(hCnt, pszSymName, RTDBGSEGIDX_RVA, u64Addr, u64Length, 0 /*fFlags*/, NULL);
                    if (   RT_FAILURE(rc)
                        && rc != VERR_DBG_DUPLICATE_SYMBOL
                        && rc != VERR_DBG_ADDRESS_CONFLICT
                        && rc != VERR_DBG_INVALID_RVA) /* (don't be too strict) */
                        return rc;
                }
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Adds the symbols from the given \"functions\" table.
 *
 * @returns IPRT status code.
 * @param   hCnt                Debug module container handle.
 * @param   elmTbl              Reference to the XML node containing the symbols.
 */
static int rtDbgModGhidraXmlParseFunctions(RTDBGMOD hCnt, const xml::ElementNode &elmTbl)
{
    xml::NodesLoop nlFun(elmTbl, "function");
    const xml::ElementNode *pelmFun;
    while ((pelmFun = nlFun.forAllNodes()))
    {
        xml::NodesLoop nlLn(*pelmFun, "line_number");
        const xml::ElementNode *pelmLn;
        while ((pelmLn = nlLn.forAllNodes()))
        {
            const char *pszFile = NULL;
            uint32_t uLineNo = 0;
            uint64_t off = 0;
            if (   pelmLn->getAttributeValue("source_file", &pszFile)
                && pelmLn->getAttributeValue("start", &uLineNo)
                && pelmLn->getAttributeValue("addr", &off))
            {
                int rc = RTDbgModLineAdd(hCnt, pszFile, uLineNo, RTDBGSEGIDX_RVA, off, NULL /*piOrdinal*/);
                if (   RT_FAILURE(rc)
                    && rc != VERR_DBG_DUPLICATE_SYMBOL
                    && rc != VERR_DBG_ADDRESS_CONFLICT
                    && rc != VERR_DBG_INVALID_RVA) /* (don't be too strict) */
                    return rc;
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * @copydoc FNRTSORTCMP
 */
static DECLCALLBACK(int) rtDbgModGhidraSegmentsSortCmp(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PCRTDBGMODGHIDRASEG pSeg1 = (PCRTDBGMODGHIDRASEG)pvElement1;
    PCRTDBGMODGHIDRASEG pSeg2 = (PCRTDBGMODGHIDRASEG)pvElement2;

    if (pSeg1->uRva > pSeg2->uRva)
        return 1;
    if (pSeg1->uRva < pSeg2->uRva)
        return -1;

    return 0;
}


/**
 * Adds the segments in the given \"SegmentMap\" table.
 *
 * @returns IPRT status code.
 * @param   hCnt                Debug module container handle.
 * @param   elmTblSeg           Reference to the XML node containing the segments.
 */
static int rtDbgModGhidraSegmentsAdd(RTDBGMOD hCnt, const xml::ElementNode &elmTblSeg)
{
    RTDBGMODGHIDRASEG aSegments[32];
    uint32_t idxSeg = 0;

    xml::NodesLoop nl1(elmTblSeg, "segment");
    const xml::ElementNode *pelmSeg;
    while (   (pelmSeg = nl1.forAllNodes())
           && idxSeg < RT_ELEMENTS(aSegments))
    {
        const char *pszNumber = NULL;
        RTUINTPTR uRva = 0;

        if (   pelmSeg->getAttributeValue("number", &pszNumber)
            && pelmSeg->getAttributeValue("address", &uRva))
        {
            aSegments[idxSeg].pszNumber = pszNumber;
            aSegments[idxSeg].uRva      = uRva;
            idxSeg++;
        }
    }

    /* Sort the segments by RVA so it is possible to deduce segment sizes. */
    RTSortShell(&aSegments[0], idxSeg, sizeof(aSegments[0]), rtDbgModGhidraSegmentsSortCmp, NULL);

    for (uint32_t i = 0; i < idxSeg - 1; i++)
    {
        int rc = RTDbgModSegmentAdd(hCnt, aSegments[i].uRva, aSegments[i + 1].uRva - aSegments[i].uRva,
                                    aSegments[i].pszNumber, 0 /*fFlags*/, NULL);
        if (RT_FAILURE(rc))
            return rc;
    }

    /* Last segment for which we assume a size of 0 right now. */
    int rc = RTDbgModSegmentAdd(hCnt, aSegments[idxSeg - 1].uRva, 0,
                                aSegments[idxSeg - 1].pszNumber, 0 /*fFlags*/, NULL);
    if (RT_FAILURE(rc))
        return rc;

    return rc;
}


/**
 * Load the symbols from an XML document.
 *
 * @returns IPRT status code.
 * @param   hCnt                Debug module container handle.
 * @param   a_pDoc              Pointer to the XML document.
 */
static int rtDbgModGhidraXmlParse(RTDBGMOD hCnt, xml::Document *a_pDoc)
{
    /*
     * Get the root element and check whether it looks like a valid Ghidra XML.
     */
    const xml::ElementNode *pelmRoot = a_pDoc->getRootElement();
    if (   !pelmRoot
        || strcmp(pelmRoot->getName(), "pdb") != 0)
        return VERR_DBG_NO_MATCHING_INTERPRETER;

    const xml::ElementNode *pelmTables = pelmRoot->findChildElement("tables");
    if (!pelmTables)
        return VERR_DBG_NO_MATCHING_INTERPRETER;

    const xml::ElementNode *pelmTbl = rtDbgModGhidraGetTableByName(pelmTables, "SegmentMap");
    if (pelmTbl)
    {
        int rc = rtDbgModGhidraSegmentsAdd(hCnt, *pelmTbl);
        if (RT_SUCCESS(rc))
        {
            pelmTbl = rtDbgModGhidraGetTableByName(pelmTables, "Symbols");
            if (pelmTbl)
            {
                rc = rtDbgModGhidraXmlParseSymbols(hCnt, *pelmTbl);
                if (RT_SUCCESS(rc))
                {
                    pelmTbl = pelmRoot->findChildElement("functions"); /* Might not be there. */
                    if (pelmTbl)
                        rc = rtDbgModGhidraXmlParseFunctions(hCnt, *pelmTbl);
                    return rc;
                }
            }
        }
    }

    return VERR_DBG_NO_MATCHING_INTERPRETER;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnTryOpen} */
static DECLCALLBACK(int) rtDbgModGhidra_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    RT_NOREF(enmArch); RT_NOREF_PV(hDbgCfg);

    /*
     * Fend off images.
     */
    if (!pMod->pszDbgFile)
        return VERR_DBG_NO_MATCHING_INTERPRETER;
    pMod->pvDbgPriv = NULL;

    /*
     * Try open the file and create an instance.
     */
    xml::Document       Doc;
    {
        xml::XmlFileParser  Parser;
        try
        {
            Parser.read(pMod->pszDbgFile, Doc);
        }
        catch (xml::XmlError &rErr)
        {
            RT_NOREF(rErr);
            return VERR_DBG_NO_MATCHING_INTERPRETER;
        }
        catch (xml::EIPRTFailure &rErr)
        {
            return rErr.rc();
        }
    }

    RTDBGMOD hCnt;
    int rc = RTDbgModCreate(&hCnt, pMod->pszName, 0 /*cbSeg*/, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        /*
         * Hand the xml doc over to the common code.
         */
        try
        {
            rc = rtDbgModGhidraXmlParse(hCnt, &Doc);
            if (RT_SUCCESS(rc))
            {
                pMod->pvDbgPriv = hCnt;
                return VINF_SUCCESS;
            }
        }
        catch (RTCError &rXcpt)      // includes all XML exceptions
        {
            RT_NOREF(rXcpt);
            rc = VERR_DBG_NO_MATCHING_INTERPRETER;
        }
        RTDbgModRelease(hCnt);
    }

    return rc;
}



/** Virtual function table for the Ghidra XML file reader. */
DECL_HIDDEN_CONST(RTDBGMODVTDBG) const g_rtDbgModVtDbgGhidra =
{
    /*.u32Magic = */            RTDBGMODVTDBG_MAGIC,
    /*.fSupports = */           RT_DBGTYPE_OTHER | RT_DBGTYPE_MAP,
    /*.pszName = */             "ghidra",
    /*.pfnTryOpen = */          rtDbgModGhidra_TryOpen,
    /*.pfnClose = */            rtDbgModGhidra_Close,

    /*.pfnRvaToSegOff = */      rtDbgModGhidra_RvaToSegOff,
    /*.pfnImageSize = */        rtDbgModGhidra_ImageSize,

    /*.pfnSegmentAdd = */       rtDbgModGhidra_SegmentAdd,
    /*.pfnSegmentCount = */     rtDbgModGhidra_SegmentCount,
    /*.pfnSegmentByIndex = */   rtDbgModGhidra_SegmentByIndex,

    /*.pfnSymbolAdd = */        rtDbgModGhidra_SymbolAdd,
    /*.pfnSymbolCount = */      rtDbgModGhidra_SymbolCount,
    /*.pfnSymbolByOrdinal = */  rtDbgModGhidra_SymbolByOrdinal,
    /*.pfnSymbolByName = */     rtDbgModGhidra_SymbolByName,
    /*.pfnSymbolByAddr = */     rtDbgModGhidra_SymbolByAddr,

    /*.pfnLineAdd = */          rtDbgModGhidra_LineAdd,
    /*.pfnLineCount = */        rtDbgModGhidra_LineCount,
    /*.pfnLineByOrdinal = */    rtDbgModGhidra_LineByOrdinal,
    /*.pfnLineByAddr = */       rtDbgModGhidra_LineByAddr,

    /*.pfnUnwindFrame = */      rtDbgModGhidra_UnwindFrame,

    /*.u32EndMagic = */         RTDBGMODVTDBG_MAGIC
};

