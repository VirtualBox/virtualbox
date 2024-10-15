/* $Id: tstGlobalConfig.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Ring-3 Management program for the GCFGM mock-up.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/vmm/vmm.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/**
 * Prints the usage and returns 1.
 * @return 1
 */
static int Usage(void)
{
    RTPrintf("usage: tstGlobalConfig <value-name> [new value]\n");
    return 1;
}


/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF1(envp);
    RTR3InitExe(argc, &argv, 0);

    /*
     * Parse args, building the request as we do so.
     */
    if (argc <= 1)
        return Usage();
    if (argc > 3)
    {
        RTPrintf("syntax error: too many arguments\n");
        Usage();
        return 1;
    }

    VMMR0OPERATION enmOp = VMMR0_DO_GCFGM_QUERY_VALUE;
    GCFGMVALUEREQ Req;
    memset(&Req, 0, sizeof(Req));
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);

    /* arg[1] = szName */
    size_t cch = strlen(argv[1]);
    if (cch < 2 || argv[1][0] != '/')
    {
        RTPrintf("syntax error: malformed name '%s'\n", argv[1]);
        return 1;
    }
    if (cch >= sizeof(Req.szName))
    {
        RTPrintf("syntax error: the name '%s' is too long. (max %zu chars)\n", argv[1], sizeof(Req.szName) - 1);
        return 1;
    }
    memcpy(&Req.szName[0], argv[1], cch + 1);

    /* argv[2] = u64SetValue; optional */
    if (argc == 3)
    {
        char *pszNext = NULL;
        int rc = RTStrToUInt64Ex(argv[2], &pszNext, 0, &Req.u64Value);
        if (RT_FAILURE(rc) || *pszNext)
        {
            RTPrintf("syntax error: '%s' didn't convert successfully to a number. (%Rrc,'%s')\n", argv[2], rc, pszNext);
            return 1;
        }
        enmOp = VMMR0_DO_GCFGM_SET_VALUE;
    }

    /*
     * Open the session, load ring-0 and issue the request.
     */
    PSUPDRVSESSION pSession;
    int rc = SUPR3Init(&pSession);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstGlobalConfig: SUPR3Init -> %Rrc\n", rc);
        return 1;
    }

    rc = SUPR3LoadVMM("./VMMR0.r0", NULL /*pErrInfo*/);
    if (RT_SUCCESS(rc))
    {
        Req.pSession = pSession;
        rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, enmOp, 0, &Req.Hdr);
        if (RT_SUCCESS(rc))
        {
            if (enmOp == VMMR0_DO_GCFGM_QUERY_VALUE)
                RTPrintf("%s = %RU64 (%#RX64)\n", Req.szName, Req.u64Value, Req.u64Value);
            else
                RTPrintf("Successfully set %s = %RU64 (%#RX64)\n", Req.szName, Req.u64Value, Req.u64Value);
        }
        else if (enmOp == VMMR0_DO_GCFGM_QUERY_VALUE)
            RTPrintf("error: Failed to query '%s', rc=%Rrc\n", Req.szName, rc);
        else
            RTPrintf("error: Failed to set '%s' to %RU64, rc=%Rrc\n", Req.szName, Req.u64Value, rc);

    }
    SUPR3Term(false /*fForced*/);

    return RT_FAILURE(rc) ? 1 : 0;
}


#if !defined(VBOX_WITH_HARDENING) || !defined(RT_OS_WINDOWS)
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    return TrustedMain(argc, argv, envp);
}
#endif

