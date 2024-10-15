/* $Id: systemmem-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemQueryTotalRam, darwin ring-3.
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

#include <errno.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <mach/mach.h>



RTDECL(int) RTSystemQueryTotalRam(uint64_t *pcb)
{
    AssertPtrReturn(pcb, VERR_INVALID_POINTER);

    int aiMib[2];
    aiMib[0] = CTL_HW;
    aiMib[1] = HW_MEMSIZE;
    uint64_t    cbPhysMem = UINT64_MAX;
    size_t      cb = sizeof(cbPhysMem);
    int rc = sysctl(aiMib, RT_ELEMENTS(aiMib), &cbPhysMem, &cb, NULL, 0);
    if (rc == 0)
    {
        *pcb = cbPhysMem;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}


RTDECL(int) RTSystemQueryAvailableRam(uint64_t *pcb)
{
    AssertPtrReturn(pcb, VERR_INVALID_POINTER);

    static mach_port_t volatile s_hSelfPort = 0;
    mach_port_t hSelfPort = s_hSelfPort;
    if (hSelfPort == 0)
        s_hSelfPort = hSelfPort = mach_host_self();

    vm_statistics_data_t    VmStats;
    mach_msg_type_number_t  cItems = sizeof(VmStats) / sizeof(natural_t);

    kern_return_t krc = host_statistics(hSelfPort, HOST_VM_INFO, (host_info_t)&VmStats, &cItems);
    if (krc == KERN_SUCCESS)
    {
        uint64_t cPages = VmStats.inactive_count;
        cPages += VmStats.free_count;
        *pcb = cPages * PAGE_SIZE;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromDarwin(krc);
}

