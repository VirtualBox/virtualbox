/* $Id: elf32.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ELF 32-bit header.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_elf32_h
#define IPRT_INCLUDED_formats_elf32_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assertcompile.h>
#include "elf-common.h"

/*
 * ELF 32 standard types.
 */
typedef uint32_t        Elf32_Addr;
typedef uint16_t        Elf32_Half;
typedef uint32_t        Elf32_Off;
typedef int32_t         Elf32_Sword;
typedef uint32_t        Elf32_Word;

/*
 * Ensure type size correctness in accordance to                                                             .
 * Portable Format Specification (for ELF), Version 1.1, fig 1-2.                                            .
 */
AssertCompileSize(Elf32_Addr,   4);
AssertCompileSize(Elf32_Half,   2);
AssertCompileSize(Elf32_Off,    4);
AssertCompileSize(Elf32_Sword,  4);
AssertCompileSize(Elf32_Word,   4);

/*
 * ELF 32 non-standard types for convenience.
 */
typedef Elf32_Word      Elf32_Size;
typedef Elf32_Word      Elf32_Hashelt;

/*
 * ELF header.
 */
typedef struct
{
    unsigned char e_ident[16];      /* ELF identification. */
    Elf32_Half    e_type;           /* Object file type. */
    Elf32_Half    e_machine;        /* Machine type. */
    Elf32_Word    e_version;        /* Object file version. */
    Elf32_Addr    e_entry;          /* Entry point address. */
    Elf32_Off     e_phoff;          /* Program header offset. */
    Elf32_Off     e_shoff;          /* Section header offset. */
    Elf32_Word    e_flags;          /* Processor-specific flags. */
    Elf32_Half    e_ehsize;         /* ELF header size. */
    Elf32_Half    e_phentsize;      /* Size of program header entries. */
    Elf32_Half    e_phnum;          /* Number of program headers. */
    Elf32_Half    e_shentsize;      /* Size of section header entries. */
    Elf32_Half    e_shnum;          /* Number of section headers. */
    Elf32_Half    e_shstrndx;       /* Section name string table index. */
} Elf32_Ehdr;

/*
 * Section header.
 */
typedef struct
{
    Elf32_Word    sh_name;          /* Section name. */
    Elf32_Word    sh_type;          /* Section type. */
    Elf32_Word    sh_flags;         /* Section attributes. */
    Elf32_Addr    sh_addr;          /* Virtual address in memory. */
    Elf32_Off     sh_offset;        /* Offset in file. */
    Elf32_Word    sh_size;          /* Size of section. */
    Elf32_Word    sh_link;          /* Link to other section. */
    Elf32_Word    sh_info;          /* Miscellaneous information. */
    Elf32_Word    sh_addralign;     /* Address alignment boundary. */
    Elf32_Word    sh_entsize;       /* Size of entries, if section has table. */
} Elf32_Shdr;


/*
 * Program header.
 */
typedef struct
{
    Elf32_Word    p_type;           /* Type of segment. */
    Elf32_Off     p_offset;         /* Offset in file. */
    Elf32_Addr    p_vaddr;          /* Virtual address in memory. */
    Elf32_Addr    p_paddr;          /* Physical address (reserved). */
    Elf32_Word    p_filesz;         /* Size of segment in file. */
    Elf32_Word    p_memsz;          /* Size of segment in memory. */
    Elf32_Word    p_flags;          /* Segment attributes. */
    Elf32_Word    p_align;          /* Alignment of segment. */
} Elf32_Phdr;


/*
 * Note header.
 */
typedef struct
{
    Elf32_Word    n_namesz;         /* Length of note's name. */
    Elf32_Word    n_descsz;         /* Length of note's description. */
    Elf32_Word    n_type;           /* Type of note. */
} Elf32_Nhdr;


/*
 * Symbol table entry.
 */
typedef struct
{
    Elf32_Word    st_name;          /* Symbol name. */
    Elf32_Addr    st_value;         /* Symbol value. */
    Elf32_Word    st_size;          /* Size associated with symbol. */
    unsigned char st_info;          /* Type and binding attributes. */
    unsigned char st_other;         /* Reserved. */
    Elf32_Half    st_shndx;         /* Section header table index. */
} Elf32_Sym;


/*
 * Relocations.
 */
typedef struct
{
    Elf32_Addr    r_offset;         /* Location to be relocated. */
    Elf32_Word    r_info;           /* Symbol index and type of relocation. */
} Elf32_Rel;

typedef struct
{
    Elf32_Addr    r_offset;         /* Location to be relocated. */
    Elf32_Word    r_info;           /* Symbol index and type of relocation. */
    Elf32_Sword   r_addend;         /* Constant part of expression. */
} Elf32_Rela;

/*
 * Dynamic section entry.
 * ".dynamic" section contains an array of this.
 */
typedef struct
{
    Elf32_Sword   d_tag;            /* Type of entry. */
    union
    {
        Elf32_Word    d_val;        /* Integer value. */
        Elf32_Addr    d_ptr;        /* Virtual address value. */
    } d_un;
} Elf32_Dyn;

/*
 * Helper macros.
 */
/** The symbol's type. */
#define ELF32_ST_TYPE(info)         ((info) & 0xF)
/** The symbol's binding. */
#define ELF32_ST_BIND(info)         ((info) >> 4)
/** Make st_info. given binding and type. */
#define ELF32_ST_INFO(bind, type)   (((bind) << 4) + ((type) & 0xf))

/** Relocation type. */
#define ELF32_R_TYPE(info)          ((unsigned char)(info))
/** Relocation symbol index. */
#define ELF32_R_SYM(info)           ((info) >> 8)
/** Make r_info given the symbol index and type.  */
#define ELF32_R_INFO(sym, type)     (((sym) << 8) + (unsigned char)(type))


#endif /* !IPRT_INCLUDED_formats_elf32_h */

