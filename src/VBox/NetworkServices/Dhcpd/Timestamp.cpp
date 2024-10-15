/* $Id: Timestamp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - timestamps
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "DhcpdInternal.h"
#include "Timestamp.h"


size_t Timestamp::strFormatHelper(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput) const RT_NOEXCEPT
{
    RTTIMESPEC TimeSpec;
    RTTIME     Time;
    char       szBuf[64];
    ssize_t    cchBuf = RTTimeToStringEx(RTTimeExplode(&Time, getAbsTimeSpec(&TimeSpec)), szBuf, sizeof(szBuf), 0);
    Assert(cchBuf > 0);
    return pfnOutput(pvArgOutput, szBuf, cchBuf);
}

