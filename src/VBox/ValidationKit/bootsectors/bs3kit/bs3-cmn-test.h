/* $Id: bs3-cmn-test.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3Test internal header.
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

#ifndef BS3KIT_INCLUDED_bs3_cmn_test_h
#define BS3KIT_INCLUDED_bs3_cmn_test_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "bs3kit.h"
#include <VBox/VMMDevTesting.h>


/** The test name. */
extern const char BS3_FAR  *g_pszBs3Test_c16;
extern const char          *g_pszBs3Test_c32;
extern const char          *g_pszBs3Test_c64;

/** Indicates whether the VMMDev is operational. */
#ifndef DOXYGEN_RUNNING
# define g_fbBs3VMMDevTesting BS3_DATA_NM(g_fbBs3VMMDevTesting)
#endif
extern bool                 g_fbBs3VMMDevTesting;

/** The number of tests that have failed. */
#ifndef DOXYGEN_RUNNING
# define g_cusBs3TestErrors BS3_DATA_NM(g_cusBs3TestErrors)
#endif
extern uint16_t             g_cusBs3TestErrors;


/** The subtest name. */
#ifndef DOXYGEN_RUNNING
# define g_szBs3SubTest     BS3_DATA_NM(g_szBs3SubTest)
#endif
extern char                 g_szBs3SubTest[64];

/** The start error count of the current subtest. */
#ifndef DOXYGEN_RUNNING
# define g_cusBs3SubTestAtErrors BS3_DATA_NM(g_cusBs3SubTestAtErrors)
#endif
extern uint16_t             g_cusBs3SubTestAtErrors;

/**  Whether we've reported the sub-test result or not. */
#ifndef DOXYGEN_RUNNING
# define g_fbBs3SubTestReported BS3_DATA_NM(g_fbBs3SubTestReported)
#endif
extern bool                 g_fbBs3SubTestReported;
/** Whether the sub-test has been skipped or not. */
#ifndef DOXYGEN_RUNNING
# define g_fbBs3SubTestSkipped BS3_DATA_NM(g_fbBs3SubTestSkipped)
#endif
extern bool                 g_fbBs3SubTestSkipped;

/** The number of sub tests. */
#ifndef DOXYGEN_RUNNING
# define g_cusBs3SubTests   BS3_DATA_NM(g_cusBs3SubTests)
#endif
extern uint16_t             g_cusBs3SubTests;

/** The number of sub tests that failed. */
#ifndef DOXYGEN_RUNNING
# define g_cusBs3SubTestsFailed BS3_DATA_NM(g_cusBs3SubTestsFailed)
#endif
extern uint16_t             g_cusBs3SubTestsFailed;


/** The subsubtest name. */
#ifndef DOXYGEN_RUNNING
# define g_szBs3SubSubTest  BS3_DATA_NM(g_szBs3SubSubTest)
#endif
extern char                 g_szBs3SubSubTest[64];

/** The start error count of the current sub-sub-test. */
#ifndef DOXYGEN_RUNNING
# define g_cusBs3SubSubTestAtErrors BS3_DATA_NM(g_cusBs3SubSubTestAtErrors)
#endif
extern uint16_t             g_cusBs3SubSubTestAtErrors;

/**  Whether we've reported the sub-sub-test result or not. */
#ifndef DOXYGEN_RUNNING
# define g_fbBs3SubSubTestReported BS3_DATA_NM(g_fbBs3SubSubTestReported)
#endif
extern bool                 g_fbBs3SubSubTestReported;
/** Whether the sub-sub-test has been skipped or not. */
#ifndef DOXYGEN_RUNNING
# define g_fbBs3SubSubTestSkipped BS3_DATA_NM(g_fbBs3SubSubTestSkipped)
#endif
extern bool                 g_fbBs3SubSubTestSkipped;

/** The number of sub-sub-tests. */
#ifndef DOXYGEN_RUNNING
# define g_cusBs3SubSubTests   BS3_DATA_NM(g_cusBs3SubSubTests)
#endif
extern uint16_t             g_cusBs3SubSubTests;

/** The number of sub-sub-tests that failed. */
#ifndef DOXYGEN_RUNNING
# define g_cusBs3SubSubTestsFailed BS3_DATA_NM(g_cusBs3SubSubTestsFailed)
#endif
extern uint16_t             g_cusBs3SubSubTestsFailed;


/** VMMDEV_TESTING_UNIT_XXX -> string */
#ifndef DOXYGEN_RUNNING
# define g_aszBs3TestUnitNames BS3_DATA_NM(g_aszBs3TestUnitNames)
#endif
extern char const           g_aszBs3TestUnitNames[][12];



/**
 * Sends a command to VMMDev followed by a single string.
 *
 * If the VMMDev is not present or is not being used, this function will
 * do nothing.
 *
 * @param   uCmd        The command.
 * @param   pszString   The string.
 */
#ifndef DOXYGEN_RUNNING
# define bs3TestSendCmdWithStr BS3_CMN_NM(bs3TestSendCmdWithStr)
#endif
BS3_DECL(void) bs3TestSendCmdWithStr(uint32_t uCmd, const char BS3_FAR *pszString);

/**
 * Sends a command to VMMDev followed by a 32-bit unsigned integer value.
 *
 * If the VMMDev is not present or is not being used, this function will
 * do nothing.
 *
 * @param   uCmd        The command.
 * @param   uValue      The value.
 */
#ifndef DOXYGEN_RUNNING
# define bs3TestSendCmdWithU32 BS3_CMN_NM(bs3TestSendCmdWithU32)
#endif
BS3_DECL(void) bs3TestSendCmdWithU32(uint32_t uCmd, uint32_t uValue);

/**
 * Checks if the VMMDev is configured for testing.
 *
 * @returns true / false.
 */
#ifndef DOXYGEN_RUNNING
# define bs3TestIsVmmDevTestingPresent BS3_CMN_NM(bs3TestIsVmmDevTestingPresent)
#endif
BS3_DECL(bool) bs3TestIsVmmDevTestingPresent(void);

/**
 * Similar to rtTestSubSubCleanup.
 */
#ifndef DOXYGEN_RUNNING
# define bs3TestSubSubCleanup BS3_CMN_NM(bs3TestSubSubCleanup)
#endif
BS3_DECL(void) bs3TestSubSubCleanup(void);

/**
 * Similar to rtTestSubCleanup.
 */
#ifndef DOXYGEN_RUNNING
# define bs3TestSubCleanup BS3_CMN_NM(bs3TestSubCleanup)
#endif
BS3_DECL(void) bs3TestSubCleanup(void);

/**
 * @callback_method_impl{FNBS3STRFORMATOUTPUT,
 *      Used by Bs3TestFailedV and Bs3TestSkippedV.
 *
 *      The @a pvUser parameter must point a BS3TESTFAILEDBUF structure. }
 */
#ifndef DOXYGEN_RUNNING
# define bs3TestFailedStrOutput BS3_CMN_NM(bs3TestFailedStrOutput)
#endif
BS3_DECL_CALLBACK(size_t) bs3TestFailedStrOutput(char ch, void BS3_FAR *pvUser);

/**
 * Output buffering for bs3TestFailedStrOutput.
 */
typedef struct BS3TESTFAILEDBUF
{
    /** Initialize to false. */
    bool    fNewLine;
    /** Initialize to zero. */
    uint8_t cchBuf;
    /** Buffer, uninitialized. */
    char    achBuf[128];
} BS3TESTFAILEDBUF;
/** Pointer to a bs3TestFailedStrOutput buffer.  */
typedef BS3TESTFAILEDBUF BS3_FAR *PBS3TESTFAILEDBUF;

#endif /* !BS3KIT_INCLUDED_bs3_cmn_test_h */

