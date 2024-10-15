/* $Id: RTHttpServer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Utility for running a (simple) HTTP server.
 *
 * Use this setup to best see what's going on:
 *    VBOX_LOG=rt_http=~0
 *    VBOX_LOG_DEST="nofile stderr"
 *    VBOX_LOG_FLAGS="unbuffered enabled thread msprog"
 *
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include <signal.h>

#include <iprt/http.h>
#include <iprt/http-server.h>

#include <iprt/net.h> /* To make use of IPv4Addr in RTGETOPTUNION. */

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#define LOG_GROUP RTLOGGROUP_HTTP
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/vfs.h>

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif


/*********************************************************************************************************************************
*   Definitations                                                                                                                *
*********************************************************************************************************************************/
typedef struct HTTPSERVERDATA
{
    /** The absolute path of the HTTP server's root directory. */
    char szPathRootAbs[RTPATH_MAX];
    RTFMODE      fMode;
    union
    {
        RTFILE   File;
        RTVFSDIR Dir;
    } h;
    /** Cached response data. */
    RTHTTPSERVERRESP Resp;
} HTTPSERVERDATA;
typedef HTTPSERVERDATA *PHTTPSERVERDATA;

/**
 * Enumeration specifying the VFS handle type of the HTTP server.
 */
typedef enum HTTPSERVERVFSHANDLETYPE
{
    HTTPSERVERVFSHANDLETYPE_INVALID    = 0,
    HTTPSERVERVFSHANDLETYPE_FILE,
    HTTPSERVERVFSHANDLETYPE_DIR,
    /** The usual 32-bit hack. */
    HTTPSERVERVFSHANDLETYPE_32BIT_HACK = 0x7fffffff
} HTTPSERVERVFSHANDLETYPE;

/**
 * Structure for keeping a VFS handle of the HTTP server.
 */
typedef struct HTTPSERVERVFSHANDLE
{
    /** The type of the handle, stored in the union below. */
    HTTPSERVERVFSHANDLETYPE enmType;
    union
    {
        /** The VFS (chain) handle to use for this file. */
        RTVFSFILE hVfsFile;
        /** The VFS (chain) handle to use for this directory. */
        RTVFSDIR  hVfsDir;
    } u;
} HTTPSERVERVFSHANDLE;
typedef HTTPSERVERVFSHANDLE *PHTTPSERVERVFSHANDLE;

/**
 * HTTP directory entry.
 */
typedef struct RTHTTPDIRENTRY
{
    /** The information about the entry. */
    RTFSOBJINFO Info;
    /** Symbolic link target (allocated after the name). */
    const char *pszTarget;
    /** Owner if applicable (allocated after the name). */
    const char *pszOwner;
    /** Group if applicable (allocated after the name). */
    const char *pszGroup;
    /** The length of szName. */
    size_t      cchName;
    /** The entry name. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char        szName[RT_FLEXIBLE_ARRAY];
} RTHTTPDIRENTRY;
/** Pointer to a HTTP directory entry. */
typedef RTHTTPDIRENTRY *PRTHTTPDIRENTRY;
/** Pointer to a HTTP directory entry pointer. */
typedef PRTHTTPDIRENTRY *PPRTHTTPDIRENTRY;

/**
 * Collection of HTTP directory entries.
 * Used for also caching stuff.
 */
typedef struct RTHTTPDIRCOLLECTION
{
    /** Current size of papEntries. */
    size_t                cEntries;
    /** Memory allocated for papEntries. */
    size_t                cEntriesAllocated;
    /** Current entries pending sorting and display. */
    PPRTHTTPDIRENTRY       papEntries;

    /** Total number of bytes allocated for the above entries. */
    uint64_t              cbTotalAllocated;
    /** Total number of file content bytes.    */
    uint64_t              cbTotalFiles;

} RTHTTPDIRCOLLECTION;
/** Pointer to a directory collection. */
typedef RTHTTPDIRCOLLECTION *PRTHTTPDIRCOLLECTION;
/** Pointer to a directory entry collection pointer. */
typedef PRTHTTPDIRCOLLECTION *PPRTHTTPDIRCOLLECTION;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Set by the signal handler when the HTTP server shall be terminated. */
static volatile bool  g_fCanceled = false;
static HTTPSERVERDATA g_HttpServerData;


#ifdef RT_OS_WINDOWS
static BOOL WINAPI signalHandler(DWORD dwCtrlType) RT_NOTHROW_DEF
{
    bool fEventHandled = FALSE;
    switch (dwCtrlType)
    {
        /* User pressed CTRL+C or CTRL+BREAK or an external event was sent
         * via GenerateConsoleCtrlEvent(). */
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_C_EVENT:
            ASMAtomicWriteBool(&g_fCanceled, true);
            fEventHandled = TRUE;
            break;
        default:
            break;
        /** @todo Add other events here. */
    }

    return fEventHandled;
}
#else /* !RT_OS_WINDOWS */
/**
 * Signal handler that sets g_fCanceled.
 *
 * This can be executed on any thread in the process, on Windows it may even be
 * a thread dedicated to delivering this signal.  Don't do anything
 * unnecessary here.
 */
static void signalHandler(int iSignal) RT_NOTHROW_DEF
{
    NOREF(iSignal);
    ASMAtomicWriteBool(&g_fCanceled, true);
}
#endif

/**
 * Installs a custom signal handler to get notified
 * whenever the user wants to intercept the program.
 *
 * @todo Make this handler available for all VBoxManage modules?
 */
static int signalHandlerInstall(void)
{
    g_fCanceled = false;

    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)signalHandler, TRUE /* Add handler */))
    {
        rc = RTErrConvertFromWin32(GetLastError());
        RTMsgError("Unable to install console control handler, rc=%Rrc\n", rc);
    }
#else
    signal(SIGINT,   signalHandler);
    signal(SIGTERM,  signalHandler);
# ifdef SIGBREAK
    signal(SIGBREAK, signalHandler);
# endif
#endif
    return rc;
}

/**
 * Uninstalls a previously installed signal handler.
 */
static int signalHandlerUninstall(void)
{
    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)NULL, FALSE /* Remove handler */))
    {
        rc = RTErrConvertFromWin32(GetLastError());
        RTMsgError("Unable to uninstall console control handler, rc=%Rrc\n", rc);
    }
#else
    signal(SIGINT,   SIG_DFL);
    signal(SIGTERM,  SIG_DFL);
# ifdef SIGBREAK
    signal(SIGBREAK, SIG_DFL);
# endif
#endif
    return rc;
}

static int dirOpen(const char *pszPathAbs, PRTVFSDIR phVfsDir)
{
    return RTVfsChainOpenDir(pszPathAbs, 0 /*fFlags*/, phVfsDir, NULL /* poffError */, NULL /* pErrInfo */);
}

static int dirClose(RTVFSDIR hVfsDir)
{
    RTVfsDirRelease(hVfsDir);

    return VINF_SUCCESS;
}

static int dirRead(RTVFSDIR hVfsDir, char **ppszEntry, PRTFSOBJINFO pInfo)
{
    size_t          cbDirEntryAlloced = sizeof(RTDIRENTRYEX);
    PRTDIRENTRYEX   pDirEntry         = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntryAlloced);
    if (!pDirEntry)
        return VERR_NO_MEMORY;

    int rc;

    for (;;)
    {
        size_t cbDirEntry = cbDirEntryAlloced;
        rc = RTVfsDirReadEx(hVfsDir, pDirEntry, &cbDirEntry, RTFSOBJATTRADD_UNIX);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_BUFFER_OVERFLOW)
            {
                RTMemTmpFree(pDirEntry);
                cbDirEntryAlloced = RT_ALIGN_Z(RT_MIN(cbDirEntry, cbDirEntryAlloced) + 64, 64);
                pDirEntry         = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntryAlloced);
                if (pDirEntry)
                    continue;
            }
            else
                break;
        }

        /* Skip dot directories. */
        if (RTDirEntryExIsStdDotLink(pDirEntry))
            continue;

        *ppszEntry = RTStrDup(pDirEntry->szName);
        AssertPtrReturn(*ppszEntry, VERR_NO_MEMORY);

        *pInfo = pDirEntry->Info;

        break;

    } /* for */

    RTMemTmpFree(pDirEntry);
    pDirEntry = NULL;

    return rc;
}

#ifdef IPRT_HTTP_WITH_WEBDAV
static int dirEntryWriteDAV(char *pszBuf, size_t cbBuf,
                            const char *pszEntry, const PRTFSOBJINFO pObjInfo, size_t *pcbWritten)
{
    char szBirthTime[32];
    if (RTTimeSpecToString(&pObjInfo->BirthTime, szBirthTime, sizeof(szBirthTime)) == NULL)
        return VERR_BUFFER_UNDERFLOW;

    char szModTime[32];
    if (RTTimeSpecToString(&pObjInfo->ModificationTime, szModTime, sizeof(szModTime)) == NULL)
        return VERR_BUFFER_UNDERFLOW;

    int rc = VINF_SUCCESS;

    /**
     * !!! HACK ALERT !!!
     ** @todo Build up and use a real XML DOM here. Works with Gnome / Gvfs-compatible apps though.
     * !!! HACK ALERT !!!
     */
    ssize_t cch = RTStrPrintf(pszBuf, cbBuf,
"<d:response>"
"<d:href>%s</d:href>"
"<d:propstat>"
"<d:status>HTTP/1.1 200 OK</d:status>"
"<d:prop>"
"<d:displayname>%s</d:displayname>"
"<d:getcontentlength>%RU64</d:getcontentlength>"
"<d:getcontenttype>%s</d:getcontenttype>"
"<d:creationdate>%s</d:creationdate>"
"<d:getlastmodified>%s</d:getlastmodified>"
"<d:getetag/>"
"<d:resourcetype><d:collection/></d:resourcetype>"
"</d:prop>"
"</d:propstat>"
"</d:response>",
                        pszEntry, pszEntry, pObjInfo->cbObject, "application/octet-stream", szBirthTime, szModTime);

    if (cch <= 0)
        rc = VERR_BUFFER_OVERFLOW;

    *pcbWritten = cch;

    return rc;
}

static int writeHeaderDAV(PRTHTTPSERVERREQ pReq, PRTFSOBJINFO pObjInfo, char *pszBuf, size_t cbBuf, size_t *pcbWritten)
{
    /**
     * !!! HACK ALERT !!!
     ** @todo Build up and use a real XML DOM here. Works with Gnome / Gvfs-compatible apps though.
     * !!! HACK ALERT !!!
     */

    size_t cbWritten = 0;

    ssize_t cch = RTStrPrintf2(pszBuf, cbBuf - cbWritten, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n");
    AssertReturn(cch, VERR_BUFFER_UNDERFLOW);
    pszBuf    += cch;
    cbWritten += cch;

    cch = RTStrPrintf2(pszBuf, cbBuf - cbWritten, "<d:multistatus xmlns:d=\"DAV:\">\r\n");
    AssertReturn(cch, VERR_BUFFER_UNDERFLOW);
    pszBuf    += cch;
    cbWritten += cch;

    int rc = dirEntryWriteDAV(pszBuf, cbBuf - cbWritten, pReq->pszUrl, pObjInfo, (size_t *)&cch);
    AssertRC(rc);
    pszBuf    += cch;
    cbWritten += cch;

    *pcbWritten += cbWritten;

    return rc;
}

static int writeFooterDAV(PRTHTTPSERVERREQ pReq, char *pszBuf, size_t cbBuf, size_t *pcbWritten)
{
    RT_NOREF(pReq, pcbWritten);

    /**
     * !!! HACK ALERT !!!
     ** @todo Build up and use a real XML DOM here. Works with Gnome / Gvfs-compatible apps though.
     * !!! HACK ALERT !!!
     */
    ssize_t cch = RTStrPrintf2(pszBuf, cbBuf, "</d:multistatus>");
    AssertReturn(cch, VERR_BUFFER_UNDERFLOW);
    RT_NOREF(cch);

    return VINF_SUCCESS;
}
#endif /* IPRT_HTTP_WITH_WEBDAV */

static int dirEntryWrite(RTHTTPMETHOD enmMethod, char *pszBuf, size_t cbBuf,
                         const char *pszEntry, const PRTFSOBJINFO pObjInfo, size_t *pcbWritten)
{
    char szModTime[32];
    if (RTTimeSpecToString(&pObjInfo->ModificationTime, szModTime, sizeof(szModTime)) == NULL)
        return VERR_BUFFER_UNDERFLOW;

    int rc = VINF_SUCCESS;

    ssize_t cch = 0;

    if (enmMethod == RTHTTPMETHOD_GET)
    {
        cch = RTStrPrintf2(pszBuf, cbBuf, "201: %s %RU64 %s %s\r\n",
                           pszEntry, pObjInfo->cbObject, szModTime,
                           /** @todo Very crude; only files and directories are supported for now. */
                           RTFS_IS_FILE(pObjInfo->Attr.fMode) ? "FILE" : "DIRECTORY");
        if (cch <= 0)
            rc = VERR_BUFFER_OVERFLOW;
    }
#ifdef IPRT_HTTP_WITH_WEBDAV
    else if (enmMethod == RTHTTPMETHOD_PROPFIND)
    {
        char szBuf[RTPATH_MAX + _4K]; /** @todo Just a rough guesstimate. */
        rc = dirEntryWriteDAV(szBuf, sizeof(szBuf), pszEntry, pObjInfo, (size_t *)&cch);
        if (RT_SUCCESS(rc))
            rc = RTStrCat(pszBuf, cbBuf, szBuf);
        AssertRC(rc);
    }
#endif /* IPRT_HTTP_WITH_WEBDAV */
    else
        rc = VERR_NOT_SUPPORTED;

    if (RT_SUCCESS(rc))
    {
        *pcbWritten = (size_t)cch;
    }

    return rc;
}

/**
 * Resolves (and validates) a given URL to an absolute (local) path.
 *
 * @returns VBox status code.
 * @param   pThis               HTTP server instance data.
 * @param   pszUrl              URL to resolve.
 * @param   ppszPathAbs         Where to store the resolved absolute path on success.
 *                              Needs to be free'd with RTStrFree().
 */
static int pathResolve(PHTTPSERVERDATA pThis, const char *pszUrl, char **ppszPathAbs)
{
    /* Construct absolute path. */
    char *pszPathAbs = NULL;
    if (RTStrAPrintf(&pszPathAbs, "%s/%s", pThis->szPathRootAbs, pszUrl) <= 0)
        return VERR_NO_MEMORY;

#ifdef VBOX_STRICT
    RTFSOBJINFO objInfo;
    int rc2 = RTPathQueryInfo(pszPathAbs, &objInfo, RTFSOBJATTRADD_NOTHING);
    AssertRCReturn(rc2, rc2); RT_NOREF(rc2);
    AssertReturn(!RTFS_IS_SYMLINK(objInfo.Attr.fMode), VERR_NOT_SUPPORTED);
#endif

    *ppszPathAbs = pszPathAbs;

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) onOpen(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void **ppvHandle)
{
    PHTTPSERVERDATA pThis = (PHTTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(HTTPSERVERDATA));

    char *pszPathAbs = NULL;
    int rc = pathResolve(pThis, pReq->pszUrl, &pszPathAbs);
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO objInfo;
        rc = RTPathQueryInfo(pszPathAbs, &objInfo, RTFSOBJATTRADD_NOTHING);
        AssertRCReturn(rc, rc);
        if (RTFS_IS_DIRECTORY(objInfo.Attr.fMode))
        {
            /* Nothing to do here;
             * The directory listing has been cached already in onQueryInfo(). */
        }
        else if (RTFS_IS_FILE(objInfo.Attr.fMode))
        {
            rc = RTFileOpen(&pThis->h.File, pszPathAbs,
                            RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        }

        if (RT_SUCCESS(rc))
        {
            pThis->fMode = objInfo.Attr.fMode;

             uint64_t *puHandle = (uint64_t *)RTMemAlloc(sizeof(uint64_t));
             *puHandle  = 42; /** @todo Fudge. */
             *ppvHandle = puHandle;
        }

        RTStrFree(pszPathAbs);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static DECLCALLBACK(int) onRead(PRTHTTPCALLBACKDATA pData,
                                PRTHTTPSERVERREQ pReq, void *pvHandle, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    RT_NOREF(pReq);

    PHTTPSERVERDATA pThis = (PHTTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(HTTPSERVERDATA));

    AssertReturn(*(uint64_t *)pvHandle == 42 /** @todo Fudge. */, VERR_NOT_FOUND);

    int rc;

    if (RTFS_IS_DIRECTORY(pThis->fMode))
    {
        PRTHTTPSERVERRESP pResp = &pThis->Resp;

        const size_t cbToCopy = RT_MIN(cbBuf, pResp->Body.cbBodyUsed - pResp->Body.offBody);
        memcpy(pvBuf, (uint8_t *)pResp->Body.pvBody + pResp->Body.offBody, cbToCopy);
        Assert(pResp->Body.cbBodyUsed >= cbToCopy);
        pResp->Body.offBody += cbToCopy;

        *pcbRead = cbToCopy;

        rc = VINF_SUCCESS;
    }
    else if (RTFS_IS_FILE(pThis->fMode))
    {
        rc = RTFileRead(pThis->h.File, pvBuf, cbBuf, pcbRead);
    }
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static DECLCALLBACK(int) onClose(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void *pvHandle)
{
    RT_NOREF(pReq);

    PHTTPSERVERDATA pThis = (PHTTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(HTTPSERVERDATA));

    AssertReturn(*(uint64_t *)pvHandle == 42 /** @todo Fudge. */, VERR_NOT_FOUND);

    int rc;

    if (RTFS_IS_FILE(pThis->fMode))
    {
        rc = RTFileClose(pThis->h.File);
        if (RT_SUCCESS(rc))
            pThis->h.File = NIL_RTFILE;
    }
    else
        rc = VINF_SUCCESS;

    RTMemFree(pvHandle);
    pvHandle = NULL;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static DECLCALLBACK(int) onQueryInfo(PRTHTTPCALLBACKDATA pData,
                                     PRTHTTPSERVERREQ pReq, PRTFSOBJINFO pObjInfo, char **ppszMIMEHint)
{
    PHTTPSERVERDATA pThis = (PHTTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(HTTPSERVERDATA));

    /** !!!! WARNING !!!
     **
     ** Not production-ready code below!
     ** @todo Use something like bodyAdd() instead of the RTStrPrintf2() hacks.
     **
     ** !!!! WARNING !!! */

    char *pszPathAbs = NULL;
    int rc = pathResolve(pThis, pReq->pszUrl, &pszPathAbs);
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO objInfo;
        rc = RTPathQueryInfo(pszPathAbs, &objInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc))
        {
            if (RTFS_IS_DIRECTORY(objInfo.Attr.fMode))
            {
                PRTHTTPSERVERRESP pResp = &pThis->Resp; /* Only one request a time for now. */

                RTVFSDIR hVfsDir;
                rc = dirOpen(pszPathAbs, &hVfsDir);
                if (RT_SUCCESS(rc))
                {
                    RTHttpServerResponseDestroy(pResp);
                    RTHttpServerResponseInitEx(pResp, _64K); /** @todo Make this more dynamic. */

                    char  *pszBody    = (char *)pResp->Body.pvBody;
                    size_t cbBodyLeft = pResp->Body.cbBodyAlloc;

                    /*
                     * Write body header.
                     */
                    if (pReq->enmMethod == RTHTTPMETHOD_GET)
                    {
                        ssize_t cch = RTStrPrintf2(pszBody, cbBodyLeft,
                                                   "300: file://%s\r\n"
                                                   "200: filename content-length last-modified file-type\r\n",
                                                   pReq->pszUrl);
                        Assert(cch);
                        pszBody    += cch;
                        cbBodyLeft -= cch;
                    }
#ifdef IPRT_HTTP_WITH_WEBDAV
                    else if (pReq->enmMethod == RTHTTPMETHOD_PROPFIND)
                    {
                        size_t cbWritten = 0;
                        rc = writeHeaderDAV(pReq, &objInfo, pszBody, cbBodyLeft, &cbWritten);
                        if (RT_SUCCESS(rc))
                        {
                            Assert(cbBodyLeft >= cbWritten);
                            cbBodyLeft -= cbWritten;
                        }

                    }
#endif /* IPRT_HTTP_WITH_WEBDAV */
                    /*
                     * Write body entries.
                     */
                    char       *pszEntry = NULL;
                    RTFSOBJINFO fsObjInfo;
                    while (RT_SUCCESS(rc = dirRead(hVfsDir, &pszEntry, &fsObjInfo)))
                    {
                        LogFlowFunc(("Entry '%s'\n", pszEntry));

                        size_t cbWritten = 0;
                        rc = dirEntryWrite(pReq->enmMethod, pszBody, cbBodyLeft, pszEntry, &fsObjInfo, &cbWritten);
                        if (rc == VERR_BUFFER_OVERFLOW)
                        {
                            pResp->Body.cbBodyAlloc += _4K; /** @todo Improve this. */
                            pResp->Body.pvBody       = RTMemRealloc(pResp->Body.pvBody, pResp->Body.cbBodyAlloc);
                            AssertPtrBreakStmt(pResp->Body.pvBody, rc = VERR_NO_MEMORY);

                            pszBody = (char *)pResp->Body.pvBody;
                            cbBodyLeft += _4K; /** @todo Ditto. */

                            rc = dirEntryWrite(pReq->enmMethod, pszBody, cbBodyLeft, pszEntry, &fsObjInfo, &cbWritten);
                        }

                        if (   RT_SUCCESS(rc)
                            && cbWritten)
                        {
                            pszBody    += cbWritten;
                            Assert(cbBodyLeft > cbWritten);
                            cbBodyLeft -= cbWritten;
                        }

                        RTStrFree(pszEntry);

                        if (RT_FAILURE(rc))
                            break;
                    }

                    if (rc == VERR_NO_MORE_FILES) /* All entries consumed? */
                        rc = VINF_SUCCESS;

                    dirClose(hVfsDir);

                    /*
                     * Write footers, if any.
                     */
                    if (RT_SUCCESS(rc))
                    {
                        if (pReq->enmMethod == RTHTTPMETHOD_GET)
                        {
                            if (ppszMIMEHint)
                                rc = RTStrAPrintf(ppszMIMEHint, "text/plain");
                        }
#ifdef IPRT_HTTP_WITH_WEBDAV
                        else if (pReq->enmMethod == RTHTTPMETHOD_PROPFIND)
                        {
                            rc  = writeFooterDAV(pReq, pszBody, cbBodyLeft, NULL);
                        }
#endif /* IPRT_HTTP_WITH_WEBDAV */

                        pResp->Body.cbBodyUsed = strlen((char *)pResp->Body.pvBody);

                        pObjInfo->cbObject = pResp->Body.cbBodyUsed;
                    }
                }
            }
            else if (RTFS_IS_FILE(objInfo.Attr.fMode))
            {
                RTFILE hFile;
                rc = RTFileOpen(&hFile, pszPathAbs,
                                RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                if (RT_SUCCESS(rc))
                {
                    rc = RTFileQueryInfo(hFile, pObjInfo, RTFSOBJATTRADD_NOTHING);

                    RTFileClose(hFile);
                }
            }
            else
                rc = VERR_NOT_SUPPORTED;
        }

        RTStrFree(pszPathAbs);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static DECLCALLBACK(int) onDestroy(PRTHTTPCALLBACKDATA pData)
{
    PHTTPSERVERDATA pThis = (PHTTPSERVERDATA)pData->pvUser;
    Assert(pData->cbUser == sizeof(HTTPSERVERDATA));

    RTHttpServerResponseDestroy(&pThis->Resp);

    return VINF_SUCCESS;
}

int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /* Use some sane defaults. */
    char     szAddress[64] = "localhost";
    uint16_t uPort         = 8080;

    RT_ZERO(g_HttpServerData);

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--address",      'a', RTGETOPT_REQ_IPV4ADDR }, /** @todo Use a string for DNS hostnames? */
        /** @todo Implement IPv6 support? */
        { "--port",         'p', RTGETOPT_REQ_UINT16 },
        { "--root-dir",     'r', RTGETOPT_REQ_STRING },
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING }
    };

    RTEXITCODE      rcExit          = RTEXITCODE_SUCCESS;
    unsigned        uVerbosityLevel = 1;

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            case 'a':
                RTStrPrintf2(szAddress, sizeof(szAddress), "%RU8.%RU8.%RU8.%RU8", /** @todo Improve this. */
                             ValueUnion.IPv4Addr.au8[0], ValueUnion.IPv4Addr.au8[1], ValueUnion.IPv4Addr.au8[2], ValueUnion.IPv4Addr.au8[3]);
                break;

            case 'p':
                uPort = ValueUnion.u16;
                break;

            case 'r':
                RTStrCopy(g_HttpServerData.szPathRootAbs, sizeof(g_HttpServerData.szPathRootAbs), ValueUnion.psz);
                break;

            case 'v':
                uVerbosityLevel++;
                break;

            case 'h':
                RTPrintf("Usage: %s [options]\n"
                         "\n"
                         "Options:\n"
                         "  -a, --address (default: localhost)\n"
                         "      Specifies the address to use for listening.\n"
                         "  -p, --port (default: 8080)\n"
                         "      Specifies the port to use for listening.\n"
                         "  -r, --root-dir (default: current dir)\n"
                         "      Specifies the root directory being served.\n"
                         "  -v, --verbose\n"
                         "      Controls the verbosity level.\n"
                         "  -h, -?, --help\n"
                         "      Display this help text and exit successfully.\n"
                         "  -V, --version\n"
                         "      Display the revision and exit successfully.\n"
                         , RTPathFilename(argv[0]));
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    if (!strlen(g_HttpServerData.szPathRootAbs))
    {
        /* By default use the current directory as serving root directory. */
        rc = RTPathGetCurrent(g_HttpServerData.szPathRootAbs, sizeof(g_HttpServerData.szPathRootAbs));
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Retrieving current directory failed: %Rrc", rc);
    }

    /* Install signal handler. */
    rc = signalHandlerInstall();
    if (RT_SUCCESS(rc))
    {
        /*
         * Create the HTTP server instance.
         */
        RTHTTPSERVERCALLBACKS Callbacks;
        RT_ZERO(Callbacks);

        Callbacks.pfnOpen          = onOpen;
        Callbacks.pfnRead          = onRead;
        Callbacks.pfnClose         = onClose;
        Callbacks.pfnQueryInfo     = onQueryInfo;
        Callbacks.pfnDestroy       = onDestroy;

        g_HttpServerData.h.File = NIL_RTFILE;
        g_HttpServerData.h.Dir  = NIL_RTVFSDIR;

        rc = RTHttpServerResponseInit(&g_HttpServerData.Resp);
        AssertRC(rc);

        RTHTTPSERVER hHTTPServer;
        rc = RTHttpServerCreate(&hHTTPServer, szAddress, uPort, &Callbacks,
                                &g_HttpServerData, sizeof(g_HttpServerData));
        if (RT_SUCCESS(rc))
        {
            RTPrintf("Starting HTTP server at %s:%RU16 ...\n", szAddress, uPort);
            RTPrintf("Root directory is '%s'\n", g_HttpServerData.szPathRootAbs);

            RTPrintf("Running HTTP server ...\n");

            for (;;)
            {
                RTThreadSleep(200);

                if (g_fCanceled)
                    break;
            }

            RTPrintf("Stopping HTTP server ...\n");

            int rc2 = RTHttpServerDestroy(hHTTPServer);
            if (RT_SUCCESS(rc))
                rc = rc2;

            RTPrintf("Stopped HTTP server\n");
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTHttpServerCreate failed: %Rrc", rc);

        int rc2 = signalHandlerUninstall();
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    /* Set rcExit on failure in case we forgot to do so before. */
    if (RT_FAILURE(rc))
        rcExit = RTEXITCODE_FAILURE;

    return rcExit;
}

