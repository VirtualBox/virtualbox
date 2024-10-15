/* $Id: dbgmodcodeview.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Module Reader For Microsoft CodeView and COFF.
 *
 * Based on the following documentation (plus guess work and googling):
 *
 *  - "Tools Interface Standard (TIS) Formats Specification for Windows",
 *    dated February 1993, version 1.0.
 *
 *  - "Visual C++ 5.0 Symbolic Debug Information Specification" chapter of
 *     SPECS.CHM from MSDN Library October 2001.
 *
 *  - "High Level Languages Debug Table Documentation", aka HLLDBG.HTML, aka
 *     IBMHLL.HTML, last changed 1996-07-08.
 *
 * Testcases using RTLdrFlt:
 *     - VBoxPcBios.sym at 0xf0000.
 *     - NT4 kernel PE image (coff syms).
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
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/fsvfs.h>
#include <iprt/latin1.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/sort.h>
#include <iprt/string.h>
#include <iprt/strcache.h>
#include <iprt/vfs.h>
#include "internal/dbgmod.h"
#include "internal/magics.h"

#include <iprt/formats/codeview.h>
#include <iprt/formats/pecoff.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * File type.
 */
typedef enum RTCVFILETYPE
{
    RTCVFILETYPE_INVALID = 0,
    /** Executable image. */
    RTCVFILETYPE_IMAGE,
    /** A DBG-file with a IMAGE_SEPARATE_DEBUG_HEADER. */
    RTCVFILETYPE_DBG,
    /** A PDB file. */
    RTCVFILETYPE_PDB,
    /** Some other kind of file with CV at the end. */
    RTCVFILETYPE_OTHER_AT_END,
    /** The end of the valid values. */
    RTCVFILETYPE_END,
    /** Type blowup. */
    RTCVFILETYPE_32BIT_HACK = 0x7fffffff
} RTCVFILETYPE;


/**
 * CodeView debug info reader instance.
 */
typedef struct RTDBGMODCV
{
    /** Using a container for managing the debug info. */
    RTDBGMOD        hCnt;

    /** @name Codeview details
     * @{ */
    /** The code view magic (used as format indicator). */
    uint32_t        u32CvMagic;
    /** The offset of the CV debug info in the file. */
    uint32_t        offBase;
    /** The size of the CV debug info. */
    uint32_t        cbDbgInfo;
    /** The offset of the subsection directory (relative to offBase). */
    uint32_t        offDir;
    /** @}  */

    /** @name COFF details.
     * @{ */
    /** Offset of the COFF header. */
    uint32_t        offCoffDbgInfo;
    /** The size of the COFF debug info. */
    uint32_t        cbCoffDbgInfo;
    /** The COFF debug info header. */
    IMAGE_COFF_SYMBOLS_HEADER CoffHdr;
    /** @} */

    /** @name External PDB (v2) with a DBG file.
     * @{ */
    /** The resolved PDB path (RTStrFree). */
    char           *pszPdbFilename;
    /** The PDB VFS.  This is only valid between rtDbgModCvOpenPdb20Callback()
     *  and the end of rtDbgModCv_TryOpen(). */
    RTVFS           hVfsPdb;
    /** @} */

    /** The file type. */
    RTCVFILETYPE    enmType;
    /** The file handle (if external).  */
    RTFILE          hFile;
    /** Pointer to the module (no reference retained). */
    PRTDBGMODINT    pMod;

    /** The image size, if we know it. This is 0 if we don't know it. */
    uint32_t        cbImage;

    /** Indicates that we've loaded segments into the container already. */
    bool            fHaveLoadedSegments;
    /** Alternative address translation method for DOS frames. */
    bool            fHaveDosFrames;

    /** @name Codeview Parsing state.
     * @{ */
    /** Number of directory entries. */
    uint32_t        cDirEnts;
    /** The directory (converted to 32-bit). */
    PRTCVDIRENT32   paDirEnts;
    /** Current debugging style when parsing modules. */
    uint16_t        uCurStyle;
    /** Current debugging style version (HLL only). */
    uint16_t        uCurStyleVer;

    /** The segment map (if present). */
    PRTCVSEGMAP     pSegMap;
    /** Segment names. */
    char           *pszzSegNames;
    /** The size of the segment names. */
    uint32_t        cbSegNames;

    /** Size of the block pchSrcStrings points to. */
    size_t          cbSrcStrings;
    /** Buffer space allocated for the source string table.  */
    size_t          cbSrcStringsAlloc;
    /** Copy of the last CV8 source string table.  */
    char           *pchSrcStrings;

    /** The size of the current source information table. */
    size_t          cbSrcInfo;
    /** Buffer space allocated for the source information table.  */
    size_t          cbSrcInfoAlloc;
    /** Copy of the last CV8 source information table. */
    uint8_t        *pbSrcInfo;

    /** @}  */

} RTDBGMODCV;
/** Pointer to a codeview debug info reader instance. */
typedef RTDBGMODCV *PRTDBGMODCV;
/** Pointer to a const codeview debug info reader instance. */
typedef RTDBGMODCV *PCRTDBGMODCV;



/**
 * Subsection callback.
 *
 * @returns IPRT status code.
 * @param   pThis           The CodeView debug info reader instance.
 * @param   pvSubSect       Pointer to the subsection data.
 * @param   cbSubSect       The size of the subsection data.
 * @param   pDirEnt         The directory entry.
 */
typedef DECLCALLBACKTYPE(int, FNDBGMODCVSUBSECTCALLBACK,(PRTDBGMODCV pThis, void const *pvSubSect, size_t cbSubSect,
                                                         PCRTCVDIRENT32 pDirEnt));
/** Pointer to a subsection callback. */
typedef FNDBGMODCVSUBSECTCALLBACK *PFNDBGMODCVSUBSECTCALLBACK;



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Light weight assert + return w/ fixed status code. */
#define RTDBGMODCV_CHECK_RET_BF(a_Expr, a_LogArgs) \
    do { \
        if (!(a_Expr)) \
        { \
            Log(("RTDbgCv: Check failed on line %d: " #a_Expr "\n", __LINE__)); \
            Log(a_LogArgs); \
            /*AssertFailed();*/ \
            return VERR_CV_BAD_FORMAT; \
        } \
    } while (0)


/** Light weight assert + return w/ fixed status code. */
#define RTDBGMODCV_CHECK_NOMSG_RET_BF(a_Expr) \
    do { \
        if (!(a_Expr)) \
        { \
            Log(("RTDbgCv: Check failed on line %d: " #a_Expr "\n", __LINE__)); \
            /*AssertFailed();*/ \
            return VERR_CV_BAD_FORMAT; \
        } \
    } while (0)



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtDbgModPdb_CommonOpenWorker(PRTDBGMODCV pThis, const char *pszFilename, RTVFS hVfsPdb, RTDBGCFG hDbgCfg);



/**
 * Reads CodeView information.
 *
 * @returns IPRT status code.
 * @param   pThis               The CodeView reader instance.
 * @param   off                 The offset to start reading at, relative to the
 *                              CodeView base header.
 * @param   pvBuf               The buffer to read into.
 * @param   cb                  How many bytes to read.
 */
static int rtDbgModCvReadAt(PRTDBGMODCV pThis, uint32_t off, void *pvBuf, size_t cb)
{
    int rc;
    if (pThis->hFile == NIL_RTFILE)
        rc = pThis->pMod->pImgVt->pfnReadAt(pThis->pMod, UINT32_MAX, off + pThis->offBase, pvBuf, cb);
    else
        rc = RTFileReadAt(pThis->hFile, off + pThis->offBase, pvBuf, cb, NULL);
    return rc;
}


/**
 * Reads CodeView information into an allocated buffer.
 *
 * @returns IPRT status code.
 * @param   pThis               The CodeView reader instance.
 * @param   off                 The offset to start reading at, relative to the
 *                              CodeView base header.
 * @param   ppvBuf              Where to return the allocated buffer on success.
 * @param   cb                  How many bytes to read.
 */
static int rtDbgModCvReadAtAlloc(PRTDBGMODCV pThis, uint32_t off, void **ppvBuf, size_t cb)
{
    int   rc;
    void *pvBuf = *ppvBuf = RTMemAlloc(cb);
    if (pvBuf)
    {
        if (pThis->hFile == NIL_RTFILE)
            rc = pThis->pMod->pImgVt->pfnReadAt(pThis->pMod, UINT32_MAX, off + pThis->offBase, pvBuf, cb);
        else
            rc = RTFileReadAt(pThis->hFile, off + pThis->offBase, pvBuf, cb, NULL);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;

        RTMemFree(pvBuf);
        *ppvBuf = NULL;
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


#ifdef LOG_ENABLED
/**
 * Gets a name string for a subsection type.
 *
 * @returns Section name (read only).
 * @param   uSubSectType    The subsection type.
 */
static const char *rtDbgModCvGetSubSectionName(uint16_t uSubSectType)
{
    switch (uSubSectType)
    {
        case kCvSst_OldModule:      return "sstOldModule";
        case kCvSst_OldPublic:      return "sstOldPublic";
        case kCvSst_OldTypes:       return "sstOldTypes";
        case kCvSst_OldSymbols:     return "sstOldSymbols";
        case kCvSst_OldSrcLines:    return "sstOldSrcLines";
        case kCvSst_OldLibraries:   return "sstOldLibraries";
        case kCvSst_OldImports:     return "sstOldImports";
        case kCvSst_OldCompacted:   return "sstOldCompacted";
        case kCvSst_OldSrcLnSeg:    return "sstOldSrcLnSeg";
        case kCvSst_OldSrcLines3:   return "sstOldSrcLines3";

        case kCvSst_Module:         return "sstModule";
        case kCvSst_Types:          return "sstTypes";
        case kCvSst_Public:         return "sstPublic";
        case kCvSst_PublicSym:      return "sstPublicSym";
        case kCvSst_Symbols:        return "sstSymbols";
        case kCvSst_AlignSym:       return "sstAlignSym";
        case kCvSst_SrcLnSeg:       return "sstSrcLnSeg";
        case kCvSst_SrcModule:      return "sstSrcModule";
        case kCvSst_Libraries:      return "sstLibraries";
        case kCvSst_GlobalSym:      return "sstGlobalSym";
        case kCvSst_GlobalPub:      return "sstGlobalPub";
        case kCvSst_GlobalTypes:    return "sstGlobalTypes";
        case kCvSst_MPC:            return "sstMPC";
        case kCvSst_SegMap:         return "sstSegMap";
        case kCvSst_SegName:        return "sstSegName";
        case kCvSst_PreComp:        return "sstPreComp";
        case kCvSst_PreCompMap:     return "sstPreCompMap";
        case kCvSst_OffsetMap16:    return "sstOffsetMap16";
        case kCvSst_OffsetMap32:    return "sstOffsetMap32";
        case kCvSst_FileIndex:      return "sstFileIndex";
        case kCvSst_StaticSym:      return "sstStaticSym";
    }
    static char s_sz[32];
    RTStrPrintf(s_sz, sizeof(s_sz), "Unknown%#x", uSubSectType);
    return s_sz;
}
#endif /* LOG_ENABLED */


#ifdef LOG_ENABLED
/**
 * Gets a name string for a symbol type.
 *
 * @returns symbol type name (read only).
 * @param   enmSymType      The symbol type to name.
 */
static const char *rtDbgModCvSsSymTypeName(RTCVSYMTYPE enmSymType)
{
    switch (enmSymType)
    {
# define CASE_RET_STR(Name)  case kCvSymType_##Name: return #Name;
        CASE_RET_STR(Compile);
        CASE_RET_STR(Register);
        CASE_RET_STR(Constant);
        CASE_RET_STR(UDT);
        CASE_RET_STR(SSearch);
        CASE_RET_STR(End);
        CASE_RET_STR(Skip);
        CASE_RET_STR(CVReserve);
        CASE_RET_STR(ObjName);
        CASE_RET_STR(EndArg);
        CASE_RET_STR(CobolUDT);
        CASE_RET_STR(ManyReg);
        CASE_RET_STR(Return);
        CASE_RET_STR(EntryThis);
        CASE_RET_STR(BpRel16);
        CASE_RET_STR(LData16);
        CASE_RET_STR(GData16);
        CASE_RET_STR(Pub16);
        CASE_RET_STR(LProc16);
        CASE_RET_STR(GProc16);
        CASE_RET_STR(Thunk16);
        CASE_RET_STR(BLock16);
        CASE_RET_STR(With16);
        CASE_RET_STR(Label16);
        CASE_RET_STR(CExModel16);
        CASE_RET_STR(VftPath16);
        CASE_RET_STR(RegRel16);
        CASE_RET_STR(BpRel32);
        CASE_RET_STR(LData32);
        CASE_RET_STR(GData32);
        CASE_RET_STR(Pub32);
        CASE_RET_STR(LProc32);
        CASE_RET_STR(GProc32);
        CASE_RET_STR(Thunk32);
        CASE_RET_STR(Block32);
        CASE_RET_STR(With32);
        CASE_RET_STR(Label32);
        CASE_RET_STR(CExModel32);
        CASE_RET_STR(VftPath32);
        CASE_RET_STR(RegRel32);
        CASE_RET_STR(LThread32);
        CASE_RET_STR(GThread32);
        CASE_RET_STR(LProcMips);
        CASE_RET_STR(GProcMips);
        CASE_RET_STR(ProcRef);
        CASE_RET_STR(DataRef);
        CASE_RET_STR(Align);
        CASE_RET_STR(LProcRef);
        CASE_RET_STR(V2_Register);
        CASE_RET_STR(V2_Constant);
        CASE_RET_STR(V2_Udt);
        CASE_RET_STR(V2_CobolUdt);
        CASE_RET_STR(V2_ManyReg);
        CASE_RET_STR(V2_BpRel);
        CASE_RET_STR(V2_LData);
        CASE_RET_STR(V2_GData);
        CASE_RET_STR(V2_Pub);
        CASE_RET_STR(V2_LProc);
        CASE_RET_STR(V2_GProc);
        CASE_RET_STR(V2_VftTable);
        CASE_RET_STR(V2_RegRel);
        CASE_RET_STR(V2_LThread);
        CASE_RET_STR(V2_GThread);
        CASE_RET_STR(V2_LProcMips);
        CASE_RET_STR(V2_GProcMips);
        CASE_RET_STR(V2_FrameInfo);
        CASE_RET_STR(V2_Compliand);
        CASE_RET_STR(V3_Compliand);
        CASE_RET_STR(V3_Thunk);
        CASE_RET_STR(V3_Block);
        CASE_RET_STR(V3_Unknown_1104);
        CASE_RET_STR(V3_Label);
        CASE_RET_STR(V3_Register);
        CASE_RET_STR(V3_Constant);
        CASE_RET_STR(V3_Udt);
        CASE_RET_STR(V3_Unknown_1109);
        CASE_RET_STR(V3_Unknown_110a);
        CASE_RET_STR(V3_BpRel);
        CASE_RET_STR(V3_LData);
        CASE_RET_STR(V3_GData);
        CASE_RET_STR(V3_Pub);
        CASE_RET_STR(V3_LProc);
        CASE_RET_STR(V3_GProc);
        CASE_RET_STR(V3_RegRel);
        CASE_RET_STR(V3_LThread);
        CASE_RET_STR(V3_GThread);
        CASE_RET_STR(V3_Unknown_1114);
        CASE_RET_STR(V3_Unknown_1115);
        CASE_RET_STR(V3_MSTool);
        CASE_RET_STR(V3_ProcRef);
        CASE_RET_STR(V3_LProcRef);
        CASE_RET_STR(V3_Unknown_1128);
        CASE_RET_STR(V3_SectInfo);
        CASE_RET_STR(V3_SubSectInfo);
        CASE_RET_STR(V3_Entrypoint);
        CASE_RET_STR(V3_Unknown_1139);
        CASE_RET_STR(V3_SecuCookie);
        CASE_RET_STR(V3_Unknown_113b);
        CASE_RET_STR(V3_MsToolInfo);
        CASE_RET_STR(V3_MsToolEnv);
        CASE_RET_STR(VS2013_Local);
        CASE_RET_STR(VS2013_FpOff);
        CASE_RET_STR(VS2013_LProc32);
        CASE_RET_STR(VS2013_GProc32);
#undef CASE_RET_STR
        case kCvSymType_EndOfValues: break;
    }
    return "<unknown type>";
}
#endif /* LOG_ENABLED */


/**
 * Adds a string to the g_hDbgModStrCache after sanitizing it.
 *
 * IPRT only deals with UTF-8 strings, so the string will be forced to UTF-8
 * encoding.  Also, codeview generally have length prefixed
 *
 * @returns String cached copy of the string.
 * @param   pch                 The string to copy to the cache.
 * @param   cch                 The length of the string.  RTSTR_MAX if zero
 *                              terminated.
 */
static const char *rtDbgModCvAddSanitizedStringToCache(const char *pch, size_t cch)
{
    /*
     * If the string is valid UTF-8 and or the right length, we're good.
     * This is usually the case.
     */
    const char *pszRet;
    int rc;
    if (cch != RTSTR_MAX)
        rc = RTStrValidateEncodingEx(pch, cch, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    else
        rc = RTStrValidateEncodingEx(pch, cch, 0);
    if (RT_SUCCESS(rc))
        pszRet = RTStrCacheEnterN(g_hDbgModStrCache, pch, cch);
    else
    {
        /*
         * Need to sanitize the string, so make a copy of it.
         */
        char *pszCopy = (char *)RTMemDupEx(pch, cch, 1);
        AssertPtrReturn(pszCopy, NULL);

        /* Deal with anyembedded zero chars. */
        char *psz = RTStrEnd(pszCopy, cch);
        while (psz)
        {
            *psz = '_';
            psz = RTStrEnd(psz, cch - (psz - pszCopy));
        }

        /* Force valid UTF-8 encoding. */
        RTStrPurgeEncoding(pszCopy);
        Assert(strlen(pszCopy) == cch);

        /* Enter it into the cache and free the temp copy. */
        pszRet = RTStrCacheEnterN(g_hDbgModStrCache, pszCopy, cch);
        RTMemFree(pszCopy);
    }
    return pszRet;
}


/**
 * Translates a codeview segment and offset into our segment layout.
 *
 * @returns
 * @param   pThis               .
 * @param   piSeg               .
 * @param   poff                .
 */
DECLINLINE(int) rtDbgModCvAdjustSegAndOffset(PRTDBGMODCV pThis, uint32_t *piSeg, uint64_t *poff)
{
    uint32_t iSeg = *piSeg;
    if (iSeg == 0)
        iSeg = RTDBGSEGIDX_ABS;
    else if (pThis->pSegMap)
    {
        if (pThis->fHaveDosFrames)
        {
            if (   iSeg > pThis->pSegMap->Hdr.cSegs
                || iSeg == 0)
                return VERR_CV_BAD_FORMAT;
            if (*poff <= pThis->pSegMap->aDescs[iSeg - 1].cb + pThis->pSegMap->aDescs[iSeg - 1].off)
                *poff -= pThis->pSegMap->aDescs[iSeg - 1].off;
            else
            {
                /* Workaround for VGABIOS where _DATA symbols like vgafont8 are
                   reported in the VGAROM segment. */
                uint64_t uAddrSym = *poff + ((uint32_t)pThis->pSegMap->aDescs[iSeg - 1].iFrame << 4);
                uint16_t j = pThis->pSegMap->Hdr.cSegs;
                while (j-- > 0)
                {
                    uint64_t uAddrFirst = (uint64_t)pThis->pSegMap->aDescs[j].off
                                        + ((uint32_t)pThis->pSegMap->aDescs[j].iFrame << 4);
                    if (uAddrSym - uAddrFirst < pThis->pSegMap->aDescs[j].cb)
                    {
                        Log(("CV addr fix: %04x:%08x -> %04x:%08x\n", iSeg, *poff, j + 1, uAddrSym - uAddrFirst));
                        *poff  = uAddrSym - uAddrFirst;
                        iSeg = j + 1;
                        break;
                    }
                }
                if (j == UINT16_MAX)
                    return VERR_CV_BAD_FORMAT;
            }
        }
        else
        {
            if (   iSeg > pThis->pSegMap->Hdr.cSegs
                || iSeg == 0
                || *poff > pThis->pSegMap->aDescs[iSeg - 1].cb)
                return VERR_CV_BAD_FORMAT;
            *poff += pThis->pSegMap->aDescs[iSeg - 1].off;
        }
        if (pThis->pSegMap->aDescs[iSeg - 1].fFlags & RTCVSEGMAPDESC_F_ABS)
            iSeg = RTDBGSEGIDX_ABS;
        else
            iSeg = pThis->pSegMap->aDescs[iSeg - 1].iGroup;
    }
    *piSeg = iSeg;
    return VINF_SUCCESS;
}


/**
 * Adds a symbol to the container.
 *
 * @returns IPRT status code
 * @param   pThis               The CodeView debug info reader instance.
 * @param   iSeg                Segment number.
 * @param   off                 Offset into the segment
 * @param   pchName             The symbol name (not necessarily terminated).
 * @param   cchName             The symbol name length.
 * @param   fFlags              Flags reserved for future exploits, MBZ.
 * @param   cbSym               Symbol size, 0 if not available.
 */
static int rtDbgModCvAddSymbol(PRTDBGMODCV pThis, uint32_t iSeg, uint64_t off, const char *pchName,
                               uint32_t cchName, uint32_t fFlags, uint32_t cbSym)
{
    RT_NOREF_PV(fFlags);
    const char *pszName = rtDbgModCvAddSanitizedStringToCache(pchName, cchName);
    int rc;
    if (pszName)
    {
#if 1
        Log2(("CV Sym: %04x:%08x %.*s\n", iSeg, off, cchName, pchName));
        rc = rtDbgModCvAdjustSegAndOffset(pThis, &iSeg, &off);
        if (RT_SUCCESS(rc))
        {
            rc = RTDbgModSymbolAdd(pThis->hCnt, pszName, iSeg, off, cbSym, RTDBGSYMBOLADD_F_ADJUST_SIZES_ON_CONFLICT, NULL);

            /* Simple duplicate symbol mangling, just to get more details. */
            if (rc == VERR_DBG_DUPLICATE_SYMBOL && cchName < _2K)
            {
                char szTmpName[_2K + 96];
                memcpy(szTmpName, pszName, cchName);
                szTmpName[cchName] = '_';
                for (uint32_t i = 1; i < 32; i++)
                {
                    RTStrFormatU32(&szTmpName[cchName + 1], 80, i, 10, 0, 0, 0);
                    rc = RTDbgModSymbolAdd(pThis->hCnt, szTmpName, iSeg, off, cbSym, 0 /*fFlags*/, NULL);
                    if (rc != VERR_DBG_DUPLICATE_SYMBOL)
                        break;
                }

            }
            else if (rc == VERR_DBG_ADDRESS_CONFLICT && cbSym)
                rc = RTDbgModSymbolAdd(pThis->hCnt, pszName, iSeg, off, cbSym,
                                       RTDBGSYMBOLADD_F_REPLACE_SAME_ADDR | RTDBGSYMBOLADD_F_ADJUST_SIZES_ON_CONFLICT, NULL);

            Log(("Symbol: %04x:%08x %.*s [%Rrc]\n", iSeg, off, cchName, pchName, rc));
            if (rc == VERR_DBG_ADDRESS_CONFLICT || rc == VERR_DBG_DUPLICATE_SYMBOL)
                rc = VINF_SUCCESS;
        }
        else
            Log(("Invalid segment index/offset %#06x:%08x for symbol %.*s\n", iSeg, off, cchName, pchName));

#else
        Log(("Symbol: %04x:%08x %.*s\n", iSeg, off, cchName, pchName));
        rc = VINF_SUCCESS;
#endif
        RTStrCacheRelease(g_hDbgModStrCache, pszName);
    }
    else
        rc = VERR_NO_STR_MEMORY;
    return rc;
}

/**
 * Validates the a length prefixed string (aka pascal string).
 *
 * @returns String length if valid, UINT16_MAX if invalid.
 * @param   cchString   The string length.
 * @param   pchString   The string to validate.
 * @param   pvRec       The pointer to the record containing the string.
 * @param   cbRec       The record length.
 */
static uint16_t rtDbgModCvValidatePascalString(uint8_t cchString, const char *pchString, void const *pvRec, uint16_t cbRec)
{
    size_t  offStrMember = (uintptr_t)pchString - (uintptr_t)pvRec;
    AssertReturn(offStrMember <= cbRec, UINT16_MAX);
    cbRec -= (uint16_t)offStrMember;
    AssertReturn(cchString <= cbRec, UINT16_MAX);

    int rc = RTStrValidateEncodingEx(pchString, cchString, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    AssertRCReturn(rc, UINT16_MAX);

    return cchString;
}


/**
 * Validates the a zero terminated string.
 *
 * @returns String length if valid, UINT16_MAX if invalid.
 * @param   pszString   The string to validate.
 * @param   pvRec       The pointer to the record containing the string.
 * @param   cbRec       The record length.
 */
static uint16_t rtDbgModCvValidateZeroString(const char *pszString, void const *pvRec, uint16_t cbRec)
{
    size_t   offStrMember = (uintptr_t)pszString - (uintptr_t)pvRec;
    AssertReturn(offStrMember < _1K, UINT16_MAX);
    AssertReturn(offStrMember <= cbRec, UINT16_MAX);
    cbRec -= (uint16_t)offStrMember;

    const char *pchEnd = RTStrEnd(pszString, cbRec);
    AssertReturn(pchEnd, UINT16_MAX);

    int rc = RTStrValidateEncoding(pszString);
    AssertRCReturn(rc, UINT16_MAX);

    return (uint16_t)(pchEnd - pszString);
}


/**
 * Parses a CV4 symbol table, adding symbols to the container.
 *
 * @returns IPRT status code
 * @param   pThis               The CodeView debug info reader instance.
 * @param   pvSymTab            The symbol table.
 * @param   cbSymTab            The size of the symbol table.
 * @param   fFlags              Flags reserved for future exploits, MBZ.
 */
static int rtDbgModCvSsProcessV4PlusSymTab(PRTDBGMODCV pThis, void const *pvSymTab, size_t cbSymTab, uint32_t fFlags)
{
    int         rc = VINF_SUCCESS;
    RTCPTRUNION uCursor;
    uCursor.pv = pvSymTab;

    RT_NOREF_PV(fFlags);

    while (cbSymTab > 0 && RT_SUCCESS(rc))
    {
        uint8_t const * const pbRecStart = uCursor.pu8;
        uint16_t cbRec = *uCursor.pu16++;
        if (cbRec >= 2)
        {
            uint16_t uSymType = *uCursor.pu16++;

            Log3(("    %p: uSymType=%#06x LB %#x %s\n",
                  pbRecStart - (uint8_t *)pvSymTab, uSymType, cbRec, rtDbgModCvSsSymTypeName((RTCVSYMTYPE)uSymType)));
            RTDBGMODCV_CHECK_RET_BF(cbRec >= 2 && cbRec <= cbSymTab, ("cbRec=%#x cbSymTab=%#x\n", cbRec, cbSymTab));

            switch (uSymType)
            {
                case kCvSymType_LData16:
                case kCvSymType_GData16:
                case kCvSymType_Pub16:
                {
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec > 2 + 2+2+2+1);
                    uint16_t off     = *uCursor.pu16++;
                    uint16_t iSeg    = *uCursor.pu16++;
                    /*uint16_t iType   = * */ uCursor.pu16++;
                    uint8_t  cchName = *uCursor.pu8++;
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cchName > 0);
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec >= 2 + 2+2+2+1 + cchName);

                    rc = rtDbgModCvAddSymbol(pThis, iSeg, off, uCursor.pch, cchName, 0, 0);
                    break;
                }

                case kCvSymType_LData32:
                case kCvSymType_GData32:
                case kCvSymType_Pub32:
                {
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec > 2 + 4+2+2+1);
                    uint32_t off     = *uCursor.pu32++;
                    uint16_t iSeg    = *uCursor.pu16++;
                    /*uint16_t iType   = * */ uCursor.pu16++;
                    uint8_t  cchName = *uCursor.pu8++;
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cchName > 0);
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec >= 2 + 4+2+2+1 + cchName);

                    rc = rtDbgModCvAddSymbol(pThis, iSeg, off, uCursor.pch, cchName, 0, 0);
                    break;
                }

                case kCvSymType_LProc16:
                case kCvSymType_GProc16:
                {
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec > 2 + 4+4+4+2+2+2+2+2+2+1+1);
                    /*uint32_t uParent       = * */ uCursor.pu32++;
                    /*uint32_t uEnd          = * */ uCursor.pu32++;
                    /*uint32_t uNext         = * */ uCursor.pu32++;
                    uint16_t cbProc        = *uCursor.pu16++;
                    /*uint16_t offDebugStart = * */ uCursor.pu16++;
                    /*uint16_t offDebugEnd   = * */ uCursor.pu16++;
                    uint16_t off           = *uCursor.pu16++;
                    uint16_t iSeg          = *uCursor.pu16++;
                    /*uint16_t iProcType     = * */ uCursor.pu16++;
                    /*uint8_t fbType         = * */ uCursor.pu8++;
                    uint8_t  cchName       = *uCursor.pu8++;
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cchName > 0);
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec >= 2 + 4+4+4+2+2+2+2+2+2+1+1 + cchName);

                    rc = rtDbgModCvAddSymbol(pThis, iSeg, off, uCursor.pch, cchName, 0, cbProc);
                    break;
                }

                case kCvSymType_LProc32:
                case kCvSymType_GProc32:
                {
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec > 2 + 4+4+4+4+4+4+4+2+2+1+1);
                    /*uint32_t uParent       = * */ uCursor.pu32++;
                    /*uint32_t uEnd          = * */ uCursor.pu32++;
                    /*uint32_t uNext         = * */ uCursor.pu32++;
                    /*uint32_t cbProc        = * */ uCursor.pu32++;
                    /*uint32_t offDebugStart = * */ uCursor.pu32++;
                    /*uint32_t offDebugEnd   = * */ uCursor.pu32++;
                    uint32_t off           = *uCursor.pu32++;
                    uint16_t iSeg          = *uCursor.pu16++;
                    /*uint16_t iProcType     = * */ uCursor.pu16++;
                    /*uint8_t fbType         = * */ uCursor.pu8++;
                    uint8_t  cchName       = *uCursor.pu8++;
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cchName > 0);
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec >= 2 + 4+4+4+4+4+4+4+2+2+1+1 + cchName);

                    rc = rtDbgModCvAddSymbol(pThis, iSeg, off, uCursor.pch, cchName, 0, 0);
                    break;
                }

                case kCvSymType_V2_Pub:
                case kCvSymType_V2_LData:
                case kCvSymType_V2_GData:
                {
                    PCRTCVSYMV2TYPEDNAME pData = (PCRTCVSYMV2TYPEDNAME)uCursor.pv;
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec >= 2 + RT_UOFFSETOF(RTCVSYMV2TYPEDNAME, achName));
                    uint16_t const cchName = rtDbgModCvValidatePascalString(pData->cchName, pData->achName, pData, cbRec);
                    if (cchName != UINT16_MAX && cchName > 0)
                        rc = rtDbgModCvAddSymbol(pThis, pData->iSection, pData->offSection, pData->achName, cchName, 0, 0);
                    else
                        Log3(("      cchName=%#x sec:off=%#x:%#x %.*Rhxs\n",
                              cchName, pData->iSection, pData->offSection, cbRec, pData));
                    break;
                }

                case kCvSymType_V3_Label:
                {
                    PCRTCVSYMV3LABEL pLabel = (PCRTCVSYMV3LABEL)uCursor.pv;
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec >= 2 + sizeof(*pLabel));
                    uint16_t cchName = rtDbgModCvValidateZeroString(pLabel->szName, pLabel, cbRec);
                    if (cchName != UINT16_MAX && cchName > 0)
                        rc = rtDbgModCvAddSymbol(pThis, pLabel->iSection, pLabel->offSection, pLabel->szName, cchName, 0, 0);
                    else
                        Log3(("      cchName=%#x sec:off=%#x:%#x %.*Rhxs\n",
                              cchName, pLabel->iSection, pLabel->offSection, cbRec, pLabel));
                    break;
                }

                case kCvSymType_V3_LData:
                case kCvSymType_V3_GData:
                case kCvSymType_V3_Pub:
                {
                    PCRTCVSYMV3TYPEDNAME pData = (PCRTCVSYMV3TYPEDNAME)uCursor.pv;
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec >= sizeof(*pData));
                    uint16_t cchName = rtDbgModCvValidateZeroString(pData->szName, pData, cbRec);
                    if (cchName != UINT16_MAX && cchName > 0)
                        rc = rtDbgModCvAddSymbol(pThis, pData->iSection, pData->offSection, pData->szName, cchName, 0, 0);
                    else
                        Log3(("      cchName=%#x sec:off=%#x:%#x idType=%#x %.*Rhxs\n",
                              cchName, pData->iSection, pData->offSection, pData->idType, cbRec, pData));
                    break;
                }

                case kCvSymType_V3_LProc:
                case kCvSymType_V3_GProc:
                {
                    PCRTCVSYMV3PROC pProc = (PCRTCVSYMV3PROC)uCursor.pv;
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbRec >= sizeof(*pProc));
                    uint16_t cchName = rtDbgModCvValidateZeroString(pProc->szName, pProc, cbRec);
                    if (cchName != UINT16_MAX && cchName > 0)
                        rc = rtDbgModCvAddSymbol(pThis, pProc->iSection, pProc->offSection, pProc->szName, cchName,
                                                 0, pProc->cbProc);
                    else
                        Log3(("      cchName=%#x sec:off=%#x:%#x LB %#x\n",
                              cchName, pProc->iSection, pProc->offSection, pProc->cbProc));
                    break;
                }

            }
        }
        /*else: shorter records can be used for alignment, I guess. */

        /* next */
        uCursor.pu8 = pbRecStart + cbRec + 2;
        cbSymTab   -= cbRec + 2;
    }
    return rc;
}


/**
 * Makes a copy of the CV8 source string table.
 *
 * It will be references in a subsequent source information table, and again by
 * line number tables thru that.
 *
 * @returns IPRT status code
 * @param   pThis               The CodeView debug info reader instance.
 * @param   pvSrcStrings        The source string table.
 * @param   cbSrcStrings        The size of the source strings.
 * @param   fFlags              Flags reserved for future exploits, MBZ.
 */
static int rtDbgModCvSsProcessV8SrcStrings(PRTDBGMODCV pThis, void const *pvSrcStrings, size_t cbSrcStrings, uint32_t fFlags)
{
    RT_NOREF_PV(fFlags);

    if (pThis->cbSrcStrings)
        Log(("\n!!More than one source file string table for this module!!\n\n"));

    if (cbSrcStrings >= pThis->cbSrcStringsAlloc)
    {
        void *pvNew = RTMemRealloc(pThis->pchSrcStrings, cbSrcStrings + 1);
        AssertReturn(pvNew, VERR_NO_MEMORY);
        pThis->pchSrcStrings     = (char *)pvNew;
        pThis->cbSrcStringsAlloc = cbSrcStrings + 1;
    }
    memcpy(pThis->pchSrcStrings, pvSrcStrings, cbSrcStrings);
    pThis->pchSrcStrings[cbSrcStrings] = '\0';
    pThis->cbSrcStrings = cbSrcStrings;
    Log2(("    saved %#x bytes of CV8 source strings\n", cbSrcStrings));

    if (LogIs3Enabled())
    {
        size_t iFile = 0;
        size_t off   = pThis->pchSrcStrings[0] != '\0' ? 0 : 1;
        while (off < cbSrcStrings)
        {
            size_t cch = strlen(&pThis->pchSrcStrings[off]);
            Log3(("  %010zx #%03zu: %s\n", off, iFile, &pThis->pchSrcStrings[off]));
            off += cch + 1;
            iFile++;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Makes a copy of the CV8 source information table.
 *
 * It will be references in subsequent line number tables.
 *
 * @returns IPRT status code
 * @param   pThis       The CodeView debug info reader instance.
 * @param   pvSrcInfo   The source information table.
 * @param   cbSrcInfo   The size of the source information table (bytes).
 * @param   fFlags      Flags reserved for future exploits, MBZ.
 */
static int rtDbgModCvSsProcessV8SrcInfo(PRTDBGMODCV pThis, void const *pvSrcInfo, size_t cbSrcInfo, uint32_t fFlags)
{
    RT_NOREF_PV(fFlags);

    if (pThis->cbSrcInfo)
        Log(("\n!!More than one source file info table for this module!!\n\n"));

    if (cbSrcInfo + sizeof(RTCV8SRCINFO) > pThis->cbSrcInfoAlloc)
    {
        void *pvNew = RTMemRealloc(pThis->pbSrcInfo, cbSrcInfo + sizeof(RTCV8SRCINFO));
        AssertReturn(pvNew, VERR_NO_MEMORY);
        pThis->pbSrcInfo      = (uint8_t *)pvNew;
        pThis->cbSrcInfoAlloc = cbSrcInfo + sizeof(RTCV8SRCINFO);
    }
    memcpy(pThis->pbSrcInfo, pvSrcInfo, cbSrcInfo);
    memset(&pThis->pbSrcInfo[cbSrcInfo], 0, sizeof(RTCV8SRCINFO));
    pThis->cbSrcInfo = cbSrcInfo;
    Log2(("    saved %#x bytes of CV8 source file info\n", cbSrcInfo));
    return VINF_SUCCESS;
}


/**
 * Makes a copy of the CV8 source string table.
 *
 * It will be references in subsequent line number tables.
 *
 * @returns IPRT status code
 * @param   pThis               The CodeView debug info reader instance.
 * @param   pvSectLines         The section source line table.
 * @param   cbSectLines         The size of the section source line table.
 * @param   fFlags              Flags reserved for future exploits, MBZ.
 */
static int rtDbgModCvSsProcessV8SectLines(PRTDBGMODCV pThis, void const *pvSectLines, size_t cbSectLines, uint32_t fFlags)
{
    RT_NOREF_PV(fFlags);

    /*
     * Starts with header.
     */
    PCRTCV8LINESHDR pHdr = (PCRTCV8LINESHDR)pvSectLines;
    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbSectLines >= sizeof(*pHdr));
    cbSectLines -= sizeof(*pHdr);
    Log2(("RTDbgModCv:     seg #%u, off %#x LB %#x \n", pHdr->iSection, pHdr->offSection, pHdr->cbSectionCovered));

    RTCPTRUNION uCursor;
    uCursor.pv = pHdr + 1;
    while (cbSectLines > 0)
    {
        /* Source file header. */
        PCRTCV8LINESSRCMAP pSrcHdr = (PCRTCV8LINESSRCMAP)uCursor.pv;
        RTDBGMODCV_CHECK_NOMSG_RET_BF(cbSectLines >= sizeof(*pSrcHdr));
        RTDBGMODCV_CHECK_NOMSG_RET_BF(pSrcHdr->cb == pSrcHdr->cLines * sizeof(RTCV8LINEPAIR) + sizeof(RTCV8LINESSRCMAP));
        RTDBGMODCV_CHECK_RET_BF(!(pSrcHdr->offSourceInfo & 3), ("offSourceInfo=%#x\n", pSrcHdr->offSourceInfo));
        if (pSrcHdr->offSourceInfo + sizeof(uint32_t) <= pThis->cbSrcInfo)
        {
            PCRTCV8SRCINFO pSrcInfo = (PCRTCV8SRCINFO)&pThis->pbSrcInfo[pSrcHdr->offSourceInfo];
            const char    *pszName  = pSrcInfo->offSourceName < pThis->cbSrcStrings
                                    ? &pThis->pchSrcStrings[pSrcInfo->offSourceName] : "unknown.c";
            pszName = rtDbgModCvAddSanitizedStringToCache(pszName, RTSTR_MAX);
            Log2(("RTDbgModCv:     #%u lines, %#x bytes, %#x=%s\n", pSrcHdr->cLines, pSrcHdr->cb, pSrcInfo->offSourceName, pszName));

            if (pszName)
            {
                /* Process the line/offset pairs. */
                uint32_t        cLeft = pSrcHdr->cLines;
                PCRTCV8LINEPAIR pPair = (PCRTCV8LINEPAIR)(pSrcHdr + 1);
                while (cLeft-- > 0)
                {
                    uint32_t idxSeg = pHdr->iSection;
                    uint64_t off    = pPair->offSection + pHdr->offSection;
                    int rc = rtDbgModCvAdjustSegAndOffset(pThis, &idxSeg, &off);
                    if (RT_SUCCESS(rc))
                        rc = RTDbgModLineAdd(pThis->hCnt, pszName, pPair->uLineNumber, idxSeg, off, NULL);
                    if (RT_SUCCESS(rc))
                        Log3(("RTDbgModCv:       %#x:%#010llx  %0u\n", idxSeg, off, pPair->uLineNumber));
                    else
                        Log(( "RTDbgModCv:       %#x:%#010llx  %0u - rc=%Rrc!! (org: idxSeg=%#x off=%#x)\n",
                              idxSeg, off, pPair->uLineNumber, rc, pHdr->iSection, pPair->offSection));

                    /* next */
                    pPair++;
                }
                Assert((uintptr_t)pPair - (uintptr_t)pSrcHdr == pSrcHdr->cb);
            }
        }
        else
            Log(("RTDbgModCv: offSourceInfo=%#x cbSrcInfo=%#x!\n", pSrcHdr->offSourceInfo, pThis->cbSrcInfo));

        /* next */
        cbSectLines -= pSrcHdr->cb;
        uCursor.pu8 += pSrcHdr->cb;
    }

    return VINF_SUCCESS;
}



/**
 * Parses a CV8 symbol table, adding symbols to the container.
 *
 * @returns IPRT status code
 * @param   pThis               The CodeView debug info reader instance.
 * @param   pvSymTab            The symbol table.
 * @param   cbSymTab            The size of the symbol table.
 * @param   fFlags              Flags reserved for future exploits, MBZ.
 */
static int rtDbgModCvSsProcessV8SymTab(PRTDBGMODCV pThis, void const *pvSymTab, size_t cbSymTab, uint32_t fFlags)
{
    size_t const    cbSymTabSaved = cbSymTab;
    int             rc = VINF_SUCCESS;

    /*
     * First pass looks for source information and source strings tables.
     * Microsoft puts the 0xf3 and 0xf4 last, usually with 0xf4 first.
     *
     * We ASSUME one string and one info table per module!
     */
    RTCPTRUNION uCursor;
    uCursor.pv = pvSymTab;
    for (;;)
    {
        RTDBGMODCV_CHECK_RET_BF(cbSymTab > sizeof(RTCV8SYMBOLSBLOCK), ("cbSymTab=%zu\n", cbSymTab));
        PCRTCV8SYMBOLSBLOCK pBlockHdr = (PCRTCV8SYMBOLSBLOCK)uCursor.pv;
        Log3(("  %p: pass #1 uType=%#04x LB %#x\n", (uint8_t *)pBlockHdr - (uint8_t *)pvSymTab, pBlockHdr->uType, pBlockHdr->cb));
        RTDBGMODCV_CHECK_RET_BF(pBlockHdr->cb <= cbSymTab - sizeof(RTCV8SYMBOLSBLOCK),
                                ("cb=%#u cbSymTab=%zu\n", pBlockHdr->cb, cbSymTab));

        switch (pBlockHdr->uType)
        {
            case RTCV8SYMBLOCK_TYPE_SRC_STR:
                rc = rtDbgModCvSsProcessV8SrcStrings(pThis, pBlockHdr + 1, pBlockHdr->cb, fFlags);
                break;

            case RTCV8SYMBLOCK_TYPE_SRC_INFO:
                rc = rtDbgModCvSsProcessV8SrcInfo(pThis, pBlockHdr + 1, pBlockHdr->cb, fFlags);
                break;

            case RTCV8SYMBLOCK_TYPE_SECT_LINES:
            case RTCV8SYMBLOCK_TYPE_SYMBOLS:
                break;
            default:
                Log(("rtDbgModCvSsProcessV8SymTab: Unknown block type %#x (LB %#x)\n", pBlockHdr->uType, pBlockHdr->cb));
                break;
        }
        uint32_t cbAligned = RT_ALIGN_32(sizeof(*pBlockHdr) + pBlockHdr->cb, 4);
        if (RT_SUCCESS(rc) && cbSymTab > cbAligned)
        {
            uCursor.pu8 += cbAligned;
            cbSymTab    -= cbAligned;
        }
        else
            break;
    }

    /*
     * Log the source info now that we've gathered both it and the strings.
     */
    if (LogIs3Enabled() && pThis->cbSrcInfo)
    {
        Log3(("    Source file info table:\n"));
        size_t iFile = 0;
        size_t off   = 0;
        while (off + 4 <= pThis->cbSrcInfo)
        {
            PCRTCV8SRCINFO pSrcInfo = (PCRTCV8SRCINFO)&pThis->pbSrcInfo[off];
#ifdef LOG_ENABLED
            const char    *pszName  = pSrcInfo->offSourceName < pThis->cbSrcStrings
                                    ? &pThis->pchSrcStrings[pSrcInfo->offSourceName] : "out-of-bounds.c!";
            if (pSrcInfo->uDigestType == RTCV8SRCINFO_DIGEST_TYPE_MD5)
                Log3(("    %010zx #%03zu: %RTuuid %#x=%s\n", off, iFile, &pSrcInfo->Digest.md5, pSrcInfo->offSourceName, pszName));
            else if (pSrcInfo->uDigestType == RTCV8SRCINFO_DIGEST_TYPE_NONE)
                Log3(("    %010zx #%03zu: <none> %#x=%s\n", off, iFile, pSrcInfo->offSourceName, pszName));
            else
                Log3(("    %010zx #%03zu: !%#x! %#x=%s\n", off, iFile, pSrcInfo->uDigestType, pSrcInfo->offSourceName, pszName));
#endif
            off += pSrcInfo->uDigestType == RTCV8SRCINFO_DIGEST_TYPE_MD5 ? sizeof(*pSrcInfo) : 8;
            iFile++;
        }
    }

    /*
     * Second pass, process symbols and line numbers.
     */
    uCursor.pv = pvSymTab;
    cbSymTab = cbSymTabSaved;
    for (;;)
    {
        RTDBGMODCV_CHECK_RET_BF(cbSymTab > sizeof(RTCV8SYMBOLSBLOCK), ("cbSymTab=%zu\n", cbSymTab));
        PCRTCV8SYMBOLSBLOCK pBlockHdr = (PCRTCV8SYMBOLSBLOCK)uCursor.pv;
        Log3(("  %p: pass #2 uType=%#04x LB %#x\n", (uint8_t *)pBlockHdr - (uint8_t *)pvSymTab, pBlockHdr->uType, pBlockHdr->cb));
        RTDBGMODCV_CHECK_RET_BF(pBlockHdr->cb <= cbSymTab - sizeof(RTCV8SYMBOLSBLOCK),
                                ("cb=%#u cbSymTab=%zu\n", pBlockHdr->cb, cbSymTab));

        switch (pBlockHdr->uType)
        {
            case RTCV8SYMBLOCK_TYPE_SYMBOLS:
                rc = rtDbgModCvSsProcessV4PlusSymTab(pThis, pBlockHdr + 1, pBlockHdr->cb, fFlags);
                break;

            case RTCV8SYMBLOCK_TYPE_SECT_LINES:
                rc = rtDbgModCvSsProcessV8SectLines(pThis, pBlockHdr + 1, pBlockHdr->cb, fFlags);
                break;

            case RTCV8SYMBLOCK_TYPE_SRC_INFO:
            case RTCV8SYMBLOCK_TYPE_SRC_STR:
                break;
            default:
                Log(("rtDbgModCvSsProcessV8SymTab: Unknown block type %#x (LB %#x)\n", pBlockHdr->uType, pBlockHdr->cb));
                break;
        }
        uint32_t cbAligned = RT_ALIGN_32(sizeof(*pBlockHdr) + pBlockHdr->cb, 4);
        if (RT_SUCCESS(rc) && cbSymTab > cbAligned)
        {
            uCursor.pu8 += cbAligned;
            cbSymTab    -= cbAligned;
        }
        else
            break;
    }
    return rc;
}


/** @callback_method_impl{FNDBGMODCVSUBSECTCALLBACK,
 * Parses kCvSst_GlobalPub\, kCvSst_GlobalSym and kCvSst_StaticSym subsections\,
 * adding symbols it finds to the container.} */
static DECLCALLBACK(int)
rtDbgModCvSs_GlobalPub_GlobalSym_StaticSym(PRTDBGMODCV pThis, void const *pvSubSect, size_t cbSubSect, PCRTCVDIRENT32 pDirEnt)
{
    PCRTCVGLOBALSYMTABHDR pHdr = (PCRTCVGLOBALSYMTABHDR)pvSubSect;
    RT_NOREF_PV(pDirEnt);

    /*
     * Quick data validation.
     */
    Log2(("RTDbgModCv: %s: uSymHash=%#x uAddrHash=%#x cbSymbols=%#x cbSymHash=%#x cbAddrHash=%#x\n",
          rtDbgModCvGetSubSectionName(pDirEnt->uSubSectType), pHdr->uSymHash,
          pHdr->uAddrHash, pHdr->cbSymbols, pHdr->cbSymHash, pHdr->cbAddrHash));
    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbSubSect >= sizeof(RTCVGLOBALSYMTABHDR));
    RTDBGMODCV_CHECK_NOMSG_RET_BF((uint64_t)pHdr->cbSymbols + pHdr->cbSymHash + pHdr->cbAddrHash <= cbSubSect - sizeof(*pHdr));
    RTDBGMODCV_CHECK_NOMSG_RET_BF(pHdr->uSymHash  < 0x20);
    RTDBGMODCV_CHECK_NOMSG_RET_BF(pHdr->uAddrHash < 0x20);
    if (!pHdr->cbSymbols)
        return VINF_SUCCESS;

    /*
     * Parse the symbols.
     */
    return rtDbgModCvSsProcessV4PlusSymTab(pThis, pHdr + 1, pHdr->cbSymbols, 0);
}


/** @callback_method_impl{FNDBGMODCVSUBSECTCALLBACK,
 * Parses kCvSst_Module subsection\, storing the debugging style in pThis.} */
static DECLCALLBACK(int)
rtDbgModCvSs_Module(PRTDBGMODCV pThis, void const *pvSubSect, size_t cbSubSect, PCRTCVDIRENT32 pDirEnt)
{
    RT_NOREF_PV(pDirEnt);

    RTCPTRUNION uCursor;
    uCursor.pv = pvSubSect;
    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbSubSect >= 2 + 2 + 2 + 2 + 0 + 1);
    uint16_t iOverlay = *uCursor.pu16++; NOREF(iOverlay);
    uint16_t iLib     = *uCursor.pu16++; NOREF(iLib);
    uint16_t cSegs    = *uCursor.pu16++;
    pThis->uCurStyle  = *uCursor.pu16++;
    if (pThis->uCurStyle == 0)
        pThis->uCurStyle = RT_MAKE_U16('C', 'V');
    pThis->uCurStyleVer = 0;
    pThis->cbSrcInfo    = 0;
    pThis->cbSrcStrings = 0;
    uint8_t cchName   = uCursor.pu8[cSegs * 12];
    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbSubSect >= 2 + 2 + 2 + 2 + cSegs * 12U + 1 + cchName);

#ifdef LOG_ENABLED
    const char *pchName = (const char *)&uCursor.pu8[cSegs * 12 + 1];
    Log2(("RTDbgModCv: Module: iOverlay=%#x iLib=%#x cSegs=%#x Style=%c%c (%#x) %.*s\n", iOverlay, iLib, cSegs,
          RT_BYTE1(pThis->uCurStyle), RT_BYTE2(pThis->uCurStyle), pThis->uCurStyle, cchName, pchName));
#endif
    RTDBGMODCV_CHECK_NOMSG_RET_BF(pThis->uCurStyle == RT_MAKE_U16('C', 'V'));

#ifdef LOG_ENABLED
    PCRTCVMODSEGINFO32 paSegs = (PCRTCVMODSEGINFO32)uCursor.pv;
    for (uint16_t iSeg = 0; iSeg < cSegs; iSeg++)
        Log2(("    #%02u: %04x:%08x LB %08x\n", iSeg, paSegs[iSeg].iSeg, paSegs[iSeg].off, paSegs[iSeg].cb));
#endif

    return VINF_SUCCESS;
}


/** @callback_method_impl{FNDBGMODCVSUBSECTCALLBACK,
 * Parses kCvSst_Symbols\, kCvSst_PublicSym and kCvSst_AlignSym subsections\,
 * adding symbols it finds to the container.} */
static DECLCALLBACK(int)
rtDbgModCvSs_Symbols_PublicSym_AlignSym(PRTDBGMODCV pThis, void const *pvSubSect, size_t cbSubSect, PCRTCVDIRENT32 pDirEnt)
{
    RT_NOREF_PV(pDirEnt);
    RTDBGMODCV_CHECK_NOMSG_RET_BF(pThis->uCurStyle == RT_MAKE_U16('C', 'V'));
    RTDBGMODCV_CHECK_NOMSG_RET_BF(cbSubSect >= 8);

    uint32_t u32Signature = *(uint32_t const *)pvSubSect;
    RTDBGMODCV_CHECK_RET_BF(u32Signature == RTCVSYMBOLS_SIGNATURE_CV4 || u32Signature == RTCVSYMBOLS_SIGNATURE_CV8,
                            ("%#x, expected %#x\n", u32Signature, RTCVSYMBOLS_SIGNATURE_CV4));
    if (u32Signature == RTCVSYMBOLS_SIGNATURE_CV8)
        return rtDbgModCvSsProcessV8SymTab(pThis, (uint8_t const *)pvSubSect + 4, cbSubSect - 4, 0);
    return rtDbgModCvSsProcessV4PlusSymTab(pThis, (uint8_t const *)pvSubSect + 4, cbSubSect - 4, 0);
}


/** @callback_method_impl{FNDBGMODCVSUBSECTCALLBACK,
 * Parses kCvSst_SrcModule adding line numbers it finds to the container.}
 */
static DECLCALLBACK(int)
rtDbgModCvSs_SrcModule(PRTDBGMODCV pThis, void const *pvSubSect, size_t cbSubSect, PCRTCVDIRENT32 pDirEnt)
{
    RT_NOREF_PV(pDirEnt);
    Log(("rtDbgModCvSs_SrcModule: uCurStyle=%#x\n%.*Rhxd\n", pThis->uCurStyle, cbSubSect, pvSubSect));

    /* Check the header. */
    PCRTCVSRCMODULE pHdr = (PCRTCVSRCMODULE)pvSubSect;
    AssertReturn(cbSubSect >= RT_UOFFSETOF(RTCVSRCMODULE, aoffSrcFiles), VERR_CV_BAD_FORMAT);
    size_t cbHdr = sizeof(RTCVSRCMODULE)
                 + pHdr->cFiles * sizeof(uint32_t)
                 + pHdr->cSegs * sizeof(uint32_t) * 2
                 + pHdr->cSegs * sizeof(uint16_t);
    Log2(("RTDbgModCv: SrcModule: cFiles=%u cSegs=%u\n", pHdr->cFiles, pHdr->cFiles));
    RTDBGMODCV_CHECK_RET_BF(cbSubSect >= cbHdr, ("cbSubSect=%#x cbHdr=%zx\n", cbSubSect, cbHdr));
#ifdef LOG_ENABLED
    if (LogIs2Enabled())
    {
        for (uint32_t i = 0; i < pHdr->cFiles; i++)
            Log2(("RTDbgModCv:   source file #%u: %#x\n", i, pHdr->aoffSrcFiles[i]));
        PCRTCVSRCRANGE  paSegRanges = (PCRTCVSRCRANGE)&pHdr->aoffSrcFiles[pHdr->cFiles];
        uint16_t const *paidxSegs   = (uint16_t const *)&paSegRanges[pHdr->cSegs];
        for (uint32_t i = 0; i < pHdr->cSegs; i++)
            Log2(("RTDbgModCv:   seg #%u: %#010x-%#010x\n", paidxSegs[i], paSegRanges[i].offStart, paSegRanges[i].offEnd));
    }
#endif

    /*
     * Work over the source files.
     */
    for (uint32_t i = 0; i < pHdr->cFiles; i++)
    {
        uint32_t const  offSrcFile  = pHdr->aoffSrcFiles[i];
        RTDBGMODCV_CHECK_RET_BF(cbSubSect - RT_UOFFSETOF(RTCVSRCFILE, aoffSrcLines) >= offSrcFile,
                                ("cbSubSect=%#x (- %#x) aoffSrcFiles[%u]=%#x\n",
                                 cbSubSect, RT_UOFFSETOF(RTCVSRCFILE, aoffSrcLines), i, offSrcFile));
        PCRTCVSRCFILE   pSrcFile    = (PCRTCVSRCFILE)((uint8_t const *)pvSubSect + offSrcFile);
        size_t         cbSrcFileHdr = RT_UOFFSETOF_DYN(RTCVSRCFILE, aoffSrcLines[pSrcFile->cSegs])
                                    + sizeof(RTCVSRCRANGE) * pSrcFile->cSegs
                                    + sizeof(uint8_t);
        RTDBGMODCV_CHECK_RET_BF(cbSubSect >= offSrcFile + cbSrcFileHdr && cbSubSect > cbSrcFileHdr,
                                ("cbSubSect=%#x aoffSrcFiles[%u]=%#x cbSrcFileHdr=%#x\n", cbSubSect, offSrcFile, i, cbSrcFileHdr));
        PCRTCVSRCRANGE  paSegRanges = (PCRTCVSRCRANGE)&pSrcFile->aoffSrcLines[pSrcFile->cSegs];
        uint8_t const  *pcchName    = (uint8_t const *)&paSegRanges[pSrcFile->cSegs]; /** @todo TIS NB09 docs say 16-bit length... */
        const char     *pchName     = (const char *)(pcchName + 1);
        RTDBGMODCV_CHECK_RET_BF(cbSubSect >= offSrcFile + cbSrcFileHdr + *pcchName,
                                ("cbSubSect=%#x offSrcFile=%#x cbSubSect=%#x *pcchName=%#x\n",
                                 cbSubSect, offSrcFile, cbSubSect, *pcchName));
        Log2(("RTDbgModCv:   source file #%u/%#x: cSegs=%#x '%.*s'\n", i, offSrcFile, pSrcFile->cSegs, *pcchName, pchName));
        const char *pszName = rtDbgModCvAddSanitizedStringToCache(pchName, *pcchName);

        /*
         * Work the segments this source file contributes code to.
         */
        for (uint32_t iSeg = 0; iSeg < pSrcFile->cSegs; iSeg++)
        {
            uint32_t const  offSrcLine  = pSrcFile->aoffSrcLines[iSeg];
            RTDBGMODCV_CHECK_RET_BF(cbSubSect - RT_UOFFSETOF(RTCVSRCLINE, aoffLines) >= offSrcLine,
                                    ("cbSubSect=%#x (- %#x) aoffSrcFiles[%u]=%#x\n",
                                     cbSubSect, RT_UOFFSETOF(RTCVSRCLINE, aoffLines), iSeg, offSrcLine));
            PCRTCVSRCLINE   pSrcLine    = (PCRTCVSRCLINE)((uint8_t const *)pvSubSect + offSrcLine);
            size_t          cbSrcLine   = RT_UOFFSETOF_DYN(RTCVSRCLINE, aoffLines[pSrcLine->cPairs])
                                        + pSrcLine->cPairs * sizeof(uint16_t);
            RTDBGMODCV_CHECK_RET_BF(cbSubSect >= offSrcLine + cbSrcLine,
                                    ("cbSubSect=%#x aoffSrcFiles[%u]=%#x cbSrcLine=%#x\n",
                                     cbSubSect, iSeg, offSrcLine, cbSrcLine));
            uint16_t const *paiLines    = (uint16_t const *)&pSrcLine->aoffLines[pSrcLine->cPairs];
            Log2(("RTDbgModCv:     seg #%u, %u pairs (off %#x)\n", pSrcLine->idxSeg, pSrcLine->cPairs, offSrcLine));
            for (uint32_t iPair = 0; iPair < pSrcLine->cPairs; iPair++)
            {

                uint32_t idxSeg = pSrcLine->idxSeg;
                uint64_t off    = pSrcLine->aoffLines[iPair];
                int rc = rtDbgModCvAdjustSegAndOffset(pThis, &idxSeg, &off);
                if (RT_SUCCESS(rc))
                    rc = RTDbgModLineAdd(pThis->hCnt, pszName, paiLines[iPair], idxSeg, off, NULL);
                if (RT_SUCCESS(rc))
                    Log3(("RTDbgModCv:       %#x:%#010llx  %0u\n", idxSeg, off, paiLines[iPair]));
                /* Note! Wlink produces the sstSrcModule subsections from LINNUM records, however the
                         CVGenLines() function assumes there is only one segment contributing to the
                         line numbers.  So, when we do assembly that jumps between segments, it emits
                         the wrong addresses for some line numbers and we end up here, typically with
                         VERR_DBG_ADDRESS_CONFLICT. */
                else
                    Log(( "RTDbgModCv:       %#x:%#010llx  %0u - rc=%Rrc!! (org: idxSeg=%#x off=%#x)\n",
                          idxSeg, off, paiLines[iPair], rc, pSrcLine->idxSeg, pSrcLine->aoffLines[iPair]));
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Processes a freshly loaded raw segment (section) map.
 *
 * This is used by both CV and PDB module types.
 */
static int rtDbgModCvProcessSegmentMap(PRTDBGMODCV pThis)
{
    /* Use local pointers to avoid lots of indirection and typing. */
    PCRTCVSEGMAPHDR  pHdr    = &pThis->pSegMap->Hdr;
    PRTCVSEGMAPDESC  paDescs = &pThis->pSegMap->aDescs[0];

    /*
     * If there are only logical segments, assume a direct mapping.
     * PE images, like the NT4 kernel, does it like this.
     */
    bool const fNoGroups = pHdr->cSegs == pHdr->cLogSegs;

    /*
     * The PE image has an extra section/segment for the headers, the others
     * doesn't.  PE images doesn't have DOS frames. So, figure the image type now.
     */
    RTLDRFMT enmImgFmt = RTLDRFMT_INVALID;
    if (pThis->pMod->pImgVt)
        enmImgFmt = pThis->pMod->pImgVt->pfnGetFormat(pThis->pMod);

    /*
     * Validate and display it all.
     */
    Log2(("RTDbgModCv: SegMap: cSegs=%#x cLogSegs=%#x (cbSegNames=%#x)\n", pHdr->cSegs, pHdr->cLogSegs, pThis->cbSegNames));
    RTDBGMODCV_CHECK_NOMSG_RET_BF(pHdr->cSegs >= pHdr->cLogSegs);

    Log2(("Logical segment descriptors: %u\n", pHdr->cLogSegs));

    bool fHaveDosFrames = false;
    for (uint32_t i = 0; i < pHdr->cSegs; i++)
    {
        if (i == pHdr->cLogSegs)
            Log2(("Group/Physical descriptors: %u\n", pHdr->cSegs - pHdr->cLogSegs));
        char szFlags[16];
        memset(szFlags, '-', sizeof(szFlags));
        if (paDescs[i].fFlags & RTCVSEGMAPDESC_F_READ)
            szFlags[0] = 'R';
        if (paDescs[i].fFlags & RTCVSEGMAPDESC_F_WRITE)
            szFlags[1] = 'W';
        if (paDescs[i].fFlags & RTCVSEGMAPDESC_F_EXECUTE)
            szFlags[2] = 'X';
        if (paDescs[i].fFlags & RTCVSEGMAPDESC_F_32BIT)
            szFlags[3] = '3', szFlags[4]  = '2';
        if (paDescs[i].fFlags & RTCVSEGMAPDESC_F_SEL)
            szFlags[5] = 'S';
        if (paDescs[i].fFlags & RTCVSEGMAPDESC_F_ABS)
            szFlags[6] = 'A';
        if (paDescs[i].fFlags & RTCVSEGMAPDESC_F_GROUP)
            szFlags[7] = 'G';
        szFlags[8] = '\0';
        if (paDescs[i].fFlags & RTCVSEGMAPDESC_F_RESERVED)
            szFlags[8]  = '!', szFlags[9] = '\0';
        Log2(("    #%02u: %#010x LB %#010x flags=%#06x ovl=%#06x group=%#06x frame=%#06x iSegName=%#06x iClassName=%#06x %s\n",
              i < pHdr->cLogSegs ? i : i - pHdr->cLogSegs, paDescs[i].off, paDescs[i].cb, paDescs[i].fFlags, paDescs[i].iOverlay,
              paDescs[i].iGroup, paDescs[i].iFrame, paDescs[i].offSegName, paDescs[i].offClassName, szFlags));

        RTDBGMODCV_CHECK_NOMSG_RET_BF(paDescs[i].offSegName == UINT16_MAX || paDescs[i].offSegName < pThis->cbSegNames);
        RTDBGMODCV_CHECK_NOMSG_RET_BF(paDescs[i].offClassName == UINT16_MAX || paDescs[i].offClassName < pThis->cbSegNames);
        const char *pszName  = paDescs[i].offSegName   != UINT16_MAX
                             ? pThis->pszzSegNames + paDescs[i].offSegName
                             : NULL;
        const char *pszClass = paDescs[i].offClassName != UINT16_MAX
                             ? pThis->pszzSegNames + paDescs[i].offClassName
                             : NULL;
        if (pszName || pszClass)
            Log2(("              pszName=%s pszClass=%s\n", pszName, pszClass));

        /* Validate the group link. */
        RTDBGMODCV_CHECK_NOMSG_RET_BF(paDescs[i].iGroup == 0 || !(paDescs[i].fFlags & RTCVSEGMAPDESC_F_GROUP));
        RTDBGMODCV_CHECK_NOMSG_RET_BF(   paDescs[i].iGroup == 0
                                      || (   paDescs[i].iGroup >= pHdr->cLogSegs
                                          && paDescs[i].iGroup <  pHdr->cSegs));
        RTDBGMODCV_CHECK_NOMSG_RET_BF(   paDescs[i].iGroup == 0
                                      || (paDescs[paDescs[i].iGroup].fFlags & RTCVSEGMAPDESC_F_GROUP));
        RTDBGMODCV_CHECK_NOMSG_RET_BF(!(paDescs[i].fFlags & RTCVSEGMAPDESC_F_GROUP) || paDescs[i].off == 0); /* assumed below */

        if (fNoGroups)
        {
            RTDBGMODCV_CHECK_NOMSG_RET_BF(paDescs[i].iGroup == 0);
            if (   !fHaveDosFrames
                && paDescs[i].iFrame != 0
                && (paDescs[i].fFlags & (RTCVSEGMAPDESC_F_SEL | RTCVSEGMAPDESC_F_ABS))
                && paDescs[i].iOverlay == 0
                && enmImgFmt != RTLDRFMT_PE
                && pThis->enmType != RTCVFILETYPE_DBG
                && pThis->enmType != RTCVFILETYPE_PDB)
                fHaveDosFrames = true; /* BIOS, only groups with frames. */
        }
    }

    /*
     * Further valiations based on fHaveDosFrames or not.
     */
    if (fNoGroups)
    {
        if (fHaveDosFrames)
            for (uint32_t i = 0; i < pHdr->cSegs; i++)
            {
                RTDBGMODCV_CHECK_NOMSG_RET_BF(paDescs[i].iOverlay == 0);
                RTDBGMODCV_CHECK_NOMSG_RET_BF(      (paDescs[i].fFlags & (RTCVSEGMAPDESC_F_SEL | RTCVSEGMAPDESC_F_ABS))
                                                 == RTCVSEGMAPDESC_F_SEL
                                              ||    (paDescs[i].fFlags & (RTCVSEGMAPDESC_F_SEL | RTCVSEGMAPDESC_F_ABS))
                                                 == RTCVSEGMAPDESC_F_ABS);
                RTDBGMODCV_CHECK_NOMSG_RET_BF(!(paDescs[i].fFlags & RTCVSEGMAPDESC_F_ABS));
            }
        else
            for (uint32_t i = 0; i < pHdr->cSegs; i++)
                RTDBGMODCV_CHECK_NOMSG_RET_BF(paDescs[i].off == 0);
    }

    /*
     * Modify the groups index to be the loader segment index instead, also
     * add the segments to the container if we haven't done that already.
     */

    /* Guess work: Group can be implicit if used.  Observed Visual C++ v1.5,
       omitting the CODE group. */
    const char *pszGroup0 = NULL;
    uint64_t    cbGroup0  = 0;
    if (!fNoGroups && !fHaveDosFrames)
    {
        for (uint32_t i = 0; i < pHdr->cSegs; i++)
            if (   !(paDescs[i].fFlags & (RTCVSEGMAPDESC_F_GROUP | RTCVSEGMAPDESC_F_ABS))
                && paDescs[i].iGroup == 0)
            {
                if (pszGroup0 == NULL && paDescs[i].offClassName != UINT16_MAX)
                    pszGroup0 = pThis->pszzSegNames + paDescs[i].offClassName;
                uint64_t offEnd = (uint64_t)paDescs[i].off + paDescs[i].cb;
                if (offEnd > cbGroup0)
                    cbGroup0 = offEnd;
            }
    }

    /* Add the segments.
       Note! The RVAs derived from this exercise are all wrong. :-/
       Note! We don't have an image loader, so we cannot add any fake sections. */
    /** @todo Try see if we can figure something out from the frame value later. */
    if (!pThis->fHaveLoadedSegments)
    {
        int      rc   = VINF_SUCCESS;
        uint16_t iSeg = 0;
        if (!fHaveDosFrames)
        {
            Assert(!pThis->pMod->pImgVt); Assert(pThis->enmType != RTCVFILETYPE_DBG);
            uint64_t uRva = 0;
            if (cbGroup0 && !fNoGroups)
            {
                rc = RTDbgModSegmentAdd(pThis->hCnt, 0, cbGroup0, pszGroup0 ? pszGroup0 : "Seg00", 0 /*fFlags*/, NULL);
                uRva += cbGroup0;
                iSeg++;
            }

            for (uint32_t i = 0; RT_SUCCESS(rc) && i < pHdr->cSegs; i++)
                if ((paDescs[i].fFlags & RTCVSEGMAPDESC_F_GROUP) || fNoGroups)
                {
                    char szName[16];
                    char *pszName = szName;
                    if (paDescs[i].offSegName != UINT16_MAX)
                        pszName = pThis->pszzSegNames + paDescs[i].offSegName;
                    else
                        RTStrPrintf(szName, sizeof(szName), "Seg%02u", iSeg);
                    rc = RTDbgModSegmentAdd(pThis->hCnt, uRva, paDescs[i].cb, pszName, 0 /*fFlags*/, NULL);
                    uRva += paDescs[i].cb;
                    iSeg++;
                }
        }
        else
        {
            /* The map is not sorted by RVA, very annoying, but I'm countering
               by being lazy and slow about it. :-) Btw. this is the BIOS case. */
            Assert(fNoGroups);
#if 1 /** @todo need more inputs */

            /* Figure image base address. */
            uint64_t uImageBase = UINT64_MAX;
            for (uint32_t i = 0; RT_SUCCESS(rc) && i < pHdr->cSegs; i++)
            {
                uint64_t uAddr = (uint64_t)paDescs[i].off + ((uint32_t)paDescs[i].iFrame << 4);
                if (uAddr < uImageBase)
                    uImageBase = uAddr;
            }

            /* Add the segments. */
            uint64_t uMinAddr = uImageBase;
            for (uint32_t i = 0; RT_SUCCESS(rc) && i < pHdr->cSegs; i++)
            {
                /* Figure out the next one. */
                uint16_t cOverlaps = 0;
                uint16_t iBest     = UINT16_MAX;
                uint64_t uBestAddr = UINT64_MAX;
                for (uint16_t j = 0; j < pHdr->cSegs; j++)
                {
                    uint64_t uAddr = (uint64_t)paDescs[j].off + ((uint32_t)paDescs[j].iFrame << 4);
                    if (uAddr >= uMinAddr && uAddr < uBestAddr)
                    {
                        uBestAddr = uAddr;
                        iBest     = j;
                    }
                    else if (uAddr == uBestAddr)
                    {
                        cOverlaps++;
                        if (paDescs[j].cb > paDescs[iBest].cb)
                        {
                            uBestAddr = uAddr;
                            iBest     = j;
                        }
                    }
                }
                if (iBest == UINT16_MAX && RT_SUCCESS(rc))
                {
                    rc = VERR_CV_IPE;
                    break;
                }

                /* Add it. */
                char szName[16];
                char *pszName = szName;
                if (paDescs[iBest].offSegName != UINT16_MAX)
                    pszName = pThis->pszzSegNames + paDescs[iBest].offSegName;
                else
                    RTStrPrintf(szName, sizeof(szName), "Seg%02u", iSeg);
                RTDBGSEGIDX idxDbgSeg = NIL_RTDBGSEGIDX;
                rc = RTDbgModSegmentAdd(pThis->hCnt, uBestAddr - uImageBase, paDescs[iBest].cb, pszName, 0 /*fFlags*/, &idxDbgSeg);
                Log(("CV: %#010x LB %#010x %s uRVA=%#010x iBest=%u cOverlaps=%u [idxDbgSeg=%#x iSeg=%#x]\n",
                     uBestAddr, paDescs[iBest].cb, szName, uBestAddr - uImageBase, iBest, cOverlaps, idxDbgSeg, idxDbgSeg));

                /* Update translations. */
                paDescs[iBest].iGroup = iSeg;
                if (cOverlaps > 0)
                {
                    for (uint16_t j = 0; j < pHdr->cSegs; j++)
                        if ((uint64_t)paDescs[j].off + ((uint32_t)paDescs[j].iFrame << 4) == uBestAddr)
                            paDescs[iBest].iGroup = iSeg;
                    i += cOverlaps;
                }

                /* Advance. */
                uMinAddr = uBestAddr + 1;
                iSeg++;
            }

            pThis->fHaveDosFrames = true;
#else
            uint32_t iFrameFirst = UINT32_MAX;
            uint16_t iSeg        = 0;
            uint32_t iFrameMin   = 0;
            do
            {
                /* Find next frame. */
                uint32_t iFrame = UINT32_MAX;
                for (uint16_t j = 0; j < pHdr->cSegs; j++)
                    if (paDescs[j].iFrame >= iFrameMin && paDescs[j].iFrame < iFrame)
                        iFrame = paDescs[j].iFrame;
                if (iFrame == UINT32_MAX)
                    break;

                /* Figure the frame span. */
                uint32_t offFirst = UINT32_MAX;
                uint64_t offEnd   = 0;
                for (uint16_t j = 0; j < pHdr->cSegs; j++)
                    if (paDescs[j].iFrame == iFrame)
                    {
                        uint64_t offThisEnd = paDescs[j].off + paDescs[j].cb;
                        if (offThisEnd > offEnd)
                            offEnd   = offThisEnd;
                        if (paDescs[j].off < offFirst)
                            offFirst = paDescs[j].off;
                    }

                if (offFirst < offEnd)
                {
                    /* Add it. */
                    char szName[16];
                    RTStrPrintf(szName, sizeof(szName), "Frame_%04x", iFrame);
                    Log(("CV: %s offEnd=%#x offFirst=%#x\n", szName, offEnd, offFirst));
                    if (iFrameFirst == UINT32_MAX)
                        iFrameFirst = iFrame;
                    rc = RTDbgModSegmentAdd(pThis->hCnt, (iFrame - iFrameFirst) << 4, offEnd, szName, 0 /*fFlags*/, NULL);

                    /* Translation updates. */
                    for (uint16_t j = 0; j < pHdr->cSegs; j++)
                        if (paDescs[j].iFrame == iFrame)
                        {
                            paDescs[j].iGroup = iSeg;
                            paDescs[j].off    = 0;
                            paDescs[j].cb     = offEnd > UINT32_MAX ? UINT32_MAX : (uint32_t)offEnd;
                        }

                    iSeg++;
                }

                iFrameMin = iFrame + 1;
            } while (RT_SUCCESS(rc));
#endif
        }

        if (RT_FAILURE(rc))
        {
            Log(("RTDbgModCv: %Rrc while adding segments from SegMap\n", rc));
            return rc;
        }

        pThis->fHaveLoadedSegments = true;

        /* Skip the stuff below if we have DOS frames since we did it all above. */
        if (fHaveDosFrames)
            return VINF_SUCCESS;
    }

    /* Pass one: Fixate the group segment indexes. */
    uint16_t iSeg0 = enmImgFmt == RTLDRFMT_PE || pThis->enmType == RTCVFILETYPE_DBG || pThis->enmType == RTCVFILETYPE_PDB ? 1 : 0;
    uint16_t iSeg = iSeg0 + (cbGroup0 > 0); /** @todo probably wrong... */
    for (uint32_t i = 0; i < pHdr->cSegs; i++)
        if (paDescs[i].fFlags & RTCVSEGMAPDESC_F_ABS)
            paDescs[i].iGroup = (uint16_t)(RTDBGSEGIDX_ABS & UINT16_MAX);
        else if ((paDescs[i].fFlags & RTCVSEGMAPDESC_F_GROUP) || fNoGroups)
            paDescs[i].iGroup = iSeg++;

    /* Pass two: Resolve group references in to segment indexes. */
    Log2(("Mapped segments (both kinds):\n"));
    for (uint32_t i = 0; i < pHdr->cSegs; i++)
    {
        if (!fNoGroups && !(paDescs[i].fFlags & (RTCVSEGMAPDESC_F_GROUP | RTCVSEGMAPDESC_F_ABS)))
            paDescs[i].iGroup = paDescs[i].iGroup == 0 ? iSeg0 : paDescs[paDescs[i].iGroup].iGroup;

        Log2(("    #%02u: %#010x LB %#010x -> %#06x (flags=%#06x ovl=%#06x frame=%#06x)\n",
              i, paDescs[i].off, paDescs[i].cb, paDescs[i].iGroup,
              paDescs[i].fFlags, paDescs[i].iOverlay, paDescs[i].iFrame));
    }

    return VINF_SUCCESS;
}


/**
 * Loads and processes the segment map.
 */
static int rtDbgModCvLoadSegmentMap(PRTDBGMODCV pThis)
{
    /*
     * Search for the segment map and segment names. They will be at the end of the directory.
     */
    uint32_t iSegMap   = UINT32_MAX;
    uint32_t iSegNames = UINT32_MAX;
    uint32_t i = pThis->cDirEnts;
    while (i-- > 0)
    {
        if (   pThis->paDirEnts[i].iMod != 0xffff
            && pThis->paDirEnts[i].iMod != 0x0000)
            break;
        if (pThis->paDirEnts[i].uSubSectType == kCvSst_SegMap)
            iSegMap = i;
        else if (pThis->paDirEnts[i].uSubSectType == kCvSst_SegName)
            iSegNames = i;
    }
    if (iSegMap == UINT32_MAX)
    {
        Log(("RTDbgModCv: No segment map present, using segment indexes as is then...\n"));
        return VINF_SUCCESS;
    }
    RTDBGMODCV_CHECK_RET_BF(pThis->paDirEnts[iSegMap].cb >= sizeof(RTCVSEGMAPHDR),
                            ("Bad sstSegMap entry: cb=%#x\n", pThis->paDirEnts[iSegMap].cb));
    RTDBGMODCV_CHECK_NOMSG_RET_BF(iSegNames == UINT32_MAX || pThis->paDirEnts[iSegNames].cb > 0);

    /*
     * Read them into memory.
     */
    int rc = rtDbgModCvReadAtAlloc(pThis, pThis->paDirEnts[iSegMap].off, (void **)&pThis->pSegMap,
                                   pThis->paDirEnts[iSegMap].cb);
    if (iSegNames != UINT32_MAX && RT_SUCCESS(rc))
    {
        pThis->cbSegNames = pThis->paDirEnts[iSegNames].cb;
        rc = rtDbgModCvReadAtAlloc(pThis, pThis->paDirEnts[iSegNames].off, (void **)&pThis->pszzSegNames,
                                   pThis->paDirEnts[iSegNames].cb);
    }
    if (RT_FAILURE(rc))
        return rc;
    RTDBGMODCV_CHECK_NOMSG_RET_BF(!pThis->pszzSegNames || !pThis->pszzSegNames[pThis->cbSegNames - 1]); /* must be terminated */
    RTDBGMODCV_CHECK_RET_BF(pThis->paDirEnts[iSegMap].cb >= RT_UOFFSETOF_DYN(RTCVSEGMAP, aDescs[pThis->pSegMap->Hdr.cSegs]),
                            ("SegMap is out of bounds: cbSubSect=%#x cSegs=%#x\n",
                             pThis->paDirEnts[iSegMap].cb, pThis->pSegMap->Hdr.cSegs));

    /*
     * Join paths with the PDB handling.
     */
    return rtDbgModCvProcessSegmentMap(pThis);
}




/**
 * @callback_method_impl{PFNRTSORTCMP,
 *      Used by rtDbgModCvLoadDirectory to sort the directory.}
 */
static DECLCALLBACK(int) rtDbgModCvDirEntCmp(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PRTCVDIRENT32 pEntry1 = (PRTCVDIRENT32)pvElement1;
    PRTCVDIRENT32 pEntry2 = (PRTCVDIRENT32)pvElement2;
    if (pEntry1->iMod < pEntry2->iMod)
        return -1;
    if (pEntry1->iMod > pEntry2->iMod)
        return 1;
    if (pEntry1->uSubSectType < pEntry2->uSubSectType)
        return -1;
    if (pEntry1->uSubSectType > pEntry2->uSubSectType)
        return 1;

    RT_NOREF_PV(pvUser);
    return 0;
}


/**
 * Loads the directory into memory (RTDBGMODCV::paDirEnts and
 * RTDBGMODCV::cDirEnts).
 *
 * Converting old format version into the newer format to simplifying the code
 * using the directory.
 *
 *
 * @returns IPRT status code. (May leave with paDirEnts allocated on failure.)
 * @param   pThis               The CV reader instance.
 */
static int rtDbgModCvLoadDirectory(PRTDBGMODCV pThis)
{
    /*
     * Read in the CV directory.
     */
    int rc;
    if (   pThis->u32CvMagic == RTCVHDR_MAGIC_NB00
        || pThis->u32CvMagic == RTCVHDR_MAGIC_NB02)
    {
        /*
         * 16-bit type.
         */
        RTCVDIRHDR16 DirHdr;
        rc = rtDbgModCvReadAt(pThis, pThis->offDir, &DirHdr, sizeof(DirHdr));
        if (RT_SUCCESS(rc))
        {
            if (DirHdr.cEntries > 2 && DirHdr.cEntries < _64K - 32U)
            {
                pThis->cDirEnts  = DirHdr.cEntries;
                pThis->paDirEnts = (PRTCVDIRENT32)RTMemAlloc(DirHdr.cEntries * sizeof(pThis->paDirEnts[0]));
                if (pThis->paDirEnts)
                {
                    rc = rtDbgModCvReadAt(pThis, pThis->offDir + sizeof(DirHdr),
                                          pThis->paDirEnts, DirHdr.cEntries * sizeof(RTCVDIRENT16));
                    if (RT_SUCCESS(rc))
                    {
                        /* Convert the entries (from the end). */
                        uint32_t               cLeft = DirHdr.cEntries;
                        RTCVDIRENT32 volatile *pDst  = pThis->paDirEnts + cLeft;
                        RTCVDIRENT16 volatile *pSrc  = (RTCVDIRENT16 volatile *)pThis->paDirEnts + cLeft;
                        while (cLeft--)
                        {
                            pDst--;
                            pSrc--;

                            pDst->cb           = pSrc->cb;
                            pDst->off          = RT_MAKE_U32(pSrc->offLow, pSrc->offHigh);
                            pDst->iMod         = pSrc->iMod;
                            pDst->uSubSectType = pSrc->uSubSectType;
                        }
                    }
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else
            {
                Log(("Old CV directory count is out of considered valid range: %#x\n", DirHdr.cEntries));
                rc = VERR_CV_BAD_FORMAT;
            }
        }
    }
    else
    {
        /*
         * 32-bit type (reading too much for NB04 is no problem).
         *
         * Note! The watcom linker (v1.9) seems to overwrite the directory
         *       header and more under some conditions.  So, if this code fails
         *       you might be so lucky as to have reproduce that issue...
         */
        RTCVDIRHDR32EX DirHdr;
        rc = rtDbgModCvReadAt(pThis, pThis->offDir, &DirHdr, sizeof(DirHdr));
        if (RT_SUCCESS(rc))
        {
            if (   DirHdr.Core.cbHdr != sizeof(DirHdr.Core)
                && DirHdr.Core.cbHdr != sizeof(DirHdr))
            {
                Log(("Unexpected CV directory size: %#x [wlink screwup?]\n", DirHdr.Core.cbHdr));
                rc = VERR_CV_BAD_FORMAT;
            }
            if (   DirHdr.Core.cbHdr == sizeof(DirHdr)
                && (   DirHdr.offNextDir != 0
                    || DirHdr.fFlags     != 0) )
            {
                Log(("Extended CV directory headers fields are not zero: fFlags=%#x offNextDir=%#x [wlink screwup?]\n",
                     DirHdr.fFlags, DirHdr.offNextDir));
                rc = VERR_CV_BAD_FORMAT;
            }
            if (DirHdr.Core.cbEntry != sizeof(RTCVDIRENT32))
            {
                Log(("Unexpected CV directory entry size: %#x (expected %#x) [wlink screwup?]\n", DirHdr.Core.cbEntry, sizeof(RTCVDIRENT32)));
                rc = VERR_CV_BAD_FORMAT;
            }
            if (DirHdr.Core.cEntries < 2 || DirHdr.Core.cEntries >= _512K)
            {
                Log(("CV directory count is out of considered valid range: %#x [wlink screwup?]\n", DirHdr.Core.cEntries));
                rc = VERR_CV_BAD_FORMAT;
            }
            if (RT_SUCCESS(rc))
            {
                pThis->cDirEnts  = DirHdr.Core.cEntries;
                pThis->paDirEnts = (PRTCVDIRENT32)RTMemAlloc(DirHdr.Core.cEntries * sizeof(pThis->paDirEnts[0]));
                if (pThis->paDirEnts)
                    rc = rtDbgModCvReadAt(pThis, pThis->offDir + DirHdr.Core.cbHdr,
                                          pThis->paDirEnts, DirHdr.Core.cEntries * sizeof(RTCVDIRENT32));
                else
                    rc = VERR_NO_MEMORY;
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        uint32_t const cbDbgInfo    = pThis->cbDbgInfo;
        uint32_t const cDirEnts     = pThis->cDirEnts;

        /*
         * Just sort the directory in a way we like, no need to make
         * complicated demands on the linker output.
         */
        RTSortShell(pThis->paDirEnts, cDirEnts, sizeof(pThis->paDirEnts[0]), rtDbgModCvDirEntCmp, NULL);

        /*
         * Basic info validation.
         */
        uint16_t       cGlobalMods  = 0;
        uint16_t       cNormalMods  = 0;
        uint16_t       iModLast     = 0;
        Log2(("RTDbgModCv: %u (%#x) directory entries:\n", cDirEnts, cDirEnts));
        for (uint32_t i = 0; i < cDirEnts; i++)
        {
            PCRTCVDIRENT32 pDirEnt = &pThis->paDirEnts[i];
            Log2(("    #%04u mod=%#06x sst=%#06x at %#010x LB %#07x %s\n",
                  i, pDirEnt->iMod, pDirEnt->uSubSectType, pDirEnt->off, pDirEnt->cb,
                  rtDbgModCvGetSubSectionName(pDirEnt->uSubSectType)));

            if (   pDirEnt->off >= cbDbgInfo
                || pDirEnt->cb  >= cbDbgInfo
                || pDirEnt->off + pDirEnt->cb > cbDbgInfo)
            {
                Log(("CV directory entry #%u is out of bounds: %#x LB %#x, max %#x\n", i, pDirEnt->off, pDirEnt->cb, cbDbgInfo));
                rc = VERR_CV_BAD_FORMAT;
            }
            if (   pDirEnt->iMod == 0
                && pThis->u32CvMagic != RTCVHDR_MAGIC_NB04
                && pThis->u32CvMagic != RTCVHDR_MAGIC_NB02
                && pThis->u32CvMagic != RTCVHDR_MAGIC_NB00)
            {
                Log(("CV directory entry #%u uses module index 0 (uSubSectType=%#x)\n", i, pDirEnt->uSubSectType));
                rc = VERR_CV_BAD_FORMAT;
            }
            if (pDirEnt->iMod == 0 || pDirEnt->iMod == 0xffff)
                cGlobalMods++;
            else
            {
                if (pDirEnt->iMod > iModLast)
                {
                    if (   pDirEnt->uSubSectType != kCvSst_Module
                        && pDirEnt->uSubSectType != kCvSst_OldModule)
                    {
                        Log(("CV directory entry #%u: expected module subsection first, found %s (%#x)\n",
                             i, rtDbgModCvGetSubSectionName(pDirEnt->uSubSectType), pDirEnt->uSubSectType));
                        rc = VERR_CV_BAD_FORMAT;
                    }
                    if (pDirEnt->iMod != iModLast + 1)
                    {
                        Log(("CV directory entry #%u: skips from mod %#x to %#x modules\n", i, iModLast, pDirEnt->iMod));
                        rc = VERR_CV_BAD_FORMAT;
                    }
                    iModLast = pDirEnt->iMod;
                }
                cNormalMods++;
            }
        }
        if (cGlobalMods == 0)
        {
            Log(("CV directory contains no global modules\n"));
            rc = VERR_CV_BAD_FORMAT;
        }
        if (RT_SUCCESS(rc))
        {
            Log(("CV dir stats: %u total, %u normal, %u special, iModLast=%#x (%u)\n",
                 cDirEnts, cNormalMods, cGlobalMods, iModLast, iModLast));

#if 0 /* skip this stuff */
            /*
             * Validate the directory ordering.
             */
            uint16_t i = 0;

            /* Normal modules. */
            if (pThis->enmDirOrder != RTCVDIRORDER_BY_SST_MOD)
            {
                uint16_t iEndNormalMods = cNormalMods + (pThis->enmDirOrder == RTCVDIRORDER_BY_MOD_0 ? cGlobalMods : 0);
                while (i < iEndNormalMods)
                {
                    if (pThis->paDirEnts[i].iMod == 0 || pThis->paDirEnts[i].iMod == 0xffff)
                    {
                        Log(("CV directory entry #%u: Unexpected global module entry.\n", i));
                        rc = VERR_CV_BAD_FORMAT;
                    }
                    i++;
                }
            }
            else
            {
                uint32_t fSeen = RT_BIT_32(kCvSst_Module      - kCvSst_Module)
                               | RT_BIT_32(kCvSst_Libraries   - kCvSst_Module)
                               | RT_BIT_32(kCvSst_GlobalSym   - kCvSst_Module)
                               | RT_BIT_32(kCvSst_GlobalPub   - kCvSst_Module)
                               | RT_BIT_32(kCvSst_GlobalTypes - kCvSst_Module)
                               | RT_BIT_32(kCvSst_SegName     - kCvSst_Module)
                               | RT_BIT_32(kCvSst_SegMap      - kCvSst_Module)
                               | RT_BIT_32(kCvSst_StaticSym   - kCvSst_Module)
                               | RT_BIT_32(kCvSst_FileIndex   - kCvSst_Module)
                               | RT_BIT_32(kCvSst_MPC         - kCvSst_Module);
                uint16_t iMod = 0;
                uint16_t uSst = kCvSst_Module;
                while (i < cNormalMods)
                {
                    PCRTCVDIRENT32 pDirEnt = &pThis->paDirEnts[i];
                    if (   pDirEnt->iMod > iMod
                        || pDirEnt->iMod == iMod) /* wlink subjected to MSVC 2010 /Z7 files with multiple .debug$S. */
                    {
                        if (pDirEnt->uSubSectType != uSst)
                        {
                            Log(("CV directory entry #%u: Expected %s (%#x), found %s (%#x).\n",
                                 i, rtDbgModCvGetSubSectionName(uSst), uSst,
                                 rtDbgModCvGetSubSectionName(pDirEnt->uSubSectType), pDirEnt->uSubSectType));
                            rc = VERR_CV_BAD_FORMAT;
                        }
                    }
                    else
                    {
                        uint32_t iBit = pDirEnt->uSubSectType - kCvSst_Module;
                        if (iBit >= 32U || (fSeen & RT_BIT_32(iBit)))
                        {
                            Log(("CV directory entry #%u: SST %s (%#x) has already been seen or is for globals.\n",
                                 i, rtDbgModCvGetSubSectionName(pDirEnt->uSubSectType), pDirEnt->uSubSectType));
                            rc = VERR_CV_BAD_FORMAT;
                        }
                        fSeen |= RT_BIT_32(iBit);
                    }

                    uSst = pDirEnt->uSubSectType;
                    iMod = pDirEnt->iMod;
                    i++;
                }
            }

            /* New style with special modules at the end. */
            if (pThis->enmDirOrder != RTCVDIRORDER_BY_MOD_0)
                while (i < cDirEnts)
                {
                    if (pThis->paDirEnts[i].iMod != 0 && pThis->paDirEnts[i].iMod != 0xffff)
                    {
                        Log(("CV directory entry #%u: Expected global module entry, not %#x.\n", i,
                             pThis->paDirEnts[i].iMod));
                        rc = VERR_CV_BAD_FORMAT;
                    }
                    i++;
                }
#endif
        }
    }

    return rc;
}


static int rtDbgModCvLoadCodeViewInfo(PRTDBGMODCV pThis)
{
    /*
     * Load the directory, the segment map (if any) and then scan for segments
     * if necessary.
     */
    int rc = rtDbgModCvLoadDirectory(pThis);
    if (RT_SUCCESS(rc))
        rc = rtDbgModCvLoadSegmentMap(pThis);
    if (RT_SUCCESS(rc) && !pThis->fHaveLoadedSegments)
    {
        rc = VERR_CV_TODO; /** @todo Scan anything containing address, in particular sstSegMap and sstModule,
                            * and reconstruct the segments from that information. */
        pThis->cbImage = 0x1000;
        rc = VINF_SUCCESS;
    }

    /*
     * Process the directory.
     */
    for (uint32_t i = 0; RT_SUCCESS(rc) && i < pThis->cDirEnts; i++)
    {
        PCRTCVDIRENT32              pDirEnt     = &pThis->paDirEnts[i];
        Log3(("Processing module %#06x subsection #%04u %s\n", pDirEnt->iMod, i, rtDbgModCvGetSubSectionName(pDirEnt->uSubSectType)));
        PFNDBGMODCVSUBSECTCALLBACK  pfnCallback = NULL;
        switch (pDirEnt->uSubSectType)
        {
            case kCvSst_GlobalPub:
            case kCvSst_GlobalSym:
            case kCvSst_StaticSym:
                pfnCallback = rtDbgModCvSs_GlobalPub_GlobalSym_StaticSym;
                break;
            case kCvSst_Module:
                pfnCallback = rtDbgModCvSs_Module;
                break;
            case kCvSst_PublicSym:
            case kCvSst_Symbols:
            case kCvSst_AlignSym:
                pfnCallback = rtDbgModCvSs_Symbols_PublicSym_AlignSym;
                break;

            case kCvSst_OldModule:
            case kCvSst_OldPublic:
            case kCvSst_OldTypes:
            case kCvSst_OldSymbols:
            case kCvSst_OldSrcLines:
            case kCvSst_OldLibraries:
            case kCvSst_OldImports:
            case kCvSst_OldCompacted:
            case kCvSst_OldSrcLnSeg:
            case kCvSst_OldSrcLines3:
                /** @todo implement more. */
                break;

            case kCvSst_Types:
            case kCvSst_Public:
            case kCvSst_SrcLnSeg:
                /** @todo implement more. */
                break;
            case kCvSst_SrcModule:
                pfnCallback = rtDbgModCvSs_SrcModule;
                break;
            case kCvSst_Libraries:
            case kCvSst_GlobalTypes:
            case kCvSst_MPC:
            case kCvSst_PreComp:
            case kCvSst_PreCompMap:
            case kCvSst_OffsetMap16:
            case kCvSst_OffsetMap32:
            case kCvSst_FileIndex:

            default:
                /** @todo implement more. */
                break;

            /* Skip because we've already processed them: */
            case kCvSst_SegMap:
            case kCvSst_SegName:
                pfnCallback = NULL;
                break;
        }

        if (pfnCallback)
        {
            void *pvSubSect;
            rc = rtDbgModCvReadAtAlloc(pThis, pDirEnt->off, &pvSubSect, pDirEnt->cb);
            if (RT_SUCCESS(rc))
            {
                rc = pfnCallback(pThis, pvSubSect, pDirEnt->cb, pDirEnt);
                RTMemFree(pvSubSect);
            }
        }
    }

    /*
     * Free temporary parsing objects.
     */
    if (pThis->pbSrcInfo)
    {
        RTMemFree(pThis->pbSrcInfo);
        pThis->pbSrcInfo      = NULL;
        pThis->cbSrcInfo      = 0;
        pThis->cbSrcInfoAlloc = 0;
    }
    if (pThis->pchSrcStrings)
    {
        RTMemFree(pThis->pchSrcStrings);
        pThis->pchSrcStrings     = NULL;
        pThis->cbSrcStrings      = 0;
        pThis->cbSrcStringsAlloc = 0;
    }

    return rc;
}


/*
 *
 * COFF Debug Info Parsing.
 * COFF Debug Info Parsing.
 * COFF Debug Info Parsing.
 *
 */

#ifdef LOG_ENABLED
static const char *rtDbgModCvGetCoffStorageClassName(uint8_t bStorageClass)
{
    switch (bStorageClass)
    {
        case IMAGE_SYM_CLASS_END_OF_FUNCTION:   return "END_OF_FUNCTION";
        case IMAGE_SYM_CLASS_NULL:              return "NULL";
        case IMAGE_SYM_CLASS_AUTOMATIC:         return "AUTOMATIC";
        case IMAGE_SYM_CLASS_EXTERNAL:          return "EXTERNAL";
        case IMAGE_SYM_CLASS_STATIC:            return "STATIC";
        case IMAGE_SYM_CLASS_REGISTER:          return "REGISTER";
        case IMAGE_SYM_CLASS_EXTERNAL_DEF:      return "EXTERNAL_DEF";
        case IMAGE_SYM_CLASS_LABEL:             return "LABEL";
        case IMAGE_SYM_CLASS_UNDEFINED_LABEL:   return "UNDEFINED_LABEL";
        case IMAGE_SYM_CLASS_MEMBER_OF_STRUCT:  return "MEMBER_OF_STRUCT";
        case IMAGE_SYM_CLASS_ARGUMENT:          return "ARGUMENT";
        case IMAGE_SYM_CLASS_STRUCT_TAG:        return "STRUCT_TAG";
        case IMAGE_SYM_CLASS_MEMBER_OF_UNION:   return "MEMBER_OF_UNION";
        case IMAGE_SYM_CLASS_UNION_TAG:         return "UNION_TAG";
        case IMAGE_SYM_CLASS_TYPE_DEFINITION:   return "TYPE_DEFINITION";
        case IMAGE_SYM_CLASS_UNDEFINED_STATIC:  return "UNDEFINED_STATIC";
        case IMAGE_SYM_CLASS_ENUM_TAG:          return "ENUM_TAG";
        case IMAGE_SYM_CLASS_MEMBER_OF_ENUM:    return "MEMBER_OF_ENUM";
        case IMAGE_SYM_CLASS_REGISTER_PARAM:    return "REGISTER_PARAM";
        case IMAGE_SYM_CLASS_BIT_FIELD:         return "BIT_FIELD";
        case IMAGE_SYM_CLASS_FAR_EXTERNAL:      return "FAR_EXTERNAL";
        case IMAGE_SYM_CLASS_BLOCK:             return "BLOCK";
        case IMAGE_SYM_CLASS_FUNCTION:          return "FUNCTION";
        case IMAGE_SYM_CLASS_END_OF_STRUCT:     return "END_OF_STRUCT";
        case IMAGE_SYM_CLASS_FILE:              return "FILE";
        case IMAGE_SYM_CLASS_SECTION:           return "SECTION";
        case IMAGE_SYM_CLASS_WEAK_EXTERNAL:     return "WEAK_EXTERNAL";
        case IMAGE_SYM_CLASS_CLR_TOKEN:         return "CLR_TOKEN";
    }

    static char s_szName[32];
    RTStrPrintf(s_szName, sizeof(s_szName), "Unknown%#04x", bStorageClass);
    return s_szName;
}
#endif /* LOG_ENABLED */


/**
 * Adds a chunk of COFF line numbers.
 *
 * @param   pThis               The COFF/CodeView reader instance.
 * @param   pszFile             The source file name.
 * @param   iSection            The section number.
 * @param   paLines             Pointer to the first line number table entry.
 * @param   cLines              The number of line number table entries to add.
 */
static void rtDbgModCvAddCoffLineNumbers(PRTDBGMODCV pThis, const char *pszFile, uint32_t iSection,
                                         PCIMAGE_LINENUMBER paLines, uint32_t cLines)
{
    RT_NOREF_PV(iSection);
    Log4(("Adding %u line numbers in section #%u  for %s\n", cLines, iSection, pszFile));
    PCIMAGE_LINENUMBER pCur = paLines;
    while (cLines-- > 0)
    {
        if (pCur->Linenumber)
        {
            int rc = RTDbgModLineAdd(pThis->hCnt, pszFile, pCur->Linenumber, RTDBGSEGIDX_RVA, pCur->Type.VirtualAddress, NULL);
            Log4(("    %#010x: %u  [%Rrc]\n", pCur->Type.VirtualAddress, pCur->Linenumber, rc)); NOREF(rc);
        }
        pCur++;
    }
}


/**
 * Adds a COFF symbol.
 *
 * @returns IPRT status (ignored)
 * @param   pThis               The COFF/CodeView reader instance.
 * @param   idxSeg              IPRT RVA or ABS segment index indicator.
 * @param   uValue              The symbol value.
 * @param   pszName             The symbol name.
 */
static int rtDbgModCvAddCoffSymbol(PRTDBGMODCV pThis, uint32_t idxSeg, uint32_t uValue, const char *pszName)
{
    int rc = RTDbgModSymbolAdd(pThis->hCnt, pszName, idxSeg, uValue, 0, 0 /*fFlags*/, NULL);
    Log(("Symbol: %s:%08x %s [%Rrc]\n", idxSeg == RTDBGSEGIDX_RVA ? "rva" : "abs", uValue, pszName, rc));
    if (rc == VERR_DBG_ADDRESS_CONFLICT || rc == VERR_DBG_DUPLICATE_SYMBOL)
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Processes the COFF symbol table.
 *
 * @returns IPRT status code
 * @param   pThis               The COFF/CodeView reader instance.
 * @param   paSymbols           Pointer to the symbol table.
 * @param   cSymbols            The number of entries in the symbol table.
 * @param   paLines             Pointer to the line number table.
 * @param   cLines              The number of entires in the line number table.
 * @param   pszzStrTab          Pointer to the string table.
 * @param   cbStrTab            Size of the string table.
 */
static int rtDbgModCvProcessCoffSymbolTable(PRTDBGMODCV pThis,
                                            PCIMAGE_SYMBOL      paSymbols,  uint32_t cSymbols,
                                            PCIMAGE_LINENUMBER  paLines,    uint32_t cLines,
                                            const char         *pszzStrTab, uint32_t cbStrTab)
{
    Log3(("Processing COFF symbol table with %#x symbols\n", cSymbols));

    /*
     * Making some bold assumption that the line numbers for the section in
     * the file are allocated sequentially, we do multiple passes until we've
     * gathered them all.
     */
    int      rc        = VINF_SUCCESS;
    uint32_t cSections = 1;
    uint32_t iLineSect = 1;
    uint32_t iLine     = 0;
    do
    {
        /*
         * Process the symbols.
         */
        char        szShort[9];
        char        szFile[RTPATH_MAX];
        uint32_t    iSymbol  = 0;
        szFile[0] = '\0';
        szShort[8] = '\0'; /* avoid having to terminate it all the time. */

        while (iSymbol < cSymbols && RT_SUCCESS(rc))
        {
            /* Copy the symbol in and hope it works around the misalignment
               issues everywhere. */
            IMAGE_SYMBOL Sym;
            memcpy(&Sym, &paSymbols[iSymbol], sizeof(Sym));
            RTDBGMODCV_CHECK_NOMSG_RET_BF(Sym.NumberOfAuxSymbols < cSymbols);

            /* Calc a zero terminated symbol name. */
            const char  *pszName;
            if (Sym.N.Name.Short)
                pszName = (const char *)memcpy(szShort, &Sym.N, 8);
            else
            {
                RTDBGMODCV_CHECK_NOMSG_RET_BF(Sym.N.Name.Long < cbStrTab);
                pszName = pszzStrTab + Sym.N.Name.Long;
            }

            /* Only log stuff and count sections the in the first pass.*/
            if (iLineSect == 1)
            {
                Log3(("%04x: s=%#06x v=%#010x t=%#06x a=%#04x c=%#04x (%s) name='%s'\n",
                      iSymbol, Sym.SectionNumber, Sym.Value, Sym.Type, Sym.NumberOfAuxSymbols,
                      Sym.StorageClass, rtDbgModCvGetCoffStorageClassName(Sym.StorageClass), pszName));
                if ((int16_t)cSections <= Sym.SectionNumber && Sym.SectionNumber > 0)
                    cSections = Sym.SectionNumber + 1;
            }

            /*
             * Use storage class to pick what we need (which isn't much because,
             * MS only provides a very restricted set of symbols).
             */
            IMAGE_AUX_SYMBOL Aux;
            switch (Sym.StorageClass)
            {
                case IMAGE_SYM_CLASS_NULL:
                    /* a NOP */
                    break;

                case IMAGE_SYM_CLASS_FILE:
                {
                    /* Change the current file name (for line numbers). Pretend
                       ANSI and ISO-8859-1 are similar enough for out purposes... */
                    RTDBGMODCV_CHECK_NOMSG_RET_BF(Sym.NumberOfAuxSymbols > 0);
                    const char *pszFile = (const char *)&paSymbols[iSymbol + 1];
                    char *pszDst = szFile;
                    rc = RTLatin1ToUtf8Ex(pszFile, Sym.NumberOfAuxSymbols * sizeof(IMAGE_SYMBOL), &pszDst, sizeof(szFile), NULL);
                    if (RT_FAILURE(rc))
                        Log(("Error converting COFF filename: %Rrc\n", rc));
                    else if (iLineSect == 1)
                        Log3(("    filename='%s'\n", szFile));
                    break;
                }

                case IMAGE_SYM_CLASS_STATIC:
                    if (   Sym.NumberOfAuxSymbols == 1
                        && (   iLineSect == 1
                            || Sym.SectionNumber == (int32_t)iLineSect) )
                    {
                        memcpy(&Aux, &paSymbols[iSymbol + 1], sizeof(Aux));
                        if (iLineSect == 1)
                            Log3(("    section: cb=%#010x #relocs=%#06x #lines=%#06x csum=%#x num=%#x sel=%x rvd=%u\n",
                                  Aux.Section.Length, Aux.Section.NumberOfRelocations,
                                  Aux.Section.NumberOfLinenumbers,
                                  Aux.Section.CheckSum,
                                  RT_MAKE_U32(Aux.Section.Number, Aux.Section.HighNumber),
                                  Aux.Section.Selection,
                                  Aux.Section.bReserved));
                        if (   Sym.SectionNumber == (int32_t)iLineSect
                            && Aux.Section.NumberOfLinenumbers > 0)
                        {
                            uint32_t cLinesToAdd = RT_MIN(Aux.Section.NumberOfLinenumbers, cLines - iLine);
                            if (iLine < cLines && szFile[0])
                                rtDbgModCvAddCoffLineNumbers(pThis, szFile, iLineSect, &paLines[iLine], cLinesToAdd);
                            iLine += cLinesToAdd;
                        }
                    }
                    /* Not so sure about the quality here, but might be useful. */
                    else if (   iLineSect == 1
                             && Sym.NumberOfAuxSymbols == 0
                             && Sym.SectionNumber != IMAGE_SYM_UNDEFINED
                             && Sym.SectionNumber != IMAGE_SYM_ABSOLUTE
                             && Sym.SectionNumber != IMAGE_SYM_DEBUG
                             && Sym.Value > 0
                             && *pszName)
                        rtDbgModCvAddCoffSymbol(pThis, RTDBGSEGIDX_RVA, Sym.Value, pszName);
                    break;

                case IMAGE_SYM_CLASS_EXTERNAL:
                    /* Add functions (first pass only). */
                    if (   iLineSect == 1
                        && (ISFCN(Sym.Type) || Sym.Type == 0)
                        && Sym.NumberOfAuxSymbols == 0
                        && *pszName )
                    {
                        if (Sym.SectionNumber == IMAGE_SYM_ABSOLUTE)
                            rtDbgModCvAddCoffSymbol(pThis, RTDBGSEGIDX_ABS, Sym.Value, pszName);
                        else if (   Sym.SectionNumber != IMAGE_SYM_UNDEFINED
                                 && Sym.SectionNumber != IMAGE_SYM_DEBUG)
                            rtDbgModCvAddCoffSymbol(pThis, RTDBGSEGIDX_RVA, Sym.Value, pszName);
                    }
                    break;

                case IMAGE_SYM_CLASS_FUNCTION:
                    /* Not sure this is really used. */
                    break;

                case IMAGE_SYM_CLASS_END_OF_FUNCTION:
                case IMAGE_SYM_CLASS_AUTOMATIC:
                case IMAGE_SYM_CLASS_REGISTER:
                case IMAGE_SYM_CLASS_EXTERNAL_DEF:
                case IMAGE_SYM_CLASS_LABEL:
                case IMAGE_SYM_CLASS_UNDEFINED_LABEL:
                case IMAGE_SYM_CLASS_MEMBER_OF_STRUCT:
                case IMAGE_SYM_CLASS_ARGUMENT:
                case IMAGE_SYM_CLASS_STRUCT_TAG:
                case IMAGE_SYM_CLASS_MEMBER_OF_UNION:
                case IMAGE_SYM_CLASS_UNION_TAG:
                case IMAGE_SYM_CLASS_TYPE_DEFINITION:
                case IMAGE_SYM_CLASS_UNDEFINED_STATIC:
                case IMAGE_SYM_CLASS_ENUM_TAG:
                case IMAGE_SYM_CLASS_MEMBER_OF_ENUM:
                case IMAGE_SYM_CLASS_REGISTER_PARAM:
                case IMAGE_SYM_CLASS_BIT_FIELD:
                case IMAGE_SYM_CLASS_FAR_EXTERNAL:
                case IMAGE_SYM_CLASS_BLOCK:
                case IMAGE_SYM_CLASS_END_OF_STRUCT:
                case IMAGE_SYM_CLASS_SECTION:
                case IMAGE_SYM_CLASS_WEAK_EXTERNAL:
                case IMAGE_SYM_CLASS_CLR_TOKEN:
                    /* Not used by MS, I think. */
                    break;

                default:
                    Log(("RTDbgCv: Unexpected COFF storage class %#x (%u)\n", Sym.StorageClass, Sym.StorageClass));
                    break;
            }

            /* next symbol */
            iSymbol += 1 + Sym.NumberOfAuxSymbols;
        }

        /* Next section with line numbers. */
        iLineSect++;
    } while (iLine < cLines && iLineSect < cSections && RT_SUCCESS(rc));

    return rc;
}


/**
 * Loads COFF debug information into the container.
 *
 * @returns IPRT status code.
 * @param   pThis               The COFF/CodeView debug reader instance.
 */
static int rtDbgModCvLoadCoffInfo(PRTDBGMODCV pThis)
{
    /*
     * Read the whole section into memory.
     * Note! Cannot use rtDbgModCvReadAt or rtDbgModCvReadAtAlloc here.
     */
    int rc;
    uint8_t *pbDbgSect = (uint8_t *)RTMemAlloc(pThis->cbCoffDbgInfo);
    if (pbDbgSect)
    {
        if (pThis->hFile == NIL_RTFILE)
            rc = pThis->pMod->pImgVt->pfnReadAt(pThis->pMod, UINT32_MAX, pThis->offCoffDbgInfo, pbDbgSect, pThis->cbCoffDbgInfo);
        else
            rc = RTFileReadAt(pThis->hFile, pThis->offCoffDbgInfo, pbDbgSect, pThis->cbCoffDbgInfo, NULL);
        if (RT_SUCCESS(rc))
        {
            /* The string table follows after the symbol table. */
            const char *pszzStrTab = (const char *)(  pbDbgSect
                                                    + pThis->CoffHdr.LvaToFirstSymbol
                                                    + pThis->CoffHdr.NumberOfSymbols * sizeof(IMAGE_SYMBOL));
            uint32_t    cbStrTab = (uint32_t)((uintptr_t)(pbDbgSect + pThis->cbCoffDbgInfo) - (uintptr_t)pszzStrTab);
            /** @todo The symbol table starts with a size. Read it and checking. Also verify
             *        that the symtab ends with a terminator character. */

            rc = rtDbgModCvProcessCoffSymbolTable(pThis,
                                                  (PCIMAGE_SYMBOL)(pbDbgSect + pThis->CoffHdr.LvaToFirstSymbol),
                                                  pThis->CoffHdr.NumberOfSymbols,
                                                  (PCIMAGE_LINENUMBER)(pbDbgSect + pThis->CoffHdr.LvaToFirstLinenumber),
                                                  pThis->CoffHdr.NumberOfLinenumbers,
                                                  pszzStrTab, cbStrTab);
        }
        RTMemFree(pbDbgSect);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}






/*
 *
 * CodeView Debug module implementation.
 * CodeView Debug module implementation.
 * CodeView Debug module implementation.
 *
 */


/** @interface_method_impl{RTDBGMODVTDBG,pfnUnwindFrame} */
static DECLCALLBACK(int) rtDbgModCv_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    RT_NOREF(pMod, iSeg, off, pState);
    return VERR_DBG_NO_UNWIND_INFO;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByAddr} */
static DECLCALLBACK(int) rtDbgModCv_LineByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                                  PRTINTPTR poffDisp, PRTDBGLINE pLineInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModLineByAddr(pThis->hCnt, iSeg, off, poffDisp, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByOrdinal} */
static DECLCALLBACK(int) rtDbgModCv_LineByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModLineByOrdinal(pThis->hCnt, iOrdinal, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineCount} */
static DECLCALLBACK(uint32_t) rtDbgModCv_LineCount(PRTDBGMODINT pMod)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModLineCount(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineAdd} */
static DECLCALLBACK(int) rtDbgModCv_LineAdd(PRTDBGMODINT pMod, const char *pszFile, size_t cchFile, uint32_t uLineNo,
                                               uint32_t iSeg, RTUINTPTR off, uint32_t *piOrdinal)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    Assert(!pszFile[cchFile]); NOREF(cchFile);
    return RTDbgModLineAdd(pThis->hCnt, pszFile, uLineNo, iSeg, off, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByAddr} */
static DECLCALLBACK(int) rtDbgModCv_SymbolByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                                    PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSymbolByAddr(pThis->hCnt, iSeg, off, fFlags, poffDisp, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByName} */
static DECLCALLBACK(int) rtDbgModCv_SymbolByName(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                    PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); RT_NOREF_PV(cchSymbol);
    return RTDbgModSymbolByName(pThis->hCnt, pszSymbol/*, cchSymbol*/, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByOrdinal} */
static DECLCALLBACK(int) rtDbgModCv_SymbolByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSymbolByOrdinal(pThis->hCnt, iOrdinal, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolCount} */
static DECLCALLBACK(uint32_t) rtDbgModCv_SymbolCount(PRTDBGMODINT pMod)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSymbolCount(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolAdd} */
static DECLCALLBACK(int) rtDbgModCv_SymbolAdd(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                 RTDBGSEGIDX iSeg, RTUINTPTR off, RTUINTPTR cb, uint32_t fFlags,
                                                 uint32_t *piOrdinal)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); NOREF(cchSymbol);
    return RTDbgModSymbolAdd(pThis->hCnt, pszSymbol, iSeg, off, cb, fFlags, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentByIndex} */
static DECLCALLBACK(int) rtDbgModCv_SegmentByIndex(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSegmentByIndex(pThis->hCnt, iSeg, pSegInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentCount} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModCv_SegmentCount(PRTDBGMODINT pMod)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSegmentCount(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentAdd} */
static DECLCALLBACK(int) rtDbgModCv_SegmentAdd(PRTDBGMODINT pMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName, size_t cchName,
                                                  uint32_t fFlags, PRTDBGSEGIDX piSeg)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    Assert(!pszName[cchName]); NOREF(cchName);
    return RTDbgModSegmentAdd(pThis->hCnt, uRva, cb, pszName, fFlags, piSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnImageSize} */
static DECLCALLBACK(RTUINTPTR) rtDbgModCv_ImageSize(PRTDBGMODINT pMod)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    if (pThis->cbImage)
        return pThis->cbImage;
    return RTDbgModImageSize(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnRvaToSegOff} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModCv_RvaToSegOff(PRTDBGMODINT pMod, RTUINTPTR uRva, PRTUINTPTR poffSeg)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModRvaToSegOff(pThis->hCnt, uRva, poffSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnClose} */
static DECLCALLBACK(int) rtDbgModCv_Close(PRTDBGMODINT pMod)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;

    RTDbgModRelease(pThis->hCnt);
    if (pThis->hFile != NIL_RTFILE)
        RTFileClose(pThis->hFile);
    RTMemFree(pThis->paDirEnts);
    RTStrFree(pThis->pszPdbFilename);
    RTVfsRelease(pThis->hVfsPdb);
    pThis->hVfsPdb = NIL_RTVFS;
    RTMemFree(pThis);

    pMod->pvDbgPriv = NULL; /* for internal use */
    return VINF_SUCCESS;
}


/*
 *
 * Probing code used by rtDbgModCv_TryOpen.
 * Probing code used by rtDbgModCv_TryOpen.
 *
 */



/**
 * @callback_method_impl{FNRTLDRENUMSEGS, Used to add segments from the image}
 */
static DECLCALLBACK(int) rtDbgModCvAddSegmentsCallback(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pvUser;
    Log(("Segment %s: LinkAddress=%#llx RVA=%#llx cb=%#llx\n",
         pSeg->pszName, (uint64_t)pSeg->LinkAddress, (uint64_t)pSeg->RVA, pSeg->cb));
    NOREF(hLdrMod);

    /* If the segment doesn't have a mapping, just add a dummy so the indexing
       works out correctly (same as for the image). */
    if (pSeg->RVA == NIL_RTLDRADDR)
        return RTDbgModSegmentAdd(pThis->hCnt, 0, 0, pSeg->pszName, 0 /*fFlags*/, NULL);

    RTLDRADDR cb = RT_MAX(pSeg->cb, pSeg->cbMapped);
    return RTDbgModSegmentAdd(pThis->hCnt, pSeg->RVA, cb, pSeg->pszName, 0 /*fFlags*/, NULL);
}


/**
 * Helper for rounding the section size up to the start of the start of the
 * next section (e.g. RTLDRSEG::cbMapped approximation).
 */
DECLINLINE(uint32_t) rtDbgModCvAdjustSectionSizeByNext(PCIMAGE_SECTION_HEADER paShs, uint32_t cShs,
                                                       uint32_t iCur, uint32_t cbMapped)
{
    size_t iNext = iCur + 1;
    while (iNext < cShs && (paShs[iNext].Characteristics & IMAGE_SCN_TYPE_NOLOAD))
        iNext++;
    if (iNext < cShs)
    {
        uint32_t cbAvailable = paShs[iNext].VirtualAddress - paShs[iCur].VirtualAddress;
        Assert(cbMapped <= cbAvailable);
        return cbAvailable;
    }
    return cbMapped;
}


/**
 * Copies the sections over from the DBG file.
 *
 * Called if we don't have an associated executable image.
 *
 * @returns IPRT status code.
 * @param   pThis               The CV module instance.
 * @param   paShs               The section headers.
 * @param   cShs                The number of section headers.
 * @param   cbSectionAlign      The section alignment
 *                              (IMAGE_SEPARATE_DEBUG_HEADER::SectionAlignment,
 *                              IMAGE_OPTIONAL_HEADER32::SectionAlignment, ++).
 * @param   cbImage             The image size (if not availble, UINT32_MAX).
 * @param   pszFilename         The filename (for logging).
 */
static int rtDbgModCvAddSegmentsFromSectHdrs(PRTDBGMODCV pThis, PCIMAGE_SECTION_HEADER paShs, uint32_t cShs,
                                             uint32_t cbSectionAlign, uint32_t cbImage, const char *pszFilename)
{
    RT_NOREF_PV(pszFilename);
    Assert(RT_IS_POWER_OF_TWO(cbSectionAlign));

    /*
     * Do some basic validation of the section headers.
     */
    int      rc        = VINF_SUCCESS;
    uint32_t cbHeaders = 0;
    uint32_t uRvaPrev  = 0;
    for (uint32_t i = 0; i < cShs; i++)
    {
        Log3(("RTDbgModCv: Section #%02u %#010x LB %#010x %.*s\n",
              i, paShs[i].VirtualAddress, paShs[i].Misc.VirtualSize, sizeof(paShs[i].Name), paShs[i].Name));

        if (paShs[i].Characteristics & IMAGE_SCN_TYPE_NOLOAD)
            continue;

        if (paShs[i].VirtualAddress < uRvaPrev)
        {
            Log(("RTDbgModCv: %s: Overlap or sorting error, VirtualAddress=%#x uRvaPrev=%#x - section #%d '%.*s'!!!\n",
                 pszFilename, paShs[i].VirtualAddress, uRvaPrev, i, sizeof(paShs[i].Name), paShs[i].Name));
            rc = VERR_CV_BAD_FORMAT;
        }
        else if (   paShs[i].VirtualAddress                             > cbImage
                 || paShs[i].Misc.VirtualSize                           > cbImage
                 || paShs[i].VirtualAddress + paShs[i].Misc.VirtualSize > cbImage)
        {
            Log(("RTDbgModCv: %s: VirtualAddress=%#x VirtualSize=%#x (total %x) - beyond image size (%#x) - section #%d '%.*s'!!!\n",
                 pszFilename, paShs[i].VirtualAddress, paShs[i].Misc.VirtualSize,
                 paShs[i].VirtualAddress + paShs[i].Misc.VirtualSize,
                 pThis->cbImage, i, sizeof(paShs[i].Name), paShs[i].Name));
            rc = VERR_CV_BAD_FORMAT;
        }
        else if (paShs[i].VirtualAddress & (cbSectionAlign - 1))
        {
            Log(("RTDbgModCv: %s: VirtualAddress=%#x misaligned (%#x) - section #%d '%.*s'!!!\n",
                 pszFilename, paShs[i].VirtualAddress, cbSectionAlign, i, sizeof(paShs[i].Name), paShs[i].Name));
            rc = VERR_CV_BAD_FORMAT;
        }
        else if (paShs[i].Characteristics & IMAGE_SCN_ALIGN_MASK)
        {
            uint32_t const cbAlign = RT_BIT_32((paShs[i].Characteristics & IMAGE_SCN_ALIGN_MASK) >> IMAGE_SCN_ALIGN_SHIFT);
            if (RT_ALIGN_32(paShs[i].VirtualAddress, cbAlign) != paShs[i].VirtualAddress)
            {
                Log(("RTDbgModCv: %s: VirtualAddress=%#x misaligned by flags (%#x) - section #%d '%.*s'!!!\n",
                     pszFilename, paShs[i].VirtualAddress, cbAlign, i, sizeof(paShs[i].Name), paShs[i].Name));
                rc = VERR_CV_BAD_FORMAT;
            }
        }

        if (uRvaPrev == 0)
            cbHeaders = paShs[i].VirtualAddress;
        uRvaPrev = paShs[i].VirtualAddress + paShs[i].Misc.VirtualSize;
    }
    if (uRvaPrev == 0)
    {
        Log(("RTDbgModCv: %s: No loadable sections.\n", pszFilename));
        rc = VERR_CV_BAD_FORMAT;
    }
    if (cbHeaders == 0)
    {
        Log(("RTDbgModCv: %s: No space for PE headers.\n", pszFilename));
        rc = VERR_CV_BAD_FORMAT;
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Add sections.
         */
        rc = RTDbgModSegmentAdd(pThis->hCnt, 0, cbHeaders, "NtHdrs", 0 /*fFlags*/, NULL);
        if (RT_SUCCESS(rc))
        {
            for (uint32_t i = 0; RT_SUCCESS(rc) && i < cShs; i++)
            {
                char szName[sizeof(paShs[i].Name) + 1];
                memcpy(szName, paShs[i].Name, sizeof(paShs[i].Name));
                szName[sizeof(szName) - 1] = '\0';
                RTStrStripR(szName);

                uint32_t uRva = paShs[i].VirtualAddress;
                uint32_t cbMapped;
                if (!(paShs[i].Characteristics & IMAGE_SCN_TYPE_NOLOAD))
                    cbMapped = rtDbgModCvAdjustSectionSizeByNext(paShs, cShs, i, paShs[i].Misc.VirtualSize);
                else
                    uRva = cbMapped = 0;
                rc = RTDbgModSegmentAdd(pThis->hCnt, paShs[i].VirtualAddress, cbMapped, szName, 0 /*fFlags*/, NULL);
                if (RT_FAILURE(rc))
                {
                    Log(("RTDbgModCv: RTDbgModSegmentAdd failed on #%u: uRva=%#RX32 cbMapped=%#RX32 Flags=%#RX32 '%s' -> %Rrc\n",
                         i + 1, uRva, cbMapped, szName, paShs[i].Characteristics, rc));
                    break;
                }

            }
            if (RT_SUCCESS(rc))
                pThis->fHaveLoadedSegments = true;
        }
        else
            Log(("RTDbgModCv: RTDbgModSegmentAdd on 'NtHdrs': cbHeaders=%#RX32 - %Rrc\n", cbHeaders, rc));
    }
    return rc;
}


/**
 * Copies the sections over from the DBG file.
 *
 * Called if we don't have an associated executable image.
 *
 * @returns IPRT status code.
 * @param   pThis               The CV module instance.
 * @param   pDbgHdr             The DBG file header.
 * @param   pszFilename         The filename (for logging).
 */
static int rtDbgModCvAddSegmentsFromDbg(PRTDBGMODCV pThis, PCIMAGE_SEPARATE_DEBUG_HEADER pDbgHdr, const char *pszFilename)
{
    RT_NOREF_PV(pszFilename);

    /*
     * Validate the header fields a little.
     */
    if (   pDbgHdr->NumberOfSections < 1
        || pDbgHdr->NumberOfSections > 4096)
    {
        Log(("RTDbgModCv: Bad NumberOfSections: %d\n", pDbgHdr->NumberOfSections));
        return VERR_CV_BAD_FORMAT;
    }

    /*
     * Read the section table.
     */
    size_t const                cbShs = pDbgHdr->NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
    PIMAGE_SECTION_HEADER const paShs = (PIMAGE_SECTION_HEADER)RTMemTmpAlloc(cbShs);
    if (paShs)
    {
        int rc = RTFileReadAt(pThis->hFile, sizeof(*pDbgHdr), paShs, cbShs, NULL);
        if (RT_SUCCESS(rc))
        {
            /*
             * Process them.
             */
            rc = rtDbgModCvAddSegmentsFromSectHdrs(pThis, paShs, pDbgHdr->NumberOfSections, pDbgHdr->SectionAlignment,
                                                   pDbgHdr->SizeOfImage, pszFilename);
        }
        RTMemTmpFree(paShs);
        return rc;
    }
    return VERR_NO_TMP_MEMORY;
}


/**
 * Instantiates the CV/COFF reader.
 *
 * @returns IPRT status code
 * @param   pDbgMod             The debug module instance.
 * @param   enmFileType         The type of input file.
 * @param   hFile               The file handle, NIL_RTFILE of image.
 * @param   ppThis              Where to return the reader instance.
 */
static int rtDbgModCvCreateInstance(PRTDBGMODINT pDbgMod, RTCVFILETYPE enmFileType, RTFILE hFile, PRTDBGMODCV *ppThis)
{
    /*
     * Do we already have an instance?  Happens if we find multiple debug
     * formats we support.
     */
    PRTDBGMODCV pThis = (PRTDBGMODCV)pDbgMod->pvDbgPriv;
    if (pThis)
    {
        Assert(pThis->enmType == enmFileType);
        Assert(pThis->hFile == hFile);
        Assert(pThis->pMod == pDbgMod);
        *ppThis = pThis;
        return VINF_SUCCESS;
    }

    /*
     * Create a new instance.
     */
    pThis = (PRTDBGMODCV)RTMemAllocZ(sizeof(RTDBGMODCV));
    if (!pThis)
        return VERR_NO_MEMORY;
    int rc = RTDbgModCreate(&pThis->hCnt, pDbgMod->pszName, 0 /*cbSeg*/, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        pDbgMod->pvDbgPriv = pThis;
        pThis->enmType          = enmFileType;
        pThis->hFile            = hFile;
        pThis->pMod             = pDbgMod;
        pThis->offBase          = UINT32_MAX;
        pThis->offCoffDbgInfo   = UINT32_MAX;
        pThis->hVfsPdb          = NIL_RTVFS;
        *ppThis = pThis;
        return VINF_SUCCESS;
    }
    RTMemFree(pThis);
    return rc;
}


/**
 * Common part of the COFF probing.
 *
 * @returns status code.
 * @param   pDbgMod             The debug module instance.  On success pvDbgPriv
 *                              will point to a valid RTDBGMODCV.
 * @param   enmFileType         The kind of file this is we're probing.
 * @param   hFile               The file with debug info in it.
 * @param   off                 The offset where to expect CV debug info.
 * @param   cb                  The number of bytes of debug info.
 * @param   pszFilename         The path to the file (for logging).
 */
static int rtDbgModCvProbeCoff(PRTDBGMODINT pDbgMod, RTCVFILETYPE enmFileType, RTFILE hFile,
                               uint32_t off, uint32_t cb, const char *pszFilename)
{
    RT_NOREF_PV(pszFilename);

    /*
     * Check that there is sufficient data for a header, then read it.
     */
    if (cb < sizeof(IMAGE_COFF_SYMBOLS_HEADER))
    {
        Log(("RTDbgModCv: Not enough room for COFF header.\n"));
        return VERR_BAD_EXE_FORMAT;
    }
    if (cb >= UINT32_C(128) * _1M)
    {
        Log(("RTDbgModCv: COFF debug information is to large (%'u bytes), max is 128MB\n", cb));
        return VERR_BAD_EXE_FORMAT;
    }

    int rc;
    IMAGE_COFF_SYMBOLS_HEADER Hdr;
    if (hFile == NIL_RTFILE)
        rc = pDbgMod->pImgVt->pfnReadAt(pDbgMod, UINT32_MAX, off, &Hdr, sizeof(Hdr));
    else
        rc = RTFileReadAt(hFile, off, &Hdr, sizeof(Hdr), NULL);
    if (RT_FAILURE(rc))
    {
        Log(("RTDbgModCv: Error reading COFF header: %Rrc\n", rc));
        return rc;
    }

    Log2(("RTDbgModCv: Found COFF debug info header at %#x (LB %#x) in %s\n", off, cb, pszFilename));
    Log2(("    NumberOfSymbols      = %#010x\n", Hdr.NumberOfSymbols));
    Log2(("    LvaToFirstSymbol     = %#010x\n", Hdr.LvaToFirstSymbol));
    Log2(("    NumberOfLinenumbers  = %#010x\n", Hdr.NumberOfLinenumbers));
    Log2(("    LvaToFirstLinenumber = %#010x\n", Hdr.LvaToFirstLinenumber));
    Log2(("    RvaToFirstByteOfCode = %#010x\n", Hdr.RvaToFirstByteOfCode));
    Log2(("    RvaToLastByteOfCode  = %#010x\n", Hdr.RvaToLastByteOfCode));
    Log2(("    RvaToFirstByteOfData = %#010x\n", Hdr.RvaToFirstByteOfData));
    Log2(("    RvaToLastByteOfData  = %#010x\n", Hdr.RvaToLastByteOfData));

    /*
     * Validate the COFF header.
     */
    if (   (uint64_t)Hdr.LvaToFirstSymbol + (uint64_t)Hdr.NumberOfSymbols * sizeof(IMAGE_SYMBOL) > cb
        || (Hdr.LvaToFirstSymbol < sizeof(Hdr) && Hdr.NumberOfSymbols > 0))
    {
        Log(("RTDbgModCv: Bad COFF symbol count or/and offset: LvaToFirstSymbol=%#x, NumberOfSymbols=%#x cbCoff=%#x\n",
             Hdr.LvaToFirstSymbol, Hdr.NumberOfSymbols, cb));
        return VERR_BAD_EXE_FORMAT;
    }
    if (   (uint64_t)Hdr.LvaToFirstLinenumber + (uint64_t)Hdr.NumberOfLinenumbers * sizeof(IMAGE_LINENUMBER) > cb
        || (Hdr.LvaToFirstLinenumber < sizeof(Hdr) && Hdr.NumberOfLinenumbers > 0))
    {
        Log(("RTDbgModCv: Bad COFF symbol count or/and offset: LvaToFirstSymbol=%#x, NumberOfSymbols=%#x cbCoff=%#x\n",
             Hdr.LvaToFirstSymbol, Hdr.NumberOfSymbols, cb));
        return VERR_BAD_EXE_FORMAT;
    }
    if (Hdr.NumberOfSymbols < 2)
    {
        Log(("RTDbgModCv: The COFF symbol table is too short to be of any worth... (%u syms)\n", Hdr.NumberOfSymbols));
        return VERR_NO_DATA;
    }

    /*
     * What we care about looks fine, use it.
     */
    PRTDBGMODCV pThis;
    rc = rtDbgModCvCreateInstance(pDbgMod, enmFileType, hFile, &pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->offCoffDbgInfo = off;
        pThis->cbCoffDbgInfo  = cb;
        pThis->CoffHdr        = Hdr;
    }

    return rc;
}

/**
 * Argument package for rtDbgModCvOpenPdb20Callback via pvUser2.
 */
typedef struct RTDBGMODCVOPENPDB20CALLBACK
{
    RTLDRARCH       enmArch;
    RTFILE          hFile;
    RTCVFILETYPE    enmFileType;
    uint32_t        uTimestamp;
    uint32_t        uAge;
} RTDBGMODCVOPENPDB20CALLBACK;

/** @callback_method_impl{FNRTDBGCFGOPEN,
 *      For opening PDB linked in windows 2000 area DBG files.}
 */
static DECLCALLBACK(int) rtDbgModCvOpenPdb20Callback(RTDBGCFG hDbgCfg, const char *pszFilename, void *pvUser1, void *pvUser2)
{
    PRTDBGMODINT const                  pDbgMod = (PRTDBGMODINT)pvUser1;
    RTDBGMODCVOPENPDB20CALLBACK * const pArgs   = (RTDBGMODCVOPENPDB20CALLBACK *)pvUser2;
    RT_NOREF(hDbgCfg);

    /*
     * Open the file as a VFS and check that the timestamp and age matches
     * what we're looking for.
     */
    Log2(("rtDbgModCvOpenPdb20Callback: Trying '%s'...\n", pszFilename));
    RTVFSFILE hVfsFilePdb = NIL_RTVFSFILE;
    int rc = RTVfsFileOpenNormal(pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hVfsFilePdb);
    if (RT_SUCCESS(rc))
    {
        RTVFS           hVfsPdb = NIL_RTVFS;
#ifdef LOG_ENABLED
        RTERRINFOSTATIC ErrInfo;
        rc = RTFsPdbVolOpen(hVfsFilePdb, 0, &hVfsPdb, RTErrInfoInitStatic(&ErrInfo));
#else
        rc = RTFsPdbVolOpen(hVfsFilePdb, 0, &hVfsPdb, NULL /*pErrInfo*/);
#endif
        RTVfsFileRelease(hVfsFilePdb);
        if (RT_SUCCESS(rc))
        {
            /*
             * The timestamp + age is available as a string.
             */
            size_t cbRet;
            char   szTimestampAndAge[64] = {0};
            rc = RTVfsQueryLabel(hVfsPdb, false /*RTVFSQIEX_VOL_LABEL*/,
                                 szTimestampAndAge, sizeof(szTimestampAndAge) - 1, &cbRet);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                char szExpect[64];
                RTStrPrintf(szExpect, sizeof(szExpect), "%08X%x", pArgs->uTimestamp, pArgs->uAge);
                if (RTStrICmpAscii(szTimestampAndAge, szExpect) == 0)
                {
                    /** @todo check ARCH. It's usually in the DBI header.   */

                    /*
                     * Okay, we're good.
                     */
                    PRTDBGMODCV pThis;
                    rc = rtDbgModCvCreateInstance(pDbgMod, pArgs->enmFileType, pArgs->hFile, &pThis);
                    if (RT_SUCCESS(rc))
                    {
                        pThis->u32CvMagic     = RTCVHDR_MAGIC_NB10;
                        pThis->offBase        = UINT32_MAX;
                        pThis->cbDbgInfo      = 0;
                        pThis->offDir         = 0;
                        pThis->pszPdbFilename = RTStrDup(pszFilename);
                        if (pThis->pszPdbFilename)
                        {
                            pThis->hVfsPdb = hVfsPdb;
                            return VINF_CALLBACK_RETURN;
                        }
                    }
                }
                else
                    Log2(("RTFsPdbVolOpen: timestamp+age mismatch: %s, expected %s (for '%s')\n",
                          szTimestampAndAge, szExpect, pszFilename));
            }
        }
        else
            Log2(("RTFsPdbVolOpen: RTVfsFileOpenNormal '%s' failed: %Rrc%#RTeim\n", pszFilename, rc, &ErrInfo.Core));
    }
    else
        Log2(("rtDbgModCvOpenPdb20Callback: RTVfsFileOpenNormal '%s' failed: %Rrc\n", pszFilename, rc));
    return rc;
}


/**
 * Common part of the CodeView probing.
 *
 * @returns status code.
 * @param   pDbgMod             The debug module instance.  On success pvDbgPriv
 *                              will point to a valid RTDBGMODCV.
 * @param   pCvHdr              The CodeView base header.
 * @param   enmFileType         The kind of file this is we're probing.
 * @param   hFile               The file with debug info in it.
 * @param   off                 The offset where to expect CV debug info.
 * @param   cb                  The number of bytes of debug info.
 * @param   enmArch             The desired image architecture.
 * @param   cbImage             The image size, if available.
 * @param   pszFilename         The path to the file (for logging).
 * @param   hDbgCfg             For dealing with external PDB found in windows
 *                              2000 area DBG files.
 */
static int rtDbgModCvProbeCommon(PRTDBGMODINT pDbgMod, PRTCVHDR pCvHdr, RTCVFILETYPE enmFileType, RTFILE hFile,
                                 uint32_t off, uint32_t cb, RTLDRARCH enmArch, size_t cbImage,
                                 const char *pszFilename, RTDBGCFG hDbgCfg)
{
    int rc = VERR_DBG_NO_MATCHING_INTERPRETER;
    RT_NOREF_PV(enmArch); RT_NOREF_PV(pszFilename);

    /* Is a codeview format we (wish to) support? */
    if (   pCvHdr->u32Magic == RTCVHDR_MAGIC_NB11
        || pCvHdr->u32Magic == RTCVHDR_MAGIC_NB09
        || pCvHdr->u32Magic == RTCVHDR_MAGIC_NB08
        || pCvHdr->u32Magic == RTCVHDR_MAGIC_NB05
        || pCvHdr->u32Magic == RTCVHDR_MAGIC_NB04
        || pCvHdr->u32Magic == RTCVHDR_MAGIC_NB02
        || pCvHdr->u32Magic == RTCVHDR_MAGIC_NB00
       )
    {
        /* We're assuming it's a base header, so the offset must be within
           the area defined by the debug info we got from the loader. */
        if (pCvHdr->off < cb && pCvHdr->off >= sizeof(*pCvHdr))
        {
            Log(("RTDbgModCv: Found %c%c%c%c at %#x - size %#x, directory at %#x. file type %d\n",
                 RT_BYTE1(pCvHdr->u32Magic), RT_BYTE2(pCvHdr->u32Magic), RT_BYTE3(pCvHdr->u32Magic), RT_BYTE4(pCvHdr->u32Magic),
                 off, cb, pCvHdr->off, enmFileType));

            /*
             * Create a module instance, if not already done.
             */
            PRTDBGMODCV pThis;
            rc = rtDbgModCvCreateInstance(pDbgMod, enmFileType, hFile, &pThis);
            if (RT_SUCCESS(rc))
            {
                pThis->u32CvMagic = pCvHdr->u32Magic;
                pThis->offBase    = off;
                pThis->cbDbgInfo  = cb;
                pThis->offDir     = pCvHdr->off;
                return VINF_SUCCESS;
            }
        }
    }
    /*
     * The NB10 is used to link to a PDB file.
     * This is the case with the windows 2000 DBG files.
     */
    else if (pCvHdr->u32Magic == RTCVHDR_MAGIC_NB10)
    {
        if (pCvHdr->off == 0)
        {
            if (cb > RT_UOFFSETOF(CVPDB20INFO, szPdbFilename) && cb < _64K)
            {
                /*
                 * Read it into memory and validate it.
                 */
                PCVPDB20INFO pInfo = (PCVPDB20INFO)RTMemTmpAlloc(cb);
                if (pInfo)
                {
                    int rc2 = RTFileReadAt(hFile, off, pInfo, cb, NULL);
                    if (RT_SUCCESS(rc2))
                    {
                        uint32_t const cbPdbFilename = cb - RT_UOFFSETOF(CVPDB20INFO, szPdbFilename);
                        rc2 = RTStrValidateEncodingEx((char const *)&pInfo->szPdbFilename[0], cbPdbFilename,
                                                      RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
                        if (RT_SUCCESS(rc2))
                        {
                            Log2(("RTDbgModCv: Found external PDB (v2.0): TS=%#010RX32 uAge=%x '%.*s'\n",  pInfo->uTimestamp,
                                  pInfo->uAge, cb - RT_UOFFSETOF(CVPDB20INFO, szPdbFilename), &pInfo->szPdbFilename[0]));
                            if (   hDbgCfg != NIL_RTDBGCFG
                                && enmFileType == RTCVFILETYPE_DBG)
                            {
                                /*
                                 * Try open it. Need to figure the image size first if
                                 * we can, though not actually used by the function.
                                 *
                                 * The callback will create an instance if a PDB is found,
                                 * but it won't actually read it. That's done later by the
                                 * RTDbgModCv_TryOpen code.
                                 */
                                RTDBGMODCVOPENPDB20CALLBACK Args = { enmArch, hFile, enmFileType, pInfo->uTimestamp, pInfo->uAge };
                                rc = RTDbgCfgOpenPdb20(hDbgCfg, (const char *)&pInfo->szPdbFilename[0],
                                                       (uint32_t)cbImage, pInfo->uTimestamp, pInfo->uAge,
                                                       rtDbgModCvOpenPdb20Callback, pDbgMod, &Args);
                                Log(("RTDbgModCv: RTDbgCfgOpenPdb20 returns %Rrc\n", rc));
                            }
                        }
                        else
                            Log(("RTDbgModCv: Found external PDB (v2.0) w/ invalid filename encoding: TS=%#010RX32 uAge=%x %.*Rhxs rc2=%Rrc\n",
                                 pInfo->uTimestamp, pInfo->uAge, cbPdbFilename, &pInfo->szPdbFilename[0], rc2));
                    }
                    else
                        Log(("RTDbgModCv: NB10 read error - %Rrc\n", rc2));
                    RTMemTmpFree(pInfo);
                }
                else
                    rc = VERR_NO_TMP_MEMORY;
            }
            else
                Log(("RTDbgModCv: NB10 with bogus size: %#x\n", cb));
        }
        else
            Log(("RTDbgModCv: NB10 with non-zero offset!\n"));
    }
    return rc;
}


/**
 * Arguments passed to rtDbgModCvEnumCallback
 */
typedef struct RTDBGMODCVENUMCALLBACKARGS
{
    PRTDBGMODINT    pDbgMod;
    RTDBGCFG        hDbgCfg;
} RTDBGMODCVENUMCALLBACKARGS;


/** @callback_method_impl{FNRTLDRENUMDBG} */
static DECLCALLBACK(int) rtDbgModCvEnumCallback(RTLDRMOD hLdrMod, PCRTLDRDBGINFO pDbgInfo, void *pvUser)
{
    RTDBGMODCVENUMCALLBACKARGS * const pArgs   = (RTDBGMODCVENUMCALLBACKARGS *)pvUser;
    PRTDBGMODINT  const                pDbgMod = pArgs->pDbgMod;
    Assert(!pDbgMod->pvDbgPriv);
    RT_NOREF_PV(hLdrMod);

    /* Skip external files, RTDbgMod will deal with those
       via RTDBGMODINT::pszDbgFile. */
    if (pDbgInfo->pszExtFile)
        return VINF_SUCCESS;

    /* We only handle the codeview sections. */
    if (pDbgInfo->enmType == RTLDRDBGINFOTYPE_CODEVIEW)
    {
        /* Read the specified header and check if we like it. */
        RTCVHDR CvHdr;
        int rc = pDbgMod->pImgVt->pfnReadAt(pDbgMod, pDbgInfo->iDbgInfo, pDbgInfo->offFile, &CvHdr, sizeof(CvHdr));
        if (RT_SUCCESS(rc))
            rc = rtDbgModCvProbeCommon(pDbgMod, &CvHdr, RTCVFILETYPE_IMAGE, NIL_RTFILE, pDbgInfo->offFile, pDbgInfo->cb,
                                       pDbgMod->pImgVt->pfnGetArch(pDbgMod), pDbgMod->pImgVt->pfnImageSize(pDbgMod),
                                       pDbgMod->pszImgFile, pArgs->hDbgCfg);
    }
    else if (pDbgInfo->enmType == RTLDRDBGINFOTYPE_COFF)
    {
        /* Join paths with the DBG code. */
        rtDbgModCvProbeCoff(pDbgMod, RTCVFILETYPE_IMAGE, NIL_RTFILE, pDbgInfo->offFile, pDbgInfo->cb, pDbgMod->pszImgFile);
    }

    return VINF_SUCCESS;
}


/**
 * Part two of the external file probing.
 *
 * @returns status code.
 * @param   pThis               The debug module instance.  On success pvDbgPriv
 *                              will point to a valid RTDBGMODCV.
 * @param   enmFileType         The kind of file this is we're probing.
 * @param   hFile               The file with debug info in it.
 * @param   off                 The offset where to expect CV debug info.
 * @param   cb                  The number of bytes of debug info.
 * @param   enmArch             The desired image architecture.
 * @param   cbImage             The image size (for PDB lookup, see hDbgCfg).
 * @param   pszFilename         The path to the file (for logging).
 * @param   hDbgCfg             Optional debug config handle for locating PDB
 *                              linked to by NB10 records (in DBG and
 *                              elsewhere).
 */
static int rtDbgModCvProbeFile2(PRTDBGMODINT pThis, RTCVFILETYPE enmFileType, RTFILE hFile, uint32_t off, uint32_t cb,
                                RTLDRARCH enmArch, size_t cbImage, const char *pszFilename, RTDBGCFG hDbgCfg)
{
    RTCVHDR CvHdr;
    int rc = RTFileReadAt(hFile, off, &CvHdr, sizeof(CvHdr), NULL);
    if (RT_SUCCESS(rc))
        rc = rtDbgModCvProbeCommon(pThis, &CvHdr, enmFileType, hFile, off, cb, enmArch, cbImage, pszFilename, hDbgCfg);
    return rc;
}

#ifdef LOG_ENABLED
/** Translate PE debug directory type value to string. */
static const char *dbgPeDebugTypeName(uint32_t uType)
{
    switch (uType)
    {
        case IMAGE_DEBUG_TYPE_UNKNOWN:       return "UNKNOWN";
        case IMAGE_DEBUG_TYPE_COFF:          return "COFF";
        case IMAGE_DEBUG_TYPE_CODEVIEW:      return "CODEVIEW";
        case IMAGE_DEBUG_TYPE_FPO:           return "FPO";
        case IMAGE_DEBUG_TYPE_MISC:          return "MISC";
        case IMAGE_DEBUG_TYPE_EXCEPTION:     return "EXCEPTION";
        case IMAGE_DEBUG_TYPE_FIXUP:         return "FIXUP";
        case IMAGE_DEBUG_TYPE_OMAP_TO_SRC:   return "OMAP_TO_SRC";
        case IMAGE_DEBUG_TYPE_OMAP_FROM_SRC: return "OMAP_FROM_SRC";
        case IMAGE_DEBUG_TYPE_BORLAND:       return "BORLAND";
        case IMAGE_DEBUG_TYPE_RESERVED10:    return "RESERVED10";
        case IMAGE_DEBUG_TYPE_CLSID:         return "CLSID";
        case IMAGE_DEBUG_TYPE_VC_FEATURE:    return "VC_FEATURE";
        case IMAGE_DEBUG_TYPE_POGO:          return "POGO";
        case IMAGE_DEBUG_TYPE_ILTCG:         return "ILTCG";
        case IMAGE_DEBUG_TYPE_MPX:           return "MPX";
        case IMAGE_DEBUG_TYPE_REPRO:         return "REPRO";
    }
    return "??";
}
#endif

/**
 * Probes an external file for CodeView information.
 *
 * @returns status code.
 * @param   pDbgMod             The debug module instance.  On success pvDbgPriv
 *                              will point to a valid RTDBGMODCV.
 * @param   pszFilename         The path to the file to probe.
 * @param   enmArch             The desired image architecture.
 * @param   hDbgCfg             Optional debug config handle for locating PDB
 *                              linked to by NB10 records (in DBG and
 *                              elsewhere).
 */
static int rtDbgModCvProbeFile(PRTDBGMODINT pDbgMod, const char *pszFilename, RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Check for .DBG file
     */
    IMAGE_SEPARATE_DEBUG_HEADER DbgHdr;
    rc = RTFileReadAt(hFile, 0, &DbgHdr, sizeof(DbgHdr), NULL);
    if (   RT_SUCCESS(rc)
        && DbgHdr.Signature == IMAGE_SEPARATE_DEBUG_SIGNATURE)
    {
        Log2(("RTDbgModCv: Found separate debug header in %s:\n", pszFilename));
        Log2(("    Flags              = %#x\n", DbgHdr.Flags));
        Log2(("    Machine            = %#x\n", DbgHdr.Machine));
        Log2(("    Characteristics    = %#x\n", DbgHdr.Characteristics));
        Log2(("    TimeDateStamp      = %#x\n", DbgHdr.TimeDateStamp));
        Log2(("    CheckSum           = %#x\n", DbgHdr.CheckSum));
        Log2(("    ImageBase          = %#x\n", DbgHdr.ImageBase));
        Log2(("    SizeOfImage        = %#x\n", DbgHdr.SizeOfImage));
        Log2(("    NumberOfSections   = %#x\n", DbgHdr.NumberOfSections));
        Log2(("    ExportedNamesSize  = %#x\n", DbgHdr.ExportedNamesSize));
        Log2(("    DebugDirectorySize = %#x\n", DbgHdr.DebugDirectorySize));
        Log2(("    SectionAlignment   = %#x\n", DbgHdr.SectionAlignment));

        /*
         * Match up the architecture if specified.
         */
        switch (enmArch)
        {
            case RTLDRARCH_X86_32:
                if (DbgHdr.Machine != IMAGE_FILE_MACHINE_I386)
                    rc = VERR_LDR_ARCH_MISMATCH;
                break;
            case RTLDRARCH_AMD64:
                if (DbgHdr.Machine != IMAGE_FILE_MACHINE_AMD64)
                    rc = VERR_LDR_ARCH_MISMATCH;
                break;

            default:
                RT_FALL_THROUGH();
            case RTLDRARCH_HOST:
                AssertFailedBreak();
            case RTLDRARCH_WHATEVER:
                break;
        }
        if (RT_FAILURE(rc))
        {
            RTFileClose(hFile);
            return rc;
        }

        /*
         * Probe for readable debug info in the debug directory.
         */
        uint32_t offDbgDir = sizeof(DbgHdr)
                           + DbgHdr.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)
                           + DbgHdr.ExportedNamesSize;

        uint32_t cEntries = DbgHdr.DebugDirectorySize / sizeof(IMAGE_DEBUG_DIRECTORY);
        for (uint32_t i = 0; i < cEntries; i++, offDbgDir += sizeof(IMAGE_DEBUG_DIRECTORY))
        {
            IMAGE_DEBUG_DIRECTORY DbgDir;
            rc = RTFileReadAt(hFile, offDbgDir, &DbgDir, sizeof(DbgDir), NULL);
            if (RT_FAILURE(rc))
                break;
            Log2(("DbgDir[%u]: Type=%#x Char=%#x TS=%#010x Ver=%#x.%#x Size=%#010x Addr=%#010x Ptr=%#010x %s\n",
                  i, DbgDir.Type, DbgDir.Characteristics, DbgDir.TimeDateStamp, DbgDir.MajorVersion, DbgDir.MinorVersion,
                  DbgDir.SizeOfData, DbgDir.AddressOfRawData, DbgDir.PointerToRawData, dbgPeDebugTypeName(DbgDir.Type)));
            if (DbgDir.Type == IMAGE_DEBUG_TYPE_CODEVIEW)
                rc = rtDbgModCvProbeFile2(pDbgMod, RTCVFILETYPE_DBG, hFile,
                                          DbgDir.PointerToRawData, DbgDir.SizeOfData,
                                          enmArch, DbgHdr.SizeOfImage, pszFilename, hDbgCfg);
            else if (DbgDir.Type == IMAGE_DEBUG_TYPE_COFF)
                rc = rtDbgModCvProbeCoff(pDbgMod, RTCVFILETYPE_DBG, hFile,
                                         DbgDir.PointerToRawData, DbgDir.SizeOfData, pszFilename);
            else
                continue;
            Log2(("DbgDir[%u]: parsing -> %Rrc\n", i, rc));
        }

#ifdef LOG_ENABLED
        /*
         * Dump the section table.
         */
        for (uint32_t i = 0, offSecHdr = sizeof(DbgHdr);
             i < DbgHdr.NumberOfSections;
             i++, offSecHdr += sizeof(IMAGE_SECTION_HEADER))
        {
            IMAGE_SECTION_HEADER Sh;
            rc = RTFileReadAt(hFile, offSecHdr, &Sh, sizeof(Sh), NULL);
            if (RT_FAILURE(rc))
                break;
            Log2(("SHdr[%02u]: VA=%#08RX32 LB %#08RX32 Char=%#010RX32 Ptr=%#08RX32 LB %#08RX32 PtrLines=%#RX32 L %#RX32 PtrReloc=%#RX32 L %#RX32 '%.8s'\n",
                  i, Sh.VirtualAddress, Sh.Misc.VirtualSize, Sh.Characteristics, Sh.PointerToRawData, Sh.SizeOfRawData,
                  Sh.PointerToLinenumbers, Sh.NumberOfLinenumbers, Sh.PointerToRelocations, Sh.NumberOfRelocations, &Sh.Name[0]));
        }
#endif

        /*
         * If we get down here with an instance, it prooves that we've found
         * something, regardless of any errors.  Add the sections and such.
         */
        PRTDBGMODCV pThis = (PRTDBGMODCV)pDbgMod->pvDbgPriv;
        if (pThis)
        {
            pThis->cbImage = DbgHdr.SizeOfImage;
            if (pDbgMod->pImgVt)
                rc = VINF_SUCCESS;
            else
            {
                rc = rtDbgModCvAddSegmentsFromDbg(pThis, &DbgHdr, pszFilename);
                if (RT_FAILURE(rc))
                    rtDbgModCv_Close(pDbgMod);
            }
            return rc;
        }

        /* Failed to find CV or smth, look at the end of the file just to be sure... */
    }

    /*
     * Look for CV tail header.
     */
    uint64_t cbFile;
    rc = RTFileSeek(hFile, -(RTFOFF)sizeof(RTCVHDR), RTFILE_SEEK_END, &cbFile);
    if (RT_SUCCESS(rc))
    {
        cbFile += sizeof(RTCVHDR);
        RTCVHDR CvHdr;
        rc = RTFileRead(hFile, &CvHdr, sizeof(CvHdr), NULL);
        if (RT_SUCCESS(rc))
            rc = rtDbgModCvProbeFile2(pDbgMod, RTCVFILETYPE_OTHER_AT_END, hFile,
                                      cbFile - CvHdr.off, CvHdr.off, enmArch,
                                      pDbgMod->pImgVt ? pDbgMod->pImgVt->pfnImageSize(pDbgMod) : 0 /*cbImage */,
                                      pszFilename, hDbgCfg);
    }

    if (RT_FAILURE(rc))
        RTFileClose(hFile);
    return rc;
}



/** @interface_method_impl{RTDBGMODVTDBG,pfnTryOpen} */
static DECLCALLBACK(int) rtDbgModCv_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    /*
     * Look for debug info.
     */
    int rc = VERR_DBG_NO_MATCHING_INTERPRETER;
    if (pMod->pszDbgFile)
        rc = rtDbgModCvProbeFile(pMod, pMod->pszDbgFile, enmArch, hDbgCfg);

    if (!pMod->pvDbgPriv && pMod->pImgVt)
    {
        RTDBGMODCVENUMCALLBACKARGS Args = { pMod, hDbgCfg };
        int rc2 = pMod->pImgVt->pfnEnumDbgInfo(pMod, rtDbgModCvEnumCallback, &Args);
        if (RT_FAILURE(rc2))
            rc = rc2;

        if (!pMod->pvDbgPriv)
        {
            /* Try the executable in case it has a NBxx tail header. */
            rc2 = rtDbgModCvProbeFile(pMod, pMod->pszImgFile, enmArch, hDbgCfg);
            if (RT_FAILURE(rc2) && (RT_SUCCESS(rc) || rc == VERR_DBG_NO_MATCHING_INTERPRETER))
                rc = rc2;
        }
    }

    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    if (!pThis)
        return RT_SUCCESS_NP(rc) ? VERR_DBG_NO_MATCHING_INTERPRETER : rc;
    Assert(   pThis->offBase != UINT32_MAX
           || pThis->offCoffDbgInfo != UINT32_MAX
           || (pThis->pszPdbFilename != NULL && pThis->hVfsPdb != NIL_RTVFS && pThis->enmType == RTCVFILETYPE_DBG));

    /*
     * Load the debug info.
     */
    if (pMod->pImgVt)
    {
        rc = pMod->pImgVt->pfnEnumSegments(pMod, rtDbgModCvAddSegmentsCallback, pThis);
        pThis->fHaveLoadedSegments = true;
    }
    if (RT_SUCCESS(rc) && pThis->offBase != UINT32_MAX)
        rc = rtDbgModCvLoadCodeViewInfo(pThis);
    if (RT_SUCCESS(rc) && pThis->offCoffDbgInfo != UINT32_MAX)
        rc = rtDbgModCvLoadCoffInfo(pThis);
    if (RT_SUCCESS(rc) && pThis->hVfsPdb != NIL_RTVFS)
    {
        rc = rtDbgModPdb_CommonOpenWorker(pThis, pThis->pszPdbFilename, pThis->hVfsPdb, hDbgCfg);
        RTVfsRelease(pThis->hVfsPdb);
        pThis->hVfsPdb = NIL_RTVFS;
    }
    if (RT_SUCCESS(rc))
    {
        Log(("RTDbgCv: Successfully loaded debug info\n"));
        return VINF_SUCCESS;
    }

    Log(("RTDbgCv: Debug info load error %Rrc\n", rc));
    rtDbgModCv_Close(pMod);
    RT_NOREF(hDbgCfg);
    return rc;
}





/** Virtual function table for the CodeView debug info reader. */
DECL_HIDDEN_CONST(RTDBGMODVTDBG) const g_rtDbgModVtDbgCodeView =
{
    /*.u32Magic = */            RTDBGMODVTDBG_MAGIC,
    /*.fSupports = */           RT_DBGTYPE_CODEVIEW,
    /*.pszName = */             "codeview",
    /*.pfnTryOpen = */          rtDbgModCv_TryOpen,
    /*.pfnClose = */            rtDbgModCv_Close,

    /*.pfnRvaToSegOff = */      rtDbgModCv_RvaToSegOff,
    /*.pfnImageSize = */        rtDbgModCv_ImageSize,

    /*.pfnSegmentAdd = */       rtDbgModCv_SegmentAdd,
    /*.pfnSegmentCount = */     rtDbgModCv_SegmentCount,
    /*.pfnSegmentByIndex = */   rtDbgModCv_SegmentByIndex,

    /*.pfnSymbolAdd = */        rtDbgModCv_SymbolAdd,
    /*.pfnSymbolCount = */      rtDbgModCv_SymbolCount,
    /*.pfnSymbolByOrdinal = */  rtDbgModCv_SymbolByOrdinal,
    /*.pfnSymbolByName = */     rtDbgModCv_SymbolByName,
    /*.pfnSymbolByAddr = */     rtDbgModCv_SymbolByAddr,

    /*.pfnLineAdd = */          rtDbgModCv_LineAdd,
    /*.pfnLineCount = */        rtDbgModCv_LineCount,
    /*.pfnLineByOrdinal = */    rtDbgModCv_LineByOrdinal,
    /*.pfnLineByAddr = */       rtDbgModCv_LineByAddr,

    /*.pfnUnwindFrame = */      rtDbgModCv_UnwindFrame,

    /*.u32EndMagic = */         RTDBGMODVTDBG_MAGIC
};


/*
 *
 * PDB Debug module implementation.
 * PDB Debug module implementation.
 * PDB Debug module implementation.
 *
 */


/** @interface_method_impl{RTDBGMODVTDBG,pfnUnwindFrame} */
static DECLCALLBACK(int) rtDbgModPdb_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    RT_NOREF(pMod, iSeg, off, pState);
    return VERR_DBG_NO_UNWIND_INFO;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByAddr} */
static DECLCALLBACK(int) rtDbgModPdb_LineByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                                PRTINTPTR poffDisp, PRTDBGLINE pLineInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModLineByAddr(pThis->hCnt, iSeg, off, poffDisp, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByOrdinal} */
static DECLCALLBACK(int) rtDbgModPdb_LineByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModLineByOrdinal(pThis->hCnt, iOrdinal, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineCount} */
static DECLCALLBACK(uint32_t) rtDbgModPdb_LineCount(PRTDBGMODINT pMod)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModLineCount(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineAdd} */
static DECLCALLBACK(int) rtDbgModPdb_LineAdd(PRTDBGMODINT pMod, const char *pszFile, size_t cchFile, uint32_t uLineNo,
                                             uint32_t iSeg, RTUINTPTR off, uint32_t *piOrdinal)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    Assert(!pszFile[cchFile]); NOREF(cchFile);
    return RTDbgModLineAdd(pThis->hCnt, pszFile, uLineNo, iSeg, off, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByAddr} */
static DECLCALLBACK(int) rtDbgModPdb_SymbolByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                                  PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSymbolByAddr(pThis->hCnt, iSeg, off, fFlags, poffDisp, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByName} */
static DECLCALLBACK(int) rtDbgModPdb_SymbolByName(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                  PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); RT_NOREF_PV(cchSymbol);
    return RTDbgModSymbolByName(pThis->hCnt, pszSymbol/*, cchSymbol*/, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByOrdinal} */
static DECLCALLBACK(int) rtDbgModPdb_SymbolByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSymbolByOrdinal(pThis->hCnt, iOrdinal, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolCount} */
static DECLCALLBACK(uint32_t) rtDbgModPdb_SymbolCount(PRTDBGMODINT pMod)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSymbolCount(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolAdd} */
static DECLCALLBACK(int) rtDbgModPdb_SymbolAdd(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                               RTDBGSEGIDX iSeg, RTUINTPTR off, RTUINTPTR cb, uint32_t fFlags,
                                               uint32_t *piOrdinal)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); NOREF(cchSymbol);
    return RTDbgModSymbolAdd(pThis->hCnt, pszSymbol, iSeg, off, cb, fFlags, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentByIndex} */
static DECLCALLBACK(int) rtDbgModPdb_SegmentByIndex(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSegmentByIndex(pThis->hCnt, iSeg, pSegInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentCount} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModPdb_SegmentCount(PRTDBGMODINT pMod)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModSegmentCount(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentAdd} */
static DECLCALLBACK(int) rtDbgModPdb_SegmentAdd(PRTDBGMODINT pMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName,
                                                size_t cchName, uint32_t fFlags, PRTDBGSEGIDX piSeg)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    Assert(!pszName[cchName]); NOREF(cchName);
    return RTDbgModSegmentAdd(pThis->hCnt, uRva, cb, pszName, fFlags, piSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnImageSize} */
static DECLCALLBACK(RTUINTPTR) rtDbgModPdb_ImageSize(PRTDBGMODINT pMod)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    if (pThis->cbImage)
        return pThis->cbImage;
    return RTDbgModImageSize(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnRvaToSegOff} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModPdb_RvaToSegOff(PRTDBGMODINT pMod, RTUINTPTR uRva, PRTUINTPTR poffSeg)
{
    PRTDBGMODCV pThis = (PRTDBGMODCV)pMod->pvDbgPriv;
    return RTDbgModRvaToSegOff(pThis->hCnt, uRva, poffSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnClose} */
static DECLCALLBACK(int) rtDbgModPdb_Close(PRTDBGMODINT pMod)
{
    return rtDbgModCv_Close(pMod);
}


/*
 *
 * Probing code used by rtDbgModPdb_TryOpen.
 * Probing code used by rtDbgModPdb_TryOpen.
 *
 */

static int rtDbgModPdb_ScanSymRecs(PRTDBGMODCV pThis, RTVFS hVfsPdb)
{
    RTVFSFILE hVfsFileSymRecs = NIL_RTVFSFILE;
    int rc = RTVfsFileOpen(hVfsPdb, "symbol-records", RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hVfsFileSymRecs);
    if (RT_SUCCESS(rc))
    {
        void    *pvSymRecs;
        size_t   cbSymRecs;
        rc = RTVfsFileReadAll(hVfsFileSymRecs, &pvSymRecs, &cbSymRecs);
        RTVfsFileRelease(hVfsFileSymRecs);
        if (RT_SUCCESS(rc))
        {
            Log3(("rtDbgModPdb_ScanSymRecs: %p LB %#x\n", pvSymRecs, cbSymRecs));
            rc = rtDbgModCvSsProcessV4PlusSymTab(pThis, pvSymRecs, cbSymRecs, 0 /*fFlags*/);

            RTVfsFileReadAllFree(pvSymRecs, cbSymRecs);
        }
        else
            Log(("rtDbgModPdb_ScanSymRecs: RTVfsFileReadAll failed - %Rrc\n", rc));
    }
    return rc;
}


static int rtDbgModPdb_LoadSegMap(PRTDBGMODCV pThis, RTVFS hVfsPdb)
{
    /*
     * Open the DBI section map and read it into a regular heap block just like
     * rtDbgModCvReadAtAlloc does for the other code path.
     */
    RTVFSFILE hVfsFile = NIL_RTVFSFILE;
    int rc = RTVfsFileOpen(hVfsPdb, "dbi-section-map", RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hVfsFile);
    if (RT_SUCCESS(rc))
    {
        uint64_t cbSegMap;
        rc = RTVfsFileQuerySize(hVfsFile, &cbSegMap);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            if (   cbSegMap >= RT_UOFFSETOF(RTCVSEGMAP, aDescs)
                && cbSegMap <= RT_UOFFSETOF_DYN(RTCVSEGMAP, aDescs[4096]))  /* random upper limit */
            {
                pThis->pSegMap = (PRTCVSEGMAP)RTMemAlloc((size_t)cbSegMap);
                if (pThis->pSegMap)
                {
                    rc = RTVfsFileReadAt(hVfsFile, 0, pThis->pSegMap, cbSegMap, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Join paths with the codeview module reader and process the map.
                         */
                        rc = rtDbgModCvProcessSegmentMap(pThis);

                    }
                    else
                        Log(("rtDbgModPdb_LoadSegMap: Read error: %Rrc\n", rc));
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else
            {
                Log(("rtDbgModPdb_LoadSegMap: Bogus section map size: %#RX64\n", cbSegMap));
                rc = VERR_CV_BAD_FORMAT;
            }
        }
        RTVfsFileRelease(hVfsFile);
    }
    else
    {
        Log(("rtDbgModPdb_LoadSegMap: Failed to open dbi-section-map: %Rrc\n", rc));
        if (rc == VERR_PATH_NOT_FOUND || rc == VERR_FILE_NOT_FOUND)
            rc = VINF_SUCCESS;
    }
    return rc;
}


/**
 * Helper for rtDbgModPdb_TryOpen that loads the section/segments into the
 * container.
 */
static int rtDbgModPdb_LoadSections(PRTDBGMODCV pThis, PRTDBGMODINT pDbgMod, RTVFS hVfsPdb, const char *pszFilename)
{
    /*
     * Use the info from the image if present.
     */
    int rc;
    if (pDbgMod->pImgVt)
        rc = pDbgMod->pImgVt->pfnEnumSegments(pDbgMod, rtDbgModCvAddSegmentsCallback, pThis);
    /*
     * Otherwise use the copy of the section table from the PDB.
     */
    else
    {
        RTVFSFILE hVfsFileSHdrs;
        rc = RTVfsFileOpen(hVfsPdb, "image-section-headers", RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hVfsFileSHdrs);
        if (RT_SUCCESS(rc))
        {
            PIMAGE_SECTION_HEADER paShs = NULL;
            size_t                cbSHdrs = 0;
            rc = RTVfsFileReadAll(hVfsFileSHdrs, (void **)&paShs, &cbSHdrs);
            RTVfsFileRelease(hVfsFileSHdrs);
            if (RT_SUCCESS(rc))
            {
                rc = rtDbgModCvAddSegmentsFromSectHdrs(pThis, paShs, (uint32_t)(cbSHdrs / sizeof(IMAGE_SECTION_HEADER)),
                                                       1 /* cbSectAlign - only for validation */,
                                                       pThis->cbImage ? (uint32_t)pThis->cbImage : UINT32_MAX /* only for validation */,
                                                       pszFilename);
                RTVfsFileReadAllFree(paShs, cbSHdrs);
            }
            else
                Log(("rtDbgModPdb_LoadSections: RTVfsFileReadAll failed - %Rrc\n", rc));
        }
        else
            Log2(("rtDbgModPdb_TryOpen: Failed to open 'image-section-headers' in '%s' -> %Rrc\n", pszFilename, rc));
    }
    RT_NOREF(pszFilename);
    pThis->fHaveLoadedSegments = true;
    return rc;
}


/**
 * Common worker for rtDbgModPdb_TryOpen and rtDbgModCv_TryOpen (for DBG+PDB).
 */
static int rtDbgModPdb_CommonOpenWorker(PRTDBGMODCV pThis, const char *pszFilename, RTVFS hVfsPdb, RTDBGCFG hDbgCfg)
{
    RT_NOREF(hDbgCfg, pszFilename);

    /*
     * We need section headers to successfully deal with the section (segment)
     * map and symbol records.  If there is an associated EXE or DBG file, they
     * will have segment information we can enumerate or load.  In the DBG case,
     * it's already done by the time we get here.   Otherwise, newer PDB files
     * will include a copy of the section header and we can use that of we got
     * no EXE or DBG files.
     */
    int rc = VINF_SUCCESS;
    if (!pThis->fHaveLoadedSegments)
        rc = rtDbgModPdb_LoadSections(pThis, pThis->pMod, hVfsPdb, pszFilename);
    if (RT_SUCCESS(rc))
    {
        /*
         * Load the section/segment map as we need to know how absolute segments
         * and such.  This should definitely be done after loading sections.
         * (Unfortuantely, the segement map doesn't not provide sufficient
         * information to replace the section headers if we cannot find them.)
         */
        rc = rtDbgModPdb_LoadSegMap(pThis, hVfsPdb);
        if (RT_SUCCESS(rc))
        {
            /*
             * Scan symbol records for symbol addresses and anything else we find
             * interesting.
             */
            rc = rtDbgModPdb_ScanSymRecs(pThis, hVfsPdb);
            if (RT_SUCCESS(rc))
            {
                /*
                 * We're good.
                 */
                /** @todo load source files and line numbers if present.   */
                /** @todo load unwind information. */
                Log(("rtDbgModPdb_TryOpen: Succeeded\n"));
                return VINF_SUCCESS;
            }
            Log(("rtDbgModPdb_TryOpen: rtDbgModPdb_ScanSymRecs failed - %Rrc\n", rc));
        }
        else
            Log(("rtDbgModPdb_TryOpen: rtDbgModPdb_LoadSegMap failed on '%s' -> %Rrc\n", pszFilename, rc));
    }
    else
        Log(("rtDbgModPdb_TryOpen: rtDbgModPdb_LoadSections failed on '%s' -> %Rrc\n", pszFilename, rc));
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnTryOpen} */
static DECLCALLBACK(int) rtDbgModPdb_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    /*
     * This only works for external files for now.
     */
    if (!pMod->pszDbgFile)
        return VERR_DBG_NO_MATCHING_INTERPRETER;

    /*
     * Open the PDB as a VFS.
     */
    RTVFSFILE hVfsFilePdb = NIL_RTVFSFILE;
    int rc = RTVfsFileOpenNormal(pMod->pszDbgFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE, &hVfsFilePdb);
    if (RT_SUCCESS(rc))
    {
        RTVFS           hVfsPdb = NIL_RTVFS;
#ifdef LOG_ENABLED
        RTERRINFOSTATIC ErrInfo;
        rc = RTFsPdbVolOpen(hVfsFilePdb, 0, &hVfsPdb, RTErrInfoInitStatic(&ErrInfo));
#else
        rc = RTFsPdbVolOpen(hVfsFilePdb, 0, &hVfsPdb, NULL /*pErrInfo*/);
#endif
        RTVfsFileRelease(hVfsFilePdb);
        if (RT_SUCCESS(rc))
        {
            /** @todo check enmArch. It's part of the new DBI header. */
            RT_NOREF(enmArch);

            /*
             * Create an instance so we can share the next bits with the DBG+PDB
             * code path in rtDbgModCv_TryOpen.  We need to be able to check if
             * section headers have been loaded among other things.
             */
            PRTDBGMODCV pThis;
            rc = rtDbgModCvCreateInstance(pMod, RTCVFILETYPE_PDB, NIL_RTFILE, &pThis);
            if (RT_SUCCESS(rc))
            {
                rc = rtDbgModPdb_CommonOpenWorker(pThis, pMod->pszDbgFile, hVfsPdb, hDbgCfg);
                if (RT_FAILURE(rc))
                    rtDbgModCv_Close(pMod);
            }
            RTVfsRelease(hVfsPdb);
        }
        else
            Log2(("rtDbgModPdb_TryOpen: RTFsPdbVolOpen '%s' -> %Rrc%#RTeim\n", pMod->pszDbgFile, rc, &ErrInfo.Core));
    }
    else
        Log2(("rtDbgModPdb_TryOpen: RTVfsFileOpenNormal '%s' -> %Rrc\n", pMod->pszDbgFile, rc));
    return rc;
}



/** Virtual function table for the PDB debug info reader. */
DECL_HIDDEN_CONST(RTDBGMODVTDBG) const g_rtDbgModVtDbgPdb =
{
    /*.u32Magic = */            RTDBGMODVTDBG_MAGIC,
    /*.fSupports = */           RT_DBGTYPE_CODEVIEW,
    /*.pszName = */             "pdb",
    /*.pfnTryOpen = */          rtDbgModPdb_TryOpen,
    /*.pfnClose = */            rtDbgModPdb_Close,

    /*.pfnRvaToSegOff = */      rtDbgModPdb_RvaToSegOff,
    /*.pfnImageSize = */        rtDbgModPdb_ImageSize,

    /*.pfnSegmentAdd = */       rtDbgModPdb_SegmentAdd,
    /*.pfnSegmentCount = */     rtDbgModPdb_SegmentCount,
    /*.pfnSegmentByIndex = */   rtDbgModPdb_SegmentByIndex,

    /*.pfnSymbolAdd = */        rtDbgModPdb_SymbolAdd,
    /*.pfnSymbolCount = */      rtDbgModPdb_SymbolCount,
    /*.pfnSymbolByOrdinal = */  rtDbgModPdb_SymbolByOrdinal,
    /*.pfnSymbolByName = */     rtDbgModPdb_SymbolByName,
    /*.pfnSymbolByAddr = */     rtDbgModPdb_SymbolByAddr,

    /*.pfnLineAdd = */          rtDbgModPdb_LineAdd,
    /*.pfnLineCount = */        rtDbgModPdb_LineCount,
    /*.pfnLineByOrdinal = */    rtDbgModPdb_LineByOrdinal,
    /*.pfnLineByAddr = */       rtDbgModPdb_LineByAddr,

    /*.pfnUnwindFrame = */      rtDbgModPdb_UnwindFrame,

    /*.u32EndMagic = */         RTDBGMODVTDBG_MAGIC
};

