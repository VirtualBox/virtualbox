/* $Id: http.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - HTTP common API.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_HTTP
#include <iprt/http-common.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTHTTPHEADERLIST_VALID_RETURN_RC(hList, a_rc) \
    do { \
        AssertPtrReturn((hList), (a_rc)); \
        AssertReturn((hList)->u32Magic == RTHTTPHEADERLIST_MAGIC, (a_rc)); \
    } while (0)

/** Validates a handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTHTTPHEADERLIST_VALID_RETURN(hList) RTHTTPHEADERLIST_VALID_RETURN_RC((hList), VERR_INVALID_HANDLE)

/** Validates a handle and returns (void) if not valid. */
#define RTHTTPHEADERLIST_VALID_RETURN_VOID(hList) \
    do { \
        AssertPtrReturnVoid(hList); \
        AssertReturnVoid((hList)->u32Magic == RTHTTPHEADERLIST_MAGIC); \
    } while (0)


/*********************************************************************************************************************************
*   Internal structs.                                                                                                            *
*********************************************************************************************************************************/
/**
 * HTTP header list, internal definition.
 */
typedef struct RTHTTPHEADERLISTINTERNAL
{
    /** The list node. */
    RTLISTANCHOR        List;
    /** Magic value. */
    uint32_t            u32Magic;
} RTHTTPHEADERLISTINTERNAL;
/** Pointer to an internal HTTP header list. */
typedef RTHTTPHEADERLISTINTERNAL *PRTHTTPHEADERLISTINTERNAL;


/*********************************************************************************************************************************
*   Prototypes.                                                                                                                  *
*********************************************************************************************************************************/
static void rtHttpHeaderListRemoveAll(PRTHTTPHEADERLISTINTERNAL pThis);


/*********************************************************************************************************************************
*   Lookup / conversion functions                                                                                                *
*********************************************************************************************************************************/

RTR3DECL(const char *) RTHttpMethodToStr(RTHTTPMETHOD enmMethod)
{
    switch (enmMethod)
    {
        case RTHTTPMETHOD_INVALID:  return "invalid";
        case RTHTTPMETHOD_GET:      return "GET";
        case RTHTTPMETHOD_PUT:      return "PUT";
        case RTHTTPMETHOD_POST:     return "POST";
        case RTHTTPMETHOD_PATCH:    return "PATCH";
        case RTHTTPMETHOD_DELETE:   return "DELETE";
        case RTHTTPMETHOD_HEAD:     return "HEAD";
        case RTHTTPMETHOD_OPTIONS:  return "OPTIONS";
        case RTHTTPMETHOD_TRACE:    return "TRACE";
#ifdef IPRT_HTTP_WITH_WEBDAV
        case RTHTTPMETHOD_PROPFIND: return "PROPFIND";
#endif
        case RTHTTPMETHOD_END:
            RT_FALL_THROUGH();
        case RTHTTPMETHOD_32BIT_HACK:
            break;
    }
    return "unknown";
}

RTR3DECL(const char *) RTHttpStatusToStr(RTHTTPSTATUS enmSts)
{
    switch (enmSts)
    {
        case RTHTTPSTATUS_OK                           : return "OK";
        case RTHTTPSTATUS_CREATED                      : return "Created";
        case RTHTTPSTATUS_ACCEPTED                     : return "Accepted";
        case RTHTTPSTATUS_NONAUTHORITATIVEINFORMATION  : return "Non-Authoritative Information";
        case RTHTTPSTATUS_NOCONTENT                    : return "No Content";
        case RTHTTPSTATUS_RESETCONTENT                 : return "Reset Content";
        case RTHTTPSTATUS_PARTIALCONTENT               : return "Partial Content";
        case RTHTTPSTATUS_MULTISTATUS                  : return "Multi-Status";
        case RTHTTPSTATUS_ALREADYREPORTED              : return "Already Reported";
        case RTHTTPSTATUS_IMUSED                       : return "IM Used";

        case RTHTTPSTATUS_BADREQUEST                   : return "Bad Request";
        case RTHTTPSTATUS_UNAUTHORIZED                 : return "Unauthorized";
        case RTHTTPSTATUS_PAYMENTREQUIRED              : return "Payment Required";
        case RTHTTPSTATUS_FORBIDDEN                    : return "Forbidden";
        case RTHTTPSTATUS_NOTFOUND                     : return "Not Found";
        case RTHTTPSTATUS_METHODNOTALLOWED             : return "Method Not Allowed";
        case RTHTTPSTATUS_NOTACCEPTABLE                : return "Not Acceptable";
        case RTHTTPSTATUS_PROXYAUTHENTICATIONREQUIRED  : return "Proxy Authentication Required";
        case RTHTTPSTATUS_REQUESTTIMEOUT               : return "Request Timeout";
        case RTHTTPSTATUS_CONFLICT                     : return "Conflict";
        case RTHTTPSTATUS_GONE                         : return "Gone";
        case RTHTTPSTATUS_LENGTHREQUIRED               : return "Length Required";
        case RTHTTPSTATUS_PRECONDITIONFAILED           : return "Precondition Failed";
        case RTHTTPSTATUS_PAYLOADTOOLARGE              : return "Payload Too Large";
        case RTHTTPSTATUS_URITOOLONG                   : return "URI Too Long";
        case RTHTTPSTATUS_UNSUPPORTEDMEDIATYPE         : return "Unsupported Media Type";
        case RTHTTPSTATUS_RANGENOTSATISFIABLE          : return "Range Not Satisfiable";
        case RTHTTPSTATUS_EXPECTATIONFAILED            : return "Expectation Failed";
        case RTHTTPSTATUS_IMATEAPOT                    : return "I'm a teapot";
        case RTHTTPSTATUS_UNPROCESSABLEENTITY          : return "Unprocessable Entity";
        case RTHTTPSTATUS_LOCKED                       : return "Locked";
        case RTHTTPSTATUS_FAILEDDEPENDENCY             : return "Failed Dependency";
        case RTHTTPSTATUS_UPGRADEREQUIRED              : return "Upgrade Required";
        case RTHTTPSTATUS_PRECONDITIONREQUIRED         : return "Precondition Required";
        case RTHTTPSTATUS_TOOMANYREQUESTS              : return "Too Many Requests";
        case RTHTTPSTATUS_REQUESTHEADERFIELDSTOOLARGE  : return "Request Header Fields Too Large";
        case RTHTTPSTATUS_UNAVAILABLEFORLEGALREASONS   : return "Unavailable For Legal Reasons";

        case RTHTTPSTATUS_INTERNALSERVERERROR          : return "Internal Server Error";
        case RTHTTPSTATUS_NOTIMPLEMENTED               : return "Not Implemented";
        case RTHTTPSTATUS_BADGATEWAY                   : return "Bad Gateway";
        case RTHTTPSTATUS_SERVICEUNAVAILABLE           : return "Service Unavailable";
        case RTHTTPSTATUS_GATEWAYTIMEOUT               : return "Gateway Time-out";
        case RTHTTPSTATUS_HTTPVERSIONNOTSUPPORTED      : return "HTTP Version Not Supported";
        case RTHTTPSTATUS_VARIANTALSONEGOTIATES        : return "Variant Also Negotiates";
        case RTHTTPSTATUS_INSUFFICIENTSTORAGE          : return "Insufficient Storage";
        case RTHTTPSTATUS_LOOPDETECTED                 : return "Loop Detected";
        case RTHTTPSTATUS_NOTEXTENDED                  : return "Not Extended";
        case RTHTTPSTATUS_NETWORKAUTHENTICATIONREQUIRED: return "Network Authentication Required";

        default: break;
    }

    AssertFailed();
    return "<Not implemented>";
}


/*********************************************************************************************************************************
*   HTTP Header List                                                                                                             *
*********************************************************************************************************************************/

RTR3DECL(int) RTHttpHeaderListInit(PRTHTTPHEADERLIST hHdrLst)
{
    PRTHTTPHEADERLISTINTERNAL pThis = (PRTHTTPHEADERLISTINTERNAL)RTMemAllocZ(sizeof(RTHTTPHEADERLISTINTERNAL));
    if (pThis)
    {
        pThis->u32Magic = RTHTTPHEADERLIST_MAGIC;

        RTListInit(&pThis->List);

        *hHdrLst = (RTHTTPHEADERLIST)pThis;

        return VINF_SUCCESS;
    }

    return VERR_NO_MEMORY;
}


RTR3DECL(void) RTHttpHeaderListDestroy(RTHTTPHEADERLIST hHdrLst)
{
    PRTHTTPHEADERLISTINTERNAL pThis = hHdrLst;
    RTHTTPHEADERLIST_VALID_RETURN_VOID(pThis);

    rtHttpHeaderListRemoveAll(pThis);

    RTMemFree(hHdrLst);
    hHdrLst = NIL_RTHTTPHEADERLIST;
}


static void rtHttpHeaderListRemoveAll(PRTHTTPHEADERLISTINTERNAL pThis)
{
    PRTHTTPHEADERENTRY pEntry, pNext;
    RTListForEachSafe(&pThis->List, pEntry, pNext, RTHTTPHEADERENTRY, Node)
    {
        RTListNodeRemove(&pEntry->Node);
        RTMemFree(pEntry);
    }
}

/**
 * Worker for RTHttpHeaderListSet and RTHttpHeaderListAdd.
 *
 * @returns IPRT status code.
 * @param   pThis       The HTTP header list instance.
 * @param   pchName     The field name.  Does not need to be terminated.
 * @param   cchName     The field name length.
 * @param   pchValue    The field value.  Does not need to be terminated.
 * @param   cchValue    The field value length.
 * @param   fFlags      RTHTTPADDHDR_F_XXX.
 */
static int rtHttpHeaderListAddWorker(PRTHTTPHEADERLISTINTERNAL pThis,
                                     const char *pchName, size_t cchName, const char *pchValue, size_t cchValue, uint32_t fFlags)
{
    /*
     * Create the list entry.
     */
    size_t             cbData = cchName + 2 + cchValue + 1;
    PRTHTTPHEADERENTRY pHdr   = (PRTHTTPHEADERENTRY)RTMemAlloc(RT_UOFFSETOF_DYN(RTHTTPHEADERENTRY, szData[cbData]));
    if (pHdr)
    {
        pHdr->cchName   = (uint32_t)cchName;
        pHdr->offValue  = (uint32_t)(cchName + 2);
        char *psz = pHdr->szData;
        memcpy(psz, pchName, cchName);
        psz += cchName;
        *psz++ = ':';
        *psz++ = ' ';
        memcpy(psz, pchValue, cchValue);
        psz[cchValue] = '\0';

        /*
         * Appending to an existing list requires no cURL interaction.
         */
        AssertCompile(RTHTTPHEADERLISTADD_F_FRONT != 0);
        if (!(fFlags & RTHTTPHEADERLISTADD_F_FRONT))
        {
            RTListAppend(&pThis->List, &pHdr->Node);
            return VINF_SUCCESS;
        }

        /*
         * When prepending or adding the first header we need to inform cURL
         * about the new list head.
         */
        RTListPrepend(&pThis->List, &pHdr->Node);
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


RTR3DECL(int) RTHttpHeaderListSet(RTHTTPHEADERLIST hHdrLst,
                                  size_t cHeaders, const char * const *papszHeaders)
{
    PRTHTTPHEADERLISTINTERNAL pThis = hHdrLst;
    RTHTTPHEADERLIST_VALID_RETURN(pThis);

    /*
     * Drop old headers and reset state.
     */
    rtHttpHeaderListRemoveAll(pThis);

    /*
     * We're done if no headers specified.
     */
    if (!cHeaders)
        return VINF_SUCCESS;

    /*
     * Add the headers, one by one.
     */
    int rc = VINF_SUCCESS;
    for (size_t i = 0; i < cHeaders; i++)
    {
        const char *pszHeader = papszHeaders[i];
        size_t      cchHeader = strlen(pszHeader);
        size_t      cchName   = (const char *)memchr(pszHeader, ':', cchHeader) - pszHeader;
        AssertBreakStmt(cchName < cchHeader, rc = VERR_INVALID_PARAMETER);
        size_t      offValue  = RT_C_IS_BLANK(pszHeader[cchName + 1]) ? cchName + 2 : cchName + 1;
        rc = rtHttpHeaderListAddWorker(pThis, pszHeader, cchName, &pszHeader[offValue], cchHeader - offValue,
                                       RTHTTPHEADERLISTADD_F_BACK);
        AssertRCBreak(rc);
    }
    if (RT_SUCCESS(rc))
        return rc;
    rtHttpHeaderListRemoveAll(pThis);
    return rc;
}


RTR3DECL(int) RTHttpHeaderListAdd(RTHTTPHEADERLIST hHdrLst,
                                  const char *pszField, const char *pszValue, size_t cchValue, uint32_t fFlags)
{
    /*
     * Validate input and calc string lengths.
     */
    PRTHTTPHEADERLISTINTERNAL pThis = hHdrLst;
    RTHTTPHEADERLIST_VALID_RETURN(pThis);
    AssertReturn(!(fFlags & ~RTHTTPHEADERLISTADD_F_BACK), VERR_INVALID_FLAGS);
    AssertPtr(pszField);
    size_t const cchField = strlen(pszField);
    AssertReturn(cchField > 0, VERR_INVALID_PARAMETER);
    AssertReturn(pszField[cchField - 1] != ':', VERR_INVALID_PARAMETER);
    AssertReturn(!RT_C_IS_SPACE(pszField[cchField - 1]), VERR_INVALID_PARAMETER);
#ifdef RT_STRICT
    for (size_t i = 0; i < cchField; i++)
    {
        char const ch = pszField[i];
        Assert(RT_C_IS_PRINT(ch) && ch != ':');
    }
#endif

    AssertPtr(pszValue);
    if (cchValue == RTSTR_MAX)
        cchValue = strlen(pszValue);

    /*
     * Just pass it along to the worker.
     */
    return rtHttpHeaderListAddWorker(pThis, pszField, cchField, pszValue, cchValue, fFlags);
}


RTR3DECL(const char *) RTHttpHeaderListGet(RTHTTPHEADERLIST hHdrLst, const char *pszField, size_t cchField)
{
    PRTHTTPHEADERLISTINTERNAL pThis = hHdrLst;
    RTHTTPHEADERLIST_VALID_RETURN_RC(pThis, NULL);

    if (cchField == RTSTR_MAX)
        cchField = strlen(pszField);

    PRTHTTPHEADERENTRY pEntry;
    RTListForEach(&pThis->List, pEntry, RTHTTPHEADERENTRY, Node)
    {
        if (   pEntry->cchName == cchField
            && RTStrNICmpAscii(pEntry->szData, pszField, cchField) == 0)
            return &pEntry->szData[pEntry->offValue];
    }
    return NULL;
}


RTR3DECL(size_t) RTHttpHeaderListGetCount(RTHTTPHEADERLIST hHdrLst)
{
    PRTHTTPHEADERLISTINTERNAL pThis = hHdrLst;
    RTHTTPHEADERLIST_VALID_RETURN_RC(pThis, 0);

    /* Note! Only for test cases and debugging, so we don't care about performance. */
    size_t cHeaders = 0;
    PRTHTTPHEADERENTRY pEntry;
    RTListForEach(&pThis->List, pEntry, RTHTTPHEADERENTRY, Node)
        cHeaders++;
    return cHeaders;
}


RTR3DECL(const char *) RTHttpHeaderListGetByOrdinal(RTHTTPHEADERLIST hHdrLst, size_t iOrdinal)
{
    PRTHTTPHEADERLISTINTERNAL pThis = hHdrLst;
    RTHTTPHEADERLIST_VALID_RETURN_RC(pThis, NULL);

    /* Note! Only for test cases and debugging, so we don't care about performance. */
    PRTHTTPHEADERENTRY pEntry;
    RTListForEach(&pThis->List, pEntry, RTHTTPHEADERENTRY, Node)
    {
        if (iOrdinal == 0)
            return pEntry->szData;
        iOrdinal--;
    }

    return NULL;
}

