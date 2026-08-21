/* $Id: http-server.h 115102 2026-08-21 11:14:19Z andreas.loeffler@oracle.com $ */
/** @file
 * Header file for HTTP server implementation.
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

#ifndef IPRT_INCLUDED_http_server_h
#define IPRT_INCLUDED_http_server_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/http-common.h>
#include <iprt/types.h>
#include <iprt/fs.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_httpserver  RTHttpServer - HTTP server implementation.
 * @ingroup grp_rt
 * @{
 */

/** @todo the following three definitions may move the iprt/types.h later. */
/** HTTP server handle. */
typedef R3PTRTYPE(struct RTHTTPSERVERINTERNAL *) RTHTTPSERVER;
/** Pointer to a HTTP server handle. */
typedef RTHTTPSERVER                            *PRTHTTPSERVER;
/** Nil HTTP client handle. */
#define NIL_RTHTTPSERVER                         ((RTHTTPSERVER)0)

/**
 * Final result of processing an HTTP server request.
 *
 * The result is finalized after the request method handler has finished and
 * can be queried by RTHTTPSERVERCALLBACKS::pfnRequestEnd using
 * RTHttpServerRequestQueryResult().
 */
typedef struct RTHTTPSERVERREQRESULT
{
    /** Result of processing the request.  The high-level GET handler preserves
     *  connection errors here even when legacy server behavior tolerates them. */
    int             rcRequest;
    /** RTHTTPSERVERREQRESULT_F_XXX. */
    uint32_t        fFlags;
    /** Number of response-body bytes successfully written to the client by
     *  the high-level GET handler; zero for handlers which do not report it. */
    uint64_t        cbBodySent;
} RTHTTPSERVERREQRESULT;
/** Pointer to a final HTTP server request result. */
typedef RTHTTPSERVERREQRESULT *PRTHTTPSERVERREQRESULT;
/** Pointer to a const final HTTP server request result. */
typedef RTHTTPSERVERREQRESULT const *PCRTHTTPSERVERREQRESULT;

/** The high-level GET handler successfully wrote the complete intended
 *  response body and closed the resource. */
#define RTHTTPSERVERREQRESULT_F_BODY_COMPLETE    RT_BIT_32(0)
/** Valid RTHTTPSERVERREQRESULT::fFlags bits. */
#define RTHTTPSERVERREQRESULT_F_VALID_MASK       UINT32_C(0x00000001)

/**
 * Structure for maintaining a HTTP client request.
 *
 * @note The layout is part of the callback ABI and must not be changed.
 */
typedef struct RTHTTPSERVERREQ
{
    /** Request URL. Unmodified (i.e. escaped by HTTP client). */
    char            *pszUrl;
    /** Request method. */
    RTHTTPMETHOD     enmMethod;
    /** Request header list. */
    RTHTTPHEADERLIST hHdrLst;
    /** Request body data. */
    RTHTTPBODY       Body;
    /** User-supplied (opaque) pointer.
     *  Can be used for faster lookups between callbacks. */
    void            *pvUser;
} RTHTTPSERVERREQ;
/** Pointer to a HTTP client request. */
typedef RTHTTPSERVERREQ *PRTHTTPSERVERREQ;

/**
 * Structure for maintaining a HTTP server response.
 */
typedef struct RTHTTPSERVERRESP
{
    /** HTTP status to send. */
    RTHTTPSTATUS     enmSts;
    /** List of headers to send. */
    RTHTTPHEADERLIST hHdrLst;
    /** Body data to send. */
    RTHTTPBODY       Body;
} RTHTTPSERVERRESP;
/** Pointer to a HTTP server response. */
typedef RTHTTPSERVERRESP *PRTHTTPSERVERRESP;

RTR3DECL(int)  RTHttpServerResponseInitEx(PRTHTTPSERVERRESP pResp, size_t cbBody);
RTR3DECL(int)  RTHttpServerResponseInit(PRTHTTPSERVERRESP pResp);
RTR3DECL(void) RTHttpServerResponseDestroy(PRTHTTPSERVERRESP pResp);

/**
 * Structure for maintaining a HTTP server client state.
 *
 * Note: The HTTP protocol itself is stateless, but we want to have to possibility to store
 *       some state stuff here nevertheless.
 */
typedef struct RTHTTPSERVERCLIENTSTATE
{
    /** If non-zero, the time (in ms) to keep a client connection alive.
     *  Requested via client header, but set and controlled by the server in the end. */
    RTMSINTERVAL msKeepAlive;
} RTHTTPSERVERCLIENTSTATE;
/** Pointer to a FTP server client state. */
typedef RTHTTPSERVERCLIENTSTATE *PRTHTTPSERVERCLIENTSTATE;

/**
 * Structure for storing HTTP server callback data.
 */
typedef struct RTHTTPCALLBACKDATA
{
    /** Pointer to the client state. */
    PRTHTTPSERVERCLIENTSTATE pClient;
    /** Saved user pointer. */
    void                    *pvUser;
    /** Size (in bytes) of data at user pointer. */
    size_t                   cbUser;
} RTHTTPCALLBACKDATA;
/** Pointer to HTTP server callback data. */
typedef RTHTTPCALLBACKDATA *PRTHTTPCALLBACKDATA;

/**
 * Function callback table for the HTTP server implementation.
 *
 * All callbacks are optional and therefore can be NULL.
 */
typedef struct RTHTTPSERVERCALLBACKS
{
    /**
     * Called before beginning to process a request. Guaranteed.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to HTTP callback data.
     * @param   pReq            Pointer to request to handle.
     */
    DECLCALLBACKMEMBER(int, pfnRequestBegin,(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq));
    /**
     * Called after processing a request. Guaranteed.  The final method result
     * can be queried using RTHttpServerRequestQueryResult().
     *
     * @returns VBox status code.
     * @param   pData           Pointer to HTTP callback data.
     * @param   pReq            Pointer to request to handle.
     */
    DECLCALLBACKMEMBER(int, pfnRequestEnd,(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq));
    /**
     * Opens a resource for a GET request.
     *
     * Will be called as as a second function for a GET request.
     *
     * Note: High level function, not being called when pfnOnGetRequest is implemented.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to HTTP callback data.
     * @param   pReq            Pointer to request to handle.
     * @param   ppvHandle       Where to return the pointer to the opaque handle used for object identification.
     */
    DECLCALLBACKMEMBER(int, pfnOpen,(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void **ppvHandle));
    /**
     * Reads a resource for a GET request.
     *
     * Note:  High level function, not being called when pfnOnGetRequest is implemented.
     * Note2: Can be called multiple times, based on the body size to send.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to HTTP callback data.
     * @param   pReq            Pointer to request to handle.
     * @param   pvHandle        Opaque handle for object identification.
     * @param   pvBuf           Pointer to buffer where to store the read data.
     * @param   cbBuf           Size (in bytes) of the buffer where to store the read data.
     * @param   pcbRead         Where to return the amount (in bytes) of read data. Optional and can be NULL.
     */
    DECLCALLBACKMEMBER(int, pfnRead,(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void *pvHandle, void *pvBuf, size_t cbBuf, size_t *pcbRead));
    /**
     * Closes a resouce for a GET a request.
     *
     * Note: High level function, not being called when pfnOnGetRequest is implemented.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to HTTP callback data.
     * @param   pReq            Pointer to request to handle.
     * @param   pvHandle        Opaque handle for object identification.
     */
    DECLCALLBACKMEMBER(int, pfnClose,(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, void *pvHandle));
    /**
     * Queries information about a given URL.
     *
     * Will be called as first function for a GET or HEAD request.
     *
     * Note: High level function, not being called when pfnOnGetRequest is implemented.*
     *
     * @returns VBox status code.
     * @param   pData           Pointer to HTTP callback data.
     * @param   pReq            Pointer to request to handle.
     * @param   pObjInfo        Where to store the queried file information on success.
     * @param   ppszMIMEHint    Where to return an allocated MIME type hint on success.
     *                          Must be free'd by the caller using RTStrFree().
     */
    DECLCALLBACKMEMBER(int, pfnQueryInfo,(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq, PRTFSOBJINFO pObjInfo, char **ppszMIMEHint));
    /**
     * Low-level handler for a GET method request.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to HTTP callback data.
     * @param   pReq            Pointer to request to handle.
     */
    DECLCALLBACKMEMBER(int, pfnOnGetRequest,(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq));
    /**
     * Low-level handler for a HEAD method request.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to HTTP callback data.
     * @param   pReq            Pointer to request to handle.
     */
    DECLCALLBACKMEMBER(int, pfnOnHeadRequest,(PRTHTTPCALLBACKDATA pData, PRTHTTPSERVERREQ pReq));
    /**
     * Called before the HTTP server will be destroyed.
     *
     * @returns VBox status code.
     * @param   pData           Pointer to HTTP callback data.
     */
    DECLCALLBACKMEMBER(int, pfnDestroy,(PRTHTTPCALLBACKDATA pData));
} RTHTTPSERVERCALLBACKS;
/** Pointer to a HTTP server callback data table. */
typedef RTHTTPSERVERCALLBACKS *PRTHTTPSERVERCALLBACKS;

/**
 * Queries the final result of processing a request.
 *
 * This may only be called for the request passed to
 * RTHTTPSERVERCALLBACKS::pfnRequestEnd and before that callback returns.
 * Using a query keeps both RTHTTPSERVERREQ and the callback table ABI stable.
 *
 * @returns IPRT status code.
 * @retval  VERR_WRONG_ORDER if the request result is not finalized.
 * @param   pReq            The request passed to pfnRequestEnd.
 * @param   pResult         Where to return the final request result.
 */
RTR3DECL(int) RTHttpServerRequestQueryResult(PRTHTTPSERVERREQ pReq, PRTHTTPSERVERREQRESULT pResult);

/** Maximum length (in bytes) a single client request can have. */
#define RTHTTPSERVER_MAX_REQ_LEN        _8K
/** EOL string according to the HTTP 1.1 specs.
 *  See https://tools.ietf.org/html/rfc2616#section-2.2 */
#define RTHTTPSERVER_HTTP11_EOL_STR     "\r\n"

/**
 * Creates a HTTP server instance.
 *
 * @returns IPRT status code.
 * @param   phHttpServer        Where to store the HTTP server handle.
 * @param   pcszAddress         The address for creating a listening socket.
 *                              If NULL or empty string the server is bound to all interfaces.
 * @param   uPort               The port for creating a listening socket.
 * @param   pCallbacks          Callback table to use.
 * @param   pvUser              Pointer to user-specific data. Optional.
 * @param   cbUser              Size of user-specific data. Optional.
 */
RTR3DECL(int) RTHttpServerCreate(PRTHTTPSERVER phHttpServer, const char *pcszAddress, uint16_t uPort,
                                 PRTHTTPSERVERCALLBACKS pCallbacks, void *pvUser, size_t cbUser);

/**
 * Destroys a HTTP server instance.
 *
 * @returns IPRT status code.
 * @param   hHttpServer          Handle to the HTTP server handle.
 */
RTR3DECL(int) RTHttpServerDestroy(RTHTTPSERVER hHttpServer);

/** @} */
RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_http_server_h */

