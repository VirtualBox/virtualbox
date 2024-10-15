/* $Id: elf.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ELF types, current architecture.
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

#ifndef IPRT_INCLUDED_formats_elf_h
#define IPRT_INCLUDED_formats_elf_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_ARM64)
# include "elf64.h"
typedef Elf64_Addr          Elf_Addr;
typedef Elf64_Half          Elf_Half;
typedef Elf64_Off           Elf_Off;
typedef Elf64_Sword         Elf_Sword;
typedef Elf64_Word          Elf_Word;
typedef Elf64_Size          Elf_Size;
typedef Elf64_Hashelt       Elf_Hashelt;
typedef Elf64_Ehdr          Elf_Ehdr;
typedef Elf64_Shdr          Elf_Shdr;
typedef Elf64_Phdr          Elf_Phdr;
typedef Elf64_Nhdr          Elf_Nhdr;
typedef Elf64_Dyn           Elf_Dyn;
typedef Elf64_Rel           Elf_Rel;
typedef Elf64_Rela          Elf_Rela;
typedef Elf64_Sym           Elf_Sym;

#define ELF_R_SYM           ELF64_R_SYM
#define ELF_R_TYPE          ELF64_R_TYPE
#define ELF_R_INFO          ELF64_R_INFO
#define ELF_ST_BIND         ELF64_ST_BIND
#define ELF_ST_TYPE         ELF64_ST_TYPE
#define ELF_ST_INFO         ELF64_ST_INFO

#elif defined(RT_ARCH_X86)
# include "elf32.h"
typedef Elf32_Addr          Elf_Addr;
typedef Elf32_Half          Elf_Half;
typedef Elf32_Off           Elf_Off;
typedef Elf32_Sword         Elf_Sword;
typedef Elf32_Word          Elf_Word;
typedef Elf32_Size          Elf_Size;
typedef Elf32_Hashelt       Elf_Hashelt;
typedef Elf32_Ehdr          Elf_Ehdr;
typedef Elf32_Shdr          Elf_Shdr;
typedef Elf32_Phdr          Elf_Phdr;
typedef Elf32_Nhdr          Elf_Nhdr;
typedef Elf32_Dyn           Elf_Dyn;
typedef Elf32_Rel           Elf_Rel;
typedef Elf32_Rela          Elf_Rela;
typedef Elf32_Sym           Elf_Sym;

#define ELF_R_SYM           ELF32_R_SYM
#define ELF_R_TYPE          ELF32_R_TYPE
#define ELF_R_INFO          ELF32_R_INFO
#define ELF_ST_BIND         ELF32_ST_BIND
#define ELF_ST_TYPE         ELF32_ST_TYPE
#define ELF_ST_INFO         ELF32_ST_INFO

#else
# error Unknown arch!
#endif

#endif /* !IPRT_INCLUDED_formats_elf_h */

