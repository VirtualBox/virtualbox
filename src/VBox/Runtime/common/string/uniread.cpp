/* $Id: uniread.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Unicode Specification Reader.
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
#include <iprt/types.h>
#include <iprt/stdarg.h>
#include <iprt/ctype.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _MSC_VER
# include <direct.h>
#else
# include <unistd.h>
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The file we're currently parsing. */
static const char *g_pszCurFile;
/** The current line number. */
static unsigned g_iLine;
/** The current output file. */
static FILE *g_pCurOutFile;


/**
 * Exit the program after printing a parse error.
 *
 * @param   pszFormat           The message.
 * @param   ...                 Format arguments.
 */
static DECL_NO_RETURN(void) ParseError(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    fprintf(stderr, "parse error: %s:%u: ", g_pszCurFile, g_iLine);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    exit(1);
}

/**
 * Strip a line.
 * @returns pointer to first non-blank char.
 * @param   pszLine     The line string to strip.
 */
static char *StripLine(char *pszLine)
{
    while (*pszLine == ' ' || *pszLine == '\t')
        pszLine++;

    char *psz = strchr(pszLine, '#');
    if (psz)
        *psz = '\0';
    else
        psz = strchr(pszLine, '\0');
    while (psz > pszLine)
    {
        switch (psz[-1])
        {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
                *--psz = '\0';
                continue;
        }
        break;
    }

    return pszLine;
}


/**
 * Checks if the line is blank or a comment line and should be skipped.
 * @returns true/false.
 * @param   pszLine     The line to consider.
 */
static bool IsCommentOrBlankLine(const char *pszLine)
{
    while (*pszLine == ' ' || *pszLine == '\t' || *pszLine == '\n' || *pszLine == '\r')
        pszLine++;
    return *pszLine == '#' || *pszLine == '\0';
}


/**
 * Get the first field in the string.
 *
 * @returns Pointer to the next field.
 * @param   ppsz        Where to store the pointer to the next field.
 * @param   pszLine     The line string. (could also be *ppsz from a FirstNext call)
 */
static char *FirstField(char **ppsz, char *pszLine)
{
    char *psz = strchr(pszLine, ';');
    if (!psz)
        *ppsz = psz = strchr(pszLine, '\0');
    else
    {
        *psz = '\0';
        *ppsz = psz + 1;
    }

    /* strip */
    while (*pszLine == ' ' || *pszLine == '\t' || *pszLine == '\r' || *pszLine == '\n')
        pszLine++;
    while (psz > pszLine)
    {
        switch (psz[-1])
        {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
                *--psz = '\0';
                continue;
        }
        break;
    }
    return pszLine;
}


/**
 * Get the next field in a field enumeration.
 *
 * @returns Pointer to the next field.
 * @param   ppsz        Where to get and store the string position.
 */
static char *NextField(char **ppsz)
{
    return FirstField(ppsz, *ppsz);
}


/**
 * Splits a decomposition field.
 *
 * This may start with a type that is enclosed in angle brackets.
 *
 * @returns Pointer to the mapping values following the type. @a *ppsz if empty.
 * @param   ppszType    Pointer to the type field pointer.  On input the type
 *                      field contains the combined type and mapping string.  On
 *                      output this should only contain the type, no angle
 *                      brackets.  If no type specified, it is replaced with an
 *                      empty string (const).
 */
static char *SplitDecompField(char **ppszType)
{
    /* Empty field? */
    char *psz = *ppszType;
    if (!*psz)
        return psz;

    /* No type? */
    if (*psz != '<')
    {
        *ppszType = (char *)"";
        return psz;
    }

    /* Split out the type. */
    *ppszType = ++psz;
    psz = strchr(psz, '>');
    if (!psz)
    {
        ParseError("Bad Decomposition Type/Mappings\n");
        /* not reached: return *ppszType; */
    }
    *psz++ = '\0';

    psz = StripLine(psz);
    if (!*psz)
        ParseError("Missing decomposition mappings\n");
    return psz;
}

/**
 * Converts a code point field to a number.
 * @returns Code point.
 * @param   psz     The field string.
 */
static RTUNICP ToNum(const char *psz)
{
    char *pszEnd = NULL;
    unsigned long ul = strtoul(psz, &pszEnd, 16);
    if (pszEnd && *pszEnd)
        ParseError("failed converting '%s' to a number!\n", psz);
    return (RTUNICP)ul;
}


/**
 * Same as ToNum except that if the field is empty the Default is returned.
 */
static RTUNICP ToNumDefault(const char *psz, RTUNICP Default)
{
    if (*psz)
        return ToNum(psz);
    return Default;
}


/**
 * Converts a code point range to numbers.
 * @returns The start code point.\
 * @returns ~(RTUNICP)0 on failure.
 * @param   psz     The field string.
 * @param   pLast   Where to store the last code point in the range.
 */
static RTUNICP ToRange(const char *psz, PRTUNICP pLast)
{
    char *pszEnd = NULL;
    unsigned long ulStart = strtoul(psz, &pszEnd, 16);
    unsigned long ulLast = ulStart;
    if (pszEnd && *pszEnd)
    {
        if (*pszEnd == '.')
        {
            while (*pszEnd == '.')
                pszEnd++;
            ulLast = strtoul(pszEnd, &pszEnd, 16);
            if (pszEnd && *pszEnd)
            {
                ParseError("failed converting '%s' to a number!\n", psz);
                /* not reached: return ~(RTUNICP)0;*/
            }
        }
        else
        {
            ParseError("failed converting '%s' to a number!\n", psz);
            /* not reached: return ~(RTUNICP)0; */
        }
    }
    *pLast = (RTUNICP)ulLast;
    return (RTUNICP)ulStart;

}

/**
 * For converting the decomposition mappings field and similar.
 *
 * @returns Mapping array or NULL if none.
 * @param   psz                 The string to convert.  Can be empty.
 * @param   pcEntries           Where to store the number of entries.
 * @param   cMax                The max number of entries.
 */
static PRTUNICP ToMapping(char *psz, unsigned *pcEntries, unsigned cMax)
{
    PRTUNICP paCps  = NULL;
    unsigned cAlloc = 0;
    unsigned i      = 0;

    /* Convert the code points. */
    while (psz)
    {
        /* skip leading spaces */
        while (RT_C_IS_BLANK(*psz))
            psz++;

        /* the end? */
        if (!*psz)
            break;

        /* room left? */
        if (i >= cMax)
        {
            ParseError("Too many mappings.\n");
            /* not reached: break; */
        }
        if (i >= cAlloc)
        {
            cAlloc += 4;
            paCps = (PRTUNICP)realloc(paCps, cAlloc * sizeof(paCps[0]));
            if (!paCps)
            {
                fprintf(stderr, "out of memory (%u)\n", (unsigned)(cAlloc * sizeof(paCps[0])));
                exit(1);
            }
        }

        /* Find the end. */
        char *pszThis = psz;
        while (RT_C_IS_XDIGIT(*psz))
            psz++;
        if (*psz && !RT_C_IS_BLANK(*psz))
            ParseError("Malformed mappings.\n");
        if (*psz)
            *psz++ = '\0';

        /* Convert to number and add it. */
        paCps[i++] = ToNum(pszThis);
    }

    *pcEntries = i;
    return paCps;
}


/**
 * Duplicate a string, optimize certain strings to save memory.
 *
 * @returns Pointer to string copy.
 * @param   pszStr      The string to duplicate.
 */
static char *DupStr(const char *pszStr)
{
    if (!*pszStr)
        return (char*)"";
    char *psz = strdup(pszStr);
    if (psz)
        return psz;

    fprintf(stderr, "out of memory!\n");
    exit(1);
}


/**
 * Array of all possible and impossible unicode code points as of 4.1
 */
struct CPINFO
{
    RTUNICP     CodePoint;
    RTUNICP     SimpleUpperCaseMapping;
    RTUNICP     SimpleLowerCaseMapping;
    RTUNICP     SimpleTitleCaseMapping;
    unsigned    CanonicalCombiningClass;
    const char *pszDecompositionType;
    unsigned    cDecompositionMapping;
    PRTUNICP    paDecompositionMapping;
    const char *pszName;
    /** Set if this is an unused entry */
    unsigned    fNullEntry : 1;

    unsigned    fAlphabetic : 1;
    unsigned    fASCIIHexDigit : 1;
    unsigned    fBidiControl : 1;
    unsigned    fCaseIgnorable : 1;
    unsigned    fCased : 1;
    unsigned    fChangesWhenCasefolded : 1;
    unsigned    fChangesWhenCasemapped : 1;
    unsigned    fChangesWhenLowercased : 1;
    unsigned    fChangesWhenTitlecased : 1;
    unsigned    fChangesWhenUppercased : 1;
    unsigned    fDash : 1;
    unsigned    fDefaultIgnorableCodePoint : 1;
    unsigned    fDeprecated : 1;
    unsigned    fDiacritic : 1;
    unsigned    fExtender : 1;
    unsigned    fGraphemeBase : 1;
    unsigned    fGraphemeExtend : 1;
    unsigned    fGraphemeLink : 1;
    unsigned    fHexDigit : 1;
    unsigned    fHyphen : 1;
    unsigned    fIDContinue : 1;
    unsigned    fIdeographic : 1;
    unsigned    fIDSBinaryOperator : 1;
    unsigned    fIDStart : 1;
    unsigned    fIDSTrinaryOperator : 1;
    unsigned    fJoinControl : 1;
    unsigned    fLogicalOrderException : 1;
    unsigned    fLowercase : 1;
    unsigned    fMath : 1;
    unsigned    fNoncharacterCodePoint : 1;
    unsigned    fOtherAlphabetic : 1;
    unsigned    fOtherDefaultIgnorableCodePoint : 1;
    unsigned    fOtherGraphemeExtend : 1;
    unsigned    fOtherIDContinue : 1;
    unsigned    fOtherIDStart : 1;
    unsigned    fOtherLowercase : 1;
    unsigned    fOtherMath : 1;
    unsigned    fOtherUppercase : 1;
    unsigned    fPatternSyntax : 1;
    unsigned    fPatternWhiteSpace : 1;
    unsigned    fQuotationMark : 1;
    unsigned    fRadical : 1;
    unsigned    fSoftDotted : 1;
    unsigned    fSTerm : 1;
    unsigned    fTerminalPunctuation : 1;
    unsigned    fUnifiedIdeograph : 1;
    unsigned    fUppercase : 1;
    unsigned    fVariationSelector : 1;
    unsigned    fWhiteSpace : 1;
    unsigned    fXIDContinue : 1;
    unsigned    fXIDStart : 1;

    /** @name DerivedNormalizationProps.txt
     * @{ */
    unsigned    fFullCompositionExclusion : 1;
    unsigned    fInvNFC_QC : 2;     /**< If 1 (NFC_QC == N) then code point 100% sure not part of NFC string. */
    unsigned    fInvNFD_QC : 2;     /**< If 1 (NFD_QC == N) then code point 100% sure not part of NFD string. */
    unsigned    fInvNFKC_QC : 2;
    unsigned    fInvNFKD_QC : 2;
    unsigned    fExpandsOnNFC : 1;
    unsigned    fExpandsOnNFD : 1;
    unsigned    fExpandsOnNFKC : 1;
    unsigned    fExpandsOnNFKD : 1;
    /** @}  */

    /* unprocessed stuff, so far. */
    const char *pszGeneralCategory;
    const char *pszBidiClass;
    const char *pszNumericType;
    const char *pszNumericValueD;
    const char *pszNumericValueN;
    const char *pszBidiMirrored;
    const char *pszUnicode1Name;
    const char *pszISOComment;
} g_aCPInfo[0x110000];


/**
 * Creates a 'null' entry at i.
 * @param   i       The entry in question.
 */
static void NullEntry(unsigned i)
{
    g_aCPInfo[i].CodePoint = i;
    g_aCPInfo[i].fNullEntry = 1;
    g_aCPInfo[i].SimpleUpperCaseMapping = i;
    g_aCPInfo[i].SimpleLowerCaseMapping = i;
    g_aCPInfo[i].SimpleTitleCaseMapping = i;
    g_aCPInfo[i].pszDecompositionType = "";
    g_aCPInfo[i].cDecompositionMapping = 0;
    g_aCPInfo[i].paDecompositionMapping = NULL;
    g_aCPInfo[i].pszName = "";
    g_aCPInfo[i].pszGeneralCategory = "";
    g_aCPInfo[i].pszBidiClass = "";
    g_aCPInfo[i].pszNumericType = "";
    g_aCPInfo[i].pszNumericValueD = "";
    g_aCPInfo[i].pszNumericValueN = "";
    g_aCPInfo[i].pszBidiMirrored = "";
    g_aCPInfo[i].pszUnicode1Name = "";
    g_aCPInfo[i].pszISOComment = "";
}


/**
 * Open a file for reading, optionally with a base path prefixed.
 *
 * @returns file stream on success, NULL w/ complaint on failure.
 * @param   pszBasePath         The base path, can be NULL.
 * @param   pszFilename         The name of the file to open.
 */
static FILE *OpenFile(const char *pszBasePath, const char *pszFilename)
{
    FILE *pFile;
    if (   !pszBasePath
        || *pszFilename == '/'
#if defined(_MSC_VER) || defined(__OS2__)
        || *pszFilename == '\\'
        || (*pszFilename && pszFilename[1] == ':')
#endif
       )
    {
        pFile = fopen(pszFilename, "r");
        if (!pFile)
            fprintf(stderr, "uniread: failed to open '%s' for reading\n", pszFilename);
    }
    else
    {
        size_t cchBasePath = strlen(pszBasePath);
        size_t cchFilename = strlen(pszFilename);
        char  *pszFullName = (char *)malloc(cchBasePath + 1 + cchFilename + 1);
        if (!pszFullName)
        {
            fprintf(stderr, "uniread: failed to allocate %d bytes\n", (int)(cchBasePath + 1 + cchFilename + 1));
            return NULL;
        }

        memcpy(pszFullName, pszBasePath, cchBasePath);
        pszFullName[cchBasePath] = '/';
        memcpy(&pszFullName[cchBasePath + 1], pszFilename, cchFilename + 1);

        pFile = fopen(pszFullName, "r");
        if (!pFile)
            fprintf(stderr, "uniread: failed to open '%s' for reading\n", pszFullName);
        free(pszFullName);
    }
    g_pszCurFile = pszFilename;
    g_iLine      = 0;
    return pFile;
}


/**
 * Wrapper around fgets that keep track of the line number.
 *
 * @returns See fgets.
 * @param   pszBuf              The buffer.  See fgets for output definition.
 * @param   cbBuf               The buffer size.
 * @param   pFile               The file to read from.
 */
static char *GetLineFromFile(char *pszBuf, int cbBuf, FILE *pFile)
{
    g_iLine++;
    return fgets(pszBuf, cbBuf, pFile);
}


/**
 * Closes a file opened by OpenFile
 *
 * @param   pFile               The file to close.
 */
static void CloseFile(FILE *pFile)
{
    g_pszCurFile = NULL;
    g_iLine = 0;
    fclose(pFile);
}


/**
 * Read the UnicodeData.txt file.
 * @returns 0 on success.
 * @returns !0 on failure.
 * @param   pszBasePath         The base path, can be NULL.
 * @param   pszFilename         The name of the file.
 */
static int ReadUnicodeData(const char *pszBasePath, const char *pszFilename)
{
    /*
     * Open input.
     */
    FILE *pFile = OpenFile(pszBasePath, pszFilename);
    if (!pFile)
        return 1;

    /*
     * Parse the input and spit out the output.
     */
    char szLine[4096];
    RTUNICP i = 0;
    while (GetLineFromFile(szLine, sizeof(szLine), pFile) != NULL)
    {
        if (IsCommentOrBlankLine(szLine))
            continue;

        char *pszCurField;
        char *pszCodePoint = FirstField(&pszCurField, StripLine(szLine)); /* 0 */
        char *pszName = NextField(&pszCurField);                          /* 1 */
        char *pszGeneralCategory = NextField(&pszCurField);               /* 2 */
        char *pszCanonicalCombiningClass = NextField(&pszCurField);       /* 3 */
        char *pszBidiClass = NextField(&pszCurField);                     /* 4 */
        char *pszDecompositionType = NextField(&pszCurField);             /* 5 */
        char *pszDecompositionMapping = SplitDecompField(&pszDecompositionType);
        char *pszNumericType = NextField(&pszCurField);                   /* 6 */
        char *pszNumericValueD = NextField(&pszCurField);                 /* 7 */
        char *pszNumericValueN = NextField(&pszCurField);                 /* 8 */
        char *pszBidiMirrored = NextField(&pszCurField);                  /* 9 */
        char *pszUnicode1Name = NextField(&pszCurField);                  /* 10 */
        char *pszISOComment = NextField(&pszCurField);                    /* 11 */
        char *pszSimpleUpperCaseMapping = NextField(&pszCurField);        /* 12 */
        char *pszSimpleLowerCaseMapping = NextField(&pszCurField);        /* 13 */
        char *pszSimpleTitleCaseMapping = NextField(&pszCurField);        /* 14 */

        RTUNICP CodePoint = ToNum(pszCodePoint);
        if (CodePoint >= RT_ELEMENTS(g_aCPInfo))
        {
            ParseError("U+05X is out of range\n", CodePoint);
            /* not reached: continue;*/
        }

        /* catchup? */
        while (i < CodePoint)
            NullEntry(i++);
        if (i != CodePoint)
        {
            ParseError("i=%d CodePoint=%u\n", i, CodePoint);
            /* not reached: CloseFile(pFile);
            return 1; */
        }

        /* this one */
        g_aCPInfo[i].CodePoint = i;
        g_aCPInfo[i].fNullEntry = 0;
        g_aCPInfo[i].pszName                    = DupStr(pszName);
        g_aCPInfo[i].SimpleUpperCaseMapping     = ToNumDefault(pszSimpleUpperCaseMapping, CodePoint);
        g_aCPInfo[i].SimpleLowerCaseMapping     = ToNumDefault(pszSimpleLowerCaseMapping, CodePoint);
        g_aCPInfo[i].SimpleTitleCaseMapping     = ToNumDefault(pszSimpleTitleCaseMapping, CodePoint);
        g_aCPInfo[i].CanonicalCombiningClass    = ToNum(pszCanonicalCombiningClass);
        g_aCPInfo[i].pszDecompositionType       = DupStr(pszDecompositionType);
        g_aCPInfo[i].paDecompositionMapping     = ToMapping(pszDecompositionMapping, &g_aCPInfo[i].cDecompositionMapping, 20);
        g_aCPInfo[i].pszGeneralCategory         = DupStr(pszGeneralCategory);
        g_aCPInfo[i].pszBidiClass               = DupStr(pszBidiClass);
        g_aCPInfo[i].pszNumericType             = DupStr(pszNumericType);
        g_aCPInfo[i].pszNumericValueD           = DupStr(pszNumericValueD);
        g_aCPInfo[i].pszNumericValueN           = DupStr(pszNumericValueN);
        g_aCPInfo[i].pszBidiMirrored            = DupStr(pszBidiMirrored);
        g_aCPInfo[i].pszUnicode1Name            = DupStr(pszUnicode1Name);
        g_aCPInfo[i].pszISOComment              = DupStr(pszISOComment);
        i++;
    }

    /* catchup? */
    while (i < RT_ELEMENTS(g_aCPInfo))
        NullEntry(i++);
    CloseFile(pFile);

    return 0;
}


/**
 * Generates excluded data.
 *
 * @returns 0 on success, exit code on failure.
 */
static int GenerateExcludedData(void)
{
    /*
     * Hangul Syllables U+AC00 to U+D7A3.
     */
    for (RTUNICP i = 0xac00; i <= 0xd7a3; i++)
    {
        g_aCPInfo[i].fNullEntry = 0;
        g_aCPInfo[i].fInvNFD_QC = 1;
        /** @todo generate the decomposition: http://unicode.org/reports/tr15/#Hangul
         *         */
    }

    /** @todo
     * CJK Ideographs Extension A (U+3400 - U+4DB5)
     * CJK Ideographs (U+4E00 - U+9FA5)
     * CJK Ideograph Extension B (U+20000 - U+2A6D6)
     * CJK Ideograph Extension C (U+2A700 - U+2B734)
     */

    return 0;
}



/**
 * Worker for ApplyProperty that handles a yes, no, maybe property value.
 *
 * @returns 0 (NO), 1 (YES), 2 (MAYBE).
 * @param   ppszNextField   The field cursor, input and output.
 */
static int YesNoMaybePropertyValue(char **ppszNextField)
{
    if (!**ppszNextField)
        ParseError("Missing Y/N/M field\n");
    else
    {
        char *psz = NextField(ppszNextField);
        if (!strcmp(psz, "N"))
            return 0;
        if (!strcmp(psz, "Y"))
            return 1;
        if (!strcmp(psz, "M"))
            return 2;
        ParseError("Unexpected Y/N/M value: '%s'\n",  psz);
    }
    /* not reached: return 0; */
}


/**
 * Inverted version of YesNoMaybePropertyValue
 *
 * @returns 1 (NO), 0 (YES), 2 (MAYBE).
 * @param   ppszNextField   The field cursor, input and output.
 */
static int YesNoMaybePropertyValueInv(char **ppszNextField)
{
    unsigned rc = YesNoMaybePropertyValue(ppszNextField);
    switch (rc)
    {
        case 0:     return 1;
        case 1:     return 0;
        default:    return rc;
    }
}


/**
 * Applies a property to a code point.
 *
 * @param   StartCP         The code point.
 * @param   pszProperty     The property name.
 * @param   pszNextField    The next field.
 */
static void ApplyProperty(RTUNICP StartCP, const char *pszProperty, char *pszNextField)
{
    if (StartCP >= RT_ELEMENTS(g_aCPInfo))
    {
        ParseError("U+%06X is out of the g_aCPInfo range.\n", StartCP);
        /* not reached: return; */
    }
    struct CPINFO *pCPInfo = &g_aCPInfo[StartCP];
    /* string switch */
         if (!strcmp(pszProperty, "ASCII_Hex_Digit")) pCPInfo->fASCIIHexDigit = 1;
    else if (!strcmp(pszProperty, "Alphabetic")) pCPInfo->fAlphabetic = 1;
    else if (!strcmp(pszProperty, "Bidi_Control")) pCPInfo->fBidiControl = 1;
    else if (!strcmp(pszProperty, "Case_Ignorable")) pCPInfo->fCaseIgnorable = 1;
    else if (!strcmp(pszProperty, "Cased")) pCPInfo->fCased = 1;
    else if (!strcmp(pszProperty, "Changes_When_Casefolded")) pCPInfo->fChangesWhenCasefolded = 1;
    else if (!strcmp(pszProperty, "Changes_When_Casemapped")) pCPInfo->fChangesWhenCasemapped = 1;
    else if (!strcmp(pszProperty, "Changes_When_Lowercased")) pCPInfo->fChangesWhenLowercased = 1;
    else if (!strcmp(pszProperty, "Changes_When_Titlecased")) pCPInfo->fChangesWhenTitlecased = 1;
    else if (!strcmp(pszProperty, "Changes_When_Uppercased")) pCPInfo->fChangesWhenUppercased = 1;
    else if (!strcmp(pszProperty, "Dash")) pCPInfo->fDash = 1;
    else if (!strcmp(pszProperty, "Default_Ignorable_Code_Point")) pCPInfo->fDefaultIgnorableCodePoint = 1;
    else if (!strcmp(pszProperty, "Deprecated")) pCPInfo->fDeprecated = 1;
    else if (!strcmp(pszProperty, "Diacritic")) pCPInfo->fDiacritic = 1;
    else if (!strcmp(pszProperty, "Extender")) pCPInfo->fExtender = 1;
    else if (!strcmp(pszProperty, "Grapheme_Base")) pCPInfo->fGraphemeBase = 1;
    else if (!strcmp(pszProperty, "Grapheme_Extend")) pCPInfo->fGraphemeExtend = 1;
    else if (!strcmp(pszProperty, "Grapheme_Link")) pCPInfo->fGraphemeLink = 1;
    else if (!strcmp(pszProperty, "Hex_Digit")) pCPInfo->fHexDigit = 1;
    else if (!strcmp(pszProperty, "Hyphen")) pCPInfo->fHyphen = 1;
    else if (!strcmp(pszProperty, "ID_Continue")) pCPInfo->fIDContinue = 1;
    else if (!strcmp(pszProperty, "ID_Start")) pCPInfo->fIDStart = 1;
    else if (!strcmp(pszProperty, "Ideographic")) pCPInfo->fIdeographic = 1;
    else if (!strcmp(pszProperty, "IDS_Binary_Operator")) pCPInfo->fIDSBinaryOperator = 1;
    else if (!strcmp(pszProperty, "IDS_Trinary_Operator")) pCPInfo->fIDSTrinaryOperator = 1;
    else if (!strcmp(pszProperty, "Join_Control")) pCPInfo->fJoinControl = 1;
    else if (!strcmp(pszProperty, "Logical_Order_Exception")) pCPInfo->fLogicalOrderException = 1;
    else if (!strcmp(pszProperty, "Lowercase")) pCPInfo->fLowercase = 1;
    else if (!strcmp(pszProperty, "Math")) pCPInfo->fMath = 1;
    else if (!strcmp(pszProperty, "Noncharacter_Code_Point")) pCPInfo->fNoncharacterCodePoint = 1;
    else if (!strcmp(pszProperty, "Other_Alphabetic")) pCPInfo->fOtherAlphabetic = 1;
    else if (!strcmp(pszProperty, "Other_Default_Ignorable_Code_Point")) pCPInfo->fOtherDefaultIgnorableCodePoint = 1;
    else if (!strcmp(pszProperty, "Other_Grapheme_Extend")) pCPInfo->fOtherGraphemeExtend = 1;
    else if (!strcmp(pszProperty, "Other_ID_Continue")) pCPInfo->fOtherIDContinue = 1;
    else if (!strcmp(pszProperty, "Other_ID_Start")) pCPInfo->fOtherIDStart = 1;
    else if (!strcmp(pszProperty, "Other_Lowercase")) pCPInfo->fOtherLowercase = 1;
    else if (!strcmp(pszProperty, "Other_Math")) pCPInfo->fOtherMath = 1;
    else if (!strcmp(pszProperty, "Other_Uppercase")) pCPInfo->fOtherUppercase = 1;
    else if (!strcmp(pszProperty, "Pattern_Syntax")) pCPInfo->fPatternSyntax = 1;
    else if (!strcmp(pszProperty, "Pattern_White_Space")) pCPInfo->fPatternWhiteSpace = 1;
    else if (!strcmp(pszProperty, "Quotation_Mark")) pCPInfo->fQuotationMark = 1;
    else if (!strcmp(pszProperty, "Radical")) pCPInfo->fRadical = 1;
    else if (!strcmp(pszProperty, "Soft_Dotted")) pCPInfo->fSoftDotted = 1;
    else if (!strcmp(pszProperty, "STerm")) pCPInfo->fSTerm = 1;
    else if (!strcmp(pszProperty, "Terminal_Punctuation")) pCPInfo->fTerminalPunctuation = 1;
    else if (!strcmp(pszProperty, "Unified_Ideograph")) pCPInfo->fUnifiedIdeograph = 1;
    else if (!strcmp(pszProperty, "Uppercase")) pCPInfo->fUppercase = 1;
    else if (!strcmp(pszProperty, "Variation_Selector")) pCPInfo->fVariationSelector = 1;
    else if (!strcmp(pszProperty, "White_Space")) pCPInfo->fWhiteSpace = 1;
    else if (!strcmp(pszProperty, "XID_Continue")) pCPInfo->fXIDContinue = 1;
    else if (!strcmp(pszProperty, "XID_Start")) pCPInfo->fXIDStart = 1;
    /* DerivedNormalizationProps: */
    else if (!strcmp(pszProperty, "FC_NFKC")) return; /* ignored */
    else if (!strcmp(pszProperty, "Full_Composition_Exclusion")) pCPInfo->fFullCompositionExclusion = 1;
    else if (!strcmp(pszProperty, "NFC_QC"))  pCPInfo->fInvNFC_QC  = YesNoMaybePropertyValueInv(&pszNextField);
    else if (!strcmp(pszProperty, "NFD_QC"))  pCPInfo->fInvNFD_QC  = YesNoMaybePropertyValueInv(&pszNextField);
    else if (!strcmp(pszProperty, "NFKC_QC")) pCPInfo->fInvNFKC_QC = YesNoMaybePropertyValueInv(&pszNextField);
    else if (!strcmp(pszProperty, "NFKD_QC")) pCPInfo->fInvNFKD_QC = YesNoMaybePropertyValueInv(&pszNextField);
    else if (!strcmp(pszProperty, "Expands_On_NFC"))  pCPInfo->fExpandsOnNFC  = 1;
    else if (!strcmp(pszProperty, "Expands_On_NFD"))  pCPInfo->fExpandsOnNFD  = 1;
    else if (!strcmp(pszProperty, "Expands_On_NFKC")) pCPInfo->fExpandsOnNFKC = 1;
    else if (!strcmp(pszProperty, "Expands_On_NFKD")) pCPInfo->fExpandsOnNFKD = 1;
    else if (!strcmp(pszProperty, "NFKC_CF")) return; /*ignore */
    else if (!strcmp(pszProperty, "Changes_When_NFKC_Casefolded")) return; /*ignore */
    else
    {
        ParseError("Unknown property '%s'\n", pszProperty);
        /* not reached: return; */
    }

    if (pszNextField && *pszNextField)
        ParseError("Unexpected next field: '%s'\n", pszNextField);
}


/**
 * Reads a property file.
 *
 * There are several property files, this code can read all
 * of those but will only make use of the properties it recognizes.
 *
 * @returns 0 on success.
 * @returns !0 on failure.
 * @param   pszBasePath         The base path, can be NULL.
 * @param   pszFilename     The name of the file.
 */
static int ReadProperties(const char *pszBasePath, const char *pszFilename)
{
    /*
     * Open input.
     */
    FILE *pFile = OpenFile(pszBasePath, pszFilename);
    if (!pFile)
        return 1;

    /*
     * Parse the input and spit out the output.
     */
    char szLine[4096];
    while (GetLineFromFile(szLine, sizeof(szLine), pFile) != NULL)
    {
        if (IsCommentOrBlankLine(szLine))
            continue;
        char *pszCurField;
        char *pszRange    = FirstField(&pszCurField, StripLine(szLine));
        char *pszProperty = NextField(&pszCurField);
        if (!*pszProperty)
        {
            ParseError("no property field.\n");
            /* not reached: continue; */
        }

        RTUNICP LastCP;
        RTUNICP StartCP = ToRange(pszRange, &LastCP);
        if (StartCP == ~(RTUNICP)0)
            continue;

        while (StartCP <= LastCP)
            ApplyProperty(StartCP++, pszProperty, pszCurField);
    }

    CloseFile(pFile);

    return 0;
}


/**
 * Append a flag to the string.
 */
static char *AppendFlag(char *psz, const char *pszFlag)
{
    char *pszEnd = strchr(psz, '\0');
    if (pszEnd != psz)
    {
        *pszEnd++ = ' ';
        *pszEnd++ = '|';
        *pszEnd++ = ' ';
    }
    strcpy(pszEnd, pszFlag);
    return psz;
}

/**
 * Calcs the flags for a code point.
 * @returns true if there is a flag.
 * @returns false if the isn't.
 */
static bool CalcFlags(struct CPINFO *pInfo, char *pszFlags)
{
    pszFlags[0] = '\0';
    /** @todo read the specs on this other vs standard stuff, and check out the finer points */
    if (pInfo->fAlphabetic || pInfo->fOtherAlphabetic)
        AppendFlag(pszFlags, "RTUNI_ALPHA");
    if (pInfo->fHexDigit || pInfo->fASCIIHexDigit)
        AppendFlag(pszFlags, "RTUNI_XDIGIT");
    if (!strcmp(pInfo->pszGeneralCategory, "Nd"))
        AppendFlag(pszFlags, "RTUNI_DDIGIT");
    if (pInfo->fWhiteSpace)
        AppendFlag(pszFlags, "RTUNI_WSPACE");
    if (pInfo->fUppercase || pInfo->fOtherUppercase)
        AppendFlag(pszFlags, "RTUNI_UPPER");
    if (pInfo->fLowercase || pInfo->fOtherLowercase)
        AppendFlag(pszFlags, "RTUNI_LOWER");
    //if (pInfo->???)
    //    AppendFlag(pszFlags, "RTUNI_BSPACE");
#if 0
    if (pInfo->fInvNFD_QC != 0 || pInfo->fInvNFC_QC != 0)
    {
        AppendFlag(pszFlags, "RTUNI_QC_NFX");
        if (!pInfo->paDecompositionMapping && pInfo->fInvNFD_QC)
            fprintf(stderr, "uniread: U+%05X is QC_NFD but has no mappings.\n", pInfo->CodePoint);
        else if (*pInfo->pszDecompositionType && pInfo->fInvNFD_QC)
            fprintf(stderr, "uniread: U+%05X is QC_NFD but has no canonical mappings.\n", pInfo->CodePoint);
    }
    else if (pInfo->paDecompositionMapping && !*pInfo->pszDecompositionType)
        fprintf(stderr, "uniread: U+%05X is not QC_NFX but has canonical mappings.\n", pInfo->CodePoint);
#endif

    if (!*pszFlags)
    {
        pszFlags[0] = '0';
        pszFlags[1] = '\0';
        return false;
    }
    return true;
}


/**
 * Closes the primary output stream.
 */
static int Stream1Close(void)
{
    if (g_pCurOutFile && g_pCurOutFile != stdout && g_pCurOutFile != stderr)
    {
        if (fclose(g_pCurOutFile) != 0)
        {
            fprintf(stderr, "Error closing output file.\n");
            return -1;
        }
    }
    g_pCurOutFile = NULL;
    return 0;
}


/**
 * Initializes the 1st stream to output to a given file.
 */
static int Stream1Init(const char *pszName)
{
    int rc = Stream1Close();
    if (!rc)
    {
        g_pCurOutFile = fopen(pszName, "w");
        if (!g_pCurOutFile)
        {
            fprintf(stderr, "Error opening output file '%s'.\n", pszName);
            rc = -1;
        }
    }
    return rc;
}


/**
 * printf wrapper for the primary output stream.
 *
 * @returns See vfprintf.
 * @param   pszFormat           The vfprintf format string.
 * @param   ...                 The format arguments.
 */
static int Stream1Printf(const char *pszFormat, ...)
{
    int     cch;
    va_list va;
    va_start(va, pszFormat);
    cch = vfprintf(g_pCurOutFile, pszFormat, va);
    va_end(va);
    return cch;
}


/** The data store for stream two. */
static char g_szStream2[10240];
static unsigned volatile g_offStream2 = 0;

/**
 * Initializes the 2nd steam.
 */
static void Stream2Init(void)
{
    g_szStream2[0] = '\0';
    g_offStream2 = 0;
}

/**
 * Flushes the 2nd stream to stdout.
 */
static int Stream2Flush(void)
{
    g_szStream2[g_offStream2] = '\0';
    Stream1Printf("%s", g_szStream2);
    Stream2Init();
    return 0;
}

/**
 * printf to the 2nd stream.
 */
static int Stream2Printf(const char *pszFormat, ...)
{
    unsigned offStream2 = g_offStream2;
    va_list va;
    va_start(va, pszFormat);
#if 1
    int cch = vsnprintf(&g_szStream2[offStream2], sizeof(g_szStream2) - offStream2, pszFormat, va);
#else
    int cch = vsprintf(&g_szStream2[offStream2], pszFormat, va);
#endif
    va_end(va);
    offStream2 += cch;
    if (offStream2 >= sizeof(g_szStream2))
    {
        fprintf(stderr, "error: stream2 overflow!\n");
        exit(1);
    }
    g_offStream2 = offStream2;
    return cch;
}


/**
 * Print the unidata.cpp file header and include list.
 */
static int PrintHeader(const char *argv0, const char *pszBaseDir)
{
    char szBuf[1024];
    if (!pszBaseDir)
    {
        memset(szBuf, 0, sizeof(szBuf));
#ifdef _MSC_VER
        if (!_getcwd(szBuf, sizeof(szBuf)))
#else
        if (!getcwd(szBuf, sizeof(szBuf)))
#endif
            return RTEXITCODE_FAILURE;
        pszBaseDir = szBuf;
    }

    const char *pszYear = __DATE__;
    pszYear += strlen(pszYear) - 4;

    Stream1Printf("/* $" "Id" "$ */\n"
                  "/** @file\n"
                  " * IPRT - Unicode Tables.\n"
                  " *\n"
                  " * Automatically Generated from %s\n"
                  " * by %s (" __DATE__ " " __TIME__ ")\n"
                  " */\n"
                  "\n"
                  "/*\n"
                  " * Copyright (C) 2006-%s Oracle and/or its affiliates.\n"
                  " *\n"
                  " * This file is part of VirtualBox base platform packages, as\n"
                  " * available from https://www.virtualbox.org.\n"
                  " *\n"
                  " * This program is free software; you can redistribute it and/or\n"
                  " * modify it under the terms of the GNU General Public License\n"
                  " * as published by the Free Software Foundation, in version 3 of the\n"
                  " * License.\n"
                  " *\n"
                  " * This program is distributed in the hope that it will be useful, but\n"
                  " * WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                  " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
                  " * General Public License for more details.\n"
                  " *\n"
                  " * You should have received a copy of the GNU General Public License\n"
                  " * along with this program; if not, see <https://www.gnu.org/licenses>.\n"
                  " *\n"
                  " * The contents of this file may alternatively be used under the terms\n"
                  " * of the Common Development and Distribution License Version 1.0\n"
                  " * (CDDL), a copy of it is provided in the \"COPYING.CDDL\" file included\n"
                  " * in the VirtualBox distribution, in which case the provisions of the\n"
                  " * CDDL are applicable instead of those of the GPL.\n"
                  " *\n"
                  " * You may elect to license modified versions of this file under the\n"
                  " * terms and conditions of either the GPL or the CDDL or both.\n"
                  " *\n"
                  " * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0\n"
                  " */\n"
                  "\n"
                  "#include <iprt/uni.h>\n"
                  "\n",
                  pszBaseDir, argv0, pszYear);
    return 0;
}


/**
 * Print the flag tables.
 */
static int PrintFlags(void)
{
    /*
     * Print flags table.
     */
    Stream2Init();
    Stream2Printf("RT_DECL_DATA_CONST(const RTUNIFLAGSRANGE) g_aRTUniFlagsRanges[] =\n"
                  "{\n");
    RTUNICP i = 0;
    int iStart = -1;
    while (i < RT_ELEMENTS(g_aCPInfo))
    {
        /* figure how far off the next chunk is */
        char szFlags[256];
        unsigned iNonNull = i;
        while (   iNonNull < RT_ELEMENTS(g_aCPInfo)
               && iNonNull >= 256
               && (g_aCPInfo[iNonNull].fNullEntry || !CalcFlags(&g_aCPInfo[iNonNull], szFlags)) )
            iNonNull++;
        if (iNonNull - i > 4096 || iNonNull == RT_ELEMENTS(g_aCPInfo))
        {
            if (iStart >= 0)
            {
                Stream1Printf("};\n\n");
                Stream2Printf("    { 0x%06x, 0x%06x, &g_afRTUniFlags0x%06x[0] },\n", iStart, i, iStart);
                iStart = -1;
            }
            i = iNonNull;
        }
        else
        {
            if (iStart < 0)
            {
                Stream1Printf("static const uint8_t g_afRTUniFlags0x%06x[] =\n"
                              "{\n", i);
                iStart = i;
            }
            CalcFlags(&g_aCPInfo[i], szFlags);
            Stream1Printf("    %50s, /* U+%06x: %s*/\n", szFlags, g_aCPInfo[i].CodePoint, g_aCPInfo[i].pszName);
            i++;
        }
    }
    Stream2Printf("    { ~(RTUNICP)0, ~(RTUNICP)0, NULL }\n"
                  "};\n\n\n");
    Stream1Printf("\n");
    return Stream2Flush();
}


/**
 * Prints the upper case tables.
 */
static int PrintUpper(void)
{
    Stream2Init();
    Stream2Printf("RT_DECL_DATA_CONST(const RTUNICASERANGE) g_aRTUniUpperRanges[] =\n"
                  "{\n");
    RTUNICP i = 0;
    int iStart = -1;
    while (i < RT_ELEMENTS(g_aCPInfo))
    {
        /* figure how far off the next chunk is */
        unsigned iSameCase = i;
        while (     iSameCase < RT_ELEMENTS(g_aCPInfo)
               &&   g_aCPInfo[iSameCase].SimpleUpperCaseMapping == g_aCPInfo[iSameCase].CodePoint
               &&   iSameCase >= 256)
            iSameCase++;
        if (iSameCase - i > 4096/sizeof(RTUNICP) || iSameCase == RT_ELEMENTS(g_aCPInfo))
        {
            if (iStart >= 0)
            {
                Stream1Printf("};\n\n");
                Stream2Printf("    { 0x%06x, 0x%06x, &g_afRTUniUpper0x%06x[0] },\n", iStart, i, iStart);
                iStart = -1;
            }
            i = iSameCase;
        }
        else
        {
            if (iStart < 0)
            {
                Stream1Printf("static const RTUNICP g_afRTUniUpper0x%06x[] =\n"
                              "{\n", i);
                iStart = i;
            }
            Stream1Printf("    0x%02x, /* U+%06x: %s*/\n", g_aCPInfo[i].SimpleUpperCaseMapping, g_aCPInfo[i].CodePoint, g_aCPInfo[i].pszName);
            i++;
        }
    }
    Stream2Printf("    { ~(RTUNICP)0, ~(RTUNICP)0, NULL }\n"
                  "};\n\n\n");
    Stream1Printf("\n");
    return Stream2Flush();
}


/**
 * Prints the lowercase tables.
 */
static int PrintLower(void)
{
    Stream2Init();
    Stream2Printf("RT_DECL_DATA_CONST(const RTUNICASERANGE) g_aRTUniLowerRanges[] =\n"
                  "{\n");
    RTUNICP i = 0;
    int iStart = -1;
    while (i < RT_ELEMENTS(g_aCPInfo))
    {
        /* figure how far off the next chunk is */
        unsigned iSameCase = i;
        while (     iSameCase < RT_ELEMENTS(g_aCPInfo)
               &&   g_aCPInfo[iSameCase].SimpleLowerCaseMapping == g_aCPInfo[iSameCase].CodePoint
               &&   iSameCase >= 256)
            iSameCase++;
        if (iSameCase - i > 4096/sizeof(RTUNICP) || iSameCase == RT_ELEMENTS(g_aCPInfo))
        {
            if (iStart >= 0)
            {
                Stream1Printf("};\n\n");
                Stream2Printf("    { 0x%06x, 0x%06x, &g_afRTUniLower0x%06x[0] },\n", iStart, i, iStart);
                iStart = -1;
            }
            i = iSameCase;
        }
        else
        {
            if (iStart < 0)
            {
                Stream1Printf("static const RTUNICP g_afRTUniLower0x%06x[] =\n"
                              "{\n", i);
                iStart = i;
            }
            Stream1Printf("    0x%02x, /* U+%06x: %s*/\n",
                          g_aCPInfo[i].SimpleLowerCaseMapping, g_aCPInfo[i].CodePoint, g_aCPInfo[i].pszName);
            i++;
        }
    }
    Stream2Printf("    { ~(RTUNICP)0, ~(RTUNICP)0, NULL }\n"
                  "};\n\n\n");
    Stream1Printf("\n");
    return Stream2Flush();
}


int main(int argc, char **argv)
{
    /*
     * Parse args.
     */
    if (argc <= 1)
    {
        printf("usage: %s [-C|--dir <UCD-dir>] [UnicodeData.txt [DerivedCoreProperties.txt [PropList.txt] [DerivedNormalizationProps.txt]]]\n",
                argv[0]);
        return 1;
    }

    const char *pszBaseDir                      = NULL;
    const char *pszUnicodeData                  = "UnicodeData.txt";
    const char *pszDerivedCoreProperties        = "DerivedCoreProperties.txt";
    const char *pszPropList                     = "PropList.txt";
    const char *pszDerivedNormalizationProps    = "DerivedNormalizationProps.txt";
    int iFile = 0;
    for (int argi = 1;  argi < argc; argi++)
    {
        if (argv[argi][0] != '-')
        {
            switch (iFile++)
            {
                case 0: pszUnicodeData                  = argv[argi]; break;
                case 1: pszDerivedCoreProperties        = argv[argi]; break;
                case 2: pszPropList                     = argv[argi]; break;
                case 3: pszDerivedNormalizationProps    = argv[argi]; break;
                default:
                    fprintf(stderr, "uniread: syntax error at '%s': too many filenames\n", argv[argi]);
                    return 1;
            }
        }
        else if (   !strcmp(argv[argi], "--dir")
                 || !strcmp(argv[argi], "-C"))
        {
            if (argi + 1 >= argc)
            {
                fprintf(stderr, "uniread: syntax error: '%s' is missing the directory name.\n", argv[argi]);
                return 1;
            }
            argi++;
            pszBaseDir = argv[argi];
        }
        else
        {
            fprintf(stderr, "uniread: syntax error at '%s': Unknown argument\n", argv[argi]);
            return 1;
        }
    }

    /*
     * Read the data.
     */
    int rc = ReadUnicodeData(pszBaseDir, pszUnicodeData);
    if (rc)
        return rc;
    rc = GenerateExcludedData();
    if (rc)
        return rc;
    rc = ReadProperties(pszBaseDir, pszPropList);
    if (rc)
        return rc;
    rc = ReadProperties(pszBaseDir, pszDerivedCoreProperties);
    if (rc)
        return rc;
    rc = ReadProperties(pszBaseDir, pszDerivedNormalizationProps);
    if (rc)
        return rc;

    /*
     * Produce output files.
     */
    rc = Stream1Init("unidata-flags.cpp");
    if (!rc)
        rc = PrintHeader(argv[0], pszBaseDir);
    if (!rc)
        rc = PrintFlags();

    rc = Stream1Init("unidata-upper.cpp");
    if (!rc)
        rc = PrintHeader(argv[0], pszBaseDir);
    if (!rc)
        rc = PrintUpper();

    rc = Stream1Init("unidata-lower.cpp");
    if (!rc)
        rc = PrintHeader(argv[0], pszBaseDir);
    if (!rc)
        rc = PrintLower();
    if (!rc)
        rc = Stream1Close();

    /* done */
    return rc;
}

