/* $Id: RTSemEventGetResolution-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT -  Single Release Event Semaphores, RTSemEventGetResolution.
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
#define RTSEMEVENT_WITHOUT_REMAPPING
#ifdef IN_RING0
# include "../r0drv/nt/the-nt-kernel.h"
#else
# include <iprt/nt/nt.h>
#endif
#include <iprt/semaphore.h>

#include <iprt/assert.h>
#include <iprt/timer.h>
#ifdef IN_RING3
# include <iprt/system.h>
#endif


RTDECL(uint32_t) RTSemEventGetResolution(void)
{
    /*
     * We need to figure the KeWaitForSingleObject / NtWaitForSingleObject timeout
     * resolution, i.e. if we wish to wait for 1000ns how long are we likely to
     * actually wait before woken up.
     *
     * In older versions of NT, these timeout were implemented using KTIMERs and
     * have the same resolution as what them.  This should be found using
     * ExSetTimerResolution or NtQueryTimerResolution.
     *
     * Probably since windows 8.1 the value returned by NtQueryTimerResolution (and
     * set NtSetTimerResolution) have been virtualized and no longer reflects the
     * timer wheel resolution, at least from what I can tell. ExSetTimerResolution
     * still works as before, but it accesses variable that I cannot find out how
     * to access from user land.  So, kernel will get (and be able to set) the right
     * granularity, while in user land we'll be forced to reporting the max value.
     *
     * (The reason why I suspect it's since 8.1 is because the high resolution
     * ExSetTimer APIs were introduced back then.)
     */
#ifdef IN_RING0
    return RTTimerGetSystemGranularity();
#else
    ULONG cNtTicksMin = 0;
    ULONG cNtTicksMax = 0;
    ULONG cNtTicksCur = 0;
    NTSTATUS rcNt = NtQueryTimerResolution(&cNtTicksMin, &cNtTicksMax, &cNtTicksCur);
    if (NT_SUCCESS(rcNt))
    {
        Assert(cNtTicksMin >= cNtTicksMax);
        if (RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(6,3,9600)) /** @todo check when the switch happened, might be much later... */
            return cNtTicksMin * 100;
        return cNtTicksCur * 100;
    }
    AssertFailed();
    return 16 * RT_NS_1MS; /* the default on 64-bit windows 10 */
#endif
}

