/* $Id: atoi.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - atoi.
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
#include <iprt/nocrt/stdlib.h>
#include <iprt/nocrt/limits.h>
#include <iprt/string.h>


#undef atoi
int RT_NOCRT(atoi)(const char *psz)
{
#if INT_MAX == INT32_MAX
    int32_t iValue = 0;
    int rc = RTStrToInt32Ex(RTStrStripL(psz), NULL, 10, &iValue);
#else
# error "Unsupported integer size"
#endif
    if (rc == VINF_SUCCESS || rc == VWRN_TRAILING_CHARS || rc == VWRN_TRAILING_SPACES)
        return iValue;
    if (rc == VWRN_NUMBER_TOO_BIG)
        return iValue < 0 ? INT_MIN : INT_MAX;
    return 0;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(atoi);

