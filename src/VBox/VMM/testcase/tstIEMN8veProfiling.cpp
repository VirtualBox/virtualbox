/* $Id: tstIEMN8veProfiling.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Queue Testcase.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_PDM_QUEUE
#define VBOX_IN_VMM

#include <VBox/vmm/iem.h>

#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmmr3vtable.h>

#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static uint32_t         g_cMaxTbs      = 0;
static uint64_t         g_cbMaxExecMem = 0;
static PCVMMR3VTABLE    g_pVMM         = NULL;


/**
 * @callback_method_impl{FNCFGMCONSTRUCTOR}
 */
static DECLCALLBACK(int) tstIEMN8veProfilingCfgConstructor(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, void *pvUser)
{
    RT_NOREF(pUVM, pvUser);
    g_pVMM = pVMM;

    /*
     * Create a default configuration tree.
     */
    int rc = pVMM->pfnCFGMR3ConstructDefaultTree(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Now for our overrides.
     */
    PCFGMNODE const pRoot    = pVMM->pfnCFGMR3GetRoot(pVM);
    PCFGMNODE       pIemNode = pVMM->pfnCFGMR3GetChild(pRoot, "IEM");
    if (!pIemNode)
    {
        rc = pVMM->pfnCFGMR3InsertNode(pRoot, "IEM", &pIemNode);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(rc, "CFGMR3InsertNode/IEM failed: %Rrc", rc);
    }

    if (g_cMaxTbs != 0)
    {
        rc = pVMM->pfnCFGMR3InsertInteger(pIemNode, "MaxTbCount", g_cMaxTbs);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(rc, "CFGMR3InsertInteger/MaxTbCount failed: %Rrc", rc);
    }

    if (g_cbMaxExecMem != 0)
    {
        rc = pVMM->pfnCFGMR3InsertInteger(pIemNode, "MaxExecMem", g_cbMaxExecMem);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(rc, "CFGMR3InsertInteger/MaxExecMemaxTbCount failed: %Rrc", rc);
    }

    return VINF_SUCCESS;
}



int main(int argc, char **argv)
{
    /*
     * We run the VMM in driverless mode to avoid needing to hardened the testcase.
     */
    RTEXITCODE rcExit;
    int rc = RTR3InitExe(argc, &argv, SUPR3INIT_F_DRIVERLESS << RTR3INIT_FLAGS_SUPLIB_SHIFT);
    if (RT_SUCCESS(rc))
    {
        /* Don't do anything unless we've been given some input. */
        if (argc > 1)
        {
            /*
             * Parse arguments.
             */
            PVM                         pVM          = NULL;
            PUVM                        pUVM         = NULL;
            unsigned                    cVerbosity   = 0;
            uint32_t                    cMinTbs      = 0;
            const char                 *pszStats     = NULL;
            static const RTGETOPTDEF    s_aOptions[] =
            {
                { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
                { "--min-tbs",      'm', RTGETOPT_REQ_UINT32  },
                { "--stats",        's', RTGETOPT_REQ_STRING  },
                { "--max-tb-count", 't', RTGETOPT_REQ_UINT32  },
                { "--max-exec-mem", 'x', RTGETOPT_REQ_UINT64  },
            };
            RTGETOPTSTATE               GetState;
            rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1,  RTGETOPTINIT_FLAGS_OPTS_FIRST);
            AssertRC(rc);

            rcExit = RTEXITCODE_SUCCESS;
            RTGETOPTUNION ValueUnion;
            while ((rc = RTGetOpt(&GetState, &ValueUnion)))
            {
                switch (rc)
                {
                    case 'm':
                        cMinTbs = ValueUnion.u32;
                        break;

                    case 'v':
                        cVerbosity++;
                        break;

                    case 'q':
                        cVerbosity = 0;
                        break;

                    case 's':
                        pszStats = *ValueUnion.psz ? ValueUnion.psz : NULL;
                        break;

                    case 't':
                        g_cMaxTbs = ValueUnion.u32;
                        break;

                    case 'x':
                        g_cbMaxExecMem = ValueUnion.u64;
                        break;

                    case 'h':
                        RTPrintf("Usage: %Rbn [options] <ThreadedTBsForRecompilerProfiling.sav>\n"
                                 "\n"
                                 "Options:\n"
                                 "  --min-tbs=<count>, -m<count>\n"
                                 "    Duplicate TBs till we have at least the specific count.\n"
                                 , argv[0]);
                        return RTEXITCODE_SUCCESS;

                    case VINF_GETOPT_NOT_OPTION:
                        /*
                         * Create the VM the first time thru here.
                         */
                        if (!pVM)
                        {
                            rc = VMR3Create(1 /*cCpus*/, NULL, VMCREATE_F_DRIVERLESS, NULL, NULL,
                                            tstIEMN8veProfilingCfgConstructor, NULL, &pVM, &pUVM);
                            if (RT_FAILURE(rc))
                            {
                                RTMsgError("VMR3Create failed: %Rrc", rc);
                                return RTEXITCODE_FAILURE;
                            }
                        }
                        rc = g_pVMM->pfnVMR3ReqCallWaitU(pUVM, 0, (PFNRT)IEMR3ThreadedProfileRecompilingSavedTbs,
                                                         3, pVM, ValueUnion.psz, cMinTbs);
                        if (RT_SUCCESS(rc))
                        {
                            if (pszStats)
                                g_pVMM->pfnSTAMR3Print(pUVM, pszStats);
                        }
                        else
                            rcExit = RTMsgErrorExitFailure("VMR3ReqCallWaitU/IEMR3ThreadedProfileRecompilingSavedTbs failed: %Rrc",
                                                           rc);
                        break;

                    default:
                        return RTGetOptPrintError(rc, &ValueUnion);
                }
            }
        }
        else
        {
            RTMsgInfo("Nothing to do. Try --help.\n");
            rcExit = RTEXITCODE_SUCCESS;
        }
    }
    else
        rcExit = RTMsgInitFailure(rc);
    return rcExit;
}

