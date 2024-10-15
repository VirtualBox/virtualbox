/* $Id: genalias.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * genalias - generate a number of alias objects.
 *
 * @note The code has its origin with kLIBC and was added to VBox by the author.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <stdarg.h>
#include <stdio.h>
#include <iprt/stdint.h>
#include <string.h>
#include <time.h>
#include <assert.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(RT_OS_DARWIN) || (defined(RT_ARCH_X86) && (defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)))
# define GENALIAS_UNDERSCORED 1
#else
# define GENALIAS_UNDERSCORED 0
#endif



static int Error(const char *pszFormat, ...)
{
    va_list va;
    fprintf(stderr, "genalias: error: ");
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return 1;
}

static int SyntaxError(const char *pszFormat, ...)
{
    va_list va;
    fprintf(stderr, "genalias: syntax error: ");
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return 1;
}

static int WriteAliasObjectAOUT(FILE *pOutput, const char *pszAlias, const char *pszReal)
{
#pragma pack(1)
    /* ASSUMES 32-bit target. */
    struct AoutHdr
    {
        uint32_t a_info;
        uint32_t a_text;
        uint32_t a_data;
        uint32_t a_bss;
        uint32_t a_syms;
        uint32_t a_entry;
        uint32_t a_trsize;
        uint32_t a_drsize;
    } Hdr;
#define OMAGIC 0407
    struct AoutSym
    {
        uint32_t n_strx;
        uint8_t n_type;
        int8_t n_other;
        uint16_t n_desc;
        uint32_t n_value;
    } Sym;
#define N_EXT 1
#define N_INDR 10
#pragma pack()
    const uint32_t cchAlias = (uint32_t)strlen(pszAlias);
    const uint32_t cchReal  = (uint32_t)strlen(pszReal);
    uint32_t     u32;

    /* write the header. */
    memset(&Hdr, 0, sizeof(Hdr));
    Hdr.a_info = OMAGIC;
    Hdr.a_syms = 2 * sizeof(Sym);
    if (fwrite(&Hdr, sizeof(Hdr), 1, pOutput) != 1)
        return -2;

    /* The alias symbol. */
    Sym.n_strx = 4 + cchReal + 1 + GENALIAS_UNDERSCORED;
    Sym.n_type = N_INDR | N_EXT;
    Sym.n_other = 0;
    Sym.n_desc = 0;
    Sym.n_value = 0;
    if (fwrite(&Sym, sizeof(Sym), 1, pOutput) != 1)
        return -2;

    /* The real symbol. */
    Sym.n_strx = 4;
    Sym.n_type = N_EXT;
    Sym.n_other = 0;
    Sym.n_desc = 0;
    Sym.n_value = 0;
    if (fwrite(&Sym, sizeof(Sym), 1, pOutput) != 1)
        return -2;

    /* the string table. */
    u32 = 4 + cchReal + 1 + cchAlias + 1 + GENALIAS_UNDERSCORED * 2;
    if (fwrite(&u32, 4, 1, pOutput) != 1)
        return -2;
#if GENALIAS_UNDERSCORED
    if (fputc('_', pOutput) == EOF)
        return -2;
#endif
    if (fwrite(pszReal, cchReal + 1, 1, pOutput) != 1)
        return -2;
#if GENALIAS_UNDERSCORED
    if (fputc('_', pOutput) == EOF)
        return -2;
#endif
    if (fwrite(pszAlias, cchAlias + 1, 1, pOutput) != 1)
        return -2;
    return 0;
}

static int WriteAliasObjectCOFF(FILE *pOutput, const char *pszAlias, const char *pszReal, bool fUnderscored)
{
#pragma pack(1)
    struct CoffHdr
    {
        uint16_t Machine;
        uint16_t NumberOfSections;
        uint32_t TimeDateStamp;
        uint32_t PointerToSymbolTable;
        uint32_t NumberOfSymbols;
        uint16_t SizeOfOptionalHeader;
        uint16_t Characteristics;
    } Hdr;
    struct CoffShdr
    {
        char        Name[8];
        uint32_t    VirtualSize;
        uint32_t    VirtualAddress;
        uint32_t    SizeOfRawData;
        uint32_t    PointerToRawData;
        uint32_t    PointerToRelocations;
        uint32_t    PointerToLinenumbers;
        uint16_t    NumberOfRelocations;
        uint16_t    NumberOfLinenumbers;
        uint32_t    Characteristics;
    } Shdr;
#define IMAGE_SCN_LNK_INFO   0x200
#define IMAGE_SCN_LNK_REMOVE 0x800
    struct CoffSym
    {
        union
        {
            char ShortName[8];
            struct
            {
                uint32_t Zeros;
                uint32_t Offset;
            } s;
        } u;
        uint32_t    Value;
        uint16_t    SectionNumber;
        uint16_t    Type;
        uint8_t     StorageClass;
        uint8_t     NumberOfAuxSymbols;
    } Sym;
#define IMAGE_SYM_UNDEFINED 0
#define IMAGE_SYM_TYPE_NULL 0
#define IMAGE_SYM_CLASS_EXTERNAL 2
#define IMAGE_SYM_CLASS_WEAK_EXTERNAL 105
    struct CoffAuxWeakExt
    {
        uint32_t    TagIndex;
        uint32_t    Characteristics;
        uint8_t     padding[10];
    } Aux;
#define IMAGE_WEAK_EXTERN_SEARCH_ALIAS 3
    assert(sizeof(Hdr) == 20); assert(sizeof(Sym) == 18); assert(sizeof(Aux) == sizeof(Sym));
#pragma pack()
    const uint32_t cchAlias = (uint32_t)strlen(pszAlias);
    const uint32_t cchReal  = (uint32_t)strlen(pszReal);
    uint32_t       u32;

    /* write the header. */
    Hdr.Machine = 0 /*unknown*/; //0x14c /* i386 */;
    Hdr.NumberOfSections = 1;
    Hdr.TimeDateStamp = time(NULL);
    Hdr.PointerToSymbolTable = sizeof(Hdr) + sizeof(Shdr);
    Hdr.NumberOfSymbols = 3;
    Hdr.SizeOfOptionalHeader = 0;
    Hdr.Characteristics = 0;
    if (fwrite(&Hdr, sizeof(Hdr), 1, pOutput) != 1)
        return -2;

    /* The directive section. */
    if (Hdr.NumberOfSections == 1)
    {
        memset(&Shdr, 0, sizeof(Shdr));
        memcpy(Shdr.Name, ".drectve", 8);
        Shdr.Characteristics = IMAGE_SCN_LNK_REMOVE | IMAGE_SCN_LNK_INFO;
        if (fwrite(&Shdr, sizeof(Shdr), 1, pOutput) != 1)
            return -2;
    }

    /* The real symbol. */
    memset(&Sym, 0, sizeof(Sym));
    Sym.u.s.Offset = 4;
    Sym.SectionNumber = IMAGE_SYM_UNDEFINED;
    Sym.Type = IMAGE_SYM_TYPE_NULL;
    Sym.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
    if (fwrite(&Sym, sizeof(Sym), 1, pOutput) != 1)
        return -2;

    /* The alias symbol. */
    memset(&Sym, 0, sizeof(Sym));
    Sym.u.s.Offset = fUnderscored + cchReal + 1 + 4;
    Sym.SectionNumber = IMAGE_SYM_UNDEFINED;
    Sym.Type = IMAGE_SYM_TYPE_NULL;
    Sym.StorageClass = IMAGE_SYM_CLASS_WEAK_EXTERNAL;
    Sym.NumberOfAuxSymbols = 1;
    if (fwrite(&Sym, sizeof(Sym), 1, pOutput) != 1)
        return -2;

    /* aux entry for that. */
    memset(&Aux, 0, sizeof(Aux));
    Aux.TagIndex = 0;
    Aux.Characteristics = IMAGE_WEAK_EXTERN_SEARCH_ALIAS;
    if (fwrite(&Aux, sizeof(Aux), 1, pOutput) != 1)
        return -2;

    /* the string table. */
    u32 = 4 + cchReal + 1 + cchAlias + 1 + fUnderscored * 2;
    if (fwrite(&u32, 4, 1, pOutput) != 1)
        return -2;
    if (fUnderscored)
        if (fputc('_', pOutput) == EOF)
            return -2;
    if (fwrite(pszReal, cchReal + 1, 1, pOutput) != 1)
        return -2;
    if (fUnderscored)
        if (fputc('_', pOutput) == EOF)
            return -2;
    if (fwrite(pszAlias, cchAlias + 1, 1, pOutput) != 1)
        return -2;
    return 0;
}

static int WriteAliasObjectTargetCOFF(FILE *pOutput, const char *pszAlias, const char *pszReal)
{
    return WriteAliasObjectCOFF(pOutput, pszAlias, pszReal, GENALIAS_UNDERSCORED);
}

static int WriteAliasObjectX86COFF(FILE *pOutput, const char *pszAlias, const char *pszReal)
{
    return WriteAliasObjectCOFF(pOutput, pszAlias, pszReal, true  /*fUnderscored*/);
}

static int WriteAliasObjectAmd64Arm64COFF(FILE *pOutput, const char *pszAlias, const char *pszReal)
{
    return WriteAliasObjectCOFF(pOutput, pszAlias, pszReal, false /*fUnderscored*/);
}


static int WriteAliasObjectELF(FILE *pOutput, const char *pszAlias, const char *pszReal)
{
    RT_NOREF(pOutput, pszAlias, pszReal);
    fprintf(stderr, "ELF does not support proper aliasing, only option seems to be adding weak symbols with the strong one.\n");
    return -1;
}

static int WriteAliasObjectOMF(FILE *pOutput, const char *pszAlias, const char *pszReal)
{
    const uint32_t cchAlias = (uint32_t)strlen(pszAlias);
    const uint32_t cchReal  = (uint32_t)strlen(pszReal);
    //const uint32_t cchName  = cchAlias > 250 ? 250 : cchAlias;
    uint32_t       cch;

    if (cchReal >= 250)
        return Error("Symbol '%s' is too long!\n", pszReal);
    if (cchAlias >= 250)
        return Error("Symbol '%s' is too long!\n", pszAlias);

    /* THEADR */
    fputc(0x80, pOutput);
    cch = cchAlias + 2;
    fputc(cch & 0xff, pOutput);
    fputc(cch >> 8, pOutput);
    fputc(cchAlias, pOutput);
    fwrite(pszAlias, cchAlias, 1, pOutput);
    fputc(0, pOutput);                  /* CRC */

    /* ALIAS */
    fputc(0xc6, pOutput);
    cch = cchAlias + 1 + cchReal + 1 + GENALIAS_UNDERSCORED * 2 + 1;
    fputc(cch & 0xff, pOutput);
    fputc(cch >> 8, pOutput);
    fputc(cchAlias + GENALIAS_UNDERSCORED, pOutput);
    if (GENALIAS_UNDERSCORED)
        fputc('_', pOutput);
    fwrite(pszAlias, cchAlias, 1, pOutput);
    fputc(cchReal + GENALIAS_UNDERSCORED, pOutput);
    if (GENALIAS_UNDERSCORED)
        fputc('_', pOutput);
    fwrite(pszReal, cchReal, 1, pOutput);
    fputc(0, pOutput);                  /* CRC */

    /* MODEND32 */
    fputc(0x8b, pOutput);
    fputc(2, pOutput);
    fputc(0, pOutput);
    fputc(0, pOutput);
    if (fputc(0, pOutput) == EOF)       /* CRC */
        return -2;
    return 0;
}

static int WriteAliasObjectMACHO(FILE *pOutput, const char *pszAlias, const char *pszReal)
{
    RT_NOREF(pOutput, pszAlias, pszReal);
    fprintf(stderr, "Mach-O support not implemented yet\n");
    return -1;
}

static int CreateAlias(char *pszBuf, size_t cchInput, char *pszFileBuf, char *pszFilename,
                       int (*pfnWriter)(FILE *, const char *, const char *))
{
    char *pszAlias = pszBuf;
    char *pszReal;
    char *pszFile;
    FILE *pOutput;
    int   rc;
    RT_NOREF(cchInput);

    /*
     * Parse input.
     */
    pszReal = strchr(pszBuf, '=');
    if (!pszReal)
        return Error("Malformed request: '%s'\n", pszBuf);
    *pszReal++ = '\0';

    pszFile = strchr(pszReal, '=');
    if (pszFile)
    {
        *pszFile++ = '\0';
        strcpy(pszFilename, pszFile);
    }
    else
        strcat(strcpy(pszFilename, pszAlias), ".o");

    /*
     * Open the output file.
     */
    pOutput = fopen(pszFileBuf, "wb");
    if (!pOutput)
        return Error("Failed to open '%s' for writing!\n", pszFileBuf);
    rc = pfnWriter(pOutput, pszAlias, pszReal);
    if (rc == -2)
        rc = Error("Write error writing '%s'!\n", pszFileBuf);
    fclose(pOutput);
    return rc;
}


static int Syntax(void)
{
    printf("syntax: genalias -f <format> -D <output-dir> alias=real[=file] [alias2=real2[=file2] [..]]\n"
           "    OR\n"
           "        genalias -f <format> -D <output-dir> -r <response-file>\n"
           "\n"
           "Format can be: aout, coff or omf\n"
           "The responsefile is a single argument per line.\n");
    return 1;
}


int main(int argc, char **argv)
{
    static char s_szBuf[4096];
    static char s_szFile[1024 + sizeof(s_szBuf)];
    int (*pfnWriter)(FILE *pOutput, const char *pszAlias, const char *pszReal);
    char *pszFilename;
    int i;
    int rc;

    /*
     * Parse arguments.
     */
    if (argc <= 5)
        return Syntax();
    if (strcmp(argv[1], "-f"))
        return SyntaxError("Expected -f as the 1st argument.\n");
    if (!strcmp(argv[2], "aout"))
        pfnWriter = WriteAliasObjectAOUT;
    else if (!strcmp(argv[2], "coff"))
        pfnWriter = WriteAliasObjectTargetCOFF;
    else if (!strcmp(argv[2], "coff.x86"))
        pfnWriter = WriteAliasObjectX86COFF;
    else if (!strcmp(argv[2], "coff.amd64"))
        pfnWriter = WriteAliasObjectAmd64Arm64COFF;
    else if (!strcmp(argv[2], "coff.arm64"))
        pfnWriter = WriteAliasObjectAmd64Arm64COFF;
    else if (!strcmp(argv[2], "elf"))
        pfnWriter = WriteAliasObjectELF;
    else if (!strcmp(argv[2], "omf"))
        pfnWriter = WriteAliasObjectOMF;
    else if (!strcmp(argv[2], "macho"))
        pfnWriter = WriteAliasObjectMACHO;
    else
        return SyntaxError("Unknown format '%s'.\n", argv[2]);
    if (strcmp(argv[3], "-D"))
        return SyntaxError("Expected -D as the 3rd argument\n");
    if (!*argv[4])
        return SyntaxError("The output directory name is empty.\n");
    size_t cchFile = strlen(argv[4]);
    if (cchFile > sizeof(s_szFile) - sizeof(s_szBuf))
        return SyntaxError("The output directory name is too long.\n");
    memcpy(s_szFile, argv[4], cchFile);
    s_szFile[cchFile++] = '/';
    pszFilename = &s_szFile[cchFile];

    rc = 0;
    if (!strcmp(argv[5], "-r"))
    {
        /*
         * Responsefile.
         */
        FILE *pResp;
        if (argc <= 6)
            return SyntaxError("Missing response file name\n");
        pResp = fopen(argv[6], "rt");
        if (!pResp)
            return Error("Failed to open '%s' for reading.\n", argv[6]);

        i = 0;
        while (fgets(s_szBuf, sizeof(s_szBuf), pResp))
        {
            size_t  cch = strlen(s_szBuf);
            i++;
            if (cch == sizeof(s_szBuf) && s_szBuf[cch - 1] != '\n')
            {
                rc = Error("Line %d is too long!\n", i);
                break;
            }
            if (cch && s_szBuf[cch - 1] == '\n')
                s_szBuf[--cch] = '\0';
            rc = CreateAlias(s_szBuf, cch, s_szFile, pszFilename, pfnWriter);
            if (rc)
                break;
        }

        fclose(pResp);
    }
    else
    {
        /*
         * Alias descriptors.
         */
        for (i = 5; i < argc; i++)
        {
            size_t  cch = strlen(argv[i]);
            if (cch >= sizeof(s_szBuf))
                return SyntaxError("Argument %d is too long\n", i);
            memcpy(s_szBuf, argv[i], cch + 1);
            rc = CreateAlias(s_szBuf, cch, s_szFile, pszFilename, pfnWriter);
            if (rc)
                break;
        }
    }
    return rc;
}

