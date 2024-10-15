/* $Id: CppUnitEmulation.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Simple cppunit emulation
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VBOX_INCLUDED_SRC_Network_testcase_CppUnitEmulation_h
#define VBOX_INCLUDED_SRC_Network_testcase_CppUnitEmulation_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/test.h>

#define CPPUNIT_TEST_SUITE(a_Name) \
public: \
    RTEXITCODE run(void) \
    { \
        RTTEST hTest = NIL_RTTEST; \
        RTEXITCODE rcExit = RTTestInitAndCreate(#a_Name, &hTest); \
        if (rcExit == RTEXITCODE_SUCCESS) \
        { \
            RTTestBanner(hTest)

#define CPPUNIT_TEST(a_MethodName) \
            RTTestISub(#a_MethodName); \
            setUp(); \
            a_MethodName(); \
            tearDown()

#define CPPUNIT_TEST_SUITE_END() \
            rcExit = RTTestSummaryAndDestroy(hTest); \
        } \
        return rcExit; \
    } \
    typedef int dummy_end

#define CPPUNIT_FAIL(a_Msg)                         RTTestIFailed(a_Msg)
#if __cplusplus+0 < 201100
# define CPPUNIT_ASSERT_EQUAL(a_Value1, a_Value2)   RTTESTI_CHECK((a_Value1) == (a_Value2))
#else
# define CPPUNIT_ASSERT_EQUAL(a_Value1, a_Value2) do { \
        auto const Val1 = (a_Value1); \
        auto const Val2 = (a_Value2); \
        if (Val1 != Val2) \
            RTTestIFailed("%s (%#llx) != %s (%#llx)", #a_Value1, (uint64_t)Val1, #a_Value2, (uint64_t)Val2); \
    } while (0)
#endif

#endif /* !VBOX_INCLUDED_SRC_Network_testcase_CppUnitEmulation_h */
