/* $Id: DBGPlugInCommonELF.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGPlugInCommonELF - Common code for dealing with ELF images, Header.
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

#ifndef DEBUGGER_INCLUDED_SRC_DBGPlugInCommonELF_h
#define DEBUGGER_INCLUDED_SRC_DBGPlugInCommonELF_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <iprt/formats/elf32.h>
#include <iprt/formats/elf64.h>

/** @name DBGDiggerCommonParseElf32Mod and DBGDiggerCommonParseElf64Mod flags
 * @{ */
/** Whether to adjust the symbol values or not. */
#define DBG_DIGGER_ELF_ADJUST_SYM_VALUE     RT_BIT_32(0)
/** Indicates that we're missing section headers and that
 * all section indexes are to be considered invalid. (Solaris hack.)
 * This flag is incompatible with DBG_DIGGER_ELF_ADJUST_SYM_VALUE. */
#define DBG_DIGGER_ELF_FUNNY_SHDRS          RT_BIT_32(1)
/** Valid bit mask. */
#define DBG_DIGGER_ELF_MASK                 UINT32_C(0x00000003)
/** @} */

int DBGDiggerCommonParseElf32Mod(PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszModName, const char *pszFilename, uint32_t fFlags,
                                 Elf32_Ehdr const *pEhdr, Elf32_Shdr const *paShdrs,
                                 Elf32_Sym const *paSyms, size_t cMaxSyms,
                                 char const *pbStrings, size_t cbMaxStrings,
                                 RTGCPTR MinAddr, RTGCPTR MaxAddr, uint64_t uModTag);

int DBGDiggerCommonParseElf64Mod(PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszModName, const char *pszFilename, uint32_t fFlags,
                                 Elf64_Ehdr const *pEhdr, Elf64_Shdr const *paShdrs,
                                 Elf64_Sym const *paSyms, size_t cMaxSyms,
                                 char const *pbStrings, size_t cbMaxStrings,
                                 RTGCPTR MinAddr, RTGCPTR MaxAddr, uint64_t uModTag);

#endif /* !DEBUGGER_INCLUDED_SRC_DBGPlugInCommonELF_h */

