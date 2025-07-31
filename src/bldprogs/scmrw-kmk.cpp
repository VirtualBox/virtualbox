/* $Id: scmrw-kmk.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT Testcase / Tool - Source Code Massager, Makefile.kmk/kup.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include "scm.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef enum KMKASSIGNTYPE
{
    kKmkAssignType_Recursive,
    kKmkAssignType_Conditional,
    kKmkAssignType_Appending,
    kKmkAssignType_Prepending,
    kKmkAssignType_Simple,
    kKmkAssignType_Immediate
} KMKASSIGNTYPE;

/** Context for scmKmkWordLength. */
typedef enum
{
    /** Target file or assignment.
     *  Separators: space, '=', ':' */
    kKmkWordCtx_TargetFileOrAssignment,
    /** Target file.
     *  Separators: space, ':' */
    kKmkWordCtx_TargetFile,
    /** Dependency file or (target variable) assignment.
     *  Separators: space, '=', ':', '|' */
    kKmkWordCtx_DepFileOrAssignment,
    /** Dependency file.
     *  Separators: space, '|' */
    kKmkWordCtx_DepFile,
    /** Last context which may do double expansion. */
    kKmkWordCtx_LastDoubleExpansion = kKmkWordCtx_DepFile
} KMKWORDCTX;

typedef struct KMKWORDSTATE
{
    uint16_t uDepth;
    char     chOpen;
} KMKWORDSTATE;

typedef enum KMKTOKEN
{
    kKmkToken_Word = 0,
    kKmkToken_Comment,

    /* Conditionals: */
    kKmkToken_ifeq,
    kKmkToken_ifneq,
    kKmkToken_if1of,
    kKmkToken_ifn1of,
    kKmkToken_ifdef,
    kKmkToken_ifndef,
    kKmkToken_if,
    kKmkToken_else,
    kKmkToken_endif,

    /* Includes: */
    kKmkToken_include,
    kKmkToken_sinclude,
    kKmkToken_dash_include,
    kKmkToken_includedep,
    kKmkToken_includedep_queue,
    kKmkToken_includedep_flush,

    /* Others: */
    kKmkToken_define,
    kKmkToken_endef,
    kKmkToken_export,
    kKmkToken_unexport,
    kKmkToken_local,
    kKmkToken_override,
    kKmkToken_undefine
} KMKTOKEN;

typedef struct KMKPARSER
{
    struct
    {
        KMKTOKEN        enmToken;
        bool            fIgnoreNesting;
        size_t          iLine;
    } aDepth[64];
    unsigned            iDepth;
    unsigned            iActualDepth;
    bool                fInRecipe;

    /** The EOL type of the current line. */
    SCMEOL              enmEol;
    /** The length of the current line. */
    size_t              cchLine;
    /** Pointer to the start of the current line. */
    char const         *pchLine;

    /** @name Only used for rule/assignment parsing.
     * @{ */
    /** Number of continuation lines at current rule/assignment. */
    uint32_t            cLines;
    /** Characters in continuation lines at current rule/assignment. */
    size_t              cchTotalLine;
    /** @} */

    /** The SCM rewriter state. */
    PSCMRWSTATE         pState;
    /** The input stream. */
    PSCMSTREAM          pIn;
    /** The output stream. */
    PSCMSTREAM          pOut;
    /** The settings. */
    PCSCMSETTINGSBASE   pSettings;
    /** Scratch buffer. */
    char                szBuf[4096];
} KMKPARSER;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static char const g_szTabs[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";


static KMKTOKEN scmKmkIdentifyToken(const char *pchWord, size_t cchWord)
{
    static struct { const char *psz; uint32_t cch; KMKTOKEN enmToken; } s_aTokens[] =
    {
        { RT_STR_TUPLE("if"),               kKmkToken_if },
        { RT_STR_TUPLE("ifeq"),             kKmkToken_ifeq },
        { RT_STR_TUPLE("ifneq"),            kKmkToken_ifneq },
        { RT_STR_TUPLE("if1of"),            kKmkToken_if1of },
        { RT_STR_TUPLE("ifn1of"),           kKmkToken_ifn1of },
        { RT_STR_TUPLE("ifdef"),            kKmkToken_ifdef },
        { RT_STR_TUPLE("ifndef"),           kKmkToken_ifndef },
        { RT_STR_TUPLE("else"),             kKmkToken_else },
        { RT_STR_TUPLE("endif"),            kKmkToken_endif },
        { RT_STR_TUPLE("include"),          kKmkToken_include },
        { RT_STR_TUPLE("sinclude"),         kKmkToken_sinclude },
        { RT_STR_TUPLE("-include"),         kKmkToken_dash_include },
        { RT_STR_TUPLE("includedep"),       kKmkToken_includedep },
        { RT_STR_TUPLE("includedep-queue"), kKmkToken_includedep_queue },
        { RT_STR_TUPLE("includedep-flush"), kKmkToken_includedep_flush },
        { RT_STR_TUPLE("define"),           kKmkToken_define },
        { RT_STR_TUPLE("endef"),            kKmkToken_endef },
        { RT_STR_TUPLE("export"),           kKmkToken_export },
        { RT_STR_TUPLE("unexport"),         kKmkToken_unexport },
        { RT_STR_TUPLE("local"),            kKmkToken_local },
        { RT_STR_TUPLE("override"),         kKmkToken_override },
        { RT_STR_TUPLE("undefine"),         kKmkToken_undefine },
    };
    char chFirst = *pchWord;
    if (   chFirst == 'i'
        || chFirst == 'e'
        || chFirst == 'd'
        || chFirst == 's'
        || chFirst == '-'
        || chFirst == 'u'
        || chFirst == 'l'
        || chFirst == 'o')
    {
        for (size_t i = 0; i < RT_ELEMENTS(s_aTokens); i++)
            if (   s_aTokens[i].cch == cchWord
                && *s_aTokens[i].psz == chFirst
                && memcmp(s_aTokens[i].psz, pchWord, cchWord) == 0)
                return s_aTokens[i].enmToken;
    }
#ifdef VBOX_STRICT
    else
        for (size_t i = 0; i < RT_ELEMENTS(s_aTokens); i++)
            Assert(chFirst != *s_aTokens[i].psz);
#endif

    if (chFirst == '#')
        return kKmkToken_Comment;
    return kKmkToken_Word;
}


/**
 * Modifies the fInRecipe state variable, logging changes in verbose mode.
 */
static void scmKmkSetInRecipe(KMKPARSER *pParser, bool fInRecipe)
{
    if (pParser->fInRecipe != fInRecipe)
        ScmVerbose(pParser->pState, 4, "%u: debug: %s\n",
                   ScmStreamTellLine(pParser->pIn), fInRecipe ? "in-recipe" : "not-in-recipe");
    pParser->fInRecipe = fInRecipe;
}


/**
 * Gives up on the current line, copying it as it and requesting manual repair.
 */
static bool scmKmkGiveUp(KMKPARSER *pParser, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    ScmFixManually(pParser->pState, "%u: %N\n", ScmStreamTellLine(pParser->pIn), pszFormat, &va);
    va_end(va);

    ScmStreamPutLine(pParser->pOut, pParser->pchLine, pParser->cchLine, pParser->enmEol);
    return false;
}


static bool scmKmkIsLineWithContinuationSlow(const char *pchLine, size_t cchLine)
{
    size_t cchSlashes = 1;
    cchLine--;
    while (cchSlashes < cchLine && pchLine[cchLine - cchSlashes - 1] == '\\')
        cchSlashes++;
    return RT_BOOL(cchSlashes & 1);
}


DECLINLINE(bool) scmKmkIsLineWithContinuation(const char *pchLine, size_t cchLine)
{
    if (cchLine == 0 || pchLine[cchLine - 1] != '\\')
        return false;
    return scmKmkIsLineWithContinuationSlow(pchLine, cchLine);
}


/**
 * Finds the length of a line where line continuation is in play.
 *
 * @returns Length from start of current line to the final unescaped EOL.
 * @param   pParser         The KMK parser state.
 * @param   pcLine          Where to return the number of lines.  Optional.
 * @param   pcchMaxLeadWord Where to return the max lead word length on
 *                          subsequent lines. Used to help balance multi-line
 *                          'if' statements (imperfect).  Optional.
 */
static size_t scmKmkLineContinuationPeek(KMKPARSER *pParser, uint32_t *pcLines, size_t *pcchMaxLeadWord)
{
    size_t const offSaved       = ScmStreamTell(pParser->pIn);
    uint32_t     cLines         = 1;
    size_t       cchMaxLeadWord = 0;
    const char  *pchLine        = pParser->pchLine;
    size_t       cchLine        = pParser->cchLine;
    SCMEOL       enmEol;
    for (;;)
    {
        /* Return if no line continuation (or end of stream): */
        if (   cchLine == 0
            || !scmKmkIsLineWithContinuation(pchLine, cchLine)
            || ScmStreamIsEndOfStream(pParser->pIn))
        {
            ScmStreamSeekAbsolute(pParser->pIn, offSaved);
            if (pcLines)
                *pcLines = cLines;
            if (pcchMaxLeadWord)
                *pcchMaxLeadWord = cchMaxLeadWord;
            return (size_t)(pchLine - pParser->pchLine) + cchLine;
        }

        /* Get the next line: */
        pchLine = ScmStreamGetLine(pParser->pIn, &cchLine, &enmEol);
        cLines++;

        /* Check the length of the first word if requested: */
        if (pcchMaxLeadWord)
        {
            size_t offLine = 0;
            while (offLine < cchLine && RT_C_IS_BLANK(pchLine[offLine]))
                offLine++;

            size_t const offStartWord = offLine;
            while (offLine < cchLine && !RT_C_IS_BLANK(pchLine[offLine]))
                offLine++;

            if (offLine - offStartWord > cchMaxLeadWord)
                cchMaxLeadWord = offLine - offStartWord;
        }
    }
}


/**
 * Checks if the given line contains a comment with the @a pszMarker word in it.
 *
 * This can be used to disable warnings.
 *
 * @returns true if this is the case, false if not
 * @param   pchLine     The line.
 * @param   cchLine     The line length.
 * @param   offLine     The current line position, 0 if uncertain.
 * @param   pszMarker   The marker to check for.
 * @param   cchMarker   The length of the marker string.
 */
static bool scmKmkHasCommentMarker(const char *pchLine, size_t cchLine, size_t offLine, const char *pszMarker, size_t cchMarker)
{
    const char *pchCur = (const char *)memchr(&pchLine[offLine], '#', cchLine - RT_MIN(offLine, cchLine));
    if (pchCur)
    {
        pchCur++;
        size_t cchLeft = (size_t)(&pchLine[cchLine] - pchCur);
        while (cchLeft >= cchMarker)
        {
            const char *pchHit = (char *)memchr(pchCur, *pszMarker, cchLeft - cchMarker + 1);
            if (!pchHit)
                break;
            if (memcmp(pchHit, pszMarker, cchMarker) == 0)
                return true;
            pchCur  = pchHit + 1;
            cchLeft = (size_t)(&pchLine[cchLine] - pchCur);
        }
    }
    return false;
}


/**
 * Pushes a if or define on the nesting stack.
 */
static bool scmKmkPushNesting(KMKPARSER *pParser, KMKTOKEN enmToken)
{
    uint32_t iDepth = pParser->iDepth;
    if (iDepth + 1 >= RT_ELEMENTS(pParser->aDepth))
    {
        ScmError(pParser->pState, VERR_ASN1_TOO_DEEPLY_NESTED /*?*/,
                 "%u: Too deep if/define nesting!\n", ScmStreamTellLine(pParser->pIn));
        return false;
    }

    pParser->aDepth[iDepth].enmToken       = enmToken;
    pParser->aDepth[iDepth].iLine          = ScmStreamTellLine(pParser->pIn);
    pParser->aDepth[iDepth].fIgnoreNesting = false;
    pParser->iDepth        = iDepth + 1;
    pParser->iActualDepth += 1;
    ScmVerbose(pParser->pState, 5, "%u: debug: nesting %u (token %u)\n", pParser->aDepth[iDepth].iLine, iDepth + 1, enmToken);
    return true;
}


/**
 * Checks if we're inside a define or not.
 */
static bool scmKmkIsInsideDefine(KMKPARSER const *pParser)
{
    unsigned iDepth = pParser->iDepth;
    while (iDepth-- > 0)
        if (pParser->aDepth[iDepth].enmToken == kKmkToken_define)
            return true;
    return false;
}


/**
 * Skips a string stopping at @a chStop1 or @a chStop2, taking $() and ${} into
 * account.
 */
static size_t scmKmkSkipExpString(const char *pchLine, size_t cchLine, size_t off, char chStop1, char chStop2 = '\0')
{
    unsigned iExpDepth = 0;
    char     ch;
    while (   off < cchLine
           && (ch = pchLine[off])
           && (    (ch != chStop1 && ch != chStop2)
                || iExpDepth > 0))
    {
        off++;
        if (ch == '$')
        {
            ch = pchLine[off];
            if (ch == '(' || ch == '{')
            {
                iExpDepth++;
                off++;
            }
        }
        else if ((ch == ')' || ch == '}') && iExpDepth > 0)
            iExpDepth--;
    }
    return off;
}


/**
 * Finds the length of the word (file) @a offStart.
 *
 * This only takes one line into account, so variable expansions (function
 * calls) spanning multiple lines will be handled as one word per line with help
 * from @a pState.  This allows the caller to properly continutation intend the
 * additional lines.
 *
 * @returns Length of word starting at @a offStart. Zero if there is whitespace
 *          at given offset or it's beyond the end of the line (both cases will
 *          assert).
 * @param   pchLine             The line.
 * @param   cchLine             The line length.
 * @param   offStart            Offset to the start of the word.
 * @param   pState              Where multiline variable expansion is tracked.
 */
static size_t scmKmkWordLength(const char *pchLine, size_t cchLine, size_t offStart, KMKWORDCTX enmCtx, KMKWORDSTATE *pState)
{
    AssertReturn(offStart < cchLine && !RT_C_IS_BLANK(pchLine[offStart]), 0);

    /*
     * Drop any line continuation slash from the line length, so we don't count
     * it into the word length. Also, any spaces preceding it (for multiline
     * variable function expansion).  ASSUMES no trailing slash escaping.
     */
    if (cchLine > 0 && pchLine[cchLine - 1] == '\\')
        do
            cchLine--;
        while (cchLine > offStart && RT_C_IS_SPACE(pchLine[cchLine - 1]));

    /*
     * If we were inside a variable function expansion, continue till we reach the end.
     * This kind of duplicates the code below.
     */
    size_t off = offStart;
    if (pState->uDepth > 0)
    {
        Assert(pState->chOpen == '(' || pState->chOpen == '{');
        char const chOpen  = pState->chOpen;
        char const chClose = chOpen == '(' ? ')' : '}';
        unsigned   uDepth  = pState->uDepth;
        for (;;)
        {
            char ch;
            if (off < cchLine)
                ch = pchLine[off++];
            else /* Reached the end while still inside the expansion. */
            {
                pState->chOpen = chOpen;
                pState->uDepth = (uint16_t)uDepth;
                return cchLine - offStart;
            }
            if (ch == chOpen)
                uDepth++;
            else if (ch == chClose && --uDepth == 0)
                break;
        }
        pState->uDepth = 0;
        pState->chOpen = 0;
    }

    /*
     * Process till we find blank or end of the line.
     */
    while (off < cchLine)
    {
        char ch = pchLine[off];
        if (RT_C_IS_BLANK(ch))
            break;

        if (ch == '$')
        {
            /*
             * Skip variable expansion. ASSUMING double expansion being enabled
             * for rules, we respond to both $() and $$() here, $$$$()
             */
            size_t cDollars = 0;
            do
            {
                off++;
                if (off >= cchLine)
                    return cchLine - offStart;
                cDollars++;
                ch = pchLine[off];
            } while (ch == '$');
            if ((cDollars & 1) || (cDollars == 2 && enmCtx <= kKmkWordCtx_LastDoubleExpansion))
            {
                char const chOpen = ch;
                if (ch == '(' || ch == '{')
                {
                    char const chClose = chOpen == '(' ? ')' : '}';
                    unsigned   uDepth  = 1;
                    off++;
                    for (;;)
                    {
                        if (off < cchLine)
                            ch = pchLine[off++];
                        else /* Reached the end while inside the expansion. */
                        {
                            pState->chOpen = chOpen;
                            pState->uDepth = (uint16_t)uDepth;
                            return cchLine - offStart;
                        }
                        if (ch == chOpen)
                            uDepth++;
                        else if (ch == chClose && --uDepth == 0)
                            break;
                    }
                }
                else if (cDollars & 1)
                    off++; /* $X */
            }
            continue;
        }
        else if (ch == ':')
        {
            /*
             * Check for plain driver letter, omitting the archive member variant.
             */
            if (off - offStart != 1 || !RT_C_IS_ALPHA(pchLine[off - 1]))
            {
                if (off == offStart)
                {
                    /* We need to check for single and double colon rules as well as
                       simple and immediate assignments here. */
                    off++;
                    if (pchLine[off] == ':')
                    {
                        off++;
                        if (pchLine[off] == '=')
                        {
                            if (enmCtx == kKmkWordCtx_TargetFileOrAssignment || enmCtx == kKmkWordCtx_DepFileOrAssignment)
                                return 3;   /* ::=  - immediate assignment. */
                            off++;
                        }
                        else if (enmCtx != kKmkWordCtx_DepFile)
                            return 2;       /* ::   - double colon rule */
                    }
                    else if (pchLine[off] == '=')
                    {
                        if (enmCtx == kKmkWordCtx_TargetFileOrAssignment || enmCtx == kKmkWordCtx_DepFileOrAssignment)
                            return 2;       /* :=   - simple assignment. */
                        off++;
                    }
                    else if (enmCtx != kKmkWordCtx_DepFile)
                        return 1;           /* :    - regular rule. */
                    continue;
                }
                /* ':' is a separator except in DepFile context. */
                else if (enmCtx != kKmkWordCtx_DepFile)
                    return off - offStart;
            }
        }
        else if (ch == '=')
        {
            /*
             * Assignment.  We check for the previous character too so we'll catch
             * append, prepend and conditional assignments.  Simple and immediate
             * assignments are handled above.
             */
            if (   enmCtx == kKmkWordCtx_TargetFileOrAssignment
                || enmCtx == kKmkWordCtx_DepFileOrAssignment)
            {
                if (off > offStart)
                {
                    ch = pchLine[off - 1];
                    if (ch == '?' || ch == '+' || ch == '>')
                        off = off - 1 == offStart
                            ? off + 2  /* return '+=', '?=', '<=' */
                            : off - 1; /* up to '+=', '?=', '<=' */
                    else
                        Assert(ch != ':'); /* handled above */
                }
                else
                    off++;  /* '=' */
                return off - offStart;
            }
        }
        else if (ch == '|')
        {
            /*
             * This is rather straight forward.
             */
            if (enmCtx == kKmkWordCtx_DepFileOrAssignment || enmCtx == kKmkWordCtx_DepFile)
            {
                if (off == offStart)
                    return 1;
                return off - offStart;
            }
        }
        off++;
    }
    return off - offStart;
}


static bool scmKmkTailComment(KMKPARSER *pParser, const char *pchLine, size_t cchLine, size_t offSrc, char **ppszDst)
{
    /* Wind back offSrc to the first blank space (not all callers can do this). */
    Assert(offSrc <= cchLine);
    while (offSrc > 0 && RT_C_IS_SPACE(pchLine[offSrc - 1]))
        offSrc--;
    size_t const offSrcStart = offSrc;

    /* Skip blanks. */
    while (offSrc < cchLine && RT_C_IS_SPACE(pchLine[offSrc]))
        offSrc++;
    if (offSrc >= cchLine)
        return true;

    /* Is it a comment? */
    char *pszDst = *ppszDst;
    if (pchLine[offSrc] == '#')
    {
        /* Try preserve the start column number. */
/** @todo tabs */
        size_t const offDst = pszDst - pParser->szBuf;
        if (offDst < offSrc)
        {
            memset(pszDst, ' ', offSrc - offDst);
            pszDst += offSrc - offDst;
        }
        else if (offSrc != offSrcStart)
            *pszDst++ = ' ';

        *ppszDst = pszDst = (char *)mempcpy(pszDst, &pchLine[offSrc], cchLine - offSrc);
        return false; /*dummy*/
    }

    /* Complain and copy out the text unmodified. */
    ScmError(pParser->pState, VERR_PARSE_ERROR, "%u:%u: Expected comment, found: %.*s",
             ScmStreamTellLine(pParser->pIn), offSrc, cchLine - offSrc, &pchLine[offSrc]);
    *ppszDst = (char *)mempcpy(pszDst, &pchLine[offSrcStart], cchLine - offSrcStart);
    return false; /*dummy*/
}


/**
 * Deals with: ifeq, ifneq, if1of and ifn1of
 *
 * @returns dummy (false) to facility return + call.
 */
static bool scmKmkHandleIfParentheses(KMKPARSER *pParser, size_t offToken, KMKTOKEN enmToken, size_t cchToken, bool fElse)
{
    const char        *pchLine   = pParser->pchLine;
    size_t             cchLine   = pParser->cchLine;
    uint32_t const     cchIndent = pParser->iActualDepth
                                 - (fElse && pParser->iActualDepth > 0 && !pParser->aDepth[pParser->iDepth - 1].fIgnoreNesting);
    const char * const pchToken = &pchLine[offToken];

    /*
     * Push it onto the stack.  All these nestings are relevant.
     */
    if (!fElse)
    {
        if (!scmKmkPushNesting(pParser, enmToken))
            return false;
    }
    else
    {
        pParser->aDepth[pParser->iDepth - 1].enmToken = enmToken;
        pParser->aDepth[pParser->iDepth - 1].iLine    = ScmStreamTellLine(pParser->pIn);
    }

    /*
     * We do not allow line continuation for these.
     */
    uint32_t cLines         = 1;
    size_t   cchMaxLeadWord = 0;
    size_t   cchTotalLine   = cchLine;
    if (scmKmkIsLineWithContinuation(pchLine, cchLine))
    {
        if (enmToken != kKmkToken_if1of  && enmToken != kKmkToken_ifn1of)
            return scmKmkGiveUp(pParser, "Line continuation not allowed with '%.*s' directive.", cchToken, pchToken);
        cchTotalLine = scmKmkLineContinuationPeek(pParser, &cLines, &cchMaxLeadWord);
    }

    /*
     * We stage the modified line in the buffer, so check that the line isn't
     * too long (it seriously should be).
     */
    if (cchTotalLine + cchIndent + 32 > sizeof(pParser->szBuf))
        return scmKmkGiveUp(pParser, "Line too long for a '%.*s' directive: %u chars", cchToken, pchToken, cchTotalLine);
    char *pszDst = pParser->szBuf;

    /*
     * Emit indent and initial token.
     */
    memset(pszDst, ' ', cchIndent);
    pszDst += cchIndent;

    if (fElse)
        pszDst = (char *)mempcpy(pszDst, RT_STR_TUPLE("else "));

    memcpy(pszDst, pchToken, cchToken);
    pszDst += cchToken;

    size_t offSrc = offToken + cchToken;

    /*
     * There shall be exactly one space between the token and the opening parenthesis.
     */
    if (pchLine[offSrc] == ' ' && pchLine[offSrc + 1] == '(')
        offSrc += 2;
    else
    {
        while (offSrc < cchLine && RT_C_IS_BLANK(pchLine[offSrc]))
            offSrc++;
        if (pchLine[offSrc] != '(')
            return scmKmkGiveUp(pParser, "Expected '(' to follow '%.*s'", cchToken, pchToken);
        offSrc++;
    }
    *pszDst++ = ' ';
    *pszDst++ = '(';
    size_t const offContIndent = pszDst - pParser->szBuf;

    /*
     * Skip spaces after the opening parenthesis.
     *
     * Note! We don't support/grok any line continuation stuff here.
     */
    while (offSrc < cchLine && RT_C_IS_BLANK(pchLine[offSrc]))
        offSrc++;

    /*
     * Work up to the ',' separator.  It shall likewise not be preceded by any spaces.
     * Need to take $(func 1,2,3) calls into account here, so we trac () and {} while
     * skipping ahead.
     *
     * Note! We don't support/grok any line continuation stuff here.
     */
    if (pchLine[offSrc] != ',')
    {
        size_t const offSrcStart = offSrc;
        offSrc = scmKmkSkipExpString(pchLine, cchLine, offSrc, ',');
        if (pchLine[offSrc] != ',')
            return scmKmkGiveUp(pParser, "Expected ',' somewhere after '%.*s('", cchToken, pchToken);

        size_t cchCopy = offSrc - offSrcStart;
        while (cchCopy > 0 && RT_C_IS_BLANK(pchLine[offSrcStart + cchCopy - 1]))
            cchCopy--;

        pszDst = (char *)mempcpy(pszDst, &pchLine[offSrcStart], cchCopy);
    }
    /* 'if1of(, stuff)' does not make sense in committed code: */
    else if (enmToken == kKmkToken_if1of || enmToken == kKmkToken_ifn1of)
        return scmKmkGiveUp(pParser, "Left set cannot be empty for '%.*s'", cchToken, pchToken);
    offSrc++;
    *pszDst++ = ',';

    /*
     * For if1of and ifn1of we require a space after the comma, whereas ifeq and
     * ifneq shall not have any blanks.  This is to help tell them apart.
     */
    if (enmToken == kKmkToken_if1of || enmToken == kKmkToken_ifn1of)
        *pszDst++ = ' ';
    while (offSrc < cchLine && RT_C_IS_BLANK(pchLine[offSrc]))
        offSrc++;

    if (pchLine[offSrc] != ')')
    {
        do
        {
            if (pchLine[offSrc] == '\\' && offSrc + 1 == cchLine)
            {
                if (pszDst[-1] != ' ')
                    *pszDst++ = ' ';
                *pszDst++ = '\\';
                *pszDst   = '\0';
                ScmStreamPutLine(pParser->pOut, pParser->szBuf, pszDst - pParser->szBuf, pParser->enmEol);
                pszDst = pParser->szBuf;

                memset(pszDst, ' ', offContIndent);
                pszDst += offContIndent;

                pParser->pchLine = pchLine = ScmStreamGetLine(pParser->pIn, &pParser->cchLine, &pParser->enmEol);
                cchLine = pParser->cchLine;
                offSrc = 0;
                while (offSrc < cchLine && RT_C_IS_BLANK(pchLine[offSrc]))
                    offSrc++;
                cLines -= 1;
            }

            size_t const offSrcStart = offSrc;
            offSrc = scmKmkSkipExpString(pchLine, cLines <= 1 ? cchLine : cchLine - 1, offSrc, ')');
            if (pchLine[offSrc] != ')' && cLines <= 1)
                return scmKmkGiveUp(pParser, "No closing parenthesis for '%.*s'?", cchToken, pchToken);

            size_t cchCopy = offSrc - offSrcStart;
            while (cchCopy > 0 && RT_C_IS_BLANK(pchLine[offSrcStart + cchCopy - 1]))
                cchCopy--;

            pszDst = (char *)mempcpy(pszDst, &pchLine[offSrcStart], cchCopy);
        } while (offSrc + 1 == cchLine && pchLine[offSrc] == '\\');
    }
    /* 'if1of(stuff, )' does not make sense in committed code: */
    else if (   (enmToken == kKmkToken_if1of || enmToken == kKmkToken_ifn1of)
             && !scmKmkHasCommentMarker(pchLine, cchLine, offSrc, RT_STR_TUPLE("scm:ignore-empty-if1of-set")))
        return scmKmkGiveUp(pParser, "Right set cannot be empty for '%.*s'", cchToken, pchToken);
    offSrc++;
    *pszDst++ = ')';

    /*
     * Handle comment.
     */
    if (offSrc < cchLine)
        scmKmkTailComment(pParser, pchLine, cchLine, offSrc, &pszDst);

    /*
     * Done.
     */
    *pszDst = '\0';
    ScmStreamPutLine(pParser->pOut, pParser->szBuf, pszDst - pParser->szBuf, pParser->enmEol);
    return false; /* dummy */
}


/**
 * Deals with: if, ifdef and ifndef
 *
 * @returns dummy (false) to facility return + call.
 */
static bool scmKmkHandleIfSpace(KMKPARSER *pParser, size_t offToken, KMKTOKEN enmToken, size_t cchToken, bool fElse)
{
    const char     *pchLine   = pParser->pchLine;
    size_t          cchLine   = pParser->cchLine;
    uint32_t const  cchIndent = pParser->iActualDepth
                              - (fElse && pParser->iActualDepth > 0 && !pParser->aDepth[pParser->iDepth - 1].fIgnoreNesting);

    /*
     * Push it onto the stack.
     *
     * For ifndef we ignore the outmost ifndef in non-Makefile.kmk files, if
     * the define matches the typical pattern for a file blocker.
     */
    bool fIgnoredNesting = false;
    if (!fElse)
    {
        if (!scmKmkPushNesting(pParser, enmToken))
            return false;
        if (enmToken == kKmkToken_ifndef)
        {
            /** @todo */
        }
    }
    else
    {
        pParser->aDepth[pParser->iDepth - 1].enmToken = enmToken;
        pParser->aDepth[pParser->iDepth - 1].iLine    = ScmStreamTellLine(pParser->pIn);
    }

    /*
     * We do not allow line continuation for ifdef and ifndef, only if.
     */
    uint32_t cLines         = 1;
    size_t   cchMaxLeadWord = 0;
    size_t   cchTotalLine   = cchLine;
    if (scmKmkIsLineWithContinuation(pchLine, cchLine))
    {
        if (enmToken != kKmkToken_if)
            return scmKmkGiveUp(pParser, "Line continuation not allowed with '%.*s' directive.", cchToken, &pchLine[offToken]);
        cchTotalLine = scmKmkLineContinuationPeek(pParser, &cLines, &cchMaxLeadWord);
    }

    /*
     * We stage the modified line in the buffer, so check that the line isn't
     * too long (plain if can be long, but not ifndef/ifdef).
     */
    if (cchTotalLine + pParser->iActualDepth + 32 > sizeof(pParser->szBuf))
        return scmKmkGiveUp(pParser, "Line too long for a '%.*s' directive: %u chars",
                            cchToken, &pchLine[offToken], cchTotalLine);
    char *pszDst = pParser->szBuf;

    /*
     * Emit indent and initial token.
     */
    memset(pszDst, ' ', cchIndent);
    pszDst += cchIndent;

    if (fElse)
        pszDst = (char *)mempcpy(pszDst, RT_STR_TUPLE("else "));

    memcpy(pszDst, &pchLine[offToken], cchToken);
    pszDst += cchToken;

    size_t offSrc = offToken + cchToken;

    /*
     * ifndef/ifdef shall have exactly one space.  For 'if' we allow up to 4, but
     * we'll deal with that further down.
     */
    size_t cchSpaces = 0;
    while (offSrc < cchLine && RT_C_IS_BLANK(pchLine[offSrc]))
    {
        cchSpaces++;
        offSrc++;
    }
    if (cchSpaces == 0)
        return scmKmkGiveUp(pParser, "Nothing following '%.*s' or bogus line continuation?", cchToken, &pchLine[offToken]);
    *pszDst++ = ' ';

    /*
     * For ifdef and ifndef there now comes a single word.
     */
    if (enmToken != kKmkToken_if)
    {
        size_t const offSrcStart = offSrc;
        offSrc = scmKmkSkipExpString(pchLine, cchLine, offSrc, ' ', '\t'); /** @todo probably not entirely correct */
        if (offSrc == offSrcStart)
            return scmKmkGiveUp(pParser, "No word following '%.*s'?", cchToken, &pchLine[offToken]);

        pszDst = (char *)mempcpy(pszDst, &pchLine[offSrcStart], offSrc - offSrcStart);
    }
    /*
     * While for 'if' things are more complicated, especially if it spans more
     * than one line.
     */
    else if (cLines <= 1)
    {
        /* Single line expression: Just assume the expression goes up to the
           EOL or comment hash. Strip and copy as-is for now. */
        const char *pchSrcHash = (const char *)memchr(&pchLine[offSrc], '#', cchLine - offSrc);
        size_t      cchExpr    = pchSrcHash ? pchSrcHash - &pchLine[offSrc] : cchLine - offSrc;
        while (cchExpr > 0 && RT_C_IS_BLANK(pchLine[offSrc + cchExpr - 1]))
            cchExpr--;

        pszDst = (char *)mempcpy(pszDst, &pchLine[offSrc], cchExpr);
        offSrc += cchExpr;
    }
    else
    {
        /* Multi line expression: We normalize leading whitespace using
           cchMaxLeadWord for now.  Expression on line 2+ are indented by two
           extra characters, because we'd otherwise be puttin the operator on
           the same level as the 'if', which would be confusing.  Thus:

                if  expr1
                  + expr2
                endif

                if   expr1
                  || expr2
                endif

                if    expr3
                  vtg expr4
                endif

           We do '#' / EOL handling for the final line the same way as above.

           Later we should add the ability to rework the expression properly,
           making sure new lines starts with operators and such. */
        /** @todo Implement simples expression parser and indenter, possibly also
         *        removing unnecessary parentheses.  Can be shared with C/C++. */
        if (cchMaxLeadWord > 3)
            return scmKmkGiveUp(pParser,
                                "Bogus multi-line 'if' expression! Extra lines must start with operator (cchMaxLeadWord=%u).",
                                cchMaxLeadWord);
        memset(pszDst, ' ', cchMaxLeadWord);
        pszDst += cchMaxLeadWord;

        size_t cchSrcContIndent = offToken + 2;
        for (uint32_t iSubLine = 0; iSubLine < cLines - 1; iSubLine++)
        {
            /* Trim the line. */
            size_t offSrcEnd = cchLine;
            Assert(pchLine[offSrcEnd - 1] == '\\');
            offSrcEnd--;

            if (pchLine[offSrcEnd - 1] == '\\')
                return scmKmkGiveUp(pParser, "Escaped '\\' before line continuation in 'if' expression is not allowed!");

            while (offSrcEnd > offSrc && RT_C_IS_BLANK(pchLine[offSrcEnd - 1]))
                offSrcEnd--;

            /* Comments with line continuation is not allowed in commited makefiles. */
            if (offSrc < offSrcEnd && memchr(&pchLine[offSrc], '#', cchLine - offSrc) != NULL)
                return scmKmkGiveUp(pParser, "Comment in multi-line 'if' expression is not allowed to start before the final line!");

            /* Output it. */
            if (offSrc < offSrcEnd)
            {
                if (iSubLine > 0 && offSrc > cchSrcContIndent)
                {
                    memset(pszDst, ' ', offSrc - cchSrcContIndent);
                    pszDst += offSrc - cchSrcContIndent;
                }
                pszDst = (char *)mempcpy(pszDst, &pchLine[offSrc], offSrcEnd - offSrc);
                *pszDst++ = ' ';
            }
            else if (iSubLine == 0)
                return scmKmkGiveUp(pParser, "Expected expression after 'if', not line continuation!");
            *pszDst++ = '\\';
            *pszDst   = '\0';
            size_t cchDst = (size_t)(pszDst - pParser->szBuf);
            ScmStreamPutLine(pParser->pOut, pParser->szBuf, cchDst, pParser->enmEol);

            /*
             * Fetch the next line and start processing it.
             */
            pParser->pchLine = pchLine = ScmStreamGetLine(pParser->pIn, &pParser->cchLine, &pParser->enmEol);
            if (!pchLine)
            {
                ScmError(pParser->pState, VERR_INTERNAL_ERROR_3, "ScmStreamGetLine unexpectedly returned NULL!");
                return false;
            }
            cchLine = pParser->cchLine;

            /* Skip leading whitespace and adjust the source continuation indent: */
            offSrc = 0;
            while (offSrc < cchLine && RT_C_IS_SPACE(pchLine[offSrc]))
                offSrc++;
            /** @todo tabs */

            if (iSubLine == 0)
                cchSrcContIndent = offSrc;

            /* Initial indent: */
            pszDst = pParser->szBuf;
            memset(pszDst, ' ', cchIndent + 2);
            pszDst += cchIndent + 2;
        }

        /* Output the expression on the final line. */
        const char *pchSrcHash = (const char *)memchr(&pchLine[offSrc], '#', cchLine - offSrc);
        size_t      cchExpr    = pchSrcHash ? pchSrcHash - &pchLine[offSrc] : cchLine - offSrc;
        while (cchExpr > 0 && RT_C_IS_BLANK(pchLine[offSrc + cchExpr - 1]))
            cchExpr--;

        pszDst = (char *)mempcpy(pszDst, &pchLine[offSrc], cchExpr);
        offSrc += cchExpr;
    }


    /*
     * Handle comment.
     *
     * Here we check for the "scm:ignore-nesting" directive that makes us not
     * add indentation for this directive.  We do this on the destination buffer
     * as that can be zero terminated and is therefore usable with strstr.
     */
    if (offSrc >= cchLine)
        *pszDst = '\0';
    else
    {
        char * const pszDstSrc = pszDst;
        scmKmkTailComment(pParser, pchLine, cchLine, offSrc, &pszDst);
        *pszDst = '\0';

        /* Check for special comment making us ignore the nesting.  We do this
           on the destination buffer since it's zero terminated allowing normal
           strstr use. */
        if (!fIgnoredNesting && strstr(pszDstSrc, "scm:ignore-nesting") != NULL)
        {
            pParser->aDepth[pParser->iDepth - 1].fIgnoreNesting = true;
            pParser->iActualDepth--;
            ScmVerbose(pParser->pState, 5, "%u: debug: ignoring nesting - actual depth: %u\n",
                       pParser->aDepth[pParser->iDepth - 1].iLine, pParser->iActualDepth);
        }
    }

    /*
     * Done.
     */
    ScmStreamPutLine(pParser->pOut, pParser->szBuf, pszDst - pParser->szBuf, pParser->enmEol);
    return false; /* dummy */
}


/**
 * Deals with: else
 *
 * @returns dummy (false) to facility return + call.
 */
static bool scmKmkHandleElse(KMKPARSER *pParser, size_t offToken)
{
    const char * const pchLine   = pParser->pchLine;
    size_t  const      cchLine   = pParser->cchLine;

    if (pParser->iDepth < 1)
        return scmKmkGiveUp(pParser, "Lone 'else'");
    uint32_t const cchIndent = pParser->iActualDepth
                             - (pParser->iActualDepth > 0 && !pParser->aDepth[pParser->iDepth - 1].fIgnoreNesting);

    /*
     * Look past the else and check if there any ifxxx token following it.
     */
    size_t offSrc = offToken + 4;
    while (offSrc < cchLine && RT_C_IS_BLANK(pchLine[offSrc]))
        offSrc++;
    if (offSrc < cchLine)
    {
        size_t cchWord = 0;
        while (offSrc + cchWord < cchLine && RT_C_IS_ALNUM(pchLine[offSrc + cchWord]))
            cchWord++;
        if (cchWord)
        {
            KMKTOKEN enmToken = scmKmkIdentifyToken(&pchLine[offSrc], cchWord);
            switch (enmToken)
            {
                case kKmkToken_ifeq:
                case kKmkToken_ifneq:
                case kKmkToken_if1of:
                case kKmkToken_ifn1of:
                    return scmKmkHandleIfParentheses(pParser, offSrc, enmToken, cchWord, true /*fElse*/);

                case kKmkToken_ifdef:
                case kKmkToken_ifndef:
                case kKmkToken_if:
                    return scmKmkHandleIfSpace(pParser, offSrc, enmToken, cchWord, true /*fElse*/);

                default:
                    break;
            }
        }
    }

    /*
     * We do not allow line continuation for these.
     */
    if (scmKmkIsLineWithContinuation(pchLine, cchLine))
        return scmKmkGiveUp(pParser, "Line continuation not allowed with 'else' directive.");

    /*
     * We stage the modified line in the buffer, so check that the line isn't
     * too long (it seriously should be).
     */
    if (cchLine + cchIndent + 32 > sizeof(pParser->szBuf))
        return scmKmkGiveUp(pParser, "Line too long for a 'else' directive: %u chars", cchLine);
    char *pszDst = pParser->szBuf;

    /*
     * Emit indent and initial token.
     */
    memset(pszDst, ' ', cchIndent);
    pszDst = (char *)mempcpy(&pszDst[cchIndent], RT_STR_TUPLE("else"));

    offSrc = offToken + 4;

    /*
     * Handle comment.
     */
    if (offSrc < cchLine)
        scmKmkTailComment(pParser, pchLine, cchLine, offSrc, &pszDst);

    /*
     * Done.
     */
    *pszDst = '\0';
    ScmStreamPutLine(pParser->pOut, pParser->szBuf, pszDst - pParser->szBuf, pParser->enmEol);
    return false; /* dummy */
}


/**
 * Deals with: endif
 *
 * @returns dummy (false) to facility return + call.
 */
static bool scmKmkHandleEndif(KMKPARSER *pParser, size_t offToken)
{
    const char * const pchLine   = pParser->pchLine;
    size_t  const      cchLine   = pParser->cchLine;

    /*
     * Pop a nesting.
     */
    if (pParser->iDepth < 1)
        return scmKmkGiveUp(pParser, "Lone 'endif'");
    uint32_t iDepth = pParser->iDepth - 1;
    pParser->iDepth = iDepth;
    if (!pParser->aDepth[iDepth].fIgnoreNesting)
    {
        AssertStmt(pParser->iActualDepth > 0, pParser->iActualDepth++);
        pParser->iActualDepth -= 1;
    }
    ScmVerbose(pParser->pState, 5, "%u: debug: unnesting %u/%u (endif)\n",
               ScmStreamTellLine(pParser->pIn), iDepth, pParser->iActualDepth);
    uint32_t const cchIndent = pParser->iActualDepth;

    /*
     * We do not allow line continuation for these.
     */
    if (scmKmkIsLineWithContinuation(pchLine, cchLine))
        return scmKmkGiveUp(pParser, "Line continuation not allowed with 'endif' directive.");

    /*
     * We stage the modified line in the buffer, so check that the line isn't
     * too long (it seriously should be).
     */
    if (cchLine + cchIndent + 32 > sizeof(pParser->szBuf))
        return scmKmkGiveUp(pParser, "Line too long for a 'else' directive: %u chars", cchLine);
    char *pszDst = pParser->szBuf;

    /*
     * Emit indent and initial token.
     */
    memset(pszDst, ' ', cchIndent);
    pszDst = (char *)mempcpy(&pszDst[cchIndent], RT_STR_TUPLE("endif"));

    size_t offSrc = offToken + 5;

    /*
     * Handle comment.
     */
    if (offSrc < cchLine)
        scmKmkTailComment(pParser, pchLine, cchLine, offSrc, &pszDst);

    /*
     * Done.
     */
    *pszDst = '\0';
    ScmStreamPutLine(pParser->pOut, pParser->szBuf, pszDst - pParser->szBuf, pParser->enmEol);
    return false; /* dummy */
}


/**
 * Passing thru any line continuation lines following the current one.
 */
static bool scmKmkPassThruLineContinuationLines(KMKPARSER *pParser)
{
    while (scmKmkIsLineWithContinuation(pParser->pchLine, pParser->cchLine))
    {
        pParser->pchLine = ScmStreamGetLine(pParser->pIn, &pParser->cchLine, &pParser->enmEol);
        if (!pParser->pchLine)
            break;
        ScmStreamPutLine(pParser->pOut, pParser->pchLine, pParser->cchLine, pParser->enmEol);
    }
    return false; /* dummy */
}


/**
 * For dealing with a directive w/o special formatting rules (yet).
 *
 * @returns dummy (false) to facility return + call.
 */
static bool scmKmkHandleSimple(KMKPARSER *pParser, size_t offToken, bool fIndentIt = true)
{
    const char    *pchLine   = pParser->pchLine;
    size_t         cchLine   = pParser->cchLine;
    uint32_t const cchIndent = fIndentIt ? pParser->iActualDepth : 0;

    /*
     * Just reindent the statement.
     */
    ScmStreamWrite(pParser->pOut, g_szSpaces, cchIndent);
    ScmStreamWrite(pParser->pOut, &pchLine[offToken], cchLine - offToken);
    ScmStreamPutEol(pParser->pOut, pParser->enmEol);

    /*
     * Check for line continuation and output concatenated lines.
     */
    scmKmkPassThruLineContinuationLines(pParser);
    return false; /* dummy */
}


static bool scmKmkHandleDefine(KMKPARSER *pParser, size_t offToken)
{
    scmKmkHandleSimple(pParser, offToken);

    /* Hack Alert! Start out parsing the define in recipe mode.

       Technically, we shouldn't evaluate the content of a define till it's
       used. However, we ASSUME they are either makefile code snippets or
       recipe templates.  */
    scmKmkPushNesting(pParser, kKmkToken_define);
    scmKmkSetInRecipe(pParser, true);
    return false;
}


static bool scmKmkHandleEndef(KMKPARSER *pParser, size_t offToken)
{
    /* Leaving a define resets the recipt mode. */
    scmKmkSetInRecipe(pParser, false);

    /*
     * Pop a nesting.
     */
    if (pParser->iDepth < 1)
        return scmKmkGiveUp(pParser, "Lone 'endef'");
    uint32_t iDepth = pParser->iDepth - 1;
    if (pParser->aDepth[iDepth].enmToken != kKmkToken_define)
        return scmKmkGiveUp(pParser, "Unpexected 'endef', expected 'endif' for line %u", pParser->aDepth[iDepth].iLine);
    pParser->iDepth = iDepth;
    if (!pParser->aDepth[iDepth].fIgnoreNesting)
    {
        AssertStmt(pParser->iActualDepth > 0, pParser->iActualDepth++);
        pParser->iActualDepth -= 1;
    }
    ScmVerbose(pParser->pState, 5, "%u: debug: unnesting %u/%u (endef)\n",
               ScmStreamTellLine(pParser->pIn), iDepth, pParser->iActualDepth);

    return scmKmkHandleSimple(pParser, offToken);
}


/**
 * Checks for escaped trailing slashes on a line, giving up and asking the
 * developer to fix those manually.
 *
 * @returns true if we gave up. false if no escaped slashed and we didn't.
 */
static bool scmKmkGiveUpIfTrailingEscapedSlashed(KMKPARSER *pParser, const char *pchLine, size_t cchLine)
{
    if (cchLine > 2 && pchLine[cchLine - 2] == '\\' && pchLine[cchLine - 1] == '\\')
    {
        scmKmkGiveUp(pParser, "Escaped slashes at end of line not allowed. Insert space before line continuation slash!");
        return true;
    }
    return false;
}


/**
 * Scans the given line segment for variable expansion and updates the state
 * accordingly.
 *
 * @returns New cExpandNesting value.
 * @param   cExpandNesting              Current variable expansion nesting
 *                                      level.
 * @param   achExpandingNestingClose    String with the closing character for
 *                                      each expansion level. 256 chars.
 * @param   pchLine                     The string to scan.
 * @param   cchLine                     Length to scan.
 */
static unsigned scmKmkScanStringForExpansions(unsigned cExpandNesting, char achExpandNestingClose[256],
                                              const char *pchLine, size_t cchLine)
{
    while (cchLine-- > 0)
    {
        char ch = *pchLine++;
        switch (ch)
        {
            case '$':
            {
                size_t cDollars = 1;
                while (cchLine > 0 && (ch = *pchLine) == '$')
                    cDollars++, pchLine++, cchLine--;
                if ((cDollars & 1) && cchLine > 0 && (ch == '{' || ch == '('))
                {
                    if (cExpandNesting < 256)
                        achExpandNestingClose[cExpandNesting] = ch == '(' ? ')' : '}';
                    cExpandNesting++;
                    pchLine++;
                    cchLine--;
                }
                break;
            }

            case ')':
            case '}':
                if (cExpandNesting > 0 && (cExpandNesting > 256 || ch == achExpandNestingClose[cExpandNesting - 1]))
                    cExpandNesting--;
                break;
        }
    }
    return cExpandNesting;
}


/**
 * @returns dummy (false) to facility return + call.
 */
static bool scmKmkHandleAssignment2(KMKPARSER *pParser, size_t offVarStart, size_t offVarEnd, KMKASSIGNTYPE enmType,
                                    size_t offAssignOp, unsigned fFlags)
{
    unsigned const      cchIndent    = pParser->iActualDepth;
    const char         *pchLine      = pParser->pchLine;
    size_t              cchLine      = pParser->cchLine;
    uint32_t const      cLines       = pParser->cLines;
    uint32_t            iSubLine     = 0;

    RT_NOREF(fFlags);
    Assert(offVarStart < cchLine);
    Assert(offVarEnd  <= cchLine);
    Assert(offVarStart < offVarEnd);
    Assert(!RT_C_IS_SPACE(pchLine[offVarStart]));
    Assert(!RT_C_IS_SPACE(pchLine[offVarEnd - 1]));

    /* Assignments takes us out of recipe mode. */
    ScmVerbose(pParser->pState, 6, "%u: debug: assignment\n", ScmStreamTellLine(pParser->pIn));
    scmKmkSetInRecipe(pParser, false);

    /* This is too much hazzle to deal with. */
    if (cLines > 1 && scmKmkGiveUpIfTrailingEscapedSlashed(pParser, pchLine, cchLine))
        return false;
    if (cchLine + 64 > sizeof(pParser->szBuf))
        return scmKmkGiveUp(pParser, "Line too long!");

    /*
     * Indent and output the variable name.
     */
    char *pszDst = pParser->szBuf;
    memset(pszDst, ' ', cchIndent);
    pszDst += cchIndent;
    pszDst = (char *)mempcpy(pszDst, &pchLine[offVarStart], offVarEnd - offVarStart);

    /*
     * Try preserve the assignment operator position, but make sure we've got a
     * space in front of it.
     */
    if (offAssignOp < cchLine)
    {
        size_t offDst         = (size_t)(pszDst - pParser->szBuf);
        size_t offEffAssignOp = ScmCalcSpacesForSrcSpan(pchLine, 0, offAssignOp, pParser->pSettings);
        if (offDst < offEffAssignOp)
        {
            size_t cchSpacesToWrite = offEffAssignOp - offDst;
            memset(pszDst, ' ', cchSpacesToWrite);
            pszDst += cchSpacesToWrite;
        }
        else
            *pszDst++ = ' ';
    }
    else
    {
        /* Pull up the assignment operator to the variable line. */
        *pszDst++ = ' ';

        /* Eat up lines till we hit the operator. */
        while (offAssignOp < cchLine)
        {
            const char * const pchPrevLine = pchLine;
            Assert(iSubLine + 1 < cLines);
            pParser->pchLine = pchLine = ScmStreamGetLine(pParser->pIn, &pParser->cchLine, &pParser->enmEol);
            AssertReturn(pchLine, false /*dummy*/);
            cchLine = pParser->cchLine;
            iSubLine++;
            if (iSubLine + 1 < cLines && scmKmkGiveUpIfTrailingEscapedSlashed(pParser, pchLine, cchLine))
                return false;

            /* Adjust offAssignOp: */
            offAssignOp -= (uintptr_t)pchLine - (uintptr_t)pchPrevLine;
            Assert(offAssignOp < ~(size_t)0 / 2);
        }

        if ((size_t)(pszDst - pParser->szBuf) > sizeof(pParser->szBuf))
            return scmKmkGiveUp(pParser, "Line too long!");
    }

    /*
     * Emit the operator.
     */
    size_t offLine = offAssignOp;
    switch (enmType)
    {
        default:
            AssertReleaseFailed();
            RT_FALL_THRU();
        case kKmkAssignType_Recursive:
            *pszDst++ = '=';
            Assert(pchLine[offLine] == '=');
            offLine++;
            break;
        case kKmkAssignType_Conditional:
            *pszDst++ = '?';
            *pszDst++ = '=';
            Assert(pchLine[offLine] == '?'); Assert(pchLine[offLine + 1] == '=');
            offLine += 2;
            break;
        case kKmkAssignType_Appending:
            *pszDst++ = '+';
            *pszDst++ = '=';
            Assert(pchLine[offLine] == '+'); Assert(pchLine[offLine + 1] == '=');
            offLine += 2;
            break;
        case kKmkAssignType_Prepending:
            *pszDst++ = '<';
            *pszDst++ = '=';
            Assert(pchLine[offLine] == '<'); Assert(pchLine[offLine + 1] == '=');
            offLine += 2;
            break;
        case kKmkAssignType_Immediate:
            *pszDst++ = ':';
            Assert(pchLine[offLine] == ':');
            offLine++;
            RT_FALL_THRU();
        case kKmkAssignType_Simple:
            *pszDst++ = ':';
            *pszDst++ = '=';
            Assert(pchLine[offLine] == ':'); Assert(pchLine[offLine + 1] == '=');
            offLine += 2;
            break;
    }

    /*
     * Skip space till we hit the value or comment.
     */
    while (offLine < cchLine && RT_C_IS_SPACE(pchLine[offLine]))
        offLine++;

/** @todo this block can probably be merged into the final loop below. */
    unsigned cPendingEols   = 0;
    while (iSubLine + 1 < cLines && offLine + 1 == cchLine && pchLine[offLine] == '\\')
    {
        pParser->pchLine = pchLine = ScmStreamGetLine(pParser->pIn, &pParser->cchLine, &pParser->enmEol);
        AssertReturn(pchLine, false /*dummy*/);
        cchLine = pParser->cchLine;
        iSubLine++;
        if (iSubLine + 1 < cLines && pchLine[cchLine - 2] == '\\')
        {
            *pszDst++ = ' ';
            *pszDst++ = '\\';
            *pszDst   = '\0';
            ScmStreamPutLine(pParser->pOut, pParser->szBuf, pszDst - pParser->szBuf, pParser->enmEol);
            return scmKmkGiveUp(pParser, "Escaped slashes at end of line not allowed. Insert space before line continuation slash!");
        }
        cPendingEols = 1;

        /* Skip indent/whitespace. */
        offLine = 0;
        while (offLine < cchLine && RT_C_IS_SPACE(pchLine[offLine]))
            offLine++;
    }

    /*
     * Okay, we've gotten to the value / comment part.
     */
    char     achExpandNestingClose[256];
    unsigned cExpandNesting = 0;
    for (;;)
    {
        /*
         * The end? Flush what we've got.
         */
        if (offLine == cchLine)
        {
            Assert(iSubLine + 1 == cLines);
            *pszDst = '\0';
            ScmStreamPutLine(pParser->pOut, pParser->szBuf, pszDst - pParser->szBuf, pParser->enmEol);
            if (cPendingEols > 0)
                ScmStreamPutEol(pParser->pOut, pParser->enmEol);
            return false; /* dummy */
        }

        /*
         * Output any non-comment stuff, stripping off newlines.
         */
        const char *pchHash = (const char *)memchr(&pchLine[offLine], '#', cchLine - offLine);
        if (pchHash != &pchLine[offLine])
        {
            /* Add space or flush pending EOLs. */
            if (!cPendingEols)
                *pszDst++ = ' ';
            else
            {
                unsigned iEol = 0;
                cPendingEols = RT_MIN(2, cPendingEols); /* reduce to two, i.e. only one empty separator line */
                do
                {
                    if (iEol++ == 0)  /* skip this for the 2nd empty line. */
                        *pszDst++ = ' ';
                    *pszDst++ = '\\';
                    *pszDst = '\0';
                    ScmStreamPutLine(pParser->pOut, pParser->szBuf, pszDst - pParser->szBuf, pParser->enmEol);

                    pszDst = pParser->szBuf;
                    memset(pszDst, ' ', cchIndent);
                    pszDst += cchIndent;

                    size_t cTabIndent = cExpandNesting + 1;
                    while (cTabIndent-- > 0)
                        *pszDst++ = '\t';

                    cPendingEols--;
                } while (cPendingEols > 0);
            }

            /* Strip backwards. */
            size_t const offValueEnd2 = pchHash ? (size_t)(pchHash - pchLine) : cchLine - (iSubLine + 1 < cLines);
            size_t       offValueEnd  = offValueEnd2;
            while (offValueEnd > offLine && RT_C_IS_BLANK(pchLine[offValueEnd - 1]))
                offValueEnd--;
            Assert(offValueEnd > offLine);

            /* Append the value part we found. */
            pszDst = (char *)mempcpy(pszDst, &pchLine[offLine], offValueEnd - offLine);
            cExpandNesting = scmKmkScanStringForExpansions(cExpandNesting, achExpandNestingClose,
                                                           &pchLine[offLine], offValueEnd - offLine);
            offLine = offValueEnd2;
        }

        /*
         * If we found a comment hash, emit it and whatever follows just as-is w/o
         * any particular reformatting.  Comments within a variable definition are
         * usually to disable portitions of a property like _DEFS or _SOURCES.
         */
        if (pchHash != NULL)
        {
            if (cPendingEols == 0)
                scmKmkTailComment(pParser, pchLine, cchLine, offLine, &pszDst);
            size_t const cchDst = (size_t)(pszDst - pParser->szBuf);
            *pszDst = '\0';
            ScmStreamPutLine(pParser->pOut, pParser->szBuf, cchDst, pParser->enmEol);

            if (cPendingEols > 1)
                ScmStreamPutEol(pParser->pOut, pParser->enmEol);

            if (cPendingEols > 0)
                ScmStreamPutLine(pParser->pOut, pchLine, cchLine, pParser->enmEol);
            scmKmkPassThruLineContinuationLines(pParser);
            return false; /* dummy */
        }

        /*
         * Fetch another line, if we've got one.
         */
        if (iSubLine + 1 >= cLines)
            Assert(offLine == cchLine);
        else
        {
            Assert(offLine + 1 == cchLine);
            while (iSubLine + 1 < cLines && offLine + 1 == cchLine && pchLine[offLine] == '\\')
            {
                pParser->pchLine = pchLine = ScmStreamGetLine(pParser->pIn, &pParser->cchLine, &pParser->enmEol);
                AssertReturn(pchLine, false /*dummy*/);
                cchLine = pParser->cchLine;
                iSubLine++;
                if (iSubLine + 1 < cLines && pchLine[cchLine - 2] == '\\')
                {
                    *pszDst++ = ' ';
                    *pszDst++ = '\\';
                    *pszDst   = '\0';
                    ScmStreamPutLine(pParser->pOut, pParser->szBuf, pszDst - pParser->szBuf, pParser->enmEol);
                    if (cPendingEols > 1)
                        ScmError(pParser->pState, VERR_NOT_SUPPORTED, "oops #1: Manually fix the next issue after reverting edits!");
                    return scmKmkGiveUp(pParser, "Escaped slashes at end of line not allowed. Insert space before line continuation slash!");
                }
                cPendingEols++;

                /* Deal with indent/whitespace. */
                offLine = 0;
                while (offLine < cchLine && RT_C_IS_SPACE(pchLine[offLine]))
                    offLine++;
            }
        }
    }
}


/**
 * A rule.
 *
 * This is a bit involved. Sigh.
 *
 * @returns dummy (false) to facility return + call.
 */
static bool scmKmkHandleRule(KMKPARSER *pParser, size_t offFirstWord, bool fDoubleColon, size_t offColon)
{
    SCMSTREAM          *pOut         = pParser->pOut;
    unsigned const      cchIndent    = pParser->iActualDepth;
    const char         *pchLine      = pParser->pchLine;
    size_t              cchLine      = pParser->cchLine;
    Assert(offFirstWord < cchLine);
    uint32_t const      cLines       = pParser->cLines;
    uint32_t            iSubLine     = 0;

    /* Following this, we'll be in recipe-mode. */
    ScmVerbose(pParser->pState, 4, "%u: debug: start rule\n", ScmStreamTellLine(pParser->pIn));
    scmKmkSetInRecipe(pParser, true);

    /* This is too much hazzle to deal with. */
    if (cLines > 0 && scmKmkGiveUpIfTrailingEscapedSlashed(pParser, pchLine, cchLine))
        return false;

    /* Too special case. */
    if (offColon <= offFirstWord)
        return scmKmkGiveUp(pParser, "Missing target file before colon!");

    /*
     * Indent it.
     */
    ScmStreamWrite(pOut, g_szSpaces, cchIndent);
    size_t offLine = offFirstWord;

    /*
     * Process word by word past the colon, taking new lines into account.
     */
    KMKWORDSTATE WordState    = { 0, 0 };
    KMKWORDCTX   enmCtx       = kKmkWordCtx_TargetFileOrAssignment;
    unsigned     cPendingEols = 0;
    for (;;)
    {
        /*
         * Output the next word.
         */
        size_t cchWord = scmKmkWordLength(pchLine, cchLine, offLine, enmCtx, &WordState);
        Assert(offLine + cchWord <= offColon);
        ScmStreamWrite(pOut, &pchLine[offLine], cchWord);
        offLine += cchWord;

        /* Skip whitespace (if any). */
        while (offLine < cchLine && RT_C_IS_SPACE(pchLine[offLine]))
            offLine++;

        /* Have we reached the colon already? */
        if (offLine >= offColon)
        {
            Assert(pchLine[offLine] == ':');
            Assert(!fDoubleColon || pchLine[offLine + 1] == ':');
            offLine += fDoubleColon ? 2 : 1;

            ScmStreamPutCh(pOut, ':');
            if (fDoubleColon)
                ScmStreamPutCh(pOut, ':');
            break;
        }

        /* Deal with new line and emit indentation. */
        if (offLine + 1 == cchLine && pchLine[offLine] == '\\')
        {
            /* Get the next input line. */
            for (;;)
            {
                const char * const pchPrevLine = pchLine;
                Assert(iSubLine + 1 < cLines);
                pParser->pchLine = pchLine = ScmStreamGetLine(pParser->pIn, &pParser->cchLine, &pParser->enmEol);
                AssertReturn(pchLine, false /*dummy*/);
                cchLine = pParser->cchLine;
                iSubLine++;
                if (iSubLine + 1 < cLines && scmKmkGiveUpIfTrailingEscapedSlashed(pParser, pchLine, cchLine))
                    return false;

                /* Adjust offColon: */
                offColon -= (uintptr_t)pchLine - (uintptr_t)pchPrevLine;
                Assert(offColon < ~(size_t)0 / 2);

                /* Skip leading spaces. */
                offLine = 0;
                while (offLine < cchLine && RT_C_IS_SPACE(pchLine[offLine]))
                    offLine++;

                /* Just drop empty lines. */
                if (offLine + 1 == cchLine && pchLine[offLine] == '\\')
                    continue;

                /* Complete the current line and emit indent, unless we reached the colon: */
                if (offLine >= offColon)
                {
                    Assert(pchLine[offLine] == ':');
                    Assert(!fDoubleColon || pchLine[offLine + 1] == ':');
                    offLine += fDoubleColon ? 2 : 1;

                    ScmStreamPutCh(pOut, ':');
                    if (fDoubleColon)
                        ScmStreamPutCh(pOut, ':');

                    cPendingEols = 1;
                }
                else
                {
                    ScmStreamWrite(pOut, RT_STR_TUPLE(" \\"));
                    ScmStreamPutEol(pOut, pParser->enmEol);
                    ScmStreamWrite(pOut, g_szSpaces, cchIndent);
                    if (WordState.uDepth > 0)
                        ScmStreamWrite(pOut, g_szTabs, RT_MIN(WordState.uDepth, sizeof(g_szTabs) - 1));
                }
                break;
            }
            if (offLine >= offColon)
                break;
        }
        else
            ScmStreamPutCh(pOut, ' ');
        enmCtx = kKmkWordCtx_TargetFile;
    }

    /*
     * We're immediately past the colon now, so eat whitespace and newlines and
     * whatever till we get to a solid word or the end of the line.
     */
    /* Skip spaces - there should be exactly one. */
    while (offLine < cchLine && RT_C_IS_SPACE(pchLine[offLine]))
        offLine++;

    /* Deal with new lines: */
    while (offLine + 1 == cchLine && pchLine[offLine] == '\\')
    {
        cPendingEols = 1;

        Assert(iSubLine + 1 < cLines);
        pParser->pchLine = pchLine = ScmStreamGetLine(pParser->pIn, &pParser->cchLine, &pParser->enmEol);
        AssertReturn(pchLine, false /*dummy*/);
        cchLine = pParser->cchLine;
        iSubLine++;
        if (iSubLine + 1 < cLines && scmKmkGiveUpIfTrailingEscapedSlashed(pParser, pchLine, cchLine))
            return false;

         /* Skip leading spaces. */
         offLine = 0;
         while (offLine < cchLine && RT_C_IS_SPACE(pchLine[offLine]))
             offLine++;

         /* Just drop empty lines. */
         if (offLine + 1 == cchLine && pchLine[offLine] == '\\')
             continue;
    }

    /*
     * Special case: No dependencies.
     */
    if (offLine == cchLine && iSubLine + 1 >= cLines)
    {
        ScmStreamPutEol(pOut, pParser->enmEol);
        return false /*dummy*/;
    }

    /*
     * Work the dependencies word for word.  Indent in spaces + two tabs.
     * (Pattern rules will also end up here, but we'll just ignore that for now.)
     */
    enmCtx = kKmkWordCtx_DepFileOrAssignment;
    for (;;)
    {
        /* Indent the next word. */
        if (cPendingEols == 0)
            ScmStreamPutCh(pOut, ' ');
        else
        {
            ScmStreamWrite(pOut, RT_STR_TUPLE(" \\"));
            ScmStreamPutEol(pOut, pParser->enmEol);
            ScmStreamWrite(pOut, g_szSpaces, cchIndent);
            ScmStreamWrite(pOut, RT_STR_TUPLE("\t\t"));
            if (cPendingEols > 1)
            {
                ScmStreamWrite(pOut, RT_STR_TUPLE("\\"));
                ScmStreamPutEol(pOut, pParser->enmEol);
                ScmStreamWrite(pOut, g_szSpaces, cchIndent);
                ScmStreamWrite(pOut, RT_STR_TUPLE("\t\t"));
            }
            cPendingEols = 0;
        }
        if (WordState.uDepth > 0)
            ScmStreamWrite(pOut, g_szTabs, RT_MIN(WordState.uDepth, sizeof(g_szTabs) - 1));

        /* Get the next word and output it. */
        size_t cchWord = scmKmkWordLength(pchLine, cchLine, offLine, enmCtx, &WordState);
        Assert(offLine + cchWord <= cchLine);

        ScmStreamWrite(pOut, &pchLine[offLine], cchWord);
        offLine += cchWord;

        /* Skip whitespace (if any). */
        while (offLine < cchLine && RT_C_IS_SPACE(pchLine[offLine]))
            offLine++;

        /* Deal with new line and emit indentation. */
        if (iSubLine + 1 < cLines && offLine + 1 == cchLine && pchLine[offLine] == '\\')
        {
            /* Get the next input line. */
            for (;;)
            {
                Assert(iSubLine + 1 < cLines);
                pParser->pchLine = pchLine = ScmStreamGetLine(pParser->pIn, &pParser->cchLine, &pParser->enmEol);
                AssertReturn(pchLine, false /*dummy*/);
                cchLine = pParser->cchLine;
                iSubLine++;
                if (iSubLine + 1 < cLines && scmKmkGiveUpIfTrailingEscapedSlashed(pParser, pchLine, cchLine))
                    return false;

                /* Skip leading spaces. */
                offLine = 0;
                while (offLine < cchLine && RT_C_IS_SPACE(pchLine[offLine]))
                    offLine++;

                /* Just drop empty lines, we'll re-add one of them afterward if we find more dependencies. */
                cPendingEols++;
                if (offLine + 1 == cchLine && pchLine[offLine] == '\\')
                    continue;
                break;
            }
        }

        if (offLine >= cchLine)
        {
            /* End of input. */
/** @todo deal with comments */
            Assert(iSubLine + 1 == cLines);
            ScmStreamPutEol(pOut, pParser->enmEol);
            return false; /* dummmy */
        }
        enmCtx = kKmkWordCtx_DepFile;
    }
}


/**
 * Checks if the (extended) line is a variable assignment.
 *
 * We scan past line continuation stuff here as the assignment operator could be
 * on the next line, even if that's very unlikely it is recommened by the coding
 * guide lines if the line needs to be split.  Fortunately, though, the caller
 * already removes empty empty leading lines, so we only have to consider the
 * line continuation issue if no '=' was found on the first line.
 *
 * @returns Modified or not.
 * @param   pParser         The parser.
 * @param   cLines          Number of lines to consider.
 * @param   cchTotalLine    Total length of all the lines to consider.
 * @param   offWord         Where the first word of the line starts.
 * @param   pfIsAssignment  Where to return whether this is an assignment or
 *                          not.
 */
static bool scmKmkHandleAssignmentOrRule(KMKPARSER *pParser, size_t offWord)
{
    const char  *pchLine      = pParser->pchLine;
    size_t const cchTotalLine = pParser->cchTotalLine;

    /*
     * Scan words till we find ':' or '='.
     */
    uint32_t iWord      = 0;
    size_t   offCurWord = offWord;
    size_t   offEndPrev = 0;
    size_t   offLine    = offWord;
    while (offLine < cchTotalLine)
    {
        char ch = pchLine[offLine++];
        if (ch == '$')
        {
            /*
             * Skip variable expansion.
             */
            char const chOpen = pchLine[offLine++];
            if (chOpen == '(' || chOpen == '{')
            {
                char const chClose = chOpen == '(' ? ')' : '}';
                unsigned   cDepth  = 1;
                while (offLine < cchTotalLine)
                {
                    ch = pchLine[offLine++];
                    if (ch == chOpen)
                        cDepth++;
                    else if (ch == chClose)
                        if (!--cDepth)
                            break;
                }
            }
            /* else: $x or $$, so just skip the next character. */
        }
        else if (RT_C_IS_SPACE(ch))
        {
            /*
             * End of word. Skip whitespace till the next word starts.
             */
            offEndPrev = offLine - 1;
            Assert(offLine != offWord);
            while (offLine < cchTotalLine)
            {
                ch = pchLine[offLine];
                if (RT_C_IS_SPACE(ch))
                    offLine++;
                else if (ch == '\\' && (pchLine[offLine] == '\r' || pchLine[offLine] == '\n'))
                    offLine += 2;
                else
                    break;
            }
            offCurWord = offLine;
            iWord++;

            /*
             * To simplify the assignment operator checks, we just check the
             * start of the 2nd word when we're here.
             */
            if (iWord == 1 && offLine < cchTotalLine)
            {
                ch = pchLine[offLine];
                if (ch == '=')
                    return scmKmkHandleAssignment2(pParser, offWord, offEndPrev, kKmkAssignType_Recursive, offLine, 0);
                if (offLine + 1 < cchTotalLine && pchLine[offLine + 1] == '=')
                {
                    if (ch == ':')
                        return scmKmkHandleAssignment2(pParser, offWord, offEndPrev, kKmkAssignType_Simple,      offLine, 0);
                    if (ch == '+')
                        return scmKmkHandleAssignment2(pParser, offWord, offEndPrev, kKmkAssignType_Appending,   offLine, 0);
                    if (ch == '<')
                        return scmKmkHandleAssignment2(pParser, offWord, offEndPrev, kKmkAssignType_Prepending,  offLine, 0);
                    if (ch == '?')
                        return scmKmkHandleAssignment2(pParser, offWord, offEndPrev, kKmkAssignType_Conditional, offLine, 0);
                }
                else if (   ch                   == ':'
                         && pchLine[offLine + 1] == ':'
                         && pchLine[offLine + 2] == '=')
                    return scmKmkHandleAssignment2(pParser, offWord, offEndPrev, kKmkAssignType_Immediate, offLine, 0);

                /* Check for rule while we're here. */
                if (ch == ':')
                    return scmKmkHandleRule(pParser, offWord, pchLine[offLine + 1] == ':', offLine);
            }
        }
        /*
         * If '=' is found in the first word it's an assignment.
         */
        else if (ch == '=')
        {
            if (iWord == 0)
            {
                KMKASSIGNTYPE enmType = kKmkAssignType_Recursive;
                ch = pchLine[offLine - 2];
                if (ch == '+')
                    enmType = kKmkAssignType_Appending;
                else if (ch == '?')
                    enmType = kKmkAssignType_Conditional;
                else if (ch == '<')
                    enmType = kKmkAssignType_Prepending;
                else
                {
                    Assert(ch != ':');
                    return scmKmkHandleAssignment2(pParser, offWord, offLine - 1, enmType, offLine - 1, 0);
                }
                return scmKmkHandleAssignment2(pParser, offWord, offLine - 2, enmType, offLine - 2, 0);
            }
        }
        /*
         * When ':' is found it can mean a drive letter, a rule or in the
         * first word a simple or immediate assignment.
         */
        else if (ch == ':')
        {
            /* Check for drive letters (we ignore the archive form): */
            if (offLine - offWord == 2 && RT_C_IS_ALPHA(pchLine[offLine - 2]))
            {  /* ignore */ }
            else
            {
                /* Simple or immediate assignment? */
                ch = pchLine[offLine];
                if (iWord == 0)
                {
                    if (ch == '=')
                        return scmKmkHandleAssignment2(pParser, offWord, offLine - 1, kKmkAssignType_Simple, offLine - 1, 0);
                    if (ch == ':' && pchLine[offLine + 1] == '=')
                        return scmKmkHandleAssignment2(pParser, offWord, offLine - 1, kKmkAssignType_Immediate, offLine - 1, 0);
                }

                /* Okay, it's a rule then. */
                return scmKmkHandleRule(pParser, offWord, ch == ':', offLine - 1);
            }
        }
    }

    /*
     * Check if this is a $(error ) or similar function call line.
     *
     * If we're inside a 'define' we treat $$ as $ as it's probably a case of
     * double expansion (e.g. def_vmm_lib_dtrace_preprocess in VMM/Makefile.kmk).
     */
    if (pchLine[offWord] == '$')
    {
        size_t const cDollars = pchLine[offWord + 1] != '$' || !scmKmkIsInsideDefine(pParser) ? 1 : 2;
        if (   pchLine[offWord + cDollars] == '('
            || pchLine[offWord + cDollars] == '{')
        {
            size_t const cchLine = pParser->cchLine;
            size_t       offEnd  = offWord + cDollars + 1;
            char         ch      = '\0';
            while (offEnd < cchLine && (RT_C_IS_LOWER(ch = pchLine[offEnd]) || RT_C_IS_DIGIT(ch) || ch == '-'))
                offEnd++;
            if (offEnd >= cchLine || RT_C_IS_SPACE(ch) || (offEnd == cchLine - 1 && ch == '\\'))
            {
                static const RTSTRTUPLE s_aAllowedFunctions[] =
                {
                    { RT_STR_TUPLE("info") },
                    { RT_STR_TUPLE("error") },
                    { RT_STR_TUPLE("warning") },
                    { RT_STR_TUPLE("set-umask") },
                    { RT_STR_TUPLE("foreach") },
                    { RT_STR_TUPLE("call") },
                    { RT_STR_TUPLE("eval") },
                    { RT_STR_TUPLE("evalctx") },
                    { RT_STR_TUPLE("evalval") },
                    { RT_STR_TUPLE("evalvalctx") },
                    { RT_STR_TUPLE("evalcall") },
                    { RT_STR_TUPLE("evalcall2") },
                    { RT_STR_TUPLE("eval-opt-var") },
                    { RT_STR_TUPLE("kb-src-one") },
                };
                size_t cchFunc = offEnd - offWord - cDollars - 1;
                for (size_t i = 0; i < RT_ELEMENTS(s_aAllowedFunctions); i++)
                    if (   cchFunc == s_aAllowedFunctions[i].cch
                        && memcmp(&pchLine[offWord + cDollars + 1], s_aAllowedFunctions[i].psz, cchFunc) == 0)
                        return scmKmkHandleSimple(pParser, offWord);
            }
        }
    }

    /*
     * If we didn't find anything, output it as-as.
     * We use scmKmkHandleSimple in a special way to do this.
     */
    if (!RTStrStartsWith(pchLine, "$(TOOL_")) /* ValKit/Config.kmk */
        ScmVerbose(pParser->pState, 1, "%u: debug: Unable to make sense of this line!\n", ScmStreamTellLine(pParser->pIn));
    return scmKmkHandleSimple(pParser, 0 /*offToken*/, false /*fIndentIt*/);
}


static bool scmKmkHandleAssignKeyword(KMKPARSER *pParser, size_t offToken, KMKTOKEN enmToken, size_t cchWord,
                                      bool fMustBeAssignment)
{
    /* Assignments takes us out of recipe mode. */
    scmKmkSetInRecipe(pParser, false);

    RT_NOREF(pParser, offToken, enmToken, cchWord, fMustBeAssignment);
    return scmKmkHandleSimple(pParser, offToken);
}


/**
 * Handles a line with a recipe command.
 */
static void scmKmkHandleRecipeCommand(KMKPARSER *pParser, const char *pchLine, size_t cchLine)
{
    /*
     * Make sure there is only a single tab and no spaces following it.
     * This helps tell prerequisites from the commands in the recipe.
     *
     * Iff the line starts with a '#' it is probably a Makefile comment line,
     * but it will be executed by the shell (kmk_ash) and waste time. So, we
     * expand the initial tab into spaces when seeing that.
     */
    Assert(*pchLine == '\t');
    size_t offLine = 1;
    while (offLine < cchLine && RT_C_IS_BLANK(pchLine[offLine]))
        offLine++;

    if (offLine < cchLine && pchLine[offLine] == '#')
        ScmStreamWrite(pParser->pOut, g_szSpaces, pParser->pSettings->cchTab);
    else
        ScmStreamPutCh(pParser->pOut, '\t');

    ScmStreamWrite(pParser->pOut, &pchLine[offLine], cchLine - offLine);
    ScmStreamPutEol(pParser->pOut, pParser->enmEol);

    /*
     * Any continuation lines are currently just passed thru as-is.
     * We could insist on these also starting with tabs, but later.
     */
    scmKmkPassThruLineContinuationLines(pParser);
}


/**
 * Rewrite a kBuild makefile.
 *
 * @returns kScmMaybeModified or kScmUnmodified.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 *
 * @todo
 *
 * Ideas for Makefile.kmk and Config.kmk:
 *      - sort if1of/ifn1of sets.
 *      - line continuation slashes should only be preceded by one space.
 */
SCMREWRITERRES rewrite_Makefile_kmk(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    if (!pSettings->fStandarizeKmk)
        return kScmUnmodified;

    /*
     * Parser state.
     */
    KMKPARSER Parser;
    Parser.iDepth       = 0;
    Parser.iActualDepth = 0;
    Parser.fInRecipe    = false;
    Parser.pState       = pState;
    Parser.pIn          = pIn;
    Parser.pOut         = pOut;
    Parser.pSettings    = pSettings;

    /*
     * Iterate the file.
     */
    const char *pchLine;
    while ((Parser.pchLine = pchLine = ScmStreamGetLine(pIn, &Parser.cchLine, &Parser.enmEol)) != NULL)
    {
        size_t cchLine = Parser.cchLine;

        /*
         * If we're in the command part of a recipe, anything starting with a
         * tab is considered another command for the recipe.
         */
        if (Parser.fInRecipe && *pchLine == '\t')
        {
            scmKmkHandleRecipeCommand(&Parser, pchLine, cchLine);
            continue;
        }

        /*
         * Skip leading whitespace and check for directives (simplified).
         *
         * This is simplified in the sense that GNU make first checks for variable
         * assignments, so that directive can be used as variable names.  We don't
         * want that, so we do the variable assignment check later.
         */
        size_t offLine = 0;
        while (offLine < cchLine && RT_C_IS_BLANK(pchLine[offLine]))
            offLine++;

        /* Find end of word (if any) - only looking for keywords here: */
        size_t cchWord = 0;
        while (   offLine + cchWord < cchLine
               && (   RT_C_IS_ALNUM(pchLine[offLine + cchWord])
                   || pchLine[offLine + cchWord] == '-'))
            cchWord++;
        if (cchWord > 0)
        {
            /* If the line is just a line continuation slash, simply remove it
               (this also makes the parsing a lot easier). */
            if (cchWord == 1 && offLine == cchLine - 1 && pchLine[cchLine] == '\\')
                continue;

            /* Unlike the GNU make parser, we won't recognize 'if' or any other
               directives as variable names, so we can  */
            KMKTOKEN enmToken = scmKmkIdentifyToken(&pchLine[offLine], cchWord);
            switch (enmToken)
            {
                case kKmkToken_ifeq:
                case kKmkToken_ifneq:
                case kKmkToken_if1of:
                case kKmkToken_ifn1of:
                    scmKmkHandleIfParentheses(&Parser, offLine, enmToken, cchWord, false /*fElse*/);
                    continue;

                case kKmkToken_ifdef:
                case kKmkToken_ifndef:
                case kKmkToken_if:
                    scmKmkHandleIfSpace(&Parser, offLine, enmToken, cchWord, false /*fElse*/);
                    continue;

                case kKmkToken_else:
                    scmKmkHandleElse(&Parser, offLine);
                    continue;

                case kKmkToken_endif:
                    scmKmkHandleEndif(&Parser, offLine);
                    continue;

                /* Includes: */
                case kKmkToken_include:
                case kKmkToken_sinclude:
                case kKmkToken_dash_include:
                case kKmkToken_includedep:
                case kKmkToken_includedep_queue:
                case kKmkToken_includedep_flush:
                    scmKmkHandleSimple(&Parser, offLine);
                    continue;

                /* Others: */
                case kKmkToken_define:
                    scmKmkHandleDefine(&Parser, offLine);
                    continue;
                case kKmkToken_endef:
                    scmKmkHandleEndef(&Parser, offLine);
                    continue;

                case kKmkToken_override:
                case kKmkToken_local:
                    scmKmkHandleAssignKeyword(&Parser, offLine, enmToken, cchWord, true /*fMustBeAssignment*/);
                    continue;

                case kKmkToken_export:
                    scmKmkHandleAssignKeyword(&Parser, offLine, enmToken, cchWord, false /*fMustBeAssignment*/);
                    continue;

                case kKmkToken_unexport:
                case kKmkToken_undefine:
                    scmKmkHandleSimple(&Parser, offLine);
                    continue;

                case kKmkToken_Comment:
                    AssertFailed(); /* not possible */
                    break;

                /*
                 * Check if it's perhaps an variable assignment or start of a rule.
                 * We'll do this in a very simple fashion.
                 */
                case kKmkToken_Word:
                {
                    Parser.cLines       = 1;
                    Parser.cchTotalLine = cchLine;
                    if (scmKmkIsLineWithContinuation(pchLine, cchLine))
                        Parser.cchTotalLine = scmKmkLineContinuationPeek(&Parser, &Parser.cLines, NULL);
                    scmKmkHandleAssignmentOrRule(&Parser, offLine);
                    continue;
                }
            }
        }
        /*
         * Not keyword, check for assignment, rule or comment:
         */
        else if (offLine < cchLine)
        {
            if (pchLine[offLine] != '#')
            {
                Parser.cLines       = 1;
                Parser.cchTotalLine = cchLine;
                if (scmKmkIsLineWithContinuation(pchLine, cchLine))
                    Parser.cchTotalLine = scmKmkLineContinuationPeek(&Parser, &Parser.cLines, NULL);
                scmKmkHandleAssignmentOrRule(&Parser, offLine);
                continue;
            }

            /*
             * Indent comment lines, unless the comment is too far too the right.
             */
            size_t const offEffLine = ScmCalcSpacesForSrcSpan(pchLine, 0, offLine, pSettings);
            if (offEffLine <= Parser.iActualDepth + 7)
            {
                ScmStreamWrite(pOut, g_szSpaces, Parser.iActualDepth);
                ScmStreamWrite(pOut, &pchLine[offLine], cchLine - offLine);
                ScmStreamPutEol(pOut, Parser.enmEol);

                /* If line continuation is used, it's typically to disable
                   a property variable, so we just pass it thru as-is */
                while (scmKmkIsLineWithContinuation(pchLine, cchLine))
                {
                    Parser.pchLine = pchLine = ScmStreamGetLine(pIn, &Parser.cchLine, &Parser.enmEol);
                    if (!pchLine)
                        break;
                    cchLine = Parser.cchLine;
                    ScmStreamPutLine(pOut, pchLine, cchLine, Parser.enmEol);
                }
                continue;
            }
        }

        /*
         * Pass it thru as-is with line continuation.
         */
        while (scmKmkIsLineWithContinuation(pchLine, cchLine))
        {
            ScmStreamPutLine(pOut, pchLine, cchLine, Parser.enmEol);
            Parser.pchLine = pchLine = ScmStreamGetLine(pIn, &Parser.cchLine, &Parser.enmEol);
            if (!pchLine)
                break;
            cchLine = Parser.cchLine;
        }
        if (pchLine)
            ScmStreamPutLine(pOut, pchLine, cchLine, Parser.enmEol);
    }

    return kScmMaybeModified; /* Make the caller check */
}


/**
 * Makefile.kup are empty files, enforce this.
 *
 * @returns true if modifications were made, false if not.
 * @param   pIn                 The input stream.
 * @param   pOut                The output stream.
 * @param   pSettings           The settings.
 */
SCMREWRITERRES rewrite_Makefile_kup(PSCMRWSTATE pState, PSCMSTREAM pIn, PSCMSTREAM pOut, PCSCMSETTINGSBASE pSettings)
{
    RT_NOREF2(pOut, pSettings);

    /* These files should be zero bytes. */
    if (pIn->cb == 0)
        return kScmUnmodified;
    ScmVerbose(pState, 2, " * Truncated file to zero bytes\n");
    return kScmModified;
}

