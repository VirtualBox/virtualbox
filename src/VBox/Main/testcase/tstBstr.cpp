/* $Id: tstBstr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * API Glue Testcase - Bstr.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/com/string.h>

#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/uni.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define CHECK_BSTR(a_Expr, a_bstr, a_szExpected) \
        do { \
            a_Expr; \
            size_t cchExpect = RTStrCalcUtf16Len(a_szExpected); \
            if ((a_bstr).length() != cchExpect) \
                RTTestFailed(hTest, "line %u: length() -> %zu, expected %zu (%ls vs %s)", \
                             __LINE__, (a_bstr).length(), cchExpect, (a_bstr).raw(), a_szExpected); \
            else \
            { \
                int iDiff = (a_bstr).compareUtf8(a_szExpected); \
                if (iDiff) \
                    RTTestFailed(hTest, "line %u: compareUtf8() -> %d: %ls vs %s", \
                                 __LINE__, iDiff, (a_bstr).raw(), a_szExpected); \
            } \
        } while (0)



static void testBstrPrintf(RTTEST hTest)
{
    RTTestSub(hTest, "Bstr::printf");

    com::Bstr bstr1;
    CHECK_BSTR(bstr1.printf(""), bstr1, "");
    CHECK_BSTR(bstr1.printf("1234098694"), bstr1, "1234098694");
    CHECK_BSTR(bstr1.printf("%u-%u-%u-%s-%u", 42, 999, 42, "asdkfhjasldfk0", 42),
               bstr1, "42-999-42-asdkfhjasldfk0-42");

    com::Bstr bstr2;
    CHECK_BSTR(bstr2.printf("%ls-%ls-%ls-%ls-%ls-%ls-%ls-%ls-%ls-%ls::%ls-%ls-%ls-%ls-%ls-%ls-%ls-%ls-%ls-%ls::%ls-%ls-%ls-%ls-%ls-%ls-%ls-%ls-%ls-%ls",
                            bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(),
                            bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(),
                            bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw(), bstr1.raw()),
               bstr2, "42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42::42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42::42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42");
    CHECK_BSTR(bstr2.appendPrintf("-9999998888888777776666655554443322110!"),
               bstr2, "42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42::42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42::42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-9999998888888777776666655554443322110!");
    CHECK_BSTR(bstr2.appendPrintf("!"),
               bstr2, "42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42::42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42::42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-42-999-42-asdkfhjasldfk0-42-9999998888888777776666655554443322110!!");
}

static void testBstrAppend(RTTEST hTest)
{
    RTTestSub(hTest, "Bstr::append");

    /* C-string source: */
    com::Bstr bstr1;
    CHECK_BSTR(bstr1.append("1234"), bstr1, "1234");
    CHECK_BSTR(bstr1.append("56"), bstr1, "123456");
    CHECK_BSTR(bstr1.append("7"), bstr1, "1234567");
    CHECK_BSTR(bstr1.append("89abcdefghijklmnopqrstuvwxyz"), bstr1, "123456789abcdefghijklmnopqrstuvwxyz");
    CHECK_BSTR(bstr1.append("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), bstr1, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    CHECK_BSTR(bstr1.append("123456789abcdefghijklmnopqrstuvwxyz"), bstr1, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz");
    CHECK_BSTR(bstr1.append("_"), bstr1, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");
    CHECK_BSTR(bstr1.append(""), bstr1, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");

    com::Bstr bstr2;
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(""), VINF_SUCCESS), bstr2, "");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow("1234"), VINF_SUCCESS), bstr2, "1234");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow("56"), VINF_SUCCESS), bstr2, "123456");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow("7"), VINF_SUCCESS), bstr2, "1234567");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow("89abcdefghijklmnopqrstuvwxyz"), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyz");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow("123456789abcdefghijklmnopqrstuvwxyz"), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow("_"), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(""), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");

    /* Bstr source:  */
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(bstr1), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr1.appendNoThrow(bstr2), VINF_SUCCESS), bstr1, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");

    com::Bstr const bstrWord1("word");
    CHECK_BSTR(bstr1.setNull(), bstr1, "");
    CHECK_BSTR(bstr1.append(bstr2, 5, 10), bstr1, "6789abcdef");
    CHECK_BSTR(bstr1.append(bstr2, 4096, 10), bstr1, "6789abcdef");
    CHECK_BSTR(bstr1.append(bstrWord1, 1), bstr1, "6789abcdeford");
    CHECK_BSTR(bstr1.append(bstrWord1, 1,1), bstr1, "6789abcdefordo");
    CHECK_BSTR(bstr1.append(bstrWord1, 1,2), bstr1, "6789abcdefordoor");
    CHECK_BSTR(bstr1.append(bstrWord1, 1,3), bstr1, "6789abcdefordoorord");
    CHECK_BSTR(bstr1.append(bstrWord1, 1,4), bstr1, "6789abcdefordoorordord");
    CHECK_BSTR(bstr1.append(bstrWord1, 3,1), bstr1, "6789abcdefordoorordordd");
    CHECK_BSTR(bstr1.append(bstrWord1, 3,2), bstr1, "6789abcdefordoorordorddd");
    CHECK_BSTR(bstr1.append(bstrWord1, 3),   bstr1, "6789abcdefordoorordordddd");

    com::Bstr bstr3;
    CHECK_BSTR(bstr3.setNull(), bstr3, "");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstr2, 5, 10),   VINF_SUCCESS), bstr3, "6789abcdef");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstr2, 4096, 10),VINF_SUCCESS), bstr3, "6789abcdef");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord1, 1),   VINF_SUCCESS), bstr3, "6789abcdeford");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord1, 1,1), VINF_SUCCESS), bstr3, "6789abcdefordo");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord1, 1,2), VINF_SUCCESS), bstr3, "6789abcdefordoor");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord1, 1,3), VINF_SUCCESS), bstr3, "6789abcdefordoorord");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord1, 1,4), VINF_SUCCESS), bstr3, "6789abcdefordoorordord");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord1, 3,1), VINF_SUCCESS), bstr3, "6789abcdefordoorordordd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord1, 3,2), VINF_SUCCESS), bstr3, "6789abcdefordoorordorddd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord1, 3),   VINF_SUCCESS), bstr3, "6789abcdefordoorordordddd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord1, 3),   VINF_SUCCESS), bstr3, "6789abcdefordoorordorddddd");
    com::Bstr const bstrWord2("-SEP-");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstrWord2, 0),   VINF_SUCCESS), bstr3, "6789abcdefordoorordorddddd-SEP-");

    /* CBSTR source:  */
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(bstr1.raw()), VINF_SUCCESS), bstr3, "6789abcdefordoorordorddddd-SEP-6789abcdefordoorordordddd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr1.appendNoThrow(bstr3.raw()), VINF_SUCCESS), bstr1, "6789abcdefordoorordordddd6789abcdefordoorordorddddd-SEP-6789abcdefordoorordordddd");

    CBSTR const pwsz2 = bstr2.raw();
    CBSTR const pwszWord1 = bstrWord1.raw();
    CHECK_BSTR(bstr1.setNull(), bstr1, "");
    CHECK_BSTR(bstr1.append(pwsz2 + 5, 10),    bstr1, "6789abcdef");
    CHECK_BSTR(bstr1.append(pwszWord1 + 1),    bstr1, "6789abcdeford");
    CHECK_BSTR(bstr1.append(pwszWord1 + 1, 1), bstr1, "6789abcdefordo");
    CHECK_BSTR(bstr1.append(pwszWord1 + 1, 2), bstr1, "6789abcdefordoor");
    CHECK_BSTR(bstr1.append(pwszWord1 + 1, 3), bstr1, "6789abcdefordoorord");
    CHECK_BSTR(bstr1.append(pwszWord1 + 1, 4), bstr1, "6789abcdefordoorordord");
    CHECK_BSTR(bstr1.append(pwszWord1 + 3, 1), bstr1, "6789abcdefordoorordordd");
    CHECK_BSTR(bstr1.append(pwszWord1 + 3, 2), bstr1, "6789abcdefordoorordorddd");
    CHECK_BSTR(bstr1.append(pwszWord1 + 3),    bstr1, "6789abcdefordoorordordddd");

    CHECK_BSTR(bstr3.setNull(), bstr3, "");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwsz2 + 5, 10),    VINF_SUCCESS), bstr3, "6789abcdef");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwszWord1 + 1),    VINF_SUCCESS), bstr3, "6789abcdeford");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwszWord1 + 1, 1), VINF_SUCCESS), bstr3, "6789abcdefordo");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwszWord1 + 1, 2), VINF_SUCCESS), bstr3, "6789abcdefordoor");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwszWord1 + 1, 3), VINF_SUCCESS), bstr3, "6789abcdefordoorord");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwszWord1 + 1, 4), VINF_SUCCESS), bstr3, "6789abcdefordoorordord");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwszWord1 + 3, 1), VINF_SUCCESS), bstr3, "6789abcdefordoorordordd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwszWord1 + 3, 2), VINF_SUCCESS), bstr3, "6789abcdefordoorordorddd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwszWord1 + 3),    VINF_SUCCESS), bstr3, "6789abcdefordoorordordddd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(pwszWord1 + 3),    VINF_SUCCESS), bstr3, "6789abcdefordoorordorddddd");

    /* RTCString source: */
    bstr1.setNull();
    CHECK_BSTR(bstr1.append(RTCString("1234")), bstr1, "1234");
    CHECK_BSTR(bstr1.append(RTCString("56")), bstr1, "123456");
    CHECK_BSTR(bstr1.append(RTCString("7")), bstr1, "1234567");
    CHECK_BSTR(bstr1.append(RTCString("89abcdefghijklmnopqrstuvwxyz")), bstr1, "123456789abcdefghijklmnopqrstuvwxyz");
    CHECK_BSTR(bstr1.append(RTCString("ABCDEFGHIJKLMNOPQRSTUVWXYZ")), bstr1, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    CHECK_BSTR(bstr1.append(RTCString("123456789abcdefghijklmnopqrstuvwxyz")), bstr1, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz");
    CHECK_BSTR(bstr1.append(RTCString("_")), bstr1, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");
    CHECK_BSTR(bstr1.append(RTCString()), bstr1, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");

    bstr2.setNull();
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(RTCString("")), VINF_SUCCESS), bstr2, "");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(RTCString("1234")), VINF_SUCCESS), bstr2, "1234");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(RTCString("56")), VINF_SUCCESS), bstr2, "123456");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(RTCString("7")), VINF_SUCCESS), bstr2, "1234567");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(RTCString("89abcdefghijklmnopqrstuvwxyz")), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyz");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(RTCString("ABCDEFGHIJKLMNOPQRSTUVWXYZ")), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(RTCString("123456789abcdefghijklmnopqrstuvwxyz")), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(RTCString("_")), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr2.appendNoThrow(RTCString("")), VINF_SUCCESS), bstr2, "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789abcdefghijklmnopqrstuvwxyz_");

    RTCString const strWord1 = "word";
    CHECK_BSTR(bstr1.setNull(), bstr1, "");
    CHECK_BSTR(bstr1.append(com::Utf8Str(bstr2), 5, 10), bstr1, "6789abcdef");
    CHECK_BSTR(bstr1.append(com::Utf8Str(bstr2), 4096, 10), bstr1, "6789abcdef");
    CHECK_BSTR(bstr1.append(strWord1, 1),   bstr1, "6789abcdeford");
    CHECK_BSTR(bstr1.append(strWord1, 1,1), bstr1, "6789abcdefordo");
    CHECK_BSTR(bstr1.append(strWord1, 1,2), bstr1, "6789abcdefordoor");
    CHECK_BSTR(bstr1.append(strWord1, 1,3), bstr1, "6789abcdefordoorord");
    CHECK_BSTR(bstr1.append(strWord1, 1,4), bstr1, "6789abcdefordoorordord");
    CHECK_BSTR(bstr1.append(strWord1, 3,1), bstr1, "6789abcdefordoorordordd");
    CHECK_BSTR(bstr1.append(strWord1, 3,2), bstr1, "6789abcdefordoorordorddd");
    CHECK_BSTR(bstr1.append(strWord1, 3),   bstr1, "6789abcdefordoorordordddd");

    CHECK_BSTR(bstr3.setNull(), bstr3, "");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(com::Utf8Str(bstr2), 5, 10),   VINF_SUCCESS), bstr3, "6789abcdef");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(com::Utf8Str(bstr2), 4096, 10),VINF_SUCCESS), bstr3, "6789abcdef");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(strWord1, 1),   VINF_SUCCESS), bstr3, "6789abcdeford");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(strWord1, 1,1), VINF_SUCCESS), bstr3, "6789abcdefordo");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(strWord1, 1,2), VINF_SUCCESS), bstr3, "6789abcdefordoor");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(strWord1, 1,3), VINF_SUCCESS), bstr3, "6789abcdefordoorord");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(strWord1, 1,4), VINF_SUCCESS), bstr3, "6789abcdefordoorordord");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(strWord1, 3,1), VINF_SUCCESS), bstr3, "6789abcdefordoorordordd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(strWord1, 3,2), VINF_SUCCESS), bstr3, "6789abcdefordoorordorddd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(strWord1, 3),   VINF_SUCCESS), bstr3, "6789abcdefordoorordordddd");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow(strWord1, 3),   VINF_SUCCESS), bstr3, "6789abcdefordoorordorddddd");

    /* char: */
    CHECK_BSTR(bstr1.setNull(), bstr1, "");
    CHECK_BSTR(bstr1.append('-'), bstr1, "-");
    CHECK_BSTR(bstr1.append('a'), bstr1, "-a");
    CHECK_BSTR(bstr1.append('b'), bstr1, "-ab");
    CHECK_BSTR(bstr1.append('Z'), bstr1, "-abZ");
    CHECK_BSTR(bstr1.append('-'), bstr1, "-abZ-");

    CHECK_BSTR(bstr3.setNull(), bstr3, "");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow('-'), VINF_SUCCESS), bstr3, "-");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow('a'), VINF_SUCCESS), bstr3, "-a");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow('b'), VINF_SUCCESS), bstr3, "-ab");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow('Z'), VINF_SUCCESS), bstr3, "-abZ");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendNoThrow('-'), VINF_SUCCESS), bstr3, "-abZ-");

    /* unicode codepoint: */
    CHECK_BSTR(bstr1.setNull(), bstr1, "");
    CHECK_BSTR(bstr1.appendCodePoint('-'),    bstr1, "-");
    CHECK_BSTR(bstr1.appendCodePoint('a'),    bstr1, "-a");
    CHECK_BSTR(bstr1.appendCodePoint('b'),    bstr1, "-ab");
    CHECK_BSTR(bstr1.appendCodePoint('Z'),    bstr1, "-abZ");
    CHECK_BSTR(bstr1.appendCodePoint('-'),    bstr1, "-abZ-");
    CHECK_BSTR(bstr1.appendCodePoint(0x39f),  bstr1, "-abZ-\xce\x9f");
    CHECK_BSTR(bstr1.appendCodePoint(0x1f50), bstr1, "-abZ-\xce\x9f\xe1\xbd\x90");
    CHECK_BSTR(bstr1.appendCodePoint(0x3c7),  bstr1, "-abZ-\xce\x9f\xe1\xbd\x90\xcf\x87");
    CHECK_BSTR(bstr1.appendCodePoint(0x1f76), bstr1, "-abZ-\xce\x9f\xe1\xbd\x90\xcf\x87\xe1\xbd\xb6");

    CHECK_BSTR(bstr3.setNull(), bstr3, "");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendCodePointNoThrow('-'),    VINF_SUCCESS), bstr3, "-");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendCodePointNoThrow('a'),    VINF_SUCCESS), bstr3, "-a");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendCodePointNoThrow('b'),    VINF_SUCCESS), bstr3, "-ab");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendCodePointNoThrow('Z'),    VINF_SUCCESS), bstr3, "-abZ");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendCodePointNoThrow('-'),    VINF_SUCCESS), bstr3, "-abZ-");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendCodePointNoThrow(0x39f),  VINF_SUCCESS), bstr3, "-abZ-\xce\x9f");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendCodePointNoThrow(0x1f50), VINF_SUCCESS), bstr3, "-abZ-\xce\x9f\xe1\xbd\x90");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendCodePointNoThrow(0x3c7),  VINF_SUCCESS), bstr3, "-abZ-\xce\x9f\xe1\xbd\x90\xcf\x87");
    CHECK_BSTR(RTTESTI_CHECK_RC(bstr3.appendCodePointNoThrow(0x1f76), VINF_SUCCESS), bstr3, "-abZ-\xce\x9f\xe1\xbd\x90\xcf\x87\xe1\xbd\xb6");
}


static void testBstrErase(RTTEST hTest)
{
    RTTestSub(hTest, "Bstr::erase");

    com::Bstr bstr1;
    CHECK_BSTR(bstr1.erase(), bstr1, "");
    CHECK_BSTR(bstr1.erase(99), bstr1, "");
    CHECK_BSTR(bstr1.erase(99,999), bstr1, "");

    CHECK_BSTR(bstr1 = "asdlfjhasldfjhaldfhjalhjsdf", bstr1, "asdlfjhasldfjhaldfhjalhjsdf");
    CHECK_BSTR(bstr1.erase(8, 9),   bstr1, "asdlfjhafhjalhjsdf");
    CHECK_BSTR(bstr1.erase(17, 20), bstr1, "asdlfjhafhjalhjsd");
    CHECK_BSTR(bstr1.erase(16, 1),  bstr1, "asdlfjhafhjalhjs");
    CHECK_BSTR(bstr1.erase(15, 0),  bstr1, "asdlfjhafhjalhjs");
    CHECK_BSTR(bstr1.erase(13, 3),  bstr1, "asdlfjhafhjal");
    CHECK_BSTR(bstr1.erase(3, 3),   bstr1, "asdhafhjal");
    CHECK_BSTR(bstr1.erase(),       bstr1, "");
}


int main()
{
    RTTEST      hTest;
    RTEXITCODE  rcExit = RTTestInitAndCreate("tstBstr", &hTest);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        RTTestBanner(hTest);

        testBstrPrintf(hTest);
        testBstrAppend(hTest);
        testBstrErase(hTest);

        rcExit = RTTestSummaryAndDestroy(hTest);
    }
    return rcExit;
}

