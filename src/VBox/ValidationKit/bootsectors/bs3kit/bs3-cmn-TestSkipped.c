/* $Id: bs3-cmn-TestSkipped.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3TestSkipped
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
#include "bs3kit-template-header.h"
#include "bs3-cmn-test.h"
#include <iprt/asm-amd64-x86.h>


/**
 * Equivalent to RTTestSkippedV.
 */
#undef Bs3TestSkippedV
BS3_CMN_DEF(void, Bs3TestSkippedV,(const char *pszFormat, va_list BS3_FAR va))
{
    if (g_cusBs3TestErrors == g_cusBs3SubTestAtErrors)
    {
        /* Just mark it as skipped and deal with it when the sub-test is done. */
        g_fbBs3SubTestSkipped = true;

        /* Tell VMMDev */
        if (g_fbBs3VMMDevTesting)
#if ARCH_BITS == 16
            ASMOutU16(VMMDEV_TESTING_IOPORT_CMD, (uint16_t)VMMDEV_TESTING_CMD_SKIPPED);
#else
            ASMOutU32(VMMDEV_TESTING_IOPORT_CMD, VMMDEV_TESTING_CMD_SKIPPED);
#endif

        /* The reason why it was skipped is optional. */
        if (pszFormat)
        {
            BS3TESTFAILEDBUF Buf;
            Buf.fNewLine = false;
            Buf.cchBuf   = 0;
            Bs3StrFormatV(pszFormat, va, bs3TestFailedStrOutput, &Buf);
        }
        else if (g_fbBs3VMMDevTesting)
            ASMOutU8(VMMDEV_TESTING_IOPORT_DATA, 0);
    }
}


/**
 * Equivalent to RTTestSkipped.
 */
#undef Bs3TestSkippedF
BS3_CMN_DEF(void, Bs3TestSkippedF,(const char *pszFormat, ...))
{
    va_list va;
    va_start(va, pszFormat);
    BS3_CMN_NM(Bs3TestSkippedV)(pszFormat, va);
    va_end(va);
}


/**
 * Equivalent to RTTestSkipped.
 */
#undef Bs3TestSkipped
BS3_CMN_DEF(void, Bs3TestSkipped,(const char *pszWhy))
{
    BS3_CMN_NM(Bs3TestSkippedF)(pszWhy ? "%s" : NULL, pszWhy);
}

