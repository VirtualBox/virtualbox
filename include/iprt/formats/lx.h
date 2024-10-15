/* $Id: lx.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * LX structures, types and defines.
 */

/*
 * Copyright (c) 2006-2007 Knut St. Osmundsen <bird-kStuff-spamix@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef IPRT_INCLUDED_formats_lx_h
#define IPRT_INCLUDED_formats_lx_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_formats_lx     LX executable format (OS/2)
 * @ingroup grp_rt_formats
 * @{ */

#ifndef IMAGE_OS2_SIGNATURE_LX
/** LX signature ("LX") */
# define IMAGE_LX_SIGNATURE  K_LE2H_U16('L' | ('X' << 8))
#endif

/**
 * Linear eXecutable header.
 * This structure is exactly 196 bytes long.
 */
typedef struct e32_exe
{
    uint8_t             e32_magic[2];
    uint8_t             e32_border;
    uint8_t             e32_worder;
    uint32_t            e32_level;
    uint16_t            e32_cpu;
    uint16_t            e32_os;
    uint32_t            e32_ver;
    uint32_t            e32_mflags;
    uint32_t            e32_mpages;
    uint32_t            e32_startobj;
    uint32_t            e32_eip;
    uint32_t            e32_stackobj;
    uint32_t            e32_esp;
    uint32_t            e32_pagesize;
    uint32_t            e32_pageshift;
    /** The size of the fixup section.
     * The fixup section consists of the fixup page table, the fixup record table,
     * the import module table, and the import procedure name table.
     */
    uint32_t            e32_fixupsize;
    uint32_t            e32_fixupsum;
    /** The size of the resident loader section.
     * This includes the object table, the object page map table, the resource table, the resident name table,
     * the entry table, the module format directives table, and the page checksum table (?). */
    uint32_t            e32_ldrsize;
    /** The checksum of the loader section. 0 if not calculated. */
    uint32_t            e32_ldrsum;
    /** The offset of the object table relative to this structure. */
    uint32_t            e32_objtab;
    /** Count of objects. */
    uint32_t            e32_objcnt;
    /** The offset of the object page map table relative to this structure. */
    uint32_t            e32_objmap;
    /** The offset of the object iterated pages (whatever this is used for) relative to the start of the file. */
    uint32_t            e32_itermap;
    /** The offset of the resource table relative to this structure. */
    uint32_t            e32_rsrctab;
    /** The number of entries in the resource table. */
    uint32_t            e32_rsrccnt;
    /** The offset of the resident name table relative to this structure. */
    uint32_t            e32_restab;
    /** The offset of the entry (export) table relative to this structure. */
    uint32_t            e32_enttab;
    /** The offset of the module format directives table relative to this structure. */
    uint32_t            e32_dirtab;
    /** The number of entries in the module format directives table. */
    uint32_t            e32_dircnt;
    /** The offset of the fixup page table relative to this structure. */
    uint32_t            e32_fpagetab;
    /** The offset of the fixup record table relative to this structure. */
    uint32_t            e32_frectab;
    /** The offset of the import module name table relative to this structure. */
    uint32_t            e32_impmod;
    /** The number of entries in the import module name table. */
    uint32_t            e32_impmodcnt;
    /** The offset of the import procedure name table relative to this structure. */
    uint32_t            e32_impproc;
    /** The offset of the page checksum table relative to this structure. */
    uint32_t            e32_pagesum;
    /** The offset of the data pages relative to the start of the file. */
    uint32_t            e32_datapage;
    /** The number of preload pages (ignored). */
    uint32_t            e32_preload;
    /** The offset of the non-resident name table relative to the start of the file. */
    uint32_t            e32_nrestab;
    /** The size of the non-resident name table. */
    uint32_t            e32_cbnrestab;
    uint32_t            e32_nressum;
    uint32_t            e32_autodata;
    uint32_t            e32_debuginfo;
    uint32_t            e32_debuglen;
    uint32_t            e32_instpreload;
    uint32_t            e32_instdemand;
    uint32_t            e32_heapsize;
    uint32_t            e32_stacksize;
    uint8_t             e32_res3[20];
} e32_exe;
AssertCompileSize(struct e32_exe, 196);

/** e32_magic[0] */
#define E32MAGIC1       'L'
/** e32_magic[1] */
#define E32MAGIC2       'X'
/** MAKEWORD(e32_magic[0], e32_magic[1]) */
#define E32MAGIC        0x584c
/** e32_border - little endian */
#define E32LEBO         0
/** e32_border - big endian */
#define E32BEBO         1
/** e32_worder - little endian */
#define E32LEWO         0
/** e32_worder - big endian */
#define E32BEWO         1
/** e32_level */
#define E32LEVEL        UINT32_C(0)
/** e32_cpu - 80286 */
#define E32CPU286       1
/** e32_cpu - 80386 */
#define E32CPU386       2
/** e32_cpu - 80486 */
#define E32CPU486       3
/** e32_pagesize */
#define OBJPAGELEN      UINT32_C(0x1000)


/** @name e32_mflags
 * @{ */
/** App Type: Fullscreen only. */
#define E32NOPMW         UINT32_C(0x00000100)
/** App Type: PM API. */
#define E32PMAPI         UINT32_C(0x00000300)
/** App Type: PM VIO compatible. */
#define E32PMW           UINT32_C(0x00000200)
/** Application type mask. */
#define E32APPMASK       UINT32_C(0x00000300)
/** Executable module. */
#define E32MODEXE        UINT32_C(0x00000000)
/** Dynamic link library (DLL / library) module. */
#define E32MODDLL        UINT32_C(0x00008000)
/** Protected memory DLL. */
#define E32PROTDLL       UINT32_C(0x00010000)
/** Physical Device Driver. */
#define E32MODPDEV       UINT32_C(0x00020000)
/** Virtual Device Driver. */
#define E32MODVDEV       UINT32_C(0x00028000)
/** Device driver */
#define E32DEVICE        E32MODPDEV
/** Dynamic link library (DLL / library) module. */
#define E32NOTP          E32MODDLL
/** Protected memory DLL. */
#define E32MODPROTDLL    (E32MODDLL | E32PROTDLL)
/** Module Type mask. */
#define E32MODMASK       UINT32_C(0x00038000)
/** Not loadable (linker error). */
#define E32NOLOAD        UINT32_C(0x00002000)
/** No internal fixups. */
#define E32NOINTFIX      UINT32_C(0x00000010)
/** No external fixups (i.e. imports). */
#define E32NOEXTFIX      UINT32_C(0x00000020)
/** System DLL, no internal fixups. */
#define E32SYSDLL        UINT32_C(0x00000008)
/** Global (set) or per instance (cleared) library initialization. */
#define E32LIBINIT       UINT32_C(0x00000004)
/** Global (set) or per instance (cleared) library termination. */
#define E32LIBTERM       UINT32_C(0x40000000)
/** Indicates when set in an executable that the process isn't SMP safe. */
#define E32NOTMPSAFE     UINT32_C(0x00080000)
/** @} */


/** @defgroup grp_rt_formats_lx_relocs Relocations (aka Fixups).
 * @{ */
typedef union r32_offset
{
    uint16_t            offset16;
    uint32_t            offset32;
} r32_offset;
AssertCompileSize(r32_offset, 4);

/** A relocation.
 * @remark this structure isn't very usable since LX relocations comes in too many size variations.
 */
#pragma pack(1)
typedef struct r32_rlc
{
    uint8_t             nr_stype;
    uint8_t             nr_flags;
    int16_t             r32_soff;
    uint16_t            r32_objmod;

    union targetid
    {
        r32_offset      intref;
        union extfixup
        {
            r32_offset  proc;
            uint32_t    ord;
        } extref;
        struct addfixup
        {
            uint16_t    entry;
            r32_offset  addval;
        } addfix;
    } r32_target;
    uint16_t            r32_srccount;
    uint16_t            r32_chain;
} r32_rlc;
#pragma pack()
AssertCompileSize(r32_rlc, 16);
/** @} */

/** @name Some attempt at size constanstants.
 * @{
 */
#define RINTSIZE16      8
#define RINTSIZE32      10
#define RORDSIZE        8
#define RNAMSIZE16      8
#define RNAMSIZE32      10
#define RADDSIZE16      10
#define RADDSIZE32      12
/** @} */

/** @name nr_stype (source flags)
 * @{ */
#define NRSBYT          0x00
#define NRSSEG          0x02
#define NRSPTR          0x03
#define NRSOFF          0x05
#define NRPTR48         0x06
#define NROFF32         0x07
#define NRSOFF32        0x08
#define NRSTYP          0x0f
#define NRSRCMASK       0x0f
#define NRALIAS         0x10
#define NRCHAIN         0x20
/** @} */

/** @name nr_flags (target flags)
 * @{ */
#define NRRINT          0x00
#define NRRORD          0x01
#define NRRNAM          0x02
#define NRRENT          0x03
#define NRRTYP          0x03
#define NRADD           0x04
#define NRICHAIN        0x08
#define NR32BITOFF      0x10
#define NR32BITADD      0x20
#define NR16OBJMOD      0x40
#define NR8BITORD       0x80
/** @} */

/** @} */


/** @defgroup grp_rt_formats_lx_object_tab  The Object Table (aka segment table)
 * @{ */

/** The Object Table Entry. */
typedef struct o32_obj
{
    /** The size of the object. */
    uint32_t            o32_size;
    /** The base address of the object. */
    uint32_t            o32_base;
    /** Object flags. */
    uint32_t            o32_flags;
    /** Page map index. */
    uint32_t            o32_pagemap;
    /** Page map size. (doesn't need to be o32_size >> page shift). */
    uint32_t            o32_mapsize;
    /** Reserved */
    uint32_t            o32_reserved;
} o32_obj;
AssertCompileSize(o32_obj, 24);

/** @name o32_flags
 * @{ */
/** Read access. */
#define OBJREAD         UINT32_C(0x00000001)
/** Write access. */
#define OBJWRITE        UINT32_C(0x00000002)
/** Execute access. */
#define OBJEXEC         UINT32_C(0x00000004)
/** Resource object. */
#define OBJRSRC         UINT32_C(0x00000008)
/** The object is discarable (i.e. don't swap, just load in pages from the executable).
 * This overlaps a bit with object type. */
#define OBJDISCARD      UINT32_C(0x00000010)
/** The object is shared. */
#define OBJSHARED       UINT32_C(0x00000020)
/** The object has preload pages. */
#define OBJPRELOAD      UINT32_C(0x00000040)
/** The object has invalid pages. */
#define OBJINVALID      UINT32_C(0x00000080)
/** Non-permanent, link386 bug. */
#define LNKNONPERM      UINT32_C(0x00000600)
/** Non-permanent, correct 'value'. */
#define OBJNONPERM      UINT32_C(0x00000000)
/** Obj Type: The object is permanent and swappable. */
#define OBJPERM         UINT32_C(0x00000100)
/** Obj Type: The object is permanent and resident (i.e. not swappable). */
#define OBJRESIDENT     UINT32_C(0x00000200)
/** Obj Type: The object is resident and contigious. */
#define OBJCONTIG       UINT32_C(0x00000300)
/** Obj Type: The object is permanent and long locable. */
#define OBJDYNAMIC      UINT32_C(0x00000400)
/** Object type mask. */
#define OBJTYPEMASK     UINT32_C(0x00000700)
/** x86: The object require an 16:16 alias. */
#define OBJALIAS16      UINT32_C(0x00001000)
/** x86: Big/Default selector setting, i.e. toggle 32-bit or 16-bit. */
#define OBJBIGDEF       UINT32_C(0x00002000)
/** x86: conforming selector setting (weird stuff). */
#define OBJCONFORM      UINT32_C(0x00004000)
/** x86: IOPL. */
#define OBJIOPL         UINT32_C(0x00008000)
/** @} */

/** A Object Page Map Entry. */
typedef struct o32_map
{
    /** The file offset of the page. */
    uint32_t            o32_pagedataoffset;
    /** The number of bytes of raw page data. */
    uint16_t            o32_pagesize;
    /** Per page flags describing how the page is encoded in the file. */
    uint16_t            o32_pageflags;
} o32_map;
AssertCompileSize(o32_map, 8);

/** @name o32 o32_pageflags
 * @{
 */
/** Raw page (uncompressed) in the file. */
#define VALID           UINT16_C(0x0000)
/** RLE encoded page in file. */
#define ITERDATA        UINT16_C(0x0001)
/** Invalid page, nothing in the file. */
#define INVALID         UINT16_C(0x0002)
/** Zero page, nothing in file. */
#define ZEROED          UINT16_C(0x0003)
/** range of pages (what is this?) */
#define RANGE           UINT16_C(0x0004)
/** Compressed page in file. */
#define ITERDATA2       UINT16_C(0x0005)
/** @} */


/** Iteration Record format (RLE compressed page). */
#pragma pack(1)
typedef struct LX_Iter
{
    /** Number of iterations. */
    uint16_t            LX_nIter;
    /** The number of bytes that's being iterated. */
    uint16_t            LX_nBytes;
    /** The bytes. */
    uint8_t             LX_Iterdata;
} LX_Iter;
#pragma pack()
AssertCompileSize(LX_Iter, 5);

/** @} */


/** A Resource Table Entry */
#pragma pack(1)
typedef struct rsrc32
{
    /** Resource Type. */
    uint16_t            type;
    /** Resource ID. */
    uint16_t            name;
    /** Resource size in bytes. */
    uint32_t            cb;
    /** The index of the object containing the resource. */
    uint16_t            obj;
    /** Offset of the resource that within the object. */
    uint32_t            offset;
} rsrc32;
#pragma pack()
AssertCompileSize(rsrc32, 14);


/** @defgroup grp_rt_formats_lx_entry_tab   The Entry Table (aka Export Table)
 * @{ */

/** Entry bundle.
 * Header descripting up to 255 entries that follows immediatly after this structure. */
typedef struct b32_bundle
{
    /** The number of entries. */
    uint8_t             b32_cnt;
    /** The type of bundle. */
    uint8_t             b32_type;
    /** The index of the object containing these entry points. */
    uint16_t            b32_obj;
} b32_bundle;
AssertCompileSize(b32_bundle, 4);

/** @name b32_type
 * @{ */
/** Empty bundle, filling up unused ranges of ordinals. */
#define EMPTY           0x00
/** 16-bit offset entry point. */
#define ENTRY16         0x01
/** 16-bit callgate entry point. */
#define GATE16          0x02
/** 32-bit offset entry point. */
#define ENTRY32         0x03
/** Forwarder entry point. */
#define ENTRYFWD        0x04
/** Typing information present indicator. */
#define TYPEINFO        0x80
/** @} */


/** Entry point. */
#pragma pack(1)
typedef struct e32_entry
{
    /** Entry point flags */
    uint8_t             e32_flags;      /* Entry point flags */
    union entrykind
    {
        /** ENTRY16 or ENTRY32. */
        r32_offset      e32_offset;
        /** GATE16 */
        struct scallgate
        {
            /** Offset into segment. */
            uint16_t    offset;
            /** The callgate selector */
            uint16_t    callgate;
        } e32_callgate;
        /** ENTRYFWD */
        struct fwd
        {
            /** Module ordinal number (i.e. into the import module table). */
            uint16_t    modord;
            /** Procedure name or ordinal number. */
            uint32_t    value;
        } e32_fwd;
    } e32_variant;
} e32_entry;
#pragma pack()

/** @name e32_flags
 * @{ */
/** Exported entry (set) or private entry (clear). */
#define E32EXPORT       0x01
/** Uses shared data. */
#define E32SHARED       0x02
/** Parameter word count mask. */
#define E32PARAMS       0xf8
/** ENTRYFWD: Imported by ordinal (set) or by name (clear). */
#define FWD_ORDINAL     0x01
/** @} */

/** @name dunno
 * @{ */
#define FIXENT16        3
#define FIXENT32        5
#define GATEENT16       5
#define FWDENT          7
/** @} */

/** @} */
RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_formats_lx_h */

