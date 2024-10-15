/* $Id: mach-o.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Mach-O Structures and Constants.
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

#ifndef IPRT_INCLUDED_formats_mach_o_h
#define IPRT_INCLUDED_formats_mach_o_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>

#ifndef CPU_ARCH_MASK

/* cputype */
#define CPU_ARCH_MASK               INT32_C(0xff000000)
#define CPU_ARCH_ABI64              INT32_C(0x01000000)
#define CPU_ARCH_ABI64_32           INT32_C(0x02000000) /**< LP32 on 64-bit hardware */

#define CPU_TYPE_ANY                INT32_C(-1)
#define CPU_TYPE_VAX                INT32_C(1)
#define CPU_TYPE_MC680x0            INT32_C(6)
#define CPU_TYPE_X86                INT32_C(7)
#define CPU_TYPE_I386               CPU_TYPE_X86
#define CPU_TYPE_X86_64             (CPU_TYPE_X86 | CPU_ARCH_ABI64)
#define CPU_TYPE_MC98000            INT32_C(10)
#define CPU_TYPE_HPPA               INT32_C(11)
#define CPU_TYPE_ARM                INT32_C(12)
#define CPU_TYPE_ARM32              CPU_TYPE_ARM
#define CPU_TYPE_ARM64              (CPU_TYPE_ARM | CPU_ARCH_ABI64)
#define CPU_TYPE_ARM64_32           (CPU_TYPE_ARM | CPU_ARCH_ABI64_32)
#define CPU_TYPE_MC88000            INT32_C(13)
#define CPU_TYPE_SPARC              INT32_C(14)
#define CPU_TYPE_I860               INT32_C(15)
#define CPU_TYPE_POWERPC            INT32_C(18)
#define CPU_TYPE_POWERPC64          (CPU_TYPE_POWERPC | CPU_ARCH_ABI64)

/* cpusubtype */
#define CPU_SUBTYPE_MULTIPLE        INT32_C(-1)
#define CPU_SUBTYPE_LITTLE_ENDIAN   INT32_C(0)
#define CPU_SUBTYPE_BIG_ENDIAN      INT32_C(1)

#define CPU_SUBTYPE_VAX_ALL         INT32_C(0)
#define CPU_SUBTYPE_VAX780          INT32_C(1)
#define CPU_SUBTYPE_VAX785          INT32_C(2)
#define CPU_SUBTYPE_VAX750          INT32_C(3)
#define CPU_SUBTYPE_VAX730          INT32_C(4)
#define CPU_SUBTYPE_UVAXI           INT32_C(5)
#define CPU_SUBTYPE_UVAXII          INT32_C(6)
#define CPU_SUBTYPE_VAX8200         INT32_C(7)
#define CPU_SUBTYPE_VAX8500         INT32_C(8)
#define CPU_SUBTYPE_VAX8600         INT32_C(9)
#define CPU_SUBTYPE_VAX8650         INT32_C(10)
#define CPU_SUBTYPE_VAX8800         INT32_C(11)
#define CPU_SUBTYPE_UVAXIII         INT32_C(12)

#define CPU_SUBTYPE_MC680x0_ALL     INT32_C(1)
#define CPU_SUBTYPE_MC68030         INT32_C(1)
#define CPU_SUBTYPE_MC68040         INT32_C(2)
#define CPU_SUBTYPE_MC68030_ONLY    INT32_C(3)

#define CPU_SUBTYPE_INTEL(fam, model)       ( (int32_t )(((model) << 4) | (fam)) )
#define CPU_SUBTYPE_INTEL_FAMILY(subtype)   ( (subtype) & 0xf )
#define CPU_SUBTYPE_INTEL_MODEL(subtype)    ( (subtype) >> 4 )
#define CPU_SUBTYPE_INTEL_FAMILY_MAX        0xf
#define CPU_SUBTYPE_INTEL_MODEL_ALL         0

#define CPU_SUBTYPE_I386_ALL        CPU_SUBTYPE_INTEL(3, 0)
#define CPU_SUBTYPE_386             CPU_SUBTYPE_INTEL(3, 0)
#define CPU_SUBTYPE_486             CPU_SUBTYPE_INTEL(4, 0)
#define CPU_SUBTYPE_486SX           CPU_SUBTYPE_INTEL(4, 8)
#define CPU_SUBTYPE_586             CPU_SUBTYPE_INTEL(5, 0)
#define CPU_SUBTYPE_PENT            CPU_SUBTYPE_INTEL(5, 0)
#define CPU_SUBTYPE_PENTPRO         CPU_SUBTYPE_INTEL(6, 1)
#define CPU_SUBTYPE_PENTII_M3       CPU_SUBTYPE_INTEL(6, 3)
#define CPU_SUBTYPE_PENTII_M5       CPU_SUBTYPE_INTEL(6, 5)
#define CPU_SUBTYPE_CELERON         CPU_SUBTYPE_INTEL(7, 6)
#define CPU_SUBTYPE_CELERON_MOBILE  CPU_SUBTYPE_INTEL(7, 7)
#define CPU_SUBTYPE_PENTIUM_3       CPU_SUBTYPE_INTEL(8, 0)
#define CPU_SUBTYPE_PENTIUM_3_M     CPU_SUBTYPE_INTEL(8, 1)
#define CPU_SUBTYPE_PENTIUM_3_XEON  CPU_SUBTYPE_INTEL(8, 2)
#define CPU_SUBTYPE_PENTIUM_M       CPU_SUBTYPE_INTEL(9, 0)
#define CPU_SUBTYPE_PENTIUM_4       CPU_SUBTYPE_INTEL(10, 0)
#define CPU_SUBTYPE_PENTIUM_4_M     CPU_SUBTYPE_INTEL(10, 1)
#define CPU_SUBTYPE_ITANIUM         CPU_SUBTYPE_INTEL(11, 0)
#define CPU_SUBTYPE_ITANIUM_2       CPU_SUBTYPE_INTEL(11, 1)
#define CPU_SUBTYPE_XEON            CPU_SUBTYPE_INTEL(12, 0)
#define CPU_SUBTYPE_XEON_MP         CPU_SUBTYPE_INTEL(12, 1)

#define CPU_SUBTYPE_X86_ALL         INT32_C(3)
#define CPU_SUBTYPE_X86_64_ALL      INT32_C(3)
#define CPU_SUBTYPE_X86_ARCH1       INT32_C(4)

#define CPU_SUBTYPE_MIPS_ALL        INT32_C(0)
#define CPU_SUBTYPE_MIPS_R2300      INT32_C(1)
#define CPU_SUBTYPE_MIPS_R2600      INT32_C(2)
#define CPU_SUBTYPE_MIPS_R2800      INT32_C(3)
#define CPU_SUBTYPE_MIPS_R2000a     INT32_C(4)
#define CPU_SUBTYPE_MIPS_R2000      INT32_C(5)
#define CPU_SUBTYPE_MIPS_R3000a     INT32_C(6)
#define CPU_SUBTYPE_MIPS_R3000      INT32_C(7)

#define CPU_SUBTYPE_MC98000_ALL     INT32_C(0)
#define CPU_SUBTYPE_MC98601         INT32_C(1)

#define CPU_SUBTYPE_HPPA_ALL        INT32_C(0)
#define CPU_SUBTYPE_HPPA_7100       INT32_C(0)
#define CPU_SUBTYPE_HPPA_7100LC     INT32_C(1)

#define CPU_SUBTYPE_ARM_ALL         INT32_C(0)
#define CPU_SUBTYPE_ARM_V4T         INT32_C(5)
#define CPU_SUBTYPE_ARM_V6          INT32_C(6)
#define CPU_SUBTYPE_ARM_V5TEJ       INT32_C(7)
#define CPU_SUBTYPE_ARM_XSCALE      INT32_C(8)
#define CPU_SUBTYPE_ARM_V7          INT32_C(9)
#define CPU_SUBTYPE_ARM_V7F         INT32_C(10)
#define CPU_SUBTYPE_ARM_V7S         INT32_C(11)
#define CPU_SUBTYPE_ARM_V7K         INT32_C(12)
#define CPU_SUBTYPE_ARM_V8          INT32_C(13)
#define CPU_SUBTYPE_ARM_V6M         INT32_C(14)
#define CPU_SUBTYPE_ARM_V7M         INT32_C(15)
#define CPU_SUBTYPE_ARM_V7EM        INT32_C(16)
#define CPU_SUBTYPE_ARM_V8M         INT32_C(17)

#define CPU_SUBTYPE_ARM64_ALL       INT32_C(0)
#define CPU_SUBTYPE_ARM64_V8        INT32_C(1)
#define CPU_SUBTYPE_ARM64E          INT32_C(2)
#define CPU_SUBTYPE_ARM64_PTR_AUTH_MASK         UINT32_C(0x0f000000)
#define CPU_SUBTYPE_ARM64_PTR_AUTH_VERSION(a)   ( ((a) & CPU_SUBTYPE_ARM64_PTR_AUTH_MASK) >> 24 )

#define CPU_SUBTYPE_ARM64_32_ALL    INT32_C(0)
#define CPU_SUBTYPE_ARM64_32_V8     INT32_C(1)

#define CPU_SUBTYPE_MC88000_ALL     INT32_C(0)
#define CPU_SUBTYPE_MC88100         INT32_C(1)
#define CPU_SUBTYPE_MC88110         INT32_C(2)

#define CPU_SUBTYPE_SPARC_ALL       INT32_C(0)

#define CPU_SUBTYPE_I860_ALL        INT32_C(0)
#define CPU_SUBTYPE_I860_860        INT32_C(1)

#define CPU_SUBTYPE_POWERPC_ALL     INT32_C(0)
#define CPU_SUBTYPE_POWERPC_601     INT32_C(1)
#define CPU_SUBTYPE_POWERPC_602     INT32_C(2)
#define CPU_SUBTYPE_POWERPC_603     INT32_C(3)
#define CPU_SUBTYPE_POWERPC_603e    INT32_C(4)
#define CPU_SUBTYPE_POWERPC_603ev   INT32_C(5)
#define CPU_SUBTYPE_POWERPC_604     INT32_C(6)
#define CPU_SUBTYPE_POWERPC_604e    INT32_C(7)
#define CPU_SUBTYPE_POWERPC_620     INT32_C(8)
#define CPU_SUBTYPE_POWERPC_750     INT32_C(9)
#define CPU_SUBTYPE_POWERPC_7400    INT32_C(10)
#define CPU_SUBTYPE_POWERPC_7450    INT32_C(11)
#define CPU_SUBTYPE_POWERPC_Max     INT32_C(10)
#define CPU_SUBTYPE_POWERPC_SCVger  INT32_C(11)
#define CPU_SUBTYPE_POWERPC_970     INT32_C(100)

#define CPU_SUBTYPE_MASK            UINT32_C(0xff000000)
#define CPU_SUBTYPE_LIB64           UINT32_C(0x80000000)

#endif /* !CPU_ARCH_MASK */


typedef struct fat_header
{
    uint32_t            magic;
    uint32_t            nfat_arch;
} fat_header_t;

#ifndef IMAGE_FAT_SIGNATURE
# define IMAGE_FAT_SIGNATURE        UINT32_C(0xcafebabe)
#endif
#ifndef IMAGE_FAT_SIGNATURE_OE
# define IMAGE_FAT_SIGNATURE_OE     UINT32_C(0xbebafeca)
#endif

typedef struct fat_arch
{
    int32_t             cputype;
    int32_t             cpusubtype;
    uint32_t            offset;
    uint32_t            size;
    uint32_t            align;
} fat_arch_t;

typedef struct mach_header_32
{
    uint32_t            magic;
    int32_t             cputype;
    int32_t             cpusubtype;
    uint32_t            filetype;
    uint32_t            ncmds;
    uint32_t            sizeofcmds;
    uint32_t            flags;
} mach_header_32_t;

/* magic */
#ifndef IMAGE_MACHO32_SIGNATURE
# define IMAGE_MACHO32_SIGNATURE    UINT32_C(0xfeedface)
#endif
#ifndef IMAGE_MACHO32_SIGNATURE_OE
# define IMAGE_MACHO32_SIGNATURE_OE UINT32_C(0xcefaedfe)
#endif
#define MH_MAGIC                    IMAGE_MACHO32_SIGNATURE
#define MH_CIGAM                    IMAGE_MACHO32_SIGNATURE_OE

typedef struct mach_header_64
{
    uint32_t            magic;          /**< 0x00 */
    int32_t             cputype;        /**< 0x04 */
    int32_t             cpusubtype;     /**< 0x08 */
    uint32_t            filetype;       /**< 0x0c */
    uint32_t            ncmds;          /**< 0x10 */
    uint32_t            sizeofcmds;     /**< 0x14 */
    uint32_t            flags;          /**< 0x18 */
    uint32_t            reserved;       /**< 0x1c */
} mach_header_64_t;
AssertCompileSize(mach_header_64_t, 0x20);

/* magic */
#ifndef IMAGE_MACHO64_SIGNATURE
# define IMAGE_MACHO64_SIGNATURE    UINT32_C(0xfeedfacf)
#endif
#ifndef IMAGE_MACHO64_SIGNATURE_OE
# define IMAGE_MACHO64_SIGNATURE_OE UINT32_C(0xfefaedfe)
#endif
#define MH_MAGIC_64                 IMAGE_MACHO64_SIGNATURE
#define MH_CIGAM_64                 IMAGE_MACHO64_SIGNATURE_OE

/* mach_header_* filetype */
#define MH_OBJECT                   UINT32_C(1)
#define MH_EXECUTE                  UINT32_C(2)
#define MH_FVMLIB                   UINT32_C(3)
#define MH_CORE                     UINT32_C(4)
#define MH_PRELOAD                  UINT32_C(5)
#define MH_DYLIB                    UINT32_C(6)
#define MH_DYLINKER                 UINT32_C(7)
#define MH_BUNDLE                   UINT32_C(8)
#define MH_DYLIB_STUB               UINT32_C(9)
#define MH_DSYM                     UINT32_C(10)
#define MH_KEXT_BUNDLE              UINT32_C(11)

/* mach_header_* flags */
#define MH_NOUNDEFS                 UINT32_C(0x00000001)
#define MH_INCRLINK                 UINT32_C(0x00000002)
#define MH_DYLDLINK                 UINT32_C(0x00000004)
#define MH_BINDATLOAD               UINT32_C(0x00000008)
#define MH_PREBOUND                 UINT32_C(0x00000010)
#define MH_SPLIT_SEGS               UINT32_C(0x00000020)
#define MH_LAZY_INIT                UINT32_C(0x00000040)
#define MH_TWOLEVEL                 UINT32_C(0x00000080)
#define MH_FORCE_FLAT               UINT32_C(0x00000100)
#define MH_NOMULTIDEFS              UINT32_C(0x00000200)
#define MH_NOFIXPREBINDING          UINT32_C(0x00000400)
#define MH_PREBINDABLE              UINT32_C(0x00000800)
#define MH_ALLMODSBOUND             UINT32_C(0x00001000)
#define MH_SUBSECTIONS_VIA_SYMBOLS  UINT32_C(0x00002000)
#define MH_CANONICAL                UINT32_C(0x00004000)
#define MH_WEAK_DEFINES             UINT32_C(0x00008000)
#define MH_BINDS_TO_WEAK            UINT32_C(0x00010000)
#define MH_ALLOW_STACK_EXECUTION    UINT32_C(0x00020000)
#define MH_ROOT_SAFE                UINT32_C(0x00040000)
#define MH_SETUID_SAFE              UINT32_C(0x00080000)
#define MH_NO_REEXPORTED_DYLIBS     UINT32_C(0x00100000)
#define MH_PIE                      UINT32_C(0x00200000)
#define MH_DEAD_STRIPPABLE_DYLIB    UINT32_C(0x00400000)
#define MH_HAS_TLV_DESCRIPTORS      UINT32_C(0x00800000)
#define MH_NO_HEAP_EXECUTION        UINT32_C(0x01000000)
#define MH_UNKNOWN                  UINT32_C(0x80000000)
#define MH_VALID_FLAGS              UINT32_C(0x81ffffff)


typedef struct load_command
{
    uint32_t            cmd;
    uint32_t            cmdsize;
} load_command_t;

/* load cmd */
#define LC_REQ_DYLD                 UINT32_C(0x80000000)
#define LC_SEGMENT_32               UINT32_C(0x01)
#define LC_SYMTAB                   UINT32_C(0x02)
#define LC_SYMSEG                   UINT32_C(0x03)
#define LC_THREAD                   UINT32_C(0x04)
#define LC_UNIXTHREAD               UINT32_C(0x05)
#define LC_LOADFVMLIB               UINT32_C(0x06)
#define LC_IDFVMLIB                 UINT32_C(0x07)
#define LC_IDENT                    UINT32_C(0x08)
#define LC_FVMFILE                  UINT32_C(0x09)
#define LC_PREPAGE                  UINT32_C(0x0a)
#define LC_DYSYMTAB                 UINT32_C(0x0b)
#define LC_LOAD_DYLIB               UINT32_C(0x0c)
#define LC_ID_DYLIB                 UINT32_C(0x0d)
#define LC_LOAD_DYLINKER            UINT32_C(0x0e)
#define LC_ID_DYLINKER              UINT32_C(0x0f)
#define LC_PREBOUND_DYLIB           UINT32_C(0x10)
#define LC_ROUTINES                 UINT32_C(0x11)
#define LC_SUB_FRAMEWORK            UINT32_C(0x12)
#define LC_SUB_UMBRELLA             UINT32_C(0x13)
#define LC_SUB_CLIENT               UINT32_C(0x14)
#define LC_SUB_LIBRARY              UINT32_C(0x15)
#define LC_TWOLEVEL_HINTS           UINT32_C(0x16)
#define LC_PREBIND_CKSUM            UINT32_C(0x17)
#define LC_LOAD_WEAK_DYLIB         (UINT32_C(0x18) | LC_REQ_DYLD)
#define LC_SEGMENT_64               UINT32_C(0x19)
#define LC_ROUTINES_64              UINT32_C(0x1a)
#define LC_UUID                     UINT32_C(0x1b)
#define LC_RPATH                   (UINT32_C(0x1c) | LC_REQ_DYLD)
#define LC_CODE_SIGNATURE           UINT32_C(0x1d)
#define LC_SEGMENT_SPLIT_INFO       UINT32_C(0x1e)
#define LC_REEXPORT_DYLIB          (UINT32_C(0x1f) | LC_REQ_DYLD)
#define LC_LAZY_LOAD_DYLIB          UINT32_C(0x20)
#define LC_ENCRYPTION_INFO          UINT32_C(0x21)
#define LC_DYLD_INFO                UINT32_C(0x22)
#define LC_DYLD_INFO_ONLY          (UINT32_C(0x22) | LC_REQ_DYLD)
#define LC_LOAD_UPWARD_DYLIB       (UINT32_C(0x23) | LC_REQ_DYLD)
#define LC_VERSION_MIN_MACOSX       UINT32_C(0x24)
#define LC_VERSION_MIN_IPHONEOS     UINT32_C(0x25)
#define LC_FUNCTION_STARTS          UINT32_C(0x26)
#define LC_DYLD_ENVIRONMENT         UINT32_C(0x27)
#define LC_MAIN                    (UINT32_C(0x28) | LC_REQ_DYLD)
#define LC_DATA_IN_CODE             UINT32_C(0x29)
#define LC_SOURCE_VERSION           UINT32_C(0x2a)  /**< source_version_command */
#define LC_DYLIB_CODE_SIGN_DRS      UINT32_C(0x2b)
#define LC_ENCRYPTION_INFO_64       UINT32_C(0x2c)
#define LC_LINKER_OPTION            UINT32_C(0x2d)
#define LC_LINKER_OPTIMIZATION_HINT UINT32_C(0x2e)
#define LC_VERSION_MIN_TVOS         UINT32_C(0x2f)
#define LC_VERSION_MIN_WATCHOS      UINT32_C(0x30)
#define LC_NOTE                     UINT32_C(0x31)
#define LC_BUILD_VERSION            UINT32_C(0x32)


typedef struct lc_str
{
    uint32_t            offset;
} lc_str_t;

typedef struct segment_command_32
{
    uint32_t            cmd;
    uint32_t            cmdsize;
    char                segname[16];
    uint32_t            vmaddr;
    uint32_t            vmsize;
    uint32_t            fileoff;
    uint32_t            filesize;
    uint32_t            maxprot;
    uint32_t            initprot;
    uint32_t            nsects;
    uint32_t            flags;
} segment_command_32_t;

typedef struct segment_command_64
{
    uint32_t            cmd;
    uint32_t            cmdsize;
    char                segname[16];
    uint64_t            vmaddr;
    uint64_t            vmsize;
    uint64_t            fileoff;
    uint64_t            filesize;
    uint32_t            maxprot;
    uint32_t            initprot;
    uint32_t            nsects;
    uint32_t            flags;
} segment_command_64_t;

/* segment flags */
#define SG_HIGHVM           UINT32_C(0x00000001)
#define SG_FVMLIB           UINT32_C(0x00000002)
#define SG_NORELOC          UINT32_C(0x00000004)
#define SG_PROTECTED_VERSION_1 UINT32_C(0x00000008)
#define SG_READ_ONLY        UINT32_C(0x00000010) /**< Make it read-only after applying fixups. @since 10.14 */

/* maxprot/initprot */
#ifndef VM_PROT_NONE
# define VM_PROT_NONE       UINT32_C(0x00000000)
# define VM_PROT_READ       UINT32_C(0x00000001)
# define VM_PROT_WRITE      UINT32_C(0x00000002)
# define VM_PROT_EXECUTE    UINT32_C(0x00000004)
# define VM_PROT_ALL        UINT32_C(0x00000007)
#endif

typedef struct section_32
{
    char                sectname[16];
    char                segname[16];
    uint32_t            addr;
    uint32_t            size;
    uint32_t            offset;
    uint32_t            align;
    uint32_t            reloff;
    uint32_t            nreloc;
    uint32_t            flags;
    /** For S_LAZY_SYMBOL_POINTERS, S_NON_LAZY_SYMBOL_POINTERS and S_SYMBOL_STUBS
     * this is the index into the indirect symbol table. */
    uint32_t            reserved1;
    /** For S_SYMBOL_STUBS this is the entry size. */
    uint32_t            reserved2;
} section_32_t;

typedef struct section_64
{
    char                sectname[16];
    char                segname[16];
    uint64_t            addr;
    uint64_t            size;
    uint32_t            offset;
    uint32_t            align;
    uint32_t            reloff;
    uint32_t            nreloc;
    uint32_t            flags;
    /** For S_LAZY_SYMBOL_POINTERS, S_NON_LAZY_SYMBOL_POINTERS and S_SYMBOL_STUBS
     * this is the index into the indirect symbol table. */
    uint32_t            reserved1;
    uint32_t            reserved2;
    uint32_t            reserved3;
} section_64_t;

/* section flags */
#define SECTION_TYPE                            UINT32_C(0xff)
#define S_REGULAR                               UINT32_C(0x00)
#define S_ZEROFILL                              UINT32_C(0x01)
#define S_CSTRING_LITERALS                      UINT32_C(0x02)
#define S_4BYTE_LITERALS                        UINT32_C(0x03)
#define S_8BYTE_LITERALS                        UINT32_C(0x04)
#define S_LITERAL_POINTERS                      UINT32_C(0x05)
#define S_NON_LAZY_SYMBOL_POINTERS              UINT32_C(0x06)
#define S_LAZY_SYMBOL_POINTERS                  UINT32_C(0x07)
#define S_SYMBOL_STUBS                          UINT32_C(0x08)
#define S_MOD_INIT_FUNC_POINTERS                UINT32_C(0x09)
#define S_MOD_TERM_FUNC_POINTERS                UINT32_C(0x0a)
#define S_COALESCED                             UINT32_C(0x0b)
#define S_GB_ZEROFILL                           UINT32_C(0x0c)
#define S_INTERPOSING                           UINT32_C(0x0d)
#define S_16BYTE_LITERALS                       UINT32_C(0x0e)
#define S_DTRACE_DOF                            UINT32_C(0x0f)
#define S_LAZY_DYLIB_SYMBOL_POINTERS            UINT32_C(0x10)
#define S_THREAD_LOCAL_REGULAR                  UINT32_C(0x11)
#define S_THREAD_LOCAL_ZEROFILL                 UINT32_C(0x12)
#define S_THREAD_LOCAL_VARIABLES                UINT32_C(0x13)
#define S_THREAD_LOCAL_VARIABLE_POINTERS        UINT32_C(0x14)
#define S_THREAD_LOCAL_INIT_FUNCTION_POINTERS   UINT32_C(0x15)




#define SECTION_ATTRIBUTES          UINT32_C(0xffffff00)
#define SECTION_ATTRIBUTES_USR      UINT32_C(0xff000000)
#define S_ATTR_PURE_INSTRUCTIONS    UINT32_C(0x80000000)
#define S_ATTR_NO_TOC               UINT32_C(0x40000000)
#define S_ATTR_STRIP_STATIC_SYMS    UINT32_C(0x20000000)
#define S_ATTR_NO_DEAD_STRIP        UINT32_C(0x10000000)
#define S_ATTR_LIVE_SUPPORT         UINT32_C(0x08000000)
#define S_ATTR_SELF_MODIFYING_CODE  UINT32_C(0x04000000)
#define S_ATTR_DEBUG                UINT32_C(0x02000000)
#define SECTION_ATTRIBUTES_SYS      UINT32_C(0x00ffff00)
#define S_ATTR_SOME_INSTRUCTIONS    UINT32_C(0x00000400)
#define S_ATTR_EXT_RELOC            UINT32_C(0x00000200)
#define S_ATTR_LOC_RELOC            UINT32_C(0x00000100)

/* standard section names */
#define SEG_PAGEZERO                "__PAGEZERO"
#define SEG_TEXT                    "__TEXT"
#define SECT_TEXT                   "__text"
#define SECT_FVMLIB_INIT0           "__fvmlib_init0"
#define SECT_FVMLIB_INIT1           "__fvmlib_init1"
#define SEG_DATA                    "__DATA"
#define SECT_DATA                   "__data"
#define SECT_BSS                    "__bss"
#define SECT_COMMON                 "__common"
#define SEG_OBJC                    "__OBJC"
#define SECT_OBJC_SYMBOLS           "__symbol_table"
#define SECT_OBJC_MODULES           "__module_info"
#define SECT_OBJC_STRINGS           "__selector_strs"
#define SECT_OBJC_REFS              "__selector_refs"
#define SEG_ICON                    "__ICON"
#define SECT_ICON_HEADER            "__header"
#define SECT_ICON_TIFF              "__tiff"
#define SEG_LINKEDIT                "__LINKEDIT"
#define SEG_UNIXSTACK               "__UNIXSTACK"
#define SEG_IMPORT                  "__IMPORT"

typedef struct thread_command
{
    uint32_t            cmd;
    uint32_t            cmdsize;
} thread_command_t;

typedef struct symtab_command
{
    uint32_t            cmd;
    uint32_t            cmdsize;
    uint32_t            symoff;
    uint32_t            nsyms;
    uint32_t            stroff;
    uint32_t            strsize;
} symtab_command_t;

typedef struct dysymtab_command
{
    uint32_t            cmd;
    uint32_t            cmdsize;
    /** @name Symbol groupings.
     * @{ */
    uint32_t            ilocalsym;          /**< Index into the symbol table of the first local symbol. */
    uint32_t            nlocalsym;          /**< Number of local symbols. */
    uint32_t            iextdefsym;         /**< Index into the symbol table of the first externally defined symbol. */
    uint32_t            nextdefsym;         /**< Number of externally defined symbols. */
    uint32_t            iundefsym;          /**< Index into the symbol table of the first undefined symbol. */
    uint32_t            nundefsym;          /**< Number of undefined symbols. */
    /** @} */
    uint32_t            tocoff;             /**< Table of content file offset. (usually empty) */
    uint32_t            ntoc;               /**< Number of entries in TOC. */
    uint32_t            modtaboff;          /** The module table file offset. (usually empty) */
    uint32_t            nmodtab;            /**< Number of entries in the module table. */
    /** @name Dynamic symbol tables.
     * @{ */
    uint32_t            extrefsymoff;       /**< Externally referenceable symbol table file offset. @sa dylib_reference_t */
    uint32_t            nextrefsym;         /**< Number externally referenceable symbols. */
    uint32_t            indirectsymboff;    /**< Indirect symbol table (32-bit symtab indexes) for thunks and offset tables. */
    uint32_t            nindirectsymb;      /**< Number of indirect symbol table entries. */
    /** @} */
    /** @name Relocations.
     * @{ */
    uint32_t            extreloff;          /**< External relocations (r_address is relative to first segment (i.e. RVA)). */
    uint32_t            nextrel;            /**< Number of external relocations. */
    uint32_t            locreloff;          /**< Local relocations (r_address is relative to first segment (i.e. RVA)). */
    uint32_t            nlocrel;            /**< Number of local relocations. */
    /** @} */
} dysymtab_command_t;
AssertCompileSize(dysymtab_command_t, 80);

/** Special indirect symbol table entry value, stripped local symbol. */
#define INDIRECT_SYMBOL_LOCAL   UINT32_C(0x80000000)
/** Special indirect symbol table entry value, stripped absolute symbol. */
#define INDIRECT_SYMBOL_ABS     UINT32_C(0x40000000)

typedef struct dylib_reference
{
    uint32_t            isym : 24;          /**< Symbol table index. */
    uint32_t            flags : 8;          /**< REFERENCE_FLAG_XXX? */
} dylib_reference_t;
AssertCompileSize(dylib_reference_t, 4);


typedef struct dylib_table_of_contents
{
    uint32_t            symbol_index;       /**< External symbol table entry. */
    uint32_t            module_index;       /**< The module table index of the module defining it. */
} dylib_table_of_contents_t;
AssertCompileSize(dylib_table_of_contents_t, 8);


/** 32-bit module table entry. */
typedef struct dylib_module
{
    uint32_t            module_name;
    uint32_t            iextdefsym;
    uint32_t            nextdefsym;
    uint32_t            irefsym;
    uint32_t            nrefsym;
    uint32_t            ilocalsym;
    uint32_t            nlocalsym;
    uint32_t            iextrel;
    uint32_t            nextrel;
    uint32_t            iinit_iterm;
    uint32_t            ninit_nterm;
    uint32_t            objc_module_info_addr;
    uint32_t            objc_module_info_size;
} dylib_module_32_t;
AssertCompileSize(dylib_module_32_t, 13*4);

/* a 64-bit module table entry */
typedef struct dylib_module_64
{
    uint32_t            module_name;
    uint32_t            iextdefsym;
    uint32_t            nextdefsym;
    uint32_t            irefsym;
    uint32_t            nrefsym;
    uint32_t            ilocalsym;
    uint32_t            nlocalsym;
    uint32_t            iextrel;
    uint32_t            nextrel;
    uint32_t            iinit_iterm;
    uint32_t            ninit_nterm;
    uint32_t            objc_module_info_size;
    uint64_t            objc_module_info_addr;
} dylib_module_64_t;
AssertCompileSize(dylib_module_64_t, 12*4+8);

typedef struct uuid_command
{
    uint32_t            cmd;
    uint32_t            cmdsize;
    uint8_t             uuid[16];
} uuid_command_t;
AssertCompileSize(uuid_command_t, 24);

typedef struct linkedit_data_command
{
    uint32_t            cmd;        /**< LC_CODE_SIGNATURE, LC_SEGMENT_SPLIT_INFO, LC_FUNCTION_STARTS */
    uint32_t            cmdsize;    /**< Size of this structure (16). */
    uint32_t            dataoff;    /**< Offset into the file of the data. */
    uint32_t            datasize;   /**< The size of the data. */
} linkedit_data_command_t;
AssertCompileSize(linkedit_data_command_t, 16);

typedef struct version_min_command
{
    uint32_t            cmd;        /**< LC_VERSION_MIN_MACOSX, LC_VERSION_MIN_IPHONEOS, LC_VERSION_MIN_TVOS, LC_VERSION_MIN_WATCHOS */
    uint32_t            cmdsize;    /**< Size of this structure (16). */
    uint32_t            version;    /**< 31..16=major, 15..8=minor, 7..0=patch. */
    uint32_t            reserved;   /**< MBZ. */
} version_min_command_t;
AssertCompileSize(version_min_command_t, 16);

typedef struct build_tool_version
{
    uint32_t            tool;       /**< TOOL_XXX */
    uint32_t            version;    /**< 31..16=major, 15..8=minor, 7..0=patch. */
} build_tool_version_t;
AssertCompileSize(build_tool_version_t, 8);

/** @name TOOL_XXX - Values for build_tool_version::tool
 * @{ */
#define TOOL_CLANG          1
#define TOOL_SWIFT          2
#define TOOL_LD             3
/** @} */

typedef struct build_version_command
{
    uint32_t            cmd;        /**< LC_BUILD_VERSION */
    uint32_t            cmdsize;    /**< Size of this structure (at least 24). */
    uint32_t            platform;   /**< PLATFORM_XXX  */
    uint32_t            minos;      /**< Minimum OS version: 31..16=major, 15..8=minor, 7..0=patch */
    uint32_t            sdk;        /**< SDK version:        31..16=major, 15..8=minor, 7..0=patch */
    uint32_t            ntools;     /**< Number of build_tool_version entries following in aTools. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    build_tool_version_t aTools[RT_FLEXIBLE_ARRAY];
} build_version_command_t;
AssertCompileMemberOffset(build_version_command_t, aTools, 24);

/** @name PLATFORM_XXX - Values for build_version_command::platform
 * @{ */
#define PLATFORM_MACOS      1
#define PLATFORM_IOS        2
#define PLATFORM_TVOS       3
#define PLATFORM_WATCHOS    4
/** @} */

typedef struct source_version_command
{
    uint32_t            cmd;        /**< LC_SOURCE_VERSION */
    uint32_t            cmdsize;    /**< Size of this structure (16). */
    uint64_t            version;    /**< A.B.C.D.E, where A is 24 bits wide and the rest 10 bits each. */
} source_version_command_t;
AssertCompileSize(source_version_command_t, 16);


typedef struct macho_nlist_32
{
    union
    {
        int32_t         n_strx;
    }                   n_un;
    uint8_t             n_type;
    uint8_t             n_sect;
    int16_t             n_desc;
    uint32_t            n_value;
} macho_nlist_32_t;


typedef struct macho_nlist_64
{
    union
    {
        uint32_t        n_strx;
    }                   n_un;
    uint8_t             n_type;
    uint8_t             n_sect;
    int16_t             n_desc;
    uint64_t            n_value;
} macho_nlist_64_t;

#define MACHO_N_EXT                 UINT8_C(0x01)
#define MACHO_N_PEXT                UINT8_C(0x10)

#define MACHO_N_TYPE                UINT8_C(0x0e)
#define MACHO_N_UNDF                UINT8_C(0x00)
#define MACHO_N_ABS                 UINT8_C(0x02)
#define MACHO_N_INDR                UINT8_C(0x0a)
#define MACHO_N_PBUD                UINT8_C(0x0c)
#define MACHO_N_SECT                UINT8_C(0x0e)

#define MACHO_N_STAB                UINT8_C(0xe0)
#define MACHO_N_GSYM                UINT8_C(0x20)
#define MACHO_N_FNAME               UINT8_C(0x22)
#define MACHO_N_FUN                 UINT8_C(0x24)
#define MACHO_N_STSYM               UINT8_C(0x26)
#define MACHO_N_LCSYM               UINT8_C(0x28)
#define MACHO_N_BNSYM               UINT8_C(0x2e)
#define MACHO_N_PC                  UINT8_C(0x30)
#define MACHO_N_OPT                 UINT8_C(0x3c)
#define MACHO_N_RSYM                UINT8_C(0x40)
#define MACHO_N_SLINE               UINT8_C(0x44)
#define MACHO_N_ENSYM               UINT8_C(0x4e)
#define MACHO_N_SSYM                UINT8_C(0x60)
#define MACHO_N_SO                  UINT8_C(0x64)
#define MACHO_N_OSO                 UINT8_C(0x66)
#define MACHO_N_LSYM                UINT8_C(0x80)
#define MACHO_N_BINCL               UINT8_C(0x82)
#define MACHO_N_SOL                 UINT8_C(0x84)
#define MACHO_N_PARAMS              UINT8_C(0x86)
#define MACHO_N_VERSION             UINT8_C(0x88)
#define MACHO_N_OLEVEL              UINT8_C(0x8A)
#define MACHO_N_PSYM                UINT8_C(0xa0)
#define MACHO_N_EINCL               UINT8_C(0xa2)
#define MACHO_N_ENTRY               UINT8_C(0xa4)
#define MACHO_N_LBRAC               UINT8_C(0xc0)
#define MACHO_N_EXCL                UINT8_C(0xc2)
#define MACHO_N_RBRAC               UINT8_C(0xe0)
#define MACHO_N_BCOMM               UINT8_C(0xe2)
#define MACHO_N_ECOMM               UINT8_C(0xe4)
#define MACHO_N_ECOML               UINT8_C(0xe8)
#define MACHO_N_LENG                UINT8_C(0xfe)

#define MACHO_NO_SECT               UINT8_C(0x00)
#define MACHO_MAX_SECT              UINT8_C(0xff)

#define REFERENCE_TYPE              UINT16_C(0x000f)
#define REFERENCE_FLAG_UNDEFINED_NON_LAZY             0
#define REFERENCE_FLAG_UNDEFINED_LAZY                 1
#define REFERENCE_FLAG_DEFINED                        2
#define REFERENCE_FLAG_PRIVATE_DEFINED                3
#define REFERENCE_FLAG_PRIVATE_UNDEFINED_NON_LAZY     4
#define REFERENCE_FLAG_PRIVATE_UNDEFINED_LAZY         5
#define REFERENCED_DYNAMICALLY      UINT16_C(0x0010)

#define GET_LIBRARY_ORDINAL(a_n_desc) \
    RT_BYTE2(a_n_desc)
#define SET_LIBRARY_ORDINAL(a_n_desc, a_ordinal) \
    do { (a_n_desc) = RT_MAKE_U16(RT_BYTE1(a_n_desc), a_ordinal); } while (0)

#define SELF_LIBRARY_ORDINAL        0x00
#define MAX_LIBRARY_ORDINAL         0xfd
#define DYNAMIC_LOOKUP_ORDINAL      0xfe
#define EXECUTABLE_ORDINAL          0xff

#define N_NO_DEAD_STRIP             UINT16_C(0x0020)
#define N_DESC_DISCARDED            UINT16_C(0x0020)
#define N_WEAK_REF                  UINT16_C(0x0040)
#define N_WEAK_DEF                  UINT16_C(0x0080)
#define N_REF_TO_WEAK               UINT16_C(0x0080)
#define N_SYMBOL_RESOLVER           UINT16_C(0x0100)
#define N_ALT_ENTRY                 UINT16_C(0x0200)

typedef struct macho_relocation_info
{
    int32_t             r_address;
    uint32_t            r_symbolnum : 24;
    uint32_t            r_pcrel     : 1;
    uint32_t            r_length    : 2;
    uint32_t            r_extern    : 1;
    uint32_t            r_type      : 4;
} macho_relocation_info_t;
AssertCompileSize(macho_relocation_info_t, 8);

#define R_ABS                       0
#define R_SCATTERED                 UINT32_C(0x80000000)

typedef struct scattered_relocation_info
{
#ifdef RT_LITTLE_ENDIAN
    uint32_t            r_address   : 24;
    uint32_t            r_type      : 4;
    uint32_t            r_length    : 2;
    uint32_t            r_pcrel     : 1;
    uint32_t            r_scattered : 1;
#elif defined(RT_BIG_ENDIAN)
    uint32_t            r_scattered : 1;
    uint32_t            r_pcrel     : 1;
    uint32_t            r_length    : 2;
    uint32_t            r_type      : 4;
    uint32_t            r_address   : 24;
#else
# error "Neither K_ENDIAN isn't LITTLE or BIG!"
#endif
    int32_t             r_value;
} scattered_relocation_info_t;
AssertCompileSize(scattered_relocation_info_t, 8);

typedef union
{
    macho_relocation_info_t     r;
    scattered_relocation_info_t s;
} macho_relocation_union_t;
AssertCompileSize(macho_relocation_union_t, 8);

typedef enum reloc_type_generic
{
    GENERIC_RELOC_VANILLA = 0,
    GENERIC_RELOC_PAIR,
    GENERIC_RELOC_SECTDIFF,
    GENERIC_RELOC_PB_LA_PTR,
    GENERIC_RELOC_LOCAL_SECTDIFF
} reloc_type_generic_t;

typedef enum reloc_type_x86_64
{
    X86_64_RELOC_UNSIGNED = 0,
    X86_64_RELOC_SIGNED,
    X86_64_RELOC_BRANCH,
    X86_64_RELOC_GOT_LOAD,
    X86_64_RELOC_GOT,
    X86_64_RELOC_SUBTRACTOR,
    X86_64_RELOC_SIGNED_1,
    X86_64_RELOC_SIGNED_2,
    X86_64_RELOC_SIGNED_4
} reloc_type_x86_64_t;

#endif /* !IPRT_INCLUDED_formats_mach_o_h */

