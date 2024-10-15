/* $Id: scriptlex.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTScript* lexer API.
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
#define LOG_GROUP RTLOGGROUP_DEFAULT /// @todo
#include <iprt/script.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Internal lexer state.
 */
typedef struct RTSCRIPTLEXINT
{
    /** Magic. */
    uint32_t             u32Magic;
    /** Source position. */
    RTSCRIPTPOS          Pos;
    /** Current and next token buffer. */
    RTSCRIPTLEXTOKEN     aToks[2];
    /** Pointer to the current token. */
    PRTSCRIPTLEXTOKEN    pTokCur;
    /** Pointer to the next token. */
    PRTSCRIPTLEXTOKEN    pTokNext;
    /** The lexer config. */
    PCRTSCRIPTLEXCFG     pCfg;
    /** The input reader. */
    PFNRTSCRIPTLEXRDR    pfnReader;
    /** The destructor callback. */
    PFNRTSCRIPTLEXDTOR   pfnDtor;
    /** Opaque user data for the reader. */
    void                 *pvUser;
    /** Identifier string cache. */
    RTSTRCACHE           hStrCacheId;
    /** String literal string cache. */
    RTSTRCACHE           hStrCacheStringLit;
    /** Status code from the reader. */
    int                  rcRdr;
    /** Internal error info. */
    RTERRINFOSTATIC      ErrInfo;
    /** Lexer flags. */
    uint32_t             fFlags;
    /** Maximum numebr of bytes allocated for temporary storage for literal strings. */
    size_t               cchStrLitMax;
    /** Pointer to the string buffer for holding the literal string. */
    char                 *pszStrLit;
    /** Pointer to the current input character. */
    const char           *pchCur;
    /** Offset to start reading the next chunk from. */
    size_t               offBufRead;
    /** Size of the input buffer. */
    size_t               cchBuf;
    /** The cached part of the input, variable in size. */
    char                 achBuf[1];
} RTSCRIPTLEXINT;
/** Pointer to the internal lexer state. */
typedef RTSCRIPTLEXINT *PRTSCRIPTLEXINT;


/** Free the identifier string cache literal on destruction. */
#define RTSCRIPT_LEX_INT_F_STR_CACHE_ID_FREE      RT_BIT_32(0)
/** Free the string literal string cache literal on destruction. */
#define RTSCRIPT_LEX_INT_F_STR_CACHE_STR_LIT_FREE RT_BIT_32(1)
/** End of stream reached. */
#define RTSCRIPT_LEX_INT_F_EOS                    RT_BIT_32(2)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** Default set of white spaces. */
static const char *g_szWsDef = " \t";
/** Default set of newlines. */
static const char *g_aszNlDef[] =
{
    "\n",
    "\r\n",
    NULL
};
/** Default set of characters allowed for identifiers. */
static const char *g_aszIdeCharSetDef = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Locates the given character in the string, consuming it if found.
 *
 * @returns Flag whether the character was found in the string.
 * @param   pThis                  The lexer state.
 * @param   ch                     The character to check for.
 * @param   psz                    The string to check.
 */
DECLINLINE(bool) rtScriptLexLocateChInStrConsume(PRTSCRIPTLEXINT pThis, char ch, const char *psz)
{
    while (   *psz != '\0'
           && *psz != ch)
        psz++;

    if (*psz != '\0')
        RTScriptLexConsumeCh(pThis);

    return *psz != '\0';
}


/**
 * Matches the input against the given string starting with the given character, consuming it
 * if found.
 *
 * @returns Flag whether there was a match.
 * @param   pThis                  The lexer state.
 * @param   ch                     The character to check start matching.
 * @param   psz                    The string to match against.
 * @param   pszExclude             When the string matched but the input continues
 *                                 with one of the characters in this string there will
 *                                 be no match.
 */
DECLINLINE(bool) rtScriptLexMatchStrConsume(PRTSCRIPTLEXINT pThis, char ch, const char *psz,
                                            const char *pszExclude)
{
    bool fMatch = false;
    if (*psz == ch)
    {
        unsigned offPeek = 1;

        psz++;
        while (   *psz != '\0'
               && *psz == RTScriptLexPeekCh(pThis, offPeek))
        {
            offPeek++;
            psz++;
        }

        if (*psz == '\0')
        {
            if (pszExclude)
            {
                ch = RTScriptLexPeekCh(pThis, offPeek);
                fMatch = strchr(pszExclude, ch) == NULL;
            }
            else
                fMatch = true;
        }

        if (fMatch)
        {
            /* Match, consume everything. */
            while (offPeek-- > 0)
                RTScriptLexConsumeCh(pThis);
        }
    }

    return fMatch;
}


/**
 * Tries to locate a string with the given starting character (+ peeking ahead) in the
 * given string array (exact match) and consumes the entire substring.
 *
 * @returns Flag whether there was a match.
 * @param   pThis                  The lexer state.
 * @param   ch                     The character to check for.
 * @param   papsz                  Pointer to the string array to check for.
 * @param   pidx                   Where to store the index of the matching substring if found,
 *                                 optional.
 */
DECLINLINE(bool) rtScriptLexLocateSubStrInStrArrayMatchConsume(PRTSCRIPTLEXINT pThis, char ch,
                                                               const char **papsz, unsigned *pidx)
{
    unsigned int idx = 0;

    while (   papsz[idx] != NULL
           && !rtScriptLexMatchStrConsume(pThis, ch, papsz[idx], NULL))
        idx++;

    if (   papsz[idx] != NULL
        && pidx)
        *pidx = idx;

    return papsz[idx] != NULL;
}


/**
 * Tries to get an exact match starting with the given character, consuming it when found.
 *
 * @returns Flag whether there was a match.
 * @param   pThis                  The lexer state.
 * @param   ch                     The character to check for.
 * @param   ppMatch                Where to store the exact match on success.
 */
DECLINLINE(bool) rtScriptLexLocateExactMatchConsume(PRTSCRIPTLEXINT pThis, char ch, PCRTSCRIPTLEXTOKMATCH *ppMatch)
{
    PCRTSCRIPTLEXTOKMATCH pTokMatch = pThis->pCfg->paTokMatches;

    if (pTokMatch)
    {
        while (   pTokMatch->pszMatch != NULL
               && !rtScriptLexMatchStrConsume(pThis, ch, pTokMatch->pszMatch,
                                                pTokMatch->fMaybeIdentifier
                                              ? g_aszIdeCharSetDef
                                              : NULL))
            pTokMatch++;

        if (pTokMatch->pszMatch != NULL)
        {
            *ppMatch = pTokMatch;
            return true;
        }
    }

    return false;
}


DECLINLINE(bool) rtScriptLexIsNewlineConsume(PRTSCRIPTLEXINT pThis, char ch)
{
    const char **papszNl = pThis->pCfg->pszWhitespace ? pThis->pCfg->papszNewline : g_aszNlDef;

    bool fMatched = rtScriptLexLocateSubStrInStrArrayMatchConsume(pThis, ch, papszNl, NULL);
    if (fMatched)
    {
        pThis->Pos.iLine++;
        pThis->Pos.iCh = 1;
    }

    return fMatched;
}


/**
 * Checks whether the character is the beginning of a multi line comment, skipping the whole
 * comment if necessary.
 *
 * @returns Flag whether a multi line comment was detected and consumed.
 * @param   hScriptLex             The lexer state.
 * @param   ch                     The character to check for.
 */
DECLINLINE(bool) rtScriptLexIsMultiLineCommentConsume(PRTSCRIPTLEXINT pThis, char ch)
{
    const char **papszCommentMultiStart = pThis->pCfg->papszCommentMultiStart;
    unsigned idxComment = 0;

    if (   papszCommentMultiStart
        && rtScriptLexLocateSubStrInStrArrayMatchConsume(pThis, ch, papszCommentMultiStart,
                                                         &idxComment))
    {
        /* Look for the matching closing lexeme in the input consuming everything along the way. */
        const char *pszClosing = pThis->pCfg->papszCommentMultiEnd[idxComment];

        for (;;)
        {
            char chTmp = RTScriptLexGetCh(pThis);

            /* Check for new lines explicetly to advance the position information. */
            if (rtScriptLexIsNewlineConsume(pThis, chTmp))
                continue;

            /** @todo Not quite correct when there is an end of stream before the closing lexeme.
             * But doesn't hurt at the moment. */
            if (   chTmp == '\0'
                || rtScriptLexMatchStrConsume(pThis, chTmp, pszClosing, NULL))
                break;

            RTScriptLexConsumeCh(pThis);
        }

        return true;
    }

    return false;
}


/**
 * Checks whether the character is the beginning of a single line comment, skipping the whole
 * comment if necessary.
 *
 * @returns Flag whether a single line comment was detected and consumed.
 * @param   hScriptLex             The lexer state.
 * @param   ch                     The character to check for.
 */
DECLINLINE(bool) rtScriptLexIsSingleLineCommentConsume(PRTSCRIPTLEXINT pThis, char ch)
{
    const char **papszCommentSingleStart = pThis->pCfg->papszCommentSingleStart;

    if (   papszCommentSingleStart
        && rtScriptLexLocateSubStrInStrArrayMatchConsume(pThis, ch, papszCommentSingleStart,
                                                         NULL))
    {
        for (;;)
        {
            char chTmp = RTScriptLexGetCh(pThis);

            if (   chTmp == '\0'
                || rtScriptLexIsNewlineConsume(pThis, chTmp))
                break;

            RTScriptLexConsumeCh(pThis);
        }

        return true;
    }

    return false;
}


/**
 * Fills the input buffer with source data.
 *
 * @returns IPRT status code.
 * @param   pThis                  The lexer state.
 */
static int rtScriptLexFillBuffer(PRTSCRIPTLEXINT pThis)
{
    int rc = VINF_SUCCESS;
    size_t cchToRead = pThis->cchBuf;
    char *pchRead = &pThis->achBuf[0];

    AssertReturn(!(pThis->fFlags & RTSCRIPT_LEX_INT_F_EOS), VERR_INVALID_STATE);

    /* If there is input left to process move it to the front and fill the remainder. */
    if (pThis->pchCur != NULL)
    {
        cchToRead = pThis->pchCur - &pThis->achBuf[0];
        /* Move the rest to the front. */
        memmove(&pThis->achBuf[0], pThis->pchCur, pThis->cchBuf - cchToRead);
        pchRead = (char *)pThis->pchCur + 1;
        memset(pchRead, 0, cchToRead);
    }

    if (cchToRead)
    {
        pThis->pchCur = &pThis->achBuf[0];

        size_t cchRead = 0;
        rc = pThis->pfnReader(pThis, pThis->offBufRead, pchRead, cchToRead, &cchRead, pThis->pvUser);
        if (RT_SUCCESS(rc))
        {
            pThis->offBufRead += cchRead;
            if (rc == VINF_EOF)
                pThis->fFlags |= RTSCRIPT_LEX_INT_F_EOS;
            rc = VINF_SUCCESS;
        }
        else
            pThis->rcRdr = rc;
    }
    else
        rc = VERR_BUFFER_OVERFLOW; /** @todo */

    return rc;
}


/**
 * Produce an end of stream token.
 *
 * @returns nothing.
 * @param   pThis                  The lexer state.
 * @param   pTok                   The token to fill.
 */
static void rtScriptLexProduceTokEos(PRTSCRIPTLEXINT pThis, PRTSCRIPTLEXTOKEN pTok)
{
    pTok->enmType  = RTSCRIPTLEXTOKTYPE_EOS;
    pTok->PosStart = pThis->Pos;
    pTok->PosEnd   = pThis->Pos;
}


/**
 * Produce an error token with the given error message.
 *
 * @returns IPRT status code.
 * @param   pThis                  The lexer state.
 * @param   pTok                   The token to fill.
 * @param   rc                     The status code to use in the message.
 * @param   pszMsg                 The format string for the error message.
 * @param   ...                    Arguments to the format string.
 */
static int rtScriptLexProduceTokError(PRTSCRIPTLEXINT pThis, PRTSCRIPTLEXTOKEN pTok,
                                      int rc, const char *pszMsg, ...)
{
    va_list va;
    va_start(va, pszMsg);

    pTok->enmType = RTSCRIPTLEXTOKTYPE_ERROR;
    pTok->PosEnd  = pThis->Pos;
    pTok->Type.Error.pErr = &pThis->ErrInfo.Core;

    RTErrInfoInitStatic(&pThis->ErrInfo);
    RTErrInfoSetV(&pThis->ErrInfo.Core, rc, pszMsg, va);
    va_end(va);

    return rc;
}


/**
 * Create the token from the exact match.
 *
 * @returns nothing.
 * @param   pThis                  The lexer state.
 * @param   pTok                   The token to fill.
 * @param   pMatch                 The matched string.
 */
static void rtScriptLexProduceTokFromExactMatch(PRTSCRIPTLEXINT pThis, PRTSCRIPTLEXTOKEN pTok,
                                                PCRTSCRIPTLEXTOKMATCH pMatch)
{
    pTok->enmType = pMatch->enmTokType;
    pTok->PosEnd  = pThis->Pos;

    switch (pTok->enmType)
    {
        case RTSCRIPTLEXTOKTYPE_OPERATOR:
            pTok->Type.Operator.pOp = pMatch;
            break;
        case RTSCRIPTLEXTOKTYPE_KEYWORD:
            pTok->Type.Keyword.pKeyword = pMatch;
            break;
        case RTSCRIPTLEXTOKTYPE_PUNCTUATOR:
            pTok->Type.Punctuator.pPunctuator = pMatch;
            break;
        default:
            rtScriptLexProduceTokError(pThis, pTok, VERR_INVALID_PARAMETER,
                                       "Lexer: The match contains an invalid token type: %d\n",
                                       pTok->enmType);
    }
}


/**
 * Goes through the rules trying to find a matching one.
 *
 * @returns Flag whether a matching rule was found.
 * @param   pThis                  The lexer state.
 * @param   ch                     The character to check.
 * @param   pTok                   The token to fill.
 */
static bool rtScriptLexProduceTokFromRules(PRTSCRIPTLEXINT pThis, char ch, PRTSCRIPTLEXTOKEN pTok)
{
    PCRTSCRIPTLEXRULE pRule = pThis->pCfg->paRules;

    if (pRule)
    {
        while (pRule->pfnProd != NULL)
        {
            if (   ch >= pRule->chStart
                && ch <= pRule->chEnd)
            {
                if (pRule->fFlags & RTSCRIPT_LEX_RULE_CONSUME)
                    RTScriptLexConsumeCh(pThis);
                int rc = pRule->pfnProd(pThis, ch, pTok, pRule->pvUser);
                AssertRC(rc);
                return true;
            }

            pRule++;
        }
    }

    return false;
}


/**
 * Fills in the given token from the scanned input at the current location.
 *
 * @returns IPRT status code.
 * @param   pThis                  The lexer state.
 * @param   pTok                   The token to fill.
 */
static int rtScriptLexProduceToken(PRTSCRIPTLEXINT pThis, PRTSCRIPTLEXTOKEN pTok)
{
    RTScriptLexSkipWhitespace(pThis);

    pTok->PosStart = pThis->Pos;

    char ch = RTScriptLexGetCh(pThis);
    PCRTSCRIPTLEXTOKMATCH pMatch = NULL;
    if (ch == '\0')
        rtScriptLexProduceTokEos(pThis, pTok);
    else if (rtScriptLexLocateExactMatchConsume(pThis, ch, &pMatch))
        rtScriptLexProduceTokFromExactMatch(pThis, pTok, pMatch);
    else if (!rtScriptLexProduceTokFromRules(pThis, ch, pTok))
    {
        if (pThis->pCfg->pfnProdDef)
            pThis->rcRdr = pThis->pCfg->pfnProdDef(pThis, ch, pTok, pThis->pCfg->pvProdDefUser);
        else
            rtScriptLexProduceTokError(pThis, pTok, VERR_INVALID_PARAMETER,
                                       "Lexer: Invalid character found in input: %c\n",
                                       ch);
    }

    return pThis->rcRdr;
}


/**
 * Populates the lexer for the initial use.
 *
 * @returns IPRT status code.
 * @param   pThis                  The lexer state.
 */
static int rtScriptLexPopulate(PRTSCRIPTLEXINT pThis)
{
    int rc = rtScriptLexFillBuffer(pThis);
    if (RT_SUCCESS(rc))
    {
        rc = rtScriptLexProduceToken(pThis, pThis->pTokCur);
        if (RT_SUCCESS(rc))
            rc = rtScriptLexProduceToken(pThis, pThis->pTokNext);
    }

    return rc;
}



RTDECL(int) RTScriptLexCreateFromReader(PRTSCRIPTLEX phScriptLex, PFNRTSCRIPTLEXRDR pfnReader,
                                        PFNRTSCRIPTLEXDTOR pfnDtor, void *pvUser,
                                        size_t cchBuf, PRTSTRCACHE phStrCacheId, PRTSTRCACHE phStrCacheStringLit,
                                        PCRTSCRIPTLEXCFG pCfg)
{
    AssertPtrReturn(phScriptLex, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnReader, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg, VERR_INVALID_POINTER);

    if (!cchBuf)
        cchBuf = _16K;
    int rc = VINF_SUCCESS;
    PRTSCRIPTLEXINT pThis = (PRTSCRIPTLEXINT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTSCRIPTLEXINT, achBuf[cchBuf]));
    if (RT_LIKELY(pThis))
    {
        pThis->u32Magic     = 0xfefecafe; /** @todo */
        pThis->Pos.iLine    = 1;
        pThis->Pos.iCh      = 1;
        pThis->pTokCur      = &pThis->aToks[0];
        pThis->pTokNext     = &pThis->aToks[1];
        pThis->pCfg         = pCfg;
        pThis->pfnReader    = pfnReader;
        pThis->pfnDtor      = pfnDtor;
        pThis->pvUser       = pvUser;
        pThis->fFlags       = 0;
        pThis->cchStrLitMax = 0;
        pThis->pszStrLit    = NULL;
        pThis->cchBuf       = cchBuf;
        pThis->offBufRead   = 0;
        pThis->pchCur       = NULL;
        pThis->hStrCacheId  = NULL;
        pThis->hStrCacheStringLit  = NULL;

        rc = RTStrCacheCreate(&pThis->hStrCacheId, "LEX-Ide");
        if (RT_SUCCESS(rc))
        {
            rc = RTStrCacheCreate(&pThis->hStrCacheStringLit, "LEX-StrLit");
            if (RT_SUCCESS(rc))
            {
                rc = rtScriptLexPopulate(pThis);
                if (RT_SUCCESS(rc))
                {
                    *phScriptLex = pThis;

                    if (phStrCacheId)
                        *phStrCacheId = pThis->hStrCacheId;
                    else
                        pThis->fFlags |= RTSCRIPT_LEX_INT_F_STR_CACHE_ID_FREE;

                    if (phStrCacheStringLit)
                        *phStrCacheStringLit = pThis->hStrCacheStringLit;
                    else
                        pThis->fFlags |= RTSCRIPT_LEX_INT_F_STR_CACHE_STR_LIT_FREE;

                    return VINF_SUCCESS;
                }

                RTStrCacheDestroy(pThis->hStrCacheStringLit);
            }

            RTStrCacheDestroy(pThis->hStrCacheId);
        }

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * @callback_method_impl{FNRTSCRIPTLEXRDR, Worker to read from a string.}
 */
static DECLCALLBACK(int) rtScriptLexReaderStr(RTSCRIPTLEX hScriptLex, size_t offBuf, char *pchCur,
                                              size_t cchBuf, size_t *pcchRead, void *pvUser)
{
    RT_NOREF(hScriptLex);

    const char *psz = (const char *)pvUser;
    size_t cch = strlen(psz);
    size_t cchCopy = RT_MIN(cchBuf, cch - offBuf);
    int rc = VINF_SUCCESS;

    *pcchRead = cchCopy;

    if (cchCopy)
        memcpy(pchCur, &psz[offBuf], cchCopy * sizeof(char));
    else
        rc = VINF_EOF;

    return rc;
}


RTDECL(int) RTScriptLexCreateFromString(PRTSCRIPTLEX phScriptLex, const char *pszSrc, PRTSTRCACHE phStrCacheId,
                                        PRTSTRCACHE phStrCacheStringLit, PCRTSCRIPTLEXCFG pCfg)
{
    return RTScriptLexCreateFromReader(phScriptLex, rtScriptLexReaderStr, NULL, (void *)pszSrc, 0,
                                       phStrCacheId, phStrCacheStringLit, pCfg);
}


/**
 * @callback_method_impl{FNRTSCRIPTLEXRDR, Worker to read from a file.}
 */
static DECLCALLBACK(int) rtScriptLexReaderFile(RTSCRIPTLEX hScriptLex, size_t offBuf, char *pchCur,
                                               size_t cchBuf, size_t *pcchRead, void *pvUser)
{
    RT_NOREF(hScriptLex);

    RTFILE hFile = (RTFILE)pvUser;
    return RTFileReadAt(hFile, offBuf, pchCur, cchBuf, pcchRead);
}


/**
 * @callback_method_impl{FNRTSCRIPTLEXDTOR, Destructor for the file variant.}
 */
static DECLCALLBACK(void) rtScriptLexDtorFile(RTSCRIPTLEX hScriptLex, void *pvUser)
{
    RT_NOREF(hScriptLex);

    RTFILE hFile = (RTFILE)pvUser;
    RTFileClose(hFile);
}


RTDECL(int) RTScriptLexCreateFromFile(PRTSCRIPTLEX phScriptLex, const char *pszFilename, PRTSTRCACHE phStrCacheId,
                                      PRTSTRCACHE phStrCacheStringLit, PCRTSCRIPTLEXCFG pCfg)
{
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN);
    if (RT_SUCCESS(rc))
    {
        rc = RTScriptLexCreateFromReader(phScriptLex, rtScriptLexReaderFile, rtScriptLexDtorFile, (void *)hFile, 0,
                                         phStrCacheId, phStrCacheStringLit, pCfg);
        if (RT_FAILURE(rc))
            RTFileClose(hFile);
    }

    return rc;
}


RTDECL(void) RTScriptLexDestroy(RTSCRIPTLEX hScriptLex)
{
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturnVoid(pThis);

    if (pThis->pfnDtor)
        pThis->pfnDtor(pThis, pThis->pvUser);

    if (pThis->fFlags & RTSCRIPT_LEX_INT_F_STR_CACHE_ID_FREE)
        RTStrCacheDestroy(pThis->hStrCacheId);
    if (pThis->fFlags & RTSCRIPT_LEX_INT_F_STR_CACHE_STR_LIT_FREE)
        RTStrCacheDestroy(pThis->hStrCacheStringLit);

    if (pThis->pszStrLit)
        RTStrFree(pThis->pszStrLit);

    RTMemFree(pThis);
}


RTDECL(int) RTScriptLexQueryToken(RTSCRIPTLEX hScriptLex, PCRTSCRIPTLEXTOKEN *ppToken)
{
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(ppToken, VERR_INVALID_POINTER);

    if (RT_SUCCESS(pThis->rcRdr))
        *ppToken = pThis->pTokCur;

    return pThis->rcRdr;
}


RTDECL(RTSCRIPTLEXTOKTYPE) RTScriptLexGetTokenType(RTSCRIPTLEX hScriptLex)
{
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, RTSCRIPTLEXTOKTYPE_INVALID);

    if (RT_SUCCESS(pThis->rcRdr))
        return pThis->pTokCur->enmType;

    return RTSCRIPTLEXTOKTYPE_INVALID;
}


RTDECL(RTSCRIPTLEXTOKTYPE) RTScriptLexPeekNextTokenType(RTSCRIPTLEX hScriptLex)
{
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, RTSCRIPTLEXTOKTYPE_INVALID);

    if (RT_SUCCESS(pThis->rcRdr))
        return pThis->pTokNext->enmType;

    return RTSCRIPTLEXTOKTYPE_INVALID;
}


RTDECL(PCRTSCRIPTLEXTOKEN) RTScriptLexConsumeToken(RTSCRIPTLEX hScriptLex)
{
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, NULL);

    /*
     * Stop token production as soon as the current token indicates the
     * end of the stream or an error
     */
    if (   pThis->pTokCur->enmType != RTSCRIPTLEXTOKTYPE_EOS
        && pThis->pTokCur->enmType != RTSCRIPTLEXTOKTYPE_ERROR)
    {
        PRTSCRIPTLEXTOKEN pTokTmp = pThis->pTokCur;

        /* Switch next token to current token and read in the next token. */
        pThis->pTokCur  = pThis->pTokNext;
        pThis->pTokNext = pTokTmp;
        if (   pThis->pTokCur->enmType != RTSCRIPTLEXTOKTYPE_EOS
            && pThis->pTokCur->enmType != RTSCRIPTLEXTOKTYPE_ERROR)
            rtScriptLexProduceToken(pThis, pThis->pTokNext);
        else
            pThis->pTokNext = pThis->pTokCur;
    }

    return pThis->pTokCur;
}


RTDECL(char) RTScriptLexConsumeCh(RTSCRIPTLEX hScriptLex)
{
    return RTScriptLexConsumeChEx(hScriptLex, RTSCRIPT_LEX_CONV_F_DEFAULT);
}


RTDECL(char) RTScriptLexConsumeChEx(RTSCRIPTLEX hScriptLex, uint32_t fFlags)
{
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, '\0');

    pThis->pchCur++;
    pThis->Pos.iCh++;
    if (pThis->pchCur == &pThis->achBuf[pThis->cchBuf])
        rtScriptLexFillBuffer(pThis);

    return RTScriptLexGetChEx(pThis, fFlags);
}


RTDECL(char) RTScriptLexPeekCh(RTSCRIPTLEX hScriptLex, unsigned idx)
{
    return RTScriptLexPeekChEx(hScriptLex, idx, RTSCRIPT_LEX_CONV_F_DEFAULT);
}


RTDECL(char) RTScriptLexPeekChEx(RTSCRIPTLEX hScriptLex, unsigned idx, uint32_t fFlags)
{
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, '\0');

    /* Just return the character if it is in the current buffer. */
    char ch = '\0';
    if (RT_LIKELY(pThis->pchCur + idx < &pThis->achBuf[pThis->cchBuf]))
        ch = pThis->pchCur[idx];
    else
    {
        /* Slow path, read data into temporary buffer to read character from and dismiss. */
        /** @todo */
        AssertReleaseFailed();
    }

    if (   (pThis->pCfg->fFlags & RTSCRIPT_LEX_CFG_F_CASE_INSENSITIVE)
        && !(fFlags & RTSCRIPT_LEX_CONV_F_NOTHING))
        ch = RT_C_TO_LOWER(ch);

    return ch;
}


RTDECL(char) RTScriptLexGetCh(RTSCRIPTLEX hScriptLex)
{
    return RTScriptLexPeekCh(hScriptLex, 0);
}


RTDECL(char) RTScriptLexGetChEx(RTSCRIPTLEX hScriptLex, uint32_t fFlags)
{
    return RTScriptLexPeekChEx(hScriptLex, 0, fFlags);
}


RTDECL(void) RTScriptLexSkipWhitespace(RTSCRIPTLEX hScriptLex)
{
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturnVoid(pThis);

    for (;;)
    {
        char ch = RTScriptLexGetCh(hScriptLex);

        if (ch == '\0')
            break;

        /* Check for whitespace. */
        const char *pszWs = pThis->pCfg->pszWhitespace ? pThis->pCfg->pszWhitespace : g_szWsDef;

        if (   rtScriptLexLocateChInStrConsume(pThis, ch, pszWs)
            || rtScriptLexIsNewlineConsume(pThis, ch)
            || rtScriptLexIsMultiLineCommentConsume(pThis, ch)
            || rtScriptLexIsSingleLineCommentConsume(pThis, ch))
            continue;

        /* All white space skipped, next is some real content. */
        break;
    }
}


RTDECL(int) RTScriptLexScanNumber(RTSCRIPTLEX hScriptLex, uint8_t uBase, bool fAllowReal,
                                  PRTSCRIPTLEXTOKEN pTok)
{
    RT_NOREF(uBase, fAllowReal, pTok);
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertReturn(!fAllowReal, VERR_NOT_IMPLEMENTED);
    AssertReturn(!uBase, VERR_NOT_IMPLEMENTED);

    /** @todo r=aeichner Quick and dirty to have something working for the disassembler testcase.
     * Among others it misses overflow handling. */
    uBase = 10;
    char ch = RTScriptLexGetCh(hScriptLex);
    pTok->Type.Number.enmType =   ch == '-'
                                ? RTSCRIPTLEXTOKNUMTYPE_INTEGER
                                : RTSCRIPTLEXTOKNUMTYPE_NATURAL;
    if (ch == '-' || ch == '+')
        ch = RTScriptLexConsumeCh(hScriptLex);

    if (ch == '0')
    {
        /* Some hex prefix? */
        char chNext = RTScriptLexPeekCh(hScriptLex, 1);
        if (chNext == 'x')
        {
            uBase = 16;
            RTScriptLexConsumeCh(hScriptLex);
        }
        else if (chNext >= '0' && chNext <= '9') /* Octal stuff. */
            AssertFailedReturn(VERR_NOT_IMPLEMENTED);

        ch = RTScriptLexConsumeCh(hScriptLex);
    }

    uint64_t u64 = 0;
    for (;;)
    {
        if (   (ch < '0' || ch > '9')
            && (ch < 'a' || ch > 'f' || uBase == 10))
        {
            if (pTok->Type.Number.enmType == RTSCRIPTLEXTOKNUMTYPE_INTEGER)
                pTok->Type.Number.Type.i64 = -(int64_t)u64;
            else
                pTok->Type.Number.Type.u64 = u64;
            pTok->enmType = RTSCRIPTLEXTOKTYPE_NUMBER;
            pTok->PosEnd  = pThis->Pos;
            return VINF_SUCCESS;
        }

        if (ch >= '0' && ch <= '9')
            u64 = (u64 * uBase) + (ch - '0');
        else if (ch >= 'a' && ch <= 'f')
        {
            Assert(uBase == 16);
            u64 = (u64 << 4) + 10 + (ch - 'a');
        }

        ch = RTScriptLexConsumeCh(hScriptLex);
    }
}


RTDECL(int) RTScriptLexScanIdentifier(RTSCRIPTLEX hScriptLex, char ch,
                                      PRTSCRIPTLEXTOKEN pTok, void *pvUser)
{
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    const char *pszCharSet = pvUser ? (const char *)pvUser : g_aszIdeCharSetDef;
    char aszIde[513]; RT_ZERO(aszIde);
    unsigned idx = 0;
    aszIde[idx++] = ch;

    ch = RTScriptLexGetCh(hScriptLex);
    while (   idx < sizeof(aszIde) - 1
           && rtScriptLexLocateChInStrConsume(hScriptLex, ch, pszCharSet))
    {
        aszIde[idx++] = ch;
        ch = RTScriptLexGetCh(hScriptLex);
    }

    if (   idx == sizeof(aszIde) - 1
        && rtScriptLexLocateChInStrConsume(hScriptLex, ch, pszCharSet))
        return rtScriptLexProduceTokError(hScriptLex, pTok, VERR_BUFFER_OVERFLOW, "Lexer: Identifier exceeds the allowed length");

    /* Insert into string cache. */
    pTok->enmType = RTSCRIPTLEXTOKTYPE_IDENTIFIER;
    pTok->Type.Id.pszIde = RTStrCacheEnterN(pThis->hStrCacheId, &aszIde[0], idx);
    if (RT_UNLIKELY(!pTok->Type.Id.pszIde))
        return rtScriptLexProduceTokError(hScriptLex, pTok, VERR_NO_STR_MEMORY, "Lexer: Out of memory inserting identifier into string cache");

    pTok->PosEnd = pThis->Pos;
    return VINF_SUCCESS;
}


/**
 * Adds the given character to the string literal add the given position, assuring the string
 * is always zero terminated.
 *
 * @returns IPRT status code.
 * @param   pThis                  The lexer state.
 * @param   ch                     The character to add.
 * @param   idx                    At which position to add the character in the string.
 */
static int rtScriptLexScanStringLiteralChAdd(PRTSCRIPTLEXINT pThis, char ch, uint32_t idx)
{
    int rc = VINF_SUCCESS;

    if (   !pThis->cchStrLitMax
        || idx >= pThis->cchStrLitMax - 1)
    {
        /* Increase memory. */
        size_t cchMaxNew = pThis->cchStrLitMax + 64;
        char *pszNew = NULL;
        rc = RTStrRealloc(&pszNew, cchMaxNew * sizeof(char));
        if (RT_SUCCESS(rc))
        {
            pThis->pszStrLit    = pszNew;
            pThis->cchStrLitMax = cchMaxNew;
        }
    }

    if (RT_SUCCESS(rc))
    {
        pThis->pszStrLit[idx]     = ch;
        pThis->pszStrLit[idx + 1] = '\0';
    }

    return rc;
}


RTDECL(int) RTScriptLexScanStringLiteralC(RTSCRIPTLEX hScriptLex, char ch,
                                          PRTSCRIPTLEXTOKEN pTok, void *pvUser)
{
    RT_NOREF(ch, pvUser);
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    uint32_t idxChCur = 0;
    int rc = rtScriptLexScanStringLiteralChAdd(pThis, '\0', idxChCur);
    if (RT_FAILURE(rc))
        return rtScriptLexProduceTokError(hScriptLex, pTok, rc, "Lexer: Error adding character to string literal");

    ch = RTScriptLexGetCh(hScriptLex);
    for (;;)
    {
        if (ch == '\0')
            return rtScriptLexProduceTokError(hScriptLex, pTok, VERR_EOF, "Lexer: End of stream before closing string literal terminal");
        else if (ch == '\"')
        {
            /* End of string, add it to the string literal cache and build the token. */
            pTok->enmType = RTSCRIPTLEXTOKTYPE_STRINGLIT;
            pTok->Type.StringLit.cchString = idxChCur;
            pTok->Type.StringLit.pszString = RTStrCacheEnterN(pThis->hStrCacheStringLit, pThis->pszStrLit, idxChCur);
            if (RT_UNLIKELY(!pTok->Type.StringLit.pszString))
                return rtScriptLexProduceTokError(hScriptLex, pTok, VERR_NO_STR_MEMORY, "Lexer: Error adding string literal to the cache");
            else
                break;
        }
        else if (ch == '\\')
        {
            /* Start of escape sequence. */
            RTScriptLexConsumeCh(hScriptLex);
            ch = RTScriptLexGetCh(hScriptLex);
            switch (ch)
            {
                case 'a': /* Alert (Bell) */
                    ch = 0x07;
                    break;
                case 'b': /* Backspace */
                    ch = 0x08;
                    break;
                case 'e': /* Escape character */
                    ch = 0x1b;
                    break;
                case 'f': /* Formfeed */
                    ch = 0x0c;
                    break;
                case 'n': /* Newline (line freed) */
                    ch = 0x0a;
                    break;
                case 'r': /* Carriage return */
                    ch = 0x0d;
                    break;
                case 't': /* Horizontal tab */
                    ch = 0x09;
                    break;
                case 'v': /* Vertical tab */
                    ch = 0x0b;
                    break;
                case '\\':
                case '\'':
                case '\"':
                case '\?':
                    /* Can be added as is. */
                    break;
                case 'x': /* Hexdecimal byte. */
                case '0': /* Octal */
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                case 'u': /* Unicode point below 10000 */
                case 'U': /* Unicode point */
                default:
                    /* Not supported for now. */
                    return rtScriptLexProduceTokError(hScriptLex, pTok, VERR_NOT_SUPPORTED, "Lexer: Invalid/unsupported escape sequence");
            }
        }

        rc = rtScriptLexScanStringLiteralChAdd(pThis, ch, idxChCur);
        if (RT_SUCCESS(rc))
            idxChCur++;
        else
            return rtScriptLexProduceTokError(hScriptLex, pTok, rc, "Lexer: Error adding character to string literal");

        ch = RTScriptLexConsumeChEx(hScriptLex, RTSCRIPT_LEX_CONV_F_NOTHING);
    }

    pTok->PosEnd = pThis->Pos;
    return VINF_SUCCESS;
}


RTDECL(int) RTScriptLexScanStringLiteralPascal(RTSCRIPTLEX hScriptLex, char ch,
                                               PRTSCRIPTLEXTOKEN pTok, void *pvUser)
{
    RT_NOREF(ch, pvUser);
    PRTSCRIPTLEXINT pThis = hScriptLex;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    uint32_t idxChCur = 0;
    int rc = rtScriptLexScanStringLiteralChAdd(pThis, '\0', idxChCur);
    if (RT_FAILURE(rc))
        return rtScriptLexProduceTokError(hScriptLex, pTok, rc, "Lexer: Error adding character to string literal");

    ch = RTScriptLexGetChEx(hScriptLex, RTSCRIPT_LEX_CONV_F_NOTHING);
    for (;;)
    {
        if (ch == '\0')
            return rtScriptLexProduceTokError(hScriptLex, pTok, VERR_EOF, "Lexer: End of stream before closing string literal terminal");
        else if (ch == '\'')
        {
            /*
             * Check whether there is a second ' coming afterwards used for
             * escaping ' characters.
             */
            ch = RTScriptLexConsumeChEx(hScriptLex, RTSCRIPT_LEX_CONV_F_NOTHING);
            if (ch != '\'')
            {
                /* End of string, add it to the string literal cache and build the token. */
                pTok->enmType = RTSCRIPTLEXTOKTYPE_STRINGLIT;
                pTok->Type.StringLit.cchString = idxChCur;
                pTok->Type.StringLit.pszString = RTStrCacheEnterN(pThis->hStrCacheStringLit, pThis->pszStrLit, idxChCur);
                if (RT_UNLIKELY(!pTok->Type.StringLit.pszString))
                    return rtScriptLexProduceTokError(hScriptLex, pTok, VERR_NO_STR_MEMORY, "Lexer: Error adding string literal to the cache");
                else
                    break;
            }
            /* else: Fall through and add the character to the string literal..*/
        }

        rc = rtScriptLexScanStringLiteralChAdd(pThis, ch, idxChCur);
        if (RT_SUCCESS(rc))
            idxChCur++;
        else
            return rtScriptLexProduceTokError(hScriptLex, pTok, rc, "Lexer: Error adding character to string literal");
        ch = RTScriptLexConsumeChEx(hScriptLex, RTSCRIPT_LEX_CONV_F_NOTHING);
    }

    pTok->PosEnd = pThis->Pos;
    return VINF_SUCCESS;
}

