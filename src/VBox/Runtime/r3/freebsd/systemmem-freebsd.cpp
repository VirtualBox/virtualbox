/* $Id: systemmem-freebsd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemQueryTotalRam, Linux ring-3.
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
#include <iprt/system.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>


RTDECL(int) RTSystemQueryTotalRam(uint64_t *pcb)
{
    int rc = VINF_SUCCESS;
    u_long cbMemPhys = 0;
    size_t cbParameter = sizeof(cbMemPhys);

    AssertPtrReturn(pcb, VERR_INVALID_POINTER);

    if (!sysctlbyname("hw.physmem", &cbMemPhys, &cbParameter, NULL, 0))
    {
        *pcb = cbMemPhys;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}


RTDECL(int) RTSystemQueryAvailableRam(uint64_t *pcb)
{
    AssertPtrReturn(pcb, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    u_int cPagesMemFree = 0;
    u_int cPagesMemInactive = 0;
    u_int cPagesMemCached = 0;
    u_int cPagesMemUsed = 0;
    int cbPage = 0;
    size_t cbParameter;
    int cProcessed = 0;

    cbParameter = sizeof(cPagesMemFree);
    if (sysctlbyname("vm.stats.vm.v_free_count", &cPagesMemFree, &cbParameter, NULL, 0))
        rc = RTErrConvertFromErrno(errno);
    cbParameter = sizeof(cPagesMemUsed);
    if (   RT_SUCCESS(rc)
        && sysctlbyname("vm.stats.vm.v_active_count", &cPagesMemUsed, &cbParameter, NULL, 0))
        rc = RTErrConvertFromErrno(errno);
    cbParameter = sizeof(cPagesMemInactive);
    if (   RT_SUCCESS(rc)
        && sysctlbyname("vm.stats.vm.v_inactive_count", &cPagesMemInactive, &cbParameter, NULL, 0))
        rc = RTErrConvertFromErrno(errno);
    cbParameter = sizeof(cPagesMemCached);
    if (   RT_SUCCESS(rc)
        && sysctlbyname("vm.stats.vm.v_cache_count", &cPagesMemCached, &cbParameter, NULL, 0))
        rc = RTErrConvertFromErrno(errno);
    cbParameter = sizeof(cbPage);
    if (   RT_SUCCESS(rc)
        && sysctlbyname("hw.pagesize", &cbPage, &cbParameter, NULL, 0))
        rc = RTErrConvertFromErrno(errno);

    if (RT_SUCCESS(rc))
        *pcb = (cPagesMemFree + cPagesMemInactive + cPagesMemCached ) * cbPage;

    return rc;
}

