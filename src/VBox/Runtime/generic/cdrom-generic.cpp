/* $Id: cdrom-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - CD/DVD/BD-ROM Drive, Generic.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/cdrom.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>


RTDECL(int) RTCdromOpen(const char *psz, uint32_t fFlags, PRTCDROM phCdrom)
{
    RT_NOREF_PV(psz); RT_NOREF_PV(fFlags); RT_NOREF_PV(phCdrom);
    return VERR_NOT_IMPLEMENTED;
}


RTDECL(uint32_t) RTCdromRetain(RTCDROM hCdrom)
{
    RT_NOREF_PV(hCdrom);
    AssertFailedReturn(UINT32_MAX);
}


RTDECL(uint32_t)    RTCdromRelease(RTCDROM hCdrom)
{
    RT_NOREF_PV(hCdrom);
    AssertFailedReturn(UINT32_MAX);
}


RTDECL(int) RTCdromQueryMountPoint(RTCDROM hCdrom, char *pszMountPoint, size_t cbMountPoint)
{
    RT_NOREF_PV(hCdrom);
    RT_NOREF_PV(pszMountPoint);
    RT_NOREF_PV(cbMountPoint);
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}


RTDECL(int)         RTCdromUnmount(RTCDROM hCdrom)
{
    RT_NOREF_PV(hCdrom);
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}


RTDECL(int)         RTCdromEject(RTCDROM hCdrom, bool fForce)
{
    RT_NOREF_PV(hCdrom);
    RT_NOREF_PV(fForce);
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}


RTDECL(int)         RTCdromLock(RTCDROM hCdrom)
{
    RT_NOREF_PV(hCdrom);
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}


RTDECL(int)         RTCdromUnlock(RTCDROM hCdrom)
{
    RT_NOREF_PV(hCdrom);
    AssertFailedReturn(VERR_NOT_IMPLEMENTED);
}


RTDECL(unsigned)    RTCdromCount(void)
{
    return 0;
}

RTDECL(int)         RTCdromOrdinalToName(unsigned iCdrom, char *pszName, size_t cbName)
{
    RT_NOREF_PV(iCdrom);
    if (cbName)
        *pszName = '\0';
    return VERR_OUT_OF_RANGE;
}


RTDECL(int)         RTCdromOpenByOrdinal(unsigned iCdrom, uint32_t fFlags, PRTCDROM phCdrom)
{
    RT_NOREF_PV(iCdrom);
    RT_NOREF_PV(fFlags);
    RT_NOREF_PV(phCdrom);
    return VERR_OUT_OF_RANGE;
}

