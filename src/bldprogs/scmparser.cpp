/* $Id: scmparser.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase / Tool - Source Code Massager, Code Parsers.
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
#include <iprt/errcore.h>
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
typedef size_t (*PFNISCOMMENT)(const char *pchLine, size_t cchLine, bool fSecond);


/**
 * Callback for checking if C++ line comment.
 */
static size_t isCppLineComment(const char *pchLine, size_t cchLine, bool fSecond)
{
    if (   cchLine >= 2
        && pchLine[0] == '/'
        && pchLine[1] == '/')
    {
        if (!fSecond)
            return 2;
        if (cchLine >= 3 && pchLine[2] == '/')
            return 3;
    }
    return 0;
}


/**
 * Callback for checking if hash comment.
 */
static size_t isHashComment(const char *pchLine, size_t cchLine, bool fSecond)
{
    if (cchLine >= 1 && *pchLine == '#')
    {
        if (!fSecond)
            return 1;
        if (cchLine >= 2 && pchLine[1] == '#')
            return 2;
    }
    return 0;
}


/**
 * Callback for checking if semicolon comment.
 */
static size_t isSemicolonComment(const char *pchLine, size_t cchLine, bool fSecond)
{
    if (cchLine >= 1 && *pchLine == ';')
    {
        if (!fSecond)
            return 1;
        if (cchLine >= 2 && pchLine[1] == ';')
            return 2;
    }
    return 0;
}


/** Macro for checking for a XML comment start. */
#define IS_XML_COMMENT_START(a_pch, a_off, a_cch) \
        (   (a_off) + 4 <= (a_cch) \
         && (a_pch)[(a_off)    ] == '<' \
         && (a_pch)[(a_off) + 1] == '!' \
         && (a_pch)[(a_off) + 2] == '-' \
         && (a_pch)[(a_off) + 3] == '-' \
         && ((a_off) + 4 == (a_cch) || RT_C_IS_SPACE((a_pch)[(a_off) + 4])) )

/** Macro for checking for a XML comment end. */
#define IS_XML_COMMENT_END(a_pch, a_off, a_cch) \
        (   (a_off) + 3 <= (a_cch) \
         && (a_pch)[(a_off)    ] == '-' \
         && (a_pch)[(a_off) + 1] == '-' \
         && (a_pch)[(a_off) + 2] == '>')


/** Macro for checking for a batch file comment prefix. */
#define IS_REM(a_pch, a_off, a_cch) \
        (   (a_off) + 3 <= (a_cch) \
         && ((a_pch)[(a_off)    ] == 'R' || (a_pch)[(a_off)    ] == 'r') \
         && ((a_pch)[(a_off) + 1] == 'E' || (a_pch)[(a_off) + 1] == 'e') \
         && ((a_pch)[(a_off) + 2] == 'M' || (a_pch)[(a_off) + 2] == 'm') \
         && ((a_off) + 3 == (a_cch) || RT_C_IS_SPACE((a_pch)[(a_off) + 3])) )


/**
 * Callback for checking if batch comment.
 */
static size_t isBatchComment(const char *pchLine, size_t cchLine, bool fSecond)
{
    if (!fSecond)
    {
        if (IS_REM(pchLine, 0, cchLine))
            return 3;
    }
    else
    {
        /* Check for the 2nd in "rem rem" lines. */
        if (   cchLine >= 4
            && RT_C_IS_SPACE(*pchLine)
            && IS_REM(pchLine, 1, cchLine))
            return 4;
    }
    return 0;
}

/**
 * Callback for checking if SQL comment.
 */
static size_t isSqlComment(const char *pchLine, size_t cchLine, bool fSecond)
{
    if (   cchLine >= 2
        && pchLine[0] == '-'
        && pchLine[1] == '-')
    {
        if (!fSecond)
            return 2;
        if (   cchLine >= 3
            && pchLine[2] == '-')
            return 3;
    }
    return 0;
}

/**
 * Callback for checking if tick comment.
 */
static size_t isTickComment(const char *pchLine, size_t cchLine, bool fSecond)
{
    if (cchLine >= 1 && *pchLine == '\'')
    {
        if (!fSecond)
            return 1;
        if (cchLine >= 2 && pchLine[1] == '\'')
            return 2;
    }
    return 0;
}


/**
 * Common worker for enumeratePythonComments and enumerateSimpleLineComments.
 *
 * @returns IPRT status code.
 * @param   pIn             The input stream.
 * @param   pfnIsComment    Comment tester function.
 * @param   pfnCallback     The callback.
 * @param   pvUser          The user argument for the callback.
 * @param   ppchLine        Pointer to the line variable.
 * @param   pcchLine        Pointer to the line length variable.
 * @param   penmEol         Pointer to the line ending type variable.
 * @param   piLine          Pointer to the line number variable.
 * @param   poff            Pointer to the line offset variable.  On input this
 *                          is positioned at the start of the comment.
 */
static int handleLineComment(PSCMSTREAM pIn, PFNISCOMMENT pfnIsComment,
                             PFNSCMCOMMENTENUMERATOR pfnCallback, void *pvUser,
                             const char **ppchLine, size_t *pcchLine, PSCMEOL penmEol,
                             uint32_t *piLine, size_t *poff)
{
    /* Unpack input/output variables. */
    uint32_t        iLine   = *piLine;
    const char     *pchLine = *ppchLine;
    size_t          cchLine = *pcchLine;
    size_t          off     = *poff;
    SCMEOL          enmEol  = *penmEol;

    /*
     * Take down the basic info about the comment.
     */
    SCMCOMMENTINFO  Info;
    Info.iLineStart         = iLine;
    Info.iLineEnd           = iLine;
    Info.offStart           = (uint32_t)off;
    Info.offEnd             = (uint32_t)cchLine;

    size_t cchSkip = pfnIsComment(&pchLine[off], cchLine - off, false);
    Assert(cchSkip > 0);
    off += cchSkip;

    /* Determine comment type. */
    Info.enmType = kScmCommentType_Line;
    char ch;
    cchSkip = 1;
    if (   off < cchLine
        && (   (ch = pchLine[off]) == '!'
            || (cchSkip = pfnIsComment(&pchLine[off], cchLine - off, true)) > 0) )
    {
        unsigned ch2;
        if (   off + cchSkip == cchLine
            || RT_C_IS_SPACE(ch2 = pchLine[off + cchSkip]) )
        {
            Info.enmType = ch != '!' ? kScmCommentType_Line_JavaDoc : kScmCommentType_Line_Qt;
            off += cchSkip;
        }
        else if (   ch2 == '<'
                 && (   off + cchSkip + 1 == cchLine
                     || RT_C_IS_SPACE(pchLine[off + cchSkip + 1]) ))
        {
            Info.enmType = ch == '!' ? kScmCommentType_Line_JavaDoc_After : kScmCommentType_Line_Qt_After;
            off += cchSkip + 1;
        }
    }

    /*
     * Copy body of the first line.  Like for C, we ignore a single space in the first comment line.
     */
    if (off < cchLine && RT_C_IS_SPACE(pchLine[off]))
        off++;
    size_t cchBody = cchLine;
    while (cchBody > off && RT_C_IS_SPACE(pchLine[cchBody - 1]))
           cchBody--;
    cchBody -= off;
    size_t   cbBodyAlloc = RT_MAX(_1K, RT_ALIGN_Z(cchBody + 64, 128));
    char    *pszBody     = (char *)RTMemAlloc(cbBodyAlloc);
    if (!pszBody)
        return VERR_NO_MEMORY;
    memcpy(pszBody, &pchLine[off], cchBody);
    pszBody[cchBody] = '\0';

    Info.cBlankLinesBefore = cchBody == 0;

    /*
     * Look for more comment lines and append them to the body.
     */
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        iLine++;

        /* Skip leading spaces. */
        off = 0;
        while (off < cchLine && RT_C_IS_SPACE(pchLine[off]))
            off++;

        /* Check if it's a comment. */
        if (   off >= cchLine
            || (cchSkip = pfnIsComment(&pchLine[off], cchLine - off, false)) == 0)
            break;
        off += cchSkip;

        /* Split on doxygen comment start (if not already in one). */
        if (   Info.enmType == kScmCommentType_Line
            && off + 1 < cchLine
            && (   pfnIsComment(&pchLine[off], cchLine - off, true) > 0
                || (   pchLine[off + 1] == '!'
                    && (   off + 2 == cchLine
                        || pchLine[off + 2] != '!') ) ) )
        {
            off -= cchSkip;
            break;
        }

        /* Append the body w/o trailing spaces and some leading ones. */
        if (off < cchLine && RT_C_IS_SPACE(pchLine[off]))
            off++;
        while (off < cchLine && off < Info.offStart + 3 && RT_C_IS_SPACE(pchLine[off]))
            off++;
        size_t cchAppend = cchLine;
        while (cchAppend > off && RT_C_IS_SPACE(pchLine[cchAppend - 1]))
            cchAppend--;
        cchAppend -= off;

        size_t cchNewBody = cchBody + 1 + cchAppend;
        if (cchNewBody >= cbBodyAlloc)
        {
            cbBodyAlloc = RT_MAX(cbBodyAlloc ? cbBodyAlloc * 2 : _1K, RT_ALIGN_Z(cchNewBody + 64, 128));
            void *pvNew = RTMemRealloc(pszBody, cbBodyAlloc);
            if (pvNew)
                pszBody = (char *)pvNew;
            else
            {
                RTMemFree(pszBody);
                return VERR_NO_MEMORY;
            }
        }

        if (   cchBody > 0
            || cchAppend > 0)
        {
            if (cchBody > 0)
                pszBody[cchBody++] = '\n';
            memcpy(&pszBody[cchBody], &pchLine[off], cchAppend);
            cchBody += cchAppend;
            pszBody[cchBody] = '\0';
        }
        else
            Info.cBlankLinesBefore++;

        /* Advance. */
        Info.offEnd   = (uint32_t)cchLine;
        Info.iLineEnd = iLine;
    }

    /*
     * Strip trailing empty lines in the body.
     */
    Info.cBlankLinesAfter = 0;
    while (cchBody >= 1 && pszBody[cchBody - 1] == '\n')
    {
        Info.cBlankLinesAfter++;
        pszBody[--cchBody] = '\0';
    }

    /*
     * Do the callback and return.
     */
    int rc = pfnCallback(&Info, pszBody, cchBody, pvUser);

    RTMemFree(pszBody);

    *piLine   = iLine;
    *ppchLine = pchLine;
    *pcchLine = cchLine;
    *poff     = off;
    *penmEol  = enmEol;
    return rc;
}



/**
 * Common string literal handler.
 *
 * @returns new pchLine value.
 * @param   pIn         The input string.
 * @param   chType      The quotation type.
 * @param   pchLine     The current line.
 * @param   ppchLine    Pointer to the line variable.
 * @param   pcchLine    Pointer to the line length variable.
 * @param   penmEol     Pointer to the line ending type variable.
 * @param   piLine      Pointer to the line number variable.
 * @param   poff        Pointer to the line offset variable.
 */
static const char *handleStringLiteral(PSCMSTREAM pIn, char chType, const char *pchLine, size_t *pcchLine, PSCMEOL penmEol,
                                       uint32_t *piLine, size_t *poff)
{
    size_t off = *poff;
    for (;;)
    {
        bool fEnd = false;
        bool fEscaped = false;
        size_t const cchLine = *pcchLine;
        while (off < cchLine)
        {
            char ch = pchLine[off++];
            if (!fEscaped)
            {
                if (ch != chType)
                {
                    if (ch != '\\')
                    { /* likely */ }
                    else
                        fEscaped = true;
                }
                else
                {
                    fEnd = true;
                    break;
                }
            }
            else
                fEscaped = false;
        }
        if (fEnd)
            break;

        /* next line */
        pchLine = ScmStreamGetLine(pIn, pcchLine, penmEol);
        if (!pchLine)
            break;
        *piLine += 1;
        off = 0;
    }

    *poff = off;
    return pchLine;
}


/**
 * Deals with comments in C and C++ code.
 *
 * @returns VBox status code / callback return code.
 * @param   pIn                 The stream to parse.
 * @param   pfnCallback         The callback.
 * @param   pvUser              The user parameter for the callback.
 */
static int enumerateCStyleComments(PSCMSTREAM pIn, PFNSCMCOMMENTENUMERATOR pfnCallback, void *pvUser)
{
    AssertCompile('\'' < '/');
    AssertCompile('"'  < '/');

    int             rcRet = VINF_SUCCESS;
    uint32_t        iLine = 0;
    SCMEOL          enmEol;
    size_t          cchLine;
    const char     *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        size_t off = 0;
        while (off < cchLine)
        {
            unsigned ch = pchLine[off++];
            if (ch > (unsigned)'/')
            { /* not interesting */ }
            else if (ch == '/')
            {
                if (off < cchLine)
                {
                    ch = pchLine[off++];
                    if (ch == '*')
                    {
                        /*
                         * Multiline comment.  Find the end.
                         *
                         * Note! This is very similar to the python doc string handling further down.
                         */
                        SCMCOMMENTINFO  Info;
                        Info.iLineStart         = iLine;
                        Info.offStart           = (uint32_t)off - 2;
                        Info.iLineEnd           = UINT32_MAX;
                        Info.offEnd             = UINT32_MAX;
                        Info.cBlankLinesBefore  = 0;

                        /* Determine comment type (same as for line-comments). */
                        Info.enmType = kScmCommentType_MultiLine;
                        if (   off < cchLine
                            && (   (ch = pchLine[off]) == '*'
                                || ch == '!') )
                        {
                            unsigned ch2;
                            if (   off + 1 == cchLine
                                || RT_C_IS_SPACE(ch2 = pchLine[off + 1]) )
                            {
                                Info.enmType = ch == '*' ? kScmCommentType_MultiLine_JavaDoc : kScmCommentType_MultiLine_Qt;
                                off += 1;
                            }
                            else if (   ch2 == '<'
                                     && (   off + 2 == cchLine
                                         || RT_C_IS_SPACE(pchLine[off + 2]) ))
                            {
                                Info.enmType = ch == '*' ? kScmCommentType_MultiLine_JavaDoc_After
                                             : kScmCommentType_MultiLine_Qt_After;
                                off += 2;
                            }
                        }

                        /*
                         * Copy the body and find the end of the multiline comment.
                         */
                        size_t          cbBodyAlloc = 0;
                        size_t          cchBody     = 0;
                        char           *pszBody     = NULL;
                        for (;;)
                        {
                            /* Parse the line up to the end-of-comment or end-of-line. */
                            size_t offLineStart     = off;
                            size_t offLastNonBlank  = off;
                            size_t offFirstNonBlank = ~(size_t)0;
                            while (off < cchLine)
                            {
                                ch = pchLine[off++];
                                if (ch != '*' || off >= cchLine || pchLine[off] != '/')
                                {
                                    if (RT_C_IS_BLANK(ch))
                                    {/* kind of likely */}
                                    else
                                    {
                                        offLastNonBlank = off - 1;
                                        if (offFirstNonBlank != ~(size_t)0)
                                        {/* likely */}
                                        else if (   ch != '*'          /* ignore continuation-asterisks */
                                                 || off > Info.offStart + 1 + 1
                                                 || off > cchLine
                                                 || (   off < cchLine
                                                     && !RT_C_IS_SPACE(pchLine[off]))
                                                 || pszBody == NULL)
                                            offFirstNonBlank = off - 1;
                                    }
                                }
                                else
                                {
                                    Info.offEnd   = (uint32_t)++off;
                                    Info.iLineEnd = iLine;
                                    break;
                                }
                            }

                            /* Append line content to the comment body string. */
                            size_t cchAppend;
                            if (offFirstNonBlank == ~(size_t)0)
                                cchAppend = 0; /* empty line */
                            else
                            {
                                if (pszBody)
                                    offLineStart = RT_MIN(Info.offStart + 3, offFirstNonBlank);
                                else if (offFirstNonBlank > Info.offStart + 2) /* Skip one leading blank at the start of the comment. */
                                    offLineStart++;
                                cchAppend = offLastNonBlank + 1 - offLineStart;
                                Assert(cchAppend <= cchLine);
                            }

                            size_t cchNewBody = cchBody + (cchBody > 0) + cchAppend;
                            if (cchNewBody >= cbBodyAlloc)
                            {
                                cbBodyAlloc = RT_MAX(cbBodyAlloc ? cbBodyAlloc * 2 : _1K, RT_ALIGN_Z(cchNewBody + 64, 128));
                                void *pvNew = RTMemRealloc(pszBody, cbBodyAlloc);
                                if (pvNew)
                                    pszBody = (char *)pvNew;
                                else
                                {
                                    RTMemFree(pszBody);
                                    return VERR_NO_MEMORY;
                                }
                            }

                            if (cchBody > 0)                        /* no leading blank lines */
                                pszBody[cchBody++] = '\n';
                            else if (cchAppend == 0)
                                Info.cBlankLinesBefore++;
                            memcpy(&pszBody[cchBody], &pchLine[offLineStart], cchAppend);
                            cchBody += cchAppend;
                            pszBody[cchBody] = '\0';

                            /* Advance to the next line, if we haven't yet seen the end of this comment. */
                            if (Info.iLineEnd != UINT32_MAX)
                                break;
                            pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
                            if (!pchLine)
                            {
                                Info.offEnd   = (uint32_t)cchLine;
                                Info.iLineEnd = iLine;
                                break;
                            }
                            iLine++;
                            off = 0;
                        }

                        /* Strip trailing empty lines in the body. */
                        Info.cBlankLinesAfter = 0;
                        while (cchBody >= 1 && pszBody[cchBody - 1] == '\n')
                        {
                            Info.cBlankLinesAfter++;
                            pszBody[--cchBody] = '\0';
                        }

                        /* Do the callback. */
                        int rc = pfnCallback(&Info, pszBody, cchBody, pvUser);
                        RTMemFree(pszBody);
                        if (RT_FAILURE(rc))
                            return rc;
                        if (rc > VINF_SUCCESS && rcRet == VINF_SUCCESS)
                            rcRet = rc;
                    }
                    else if (ch == '/')
                    {
                        /*
                         * Line comment.  Join the other line comment guys.
                         */
                        off -= 2;
                        int rc = handleLineComment(pIn, isCppLineComment, pfnCallback, pvUser,
                                                   &pchLine, &cchLine, &enmEol, &iLine, &off);
                        if (RT_FAILURE(rc))
                            return rc;
                        if (rcRet == VINF_SUCCESS)
                            rcRet = rc;
                    }

                    if (!pchLine)
                        break;
                }
            }
            else if (ch == '"')
            {
                /*
                 * String literal may include sequences that looks like comments.  So,
                 * they needs special handling to avoid confusion.
                 */
                pchLine = handleStringLiteral(pIn, '"', pchLine, &cchLine, &enmEol, &iLine, &off);
            }
            /* else: We don't have to deal with character literal as these shouldn't
                     include comment-like sequences. */
        } /* for each character in the line */

        iLine++;
    } /* for each line in the stream */

    int rcStream = ScmStreamGetStatus(pIn);
    if (RT_SUCCESS(rcStream))
        return rcRet;
    return rcStream;
}


/**
 * Deals with comments in Python code.
 *
 * @returns VBox status code / callback return code.
 * @param   pIn                 The stream to parse.
 * @param   pfnCallback         The callback.
 * @param   pvUser              The user parameter for the callback.
 */
static int enumeratePythonComments(PSCMSTREAM pIn, PFNSCMCOMMENTENUMERATOR pfnCallback, void *pvUser)
{
    AssertCompile('#'  < '\'');
    AssertCompile('"'  < '\'');

    int             rcRet = VINF_SUCCESS;
    uint32_t        iLine = 0;
    SCMEOL          enmEol;
    size_t          cchLine;
    const char     *pchLine;
    SCMCOMMENTINFO  Info;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        size_t off = 0;
        while (off < cchLine)
        {
            char ch = pchLine[off++];
            if ((unsigned char)ch > (unsigned char)'\'')
            { /* not interesting */ }
            else if (ch == '#')
            {
                /*
                 * Line comment.  Join paths with the others.
                 */
                off -= 1;
                int rc = handleLineComment(pIn, isHashComment, pfnCallback, pvUser,
                                           &pchLine, &cchLine, &enmEol, &iLine, &off);
                if (RT_FAILURE(rc))
                    return rc;
                if (rcRet == VINF_SUCCESS)
                    rcRet = rc;

                if (!pchLine)
                    break;
            }
            else if (ch == '"' || ch == '\'')
            {
                /*
                 * String literal may be doc strings and they may legally include hashes.
                 */
                const char chType = ch;
                if (   off + 1 >= cchLine
                    || pchLine[off] != chType
                    || pchLine[off + 1] != chType)
                    pchLine = handleStringLiteral(pIn, chType, pchLine, &cchLine, &enmEol, &iLine, &off);
                else
                {
                    /*
                     * Doc string (/ long string).
                     *
                     * Note! This is very similar to the multiline C comment handling above.
                     */
                    Info.iLineStart         = iLine;
                    Info.offStart           = (uint32_t)off - 1;
                    Info.iLineEnd           = UINT32_MAX;
                    Info.offEnd             = UINT32_MAX;
                    Info.cBlankLinesBefore  = 0;
                    Info.enmType            = kScmCommentType_DocString;

                    off += 2;

                    /* Copy the body and find the end of the doc string comment. */
                    size_t          cbBodyAlloc = 0;
                    size_t          cchBody     = 0;
                    char           *pszBody     = NULL;
                    for (;;)
                    {
                        /* Parse the line up to the end-of-comment or end-of-line. */
                        size_t offLineStart     = off;
                        size_t offLastNonBlank  = off;
                        size_t offFirstNonBlank = ~(size_t)0;
                        bool fEscaped = false;
                        while (off < cchLine)
                        {
                            ch = pchLine[off++];
                            if (!fEscaped)
                            {
                                if (   off + 1 >= cchLine
                                    || ch != chType
                                    || pchLine[off] != chType
                                    || pchLine[off + 1] != chType)
                                {
                                    if (RT_C_IS_BLANK(ch))
                                    {/* kind of likely */}
                                    else
                                    {
                                        offLastNonBlank = off - 1;
                                        if (offFirstNonBlank != ~(size_t)0)
                                        {/* likely */}
                                        else if (   ch != '*'          /* ignore continuation-asterisks */
                                                 || off > Info.offStart + 1 + 1
                                                 || off > cchLine
                                                 || (   off < cchLine
                                                     && !RT_C_IS_SPACE(pchLine[off]))
                                                 || pszBody == NULL)
                                            offFirstNonBlank = off - 1;

                                        if (ch != '\\')
                                        {/* likely */ }
                                        else
                                            fEscaped = true;
                                    }
                                }
                                else
                                {
                                    off += 2;
                                    Info.offEnd   = (uint32_t)off;
                                    Info.iLineEnd = iLine;
                                    break;
                                }
                            }
                            else
                                fEscaped = false;
                        }

                        /* Append line content to the comment body string. */
                        size_t cchAppend;
                        if (offFirstNonBlank == ~(size_t)0)
                            cchAppend = 0; /* empty line */
                        else
                        {
                            if (pszBody)
                                offLineStart = RT_MIN(Info.offStart + 3, offFirstNonBlank);
                            else if (offFirstNonBlank > Info.offStart + 2) /* Skip one leading blank at the start of the comment. */
                                offLineStart++;
                            cchAppend = offLastNonBlank + 1 - offLineStart;
                            Assert(cchAppend <= cchLine);
                        }

                        size_t cchNewBody = cchBody + (cchBody > 0) + cchAppend;
                        if (cchNewBody >= cbBodyAlloc)
                        {
                            cbBodyAlloc = RT_MAX(cbBodyAlloc ? cbBodyAlloc * 2 : _1K, RT_ALIGN_Z(cchNewBody + 64, 128));
                            void *pvNew = RTMemRealloc(pszBody, cbBodyAlloc);
                            if (pvNew)
                                pszBody = (char *)pvNew;
                            else
                            {
                                RTMemFree(pszBody);
                                return VERR_NO_MEMORY;
                            }
                        }

                        if (cchBody > 0)                        /* no leading blank lines */
                            pszBody[cchBody++] = '\n';
                        else if (cchAppend == 0)
                            Info.cBlankLinesBefore++;
                        memcpy(&pszBody[cchBody], &pchLine[offLineStart], cchAppend);
                        cchBody += cchAppend;
                        pszBody[cchBody] = '\0';

                        /* Advance to the next line, if we haven't yet seen the end of this comment. */
                        if (Info.iLineEnd != UINT32_MAX)
                            break;
                        pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
                        if (!pchLine)
                        {
                            Info.offEnd   = (uint32_t)cchLine;
                            Info.iLineEnd = iLine;
                            break;
                        }
                        iLine++;
                        off = 0;
                    }

                    /* Strip trailing empty lines in the body. */
                    Info.cBlankLinesAfter = 0;
                    while (cchBody >= 1 && pszBody[cchBody - 1] == '\n')
                    {
                        Info.cBlankLinesAfter++;
                        pszBody[--cchBody] = '\0';
                    }

                    /* Do the callback. */
                    int rc = pfnCallback(&Info, pszBody, cchBody, pvUser);
                    RTMemFree(pszBody);
                    if (RT_FAILURE(rc))
                        return rc;
                    if (rc > VINF_SUCCESS && rcRet == VINF_SUCCESS)
                        rcRet = rc;
                }

                if (!pchLine)
                    break;
            }
            /* else: We don't have to deal with character literal as these shouldn't
                     include comment-like sequences. */
        } /* for each character in the line */

        iLine++;
    } /* for each line in the stream */

    int rcStream = ScmStreamGetStatus(pIn);
    if (RT_SUCCESS(rcStream))
        return rcRet;
    return rcStream;
}


/**
 * Deals with XML comments.
 *
 * @returns VBox status code / callback return code.
 * @param   pIn                 The stream to parse.
 * @param   pfnCallback         The callback.
 * @param   pvUser              The user parameter for the callback.
 */
static int enumerateXmlComments(PSCMSTREAM pIn, PFNSCMCOMMENTENUMERATOR pfnCallback, void *pvUser)
{
    int             rcRet = VINF_SUCCESS;
    uint32_t        iLine = 0;
    SCMEOL          enmEol;
    size_t          cchLine;
    const char     *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        size_t off = 0;
        while (off < cchLine)
        {
            /*
             * Skip leading blanks and check for start of XML comment.
             */
            while (off + 3 < cchLine && RT_C_IS_SPACE(pchLine[off]))
                off++;
            if (IS_XML_COMMENT_START(pchLine, off, cchLine))
            {
                /*
                 * XML comment.  Find the end.
                 *
                 * Note! This is very similar to the python doc string handling above.
                 */
                SCMCOMMENTINFO  Info;
                Info.iLineStart         = iLine;
                Info.offStart           = (uint32_t)off;
                Info.iLineEnd           = UINT32_MAX;
                Info.offEnd             = UINT32_MAX;
                Info.cBlankLinesBefore  = 0;
                Info.enmType            = kScmCommentType_Xml;

                off += 4;

                /*
                 * Copy the body and find the end of the XML comment.
                 */
                size_t          cbBodyAlloc = 0;
                size_t          cchBody     = 0;
                char           *pszBody     = NULL;
                for (;;)
                {
                    /* Parse the line up to the end-of-comment or end-of-line. */
                    size_t offLineStart     = off;
                    size_t offLastNonBlank  = off;
                    size_t offFirstNonBlank = ~(size_t)0;
                    while (off < cchLine)
                    {
                        if (!IS_XML_COMMENT_END(pchLine, off, cchLine))
                        {
                            char ch = pchLine[off++];
                            if (RT_C_IS_BLANK(ch))
                            {/* kind of likely */}
                            else
                            {
                                offLastNonBlank = off - 1;
                                if (offFirstNonBlank != ~(size_t)0)
                                {/* likely */}
                                else if (   (ch != '*' && ch != '#')    /* ignore continuation-asterisks */
                                         || off > Info.offStart + 1 + 1
                                         || off > cchLine
                                         || (   off < cchLine
                                              && !RT_C_IS_SPACE(pchLine[off]))
                                         || pszBody == NULL)
                                    offFirstNonBlank = off - 1;
                            }
                        }
                        else
                        {
                            off += 3;
                            Info.offEnd   = (uint32_t)off;
                            Info.iLineEnd = iLine;
                            break;
                        }
                    }

                    /* Append line content to the comment body string. */
                    size_t cchAppend;
                    if (offFirstNonBlank == ~(size_t)0)
                        cchAppend = 0; /* empty line */
                    else
                    {
                        offLineStart = offFirstNonBlank;
                        cchAppend = offLastNonBlank + 1 - offLineStart;
                        Assert(cchAppend <= cchLine);
                    }

                    size_t cchNewBody = cchBody + (cchBody > 0) + cchAppend;
                    if (cchNewBody >= cbBodyAlloc)
                    {
                        cbBodyAlloc = RT_MAX(cbBodyAlloc ? cbBodyAlloc * 2 : _1K, RT_ALIGN_Z(cchNewBody + 64, 128));
                        void *pvNew = RTMemRealloc(pszBody, cbBodyAlloc);
                        if (pvNew)
                            pszBody = (char *)pvNew;
                        else
                        {
                            RTMemFree(pszBody);
                            return VERR_NO_MEMORY;
                        }
                    }

                    if (cchBody > 0)                        /* no leading blank lines */
                        pszBody[cchBody++] = '\n';
                    else if (cchAppend == 0)
                        Info.cBlankLinesBefore++;
                    memcpy(&pszBody[cchBody], &pchLine[offLineStart], cchAppend);
                    cchBody += cchAppend;
                    pszBody[cchBody] = '\0';

                    /* Advance to the next line, if we haven't yet seen the end of this comment. */
                    if (Info.iLineEnd != UINT32_MAX)
                        break;
                    pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
                    if (!pchLine)
                    {
                        Info.offEnd   = (uint32_t)cchLine;
                        Info.iLineEnd = iLine;
                        break;
                    }
                    iLine++;
                    off = 0;
                }

                /* Strip trailing empty lines in the body. */
                Info.cBlankLinesAfter = 0;
                while (cchBody >= 1 && pszBody[cchBody - 1] == '\n')
                {
                    Info.cBlankLinesAfter++;
                    pszBody[--cchBody] = '\0';
                }

                /* Do the callback. */
                int rc = pfnCallback(&Info, pszBody, cchBody, pvUser);
                RTMemFree(pszBody);
                if (RT_FAILURE(rc))
                    return rc;
                if (rc > VINF_SUCCESS && rcRet == VINF_SUCCESS)
                    rcRet = rc;
            }
            else
                off++;
        } /* for each character in the line */

        iLine++;
    } /* for each line in the stream */

    int rcStream = ScmStreamGetStatus(pIn);
    if (RT_SUCCESS(rcStream))
        return rcRet;
    return rcStream;
}


/**
 * Deals with comments in DOS batch files.
 *
 * @returns VBox status code / callback return code.
 * @param   pIn                 The stream to parse.
 * @param   pfnCallback         The callback.
 * @param   pvUser              The user parameter for the callback.
 */
static int enumerateBatchComments(PSCMSTREAM pIn, PFNSCMCOMMENTENUMERATOR pfnCallback, void *pvUser)
{
    int             rcRet = VINF_SUCCESS;
    uint32_t        iLine = 0;
    SCMEOL          enmEol;
    size_t          cchLine;
    const char     *pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
    while (pchLine != NULL)
    {
        /*
         * Skip leading blanks and check for 'rem'.
         * At the moment we do not parse '::label-comments'.
         */
        size_t off = 0;
        while (off + 3 < cchLine && RT_C_IS_SPACE(pchLine[off]))
            off++;
        if (!IS_REM(pchLine, off, cchLine))
        {
            iLine++;
            pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
        }
        else
        {
            int rc = handleLineComment(pIn, isBatchComment, pfnCallback, pvUser,
                                       &pchLine, &cchLine, &enmEol, &iLine, &off);
            if (RT_FAILURE(rc))
                return rc;
            if (rcRet == VINF_SUCCESS)
                rcRet = rc;
        }
    }

    int rcStream = ScmStreamGetStatus(pIn);
    if (RT_SUCCESS(rcStream))
        return rcRet;
    return rcStream;
}


/**
 * Deals with comments in SQL files.
 *
 * @returns VBox status code / callback return code.
 * @param   pIn                 The stream to parse.
 * @param   pfnCallback         The callback.
 * @param   pvUser              The user parameter for the callback.
 */
static int enumerateSqlComments(PSCMSTREAM pIn, PFNSCMCOMMENTENUMERATOR pfnCallback, void *pvUser)
{
    int             rcRet = VINF_SUCCESS;
    uint32_t        iLine = 0;
    SCMEOL          enmEol;
    size_t          cchLine;
    const char     *pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
    while (pchLine != NULL)
    {
        /*
         * Skip leading blanks and check for '--'.
         */
        size_t off = 0;
        while (off + 3 < cchLine && RT_C_IS_SPACE(pchLine[off]))
            off++;
        if (   cchLine < 2
            || pchLine[0] != '-'
            || pchLine[1] != '-')
        {
            iLine++;
            pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol);
        }
        else
        {
            int rc = handleLineComment(pIn, isSqlComment, pfnCallback, pvUser,
                                       &pchLine, &cchLine, &enmEol, &iLine, &off);
            if (RT_FAILURE(rc))
                return rc;
            if (rcRet == VINF_SUCCESS)
                rcRet = rc;
        }
    }

    int rcStream = ScmStreamGetStatus(pIn);
    if (RT_SUCCESS(rcStream))
        return rcRet;
    return rcStream;
}


/**
 * Deals with simple line comments.
 *
 * @returns VBox status code / callback return code.
 * @param   pIn                 The stream to parse.
 * @param   chStart             The start of comment character.
 * @param   pfnIsComment        Comment tester function.
 * @param   pfnCallback         The callback.
 * @param   pvUser              The user parameter for the callback.
 */
static int enumerateSimpleLineComments(PSCMSTREAM pIn, char chStart, PFNISCOMMENT pfnIsComment,
                                       PFNSCMCOMMENTENUMERATOR pfnCallback, void *pvUser)
{
    int             rcRet = VINF_SUCCESS;
    uint32_t        iLine = 0;
    SCMEOL          enmEol;
    size_t          cchLine;
    const char     *pchLine;
    while ((pchLine = ScmStreamGetLine(pIn, &cchLine, &enmEol)) != NULL)
    {
        size_t off = 0;
        while (off < cchLine)
        {
            char ch = pchLine[off++];
            if (ch != chStart)
            { /* not interesting */ }
            else
            {
                off -= 1;
                int rc = handleLineComment(pIn, pfnIsComment, pfnCallback, pvUser,
                                           &pchLine, &cchLine, &enmEol,  &iLine, &off);
                if (RT_FAILURE(rc))
                    return rc;
                if (rcRet == VINF_SUCCESS)
                    rcRet = rc;

                if (!pchLine)
                    break;
            }
        } /* for each character in the line */

        iLine++;
    } /* for each line in the stream */

    int rcStream = ScmStreamGetStatus(pIn);
    if (RT_SUCCESS(rcStream))
        return rcRet;
    return rcStream;
}


/**
 * Enumerates the comments in the given stream, calling @a pfnCallback for each.
 *
 * @returns IPRT status code.
 * @param   pIn             The stream to parse.
 * @param   enmCommentStyle The comment style of the source stream.
 * @param   pfnCallback     The function to call.
 * @param   pvUser          User argument to the callback.
 */
int ScmEnumerateComments(PSCMSTREAM pIn, SCMCOMMENTSTYLE enmCommentStyle, PFNSCMCOMMENTENUMERATOR pfnCallback, void *pvUser)
{
    switch (enmCommentStyle)
    {
        case kScmCommentStyle_C:
            return enumerateCStyleComments(pIn, pfnCallback, pvUser);

        case kScmCommentStyle_Python:
            return enumeratePythonComments(pIn, pfnCallback, pvUser);

        case kScmCommentStyle_Semicolon:
            return enumerateSimpleLineComments(pIn, ';', isSemicolonComment, pfnCallback, pvUser);

        case kScmCommentStyle_Hash:
            return enumerateSimpleLineComments(pIn, '#', isHashComment, pfnCallback, pvUser);

        case kScmCommentStyle_Rem_Upper:
        case kScmCommentStyle_Rem_Lower:
        case kScmCommentStyle_Rem_Camel:
            return enumerateBatchComments(pIn, pfnCallback, pvUser);

        case kScmCommentStyle_Sql:
            return enumerateSqlComments(pIn, pfnCallback, pvUser);

        case kScmCommentStyle_Tick:
            return enumerateSimpleLineComments(pIn, '\'', isTickComment, pfnCallback, pvUser);

        case kScmCommentStyle_Xml:
            return enumerateXmlComments(pIn, pfnCallback, pvUser);

        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }
}

