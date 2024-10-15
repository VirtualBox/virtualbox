/* $Id: clipboard-transfers-http.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard: HTTP server implementation for Shared Clipboard transfers on UNIX-y guests / hosts.
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
 * SPDX-License-Identifier: GPL-3.0-only
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
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/uri.h>
#include <iprt/uuid.h>
#include <iprt/vfs.h>

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <iprt/log.h>

#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/SharedClipboard-x11.h>
#include <VBox/GuestHost/SharedClipboard-transfers.h>


/*********************************************************************************************************************************
*   Definitations                                                                                                                *
*********************************************************************************************************************************/

#ifdef DEBUG_andy_0
/** When enabled, this lets the HTTP server run at a predictable URL and port for debugging:
 *  URL: http://localhost:49200/transfer<ID> */
# define VBOX_SHCL_DEBUG_HTTPSERVER
#endif

typedef struct _SHCLHTTPSERVERTRANSFER
{
    /** The node list. */
    RTLISTNODE          Node;
    /** Pointer to associated transfer. */
    PSHCLTRANSFER       pTransfer;
    /** Critical section for serializing access. */
    RTCRITSECT          CritSect;
    /** The handle we're going to use for this HTTP transfer. */
    SHCLOBJHANDLE       hObj;
    /** The virtual path of the HTTP server's root directory for this transfer.
     *  Always has to start with a "/". Unescaped. */
    char                szPathVirtual[RTPATH_MAX];
} SHCLHTTPSERVERTRANSFER;
typedef SHCLHTTPSERVERTRANSFER *PSHCLHTTPSERVERTRANSFER;


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static int shClTransferHttpServerDestroyInternal(PSHCLHTTPSERVER pThis);
static const char *shClTransferHttpServerGetHost(PSHCLHTTPSERVER pSrv);
static int shClTransferHttpServerDestroyTransfer(PSHCLHTTPSERVER pSrv, PSHCLHTTPSERVERTRANSFER pSrvTx);
static SHCLHTTPSERVERSTATUS shclTransferHttpServerSetStatusLocked(PSHCLHTTPSERVER pSrv, SHCLHTTPSERVERSTATUS fStatus);


/*********************************************************************************************************************************
*   Static assets                                                                                                                *
*********************************************************************************************************************************/

static char s_shClHttpServerPage404[] = " \
<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \
        \"http://www.w3.org/TR/html4/strict.dtd\"> \
<html> \
    <head> \
        <meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\"> \
        <title>VirtualBox Shared Clipboard</title> \
    </head> \
    <body> \
        <h1>VirtualBox Shared Clipboard</h1> \
        <p>Error: 404</p> \
        <p>Message: Entry not found.</p> \
    </body> \
</html>";


/*********************************************************************************************************************************
*   Internal Shared Clipboard HTTP transfer functions                                                                            *
*********************************************************************************************************************************/

/**
 * Locks the critical section of a Shared Clipboard HTTP server instance.
 *
 * @param   pSrv                Shared Clipboard HTTP server instance to lock.
 */
DECLINLINE(void) shClTransferHttpServerLock(PSHCLHTTPSERVER pSrv)
{
    int rc2 = RTCritSectEnter(&pSrv->CritSect);
    AssertRC(rc2);
}

/**
 * Unlocks the critical section of a Shared Clipboard HTTP server instance.
 *
 * @param   pSrv                Shared Clipboard HTTP server instance to unlock.
 */
DECLINLINE(void) shClTransferHttpServerUnlock(PSHCLHTTPSERVER pSrv)
{
    int rc2 = RTCritSectLeave(&pSrv->CritSect);
    AssertRC(rc2);
}

/**
 * Locks an HTTP transfer.
 *
 * @param   pSrvTx              HTTP transfer to lock.
 */
DECLINLINE(void) shClHttpTransferLock(PSHCLHTTPSERVERTRANSFER pSrvTx)
{
    int rc2 = RTCritSectEnter(&pSrvTx->CritSect);
    AssertRC(rc2);
}

/**
 * Unlocks an HTTP transfer.
 *
 * @param   pSrvTx              HTTP transfer to unlock.
 */
DECLINLINE(void) shClHttpTransferUnlock(PSHCLHTTPSERVERTRANSFER pSrvTx)
{
    int rc2 = RTCritSectLeave(&pSrvTx->CritSect);
    AssertRC(rc2);
}

/**
 * Creates an URL from a given path, extended version.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_PARAMETER if the path is not valid.
 * @param   pszPath             Path to create URL for.
 * @param   ppszURL             Where to return the allocated URL on success.
 * @param   pchScheme           Where to return the size of the full HTTP scheme including "://". Optional and can be NULL.
 *                              Right now this always is sizeof("http://").
 *
 * @note    The path is not checked on file system level.
 */
static int shClTransferHttpURLCreateFromPathEx(const char *pszPath, char **ppszURL, size_t *pchScheme)
{
    AssertRCReturn(ShClTransferValidatePath(pszPath, false /* fMustExist */), VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;

    const char   szScheme[] = "http://"; /** @todo For now we only support HTTP. */
    const size_t cchScheme  = strlen(szScheme);

    char *pszURL = RTStrAPrintf2("%s%s", szScheme, pszPath);
    if (pszURL)
    {
        AssertReturn(strlen(pszURL) > cchScheme, VERR_INVALID_PARAMETER);

        *ppszURL = pszURL;
        if (pchScheme)
            *pchScheme = cchScheme;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Creates an URL from a given path.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_PARAMETER if the path is not valid.
 * @param   pszPath             Path to create URL for.
 * @param   ppszURL             Where to return the allocated URL on success.
 *
 * @note    The path is not checked on file system level.
 */
static int shClTransferHttpURLCreateFromPath(const char *pszPath, char **ppszURL)
{
    return shClTransferHttpURLCreateFromPathEx(pszPath, ppszURL, NULL /* pchScheme */);
}

/**
 * Return the HTTP server transfer for a specific transfer ID.
 *
 * @returns Pointer to HTTP server transfer if found, NULL if not found.
 * @param   pSrv                HTTP server instance.
 * @param   idTransfer          Transfer ID to return HTTP server transfer for.
 */
DECLINLINE(PSHCLHTTPSERVERTRANSFER) shClTransferHttpServerGetTransferById(PSHCLHTTPSERVER pSrv, SHCLTRANSFERID idTransfer)
{
    PSHCLHTTPSERVERTRANSFER pSrvTx;
    RTListForEach(&pSrv->lstTransfers, pSrvTx, SHCLHTTPSERVERTRANSFER, Node) /** @todo Slow O(n) lookup, but does it for now. */
    {
        if (pSrvTx->pTransfer->State.uID == idTransfer)
            return pSrvTx;
    }

    return NULL;
}

/**
 * Returns a HTTP server transfer from a given URL.
 *
 * @returns Pointer to HTTP server transfer if found, NULL if not found.
 * @param   pThis               HTTP server instance data.
 * @param   pszUrl              URL to validate.
 */
DECLINLINE(PSHCLHTTPSERVERTRANSFER) shClTransferHttpGetTransferFromUrl(PSHCLHTTPSERVER pThis, const char *pszUrl)
{
    AssertPtrReturn(pszUrl, NULL);

    PSHCLHTTPSERVERTRANSFER pSrvTx = NULL;

    PSHCLHTTPSERVERTRANSFER pSrvTxCur;
    RTListForEach(&pThis->lstTransfers, pSrvTxCur, SHCLHTTPSERVERTRANSFER, Node)
    {
        AssertPtr(pSrvTxCur->pTransfer);

        LogFlowFunc(("pSrvTxCur=%s\n", pSrvTxCur->szPathVirtual));

        /* Be picky here, do a case sensitive comparison. */
        if (RTStrStartsWith(pszUrl, pSrvTxCur->szPathVirtual))
        {
            pSrvTx = pSrvTxCur;
            break;
        }
    }

    if (!pSrvTx)
        LogRel2(("Shared Clipboard: HTTP URL '%s' not valid\n", pszUrl));

    LogFlowFunc(("pszUrl=%s, pSrvTx=%p\n", pszUrl, pSrvTx));
    return pSrvTx;
}

/**
 * Returns a HTTP server transfer from an internal HTTP handle.
 *
 * @returns Pointer to HTTP server transfer if found, NULL if not found.
 * @param   pThis               HTTP server instance data.
 * @param   pvHandle            Handle to return transfer for.
 */
DECLINLINE(PSHCLHTTPSERVERTRANSFER) shClTransferHttpGetTransferFromHandle(PSHCLHTTPSERVER pThis, void *pvHandle)
{
    AssertPtrReturn(pvHandle, NULL);

    const SHCLTRANSFERID uHandle = *(uint16_t *)pvHandle;

    /** @ŧodo Use a handle lookup table (map) later. */
    PSHCLHTTPSERVERTRANSFER pSrvTxCur;
    RTListForEach(&pThis->lstTransfers, pSrvTxCur, SHCLHTTPSERVERTRANSFER, Node)
    {
        AssertPtr(pSrvTxCur->pTransfer);

        if (pSrvTxCur->pTransfer->State.uID == uHandle) /** @ŧodo We're using the transfer ID as handle for now. */
            return pSrvTxCur;
    }

    return NULL;
}


/*********************************************************************************************************************************
*   HTTP server callback implementations                                                                                         *
*********************************************************************************************************************************/

/** @copydoc RTHTTPSERVERCALLBACKS::pfnRequestBegin */
static DECLCALLBACK(int) shClTransferHttpBegin(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq)
{
    PSHCLHTTPSERVER pThis = (PSHCLHTTPSERVER)pData->pvUser; RT_NOREF(pThis);
    Assert(pData->cbUser == sizeof(SHCLHTTPSERVER));

    LogRel2(("Shared Clipboard: HTTP request begin\n"));

    PSHCLHTTPSERVERTRANSFER pSrvTx = shClTransferHttpGetTransferFromUrl(pThis, pReq->pszUrl);
    if (pSrvTx)
    {
        pReq->pvUser = pSrvTx;
    }

    return VINF_SUCCESS;
}

/** @copydoc RTHTTPSERVERCALLBACKS::pfnRequestEnd */
static DECLCALLBACK(int) shClTransferHttpEnd(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq)
{
    PSHCLHTTPSERVER pThis = (PSHCLHTTPSERVER)pData->pvUser; RT_NOREF(pThis);
    Assert(pData->cbUser == sizeof(SHCLHTTPSERVER));

    LogRel2(("Shared Clipboard: HTTP request end\n"));

    PSHCLHTTPSERVERTRANSFER pSrvTx = (PSHCLHTTPSERVERTRANSFER)pReq->pvUser;
    if (pSrvTx)
    {
        pReq->pvUser = NULL;
    }

    return VINF_SUCCESS;

}

/** @copydoc RTHTTPSERVERCALLBACKS::pfnOpen */
static DECLCALLBACK(int) shClTransferHttpOpen(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void **ppvHandle)
{
    PSHCLHTTPSERVER pThis = (PSHCLHTTPSERVER)pData->pvUser; RT_NOREF(pThis);
    Assert(pData->cbUser == sizeof(SHCLHTTPSERVER));

    int rc;

    AssertPtr(pReq->pvUser);
    PSHCLHTTPSERVERTRANSFER pSrvTx = (PSHCLHTTPSERVERTRANSFER)pReq->pvUser;
    if (pSrvTx)
    {
        LogRel2(("Shared Clipboard: HTTP transfer (handle %RU64) started ...\n", pSrvTx->hObj));

        Assert(pSrvTx->hObj != NIL_SHCLOBJHANDLE);
        *ppvHandle = &pSrvTx->hObj;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NOT_FOUND;

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Error starting HTTP transfer for '%s', rc=%Rrc\n", pReq->pszUrl, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc RTHTTPSERVERCALLBACKS::pfnRead */
static DECLCALLBACK(int) shClTransferHttpRead(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq,
                                              void *pvHandle, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    RT_NOREF(pData);

    if (pvHandle == NULL) /* Serve a 404 page if we got an invalid handle. */
    {
        Assert(cbBuf >= sizeof(s_shClHttpServerPage404)); /* Keep it simple for now. */
        memcpy(pvBuf, &s_shClHttpServerPage404, RT_MIN(cbBuf, sizeof(s_shClHttpServerPage404)));
        *pcbRead = sizeof(s_shClHttpServerPage404);
        return VINF_SUCCESS;
    }

    int rc;

    LogRel3(("Shared Clipboard: Reading %RU32 bytes from HTTP ...\n", cbBuf));

    AssertPtr(pReq->pvUser);
    PSHCLHTTPSERVERTRANSFER pSrvTx = (PSHCLHTTPSERVERTRANSFER)pReq->pvUser;
    if (pSrvTx)
    {
        PSHCLOBJHANDLE phObj = (PSHCLOBJHANDLE)pvHandle;
        if (phObj)
        {
            uint32_t cbRead;
            rc = ShClTransferObjRead(pSrvTx->pTransfer, *phObj, pvBuf, cbBuf, 0 /* fFlags */, &cbRead);
            if (RT_SUCCESS(rc))
                *pcbRead = (uint32_t)cbRead;

            if (RT_FAILURE(rc))
                LogRel(("Shared Clipboard: Error reading HTTP transfer (handle %RU64), rc=%Rrc\n", *phObj, rc));
        }
        else
            rc = VERR_NOT_FOUND;
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc RTHTTPSERVERCALLBACKS::pfnClose */
static DECLCALLBACK(int) shClTransferHttpClose(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void *pvHandle)
{
    RT_NOREF(pData);

    int rc;

    AssertPtr(pReq->pvUser);
    PSHCLHTTPSERVERTRANSFER pSrvTx = (PSHCLHTTPSERVERTRANSFER)pReq->pvUser;
    if (pSrvTx)
    {
        PSHCLOBJHANDLE phObj = (PSHCLOBJHANDLE)pvHandle;
        if (phObj)
        {
            Assert(*phObj != NIL_SHCLOBJHANDLE);
            rc = ShClTransferObjClose(pSrvTx->pTransfer, *phObj);
            if (RT_SUCCESS(rc))
            {
                pSrvTx->hObj = NIL_SHCLOBJHANDLE;
                LogRel2(("Shared Clipboard: HTTP transfer %RU16 done\n", pSrvTx->pTransfer->State.uID));
            }

            if (RT_FAILURE(rc))
                LogRel(("Shared Clipboard: Error closing HTTP transfer (handle %RU64), rc=%Rrc\n", *phObj, rc));
        }
        else
            rc = VERR_NOT_FOUND;
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc RTHTTPSERVERCALLBACKS::pfnQueryInfo */
static DECLCALLBACK(int) shClTransferHttpQueryInfo(PRTHTTPCALLBACKDATA pData,
                                                   PRTHTTPSERVERREQ pReq, PRTFSOBJINFO pObjInfo, char **ppszMIMEHint)
{
    RT_NOREF(pData);
    RT_NOREF(ppszMIMEHint);

    AssertReturn(RTStrIsValidEncoding(pReq->pszUrl), VERR_INVALID_PARAMETER);

    LogRel2(("Shared Clipboard: HTTP query for '%s' ...\n", pReq->pszUrl));

    char *pszUrl;
    int rc = shClTransferHttpURLCreateFromPath(pReq->pszUrl, &pszUrl);
    AssertRCReturn(rc, rc);

    RTURIPARSED Parsed;
    rc = RTUriParse(pszUrl, &Parsed);
    if (RT_SUCCESS(rc))
    {
        char        *pszParsedPath = RTUriParsedPath(pszUrl, &Parsed);
        AssertPtrReturn(pszParsedPath, VERR_NO_MEMORY); /* Should be okay, as we succeeded RTUriParse() above. */
        size_t const cchParsedPath = strlen(pszParsedPath);

        /* For now we only know the transfer -- now we need to figure out the entry we want to serve. */
        PSHCLHTTPSERVERTRANSFER pSrvTx = (PSHCLHTTPSERVERTRANSFER)pReq->pvUser;
        if (pSrvTx)
        {
            size_t const cchRoot = strlen(pSrvTx->szPathVirtual) + 1 /* Skip slash separating the base from the rest */;
            AssertStmt(cchParsedPath >= cchRoot, rc = VERR_INVALID_PARAMETER);
            const char  *pszRoot = pszParsedPath + cchRoot; /* Marks the actual root path. */
            AssertStmt(RT_VALID_PTR(pszRoot), rc = VERR_INVALID_POINTER);
            AssertStmt(*pszRoot != '\0',      rc = VERR_INVALID_PARAMETER);

            if (RT_SUCCESS(rc))
            {
                SHCLOBJOPENCREATEPARMS openParms;
                rc = ShClTransferObjOpenParmsInit(&openParms);
                if (RT_SUCCESS(rc))
                {
                    openParms.fCreate = SHCL_OBJ_CF_ACCESS_READ
                                      | SHCL_OBJ_CF_ACCESS_DENYWRITE;

                    PSHCLTRANSFER pTx = pSrvTx->pTransfer;
                    AssertPtr(pTx);

                    rc = VERR_NOT_FOUND; /* Must find the matching root entry first. */

                    Log3Func(("pszParsedPath=%s\n", pszParsedPath));

                    uint64_t const cRoots = ShClTransferRootsCount(pTx);
                    for (uint32_t i = 0; i < cRoots; i++)
                    {
                        PCSHCLLISTENTRY pEntry = ShClTransferRootsEntryGet(pTx, i);
                        AssertPtrBreakStmt(pEntry, rc = VERR_NOT_FOUND);

                        Log3Func(("pszRoot=%s vs. pEntry=%s\n", pszRoot, pEntry->pszName));

                        if (RTStrCmp(pszRoot, pEntry->pszName)) /* Case-sensitive! */
                            continue;

                        rc = RTStrCopy(openParms.pszPath, openParms.cbPath, pEntry->pszName);
                        if (RT_SUCCESS(rc))
                        {
                            rc = ShClTransferObjOpen(pTx, &openParms, &pSrvTx->hObj);
                            if (RT_SUCCESS(rc))
                            {
                                rc = VERR_NOT_SUPPORTED; /* Play safe by default. */

                                if (   pEntry->fInfo & VBOX_SHCL_INFO_F_FSOBJINFO
                                    && pEntry->cbInfo == sizeof(SHCLFSOBJINFO))
                                {
                                    PCSHCLFSOBJINFO pSrcObjInfo = (PSHCLFSOBJINFO)pEntry->pvInfo;

                                    LogFlowFunc(("pszName=%s, cbInfo=%RU32, fMode=%#x (type %#x)\n",
                                                 pEntry->pszName, pEntry->cbInfo, pSrcObjInfo->Attr.fMode, (pSrcObjInfo->Attr.fMode & RTFS_TYPE_MASK)));

                                    LogRel2(("Shared Clipboard: HTTP object info: fMode=%#x, cbObject=%zu\n", pSrcObjInfo->Attr.fMode, pSrcObjInfo->cbObject));

                                    if (RTFS_IS_FILE(pSrcObjInfo->Attr.fMode))
                                    {
                                        memcpy(pObjInfo, pSrcObjInfo, sizeof(SHCLFSOBJINFO));
                                        rc = VINF_SUCCESS;
                                    }
                                }
                                else
                                    LogRel2(("Shared Clipboard: Supplied entry information for '%s' not supported (fInfo=%#x, cbInfo=%RU32\n",
                                             pEntry->pszName, pEntry->fInfo, pEntry->cbInfo));
                                /* Note: Directories / symlinks or other fancy stuff is not supported here (yet) -- would require using WebDAV. */
                            }
                        }

                        break;
                    }

                    ShClTransferObjOpenParmsDestroy(&openParms);
                }
            }

            RTStrFree(pszParsedPath);
            pszParsedPath = NULL;
        }
        else
            rc = VERR_NOT_FOUND;
    }

    RTStrFree(pszUrl);

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Querying info for HTTP transfer failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/*********************************************************************************************************************************
*   Internal Shared Clipboard HTTP server functions                                                                              *
*********************************************************************************************************************************/

/**
 * Destroys a Shared Clipboard HTTP server instance, internal version.
 *
 * @returns VBox status code.
 * @param   pSrv                Shared Clipboard HTTP server instance to destroy.
 *
 * @note    Caller needs to take the critical section.
 */
static int shClTransferHttpServerDestroyInternal(PSHCLHTTPSERVER pSrv)
{
    Assert(RTCritSectIsOwner(&pSrv->CritSect));

    LogFlowFuncEnter();

    pSrv->fInitialized = false;
    pSrv->fRunning     = false;

    int rc = VINF_SUCCESS;

    PSHCLHTTPSERVERTRANSFER pSrvTx, pSrvTxNext;
    RTListForEachSafe(&pSrv->lstTransfers, pSrvTx, pSrvTxNext, SHCLHTTPSERVERTRANSFER, Node)
    {
        int rc2 = shClTransferHttpServerDestroyTransfer(pSrv, pSrvTx);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    RTHttpServerResponseDestroy(&pSrv->Resp);

    pSrv->hHTTPServer = NIL_RTHTTPSERVER;

    shClTransferHttpServerUnlock(pSrv); /* Unlock critical section taken by the caller before deleting it. */

    if (RTCritSectIsInitialized(&pSrv->CritSect))
    {
        int rc2 = RTCritSectDelete(&pSrv->CritSect);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    RTSemEventDestroy(pSrv->StatusEvent);
    pSrv->StatusEvent = NIL_RTSEMEVENT;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Initializes a new Shared Clipboard HTTP server instance.
 *
 * @return  VBox status code.
 * @param   pSrv                HTTP server instance to initialize.
 */
static int shClTransferHttpServerInitInternal(PSHCLHTTPSERVER pSrv)
{
    int rc = RTCritSectInit(&pSrv->CritSect);
    AssertRCReturn(rc, rc);

    rc = RTSemEventCreate(&pSrv->StatusEvent);
    AssertRCReturn(rc, rc);

    pSrv->hHTTPServer = NIL_RTHTTPSERVER;
    pSrv->uPort       = 0;
    RTListInit(&pSrv->lstTransfers);
    pSrv->cTransfers  = 0;

    rc = RTHttpServerResponseInit(&pSrv->Resp);
    AssertRCReturn(rc, rc);

    ASMAtomicXchgBool(&pSrv->fInitialized, true);
    ASMAtomicXchgBool(&pSrv->fRunning, false);

    return rc;
}


/*********************************************************************************************************************************
*   Public Shared Clipboard HTTP server functions                                                                                *
*********************************************************************************************************************************/

/**
 * Initializes a new Shared Clipboard HTTP server instance.
 *
 * @return  VBox status code.
 * @param   pSrv                HTTP server instance to initialize.
 */
int ShClTransferHttpServerInit(PSHCLHTTPSERVER pSrv)
{
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);

    return shClTransferHttpServerInitInternal(pSrv);
}

/**
 * Returns whether a given TCP port is known to be buggy or not.
 *
 * @returns \c true if the given port is known to be buggy, or \c false if not.
 * @param   uPort               TCP port to check.
 */
static bool shClTransferHttpServerPortIsBuggy(uint16_t uPort)
{
    uint16_t const aBuggyPorts[] = {
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
        /* GNOME Nautilus ("Files") v43 is unable download HTTP files from this port. */
        8080
#else   /* Prevents zero-sized arrays. */
        0
#endif
    };

    for (size_t i = 0; i < RT_ELEMENTS(aBuggyPorts); i++)
        if (uPort == aBuggyPorts[i])
            return true;
    return false;
}

/**
 * Starts the Shared Clipboard HTTP server instance, extended version.
 *
 * @returns VBox status code.
 * @return  VERR_ADDRESS_CONFLICT if the port is already taken or the port is known to be buggy.
 * @param   pSrv                HTTP server instance to create.
 * @param   uPort               TCP port number to use.
 */
int ShClTransferHttpServerStartEx(PSHCLHTTPSERVER pSrv, uint16_t uPort)
{
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);
    AssertReturn(uPort, VERR_INVALID_PARAMETER);

    AssertReturn(!shClTransferHttpServerPortIsBuggy(uPort), VERR_ADDRESS_CONFLICT);

    shClTransferHttpServerLock(pSrv);

    RTHTTPSERVERCALLBACKS Callbacks;
    RT_ZERO(Callbacks);

    Callbacks.pfnRequestBegin  = shClTransferHttpBegin;
    Callbacks.pfnRequestEnd    = shClTransferHttpEnd;
    Callbacks.pfnOpen          = shClTransferHttpOpen;
    Callbacks.pfnRead          = shClTransferHttpRead;
    Callbacks.pfnClose         = shClTransferHttpClose;
    Callbacks.pfnQueryInfo     = shClTransferHttpQueryInfo;

    /* Note: The server always and *only* runs against the localhost interface. */
    int rc = RTHttpServerCreate(&pSrv->hHTTPServer, "localhost", uPort, &Callbacks,
                                pSrv, sizeof(SHCLHTTPSERVER));
    if (RT_SUCCESS(rc))
    {
        pSrv->uPort = uPort;
        ASMAtomicXchgBool(&pSrv->fRunning, true);

        LogRel2(("Shared Clipboard: HTTP server started at port %RU16\n", pSrv->uPort));

        rc = shclTransferHttpServerSetStatusLocked(pSrv, SHCLHTTPSERVERSTATUS_STARTED);
    }

    shClTransferHttpServerUnlock(pSrv);

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: HTTP server failed to start, rc=%Rrc\n", rc));

    return rc;
}

/**
 * Returns a Shared Clipboard HTTP server status as a string.
 *
 * @returns Status as a string, or "Unknown" if invalid / unknown.
 * @param   enmStatus           HTTP server status to return as a string.
 */
DECLINLINE(const char *) shClTransferHttpServerStatusToStr(SHCLHTTPSERVERSTATUS enmStatus)
{
    switch (enmStatus)
    {
        RT_CASE_RET_STR(SHCLHTTPSERVERSTATUS_NONE);
        RT_CASE_RET_STR(SHCLHTTPSERVERSTATUS_STARTED);
        RT_CASE_RET_STR(SHCLHTTPSERVERSTATUS_STOPPED);
        RT_CASE_RET_STR(SHCLHTTPSERVERSTATUS_TRANSFER_REGISTERED);
        RT_CASE_RET_STR(SHCLHTTPSERVERSTATUS_TRANSFER_UNREGISTERED);
    }

    AssertFailedReturn("Unknown");
}

/**
 * Starts the Shared Clipboard HTTP server instance using a random port (>= 49152).
 *
 * This does automatic probing of TCP ports if a port already is being used.
 *
 * @returns VBox status code.
 * @param   pSrv                HTTP server instance to create.
 * @param   cMaxAttempts        Maximum number of attempts to create a HTTP server.
 * @param   puPort              Where to return the TCP port number being used on success. Optional.
 *
 * @note    Complies with RFC 6335 (IANA).
 */
int ShClTransferHttpServerStart(PSHCLHTTPSERVER pSrv, unsigned cMaxAttempts, uint16_t *puPort)
{
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);
    AssertReturn(cMaxAttempts, VERR_INVALID_PARAMETER);
    /* puPort is optional. */

    int rc;
#ifdef VBOX_SHCL_DEBUG_HTTPSERVER
    uint16_t uDebugPort = 49200;
    rc = ShClTransferHttpServerStartEx(pSrv, (uint32_t)uDebugPort);
    if (RT_SUCCESS(rc))
    {
        if (puPort)
            *puPort = uDebugPort;
    }
    return rc;
#endif

    RTRAND hRand;
    rc = RTRandAdvCreateSystemFaster(&hRand); /* Should be good enough for this task. */
    if (RT_SUCCESS(rc))
    {
        uint16_t uPort;
        unsigned i;
        for (i = 0; i < cMaxAttempts; i++)
        {
            /* Try some random ports >= 49152 (i.e. "dynamic ports", see RFC 6335)
             * -- required, as VBoxClient runs as a user process on the guest. */
            uPort = RTRandAdvU32Ex(hRand, 49152, UINT16_MAX);

            /* If the port selected turns is known to be buggy for whatever reason, skip it and try another one. */
            if (shClTransferHttpServerPortIsBuggy(uPort))
                continue;

            rc = ShClTransferHttpServerStartEx(pSrv, (uint32_t)uPort);
            if (RT_SUCCESS(rc))
            {
                if (puPort)
                    *puPort = uPort;
                break;
            }
        }

        if (   RT_FAILURE(rc)
            && i == cMaxAttempts)
            LogRel(("Shared Clipboard: Maximum attempts to start HTTP server reached (%u), giving up\n", cMaxAttempts));

        RTRandAdvDestroy(hRand);
    }

    return rc;
}

/**
 * Stops a Shared Clipboard HTTP server instance.
 *
 * @returns VBox status code.
 * @param   pSrv                HTTP server instance to stop.
 */
int ShClTransferHttpServerStop(PSHCLHTTPSERVER pSrv)
{
     LogFlowFuncEnter();

     shClTransferHttpServerLock(pSrv);

     int rc = VINF_SUCCESS;

     if (pSrv->fRunning)
     {
         Assert(pSrv->hHTTPServer != NIL_RTHTTPSERVER);

         rc = RTHttpServerDestroy(pSrv->hHTTPServer);
         if (RT_SUCCESS(rc))
         {
             pSrv->hHTTPServer = NIL_RTHTTPSERVER;
             pSrv->fRunning    = false;

             /* Let any eventual waiters know. */
             shclTransferHttpServerSetStatusLocked(pSrv, SHCLHTTPSERVERSTATUS_STOPPED);

             LogRel2(("Shared Clipboard: HTTP server stopped\n"));
         }
     }

     if (RT_FAILURE(rc))
         LogRel(("Shared Clipboard: HTTP server failed to stop, rc=%Rrc\n", rc));

     shClTransferHttpServerUnlock(pSrv);

     LogFlowFuncLeaveRC(rc);
     return rc;
}

/**
 * Destroys a Shared Clipboard HTTP server instance.
 *
 * @returns VBox status code.
 * @param   pSrv                HTTP server instance to destroy.
 */
int ShClTransferHttpServerDestroy(PSHCLHTTPSERVER pSrv)
{
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);

    int rc = ShClTransferHttpServerStop(pSrv);
    if (RT_FAILURE(rc))
        return rc;

    if (!ASMAtomicReadBool(&pSrv->fInitialized))
        return VINF_SUCCESS;

    shClTransferHttpServerLock(pSrv);

    rc = shClTransferHttpServerDestroyInternal(pSrv);

    /* Unlock not needed anymore, as the critical section got destroyed. */

    return rc;
}

/**
 * Returns the host name (scheme) of a HTTP server instance.
 *
 * @returns Host name (scheme).
 * @param   pSrv                HTTP server instance to return host name (scheme) for.
 *
 * @note    This is hardcoded to "localhost" for now.
 */
static const char *shClTransferHttpServerGetHost(PSHCLHTTPSERVER pSrv)
{
    RT_NOREF(pSrv);
    return "http://localhost"; /* Hardcoded for now. */
}

/**
 * Destroys a server transfer, internal version.
 *
 * @returns VBox status code.
 * @param   pSrv                HTTP server instance to unregister transfer from.
 * @param   pSrvTx              HTTP server transfer to destroy.
 *                              The pointer will be invalid on success.
 *
 * @note    Caller needs to take the server critical section.
 */
static int shClTransferHttpServerDestroyTransfer(PSHCLHTTPSERVER pSrv, PSHCLHTTPSERVERTRANSFER pSrvTx)
{
    Assert(RTCritSectIsOwner(&pSrv->CritSect));

    RTListNodeRemove(&pSrvTx->Node);

    Assert(pSrv->cTransfers);
    pSrv->cTransfers--;

    LogFunc(("pTransfer=%p, idTransfer=%RU16, szPath=%s -> %RU32 transfers\n",
             pSrvTx->pTransfer, pSrvTx->pTransfer->State.uID, pSrvTx->szPathVirtual, pSrv->cTransfers));

    LogRel2(("Shared Clipboard: Destroyed HTTP transfer %RU16, now %RU32 HTTP transfers total\n",
             pSrvTx->pTransfer->State.uID, pSrv->cTransfers));

    if (RTCritSectIsInitialized(&pSrvTx->CritSect))
    {
        int rc = RTCritSectDelete(&pSrvTx->CritSect);
        AssertRCReturn(rc, rc);
    }

    RTMemFree(pSrvTx);
    pSrvTx = NULL;

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Public Shared Clipboard HTTP server functions                                                                                *
*********************************************************************************************************************************/

/**
 * Registers a Shared Clipboard transfer to a HTTP server instance.
 *
 * @returns VBox status code.
 * @param   pSrv                HTTP server instance to register transfer for.
 * @param   pTransfer           Transfer to register. Needs to be on the heap.
 */
int ShClTransferHttpServerRegisterTransfer(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    AssertMsgReturn(pTransfer->State.uID, ("Transfer needs to be registered with a transfer context first\n"),
                    VERR_INVALID_PARAMETER);

    uint64_t const cRoots = ShClTransferRootsCount(pTransfer);
    AssertMsgReturn(cRoots  > 0, ("Transfer has no root entries\n"), VERR_INVALID_PARAMETER);
    /** @todo Check for directories? */

    shClTransferHttpServerLock(pSrv);

    PSHCLHTTPSERVERTRANSFER pSrvTx = (PSHCLHTTPSERVERTRANSFER)RTMemAllocZ(sizeof(SHCLHTTPSERVERTRANSFER));
    AssertPtrReturn(pSrvTx, VERR_NO_MEMORY);

    RTUUID Uuid;
    int rc = RTUuidCreate(&Uuid);
    if (RT_SUCCESS(rc))
    {
        char szUuid[64];
        rc = RTUuidToStr(&Uuid, szUuid, sizeof(szUuid));
        if (RT_SUCCESS(rc))
        {
            rc = RTCritSectInit(&pSrvTx->CritSect);
            AssertRCReturn(rc, rc);

            /* Create the virtual HTTP path for the transfer.
             * Every transfer has a dedicated HTTP path (but live in the same URL namespace). */
            char *pszPath;
#ifdef VBOX_SHCL_DEBUG_HTTPSERVER
# ifdef DEBUG_andy /** Too lazy to specify a different transfer ID for debugging. */
            ssize_t cch = RTStrAPrintf(&pszPath, "/transfer");
# else
            ssize_t cch = RTStrAPrintf(&pszPath, "/transfer%RU16", pTransfer->State.uID);
# endif
#else /* Release mode */
            ssize_t cch = RTStrAPrintf(&pszPath, "/%s/%s", SHCL_HTTPT_URL_NAMESPACE, szUuid);
#endif
            AssertReturn(cch, VERR_NO_MEMORY);

            char  *pszURI;
            size_t cchScheme;
            rc = shClTransferHttpURLCreateFromPathEx(pszPath, &pszURI, &cchScheme);
            if (RT_SUCCESS(rc))
            {
                /* For the virtual path we only keep everything after the full scheme (e.g. "http://").
                 * The virtual path always has to start with a "/". */
                if (RTStrPrintf2(pSrvTx->szPathVirtual, sizeof(pSrvTx->szPathVirtual), "%s", pszURI + cchScheme) <= 0)
                    rc = VERR_BUFFER_OVERFLOW;

                RTStrFree(pszURI);
                pszURI = NULL;
            }
            else
                rc = VERR_NO_MEMORY;

            RTStrFree(pszPath);
            pszPath = NULL;

            if (RT_SUCCESS(rc))
            {
                pSrvTx->pTransfer = pTransfer;
                pSrvTx->hObj      = NIL_SHCLOBJHANDLE;

                RTListAppend(&pSrv->lstTransfers, &pSrvTx->Node);
                pSrv->cTransfers++;

                shclTransferHttpServerSetStatusLocked(pSrv, SHCLHTTPSERVERSTATUS_TRANSFER_REGISTERED);

                LogFunc(("pTransfer=%p, idTransfer=%RU16, szPath=%s -> %RU32 transfers\n",
                         pSrvTx->pTransfer, pSrvTx->pTransfer->State.uID, pSrvTx->szPathVirtual, pSrv->cTransfers));

                LogRel2(("Shared Clipboard: Registered HTTP transfer %RU16, now %RU32 HTTP transfers total\n",
                         pTransfer->State.uID, pSrv->cTransfers));
            }
        }

    }

    if (RT_FAILURE(rc))
        RTMemFree(pSrvTx);

    shClTransferHttpServerUnlock(pSrv);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Unregisters a formerly registered Shared Clipboard transfer.
 *
 * @returns VBox status code.
 * @param   pSrv                HTTP server instance to unregister transfer from.
 * @param   pTransfer           Transfer to unregister.
 */
int ShClTransferHttpServerUnregisterTransfer(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    shClTransferHttpServerLock(pSrv);

    int rc = VINF_SUCCESS;

    PSHCLHTTPSERVERTRANSFER pSrvTx, pSrvTxNext;
    RTListForEachSafe(&pSrv->lstTransfers, pSrvTx, pSrvTxNext, SHCLHTTPSERVERTRANSFER, Node)
    {
        AssertPtr(pSrvTx->pTransfer);
        if (pSrvTx->pTransfer->State.uID == pTransfer->State.uID)
        {
            rc = shClTransferHttpServerDestroyTransfer(pSrv, pSrvTx);
            if (RT_SUCCESS(rc))
                shclTransferHttpServerSetStatusLocked(pSrv, SHCLHTTPSERVERSTATUS_TRANSFER_UNREGISTERED);
            break;
        }
    }

    shClTransferHttpServerUnlock(pSrv);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sets a new status.
 *
 * @returns New status set.
 * @param   pSrv                HTTP server instance to set status for.
 * @param   fStatus             New status to set.
 *
 * @note    Caller needs to take critical section.
 */
static SHCLHTTPSERVERSTATUS shclTransferHttpServerSetStatusLocked(PSHCLHTTPSERVER pSrv, SHCLHTTPSERVERSTATUS enmStatus)
{
    Assert(RTCritSectIsOwner(&pSrv->CritSect));

    /* Bogus checks. */
    Assert(!(enmStatus & SHCLHTTPSERVERSTATUS_NONE) || enmStatus == SHCLHTTPSERVERSTATUS_NONE);

    pSrv->enmStatus = enmStatus;
    LogFlowFunc(("fStatus=%#x\n", pSrv->enmStatus));

    int rc2 = RTSemEventSignal(pSrv->StatusEvent);
    AssertRC(rc2);

    return pSrv->enmStatus;
}

/**
 * Returns the first transfer in the list.
 *
 * @returns Pointer to first transfer if found, or NULL if not found.
 * @param   pSrv                HTTP server instance.
 */
PSHCLTRANSFER ShClTransferHttpServerGetTransferFirst(PSHCLHTTPSERVER pSrv)
{
    shClTransferHttpServerLock(pSrv);

    PSHCLHTTPSERVERTRANSFER pHttpTransfer = RTListGetFirst(&pSrv->lstTransfers, SHCLHTTPSERVERTRANSFER, Node);

    shClTransferHttpServerUnlock(pSrv);

    return pHttpTransfer ? pHttpTransfer->pTransfer : NULL;
}

/**
 * Returns the last transfer in the list.
 *
 * @returns Pointer to last transfer if found, or NULL if not found.
 * @param   pSrv                HTTP server instance.
 */
PSHCLTRANSFER ShClTransferHttpServerGetTransferLast(PSHCLHTTPSERVER pSrv)
{
    shClTransferHttpServerLock(pSrv);

    PSHCLHTTPSERVERTRANSFER pHttpTransfer = RTListGetLast(&pSrv->lstTransfers, SHCLHTTPSERVERTRANSFER, Node);

    shClTransferHttpServerUnlock(pSrv);

    return pHttpTransfer ? pHttpTransfer->pTransfer : NULL;
}

/**
 * Returns a transfer for a specific ID.
 *
 * @returns Pointer to the transfer if found, or NULL if not found.
 * @param   pSrv                HTTP server instance.
 * @param   idTransfer          Transfer ID of transfer to return..
 */
bool ShClTransferHttpServerGetTransfer(PSHCLHTTPSERVER pSrv, SHCLTRANSFERID idTransfer)
{
    AssertPtrReturn(pSrv, false);

    shClTransferHttpServerLock(pSrv);

    PSHCLHTTPSERVERTRANSFER pTransfer = shClTransferHttpServerGetTransferById(pSrv, idTransfer);

    shClTransferHttpServerUnlock(pSrv);

    return pTransfer;
}

/**
 * Returns the used TCP port number of a HTTP server instance.
 *
 * @returns TCP port number. 0 if not specified yet.
 * @param   pSrv                HTTP server instance to return port for.
 */
uint16_t ShClTransferHttpServerGetPort(PSHCLHTTPSERVER pSrv)
{
    AssertPtrReturn(pSrv, 0);

    shClTransferHttpServerLock(pSrv);

    const uint16_t uPort = pSrv->uPort;

    shClTransferHttpServerUnlock(pSrv);

    return uPort;
}

/**
 * Returns the number of registered HTTP server transfers of a HTTP server instance.
 *
 * @returns Number of registered transfers.
 * @param   pSrv                HTTP server instance to return registered transfers for.
 */
uint32_t ShClTransferHttpServerGetTransferCount(PSHCLHTTPSERVER pSrv)
{
    AssertPtrReturn(pSrv, 0);

    shClTransferHttpServerLock(pSrv);

    const uint32_t cTransfers = pSrv->cTransfers;
    LogFlowFunc(("cTransfers=%RU32\n", cTransfers));

    shClTransferHttpServerUnlock(pSrv);

    return cTransfers;
}

/**
 * Returns an allocated string with a HTTP server instance's address.
 *
 * @returns Allocated string with a HTTP server instance's address, or NULL on OOM.
 *          Needs to be free'd by the caller using RTStrFree().
 * @param   pSrv                HTTP server instance to return address for.
 */
char *ShClTransferHttpServerGetAddressA(PSHCLHTTPSERVER pSrv)
{
    AssertPtrReturn(pSrv, NULL);

    shClTransferHttpServerLock(pSrv);

    char *pszAddress = RTStrAPrintf2("%s:%RU16", shClTransferHttpServerGetHost(pSrv), pSrv->uPort);
    AssertPtr(pszAddress);

    shClTransferHttpServerUnlock(pSrv);

    return pszAddress;
}

/**
 * Returns an allocated string with the URL of a given Shared Clipboard transfer ID.
 *
 * @returns Allocated string with the URL of a given Shared Clipboard transfer ID, or NULL if not found.
 *          Needs to be free'd by the caller using RTStrFree().
 * @param   pSrv                HTTP server instance to return URL for.
 * @param   idTransfer          Transfer ID to return the URL for.
 * @param   idxEntry            Index of transfer entry to return URL for.
 *                              Specify UINT64_MAX to only return the base URL.
 */
char *ShClTransferHttpServerGetUrlA(PSHCLHTTPSERVER pSrv, SHCLTRANSFERID idTransfer, uint64_t idxEntry)
{
    AssertPtrReturn(pSrv, NULL);
    AssertReturn(idTransfer != NIL_SHCLTRANSFERID, NULL);

    shClTransferHttpServerLock(pSrv);

    PSHCLHTTPSERVERTRANSFER pSrvTx = shClTransferHttpServerGetTransferById(pSrv, idTransfer);
    if (!pSrvTx)
    {
        AssertFailed();
        shClTransferHttpServerUnlock(pSrv);
        return NULL;
    }

    PSHCLTRANSFER pTx = pSrvTx->pTransfer;
    AssertPtr(pTx);

    char *pszUrl = NULL;

    if (RT_LIKELY(idxEntry != UINT64_MAX))
    {
        /* For now this only supports root entries. */
        PCSHCLLISTENTRY pEntry = ShClTransferRootsEntryGet(pTx, idxEntry);
        if (pEntry)
        {
            AssertReturn(RTStrNLen(pSrvTx->szPathVirtual, RTPATH_MAX), NULL);
            pszUrl = RTStrAPrintf2("%s:%RU16%s/%s", shClTransferHttpServerGetHost(pSrv), pSrv->uPort, pSrvTx->szPathVirtual, pEntry->pszName);
        }
    }
    else /* Only return the base. */
        pszUrl = RTStrAPrintf2("%s:%RU16%s", shClTransferHttpServerGetHost(pSrv), pSrv->uPort, pSrvTx->szPathVirtual);

    shClTransferHttpServerUnlock(pSrv);
    return pszUrl;
}

/**
 * Converts a HTTP transfer to a string list.
 *
 * @returns VBox status code.
 * @param   pSrv                HTTP server that contains the transfer.
 * @param   pTransfer           Transfer to convert data from.
 * @param   pszSep              Separator to use for the transfer entries.
 * @param   ppszData            Where to store the string list on success.
 * @param   pcbData             Where to return the bytes of \a ppszData on success.
 *                              Includes terminator. Optional.
 */
static int shClTransferHttpConvertToStringListEx(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer, const char *pszSep,
                                                 char **ppszData, size_t *pcbData)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pszSep, VERR_INVALID_POINTER);
    AssertPtrReturn(ppszData, VERR_INVALID_POINTER);
    /* pcbData is optional. */

    int   rc      = VINF_SUCCESS;
    char *pszData = NULL;

    uint64_t const cRoots = ShClTransferRootsCount(pTransfer);
    for (uint32_t i = 0; i < cRoots; i++)
    {
        char *pszEntry = ShClTransferHttpServerGetUrlA(pSrv, ShClTransferGetID(pTransfer), i /* Entry index */);
        AssertPtrBreakStmt(pszEntry, rc = VERR_NO_MEMORY);

        if (i > 0)
        {
            rc = RTStrAAppend(&pszData, pszSep); /* Separate entries with a newline. */
            AssertRCBreak(rc);
        }

        rc = RTStrAAppend(&pszData, pszEntry);
        AssertRCBreak(rc);

        RTStrFree(pszEntry);
    }

    if (RT_FAILURE(rc))
    {
        RTStrFree(pszData);
        return rc;
    }

    *ppszData = pszData;
    if (pcbData)
        *pcbData = RTStrNLen(pszData, RTSTR_MAX) + 1 /* Terminator. */;

    return VINF_SUCCESS;
}

/**
 * Converts a HTTP transfer to a string list.
 *
 * @returns VBox status code.
 * @param   pSrv                HTTP server that contains the transfer.
 * @param   pTransfer           Transfer to convert data from.
 * @param   ppszData            Where to store the string list on success.
 * @param   pcbData             Where to return the bytes of \a ppszData on success.
 *                              Includes terminator. Optional.
 *
 * @note    Uses '\n' as the separator. @sa ShClTransferHttpConvertToStringListEx().
 */
int ShClTransferHttpConvertToStringList(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer, char **ppszData, size_t *pcbData)
{
    return shClTransferHttpConvertToStringListEx(pSrv, pTransfer, "\n", ppszData, pcbData);
}

/**
 * Returns whether a given HTTP server instance is initialized or not.
 *
 * @returns \c true if running, or \c false if not.
 * @param   pSrv                HTTP server instance to check initialized state for.
 */
bool ShClTransferHttpServerIsInitialized(PSHCLHTTPSERVER pSrv)
{
    AssertPtrReturn(pSrv, false);

    return ASMAtomicReadBool(&pSrv->fInitialized);
}

/**
 * Returns whether a given HTTP server instance is running or not.
 *
 * @returns \c true if running, or \c false if not.
 * @param   pSrv                HTTP server instance to check running state for.
 */
bool ShClTransferHttpServerIsRunning(PSHCLHTTPSERVER pSrv)
{
    AssertPtrReturn(pSrv, false);

    return ASMAtomicReadBool(&pSrv->fRunning);
}

/**
 * Waits for a server status change.
 *
 * @returns VBox status code.
 * @retval  VERR_STATE_CHANGED if the HTTP server was uninitialized.
 * @param   pSrv                HTTP server instance to wait for.
 * @param   fStatus             Status to wait for.
 *                              Multiple statuses are possible, @sa SHCLHTTPSERVERSTATUS.
 * @param   msTimeout           Timeout (in ms) to wait.
 */
int ShClTransferHttpServerWaitForStatusChange(PSHCLHTTPSERVER pSrv, SHCLHTTPSERVERSTATUS fStatus, RTMSINTERVAL msTimeout)
{
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);
    AssertMsgReturn(ASMAtomicReadBool(&pSrv->fInitialized), ("Server not initialized yet\n"), VERR_WRONG_ORDER);

    shClTransferHttpServerLock(pSrv);

    uint64_t const tsStartMs = RTTimeMilliTS();

    int rc = VERR_TIMEOUT;

    LogFlowFunc(("fStatus=%#x, msTimeout=%RU32 -- current is %#x\n", fStatus, msTimeout, pSrv->enmStatus));

    while (RTTimeMilliTS() - tsStartMs <= msTimeout)
    {
        if (!pSrv->fInitialized)
        {
            rc = VERR_STATE_CHANGED;
            break;
        }

        shClTransferHttpServerUnlock(pSrv); /* Leave lock before waiting. */

        rc = RTSemEventWait(pSrv->StatusEvent, msTimeout);

        shClTransferHttpServerLock(pSrv);

        if (RT_FAILURE(rc))
            break;

        LogFlowFunc(("Current status now is: %#x\n", pSrv->enmStatus));
        LogRel2(("Shared Clipboard: HTTP server entered status '%s'\n", shClTransferHttpServerStatusToStr(pSrv->enmStatus)));

        if (pSrv->enmStatus & fStatus)
        {
            rc = VINF_SUCCESS;
            break;
        }
    }

    shClTransferHttpServerUnlock(pSrv);

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/*********************************************************************************************************************************
*   Public Shared Clipboard HTTP context functions                                                                               *
*********************************************************************************************************************************/

/**
 * Starts the HTTP server, if not started already.
 *
 * @returns VBox status code.
 * @param   pCtx                HTTP context to start HTTP server for.
 */
int ShClTransferHttpServerMaybeStart(PSHCLHTTPCONTEXT pCtx)
{
    int rc = VINF_SUCCESS;

    LogFlowFuncEnter();

    /* Start the built-in HTTP server to serve file(s). */
    if (!ShClTransferHttpServerIsRunning(&pCtx->HttpServer)) /* Only one HTTP server per transfer context. */
        rc = ShClTransferHttpServerStart(&pCtx->HttpServer, 32 /* cMaxAttempts */, NULL /* puPort */);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Stops the HTTP server, if no running transfers are left.
 *
 * @returns VBox status code.
 * @param   pCtx                HTTP context to stop HTTP server for.
 */
int ShClTransferHttpServerMaybeStop(PSHCLHTTPCONTEXT pCtx)
{
    int rc = VINF_SUCCESS;

    LogFlowFuncEnter();

    if (ShClTransferHttpServerIsRunning(&pCtx->HttpServer))
    {
        /* No more registered transfers left? Tear down the HTTP server instance then. */
        if (ShClTransferHttpServerGetTransferCount(&pCtx->HttpServer) == 0)
            rc = ShClTransferHttpServerStop(&pCtx->HttpServer);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}
