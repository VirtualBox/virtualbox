/* $Id: tstRTNetIPv6.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - IPv6.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <iprt/net.h>

#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/test.h>

#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define CHECKADDR(String, rcExpected, u32_0, u32_1, u32_2, u32_3)       \
    do {                                                                \
        RTNETADDRIPV6 Addr;                                             \
        char *pszZone;                                                  \
        uint32_t ExpectedAddr[4] = {                                    \
            RT_H2N_U32_C(u32_0), RT_H2N_U32_C(u32_1),                   \
            RT_H2N_U32_C(u32_2), RT_H2N_U32_C(u32_3)                    \
        };                                                              \
        int rc2 = RTNetStrToIPv6Addr(String, &Addr, &pszZone);          \
        if ((rcExpected) && !rc2)                                       \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc\n", \
                          __LINE__, String, (rcExpected), rc2);         \
        }                                                               \
        else if (   (rcExpected) != rc2                                 \
                 || (   rc2 == VINF_SUCCESS                             \
                     && memcmp(ExpectedAddr, &Addr, sizeof(Addr)) != 0)) \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc,"   \
                          " expected address %RTnaipv6 got %RTnaipv6\n", \
                          __LINE__, String, rcExpected, rc2,            \
                          ExpectedAddr, &Addr);                         \
        }                                                               \
    } while (0)

#define GOODADDR(String, u32_0, u32_1, u32_2, u32_3) \
    CHECKADDR(String, VINF_SUCCESS, u32_0, u32_1, u32_2, u32_3)

#define BADADDR(String) \
    CHECKADDR(String, VERR_INVALID_PARAMETER, 0, 0, 0, 0)


#define CHECKCIDR(String, rcExpected, u32_0, u32_1, u32_2, u32_3, iExpectedPrefix)    \
    do {                                                                \
        RTNETADDRIPV6 Addr;                                             \
        uint32_t ExpectedAddr[4] = {                                    \
            RT_H2N_U32_C(u32_0), RT_H2N_U32_C(u32_1),                   \
            RT_H2N_U32_C(u32_2), RT_H2N_U32_C(u32_3)                    \
        };                                                              \
        int iPrefix;                                                    \
                                                                        \
        int rc2 = RTNetStrToIPv6Cidr(String, &Addr, &iPrefix);          \
        if ((rcExpected) && !rc2)                                       \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc\n", \
                          __LINE__, String, (rcExpected), rc2);         \
        }                                                               \
        else if (   (rcExpected) != rc2                                 \
                 || (   rc2 == VINF_SUCCESS                             \
                     && (   memcmp(ExpectedAddr, &Addr, sizeof(Addr)) != 0 \
                         || iExpectedPrefix != iPrefix)))               \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc,"   \
                          " expected address %RTnaipv6/%d got %RTnaipv6/%d\n",\
                          __LINE__, String, rcExpected, rc2,            \
                          ExpectedAddr, iExpectedPrefix,                \
                          &Addr, iPrefix);                              \
        }                                                               \
    } while (0)

#define GOODCIDR(String, u32_0, u32_1, u32_2, u32_3, iExpectedPrefix) \
    CHECKCIDR(String, VINF_SUCCESS, u32_0, u32_1, u32_2, u32_3, iExpectedPrefix)

#define BADCIDR(String) \
    CHECKCIDR(String, VERR_INVALID_PARAMETER, 0, 0, 0, 0, 0)


#define CHECKANY(String, fExpected)                                     \
    do {                                                                \
        bool fRc = RTNetStrIsIPv6AddrAny(String);                       \
        if (fRc != fExpected)                                           \
        {                                                               \
            RTTestIFailed("at line %d: '%s':"                           \
                          " expected %RTbool got %RTbool\n",            \
                          __LINE__, (String), fExpected, fRc);          \
        }                                                               \
    } while (0)

#define IS_ANY(String)  CHECKANY((String), true)
#define NOT_ANY(String) CHECKANY((String), false)


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTNetIPv6", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);


    /* base case: eight groups fully spelled */
    GOODADDR("1:2:3:4:5:6:7:8", 0x00010002, 0x00030004, 0x00050006, 0x00070008);
    GOODADDR("0001:0002:0003:0004:0005:0006:0007:0008", 0x00010002, 0x00030004, 0x00050006, 0x00070008);
    GOODADDR("D:E:A:D:b:e:e:f", 0x000d000e, 0x000a000d, 0x000b000e, 0x000e000f);

    /* ... too short or too long */
    BADADDR("1:2:3:4:5:6:7");
    BADADDR("1:2:3:4:5:6:7:8:9");

    /* ... hex group constraints */
    BADADDR("1:2:3:4:5:6:7:-8");
    BADADDR("1:2:3:4:5:6:7:0x8");
    BADADDR("1:2:3:4:5:6:7:88888");
    BADADDR("1:2:3:4:5:6:7:00008");


    /* embedded IPv4 at the end */
    GOODADDR("0:0:0:0:0:0:1.2.3.4", 0, 0, 0, 0x01020304);

    /* ... not at the end */
    BADADDR("0:0:0:0:0:1.2.3.4:0");

    /* ... too short or too long */
    BADADDR("0:0:0:0:0:0:0:1.2.3.4");
    BADADDR("0:0:0:0:0:1.2.3.4");

    /* ... invalid IPv4 address */
    BADADDR("0:0:0:0:0:0:111.222.333.444");


    /* "any" in compressed form */
    GOODADDR("::",  0, 0, 0, 0);

    /* compressed run at the beginning */
    GOODADDR("::8",                      0,          0,          0, 0x00000008);
    GOODADDR("::7:8",                    0,          0,          0, 0x00070008);
    GOODADDR("::6:7:8",                  0,          0, 0x00000006, 0x00070008);
    GOODADDR("::5:6:7:8",                0,          0, 0x00050006, 0x00070008);
    GOODADDR("::4:5:6:7:8",              0, 0x00000004, 0x00050006, 0x00070008);
    GOODADDR("::3:4:5:6:7:8",            0, 0x00030004, 0x00050006, 0x00070008);
    GOODADDR("::2:3:4:5:6:7:8", 0x00000002, 0x00030004, 0x00050006, 0x00070008);

    /* ... too long */
    BADADDR("::1:2:3:4:5:6:7:8");

    /* compressed run at the end */
    GOODADDR("1::",             0x00010000,          0,          0,          0);
    GOODADDR("1:2::",           0x00010002,          0,          0,          0);
    GOODADDR("1:2:3::",         0x00010002, 0x00030000,          0,          0);
    GOODADDR("1:2:3:4::",       0x00010002, 0x00030004,          0,          0);
    GOODADDR("1:2:3:4:5::",     0x00010002, 0x00030004, 0x00050000,          0);
    GOODADDR("1:2:3:4:5:6::",   0x00010002, 0x00030004, 0x00050006,          0);
    GOODADDR("1:2:3:4:5:6:7::", 0x00010002, 0x00030004, 0x00050006, 0x00070000);

    /* ... too long */
    BADADDR("1:2:3:4:5:6:7:8::");

    /* compressed run in the middle  */
    GOODADDR("1::8",            0x00010000,          0,          0, 0x00000008);
    GOODADDR("1:2::8",          0x00010002,          0,          0, 0x00000008);
    GOODADDR("1:2:3::8",        0x00010002, 0x00030000,          0, 0x00000008);
    GOODADDR("1:2:3:4::8",      0x00010002, 0x00030004,          0, 0x00000008);
    GOODADDR("1:2:3:4:5::8",    0x00010002, 0x00030004, 0x00050000, 0x00000008);
    GOODADDR("1:2:3:4:5:6::8",  0x00010002, 0x00030004, 0x00050006, 0x00000008);

    GOODADDR("1::7:8",          0x00010000,          0,          0, 0x00070008);
    GOODADDR("1::6:7:8",        0x00010000,          0, 0x00000006, 0x00070008);
    GOODADDR("1::5:6:7:8",      0x00010000,          0, 0x00050006, 0x00070008);
    GOODADDR("1::4:5:6:7:8",    0x00010000, 0x00000004, 0x00050006, 0x00070008);
    GOODADDR("1::3:4:5:6:7:8",  0x00010000, 0x00030004, 0x00050006, 0x00070008);

    /* ... too long */
    BADADDR("1::2:3:4:5:6:7:8");
    BADADDR("1:2::3:4:5:6:7:8");
    BADADDR("1:2:3::4:5:6:7:8");
    BADADDR("1:2:3:4::5:6:7:8");
    BADADDR("1:2:3:4:5::6:7:8");
    BADADDR("1:2:3:4:5:6::7:8");
    BADADDR("1:2:3:4:5:6:7::8");

    /* compressed with embedded IPv4 */
    GOODADDR("::0.0.0.0",        0, 0,          0,          0);
    GOODADDR("::1.2.3.4",        0, 0,          0, 0x01020304);
    GOODADDR("::ffff:1.2.3.4",   0, 0, 0x0000ffff, 0x01020304);
    GOODADDR("::ffff:0:1.2.3.4", 0, 0, 0xffff0000, 0x01020304);

    GOODADDR("1::1.2.3.4",         0x00010000,          0,          0, 0x01020304);
    GOODADDR("1:2::1.2.3.4",       0x00010002,          0,          0, 0x01020304);
    GOODADDR("1:2:3::1.2.3.4",     0x00010002, 0x00030000,          0, 0x01020304);
    GOODADDR("1:2:3:4::1.2.3.4",   0x00010002, 0x00030004,          0, 0x01020304);
    GOODADDR("1:2:3:4:5::1.2.3.4", 0x00010002, 0x00030004, 0x00050000, 0x01020304);

    /* ... too long */
    BADADDR("1:2:3:4:5:6::1.2.3.4");
    BADADDR("1:2:3:4:5::6:1.2.3.4");
    BADADDR("1:2:3:4::5:6:1.2.3.4");
    BADADDR("1:2:3::4:5:6:1.2.3.4");
    BADADDR("1:2::3:4:5:6:1.2.3.4");
    BADADDR("1::2:3:4:5:6:1.2.3.4");

    /* zone ids (beware, shaky ground) */
    GOODADDR("ff01::1%0",      0xff010000, 0, 0, 1);
    GOODADDR("ff01::1%eth0",   0xff010000, 0, 0, 1);
    GOODADDR("ff01::1%net1.0", 0xff010000, 0, 0, 1);

    GOODADDR(" ff01::1%net1.1\t", 0xff010000, 0, 0, 1);

    /* just some light testing */
    GOODCIDR("1:2:3:4:5:6:7:8", 0x00010002, 0x00030004, 0x00050006, 0x00070008, 128);
    GOODCIDR("1:2:3:4::/64",    0x00010002, 0x00030004,          0,          0,  64);
    GOODCIDR(" 1:2:3:4::/64 ",  0x00010002, 0x00030004,          0,          0,  64);

    /* we currently ignore the zone */
    GOODCIDR("1:2:3:4::%if/64", 0x00010002, 0x00030004,          0,          0,  64);


    GOODCIDR("::/0", 0, 0, 0, 0, 0);

    /*
     * we allow zero prefix mostly for the sake of the above
     * "everything"/default case, but allow it on everything - a
     * conscientious caller should be doing more checks on the result
     * anyway.
     */
    GOODCIDR("1:2:3:4::/0",    0x00010002, 0x00030004,          0,          0,    0);


    BADCIDR("1:2:3:4:: 64");
    BADCIDR("1:2:3:4::/64x");
    BADCIDR("1:2:3:4::/-1");
    BADCIDR("1:2:3:4::/129");
    BADCIDR("1:2:3:4::/256");

    IS_ANY("::");
    IS_ANY("::0.0.0.0");
    IS_ANY("0:0:0:0:0:0:0:0");
    IS_ANY("0000:0000:0000:0000:0000:0000:0000:0000");

    IS_ANY("\t :: \t");

    NOT_ANY("::1");
    NOT_ANY("0:0:0:0:0:0:0:1");

    NOT_ANY(":: x");
    NOT_ANY("::%");
    NOT_ANY("::%eth0");         /* or is it? */

    return RTTestSummaryAndDestroy(hTest);
}
