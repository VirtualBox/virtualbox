/* $Id: RTCRestClientApiBaseOci.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestClientApiBase implementation, OCI specific bits.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_REST
#include <iprt/cpp/restclient.h>

#include <iprt/assert.h>
#include <iprt/base64.h>
#include <iprt/errcore.h>
#include <iprt/http.h>
#include <iprt/log.h>
#include <iprt/sha.h>
#include <iprt/time.h>
#include <iprt/uri.h>



/**
 * Ensures that we've got an 'X-Date' or 'Date' header.
 *
 * @returns IPRT status code.
 * @param   hHttp       The HTTP client handle.
 * @param   pvContent
 */
static int ociSignRequestEnsureDateOrXDate(RTHTTP hHttp) RT_NOEXCEPT
{
    if (RTHttpGetHeader(hHttp, RT_STR_TUPLE("x-date")))
        return VINF_SUCCESS;
    if (RTHttpGetHeader(hHttp, RT_STR_TUPLE("date")))
        return VINF_SUCCESS;

    RTTIMESPEC NowSpec;
    RTTIME     Now;
    char       szDate[RTTIME_RFC2822_LEN];
    ssize_t cch = RTTimeToRfc2822(RTTimeExplode(&Now, RTTimeNow(&NowSpec)), szDate, sizeof(szDate), RTTIME_RFC2822_F_GMT);
    AssertRCReturn((int)cch, (int)cch);

    return RTHttpAddHeader(hHttp, "x-date", szDate, cch, RTHTTPADDHDR_F_BACK);
}


/**
 * Ensures that we've got a 'x-content-sha256' header.
 *
 * @returns IPRT status code.
 * @param   hHttp       The HTTP client handle.
 * @param   pvContent
 */
static int ociSignRequestEnsureXContentSha256(RTHTTP hHttp, void const *pvContent, size_t cbContent) RT_NOEXCEPT
{
    if (RTHttpGetHeader(hHttp, RT_STR_TUPLE("x-content-sha256")))
        return VINF_SUCCESS;

#ifdef RT_STRICT
    if (cbContent != 0)
    {
        const char *pszContentLength = RTHttpGetHeader(hHttp, RT_STR_TUPLE("Content-Length"));
        Assert(pszContentLength);
        AssertMsg(!pszContentLength || RTStrToUInt64(pszContentLength) == cbContent,
                  ("'%s' vs %RU64\n", pszContentLength, cbContent));
    }
#endif

    uint8_t abHash[RTSHA256_HASH_SIZE];
    RTSha256(pvContent, cbContent, abHash);

    char szBase64[RTSHA256_DIGEST_LEN + 1]; /* (base64 should be shorter) */
    int rc = RTBase64EncodeEx(abHash, sizeof(abHash), RTBASE64_FLAGS_NO_LINE_BREAKS, szBase64, sizeof(szBase64), NULL);
    AssertRCReturn(rc, rc);

    return RTHttpAddHeader(hHttp, "x-content-sha256", szBase64, RTSTR_MAX, RTHTTPADDHDR_F_BACK);
}


/**
 * Ensures that we've got a 'Content-Length' header.
 *
 * @returns IPRT status code.
 * @param   hHttp       The HTTP client handle.
 * @param   cbContent   The content length.
 */
static int ociSignRequestEnsureContentLength(RTHTTP hHttp, uint64_t cbContent) RT_NOEXCEPT
{
    if (RTHttpGetHeader(hHttp, RT_STR_TUPLE("Content-Length")))
        return VINF_SUCCESS;
    char    szValue[64];
    ssize_t cchValue = RTStrFormatU64(szValue, sizeof(szValue), cbContent, 10, 0, 0, 0);
    AssertRCReturn((int)cchValue, (int)cchValue);
    return RTHttpAddHeader(hHttp, "Content-Length", szValue, cchValue, RTHTTPADDHDR_F_BACK);
}


/**
 * Ensures that we've got a host header.
 *
 * @returns IPRT status code.
 * @param   hHttp       The HTTP client handle.
 * @param   pszUrl      The URL.
 */
static int ociSignRequestEnsureHost(RTHTTP hHttp, const char *pszUrl) RT_NOEXCEPT
{
    if (RTHttpGetHeader(hHttp, RT_STR_TUPLE("host")))
        return VINF_SUCCESS;

    RTURIPARSED ParsedUrl;
    int rc = RTUriParse(pszUrl, &ParsedUrl);
    AssertRCReturn(rc, rc);

    return RTHttpAddHeader(hHttp, "host", &pszUrl[ParsedUrl.offAuthorityHost], ParsedUrl.cchAuthorityHost, RTHTTPADDHDR_F_BACK);
}


int RTCRestClientApiBase::ociSignRequest(RTHTTP a_hHttp, RTCString const &a_rStrFullUrl, RTHTTPMETHOD a_enmHttpMethod,
                                         RTCString const &a_rStrXmitBody, uint32_t a_fFlags,
                                         RTCRKEY a_hKey, RTCString const &a_rStrKeyId) RT_NOEXCEPT
{
    /*
     * First make sure required headers are present, adding them as needed.
     */
    int rc = ociSignRequestEnsureHost(a_hHttp, a_rStrFullUrl.c_str());
    if (RT_SUCCESS(rc))
    {
        bool fHasBody
            =  a_rStrXmitBody.isNotEmpty()
               /* but sometimes we need an empty body signed too */
            || (a_fFlags & kDoCall_RequireBody)
            || a_enmHttpMethod == RTHTTPMETHOD_POST
            || a_enmHttpMethod == RTHTTPMETHOD_PUT;

        if (fHasBody)
        {
            rc = ociSignRequestEnsureContentLength(a_hHttp, a_rStrXmitBody.length());
            if (RT_SUCCESS(rc))
                rc = ociSignRequestEnsureXContentSha256(a_hHttp, a_rStrXmitBody.c_str(), a_rStrXmitBody.length());
        }
        if (RT_SUCCESS(rc))
            rc = ociSignRequestEnsureDateOrXDate(a_hHttp);
        if (RT_SUCCESS(rc))
        {
            /*
             * Do the signing.
             */
            rc = RTHttpSignHeaders(a_hHttp, a_enmHttpMethod, a_rStrFullUrl.c_str(), a_hKey, a_rStrKeyId.c_str(), 0 /*fFlags*/);
        }
    }
    return rc;
}

