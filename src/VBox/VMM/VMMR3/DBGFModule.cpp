/* $Id: DBGFModule.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Module & Segment Management.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/** @page pg_dbgf_module    DBGFModule - Module & Segment Management
 *
 * A module is our representation of an executable binary. It's main purpose
 * is to provide segments that can be mapped into address spaces and thereby
 * provide debug info for those parts for the guest code or data.
 *
 * This module will not deal directly with debug info, it will only serve
 * as an interface between the debugger / symbol lookup and the debug info
 * readers.
 *
 * An executable binary doesn't need to have a file, or that is, we don't
 * need the file to create a module for it. There will be interfaces for
 * ROMs to register themselves so we can get to their symbols, and there
 * will be interfaces for the guest OS plugins (@see pg_dbgf_os) to
 * register kernel, drivers and other global modules.
 */

#if 0
#include <VBox/vmm/dbgf.h>


/** Special segment number that indicates that the offset is a relative
 * virtual address (RVA). I.e. an offset from the start of the module. */
#define DBGF_SEG_RVA UINT32_C(0xfffffff0)

/** @defgroup grp_dbgf_dbginfo Debug Info Types
 * @{ */
/** Other format. */
#define DBGF_DBGINFO_OTHER      RT_BIT_32(0)
/** Stabs. */
#define DBGF_DBGINFO_STABS      RT_BIT_32(1)
/** Debug With Arbitrary Record Format (DWARF). */
#define DBGF_DBGINFO_DWARF      RT_BIT_32(2)
/** Microsoft Codeview debug info. */
#define DBGF_DBGINFO_CODEVIEW   RT_BIT_32(3)
/** Watcom debug info. */
#define DBGF_DBGINFO_WATCOM     RT_BIT_32(4)
/** IBM High Level Language debug info. */
#define DBGF_DBGINFO_HLL        RT_BIT_32(5)
/** Old OS/2 and Windows symbol file. */
#define DBGF_DBGINFO_SYM        RT_BIT_32(6)
/** Map file. */
#define DBGF_DBGINFO_MAP        RT_BIT_32(7)
/** @} */

/** @defgroup grp_dbgf_exeimg Executable Image Types
 * @{ */
/** Some other format. */
#define DBGF_EXEIMG_OTHER       RT_BIT_32(0)
/** Portable Executable. */
#define DBGF_EXEIMG_PE          RT_BIT_32(1)
/** Linear eXecutable. */
#define DBGF_EXEIMG_LX          RT_BIT_32(2)
/** Linear Executable. */
#define DBGF_EXEIMG_LE          RT_BIT_32(3)
/** New Executable. */
#define DBGF_EXEIMG_NE          RT_BIT_32(4)
/** DOS Executable (Mark Zbikowski). */
#define DBGF_EXEIMG_MZ          RT_BIT_32(5)
/** COM Executable. */
#define DBGF_EXEIMG_COM         RT_BIT_32(6)
/** a.out Executable. */
#define DBGF_EXEIMG_AOUT        RT_BIT_32(7)
/** Executable and Linkable Format. */
#define DBGF_EXEIMG_ELF         RT_BIT_32(8)
/** Mach-O Executable (including FAT ones). */
#define DBGF_EXEIMG_MACHO       RT_BIT_32(9)
/** @} */

/** Pointer to a module. */
typedef struct DBGFMOD *PDBGFMOD;


/**
 * Virtual method table for executable image interpreters.
 */
typedef struct DBGFMODVTIMG
{
    /** Magic number (DBGFMODVTIMG_MAGIC). */
    uint32_t    u32Magic;
    /** Mask of supported debug info types, see grp_dbgf_exeimg.
     * Used to speed up the search for a suitable interpreter. */
    uint32_t    fSupports;
    /** The name of the interpreter. */
    const char *pszName;

    /**
     * Try open the image.
     *
     * This combines probing and opening.
     *
     * @returns VBox status code. No informational returns defined.
     *
     * @param   pMod        Pointer to the module that is being opened.
     *
     *                      The DBGFMOD::pszDbgFile member will point to
     *                      the filename of any debug info we're aware of
     *                      on input. Also, or alternatively, it is expected
     *                      that the interpreter will look for debug info in
     *                      the executable image file when present and that it
     *                      may ask the image interpreter for this when it's
     *                      around.
     *
     *                      Upon successful return the method is expected to
     *                      initialize pDbgOps and pvDbgPriv.
     */
    DECLCALLBACKMEMBER(int, pfnTryOpen,(PDBGFMOD pMod));

    /**
     * Close the interpreter, freeing all associated resources.
     *
     * The caller sets the pDbgOps and pvDbgPriv DBGFMOD members
     * to NULL upon return.
     *
     * @param   pMod        Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(int, pfnClose,(PDBGFMOD pMod));

} DBGFMODVTIMG

/**
 * Virtual method table for debug info interpreters.
 */
typedef struct DBGFMODVTDBG
{
    /** Magic number (DBGFMODVTDBG_MAGIC). */
    uint32_t    u32Magic;
    /** Mask of supported debug info types, see grp_dbgf_dbginfo.
     * Used to speed up the search for a suitable interpreter. */
    uint32_t    fSupports;
    /** The name of the interpreter. */
    const char *pszName;

    /**
     * Try open the image.
     *
     * This combines probing and opening.
     *
     * @returns VBox status code. No informational returns defined.
     *
     * @param   pMod        Pointer to the module that is being opened.
     *
     *                      The DBGFMOD::pszDbgFile member will point to
     *                      the filename of any debug info we're aware of
     *                      on input. Also, or alternatively, it is expected
     *                      that the interpreter will look for debug info in
     *                      the executable image file when present and that it
     *                      may ask the image interpreter for this when it's
     *                      around.
     *
     *                      Upon successful return the method is expected to
     *                      initialize pDbgOps and pvDbgPriv.
     */
    DECLCALLBACKMEMBER(int, pfnTryOpen,(PDBGFMOD pMod));

    /**
     * Close the interpreter, freeing all associated resources.
     *
     * The caller sets the pDbgOps and pvDbgPriv DBGFMOD members
     * to NULL upon return.
     *
     * @param   pMod        Pointer to the module structure.
     */
    DECLCALLBACKMEMBER(int, pfnClose,(PDBGFMOD pMod));

    /**
     * Queries symbol information by symbol name.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS on success, no informational status code.
     * @retval  VERR_DBGF_NO_SYMBOLS if there aren't any symbols.
     * @retval  VERR_SYMBOL_NOT_FOUND if no suitable symbol was found.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   pszSymbol   The symbol name.
     * @para    pSymbol     Where to store the symbol information.
     */
    DECLCALLBACKMEMBER(int, pfnSymbolByName,(PDBGFMOD pMod, const char *pszSymbol, PDBGFSYMBOL pSymbol));

    /**
     * Queries symbol information by address.
     *
     * The returned symbol is what the debug info interpreter considers the symbol
     * most applicable to the specified address. This usually means a symbol with an
     * address equal or lower than the requested.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS on success, no informational status code.
     * @retval  VERR_DBGF_NO_SYMBOLS if there aren't any symbols.
     * @retval  VERR_SYMBOL_NOT_FOUND if no suitable symbol was found.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   iSeg        The segment number (0-based). DBGF_SEG_RVA can be used.
     * @param   off         The offset into the segment.
     * @param   poffDisp    Where to store the distance between the specified address
     *                      and the returned symbol. Optional.
     * @param   pSymbol     Where to store the symbol information.
     */
    DECLCALLBACKMEMBER(int, pfnSymbolByAddr,(PDBGFMOD pMod, uint32_t iSeg, RTGCUINTPTR off, PRTGCINTPTR poffDisp, PDBGFSYMBOL pSymbol));

    /**
     * Queries line number information by address.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS on success, no informational status code.
     * @retval  VERR_DBGF_NO_LINE_NUMBERS if there aren't any line numbers.
     * @retval  VERR_DBGF_LINE_NOT_FOUND if no suitable line number was found.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   iSeg        The segment number (0-based). DBGF_SEG_RVA can be used.
     * @param   off         The offset into the segment.
     * @param   poffDisp    Where to store the distance between the specified address
     *                      and the returned line number. Optional.
     * @param   pLine       Where to store the information about the closest line number.
     */
    DECLCALLBACKMEMBER(int, pfnLineByAddr,(PDBGFMOD pMod, uint32_t iSeg, RTGCUINTPTR off, PRTGCINTPTR poffDisp, PDBGFLINE pLine));

    /**
     * Adds a symbol to the module (optional).
     *
     * This method is used to implement DBGFR3SymbolAdd.
     *
     * @returns VBox status code.
     * @retval  VERR_NOT_SUPPORTED if the interpreter doesn't support this feature.
     *
     * @param   pMod        Pointer to the module structure.
     * @param   pszSymbol   The symbol name.
     * @param   iSeg        The segment number (0-based). DBGF_SEG_RVA can be used.
     * @param   off         The offset into the segment.
     * @param   cbSymbol    The area covered by the symbol. 0 is fine.
     */
    DECLCALLBACKMEMBER(int, pfnSymbolAdd,(PDBGFMOD pMod, const char *pszSymbol, uint32_t iSeg, RTGCUINTPTR off, RTUINT cbSymbol));

    /** For catching initialization errors (DBGFMODVTDBG_MAGIC). */
    uint32_t    u32EndMagic;
} DBGFMODVTDBG;

#define DBGFMODVTDBG_MAGIC 123

/**
 * Module.
 */
typedef struct DBGFMOD
{
    /** Magic value (DBGFMOD_MAGIC). */
    uint32_t        u32Magic;
    /** The number of address spaces this module is currently linked into.
     * This is used to perform automatic cleanup and sharing. */
    uint32_t        cLinks;
    /** The module name (short). */
    const char     *pszName;
    /** The module filename. Can be NULL. */
    const char     *pszImgFile;
    /** The debug info file (if external). Can be NULL. */
    const char     *pszDbgFile;

    /** The method table for the executable image interpreter. */
    PCDBGFMODVTIMG  pImgVt;
    /** Pointer to the private data of the executable image interpreter. */
    void           *pvImgPriv;

    /** The method table for the debug info interpreter. */
    PCDBGFMODVTDBG  pDbgVt;
    /** Pointer to the private data of the debug info interpreter. */
    void           *pvDbgPriv;

} DBGFMOD;

#define DBGFMOD_MAGIC   0x12345678

#endif

