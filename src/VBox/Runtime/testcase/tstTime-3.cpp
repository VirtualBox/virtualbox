/* $Id: tstTime-3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Simple RTTime test.
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
#include <iprt/time.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/thread.h>
#include <iprt/errcore.h>



int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_SUPLIB);

    if (argc <= 1)
    {
        RTPrintf("tstTime-3: usage: tstTime-3 <seconds> [seconds2 [..]]\n");
        return 1;
    }

    RTPrintf("tstTime-3: Testing difference between RTTimeNanoTS() and RTTimeSystemNanoTS()...\n");

    for (int i = 1; i < argc; i++)
    {
        uint64_t cSeconds = 0;
        int rc = RTStrToUInt64Ex(argv[i], NULL, 0, &cSeconds);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstTime-3: Invalid argument %d: %s\n", i, argv[i]);
            return 1;
        }
        RTPrintf("tstTime-3: %d - %RU64 seconds period...\n", i, cSeconds);

        RTTimeNanoTS(); RTTimeSystemNanoTS(); RTThreadSleep(1);
        uint64_t u64RTStartTS = RTTimeNanoTS();
        uint64_t u64OSStartTS = RTTimeSystemNanoTS();

        RTThreadSleep(cSeconds * 1000);

        uint64_t u64RTElapsedTS = RTTimeNanoTS();
        uint64_t u64OSElapsedTS = RTTimeSystemNanoTS();
        u64RTElapsedTS -= u64RTStartTS;
        u64OSElapsedTS -= u64OSStartTS;

        RTPrintf("tstTime-3: %d -   RT: %16RU64 ns\n", i, u64RTElapsedTS);
        RTPrintf("tstTime-3: %d -   OS: %16RU64 ns\n", i, u64OSElapsedTS);
        RTPrintf("tstTime-3: %d - diff: %16RI64 ns\n", i, u64RTElapsedTS - u64OSElapsedTS);
    }

    return 0;
}

