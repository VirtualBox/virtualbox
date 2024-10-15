/* $Id: tstGuestControlMockHGCM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Control host service test case.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#include <VBox/VBoxGuestLib.h>

#include <VBox/HostServices/TstHGCMMock.h>
#include <VBox/HostServices/TstHGCMMockUtils.h>

#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Static globals                                                                                                               *
*********************************************************************************************************************************/
static RTTEST     g_hTest;


/*********************************************************************************************************************************
*   Shared Clipboard testing                                                                                                     *
*********************************************************************************************************************************/
/** @todo r=bird: Clipboard?  */

struct CLIPBOARDTESTDESC;
/** Pointer to a test description. */
typedef CLIPBOARDTESTDESC *PTESTDESC;

struct CLIPBOARDTESTCTX;
/** Pointer to a test context. */
typedef CLIPBOARDTESTCTX *PCLIPBOARDTESTCTX;

/** Pointer a test descriptor. */
typedef CLIPBOARDTESTDESC *PTESTDESC;

typedef DECLCALLBACKTYPE(int, FNTESTSETUP,(PCLIPBOARDTESTCTX pTstCtx, void **ppvCtx));
/** Pointer to a test setup callback. */
typedef FNTESTSETUP *PFNTESTSETUP;

typedef DECLCALLBACKTYPE(int, FNTESTEXEC,(PCLIPBOARDTESTCTX pTstCtx, void *pvCtx));
/** Pointer to a test exec callback. */
typedef FNTESTEXEC *PFNTESTEXEC;

typedef DECLCALLBACKTYPE(int, FNTESTDESTROY,(PCLIPBOARDTESTCTX pTstCtx, void *pvCtx));
/** Pointer to a test destroy callback. */
typedef FNTESTDESTROY *PFNTESTDESTROY;


/**
 * Structure for keeping a clipboard test task.
 */
typedef struct CLIPBOARDTESTTASK
{

} CLIPBOARDTESTTASK;
typedef CLIPBOARDTESTTASK *PCLIPBOARDTESTTASK;

/**
 * Structure for keeping a clipboard test context.
 */
typedef struct CLIPBOARDTESTCTX
{
    /** The HGCM Mock utils context. */
    TSTHGCMUTILSCTX   HGCM;
    /** Clipboard-specific task data. */
    CLIPBOARDTESTTASK Task;
    struct
    {
        /** The VbglR3 Guest Control context to work on. */
        VBGLR3GUESTCTRLCMDCTX CmdCtx;
    } Guest;
} CLIPBOARDTESTCTX;

/** The one and only clipboard test context. One at a time. */
CLIPBOARDTESTCTX g_TstCtx;

#if 0 /** @todo r=bird: Clipboard? This times out and asserts and doesn't seems to do anything sensible. */

/**
 * Structure for keeping a clipboard test description.
 */
typedef struct CLIPBOARDTESTDESC
{
    /** The setup callback. */
    PFNTESTSETUP         pfnSetup;
    /** The exec callback. */
    PFNTESTEXEC          pfnExec;
    /** The destruction callback. */
    PFNTESTDESTROY       pfnDestroy;
} CLIPBOARDTESTDESC;

typedef struct SHCLCONTEXT
{
} SHCLCONTEXT;

#endif


static void testGuestSimple(void)
{
    RTTestISub("Testing client (guest) API - Simple");

    PTSTHGCMMOCKSVC pSvc = TstHgcmMockSvcInst(); RT_NOREF(pSvc);

    /* Preparations. */
    VBGLR3GUESTCTRLCMDCTX Ctx;
    RT_ZERO(Ctx);

    /*
     * Multiple connects / disconnects.
     */
    RTTESTI_CHECK_RC_OK(VbglR3GuestCtrlConnect(&Ctx.uClientID));
    RTTESTI_CHECK_RC_OK(VbglR3GuestCtrlDisconnect(Ctx.uClientID));

    /*
     * Feature tests.
     */
    RTTESTI_CHECK_RC_OK(VbglR3GuestCtrlConnect(&Ctx.uClientID));
    RTTESTI_CHECK_RC_OK(VbglR3GuestCtrlReportFeatures(Ctx.uClientID, 0x0,        NULL /* pfHostFeatures */));
    /* Report bogus features to the host. */
    RTTESTI_CHECK_RC_OK(VbglR3GuestCtrlReportFeatures(Ctx.uClientID, 0xdeadb33f, NULL /* pfHostFeatures */));
    RTTESTI_CHECK_RC_OK(VbglR3GuestCtrlDisconnect(Ctx.uClientID));
}

static void testHostCall(void)
{
}


/*********************************************************************************************************************************
 * Test: Guest reading from host                                                                                                 *
 ********************************************************************************************************************************/
/** @todo r=bird: Reading from the host? WTF?  Doesn't seem to work, so I've disabled it till it can be rewritten and
 * made to do something useful rather than asserting. */
#if 0
typedef struct TSTUSERMOCK
{
} TSTUSERMOCK;
typedef TSTUSERMOCK *PTSTUSERMOCK;

static void tstTestReadFromHost_MockInit(PTSTUSERMOCK pUsrMock, const char *pszName)
{
    RT_NOREF(pUsrMock, pszName);
}

static void tstTestReadFromHost_MockDestroy(PTSTUSERMOCK pUsrMock)
{
    RT_NOREF(pUsrMock);
}

static DECLCALLBACK(int) tstTestReadFromHost_ThreadGuest(PTSTHGCMUTILSCTX pCtx, void *pvCtx)
{
    RTThreadSleep(1000); /* Fudge; wait until the host has prepared the data for the clipboard. */

    PCLIPBOARDTESTCTX  pTstCtx  = (PCLIPBOARDTESTCTX)pvCtx;
    AssertPtr(pTstCtx);

    RT_ZERO(pTstCtx->Guest.CmdCtx);
    RTTEST_CHECK_RC_OK(g_hTest, VbglR3GuestCtrlConnect(&pTstCtx->Guest.CmdCtx.uClientID));

    RTThreadSleep(1000); /* Fudge; wait until the host has prepared the data for the clipboard. */

    /* Signal that the task ended. */
    TstHGCMUtilsTaskSignal(&pCtx->Task, VINF_SUCCESS);

    RTTEST_CHECK_RC_OK(g_hTest, VbglR3GuestCtrlDisconnect(pTstCtx->Guest.CmdCtx.uClientID));

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstTestReadFromHostSetup(PCLIPBOARDTESTCTX pTstCtx, void **ppvCtx)
{
    RT_NOREF(pTstCtx, ppvCtx);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstTestReadFromHostExec(PCLIPBOARDTESTCTX pTstCtx, void *pvCtx)
{
    RT_NOREF(pvCtx);

    TstHGCMUtilsGuestThreadStart(&pTstCtx->HGCM, tstTestReadFromHost_ThreadGuest, pTstCtx);

    PTSTHGCMUTILSTASK pTask = (PTSTHGCMUTILSTASK)TstHGCMUtilsTaskGetCurrent(&pTstCtx->HGCM);

    bool fUseMock = false;
    TSTUSERMOCK UsrMock;
    if (fUseMock)
        tstTestReadFromHost_MockInit(&UsrMock, "tstX11Hst");

    /* Wait until the task has been finished. */
    TstHGCMUtilsTaskWait(pTask, RT_MS_30SEC);

    if (fUseMock)
        tstTestReadFromHost_MockDestroy(&UsrMock);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstTestReadFromHostDestroy(PCLIPBOARDTESTCTX pTstCtx, void *pvCtx)
{
    RT_NOREF(pvCtx);

    int vrc = TstHGCMUtilsGuestThreadStop(&pTstCtx->HGCM);
    AssertRC(vrc);
    vrc = TstHGCMUtilsHostThreadStop(&pTstCtx->HGCM);
    AssertRC(vrc);

    return vrc;
}

#endif


/*********************************************************************************************************************************
 * Main                                                                                                                          *
 ********************************************************************************************************************************/
#if 0 /** @todo r=bird: Same as above.  */

/** Test definition table. */
CLIPBOARDTESTDESC g_aTests[] =
{
    /* Tests guest reading clipboard data from the host.  */
    { tstTestReadFromHostSetup,       tstTestReadFromHostExec,      tstTestReadFromHostDestroy }
};
/** Number of tests defined. */
unsigned g_cTests = RT_ELEMENTS(g_aTests);

static int tstOne(PTESTDESC pTstDesc)
{
    PCLIPBOARDTESTCTX pTstCtx = &g_TstCtx;

    void *pvCtx;
    int rc = pTstDesc->pfnSetup(pTstCtx, &pvCtx);
    if (RT_SUCCESS(rc))
    {
        rc = pTstDesc->pfnExec(pTstCtx, pvCtx);

        int rc2 = pTstDesc->pfnDestroy(pTstCtx, pvCtx);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}

#endif

int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate("tstGuestControlMockHGCM", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    PTSTHGCMMOCKSVC const pSvc = TstHgcmMockSvcInst();
    TstHgcmMockSvcCreate(pSvc);
    TstHgcmMockSvcStart(pSvc);

    /*
     * Run the tests.
     */
    if (1)
    {
        testGuestSimple();
        testHostCall();
    }

#if 0 /** @todo r=bird: Clipboard? This times out and asserts and doesn't seems to do anything sensible. */
    RT_ZERO(g_TstCtx);

    PTSTHGCMUTILSCTX pCtx = &g_TstCtx.HGCM;
    TstHGCMUtilsCtxInit(pCtx, pSvc);

    PTSTHGCMUTILSTASK pTask = (PTSTHGCMUTILSTASK)TstHGCMUtilsTaskGetCurrent(pCtx);
    TstHGCMUtilsTaskInit(pTask);
    pTask->pvUser = &g_TstCtx.Task;

    for (unsigned i = 0; i < RT_ELEMENTS(g_aTests); i++)
        tstOne(&g_aTests[i]);

    TstHGCMUtilsTaskDestroy(pTask);
#endif

    TstHgcmMockSvcStop(pSvc);
    TstHgcmMockSvcDestroy(pSvc);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(g_hTest);
}

