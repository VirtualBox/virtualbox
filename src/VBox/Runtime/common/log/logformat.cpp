/* $Id: logformat.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Log Formatter.
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
#include <iprt/log.h>
#include "internal/iprt.h"

#include <iprt/string.h>
#include <iprt/assert.h>
#ifdef IN_RING3
# include <iprt/thread.h>
# include <iprt/errcore.h>
#endif

#include <iprt/stdarg.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(size_t) rtlogFormatStr(void *pvArg, PFNRTSTROUTPUT pfnOutput,
                                           void *pvArgOutput, const char **ppszFormat,
                                           va_list *pArgs, int cchWidth, int cchPrecision,
                                           unsigned fFlags, char chArgSize);


/**
 * Partial vsprintf worker implementation.
 *
 * @returns number of bytes formatted.
 * @param   pfnOutput   Output worker.
 *                      Called in two ways. Normally with a string an it's length.
 *                      For termination, it's called with NULL for string, 0 for length.
 * @param   pvArg       Argument to output worker.
 * @param   pszFormat   Format string.
 * @param   args        Argument list.
 */
RTDECL(size_t) RTLogFormatV(PFNRTSTROUTPUT pfnOutput, void *pvArg, const char *pszFormat, va_list args)
{
    return RTStrFormatV(pfnOutput, pvArg, rtlogFormatStr, NULL, pszFormat, args);
}
RT_EXPORT_SYMBOL(RTLogFormatV);


/**
 * Callback to format VBox formatting extentions.
 * See @ref pg_rt_str_format for a reference on the format types.
 *
 * @returns The number of bytes formatted.
 * @param   pvArg           Formatter argument.
 * @param   pfnOutput       Pointer to output function.
 * @param   pvArgOutput     Argument for the output function.
 * @param   ppszFormat      Pointer to the format string pointer. Advance this till the char
 *                          after the format specifier.
 * @param   pArgs           Pointer to the argument list. Use this to fetch the arguments.
 * @param   cchWidth        Format Width. -1 if not specified.
 * @param   cchPrecision    Format Precision. -1 if not specified.
 * @param   fFlags          Flags (RTSTR_NTFS_*).
 * @param   chArgSize       The argument size specifier, 'l' or 'L'.
 */
static DECLCALLBACK(size_t) rtlogFormatStr(void *pvArg, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                           const char **ppszFormat, va_list *pArgs, int cchWidth,
                                           int cchPrecision, unsigned fFlags, char chArgSize)
{
    char ch = *(*ppszFormat)++;

    AssertMsgFailed(("Invalid logger format type '%%%c%.10s'!\n", ch, *ppszFormat)); NOREF(ch);

    NOREF(pvArg); NOREF(pfnOutput); NOREF(pvArgOutput); NOREF(pArgs); NOREF(cchWidth);
    NOREF(cchPrecision); NOREF(fFlags); NOREF(chArgSize);
    return 0;
}

