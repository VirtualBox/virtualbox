/* $Id: RTCRestClientApiBase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestClientApiBase implementation.
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
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/http.h>
#include <iprt/log.h>
#include <iprt/uri.h>


/**
 * Default constructor.
 */
RTCRestClientApiBase::RTCRestClientApiBase() RT_NOEXCEPT
    : m_hHttp(NIL_RTHTTP)
{
}


/**
 * The destructor.
 */
RTCRestClientApiBase::~RTCRestClientApiBase()
{
    if (m_hHttp != NIL_RTHTTP)
    {
        int rc = RTHttpDestroy(m_hHttp);
        AssertRC(rc);
        m_hHttp = NIL_RTHTTP;
    }
}


int RTCRestClientApiBase::setCAFile(const char *pcszCAFile) RT_NOEXCEPT
{
    return m_strCAFile.assignNoThrow(pcszCAFile);
}


int RTCRestClientApiBase::setCAFile(const RTCString &strCAFile) RT_NOEXCEPT
{
    return m_strCAFile.assignNoThrow(strCAFile);
}


const char *RTCRestClientApiBase::getServerUrl(void) const RT_NOEXCEPT
{
    if (m_strServerUrl.isEmpty())
        return getDefaultServerUrl();
    return m_strServerUrl.c_str();
}


int RTCRestClientApiBase::setServerUrl(const char *a_pszUrl) RT_NOEXCEPT
{
#ifdef RT_STRICT
    if (a_pszUrl)
    {
        RTURIPARSED Parsed;
        int rc = RTUriParse(a_pszUrl, &Parsed);
        AssertRC(rc);
    }
#endif

    return m_strServerUrl.assignNoThrow(a_pszUrl);
}


int RTCRestClientApiBase::setServerUrlPart(const char *a_pszServerUrl, size_t a_offDst, size_t a_cchDst,
                                           const char *a_pszSrc, size_t a_cchSrc) RT_NOEXCEPT
{
    if (   a_cchDst == a_cchSrc
        && memcmp(&a_pszServerUrl[0], a_pszSrc, a_cchSrc) == 0)
        return VINF_SUCCESS;

    if (m_strServerUrl.isEmpty())
    {
        int rc = m_strServerUrl.assignNoThrow(a_pszServerUrl);
        AssertRCReturn(rc, rc);
    }
    return m_strServerUrl.replaceNoThrow(a_offDst, a_cchDst, a_pszSrc, a_cchSrc);
}


int RTCRestClientApiBase::setServerScheme(const char *a_pszScheme) RT_NOEXCEPT
{
    /*
     * Validate.
     */
    AssertReturn(a_pszScheme, VERR_INVALID_POINTER);
    size_t const cchScheme = strlen(a_pszScheme);
    AssertReturn(cchScheme > 0, VERR_INVALID_PARAMETER);
    Assert(cchScheme < 16);
#ifdef RT_STRICT
    for (size_t i = 0; i < cchScheme; i++)
        Assert(RT_C_IS_ALNUM(a_pszScheme[i]));
#endif

    /*
     * Parse, compare & replace.
     */
    RTURIPARSED Parsed;
    const char *pszUrl = getServerUrl();
    int rc = RTUriParse(pszUrl, &Parsed);
    AssertRCReturn(rc, rc);
    return setServerUrlPart(pszUrl, 0, Parsed.cchScheme, a_pszScheme, cchScheme);
}


int RTCRestClientApiBase::setServerAuthority(const char *a_pszAuthority) RT_NOEXCEPT
{
    /*
     * Validate.
     */
    AssertReturn(a_pszAuthority, VERR_INVALID_POINTER);
    size_t const cchAuthority = strlen(a_pszAuthority);
    AssertReturn(cchAuthority > 0, VERR_INVALID_PARAMETER);
    Assert(memchr(a_pszAuthority, '/', cchAuthority) == NULL);
    Assert(memchr(a_pszAuthority, '\\', cchAuthority) == NULL);
    Assert(memchr(a_pszAuthority, '#', cchAuthority) == NULL);
    Assert(memchr(a_pszAuthority, '?', cchAuthority) == NULL);

    /*
     * Parse, compare & replace.
     */
    RTURIPARSED Parsed;
    const char *pszUrl = getServerUrl();
    int rc = RTUriParse(pszUrl, &Parsed);
    AssertRCReturn(rc, rc);
    return setServerUrlPart(pszUrl, Parsed.offAuthority, Parsed.cchAuthority, a_pszAuthority, cchAuthority);
}


int RTCRestClientApiBase::setServerBasePath(const char *a_pszBasePath) RT_NOEXCEPT
{
    /*
     * Validate.
     */
    AssertReturn(a_pszBasePath, VERR_INVALID_POINTER);
    size_t const cchBasePath = strlen(a_pszBasePath);
    AssertReturn(cchBasePath > 0, VERR_INVALID_PARAMETER);
    Assert(memchr(a_pszBasePath, '?', cchBasePath) == NULL);
    Assert(memchr(a_pszBasePath, '#', cchBasePath) == NULL);

    /*
     * Parse, compare & replace.
     */
    RTURIPARSED Parsed;
    const char *pszUrl = getServerUrl();
    int rc = RTUriParse(pszUrl, &Parsed);
    AssertRCReturn(rc, rc);
    return setServerUrlPart(pszUrl, Parsed.offPath, Parsed.cchPath, a_pszBasePath, cchBasePath);
}


int RTCRestClientApiBase::reinitHttpInstance() RT_NOEXCEPT
{
    if (m_hHttp != NIL_RTHTTP)
        return RTHttpReset(m_hHttp, 0 /*fFlags*/);

    int rc = RTHttpCreate(&m_hHttp);
    if (RT_SUCCESS(rc) && m_strCAFile.isNotEmpty())
        rc = RTHttpSetCAFile(m_hHttp, m_strCAFile.c_str());

    if (RT_FAILURE(rc) && m_hHttp != NIL_RTHTTP)
    {
        RTHttpDestroy(m_hHttp);
        m_hHttp = NIL_RTHTTP;
    }
    return rc;
}


int RTCRestClientApiBase::xmitReady(RTHTTP a_hHttp, RTCString const &a_rStrFullUrl, RTHTTPMETHOD a_enmHttpMethod,
                                    RTCString const &a_rStrXmitBody, uint32_t a_fFlags) RT_NOEXCEPT
{
    RT_NOREF(a_hHttp, a_rStrFullUrl, a_enmHttpMethod, a_rStrXmitBody, a_fFlags);
    return VINF_SUCCESS;
}


int RTCRestClientApiBase::doCall(RTCRestClientRequestBase const &a_rRequest, RTHTTPMETHOD a_enmHttpMethod,
                                 RTCRestClientResponseBase *a_pResponse, const char *a_pszMethod, uint32_t a_fFlags) RT_NOEXCEPT
{
    LogFlow(("doCall: %s %s\n", a_pszMethod, RTHttpMethodToStr(a_enmHttpMethod)));


    /*
     * Reset the response object (allowing reuse of such) and check the request
     * object for assignment errors.
     */
    int    rc;
    RTHTTP hHttp = NIL_RTHTTP;

    a_pResponse->reset();
    if (!a_rRequest.hasAssignmentErrors())
    {
        /*
         * Initialize the HTTP instance.
         */
        rc = reinitHttpInstance();
        if (RT_SUCCESS(rc))
        {
            hHttp = m_hHttp;
            Assert(hHttp != NIL_RTHTTP);

            /*
             * Prepare the response side.
             */
            rc = a_pResponse->receivePrepare(hHttp);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Prepare the request for the transmission.
                 */
                RTCString strExtraPath;
                RTCString strQuery;
                RTCString strXmitBody;
                rc = a_rRequest.xmitPrepare(&strExtraPath, &strQuery, hHttp, &strXmitBody);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Construct the full URL.
                     */
                    RTCString strFullUrl;
                    rc = strFullUrl.assignNoThrow(getServerUrl());
                    if (strExtraPath.isNotEmpty())
                    {
                        if (!strExtraPath.startsWith("/") && !strFullUrl.endsWith("/") && RT_SUCCESS(rc))
                            rc = strFullUrl.appendNoThrow('/');
                        if (RT_SUCCESS(rc))
                            rc = strFullUrl.appendNoThrow(strExtraPath);
                        strExtraPath.setNull();
                    }
                    if (strQuery.isNotEmpty())
                    {
                        Assert(strQuery.startsWith("?"));
                        rc = strFullUrl.appendNoThrow(strQuery);
                        strQuery.setNull();
                    }
                    if (RT_SUCCESS(rc))
                    {
                        rc = xmitReady(hHttp, strFullUrl, a_enmHttpMethod, strXmitBody, a_fFlags);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Perform HTTP request.
                             */
                            uint32_t uHttpStatus = 0;
                            size_t   cbBody      = 0;
                            void    *pvBody      = NULL;
                            rc = RTHttpPerform(hHttp, strFullUrl.c_str(), a_enmHttpMethod,
                                               strXmitBody.c_str(), strXmitBody.length(),
                                               &uHttpStatus, NULL /*ppvHdrs*/, NULL /*pcbHdrs*/, &pvBody, &cbBody);
                            if (RT_SUCCESS(rc))
                            {
                                a_rRequest.xmitComplete(uHttpStatus, hHttp);

                                /*
                                 * Do response processing.
                                 */
                                a_pResponse->receiveComplete(uHttpStatus, hHttp);
                                a_pResponse->consumeBody((const char *)pvBody, cbBody);
                                if (pvBody)
                                    RTHttpFreeResponse(pvBody);
                                a_pResponse->receiveFinal();

                                return a_pResponse->getStatus();
                            }
                        }
                    }
                }
                a_rRequest.xmitComplete(rc, hHttp);
            }
        }
    }
    else
        rc = VERR_NO_MEMORY;

    a_pResponse->receiveComplete(rc, hHttp);
    RT_NOREF_PV(a_pszMethod);

    return a_pResponse->getStatus();
}

