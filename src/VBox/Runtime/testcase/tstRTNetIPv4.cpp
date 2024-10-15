/* $Id: tstRTNetIPv4.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - IPv4.
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


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define CHECKADDR(String, rcExpected, ExpectedAddr)                     \
    do {                                                                \
        RTNETADDRIPV4 Addr;                                             \
        int rc2 = RTNetStrToIPv4Addr(String, &Addr);                    \
        if ((rcExpected) && !rc2)                                       \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc\n", \
                          __LINE__, String, (rcExpected), rc2);         \
        }                                                               \
        else if (   (rcExpected) != rc2                                 \
                 || (   rc2 == VINF_SUCCESS                             \
                     && RT_H2N_U32_C(ExpectedAddr) != Addr.u))          \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc,"   \
                          " expected address %RTnaipv4 got %RTnaipv4\n", \
                          __LINE__, String, rcExpected, rc2,            \
                          RT_H2N_U32_C(ExpectedAddr), Addr.u);          \
        }                                                               \
    } while (0)

#define GOODADDR(String, ExpectedAddr) \
    CHECKADDR(String, VINF_SUCCESS, ExpectedAddr)

#define BADADDR(String) \
    CHECKADDR(String, VERR_INVALID_PARAMETER, 0)


#define CHECKADDREX(String, Trailer, rcExpected, ExpectedAddr)          \
    do {                                                                \
        RTNETADDRIPV4 Addr;                                             \
        const char *strAll = String /* concat */ Trailer;               \
        const char *pTrailer = strAll + sizeof(String) - 1;             \
        char *pNext = NULL;                                             \
        int rc2 = RTNetStrToIPv4AddrEx(strAll, &Addr, &pNext);          \
        if ((rcExpected) && !rc2)                                       \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc\n", \
                          __LINE__, String, (rcExpected), rc2);         \
        }                                                               \
        else if ((rcExpected) != rc2                                    \
                 || (rc2 == VINF_SUCCESS                                \
                     && (RT_H2N_U32_C(ExpectedAddr) != Addr.u           \
                         || pTrailer != pNext)))                        \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc,"   \
                          " expected address %RTnaipv4 got %RTnaipv4"   \
                          " expected trailer \"%s\" got %s%s%s"         \
                          "\n",                                         \
                          __LINE__, String, rcExpected, rc2,            \
                          RT_H2N_U32_C(ExpectedAddr), Addr.u,           \
                          pTrailer,                                     \
                          pNext ? "\"" : "",                            \
                          pNext ? pNext : "(null)",                     \
                          pNext ? "\"" : "");                           \
        }                                                               \
    } while (0)


#define CHECKCIDR(String, rcExpected, ExpectedAddr, iExpectedPrefix)    \
    do {                                                                \
        RTNETADDRIPV4 Addr;                                             \
        int iPrefix;                                                    \
                                                                        \
        int rc2 = RTNetStrToIPv4Cidr(String, &Addr, &iPrefix);          \
        if ((rcExpected) && !rc2)                                       \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc\n", \
                          __LINE__, String, (rcExpected), rc2);         \
        }                                                               \
        else if (   (rcExpected) != rc2                                 \
                 || (   rc2 == VINF_SUCCESS                             \
                     && (   RT_H2N_U32_C(ExpectedAddr) != Addr.u        \
                         || iExpectedPrefix != iPrefix)))               \
        {                                                               \
            RTTestIFailed("at line %d: '%s': expected %Rrc got %Rrc,"   \
                          " expected address %RTnaipv4/%d got %RTnaipv4/%d\n", \
                          __LINE__, String, rcExpected, rc2,            \
                          RT_H2N_U32_C(ExpectedAddr), (iExpectedPrefix), \
                          Addr.u, iPrefix);                             \
        }                                                               \
    } while (0)

#define GOODCIDR(String, ExpectedAddr, iExpectedPrefix) \
    CHECKCIDR(String, VINF_SUCCESS, ExpectedAddr, iExpectedPrefix)

#define BADCIDR(String) \
    CHECKCIDR(String, VERR_INVALID_PARAMETER, 0, 0)


#define CHECKISADDR(String, fExpected)                                  \
    do {                                                                \
        bool fRc = RTNetIsIPv4AddrStr(String);                          \
        if (fRc != fExpected)                                           \
        {                                                               \
            RTTestIFailed("at line %d: '%s':"                           \
                          " expected %RTbool got %RTbool\n",            \
                          __LINE__, (String), fExpected, fRc);          \
        }                                                               \
    } while (0)

#define IS_ADDR(String)  CHECKISADDR((String), true)
#define NOT_ADDR(String) CHECKISADDR((String), false)


#define CHECKANY(String, fExpected)                                     \
    do {                                                                \
        bool fRc = RTNetStrIsIPv4AddrAny(String);                       \
        if (fRc != fExpected)                                           \
        {                                                               \
            RTTestIFailed("at line %d: '%s':"                           \
                          " expected %RTbool got %RTbool\n",            \
                          __LINE__, (String), fExpected, fRc);          \
        }                                                               \
    } while (0)

#define IS_ANY(String)  CHECKANY((String), true)
#define NOT_ANY(String) CHECKANY((String), false)


#define CHECKMASK(_mask, _rcExpected, _iExpectedPrefix)                 \
    do {                                                                \
        /* const */ RTNETADDRIPV4 Mask;                                 \
        int iExpectedPrefix = (_iExpectedPrefix);                       \
        int iPrefix;                                                    \
        const int rcExpected = (_rcExpected);                           \
        int rc2;                                                        \
                                                                        \
        Mask.u = RT_H2N_U32_C(UINT32_C(_mask));                         \
        rc2 = RTNetMaskToPrefixIPv4(&Mask, &iPrefix);                   \
                                                                        \
        if (rcExpected == VINF_SUCCESS)                                 \
        {                                                               \
            if (rc2 != rcExpected)                                      \
            {                                                           \
                /* unexpected failure */                                \
                RTTestIFailed("at line %d: mask %RTnaipv4:"             \
                              " expected prefix length %d got %Rrc\n",  \
                              __LINE__, Mask.u,                         \
                              iExpectedPrefix, rc2);                    \
            }                                                           \
            else if (iPrefix != iExpectedPrefix)                        \
            {                                                           \
                /* unexpected result */                                 \
                RTTestIFailed("at line %d: mask %RTnaipv4:"             \
                              " expected prefix length %d got %d\n",    \
                              __LINE__, Mask.u,                         \
                              iExpectedPrefix, iPrefix);                \
            }                                                           \
        }                                                               \
        else /* expect failure */                                       \
        {                                                               \
            if (rc2 == VINF_SUCCESS)                                    \
            {                                                           \
                /* unexpected success */                                \
                RTTestIFailed("at line %d: mask %RTnaipv4:"             \
                              " expected %Rrc got prefix length %d\n",  \
                              __LINE__, Mask.u,                         \
                              rcExpected, iPrefix);                     \
            }                                                           \
            else if (rc2 != rcExpected)                                 \
            {                                                           \
                /* unexpected error */                                  \
                RTTestIFailed("at line %d: mask %RTnaipv4:"             \
                              " expected %Rrc got %Rrc\n",              \
                              __LINE__, Mask.u,                         \
                              rcExpected, rc2);                         \
            }                                                           \
        }                                                               \
    } while (0)

#define CHECKPREFIX(_prefix, _rcExpected, _mask)                        \
    do {                                                                \
        const int iPrefix = (_prefix);                                  \
        RTNETADDRIPV4 ExpectedMask, Mask;                               \
        const int rcExpected = (_rcExpected);                           \
        int rc2;                                                        \
                                                                        \
        ExpectedMask.u = RT_H2N_U32_C(UINT32_C(_mask));                 \
        rc2 = RTNetPrefixToMaskIPv4(iPrefix, &Mask);                    \
                                                                        \
        if (rcExpected == VINF_SUCCESS)                                 \
        {                                                               \
            if (rc2 != rcExpected)                                      \
            {                                                           \
                /* unexpected failure */                                \
                RTTestIFailed("at line %d: prefix %d:"                  \
                              " expected mask %RTnaipv4 got %Rrc\n",    \
                              __LINE__, iPrefix,                        \
                              ExpectedMask.u, rc2);                     \
            }                                                           \
            else if (Mask.u != ExpectedMask.u)                          \
            {                                                           \
                /* unexpected result */                                 \
                RTTestIFailed("at line %d: prefix %d:"                  \
                              " expected mask %RTnaipv4 got %RTnaipv4\n", \
                              __LINE__, iPrefix,                        \
                              ExpectedMask.u, Mask.u);                  \
            }                                                           \
        }                                                               \
        else /* expect failure */                                       \
        {                                                               \
            if (rc2 == VINF_SUCCESS)                                    \
            {                                                           \
                /* unexpected success */                                \
                RTTestIFailed("at line %d: prefix %d:"                  \
                              " expected %Rrc got mask %RTnaipv4\n",    \
                              __LINE__, iPrefix,                        \
                              rcExpected, Mask.u);                      \
            }                                                           \
            else if (rc2 != rcExpected)                                 \
            {                                                           \
                /* unexpected error */                                  \
                RTTestIFailed("at line %d: prefix %d:"                  \
                              " expected %Rrc got %Rrc\n",              \
                              __LINE__, iPrefix,                        \
                              rcExpected, rc2);                         \
            }                                                           \
        }                                                               \
    } while (0)

#define GOODMASK(_mask, _prefix)                                        \
    do {                                                                \
        CHECKMASK(_mask, VINF_SUCCESS, _prefix);                        \
        CHECKPREFIX(_prefix, VINF_SUCCESS, _mask);                      \
    } while (0)

#define BADMASK(_mask) \
    CHECKMASK(_mask, VERR_INVALID_PARAMETER, -1)

#define BADPREFIX(_prefix) \
    CHECKPREFIX(_prefix, VERR_INVALID_PARAMETER, 0)


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTNetIPv4", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    GOODADDR("1.2.3.4",         0x01020304);
    GOODADDR("0.0.0.0",         0x00000000);
    GOODADDR("255.255.255.255", 0xFFFFFFFF);

    /* leading and trailing whitespace is allowed */
    GOODADDR(" 1.2.3.4 ",       0x01020304);
    GOODADDR("\t1.2.3.4\t",     0x01020304);

    BADADDR("1.2.3.4x");
    BADADDR("1.2.3.4.");
    BADADDR("1.2.3");
    BADADDR("0x1.2.3.4");
    BADADDR("666.2.3.4");
    BADADDR("1.666.3.4");
    BADADDR("1.2.666.4");
    BADADDR("1.2.3.666");

    /*
     * Parsing itself is covered by the tests above, here we only
     * check trailers
     */
    CHECKADDREX("1.2.3.4",  "",   VINF_SUCCESS,           0x01020304);
    CHECKADDREX("1.2.3.4",  " ",  VWRN_TRAILING_SPACES,   0x01020304);
    CHECKADDREX("1.2.3.4",  "x",  VWRN_TRAILING_CHARS,    0x01020304);
    CHECKADDREX("1.2.3.444", "",  VERR_INVALID_PARAMETER,          0);


    GOODCIDR("1.2.3.4",         0x01020304, 32);
    GOODCIDR("1.2.3.4/32",      0x01020304, 32);
    GOODCIDR("1.2.3.4/24",      0x01020304, 24); /* address is not truncated to prefix */

    GOODCIDR("1.2.3.0/0xffffff00",      0x01020300, 24);
    GOODCIDR("1.2.3.4/0xffffff00",      0x01020304, 24);
    GOODCIDR("1.2.3.4/0xffffffff",      0x01020304, 32);

    GOODCIDR("1.2.3.0/255.255.255.0",   0x01020300, 24);
    GOODCIDR("1.2.3.4/255.255.255.0",   0x01020304, 24);
    GOODCIDR("1.2.3.4/255.255.255.255", 0x01020304, 32);

    GOODCIDR("0.0.0.0/0",       0x00000000,  0);
    GOODCIDR("0.0.0.0/0x0",     0x00000000,  0);
    GOODCIDR("0.0.0.0/0.0.0.0", 0x00000000,  0);

    /*
     * we allow zero prefix mostly for the sake of the above
     * "everything"/default case, but allow it on everything - a
     * conscientious caller should be doing more checks on the result
     * anyway.
     */
    GOODCIDR("1.2.3.4/0",       0x01020304,  0);        /* prefix can be zero */

    GOODCIDR("\t " "1.2.3.4/24",       0x01020304, 24); /* leading spaces ok */
    GOODCIDR(      "1.2.3.4/24" " \t", 0x01020304, 24); /* trailing spaces ok */
    GOODCIDR("\t " "1.2.3.4/24" " \t", 0x01020304, 24); /* both are ok */

    /* trailing space with netmask notation */
    GOODCIDR("1.2.3.0/0xffffff00" " ",    0x01020300, 24);
    GOODCIDR("1.2.3.0/255.255.255.0" " ", 0x01020300, 24);

    BADCIDR("1.2.3.4/24.");
    BADCIDR("1.2.3.4/24 .");
    BADCIDR("1.2.3.4/240.");
    BADCIDR("1.2.3.4/240.");

    BADCIDR("1.2.3.4/33");      /* prefix is too big */
    BADCIDR("1.2.3.4/256");     /* prefix is too big */
    BADCIDR("1.2.3.4/257");     /* prefix is too big */
    BADCIDR("1.2.3.4/-1");      /* prefix is negative */
    BADCIDR("1.2.3.4/");        /* prefix is missing */
    BADCIDR("1.2.3.4/a");       /* prefix is not a number */
    BADCIDR("1.2.3.4/0xa");     /* prefix is not decimal */
//  BADCIDR("1.2.3.0/024");     /* XXX: prefix is not decimal */

    BADCIDR("1.2.3.0 /24");     /* no spaces after address */
    BADCIDR("1.2.3.0/ 24");     /* no spaces after slash */

    BADCIDR("1.2.3.0/24" "x");  /* trailing chars */
    BADCIDR("1.2.3.0/24" " x"); /* trailing chars */

    BADCIDR("1.2.3.0/0xffffff01");    /* non-contiguous mask */
    BADCIDR("1.2.3.0/255.255.255.1"); /* non-contiguous mask */

    /* NB: RTNetIsIPv4AddrStr does NOT allow leading/trailing whitespace */
    IS_ADDR("1.2.3.4");
    NOT_ADDR(" 1.2.3.4");
    NOT_ADDR("1.2.3.4 ");
    NOT_ADDR("1.2.3.4x");

    IS_ANY("0.0.0.0");
    IS_ANY("\t 0.0.0.0 \t");    /* ... but RTNetStrIsIPv4AddrAny does */

    NOT_ANY("1.1.1.1");         /* good address, but not INADDR_ANY */
    NOT_ANY("0.0.0.0x");        /* bad address */


    /*
     * The mask <-> prefix table is small so we can test it all.
     */
    GOODMASK(0x00000000,  0); /* 0.0.0.0         */
    GOODMASK(0x80000000,  1); /* 128.0.0.0       */
    GOODMASK(0xc0000000,  2); /* 192.0.0.0       */
    GOODMASK(0xe0000000,  3); /* 224.0.0.0       */
    GOODMASK(0xf0000000,  4); /* 240.0.0.0       */
    GOODMASK(0xf8000000,  5); /* 248.0.0.0       */
    GOODMASK(0xfc000000,  6); /* 252.0.0.0       */
    GOODMASK(0xfe000000,  7); /* 254.0.0.0       */
    GOODMASK(0xff000000,  8); /* 255.0.0.0       */
    GOODMASK(0xff800000,  9); /* 255.128.0.0     */
    GOODMASK(0xffc00000, 10); /* 255.192.0.0     */
    GOODMASK(0xffe00000, 11); /* 255.224.0.0     */
    GOODMASK(0xfff00000, 12); /* 255.240.0.0     */
    GOODMASK(0xfff80000, 13); /* 255.248.0.0     */
    GOODMASK(0xfffc0000, 14); /* 255.252.0.0     */
    GOODMASK(0xfffe0000, 15); /* 255.254.0.0     */
    GOODMASK(0xffff0000, 16); /* 255.255.0.0     */
    GOODMASK(0xffff8000, 17); /* 255.255.128.0   */
    GOODMASK(0xffffc000, 18); /* 255.255.192.0   */
    GOODMASK(0xffffe000, 19); /* 255.255.224.0   */
    GOODMASK(0xfffff000, 20); /* 255.255.240.0   */
    GOODMASK(0xfffff800, 21); /* 255.255.248.0   */
    GOODMASK(0xfffffc00, 22); /* 255.255.252.0   */
    GOODMASK(0xfffffe00, 23); /* 255.255.254.0   */
    GOODMASK(0xffffff00, 24); /* 255.255.255.0   */
    GOODMASK(0xffffff80, 25); /* 255.255.255.128 */
    GOODMASK(0xffffffc0, 26); /* 255.255.255.192 */
    GOODMASK(0xffffffe0, 27); /* 255.255.255.224 */
    GOODMASK(0xfffffff0, 28); /* 255.255.255.240 */
    GOODMASK(0xfffffff8, 29); /* 255.255.255.248 */
    GOODMASK(0xfffffffc, 30); /* 255.255.255.252 */
    GOODMASK(0xfffffffe, 31); /* 255.255.255.254 */
    GOODMASK(0xffffffff, 32); /* 255.255.255.255 */

    BADMASK(0x01020304);

    BADPREFIX(-1);
    BADPREFIX(33);

    return RTTestSummaryAndDestroy(hTest);
}
