/* $Id: oiddb2c.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT - OID text database to C converter.
 *
 * The output is used by asn1-dump.cpp.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/assert.h>
#include <iprt/types.h>
#include <iprt/ctype.h>
#include <iprt/stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Include the string table code.
 */
#define BLDPROG_STRTAB_MAX_STRLEN           48
#define BLDPROG_STRTAB_WITH_COMPRESSION
#define BLDPROG_STRTAB_PURE_ASCII
#define BLDPROG_STRTAB_WITH_CAMEL_WORDS
#include <iprt/bldprog-strtab-template.cpp.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define OID2C_MAX_COMP_VALUE    _1G


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Raw OID tree node.
 *
 * This is what we produce while loading OID input files.
 */
typedef struct RAWOIDNODE
{
    /** The component value. */
    uint32_t                uKey;
    /** Number of children.  */
    uint32_t                cChildren;
    /** Pointer to the children pointers (sorted by key). */
    struct RAWOIDNODE     **papChildren;
    /** Pointer to the parent.  */
    struct RAWOIDNODE      *pParent;
    /** The string table entry for this node. */
    BLDPROGSTRING           StrTabEntry;
    /** The table index of the children.  */
    uint32_t                idxChildren;
    /** Set if we've got one or more children with large keys. */
    bool                    fChildrenInBigTable;
} RAWOIDNODE;
/** Pointer to a raw OID node. */
typedef RAWOIDNODE *PRAWOIDNODE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** What to prefix errors with. */
static const char  *g_pszProgName = "oiddb2c";

/** The OID tree. */
static PRAWOIDNODE  g_pOidRoot = NULL;
/** Number of nodes in the OID tree. */
static uint32_t     g_cOidNodes = 0;
/** Number of nodes in the OID tree that has strings (for the string table). */
static uint32_t     g_cOidNodesWithStrings = 0;
/** Max number of children of a node in the OID tree.  */
static uint32_t     g_cMaxOidChildren = 0;
/** Number of nodes which key fits within 6-bits.  */
static uint32_t     g_cOidNodiesWith6bitKeys = 0;


static RTEXITCODE error(const char *pszFormat,  ...)
{
    va_list va;
    va_start(va, pszFormat);
    fprintf(stderr, "%s: error: ", g_pszProgName);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return RTEXITCODE_FAILURE;
}

static RTEXITCODE warning(const char *pszFormat,  ...)
{
    va_list va;
    va_start(va, pszFormat);
    fprintf(stderr, "%s: warning: ", g_pszProgName);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return RTEXITCODE_FAILURE;
}


static void writeDottedOidForNode(PRAWOIDNODE pCurNode, FILE *pOut)
{
    if (pCurNode->pParent)
    {
        writeDottedOidForNode(pCurNode->pParent, pOut);
        fprintf(pOut, ".%u", pCurNode->uKey);
    }
    else
        fprintf(pOut, "%u", pCurNode->uKey);
}


static void writeOidTree(PRAWOIDNODE pCurNode, FILE *pOut, bool fBigTable, PBLDPROGSTRTAB pStrTab)
{
    /*
     * First we produce the entries for our children.
     */
    if (pCurNode->fChildrenInBigTable == fBigTable)
    {
        for (unsigned i = 0; i < pCurNode->cChildren; i++)
        {
            PRAWOIDNODE pChild = pCurNode->papChildren[i];
            fprintf(pOut, "    { %*u, %2u, %u, %2u, %4u, %#06x }, /* ",
                    fBigTable ? 7 : 2,
                    pChild->uKey,
                    (unsigned)pChild->StrTabEntry.cchString,
                    pChild->fChildrenInBigTable,
                    pChild->cChildren,
                    pChild->idxChildren,
                    pChild->StrTabEntry.offStrTab);
            writeDottedOidForNode(pChild, pOut);
            if (pChild->StrTabEntry.pszString)
            {
                fputs(" = \"", pOut);
                BldProgStrTab_PrintCStringLitteral(pStrTab, &pChild->StrTabEntry, pOut);
                fputs("\" */\n", pOut);
            }
            else
                fputs(" */\n", pOut);
        }
    }

    /*
     * Then we decend and let our children do the same.
     */
    for (unsigned i = 0; i < pCurNode->cChildren; i++)
        writeOidTree(pCurNode->papChildren[i], pOut, fBigTable, pStrTab);
}


static uint32_t prepareOidTreeForWriting(PRAWOIDNODE pCurNode, uint32_t idxCur, bool fBigTable)
{
    if (pCurNode->fChildrenInBigTable == fBigTable)
    {
        pCurNode->idxChildren = pCurNode->cChildren ? idxCur : 0;
        idxCur += pCurNode->cChildren;
    }

    for (unsigned i = 0; i < pCurNode->cChildren; i++)
        idxCur = prepareOidTreeForWriting(pCurNode->papChildren[i], idxCur, fBigTable);

    return idxCur;
}


static void addStringFromOidTree(PRAWOIDNODE pCurNode, PBLDPROGSTRTAB pStrTab)
{
    /* Do self. */
    if (pCurNode->StrTabEntry.pszString)
        BldProgStrTab_AddString(pStrTab, &pCurNode->StrTabEntry);

    /* Recurse into children. */
    unsigned i = pCurNode->cChildren;
    while (i-- > 0)
        addStringFromOidTree(pCurNode->papChildren[i], pStrTab);
}


static bool isNiceAsciiString(const char *psz)
{
    unsigned uch;
    while ((uch = *psz) != '\0')
        if (   !(uch & 0x80)
            && (   uch >= 0x20
                || uch == '\t') )
            psz++;
        else
            return false;
    return true;
}


static RTEXITCODE addOidToTree(uint32_t const *pauComponents, unsigned cComponents, const char *pszName,
                               const char *pszFile, unsigned iLineNo)
{
    /*
     * Check preconditions.
     */
    size_t cchName = strlen(pszName);
    if (cchName == '\0')
        return warning("%s(%d): Empty OID name!\n", pszFile, iLineNo);
    if (cchName >= BLDPROG_STRTAB_MAX_STRLEN)
        return warning("%s(%d): OID name is too long (%u)!\n", pszFile, iLineNo, (unsigned)cchName);
    if (cComponents == 0)
        return warning("%s(%d): 'Description' without valid OID preceding it!\n", pszFile, iLineNo);
    if (!isNiceAsciiString(pszName))
        return warning("%s(%d): Contains unwanted characters!\n", pszFile, iLineNo);

    /*
     * Make sure we've got a root node (it has no actual OID componet value,
     * it's just a place to put the top level children).
     */
    if (!g_pOidRoot)
    {
        g_pOidRoot = (PRAWOIDNODE)calloc(1, sizeof(*g_pOidRoot));
        if (!g_pOidRoot)
            return error("Out of memory!\n");
    }

    /*
     * Decend into the tree, adding any missing nodes as we go along.
     * We'll end up with the node which is being named.
     */
    PRAWOIDNODE pCur = g_pOidRoot;
    while (cComponents-- > 0)
    {
        uint32_t const  uKey = *pauComponents++;
        uint32_t        i    = pCur->cChildren;
        while (   i > 0
               && pCur->papChildren[i - 1]->uKey >= uKey)
            i--;
        if (   i < pCur->cChildren
            && pCur->papChildren[i]->uKey == uKey)
            pCur = pCur->papChildren[i];
        else
        {
            /* Resize the child pointer array? */
            if ((pCur->cChildren % 16) == 0)
            {
                void *pvNew = realloc(pCur->papChildren, sizeof(pCur->papChildren[0]) * (pCur->cChildren + 16));
                if (!pvNew)
                    return error("Out of memory!\n");
                pCur->papChildren = (PRAWOIDNODE *)pvNew;
            }

            /* Allocate and initialize the node. */
            PRAWOIDNODE pNew = (PRAWOIDNODE)malloc(sizeof(*pNew));
            if (!pNew)
                return error("Out of memory!\n");
            pNew->uKey = uKey;
            pNew->pParent = pCur;
            pNew->papChildren = NULL;
            pNew->cChildren = 0;
            pNew->fChildrenInBigTable = false;
            memset(&pNew->StrTabEntry, 0, sizeof(pNew->StrTabEntry));

            /* Insert it. */
            if (i < pCur->cChildren)
                memmove(&pCur->papChildren[i + 1], &pCur->papChildren[i], (pCur->cChildren - i) * sizeof(pCur->papChildren[0]));
            pCur->papChildren[i] = pNew;
            pCur->cChildren++;

            if (pCur->cChildren > g_cMaxOidChildren)
                g_cMaxOidChildren = pCur->cChildren;
            g_cOidNodes++;
            if (uKey < 64)
                g_cOidNodiesWith6bitKeys++;
            else
            {
                pCur->fChildrenInBigTable = true;
                if (!pCur->pParent)
                    return error("Invalid OID! Top level componet value is out of range: %u (max 2)\n", uKey);
            }

            /* Decend (could optimize insertion of the remaining nodes, but
               too much work for very little gain). */
            pCur = pNew;
        }
    }

    /*
     * Update the node.
     */
    if (!pCur->StrTabEntry.pszString)
    {
        pCur->StrTabEntry.pszString = (char *)malloc(cchName + 1);
        if (pCur->StrTabEntry.pszString)
            memcpy(pCur->StrTabEntry.pszString, pszName, cchName + 1);
        else
            return error("Out of memory!\n");
        pCur->StrTabEntry.cchString = cchName;
        if (cchName >= 64)
            pCur->fChildrenInBigTable = true;
        g_cOidNodesWithStrings++;
    }
    /* Ignore duplicates, but warn if different name. */
    else if (   pCur->StrTabEntry.cchString != cchName
             || strcmp(pszName, pCur->StrTabEntry.pszString) != 0)
        warning("%s(%d): Duplicate OID, name differs: '%s' vs '%s'\n", pszFile, iLineNo, pCur->StrTabEntry.pszString, pszName);

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE parseOid(uint32_t *pauComponents, unsigned *pcComponents, unsigned cMaxComponents, char const *pszOid,
                           const char *pszFile, unsigned iLine)
{
    const char *pszInput = pszOid;
    unsigned i = 0;
    char     ch;
    for (;;)
    {
        /*
         * Parse the value.
         */
        unsigned    uValue = 0;
        if (RT_C_IS_DIGIT((ch = *pszOid)))
        {
            do
            {
                uValue *= 10;
                uValue += ch - '0';
                if (uValue < OID2C_MAX_COMP_VALUE)
                    pszOid++;
                else
                    return warning("%s(%d): Component %u in OID attribute value '%s' is out side the supported!\n",
                                   pszFile, iLine, i, pszInput);
            } while (RT_C_IS_DIGIT((ch = *pszOid)));
            if (   ch == '\0'
                || ch == '.'
                || RT_C_IS_BLANK(ch))
            {
                if (i < cMaxComponents)
                {
                    pauComponents[i] = uValue;
                    i++;
                    if (ch != '\0')
                        pszOid++;
                    else
                    {
                        *pcComponents = i;
                        return RTEXITCODE_SUCCESS;
                    }
                }
                else
                    return warning("%s(%d): Too many OID components in '%s'!\n", pszFile, iLine, pszInput);
            }
            else
                return warning("%s(%d): Invalid OID attribute value '%s' (ch=%c)!\n", pszFile, iLine, pszInput, ch);
        }
        else
            return warning("%s(%d): Invalid OID attribute value '%s' (ch=%c)!\n", pszFile, iLine, pszInput, ch);
    }
}


static RTEXITCODE loadOidFile(FILE *pIn, const char *pszFile)
{
    /*
     * We share the format used by dumpasn1.cfg, except that we accept
     * dotted OIDs.
     *
     * An OID entry starts with a 'OID = <space or dot separated OID>'.
     * It is usually followed by an 'Comment = ', which we ignore, and a
     * 'Description = <name>' which we keep.  We save the entry once we
     * see the description attribute.
     */
    unsigned    cOidComponents = 0;
    uint32_t    auOidComponents[16];
    unsigned    iLineNo = 0;
    char        szLine[16384];
    char       *pszLine;
    szLine[sizeof(szLine) - 1] = '\0';
    while ((pszLine = fgets(szLine, sizeof(szLine) - 1, pIn)) != NULL)
    {
        iLineNo++;

        /* Strip leading spaces.*/
        char ch;
        while (RT_C_IS_SPACE((ch = *pszLine)) )
            pszLine++;

        /* We only care about lines starting with 'OID =', 'Description =' or
           a numbered OID. */
        if (   ch == 'O' || ch == 'o'
            || ch == 'D' || ch == 'd'
            || ch == '0' || ch == '1' || ch == '2')
        {
            /* Right strip the line. */
            size_t cchLine = strlen(pszLine);
            while (cchLine > 0 && RT_C_IS_SPACE(pszLine[cchLine - 1]))
                cchLine--;
            pszLine[cchLine] = '\0';

            /* Separate the attribute name from the value. */
            char *pszValue = (char *)memchr(pszLine, '=', cchLine);
            if (pszValue)
            {
                size_t cchName = pszValue - pszLine;

                /* Right strip the name. */
                while (cchName > 0 && RT_C_IS_SPACE(pszLine[cchName - 1]))
                    cchName--;
                pszLine[cchName] = '\0';

                /* Left strip the value. */
                do
                    pszValue++;
                while (RT_C_IS_SPACE(*pszValue));

                /* Attribute switch */
                if (   cchName == 3
                    && (pszLine[0] == 'O' || pszLine[0] == 'o')
                    && (pszLine[1] == 'I' || pszLine[1] == 'i')
                    && (pszLine[2] == 'D' || pszLine[2] == 'd'))
                {
                    cOidComponents = 0;
                    parseOid(auOidComponents, &cOidComponents, RT_ELEMENTS(auOidComponents), pszValue, pszFile, iLineNo);
                }
                else if (   cchName == 11
                         && (pszLine[0]  == 'D' || pszLine[0]  == 'd')
                         && (pszLine[1]  == 'e' || pszLine[1]  == 'E')
                         && (pszLine[2]  == 's' || pszLine[2]  == 'S')
                         && (pszLine[3]  == 'c' || pszLine[3]  == 'C')
                         && (pszLine[4]  == 'r' || pszLine[4]  == 'R')
                         && (pszLine[5]  == 'i' || pszLine[5]  == 'I')
                         && (pszLine[6]  == 'p' || pszLine[6]  == 'P')
                         && (pszLine[7]  == 't' || pszLine[7]  == 'T')
                         && (pszLine[8]  == 'i' || pszLine[8]  == 'I')
                         && (pszLine[9]  == 'o' || pszLine[9]  == 'O')
                         && (pszLine[10] == 'n' || pszLine[10] == 'N'))
                {
                    if (   addOidToTree(auOidComponents, cOidComponents, pszValue, pszFile, iLineNo)
                        != RTEXITCODE_SUCCESS)
                        return RTEXITCODE_FAILURE;
                    cOidComponents = 0;
                }
                else
                {
                    /* <OID> = <Value> */
                    cOidComponents = 0;
                    if (   parseOid(auOidComponents, &cOidComponents, RT_ELEMENTS(auOidComponents), pszLine, pszLine, iLineNo)
                        == RTEXITCODE_SUCCESS)
                    {
                        if (   addOidToTree(auOidComponents, cOidComponents, pszValue, pszFile, iLineNo)
                            != RTEXITCODE_SUCCESS)
                            return RTEXITCODE_FAILURE;
                    }
                    cOidComponents = 0;
                }
            }
        }

    }
    if (feof(pIn))
        return RTEXITCODE_SUCCESS;
    return error("error or something reading '%s'.\n", pszFile);
}



static RTEXITCODE usage(FILE *pOut, const char *argv0, RTEXITCODE rcExit)
{
    fprintf(pOut, "usage: %s <out-file.c> <oid-file> [oid-file2 [...]]\n", argv0);
    return rcExit;
}

int main(int argc, char **argv)
{
    /*
     * Process arguments and input files.
     */
    bool        fVerbose   = false;
    unsigned    cInFiles   = 0;
    const char *pszOutFile = NULL;
    for (int i = 1; i < argc; i++)
    {
        const char *pszFile = NULL;
        if (argv[i][0] != '-')
            pszFile = argv[i];
        else if (!strcmp(argv[i], "-"))
            pszFile = argv[i];
        else
            return usage(stderr, argv[0], RTEXITCODE_SYNTAX);

        if (!pszOutFile)
            pszOutFile = pszFile;
        else
        {
            cInFiles++;
            FILE *pInFile = fopen(pszFile, "r");
            if (!pInFile)
                return error("opening '%s' for reading.\n", pszFile);
            RTEXITCODE rcExit = loadOidFile(pInFile, pszFile);
            fclose(pInFile);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
        }
    }

    /*
     * Check that the user specified at least one input and an output file.
     */
    if (!pszOutFile)
        return error("No output file specified specified!\n");
    if (!cInFiles)
        return error("No input files specified!\n");
    if (!g_cOidNodes)
        return error("No OID found!\n");
    if (fVerbose)
        printf("debug: %u nodes with strings;  %u nodes without strings;  %u nodes in total;\n"
               "debug: max %u children;  %u nodes with 6-bit keys (%u others)\n",
               g_cOidNodesWithStrings, g_cOidNodes - g_cOidNodesWithStrings, g_cOidNodes,
               g_cMaxOidChildren, g_cOidNodiesWith6bitKeys, g_cOidNodes - g_cOidNodiesWith6bitKeys);

    /*
     * Compile the string table.
     */
    BLDPROGSTRTAB StrTab;
    if (!BldProgStrTab_Init(&StrTab, g_cOidNodesWithStrings))
        return error("Out of memory!\n");

    addStringFromOidTree(g_pOidRoot, &StrTab);

    if (!BldProgStrTab_CompileIt(&StrTab, fVerbose))
        return error("BldProgStrTab_CompileIt failed!\n");

    /*
     * Open the output file and write out the stuff.
     */
    FILE *pOut;
    if (!strcmp(pszOutFile, "-"))
        pOut = stdout;
    else
        pOut = fopen(pszOutFile, "w");
    if (!pOut)
        return error("opening '%s' for writing.\n", pszOutFile);

    /* Write the string table. */
    BldProgStrTab_WriteStringTable(&StrTab, pOut, "static ", "g_", "OidDbStrTab");

    prepareOidTreeForWriting(g_pOidRoot, 0, false /*fBigTable*/);
    prepareOidTreeForWriting(g_pOidRoot, 0,  true /*fBigTable*/);

    fprintf(pOut,
            "\n"
            "#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)\n"
            "# pragma pack(2)\n"
            "#endif\n"
            "typedef struct RTOIDENTRYSMALL\n"
            "{\n"
            "    uint32_t    uKey        : 6;\n"
            "    uint32_t    cchString   : 6;\n"
            "    uint32_t    fBigTable   : 1;\n"
            "    uint32_t    cChildren   : 7;\n"
            "    uint32_t    idxChildren : 12;\n"
            "    uint16_t    offString;\n"
            "} RTOIDENTRYSMALL;\n"
            "#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)\n"
            "# pragma pack()\n"
            "AssertCompileSize(RTOIDENTRYSMALL, 6);\n"
            "#endif\n"
            "typedef RTOIDENTRYSMALL const *PCRTOIDENTRYSMALL;\n"
            "\n"
            "static const RTOIDENTRYSMALL g_aSmallOidTable[] = \n{\n");
    writeOidTree(g_pOidRoot, pOut, false /*fBigTable*/, &StrTab);
    fprintf(pOut, "};\n");

    fprintf(pOut,
            "\n"
            "#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)\n"
            "# pragma pack(2)\n"
            "#endif\n"
            "typedef struct RTOIDENTRYBIG\n"
            "{\n"
            "    uint32_t    uKey;\n"
            "    uint8_t     cchString;\n"
            "    uint8_t     fBigTable  : 1;\n"
            "    uint8_t     cChildren  : 7;\n"
            "    uint16_t    idxChildren;\n"
            "    uint16_t    offString;\n"
            "} RTOIDENTRYBIG;\n"
            "#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)\n"
            "# pragma pack()\n"
            "AssertCompileSize(RTOIDENTRYBIG, 10);\n"
            "#endif\n"
            "typedef RTOIDENTRYBIG const *PCRTOIDENTRYBIG;\n"
            "\n"
            "static const RTOIDENTRYBIG g_aBigOidTable[] = \n{\n");
    writeOidTree(g_pOidRoot, pOut,  true /*fBigTable*/, &StrTab);
    fprintf(pOut, "};\n");

    /* Carefully close the output file. */
    if (ferror(pOut))
        return error("problem writing '%s'!\n", pszOutFile);
    if (fclose(pOut) != 0)
        return error("closing '%s' after writing it.\n", pszOutFile);

    return RTEXITCODE_SUCCESS;
}

