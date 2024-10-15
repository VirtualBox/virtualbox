/* $Id: tstVMM-HM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM Testcase.
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
#include <VBox/vmm/cpum.h>
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define TESTCASE    "tstVMM-Hm"

VMMR3DECL(int) VMMDoHmTest(PVM pVM);


#if 0
static DECLCALLBACK(int) tstVmmHmConfigConstructor(PUVM pUVM, PVM pVM, void *pvUser)
{
    RT_NOREF2(pUVM, pvUser);

    /*
     * Get root node first.
     * This is the only node in the tree.
     */
    PCFGMNODE pRoot = CFGMR3GetRoot(pVM);
    int rc = CFGMR3InsertInteger(pRoot, "RamSize", 32*1024*1024);
    AssertRC(rc);

    /* rc = CFGMR3InsertInteger(pRoot, "EnableNestedPaging", false);
    AssertRC(rc); */

    PCFGMNODE pHWVirtExt;
    rc = CFGMR3InsertNode(pRoot, "HWVirtExt", &pHWVirtExt);
    AssertRC(rc);
    rc = CFGMR3InsertInteger(pHWVirtExt, "Enabled", 1);
    AssertRC(rc);

    return VINF_SUCCESS;
}
#endif

int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);

    /*
     * Doesn't work and I'm sick of rebooting the machine to try figure out
     * what the heck is going wrong. (Linux sucks at this)
     */
    RTPrintf(TESTCASE ": This testcase hits a bunch of breakpoint assertions which\n"
             TESTCASE ": causes kernel panics on linux regardless of what\n"
             TESTCASE ": RTAssertDoBreakpoint returns. Only checked AMD-V on linux.\n");
#if 1
    /** @todo Make tstVMM-Hm to cause kernel panics. */
    return 1;
#else
    int     rcRet = 0;                  /* error count. */

    /*
     * Create empty VM.
     */
    RTPrintf(TESTCASE ": Initializing...\n");
    PVM pVM;
    PUVM pUVM;
    int rc = VMR3Create(1 /*cCpus*/, NULL, 0 /*fFlags*/, NULL, NULL, tstVmmHmConfigConstructor, NULL, &pVM, &pUVM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do testing.
         */
        RTPrintf(TESTCASE ": Testing...\n");
        rc = VMR3ReqCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)VMMDoHmTest, 1, pVM);
        AssertRC(rc);

        STAMR3Dump(pUVM, "*");

        /*
         * Cleanup.
         */
        rc = VMR3Destroy(pUVM);
        if (RT_FAILURE(rc))
        {
            RTPrintf(TESTCASE ": error: failed to destroy vm! rc=%d\n", rc);
            rcRet++;
        }
        VMR3ReleaseUVM(pUVM);
    }
    else
    {
        RTPrintf(TESTCASE ": fatal error: failed to create vm! rc=%d\n", rc);
        rcRet++;
    }

    return rcRet;
#endif
}
