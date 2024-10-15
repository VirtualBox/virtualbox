/* $Id: thread-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads, Ring-0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
#include "internal/iprt.h"
#include <iprt/thread.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>



RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)current_thread();
}


static int rtR0ThreadDarwinSleepCommon(RTMSINTERVAL cMillies)
{
    RT_ASSERT_PREEMPTIBLE();
    IPRT_DARWIN_SAVE_EFL_AC();

    uint64_t u64Deadline;
    clock_interval_to_deadline(cMillies, kMillisecondScale, &u64Deadline);
    clock_delay_until(u64Deadline);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies)
{
    return rtR0ThreadDarwinSleepCommon(cMillies);
}


RTDECL(int) RTThreadSleepNoLog(RTMSINTERVAL cMillies)
{
    return rtR0ThreadDarwinSleepCommon(cMillies);
}


RTDECL(bool) RTThreadYield(void)
{
    RT_ASSERT_PREEMPTIBLE();
    IPRT_DARWIN_SAVE_EFL_AC();

    thread_block(THREAD_CONTINUE_NULL);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return true; /* this is fishy */
}

