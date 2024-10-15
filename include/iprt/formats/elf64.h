/* $Id: elf64.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ELF 64-bit header.
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

#ifndef IPRT_INCLUDED_formats_elf64_h
#define IPRT_INCLUDED_formats_elf64_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assertcompile.h>
#include "elf-common.h"

/*
 * ELF 64 standard types.
 */
typedef uint64_t        Elf64_Addr;
typedef uint64_t        Elf64_Off;
typedef uint16_t        Elf64_Half;
typedef uint32_t        Elf64_Word;
typedef int32_t         Elf64_Sword;
typedef uint64_t        Elf64_Xword;
typedef int64_t         Elf64_Sxword;

/*
 * Ensure type size correctness in accordance to ELF-64 Object File Format, Version 1.5 Draft 2, p2.
 */
AssertCompileSize(Elf64_Addr,    8);
AssertCompileSize(Elf64_Off,     8);
AssertCompileSize(Elf64_Half,    2);
AssertCompileSize(Elf64_Word,    4);
AssertCompileSize(Elf64_Sword,   4);
AssertCompileSize(Elf64_Xword,   8);
AssertCompileSize(Elf64_Sxword,  8);

/*
 * ELF 64 non-standard types for convenience.
 */
typedef Elf64_Xword     Elf64_Size;
typedef Elf64_Word      Elf64_Hashelt;

/*
 * ELF Header.
 */
typedef struct
{
    unsigned char e_ident[16];      /* ELF identification. */
    Elf64_Half    e_type;           /* Object file type. */
    Elf64_Half    e_machine;        /* Machine type. */
    Elf64_Word    e_version;        /* Object file version. */
    Elf64_Addr    e_entry;          /* Entry point address. */
    Elf64_Off     e_phoff;          /* Program header offset. */
    Elf64_Off     e_shoff;          /* Section header offset. */
    Elf64_Word    e_flags;          /* Processor-specific flags. */
    Elf64_Half    e_ehsize;         /* ELF header size. */
    Elf64_Half    e_phentsize;      /* Size of program header entry. */
    Elf64_Half    e_phnum;          /* Number of program header entries. */
    Elf64_Half    e_shentsize;      /* Size of section header entry. */
    Elf64_Half    e_shnum;          /* Number of section header entries. */
    Elf64_Half    e_shstrndx;       /* Section name string table index. */
} Elf64_Ehdr;

/*
 * Section header.
 */
typedef struct
{
    Elf64_Word    sh_name;          /* Section name. */
    Elf64_Word    sh_type;          /* Section type. */
    Elf64_Xword   sh_flags;         /* Section attributes. */
    Elf64_Addr    sh_addr;          /* Virtual address in memory. */
    Elf64_Off     sh_offset;        /* Offset in file. */
    Elf64_Xword   sh_size;          /* Size of section. */
    Elf64_Word    sh_link;          /* Link to other section. */
    Elf64_Word    sh_info;          /* Miscellaneous information. */
    Elf64_Xword   sh_addralign;     /* Address alignment boundary. */
    Elf64_Xword   sh_entsize;       /* Size of entries, if section has table. */
} Elf64_Shdr;

/*
 * Program header.
 */
typedef struct
{
    Elf64_Word    p_type;           /* Type of segment. */
    Elf64_Word    p_flags;          /* Segment attributes. */
    Elf64_Off     p_offset;         /* Offset in file. */
    Elf64_Addr    p_vaddr;          /* Virtual address in memory. */
    Elf64_Addr    p_paddr;          /* Physical address (reserved). */
    Elf64_Xword   p_filesz;         /* Size of segment in file. */
    Elf64_Xword   p_memsz;          /* Size of segment in memory. */
    Elf64_Xword   p_align;          /* Alignment of segment. */
} Elf64_Phdr;

/*
 * Note header.
 */
typedef struct
{
    Elf64_Word    n_namesz;         /* Length of note's name. */
    Elf64_Word    n_descsz;         /* Length of note's description. */
    Elf64_Word    n_type;           /* Type of note. */
} Elf64_Nhdr;

/*
 * Symbol table entry.
 */
typedef struct
{
    Elf64_Word    st_name;          /* Symbol name. */
    unsigned char st_info;          /* Type and binding attributes. */
    unsigned char st_other;         /* Reserved. */
    Elf64_Half    st_shndx;         /* Section header table index. */
    Elf64_Addr    st_value;         /* Symbol value. */
    Elf64_Xword   st_size;          /* Size associated with symbol. */
} Elf64_Sym;

/*
 * Relocations.
 */
typedef struct
{
    Elf64_Addr    r_offset;         /* Location to be relocated. */
    Elf64_Xword   r_info;           /* Symbol index and type of relocation. */
} Elf64_Rel;

typedef struct
{
    Elf64_Addr    r_offset;         /* Location to be relocated. */
    Elf64_Xword   r_info;           /* Symbol index and type of relocation. */
    Elf64_Sxword  r_addend;         /* Constant part of expression. */
} Elf64_Rela;

/*
 * Dynamic section entry.
 * ".dynamic" section contains an array of this.
 */
typedef struct
{
    Elf64_Sxword  d_tag;            /* Type of entry. */
    union
    {
        Elf64_Xword   d_val;        /* Integer value. */
        Elf64_Addr    d_ptr;        /* Virtual address value. */
    } d_un;
} Elf64_Dyn;

/*
 * Helper macros.
 */
/** The symbol's type. */
#define ELF64_ST_TYPE(info)         ((info) & 0xF)
/** The symbol's binding. */
#define ELF64_ST_BIND(info)         ((info) >> 4)
/** Make st_info. given binding and type. */
#define ELF64_ST_INFO(bind, type)   (((bind) << 4) + ((type) & 0xf))

/** Relocation type. */
#define ELF64_R_TYPE(info)          ((unsigned char)(info))
/** Relocation symbol index. */
#define ELF64_R_SYM(info)           ((info) >> 32)
/** Make r_info given the symbol index and type.  */
#define ELF64_R_INFO(sym, type)     (((sym) << 32) + (unsigned char)(type))


#endif /* !IPRT_INCLUDED_formats_elf64_h */

