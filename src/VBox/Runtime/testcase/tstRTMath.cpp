/* $Id: tstRTMath.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - base mathematics.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#include <iprt/test.h>

RT_C_DECLS_BEGIN
extern uint64_t __udivmoddi4(uint64_t u64A, uint64_t u64B, uint64_t *pu64R);
RT_C_DECLS_END

static struct {
    uint64_t u64A;
    uint64_t u64B;
    uint64_t u64Div;
    uint64_t u64Rem;
} gUDivMod[]
=
{
    {                           10,                            5,                            2,                            0 },
    {                (uint64_t)-10,                 (uint64_t)-5,                            0,                (uint64_t)-10 },
    { UINT64_C(0x7FFFFFFFFFFFFFFF),                            1, UINT64_C(0x7FFFFFFFFFFFFFFF),                            0 },
    { UINT64_C(0x7FFFFFFFFFFFFFFF), UINT64_C(0x7FFFFFFFFFFFFFFF),                            1,                            0 },
    { UINT64_C(0xFFFFFFFFFFFFFFFF),                            2, UINT64_C(0x7FFFFFFFFFFFFFFF),                            1 },
    {                            1,                            2,                            0,                            1 },
    { UINT64_C(0xFFFFFFFFFFFFFFFE), UINT64_C(0xFFFFFFFFFFFFFFFF),                            0, UINT64_C(0xFFFFFFFFFFFFFFFE) },
    { UINT64_C(0xEEEEEEEE12345678), UINT64_C(0x00000000EEEEEEEE),                  0x100000000,                   0x12345678 },
};

static void tstCorrectness(RTTEST hTest)
{
    RTTestSub(hTest, "Correctness");

    uint64_t u64Rem;
    for (unsigned i = 0; i < RT_ELEMENTS(gUDivMod); i++)
        RTTEST_CHECK(hTest, __udivmoddi4(gUDivMod[i].u64A, gUDivMod[i].u64B, &u64Rem) == gUDivMod[i].u64Div && u64Rem == gUDivMod[i].u64Rem);
}

int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTMath", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstCorrectness(hTest);

    return RTTestSummaryAndDestroy(hTest);
}
