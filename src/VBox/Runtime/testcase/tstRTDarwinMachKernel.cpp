/* $Id: tstRTDarwinMachKernel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - mach_kernel symbol resolving hack.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/dbg.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
const char *g_pszTestKernel = "/no-such-file";


static void dotest(void)
{
    RTDBGKRNLINFO hKrnlInfo;
    RTTESTI_CHECK_RC_RETV(RTR0DbgKrnlInfoOpen(&hKrnlInfo, 0), VINF_SUCCESS);
    static const char * const s_apszSyms[] =
    {
        "ast_pending",
        "cpu_interrupt",
        "dtrace_register",
        "dtrace_suspend",
        "kext_alloc",
        "kext_free",
        "vm_map_protect"
    };
    for (unsigned i = 0; i < RT_ELEMENTS(s_apszSyms); i++)
    {
        void *pvValue = NULL;
        int rc = RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, s_apszSyms[i], &pvValue);
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%Rrc %p %s\n", rc, pvValue, s_apszSyms[i]);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            RTTESTI_CHECK_RC(RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, s_apszSyms[i], NULL), VINF_SUCCESS);
    }

    RTTESTI_CHECK_RC(RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "no_such_symbol_name_really", NULL), VERR_SYMBOL_NOT_FOUND);
    RTTESTI_CHECK(RTR0DbgKrnlInfoRelease(hKrnlInfo) == 0);
    RTTESTI_CHECK(RTR0DbgKrnlInfoRelease(NIL_RTDBGKRNLINFO) == 0);
}


int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTDarwinMachKernel", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /* Optional kernel path as argument. */
    if (argc == 2)
        g_pszTestKernel = argv[1];
#ifndef RT_OS_DARWIN
    else
        return RTTestSkipAndDestroy(hTest, "not on darwin");
#endif

    dotest();

    return RTTestSummaryAndDestroy(hTest);
}

