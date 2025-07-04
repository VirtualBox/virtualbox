/* $Id: RTMpGetArraySize-generic.cpp 109339 2025-04-25 10:53:30Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Generic RTMpGetArraySize.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/mp.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/cpuset.h>


RTDECL(uint32_t) RTMpGetArraySize(void)
{
    /*
     * Cache the result here.  This whole point of this function is that it
     * will always return the same value, so that should be safe.
     *
     * Note! Because RTCPUSET may be to small to represent all the CPUs, we
     *       check with RTMpGetCount() as well.
     */
    static uint32_t s_cMaxCpus = 0;
    uint32_t cCpus = s_cMaxCpus;
    if (RT_UNLIKELY(cCpus == 0))
    {
        RTCPUSET    CpuSet;
        uint32_t    cCpus1 = RTCpuSetLastIndex(RTMpGetSet(&CpuSet)) + 1;
        uint32_t    cCpus2 = RTMpGetCount();
        cCpus              = RT_MAX(cCpus1, cCpus2);
        ASMAtomicCmpXchgU32(&s_cMaxCpus, cCpus, 0);
        return cCpus;
    }
    return s_cMaxCpus;

}
RT_EXPORT_SYMBOL(RTMpGetArraySize);

