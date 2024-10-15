/* $Id: RTCRestClientResponseBase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestClientResponseBase implementation.
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

#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/cpp/reststringmap.h>


/**
 * Default constructor.
 */
RTCRestClientResponseBase::RTCRestClientResponseBase() RT_NOEXCEPT
    : m_rcStatus(VERR_WRONG_ORDER)
    , m_rcHttp(VERR_NOT_AVAILABLE)
    , m_pErrInfo(NULL)
{
}


/**
 * Destructor.
 */
RTCRestClientResponseBase::~RTCRestClientResponseBase()
{
    deleteErrInfo();
}


/**
 * Copy constructor.
 */
RTCRestClientResponseBase::RTCRestClientResponseBase(RTCRestClientResponseBase const &a_rThat)
    : m_rcStatus(a_rThat.m_rcStatus)
    , m_rcHttp(a_rThat.m_rcHttp)
    , m_pErrInfo(NULL)
    , m_strContentType(a_rThat.m_strContentType)
{
    if (a_rThat.m_pErrInfo)
        copyErrInfo(a_rThat.m_pErrInfo);
}


/**
 * Copy assignment operator.
 */
RTCRestClientResponseBase &RTCRestClientResponseBase::operator=(RTCRestClientResponseBase const &a_rThat)
{
    m_rcStatus       = a_rThat.m_rcStatus;
    m_rcHttp         = a_rThat.m_rcHttp;
    m_strContentType = a_rThat.m_strContentType;
    if (a_rThat.m_pErrInfo)
        copyErrInfo(a_rThat.m_pErrInfo);
    else if (m_pErrInfo)
        deleteErrInfo();

    return *this;
}


void RTCRestClientResponseBase::reset() RT_NOEXCEPT
{
    /* Return to default constructor state. */
    m_rcStatus = VERR_WRONG_ORDER;
    m_rcHttp   = VERR_NOT_AVAILABLE;
    if (m_pErrInfo)
        deleteErrInfo();
    m_strContentType.setNull();
}


int RTCRestClientResponseBase::receivePrepare(RTHTTP a_hHttp) RT_NOEXCEPT
{
    int rc = RTHttpSetHeaderCallback(a_hHttp, receiveHttpHeaderCallback, this);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


void RTCRestClientResponseBase::receiveComplete(int a_rcStatus, RTHTTP a_hHttp) RT_NOEXCEPT
{
    RT_NOREF_PV(a_hHttp);
    m_rcStatus = a_rcStatus;
    if (a_rcStatus >= 0)
        m_rcHttp = a_rcStatus;

    int rc = RTHttpSetHeaderCallback(a_hHttp, NULL, NULL);
    AssertRC(rc);
}


int RTCRestClientResponseBase::consumeHeader(uint32_t a_uMatchWord, const char *a_pchField, size_t a_cchField,
                                             const char *a_pchValue, size_t a_cchValue) RT_NOEXCEPT
{
    if (   a_uMatchWord == RTHTTP_MAKE_HDR_MATCH_WORD(sizeof("Content-Type") - 1, 'c', 'o', 'n')
        && RTStrNICmpAscii(a_pchField, RT_STR_TUPLE("Content-Type")) == 0)
    {
        int rc = RTStrValidateEncodingEx(a_pchValue, a_cchValue, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            return m_strContentType.assignNoThrow(a_pchValue, a_cchValue);
    }
    RT_NOREF(a_cchField);
    return VINF_SUCCESS;
}


/*static*/ DECLCALLBACK(int)
RTCRestClientResponseBase::receiveHttpHeaderCallback(RTHTTP hHttp, uint32_t uMatchWord, const char *pchField, size_t cchField,
                                                     const char *pchValue, size_t cchValue, void *pvUser) RT_NOEXCEPT
{
    RTCRestClientResponseBase *pThis = (RTCRestClientResponseBase *)pvUser;
    RT_NOREF(hHttp);
    return pThis->consumeHeader(uMatchWord, pchField, cchField, pchValue, cchValue);
}


void RTCRestClientResponseBase::consumeBody(const char *a_pchData, size_t a_cbData) RT_NOEXCEPT
{
    RT_NOREF(a_pchData, a_cbData);
}


void RTCRestClientResponseBase::receiveFinal() RT_NOEXCEPT
{
}


PRTERRINFO RTCRestClientResponseBase::getErrInfoInternal(void) RT_NOEXCEPT
{
    if (m_pErrInfo)
        return m_pErrInfo;
    size_t cbMsg = _4K;
    m_pErrInfo = (PRTERRINFO)RTMemAllocZ(sizeof(*m_pErrInfo) + cbMsg);
    if (m_pErrInfo)
        return RTErrInfoInit(m_pErrInfo, (char *)(m_pErrInfo + 1), cbMsg);
    return NULL;
}


void RTCRestClientResponseBase::deleteErrInfo(void) RT_NOEXCEPT
{
    if (m_pErrInfo)
    {
        RTMemFree(m_pErrInfo);
        m_pErrInfo = NULL;
    }
}


void RTCRestClientResponseBase::copyErrInfo(PCRTERRINFO pErrInfo) RT_NOEXCEPT
{
    deleteErrInfo();
    m_pErrInfo = (PRTERRINFO)RTMemDup(pErrInfo, pErrInfo->cbMsg + sizeof(*pErrInfo));
    if (m_pErrInfo)
    {
        m_pErrInfo->pszMsg = (char *)(m_pErrInfo + 1);
        m_pErrInfo->apvReserved[0] = NULL;
        m_pErrInfo->apvReserved[1] = NULL;
    }
}


int RTCRestClientResponseBase::addError(int rc, const char *pszFormat, ...) RT_NOEXCEPT
{
    PRTERRINFO pErrInfo = getErrInfoInternal();
    if (pErrInfo)
    {
        va_list va;
        va_start(va, pszFormat);
        if (   !RTErrInfoIsSet(pErrInfo)
            || pErrInfo->cbMsg == 0
            || pErrInfo->pszMsg[pErrInfo->cbMsg - 1] == '\n')
            RTErrInfoAddV(pErrInfo, rc, pszFormat, va);
        else
            RTErrInfoAddF(pErrInfo, rc, "\n%N", pszFormat, &va);
        va_end(va);
    }
    if (RT_SUCCESS(m_rcStatus) && RT_FAILURE_NP(rc))
        m_rcStatus = rc;
    return rc;
}


RTCRestClientResponseBase::PrimaryJsonCursorForBody::PrimaryJsonCursorForBody(RTJSONVAL hValue, const char *pszName,
                                                                              RTCRestClientResponseBase *a_pThat) RT_NOEXCEPT
    : RTCRestJsonPrimaryCursor(hValue, pszName, a_pThat->getErrInfoInternal())
    , m_pThat(a_pThat)
{
}


int RTCRestClientResponseBase::PrimaryJsonCursorForBody::addError(RTCRestJsonCursor const &a_rCursor, int a_rc,
                                                                  const char *a_pszFormat, ...) RT_NOEXCEPT
{
    va_list va;
    va_start(va, a_pszFormat);
    char szPath[256];
    m_pThat->addError(a_rc, "response body/%s: %N", getPath(a_rCursor, szPath, sizeof(szPath)), a_pszFormat, &va);
    va_end(va);
    return a_rc;
}


int RTCRestClientResponseBase::PrimaryJsonCursorForBody::unknownField(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    char szPath[256];
    m_pThat->addError(VWRN_NOT_FOUND, "response body/%s: unknown field (type %s)",
                      getPath(a_rCursor, szPath, sizeof(szPath)), RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
    return VWRN_NOT_FOUND;
}


int RTCRestClientResponseBase::deserializeHeader(RTCRestObjectBase *a_pObj, const char *a_pchValue, size_t a_cchValue,
                                                 uint32_t a_fFlags, const char *a_pszErrorTag) RT_NOEXCEPT
{
    /*
     * Start by checking the encoding and transfering the value to a RTCString object.
     */
    int rc = RTStrValidateEncodingEx(a_pchValue, a_cchValue, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    if (RT_SUCCESS(rc))
    {
        RTCString strValue;
        rc = strValue.assignNoThrow(a_pchValue, a_cchValue);
        if (RT_SUCCESS(rc))
        {
            LogRel7(("< %s: :%s = %s\n",
                     getOperationName(), a_pszErrorTag, strValue.c_str()));

            /*
             * Try deserialize it.
             */
            RTERRINFOSTATIC ErrInfo;
            rc = a_pObj->fromString(strValue, a_pszErrorTag, RTErrInfoInitStatic(&ErrInfo), a_fFlags);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else if (RTErrInfoIsSet(&ErrInfo.Core))
                addError(rc, "Error %Rrc parsing header field '%s': %s", rc, a_pszErrorTag, ErrInfo.Core.pszMsg);
            else
                addError(rc, "Error %Rrc parsing header field '%s'", rc, a_pszErrorTag);
        }
    }
    else
    {
        addError(rc, "Error %Rrc validating value encoding of header field '%s': %.*Rhxs",
                 rc, a_pszErrorTag, a_cchValue, a_pchValue);
        rc = VINF_SUCCESS; /* ignore */
    }
    return rc;
}


int RTCRestClientResponseBase::deserializeHeaderIntoMap(RTCRestStringMapBase *a_pMap, const char *a_pchField, size_t a_cchField,
                                                        const char *a_pchValue, size_t a_cchValue, uint32_t a_fFlags,
                                                        const char *a_pszErrorTag) RT_NOEXCEPT
{
    /*
     * Start by checking the encoding of both the field and value,
     * then transfering the value to a RTCString object.
     */
    int rc = RTStrValidateEncodingEx(a_pchField, a_cchField, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrValidateEncodingEx(a_pchValue, a_cchValue, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        if (RT_SUCCESS(rc))
        {
            RTCString strValue;
            rc = strValue.assignNoThrow(a_pchValue, a_cchValue);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create a value object and put it into the map.
                 */
                RTCRestObjectBase *pValue;
                rc = a_pMap->putNewValue(&pValue, a_pchField, a_cchField);
                if (RT_SUCCESS(rc))
                {
                    LogRel7(("< %s: :%s%.*s = %s\n",
                             getOperationName(), a_pszErrorTag, a_cchField, a_pchField,  strValue.c_str()));

                    /*
                     * Try deserialize the value.
                     */
                    RTERRINFOSTATIC ErrInfo;
                    rc = pValue->fromString(strValue, a_pszErrorTag, RTErrInfoInitStatic(&ErrInfo), a_fFlags);
                    if (RT_SUCCESS(rc))
                    { /* likely */ }
                    else if (RTErrInfoIsSet(&ErrInfo.Core))
                        addError(rc, "Error %Rrc parsing header field '%s' subfield '%.*s': %s",
                                 rc, a_pszErrorTag, a_cchField, a_pchField, ErrInfo.Core.pszMsg);
                    else
                        addError(rc, "Error %Rrc parsing header field '%s' subfield '%.*s'",
                                 rc, a_pszErrorTag, a_cchField, a_pchField);
                }
            }
        }
        else
        {
            addError(rc, "Error %Rrc validating value encoding of header field '%s': %.*Rhxs",
                     rc, a_pszErrorTag, a_cchValue, a_pchValue);
            rc = VINF_SUCCESS; /* ignore */
        }
    }
    else
    {
        addError(rc, "Error %Rrc validating sub-field encoding of header field '%s*': %.*Rhxs",
                 rc, a_pszErrorTag, a_cchField, a_pchField);
        rc = VINF_SUCCESS; /* ignore */
    }
    return rc;
}


void RTCRestClientResponseBase::deserializeBody(const char *a_pchData, size_t a_cbData, const char *a_pszBodyName) RT_NOEXCEPT
{
    if (m_strContentType.startsWith("application/json"))
    {
        int rc = RTStrValidateEncodingEx(a_pchData, a_cbData, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        if (RT_SUCCESS(rc))
        {
            if (LogRelIs7Enabled())
            {
                /* skip m_ or m_p prefix */
                const char *pszName = a_pszBodyName;
                if (pszName[0] == 'm' && pszName[1] == '_')
                {
                    if (pszName[2] == 'p')
                        pszName += 3;
                    else
                        pszName += 2;
                }

                LogRel7(("< %s: %d: %s = %.*s\n",
                         getOperationName(), m_rcHttp, pszName, a_cbData, a_pchData));
            }

            RTERRINFOSTATIC ErrInfo;
            RTJSONVAL hValue;
            rc = RTJsonParseFromBuf(&hValue, (const uint8_t *)a_pchData, a_cbData, RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(rc))
            {
                PrimaryJsonCursorForBody PrimaryCursor(hValue, a_pszBodyName, this); /* note: consumes hValue */
                deserializeBodyFromJsonCursor(PrimaryCursor.m_Cursor);
            }
            else if (RTErrInfoIsSet(&ErrInfo.Core))
                addError(rc, "Error %Rrc parsing server response as JSON (type %s): %s",
                         rc, a_pszBodyName, ErrInfo.Core.pszMsg);
            else
                addError(rc, "Error %Rrc parsing server response as JSON (type %s)", rc, a_pszBodyName);
        }
        else if (rc == VERR_INVALID_UTF8_ENCODING)
            addError(VERR_REST_RESPONSE_INVALID_UTF8_ENCODING, "Invalid UTF-8 body encoding (object type %s; Content-Type: %s)",
                     a_pszBodyName, m_strContentType.c_str());
        else if (rc == VERR_BUFFER_UNDERFLOW)
            addError(VERR_REST_RESPONSE_EMBEDDED_ZERO_CHAR, "Embedded zero character in response (object type %s; Content-Type: %s)",
                     a_pszBodyName, m_strContentType.c_str());
        else
            addError(rc, "Unexpected body validation error (object type %s; Content-Type: %s): %Rrc",
                     a_pszBodyName, m_strContentType.c_str(), rc);
    }
    else
        addError(VERR_REST_RESPONSE_CONTENT_TYPE_NOT_SUPPORTED, "Unsupported content type for '%s': %s",
                 a_pszBodyName, m_strContentType.c_str());
}


void RTCRestClientResponseBase::deserializeBodyFromJsonCursor(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    a_rCursor.m_pPrimary->addError(a_rCursor, VERR_REST_INTERNAL_ERROR_8, "deserializeBodyFromJsonCursor must be overridden!");
    AssertFailed();
}

