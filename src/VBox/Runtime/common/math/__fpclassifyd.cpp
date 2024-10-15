/* $Id: __fpclassifyd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - __fpclassifyd().
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
#define IPRT_NO_CRT_FOR_3RD_PARTY
#include "internal/nocrt.h"
#include <iprt/nocrt/math.h>
#include <iprt/assertcompile.h>
#include <iprt/assert.h>


#undef __fpclassifyd
int RT_NOCRT(__fpclassifyd)(double rd)
{
    AssertCompile(sizeof(rd) == sizeof(RTFLOAT64U));
    RTFLOAT64U u;
    u.rd = rd;
    if (RTFLOAT64U_IS_ZERO(&u))
        return RT_NOCRT_FP_ZERO;
    if (RTFLOAT64U_IS_NORMAL(&u))
        return RT_NOCRT_FP_NORMAL;
    if (RTFLOAT64U_IS_NAN(&u))
        return RT_NOCRT_FP_NAN;
    if (RTFLOAT64U_IS_INF(&u))
        return RT_NOCRT_FP_INFINITE;
    Assert(RTFLOAT64U_IS_SUBNORMAL(&u));
    return RT_NOCRT_FP_SUBNORMAL;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL_WITHOUT_UNDERSCORE(__fpclassifyd);

