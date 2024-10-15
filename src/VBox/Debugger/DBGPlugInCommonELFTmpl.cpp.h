/* $Id: DBGPlugInCommonELFTmpl.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGPlugInCommonELF - Code Template for dealing with one kind of ELF.
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

#if ELF_MODE == 32
# define Elf_Ehdr                   Elf32_Ehdr
# define Elf_Shdr                   Elf32_Shdr
# define Elf_Phdr                   Elf32_Phdr
# define Elf_Sym                    Elf32_Sym
# define MY_ELFCLASS                ELFCLASS32
# define ELF_ST_BIND                ELF32_ST_BIND
# define DBGDiggerCommonParseElfMod DBGDiggerCommonParseElf32Mod
#else
# define Elf_Ehdr                   Elf64_Ehdr
# define Elf_Shdr                   Elf64_Shdr
# define Elf_Phdr                   Elf64_Phdr
# define Elf_Sym                    Elf64_Sym
# define MY_ELFCLASS                ELFCLASS64
# define ELF_ST_BIND                ELF64_ST_BIND
# define DBGDiggerCommonParseElfMod DBGDiggerCommonParseElf64Mod
#endif


/**
 * Common ELF module parser.
 *
 * It takes the essential bits of the ELF module (elf header, section headers,
 * symbol table and string table), and inserts/updates the module and symbols.
 *
 *
 * @returns VBox status code.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   pVMM            The VMM function table.
 * @param   pszModName      The module name.
 * @param   pszFilename     The filename. optional.
 * @param   fFlags          Flags.
 * @param   pEhdr           Pointer to the ELF header.
 * @param   paShdrs         Pointer to the section headers. The caller must verify that
 *                          the e_shnum member of the ELF header is within the bounds of
 *                          this table. The caller should also adjust the section addresses
 *                          so these correspond to actual load addresses.
 * @param   paSyms          Pointer to the symbol table.
 * @param   cMaxSyms        The maximum number of symbols paSyms may hold. This isn't
 *                          the exact count, it's just a cap for avoiding SIGSEGVs
 *                          and general corruption.
 * @param   pbStrings       Pointer to the string table.
 * @param   cbMaxStrings    The size of the memory pbStrings points to. This doesn't
 *                          have to match the string table size exactly, it's just to
 *                          avoid SIGSEGV when a bad string index is encountered.
 * @param   MinAddr         Min address to care about.
 * @param   MaxAddr         Max address to care about (inclusive).  Together
 *                          with MinAddr this forms a valid address range for
 *                          symbols and sections that we care about.  Anything
 *                          outside the range is ignored, except when doing
 *                          sanity checks..
 * @param   uModTag         Module tag. Pass 0 if tagging is of no interest.
 */
int DBGDiggerCommonParseElfMod(PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszModName, const char *pszFilename, uint32_t fFlags,
                               Elf_Ehdr const *pEhdr, Elf_Shdr const *paShdrs,
                               Elf_Sym const *paSyms, size_t cMaxSyms,
                               char const *pbStrings, size_t cbMaxStrings,
                               RTGCPTR MinAddr, RTGCPTR MaxAddr, uint64_t uModTag)
{
    AssertPtrReturn(pUVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pVMM, VERR_INVALID_POINTER);
    AssertPtrReturn(pszModName, VERR_INVALID_POINTER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~DBG_DIGGER_ELF_MASK), VERR_INVALID_PARAMETER);
    AssertReturn((fFlags & (DBG_DIGGER_ELF_FUNNY_SHDRS | DBG_DIGGER_ELF_ADJUST_SYM_VALUE))
                 != (DBG_DIGGER_ELF_FUNNY_SHDRS | DBG_DIGGER_ELF_ADJUST_SYM_VALUE), VERR_INVALID_PARAMETER);
    AssertPtrReturn(paShdrs, VERR_INVALID_POINTER);
    AssertPtrReturn(paSyms, VERR_INVALID_POINTER);
    AssertPtrReturn(pbStrings, VERR_INVALID_POINTER);

    /*
     * Validate the ELF header.
     */
    if (    pEhdr->e_ident[EI_MAG0] != ELFMAG0
        ||  pEhdr->e_ident[EI_MAG1] != ELFMAG1
        ||  pEhdr->e_ident[EI_MAG2] != ELFMAG2
        ||  pEhdr->e_ident[EI_MAG3] != ELFMAG3)
        return VERR_INVALID_EXE_SIGNATURE;
    if (pEhdr->e_ident[EI_CLASS] != MY_ELFCLASS)
        return VERR_LDRELF_MACHINE;

    if (pEhdr->e_ident[EI_DATA] != ELFDATA2LSB)
        return VERR_LDRELF_ODD_ENDIAN;
    if (pEhdr->e_ident[EI_VERSION] != EV_CURRENT)
        return VERR_LDRELF_VERSION;
    if (pEhdr->e_version != EV_CURRENT)
        return VERR_LDRELF_VERSION;
    if (pEhdr->e_ehsize != sizeof(*pEhdr))
        return VERR_BAD_EXE_FORMAT;

#if ELF_MODE == 32
    if (    pEhdr->e_machine != EM_386
        &&  pEhdr->e_machine != EM_486)
        return VERR_LDRELF_MACHINE;
#else
    if (pEhdr->e_machine != EM_X86_64)
        return VERR_LDRELF_MACHINE;
#endif

    if (    pEhdr->e_type != ET_DYN
        &&  pEhdr->e_type != ET_REL
        &&  pEhdr->e_type != ET_EXEC) //??
        return VERR_BAD_EXE_FORMAT;
    if (    pEhdr->e_phentsize != sizeof(Elf_Phdr)
        &&  pEhdr->e_phentsize) //??
        return VERR_BAD_EXE_FORMAT;
    if (pEhdr->e_shentsize != sizeof(Elf_Shdr))
        return VERR_BAD_EXE_FORMAT;
    if (pEhdr->e_shentsize != sizeof(Elf_Shdr))
        return VERR_BAD_EXE_FORMAT;
    if (!ASMMemIsZero(&pEhdr->e_ident[EI_PAD], EI_NIDENT - EI_PAD)) //??
        return VERR_BAD_EXE_FORMAT;

    /*
     * Validate the section headers, finding the string and symbol table
     * headers and the load address while at it.
     */
    uint64_t        uLoadAddr = UINT64_MAX;
    const Elf_Shdr *pSymShdr  = NULL;
    const Elf_Shdr *pStrShdr  = NULL;
    for (unsigned iSh = fFlags & DBG_DIGGER_ELF_FUNNY_SHDRS ? 1 : 0; iSh < pEhdr->e_shnum; iSh++)
    {
        /* Minimal validation. */
        if (paShdrs[iSh].sh_link >= pEhdr->e_shnum)
            return VERR_BAD_EXE_FORMAT;

        /* Is it the symbol table?*/
        if (paShdrs[iSh].sh_type == SHT_SYMTAB)
        {
            if (pSymShdr)
                return VERR_LDRELF_MULTIPLE_SYMTABS;
            pSymShdr = &paShdrs[iSh];
            if (pSymShdr->sh_entsize != sizeof(Elf32_Sym))
                return VERR_BAD_EXE_FORMAT;
            pStrShdr = &paShdrs[paShdrs[iSh].sh_link];
        }
        if (uLoadAddr > paShdrs[iSh].sh_addr)
            uLoadAddr = paShdrs[iSh].sh_addr;
    }

    /*
     * Validate the symbol table and determine the max section index
     * when DBG_DIGGER_ELF_FUNNY_SHDRS is flagged.
     */
    uint32_t     uMaxShIdx = fFlags & DBG_DIGGER_ELF_FUNNY_SHDRS ? 0 : pEhdr->e_shnum - 1;
    size_t const cbStrings = pStrShdr ? pStrShdr->sh_size : cbMaxStrings;
    size_t const cSyms = pSymShdr
                       ? RT_MIN(cMaxSyms, pSymShdr->sh_size / sizeof(Elf_Sym))
                       : cMaxSyms;
    for (size_t iSym = 1; iSym < cSyms; iSym++)
    {
        if (paSyms[iSym].st_name >= cbStrings)
            return VERR_LDRELF_INVALID_SYMBOL_NAME_OFFSET;
        if (fFlags & DBG_DIGGER_ELF_FUNNY_SHDRS)
        {
            if (    paSyms[iSym].st_shndx > uMaxShIdx
                &&  paSyms[iSym].st_shndx < SHN_LORESERVE)
                uMaxShIdx = paSyms[iSym].st_shndx;
        }
        else if (   paSyms[iSym].st_shndx >= pEhdr->e_shnum
                 && paSyms[iSym].st_shndx != SHN_UNDEF
                 && (   paSyms[iSym].st_shndx < SHN_LORESERVE
                     /*|| paSyms[iSym].st_shndx > SHN_HIRESERVE*/
                     || ELF_ST_BIND(paSyms[iSym].st_info) == STB_GLOBAL
                     || ELF_ST_BIND(paSyms[iSym].st_info) == STB_WEAK) )
            return VERR_BAD_EXE_FORMAT;
    }
    if (uMaxShIdx > 4096)
        return VERR_BAD_EXE_FORMAT;

    /*
     * Create new module.
     * The funny ELF section headers on solaris makes this very complicated.
     */
    uint32_t            cSegs  = uMaxShIdx + 1;
    PDBGDIGGERELFSEG    paSegs = (PDBGDIGGERELFSEG)alloca(sizeof(paSegs[0]) * cSegs);
    for (uint32_t i = 0; i < cSegs; i++)
    {
        paSegs[i].uLoadAddr = RTGCPTR_MAX;
        paSegs[i].uLastAddr = 0;
        paSegs[i].iSeg      = NIL_RTDBGSEGIDX;
    }

    RTDBGMOD hMod;
    int rc = RTDbgModCreate(&hMod, pszModName, 0 /*cbSeg*/, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTDbgModSetTag(hMod, uModTag); AssertRC(rc);

    if (fFlags & DBG_DIGGER_ELF_FUNNY_SHDRS)
    {
        /* Seek out the min and max symbol values for each section. */
        for (uint32_t iSym = 1; iSym < cSyms; iSym++)
        {
            /* Ignore undefined, absolute and weak symbols in this pass,
               but include local ones as well as nameless. */
            uint32_t iSh = paSyms[iSym].st_shndx;
            if (    iSh != SHN_UNDEF
                &&  iSh < cSegs
                &&  (   ELF_ST_BIND(paSyms[iSym].st_info) == STB_GLOBAL
                     || ELF_ST_BIND(paSyms[iSym].st_info) == STB_LOCAL))
            {
                /* Calc the address and check that it doesn't wrap with the size. */
                RTGCUINTPTR Address     = paSyms[iSym].st_value;
                RTGCUINTPTR AddressLast = Address + RT_MAX(paSyms[iSym].st_size, 1) - 1;
                if (AddressLast < Address)
                    continue;
                if (    Address     < MinAddr
                    ||  AddressLast > MaxAddr)
                    continue;

                /* update min/max. */
                if (Address     < paSegs[iSh].uLoadAddr)
                    paSegs[iSh].uLoadAddr = Address;
                if (AddressLast > paSegs[iSh].uLastAddr)
                    paSegs[iSh].uLastAddr = AddressLast;
            }
        }

        /* Add the segments and fill in the translation table. */
        RTGCPTR uRvaNext = 0;
        for (unsigned i = 0; i < cSegs; i++)
            if (paSegs[i].uLastAddr != 0)
            {
                char szSeg[32];
                RTStrPrintf(szSeg, sizeof(szSeg), "sec%02u", i);
                RTGCPTR cbSeg = paSegs[i].uLastAddr - paSegs[i].uLoadAddr + 1;
                rc = RTDbgModSegmentAdd(hMod, uRvaNext, cbSeg, szSeg, 0 /*fFlags*/, &paSegs[i].iSeg);
                if (RT_FAILURE(rc))
                    break;
                uRvaNext += RT_ALIGN_T(cbSeg, 32, RTGCPTR);
            }
    }
    else
    {
        /* Add the segments and fill in the translation table. */
        for (unsigned i = 0; i < cSegs; i++)
            if (paShdrs[i].sh_flags & SHF_ALLOC)
            {
                char szSeg[32];
                RTStrPrintf(szSeg, sizeof(szSeg), "sec%02u", i);
                rc = RTDbgModSegmentAdd(hMod, paShdrs[i].sh_addr - uLoadAddr, paShdrs[i].sh_size, szSeg, 0 /*fFlags*/, &paSegs[i].iSeg);
                if (RT_FAILURE(rc))
                    break;
                paSegs[i].uLoadAddr = paShdrs[i].sh_addr;
                paSegs[i].uLastAddr = paShdrs[i].sh_addr + paShdrs[i].sh_size - 1;
            }
    }
    if (RT_FAILURE(rc))
    {
        RTDbgModRelease(hMod);
        return rc;
    }


    /*
     * Add all relevant symbols in the module
     */
    for (uint32_t iSym = 1; iSym < cSyms; iSym++)
    {
        /* Undefined symbols are not exports, they are imports. */
        RTDBGSEGIDX iSeg = paSyms[iSym].st_shndx;
        if (    iSeg != SHN_UNDEF
            &&  (   ELF_ST_BIND(paSyms[iSym].st_info) == STB_GLOBAL
                 || ELF_ST_BIND(paSyms[iSym].st_info) == STB_LOCAL
                 || ELF_ST_BIND(paSyms[iSym].st_info) == STB_WEAK))
        {
            /* Get the symbol name. */
            if (paSyms[iSym].st_name >= cbMaxStrings)
                continue;
            const char *pszSymbol = pbStrings + paSyms[iSym].st_name;
            if (!*pszSymbol)
                continue;

            /* Calc the address (value) and size. */
            RTGCUINTPTR cbSym  = paSyms[iSym].st_size;
            RTGCUINTPTR offSeg = paSyms[iSym].st_value;
            if (iSeg == SHN_ABS)
                iSeg = RTDBGSEGIDX_ABS; /* absolute symbols are not subject to any relocation. */
            else
            {
                Assert(iSeg < cSegs);
                if (fFlags & (DBG_DIGGER_ELF_FUNNY_SHDRS | DBG_DIGGER_ELF_ADJUST_SYM_VALUE))
                    offSeg -= paSegs[iSeg].uLoadAddr;
                iSeg = paSegs[iSeg].iSeg;
                if (iSeg == NIL_RTDBGSEGIDX)
                    continue;
            }
            if (offSeg + cbSym < offSeg)
                continue;

            rc = RTDbgModSymbolAdd(hMod, pszSymbol, iSeg, offSeg, cbSym, 0 /*fFlags*/, NULL);
            if (RT_FAILURE(rc))
                LogRel(("%02x:%RGv %RGv %s!%s -> rc=%Rrc\n", paSyms[iSym].st_shndx, offSeg, cbSym, pszModName, pszSymbol, rc));
            Log(("%02x:%RGv %RGv %s!%s (rc=%Rrc)\n", paSyms[iSym].st_shndx, offSeg, cbSym, pszModName, pszSymbol, rc));
        }
        /*else: silently ignore */
    }

    /*
     * Link it into the address space.
     */
    RTDBGAS hAs = pVMM->pfnDBGFR3AsResolveAndRetain(pUVM, DBGF_AS_KERNEL);
    if (hAs != NIL_RTDBGAS)
        rc = dbgDiggerCommonLinkElfSegs(hAs, hMod, paSegs, cSegs);
    else
        rc = VERR_INTERNAL_ERROR;
    RTDbgModRelease(hMod);
    RTDbgAsRelease(hAs);
    return rc;
}


#undef Elf_Ehdr
#undef Elf_Shdr
#undef Elf_Phdr
#undef Elf_Sym
#undef MY_ELFCLASS
#undef ELF_ST_BIND
#undef DBGDiggerCommonParseElfMod

