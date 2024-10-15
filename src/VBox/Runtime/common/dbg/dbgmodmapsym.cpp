/* $Id: dbgmodmapsym.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Map Reader for MAPSYM files (used by SYMDBG from old MASM).
 *
 * MAPSYM is was the tool producing these files from linker map files for
 * use with SYMDBG (which shipped with MASM 3.0 (possibly earlier)), the OS/2
 * kernel debugger, and other tools.  The format is very limited and they had
 * to strip down the os2krnl.map file in later years to keep MAPSYM happy.
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

#include <iprt/err.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include "internal/dbgmod.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** @name MAPSYM structures and constants.
 * @{ */

/** MAPSYM: Header structure.   */
typedef struct MAPSYMHDR
{
    uint16_t    off16NextMap;                   /**< 0x00: Offset of the next map divided by 16. */
    uint8_t     bFlags;                         /**< 0x02: Who really knows... */
    uint8_t     bReserved;                      /**< 0x03: Reserved / unknown. */
    uint16_t    uSegEntry;                      /**< 0x04: Some entrypoint/segment thing we don't care about. */
    uint16_t    cConsts;                        /**< 0x06: Constants referenced by offConstDef. */
    uint16_t    offConstDef;                    /**< 0x08: Offset to head of constant chain.  Not div 16? */
    uint16_t    cSegs;                          /**< 0x0a: Number of segments in the map. */
    uint16_t    off16SegDef;                    /**< 0x0c: Offset of the segment defintions divided by 16. */
    uint8_t     cchMaxSym;                      /**< 0x0e: Maximum symbol-name length. */
    uint8_t     cchModule;                      /**< 0x0f: Length of the module name. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char        achModule[RT_FLEXIBLE_ARRAY];   /**< 0x10: Module name, length given by cchModule. */
} MAPSYMHDR;

/** MAPSYM: Tail structure.   */
typedef struct MAPSYMTAIL
{
    uint16_t    offNextMap;                     /**< 0x00: Always zero (it's the tail, see). */
    uint8_t     bRelease;                       /**< 0x02: Minor version number. */
    uint8_t     bVersion;                       /**< 0x03: Major version number. */
} MAPSYMTAIL;

/** MAPSYM: Segment defintion.   */
typedef struct MAPSYMSEGDEF
{
    uint16_t    off16NextSeg;                  /**< 0x00: Offset of the next segment divided by 16. */
    uint16_t    cSymbols;                      /**< 0x02: Number of symbol offsets . */
    uint16_t    offSymbolOffsets;              /**< 0x04: Offset of the symbol offset table. Each entry is a 16-bit value giving
                                                *         the offset symbol relative to this structure. */
    uint16_t    au16Reserved0[4];              /**< 0x06: Reserved / unknown.
                                                * First byte/word seems to be 1-based segment number. */
    uint8_t     bFlags;                        /**< 0x0e: MAPSYMSEGDEF_F_32BIT or zero. */
    uint8_t     bReserved1;                    /**< 0x0f: Reserved / unknown. */
    uint16_t    offLineDef;                    /**< 0x10: Offset to the line defintions. */
    uint16_t    u16Reserved2;                  /**< 0x12: Reserved / unknown.  Often seen holding 0xff00. */
    uint8_t     cchSegName;                    /**< 0x14: Segment name length. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char        achSegName[RT_FLEXIBLE_ARRAY]; /**< 0x15: Segment name, length given by cchSegName. */
} MAPSYMSEGDEF;

#define MAPSYMSEGDEF_F_32BIT    UINT8_C(0x01)  /**< Indicates 32-bit segment rather than 16-bit, relevant for symbols. */
#define MAPSYMSEGDEF_F_UNKNOWN  UINT8_C(0x02)  /**< Set on all segments in os2krnlr.sym from ACP2. */

/** MAPSYM: 16-bit symbol   */
typedef struct MAPSYMSYMDEF16
{
    uint16_t    uValue;                        /**< 0x00: The symbol value (address). */
    uint8_t     cchName;                       /**< 0x02: Symbol name length. */
    char        achName[1];                    /**< 0x03: The symbol name, length give by cchName. */
} MAPSYMSYMDEF16;

/** MAPSYM: 16-bit symbol   */
typedef struct MAPSYMSYMDEF32
{
    uint32_t    uValue;                        /**< 0x00: The symbol value (address). */
    uint8_t     cchName;                       /**< 0x04: Symbol name length. */
    char        achName[1];                    /**< 0x05: The symbol name, length give by cchName. */
} MAPSYMSYMDEF32;

/** MAPSYM: Line number defintions. */
typedef struct MAPSYMLINEDEF
{
    uint16_t    off16NextLine;                 /**< 0x00: Offset to the next line defintion divided by 16. */
    uint16_t    uSegment;                      /**< 0x02: Guessing this must be segment number. */
    uint16_t    offLines;                      /**< 0x04: Offset to the line number array, relative to this structure. */
    uint16_t    cLines;                        /**< 0x08: Number of line numbers in the array. */
    uint8_t     cchSrcFile;                    /**< 0x0a: Length of source filename. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char        achSrcFile[RT_FLEXIBLE_ARRAY]; /**< 0x0b: Source filename, length given by cchSrcFile. */
} MAPSYMLINEDEF;

/** MAPSYM: 16-bit line numbers. */
typedef struct MAPSYMLINENO16
{
    uint16_t    offSeg;
    uint16_t    uLineNo;
} MAPSYMLINENO16;

/** @} */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Maximum number of segments we expect in a MAPSYM file. */
#define RTDBGMODMAPSYM_MAX_SEGMENTS 256



/** @interface_method_impl{RTDBGMODVTDBG,pfnUnwindFrame} */
static DECLCALLBACK(int) rtDbgModMapSym_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    RT_NOREF(pMod, iSeg, off, pState);
    return VERR_DBG_NO_UNWIND_INFO;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByAddr} */
static DECLCALLBACK(int) rtDbgModMapSym_LineByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                               PRTINTPTR poffDisp, PRTDBGLINE pLineInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModLineByAddr(hCnt, iSeg, off, poffDisp, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByOrdinal} */
static DECLCALLBACK(int) rtDbgModMapSym_LineByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModLineByOrdinal(hCnt, iOrdinal, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineCount} */
static DECLCALLBACK(uint32_t) rtDbgModMapSym_LineCount(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModLineCount(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineAdd} */
static DECLCALLBACK(int) rtDbgModMapSym_LineAdd(PRTDBGMODINT pMod, const char *pszFile, size_t cchFile, uint32_t uLineNo,
                                            uint32_t iSeg, RTUINTPTR off, uint32_t *piOrdinal)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszFile[cchFile]); NOREF(cchFile);
    return RTDbgModLineAdd(hCnt, pszFile, uLineNo, iSeg, off, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByAddr} */
static DECLCALLBACK(int) rtDbgModMapSym_SymbolByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                                 PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSymbolByAddr(hCnt, iSeg, off, fFlags, poffDisp, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByName} */
static DECLCALLBACK(int) rtDbgModMapSym_SymbolByName(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                 PRTDBGSYMBOL pSymInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); NOREF(cchSymbol);
    return RTDbgModSymbolByName(hCnt, pszSymbol, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByOrdinal} */
static DECLCALLBACK(int) rtDbgModMapSym_SymbolByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSymbolByOrdinal(hCnt, iOrdinal, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolCount} */
static DECLCALLBACK(uint32_t) rtDbgModMapSym_SymbolCount(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSymbolCount(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolAdd} */
static DECLCALLBACK(int) rtDbgModMapSym_SymbolAdd(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                  RTDBGSEGIDX iSeg, RTUINTPTR off, RTUINTPTR cb, uint32_t fFlags,
                                                  uint32_t *piOrdinal)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); NOREF(cchSymbol);
    return RTDbgModSymbolAdd(hCnt, pszSymbol, iSeg, off, cb, fFlags, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentByIndex} */
static DECLCALLBACK(int) rtDbgModMapSym_SegmentByIndex(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSegmentByIndex(hCnt, iSeg, pSegInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentCount} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModMapSym_SegmentCount(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModSegmentCount(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentAdd} */
static DECLCALLBACK(int) rtDbgModMapSym_SegmentAdd(PRTDBGMODINT pMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName,
                                               size_t cchName, uint32_t fFlags, PRTDBGSEGIDX piSeg)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    Assert(!pszName[cchName]); NOREF(cchName);
    return RTDbgModSegmentAdd(hCnt, uRva, cb, pszName, fFlags, piSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnImageSize} */
static DECLCALLBACK(RTUINTPTR) rtDbgModMapSym_ImageSize(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModImageSize(hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnRvaToSegOff} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModMapSym_RvaToSegOff(PRTDBGMODINT pMod, RTUINTPTR uRva, PRTUINTPTR poffSeg)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    return RTDbgModRvaToSegOff(hCnt, uRva, poffSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnClose} */
static DECLCALLBACK(int) rtDbgModMapSym_Close(PRTDBGMODINT pMod)
{
    RTDBGMOD hCnt = (RTDBGMOD)pMod->pvDbgPriv;
    RTDbgModRelease(hCnt);
    pMod->pvDbgPriv = NULL;
    return VINF_SUCCESS;
}


/**
 * Validate the module header.
 *
 * @returns true if valid, false if not.
 * @param   pHdr        The header.
 * @param   cbAvail     How much we've actually read.
 * @param   cbFile      The file size (relative to module header).
 */
static bool rtDbgModMapSymIsValidHeader(MAPSYMHDR const *pHdr, size_t cbAvail, uint64_t cbFile)
{
    if (cbAvail <= RT_UOFFSETOF(MAPSYMHDR, achModule))
        return false;

    if (pHdr->cSegs == 0)
        return false;
    if (pHdr->cSegs > RTDBGMODMAPSYM_MAX_SEGMENTS)
        return false;

    if (pHdr->off16SegDef == 0)
        return false;
    if (pHdr->off16SegDef * (uint32_t)16 >= cbFile)
        return false;

    if (pHdr->cchModule == 0)
        return false;
    if (pHdr->cchModule > 128) /* Note must be smaller than abPadding below in caller */
        return false;

    size_t cchMaxName = cbAvail - RT_UOFFSETOF(MAPSYMHDR, achModule);
    if (pHdr->cchModule > cchMaxName)
        return false;

    for (uint32_t i = 0; i < pHdr->cchModule; i++)
    {
        unsigned char const uch = pHdr->achModule[i];
        if (   uch <  0x20
            || uch >= 0x7f)
            return false;
    }

    return true;
}


/**
 * Validate the given segment definition.
 *
 * @returns true if valid, false if not.
 * @param   pSegDef     The segment definition structure.
 * @param   cbMax       Host many bytes are available starting with pSegDef.
 */
static bool rtDbgModMapSymIsValidSegDef(MAPSYMSEGDEF const *pSegDef, size_t cbMax)
{
    if (RT_UOFFSETOF(MAPSYMSEGDEF, achSegName) > cbMax)
        return false;
    if (pSegDef->cSymbols)
    {
        if (pSegDef->cSymbols > _32K)
        {
            Log(("rtDbgModMapSymIsValidSegDef: Too many symbols: %#x\n", pSegDef->cSymbols));
            return false;
        }

        if (pSegDef->offSymbolOffsets + (uint32_t)2 * pSegDef->cSymbols > cbMax)
        {
            Log(("rtDbgModMapSymIsValidSegDef: Bad symbol offset/count: %#x/%#x\n", pSegDef->offSymbolOffsets, pSegDef->cSymbols));
            return false;
        }
    }

    size_t cchMaxName = cbMax - RT_UOFFSETOF(MAPSYMHDR, achModule);
    if (pSegDef->cchSegName > cchMaxName)
    {
        Log(("rtDbgModMapSymIsValidSegDef: Bad segment name length\n"));
        return false;
    }

    for (uint32_t i = 0; i < pSegDef->cchSegName; i++)
    {
        unsigned char uch = pSegDef->achSegName[i];
        if (   uch <  0x20
            || uch >= 0x7f)
        {
            Log(("rtDbgModMapSymIsValidSegDef: Bad segment name: %.*Rhxs\n", pSegDef->cchSegName, pSegDef->achSegName));
            return false;
        }
    }

    return true;
}


/**
 * Fills @a hCnt with segments and symbols from the MAPSYM file.
 *
 * @note  We only support reading the first module, right now.
 */
static int rtDbgModMapSymReadIt(RTDBGMOD hCnt, uint8_t const *pbFile, size_t cbFile)
{
    /*
     * Revalidate the header.
     */
    MAPSYMHDR const *pHdr = (MAPSYMHDR const *)pbFile;
    if (!rtDbgModMapSymIsValidHeader(pHdr, cbFile, cbFile))
        return VERR_DBG_NO_MATCHING_INTERPRETER;
    Log(("rtDbgModMapSymReadIt: szModule='%.*s' cSegs=%u off16NextMap=%#x\n",
         pHdr->cchModule, pHdr->achModule, pHdr->cSegs, pHdr->off16NextMap));

    /*
     * Load each segment.
     */
    uint32_t       uRva       = 0;
    uint32_t const cSegs      = pHdr->cSegs;
    uint32_t       offSegment = pHdr->off16SegDef * (uint32_t)16;
    for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
    {
        if (offSegment >= cbFile)
            return VERR_DBG_NO_MATCHING_INTERPRETER;

        size_t const cbMax = cbFile - offSegment;
        MAPSYMSEGDEF const *pSegDef = (MAPSYMSEGDEF const *)&pbFile[offSegment];
        if (!rtDbgModMapSymIsValidSegDef(pSegDef, cbMax))
            return VERR_DBG_NO_MATCHING_INTERPRETER;

        Log(("rtDbgModMapSymReadIt:  Segment #%u: flags=%#x name='%.*s' symbols=%#x @ %#x next=%#x lines=@%#x (reserved: %#x %#x %#x %#x %#x %#x)\n",
             iSeg, pSegDef->bFlags, pSegDef->cchSegName, pSegDef->achSegName, pSegDef->cSymbols, pSegDef->offSymbolOffsets,
             pSegDef->off16NextSeg, pSegDef->offLineDef, pSegDef->au16Reserved0[0], pSegDef->au16Reserved0[1],
             pSegDef->au16Reserved0[2], pSegDef->au16Reserved0[3], pSegDef->bReserved1, pSegDef->u16Reserved2));

        /*
         * First symbol pass finds the largest symbol and uses that as the segment size.
         */
        uint32_t               cbSegmentEst = 0;
        uint32_t const         cSymbols     = pSegDef->cSymbols;
        uint16_t const * const paoffSymbols = (uint16_t const *)&pbFile[offSegment + pSegDef->offSymbolOffsets];
        bool const             fIs32Bit     = RT_BOOL(pSegDef->bFlags & MAPSYMSEGDEF_F_32BIT);
        uint32_t const         cbSymDef     = fIs32Bit ? 4 + 1 : 2 + 1;
        for (uint32_t iSymbol = 0; iSymbol < cSymbols; iSymbol++)
        {
            uint32_t off = paoffSymbols[iSymbol] + offSegment;
            if (off + cbSymDef <= cbFile)
            {
                uint32_t uValue = fIs32Bit ? *(uint32_t const *)&pbFile[off] : (uint32_t)*(uint16_t const *)&pbFile[off];
                if (uValue > cbSegmentEst)
                    cbSegmentEst = uValue;
            }
            else
                Log(("rtDbgModMapSymReadIt:  Bad symbol offset %#x\n", off));
        }

        /*
         * Add the segment.
         */
        char szName[256];
        memcpy(szName, pSegDef->achSegName, pSegDef->cchSegName);
        szName[pSegDef->cchSegName] = '\0';
        if (!pSegDef->cchSegName)
            RTStrPrintf(szName, sizeof(szName), "seg%02u", iSeg);

        RTDBGSEGIDX idxDbgSeg = iSeg;
        int rc = RTDbgModSegmentAdd(hCnt, uRva, cbSegmentEst, szName, 0 /*fFlags*/, &idxDbgSeg);
        if (RT_FAILURE(rc))
            return rc;

        uRva += cbSegmentEst;

        /*
         * The second symbol pass loads the symbol values and names.
         */
        for (uint32_t iSymbol = 0; iSymbol < cSymbols; iSymbol++)
        {
            uint32_t off = paoffSymbols[iSymbol] + offSegment;
            if (off + cbSymDef <= cbFile)
            {
                /* Get value: */
                uint32_t uValue = RT_MAKE_U16(pbFile[off], pbFile[off + 1]);
                off += 2;
                if (fIs32Bit)
                {
                    uValue |= RT_MAKE_U32_FROM_U8(0, 0, pbFile[off], pbFile[off + 1]);
                    off += 2;
                }

                /* Get name: */
                uint8_t cchName = pbFile[off++];
                if (off + cchName <= cbFile)
                {
                    memcpy(szName, &pbFile[off], cchName);
                    szName[cchName] = '\0';
                    RTStrPurgeEncoding(szName);
                }
                else
                    cchName = 0;
                if (cchName == 0)
                    RTStrPrintf(szName, sizeof(szName), "unknown_%u_%u", iSeg, iSymbol);

                /* Try add it: */
                rc = RTDbgModSymbolAdd(hCnt, szName, idxDbgSeg, uValue, 0 /*cb*/, 0 /*fFlags*/, NULL /*piOrdinal*/);
                if (RT_SUCCESS(rc))
                    Log7(("rtDbgModMapSymReadIt: %02x:%06x %s\n", idxDbgSeg, uValue, szName));
                else if (   rc == VERR_DBG_DUPLICATE_SYMBOL
                         || rc == VERR_DBG_ADDRESS_CONFLICT
                         || rc == VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE)
                    Log(("rtDbgModMapSymReadIt: %02x:%06x %s\n", idxDbgSeg, uValue, szName));
                else
                {
                    Log(("rtDbgModMapSymReadIt: Unexpected RTDbgModSymbolAdd failure: %Rrc - %02x:%06x %s\n",
                         rc, idxDbgSeg, uValue, szName));
                    return rc;
                }
            }
        }

        /* Next segment */
        offSegment = pSegDef->off16NextSeg * (uint32_t)16;
    }
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnTryOpen} */
static DECLCALLBACK(int) rtDbgModMapSym_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    NOREF(enmArch); RT_NOREF_PV(hDbgCfg);

    /*
     * Fend off images.
     */
    if (   !pMod->pszDbgFile
        || pMod->pImgVt)
        return VERR_DBG_NO_MATCHING_INTERPRETER;
    pMod->pvDbgPriv = NULL;

    /*
     * Try open the file and check out the first header.
     */
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pMod->pszDbgFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        uint64_t cbFile = 0;
        rc = RTFileQuerySize(hFile, &cbFile);
        if (   RT_SUCCESS(rc)
            && cbFile < _2M)
        {
            union
            {
                MAPSYMHDR Hdr;
                char      abPadding[sizeof(MAPSYMHDR) + 257]; /* rtDbgModMapSymIsValidHeader makes size assumptions.  */
            } uBuf;
            size_t cbToRead = (size_t)RT_MIN(cbFile, sizeof(uBuf));
            rc = RTFileReadAt(hFile, 0, &uBuf, RT_MIN(cbFile, sizeof(uBuf)), NULL);
            if (RT_SUCCESS(rc))
            {
                if (rtDbgModMapSymIsValidHeader(&uBuf.Hdr, cbToRead, cbFile))
                {
                    uBuf.Hdr.achModule[uBuf.Hdr.cchModule] = '\0';

                    /*
                     * Read the whole thing into memory, create an
                     * instance/container and load it with symbols.
                     */
                    void  *pvFile  = NULL;
                    size_t cbFile2 = 0;
                    rc = RTFileReadAllByHandle(hFile, &pvFile, &cbFile2);
                    if (RT_SUCCESS(rc))
                    {
                        RTDBGMOD hCnt;
                        rc = RTDbgModCreate(&hCnt, uBuf.Hdr.achModule, 0 /*cbSeg*/, 0 /*fFlags*/);
                        if (RT_SUCCESS(rc))
                        {
                            rc = rtDbgModMapSymReadIt(hCnt, (uint8_t const *)pvFile, cbFile2);
                            if (RT_SUCCESS(rc))
                                pMod->pvDbgPriv = hCnt;
                            else
                                RTDbgModRelease(hCnt);
                        }
                        RTFileReadAllFree(pvFile, cbFile2);
                    }
                }
                else
                    rc = VERR_DBG_NO_MATCHING_INTERPRETER;
            }
        }
        else
            rc = VERR_DBG_NO_MATCHING_INTERPRETER;
        RTFileClose(hFile);
    }
    Log(("rtDbgModMapSym_TryOpen: %s -> %Rrc, %p\n", pMod->pszDbgFile, rc, pMod->pvDbgPriv));
    return rc;
}



/** Virtual function table for the MAPSYM file reader. */
DECL_HIDDEN_CONST(RTDBGMODVTDBG) const g_rtDbgModVtDbgMapSym =
{
    /*.u32Magic = */            RTDBGMODVTDBG_MAGIC,
    /*.fSupports = */           RT_DBGTYPE_SYM,
    /*.pszName = */             "mapsym",
    /*.pfnTryOpen = */          rtDbgModMapSym_TryOpen,
    /*.pfnClose = */            rtDbgModMapSym_Close,

    /*.pfnRvaToSegOff = */      rtDbgModMapSym_RvaToSegOff,
    /*.pfnImageSize = */        rtDbgModMapSym_ImageSize,

    /*.pfnSegmentAdd = */       rtDbgModMapSym_SegmentAdd,
    /*.pfnSegmentCount = */     rtDbgModMapSym_SegmentCount,
    /*.pfnSegmentByIndex = */   rtDbgModMapSym_SegmentByIndex,

    /*.pfnSymbolAdd = */        rtDbgModMapSym_SymbolAdd,
    /*.pfnSymbolCount = */      rtDbgModMapSym_SymbolCount,
    /*.pfnSymbolByOrdinal = */  rtDbgModMapSym_SymbolByOrdinal,
    /*.pfnSymbolByName = */     rtDbgModMapSym_SymbolByName,
    /*.pfnSymbolByAddr = */     rtDbgModMapSym_SymbolByAddr,

    /*.pfnLineAdd = */          rtDbgModMapSym_LineAdd,
    /*.pfnLineCount = */        rtDbgModMapSym_LineCount,
    /*.pfnLineByOrdinal = */    rtDbgModMapSym_LineByOrdinal,
    /*.pfnLineByAddr = */       rtDbgModMapSym_LineByAddr,

    /*.pfnUnwindFrame = */      rtDbgModMapSym_UnwindFrame,

    /*.u32EndMagic = */         RTDBGMODVTDBG_MAGIC
};

