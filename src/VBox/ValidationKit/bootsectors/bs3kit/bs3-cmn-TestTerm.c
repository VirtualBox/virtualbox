/* $Id: bs3-cmn-TestTerm.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3TestTerm
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


static void bs3TestSubCleanupWorker(char const BS3_FAR *pszName, bool fSkipped, uint16_t cErrorsAtStart, uint32_t uDoneCmd,
                                    char const BS3_FAR *pszParent)
{
    uint16_t const cErrors = g_cusBs3TestErrors - cErrorsAtStart;
    size_t         cch     = Bs3StrLen(pszName);

    /* Tell VMMDev. */
    bs3TestSendCmdWithU32(uDoneCmd, cErrors);

    /* Print result to the console. */
    if (pszParent)
    {
        Bs3PrintStr(pszParent);
        cch += Bs3StrLen(pszParent) + 3;
        Bs3PrintStr(" / ");
    }
    Bs3PrintStr(pszName);
    Bs3PrintChr(':');
    do
        Bs3PrintChr(' ');
    while (cch++ < 49);

    if (!cErrors)
        Bs3PrintStr(!fSkipped ? "PASSED\n" : "SKIPPED\n");
    else
    {
        if (uDoneCmd == VMMDEV_TESTING_CMD_SUB_DONE)
            g_cusBs3SubTestsFailed++;
        else
            g_cusBs3SubSubTestsFailed++;
        Bs3Printf("FAILED (%u errors)\n", cErrors);
    }
}


/**
 * Cleans up the current sub-sub-test.
 */
BS3_DECL(void) bs3TestSubSubCleanup(void)
{
    if (g_szBs3SubSubTest[0] != '\0')
    {
        if (!g_fbBs3SubSubTestReported)
            bs3TestSubCleanupWorker(g_szBs3SubSubTest, g_fbBs3SubSubTestSkipped, g_cusBs3SubSubTestAtErrors,
                                    VMMDEV_TESTING_CMD_SUBSUB_DONE, g_szBs3SubTest);

        /* Reset the sub-sub-test. */
        g_fbBs3SubSubTestReported = true;
        g_fbBs3SubSubTestSkipped  = false;
        g_szBs3SubSubTest[0]      = '\0';
    }
}


/**
 * Equivalent to rtTestSubCleanup + rtTestSubTestReport.
 */
BS3_DECL(void) bs3TestSubCleanup(void)
{
    bs3TestSubSubCleanup();
    if (g_szBs3SubTest[0] != '\0')
    {
        if (!g_fbBs3SubTestReported)
            bs3TestSubCleanupWorker(g_szBs3SubTest, g_fbBs3SubTestSkipped, g_cusBs3SubTestAtErrors,
                                    VMMDEV_TESTING_CMD_SUB_DONE, NULL);

        /* Reset the sub-test. */
        g_fbBs3SubTestReported = true;
        g_fbBs3SubTestSkipped  = false;
        g_szBs3SubTest[0]      = '\0';
    }
}


/**
 * Equivalent to RTTestSummaryAndDestroy.
 */
#undef Bs3TestTerm
BS3_CMN_DEF(void, Bs3TestTerm,(void))
{
    /*
     * Close any current sub-test.
     */
    bs3TestSubCleanup();

    /*
     * Report summary.
     */
    if (BS3_CMN_NM(g_pszBs3Test))
    {
        Bs3PrintStr(BS3_CMN_NM(g_pszBs3Test));
        if (g_cusBs3TestErrors == 0)
            Bs3Printf(": SUCCESS (%u tests)\n", g_cusBs3SubTests);
        else
            Bs3Printf(": FAILURE - %u (%u of %u tests)\n",
                      g_cusBs3TestErrors, g_cusBs3SubTestsFailed, g_cusBs3SubTests);
    }

    /*
     * Tell VMMDev.
     */
    bs3TestSendCmdWithU32(VMMDEV_TESTING_CMD_TERM, g_cusBs3TestErrors);

    BS3_CMN_NM(g_pszBs3Test) = NULL;
}

