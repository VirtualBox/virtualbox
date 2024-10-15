/* $Id: http-server.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Simple HTTP server (RFC 7231) implementation.
 *
 * Known limitations so far:
 * - Only HTTP 1.1.
 * - Only supports GET + HEAD methods so far.
 * - Only supports UTF-8 charset.
 * - Only supports plain text and octet stream MIME types.
 * - No content compression ("gzip", "x-gzip", ++).
 * - No caching.
 * - No redirections (via 302).
 * - No encryption (TLS).
 * - No IPv6 support.
 * - No multi-threading.
 *
 * For WebDAV (optional via IPRT_HTTP_WITH_WEBDAV):
 * - Only OPTIONS + PROPLIST methods are implemented (e.g. simple read-only support).
 * - No pagination support for directory listings.
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
#define LOG_GROUP RTLOGGROUP_HTTP
#include <iprt/http.h>
#include <iprt/http-server.h>
#include "internal/iprt.h"
#include "internal/magics.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/circbuf.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h> /* For file mode flags. */
#include <iprt/getopt.h>
#include <iprt/mem.h>
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/poll.h>
#include <iprt/socket.h>
#include <iprt/sort.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/tcp.h>

#define LOG_GROUP RTLOGGROUP_HTTP
#include <iprt/log.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal HTTP server instance.
 */
typedef struct RTHTTPSERVERINTERNAL
{
    /** Magic value. */
    uint32_t                u32Magic;
    /** Callback table. */
    RTHTTPSERVERCALLBACKS   Callbacks;
    /** Pointer to TCP server instance. */
    PRTTCPSERVER            pTCPServer;
    /** Pointer to user-specific data. Optional. */
    void                   *pvUser;
    /** Size of user-specific data. Optional. */
    size_t                  cbUser;
} RTHTTPSERVERINTERNAL;
/** Pointer to an internal HTTP server instance. */
typedef RTHTTPSERVERINTERNAL *PRTHTTPSERVERINTERNAL;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTHTTPSERVER_VALID_RETURN_RC(hHttpServer, a_rc) \
    do { \
        AssertPtrReturn((hHttpServer), (a_rc)); \
        AssertReturn((hHttpServer)->u32Magic == RTHTTPSERVER_MAGIC, (a_rc)); \
    } while (0)

/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTHTTPSERVER_VALID_RETURN(hHttpServer) RTHTTPSERVER_VALID_RETURN_RC((hHttpServer), VERR_INVALID_HANDLE)

/** Validates a handle and returns (void) if not valid. */
#define RTHTTPSERVER_VALID_RETURN_VOID(hHttpServer) \
    do { \
        AssertPtrReturnVoid(hHttpServer); \
        AssertReturnVoid((hHttpServer)->u32Magic == RTHTTPSERVER_MAGIC); \
    } while (0)


/** Handles a HTTP server callback with no arguments and returns. */
#define RTHTTPSERVER_HANDLE_CALLBACK_RET(a_Name) \
    do \
    { \
        PRTHTTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTHTTPCALLBACKDATA Data = { &pClient->State }; \
            return pCallbacks->a_Name(&Data); \
        } \
        return VERR_NOT_IMPLEMENTED; \
    } while (0)

/** Handles a HTTP server callback with no arguments and sets rc accordingly. */
#define RTHTTPSERVER_HANDLE_CALLBACK(a_Name) \
    do \
    { \
        PRTHTTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTHTTPCALLBACKDATA Data = { &pClient->State, pClient->pServer->pvUser, pClient->pServer->cbUser }; \
            rc = pCallbacks->a_Name(&Data); \
        } \
    } while (0)

/** Handles a HTTP server callback with arguments and sets rc accordingly. */
#define RTHTTPSERVER_HANDLE_CALLBACK_VA(a_Name, ...) \
    do \
    { \
        PRTHTTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTHTTPCALLBACKDATA Data = { &pClient->State, pClient->pServer->pvUser, pClient->pServer->cbUser }; \
            rc = pCallbacks->a_Name(&Data, __VA_ARGS__); \
        } \
    } while (0)

/** Handles a HTTP server callback with arguments and returns. */
#define RTHTTPSERVER_HANDLE_CALLBACK_VA_RET(a_Name, ...) \
    do \
    { \
        PRTHTTPSERVERCALLBACKS pCallbacks = &pClient->pServer->Callbacks; \
        if (pCallbacks->a_Name) \
        { \
            RTHTTPCALLBACKDATA Data = { &pClient->State, pClient->pServer->pvUser, pClient->pServer->cbUser }; \
            return pCallbacks->a_Name(&Data, __VA_ARGS__); \
        } \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Structure for maintaining an internal HTTP server client.
 */
typedef struct RTHTTPSERVERCLIENT
{
    /** Pointer to internal server state. */
    PRTHTTPSERVERINTERNAL       pServer;
    /** Socket handle the client is bound to. */
    RTSOCKET                    hSocket;
    /** Actual client state. */
    RTHTTPSERVERCLIENTSTATE     State;
} RTHTTPSERVERCLIENT;
/** Pointer to an internal HTTP server client state. */
typedef RTHTTPSERVERCLIENT *PRTHTTPSERVERCLIENT;

/** Function pointer declaration for a specific HTTP server method handler. */
typedef DECLCALLBACKTYPE(int, FNRTHTTPSERVERMETHOD,(PRTHTTPSERVERCLIENT pClient, PRTHTTPSERVERREQ pReq));
/** Pointer to a FNRTHTTPSERVERMETHOD(). */
typedef FNRTHTTPSERVERMETHOD *PFNRTHTTPSERVERMETHOD;

/**
 * Static lookup table for some file extensions <-> MIME type. Add more as needed.
 * Keep this alphabetical (file extension).
 */
static const struct
{
    /** File extension. */
    const char *pszExt;
    /** MIME type. */
    const char *pszMIMEType;
} s_aFileExtMIMEType[] = {
    { ".arj",     "application/x-arj-compressed" },
    { ".asf",     "video/x-ms-asf" },
    { ".avi",     "video/x-msvideo" },
    { ".bmp",     "image/bmp" },
    { ".css",     "text/css" },
    { ".doc",     "application/msword" },
    { ".exe",     "application/octet-stream" },
    { ".gif",     "image/gif" },
    { ".gz",      "application/x-gunzip" },
    { ".htm",     "text/html" },
    { ".html",    "text/html" },
    { ".ico",     "image/x-icon" },
    { ".js",      "application/x-javascript" },
    { ".json",    "text/json" },
    { ".jpg",     "image/jpeg" },
    { ".jpeg",    "image/jpeg" },
    { ".ogg",     "application/ogg" },
    { ".m3u",     "audio/x-mpegurl" },
    { ".m4v",     "video/x-m4v" },
    { ".mid",     "audio/mid" },
    { ".mov",     "video/quicktime" },
    { ".mp3",     "audio/x-mp3" },
    { ".mp4",     "video/mp4" },
    { ".mpg",     "video/mpeg" },
    { ".mpeg",    "video/mpeg" },
    { ".pdf",     "application/pdf" },
    { ".png",     "image/png" },
    { ".ra",      "audio/x-pn-realaudio" },
    { ".ram",     "audio/x-pn-realaudio" },
    { ".rar",     "application/x-arj-compressed" },
    { ".rtf",     "application/rtf" },
    { ".shtm",    "text/html" },
    { ".shtml",   "text/html" },
    { ".svg",     "image/svg+xml" },
    { ".swf",     "application/x-shockwave-flash" },
    { ".torrent", "application/x-bittorrent" },
    { ".tar",     "application/x-tar" },
    { ".tgz",     "application/x-tar-gz" },
    { ".ttf",     "application/x-font-ttf" },
    { ".txt",     "text/plain" },
    { ".wav",     "audio/x-wav" },
    { ".webm",    "video/webm" },
    { ".xml",     "text/xml" },
    { ".xls",     "application/excel" },
    { ".xsl",     "application/xml" },
    { ".xslt",    "application/xml" },
    { ".zip",     "application/x-zip-compressed" },
    { NULL,       NULL }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/** @name Method handlers.
 * @{
 */
static FNRTHTTPSERVERMETHOD  rtHttpServerHandleGET;
static FNRTHTTPSERVERMETHOD  rtHttpServerHandleHEAD;
#ifdef IPRT_HTTP_WITH_WEBDAV
 static FNRTHTTPSERVERMETHOD rtHttpServerHandleOPTIONS;
 static FNRTHTTPSERVERMETHOD rtHttpServerHandlePROPFIND;
#endif
/** @} */

/**
 * Structure for maintaining a single method entry for the methods table.
 */
typedef struct RTHTTPSERVERMETHOD_ENTRY
{
    /** Method ID. */
    RTHTTPMETHOD          enmMethod;
    /** Function pointer invoked to handle the command. */
    PFNRTHTTPSERVERMETHOD pfnMethod;
} RTHTTPSERVERMETHOD_ENTRY;
/** Pointer to a command entry. */
typedef RTHTTPSERVERMETHOD_ENTRY *PRTHTTPMETHOD_ENTRY;



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Table of handled methods.
 */
static const RTHTTPSERVERMETHOD_ENTRY g_aMethodMap[] =
{
    { RTHTTPMETHOD_GET,      rtHttpServerHandleGET },
    { RTHTTPMETHOD_HEAD,     rtHttpServerHandleHEAD },
#ifdef IPRT_HTTP_WITH_WEBDAV
    { RTHTTPMETHOD_OPTIONS,  rtHttpServerHandleOPTIONS },
    { RTHTTPMETHOD_PROPFIND, rtHttpServerHandlePROPFIND },
#endif
    { RTHTTPMETHOD_END,  NULL }
};

/** Maximum length in characters a HTTP server path can have (excluding termination). */
#define RTHTTPSERVER_MAX_PATH        RTPATH_MAX


/*********************************************************************************************************************************
*   Internal functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Guesses the HTTP MIME type based on a given file extension.
 *
 * Note: Has to include the beginning dot, e.g. ".mp3" (see IPRT).
 *
 * @returns Guessed MIME type, or "application/octet-stream" if not found.
 * @param   pszFileExt          File extension to guess MIME type for.
 */
static const char *rtHttpServerGuessMIMEType(const char *pszFileExt)
{
    if (pszFileExt)
    {
        size_t i = 0;
        while (s_aFileExtMIMEType[i++].pszExt) /* Slow, but does the job for now. */
        {
            if (!RTStrICmp(pszFileExt, s_aFileExtMIMEType[i].pszExt))
                return s_aFileExtMIMEType[i].pszMIMEType;
        }
    }

    return "application/octet-stream";
}

/**
 * Initializes a HTTP body.
 *
 * @param   pBody               Body to initialize.
 * @param   cbSize              Size of body (in bytes) to allocate. Optional and can be 0.
 */
static int rtHttpServerBodyInit(PRTHTTPBODY pBody, size_t cbSize)
{
    if (cbSize)
    {
        pBody->pvBody      = RTMemAlloc(cbSize);
        AssertPtrReturn(pBody->pvBody, VERR_NO_MEMORY);
        pBody->cbBodyAlloc = cbSize;
    }
    else
    {
        pBody->pvBody      = NULL;
        pBody->cbBodyAlloc = 0;
    }

    pBody->cbBodyUsed = 0;
    pBody->offBody    = 0;

    return VINF_SUCCESS;
}

/**
 * Destroys a HTTP body.
 *
 * @param   pBody               Body to destroy.
 */
static void rtHttpServerBodyDestroy(PRTHTTPBODY pBody)
{
    if (!pBody)
        return;

    if (pBody->pvBody)
    {
        Assert(pBody->cbBodyAlloc);

        RTMemFree(pBody->pvBody);
        pBody->pvBody = NULL;
    }

    pBody->cbBodyAlloc  = 0;
    pBody->cbBodyUsed   = 0;
    pBody->offBody      = 0;
}

/**
 * Allocates and initializes a new client request.
 *
 * @returns Pointer to the new client request, or NULL on OOM.
 *          Needs to be free'd with rtHttpServerReqFree().
 */
static PRTHTTPSERVERREQ rtHttpServerReqAlloc(void)
{
    PRTHTTPSERVERREQ pReq = (PRTHTTPSERVERREQ)RTMemAllocZ(sizeof(RTHTTPSERVERREQ));
    AssertPtrReturn(pReq, NULL);

    int rc2 = RTHttpHeaderListInit(&pReq->hHdrLst);
    AssertRC(rc2);

    rc2 = rtHttpServerBodyInit(&pReq->Body, 0 /* cbSize */);
    AssertRC(rc2);

    return pReq;
}

/**
 * Frees a formerly allocated client request.
 *
 * @param   pReq                Pointer to client request to free.
 *                              The pointer will be invalid on return.
 */
static void rtHttpServerReqFree(PRTHTTPSERVERREQ pReq)
{
    if (!pReq)
        return;

    RTStrFree(pReq->pszUrl);

    RTHttpHeaderListDestroy(pReq->hHdrLst);
    pReq->hHdrLst = NIL_RTHTTPHEADERLIST;

    rtHttpServerBodyDestroy(&pReq->Body);

    RTMemFree(pReq);
    pReq = NULL;
}

/**
 * Initializes a HTTP server response with an allocated body size.
 *
 * @returns VBox status code.
 * @param   pResp               HTTP server response to initialize.
 * @param   cbBody              Body size (in bytes) to allocate.
 */
RTR3DECL(int) RTHttpServerResponseInitEx(PRTHTTPSERVERRESP pResp, size_t cbBody)
{
    pResp->enmSts = RTHTTPSTATUS_INTERNAL_NOT_SET;

    int rc = RTHttpHeaderListInit(&pResp->hHdrLst);
    AssertRCReturn(rc, rc);

    rc = rtHttpServerBodyInit(&pResp->Body, cbBody);

    return rc;
}

/**
 * Initializes a HTTP server response.
 *
 * @returns VBox status code.
 * @param   pResp               HTTP server response to initialize.
 */
RTR3DECL(int) RTHttpServerResponseInit(PRTHTTPSERVERRESP pResp)
{
    return RTHttpServerResponseInitEx(pResp, 0 /* cbBody */);
}

/**
 * Destroys a formerly initialized HTTP server response.
 *
 * @param   pResp               Pointer to HTTP server response to destroy.
 */
RTR3DECL(void) RTHttpServerResponseDestroy(PRTHTTPSERVERRESP pResp)
{
    if (!pResp)
        return;

    pResp->enmSts = RTHTTPSTATUS_INTERNAL_NOT_SET;

    RTHttpHeaderListDestroy(pResp->hHdrLst);
    pResp->hHdrLst = NIL_RTHTTPHEADERLIST;

    rtHttpServerBodyDestroy(&pResp->Body);
}


/*********************************************************************************************************************************
*   Protocol Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Logs the HTTP protocol communication to the debug logger (2).
 *
 * @param   pClient             Client to log communication for.
 * @param   fWrite              Whether the server is writing (to client) or reading (from client).
 * @param   pszData             Actual protocol communication data to log.
 */
static void rtHttpServerLogProto(PRTHTTPSERVERCLIENT pClient, bool fWrite, const char *pszData)
{
    RT_NOREF(pClient);

#ifdef LOG_ENABLED
    if (!pszData) /* Nothing to log? Bail out. */
        return;

    char **ppapszStrings;
    size_t cStrings;
    int rc2 = RTStrSplit(pszData, strlen(pszData) + 1 /* Must include terminator */,
                         RTHTTPSERVER_HTTP11_EOL_STR, &ppapszStrings, &cStrings);
    if (RT_SUCCESS(rc2))
    {
        for (size_t i = 0; i < cStrings; i++)
        {
            Log2(("%s %s\n", fWrite ? ">" : "<", ppapszStrings[i]));
            RTStrFree(ppapszStrings[i]);
        }

        RTMemFree(ppapszStrings);
    }
#else
    RT_NOREF(fWrite, pszData);
#endif
}

/**
 * Writes HTTP protocol communication data to a connected client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to write data to.
 * @param   pszData             Data to write. Must be zero-terminated.
 */
static int rtHttpServerWriteProto(PRTHTTPSERVERCLIENT pClient, const char *pszData)
{
    rtHttpServerLogProto(pClient, true /* fWrite */, pszData);
    return RTTcpWrite(pClient->hSocket, pszData, strlen(pszData));
}

/**
 * Main function for sending a response back to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   enmSts              Status code to send.
 */
static int rtHttpServerSendResponse(PRTHTTPSERVERCLIENT pClient, RTHTTPSTATUS enmSts)
{
    char *pszResp;
    int rc = RTStrAPrintf(&pszResp,
                          "%s %RU32 %s\r\n", RTHTTPVER_1_1_STR, enmSts, RTHttpStatusToStr(enmSts));
    AssertRCReturn(rc, rc);
    rc = rtHttpServerWriteProto(pClient, pszResp);
    RTStrFree(pszResp);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Main function for sending response headers back to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   pHdrLst             Header list to send. Optional and can be NULL.
 */
static int rtHttpServerSendResponseHdrEx(PRTHTTPSERVERCLIENT pClient, PRTHTTPHEADERLIST pHdrLst)
{
    RTHTTPHEADERLIST HdrLst;
    int rc = RTHttpHeaderListInit(&HdrLst);
    AssertRCReturn(rc, rc);

#ifdef DEBUG
    /* Include a timestamp when running a debug build. */
    RTTIMESPEC tsNow;
    char szTS[64];
    RTTimeSpecToString(RTTimeNow(&tsNow), szTS, sizeof(szTS));
    rc = RTHttpHeaderListAdd(HdrLst, "Date",  szTS, strlen(szTS), RTHTTPHEADERLISTADD_F_BACK);
    AssertRCReturn(rc, rc);
#endif

    /* Note: Deliberately don't include the VBox version due to security reasons. */
    rc = RTHttpHeaderListAdd(HdrLst, "Server", "Oracle VirtualBox", strlen("Oracle VirtualBox"), RTHTTPHEADERLISTADD_F_BACK);
    AssertRCReturn(rc, rc);

#ifdef IPRT_HTTP_WITH_WEBDAV
    rc = RTHttpHeaderListAdd(HdrLst, "Allow", "GET, HEAD, PROPFIND", strlen("GET, HEAD, PROPFIND"), RTHTTPHEADERLISTADD_F_BACK);
    AssertRCReturn(rc, rc);
    rc = RTHttpHeaderListAdd(HdrLst, "DAV", "1", strlen("1"), RTHTTPHEADERLISTADD_F_BACK); /* Note: v1 is sufficient for read-only access. */
    AssertRCReturn(rc, rc);
#endif

    char *pszHdr = NULL;

    size_t i = 0;
    const char *pszEntry;
    while ((pszEntry = RTHttpHeaderListGetByOrdinal(HdrLst, i++)) != NULL)
    {
        rc = RTStrAAppend(&pszHdr, pszEntry);
        AssertRCBreak(rc);
        rc = RTStrAAppend(&pszHdr, RTHTTPSERVER_HTTP11_EOL_STR);
        AssertRCBreak(rc);
    }

    /* Append optional headers, if any. */
    if (pHdrLst)
    {
        i = 0;
        while ((pszEntry = RTHttpHeaderListGetByOrdinal(*pHdrLst, i++)) != NULL)
        {
            rc = RTStrAAppend(&pszHdr, pszEntry);
            AssertRCBreak(rc);
            rc = RTStrAAppend(&pszHdr, RTHTTPSERVER_HTTP11_EOL_STR);
            AssertRCBreak(rc);
        }
    }

    if (RT_SUCCESS(rc))
    {
        /* Append trailing EOL. */
        rc = RTStrAAppend(&pszHdr, RTHTTPSERVER_HTTP11_EOL_STR);
        if (RT_SUCCESS(rc))
            rc = rtHttpServerWriteProto(pClient, pszHdr);
    }

    RTStrFree(pszHdr);

    RTHttpHeaderListDestroy(HdrLst);

    LogFlowFunc(("rc=%Rrc\n", rc));
    return rc;
}

/**
 * Replies with (three digit) response status back to the client, extended version.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   enmSts              Status code to send.
 * @param   pHdrLst             Header list to send. Optional and can be NULL.
 */
static int rtHttpServerSendResponseEx(PRTHTTPSERVERCLIENT pClient, RTHTTPSTATUS enmSts, PRTHTTPHEADERLIST pHdrLst)
{
    int rc = rtHttpServerSendResponse(pClient, enmSts);
    if (RT_SUCCESS(rc))
        rc = rtHttpServerSendResponseHdrEx(pClient, pHdrLst);

    return rc;
}

/**
 * Replies with (three digit) response status back to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to reply to.
 * @param   enmSts              Status code to send.
 */
static int rtHttpServerSendResponseSimple(PRTHTTPSERVERCLIENT pClient, RTHTTPSTATUS enmSts)
{
    return rtHttpServerSendResponseEx(pClient, enmSts, NULL /* pHdrLst */);
}

/**
 * Sends a chunk of the response body to the client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to send body to.
 * @param   pvBuf               Data buffer to send.
 * @param   cbBuf               Size (in bytes) of data buffer to send.
 * @param   pcbSent             Where to store the sent bytes. Optional and can be NULL.
 */
static int rtHttpServerSendResponseBody(PRTHTTPSERVERCLIENT pClient, void *pvBuf, size_t cbBuf, size_t *pcbSent)
{
    int rc = RTTcpWrite(pClient->hSocket, pvBuf, cbBuf);
    if (   RT_SUCCESS(rc)
        && pcbSent)
        *pcbSent = cbBuf;

    return rc;
}

/**
 * Resolves a VBox status code to a HTTP status code.
 *
 * @returns Resolved HTTP status code, or RTHTTPSTATUS_INTERNALSERVERERROR if not able to resolve.
 * @param   rc                  VBox status code to resolve.
 */
static RTHTTPSTATUS rtHttpServerRcToStatus(int rc)
{
    switch (rc)
    {
        case VINF_SUCCESS:              return RTHTTPSTATUS_OK;
        case VERR_INVALID_PARAMETER:    return RTHTTPSTATUS_BADREQUEST;
        case VERR_INVALID_POINTER:      return RTHTTPSTATUS_BADREQUEST;
        case VERR_NOT_IMPLEMENTED:      return RTHTTPSTATUS_NOTIMPLEMENTED;
        case VERR_NOT_SUPPORTED:        return RTHTTPSTATUS_NOTIMPLEMENTED;
        case VERR_PATH_NOT_FOUND:       return RTHTTPSTATUS_NOTFOUND;
        case VERR_FILE_NOT_FOUND:       return RTHTTPSTATUS_NOTFOUND;
        case VERR_IS_A_DIRECTORY:       return RTHTTPSTATUS_FORBIDDEN;
        case VERR_NOT_FOUND:            return RTHTTPSTATUS_NOTFOUND;
        case VERR_INTERNAL_ERROR:       return RTHTTPSTATUS_INTERNALSERVERERROR;
        default:
            break;
    }

    AssertMsgFailed(("rc=%Rrc not handled for HTTP status\n", rc));
    return RTHTTPSTATUS_INTERNALSERVERERROR;
}


/*********************************************************************************************************************************
*   Command Protocol Handlers                                                                                                    *
*********************************************************************************************************************************/

/**
 * Handler for the GET method.
 *
 * @returns VBox status code.
 * @param   pClient             Client to handle GET method for.
 * @param   pReq                Client request to handle.
 */
static DECLCALLBACK(int) rtHttpServerHandleGET(PRTHTTPSERVERCLIENT pClient, PRTHTTPSERVERREQ pReq)
{
    LogFlowFuncEnter();

    /* If a low-level GET request handler is defined, call it and return. */
    RTHTTPSERVER_HANDLE_CALLBACK_VA_RET(pfnOnGetRequest, pReq);

    RTFSOBJINFO fsObj;
    RT_ZERO(fsObj); /* Shut up MSVC. */

    char *pszMIMEHint = NULL;

    RTHTTPSTATUS enmStsResponse = RTHTTPSTATUS_OK;

    int rc = VINF_SUCCESS;

    RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnQueryInfo, pReq, &fsObj, &pszMIMEHint);
    if (RT_FAILURE(rc))
         enmStsResponse = rtHttpServerRcToStatus(rc);

    void *pvHandle = NULL;
    if (RT_SUCCESS(rc)) /* Only call open if querying information above succeeded. */
        RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnOpen, pReq, &pvHandle);

    size_t cbBuf = _64K;
    void  *pvBuf = RTMemAlloc(cbBuf);
    AssertPtrReturn(pvBuf, VERR_NO_MEMORY);

    for (;;)
    {
        RTHTTPHEADERLIST HdrLst;
        rc = RTHttpHeaderListInit(&HdrLst);
        AssertRCBreak(rc);

        char szVal[16];

        /* Note: For directories fsObj.cbObject contains the actual size (in bytes)
         *       of the body data for the directory listing. */

        ssize_t cch = RTStrPrintf2(szVal, sizeof(szVal), "%RU64", fsObj.cbObject);
        AssertBreakStmt(cch, rc = VERR_BUFFER_OVERFLOW);
        rc = RTHttpHeaderListAdd(HdrLst, "Content-Length", szVal, strlen(szVal), RTHTTPHEADERLISTADD_F_BACK);
        AssertRCBreak(rc);

        cch = RTStrPrintf2(szVal, sizeof(szVal), "identity");
        AssertBreakStmt(cch, rc = VERR_BUFFER_OVERFLOW);
        rc = RTHttpHeaderListAdd(HdrLst, "Content-Encoding", szVal, strlen(szVal), RTHTTPHEADERLISTADD_F_BACK);
        AssertRCBreak(rc);

        if (pszMIMEHint == NULL)
        {
            const char *pszMIME = rtHttpServerGuessMIMEType(RTPathSuffix(pReq->pszUrl));
            rc = RTHttpHeaderListAdd(HdrLst, "Content-Type", pszMIME, strlen(pszMIME), RTHTTPHEADERLISTADD_F_BACK);
        }
        else
        {
            rc = RTHttpHeaderListAdd(HdrLst, "Content-Type", pszMIMEHint, strlen(pszMIMEHint), RTHTTPHEADERLISTADD_F_BACK);
            RTStrFree(pszMIMEHint);
            pszMIMEHint = NULL;
        }
        AssertRCBreak(rc);

        if (pClient->State.msKeepAlive)
        {
            /* If the client requested to keep alive the connection,
             * always override this with 30s and report this back to the client. */
            pClient->State.msKeepAlive = RT_MS_30SEC; /** @todo Make this configurable. */
#ifdef DEBUG_andy
            pClient->State.msKeepAlive = 5000;
#endif
            cch = RTStrPrintf2(szVal, sizeof(szVal), "timeout=%RU64", pClient->State.msKeepAlive / RT_MS_1SEC); /** @todo No pipelining support here yet. */
            AssertBreakStmt(cch, rc = VERR_BUFFER_OVERFLOW);
            rc = RTHttpHeaderListAdd(HdrLst, "Keep-Alive", szVal, strlen(szVal), RTHTTPHEADERLISTADD_F_BACK);
            AssertRCBreak(rc);
        }

        rc = rtHttpServerSendResponseEx(pClient, enmStsResponse, &HdrLst);

        RTHttpHeaderListDestroy(HdrLst);
        HdrLst = NIL_RTHTTPHEADERLIST;

        if (rc == VERR_BROKEN_PIPE) /* Could happen on fast reloads. */
            break;
        AssertRCBreak(rc);

        size_t cbToRead  = fsObj.cbObject;
        size_t cbRead    = 0; /* Shut up GCC. */
        size_t cbWritten = 0; /* Ditto. */
        while (cbToRead)
        {
            RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnRead, pReq, pvHandle, pvBuf, RT_MIN(cbBuf, cbToRead), &cbRead);
            if (RT_FAILURE(rc))
                break;
            rc = rtHttpServerSendResponseBody(pClient, pvBuf, cbRead, &cbWritten);
            if (RT_FAILURE(rc))
                break;
            AssertBreak(cbToRead >= cbWritten);
            cbToRead -= cbWritten;
#if 0 /* Disabled, VERR_BROKEN_PIPE happens too often with GVFS 1.50. */
            AssertMsgRCBreak(rc, ("rc=%Rrc, cbRead=%zu, cbToRead=%zu, cbWritten=%zu\n", rc, cbRead, cbToRead, cbWritten));
#endif
        }

        break;
    } /* for (;;) */

    if (rc == VERR_NET_CONNECTION_RESET_BY_PEER) /* Clients often apruptly abort the connection when done. */
        rc = VINF_SUCCESS;

    RTMemFree(pvBuf);

    int rc2 = rc; /* Save rc. */

    if (pvHandle)
        RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnClose, pReq, pvHandle);

    if (RT_FAILURE(rc2)) /* Restore original rc on failure. */
        rc = rc2;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handler for the HEAD method.
 *
 * @returns VBox status code.
 * @param   pClient             Client to handle HEAD method for.
 * @param   pReq                Client request to handle.
 */
static DECLCALLBACK(int) rtHttpServerHandleHEAD(PRTHTTPSERVERCLIENT pClient, PRTHTTPSERVERREQ pReq)
{
    LogFlowFuncEnter();

    /* If a low-level HEAD request handler is defined, call it and return. */
    RTHTTPSERVER_HANDLE_CALLBACK_VA_RET(pfnOnHeadRequest, pReq);

    int rc = VINF_SUCCESS;

    RTFSOBJINFO fsObj;
    RT_ZERO(fsObj); /* Shut up MSVC. */

    RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnQueryInfo, pReq, &fsObj, NULL /* pszMIMEHint */);
    if (RT_SUCCESS(rc))
    {
        RTHTTPHEADERLIST HdrLst;
        rc = RTHttpHeaderListInit(&HdrLst);
        AssertRCReturn(rc, rc);

        /*
         * Note: A response to a HEAD request does not have a body.
         * All entity headers below are assumed to describe the the response a similar GET
         * request would return (but then with a body).
         */
        char szVal[16];

        ssize_t cch = RTStrPrintf2(szVal, sizeof(szVal), "%RU64", fsObj.cbObject);
        AssertReturn(cch, VERR_BUFFER_OVERFLOW);
        rc = RTHttpHeaderListAdd(HdrLst, "Content-Length", szVal, strlen(szVal), RTHTTPHEADERLISTADD_F_BACK);
        AssertRCReturn(rc, rc);

        cch = RTStrPrintf2(szVal, sizeof(szVal), "identity");
        AssertReturn(cch, VERR_BUFFER_OVERFLOW);
        rc = RTHttpHeaderListAdd(HdrLst, "Content-Encoding", szVal, strlen(szVal), RTHTTPHEADERLISTADD_F_BACK);
        AssertRCReturn(rc, rc);

        const char *pszMIME = rtHttpServerGuessMIMEType(RTPathSuffix(pReq->pszUrl));
        rc = RTHttpHeaderListAdd(HdrLst, "Content-Type", pszMIME, strlen(pszMIME), RTHTTPHEADERLISTADD_F_BACK);
        AssertRCReturn(rc, rc);

        rc = rtHttpServerSendResponseEx(pClient, RTHTTPSTATUS_OK, &HdrLst);
        AssertRCReturn(rc, rc);

        RTHttpHeaderListDestroy(HdrLst);
        HdrLst = NIL_RTHTTPHEADERLIST;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef IPRT_HTTP_WITH_WEBDAV
/**
 * Handler for the OPTIONS method.
 *
 * @returns VBox status code.
 * @param   pClient             Client to handle OPTIONS method for.
 * @param   pReq                Client request to handle.
 */
static DECLCALLBACK(int) rtHttpServerHandleOPTIONS(PRTHTTPSERVERCLIENT pClient, PRTHTTPSERVERREQ pReq)
{
    LogFlowFuncEnter();

    RT_NOREF(pReq);

    int rc = rtHttpServerSendResponseEx(pClient, RTHTTPSTATUS_OK, NULL /* pHdrLst */);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handler for the PROPFIND (WebDAV) method.
 *
 * @returns VBox status code.
 * @param   pClient             Client to handle PROPFIND method for.
 * @param   pReq                Client request to handle.
 */
static DECLCALLBACK(int) rtHttpServerHandlePROPFIND(PRTHTTPSERVERCLIENT pClient, PRTHTTPSERVERREQ pReq)
{
    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    /* If a low-level GET request handler is defined, call it and return. */
    RTHTTPSERVER_HANDLE_CALLBACK_VA_RET(pfnOnGetRequest, pReq);

    RTFSOBJINFO fsObj;
    RT_ZERO(fsObj); /* Shut up MSVC. */

    RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnQueryInfo, pReq, &fsObj, NULL /* pszMIMEHint */);
    if (RT_FAILURE(rc))
        return rc;

    void *pvHandle = NULL;
    RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnOpen, pReq, &pvHandle);

    if (RT_SUCCESS(rc))
    {
        size_t cbBuf = _64K;
        void  *pvBuf = RTMemAlloc(cbBuf);
        AssertPtrReturn(pvBuf, VERR_NO_MEMORY);

        for (;;)
        {
            RTHTTPHEADERLIST HdrLst;
            rc = RTHttpHeaderListInit(&HdrLst);
            AssertRCReturn(rc, rc);

            char szVal[16];

            rc = RTHttpHeaderListAdd(HdrLst, "Content-Type", "text/xml; charset=utf-8", strlen("text/xml; charset=utf-8"), RTHTTPHEADERLISTADD_F_BACK);
            AssertRCBreak(rc);

            /* Note: For directories fsObj.cbObject contains the actual size (in bytes)
             *       of the body data for the directory listing. */

            ssize_t cch = RTStrPrintf2(szVal, sizeof(szVal), "%RU64", fsObj.cbObject);
            AssertBreakStmt(cch, VERR_BUFFER_OVERFLOW);
            rc = RTHttpHeaderListAdd(HdrLst, "Content-Length", szVal, strlen(szVal), RTHTTPHEADERLISTADD_F_BACK);
            AssertRCBreak(rc);

            rc = rtHttpServerSendResponseEx(pClient, RTHTTPSTATUS_MULTISTATUS, &HdrLst);
            AssertRCReturn(rc, rc);

            RTHttpHeaderListDestroy(HdrLst);
            HdrLst = NIL_RTHTTPHEADERLIST;

            size_t cbToRead  = fsObj.cbObject;
            size_t cbRead    = 0; /* Shut up GCC. */
            size_t cbWritten = 0; /* Ditto. */
            while (cbToRead)
            {
                RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnRead, pReq, pvHandle, pvBuf, RT_MIN(cbBuf, cbToRead), &cbRead);
                if (RT_FAILURE(rc))
                    break;
                //rtHttpServerLogProto(pClient, true /* fWrite */, (const char *)pvBuf);
                rc = rtHttpServerSendResponseBody(pClient, pvBuf, cbRead, &cbWritten);
                AssertBreak(cbToRead >= cbWritten);
                cbToRead -= cbWritten;
                if (rc == VERR_NET_CONNECTION_RESET_BY_PEER) /* Clients often apruptly abort the connection when done. */
                {
                    rc = VINF_SUCCESS;
                    break;
                }
                AssertRCBreak(rc);
            }

            break;
        } /* for (;;) */

        RTMemFree(pvBuf);

        int rc2 = rc; /* Save rc. */

        RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnClose, pReq, pvHandle);

        if (RT_FAILURE(rc2)) /* Restore original rc on failure. */
            rc = rc2;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}
#endif /* IPRT_HTTP_WITH_WEBDAV */

/**
 * Validates if a given path is valid or not.
 *
 * @returns \c true if path is valid, or \c false if not.
 * @param   pszPath             Path to check.
 * @param   fIsAbsolute         Whether the path to check is an absolute path or not.
 */
static bool rtHttpServerPathIsValid(const char *pszPath, bool fIsAbsolute)
{
    if (!pszPath)
        return false;

    bool fIsValid =    strlen(pszPath)
                    && RTStrIsValidEncoding(pszPath)
                    && RTStrStr(pszPath, "..") == NULL;     /** @todo Very crude for now -- improve this. */
    if (   fIsValid
        && fIsAbsolute)
    {
        RTFSOBJINFO objInfo;
        int rc2 = RTPathQueryInfo(pszPath, &objInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc2))
        {
            fIsValid =    RTFS_IS_DIRECTORY(objInfo.Attr.fMode)
                       || RTFS_IS_FILE(objInfo.Attr.fMode);

            /* No symlinks and other stuff allowed. */
        }
        else
            fIsValid = false;
    }

    LogFlowFunc(("pszPath=%s -> %RTbool\n", pszPath, fIsValid));
    return fIsValid;

}

/**
 * Parses headers and sets (replaces) a given header list.
 *
 * @returns VBox status code.
 * @param   hList               Header list to fill parsed headers in.
 * @param   cStrings            Number of strings to parse for \a papszStrings.
 * @param   papszStrings        Array of strings to parse.
 */
static int rtHttpServerParseHeaders(RTHTTPHEADERLIST hList, size_t cStrings, char **papszStrings)
{
    /* Nothing to parse left? Bail out early. */
    if (   !cStrings
        || !papszStrings)
        return VINF_SUCCESS;

#ifdef LOG_ENABLED
    for (size_t i = 0; i < cStrings; i++)
        LogFlowFunc(("Header: %s\n", papszStrings[i]));
#endif

    int rc = RTHttpHeaderListSet(hList, cStrings, papszStrings);

    LogFlowFunc(("rc=%Rrc, cHeaders=%zu\n", rc, cStrings));
    return rc;
}

/**
 * Main function for parsing and allocating a client request.
 *
 * See: https://tools.ietf.org/html/rfc2616#section-2.2
 *
 * @returns VBox status code.
 * @param   pClient             Client to parse request from.
 * @param   pszReq              Request string with headers to parse.
 * @param   cbReq               Size (in bytes) of request string to parse.
 * @param   ppReq               Where to store the allocated client request on success.
 *                              Needs to be free'd via rtHttpServerReqFree().
 */
static int rtHttpServerParseRequest(PRTHTTPSERVERCLIENT pClient, const char *pszReq, size_t cbReq,
                                    PRTHTTPSERVERREQ *ppReq)
{
    RT_NOREF(pClient);

    AssertPtrReturn(pszReq, VERR_INVALID_POINTER);
    AssertReturn(cbReq, VERR_INVALID_PARAMETER);

    /* We only support UTF-8 charset for now. */
    AssertReturn(RTStrIsValidEncoding(pszReq), VERR_INVALID_PARAMETER);

    char **ppapszReq = NULL;
    size_t cReq      = 0;
    int rc = RTStrSplit(pszReq, cbReq, RTHTTPSERVER_HTTP11_EOL_STR, &ppapszReq, &cReq);
    if (RT_FAILURE(rc))
        return rc;

    if (!cReq)
        return VERR_INVALID_PARAMETER;

#ifdef LOG_ENABLED
    for (size_t i = 0; i < cReq; i++)
        LogFlowFunc(("%s\n", ppapszReq[i]));
#endif

    PRTHTTPSERVERREQ pReq = NULL;

    char **ppapszFirstLine = NULL;
    size_t cFirstLine = 0;
    rc = RTStrSplit(ppapszReq[0], strlen(ppapszReq[0]), " ", &ppapszFirstLine, &cFirstLine);
    if (RT_SUCCESS(rc))
    {
        if (cFirstLine < 3) /* At leat the method, path and version has to be present. */
            rc = VERR_INVALID_PARAMETER;
    }

    while (RT_SUCCESS(rc)) /* To use break. */
    {
        pReq = rtHttpServerReqAlloc();
        AssertPtrBreakStmt(pReq, rc = VERR_NO_MEMORY);

        /*
         * Parse method to use. Method names are case sensitive.
         */
        const char *pszMethod = ppapszFirstLine[0];
        if      (!RTStrCmp(pszMethod, "GET"))      pReq->enmMethod = RTHTTPMETHOD_GET;
        else if (!RTStrCmp(pszMethod, "HEAD"))     pReq->enmMethod = RTHTTPMETHOD_HEAD;
#ifdef IPRT_HTTP_WITH_WEBDAV
        else if (!RTStrCmp(pszMethod, "OPTIONS"))  pReq->enmMethod = RTHTTPMETHOD_OPTIONS;
        else if (!RTStrCmp(pszMethod, "PROPFIND")) pReq->enmMethod = RTHTTPMETHOD_PROPFIND;
#endif
        else
        {
            rc = VERR_NOT_SUPPORTED;
            break;
        }

        /*
         * Parse requested path.
         */
        /** @todo Do URL unescaping here. */
        const char *pszPath = ppapszFirstLine[1];
        if (!rtHttpServerPathIsValid(pszPath, false /* fIsAbsolute */))
        {
            rc = VERR_PATH_NOT_FOUND;
            break;
        }

        pReq->pszUrl = RTStrDup(pszPath);

        /*
         * Parse HTTP version to use.
         * We're picky here: Only HTTP 1.1 is supported by now.
         */
        const char *pszVer = ppapszFirstLine[2];
        if (RTStrCmp(pszVer, RTHTTPVER_1_1_STR)) /** @todo Use RTStrVersionCompare. Later. */
        {
            rc = VERR_NOT_SUPPORTED;
            break;
        }

        /** @todo Anything else needed for the first line here? */

        /*
         * Process headers, if any.
         */
        if (cReq > 1)
        {
            rc = rtHttpServerParseHeaders(pReq->hHdrLst, cReq - 1, &ppapszReq[1]);
            if (RT_SUCCESS(rc))
            {
                if (RTHttpHeaderListGet(pReq->hHdrLst, "Connection", RTSTR_MAX))
                    pClient->State.msKeepAlive = RT_MS_30SEC; /** @todo Insert the real value here. */
            }
        }
        break;
    } /* for (;;) */

    /*
     * Cleanup.
     */

    for (size_t i = 0; i < cFirstLine; i++)
        RTStrFree(ppapszFirstLine[i]);
    RTMemFree(ppapszFirstLine);

    for (size_t i = 0; i < cReq; i++)
        RTStrFree(ppapszReq[i]);
    RTMemFree(ppapszReq);

    if (RT_FAILURE(rc))
    {
        rtHttpServerReqFree(pReq);
        pReq = NULL;
    }
    else
        *ppReq = pReq;

    return rc;
}

/**
 * Main function for processing client requests.
 *
 * @returns VBox status code.
 * @param   pClient             Client to process request for.
 * @param   pszReq              Request string to parse and handle.
 * @param   cbReq               Size (in bytes) of request string.
 */
static int rtHttpServerProcessRequest(PRTHTTPSERVERCLIENT pClient, char *pszReq, size_t cbReq)
{
    RTHTTPSTATUS enmSts = RTHTTPSTATUS_INTERNAL_NOT_SET;

    PRTHTTPSERVERREQ pReq = NULL; /* Shut up GCC. */
    int rc = rtHttpServerParseRequest(pClient, pszReq, cbReq, &pReq);
    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("Request %s %s\n", RTHttpMethodToStr(pReq->enmMethod), pReq->pszUrl));

        RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnRequestBegin, pReq);

        unsigned i = 0;
        for (; i < RT_ELEMENTS(g_aMethodMap); i++)
        {
            const RTHTTPSERVERMETHOD_ENTRY *pMethodEntry = &g_aMethodMap[i];
            if (pReq->enmMethod == pMethodEntry->enmMethod)
            {
                /* Hand in request to method handler. */
                int rcMethod = pMethodEntry->pfnMethod(pClient, pReq);
                if (RT_FAILURE(rcMethod))
                    LogFunc(("Request %s %s failed with %Rrc\n", RTHttpMethodToStr(pReq->enmMethod), pReq->pszUrl, rcMethod));
                break;
            }
        }

        RTHTTPSERVER_HANDLE_CALLBACK_VA(pfnRequestEnd, pReq);

        if (i == RT_ELEMENTS(g_aMethodMap))
            enmSts = RTHTTPSTATUS_NOTIMPLEMENTED;

        rtHttpServerReqFree(pReq);
    }
    else
        enmSts = RTHTTPSTATUS_BADREQUEST;

    /* If a status was set here explicitly, return it to prevent client hangs. */
    if (enmSts != RTHTTPSTATUS_INTERNAL_NOT_SET)
    {
        int rc2 = rtHttpServerSendResponseSimple(pClient, enmSts);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Main loop for processing client requests.
 *
 * @returns VBox status code.
 * @param   pClient             Client to process requests for.
 * @param   msTimeout           Timeout to wait for reading data.
 *                              Gets renewed for a each reading round.
 */
static int rtHttpServerClientMain(PRTHTTPSERVERCLIENT pClient, RTMSINTERVAL msTimeout)
{
    int rc;

    char szReq[RTHTTPSERVER_MAX_REQ_LEN + 1];

    LogFlowFunc(("Client connected\n"));

    /* Initialize client state. */
    pClient->State.msKeepAlive = 0;

    RTMSINTERVAL cWaitMs      = msTimeout;
    uint64_t     tsLastReadMs = 0;

    for (;;) /* For keep-alive handling. */
    {
        rc = RTTcpSelectOne(pClient->hSocket, cWaitMs);
        if (RT_FAILURE(rc))
        {
            Log2Func(("RTTcpSelectOne=%Rrc (cWaitMs=%RU64)\n", rc, cWaitMs));
            if (rc == VERR_TIMEOUT)
            {
                if (pClient->State.msKeepAlive) /* Keep alive handling needed? */
                {
                    if (!tsLastReadMs)
                        tsLastReadMs = RTTimeMilliTS();
                    const uint64_t tsDeltaMs = pClient->State.msKeepAlive - (RTTimeMilliTS() - tsLastReadMs);
                    Log2Func(("tsLastReadMs=%RU64, tsDeltaMs=%RU64\n", tsLastReadMs, tsDeltaMs));
                    Log2Func(("Keep alive active (%RU32ms): %RU64ms remaining\n", pClient->State.msKeepAlive, tsDeltaMs));
                    if (   tsDeltaMs > cWaitMs
                        && tsDeltaMs < pClient->State.msKeepAlive)
                        continue;

                    LogFunc(("Keep alive active: Client did not respond within %RU32ms, closing\n", pClient->State.msKeepAlive));
                    rc = VINF_SUCCESS;
                    break;
                }
            }

            break;
        }

        LogFlowFunc(("Reading client request ...\n"));

        tsLastReadMs = RTTimeMilliTS();
        cWaitMs      = 200;  /* All consequtive waits do busy waiting for now. */

        char  *pszReq      = szReq;
        size_t cbRead;
        size_t cbToRead    = sizeof(szReq);
        size_t cbReadTotal = 0;

        do
        {
            rc = RTTcpReadNB(pClient->hSocket, pszReq, cbToRead, &cbRead);
            if (RT_FAILURE(rc))
                break;

            if (!cbRead)
                break;

            /* Make sure to terminate string read so far. */
            pszReq[cbRead] = '\0';

            /* End of request reached? */
            /** @todo BUGBUG Improve this. */
            char *pszEOR = RTStrStr(&pszReq[cbReadTotal], "\r\n\r\n");
            if (pszEOR)
            {
                cbReadTotal = pszEOR - pszReq;
                *pszEOR = '\0';
                break;
            }

            AssertBreak(cbToRead >= cbRead);
            cbToRead    -= cbRead;
            cbReadTotal += cbRead;
            AssertBreak(cbReadTotal <= sizeof(szReq));
            pszReq      += cbRead;

        } while (cbToRead);

        Log2Func(("Read client request done (%zu bytes) -> rc=%Rrc\n", cbReadTotal, rc));

        if (   RT_SUCCESS(rc)
            && cbReadTotal)
        {
            rtHttpServerLogProto(pClient, false /* fWrite */, szReq);

            rc = rtHttpServerProcessRequest(pClient, szReq, cbReadTotal);
        }

        break;

    } /* for */

    if (RT_FAILURE(rc))
    {
        switch (rc)
        {
            case VERR_NET_CONNECTION_RESET_BY_PEER:
            {
                LogFunc(("Client closed the connection\n"));
                rc = VINF_SUCCESS;
                break;
            }

            default:
                LogFunc(("Client processing failed with %Rrc\n", rc));
                break;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Per-client thread for serving the server's control connection.
 *
 * @returns VBox status code.
 * @param   hSocket             Socket handle to use for the control connection.
 * @param   pvUser              User-provided arguments. Of type PRTHTTPSERVERINTERNAL.
 */
static DECLCALLBACK(int) rtHttpServerClientThread(RTSOCKET hSocket, void *pvUser)
{
    PRTHTTPSERVERINTERNAL pThis = (PRTHTTPSERVERINTERNAL)pvUser;
    RTHTTPSERVER_VALID_RETURN(pThis);

    LogFlowFuncEnter();

    RTHTTPSERVERCLIENT Client;
    RT_ZERO(Client);

    Client.pServer = pThis;
    Client.hSocket = hSocket;

    return rtHttpServerClientMain(&Client, RT_MS_30SEC /* Timeout */);
}

RTR3DECL(int) RTHttpServerCreate(PRTHTTPSERVER hHttpServer, const char *pszAddress, uint16_t uPort,
                                 PRTHTTPSERVERCALLBACKS pCallbacks, void *pvUser, size_t cbUser)
{
    AssertPtrReturn(hHttpServer, VERR_INVALID_POINTER);
    AssertPtrReturn(pszAddress,  VERR_INVALID_POINTER);
    AssertReturn   (uPort,       VERR_INVALID_PARAMETER);
    AssertPtrReturn(pCallbacks,  VERR_INVALID_POINTER);
    /* pvUser is optional. */

    int rc;

    PRTHTTPSERVERINTERNAL pThis = (PRTHTTPSERVERINTERNAL)RTMemAllocZ(sizeof(RTHTTPSERVERINTERNAL));
    if (pThis)
    {
        pThis->u32Magic  = RTHTTPSERVER_MAGIC;
        pThis->Callbacks = *pCallbacks;
        pThis->pvUser    = pvUser;
        pThis->cbUser    = cbUser;

        rc = RTTcpServerCreate(pszAddress, uPort, RTTHREADTYPE_DEFAULT, "httpsrv",
                               rtHttpServerClientThread, pThis /* pvUser */, &pThis->pTCPServer);
        if (RT_SUCCESS(rc))
        {
            *hHttpServer = (RTHTTPSERVER)pThis;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

RTR3DECL(int) RTHttpServerDestroy(RTHTTPSERVER hHttpServer)
{
    if (hHttpServer == NIL_RTHTTPSERVER)
        return VINF_SUCCESS;

    PRTHTTPSERVERINTERNAL pThis = hHttpServer;
    RTHTTPSERVER_VALID_RETURN(pThis);

    AssertPtr(pThis->pTCPServer);

    int rc = VINF_SUCCESS;

    PRTHTTPSERVERCALLBACKS pCallbacks = &pThis->Callbacks;
    if (pCallbacks->pfnDestroy)
    {
        RTHTTPCALLBACKDATA Data = { NULL /* pClient */, pThis->pvUser, pThis->cbUser };
        rc = pCallbacks->pfnDestroy(&Data);
    }

    if (RT_SUCCESS(rc))
    {
        rc = RTTcpServerDestroy(pThis->pTCPServer);
        if (RT_SUCCESS(rc))
        {
            pThis->u32Magic = RTHTTPSERVER_MAGIC_DEAD;

            RTMemFree(pThis);
        }
    }

    return rc;
}
