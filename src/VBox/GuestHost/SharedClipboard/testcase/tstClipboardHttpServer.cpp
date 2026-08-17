/* $Id: tstClipboardHttpServer.cpp 115060 2026-08-17 17:28:06Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard HTTP server test case.
 */

/*
 * Copyright (C) 2023-2026 Oracle and/or its affiliates.
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

#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/http.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/utf16.h>

#ifdef TESTCASE_WITH_X11
#include <VBox/GuestHost/SharedClipboard-x11.h>
#endif

#include <VBox/GuestHost/clipboard-transfers-http.h>


/** The release logger. */
static PRTLOGGER    g_pRelLogger;
/** The current logging verbosity level. */
static unsigned     g_uVerbosity = 0;
/** Default maximum HTTP server runtime (in ms). */
static RTMSINTERVAL g_msRuntime     = RT_MS_5MIN;
/** Shutdown indicator. */
static bool         g_fShutdown     = false;
/** Manual mode indicator; allows manual (i.e. interactive) testing w/ other HTTP clients or desktop environments. */
static bool         g_fManual       = false;
#ifdef TESTCASE_WITH_X11
 /** Puts the URL on the X11 clipboard. Only works with manual mode. */
 static bool        g_fX11          = false;
#endif

/** Test files to handle + download.
 *  All files reside in a common temporary directory. */
static struct
{
    /** File mode. Used for RTFS_TYPE_XXX only. */
    RTFMODE     fMode;
    /** Local path to serve via HTTP server. */
    const char *pszPath;
    /** Expected URL path component. Has to be fully percent-encoded. */
    const char *pszUrl;
    /** File allocation size.
     *  Specify UINT64_MAX for random size. */
    uint64_t    cbSize;
    /** Expected test result. */
    int         rc;
} g_aTests[] =
{
    /*
     * Files.
     */
    { RTFS_TYPE_FILE, "file1.txt",                          "file1.txt",                  _64K,       VINF_SUCCESS },
    /* Note: For RTHttpGetFile() the URL needs to be percent-encoded. */
    { RTFS_TYPE_FILE, "file2 with spaces.txt",              "file2%20with%20spaces.txt",  _64K,       VINF_SUCCESS },
    { RTFS_TYPE_FILE, "file #%20?.txt",                     "file%20%23%2520%3F.txt",     42,         VINF_SUCCESS },
    { RTFS_TYPE_FILE, "bigfile.bin",                        "bigfile.bin",                _512M,      VINF_SUCCESS },
    { RTFS_TYPE_FILE, "zerobytes",                          "zerobytes",                  0,          VINF_SUCCESS },
    { RTFS_TYPE_FILE, "file\\with\\slashes",                "file%5Cwith%5Cslashes",      42,         VINF_SUCCESS },
    /* Korean encoding. */
    { RTFS_TYPE_FILE, "VirtualBox가 크게 성공했습니다!",         "VirtualBox%EA%B0%80%20%ED%81%AC%EA%B2%8C%20%EC%84%B1%EA%B3%B5%ED%96%88%EC%8A%B5%EB%8B%88%EB%8B%A4%21", 42, VINF_SUCCESS },
    /*
     * Other stuff (not supported).
     */
    { RTFS_TYPE_DIRECTORY, "test-directory",                "test-directory",             0,          VERR_HTTP_NOT_SUPPORTED }
};

/**
 * Creates and registers a single local transfer for HTTP serving.
 *
 * @returns VBox status code.
 * @param   hTest           The test handle.
 * @param   pTransferCtx    Transfer context to register with.
 * @param   pSrv            HTTP server to register with.
 * @param   pszPath         Local path to serve.
 * @param   pProvider       Local transfer provider.
 */
static int tstCreateTransferSingle(RTTEST hTest, PSHCLTRANSFERCTX pTransferCtx, PSHCLHTTPSERVER pSrv,
                                   const char *pszPath, PSHCLTXPROVIDER pProvider)
{
    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "tstCreateTransferSingle: pszPath=%s\n", pszPath);

    PSHCLTRANSFER pTx = NULL;
    bool fCtxRegistered = false;
    bool fHttpRegistered = false;
    int rc = VINF_SUCCESS;

    do
    {
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferCreate(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL,
                                                                NULL /* Callbacks */, &pTx));
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferSetProvider(pTx, pProvider));
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferRootsSetFromPath(pTx, pszPath));
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferInit(pTx));
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferCtxRegister(pTransferCtx, pTx, NULL));
        fCtxRegistered = true;
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferHttpServerRegisterTransfer(pSrv, pTx));
        fHttpRegistered = true;
    } while (0);

    if (RT_FAILURE(rc))
    {
        if (fHttpRegistered)
        {
            int rc2 = ShClTransferHttpServerUnregisterTransfer(pSrv, pTx);
            RTTEST_CHECK_RC_OK(hTest, rc2);
            fHttpRegistered = RT_FAILURE(rc2);
        }
        if (!fHttpRegistered && fCtxRegistered)
        {
            int rc2 = ShClTransferCtxUnregisterById(pTransferCtx, ShClTransferGetID(pTx));
            RTTEST_CHECK_RC_OK(hTest, rc2);
            fCtxRegistered = RT_FAILURE(rc2);
        }
        if (!fHttpRegistered && !fCtxRegistered && pTx)
            RTTEST_CHECK_RC_OK(hTest, ShClTransferDestroy(pTx));
    }

    return rc;
}

/**
 * Performs one malformed HTTP GET request and checks the returned status.
 *
 * @param   hTest       The test handle.
 * @param   hClient     HTTP client handle.
 * @param   pszUrl      URL to request.
 * @param   rcExpected  Expected IPRT status code.
 */
static void tstMalformedGet(RTTEST hTest, RTHTTP hClient, const char *pszUrl, int rcExpected)
{
    char szDstFile[RTPATH_MAX];
    RTTEST_CHECK_RC_OK_RETV(hTest, RTPathTemp(szDstFile, sizeof(szDstFile)));
    RTTEST_CHECK_RC_OK_RETV(hTest, RTPathAppend(szDstFile, sizeof(szDstFile), "tstClipboardHttpServer-XXXXXX"));
    RTTEST_CHECK_RC_OK_RETV(hTest, RTFileCreateTemp(szDstFile, 0600));
    RTTEST_CHECK_RC(hTest, RTHttpGetFile(hClient, pszUrl, szDstFile), rcExpected);
    RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szDstFile));
}


/**
 * Tests malformed HTTP paths.
 *
 * @param   hTest       The test handle.
 * @param   hClient     HTTP client handle.
 * @param   pszBaseUrl  Base URL of a registered transfer.
 */
static void tstMalformedPaths(RTTEST hTest, RTHTTP hClient, const char *pszBaseUrl)
{
    RTTestSub(hTest, "malformed HTTP transfer paths");

    char szUrl[RTPATH_MAX];
    RTTEST_CHECK(hTest, RTStrPrintf2(szUrl, sizeof(szUrl), "%s", pszBaseUrl));
    tstMalformedGet(hTest, hClient, szUrl, VERR_HTTP_BAD_REQUEST);

    RTTEST_CHECK(hTest, RTStrPrintf2(szUrl, sizeof(szUrl), "%s/", pszBaseUrl));
    tstMalformedGet(hTest, hClient, szUrl, VERR_HTTP_BAD_REQUEST);

    RTTEST_CHECK(hTest, RTStrPrintf2(szUrl, sizeof(szUrl), "%sx/file1.txt", pszBaseUrl));
    tstMalformedGet(hTest, hClient, szUrl, VERR_HTTP_NOT_FOUND);
}


/**
 * Tests duplicate HTTP transfer registration handling.
 *
 * @param   hTest           The test handle.
 * @param   pTransferCtx    Transfer context to register with.
 * @param   pSrv            HTTP server to register with.
 * @param   pszPath         Local path to serve.
 * @param   pProvider       Local transfer provider.
 */
static void tstDuplicateTransferRegistration(RTTEST hTest, PSHCLTRANSFERCTX pTransferCtx, PSHCLHTTPSERVER pSrv,
                                             const char *pszPath, PSHCLTXPROVIDER pProvider)
{
    RTTestSub(hTest, "duplicate HTTP transfer registration");

    PSHCLTRANSFER pTx = NULL;
    bool fCtxRegistered = false;
    bool fHttpRegistered = false;
    int rc = VINF_SUCCESS;

    do
    {
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferCreate(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL,
                                                                NULL /* Callbacks */, &pTx));
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferSetProvider(pTx, pProvider));
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferRootsSetFromPath(pTx, pszPath));
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferInit(pTx));
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferCtxRegister(pTransferCtx, pTx, NULL));
        fCtxRegistered = true;
        RTTEST_CHECK_RC_OK_BREAK(hTest, rc = ShClTransferHttpServerRegisterTransfer(pSrv, pTx));
        fHttpRegistered = true;

        uint32_t const cTransfers = ShClTransferHttpServerGetTransferCount(pSrv);
        RTTEST_CHECK_RC(hTest, ShClTransferHttpServerRegisterTransfer(pSrv, pTx), VERR_ALREADY_EXISTS);
        RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransferCount(pSrv) == cTransfers);

        rc = ShClTransferHttpServerUnregisterTransfer(pSrv, pTx);
        RTTEST_CHECK_RC_OK(hTest, rc);
        fHttpRegistered = RT_FAILURE(rc);
        if (RT_FAILURE(rc))
            break;
        RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransferCount(pSrv) + 1 == cTransfers);

        rc = ShClTransferCtxUnregisterById(pTransferCtx, ShClTransferGetID(pTx));
        RTTEST_CHECK_RC_OK(hTest, rc);
        fCtxRegistered = RT_FAILURE(rc);
        if (RT_FAILURE(rc))
            break;

        rc = ShClTransferDestroy(pTx);
        RTTEST_CHECK_RC_OK(hTest, rc);
        pTx = NULL;
    } while (0);

    if (RT_FAILURE(rc))
    {
        if (fHttpRegistered)
        {
            int rc2 = ShClTransferHttpServerUnregisterTransfer(pSrv, pTx);
            RTTEST_CHECK_RC_OK(hTest, rc2);
            fHttpRegistered = RT_FAILURE(rc2);
        }
        if (!fHttpRegistered && fCtxRegistered)
        {
            int rc2 = ShClTransferCtxUnregisterById(pTransferCtx, ShClTransferGetID(pTx));
            RTTEST_CHECK_RC_OK(hTest, rc2);
            fCtxRegistered = RT_FAILURE(rc2);
        }
        if (!fHttpRegistered && !fCtxRegistered && pTx)
            RTTEST_CHECK_RC_OK(hTest, ShClTransferDestroy(pTx));
    }
}


/** Provider wrapper state used for observing and pausing HTTP object access. */
typedef struct TSTHTTPPROVIDERCTX
{
    /** The wrapped local provider interface. */
    SHCLTXPROVIDERIFACE LocalIface;
    /** Signalled whenever a read enters the provider. */
    RTSEMEVENT          hReadEntered;
    /** Releases all reads paused by the test. */
    RTSEMEVENTMULTI     hReadContinue;
    /** Number of successful object opens. */
    volatile uint32_t   cObjOpens;
    /** Number of successful object closes. */
    volatile uint32_t   cObjCloses;
    /** Number of object reads. */
    volatile uint32_t   cObjReads;
    /** Number of initial reads to pause. */
    uint32_t            cReadsToPause;
} TSTHTTPPROVIDERCTX;
/** Pointer to an HTTP provider wrapper state. */
typedef TSTHTTPPROVIDERCTX *PTSTHTTPPROVIDERCTX;

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjOpen */
static DECLCALLBACK(int) tstHttpProviderObjOpen(PSHCLTXPROVIDERCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms,
                                               PSHCLOBJHANDLE phObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->cbUser == sizeof(TSTHTTPPROVIDERCTX), VERR_INVALID_PARAMETER);
    PTSTHTTPPROVIDERCTX pThis = (PTSTHTTPPROVIDERCTX)pCtx->pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    int rc = pThis->LocalIface.pfnObjOpen(pCtx, pCreateParms, phObj);
    if (RT_SUCCESS(rc))
        ASMAtomicIncU32(&pThis->cObjOpens);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjClose */
static DECLCALLBACK(int) tstHttpProviderObjClose(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->cbUser == sizeof(TSTHTTPPROVIDERCTX), VERR_INVALID_PARAMETER);
    PTSTHTTPPROVIDERCTX pThis = (PTSTHTTPPROVIDERCTX)pCtx->pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    int rc = pThis->LocalIface.pfnObjClose(pCtx, hObj);
    if (RT_SUCCESS(rc))
        ASMAtomicIncU32(&pThis->cObjCloses);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjRead */
static DECLCALLBACK(int) tstHttpProviderObjRead(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj, void *pvData,
                                               uint32_t cbData, uint32_t fFlags, uint32_t *pcbRead)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->cbUser == sizeof(TSTHTTPPROVIDERCTX), VERR_INVALID_PARAMETER);
    PTSTHTTPPROVIDERCTX pThis = (PTSTHTTPPROVIDERCTX)pCtx->pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    uint32_t const iRead = ASMAtomicIncU32(&pThis->cObjReads);
    if (iRead <= pThis->cReadsToPause)
    {
        int rc = RTSemEventSignal(pThis->hReadEntered);
        if (RT_SUCCESS(rc))
            rc = RTSemEventMultiWait(pThis->hReadContinue, RT_MS_30SEC);
        if (RT_FAILURE(rc))
            return rc;
    }

    return pThis->LocalIface.pfnObjRead(pCtx, hObj, pvData, cbData, fFlags, pcbRead);
}

/**
 * Initializes an observable local provider.
 *
 * @returns VBox status code.
 * @param   pThis           Provider wrapper state to initialize.
 * @param   pProvider       Provider interface to initialize.
 */
static int tstHttpProviderInit(PTSTHTTPPROVIDERCTX pThis, PSHCLTXPROVIDER pProvider)
{
    RT_ZERO(*pThis);
    RT_ZERO(*pProvider);

    int rc = RTSemEventCreate(&pThis->hReadEntered);
    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventMultiCreate(&pThis->hReadContinue);
        if (RT_SUCCESS(rc))
        {
            AssertPtrReturn(ShClTransferProviderLocalQueryInterface(pProvider), VERR_INTERNAL_ERROR);
            pThis->LocalIface = pProvider->Interface;

            pProvider->Interface.pfnObjOpen  = tstHttpProviderObjOpen;
            pProvider->Interface.pfnObjClose = tstHttpProviderObjClose;
            pProvider->Interface.pfnObjRead  = tstHttpProviderObjRead;
            pProvider->pvUser                = pThis;
            pProvider->cbUser                = sizeof(*pThis);
            return VINF_SUCCESS;
        }

        RTSemEventDestroy(pThis->hReadEntered);
        pThis->hReadEntered = NIL_RTSEMEVENT;
    }

    return rc;
}

/**
 * Terminates an observable local provider.
 *
 * @param   pThis           Provider wrapper state to terminate.
 */
static void tstHttpProviderTerm(PTSTHTTPPROVIDERCTX pThis)
{
    if (pThis->hReadContinue != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiSignal(pThis->hReadContinue);
        RTSemEventMultiDestroy(pThis->hReadContinue);
        pThis->hReadContinue = NIL_RTSEMEVENTMULTI;
    }
    if (pThis->hReadEntered != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThis->hReadEntered);
        pThis->hReadEntered = NIL_RTSEMEVENT;
    }
}

/**
 * Creates a deterministic file for an HTTP lifetime test.
 *
 * @returns VBox status code.
 * @param   pszPath         File path.
 * @param   cbFile          File size in bytes.
 */
static int tstCreatePatternFile(const char *pszPath, size_t cbFile)
{
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszPath, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(rc))
    {
        uint8_t abBuf[_64K];
        for (size_t i = 0; i < sizeof(abBuf); i++)
            abBuf[i] = (uint8_t)(i * 131U + 17U);

        while (cbFile > 0 && RT_SUCCESS(rc))
        {
            size_t const cbToWrite = RT_MIN(cbFile, sizeof(abBuf));
            rc = RTFileWrite(hFile, abBuf, cbToWrite, NULL);
            cbFile -= cbToWrite;
        }

        int rc2 = RTFileClose(hFile);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}

/**
 * Creates and registers one transfer with both a transfer context and HTTP server.
 *
 * @returns VBox status code.
 * @param   pTransferCtx    Transfer context to register with.
 * @param   pSrv            HTTP server to register with.
 * @param   pszPath         Local path to serve.
 * @param   pProvider       Provider to use.
 * @param   idTransfer      Transfer ID to request, or NIL_SHCLTRANSFERID for an automatically allocated ID.
 * @param   ppTransfer      Where to return the transfer on success.
 */
static int tstCreateRegisteredTransfer(PSHCLTRANSFERCTX pTransferCtx, PSHCLHTTPSERVER pSrv, const char *pszPath,
                                       PSHCLTXPROVIDER pProvider, SHCLTRANSFERID idTransfer, PSHCLTRANSFER *ppTransfer)
{
    PSHCLTRANSFER pTransfer = NULL;
    bool fCtxRegistered = false;
    bool fHttpRegistered = false;

    int rc = ShClTransferCreate(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTransfer);
    if (RT_SUCCESS(rc))
        rc = ShClTransferSetProvider(pTransfer, pProvider);
    if (RT_SUCCESS(rc))
        rc = ShClTransferRootsSetFromPath(pTransfer, pszPath);
    if (RT_SUCCESS(rc))
        rc = ShClTransferInit(pTransfer);
    if (RT_SUCCESS(rc))
    {
        if (idTransfer == NIL_SHCLTRANSFERID)
            rc = ShClTransferCtxRegister(pTransferCtx, pTransfer, NULL);
        else
            rc = ShClTransferCtxRegisterById(pTransferCtx, pTransfer, idTransfer);
        fCtxRegistered = RT_SUCCESS(rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferHttpServerRegisterTransfer(pSrv, pTransfer);
        fHttpRegistered = RT_SUCCESS(rc);
    }

    if (RT_SUCCESS(rc))
    {
        *ppTransfer = pTransfer;
        return VINF_SUCCESS;
    }

    if (fHttpRegistered)
        ShClTransferHttpServerUnregisterTransfer(pSrv, pTransfer);
    if (fCtxRegistered)
        ShClTransferCtxUnregisterById(pTransferCtx, ShClTransferGetID(pTransfer));
    if (pTransfer)
        ShClTransferDestroy(pTransfer);
    return rc;
}

/** HTTP GET worker context. */
typedef struct TSTHTTPGETCTX
{
    /** URL to download. */
    const char *pszUrl;
    /** Destination file path. */
    const char *pszDst;
    /** Result of the HTTP operation. */
    int         rc;
} TSTHTTPGETCTX;
/** Pointer to an HTTP GET worker context. */
typedef TSTHTTPGETCTX *PTSTHTTPGETCTX;

/** Performs an HTTP GET on a worker thread. */
static DECLCALLBACK(int) tstHttpGetThread(RTTHREAD hThread, void *pvUser)
{
    PTSTHTTPGETCTX pCtx = (PTSTHTTPGETCTX)pvUser;

    int rc = RTThreadUserSignal(hThread);
    if (RT_SUCCESS(rc))
    {
        RTHTTP hClient;
        rc = RTHttpCreate(&hClient);
        if (RT_SUCCESS(rc))
        {
            rc = RTHttpSetProxy(hClient, NULL /* pszProxyUrl */, 0 /* uPort */,
                                NULL /* pszProxyUser */, NULL /* pszProxyPwd */);
            if (RT_SUCCESS(rc))
                rc = RTHttpGetFile(hClient, pCtx->pszUrl, pCtx->pszDst);

            int rc2 = RTHttpDestroy(hClient);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }

    pCtx->rc = rc;
    return rc;
}

/** HTTP transfer unregister worker context. */
typedef struct TSTHTTPUNREGISTERCTX
{
    /** HTTP server to unregister from. */
    PSHCLHTTPSERVER pSrv;
    /** Transfer to unregister. */
    PSHCLTRANSFER   pTransfer;
    /** Result of the unregister operation. */
    int             rc;
} TSTHTTPUNREGISTERCTX;
/** Pointer to an HTTP transfer unregister worker context. */
typedef TSTHTTPUNREGISTERCTX *PTSTHTTPUNREGISTERCTX;

/** Unregisters an HTTP transfer on a worker thread. */
static DECLCALLBACK(int) tstHttpUnregisterThread(RTTHREAD hThread, void *pvUser)
{
    PTSTHTTPUNREGISTERCTX pCtx = (PTSTHTTPUNREGISTERCTX)pvUser;

    int rc = RTThreadUserSignal(hThread);
    if (RT_SUCCESS(rc))
        rc = ShClTransferHttpServerUnregisterTransfer(pCtx->pSrv, pCtx->pTransfer);

    pCtx->rc = rc;
    return rc;
}

/**
 * Checks that HEAD and sequential GET requests each own and close their object handle.
 *
 * @param   hTest           The test handle.
 * @param   pszTempDir      Temporary directory for test files.
 */
static void tstRepeatedRequestHandles(RTTEST hTest, const char *pszTempDir)
{
    RTTestSub(hTest, "request-owned object handles");

    char szSrcFile[RTPATH_MAX];
    RTTEST_CHECK_RETV(hTest, RTStrPrintf2(szSrcFile, sizeof(szSrcFile), "%s", pszTempDir) > 0);
    RTTEST_CHECK_RC_OK_RETV(hTest, RTPathAppend(szSrcFile, sizeof(szSrcFile), "request-handles.bin"));
    RTTEST_CHECK_RC_OK_RETV(hTest, tstCreatePatternFile(szSrcFile, _128K));

    TSTHTTPPROVIDERCTX ProviderCtx;
    SHCLTXPROVIDER Provider;
    bool fProviderInitialized = false;
    bool fServerInitialized = false;
    bool fCtxInitialized = false;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID idTransfer = NIL_SHCLTRANSFERID;
    char *pszUrl = NULL;
    RTHTTP hClient = NIL_RTHTTP;

    SHCLHTTPSERVER HttpSrv;
    SHCLTRANSFERCTX TransferCtx;
    uint16_t uPort = 0;
    int rc = tstHttpProviderInit(&ProviderCtx, &Provider);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        fProviderInitialized = true;
        rc = ShClTransferHttpServerInit(&HttpSrv);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        fServerInitialized = true;
        rc = ShClTransferHttpServerStart(&HttpSrv, 32 /* cMaxAttempts */, &uPort);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferCtxInit(&TransferCtx);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        fCtxInitialized = true;
        rc = ShClTransferCtxBeginSession(&TransferCtx, 101);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = tstCreateRegisteredTransfer(&TransferCtx, &HttpSrv, szSrcFile, &Provider,
                                         NIL_SHCLTRANSFERID, &pTransfer);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        idTransfer = ShClTransferGetID(pTransfer);
        pszUrl = ShClTransferHttpServerGetUrlA(&HttpSrv, idTransfer, 0 /* idxEntry */);
        RTTEST_CHECK(hTest, pszUrl != NULL);
        if (!pszUrl)
            rc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(rc))
    {
        rc = RTHttpCreate(&hClient);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = RTHttpSetProxy(hClient, NULL /* pszProxyUrl */, 0 /* uPort */,
                            NULL /* pszProxyUser */, NULL /* pszProxyPwd */);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }

    if (RT_SUCCESS(rc))
    {
        for (uint32_t i = 0; i < 3; i++)
        {
            void *pvResponse = NULL;
            size_t cbResponse = 0;
            rc = RTHttpGetHeaderBinary(hClient, pszUrl, &pvResponse, &cbResponse);
            RTTEST_CHECK_RC_OK(hTest, rc);
            RTHttpFreeResponse(pvResponse);
            if (RT_FAILURE(rc))
                break;

            RTTEST_CHECK(hTest, ASMAtomicReadU32(&ProviderCtx.cObjOpens) == i + 1);
            RTTEST_CHECK(hTest, ASMAtomicReadU32(&ProviderCtx.cObjCloses) == i + 1);
        }
    }

    if (RT_SUCCESS(rc))
    {
        for (uint32_t i = 0; i < 2; i++)
        {
            char szDstFile[RTPATH_MAX];
            RTTEST_CHECK_BREAK(hTest, RTStrPrintf(szDstFile, sizeof(szDstFile), "%s/request-handles-%RU32.bin",
                                                 pszTempDir, i) > 0);

            rc = RTHttpGetFile(hClient, pszUrl, szDstFile);
            RTTEST_CHECK_RC_OK(hTest, rc);
            if (RT_SUCCESS(rc))
                RTTEST_CHECK_RC_OK(hTest, RTFileCompare(szSrcFile, szDstFile));
            RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szDstFile));
            if (RT_FAILURE(rc))
                break;

            RTTEST_CHECK(hTest, ASMAtomicReadU32(&ProviderCtx.cObjOpens) == i + 4);
            RTTEST_CHECK(hTest, ASMAtomicReadU32(&ProviderCtx.cObjCloses) == i + 4);
        }
    }

    if (hClient != NIL_RTHTTP)
        RTTEST_CHECK_RC_OK(hTest, RTHttpDestroy(hClient));
    RTStrFree(pszUrl);

    if (pTransfer)
    {
        if (fServerInitialized && ShClTransferHttpServerGetTransfer(&HttpSrv, idTransfer))
            RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerUnregisterTransfer(&HttpSrv, pTransfer));
        if (fCtxInitialized && ShClTransferCtxGetTransferById(&TransferCtx, idTransfer) == pTransfer)
            RTTEST_CHECK_RC_OK(hTest, ShClTransferCtxUnregisterById(&TransferCtx, idTransfer));
        RTTEST_CHECK_RC_OK(hTest, ShClTransferDestroy(pTransfer));
    }
    if (fCtxInitialized)
        ShClTransferCtxDestroy(&TransferCtx);
    if (fServerInitialized)
    {
        RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerDestroy(&HttpSrv));
        RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerDestroy(&HttpSrv));
    }
    if (fProviderInitialized)
        tstHttpProviderTerm(&ProviderCtx);
    RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szSrcFile));
}

/**
 * Checks that a stale transfer key cannot unregister a newer transfer which reused its numeric ID.
 *
 * @param   hTest           The test handle.
 * @param   pszTempDir      Temporary directory for test files.
 */
static void tstStaleKeyUnregister(RTTEST hTest, const char *pszTempDir)
{
    RTTestSub(hTest, "stale transfer key unregister");

    char szSrcFile[RTPATH_MAX];
    RTTEST_CHECK_RETV(hTest, RTStrPrintf2(szSrcFile, sizeof(szSrcFile), "%s", pszTempDir) > 0);
    RTTEST_CHECK_RC_OK_RETV(hTest, RTPathAppend(szSrcFile, sizeof(szSrcFile), "stale-key.bin"));
    RTTEST_CHECK_RC_OK_RETV(hTest, tstCreatePatternFile(szSrcFile, _4K));

    SHCLTXPROVIDER Provider;
    RT_ZERO(Provider);
    RTTEST_CHECK_RETV(hTest, ShClTransferProviderLocalQueryInterface(&Provider) != NULL);

    SHCLHTTPSERVER HttpSrv;
    SHCLTRANSFERCTX OldTransferCtx;
    SHCLTRANSFERCTX NewTransferCtx;
    bool fServerInitialized = false;
    bool fOldCtxInitialized = false;
    bool fNewCtxInitialized = false;
    PSHCLTRANSFER pOldTransfer = NULL;
    PSHCLTRANSFER pNewTransfer = NULL;
    char *pszOldUrl = NULL;
    char *pszNewUrl = NULL;
    size_t cbOldUrl = 0;
    size_t cbNewUrl = 0;
    uint16_t uPort = 0;

    int rc = ShClTransferHttpServerInit(&HttpSrv);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        fServerInitialized = true;
        rc = ShClTransferHttpServerStart(&HttpSrv, 32 /* cMaxAttempts */, &uPort);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferCtxInit(&OldTransferCtx);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        fOldCtxInitialized = true;
        rc = ShClTransferCtxBeginSession(&OldTransferCtx, 201);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferCtxInit(&NewTransferCtx);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        fNewCtxInitialized = true;
        rc = ShClTransferCtxBeginSession(&NewTransferCtx, 202);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = tstCreateRegisteredTransfer(&OldTransferCtx, &HttpSrv, szSrcFile, &Provider, 42, &pOldTransfer);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = tstCreateRegisteredTransfer(&NewTransferCtx, &HttpSrv, szSrcFile, &Provider, 42, &pNewTransfer);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, ShClTransferKeyIsValid(ShClTransferGetSessionId(pOldTransfer), 42,
                                                   ShClTransferGetGeneration(pOldTransfer)));
        RTTEST_CHECK(hTest, ShClTransferKeyIsValid(ShClTransferGetSessionId(pNewTransfer), 42,
                                                   ShClTransferGetGeneration(pNewTransfer)));
        RTTEST_CHECK(hTest, ShClTransferGetSessionId(pOldTransfer) != ShClTransferGetSessionId(pNewTransfer));
        RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransferCount(&HttpSrv) == 2);

        rc = ShClTransferHttpConvertToStringList(&HttpSrv, pOldTransfer, &pszOldUrl, &cbOldUrl);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferHttpConvertToStringList(&HttpSrv, pNewTransfer, &pszNewUrl, &cbNewUrl);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, cbOldUrl == strlen(pszOldUrl) + 1);
        RTTEST_CHECK(hTest, cbNewUrl == strlen(pszNewUrl) + 1);
        RTTEST_CHECK(hTest, RTStrCmp(pszOldUrl, pszNewUrl) != 0);

        rc = ShClTransferHttpServerUnregisterTransfer(&HttpSrv, pOldTransfer);
        RTTEST_CHECK_RC_OK(hTest, rc);
        RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransferCount(&HttpSrv) == 1);
        RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransfer(&HttpSrv, 42));
    }
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferHttpServerUnregisterTransfer(&HttpSrv, pOldTransfer);
        RTTEST_CHECK_RC_OK(hTest, rc);
        RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransferCount(&HttpSrv) == 1);
        RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransfer(&HttpSrv, 42));
    }
    if (RT_SUCCESS(rc))
    {
        RTHTTP hClient;
        rc = RTHttpCreate(&hClient);
        RTTEST_CHECK_RC_OK(hTest, rc);
        if (RT_SUCCESS(rc))
        {
            rc = RTHttpSetProxy(hClient, NULL /* pszProxyUrl */, 0 /* uPort */,
                                NULL /* pszProxyUser */, NULL /* pszProxyPwd */);
            RTTEST_CHECK_RC_OK(hTest, rc);
            if (RT_SUCCESS(rc))
            {
                tstMalformedGet(hTest, hClient, pszOldUrl, VERR_HTTP_NOT_FOUND);

                char szDstFile[RTPATH_MAX];
                RTTEST_CHECK(hTest, RTStrPrintf(szDstFile, sizeof(szDstFile), "%s/stale-key-copy.bin", pszTempDir) > 0);
                rc = RTHttpGetFile(hClient, pszNewUrl, szDstFile);
                RTTEST_CHECK_RC_OK(hTest, rc);
                if (RT_SUCCESS(rc))
                    RTTEST_CHECK_RC_OK(hTest, RTFileCompare(szSrcFile, szDstFile));
                if (RTFileExists(szDstFile))
                    RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szDstFile));
            }

            int rc2 = RTHttpDestroy(hClient);
            RTTEST_CHECK_RC_OK(hTest, rc2);
        }
    }

    RTStrFree(pszNewUrl);
    RTStrFree(pszOldUrl);

    if (pNewTransfer)
    {
        if (fServerInitialized)
            RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerUnregisterTransfer(&HttpSrv, pNewTransfer));
        if (fNewCtxInitialized && ShClTransferCtxGetTransferById(&NewTransferCtx, 42) == pNewTransfer)
            RTTEST_CHECK_RC_OK(hTest, ShClTransferCtxUnregisterById(&NewTransferCtx, 42));
        RTTEST_CHECK_RC_OK(hTest, ShClTransferDestroy(pNewTransfer));
    }
    if (pOldTransfer)
    {
        if (fServerInitialized)
            RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerUnregisterTransfer(&HttpSrv, pOldTransfer));
        if (fOldCtxInitialized && ShClTransferCtxGetTransferById(&OldTransferCtx, 42) == pOldTransfer)
            RTTEST_CHECK_RC_OK(hTest, ShClTransferCtxUnregisterById(&OldTransferCtx, 42));
        RTTEST_CHECK_RC_OK(hTest, ShClTransferDestroy(pOldTransfer));
    }
    if (fNewCtxInitialized)
        ShClTransferCtxDestroy(&NewTransferCtx);
    if (fOldCtxInitialized)
        ShClTransferCtxDestroy(&OldTransferCtx);
    if (fServerInitialized)
        RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerDestroy(&HttpSrv));
    RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szSrcFile));
}

/**
 * Checks that unregister waits for an active request while retaining its transfer until request end.
 *
 * @param   hTest           The test handle.
 * @param   pszTempDir      Temporary directory for test files.
 */
static void tstUnregisterDuringRequest(RTTEST hTest, const char *pszTempDir)
{
    RTTestSub(hTest, "unregister during active request");

    char szSrcFile[RTPATH_MAX];
    char szDstFile[RTPATH_MAX];
    RTTEST_CHECK_RETV(hTest, RTStrPrintf2(szSrcFile, sizeof(szSrcFile), "%s", pszTempDir) > 0);
    RTTEST_CHECK_RC_OK_RETV(hTest, RTPathAppend(szSrcFile, sizeof(szSrcFile), "active-request.bin"));
    RTTEST_CHECK_RETV(hTest, RTStrPrintf2(szDstFile, sizeof(szDstFile), "%s", pszTempDir) > 0);
    RTTEST_CHECK_RC_OK_RETV(hTest, RTPathAppend(szDstFile, sizeof(szDstFile), "active-request-copy.bin"));
    RTTEST_CHECK_RC_OK_RETV(hTest, tstCreatePatternFile(szSrcFile, _128K));

    TSTHTTPPROVIDERCTX ProviderCtx;
    SHCLTXPROVIDER Provider;
    bool fProviderInitialized = false;
    bool fServerInitialized = false;
    bool fCtxInitialized = false;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID idTransfer = NIL_SHCLTRANSFERID;
    char *pszUrl = NULL;
    RTTHREAD hGetThread = NIL_RTTHREAD;
    RTTHREAD hUnregisterThread = NIL_RTTHREAD;
    bool fGetThreadStarted = false;
    bool fUnregisterThreadStarted = false;
    bool fUnregisterThreadJoined = false;

    SHCLHTTPSERVER HttpSrv;
    SHCLTRANSFERCTX TransferCtx;
    uint16_t uPort = 0;
    int rc = tstHttpProviderInit(&ProviderCtx, &Provider);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        fProviderInitialized = true;
        ProviderCtx.cReadsToPause = 1;
        rc = ShClTransferHttpServerInit(&HttpSrv);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        fServerInitialized = true;
        rc = ShClTransferHttpServerStart(&HttpSrv, 32 /* cMaxAttempts */, &uPort);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferCtxInit(&TransferCtx);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        fCtxInitialized = true;
        rc = ShClTransferCtxBeginSession(&TransferCtx, 301);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = tstCreateRegisteredTransfer(&TransferCtx, &HttpSrv, szSrcFile, &Provider,
                                         NIL_SHCLTRANSFERID, &pTransfer);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        idTransfer = ShClTransferGetID(pTransfer);
        pszUrl = ShClTransferHttpServerGetUrlA(&HttpSrv, idTransfer, 0 /* idxEntry */);
        RTTEST_CHECK(hTest, pszUrl != NULL);
        if (!pszUrl)
            rc = VERR_NO_MEMORY;
    }

    TSTHTTPGETCTX GetCtx = { pszUrl, szDstFile, VERR_IPE_UNINITIALIZED_STATUS };
    TSTHTTPUNREGISTERCTX UnregisterCtx = { &HttpSrv, pTransfer, VERR_IPE_UNINITIALIZED_STATUS };
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate(&hGetThread, tstHttpGetThread, &GetCtx, 0 /* cbStack */, RTTHREADTYPE_DEFAULT,
                            RTTHREADFLAGS_WAITABLE, "ShClHttpGet");
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        fGetThreadStarted = true;
        rc = RTThreadUserWait(hGetThread, RT_MS_5SEC);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventWait(ProviderCtx.hReadEntered, RT_MS_10SEC);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }

    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate(&hUnregisterThread, tstHttpUnregisterThread, &UnregisterCtx, 0 /* cbStack */,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "ShClHttpUnreg");
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        fUnregisterThreadStarted = true;
        rc = RTThreadUserWait(hUnregisterThread, RT_MS_5SEC);
        RTTEST_CHECK_RC_OK(hTest, rc);
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Unregister removes the transfer from lookup before waiting for the
         * active request.  Wait for that point so the thread timeout below
         * proves the request drain, rather than merely a scheduling delay.
         */
        for (uint32_t i = 0; i < 1000 && ShClTransferHttpServerGetTransferCount(&HttpSrv) != 0; ++i)
            RTThreadSleep(1);
        RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransferCount(&HttpSrv) == 0);

        int rcUnregisterThread = VERR_IPE_UNINITIALIZED_STATUS;
        int const rcWait = RTThreadWait(hUnregisterThread, 100 /* msTimeout */, &rcUnregisterThread);
        RTTEST_CHECK_RC(hTest, rcWait, VERR_TIMEOUT);
    }

    if (fProviderInitialized)
        RTTEST_CHECK_RC_OK(hTest, RTSemEventMultiSignal(ProviderCtx.hReadContinue));
    if (fGetThreadStarted)
    {
        int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
        int rcWait = RTThreadWait(hGetThread, RT_MS_10SEC, &rcThread);
        RTTEST_CHECK_RC_OK(hTest, rcWait);
        if (RT_FAILURE(rcWait))
            rcWait = RTThreadWait(hGetThread, RT_INDEFINITE_WAIT, &rcThread);
        if (RT_SUCCESS(rcWait))
        {
            RTTEST_CHECK_RC_OK(hTest, rcThread);
            RTTEST_CHECK_RC_OK(hTest, GetCtx.rc);
            if (RT_SUCCESS(GetCtx.rc))
                RTTEST_CHECK_RC_OK(hTest, RTFileCompare(szSrcFile, szDstFile));
        }
    }
    if (fUnregisterThreadStarted)
    {
        int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
        int rcWait = RTThreadWait(hUnregisterThread, RT_MS_10SEC, &rcThread);
        RTTEST_CHECK_RC_OK(hTest, rcWait);
        if (RT_FAILURE(rcWait))
            rcWait = RTThreadWait(hUnregisterThread, RT_INDEFINITE_WAIT, &rcThread);
        if (RT_SUCCESS(rcWait))
        {
            fUnregisterThreadJoined = true;
            RTTEST_CHECK_RC_OK(hTest, rcThread);
            RTTEST_CHECK_RC_OK(hTest, UnregisterCtx.rc);
            RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransferCount(&HttpSrv) == 0);
            RTTEST_CHECK(hTest, !ShClTransferHttpServerGetTransfer(&HttpSrv, idTransfer));
        }
    }

    RTTEST_CHECK(hTest, ASMAtomicReadU32(&ProviderCtx.cObjOpens) == 1);
    RTTEST_CHECK(hTest, ASMAtomicReadU32(&ProviderCtx.cObjCloses) == 1);

    RTStrFree(pszUrl);
    if (fUnregisterThreadStarted && !fUnregisterThreadJoined)
    {
        int rcThread;
        RTTEST_CHECK_RC_OK(hTest, RTThreadWait(hUnregisterThread, RT_INDEFINITE_WAIT, &rcThread));
    }
    if (pTransfer)
    {
        if (fServerInitialized && ShClTransferHttpServerGetTransfer(&HttpSrv, idTransfer))
            RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerUnregisterTransfer(&HttpSrv, pTransfer));
        if (fCtxInitialized && ShClTransferCtxGetTransferById(&TransferCtx, idTransfer) == pTransfer)
            RTTEST_CHECK_RC_OK(hTest, ShClTransferCtxUnregisterById(&TransferCtx, idTransfer));
        RTTEST_CHECK_RC_OK(hTest, ShClTransferDestroy(pTransfer));
    }
    if (fCtxInitialized)
        ShClTransferCtxDestroy(&TransferCtx);
    if (fServerInitialized)
        RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerDestroy(&HttpSrv));
    if (fProviderInitialized)
        tstHttpProviderTerm(&ProviderCtx);
    if (RTFileExists(szDstFile))
        RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szDstFile));
    RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szSrcFile));
}

/**
 * Run a manual (i.e. interacive) test.
 *
 * This will keep the HTTP server running, so that the file(s) can be downloaded manually.
 */
static void tstManual(RTTEST hTest, PSHCLTRANSFERCTX pTransferCtx, PSHCLHTTPSERVER pHttpSrv)
{
    char *pszUrls = NULL;

    uint32_t const cTx = ShClTransferCtxGetTotalTransfers(pTransferCtx);
    if (!cTx)
    {
        RTTestFailed(hTest, "Must specify at least one file to serve!\n");
        return;
    }

    for (uint32_t i = 0; i < cTx; i++)
    {
        PSHCLTRANSFER pTx = ShClTransferCtxGetTransferByIndex(pTransferCtx, i);

        uint16_t const uId    = ShClTransferGetID(pTx);
        char          *pszUrl = ShClTransferHttpServerGetUrlA(pHttpSrv, uId, 0 /* Entry index */);
        RTTEST_CHECK(hTest, pszUrl != NULL);
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "URL #%02RU32: %s\n", i, pszUrl);
        int rcAppend = VINF_SUCCESS;
        if (i > 0)
        {
            rcAppend = RTStrAAppend(&pszUrls, "\n");
            RTTEST_CHECK_RC(hTest, rcAppend, VINF_SUCCESS);
        }
        if (RT_SUCCESS(rcAppend))
        {
            rcAppend = RTStrAAppend(&pszUrls, pszUrl);
            RTTEST_CHECK_RC(hTest, rcAppend, VINF_SUCCESS);
        }
        RTStrFree(pszUrl);
        if (RT_FAILURE(rcAppend))
            break;
    }

#ifdef TESTCASE_WITH_X11
    SHCLX11CTX      X11Ctx;
    SHCLEVENTSOURCE EventSource;

    if (g_fX11)
    {
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Copied URLs to X11 clipboard\n");

        SHCLCALLBACKS Callbacks;
        RT_ZERO(Callbacks);
        RTTEST_CHECK_RC_OK(hTest, ShClX11Init(&X11Ctx, &Callbacks, NULL /* pParent */));
        RTTEST_CHECK_RC_OK(hTest, ShClX11ThreadStart(&X11Ctx, false /* fGrab */));
        RTTEST_CHECK_RC_OK(hTest, ShClEventSourceInit(&EventSource, 0));
        RTTEST_CHECK_RC_OK(hTest, ShClX11WriteDataToX11(&X11Ctx, &EventSource, RT_MS_30SEC,
                                                        VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_URI_LIST,
                                                        pszUrls, RTStrNLen(pszUrls, RTSTR_MAX), NULL /* pcbWritten */));
    }
#endif

    RTThreadSleep(g_msRuntime);

#ifdef TESTCASE_WITH_X11
    if (g_fX11)
    {
        RTTEST_CHECK_RC_OK(hTest, ShClEventSourceTerm(&EventSource));
        ShClX11ThreadStop(&X11Ctx);
        ShClX11Term(&X11Ctx);
    }
#endif

    RTStrFree(pszUrls);
}

static RTEXITCODE tstUsage(PRTSTREAM pStrm)
{
    RTStrmPrintf(pStrm, "Tests for the clipboard HTTP server.\n\n");

    RTStrmPrintf(pStrm, "Usage: %s [options] [file 1] ... [file N]\n", RTProcShortName());
    RTStrmPrintf(pStrm,
                 "\n"
                 "Options:\n"
                 "  -h, -?, --help\n"
                 "    Displays help.\n"
                 "  -m, --manual\n"
                 "    Enables manual (i.e. interactive) testing the HTTP server.\n"
                 "  -p, --port\n"
                 "    Sets the HTTP server port.\n"
                 "  -v, --verbose\n"
                 "    Increases verbosity.\n"
#ifdef TESTCASE_WITH_X11
                 "  -X, --x11\n"
                 "    Copies the HTTP URLs to the X11 clipboard(s). Implies manual testing.\n"
#endif
                 );

    return RTEXITCODE_SUCCESS;
}

int main(int argc, char *argv[])
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardHttpServer", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Process options.
     */
    static const RTGETOPTDEF aOpts[] =
    {
        { "--help",             'h',               RTGETOPT_REQ_NOTHING },
        { "--manual",           'm',               RTGETOPT_REQ_NOTHING },
        { "--max-time",         't',               RTGETOPT_REQ_UINT32 },
        { "--port",             'p',               RTGETOPT_REQ_UINT16 },
        { "--verbose",          'v',               RTGETOPT_REQ_NOTHING },
        { "--x11",              'X',               RTGETOPT_REQ_NOTHING }
    };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, argc, argv, aOpts, RT_ELEMENTS(aOpts), 1 /*idxFirst*/, 0 /*fFlags - must not sort! */);
    AssertRCReturn(rc, RTEXITCODE_INIT);

    uint16_t     uPort = 0;

    int           ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'h':
                return tstUsage(g_pStdErr);

            case 'p':
                uPort = ValueUnion.u16;
                break;

            case 't':
                g_msRuntime = ValueUnion.u32 * RT_MS_1SEC; /* Convert s to ms. */
                break;

            case 'm':
                g_fManual = true;
                break;

            case 'v':
                g_uVerbosity++;
                break;

#ifdef TESTCASE_WITH_X11
            case 'X':
                g_fX11 = true;
                break;
#endif
            case VINF_GETOPT_NOT_OPTION:
                continue;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    RTTestBanner(hTest);

#ifdef TESTCASE_WITH_X11
    /* Enable manual mode if X11 was selected. Pure convenience. */
    if (g_fX11 && !g_fManual)
        g_fManual = true;
#endif

    /*
     * Configure release logging to go to stdout.
     */
    RTUINT fFlags = RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fFlags |= RTLOGFLAGS_USECRLF;
#endif
    static const char * const s_apszLogGroups[] = VBOX_LOGGROUP_NAMES;
    rc = RTLogCreate(&g_pRelLogger, fFlags, "all.e.l", "TST_CLIPBOARD_HTTPSERVER_RELEASE_LOG",
                     RT_ELEMENTS(s_apszLogGroups), s_apszLogGroups, RTLOGDEST_STDOUT, NULL /*"vkat-release.log"*/);
    if (RT_SUCCESS(rc))
    {
        RTLogSetDefaultInstance(g_pRelLogger);
        if (g_uVerbosity)
        {
            RTMsgInfo("Setting verbosity logging to level %u\n", g_uVerbosity);
            switch (g_uVerbosity) /* Not very elegant, but has to do it for now. */
            {
                case 1:
                    rc = RTLogGroupSettings(g_pRelLogger, "shared_clipboard.e.l+http.e.l");
                    break;

                case 2:
                    rc = RTLogGroupSettings(g_pRelLogger, "shared_clipboard.e.l.l2+http.e.l.l2");
                    break;

                case 3:
                    rc = RTLogGroupSettings(g_pRelLogger, "shared_clipboard.e.l.l2.l3+http.e.l.l2.l3");
                    break;

                case 4:
                    RT_FALL_THROUGH();
                default:
                    rc = RTLogGroupSettings(g_pRelLogger, "shared_clipboard.e.l.l2.l3.l4.f+http.e.l.l2.l3.l4.f");
                    break;
            }
            if (RT_FAILURE(rc))
                RTMsgError("Setting debug logging failed, rc=%Rrc\n", rc);
        }
    }
    else
        RTMsgWarning("Failed to create release logger: %Rrc", rc);

    /*
     * Create HTTP server.
     */
    SHCLHTTPSERVER HttpSrv;
    ShClTransferHttpServerInit(&HttpSrv);
    ShClTransferHttpServerStop(&HttpSrv); /* Try to stop a non-running server twice. */
    ShClTransferHttpServerStop(&HttpSrv);
    RTTEST_CHECK(hTest, ShClTransferHttpServerIsRunning(&HttpSrv) == false);
    if (uPort)
        rc = ShClTransferHttpServerStartEx(&HttpSrv, uPort);
    else
        rc = ShClTransferHttpServerStart(&HttpSrv, 32 /* cMaxAttempts */, &uPort);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransfer(&HttpSrv, 0) == false);
    RTTEST_CHECK(hTest, ShClTransferHttpServerGetTransfer(&HttpSrv, 42) == false);

    char *pszSrvAddr = ShClTransferHttpServerGetAddressA(&HttpSrv);
    RTTEST_CHECK(hTest, pszSrvAddr != NULL);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "HTTP server running: %s (for %RU32ms) ...\n", pszSrvAddr, g_msRuntime);
    RTStrFree(pszSrvAddr);
    pszSrvAddr = NULL;

    SHCLTRANSFERCTX TxCtx;
    RTTEST_CHECK_RC_OK(hTest, ShClTransferCtxInit(&TxCtx));
    RTTEST_CHECK_RC_OK(hTest, ShClTransferCtxBeginSession(&TxCtx, 1));

    /* Query the local transfer provider. */
    SHCLTXPROVIDER Provider;
    RT_ZERO(Provider);
    RTTESTI_CHECK(ShClTransferProviderLocalQueryInterface(&Provider) != NULL);

    /* Parse options again, but this time we only fetch all files we want to serve.
     * Only can be done after we initialized the HTTP server above. */
    RT_ZERO(GetState);
    rc = RTGetOptInit(&GetState, argc, argv, aOpts, RT_ELEMENTS(aOpts), 1 /*idxFirst*/, 0 /*fFlags - must not sort! */);
    AssertRCReturn(rc, RTEXITCODE_INIT);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case VINF_GETOPT_NOT_OPTION:
            {
                RTTEST_CHECK_RC_OK(hTest, tstCreateTransferSingle(hTest, &TxCtx, &HttpSrv, ValueUnion.psz, &Provider));
                break;
            }

            default:
                continue;
        }
    }

    char szTempDir[RTPATH_MAX];
    RTTEST_CHECK_RC_OK(hTest, RTPathTemp(szTempDir, sizeof(szTempDir)));
    RTTEST_CHECK_RC_OK(hTest, RTPathAppend(szTempDir, sizeof(szTempDir), "tstClipboardHttpServer-XXXXXX"));
    RTTEST_CHECK_RC_OK(hTest, RTDirCreateTemp(szTempDir, 0700));

    if (!g_fManual)
    {
        tstRepeatedRequestHandles(hTest, szTempDir);
        tstStaleKeyUnregister(hTest, szTempDir);
        tstUnregisterDuringRequest(hTest, szTempDir);
    }

    tstDuplicateTransferRegistration(hTest, &TxCtx, &HttpSrv, szTempDir, &Provider);

    if (!g_fManual)
    {
        char szPath[RTPATH_MAX];
        for (size_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
        {
            RTTEST_CHECK      (hTest, RTStrPrintf(szPath, sizeof(szPath),  szTempDir));
            RTTEST_CHECK_RC_OK(hTest, RTPathAppend(szPath, sizeof(szPath), g_aTests[i].pszPath));

            size_t cbSize =  g_aTests[i].cbSize == UINT64_MAX ? RTRandU32Ex(0, _256M) : g_aTests[i].cbSize;

            switch (g_aTests[i].fMode & RTFS_TYPE_MASK)
            {
                case RTFS_TYPE_FILE:
                {
                    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Random test file (%zu bytes): %s\n", cbSize, szPath);

                    RTFILE hFile;
                    rc = RTFileOpen(&hFile, szPath, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE);
                    if (RT_SUCCESS(rc))
                    {
                        uint8_t abBuf[_64K]; RTRandBytes(abBuf, sizeof(abBuf));

                        while (cbSize > 0)
                        {
                            size_t cbToWrite = sizeof(abBuf);
                            if (cbToWrite > cbSize)
                                cbToWrite = cbSize;
                            rc = RTFileWrite(hFile, abBuf, cbToWrite, NULL);
                            if (RT_FAILURE(rc))
                            {
                                RTTestIFailed("RTFileWrite(%#x) -> %Rrc\n", cbToWrite, rc);
                                break;
                            }
                            cbSize -= cbToWrite;
                        }

                        RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);
                    }
                    else
                        RTTestIFailed("RTFileOpen(%s) -> %Rrc\n", szPath, rc);
                    break;
                }

                case RTFS_TYPE_DIRECTORY:
                    RTTESTI_CHECK_RC_OK(RTDirCreate(szPath, 0755 /* fMode */, 0 /* fCreate */));
                    break;

                case RTFS_TYPE_SYMLINK:
                    RTTESTI_CHECK_RC_OK(RTSymlinkCreate(szPath, szTempDir, RTSYMLINKTYPE_UNKNOWN, 0 /* fCreate */));
                    break;

                default:
                    break;
            }

            if (RT_SUCCESS(rc))
                RTTEST_CHECK_RC_OK(hTest, tstCreateTransferSingle(hTest, &TxCtx, &HttpSrv, szPath, &Provider));
        }
    }

    /* Don't bail out here to prevent cleaning up after ourselves on failure. */
    if (RTTestErrorCount(hTest) == 0)
    {
        if (g_fManual)
        {
            tstManual(hTest, &TxCtx, &HttpSrv);
        }
        else /* Download all files to a temp file using our HTTP client. */
        {
            RTHTTP hClient;
            rc = RTHttpCreate(&hClient);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Set it to not use any proxies for our testcase as it is not necessary and
                 * it will cause memory leaks on Linux where libproxy is used making the testcase fail
                 * for ASAN enabled builds.
                 */
                RTTEST_CHECK_RC_OK(hTest, RTHttpSetProxy(hClient, NULL /*pszProxyUrl*/, 0 /*uPort*/,
                                                         NULL /*pszProxyUser*/, NULL /*pszProxyPwd*/));

                char szExpectedUrl[RTPATH_MAX];
                if (ShClTransferCtxGetTotalTransfers(&TxCtx) > 0)
                {
                    PSHCLTRANSFER pTx = ShClTransferCtxGetTransferByIndex(&TxCtx, 0);
                    char *pszUrlBase = ShClTransferHttpServerGetUrlA(&HttpSrv, ShClTransferGetID(pTx), UINT64_MAX);
                    if (pszUrlBase)
                    {
                        tstMalformedPaths(hTest, hClient, pszUrlBase);
                        RTStrFree(pszUrlBase);
                    }
                }
                for (size_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
                {
                    PSHCLTRANSFER pTx = ShClTransferCtxGetTransferByIndex(&TxCtx, i);
                    char *pszUrlBase  = ShClTransferHttpServerGetUrlA(&HttpSrv, ShClTransferGetID(pTx), UINT64_MAX);
                    char *pszUrl      = ShClTransferHttpServerGetUrlA(&HttpSrv, ShClTransferGetID(pTx), 0 /* idxEntry */);
                    RTTEST_CHECK(hTest, pszUrlBase != NULL);
                    RTTEST_CHECK(hTest, pszUrl != NULL);
                    if (!pszUrlBase || !pszUrl)
                    {
                        RTStrFree(pszUrlBase);
                        RTStrFree(pszUrl);
                        continue;
                    }
                    RTTEST_CHECK(hTest, RTStrPrintf2(szExpectedUrl, sizeof(szExpectedUrl), "%s/%s",
                                                    pszUrlBase, g_aTests[i].pszUrl));
                    RTTEST_CHECK_MSG(hTest, RTStrCmp(pszUrl, szExpectedUrl) == 0,
                                     (hTest, "Expected URL '%s', got '%s'\n", szExpectedUrl, pszUrl));
                    RTStrFree(pszUrlBase);

                    switch (g_aTests[i].fMode & RTFS_TYPE_MASK)
                    {
                        case RTFS_TYPE_FILE:
                        {
                            /* Download to destination file. */
                            char szDstFile[RTPATH_MAX];
                            RTTEST_CHECK_RC_OK(hTest, RTPathTemp(szDstFile, sizeof(szDstFile)));
                            RTTEST_CHECK_RC_OK(hTest, RTPathAppend(szDstFile, sizeof(szDstFile), "tstClipboardHttpServer-XXXXXX"));
                            RTTEST_CHECK_RC_OK(hTest, RTFileCreateTemp(szDstFile, 0600));
                            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Downloading file '%s' -> '%s'\n", pszUrl, szDstFile);
                            RTTEST_CHECK_RC_OK(hTest, RTHttpGetFile(hClient, pszUrl, szDstFile));

                            /* Compare files. */
                            char szSrcFile[RTPATH_MAX];
                            RTTEST_CHECK      (hTest, RTStrPrintf(szSrcFile, sizeof(szSrcFile),  szTempDir));
                            RTTEST_CHECK_RC_OK(hTest, RTPathAppend(szSrcFile, sizeof(szSrcFile), g_aTests[i].pszPath));
                            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Comparing files '%s' vs. '%s'\n", szSrcFile, szDstFile);
                            RTTEST_CHECK_RC_OK(hTest, RTFileCompare(szSrcFile, szDstFile));

                            RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szDstFile));
                            break;
                        }

                        case RTFS_TYPE_DIRECTORY:
                            RT_FALL_THROUGH();
                        case RTFS_TYPE_SYMLINK:
                        {
                            char szDstFile[RTPATH_MAX];
                            RTTEST_CHECK_RC_OK(hTest, RTPathTemp(szDstFile, sizeof(szDstFile)));
                            RTTEST_CHECK_RC_OK(hTest, RTPathAppend(szDstFile, sizeof(szDstFile), "tstClipboardHttpServer-XXXXXX"));
                            RTTEST_CHECK_RC_OK(hTest, RTFileCreateTemp(szDstFile, 0600));
                            RTTEST_CHECK_RC   (hTest, RTHttpGetFile(hClient, pszUrl, szDstFile), g_aTests[i].rc);
                            RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szDstFile));
                            break;
                        }

                        default:
                            break;
                    }
                    RTStrFree(pszUrl);
                }

                RTTEST_CHECK_RC_OK(hTest, RTHttpDestroy(hClient));
            }

            /* This is supposed to run unattended, so shutdown automatically. */
            ASMAtomicXchgBool(&g_fShutdown, true); /* Set shutdown indicator. */
        }
    }

    RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerDestroy(&HttpSrv));
    ShClTransferCtxDestroy(&TxCtx);

    /*
     * Cleanup
     */
    if (!g_fManual)
    {
        char szPath[RTPATH_MAX];
        for (size_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
        {
            RTTEST_CHECK      (hTest, RTStrPrintf(szPath, sizeof(szPath), szTempDir));
            RTTEST_CHECK_RC_OK(hTest, RTPathAppend(szPath, sizeof(szPath), g_aTests[i].pszPath));

            switch (g_aTests[i].fMode & RTFS_TYPE_MASK)
            {
                case RTFS_TYPE_FILE:
                {
                    RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szPath));
                    break;
                }

                case RTFS_TYPE_DIRECTORY:
                {
                    RTTEST_CHECK_RC_OK(hTest, RTDirRemove(szPath)); /* ASSUMES empty dir. */
                    break;
                }

                case RTFS_TYPE_SYMLINK:
                {
                    RTTEST_CHECK_RC_OK(hTest, RTSymlinkDelete(szPath, 0 /* fDelete */));
                    break;
                }

                default:
                    break;
            }
        }
    }
    RTTEST_CHECK_RC_OK(hTest, RTDirRemove(szTempDir));

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}
