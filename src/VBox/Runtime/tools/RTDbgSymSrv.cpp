/* $Id: RTDbgSymSrv.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Debug Symbol Server.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/err.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/getopt.h>
#include <iprt/http.h>
#include <iprt/http-server.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/thread.h>
#include <iprt/pipe.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) dbgSymSrvOpen(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void **ppvHandle);
static DECLCALLBACK(int) dbgSymSrvRead(PRTHTTPCALLBACKDATA pData, void *pvHandle, void *pvBuf, size_t cbBuf, size_t *pcbRead);
static DECLCALLBACK(int) dbgSymSrvClose(PRTHTTPCALLBACKDATA pData, void *pvHandle);
static DECLCALLBACK(int) dbgSymSrvQueryInfo(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, PRTFSOBJINFO pObjInfo, char **ppszMIMEHint);
static DECLCALLBACK(int) dbgSymSrvDestroy(PRTHTTPCALLBACKDATA pData);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Flag whether the server was interrupted. */
static bool g_fCanceled = false;
/** The symbol cache absolute root. */
static const char *g_pszSymCacheRoot = NULL;
/** The path to the pdb.exe. */
static const char *g_pszPdbExe = NULL;
/** Symbol server to forward requests to if not found locally. */
static const char *g_pszSymSrvFwd = NULL;
#ifndef RT_OS_WINDOWS
/** The WINEPREFIX to use. */
static const char *g_pszWinePrefix = NULL;
/** The path to the wine binary to use for pdb.exe. */
static const char *g_pszWinePath = NULL;
#endif
/** Verbositity level. */
//static uint32_t g_iLogLevel = 99;
/** Server callbacks. */
static RTHTTPSERVERCALLBACKS g_SrvCallbacks =
{
    dbgSymSrvOpen,
    dbgSymSrvRead,
    dbgSymSrvClose,
    dbgSymSrvQueryInfo,
    NULL,
    NULL,
    dbgSymSrvDestroy
};


/**
 * Resolves (and validates) a given URL to an absolute (local) path.
 *
 * @returns VBox status code.
 * @param   pszUrl              URL to resolve.
 * @param   ppszPathAbs         Where to store the resolved absolute path on success.
 *                              Needs to be free'd with RTStrFree().
 * @param   ppszPathAbsXml      Where to store the resolved absolute path for the converted XML
 *                              file. Needs to be free'd with RTStrFree().
 */
static int rtDbgSymSrvPathResolve(const char *pszUrl, char **ppszPathAbs, char **ppszPathAbsXml)
{
    /* The URL needs to start with /download/symbols/. */
    if (strncmp(pszUrl, "/download/symbols/", sizeof("/download/symbols/") - 1))
        return VERR_NOT_FOUND;

    pszUrl += sizeof("/download/symbols/") - 1;
    /* Construct absolute path. */
    char *pszPathAbs = NULL;
    if (RTStrAPrintf(&pszPathAbs, "%s/%s", g_pszSymCacheRoot, pszUrl) <= 0)
        return VERR_NO_MEMORY;

    if (ppszPathAbsXml)
    {
        char *pszPathAbsXml = NULL;
        if (RTStrAPrintf(&pszPathAbsXml, "%s/%s.xml", g_pszSymCacheRoot, pszUrl) <= 0)
            return VERR_NO_MEMORY;

        *ppszPathAbsXml = pszPathAbsXml;
    }

    *ppszPathAbs = pszPathAbs;

    return VINF_SUCCESS;
}


static int rtDbgSymSrvFwdDownload(const char *pszUrl, char *pszPathAbs)
{
    RTPrintf("'%s' not in local cache, fetching from '%s'\n", pszPathAbs, g_pszSymSrvFwd);

    char *pszFilename = RTPathFilename(pszPathAbs);
    char chStart = *pszFilename;
    *pszFilename = '\0';
    int rc = RTDirCreateFullPath(pszPathAbs, 0766);
    if (!RTDirExists(pszPathAbs))
    {
        Log(("Error creating cache dir '%s': %Rrc\n", pszPathAbs, rc));
        return rc;
    }
    *pszFilename = chStart;

    char szUrl[_2K];
    RTHTTP hHttp;
    rc = RTHttpCreate(&hHttp);
    if (RT_SUCCESS(rc))
    {
        RTHttpUseSystemProxySettings(hHttp);
        RTHttpSetFollowRedirects(hHttp, 8);

        static const char * const s_apszHeaders[] =
        {
            "User-Agent: Microsoft-Symbol-Server/6.6.0999.9",
            "Pragma: no-cache",
        };

        rc = RTHttpSetHeaders(hHttp, RT_ELEMENTS(s_apszHeaders), s_apszHeaders);
        if (RT_SUCCESS(rc))
        {
            RTStrPrintf(szUrl, sizeof(szUrl), "%s/%s", g_pszSymSrvFwd, pszUrl + sizeof("/download/symbols/") - 1);

            /** @todo Use some temporary file name and rename it after the operation
             *        since not all systems support read-deny file sharing
             *        settings. */
            RTPrintf("Downloading '%s' to '%s'...\n", szUrl, pszPathAbs);
            rc = RTHttpGetFile(hHttp, szUrl, pszPathAbs);
            if (RT_FAILURE(rc))
            {
                RTFileDelete(pszPathAbs);
                RTPrintf("%Rrc on URL '%s'\n", rc, szUrl);
            }
            if (rc == VERR_HTTP_NOT_FOUND)
            {
                /* Try the compressed version of the file. */
                pszPathAbs[strlen(pszPathAbs) - 1] = '_';
                szUrl[strlen(szUrl)     - 1] = '_';
                RTPrintf("Downloading '%s' to '%s'...\n", szUrl, pszPathAbs);
                rc = RTHttpGetFile(hHttp, szUrl, pszPathAbs);
#if 0 /** @todo */
                if (RT_SUCCESS(rc))
                    rc = rtDbgCfgUnpackMsCacheFile(pThis, pszPathAbs, pszFilename);
                else
#endif
                {
                    RTPrintf("%Rrc on URL '%s'\n", rc, pszPathAbs);
                    RTFileDelete(pszPathAbs);
                }
            }
        }

        RTHttpDestroy(hHttp);
    }

    return rc;
}


static int rtDbgSymSrvConvertToGhidraXml(char *pszPath, const char *pszFilename)
{
    RTPrintf("Converting '%s' to ghidra XML into '%s'\n", pszPath, pszFilename);

    /*
     * Figuring out the argument list for the platform specific way to call pdb.exe.
     */
#ifdef RT_OS_WINDOWS
    RTENV hEnv = RTENV_DEFAULT;
    RTPathChangeToDosSlashes(pszPath, false /*fForce*/);
    const char *papszArgs[] =
    {
        g_pszPdbExe,
        pszPath,
        NULL
    };

#else
    const char *papszArgs[] =
    {
        g_pszWinePath,
        g_pszPdbExe,
        pszPath,
        NULL
    };

    RTENV hEnv;
    {
        int rc = RTEnvCreate(&hEnv);
        if (RT_SUCCESS(rc))
        {
            rc = RTEnvSetEx(hEnv, "WINEPREFIX", g_pszWinePrefix);
            if (RT_SUCCESS(rc))
                rc = RTEnvSetEx(hEnv, "WINEDEBUG", "-all");
            if (RT_FAILURE(rc))
            {
                RTEnvDestroy(hEnv);
                return rc;
            }
        }
    }
#endif

    RTPIPE hPipeR, hPipeW;
    int rc = RTPipeCreate(&hPipeR, &hPipeW, RTPIPE_C_INHERIT_WRITE);
    if (RT_SUCCESS(rc))
    {
        RTHANDLE Handle;
        Handle.enmType = RTHANDLETYPE_PIPE;
        Handle.u.hPipe = hPipeW;

        /*
         * Do the conversion.
         */
        RTPROCESS hChild;
        RTFILE hFile;

        rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE); AssertRC(rc);

        rc = RTProcCreateEx(papszArgs[0], papszArgs, hEnv,
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
                              RTPROC_FLAGS_NO_WINDOW | RTPROC_FLAGS_HIDDEN | RTPROC_FLAGS_SEARCH_PATH,
#else
                              RTPROC_FLAGS_SEARCH_PATH,
#endif
                              NULL /*phStdIn*/, &Handle, NULL /*phStdErr*/,
                              NULL /*pszAsUser*/, NULL /*pszPassword*/, NULL /*pvExtraData*/,
                              &hChild);
        if (RT_SUCCESS(rc))
        {
            rc = RTPipeClose(hPipeW); AssertRC(rc);

            for (;;)
            {
                char szOutput[_4K];
                size_t cbRead;
                rc = RTPipeReadBlocking(hPipeR, &szOutput[0], sizeof(szOutput), &cbRead);
                if (RT_FAILURE(rc))
                {
                    Assert(rc == VERR_BROKEN_PIPE);
                    break;
                }

                rc = RTFileWrite(hFile, &szOutput[0], cbRead, NULL /*pcbWritten*/); AssertRC(rc);
            }
            rc = RTPipeClose(hPipeR); AssertRC(rc);

            RTPROCSTATUS ProcStatus;
            rc = RTProcWait(hChild, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus);
            if (RT_SUCCESS(rc))
            {
                if (   ProcStatus.enmReason == RTPROCEXITREASON_NORMAL
                    && ProcStatus.iStatus   == 0)
                {
                    if (RTPathExists(pszPath))
                    {
                        RTPrintf("Successfully unpacked '%s' to '%s'.\n", pszPath, pszFilename);
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        RTPrintf("Successfully ran unpacker on '%s', but '%s' is missing!\n", pszPath, pszFilename);
                        rc = VERR_FILE_NOT_FOUND;
                    }
                }
                else
                {
                    RTPrintf("Unpacking '%s' failed: iStatus=%d enmReason=%d\n",
                             pszPath, ProcStatus.iStatus, ProcStatus.enmReason);
                    rc = VERR_ZIP_CORRUPTED;
                }
            }
            else
                RTPrintf("Error waiting for process: %Rrc\n", rc);

            RTFileClose(hFile);

        }
        else
            RTPrintf("Error starting unpack process '%s': %Rrc\n", papszArgs[0], rc);
    }

#ifndef RT_OS_WINDOWS
    RTEnvDestroy(hEnv);
#endif
    return rc;
}


static DECLCALLBACK(int) dbgSymSrvOpen(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void **ppvHandle)
{
    RT_NOREF(pData);

    char *pszPathAbs = NULL;
    char *pszPathAbsXml = NULL;
    int rc = rtDbgSymSrvPathResolve(pReq->pszUrl, &pszPathAbs, &pszPathAbsXml);
    if (RT_SUCCESS(rc))
    {
        RTFILE hFile;
        if (   g_pszPdbExe
            && RTPathExists(pszPathAbsXml))
        {
            RTPrintf("Opening '%s'\n", pszPathAbsXml);
            rc = RTFileOpen(&hFile, pszPathAbsXml, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        }
        else
        {
            RTPrintf("Opening '%s'\n", pszPathAbs);
            rc = RTFileOpen(&hFile, pszPathAbs, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        }
        if (RT_SUCCESS(rc))
             *ppvHandle = hFile;

        RTStrFree(pszPathAbs);
        RTStrFree(pszPathAbsXml);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


static DECLCALLBACK(int) dbgSymSrvRead(PRTHTTPCALLBACKDATA pData, void *pvHandle, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    RT_NOREF(pData);
    return RTFileRead((RTFILE)pvHandle, pvBuf, cbBuf, pcbRead);
}


static DECLCALLBACK(int) dbgSymSrvClose(PRTHTTPCALLBACKDATA pData, void *pvHandle)
{
    RT_NOREF(pData);
    return RTFileClose((RTFILE)pvHandle);
}


static DECLCALLBACK(int) dbgSymSrvQueryInfo(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, PRTFSOBJINFO pObjInfo, char **ppszMIMEHint)
{
    RT_NOREF(pData, ppszMIMEHint);
    char *pszPathAbs = NULL;
    char *pszPathAbsXml = NULL;
    int rc = rtDbgSymSrvPathResolve(pReq->pszUrl, &pszPathAbs, &pszPathAbsXml);
    if (RT_SUCCESS(rc))
    {
        if (   !RTPathExists(pszPathAbs)
            && g_pszSymSrvFwd)
            rc = rtDbgSymSrvFwdDownload(pReq->pszUrl, pszPathAbs);

        if (   RT_SUCCESS(rc)
            && RTPathExists(pszPathAbs))
        {
            const char *pszFile = pszPathAbs;

            if (g_pszPdbExe)
            {
                if (!RTPathExists(pszPathAbsXml))
                    rc = rtDbgSymSrvConvertToGhidraXml(pszPathAbs, pszPathAbsXml);
                if (RT_SUCCESS(rc))
                    pszFile = pszPathAbsXml;
            }

            if (   RT_SUCCESS(rc)
                && RTPathExists(pszFile))
            {
                rc = RTPathQueryInfo(pszFile, pObjInfo, RTFSOBJATTRADD_NOTHING);
                if (RT_SUCCESS(rc))
                {
                    if (!RTFS_IS_FILE(pObjInfo->Attr.fMode))
                        rc = VERR_NOT_SUPPORTED;
                }
            }
            else
                rc = VERR_FILE_NOT_FOUND;
        }
        else
            rc = VERR_FILE_NOT_FOUND;

        RTStrFree(pszPathAbs);
        RTStrFree(pszPathAbsXml);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


static DECLCALLBACK(int) dbgSymSrvDestroy(PRTHTTPCALLBACKDATA pData)
{
    RTPrintf("%s\n", __FUNCTION__);
    RT_NOREF(pData);
    return VINF_SUCCESS;
}


/**
 * Display the version of the server program.
 *
 * @returns exit code.
 */
static RTEXITCODE rtDbgSymSrvVersion(void)
{
    RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
    return RTEXITCODE_SUCCESS;
}


/**
 * Shows the usage of the cache program.
 *
 * @returns Exit code.
 * @param   pszArg0             Program name.
 */
static RTEXITCODE rtDbgSymSrvUsage(const char *pszArg0)
{
    RTPrintf("Usage: %s --address <interface> --port <port> --sym-cache <symbol cache root> --pdb-exe <ghidra pdb.exe path>\n"
             "\n"
             "Options:\n"
             "  -a, --address\n"
             "      The interface to listen on, default is localhost.\n"
             "  -p, --port\n"
             "      The port to listen on, default is 80.\n"
             "  -c, --sym-cache\n"
             "      The absolute path of the symbol cache.\n"
             "  -x, --pdb-exe\n"
             "      The path of Ghidra's pdb.exe to convert PDB files to XML on the fly.\n"
             "  -f, --sym-srv-forward\n"
             "      The symbol server to forward requests to if a file is not in the local cache\n"
#ifndef RT_OS_WINDOWS
             "  -w, --wine-prefix\n"
             "      The prefix of the wine environment to use which has msdia140.dll set up for pdb.exe.\n"
             "  -b, --wine-bin\n"
             "      The wine binary path to run pdb.exe with.\n"
#endif
             , RTPathFilename(pszArg0));

    return RTEXITCODE_SUCCESS;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse the command line.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--address",                  'a', RTGETOPT_REQ_STRING },
        { "--port",                     'p', RTGETOPT_REQ_UINT16 },
        { "--sym-cache",                'c', RTGETOPT_REQ_STRING },
        { "--pdb-exe",                  'x', RTGETOPT_REQ_STRING },
        { "--sym-srv-forward",          'f', RTGETOPT_REQ_STRING },
#ifndef RT_OS_WINDOWS
        { "--wine-prefix",              'w', RTGETOPT_REQ_STRING },
        { "--wine-bin",                 'b', RTGETOPT_REQ_STRING },
#endif
        { "--help",                     'h', RTGETOPT_REQ_NOTHING },
        { "--version",                  'V', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE State;
    rc = RTGetOptInit(&State, argc, argv, &s_aOptions[0], RT_ELEMENTS(s_aOptions), 1,  RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc", rc);

    const char *pszAddress = "localhost";
    uint16_t uPort = 80;

    RTGETOPTUNION   ValueUnion;
    int             chOpt;
    while ((chOpt = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (chOpt)
        {
            case 'a':
                pszAddress = ValueUnion.psz;
                break;
            case 'p':
                uPort = ValueUnion.u16;
                break;
            case 'c':
                g_pszSymCacheRoot = ValueUnion.psz;
                break;
            case 'x':
                g_pszPdbExe = ValueUnion.psz;
                break;
            case 'f':
                g_pszSymSrvFwd = ValueUnion.psz;
                break;
#ifndef RT_OS_WINDOWS
            case 'w':
                g_pszWinePrefix = ValueUnion.psz;
                break;
            case 'b':
                g_pszWinePath = ValueUnion.psz;
                break;
#endif

            case 'h':
                return rtDbgSymSrvUsage(argv[0]);
            case 'V':
                return rtDbgSymSrvVersion();
            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

    if (!g_pszSymCacheRoot)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The symbol cache root needs to be set");

    RTHTTPSERVER hHttpSrv;
    rc = RTHttpServerCreate(&hHttpSrv, pszAddress, uPort, &g_SrvCallbacks,
                            NULL, 0);
    if (RT_SUCCESS(rc))
    {
        RTPrintf("Starting HTTP server at %s:%RU16 ...\n", pszAddress, uPort);
        RTPrintf("Root directory is '%s'\n", g_pszSymCacheRoot);

        RTPrintf("Running HTTP server ...\n");

        for (;;)
        {
            RTThreadSleep(1000);

            if (g_fCanceled)
                break;
        }

        RTPrintf("Stopping HTTP server ...\n");

        int rc2 = RTHttpServerDestroy(hHttpSrv);
        if (RT_SUCCESS(rc))
            rc = rc2;

        RTPrintf("Stopped HTTP server\n");
    }
    else
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTHttpServerCreate failed: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}

