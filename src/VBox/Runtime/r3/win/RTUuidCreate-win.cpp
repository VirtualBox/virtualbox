/* $Id: RTUuidCreate-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - UUID, Windows RTUuidCreate implementation.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_UUID
#include <iprt/win/windows.h>

#include <iprt/uuid.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/rand.h>

#include "internal-r3-win.h"


RTDECL(int)  RTUuidCreate(PRTUUID pUuid)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);

    /*
     * When using the UuidCreate API shortly after boot on NT 3.1 it typcially
     * hangs for a long long time while polling for some service to start.
     * What then usually happens next is a failure because it couldn't figure
     * out the MAC address of the NIC.  So, on NT 3.1 we always use the fallback.
     */
    if (g_enmWinVer != kRTWinOSType_NT310)
    {
        RPC_STATUS rc = UuidCreate((UUID *)pUuid);
        if (   rc == RPC_S_OK
            || rc == RPC_S_UUID_LOCAL_ONLY)
            return VINF_SUCCESS;
        AssertMsg(rc == RPC_S_UUID_NO_ADDRESS, ("UuidCreate -> %u (%#x)\n", rc, rc));
    }

    /*
     * Use generic implementation as fallback (copy of RTUuidCreate-generic.cpp).
     */
    RTRandBytes(pUuid, sizeof(*pUuid));
    pUuid->Gen.u8ClockSeqHiAndReserved = (pUuid->Gen.u8ClockSeqHiAndReserved & 0x3f) | 0x80;
    pUuid->Gen.u16TimeHiAndVersion     = (pUuid->Gen.u16TimeHiAndVersion & 0x0fff) | 0x4000;
    return VINF_SUCCESS;
}

