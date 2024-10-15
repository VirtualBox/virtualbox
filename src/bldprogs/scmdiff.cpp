/* $Id: scmdiff.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase / Tool - Source Code Massager.
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
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include "scmdiff.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char   g_szTabSpaces[16+1]     = "                ";



/**
 * Prints a range of lines with a prefix.
 *
 * @param   pState              The diff state.
 * @param   chPrefix            The prefix.
 * @param   pStream             The stream to get the lines from.
 * @param   iLine               The first line.
 * @param   cLines              The number of lines.
 */
static void scmDiffPrintLines(PSCMDIFFSTATE pState, char chPrefix, PSCMSTREAM pStream, size_t iLine, size_t cLines)
{
    while (cLines-- > 0)
    {
        SCMEOL      enmEol;
        size_t      cchLine;
        const char *pchLine = ScmStreamGetLineByNo(pStream, iLine, &cchLine, &enmEol);

        RTStrmPutCh(pState->pDiff, chPrefix);
        if (pchLine && cchLine)
        {
            if (!pState->fSpecialChars)
                RTStrmWrite(pState->pDiff, pchLine, cchLine);
            else
            {
                size_t      offVir   = 0;
                const char *pchStart = pchLine;
                const char *pchTab   = (const char *)memchr(pchLine, '\t', cchLine);
                while (pchTab)
                {
                    RTStrmWrite(pState->pDiff, pchStart, pchTab - pchStart);
                    offVir += pchTab - pchStart;

                    size_t cchTab = pState->cchTab - offVir % pState->cchTab;
                    switch (cchTab)
                    {
                        case 1: RTStrmPutStr(pState->pDiff, "."); break;
                        case 2: RTStrmPutStr(pState->pDiff, ".."); break;
                        case 3: RTStrmPutStr(pState->pDiff, "[T]"); break;
                        case 4: RTStrmPutStr(pState->pDiff, "[TA]"); break;
                        case 5: RTStrmPutStr(pState->pDiff, "[TAB]"); break;
                        default: RTStrmPrintf(pState->pDiff, "[TAB%.*s]", cchTab - 5, g_szTabSpaces); break;
                    }
                    offVir += cchTab;

                    /* next */
                    pchStart = pchTab + 1;
                    pchTab = (const char *)memchr(pchStart, '\t', cchLine - (pchStart - pchLine));
                }
                size_t cchLeft = cchLine - (pchStart - pchLine);
                if (cchLeft)
                    RTStrmWrite(pState->pDiff, pchStart, cchLeft);
            }
        }

        if (!pState->fSpecialChars)
            RTStrmPutCh(pState->pDiff, '\n');
        else if (enmEol == SCMEOL_LF)
            RTStrmPutStr(pState->pDiff, "[LF]\n");
        else if (enmEol == SCMEOL_CRLF)
            RTStrmPutStr(pState->pDiff, "[CRLF]\n");
        else
            RTStrmPutStr(pState->pDiff, "[NONE]\n");

        iLine++;
    }
}


/**
 * Reports a difference and propels the streams to the lines following the
 * resync.
 *
 *
 * @returns New pState->cDiff value (just to return something).
 * @param   pState              The diff state.  The cDiffs member will be
 *                              incremented.
 * @param   cMatches            The resync length.
 * @param   iLeft               Where the difference starts on the left side.
 * @param   cLeft               How long it is on this side.  ~(size_t)0 is used
 *                              to indicate that it goes all the way to the end.
 * @param   iRight              Where the difference starts on the right side.
 * @param   cRight              How long it is.
 */
static size_t scmDiffReport(PSCMDIFFSTATE pState, size_t cMatches,
                            size_t iLeft, size_t cLeft,
                            size_t iRight, size_t cRight)
{
    /*
     * Adjust the input.
     */
    if (cLeft == ~(size_t)0)
    {
        size_t c = ScmStreamCountLines(pState->pLeft);
        if (c >= iLeft)
            cLeft = c - iLeft;
        else
        {
            iLeft = c;
            cLeft = 0;
        }
    }

    if (cRight == ~(size_t)0)
    {
        size_t c = ScmStreamCountLines(pState->pRight);
        if (c >= iRight)
            cRight = c - iRight;
        else
        {
            iRight = c;
            cRight = 0;
        }
    }

    /*
     * Print header if it's the first difference
     */
    if (!pState->cDiffs)
        RTStrmPrintf(pState->pDiff, "diff %s %s\n", pState->pszFilename, pState->pszFilename);

    /*
     * Emit the change description.
     */
    char ch = cLeft == 0
            ? 'a'
            : cRight == 0
            ? 'd'
            : 'c';
    if (cLeft > 1 && cRight > 1)
        RTStrmPrintf(pState->pDiff, "%zu,%zu%c%zu,%zu\n", iLeft + 1, iLeft + cLeft, ch, iRight + 1, iRight + cRight);
    else if (cLeft > 1)
        RTStrmPrintf(pState->pDiff, "%zu,%zu%c%zu\n",     iLeft + 1, iLeft + cLeft, ch, iRight + 1);
    else if (cRight > 1)
        RTStrmPrintf(pState->pDiff, "%zu%c%zu,%zu\n",     iLeft + 1,                ch, iRight + 1, iRight + cRight);
    else
        RTStrmPrintf(pState->pDiff, "%zu%c%zu\n",         iLeft + 1,                ch, iRight + 1);

    /*
     * And the lines.
     */
    if (cLeft)
        scmDiffPrintLines(pState, '<', pState->pLeft, iLeft, cLeft);
    if (cLeft && cRight)
        RTStrmPrintf(pState->pDiff, "---\n");
    if (cRight)
        scmDiffPrintLines(pState, '>', pState->pRight, iRight, cRight);

    /*
     * Reposition the streams (safely ignores return value).
     */
    ScmStreamSeekByLine(pState->pLeft,  iLeft  + cLeft  + cMatches);
    ScmStreamSeekByLine(pState->pRight, iRight + cRight + cMatches);

    pState->cDiffs++;
    return pState->cDiffs;
}

/**
 * Helper for scmDiffCompare that takes care of trailing spaces and stuff
 * like that.
 */
static bool scmDiffCompareSlow(PSCMDIFFSTATE pState,
                               const char *pchLeft,  size_t cchLeft,  SCMEOL enmEolLeft,
                               const char *pchRight, size_t cchRight, SCMEOL enmEolRight)
{
    if (pState->fIgnoreTrailingWhite)
    {
        while (cchLeft > 0 && RT_C_IS_SPACE(pchLeft[cchLeft - 1]))
            cchLeft--;
        while (cchRight > 0 && RT_C_IS_SPACE(pchRight[cchRight - 1]))
            cchRight--;
    }

    if (pState->fIgnoreLeadingWhite)
    {
        while (cchLeft > 0 && RT_C_IS_SPACE(*pchLeft))
            pchLeft++, cchLeft--;
        while (cchRight > 0 && RT_C_IS_SPACE(*pchRight))
            pchRight++, cchRight--;
    }

    if (   cchLeft != cchRight
        || (enmEolLeft != enmEolRight && !pState->fIgnoreEol)
        || memcmp(pchLeft, pchRight, cchLeft))
        return false;
    return true;
}

/**
 * Compare two lines.
 *
 * @returns true if the are equal, false if not.
 */
DECLINLINE(bool) scmDiffCompare(PSCMDIFFSTATE pState,
                                const char *pchLeft,  size_t cchLeft,  SCMEOL enmEolLeft,
                                const char *pchRight, size_t cchRight, SCMEOL enmEolRight)
{
    if (   cchLeft != cchRight
        || (enmEolLeft != enmEolRight && !pState->fIgnoreEol)
        || memcmp(pchLeft, pchRight, cchLeft))
    {
        if (   pState->fIgnoreTrailingWhite
            || pState->fIgnoreLeadingWhite)
            return scmDiffCompareSlow(pState,
                                      pchLeft, cchLeft, enmEolLeft,
                                      pchRight, cchRight, enmEolRight);
        return false;
    }
    return true;
}

/**
 * Compares two sets of lines from the two files.
 *
 * @returns true if they matches, false if they don't.
 * @param   pState              The diff state.
 * @param   iLeft               Where to start in the left stream.
 * @param   iRight              Where to start in the right stream.
 * @param   cLines              How many lines to compare.
 */
static bool scmDiffCompareLines(PSCMDIFFSTATE pState, size_t iLeft, size_t iRight, size_t cLines)
{
    for (size_t iLine = 0; iLine < cLines; iLine++)
    {
        SCMEOL      enmEolLeft;
        size_t      cchLeft;
        const char *pchLeft  = ScmStreamGetLineByNo(pState->pLeft,  iLeft + iLine,  &cchLeft,  &enmEolLeft);

        SCMEOL      enmEolRight;
        size_t      cchRight;
        const char *pchRight = ScmStreamGetLineByNo(pState->pRight, iRight + iLine, &cchRight, &enmEolRight);

        if (!scmDiffCompare(pState, pchLeft, cchLeft, enmEolLeft, pchRight, cchRight, enmEolRight))
            return false;
    }
    return true;
}


/**
 * Resynchronize the two streams and reports the difference.
 *
 * Upon return, the streams will be positioned after the block of @a cMatches
 * lines where it resynchronized them.
 *
 * @returns pState->cDiffs (just so we can use it in a return statement).
 * @param   pState              The state.
 * @param   cMatches            The number of lines that needs to match for the
 *                              stream to be considered synchronized again.
 */
static size_t scmDiffSynchronize(PSCMDIFFSTATE pState, size_t cMatches)
{
    size_t const iStartLeft  = ScmStreamTellLine(pState->pLeft)  - 1;
    size_t const iStartRight = ScmStreamTellLine(pState->pRight) - 1;
    Assert(cMatches > 0);

    /*
     * Compare each new line from each of the streams will all the preceding
     * ones, including iStartLeft/Right.
     */
    for (size_t iRange = 1; ; iRange++)
    {
        /*
         * Get the next line in the left stream and compare it against all the
         * preceding lines on the right side.
         */
        SCMEOL      enmEol;
        size_t      cchLine;
        const char *pchLine = ScmStreamGetLineByNo(pState->pLeft, iStartLeft + iRange, &cchLine, &enmEol);
        if (!pchLine)
            return scmDiffReport(pState, 0, iStartLeft, ~(size_t)0, iStartRight, ~(size_t)0);

        for (size_t iRight = cMatches - 1; iRight < iRange; iRight++)
        {
            SCMEOL      enmEolRight;
            size_t      cchRight;
            const char *pchRight = ScmStreamGetLineByNo(pState->pRight, iStartRight + iRight,
                                                        &cchRight, &enmEolRight);
            if (   scmDiffCompare(pState, pchLine, cchLine, enmEol, pchRight, cchRight, enmEolRight)
                && scmDiffCompareLines(pState,
                                       iStartLeft  + iRange + 1 - cMatches,
                                       iStartRight + iRight + 1 - cMatches,
                                       cMatches - 1)
               )
                return scmDiffReport(pState, cMatches,
                                     iStartLeft,  iRange + 1 - cMatches,
                                     iStartRight, iRight + 1 - cMatches);
        }

        /*
         * Get the next line in the right stream and compare it against all the
         * lines on the right side.
         */
        pchLine = ScmStreamGetLineByNo(pState->pRight, iStartRight + iRange, &cchLine, &enmEol);
        if (!pchLine)
            return scmDiffReport(pState, 0, iStartLeft, ~(size_t)0, iStartRight, ~(size_t)0);

        for (size_t iLeft = cMatches - 1; iLeft <= iRange; iLeft++)
        {
            SCMEOL      enmEolLeft;
            size_t      cchLeft;
            const char *pchLeft = ScmStreamGetLineByNo(pState->pLeft, iStartLeft + iLeft,
                                                       &cchLeft, &enmEolLeft);
            if (    scmDiffCompare(pState, pchLeft, cchLeft, enmEolLeft, pchLine, cchLine, enmEol)
                && scmDiffCompareLines(pState,
                                       iStartLeft  + iLeft  + 1 - cMatches,
                                       iStartRight + iRange + 1 - cMatches,
                                       cMatches - 1)
               )
                return scmDiffReport(pState, cMatches,
                                     iStartLeft,  iLeft  + 1 - cMatches,
                                     iStartRight, iRange + 1 - cMatches);
        }
    }
}

/**
 * Creates a diff of the changes between the streams @a pLeft and @a pRight.
 *
 * This currently only implements the simplest diff format, so no contexts.
 *
 * Also, note that we won't detect differences in the final newline of the
 * streams.
 *
 * @returns The number of differences.
 * @param   pszFilename         The filename.
 * @param   pLeft               The left side stream.
 * @param   pRight              The right side stream.
 * @param   fIgnoreEol          Whether to ignore end of line markers.
 * @param   fIgnoreLeadingWhite Set if leading white space should be ignored.
 * @param   fIgnoreTrailingWhite  Set if trailing white space should be ignored.
 * @param   fSpecialChars       Whether to print special chars in a human
 *                              readable form or not.
 * @param   cchTab              The tab size.
 * @param   pDiff               Where to write the diff.
 */
size_t ScmDiffStreams(const char *pszFilename, PSCMSTREAM pLeft, PSCMSTREAM pRight, bool fIgnoreEol,
                      bool fIgnoreLeadingWhite, bool fIgnoreTrailingWhite, bool fSpecialChars,
                      size_t cchTab, PRTSTREAM pDiff)
{
#ifdef RT_STRICT
    ScmStreamCheckItegrity(pLeft);
    ScmStreamCheckItegrity(pRight);
#endif

    /*
     * Set up the diff state.
     */
    SCMDIFFSTATE State;
    State.cDiffs                = 0;
    State.pszFilename           = pszFilename;
    State.pLeft                 = pLeft;
    State.pRight                = pRight;
    State.fIgnoreEol            = fIgnoreEol;
    State.fIgnoreLeadingWhite   = fIgnoreLeadingWhite;
    State.fIgnoreTrailingWhite  = fIgnoreTrailingWhite;
    State.fSpecialChars         = fSpecialChars;
    State.cchTab                = cchTab;
    State.pDiff                 = pDiff;

    /*
     * Compare them line by line.
     */
    ScmStreamRewindForReading(pLeft);
    ScmStreamRewindForReading(pRight);
    const char *pchLeft;
    const char *pchRight;

    for (;;)
    {
        SCMEOL  enmEolLeft;
        size_t  cchLeft;
        pchLeft  = ScmStreamGetLine(pLeft,  &cchLeft,  &enmEolLeft);

        SCMEOL  enmEolRight;
        size_t  cchRight;
        pchRight = ScmStreamGetLine(pRight, &cchRight, &enmEolRight);
        if (!pchLeft || !pchRight)
            break;

        if (!scmDiffCompare(&State, pchLeft, cchLeft, enmEolLeft, pchRight, cchRight, enmEolRight))
            scmDiffSynchronize(&State, 3);
    }

    /*
     * Deal with any remaining differences.
     */
    if (pchLeft)
        scmDiffReport(&State, 0, ScmStreamTellLine(pLeft) - 1, ~(size_t)0, ScmStreamTellLine(pRight), 0);
    else if (pchRight)
        scmDiffReport(&State, 0, ScmStreamTellLine(pLeft), 0, ScmStreamTellLine(pRight) - 1, ~(size_t)0);

    /*
     * Report any errors.
     */
    if (RT_FAILURE(ScmStreamGetStatus(pLeft)))
        RTMsgError("Left diff stream error: %Rrc\n", ScmStreamGetStatus(pLeft));
    if (RT_FAILURE(ScmStreamGetStatus(pRight)))
        RTMsgError("Right diff stream error: %Rrc\n", ScmStreamGetStatus(pRight));

    return State.cDiffs;
}

