/* $Id: assert-r0drv-darwin.cpp 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT -  Assertion Workers, Ring-0 Drivers, Darwin.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include "the-darwin-kernel.h"
#include "internal/iprt.h"
#include <iprt/assert.h>

#include <iprt/asm.h>
#include <iprt/log.h>
#include <iprt/stdarg.h>
#include <iprt/string.h>

#include "internal/assert.h"


DECLHIDDEN(void) rtR0AssertNativeMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    IPRT_DARWIN_SAVE_EFL_AC();
    kprintf("\r\n!!Assertion Failed!!\r\n"
            "Expression: %s\r\n"
            "Location  : %s(%u) %s\r\n",
            pszExpr, pszFile, uLine, pszFunction);
    printf("\r\n!!Assertion Failed!!\r\n"
           "Expression: %s\r\n"
           "Location  : %s(%u) %s\r\n",
           pszExpr, pszFile, uLine, pszFunction);
    IPRT_DARWIN_RESTORE_EFL_AC();
}


DECLHIDDEN(void) rtR0AssertNativeMsg2V(bool fInitial, const char *pszFormat, va_list va)
{
    IPRT_DARWIN_SAVE_EFL_AC();
    char szMsg[256];

    RTStrPrintfV(szMsg, sizeof(szMsg) - 1, pszFormat, va);
    szMsg[sizeof(szMsg) - 1] = '\0';
    kprintf("%s", szMsg);
    printf("%s", szMsg);

    NOREF(fInitial);
    IPRT_DARWIN_RESTORE_EFL_AC();
}


RTR0DECL(void) RTR0AssertPanicSystem(void)
{
    panic("%s%s", g_szRTAssertMsg1, g_szRTAssertMsg2);
}

