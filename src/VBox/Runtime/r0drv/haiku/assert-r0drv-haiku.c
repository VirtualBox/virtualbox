/* $Id: assert-r0drv-haiku.c 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Assertion Workers, Ring-0 Drivers, Haiku.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include "the-haiku-kernel.h"
#include "internal/iprt.h"
#include <iprt/assert.h>

#include <iprt/asm.h>
#include <iprt/log.h>
#include <iprt/stdarg.h>
#include <iprt/string.h>

#include "internal/assert.h"


void rtR0AssertNativeMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    dprintf("\r\n!!Assertion Failed!!\r\n"
            "Expression: %s\r\n"
            "Location  : %s(%d) %s\r\n",
            pszExpr, pszFile, uLine, pszFunction);
}


void rtR0AssertNativeMsg2V(bool fInitial, const char *pszFormat, va_list va)
{
    char szMsg[256];

    RTStrPrintfV(szMsg, sizeof(szMsg) - 1, pszFormat, va);
    szMsg[sizeof(szMsg) - 1] = '\0';
    dprintf("%s", szMsg);

    NOREF(fInitial);
}


RTR0DECL(void) RTR0AssertPanicSystem(void)
{
    panic("%s%s", g_szRTAssertMsg1, g_szRTAssertMsg2);
}

