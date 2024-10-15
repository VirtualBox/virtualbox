/* $Id: tstClipboardHttpServer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard HTTP server test case.
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

#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/http.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>

#ifdef TESTCASE_WITH_X11
#include <VBox/GuestHost/SharedClipboard-x11.h>
#endif

#include <VBox/GuestHost/SharedClipboard-transfers.h>


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
    /** URL to use for downloading the file via RTHttp APIs. Has to be fully escaped. */
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
    RTFS_TYPE_FILE, "file1.txt",                          "file1.txt",                  _64K,       VINF_SUCCESS,
    /* Note: For RTHttpGetFile() the URL needs to be percent-encoded. */
    RTFS_TYPE_FILE, "file2 with spaces.txt",              "file2%20with%20spaces.txt",  _64K,       VINF_SUCCESS,
    RTFS_TYPE_FILE, "bigfile.bin",                        "bigfile.bin",                _512M,      VINF_SUCCESS,
    RTFS_TYPE_FILE, "zerobytes",                          "zerobytes",                  0,          VINF_SUCCESS,
    RTFS_TYPE_FILE, "file\\with\\slashes",                "file%5Cwith%5Cslashes",      42,         VINF_SUCCESS,
    /* Korean encoding. */
    RTFS_TYPE_FILE, "VirtualBox가 크게 성공했습니다!",         "VirtualBox%EA%B0%80%20%ED%81%AC%EA%B2%8C%20%EC%84%B1%EA%B3%B5%ED%96%88%EC%8A%B5%EB%8B%88%EB%8B%A4%21", 42, VINF_SUCCESS,
    /*
     * Other stuff (not supported).
     */
    RTFS_TYPE_DIRECTORY, "test-directory",                "test-directory",             0,          VERR_HTTP_NOT_SUPPORTED,
    RTFS_TYPE_SYMLINK,   "test-symlink",                  "test-symlink",               0,          VERR_HTTP_NOT_SUPPORTED
};


static void tstCreateTransferSingle(RTTEST hTest, PSHCLTRANSFERCTX pTransferCtx, PSHCLHTTPSERVER pSrv,
                                    const char *pszPath, PSHCLTXPROVIDER pProvider)
{
    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "tstCreateTransferSingle: pszPath=%s\n", pszPath);

    PSHCLTRANSFER pTx;
    RTTEST_CHECK_RC_OK(hTest, ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL, NULL /* Callbacks */, &pTx));
    RTTEST_CHECK_RC_OK(hTest, ShClTransferSetProvider(pTx, pProvider));
    RTTEST_CHECK_RC_OK(hTest, ShClTransferRootsSetFromPath(pTx, pszPath));
    RTTEST_CHECK_RC_OK(hTest, ShClTransferInit(pTx));
    RTTEST_CHECK_RC_OK(hTest, ShClTransferCtxRegister(pTransferCtx, pTx, NULL));
    RTTEST_CHECK_RC_OK(hTest, ShClTransferHttpServerRegisterTransfer(pSrv, pTx));
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
        RTStrAPrintf(&pszUrls, "%s", pszUrl);
        if (i > 0)
            RTStrAPrintf(&pszUrls, "\n");
        RTStrFree(pszUrl);
    }

#ifdef TESTCASE_WITH_X11
    SHCLX11CTX      X11Ctx;
    SHCLEVENTSOURCE EventSource;

    if (g_fX11)
    {
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Copied URLs to X11 clipboard\n");

        SHCLCALLBACKS Callbacks;
        RT_ZERO(Callbacks);
        RTTEST_CHECK_RC_OK(hTest, ShClX11Init(&X11Ctx, &Callbacks, NULL /* pParent */, false /* fHeadless */));
        RTTEST_CHECK_RC_OK(hTest, ShClX11ThreadStart(&X11Ctx, false /* fGrab */));
        RTTEST_CHECK_RC_OK(hTest, ShClEventSourceCreate(&EventSource, 0));
        RTTEST_CHECK_RC_OK(hTest, ShClX11WriteDataToX11(&X11Ctx, &EventSource, RT_MS_30SEC,
                                                        VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_URI_LIST,
                                                        pszUrls, RTStrNLen(pszUrls, RTSTR_MAX), NULL /* pcbWritten */));
    }
#endif

    RTThreadSleep(g_msRuntime);

#ifdef TESTCASE_WITH_X11
    if (g_fX11)
    {
        RTTEST_CHECK_RC_OK(hTest, ShClEventSourceDestroy(&EventSource));
        ShClX11ThreadStop(&X11Ctx);
        ShClX11Destroy(&X11Ctx);
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

    /* Query the local transfer provider. */
    SHCLTXPROVIDER Provider;
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
                tstCreateTransferSingle(hTest, &TxCtx, &HttpSrv, ValueUnion.psz, &Provider);
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
                tstCreateTransferSingle(hTest, &TxCtx, &HttpSrv, szPath, &Provider);
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

                char szURL[RTPATH_MAX];
                for (size_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
                {
                    PSHCLTRANSFER pTx = ShClTransferCtxGetTransferByIndex(&TxCtx, i);
                    char *pszUrlBase  = ShClTransferHttpServerGetUrlA(&HttpSrv, ShClTransferGetID(pTx), UINT64_MAX);
                    RTTEST_CHECK(hTest, RTStrPrintf2(szURL, sizeof(szURL), "%s/%s", pszUrlBase, g_aTests[i].pszUrl));
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
                            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Downloading file '%s' -> '%s'\n", szURL, szDstFile);
                            RTTEST_CHECK_RC_OK(hTest, RTHttpGetFile(hClient, szURL, szDstFile));

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
                            RTTEST_CHECK_RC   (hTest, RTHttpGetFile(hClient, szURL, szDstFile), g_aTests[i].rc);
                            RTTEST_CHECK_RC_OK(hTest, RTFileDelete(szDstFile));
                            break;
                        }

                        default:
                            break;
                    }
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
    RTTEST_CHECK_RC_OK(hTest, RTDirRemove(szTempDir));

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}
