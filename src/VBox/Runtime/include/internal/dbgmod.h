/* $Id: dbgmod.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal Header for RTDbgMod and the associated interpreters.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_INTERNAL_dbgmod_h
#define IPRT_INCLUDED_INTERNAL_dbgmod_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/critsect.h>
#include <iprt/ldr.h> /* for PFNRTLDRENUMDBG */
#include "internal/magics.h"

RT_C_DECLS_BEGIN

/** @addtogroup grp_rt_dbgmod
 * @internal
 * @{
 */


/** Pointer to the internal module structure. */
typedef struct RTDBGMODINT *PRTDBGMODINT;

/**
 * Virtual method table for executable image interpreters.
 */
typedef struct RTDBGMODVTIMG
{
    /** Magic number (RTDBGMODVTIMG_MAGIC). */
    uint32_t    u32Magic;
    /** Reserved. */
    uint32_t    fReserved;
    /** The name of the interpreter. */
    const char *pszName;

    /**
     * Try open the image.
     *
     * This combines probing and opening.
     *
     * @returns IPRT status code. No informational returns defined.
     *
     * @param   pMod        Pointer to the module that is being opened.
     *
     *                      The RTDBGMOD::pszDbgFile member will point to
     *                      the filename of any debug info we're aware of
     *                      on input. Also, or alternatively, it is expected
     *                      that the interpreter will look for debug info in
     *                      the executable image file when present and that it
     *                      may ask the image interpreter for this when it's
     *                      around.
     *
     *                      Upon successful return the method is expected to
     *                      initialize pImgOps and pvImgPriv.
     * @param   enmArch     The desired architecture.
     * @param   fLdrFlags   Extra loader flags (RTLDR_O_XXX).
     */
    DECLCALLBACKMEMBER(int, pfnTryOpen,(PRTDBGMODINT pMod, RTLDRARCH enmArch, uint32_t fLdrFlags));

    /**
     * Close the interpreter, freeing all associated resources.
     *
     * The caller sets the pDbgOps and pvDbgPriv RTDBGMOD members
     * to NULL upon return.
     *
     * @param   pMod        Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(int, pfnClose,(PRTDBGMODINT pMod));

    /**
     * Enumerate the debug info contained in the executable image.
     *
     * Identical to RTLdrEnumDbgInfo.
     *
     * @returns IPRT status code or whatever pfnCallback returns.
     *
     * @param   pMod            Pointer to the module structure.
     * @param   pfnCallback     The callback function.  Ignore the module
     *                          handle argument!
     * @param   pvUser          The user argument.
     */
    DECLCALLBACKMEMBER(int, pfnEnumDbgInfo,(PRTDBGMODINT pMod, PFNRTLDRENUMDBG pfnCallback, void *pvUser));

    /**
     * Enumerate the segments in the executable image.
     *
     * Identical to RTLdrEnumSegments.
     *
     * @returns IPRT status code or whatever pfnCallback returns.
     *
     * @param   pMod            Pointer to the module structure.
     * @param   pfnCallback     The callback function.  Ignore the module
     *                          handle argument!
     * @param   pvUser          The user argument.
     */
    DECLCALLBACKMEMBER(int, pfnEnumSegments,(PRTDBGMODINT pMod, PFNRTLDRENUMSEGS pfnCallback, void *pvUser));

    /**
     * Enumerates the symbols exported by the module.
     *
     * @returns iprt status code, which might have been returned by pfnCallback.
     * @param   pMod            Pointer to the module structure.
     * @param   fFlags          Flags indicating what to return and such.
     * @param   BaseAddress     The image base addressto use when calculating the
     *                          symbol values.
     * @param   pfnCallback     The callback function which each symbol is to be fed
     *                          to.
     * @param   pvUser          User argument to pass to the enumerator.
     */
    DECLCALLBACKMEMBER(int, pfnEnumSymbols,(PRTDBGMODINT pMod, uint32_t fFlags, RTLDRADDR BaseAddress,
                                            PFNRTLDRENUMSYMS pfnCallback, void *pvUser));

    /**
     * Gets the size of the loaded image.
     *
     * Identical to RTLdrSize.
     *
     * @returns The size in bytes, RTUINTPTR_MAX on failure.
     *
     * @param   pMod            Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(RTUINTPTR, pfnImageSize,(PRTDBGMODINT pMod));

    /**
     * Converts a link address to a segment:offset address (RVA included).
     *
     * @returns IPRT status code.
     *
     * @param   pMod            Pointer to the module structure.
     * @param   LinkAddress     The link address to convert.
     * @param   piSeg           The segment index.
     * @param   poffSeg         Where to return the segment offset.
     */
    DECLCALLBACKMEMBER(int, pfnLinkAddressToSegOffset,(PRTDBGMODINT pMod, RTLDRADDR LinkAddress,
                                                       PRTDBGSEGIDX piSeg, PRTLDRADDR poffSeg));

    /**
     * Converts an image relative virtual address to a segment:offset.
     *
     * @returns IPRT status code.
     *
     * @param   pMod            Pointer to the loader module structure.
     * @param   Rva             The RVA to convert.
     * @param   piSeg           The segment index.
     * @param   poffSeg         Where to return the segment offset.
     */
    DECLCALLBACKMEMBER(int, pfnRvaToSegOffset,(PRTDBGMODINT pMod, RTLDRADDR Rva, uint32_t *piSeg, PRTLDRADDR poffSeg));

    /**
     * Creates a read-only mapping of a part of the image file.
     *
     * @returns IPRT status code and *ppvMap set on success.
     *
     * @param   pMod            Pointer to the module structure.
     * @param   iDbgInfo        The debug info ordinal number if the request
     *                          corresponds exactly to a debug info part from
     *                          pfnEnumDbgInfo.  Otherwise, pass UINT32_MAX.
     * @param   off             The offset into the image file.
     * @param   cb              The number of bytes to map.
     * @param   ppvMap          Where to return the mapping address on success.
     *
     * @remarks Fixups will only be applied if @a iDbgInfo is specified.
     */
    DECLCALLBACKMEMBER(int, pfnMapPart,(PRTDBGMODINT pMod, uint32_t iDbgInfo, RTFOFF off, size_t cb, void const **ppvMap));

    /**
     * Unmaps memory previously mapped by pfnMapPart.
     *
     * @returns IPRT status code, *ppvMap set to NULL on success.
     *
     * @param   pMod            Pointer to the module structure.
     * @param   cb              The size of the mapping.
     * @param   ppvMap          The mapping address on input, NULL on
     *                          successful return.
     */
    DECLCALLBACKMEMBER(int, pfnUnmapPart,(PRTDBGMODINT pMod, size_t cb, void const **ppvMap));

    /**
     * Reads data from the image file.
     *
     * @returns IPRT status code, *ppvMap set to NULL on success.
     *
     * @param   pMod            Pointer to the module structure.
     * @param   iDbgInfoHint    The debug info ordinal number hint, pass UINT32_MAX
     *                          if not know or sure.
     * @param   off             The offset into the image file.
     * @param   pvBuf           The buffer to read into.
     * @param   cb              The number of bytes to read.
     */
    DECLCALLBACKMEMBER(int, pfnReadAt,(PRTDBGMODINT pMod, uint32_t iDbgInfoHint, RTFOFF off, void *pvBuf, size_t cb));

    /**
     * Gets the image format.
     *
     * @returns Valid image format on success, RTLDRFMT_INVALID if not supported.
     * @param   pMod            Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(RTLDRFMT, pfnGetFormat,(PRTDBGMODINT pMod));

    /**
     * Gets the image architecture.
     *
     * @returns Valid image architecutre on success, RTLDRARCH_WHATEVER if not
     *          supported.
     * @param   pMod            Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(RTLDRARCH, pfnGetArch,(PRTDBGMODINT pMod));

    /**
     * Generic method for querying image properties.
     *
     * @returns IPRT status code.
     * @param   pMod            Pointer to the module structure.
     * @param   enmProp         The property to query.
     * @param   pvBuf           Pointer to the return buffer.
     * @param   cbBuf           The size of the return buffer.
     * @param   pcbRet          How many bytes was actually returned.  In the
     *                          case of VERR_BUFFER_OVERFLOW this will contain
     *                          the required buffer size.  Optional.
     * @sa      RTLdrQueryPropEx
     */
    DECLCALLBACKMEMBER(int, pfnQueryProp,(PRTDBGMODINT pMod, RTLDRPROP enmProp, void *pvBuf, size_t cbBuf, size_t *pcbRet));

    /**
     * Try use unwind information to unwind one frame.
     *
     * @returns IPRT status code.  Last informational status from stack reader callback.
     * @retval  VERR_DBG_NO_UNWIND_INFO if the module contains no unwind information.
     * @retval  VERR_DBG_UNWIND_INFO_NOT_FOUND if no unwind information was found
     *          for the location given by iSeg:off.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   iSeg        The segment number of the program counter.
     * @param   off         The offset into @a iSeg.  Together with @a iSeg
     *                      this corresponds to the RTDBGUNWINDSTATE::uPc
     *                      value pointed to by @a pState.
     * @param   pState      The unwind state to work.
     *
     * @sa      RTLdrUnwindFrame, RTDbgModUnwindFrame
     */
    DECLCALLBACKMEMBER(int, pfnUnwindFrame,(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState));

    /** For catching initialization errors (RTDBGMODVTIMG_MAGIC). */
    uint32_t    u32EndMagic;
} RTDBGMODVTIMG;
/** Pointer to a const RTDBGMODVTIMG. */
typedef RTDBGMODVTIMG const *PCRTDBGMODVTIMG;


/**
 * Virtual method table for debug info interpreters.
 */
typedef struct RTDBGMODVTDBG
{
    /** Magic number (RTDBGMODVTDBG_MAGIC). */
    uint32_t    u32Magic;
    /** Mask of supported debug info types, see grp_rt_dbg_type.
     * Used to speed up the search for a suitable interpreter. */
    uint32_t    fSupports;
    /** The name of the interpreter. */
    const char *pszName;

    /**
     * Try open the image.
     *
     * This combines probing and opening.
     *
     * @returns IPRT status code. No informational returns defined.
     *
     * @param   pMod        Pointer to the module that is being opened.
     *
     *                      The RTDBGMOD::pszDbgFile member will point to
     *                      the filename of any debug info we're aware of
     *                      on input. Also, or alternatively, it is expected
     *                      that the interpreter will look for debug info in
     *                      the executable image file when present and that it
     *                      may ask the image interpreter for this when it's
     *                      around.
     *
     *                      Upon successful return the method is expected to
     *                      initialize pDbgOps and pvDbgPriv.
     * @param   enmArch     The desired architecture.
     * @param   hDbgCfg     Optional DBG configuration handle, for loading
     *                      additional external files and later logging debug file
     *                      errors during load.  Specifically this is to try deal
     *                      with stuff like the Windows 2000 DBG files which
     *                      contains a link to the PDB file with the actual symbol
     *                      information.
     */
    DECLCALLBACKMEMBER(int, pfnTryOpen,(PRTDBGMODINT pMod, RTLDRARCH enmArch, RTDBGCFG hDbgCfg));

    /**
     * Close the interpreter, freeing all associated resources.
     *
     * The caller sets the pDbgOps and pvDbgPriv RTDBGMOD members
     * to NULL upon return.
     *
     * @param   pMod        Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(int, pfnClose,(PRTDBGMODINT pMod));



    /**
     * Converts an image relative virtual address address to a segmented address.
     *
     * @returns Segment index on success, NIL_RTDBGSEGIDX on failure.
     * @param   pMod        Pointer to the module structure.
     * @param   uRva        The image relative address to convert.
     * @param   poffSeg     Where to return the segment offset. Optional.
     */
    DECLCALLBACKMEMBER(RTDBGSEGIDX, pfnRvaToSegOff,(PRTDBGMODINT pMod, RTUINTPTR uRva, PRTUINTPTR poffSeg));

    /**
     * Image size when mapped if segments are mapped adjacently.
     *
     * For ELF, PE, and Mach-O images this is (usually) a natural query, for LX and
     * NE and such it's a bit odder and the answer may not make much sense for them.
     *
     * @returns Image mapped size.
     * @param   pMod        Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(RTUINTPTR, pfnImageSize,(PRTDBGMODINT pMod));



    /**
     * Adds a segment to the module (optional).
     *
     * @returns IPRT status code.
     * @retval  VERR_NOT_SUPPORTED if the interpreter doesn't support this feature.
     * @retval  VERR_DBG_SEGMENT_INDEX_CONFLICT if the segment index exists already.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   uRva        The segment image relative address.
     * @param   cb          The segment size.
     * @param   pszName     The segment name.
     * @param   cchName     The length of the segment name.
     * @param   fFlags      Segment flags.
     * @param   piSeg       The segment index or NIL_RTDBGSEGIDX on input.
     *                      The assigned segment index on successful return.
     *                      Optional.
     */
    DECLCALLBACKMEMBER(int, pfnSegmentAdd,(PRTDBGMODINT pMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName, size_t cchName,
                                           uint32_t fFlags, PRTDBGSEGIDX piSeg));

    /**
     * Gets the segment count.
     *
     * @returns Number of segments.
     * @retval  NIL_RTDBGSEGIDX if unknown.
     *
     * @param   pMod        Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(RTDBGSEGIDX, pfnSegmentCount,(PRTDBGMODINT pMod));

    /**
     * Gets information about a segment.
     *
     * @returns IPRT status code.
     * @retval  VERR_DBG_INVALID_SEGMENT_INDEX if iSeg is too high.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   iSeg        The segment.
     * @param   pSegInfo    Where to store the segment information.
     */
    DECLCALLBACKMEMBER(int, pfnSegmentByIndex,(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo));



    /**
     * Adds a symbol to the module (optional).
     *
     * @returns IPRT  code.
     * @retval  VERR_NOT_SUPPORTED if the interpreter doesn't support this feature.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   pszSymbol   The symbol name.
     * @param   cchSymbol   The length for the symbol name.
     * @param   iSeg        The segment number (0-based). RTDBGMOD_SEG_RVA can be used.
     * @param   off         The offset into the segment.
     * @param   cb          The area covered by the symbol. 0 is fine.
     * @param   fFlags      Flags.
     * @param   piOrdinal   Where to return the symbol ordinal on success. If the
     *                      interpreter doesn't do ordinals, this will be set to
     *                      UINT32_MAX. Optional
     */
    DECLCALLBACKMEMBER(int, pfnSymbolAdd,(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                          uint32_t iSeg, RTUINTPTR off, RTUINTPTR cb, uint32_t fFlags,
                                          uint32_t *piOrdinal));

    /**
     * Gets the number of symbols in the module.
     *
     * This is used for figuring out the max value to pass to pfnSymbolByIndex among
     * other things.
     *
     * @returns The number of symbols, UINT32_MAX if not known/supported.
     *
     * @param   pMod        Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(uint32_t, pfnSymbolCount,(PRTDBGMODINT pMod));

    /**
     * Queries symbol information by ordinal number.
     *
     * @returns IPRT status code.
     * @retval  VINF_SUCCESS on success, no informational status code.
     * @retval  VERR_DBG_NO_SYMBOLS if there aren't any symbols.
     * @retval  VERR_NOT_SUPPORTED if lookup by ordinal is not supported.
     * @retval  VERR_SYMBOL_NOT_FOUND if there is no symbol at that index.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   iOrdinal    The symbol ordinal number.
     * @param   pSymInfo    Where to store the symbol information.
     */
    DECLCALLBACKMEMBER(int, pfnSymbolByOrdinal,(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo));

    /**
     * Queries symbol information by symbol name.
     *
     * @returns IPRT status code.
     * @retval  VINF_SUCCESS on success, no informational status code.
     * @retval  VERR_DBG_NO_SYMBOLS if there aren't any symbols.
     * @retval  VERR_SYMBOL_NOT_FOUND if no suitable symbol was found.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   pszSymbol   The symbol name.
     * @param   cchSymbol   The length of the symbol name.
     * @param   pSymInfo    Where to store the symbol information.
     */
    DECLCALLBACKMEMBER(int, pfnSymbolByName,(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol, PRTDBGSYMBOL pSymInfo));

    /**
     * Queries symbol information by address.
     *
     * The returned symbol is what the debug info interpreter considers the symbol
     * most applicable to the specified address. This usually means a symbol with an
     * address equal or lower than the requested.
     *
     * @returns IPRT status code.
     * @retval  VINF_SUCCESS on success, no informational status code.
     * @retval  VERR_DBG_NO_SYMBOLS if there aren't any symbols.
     * @retval  VERR_SYMBOL_NOT_FOUND if no suitable symbol was found.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   iSeg        The segment number (0-based) or RTDBGSEGIDX_ABS.
     * @param   off         The offset into the segment.
     * @param   fFlags      Symbol search flags, see RTDBGSYMADDR_FLAGS_XXX.
     * @param   poffDisp    Where to store the distance between the specified address
     *                      and the returned symbol. Optional.
     * @param   pSymInfo    Where to store the symbol information.
     */
    DECLCALLBACKMEMBER(int, pfnSymbolByAddr,(PRTDBGMODINT pMod, uint32_t iSeg, RTUINTPTR off, uint32_t fFlags,
                                             PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo));



    /**
     * Adds a line number to the module (optional).
     *
     * @returns IPRT status code.
     * @retval  VERR_NOT_SUPPORTED if the interpreter doesn't support this feature.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   pszFile     The filename.
     * @param   cchFile     The length of the filename.
     * @param   uLineNo     The line number.
     * @param   iSeg        The segment number (0-based).
     * @param   off         The offset into the segment.
     * @param   piOrdinal   Where to return the line number ordinal on success. If
     *                      the interpreter doesn't do ordinals, this will be set to
     *                      UINT32_MAX. Optional
     */
    DECLCALLBACKMEMBER(int, pfnLineAdd,(PRTDBGMODINT pMod, const char *pszFile, size_t cchFile, uint32_t uLineNo,
                                        uint32_t iSeg, RTUINTPTR off, uint32_t *piOrdinal));

    /**
     * Gets the number of line numbers in the module.
     *
     * @returns The number or UINT32_MAX if not known/supported.
     *
     * @param   pMod        Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(uint32_t, pfnLineCount,(PRTDBGMODINT pMod));

    /**
     * Queries line number information by ordinal number.
     *
     * @returns IPRT status code.
     * @retval  VINF_SUCCESS on success, no informational status code.
     * @retval  VERR_DBG_NO_LINE_NUMBERS if there aren't any line numbers.
     * @retval  VERR_DBG_LINE_NOT_FOUND if there is no line number with that
     *          ordinal.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   iOrdinal    The line number ordinal number.
     * @param   pLineInfo   Where to store the information about the line number.
     */
    DECLCALLBACKMEMBER(int, pfnLineByOrdinal,(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo));

    /**
     * Queries line number information by address.
     *
     * @returns IPRT status code.
     * @retval  VINF_SUCCESS on success, no informational status code.
     * @retval  VERR_DBG_NO_LINE_NUMBERS if there aren't any line numbers.
     * @retval  VERR_DBG_LINE_NOT_FOUND if no suitable line number was found.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   iSeg        The segment number (0-based) or RTDBGSEGIDX_ABS.
     * @param   off         The offset into the segment.
     * @param   poffDisp    Where to store the distance between the specified address
     *                      and the returned line number. Optional.
     * @param   pLineInfo   Where to store the information about the closest line
     *                      number.
     */
    DECLCALLBACKMEMBER(int, pfnLineByAddr,(PRTDBGMODINT pMod, uint32_t iSeg, RTUINTPTR off,
                                           PRTINTPTR poffDisp, PRTDBGLINE pLineInfo));

    /**
     * Try use unwind information to unwind one frame.
     *
     * @returns IPRT status code.  Last informational status from stack reader callback.
     * @retval  VERR_DBG_NO_UNWIND_INFO if the module contains no unwind information.
     * @retval  VERR_DBG_UNWIND_INFO_NOT_FOUND if no unwind information was found
     *          for the location given by iSeg:off.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   iSeg        The segment number of the program counter.
     * @param   off         The offset into @a iSeg.  Together with @a iSeg
     *                      this corresponds to the RTDBGUNWINDSTATE::uPc
     *                      value pointed to by @a pState.
     * @param   pState      The unwind state to work.
     *
     * @sa      RTDbgModUnwindFrame
     */
    DECLCALLBACKMEMBER(int, pfnUnwindFrame,(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState));

    /** For catching initialization errors (RTDBGMODVTDBG_MAGIC). */
    uint32_t    u32EndMagic;
} RTDBGMODVTDBG;
/** Pointer to a const RTDBGMODVTDBG. */
typedef RTDBGMODVTDBG const *PCRTDBGMODVTDBG;


/**
 * Deferred loading callback.
 *
 * @returns IPRT status code. On success the necessary method tables should be
 *          installed in @a pMod.
 * @param   pDbgMod         Pointer to the debug module structure.
 * @param   pDeferred       The deferred load data.
 */
typedef DECLCALLBACKTYPE(int, FNRTDBGMODDEFERRED,(PRTDBGMODINT pDbgMod, struct RTDBGMODDEFERRED *pDeferred));
/** Pointer to a deferred loading callback. */
typedef FNRTDBGMODDEFERRED *PFNRTDBGMODDEFERRED;


/**
 * Structure pointed to by pvDbgPriv and/or pvImgPriv when
 * g_rtDbgModVtDbgDeferred and/or g_rtDbgModVtImgDeferred are being used.
 */
typedef struct RTDBGMODDEFERRED
{
    /** Magic value (RTDBGMODDEFERRED_MAGIC). */
    uint32_t            u32Magic;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** RTDBGMOD_F_XXX */
    uint32_t            fFlags;
    /** The image size.
     * Deferred loading is almost pointless without knowing the module size, as
     * it cannot be mapped (correctly) without it. */
    RTUINTPTR           cbImage;
    /** The configuration instance (referenced), can be NIL. */
    RTDBGCFG            hDbgCfg;
    /** Performs deferred loading of the module. */
    PFNRTDBGMODDEFERRED pfnDeferred;
    /** Callback specific data.  */
    union
    {
        struct
        {
            /** The time/date stamp of the executable image and codeview file. */
            uint32_t    uTimestamp;
        }   PeImage,
            OldCodeView;

        struct
        {
            /** The PDB uuid. */
            RTUUID      Uuid;
            /** The PDB age. */
            uint32_t    uAge;
        }   NewCodeview;

        struct
        {
            /** The CRC-32 value found in the .gnu_debuglink section. */
            uint32_t    uCrc32;
        }   GnuDebugLink;

        struct
        {
            /** The image UUID. */
            RTUUID          Uuid;
            /** Image architecture. */
            RTLDRARCH       enmArch;
            /** Number of segment mappings. */
            uint32_t        cSegs;
            /** Segment mappings. */
            RTDBGSEGMENT    aSegs[1];
        }   MachO;
    } u;
} RTDBGMODDEFERRED;
/** Pointer to the deferred loading data. */
typedef RTDBGMODDEFERRED *PRTDBGMODDEFERRED;


/**
 * Debug module structure.
 */
typedef struct RTDBGMODINT
{
    /** Magic value (RTDBGMOD_MAGIC). */
    uint32_t            u32Magic;
    /** The number of reference there are to this module.
     * This is used to perform automatic cleanup and sharing. */
    uint32_t volatile   cRefs;
    /** The module tag. */
    uint64_t            uTag;

    /** When set, the loading of the image and debug info (including locating any
     * external files), will not have taken place yet.  */
    uint32_t            fDeferred : 1;
    /** Set if deferred loading failed. */
    uint32_t            fDeferredFailed : 1;
    /** Set if the debug info is based on image exports and segments. */
    uint32_t            fExports : 1;
    /** Alignment padding. */
    uint32_t            fPadding1 : 29;
#if ARCH_BITS == 64
    uint32_t            u32Padding2;
#endif

    /** The module name (short). */
    char const         *pszName;
    /** The image file specified by the user.  Can be NULL. */
    char const         *pszImgFileSpecified;
    /** The module filename. Can be NULL. */
    char const         *pszImgFile;
    /** The debug info file (if external). Can be NULL. */
    char const         *pszDbgFile;

    /** The method table for the executable image interpreter. */
    PCRTDBGMODVTIMG     pImgVt;
    /** Pointer to the private data of the executable image interpreter. */
    void               *pvImgPriv;

    /** The method table for the debug info interpreter. */
    PCRTDBGMODVTDBG     pDbgVt;
    /** Pointer to the private data of the debug info interpreter. */
    void               *pvDbgPriv;

    /** Critical section serializing access to the module. */
    RTCRITSECT          CritSect;
} RTDBGMODINT;
/** Pointer to an debug module structure.  */
typedef RTDBGMODINT *PRTDBGMODINT;


extern DECL_HIDDEN_DATA(RTSTRCACHE)             g_hDbgModStrCache;
extern DECL_HIDDEN_DATA(RTDBGMODVTDBG const)    g_rtDbgModVtDbgCodeView;
extern DECL_HIDDEN_DATA(RTDBGMODVTDBG const)    g_rtDbgModVtDbgPdb;
extern DECL_HIDDEN_DATA(RTDBGMODVTDBG const)    g_rtDbgModVtDbgDwarf;
extern DECL_HIDDEN_DATA(RTDBGMODVTDBG const)    g_rtDbgModVtDbgNm;
extern DECL_HIDDEN_DATA(RTDBGMODVTDBG const)    g_rtDbgModVtDbgMapSym;
#ifdef IPRT_WITH_GHIDRA_DBG_MOD
extern DECL_HIDDEN_DATA(RTDBGMODVTDBG const)    g_rtDbgModVtDbgGhidra;
#endif
#ifdef RT_OS_WINDOWS
extern DECL_HIDDEN_DATA(RTDBGMODVTDBG const)    g_rtDbgModVtDbgDbgHelp;
#endif
extern DECL_HIDDEN_DATA(RTDBGMODVTDBG const)    g_rtDbgModVtDbgDeferred;
extern DECL_HIDDEN_DATA(RTDBGMODVTDBG const)    g_rtDbgModVtDbgContainer;

extern DECL_HIDDEN_DATA(RTDBGMODVTIMG const)    g_rtDbgModVtImgLdr;
extern DECL_HIDDEN_DATA(RTDBGMODVTIMG const)    g_rtDbgModVtImgDeferred;

DECLHIDDEN(int) rtDbgModContainerCreate(PRTDBGMODINT pMod, RTUINTPTR cbSeg);
DECLHIDDEN(int) rtDbgModContainer_SymbolRemoveAll(PRTDBGMODINT pMod);
DECLHIDDEN(int) rtDbgModContainer_LineRemoveAll(PRTDBGMODINT pMod);
DECLHIDDEN(int) rtDbgModContainer_RemoveAll(PRTDBGMODINT pMod);

DECLHIDDEN(int) rtDbgModCreateForExports(PRTDBGMODINT pDbgMod);
DECLHIDDEN(int) rtDbgModDeferredCreate(PRTDBGMODINT pDbgMod, PFNRTDBGMODDEFERRED pfnDeferred, RTUINTPTR cbImage,
                                       RTDBGCFG hDbgCfg, size_t cbDeferred, uint32_t fFlags, PRTDBGMODDEFERRED *ppDeferred);

DECLHIDDEN(int) rtDbgModLdrOpenFromHandle(PRTDBGMODINT pDbgMod, RTLDRMOD hLdrMod);

DECLHIDDEN(int) rtDwarfUnwind_EhData(void const *pvSection, size_t cbSection, RTUINTPTR uRvaSection,
                                     RTDBGSEGIDX idxSeg, RTUINTPTR offSeg, RTUINTPTR uRva,
                                     PRTDBGUNWINDSTATE pState, RTLDRARCH enmArch);

/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_dbgmod_h */

