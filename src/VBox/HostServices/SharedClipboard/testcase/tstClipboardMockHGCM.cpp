/* $Id: tstClipboardMockHGCM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard host service test case.
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
#include "../VBoxSharedClipboardSvc-internal.h"

#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/VBoxGuestLib.h>
#ifdef RT_OS_LINUX
# include <VBox/GuestHost/SharedClipboard-x11.h>
#endif
#ifdef RT_OS_WINDOWS
# include <VBox/GuestHost/SharedClipboard-win.h>
#endif

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
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST     g_hTest;


/*********************************************************************************************************************************
*   Shared Clipboard testing                                                                                                     *
*********************************************************************************************************************************/
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
    SHCLFORMATS enmFmtHst;
    SHCLFORMATS enmFmtGst;
    /** For testing chunked reads / writes. */
    size_t      cbChunk;
    /** Data buffer to read / write for this task.
     *  Can be NULL if not needed. */
    void       *pvData;
    /** Size (in bytes) of \a pvData. */
    size_t      cbData;
    /** Number of bytes read / written from / to \a pvData. */
    size_t      cbProcessed;
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
        /** The VbglR3 Shared Clipboard context to work on. */
        VBGLR3SHCLCMDCTX CmdCtx;
    } Guest;
} CLIPBOARDTESTCTX;

/** The one and only clipboard test context. One at a time. */
CLIPBOARDTESTCTX g_TstCtx;

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


static int tstSetModeRc(PTSTHGCMMOCKSVC pSvc, uint32_t uMode, int rcExpected)
{
    VBOXHGCMSVCPARM aParms[2];
    HGCMSvcSetU32(&aParms[0], uMode);
    int rc2 = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, aParms);
    RTTESTI_CHECK_MSG_RET(rcExpected == rc2, ("Expected %Rrc, got %Rrc\n", rcExpected, rc2), rc2);
    if (RT_SUCCESS(rcExpected))
    {
        uint32_t const uModeRet = ShClSvcGetMode();
        RTTESTI_CHECK_MSG_RET(uMode == uModeRet, ("Expected mode %RU32, got %RU32\n", uMode, uModeRet), VERR_WRONG_TYPE);
    }
    return rc2;
}

static int tstClipboardSetMode(PTSTHGCMMOCKSVC pSvc, uint32_t uMode)
{
    return tstSetModeRc(pSvc, uMode, VINF_SUCCESS);
}

static bool tstClipboardGetMode(PTSTHGCMMOCKSVC pSvc, uint32_t uModeExpected)
{
    RT_NOREF(pSvc);
    RTTESTI_CHECK_RET(ShClSvcGetMode() == uModeExpected, false);
    return true;
}

static void tstOperationModes(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    uint32_t u32Mode;
    int rc;

    RTTestISub("Testing VBOX_SHCL_HOST_FN_SET_MODE");

    PTSTHGCMMOCKSVC pSvc = TstHgcmMockSvcInst();

    /* Reset global variable which doesn't reset itself. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_OFF);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    u32Mode = ShClSvcGetMode();
    RTTESTI_CHECK_MSG(u32Mode == VBOX_SHCL_MODE_OFF, ("u32Mode=%u\n", (unsigned) u32Mode));

    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 0, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 2, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    HGCMSvcSetU64(&parms[0], 99);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    tstClipboardSetMode(pSvc, VBOX_SHCL_MODE_HOST_TO_GUEST);
    tstSetModeRc(pSvc, 99, VERR_NOT_SUPPORTED);
    tstClipboardGetMode(pSvc, VBOX_SHCL_MODE_OFF);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
static void testSetTransferMode(void)
{
    RTTestISub("Testing VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE");

    PTSTHGCMMOCKSVC pSvc = TstHgcmMockSvcInst();

    /* Invalid parameter. */
    VBOXHGCMSVCPARM parms[2];
    HGCMSvcSetU64(&parms[0], 99);
    int rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    /* Invalid mode. */
    HGCMSvcSetU32(&parms[0], 99);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_FLAGS);

    /* Enable transfers. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /* Disable transfers again. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_F_NONE);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

static void testGuestSimple(void)
{
    RTTestISub("Testing client (guest) API - Simple");

    PTSTHGCMMOCKSVC pSvc = TstHgcmMockSvcInst();

    /* Preparations. */
    VBGLR3SHCLCMDCTX Ctx;
    RT_ZERO(Ctx);

    /*
     * Multiple connects / disconnects.
     */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardConnectEx(&Ctx, VBOX_SHCL_GF_0_CONTEXT_ID));
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardDisconnectEx(&Ctx));
    /* Report bogus guest features while connecting. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardConnectEx(&Ctx, 0xdeadbeef));
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardDisconnectEx(&Ctx));

    RTTESTI_CHECK_RC_OK(VbglR3ClipboardConnectEx(&Ctx, VBOX_SHCL_GF_0_CONTEXT_ID));

    /*
     * Feature tests.
     */

    RTTESTI_CHECK_RC_OK(VbglR3ClipboardReportFeatures(Ctx.idClient, 0x0,        NULL /* pfHostFeatures */));
    /* Report bogus features to the host. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardReportFeatures(Ctx.idClient, 0xdeadb33f, NULL /* pfHostFeatures */));

    /*
     * Access denied tests.
     */

    /* Try reading data from host. */
    uint8_t abData[32]; uint32_t cbIgnored;
    RTTESTI_CHECK_RC(VbglR3ClipboardReadData(Ctx.idClient, VBOX_SHCL_FMT_UNICODETEXT,
                                             abData, sizeof(abData), &cbIgnored), VERR_ACCESS_DENIED);
    /* Try writing data without reporting formats before (legacy). */
    RTTESTI_CHECK_RC(VbglR3ClipboardWriteData(Ctx.idClient, 0xdeadb33f, abData, sizeof(abData)), VERR_ACCESS_DENIED);
    /* Try writing data without reporting formats before. */
    RTTESTI_CHECK_RC(VbglR3ClipboardWriteDataEx(&Ctx, 0xdeadb33f, abData, sizeof(abData)), VERR_ACCESS_DENIED);
    /* Report bogus formats to the host. */
    RTTESTI_CHECK_RC(VbglR3ClipboardReportFormats(Ctx.idClient, 0xdeadb33f), VERR_ACCESS_DENIED);
    /* Report supported formats to host. */
    RTTESTI_CHECK_RC(VbglR3ClipboardReportFormats(Ctx.idClient,
                                                  VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_BITMAP | VBOX_SHCL_FMT_HTML),
                                                  VERR_ACCESS_DENIED);
    /*
     * Access allowed tests.
     */
    tstClipboardSetMode(pSvc, VBOX_SHCL_MODE_BIDIRECTIONAL);

    /* Try writing data without reporting formats before. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardWriteDataEx(&Ctx, 0xdeadb33f, abData, sizeof(abData)));
    /* Try reading data from host. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardReadData(Ctx.idClient, VBOX_SHCL_FMT_UNICODETEXT,
                                                abData, sizeof(abData), &cbIgnored));
    /* Report bogus formats to the host. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardReportFormats(Ctx.idClient, 0xdeadb33f));
    /* Report supported formats to host. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardReportFormats(Ctx.idClient,
                                                     VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_BITMAP | VBOX_SHCL_FMT_HTML));
    /* Tear down. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardDisconnectEx(&Ctx));
}

static RTUTF16 tstGetRandUtf8(void)
{
    return RTRandU32Ex(0x20, 0x7A);
}

static char *tstGenerateUtf8StringA(uint32_t uCch)
{
    char * pszRand = (char *)RTMemAlloc(uCch + 1);
    for (uint32_t i = 0; i < uCch; i++)
        pszRand[i] = tstGetRandUtf8();
    pszRand[uCch] = 0;
    return pszRand;
}

#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
static RTUTF16 tstGetRandUtf16(void)
{
    RTUTF16 wc;
    do
    {
        wc = (RTUTF16)RTRandU32Ex(1, 0xfffd);
    } while (wc >= 0xd800 && wc <= 0xdfff);
    return wc;
}

static PRTUTF16 tstGenerateUtf16StringA(uint32_t uCch)
{
    PRTUTF16 pwszRand = (PRTUTF16)RTMemAlloc((uCch + 1) * sizeof(RTUTF16));
    for (uint32_t i = 0; i < uCch; i++)
        pwszRand[i] = tstGetRandUtf16();
    pwszRand[uCch] = 0;
    return pwszRand;
}
#endif /* RT_OS_WINDOWS) || RT_OS_OS2 */

static void testSetHeadless(void)
{
    RTTestISub("Testing HOST_FN_SET_HEADLESS");

    PTSTHGCMMOCKSVC pSvc = TstHgcmMockSvcInst();

    VBOXHGCMSVCPARM parms[2];
    HGCMSvcSetU32(&parms[0], false);
    int rc = pSvc->fnTable.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    bool fHeadless = ShClSvcGetHeadless();
    RTTESTI_CHECK_MSG(fHeadless == false, ("fHeadless=%RTbool\n", fHeadless));
    rc = pSvc->fnTable.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS, 0, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    rc = pSvc->fnTable.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS, 2, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    HGCMSvcSetU64(&parms[0], 99);
    rc = pSvc->fnTable.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    HGCMSvcSetU32(&parms[0], true);
    rc = pSvc->fnTable.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    fHeadless = ShClSvcGetHeadless();
    RTTESTI_CHECK_MSG(fHeadless == true, ("fHeadless=%RTbool\n", fHeadless));
    HGCMSvcSetU32(&parms[0], 99);
    rc = pSvc->fnTable.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    fHeadless = ShClSvcGetHeadless();
    RTTESTI_CHECK_MSG(fHeadless == true, ("fHeadless=%RTbool\n", fHeadless));
}

static void testHostCall(void)
{
    tstOperationModes();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    testSetTransferMode();
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
    testSetHeadless();
}


/*********************************************************************************************************************************
 * Test: Guest reading from host                                                                                                 *
 ********************************************************************************************************************************/
#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
/* Called from SHCLX11 thread. */
static DECLCALLBACK(int) tstTestReadFromHost_ReportFormatsCallback(PSHCLCONTEXT pCtx, uint32_t fFormats, void *pvUser)
{
    RT_NOREF(pCtx, fFormats, pvUser);

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "tstTestReadFromHost_SvcReportFormatsCallback: fFormats=%#x\n", fFormats);
    return VINF_SUCCESS;
}

/* Called by the backend, e.g. for X11 in the SHCLX11 thread. */
static DECLCALLBACK(int) tstTestReadFromHost_OnClipboardReadCallback(PSHCLCONTEXT pCtx,
                                                                     SHCLFORMAT uFmt, void **ppv, size_t *pcb, void *pvUser)
{
    RT_NOREF(pCtx, uFmt, pvUser);

    PCLIPBOARDTESTTASK pTask = (PCLIPBOARDTESTTASK)TstHGCMUtilsTaskGetCurrent(&g_TstCtx.HGCM)->pvUser;

    void   *pvData = NULL;
    size_t  cbData = pTask->cbData - pTask->cbProcessed;
    if (cbData)
    {
        pvData = RTMemDup((uint8_t *)pTask->pvData + pTask->cbProcessed, cbData);
        AssertPtr(pvData);
    }

    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Host reporting back %RU32 bytes of data\n", cbData);

    *ppv = pvData;
    *pcb = cbData;

    return VINF_SUCCESS;
}
#endif /* (RT_OS_LINUX) || defined (RT_OS_SOLARIS) */

typedef struct TSTUSERMOCK
{
#if defined(RT_OS_LINUX)
    SHCLX11CTX   X11Ctx;
#endif
    PSHCLCONTEXT pCtx;
} TSTUSERMOCK;
typedef TSTUSERMOCK *PTSTUSERMOCK;

static void tstTestReadFromHost_MockInit(PTSTUSERMOCK pUsrMock, const char *pszName)
{
#if defined(RT_OS_LINUX)
    SHCLCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Callbacks.pfnReportFormats   = tstTestReadFromHost_ReportFormatsCallback;
    Callbacks.pfnOnClipboardRead = tstTestReadFromHost_OnClipboardReadCallback;

    pUsrMock->pCtx = (PSHCLCONTEXT)RTMemAllocZ(sizeof(SHCLCONTEXT));
    AssertPtrReturnVoid(pUsrMock->pCtx);

    ShClX11Init(&pUsrMock->X11Ctx, &Callbacks, pUsrMock->pCtx, false);
    ShClX11ThreadStartEx(&pUsrMock->X11Ctx, pszName, false /* fGrab */);
    /* Give the clipboard time to synchronise. */
    RTThreadSleep(500);
#else
    RT_NOREF(pUsrMock, pszName);
#endif /* RT_OS_LINUX */
}

static void tstTestReadFromHost_MockDestroy(PTSTUSERMOCK pUsrMock)
{
#if defined(RT_OS_LINUX)
    ShClX11ThreadStop(&pUsrMock->X11Ctx);
    ShClX11Destroy(&pUsrMock->X11Ctx);
    RTMemFree(pUsrMock->pCtx);
#else
    RT_NOREF(pUsrMock);
#endif
}

static int tstTestReadFromHost_DoIt(PCLIPBOARDTESTCTX pCtx, PCLIPBOARDTESTTASK pTask)
{
    size_t   cbDst       = RT_MAX(_64K, pTask->cbData);
    uint8_t *pabDst      = (uint8_t *)RTMemAllocZ(cbDst);
    AssertPtrReturn(pabDst, VERR_NO_MEMORY);

    AssertPtr(pTask->pvData);                /* Racing condition with host thread? */
    Assert(pTask->cbChunk);                  /* Buggy test? */
    Assert(pTask->cbChunk <= pTask->cbData); /* Ditto. */

    size_t   cbToRead = pTask->cbData;
    switch (pTask->enmFmtGst)
    {
        case VBOX_SHCL_FMT_UNICODETEXT:
#ifndef RT_OS_WINDOWS /** @todo Not sure about OS/2. */
            cbToRead *= sizeof(RTUTF16);
#endif
            break;

        default:
            break;
    }

    PVBGLR3SHCLCMDCTX pCmdCtx = &pCtx->Guest.CmdCtx;

    /* Do random chunked reads. */
    uint32_t const cChunkedReads = RTRandU32Ex(1, 16);
    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "%RU32 chunked reads\n", cChunkedReads);
    for (uint32_t i = 0; i < cChunkedReads; i++)
    {
        /* Note! VbglR3ClipboardReadData() currently does not support chunked reads!
          *      It in turn returns VINF_BUFFER_OVERFLOW when the supplied buffer was too small. */

        uint32_t cbChunk    = RTRandU32Ex(1, (uint32_t)(pTask->cbData / cChunkedReads));
        uint32_t cbRead     = 0;
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Guest trying to read %RU32 bytes\n", cbChunk);
        int vrc2 = VbglR3ClipboardReadData(pCmdCtx->idClient, pTask->enmFmtGst, pabDst, cbChunk, &cbRead);
        if (   vrc2   == VINF_SUCCESS
            && cbRead == 0) /* No data there yet? */
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "No data (yet) from host\n");
            RTThreadSleep(10);
            continue;
        }
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Trying reading host clipboard data with a %RU32 buffer -> %Rrc (%RU32)\n", cbChunk, vrc2, cbRead);
        RTTEST_CHECK_MSG(g_hTest, vrc2 == VINF_BUFFER_OVERFLOW, (g_hTest, "Got %Rrc, expected VINF_BUFFER_OVERFLOW\n", vrc2));
    }

    /* Last read: Read the data with a buffer big enough. This must succeed. */
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Reading full data (%zu)\n", pTask->cbData);
    uint32_t cbRead = 0;
    int vrc2 = VbglR3ClipboardReadData(pCmdCtx->idClient, pTask->enmFmtGst, pabDst, (uint32_t)cbDst, &cbRead);
    RTTEST_CHECK_MSG(g_hTest, vrc2 == VINF_SUCCESS, (g_hTest, "Got %Rrc, expected VINF_SUCCESS\n", vrc2));
    RTTEST_CHECK_MSG(g_hTest, cbRead == cbToRead, (g_hTest, "Read %RU32 bytes, expected %zu\n", cbRead, cbToRead));

    if (pTask->enmFmtGst == VBOX_SHCL_FMT_UNICODETEXT)
        RTTEST_CHECK_MSG(g_hTest, RTUtf16ValidateEncoding((PRTUTF16)pabDst) == VINF_SUCCESS, (g_hTest, "Read data is not valid UTF-16\n"));
    if (cbRead == cbToRead)
    {
#ifndef RT_OS_WINDOWS /** @todo Not sure about OS/2. */
        PRTUTF16 pwszSrc = NULL;
        RTTEST_CHECK(g_hTest, RT_SUCCESS(RTStrToUtf16((const char *)pTask->pvData, &pwszSrc)));
        RTTEST_CHECK_MSG(g_hTest, memcmp(pwszSrc, pabDst, cbRead) == 0, (g_hTest, "Read data does not match host data\n"));
        RTUtf16Free(pwszSrc);
#else
        RTTEST_CHECK_MSG(g_hTest, memcmp(pTask->pvData, pabDst, cbRead) == 0, (g_hTest, "Read data does not match host data\n"));
#endif
    }

    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Read data from host:\n%.*Rhxd\n", cbRead, pabDst);

    RTMemFree(pabDst);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstTestReadFromHost_ThreadGuest(PTSTHGCMUTILSCTX pCtx, void *pvCtx)
{
    RTThreadSleep(1000); /* Fudge; wait until the host has prepared the data for the clipboard. */

    PCLIPBOARDTESTCTX  pTstCtx  = (PCLIPBOARDTESTCTX)pvCtx;
    AssertPtr(pTstCtx);

    RT_ZERO(pTstCtx->Guest.CmdCtx);
    RTTEST_CHECK_RC_OK(g_hTest, VbglR3ClipboardConnectEx(&pTstCtx->Guest.CmdCtx, VBOX_SHCL_GF_0_CONTEXT_ID));

    RTThreadSleep(1000); /* Fudge; wait until the host has prepared the data for the clipboard. */

    PCLIPBOARDTESTTASK pTstTask = (PCLIPBOARDTESTTASK)pCtx->Task.pvUser;
    AssertPtr(pTstTask);
    tstTestReadFromHost_DoIt(pTstCtx, pTstTask);

    /* Signal that the task ended. */
    TstHGCMUtilsTaskSignal(&pCtx->Task, VINF_SUCCESS);

    RTTEST_CHECK_RC_OK(g_hTest, VbglR3ClipboardDisconnectEx(&pTstCtx->Guest.CmdCtx));

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstTestReadFromHost_ClientConnectedCallback(PTSTHGCMUTILSCTX pCtx, PTSTHGCMMOCKCLIENT pClient,
                                                                     void *pvUser)
{
    RT_NOREF(pCtx, pClient);

    PCLIPBOARDTESTCTX pTstCtx = (PCLIPBOARDTESTCTX)pvUser;
    AssertPtr(pTstCtx); RT_NOREF(pTstCtx);

    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Client %RU32 connected\n", pClient->idClient);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstTestReadFromHostSetup(PCLIPBOARDTESTCTX pTstCtx, void **ppvCtx)
{
    RT_NOREF(ppvCtx);

    /* Set the right clipboard mode, so that the guest can read from the host. */
    tstClipboardSetMode(TstHgcmMockSvcInst(), VBOX_SHCL_MODE_BIDIRECTIONAL);

    /* Start the host thread first, so that the guest thread can connect to it later. */
    TSTHGCMUTILSHOSTCALLBACKS HostCallbacks;
    RT_ZERO(HostCallbacks);
    HostCallbacks.pfnOnClientConnected = tstTestReadFromHost_ClientConnectedCallback;
    TstHGCMUtilsHostThreadStart(&pTstCtx->HGCM, &HostCallbacks, pTstCtx /* pvUser */);

    PCLIPBOARDTESTTASK pTask  = &pTstCtx->Task;
    AssertPtr(pTask);
    pTask->enmFmtGst   = VBOX_SHCL_FMT_UNICODETEXT;
    pTask->enmFmtHst   = pTask->enmFmtGst;
    pTask->cbChunk     = RTRandU32Ex(1, 512);
    pTask->cbData      = RT_ALIGN_32(pTask->cbChunk * RTRandU32Ex(1, 16), 2);
    Assert(pTask->cbData % sizeof(RTUTF16) == 0);
#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
    pTask->pvData      = tstGenerateUtf8StringA(pTask->cbData);
    pTask->cbData++; /* Add terminating zero. */
#else
    pTask->pvData      = tstGenerateUtf16StringA((uint32_t)(pTask->cbData /* We use bytes == chars here */));
    pTask->cbData     *= sizeof(RTUTF16);
    pTask->cbData     += sizeof(RTUTF16); /* Add terminating zero. */
#endif
    pTask->cbProcessed = 0;

    int rc = VINF_SUCCESS;

#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
    /* Initialize the Shared Clipboard backend callbacks. */
    PSHCLBACKEND pBackend = ShClSvcGetBackend();

    SHCLCALLBACKS ShClCallbacks;
    RT_ZERO(ShClCallbacks);
    ShClCallbacks.pfnReportFormats   = tstTestReadFromHost_ReportFormatsCallback;
    ShClCallbacks.pfnOnClipboardRead = tstTestReadFromHost_OnClipboardReadCallback;
    ShClBackendSetCallbacks(pBackend, &ShClCallbacks);
#elif defined (RT_OS_WINDOWS)
    rc = ShClWinOpen(GetDesktopWindow());
    if (RT_SUCCESS(rc))
    {
        rc = ShClWinDataWrite(CF_UNICODETEXT, pTask->pvData, (uint32_t)pTask->cbData);
        ShClWinClose();
    }
#endif /* defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS) */

    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Host data (%RU32):\n%.*Rhxd\n", pTask->cbData, pTask->cbData, pTask->pvData);
    return rc;
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


/*********************************************************************************************************************************
*   Main                                                                                                                         *
*********************************************************************************************************************************/

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

int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardMockHGCM", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    PTSTHGCMMOCKSVC const pSvc = TstHgcmMockSvcInst();
    TstHgcmMockSvcCreate(pSvc);
    TstHgcmMockSvcStart(pSvc);

    /*
     * Run the tests.
     */
    if (0) /** @todo triggers assertion */
        testGuestSimple();
    if (1)
        testHostCall();

    RT_ZERO(g_TstCtx);

    PTSTHGCMUTILSCTX pCtx = &g_TstCtx.HGCM;
    TstHGCMUtilsCtxInit(pCtx, pSvc);

    PTSTHGCMUTILSTASK pTask = (PTSTHGCMUTILSTASK)TstHGCMUtilsTaskGetCurrent(pCtx);
    TstHGCMUtilsTaskInit(pTask);
    pTask->pvUser = &g_TstCtx.Task;

    for (unsigned i = 0; i < RT_ELEMENTS(g_aTests); i++)
        tstOne(&g_aTests[i]);

    TstHGCMUtilsTaskDestroy(pTask);

    TstHgcmMockSvcStop(pSvc);
    TstHgcmMockSvcDestroy(pSvc);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(g_hTest);
}

