/* $Id: dbgmoddwarf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Info Reader For DWARF.
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
#define LOG_GROUP   RTLOGGROUP_DBG_DWARF
#include <iprt/dbg.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#define RTDBGMODDWARF_WITH_MEM_CACHE
#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
# include <iprt/memcache.h>
#endif
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/strcache.h>
#include <iprt/x86.h>
#include <iprt/formats/dwarf.h>
#include "internal/dbgmod.h"



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a DWARF section reader. */
typedef struct RTDWARFCURSOR *PRTDWARFCURSOR;
/** Pointer to an attribute descriptor. */
typedef struct RTDWARFATTRDESC const *PCRTDWARFATTRDESC;
/** Pointer to a DIE. */
typedef struct RTDWARFDIE *PRTDWARFDIE;
/** Pointer to a const DIE. */
typedef struct RTDWARFDIE const *PCRTDWARFDIE;

/**
 * DWARF sections.
 */
typedef enum krtDbgModDwarfSect
{
    krtDbgModDwarfSect_abbrev = 0,
    krtDbgModDwarfSect_aranges,
    krtDbgModDwarfSect_frame,
    krtDbgModDwarfSect_info,
    krtDbgModDwarfSect_inlined,
    krtDbgModDwarfSect_line,
    krtDbgModDwarfSect_loc,
    krtDbgModDwarfSect_macinfo,
    krtDbgModDwarfSect_pubnames,
    krtDbgModDwarfSect_pubtypes,
    krtDbgModDwarfSect_ranges,
    krtDbgModDwarfSect_str,
    krtDbgModDwarfSect_types,
    /** End of valid parts (exclusive). */
    krtDbgModDwarfSect_End
} krtDbgModDwarfSect;

/**
 * Abbreviation cache entry.
 */
typedef struct RTDWARFABBREV
{
    /** Whether there are children or not. */
    bool                fChildren;
#ifdef LOG_ENABLED
    uint8_t             cbHdr; /**< For calcing ABGOFF matching dwarfdump. */
#endif
    /** The tag. */
    uint16_t            uTag;
    /** Offset into the abbrev section of the specification pairs. */
    uint32_t            offSpec;
    /** The abbreviation table offset this is entry is valid for.
     * UINT32_MAX if not valid. */
    uint32_t            offAbbrev;
} RTDWARFABBREV;
/** Pointer to an abbreviation cache entry. */
typedef RTDWARFABBREV *PRTDWARFABBREV;
/** Pointer to a const abbreviation cache entry. */
typedef RTDWARFABBREV const *PCRTDWARFABBREV;

/**
 * Structure for gathering segment info.
 */
typedef struct RTDBGDWARFSEG
{
    /** The highest offset in the segment. */
    uint64_t            offHighest;
    /** Calculated base address. */
    uint64_t            uBaseAddr;
    /** Estimated The segment size. */
    uint64_t            cbSegment;
    /** Segment number (RTLDRSEG::Sel16bit). */
    RTSEL               uSegment;
} RTDBGDWARFSEG;
/** Pointer to segment info. */
typedef RTDBGDWARFSEG *PRTDBGDWARFSEG;


/**
 * The instance data of the DWARF reader.
 */
typedef struct RTDBGMODDWARF
{
    /** The debug container containing doing the real work. */
    RTDBGMOD                hCnt;
    /** The image module (no reference). */
    PRTDBGMODINT            pImgMod;
    /** The debug info module (no reference). */
    PRTDBGMODINT            pDbgInfoMod;
    /** Nested image module (with reference ofc). */
    PRTDBGMODINT            pNestedMod;

    /** DWARF debug info sections. */
    struct
    {
        /** The file offset of the part. */
        RTFOFF              offFile;
        /** The size of the part. */
        size_t              cb;
        /** The memory mapping of the part. */
        void const         *pv;
        /** Set if present. */
        bool                fPresent;
        /** The debug info ordinal number in the image file. */
        uint32_t            iDbgInfo;
    } aSections[krtDbgModDwarfSect_End];

    /** The offset into the abbreviation section of the current cache. */
    uint32_t                offCachedAbbrev;
    /** The number of cached abbreviations we've allocated space for. */
    uint32_t                cCachedAbbrevsAlloced;
    /** Array of cached abbreviations, indexed by code. */
    PRTDWARFABBREV          paCachedAbbrevs;
    /** Used by rtDwarfAbbrev_Lookup when the result is uncachable. */
    RTDWARFABBREV           LookupAbbrev;

    /** The list of compilation units (RTDWARFDIE). */
    RTLISTANCHOR            CompileUnitList;

    /** Set if we have to use link addresses because the module does not have
     *  fixups (mach_kernel). */
    bool                    fUseLinkAddress;
    /** This is set to -1 if we're doing everything in one pass.
     * Otherwise it's 1 or 2:
     *      - In pass 1, we collect segment info.
     *      - In pass 2, we add debug info to the container.
     * The two pass parsing is necessary for watcom generated symbol files as
     * these contains no information about the code and data segments in the
     * image.  So we have to figure out some approximate stuff based on the
     * segments and offsets we encounter in the debug info. */
    int8_t                  iWatcomPass;
    /** Segment index hint. */
    uint16_t                iSegHint;
    /** The number of segments in paSegs.
     * (During segment copying, this is abused to count useful segments.) */
    uint32_t                cSegs;
    /** Pointer to segments if iWatcomPass isn't -1. */
    PRTDBGDWARFSEG          paSegs;
#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
    /** DIE allocators. */
    struct
    {
        RTMEMCACHE          hMemCache;
        uint32_t            cbMax;
    } aDieAllocators[2];
#endif
} RTDBGMODDWARF;
/** Pointer to instance data of the DWARF reader. */
typedef RTDBGMODDWARF *PRTDBGMODDWARF;

/**
 * DWARF cursor for reading byte data.
 */
typedef struct RTDWARFCURSOR
{
    /** The current position. */
    uint8_t const          *pb;
    /** The number of bytes left to read. */
    size_t                  cbLeft;
    /** The number of bytes left to read in the current unit. */
    size_t                  cbUnitLeft;
    /** The DWARF debug info reader instance.  (Can be NULL for eh_frame.) */
    PRTDBGMODDWARF          pDwarfMod;
    /** Set if this is 64-bit DWARF, clear if 32-bit. */
    bool                    f64bitDwarf;
    /** Set if the format endian is native, clear if endian needs to be
     * inverted. */
    bool                    fNativEndian;
    /** The size of a native address. */
    uint8_t                 cbNativeAddr;
    /** The cursor status code.  This is VINF_SUCCESS until some error
     *  occurs. */
    int                     rc;
    /** The start of the area covered by the cursor.
     * Used for repositioning the cursor relative to the start of a section. */
    uint8_t const          *pbStart;
    /** The section. */
    krtDbgModDwarfSect      enmSect;
} RTDWARFCURSOR;


/**
 * DWARF line number program state.
 */
typedef struct RTDWARFLINESTATE
{
    /** @name Virtual Line Number Machine Registers.
     * @{ */
    struct
    {
        uint64_t        uAddress;
        uint64_t        idxOp;
        uint32_t        iFile;
        uint32_t        uLine;
        uint32_t        uColumn;
        bool            fIsStatement;
        bool            fBasicBlock;
        bool            fEndSequence;
        bool            fPrologueEnd;
        bool            fEpilogueBegin;
        uint32_t        uIsa;
        uint32_t        uDiscriminator;
        RTSEL           uSegment;
    } Regs;
    /** @} */

    /** Header. */
    struct
    {
        uint32_t        uVer;
        uint64_t        offFirstOpcode;
        uint8_t         cbMinInstr;
        uint8_t         cMaxOpsPerInstr;
        uint8_t         u8DefIsStmt;
        int8_t          s8LineBase;
        uint8_t         u8LineRange;
        uint8_t         u8OpcodeBase;
        uint8_t const  *pacStdOperands;
    } Hdr;

    /** @name Include Path Table (0-based)
     * @{ */
    const char    **papszIncPaths;
    uint32_t        cIncPaths;
    /** @} */

    /** @name File Name Table (0-based, dummy zero entry)
     * @{ */
    char          **papszFileNames;
    uint32_t        cFileNames;
    /** @} */

    /** The DWARF debug info reader instance. */
    PRTDBGMODDWARF  pDwarfMod;
} RTDWARFLINESTATE;
/** Pointer to a DWARF line number program state. */
typedef RTDWARFLINESTATE *PRTDWARFLINESTATE;


/**
 * Decodes an attribute and stores it in the specified DIE member field.
 *
 * @returns IPRT status code.
 * @param   pDie            Pointer to the DIE structure.
 * @param   pbMember        Pointer to the first byte in the member.
 * @param   pDesc           The attribute descriptor.
 * @param   uForm           The data form.
 * @param   pCursor         The cursor to read data from.
 */
typedef DECLCALLBACKTYPE(int, FNRTDWARFATTRDECODER,(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                                    uint32_t uForm, PRTDWARFCURSOR pCursor));
/** Pointer to an attribute decoder callback. */
typedef FNRTDWARFATTRDECODER *PFNRTDWARFATTRDECODER;

/**
 * Attribute descriptor.
 */
typedef struct RTDWARFATTRDESC
{
    /** The attribute. */
    uint16_t                uAttr;
    /** The data member offset. */
    uint16_t                off;
    /** The data member size and initialization method. */
    uint8_t                 cbInit;
    uint8_t                 bPadding[3]; /**< Alignment padding. */
    /** The decoder function. */
    PFNRTDWARFATTRDECODER   pfnDecoder;
} RTDWARFATTRDESC;

/** Define a attribute entry. */
#define ATTR_ENTRY(a_uAttr, a_Struct, a_Member, a_Init, a_pfnDecoder) \
    { \
        a_uAttr, \
        (uint16_t)RT_OFFSETOF(a_Struct, a_Member), \
        a_Init | ((uint8_t)RT_SIZEOFMEMB(a_Struct, a_Member) & ATTR_SIZE_MASK), \
        { 0, 0, 0 }, \
        a_pfnDecoder\
    }

/** @name Attribute size and init methods.
 * @{ */
#define ATTR_INIT_ZERO      UINT8_C(0x00)
#define ATTR_INIT_FFFS      UINT8_C(0x80)
#define ATTR_INIT_MASK      UINT8_C(0x80)
#define ATTR_SIZE_MASK      UINT8_C(0x3f)
#define ATTR_GET_SIZE(a_pAttrDesc)   ((a_pAttrDesc)->cbInit & ATTR_SIZE_MASK)
/** @} */


/**
 * DIE descriptor.
 */
typedef struct RTDWARFDIEDESC
{
    /** The size of the DIE. */
    size_t              cbDie;
    /** The number of attributes. */
    size_t              cAttributes;
    /** Pointer to the array of attributes. */
    PCRTDWARFATTRDESC   paAttributes;
} RTDWARFDIEDESC;
typedef struct RTDWARFDIEDESC const *PCRTDWARFDIEDESC;
/** DIE descriptor initializer. */
#define DIE_DESC_INIT(a_Type, a_aAttrs)  { sizeof(a_Type), RT_ELEMENTS(a_aAttrs), &a_aAttrs[0] }


/**
 * DIE core structure, all inherits (starts with) this.
 */
typedef struct RTDWARFDIE
{
    /** Pointer to the parent node. NULL if root unit. */
    struct RTDWARFDIE  *pParent;
    /** Our node in the sibling list. */
    RTLISTNODE          SiblingNode;
    /** List of children. */
    RTLISTNODE          ChildList;
    /** The number of attributes successfully decoded. */
    uint8_t             cDecodedAttrs;
    /** The number of unknown or otherwise unhandled attributes. */
    uint8_t             cUnhandledAttrs;
#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
    /** The allocator index. */
    uint8_t             iAllocator;
#endif
    /** The die tag, indicating which union structure to use. */
    uint16_t            uTag;
    /** Offset of the abbreviation specification (within debug_abbrev). */
    uint32_t            offSpec;
} RTDWARFDIE;


/**
 * DWARF address structure.
 */
typedef struct RTDWARFADDR
{
    /** The address. */
    uint64_t            uAddress;
} RTDWARFADDR;
typedef RTDWARFADDR *PRTDWARFADDR;
typedef RTDWARFADDR const *PCRTDWARFADDR;


/**
 * DWARF address range.
 */
typedef struct RTDWARFADDRRANGE
{
    uint64_t            uLowAddress;
    uint64_t            uHighAddress;
    uint8_t const      *pbRanges; /* ?? */
    uint8_t             cAttrs           : 2;
    uint8_t             fHaveLowAddress  : 1;
    uint8_t             fHaveHighAddress : 1;
    uint8_t             fHaveHighIsAddress : 1;
    uint8_t             fHaveRanges      : 1;
} RTDWARFADDRRANGE;
typedef RTDWARFADDRRANGE *PRTDWARFADDRRANGE;
typedef RTDWARFADDRRANGE const *PCRTDWARFADDRRANGE;

/** What a RTDWARFREF is relative to. */
typedef enum krtDwarfRef
{
    krtDwarfRef_NotSet,
    krtDwarfRef_LineSection,
    krtDwarfRef_LocSection,
    krtDwarfRef_RangesSection,
    krtDwarfRef_InfoSection,
    krtDwarfRef_SameUnit,
    krtDwarfRef_TypeId64
} krtDwarfRef;

/**
 * DWARF reference.
 */
typedef struct RTDWARFREF
{
    /** The offset. */
    uint64_t        off;
    /** What the offset is relative to. */
    krtDwarfRef     enmWrt;
} RTDWARFREF;
typedef RTDWARFREF *PRTDWARFREF;
typedef RTDWARFREF const *PCRTDWARFREF;


/**
 * DWARF Location state.
 */
typedef struct RTDWARFLOCST
{
    /** The input cursor. */
    RTDWARFCURSOR   Cursor;
    /** Points to the current top of the stack. Initial value -1. */
    int32_t         iTop;
    /** The value stack. */
    uint64_t        auStack[64];
} RTDWARFLOCST;
/** Pointer to location state. */
typedef RTDWARFLOCST *PRTDWARFLOCST;



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNRTDWARFATTRDECODER rtDwarfDecode_Address;
static FNRTDWARFATTRDECODER rtDwarfDecode_Bool;
static FNRTDWARFATTRDECODER rtDwarfDecode_LowHighPc;
static FNRTDWARFATTRDECODER rtDwarfDecode_Ranges;
static FNRTDWARFATTRDECODER rtDwarfDecode_Reference;
static FNRTDWARFATTRDECODER rtDwarfDecode_SectOff;
static FNRTDWARFATTRDECODER rtDwarfDecode_String;
static FNRTDWARFATTRDECODER rtDwarfDecode_UnsignedInt;
static FNRTDWARFATTRDECODER rtDwarfDecode_SegmentLoc;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** RTDWARFDIE description. */
static const RTDWARFDIEDESC g_CoreDieDesc = { sizeof(RTDWARFDIE), 0, NULL };


/**
 * DW_TAG_compile_unit & DW_TAG_partial_unit.
 */
typedef struct RTDWARFDIECOMPILEUNIT
{
    /** The DIE core structure. */
    RTDWARFDIE          Core;
    /** The unit name. */
    const char         *pszName;
    /** The address range of the code belonging to this unit. */
    RTDWARFADDRRANGE    PcRange;
    /** The language name. */
    uint16_t            uLanguage;
    /** The identifier case. */
    uint8_t             uIdentifierCase;
    /** String are UTF-8 encoded.  If not set, the encoding is
     * unknown. */
    bool                fUseUtf8;
    /** The unit contains main() or equivalent. */
    bool                fMainFunction;
    /** The line numbers for this unit. */
    RTDWARFREF          StmtListRef;
    /** The macro information for this unit. */
    RTDWARFREF          MacroInfoRef;
    /** Reference to the base types. */
    RTDWARFREF          BaseTypesRef;
    /** Working directory for the unit. */
    const char         *pszCurDir;
    /** The name of the compiler or whatever that produced this unit. */
    const char         *pszProducer;

    /** @name From the unit header.
     * @{ */
    /** The offset into debug_info of this unit (for references). */
    uint64_t            offUnit;
    /** The length of this unit. */
    uint64_t            cbUnit;
    /** The offset into debug_abbrev of the abbreviation for this unit. */
    uint64_t            offAbbrev;
    /** The native address size. */
    uint8_t             cbNativeAddr;
    /** The DWARF version. */
    uint8_t             uDwarfVer;
    /** @} */
} RTDWARFDIECOMPILEUNIT;
typedef RTDWARFDIECOMPILEUNIT *PRTDWARFDIECOMPILEUNIT;


/** RTDWARFDIECOMPILEUNIT attributes. */
static const RTDWARFATTRDESC g_aCompileUnitAttrs[] =
{
    ATTR_ENTRY(DW_AT_name,              RTDWARFDIECOMPILEUNIT, pszName,        ATTR_INIT_ZERO, rtDwarfDecode_String),
    ATTR_ENTRY(DW_AT_low_pc,            RTDWARFDIECOMPILEUNIT, PcRange,        ATTR_INIT_ZERO, rtDwarfDecode_LowHighPc),
    ATTR_ENTRY(DW_AT_high_pc,           RTDWARFDIECOMPILEUNIT, PcRange,        ATTR_INIT_ZERO, rtDwarfDecode_LowHighPc),
    ATTR_ENTRY(DW_AT_ranges,            RTDWARFDIECOMPILEUNIT, PcRange,        ATTR_INIT_ZERO, rtDwarfDecode_Ranges),
    ATTR_ENTRY(DW_AT_language,          RTDWARFDIECOMPILEUNIT, uLanguage,      ATTR_INIT_ZERO, rtDwarfDecode_UnsignedInt),
    ATTR_ENTRY(DW_AT_macro_info,        RTDWARFDIECOMPILEUNIT, MacroInfoRef,   ATTR_INIT_ZERO, rtDwarfDecode_SectOff),
    ATTR_ENTRY(DW_AT_stmt_list,         RTDWARFDIECOMPILEUNIT, StmtListRef,    ATTR_INIT_ZERO, rtDwarfDecode_SectOff),
    ATTR_ENTRY(DW_AT_comp_dir,          RTDWARFDIECOMPILEUNIT, pszCurDir,      ATTR_INIT_ZERO, rtDwarfDecode_String),
    ATTR_ENTRY(DW_AT_producer,          RTDWARFDIECOMPILEUNIT, pszProducer,    ATTR_INIT_ZERO, rtDwarfDecode_String),
    ATTR_ENTRY(DW_AT_identifier_case,   RTDWARFDIECOMPILEUNIT, uIdentifierCase,ATTR_INIT_ZERO, rtDwarfDecode_UnsignedInt),
    ATTR_ENTRY(DW_AT_base_types,        RTDWARFDIECOMPILEUNIT, BaseTypesRef,   ATTR_INIT_ZERO, rtDwarfDecode_Reference),
    ATTR_ENTRY(DW_AT_use_UTF8,          RTDWARFDIECOMPILEUNIT, fUseUtf8,       ATTR_INIT_ZERO, rtDwarfDecode_Bool),
    ATTR_ENTRY(DW_AT_main_subprogram,   RTDWARFDIECOMPILEUNIT, fMainFunction,  ATTR_INIT_ZERO, rtDwarfDecode_Bool)
};

/** RTDWARFDIECOMPILEUNIT description. */
static const RTDWARFDIEDESC g_CompileUnitDesc = DIE_DESC_INIT(RTDWARFDIECOMPILEUNIT, g_aCompileUnitAttrs);


/**
 * DW_TAG_subprogram.
 */
typedef struct RTDWARFDIESUBPROGRAM
{
    /** The DIE core structure. */
    RTDWARFDIE          Core;
    /** The name. */
    const char         *pszName;
    /** The linkage name. */
    const char         *pszLinkageName;
    /** The address range of the code belonging to this unit. */
    RTDWARFADDRRANGE    PcRange;
    /** The first instruction in the function. */
    RTDWARFADDR         EntryPc;
    /** Segment number (watcom). */
    RTSEL               uSegment;
    /** Reference to the specification. */
    RTDWARFREF          SpecRef;
} RTDWARFDIESUBPROGRAM;
/** Pointer to a DW_TAG_subprogram DIE. */
typedef RTDWARFDIESUBPROGRAM *PRTDWARFDIESUBPROGRAM;
/** Pointer to a const DW_TAG_subprogram DIE. */
typedef RTDWARFDIESUBPROGRAM const *PCRTDWARFDIESUBPROGRAM;


/** RTDWARFDIESUBPROGRAM attributes. */
static const RTDWARFATTRDESC g_aSubProgramAttrs[] =
{
    ATTR_ENTRY(DW_AT_name,              RTDWARFDIESUBPROGRAM, pszName,        ATTR_INIT_ZERO, rtDwarfDecode_String),
    ATTR_ENTRY(DW_AT_linkage_name,      RTDWARFDIESUBPROGRAM, pszLinkageName, ATTR_INIT_ZERO, rtDwarfDecode_String),
    ATTR_ENTRY(DW_AT_MIPS_linkage_name, RTDWARFDIESUBPROGRAM, pszLinkageName, ATTR_INIT_ZERO, rtDwarfDecode_String),
    ATTR_ENTRY(DW_AT_low_pc,            RTDWARFDIESUBPROGRAM, PcRange,        ATTR_INIT_ZERO, rtDwarfDecode_LowHighPc),
    ATTR_ENTRY(DW_AT_high_pc,           RTDWARFDIESUBPROGRAM, PcRange,        ATTR_INIT_ZERO, rtDwarfDecode_LowHighPc),
    ATTR_ENTRY(DW_AT_ranges,            RTDWARFDIESUBPROGRAM, PcRange,        ATTR_INIT_ZERO, rtDwarfDecode_Ranges),
    ATTR_ENTRY(DW_AT_entry_pc,          RTDWARFDIESUBPROGRAM, EntryPc,        ATTR_INIT_ZERO, rtDwarfDecode_Address),
    ATTR_ENTRY(DW_AT_segment,           RTDWARFDIESUBPROGRAM, uSegment,       ATTR_INIT_ZERO, rtDwarfDecode_SegmentLoc),
    ATTR_ENTRY(DW_AT_specification,     RTDWARFDIESUBPROGRAM, SpecRef,        ATTR_INIT_ZERO, rtDwarfDecode_Reference)
};

/** RTDWARFDIESUBPROGRAM description. */
static const RTDWARFDIEDESC g_SubProgramDesc = DIE_DESC_INIT(RTDWARFDIESUBPROGRAM, g_aSubProgramAttrs);


/** RTDWARFDIESUBPROGRAM attributes for the specification hack. */
static const RTDWARFATTRDESC g_aSubProgramSpecHackAttrs[] =
{
    ATTR_ENTRY(DW_AT_name,              RTDWARFDIESUBPROGRAM, pszName,        ATTR_INIT_ZERO, rtDwarfDecode_String),
    ATTR_ENTRY(DW_AT_linkage_name,      RTDWARFDIESUBPROGRAM, pszLinkageName, ATTR_INIT_ZERO, rtDwarfDecode_String),
    ATTR_ENTRY(DW_AT_MIPS_linkage_name, RTDWARFDIESUBPROGRAM, pszLinkageName, ATTR_INIT_ZERO, rtDwarfDecode_String),
};

/** RTDWARFDIESUBPROGRAM description for the specification hack. */
static const RTDWARFDIEDESC g_SubProgramSpecHackDesc = DIE_DESC_INIT(RTDWARFDIESUBPROGRAM, g_aSubProgramSpecHackAttrs);


/**
 * DW_TAG_label.
 */
typedef struct RTDWARFDIELABEL
{
    /** The DIE core structure. */
    RTDWARFDIE          Core;
    /** The name. */
    const char         *pszName;
    /** The address of the first instruction. */
    RTDWARFADDR         Address;
    /** Segment number (watcom). */
    RTSEL               uSegment;
    /** Externally visible? */
    bool                fExternal;
} RTDWARFDIELABEL;
/** Pointer to a DW_TAG_label DIE. */
typedef RTDWARFDIELABEL *PRTDWARFDIELABEL;
/** Pointer to a const DW_TAG_label DIE. */
typedef RTDWARFDIELABEL const *PCRTDWARFDIELABEL;


/** RTDWARFDIESUBPROGRAM attributes. */
static const RTDWARFATTRDESC g_aLabelAttrs[] =
{
    ATTR_ENTRY(DW_AT_name,              RTDWARFDIELABEL, pszName,               ATTR_INIT_ZERO, rtDwarfDecode_String),
    ATTR_ENTRY(DW_AT_low_pc,            RTDWARFDIELABEL, Address,               ATTR_INIT_ZERO, rtDwarfDecode_Address),
    ATTR_ENTRY(DW_AT_segment,           RTDWARFDIELABEL, uSegment,              ATTR_INIT_ZERO, rtDwarfDecode_SegmentLoc),
    ATTR_ENTRY(DW_AT_external,          RTDWARFDIELABEL, fExternal,             ATTR_INIT_ZERO, rtDwarfDecode_Bool)
};

/** RTDWARFDIESUBPROGRAM description. */
static const RTDWARFDIEDESC g_LabelDesc = DIE_DESC_INIT(RTDWARFDIELABEL, g_aLabelAttrs);


/**
 * Tag names and descriptors.
 */
static const struct RTDWARFTAGDESC
{
    /** The tag value. */
    uint16_t            uTag;
    /** The tag name as string. */
    const char         *pszName;
    /** The DIE descriptor to use. */
    PCRTDWARFDIEDESC    pDesc;
}   g_aTagDescs[] =
{
#define TAGDESC(a_Name, a_pDesc)        { DW_ ## a_Name, #a_Name, a_pDesc }
#define TAGDESC_EMPTY()                 { 0, NULL, &g_CoreDieDesc }
#define TAGDESC_CORE(a_Name)            TAGDESC(a_Name, &g_CoreDieDesc)
    TAGDESC_EMPTY(),                            /* 0x00 */
    TAGDESC_CORE(TAG_array_type),
    TAGDESC_CORE(TAG_class_type),
    TAGDESC_CORE(TAG_entry_point),
    TAGDESC_CORE(TAG_enumeration_type),         /* 0x04 */
    TAGDESC_CORE(TAG_formal_parameter),
    TAGDESC_EMPTY(),
    TAGDESC_EMPTY(),
    TAGDESC_CORE(TAG_imported_declaration),     /* 0x08 */
    TAGDESC_EMPTY(),
    TAGDESC(TAG_label, &g_LabelDesc),
    TAGDESC_CORE(TAG_lexical_block),
    TAGDESC_EMPTY(),                            /* 0x0c */
    TAGDESC_CORE(TAG_member),
    TAGDESC_EMPTY(),
    TAGDESC_CORE(TAG_pointer_type),
    TAGDESC_CORE(TAG_reference_type),           /* 0x10 */
    TAGDESC_CORE(TAG_compile_unit),
    TAGDESC_CORE(TAG_string_type),
    TAGDESC_CORE(TAG_structure_type),
    TAGDESC_EMPTY(),                            /* 0x14 */
    TAGDESC_CORE(TAG_subroutine_type),
    TAGDESC_CORE(TAG_typedef),
    TAGDESC_CORE(TAG_union_type),
    TAGDESC_CORE(TAG_unspecified_parameters),   /* 0x18 */
    TAGDESC_CORE(TAG_variant),
    TAGDESC_CORE(TAG_common_block),
    TAGDESC_CORE(TAG_common_inclusion),
    TAGDESC_CORE(TAG_inheritance),              /* 0x1c */
    TAGDESC_CORE(TAG_inlined_subroutine),
    TAGDESC_CORE(TAG_module),
    TAGDESC_CORE(TAG_ptr_to_member_type),
    TAGDESC_CORE(TAG_set_type),                 /* 0x20 */
    TAGDESC_CORE(TAG_subrange_type),
    TAGDESC_CORE(TAG_with_stmt),
    TAGDESC_CORE(TAG_access_declaration),
    TAGDESC_CORE(TAG_base_type),                /* 0x24 */
    TAGDESC_CORE(TAG_catch_block),
    TAGDESC_CORE(TAG_const_type),
    TAGDESC_CORE(TAG_constant),
    TAGDESC_CORE(TAG_enumerator),               /* 0x28 */
    TAGDESC_CORE(TAG_file_type),
    TAGDESC_CORE(TAG_friend),
    TAGDESC_CORE(TAG_namelist),
    TAGDESC_CORE(TAG_namelist_item),            /* 0x2c */
    TAGDESC_CORE(TAG_packed_type),
    TAGDESC(TAG_subprogram, &g_SubProgramDesc),
    TAGDESC_CORE(TAG_template_type_parameter),
    TAGDESC_CORE(TAG_template_value_parameter), /* 0x30 */
    TAGDESC_CORE(TAG_thrown_type),
    TAGDESC_CORE(TAG_try_block),
    TAGDESC_CORE(TAG_variant_part),
    TAGDESC_CORE(TAG_variable),                 /* 0x34 */
    TAGDESC_CORE(TAG_volatile_type),
    TAGDESC_CORE(TAG_dwarf_procedure),
    TAGDESC_CORE(TAG_restrict_type),
    TAGDESC_CORE(TAG_interface_type),           /* 0x38 */
    TAGDESC_CORE(TAG_namespace),
    TAGDESC_CORE(TAG_imported_module),
    TAGDESC_CORE(TAG_unspecified_type),
    TAGDESC_CORE(TAG_partial_unit),             /* 0x3c */
    TAGDESC_CORE(TAG_imported_unit),
    TAGDESC_EMPTY(),
    TAGDESC_CORE(TAG_condition),
    TAGDESC_CORE(TAG_shared_type),              /* 0x40 */
    TAGDESC_CORE(TAG_type_unit),
    TAGDESC_CORE(TAG_rvalue_reference_type),
    TAGDESC_CORE(TAG_template_alias)
#undef TAGDESC
#undef TAGDESC_EMPTY
#undef TAGDESC_CORE
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtDwarfInfo_ParseDie(PRTDBGMODDWARF pThis, PRTDWARFDIE pDie, PCRTDWARFDIEDESC pDieDesc,
                                PRTDWARFCURSOR pCursor, PCRTDWARFABBREV pAbbrev, bool fInitDie);



#if defined(LOG_ENABLED) || defined(RT_STRICT)

# if 0 /* unused */
/**
 * Turns a tag value into a string for logging purposes.
 *
 * @returns String name.
 * @param   uTag            The tag.
 */
static const char *rtDwarfLog_GetTagName(uint32_t uTag)
{
    if (uTag < RT_ELEMENTS(g_aTagDescs))
    {
        const char *pszTag = g_aTagDescs[uTag].pszName;
        if (pszTag)
            return pszTag;
    }

    static char s_szStatic[32];
    RTStrPrintf(s_szStatic, sizeof(s_szStatic),"DW_TAG_%#x", uTag);
    return s_szStatic;
}
# endif


/**
 * Turns an attributevalue into a string for logging purposes.
 *
 * @returns String name.
 * @param   uAttr               The attribute.
 */
static const char *rtDwarfLog_AttrName(uint32_t uAttr)
{
    switch (uAttr)
    {
        RT_CASE_RET_STR(DW_AT_sibling);
        RT_CASE_RET_STR(DW_AT_location);
        RT_CASE_RET_STR(DW_AT_name);
        RT_CASE_RET_STR(DW_AT_ordering);
        RT_CASE_RET_STR(DW_AT_byte_size);
        RT_CASE_RET_STR(DW_AT_bit_offset);
        RT_CASE_RET_STR(DW_AT_bit_size);
        RT_CASE_RET_STR(DW_AT_stmt_list);
        RT_CASE_RET_STR(DW_AT_low_pc);
        RT_CASE_RET_STR(DW_AT_high_pc);
        RT_CASE_RET_STR(DW_AT_language);
        RT_CASE_RET_STR(DW_AT_discr);
        RT_CASE_RET_STR(DW_AT_discr_value);
        RT_CASE_RET_STR(DW_AT_visibility);
        RT_CASE_RET_STR(DW_AT_import);
        RT_CASE_RET_STR(DW_AT_string_length);
        RT_CASE_RET_STR(DW_AT_common_reference);
        RT_CASE_RET_STR(DW_AT_comp_dir);
        RT_CASE_RET_STR(DW_AT_const_value);
        RT_CASE_RET_STR(DW_AT_containing_type);
        RT_CASE_RET_STR(DW_AT_default_value);
        RT_CASE_RET_STR(DW_AT_inline);
        RT_CASE_RET_STR(DW_AT_is_optional);
        RT_CASE_RET_STR(DW_AT_lower_bound);
        RT_CASE_RET_STR(DW_AT_producer);
        RT_CASE_RET_STR(DW_AT_prototyped);
        RT_CASE_RET_STR(DW_AT_return_addr);
        RT_CASE_RET_STR(DW_AT_start_scope);
        RT_CASE_RET_STR(DW_AT_bit_stride);
        RT_CASE_RET_STR(DW_AT_upper_bound);
        RT_CASE_RET_STR(DW_AT_abstract_origin);
        RT_CASE_RET_STR(DW_AT_accessibility);
        RT_CASE_RET_STR(DW_AT_address_class);
        RT_CASE_RET_STR(DW_AT_artificial);
        RT_CASE_RET_STR(DW_AT_base_types);
        RT_CASE_RET_STR(DW_AT_calling_convention);
        RT_CASE_RET_STR(DW_AT_count);
        RT_CASE_RET_STR(DW_AT_data_member_location);
        RT_CASE_RET_STR(DW_AT_decl_column);
        RT_CASE_RET_STR(DW_AT_decl_file);
        RT_CASE_RET_STR(DW_AT_decl_line);
        RT_CASE_RET_STR(DW_AT_declaration);
        RT_CASE_RET_STR(DW_AT_discr_list);
        RT_CASE_RET_STR(DW_AT_encoding);
        RT_CASE_RET_STR(DW_AT_external);
        RT_CASE_RET_STR(DW_AT_frame_base);
        RT_CASE_RET_STR(DW_AT_friend);
        RT_CASE_RET_STR(DW_AT_identifier_case);
        RT_CASE_RET_STR(DW_AT_macro_info);
        RT_CASE_RET_STR(DW_AT_namelist_item);
        RT_CASE_RET_STR(DW_AT_priority);
        RT_CASE_RET_STR(DW_AT_segment);
        RT_CASE_RET_STR(DW_AT_specification);
        RT_CASE_RET_STR(DW_AT_static_link);
        RT_CASE_RET_STR(DW_AT_type);
        RT_CASE_RET_STR(DW_AT_use_location);
        RT_CASE_RET_STR(DW_AT_variable_parameter);
        RT_CASE_RET_STR(DW_AT_virtuality);
        RT_CASE_RET_STR(DW_AT_vtable_elem_location);
        RT_CASE_RET_STR(DW_AT_allocated);
        RT_CASE_RET_STR(DW_AT_associated);
        RT_CASE_RET_STR(DW_AT_data_location);
        RT_CASE_RET_STR(DW_AT_byte_stride);
        RT_CASE_RET_STR(DW_AT_entry_pc);
        RT_CASE_RET_STR(DW_AT_use_UTF8);
        RT_CASE_RET_STR(DW_AT_extension);
        RT_CASE_RET_STR(DW_AT_ranges);
        RT_CASE_RET_STR(DW_AT_trampoline);
        RT_CASE_RET_STR(DW_AT_call_column);
        RT_CASE_RET_STR(DW_AT_call_file);
        RT_CASE_RET_STR(DW_AT_call_line);
        RT_CASE_RET_STR(DW_AT_description);
        RT_CASE_RET_STR(DW_AT_binary_scale);
        RT_CASE_RET_STR(DW_AT_decimal_scale);
        RT_CASE_RET_STR(DW_AT_small);
        RT_CASE_RET_STR(DW_AT_decimal_sign);
        RT_CASE_RET_STR(DW_AT_digit_count);
        RT_CASE_RET_STR(DW_AT_picture_string);
        RT_CASE_RET_STR(DW_AT_mutable);
        RT_CASE_RET_STR(DW_AT_threads_scaled);
        RT_CASE_RET_STR(DW_AT_explicit);
        RT_CASE_RET_STR(DW_AT_object_pointer);
        RT_CASE_RET_STR(DW_AT_endianity);
        RT_CASE_RET_STR(DW_AT_elemental);
        RT_CASE_RET_STR(DW_AT_pure);
        RT_CASE_RET_STR(DW_AT_recursive);
        RT_CASE_RET_STR(DW_AT_signature);
        RT_CASE_RET_STR(DW_AT_main_subprogram);
        RT_CASE_RET_STR(DW_AT_data_bit_offset);
        RT_CASE_RET_STR(DW_AT_const_expr);
        RT_CASE_RET_STR(DW_AT_enum_class);
        RT_CASE_RET_STR(DW_AT_linkage_name);
        RT_CASE_RET_STR(DW_AT_MIPS_linkage_name);
        RT_CASE_RET_STR(DW_AT_WATCOM_memory_model);
        RT_CASE_RET_STR(DW_AT_WATCOM_references_start);
        RT_CASE_RET_STR(DW_AT_WATCOM_parm_entry);
    }
    static char s_szStatic[32];
    RTStrPrintf(s_szStatic, sizeof(s_szStatic),"DW_AT_%#x", uAttr);
    return s_szStatic;
}


/**
 * Turns a form value into a string for logging purposes.
 *
 * @returns String name.
 * @param   uForm               The form.
 */
static const char *rtDwarfLog_FormName(uint32_t uForm)
{
    switch (uForm)
    {
        RT_CASE_RET_STR(DW_FORM_addr);
        RT_CASE_RET_STR(DW_FORM_block2);
        RT_CASE_RET_STR(DW_FORM_block4);
        RT_CASE_RET_STR(DW_FORM_data2);
        RT_CASE_RET_STR(DW_FORM_data4);
        RT_CASE_RET_STR(DW_FORM_data8);
        RT_CASE_RET_STR(DW_FORM_string);
        RT_CASE_RET_STR(DW_FORM_block);
        RT_CASE_RET_STR(DW_FORM_block1);
        RT_CASE_RET_STR(DW_FORM_data1);
        RT_CASE_RET_STR(DW_FORM_flag);
        RT_CASE_RET_STR(DW_FORM_sdata);
        RT_CASE_RET_STR(DW_FORM_strp);
        RT_CASE_RET_STR(DW_FORM_udata);
        RT_CASE_RET_STR(DW_FORM_ref_addr);
        RT_CASE_RET_STR(DW_FORM_ref1);
        RT_CASE_RET_STR(DW_FORM_ref2);
        RT_CASE_RET_STR(DW_FORM_ref4);
        RT_CASE_RET_STR(DW_FORM_ref8);
        RT_CASE_RET_STR(DW_FORM_ref_udata);
        RT_CASE_RET_STR(DW_FORM_indirect);
        RT_CASE_RET_STR(DW_FORM_sec_offset);
        RT_CASE_RET_STR(DW_FORM_exprloc);
        RT_CASE_RET_STR(DW_FORM_flag_present);
        RT_CASE_RET_STR(DW_FORM_ref_sig8);
    }
    static char s_szStatic[32];
    RTStrPrintf(s_szStatic, sizeof(s_szStatic),"DW_FORM_%#x", uForm);
    return s_szStatic;
}

#endif /* LOG_ENABLED || RT_STRICT */



/** @callback_method_impl{FNRTLDRENUMSEGS} */
static DECLCALLBACK(int) rtDbgModDwarfScanSegmentsCallback(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pvUser;
    Log(("Segment %.*s: LinkAddress=%#llx RVA=%#llx cb=%#llx\n",
         pSeg->cchName, pSeg->pszName, (uint64_t)pSeg->LinkAddress, (uint64_t)pSeg->RVA, pSeg->cb));
    NOREF(hLdrMod);

    /* Count relevant segments. */
    if (pSeg->RVA != NIL_RTLDRADDR)
        pThis->cSegs++;

    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTLDRENUMSEGS} */
static DECLCALLBACK(int) rtDbgModDwarfAddSegmentsCallback(RTLDRMOD hLdrMod, PCRTLDRSEG pSeg, void *pvUser)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pvUser;
    Log(("Segment %.*s: LinkAddress=%#llx RVA=%#llx cb=%#llx cbMapped=%#llx\n",
         pSeg->cchName, pSeg->pszName, (uint64_t)pSeg->LinkAddress, (uint64_t)pSeg->RVA, pSeg->cb, pSeg->cbMapped));
    NOREF(hLdrMod);
    Assert(pSeg->cchName > 0);
    Assert(!pSeg->pszName[pSeg->cchName]);

    /* If the segment doesn't have a mapping, just add a dummy so the indexing
       works out correctly (same as for the image). */
    if (pSeg->RVA == NIL_RTLDRADDR)
        return RTDbgModSegmentAdd(pThis->hCnt, 0, 0, pSeg->pszName, 0 /*fFlags*/, NULL);

    /* The link address is 0 for all segments in a relocatable ELF image. */
    RTLDRADDR cb = pSeg->cb;
    if (   cb < pSeg->cbMapped
        && RTLdrGetFormat(hLdrMod) != RTLDRFMT_LX /* for debugging our drivers; 64KB section align by linker, 4KB by loader. */
       )
        cb = pSeg->cbMapped;
    return RTDbgModSegmentAdd(pThis->hCnt, pSeg->RVA, cb, pSeg->pszName, 0 /*fFlags*/, NULL);
}


/**
 * Calls rtDbgModDwarfAddSegmentsCallback for each segment in the executable
 * image.
 *
 * @returns IPRT status code.
 * @param   pThis               The DWARF instance.
 */
static int rtDbgModDwarfAddSegmentsFromImage(PRTDBGMODDWARF pThis)
{
    AssertReturn(pThis->pImgMod && pThis->pImgMod->pImgVt, VERR_INTERNAL_ERROR_2);
    Assert(!pThis->cSegs);
    int rc = pThis->pImgMod->pImgVt->pfnEnumSegments(pThis->pImgMod, rtDbgModDwarfScanSegmentsCallback, pThis);
    if (RT_SUCCESS(rc))
    {
        if (pThis->cSegs == 0)
            pThis->iWatcomPass = 1;
        else
        {
            pThis->cSegs = 0;
            pThis->iWatcomPass = -1;
            rc = pThis->pImgMod->pImgVt->pfnEnumSegments(pThis->pImgMod, rtDbgModDwarfAddSegmentsCallback, pThis);
        }
    }

    return rc;
}


/**
 * Looks up a segment.
 *
 * @returns Pointer to the segment on success, NULL if not found.
 * @param   pThis               The DWARF instance.
 * @param   uSeg                The segment number / selector.
 */
static PRTDBGDWARFSEG rtDbgModDwarfFindSegment(PRTDBGMODDWARF pThis, RTSEL uSeg)
{
    uint32_t        cSegs  = pThis->cSegs;
    uint32_t        iSeg   = pThis->iSegHint;
    PRTDBGDWARFSEG  paSegs = pThis->paSegs;
    if (   iSeg < cSegs
        && paSegs[iSeg].uSegment == uSeg)
        return &paSegs[iSeg];

    for (iSeg = 0; iSeg < cSegs; iSeg++)
        if (uSeg == paSegs[iSeg].uSegment)
        {
            pThis->iSegHint = iSeg;
            return &paSegs[iSeg];
        }

    AssertFailed();
    return NULL;
}


/**
 * Record a segment:offset during pass 1.
 *
 * @returns IPRT status code.
 * @param   pThis               The DWARF instance.
 * @param   uSeg                The segment number / selector.
 * @param   offSeg              The segment offset.
 */
static int rtDbgModDwarfRecordSegOffset(PRTDBGMODDWARF pThis, RTSEL uSeg, uint64_t offSeg)
{
    /* Look up the segment. */
    uint32_t        cSegs  = pThis->cSegs;
    uint32_t        iSeg   = pThis->iSegHint;
    PRTDBGDWARFSEG  paSegs = pThis->paSegs;
    if (   iSeg >= cSegs
        || paSegs[iSeg].uSegment != uSeg)
    {
        for (iSeg = 0; iSeg < cSegs; iSeg++)
            if (uSeg <= paSegs[iSeg].uSegment)
                break;
        if (   iSeg >= cSegs
            || paSegs[iSeg].uSegment != uSeg)
        {
            /* Add */
            void *pvNew = RTMemRealloc(paSegs, (pThis->cSegs + 1) * sizeof(paSegs[0]));
            if (!pvNew)
                return VERR_NO_MEMORY;
            pThis->paSegs = paSegs = (PRTDBGDWARFSEG)pvNew;
            if (iSeg != cSegs)
                memmove(&paSegs[iSeg + 1], &paSegs[iSeg], (cSegs - iSeg) * sizeof(paSegs[0]));
            paSegs[iSeg].offHighest = offSeg;
            paSegs[iSeg].uBaseAddr  = 0;
            paSegs[iSeg].cbSegment  = 0;
            paSegs[iSeg].uSegment   = uSeg;
            pThis->cSegs++;
        }

        pThis->iSegHint = iSeg;
    }

    /* Increase it's range? */
    if (paSegs[iSeg].offHighest < offSeg)
    {
        Log3(("rtDbgModDwarfRecordSegOffset: iSeg=%d uSeg=%#06x offSeg=%#llx\n", iSeg, uSeg, offSeg));
        paSegs[iSeg].offHighest = offSeg;
    }

    return VINF_SUCCESS;
}


/**
 * Calls pfnSegmentAdd for each segment in the executable image.
 *
 * @returns IPRT status code.
 * @param   pThis               The DWARF instance.
 */
static int rtDbgModDwarfAddSegmentsFromPass1(PRTDBGMODDWARF pThis)
{
    AssertReturn(pThis->cSegs, VERR_DWARF_BAD_INFO);
    uint32_t const  cSegs   = pThis->cSegs;
    PRTDBGDWARFSEG  paSegs  = pThis->paSegs;

    /*
     * Are the segments assigned more or less in numerical order?
     */
    if (   paSegs[0].uSegment < 16U
        && paSegs[cSegs - 1].uSegment - paSegs[0].uSegment + 1U <= cSegs + 16U)
    {
        /** @todo heuristics, plase. */
        AssertFailedReturn(VERR_DWARF_TODO);

    }
    /*
     * Assume DOS segmentation.
     */
    else
    {
        for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
            paSegs[iSeg].uBaseAddr = (uint32_t)paSegs[iSeg].uSegment << 16;
        for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
            paSegs[iSeg].cbSegment = paSegs[iSeg].offHighest;
    }

    /*
     * Add them.
     */
    for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
    {
        Log3(("rtDbgModDwarfAddSegmentsFromPass1: Seg#%u: %#010llx LB %#llx uSegment=%#x\n",
              iSeg, paSegs[iSeg].uBaseAddr, paSegs[iSeg].cbSegment, paSegs[iSeg].uSegment));
        char szName[32];
        RTStrPrintf(szName, sizeof(szName), "seg-%#04xh", paSegs[iSeg].uSegment);
        int rc = RTDbgModSegmentAdd(pThis->hCnt, paSegs[iSeg].uBaseAddr, paSegs[iSeg].cbSegment,
                                    szName, 0 /*fFlags*/, NULL);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Loads a DWARF section from the image file.
 *
 * @returns IPRT status code.
 * @param   pThis               The DWARF instance.
 * @param   enmSect             The section to load.
 */
static int rtDbgModDwarfLoadSection(PRTDBGMODDWARF pThis, krtDbgModDwarfSect enmSect)
{
    /*
     * Don't load stuff twice.
     */
    if (pThis->aSections[enmSect].pv)
        return VINF_SUCCESS;

    /*
     * Sections that are not present cannot be loaded, treat them like they
     * are empty
     */
    if (!pThis->aSections[enmSect].fPresent)
    {
        Assert(pThis->aSections[enmSect].cb);
        return VINF_SUCCESS;
    }
    if (!pThis->aSections[enmSect].cb)
        return VINF_SUCCESS;

    /*
     * Sections must be readable with the current image interface.
     */
    if (pThis->aSections[enmSect].offFile < 0)
        return VERR_OUT_OF_RANGE;

    /*
     * Do the job.
     */
    return pThis->pDbgInfoMod->pImgVt->pfnMapPart(pThis->pDbgInfoMod,
                                                  pThis->aSections[enmSect].iDbgInfo,
                                                  pThis->aSections[enmSect].offFile,
                                                  pThis->aSections[enmSect].cb,
                                                  &pThis->aSections[enmSect].pv);
}


#ifdef SOME_UNUSED_FUNCTION
/**
 * Unloads a DWARF section previously mapped by rtDbgModDwarfLoadSection.
 *
 * @returns IPRT status code.
 * @param   pThis               The DWARF instance.
 * @param   enmSect             The section to unload.
 */
static int rtDbgModDwarfUnloadSection(PRTDBGMODDWARF pThis, krtDbgModDwarfSect enmSect)
{
    if (!pThis->aSections[enmSect].pv)
        return VINF_SUCCESS;

    int rc = pThis->pDbgInfoMod->pImgVt->pfnUnmapPart(pThis->pDbgInfoMod, pThis->aSections[enmSect].cb, &pThis->aSections[enmSect].pv);
    AssertRC(rc);
    return rc;
}
#endif


/**
 * Converts to UTF-8 or otherwise makes sure it's valid UTF-8.
 *
 * @returns IPRT status code.
 * @param   pThis               The DWARF instance.
 * @param   ppsz                Pointer to the string pointer.  May be
 *                              reallocated (RTStr*).
 */
static int rtDbgModDwarfStringToUtf8(PRTDBGMODDWARF pThis, char **ppsz)
{
    /** @todo DWARF & UTF-8. */
    NOREF(pThis);
    RTStrPurgeEncoding(*ppsz);
    return VINF_SUCCESS;
}


/**
 * Convers a link address into a segment+offset or RVA.
 *
 * @returns IPRT status code.
 * @param   pThis           The DWARF instance.
 * @param   uSegment        The segment, 0 if not applicable.
 * @param   LinkAddress     The address to convert..
 * @param   piSeg           The segment index.
 * @param   poffSeg         Where to return the segment offset.
 */
static int rtDbgModDwarfLinkAddressToSegOffset(PRTDBGMODDWARF pThis, RTSEL uSegment, uint64_t LinkAddress,
                                               PRTDBGSEGIDX piSeg, PRTLDRADDR poffSeg)
{
    if (pThis->paSegs)
    {
        PRTDBGDWARFSEG pSeg = rtDbgModDwarfFindSegment(pThis, uSegment);
        if (pSeg)
        {
            *piSeg   = pSeg - pThis->paSegs;
            *poffSeg = LinkAddress;
            return VINF_SUCCESS;
        }
    }

    if (pThis->fUseLinkAddress)
        return pThis->pImgMod->pImgVt->pfnLinkAddressToSegOffset(pThis->pImgMod, LinkAddress, piSeg, poffSeg);

    /* If we have a non-zero segment number, assume it's correct for now.
       This helps loading watcom linked LX drivers. */
    if (uSegment > 0)
    {
        *piSeg   = uSegment - 1;
        *poffSeg = LinkAddress;
        return VINF_SUCCESS;
    }

    return pThis->pImgMod->pImgVt->pfnRvaToSegOffset(pThis->pImgMod, LinkAddress, piSeg, poffSeg);
}


/**
 * Converts a segment+offset address into an RVA.
 *
 * @returns IPRT status code.
 * @param   pThis           The DWARF instance.
 * @param   idxSegment      The segment index.
 * @param   offSegment      The segment offset.
 * @param   puRva           Where to return the calculated RVA.
 */
static int rtDbgModDwarfSegOffsetToRva(PRTDBGMODDWARF pThis, RTDBGSEGIDX idxSegment, uint64_t offSegment, PRTUINTPTR puRva)
{
    if (pThis->paSegs)
    {
        PRTDBGDWARFSEG pSeg = rtDbgModDwarfFindSegment(pThis, idxSegment);
        if (pSeg)
        {
            *puRva = pSeg->uBaseAddr + offSegment;
            return VINF_SUCCESS;
        }
    }

    RTUINTPTR uRva = RTDbgModSegmentRva(pThis->pImgMod, idxSegment);
    if (uRva != RTUINTPTR_MAX)
    {
        *puRva = uRva + offSegment;
        return VINF_SUCCESS;
    }
    return VERR_INVALID_POINTER;
}

/**
 * Converts a segment+offset address into an RVA.
 *
 * @returns IPRT status code.
 * @param   pThis           The DWARF instance.
 * @param   uRva            The RVA to convert.
 * @param   pidxSegment     Where to return the segment index.
 * @param   poffSegment     Where to return the segment offset.
 */
static int rtDbgModDwarfRvaToSegOffset(PRTDBGMODDWARF pThis, RTUINTPTR uRva, RTDBGSEGIDX *pidxSegment, uint64_t *poffSegment)
{
    RTUINTPTR   offSeg = 0;
    RTDBGSEGIDX idxSeg = RTDbgModRvaToSegOff(pThis->pImgMod, uRva, &offSeg);
    if (idxSeg != NIL_RTDBGSEGIDX)
    {
        *pidxSegment = idxSeg;
        *poffSegment = offSeg;
        return VINF_SUCCESS;
    }
    return VERR_INVALID_POINTER;
}



/*
 *
 * DWARF Cursor.
 * DWARF Cursor.
 * DWARF Cursor.
 *
 */


/**
 * Reads a 8-bit unsigned integer and advances the cursor.
 *
 * @returns 8-bit unsigned integer. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           What to return on read error.
 */
static uint8_t rtDwarfCursor_GetU8(PRTDWARFCURSOR pCursor, uint8_t uErrValue)
{
    if (pCursor->cbUnitLeft < 1)
    {
        pCursor->rc = VERR_DWARF_UNEXPECTED_END;
        return uErrValue;
    }

    uint8_t u8 = pCursor->pb[0];
    pCursor->pb         += 1;
    pCursor->cbUnitLeft -= 1;
    pCursor->cbLeft     -= 1;
    return u8;
}


/**
 * Reads a 16-bit unsigned integer and advances the cursor.
 *
 * @returns 16-bit unsigned integer. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           What to return on read error.
 */
static uint16_t rtDwarfCursor_GetU16(PRTDWARFCURSOR pCursor, uint16_t uErrValue)
{
    if (pCursor->cbUnitLeft < 2)
    {
        pCursor->pb         += pCursor->cbUnitLeft;
        pCursor->cbLeft     -= pCursor->cbUnitLeft;
        pCursor->cbUnitLeft  = 0;
        pCursor->rc          = VERR_DWARF_UNEXPECTED_END;
        return uErrValue;
    }

    uint16_t u16 = RT_MAKE_U16(pCursor->pb[0], pCursor->pb[1]);
    pCursor->pb         += 2;
    pCursor->cbUnitLeft -= 2;
    pCursor->cbLeft     -= 2;
    if (!pCursor->fNativEndian)
        u16 = RT_BSWAP_U16(u16);
    return u16;
}


/**
 * Reads a 32-bit unsigned integer and advances the cursor.
 *
 * @returns 32-bit unsigned integer. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           What to return on read error.
 */
static uint32_t rtDwarfCursor_GetU32(PRTDWARFCURSOR pCursor, uint32_t uErrValue)
{
    if (pCursor->cbUnitLeft < 4)
    {
        pCursor->pb         += pCursor->cbUnitLeft;
        pCursor->cbLeft     -= pCursor->cbUnitLeft;
        pCursor->cbUnitLeft  = 0;
        pCursor->rc          = VERR_DWARF_UNEXPECTED_END;
        return uErrValue;
    }

    uint32_t u32 = RT_MAKE_U32_FROM_U8(pCursor->pb[0], pCursor->pb[1], pCursor->pb[2], pCursor->pb[3]);
    pCursor->pb         += 4;
    pCursor->cbUnitLeft -= 4;
    pCursor->cbLeft     -= 4;
    if (!pCursor->fNativEndian)
        u32 = RT_BSWAP_U32(u32);
    return u32;
}


/**
 * Reads a 64-bit unsigned integer and advances the cursor.
 *
 * @returns 64-bit unsigned integer. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           What to return on read error.
 */
static uint64_t rtDwarfCursor_GetU64(PRTDWARFCURSOR pCursor, uint64_t uErrValue)
{
    if (pCursor->cbUnitLeft < 8)
    {
        pCursor->pb         += pCursor->cbUnitLeft;
        pCursor->cbLeft     -= pCursor->cbUnitLeft;
        pCursor->cbUnitLeft  = 0;
        pCursor->rc          = VERR_DWARF_UNEXPECTED_END;
        return uErrValue;
    }

    uint64_t u64 = RT_MAKE_U64_FROM_U8(pCursor->pb[0], pCursor->pb[1], pCursor->pb[2], pCursor->pb[3],
                                       pCursor->pb[4], pCursor->pb[5], pCursor->pb[6], pCursor->pb[7]);
    pCursor->pb         += 8;
    pCursor->cbUnitLeft -= 8;
    pCursor->cbLeft     -= 8;
    if (!pCursor->fNativEndian)
        u64 = RT_BSWAP_U64(u64);
    return u64;
}


/**
 * Reads an unsigned LEB128 encoded number.
 *
 * @returns unsigned 64-bit number. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           The value to return on error.
 */
static uint64_t rtDwarfCursor_GetULeb128(PRTDWARFCURSOR pCursor, uint64_t uErrValue)
{
    if (pCursor->cbUnitLeft < 1)
    {
        pCursor->rc = VERR_DWARF_UNEXPECTED_END;
        return uErrValue;
    }

    /*
     * Special case - single byte.
     */
    uint8_t b = pCursor->pb[0];
    if (!(b & 0x80))
    {
        pCursor->pb         += 1;
        pCursor->cbUnitLeft -= 1;
        pCursor->cbLeft     -= 1;
        return b;
    }

    /*
     * Generic case.
     */
    /* Decode. */
    uint32_t off    = 1;
    uint64_t u64Ret = b & 0x7f;
    do
    {
        if (off == pCursor->cbUnitLeft)
        {
            pCursor->rc = VERR_DWARF_UNEXPECTED_END;
            u64Ret = uErrValue;
            break;
        }
        b = pCursor->pb[off];
        u64Ret |= (b & 0x7f) << off * 7;
        off++;
    } while (b & 0x80);

    /* Update the cursor. */
    pCursor->pb         += off;
    pCursor->cbUnitLeft -= off;
    pCursor->cbLeft     -= off;

    /* Check the range. */
    uint32_t cBits = off * 7;
    if (cBits > 64)
    {
        pCursor->rc = VERR_DWARF_LEB_OVERFLOW;
        u64Ret = uErrValue;
    }

    return u64Ret;
}


/**
 * Reads a signed LEB128 encoded number.
 *
 * @returns signed 64-bit number. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   sErrValue           The value to return on error.
 */
static int64_t rtDwarfCursor_GetSLeb128(PRTDWARFCURSOR pCursor, int64_t sErrValue)
{
    if (pCursor->cbUnitLeft < 1)
    {
        pCursor->rc = VERR_DWARF_UNEXPECTED_END;
        return sErrValue;
    }

    /*
     * Special case - single byte.
     */
    uint8_t b = pCursor->pb[0];
    if (!(b & 0x80))
    {
        pCursor->pb         += 1;
        pCursor->cbUnitLeft -= 1;
        pCursor->cbLeft     -= 1;
        if (b & 0x40)
            b |= 0x80;
        return (int8_t)b;
    }

    /*
     * Generic case.
     */
    /* Decode it. */
    uint32_t off    = 1;
    uint64_t u64Ret = b & 0x7f;
    do
    {
        if (off == pCursor->cbUnitLeft)
        {
            pCursor->rc = VERR_DWARF_UNEXPECTED_END;
            u64Ret = (uint64_t)sErrValue;
            break;
        }
        b = pCursor->pb[off];
        u64Ret |= (b & 0x7f) << off * 7;
        off++;
    } while (b & 0x80);

    /* Update cursor. */
    pCursor->pb         += off;
    pCursor->cbUnitLeft -= off;
    pCursor->cbLeft     -= off;

    /* Check the range. */
    uint32_t cBits = off * 7;
    if (cBits > 64)
    {
        pCursor->rc = VERR_DWARF_LEB_OVERFLOW;
        u64Ret = (uint64_t)sErrValue;
    }
    /* Sign extend the value. */
    else if (u64Ret & RT_BIT_64(cBits - 1))
        u64Ret |= ~(RT_BIT_64(cBits - 1) - 1);

    return (int64_t)u64Ret;
}


/**
 * Reads an unsigned LEB128 encoded number, max 32-bit width.
 *
 * @returns unsigned 32-bit number. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           The value to return on error.
 */
static uint32_t rtDwarfCursor_GetULeb128AsU32(PRTDWARFCURSOR pCursor, uint32_t uErrValue)
{
    uint64_t u64 = rtDwarfCursor_GetULeb128(pCursor, uErrValue);
    if (u64 > UINT32_MAX)
    {
        pCursor->rc = VERR_DWARF_LEB_OVERFLOW;
        return uErrValue;
    }
    return (uint32_t)u64;
}


/**
 * Reads a signed LEB128 encoded number, max 32-bit width.
 *
 * @returns signed 32-bit number. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   sErrValue           The value to return on error.
 */
static int32_t rtDwarfCursor_GetSLeb128AsS32(PRTDWARFCURSOR pCursor, int32_t sErrValue)
{
    int64_t s64 = rtDwarfCursor_GetSLeb128(pCursor, sErrValue);
    if (s64 > INT32_MAX || s64 < INT32_MIN)
    {
        pCursor->rc = VERR_DWARF_LEB_OVERFLOW;
        return sErrValue;
    }
    return (int32_t)s64;
}


/**
 * Skips a LEB128 encoded number.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor.
 */
static int rtDwarfCursor_SkipLeb128(PRTDWARFCURSOR pCursor)
{
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;

    if (pCursor->cbUnitLeft < 1)
        return pCursor->rc = VERR_DWARF_UNEXPECTED_END;

    uint32_t offSkip = 1;
    if (pCursor->pb[0] & 0x80)
        do
        {
            if (offSkip == pCursor->cbUnitLeft)
            {
                pCursor->rc = VERR_DWARF_UNEXPECTED_END;
                break;
            }
        } while (pCursor->pb[offSkip++] & 0x80);

    pCursor->pb         += offSkip;
    pCursor->cbUnitLeft -= offSkip;
    pCursor->cbLeft     -= offSkip;
    return pCursor->rc;
}


/**
 * Advances the cursor a given number of bytes.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor.
 * @param   offSkip             The number of bytes to advance.
 */
static int rtDwarfCursor_SkipBytes(PRTDWARFCURSOR pCursor, uint64_t offSkip)
{
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;
    if (pCursor->cbUnitLeft < offSkip)
        return pCursor->rc = VERR_DWARF_UNEXPECTED_END;

    size_t const offSkipSizeT = (size_t)offSkip;
    pCursor->cbUnitLeft -= offSkipSizeT;
    pCursor->cbLeft     -= offSkipSizeT;
    pCursor->pb         += offSkipSizeT;

    return VINF_SUCCESS;
}


/**
 * Reads a zero terminated string, advancing the cursor beyond the terminator.
 *
 * @returns Pointer to the string.
 * @param   pCursor             The cursor.
 * @param   pszErrValue         What to return if the string isn't terminated
 *                              before the end of the unit.
 */
static const char *rtDwarfCursor_GetSZ(PRTDWARFCURSOR pCursor, const char *pszErrValue)
{
    const char *pszRet = (const char *)pCursor->pb;
    for (;;)
    {
        if (!pCursor->cbUnitLeft)
        {
            pCursor->rc = VERR_DWARF_BAD_STRING;
            return pszErrValue;
        }
        pCursor->cbUnitLeft--;
        pCursor->cbLeft--;
        if (!*pCursor->pb++)
            break;
    }
    return pszRet;
}


/**
 * Reads a 1, 2, 4 or 8 byte unsigned value.
 *
 * @returns 64-bit unsigned value.
 * @param   pCursor             The cursor.
 * @param   cbValue             The value size.
 * @param   uErrValue           The error value.
 */
static uint64_t rtDwarfCursor_GetVarSizedU(PRTDWARFCURSOR pCursor, size_t cbValue, uint64_t uErrValue)
{
    uint64_t u64Ret;
    switch (cbValue)
    {
        case 1: u64Ret = rtDwarfCursor_GetU8( pCursor, UINT8_MAX); break;
        case 2: u64Ret = rtDwarfCursor_GetU16(pCursor, UINT16_MAX); break;
        case 4: u64Ret = rtDwarfCursor_GetU32(pCursor, UINT32_MAX); break;
        case 8: u64Ret = rtDwarfCursor_GetU64(pCursor, UINT64_MAX); break;
        default:
            pCursor->rc = VERR_DWARF_BAD_INFO;
            return uErrValue;
    }
    if (RT_FAILURE(pCursor->rc))
        return uErrValue;
    return u64Ret;
}


#if 0 /* unused */
/**
 * Gets the pointer to a variable size block and advances the cursor.
 *
 * @returns Pointer to the block at the current cursor location. On error
 *          RTDWARFCURSOR::rc is set and NULL returned.
 * @param   pCursor             The cursor.
 * @param   cbBlock             The block size.
 */
static const uint8_t *rtDwarfCursor_GetBlock(PRTDWARFCURSOR pCursor, uint32_t cbBlock)
{
    if (cbBlock > pCursor->cbUnitLeft)
    {
        pCursor->rc = VERR_DWARF_UNEXPECTED_END;
        return NULL;
    }

    uint8_t const *pb = &pCursor->pb[0];
    pCursor->pb         += cbBlock;
    pCursor->cbUnitLeft -= cbBlock;
    pCursor->cbLeft     -= cbBlock;
    return pb;
}
#endif


/**
 * Reads an unsigned DWARF half number.
 *
 * @returns The number. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           What to return on error.
 */
static uint16_t rtDwarfCursor_GetUHalf(PRTDWARFCURSOR pCursor, uint16_t uErrValue)
{
    return rtDwarfCursor_GetU16(pCursor, uErrValue);
}


/**
 * Reads an unsigned DWARF byte number.
 *
 * @returns The number. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           What to return on error.
 */
static uint8_t rtDwarfCursor_GetUByte(PRTDWARFCURSOR pCursor, uint8_t uErrValue)
{
    return rtDwarfCursor_GetU8(pCursor, uErrValue);
}


/**
 * Reads a signed DWARF byte number.
 *
 * @returns The number. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   iErrValue           What to return on error.
 */
static int8_t rtDwarfCursor_GetSByte(PRTDWARFCURSOR pCursor, int8_t iErrValue)
{
    return (int8_t)rtDwarfCursor_GetU8(pCursor, (uint8_t)iErrValue);
}


/**
 * Reads a unsigned DWARF offset value.
 *
 * @returns The value. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           What to return on error.
 */
static uint64_t rtDwarfCursor_GetUOff(PRTDWARFCURSOR pCursor, uint64_t uErrValue)
{
    if (pCursor->f64bitDwarf)
        return rtDwarfCursor_GetU64(pCursor, uErrValue);
    return rtDwarfCursor_GetU32(pCursor, (uint32_t)uErrValue);
}


/**
 * Reads a unsigned DWARF native offset value.
 *
 * @returns The value. On error RTDWARFCURSOR::rc is set and @a
 *          uErrValue is returned.
 * @param   pCursor             The cursor.
 * @param   uErrValue           What to return on error.
 */
static uint64_t rtDwarfCursor_GetNativeUOff(PRTDWARFCURSOR pCursor, uint64_t uErrValue)
{
    switch (pCursor->cbNativeAddr)
    {
        case 1: return rtDwarfCursor_GetU8(pCursor,  (uint8_t )uErrValue);
        case 2: return rtDwarfCursor_GetU16(pCursor, (uint16_t)uErrValue);
        case 4: return rtDwarfCursor_GetU32(pCursor, (uint32_t)uErrValue);
        case 8: return rtDwarfCursor_GetU64(pCursor, uErrValue);
        default:
            pCursor->rc = VERR_INTERNAL_ERROR_2;
            return uErrValue;
    }
}


/**
 * Reads a 1, 2, 4 or 8 byte unsigned value.
 *
 * @returns 64-bit unsigned value.
 * @param   pCursor             The cursor.
 * @param   bPtrEnc             The pointer encoding.
 * @param   uErrValue           The error value.
 */
static uint64_t rtDwarfCursor_GetPtrEnc(PRTDWARFCURSOR pCursor, uint8_t bPtrEnc, uint64_t uErrValue)
{
    uint64_t u64Ret;
    switch (bPtrEnc & DW_EH_PE_FORMAT_MASK)
    {
        case DW_EH_PE_ptr:
            u64Ret = rtDwarfCursor_GetNativeUOff(pCursor, uErrValue);
            break;
        case DW_EH_PE_uleb128:
            u64Ret = rtDwarfCursor_GetULeb128(pCursor, uErrValue);
            break;
        case DW_EH_PE_udata2:
            u64Ret = rtDwarfCursor_GetU16(pCursor, UINT16_MAX);
            break;
        case DW_EH_PE_udata4:
            u64Ret = rtDwarfCursor_GetU32(pCursor, UINT32_MAX);
            break;
        case DW_EH_PE_udata8:
            u64Ret = rtDwarfCursor_GetU64(pCursor, UINT64_MAX);
            break;
        case DW_EH_PE_sleb128:
            u64Ret = rtDwarfCursor_GetSLeb128(pCursor, uErrValue);
            break;
        case DW_EH_PE_sdata2:
            u64Ret = (int64_t)(int16_t)rtDwarfCursor_GetU16(pCursor, UINT16_MAX);
            break;
        case DW_EH_PE_sdata4:
            u64Ret = (int64_t)(int32_t)rtDwarfCursor_GetU32(pCursor, UINT32_MAX);
            break;
        case DW_EH_PE_sdata8:
            u64Ret = rtDwarfCursor_GetU64(pCursor, UINT64_MAX);
            break;
        default:
            pCursor->rc = VERR_DWARF_BAD_INFO;
            return uErrValue;
    }
    if (RT_FAILURE(pCursor->rc))
        return uErrValue;
    return u64Ret;
}


/**
 * Gets the unit length, updating the unit length member and DWARF bitness
 * members of the cursor.
 *
 * @returns The unit length.
 * @param   pCursor             The cursor.
 */
static uint64_t rtDwarfCursor_GetInitialLength(PRTDWARFCURSOR pCursor)
{
    /*
     * Read the initial length.
     */
    pCursor->cbUnitLeft = pCursor->cbLeft;
    uint64_t cbUnit = rtDwarfCursor_GetU32(pCursor, 0);
    if (cbUnit != UINT32_C(0xffffffff))
        pCursor->f64bitDwarf = false;
    else
    {
        pCursor->f64bitDwarf = true;
        cbUnit = rtDwarfCursor_GetU64(pCursor, 0);
    }


    /*
     * Set the unit length, quitely fixing bad lengths.
     */
    pCursor->cbUnitLeft = (size_t)cbUnit;
    if (   pCursor->cbUnitLeft > pCursor->cbLeft
        || pCursor->cbUnitLeft != cbUnit)
        pCursor->cbUnitLeft = pCursor->cbLeft;

    return cbUnit;
}


/**
 * Calculates the section offset corresponding to the current cursor position.
 *
 * @returns 32-bit section offset. If out of range, RTDWARFCURSOR::rc will be
 *          set and UINT32_MAX returned.
 * @param   pCursor             The cursor.
 */
static uint32_t rtDwarfCursor_CalcSectOffsetU32(PRTDWARFCURSOR pCursor)
{
    size_t off = pCursor->pb - pCursor->pbStart;
    uint32_t offRet = (uint32_t)off;
    if (offRet != off)
    {
        AssertFailed();
        pCursor->rc = VERR_OUT_OF_RANGE;
        offRet = UINT32_MAX;
    }
    return offRet;
}


/**
 * Calculates an absolute cursor position from one relative to the current
 * cursor position.
 *
 * @returns The absolute cursor position.
 * @param   pCursor             The cursor.
 * @param   offRelative         The relative position.  Must be a positive
 *                              offset.
 */
static uint8_t const *rtDwarfCursor_CalcPos(PRTDWARFCURSOR pCursor, size_t offRelative)
{
    if (offRelative > pCursor->cbUnitLeft)
    {
        Log(("rtDwarfCursor_CalcPos: bad position %#zx, cbUnitLeft=%#zu\n", offRelative, pCursor->cbUnitLeft));
        pCursor->rc = VERR_DWARF_BAD_POS;
        return NULL;
    }
    return pCursor->pb + offRelative;
}


/**
 * Advances the cursor to the given position.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor.
 * @param   pbNewPos            The new position - returned by
 *                              rtDwarfCursor_CalcPos().
 */
static int rtDwarfCursor_AdvanceToPos(PRTDWARFCURSOR pCursor, uint8_t const *pbNewPos)
{
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;
    AssertPtr(pbNewPos);
    if ((uintptr_t)pbNewPos < (uintptr_t)pCursor->pb)
    {
        Log(("rtDwarfCursor_AdvanceToPos: bad position %p, current %p\n", pbNewPos, pCursor->pb));
        return pCursor->rc = VERR_DWARF_BAD_POS;
    }

    uintptr_t cbAdj = (uintptr_t)pbNewPos - (uintptr_t)pCursor->pb;
    if (RT_UNLIKELY(cbAdj > pCursor->cbUnitLeft))
    {
        AssertFailed();
        pCursor->rc = VERR_DWARF_BAD_POS;
        cbAdj = pCursor->cbUnitLeft;
    }

    pCursor->cbUnitLeft -= cbAdj;
    pCursor->cbLeft     -= cbAdj;
    pCursor->pb         += cbAdj;
    return pCursor->rc;
}


/**
 * Check if the cursor is at the end of the current DWARF unit.
 *
 * @retval  true if at the end or a cursor error is pending.
 * @retval  false if not.
 * @param   pCursor             The cursor.
 */
static bool rtDwarfCursor_IsAtEndOfUnit(PRTDWARFCURSOR pCursor)
{
    return !pCursor->cbUnitLeft || RT_FAILURE(pCursor->rc);
}


/**
 * Skips to the end of the current unit.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor.
 */
static int rtDwarfCursor_SkipUnit(PRTDWARFCURSOR pCursor)
{
    pCursor->pb        += pCursor->cbUnitLeft;
    pCursor->cbLeft    -= pCursor->cbUnitLeft;
    pCursor->cbUnitLeft = 0;
    return pCursor->rc;
}


/**
 * Check if the cursor is at the end of the section (or whatever the cursor is
 * processing).
 *
 * @retval  true if at the end or a cursor error is pending.
 * @retval  false if not.
 * @param   pCursor             The cursor.
 */
static bool rtDwarfCursor_IsAtEnd(PRTDWARFCURSOR pCursor)
{
    return !pCursor->cbLeft || RT_FAILURE(pCursor->rc);
}


/**
 * Initialize a section reader cursor.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor.
 * @param   pThis               The dwarf module.
 * @param   enmSect             The name of the section to read.
 */
static int rtDwarfCursor_Init(PRTDWARFCURSOR pCursor, PRTDBGMODDWARF pThis, krtDbgModDwarfSect enmSect)
{
    int rc = rtDbgModDwarfLoadSection(pThis, enmSect);
    if (RT_FAILURE(rc))
        return rc;

    pCursor->enmSect          = enmSect;
    pCursor->pbStart          = (uint8_t const *)pThis->aSections[enmSect].pv;
    pCursor->pb               = pCursor->pbStart;
    pCursor->cbLeft           = pThis->aSections[enmSect].cb;
    pCursor->cbUnitLeft       = pCursor->cbLeft;
    pCursor->pDwarfMod        = pThis;
    pCursor->f64bitDwarf      = false;
    /** @todo ask the image about the endian used as well as the address
     *        width. */
    pCursor->fNativEndian     = true;
    pCursor->cbNativeAddr     = 4;
    pCursor->rc               = VINF_SUCCESS;

    return VINF_SUCCESS;
}


/**
 * Initialize a section reader cursor with a skip offset.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor.
 * @param   pThis               The dwarf module.
 * @param   enmSect             The name of the section to read.
 * @param   offSect             The offset to skip into the section.
 */
static int rtDwarfCursor_InitWithOffset(PRTDWARFCURSOR pCursor, PRTDBGMODDWARF pThis,
                                        krtDbgModDwarfSect enmSect, uint32_t offSect)
{
    if (offSect > pThis->aSections[enmSect].cb)
    {
        Log(("rtDwarfCursor_InitWithOffset: offSect=%#x cb=%#x enmSect=%d\n", offSect, pThis->aSections[enmSect].cb, enmSect));
        return VERR_DWARF_BAD_POS;
    }

    int rc = rtDwarfCursor_Init(pCursor, pThis, enmSect);
    if (RT_SUCCESS(rc))
    {
        /* pCursor->pbStart += offSect; - we're skipping, offsets are relative to start of section... */
        pCursor->pb         += offSect;
        pCursor->cbLeft     -= offSect;
        pCursor->cbUnitLeft -= offSect;
    }

    return rc;
}


/**
 * Initialize a cursor for a block (subsection) retrieved from the given cursor.
 *
 * The parent cursor will be advanced past the block.
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor.
 * @param   pParent             The parent cursor. Will be moved by @a cbBlock.
 * @param   cbBlock             The size of the block the new cursor should
 *                              cover.
 */
static int rtDwarfCursor_InitForBlock(PRTDWARFCURSOR pCursor, PRTDWARFCURSOR pParent, uint32_t cbBlock)
{
    if (RT_FAILURE(pParent->rc))
        return pParent->rc;
    if (pParent->cbUnitLeft < cbBlock)
    {
        Log(("rtDwarfCursor_InitForBlock: cbUnitLeft=%#x < cbBlock=%#x \n", pParent->cbUnitLeft, cbBlock));
        return VERR_DWARF_BAD_POS;
    }

    *pCursor = *pParent;
    pCursor->cbLeft     = cbBlock;
    pCursor->cbUnitLeft = cbBlock;

    pParent->pb         += cbBlock;
    pParent->cbLeft     -= cbBlock;
    pParent->cbUnitLeft -= cbBlock;

    return VINF_SUCCESS;
}


/**
 * Initialize a reader cursor for a memory block (eh_frame).
 *
 * @returns IPRT status code.
 * @param   pCursor             The cursor.
 * @param   pvMem               The memory block.
 * @param   cbMem               The size of the memory block.
 */
static int rtDwarfCursor_InitForMem(PRTDWARFCURSOR pCursor, void const *pvMem, size_t cbMem)
{
    pCursor->enmSect          = krtDbgModDwarfSect_End;
    pCursor->pbStart          = (uint8_t const *)pvMem;
    pCursor->pb               = (uint8_t const *)pvMem;
    pCursor->cbLeft           = cbMem;
    pCursor->cbUnitLeft       = cbMem;
    pCursor->pDwarfMod        = NULL;
    pCursor->f64bitDwarf      = false;
    /** @todo ask the image about the endian used as well as the address
     *        width. */
    pCursor->fNativEndian     = true;
    pCursor->cbNativeAddr     = 4;
    pCursor->rc               = VINF_SUCCESS;

    return VINF_SUCCESS;
}


/**
 * Deletes a section reader initialized by rtDwarfCursor_Init.
 *
 * @returns @a rcOther or RTDWARCURSOR::rc.
 * @param   pCursor             The section reader.
 * @param   rcOther             Other error code to be returned if it indicates
 *                              error or if the cursor status is OK.
 */
static int rtDwarfCursor_Delete(PRTDWARFCURSOR pCursor, int rcOther)
{
    /* ... and a drop of poison. */
    pCursor->pb         = NULL;
    pCursor->cbLeft     = ~(size_t)0;
    pCursor->cbUnitLeft = ~(size_t)0;
    pCursor->pDwarfMod  = NULL;
    if (RT_FAILURE(pCursor->rc) && RT_SUCCESS(rcOther))
        rcOther = pCursor->rc;
    pCursor->rc         = VERR_INTERNAL_ERROR_4;
    return rcOther;
}


/*
 *
 * DWARF Frame Unwind Information.
 * DWARF Frame Unwind Information.
 * DWARF Frame Unwind Information.
 *
 */

/**
 * Common information entry (CIE) information.
 */
typedef struct RTDWARFCIEINFO
{
    /** The segment location of the CIE. */
    uint64_t        offCie;
    /** The DWARF version. */
    uint8_t         uDwarfVer;
    /** The address pointer encoding. */
    uint8_t         bAddressPtrEnc;
    /** The segment size (v4). */
    uint8_t         cbSegment;
    /** The return register column.  UINT8_MAX if default register. */
    uint8_t         bRetReg;
    /** The LSDA pointer encoding. */
    uint8_t         bLsdaPtrEnc;

    /** Set if the EH data field is present ('eh'). */
    bool            fHasEhData : 1;
    /** Set if there is an augmentation data size ('z'). */
    bool            fHasAugmentationSize : 1;
    /** Set if the augmentation data contains a LSDA (pointer size byte in CIE,
     * pointer in FDA) ('L'). */
    bool            fHasLanguageSpecificDataArea : 1;
    /** Set if the augmentation data contains a personality routine
     * (pointer size + pointer) ('P'). */
    bool            fHasPersonalityRoutine : 1;
    /** Set if the augmentation data contains the address encoding . */
    bool            fHasAddressEnc : 1;
    /** Set if signal frame. */
    bool            fIsSignalFrame : 1;
    /** Set if we've encountered unknown augmentation data.  This
     * means the CIE is incomplete and cannot be used. */
    bool            fHasUnknowAugmentation : 1;

    /** Copy of the augmentation string. */
    const char     *pszAugmentation;

    /** Code alignment factor for the instruction. */
    uint64_t        uCodeAlignFactor;
    /** Data alignment factor for the instructions. */
    int64_t         iDataAlignFactor;

    /** Pointer to the instruction sequence. */
    uint8_t const  *pbInstructions;
    /** The length of the instruction sequence. */
    size_t          cbInstructions;
} RTDWARFCIEINFO;
/** Pointer to CIE info. */
typedef RTDWARFCIEINFO *PRTDWARFCIEINFO;
/** Pointer to const CIE info. */
typedef RTDWARFCIEINFO const *PCRTDWARFCIEINFO;


/** Number of registers we care about.
 * @note We're currently not expecting to be decoding ppc, arm, ia64 or such,
 *       only x86 and x86_64.  We can easily increase the column count. */
#define RTDWARFCF_MAX_REGISTERS         96


/**
 * Call frame state row.
 */
typedef struct RTDWARFCFROW
{
    /** Stack worked by DW_CFA_remember_state and DW_CFA_restore_state. */
    struct RTDWARFCFROW    *pNextOnStack;

    /** @name CFA - Canonical frame address expression.
     * Since there are partial CFA instructions, we cannot be lazy like with the
     * register but keep register+offset around.  For DW_CFA_def_cfa_expression
     * we just take down the program location, though.
     * @{ */
    /** Pointer to DW_CFA_def_cfa_expression instruction, NULL if reg+offset. */
    uint8_t const          *pbCfaExprInstr;
    /** The CFA register offset. */
    int64_t                 offCfaReg;
    /** The CFA base register number. */
    uint16_t                uCfaBaseReg;
    /** Set if we've got a valid CFA definition. */
    bool                    fCfaDefined : 1;
    /** @} */

    /** Set if on the heap and needs freeing. */
    bool                    fOnHeap : 1;
    /** Pointer to the instructions bytes defining registers.
     * NULL means  */
    uint8_t const          *apbRegInstrs[RTDWARFCF_MAX_REGISTERS];
} RTDWARFCFROW;
typedef RTDWARFCFROW *PRTDWARFCFROW;
typedef RTDWARFCFROW const *PCRTDWARFCFROW;

/** Row program execution state. */
typedef struct RTDWARFCFEXEC
{
    PRTDWARFCFROW       pRow;
    /** Number of PC bytes left to advance before we get a hit. */
    uint64_t            cbLeftToAdvance;
    /** Number of pushed rows. */
    uint32_t            cPushes;
    /** Set if little endian, clear if big endian. */
    bool                fLittleEndian;
    /** The CIE.  */
    PCRTDWARFCIEINFO    pCie;
    /** The program counter value for the FDE.  Subjected to segment.
     * Needed for DW_CFA_set_loc.  */
    uint64_t            uPcBegin;
    /** The offset relative to uPcBegin for which we're searching for a row.
     * Needed for DW_CFA_set_loc.  */
    uint64_t            offInRange;
} RTDWARFCFEXEC;
typedef RTDWARFCFEXEC *PRTDWARFCFEXEC;


/* Set of macros for getting and skipping operands. */
#define SKIP_ULEB128_OR_LEB128() \
    do \
    { \
        AssertReturn(offInstr < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO); \
    } while (pbInstr[offInstr++] & 0x80)

#define GET_ULEB128_AS_U14(a_uDst) \
    do \
    { \
        AssertReturn(offInstr < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO); \
        uint8_t b = pbInstr[offInstr++]; \
        (a_uDst) = b & 0x7f; \
        if (b & 0x80) \
        { \
            AssertReturn(offInstr < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO); \
            b = pbInstr[offInstr++]; \
            AssertReturn(!(b & 0x80), VERR_DBG_MALFORMED_UNWIND_INFO); \
            (a_uDst) |= (uint16_t)b << 7; \
        } \
    } while (0)
#define GET_ULEB128_AS_U63(a_uDst) \
    do \
    { \
        AssertReturn(offInstr < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO); \
        uint8_t b = pbInstr[offInstr++]; \
        (a_uDst) = b & 0x7f; \
        if (b & 0x80) \
        { \
            unsigned cShift = 7; \
            do \
            { \
                AssertReturn(offInstr < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO); \
                AssertReturn(cShift < 63, VERR_DWARF_LEB_OVERFLOW); \
                b = pbInstr[offInstr++]; \
                (a_uDst) |= (uint16_t)(b & 0x7f) << cShift; \
                cShift += 7; \
            } while (b & 0x80); \
        } \
    } while (0)
#define GET_LEB128_AS_I63(a_uDst) \
    do \
    { \
        AssertReturn(offInstr < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO); \
        uint8_t b = pbInstr[offInstr++]; \
        if (!(b & 0x80)) \
            (a_uDst) = !(b & 0x40) ? b : (int64_t)(int8_t)(b | 0x80); \
        else \
        { \
            /* Read value into unsigned variable: */ \
            unsigned cShift = 7; \
            uint64_t uTmp   = b & 0x7f; \
            do \
            { \
                AssertReturn(offInstr < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO); \
                AssertReturn(cShift < 63, VERR_DWARF_LEB_OVERFLOW); \
                b = pbInstr[offInstr++]; \
                uTmp |= (uint16_t)(b & 0x7f) << cShift; \
                cShift += 7; \
            } while (b & 0x80); \
            /* Sign extend before setting the destination value: */ \
            cShift -= 7 + 1; \
            if (uTmp & RT_BIT_64(cShift)) \
                uTmp |= ~(RT_BIT_64(cShift) - 1); \
            (a_uDst) = (int64_t)uTmp; \
        } \
    } while (0)

#define SKIP_BLOCK() \
    do \
    { \
        uint16_t cbBlock; \
        GET_ULEB128_AS_U14(cbBlock); \
        AssertReturn(offInstr + cbBlock <= cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO); \
        offInstr += cbBlock; \
    } while (0)


static int rtDwarfUnwind_Execute(PRTDWARFCFEXEC pExecState, uint8_t const *pbInstr, uint32_t cbInstr)
{
    PRTDWARFCFROW pRow = pExecState->pRow;
    for (uint32_t offInstr = 0; offInstr < cbInstr;)
    {
        /*
         * Instruction switches.
         */
        uint8_t const bInstr = pbInstr[offInstr++];
        switch (bInstr & DW_CFA_high_bit_mask)
        {
            case DW_CFA_advance_loc:
            {
                uint8_t const cbAdvance = bInstr & ~DW_CFA_high_bit_mask;
                if (cbAdvance > pExecState->cbLeftToAdvance)
                    return VINF_SUCCESS;
                pExecState->cbLeftToAdvance -= cbAdvance;
                break;
            }

            case DW_CFA_offset:
            {
                uint8_t iReg = bInstr & ~DW_CFA_high_bit_mask;
                if (iReg < RT_ELEMENTS(pRow->apbRegInstrs))
                    pRow->apbRegInstrs[iReg] = &pbInstr[offInstr - 1];
                SKIP_ULEB128_OR_LEB128();
                break;
            }

            case 0:
                switch (bInstr)
                {
                    case DW_CFA_nop:
                        break;

                    /*
                     * Register instructions.
                     */
                    case DW_CFA_register:
                    case DW_CFA_offset_extended:
                    case DW_CFA_offset_extended_sf:
                    case DW_CFA_val_offset:
                    case DW_CFA_val_offset_sf:
                    {
                        uint8_t const * const pbCurInstr = &pbInstr[offInstr - 1];
                        uint16_t              iReg;
                        GET_ULEB128_AS_U14(iReg);
                        if (iReg < RT_ELEMENTS(pRow->apbRegInstrs))
                            pRow->apbRegInstrs[iReg] = pbCurInstr;
                        SKIP_ULEB128_OR_LEB128();
                        break;
                    }

                    case DW_CFA_expression:
                    case DW_CFA_val_expression:
                    {
                        uint8_t const * const pbCurInstr = &pbInstr[offInstr - 1];
                        uint16_t              iReg;
                        GET_ULEB128_AS_U14(iReg);
                        if (iReg < RT_ELEMENTS(pRow->apbRegInstrs))
                            pRow->apbRegInstrs[iReg] = pbCurInstr;
                        SKIP_BLOCK();
                        break;
                    }

                    case DW_CFA_restore_extended:
                    {
                        uint8_t const * const pbCurInstr = &pbInstr[offInstr - 1];
                        uint16_t              iReg;
                        GET_ULEB128_AS_U14(iReg);
                        if (iReg < RT_ELEMENTS(pRow->apbRegInstrs))
                            pRow->apbRegInstrs[iReg] = pbCurInstr;
                        break;
                    }

                    case DW_CFA_undefined:
                    {
                        uint16_t iReg;
                        GET_ULEB128_AS_U14(iReg);
                        if (iReg < RT_ELEMENTS(pRow->apbRegInstrs))
                            pRow->apbRegInstrs[iReg] = NULL;
                        break;
                    }

                    case DW_CFA_same_value:
                    {
                        uint8_t const * const pbCurInstr = &pbInstr[offInstr - 1];
                        uint16_t              iReg;
                        GET_ULEB128_AS_U14(iReg);
                        if (iReg < RT_ELEMENTS(pRow->apbRegInstrs))
                            pRow->apbRegInstrs[iReg] = pbCurInstr;
                        break;
                    }


                    /*
                     * CFA instructions.
                     */
                    case DW_CFA_def_cfa:
                    {
                        GET_ULEB128_AS_U14(pRow->uCfaBaseReg);
                        uint64_t offCfaReg;
                        GET_ULEB128_AS_U63(offCfaReg);
                        pRow->offCfaReg        = offCfaReg;
                        pRow->pbCfaExprInstr   = NULL;
                        pRow->fCfaDefined      = true;
                        break;
                    }

                    case DW_CFA_def_cfa_register:
                    {
                        GET_ULEB128_AS_U14(pRow->uCfaBaseReg);
                        pRow->pbCfaExprInstr   = NULL;
                        pRow->fCfaDefined      = true;
                        /* Leaves offCfaReg as is. */
                        break;
                    }

                    case DW_CFA_def_cfa_offset:
                    {
                        uint64_t offCfaReg;
                        GET_ULEB128_AS_U63(offCfaReg);
                        pRow->offCfaReg        = offCfaReg;
                        pRow->pbCfaExprInstr   = NULL;
                        pRow->fCfaDefined      = true;
                        /* Leaves uCfaBaseReg as is. */
                        break;
                    }

                    case DW_CFA_def_cfa_sf:
                        GET_ULEB128_AS_U14(pRow->uCfaBaseReg);
                        GET_LEB128_AS_I63(pRow->offCfaReg);
                        pRow->pbCfaExprInstr   = NULL;
                        pRow->fCfaDefined      = true;
                        break;

                    case DW_CFA_def_cfa_offset_sf:
                        GET_LEB128_AS_I63(pRow->offCfaReg);
                        pRow->pbCfaExprInstr   = NULL;
                        pRow->fCfaDefined      = true;
                        /* Leaves uCfaBaseReg as is. */
                        break;

                    case DW_CFA_def_cfa_expression:
                        pRow->pbCfaExprInstr   = &pbInstr[offInstr - 1];
                        pRow->fCfaDefined      = true;
                        SKIP_BLOCK();
                        break;

                    /*
                     * Less likely instructions:
                     */
                    case DW_CFA_advance_loc1:
                    {
                        AssertReturn(offInstr < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO);
                        uint8_t const cbAdvance = pbInstr[offInstr++];
                        if (cbAdvance > pExecState->cbLeftToAdvance)
                            return VINF_SUCCESS;
                        pExecState->cbLeftToAdvance -= cbAdvance;
                        break;
                    }

                    case DW_CFA_advance_loc2:
                    {
                        AssertReturn(offInstr + 1 < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO);
                        uint16_t const cbAdvance = pExecState->fLittleEndian
                                                 ? RT_MAKE_U16(pbInstr[offInstr], pbInstr[offInstr + 1])
                                                 : RT_MAKE_U16(pbInstr[offInstr + 1], pbInstr[offInstr]);
                        if (cbAdvance > pExecState->cbLeftToAdvance)
                            return VINF_SUCCESS;
                        pExecState->cbLeftToAdvance -= cbAdvance;
                        offInstr += 2;
                        break;
                    }

                    case DW_CFA_advance_loc4:
                    {
                        AssertReturn(offInstr + 3 < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO);
                        uint32_t const cbAdvance = pExecState->fLittleEndian
                                                 ? RT_MAKE_U32_FROM_U8(pbInstr[offInstr + 0], pbInstr[offInstr + 1],
                                                                       pbInstr[offInstr + 2], pbInstr[offInstr + 3])
                                                 : RT_MAKE_U32_FROM_U8(pbInstr[offInstr + 3], pbInstr[offInstr + 2],
                                                                       pbInstr[offInstr + 1], pbInstr[offInstr + 0]);
                        if (cbAdvance > pExecState->cbLeftToAdvance)
                            return VINF_SUCCESS;
                        pExecState->cbLeftToAdvance -= cbAdvance;
                        offInstr += 4;
                        break;
                    }

                    /*
                     * This bugger is really annoying and probably never used.
                     */
                    case DW_CFA_set_loc:
                    {
                        /* Ignore the segment number. */
                        if (pExecState->pCie->cbSegment)
                        {
                            offInstr += pExecState->pCie->cbSegment;
                            AssertReturn(offInstr < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO);
                        }

                        /* Retrieve the address. sigh. */
                        uint64_t uAddress;
                        switch (pExecState->pCie->bAddressPtrEnc & (DW_EH_PE_FORMAT_MASK | DW_EH_PE_indirect))
                        {
                            case DW_EH_PE_udata2:
                                AssertReturn(offInstr + 1 < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO);
                                if (pExecState->fLittleEndian)
                                    uAddress = RT_MAKE_U16(pbInstr[offInstr], pbInstr[offInstr + 1]);
                                else
                                    uAddress = RT_MAKE_U16(pbInstr[offInstr + 1], pbInstr[offInstr]);
                                offInstr += 2;
                                break;
                            case DW_EH_PE_sdata2:
                                AssertReturn(offInstr + 1 < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO);
                                if (pExecState->fLittleEndian)
                                    uAddress = (int64_t)(int16_t)RT_MAKE_U16(pbInstr[offInstr], pbInstr[offInstr + 1]);
                                else
                                    uAddress = (int64_t)(int16_t)RT_MAKE_U16(pbInstr[offInstr + 1], pbInstr[offInstr]);
                                offInstr += 2;
                                break;
                            case DW_EH_PE_udata4:
                                AssertReturn(offInstr + 3 < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO);
                                if (pExecState->fLittleEndian)
                                    uAddress = RT_MAKE_U32_FROM_U8(pbInstr[offInstr + 0], pbInstr[offInstr + 1],
                                                                   pbInstr[offInstr + 2], pbInstr[offInstr + 3]);
                                else
                                    uAddress = RT_MAKE_U32_FROM_U8(pbInstr[offInstr + 3], pbInstr[offInstr + 2],
                                                                   pbInstr[offInstr + 1], pbInstr[offInstr + 0]);

                                offInstr += 4;
                                break;
                            case DW_EH_PE_sdata4:
                                AssertReturn(offInstr + 3 < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO);
                                if (pExecState->fLittleEndian)
                                    uAddress = (int64_t)(int32_t)RT_MAKE_U32_FROM_U8(pbInstr[offInstr + 0], pbInstr[offInstr + 1],
                                                                                     pbInstr[offInstr + 2], pbInstr[offInstr + 3]);
                                else
                                    uAddress = (int64_t)(int32_t)RT_MAKE_U32_FROM_U8(pbInstr[offInstr + 3], pbInstr[offInstr + 2],
                                                                                     pbInstr[offInstr + 1], pbInstr[offInstr + 0]);
                                offInstr += 4;
                                break;
                            case DW_EH_PE_udata8:
                            case DW_EH_PE_sdata8:
                                AssertReturn(offInstr + 7 < cbInstr, VERR_DBG_MALFORMED_UNWIND_INFO);
                                if (pExecState->fLittleEndian)
                                    uAddress = RT_MAKE_U64_FROM_U8(pbInstr[offInstr + 0], pbInstr[offInstr + 1],
                                                                   pbInstr[offInstr + 2], pbInstr[offInstr + 3],
                                                                   pbInstr[offInstr + 4], pbInstr[offInstr + 5],
                                                                   pbInstr[offInstr + 6], pbInstr[offInstr + 7]);
                                else
                                    uAddress = RT_MAKE_U64_FROM_U8(pbInstr[offInstr + 7], pbInstr[offInstr + 6],
                                                                   pbInstr[offInstr + 5], pbInstr[offInstr + 4],
                                                                   pbInstr[offInstr + 3], pbInstr[offInstr + 2],
                                                                   pbInstr[offInstr + 1], pbInstr[offInstr + 0]);
                                offInstr += 8;
                                break;
                            case DW_EH_PE_sleb128:
                            case DW_EH_PE_uleb128:
                            default:
                                AssertMsgFailedReturn(("%#x\n", pExecState->pCie->bAddressPtrEnc), VERR_DWARF_TODO);
                        }
                        AssertReturn(uAddress >= pExecState->uPcBegin, VERR_DBG_MALFORMED_UNWIND_INFO);

                        /* Did we advance past the desire address already? */
                        if (uAddress > pExecState->uPcBegin + pExecState->offInRange)
                            return VINF_SUCCESS;
                        pExecState->cbLeftToAdvance = pExecState->uPcBegin + pExecState->offInRange - uAddress;
                        break;


                        /*
                         * Row state push/pop instructions.
                         */

                        case DW_CFA_remember_state:
                        {
                            AssertReturn(pExecState->cPushes < 10, VERR_DBG_MALFORMED_UNWIND_INFO);
                            PRTDWARFCFROW pNewRow = (PRTDWARFCFROW)RTMemTmpAlloc(sizeof(*pNewRow));
                            AssertReturn(pNewRow, VERR_NO_TMP_MEMORY);
                            memcpy(pNewRow, pRow, sizeof(*pNewRow));
                            pNewRow->pNextOnStack = pRow;
                            pNewRow->fOnHeap      = true;
                            pExecState->pRow      = pNewRow;
                            pExecState->cPushes  += 1;
                            pRow = pNewRow;
                            break;
                        }

                        case DW_CFA_restore_state:
                            AssertReturn(pRow->pNextOnStack, VERR_DBG_MALFORMED_UNWIND_INFO);
                            Assert(pRow->fOnHeap);
                            Assert(pExecState->cPushes > 0);
                            pExecState->cPushes -= 1;
                            pExecState->pRow     = pRow->pNextOnStack;
                            RTMemTmpFree(pRow);
                            pRow = pExecState->pRow;
                            break;
                    }
                }
                break;

            case DW_CFA_restore:
            {
                uint8_t const * const pbCurInstr = &pbInstr[offInstr - 1];
                uint8_t const         iReg       = bInstr & ~DW_CFA_high_bit_mask;
                if (iReg < RT_ELEMENTS(pRow->apbRegInstrs))
                    pRow->apbRegInstrs[iReg] = pbCurInstr;
                break;
            }
        }
    }
    return VINF_TRY_AGAIN;
}


/**
 * Register getter for AMD64.
 *
 * @returns true if found, false if not.
 * @param   pState              The unwind state to get the register from.
 * @param   iReg                The dwarf register number.
 * @param   puValue             Where to store the register value.
 */
static bool rtDwarfUnwind_Amd64GetRegFromState(PCRTDBGUNWINDSTATE pState, uint16_t iReg, uint64_t *puValue)
{
    switch (iReg)
    {
        case DWREG_AMD64_RAX:       *puValue = pState->u.x86.auRegs[X86_GREG_xAX];  return true;
        case DWREG_AMD64_RDX:       *puValue = pState->u.x86.auRegs[X86_GREG_xDX];  return true;
        case DWREG_AMD64_RCX:       *puValue = pState->u.x86.auRegs[X86_GREG_xCX];  return true;
        case DWREG_AMD64_RBX:       *puValue = pState->u.x86.auRegs[X86_GREG_xBX];  return true;
        case DWREG_AMD64_RSI:       *puValue = pState->u.x86.auRegs[X86_GREG_xSI];  return true;
        case DWREG_AMD64_RDI:       *puValue = pState->u.x86.auRegs[X86_GREG_xDI];  return true;
        case DWREG_AMD64_RBP:       *puValue = pState->u.x86.auRegs[X86_GREG_xBP];  return true;
        case DWREG_AMD64_RSP:       *puValue = pState->u.x86.auRegs[X86_GREG_xSP];  return true;
        case DWREG_AMD64_R8:        *puValue = pState->u.x86.auRegs[X86_GREG_x8];   return true;
        case DWREG_AMD64_R9:        *puValue = pState->u.x86.auRegs[X86_GREG_x9];   return true;
        case DWREG_AMD64_R10:       *puValue = pState->u.x86.auRegs[X86_GREG_x10];  return true;
        case DWREG_AMD64_R11:       *puValue = pState->u.x86.auRegs[X86_GREG_x11];  return true;
        case DWREG_AMD64_R12:       *puValue = pState->u.x86.auRegs[X86_GREG_x12];  return true;
        case DWREG_AMD64_R13:       *puValue = pState->u.x86.auRegs[X86_GREG_x13];  return true;
        case DWREG_AMD64_R14:       *puValue = pState->u.x86.auRegs[X86_GREG_x14];  return true;
        case DWREG_AMD64_R15:       *puValue = pState->u.x86.auRegs[X86_GREG_x15];  return true;
        case DWREG_AMD64_RFLAGS:    *puValue = pState->u.x86.uRFlags;               return true;
        case DWREG_AMD64_ES:        *puValue = pState->u.x86.auSegs[X86_SREG_ES];   return true;
        case DWREG_AMD64_CS:        *puValue = pState->u.x86.auSegs[X86_SREG_CS];   return true;
        case DWREG_AMD64_SS:        *puValue = pState->u.x86.auSegs[X86_SREG_SS];   return true;
        case DWREG_AMD64_DS:        *puValue = pState->u.x86.auSegs[X86_SREG_DS];   return true;
        case DWREG_AMD64_FS:        *puValue = pState->u.x86.auSegs[X86_SREG_FS];   return true;
        case DWREG_AMD64_GS:        *puValue = pState->u.x86.auSegs[X86_SREG_GS];   return true;
    }
    return false;
}


/**
 * Register getter for 386+.
 *
 * @returns true if found, false if not.
 * @param   pState              The unwind state to get the register from.
 * @param   iReg                The dwarf register number.
 * @param   puValue             Where to store the register value.
 */
static bool rtDwarfUnwind_X86GetRegFromState(PCRTDBGUNWINDSTATE pState, uint16_t iReg, uint64_t *puValue)
{
    switch (iReg)
    {
        case DWREG_X86_EAX:         *puValue = pState->u.x86.auRegs[X86_GREG_xAX];  return true;
        case DWREG_X86_ECX:         *puValue = pState->u.x86.auRegs[X86_GREG_xCX];  return true;
        case DWREG_X86_EDX:         *puValue = pState->u.x86.auRegs[X86_GREG_xDX];  return true;
        case DWREG_X86_EBX:         *puValue = pState->u.x86.auRegs[X86_GREG_xBX];  return true;
        case DWREG_X86_ESP:         *puValue = pState->u.x86.auRegs[X86_GREG_xSP];  return true;
        case DWREG_X86_EBP:         *puValue = pState->u.x86.auRegs[X86_GREG_xBP];  return true;
        case DWREG_X86_ESI:         *puValue = pState->u.x86.auRegs[X86_GREG_xSI];  return true;
        case DWREG_X86_EDI:         *puValue = pState->u.x86.auRegs[X86_GREG_xDI];  return true;
        case DWREG_X86_EFLAGS:      *puValue = pState->u.x86.uRFlags;               return true;
        case DWREG_X86_ES:          *puValue = pState->u.x86.auSegs[X86_SREG_ES];   return true;
        case DWREG_X86_CS:          *puValue = pState->u.x86.auSegs[X86_SREG_CS];   return true;
        case DWREG_X86_SS:          *puValue = pState->u.x86.auSegs[X86_SREG_SS];   return true;
        case DWREG_X86_DS:          *puValue = pState->u.x86.auSegs[X86_SREG_DS];   return true;
        case DWREG_X86_FS:          *puValue = pState->u.x86.auSegs[X86_SREG_FS];   return true;
        case DWREG_X86_GS:          *puValue = pState->u.x86.auSegs[X86_SREG_GS];   return true;
    }
    return false;
}

/** Register getter. */
typedef bool FNDWARFUNWINDGEREGFROMSTATE(PCRTDBGUNWINDSTATE pState, uint16_t iReg, uint64_t *puValue);
/** Pointer to a register getter. */
typedef FNDWARFUNWINDGEREGFROMSTATE *PFNDWARFUNWINDGEREGFROMSTATE;



/**
 * Does the heavy work for figuring out the return value of a register.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if register is undefined.
 *
 * @param   pRow        The DWARF unwind table "row" to use.
 * @param   uReg        The DWARF register number.
 * @param   pCie        The corresponding CIE.
 * @param   uCfa        The canonical frame address to use.
 * @param   pState      The unwind to use when reading stack.
 * @param   pOldState   The unwind state to get register values from.
 * @param   pfnGetReg   The register value getter.
 * @param   puValue     Where to store the return value.
 * @param   cbValue     The size this register would have on the stack.
 */
static int rtDwarfUnwind_CalcRegisterValue(PRTDWARFCFROW pRow, unsigned uReg, PCRTDWARFCIEINFO pCie, uint64_t uCfa,
                                           PRTDBGUNWINDSTATE pState, PCRTDBGUNWINDSTATE pOldState,
                                           PFNDWARFUNWINDGEREGFROMSTATE pfnGetReg, uint64_t *puValue, uint8_t cbValue)
{
    Assert(uReg < RT_ELEMENTS(pRow->apbRegInstrs));
    uint8_t const *pbInstr = pRow->apbRegInstrs[uReg];
    if (!pbInstr)
        return VERR_NOT_FOUND;

    uint32_t      cbInstr  = UINT32_MAX / 2;
    uint32_t      offInstr = 1;
    uint8_t const bInstr   = *pbInstr;
    switch (bInstr)
    {
        default:
            if ((bInstr & DW_CFA_high_bit_mask) == DW_CFA_offset)
            {
                uint64_t offCfa;
                GET_ULEB128_AS_U63(offCfa);
                int rc = pState->pfnReadStack(pState, uCfa + (int64_t)offCfa * pCie->iDataAlignFactor, cbValue, puValue);
                Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_offset %#RX64: %Rrc, %#RX64\n", uReg, uCfa + (int64_t)offCfa * pCie->iDataAlignFactor, rc, *puValue));
                return rc;
            }
            AssertReturn((bInstr & DW_CFA_high_bit_mask) == DW_CFA_restore, VERR_INTERNAL_ERROR);
            RT_FALL_THRU();
        case DW_CFA_restore_extended:
            /* Need to search the CIE for the rule. */
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_restore/extended:\n", uReg));
            AssertFailedReturn(VERR_DWARF_TODO);

        case DW_CFA_offset_extended:
        {
            SKIP_ULEB128_OR_LEB128();
            uint64_t offCfa;
            GET_ULEB128_AS_U63(offCfa);
            int rc = pState->pfnReadStack(pState, uCfa + (int64_t)offCfa * pCie->iDataAlignFactor, cbValue, puValue);
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_offset_extended %#RX64: %Rrc, %#RX64\n", uReg, uCfa + (int64_t)offCfa * pCie->iDataAlignFactor, rc, *puValue));
            return rc;
        }

        case DW_CFA_offset_extended_sf:
        {
            SKIP_ULEB128_OR_LEB128();
            int64_t offCfa;
            GET_LEB128_AS_I63(offCfa);
            int rc = pState->pfnReadStack(pState, uCfa + offCfa * pCie->iDataAlignFactor, cbValue, puValue);
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_offset_extended_sf %#RX64: %Rrc, %#RX64\n", uReg, uCfa + offCfa * pCie->iDataAlignFactor, rc, *puValue));
            return rc;
        }

        case DW_CFA_val_offset:
        {
            SKIP_ULEB128_OR_LEB128();
            uint64_t offCfa;
            GET_ULEB128_AS_U63(offCfa);
            *puValue = uCfa + (int64_t)offCfa * pCie->iDataAlignFactor;
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_val_offset: %#RX64\n", uReg, *puValue));
            return VINF_SUCCESS;
        }

        case DW_CFA_val_offset_sf:
        {
            SKIP_ULEB128_OR_LEB128();
            int64_t offCfa;
            GET_LEB128_AS_I63(offCfa);
            *puValue = uCfa + offCfa * pCie->iDataAlignFactor;
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_val_offset_sf: %#RX64\n", uReg, *puValue));
            return VINF_SUCCESS;
        }

        case DW_CFA_register:
        {
            SKIP_ULEB128_OR_LEB128();
            uint16_t iSrcReg;
            GET_ULEB128_AS_U14(iSrcReg);
            if (pfnGetReg(pOldState, uReg, puValue))
            {
                Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_register: %#RX64\n", uReg, *puValue));
                return VINF_SUCCESS;
            }
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_register: VERR_NOT_FOUND\n", uReg));
            return VERR_NOT_FOUND;
        }

        case DW_CFA_expression:
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_expression: TODO\n", uReg));
            AssertFailedReturn(VERR_DWARF_TODO);

        case DW_CFA_val_expression:
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_val_expression: TODO\n", uReg));
            AssertFailedReturn(VERR_DWARF_TODO);

        case DW_CFA_undefined:
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_undefined\n", uReg));
            return VERR_NOT_FOUND;

        case DW_CFA_same_value:
            if (pfnGetReg(pOldState, uReg, puValue))
            {
                Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_same_value: %#RX64\n", uReg, *puValue));
                return VINF_SUCCESS;
            }
            Log8(("rtDwarfUnwind_CalcRegisterValue(%#x): DW_CFA_same_value: VERR_NOT_FOUND\n", uReg));
            return VERR_NOT_FOUND;
    }
}


DECLINLINE(void) rtDwarfUnwind_UpdateX86GRegFromRow(PRTDBGUNWINDSTATE pState, PCRTDBGUNWINDSTATE pOldState, unsigned idxGReg,
                                                    PRTDWARFCFROW pRow, unsigned idxDwReg, PCRTDWARFCIEINFO pCie,
                                                    uint64_t uCfa, PFNDWARFUNWINDGEREGFROMSTATE pfnGetReg, uint8_t cbGReg)
{
    int rc = rtDwarfUnwind_CalcRegisterValue(pRow, idxDwReg, pCie, uCfa, pState, pOldState, pfnGetReg,
                                             &pState->u.x86.auRegs[idxGReg], cbGReg);
    if (RT_SUCCESS(rc))
        pState->u.x86.Loaded.s.fRegs |= RT_BIT_32(idxGReg);
}


DECLINLINE(void) rtDwarfUnwind_UpdateX86SRegFromRow(PRTDBGUNWINDSTATE pState, PCRTDBGUNWINDSTATE pOldState, unsigned idxSReg,
                                                    PRTDWARFCFROW pRow, unsigned idxDwReg, PCRTDWARFCIEINFO pCie,
                                                    uint64_t uCfa, PFNDWARFUNWINDGEREGFROMSTATE pfnGetReg)
{
    uint64_t uValue = pState->u.x86.auSegs[idxSReg];
    int rc = rtDwarfUnwind_CalcRegisterValue(pRow, idxDwReg, pCie, uCfa, pState, pOldState, pfnGetReg, &uValue, sizeof(uint16_t));
    if (RT_SUCCESS(rc))
    {
        pState->u.x86.auSegs[idxSReg] = (uint16_t)uValue;
        pState->u.x86.Loaded.s.fSegs |= RT_BIT_32(idxSReg);
    }
}


DECLINLINE(void) rtDwarfUnwind_UpdateX86RFlagsFromRow(PRTDBGUNWINDSTATE pState, PCRTDBGUNWINDSTATE pOldState,
                                                      PRTDWARFCFROW pRow, unsigned idxDwReg, PCRTDWARFCIEINFO pCie,
                                                      uint64_t uCfa, PFNDWARFUNWINDGEREGFROMSTATE pfnGetReg)
{
    int rc = rtDwarfUnwind_CalcRegisterValue(pRow, idxDwReg, pCie, uCfa, pState, pOldState, pfnGetReg,
                                             &pState->u.x86.uRFlags, sizeof(uint32_t));
    if (RT_SUCCESS(rc))
        pState->u.x86.Loaded.s.fRFlags = 1;
}


DECLINLINE(void) rtDwarfUnwind_UpdatePCFromRow(PRTDBGUNWINDSTATE pState, PCRTDBGUNWINDSTATE pOldState,
                                               PRTDWARFCFROW pRow, unsigned idxDwReg, PCRTDWARFCIEINFO pCie,
                                               uint64_t uCfa, PFNDWARFUNWINDGEREGFROMSTATE pfnGetReg, uint8_t cbPc)
{
    if (pCie->bRetReg != UINT8_MAX)
        idxDwReg = pCie->bRetReg;
    int rc = rtDwarfUnwind_CalcRegisterValue(pRow, idxDwReg, pCie, uCfa, pState, pOldState, pfnGetReg, &pState->uPc, cbPc);
    if (RT_SUCCESS(rc))
        pState->u.x86.Loaded.s.fPc = 1;
    else
    {
        rc = pState->pfnReadStack(pState, uCfa - cbPc, cbPc, &pState->uPc);
        if (RT_SUCCESS(rc))
            pState->u.x86.Loaded.s.fPc = 1;
    }
}



/**
 * Updates @a pState with the rules found in @a pRow.
 *
 * @returns IPRT status code.
 * @param   pState          The unwind state to update.
 * @param   pRow            The "row" in the dwarf unwind table.
 * @param   pCie            The CIE structure for the row.
 * @param   enmImageArch    The image architecture.
 */
static int rtDwarfUnwind_UpdateStateFromRow(PRTDBGUNWINDSTATE pState, PRTDWARFCFROW pRow,
                                            PCRTDWARFCIEINFO pCie, RTLDRARCH enmImageArch)
{
    /*
     * We need to make a copy of the current state so we can get at the
     * current register values while calculating the ones of the next frame.
     */
    RTDBGUNWINDSTATE const Old = *pState;

    /*
     * Get the register state getter.
     */
    PFNDWARFUNWINDGEREGFROMSTATE pfnGetReg;
    switch (enmImageArch)
    {
        case RTLDRARCH_AMD64:
            pfnGetReg = rtDwarfUnwind_Amd64GetRegFromState;
            break;
        case RTLDRARCH_X86_32:
        case RTLDRARCH_X86_16:
            pfnGetReg = rtDwarfUnwind_X86GetRegFromState;
            break;
        default:
            return VERR_NOT_SUPPORTED;
    }

    /*
     * Calc the canonical frame address for the current row.
     */
    AssertReturn(pRow->fCfaDefined, VERR_DBG_MALFORMED_UNWIND_INFO);
    uint64_t uCfa = 0;
    if (!pRow->pbCfaExprInstr)
    {
        pfnGetReg(&Old, pRow->uCfaBaseReg, &uCfa);
        uCfa += pRow->offCfaReg;
    }
    else
    {
        AssertFailed();
        return VERR_DWARF_TODO;
    }
    Log8(("rtDwarfUnwind_UpdateStateFromRow: uCfa=%RX64\n", uCfa));

    /*
     * Do the architecture specific register updating.
     */
    switch (enmImageArch)
    {
        case RTLDRARCH_AMD64:
            pState->enmRetType = RTDBGRETURNTYPE_NEAR64;
            pState->u.x86.FrameAddr.off       = uCfa - 8*2;
            pState->u.x86.Loaded.fAll         = 0;
            pState->u.x86.Loaded.s.fFrameAddr = 1;
            rtDwarfUnwind_UpdatePCFromRow(pState, &Old,                    pRow, DWREG_AMD64_RA,  pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86RFlagsFromRow(pState, &Old,             pRow, DWREG_AMD64_RFLAGS, pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xAX, pRow, DWREG_AMD64_RAX, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xCX, pRow, DWREG_AMD64_RCX, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xDX, pRow, DWREG_AMD64_RDX, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xBX, pRow, DWREG_AMD64_RBX, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xSP, pRow, DWREG_AMD64_RSP, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xBP, pRow, DWREG_AMD64_RBP, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xSI, pRow, DWREG_AMD64_RSI, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xDI, pRow, DWREG_AMD64_RDI, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_x8,  pRow, DWREG_AMD64_R8,  pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_x9,  pRow, DWREG_AMD64_R9,  pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_x10, pRow, DWREG_AMD64_R10, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_x11, pRow, DWREG_AMD64_R11, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_x12, pRow, DWREG_AMD64_R12, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_x13, pRow, DWREG_AMD64_R13, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_x14, pRow, DWREG_AMD64_R14, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_x15, pRow, DWREG_AMD64_R15, pCie, uCfa, pfnGetReg, sizeof(uint64_t));
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_ES,  pRow, DWREG_AMD64_ES,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_CS,  pRow, DWREG_AMD64_CS,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_SS,  pRow, DWREG_AMD64_SS,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_DS,  pRow, DWREG_AMD64_DS,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_FS,  pRow, DWREG_AMD64_FS,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_GS,  pRow, DWREG_AMD64_GS,  pCie, uCfa, pfnGetReg);
            break;

        case RTLDRARCH_X86_32:
        case RTLDRARCH_X86_16:
            pState->enmRetType = RTDBGRETURNTYPE_NEAR32;
            pState->u.x86.FrameAddr.off       = uCfa - 4*2;
            pState->u.x86.Loaded.fAll         = 0;
            pState->u.x86.Loaded.s.fFrameAddr = 1;
            rtDwarfUnwind_UpdatePCFromRow(pState, &Old,                    pRow, DWREG_X86_RA,  pCie, uCfa, pfnGetReg, sizeof(uint32_t));
            rtDwarfUnwind_UpdateX86RFlagsFromRow(pState, &Old,             pRow, DWREG_X86_EFLAGS, pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xAX, pRow, DWREG_X86_EAX, pCie, uCfa, pfnGetReg, sizeof(uint32_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xCX, pRow, DWREG_X86_ECX, pCie, uCfa, pfnGetReg, sizeof(uint32_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xDX, pRow, DWREG_X86_EDX, pCie, uCfa, pfnGetReg, sizeof(uint32_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xBX, pRow, DWREG_X86_EBX, pCie, uCfa, pfnGetReg, sizeof(uint32_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xSP, pRow, DWREG_X86_ESP, pCie, uCfa, pfnGetReg, sizeof(uint32_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xBP, pRow, DWREG_X86_EBP, pCie, uCfa, pfnGetReg, sizeof(uint32_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xSI, pRow, DWREG_X86_ESI, pCie, uCfa, pfnGetReg, sizeof(uint32_t));
            rtDwarfUnwind_UpdateX86GRegFromRow(pState, &Old, X86_GREG_xDI, pRow, DWREG_X86_EDI, pCie, uCfa, pfnGetReg, sizeof(uint32_t));
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_ES,  pRow, DWREG_X86_ES,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_CS,  pRow, DWREG_X86_CS,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_SS,  pRow, DWREG_X86_SS,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_DS,  pRow, DWREG_X86_DS,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_FS,  pRow, DWREG_X86_FS,  pCie, uCfa, pfnGetReg);
            rtDwarfUnwind_UpdateX86SRegFromRow(pState, &Old, X86_SREG_GS,  pRow, DWREG_X86_GS,  pCie, uCfa, pfnGetReg);
            if (pState->u.x86.Loaded.s.fRegs & RT_BIT_32(X86_GREG_xSP))
                pState->u.x86.FrameAddr.off = pState->u.x86.auRegs[X86_GREG_xSP] - 8;
            else
                pState->u.x86.FrameAddr.off = uCfa - 8;
            pState->u.x86.FrameAddr.sel = pState->u.x86.auSegs[X86_SREG_SS];
            if (pState->u.x86.Loaded.s.fSegs & RT_BIT_32(X86_SREG_CS))
            {
                if ((pState->uPc >> 16) == pState->u.x86.auSegs[X86_SREG_CS])
                {
                    pState->enmRetType = RTDBGRETURNTYPE_FAR16;
                    pState->uPc &= UINT16_MAX;
                    Log8(("rtDwarfUnwind_UpdateStateFromRow: Detected FAR16 return to %04x:%04RX64\n", pState->u.x86.auSegs[X86_SREG_CS], pState->uPc));
                }
                else
                {
                    pState->enmRetType = RTDBGRETURNTYPE_FAR32;
                    Log8(("rtDwarfUnwind_UpdateStateFromRow: CS loaded, assume far return.\n"));
                }
            }
            break;

        default:
            AssertFailedReturn(VERR_NOT_SUPPORTED);
    }

    return VINF_SUCCESS;
}


/**
 * Processes a FDE, taking over after the PC range field.
 *
 * @returns IPRT status code.
 * @param   pCursor         The cursor.
 * @param   pCie            Information about the corresponding CIE.
 * @param   uPcBegin        The PC begin field value (sans segment).
 * @param   cbPcRange       The PC range from @a uPcBegin.
 * @param   offInRange      The offset into the range corresponding to
 *                          pState->uPc.
 * @param   enmImageArch    The image architecture.
 * @param   pState          The unwind state to work.
 */
static int rtDwarfUnwind_ProcessFde(PRTDWARFCURSOR pCursor, PCRTDWARFCIEINFO pCie, uint64_t uPcBegin,
                                    uint64_t cbPcRange, uint64_t offInRange, RTLDRARCH enmImageArch, PRTDBGUNWINDSTATE pState)
{
    /*
     * Deal with augmented data fields.
     */
    /* The size. */
    size_t cbInstr = ~(size_t)0;
    if (pCie->fHasAugmentationSize)
    {
        uint64_t cbAugData = rtDwarfCursor_GetULeb128(pCursor, UINT64_MAX);
        if (RT_FAILURE(pCursor->rc))
            return pCursor->rc;
        if (cbAugData > pCursor->cbUnitLeft)
            return VERR_DBG_MALFORMED_UNWIND_INFO;
        cbInstr = pCursor->cbUnitLeft - cbAugData;
    }
    else if (pCie->fHasUnknowAugmentation)
        return VERR_DBG_MALFORMED_UNWIND_INFO;

    /* Parse the string and fetch FDE fields. */
    if (!pCie->fHasEhData)
        for (const char *pszAug = pCie->pszAugmentation; *pszAug != '\0'; pszAug++)
            switch (*pszAug)
            {
                case 'L':
                    if (pCie->bLsdaPtrEnc != DW_EH_PE_omit)
                        rtDwarfCursor_GetPtrEnc(pCursor, pCie->bLsdaPtrEnc, 0);
                    break;
            }

    /* Skip unconsumed bytes. */
    if (   cbInstr != ~(size_t)0
        && pCursor->cbUnitLeft > cbInstr)
        rtDwarfCursor_SkipBytes(pCursor, pCursor->cbUnitLeft - cbInstr);
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;

    /*
     * Now "execute" the programs till we've constructed the desired row.
     */
    RTDWARFCFROW            Row;
    RTDWARFCFEXEC           ExecState = { &Row, offInRange, 0, true /** @todo byte-order*/, pCie, uPcBegin, offInRange };
    RT_ZERO(Row);

    int rc = rtDwarfUnwind_Execute(&ExecState, pCie->pbInstructions, (uint32_t)pCie->cbInstructions);
    if (rc == VINF_TRY_AGAIN)
        rc = rtDwarfUnwind_Execute(&ExecState, pCursor->pb, (uint32_t)pCursor->cbUnitLeft);

    /* On success, extract whatever state we've got. */
    if (RT_SUCCESS(rc))
        rc = rtDwarfUnwind_UpdateStateFromRow(pState, &Row, pCie, enmImageArch);

    /*
     * Clean up allocations in case of pushes.
     */
    if (ExecState.pRow == &Row)
        Assert(!ExecState.pRow->fOnHeap);
    else
        do
        {
            PRTDWARFCFROW pPopped = ExecState.pRow;
            ExecState.pRow = ExecState.pRow->pNextOnStack;
            Assert(pPopped->fOnHeap);
            RTMemTmpFree(pPopped);
        } while (ExecState.pRow && ExecState.pRow != &Row);

    RT_NOREF(pState, uPcBegin, cbPcRange, offInRange);
    return rc;
}


/**
 * Load the information we need from a CIE.
 *
 * This starts after the initial length and CIE_pointer fields has
 * been processed.
 *
 * @returns IPRT status code.
 * @param   pCursor         The cursor.
 * @param   pNewCie         The structure to populate with parsed CIE info.
 * @param   offUnit         The unit offset.
 * @param   bDefaultPtrEnc  The default pointer encoding.
 */
static int rtDwarfUnwind_LoadCie(PRTDWARFCURSOR pCursor, PRTDWARFCIEINFO pNewCie, uint64_t offUnit, uint8_t bDefaultPtrEnc)
{
    /*
     * Initialize the CIE record and get the version.
     */
    RT_ZERO(*pNewCie);
    pNewCie->offCie         = offUnit;
    pNewCie->bLsdaPtrEnc    = DW_EH_PE_omit;
    pNewCie->bAddressPtrEnc = DW_EH_PE_omit; /* set later */
    pNewCie->uDwarfVer      = rtDwarfCursor_GetUByte(pCursor, 0);
    if (   pNewCie->uDwarfVer >= 1 /* Note! Some GCC versions may emit v1 here. */
        && pNewCie->uDwarfVer <= 5)
    { /* likely */ }
    else
    {
        Log(("rtDwarfUnwind_LoadCie(%RX64): uDwarfVer=%u: VERR_VERSION_MISMATCH\n", offUnit, pNewCie->uDwarfVer));
        return VERR_VERSION_MISMATCH;
    }

    /*
     * The augmentation string.
     *
     * First deal with special "eh" string from oldish GCC (dwarf2out.c about 1997), specified in LSB:
     *     https://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-PDA/LSB-PDA/ehframechpt.html
     */
    pNewCie->pszAugmentation = rtDwarfCursor_GetSZ(pCursor, "");
    if (   pNewCie->pszAugmentation[0] == 'e'
        && pNewCie->pszAugmentation[1] == 'h'
        && pNewCie->pszAugmentation[2] == '\0')
    {
        pNewCie->fHasEhData = true;
        rtDwarfCursor_GetPtrEnc(pCursor, bDefaultPtrEnc, 0);
    }
    else
    {
        /* Regular augmentation string. */
        for (const char *pszAug = pNewCie->pszAugmentation; *pszAug != '\0'; pszAug++)
            switch (*pszAug)
            {
                case 'z':
                    pNewCie->fHasAugmentationSize = true;
                    break;
                case 'L':
                    pNewCie->fHasLanguageSpecificDataArea = true;
                    break;
                case 'P':
                    pNewCie->fHasPersonalityRoutine = true;
                    break;
                case 'R':
                    pNewCie->fHasAddressEnc = true;
                    break;
                case 'S':
                    pNewCie->fIsSignalFrame = true;
                    break;
                default:
                    pNewCie->fHasUnknowAugmentation = true;
                    break;
            }
    }

    /*
     * More standard fields
     */
    uint8_t cbAddress = 0;
    if (pNewCie->uDwarfVer >= 4)
    {
        cbAddress = rtDwarfCursor_GetU8(pCursor, bDefaultPtrEnc == DW_EH_PE_udata8 ? 8 : 4);
        pNewCie->cbSegment = rtDwarfCursor_GetU8(pCursor, 0);
    }
    pNewCie->uCodeAlignFactor = rtDwarfCursor_GetULeb128(pCursor, 1);
    pNewCie->iDataAlignFactor = rtDwarfCursor_GetSLeb128(pCursor, 1);
    pNewCie->bRetReg          = rtDwarfCursor_GetU8(pCursor, UINT8_MAX);

    /*
     * Augmentation data.
     */
    if (!pNewCie->fHasEhData)
    {
        /* The size. */
        size_t cbInstr = ~(size_t)0;
        if (pNewCie->fHasAugmentationSize)
        {
            uint64_t cbAugData = rtDwarfCursor_GetULeb128(pCursor, UINT64_MAX);
            if (RT_FAILURE(pCursor->rc))
            {
                Log(("rtDwarfUnwind_LoadCie(%#RX64): rtDwarfCursor_GetULeb128 -> %Rrc!\n", offUnit, pCursor->rc));
                return pCursor->rc;
            }
            if (cbAugData > pCursor->cbUnitLeft)
            {
                Log(("rtDwarfUnwind_LoadCie(%#RX64): cbAugData=%#x pCursor->cbUnitLeft=%#x -> VERR_DBG_MALFORMED_UNWIND_INFO!\n", offUnit, cbAugData, pCursor->cbUnitLeft));
                return VERR_DBG_MALFORMED_UNWIND_INFO;
            }
            cbInstr = pCursor->cbUnitLeft - cbAugData;
        }
        else if (pNewCie->fHasUnknowAugmentation)
        {
            Log(("rtDwarfUnwind_LoadCie(%#RX64): fHasUnknowAugmentation=1 -> VERR_DBG_MALFORMED_UNWIND_INFO!\n", offUnit));
            return VERR_DBG_MALFORMED_UNWIND_INFO;
        }

        /* Parse the string. */
        for (const char *pszAug = pNewCie->pszAugmentation; *pszAug != '\0'; pszAug++)
            switch (*pszAug)
            {
                case 'L':
                    pNewCie->bLsdaPtrEnc = rtDwarfCursor_GetU8(pCursor, DW_EH_PE_omit);
                    break;
                case 'P':
                    rtDwarfCursor_GetPtrEnc(pCursor, rtDwarfCursor_GetU8(pCursor, DW_EH_PE_omit), 0);
                    break;
                case 'R':
                    pNewCie->bAddressPtrEnc = rtDwarfCursor_GetU8(pCursor, DW_EH_PE_omit);
                    break;
            }

        /* Skip unconsumed bytes. */
        if (   cbInstr != ~(size_t)0
            && pCursor->cbUnitLeft > cbInstr)
            rtDwarfCursor_SkipBytes(pCursor, pCursor->cbUnitLeft - cbInstr);
    }

    /*
     * Note down where the instructions are.
     */
    pNewCie->pbInstructions = pCursor->pb;
    pNewCie->cbInstructions = pCursor->cbUnitLeft;

    /*
     * Determine the target address encoding.  Make sure we resolve DW_EH_PE_ptr.
     */
    if (pNewCie->bAddressPtrEnc == DW_EH_PE_omit)
        switch (cbAddress)
        {
            case 2:     pNewCie->bAddressPtrEnc = DW_EH_PE_udata2; break;
            case 4:     pNewCie->bAddressPtrEnc = DW_EH_PE_udata4; break;
            case 8:     pNewCie->bAddressPtrEnc = DW_EH_PE_udata8; break;
            default:    pNewCie->bAddressPtrEnc = bDefaultPtrEnc;  break;
        }
    else if ((pNewCie->bAddressPtrEnc & DW_EH_PE_FORMAT_MASK) == DW_EH_PE_ptr)
        pNewCie->bAddressPtrEnc = bDefaultPtrEnc;

    return VINF_SUCCESS;
}


/**
 * Does a slow unwind of a '.debug_frame' or '.eh_frame' section.
 *
 * @returns IPRT status code.
 * @param   pCursor         The cursor.
 * @param   uRvaCursor      The RVA corrsponding to the cursor start location.
 * @param   idxSeg          The segment of the PC location.
 * @param   offSeg          The segment offset of the PC location.
 * @param   uRva            The RVA of the PC location.
 * @param   pState          The unwind state to work.
 * @param   bDefaultPtrEnc  The default pointer encoding.
 * @param   fIsEhFrame      Set if this is a '.eh_frame'.  GCC generate these
 *                          with different CIE_pointer values.
 * @param   enmImageArch    The image architecture.
 */
static int rtDwarfUnwind_Slow(PRTDWARFCURSOR pCursor, RTUINTPTR uRvaCursor,
                              RTDBGSEGIDX idxSeg, RTUINTPTR offSeg, RTUINTPTR uRva,
                              PRTDBGUNWINDSTATE pState, uint8_t bDefaultPtrEnc, bool fIsEhFrame, RTLDRARCH enmImageArch)
{
    Log8(("rtDwarfUnwind_Slow: idxSeg=%#x offSeg=%RTptr uRva=%RTptr enmArch=%d PC=%#RX64\n", idxSeg, offSeg, uRva, pState->enmArch, pState->uPc));

    /*
     * CIE info we collect.
     */
    PRTDWARFCIEINFO paCies   = NULL;
    uint32_t        cCies    = 0;
    PRTDWARFCIEINFO pCieHint = NULL;

    /*
     * Do the scanning.
     */
    uint64_t const offCieOffset = pCursor->f64bitDwarf ? UINT64_MAX : UINT32_MAX;
    int rc = VERR_DBG_UNWIND_INFO_NOT_FOUND;
    while (!rtDwarfCursor_IsAtEnd(pCursor))
    {
        uint64_t const offUnit = rtDwarfCursor_CalcSectOffsetU32(pCursor);
        if (rtDwarfCursor_GetInitialLength(pCursor) == 0)
            break;

        uint64_t const offRelCie = rtDwarfCursor_GetUOff(pCursor, offCieOffset);
        if (offRelCie != offCieOffset)
        {
            /*
             * Frame descriptor entry (FDE).
             */
            /* Locate the corresponding CIE.  The CIE pointer is self relative
               in .eh_frame and section relative in .debug_frame. */
            PRTDWARFCIEINFO pCieForFde;
            uint64_t offCie = fIsEhFrame ? offUnit + 4 - offRelCie : offRelCie;
            if (pCieHint && pCieHint->offCie == offCie)
                pCieForFde = pCieHint;
            else
            {
                pCieForFde = NULL;
                uint32_t i = cCies;
                while (i-- > 0)
                    if (paCies[i].offCie == offCie)
                    {
                        pCieHint = pCieForFde = &paCies[i];
                        break;
                    }
            }
            if (pCieForFde)
            {
                /* Read the PC range covered by this FDE (the fields are also known as initial_location). */
                RTDBGSEGIDX idxFdeSeg = RTDBGSEGIDX_RVA;
                if (pCieForFde->cbSegment)
                    idxFdeSeg = rtDwarfCursor_GetVarSizedU(pCursor, pCieForFde->cbSegment, RTDBGSEGIDX_RVA);
                uint64_t uPcBegin;
                switch (pCieForFde->bAddressPtrEnc & DW_EH_PE_APPL_MASK)
                {
                    default: AssertFailed();
                        RT_FALL_THRU();
                    case DW_EH_PE_absptr:
                        uPcBegin = rtDwarfCursor_GetPtrEnc(pCursor, pCieForFde->bAddressPtrEnc, 0);
                        break;
                    case DW_EH_PE_pcrel:
                    {
                        uPcBegin = rtDwarfCursor_CalcSectOffsetU32(pCursor) + uRvaCursor;
                        uPcBegin += rtDwarfCursor_GetPtrEnc(pCursor, pCieForFde->bAddressPtrEnc, 0);
                        break;
                    }
                }
                uint64_t cbPcRange = rtDwarfCursor_GetPtrEnc(pCursor, pCieForFde->bAddressPtrEnc, 0);

                /* Match it with what we're looking for. */
                bool fMatch = idxFdeSeg == RTDBGSEGIDX_RVA
                            ? uRva - uPcBegin < cbPcRange
                            : idxSeg == idxFdeSeg && offSeg - uPcBegin < cbPcRange;
                Log8(("%#08RX64: FDE pCie=%p idxFdeSeg=%#x uPcBegin=%#RX64 cbPcRange=%#x fMatch=%d\n",
                      offUnit, pCieForFde, idxFdeSeg, uPcBegin, cbPcRange, fMatch));
                if (fMatch)
                {
                    rc = rtDwarfUnwind_ProcessFde(pCursor, pCieForFde, uPcBegin, cbPcRange,
                                                  idxFdeSeg == RTDBGSEGIDX_RVA ? uRva - uPcBegin : offSeg - uPcBegin,
                                                  enmImageArch, pState);
                    break;
                }
            }
            else
                Log8(("%#08RX64: FDE -  pCie=NULL!!  offCie=%#RX64 offRelCie=%#RX64 fIsEhFrame=%d\n", offUnit, offCie, offRelCie, fIsEhFrame));
        }
        else
        {
            /*
             * Common information entry (CIE).  Record the info we need about it.
             */
            if ((cCies % 8) == 0)
            {
                void *pvNew = RTMemRealloc(paCies, sizeof(paCies[0]) * (cCies + 8));
                if (pvNew)
                {
                    paCies = (PRTDWARFCIEINFO)pvNew;
                    pCieHint = NULL;
                }
                else
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }
            }
            Log8(("%#08RX64: CIE\n", offUnit));
            int rc2 = rtDwarfUnwind_LoadCie(pCursor, &paCies[cCies], offUnit, bDefaultPtrEnc);
            if (RT_SUCCESS(rc2))
            {
                Log8(("%#08RX64: CIE #%u: offCie=%#RX64\n", offUnit, cCies, paCies[cCies].offCie));
                cCies++;
            }
        }
        rtDwarfCursor_SkipUnit(pCursor);
    }

    /*
     * Cleanup.
     */
    if (paCies)
        RTMemFree(paCies);
    Log8(("rtDwarfUnwind_Slow: returns %Rrc PC=%#RX64\n", rc, pState->uPc));
    return rc;
}


/**
 * Helper for translating a loader architecture value to a pointe encoding.
 *
 * @returns Pointer encoding.
 * @param   enmLdrArch          The loader architecture value to convert.
 */
static uint8_t rtDwarfUnwind_ArchToPtrEnc(RTLDRARCH enmLdrArch)
{
    switch (enmLdrArch)
    {
        case RTLDRARCH_AMD64:
        case RTLDRARCH_ARM64:
            return DW_EH_PE_udata8;
        case RTLDRARCH_X86_16:
        case RTLDRARCH_X86_32:
        case RTLDRARCH_ARM32:
            return DW_EH_PE_udata4;
        case RTLDRARCH_HOST:
        case RTLDRARCH_WHATEVER:
        case RTLDRARCH_INVALID:
        case RTLDRARCH_END:
        case RTLDRARCH_32BIT_HACK:
            break;
    }
    AssertFailed();
    return DW_EH_PE_udata4;
}


/**
 * Interface for the loader code.
 *
 * @returns IPRT status.
 * @param   pvSection       The '.eh_frame' section data.
 * @param   cbSection       The size of the '.eh_frame' section data.
 * @param   uRvaSection     The RVA of the '.eh_frame' section.
 * @param   idxSeg          The segment of the PC location.
 * @param   offSeg          The segment offset of the PC location.
 * @param   uRva            The RVA of the PC location.
 * @param   pState          The unwind state to work.
 * @param   enmArch         The image architecture.
 */
DECLHIDDEN(int) rtDwarfUnwind_EhData(void const *pvSection, size_t cbSection, RTUINTPTR uRvaSection,
                                     RTDBGSEGIDX idxSeg, RTUINTPTR offSeg, RTUINTPTR uRva,
                                     PRTDBGUNWINDSTATE pState, RTLDRARCH enmArch)
{
    RTDWARFCURSOR Cursor;
    rtDwarfCursor_InitForMem(&Cursor, pvSection, cbSection);
    int rc = rtDwarfUnwind_Slow(&Cursor, uRvaSection, idxSeg, offSeg, uRva, pState,
                                rtDwarfUnwind_ArchToPtrEnc(enmArch), true /*fIsEhFrame*/, enmArch);
    LogFlow(("rtDwarfUnwind_EhData: rtDwarfUnwind_Slow -> %Rrc\n", rc));
    rc = rtDwarfCursor_Delete(&Cursor, rc);
    LogFlow(("rtDwarfUnwind_EhData: returns %Rrc\n", rc));
    return rc;
}


/*
 *
 * DWARF Line Numbers.
 * DWARF Line Numbers.
 * DWARF Line Numbers.
 *
 */


/**
 * Defines a file name.
 *
 * @returns IPRT status code.
 * @param   pLnState            The line number program state.
 * @param   pszFilename         The name of the file.
 * @param   idxInc              The include path index.
 */
static int rtDwarfLine_DefineFileName(PRTDWARFLINESTATE pLnState, const char *pszFilename, uint64_t idxInc)
{
    /*
     * Resize the array if necessary.
     */
    uint32_t iFileName = pLnState->cFileNames;
    if ((iFileName % 2) == 0)
    {
        void *pv = RTMemRealloc(pLnState->papszFileNames, sizeof(pLnState->papszFileNames[0]) * (iFileName + 2));
        if (!pv)
            return VERR_NO_MEMORY;
        pLnState->papszFileNames = (char **)pv;
    }

    /*
     * Add the file name.
     */
    if (   pszFilename[0] == '/'
        || pszFilename[0] == '\\'
        || (RT_C_IS_ALPHA(pszFilename[0]) && pszFilename[1] == ':') )
        pLnState->papszFileNames[iFileName] = RTStrDup(pszFilename);
    else if (idxInc < pLnState->cIncPaths)
        pLnState->papszFileNames[iFileName] = RTPathJoinA(pLnState->papszIncPaths[idxInc], pszFilename);
    else
        return VERR_DWARF_BAD_LINE_NUMBER_HEADER;
    if (!pLnState->papszFileNames[iFileName])
        return VERR_NO_STR_MEMORY;
    pLnState->cFileNames = iFileName + 1;

    /*
     * Sanitize the name.
     */
    int rc = rtDbgModDwarfStringToUtf8(pLnState->pDwarfMod, &pLnState->papszFileNames[iFileName]);
    Log(("  File #%02u = '%s'\n", iFileName, pLnState->papszFileNames[iFileName]));
    return rc;
}


/**
 * Adds a line to the table and resets parts of the state (DW_LNS_copy).
 *
 * @returns IPRT status code
 * @param   pLnState            The line number program state.
 * @param   offOpCode           The opcode offset (for logging
 *                              purposes).
 */
static int rtDwarfLine_AddLine(PRTDWARFLINESTATE pLnState, uint32_t offOpCode)
{
    PRTDBGMODDWARF  pThis = pLnState->pDwarfMod;
    int             rc;
    if (pThis->iWatcomPass == 1)
        rc = rtDbgModDwarfRecordSegOffset(pThis, pLnState->Regs.uSegment, pLnState->Regs.uAddress + 1);
    else
    {
        const char *pszFile = pLnState->Regs.iFile < pLnState->cFileNames
                            ? pLnState->papszFileNames[pLnState->Regs.iFile]
                            : "<bad file name index>";
        NOREF(offOpCode);

        RTDBGSEGIDX iSeg;
        RTUINTPTR   offSeg;
        rc = rtDbgModDwarfLinkAddressToSegOffset(pLnState->pDwarfMod, pLnState->Regs.uSegment, pLnState->Regs.uAddress,
                                                 &iSeg, &offSeg); /*AssertRC(rc);*/
        if (RT_SUCCESS(rc))
        {
            Log2(("rtDwarfLine_AddLine: %x:%08llx (%#llx) %s(%d) [offOpCode=%08x]\n", iSeg, offSeg, pLnState->Regs.uAddress, pszFile, pLnState->Regs.uLine, offOpCode));
            rc = RTDbgModLineAdd(pLnState->pDwarfMod->hCnt, pszFile, pLnState->Regs.uLine, iSeg, offSeg, NULL);

            /* Ignore address conflicts for now. */
            if (rc == VERR_DBG_ADDRESS_CONFLICT)
                rc = VINF_SUCCESS;
        }
        else
            rc = VINF_SUCCESS; /* ignore failure */
    }

    pLnState->Regs.fBasicBlock    = false;
    pLnState->Regs.fPrologueEnd   = false;
    pLnState->Regs.fEpilogueBegin = false;
    pLnState->Regs.uDiscriminator = 0;
    return rc;
}


/**
 * Reset the program to the start-of-sequence state.
 *
 * @param   pLnState            The line number program state.
 */
static void rtDwarfLine_ResetState(PRTDWARFLINESTATE pLnState)
{
    pLnState->Regs.uAddress         = 0;
    pLnState->Regs.idxOp            = 0;
    pLnState->Regs.iFile            = 1;
    pLnState->Regs.uLine            = 1;
    pLnState->Regs.uColumn          = 0;
    pLnState->Regs.fIsStatement     = RT_BOOL(pLnState->Hdr.u8DefIsStmt);
    pLnState->Regs.fBasicBlock      = false;
    pLnState->Regs.fEndSequence     = false;
    pLnState->Regs.fPrologueEnd     = false;
    pLnState->Regs.fEpilogueBegin   = false;
    pLnState->Regs.uIsa             = 0;
    pLnState->Regs.uDiscriminator   = 0;
    pLnState->Regs.uSegment         = 0;
}


/**
 * Runs the line number program.
 *
 * @returns IPRT status code.
 * @param   pLnState            The line number program state.
 * @param   pCursor             The cursor.
 */
static int rtDwarfLine_RunProgram(PRTDWARFLINESTATE pLnState, PRTDWARFCURSOR pCursor)
{
    LogFlow(("rtDwarfLine_RunProgram: cbUnitLeft=%zu\n", pCursor->cbUnitLeft));

    int rc = VINF_SUCCESS;
    rtDwarfLine_ResetState(pLnState);

    while (!rtDwarfCursor_IsAtEndOfUnit(pCursor))
    {
#ifdef LOG_ENABLED
        uint32_t const offOpCode = rtDwarfCursor_CalcSectOffsetU32(pCursor);
#else
        uint32_t const offOpCode = 0;
#endif
        uint8_t        bOpCode   = rtDwarfCursor_GetUByte(pCursor, DW_LNS_extended);
        if (bOpCode >= pLnState->Hdr.u8OpcodeBase)
        {
            /*
             * Special opcode.
             */
            uint8_t const bLogOpCode = bOpCode; NOREF(bLogOpCode);
            bOpCode -= pLnState->Hdr.u8OpcodeBase;

            int32_t const cLineDelta = bOpCode % pLnState->Hdr.u8LineRange + (int32_t)pLnState->Hdr.s8LineBase;
            bOpCode /= pLnState->Hdr.u8LineRange;

            uint64_t uTmp = bOpCode + pLnState->Regs.idxOp;
            uint64_t const cAddressDelta = uTmp / pLnState->Hdr.cMaxOpsPerInstr * pLnState->Hdr.cbMinInstr;
            uint64_t const cOpIndexDelta = uTmp % pLnState->Hdr.cMaxOpsPerInstr;

            pLnState->Regs.uLine    += cLineDelta;
            pLnState->Regs.uAddress += cAddressDelta;
            pLnState->Regs.idxOp    += cOpIndexDelta;
            Log2(("%08x: DW Special Opcode %#04x: uLine + %d => %u; uAddress + %#llx => %#llx; idxOp + %#llx => %#llx\n",
                  offOpCode, bLogOpCode, cLineDelta, pLnState->Regs.uLine, cAddressDelta, pLnState->Regs.uAddress,
                  cOpIndexDelta, pLnState->Regs.idxOp));

            /*
             * LLVM emits debug info for global constructors (_GLOBAL__I_a) which are not part of source
             * code but are inserted by the compiler: The resulting line number will be 0
             * because they are not part of the source file obviously (see https://reviews.llvm.org/rL205999),
             * so skip adding them when they are encountered.
             */
            if (pLnState->Regs.uLine)
                rc = rtDwarfLine_AddLine(pLnState, offOpCode);
        }
        else
        {
            switch (bOpCode)
            {
                /*
                 * Standard opcode.
                 */
                case DW_LNS_copy:
                    Log2(("%08x: DW_LNS_copy\n", offOpCode));
                    /* See the comment about LLVM above. */
                    if (pLnState->Regs.uLine)
                        rc = rtDwarfLine_AddLine(pLnState, offOpCode);
                    break;

                case DW_LNS_advance_pc:
                {
                    uint64_t u64Adv = rtDwarfCursor_GetULeb128(pCursor, 0);
                    pLnState->Regs.uAddress += (pLnState->Regs.idxOp + u64Adv) / pLnState->Hdr.cMaxOpsPerInstr
                                             * pLnState->Hdr.cbMinInstr;
                    pLnState->Regs.idxOp    += (pLnState->Regs.idxOp + u64Adv) % pLnState->Hdr.cMaxOpsPerInstr;
                    Log2(("%08x: DW_LNS_advance_pc: u64Adv=%#llx (%lld) )\n", offOpCode, u64Adv, u64Adv));
                    break;
                }

                case DW_LNS_advance_line:
                {
                    int32_t cLineDelta = rtDwarfCursor_GetSLeb128AsS32(pCursor, 0);
                    pLnState->Regs.uLine += cLineDelta;
                    Log2(("%08x: DW_LNS_advance_line: uLine + %d => %u\n", offOpCode, cLineDelta, pLnState->Regs.uLine));
                    break;
                }

                case DW_LNS_set_file:
                    pLnState->Regs.iFile = rtDwarfCursor_GetULeb128AsU32(pCursor, 0);
                    Log2(("%08x: DW_LNS_set_file: iFile=%u\n", offOpCode, pLnState->Regs.iFile));
                    break;

                case DW_LNS_set_column:
                    pLnState->Regs.uColumn = rtDwarfCursor_GetULeb128AsU32(pCursor, 0);
                    Log2(("%08x: DW_LNS_set_column\n", offOpCode));
                    break;

                case DW_LNS_negate_stmt:
                    pLnState->Regs.fIsStatement = !pLnState->Regs.fIsStatement;
                    Log2(("%08x: DW_LNS_negate_stmt\n", offOpCode));
                    break;

                case DW_LNS_set_basic_block:
                    pLnState->Regs.fBasicBlock = true;
                    Log2(("%08x: DW_LNS_set_basic_block\n", offOpCode));
                    break;

                case DW_LNS_const_add_pc:
                {
                    uint8_t u8Adv = (255 - pLnState->Hdr.u8OpcodeBase) / pLnState->Hdr.u8LineRange;
                    if (pLnState->Hdr.cMaxOpsPerInstr <= 1)
                        pLnState->Regs.uAddress += (uint32_t)pLnState->Hdr.cbMinInstr * u8Adv;
                    else
                    {
                        pLnState->Regs.uAddress += (pLnState->Regs.idxOp + u8Adv) / pLnState->Hdr.cMaxOpsPerInstr
                                                 * pLnState->Hdr.cbMinInstr;
                        pLnState->Regs.idxOp     = (pLnState->Regs.idxOp + u8Adv) % pLnState->Hdr.cMaxOpsPerInstr;
                    }
                    Log2(("%08x: DW_LNS_const_add_pc\n", offOpCode));
                    break;
                }
                case DW_LNS_fixed_advance_pc:
                    pLnState->Regs.uAddress += rtDwarfCursor_GetUHalf(pCursor, 0);
                    pLnState->Regs.idxOp     = 0;
                    Log2(("%08x: DW_LNS_fixed_advance_pc\n", offOpCode));
                    break;

                case DW_LNS_set_prologue_end:
                    pLnState->Regs.fPrologueEnd = true;
                    Log2(("%08x: DW_LNS_set_prologue_end\n", offOpCode));
                    break;

                case DW_LNS_set_epilogue_begin:
                    pLnState->Regs.fEpilogueBegin = true;
                    Log2(("%08x: DW_LNS_set_epilogue_begin\n", offOpCode));
                    break;

                case DW_LNS_set_isa:
                    pLnState->Regs.uIsa = rtDwarfCursor_GetULeb128AsU32(pCursor, 0);
                    Log2(("%08x: DW_LNS_set_isa %#x\n", offOpCode, pLnState->Regs.uIsa));
                    break;

                default:
                {
                    unsigned cOpsToSkip = pLnState->Hdr.pacStdOperands[bOpCode - 1];
                    Log(("rtDwarfLine_RunProgram: Unknown standard opcode %#x, %#x operands, at %08x.\n", bOpCode, cOpsToSkip, offOpCode));
                    while (cOpsToSkip-- > 0)
                        rc = rtDwarfCursor_SkipLeb128(pCursor);
                    break;
                }

                /*
                 * Extended opcode.
                 */
                case DW_LNS_extended:
                {
                    /* The instruction has a length prefix. */
                    uint64_t cbInstr = rtDwarfCursor_GetULeb128(pCursor, UINT64_MAX);
                    if (RT_FAILURE(pCursor->rc))
                        return pCursor->rc;
                    if (cbInstr > pCursor->cbUnitLeft)
                        return VERR_DWARF_BAD_LNE;
                    uint8_t const * const pbEndOfInstr = rtDwarfCursor_CalcPos(pCursor, cbInstr);

                    /* Get the opcode and deal with it if we know it. */
                    bOpCode = rtDwarfCursor_GetUByte(pCursor, 0);
                    switch (bOpCode)
                    {
                        case DW_LNE_end_sequence:
#if 0 /* No need for this, I think. */
                            pLnState->Regs.fEndSequence = true;
                            rc = rtDwarfLine_AddLine(pLnState, offOpCode);
#endif
                            rtDwarfLine_ResetState(pLnState);
                            Log2(("%08x: DW_LNE_end_sequence\n", offOpCode));
                            break;

                        case DW_LNE_set_address:
                            pLnState->Regs.uAddress = rtDwarfCursor_GetVarSizedU(pCursor, cbInstr - 1, UINT64_MAX);
                            pLnState->Regs.idxOp    = 0;
                            Log2(("%08x: DW_LNE_set_address: %#llx\n", offOpCode, pLnState->Regs.uAddress));
                            break;

                        case DW_LNE_define_file:
                        {
                            const char *pszFilename = rtDwarfCursor_GetSZ(pCursor, NULL);
                            uint32_t    idxInc      = rtDwarfCursor_GetULeb128AsU32(pCursor, UINT32_MAX);
                            rtDwarfCursor_SkipLeb128(pCursor); /* st_mtime */
                            rtDwarfCursor_SkipLeb128(pCursor); /* st_size */
                            Log2(("%08x: DW_LNE_define_file: {%d}/%s\n", offOpCode, idxInc, pszFilename));

                            rc = rtDwarfCursor_AdvanceToPos(pCursor, pbEndOfInstr);
                            if (RT_SUCCESS(rc))
                                rc = rtDwarfLine_DefineFileName(pLnState, pszFilename, idxInc);
                            break;
                        }

                        /*
                         * Note! Was defined in DWARF 4.  But... Watcom used it for setting the
                         *       segment in DWARF 2, creating an incompatibility with the newer
                         *       standard.  And gcc 10 uses v3 for these.
                         */
                        case DW_LNE_set_descriminator:
                            if (pLnState->Hdr.uVer != 2)
                            {
                                Assert(pLnState->Hdr.uVer >= 3);
                                pLnState->Regs.uDiscriminator = rtDwarfCursor_GetULeb128AsU32(pCursor, UINT32_MAX);
                                Log2(("%08x: DW_LNE_set_descriminator: %u\n", offOpCode, pLnState->Regs.uDiscriminator));
                            }
                            else
                            {
                                uint64_t uSeg = rtDwarfCursor_GetVarSizedU(pCursor, cbInstr - 1, UINT64_MAX);
                                Log2(("%08x: DW_LNE_set_segment: %#llx, cbInstr=%#x - Watcom Extension\n", offOpCode, uSeg, cbInstr));
                                pLnState->Regs.uSegment = (RTSEL)uSeg;
                                AssertStmt(pLnState->Regs.uSegment == uSeg, rc = VERR_DWARF_BAD_INFO);
                            }
                            break;

                        default:
                            Log(("rtDwarfLine_RunProgram: Unknown extended opcode %#x, length %#x at %08x\n", bOpCode, cbInstr, offOpCode));
                            break;
                    }

                    /* Advance the cursor to the end of the instruction . */
                    rtDwarfCursor_AdvanceToPos(pCursor, pbEndOfInstr);
                    break;
                }
            }
        }

        /*
         * Check the status before looping.
         */
        if (RT_FAILURE(rc))
            return rc;
        if (RT_FAILURE(pCursor->rc))
            return pCursor->rc;
    }
    return rc;
}


/**
 * Reads the include directories for a line number unit.
 *
 * @returns IPRT status code
 * @param   pLnState            The line number program state.
 * @param   pCursor             The cursor.
 */
static int rtDwarfLine_ReadFileNames(PRTDWARFLINESTATE pLnState, PRTDWARFCURSOR pCursor)
{
    int rc = rtDwarfLine_DefineFileName(pLnState, "/<bad-zero-file-name-entry>", 0);
    if (RT_FAILURE(rc))
        return rc;

    for (;;)
    {
        const char *psz = rtDwarfCursor_GetSZ(pCursor, NULL);
        if (!*psz)
            break;

        uint64_t idxInc = rtDwarfCursor_GetULeb128(pCursor, UINT64_MAX);
        rtDwarfCursor_SkipLeb128(pCursor); /* st_mtime */
        rtDwarfCursor_SkipLeb128(pCursor); /* st_size */

        rc = rtDwarfLine_DefineFileName(pLnState, psz, idxInc);
        if (RT_FAILURE(rc))
            return rc;
    }
    return pCursor->rc;
}


/**
 * Reads the include directories for a line number unit.
 *
 * @returns IPRT status code
 * @param   pLnState            The line number program state.
 * @param   pCursor             The cursor.
 */
static int rtDwarfLine_ReadIncludePaths(PRTDWARFLINESTATE pLnState, PRTDWARFCURSOR pCursor)
{
    const char *psz = "";   /* The zeroth is the unit dir. */
    for (;;)
    {
        if ((pLnState->cIncPaths % 2) == 0)
        {
            void *pv = RTMemRealloc(pLnState->papszIncPaths, sizeof(pLnState->papszIncPaths[0]) * (pLnState->cIncPaths + 2));
            if (!pv)
                return VERR_NO_MEMORY;
            pLnState->papszIncPaths = (const char **)pv;
        }
        Log(("  Path #%02u = '%s'\n", pLnState->cIncPaths, psz));
        pLnState->papszIncPaths[pLnState->cIncPaths] = psz;
        pLnState->cIncPaths++;

        psz = rtDwarfCursor_GetSZ(pCursor, NULL);
        if (!*psz)
            break;
    }

    return pCursor->rc;
}


/**
 * Explodes the line number table for a compilation unit.
 *
 * @returns IPRT status code
 * @param   pThis               The DWARF instance.
 * @param   pCursor             The cursor to read the line number information
 *                              via.
 */
static int rtDwarfLine_ExplodeUnit(PRTDBGMODDWARF pThis, PRTDWARFCURSOR pCursor)
{
    RTDWARFLINESTATE LnState;
    RT_ZERO(LnState);
    LnState.pDwarfMod = pThis;

    /*
     * Parse the header.
     */
    rtDwarfCursor_GetInitialLength(pCursor);
    LnState.Hdr.uVer           = rtDwarfCursor_GetUHalf(pCursor, 0);
    if (   LnState.Hdr.uVer < 2
        || LnState.Hdr.uVer > 4)
        return rtDwarfCursor_SkipUnit(pCursor);

    LnState.Hdr.offFirstOpcode = rtDwarfCursor_GetUOff(pCursor, 0);
    uint8_t const * const pbFirstOpcode = rtDwarfCursor_CalcPos(pCursor, LnState.Hdr.offFirstOpcode);

    LnState.Hdr.cbMinInstr     = rtDwarfCursor_GetUByte(pCursor, 0);
    if (LnState.Hdr.uVer >= 4)
        LnState.Hdr.cMaxOpsPerInstr = rtDwarfCursor_GetUByte(pCursor, 0);
    else
        LnState.Hdr.cMaxOpsPerInstr = 1;
    LnState.Hdr.u8DefIsStmt    = rtDwarfCursor_GetUByte(pCursor, 0);
    LnState.Hdr.s8LineBase     = rtDwarfCursor_GetSByte(pCursor, 0);
    LnState.Hdr.u8LineRange    = rtDwarfCursor_GetUByte(pCursor, 0);
    LnState.Hdr.u8OpcodeBase   = rtDwarfCursor_GetUByte(pCursor, 0);

    if (   !LnState.Hdr.u8OpcodeBase
        || !LnState.Hdr.cMaxOpsPerInstr
        || !LnState.Hdr.u8LineRange
        || LnState.Hdr.u8DefIsStmt > 1)
        return VERR_DWARF_BAD_LINE_NUMBER_HEADER;
    Log2(("DWARF Line number header:\n"
          "    uVer             %d\n"
          "    offFirstOpcode   %#llx\n"
          "    cbMinInstr       %u\n"
          "    cMaxOpsPerInstr  %u\n"
          "    u8DefIsStmt      %u\n"
          "    s8LineBase       %d\n"
          "    u8LineRange      %u\n"
          "    u8OpcodeBase     %u\n",
          LnState.Hdr.uVer,    LnState.Hdr.offFirstOpcode, LnState.Hdr.cbMinInstr,  LnState.Hdr.cMaxOpsPerInstr,
          LnState.Hdr.u8DefIsStmt, LnState.Hdr.s8LineBase, LnState.Hdr.u8LineRange, LnState.Hdr.u8OpcodeBase));

    LnState.Hdr.pacStdOperands = pCursor->pb;
    for (uint8_t iStdOpcode = 1; iStdOpcode < LnState.Hdr.u8OpcodeBase; iStdOpcode++)
        rtDwarfCursor_GetUByte(pCursor, 0);

    int rc = pCursor->rc;
    if (RT_SUCCESS(rc))
        rc = rtDwarfLine_ReadIncludePaths(&LnState, pCursor);
    if (RT_SUCCESS(rc))
        rc = rtDwarfLine_ReadFileNames(&LnState, pCursor);

    /*
     * Run the program....
     */
    if (RT_SUCCESS(rc))
        rc = rtDwarfCursor_AdvanceToPos(pCursor, pbFirstOpcode);
    if (RT_SUCCESS(rc))
        rc = rtDwarfLine_RunProgram(&LnState, pCursor);

    /*
     * Clean up.
     */
    size_t i = LnState.cFileNames;
    while (i-- > 0)
        RTStrFree(LnState.papszFileNames[i]);
    RTMemFree(LnState.papszFileNames);
    RTMemFree(LnState.papszIncPaths);

    Assert(rtDwarfCursor_IsAtEndOfUnit(pCursor) || RT_FAILURE(rc));
    return rc;
}


/**
 * Explodes the line number table.
 *
 * The line numbers are insered into the debug info container.
 *
 * @returns IPRT status code
 * @param   pThis               The DWARF instance.
 */
static int rtDwarfLine_ExplodeAll(PRTDBGMODDWARF pThis)
{
    if (!pThis->aSections[krtDbgModDwarfSect_line].fPresent)
        return VINF_SUCCESS;

    RTDWARFCURSOR Cursor;
    int rc = rtDwarfCursor_Init(&Cursor, pThis, krtDbgModDwarfSect_line);
    if (RT_FAILURE(rc))
        return rc;

    while (   !rtDwarfCursor_IsAtEnd(&Cursor)
           && RT_SUCCESS(rc))
        rc = rtDwarfLine_ExplodeUnit(pThis, &Cursor);

    return rtDwarfCursor_Delete(&Cursor, rc);
}


/*
 *
 * DWARF Abbreviations.
 * DWARF Abbreviations.
 * DWARF Abbreviations.
 *
 */

/**
 * Deals with a cache miss in rtDwarfAbbrev_Lookup.
 *
 * @returns Pointer to abbreviation cache entry (read only).  May be rendered
 *          invalid by subsequent calls to this function.
 * @param   pThis               The DWARF instance.
 * @param   uCode               The abbreviation code to lookup.
 */
static PCRTDWARFABBREV rtDwarfAbbrev_LookupMiss(PRTDBGMODDWARF pThis, uint32_t uCode)
{
    /*
     * There is no entry with code zero.
     */
    if (!uCode)
        return NULL;

    /*
     * Resize the cache array if the code is considered cachable.
     */
    bool fFillCache = true;
    if (pThis->cCachedAbbrevsAlloced < uCode)
    {
        if (uCode >= _64K)
            fFillCache = false;
        else
        {
            uint32_t cNew = RT_ALIGN(uCode, 64);
            void *pv = RTMemRealloc(pThis->paCachedAbbrevs, sizeof(pThis->paCachedAbbrevs[0]) * cNew);
            if (!pv)
                fFillCache = false;
            else
            {
                Log(("rtDwarfAbbrev_LookupMiss: Growing from %u to %u...\n", pThis->cCachedAbbrevsAlloced, cNew));
                pThis->paCachedAbbrevs       = (PRTDWARFABBREV)pv;
                for (uint32_t i = pThis->cCachedAbbrevsAlloced; i < cNew; i++)
                    pThis->paCachedAbbrevs[i].offAbbrev = UINT32_MAX;
                pThis->cCachedAbbrevsAlloced = cNew;
            }
        }
    }

    /*
     * Walk the abbreviations till we find the desired code.
     */
    RTDWARFCURSOR Cursor;
    int rc = rtDwarfCursor_InitWithOffset(&Cursor, pThis, krtDbgModDwarfSect_abbrev, pThis->offCachedAbbrev);
    if (RT_FAILURE(rc))
        return NULL;

    PRTDWARFABBREV pRet = NULL;
    if (fFillCache)
    {
        /*
         * Search for the entry and fill the cache while doing so.
         * We assume that abbreviation codes for a unit will stop when we see
         * zero code or when the code value drops.
         */
        uint32_t uPrevCode = 0;
        for (;;)
        {
            /* Read the 'header'. Skipping zero code bytes. */
#ifdef LOG_ENABLED
            uint32_t const offStart = rtDwarfCursor_CalcSectOffsetU32(&Cursor);
#endif
            uint32_t const uCurCode = rtDwarfCursor_GetULeb128AsU32(&Cursor, 0);
            if (pRet && (uCurCode == 0 || uCurCode < uPrevCode))
                break; /* probably end of unit. */
            if (uCurCode != 0)
            {
                uint32_t const uCurTag   = rtDwarfCursor_GetULeb128AsU32(&Cursor, 0);
                uint8_t  const uChildren = rtDwarfCursor_GetU8(&Cursor, 0);
                if (RT_FAILURE(Cursor.rc))
                    break;
                if (   uCurTag > 0xffff
                    || uChildren > 1)
                {
                    Cursor.rc = VERR_DWARF_BAD_ABBREV;
                    break;
                }

                /* Cache it? */
                if (uCurCode <= pThis->cCachedAbbrevsAlloced)
                {
                    PRTDWARFABBREV pEntry = &pThis->paCachedAbbrevs[uCurCode - 1];
                    if (pEntry->offAbbrev != pThis->offCachedAbbrev)
                    {
                        pEntry->offAbbrev = pThis->offCachedAbbrev;
                        pEntry->fChildren = RT_BOOL(uChildren);
                        pEntry->uTag      = uCurTag;
                        pEntry->offSpec   = rtDwarfCursor_CalcSectOffsetU32(&Cursor);
#ifdef LOG_ENABLED
                        pEntry->cbHdr     = (uint8_t)(pEntry->offSpec - offStart);
                        Log7(("rtDwarfAbbrev_LookupMiss(%#x): fill: %#x: uTag=%#x offAbbrev=%#x%s\n",
                              uCode, offStart, pEntry->uTag, pEntry->offAbbrev, pEntry->fChildren ? " has-children" : ""));
#endif
                        if (uCurCode == uCode)
                        {
                            Assert(!pRet);
                            pRet = pEntry;
                            if (uCurCode == pThis->cCachedAbbrevsAlloced)
                                break;
                        }
                    }
                    else if (pRet)
                        break; /* Next unit, don't cache more. */
                    /* else: We're growing the cache and re-reading old data. */
                }

                /* Skip the specification. */
                uint32_t uAttr, uForm;
                do
                {
                    uAttr = rtDwarfCursor_GetULeb128AsU32(&Cursor, 0);
                    uForm = rtDwarfCursor_GetULeb128AsU32(&Cursor, 0);
                } while (uAttr != 0);
            }
            if (RT_FAILURE(Cursor.rc))
                break;

            /* Done? (Maximize cache filling.) */
            if (   pRet != NULL
                && uCurCode >= pThis->cCachedAbbrevsAlloced)
                break;
            uPrevCode = uCurCode;
        }
        if (pRet)
            Log6(("rtDwarfAbbrev_LookupMiss(%#x): uTag=%#x offSpec=%#x offAbbrev=%#x [fill]\n",
                  uCode, pRet->uTag, pRet->offSpec, pRet->offAbbrev));
        else
            Log6(("rtDwarfAbbrev_LookupMiss(%#x): failed [fill]\n", uCode));
    }
    else
    {
        /*
         * Search for the entry with the desired code, no cache filling.
         */
        for (;;)
        {
            /* Read the 'header'. */
#ifdef LOG_ENABLED
            uint32_t const offStart  = rtDwarfCursor_CalcSectOffsetU32(&Cursor);
#endif
            uint32_t const uCurCode  = rtDwarfCursor_GetULeb128AsU32(&Cursor, 0);
            uint32_t const uCurTag   = rtDwarfCursor_GetULeb128AsU32(&Cursor, 0);
            uint8_t  const uChildren = rtDwarfCursor_GetU8(&Cursor, 0);
            if (RT_FAILURE(Cursor.rc))
                break;
            if (   uCurTag > 0xffff
                || uChildren > 1)
            {
                Cursor.rc = VERR_DWARF_BAD_ABBREV;
                break;
            }

            /* Do we have a match? */
            if (uCurCode == uCode)
            {
                pRet = &pThis->LookupAbbrev;
                pRet->fChildren = RT_BOOL(uChildren);
                pRet->uTag      = uCurTag;
                pRet->offSpec   = rtDwarfCursor_CalcSectOffsetU32(&Cursor);
                pRet->offAbbrev = pThis->offCachedAbbrev;
#ifdef LOG_ENABLED
                pRet->cbHdr     = (uint8_t)(pRet->offSpec - offStart);
#endif
                break;
            }

            /* Skip the specification. */
            uint32_t uAttr, uForm;
            do
            {
                uAttr = rtDwarfCursor_GetULeb128AsU32(&Cursor, 0);
                uForm = rtDwarfCursor_GetULeb128AsU32(&Cursor, 0);
            } while (uAttr != 0);
            if (RT_FAILURE(Cursor.rc))
                break;
        }
        if (pRet)
            Log6(("rtDwarfAbbrev_LookupMiss(%#x): uTag=%#x offSpec=%#x offAbbrev=%#x [no-fill]\n",
                  uCode, pRet->uTag, pRet->offSpec, pRet->offAbbrev));
        else
            Log6(("rtDwarfAbbrev_LookupMiss(%#x): failed [no-fill]\n", uCode));
    }

    rtDwarfCursor_Delete(&Cursor, VINF_SUCCESS);
    return pRet;
}


/**
 * Looks up an abbreviation.
 *
 * @returns Pointer to abbreviation cache entry (read only).  May be rendered
 *          invalid by subsequent calls to this function.
 * @param   pThis               The DWARF instance.
 * @param   uCode               The abbreviation code to lookup.
 */
static PCRTDWARFABBREV rtDwarfAbbrev_Lookup(PRTDBGMODDWARF pThis, uint32_t uCode)
{
    uCode -= 1;
    if (uCode < pThis->cCachedAbbrevsAlloced)
    {
        if (pThis->paCachedAbbrevs[uCode].offAbbrev == pThis->offCachedAbbrev)
            return &pThis->paCachedAbbrevs[uCode];
    }
    return rtDwarfAbbrev_LookupMiss(pThis, uCode + 1);
}


/**
 * Sets the abbreviation offset of the current unit.
 *
 * @param   pThis               The DWARF instance.
 * @param   offAbbrev           The offset into the abbreviation section.
 */
static void rtDwarfAbbrev_SetUnitOffset(PRTDBGMODDWARF pThis, uint32_t offAbbrev)
{
    pThis->offCachedAbbrev = offAbbrev;
}



/*
 *
 * DIE Attribute Parsers.
 * DIE Attribute Parsers.
 * DIE Attribute Parsers.
 *
 */

/**
 * Gets the compilation unit a DIE belongs to.
 *
 * @returns The compilation unit DIE.
 * @param   pDie                Some DIE in the unit.
 */
static PRTDWARFDIECOMPILEUNIT rtDwarfDie_GetCompileUnit(PRTDWARFDIE pDie)
{
    while (pDie->pParent)
        pDie = pDie->pParent;
    AssertReturn(   pDie->uTag == DW_TAG_compile_unit
                 || pDie->uTag == DW_TAG_partial_unit,
                 NULL);
    return (PRTDWARFDIECOMPILEUNIT)pDie;
}


/**
 * Resolves a string section (debug_str) reference.
 *
 * @returns Pointer to the string (inside the string section).
 * @param   pThis               The DWARF instance.
 * @param   pCursor             The cursor.
 * @param   pszErrValue         What to return on failure (@a
 *                              pCursor->rc is set).
 */
static const char *rtDwarfDecodeHlp_GetStrp(PRTDBGMODDWARF pThis, PRTDWARFCURSOR pCursor, const char *pszErrValue)
{
    uint64_t offDebugStr = rtDwarfCursor_GetUOff(pCursor, UINT64_MAX);
    if (RT_FAILURE(pCursor->rc))
        return pszErrValue;

    if (offDebugStr >= pThis->aSections[krtDbgModDwarfSect_str].cb)
    {
        /* Ugly: Exploit the cursor status field for reporting errors. */
        pCursor->rc = VERR_DWARF_BAD_INFO;
        return pszErrValue;
    }

    if (!pThis->aSections[krtDbgModDwarfSect_str].pv)
    {
        int rc = rtDbgModDwarfLoadSection(pThis, krtDbgModDwarfSect_str);
        if (RT_FAILURE(rc))
        {
            /* Ugly: Exploit the cursor status field for reporting errors. */
            pCursor->rc = rc;
            return pszErrValue;
        }
    }

    return (const char *)pThis->aSections[krtDbgModDwarfSect_str].pv + (size_t)offDebugStr;
}


/** @callback_method_impl{FNRTDWARFATTRDECODER} */
static DECLCALLBACK(int) rtDwarfDecode_Address(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                               uint32_t uForm, PRTDWARFCURSOR pCursor)
{
    AssertReturn(ATTR_GET_SIZE(pDesc) == sizeof(RTDWARFADDR), VERR_INTERNAL_ERROR_3);
    NOREF(pDie);

    uint64_t uAddr;
    switch (uForm)
    {
        case DW_FORM_addr:  uAddr = rtDwarfCursor_GetNativeUOff(pCursor, 0); break;
        case DW_FORM_data1: uAddr = rtDwarfCursor_GetU8(pCursor, 0); break;
        case DW_FORM_data2: uAddr = rtDwarfCursor_GetU16(pCursor, 0); break;
        case DW_FORM_data4: uAddr = rtDwarfCursor_GetU32(pCursor, 0); break;
        case DW_FORM_data8: uAddr = rtDwarfCursor_GetU64(pCursor, 0); break;
        case DW_FORM_udata: uAddr = rtDwarfCursor_GetULeb128(pCursor, 0); break;
        default:
            AssertMsgFailedReturn(("%#x (%s)\n", uForm, rtDwarfLog_FormName(uForm)), VERR_DWARF_UNEXPECTED_FORM);
    }
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;

    PRTDWARFADDR pAddr = (PRTDWARFADDR)pbMember;
    pAddr->uAddress = uAddr;

    Log4(("          %-20s  %#010llx  [%s]\n", rtDwarfLog_AttrName(pDesc->uAttr), uAddr, rtDwarfLog_FormName(uForm)));
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTDWARFATTRDECODER} */
static DECLCALLBACK(int) rtDwarfDecode_Bool(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                            uint32_t uForm, PRTDWARFCURSOR pCursor)
{
    AssertReturn(ATTR_GET_SIZE(pDesc) == sizeof(bool), VERR_INTERNAL_ERROR_3);
    NOREF(pDie);

    bool *pfMember = (bool *)pbMember;
    switch (uForm)
    {
        case DW_FORM_flag:
        {
            uint8_t b = rtDwarfCursor_GetU8(pCursor, UINT8_MAX);
            if (b > 1)
            {
                Log(("Unexpected boolean value %#x\n", b));
                return RT_FAILURE(pCursor->rc) ? pCursor->rc : pCursor->rc = VERR_DWARF_BAD_INFO;
            }
            *pfMember = RT_BOOL(b);
            break;
        }

        case DW_FORM_flag_present:
            *pfMember = true;
            break;

        default:
            AssertMsgFailedReturn(("%#x\n", uForm), VERR_DWARF_UNEXPECTED_FORM);
    }

    Log4(("          %-20s  %RTbool  [%s]\n", rtDwarfLog_AttrName(pDesc->uAttr), *pfMember, rtDwarfLog_FormName(uForm)));
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTDWARFATTRDECODER} */
static DECLCALLBACK(int) rtDwarfDecode_LowHighPc(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                                 uint32_t uForm, PRTDWARFCURSOR pCursor)
{
    AssertReturn(ATTR_GET_SIZE(pDesc) == sizeof(RTDWARFADDRRANGE), VERR_INTERNAL_ERROR_3);
    AssertReturn(pDesc->uAttr == DW_AT_low_pc || pDesc->uAttr == DW_AT_high_pc, VERR_INTERNAL_ERROR_3);
    NOREF(pDie);

    uint64_t uAddr;
    switch (uForm)
    {
        case DW_FORM_addr:  uAddr = rtDwarfCursor_GetNativeUOff(pCursor, 0); break;
        case DW_FORM_data1: uAddr = rtDwarfCursor_GetU8(pCursor, 0); break;
        case DW_FORM_data2: uAddr = rtDwarfCursor_GetU16(pCursor, 0); break;
        case DW_FORM_data4: uAddr = rtDwarfCursor_GetU32(pCursor, 0); break;
        case DW_FORM_data8: uAddr = rtDwarfCursor_GetU64(pCursor, 0); break;
        case DW_FORM_udata: uAddr = rtDwarfCursor_GetULeb128(pCursor, 0); break;
        default:
            AssertMsgFailedReturn(("%#x\n", uForm), VERR_DWARF_UNEXPECTED_FORM);
    }
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;

    PRTDWARFADDRRANGE pRange = (PRTDWARFADDRRANGE)pbMember;
    if (pDesc->uAttr == DW_AT_low_pc)
    {
        if (pRange->fHaveLowAddress)
        {
            Log(("rtDwarfDecode_LowHighPc: Duplicate DW_AT_low_pc\n"));
            return pCursor->rc = VERR_DWARF_BAD_INFO;
        }
        pRange->fHaveLowAddress  = true;
        pRange->uLowAddress      = uAddr;
    }
    else
    {
        if (pRange->fHaveHighAddress)
        {
            Log(("rtDwarfDecode_LowHighPc: Duplicate DW_AT_high_pc\n"));
            return pCursor->rc = VERR_DWARF_BAD_INFO;
        }
        pRange->fHaveHighAddress = true;
        pRange->fHaveHighIsAddress = uForm == DW_FORM_addr;
        if (!pRange->fHaveHighIsAddress && pRange->fHaveLowAddress)
        {
            pRange->fHaveHighIsAddress = true;
            pRange->uHighAddress     = uAddr + pRange->uLowAddress;
        }
        else
            pRange->uHighAddress     = uAddr;

    }
    pRange->cAttrs++;

    Log4(("          %-20s  %#010llx  [%s]\n", rtDwarfLog_AttrName(pDesc->uAttr), uAddr, rtDwarfLog_FormName(uForm)));
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTDWARFATTRDECODER} */
static DECLCALLBACK(int) rtDwarfDecode_Ranges(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                              uint32_t uForm, PRTDWARFCURSOR pCursor)
{
    AssertReturn(ATTR_GET_SIZE(pDesc) == sizeof(RTDWARFADDRRANGE), VERR_INTERNAL_ERROR_3);
    AssertReturn(pDesc->uAttr == DW_AT_ranges, VERR_INTERNAL_ERROR_3);
    NOREF(pDie);

    /* Decode it. */
    uint64_t off;
    switch (uForm)
    {
        case DW_FORM_addr:          off = rtDwarfCursor_GetNativeUOff(pCursor, 0); break;
        case DW_FORM_data4:         off = rtDwarfCursor_GetU32(pCursor, 0); break;
        case DW_FORM_data8:         off = rtDwarfCursor_GetU64(pCursor, 0); break;
        case DW_FORM_sec_offset:    off = rtDwarfCursor_GetUOff(pCursor, 0); break;
        default:
            AssertMsgFailedReturn(("%#x\n", uForm), VERR_DWARF_UNEXPECTED_FORM);
    }
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;

    /* Validate the offset and load the ranges. */
    PRTDBGMODDWARF pThis = pCursor->pDwarfMod;
    if (off >= pThis->aSections[krtDbgModDwarfSect_ranges].cb)
    {
        Log(("rtDwarfDecode_Ranges: bad ranges off=%#llx\n", off));
        return pCursor->rc = VERR_DWARF_BAD_POS;
    }

    if (!pThis->aSections[krtDbgModDwarfSect_ranges].pv)
    {
        int rc = rtDbgModDwarfLoadSection(pThis, krtDbgModDwarfSect_ranges);
        if (RT_FAILURE(rc))
            return pCursor->rc = rc;
    }

    /* Store the result. */
    PRTDWARFADDRRANGE pRange = (PRTDWARFADDRRANGE)pbMember;
    if (pRange->fHaveRanges)
    {
        Log(("rtDwarfDecode_Ranges: Duplicate DW_AT_ranges\n"));
        return pCursor->rc = VERR_DWARF_BAD_INFO;
    }
    pRange->fHaveRanges = true;
    pRange->cAttrs++;
    pRange->pbRanges    = (uint8_t const *)pThis->aSections[krtDbgModDwarfSect_ranges].pv + (size_t)off;

    Log4(("          %-20s  TODO  [%s]\n", rtDwarfLog_AttrName(pDesc->uAttr), rtDwarfLog_FormName(uForm)));
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTDWARFATTRDECODER} */
static DECLCALLBACK(int) rtDwarfDecode_Reference(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                                 uint32_t uForm, PRTDWARFCURSOR pCursor)
{
    AssertReturn(ATTR_GET_SIZE(pDesc) == sizeof(RTDWARFREF), VERR_INTERNAL_ERROR_3);

    /* Decode it. */
    uint64_t        off;
    krtDwarfRef     enmWrt = krtDwarfRef_SameUnit;
    switch (uForm)
    {
        case DW_FORM_ref1:          off = rtDwarfCursor_GetU8(pCursor, 0); break;
        case DW_FORM_ref2:          off = rtDwarfCursor_GetU16(pCursor, 0); break;
        case DW_FORM_ref4:          off = rtDwarfCursor_GetU32(pCursor, 0); break;
        case DW_FORM_ref8:          off = rtDwarfCursor_GetU64(pCursor, 0); break;
        case DW_FORM_ref_udata:     off = rtDwarfCursor_GetULeb128(pCursor, 0); break;

        case DW_FORM_ref_addr:
            enmWrt = krtDwarfRef_InfoSection;
            off = rtDwarfCursor_GetUOff(pCursor, 0);
            break;

        case DW_FORM_ref_sig8:
            enmWrt = krtDwarfRef_TypeId64;
            off = rtDwarfCursor_GetU64(pCursor, 0);
            break;

        default:
            AssertMsgFailedReturn(("%#x\n", uForm), VERR_DWARF_UNEXPECTED_FORM);
    }
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;

    /* Validate the offset and convert to debug_info relative offsets. */
    if (enmWrt == krtDwarfRef_InfoSection)
    {
        if (off >= pCursor->pDwarfMod->aSections[krtDbgModDwarfSect_info].cb)
        {
            Log(("rtDwarfDecode_Reference: bad info off=%#llx\n", off));
            return pCursor->rc = VERR_DWARF_BAD_POS;
        }
    }
    else if (enmWrt == krtDwarfRef_SameUnit)
    {
        PRTDWARFDIECOMPILEUNIT pUnit = rtDwarfDie_GetCompileUnit(pDie);
        if (off >= pUnit->cbUnit)
        {
            Log(("rtDwarfDecode_Reference: bad unit off=%#llx\n", off));
            return pCursor->rc = VERR_DWARF_BAD_POS;
        }
        off += pUnit->offUnit;
        enmWrt = krtDwarfRef_InfoSection;
    }
    /* else: not bother verifying/resolving the indirect type reference yet. */

    /* Store it */
    PRTDWARFREF pRef = (PRTDWARFREF)pbMember;
    pRef->enmWrt = enmWrt;
    pRef->off    = off;

    Log4(("          %-20s  %d:%#010llx  [%s]\n", rtDwarfLog_AttrName(pDesc->uAttr), enmWrt, off, rtDwarfLog_FormName(uForm)));
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTDWARFATTRDECODER} */
static DECLCALLBACK(int) rtDwarfDecode_SectOff(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                               uint32_t uForm, PRTDWARFCURSOR pCursor)
{
    AssertReturn(ATTR_GET_SIZE(pDesc) == sizeof(RTDWARFREF), VERR_INTERNAL_ERROR_3);
    NOREF(pDie);

    uint64_t off;
    switch (uForm)
    {
        case DW_FORM_data4:      off = rtDwarfCursor_GetU32(pCursor, 0);  break;
        case DW_FORM_data8:      off = rtDwarfCursor_GetU64(pCursor, 0);  break;
        case DW_FORM_sec_offset: off = rtDwarfCursor_GetUOff(pCursor, 0); break;
        default:
            AssertMsgFailedReturn(("%#x (%s)\n", uForm, rtDwarfLog_FormName(uForm)), VERR_DWARF_UNEXPECTED_FORM);
    }
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;

    krtDbgModDwarfSect  enmSect;
    krtDwarfRef         enmWrt;
    switch (pDesc->uAttr)
    {
        case DW_AT_stmt_list:   enmSect = krtDbgModDwarfSect_line;    enmWrt = krtDwarfRef_LineSection;    break;
        case DW_AT_macro_info:  enmSect = krtDbgModDwarfSect_loc;     enmWrt = krtDwarfRef_LocSection;     break;
        case DW_AT_ranges:      enmSect = krtDbgModDwarfSect_ranges;  enmWrt = krtDwarfRef_RangesSection;  break;
        default:
            AssertMsgFailedReturn(("%u (%s)\n", pDesc->uAttr, rtDwarfLog_AttrName(pDesc->uAttr)), VERR_INTERNAL_ERROR_4);
    }
    size_t cbSect = pCursor->pDwarfMod->aSections[enmSect].cb;
    if (off >= cbSect)
    {
        /* Watcom generates offset past the end of the section, increasing the
           offset by one for each compile unit. So, just fudge it. */
        Log(("rtDwarfDecode_SectOff: bad off=%#llx, attr %#x (%s), enmSect=%d cb=%#llx; Assuming watcom/gcc.\n", off,
             pDesc->uAttr, rtDwarfLog_AttrName(pDesc->uAttr), enmSect, cbSect));
        off = cbSect;
    }

    PRTDWARFREF pRef = (PRTDWARFREF)pbMember;
    pRef->enmWrt = enmWrt;
    pRef->off    = off;

    Log4(("          %-20s  %d:%#010llx  [%s]\n", rtDwarfLog_AttrName(pDesc->uAttr), enmWrt, off, rtDwarfLog_FormName(uForm)));
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTDWARFATTRDECODER} */
static DECLCALLBACK(int) rtDwarfDecode_String(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                              uint32_t uForm, PRTDWARFCURSOR pCursor)
{
    AssertReturn(ATTR_GET_SIZE(pDesc) == sizeof(const char *), VERR_INTERNAL_ERROR_3);
    NOREF(pDie);

    const char *psz;
    switch (uForm)
    {
        case DW_FORM_string:
            psz = rtDwarfCursor_GetSZ(pCursor, NULL);
            break;

        case DW_FORM_strp:
            psz = rtDwarfDecodeHlp_GetStrp(pCursor->pDwarfMod, pCursor, NULL);
            break;

        default:
            AssertMsgFailedReturn(("%#x\n", uForm), VERR_DWARF_UNEXPECTED_FORM);
    }

    *(const char **)pbMember = psz;
    Log4(("          %-20s  '%s'  [%s]\n", rtDwarfLog_AttrName(pDesc->uAttr), psz, rtDwarfLog_FormName(uForm)));
    return pCursor->rc;
}


/** @callback_method_impl{FNRTDWARFATTRDECODER} */
static DECLCALLBACK(int) rtDwarfDecode_UnsignedInt(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                                   uint32_t uForm, PRTDWARFCURSOR pCursor)
{
    NOREF(pDie);
    uint64_t u64Val;
    switch (uForm)
    {
        case DW_FORM_udata: u64Val = rtDwarfCursor_GetULeb128(pCursor, 0); break;
        case DW_FORM_data1: u64Val = rtDwarfCursor_GetU8(pCursor, 0); break;
        case DW_FORM_data2: u64Val = rtDwarfCursor_GetU16(pCursor, 0); break;
        case DW_FORM_data4: u64Val = rtDwarfCursor_GetU32(pCursor, 0); break;
        case DW_FORM_data8: u64Val = rtDwarfCursor_GetU64(pCursor, 0); break;
        default:
            AssertMsgFailedReturn(("%#x\n", uForm), VERR_DWARF_UNEXPECTED_FORM);
    }
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;

    switch (ATTR_GET_SIZE(pDesc))
    {
        case 1:
            *pbMember = (uint8_t)u64Val;
            if (*pbMember != u64Val)
            {
                AssertFailed();
                return VERR_OUT_OF_RANGE;
            }
            break;

        case 2:
            *(uint16_t *)pbMember = (uint16_t)u64Val;
            if (*(uint16_t *)pbMember != u64Val)
            {
                AssertFailed();
                return VERR_OUT_OF_RANGE;
            }
            break;

        case 4:
            *(uint32_t *)pbMember = (uint32_t)u64Val;
            if (*(uint32_t *)pbMember != u64Val)
            {
                AssertFailed();
                return VERR_OUT_OF_RANGE;
            }
            break;

        case 8:
            *(uint64_t *)pbMember = (uint64_t)u64Val;
            if (*(uint64_t *)pbMember != u64Val)
            {
                AssertFailed();
                return VERR_OUT_OF_RANGE;
            }
            break;

        default:
            AssertMsgFailedReturn(("%#x\n", ATTR_GET_SIZE(pDesc)), VERR_INTERNAL_ERROR_2);
    }
    return VINF_SUCCESS;
}


/**
 * Initialize location interpreter state from cursor & form.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if no location information (i.e. there is source but
 *          it resulted in no byte code).
 * @param   pLoc                The location state structure to initialize.
 * @param   pCursor             The cursor to read from.
 * @param   uForm               The attribute form.
 */
static int rtDwarfLoc_Init(PRTDWARFLOCST pLoc, PRTDWARFCURSOR pCursor, uint32_t uForm)
{
    uint32_t cbBlock;
    switch (uForm)
    {
        case DW_FORM_block1:
            cbBlock = rtDwarfCursor_GetU8(pCursor, 0);
            break;

        case DW_FORM_block2:
            cbBlock = rtDwarfCursor_GetU16(pCursor, 0);
            break;

        case DW_FORM_block4:
            cbBlock = rtDwarfCursor_GetU32(pCursor, 0);
            break;

        case DW_FORM_block:
            cbBlock = rtDwarfCursor_GetULeb128(pCursor, 0);
            break;

        default:
            AssertMsgFailedReturn(("uForm=%#x\n", uForm), VERR_DWARF_UNEXPECTED_FORM);
    }
    if (!cbBlock)
        return VERR_NOT_FOUND;

    int rc = rtDwarfCursor_InitForBlock(&pLoc->Cursor, pCursor, cbBlock);
    if (RT_FAILURE(rc))
        return rc;
    pLoc->iTop = -1;
    return VINF_SUCCESS;
}


/**
 * Pushes a value onto the stack.
 *
 * @returns VINF_SUCCESS or VERR_DWARF_STACK_OVERFLOW.
 * @param   pLoc                The state.
 * @param   uValue              The value to push.
 */
static int rtDwarfLoc_Push(PRTDWARFLOCST pLoc, uint64_t uValue)
{
    int iTop = pLoc->iTop + 1;
    AssertReturn((unsigned)iTop < RT_ELEMENTS(pLoc->auStack), VERR_DWARF_STACK_OVERFLOW);
    pLoc->auStack[iTop] = uValue;
    pLoc->iTop = iTop;
    return VINF_SUCCESS;
}


static int rtDwarfLoc_Evaluate(PRTDWARFLOCST pLoc, void *pvLater, void *pvUser)
{
    RT_NOREF_PV(pvLater); RT_NOREF_PV(pvUser);

    while (!rtDwarfCursor_IsAtEndOfUnit(&pLoc->Cursor))
    {
        /* Read the next opcode.*/
        uint8_t const bOpcode = rtDwarfCursor_GetU8(&pLoc->Cursor, 0);

        /* Get its operands. */
        uint64_t uOperand1 = 0;
        uint64_t uOperand2 = 0;
        switch (bOpcode)
        {
            case DW_OP_addr:
                uOperand1 = rtDwarfCursor_GetNativeUOff(&pLoc->Cursor, 0);
                break;
            case DW_OP_pick:
            case DW_OP_const1u:
            case DW_OP_deref_size:
            case DW_OP_xderef_size:
                uOperand1 = rtDwarfCursor_GetU8(&pLoc->Cursor, 0);
                break;
            case DW_OP_const1s:
                uOperand1 = (int8_t)rtDwarfCursor_GetU8(&pLoc->Cursor, 0);
                break;
            case DW_OP_const2u:
                uOperand1 = rtDwarfCursor_GetU16(&pLoc->Cursor, 0);
                break;
            case DW_OP_skip:
            case DW_OP_bra:
            case DW_OP_const2s:
                uOperand1 = (int16_t)rtDwarfCursor_GetU16(&pLoc->Cursor, 0);
                break;
            case DW_OP_const4u:
                uOperand1 = rtDwarfCursor_GetU32(&pLoc->Cursor, 0);
                break;
            case DW_OP_const4s:
                uOperand1 = (int32_t)rtDwarfCursor_GetU32(&pLoc->Cursor, 0);
                break;
            case DW_OP_const8u:
                uOperand1 = rtDwarfCursor_GetU64(&pLoc->Cursor, 0);
                break;
            case DW_OP_const8s:
                uOperand1 = rtDwarfCursor_GetU64(&pLoc->Cursor, 0);
                break;
            case DW_OP_regx:
            case DW_OP_piece:
            case DW_OP_plus_uconst:
            case DW_OP_constu:
                uOperand1 = rtDwarfCursor_GetULeb128(&pLoc->Cursor, 0);
                break;
            case DW_OP_consts:
            case DW_OP_fbreg:
            case DW_OP_breg0+0: case DW_OP_breg0+1: case DW_OP_breg0+2: case DW_OP_breg0+3:
            case DW_OP_breg0+4: case DW_OP_breg0+5: case DW_OP_breg0+6: case DW_OP_breg0+7:
            case DW_OP_breg0+8: case DW_OP_breg0+9: case DW_OP_breg0+10: case DW_OP_breg0+11:
            case DW_OP_breg0+12: case DW_OP_breg0+13: case DW_OP_breg0+14: case DW_OP_breg0+15:
            case DW_OP_breg0+16: case DW_OP_breg0+17: case DW_OP_breg0+18: case DW_OP_breg0+19:
            case DW_OP_breg0+20: case DW_OP_breg0+21: case DW_OP_breg0+22: case DW_OP_breg0+23:
            case DW_OP_breg0+24: case DW_OP_breg0+25: case DW_OP_breg0+26: case DW_OP_breg0+27:
            case DW_OP_breg0+28: case DW_OP_breg0+29: case DW_OP_breg0+30: case DW_OP_breg0+31:
                uOperand1 = rtDwarfCursor_GetSLeb128(&pLoc->Cursor, 0);
                break;
            case DW_OP_bregx:
                uOperand1 = rtDwarfCursor_GetULeb128(&pLoc->Cursor, 0);
                uOperand2 = rtDwarfCursor_GetSLeb128(&pLoc->Cursor, 0);
                break;
        }
        if (RT_FAILURE(pLoc->Cursor.rc))
            break;

        /* Interpret the opcode. */
        int rc;
        switch (bOpcode)
        {
            case DW_OP_const1u:
            case DW_OP_const1s:
            case DW_OP_const2u:
            case DW_OP_const2s:
            case DW_OP_const4u:
            case DW_OP_const4s:
            case DW_OP_const8u:
            case DW_OP_const8s:
            case DW_OP_constu:
            case DW_OP_consts:
            case DW_OP_addr:
                rc = rtDwarfLoc_Push(pLoc, uOperand1);
                break;
            case DW_OP_lit0 +  0: case DW_OP_lit0 +  1: case DW_OP_lit0 +  2: case DW_OP_lit0 +  3:
            case DW_OP_lit0 +  4: case DW_OP_lit0 +  5: case DW_OP_lit0 +  6: case DW_OP_lit0 +  7:
            case DW_OP_lit0 +  8: case DW_OP_lit0 +  9: case DW_OP_lit0 + 10: case DW_OP_lit0 + 11:
            case DW_OP_lit0 + 12: case DW_OP_lit0 + 13: case DW_OP_lit0 + 14: case DW_OP_lit0 + 15:
            case DW_OP_lit0 + 16: case DW_OP_lit0 + 17: case DW_OP_lit0 + 18: case DW_OP_lit0 + 19:
            case DW_OP_lit0 + 20: case DW_OP_lit0 + 21: case DW_OP_lit0 + 22: case DW_OP_lit0 + 23:
            case DW_OP_lit0 + 24: case DW_OP_lit0 + 25: case DW_OP_lit0 + 26: case DW_OP_lit0 + 27:
            case DW_OP_lit0 + 28: case DW_OP_lit0 + 29: case DW_OP_lit0 + 30: case DW_OP_lit0 + 31:
                rc = rtDwarfLoc_Push(pLoc, bOpcode - DW_OP_lit0);
                break;
            case DW_OP_nop:
                break;
            case DW_OP_dup:               /** @todo 0 operands. */
            case DW_OP_drop:              /** @todo 0 operands. */
            case DW_OP_over:              /** @todo 0 operands. */
            case DW_OP_pick:              /** @todo 1 operands, a 1-byte stack index. */
            case DW_OP_swap:              /** @todo 0 operands. */
            case DW_OP_rot:               /** @todo 0 operands. */
            case DW_OP_abs:               /** @todo 0 operands. */
            case DW_OP_and:               /** @todo 0 operands. */
            case DW_OP_div:               /** @todo 0 operands. */
            case DW_OP_minus:             /** @todo 0 operands. */
            case DW_OP_mod:               /** @todo 0 operands. */
            case DW_OP_mul:               /** @todo 0 operands. */
            case DW_OP_neg:               /** @todo 0 operands. */
            case DW_OP_not:               /** @todo 0 operands. */
            case DW_OP_or:                /** @todo 0 operands. */
            case DW_OP_plus:              /** @todo 0 operands. */
            case DW_OP_plus_uconst:       /** @todo 1 operands, a ULEB128 addend. */
            case DW_OP_shl:               /** @todo 0 operands. */
            case DW_OP_shr:               /** @todo 0 operands. */
            case DW_OP_shra:              /** @todo 0 operands. */
            case DW_OP_xor:               /** @todo 0 operands. */
            case DW_OP_skip:              /** @todo 1 signed 2-byte constant. */
            case DW_OP_bra:               /** @todo 1 signed 2-byte constant. */
            case DW_OP_eq:                /** @todo 0 operands. */
            case DW_OP_ge:                /** @todo 0 operands. */
            case DW_OP_gt:                /** @todo 0 operands. */
            case DW_OP_le:                /** @todo 0 operands. */
            case DW_OP_lt:                /** @todo 0 operands. */
            case DW_OP_ne:                /** @todo 0 operands. */
            case DW_OP_reg0 +  0: case DW_OP_reg0 +  1: case DW_OP_reg0 +  2: case DW_OP_reg0 +  3: /** @todo 0 operands - reg 0..31. */
            case DW_OP_reg0 +  4: case DW_OP_reg0 +  5: case DW_OP_reg0 +  6: case DW_OP_reg0 +  7:
            case DW_OP_reg0 +  8: case DW_OP_reg0 +  9: case DW_OP_reg0 + 10: case DW_OP_reg0 + 11:
            case DW_OP_reg0 + 12: case DW_OP_reg0 + 13: case DW_OP_reg0 + 14: case DW_OP_reg0 + 15:
            case DW_OP_reg0 + 16: case DW_OP_reg0 + 17: case DW_OP_reg0 + 18: case DW_OP_reg0 + 19:
            case DW_OP_reg0 + 20: case DW_OP_reg0 + 21: case DW_OP_reg0 + 22: case DW_OP_reg0 + 23:
            case DW_OP_reg0 + 24: case DW_OP_reg0 + 25: case DW_OP_reg0 + 26: case DW_OP_reg0 + 27:
            case DW_OP_reg0 + 28: case DW_OP_reg0 + 29: case DW_OP_reg0 + 30: case DW_OP_reg0 + 31:
            case DW_OP_breg0+  0: case DW_OP_breg0+  1: case DW_OP_breg0+  2: case DW_OP_breg0+  3: /** @todo 1 operand, a SLEB128 offset. */
            case DW_OP_breg0+  4: case DW_OP_breg0+  5: case DW_OP_breg0+  6: case DW_OP_breg0+  7:
            case DW_OP_breg0+  8: case DW_OP_breg0+  9: case DW_OP_breg0+ 10: case DW_OP_breg0+ 11:
            case DW_OP_breg0+ 12: case DW_OP_breg0+ 13: case DW_OP_breg0+ 14: case DW_OP_breg0+ 15:
            case DW_OP_breg0+ 16: case DW_OP_breg0+ 17: case DW_OP_breg0+ 18: case DW_OP_breg0+ 19:
            case DW_OP_breg0+ 20: case DW_OP_breg0+ 21: case DW_OP_breg0+ 22: case DW_OP_breg0+ 23:
            case DW_OP_breg0+ 24: case DW_OP_breg0+ 25: case DW_OP_breg0+ 26: case DW_OP_breg0+ 27:
            case DW_OP_breg0+ 28: case DW_OP_breg0+ 29: case DW_OP_breg0+ 30: case DW_OP_breg0+ 31:
            case DW_OP_piece:             /** @todo 1 operand, a ULEB128 size of piece addressed. */
            case DW_OP_regx:              /** @todo 1 operand, a ULEB128 register. */
            case DW_OP_fbreg:             /** @todo 1 operand, a SLEB128 offset. */
            case DW_OP_bregx:             /** @todo 2 operands, a ULEB128 register followed by a SLEB128 offset. */
            case DW_OP_deref:             /** @todo 0 operands. */
            case DW_OP_deref_size:        /** @todo 1 operand, a 1-byte size of data retrieved. */
            case DW_OP_xderef:            /** @todo 0 operands. */
            case DW_OP_xderef_size:        /** @todo 1 operand, a 1-byte size of data retrieved. */
                AssertMsgFailedReturn(("bOpcode=%#x\n", bOpcode), VERR_DWARF_TODO);
            default:
                AssertMsgFailedReturn(("bOpcode=%#x\n", bOpcode), VERR_DWARF_UNKNOWN_LOC_OPCODE);
        }
    }

    return pLoc->Cursor.rc;
}


/** @callback_method_impl{FNRTDWARFATTRDECODER} */
static DECLCALLBACK(int) rtDwarfDecode_SegmentLoc(PRTDWARFDIE pDie, uint8_t *pbMember, PCRTDWARFATTRDESC pDesc,
                                                  uint32_t uForm, PRTDWARFCURSOR pCursor)
{
    NOREF(pDie);
    AssertReturn(ATTR_GET_SIZE(pDesc) == 2, VERR_DWARF_IPE);

    int rc;
    if (   uForm == DW_FORM_block
        || uForm == DW_FORM_block1
        || uForm == DW_FORM_block2
        || uForm == DW_FORM_block4)
    {
        RTDWARFLOCST LocSt;
        rc = rtDwarfLoc_Init(&LocSt, pCursor, uForm);
        if (RT_SUCCESS(rc))
        {
            rc = rtDwarfLoc_Evaluate(&LocSt, NULL, NULL);
            if (RT_SUCCESS(rc))
            {
                if (LocSt.iTop >= 0)
                {
                    *(uint16_t *)pbMember = LocSt.auStack[LocSt.iTop];
                    Log4(("          %-20s  %#06llx  [%s]\n", rtDwarfLog_AttrName(pDesc->uAttr),
                          LocSt.auStack[LocSt.iTop],  rtDwarfLog_FormName(uForm)));
                    return VINF_SUCCESS;
                }
                rc = VERR_DWARF_STACK_UNDERFLOW;
            }
        }
    }
    else
        rc = rtDwarfDecode_UnsignedInt(pDie, pbMember, pDesc, uForm, pCursor);
    return rc;
}

/*
 *
 * DWARF debug_info parser
 * DWARF debug_info parser
 * DWARF debug_info parser
 *
 */


/**
 * Special hack to get the name and/or linkage name for a subprogram via a
 * specification reference.
 *
 * Since this is a hack, we ignore failure.
 *
 * If we want to really make use of DWARF info, we'll have to create some kind
 * of lookup tree for handling this. But currently we don't, so a hack will
 * suffice.
 *
 * @param   pThis               The DWARF instance.
 * @param   pSubProgram         The subprogram which is short on names.
 */
static void rtDwarfInfo_TryGetSubProgramNameFromSpecRef(PRTDBGMODDWARF pThis, PRTDWARFDIESUBPROGRAM pSubProgram)
{
    /*
     * Must have a spec ref, and it must be in the info section.
     */
    if (pSubProgram->SpecRef.enmWrt != krtDwarfRef_InfoSection)
        return;

    /*
     * Create a cursor for reading the info and then the abbrivation code
     * starting the off the DIE.
     */
    RTDWARFCURSOR InfoCursor;
    int rc = rtDwarfCursor_InitWithOffset(&InfoCursor, pThis, krtDbgModDwarfSect_info, pSubProgram->SpecRef.off);
    if (RT_FAILURE(rc))
        return;

    uint32_t uAbbrCode = rtDwarfCursor_GetULeb128AsU32(&InfoCursor, UINT32_MAX);
    if (uAbbrCode)
    {
        /* Only references to subprogram tags are interesting here. */
        PCRTDWARFABBREV pAbbrev = rtDwarfAbbrev_Lookup(pThis, uAbbrCode);
        if (   pAbbrev
            && pAbbrev->uTag == DW_TAG_subprogram)
        {
            /*
             * Use rtDwarfInfo_ParseDie to do the parsing, but with a different
             * attribute spec than usual.
             */
            rtDwarfInfo_ParseDie(pThis, &pSubProgram->Core, &g_SubProgramSpecHackDesc, &InfoCursor,
                                 pAbbrev, false /*fInitDie*/);
        }
    }

    rtDwarfCursor_Delete(&InfoCursor, VINF_SUCCESS);
}


/**
 * Select which name to use.
 *
 * @returns One of the names.
 * @param   pszName             The DWARF name, may exclude namespace and class.
 *                              Can also be NULL.
 * @param   pszLinkageName      The linkage name. Can be NULL.
 */
static const char *rtDwarfInfo_SelectName(const char *pszName,  const char *pszLinkageName)
{
    if (!pszName || !pszLinkageName)
        return pszName ? pszName : pszLinkageName;

    /*
     * Some heuristics for selecting the link name if the normal name is missing
     * namespace or class prefixes.
     */
    size_t cchName = strlen(pszName);
    size_t cchLinkageName = strlen(pszLinkageName);
    if (cchLinkageName <= cchName + 1)
        return pszName;

    const char *psz = strstr(pszLinkageName, pszName);
    if (!psz || psz - pszLinkageName < 4)
        return pszName;

    return pszLinkageName;
}


/**
 * Parse the attributes of a DIE.
 *
 * @returns IPRT status code.
 * @param   pThis               The DWARF instance.
 * @param   pDie                The internal DIE structure to fill.
 */
static int rtDwarfInfo_SnoopSymbols(PRTDBGMODDWARF pThis, PRTDWARFDIE pDie)
{
    int rc = VINF_SUCCESS;
    switch (pDie->uTag)
    {
        case DW_TAG_subprogram:
        {
            PRTDWARFDIESUBPROGRAM pSubProgram = (PRTDWARFDIESUBPROGRAM)pDie;

            /* Obtain referenced specification there is only partial info. */
            if (   pSubProgram->PcRange.cAttrs
                && !pSubProgram->pszName)
                rtDwarfInfo_TryGetSubProgramNameFromSpecRef(pThis, pSubProgram);

            if (pSubProgram->PcRange.cAttrs)
            {
                if (pSubProgram->PcRange.fHaveRanges)
                    Log5(("subprogram %s (%s) <implement ranges>\n", pSubProgram->pszName, pSubProgram->pszLinkageName));
                else
                {
                    Log5(("subprogram %s (%s) %#llx-%#llx%s\n", pSubProgram->pszName, pSubProgram->pszLinkageName,
                          pSubProgram->PcRange.uLowAddress, pSubProgram->PcRange.uHighAddress,
                          pSubProgram->PcRange.cAttrs == 2 ? "" : " !bad!"));
                    if (   ( pSubProgram->pszName || pSubProgram->pszLinkageName)
                        && pSubProgram->PcRange.cAttrs == 2)
                    {
                        if (pThis->iWatcomPass == 1)
                            rc = rtDbgModDwarfRecordSegOffset(pThis, pSubProgram->uSegment, pSubProgram->PcRange.uHighAddress);
                        else
                        {
                            RTDBGSEGIDX iSeg;
                            RTUINTPTR   offSeg;
                            rc = rtDbgModDwarfLinkAddressToSegOffset(pThis, pSubProgram->uSegment,
                                                                     pSubProgram->PcRange.uLowAddress,
                                                                     &iSeg, &offSeg);
                            if (RT_SUCCESS(rc))
                            {
                                uint64_t cb;
                                if (pSubProgram->PcRange.uHighAddress >= pSubProgram->PcRange.uLowAddress)
                                    cb = pSubProgram->PcRange.uHighAddress - pSubProgram->PcRange.uLowAddress;
                               else
                                    cb = 1;
                                rc = RTDbgModSymbolAdd(pThis->hCnt,
                                                       rtDwarfInfo_SelectName(pSubProgram->pszName, pSubProgram->pszLinkageName),
                                                       iSeg, offSeg, cb, 0 /*fFlags*/, NULL /*piOrdinal*/);
                                if (RT_FAILURE(rc))
                                {
                                    if (   rc == VERR_DBG_DUPLICATE_SYMBOL
                                        || rc == VERR_DBG_ADDRESS_CONFLICT /** @todo figure why this happens with 10.6.8 mach_kernel, 32-bit. */
                                        )
                                        rc = VINF_SUCCESS;
                                    else
                                        AssertMsgFailed(("%Rrc\n", rc));
                                }
                            }
                            else if (   pSubProgram->PcRange.uLowAddress  == 0 /* see with vmlinux */
                                     && pSubProgram->PcRange.uHighAddress == 0)
                            {
                                Log5(("rtDbgModDwarfLinkAddressToSegOffset: Ignoring empty range.\n"));
                                rc = VINF_SUCCESS; /* ignore */
                            }
                            else
                            {
                                AssertRC(rc);
                                Log5(("rtDbgModDwarfLinkAddressToSegOffset failed: %Rrc\n", rc));
                            }
                        }
                    }
                }
            }
            else
                Log5(("subprogram %s (%s) external\n", pSubProgram->pszName, pSubProgram->pszLinkageName));
            break;
        }

        case DW_TAG_label:
        {
            PCRTDWARFDIELABEL pLabel = (PCRTDWARFDIELABEL)pDie;
            //if (pLabel->fExternal)
            {
                Log5(("label %s %#x:%#llx\n", pLabel->pszName, pLabel->uSegment, pLabel->Address.uAddress));
                if (pThis->iWatcomPass == 1)
                    rc = rtDbgModDwarfRecordSegOffset(pThis, pLabel->uSegment, pLabel->Address.uAddress);
                else if (pLabel->pszName && *pLabel->pszName != '\0') /* Seen empty labels with isolinux. */
                {
                    RTDBGSEGIDX iSeg;
                    RTUINTPTR   offSeg;
                    rc = rtDbgModDwarfLinkAddressToSegOffset(pThis, pLabel->uSegment, pLabel->Address.uAddress,
                                                             &iSeg, &offSeg);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTDbgModSymbolAdd(pThis->hCnt, pLabel->pszName, iSeg, offSeg, 0 /*cb*/,
                                               0 /*fFlags*/, NULL /*piOrdinal*/);
                        AssertMsg(RT_SUCCESS(rc) || rc == VERR_DBG_ADDRESS_CONFLICT,
                                  ("%Rrc %s %x:%x\n", rc, pLabel->pszName, iSeg, offSeg));
                    }
                    else
                        Log5(("rtDbgModDwarfLinkAddressToSegOffset failed: %Rrc\n", rc));

                    /* Ignore errors regarding local labels. */
                    if (RT_FAILURE(rc) && !pLabel->fExternal)
                        rc = -rc;
                }

            }
            break;
        }

    }
    return rc;
}


/**
 * Initializes the non-core fields of an internal  DIE structure.
 *
 * @param   pDie                The DIE structure.
 * @param   pDieDesc            The DIE descriptor.
 */
static void rtDwarfInfo_InitDie(PRTDWARFDIE pDie, PCRTDWARFDIEDESC pDieDesc)
{
    size_t i = pDieDesc->cAttributes;
    while (i-- > 0)
    {
        switch (pDieDesc->paAttributes[i].cbInit & ATTR_INIT_MASK)
        {
            case ATTR_INIT_ZERO:
                /* Nothing to do (RTMemAllocZ). */
                break;

            case ATTR_INIT_FFFS:
                switch (pDieDesc->paAttributes[i].cbInit & ATTR_SIZE_MASK)
                {
                    case 1:
                        *(uint8_t *)((uintptr_t)pDie + pDieDesc->paAttributes[i].off)  = UINT8_MAX;
                        break;
                    case 2:
                        *(uint16_t *)((uintptr_t)pDie + pDieDesc->paAttributes[i].off) = UINT16_MAX;
                        break;
                    case 4:
                        *(uint32_t *)((uintptr_t)pDie + pDieDesc->paAttributes[i].off) = UINT32_MAX;
                        break;
                    case 8:
                        *(uint64_t *)((uintptr_t)pDie + pDieDesc->paAttributes[i].off) = UINT64_MAX;
                        break;
                    default:
                        AssertFailed();
                        memset((uint8_t *)pDie + pDieDesc->paAttributes[i].off, 0xff,
                               pDieDesc->paAttributes[i].cbInit & ATTR_SIZE_MASK);
                        break;
                }
                break;

            default:
                AssertFailed();
        }
    }
}


/**
 * Creates a new internal DIE structure and links it up.
 *
 * @returns Pointer to the new DIE structure.
 * @param   pThis               The DWARF instance.
 * @param   pDieDesc            The DIE descriptor (for size and init).
 * @param   pAbbrev             The abbreviation cache entry.
 * @param   pParent             The parent DIE (NULL if unit).
 */
static PRTDWARFDIE rtDwarfInfo_NewDie(PRTDBGMODDWARF pThis, PCRTDWARFDIEDESC pDieDesc,
                                      PCRTDWARFABBREV pAbbrev, PRTDWARFDIE pParent)
{
    NOREF(pThis);
    Assert(pDieDesc->cbDie >= sizeof(RTDWARFDIE));
#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
    uint32_t iAllocator = pDieDesc->cbDie > pThis->aDieAllocators[0].cbMax;
    Assert(pDieDesc->cbDie <= pThis->aDieAllocators[iAllocator].cbMax);
    PRTDWARFDIE pDie = (PRTDWARFDIE)RTMemCacheAlloc(pThis->aDieAllocators[iAllocator].hMemCache);
#else
    PRTDWARFDIE pDie = (PRTDWARFDIE)RTMemAllocZ(pDieDesc->cbDie);
#endif
    if (pDie)
    {
#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
        RT_BZERO(pDie, pDieDesc->cbDie);
        pDie->iAllocator   = iAllocator;
#endif
        rtDwarfInfo_InitDie(pDie, pDieDesc);

        pDie->uTag         = pAbbrev->uTag;
        pDie->offSpec      = pAbbrev->offSpec;
        pDie->pParent      = pParent;
        if (pParent)
            RTListAppend(&pParent->ChildList, &pDie->SiblingNode);
        else
            RTListInit(&pDie->SiblingNode);
        RTListInit(&pDie->ChildList);

    }
    return pDie;
}


/**
 * Free all children of a DIE.
 *
 * @param   pThis               The DWARF instance.
 * @param   pParentDie          The parent DIE.
 */
static void rtDwarfInfo_FreeChildren(PRTDBGMODDWARF pThis, PRTDWARFDIE pParentDie)
{
    PRTDWARFDIE pChild, pNextChild;
    RTListForEachSafe(&pParentDie->ChildList, pChild, pNextChild, RTDWARFDIE, SiblingNode)
    {
        if (!RTListIsEmpty(&pChild->ChildList))
            rtDwarfInfo_FreeChildren(pThis, pChild);
        RTListNodeRemove(&pChild->SiblingNode);
#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
        RTMemCacheFree(pThis->aDieAllocators[pChild->iAllocator].hMemCache, pChild);
#else
        RTMemFree(pChild);
#endif
    }
}


/**
 * Free a DIE an all its children.
 *
 * @param   pThis               The DWARF instance.
 * @param   pDie                The DIE to free.
 */
static void rtDwarfInfo_FreeDie(PRTDBGMODDWARF pThis, PRTDWARFDIE pDie)
{
    rtDwarfInfo_FreeChildren(pThis, pDie);
    RTListNodeRemove(&pDie->SiblingNode);
#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
    RTMemCacheFree(pThis->aDieAllocators[pDie->iAllocator].hMemCache, pDie);
#else
    RTMemFree(pChild);
#endif
}


/**
 * Skips a form.
 * @returns IPRT status code
 * @param   pCursor             The cursor.
 * @param   uForm               The form to skip.
 */
static int rtDwarfInfo_SkipForm(PRTDWARFCURSOR pCursor, uint32_t uForm)
{
    switch (uForm)
    {
        case DW_FORM_addr:
            return rtDwarfCursor_SkipBytes(pCursor, pCursor->cbNativeAddr);

        case DW_FORM_block:
        case DW_FORM_exprloc:
            return rtDwarfCursor_SkipBytes(pCursor, rtDwarfCursor_GetULeb128(pCursor, 0));

        case DW_FORM_block1:
            return rtDwarfCursor_SkipBytes(pCursor, rtDwarfCursor_GetU8(pCursor, 0));

        case DW_FORM_block2:
            return rtDwarfCursor_SkipBytes(pCursor, rtDwarfCursor_GetU16(pCursor, 0));

        case DW_FORM_block4:
            return rtDwarfCursor_SkipBytes(pCursor, rtDwarfCursor_GetU32(pCursor, 0));

        case DW_FORM_data1:
        case DW_FORM_ref1:
        case DW_FORM_flag:
            return rtDwarfCursor_SkipBytes(pCursor, 1);

        case DW_FORM_data2:
        case DW_FORM_ref2:
            return rtDwarfCursor_SkipBytes(pCursor, 2);

        case DW_FORM_data4:
        case DW_FORM_ref4:
            return rtDwarfCursor_SkipBytes(pCursor, 4);

        case DW_FORM_data8:
        case DW_FORM_ref8:
        case DW_FORM_ref_sig8:
            return rtDwarfCursor_SkipBytes(pCursor, 8);

        case DW_FORM_udata:
        case DW_FORM_sdata:
        case DW_FORM_ref_udata:
            return rtDwarfCursor_SkipLeb128(pCursor);

        case DW_FORM_string:
            rtDwarfCursor_GetSZ(pCursor, NULL);
            return pCursor->rc;

        case DW_FORM_indirect:
            return rtDwarfInfo_SkipForm(pCursor, rtDwarfCursor_GetULeb128AsU32(pCursor, UINT32_MAX));

        case DW_FORM_strp:
        case DW_FORM_ref_addr:
        case DW_FORM_sec_offset:
            return rtDwarfCursor_SkipBytes(pCursor, pCursor->f64bitDwarf ? 8 : 4);

        case DW_FORM_flag_present:
            return pCursor->rc; /* no data */

        default:
            Log(("rtDwarfInfo_SkipForm: Unknown form %#x\n", uForm));
            return VERR_DWARF_UNKNOWN_FORM;
    }
}



#ifdef SOME_UNUSED_FUNCTION
/**
 * Skips a DIE.
 *
 * @returns IPRT status code.
 * @param   pCursor         The cursor.
 * @param   pAbbrevCursor   The abbreviation cursor.
 */
static int rtDwarfInfo_SkipDie(PRTDWARFCURSOR pCursor, PRTDWARFCURSOR pAbbrevCursor)
{
    for (;;)
    {
        uint32_t uAttr = rtDwarfCursor_GetULeb128AsU32(pAbbrevCursor, 0);
        uint32_t uForm = rtDwarfCursor_GetULeb128AsU32(pAbbrevCursor, 0);
        if (uAttr == 0 && uForm == 0)
            break;

        int rc = rtDwarfInfo_SkipForm(pCursor, uForm);
        if (RT_FAILURE(rc))
            return rc;
    }
    return RT_FAILURE(pCursor->rc) ? pCursor->rc : pAbbrevCursor->rc;
}
#endif


/**
 * Parse the attributes of a DIE.
 *
 * @returns IPRT status code.
 * @param   pThis               The DWARF instance.
 * @param   pDie                The internal DIE structure to fill.
 * @param   pDieDesc            The DIE descriptor.
 * @param   pCursor             The debug_info cursor.
 * @param   pAbbrev             The abbreviation cache entry.
 * @param   fInitDie            Whether to initialize the DIE first.  If not (@c
 *                              false) it's safe to assume we're following a
 *                              DW_AT_specification or DW_AT_abstract_origin,
 *                              and that we shouldn't be snooping any symbols.
 */
static int rtDwarfInfo_ParseDie(PRTDBGMODDWARF pThis, PRTDWARFDIE pDie, PCRTDWARFDIEDESC pDieDesc,
                                PRTDWARFCURSOR pCursor, PCRTDWARFABBREV pAbbrev, bool fInitDie)
{
    RTDWARFCURSOR AbbrevCursor;
    int rc = rtDwarfCursor_InitWithOffset(&AbbrevCursor, pThis, krtDbgModDwarfSect_abbrev, pAbbrev->offSpec);
    if (RT_FAILURE(rc))
        return rc;

    if (fInitDie)
        rtDwarfInfo_InitDie(pDie, pDieDesc);
    for (;;)
    {
#ifdef LOG_ENABLED
        uint32_t const off = (uint32_t)(AbbrevCursor.pb - AbbrevCursor.pbStart);
#endif
        uint32_t uAttr = rtDwarfCursor_GetULeb128AsU32(&AbbrevCursor, 0);
        uint32_t uForm = rtDwarfCursor_GetULeb128AsU32(&AbbrevCursor, 0);
        Log4(("    %04x: %-23s [%s]\n", off, rtDwarfLog_AttrName(uAttr), rtDwarfLog_FormName(uForm)));
        if (uAttr == 0)
            break;
        if (uForm == DW_FORM_indirect)
            uForm = rtDwarfCursor_GetULeb128AsU32(pCursor, 0);

        /* Look up the attribute in the descriptor and invoke the decoder. */
        PCRTDWARFATTRDESC pAttr = NULL;
        size_t i = pDieDesc->cAttributes;
        while (i-- > 0)
            if (pDieDesc->paAttributes[i].uAttr == uAttr)
            {
                pAttr = &pDieDesc->paAttributes[i];
                rc = pAttr->pfnDecoder(pDie, (uint8_t *)pDie + pAttr->off, pAttr, uForm, pCursor);
                break;
            }

        /* Some house keeping. */
        if (pAttr)
            pDie->cDecodedAttrs++;
        else
        {
            pDie->cUnhandledAttrs++;
            rc = rtDwarfInfo_SkipForm(pCursor, uForm);
        }
        if (RT_FAILURE(rc))
            break;
    }

    rc = rtDwarfCursor_Delete(&AbbrevCursor, rc);
    if (RT_SUCCESS(rc))
        rc = pCursor->rc;

    /*
     * Snoop up symbols on the way out.
     */
    if (RT_SUCCESS(rc) && fInitDie)
    {
        rc = rtDwarfInfo_SnoopSymbols(pThis, pDie);
        /* Ignore duplicates, get work done instead. */
        /** @todo clean up global/static symbol mess. */
        if (rc == VERR_DBG_DUPLICATE_SYMBOL || rc == VERR_DBG_ADDRESS_CONFLICT)
            rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Load the debug information of a unit.
 *
 * @returns IPRT status code.
 * @param   pThis               The DWARF instance.
 * @param   pCursor             The debug_info cursor.
 * @param   fKeepDies           Whether to keep the DIEs or discard them as soon
 *                              as possible.
 */
static int rtDwarfInfo_LoadUnit(PRTDBGMODDWARF pThis, PRTDWARFCURSOR pCursor, bool fKeepDies)
{
    Log(("rtDwarfInfo_LoadUnit: %#x\n", rtDwarfCursor_CalcSectOffsetU32(pCursor)));

    /*
     * Read the compilation unit header.
     */
    uint64_t offUnit = rtDwarfCursor_CalcSectOffsetU32(pCursor);
    uint64_t cbUnit  = rtDwarfCursor_GetInitialLength(pCursor);
    cbUnit += rtDwarfCursor_CalcSectOffsetU32(pCursor) - offUnit;
    uint16_t const uVer = rtDwarfCursor_GetUHalf(pCursor, 0);
    if (   uVer < 2
        || uVer > 4)
        return rtDwarfCursor_SkipUnit(pCursor);
    uint64_t const offAbbrev    = rtDwarfCursor_GetUOff(pCursor, UINT64_MAX);
    uint8_t  const cbNativeAddr = rtDwarfCursor_GetU8(pCursor, UINT8_MAX);
    if (RT_FAILURE(pCursor->rc))
        return pCursor->rc;
    Log(("   uVer=%d  offAbbrev=%#llx cbNativeAddr=%d\n", uVer, offAbbrev, cbNativeAddr));

    /*
     * Set up the abbreviation cache and store the native address size in the cursor.
     */
    if (offAbbrev > UINT32_MAX)
    {
        Log(("Unexpected abbrviation code offset of %#llx\n", offAbbrev));
        return VERR_DWARF_BAD_INFO;
    }
    rtDwarfAbbrev_SetUnitOffset(pThis, (uint32_t)offAbbrev);
    pCursor->cbNativeAddr = cbNativeAddr;

    /*
     * The first DIE is a compile or partial unit, parse it here.
     */
    uint32_t uAbbrCode = rtDwarfCursor_GetULeb128AsU32(pCursor, UINT32_MAX);
    if (!uAbbrCode)
    {
        Log(("Unexpected abbrviation code of zero\n"));
        return VERR_DWARF_BAD_INFO;
    }
    PCRTDWARFABBREV pAbbrev = rtDwarfAbbrev_Lookup(pThis, uAbbrCode);
    if (!pAbbrev)
        return VERR_DWARF_ABBREV_NOT_FOUND;
    if (   pAbbrev->uTag != DW_TAG_compile_unit
        && pAbbrev->uTag != DW_TAG_partial_unit)
    {
        Log(("Unexpected compile/partial unit tag %#x\n", pAbbrev->uTag));
        return VERR_DWARF_BAD_INFO;
    }

    PRTDWARFDIECOMPILEUNIT pUnit;
    pUnit = (PRTDWARFDIECOMPILEUNIT)rtDwarfInfo_NewDie(pThis, &g_CompileUnitDesc, pAbbrev, NULL /*pParent*/);
    if (!pUnit)
        return VERR_NO_MEMORY;
    pUnit->offUnit      = offUnit;
    pUnit->cbUnit       = cbUnit;
    pUnit->offAbbrev    = offAbbrev;
    pUnit->cbNativeAddr = cbNativeAddr;
    pUnit->uDwarfVer    = (uint8_t)uVer;
    RTListAppend(&pThis->CompileUnitList, &pUnit->Core.SiblingNode);

    int rc = rtDwarfInfo_ParseDie(pThis, &pUnit->Core, &g_CompileUnitDesc, pCursor, pAbbrev, true /*fInitDie*/);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Parse DIEs.
     */
    uint32_t    cDepth     = 0;
    PRTDWARFDIE pParentDie = &pUnit->Core;
    while (!rtDwarfCursor_IsAtEndOfUnit(pCursor))
    {
#ifdef LOG_ENABLED
        uint32_t offLog = rtDwarfCursor_CalcSectOffsetU32(pCursor);
#endif
        uAbbrCode = rtDwarfCursor_GetULeb128AsU32(pCursor, UINT32_MAX);
        if (!uAbbrCode)
        {
            /* End of siblings, up one level. (Is this correct?) */
            if (pParentDie->pParent)
            {
                pParentDie = pParentDie->pParent;
                cDepth--;
                if (!fKeepDies && pParentDie->pParent)
                    rtDwarfInfo_FreeChildren(pThis, pParentDie);
            }
        }
        else
        {
            /*
             * Look up the abbreviation and match the tag up with a descriptor.
             */
            pAbbrev = rtDwarfAbbrev_Lookup(pThis, uAbbrCode);
            if (!pAbbrev)
                return VERR_DWARF_ABBREV_NOT_FOUND;

            PCRTDWARFDIEDESC pDieDesc;
            const char      *pszName;
            if (pAbbrev->uTag < RT_ELEMENTS(g_aTagDescs))
            {
                Assert(g_aTagDescs[pAbbrev->uTag].uTag == pAbbrev->uTag || g_aTagDescs[pAbbrev->uTag].uTag == 0);
                pszName  = g_aTagDescs[pAbbrev->uTag].pszName;
                pDieDesc = g_aTagDescs[pAbbrev->uTag].pDesc;
            }
            else
            {
                pszName  = "<unknown>";
                pDieDesc = &g_CoreDieDesc;
            }
            Log4(("%08x: %*stag=%s (%#x, abbrev %u @ %#x)%s\n", offLog, cDepth * 2, "", pszName,
                  pAbbrev->uTag, uAbbrCode, pAbbrev->offSpec - pAbbrev->cbHdr, pAbbrev->fChildren ? " has children" : ""));

            /*
             * Create a new internal DIE structure and parse the
             * attributes.
             */
            PRTDWARFDIE pNewDie = rtDwarfInfo_NewDie(pThis, pDieDesc, pAbbrev, pParentDie);
            if (!pNewDie)
                return VERR_NO_MEMORY;

            if (pAbbrev->fChildren)
            {
                pParentDie = pNewDie;
                cDepth++;
            }

            rc = rtDwarfInfo_ParseDie(pThis, pNewDie, pDieDesc, pCursor, pAbbrev, true /*fInitDie*/);
            if (RT_FAILURE(rc))
                return rc;

            if (!fKeepDies && !pAbbrev->fChildren)
                rtDwarfInfo_FreeDie(pThis, pNewDie);
        }
    } /* while more DIEs */


    /* Unlink and free child DIEs if told to do so. */
    if (!fKeepDies)
        rtDwarfInfo_FreeChildren(pThis, &pUnit->Core);

    return RT_SUCCESS(rc) ? pCursor->rc : rc;
}


/**
 * Extracts the symbols.
 *
 * The symbols are insered into the debug info container.
 *
 * @returns IPRT status code
 * @param   pThis               The DWARF instance.
 */
static int rtDwarfInfo_LoadAll(PRTDBGMODDWARF pThis)
{
    RTDWARFCURSOR Cursor;
    int rc = rtDwarfCursor_Init(&Cursor, pThis, krtDbgModDwarfSect_info);
    if (RT_SUCCESS(rc))
    {
        while (   !rtDwarfCursor_IsAtEnd(&Cursor)
               && RT_SUCCESS(rc))
            rc = rtDwarfInfo_LoadUnit(pThis, &Cursor, false /* fKeepDies */);

        rc = rtDwarfCursor_Delete(&Cursor, rc);
    }
    return rc;
}



/*
 *
 * Public and image level symbol handling.
 * Public and image level symbol handling.
 * Public and image level symbol handling.
 * Public and image level symbol handling.
 *
 *
 */

#define RTDBGDWARF_SYM_ENUM_BASE_ADDRESS  UINT32_C(0x200000)

/** @callback_method_impl{FNRTLDRENUMSYMS,
 *  Adds missing symbols from the image symbol table.} */
static DECLCALLBACK(int) rtDwarfSyms_EnumSymbolsCallback(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol,
                                                         RTLDRADDR Value, void *pvUser)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pvUser;
    RT_NOREF_PV(hLdrMod); RT_NOREF_PV(uSymbol);
    Assert(pThis->iWatcomPass != 1);

    RTLDRADDR uRva = Value - RTDBGDWARF_SYM_ENUM_BASE_ADDRESS;
    if (   Value >= RTDBGDWARF_SYM_ENUM_BASE_ADDRESS
        && uRva  <  _1G)
    {
        RTDBGSYMBOL SymInfo;
        RTINTPTR    offDisp;
        int rc = RTDbgModSymbolByAddr(pThis->hCnt, RTDBGSEGIDX_RVA, uRva, RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL, &offDisp, &SymInfo);
        if (   RT_FAILURE(rc)
            || offDisp != 0)
        {
            rc = RTDbgModSymbolAdd(pThis->hCnt, pszSymbol, RTDBGSEGIDX_RVA, uRva, 1, 0 /*fFlags*/, NULL /*piOrdinal*/);
            Log(("Dwarf: Symbol #%05u %#018RTptr %s [%Rrc]\n", uSymbol, Value, pszSymbol, rc)); NOREF(rc);
        }
    }
    else
        Log(("Dwarf: Symbol #%05u %#018RTptr '%s' [SKIPPED - INVALID ADDRESS]\n", uSymbol, Value, pszSymbol));
    return VINF_SUCCESS;
}



/**
 * Loads additional symbols from the pubnames section and the executable image.
 *
 * The symbols are insered into the debug info container.
 *
 * @returns IPRT status code
 * @param   pThis               The DWARF instance.
 */
static int rtDwarfSyms_LoadAll(PRTDBGMODDWARF pThis)
{
    /*
     * pubnames.
     */
    int rc = VINF_SUCCESS;
    if (pThis->aSections[krtDbgModDwarfSect_pubnames].fPresent)
    {
//        RTDWARFCURSOR Cursor;
//        int rc = rtDwarfCursor_Init(&Cursor, pThis, krtDbgModDwarfSect_info);
//        if (RT_SUCCESS(rc))
//        {
//            while (   !rtDwarfCursor_IsAtEnd(&Cursor)
//                   && RT_SUCCESS(rc))
//                rc = rtDwarfInfo_LoadUnit(pThis, &Cursor, false /* fKeepDies */);
//
//            rc = rtDwarfCursor_Delete(&Cursor, rc);
//        }
//        return rc;
    }

    /*
     * The executable image.
     */
    if (   pThis->pImgMod
        && pThis->pImgMod->pImgVt->pfnEnumSymbols
        && pThis->iWatcomPass != 1
        && RT_SUCCESS(rc))
    {
        rc = pThis->pImgMod->pImgVt->pfnEnumSymbols(pThis->pImgMod,
                                                    RTLDR_ENUM_SYMBOL_FLAGS_ALL | RTLDR_ENUM_SYMBOL_FLAGS_NO_FWD,
                                                    RTDBGDWARF_SYM_ENUM_BASE_ADDRESS,
                                                    rtDwarfSyms_EnumSymbolsCallback,
                                                    pThis);
    }

    return rc;
}




/*
 *
 * DWARF Debug module implementation.
 * DWARF Debug module implementation.
 * DWARF Debug module implementation.
 *
 */


/** @interface_method_impl{RTDBGMODVTDBG,pfnUnwindFrame} */
static DECLCALLBACK(int) rtDbgModDwarf_UnwindFrame(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;

    /*
     * Unwinding info is stored in the '.debug_frame' section, or altertively
     * in the '.eh_frame' one in the image.  In the latter case the dbgmodldr.cpp
     * part of the operation will take care of it.  Since the sections contain the
     * same data, we just create a cursor and call a common function to do the job.
     */
    if (pThis->aSections[krtDbgModDwarfSect_frame].fPresent)
    {
        RTDWARFCURSOR Cursor;
        int rc = rtDwarfCursor_Init(&Cursor, pThis, krtDbgModDwarfSect_frame);
        if (RT_SUCCESS(rc))
        {
            /* Figure default pointer encoding from image arch. */
            uint8_t bPtrEnc = rtDwarfUnwind_ArchToPtrEnc(pMod->pImgVt->pfnGetArch(pMod));

            /* Make sure we've got both seg:off and rva for the input address. */
            RTUINTPTR uRva = off;
            if (iSeg == RTDBGSEGIDX_RVA)
                rtDbgModDwarfRvaToSegOffset(pThis, uRva, &iSeg, &off);
            else
                rtDbgModDwarfSegOffsetToRva(pThis, iSeg, off, &uRva);

            /* Do the work */
            rc = rtDwarfUnwind_Slow(&Cursor, 0 /** @todo .debug_frame RVA*/, iSeg, off, uRva,
                                    pState, bPtrEnc, false /*fIsEhFrame*/, pMod->pImgVt->pfnGetArch(pMod));

            rc = rtDwarfCursor_Delete(&Cursor, rc);
        }
        return rc;
    }
    return VERR_DBG_NO_UNWIND_INFO;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByAddr} */
static DECLCALLBACK(int) rtDbgModDwarf_LineByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off,
                                                  PRTINTPTR poffDisp, PRTDBGLINE pLineInfo)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    return RTDbgModLineByAddr(pThis->hCnt, iSeg, off, poffDisp, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineByOrdinal} */
static DECLCALLBACK(int) rtDbgModDwarf_LineByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGLINE pLineInfo)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    return RTDbgModLineByOrdinal(pThis->hCnt, iOrdinal, pLineInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineCount} */
static DECLCALLBACK(uint32_t) rtDbgModDwarf_LineCount(PRTDBGMODINT pMod)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    return RTDbgModLineCount(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnLineAdd} */
static DECLCALLBACK(int) rtDbgModDwarf_LineAdd(PRTDBGMODINT pMod, const char *pszFile, size_t cchFile, uint32_t uLineNo,
                                               uint32_t iSeg, RTUINTPTR off, uint32_t *piOrdinal)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    Assert(!pszFile[cchFile]); NOREF(cchFile);
    return RTDbgModLineAdd(pThis->hCnt, pszFile, uLineNo, iSeg, off, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByAddr} */
static DECLCALLBACK(int) rtDbgModDwarf_SymbolByAddr(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, RTUINTPTR off, uint32_t fFlags,
                                                    PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    return RTDbgModSymbolByAddr(pThis->hCnt, iSeg, off, fFlags, poffDisp, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByName} */
static DECLCALLBACK(int) rtDbgModDwarf_SymbolByName(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                    PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); RT_NOREF_PV(cchSymbol);
    return RTDbgModSymbolByName(pThis->hCnt, pszSymbol/*, cchSymbol*/, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolByOrdinal} */
static DECLCALLBACK(int) rtDbgModDwarf_SymbolByOrdinal(PRTDBGMODINT pMod, uint32_t iOrdinal, PRTDBGSYMBOL pSymInfo)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    return RTDbgModSymbolByOrdinal(pThis->hCnt, iOrdinal, pSymInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolCount} */
static DECLCALLBACK(uint32_t) rtDbgModDwarf_SymbolCount(PRTDBGMODINT pMod)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    return RTDbgModSymbolCount(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSymbolAdd} */
static DECLCALLBACK(int) rtDbgModDwarf_SymbolAdd(PRTDBGMODINT pMod, const char *pszSymbol, size_t cchSymbol,
                                                 RTDBGSEGIDX iSeg, RTUINTPTR off, RTUINTPTR cb, uint32_t fFlags,
                                                 uint32_t *piOrdinal)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    Assert(!pszSymbol[cchSymbol]); NOREF(cchSymbol);
    return RTDbgModSymbolAdd(pThis->hCnt, pszSymbol, iSeg, off, cb, fFlags, piOrdinal);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentByIndex} */
static DECLCALLBACK(int) rtDbgModDwarf_SegmentByIndex(PRTDBGMODINT pMod, RTDBGSEGIDX iSeg, PRTDBGSEGMENT pSegInfo)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    return RTDbgModSegmentByIndex(pThis->hCnt, iSeg, pSegInfo);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentCount} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModDwarf_SegmentCount(PRTDBGMODINT pMod)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    return RTDbgModSegmentCount(pThis->hCnt);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnSegmentAdd} */
static DECLCALLBACK(int) rtDbgModDwarf_SegmentAdd(PRTDBGMODINT pMod, RTUINTPTR uRva, RTUINTPTR cb, const char *pszName, size_t cchName,
                                                  uint32_t fFlags, PRTDBGSEGIDX piSeg)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    Assert(!pszName[cchName]); NOREF(cchName);
    return RTDbgModSegmentAdd(pThis->hCnt, uRva, cb, pszName, fFlags, piSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnImageSize} */
static DECLCALLBACK(RTUINTPTR) rtDbgModDwarf_ImageSize(PRTDBGMODINT pMod)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    RTUINTPTR cb1 = RTDbgModImageSize(pThis->hCnt);
    RTUINTPTR cb2 = pThis->pImgMod->pImgVt->pfnImageSize(pMod);
    return RT_MAX(cb1, cb2);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnRvaToSegOff} */
static DECLCALLBACK(RTDBGSEGIDX) rtDbgModDwarf_RvaToSegOff(PRTDBGMODINT pMod, RTUINTPTR uRva, PRTUINTPTR poffSeg)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;
    return RTDbgModRvaToSegOff(pThis->hCnt, uRva, poffSeg);
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnClose} */
static DECLCALLBACK(int) rtDbgModDwarf_Close(PRTDBGMODINT pMod)
{
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pMod->pvDbgPriv;

    for (unsigned iSect = 0; iSect < RT_ELEMENTS(pThis->aSections); iSect++)
        if (pThis->aSections[iSect].pv)
            pThis->pDbgInfoMod->pImgVt->pfnUnmapPart(pThis->pDbgInfoMod, pThis->aSections[iSect].cb, &pThis->aSections[iSect].pv);

    RTDbgModRelease(pThis->hCnt);
    RTMemFree(pThis->paCachedAbbrevs);
    if (pThis->pNestedMod)
    {
        pThis->pNestedMod->pImgVt->pfnClose(pThis->pNestedMod);
        RTStrCacheRelease(g_hDbgModStrCache, pThis->pNestedMod->pszName);
        RTStrCacheRelease(g_hDbgModStrCache, pThis->pNestedMod->pszDbgFile);
        RTMemFree(pThis->pNestedMod);
        pThis->pNestedMod = NULL;
    }

#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
    uint32_t i = RT_ELEMENTS(pThis->aDieAllocators);
    while (i-- > 0)
    {
        RTMemCacheDestroy(pThis->aDieAllocators[i].hMemCache);
        pThis->aDieAllocators[i].hMemCache = NIL_RTMEMCACHE;
    }
#endif

    RTMemFree(pThis);

    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTLDRENUMDBG} */
static DECLCALLBACK(int) rtDbgModDwarfEnumCallback(RTLDRMOD hLdrMod, PCRTLDRDBGINFO pDbgInfo, void *pvUser)
{
    RT_NOREF_PV(hLdrMod);

    /*
     * Skip stuff we can't handle.
     */
    if (pDbgInfo->enmType != RTLDRDBGINFOTYPE_DWARF)
        return VINF_SUCCESS;
    const char *pszSection = pDbgInfo->u.Dwarf.pszSection;
    if (!pszSection || !*pszSection)
        return VINF_SUCCESS;
    Assert(!pDbgInfo->pszExtFile);

    /*
     * Must have a part name starting with debug_ and possibly prefixed by dots
     * or underscores.
     */
    if (!strncmp(pszSection, RT_STR_TUPLE(".debug_")))       /* ELF */
        pszSection += sizeof(".debug_") - 1;
    else if (!strncmp(pszSection, RT_STR_TUPLE("__debug_"))) /* Mach-O */
        pszSection += sizeof("__debug_") - 1;
    else if (!strcmp(pszSection, ".WATCOM_references"))
        return VINF_SUCCESS; /* Ignore special watcom section for now.*/
    else if (   !strcmp(pszSection, "__apple_types")
             || !strcmp(pszSection, "__apple_namespac")
             || !strcmp(pszSection, "__apple_objc")
             || !strcmp(pszSection, "__apple_names"))
        return VINF_SUCCESS; /* Ignore special apple sections for now. */
    else
        AssertMsgFailedReturn(("%s\n", pszSection), VINF_SUCCESS /*ignore*/);

    /*
     * Figure out which part we're talking about.
     */
    krtDbgModDwarfSect enmSect;
    if (0) { /* dummy */ }
#define ELSE_IF_STRCMP_SET(a_Name) else if (!strcmp(pszSection, #a_Name))  enmSect = krtDbgModDwarfSect_ ## a_Name
    ELSE_IF_STRCMP_SET(abbrev);
    ELSE_IF_STRCMP_SET(aranges);
    ELSE_IF_STRCMP_SET(frame);
    ELSE_IF_STRCMP_SET(info);
    ELSE_IF_STRCMP_SET(inlined);
    ELSE_IF_STRCMP_SET(line);
    ELSE_IF_STRCMP_SET(loc);
    ELSE_IF_STRCMP_SET(macinfo);
    ELSE_IF_STRCMP_SET(pubnames);
    ELSE_IF_STRCMP_SET(pubtypes);
    ELSE_IF_STRCMP_SET(ranges);
    ELSE_IF_STRCMP_SET(str);
    ELSE_IF_STRCMP_SET(types);
#undef ELSE_IF_STRCMP_SET
    else
    {
        AssertMsgFailed(("%s\n", pszSection));
        return VINF_SUCCESS;
    }

    /*
     * Record the section.
     */
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)pvUser;
    AssertMsgReturn(!pThis->aSections[enmSect].fPresent, ("duplicate %s\n", pszSection), VINF_SUCCESS /*ignore*/);

    pThis->aSections[enmSect].fPresent  = true;
    pThis->aSections[enmSect].offFile   = pDbgInfo->offFile;
    pThis->aSections[enmSect].pv        = NULL;
    pThis->aSections[enmSect].cb        = (size_t)pDbgInfo->cb;
    pThis->aSections[enmSect].iDbgInfo  = pDbgInfo->iDbgInfo;
    if (pThis->aSections[enmSect].cb != pDbgInfo->cb)
        pThis->aSections[enmSect].cb    = ~(size_t)0;

    return VINF_SUCCESS;
}


static int rtDbgModDwarfTryOpenDbgFile(PRTDBGMODINT pDbgMod, PRTDBGMODDWARF pThis, RTLDRARCH enmArch)
{
    if (   !pDbgMod->pszDbgFile
        || RTPathIsSame(pDbgMod->pszDbgFile, pDbgMod->pszImgFile) == (int)true /* returns VERR too */)
        return VERR_DBG_NO_MATCHING_INTERPRETER;

    /*
     * Only open the image.
     */
    PRTDBGMODINT pDbgInfoMod = (PRTDBGMODINT)RTMemAllocZ(sizeof(*pDbgInfoMod));
    if (!pDbgInfoMod)
        return VERR_NO_MEMORY;

    int rc;
    pDbgInfoMod->u32Magic     = RTDBGMOD_MAGIC;
    pDbgInfoMod->cRefs        = 1;
    if (RTStrCacheRetain(pDbgMod->pszDbgFile) != UINT32_MAX)
    {
        pDbgInfoMod->pszImgFile = pDbgMod->pszDbgFile;
        if (RTStrCacheRetain(pDbgMod->pszName) != UINT32_MAX)
        {
            pDbgInfoMod->pszName = pDbgMod->pszName;
            pDbgInfoMod->pImgVt  = &g_rtDbgModVtImgLdr;
            rc = pDbgInfoMod->pImgVt->pfnTryOpen(pDbgInfoMod, enmArch, 0 /*fLdrFlags*/);
            if (RT_SUCCESS(rc))
            {
                pThis->pDbgInfoMod = pDbgInfoMod;
                pThis->pNestedMod  = pDbgInfoMod;
                return VINF_SUCCESS;
            }

            RTStrCacheRelease(g_hDbgModStrCache, pDbgInfoMod->pszName);
        }
        else
            rc = VERR_NO_STR_MEMORY;
        RTStrCacheRelease(g_hDbgModStrCache,  pDbgInfoMod->pszImgFile);
    }
    else
        rc = VERR_NO_STR_MEMORY;
    RTMemFree(pDbgInfoMod);
    return rc;
}


/** @interface_method_impl{RTDBGMODVTDBG,pfnTryOpen} */
static DECLCALLBACK(int) rtDbgModDwarf_TryOpen(PRTDBGMODINT pMod, RTLDRARCH enmArch, RTDBGCFG hDbgCfg)
{
    RT_NOREF_PV(hDbgCfg);

    /*
     * DWARF is only supported when part of an image.
     */
    if (!pMod->pImgVt)
        return VERR_DBG_NO_MATCHING_INTERPRETER;

    /*
     * Create the module instance data.
     */
    PRTDBGMODDWARF pThis = (PRTDBGMODDWARF)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    pThis->pDbgInfoMod = pMod;
    pThis->pImgMod     = pMod;
    RTListInit(&pThis->CompileUnitList);

    /** @todo better fUseLinkAddress heuristics! */
    /* mach_kernel: */
    if (   (pMod->pszDbgFile          && strstr(pMod->pszDbgFile,          "mach_kernel"))
        || (pMod->pszImgFile          && strstr(pMod->pszImgFile,          "mach_kernel"))
        || (pMod->pszImgFileSpecified && strstr(pMod->pszImgFileSpecified, "mach_kernel")) )
        pThis->fUseLinkAddress = true;

#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
    AssertCompile(RT_ELEMENTS(pThis->aDieAllocators) == 2);
    pThis->aDieAllocators[0].cbMax = sizeof(RTDWARFDIE);
    pThis->aDieAllocators[1].cbMax = sizeof(RTDWARFDIECOMPILEUNIT);
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aTagDescs); i++)
        if (g_aTagDescs[i].pDesc && g_aTagDescs[i].pDesc->cbDie > pThis->aDieAllocators[1].cbMax)
            pThis->aDieAllocators[1].cbMax = (uint32_t)g_aTagDescs[i].pDesc->cbDie;
    pThis->aDieAllocators[1].cbMax = RT_ALIGN_32(pThis->aDieAllocators[1].cbMax, sizeof(uint64_t));

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aDieAllocators); i++)
    {
        int rc = RTMemCacheCreate(&pThis->aDieAllocators[i].hMemCache, pThis->aDieAllocators[i].cbMax, sizeof(uint64_t),
                                  UINT32_MAX, NULL /*pfnCtor*/, NULL /*pfnDtor*/, NULL /*pvUser*/, 0 /*fFlags*/);
        if (RT_FAILURE(rc))
        {
            while (i-- > 0)
                RTMemCacheDestroy(pThis->aDieAllocators[i].hMemCache);
            RTMemFree(pThis);
            return rc;
        }
    }
#endif

    /*
     * If the debug file name is set, let's see if it's an ELF image with DWARF
     * inside it. In that case we'll have to deal with two image modules, one
     * for segments and address translation and one for the debug information.
     */
    if (pMod->pszDbgFile != NULL)
        rtDbgModDwarfTryOpenDbgFile(pMod, pThis, enmArch);

    /*
     * Enumerate the debug info in the module, looking for DWARF bits.
     */
    int rc = pThis->pDbgInfoMod->pImgVt->pfnEnumDbgInfo(pThis->pDbgInfoMod, rtDbgModDwarfEnumCallback, pThis);
    if (RT_SUCCESS(rc))
    {
        if (pThis->aSections[krtDbgModDwarfSect_info].fPresent)
        {
            /*
             * Extract / explode the data we want (symbols and line numbers)
             * storing them in a container module.
             */
            rc = RTDbgModCreate(&pThis->hCnt, pMod->pszName, 0 /*cbSeg*/, 0 /*fFlags*/);
            if (RT_SUCCESS(rc))
            {
                pMod->pvDbgPriv = pThis;

                rc = rtDbgModDwarfAddSegmentsFromImage(pThis);
                if (RT_SUCCESS(rc))
                    rc = rtDwarfInfo_LoadAll(pThis);
                if (RT_SUCCESS(rc))
                    rc = rtDwarfSyms_LoadAll(pThis);
                if (RT_SUCCESS(rc))
                    rc = rtDwarfLine_ExplodeAll(pThis);
                if (RT_SUCCESS(rc) && pThis->iWatcomPass == 1)
                {
                    rc = rtDbgModDwarfAddSegmentsFromPass1(pThis);
                    pThis->iWatcomPass = 2;
                    if (RT_SUCCESS(rc))
                        rc = rtDwarfInfo_LoadAll(pThis);
                    if (RT_SUCCESS(rc))
                        rc = rtDwarfSyms_LoadAll(pThis);
                    if (RT_SUCCESS(rc))
                        rc = rtDwarfLine_ExplodeAll(pThis);
                }

                /*
                 * Free the cached abbreviations and unload all sections.
                 */
                pThis->cCachedAbbrevsAlloced = 0;
                RTMemFree(pThis->paCachedAbbrevs);
                pThis->paCachedAbbrevs = NULL;

                for (unsigned iSect = 0; iSect < RT_ELEMENTS(pThis->aSections); iSect++)
                    if (pThis->aSections[iSect].pv)
                        pThis->pDbgInfoMod->pImgVt->pfnUnmapPart(pThis->pDbgInfoMod, pThis->aSections[iSect].cb,
                                                                 &pThis->aSections[iSect].pv);

                if (RT_SUCCESS(rc))
                {
                    /** @todo Kill pThis->CompileUnitList and the alloc caches. */
                    return VINF_SUCCESS;
                }

                /* bail out. */
                RTDbgModRelease(pThis->hCnt);
                pMod->pvDbgPriv = NULL;
            }
        }
        else
            rc = VERR_DBG_NO_MATCHING_INTERPRETER;
    }

    if (pThis->paCachedAbbrevs)
        RTMemFree(pThis->paCachedAbbrevs);
    pThis->paCachedAbbrevs = NULL;

    for (unsigned iSect = 0; iSect < RT_ELEMENTS(pThis->aSections); iSect++)
        if (pThis->aSections[iSect].pv)
            pThis->pDbgInfoMod->pImgVt->pfnUnmapPart(pThis->pDbgInfoMod, pThis->aSections[iSect].cb,
                                                     &pThis->aSections[iSect].pv);

#ifdef RTDBGMODDWARF_WITH_MEM_CACHE
    uint32_t i = RT_ELEMENTS(pThis->aDieAllocators);
    while (i-- > 0)
    {
        RTMemCacheDestroy(pThis->aDieAllocators[i].hMemCache);
        pThis->aDieAllocators[i].hMemCache = NIL_RTMEMCACHE;
    }
#endif

    RTMemFree(pThis);

    return rc;
}



/** Virtual function table for the DWARF debug info reader. */
DECL_HIDDEN_CONST(RTDBGMODVTDBG) const g_rtDbgModVtDbgDwarf =
{
    /*.u32Magic = */            RTDBGMODVTDBG_MAGIC,
    /*.fSupports = */           RT_DBGTYPE_DWARF,
    /*.pszName = */             "dwarf",
    /*.pfnTryOpen = */          rtDbgModDwarf_TryOpen,
    /*.pfnClose = */            rtDbgModDwarf_Close,

    /*.pfnRvaToSegOff = */      rtDbgModDwarf_RvaToSegOff,
    /*.pfnImageSize = */        rtDbgModDwarf_ImageSize,

    /*.pfnSegmentAdd = */       rtDbgModDwarf_SegmentAdd,
    /*.pfnSegmentCount = */     rtDbgModDwarf_SegmentCount,
    /*.pfnSegmentByIndex = */   rtDbgModDwarf_SegmentByIndex,

    /*.pfnSymbolAdd = */        rtDbgModDwarf_SymbolAdd,
    /*.pfnSymbolCount = */      rtDbgModDwarf_SymbolCount,
    /*.pfnSymbolByOrdinal = */  rtDbgModDwarf_SymbolByOrdinal,
    /*.pfnSymbolByName = */     rtDbgModDwarf_SymbolByName,
    /*.pfnSymbolByAddr = */     rtDbgModDwarf_SymbolByAddr,

    /*.pfnLineAdd = */          rtDbgModDwarf_LineAdd,
    /*.pfnLineCount = */        rtDbgModDwarf_LineCount,
    /*.pfnLineByOrdinal = */    rtDbgModDwarf_LineByOrdinal,
    /*.pfnLineByAddr = */       rtDbgModDwarf_LineByAddr,

    /*.pfnUnwindFrame = */      rtDbgModDwarf_UnwindFrame,

    /*.u32EndMagic = */         RTDBGMODVTDBG_MAGIC
};

