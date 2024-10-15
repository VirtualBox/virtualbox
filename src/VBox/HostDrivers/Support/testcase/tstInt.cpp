/* $Id: tstInt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SUP Testcase - Test the interrupt gate feature of the support library.
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
#include <VBox/sup.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/vm.h>
#include <iprt/errcore.h>
#include <VBox/param.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#else
# define ASMReadTSC RTTimeSystemNanoTS
#endif
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/alloc.h>
#include <iprt/time.h>
#include <iprt/path.h>


int main(int argc, char **argv)
{
    int rcRet = 0;
    int i;
    int cIterations = argc > 1 ? RTStrToUInt32(argv[1]) : 32;
    if (cIterations == 0)
        cIterations = 64;

    /*
     * Init.
     */
    RTR3InitExe(argc, &argv, 0);
    PSUPDRVSESSION pSession;
    int rc = SUPR3Init(&pSession);
    RTPrintf("tstInt: SUPR3Init -> rc=%Rrc\n", rc);
    if (RT_FAILURE(rc))
        return 1;

    char szFile[RTPATH_MAX];
    rc = RTPathExecDir(szFile, sizeof(szFile));
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szFile, sizeof(szFile), "VMMR0.r0");

    char szAbsFile[RTPATH_MAX];
    if (RT_SUCCESS(rc))
        rc = RTPathAbs(szFile, szAbsFile, sizeof(szAbsFile));
    if (RT_SUCCESS(rc))
    {
        /*
         * Load VMM code.
         */
        RTERRINFOSTATIC ErrInfo;
        rc = SUPR3LoadVMM(szAbsFile, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            /*
             * Create a tiny dummy VM so we can do NOP calls into it using the fast I/O control path.
             */
            GVMMCREATEVMREQ CreateVMReq;
            CreateVMReq.Hdr.u32Magic    = SUPVMMR0REQHDR_MAGIC;
            CreateVMReq.Hdr.cbReq       = sizeof(CreateVMReq);
            CreateVMReq.pSession        = pSession;
            CreateVMReq.pVMR0           = NIL_RTR0PTR;
            CreateVMReq.pVMR3           = NULL;
            CreateVMReq.cCpus           = 1;
            rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_GVMM_CREATE_VM, 0, &CreateVMReq.Hdr);
            if (RT_SUCCESS(rc))
            {
                PVM pVM = CreateVMReq.pVMR3;
                AssertRelease(RT_VALID_PTR(pVM));
                AssertRelease(pVM->pVMR0ForCall == CreateVMReq.pVMR0);
                AssertRelease(pVM->pSession == pSession);
                AssertRelease(pVM->cCpus == 1);
                pVM->enmVMState = VMSTATE_CREATED;
                PVMR0 const pVMR0 = CreateVMReq.pVMR0;

                rc = SUPR3SetVMForFastIOCtl(pVMR0);
                if (!rc)
                {
                    /*
                     * Call VMM code with invalid function.
                     */
                    for (i = cIterations; i > 0; i--)
                    {
                        rc = SUPR3CallVMMR0(pVMR0, NIL_VMCPUID, VMMR0_DO_SLOW_NOP, NULL);
                        if (rc != VINF_SUCCESS)
                        {
                            RTPrintf("tstInt: SUPR3CallVMMR0 -> rc=%Rrc i=%d Expected VINF_SUCCESS!\n", rc, i);
                            rcRet++;
                            break;
                        }
                    }
                    RTPrintf("tstInt: Performed SUPR3CallVMMR0 %d times (rc=%Rrc)\n", cIterations, rc);

                    /*
                     * The fast path.
                     */
                    if (rc == VINF_SUCCESS)
                    {
                        RTTimeNanoTS();
                        uint64_t StartTS = RTTimeNanoTS();
                        uint64_t StartTick = ASMReadTSC();
                        uint64_t MinTicks = UINT64_MAX;
                        for (i = 0; i < 1000000; i++)
                        {
                            uint64_t OneStartTick = ASMReadTSC();
                            rc = SUPR3CallVMMR0Fast(pVMR0, VMMR0_DO_NOP, 0);
                            uint64_t Ticks = ASMReadTSC() - OneStartTick;
                            if (Ticks < MinTicks)
                                MinTicks = Ticks;

                            if (RT_UNLIKELY(rc != VINF_SUCCESS))
                            {
                                RTPrintf("tstInt: SUPR3CallVMMR0Fast -> rc=%Rrc i=%d Expected VINF_SUCCESS!\n", rc, i);
                                rcRet++;
                                break;
                            }
                        }
                        uint64_t Ticks = ASMReadTSC() - StartTick;
                        uint64_t NanoSecs = RTTimeNanoTS() - StartTS;

                        RTPrintf("tstInt: SUPR3CallVMMR0Fast - %d iterations in %llu ns / %llu ticks. %llu ns / %#llu ticks per iteration. Min %llu ticks.\n",
                                 i, NanoSecs, Ticks, NanoSecs / i, Ticks / i, MinTicks);

                        /*
                         * The ordinary path.
                         */
                        RTTimeNanoTS();
                        StartTS = RTTimeNanoTS();
                        StartTick = ASMReadTSC();
                        MinTicks = UINT64_MAX;
                        for (i = 0; i < 1000000; i++)
                        {
                            uint64_t OneStartTick = ASMReadTSC();
                            rc = SUPR3CallVMMR0Ex(pVMR0, NIL_VMCPUID, VMMR0_DO_SLOW_NOP, 0, NULL);
                            uint64_t OneTicks = ASMReadTSC() - OneStartTick;
                            if (OneTicks < MinTicks)
                                MinTicks = OneTicks;

                            if (RT_UNLIKELY(rc != VINF_SUCCESS))
                            {
                                RTPrintf("tstInt: SUPR3CallVMMR0Ex -> rc=%Rrc i=%d Expected VINF_SUCCESS!\n", rc, i);
                                rcRet++;
                                break;
                            }
                        }
                        Ticks = ASMReadTSC() - StartTick;
                        NanoSecs = RTTimeNanoTS() - StartTS;

                        RTPrintf("tstInt: SUPR3CallVMMR0Ex   - %d iterations in %llu ns / %llu ticks. %llu ns / %#llu ticks per iteration. Min %llu ticks.\n",
                                 i, NanoSecs, Ticks, NanoSecs / i, Ticks / i, MinTicks);
                    }
                }
                else
                {
                    RTPrintf("tstInt: SUPR3SetVMForFastIOCtl failed: %Rrc\n", rc);
                    rcRet++;
                }

                rc = SUPR3CallVMMR0Ex(pVMR0, 0 /*idCpu*/, VMMR0_DO_GVMM_DESTROY_VM, 0, NULL);
                if (RT_FAILURE(rc))
                {
                    RTPrintf("tstInt: VMMR0_DO_GVMM_DESTROY_VM failed: %Rrc\n", rc);
                    rcRet++;
                }
            }
            else
            {
                RTPrintf("tstInt: VMMR0_DO_GVMM_CREATE_VM failed\n");
                rcRet++;
            }

            /*
             * Unload VMM.
             */
            rc = SUPR3UnloadVMM();
            if (rc)
            {
                RTPrintf("tstInt: SUPR3UnloadVMM failed with rc=%Rrc\n", rc);
                rcRet++;
            }
        }
        else
        {
            RTPrintf("tstInt: SUPR3LoadVMM failed with rc=%Rrc%#RTeim\n", rc, &ErrInfo.Core);
            rcRet++;
        }

        /*
         * Terminate.
         */
        rc = SUPR3Term(false /*fForced*/);
        rcRet += rc != 0;
        RTPrintf("tstInt: SUPR3Term -> rc=%Rrc\n", rc);
    }
    else
    {
        RTPrintf("tstInt: Failed to construct VMMR0.r0 path: %Rrc\n", rc);
        rcRet++;
    }

    return rcRet;
}

