/* $Id: tstIprtMiniList.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTCList.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#include <iprt/test.h>
#include <iprt/cpp/list.h>
#include <iprt/cpp/ministring.h>


int main()
{
    RTTEST      hTest;
    RTEXITCODE  rcExit = RTTestInitAndCreate("tstIprtMiniList", &hTest);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        RTTestBanner(hTest);

        /*
         * Test == and != operators.
         */
        RTCList<uint8_t> u8ListEmpty1(1);
        RTCList<uint8_t> u8ListEmpty2(2);
        RTTESTI_CHECK(u8ListEmpty1 == u8ListEmpty2);

        RTCList<uint8_t *> u8PtrListEmpty1;
        RTCList<uint8_t *> u8PtrListEmpty2(42);
        RTTESTI_CHECK(u8PtrListEmpty1 == u8PtrListEmpty2);

        uint8_t a, b;
        RTCList<uint8_t *> u8PtrListA; u8PtrListA.append(&a);
        RTCList<uint8_t *> u8PtrListB; u8PtrListB.append(&b);
        RTTESTI_CHECK(u8PtrListA == u8PtrListA);
        RTTESTI_CHECK(u8PtrListA != u8PtrListB);

        RTCList<RTCString> spList1 = RTCString("##abcdef##abcdef####abcdef##").split("##", RTCString::RemoveEmptyParts);
        RTCList<RTCString> spList2 = RTCString("##abcdef##abcdef####abcdef##").split("##", RTCString::RemoveEmptyParts);
        RTCList<RTCString> spList3 = RTCString("").split("##", RTCString::RemoveEmptyParts);
        RTCList<RTCString> spList4 = RTCString("##abcdef##qwer####abcdef##").split("##", RTCString::RemoveEmptyParts);

        RTTESTI_CHECK(spList1 == spList1);
        RTTESTI_CHECK(spList1 == spList2);
        RTTESTI_CHECK(spList1 != spList3);
        RTTESTI_CHECK(spList1 != spList4);

        /*
         * Test filtering.
         */
        /* Basics. */
        RTCList<RTCString> spListFiltered;
        spListFiltered.filter(RTCString("").split(",")); /* Empty filter. */
        /* String list. */
        spListFiltered = RTCString("filter-out1,filter-out2,foo").split(",");
        spListFiltered.filter(RTCString("filter-out1,filter-out2").split(","));
        RTTESTI_CHECK(spListFiltered == RTCString("foo").split(","));
        /* Repeat. */
        spListFiltered.filter(RTCString("filter-out1,filter-out2").split(","));
        RTTESTI_CHECK(spListFiltered == RTCString("foo").split(","));
        RTTESTI_CHECK(spListFiltered != RTCString("bar").split(","));

        rcExit = RTTestSummaryAndDestroy(hTest);
    }
    return rcExit;
}

