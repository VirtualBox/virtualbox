/* $Id: messagerefentry.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Program usage and help formatting.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/message.h>

#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include "internal/process.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Spaces for intending. */
static const char           g_szSpaces[] = "                                                ";


/**
 * Retruns the width for the given handle.
 *
 * @returns Screen width.
 * @param   pStrm           The stream, g_pStdErr or g_pStdOut.
 */
static uint32_t getScreenWidth(PRTSTREAM pStrm)
{
    static uint32_t s_acch[2] = { 0, 0 };
    uint32_t        iWhich    = pStrm == g_pStdErr ? 1 : 0;
    uint32_t        cch       = s_acch[iWhich];
    if (cch)
        return cch;

    const char *psz = RTEnvGet("IPRT_SCREEN_WIDTH");
    if (   !psz
        || RTStrToUInt32Full(psz, 0, &cch) != VINF_SUCCESS
        || cch == 0)
    {
        int rc = RTStrmQueryTerminalWidth(pStrm, &cch);
        if (rc == VERR_INVALID_FUNCTION)
        {
            /* It's not a console, but in case we're being piped to less/more/list
               we look for a console handle on the other standard output handle
               and standard input.  (Latter doesn't work on windows.)  */
            rc = RTStrmQueryTerminalWidth(pStrm == g_pStdErr ? g_pStdOut : g_pStdErr, &cch);
            if (rc == VERR_INVALID_FUNCTION || rc == VERR_INVALID_HANDLE)
                rc = RTStrmQueryTerminalWidth(g_pStdIn, &cch);
            if (RT_FAILURE(rc))
                cch = 80;
        }
    }

    s_acch[iWhich] = cch;
    return cch;
}


/**
 * Prints a string table string (paragraph), performing non-breaking-space
 * replacement and wrapping.
 *
 * @returns IRPT status code.
 * @param   pStrm           The output stream.
 * @param   psz             The string table string to print.
 * @param   cchMaxWidth     The maximum output width.
 * @param   fFlags          String flags that may affect formatting.
 * @param   pcLinesWritten  Pointer to variable to update with written lines.
 */
static int printString(PRTSTREAM pStrm, const char *psz, uint32_t cchMaxWidth, uint64_t fFlags, uint32_t *pcLinesWritten)
{
    uint32_t    cLinesWritten;
    size_t      cch     = strlen(psz);
    const char *pszNbsp = strchr(psz, RTMSGREFENTRY_NBSP);
    int         rc;

    /*
     * No-wrap case is simpler, so handle that separately.
     */
    if (cch <= cchMaxWidth)
    {
        if (!pszNbsp)
            rc = RTStrmWrite(pStrm, psz, cch);
        else
        {
            do
            {
                rc = RTStrmWrite(pStrm, psz, pszNbsp - psz);
                if (RT_SUCCESS(rc))
                    rc = RTStrmPutCh(pStrm, ' ');
                psz = pszNbsp + 1;
                pszNbsp = strchr(psz, RTMSGREFENTRY_NBSP);
            } while (pszNbsp && RT_SUCCESS(rc));
            if (RT_SUCCESS(rc))
                rc = RTStrmWrite(pStrm, psz, strlen(psz));
        }
        if (RT_SUCCESS(rc))
            rc = RTStrmPutCh(pStrm, '\n');
        cLinesWritten = 1;
    }
    /*
     * We need to wrap stuff, too bad.
     */
    else
    {
        /* Figure the paragraph indent level first. */
        uint32_t cchIndent = 0;
        while (*psz == ' ')
            cchIndent++, psz++;
        Assert(cchIndent + 4 + 1 <= RT_ELEMENTS(g_szSpaces));

        if (cchIndent + 8 >= cchMaxWidth)
            cchMaxWidth += cchIndent + 8;

        /* Work our way thru the string, line by line. */
        uint32_t cchHangingIndent = 0;
        cLinesWritten = 0;
        do
        {
            rc = RTStrmWrite(pStrm, g_szSpaces, cchIndent + cchHangingIndent);
            if (RT_FAILURE(rc))
                break;

            size_t  offLine       = cchIndent + cchHangingIndent;
            bool    fPendingSpace = false;
            do
            {
                const char *pszSpace = strchr(psz, ' ');
                size_t      cchWord  = pszSpace ? pszSpace - psz : strlen(psz);
                if (   offLine + cchWord + fPendingSpace > cchMaxWidth
                    && offLine != cchIndent
                    && fPendingSpace /* don't stop before first word */)
                    break;

                pszNbsp = (const char *)memchr(psz, RTMSGREFENTRY_NBSP, cchWord);
                while (pszNbsp)
                {
                    size_t cchSubWord = pszNbsp - psz;
                    if (fPendingSpace)
                    {
                        rc = RTStrmPutCh(pStrm, ' ');
                        if (RT_FAILURE(rc))
                            break;
                    }
                    rc = RTStrmWrite(pStrm, psz, cchSubWord);
                    if (RT_FAILURE(rc))
                        break;
                    offLine += cchSubWord + fPendingSpace;
                    psz     += cchSubWord + 1;
                    cchWord -= cchSubWord + 1;
                    pszNbsp = (const char *)memchr(psz, RTMSGREFENTRY_NBSP, cchWord);
                    fPendingSpace = true;
                }
                if (RT_FAILURE(rc))
                    break;

                if (fPendingSpace)
                {
                    rc = RTStrmPutCh(pStrm, ' ');
                    if (RT_FAILURE(rc))
                        break;
                }
                rc = RTStrmWrite(pStrm, psz, cchWord);
                if (RT_FAILURE(rc))
                    break;

                offLine += cchWord + fPendingSpace;
                psz      = pszSpace ? pszSpace + 1 : strchr(psz, '\0');
                fPendingSpace = true;
            } while (offLine < cchMaxWidth && *psz != '\0' && RT_SUCCESS(rc));

            if (RT_SUCCESS(rc))
                rc = RTStrmPutCh(pStrm, '\n');
            if (RT_FAILURE(rc))
                break;
            cLinesWritten++;

            /* Set up hanging indent if relevant. */
            if (fFlags & RTMSGREFENTRYSTR_FLAGS_SYNOPSIS)
                cchHangingIndent = 4;
        } while (*psz != '\0');
    }
    *pcLinesWritten += cLinesWritten;
    return rc;
}


/**
 * Checks if the given string is empty (only spaces).
 * @returns true if empty, false if not.
 * @param   psz                 The string to examine.
 */
DECLINLINE(bool) isEmptyString(const char *psz)
{
    char ch;
    while ((ch = *psz) == ' ')
        psz++;
    return ch == '\0';
}


/**
 * Prints a string table.
 *
 * @returns Current number of pending blank lines.
 * @param   pStrm               The output stream.
 * @param   pStrTab             The string table.
 * @param   fScope              The selection scope.
 * @param   pcPendingBlankLines In: Pending blank lines from previous string
 *                              table.  Out: Pending blank lines.
 * @param   pcLinesWritten      Pointer to variable that should be incremented
 *                              by the number of lines written.  Optional.
 */
RTDECL(int) RTMsgRefEntryPrintStringTable(PRTSTREAM pStrm, PCRTMSGREFENTRYSTRTAB pStrTab, uint64_t fScope,
                                          uint32_t *pcPendingBlankLines, uint32_t *pcLinesWritten)
{
    uint32_t cPendingBlankLines = pcPendingBlankLines ? *pcPendingBlankLines : 0;
    uint32_t cLinesWritten      = 0;
    uint32_t cchWidth           = getScreenWidth(pStrm) - 1; /* (Seems a -1 here is prudent, at least on windows.) */
    uint64_t fPrevScope         = fScope;
    int      rc                 = VINF_SUCCESS;
    for (uint32_t i = 0; i < pStrTab->cStrings; i++)
    {
        uint64_t fCurScope = pStrTab->paStrings[i].fScope;
        if ((fCurScope & RTMSGREFENTRYSTR_SCOPE_MASK) == RTMSGREFENTRYSTR_SCOPE_SAME)
        {
            fCurScope &= ~RTMSGREFENTRYSTR_SCOPE_MASK;
            fCurScope |= (fPrevScope & RTMSGREFENTRYSTR_SCOPE_MASK);
        }
        if (fCurScope & RTMSGREFENTRYSTR_SCOPE_MASK & fScope)
        {
            const char *psz = pStrTab->paStrings[i].psz;
            if (psz && !isEmptyString(psz))
            {
                while (cPendingBlankLines > 0 && RT_SUCCESS(rc))
                {
                    cPendingBlankLines--;
                    rc = RTStrmPutCh(pStrm, '\n');
                    cLinesWritten++;
                }
                if (RT_SUCCESS(rc))
                    rc = printString(pStrm, psz, cchWidth, fCurScope & RTMSGREFENTRYSTR_FLAGS_MASK, &cLinesWritten);
                if (RT_FAILURE(rc))
                    break;
            }
            else
                cPendingBlankLines++;
        }
        fPrevScope = fCurScope;
    }

    if (pcLinesWritten)
        *pcLinesWritten += cLinesWritten;
    if (pcPendingBlankLines)
        *pcPendingBlankLines = cPendingBlankLines;
    return rc;
}


RTDECL(int) RTMsgRefEntrySynopsisEx(PRTSTREAM pStrm, PCRTMSGREFENTRY pEntry, uint64_t fScope, uint32_t fFlags)
{
    AssertReturn(!(fFlags & ~RTMSGREFENTRY_SYNOPSIS_F_USAGE), VERR_INVALID_FLAGS);

    if (!pStrm)
        pStrm = g_pStdOut;
    int rc = VINF_SUCCESS;
    if (fFlags & RTMSGREFENTRY_SYNOPSIS_F_USAGE)
        RTStrmPutStr(pStrm, "Usage: ");
    if (RT_SUCCESS(rc))
        rc = RTMsgRefEntryPrintStringTable(pStrm, &pEntry->Synopsis, fScope, NULL, NULL);
    return rc;
}


RTDECL(int) RTMsgRefEntrySynopsis(PRTSTREAM pStrm, PCRTMSGREFENTRY pEntry)
{
    return RTMsgRefEntrySynopsisEx(pStrm, pEntry, UINT64_MAX, true /*fPrintUsage*/);
}


RTDECL(int) RTMsgRefEntryHelpEx(PRTSTREAM pStrm, PCRTMSGREFENTRY pEntry, uint64_t fScope, uint32_t fFlags)
{
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);
    if (!pStrm)
        pStrm = g_pStdOut;
    return RTMsgRefEntryPrintStringTable(pStrm, &pEntry->Help, fScope, NULL, NULL);
}


RTDECL(int) RTMsgRefEntryHelp(PRTSTREAM pStrm, PCRTMSGREFENTRY pEntry)
{
    return RTMsgRefEntryHelpEx(pStrm, pEntry, UINT64_MAX, 0 /*fFlags*/);
}

