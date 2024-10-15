/* $Id: http-common.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Common (client / server) HTTP API.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_http_common_h
#define IPRT_INCLUDED_http_common_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/list.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** HTTP methods. */
typedef enum RTHTTPMETHOD
{
    RTHTTPMETHOD_INVALID = 0,
    RTHTTPMETHOD_GET,
    RTHTTPMETHOD_PUT,
    RTHTTPMETHOD_POST,
    RTHTTPMETHOD_PATCH,
    RTHTTPMETHOD_DELETE,
    RTHTTPMETHOD_HEAD,
    RTHTTPMETHOD_OPTIONS,
    RTHTTPMETHOD_TRACE,
#ifdef IPRT_HTTP_WITH_WEBDAV
    RTHTTPMETHOD_PROPFIND,
#endif
    RTHTTPMETHOD_END,
    RTHTTPMETHOD_32BIT_HACK = 0x7fffffff
} RTHTTPMETHOD;

/** HTTP status codes. */
typedef enum RTHTTPSTATUS
{
    RTHTTPSTATUS_INTERNAL_NOT_SET              = 0,
    /**
     * 2xx - Success / information codes.
     */
    RTHTTPSTATUS_OK                            = 200,
    RTHTTPSTATUS_CREATED                       = 201,
    RTHTTPSTATUS_ACCEPTED                      = 202,
    RTHTTPSTATUS_NONAUTHORITATIVEINFORMATION   = 203,
    RTHTTPSTATUS_NOCONTENT                     = 204,
    RTHTTPSTATUS_RESETCONTENT                  = 205,
    RTHTTPSTATUS_PARTIALCONTENT                = 206,
    RTHTTPSTATUS_MULTISTATUS                   = 207,
    RTHTTPSTATUS_ALREADYREPORTED               = 208,
    RTHTTPSTATUS_IMUSED                        = 226,
    /**
     * 4xx - Client error codes.
     */
    RTHTTPSTATUS_BADREQUEST                    = 400,
    RTHTTPSTATUS_UNAUTHORIZED                  = 401,
    RTHTTPSTATUS_PAYMENTREQUIRED               = 402,
    RTHTTPSTATUS_FORBIDDEN                     = 403,
    RTHTTPSTATUS_NOTFOUND                      = 404,
    RTHTTPSTATUS_METHODNOTALLOWED              = 405,
    RTHTTPSTATUS_NOTACCEPTABLE                 = 406,
    RTHTTPSTATUS_PROXYAUTHENTICATIONREQUIRED   = 407,
    RTHTTPSTATUS_REQUESTTIMEOUT                = 408,
    RTHTTPSTATUS_CONFLICT                      = 409,
    RTHTTPSTATUS_GONE                          = 410,
    RTHTTPSTATUS_LENGTHREQUIRED                = 411,
    RTHTTPSTATUS_PRECONDITIONFAILED            = 412,
    RTHTTPSTATUS_PAYLOADTOOLARGE               = 413,
    RTHTTPSTATUS_URITOOLONG                    = 414,
    RTHTTPSTATUS_UNSUPPORTEDMEDIATYPE          = 415,
    RTHTTPSTATUS_RANGENOTSATISFIABLE           = 416,
    RTHTTPSTATUS_EXPECTATIONFAILED             = 417,
    RTHTTPSTATUS_IMATEAPOT                     = 418,
    RTHTTPSTATUS_UNPROCESSABLEENTITY           = 422,
    RTHTTPSTATUS_LOCKED                        = 423,
    RTHTTPSTATUS_FAILEDDEPENDENCY              = 424,
    RTHTTPSTATUS_UPGRADEREQUIRED               = 426,
    RTHTTPSTATUS_PRECONDITIONREQUIRED          = 428,
    RTHTTPSTATUS_TOOMANYREQUESTS               = 429,
    RTHTTPSTATUS_REQUESTHEADERFIELDSTOOLARGE   = 431,
    RTHTTPSTATUS_UNAVAILABLEFORLEGALREASONS    = 451,
    /**
     * 5xx - Server error codes.
     */
    RTHTTPSTATUS_INTERNALSERVERERROR           = 500,
    RTHTTPSTATUS_NOTIMPLEMENTED                = 501,
    RTHTTPSTATUS_BADGATEWAY                    = 502,
    RTHTTPSTATUS_SERVICEUNAVAILABLE            = 503,
    RTHTTPSTATUS_GATEWAYTIMEOUT                = 504,
    RTHTTPSTATUS_HTTPVERSIONNOTSUPPORTED       = 505,
    RTHTTPSTATUS_VARIANTALSONEGOTIATES         = 506,
    RTHTTPSTATUS_INSUFFICIENTSTORAGE           = 507,
    RTHTTPSTATUS_LOOPDETECTED                  = 508,
    RTHTTPSTATUS_NOTEXTENDED                   = 510,
    RTHTTPSTATUS_NETWORKAUTHENTICATIONREQUIRED = 511,

    RTHTTPSTATUS_32BIT_HACK                    = 0x7fffffff
} RTHTTPSTATUS;

/** Checks whether a HTTP status is of type "informational" or not. */
#define RTHTTPSTATUS_IS_INFO(a_Code)        (a_Code >= 100 && a_Code < 200)
/** Checks whether a HTTP status indicates success or not. */
#define RTHTTPSTATUS_IS_OK(a_Code)          (a_Code >= 200 && a_Code < 300)
/** Checks whether a HTTP status indicates a redirection or not. */
#define RTHTTPSTATUS_IS_REDIRECT(a_Code)    (a_Code >= 300 && a_Code < 400)
/** Checks whether a HTTP status indicates a client error or not. */
#define RTHTTPSTATUS_IS_CLIENTERROR(a_Code) (a_Code >= 400 && a_Code < 500)
/** Checks whether a HTTP status indicates a server error or not. */
#define RTHTTPSTATUS_IS_SERVERERROR(a_Code) (a_Code >= 500 && a_Code < 600)
/** Checks whether a HTTP status indicates an error or not. */
#define RTHTTPSTATUS_IS_ERROR(a_Code)       (a_Code >= 400)

/** Specifies a HTTP MIME type. */
typedef uint32_t RTHTTPMIMETYPE;

#define RTHTTPMIMETYPE_TEXT_PLAIN               "text/plain"
#define RTHTTPMIMETYPE_APPLICATION_OCTET_STREAM "application/octet-stream"

/** Specifies HTTP version 1.1 as a string. */
#define RTHTTPVER_1_1_STR                       "HTTP/1.1"

/** @todo the following three definitions may move the iprt/types.h later. */
/** HTTP header list handle. */
typedef R3PTRTYPE(struct RTHTTPHEADERLISTINTERNAL *) RTHTTPHEADERLIST;
/** Pointer to a HTTP header list handle. */
typedef RTHTTPHEADERLIST                            *PRTHTTPHEADERLIST;
/** Nil HTTP HTTP header list handle. */
#define NIL_RTHTTPHEADERLIST                         ((RTHTTPHEADERLIST)0)

/**
 * HTTP header list entry.
 */
typedef struct RTHTTPHEADERENTRY
{
    /** The list node. */
    RTLISTNODE          Node;
    /** The field name length. */
    uint32_t            cchName;
    /** The value offset. */
    uint32_t            offValue;
    /** The full header field. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RT_GCC_EXTENSION char szData[RT_FLEXIBLE_ARRAY];
} RTHTTPHEADERENTRY;
/** Pointer to a HTTP header. */
typedef RTHTTPHEADERENTRY *PRTHTTPHEADERENTRY;

/**
 * Structure for maintaining a HTTP body.
 */
typedef struct RTHTTPBODY
{
    /** Body to send, if any. Can be NULL. */
    void            *pvBody;
    /** Body allocation size (in bytes). */
    size_t           cbBodyAlloc;
    /** How much body data is being used (in bytes). */
    size_t           cbBodyUsed;
    /** Current body data read/write offset (in bytes). */
    size_t           offBody;
} RTHTTPBODY;
/** Pointer to a HTTP body. */
typedef RTHTTPBODY *PRTHTTPBODY;

/**
 * Returns the name of the HTTP method as a string.
 *
 * @returns Read only string.
 * @param   enmMethod       The HTTP method to return string for.
 */
RTR3DECL(const char *) RTHttpMethodToStr(RTHTTPMETHOD enmMethod);

/**
 * Returns the name of the HTTP status as a string.
 *
 * @returns Read only string.
 * @param   enmSts          The HTTP status to return string for.
 */
RTR3DECL(const char *) RTHttpStatusToStr(RTHTTPSTATUS enmSts);

/**
 * Initializes an HTTP header list.
 *
 * @returns VBox status code.
 * @param   hHdrList            Header list to initialize.
 */
RTR3DECL(int) RTHttpHeaderListInit(PRTHTTPHEADERLIST hHdrList);

/**
 * Destroys an HTTP header list.
 *
 * @param   hHdrList            Header list to destroy.
 *                              The object will be invalid on return.
 */
RTR3DECL(void) RTHttpHeaderListDestroy(RTHTTPHEADERLIST hHdrList);

/**
 * Set custom raw headers.
 *
 * @returns IPRT status code.
 * @param   hHdrLst         The HTTP header list handle.
 * @param   cHeaders        Number of custom headers.
 * @param   papszHeaders    Array of headers in form "foo: bar".
 */
RTR3DECL(int) RTHttpHeaderListSet(RTHTTPHEADERLIST hHdrLst, size_t cHeaders, const char * const *papszHeaders);

/** @name RTHTTPHEADERLISTADD_F_XXX - Flags for RTHttpHeaderListAddRaw and RTHttpHeaderListAdd
 * @{ */
#define RTHTTPHEADERLISTADD_F_BACK     UINT32_C(0) /**< Append the header. */
#define RTHTTPHEADERLISTADD_F_FRONT    UINT32_C(1) /**< Prepend the header. */
/** @} */

/**
 * Adds a raw header.
 *
 * @returns IPRT status code.
 * @param   hHdrLst         The HTTP header list handle.
 * @param   pszHeader       Header string on the form "foo: bar".
 * @param   fFlags          RTHTTPADDHDR_F_FRONT or RTHTTPADDHDR_F_BACK.
 */
RTR3DECL(int) RTHttpHeaderListAddRaw(RTHTTPHEADERLIST hHdrLst, const char *pszHeader, uint32_t fFlags);

/**
 * Adds a header field and value.
 *
 * @returns IPRT status code.
 * @param   hHdrLst         The HTTP header list handle.
 * @param   pszField        The header field name.
 * @param   pszValue        The header field value.
 * @param   cchValue        The value length or RTSTR_MAX.
 * @param   fFlags          Only RTHTTPADDHDR_F_FRONT or RTHTTPADDHDR_F_BACK,
 *                          may be extended with encoding controlling flags if
 *                          needed later.
 */
RTR3DECL(int) RTHttpHeaderListAdd(RTHTTPHEADERLIST hHdrLst, const char *pszField, const char *pszValue, size_t cchValue, uint32_t fFlags);

/**
 * Gets a header previously added using RTHttpSetHeaders, RTHttpAppendRawHeader
 * or RTHttpAppendHeader.
 *
 * @returns Pointer to the header value on if found, otherwise NULL.
 * @param   hHdrLst         The HTTP header list handle.
 * @param   pszField        The field name (no colon).
 * @param   cchField        The length of the field name or RTSTR_MAX.
 */
RTR3DECL(const char *) RTHttpHeaderListGet(RTHTTPHEADERLIST hHdrLst, const char *pszField, size_t cchField);

/**
 * Gets the number of headers specified by RTHttpAddHeader, RTHttpAddRawHeader or RTHttpSetHeaders.
 *
 * @returns Number of headers.
 * @param   hHdrLst         The HTTP header list handle.
 * @note    This can be slow and is only really intended for test cases and debugging!
 */
RTR3DECL(size_t)    RTHttpHeaderListGetCount(RTHTTPHEADERLIST hHdrLst);

/**
 * Gets a header by ordinal.
 *
 * Can be used together with RTHttpGetHeaderCount by test case and debug code to
 * iterate headers specified by RTHttpAddHeader, RTHttpAddRawHeader or RTHttpSetHeaders.
 *
 * @returns The header string ("field: value").
 * @param   hHdrLst         The HTTP header list handle.
 * @param   iOrdinal        The number of the header to get.
 * @note    This can be slow and is only really intended for test cases and debugging!
 */
RTR3DECL(const char *) RTHttpHeaderListGetByOrdinal(RTHTTPHEADERLIST hHdrLst, size_t iOrdinal);

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_http_common_h */

