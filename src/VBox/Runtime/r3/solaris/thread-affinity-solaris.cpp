/* $Id: thread-affinity-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Thread Affinity, Solaris ring-3 implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/thread.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/cpuset.h>
#include <iprt/err.h>
#include <iprt/mp.h>

#include <sys/types.h>
#include <sys/processor.h>
#include <sys/procset.h>
#include <unistd.h>
#include <errno.h>


/* Note! The current implementation can only bind to a single CPU. */


RTR3DECL(int) RTThreadSetAffinity(PCRTCPUSET pCpuSet)
{
    int rc;
    if (pCpuSet == NULL)
        rc = processor_bind(P_LWPID, P_MYID, PBIND_NONE, NULL);
    else
    {
        RTCPUSET PresentSet;
        int cCpusInSet = RTCpuSetCount(pCpuSet);
        if (cCpusInSet == 1)
        {
            unsigned iCpu = 0;
            while (   iCpu < RTCPUSET_MAX_CPUS
                   && !RTCpuSetIsMemberByIndex(pCpuSet, iCpu))
                iCpu++;
            rc = processor_bind(P_LWPID, P_MYID, iCpu, NULL);
        }
        else if (   cCpusInSet == RTCPUSET_MAX_CPUS
                 || RTCpuSetIsEqual(pCpuSet, RTMpGetPresentSet(&PresentSet)))
            rc = processor_bind(P_LWPID, P_MYID, PBIND_NONE, NULL);
        else
            return VERR_NOT_SUPPORTED;
    }
    if (!rc)
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTThreadGetAffinity(PRTCPUSET pCpuSet)
{
    processorid_t iOldCpu;
    int rc = processor_bind(P_LWPID, P_MYID, PBIND_QUERY, &iOldCpu);
    if (rc)
        return RTErrConvertFromErrno(errno);
    if (iOldCpu == PBIND_NONE)
        RTMpGetPresentSet(pCpuSet);
    else
    {
        RTCpuSetEmpty(pCpuSet);
        if (RTCpuSetAdd(pCpuSet, iOldCpu) != 0)
            return VERR_INTERNAL_ERROR_5;
    }
    return VINF_SUCCESS;
}

