/* $Id: llroundl.cpp 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - No-CRT - llroundl().
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
#include <iprt/nocrt/limits.h>
#include <iprt/nocrt/fenv.h>


#undef llroundl
long long RT_NOCRT(llroundl)(long double lrd)
{
    if (isfinite(lrd))
    {
        lrd = RT_NOCRT(roundl)(lrd);
        if (lrd >= (long double)LLONG_MIN && lrd <= (long double)LLONG_MAX)
            return (long long)lrd;
        RT_NOCRT(feraiseexcept)(FE_INVALID);
        return lrd > 0.0L ? LLONG_MAX : LLONG_MIN;
    }
    RT_NOCRT(feraiseexcept)(FE_INVALID);
    if (RT_NOCRT(__isinfl)(lrd) && lrd < 0.0)
        return LLONG_MIN;
    return LLONG_MAX;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(llroundl);

