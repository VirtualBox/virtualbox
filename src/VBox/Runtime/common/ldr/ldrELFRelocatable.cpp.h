/* $Id: ldrELFRelocatable.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Binary Image Loader, Template for ELF Relocatable Images.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#if ELF_MODE == 32
# define RTLDRELF_NAME(name)    rtldrELF32##name
# define RTLDRELF_SUFF(name)    name##32
# define RTLDRELF_MID(pre,suff) pre##32##suff
# define FMT_ELF_ADDR           "%08RX32"
# define FMT_ELF_ADDR7          "%07RX32"
# define FMT_ELF_HALF           "%04RX16"
# define FMT_ELF_OFF            "%08RX32"
# define FMT_ELF_SIZE           "%08RX32"
# define FMT_ELF_SWORD          "%RI32"
# define FMT_ELF_WORD           "%08RX32"
# define FMT_ELF_XWORD          "%08RX32"
# define FMT_ELF_SXWORD         "%RI32"
# define Elf_Xword              Elf32_Word
# define Elf_Sxword             Elf32_Sword

#elif ELF_MODE == 64
# define RTLDRELF_NAME(name)    rtldrELF64##name
# define RTLDRELF_SUFF(name)    name##64
# define RTLDRELF_MID(pre,suff) pre##64##suff
# define FMT_ELF_ADDR           "%016RX64"
# define FMT_ELF_ADDR7          "%08RX64"
# define FMT_ELF_HALF           "%04RX16"
# define FMT_ELF_SHALF          "%RI16"
# define FMT_ELF_OFF            "%016RX64"
# define FMT_ELF_SIZE           "%016RX64"
# define FMT_ELF_SWORD          "%RI32"
# define FMT_ELF_WORD           "%08RX32"
# define FMT_ELF_XWORD          "%016RX64"
# define FMT_ELF_SXWORD         "%RI64"
# define Elf_Xword              Elf64_Xword
# define Elf_Sxword             Elf64_Sxword
#endif

#define Elf_Ehdr            RTLDRELF_MID(Elf,_Ehdr)
#define Elf_Phdr            RTLDRELF_MID(Elf,_Phdr)
#define Elf_Shdr            RTLDRELF_MID(Elf,_Shdr)
#define Elf_Sym             RTLDRELF_MID(Elf,_Sym)
#define Elf_Rel             RTLDRELF_MID(Elf,_Rel)
#define Elf_Rela            RTLDRELF_MID(Elf,_Rela)
#define Elf_Nhdr            RTLDRELF_MID(Elf,_Nhdr)
#define Elf_Dyn             RTLDRELF_MID(Elf,_Dyn)
#define Elf_Addr            RTLDRELF_MID(Elf,_Addr)
#define Elf_Half            RTLDRELF_MID(Elf,_Half)
#define Elf_Off             RTLDRELF_MID(Elf,_Off)
#define Elf_Size            RTLDRELF_MID(Elf,_Size)
#define Elf_Sword           RTLDRELF_MID(Elf,_Sword)
#define Elf_Word            RTLDRELF_MID(Elf,_Word)

#define RTLDRMODELF         RTLDRELF_MID(RTLDRMODELF,RT_NOTHING)
#define PRTLDRMODELF        RTLDRELF_MID(PRTLDRMODELF,RT_NOTHING)

#define RTLDRMODELFSHX      RTLDRELF_MID(RTLDRMODELFSHX,RT_NOTHING)
#define PRTLDRMODELFSHX     RTLDRELF_MID(PRTLDRMODELFSHX,RT_NOTHING)

#define ELF_R_SYM(info)     RTLDRELF_MID(ELF,_R_SYM)(info)
#define ELF_R_TYPE(info)    RTLDRELF_MID(ELF,_R_TYPE)(info)
#define ELF_R_INFO(sym, type) RTLDRELF_MID(ELF,_R_INFO)(sym, type)

#define ELF_ST_BIND(info)   RTLDRELF_MID(ELF,_ST_BIND)(info)



/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Extra section info.
 */
typedef struct RTLDRMODELFSHX
{
    /** The corresponding program header. */
    uint16_t        idxPhdr;
    /** The corresponding dynamic section entry (address). */
    uint16_t        idxDt;
    /** The DT tag. */
    uint32_t        uDtTag;
} RTLDRMODELFSHX;
typedef RTLDRMODELFSHX *PRTLDRMODELFSHX;

/**
 * The ELF loader structure.
 */
typedef struct RTLDRMODELF
{
    /** Core module structure. */
    RTLDRMODINTERNAL        Core;
    /** Pointer to readonly mapping of the image bits.
     * This mapping is provided by the pReader. */
    const void             *pvBits;

    /** The ELF header. */
    Elf_Ehdr                Ehdr;
    /** Pointer to our copy of the section headers with sh_addr as RVAs.
     * The virtual addresses in this array is the 0 based assignments we've given the image.
     * Not valid if the image is DONE. */
    Elf_Shdr               *paShdrs;
    /** Unmodified section headers (allocated after paShdrs, so no need to free).
     * Not valid if the image is DONE. */
    Elf_Shdr const         *paOrgShdrs;
    /** Runs parallel to paShdrs and is part of the same allocation. */
    PRTLDRMODELFSHX         paShdrExtras;
    /** Base section number, either 1 or zero depending on whether we've
     *  re-used the NULL entry for .elf.headers in ET_EXEC/ET_DYN. */
    unsigned                iFirstSect;
    /** Set if the SHF_ALLOC section headers are in order of sh_addr. */
    bool                    fShdrInOrder;
    /** The size of the loaded image. */
    size_t                  cbImage;

    /** The image base address if it's an EXEC or DYN image. */
    Elf_Addr                LinkAddress;

    struct
    {
        /** The symbol section index. */
        unsigned            iSymSh;
        /** Number of symbols in the table. */
        unsigned            cSyms;
        /** Pointer to symbol table within RTLDRMODELF::pvBits. */
        const Elf_Sym      *paSyms;

        /** The string section index. */
        unsigned            iStrSh;
        /** Size of the string table. */
        unsigned            cbStr;
        /** Pointer to string table within RTLDRMODELF::pvBits. */
        const char         *pStr;
    } Rel /**< Regular symbols and strings. */
    , Dyn /**< Dynamic symbols and strings. */;

    /** Pointer to section header string table within RTLDRMODELF::pvBits. */
    const char             *pShStr;
    /** Size of the section header string table. */
    unsigned                cbShStr;

    /** The '.eh_frame' section index.  Zero if not searched for, ~0U if not found. */
    unsigned                iShEhFrame;
    /** The '.eh_frame_hdr' section index.  Zero if not searched for, ~0U if not found. */
    unsigned                iShEhFrameHdr;

    /** The '.dynamic' / SHT_DYNAMIC section index.  ~0U if not present. */
    unsigned                iShDynamic;
    /** Number of entries in paDynamic. */
    unsigned                cDynamic;
    /** The dynamic section (NULL for ET_REL). */
    Elf_Dyn                *paDynamic;
    /** Program headers (NULL for ET_REL). */
    Elf_Phdr               *paPhdrs;

    /** Info extracted from PT_DYNAMIC and the program headers. */
    struct
    {
        /** DT_RELA/DT_REL. */
        Elf_Addr            uPtrRelocs;
        /** DT_RELASZ/DT_RELSZ. */
        Elf_Xword           cbRelocs;
        /** Non-zero if we've seen DT_RELAENT/DT_RELENT. */
        unsigned            cbRelocEntry;
        /** DT_RELA or DT_REL. */
        unsigned            uRelocType;
        /** The index of the section header matching DT_RELA/DT_REL. */
        unsigned            idxShRelocs;

        /** DT_JMPREL. */
        Elf_Addr            uPtrJmpRelocs;
        /** DT_PLTRELSZ. */
        Elf_Xword           cbJmpRelocs;
        /** DT_RELA or DT_REL (if we've seen DT_PLTREL). */
        unsigned            uJmpRelocType;
        /** The index of the section header matching DT_JMPREL. */
        unsigned            idxShJmpRelocs;
    } DynInfo;
} RTLDRMODELF;
/** Pointer to an ELF module instance. */
typedef RTLDRMODELF *PRTLDRMODELF;


/**
 * Maps the image bits into memory and resolve pointers into it.
 *
 * @returns iprt status code.
 * @param   pModElf         The ELF loader module instance data.
 * @param   fNeedsBits      Set if we actually need the pvBits member.
 *                          If we don't, we can simply read the string and symbol sections, thus saving memory.
 */
static int RTLDRELF_NAME(MapBits)(PRTLDRMODELF pModElf, bool fNeedsBits)
{
    NOREF(fNeedsBits);
    if (pModElf->pvBits)
        return VINF_SUCCESS;
    int rc = pModElf->Core.pReader->pfnMap(pModElf->Core.pReader, &pModElf->pvBits);
    if (RT_SUCCESS(rc))
    {
        const uint8_t *pu8 = (const uint8_t *)pModElf->pvBits;
        if (pModElf->Rel.iSymSh != ~0U)
            pModElf->Rel.paSyms = (const Elf_Sym *)(pu8 + pModElf->paShdrs[pModElf->Rel.iSymSh].sh_offset);
        if (pModElf->Rel.iStrSh != ~0U)
            pModElf->Rel.pStr   =    (const char *)(pu8 + pModElf->paShdrs[pModElf->Rel.iStrSh].sh_offset);
        if (pModElf->Dyn.iSymSh != ~0U)
            pModElf->Dyn.paSyms = (const Elf_Sym *)(pu8 + pModElf->paShdrs[pModElf->Dyn.iSymSh].sh_offset);
        if (pModElf->Dyn.iStrSh != ~0U)
            pModElf->Dyn.pStr   =    (const char *)(pu8 + pModElf->paShdrs[pModElf->Dyn.iStrSh].sh_offset);
        pModElf->pShStr         =    (const char *)(pu8 + pModElf->paShdrs[pModElf->Ehdr.e_shstrndx].sh_offset);

        /*
         * Verify that the ends of the string tables have a zero terminator
         * (this avoids duplicating the appropriate checks later in the code accessing the string tables).
         *
         * sh_offset and sh_size were verfied in RTLDRELF_NAME(ValidateSectionHeader)() already so they
         * are safe to use.
         */
        AssertMsgStmt(   pModElf->Rel.iStrSh == ~0U
                      || pModElf->Rel.pStr[pModElf->paShdrs[pModElf->Rel.iStrSh].sh_size - 1] == '\0',
                      ("The string table is not zero terminated!\n"),
                      rc = VERR_LDRELF_UNTERMINATED_STRING_TAB);
        AssertMsgStmt(   pModElf->Dyn.iStrSh == ~0U
                      || pModElf->Dyn.pStr[pModElf->paShdrs[pModElf->Dyn.iStrSh].sh_size - 1] == '\0',
                      ("The string table is not zero terminated!\n"),
                      rc = VERR_LDRELF_UNTERMINATED_STRING_TAB);
        AssertMsgStmt(pModElf->pShStr[pModElf->paShdrs[pModElf->Ehdr.e_shstrndx].sh_size - 1] == '\0',
                      ("The section header string table is not zero terminated!\n"),
                      rc = VERR_LDRELF_UNTERMINATED_STRING_TAB);

        if (RT_FAILURE(rc))
        {
            /* Unmap. */
            int rc2 = pModElf->Core.pReader->pfnUnmap(pModElf->Core.pReader, pModElf->pvBits);
            AssertRC(rc2);
            pModElf->pvBits     = NULL;
            pModElf->Rel.paSyms = NULL;
            pModElf->Rel.pStr   = NULL;
            pModElf->Dyn.paSyms = NULL;
            pModElf->Dyn.pStr   = NULL;
            pModElf->pShStr     = NULL;
        }
    }
    return rc;
}


/*
 *
 * EXEC & DYN.
 * EXEC & DYN.
 * EXEC & DYN.
 * EXEC & DYN.
 * EXEC & DYN.
 *
 */

/**
 * Get the symbol and symbol value.
 *
 * @returns iprt status code.
 * @param   pModElf         The ELF loader module instance data.
 * @param   BaseAddr        The base address which the module is being fixedup to.
 * @param   pfnGetImport    The callback function to use to resolve imports (aka unresolved externals).
 * @param   pvUser          User argument to pass to the callback.
 * @param   iSym            The symbol to get.
 * @param   ppSym           Where to store the symbol pointer on success. (read only)
 * @param   pSymValue       Where to store the symbol value on success.
 */
static int RTLDRELF_NAME(SymbolExecDyn)(PRTLDRMODELF pModElf, Elf_Addr BaseAddr, PFNRTLDRIMPORT pfnGetImport, void *pvUser,
                                        Elf_Size iSym, const Elf_Sym **ppSym, Elf_Addr *pSymValue)
{
    /*
     * Validate and find the symbol.
     */
    AssertMsgReturn(iSym < pModElf->Dyn.cSyms, ("iSym=%d is an invalid symbol index!\n", iSym), VERR_LDRELF_INVALID_SYMBOL_INDEX);
    const Elf_Sym *pSym = &pModElf->Dyn.paSyms[iSym];
    *ppSym = pSym;

    AssertMsgReturn(pSym->st_name < pModElf->Dyn.cbStr,
                    ("iSym=%d st_name=%d str sh_size=%d\n", iSym, pSym->st_name, pModElf->Dyn.cbStr),
                    VERR_LDRELF_INVALID_SYMBOL_NAME_OFFSET);
    const char * const pszName = pModElf->Dyn.pStr + pSym->st_name;

    /*
     * Determine the symbol value.
     *
     * Symbols needs different treatment depending on which section their are in.
     * Undefined and absolute symbols goes into special non-existing sections.
     */
    switch (pSym->st_shndx)
    {
        /*
         * Undefined symbol, needs resolving.
         *
         * Since ELF has no generic concept of importing from specific module (the OS/2 ELF format
         * has but that's an OS extension and only applies to programs and dlls), we'll have to ask
         * the resolver callback to do a global search.
         */
        case SHN_UNDEF:
        {
            /* Try to resolve the symbol. */
            RTUINTPTR Value;
            int rc = pfnGetImport(&pModElf->Core, "", pszName, ~0U, &Value, pvUser);
            AssertMsgRCReturn(rc, ("Failed to resolve '%s' (iSym=" FMT_ELF_SIZE " rc=%Rrc\n", pszName, iSym, rc), rc);

            *pSymValue = (Elf_Addr)Value;
            AssertMsgReturn((RTUINTPTR)*pSymValue == Value,
                            ("Symbol value overflowed! '%s' (iSym=" FMT_ELF_SIZE "\n", pszName, iSym), VERR_SYMBOL_VALUE_TOO_BIG);

            Log2(("rtldrELF: #%-3d - UNDEF " FMT_ELF_ADDR " '%s'\n", iSym, *pSymValue, pszName));
            break;
        }

        /*
         * Absolute symbols needs no fixing since they are, well, absolute.
         */
        case SHN_ABS:
            *pSymValue = pSym->st_value;
            Log2(("rtldrELF: #%-3d - ABS   " FMT_ELF_ADDR " '%s'\n", iSym, *pSymValue, pszName));
            break;

        /*
         * All other symbols are addressed relative the image base in DYN and EXEC binaries.
         */
        default:
            AssertMsgReturn(pSym->st_shndx < pModElf->Ehdr.e_shnum,
                            ("iSym=%d st_shndx=%d e_shnum=%d pszName=%s\n", iSym, pSym->st_shndx, pModElf->Ehdr.e_shnum, pszName),
                            VERR_BAD_EXE_FORMAT);
            *pSymValue = pSym->st_value + BaseAddr;
            Log2(("rtldrELF: #%-3d - %5d " FMT_ELF_ADDR " '%s'\n", iSym, pSym->st_shndx, *pSymValue, pszName));
            break;
    }

    return VINF_SUCCESS;
}


#if   ELF_MODE == 32
/** Helper for RelocateSectionExecDyn. */
DECLINLINE(const Elf_Shdr *) RTLDRELF_NAME(RvaToSectionHeader)(PRTLDRMODELF pModElf, Elf_Addr uRva)
{
    const Elf_Shdr * const pShdrFirst = pModElf->paShdrs;
    const Elf_Shdr *pShdr = pShdrFirst + pModElf->Ehdr.e_shnum;
    while (--pShdr != pShdrFirst)
        if (uRva - pShdr->sh_addr /*rva*/ < pShdr->sh_size)
            return pShdr;
    AssertFailed();
    return pShdr;
}
#endif


/**
 * Applies the fixups for a section in an executable image.
 *
 * @returns iprt status code.
 * @param   pModElf         The ELF loader module instance data.
 * @param   BaseAddr        The base address which the module is being fixedup to.
 * @param   pfnGetImport    The callback function to use to resolve imports (aka unresolved externals).
 * @param   pvUser          User argument to pass to the callback.
 * @param   SecAddr         The section address. This is the address the relocations are relative to.
 * @param   cbSec           The section size. The relocations must be inside this.
 * @param   pu8SecBaseR     Where we read section bits from.
 * @param   pu8SecBaseW     Where we write section bits to.
 * @param   pvRelocs        Pointer to where we read the relocations from.
 * @param   cbRelocs        Size of the relocations.
 */
static int RTLDRELF_NAME(RelocateSectionExecDyn)(PRTLDRMODELF pModElf, Elf_Addr BaseAddr,
                                                 PFNRTLDRIMPORT pfnGetImport, void *pvUser,
                                                 const Elf_Addr SecAddr, Elf_Size cbSec,
                                                 const uint8_t *pu8SecBaseR, uint8_t *pu8SecBaseW,
                                                 const void *pvRelocs, Elf_Size cbRelocs)
{
#if ELF_MODE != 32
    NOREF(pu8SecBaseR);
#endif

    /*
     * Iterate the relocations.
     * The relocations are stored in an array of Elf32_Rel records and covers the entire relocation section.
     */
#if   ELF_MODE == 32
    const Elf_Shdr   *pShdr    = pModElf->paShdrs;
    const Elf_Addr    offDelta = BaseAddr - pModElf->LinkAddress;
#endif
    const Elf_Reloc  *paRels   = (const Elf_Reloc *)pvRelocs;
    const unsigned    iRelMax  = (unsigned)(cbRelocs / sizeof(paRels[0]));
    AssertMsgReturn(iRelMax == cbRelocs / sizeof(paRels[0]), (FMT_ELF_SIZE "\n", cbRelocs / sizeof(paRels[0])),
                    VERR_IMAGE_TOO_BIG);
    for (unsigned iRel = 0; iRel < iRelMax; iRel++)
    {
        /*
         * Apply fixups not taking a symbol (will 'continue' rather than 'break').
         */
        AssertMsgReturn(paRels[iRel].r_offset < cbSec, (FMT_ELF_ADDR " " FMT_ELF_SIZE "\n", paRels[iRel].r_offset, cbSec),
                        VERR_LDRELF_INVALID_RELOCATION_OFFSET);
#if   ELF_MODE == 32
        if (paRels[iRel].r_offset - pShdr->sh_addr /*rva*/ >= pShdr->sh_size)
            pShdr = RTLDRELF_NAME(RvaToSectionHeader)(pModElf, paRels[iRel].r_offset);
        static const Elf_Addr s_uZero = 0;
        const Elf_Addr *pAddrR = RT_LIKELY(pShdr->sh_type != SHT_NOBITS)                     /* Where to read the addend. */
                               ? (const Elf_Addr *)(pu8SecBaseR + paRels[iRel].r_offset - pShdr->sh_addr /*rva*/
                                                    + pShdr->sh_offset)
                               : &s_uZero;
#endif
        Elf_Addr       *pAddrW =       (Elf_Addr *)(pu8SecBaseW + paRels[iRel].r_offset);    /* Where to write the fixup. */
        switch (ELF_R_TYPE(paRels[iRel].r_info))
        {
            /*
             * Image relative (addend + base).
             */
#if   ELF_MODE == 32
            case R_386_RELATIVE:
            {
                const Elf_Addr Value = *pAddrR + BaseAddr;
                *(uint32_t *)pAddrW = Value;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_386_RELATIVE Value=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, Value));
                AssertCompile(sizeof(Value) == sizeof(uint32_t));
                continue;
            }
#elif ELF_MODE == 64
            case R_X86_64_RELATIVE:
            {
                const Elf_Addr Value = paRels[iRel].r_addend + BaseAddr;
                *(uint64_t *)pAddrW = (uint64_t)Value;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_X86_64_RELATIVE Value=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, Value));
                AssertCompile(sizeof(Value) == sizeof(uint64_t));
                continue;
            }
#endif

            /*
             * R_XXX_NONE.
             */
#if   ELF_MODE == 32
            case R_386_NONE:
#elif ELF_MODE == 64
            case R_X86_64_NONE:
#endif
                continue;
        }

        /*
         * Validate and find the symbol, resolve undefined ones.
         */
        const Elf_Sym  *pSym = NULL; /* shut up gcc */
        Elf_Addr        SymValue = 0; /* shut up gcc-4 */
        int rc = RTLDRELF_NAME(SymbolExecDyn)(pModElf, BaseAddr, pfnGetImport, pvUser, ELF_R_SYM(paRels[iRel].r_info), &pSym, &SymValue);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Apply the fixup.
         */
        switch (ELF_R_TYPE(paRels[iRel].r_info))
        {
#if   ELF_MODE == 32
            /*
             * GOT/PLT.
             */
            case R_386_GLOB_DAT:
            {
                *(uint32_t *)pAddrW = (uint32_t)SymValue;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_386_GLOB_DAT Value=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, SymValue));
                AssertCompile(sizeof(SymValue) == sizeof(uint32_t));
                break;
            }

            case R_386_JMP_SLOT:
            {
                *(uint32_t *)pAddrW = (uint32_t)SymValue;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_386_JMP_SLOT Value=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, SymValue));
                AssertCompile(sizeof(SymValue) == sizeof(uint32_t));
                break;
            }

            /*
             * Absolute addressing.
             */
            case R_386_32:
            {
                Elf_Addr Value;
                if (pSym->st_shndx < pModElf->Ehdr.e_shnum)
                    Value = *pAddrR + offDelta;         /* Simplified. */
                else if (pSym->st_shndx == SHN_ABS)
                    continue;                           /* Internal fixup, no need to apply it. */
                else if (pSym->st_shndx == SHN_UNDEF)
                    Value = SymValue + *pAddrR;
                else
                    AssertFailedReturn(VERR_LDR_GENERAL_FAILURE); /** @todo SHN_COMMON */
                *(uint32_t *)pAddrW = Value;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_386_32   Value=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, Value));
                break;
            }

            /*
             * PC relative addressing.
             */
            case R_386_PC32:
            {
                Elf_Addr Value;
                if (pSym->st_shndx < pModElf->Ehdr.e_shnum)
                    continue;                           /* Internal fixup, no need to apply it. */
                else if (pSym->st_shndx == SHN_ABS)
                    Value = *pAddrR + offDelta;         /* Simplified. */
                else if (pSym->st_shndx == SHN_UNDEF)
                {
                    const Elf_Addr SourceAddr = SecAddr + paRels[iRel].r_offset + BaseAddr; /* Where the source really is. */
                    Value = SymValue + *(uint32_t *)pAddrR - SourceAddr;
                    *(uint32_t *)pAddrW = Value;
                }
                else
                    AssertFailedReturn(VERR_LDR_GENERAL_FAILURE); /** @todo SHN_COMMON */
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_386_PC32 Value=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, Value));
                break;
            }

#elif ELF_MODE == 64
            /*
             * GOT/PLT.
             */
            case R_X86_64_GLOB_DAT:
            {
                *(uint64_t *)pAddrW = (uint64_t)SymValue;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_X86_64_GLOB_DAT Value=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, SymValue));
                AssertCompile(sizeof(SymValue) == sizeof(uint64_t));
                break;
            }

            case R_X86_64_JMP_SLOT:
            {
                *(uint64_t *)pAddrW = (uint64_t)SymValue;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_X86_64_JMP_SLOT Value=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, SymValue));
                AssertCompile(sizeof(SymValue) == sizeof(uint64_t));
                break;
            }

            /*
             * Absolute addressing.
             */
            case R_X86_64_64:
            {
                const Elf_Addr Value = SymValue + paRels[iRel].r_addend;
                *(uint64_t *)pAddrW = Value;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_X86_64_64   Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, Value, SymValue));
                break;
            }

            /*
             * Truncated 32-bit value (zero-extendedable to the 64-bit value).
             */
            case R_X86_64_32:
            {
                const Elf_Addr Value = SymValue + paRels[iRel].r_addend;
                *(uint32_t *)pAddrW = (uint32_t)Value;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_X86_64_32   Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, Value, SymValue));
                AssertMsgReturn((Elf_Addr)*(uint32_t *)pAddrW == SymValue, ("Value=" FMT_ELF_ADDR "\n", SymValue),
                                VERR_SYMBOL_VALUE_TOO_BIG);
                break;
            }

            /*
             * Truncated 32-bit value (sign-extendedable to the 64-bit value).
             */
            case R_X86_64_32S:
            {
                const Elf_Addr Value = SymValue + paRels[iRel].r_addend;
                *(int32_t *)pAddrW = (int32_t)Value;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_X86_64_32S  Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, paRels[iRel].r_offset, Value, SymValue));
                AssertMsgReturn((Elf_Addr)*(int32_t *)pAddrW == Value, ("Value=" FMT_ELF_ADDR "\n", Value), VERR_SYMBOL_VALUE_TOO_BIG); /** @todo check the sign-extending here. */
                break;
            }

            /*
             * PC relative addressing.
             */
            case R_X86_64_PC32:
            {
                const Elf_Addr SourceAddr = SecAddr  + paRels[iRel].r_offset + BaseAddr; /* Where the source really is. */
                const Elf_Addr Value      = SymValue + paRels[iRel].r_addend - SourceAddr;
                *(int32_t *)pAddrW = (int32_t)Value;
                Log4((FMT_ELF_ADDR "/" FMT_ELF_ADDR7 ": R_X86_64_PC32 Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SourceAddr, paRels[iRel].r_offset, Value, SymValue));
                AssertMsgReturn((Elf_Addr)*(int32_t *)pAddrW == Value, ("Value=" FMT_ELF_ADDR "\n", Value), VERR_SYMBOL_VALUE_TOO_BIG); /** @todo check the sign-extending here. */
                break;
            }

#endif
            default:
                AssertMsgFailed(("Unknown relocation type: %d (iRel=%d iRelMax=%d)\n",
                                 ELF_R_TYPE(paRels[iRel].r_info), iRel, iRelMax));
                return VERR_LDRELF_RELOCATION_NOT_SUPPORTED;
        }
    }

    return VINF_SUCCESS;
}



/*
 *
 * REL
 * REL
 * REL
 * REL
 * REL
 *
 */

/**
 * Get the symbol and symbol value.
 *
 * @returns iprt status code.
 * @param   pModElf         The ELF loader module instance data.
 * @param   BaseAddr        The base address which the module is being fixedup to.
 * @param   pfnGetImport    The callback function to use to resolve imports (aka unresolved externals).
 * @param   pvUser          User argument to pass to the callback.
 * @param   iSym            The symbol to get.
 * @param   ppSym           Where to store the symbol pointer on success. (read only)
 * @param   pSymValue       Where to store the symbol value on success.
 */
static int RTLDRELF_NAME(Symbol)(PRTLDRMODELF pModElf, Elf_Addr BaseAddr, PFNRTLDRIMPORT pfnGetImport, void *pvUser,
                                 Elf_Size iSym, const Elf_Sym **ppSym, Elf_Addr *pSymValue)
{
    /*
     * Validate and find the symbol.
     */
    AssertMsgReturn(iSym < pModElf->Rel.cSyms, ("iSym=%d is an invalid symbol index!\n", iSym), VERR_LDRELF_INVALID_SYMBOL_INDEX);
    const Elf_Sym *pSym = &pModElf->Rel.paSyms[iSym];
    *ppSym = pSym;

    AssertMsgReturn(pSym->st_name < pModElf->Rel.cbStr,
                    ("iSym=%d st_name=%d str sh_size=%d\n", iSym, pSym->st_name, pModElf->Rel.cbStr),
                    VERR_LDRELF_INVALID_SYMBOL_NAME_OFFSET);
    const char *pszName = ELF_STR(pModElf, pSym->st_name);

    /*
     * Determine the symbol value.
     *
     * Symbols needs different treatment depending on which section their are in.
     * Undefined and absolute symbols goes into special non-existing sections.
     */
    switch (pSym->st_shndx)
    {
        /*
         * Undefined symbol, needs resolving.
         *
         * Since ELF has no generic concept of importing from specific module (the OS/2 ELF format
         * has but that's an OS extension and only applies to programs and dlls), we'll have to ask
         * the resolver callback to do a global search.
         */
        case SHN_UNDEF:
        {
            /* Try to resolve the symbol. */
            RTUINTPTR Value;
            int rc = pfnGetImport(&pModElf->Core, "", pszName, ~0U, &Value, pvUser);
            AssertMsgRCReturn(rc, ("Failed to resolve '%s' (iSym=" FMT_ELF_SIZE " rc=%Rrc\n", pszName, iSym, rc), rc);
            *pSymValue = (Elf_Addr)Value;

            AssertMsgReturn((RTUINTPTR)*pSymValue == Value,
                            ("Symbol value overflowed! '%s' (iSym=" FMT_ELF_SIZE ")\n", pszName, iSym),
                            VERR_SYMBOL_VALUE_TOO_BIG);

            Log2(("rtldrELF: #%-3d - UNDEF " FMT_ELF_ADDR " '%s'\n", iSym, *pSymValue, pszName));
            break;
        }

        /*
         * Absolute symbols needs no fixing since they are, well, absolute.
         */
        case SHN_ABS:
            *pSymValue = pSym->st_value;
            Log2(("rtldrELF: #%-3d - ABS   " FMT_ELF_ADDR " '%s'\n", iSym, *pSymValue, pszName));
            break;

        /*
         * All other symbols are addressed relative to their section and need to be fixed up.
         */
        default:
            if (pSym->st_shndx >= pModElf->Ehdr.e_shnum)
            {
                /* what about common symbols? */
                AssertMsg(pSym->st_shndx < pModElf->Ehdr.e_shnum,
                          ("iSym=%d st_shndx=%d e_shnum=%d pszName=%s\n", iSym, pSym->st_shndx, pModElf->Ehdr.e_shnum, pszName));
                return VERR_BAD_EXE_FORMAT;
            }
            *pSymValue = pSym->st_value + pModElf->paShdrs[pSym->st_shndx].sh_addr + BaseAddr;
            Log2(("rtldrELF: #%-3d - %5d " FMT_ELF_ADDR " '%s'\n", iSym, pSym->st_shndx, *pSymValue, pszName));
            break;
    }

    return VINF_SUCCESS;
}


/**
 * Applies the fixups for a sections.
 *
 * @returns iprt status code.
 * @param   pModElf         The ELF loader module instance data.
 * @param   BaseAddr        The base address which the module is being fixedup to.
 * @param   pfnGetImport    The callback function to use to resolve imports (aka unresolved externals).
 * @param   pvUser          User argument to pass to the callback.
 * @param   SecAddr         The section address. This is the address the relocations are relative to.
 * @param   cbSec           The section size. The relocations must be inside this.
 * @param   pu8SecBaseR     Where we read section bits from.
 * @param   pu8SecBaseW     Where we write section bits to.
 * @param   pvRelocs        Pointer to where we read the relocations from.
 * @param   cbRelocs        Size of the relocations.
 */
static int RTLDRELF_NAME(RelocateSectionRel)(PRTLDRMODELF pModElf, Elf_Addr BaseAddr, PFNRTLDRIMPORT pfnGetImport, void *pvUser,
                                             const Elf_Addr SecAddr, Elf_Size cbSec, const uint8_t *pu8SecBaseR,
                                             uint8_t *pu8SecBaseW, const void *pvRelocs, Elf_Size cbRelocs)
{
#if ELF_MODE != 32
    NOREF(pu8SecBaseR);
#endif

    /*
     * Iterate the relocations.
     * The relocations are stored in an array of Elf32_Rel records and covers the entire relocation section.
     */
    const Elf_Reloc  *paRels = (const Elf_Reloc *)pvRelocs;
    const unsigned   iRelMax = (unsigned)(cbRelocs / sizeof(paRels[0]));
    AssertMsgReturn(iRelMax == cbRelocs / sizeof(paRels[0]), (FMT_ELF_SIZE "\n", cbRelocs / sizeof(paRels[0])), VERR_IMAGE_TOO_BIG);
    for (unsigned iRel = 0; iRel < iRelMax; iRel++)
    {
        /*
         * Skip R_XXX_NONE entries early to avoid confusion in the symbol
         * getter code.
         */
#if   ELF_MODE == 32
        if (ELF_R_TYPE(paRels[iRel].r_info) == R_386_NONE)
            continue;
#elif ELF_MODE == 64
        if (ELF_R_TYPE(paRels[iRel].r_info) == R_X86_64_NONE)
            continue;
#endif


        /*
         * Get the symbol.
         */
        const Elf_Sym  *pSym = NULL; /* shut up gcc */
        Elf_Addr        SymValue = 0; /* shut up gcc-4 */
        int rc = RTLDRELF_NAME(Symbol)(pModElf, BaseAddr, pfnGetImport, pvUser, ELF_R_SYM(paRels[iRel].r_info), &pSym, &SymValue);
        if (RT_FAILURE(rc))
            return rc;

        Log3(("rtldrELF: " FMT_ELF_ADDR " %02x %06x - " FMT_ELF_ADDR " %3d %02x %s\n",
              paRels[iRel].r_offset, ELF_R_TYPE(paRels[iRel].r_info), (unsigned)ELF_R_SYM(paRels[iRel].r_info),
              SymValue, (unsigned)pSym->st_shndx, pSym->st_info, ELF_STR(pModElf, pSym->st_name)));

        /*
         * Apply the fixup.
         */
        AssertMsgReturn(paRels[iRel].r_offset < cbSec, (FMT_ELF_ADDR " " FMT_ELF_SIZE "\n", paRels[iRel].r_offset, cbSec), VERR_LDRELF_INVALID_RELOCATION_OFFSET);
#if   ELF_MODE == 32
        const Elf_Addr *pAddrR = (const Elf_Addr *)(pu8SecBaseR + paRels[iRel].r_offset);    /* Where to read the addend. */
#endif
        Elf_Addr       *pAddrW =       (Elf_Addr *)(pu8SecBaseW + paRels[iRel].r_offset);    /* Where to write the fixup. */
        switch (ELF_R_TYPE(paRels[iRel].r_info))
        {
#if   ELF_MODE == 32
            /*
             * Absolute addressing.
             */
            case R_386_32:
            {
                const Elf_Addr Value = SymValue + *pAddrR;
                *(uint32_t *)pAddrW = Value;
                Log4((FMT_ELF_ADDR": R_386_32   Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, Value, SymValue));
                break;
            }

            /*
             * PC relative addressing.
             */
            case R_386_PC32:
            {
                const Elf_Addr SourceAddr = SecAddr + paRels[iRel].r_offset + BaseAddr; /* Where the source really is. */
                const Elf_Addr Value = SymValue + *(uint32_t *)pAddrR - SourceAddr;
                *(uint32_t *)pAddrW = Value;
                Log4((FMT_ELF_ADDR": R_386_PC32 Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SourceAddr, Value, SymValue));
                break;
            }

            /* ignore */
            case R_386_NONE:
                break;

#elif ELF_MODE == 64

            /*
             * Absolute addressing
             */
            case R_X86_64_64:
            {
                const Elf_Addr Value = SymValue + paRels[iRel].r_addend;
                *(uint64_t *)pAddrW = Value;
                Log4((FMT_ELF_ADDR": R_X86_64_64   Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, Value, SymValue));
                break;
            }

            /*
             * Truncated 32-bit value (zero-extendedable to the 64-bit value).
             */
            case R_X86_64_32:
            {
                const Elf_Addr Value = SymValue + paRels[iRel].r_addend;
                *(uint32_t *)pAddrW = (uint32_t)Value;
                Log4((FMT_ELF_ADDR": R_X86_64_32   Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, Value, SymValue));
                AssertMsgReturn((Elf_Addr)*(uint32_t *)pAddrW == Value, ("Value=" FMT_ELF_ADDR "\n", Value), VERR_SYMBOL_VALUE_TOO_BIG);
                break;
            }

            /*
             * Truncated 32-bit value (sign-extendedable to the 64-bit value).
             */
            case R_X86_64_32S:
            {
                const Elf_Addr Value = SymValue + paRels[iRel].r_addend;
                *(int32_t *)pAddrW = (int32_t)Value;
                Log4((FMT_ELF_ADDR": R_X86_64_32S  Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SecAddr + paRels[iRel].r_offset + BaseAddr, Value, SymValue));
                AssertMsgReturn((Elf_Addr)*(int32_t *)pAddrW == Value, ("Value=" FMT_ELF_ADDR "\n", Value), VERR_SYMBOL_VALUE_TOO_BIG); /** @todo check the sign-extending here. */
                break;
            }

            /*
             * PC relative addressing.
             */
            case R_X86_64_PC32:
            case R_X86_64_PLT32: /* binutils commit 451875b4f976a527395e9303224c7881b65e12ed feature/regression. */
            {
                const Elf_Addr SourceAddr = SecAddr + paRels[iRel].r_offset + BaseAddr; /* Where the source really is. */
                const Elf_Addr Value = SymValue + paRels[iRel].r_addend - SourceAddr;
                *(int32_t *)pAddrW = (int32_t)Value;
                Log4((FMT_ELF_ADDR": R_X86_64_PC32 Value=" FMT_ELF_ADDR " SymValue=" FMT_ELF_ADDR "\n",
                      SourceAddr, Value, SymValue));
                AssertMsgReturn((Elf_Addr)*(int32_t *)pAddrW == Value, ("Value=" FMT_ELF_ADDR "\n", Value), VERR_SYMBOL_VALUE_TOO_BIG); /** @todo check the sign-extending here. */
                break;
            }

            /* ignore */
            case R_X86_64_NONE:
                break;
#endif

            default:
                AssertMsgFailed(("Unknown relocation type: %d (iRel=%d iRelMax=%d)\n",
                                 ELF_R_TYPE(paRels[iRel].r_info), iRel, iRelMax));
                return VERR_LDRELF_RELOCATION_NOT_SUPPORTED;
        }
    }

    return VINF_SUCCESS;
}



/** @copydoc RTLDROPS::pfnClose */
static DECLCALLBACK(int) RTLDRELF_NAME(Close)(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;

    if (pModElf->paShdrs)
    {
        RTMemFree(pModElf->paShdrs);
        pModElf->paShdrs = NULL;
    }

    if (pModElf->paPhdrs)
    {
        RTMemFree(pModElf->paPhdrs);
        pModElf->paPhdrs = NULL;
    }

    if (pModElf->paDynamic)
    {
        RTMemFree(pModElf->paDynamic);
        pModElf->paDynamic = NULL;
    }

    if (pModElf->pvBits)
    {
        pModElf->Core.pReader->pfnUnmap(pModElf->Core.pReader, pModElf->pvBits);
        pModElf->pvBits = NULL;
    }

    return VINF_SUCCESS;
}


/** @copydoc RTLDROPS::Done */
static DECLCALLBACK(int) RTLDRELF_NAME(Done)(PRTLDRMODINTERNAL pMod)
{
    NOREF(pMod); /*PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;*/
    /** @todo  Have to think more about this .... */
    return -1;
}


/** @copydoc RTLDROPS::pfnEnumSymbols */
static DECLCALLBACK(int) RTLDRELF_NAME(EnumSymbols)(PRTLDRMODINTERNAL pMod, unsigned fFlags, const void *pvBits,
                                                    RTUINTPTR BaseAddress, PFNRTLDRENUMSYMS pfnCallback, void *pvUser)
{
    PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;
    NOREF(pvBits);

    /*
     * Validate the input.
     */
    Elf_Addr BaseAddr = (Elf_Addr)BaseAddress;
    AssertMsgReturn((RTUINTPTR)BaseAddr == BaseAddress, ("%RTptr", BaseAddress), VERR_IMAGE_BASE_TOO_HIGH);

    /*
     * Make sure we've got the string and symbol tables. (We don't need the pvBits.)
     */
    int rc = RTLDRELF_NAME(MapBits)(pModElf, false);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Enumerate the symbol table.
     */
    const Elf_Sym  *paSyms  = pModElf->Rel.paSyms;
    unsigned        cSyms   = pModElf->Rel.cSyms;
    const char     *pszzStr = pModElf->Rel.pStr;
    unsigned        cbStr   = pModElf->Rel.cbStr;
    if (   (   !(fFlags & RTLDR_ENUM_SYMBOL_FLAGS_ALL)
            && pModElf->Dyn.cSyms > 0)
        || cSyms == 0)
    {
        paSyms  = pModElf->Dyn.paSyms;
        cSyms   = pModElf->Dyn.cSyms;
        pszzStr = pModElf->Dyn.pStr;
        cbStr   = pModElf->Dyn.cbStr;
    }

    for (unsigned iSym = 1; iSym < cSyms; iSym++)
    {
        /*
         * Skip imports (undefined).
         */
        if (paSyms[iSym].st_shndx != SHN_UNDEF)
        {
            /*
             * Calc value and get name.
             */
            Elf_Addr Value;
            if (paSyms[iSym].st_shndx == SHN_ABS)
                /* absolute symbols are not subject to any relocation. */
                Value = paSyms[iSym].st_value;
            else if (paSyms[iSym].st_shndx < pModElf->Ehdr.e_shnum)
            {
                if (pModElf->Ehdr.e_type == ET_REL)
                    /* relative to the section. */
                    Value = BaseAddr + paSyms[iSym].st_value + pModElf->paShdrs[paSyms[iSym].st_shndx].sh_addr;
                else /* Fixed up for link address. */
                    Value = BaseAddr + paSyms[iSym].st_value - pModElf->LinkAddress;
            }
            else
            {
                AssertMsgFailed(("Arg! paSyms[%u].st_shndx=" FMT_ELF_HALF "\n", iSym, paSyms[iSym].st_shndx));
                return VERR_BAD_EXE_FORMAT;
            }

            AssertMsgReturn(paSyms[iSym].st_name < cbStr,
                            ("String outside string table! iSym=%d paSyms[iSym].st_name=%#x\n", iSym, paSyms[iSym].st_name),
                            VERR_LDRELF_INVALID_SYMBOL_NAME_OFFSET);
            const char * const pszName = pszzStr + paSyms[iSym].st_name;

            /* String termination was already checked when the string table was mapped. */
            if (    *pszName != '\0'
                &&  (   (fFlags & RTLDR_ENUM_SYMBOL_FLAGS_ALL)
                     || ELF_ST_BIND(paSyms[iSym].st_info) == STB_GLOBAL) )
            {
                /*
                 * Call back.
                 */
                AssertMsgReturn(Value == (RTUINTPTR)Value, (FMT_ELF_ADDR "\n", Value), VERR_SYMBOL_VALUE_TOO_BIG);
                rc = pfnCallback(pMod, pszName, iSym, (RTUINTPTR)Value, pvUser);
                if (rc)
                    return rc;
            }
        }
    }

    return VINF_SUCCESS;
}


/** @copydoc RTLDROPS::GetImageSize */
static DECLCALLBACK(size_t) RTLDRELF_NAME(GetImageSize)(PRTLDRMODINTERNAL pMod)
{
    PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;

    return pModElf->cbImage;
}


/** @copydoc RTLDROPS::GetBits */
static DECLCALLBACK(int) RTLDRELF_NAME(GetBits)(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR BaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PRTLDRMODELF    pModElf = (PRTLDRMODELF)pMod;

    /*
     * This operation is currently only available on relocatable images.
     */
    switch (pModElf->Ehdr.e_type)
    {
        case ET_REL:
        case ET_DYN:
            break;
        case ET_EXEC:
            Log(("RTLdrELF: %s: Executable images are not supported yet!\n", pModElf->Core.pReader->pfnLogName(pModElf->Core.pReader)));
            return VERR_LDRELF_EXEC;
        default: AssertFailedReturn(VERR_BAD_EXE_FORMAT);
    }

    /*
     * Load the bits into pvBits.
     */
    const Elf_Shdr *paShdrs = pModElf->paShdrs;
    for (unsigned iShdr = 0; iShdr < pModElf->Ehdr.e_shnum; iShdr++)
    {
        if (paShdrs[iShdr].sh_flags & SHF_ALLOC)
        {
            AssertMsgReturn((size_t)paShdrs[iShdr].sh_size == (size_t)paShdrs[iShdr].sh_size, (FMT_ELF_SIZE "\n", paShdrs[iShdr].sh_size), VERR_IMAGE_TOO_BIG);
            switch (paShdrs[iShdr].sh_type)
            {
                case SHT_NOBITS:
                    memset((uint8_t *)pvBits + paShdrs[iShdr].sh_addr, 0, (size_t)paShdrs[iShdr].sh_size);
                    break;

                case SHT_PROGBITS:
                default:
                {
                    int rc = pModElf->Core.pReader->pfnRead(pModElf->Core.pReader, (uint8_t *)pvBits + paShdrs[iShdr].sh_addr,
                                                            (size_t)paShdrs[iShdr].sh_size, paShdrs[iShdr].sh_offset);
                    if (RT_FAILURE(rc))
                    {
                        Log(("RTLdrELF: %s: Read error when reading " FMT_ELF_SIZE " bytes at " FMT_ELF_OFF ", iShdr=%d\n",
                             pModElf->Core.pReader->pfnLogName(pModElf->Core.pReader),
                             paShdrs[iShdr].sh_size, paShdrs[iShdr].sh_offset, iShdr));
                        return rc;
                    }
                }
            }
        }
    }

    /*
     * Relocate the image.
     */
    return pModElf->Core.pOps->pfnRelocate(pMod, pvBits, BaseAddress, ~(RTUINTPTR)0, pfnGetImport, pvUser);
}


/** @copydoc RTLDROPS::Relocate */
static DECLCALLBACK(int) RTLDRELF_NAME(Relocate)(PRTLDRMODINTERNAL pMod, void *pvBits, RTUINTPTR NewBaseAddress,
                                                 RTUINTPTR OldBaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser)
{
    PRTLDRMODELF    pModElf = (PRTLDRMODELF)pMod;
#ifdef LOG_ENABLED
    const char     *pszLogName = pModElf->Core.pReader->pfnLogName(pModElf->Core.pReader);
#endif
    NOREF(OldBaseAddress);

    /*
     * This operation is currently only available on relocatable images.
     */
    switch (pModElf->Ehdr.e_type)
    {
        case ET_REL:
        case ET_DYN:
            break;
        case ET_EXEC:
            Log(("RTLdrELF: %s: Executable images are not supported yet!\n", pszLogName));
            return VERR_LDRELF_EXEC;
        default: AssertFailedReturn(VERR_BAD_EXE_FORMAT);
    }

    /*
     * Validate the input.
     */
    Elf_Addr BaseAddr = (Elf_Addr)NewBaseAddress;
    AssertMsgReturn((RTUINTPTR)BaseAddr == NewBaseAddress, ("%RTptr", NewBaseAddress), VERR_IMAGE_BASE_TOO_HIGH);

    /*
     * Map the image bits if not already done and setup pointer into it.
     */
    int rc = RTLDRELF_NAME(MapBits)(pModElf, true);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Iterate the sections looking for interesting SHT_REL[A] sections.
     *
     * In ET_REL files the SHT_REL[A] sections have the section index of
     * the section they contain fixups for in the sh_info member.
     */
    const Elf_Shdr *paShdrs = pModElf->paShdrs;
    Log2(("rtLdrElf: %s: Fixing up image\n", pszLogName));
    for (unsigned iShdr = 0; iShdr < pModElf->Ehdr.e_shnum; iShdr++)
    {
        const Elf_Shdr *pShdrRel = &paShdrs[iShdr];

        /*
         * Skip sections without interest to us.
         */
#if ELF_MODE == 32
        if (pShdrRel->sh_type != SHT_REL)
#else
        if (pShdrRel->sh_type != SHT_RELA)
#endif
            continue;
        if (pModElf->Ehdr.e_type == ET_REL)
        {
            if (pShdrRel->sh_info >= pModElf->Ehdr.e_shnum)
                continue;
            const Elf_Shdr *pShdr = &paShdrs[pShdrRel->sh_info]; /* the section to fixup. */
            if (!(pShdr->sh_flags & SHF_ALLOC))
                continue;

            /*
             * Relocate the section.
             */
            Log2(("rtldrELF: %s: Relocation records for #%d [%s] (sh_info=%d sh_link=%d) found in #%d [%s] (sh_info=%d sh_link=%d)\n",
                  pszLogName, (int)pShdrRel->sh_info, ELF_SH_STR(pModElf, pShdr->sh_name), (int)pShdr->sh_info, (int)pShdr->sh_link,
                  iShdr, ELF_SH_STR(pModElf, pShdrRel->sh_name), (int)pShdrRel->sh_info, (int)pShdrRel->sh_link));

            rc = RTLDRELF_NAME(RelocateSectionRel)(pModElf, BaseAddr, pfnGetImport, pvUser,
                                                   pShdr->sh_addr,
                                                   pShdr->sh_size,
                                                   (const uint8_t *)pModElf->pvBits + pShdr->sh_offset,
                                                   (uint8_t *)pvBits + pShdr->sh_addr,
                                                   (const uint8_t *)pModElf->pvBits + pShdrRel->sh_offset,
                                                   pShdrRel->sh_size);
        }
        else
            rc = RTLDRELF_NAME(RelocateSectionExecDyn)(pModElf, BaseAddr, pfnGetImport, pvUser,
                                                       0, (Elf_Size)pModElf->cbImage,
                                                       (const uint8_t *)pModElf->pvBits /** @todo file offset ?? */,
                                                       (uint8_t *)pvBits,
                                                       (const uint8_t *)pModElf->pvBits + pShdrRel->sh_offset,
                                                       pShdrRel->sh_size);

        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Worker for pfnGetSymbolEx.
 */
static int RTLDRELF_NAME(ReturnSymbol)(PRTLDRMODELF pThis, const Elf_Sym *pSym, Elf_Addr uBaseAddr, PRTUINTPTR pValue)
{
    Elf_Addr Value;
    if (pSym->st_shndx == SHN_ABS)
        /* absolute symbols are not subject to any relocation. */
        Value = pSym->st_value;
    else if (pSym->st_shndx < pThis->Ehdr.e_shnum)
    {
        if (pThis->Ehdr.e_type == ET_REL)
            /* relative to the section. */
            Value = uBaseAddr + pSym->st_value + pThis->paShdrs[pSym->st_shndx].sh_addr;
        else /* Fixed up for link address. */
            Value = uBaseAddr + pSym->st_value - pThis->LinkAddress;
    }
    else
    {
        AssertMsgFailed(("Arg! pSym->st_shndx=%d\n", pSym->st_shndx));
        return VERR_BAD_EXE_FORMAT;
    }
    AssertMsgReturn(Value == (RTUINTPTR)Value, (FMT_ELF_ADDR "\n", Value), VERR_SYMBOL_VALUE_TOO_BIG);
    *pValue = (RTUINTPTR)Value;
    return VINF_SUCCESS;
}


/** @copydoc RTLDROPS::pfnGetSymbolEx */
static DECLCALLBACK(int) RTLDRELF_NAME(GetSymbolEx)(PRTLDRMODINTERNAL pMod, const void *pvBits, RTUINTPTR BaseAddress,
                                                    uint32_t iOrdinal, const char *pszSymbol, RTUINTPTR *pValue)
{
    PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;
    NOREF(pvBits);

    /*
     * Validate the input.
     */
    Elf_Addr uBaseAddr = (Elf_Addr)BaseAddress;
    AssertMsgReturn((RTUINTPTR)uBaseAddr == BaseAddress, ("%RTptr", BaseAddress), VERR_IMAGE_BASE_TOO_HIGH);

    /*
     * Map the image bits if not already done and setup pointer into it.
     */
    int rc = RTLDRELF_NAME(MapBits)(pModElf, true);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Calc all kinds of pointers before we start iterating the symbol table.
     */
    const Elf_Sym *paSyms  = pModElf->Rel.paSyms;
    unsigned       cSyms   = pModElf->Rel.cSyms;
    const char    *pszzStr = pModElf->Rel.pStr;
    unsigned       cbStr   = pModElf->Rel.cbStr;
    if (pModElf->Dyn.cSyms > 0)
    {
        paSyms  = pModElf->Dyn.paSyms;
        cSyms   = pModElf->Dyn.cSyms;
        pszzStr = pModElf->Dyn.pStr;
        cbStr   = pModElf->Dyn.cbStr;
    }

    if (iOrdinal == UINT32_MAX)
    {
        for (unsigned iSym = 1; iSym < cSyms; iSym++)
        {
            /* Undefined symbols are not exports, they are imports. */
            if (    paSyms[iSym].st_shndx != SHN_UNDEF
                &&  (   ELF_ST_BIND(paSyms[iSym].st_info) == STB_GLOBAL
                     || ELF_ST_BIND(paSyms[iSym].st_info) == STB_WEAK))
            {
                /* Validate the name string and try match with it. */
                AssertMsgReturn(paSyms[iSym].st_name < cbStr,
                                ("String outside string table! iSym=%d paSyms[iSym].st_name=%#x\n", iSym, paSyms[iSym].st_name),
                                VERR_LDRELF_INVALID_SYMBOL_NAME_OFFSET);
                if (!strcmp(pszSymbol, pszzStr + paSyms[iSym].st_name))
                {
                    /* matched! */
                    return RTLDRELF_NAME(ReturnSymbol)(pModElf, &paSyms[iSym], uBaseAddr, pValue);
                }
            }
        }
    }
    else if (iOrdinal < cSyms)
    {
        if (    paSyms[iOrdinal].st_shndx != SHN_UNDEF
            &&  (   ELF_ST_BIND(paSyms[iOrdinal].st_info) == STB_GLOBAL
                 || ELF_ST_BIND(paSyms[iOrdinal].st_info) == STB_WEAK))
            return RTLDRELF_NAME(ReturnSymbol)(pModElf, &paSyms[iOrdinal], uBaseAddr, pValue);
    }

    return VERR_SYMBOL_NOT_FOUND;
}


/** @copydoc RTLDROPS::pfnEnumDbgInfo */
static DECLCALLBACK(int) RTLDRELF_NAME(EnumDbgInfo)(PRTLDRMODINTERNAL pMod, const void *pvBits,
                                                    PFNRTLDRENUMDBG pfnCallback, void *pvUser)
{
    PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;
    RT_NOREF_PV(pvBits);

    /*
     * Map the image bits if not already done and setup pointer into it.
     */
    int rc = RTLDRELF_NAME(MapBits)(pModElf, true);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Do the enumeration.
     */
    const Elf_Shdr *paShdrs = pModElf->paOrgShdrs;
    for (unsigned iShdr = 0; iShdr < pModElf->Ehdr.e_shnum; iShdr++)
    {
        /* Debug sections are expected to be PROGBITS and not allocated. */
        if (paShdrs[iShdr].sh_type != SHT_PROGBITS)
            continue;
        if (paShdrs[iShdr].sh_flags & SHF_ALLOC)
            continue;

        RTLDRDBGINFO DbgInfo;
        const char *pszSectName = ELF_SH_STR(pModElf, paShdrs[iShdr].sh_name);
        if (   !strncmp(pszSectName, RT_STR_TUPLE(".debug_"))
            || !strcmp(pszSectName, ".WATCOM_references") )
        {
            RT_ZERO(DbgInfo.u);
            DbgInfo.enmType         = RTLDRDBGINFOTYPE_DWARF;
            DbgInfo.pszExtFile      = NULL;
            DbgInfo.offFile         = paShdrs[iShdr].sh_offset;
            DbgInfo.cb              = paShdrs[iShdr].sh_size;
            DbgInfo.u.Dwarf.pszSection = pszSectName;
        }
        else if (!strcmp(pszSectName, ".gnu_debuglink"))
        {
            if ((paShdrs[iShdr].sh_size & 3) || paShdrs[iShdr].sh_size < 8)
                return VERR_BAD_EXE_FORMAT;

            RT_ZERO(DbgInfo.u);
            DbgInfo.enmType         = RTLDRDBGINFOTYPE_DWARF_DWO;
            DbgInfo.pszExtFile      = (const char *)((uintptr_t)pModElf->pvBits + (uintptr_t)paShdrs[iShdr].sh_offset);
            if (!RTStrEnd(DbgInfo.pszExtFile, paShdrs[iShdr].sh_size))
                return VERR_BAD_EXE_FORMAT;
            DbgInfo.u.Dwo.uCrc32    = *(uint32_t *)((uintptr_t)DbgInfo.pszExtFile + (uintptr_t)paShdrs[iShdr].sh_size
                                                    - sizeof(uint32_t));
            DbgInfo.offFile         = -1;
            DbgInfo.cb              = 0;
        }
        else
            continue;

        DbgInfo.LinkAddress         = NIL_RTLDRADDR;
        DbgInfo.iDbgInfo            = iShdr - 1;

        rc = pfnCallback(pMod, &DbgInfo, pvUser);
        if (rc != VINF_SUCCESS)
            return rc;

    }

    return VINF_SUCCESS;
}


/**
 * Locate the next allocated section by RVA (sh_addr).
 *
 * This is a helper for EnumSegments and SegOffsetToRva.
 *
 * @returns Pointer to the section header if found, NULL if none.
 * @param   pModElf     The module instance.
 * @param   iShdrCur    The current section header.
 */
static const Elf_Shdr *RTLDRELF_NAME(GetNextAllocatedSection)(PRTLDRMODELF pModElf, unsigned iShdrCur)
{
    unsigned const          cShdrs  = pModElf->Ehdr.e_shnum;
    const Elf_Shdr * const  paShdrs = pModElf->paShdrs;
    if (pModElf->fShdrInOrder)
    {
        for (unsigned iShdr = iShdrCur + 1; iShdr < cShdrs; iShdr++)
            if (paShdrs[iShdr].sh_flags & SHF_ALLOC)
                return &paShdrs[iShdr];
    }
    else
    {
        Elf_Addr const uEndCur = paShdrs[iShdrCur].sh_addr + paShdrs[iShdrCur].sh_size;
        Elf_Addr       offBest = ~(Elf_Addr)0;
        unsigned       iBest   = cShdrs;
        for (unsigned iShdr = pModElf->iFirstSect; iShdr < cShdrs; iShdr++)
            if ((paShdrs[iShdr].sh_flags & SHF_ALLOC) && iShdr != iShdrCur)
            {
                Elf_Addr const offDelta = paShdrs[iShdr].sh_addr - uEndCur;
                if (   offDelta < offBest
                    && paShdrs[iShdr].sh_addr >= uEndCur)
                {
                    offBest = offDelta;
                    iBest   = iShdr;
                }
            }
        if (iBest < cShdrs)
            return &paShdrs[iBest];
    }
    return NULL;
}


/** @copydoc RTLDROPS::pfnEnumSegments. */
static DECLCALLBACK(int) RTLDRELF_NAME(EnumSegments)(PRTLDRMODINTERNAL pMod, PFNRTLDRENUMSEGS pfnCallback, void *pvUser)
{
    PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;

    /*
     * Map the image bits if not already done and setup pointer into it.
     */
    int rc = RTLDRELF_NAME(MapBits)(pModElf, true);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Do the enumeration.
     */
    char            szName[32];
    Elf_Addr        uPrevMappedRva = 0;
    const Elf_Shdr *paShdrs    = pModElf->paShdrs;
    const Elf_Shdr *paOrgShdrs = pModElf->paOrgShdrs;
    for (unsigned iShdr = pModElf->iFirstSect; iShdr < pModElf->Ehdr.e_shnum; iShdr++)
    {
        RTLDRSEG Seg;
        if (iShdr != 0)
        {
            Seg.pszName     = ELF_SH_STR(pModElf, paShdrs[iShdr].sh_name);
            Seg.cchName     = (uint32_t)strlen(Seg.pszName);
            if (Seg.cchName == 0)
            {
                Seg.pszName = szName;
                Seg.cchName = (uint32_t)RTStrPrintf(szName, sizeof(szName), "UnamedSect%02u", iShdr);
            }
        }
        else
        {
            Seg.pszName = ".elf.headers";
            Seg.cchName = 12;
        }
        Seg.SelFlat     = 0;
        Seg.Sel16bit    = 0;
        Seg.fFlags      = 0;
        Seg.fProt       = RTMEM_PROT_READ;
        if (paShdrs[iShdr].sh_flags & SHF_WRITE)
            Seg.fProt  |= RTMEM_PROT_WRITE;
        if (paShdrs[iShdr].sh_flags & SHF_EXECINSTR)
            Seg.fProt  |= RTMEM_PROT_EXEC;
        Seg.cb          = paShdrs[iShdr].sh_size;
        Seg.Alignment   = paShdrs[iShdr].sh_addralign;
        if (paShdrs[iShdr].sh_flags & SHF_ALLOC)
        {
            Seg.LinkAddress = paOrgShdrs[iShdr].sh_addr;
            Seg.RVA         = paShdrs[iShdr].sh_addr;
            const Elf_Shdr *pShdr2 = RTLDRELF_NAME(GetNextAllocatedSection)(pModElf, iShdr);
            if (pShdr2)
                Seg.cbMapped = pShdr2->sh_addr - paShdrs[iShdr].sh_addr;
            else
                Seg.cbMapped = pModElf->cbImage - paShdrs[iShdr].sh_addr;
            uPrevMappedRva = Seg.RVA;
        }
        else
        {
            Seg.LinkAddress = NIL_RTLDRADDR;
            Seg.RVA         = NIL_RTLDRADDR;
            Seg.cbMapped    = NIL_RTLDRADDR;
        }
        if (paShdrs[iShdr].sh_type != SHT_NOBITS)
        {
            Seg.offFile     = paShdrs[iShdr].sh_offset;
            Seg.cbFile      = paShdrs[iShdr].sh_size;
        }
        else
        {
            Seg.offFile     = -1;
            Seg.cbFile      = 0;
        }

        rc = pfnCallback(pMod, &Seg, pvUser);
        if (rc != VINF_SUCCESS)
            return rc;
    }

    return VINF_SUCCESS;
}


/** @copydoc RTLDROPS::pfnLinkAddressToSegOffset. */
static DECLCALLBACK(int) RTLDRELF_NAME(LinkAddressToSegOffset)(PRTLDRMODINTERNAL pMod, RTLDRADDR LinkAddress,
                                                               uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;

    const Elf_Shdr *pShdrEnd = NULL;
    unsigned        cLeft    = pModElf->Ehdr.e_shnum - pModElf->iFirstSect;
    const Elf_Shdr *pShdr    = &pModElf->paOrgShdrs[pModElf->Ehdr.e_shnum];
    while (cLeft-- > 0)
    {
        pShdr--;
        if (pShdr->sh_flags & SHF_ALLOC)
        {
            RTLDRADDR offSeg = LinkAddress - pShdr->sh_addr;
            if (offSeg < pShdr->sh_size)
            {
                *poffSeg = offSeg;
                *piSeg   = cLeft;
                return VINF_SUCCESS;
            }
            if (offSeg == pShdr->sh_size)
                pShdrEnd = pShdr;
        }
    }

    if (pShdrEnd)
    {
        *poffSeg = pShdrEnd->sh_size;
        *piSeg   = pShdrEnd - pModElf->paOrgShdrs - pModElf->iFirstSect;
        return VINF_SUCCESS;
    }

    return VERR_LDR_INVALID_LINK_ADDRESS;
}


/** @copydoc RTLDROPS::pfnLinkAddressToRva. */
static DECLCALLBACK(int) RTLDRELF_NAME(LinkAddressToRva)(PRTLDRMODINTERNAL pMod, RTLDRADDR LinkAddress, PRTLDRADDR pRva)
{
    PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;
    uint32_t     iSeg;
    RTLDRADDR    offSeg;
    int rc = RTLDRELF_NAME(LinkAddressToSegOffset)(pMod, LinkAddress, &iSeg, &offSeg);
    if (RT_SUCCESS(rc))
        *pRva = pModElf->paShdrs[iSeg + pModElf->iFirstSect].sh_addr + offSeg;
    return rc;
}


/** @copydoc RTLDROPS::pfnSegOffsetToRva. */
static DECLCALLBACK(int) RTLDRELF_NAME(SegOffsetToRva)(PRTLDRMODINTERNAL pMod, uint32_t iSeg, RTLDRADDR offSeg,
                                                       PRTLDRADDR pRva)
{
    PRTLDRMODELF pModElf = (PRTLDRMODELF)pMod;
    if (iSeg >= pModElf->Ehdr.e_shnum - pModElf->iFirstSect)
        return VERR_LDR_INVALID_SEG_OFFSET;

    iSeg += pModElf->iFirstSect; /* skip section 0 if not used */
    if (offSeg > pModElf->paShdrs[iSeg].sh_size)
    {
        const Elf_Shdr *pShdr2 = RTLDRELF_NAME(GetNextAllocatedSection)(pModElf, iSeg);
        if (   !pShdr2
            || offSeg > (pShdr2->sh_addr - pModElf->paShdrs[iSeg].sh_addr))
            return VERR_LDR_INVALID_SEG_OFFSET;
    }

    if (!(pModElf->paShdrs[iSeg].sh_flags & SHF_ALLOC))
        return VERR_LDR_INVALID_SEG_OFFSET;

    *pRva = pModElf->paShdrs[iSeg].sh_addr;
    return VINF_SUCCESS;
}


/** @copydoc RTLDROPS::pfnRvaToSegOffset. */
static DECLCALLBACK(int) RTLDRELF_NAME(RvaToSegOffset)(PRTLDRMODINTERNAL pMod, RTLDRADDR Rva,
                                                       uint32_t *piSeg, PRTLDRADDR poffSeg)
{
    PRTLDRMODELF    pModElf  = (PRTLDRMODELF)pMod;
    Elf_Addr        PrevAddr = 0;
    unsigned        cLeft    = pModElf->Ehdr.e_shnum - pModElf->iFirstSect;
    const Elf_Shdr *pShdr    = &pModElf->paShdrs[pModElf->Ehdr.e_shnum];
    while (cLeft-- > 0)
    {
        pShdr--;
        if (pShdr->sh_flags & SHF_ALLOC)
        {
            Elf_Addr    cbSeg  = PrevAddr ? PrevAddr - pShdr->sh_addr : pShdr->sh_size;
            RTLDRADDR   offSeg = Rva - pShdr->sh_addr;
            if (offSeg <= cbSeg)
            {
                *poffSeg = offSeg;
                *piSeg   = cLeft;
                return VINF_SUCCESS;
            }
            PrevAddr = pShdr->sh_addr;
        }
    }

    return VERR_LDR_INVALID_RVA;
}


/** @callback_method_impl{FNRTLDRIMPORT, Stub used by ReadDbgInfo.} */
static DECLCALLBACK(int) RTLDRELF_NAME(GetImportStubCallback)(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol,
                                                              unsigned uSymbol, PRTLDRADDR pValue, void *pvUser)
{
    RT_NOREF_PV(hLdrMod); RT_NOREF_PV(pszModule); RT_NOREF_PV(pszSymbol);
    RT_NOREF_PV(uSymbol); RT_NOREF_PV(pValue); RT_NOREF_PV(pvUser);
    return VERR_SYMBOL_NOT_FOUND;
}


/** @copydoc RTLDROPS::pfnReadDbgInfo. */
static DECLCALLBACK(int) RTLDRELF_NAME(ReadDbgInfo)(PRTLDRMODINTERNAL pMod, uint32_t iDbgInfo, RTFOFF off,
                                                    size_t cb, void *pvBuf)
{
    PRTLDRMODELF pThis = (PRTLDRMODELF)pMod;
    LogFlow(("%s: iDbgInfo=%#x off=%RTfoff cb=%#zu\n", __FUNCTION__, iDbgInfo, off, cb));

    /*
     * Input validation.
     */
    AssertReturn(iDbgInfo < pThis->Ehdr.e_shnum && iDbgInfo + 1 < pThis->Ehdr.e_shnum, VERR_INVALID_PARAMETER);
    iDbgInfo++;
    AssertReturn(!(pThis->paShdrs[iDbgInfo].sh_flags & SHF_ALLOC), VERR_INVALID_PARAMETER);
    AssertReturn(pThis->paShdrs[iDbgInfo].sh_type   == SHT_PROGBITS, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->paShdrs[iDbgInfo].sh_offset == (uint64_t)off, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->paShdrs[iDbgInfo].sh_size   == cb, VERR_INVALID_PARAMETER);
    uint64_t cbRawImage = pThis->Core.pReader->pfnSize(pThis->Core.pReader);
    AssertReturn(off >= 0 && cb <= cbRawImage && (uint64_t)off + cb <= cbRawImage, VERR_INVALID_PARAMETER);

    /*
     * Read it from the file and look for fixup sections.
     */
    int rc;
    if (pThis->pvBits)
        memcpy(pvBuf, (const uint8_t *)pThis->pvBits + (size_t)off, cb);
    else
    {
        rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, pvBuf, cb, off);
        if (RT_FAILURE(rc))
            return rc;
    }

    uint32_t iRelocs = iDbgInfo + 1;
    if (   iRelocs >= pThis->Ehdr.e_shnum
        || pThis->paShdrs[iRelocs].sh_info != iDbgInfo
        || (   pThis->paShdrs[iRelocs].sh_type != SHT_REL
            && pThis->paShdrs[iRelocs].sh_type != SHT_RELA) )
    {
        iRelocs = 0;
        while (   iRelocs < pThis->Ehdr.e_shnum
               && (   pThis->paShdrs[iRelocs].sh_info != iDbgInfo
                   || (   pThis->paShdrs[iRelocs].sh_type != SHT_REL
                       && pThis->paShdrs[iRelocs].sh_type != SHT_RELA)) )
            iRelocs++;
    }
    if (   iRelocs < pThis->Ehdr.e_shnum
        && pThis->paShdrs[iRelocs].sh_size > 0)
    {
        /*
         * Load the relocations.
         */
        uint8_t       *pbRelocsBuf = NULL;
        const uint8_t *pbRelocs;
        if (pThis->pvBits)
            pbRelocs = (const uint8_t *)pThis->pvBits + pThis->paShdrs[iRelocs].sh_offset;
        else
        {
            pbRelocs = pbRelocsBuf = (uint8_t *)RTMemTmpAlloc(pThis->paShdrs[iRelocs].sh_size);
            if (!pbRelocsBuf)
                return VERR_NO_TMP_MEMORY;
            rc = pThis->Core.pReader->pfnRead(pThis->Core.pReader, pbRelocsBuf,
                                              pThis->paShdrs[iRelocs].sh_size,
                                              pThis->paShdrs[iRelocs].sh_offset);
            if (RT_FAILURE(rc))
            {
                RTMemTmpFree(pbRelocsBuf);
                return rc;
            }
        }

        /*
         * Apply the relocations.
         */
        if (pThis->Ehdr.e_type == ET_REL)
            rc = RTLDRELF_NAME(RelocateSectionRel)(pThis, pThis->LinkAddress,
                                                   RTLDRELF_NAME(GetImportStubCallback), NULL /*pvUser*/,
                                                   pThis->paShdrs[iDbgInfo].sh_addr,
                                                   pThis->paShdrs[iDbgInfo].sh_size,
                                                   (const uint8_t *)pvBuf,
                                                   (uint8_t *)pvBuf,
                                                   pbRelocs,
                                                   pThis->paShdrs[iRelocs].sh_size);
        else
            rc = RTLDRELF_NAME(RelocateSectionExecDyn)(pThis, pThis->LinkAddress,
                                                       RTLDRELF_NAME(GetImportStubCallback), NULL /*pvUser*/,
                                                       pThis->paShdrs[iDbgInfo].sh_addr,
                                                       pThis->paShdrs[iDbgInfo].sh_size,
                                                       (const uint8_t *)pvBuf,
                                                       (uint8_t *)pvBuf,
                                                       pbRelocs,
                                                       pThis->paShdrs[iRelocs].sh_size);

        RTMemTmpFree(pbRelocsBuf);
    }
    else
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Handles RTLDRPROP_BUILDID queries.
 */
static int RTLDRELF_NAME(QueryPropBuildId)(PRTLDRMODELF pThis, void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    /*
     * Map the image bits if not already done and setup pointer into it.
     */
    int rc = RTLDRELF_NAME(MapBits)(pThis, true);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Search for the build ID.
     */
    const Elf_Shdr *paShdrs = pThis->paOrgShdrs;
    for (unsigned iShdr = 0; iShdr < pThis->Ehdr.e_shnum; iShdr++)
    {
        const char *pszSectName = ELF_SH_STR(pThis, paShdrs[iShdr].sh_name);

        if (!strcmp(pszSectName, ".note.gnu.build-id"))
        {
            if ((paShdrs[iShdr].sh_size & 3) || paShdrs[iShdr].sh_size < sizeof(Elf_Nhdr))
                return VERR_BAD_EXE_FORMAT;

            Elf_Nhdr *pNHdr = (Elf_Nhdr *)((uintptr_t)pThis->pvBits + (uintptr_t)paShdrs[iShdr].sh_offset);
            if (   pNHdr->n_namesz > paShdrs[iShdr].sh_size
                || pNHdr->n_descsz > paShdrs[iShdr].sh_size
                || (paShdrs[iShdr].sh_size - pNHdr->n_descsz) < pNHdr->n_namesz
                || pNHdr->n_type != NT_GNU_BUILD_ID)
                return VERR_BAD_EXE_FORMAT;

            const char *pszOwner = (const char *)(pNHdr + 1);
            if (   !RTStrEnd(pszOwner, pNHdr->n_namesz)
                || strcmp(pszOwner, "GNU"))
                return VERR_BAD_EXE_FORMAT;

            if (cbBuf < pNHdr->n_descsz)
                return VERR_BUFFER_OVERFLOW;

            memcpy(pvBuf, pszOwner + pNHdr->n_namesz, pNHdr->n_descsz);
            *pcbRet = pNHdr->n_descsz;
            return VINF_SUCCESS;
        }
    }

    return VERR_NOT_FOUND;
}


/** @interface_method_impl{RTLDROPS,pfnQueryProp} */
static DECLCALLBACK(int) RTLDRELF_NAME(QueryProp)(PRTLDRMODINTERNAL pMod, RTLDRPROP enmProp, void const *pvBits,
                                                  void *pvBuf, size_t cbBuf, size_t *pcbRet)
{
    PRTLDRMODELF pThis = (PRTLDRMODELF)pMod;
    RT_NOREF(pvBits);
    switch (enmProp)
    {
        case RTLDRPROP_BUILDID:
            return RTLDRELF_NAME(QueryPropBuildId)(pThis, pvBuf, cbBuf, pcbRet);

        case RTLDRPROP_IS_SIGNED:
            *pcbRet = sizeof(bool);
            return rtLdrELFLnxKModQueryPropIsSigned(pThis->Core.pReader, (bool *)pvBuf);

        case RTLDRPROP_PKCS7_SIGNED_DATA:
            *pcbRet = sizeof(bool);
            return rtLdrELFLnxKModQueryPropPkcs7SignedData(pThis->Core.pReader, pvBuf, cbBuf, pcbRet);

        default:
            return VERR_NOT_FOUND;
    }
}


/**
 * @interface_method_impl{RTLDROPS,pfnUnwindFrame}
 */
static DECLCALLBACK(int)
RTLDRELF_NAME(UnwindFrame)(PRTLDRMODINTERNAL pMod, void const *pvBits, uint32_t iSeg, RTUINTPTR off, PRTDBGUNWINDSTATE pState)
{
    PRTLDRMODELF pThis = (PRTLDRMODELF)pMod;
    LogFlow(("%s: iSeg=%#x off=%RTptr\n", __FUNCTION__, iSeg, off));

    /*
     * Process the input address, making us both RVA and proper seg:offset out of it.
     */
    int rc;
    RTLDRADDR uRva = off;
    if (iSeg == UINT32_MAX)
        rc = RTLDRELF_NAME(RvaToSegOffset)(pMod, uRva, &iSeg, &off);
    else
        rc = RTLDRELF_NAME(SegOffsetToRva)(pMod, iSeg, off, &uRva);
    AssertRCReturn(rc, rc);

    /*
     * Map the image bits if not already done and setup pointer into it.
     */
    RT_NOREF(pvBits); /** @todo Try use passed in pvBits? */
    rc = RTLDRELF_NAME(MapBits)(pThis, true);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Do we need to search for .eh_frame and .eh_frame_hdr?
     */
    if (pThis->iShEhFrame == 0)
    {
        pThis->iShEhFrame = ~0U;
        pThis->iShEhFrameHdr = ~0U;
        unsigned cLeft = 2;
        for (unsigned iShdr = 1; iShdr < pThis->Ehdr.e_shnum; iShdr++)
        {
            const char *pszName = ELF_SH_STR(pThis, pThis->paShdrs[iShdr].sh_name);
            if (   pszName[0] == '.'
                && pszName[1] == 'e'
                && pszName[2] == 'h'
                && pszName[3] == '_'
                && pszName[4] == 'f'
                && pszName[5] == 'r'
                && pszName[6] == 'a'
                && pszName[7] == 'm'
                && pszName[8] == 'e')
            {
                if (pszName[9] == '\0')
                    pThis->iShEhFrame = iShdr;
                else if (   pszName[9] == '_'
                         && pszName[10] == 'h'
                         && pszName[11] == 'd'
                         && pszName[12] == 'r'
                         && pszName[13] == '\0')
                    pThis->iShEhFrameHdr = iShdr;
                else
                    continue;
                if (--cLeft == 0)
                    break;
            }
        }
    }

    /*
     * Any info present?
     */
    unsigned iShdr = pThis->iShEhFrame;
    if (   iShdr != ~0U
        && pThis->paShdrs[iShdr].sh_size > 0)
    {
        if (pThis->paShdrs[iShdr].sh_flags & SHF_ALLOC)
            return rtDwarfUnwind_EhData((uint8_t const *)pThis->pvBits + pThis->paShdrs[iShdr].sh_addr,
                                        pThis->paShdrs[iShdr].sh_size, pThis->paShdrs[iShdr].sh_addr,
                                        iSeg, off, uRva, pState, pThis->Core.enmArch);
    }
    return VERR_DBG_NO_UNWIND_INFO;
}


/**
 * The ELF module operations.
 */
static RTLDROPS RTLDRELF_MID(s_rtldrElf,Ops) =
{
#if   ELF_MODE == 32
    "elf32",
#elif ELF_MODE == 64
    "elf64",
#endif
    RTLDRELF_NAME(Close),
    NULL, /* Get Symbol */
    RTLDRELF_NAME(Done),
    RTLDRELF_NAME(EnumSymbols),
    /* ext: */
    RTLDRELF_NAME(GetImageSize),
    RTLDRELF_NAME(GetBits),
    RTLDRELF_NAME(Relocate),
    RTLDRELF_NAME(GetSymbolEx),
    NULL /*pfnQueryForwarderInfo*/,
    RTLDRELF_NAME(EnumDbgInfo),
    RTLDRELF_NAME(EnumSegments),
    RTLDRELF_NAME(LinkAddressToSegOffset),
    RTLDRELF_NAME(LinkAddressToRva),
    RTLDRELF_NAME(SegOffsetToRva),
    RTLDRELF_NAME(RvaToSegOffset),
    RTLDRELF_NAME(ReadDbgInfo),
    RTLDRELF_NAME(QueryProp),
    NULL /*pfnVerifySignature*/,
    rtldrELFLnxKModHashImage,
    RTLDRELF_NAME(UnwindFrame),
    42
};



/**
 * Validates the ELF header.
 *
 * @returns iprt status code.
 * @param   pEhdr       Pointer to the ELF header.
 * @param   cbRawImage  The size of the raw image.
 * @param   pszLogName  The log name.
 * @param   penmArch    Where to return the architecture.
 * @param   pErrInfo    Where to return extended error info. Optional.
 */
static int RTLDRELF_NAME(ValidateElfHeader)(const Elf_Ehdr *pEhdr, uint64_t cbRawImage, const char *pszLogName,
                                            PRTLDRARCH penmArch, PRTERRINFO pErrInfo)
{
    Log3(("RTLdrELF:     e_ident: %.*Rhxs\n"
          "RTLdrELF:      e_type: " FMT_ELF_HALF "\n"
          "RTLdrELF:   e_version: " FMT_ELF_HALF "\n"
          "RTLdrELF:     e_entry: " FMT_ELF_ADDR "\n"
          "RTLdrELF:     e_phoff: " FMT_ELF_OFF  "\n"
          "RTLdrELF:     e_shoff: " FMT_ELF_OFF  "\n"
          "RTLdrELF:     e_flags: " FMT_ELF_WORD "\n"
          "RTLdrELF:    e_ehsize: " FMT_ELF_HALF "\n"
          "RTLdrELF: e_phentsize: " FMT_ELF_HALF "\n"
          "RTLdrELF:     e_phnum: " FMT_ELF_HALF "\n"
          "RTLdrELF: e_shentsize: " FMT_ELF_HALF "\n"
          "RTLdrELF:     e_shnum: " FMT_ELF_HALF "\n"
          "RTLdrELF:  e_shstrndx: " FMT_ELF_HALF "\n",
          RT_ELEMENTS(pEhdr->e_ident), &pEhdr->e_ident[0], pEhdr->e_type, pEhdr->e_version,
          pEhdr->e_entry, pEhdr->e_phoff, pEhdr->e_shoff,pEhdr->e_flags, pEhdr->e_ehsize, pEhdr->e_phentsize,
          pEhdr->e_phnum, pEhdr->e_shentsize, pEhdr->e_shnum, pEhdr->e_shstrndx));

    if (    pEhdr->e_ident[EI_MAG0] != ELFMAG0
        ||  pEhdr->e_ident[EI_MAG1] != ELFMAG1
        ||  pEhdr->e_ident[EI_MAG2] != ELFMAG2
        ||  pEhdr->e_ident[EI_MAG3] != ELFMAG3)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: Invalid ELF magic (%.*Rhxs)", pszLogName, sizeof(pEhdr->e_ident), pEhdr->e_ident);
    if (pEhdr->e_ident[EI_CLASS] != RTLDRELF_SUFF(ELFCLASS))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: Invalid ELF class (%.*Rhxs)", pszLogName, sizeof(pEhdr->e_ident), pEhdr->e_ident);
    if (pEhdr->e_ident[EI_DATA] != ELFDATA2LSB)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRELF_ODD_ENDIAN,
                                   "%s: ELF endian %x is unsupported", pszLogName, pEhdr->e_ident[EI_DATA]);
    if (pEhdr->e_version != EV_CURRENT)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRELF_VERSION,
                                   "%s: ELF version %x is unsupported", pszLogName, pEhdr->e_version);

    if (sizeof(Elf_Ehdr) != pEhdr->e_ehsize)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: Elf header e_ehsize is %d expected %d!", pszLogName, pEhdr->e_ehsize, sizeof(Elf_Ehdr));
    if (    sizeof(Elf_Phdr) != pEhdr->e_phentsize
        &&  (   pEhdr->e_phnum != 0
             || pEhdr->e_type == ET_DYN
             || pEhdr->e_type == ET_EXEC))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Elf header e_phentsize is %d expected %d!",
                                   pszLogName, pEhdr->e_phentsize, sizeof(Elf_Phdr));
    if (sizeof(Elf_Shdr) != pEhdr->e_shentsize)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Elf header e_shentsize is %d expected %d!",
                                   pszLogName, pEhdr->e_shentsize, sizeof(Elf_Shdr));

    switch (pEhdr->e_type)
    {
        case ET_REL:
        case ET_EXEC:
        case ET_DYN:
            break;
        default:
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: image type %#x is not supported!",
                                       pszLogName, pEhdr->e_type);
    }

    switch (pEhdr->e_machine)
    {
#if   ELF_MODE == 32
        case EM_386:
        case EM_486:
            *penmArch = RTLDRARCH_X86_32;
            break;
#elif ELF_MODE == 64
        case EM_X86_64:
            *penmArch = RTLDRARCH_AMD64;
            break;
#endif
        default:
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRELF_MACHINE,
                                       "%s: machine type %u is not supported!", pszLogName, pEhdr->e_machine);
    }

    if (    pEhdr->e_phoff < pEhdr->e_ehsize
        &&  !(pEhdr->e_phoff && pEhdr->e_phnum)
        &&  pEhdr->e_phnum)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: The program headers overlap with the ELF header! e_phoff=" FMT_ELF_OFF,
                                   pszLogName, pEhdr->e_phoff);
    if (    pEhdr->e_phoff + pEhdr->e_phnum * pEhdr->e_phentsize > cbRawImage
        ||  pEhdr->e_phoff + pEhdr->e_phnum * pEhdr->e_phentsize < pEhdr->e_phoff)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: The program headers extends beyond the file! e_phoff=" FMT_ELF_OFF " e_phnum=" FMT_ELF_HALF,
                                   pszLogName, pEhdr->e_phoff, pEhdr->e_phnum);


    if (    pEhdr->e_shoff < pEhdr->e_ehsize
        &&  !(pEhdr->e_shoff && pEhdr->e_shnum))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: The section headers overlap with the ELF header! e_shoff=" FMT_ELF_OFF,
                                   pszLogName, pEhdr->e_shoff);
    if (    pEhdr->e_shoff + pEhdr->e_shnum * pEhdr->e_shentsize > cbRawImage
        ||  pEhdr->e_shoff + pEhdr->e_shnum * pEhdr->e_shentsize < pEhdr->e_shoff)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: The section headers extends beyond the file! e_shoff=" FMT_ELF_OFF " e_shnum=" FMT_ELF_HALF,
                                   pszLogName, pEhdr->e_shoff, pEhdr->e_shnum);

    if (pEhdr->e_shstrndx == 0 || pEhdr->e_shstrndx > pEhdr->e_shnum)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: The section headers string table is out of bounds! e_shstrndx=" FMT_ELF_HALF " e_shnum=" FMT_ELF_HALF,
                                   pszLogName, pEhdr->e_shstrndx, pEhdr->e_shnum);

    return VINF_SUCCESS;
}


/**
 * Gets the section header name.
 *
 * @returns pszName.
 * @param   pEhdr           The elf header.
 * @param   offName         The offset of the section header name.
 * @param   pszName         Where to store the name.
 * @param   cbName          The size of the buffer pointed to by pszName.
 */
static const char *RTLDRELF_NAME(GetSHdrName)(PRTLDRMODELF pModElf, Elf_Word offName, char *pszName, size_t cbName)
{
    RTFOFF off = pModElf->paShdrs[pModElf->Ehdr.e_shstrndx].sh_offset + offName;
    int rc = pModElf->Core.pReader->pfnRead(pModElf->Core.pReader, pszName, cbName - 1, off);
    if (RT_FAILURE(rc))
    {
        /* read by for byte. */
        for (unsigned i = 0; i < cbName; i++, off++)
        {
            rc = pModElf->Core.pReader->pfnRead(pModElf->Core.pReader, pszName + i, 1, off);
            if (RT_FAILURE(rc))
            {
                pszName[i] = '\0';
                break;
            }
        }
    }

    pszName[cbName - 1] = '\0';
    return pszName;
}


/**
 * Validates a section header.
 *
 * @returns iprt status code.
 * @param   pModElf     Pointer to the module structure.
 * @param   iShdr       The index of section header which should be validated.
 *                      The section headers are found in the pModElf->paShdrs array.
 * @param   cbRawImage  The size of the raw image.
 * @param   pszLogName  The log name.
 * @param   pErrInfo    Where to return extended error info. Optional.
 */
static int RTLDRELF_NAME(ValidateSectionHeader)(PRTLDRMODELF pModElf, unsigned iShdr, uint64_t cbRawImage,
                                                const char *pszLogName, PRTERRINFO pErrInfo)
{
    const Elf_Shdr *pShdr = &pModElf->paShdrs[iShdr];
    char szSectionName[80]; NOREF(szSectionName);
    Log3(("RTLdrELF: Section Header #%d:\n"
          "RTLdrELF:      sh_name: " FMT_ELF_WORD " - %s\n"
          "RTLdrELF:      sh_type: " FMT_ELF_WORD " (%s)\n"
          "RTLdrELF:     sh_flags: " FMT_ELF_XWORD "\n"
          "RTLdrELF:      sh_addr: " FMT_ELF_ADDR "\n"
          "RTLdrELF:    sh_offset: " FMT_ELF_OFF "\n"
          "RTLdrELF:      sh_size: " FMT_ELF_XWORD "\n"
          "RTLdrELF:      sh_link: " FMT_ELF_WORD "\n"
          "RTLdrELF:      sh_info: " FMT_ELF_WORD "\n"
          "RTLdrELF: sh_addralign: " FMT_ELF_XWORD "\n"
          "RTLdrELF:   sh_entsize: " FMT_ELF_XWORD "\n",
          iShdr,
          pShdr->sh_name, RTLDRELF_NAME(GetSHdrName)(pModElf, pShdr->sh_name, szSectionName, sizeof(szSectionName)),
          pShdr->sh_type, rtldrElfGetShdrType(pShdr->sh_type), pShdr->sh_flags, pShdr->sh_addr,
          pShdr->sh_offset, pShdr->sh_size, pShdr->sh_link, pShdr->sh_info, pShdr->sh_addralign,
          pShdr->sh_entsize));

    if (iShdr == 0)
    {
        if (   pShdr->sh_name       != 0
            || pShdr->sh_type       != SHT_NULL
            || pShdr->sh_flags      != 0
            || pShdr->sh_addr       != 0
            || pShdr->sh_size       != 0
            || pShdr->sh_offset     != 0
            || pShdr->sh_link       != SHN_UNDEF
            || pShdr->sh_addralign  != 0
            || pShdr->sh_entsize    != 0 )
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                       "%s: Bad #0 section: %.*Rhxs", pszLogName, sizeof(*pShdr), pShdr);
        return VINF_SUCCESS;
    }

    if (pShdr->sh_name >= pModElf->cbShStr)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: Shdr #%d: sh_name (%d) is beyond the end of the section header string table (%d)!",
                                   pszLogName, iShdr, pShdr->sh_name, pModElf->cbShStr);

    if (pShdr->sh_link >= pModElf->Ehdr.e_shnum)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: Shdr #%d: sh_link (%d) is beyond the end of the section table (%d)!",
                                   pszLogName, iShdr, pShdr->sh_link, pModElf->Ehdr.e_shnum);

    switch (pShdr->sh_type)
    {
        /** @todo find specs and check up which sh_info fields indicates section table entries */
        case 12301230:
            if (pShdr->sh_info >= pModElf->Ehdr.e_shnum)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                           "%s: Shdr #%d: sh_info (%d) is beyond the end of the section table (%d)!",
                                           pszLogName, iShdr, pShdr->sh_link, pModElf->Ehdr.e_shnum);
            break;

        case SHT_NULL:
            break;
        case SHT_PROGBITS:
        case SHT_SYMTAB:
        case SHT_STRTAB:
        case SHT_RELA:
        case SHT_HASH:
        case SHT_DYNAMIC:
        case SHT_NOTE:
        case SHT_NOBITS:
        case SHT_REL:
        case SHT_SHLIB:
        case SHT_DYNSYM:
            /*
             * For these types sh_info doesn't have any special meaning, or anything which
             * we need/can validate now.
             */
            break;


        default:
            Log(("RTLdrELF: %s: Warning, unknown type %d!\n", pszLogName, pShdr->sh_type));
            break;
    }

    if (    pShdr->sh_type != SHT_NOBITS
        &&  pShdr->sh_size)
    {
        uint64_t offEnd = pShdr->sh_offset + pShdr->sh_size;
        if (    offEnd > cbRawImage
            ||  offEnd < (uint64_t)pShdr->sh_offset)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                       "%s: Shdr #%d: sh_offset (" FMT_ELF_OFF ") + sh_size (" FMT_ELF_XWORD " = %RX64) is beyond the end of the file (%RX64)!",
                                       pszLogName, iShdr, pShdr->sh_offset, pShdr->sh_size, offEnd, cbRawImage);
        if (pShdr->sh_offset < sizeof(Elf_Ehdr))
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                       "%s: Shdr #%d: sh_offset (" FMT_ELF_OFF ") + sh_size (" FMT_ELF_XWORD ") is starting in the ELF header!",
                                       pszLogName, iShdr, pShdr->sh_offset, pShdr->sh_size);
    }

    return VINF_SUCCESS;
}


/**
 * Process the section headers.
 *
 * @returns iprt status code.
 * @param   pModElf     Pointer to the module structure.
 * @param   paShdrs     The section headers.
 * @param   cbRawImage  The size of the raw image.
 * @param   pszLogName  The log name.
 * @param   pErrInfo    Where to return extended error info. Optional.
 */
static int RTLDRELF_NAME(ValidateAndProcessSectionHeaders)(PRTLDRMODELF pModElf, Elf_Shdr *paShdrs, uint64_t cbRawImage,
                                                           const char *pszLogName, PRTERRINFO pErrInfo)
{
    Elf_Addr uNextAddr = 0;
    for (unsigned i = 0; i < pModElf->Ehdr.e_shnum; i++)
    {
        int rc = RTLDRELF_NAME(ValidateSectionHeader)(pModElf, i, cbRawImage, pszLogName, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * We're looking for symbol tables.
         */
        if (paShdrs[i].sh_type == SHT_SYMTAB)
        {
            if (pModElf->Rel.iSymSh != ~0U)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRELF_MULTIPLE_SYMTABS,
                                           "%s: Multiple symbol tabs! iSymSh=%d i=%d", pszLogName, pModElf->Rel.iSymSh, i);
            pModElf->Rel.iSymSh = i;
            pModElf->Rel.cSyms  = (unsigned)(paShdrs[i].sh_size / sizeof(Elf_Sym));
            AssertBreakStmt(pModElf->Rel.cSyms == paShdrs[i].sh_size / sizeof(Elf_Sym), rc = VERR_IMAGE_TOO_BIG);
            pModElf->Rel.iStrSh = paShdrs[i].sh_link;
            pModElf->Rel.cbStr  = (unsigned)paShdrs[pModElf->Rel.iStrSh].sh_size;
            AssertBreakStmt(pModElf->Rel.cbStr == paShdrs[pModElf->Rel.iStrSh].sh_size, rc = VERR_IMAGE_TOO_BIG);
        }
        else if (paShdrs[i].sh_type == SHT_DYNSYM)
        {
            if (pModElf->Dyn.iSymSh != ~0U)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_LDRELF_MULTIPLE_SYMTABS,
                                           "%s: Multiple dynamic symbol tabs! iSymSh=%d i=%d", pszLogName, pModElf->Dyn.iSymSh, i);
            if (pModElf->Ehdr.e_type != ET_DYN && pModElf->Ehdr.e_type != ET_EXEC)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                           "%s: Unexpected SHT_DYNSYM (i=%d) for e_type=%d", pszLogName, i, pModElf->Ehdr.e_type);
            pModElf->Dyn.iSymSh = i;
            pModElf->Dyn.cSyms  = (unsigned)(paShdrs[i].sh_size / sizeof(Elf_Sym));
            AssertBreakStmt(pModElf->Dyn.cSyms == paShdrs[i].sh_size / sizeof(Elf_Sym), rc = VERR_IMAGE_TOO_BIG);
            pModElf->Dyn.iStrSh = paShdrs[i].sh_link;
            pModElf->Dyn.cbStr  = (unsigned)paShdrs[pModElf->Dyn.iStrSh].sh_size;
            AssertBreakStmt(pModElf->Dyn.cbStr == paShdrs[pModElf->Dyn.iStrSh].sh_size, rc = VERR_IMAGE_TOO_BIG);
        }
        /*
         * We're also look for the dynamic section.
         */
        else if (paShdrs[i].sh_type == SHT_DYNAMIC)
        {
            if (pModElf->iShDynamic != ~0U)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                           "%s: Multiple dynamic sections! iShDynamic=%d i=%d",
                                           pszLogName, pModElf->iShDynamic, i);
            if (pModElf->Ehdr.e_type != ET_DYN && pModElf->Ehdr.e_type != ET_EXEC)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                           "%s: Unexpected SHT_DYNAMIC (i=%d) for e_type=%d", pszLogName, i, pModElf->Ehdr.e_type);
            if (paShdrs[i].sh_entsize != sizeof(Elf_Dyn))
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                           "%s: SHT_DYNAMIC (i=%d) sh_entsize=" FMT_ELF_XWORD ",  expected %#zx",
                                           pszLogName, i, paShdrs[i].sh_entsize, sizeof(Elf_Dyn));
            pModElf->iShDynamic = i;
            Elf_Xword const cDynamic = paShdrs[i].sh_size / sizeof(Elf_Dyn);
            if (cDynamic > _64K || cDynamic < 2)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                           "%s: SHT_DYNAMIC (i=%d) sh_size=" FMT_ELF_XWORD " is out of range (2..64K)",
                                           pszLogName, i, paShdrs[i].sh_size);
            pModElf->cDynamic = (unsigned)cDynamic;
        }

        /*
         * Special checks for the section string table.
         */
        if (i == pModElf->Ehdr.e_shstrndx)
        {
            if (paShdrs[i].sh_type != SHT_STRTAB)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                           "%s: Section header string table is not a SHT_STRTAB: %#x",
                                           pszLogName, paShdrs[i].sh_type);
            if (paShdrs[i].sh_size == 0)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Section header string table is empty", pszLogName);
        }

        /*
         * Kluge for the .data..percpu segment in 64-bit linux kernels.
         */
        if (paShdrs[i].sh_flags & SHF_ALLOC)
        {
            if (   paShdrs[i].sh_addr == 0
                && paShdrs[i].sh_addr < uNextAddr)
            {
                Elf_Addr uAddr = RT_ALIGN_T(uNextAddr, paShdrs[i].sh_addralign, Elf_Addr);
                Log(("RTLdrElf: Out of order section #%d; adjusting sh_addr from " FMT_ELF_ADDR " to " FMT_ELF_ADDR "\n",
                     i, paShdrs[i].sh_addr, uAddr));
                paShdrs[i].sh_addr = uAddr;
            }
            uNextAddr = paShdrs[i].sh_addr + paShdrs[i].sh_size;
        }
    } /* for each section header */

    return VINF_SUCCESS;
}


/**
 * Process the section headers.
 *
 * @returns iprt status code.
 * @param   pModElf     Pointer to the module structure.
 * @param   paShdrs     The section headers.
 * @param   cbRawImage  The size of the raw image.
 * @param   pszLogName  The log name.
 * @param   pErrInfo    Where to return extended error info. Optional.
 */
static int RTLDRELF_NAME(ValidateAndProcessDynamicInfo)(PRTLDRMODELF pModElf, uint64_t cbRawImage, uint32_t fFlags,
                                                        const char *pszLogName, PRTERRINFO pErrInfo)
{
    /*
     * Check preconditions.
     */
    AssertReturn(pModElf->Ehdr.e_type == ET_DYN || pModElf->Ehdr.e_type == ET_EXEC, VERR_INTERNAL_ERROR_2);
    if (pModElf->Ehdr.e_phnum <= 1 || pModElf->Ehdr.e_phnum >= _32K)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                   "%s: e_phnum=%u is out of bounds (2..32K)", pszLogName, pModElf->Ehdr.e_phnum);
    if (pModElf->iShDynamic == ~0U)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: no .dynamic section", pszLogName);
    AssertReturn(pModElf->cDynamic > 1 && pModElf->cDynamic <= _64K, VERR_INTERNAL_ERROR_3);

    /* ASSUME that the sections are ordered by address.  That simplifies
       validation code further down. */
    AssertReturn(pModElf->Ehdr.e_shnum >= 2, VERR_INTERNAL_ERROR_4);
    Elf_Shdr const *paShdrs  = pModElf->paShdrs;
    Elf_Addr        uPrevEnd = paShdrs[1].sh_addr + paShdrs[1].sh_size;
    for (unsigned i = 2; i < pModElf->Ehdr.e_shnum; i++)
        if (paShdrs[i].sh_flags & SHF_ALLOC)
        {
            if (uPrevEnd > paShdrs[i].sh_addr)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                           "%s: section %u is out of order: uPrevEnd=" FMT_ELF_ADDR " sh_addr=" FMT_ELF_ADDR,
                                           pszLogName, i, uPrevEnd, paShdrs[i].sh_addr);
            uPrevEnd = paShdrs[i].sh_addr + paShdrs[i].sh_size;
        }

    /* Must have string and symbol tables. */
    if (pModElf->Dyn.iStrSh == ~0U)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: No dynamic string table section", pszLogName);
    if (pModElf->Dyn.iSymSh == ~0U)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: No dynamic symbol table section", pszLogName);

    /*
     * Load the program headers.
     */
    size_t const cbPhdrs = sizeof(pModElf->paPhdrs[0]) * pModElf->Ehdr.e_phnum;
    Elf_Phdr    *paPhdrs = (Elf_Phdr *)RTMemAllocZ(cbPhdrs);
    pModElf->paPhdrs = paPhdrs;
    AssertReturn(paPhdrs, VERR_NO_MEMORY);

    int rc = pModElf->Core.pReader->pfnRead(pModElf->Core.pReader, paPhdrs, cbPhdrs, pModElf->Ehdr.e_phoff);
    if (RT_FAILURE(rc))
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "%s: pfnRead(,,%#zx, " FMT_ELF_OFF ") -> %Rrc",
                                   pszLogName, cbPhdrs, pModElf->Ehdr.e_phoff, rc);

    /*
     * Validate them.
     */
    unsigned cbPage = _4K; /** @todo generalize architecture specific stuff using its own code template header.  */
    switch (pModElf->Core.enmArch)
    {
        case RTLDRARCH_AMD64:
        case RTLDRARCH_X86_32:
            break;
        default:
            AssertFailedBreak(/** @todo page size for got.plt hacks */);
    }
    unsigned iLoad          = 0;
    unsigned iLoadShdr      = 1; /* ASSUMES ordered (checked above). */
    unsigned cDynamic       = 0;
    Elf_Addr cbImage        = 0;
    Elf_Addr uLinkAddress   = ~(Elf_Addr)0;
    for (unsigned i = 0; i < pModElf->Ehdr.e_phnum; i++)
    {
        const Elf_Phdr * const pPhdr = &paPhdrs[i];
        Log3(("RTLdrELF: Program Header #%d:\n"
              "RTLdrELF:   p_type: " FMT_ELF_WORD " (%s)\n"
              "RTLdrELF:  p_flags: " FMT_ELF_WORD "\n"
              "RTLdrELF: p_offset: " FMT_ELF_OFF "\n"
              "RTLdrELF:  p_vaddr: " FMT_ELF_ADDR "\n"
              "RTLdrELF:  p_paddr: " FMT_ELF_ADDR "\n"
              "RTLdrELF: p_filesz: " FMT_ELF_XWORD "\n"
              "RTLdrELF:  p_memsz: " FMT_ELF_XWORD "\n"
              "RTLdrELF:  p_align: " FMT_ELF_XWORD "\n",
              i,
              pPhdr->p_type, rtldrElfGetPhdrType(pPhdr->p_type), pPhdr->p_flags, pPhdr->p_offset,
              pPhdr->p_vaddr, pPhdr->p_paddr, pPhdr->p_filesz, pPhdr->p_memsz, pPhdr->p_align));

        if (pPhdr->p_type == DT_NULL)
            continue;

        if (   pPhdr->p_filesz != 0
            && (   pPhdr->p_offset >= cbRawImage
                || pPhdr->p_filesz > cbRawImage
                || pPhdr->p_offset + pPhdr->p_filesz > cbRawImage))
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                       "%s: Prog Hdr #%u: bogus p_offset=" FMT_ELF_OFF " & p_filesz=" FMT_ELF_XWORD " (file size %#RX64)",
                                       pszLogName, i, pPhdr->p_offset, pPhdr->p_filesz, cbRawImage);

        if (pPhdr->p_flags & ~(Elf64_Word)(PF_X | PF_R | PF_W))
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Prog Hdr #%u: bogus p_flags=" FMT_ELF_WORD,
                                       pszLogName, i, pPhdr->p_flags);

        if (!RT_IS_POWER_OF_TWO(pPhdr->p_align))
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Prog Hdr #%u: bogus p_align=" FMT_ELF_XWORD,
                                       pszLogName, i, pPhdr->p_align);

        if (   pPhdr->p_align  > 1
            && pPhdr->p_memsz  > 0
            && pPhdr->p_filesz > 0
            && (pPhdr->p_offset & (pPhdr->p_align - 1)) != (pPhdr->p_vaddr & (pPhdr->p_align - 1)))
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                       "%s: Prog Hdr #%u: misaligned p_offset=" FMT_ELF_OFF " p_vaddr=" FMT_ELF_ADDR " p_align=" FMT_ELF_XWORD,
                                       pszLogName, i, pPhdr->p_offset, pPhdr->p_vaddr, pPhdr->p_align);

        /* Do some type specfic checks: */
        switch (pPhdr->p_type)
        {
            case PT_LOAD:
            {
                if (pPhdr->p_memsz < pPhdr->p_filesz)
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                               "%s: Prog Hdr #%u/LOAD#%u: bogus p_memsz=" FMT_ELF_XWORD " or p_filesz=" FMT_ELF_XWORD,
                                               pszLogName, i, iLoad, pPhdr->p_memsz, pPhdr->p_filesz);
                cbImage = pPhdr->p_vaddr + pPhdr->p_memsz;
                if (iLoad == 0)
                    uLinkAddress = pPhdr->p_vaddr;

                /* Find the corresponding sections, checking their addresses and
                   file offsets since the rest of the code is still section based
                   rather than using program headers as it should... */
                Elf_Off         off     = pPhdr->p_offset;
                Elf_Addr        uAddr   = pPhdr->p_vaddr;
                Elf_Xword       cbMem   = pPhdr->p_memsz;
                Elf_Xword       cbFile  = pPhdr->p_filesz;

                /* HACK to allow loading isolinux-debug.elf where program headers aren't
                   sorted by virtual address. */
                if (   (fFlags & RTLDR_O_FOR_DEBUG)
                    && uAddr != paShdrs[iLoadShdr].sh_addr)
                {
                    for (unsigned iShdr = 1; iShdr < pModElf->Ehdr.e_shnum; iShdr++)
                        if (uAddr == paShdrs[iShdr].sh_addr)
                        {
                            iLoadShdr = iShdr;
                            break;
                        }
                }

                while (cbMem > 0)
                {
                    if (iLoadShdr < pModElf->Ehdr.e_shnum)
                    { /* likely */ }
                    else if (iLoadShdr == pModElf->Ehdr.e_shnum)
                    {
                        /** @todo anything else to check here? */
                        iLoadShdr++;
                        break;
                    }
                    else
                        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                                   "%s: Prog Hdr #%u/LOAD#%u: Out of sections at " FMT_ELF_ADDR " LB " FMT_ELF_XWORD,
                                                   pszLogName, i, iLoad, uAddr, cbMem);
                    if (!(paShdrs[iLoadShdr].sh_flags & SHF_ALLOC))
                    {
                        if (   paShdrs[iLoadShdr].sh_type != SHT_NOBITS
                            && paShdrs[iLoadShdr].sh_size > 0
                            && off < paShdrs[iLoadShdr].sh_offset + paShdrs[iLoadShdr].sh_size
                            && paShdrs[iLoadShdr].sh_offset < off + cbMem)
                            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                                       "%s: Prog Hdr #%u/LOAD#%u: Overlaps with !SHF_ALLOC section at " FMT_ELF_OFF " LB " FMT_ELF_XWORD,
                                                       pszLogName, i, iLoad, paShdrs[iLoadShdr].sh_offset, paShdrs[iLoadShdr].sh_size);
                        pModElf->paShdrExtras[iLoadShdr].idxPhdr = UINT16_MAX;
                        iLoadShdr++;
                        continue;
                    }

                    if (uAddr != paShdrs[iLoadShdr].sh_addr)
                    {
                        /* Before the first section we expect headers to be loaded, so
                           that the file is simply mapped from file offset zero. */
                        if (   iLoadShdr == 1
                            && iLoad     == 0
                            && paShdrs[1].sh_addr == paShdrs[1].sh_offset
                            && cbFile    >= paShdrs[1].sh_offset
                            && cbMem     >= paShdrs[1].sh_offset)
                        {
                            /* Modify paShdrs[0] to describe the gap. ".elf.headers" */
                            pModElf->iFirstSect              = 0;
                            pModElf->paShdrs[0].sh_name      = 0;
                            pModElf->paShdrs[0].sh_type      = SHT_PROGBITS;
                            pModElf->paShdrs[0].sh_flags     = SHF_ALLOC
                                                             | (pPhdr->p_flags & PF_W ? SHF_WRITE     : 0)
                                                             | (pPhdr->p_flags & PF_X ? SHF_EXECINSTR : 0);
                            pModElf->paShdrs[0].sh_addr      = uAddr;
                            pModElf->paShdrs[0].sh_offset    = off;
                            pModElf->paShdrs[0].sh_size      = paShdrs[1].sh_offset;
                            pModElf->paShdrs[0].sh_link      = 0;
                            pModElf->paShdrs[0].sh_info      = 0;
                            pModElf->paShdrs[0].sh_addralign = pPhdr->p_align;
                            pModElf->paShdrs[0].sh_entsize   = 0;
                            *(Elf_Shdr *)pModElf->paOrgShdrs = pModElf->paShdrs[0]; /* (necessary for segment enumeration) */

                            uAddr  += paShdrs[1].sh_offset;
                            cbMem  -= paShdrs[1].sh_offset;
                            cbFile -= paShdrs[1].sh_offset;
                            off     = paShdrs[1].sh_offset;
                        }
                        /* Alignment padding?  Allow up to a page size. */
                        else if (   paShdrs[iLoadShdr].sh_addr > uAddr
                                 &&   paShdrs[iLoadShdr].sh_addr - uAddr
                                    < RT_MAX(paShdrs[iLoadShdr].sh_addralign, cbPage /*got.plt hack*/))
                        {
                            Elf_Xword cbAlignPadding = paShdrs[iLoadShdr].sh_addr - uAddr;
                            if (cbAlignPadding >= cbMem)
                                break;
                            cbMem -= cbAlignPadding;
                            uAddr += cbAlignPadding;
                            if (cbFile > cbAlignPadding)
                            {
                                off    += cbAlignPadding;
                                cbFile -= cbAlignPadding;
                            }
                            else
                            {
                                off   += cbFile;
                                cbFile = 0;
                            }
                        }
                    }

                    if (   uAddr == paShdrs[iLoadShdr].sh_addr
                        && cbMem >= paShdrs[iLoadShdr].sh_size
                        && (  paShdrs[iLoadShdr].sh_type != SHT_NOBITS
                            ?    off    == paShdrs[iLoadShdr].sh_offset
                              && cbFile >= paShdrs[iLoadShdr].sh_size /* this might be too strict... */
                            :    cbFile == 0
                              || cbMem > paShdrs[iLoadShdr].sh_size /* isolinux.elf: linker merge no-bits and progbits sections */) )
                    {
                        if (   paShdrs[iLoadShdr].sh_type != SHT_NOBITS
                            || cbFile != 0)
                        {
                            off    += paShdrs[iLoadShdr].sh_size;
                            cbFile -= paShdrs[iLoadShdr].sh_size;
                        }
                        uAddr += paShdrs[iLoadShdr].sh_size;
                        cbMem -= paShdrs[iLoadShdr].sh_size;
                    }
                    else
                        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                                   "%s: Prog Hdr #%u/LOAD#%u: Mismatch at " FMT_ELF_ADDR " LB " FMT_ELF_XWORD " (file " FMT_ELF_OFF " LB " FMT_ELF_XWORD ") with section #%u " FMT_ELF_ADDR " LB " FMT_ELF_XWORD " (file " FMT_ELF_OFF " sh_type=" FMT_ELF_WORD ")",
                                                   pszLogName, i, iLoad, uAddr, cbMem, off, cbFile,
                                                   iLoadShdr, paShdrs[iLoadShdr].sh_addr, paShdrs[iLoadShdr].sh_size,
                                                   paShdrs[iLoadShdr].sh_offset, paShdrs[iLoadShdr].sh_type);

                    pModElf->paShdrExtras[iLoadShdr].idxPhdr = iLoad;
                    iLoadShdr++;
                } /* section loop */

                iLoad++;
                break;
            }

            case PT_DYNAMIC:
            {
                const Elf_Shdr *pShdr = &pModElf->paShdrs[pModElf->iShDynamic];
                if (pPhdr->p_offset != pShdr->sh_offset)
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                               "%s: Prog Hdr #%u/DYNAMIC: p_offset=" FMT_ELF_OFF " expected " FMT_ELF_OFF,
                                               pszLogName, i, pPhdr->p_offset, pShdr->sh_offset);
                if (RT_MAX(pPhdr->p_memsz, pPhdr->p_filesz) != pShdr->sh_size)
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                               "%s: Prog Hdr #%u/DYNAMIC: expected " FMT_ELF_XWORD " for RT_MAX(p_memsz=" FMT_ELF_XWORD ", p_filesz=" FMT_ELF_XWORD ")",
                                               pszLogName, i, pShdr->sh_size, pPhdr->p_memsz, pPhdr->p_filesz);
                cDynamic++;
                break;
            }
        }
    }

    if (iLoad == 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "%s: No PT_LOAD program headers", pszLogName);
    if (cDynamic != 1)
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "%s: No program header for the DYNAMIC section", pszLogName);

    cbImage -= uLinkAddress;
    pModElf->cbImage     = (uint64_t)cbImage;
    pModElf->LinkAddress = uLinkAddress;
    AssertReturn(pModElf->cbImage == cbImage, VERR_INTERNAL_ERROR_5);
    Log3(("RTLdrELF: LinkAddress=" FMT_ELF_ADDR " cbImage=" FMT_ELF_ADDR " (from PT_LOAD)\n", uLinkAddress, cbImage));

    for (; iLoadShdr < pModElf->Ehdr.e_shnum; iLoadShdr++)
        if (   !(paShdrs[iLoadShdr].sh_flags & SHF_ALLOC)
            || paShdrs[iLoadShdr].sh_size == 0)
            pModElf->paShdrExtras[iLoadShdr].idxPhdr = UINT16_MAX;
        else
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                       "%s: No PT_LOAD for section #%u " FMT_ELF_ADDR " LB " FMT_ELF_XWORD " (file " FMT_ELF_OFF " sh_type=" FMT_ELF_WORD ")",
                                       pszLogName, iLoadShdr, paShdrs[iLoadShdr].sh_addr, paShdrs[iLoadShdr].sh_size,
                                       paShdrs[iLoadShdr].sh_offset, paShdrs[iLoadShdr].sh_type);

    /*
     * Load and validate the dynamic table.  We have got / will get most of the
     * info we need from the section table, so we must make sure this matches up.
     */
    Log3(("RTLdrELF: Dynamic section - %u entries\n", pModElf->cDynamic));
    size_t const    cbDynamic = pModElf->cDynamic * sizeof(pModElf->paDynamic[0]);
    Elf_Dyn * const paDynamic = (Elf_Dyn *)RTMemAlloc(cbDynamic);
    AssertReturn(paDynamic, VERR_NO_MEMORY);
    pModElf->paDynamic = paDynamic;

    rc = pModElf->Core.pReader->pfnRead(pModElf->Core.pReader, paDynamic, cbDynamic, paShdrs[pModElf->iShDynamic].sh_offset);
    if (RT_FAILURE(rc))
        return RTERRINFO_LOG_SET_F(pErrInfo, rc, "%s: pfnRead(,,%#zx, " FMT_ELF_OFF ") -> %Rrc",
                                   pszLogName, cbDynamic, paShdrs[pModElf->iShDynamic].sh_offset, rc);

    for (uint32_t i = 0; i < pModElf->cDynamic; i++)
    {
#define LOG_VALIDATE_PTR_RET(szName) do { \
            Log3(("RTLdrELF: DT[%u]: %16s " FMT_ELF_ADDR "\n", i, szName, paDynamic[i].d_un.d_ptr)); \
            if ((uint64_t)paDynamic[i].d_un.d_ptr - uLinkAddress < cbImage) { /* likely */ } \
            else return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/" szName ": Invalid address " FMT_ELF_ADDR " (valid range: " FMT_ELF_ADDR " LB " FMT_ELF_ADDR ")", \
                                            pszLogName, i, paDynamic[i].d_un.d_ptr, uLinkAddress, cbImage); \
        } while (0)
#define LOG_VALIDATE_PTR_VAL_RET(szName, uExpected) do { \
            Log3(("RTLdrELF: DT[%u]: %16s " FMT_ELF_ADDR "\n", i, szName, (uint64_t)paDynamic[i].d_un.d_ptr)); \
            if (paDynamic[i].d_un.d_ptr == (Elf_Addr)(uExpected)) { /* likely */ } \
            else return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/" szName ": " FMT_ELF_ADDR ", expected " FMT_ELF_ADDR, \
                                            pszLogName, i, paDynamic[i].d_un.d_ptr, (Elf_Addr)(uExpected)); \
        } while (0)
#define LOG_VALIDATE_STR_RET(szName) do { \
            Log3(("RTLdrELF: DT[%u]: %16s %#RX64\n", i, szName, (uint64_t)paDynamic[i].d_un.d_val)); \
            if ((uint64_t)paDynamic[i].d_un.d_val < pModElf->Dyn.cbStr) { /* likely */ } \
            else return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/" szName ": Invalid string table offset %#RX64 (max %#x)", \
                                            pszLogName, i, (uint64_t)paDynamic[i].d_un.d_val, pModElf->Dyn.cbStr); \
        } while (0)
#define LOG_VALIDATE_VAL_RET(szName, uExpected) do { \
            Log3(("RTLdrELF: DT[%u]: %16s %#RX64\n", i, szName, (uint64_t)paDynamic[i].d_un.d_val)); \
            if ((uint64_t)paDynamic[i].d_un.d_val == (uint64_t)(uExpected)) { /* likely */ } \
            else return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/" szName ": %#RX64, expected %#RX64", \
                                            pszLogName, i, (uint64_t)paDynamic[i].d_un.d_val, (uint64_t)(uExpected)); \
        } while (0)
#define SET_RELOC_TYPE_RET(a_szName, a_uType) do { \
            if (pModElf->DynInfo.uRelocType == 0 || pModElf->DynInfo.uRelocType == (a_uType)) \
                pModElf->DynInfo.uRelocType = (a_uType); \
            else return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/" a_szName ": Mixing DT_RELA and DT_REL", pszLogName, i); \
        } while (0)
#define SET_INFO_FIELD_RET(a_szName, a_Field, a_Value, a_UnsetValue, a_szFmt) do { \
            if ((a_Field) == (a_UnsetValue) && (a_Value) != (a_UnsetValue)) \
                (a_Field) = (a_Value); /* likely */ \
            else if ((a_Field) != (a_UnsetValue)) \
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/" a_szName ": Multiple entries (first value " a_szFmt ", second " a_szFmt ")", pszLogName, i, (a_Field), (a_Value)); \
            else if ((a_Value) != (a_UnsetValue)) \
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/" a_szName ": Unexpected value " a_szFmt, pszLogName, i, (a_Value)); \
        } while (0)
#define FIND_MATCHING_SECTION_RET(a_szName, a_ExtraMatchExpr, a_idxShFieldToSet) do { \
            unsigned iSh; \
            for (iSh = 1; iSh < pModElf->Ehdr.e_shnum; iSh++) \
                if (   paShdrs[iSh].sh_addr == paDynamic[i].d_un.d_ptr \
                    && (a_ExtraMatchExpr)) \
                { \
                    (a_idxShFieldToSet) = iSh; \
                    if (pModElf->paShdrExtras[iSh].idxDt != UINT16_MAX) \
                        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, \
                                                   "%s: DT[%u]/" a_szName ": section #%u (" FMT_ELF_ADDR ") already referenced by DT[%u]", \
                                                   pszLogName, i, iSh, paShdrs[iSh].sh_addr, pModElf->paShdrExtras[iSh].idxDt); \
                    pModElf->paShdrExtras[iSh].idxDt  = i; \
                    pModElf->paShdrExtras[iSh].uDtTag = (uint32_t)paDynamic[i].d_tag; \
                    break; \
                } \
            if (iSh < pModElf->Ehdr.e_shnum) { /* likely */ } \
            else return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/" a_szName ": No matching section for " FMT_ELF_ADDR, pszLogName, i, paDynamic[i].d_un.d_ptr); \
        } while (0)
#define ONLY_FOR_DEBUG_OR_VALIDATION_RET(a_szName) do { \
            if (fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION)) { /* likely */ } \
            else return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/" a_szName ": Not supported (" FMT_ELF_ADDR ")", pszLogName, i, paDynamic[i].d_un.d_ptr); \
        } while (0)
#define LOG_NON_VALUE_ENTRY(a_szName) Log3(("RTLdrELF: DT[%u]: %16s (%#RX64)\n", i, a_szName, (uint64_t)paDynamic[i].d_un.d_val))

        switch (paDynamic[i].d_tag)
        {
            case DT_NULL:
                LOG_NON_VALUE_ENTRY("DT_NULL");
                for (unsigned iNull = i + 1; iNull < pModElf->cDynamic; iNull++)
                    if (paDynamic[i].d_tag == DT_NULL) /* Not technically a bug, but let's try being extremely strict for now */
                        LOG_NON_VALUE_ENTRY("DT_NULL");
                    else if (!(fFlags & (RTLDR_O_FOR_DEBUG | RTLDR_O_FOR_VALIDATION)))
                        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                                   "%s: DT[%u]/DT_NULL: Dynamic section isn't zero padded (extra #%u of #%u)",
                                                   pszLogName, i, iNull - i, pModElf->cDynamic - i);
                i = pModElf->cDynamic;
                break;
            case DT_NEEDED:
                LOG_VALIDATE_STR_RET("DT_NEEDED");
                break;
            case DT_PLTRELSZ:
                Log3(("RTLdrELF: DT[%u]: %16s %#RX64 bytes\n", i, "DT_PLTRELSZ", (uint64_t)paDynamic[i].d_un.d_val));
                SET_INFO_FIELD_RET("DT_PLTRELSZ", pModElf->DynInfo.cbJmpRelocs, (Elf_Xword)paDynamic[i].d_un.d_val, 0, FMT_ELF_XWORD);
                break;
            case DT_PLTGOT:
                LOG_VALIDATE_PTR_RET("DT_PLTGOT");
                break;
            case DT_HASH:
                LOG_VALIDATE_PTR_RET("DT_HASH");
                break;
            case DT_STRTAB:
                LOG_VALIDATE_PTR_VAL_RET("DT_STRTAB", paShdrs[pModElf->Dyn.iStrSh].sh_addr);
                pModElf->paShdrExtras[pModElf->Dyn.iStrSh].idxDt  = i;
                pModElf->paShdrExtras[pModElf->Dyn.iSymSh].uDtTag = DT_STRTAB;
                break;
            case DT_SYMTAB:
                LOG_VALIDATE_PTR_VAL_RET("DT_SYMTAB", paShdrs[pModElf->Dyn.iSymSh].sh_addr);
                pModElf->paShdrExtras[pModElf->Dyn.iSymSh].idxDt  = i;
                pModElf->paShdrExtras[pModElf->Dyn.iSymSh].uDtTag = DT_SYMTAB;
                break;
            case DT_RELA:
                LOG_VALIDATE_PTR_RET("DT_RELA");
                SET_RELOC_TYPE_RET("DT_RELA", DT_RELA);
                SET_INFO_FIELD_RET("DT_RELA", pModElf->DynInfo.uPtrRelocs, paDynamic[i].d_un.d_ptr, ~(Elf_Addr)0, FMT_ELF_ADDR);
                FIND_MATCHING_SECTION_RET("DT_RELA", paShdrs[iSh].sh_type == SHT_RELA, pModElf->DynInfo.idxShRelocs);
                break;
            case DT_RELASZ:
                Log3(("RTLdrELF: DT[%u]: %16s %#RX64 bytes\n", i, "DT_RELASZ", (uint64_t)paDynamic[i].d_un.d_val));
                SET_RELOC_TYPE_RET("DT_RELASZ", DT_RELA);
                SET_INFO_FIELD_RET("DT_RELASZ", pModElf->DynInfo.cbRelocs, (Elf_Xword)paDynamic[i].d_un.d_val, 0, FMT_ELF_XWORD);
                break;
            case DT_RELAENT:
                LOG_VALIDATE_VAL_RET("DT_RELAENT", sizeof(Elf_Rela));
                SET_RELOC_TYPE_RET("DT_RELAENT", DT_RELA);
                SET_INFO_FIELD_RET("DT_RELAENT", pModElf->DynInfo.cbRelocEntry, (unsigned)sizeof(Elf_Rela), 0, "%u");
                break;
            case DT_STRSZ:
                LOG_VALIDATE_VAL_RET("DT_STRSZ", pModElf->Dyn.cbStr);
                break;
            case DT_SYMENT:
                LOG_VALIDATE_VAL_RET("DT_SYMENT", sizeof(Elf_Sym));
                break;
            case DT_INIT:
                LOG_VALIDATE_PTR_RET("DT_INIT");
                ONLY_FOR_DEBUG_OR_VALIDATION_RET("DT_INIT");
                break;
            case DT_FINI:
                LOG_VALIDATE_PTR_RET("DT_FINI");
                ONLY_FOR_DEBUG_OR_VALIDATION_RET("DT_FINI");
                break;
            case DT_SONAME:
                LOG_VALIDATE_STR_RET("DT_SONAME");
                break;
            case DT_RPATH:
                LOG_VALIDATE_STR_RET("DT_RPATH");
                break;
            case DT_SYMBOLIC:
                LOG_NON_VALUE_ENTRY("DT_SYMBOLIC");
                break;
            case DT_REL:
                LOG_VALIDATE_PTR_RET("DT_REL");
                SET_RELOC_TYPE_RET("DT_REL", DT_REL);
                SET_INFO_FIELD_RET("DT_REL", pModElf->DynInfo.uPtrRelocs, paDynamic[i].d_un.d_ptr, ~(Elf_Addr)0, FMT_ELF_ADDR);
                FIND_MATCHING_SECTION_RET("DT_REL", paShdrs[iSh].sh_type == SHT_REL, pModElf->DynInfo.idxShRelocs);
                break;
            case DT_RELSZ:
                Log3(("RTLdrELF: DT[%u]: %16s %#RX64 bytes\n", i, "DT_RELSZ", (uint64_t)paDynamic[i].d_un.d_val));
                SET_RELOC_TYPE_RET("DT_RELSZ", DT_REL);
                SET_INFO_FIELD_RET("DT_RELSZ", pModElf->DynInfo.cbRelocs, (Elf_Xword)paDynamic[i].d_un.d_val, 0, FMT_ELF_XWORD);
                break;
            case DT_RELENT:
                LOG_VALIDATE_VAL_RET("DT_RELENT", sizeof(Elf_Rel));
                SET_RELOC_TYPE_RET("DT_RELENT", DT_REL);
                SET_INFO_FIELD_RET("DT_RELENT", pModElf->DynInfo.cbRelocEntry, (unsigned)sizeof(Elf_Rel), 0, "%u");
                break;
            case DT_PLTREL:
                if (paDynamic[i].d_un.d_val != DT_RELA && paDynamic[i].d_un.d_val != DT_REL)
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT[%u]/DT_PLTREL: Invalid value %#RX64",
                                               pszLogName, i, (uint64_t)paDynamic[i].d_un.d_val);
                Log3(("RTLdrELF: DT[%u]: %16s DT_REL%s\n", i, "DT_PLTREL", paDynamic[i].d_un.d_val == DT_RELA ? "A" : ""));
                SET_INFO_FIELD_RET("DT_PLTREL", pModElf->DynInfo.uJmpRelocType, (unsigned)paDynamic[i].d_un.d_val, 0, "%u");
                break;
            case DT_DEBUG:
                /*
                 * DT_DEBUG is filled in by the dynamic linker to point a debugger to the head of the link map,
                 * it can point anywhere in userspace. For binaries not being executed it will be 0,
                 * so there is nothing we can validate here (and it is not required as we don't use
                 * this dynamic section). See https://ypl.coffee/dl-resolve-full-relro/ for more information.
                 */
                break;
            case DT_TEXTREL:
                LOG_NON_VALUE_ENTRY("DT_TEXTREL");
                break;
            case DT_JMPREL:
                LOG_VALIDATE_PTR_RET("DT_JMPREL");
                SET_INFO_FIELD_RET("DT_JMPREL", pModElf->DynInfo.uPtrJmpRelocs, paDynamic[i].d_un.d_ptr, ~(Elf_Addr)0, FMT_ELF_ADDR);
                FIND_MATCHING_SECTION_RET("DT_JMPREL", 1, pModElf->DynInfo.idxShJmpRelocs);
                break;
            case DT_BIND_NOW:
                LOG_NON_VALUE_ENTRY("DT_BIND_NOW");
                break;
            case DT_INIT_ARRAY:
                LOG_VALIDATE_PTR_RET("DT_INIT_ARRAY");
                ONLY_FOR_DEBUG_OR_VALIDATION_RET("DT_INIT_ARRAY");
                break;
            case DT_FINI_ARRAY:
                LOG_VALIDATE_PTR_RET("DT_FINI_ARRAY");
                ONLY_FOR_DEBUG_OR_VALIDATION_RET("DT_FINI_ARRAY");
                break;
            case DT_INIT_ARRAYSZ:
                Log3(("RTLdrELF: DT[%u]: %16s %#RX64 bytes\n", i, "DT_INIT_ARRAYSZ", (uint64_t)paDynamic[i].d_un.d_val));
                ONLY_FOR_DEBUG_OR_VALIDATION_RET("DT_INIT_ARRAYSZ");
                break;
            case DT_FINI_ARRAYSZ:
                Log3(("RTLdrELF: DT[%u]: %16s %#RX64 bytes\n", i, "DT_FINI_ARRAYSZ", (uint64_t)paDynamic[i].d_un.d_val));
                ONLY_FOR_DEBUG_OR_VALIDATION_RET("DT_FINI_ARRAYSZ");
                break;
            case DT_RUNPATH:
                LOG_VALIDATE_STR_RET("DT_RUNPATH");
                break;
            case DT_FLAGS:
                Log3(("RTLdrELF: DT[%u]: %16s %#RX64\n", i, "DT_FLAGS", (uint64_t)paDynamic[i].d_un.d_val));
                break;
            case DT_PREINIT_ARRAY:
                LOG_VALIDATE_PTR_RET("DT_PREINIT_ARRAY");
                ONLY_FOR_DEBUG_OR_VALIDATION_RET("DT_PREINIT_ARRAY");
                break;
            case DT_PREINIT_ARRAYSZ:
                Log3(("RTLdrELF: DT[%u]: %16s %#RX64 bytes\n", i, "DT_PREINIT_ARRAYSZ", (uint64_t)paDynamic[i].d_un.d_val));
                ONLY_FOR_DEBUG_OR_VALIDATION_RET("DT_PREINIT_ARRAYSZ");
                break;
            default:
                if (   paDynamic[i].d_tag <  DT_ENCODING
                    || paDynamic[i].d_tag >= DT_LOOS
                    || (paDynamic[i].d_tag & 1))
                    Log3(("RTLdrELF: DT[%u]: %#010RX64       %#RX64%s\n", i, (uint64_t)paDynamic[i].d_tag,
                          (uint64_t)paDynamic[i].d_un.d_val, paDynamic[i].d_un.d_val >= DT_ENCODING ? " (val)" : ""));
                else
                {
                    Log3(("RTLdrELF: DT[%u]: %#010RX64       " FMT_ELF_ADDR " (addr)\n",
                          i, (uint64_t)paDynamic[i].d_tag, paDynamic[i].d_un.d_ptr));
                    if ((uint64_t)paDynamic[i].d_un.d_ptr - uLinkAddress >= cbImage)
                        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                                   "%s: DT[%u]/%#RX64: Invalid address " FMT_ELF_ADDR " (valid range: " FMT_ELF_ADDR " LB " FMT_ELF_ADDR ")",
                                                   pszLogName, i, (uint64_t)paDynamic[i].d_tag,
                                                   paDynamic[i].d_un.d_ptr, uLinkAddress, cbImage);
                }
                break;
        }
#undef LOG_VALIDATE_VAL_RET
#undef LOG_VALIDATE_STR_RET
#undef LOG_VALIDATE_PTR_VAL_RET
#undef LOG_VALIDATE_PTR_RET
#undef SET_RELOC_TYPE_RET
#undef SET_INFO_FIELD_RET
#undef FIND_MATCHING_SECTION_RET
#undef ONLY_FOR_DEBUG_OR_VALIDATION_RET
    }

    /*
     * Validate the relocation information we've gathered.
     */
    Elf_Word uShTypeArch = SHT_RELA; /** @todo generalize architecture specific stuff using its own code template header.  */
    switch (pModElf->Core.enmArch)
    {
        case RTLDRARCH_AMD64:
            break;
        case RTLDRARCH_X86_32:
            uShTypeArch = SHT_REL;
            break;
        default:
            AssertFailedBreak(/** @todo page size for got.plt hacks */);

    }

    if (pModElf->DynInfo.uRelocType != 0)
    {
        const char * const pszModifier = pModElf->DynInfo.uRelocType == DT_RELA ? "A" : "";
        if (pModElf->DynInfo.uPtrRelocs == ~(Elf_Addr)0)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Missing DT_REL%s", pszLogName, pszModifier);
        if (pModElf->DynInfo.cbRelocs == 0)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Missing DT_REL%sSZ", pszLogName, pszModifier);
        if (pModElf->DynInfo.cbRelocEntry == 0)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Missing DT_REL%sENT", pszLogName, pszModifier);
        Elf_Shdr const *pShdrRelocs = &paShdrs[pModElf->DynInfo.idxShRelocs];
        Elf_Word const  uShType     = pModElf->DynInfo.uJmpRelocType == DT_RELA ? SHT_RELA : SHT_REL;
        if (pShdrRelocs->sh_type != uShType)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT_REL%s* does not match section type: %u vs %u",
                                       pszLogName, pszModifier, pShdrRelocs->sh_type, uShType);
        if (pShdrRelocs->sh_size != pModElf->DynInfo.cbRelocs)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT_REL%sSZ does not match section size: %u vs %u",
                                       pszLogName, pszModifier, pShdrRelocs->sh_size, pModElf->DynInfo.cbRelocs);
        if (uShType != uShTypeArch)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT_REL%s* does not match architecture: %u, arch wants %u",
                                       pszLogName, pszModifier, uShType, uShTypeArch);
    }

    if (   pModElf->DynInfo.uPtrJmpRelocs != ~(Elf_Addr)0
        || pModElf->DynInfo.cbJmpRelocs   != 0
        || pModElf->DynInfo.uJmpRelocType != 0)
    {
        if (pModElf->DynInfo.uPtrJmpRelocs == ~(Elf_Addr)0)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Missing DT_JMPREL", pszLogName);
        if (pModElf->DynInfo.cbJmpRelocs == 0)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Missing DT_PLTRELSZ", pszLogName);
        if (pModElf->DynInfo.uJmpRelocType == 0)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: Missing DT_PLTREL", pszLogName);
        Elf_Shdr const *pShdrRelocs = &paShdrs[pModElf->DynInfo.idxShJmpRelocs];
        Elf_Word const  uShType     = pModElf->DynInfo.uJmpRelocType == DT_RELA ? SHT_RELA : SHT_REL;
        if (pShdrRelocs->sh_type != uShType)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT_PLTREL does not match section type: %u vs %u",
                                       pszLogName, pShdrRelocs->sh_type, uShType);
        if (pShdrRelocs->sh_size != pModElf->DynInfo.cbJmpRelocs)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT_PLTRELSZ does not match section size: %u vs %u",
                                       pszLogName, pShdrRelocs->sh_size, pModElf->DynInfo.cbJmpRelocs);
        if (uShType != uShTypeArch)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT, "%s: DT_PLTREL does not match architecture: %u, arch wants %u",
                                       pszLogName, uShType, uShTypeArch);
    }

    /*
     * Check that there aren't any other relocations hiding in the section table.
     */
    for (uint32_t i = 1; i < pModElf->Ehdr.e_shnum; i++)
        if (   (paShdrs[i].sh_type == SHT_REL || paShdrs[i].sh_type == SHT_RELA)
            && pModElf->paShdrExtras[i].uDtTag != DT_REL
            && pModElf->paShdrExtras[i].uDtTag != DT_RELA
            && pModElf->paShdrExtras[i].uDtTag != DT_JMPREL)
        {
            char szSecHdrNm[80];
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_BAD_EXE_FORMAT,
                                       "%s: section header #%u (%s type=" FMT_ELF_WORD " size=" FMT_ELF_XWORD ") contains relocations not referenced by the dynamic section",
                                       pszLogName, i,
                                       RTLDRELF_NAME(GetSHdrName)(pModElf, paShdrs[i].sh_name, szSecHdrNm, sizeof(szSecHdrNm)),
                                       paShdrs[i].sh_type, paShdrs[i].sh_size);
        }

    return VINF_SUCCESS;
}



/**
 * Opens an ELF image, fixed bitness.
 *
 * @returns iprt status code.
 * @param   pReader     The loader reader instance which will provide the raw image bits.
 * @param   fFlags      Reserved, MBZ.
 * @param   enmArch     Architecture specifier.
 * @param   phLdrMod    Where to store the handle.
 * @param   pErrInfo    Where to return extended error info. Optional.
 */
static int RTLDRELF_NAME(Open)(PRTLDRREADER pReader, uint32_t fFlags, RTLDRARCH enmArch, PRTLDRMOD phLdrMod, PRTERRINFO pErrInfo)
{
    const char *pszLogName = pReader->pfnLogName(pReader);
    uint64_t    cbRawImage = pReader->pfnSize(pReader);
    RT_NOREF_PV(fFlags);

    /*
     * Create the loader module instance.
     */
    PRTLDRMODELF pModElf = (PRTLDRMODELF)RTMemAllocZ(sizeof(*pModElf));
    if (!pModElf)
        return VERR_NO_MEMORY;

    pModElf->Core.u32Magic  = RTLDRMOD_MAGIC;
    pModElf->Core.eState    = LDR_STATE_INVALID;
    pModElf->Core.pReader   = pReader;
    pModElf->Core.enmFormat = RTLDRFMT_ELF;
    pModElf->Core.enmType   = RTLDRTYPE_OBJECT;
    pModElf->Core.enmEndian = RTLDRENDIAN_LITTLE;
#if ELF_MODE == 32
    pModElf->Core.enmArch   = RTLDRARCH_X86_32;
#else
    pModElf->Core.enmArch   = RTLDRARCH_AMD64;
#endif
    //pModElf->pvBits       = NULL;
    //pModElf->Ehdr         = {0};
    //pModElf->paShdrs      = NULL;
    //pModElf->Rel.paSyms   = NULL;
    pModElf->Rel.iSymSh     = ~0U;
    //pModElf->Rel.cSyms    = 0;
    pModElf->Rel.iStrSh     = ~0U;
    //pModElf->Rel.cbStr    = 0;
    //pModElf->Rel.pStr     = NULL;
    //pModElf->Dyn.paSyms   = NULL;
    pModElf->Dyn.iSymSh     = ~0U;
    //pModElf->Dyn.cSyms    = 0;
    pModElf->Dyn.iStrSh     = ~0U;
    //pModElf->Dyn.cbStr    = 0;
    //pModElf->Dyn.pStr     = NULL;
    pModElf->iFirstSect     = 1;
    //pModElf->fShdrInOrder = false;
    //pModElf->cbImage      = 0;
    pModElf->LinkAddress    = ~(Elf_Addr)0;
    //pModElf->cbShStr      = 0;
    //pModElf->pShStr       = NULL;
    //pModElf->iShEhFrame   = 0;
    //pModElf->iShEhFrameHdr= 0;
    pModElf->iShDynamic     = ~0U;
    //pModElf->cDynamic     = 0;
    //pModElf->paDynamic    = NULL;
    //pModElf->paPhdrs      = NULL;
    pModElf->DynInfo.uPtrRelocs         = ~(Elf_Addr)0;
    //pModElf->DynInfo.cbRelocs         = 0;
    //pModElf->DynInfo.cbRelocEntry     = 0;
    //pModElf->DynInfo.uRelocType       = 0;
    //pModElf->DynInfo.idxShRelocs      = 0;
    pModElf->DynInfo.uPtrJmpRelocs      = ~(Elf_Addr)0;
    //pModElf->DynInfo.cbJmpRelocs      = 0;
    //pModElf->DynInfo.uJmpRelocType    = 0;
    //pModElf->DynInfo.idxShJmpRelocs   = 0;

    /*
     * Read and validate the ELF header and match up the CPU architecture.
     */
    int rc = pReader->pfnRead(pReader, &pModElf->Ehdr, sizeof(pModElf->Ehdr), 0);
    if (RT_SUCCESS(rc))
    {
        RTLDRARCH enmArchImage = RTLDRARCH_INVALID; /* shut up gcc */
        rc = RTLDRELF_NAME(ValidateElfHeader)(&pModElf->Ehdr, cbRawImage, pszLogName, &enmArchImage, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            if (    enmArch != RTLDRARCH_WHATEVER
                &&  enmArch != enmArchImage)
                rc = VERR_LDR_ARCH_MISMATCH;
        }
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Read the section headers, keeping a prestine copy for the module
         * introspection methods.
         */
        size_t const cbShdrs = pModElf->Ehdr.e_shnum * sizeof(Elf_Shdr);
        Elf_Shdr *paShdrs = (Elf_Shdr *)RTMemAlloc(cbShdrs * 2 + sizeof(RTLDRMODELFSHX) * pModElf->Ehdr.e_shnum);
        if (paShdrs)
        {
            pModElf->paShdrs = paShdrs;
            rc = pReader->pfnRead(pReader, paShdrs, cbShdrs, pModElf->Ehdr.e_shoff);
            if (RT_SUCCESS(rc))
            {
                memcpy(&paShdrs[pModElf->Ehdr.e_shnum], paShdrs, cbShdrs);
                pModElf->paOrgShdrs = &paShdrs[pModElf->Ehdr.e_shnum];

                pModElf->paShdrExtras = (PRTLDRMODELFSHX)&pModElf->paOrgShdrs[pModElf->Ehdr.e_shnum];
                memset(pModElf->paShdrExtras, 0xff, sizeof(RTLDRMODELFSHX) * pModElf->Ehdr.e_shnum);

                pModElf->cbShStr = paShdrs[pModElf->Ehdr.e_shstrndx].sh_size;

                /*
                 * Validate the section headers and find relevant sections.
                 */
                rc = RTLDRELF_NAME(ValidateAndProcessSectionHeaders)(pModElf, paShdrs, cbRawImage, pszLogName, pErrInfo);

                /*
                 * Read validate and process program headers if ET_DYN or ET_EXEC.
                 */
                if (RT_SUCCESS(rc) && (pModElf->Ehdr.e_type == ET_DYN || pModElf->Ehdr.e_type == ET_EXEC))
                    rc = RTLDRELF_NAME(ValidateAndProcessDynamicInfo)(pModElf, cbRawImage, fFlags, pszLogName, pErrInfo);

                /*
                 * Massage the section headers.
                 */
                if (RT_SUCCESS(rc))
                {
                    if (pModElf->Ehdr.e_type == ET_REL)
                    {
                        /* Do allocations and figure the image size: */
                        pModElf->LinkAddress = 0;
                        for (unsigned i = 1; i < pModElf->Ehdr.e_shnum; i++)
                            if (paShdrs[i].sh_flags & SHF_ALLOC)
                            {
                                paShdrs[i].sh_addr = paShdrs[i].sh_addralign
                                                   ? RT_ALIGN_T(pModElf->cbImage, paShdrs[i].sh_addralign, Elf_Addr)
                                                   : (Elf_Addr)pModElf->cbImage;
                                Elf_Addr EndAddr = paShdrs[i].sh_addr + paShdrs[i].sh_size;
                                if (pModElf->cbImage < EndAddr)
                                {
                                    pModElf->cbImage = (size_t)EndAddr;
                                    AssertMsgBreakStmt(pModElf->cbImage == EndAddr, (FMT_ELF_ADDR "\n", EndAddr), rc = VERR_IMAGE_TOO_BIG);
                                }
                                Log2(("RTLdrElf: %s: Assigned " FMT_ELF_ADDR " to section #%d\n", pszLogName, paShdrs[i].sh_addr, i));
                            }
                    }
                    else
                    {
                        /* Convert sh_addr to RVA: */
                        Assert(pModElf->LinkAddress != ~(Elf_Addr)0);
                        for (unsigned i = 0 /*!*/; i < pModElf->Ehdr.e_shnum; i++)
                            if (paShdrs[i].sh_flags & SHF_ALLOC)
                                    paShdrs[i].sh_addr -= pModElf->LinkAddress;
                    }
                }

                /*
                 * Check if the sections are in order by address, as that will simplify
                 * enumeration and address translation.
                 */
                pModElf->fShdrInOrder = true;
                Elf_Addr uEndAddr = 0;
                for (unsigned i = pModElf->iFirstSect; i < pModElf->Ehdr.e_shnum; i++)
                    if (paShdrs[i].sh_flags & SHF_ALLOC)
                    {
                        if (uEndAddr <= paShdrs[i].sh_addr)
                            uEndAddr = paShdrs[i].sh_addr + paShdrs[i].sh_size;
                        else
                        {
                            pModElf->fShdrInOrder = false;
                            break;
                        }
                    }

                Log2(("RTLdrElf: iSymSh=%u cSyms=%u iStrSh=%u cbStr=%u rc=%Rrc cbImage=%#zx LinkAddress=" FMT_ELF_ADDR " fShdrInOrder=%RTbool\n",
                      pModElf->Rel.iSymSh, pModElf->Rel.cSyms, pModElf->Rel.iStrSh, pModElf->Rel.cbStr, rc,
                      pModElf->cbImage, pModElf->LinkAddress, pModElf->fShdrInOrder));
                if (RT_SUCCESS(rc))
                {
                    pModElf->Core.pOps      = &RTLDRELF_MID(s_rtldrElf,Ops);
                    pModElf->Core.eState    = LDR_STATE_OPENED;
                    *phLdrMod = &pModElf->Core;

                    LogFlow(("%s: %s: returns VINF_SUCCESS *phLdrMod=%p\n", __FUNCTION__, pszLogName, *phLdrMod));
                    return VINF_SUCCESS;
                }
            }

            RTMemFree(paShdrs);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    RTMemFree(pModElf);
    LogFlow(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}




/*******************************************************************************
*   Cleanup Constants And Macros                                               *
*******************************************************************************/
#undef RTLDRELF_NAME
#undef RTLDRELF_SUFF
#undef RTLDRELF_MID

#undef FMT_ELF_ADDR
#undef FMT_ELF_ADDR7
#undef FMT_ELF_HALF
#undef FMT_ELF_SHALF
#undef FMT_ELF_OFF
#undef FMT_ELF_SIZE
#undef FMT_ELF_SWORD
#undef FMT_ELF_WORD
#undef FMT_ELF_XWORD
#undef FMT_ELF_SXWORD

#undef Elf_Ehdr
#undef Elf_Phdr
#undef Elf_Shdr
#undef Elf_Sym
#undef Elf_Rel
#undef Elf_Rela
#undef Elf_Reloc
#undef Elf_Nhdr
#undef Elf_Dyn

#undef Elf_Addr
#undef Elf_Half
#undef Elf_Off
#undef Elf_Size
#undef Elf_Sword
#undef Elf_Word
#undef Elf_Xword
#undef Elf_Sxword

#undef RTLDRMODELF
#undef PRTLDRMODELF

#undef ELF_R_SYM
#undef ELF_R_TYPE
#undef ELF_R_INFO

#undef ELF_ST_BIND

