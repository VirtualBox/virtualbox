/* $Id: clipboard-transfers-http.cpp 115109 2026-08-25 09:04:04Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard: HTTP server implementation for Shared Clipboard transfers on UNIX-y guests / hosts.
 */

/*
 * Copyright (C) 2020-2026 Oracle and/or its affiliates.
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
#include <VBox/GuestHost/clipboard-transfers-http.h>


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
    /** Pointer to associated transfer.  Held by one transfer reference while this structure exists. */
    PSHCLTRANSFER       pTransfer;
    /** Critical section protecting the request count and drain event state. */
    RTCRITSECT          CritSect;
    /** Number of references to this registration (list owner, requests and an eventual drain waiter). */
    volatile uint32_t   cRefs;
    /** Number of active HTTP requests using this registration. */
    uint32_t            cRequests;
    /** Number of advertised roots. */
    uint64_t            cRoots;
    /** Number of advertised regular-file roots. */
    uint64_t            cFileRoots;
    /** Number of regular-file roots successfully served at least once. */
    uint64_t            cFileRootsCompleted;
    /** Per-root completion state, indexed exactly like the transfer root list. */
    bool               *pafRootCompleted;
    /** Whether every advertised root is a regular file. */
    bool                fAllRootsAreFiles;
    /** Whether ShClTransferComplete() already has been issued. */
    bool                fCompletionIssued;
    /** Signaled when the active request count transitions to zero. */
    RTSEMEVENTMULTI     hRequestsDrained;
    /** Whether the registration still is visible in the server lookup list.  Protected by the server lock. */
    bool                fRegistered;
    /** Immutable identity of the registered transfer. */
    SHCLTRANSFERKEY     Key;
    /** The virtual path of the HTTP server's root directory for this transfer.
     *  Always has to start with a "/". Unescaped. */
    char                szPathVirtual[RTPATH_MAX];
} SHCLHTTPSERVERTRANSFER;
typedef SHCLHTTPSERVERTRANSFER *PSHCLHTTPSERVERTRANSFER;

/** Per-request state.  In particular, object handles must never be shared by two HTTP requests. */
typedef struct _SHCLHTTPSERVERREQUEST
{
    /** Retained registration used by this request. */
    PSHCLHTTPSERVERTRANSFER pSrvTx;
    /** Object handle opened for this request. */
    SHCLOBJHANDLE           hObj;
    /** Root index resolved for this request. */
    uint64_t                idxRoot;
    /** Advertised size of the resolved regular-file root. */
    uint64_t                cbRoot;
    /** Whether @a idxRoot and @a cbRoot describe a regular-file GET payload. */
    bool                    fRootResolved;
    /** Result of closing the provider object. */
    int                     rcClose;
    /** Whether request-end must complete the transfer after releasing this request. */
    bool                    fCompleteTransfer;
} SHCLHTTPSERVERREQUEST;
typedef SHCLHTTPSERVERREQUEST *PSHCLHTTPSERVERREQUEST;


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static int shClTransferHttpServerDestroyInternal(PSHCLHTTPSERVER pThis);
static const char *shClTransferHttpServerGetHost(PSHCLHTTPSERVER pSrv);
static int shClTransferHttpServerDestroyTransfer(PSHCLHTTPSERVER pSrv, PSHCLHTTPSERVERTRANSFER pSrvTx);
static SHCLHTTPSERVERSTATUS shclTransferHttpServerSetStatusLocked(PSHCLHTTPSERVER pSrv, SHCLHTTPSERVERSTATUS fStatus);
static void shClTransferHttpRequestCompleted(PRTHTTPSERVERREQ pReq, PSHCLHTTPSERVERREQUEST pHttpReq);


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
 * Retains an HTTP transfer registration.
 *
 * @returns New reference count.
 * @param   pSrvTx              HTTP transfer registration to retain.
 */
static uint32_t shClHttpTransferRetain(PSHCLHTTPSERVERTRANSFER pSrvTx)
{
    uint32_t const cRefs = ASMAtomicIncU32(&pSrvTx->cRefs);
    Assert(cRefs > 1 && cRefs < UINT32_MAX / 2);
    return cRefs;
}

/**
 * Releases an HTTP transfer registration and destroys it on the final release.
 *
 * @returns New reference count.
 * @param   pSrvTx              HTTP transfer registration to release.
 */
static uint32_t shClHttpTransferRelease(PSHCLHTTPSERVERTRANSFER pSrvTx)
{
    Assert(ASMAtomicReadU32(&pSrvTx->cRefs) > 0);

    uint32_t const cRefs = ASMAtomicDecU32(&pSrvTx->cRefs);
    if (cRefs == 0)
    {
        Assert(!pSrvTx->fRegistered);
        Assert(pSrvTx->cRequests == 0);

        if (RTCritSectIsInitialized(&pSrvTx->CritSect))
        {
            int rc2 = RTCritSectDelete(&pSrvTx->CritSect);
            AssertRC(rc2);
        }

        int rc2 = RTSemEventMultiDestroy(pSrvTx->hRequestsDrained);
        AssertRC(rc2);
        pSrvTx->hRequestsDrained = NIL_RTSEMEVENTMULTI;

        RTMemFree(pSrvTx->pafRootCompleted);
        pSrvTx->pafRootCompleted = NULL;

        ShClTransferRelease(pSrvTx->pTransfer);
        pSrvTx->pTransfer = NULL;

        RTMemFree(pSrvTx);
    }

    return cRefs;
}

/**
 * Adds an active request reference to a registered HTTP transfer.
 *
 * @returns VBox status code.
 * @param   pSrvTx              HTTP transfer registration to retain for a request.
 *
 * @note    The caller must own the HTTP server lock, which prevents the
 *          registration from being detached while the request is added.
 */
static int shClHttpTransferRequestRetain(PSHCLHTTPSERVERTRANSFER pSrvTx)
{
    Assert(pSrvTx->fRegistered);

    shClHttpTransferRetain(pSrvTx);
    shClHttpTransferLock(pSrvTx);

    int rc = VINF_SUCCESS;
    if (pSrvTx->cRequests == 0)
        rc = RTSemEventMultiReset(pSrvTx->hRequestsDrained);
    if (RT_SUCCESS(rc))
        pSrvTx->cRequests++;

    shClHttpTransferUnlock(pSrvTx);

    if (RT_FAILURE(rc))
        shClHttpTransferRelease(pSrvTx);
    return rc;
}

/**
 * Releases an active request reference to an HTTP transfer registration.
 *
 * @param   pSrvTx              HTTP transfer registration to release for a request.
 */
static void shClHttpTransferRequestRelease(PSHCLHTTPSERVERTRANSFER pSrvTx)
{
    shClHttpTransferLock(pSrvTx);

    Assert(pSrvTx->cRequests > 0);
    pSrvTx->cRequests--;
    if (pSrvTx->cRequests == 0)
    {
        int rc2 = RTSemEventMultiSignal(pSrvTx->hRequestsDrained);
        AssertRC(rc2);
    }

    shClHttpTransferUnlock(pSrvTx);
    shClHttpTransferRelease(pSrvTx);
}

/**
 * Waits for all requests using a detached HTTP transfer registration to end.
 *
 * @returns VBox status code.
 * @param   pSrvTx              Detached HTTP transfer registration to drain.
 */
static int shClHttpTransferDrainRequests(PSHCLHTTPSERVERTRANSFER pSrvTx)
{
    Assert(!pSrvTx->fRegistered);

    int rc = VINF_SUCCESS;
    for (;;)
    {
        shClHttpTransferLock(pSrvTx);
        uint32_t const cRequests = pSrvTx->cRequests;
        shClHttpTransferUnlock(pSrvTx);
        if (cRequests == 0)
            break;

        rc = RTSemEventMultiWait(pSrvTx->hRequestsDrained, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            break;
    }

    return rc;
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
 *
 * @note    Caller needs to take the server critical section.
 */
DECLINLINE(PSHCLHTTPSERVERTRANSFER) shClTransferHttpServerGetTransferById(PSHCLHTTPSERVER pSrv, SHCLTRANSFERID idTransfer)
{
    Assert(RTCritSectIsOwner(&pSrv->CritSect));

    PSHCLHTTPSERVERTRANSFER pSrvTx;
    RTListForEach(&pSrv->lstTransfers, pSrvTx, SHCLHTTPSERVERTRANSFER, Node) /** @todo Slow O(n) lookup, but does it for now. */
    {
        if (ShClTransferKeyGetTransferId(&pSrvTx->Key) == idTransfer)
            return pSrvTx;
    }

    return NULL;
}

/**
 * Returns the HTTP server transfer matching an exact transfer key.
 *
 * @returns Pointer to HTTP server transfer if found, NULL if not found.
 * @param   pSrv                HTTP server instance.
 * @param   pKey                Host-side transfer key to match.
 *
 * @note    Caller needs to take the server critical section.
 */
DECLINLINE(PSHCLHTTPSERVERTRANSFER)
shClTransferHttpServerGetTransferByKey(PSHCLHTTPSERVER pSrv, PCSHCLTRANSFERKEY pKey)
{
    Assert(RTCritSectIsOwner(&pSrv->CritSect));
    AssertPtrReturn(pKey, NULL);

    PSHCLHTTPSERVERTRANSFER pSrvTx;
    RTListForEach(&pSrv->lstTransfers, pSrvTx, SHCLHTTPSERVERTRANSFER, Node)
    {
        if (ShClTransferKeyIsEqual(&pSrvTx->Key, pKey))
            return pSrvTx;
    }

    return NULL;
}

/**
 * Returns a HTTP server transfer from a given URL.
 *
 * @returns Pointer to HTTP server transfer if found, NULL if not found.
 * @param   pSrv                HTTP server instance data.
 * @param   pszUrl              URL to validate.
 *
 * @note    Caller needs to take the server critical section.
 */
DECLINLINE(PSHCLHTTPSERVERTRANSFER) shClTransferHttpGetTransferFromUrl(PSHCLHTTPSERVER pSrv, const char *pszUrl)
{
    AssertPtrReturn(pszUrl, NULL);
    Assert(RTCritSectIsOwner(&pSrv->CritSect));

    PSHCLHTTPSERVERTRANSFER pSrvTx = NULL;

    PSHCLHTTPSERVERTRANSFER pSrvTxCur;
    RTListForEach(&pSrv->lstTransfers, pSrvTxCur, SHCLHTTPSERVERTRANSFER, Node)
    {
        AssertPtr(pSrvTxCur->pTransfer);

        LogFlowFunc(("pSrvTxCur=%s\n", pSrvTxCur->szPathVirtual));

        /* Be picky here, do a case sensitive comparison with a path boundary. */
        size_t const cchPathVirtual = strlen(pSrvTxCur->szPathVirtual);
        if (   !strncmp(pszUrl, pSrvTxCur->szPathVirtual, cchPathVirtual)
            && (   pszUrl[cchPathVirtual] == '\0'
                || pszUrl[cchPathVirtual] == '/'))
        {
            pSrvTx = pSrvTxCur;
            break;
        }
    }

    if (!pSrvTx)
        LogRelMax(16, ("Shared Clipboard: HTTP URL '%s' is not valid\n", pszUrl));

    LogFlowFunc(("pszUrl=%s, pSrvTx=%p\n", pszUrl, pSrvTx));
    return pSrvTx;
}

#if 0 /* unused */
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

        if (ShClTransferKeyGetTransferId(&pSrvTxCur->pTransfer->State.Key) == uHandle) /** @ŧodo We're using the transfer ID as handle for now. */
            return pSrvTxCur;
    }

    return NULL;
}
#endif


/*********************************************************************************************************************************
*   HTTP server callback implementations                                                                                         *
*********************************************************************************************************************************/

/**
 * Closes the object owned by an HTTP request, if any.
 *
 * @returns VBox status code.
 * @param   pHttpReq            HTTP request state whose object to close.
 *
 * @note    The request relinquishes the handle even if the provider reports a
 *          close error.  The transfer's final reset remains the last-resort
 *          cleanup for provider-owned object state.
 */
static int shClTransferHttpRequestClose(PSHCLHTTPSERVERREQUEST pHttpReq)
{
    if (pHttpReq->hObj == NIL_SHCLOBJHANDLE)
        return VINF_SUCCESS;

    SHCLOBJHANDLE const hObj = pHttpReq->hObj;
    pHttpReq->hObj = NIL_SHCLOBJHANDLE;

    int const rc = ShClTransferObjClose(pHttpReq->pSrvTx->pTransfer, hObj);
    pHttpReq->rcClose = rc;
    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Error closing HTTP request object (handle %RU64), rc=%Rrc\n", hObj, rc));
    return rc;
}

/** @copydoc RTHTTPSERVERCALLBACKS::pfnRequestBegin */
static DECLCALLBACK(int) shClTransferHttpBegin(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq)
{
    PSHCLHTTPSERVER pSrv = (PSHCLHTTPSERVER)pData->pvUser;
    Assert(pData->cbUser == sizeof(SHCLHTTPSERVER));

    LogRel2(("Shared Clipboard: HTTP request begin\n"));

    PSHCLHTTPSERVERREQUEST pHttpReq = (PSHCLHTTPSERVERREQUEST)RTMemAllocZ(sizeof(SHCLHTTPSERVERREQUEST));
    if (!pHttpReq)
        return VERR_NO_MEMORY;

    int rc = VERR_NOT_FOUND;

    shClTransferHttpServerLock(pSrv);

    PSHCLHTTPSERVERTRANSFER pSrvTx = NULL;
    if (pSrv->fRunning)
        pSrvTx = shClTransferHttpGetTransferFromUrl(pSrv, pReq->pszUrl);
    if (pSrvTx)
    {
        rc = shClHttpTransferRequestRetain(pSrvTx);
        if (RT_SUCCESS(rc))
        {
            pHttpReq->pSrvTx  = pSrvTx;
            pHttpReq->hObj    = NIL_SHCLOBJHANDLE;
            pHttpReq->idxRoot = UINT64_MAX;
            pHttpReq->rcClose = VINF_SUCCESS;
            pReq->pvUser      = pHttpReq;
        }
    }

    shClTransferHttpServerUnlock(pSrv);

    if (RT_FAILURE(rc))
        RTMemFree(pHttpReq);

    /* Keep request-begin lookup failures transparent to the HTTP server.  The
     * method callback maps a request without private state to HTTP 404. */
    return VINF_SUCCESS;
}

/** @copydoc RTHTTPSERVERCALLBACKS::pfnRequestEnd */
static DECLCALLBACK(int) shClTransferHttpEnd(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq)
{
    RT_NOREF(pData);
    Assert(pData->cbUser == sizeof(SHCLHTTPSERVER));

    LogRel2(("Shared Clipboard: HTTP request end\n"));

    PSHCLHTTPSERVERREQUEST pHttpReq = (PSHCLHTTPSERVERREQUEST)pReq->pvUser;
    if (pHttpReq)
    {
        shClTransferHttpRequestCompleted(pReq, pHttpReq);
        pReq->pvUser = NULL;

        int rc2 = shClTransferHttpRequestClose(pHttpReq);
        AssertRC(rc2);

        PSHCLHTTPSERVERTRANSFER const pSrvTx = pHttpReq->pSrvTx;
        bool const fCompleteTransfer = pHttpReq->fCompleteTransfer;
        if (fCompleteTransfer)
            shClHttpTransferRetain(pSrvTx);

        shClHttpTransferRequestRelease(pSrvTx);
        pHttpReq->pSrvTx = NULL;
        RTMemFree(pHttpReq);

        if (fCompleteTransfer)
        {
            rc2 = ShClTransferComplete(pSrvTx->pTransfer);
            if (RT_FAILURE(rc2))
                LogRel(("Shared Clipboard: Completing HTTP transfer %RU16 failed, rc=%Rrc\n", ShClTransferKeyGetTransferId(&pSrvTx->Key), rc2));
            shClHttpTransferRelease(pSrvTx);
        }
    }

    return VINF_SUCCESS;

}

/** @copydoc RTHTTPSERVERCALLBACKS::pfnOpen */
static DECLCALLBACK(int) shClTransferHttpOpen(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void **ppvHandle)
{
    RT_NOREF(pData);
    Assert(pData->cbUser == sizeof(SHCLHTTPSERVER));

    int rc;

    AssertPtr(pReq->pvUser);
    PSHCLHTTPSERVERREQUEST pHttpReq = (PSHCLHTTPSERVERREQUEST)pReq->pvUser;
    if (pHttpReq)
    {
        LogRel2(("Shared Clipboard: HTTP transfer (handle %RU64) started ...\n", pHttpReq->hObj));

        if (pHttpReq->hObj != NIL_SHCLOBJHANDLE)
        {
            *ppvHandle = pHttpReq;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NOT_FOUND;
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
    PSHCLHTTPSERVERREQUEST pHttpReq = (PSHCLHTTPSERVERREQUEST)pvHandle;
    if (   pHttpReq
        && pReq->pvUser == pHttpReq)
    {
        if (pHttpReq->hObj != NIL_SHCLOBJHANDLE)
        {
            uint32_t cbRead;
            rc = ShClTransferObjRead(pHttpReq->pSrvTx->pTransfer, pHttpReq->hObj,
                                     pvBuf, (uint32_t)cbBuf, 0 /* fFlags */, &cbRead);
            if (RT_SUCCESS(rc))
                *pcbRead = (uint32_t)cbRead;

            if (RT_FAILURE(rc))
                LogRel(("Shared Clipboard: Error reading HTTP transfer (handle %RU64), rc=%Rrc\n", pHttpReq->hObj, rc));
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
    PSHCLHTTPSERVERREQUEST pHttpReq = (PSHCLHTTPSERVERREQUEST)pvHandle;
    if (   pHttpReq
        && pReq->pvUser == pHttpReq)
    {
        if (pHttpReq->hObj != NIL_SHCLOBJHANDLE)
        {
            SHCLOBJHANDLE const hObj = pHttpReq->hObj;
            rc = shClTransferHttpRequestClose(pHttpReq);
            if (RT_SUCCESS(rc))
                LogRel2(("Shared Clipboard: HTTP transfer %RU16 done\n", ShClTransferKeyGetTransferId(&pHttpReq->pSrvTx->Key)));
            else
                LogRel(("Shared Clipboard: Error closing HTTP transfer (handle %RU64), rc=%Rrc\n", hObj, rc));
        }
        else
            rc = VERR_NOT_FOUND;
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Commits a successfully delivered GET to its advertised root.
 *
 * @param   pReq                Completed HTTP request.
 * @param   pHttpReq            Shared Clipboard request state.
 */
static void shClTransferHttpRequestCompleted(PRTHTTPSERVERREQ pReq, PSHCLHTTPSERVERREQUEST pHttpReq)
{
    RTHTTPSERVERREQRESULT Result;
    int const rc = RTHttpServerRequestQueryResult(pReq, &Result);
    AssertRCReturnVoid(rc);

    if (   pReq->enmMethod != RTHTTPMETHOD_GET
        || RT_FAILURE(Result.rcRequest)
        || !(Result.fFlags & RTHTTPSERVERREQRESULT_F_BODY_COMPLETE))
        return;

    if (   !pHttpReq->fRootResolved
        || RT_FAILURE(pHttpReq->rcClose)
        || Result.cbBodySent != pHttpReq->cbRoot)
        return;

    PSHCLHTTPSERVERTRANSFER pSrvTx = pHttpReq->pSrvTx;
    AssertPtrReturnVoid(pSrvTx);
    AssertPtrReturnVoid(pSrvTx->pafRootCompleted);

    shClHttpTransferLock(pSrvTx);

    if (   pHttpReq->idxRoot < pSrvTx->cRoots
        && !pSrvTx->pafRootCompleted[pHttpReq->idxRoot])
    {
        pSrvTx->pafRootCompleted[pHttpReq->idxRoot] = true;
        pSrvTx->cFileRootsCompleted++;
        Assert(pSrvTx->cFileRootsCompleted <= pSrvTx->cFileRoots);

        if (   pSrvTx->fAllRootsAreFiles
            && pSrvTx->cFileRootsCompleted == pSrvTx->cFileRoots
            && !pSrvTx->fCompletionIssued)
        {
            pSrvTx->fCompletionIssued = true;
            pHttpReq->fCompleteTransfer = true;
        }
    }

    shClHttpTransferUnlock(pSrvTx);
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
        char *pszParsedPath = RTUriParsedPath(pszUrl, &Parsed);
        if (pszParsedPath)
        {
            size_t const cchParsedPath = strlen(pszParsedPath);

            /* For now we only know the transfer -- now we need to figure out the entry we want to serve. */
            PSHCLHTTPSERVERREQUEST pHttpReq = (PSHCLHTTPSERVERREQUEST)pReq->pvUser;
            if (pHttpReq)
            {
                PSHCLHTTPSERVERTRANSFER pSrvTx = pHttpReq->pSrvTx;
                size_t const cchVirtual = strlen(pSrvTx->szPathVirtual);
                size_t const cchRoot = cchVirtual + 1 /* Skip slash separating the base from the rest */;
                const char *pszRoot = NULL;
                if (   cchParsedPath >= cchRoot
                    && pszParsedPath[cchVirtual] == '/')
                {
                    pszRoot = pszParsedPath + cchRoot; /* Marks the actual root path. */
                    if (*pszRoot == '\0')
                        rc = VERR_INVALID_PARAMETER;
                }
                else
                    rc = VERR_INVALID_PARAMETER;

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
                        for (uint64_t i = 0; i < cRoots; i++)
                        {
                            PCSHCLLISTENTRY pEntry = ShClTransferRootsEntryGet(pTx, i);
                            AssertPtrBreakStmt(pEntry, rc = VERR_NOT_FOUND);

                            Log3Func(("pszRoot=%s vs. pEntry=%s\n", pszRoot, pEntry->pszName));

                            if (RTStrCmp(pszRoot, pEntry->pszName)) /* Case-sensitive! */
                                continue;

                            rc = RTStrCopy(openParms.pszPath, openParms.cbPath, pEntry->pszName);
                            if (RT_SUCCESS(rc))
                            {
                                Assert(pHttpReq->hObj == NIL_SHCLOBJHANDLE);
                                rc = ShClTransferObjOpen(pTx, &openParms, &pHttpReq->hObj);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = VERR_NOT_SUPPORTED; /* Play safe by default. */

                                    if (   pEntry->fInfo & VBOX_SHCL_INFO_F_FSOBJINFO
                                        && pEntry->cbInfo == sizeof(SHCLFSOBJINFO)
                                        && pEntry->pvInfo)
                                    {
                                        PCSHCLFSOBJINFO pSrcObjInfo = (PSHCLFSOBJINFO)pEntry->pvInfo;

                                        LogFlowFunc(("pszName=%s, cbInfo=%RU32, fMode=%#x (type %#x)\n",
                                                     pEntry->pszName, pEntry->cbInfo, pSrcObjInfo->Attr.fMode, (pSrcObjInfo->Attr.fMode & RTFS_TYPE_MASK)));

                                        LogRel2(("Shared Clipboard: HTTP object info: fMode=%#x, cbObject=%zu\n", pSrcObjInfo->Attr.fMode, pSrcObjInfo->cbObject));

                                        if (RTFS_IS_FILE(pSrcObjInfo->Attr.fMode))
                                        {
                                            if (pSrcObjInfo->cbObject >= 0)
                                            {
                                                memcpy(pObjInfo, pSrcObjInfo, sizeof(SHCLFSOBJINFO));
                                                pHttpReq->idxRoot       = i;
                                                pHttpReq->cbRoot        = (uint64_t)pSrcObjInfo->cbObject;
                                                pHttpReq->fRootResolved = true;
                                                rc = VINF_SUCCESS;
                                            }
                                            else
                                                rc = VERR_OUT_OF_RANGE;
                                        }
                                        else
                                            rc = VERR_NOT_SUPPORTED;
                                    }
                                    else
                                        LogRelMax(16, ("Shared Clipboard: Supplied entry information for '%s' is not supported (fInfo=%#x, cbInfo=%RU32)\n",
                                                 pEntry->pszName, pEntry->fInfo, pEntry->cbInfo));
                                    /* Note: Directories / symlinks or other fancy stuff is not supported here (yet) -- would require using WebDAV. */
                                    if (   RT_FAILURE(rc)
                                        && pHttpReq->hObj != NIL_SHCLOBJHANDLE)
                                    {
                                        int rc2 = shClTransferHttpRequestClose(pHttpReq);
                                        AssertRC(rc2);
                                    }
                                }
                                else if (   rc == VERR_NOT_A_FILE
                                         || rc == VERR_IS_A_DIRECTORY
                                         || rc == VERR_IS_A_SYMLINK)
                                    rc = VERR_NOT_SUPPORTED;
                            }

                            break;
                        }

                        ShClTransferObjOpenParmsDestroy(&openParms);
                    }
                }

                if (   pReq->enmMethod == RTHTTPMETHOD_HEAD
                    && pHttpReq->hObj != NIL_SHCLOBJHANDLE)
                {
                    int rc2 = shClTransferHttpRequestClose(pHttpReq);
                    AssertRC(rc2);
                }
            }
            else
                rc = VERR_NOT_FOUND;

            RTStrFree(pszParsedPath);
            pszParsedPath = NULL;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_INVALID_PARAMETER;

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

    ASMAtomicXchgBool(&pSrv->fInitialized, false);
    ASMAtomicXchgBool(&pSrv->fRunning, false);
    pSrv->fStopping = false;

    int rc = VINF_SUCCESS;

    while (!RTListIsEmpty(&pSrv->lstTransfers))
    {
        PSHCLHTTPSERVERTRANSFER pSrvTx = RTListGetFirst(&pSrv->lstTransfers, SHCLHTTPSERVERTRANSFER, Node);
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
    ASMAtomicXchgBool(&pSrv->fInitialized, false);
    ASMAtomicXchgBool(&pSrv->fRunning, false);
    pSrv->fStopping   = false;
    pSrv->hHTTPServer = NIL_RTHTTPSERVER;

    int rc = RTCritSectInit(&pSrv->CritSect);
    AssertRCReturn(rc, rc);

    pSrv->uPort       = 0;
    RTListInit(&pSrv->lstTransfers);
    pSrv->cTransfers  = 0;
    pSrv->cDownloaded = 0;
    pSrv->enmStatus   = SHCLHTTPSERVERSTATUS_NONE;

    rc = RTHttpServerResponseInit(&pSrv->Resp);
    if (RT_FAILURE(rc))
    {
        RTCritSectDelete(&pSrv->CritSect);
        return rc;
    }

    ASMAtomicXchgBool(&pSrv->fInitialized, true);
    pSrv->fStopping = false;

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
    AssertReturn(ASMAtomicReadBool(&pSrv->fInitialized), VERR_WRONG_ORDER);

    AssertReturn(!shClTransferHttpServerPortIsBuggy(uPort), VERR_ADDRESS_CONFLICT);

    shClTransferHttpServerLock(pSrv);

    if (   pSrv->fRunning
        || pSrv->fStopping
        || pSrv->hHTTPServer != NIL_RTHTTPSERVER)
    {
        shClTransferHttpServerUnlock(pSrv);
        return VERR_WRONG_ORDER;
    }

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
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    if (!ASMAtomicReadBool(&pSrv->fInitialized))
        return VINF_SUCCESS;

    shClTransferHttpServerLock(pSrv);

    int rc = VINF_SUCCESS;
    RTHTTPSERVER hHTTPServer = NIL_RTHTTPSERVER;

    if (pSrv->fStopping)
        rc = VERR_WRONG_ORDER;
    else if (pSrv->fRunning)
    {
        Assert(pSrv->hHTTPServer != NIL_RTHTTPSERVER);

        hHTTPServer    = pSrv->hHTTPServer;
        pSrv->fRunning = false;
        pSrv->fStopping = true;
    }

    shClTransferHttpServerUnlock(pSrv);

    if (hHTTPServer != NIL_RTHTTPSERVER)
    {
        /* Do not hold the server lock while stopping worker callbacks.  Request
         * begin needs that lock in order to take a registration reference. */
        rc = RTHttpServerDestroy(hHTTPServer);

        shClTransferHttpServerLock(pSrv);

        pSrv->fStopping = false;
        if (RT_SUCCESS(rc))
        {
            pSrv->hHTTPServer = NIL_RTHTTPSERVER;

            /* Let any eventual waiters know. */
            shclTransferHttpServerSetStatusLocked(pSrv, SHCLHTTPSERVERSTATUS_STOPPED);

            LogRel2(("Shared Clipboard: HTTP server stopped\n"));
        }
        else
            pSrv->fRunning = true;

        shClTransferHttpServerUnlock(pSrv);
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: HTTP server failed to stop, rc=%Rrc\n", rc));

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

    if (!ASMAtomicReadBool(&pSrv->fInitialized))
        return VINF_SUCCESS;

    int rc = ShClTransferHttpServerStop(pSrv);
    if (RT_FAILURE(rc))
        return rc;

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
 *                              The pointer must not be used after return.
 *
 * @note    Caller needs to take the server critical section.  This function
 *          temporarily releases it while synchronously draining requests and
 *          owns it again on return.
 */
static int shClTransferHttpServerDestroyTransfer(PSHCLHTTPSERVER pSrv, PSHCLHTTPSERVERTRANSFER pSrvTx)
{
    Assert(RTCritSectIsOwner(&pSrv->CritSect));
    Assert(pSrvTx->fRegistered);

    /* Keep the registration alive for this drain operation before dropping
     * the list-owner reference below. */
    shClHttpTransferRetain(pSrvTx);

    pSrvTx->fRegistered = false;
    RTListNodeRemove(&pSrvTx->Node);

    Assert(pSrv->cTransfers);
    pSrv->cTransfers--;

    LogFunc(("pTransfer=%p, idSession=%RU16, idTransfer=%RU16, uGeneration=%RU64, szPath=%s -> %RU32 transfers\n",
             pSrvTx->pTransfer, ShClTransferKeyGetSessionId(&pSrvTx->Key), ShClTransferKeyGetTransferId(&pSrvTx->Key),
             pSrvTx->Key.uGeneration, pSrvTx->szPathVirtual, pSrv->cTransfers));

    LogRel2(("Shared Clipboard: Destroyed HTTP transfer %RU16, now %RU32 HTTP transfers total\n",
             ShClTransferKeyGetTransferId(&pSrvTx->Key), pSrv->cTransfers));

    /* Drop the list owner only after the registration is unreachable to new
     * requests.  Existing requests keep their own references. */
    shClHttpTransferRelease(pSrvTx);

    shClTransferHttpServerUnlock(pSrv);

    int rc = shClHttpTransferDrainRequests(pSrvTx);
    shClHttpTransferRelease(pSrvTx); /* Drain waiter reference. */

    shClTransferHttpServerLock(pSrv);
    return rc;
}


/*********************************************************************************************************************************
*   Public Shared Clipboard HTTP server functions                                                                                *
*********************************************************************************************************************************/

/**
 * Registers a Shared Clipboard transfer to a HTTP server instance.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_EXISTS if the exact transfer key already is registered.
 * @param   pSrv                HTTP server instance to register transfer for.
 * @param   pTransfer           Transfer to register. Needs to be on the heap.
 */
int ShClTransferHttpServerRegisterTransfer(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertReturn(ASMAtomicReadBool(&pSrv->fInitialized), VERR_WRONG_ORDER);

    SHCLTRANSFERKEY Key;
    ShClTransferGetKey(pTransfer, &Key);
    AssertMsgReturn(ShClTransferKeyIsValid(&Key),
                    ("Transfer needs a valid session/ID/generation key before HTTP registration\n"), VERR_INVALID_PARAMETER);
    SHCLTRANSFERID const idTransfer = ShClTransferKeyGetTransferId(&Key);

    uint64_t const cRoots = ShClTransferRootsCount(pTransfer);
    AssertMsgReturn(cRoots  > 0, ("Transfer has no root entries\n"), VERR_INVALID_PARAMETER);
    AssertReturn(cRoots <= (uint64_t)SIZE_MAX / sizeof(bool), VERR_OUT_OF_RANGE);

    uint64_t cFileRoots = 0;
    bool fAllRootsAreFiles = true;
    for (uint64_t i = 0; i < cRoots; i++)
    {
        PCSHCLLISTENTRY const pEntry = ShClTransferRootsEntryGet(pTransfer, i);
        if (   pEntry
            && (pEntry->fInfo & VBOX_SHCL_INFO_F_FSOBJINFO)
            && pEntry->cbInfo == sizeof(SHCLFSOBJINFO)
            && pEntry->pvInfo
            && RTFS_IS_FILE(((PCSHCLFSOBJINFO)pEntry->pvInfo)->Attr.fMode)
            && ((PCSHCLFSOBJINFO)pEntry->pvInfo)->cbObject >= 0)
            cFileRoots++;
        else
            fAllRootsAreFiles = false;
    }

    bool *pafRootCompleted = (bool *)RTMemAllocZ((size_t)cRoots * sizeof(bool));
    if (!pafRootCompleted)
        return VERR_NO_MEMORY;

    shClTransferHttpServerLock(pSrv);

    PSHCLHTTPSERVERTRANSFER pSrvTx = NULL;
    bool fCritSectInitialized = false;
    bool fDrainEventCreated   = false;
    int rc = VINF_SUCCESS;

    if (shClTransferHttpServerGetTransferByKey(pSrv, &Key))
        rc = VERR_ALREADY_EXISTS;
    else
    {
        pSrvTx = (PSHCLHTTPSERVERTRANSFER)RTMemAllocZ(sizeof(SHCLHTTPSERVERTRANSFER));
        if (pSrvTx)
        {
            RTUUID Uuid;
            rc = RTUuidCreate(&Uuid);
            if (RT_SUCCESS(rc))
            {
                char szUuid[64];
                rc = RTUuidToStr(&Uuid, szUuid, sizeof(szUuid));
                if (RT_SUCCESS(rc))
                {
                    rc = RTCritSectInit(&pSrvTx->CritSect);
                    if (RT_SUCCESS(rc))
                    {
                        fCritSectInitialized = true;

                        rc = RTSemEventMultiCreate(&pSrvTx->hRequestsDrained);
                        if (RT_SUCCESS(rc))
                            fDrainEventCreated = true;

                        /* Create the virtual HTTP path for the transfer.
                         * Every transfer has a dedicated HTTP path (but live in the same URL namespace). */
                        char   *pszPath = NULL;
                        ssize_t cch     = -1;
                        if (RT_SUCCESS(rc))
                        {
#ifdef VBOX_SHCL_DEBUG_HTTPSERVER
# ifdef DEBUG_andy /** Too lazy to specify a different transfer ID for debugging. */
                            cch = RTStrAPrintf(&pszPath, "/transfer");
# else
                            cch = RTStrAPrintf(&pszPath, "/transfer%RU16", idTransfer);
# endif
#else /* Release mode */
                            cch = RTStrAPrintf(&pszPath, "/%s/%s", SHCL_HTTPT_URL_NAMESPACE, szUuid);
#endif
                        }
                        if (cch >= 0)
                        {
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

                        }
                        else if (RT_SUCCESS(rc))
                            rc = VERR_NO_MEMORY;
                        RTStrFree(pszPath);
                        pszPath = NULL;

                        if (RT_SUCCESS(rc))
                        {
                            pSrvTx->pTransfer           = pTransfer;
                            pSrvTx->cRefs               = 1; /* Registration list owner. */
                            pSrvTx->cRequests           = 0;
                            pSrvTx->cRoots              = cRoots;
                            pSrvTx->cFileRoots          = cFileRoots;
                            pSrvTx->pafRootCompleted    = pafRootCompleted;
                            pSrvTx->fAllRootsAreFiles   = fAllRootsAreFiles;
                            pSrvTx->fRegistered         = true;
                            pSrvTx->Key                 = Key;

                            ShClTransferAcquire(pTransfer);

                            RTListAppend(&pSrv->lstTransfers, &pSrvTx->Node);
                            pSrv->cTransfers++;

                            shclTransferHttpServerSetStatusLocked(pSrv, SHCLHTTPSERVERSTATUS_TRANSFER_REGISTERED);

                            LogFunc(("pTransfer=%p, idSession=%RU16, idTransfer=%RU16, uGeneration=%RU64, szPath=%s -> %RU32 transfers\n",
                                     pSrvTx->pTransfer, ShClTransferKeyGetSessionId(&pSrvTx->Key),
                                     ShClTransferKeyGetTransferId(&pSrvTx->Key), pSrvTx->Key.uGeneration,
                                     pSrvTx->szPathVirtual, pSrv->cTransfers));

                            LogRel2(("Shared Clipboard: Registered HTTP transfer %RU16, now %RU32 HTTP transfers total\n",
                                     idTransfer, pSrv->cTransfers));

                            pSrvTx = NULL;
                            pafRootCompleted = NULL;
                            fCritSectInitialized = false;
                            fDrainEventCreated   = false;
                        }
                    }
                }
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (pSrvTx)
    {
        if (fDrainEventCreated)
        {
            int rc2 = RTSemEventMultiDestroy(pSrvTx->hRequestsDrained);
            AssertRC(rc2);
            pSrvTx->hRequestsDrained = NIL_RTSEMEVENTMULTI;
        }
        if (fCritSectInitialized)
        {
            int rc2 = RTCritSectDelete(&pSrvTx->CritSect);
            AssertRC(rc2);
        }
        RTMemFree(pSrvTx);
    }

    shClTransferHttpServerUnlock(pSrv);

    RTMemFree(pafRootCompleted);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Unregisters a formerly registered Shared Clipboard transfer.
 *
 * @returns VBox status code.
 * @param   pSrv                HTTP server instance to unregister transfer from.
 * @param   pTransfer           Transfer to unregister.
 *
 * @note    Removes all registrations matching the exact session/ID/generation
 *          key to recover from stale duplicate entries without disturbing a
 *          newer generation which happens to reuse the same transfer ID.
 *          The call synchronously drains active requests before returning so
 *          the caller may destroy the transfer immediately afterwards.
 */
int ShClTransferHttpServerUnregisterTransfer(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pSrv, VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertReturn(ASMAtomicReadBool(&pSrv->fInitialized), VERR_WRONG_ORDER);

    SHCLTRANSFERKEY Key;
    ShClTransferGetKey(pTransfer, &Key);
    AssertReturn(ShClTransferKeyIsValid(&Key), VERR_INVALID_PARAMETER);

    shClTransferHttpServerLock(pSrv);

    int rc = VINF_SUCCESS;

    PSHCLHTTPSERVERTRANSFER pSrvTx;
    while ((pSrvTx = shClTransferHttpServerGetTransferByKey(pSrv, &Key)) != NULL)
    {
        rc = shClTransferHttpServerDestroyTransfer(pSrv, pSrvTx);
        if (RT_SUCCESS(rc))
            shclTransferHttpServerSetStatusLocked(pSrv, SHCLHTTPSERVERSTATUS_TRANSFER_UNREGISTERED);
        else
            break;
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

    return pSrv->enmStatus;
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

    return pTransfer != NULL;
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
 * Returns an allocated URL for a locked HTTP transfer registration.
 *
 * @returns Allocated URL, or NULL if the entry does not exist or allocation failed.
 * @param   pSrv                HTTP server instance.
 * @param   pSrvTx              HTTP transfer registration.
 * @param   idxEntry            Root entry index, or UINT64_MAX for the base URL.
 *
 * @note    Caller needs to take the server critical section.
 */
static char *shClTransferHttpServerGetUrlLocked(PSHCLHTTPSERVER pSrv, PSHCLHTTPSERVERTRANSFER pSrvTx, uint64_t idxEntry)
{
    Assert(RTCritSectIsOwner(&pSrv->CritSect));
    AssertPtrReturn(pSrvTx, NULL);

    char *pszUrl = NULL;

    if (RT_LIKELY(idxEntry != UINT64_MAX))
    {
        /* For now this only supports root entries. */
        PCSHCLLISTENTRY pEntry = ShClTransferRootsEntryGet(pSrvTx->pTransfer, idxEntry);
        if (   pEntry
            && RTStrNLen(pSrvTx->szPathVirtual, RTPATH_MAX))
            pszUrl = RTStrAPrintf2("%s:%RU16%s/%RMpp", shClTransferHttpServerGetHost(pSrv), pSrv->uPort,
                                   pSrvTx->szPathVirtual, pEntry->pszName);
    }
    else /* Only return the base. */
        pszUrl = RTStrAPrintf2("%s:%RU16%s", shClTransferHttpServerGetHost(pSrv), pSrv->uPort, pSrvTx->szPathVirtual);

    return pszUrl;
}

/**
 * Returns an allocated URL for an exact Shared Clipboard transfer key.
 *
 * @returns Allocated URL, or NULL if the registration or entry was not found.
 * @param   pSrv                HTTP server instance.
 * @param   pTransfer           Transfer whose exact registration to use.
 * @param   idxEntry            Root entry index, or UINT64_MAX for the base URL.
 */
static char *shClTransferHttpServerGetUrlForTransferA(PSHCLHTTPSERVER pSrv, PSHCLTRANSFER pTransfer, uint64_t idxEntry)
{
    SHCLTRANSFERKEY Key;
    ShClTransferGetKey(pTransfer, &Key);
    AssertReturn(ShClTransferKeyIsValid(&Key), NULL);

    shClTransferHttpServerLock(pSrv);

    PSHCLHTTPSERVERTRANSFER pSrvTx = shClTransferHttpServerGetTransferByKey(pSrv, &Key);
    char *pszUrl = pSrvTx ? shClTransferHttpServerGetUrlLocked(pSrv, pSrvTx, idxEntry) : NULL;

    shClTransferHttpServerUnlock(pSrv);
    return pszUrl;
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

    char *pszUrl = shClTransferHttpServerGetUrlLocked(pSrv, pSrvTx, idxEntry);

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
        char *pszEntry = shClTransferHttpServerGetUrlForTransferA(pSrv, pTransfer, i /* Entry index */);
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
