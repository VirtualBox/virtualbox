/* $Id: CUE.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CUE - CUE/BIN Disk image, Core Code.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_VD_CUE
#include <VBox/vd-plugin.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <VBox/scsiinline.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/alloc.h>
#include <iprt/cdefs.h>
#include <iprt/ctype.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include "VDBackends.h"
#include "VDBackendsInline.h"


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/**
 * CUE descriptor file token type.
 */
typedef enum CUETOKENTYPE
{
    /** Invalid token type. */
    CUETOKENTYPE_INVALID = 0,
    /** Reserved keyword. */
    CUETOKENTYPE_KEYWORD,
    /** String token. */
    CUETOKENTYPE_STRING,
    /** Unsigned integer. */
    CUETOKENTYPE_INTEGER_UNSIGNED,
    /** MSF (mm:ss:ff) location token. */
    CUETOKENTYPE_MSF,
    /** Error token (unexpected character found). */
    CUETOKENTYPE_ERROR,
    /** End of stream token. */
    CUETOKENTYPE_EOS
} CUETOKENTYPE;

/**
 * CUE reservered keyword type.
 */
typedef enum CUEKEYWORD
{
    /** Invalid keyword. */
    CUEKEYWORD_INVALID = 0,
    /** FILE. */
    CUEKEYWORD_FILE,
    /** BINARY */
    CUEKEYWORD_BINARY,
    /** MOTOROLA */
    CUEKEYWORD_MOTOROLA,
    /** WAVE */
    CUEKEYWORD_WAVE,
    /** MP3 */
    CUEKEYWORD_MP3,
    /** AIFF */
    CUEKEYWORD_AIFF,
    /** CATALOG */
    CUEKEYWORD_CATALOG,
    CUEKEYWORD_CDTEXTFILE,
    CUEKEYWORD_FLAGS,
    CUEKEYWORD_INDEX,
    CUEKEYWORD_ISRC,
    CUEKEYWORD_PERFORMER,
    CUEKEYWORD_POSTGAP,
    CUEKEYWORD_PREGAP,
    CUEKEYWORD_SONGWRITER,
    CUEKEYWORD_TITLE,
    CUEKEYWORD_TRACK,
    CUEKEYWORD_MODE1_2048,
    CUEKEYWORD_MODE1_2352,
    CUEKEYWORD_MODE2_2352,
    CUEKEYWORD_AUDIO,
    CUEKEYWORD_REM
} CUEKEYWORD;

/**
 * CUE sheet token.
 */
typedef struct CUETOKEN
{
    /** The token type. */
    CUETOKENTYPE        enmType;
    /** Token type dependent data. */
    union
    {
        /** Keyword token. */
        struct
        {
            /** The keyword enumerator. */
            CUEKEYWORD  enmKeyword;
        } Keyword;
        /** String token (without quotation marks). */
        struct
        {
            /** Pointer to the start of the string. */
            const char *psz;
            /** Number of characters for the string excluding the null terminator. */
            size_t      cch;
        } String;
        /** Integer token. */
        struct
        {
            /** Numerical constant. */
            uint64_t    u64;
        } Int;
        /** MSF location token. */
        struct
        {
            /** Minute part. */
            uint8_t     u8Minute;
            /** Second part. */
            uint8_t     u8Second;
            /** Frame part. */
            uint8_t     u8Frame;
        } Msf;
    } Type;
} CUETOKEN;
/** Pointer to a CUE sheet token. */
typedef CUETOKEN *PCUETOKEN;
/** Pointer to a const CUE sheet token. */
typedef const CUETOKEN *PCCUETOKEN;

/**
 * CUE tokenizer state.
 */
typedef struct CUETOKENIZER
{
    /** Char buffer to read from. */
    const char    *pszInput;
    /** Token 1. */
    CUETOKEN       Token1;
    /** Token 2. */
    CUETOKEN       Token2;
    /** Pointer to the current active token. */
    PCUETOKEN      pTokenCurr;
    /** The next token in the input stream (used for peeking). */
    PCUETOKEN      pTokenNext;
} CUETOKENIZER;
/** Pointer to a CUE tokenizer state. */
typedef CUETOKENIZER *PCUETOKENIZER;

/**
 * CUE keyword entry.
 */
typedef struct CUEKEYWORDDESC
{
    /** Keyword string. */
    const char          *pszKeyword;
    /** Size of the string in characters without zero terminator. */
    size_t               cchKeyword;
    /** Keyword type. */
    CUEKEYWORD           enmKeyword;
} CUEKEYWORDDESC;
/** Pointer to a CUE keyword entry. */
typedef CUEKEYWORDDESC *PCUEKEYWORDDESC;
/** Pointer to a const CUE keyword entry. */
typedef const CUEKEYWORDDESC *PCCUEKEYWORDDESC;

/**
 * CUE image data structure.
 */
typedef struct CUEIMAGE
{
    /** Image name. */
    const char          *pszFilename;
    /** Storage handle. */
    PVDIOSTORAGE        pStorage;
    /** The backing file containing the actual data. */
    char               *pszDataFilename;
    /** Storage handle for the backing file. */
    PVDIOSTORAGE        pStorageData;

    /** Pointer to the per-disk VD interface list. */
    PVDINTERFACE        pVDIfsDisk;
    /** Pointer to the per-image VD interface list. */
    PVDINTERFACE        pVDIfsImage;
    /** Error interface. */
    PVDINTERFACEERROR   pIfError;
    /** I/O interface. */
    PVDINTERFACEIOINT   pIfIo;

    /** Open flags passed by VD layer. */
    unsigned            uOpenFlags;
    /** Image flags defined during creation or determined during open. */
    unsigned            uImageFlags;
    /** Maximum number of tracks the region list can hold. */
    uint32_t            cTracksMax;
    /** Pointer to our internal region list. */
    PVDREGIONLIST       pRegionList;
    /** Flag whether the backing file is little (BINARY) or big (MOTOROLA) endian. */
    bool                fLittleEndian;
} CUEIMAGE, *PCUEIMAGE;


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/** NULL-terminated array of supported file extensions. */
static const VDFILEEXTENSION s_aCueFileExtensions[] =
{
    {"cue", VDTYPE_OPTICAL_DISC},
    {NULL, VDTYPE_INVALID}
};

/**
 * Known keywords.
 */
static const CUEKEYWORDDESC g_aCueKeywords[] =
{
    {RT_STR_TUPLE("FILE"),       CUEKEYWORD_FILE},
    {RT_STR_TUPLE("BINARY"),     CUEKEYWORD_BINARY},
    {RT_STR_TUPLE("MOTOROLA"),   CUEKEYWORD_MOTOROLA},
    {RT_STR_TUPLE("WAVE"),       CUEKEYWORD_WAVE},
    {RT_STR_TUPLE("MP3"),        CUEKEYWORD_MP3},
    {RT_STR_TUPLE("AIFF"),       CUEKEYWORD_AIFF},
    {RT_STR_TUPLE("CATALOG"),    CUEKEYWORD_CATALOG},
    {RT_STR_TUPLE("CDTEXTFILE"), CUEKEYWORD_CDTEXTFILE},
    {RT_STR_TUPLE("FLAGS"),      CUEKEYWORD_FLAGS},
    {RT_STR_TUPLE("INDEX"),      CUEKEYWORD_INDEX},
    {RT_STR_TUPLE("ISRC"),       CUEKEYWORD_ISRC},
    {RT_STR_TUPLE("PERFORMER"),  CUEKEYWORD_PERFORMER},
    {RT_STR_TUPLE("POSTGAP"),    CUEKEYWORD_POSTGAP},
    {RT_STR_TUPLE("PREGAP"),     CUEKEYWORD_PREGAP},
    {RT_STR_TUPLE("SONGWRITER"), CUEKEYWORD_SONGWRITER},
    {RT_STR_TUPLE("TITLE"),      CUEKEYWORD_TITLE},
    {RT_STR_TUPLE("TRACK"),      CUEKEYWORD_TRACK},
    {RT_STR_TUPLE("MODE1/2048"), CUEKEYWORD_MODE1_2048},
    {RT_STR_TUPLE("MODE1/2352"), CUEKEYWORD_MODE1_2352},
    {RT_STR_TUPLE("MODE2/2352"), CUEKEYWORD_MODE2_2352},
    {RT_STR_TUPLE("AUDIO"),      CUEKEYWORD_AUDIO},
    {RT_STR_TUPLE("REM"),        CUEKEYWORD_REM}
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Converts a MSF formatted address value read from the given buffer
 * to an LBA number. MSF 00:00:00 equals LBA 0.
 *
 * @returns The LBA number.
 * @param   pbBuf               The buffer to read the MSF formatted address
 *                              from.
 */
DECLINLINE(uint32_t) cueMSF2LBA(const uint8_t *pbBuf)
{
    return (pbBuf[0] * 60 + pbBuf[1]) * 75 + pbBuf[2];
}

/**
 * Ensures that the region list can hold up to the given number of tracks.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   cTracksMax    Maximum number of tracks.
 */
static int cueEnsureRegionListSize(PCUEIMAGE pThis, uint32_t cTracksMax)
{
    int rc = VINF_SUCCESS;

    if (pThis->cTracksMax < cTracksMax)
    {
        PVDREGIONLIST pRegionListNew = (PVDREGIONLIST)RTMemRealloc(pThis->pRegionList,
                                                                   RT_UOFFSETOF_DYN(VDREGIONLIST, aRegions[cTracksMax]));
        if (pRegionListNew)
        {
            /* Init all the new allocated tracks. */
            for (uint32_t i = pThis->cTracksMax; i < cTracksMax; i++)
                pRegionListNew->aRegions[i].offRegion = UINT64_MAX;
            pThis->pRegionList = pRegionListNew;
            pThis->cTracksMax  = cTracksMax;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}

/**
 * Returns whether the tokenizer reached the end of the stream.
 *
 * @returns true if the tokenizer reached the end of stream marker
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(bool) cueTokenizerIsEos(PCUETOKENIZER pTokenizer)
{
    return *pTokenizer->pszInput == '\0';
}

/**
 * Skip one character in the input stream.
 *
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(void) cueTokenizerSkipCh(PCUETOKENIZER pTokenizer)
{
    /* Never ever go past EOS. */
    if (!cueTokenizerIsEos(pTokenizer))
        pTokenizer->pszInput++;
}

/**
 * Returns the next char in the input buffer without advancing it.
 *
 * @returns Next character in the input buffer.
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(char) cueTokenizerPeekCh(PCUETOKENIZER pTokenizer)
{
    return   cueTokenizerIsEos(pTokenizer)
           ? '\0'
           : *(pTokenizer->pszInput + 1);
}

/**
 * Returns the next character in the input buffer advancing the internal
 * position.
 *
 * @returns Next character in the stream.
 * @param   pTokenizer     The tokenizer state.
 */
DECLINLINE(char) cueTokenizerGetCh(PCUETOKENIZER pTokenizer)
{
    char ch;

    if (cueTokenizerIsEos(pTokenizer))
        ch = '\0';
    else
        ch = *pTokenizer->pszInput;

    return ch;
}

/**
 * Sets a new line for the tokenizer.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   cSkip         How many characters to skip.
 */
DECLINLINE(void) cueTokenizerNewLine(PCUETOKENIZER pTokenizer, unsigned cSkip)
{
    pTokenizer->pszInput += cSkip;
}

/**
 * Checks whether the current position in the input stream is a new line
 * and skips it.
 *
 * @returns Flag whether there was a new line at the current position
 *          in the input buffer.
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(bool) cueTokenizerIsSkipNewLine(PCUETOKENIZER pTokenizer)
{
    bool fNewline = true;

    if (   cueTokenizerGetCh(pTokenizer) == '\r'
        && cueTokenizerPeekCh(pTokenizer) == '\n')
        cueTokenizerNewLine(pTokenizer, 2);
    else if (cueTokenizerGetCh(pTokenizer) == '\n')
        cueTokenizerNewLine(pTokenizer, 1);
    else
        fNewline = false;

    return fNewline;
}

/**
 * Skip all whitespace starting from the current input buffer position.
 * Skips all present comments too.
 *
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(void) cueTokenizerSkipWhitespace(PCUETOKENIZER pTokenizer)
{
    while (!cueTokenizerIsEos(pTokenizer))
    {
        while (   cueTokenizerGetCh(pTokenizer) == ' '
               || cueTokenizerGetCh(pTokenizer) == '\t')
            cueTokenizerSkipCh(pTokenizer);

        if (   !cueTokenizerIsEos(pTokenizer)
            && !cueTokenizerIsSkipNewLine(pTokenizer))
            break; /* Skipped everything, next is some real content. */
    }
}

/**
 * Skips a multi line comment.
 *
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(void) cueTokenizerSkipComment(PCUETOKENIZER pTokenizer)
{
    while (   !cueTokenizerIsEos(pTokenizer)
           && !cueTokenizerIsSkipNewLine(pTokenizer))
        cueTokenizerSkipCh(pTokenizer);
    cueTokenizerSkipWhitespace(pTokenizer);
}

/**
 * Get an identifier token from the tokenizer.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   pToken        The uninitialized token.
 */
static void cueTokenizerGetKeyword(PCUETOKENIZER pTokenizer, PCUETOKEN pToken)
{
    char ch;
    unsigned cchKeyword = 0;
    bool fIsKeyword = false;
    bool fIsComment;
    const char *pszKeyword;

    Assert(RT_C_IS_ALPHA(*pTokenizer->pszInput));

    do
    {
        fIsComment = false;
        pszKeyword = pTokenizer->pszInput;

        do
        {
            cchKeyword++;
            cueTokenizerSkipCh(pTokenizer);
            ch = cueTokenizerGetCh(pTokenizer);
        }
        while (RT_C_IS_ALNUM(ch) || ch == '_' || ch == '/' || ch == '.');

        /* Check whether we got a keyword or a string constant. */
        for (unsigned i = 0; i < RT_ELEMENTS(g_aCueKeywords); i++)
        {
            if (!RTStrNCmp(g_aCueKeywords[i].pszKeyword, pszKeyword, RT_MIN(cchKeyword, g_aCueKeywords[i].cchKeyword)))
            {
                if (g_aCueKeywords[i].enmKeyword == CUEKEYWORD_REM)
                {
                    /* The REM keyword is handled here as it indicates a comment which we just skip. */
                    cueTokenizerSkipComment(pTokenizer);
                    fIsComment = true;
                }
                else
                {
                    fIsKeyword = true;
                    pToken->enmType = CUETOKENTYPE_KEYWORD;
                    pToken->Type.Keyword.enmKeyword = g_aCueKeywords[i].enmKeyword;
                }
                break;
            }
        }
    } while (fIsComment);

    /* Make it a string. */
    if (ch == '\0')
        pToken->enmType = CUETOKENTYPE_EOS;
    else if (!fIsKeyword)
    {
        pToken->enmType = CUETOKENTYPE_STRING;
        pToken->Type.String.psz = pszKeyword;
        pToken->Type.String.cch = cchKeyword;
    }
}

/**
 * Get an integer value or MSF location indicator from the tokenizer.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   pToken        The uninitialized token.
 */
static void cueTokenizerGetIntegerOrMsf(PCUETOKENIZER pTokenizer, PCUETOKEN pToken)
{
    char szInt[20 + 1]; /* Maximum which fits into an unsigned 64bit integer + zero terminator. */
    unsigned cchInt = 0;
    bool fMsf = false;
    char ch = cueTokenizerGetCh(pTokenizer);

    Assert(RT_C_IS_DIGIT(ch));
    RT_ZERO(szInt);

    /* Go through the characters and check for the : mark which denotes MSF location indicator. */
    do
    {
        szInt[cchInt++] = ch;
        cueTokenizerSkipCh(pTokenizer);
        ch = cueTokenizerGetCh(pTokenizer);
        if (ch == ':')
            fMsf = true;
    }
    while (   (RT_C_IS_DIGIT(ch) || ch == ':')
           && cchInt < sizeof(szInt));

    if (cchInt < sizeof(szInt) - 1)
    {
        if (fMsf)
        {
            /* Check that the format matches our expectations (mm:ss:ff). */
            if (   cchInt == 8 && szInt[2] == ':' && szInt[5] == ':')
            {
                /* Parse the single fields. */
                szInt[2] = '\0';
                szInt[5] = '\0';

                int rc = RTStrToUInt8Full(&szInt[0], 10, &pToken->Type.Msf.u8Minute);
                if (RT_SUCCESS(rc))
                    rc = RTStrToUInt8Full(&szInt[3], 10, &pToken->Type.Msf.u8Second);
                if (RT_SUCCESS(rc))
                    rc = RTStrToUInt8Full(&szInt[6], 10, &pToken->Type.Msf.u8Frame);
                if (RT_SUCCESS(rc))
                    pToken->enmType = CUETOKENTYPE_MSF;
                else
                    pToken->enmType = CUETOKENTYPE_ERROR;
            }
            else
                pToken->enmType = CUETOKENTYPE_ERROR;
        }
        else
        {
            pToken->enmType = CUETOKENTYPE_INTEGER_UNSIGNED;
            int rc = RTStrToUInt64Full(&szInt[0], 10, &pToken->Type.Int.u64);
            if (RT_FAILURE(rc))
                pToken->enmType = CUETOKENTYPE_ERROR;
        }
    }
    else
        pToken->enmType = CUETOKENTYPE_ERROR;
}

/**
 * Parses a string constant.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   pToken        The uninitialized token.
 *
 * @remarks: No escape sequences allowed at this time.
 */
static void cueTokenizerGetStringConst(PCUETOKENIZER pTokenizer, PCUETOKEN pToken)
{
    unsigned cchStr = 0;

    Assert(cueTokenizerGetCh(pTokenizer) == '\"');
    cueTokenizerSkipCh(pTokenizer); /* Skip " */

    pToken->enmType = CUETOKENTYPE_STRING;
    pToken->Type.String.psz = pTokenizer->pszInput;

    while (   !cueTokenizerIsEos(pTokenizer)
           && cueTokenizerGetCh(pTokenizer) != '\"')
    {
        cchStr++;
        cueTokenizerSkipCh(pTokenizer);
    }

    /* End of stream without a closing quote is an error. */
    if (RT_UNLIKELY(cueTokenizerIsEos(pTokenizer)))
        pToken->enmType = CUETOKENTYPE_ERROR;
    else
    {
        cueTokenizerSkipCh(pTokenizer); /* Skip closing " */
        pToken->Type.String.cch = cchStr;
    }
}

/**
 * Get the end of stream token.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   pToken        The uninitialized token.
 */
static void cueTokenizerGetEos(PCUETOKENIZER pTokenizer, PCUETOKEN pToken)
{
    Assert(cueTokenizerGetCh(pTokenizer) == '\0'); RT_NOREF(pTokenizer);

    pToken->enmType = CUETOKENTYPE_EOS;
}

/**
 * Read the next token from the tokenizer stream.
 *
 * @param   pTokenizer    The tokenizer to read from.
 * @param   pToken        Uninitialized token to fill the token data into.
 */
static void cueTokenizerReadNextToken(PCUETOKENIZER pTokenizer, PCUETOKEN pToken)
{
    /* Skip all eventually existing whitespace, newlines and comments first. */
    cueTokenizerSkipWhitespace(pTokenizer);

    char ch = cueTokenizerGetCh(pTokenizer);
    if (RT_C_IS_ALPHA(ch))
        cueTokenizerGetKeyword(pTokenizer, pToken);
    else if (RT_C_IS_DIGIT(ch))
        cueTokenizerGetIntegerOrMsf(pTokenizer, pToken);
    else if (ch == '\"')
        cueTokenizerGetStringConst(pTokenizer, pToken);
    else if (ch == '\0')
        cueTokenizerGetEos(pTokenizer, pToken);
    else
        pToken->enmType = CUETOKENTYPE_ERROR;
}

/**
 * Create a new tokenizer.
 *
 * @returns Pointer to the new tokenizer state on success.
 *          NULL if out of memory.
 * @param   pszInput    The input to create the tokenizer for.
 */
static PCUETOKENIZER cueTokenizerCreate(const char *pszInput)
{
    PCUETOKENIZER pTokenizer = (PCUETOKENIZER)RTMemAllocZ(sizeof(CUETOKENIZER));
    if (pTokenizer)
    {
        pTokenizer->pszInput     = pszInput;
        pTokenizer->pTokenCurr   = &pTokenizer->Token1;
        pTokenizer->pTokenNext   = &pTokenizer->Token2;
        /* Fill the tokenizer with two first tokens. */
        cueTokenizerReadNextToken(pTokenizer, pTokenizer->pTokenCurr);
        cueTokenizerReadNextToken(pTokenizer, pTokenizer->pTokenNext);
    }

    return pTokenizer;
}

/**
 * Get the current token in the input stream.
 *
 * @returns Pointer to the next token in the stream.
 * @param   pTokenizer    The tokenizer to destroy.
 */
DECLINLINE(PCCUETOKEN) cueTokenizerGetToken(PCUETOKENIZER pTokenizer)
{
    return pTokenizer->pTokenCurr;
}

/**
 * Get the class of the current token.
 *
 * @returns Class of the current token.
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(CUETOKENTYPE) cueTokenizerGetTokenType(PCUETOKENIZER pTokenizer)
{
    return pTokenizer->pTokenCurr->enmType;
}

/**
 * Consume the current token advancing to the next in the stream.
 *
 * @param   pTokenizer    The tokenizer state.
 */
static void cueTokenizerConsume(PCUETOKENIZER pTokenizer)
{
    PCUETOKEN  pTokenTmp = pTokenizer->pTokenCurr;

    /* Switch next token to current token and read in the next token. */
    pTokenizer->pTokenCurr = pTokenizer->pTokenNext;
    pTokenizer->pTokenNext = pTokenTmp;
    cueTokenizerReadNextToken(pTokenizer, pTokenizer->pTokenNext);
}

/**
 * Check whether the next token in the input stream is a keyword and matches the given
 * keyword.
 *
 * @returns true if the token matched.
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 * @param   enmKeyword    The keyword to check against.
 */
static bool cueTokenizerIsKeywordEqual(PCUETOKENIZER pTokenizer, CUEKEYWORD enmKeyword)
{
    PCCUETOKEN pToken = cueTokenizerGetToken(pTokenizer);

    if (   pToken->enmType == CUETOKENTYPE_KEYWORD
        && pToken->Type.Keyword.enmKeyword == enmKeyword)
        return true;

    return false;
}

/**
 * Check whether the next token in the input stream is a keyword and matches the given
 * keyword and skips it.
 *
 * @returns true if the token matched and was skipped.
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 * @param   enmKeyword    The keyword to check against.
 */
static bool cueTokenizerSkipIfIsKeywordEqual(PCUETOKENIZER pTokenizer, CUEKEYWORD enmKeyword)
{
    bool fEqual = cueTokenizerIsKeywordEqual(pTokenizer, enmKeyword);
    if (fEqual)
        cueTokenizerConsume(pTokenizer);

    return fEqual;
}

/**
 * Duplicates the string of the current token and consumes it.
 *
 * @returns VBox status code.
 * @param   pTokenizer    The tokenizer state.
 * @param   ppszStr       Where to store the pointer to the duplicated string on success.
 *                        Free with RTStrFree().
 */
static int cueTokenizerConsumeStringDup(PCUETOKENIZER pTokenizer, char **ppszStr)
{
    int rc = VINF_SUCCESS;
    Assert(cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_STRING);

    *ppszStr = RTStrDupN(pTokenizer->pTokenCurr->Type.String.psz,
                         pTokenizer->pTokenCurr->Type.String.cch);
    if (!*ppszStr)
        rc = VERR_NO_STR_MEMORY;

    cueTokenizerConsume(pTokenizer);
    return rc;
}

/**
 * Consumes an integer token returning the value.
 *
 * @returns Integer value in the token.
 * @param   pTokenizer    The tokenizer state.
 */
static uint64_t cueTokenizerConsumeInteger(PCUETOKENIZER pTokenizer)
{
    uint64_t u64 = 0;
    Assert(cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_INTEGER_UNSIGNED);

    u64 = pTokenizer->pTokenCurr->Type.Int.u64;
    cueTokenizerConsume(pTokenizer);
    return u64;
}

/**
 * Parses and skips the remaining string part of a directive.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   pTokenizer    The tokenizer state.
 * @param   pszDirective  The directive we skip the string part for.
 */
static int cueParseAndSkipStringRemainder(PCUEIMAGE pThis, PCUETOKENIZER pTokenizer,
                                          const char *pszDirective)
{
    int rc = VINF_SUCCESS;

    if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_STRING)
        cueTokenizerConsume(pTokenizer);
    else
        rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                       N_("CUE: Error parsing '%s', expected string for %s directive"), pThis->pszFilename,
                       pszDirective);

    return rc;
}

/**
 * Parses and skips the remaining MSF part of a directive.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   pTokenizer    The tokenizer state.
 * @param   pszDirective  The directive we skip the string part for.
 */
static int cueParseAndSkipMsfRemainder(PCUEIMAGE pThis, PCUETOKENIZER pTokenizer,
                                          const char *pszDirective)
{
    int rc = VINF_SUCCESS;

    if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_MSF)
        cueTokenizerConsume(pTokenizer);
    else
        rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                       N_("CUE: Error parsing '%s', expected MSF location for %s directive"), pThis->pszFilename,
                       pszDirective);

    return rc;
}

/**
 * Parses the remainder of a INDEX directive.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   pTokenizer    The tokenizer state.
 * @param   pu8Index      Where to store the parsed index number on success.
 * @param   pu64Lba       Where to store the parsed positional information on success.
 */
static int cueParseIndex(PCUEIMAGE pThis, PCUETOKENIZER pTokenizer,
                         uint8_t *pu8Index, uint64_t *pu64Lba)
{
    int rc = VINF_SUCCESS;

    /*
     * The index consists of the index number and positional information in MSF format.
     */
    if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_INTEGER_UNSIGNED)
    {
        uint64_t u64Index = cueTokenizerConsumeInteger(pTokenizer);
        if (u64Index <= 99)
        {
            /* Parse the position. */
            if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_MSF)
            {
                PCCUETOKEN pToken = cueTokenizerGetToken(pTokenizer);
                uint8_t abMsf[3];
                abMsf[0] = pToken->Type.Msf.u8Minute;
                abMsf[1] = pToken->Type.Msf.u8Second;
                abMsf[2] = pToken->Type.Msf.u8Frame;

                *pu8Index = (uint8_t)u64Index;
                *pu64Lba  = cueMSF2LBA(&abMsf[0]);
                cueTokenizerConsume(pTokenizer);
            }
            else
                rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                               N_("CUE: Error parsing '%s', expected MSF location"), pThis->pszFilename);
        }
        else
            rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                           N_("CUE: Error parsing '%s', index number must be between 01 and 99"), pThis->pszFilename);
    }
    else
        rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                       N_("CUE: Error parsing '%s', expected index number after INDEX directive"), pThis->pszFilename);

    return rc;
}

/**
 * Parses the things coming below a TRACK directive.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   pTokenizer    The tokenizer state.
 * @param   pu64LbaStart  Where to store the starting LBA for this track on success.
 */
static int cueParseTrackNesting(PCUEIMAGE pThis, PCUETOKENIZER pTokenizer, uint64_t *pu64LbaStart)
{
    int rc = VINF_SUCCESS;
    bool fSeenInitialIndex = false;

    do
    {
        if (   cueTokenizerIsKeywordEqual(pTokenizer, CUEKEYWORD_TRACK)
            || cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_EOS)
            break;

        if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_KEYWORD)
        {
            if (cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_TITLE))
                rc = cueParseAndSkipStringRemainder(pThis, pTokenizer, "TITLE");
            else if (cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_PERFORMER))
                rc = cueParseAndSkipStringRemainder(pThis, pTokenizer, "PERFORMER");
            else if (cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_PREGAP))
                rc = cueParseAndSkipMsfRemainder(pThis, pTokenizer, "PREGAP");
            else if (cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_POSTGAP))
                rc = cueParseAndSkipMsfRemainder(pThis, pTokenizer, "POSTGAP");
            else if (cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_INDEX))
            {
                uint8_t u8Index = 0;
                uint64_t u64Lba = 0;
                rc = cueParseIndex(pThis, pTokenizer, &u8Index, &u64Lba);
                if (   RT_SUCCESS(rc)
                    && u8Index == 1)
                {
                    if (!fSeenInitialIndex)
                    {
                        fSeenInitialIndex = true;
                        *pu64LbaStart = u64Lba;
                    }
                    else
                        rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                                       N_("CUE: Error parsing '%s', multiple INDEX 01 directives"), pThis->pszFilename);
                }
            }
            else
                rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                               N_("CUE: Error parsing '%s', unexpected directive for TRACK found"), pThis->pszFilename);
        }
        else
            rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                           N_("CUE: Error parsing '%s', expected a CUE sheet keyword"), pThis->pszFilename);
    }
    while (RT_SUCCESS(rc));

    if (   RT_SUCCESS(rc)
        && !fSeenInitialIndex)
        rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                           N_("CUE: Error parsing '%s', no initial INDEX directive for this track"), pThis->pszFilename);

    return rc;
}

/**
 * Parses the remainder of a TRACK directive.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   pTokenizer    The tokenizer state.
 */
static int cueParseTrack(PCUEIMAGE pThis, PCUETOKENIZER pTokenizer)
{
    int rc = VINF_SUCCESS;

    /*
     * A track consists of the track number and data type followed by a list of indexes
     * and other metadata like title and performer we don't care about.
     */
    if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_INTEGER_UNSIGNED)
    {
        uint64_t u64Track = cueTokenizerConsumeInteger(pTokenizer);
        if (u64Track <= 99)
        {
            /* Parse the data mode. */
            if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_KEYWORD)
            {
                CUEKEYWORD enmDataMode = pTokenizer->pTokenCurr->Type.Keyword.enmKeyword;
                if (   cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_AUDIO)
                    || cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_MODE1_2048)
                    || cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_MODE1_2352)
                    || cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_MODE2_2352))
                {
                    /*
                     * Parse everything coming below the track (index points, etc.), we only need to find
                     * the starting point.
                     */
                    uint64_t uLbaStart = 0;
                    rc = cueParseTrackNesting(pThis, pTokenizer, &uLbaStart);
                    if (RT_SUCCESS(rc))
                    {
                        /* Create a new region for this track. */
                        RT_NOREF1(enmDataMode);
                        rc = cueEnsureRegionListSize(pThis, u64Track);
                        if (RT_SUCCESS(rc))
                        {
                            PVDREGIONDESC pRegion = &pThis->pRegionList->aRegions[u64Track - 1];
                            pRegion->offRegion = uLbaStart;
                            if (enmDataMode == CUEKEYWORD_MODE1_2352)
                            {
                                pRegion->cbBlock     = 2352;
                                pRegion->enmDataForm = VDREGIONDATAFORM_MODE1_2352;
                            }
                            else if (enmDataMode == CUEKEYWORD_MODE2_2352)
                            {
                                pRegion->cbBlock     = 2352;
                                pRegion->enmDataForm = VDREGIONDATAFORM_MODE2_2352;
                            }
                            else if (enmDataMode == CUEKEYWORD_AUDIO)
                            {
                                pRegion->cbBlock = 2352;
                                pRegion->enmDataForm = VDREGIONDATAFORM_CDDA;
                            }
                            else
                            {
                                pRegion->cbBlock = 2048;
                                pRegion->enmDataForm = VDREGIONDATAFORM_MODE1_2048;
                            }
                            pRegion->enmMetadataForm = VDREGIONMETADATAFORM_NONE;
                            pRegion->cbData          = pRegion->cbBlock;
                            pRegion->cbMetadata      = 0;
                        }
                        else
                            rc = vdIfError(pThis->pIfError, rc, RT_SRC_POS,
                                           N_("CUE: Failed to allocate memory for the track list for '%s'"),
                                           pThis->pszFilename);
                    }
                }
                else
                    rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                                   N_("CUE: Error parsing '%s', the data mode is not supported"), pThis->pszFilename);
            }
            else
                rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                               N_("CUE: Error parsing '%s', expected data mode"), pThis->pszFilename);
        }
        else
            rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                           N_("CUE: Error parsing '%s', track number must be between 01 and 99"), pThis->pszFilename);
    }
    else
        rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                       N_("CUE: Error parsing '%s', expected track number after TRACK directive"), pThis->pszFilename);

    return rc;
}

/**
 * Parses a list of tracks which must come after a FILE directive.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   pTokenizer    The tokenizer state.
 */
static int cueParseTrackList(PCUEIMAGE pThis, PCUETOKENIZER pTokenizer)
{
    int rc = VINF_SUCCESS;

    /*
     * Sometimes there is a TITLE/PERFORMER/SONGWRITER directive before the start of the track list,
     * skip and ignore those.
     */
    while (   RT_SUCCESS(rc)
           && (   cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_TITLE)
               || cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_PERFORMER)
               || cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_SONGWRITER)))
        rc = cueParseAndSkipStringRemainder(pThis, pTokenizer, "TITLE/PERFORMER/SONGWRITER");

    while (   RT_SUCCESS(rc)
           && cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_TRACK))
        rc = cueParseTrack(pThis, pTokenizer);

    return rc;
}

/**
 * Parses the remainder of a FILE directive.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   pTokenizer    The tokenizer state.
 */
static int cueParseFile(PCUEIMAGE pThis, PCUETOKENIZER pTokenizer)
{
    int rc = VINF_SUCCESS;

    /* First must come a string constant followed by a keyword giving the file type. */
    if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_STRING)
    {
        rc = cueTokenizerConsumeStringDup(pTokenizer, &pThis->pszDataFilename);
        if (RT_SUCCESS(rc))
        {
            if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_KEYWORD)
            {
                if (cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_BINARY))
                {
                    pThis->fLittleEndian = true;
                    rc = cueParseTrackList(pThis, pTokenizer);
                }
                else if (cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_MOTOROLA))
                {
                    pThis->fLittleEndian = false;
                    rc = cueParseTrackList(pThis, pTokenizer);
                }
                else
                    rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                                   N_("CUE: Error parsing '%s', the file type is not supported (only BINARY)"), pThis->pszFilename);
            }
            else
                rc = vdIfError(pThis->pIfError, rc, RT_SRC_POS,
                               N_("CUE: Error parsing '%s', expected file type"), pThis->pszFilename);
        }
        else
            rc = vdIfError(pThis->pIfError, rc, RT_SRC_POS,
                           N_("CUE: Error parsing '%s', failed to allocate memory for filename"), pThis->pszFilename);
    }
    else
        rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                       N_("CUE: Error parsing '%s', expected filename after FILE directive"), pThis->pszFilename);

    return rc;
}

/**
 * Parses the keyword in the given tokenizer.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   pTokenizer    The tokenizer state.
 */
static int cueParseKeyword(PCUEIMAGE pThis, PCUETOKENIZER pTokenizer)
{
    int rc = VINF_SUCCESS;

    if (cueTokenizerSkipIfIsKeywordEqual(pTokenizer, CUEKEYWORD_FILE))
        rc = cueParseFile(pThis, pTokenizer);
    else /* Skip all other keywords we don't need/support. */
        cueTokenizerConsume(pTokenizer);

    return rc;
}


/**
 * Parses the CUE sheet from the given tokenizer.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   pTokenizer    The tokenizer state.
 */
static int cueParseFromTokenizer(PCUEIMAGE pThis, PCUETOKENIZER pTokenizer)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%p\n", pThis));

    /* We don't support multiple FILE directives for now. */
    if (cueTokenizerGetTokenType(pTokenizer) == CUETOKENTYPE_KEYWORD)
        rc = cueParseKeyword(pThis, pTokenizer);
    else
        rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                       N_("CUE: Error parsing '%s', expected a keyword"), pThis->pszFilename);

    if (   RT_SUCCESS(rc)
        && cueTokenizerGetTokenType(pTokenizer) != CUETOKENTYPE_EOS)
        rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                       N_("CUE: Error parsing '%s', expected end of stream"), pThis->pszFilename);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Finalizes the track list of the image.
 *
 * @returns VBox status code.
 * @param   pThis         The CUE image state.
 * @param   cbImage       Size of the image data in bytes.
 */
static int cueTrackListFinalize(PCUEIMAGE pThis, uint64_t cbImage)
{
    int rc = VINF_SUCCESS;

    if (   pThis->cTracksMax == 0
        || pThis->pRegionList->aRegions[0].offRegion == UINT64_MAX)
        return vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                         N_("CUE: Error parsing '%s', detected empty track list"), pThis->pszFilename);

    /*
     * Fixup the track list to contain the proper sizes now that we parsed all tracks,
     * check also that there are no gaps in the list.
     */
    uint32_t cTracks = 1;
    uint64_t offDisk = 0;
    for (uint32_t i = 1; i < pThis->cTracksMax; i++)
    {
        PVDREGIONDESC pRegion = &pThis->pRegionList->aRegions[i];
        PVDREGIONDESC pRegionPrev = &pThis->pRegionList->aRegions[i - 1];
        if (pRegion->offRegion != UINT64_MAX)
        {
            cTracks++;
            uint64_t cBlocks = pRegion->offRegion - (pRegionPrev->offRegion / pRegionPrev->cbBlock);
            pRegionPrev->cRegionBlocksOrBytes = pRegionPrev->cbBlock * cBlocks;
            offDisk += pRegionPrev->cRegionBlocksOrBytes;

            if (cbImage < pRegionPrev->cRegionBlocksOrBytes)
                return vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                                 N_("CUE: Error parsing '%s', image file is too small for track list"),
                                 pThis->pszFilename);

            cbImage -= pRegionPrev->cRegionBlocksOrBytes;
            pRegion->offRegion = offDisk;
        }
        else
            break;
    }

    /* Fixup last track. */
    PVDREGIONDESC pRegion = &pThis->pRegionList->aRegions[cTracks - 1];
    pRegion->cRegionBlocksOrBytes = cbImage;

    pThis->pRegionList->cRegions = cTracks;
    pThis->pRegionList->fFlags   = 0;

    /* Check that there are no gaps in the track list. */
    for (uint32_t i = cTracks; cTracks < pThis->cTracksMax; i++)
    {
        pRegion = &pThis->pRegionList->aRegions[i];
        if (pRegion->offRegion != UINT64_MAX)
        {
            rc = vdIfError(pThis->pIfError, VERR_NOT_SUPPORTED, RT_SRC_POS,
                           N_("CUE: Error parsing '%s', detected gaps in the track list"), pThis->pszFilename);
            break;
        }
    }

    return rc;
}

/**
 * Internal. Free all allocated space for representing an image except pThis,
 * and optionally delete the image from disk.
 */
static int cueFreeImage(PCUEIMAGE pThis, bool fDelete)
{
    int rc = VINF_SUCCESS;

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pThis)
    {
        if (pThis->pStorage)
        {
            rc = vdIfIoIntFileClose(pThis->pIfIo, pThis->pStorage);
            pThis->pStorage = NULL;
        }

        if (pThis->pStorageData)
        {
            rc = vdIfIoIntFileClose(pThis->pIfIo, pThis->pStorageData);
            pThis->pStorageData = NULL;
        }

        if (pThis->pRegionList)
        {
            RTMemFree(pThis->pRegionList);
            pThis->pRegionList = NULL;
        }

        if (pThis->pszDataFilename)
        {
            RTStrFree(pThis->pszDataFilename);
            pThis->pszDataFilename = NULL;
        }

        if (fDelete && pThis->pszFilename)
            vdIfIoIntFileDelete(pThis->pIfIo, pThis->pszFilename);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Internal: Open an image, constructing all necessary data structures.
 */
static int cueOpenImage(PCUEIMAGE pThis, unsigned uOpenFlags)
{
    pThis->uOpenFlags = uOpenFlags;

    pThis->pIfError = VDIfErrorGet(pThis->pVDIfsDisk);
    pThis->pIfIo = VDIfIoIntGet(pThis->pVDIfsImage);
    AssertPtrReturn(pThis->pIfIo, VERR_INVALID_PARAMETER);

    /* Open the image. */
    int rc = vdIfIoIntFileOpen(pThis->pIfIo, pThis->pszFilename,
                               VDOpenFlagsToFileOpenFlags(uOpenFlags,
                                                          false /* fCreate */),
                               &pThis->pStorage);
    if (RT_SUCCESS(rc))
    {
        uint64_t cbFile;
        /* The descriptor file shouldn't be huge, so limit ourselfs to 16KB for now. */
        rc = vdIfIoIntFileGetSize(pThis->pIfIo, pThis->pStorage, &cbFile);
        if (   RT_SUCCESS(rc)
            && cbFile <= _16K - 1)
        {
            char szInput[_16K];
            RT_ZERO(szInput);

            rc = vdIfIoIntFileReadSync(pThis->pIfIo, pThis->pStorage, 0,
                                       &szInput, cbFile);
            if (RT_SUCCESS(rc))
            {
                RTStrPurgeEncoding(&szInput[0]);
                PCUETOKENIZER pTokenizer = cueTokenizerCreate(&szInput[0]);
                if (pTokenizer)
                {
                    rc = cueParseFromTokenizer(pThis, pTokenizer);
                    RTMemFree(pTokenizer);
                    if (RT_SUCCESS(rc))
                    {
                        /* Open the backing file. */
                        char szBackingFile[RTPATH_MAX];
                        rc = RTStrCopy(&szBackingFile[0], sizeof(szBackingFile), pThis->pszFilename);
                        if (RT_SUCCESS(rc))
                        {
                            RTPathStripFilename(&szBackingFile[0]);
                            rc = RTPathAppend(&szBackingFile[0], sizeof(szBackingFile), pThis->pszDataFilename);
                            if (RT_SUCCESS(rc))
                            {
                                rc = vdIfIoIntFileOpen(pThis->pIfIo, szBackingFile,
                                                       VDOpenFlagsToFileOpenFlags(uOpenFlags,
                                                                                  false /* fCreate */),
                                                       &pThis->pStorageData);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = vdIfIoIntFileGetSize(pThis->pIfIo, pThis->pStorageData, &cbFile);
                                    if (RT_SUCCESS(rc))
                                        rc = cueTrackListFinalize(pThis, cbFile);
                                    else
                                        rc = vdIfError(pThis->pIfError, rc, RT_SRC_POS,
                                                       N_("CUE: Unable to query size of backing file '%s'"),
                                                       szBackingFile);
                                }
                                else
                                    rc = vdIfError(pThis->pIfError, rc, RT_SRC_POS,
                                                   N_("CUE: Unable to open backing file '%s'"),
                                                   szBackingFile);
                            }
                            else
                                rc = vdIfError(pThis->pIfError, rc, RT_SRC_POS,
                                               N_("CUE: Error constructing backing filename from '%s'"),
                                               pThis->pszFilename);
                        }
                        else
                            rc = vdIfError(pThis->pIfError, rc, RT_SRC_POS,
                                           N_("CUE: Error constructing backing filename from '%s'"),
                                           pThis->pszFilename);
                    }
                }
            }
            else
                rc = vdIfError(pThis->pIfError, rc, RT_SRC_POS, N_("CUE: Error reading '%s'"), pThis->pszFilename);
        }
        else if (RT_SUCCESS(rc))
            rc = vdIfError(pThis->pIfError, VERR_VD_INVALID_SIZE,
                           RT_SRC_POS, N_("CUE: The descriptor file '%s' is too huge (%llu vs %llu)"),
                           pThis->pszFilename, cbFile, _16K - 1);
    }
    /* else: Do NOT signal an appropriate error here, as the VD layer has the
     *       choice of retrying the open if it failed. */

    if (RT_FAILURE(rc))
        cueFreeImage(pThis, false);
    return rc;
}

/**
 * Converts the data form enumeration to a string.
 *
 * @returns String name of the given data form.
 * @param   enmDataForm   The data form.
 */
static const char *cueRegionDataFormStringify(VDREGIONDATAFORM enmDataForm)
{
    switch (enmDataForm)
    {
        #define DATAFORM2STR(tag) case VDREGIONDATAFORM_##tag: return #tag

        DATAFORM2STR(INVALID);
        DATAFORM2STR(RAW);
        DATAFORM2STR(CDDA);
        DATAFORM2STR(CDDA_PAUSE);
        DATAFORM2STR(MODE1_2048);
        DATAFORM2STR(MODE1_2352);
        DATAFORM2STR(MODE1_0);
        DATAFORM2STR(XA_2336);
        DATAFORM2STR(XA_2352);
        DATAFORM2STR(XA_0);
        DATAFORM2STR(MODE2_2336);
        DATAFORM2STR(MODE2_2352);
        DATAFORM2STR(MODE2_0);

        #undef DATAFORM2STR

        default:
        {
            AssertMsgFailed(("Unknown data form %d! forgot to add it to the switch?\n", enmDataForm));
            return "UNKNOWN!";
        }
    }
}

/**
 * Converts the data form enumeration to a string.
 *
 * @returns String name of the given data form.
 * @param   enmMetadataForm   The metadata form.
 */
static const char *cueRegionMetadataFormStringify(VDREGIONMETADATAFORM enmMetadataForm)
{
    switch (enmMetadataForm)
    {
        #define METADATAFORM2STR(tag) case VDREGIONMETADATAFORM_##tag: return #tag

        METADATAFORM2STR(INVALID);
        METADATAFORM2STR(RAW);
        METADATAFORM2STR(NONE);

        #undef METADATAFORM2STR

        default:
        {
            AssertMsgFailed(("Unknown metadata form %d! forgot to add it to the switch?\n", enmMetadataForm));
            return "UNKNOWN!";
        }
    }
}

/**
 * Returns the region containing the given offset.
 *
 * @returns Pointer to the region or NULL if not found.
 * @param   pThis         The CUE image state.
 * @param   uOffset       The offset to look for.
 */
static PCVDREGIONDESC cueRegionQueryByOffset(PCUEIMAGE pThis, uint64_t uOffset)
{
    for (uint32_t i = 0; i < pThis->pRegionList->cRegions; i++)
    {
        PCVDREGIONDESC pRegion = &pThis->pRegionList->aRegions[i];
        if (   pRegion->offRegion <= uOffset
            && pRegion->offRegion + pRegion->cRegionBlocksOrBytes > uOffset)
            return pRegion;
    }

    return NULL;
}

/** @copydoc VDIMAGEBACKEND::pfnProbe */
static DECLCALLBACK(int) cueProbe(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                                  PVDINTERFACE pVDIfsImage, VDTYPE enmDesiredType, VDTYPE *penmType)
{
    RT_NOREF(pVDIfsDisk, enmDesiredType);
    LogFlowFunc(("pszFilename=\"%s\" pVDIfsDisk=%#p pVDIfsImage=%#p\n", pszFilename, pVDIfsDisk, pVDIfsImage));
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);


    PCUEIMAGE pThis = (PCUEIMAGE)RTMemAllocZ(sizeof(CUEIMAGE));
    if (RT_LIKELY(pThis))
    {
        pThis->pszFilename = pszFilename;
        pThis->pStorage = NULL;
        pThis->pVDIfsDisk = pVDIfsDisk;
        pThis->pVDIfsImage = pVDIfsImage;

        rc = cueOpenImage(pThis, VD_OPEN_FLAGS_INFO | VD_OPEN_FLAGS_READONLY);
        cueFreeImage(pThis, false);
        RTMemFree(pThis);

        if (RT_SUCCESS(rc))
            *penmType = VDTYPE_OPTICAL_DISC;
        else
            rc = VERR_VD_GEN_INVALID_HEADER;
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnOpen */
static DECLCALLBACK(int) cueOpen(const char *pszFilename, unsigned uOpenFlags,
                                 PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                 VDTYPE enmType, void **ppBackendData)
{
    LogFlowFunc(("pszFilename=\"%s\" uOpenFlags=%#x pVDIfsDisk=%#p pVDIfsImage=%#p enmType=%u ppBackendData=%#p\n",
                 pszFilename, uOpenFlags, pVDIfsDisk, pVDIfsImage, enmType, ppBackendData));
    int rc;
    PCUEIMAGE pThis;

    /* Check open flags. All valid flags are supported. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);

    AssertReturn(enmType == VDTYPE_OPTICAL_DISC, VERR_NOT_SUPPORTED);

    pThis = (PCUEIMAGE)RTMemAllocZ(sizeof(CUEIMAGE));
    if (RT_LIKELY(pThis))
    {
        pThis->pszFilename = pszFilename;
        pThis->pStorage = NULL;
        pThis->pVDIfsDisk = pVDIfsDisk;
        pThis->pVDIfsImage = pVDIfsImage;

        rc = cueOpenImage(pThis, uOpenFlags);
        if (RT_SUCCESS(rc))
            *ppBackendData = pThis;
        else
            RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnClose */
static DECLCALLBACK(int) cueClose(void *pBackendData, bool fDelete)
{
    LogFlowFunc(("pBackendData=%#p fDelete=%d\n", pBackendData, fDelete));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;
    int rc = cueFreeImage(pThis, fDelete);
    RTMemFree(pThis);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRead */
static DECLCALLBACK(int) cueRead(void *pBackendData, uint64_t uOffset, size_t cbToRead,
                                 PVDIOCTX pIoCtx, size_t *pcbActuallyRead)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu cbToRead=%zu pIoCtx=%#p pcbActuallyRead=%#p\n",
                 pBackendData, uOffset, cbToRead, pIoCtx, pcbActuallyRead));
    int rc = VINF_SUCCESS;
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;

    /* Get the region */
    PCVDREGIONDESC pRegion = cueRegionQueryByOffset(pThis, uOffset);
    if (pRegion)
    {
        /* Clip read size to remain in the region (not necessary I think). */
        uint64_t offRead = uOffset - pRegion->offRegion;

        cbToRead = RT_MIN(cbToRead, pRegion->cRegionBlocksOrBytes - offRead);
        Assert(!(cbToRead % pRegion->cbBlock));

        /* Need to convert audio data samples to little endian. */
        if (   pRegion->enmDataForm == VDREGIONDATAFORM_CDDA
            && !pThis->fLittleEndian)
        {
            *pcbActuallyRead = cbToRead;

            while (cbToRead)
            {
                RTSGSEG Segment;
                unsigned cSegments = 1;
                size_t cbSeg = 0;

                cbSeg = vdIfIoIntIoCtxSegArrayCreate(pThis->pIfIo, pIoCtx, &Segment,
                                                     &cSegments, cbToRead);

                rc = vdIfIoIntFileReadSync(pThis->pIfIo, pThis->pStorageData, uOffset, Segment.pvSeg, cbSeg);
                if (RT_FAILURE(rc))
                    break;

                uint16_t *pu16Buf = (uint16_t *)Segment.pvSeg;
                for (uint32_t i = 0; i < cbSeg / sizeof(uint16_t); i++)
                {
                    *pu16Buf = RT_BSWAP_U16(*pu16Buf);
                    pu16Buf++;
                }

                cbToRead -= RT_MIN(cbToRead, cbSeg);
                uOffset += cbSeg;
            }
        }
        else
        {
            rc = vdIfIoIntFileReadUser(pThis->pIfIo, pThis->pStorageData, uOffset,
                                       pIoCtx, cbToRead);
            if (RT_SUCCESS(rc))
                *pcbActuallyRead = cbToRead;
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnWrite */
static DECLCALLBACK(int) cueWrite(void *pBackendData, uint64_t uOffset, size_t cbToWrite,
                                  PVDIOCTX pIoCtx, size_t *pcbWriteProcess, size_t *pcbPreRead,
                                  size_t *pcbPostRead, unsigned fWrite)
{
    RT_NOREF7(uOffset, cbToWrite, pIoCtx, pcbWriteProcess, pcbPreRead, pcbPostRead, fWrite);
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToWrite=%zu pcbWriteProcess=%#p pcbPreRead=%#p pcbPostRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToWrite, pcbWriteProcess, pcbPreRead, pcbPostRead));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;
    int rc;

    AssertPtr(pThis);

    if (pThis->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnFlush */
static DECLCALLBACK(int) cueFlush(void *pBackendData, PVDIOCTX pIoCtx)
{
    RT_NOREF2(pBackendData, pIoCtx);

    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnGetVersion */
static DECLCALLBACK(unsigned) cueGetVersion(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;

    AssertPtrReturn(pThis, 0);

    return 1;
}

/** @copydoc VDIMAGEBACKEND::pfnGetFileSize */
static DECLCALLBACK(uint64_t) cueGetFileSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;

    AssertPtrReturn(pThis, 0);

    uint64_t cbFile = 0;
    if (pThis->pStorage)
    {
        int rc = vdIfIoIntFileGetSize(pThis->pIfIo, pThis->pStorageData, &cbFile);
        if (RT_FAILURE(rc))
            cbFile = 0; /* Make sure it is 0 */
    }

    LogFlowFunc(("returns %lld\n", cbFile));
    return cbFile;
}

/** @copydoc VDIMAGEBACKEND::pfnGetPCHSGeometry */
static DECLCALLBACK(int) cueGetPCHSGeometry(void *pBackendData,
                                            PVDGEOMETRY pPCHSGeometry)
{
    RT_NOREF1(pPCHSGeometry);
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p\n", pBackendData, pPCHSGeometry));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc (PCHS=%u/%u/%u)\n", rc, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetPCHSGeometry */
static DECLCALLBACK(int) cueSetPCHSGeometry(void *pBackendData,
                                            PCVDGEOMETRY pPCHSGeometry)
{
    RT_NOREF1(pPCHSGeometry);
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p PCHS=%u/%u/%u\n",
                 pBackendData, pPCHSGeometry, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    if (pThis->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetLCHSGeometry */
static DECLCALLBACK(int) cueGetLCHSGeometry(void *pBackendData,
                                            PVDGEOMETRY pLCHSGeometry)
{
    RT_NOREF1(pLCHSGeometry);
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p\n", pBackendData, pLCHSGeometry));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc (LCHS=%u/%u/%u)\n", rc, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetLCHSGeometry */
static DECLCALLBACK(int) cueSetLCHSGeometry(void *pBackendData,
                                            PCVDGEOMETRY pLCHSGeometry)
{
    RT_NOREF1(pLCHSGeometry);
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p LCHS=%u/%u/%u\n",
                 pBackendData, pLCHSGeometry, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    if (pThis->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnQueryRegions */
static DECLCALLBACK(int) cueQueryRegions(void *pBackendData, PCVDREGIONLIST *ppRegionList)
{
    LogFlowFunc(("pBackendData=%#p ppRegionList=%#p\n", pBackendData, ppRegionList));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    *ppRegionList = pThis->pRegionList;
    LogFlowFunc(("returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnRegionListRelease */
static DECLCALLBACK(void) cueRegionListRelease(void *pBackendData, PCVDREGIONLIST pRegionList)
{
    RT_NOREF1(pRegionList);
    LogFlowFunc(("pBackendData=%#p pRegionList=%#p\n", pBackendData, pRegionList));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;
    AssertPtr(pThis); RT_NOREF(pThis);

    /* Nothing to do here. */
}

/** @copydoc VDIMAGEBACKEND::pfnGetImageFlags */
static DECLCALLBACK(unsigned) cueGetImageFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;

    AssertPtrReturn(pThis, 0);

    LogFlowFunc(("returns %#x\n", pThis->uImageFlags));
    return pThis->uImageFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnGetOpenFlags */
static DECLCALLBACK(unsigned) cueGetOpenFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;

    AssertPtrReturn(pThis, 0);

    LogFlowFunc(("returns %#x\n", pThis->uOpenFlags));
    return pThis->uOpenFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnSetOpenFlags */
static DECLCALLBACK(int) cueSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    LogFlowFunc(("pBackendData=%#p\n uOpenFlags=%#x", pBackendData, uOpenFlags));
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    /* Image must be opened and the new flags must be valid. */
    if (!pThis || (uOpenFlags & ~(  VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO
                                   | VD_OPEN_FLAGS_ASYNC_IO | VD_OPEN_FLAGS_SHAREABLE
                                   | VD_OPEN_FLAGS_SEQUENTIAL | VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS)))
        rc = VERR_INVALID_PARAMETER;
    else
    {
        /* Implement this operation via reopening the image. */
        rc = cueFreeImage(pThis, false);
        if (RT_SUCCESS(rc))
            rc = cueOpenImage(pThis, uOpenFlags);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetComment */
VD_BACKEND_CALLBACK_GET_COMMENT_DEF_NOT_SUPPORTED(cueGetComment);

/** @copydoc VDIMAGEBACKEND::pfnSetComment */
VD_BACKEND_CALLBACK_SET_COMMENT_DEF_NOT_SUPPORTED(cueSetComment, PCUEIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(cueGetUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(cueSetUuid, PCUEIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetModificationUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(cueGetModificationUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetModificationUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(cueSetModificationUuid, PCUEIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetParentUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(cueGetParentUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetParentUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(cueSetParentUuid, PCUEIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetParentModificationUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(cueGetParentModificationUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetParentModificationUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(cueSetParentModificationUuid, PCUEIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnDump */
static DECLCALLBACK(void) cueDump(void *pBackendData)
{
    PCUEIMAGE pThis = (PCUEIMAGE)pBackendData;

    AssertPtrReturnVoid(pThis);
    vdIfErrorMessage(pThis->pIfError, "Dumping CUE image \"%s\" mode=%s uOpenFlags=%X File=%#p\n",
                     pThis->pszFilename,
                     (pThis->uOpenFlags & VD_OPEN_FLAGS_READONLY) ? "r/o" : "r/w",
                     pThis->uOpenFlags,
                     pThis->pStorage);
    vdIfErrorMessage(pThis->pIfError, "Backing File \"%s\" File=%#p\n",
                     pThis->pszDataFilename, pThis->pStorageData);
    vdIfErrorMessage(pThis->pIfError, "Number of tracks: %u\n", pThis->pRegionList->cRegions);
    for (uint32_t i = 0; i < pThis->pRegionList->cRegions; i++)
    {
        PCVDREGIONDESC pRegion = &pThis->pRegionList->aRegions[i];

        vdIfErrorMessage(pThis->pIfError, "------------------------ Track %u ------------------------\n", i);
        vdIfErrorMessage(pThis->pIfError, "Start=%llu Size=%llu BlockSize=%llu DataSize=%llu MetadataSize=%llu\n",
                         pRegion->offRegion, pRegion->cRegionBlocksOrBytes, pRegion->cbBlock, pRegion->cbData,
                         pRegion->cbMetadata);
        vdIfErrorMessage(pThis->pIfError, "DataForm=%s MetadataForm=%s\n",
                         cueRegionDataFormStringify(pRegion->enmDataForm),
                         cueRegionMetadataFormStringify(pRegion->enmMetadataForm));
    }
}



const VDIMAGEBACKEND g_CueBackend =
{
    /* u32Version */
    VD_IMGBACKEND_VERSION,
    /* pszBackendName */
    "CUE",
    /* uBackendCaps */
    VD_CAP_FILE | VD_CAP_VFS,
    /* paFileExtensions */
    s_aCueFileExtensions,
    /* paConfigInfo */
    NULL,
    /* pfnProbe */
    cueProbe,
    /* pfnOpen */
    cueOpen,
    /* pfnCreate */
    NULL,
    /* pfnRename */
    NULL,
    /* pfnClose */
    cueClose,
    /* pfnRead */
    cueRead,
    /* pfnWrite */
    cueWrite,
    /* pfnFlush */
    cueFlush,
    /* pfnDiscard */
    NULL,
    /* pfnGetVersion */
    cueGetVersion,
    /* pfnGetFileSize */
    cueGetFileSize,
    /* pfnGetPCHSGeometry */
    cueGetPCHSGeometry,
    /* pfnSetPCHSGeometry */
    cueSetPCHSGeometry,
    /* pfnGetLCHSGeometry */
    cueGetLCHSGeometry,
    /* pfnSetLCHSGeometry */
    cueSetLCHSGeometry,
    /* pfnQueryRegions */
    cueQueryRegions,
    /* pfnRegionListRelease */
    cueRegionListRelease,
    /* pfnGetImageFlags */
    cueGetImageFlags,
    /* pfnGetOpenFlags */
    cueGetOpenFlags,
    /* pfnSetOpenFlags */
    cueSetOpenFlags,
    /* pfnGetComment */
    cueGetComment,
    /* pfnSetComment */
    cueSetComment,
    /* pfnGetUuid */
    cueGetUuid,
    /* pfnSetUuid */
    cueSetUuid,
    /* pfnGetModificationUuid */
    cueGetModificationUuid,
    /* pfnSetModificationUuid */
    cueSetModificationUuid,
    /* pfnGetParentUuid */
    cueGetParentUuid,
    /* pfnSetParentUuid */
    cueSetParentUuid,
    /* pfnGetParentModificationUuid */
    cueGetParentModificationUuid,
    /* pfnSetParentModificationUuid */
    cueSetParentModificationUuid,
    /* pfnDump */
    cueDump,
    /* pfnGetTimestamp */
    NULL,
    /* pfnGetParentTimestamp */
    NULL,
    /* pfnSetParentTimestamp */
    NULL,
    /* pfnGetParentFilename */
    NULL,
    /* pfnSetParentFilename */
    NULL,
    /* pfnComposeLocation */
    genericFileComposeLocation,
    /* pfnComposeName */
    genericFileComposeName,
    /* pfnCompact */
    NULL,
    /* pfnResize */
    NULL,
    /* pfnRepair */
    NULL,
    /* pfnTraverseMetadata */
    NULL,
    /* u32VersionEnd */
    VD_IMGBACKEND_VERSION
};
