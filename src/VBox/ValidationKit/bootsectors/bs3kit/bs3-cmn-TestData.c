/* $Id: bs3-cmn-TestData.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Test Data.
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


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test name. */
const char BS3_FAR *BS3_CMN_NM(g_pszBs3Test) = NULL;

#if ARCH_BITS == 16

/** Indicates whether the VMMDev is operational. */
bool        g_fbBs3VMMDevTesting = true;

/** Alignment padding.    */
bool        g_fTestDataPadding0 = true;

/** The number of tests that have failed. */
uint16_t    g_cusBs3TestErrors = 0;

/** The start error count of the current sub-sub-test. */
uint16_t    g_cusBs3SubSubTestAtErrors = 0;
/** Whether we've reported the sub-sub-test result or not. */
bool        g_fbBs3SubSubTestReported = true;
/** Whether the sub-sub-test has been skipped or not. */
bool        g_fbBs3SubSubTestSkipped = false;
/** The number of sub-sub-tests. */
uint16_t    g_cusBs3SubSubTests = 0;
/** The number of sub-sub-tests that failed. */
uint16_t    g_cusBs3SubSubTestsFailed = 0;

/** The start error count of the current subtest. */
uint16_t    g_cusBs3SubTestAtErrors = 0;
/** Whether we've reported the sub-test result or not. */
bool        g_fbBs3SubTestReported = true;
/** Whether the sub-test has been skipped or not. */
bool        g_fbBs3SubTestSkipped = false;
/** The number of sub-tests. */
uint16_t    g_cusBs3SubTests = 0;
/** The number of sub-tests that failed. */
uint16_t    g_cusBs3SubTestsFailed = 0;

/** The current test step. */
uint16_t    g_usBs3TestStep;

/** The host configurable BS3_THRESHOLD_NATIVE_RECOMPILER value. */
uint16_t    g_cBs3ThresholdNativeRecompiler = BS3_THRESHOLD_NATIVE_RECOMPILER;


/** VMMDEV_TESTING_UNIT_XXX -> string */
char const  g_aszBs3TestUnitNames[][12] =
{
    "inv",
    "%",
    "bytes",
    "bytes/s",
    "KB",
    "KB/s",
    "MB",
    "MB/s",
    "packets",
    "packets/s",
    "frames",
    "frames/",
    "occ",
    "occ/s",
    "rndtrp",
    "calls",
    "calls/s",
    "s",
    "ms",
    "ns",
    "ns/call",
    "ns/frame",
    "ns/occ",
    "ns/packet",
    "ns/rndtrp",
    "ins",
    "ins/s",
    "", /* none */
    "pp1k",
    "pp10k",
    "ppm",
    "ppb",
    "ticks",
    "ticks/call",
    "ticks/occ",
    "pages",
    "pages/s",
    "ticks/page",
    "ns/page",
    "ps",
    "ps/call",
    "ps/frame",
    "ps/occ",
    "ps/packet",
    "ps/rndtrp",
    "ps/page",
};


/** The subtest name. */
char        g_szBs3SubTest[64];
/** The subsubtest name. */
char        g_szBs3SubSubTest[64];

#endif /* ARCH_BITS == 16 */

