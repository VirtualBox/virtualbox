/* $Id: tstGuestControlSvc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Testcase for the guest control service.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <VBox/HostServices/GuestControlSvc.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest = NIL_RTTEST;

using namespace guestControl;

extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad(VBOXHGCMSVCFNTABLE *pTable);

/** Simple call handle structure for the guest call completion callback */
struct VBOXHGCMCALLHANDLE_TYPEDEF
{
    /** Where to store the result code. */
    int32_t rc;
};

/** Call completion callback for guest calls. */
static DECLCALLBACK(int) callComplete(VBOXHGCMCALLHANDLE callHandle, int32_t rc)
{
    callHandle->rc = rc;
    return VINF_SUCCESS;
}

/**
 * Initialise the HGCM service table as much as we need to start the
 * service.
 *
 * @return IPRT status code.
 * @param  pTable the table to initialise
 */
static int initTable(VBOXHGCMSVCFNTABLE *pTable, VBOXHGCMSVCHELPERS *pHelpers)
{
    pTable->cbSize = sizeof (VBOXHGCMSVCFNTABLE);
    pTable->u32Version = VBOX_HGCM_SVC_VERSION;
    pHelpers->pfnCallComplete = callComplete;
    pTable->pHelpers = pHelpers;

    return VINF_SUCCESS;
}

typedef struct CMDHOST
{
    /** The HGCM command to execute. */
    int cmd;
    /** Number of parameters. */
    int num_parms;
    /** The actual parameters. */
    const PVBOXHGCMSVCPARM parms;
    /** Flag indicating whether we need a connected client for this command. */
    bool fNeedsClient;
    /** The desired return value from the host. */
    int rc;
} CMDHOST, *PCMDHOST;

typedef struct CMDCLIENT
{
    /** The client's ID. */
    int client_id;
    /** The HGCM command to execute. */
    int cmd;
    /** Number of parameters. */
    int num_parms;
    /** The actual parameters. */
    const PVBOXHGCMSVCPARM parms;
    /** The desired return value from the host. */
    int rc;
} CMDCLIENT, *PCMDCLIENT;

/**
 * Tests the HOST_EXEC_CMD function.
 * @returns iprt status value to indicate whether the test went as expected.
 * @note    prints its own diagnostic information to stdout.
 */
static int testHostCmd(const VBOXHGCMSVCFNTABLE *pTable, const PCMDHOST pCmd, uint32_t uNumTests)
{
    int rc = VINF_SUCCESS;
    if (!RT_VALID_PTR(pTable->pfnHostCall))
    {
        RTTestPrintf(g_hTest, RTTESTLVL_FAILURE, "Invalid pfnHostCall() pointer\n");
        rc = VERR_INVALID_POINTER;
    }
    if (RT_SUCCESS(rc))
    {
        for (unsigned i = 0; (i < uNumTests) && RT_SUCCESS(rc); i++)
        {
            RTTestPrintf(g_hTest, RTTESTLVL_INFO, "Testing #%u (cmd: %d, num_parms: %d, parms: 0x%p\n",
                         i, pCmd[i].cmd, pCmd[i].num_parms, pCmd[i].parms);

            if (pCmd[i].fNeedsClient)
            {
                int client_rc = pTable->pfnConnect(pTable->pvService, 1000 /* Client ID */, NULL /* pvClient */, 0, false);
                if (RT_FAILURE(client_rc))
                    rc = client_rc;
            }

            if (RT_SUCCESS(rc))
            {
                int host_rc = pTable->pfnHostCall(pTable->pvService,
                                                  pCmd[i].cmd,
                                                  pCmd[i].num_parms,
                                                  pCmd[i].parms);
                if (host_rc != pCmd[i].rc)
                {
                    RTTestPrintf(g_hTest, RTTESTLVL_FAILURE, "Host call test #%u returned with rc=%Rrc instead of rc=%Rrc\n",
                                 i, host_rc, pCmd[i].rc);
                    rc = host_rc;
                    if (RT_SUCCESS(rc))
                        rc = VERR_INVALID_PARAMETER;
                }

                if (pCmd[i].fNeedsClient)
                {
                    int client_rc = pTable->pfnDisconnect(pTable->pvService, 1000 /* Client ID */, NULL /* pvClient */);
                    if (RT_SUCCESS(rc))
                        rc = client_rc;
                }
            }
        }
    }
    return rc;
}

static int testHost(const VBOXHGCMSVCFNTABLE *pTable)
{
    RTTestSub(g_hTest, "Testing host commands ...");

    VBOXHGCMSVCPARM aParms[1];
    HGCMSvcSetU32(&aParms[0], 1000 /* Context ID */);

    CMDHOST aCmdHostAll[] =
    {
#if 0
        /** No client connected. */
        { 1024 /* Not existing command */, 0, 0, false, VERR_NOT_FOUND },
        { -1 /* Invalid command */, 0, 0, false, VERR_NOT_FOUND },
        { HOST_CANCEL_PENDING_WAITS, 1024, 0, false, VERR_NOT_FOUND },
        { HOST_CANCEL_PENDING_WAITS, 0, &aParms[0], false, VERR_NOT_FOUND },

        /** No client connected, valid command. */
        { HOST_CANCEL_PENDING_WAITS, 0, 0, false, VERR_NOT_FOUND },

        /** Client connected, no parameters given. */
        { HOST_EXEC_SET_INPUT, 0 /* No parameters given */, 0, true, VERR_INVALID_PARAMETER },
        { 1024 /* Not existing command */, 0 /* No parameters given */, 0, true, VERR_INVALID_PARAMETER },
        { -1 /* Invalid command */, 0 /* No parameters given */, 0, true, VERR_INVALID_PARAMETER },

        /** Client connected, valid parameters given. */
        { HOST_CANCEL_PENDING_WAITS, 0, 0, true, VINF_SUCCESS },
        { HOST_CANCEL_PENDING_WAITS, 1024, &aParms[0], true, VINF_SUCCESS },
        { HOST_CANCEL_PENDING_WAITS, 0, &aParms[0], true, VINF_SUCCESS},
#endif

        /** Client connected, invalid parameters given. */
        { HOST_MSG_EXEC_CMD, 1024, 0, true, VERR_INVALID_POINTER },
        { HOST_MSG_EXEC_CMD, 1, 0, true, VERR_INVALID_POINTER },
        { HOST_MSG_EXEC_CMD, -1, 0, true, VERR_INVALID_POINTER },

        /** Client connected, parameters given. */
        { HOST_MSG_CANCEL_PENDING_WAITS, 1, &aParms[0], true, VINF_SUCCESS },
        { HOST_MSG_EXEC_CMD, 1, &aParms[0], true, VINF_SUCCESS },
        { HOST_MSG_EXEC_SET_INPUT, 1, &aParms[0], true, VINF_SUCCESS },
        { HOST_MSG_EXEC_GET_OUTPUT, 1, &aParms[0], true, VINF_SUCCESS },

        /** Client connected, unknown command + valid parameters given. */
        { -1, 1, &aParms[0], true, VINF_SUCCESS }
    };

    int rc = testHostCmd(pTable, &aCmdHostAll[0], RT_ELEMENTS(aCmdHostAll));
    RTTestSubDone(g_hTest);
    return rc;
}

static int testClient(const VBOXHGCMSVCFNTABLE *pTable)
{
    RTTestSub(g_hTest, "Testing client commands ...");

    int rc = pTable->pfnConnect(pTable->pvService, 1 /* Client ID */, NULL /* pvClient */, 0, false);
    if (RT_SUCCESS(rc))
    {
        VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

        /* No commands from host yet. */
        VBOXHGCMSVCPARM aParmsGuest[8];
        HGCMSvcSetU32(&aParmsGuest[0], 0 /* Msg type */);
        HGCMSvcSetU32(&aParmsGuest[1], 0 /* Parameters */);
        pTable->pfnCall(pTable->pvService, &callHandle, 1 /* Client ID */, NULL /* pvClient */,
                        GUEST_MSG_WAIT, 2, &aParmsGuest[0], 0);
        RTTEST_CHECK_RC_RET(g_hTest, callHandle.rc, VINF_SUCCESS, callHandle.rc);

        /* Host: Add a dummy command. */
        VBOXHGCMSVCPARM aParmsHost[8];
        HGCMSvcSetU32(&aParmsHost[0], 1000 /* Context ID */);
        HGCMSvcSetStr(&aParmsHost[1], "foo.bar");
        HGCMSvcSetStr(&aParmsHost[2], "baz");

        rc = pTable->pfnHostCall(pTable->pvService, HOST_MSG_EXEC_CMD, 3, &aParmsHost[0]);
        RTTEST_CHECK_RC_RET(g_hTest, rc, VINF_SUCCESS, rc);

        /* Client: Disconnect again. */
        int rc2 = pTable->pfnDisconnect(pTable->pvService, 1000 /* Client ID */, NULL /* pvClient */);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    RTTestSubDone(g_hTest);
    return rc;
}

/*
 * Set environment variable "IPRT_TEST_MAX_LEVEL=all" to get more debug output!
 */
int main()
{
    RTEXITCODE rcExit  = RTTestInitAndCreate("tstGuestControlSvc", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    /* Some host info. */
    RTTestIPrintf(RTTESTLVL_ALWAYS, "sizeof(void*)=%d\n", sizeof(void*));

    /* Do the tests. */
    VBOXHGCMSVCFNTABLE svcTable;
    VBOXHGCMSVCHELPERS svcHelpers;
    RTTEST_CHECK_RC_RET(g_hTest, initTable(&svcTable, &svcHelpers), VINF_SUCCESS, 1);

    do
    {
        RTTESTI_CHECK_RC_BREAK(VBoxHGCMSvcLoad(&svcTable), VINF_SUCCESS);

        RTTESTI_CHECK_RC_BREAK(testHost(&svcTable), VINF_SUCCESS);

        RTTESTI_CHECK_RC_BREAK(svcTable.pfnUnload(svcTable.pvService), VINF_SUCCESS);

        RTTESTI_CHECK_RC_BREAK(VBoxHGCMSvcLoad(&svcTable), VINF_SUCCESS);

        RTTESTI_CHECK_RC_BREAK(testClient(&svcTable), VINF_SUCCESS);

        RTTESTI_CHECK_RC_BREAK(svcTable.pfnUnload(svcTable.pvService), VINF_SUCCESS);

    } while (0);

    return RTTestSummaryAndDestroy(g_hTest);
}

