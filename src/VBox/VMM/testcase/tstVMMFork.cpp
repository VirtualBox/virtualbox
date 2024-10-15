/* $Id: tstVMMFork.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM Fork Test.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmm.h>
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>

#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define TESTCASE        "tstVMMFork"
#define AUTO_TEST_ARGS  1

VMMR3DECL(int) VMMDoTest(PVM pVM);


int main(int argc, char* argv[])
{
    int rcErrors = 0;

    /*
     * Initialize the runtime.
     */
    RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);

#ifndef AUTO_TEST_ARGS
    if (argc < 2)
    {
        RTPrintf("syntax: %s command [args]\n"
                    "\n"
                    "command    Command to run under child process in fork.\n"
                    "[args]     Arguments to command.\n", argv[0]);
        return 1;
    }
#endif

    /*
     * Create empty VM.
     */
    RTPrintf(TESTCASE ": Initializing...\n");
    PVM pVM;
    PUVM pUVM;
    int rc = VMR3Create(1 /*cCpus*/, NULL, 0 /*fFlags*/, NULL, NULL, NULL, NULL, &pVM, &pUVM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do testing.
         */
        int iCowTester = 0;
        char cCowTester = 'a';

#ifndef AUTO_TEST_ARGS
        int cArgs = argc - 1;
        char **ppszArgs = &argv[1];
#else
        int cArgs = 2;
        char *ppszArgs[3];
        ppszArgs[0] = (char *)"/bin/sleep";
        ppszArgs[1] = (char *)"3";
        ppszArgs[2] = NULL;
#endif

        RTPrintf(TESTCASE ": forking current process...\n");
        pid_t pid = fork();
        if (pid < 0)
        {
            /* Bad. fork() failed! */
            RTPrintf(TESTCASE ": error: fork() failed.\n");
            rcErrors++;
        }
        else if (pid == 0)
        {
            /*
             * The child process.
             * Write to some local variables to trigger copy-on-write if it's used.
             */
            RTPrintf(TESTCASE ": running child process...\n");
            RTPrintf(TESTCASE ": writing local variables...\n");
            iCowTester = 2;
            cCowTester = 'z';

            RTPrintf(TESTCASE ": calling execv() with command-line:\n");
            for (int i = 0; i < cArgs; i++)
                RTPrintf(TESTCASE ": ppszArgs[%d]=%s\n", i, ppszArgs[i]);
            execv(ppszArgs[0], ppszArgs);
            RTPrintf(TESTCASE ": error: execv() returned to caller. errno=%d.\n", errno);
            _exit(-1);
        }
        else
        {
            /*
             * The parent process.
             * Wait for child & run VMM test to ensure things are fine.
             */
            int result;
            while (waitpid(pid, &result, 0) < 0)
                ;
            if (!WIFEXITED(result) || WEXITSTATUS(result) != 0)
            {
                RTPrintf(TESTCASE ": error: failed to run child process. errno=%d\n", errno);
                rcErrors++;
            }

            if (rcErrors == 0)
            {
                RTPrintf(TESTCASE ": fork() returned fine.\n");
                RTPrintf(TESTCASE ": testing VM after fork.\n");
                VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)VMMDoTest, 1, pVM);

                STAMR3Dump(pUVM, "*");
            }
        }

        if (rcErrors > 0)
            RTPrintf(TESTCASE ": error: %d error(s) during fork(). Cannot proceed to test the VM.\n", rcErrors);
        else
            RTPrintf(TESTCASE ": fork() and VM test, SUCCESS.\n");

        /*
         * Cleanup.
         */
        rc = VMR3PowerOff(pUVM);
        if (!RT_SUCCESS(rc))
        {
            RTPrintf(TESTCASE ": error: failed to power off vm! rc=%Rrc\n", rc);
            rcErrors++;
        }
        rc = VMR3Destroy(pUVM);
        if (!RT_SUCCESS(rc))
        {
            RTPrintf(TESTCASE ": error: failed to destroy vm! rc=%Rrc\n", rc);
            rcErrors++;
        }
        VMR3ReleaseUVM(pUVM);
    }
    else
    {
        RTPrintf(TESTCASE ": fatal error: failed to create vm! rc=%Rrc\n", rc);
        rcErrors++;
    }

    return rcErrors;
}
