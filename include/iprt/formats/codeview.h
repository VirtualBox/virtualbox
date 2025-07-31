/** @file
 * IPRT - Microsoft CodeView Debug Information.
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

#ifndef IPRT_INCLUDED_formats_codeview_h
#define IPRT_INCLUDED_formats_codeview_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#include <iprt/types.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_fmt_codeview  Microsoft CodeView Debug Information
 * @{
 */


/**
 * CodeView Header.  There are two of this, base header at the start of the debug
 * information and a trailing header at the end.
 */
typedef struct RTCVHDR
{
    /** The magic ('NBxx'), see RTCVHDR_MAGIC_XXX. */
    uint32_t    u32Magic;
    /**
     * Base header: Subsection directory offset relative to this header (start).
     * Trailing header: Offset of the base header relative to the end of the file.
     *
     * Called lfoBase, lfaBase, lfoDirectory, lfoDir and probably other things in
     * the various specs/docs available. */
    uint32_t    off;
} RTCVHDR;
/** Pointer to a CodeView header. */
typedef RTCVHDR *PRTCVHDR;

/** @name CodeView magic values (RTCVHDR::u32Magic).
 * @{  */
/** CodeView from Visual C++ 5.0.  Specified in the 2001 MSDN specs.chm file. */
#define RTCVHDR_MAGIC_NB11  RT_MAKE_U32_FROM_U8('N', 'B', '1', '1')
/** External PDB reference (often referred to as PDB 2.0). */
#define RTCVHDR_MAGIC_NB10  RT_MAKE_U32_FROM_U8('N', 'B', '1', '0')
/** CodeView v4.10, packed. Specified in the TIS document. */
#define RTCVHDR_MAGIC_NB09  RT_MAKE_U32_FROM_U8('N', 'B', '0', '9')
/** CodeView v4.00 thru v4.05.  Specified in the TIS document?  */
#define RTCVHDR_MAGIC_NB08  RT_MAKE_U32_FROM_U8('N', 'B', '0', '8')
/** Quick C for Windows 1.0 debug info. */
#define RTCVHDR_MAGIC_NB07  RT_MAKE_U32_FROM_U8('N', 'B', '0', '7')
/** Emitted by ILINK indicating incremental link. Comparable to NB05?  */
#define RTCVHDR_MAGIC_NB06  RT_MAKE_U32_FROM_U8('N', 'B', '0', '6')
/** Emitted by LINK version 5.20 and later before packing. */
#define RTCVHDR_MAGIC_NB05  RT_MAKE_U32_FROM_U8('N', 'B', '0', '5')
/** Emitted by IBM ILINK for HLL (similar to NB02 in many ways). */
#define RTCVHDR_MAGIC_NB04  RT_MAKE_U32_FROM_U8('N', 'B', '0', '4')
/** Emitted by LINK version 5.10 (or similar OMF linkers), as shipped with
 * Microsoft C v6.0 for example.  More or less entirely 16-bit. */
#define RTCVHDR_MAGIC_NB02  RT_MAKE_U32_FROM_U8('N', 'B', '0', '2')
/* No idea what NB03 might have been. */
/** AIX debugger format according to "IBM OS/2 16/32-bit Object Module Format
 *  (OMF) and Linear eXecutable Module Format (LX)" revision 10 (LXOMF.PDF). */
#define RTCVHDR_MAGIC_NB01  RT_MAKE_U32_FROM_U8('N', 'B', '0', '1')
/** Ancient CodeView format according to LXOMF.PDF. */
#define RTCVHDR_MAGIC_NB00  RT_MAKE_U32_FROM_U8('N', 'B', '0', '0')
/** @} */


/** @name CV directory headers.
 * @{ */

/**
 * Really old CV directory header used with NB00 and NB02.
 *
 * Uses 16-bit directory entires (RTCVDIRENT16).
 */
typedef struct RTCVDIRHDR16
{
    /** The number of directory entries. */
    uint16_t        cEntries;
} RTCVDIRHDR16;
/** Pointer to a old CV directory header. */
typedef RTCVDIRHDR16 *PRTCVDIRHDR16;

/**
 * Simple 32-bit CV directory base header, used by NB04 (aka IBM HLL).
 */
typedef struct RTCVDIRHDR32
{
    /** The number of bytes of this header structure. */
    uint16_t        cbHdr;
    /** The number of bytes per entry. */
    uint16_t        cbEntry;
    /** The number of directory entries. */
    uint32_t        cEntries;
} RTCVDIRHDR32;
/** Pointer to a 32-bit CV directory header. */
typedef RTCVDIRHDR32 *PRTCVDIRHDR32;

/**
 * Extended 32-bit CV directory header as specified in the TIS doc.
 * The two extra fields seems to never have been assigned any official purpose.
 */
typedef struct RTCVDIRHDR32EX
{
    /** This starts the same way as the NB04 header. */
    RTCVDIRHDR32    Core;
    /** Tentatively decleared as the offset to the next directory generated by
     * the incremental linker.  Haven't seen this used yet. */
    uint32_t        offNextDir;
    /** Flags, non defined apparently, so MBZ. */
    uint32_t        fFlags;
} RTCVDIRHDR32EX;
/** Pointer to an extended 32-bit CV directory header. */
typedef RTCVDIRHDR32EX *PRTCVDIRHDR32EX;

/** @} */


/**
 * 16-bit CV directory entry used with NB00 and NB02.
 */
typedef struct RTCVDIRENT16
{
    /** Subsection type (RTCVSST). */
    uint16_t        uSubSectType;
    /** Which module (1-based, 0xffff is special). */
    uint16_t        iMod;
    /** The lowe offset of this subsection relative to the base CV header. */
    uint16_t        offLow;
    /** The high part of the subsection offset. */
    uint16_t        offHigh;
    /** The size of the subsection. */
    uint16_t        cb;
} RTCVDIRENT16;
AssertCompileSize(RTCVDIRENT16, 10);
/** Pointer to a 16-bit CV directory entry. */
typedef RTCVDIRENT16 *PRTCVDIRENT16;


/**
 * 32-bit CV directory entry used starting with NB04.
 */
typedef struct RTCVDIRENT32
{
    /** Subsection type (RTCVSST). */
    uint16_t        uSubSectType;
    /** Which module (1-based, 0xffff is special). */
    uint16_t        iMod;
    /** The offset of this subsection relative to the base CV header. */
    uint32_t        off;
    /** The size of the subsection. */
    uint32_t        cb;
} RTCVDIRENT32;
AssertCompileSize(RTCVDIRENT32, 12);
/** Pointer to a 32-bit CV directory entry. */
typedef RTCVDIRENT32 *PRTCVDIRENT32;
/** Pointer to a const 32-bit CV directory entry. */
typedef RTCVDIRENT32 const *PCRTCVDIRENT32;


/**
 * CodeView subsection types.
 */
typedef enum RTCVSST
{
    /** @name NB00, NB02 and NB04 subsection types.
     * The actual format of each subsection varies between NB04 and the others,
     * and it may further vary in NB04 depending on the module type.
     * @{ */
    kCvSst_OldModule    = 0x101,
    kCvSst_OldPublic,
    kCvSst_OldTypes,
    kCvSst_OldSymbols,
    kCvSst_OldSrcLines,
    kCvSst_OldLibraries,
    kCvSst_OldImports,
    kCvSst_OldCompacted,
    kCvSst_OldSrcLnSeg = 0x109,
    kCvSst_OldSrcLines3 = 0x10b,
    /** @} */

    /** @name NB09, NB11 (and possibly NB05, NB06, NB07, and NB08) subsection types.
     * @{ */
    kCvSst_Module    = 0x120,
    kCvSst_Types,
    kCvSst_Public,
    kCvSst_PublicSym,
    kCvSst_Symbols,
    kCvSst_AlignSym,
    kCvSst_SrcLnSeg,
    kCvSst_SrcModule,
    kCvSst_Libraries,
    kCvSst_GlobalSym,
    kCvSst_GlobalPub,
    kCvSst_GlobalTypes,
    kCvSst_MPC,
    kCvSst_SegMap,
    kCvSst_SegName,
    kCvSst_PreComp,
    kCvSst_PreCompMap,
    kCvSst_OffsetMap16,
    kCvSst_OffsetMap32,
    kCvSst_FileIndex = 0x133,
    kCvSst_StaticSym
    /** @} */
} RTCVSST;
/** Pointer to a CV subsection type value.  */
typedef RTCVSST *PRTCVSST;
/** Pointer to a const CV subsection type value.  */
typedef RTCVSST const *PCRTCVSST;


/**
 * CV4 module segment info.
 */
typedef struct RTCVMODSEGINFO32
{
    /** The segment number. */
    uint16_t        iSeg;
    /** Explicit padding. */
    uint16_t        u16Padding;
    /** Offset into the segment. */
    uint32_t        off;
    /** The size of the contribution. */
    uint32_t        cb;
} RTCVMODSEGINFO32;
typedef RTCVMODSEGINFO32 *PRTCVMODSEGINFO32;
typedef RTCVMODSEGINFO32 const *PCRTCVMODSEGINFO32;


/**
 * CV4 segment map header.
 */
typedef struct RTCVSEGMAPHDR
{
    /** Number of segments descriptors in the table. */
    uint16_t        cSegs;
    /** Number of logical segment descriptors. */
    uint16_t        cLogSegs;
} RTCVSEGMAPHDR;
/** Pointer to a CV4 segment map header. */
typedef RTCVSEGMAPHDR *PRTCVSEGMAPHDR;
/** Pointer to a const CV4 segment map header. */
typedef RTCVSEGMAPHDR const *PCRTCVSEGMAPHDR;

/**
 * CV4 Segment map descriptor entry.
 */
typedef struct RTCVSEGMAPDESC
{
    /** Segment flags. */
    uint16_t        fFlags;
    /** The overlay number. */
    uint16_t        iOverlay;
    /** Group index into this segment descriptor array. 0 if not relevant.
     * The group descriptors are found in the second half of the table.  */
    uint16_t        iGroup;
    /** Complicated. */
    uint16_t        iFrame;
    /** Offset (byte) into the kCvSst_SegName table of the segment name, or
     * 0xffff. */
    uint16_t        offSegName;
    /** Offset (byte) into the kCvSst_SegName table of the class name, or 0xffff. */
    uint16_t        offClassName;
    /** Offset into the physical segment. */
    uint32_t        off;
    /** Size of segment. */
    uint32_t        cb;
} RTCVSEGMAPDESC;
/** Pointer to a segment map descriptor entry. */
typedef RTCVSEGMAPDESC *PRTCVSEGMAPDESC;
/** Pointer to a const segment map descriptor entry. */
typedef RTCVSEGMAPDESC const *PCRTCVSEGMAPDESC;

/** @name RTCVSEGMAPDESC_F_XXX - RTCVSEGMAPDESC::fFlags values.
 * @{ */
#define RTCVSEGMAPDESC_F_READ       UINT16_C(0x0001)
#define RTCVSEGMAPDESC_F_WRITE      UINT16_C(0x0002)
#define RTCVSEGMAPDESC_F_EXECUTE    UINT16_C(0x0004)
#define RTCVSEGMAPDESC_F_32BIT      UINT16_C(0x0008)
#define RTCVSEGMAPDESC_F_SEL        UINT16_C(0x0100)
#define RTCVSEGMAPDESC_F_ABS        UINT16_C(0x0200)
#define RTCVSEGMAPDESC_F_GROUP      UINT16_C(0x1000)
#define RTCVSEGMAPDESC_F_RESERVED   UINT16_C(0xecf0)
/** @} */

/**
 * CV4 segment map subsection.
 */
typedef struct RTCVSEGMAP
{
    /** The header. */
    RTCVSEGMAPHDR   Hdr;
    /** Descriptor array. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RTCVSEGMAPDESC  aDescs[RT_FLEXIBLE_ARRAY];
} RTCVSEGMAP;
/** Pointer to a segment map subsection. */
typedef RTCVSEGMAP *PRTCVSEGMAP;
/** Pointer to a const segment map subsection. */
typedef RTCVSEGMAP const *PCRTCVSEGMAP;


/**
 * CV4 line number segment contribution start/end table entry.
 * Part of RTCVSRCMODULE.
 */
typedef struct RTCVSRCRANGE
{
    /** Start segment offset. */
    uint32_t        offStart;
    /** End segment offset (inclusive?). */
    uint32_t        offEnd;
} RTCVSRCRANGE;
/** Pointer to a line number segment contributation. */
typedef RTCVSRCRANGE *PRTCVSRCRANGE;
/** Pointer to a const line number segment contributation. */
typedef RTCVSRCRANGE const *PCRTCVSRCRANGE;

/**
 * CV4 header for a line number subsection, used by kCvSst_SrcModule.
 *
 * The aoffSrcFiles member is followed by an array of segment ranges
 * (RTCVSRCRANGE), cSegs in length.  This may contain zero entries if the
 * information is not known or not possible to express in this manner.
 *
 * After the range table, a segment index (uint16_t) mapping table follows, also
 * cSegs in length.
 */
typedef struct RTCVSRCMODULE
{
    /** The number of files described in this subsection. */
    uint16_t        cFiles;
    /** The number of code segments this module contributes to. */
    uint16_t        cSegs;
    /** Offsets of the RTCVSRCFILE entries in this subsection, length given by
     * the above cFiles member. */
    uint32_t        aoffSrcFiles[1 /*cFiles*/];
    /* RTCVSRCRANGE   aSegRanges[cSegs]; */
    /* uint16_t       aidxSegs[cSegs]; */
} RTCVSRCMODULE;
/** Pointer to a source module subsection header. */
typedef RTCVSRCMODULE *PRTCVSRCMODULE;
/** Pointer to a const source module subsection header. */
typedef RTCVSRCMODULE const *PCRTCVSRCMODULE;

/**
 * CV4 source file, inside a kCvSst_SrcModule (see RTCVSRCMODULE::aoffSrcFiles)
 *
 * The aoffSrcLines member is followed by an array of segment ranges
 * (RTCVSRCRANGE), cSegs in length.  Just like for RTCVSRCMODULE this may
 * contain zero entries.
 *
 * After the range table is the filename, which is preceded by a 8-bit length
 * (actually documented to be 16-bit, but seeing 8-bit here with wlink).
 */
typedef struct RTCVSRCFILE
{
    /** The number segments that this source file contributed to. */
    uint16_t        cSegs;
    /** Alignment padding. */
    uint16_t        uPadding;
    /** Offsets of the RTCVSRCLN entries for this source file, length given by
     * the above cSegs member.  Relative to the start of the subsection. */
    uint32_t        aoffSrcLines[1 /*cSegs*/];
    /* RTCVSRCRANGE aSegRanges[cSegs]; */
    /* uint8_t/uint16_t cchName; */
    /* char         achName[cchName]; */
} RTCVSRCFILE;
/** Pointer to a source file. */
typedef RTCVSRCFILE *PRTCVSRCFILE;
/** Pointer to a const source file. */
typedef RTCVSRCFILE const *PCRTCVSRCFILE;

/**
 * CV4 line numbers header.
 *
 * The aoffLines member is followed by an array of line numbers (uint16_t).
 */
typedef struct RTCVSRCLINE
{
    /** The index of the segment these line numbers belong to. */
    uint16_t        idxSeg;
    /** The number of line number pairs the two following tables. */
    uint16_t        cPairs;
    /** Segment offsets, cPairs long. */
    uint32_t        aoffLines[1 /*cPairs*/];
    /* uint16_t     aiLines[cPairs]; */
} RTCVSRCLINE;
/** Pointer to a line numbers header. */
typedef RTCVSRCLINE *PRTCVSRCLINE;
/** Pointer to a const line numbers header. */
typedef RTCVSRCLINE const *PCRTCVSRCLINE;


/**
 * Global symbol table header, used by kCvSst_GlobalSym and kCvSst_GlobalPub.
 */
typedef struct RTCVGLOBALSYMTABHDR
{
    /** The symbol hash function. */
    uint16_t        uSymHash;
    /** The address hash function. */
    uint16_t        uAddrHash;
    /** The amount of symbol information following immediately after the header. */
    uint32_t        cbSymbols;
    /** The amount of symbol hash tables following the symbols. */
    uint32_t        cbSymHash;
    /** The amount of address hash tables following the symbol hash tables. */
    uint32_t        cbAddrHash;
} RTCVGLOBALSYMTABHDR;
/** Pointer to a global symbol table header. */
typedef RTCVGLOBALSYMTABHDR *PRTCVGLOBALSYMTABHDR;
/** Pointer to a const global symbol table header. */
typedef RTCVGLOBALSYMTABHDR const *PCRTCVGLOBALSYMTABHDR;


typedef enum RTCVSYMTYPE
{
    /** @name Symbols that doesn't change with compilation model or target machine.
     * @{ */
    kCvSymType_Compile = 0x0001,
    kCvSymType_Register,
    kCvSymType_Constant,
    kCvSymType_UDT,
    kCvSymType_SSearch,
    kCvSymType_End,
    kCvSymType_Skip,
    kCvSymType_CVReserve,
    kCvSymType_ObjName,
    kCvSymType_EndArg,
    kCvSymType_CobolUDT,
    kCvSymType_ManyReg,
    kCvSymType_Return,
    kCvSymType_EntryThis,
    /** @}  */

    /** @name Symbols with 16:16 addresses.
     * @{ */
    kCvSymType_BpRel16 = 0x0100,
    kCvSymType_LData16,
    kCvSymType_GData16,
    kCvSymType_Pub16,
    kCvSymType_LProc16,
    kCvSymType_GProc16,
    kCvSymType_Thunk16,
    kCvSymType_BLock16,
    kCvSymType_With16,
    kCvSymType_Label16,
    kCvSymType_CExModel16,
    kCvSymType_VftPath16,
    kCvSymType_RegRel16,
    /** @}  */

    /** @name Symbols with 16:32 addresses.
     * @{ */
    kCvSymType_BpRel32 = 0x0200,
    kCvSymType_LData32,
    kCvSymType_GData32,
    kCvSymType_Pub32,
    kCvSymType_LProc32,
    kCvSymType_GProc32,
    kCvSymType_Thunk32,
    kCvSymType_Block32,
    kCvSymType_With32,
    kCvSymType_Label32,
    kCvSymType_CExModel32,
    kCvSymType_VftPath32,
    kCvSymType_RegRel32,
    kCvSymType_LThread32,
    kCvSymType_GThread32,
    /** @}  */

    /** @name Symbols for MIPS.
     * @{ */
    kCvSymType_LProcMips = 0x0300,
    kCvSymType_GProcMips,
    /** @} */

    /** @name Symbols for Microsoft CodeView.
     * @{ */
    kCvSymType_ProcRef = 0x0400,
    kCvSymType_DataRef,
    kCvSymType_Align,
    kCvSymType_LProcRef,
    /** @} */

    /** @name Symbols with 32-bit address (I think) and 32-bit type indices.
     * @{ */
    kCvSymType_V2_Register = 0x1001,
    kCvSymType_V2_Constant,
    kCvSymType_V2_Udt,
    kCvSymType_V2_CobolUdt,
    kCvSymType_V2_ManyReg,
    kCvSymType_V2_BpRel,
    kCvSymType_V2_LData,
    kCvSymType_V2_GData,
    kCvSymType_V2_Pub,
    kCvSymType_V2_LProc,
    kCvSymType_V2_GProc,
    kCvSymType_V2_VftTable,
    kCvSymType_V2_RegRel,
    kCvSymType_V2_LThread,
    kCvSymType_V2_GThread,
    kCvSymType_V2_LProcMips,
    kCvSymType_V2_GProcMips,
    kCvSymType_V2_FrameInfo,
    kCvSymType_V2_Compliand,
    /** @} */

    /** @name Version 3 symbol types.
     * @{ */
    /** Name of the object file, preceded by a 4-byte language type (ASM=0) */
    kCvSymType_V3_Compliand = 0x1101,
    kCvSymType_V3_Thunk,
    kCvSymType_V3_Block,
    kCvSymType_V3_Unknown_1104,
    kCvSymType_V3_Label,                /**< RTCVSYMV3LABEL */
    kCvSymType_V3_Register,
    kCvSymType_V3_Constant,
    kCvSymType_V3_Udt,
    kCvSymType_V3_Unknown_1109,
    kCvSymType_V3_Unknown_110a,
    kCvSymType_V3_BpRel,
    kCvSymType_V3_LData,               /**< RTCVSYMV3TYPEDNAME */
    kCvSymType_V3_GData,               /**< RTCVSYMV3TYPEDNAME */
    kCvSymType_V3_Pub,
    kCvSymType_V3_LProc,
    kCvSymType_V3_GProc,
    kCvSymType_V3_RegRel,
    kCvSymType_V3_LThread,
    kCvSymType_V3_GThread,
    kCvSymType_V3_Unknown_1114,
    kCvSymType_V3_Unknown_1115,
    kCvSymType_V3_MSTool,               /**< RTCVSYMV3MSTOOL */

    kCvSymType_V3_ProcRef = 0x1125,
    kCvSymType_V3_LProcRef = 0x1127,
    kCvSymType_V3_Unknown_1128,
    kCvSymType_V3_SectInfo = 0x1136,
    kCvSymType_V3_SubSectInfo,
    kCvSymType_V3_Entrypoint,
    kCvSymType_V3_Unknown_1139,
    kCvSymType_V3_SecuCookie,
    kCvSymType_V3_Unknown_113b,
    kCvSymType_V3_MsToolInfo,
    kCvSymType_V3_MsToolEnv,

    kCvSymType_VS2013_Local,
    kCvSymType_VS2013_FpOff = 0x1144,
    kCvSymType_VS2013_LProc32 = 0x1146,
    kCvSymType_VS2013_GProc32,
    /** @} */

    kCvSymType_EndOfValues
} RTCVSYMTYPE;
AssertCompile(kCvSymType_V3_Udt == 0x1108);
AssertCompile(kCvSymType_V3_GProc == 0x1110);
AssertCompile(kCvSymType_V3_MSTool == 0x1116);
AssertCompile(kCvSymType_VS2013_Local == 0x113E);
typedef RTCVSYMTYPE *PRTCVSYMTYPE;
typedef RTCVSYMTYPE const *PCRTCVSYMTYPE;


/**
 * kCvSymType_V3_MSTool format.
 */
typedef struct RTCVSYMV3MSTOOL
{
    /** Language or tool ID (3 == masm). */
    uint32_t    uLanguage;
    /** Target CPU (0xd0 == AMD64). */
    uint32_t    uTargetCpu;
    /** Flags. */
    uint32_t    fFlags;
    /** Version.   */
    uint32_t    uVersion;
    /** The creator name, zero terminated.
     *
     * It is followed by key/value pairs of zero terminated strings giving more
     * details about the current directory ('cwd'), compiler executable ('cl'),
     * full command line ('cmd'), source path relative to cwd ('src'), the
     * full program database path ('pdb'), and possibly others.  Terminated by a
     * pair of empty strings, usually. */
    char        szCreator[1];
} RTCVSYMV3MSTOOL;
typedef RTCVSYMV3MSTOOL *PRTCVSYMV3MSTOOL;
typedef RTCVSYMV3MSTOOL const *PCRTCVSYMV3MSTOOL;

/**
 * kCvSymType_V3_Label format.
 */
typedef struct RTCVSYMV3LABEL
{
    /** Offset into iSection of this symbol. */
    uint32_t        offSection;
    /** The index of the section where the symbol lives. */
    uint16_t        iSection;
    /** Flags or something. */
    uint8_t         fFlags;
    /** Zero terminated symbol name (variable length). */
    char            szName[1];
} RTCVSYMV3LABEL;
AssertCompileSize(RTCVSYMV3LABEL, 8);
typedef RTCVSYMV3LABEL *PRTCVSYMV3LABEL;
typedef RTCVSYMV3LABEL const *PCRTCVSYMV3LABEL;

/**
 * kCvSymType_V2_LData, kCvSymType_V2_GData and kCvSymType_V2_Pub format.
 *
 * Same as RTCVSYMV3TYPEDNAME but with pascal string name, rather than C.
 */
typedef struct RTCVSYMV2TYPEDNAME
{
    /** The type ID. */
    uint32_t        idType;
    /** Offset into iSection of this symbol. */
    uint32_t        offSection;
    /** The index of the section where the symbol lives. */
    uint16_t        iSection;
    /** Name length. */
    uint8_t         cchName;
    /** Zero terminated symbol name (variable length). */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char            achName[RT_FLEXIBLE_ARRAY];
} RTCVSYMV2TYPEDNAME;
AssertCompileMemberOffset(RTCVSYMV2TYPEDNAME, achName, 11);
typedef RTCVSYMV2TYPEDNAME *PRTCVSYMV2TYPEDNAME;
typedef RTCVSYMV2TYPEDNAME const *PCRTCVSYMV2TYPEDNAME;

/**
 * kCvSymType_V3_LData and kCvSymType_V3_GData format.
 */
typedef struct RTCVSYMV3TYPEDNAME
{
    /** The type ID. */
    uint32_t        idType;
    /** Offset into iSection of this symbol. */
    uint32_t        offSection;
    /** The index of the section where the symbol lives. */
    uint16_t        iSection;
    /** Zero terminated symbol name (variable length). */
    char            szName[2];
} RTCVSYMV3TYPEDNAME;
AssertCompileSize(RTCVSYMV3TYPEDNAME, 12);
typedef RTCVSYMV3TYPEDNAME *PRTCVSYMV3TYPEDNAME;
typedef RTCVSYMV3TYPEDNAME const *PCRTCVSYMV3TYPEDNAME;

/**
 * kCvSymType_V3_LProc and kCvSymType_V3_GProc format.
 */
typedef struct RTCVSYMV3PROC
{
    /** Lexical scope linking: Parent. */
    uint32_t        uParent;
    /** Lexical scope linking: End. */
    uint32_t        uEnd;
    /** Lexical scope linking: Next. */
    uint32_t        uNext;
    /** The procedure length. */
    uint32_t        cbProc;
    /** Offset into the procedure where the stack frame has been setup and is an
     * excellent position for a function breakpoint. */
    uint32_t        offDebugStart;
    /** Offset into the procedure where the procedure is ready to return and has a
     * return value (if applicable). */
    uint32_t        offDebugEnd;
    /** The type ID for the procedure. */
    uint32_t        idType;
    /** Offset into iSection of this procedure. */
    uint32_t        offSection;
    /** The index of the section where the procedure lives. */
    uint16_t        iSection;
    /** Flags.   */
    uint8_t         fFlags;
    /** Zero terminated procedure name (variable length). */
    char            szName[1];
} RTCVSYMV3PROC;
AssertCompileSize(RTCVSYMV3PROC, 36);
typedef RTCVSYMV3PROC *PRTCVSYMV3PROC;
typedef RTCVSYMV3PROC const *PCRTCVSYMV3PROC;


/** @name $$SYMBOLS signatures.
 * @{ */
/** The $$SYMBOL table signature for CV4. */
#define RTCVSYMBOLS_SIGNATURE_CV4   UINT32_C(0x00000001)
/** The $$SYMBOL table signature for CV8 (MSVC 8/2005).
 * Also seen with MSVC 2010 using -Z7, so maybe more appropriate to call it
 * CV7? */
#define RTCVSYMBOLS_SIGNATURE_CV8   UINT32_C(0x00000004)
/** @} */


/**
 * CV8 $$SYMBOLS block header.
 */
typedef struct RTCV8SYMBOLSBLOCK
{
    /** BLock type (RTCV8SYMBLOCK_TYPE_XXX). */
    uint32_t    uType;
    /** The block length, including this header? */
    uint32_t    cb;
} RTCV8SYMBOLSBLOCK;
AssertCompileSize(RTCV8SYMBOLSBLOCK, 8);
typedef RTCV8SYMBOLSBLOCK *PRTCV8SYMBOLSBLOCK;
typedef RTCV8SYMBOLSBLOCK const *PCRTCV8SYMBOLSBLOCK;

/** @name RTCV8SYMBLOCK_TYPE_XXX - CV8 (MSVC 8/2005) $$SYMBOL table types.
 * @{ */
/** Symbol information.
 * Sequence of types.  Each type entry starts with a 16-bit length followed
 * by a 16-bit RTCVSYMTYPE value.  Just like CV4/5, but with C-strings
 * instead of pascal. */
#define RTCV8SYMBLOCK_TYPE_SYMBOLS        UINT32_C(0x000000f1)
/** Line numbers for a section. */
#define RTCV8SYMBLOCK_TYPE_SECT_LINES     UINT32_C(0x000000f2)
/** Source file string table.
 * The strings are null terminated. Indexed by RTCV8SYMBLOCK_TYPE_SRC_INFO. */
#define RTCV8SYMBLOCK_TYPE_SRC_STR        UINT32_C(0x000000f3)
/** Source file information. */
#define RTCV8SYMBLOCK_TYPE_SRC_INFO       UINT32_C(0x000000f4)
/** @} */

/**
 * Line number header found in a RTCV8SYMBLOCK_TYPE_SECT_LINES block.
 *
 * This is followed by a sequence of RTCV8LINESSRCMAP structures.
 */
typedef struct RTCV8LINESHDR
{
    /** Offset into the section. */
    uint32_t    offSection;
    /** The section number.  */
    uint16_t    iSection;
    /** Padding/zero/maybe-previous-member-is-a-32-bit-value. */
    uint16_t    u16Padding;
    /** Number of bytes covered by this table, starting at offSection. */
    uint32_t    cbSectionCovered;
} RTCV8LINESHDR;
AssertCompileSize(RTCV8LINESHDR, 12);
typedef RTCV8LINESHDR *PRTCV8LINESHDR;
typedef RTCV8LINESHDR const *PCRTCV8LINESHDR;

/**
 * CV8 (MSVC 8/2005) line number source map.
 *
 * This is followed by an array of RTCV8LINEPAIR.
 */
typedef struct RTCV8LINESSRCMAP
{
    /** The source file, given as an offset (byte) into the source file
     * information table (RTCV8SYMBLOCK_TYPE_SRC_INFO). */
    uint32_t    offSourceInfo;
    /** Number of line numbers following this structure. */
    uint32_t    cLines;
    /** The size of this source map. */
    uint32_t    cb;
} RTCV8LINESSRCMAP;
AssertCompileSize(RTCV8LINESSRCMAP, 12);
typedef RTCV8LINESSRCMAP *PRTCV8LINESSRCMAP;
typedef RTCV8LINESSRCMAP const *PCRTCV8LINESSRCMAP;

/**
 * One line number.
 */
typedef struct RTCV8LINEPAIR
{
    /** Offset into the section of this line number. */
    uint32_t    offSection;
    /** The line number. */
    uint32_t    uLineNumber : 30;
    /** Indicates that it's not possible to set breakpoint? */
    uint32_t    fEndOfStatement : 1;
} RTCV8LINEPAIR;
AssertCompileSize(RTCV8LINEPAIR, 8);
typedef RTCV8LINEPAIR *PRTCV8LINEPAIR;
typedef RTCV8LINEPAIR const *PCRTCV8LINEPAIR;

/**
 * Source file information found in a RTCV8SYMBLOCK_TYPE_SRC_INFO block.
 */
typedef struct RTCV8SRCINFO
{
    /** The source file name, given as an offset into the string table
     * (RTCV8SYMBLOCK_TYPE_SRC_STR). */
    uint32_t    offSourceName;
    /** Digest/checksum type. */
    uint16_t    uDigestType;
    union
    {
        /** RTCV8SRCINFO_DIGEST_TYPE_MD5. */
        struct
        {
            /** The digest. */
            uint8_t ab[16];
            /** Structur alignment padding. */
            uint8_t abPadding[2];
        } md5;
        /** RTCV8SRCINFO_DIGEST_TYPE_NONE: Padding. */
        uint8_t abNone[2];
    } Digest;
} RTCV8SRCINFO;
AssertCompileSize(RTCV8SRCINFO, 24);
typedef RTCV8SRCINFO *PRTCV8SRCINFO;
typedef RTCV8SRCINFO const *PCRTCV8SRCINFO;

/** @name  RTCV8SRCINFO_DIGEST_TYPE_XXX - CV8 source digest types.
 * Used by RTCV8SRCINFO::uDigestType.
 * @{ */
#define RTCV8SRCINFO_DIGEST_TYPE_NONE   UINT16_C(0x0000)
#define RTCV8SRCINFO_DIGEST_TYPE_MD5    UINT16_C(0x0110)
/** @} */



/**
 * PDB v2.0 in image debug info.
 * The URL is constructed from the timestamp and age?
 */
typedef struct CVPDB20INFO
{
    uint32_t    u32Magic;               /**< CVPDB20INFO_SIGNATURE. */
    int32_t     offDbgInfo;             /**< Always 0. Used to be the offset to the real debug info. */
    uint32_t    uTimestamp;
    uint32_t    uAge;
    uint8_t     szPdbFilename[4];
} CVPDB20INFO;
/** Pointer to in executable image PDB v2.0 info. */
typedef CVPDB20INFO       *PCVPDB20INFO;
/** Pointer to read only in executable image PDB v2.0 info. */
typedef CVPDB20INFO const *PCCVPDB20INFO;
/** The CVPDB20INFO magic value. */
#define CVPDB20INFO_MAGIC           RT_MAKE_U32_FROM_U8('N','B','1','0')

/**
 * PDB v7.0 in image debug info.
 * The URL is constructed from the signature and the age.
 */
#pragma pack(4)
typedef struct CVPDB70INFO
{
    uint32_t    u32Magic;            /**< CVPDB70INFO_SIGNATURE. */
    RTUUID      PdbUuid;
    uint32_t    uAge;
    uint8_t     szPdbFilename[4];
} CVPDB70INFO;
#pragma pack()
AssertCompileMemberOffset(CVPDB70INFO, PdbUuid, 4);
AssertCompileMemberOffset(CVPDB70INFO, uAge, 4 + 16);
/** Pointer to in executable image PDB v7.0 info. */
typedef CVPDB70INFO       *PCVPDB70INFO;
/** Pointer to read only in executable image PDB v7.0 info. */
typedef CVPDB70INFO const *PCCVPDB70INFO;
/** The CVPDB70INFO magic value. */
#define CVPDB70INFO_MAGIC           RT_MAKE_U32_FROM_U8('R','S','D','S')


/** @}  */

#endif /* !IPRT_INCLUDED_formats_codeview_h */

