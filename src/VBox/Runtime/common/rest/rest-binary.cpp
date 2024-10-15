/* $Id: rest-binary.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestBinary and Descendants.
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
#include <iprt/cpp/restbase.h>
#include <iprt/cpp/restclient.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/cpp/restoutput.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The default maximum download size. */
#if ARCH_BITS == 32
# define RTCREST_MAX_DOWNLOAD_SIZE_DEFAULT  _32M
#else
# define RTCREST_MAX_DOWNLOAD_SIZE_DEFAULT  _128M
#endif



/*********************************************************************************************************************************
*   RTCRestBinary Implementation.                                                                                                *
*********************************************************************************************************************************/
/**
 * Default constructor.
 */
RTCRestBinary::RTCRestBinary() RT_NOEXCEPT
    : m_pbData(NULL)
    , m_cbData(0)
    , m_cbAllocated(0)
    , m_fFreeable(true)
    , m_fReadOnly(false)
{
}


/**
 * Destructor.
 */
RTCRestBinary::~RTCRestBinary()
{
    freeData();
}

/**
 * Safe copy assignment method.
 */
int RTCRestBinary::assignCopy(RTCRestBinary const &a_rThat) RT_NOEXCEPT
{
    freeData();
    if (a_rThat.m_pbData)
    {
        m_pbData         = (uint8_t *)RTMemDup(a_rThat.m_pbData, a_rThat.m_cbAllocated);
        AssertReturn(m_pbData, VERR_NO_MEMORY);
        m_cbData         = a_rThat.m_cbData;
        m_cbAllocated    = a_rThat.m_cbAllocated;
        m_fFreeable      = true;
        m_fReadOnly      = false;
        m_fNullIndicator = false;
    }
    else
        m_fNullIndicator = a_rThat.m_fNullIndicator;
    return VINF_SUCCESS;
}


/**
 * Safe buffer copy method.
 */
int RTCRestBinary::assignCopy(void const *a_pvData, size_t a_cbData) RT_NOEXCEPT
{
    if (   m_pbData == NULL
        || m_fReadOnly
        || a_cbData > m_cbAllocated)
    {
        freeData();
        m_pbData      = (uint8_t *)RTMemDup(a_pvData, a_cbData);
        AssertReturn(m_pbData, VERR_NO_MEMORY);
        m_cbData      = a_cbData;
        m_cbAllocated = a_cbData;
        m_fFreeable   = true;
        m_fReadOnly   = false;
    }
    else
    {
        m_cbData = a_cbData;
        memcpy(m_pbData, a_pvData, a_cbData);
    }
    m_fNullIndicator = false;
    return VINF_SUCCESS;
}


/**
 * Use the specified data buffer directly.
 */
int RTCRestBinary::assignReadOnly(void const *a_pvData, size_t a_cbData) RT_NOEXCEPT
{
    freeData();
    if (a_pvData)
    {
        m_pbData         = (uint8_t *)a_pvData;
        m_cbData         = a_cbData;
        m_cbAllocated    = 0;
        m_fFreeable      = false;
        m_fReadOnly      = true;
        m_fNullIndicator = false;
    }
    return VINF_SUCCESS;
}


/**
 * Use the specified data buffer directly.
 */
int RTCRestBinary::assignWriteable(void *a_pvBuf, size_t a_cbBuf) RT_NOEXCEPT
{
    freeData();
    if (a_pvBuf)
    {
        m_pbData         = (uint8_t *)a_pvBuf;
        m_cbData         = a_cbBuf;
        m_cbAllocated    = a_cbBuf;
        m_fFreeable      = false;
        m_fReadOnly      = false;
        m_fNullIndicator = false;
    }
    return VINF_SUCCESS;
}


/**
 * Frees the data held by the object and resets it default state.
 */
void RTCRestBinary::freeData() RT_NOEXCEPT
{
    if (m_fFreeable)
        RTMemFree(m_pbData);
    m_pbData        = NULL;
    m_cbData        = 0;
    m_cbAllocated   = 0;
    m_fFreeable     = true;
    m_fReadOnly     = false;
}


/* Overridden methods: */

RTCRestObjectBase *RTCRestBinary::baseClone() const RT_NOEXCEPT
{
    RTCRestBinary *pClone = new (std::nothrow) RTCRestBinary();
    if (pClone)
    {
        int rc = pClone->assignCopy(*this);
        if (RT_SUCCESS(rc))
            return pClone;
        delete pClone;
    }
    return NULL;
}


int RTCRestBinary::setNull(void) RT_NOEXCEPT
{
    freeData();
    m_fNullIndicator = true;
    return VINF_SUCCESS;
}


int RTCRestBinary::resetToDefault(void) RT_NOEXCEPT
{
    freeData();
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestBinary::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    AssertMsgFailed(("We should never get here!\n"));
    a_rDst.nullValue();
    return a_rDst;
}


int RTCRestBinary::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_NOT_SUPPORTED, "RTCRestBinary does not support deserialization!");
}


int RTCRestBinary::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    RT_NOREF(a_pDst, a_fFlags);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}


int RTCRestBinary::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo/*= NULL*/,
                              uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    RT_NOREF(a_rValue, a_pszName, a_fFlags);
    AssertFailedReturn(RTErrInfoSet(a_pErrInfo, VERR_NOT_SUPPORTED, "RTCRestBinary does not support fromString()!"));
}


RTCRestObjectBase::kTypeClass RTCRestBinary::typeClass(void) const RT_NOEXCEPT
{
    return kTypeClass_Binary;
}


const char *RTCRestBinary::typeName(void) const RT_NOEXCEPT
{
    return "RTCRestBinary";
}


/** Factory method. */
/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestBinary::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestBinary();
}


/**
 * @copydoc RTCRestObjectBase::FNDESERIALIZEINSTANCEFROMJSON
 */
/*static*/ DECLCALLBACK(int)
RTCRestBinary::deserializeInstanceFromJson(RTCRestJsonCursor const &a_rCursor, RTCRestObjectBase **a_ppInstance) RT_NOEXCEPT
{
    RTCRestObjectBase *pObj;
    *a_ppInstance = pObj = createInstance();
    if (pObj)
        return pObj->deserializeFromJson(a_rCursor);
    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_NO_MEMORY, "Out of memory");
}



/*********************************************************************************************************************************
*   RTCRestBinaryParameter Implementation.                                                                                       *
*********************************************************************************************************************************/

/**
 * Default constructor.
 */
RTCRestBinaryParameter::RTCRestBinaryParameter() RT_NOEXCEPT
    : RTCRestBinary()
    , m_cbContentLength(UINT64_MAX)
    , m_strContentType()
    , m_pfnProducer(NULL)
    , m_pvCallbackData(NULL)
{
}


int RTCRestBinaryParameter::assignCopy(RTCRestBinaryParameter const &a_rThat) RT_NOEXCEPT
{
    AssertReturn(a_rThat.m_pfnProducer, VERR_INVALID_STATE);
    int rc = assignCopy(*(RTCRestBinary const *)&a_rThat);
    if (RT_SUCCESS(rc))
        rc = m_strContentType.assignNoThrow(a_rThat.m_strContentType);
    m_cbContentLength = a_rThat.m_cbContentLength;
    m_pfnProducer = a_rThat.m_pfnProducer;
    m_pvCallbackData = a_rThat.m_pvCallbackData;
    return VINF_SUCCESS;
}


int RTCRestBinaryParameter::assignCopy(RTCRestBinary const &a_rThat) RT_NOEXCEPT
{
    m_cbContentLength = a_rThat.getSize();
    m_strContentType.setNull();
    m_pfnProducer     = NULL;
    m_pvCallbackData  = NULL;
    return RTCRestBinary::assignCopy(a_rThat);
}


int RTCRestBinaryParameter::assignCopy(void const *a_pvData, size_t a_cbData) RT_NOEXCEPT
{
    m_cbContentLength = a_cbData;
    m_pfnProducer     = NULL;
    m_pvCallbackData  = NULL;
    return RTCRestBinary::assignCopy(a_pvData, a_cbData);
}


int RTCRestBinaryParameter::assignReadOnly(void const *a_pvData, size_t a_cbData) RT_NOEXCEPT
{
    m_cbContentLength = a_cbData;
    m_pfnProducer     = NULL;
    m_pvCallbackData  = NULL;
    return RTCRestBinary::assignReadOnly(a_pvData, a_cbData);
}


int RTCRestBinaryParameter::assignWriteable(void *a_pvBuf, size_t a_cbBuf) RT_NOEXCEPT
{
    AssertMsgFailed(("Please use assignReadOnly!\n"));
    return assignReadOnly(a_pvBuf, a_cbBuf);
}


RTCRestObjectBase *RTCRestBinaryParameter::baseClone() const RT_NOEXCEPT
{
    RTCRestBinaryParameter *pClone = new (std::nothrow) RTCRestBinaryParameter();
    if (pClone)
    {
        int rc = pClone->assignCopy(*this);
        if (RT_SUCCESS(rc))
            return pClone;
        delete pClone;
    }
    return NULL;
}


int RTCRestBinaryParameter::resetToDefault() RT_NOEXCEPT
{
    m_cbContentLength = UINT64_MAX;
    m_pfnProducer     = NULL;
    m_pvCallbackData  = NULL;
    return RTCRestBinary::resetToDefault();
}


const char *RTCRestBinaryParameter::typeName(void) const RT_NOEXCEPT
{
    return "RTCRestBinaryParameter";
}


/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestBinaryParameter::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestBinaryParameter();
}


int RTCRestBinaryParameter::setContentType(const char *a_pszContentType) RT_NOEXCEPT
{
    return m_strContentType.assignNoThrow(a_pszContentType);
}


void RTCRestBinaryParameter::setProducerCallback(PFNPRODUCER a_pfnProducer,  void *a_pvCallbackData /*= NULL*/,
                                                 uint64_t a_cbContentLength /*= UINT64_MAX*/) RT_NOEXCEPT
{
    freeData();

    m_pfnProducer     = a_pfnProducer;
    m_pvCallbackData  = a_pvCallbackData;
    m_cbContentLength = a_cbContentLength;
}


int RTCRestBinaryParameter::xmitPrepare(RTHTTP a_hHttp) const RT_NOEXCEPT
{
    AssertReturn(m_pbData != NULL || m_pfnProducer != NULL || m_cbContentLength == 0, VERR_INVALID_STATE);


    /*
     * Set the content type if given.
     */
    if (m_strContentType.isNotEmpty())
    {
        Assert(!RTHttpGetHeader(a_hHttp, RT_STR_TUPLE("Content-Type")));
        int rc = RTHttpAddHeader(a_hHttp, "Content-Type", m_strContentType.c_str(), m_strContentType.length(),
                                 RTHTTPADDHDR_F_BACK);
        AssertRCReturn(rc, rc);
    }

    /*
     * Set the content length if given.
     */
    if (m_cbContentLength != UINT64_MAX)
    {
        const char *pszContentLength = RTHttpGetHeader(a_hHttp, RT_STR_TUPLE("Content-Length"));
        AssertMsgReturn(!pszContentLength || RTStrToUInt64(pszContentLength) == m_cbContentLength,
                        ("pszContentLength=%s does not match m_cbContentLength=%RU64\n", pszContentLength, m_cbContentLength),
                        VERR_MISMATCH);
        if (!pszContentLength)
        {
            char szValue[64];
            ssize_t cchValue = RTStrFormatU64(szValue, sizeof(szValue), m_cbContentLength, 10, 0, 0, 0);
            int rc = RTHttpAddHeader(a_hHttp, "Content-Length", szValue, cchValue, RTHTTPADDHDR_F_BACK);
            AssertRCReturn(rc, rc);
        }
    }

    /*
     * Register an upload callback.
     */
    int rc = RTHttpSetUploadCallback(a_hHttp, m_cbContentLength, xmitHttpCallback, (RTCRestBinaryParameter *)this);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;

}


/*static*/ DECLCALLBACK(int)
RTCRestBinaryParameter::xmitHttpCallback(RTHTTP hHttp, void *pvBuf, size_t cbBuf,
                                         uint64_t offContent, size_t *pcbActual, void *pvUser) RT_NOEXCEPT
{
    RTCRestBinaryParameter *pThis = (RTCRestBinaryParameter *)pvUser;

    /*
     * Call the user upload callback if we've got one.
     */
    if (pThis->m_pfnProducer)
        return pThis->m_pfnProducer(pThis, pvBuf, cbBuf, offContent, pcbActual);

    /*
     * Feed from the memory buffer.
     */
    if (offContent < pThis->m_cbContentLength)
    {
        uint64_t const cbLeft = pThis->m_cbContentLength - offContent;
        size_t const cbToCopy = cbLeft >= cbBuf ? cbBuf : (size_t)cbLeft;
        memcpy(pvBuf, &pThis->m_pbData[(size_t)offContent], cbToCopy);
        *pcbActual = cbToCopy;
    }
    else
        *pcbActual = 0;

    RT_NOREF(hHttp);
    return VINF_SUCCESS;
}


void RTCRestBinaryParameter::xmitComplete(RTHTTP a_hHttp) const RT_NOEXCEPT
{
    /* Unset the callback. */
    int rc = RTHttpSetUploadCallback(a_hHttp, UINT64_MAX, NULL, NULL);
    AssertRC(rc);
}


/*********************************************************************************************************************************
*   RTCRestBinaryResponse Implementation.                                                                                        *
*********************************************************************************************************************************/

/**
 * Default constructor.
 */
RTCRestBinaryResponse::RTCRestBinaryResponse() RT_NOEXCEPT
    : RTCRestBinary()
    , m_cbContentLength(UINT64_MAX)
    , m_cbDownloaded(0)
    , m_pfnConsumer(NULL)
    , m_pvCallbackData(NULL)
    , m_cbMaxDownload(RTCREST_MAX_DOWNLOAD_SIZE_DEFAULT)
{
}


int RTCRestBinaryResponse::assignCopy(RTCRestBinaryResponse const &a_rThat) RT_NOEXCEPT
{
    AssertReturn(a_rThat.m_pfnConsumer, VERR_INVALID_STATE);
    int rc = assignCopy(*(RTCRestBinary const *)&a_rThat);
    m_cbContentLength = a_rThat.m_cbContentLength;
    m_cbDownloaded    = a_rThat.m_cbDownloaded;
    m_cbMaxDownload   = a_rThat.m_cbMaxDownload;
    return rc;
}


int RTCRestBinaryResponse::assignCopy(RTCRestBinary const &a_rThat) RT_NOEXCEPT
{
    m_cbContentLength = UINT64_MAX;
    m_cbDownloaded    = 0;
    m_pfnConsumer     = NULL;
    m_pvCallbackData  = NULL;
    return RTCRestBinary::assignCopy(a_rThat);
}


int RTCRestBinaryResponse::assignCopy(void const *a_pvData, size_t a_cbData) RT_NOEXCEPT
{
    RT_NOREF(a_pvData, a_cbData);
    AssertMsgFailedReturn(("Makes no sense for downloads.\n"), VERR_INVALID_STATE);
}


int RTCRestBinaryResponse::assignReadOnly(void const *a_pvData, size_t a_cbData) RT_NOEXCEPT
{
    RT_NOREF(a_pvData, a_cbData);
    AssertMsgFailedReturn(("Makes no sense for downloads.\n"), VERR_INVALID_STATE);
}


int RTCRestBinaryResponse::assignWriteable(void *a_pvBuf, size_t a_cbBuf) RT_NOEXCEPT
{
    m_cbContentLength = UINT64_MAX;
    m_cbDownloaded    = 0;
    m_pfnConsumer     = NULL;
    m_pvCallbackData  = NULL;
    AssertStmt(a_cbBuf <= m_cbMaxDownload, m_cbMaxDownload = a_cbBuf);
    return RTCRestBinary::assignWriteable(a_pvBuf, a_cbBuf);
}


RTCRestObjectBase *RTCRestBinaryResponse::baseClone() const RT_NOEXCEPT
{
    RTCRestBinaryResponse *pClone = new (std::nothrow) RTCRestBinaryResponse();
    if (pClone)
    {
        int rc = pClone->assignCopy(*this);
        if (RT_SUCCESS(rc))
            return pClone;
        delete pClone;
    }
    return NULL;
}


int RTCRestBinaryResponse::resetToDefault() RT_NOEXCEPT
{
    m_cbContentLength = UINT64_MAX;
    m_cbDownloaded    = 0;
    m_pfnConsumer     = NULL;
    m_pvCallbackData  = NULL;
    m_cbMaxDownload   = RTCREST_MAX_DOWNLOAD_SIZE_DEFAULT;
    return RTCRestBinary::resetToDefault();
}


const char *RTCRestBinaryResponse::typeName(void) const RT_NOEXCEPT
{
    return "RTCRestBinaryResponse";
}


/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestBinaryResponse::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestBinaryResponse();
}


void RTCRestBinaryResponse::setMaxDownloadSize(size_t a_cbMaxDownload) RT_NOEXCEPT
{
    if (a_cbMaxDownload == 0)
        m_cbMaxDownload = RTCREST_MAX_DOWNLOAD_SIZE_DEFAULT;
    else
        m_cbMaxDownload = a_cbMaxDownload;
}


void RTCRestBinaryResponse::setConsumerCallback(PFNCONSUMER a_pfnConsumer, void *a_pvCallbackData /*= NULL*/) RT_NOEXCEPT
{
    freeData();

    m_pfnConsumer     = a_pfnConsumer;
    m_pvCallbackData  = a_pvCallbackData;
    m_cbDownloaded    = 0;
    m_cbContentLength = UINT64_MAX;
}


int RTCRestBinaryResponse::receivePrepare(RTHTTP a_hHttp, uint32_t a_fCallbackFlags) RT_NOEXCEPT
{
    AssertReturn(!m_fReadOnly, VERR_INVALID_STATE);

    /*
     * Register the download callback.
     */
    int rc = RTHttpSetDownloadCallback(a_hHttp, a_fCallbackFlags, receiveHttpCallback, this);
    AssertRC(rc);
    return rc;
}


/*static*/ DECLCALLBACK(int)
RTCRestBinaryResponse::receiveHttpCallback(RTHTTP hHttp, void const *pvBuf, size_t cbBuf, uint32_t uHttpStatus,
                                           uint64_t offContent, uint64_t cbContent, void *pvUser) RT_NOEXCEPT
{
    RTCRestBinaryResponse *pThis = (RTCRestBinaryResponse *)pvUser;
    Assert(offContent == pThis->m_cbDownloaded);
    pThis->m_cbContentLength = cbContent;

    /*
     * Call the user download callback if we've got one.
     */
    if (pThis->m_pfnConsumer)
    {
        int rc = pThis->m_pfnConsumer(pThis, pvBuf, cbBuf, uHttpStatus, offContent, cbContent);
        if (RT_SUCCESS(rc))
            pThis->m_cbDownloaded = offContent + cbBuf;
        return rc;
    }

    /*
     * Check download limit before adding more data.
     */
    AssertMsgReturn(offContent + cbBuf <= pThis->m_cbMaxDownload,
                    ("%RU64 + %zu = %RU64; max=%RU64", offContent, cbBuf, offContent + cbBuf, pThis->m_cbMaxDownload),
                    VERR_TOO_MUCH_DATA);

    /*
     * Make sure we've got sufficient writable buffer space before we copy in the data.
     */
    AssertReturn(!pThis->m_fReadOnly, VERR_INVALID_STATE);
    if (offContent + cbBuf <= pThis->m_cbAllocated)
    { /* likely, except for the first time. */ }
    else
    {
        AssertMsgReturn(pThis->m_fFreeable,
                        ("offContent=%RU64 cbBuf=%zu m_cbAllocated=%zu", offContent, cbBuf, pThis->m_cbAllocated),
                        VERR_TOO_MUCH_DATA);
        AssertMsgReturn(cbContent <= pThis->m_cbMaxDownload || cbContent == UINT64_MAX,
                        ("cbContent: %RU64; max=%RU64", cbContent,  pThis->m_cbMaxDownload),
                        VERR_TOO_MUCH_DATA);

        if (offContent == 0 && cbContent != UINT64_MAX)
        {
            void *pvNew = RTMemRealloc(pThis->m_pbData, (size_t)cbContent);
            if (!pvNew)
                return VERR_NO_MEMORY;
            pThis->m_pbData = (uint8_t *)pvNew;
            pThis->m_cbAllocated = (size_t)cbContent;
        }
        else
        {
            size_t cbNeeded = offContent + cbBuf;
            size_t cbNew;
            if (pThis->m_cbAllocated == 0)
                cbNew = RT_MAX(_64K, RT_ALIGN_Z(cbNeeded, _64K));
            else if (pThis->m_cbAllocated < _64M && cbNeeded <= _64M)
            {
                cbNew = pThis->m_cbAllocated * 2;
                while (cbNew < cbNeeded)
                    cbNew *= 2;
            }
            else
                cbNew = RT_ALIGN_Z(cbNeeded, _32M);

            void *pvNew = RTMemRealloc(pThis->m_pbData, cbNew);
            if (!pvNew)
                return VERR_NO_MEMORY;
            pThis->m_pbData = (uint8_t *)pvNew;
            pThis->m_cbAllocated = cbNew;
        }
    }

    /*
     * Do the copying.
     */
    memcpy(&pThis->m_pbData[(size_t)offContent], pvBuf, cbBuf);
    pThis->m_cbDownloaded = offContent + cbBuf;

    /* we cap it at m_cbMaxDownload which is size_t so this cast is safe */
    pThis->m_cbData = (size_t)pThis->m_cbDownloaded;

    RT_NOREF(hHttp);
    return VINF_SUCCESS;
}


void RTCRestBinaryResponse::receiveComplete(RTHTTP a_hHttp) RT_NOEXCEPT
{
    /* Unset the callback. */
    int rc = RTHttpSetDownloadCallback(a_hHttp, RTHTTPDOWNLOAD_F_ANY_STATUS, NULL, NULL);
    AssertRC(rc);
}

