/* $Id: tstRTCType.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - ctype.h.
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
#include <iprt/ctype.h>

#include <iprt/test.h>
#if defined(RT_OS_LINUX) || defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS)
# define VERIFY_TEST_DATA
# include <locale.h>
# include <ctype.h>
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#define TEST_X(a_ch, a_Macro, a_fMask) \
    do { \
        bool const fBit  = RT_BOOL(g_afCharMap[(a_ch)] & (a_fMask)); \
        bool const fTest = a_Macro(a_ch); \
        if (fBit != fTest) \
            RTTestIFailed(#a_Macro "(%03d [%#04x]) returns %RTbool, expected %RTbool", \
                          (int)(a_ch), (int)(a_ch), fTest, fBit); \
    } while (0)

#define X_CNTRL     RT_BIT_32(0)
#define X_SPACE     RT_BIT_32(1)
#define X_BLANK     RT_BIT_32(2)
#define X_PRINT     RT_BIT_32(3)
#define X_PUNCT     RT_BIT_32(4)
#define X_GRAPH     RT_BIT_32(5)
#define X_DIGIT     RT_BIT_32(6)
#define X_XDIGIT    RT_BIT_32(7)
#define X_ODIGIT    RT_BIT_32(8)
#define X_ALPHA     RT_BIT_32(9)
#define X_UPPER     RT_BIT_32(10)
#define X_LOWER     RT_BIT_32(11)

static const uint32_t g_afCharMap[128] =
{
    /* [0x00] = */ X_CNTRL,
    /* [0x01] = */ X_CNTRL,
    /* [0x02] = */ X_CNTRL,
    /* [0x03] = */ X_CNTRL,
    /* [0x04] = */ X_CNTRL,
    /* [0x05] = */ X_CNTRL,
    /* [0x06] = */ X_CNTRL,
    /* [0x07] = */ X_CNTRL,
    /* [0x08] = */ X_CNTRL,
    /* [0x09] = */ X_CNTRL | X_SPACE | X_BLANK, /* tab */
    /* [0x0a] = */ X_CNTRL | X_SPACE,
    /* [0x0b] = */ X_CNTRL | X_SPACE,
    /* [0x0c] = */ X_CNTRL | X_SPACE,
    /* [0x0d] = */ X_CNTRL | X_SPACE,           /* carriage return */
    /* [0x0e] = */ X_CNTRL,
    /* [0x0f] = */ X_CNTRL,
    /* [0x10] = */ X_CNTRL,
    /* [0x11] = */ X_CNTRL,
    /* [0x12] = */ X_CNTRL,
    /* [0x13] = */ X_CNTRL,
    /* [0x14] = */ X_CNTRL,
    /* [0x15] = */ X_CNTRL,
    /* [0x16] = */ X_CNTRL,
    /* [0x17] = */ X_CNTRL,
    /* [0x18] = */ X_CNTRL,
    /* [0x19] = */ X_CNTRL,
    /* [0x1a] = */ X_CNTRL,
    /* [0x1b] = */ X_CNTRL,
    /* [0x1c] = */ X_CNTRL,
    /* [0x1d] = */ X_CNTRL,
    /* [0x1e] = */ X_CNTRL,
    /* [0x1f] = */ X_CNTRL,
    /* [0x20] = */ X_PRINT | X_SPACE | X_BLANK, /* space */
    /* [0x21] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x22] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x23] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x24] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x25] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x26] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x27] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x28] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x29] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x2a] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x2b] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x2c] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x2d] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x2e] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x2f] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x30] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT | X_ODIGIT,
    /* [0x31] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT | X_ODIGIT,
    /* [0x32] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT | X_ODIGIT,
    /* [0x33] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT | X_ODIGIT,
    /* [0x34] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT | X_ODIGIT,
    /* [0x35] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT | X_ODIGIT,
    /* [0x36] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT | X_ODIGIT,
    /* [0x37] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT | X_ODIGIT,
    /* [0x38] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT,
    /* [0x39] = */ X_PRINT | X_GRAPH | X_DIGIT | X_XDIGIT,
    /* [0x3a] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x3b] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x3c] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x3d] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x3e] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x3f] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x40] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x41] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER | X_XDIGIT, /* A */
    /* [0x42] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER | X_XDIGIT,
    /* [0x43] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER | X_XDIGIT,
    /* [0x44] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER | X_XDIGIT,
    /* [0x45] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER | X_XDIGIT,
    /* [0x46] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER | X_XDIGIT,
    /* [0x47] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x48] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x49] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x4a] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x4b] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x4c] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x4d] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x4e] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x4f] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x50] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x51] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x52] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x53] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x54] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x55] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x56] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x57] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x58] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x59] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x5a] = */ X_PRINT | X_GRAPH | X_ALPHA | X_UPPER,
    /* [0x5b] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x5c] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x5d] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x5e] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x5f] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x60] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x61] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER | X_XDIGIT, /* a */
    /* [0x62] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER | X_XDIGIT,
    /* [0x63] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER | X_XDIGIT,
    /* [0x64] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER | X_XDIGIT,
    /* [0x65] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER | X_XDIGIT,
    /* [0x66] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER | X_XDIGIT,
    /* [0x67] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x68] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x69] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x6a] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x6b] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x6c] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x6d] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x6e] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x6f] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x70] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x71] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x72] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x73] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x74] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x75] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x76] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x77] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x78] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x79] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x7a] = */ X_PRINT | X_GRAPH | X_ALPHA | X_LOWER,
    /* [0x7b] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x7c] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x7d] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x7e] = */ X_PRINT | X_GRAPH | X_PUNCT,
    /* [0x7f] = */ X_CNTRL,                     /* del */
};

#ifdef VERIFY_TEST_DATA
static void test0(void)
{
    RTTestISub("Verify test data");
    RTTESTI_CHECK(setlocale(LC_ALL, "C") != NULL);

    for (int ch = 0; ch < (int)RT_ELEMENTS(g_afCharMap); ch++)
    {
        TEST_X(ch, iscntrl,  X_CNTRL);
        TEST_X(ch, isspace,  X_SPACE);
        TEST_X(ch, isblank,  X_BLANK);
        TEST_X(ch, isprint,  X_PRINT);
        TEST_X(ch, ispunct,  X_PUNCT);
        TEST_X(ch, isgraph,  X_GRAPH);
        TEST_X(ch, isdigit,  X_DIGIT);
        TEST_X(ch, isxdigit, X_XDIGIT);
        /*TEST_X(ch, isodigit, X_ODIGIT);*/
        TEST_X(ch, isalpha,  X_ALPHA);
        TEST_X(ch, isupper,  X_UPPER);
        TEST_X(ch, islower,  X_LOWER);
    }
}
#endif /* VERIFY_TEST_DATA */

static void test1(void)
{
    RTTestISub("ASCII range");

    for (int ch = 0; ch < (int)RT_ELEMENTS(g_afCharMap); ch++)
    {
        TEST_X(ch, RT_C_IS_CNTRL,  X_CNTRL);
        TEST_X(ch, RT_C_IS_SPACE,  X_SPACE);
        TEST_X(ch, RT_C_IS_BLANK,  X_BLANK);
        TEST_X(ch, RT_C_IS_PRINT,  X_PRINT);
        TEST_X(ch, RT_C_IS_PUNCT,  X_PUNCT);
        TEST_X(ch, RT_C_IS_GRAPH,  X_GRAPH);
        TEST_X(ch, RT_C_IS_DIGIT,  X_DIGIT);
        TEST_X(ch, RT_C_IS_XDIGIT, X_XDIGIT);
        TEST_X(ch, RT_C_IS_ODIGIT, X_ODIGIT);
        TEST_X(ch, RT_C_IS_ALPHA,  X_ALPHA);
        TEST_X(ch, RT_C_IS_UPPER,  X_UPPER);
        TEST_X(ch, RT_C_IS_LOWER,  X_LOWER);
    }
}

static void test2(void)
{
    RTTestISub("< 0");
    for (int ch = -1; ch > -2000000; ch--)
    {
        RTTESTI_CHECK(!RT_C_IS_CNTRL(ch));
        RTTESTI_CHECK(!RT_C_IS_SPACE(ch));
        RTTESTI_CHECK(!RT_C_IS_BLANK(ch));
        RTTESTI_CHECK(!RT_C_IS_PRINT(ch));
        RTTESTI_CHECK(!RT_C_IS_PUNCT(ch));
        RTTESTI_CHECK(!RT_C_IS_GRAPH(ch));
        RTTESTI_CHECK(!RT_C_IS_DIGIT(ch));
        RTTESTI_CHECK(!RT_C_IS_XDIGIT(ch));
        RTTESTI_CHECK(!RT_C_IS_ODIGIT(ch));
        RTTESTI_CHECK(!RT_C_IS_ALPHA(ch));
        RTTESTI_CHECK(!RT_C_IS_UPPER(ch));
        RTTESTI_CHECK(!RT_C_IS_LOWER(ch));
    }
}

static void test3(void)
{
    RTTestISub("> 127");
    for (int ch = 128; ch < 2000000; ch++)
    {
        RTTESTI_CHECK(!RT_C_IS_CNTRL(ch));
        RTTESTI_CHECK(!RT_C_IS_SPACE(ch));
        RTTESTI_CHECK(!RT_C_IS_BLANK(ch));
        RTTESTI_CHECK(!RT_C_IS_PRINT(ch));
        RTTESTI_CHECK(!RT_C_IS_PUNCT(ch));
        RTTESTI_CHECK(!RT_C_IS_GRAPH(ch));
        RTTESTI_CHECK(!RT_C_IS_DIGIT(ch));
        RTTESTI_CHECK(!RT_C_IS_XDIGIT(ch));
        RTTESTI_CHECK(!RT_C_IS_ODIGIT(ch));
        RTTESTI_CHECK(!RT_C_IS_ALPHA(ch));
        RTTESTI_CHECK(!RT_C_IS_UPPER(ch));
        RTTESTI_CHECK(!RT_C_IS_LOWER(ch));
    }
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTCType", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

#ifdef VERIFY_TEST_DATA
    test0();
#endif
    test1();
    test2();
    test3();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

