/* $Id: ntsetfreq.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Set the NT timer frequency.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <iprt/nt/nt.h>

#include <iprt/initterm.h>
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/errcore.h>


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse arguments.
     */
    bool        fVerbose   = true;
    uint32_t    u32NewRes  = 0;
    uint32_t    cSecsSleep = UINT32_MAX;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--resolution",  'r', RTGETOPT_REQ_UINT32 },
        { "--sleep",       's', RTGETOPT_REQ_UINT32 },
        { "--quiet",       'q', RTGETOPT_REQ_NOTHING },
        { "--verbose",     'v', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            case 'r':
                u32NewRes = ValueUnion.u32;
                if (u32NewRes > 16*10000 /* 16 ms */ || u32NewRes < 1000 /* 100 microsec */)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                          "syntax error: the new timer resolution (%RU32) is out of range\n",
                                          u32NewRes);
                break;

            case 's':
                cSecsSleep = ValueUnion.u32;
                break;

            case 'q':
                fVerbose = false;
                break;

            case 'v':
                fVerbose = true;
                break;

            case 'h':
                RTPrintf("Usage: ntsetfreq [-q|--quiet] [-v|--verbose] [-r|--resolution <100ns>] [-s|--sleep <1s>]\n");
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }


    /*
     * Query the current resolution.
     */
    ULONG    Cur = UINT32_MAX;
    ULONG    Min = UINT32_MAX;
    ULONG    Max = UINT32_MAX;
    NTSTATUS rcNt = STATUS_SUCCESS;
    if (fVerbose || !u32NewRes)
    {
        rcNt = NtQueryTimerResolution(&Min, &Max, &Cur);
        if (NT_SUCCESS(rcNt))
            RTMsgInfo("cur: %u (%u.%02u Hz)  min: %u (%u.%02u Hz)  max: %u (%u.%02u Hz)\n",
                      Cur, 10000000 / Cur, (10000000 / (Cur * 100)) % 100,
                      Min, 10000000 / Min, (10000000 / (Min * 100)) % 100,
                      Max, 10000000 / Max, (10000000 / (Max * 100)) % 100);
        else
            RTMsgError("NTQueryTimerResolution failed with status %#x\n", rcNt);
    }

    if (u32NewRes)
    {
        rcNt = NtSetTimerResolution(u32NewRes, TRUE, &Cur);
        if (!NT_SUCCESS(rcNt))
            RTMsgError("NTSetTimerResolution(%RU32,,) failed with status %#x\n", u32NewRes, rcNt);
        else if (fVerbose)
        {
            Cur = Min = Max = UINT32_MAX;
            rcNt = NtQueryTimerResolution(&Min, &Max, &Cur);
            if (NT_SUCCESS(rcNt))
                RTMsgInfo("new: %u (%u.%02u Hz) requested %RU32 (%u.%02u Hz)\n",
                          Cur, 10000000 / Cur, (10000000 / (Cur * 100)) % 100,
                          u32NewRes, 10000000 / u32NewRes, (10000000 / (u32NewRes * 100)) % 100);
            else
                RTMsgError("NTSetTimerResolution succeeded but the NTQueryTimerResolution call failed with status %#x (ignored)\n",
                           rcNt);
            rcNt = STATUS_SUCCESS;
        }
    }

    if (u32NewRes && NT_SUCCESS(rcNt))
    {
        if (cSecsSleep == UINT32_MAX)
            for (;;)
                RTThreadSleep(RT_INDEFINITE_WAIT);
        else
            while (cSecsSleep-- > 0)
                RTThreadSleep(1000);
    }

    return NT_SUCCESS(rcNt) ? 0 : 1;
}

