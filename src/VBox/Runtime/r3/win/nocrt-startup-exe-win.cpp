/* $Id: nocrt-startup-exe-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - Windows EXE startup code.
 *
 * @note Does not run static constructors and destructors!
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
#include "internal/nocrt.h"
#include "internal/process.h"

#include <iprt/nt/nt-and-windows.h>
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include "internal/compiler-vcc.h"


/*********************************************************************************************************************************
*   External Symbols                                                                                                             *
*********************************************************************************************************************************/
extern int main(int argc, char **argv, char **envp);    /* in program */
#ifndef IPRT_NO_CRT
extern DECLHIDDEN(void) InitStdHandles(PRTL_USER_PROCESS_PARAMETERS pParams); /* nocrt-streams-win.cpp */ /** @todo put in header */
#endif


static int rtTerminateProcess(int32_t rcExit, bool fDoAtExit)
{
#ifdef IPRT_NO_CRT
    /*
     * Run atexit callback in reverse order.
     */
    if (fDoAtExit)
    {
        rtVccTermRunAtExit();
        rtVccInitializersRunTerm();
    }
#else
    RT_NOREF(fDoAtExit);
#endif

    /*
     * Terminate.
     */
    for (;;)
#if 1 /* Using NtTerminateProcess triggers heuristics in some annoying AV scanner.  */
        TerminateProcess(NtCurrentProcess(), rcExit);
#else
        NtTerminateProcess(NtCurrentProcess(), rcExit);
#endif
}


DECLASM(void) CustomMainEntrypoint(void)
{
    /* Looks like might have gotten the PPEB as parameter here before NT4,
       however, there the EXE entry function clearly takes no parameters.
       So, we have to retrieve the PEB our selves here. */
    PPEB_COMMON const pPeb = RTNtCurrentPeb();

    /*
     * Initialize stuff.
     */
#ifdef IPRT_NO_CRT
# ifdef RT_ARCH_X86
    rtVccWinInitBssOnNt3(pPeb->ImageBaseAddress);
# endif
    rtVccInitSecurityCookie();
#else
    InitStdHandles(pPeb->ProcessParameters);
#endif
    rtVccWinInitProcExecPath();

    RTEXITCODE rcExit;
#ifdef IPRT_NO_CRT
    AssertCompile(sizeof(rcExit) == sizeof(int));
    rcExit = (RTEXITCODE)rtVccInitializersRunInit();
    if (rcExit == RTEXITCODE_SUCCESS)
#endif
    {
        /*
         * Get and convert the command line to argc/argv format.
         */
        rcExit = RTEXITCODE_INIT;
        UNICODE_STRING const *pCmdLine = pPeb->ProcessParameters ? &pPeb->ProcessParameters->CommandLine : NULL;
        if (pCmdLine)
        {
            char *pszCmdLine = NULL;
            int rc = RTUtf16ToUtf8Ex(pCmdLine->Buffer, pCmdLine->Length / sizeof(WCHAR), &pszCmdLine, 0, NULL);
            if (RT_SUCCESS(rc))
            {
                char **papszArgv;
                int    cArgs = 0;
                rc = RTGetOptArgvFromString(&papszArgv, &cArgs, pszCmdLine,
                                            RTGETOPTARGV_CNV_MODIFY_INPUT | RTGETOPTARGV_CNV_QUOTE_MS_CRT,  NULL);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Call the main function.
                     */
                    AssertCompile(sizeof(rcExit) == sizeof(int));
                    rcExit = (RTEXITCODE)main(cArgs, papszArgv, NULL /*envp*/);
                }
                else
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
                    RTMsgError("Error parsing command line: %Rrc\n", rc);
#else
                    rtNoCrtFatalMsgWithRc(RT_STR_TUPLE("Error parsing command line: "), rc);
#endif
            }
            else
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
                RTMsgError("Failed to convert command line to UTF-8: %Rrc\n", rc);
#else
                rtNoCrtFatalMsgWithRc(RT_STR_TUPLE("Failed to convert command line to UTF-8: "), rc);
#endif
        }
        else
#ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
            RTMsgError("No command line\n");
#else
            rtNoCrtFatalMsg(RT_STR_TUPLE("No command line\r\n"));
#endif
        rtTerminateProcess(rcExit, true /*fDoAtExit*/);
    }
#ifdef IPRT_NO_CRT
    else
    {
# ifdef IPRT_NOCRT_WITHOUT_FATAL_WRITE
        RTMsgError("A C static initializor failed (%d)\n", rcExit);
# else
        rtNoCrtFatalWriteBegin(RT_STR_TUPLE("A C static initializor failed ("));
        rtNoCrtFatalWriteWinRc(rcExit);
        rtNoCrtFatalWriteEnd(RT_STR_TUPLE("\r\n"));
# endif
        rtTerminateProcess(rcExit, false /*fDoAtExit*/);
    }
#endif
}

