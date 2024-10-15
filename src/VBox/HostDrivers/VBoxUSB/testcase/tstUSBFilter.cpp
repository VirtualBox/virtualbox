/** $Id: tstUSBFilter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB filter abstraction - testcase.
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
#include <VBox/usbfilter.h>
#include <iprt/errcore.h>

#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define TESTCASE "tstUSBFilter"

#define TST_CHECK_RC(expr) do { \
        int rc = expr; \
        if (RT_FAILURE(rc)) \
        { \
            RTPrintf( TESTCASE "(%d): %Rrc - %s\n", __LINE__, rc, #expr); \
            cErrors++; \
        } \
    } while (0)

#define TST_CHECK_EXPR(expr) do { \
    int rc = (intptr_t)(expr); \
    if (!rc) \
    { \
        RTPrintf( TESTCASE "(%d): %s -> %d\n", __LINE__, #expr, rc); \
        cErrors++; \
    } \
} while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char g_szString64[64+1] =
{
    "abcdefghijklmnopqrstuvwxyz012345"
    "abcdefghijklmnopqrstuvwxyz012345"
};

static const char g_szString128[128+1] =
{
    "abcdefghijklmnopqrstuvwxyz012345abcdefghijklmnopqrstuvwxyz012345"
    "abcdefghijklmnopqrstuvwxyz012345abcdefghijklmnopqrstuvwxyz012345"
};

static const char g_szString256[256+1] =
{
    "abcdefghijklmnopqrstuvwxyz012345abcdefghijklmnopqrstuvwxyz012345"
    "abcdefghijklmnopqrstuvwxyz012345abcdefghijklmnopqrstuvwxyz012345"
    "abcdefghijklmnopqrstuvwxyz012345abcdefghijklmnopqrstuvwxyz012345"
    "abcdefghijklmnopqrstuvwxyz012345abcdefghijklmnopqrstuvwxyz012345"
};


int main()
{
    unsigned cErrors = 0;
    RTR3InitExeNoArguments(0);

    /*
     * Basic property setting and simple matching.
     */
    USBFILTER Flt1;
    USBFilterInit(&Flt1, USBFILTERTYPE_CAPTURE);
    /* numbers */
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_VENDOR_ID, 0x1111, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_PRODUCT_ID, 0x2222, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_DEVICE, 0, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_DEVICE_CLASS, 0, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_DEVICE_SUB_CLASS, 0, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_DEVICE_PROTOCOL, 0xff, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_BUS, 1, true));
    TST_CHECK_RC(USBFilterSetIgnore(&Flt1, USBFILTERIDX_BUS));
    TST_CHECK_RC(USBFilterSetPresentOnly(&Flt1, USBFILTERIDX_BUS));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_BUS, 1, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_BUS, 1, false));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_PORT, 1, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_PORT, 1, false));
    TST_CHECK_RC(USBFilterSetIgnore(&Flt1, USBFILTERIDX_PORT));
    /* strings */
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR, "foobar", true, false ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR, "foobar", true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString128, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString128, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString128, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString128, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString128, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_PRODUCT_STR,       "barbar", true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_PRODUCT_STR,       g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_PRODUCT_STR,       g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_SERIAL_NUMBER_STR, g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_SERIAL_NUMBER_STR, g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_SERIAL_NUMBER_STR, g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_SERIAL_NUMBER_STR, g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_SERIAL_NUMBER_STR, g_szString64, true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_MANUFACTURER_STR,  "vendor", true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_PRODUCT_STR,       "product", true, false  ));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_SERIAL_NUMBER_STR, "serial", true, false  ));

    /* cloning */
    USBFILTER Dev;
    USBFilterClone(&Dev, &Flt1);

    TST_CHECK_EXPR(USBFilterIsIdentical(&Dev, &Flt1));
    TST_CHECK_EXPR(USBFilterMatch(&Dev, &Flt1));

    USBFilterDelete(&Flt1);
    USBFilterDelete(&Dev);

    /* make a sample device */
    USBFilterInit(&Dev, USBFILTERTYPE_CAPTURE);
    TST_CHECK_RC(USBFilterSetNumExact(&Dev,     USBFILTERIDX_VENDOR_ID,         0x1111, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Dev,     USBFILTERIDX_PRODUCT_ID,        0x2222, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Dev,     USBFILTERIDX_DEVICE,            0, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Dev,     USBFILTERIDX_DEVICE_CLASS,      0, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Dev,     USBFILTERIDX_DEVICE_SUB_CLASS,  0, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Dev,     USBFILTERIDX_DEVICE_PROTOCOL,   0xff, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Dev,     USBFILTERIDX_BUS,               1, true));
    TST_CHECK_RC(USBFilterSetNumExact(&Dev,     USBFILTERIDX_PORT,              2, true));
    TST_CHECK_RC(USBFilterSetStringExact(&Dev,  USBFILTERIDX_MANUFACTURER_STR,  "vendor", true, false ));
    TST_CHECK_RC(USBFilterSetStringExact(&Dev,  USBFILTERIDX_PRODUCT_STR,       "product", true, false ));
    TST_CHECK_RC(USBFilterSetStringExact(&Dev,  USBFILTERIDX_SERIAL_NUMBER_STR, "serial", true, false ));

    /* do some basic matching tests */
    USBFilterInit(&Flt1, USBFILTERTYPE_CAPTURE);
    TST_CHECK_EXPR(!USBFilterHasAnySubstatialCriteria(&Flt1));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev) /* 100% ignore filter */);

    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_PORT, 3, true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_PORT, 2, true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_BUS, 2, true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExact(&Flt1, USBFILTERIDX_BUS, 1, true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_PRODUCT_STR, "no match", true, false ));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringExact(&Flt1, USBFILTERIDX_PRODUCT_STR, "product", true, false ));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    /* string patterns */
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "p*", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "*product", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "product*", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "pro*t", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "pro*uct", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "pro*uct", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "pro*duct", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "pro*x", true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "*product*", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "*oduct*", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "*produc*", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "?r??u*?t", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "?r??u*?*?*?***??t", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "?r??u*?*?*?***??", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "p*d*t", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetStringPattern(&Flt1, USBFILTERIDX_PRODUCT_STR, "p*x*t", true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetIgnore(&Flt1, USBFILTERIDX_PRODUCT_STR));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    /* numeric patterns */
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x1111", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0X1111", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "4369", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "010421", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x1111-0x1111", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "4369-4369", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "010421-010421", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x1110-0x1112", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "4360-4370", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "010420-010422", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x1112-0x1110", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x0-0x1f", true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0-19", true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0-017", true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x0-0xffff", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0-65535", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0-177777", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x0-0XABCD", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x0EF-0XABCD", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0X0ef-0Xabcd", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "42|1|0x1111", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "42|0x1111|1", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x1111|42|1", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x1112|42|1", true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));

    /* numeric patterns - interval filters */
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "int:0x0-0xffff", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "int: 0x0 - 0xffff ", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_PRODUCT_ID, "int:0x0028-", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_DEVICE_REV, "int:-0x0045", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_PORT, "int:1,4", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_PORT, "int:( 1, 3 )", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));

    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "39-59|0x256-0x101f|0xfffff-0xf000|0x1000-0x2000", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "0x000256-0x0101f|0xf000-0xfffff|0x000008000-0x2000|39-59", true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "| | \t \t\t| 0x256 - 0x101f   | 0xf000 - 0xfeff\t| 0x1000 -\t0x6000 | 1- 0512", true));
    TST_CHECK_EXPR(USBFilterMatch(&Flt1, &Dev));
    TST_CHECK_RC(USBFilterSetNumExpression(&Flt1, USBFILTERIDX_VENDOR_ID, "| | \t \t\t| 0x256 - 0x101f   | 0xf000 - 0xfeff\t| 0x1112 -\t0x6000 | 1- 0512", true));
    TST_CHECK_EXPR(!USBFilterMatch(&Flt1, &Dev));


    USBFilterDelete(&Flt1);

    /*
     * string overflow
     */
    struct
    {
        uint64_t  u64Pre;
        USBFILTER Flt;
        uint64_t  u64Post;
    } sOf;
    sOf.u64Pre = sOf.u64Post = UINT64_C(0x1234567887654321);

    USBFilterInit(&sOf.Flt, USBFILTERTYPE_CAPTURE);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));

    AssertCompileMemberSize(USBFILTER, achStrTab, 256);
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_SERIAL_NUMBER_STR,   &g_szString256[0],  true, false  ) == VERR_BUFFER_OVERFLOW);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_SERIAL_NUMBER_STR,   &g_szString256[1],  true, false  ) == VERR_BUFFER_OVERFLOW);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_SERIAL_NUMBER_STR,   &g_szString256[2],  true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_SERIAL_NUMBER_STR,   &g_szString256[3],  true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    /* 0 + 1 */
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_SERIAL_NUMBER_STR,   "",                 true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_PRODUCT_STR,         &g_szString256[2],  true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_PRODUCT_STR,         &g_szString256[1],  true, false  ) == VERR_BUFFER_OVERFLOW);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    /* 0 + 2 */
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_PRODUCT_STR,         &g_szString128[2],  true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_SERIAL_NUMBER_STR,   &g_szString128[1],  true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    /* 3 */
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_SERIAL_NUMBER_STR,   &g_szString64[0],   true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_PRODUCT_STR,         &g_szString64[0],   true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_MANUFACTURER_STR,    &g_szString128[4],  true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_MANUFACTURER_STR,    &g_szString128[4],  true, false  ) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetStringExact(&sOf.Flt, USBFILTERIDX_MANUFACTURER_STR,    &g_szString128[3],  true, false  ) == VERR_BUFFER_OVERFLOW);
    TST_CHECK_EXPR(sOf.u64Pre == UINT64_C(0x1234567887654321)); TST_CHECK_EXPR(sOf.u64Post == UINT64_C(0x1234567887654321));

    /*
     * Check for a string replacement bug.
     */
    USBFILTER Dev2;
    USBFilterInit(&Dev2, USBFILTERTYPE_CAPTURE);
    TST_CHECK_EXPR(USBFilterSetNumExact(&Dev2,      USBFILTERIDX_VENDOR_ID,         0x19b6, true) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetNumExact(&Dev2,      USBFILTERIDX_PRODUCT_ID,        0x1024, true) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetNumExact(&Dev2,      USBFILTERIDX_DEVICE_REV,        0x0141, true) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetNumExact(&Dev2,      USBFILTERIDX_DEVICE_CLASS,      0,      true) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetNumExact(&Dev2,      USBFILTERIDX_DEVICE_SUB_CLASS,  0,      true) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetNumExact(&Dev2,      USBFILTERIDX_DEVICE_PROTOCOL,   0,      true) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetNumExact(&Dev2,      USBFILTERIDX_PORT,              0x1,    true) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetStringExact(&Dev2,   USBFILTERIDX_MANUFACTURER_STR, "Generic", true, false ) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetStringExact(&Dev2,   USBFILTERIDX_PRODUCT_STR, "Mass Storage Device", true, false ) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterSetStringExact(&Dev2,   USBFILTERIDX_MANUFACTURER_STR, "YBU1PPRS", true, false ) == VINF_SUCCESS);
    TST_CHECK_EXPR(USBFilterGetNum(&Dev2, USBFILTERIDX_VENDOR_ID) == 0x19b6);
    TST_CHECK_EXPR(USBFilterGetNum(&Dev2, USBFILTERIDX_PRODUCT_ID) == 0x1024);
    TST_CHECK_EXPR(USBFilterGetNum(&Dev2, USBFILTERIDX_DEVICE_REV) == 0x0141);
    TST_CHECK_EXPR(USBFilterGetNum(&Dev2, USBFILTERIDX_DEVICE_CLASS) == 0);
    TST_CHECK_EXPR(USBFilterGetNum(&Dev2, USBFILTERIDX_DEVICE_SUB_CLASS) == 0);
    TST_CHECK_EXPR(USBFilterGetNum(&Dev2, USBFILTERIDX_DEVICE_PROTOCOL) == 0);
    TST_CHECK_EXPR(USBFilterGetNum(&Dev2, USBFILTERIDX_PORT) == 1);


    /*
     * Summary.
     */
    if (!cErrors)
        RTPrintf(TESTCASE ": SUCCESS\n");
    else
        RTPrintf(TESTCASE ": FAILURE - %d errors\n", cErrors);
    return !!cErrors;
}
