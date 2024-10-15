/* $Id: RTMpOn-r0drv-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor, Ring-0 Driver, Generic Stubs.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#include <iprt/errcore.h>


RTDECL(int) RTMpOnAll(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    NOREF(pfnWorker);
    NOREF(pvUser1);
    NOREF(pvUser2);
    return VERR_NOT_SUPPORTED;
}
RT_EXPORT_SYMBOL(RTMpOnAll);


RTDECL(bool) RTMpOnAllIsConcurrentSafe(void)
{
    return false;
}
RT_EXPORT_SYMBOL(RTMpOnAllIsConcurrentSafe);


RTDECL(int) RTMpOnOthers(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    NOREF(pfnWorker);
    NOREF(pvUser1);
    NOREF(pvUser2);
    return VERR_NOT_SUPPORTED;
}
RT_EXPORT_SYMBOL(RTMpOnOthers);


RTDECL(int) RTMpOnSpecific(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    NOREF(idCpu);
    NOREF(pfnWorker);
    NOREF(pvUser1);
    NOREF(pvUser2);
    return VERR_NOT_SUPPORTED;
}
RT_EXPORT_SYMBOL(RTMpOnSpecific);


RTDECL(int) RTMpOnPair(RTCPUID idCpu1, RTCPUID idCpu2, uint32_t fFlags, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    NOREF(idCpu1);
    NOREF(idCpu2);
    NOREF(fFlags);
    NOREF(pfnWorker);
    NOREF(pvUser1);
    NOREF(pvUser2);
    return VERR_NOT_SUPPORTED;
}
RT_EXPORT_SYMBOL(RTMpOnPair);



RTDECL(bool) RTMpOnPairIsConcurrentExecSupported(void)
{
    return false;
}
RT_EXPORT_SYMBOL(RTMpOnPairIsConcurrentExecSupported);

