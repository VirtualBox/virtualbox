/* $Id: tstIprtMiniString.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTCString.
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
#include <iprt/cpp/ministring.h>

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/uni.h>


static void test1Hlp1(const char *pszExpect, const char *pszFormat, ...)
{
#if 0
    va_list va;
    va_start(va, pszFormat);
    RTCString strTst(pszFormat, va);
    va_end(va);
    RTTESTI_CHECK_MSG(strTst.equals(pszExpect),  ("strTst='%s' expected='%s'\n",  strTst.c_str(), pszExpect));
#else
    RT_NOREF_PV(pszExpect);
    RT_NOREF_PV(pszFormat);
#endif
}

static void test1(RTTEST hTest)
{
    RTTestSub(hTest, "Basics");

#define CHECK(expr) RTTESTI_CHECK(expr)
#define CHECK_DUMP(expr, value) \
    do { \
        if (!(expr)) \
            RTTestFailed(hTest, "%d: FAILED %s, got \"%s\"", __LINE__, #expr, value); \
    } while (0)

#define CHECK_DUMP_I(expr) \
    do { \
        if (!(expr)) \
            RTTestFailed(hTest, "%d: FAILED %s, got \"%d\"", __LINE__, #expr, expr); \
    } while (0)
#define CHECK_EQUAL(Str, szExpect) \
    do { \
        if (!(Str).equals(szExpect)) \
            RTTestIFailed("line %u: expected \"%s\" got \"%s\"", __LINE__, szExpect, (Str).c_str()); \
    } while (0)
#define CHECK_EQUAL_I(iRes, iExpect) \
    do { \
        if (iRes != iExpect) \
            RTTestIFailed("line %u: expected \"%zd\" got \"%zd\"", __LINE__, iExpect, iRes); \
    } while (0)

    RTCString empty;
    CHECK(empty.length() == 0);
    CHECK(empty.capacity() == 0);

    empty.reserve(1);
    CHECK(empty.length() == 0);
    CHECK(empty.capacity() == 1);
    char *pszEmpty = empty.mutableRaw();
    CHECK(pszEmpty != NULL);

    RTCString sixbytes("12345");
    CHECK(sixbytes.length() == 5);
    CHECK(sixbytes.capacity() == 6);

    sixbytes.append(RTCString("678"));
    CHECK(sixbytes.length() == 8);
    CHECK(sixbytes.capacity() >= 9);

    sixbytes.append("9a");
    CHECK(sixbytes.length() == 10);
    CHECK(sixbytes.capacity() >= 11);

    char *psz = sixbytes.mutableRaw();
        // 123456789a
        //       ^
        // 0123456
    psz[6] = '\0';
    sixbytes.jolt();
    CHECK(sixbytes.length() == 6);
    CHECK(sixbytes.capacity() == 7);

    RTCString morebytes("tobereplaced");
    morebytes = "newstring ";
    morebytes.append(sixbytes);

    CHECK_DUMP(morebytes == "newstring 123456", morebytes.c_str());

    RTCString third(morebytes);
    third.reserve(100 * 1024);      // 100 KB
    CHECK_DUMP(third == "newstring 123456", morebytes.c_str() );
    CHECK(third.capacity() == 100 * 1024);
    CHECK(third.length() == morebytes.length());          // must not have changed

    RTCString copy1(morebytes);
    RTCString copy2 = morebytes;
    CHECK(copy1 == copy2);

    copy1 = NULL;
    CHECK(copy1.length() == 0);

    copy1 = "";
    CHECK(copy1.length() == 0);

    CHECK(RTCString("abc") <  RTCString("def"));
    CHECK(RTCString("") <  RTCString("def"));
    CHECK(RTCString("abc") > RTCString(""));
    CHECK(RTCString("abc") != RTCString("def"));
    CHECK_DUMP_I(RTCString("def") > RTCString("abc"));
    CHECK(RTCString("abc") == RTCString("abc"));
    CHECK(RTCString("").compare("") == 0);
    CHECK(RTCString("").compare(NULL) == 0);
    CHECK(RTCString("").compare("a") < 0);
    CHECK(RTCString("a").compare("") > 0);
    CHECK(RTCString("a").compare(NULL) > 0);

    CHECK(RTCString("abc") <  "def");
    CHECK(RTCString("abc") != "def");
    CHECK_DUMP_I(RTCString("def") > "abc");
    CHECK(RTCString("abc") == "abc");

    CHECK(RTCString("abc").equals("abc"));
    CHECK(!RTCString("abc").equals("def"));
    CHECK(RTCString("abc").equalsIgnoreCase("Abc"));
    CHECK(RTCString("abc").equalsIgnoreCase("ABc"));
    CHECK(RTCString("abc").equalsIgnoreCase("ABC"));
    CHECK(!RTCString("abc").equalsIgnoreCase("dBC"));
    CHECK(RTCString("").equals(""));
    CHECK(RTCString("").equals(NULL));
    CHECK(!RTCString("").equals("a"));
    CHECK(!RTCString("a").equals(""));
    CHECK(!RTCString("a").equals(NULL));
    CHECK(RTCString("").equalsIgnoreCase(""));
    CHECK(RTCString("").equalsIgnoreCase(NULL));
    CHECK(!RTCString("").equalsIgnoreCase("a"));
    CHECK(!RTCString("a").equalsIgnoreCase(""));

    copy2.setNull();
    for (int i = 0; i < 100; ++i)
    {
        copy2.reserve(50);      // should be ignored after 50 loops
        copy2.append("1");
    }
    CHECK(copy2.length() == 100);

    copy2.setNull();
    for (int i = 0; i < 100; ++i)
    {
        copy2.reserve(50);      // should be ignored after 50 loops
        copy2.append('1');
    }
    CHECK(copy2.length() == 100);

    /* printf */
    {
        RTCString StrFmt;
        CHECK(StrFmt.printf("%s-%s-%d", "abc", "def", 42).equals("abc-def-42"));
        test1Hlp1("abc-42-def", "%s-%d-%s", "abc", 42, "def");
        test1Hlp1("", "");
        test1Hlp1("1", "1");
        test1Hlp1("foobar", "%s", "foobar");
    }

    /* substring constructors */
    {
        RTCString SubStr1("", (size_t)0);
        CHECK_EQUAL(SubStr1, "");

        RTCString SubStr2("abcdef", 2);
        CHECK_EQUAL(SubStr2, "ab");

        RTCString SubStr3("abcdef", 1);
        CHECK_EQUAL(SubStr3, "a");

        RTCString SubStr4("abcdef", 6);
        CHECK_EQUAL(SubStr4, "abcdef");

        RTCString SubStr5("abcdef", 7);
        CHECK_EQUAL(SubStr5, "abcdef");

        RTCString SubStrBase("abcdef");

        RTCString SubStr10(SubStrBase, 0);
        CHECK_EQUAL(SubStr10, "abcdef");

        RTCString SubStr11(SubStrBase, 1);
        CHECK_EQUAL(SubStr11, "bcdef");

        RTCString SubStr12(SubStrBase, 1, 1);
        CHECK_EQUAL(SubStr12, "b");

        RTCString SubStr13(SubStrBase, 2, 3);
        CHECK_EQUAL(SubStr13, "cde");

        RTCString SubStr14(SubStrBase, 2, 4);
        CHECK_EQUAL(SubStr14, "cdef");

        RTCString SubStr15(SubStrBase, 2, 5);
        CHECK_EQUAL(SubStr15, "cdef");
    }

    /* substr() and substrCP() functions */
    RTCString strTest("");
    CHECK_EQUAL(strTest.substr(0), "");
    CHECK_EQUAL(strTest.substrCP(0), "");
    CHECK_EQUAL(strTest.substr(1), "");
    CHECK_EQUAL(strTest.substrCP(1), "");

    /* now let's have some non-ASCII to chew on */
    strTest = "abcdefßäbcdef";
            // 13 codepoints, but 15 bytes (excluding null terminator);
            // "ß" and "ä" consume two bytes each
    CHECK_EQUAL(strTest.substr(0),   strTest.c_str());
    CHECK_EQUAL(strTest.substrCP(0), strTest.c_str());

    CHECK_EQUAL(strTest.substr(2),   "cdefßäbcdef");
    CHECK_EQUAL(strTest.substrCP(2), "cdefßäbcdef");

    CHECK_EQUAL(strTest.substr(2, 2),   "cd");
    CHECK_EQUAL(strTest.substrCP(2, 2), "cd");

    CHECK_EQUAL(strTest.substr(6),   "ßäbcdef");
    CHECK_EQUAL(strTest.substrCP(6), "ßäbcdef");

    CHECK_EQUAL(strTest.substr(6, 2),   "ß");           // UTF-8 "ß" consumes two bytes
    CHECK_EQUAL(strTest.substrCP(6, 1), "ß");

    CHECK_EQUAL(strTest.substr(8),   "äbcdef");         // UTF-8 "ß" consumes two bytes
    CHECK_EQUAL(strTest.substrCP(7), "äbcdef");

    CHECK_EQUAL(strTest.substr(8, 3),   "äb");          // UTF-8 "ä" consumes two bytes
    CHECK_EQUAL(strTest.substrCP(7, 2), "äb");

    CHECK_EQUAL(strTest.substr(14, 1),   "f");
    CHECK_EQUAL(strTest.substrCP(12, 1), "f");

    CHECK_EQUAL(strTest.substr(15, 1),   "");
    CHECK_EQUAL(strTest.substrCP(13, 1), "");

    CHECK_EQUAL(strTest.substr(16, 1),   "");
    CHECK_EQUAL(strTest.substrCP(15, 1), "");

    /* and check cooperation with find() */
    size_t pos = strTest.find("ß");
    CHECK_EQUAL(strTest.substr(pos), "ßäbcdef");

    /* check find() */
    CHECK_EQUAL_I(strTest.find("f"), 5);
    CHECK_EQUAL_I(strTest.find("f", 0), 5);
    CHECK_EQUAL_I(strTest.find("f", 3), 5);
    CHECK_EQUAL_I(strTest.find("f", 6), 14);
    CHECK_EQUAL_I(strTest.find("f", 9), 14);
    CHECK_EQUAL_I(strTest.substr(pos).find("d"), 6);

    /* split */
    RTCList<RTCString> spList1 = RTCString("##abcdef##abcdef####abcdef##").split("##", RTCString::RemoveEmptyParts);
    RTTESTI_CHECK(spList1.size() == 3);
    for (size_t i = 0; i < spList1.size(); ++i)
        RTTESTI_CHECK(spList1.at(i) == "abcdef");
    RTCList<RTCString> spList2 = RTCString("##abcdef##abcdef####abcdef##").split("##", RTCString::KeepEmptyParts);
    RTTESTI_CHECK_RETV(spList2.size() == 5);
    RTTESTI_CHECK(spList2.at(0) == "");
    RTTESTI_CHECK(spList2.at(1) == "abcdef");
    RTTESTI_CHECK(spList2.at(2) == "abcdef");
    RTTESTI_CHECK(spList2.at(3) == "");
    RTTESTI_CHECK(spList2.at(4) == "abcdef");
    RTCList<RTCString> spList3 = RTCString().split("##", RTCString::KeepEmptyParts);
    RTTESTI_CHECK(spList3.size() == 0);
    RTCList<RTCString> spList4 = RTCString().split("");
    RTTESTI_CHECK(spList4.size() == 0);
    RTCList<RTCString> spList5 = RTCString("abcdef").split("");
    RTTESTI_CHECK_RETV(spList5.size() == 1);
    RTTESTI_CHECK(spList5.at(0) == "abcdef");

    /* join */
    RTCList<RTCString> jnList;
    strTest = RTCString::join(jnList);
    RTTESTI_CHECK(strTest == "");
    strTest = RTCString::join(jnList, "##");
    RTTESTI_CHECK(strTest == "");

    jnList.append("abcdef");
    strTest = RTCString::join(jnList, "##");
    RTTESTI_CHECK(strTest == "abcdef");

    jnList.append("abcdef");
    strTest = RTCString::join(jnList, ";");
    RTTESTI_CHECK(strTest == "abcdef;abcdef");

    for (size_t i = 0; i < 3; ++i)
        jnList.append("abcdef");
    strTest = RTCString::join(jnList);
    RTTESTI_CHECK(strTest == "abcdefabcdefabcdefabcdefabcdef");
    strTest = RTCString::join(jnList, "##");
    RTTESTI_CHECK(strTest == "abcdef##abcdef##abcdef##abcdef##abcdef");

    /* special constructor and assignment arguments */
    RTCString StrCtor1("");
    RTTESTI_CHECK(StrCtor1.isEmpty());
    RTTESTI_CHECK(StrCtor1.length() == 0);

    RTCString StrCtor2(NULL);
    RTTESTI_CHECK(StrCtor2.isEmpty());
    RTTESTI_CHECK(StrCtor2.length() == 0);

    RTCString StrCtor1d(StrCtor1);
    RTTESTI_CHECK(StrCtor1d.isEmpty());
    RTTESTI_CHECK(StrCtor1d.length() == 0);

    RTCString StrCtor2d(StrCtor2);
    RTTESTI_CHECK(StrCtor2d.isEmpty());
    RTTESTI_CHECK(StrCtor2d.length() == 0);

    for (unsigned i = 0; i < 2; i++)
    {
        RTCString StrAssign;
        if (i) StrAssign = "abcdef";
        StrAssign = (char *)NULL;
        RTTESTI_CHECK(StrAssign.isEmpty());
        RTTESTI_CHECK(StrAssign.length() == 0);

        if (i) StrAssign = "abcdef";
        StrAssign = "";
        RTTESTI_CHECK(StrAssign.isEmpty());
        RTTESTI_CHECK(StrAssign.length() == 0);

        if (i) StrAssign = "abcdef";
        StrAssign = StrCtor1;
        RTTESTI_CHECK(StrAssign.isEmpty());
        RTTESTI_CHECK(StrAssign.length() == 0);

        if (i) StrAssign = "abcdef";
        StrAssign = StrCtor2;
        RTTESTI_CHECK(StrAssign.isEmpty());
        RTTESTI_CHECK(StrAssign.length() == 0);
    }

    /* truncation */
    RTCString StrTruncate1("abcdef");
    RTTESTI_CHECK(StrTruncate1.length() == 6);
    for (int i = 5; i >= 0; i--)
    {
        StrTruncate1.truncate(i);
        RTTESTI_CHECK(StrTruncate1.length() == (size_t)i);
    }

    RTCString StrTruncate2("01ßä6");
    CHECK_EQUAL(StrTruncate2, "01ßä6");
    StrTruncate2.truncate(6);
    CHECK_EQUAL(StrTruncate2, "01ßä");
    StrTruncate2.truncate(5);
    CHECK_EQUAL(StrTruncate2, "01ß");
    StrTruncate2.truncate(10);
    CHECK_EQUAL(StrTruncate2, "01ß");
    StrTruncate2.truncate(4);
    CHECK_EQUAL(StrTruncate2, "01ß");
    StrTruncate2.truncate(3);
    CHECK_EQUAL(StrTruncate2, "01");
    StrTruncate2.truncate(1);
    CHECK_EQUAL(StrTruncate2, "0");
    StrTruncate2.truncate(0);
    CHECK_EQUAL(StrTruncate2, "");

    /* endsWith */
    RTCString const strEmpty;
    strTest = "qwerty";
    CHECK(strTest.endsWith(strTest));
    CHECK(strTest.endsWith("qwerty"));
    CHECK(strTest.endsWith("werty"));
    CHECK(strTest.endsWith("erty"));
    CHECK(strTest.endsWith("rty"));
    CHECK(strTest.endsWith("ty"));
    CHECK(strTest.endsWith("y"));
    CHECK(!strTest.endsWith("ty00", 3));
    CHECK(strTest.endsWith("ty00", 2));
    CHECK(!strTest.endsWith(""));
    CHECK(!strTest.endsWith(NULL));
    CHECK(!strTest.endsWith(strEmpty));
    CHECK(!strEmpty.endsWith(""));
    CHECK(!strEmpty.endsWith(NULL));
    CHECK(!strEmpty.endsWith(strEmpty));

    CHECK(strTest.endsWithI("qwerty"));
    CHECK(strTest.endsWithI("QWERTY"));
    CHECK(strTest.endsWithI("wErtY"));
    CHECK(strTest.endsWithI("eRty"));
    CHECK(strTest.endsWithI("rTy"));
    CHECK(strTest.endsWithI("ty"));
    CHECK(strTest.endsWithI("y"));
    CHECK(!strTest.endsWithI("ty000", 3));
    CHECK(strTest.endsWithI("ty000", 2));
    CHECK(!strTest.endsWithI(""));
    CHECK(!strTest.endsWithI(NULL));
    CHECK(!strTest.endsWithI(strEmpty));
    CHECK(!strEmpty.endsWithI(""));
    CHECK(!strEmpty.endsWithI(NULL));
    CHECK(!strEmpty.endsWithI(strEmpty));

    /* startsWith */
    CHECK(strTest.startsWith(strTest));
    CHECK(strTest.startsWith("qwerty"));
    CHECK(strTest.startsWith("qwert"));
    CHECK(strTest.startsWith("qwer"));
    CHECK(strTest.startsWith("qwe"));
    CHECK(strTest.startsWith("qw"));
    CHECK(strTest.startsWith("q"));
    CHECK(strTest.startsWith("q00000", 1));
    CHECK(strTest.startsWith("qw0000", 2));
    CHECK(!strTest.startsWith("qw0000", 3));
    CHECK(!strTest.startsWith(""));
    CHECK(!strTest.startsWith(strEmpty));
    CHECK(!strEmpty.startsWith(strTest));
    CHECK(!strEmpty.startsWith(strEmpty));
    CHECK(!strEmpty.startsWith(""));
    CHECK(!strEmpty.startsWith(NULL));

    CHECK(strTest.startsWithI(strTest));
    CHECK(strTest.startsWithI("qWeRty"));
    CHECK(strTest.startsWithI("qWerT"));
    CHECK(strTest.startsWithI("qWeR"));
    CHECK(strTest.startsWithI("qwE"));
    CHECK(strTest.startsWithI("qW"));
    CHECK(strTest.startsWithI("q"));
    CHECK(strTest.startsWithI("Q00000", 1));
    CHECK(strTest.startsWithI("qW0000", 2));
    CHECK(!strTest.startsWithI("qW0000", 3));
    CHECK(!strTest.startsWithI(""));
    CHECK(!strTest.startsWithI(strEmpty));
    CHECK(!strEmpty.startsWithI(strTest));
    CHECK(!strEmpty.startsWithI(strEmpty));
    CHECK(!strEmpty.startsWithI(""));
    CHECK(!strEmpty.startsWithI(NULL));

#undef CHECK
#undef CHECK_DUMP
#undef CHECK_DUMP_I
#undef CHECK_EQUAL
}


static int mymemcmp(const char *psz1, const char *psz2, size_t cch)
{
    for (size_t off = 0; off < cch; off++)
        if (psz1[off] != psz2[off])
        {
            RTTestIFailed("off=%#x  psz1=%.*Rhxs  psz2=%.*Rhxs\n", off,
                          RT_MIN(cch - off, 8), &psz1[off],
                          RT_MIN(cch - off, 8), &psz2[off]);
            return psz1[off] > psz2[off] ? 1 : -1;
        }
    return 0;
}

#if 0
/**
 * Detects a few annoying unicode points with unstable case folding for UTF-8.
 *
 * Unicode 4.01, I think, introduces a few codepoints with lower/upper mappings
 * that has a different length when encoded as UTF-8.  This breaks some
 * assumptions we used to make.  Since it's just a handful codepoints, we'll
 * detect them and ignore them here.  The actual case folding functions in
 * IPRT will of course deal with this in a more robust manner.
 *
 * @returns true if problematic, false if not.
 * @param   uc      The codepoints.
 */
static bool isUnevenUtf8FoldingCp(RTUNICP uc)
{
    RTUNICP ucLower = RTUniCpToLower(uc);
    RTUNICP ucUpper = RTUniCpToUpper(uc);
    //return RTUniCpCalcUtf8Len(ucLower) != RTUniCpCalcUtf8Len(ucUpper);
    return false;
}
#endif

static void test2(RTTEST hTest)
{
    RTTestSub(hTest, "UTF-8 upper/lower encoding assumption");

#define CHECK_EQUAL(str1, str2) \
    do \
    { \
        RTTESTI_CHECK(strlen((str1).c_str()) == (str1).length()); \
        RTTESTI_CHECK((str1).length() == (str2).length()); \
        RTTESTI_CHECK(mymemcmp((str1).c_str(), (str2).c_str(), (str2).length() + 1) == 0); \
    } while (0)

    RTCString strTmp, strExpect;
    char szDst[16];

    /* Some simple ascii stuff. */
    strTmp    = "abcdefghijklmnopqrstuvwxyz0123456ABCDEFGHIJKLMNOPQRSTUVWXYZ;-+/\\";
    strExpect = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456ABCDEFGHIJKLMNOPQRSTUVWXYZ;-+/\\";
    strTmp.toUpper();
    CHECK_EQUAL(strTmp, strExpect);

    strTmp.toLower();
    strExpect = "abcdefghijklmnopqrstuvwxyz0123456abcdefghijklmnopqrstuvwxyz;-+/\\";
    CHECK_EQUAL(strTmp, strExpect);

    strTmp    = "abcdefghijklmnopqrstuvwxyz0123456ABCDEFGHIJKLMNOPQRSTUVWXYZ;-+/\\";
    strTmp.toLower();
    CHECK_EQUAL(strTmp, strExpect);

    /* Collect all upper and lower case code points. */
    RTCString strLower("");
    strLower.reserve(_4M);

    RTCString strUpper("");
    strUpper.reserve(_4M);

    for (RTUNICP uc = 1; uc <= 0x10fffd; uc++)
    {
        /* Unicode 4.01, I think, introduced a few codepoints with lower/upper mappings
           that aren't up for roundtrips and which case folding has a different UTF-8
           length.  We'll just skip them here as there are very few:
            - Dotless small i and dotless capital I folds into ASCII I and i.
            - The small letter long s folds to ASCII S.
            - Greek prosgegrammeni folds to iota, which is a letter with both upper
              and lower case foldings of its own. */
#if 0 /** @todo totally busted testcase, plz figure out how to fix. */
        if (   uc == 0x131
            || uc == 0x130
            || uc == 0x17f
            || uc == 0x1fbe
            )
            continue;

        if (RTUniCpIsLower(uc))
        {
            RTTESTI_CHECK_MSG(uc < 0xd800 || (uc > 0xdfff && uc != 0xfffe && uc != 0xffff), ("%#x\n", uc));
            strLower.appendCodePoint(uc);
        }
        if (RTUniCpIsUpper(uc))
        {
            RTTESTI_CHECK_MSG(uc < 0xd800 || (uc > 0xdfff && uc != 0xfffe && uc != 0xffff), ("%#x\n", uc));
            strUpper.appendCodePoint(uc);
        }
#else
        continue;
#endif
    }
    RTTESTI_CHECK(strlen(strLower.c_str()) == strLower.length());
    RTTESTI_CHECK(strlen(strUpper.c_str()) == strUpper.length());

    /* Fold each code point in the lower case string and check that it encodes
       into the same or less number of bytes. */
    size_t      cch    = 0;
    const char *pszCur = strLower.c_str();
    RTCString    strUpper2("");
    strUpper2.reserve(strLower.length() + 64);
    for (;;)
    {
        RTUNICP             ucLower;
        const char * const  pszPrev   = pszCur;
        RTTESTI_CHECK_RC_BREAK(RTStrGetCpEx(&pszCur, &ucLower), VINF_SUCCESS);
        size_t const        cchSrc    = pszCur - pszPrev;
        if (!ucLower)
            break;

        RTUNICP const       ucUpper   = RTUniCpToUpper(ucLower);
        const char         *pszDstEnd = RTStrPutCp(szDst, ucUpper);
        size_t const        cchDst    = pszDstEnd - &szDst[0];
        RTTESTI_CHECK_MSG(cchSrc >= cchDst,
                          ("ucLower=%#x %u bytes;  ucUpper=%#x %u bytes\n",
                           ucLower, cchSrc, ucUpper, cchDst));
        cch += cchDst;
        strUpper2.appendCodePoint(ucUpper);

        /* roundtrip stability */
        RTUNICP const       ucUpper2  = RTUniCpToUpper(ucUpper);
        RTTESTI_CHECK_MSG(ucUpper2 == ucUpper, ("ucUpper2=%#x ucUpper=%#x\n", ucUpper2, ucUpper));

        RTUNICP const       ucLower2  = RTUniCpToLower(ucUpper);
        RTTESTI_CHECK_MSG(ucLower2 == ucLower, ("ucLower2=%#x ucLower=%#x\n", ucLower2, ucLower));
        RTUNICP const       ucUpper3  = RTUniCpToUpper(ucLower2);
        RTTESTI_CHECK_MSG(ucUpper3 == ucUpper, ("ucUpper3=%#x ucUpper=%#x\n", ucUpper3, ucUpper));

        pszDstEnd = RTStrPutCp(szDst, ucLower2);
        size_t const        cchLower2 = pszDstEnd - &szDst[0];
        RTTESTI_CHECK_MSG(cchDst == cchLower2,
                          ("ucLower2=%#x %u bytes;  ucUpper=%#x %u bytes; ucLower=%#x\n",
                           ucLower2, cchLower2, ucUpper, cchDst, ucLower));
    }
    RTTESTI_CHECK(strlen(strUpper2.c_str()) == strUpper2.length());
    RTTESTI_CHECK_MSG(cch == strUpper2.length(), ("cch=%u length()=%u\n", cch, strUpper2.length()));

    /* the toUpper method shall do the same thing. */
    strTmp = strLower;      CHECK_EQUAL(strTmp, strLower);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);

    /* Ditto for the upper case string. */
    cch    = 0;
    pszCur = strUpper.c_str();
    RTCString    strLower2("");
    strLower2.reserve(strUpper.length() + 64);
    for (;;)
    {
        RTUNICP             ucUpper;
        const char * const  pszPrev   = pszCur;
        RTTESTI_CHECK_RC_BREAK(RTStrGetCpEx(&pszCur, &ucUpper), VINF_SUCCESS);
        size_t const        cchSrc    = pszCur - pszPrev;
        if (!ucUpper)
            break;

        RTUNICP const       ucLower   = RTUniCpToLower(ucUpper);
        const char         *pszDstEnd = RTStrPutCp(szDst, ucLower);
        size_t const        cchDst    = pszDstEnd - &szDst[0];
        RTTESTI_CHECK_MSG(cchSrc >= cchDst,
                          ("ucUpper=%#x %u bytes;  ucLower=%#x %u bytes\n",
                           ucUpper, cchSrc, ucLower, cchDst));

        cch += cchDst;
        strLower2.appendCodePoint(ucLower);

        /* roundtrip stability */
        RTUNICP const       ucLower2  = RTUniCpToLower(ucLower);
        RTTESTI_CHECK_MSG(ucLower2 == ucLower, ("ucLower2=%#x ucLower=%#x\n", ucLower2, ucLower));

        RTUNICP const       ucUpper2  = RTUniCpToUpper(ucLower);
        RTTESTI_CHECK_MSG(ucUpper2 == ucUpper, ("ucUpper2=%#x ucUpper=%#x\n", ucUpper2, ucUpper));
        RTUNICP const       ucLower3  = RTUniCpToLower(ucUpper2);
        RTTESTI_CHECK_MSG(ucLower3 == ucLower, ("ucLower3=%#x ucLower=%#x\n", ucLower3, ucLower));

        pszDstEnd = RTStrPutCp(szDst, ucUpper2);
        size_t const        cchUpper2 = pszDstEnd - &szDst[0];
        RTTESTI_CHECK_MSG(cchDst == cchUpper2,
                          ("ucUpper2=%#x %u bytes;  ucLower=%#x %u bytes\n",
                           ucUpper2, cchUpper2, ucLower, cchDst));
    }
    RTTESTI_CHECK(strlen(strLower2.c_str()) == strLower2.length());
    RTTESTI_CHECK_MSG(cch == strLower2.length(), ("cch=%u length()=%u\n", cch, strLower2.length()));

    strTmp = strUpper;      CHECK_EQUAL(strTmp, strUpper);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);

    /* Checks of folding stability when nothing shall change. */
    strTmp = strUpper;      CHECK_EQUAL(strTmp, strUpper);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper);

    strTmp = strUpper2;     CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);

    strTmp = strLower;      CHECK_EQUAL(strTmp, strLower);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower);

    strTmp = strLower2;     CHECK_EQUAL(strTmp, strLower2);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);

    /* Check folding stability for roundtrips. */
    strTmp = strUpper;      CHECK_EQUAL(strTmp, strUpper);
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);
    strTmp.toUpper();
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);
    strTmp.toUpper();
    strTmp.toLower();       CHECK_EQUAL(strTmp, strLower2);

    strTmp = strLower;      CHECK_EQUAL(strTmp, strLower);
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toLower();
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
    strTmp.toLower();
    strTmp.toUpper();       CHECK_EQUAL(strTmp, strUpper2);
}


int main()
{
    RTTEST      hTest;
    RTEXITCODE  rcExit = RTTestInitAndCreate("tstIprtMiniString", &hTest);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        RTTestBanner(hTest);

        test1(hTest);
        test2(hTest);

        rcExit = RTTestSummaryAndDestroy(hTest);
    }
    return rcExit;
}

