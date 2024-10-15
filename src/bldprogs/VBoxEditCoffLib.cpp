/* $Id: VBoxEditCoffLib.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxEditCoffLib - Simple COFF editor for library files.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <iprt/assertcompile.h>
#include <iprt/types.h>
#include <iprt/ctype.h>
#include <iprt/formats/pecoff.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct ARHDR
{
    char    achName[16];
    char    achDate[12];
    char    achUid[6];
    char    achGid[6];
    char    achMode[8];
    char    achSize[10];
    char    achMagic[2];
} ARHDR;
AssertCompileSize(ARHDR, 16+12+6+6+8+10+2);



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Verbosity level. */
static int      g_cVerbosity = 0;

/** The binary size. */
static unsigned g_cbBinary   = 0;
/** The binary data we're editing.   */
static uint8_t *g_pbBinary   = NULL;

/** Size of the currently selected member. */
static unsigned g_cbMember   = 0;
/** Pointer to the data for the currently selected member. */
static uint8_t *g_pbMember   = NULL;


/**
 * File size.
 *
 * @returns file size in bytes.
 * @returns 0 on failure.
 * @param   pFile   File to size.
 */
static unsigned fsize(FILE *pFile)
{
    long    cbFile;
    off_t   Pos = ftell(pFile);
    if (    Pos >= 0
        &&  !fseek(pFile, 0, SEEK_END))
    {
        cbFile = ftell(pFile);
        if (    cbFile >= 0
            &&  !fseek(pFile, 0, SEEK_SET))
            return cbFile;
    }
    return 0;
}


/**
 * Reports a problem.
 *
 * @returns RTEXITCODE_FAILURE
 */
static int error(const char *pszFormat, ...)
{
    fprintf(stderr, "error: ");
    va_list va;
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return RTEXITCODE_FAILURE;
}


/**
 * Reports a syntax problem.
 *
 * @returns RTEXITCODE_SYNTAX
 */
static int syntax(const char *pszFormat, ...)
{
    fprintf(stderr, "syntax error: ");
    va_list va;
    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return RTEXITCODE_FAILURE;
}


/**
 * Display usage
 *
 * @returns success if stdout, syntax error if stderr.
 */
static int usage(FILE *pOut, const char *argv0)
{
    fprintf(pOut,
            "usage: %s --input <in.lib> --output <out.lib> [options and operations]\n"
            "\n"
            "Operations and Options (processed in place):\n"
            "  --verbose   Noisier.\n"
            "  --quiet     Quiet execution.\n"
            "  --select <member>\n"
            "      Selects archive member which name ends in the given string.\n"
            "  --redefine-sym <old>=<new>\n"
            "      Redefine the symbol <old> to <new>.\n"
            "      Note! the length must be the same!\n"
            , argv0);
    return pOut == stdout ? RTEXITCODE_SUCCESS : RTEXITCODE_SYNTAX;
}


/**
 * Helper for SelectMember.
 */
static bool AreAllDigits(const char *pch, size_t cch, size_t *puValue)
{
    *puValue = 0;
    do
    {
        if (!RT_C_IS_DIGIT(*pch))
            return false;
        *puValue = *puValue * 10 + *pch - '0';
        pch++;
        cch--;
    } while (cch > 0);
    return true;
}


/**
 * Selects archive member ending with the given name.
 *
 * Updates g_cbMember and g_pbMember.
 */
static int SelectMember(const char *pszEndsWith)
{
    size_t const cchEndsWith = strlen(pszEndsWith);

    /*
     * Check the header.
     */
    if (memcmp(g_pbBinary, RT_STR_TUPLE("!<arch>\n")))
        return error("Not an AR library!\n");

    /*
     * Work the members.
     */
    const char *pszStringTab = NULL;
    size_t      cbStringTab  = 0;
    for (size_t off = sizeof("!<arch>\n") - 1; off < g_cbBinary;)
    {
        ARHDR      *pHdr = (ARHDR *)&g_pbBinary[off];
        char        szTmp[16 + 8];
        size_t      uValue;
        char       *pszIgn;
#define COPY_AND_TRIM(a_pchSrc, a_cbSrc) do { \
            memcpy(szTmp, (a_pchSrc), (a_cbSrc)); \
            size_t offCopy = (a_cbSrc); \
            while (offCopy > 0 && (szTmp[offCopy - 1] == ' ' || szTmp[offCopy - 1] == '\0')) \
                offCopy--; \
            szTmp[offCopy] = '\0'; \
        } while (0)

        /*
         * Parse the header.
         */

        /* The size: */
        COPY_AND_TRIM(pHdr->achSize, sizeof(pHdr->achSize));
        size_t cbFile = strtol(szTmp, &pszIgn, 10);

        /* The name: */
        size_t      cbExtra = 0;
        size_t      cchName = sizeof(pHdr->achName);
        const char *pchName = pHdr->achName;
        if (   pchName[0] == '#'
            && pchName[1] == '1'
            && pchName[2] == '/')
        {
            COPY_AND_TRIM(&pchName[3], cchName - 3);
            cchName = cbExtra = strtol(szTmp, &pszIgn, 10);
            pchName = (char *)(pHdr + 1);
        }
        else
        {
            while (cchName > 0 && (pchName[cchName - 1] == ' ' || pchName[cchName - 1] == '\0'))
                cchName--;

            /* Long filename string table? */
            if (   (cchName == 2 && pchName[0] == '/' && pchName[1] == '/')
                || (cchName == sizeof("ARFILENAMES/") - 1 && memcmp(pchName, RT_STR_TUPLE("ARFILENAMES/")) == 0))
            {
                pszStringTab = (char *)(pHdr + 1);
                cbStringTab  = cbFile;
            }
            /* Long filename string table reference? */
            else if (   cchName >= 2
                     && (   pchName[0] == '/' /* System V */
                         || pchName[0] == ' ' /* Other */)
                     && AreAllDigits(&pchName[1], cchName - 1, &uValue) && uValue < cbStringTab)
            {
                pchName = &pszStringTab[uValue];
                cchName = strlen(pchName); /** @todo unsafe! */
            }
            /* Drop trailing slash in case of System V filename: */
            else if (cchName > 1 && pchName[cchName - 1] == '/')
                cchName -= 1;
        }

        if (g_cVerbosity > 2)
            fprintf(stderr, "debug: %#08x: %#010x %*.*s\n",
                    (unsigned)off, (unsigned)(cbFile - cbExtra), (int)cchName, (int)cchName, pchName);

        /*
         * Do matching.
         */
        if (   cchName >= cchEndsWith
            && strncmp(&pchName[cchName - cchEndsWith], pszEndsWith, cchEndsWith) == 0)
        {
            g_pbMember = (uint8_t *)(pHdr + 1) + cbExtra;
            g_cbMember = (unsigned)(cbFile - cbExtra);
            if (g_cVerbosity > 1)
                fprintf(stderr, "debug: selected '%*.*s': %#x LB %#x\n",
                        (int)cchName, (int)cchName, pchName, (unsigned)(off + sizeof(*pHdr) + cbExtra), g_cbMember);
            return 0;
        }

        /*
         * Advance.
         */
        off += sizeof(ARHDR) + cbFile + (cbFile & 1);
    }

    return error("No member ending with '%s' was found!\n", pszEndsWith);
}


/**
 * @note Borrowed from VBoxBs3objConverter.cpp
 */
static const char *coffGetSymbolName(PCIMAGE_SYMBOL pSym, const char *pchStrTab, uint32_t cbStrTab, char pszShortName[16])
{
    if (pSym->N.Name.Short != 0)
    {
        memcpy(pszShortName, pSym->N.ShortName, 8);
        pszShortName[8] = '\0';
        return pszShortName;
    }
    if (pSym->N.Name.Long < cbStrTab)
    {
        uint32_t const cbLeft = cbStrTab - pSym->N.Name.Long;
        const char    *pszRet = pchStrTab + pSym->N.Name.Long;
        if (memchr(pszRet, '\0', cbLeft) != NULL)
            return pszRet;
    }
    error("Invalid string table index %#x!\n", pSym->N.Name.Long);
    return "Invalid Symbol Table Entry";
}


/**
 * Redefine a symbol with a different name.
 */
static int RedefineSymbol(const char *pszOldEqualNew)
{
    /*
     * Check state and split up the input.
     */
    if (!g_pbMember)
        return error("No selected archive member!\n");

    const char *pszNew = strchr(pszOldEqualNew, '=');
    if (!pszNew || pszNew[1] == '\0')
        return error("Malformed 'old=new' argument: %s\n", pszOldEqualNew);
    const char  *pszOld = pszOldEqualNew;
    size_t const cchOld = pszNew - pszOldEqualNew;
    pszNew += 1;
    size_t const cchNew = strlen(pszNew);
    if (cchNew > cchOld)
        return error("The new symbol must not be longer than the old symbol: %#x vs %#x (%s)\n", cchNew, cchOld, pszOldEqualNew);

    if (g_cVerbosity > 2)
        fprintf(stderr, "debug: redefining symbol '%*.*s' to '%*.*s'...\n",
                (int)cchOld, (int)cchOld, pszOld, (int)cchNew, (int)cchNew, pszNew);

    /*
     * Parse COFF header.
     */
    const IMAGE_FILE_HEADER *pHdr = (const IMAGE_FILE_HEADER *)g_pbMember;
    if (sizeof(*pHdr) >= g_cbMember)
        return error("member too small for COFF\n");
    if (   pHdr->Machine != IMAGE_FILE_MACHINE_AMD64
        && pHdr->Machine != IMAGE_FILE_MACHINE_I386)
        return error("Unsupported COFF machine: %#x\n", pHdr->Machine);
    if (   pHdr->PointerToSymbolTable >= g_cbMember
        || pHdr->PointerToSymbolTable < sizeof(*pHdr))
        return error("PointerToSymbolTable is out of bounds: %#x, max %#x\n", pHdr->PointerToSymbolTable, g_cbMember);
    unsigned const cSymbols = pHdr->NumberOfSymbols;
    if (   cSymbols >= g_cbMember - pHdr->PointerToSymbolTable
        || cSymbols * sizeof(IMAGE_SYMBOL) > g_cbMember - pHdr->PointerToSymbolTable)
        return error("PointerToSymbolTable + NumberOfSymbols is out of bounds: %#x + %#x * %#x (%#x), max %#x\n",
                     pHdr->PointerToSymbolTable, cSymbols, sizeof(IMAGE_SYMBOL),
                     pHdr->PointerToSymbolTable + cSymbols * sizeof(IMAGE_SYMBOL), g_cbMember);

    /*
     * Work the symbol table.
     */
    unsigned            cRenames  = 0;
    PIMAGE_SYMBOL const paSymTab  = (PIMAGE_SYMBOL)&g_pbMember[pHdr->PointerToSymbolTable];
    const char * const  pchStrTab = (const char *)&paSymTab[pHdr->NumberOfSymbols];
    uint32_t const      cbStrTab  = (uint32_t)((uintptr_t)&g_pbMember[g_cbMember] - (uintptr_t)pchStrTab);
    for (unsigned iSym = 0; iSym < cSymbols; iSym++)
    {
        char        szShort[16];
        const char *pszSymName = coffGetSymbolName(&paSymTab[iSym], pchStrTab, cbStrTab, szShort);
        size_t      cchSymName = strlen(pszSymName);
        if (g_cVerbosity > 3 && cchSymName > 0)
            fprintf(stderr, "debug: symbol %u: %s\n", iSym, pszSymName);
        if (   cchSymName == cchOld
            && memcmp(pszSymName, pszOld, cchSymName) == 0)
        {
            size_t const offStrTab = (size_t)(pszSymName - pchStrTab);
            if (offStrTab < cbStrTab)
            {
                if (g_cVerbosity > 1)
                    fprintf(stderr, "debug: Found symbol '%s' in at string table offset %#x, renaming to '%s'.\n",
                            pszSymName, (uint32_t)offStrTab, pszNew);
                if (offStrTab > 0 && pchStrTab[offStrTab - 1] != '\0')
                    return error("Cannot rename sub-string!\n");
                memset((char *)pszSymName, 0, cchOld);
                memcpy((char *)pszSymName, pszNew, cchNew);
            }
            else
            {
                if (g_cVerbosity > 1)
                    fprintf(stderr, "debug: Found symbol '%s' in symbol table, renaming to '%s'.\n", pszSymName, pszNew);
                memset(paSymTab[iSym].N.ShortName, 0, sizeof(paSymTab[iSym].N.ShortName));
                memcpy(paSymTab[iSym].N.ShortName, pszNew, cchNew);
            }
            cRenames++;
        }

        /* Skip AUX symbols. */
        uint8_t cAuxSyms = paSymTab[iSym].NumberOfAuxSymbols;
        while (cAuxSyms-- > 0)
            iSym++;
    }

    if (cRenames > 0)
        return RTEXITCODE_SUCCESS;
    return error("Symbol '%*.*s' was not found!\n", cchOld, cchOld, pszOld);
}


int main(int argc, char **argv)
{
    /*
     * Parse arguments.
     */
    const char *pszIn  = NULL;
    const char *pszOut = NULL;
    for (int i = 1; i < argc; i++)
    {
        const char *pszArg = argv[i];

        /* Options without values first: */
        if (   strcmp(pszArg, "--verbose") == 0
            || strcmp(pszArg, "-v") == 0)
            g_cVerbosity += 1;
        else if (   strcmp(pszArg, "--quiet") == 0
                 || strcmp(pszArg, "--q") == 0)
            g_cVerbosity  = 0;
        else if (   strcmp(pszArg, "--help") == 0
                 || strcmp(pszArg, "-h") == 0
                 || strcmp(pszArg, "-?") == 0)
            return usage(stdout, argv[0]);
        else if (i + 1 >= argc)
            return syntax("Missing argument value or unknown option '%s'!\n", pszArg);
        else
        {
            i++;
            const char *pszValue = argv[i];
            int         rc = 0;
            if (strcmp(pszArg, "--input") == 0)
            {
                if (pszIn)
                    return syntax("--input can only be specified once!\n");
                pszIn = pszValue;

                /* Load it into memory: */
                FILE *pIn = fopen(pszIn, "rb");
                if (!pIn)
                    return error("Failed to open '%s' for reading!\n", pszIn);
                g_cbBinary = fsize(pIn);
                if (!g_cbBinary)
                    return error("Failed to determin the size of '%s'!\n", pszIn);
                if (g_cbBinary > _128M)
                    return error("'%s' is too large: %x, max %x\n", g_cbBinary, (size_t)_128M);
                g_pbBinary = (uint8_t *)calloc(1, g_cbBinary + 4096);
                if (!g_pbBinary)
                    return error("Out of memory!\n");
                if (fread(g_pbBinary, g_cbBinary, 1, pIn) != 1)
                    return error("Failed to read '%s' into memory!\n", pszIn);
                fclose(pIn);
            }
            else if (strcmp(pszArg, "--output") == 0)
                pszOut = pszValue;
            else if (strcmp(pszArg, "--select") == 0)
                rc = SelectMember(pszValue);
            else if (strcmp(pszArg, "--redefine-sym") == 0)
                rc = RedefineSymbol(pszValue);
            else
                return syntax("Unknown option: %s\n", pszArg);
            if (rc != RTEXITCODE_SUCCESS)
                return rc;
        }
    }

    if (!pszIn || !pszOut)
        return syntax("No %s specified!\n", pszIn ? "output file" : "intput library file");

    /*
     * Write out the result.
     */
    FILE *pOut = fopen(pszOut, "wb");
    if (!pOut)
        return error("Failed to open '%s' for writing!\n", pszOut);
    if (fwrite(g_pbBinary, g_cbBinary, 1, pOut) != 1)
        return error("Error writing %#x bytes to '%s'!\n", g_cbBinary, pszOut);
    if (fclose(pOut) != 0)
        return error("Error closing '%s'!\n", pszOut);
    return RTEXITCODE_SUCCESS;
}

