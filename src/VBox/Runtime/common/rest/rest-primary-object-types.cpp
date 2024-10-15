/* $Id: rest-primary-object-types.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestObjectBase implementation.
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

#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/cpp/restoutput.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>



/*********************************************************************************************************************************
*   RTCRestObjectBase implementation                                                                                             *
*********************************************************************************************************************************/

/** Default constructor. */
RTCRestObjectBase::RTCRestObjectBase() RT_NOEXCEPT
    : m_fNullIndicator(false)
{
}


/** Copy constructor. */
RTCRestObjectBase::RTCRestObjectBase(RTCRestObjectBase const &a_rThat) RT_NOEXCEPT
    : m_fNullIndicator(a_rThat.m_fNullIndicator)
{
}


/** Destructor. */
RTCRestObjectBase::~RTCRestObjectBase()
{
    /* nothing to do */
}


/** Copy assignment operator. */
RTCRestObjectBase &RTCRestObjectBase::operator=(RTCRestObjectBase const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    return *this;
}


int RTCRestObjectBase::setNull() RT_NOEXCEPT
{
    int rc = resetToDefault();
    m_fNullIndicator = true;
    return rc;
}


void RTCRestObjectBase::setNotNull() RT_NOEXCEPT
{
    m_fNullIndicator = false;
}


int RTCRestObjectBase::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    /*
     * Just wrap the JSON serialization method.
     */
    RTCRestOutputToString Tmp(a_pDst, RT_BOOL(a_fFlags & kToString_Append));
    serializeAsJson(Tmp);
    return Tmp.finalize() ? VINF_SUCCESS : VERR_NO_MEMORY;
}


RTCString RTCRestObjectBase::toString() const
{
    RTCString strRet;
    toString(&strRet, 0);
    return strRet;
}


int RTCRestObjectBase::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                                  uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    RT_NOREF(a_fFlags);

    /*
     * Just wrap the JSON serialization method.
     */
    RTJSONVAL hValue = NIL_RTJSONVAL;
    int rc = RTJsonParseFromString(&hValue, a_rValue.c_str(), a_pErrInfo);
    if (RT_SUCCESS(rc))
    {
        RTCRestJsonPrimaryCursor PrimaryCursor(hValue, a_pszName, a_pErrInfo);
        rc = deserializeFromJson(PrimaryCursor.m_Cursor);
    }
    return rc;
}



/*********************************************************************************************************************************
*   RTCRestBool implementation                                                                                                   *
*********************************************************************************************************************************/

/** Factory method. */
/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestBool::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestBool();
}


/**
 * @copydoc RTCRestObjectBase::FNDESERIALIZEINSTANCEFROMJSON
 */
/*static*/ DECLCALLBACK(int)
RTCRestBool::deserializeInstanceFromJson(RTCRestJsonCursor const &a_rCursor, RTCRestObjectBase **a_ppInstance) RT_NOEXCEPT
{
    RTCRestObjectBase *pObj = createInstance();
    *a_ppInstance = pObj;
    if (pObj)
        return pObj->deserializeFromJson(a_rCursor);
    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_NO_MEMORY, "Out of memory");
}


/** Default constructor. */
RTCRestBool::RTCRestBool() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_fValue(false)
{
}


/** Copy constructor. */
RTCRestBool::RTCRestBool(RTCRestBool const &a_rThat) RT_NOEXCEPT
    : RTCRestObjectBase(a_rThat)
    , m_fValue(a_rThat.m_fValue)
{
}


/** From value constructor. */
RTCRestBool::RTCRestBool(bool fValue) RT_NOEXCEPT
    : m_fValue(fValue)
{
}


/** Destructor. */
RTCRestBool::~RTCRestBool()
{
    /* nothing to do */
}


/** Copy assignment operator. */
RTCRestBool &RTCRestBool::operator=(RTCRestBool const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_fValue = a_rThat.m_fValue;
    return *this;
}


int RTCRestBool::assignCopy(RTCRestBool const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_fValue = a_rThat.m_fValue;
    return VINF_SUCCESS;
}


void RTCRestBool::assignValue(bool a_fValue) RT_NOEXCEPT
{
    m_fValue = a_fValue;
    m_fNullIndicator = false;
}


RTCRestObjectBase *RTCRestBool::baseClone() const RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestBool(*this);
}


int RTCRestBool::resetToDefault() RT_NOEXCEPT
{
    m_fValue = false;
    m_fNullIndicator = false;
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestBool::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    a_rDst.printf(!m_fNullIndicator ? m_fValue ? "true" : "false" : "null");
    return a_rDst;
}


int RTCRestBool::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    m_fValue = false;
    m_fNullIndicator = false;

    RTJSONVALTYPE enmType = RTJsonValueGetType(a_rCursor.m_hValue);

    if (enmType == RTJSONVALTYPE_TRUE)
    {
        m_fValue = true;
        return VINF_SUCCESS;
    }

    if (enmType == RTJSONVALTYPE_FALSE)
        return VINF_SUCCESS;

    if (enmType == RTJSONVALTYPE_NULL)
    {
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_REST_WRONG_JSON_TYPE_FOR_BOOL, "wrong JSON type %s for boolean",
                                          RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
}


int RTCRestBool::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    if (!(a_fFlags & kToString_Append))
    {
        if (!m_fNullIndicator)
        {
            if (m_fValue)
                return a_pDst->assignNoThrow(RT_STR_TUPLE("true"));
            return a_pDst->assignNoThrow(RT_STR_TUPLE("false"));
        }
        return a_pDst->assignNoThrow(RT_STR_TUPLE("null"));
    }

    if (!m_fNullIndicator)
    {
        if (m_fValue)
            return a_pDst->appendNoThrow(RT_STR_TUPLE("true"));
        return a_pDst->appendNoThrow(RT_STR_TUPLE("false"));
    }
    return a_pDst->appendNoThrow(RT_STR_TUPLE("null"));
}


int RTCRestBool::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                            uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    RT_NOREF(a_fFlags);

    if (a_rValue.startsWithWord("true", RTCString::CaseInsensitive))
    {
        m_fValue = true;
        m_fNullIndicator = false;
    }
    else if (a_rValue.startsWithWord("false", RTCString::CaseInsensitive))
    {
        m_fValue = false;
        m_fNullIndicator = false;
    }
    else if (a_rValue.startsWithWord("null", RTCString::CaseInsensitive))
    {
        m_fValue = false;
        m_fNullIndicator = true;
    }
    else
        return RTErrInfoSetF(a_pErrInfo, VERR_REST_UNABLE_TO_PARSE_STRING_AS_BOOL,
                             "%s: unable to parse '%s' as bool", a_pszName, a_rValue.c_str());
    return VINF_SUCCESS;
}


RTCRestObjectBase::kTypeClass RTCRestBool::typeClass() const RT_NOEXCEPT
{
    return kTypeClass_Bool;
}


const char *RTCRestBool::typeName() const RT_NOEXCEPT
{
    return "bool";
}



/*********************************************************************************************************************************
*   RTCRestInt64 implementation                                                                                                  *
*********************************************************************************************************************************/

/** Factory method. */
/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestInt64::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestInt64();
}


/**
 * @copydoc RTCRestObjectBase::FNDESERIALIZEINSTANCEFROMJSON
 */
/*static*/ DECLCALLBACK(int)
RTCRestInt64::deserializeInstanceFromJson(RTCRestJsonCursor const &a_rCursor, RTCRestObjectBase **a_ppInstance) RT_NOEXCEPT
{
    RTCRestObjectBase *pObj = createInstance();
    *a_ppInstance = pObj;
    if (pObj)
        return pObj->deserializeFromJson(a_rCursor);
    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_NO_MEMORY, "Out of memory");
}


/** Default constructor. */
RTCRestInt64::RTCRestInt64() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_iValue(0)
{
}


/** Copy constructor. */
RTCRestInt64::RTCRestInt64(RTCRestInt64 const &a_rThat) RT_NOEXCEPT
    : RTCRestObjectBase(a_rThat)
    , m_iValue(a_rThat.m_iValue)
{
}


/** From value constructor. */
RTCRestInt64::RTCRestInt64(int64_t iValue) RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_iValue(iValue)
{
}


/** Destructor. */
RTCRestInt64::~RTCRestInt64()
{
    /* nothing to do */
}


/** Copy assignment operator. */
RTCRestInt64 &RTCRestInt64::operator=(RTCRestInt64 const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_iValue = a_rThat.m_iValue;
    return *this;
}


int RTCRestInt64::assignCopy(RTCRestInt64 const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_iValue = a_rThat.m_iValue;
    return VINF_SUCCESS;
}


void RTCRestInt64::assignValue(int64_t a_iValue) RT_NOEXCEPT
{
    m_iValue = a_iValue;
    m_fNullIndicator = false;
}


RTCRestObjectBase *RTCRestInt64::baseClone() const RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestInt64(*this);
}


int RTCRestInt64::resetToDefault() RT_NOEXCEPT
{
    m_iValue = 0;
    m_fNullIndicator = false;
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestInt64::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
        a_rDst.printf("%RI64", m_iValue);
    else
        a_rDst.nullValue();
    return a_rDst;
}


int RTCRestInt64::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    m_iValue = 0;
    m_fNullIndicator = false;

    RTJSONVALTYPE enmType = RTJsonValueGetType(a_rCursor.m_hValue);
    if (enmType == RTJSONVALTYPE_INTEGER)
    {
        int rc = RTJsonValueQueryInteger(a_rCursor.m_hValue, &m_iValue);
        if (RT_SUCCESS(rc))
            return rc;
        return a_rCursor.m_pPrimary->addError(a_rCursor, rc, "RTJsonValueQueryInteger failed with %Rrc", rc);
    }

    if (enmType == RTJSONVALTYPE_NULL)
    {
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    /* This is probably non-sense... */
    if (enmType == RTJSONVALTYPE_TRUE)
        m_iValue = 1;

    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_REST_WRONG_JSON_TYPE_FOR_INTEGER, "wrong JSON type %s for 64-bit integer",
                                          RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
}


int RTCRestInt64::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    if (!(a_fFlags & kToString_Append))
    {
        if (!m_fNullIndicator)
            return a_pDst->printfNoThrow("%RI64", m_iValue);
        return a_pDst->assignNoThrow(RT_STR_TUPLE("null"));
    }
    if (!m_fNullIndicator)
        return a_pDst->appendPrintfNoThrow("%RI64", m_iValue);
    return a_pDst->appendNoThrow(RT_STR_TUPLE("null"));
}


int RTCRestInt64::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                             uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    RT_NOREF(a_fFlags);

    m_iValue = 0;
    m_fNullIndicator = false;

/** @todo RTStrStripL and RTStrToInt64Full has a different idea what consitutes spaces... */
    int rc = RTStrToInt64Full(RTStrStripL(a_rValue.c_str()), 0, &m_iValue);
    if (rc == VINF_SUCCESS || rc == VERR_TRAILING_SPACES)
        return VINF_SUCCESS;

    if (a_rValue.startsWithWord("null", RTCString::CaseInsensitive))
    {
        m_iValue = 0;
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    return RTErrInfoSetF(a_pErrInfo, rc, "%s: error %Rrc parsing '%s' as int64_t", a_pszName, rc, a_rValue.c_str());
}


RTCRestObjectBase::kTypeClass RTCRestInt64::typeClass() const RT_NOEXCEPT
{
    return kTypeClass_Int64;
}


const char *RTCRestInt64::typeName() const RT_NOEXCEPT
{
    return "int64_t";
}



/*********************************************************************************************************************************
*   RTCRestInt32 implementation                                                                                                  *
*********************************************************************************************************************************/

/** Factory method. */
/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestInt32::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestInt32();
}


/**
 * @copydoc RTCRestObjectBase::FNDESERIALIZEINSTANCEFROMJSON
 */
/*static*/ DECLCALLBACK(int)
RTCRestInt32::deserializeInstanceFromJson(RTCRestJsonCursor const &a_rCursor, RTCRestObjectBase **a_ppInstance) RT_NOEXCEPT
{
    RTCRestObjectBase *pObj = createInstance();
    *a_ppInstance = pObj;
    if (pObj)
        return pObj->deserializeFromJson(a_rCursor);
    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_NO_MEMORY, "Out of memory");
}


/** Default constructor. */
RTCRestInt32::RTCRestInt32() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_iValue(0)
{
}


/** Copy constructor. */
RTCRestInt32::RTCRestInt32(RTCRestInt32 const &a_rThat) RT_NOEXCEPT
    : RTCRestObjectBase(a_rThat)
    , m_iValue(a_rThat.m_iValue)
{
}


/** From value constructor. */
RTCRestInt32::RTCRestInt32(int32_t iValue) RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_iValue(iValue)
{
}


/** Destructor. */
RTCRestInt32::~RTCRestInt32() RT_NOEXCEPT
{
    /* nothing to do */
}


/** Copy assignment operator. */
RTCRestInt32 &RTCRestInt32::operator=(RTCRestInt32 const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_iValue = a_rThat.m_iValue;
    return *this;
}


int RTCRestInt32::assignCopy(RTCRestInt32 const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_iValue = a_rThat.m_iValue;
    return VINF_SUCCESS;
}


RTCRestObjectBase *RTCRestInt32::baseClone() const RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestInt32(*this);
}


int RTCRestInt32::resetToDefault() RT_NOEXCEPT
{
    m_iValue = 0;
    m_fNullIndicator = false;
    return VINF_SUCCESS;
}


void RTCRestInt32::assignValue(int32_t a_iValue) RT_NOEXCEPT
{
    m_iValue = a_iValue;
    m_fNullIndicator = false;
}


RTCRestOutputBase &RTCRestInt32::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
        a_rDst.printf("%RI32", m_iValue);
    else
        a_rDst.nullValue();
    return a_rDst;
}


int RTCRestInt32::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    m_iValue = 0;
    m_fNullIndicator = false;

    RTJSONVALTYPE enmType = RTJsonValueGetType(a_rCursor.m_hValue);
    if (enmType == RTJSONVALTYPE_INTEGER)
    {
        int64_t iTmp = m_iValue;
        int rc = RTJsonValueQueryInteger(a_rCursor.m_hValue, &iTmp);
        if (RT_SUCCESS(rc))
        {
            m_iValue = (int32_t)iTmp;
            if (m_iValue == iTmp)
                return rc;
            return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_OUT_OF_RANGE, "value %RI64 does not fit in 32 bits", iTmp);
        }
        return a_rCursor.m_pPrimary->addError(a_rCursor, rc, "RTJsonValueQueryInteger failed with %Rrc", rc);
    }

    if (enmType == RTJSONVALTYPE_NULL)
    {
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    /* This is probably non-sense... */
    if (enmType == RTJSONVALTYPE_TRUE)
        m_iValue = 1;

    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_REST_WRONG_JSON_TYPE_FOR_INTEGER, "wrong JSON type %s for 32-bit integer",
                                          RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
}


int RTCRestInt32::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    if (!(a_fFlags & kToString_Append))
    {
        if (!m_fNullIndicator)
            return a_pDst->printfNoThrow("%RI32", m_iValue);
        return a_pDst->assignNoThrow(RT_STR_TUPLE("null"));
    }
    if (!m_fNullIndicator)
        return a_pDst->appendPrintfNoThrow("%RI32", m_iValue);
    return a_pDst->appendNoThrow(RT_STR_TUPLE("null"));
}


int RTCRestInt32::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                             uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    RT_NOREF(a_fFlags);

    m_iValue = 0;
    m_fNullIndicator = false;

/** @todo RTStrStripL and RTStrToInt32Full has a different idea what consitutes spaces... */
    int rc = RTStrToInt32Full(RTStrStripL(a_rValue.c_str()), 0, &m_iValue);
    if (rc == VINF_SUCCESS || rc == VERR_TRAILING_SPACES)
        return VINF_SUCCESS;

    if (a_rValue.startsWithWord("null", RTCString::CaseInsensitive))
    {
        m_iValue = 0;
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    return RTErrInfoSetF(a_pErrInfo, rc, "%s: error %Rrc parsing '%s' as int32_t", a_pszName, rc, a_rValue.c_str());
}


RTCRestObjectBase::kTypeClass RTCRestInt32::typeClass() const RT_NOEXCEPT
{
    return kTypeClass_Int32;
}


const char *RTCRestInt32::typeName() const RT_NOEXCEPT
{
    return "int32_t";
}



/*********************************************************************************************************************************
*   RTCRestInt16 implementation                                                                                                  *
*********************************************************************************************************************************/

/** Factory method. */
/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestInt16::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestInt16();
}


/**
 * @copydoc RTCRestObjectBase::FNDESERIALIZEINSTANCEFROMJSON
 */
/*static*/ DECLCALLBACK(int)
RTCRestInt16::deserializeInstanceFromJson(RTCRestJsonCursor const &a_rCursor, RTCRestObjectBase **a_ppInstance) RT_NOEXCEPT
{
    RTCRestObjectBase *pObj = createInstance();
    *a_ppInstance = pObj;
    if (pObj)
        return pObj->deserializeFromJson(a_rCursor);
    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_NO_MEMORY, "Out of memory");
}


/** Default constructor. */
RTCRestInt16::RTCRestInt16() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_iValue(0)
{
}


/** Copy constructor. */
RTCRestInt16::RTCRestInt16(RTCRestInt16 const &a_rThat) RT_NOEXCEPT
    : RTCRestObjectBase(a_rThat)
    , m_iValue(a_rThat.m_iValue)
{
}


/** From value constructor. */
RTCRestInt16::RTCRestInt16(int16_t iValue) RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_iValue(iValue)
{
}


/** Destructor. */
RTCRestInt16::~RTCRestInt16()
{
    /* nothing to do */
}


/** Copy assignment operator. */
RTCRestInt16 &RTCRestInt16::operator=(RTCRestInt16 const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_iValue = a_rThat.m_iValue;
    return *this;
}


int RTCRestInt16::assignCopy(RTCRestInt16 const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_iValue = a_rThat.m_iValue;
    return VINF_SUCCESS;
}


void RTCRestInt16::assignValue(int16_t a_iValue) RT_NOEXCEPT
{
    m_iValue = a_iValue;
    m_fNullIndicator = false;
}


RTCRestObjectBase *RTCRestInt16::baseClone() const RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestInt16(*this);
}


int RTCRestInt16::resetToDefault() RT_NOEXCEPT
{
    m_iValue = 0;
    m_fNullIndicator = false;
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestInt16::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
        a_rDst.printf("%RI16", m_iValue);
    else
        a_rDst.nullValue();
    return a_rDst;
}


int RTCRestInt16::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    m_iValue = 0;
    m_fNullIndicator = false;

    RTJSONVALTYPE enmType = RTJsonValueGetType(a_rCursor.m_hValue);
    if (enmType == RTJSONVALTYPE_INTEGER)
    {
        int64_t iTmp = m_iValue;
        int rc = RTJsonValueQueryInteger(a_rCursor.m_hValue, &iTmp);
        if (RT_SUCCESS(rc))
        {
            m_iValue = (int16_t)iTmp;
            if (m_iValue == iTmp)
                return rc;
            return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_OUT_OF_RANGE, "value %RI64 does not fit in 16 bits", iTmp);
        }
        return a_rCursor.m_pPrimary->addError(a_rCursor, rc, "RTJsonValueQueryInteger failed with %Rrc", rc);
    }

    if (enmType == RTJSONVALTYPE_NULL)
    {
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    /* This is probably non-sense... */
    if (enmType == RTJSONVALTYPE_TRUE)
        m_iValue = 1;

    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_REST_WRONG_JSON_TYPE_FOR_INTEGER, "wrong JSON type %s for 16-bit integer",
                                          RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
}


int RTCRestInt16::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    if (!(a_fFlags & kToString_Append))
    {
        if (!m_fNullIndicator)
            return a_pDst->printfNoThrow("%RI16", m_iValue);
        return a_pDst->assignNoThrow(RT_STR_TUPLE("null"));
    }
    if (!m_fNullIndicator)
        return a_pDst->appendPrintfNoThrow("%RI16", m_iValue);
    return a_pDst->appendNoThrow(RT_STR_TUPLE("null"));
}


int RTCRestInt16::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                             uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    RT_NOREF(a_fFlags);

    m_iValue = 0;
    m_fNullIndicator = false;

/** @todo RTStrStripL and RTStrToInt16Full has a different idea what consitutes spaces... */
    int rc = RTStrToInt16Full(RTStrStripL(a_rValue.c_str()), 0, &m_iValue);
    if (rc == VINF_SUCCESS || rc == VERR_TRAILING_SPACES)
        return VINF_SUCCESS;

    if (a_rValue.startsWithWord("null", RTCString::CaseInsensitive))
    {
        m_iValue = 0;
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    return RTErrInfoSetF(a_pErrInfo, rc, "%s: error %Rrc parsing '%s' as int16_t", a_pszName, rc, a_rValue.c_str());
}


RTCRestObjectBase::kTypeClass RTCRestInt16::typeClass() const RT_NOEXCEPT
{
    return kTypeClass_Int16;
}


const char *RTCRestInt16::typeName() const RT_NOEXCEPT
{
    return "int16_t";
}



/*********************************************************************************************************************************
*   RTCRestDouble implementation                                                                                                 *
*********************************************************************************************************************************/

/** Factory method. */
/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestDouble::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestDouble();
}


/**
 * @copydoc RTCRestObjectBase::FNDESERIALIZEINSTANCEFROMJSON
 */
/*static*/ DECLCALLBACK(int)
RTCRestDouble::deserializeInstanceFromJson(RTCRestJsonCursor const &a_rCursor, RTCRestObjectBase **a_ppInstance) RT_NOEXCEPT
{
    RTCRestObjectBase *pObj = createInstance();
    *a_ppInstance = pObj;
    if (pObj)
        return pObj->deserializeFromJson(a_rCursor);
    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_NO_MEMORY, "Out of memory");
}


/** Default constructor. */
RTCRestDouble::RTCRestDouble() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_rdValue(0.0)
{
}


/** Copy constructor. */
RTCRestDouble::RTCRestDouble(RTCRestDouble const &a_rThat) RT_NOEXCEPT
    : RTCRestObjectBase(a_rThat)
    , m_rdValue(a_rThat.m_rdValue)
{
}


/** From value constructor. */
RTCRestDouble::RTCRestDouble(double rdValue) RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_rdValue(rdValue)
{
}


/** Destructor. */
RTCRestDouble::~RTCRestDouble()
{
    /* nothing to do */
}


/** Copy assignment operator. */
RTCRestDouble &RTCRestDouble::operator=(RTCRestDouble const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_rdValue = a_rThat.m_rdValue;
    return *this;
}


int RTCRestDouble::assignCopy(RTCRestDouble const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_rdValue = a_rThat.m_rdValue;
    return VINF_SUCCESS;
}


void RTCRestDouble::assignValue(double a_rdValue) RT_NOEXCEPT
{
    m_rdValue = a_rdValue;
    m_fNullIndicator = false;
}


RTCRestObjectBase *RTCRestDouble::baseClone() const RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestDouble(*this);
}


int RTCRestDouble::resetToDefault() RT_NOEXCEPT
{
    m_rdValue = 0.0;
    m_fNullIndicator = false;
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestDouble::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
    {

        /* Just a simple approximation here. */
        /** @todo Not 100% sure printf %g produces the right result for JSON floating point, but it'll have to do for now... */
        char szValue[128];
#ifdef _MSC_VER
        _snprintf(szValue, sizeof(szValue), "%.18g", m_rdValue);
#else
        snprintf(szValue, sizeof(szValue), "%.18g", m_rdValue);
#endif
        size_t cchValue = strlen(szValue);
        while (cchValue > 0 && szValue[cchValue - 1] == '0')
            cchValue--;
        szValue[cchValue] = '\0';

        a_rDst.printf("%s", szValue);
    }
    else
        a_rDst.nullValue();
    return a_rDst;
}


int RTCRestDouble::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    m_rdValue = 0.0;
    m_fNullIndicator = false;

    RTJSONVALTYPE enmType = RTJsonValueGetType(a_rCursor.m_hValue);
    if (enmType == RTJSONVALTYPE_NUMBER)
    {
        int rc = RTJsonValueQueryNumber(a_rCursor.m_hValue, &m_rdValue);
        if (RT_SUCCESS(rc))
            return rc;
        return a_rCursor.m_pPrimary->addError(a_rCursor, rc, "RTJsonValueQueryNumber failed with %Rrc", rc);
    }

    if (enmType == RTJSONVALTYPE_INTEGER)
    {
        int64_t iTmp = 0;
        int rc = RTJsonValueQueryInteger(a_rCursor.m_hValue, &iTmp);
        if (RT_SUCCESS(rc))
        {
            m_rdValue = iTmp;
            if ((int64_t)m_rdValue == iTmp)
                return rc;
            return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_OUT_OF_RANGE, "value %RI64 does not fit in a double", iTmp);
        }
        return a_rCursor.m_pPrimary->addError(a_rCursor, rc, "RTJsonValueQueryInteger failed with %Rrc", rc);
    }

    if (enmType == RTJSONVALTYPE_NULL)
    {
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    /* This is probably non-sense... */
    if (enmType == RTJSONVALTYPE_TRUE)
        m_rdValue = 1.0;

    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_REST_WRONG_JSON_TYPE_FOR_DOUBLE, "wrong JSON type %s for a double",
                                          RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
}


int RTCRestDouble::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
    {
        /* Just a simple approximation here. */
        /** @todo Not 100% sure printf %g produces the right result for JSON floating point, but it'll have to do for now... */
        char szValue[128];
#ifdef _MSC_VER
        _snprintf(szValue, sizeof(szValue), "%.18g", m_rdValue);
#else
        snprintf(szValue, sizeof(szValue), "%.18g", m_rdValue);
#endif
        size_t cchValue = strlen(szValue);
        while (cchValue > 0 && szValue[cchValue - 1] == '0')
            cchValue--;
        szValue[cchValue] = '\0';

        if (!(a_fFlags & kToString_Append))
            return a_pDst->assignNoThrow(szValue, cchValue);
        return a_pDst->appendNoThrow(szValue, cchValue);
    }

    if (!(a_fFlags & kToString_Append))
        return a_pDst->assignNoThrow(RT_STR_TUPLE("null"));
    return a_pDst->appendNoThrow(RT_STR_TUPLE("null"));
}


int RTCRestDouble::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                              uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    RT_NOREF(a_fFlags);

    if (a_rValue.startsWithWord("null", RTCString::CaseInsensitive))
    {
        m_rdValue = 0.0;
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    m_fNullIndicator = false;

    const char *pszValue = RTStrStripL(a_rValue.c_str());
    errno = 0;
    char *pszNext = NULL;
    m_rdValue = strtod(pszValue, &pszNext);
    if (errno == 0 && pszNext != pszValue)
    {
        if (!pszNext || *pszNext == '\0')
            return VINF_SUCCESS;

        while (RT_C_IS_SPACE(*pszNext))
            pszNext++;
        if (*pszNext == '\0')
            return VINF_SUCCESS;

        return RTErrInfoSetF(a_pErrInfo, VERR_TRAILING_CHARS, "%s: error VERR_TRAILING_CHARS parsing '%s' as double",
                             a_pszName, a_rValue.c_str());
    }

    if (!RT_C_IS_DIGIT(*pszValue) && *pszValue != '.')
        return RTErrInfoSetF(a_pErrInfo, VERR_NO_DIGITS, "%s: error VERR_NO_DIGITS parsing '%s' as double",
                             a_pszName, a_rValue.c_str());
    int rc = RTErrConvertFromErrno(errno);
    return RTErrInfoSetF(a_pErrInfo, rc, "%s: error %Rrc parsing '%s' as double", a_pszName, rc, a_rValue.c_str());
}


RTCRestObjectBase::kTypeClass RTCRestDouble::typeClass() const RT_NOEXCEPT
{
    return kTypeClass_Double;
}


const char *RTCRestDouble::typeName() const RT_NOEXCEPT
{
    return "double";
}



/*********************************************************************************************************************************
*   RTCRestString implementation                                                                                                 *
*********************************************************************************************************************************/

/** Factory method. */
/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestString::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestString();
}


/**
 * @copydoc RTCRestObjectBase::FNDESERIALIZEINSTANCEFROMJSON
 */
/*static*/ DECLCALLBACK(int)
RTCRestString::deserializeInstanceFromJson(RTCRestJsonCursor const &a_rCursor, RTCRestObjectBase **a_ppInstance) RT_NOEXCEPT
{
    RTCRestObjectBase *pObj = createInstance();
    *a_ppInstance = pObj;
    if (pObj)
        return pObj->deserializeFromJson(a_rCursor);
    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_NO_MEMORY, "Out of memory");
}


/** Default constructor. */
RTCRestString::RTCRestString() RT_NOEXCEPT
    : RTCRestObjectBase()
    , RTCString()
{
}


/** Copy constructor. */
RTCRestString::RTCRestString(RTCRestString const &a_rThat)
    : RTCRestObjectBase(a_rThat)
    , RTCString(a_rThat)
{
}


/** From value constructor. */
RTCRestString::RTCRestString(RTCString const &a_rThat)
    : RTCString(a_rThat)
{
}


/** From value constructor. */
RTCRestString::RTCRestString(const char *a_pszSrc)
    : RTCRestObjectBase()
    , RTCString(a_pszSrc)
{
}


/** Destructor. */
RTCRestString::~RTCRestString()
{
    /* nothing to do */
}


int RTCRestString::assignCopy(RTCRestString const &a_rThat) RT_NOEXCEPT
{
    int rc = assignNoThrow(a_rThat);
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    return rc;
}


int RTCRestString::assignCopy(RTCString const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    return assignNoThrow(a_rThat);
}


int RTCRestString::assignCopy(const char *a_pszThat) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    return assignNoThrow(a_pszThat);
}


int RTCRestString::setNull() RT_NOEXCEPT
{
    RTCString::setNull();
    m_fNullIndicator = true;
    return VINF_SUCCESS;
}


RTCRestObjectBase *RTCRestString::baseClone() const RT_NOEXCEPT
{
    RTCRestString *pClone = new (std::nothrow) RTCRestString();
    if (pClone)
    {
        int rc = pClone->assignCopy(*this);
        if (RT_SUCCESS(rc))
            return pClone;
        delete pClone;
    }
    return NULL;
}


int RTCRestString::resetToDefault() RT_NOEXCEPT
{
    RTCString::setNull();
    m_fNullIndicator = false;
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestString::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
        a_rDst.printf("%RMjs", m_psz ? m_psz : "");
    else
        a_rDst.nullValue();
    return a_rDst;
}


int RTCRestString::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    m_fNullIndicator = false;

    RTJSONVALTYPE enmType = RTJsonValueGetType(a_rCursor.m_hValue);
    if (enmType == RTJSONVALTYPE_STRING)
    {
        const char  *pszValue = RTJsonValueGetString(a_rCursor.m_hValue);
        const size_t cchValue = strlen(pszValue);
        int rc = assignNoThrow(pszValue, cchValue);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;
        return a_rCursor.m_pPrimary->addError(a_rCursor, rc, "no memory for %zu char long string", cchValue);
    }

    RTCString::setNull();

    if (enmType == RTJSONVALTYPE_NULL)
    {
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_REST_WRONG_JSON_TYPE_FOR_STRING, "wrong JSON type %s for string",
                                          RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
}


int RTCRestString::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    /* Note! m_fNullIndicator == true: empty string. */
    if (!(a_fFlags & kToString_Append))
        return a_pDst->assignNoThrow(*this);
    return a_pDst->appendNoThrow(*this);
}


int RTCRestString::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                              uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    RT_NOREF(a_fFlags); RT_NOREF(a_pszName); RT_NOREF(a_pErrInfo);

    /* Note! Unable to set m_fNullIndicator = true here. */
    m_fNullIndicator = false;
    return assignNoThrow(a_rValue);
}


RTCRestObjectBase::kTypeClass RTCRestString::typeClass() const RT_NOEXCEPT
{
    return kTypeClass_String;
}


const char *RTCRestString::typeName() const RT_NOEXCEPT
{
    return "RTCString";
}


int RTCRestString::assignNoThrow(const RTCString &a_rSrc) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    return RTCString::assignNoThrow(a_rSrc);
}


int RTCRestString::assignNoThrow(const char *a_pszSrc) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    return RTCString::assignNoThrow(a_pszSrc);
}


int RTCRestString::assignNoThrow(const RTCString &a_rSrc, size_t a_offSrc, size_t a_cchSrc /*= npos*/) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    return RTCString::assignNoThrow(a_rSrc, a_offSrc, a_cchSrc);
}


int RTCRestString::assignNoThrow(const char *a_pszSrc, size_t a_cchSrc) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    return RTCString::assignNoThrow(a_pszSrc, a_cchSrc);
}


int RTCRestString::assignNoThrow(size_t a_cTimes, char a_ch) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    return RTCString::assignNoThrow(a_cTimes, a_ch);
}


int RTCRestString::printfNoThrow(const char *pszFormat, ...) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    va_list va;
    va_start(va, pszFormat);
    int rc = RTCString::printfVNoThrow(pszFormat, va);
    va_end(va);
    return rc;
}


int RTCRestString::printfVNoThrow(const char *pszFormat, va_list va) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    return RTCString::printfVNoThrow(pszFormat, va);
}


RTCRestString &RTCRestString::operator=(const char *a_pcsz)
{
    m_fNullIndicator = false;
    RTCString::operator=(a_pcsz);
    return *this;
}


RTCRestString &RTCRestString::operator=(const RTCString &a_rThat)
{
    m_fNullIndicator = false;
    RTCString::operator=(a_rThat);
    return *this;
}


RTCRestString &RTCRestString::operator=(const RTCRestString &a_rThat)
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    RTCString::operator=(a_rThat);
    return *this;
}


RTCRestString &RTCRestString::assign(const RTCString &a_rSrc)
{
    m_fNullIndicator = false;
    RTCString::assign(a_rSrc);
    return *this;
}


RTCRestString &RTCRestString::assign(const char *a_pszSrc)
{
    m_fNullIndicator = false;
    RTCString::assign(a_pszSrc);
    return *this;
}


RTCRestString &RTCRestString::assign(const RTCString &a_rSrc, size_t a_offSrc, size_t a_cchSrc /*= npos*/)
{
    m_fNullIndicator = false;
    RTCString::assign(a_rSrc, a_offSrc, a_cchSrc);
    return *this;
}


RTCRestString &RTCRestString::assign(const char *a_pszSrc, size_t a_cchSrc)
{
    m_fNullIndicator = false;
    RTCString::assign(a_pszSrc, a_cchSrc);
    return *this;
}


RTCRestString &RTCRestString::assign(size_t a_cTimes, char a_ch)
{
    m_fNullIndicator = false;
    RTCString::assign(a_cTimes, a_ch);
    return *this;
}


RTCRestString &RTCRestString::printf(const char *pszFormat, ...)
{
    m_fNullIndicator = false;
    va_list va;
    va_start(va, pszFormat);
    RTCString::printfV(pszFormat, va);
    va_end(va);
    return *this;
}


RTCRestString &RTCRestString::printfV(const char *pszFormat, va_list va)
{
    m_fNullIndicator = false;
    RTCString::printfV(pszFormat, va);
    return *this;
}



/*********************************************************************************************************************************
*   RTCRestDate implementation                                                                                                   *
*********************************************************************************************************************************/
/*static*/ DECLCALLBACK(RTCRestObjectBase *) RTCRestDate::createInstance(void) RT_NOEXCEPT
{
    return new (std::nothrow) RTCRestDate();
}


/**
 * @copydoc RTCRestObjectBase::FNDESERIALIZEINSTANCEFROMJSON
 */
/*static*/ DECLCALLBACK(int)
RTCRestDate::deserializeInstanceFromJson(RTCRestJsonCursor const &a_rCursor, RTCRestObjectBase **a_ppInstance) RT_NOEXCEPT
{
    RTCRestObjectBase *pObj = createInstance();
    *a_ppInstance = pObj;
    if (pObj)
        return pObj->deserializeFromJson(a_rCursor);
    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_NO_MEMORY, "Out of memory");
}


RTCRestDate::RTCRestDate() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_fTimeSpecOkay(false)
    , m_enmFormat(kFormat_Invalid)
    , m_strFormatted()
{
    RTTimeSpecSetNano(&m_TimeSpec, 0);
    RT_ZERO(m_Exploded);

    /* Since we need to know the format, all date-time values default to 'null'. */
    m_fNullIndicator = true;
}


RTCRestDate::RTCRestDate(RTCRestDate const &a_rThat)
    : RTCRestObjectBase(a_rThat)
    , m_fTimeSpecOkay(a_rThat.m_fTimeSpecOkay)
    , m_enmFormat(a_rThat.m_enmFormat)
    , m_strFormatted(a_rThat.m_strFormatted)
{
    m_TimeSpec = a_rThat.m_TimeSpec;
    m_Exploded = a_rThat.m_Exploded;
}


RTCRestDate::~RTCRestDate()
{
    /* nothing to do */
}


RTCRestDate &RTCRestDate::operator=(RTCRestDate const &a_rThat)
{
    RTCRestObjectBase::operator=(a_rThat);
    m_TimeSpec      = a_rThat.m_TimeSpec;
    m_Exploded      = a_rThat.m_Exploded;
    m_fTimeSpecOkay = a_rThat.m_fTimeSpecOkay;
    m_enmFormat     = a_rThat.m_enmFormat;
    m_strFormatted  = a_rThat.m_strFormatted;
    return *this;
}


int RTCRestDate::assignCopy(RTCRestDate const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_TimeSpec       = a_rThat.m_TimeSpec;
    m_Exploded       = a_rThat.m_Exploded;
    m_fTimeSpecOkay  = a_rThat.m_fTimeSpecOkay;
    m_enmFormat      = a_rThat.m_enmFormat;
    return m_strFormatted.assignNoThrow(a_rThat.m_strFormatted);
}


RTCRestObjectBase *RTCRestDate::baseClone() const RT_NOEXCEPT
{
    RTCRestDate *pClone = new (std::nothrow) RTCRestDate();
    if (pClone)
    {
        int rc = pClone->assignCopy(*this);
        if (RT_SUCCESS(rc))
            return pClone;
        delete pClone;
    }
    return NULL;
}


int RTCRestDate::resetToDefault() RT_NOEXCEPT
{
    m_fNullIndicator = true;
    RTTimeSpecSetNano(&m_TimeSpec, 0);
    RT_ZERO(m_Exploded);
    m_fTimeSpecOkay = false;
    m_strFormatted.setNull();
    /*m_enmFormat - leave as hint. */
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestDate::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (m_fNullIndicator)
        a_rDst.nullValue();
    else
        a_rDst.printf("%RMjs", m_strFormatted.c_str());
    return a_rDst;
}


int RTCRestDate::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    setNull();

    RTJSONVALTYPE enmType = RTJsonValueGetType(a_rCursor.m_hValue);
    if (enmType == RTJSONVALTYPE_STRING)
    {
        int rc = m_strFormatted.assignNoThrow(RTJsonValueGetString(a_rCursor.m_hValue));
        AssertRCReturn(rc, rc);

        m_fNullIndicator = false;
        rc = decodeFormattedString(m_enmFormat);
        if (RT_SUCCESS(rc))
            return rc;
        if (m_enmFormat != kFormat_Invalid)
        {
            rc = decodeFormattedString();
            if (RT_SUCCESS(rc))
                return rc;
        }
        return a_rCursor.m_pPrimary->addError(a_rCursor, VWRN_REST_UNABLE_TO_DECODE_DATE,
                                              "Unable to decode date value: %s", m_strFormatted.c_str());
    }

    if (enmType == RTJSONVALTYPE_NULL)
        return VINF_SUCCESS;

    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_REST_WRONG_JSON_TYPE_FOR_DATE, "wrong JSON type for date: %s",
                                          RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
}


int RTCRestDate::toString(RTCString *a_pDst, uint32_t a_fFlags /*= 0*/) const RT_NOEXCEPT
{
    if (m_fNullIndicator)
    {
        if (a_fFlags & kToString_Append)
            return a_pDst->appendNoThrow(RT_STR_TUPLE("null"));
        return a_pDst->assignNoThrow(RT_STR_TUPLE("null"));
    }
    if (a_fFlags & kToString_Append)
        return a_pDst->appendNoThrow(m_strFormatted);
    return a_pDst->assignNoThrow(m_strFormatted);
}


int RTCRestDate::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                            uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    setNull();
    if (a_rValue.startsWithWord("null", RTCString::CaseInsensitive))
        return VINF_SUCCESS;

    int rc = m_strFormatted.assignNoThrow(a_rValue);
    AssertRCReturn(rc, rc);

    m_fNullIndicator = false;
    rc = decodeFormattedString(m_enmFormat);
    if (RT_SUCCESS(rc))
        return rc;
    if (m_enmFormat != kFormat_Invalid)
    {
        rc = decodeFormattedString();
        if (RT_SUCCESS(rc))
            return rc;
    }
    RT_NOREF(a_fFlags);
    return RTErrInfoSetF(a_pErrInfo, VERR_REST_UNABLE_TO_DECODE_DATE,
                         "Unable to decode date value (%s): %s", a_pszName, m_strFormatted.c_str());
}


RTCRestObjectBase::kTypeClass RTCRestDate::typeClass(void) const RT_NOEXCEPT
{
    return kTypeClass_Date;
}


const char *RTCRestDate::typeName(void) const RT_NOEXCEPT
{
    return "RTCRestDate";
}


int RTCRestDate::assignValue(PCRTTIMESPEC a_pTimeSpec, kFormat a_enmFormat) RT_NOEXCEPT
{
    AssertPtrReturn(a_pTimeSpec, VERR_INVALID_PARAMETER);
    AssertReturn(a_enmFormat > kFormat_Invalid && a_enmFormat < kFormat_End, VERR_INVALID_PARAMETER);

    m_TimeSpec = *a_pTimeSpec;
    return explodeAndFormat(a_enmFormat);
}


int RTCRestDate::assignValueRfc2822(PCRTTIMESPEC a_pTimeSpec) RT_NOEXCEPT
{
    AssertPtrReturn(a_pTimeSpec, VERR_INVALID_PARAMETER);
    m_TimeSpec = *a_pTimeSpec;
    return explodeAndFormat(kFormat_Rfc2822);
}


int RTCRestDate::assignValueRfc7131(PCRTTIMESPEC a_pTimeSpec) RT_NOEXCEPT
{
    AssertPtrReturn(a_pTimeSpec, VERR_INVALID_PARAMETER);
    m_TimeSpec = *a_pTimeSpec;
    return explodeAndFormat(kFormat_Rfc7131);
}


int RTCRestDate::assignValueRfc3339(PCRTTIMESPEC a_pTimeSpec) RT_NOEXCEPT
{
    AssertPtrReturn(a_pTimeSpec, VERR_INVALID_PARAMETER);
    m_TimeSpec = *a_pTimeSpec;
    return explodeAndFormat(kFormat_Rfc3339);
}


int RTCRestDate::assignNow(kFormat a_enmFormat) RT_NOEXCEPT
{
    RTTIMESPEC Now;
    return assignValue(RTTimeNow(&Now), a_enmFormat);
}


int RTCRestDate::assignNowRfc2822() RT_NOEXCEPT
{
    RTTIMESPEC Now;
    return assignValueRfc2822(RTTimeNow(&Now));
}


int RTCRestDate::assignNowRfc7131() RT_NOEXCEPT
{
    RTTIMESPEC Now;
    return assignValueRfc7131(RTTimeNow(&Now));
}


int RTCRestDate::assignNowRfc3339() RT_NOEXCEPT
{
    RTTIMESPEC Now;
    return assignValueRfc3339(RTTimeNow(&Now));
}


int RTCRestDate::setFormat(kFormat a_enmFormat) RT_NOEXCEPT
{
    /*
     * If this is a null object, just set the format as a hint for upcoming deserialization.
     */
    if (m_fNullIndicator)
    {
        AssertReturn(a_enmFormat >= kFormat_Invalid && a_enmFormat < kFormat_End, VERR_INVALID_PARAMETER);
        m_enmFormat = a_enmFormat;
        return VINF_SUCCESS;
    }

    /*
     * If the tiem spec is okay, just reformat the string value accordingly.
     */
    if (m_fTimeSpecOkay)
    {
        AssertReturn(a_enmFormat > kFormat_Invalid && a_enmFormat < kFormat_End, VERR_INVALID_PARAMETER);
        if (a_enmFormat ==  m_enmFormat)
            return VINF_SUCCESS;
        return format(a_enmFormat);
    }

    /*
     * Try decode
     */
    AssertReturn(a_enmFormat > kFormat_Invalid && a_enmFormat < kFormat_End, VERR_INVALID_PARAMETER);
    return decodeFormattedString(a_enmFormat);
}


int RTCRestDate::explodeAndFormat(kFormat a_enmFormat) RT_NOEXCEPT
{
    RTTimeExplode(&m_Exploded, &m_TimeSpec);
    return format(a_enmFormat);
}


/**
 * Formats the m_Exploded value.
 *
 * Sets m_strFormatted, m_fTimeSpecOkay, and m_enmFormat, clears m_fNullIndicator.
 *
 * @returns VINF_SUCCESS or VERR_NO_STR_MEMORY.
 * @param   a_enmFormat The format to use.
 */
int RTCRestDate::format(kFormat a_enmFormat) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    m_fTimeSpecOkay  = true;
    m_enmFormat      = a_enmFormat;
    int rc;
    switch (a_enmFormat)
    {
        case kFormat_Rfc2822:
        case kFormat_Rfc7131:
            rc = m_strFormatted.reserveNoThrow(RTTIME_RFC2822_LEN);
            AssertRCReturn(rc, rc);
            RTTimeToRfc2822(&m_Exploded, m_strFormatted.mutableRaw(), m_strFormatted.capacity(),
                            a_enmFormat == kFormat_Rfc7131 ? RTTIME_RFC2822_F_GMT : 0);
            m_strFormatted.jolt();
            return VINF_SUCCESS;

        case kFormat_Rfc3339:
        case kFormat_Rfc3339_Fraction_2:
        case kFormat_Rfc3339_Fraction_3:
        case kFormat_Rfc3339_Fraction_6:
        case kFormat_Rfc3339_Fraction_9:
            rc = m_strFormatted.reserveNoThrow(RTTIME_STR_LEN);
            AssertRCReturn(rc, rc);
            RTTimeToStringEx(&m_Exploded, m_strFormatted.mutableRaw(), m_strFormatted.capacity(),
                             a_enmFormat == kFormat_Rfc3339 ? 0
                             : a_enmFormat == kFormat_Rfc3339_Fraction_2 ? 2
                             : a_enmFormat == kFormat_Rfc3339_Fraction_3 ? 3
                             : a_enmFormat == kFormat_Rfc3339_Fraction_6 ? 6 : 9);
            m_strFormatted.jolt();
            return VINF_SUCCESS;

        /* no default */
        case kFormat_Invalid:
        case kFormat_End:
            break;
    }
    AssertFailedReturn(VERR_REST_INTERNAL_ERROR_7);
}


/**
 * Internal worker that attempts to decode m_strFormatted.
 *
 * Sets m_fTimeSpecOkay.
 *
 * @returns IPRT status code.
 * @param   enmFormat   Specific format to try, kFormat_Invalid (default) to try guess it.
 */
int RTCRestDate::decodeFormattedString(kFormat enmFormat /*= kFormat_Invalid*/) RT_NOEXCEPT
{
    /*
     * Take empty string to mean null.
     */
    const char *pszTmp = RTStrStripL(m_strFormatted.c_str());
    if (*pszTmp == '\0')
    {
        setNull();
        return VINF_SUCCESS;
    }

    switch (enmFormat)
    {
        case kFormat_Invalid:
        {
            size_t cch = strlen(pszTmp);
            if (cch >= 6)
            {
                if (   !RT_C_IS_DIGIT(pszTmp[0])
                    || RT_C_IS_SPACE(pszTmp[5])
                    || RT_C_IS_SPACE(pszTmp[2])
                    || RT_C_IS_SPACE(pszTmp[1])
                    || RT_C_IS_SPACE(pszTmp[3])
                    || RT_C_IS_SPACE(pszTmp[4]))
                    return decodeFormattedString(kFormat_Rfc2822);
                return decodeFormattedString(kFormat_Rfc3339);
            }
            return VERR_REST_UNABLE_TO_DECODE_DATE;
        }

        /*
         * Examples:
         *      Fri, 31 Aug 2018 00:00:00 +0200
         *      Mon, 3 Sep 2018 00:00:00 GMT
         *      Mon, 3 Sep 2018 00:00:00 -0000
         *      3 Sep 2018 00:00:00 -0000 (?)
         *      3 Sep 2018 00:00:00 GMT   (?)
         */
        case kFormat_Rfc2822:
        case kFormat_Rfc7131:
            if (RTTimeFromRfc2822(&m_Exploded, pszTmp))
            {
                RTTimeImplode(&m_TimeSpec, &m_Exploded);

                pszTmp = strchr(pszTmp, '\0');
                if (pszTmp[-1] == 'T' || pszTmp[-1] == 't')
                    m_enmFormat = kFormat_Rfc7131;
                else
                    m_enmFormat = kFormat_Rfc2822;
                m_fTimeSpecOkay = true;
                return VINF_SUCCESS;
            }
            return VERR_REST_UNABLE_TO_DECODE_DATE;

        /*
         * Examples:
         *      2018-08-31T00:00:00+0200
         *      2018-09-03T00:00:00Z
         *      2018-09-03T00:00:00+0000
         *      2018-09-03T00:00:00.123456789Z
         */
        case kFormat_Rfc3339:
        case kFormat_Rfc3339_Fraction_2:
        case kFormat_Rfc3339_Fraction_3:
        case kFormat_Rfc3339_Fraction_6:
        case kFormat_Rfc3339_Fraction_9:
            if (RTTimeFromString(&m_Exploded, pszTmp))
            {
                RTTimeImplode(&m_TimeSpec, &m_Exploded);

                pszTmp = strchr(pszTmp, '.');
                if (!pszTmp)
                    m_enmFormat = kFormat_Rfc3339;
                else
                {
                    size_t cchFraction = 0;
                    pszTmp++;
                    while (RT_C_IS_DIGIT(pszTmp[cchFraction]))
                        cchFraction++;
                    if (cchFraction == 0)
                        m_enmFormat = kFormat_Rfc3339;
                    else if (cchFraction <= 2)
                        m_enmFormat = kFormat_Rfc3339_Fraction_2;
                    else if (cchFraction <= 3)
                        m_enmFormat = kFormat_Rfc3339_Fraction_3;
                    else if (cchFraction <= 6)
                        m_enmFormat = kFormat_Rfc3339_Fraction_6;
                    else
                        m_enmFormat = kFormat_Rfc3339_Fraction_9;
                }
                m_fTimeSpecOkay = true;
                return VINF_SUCCESS;
            }
            return VERR_REST_UNABLE_TO_DECODE_DATE;

        /* no default */
        case kFormat_End:
            break;
    }
    AssertFailedReturn(VERR_INVALID_PARAMETER);
}


/*********************************************************************************************************************************
*   RTCRestStringEnumBase implementation                                                                                         *
*********************************************************************************************************************************/

/** Default constructor. */
RTCRestStringEnumBase::RTCRestStringEnumBase() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_iEnumValue(0 /*invalid*/)
    , m_strValue()
{
}


/** Destructor. */
RTCRestStringEnumBase::~RTCRestStringEnumBase()
{
    /* nothing to do */
}


/** Copy constructor. */
RTCRestStringEnumBase::RTCRestStringEnumBase(RTCRestStringEnumBase const &a_rThat)
    : RTCRestObjectBase(a_rThat)
    , m_iEnumValue(a_rThat.m_iEnumValue)
    , m_strValue(a_rThat.m_strValue)
{
}


/** Copy assignment operator. */
RTCRestStringEnumBase &RTCRestStringEnumBase::operator=(RTCRestStringEnumBase const &a_rThat)
{
    RTCRestObjectBase::operator=(a_rThat);
    m_iEnumValue = a_rThat.m_iEnumValue;
    m_strValue = a_rThat.m_strValue;
    return *this;
}


int RTCRestStringEnumBase::assignCopy(RTCRestStringEnumBase const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_iEnumValue = a_rThat.m_iEnumValue;
    return m_strValue.assignNoThrow(a_rThat.m_strValue);
}


int RTCRestStringEnumBase::resetToDefault() RT_NOEXCEPT
{
    m_iEnumValue = 0; /*invalid*/
    m_strValue.setNull();
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestStringEnumBase::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
        a_rDst.printf("%RMjs", getString());
    else
        a_rDst.nullValue();
    return a_rDst;
}


int RTCRestStringEnumBase::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    m_fNullIndicator = false;
    m_iEnumValue     = 0;

    RTJSONVALTYPE enmType = RTJsonValueGetType(a_rCursor.m_hValue);
    if (enmType == RTJSONVALTYPE_STRING)
    {
        const char  *pszValue = RTJsonValueGetString(a_rCursor.m_hValue);
        const size_t cchValue = strlen(pszValue);
        int rc = setByString(pszValue, cchValue);
        if (RT_SUCCESS(rc))
            return rc;
        return a_rCursor.m_pPrimary->addError(a_rCursor, rc, "no memory for %zu char long string", cchValue);
    }

    m_strValue.setNull();
    if (enmType == RTJSONVALTYPE_NULL)
    {
        m_fNullIndicator = true;
        return VINF_SUCCESS;
    }

    return a_rCursor.m_pPrimary->addError(a_rCursor, VERR_REST_WRONG_JSON_TYPE_FOR_STRING, "wrong JSON type %s for string/enum",
                                          RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
}


int RTCRestStringEnumBase::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
    {
        if (m_iEnumValue > 0)
        {
            size_t              cEntries  = 0;
            ENUMMAPENTRY const *paEntries = getMappingTable(&cEntries);
            AssertReturn((unsigned)(m_iEnumValue - 1) < cEntries, VERR_REST_INTERNAL_ERROR_3);
            Assert(paEntries[m_iEnumValue - 1].iValue == m_iEnumValue);

            if (a_fFlags & kToString_Append)
                return a_pDst->appendNoThrow(paEntries[m_iEnumValue - 1].pszName, paEntries[m_iEnumValue - 1].cchName);
            return a_pDst->assignNoThrow(paEntries[m_iEnumValue - 1].pszName, paEntries[m_iEnumValue - 1].cchName);
        }
        if (a_fFlags & kToString_Append)
            return a_pDst->appendNoThrow(m_strValue);
        return a_pDst->assignNoThrow(m_strValue);
    }
    if (a_fFlags & kToString_Append)
        return a_pDst->appendNoThrow(RT_STR_TUPLE("null"));
    return a_pDst->assignNoThrow(RT_STR_TUPLE("null"));
}


int RTCRestStringEnumBase::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                                      uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    int iEnumValue = stringToEnum(a_rValue);
    if (iEnumValue > 0)
    {
        m_iEnumValue = iEnumValue;
        m_strValue.setNull();
        return VINF_SUCCESS;
    }

    /* No translation.  Check for null... */
    m_iEnumValue = 0;
    if (a_rValue.startsWithWord("null", RTCString::CaseInsensitive))
    {
        m_strValue.setNull();
        setNull();
        return VINF_SUCCESS;
    }

    /* Try copy the string. */
    int rc = m_strValue.assignNoThrow(a_rValue);
    if (RT_SUCCESS(rc))
        return VWRN_NOT_FOUND;

    RT_NOREF(a_pszName, a_pErrInfo, a_fFlags);
    return rc;
}


RTCRestObjectBase::kTypeClass RTCRestStringEnumBase::typeClass(void) const RT_NOEXCEPT
{
    return kTypeClass_StringEnum;
}


int RTCRestStringEnumBase::setByString(const char *a_pszValue, size_t a_cchValue /*= RTSTR_MAX*/) RT_NOEXCEPT
{
    if (a_cchValue == RTSTR_MAX)
        a_cchValue = strlen(a_pszValue);
    int iEnumValue = stringToEnum(a_pszValue, a_cchValue);
    if (iEnumValue > 0)
    {
        m_iEnumValue = iEnumValue;
        m_strValue.setNull();
        return VINF_SUCCESS;
    }

    /* No translation. */
    m_iEnumValue = 0;
    int rc = m_strValue.assignNoThrow(a_pszValue, a_cchValue);
    if (RT_SUCCESS(rc))
        return VWRN_NOT_FOUND;
    return rc;
}


int RTCRestStringEnumBase::setByString(RTCString const &a_rValue) RT_NOEXCEPT
{
    return setByString(a_rValue.c_str(), a_rValue.length());
}


const char *RTCRestStringEnumBase::getString() const RT_NOEXCEPT
{
    /* We ASSUME a certain mapping table layout here.  */
    if (m_iEnumValue > 0)
    {
        size_t              cEntries  = 0;
        ENUMMAPENTRY const *paEntries = getMappingTable(&cEntries);
        AssertReturn((unsigned)(m_iEnumValue - 1) < cEntries, "<internal-error-#1>");
        Assert(paEntries[m_iEnumValue - 1].iValue == m_iEnumValue);
        return paEntries[m_iEnumValue - 1].pszName;
    }

    AssertReturn(m_iEnumValue == 0, "<internal-error-#2>");
    if (m_strValue.isEmpty())
        return "invalid";

    return m_strValue.c_str();
}


int RTCRestStringEnumBase::stringToEnum(const char *a_pszValue, size_t a_cchValue) RT_NOEXCEPT
{
    if (a_cchValue == RTSTR_MAX)
        a_cchValue = strlen(a_pszValue);

    size_t              cEntries  = 0;
    ENUMMAPENTRY const *paEntries = getMappingTable(&cEntries);
    for (size_t i = 0; i < cEntries; i++)
        if (   paEntries[i].cchName == a_cchValue
            && memcmp(paEntries[i].pszName, a_pszValue, a_cchValue) == 0)
            return paEntries[i].iValue;
    return 0;
}


int RTCRestStringEnumBase::stringToEnum(RTCString const &a_rStrValue) RT_NOEXCEPT
{
    return stringToEnum(a_rStrValue.c_str(), a_rStrValue.length());
}


const char *RTCRestStringEnumBase::enumToString(int a_iEnumValue, size_t *a_pcchString) RT_NOEXCEPT
{
    /* We ASSUME a certain mapping table layout here.  */
    if (a_iEnumValue > 0)
    {
        size_t              cEntries  = 0;
        ENUMMAPENTRY const *paEntries = getMappingTable(&cEntries);
        if ((unsigned)(a_iEnumValue - 1) < cEntries)
        {
            Assert(paEntries[a_iEnumValue - 1].iValue == a_iEnumValue);
            if (a_pcchString)
                *a_pcchString = paEntries[a_iEnumValue - 1].cchName;
            return paEntries[a_iEnumValue - 1].pszName;
        }
    }
    /* Zero is the special invalid value. */
    else if (a_iEnumValue == 0)
    {
        if (a_pcchString)
            *a_pcchString = 7;
        return "invalid";
    }
    return NULL;
}


bool RTCRestStringEnumBase::setWorker(int a_iEnumValue) RT_NOEXCEPT
{
    /* We ASSUME a certain mapping table layout here.  */
    if (a_iEnumValue > 0)
    {
        size_t              cEntries  = 0;
        ENUMMAPENTRY const *paEntries = getMappingTable(&cEntries);
        AssertReturn((unsigned)(a_iEnumValue - 1) < cEntries, false);
        Assert(paEntries[a_iEnumValue - 1].iValue == a_iEnumValue);
        RT_NOREF(paEntries);
    }
    /* Zero is the special invalid value. */
    else if (a_iEnumValue != 0)
        AssertFailedReturn(false);

    m_iEnumValue = a_iEnumValue;
    m_strValue.setNull();
    return true;
}


RTCRestObjectBase  *RTCRestStringEnumBase::cloneWorker(RTCRestStringEnumBase *a_pDst) const RT_NOEXCEPT
{
    if (a_pDst)
    {
        int rc = a_pDst->assignCopy(*this);
        if (RT_SUCCESS(rc))
            return a_pDst;
        delete a_pDst;
    }
    return NULL;
}



/*********************************************************************************************************************************
*   RTCRestDataObject                                                                                                            *
*********************************************************************************************************************************/

RTCRestDataObject::RTCRestDataObject() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_fIsSet(0)
{
}


RTCRestDataObject::~RTCRestDataObject()
{
}


RTCRestDataObject::RTCRestDataObject(RTCRestDataObject const &a_rThat) RT_NOEXCEPT
    : RTCRestObjectBase(a_rThat)
    , m_fIsSet(a_rThat.m_fIsSet)
{
}


RTCRestDataObject &RTCRestDataObject::operator=(RTCRestDataObject const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_fIsSet = a_rThat.m_fIsSet;
    return *this;
}


int RTCRestDataObject::assignCopy(RTCRestDataObject const &a_rThat) RT_NOEXCEPT
{
    m_fNullIndicator = a_rThat.m_fNullIndicator;
    m_fIsSet = a_rThat.m_fIsSet;
    return VINF_SUCCESS;
}


int RTCRestDataObject::resetToDefault() RT_NOEXCEPT
{
    m_fNullIndicator = false;
    m_fIsSet = 0;
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestDataObject::serializeMembersAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    RT_NOREF(a_rDst);
    return a_rDst;
}


RTCRestOutputBase &RTCRestDataObject::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
    {
        uint32_t const uOldState = a_rDst.beginObject();
        serializeMembersAsJson(a_rDst);
        a_rDst.endObject(uOldState);
    }
    else
        a_rDst.nullValue();
    return a_rDst;
}


int RTCRestDataObject::deserializeMemberFromJson(RTCRestJsonCursor const &a_rCursor, size_t a_cchName) RT_NOEXCEPT
{
    RT_NOREF(a_rCursor, a_cchName);
    return VERR_NOT_FOUND;
}


int RTCRestDataObject::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    if (RTJsonValueGetType(a_rCursor.m_hValue) == RTJSONVALTYPE_NULL)
    {
        setNull();
        return VINF_SUCCESS;
    }

    /*
     * Make sure the object starts out with default values.
     */
    if (m_fIsSet == 0)
        m_fNullIndicator = false;
    else
        resetToDefault();

    /*
     * Iterate the object values.
     */
    RTJSONIT hIterator;
    int rcRet = RTJsonIteratorBeginObject(a_rCursor.m_hValue, &hIterator);
    if (RT_SUCCESS(rcRet))
    {
        for (;;)
        {
            RTCRestJsonCursor SubCursor(a_rCursor);
            int rc = RTJsonIteratorQueryValue(hIterator, &SubCursor.m_hValue, &SubCursor.m_pszName);
            if (RT_SUCCESS(rc))
            {
                size_t const cchName = strlen(SubCursor.m_pszName);

                rc = deserializeMemberFromJson(SubCursor, cchName);
                if (rc == VINF_SUCCESS)
                { /* likely */ }
                else if (rc == VERR_NOT_FOUND)
                {
                    rc = SubCursor.m_pPrimary->unknownField(SubCursor);
                    if (rcRet == VINF_SUCCESS)
                        rcRet = rc;
                }
                else if (RT_SUCCESS(rc))
                {
                    if (rcRet == VINF_SUCCESS)
                        rcRet = rc;
                }
                else if (RT_SUCCESS(rcRet))
                    rcRet = rc;
            }
            else
                rcRet = a_rCursor.m_pPrimary->addError(a_rCursor, rc, "RTJsonIteratorQueryValue failed: %Rrc", rc);

            /*
             * Advance.
             */
            rc = RTJsonIteratorNext(hIterator);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else if (rc == VERR_JSON_ITERATOR_END)
                break;
            else
            {
                rcRet = a_rCursor.m_pPrimary->addError(a_rCursor, rc, "RTJsonIteratorNext failed: %Rrc", rc);
                break;
            }
        }

        RTJsonIteratorFree(hIterator);
    }
    else if (   rcRet == VERR_JSON_VALUE_INVALID_TYPE
             && RTJsonValueGetType(a_rCursor.m_hValue) == RTJSONVALTYPE_NULL)
    {
        m_fNullIndicator = true;
        rcRet = VINF_SUCCESS;
    }
    else
        rcRet = a_rCursor.m_pPrimary->addError(a_rCursor, rcRet, "RTJsonIteratorBeginObject failed: %Rrc (type %s)",
                                               rcRet, RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
    return rcRet;
}


RTCRestObjectBase::kTypeClass RTCRestDataObject::typeClass(void) const RT_NOEXCEPT
{
    return kTypeClass_DataObject;
}



/*********************************************************************************************************************************
*   RTCRestPolyDataObject                                                                                                        *
*********************************************************************************************************************************/

RTCRestPolyDataObject::RTCRestPolyDataObject() RT_NOEXCEPT
    : RTCRestDataObject()
{
}


RTCRestPolyDataObject::RTCRestPolyDataObject(RTCRestPolyDataObject const &a_rThat) RT_NOEXCEPT
    : RTCRestDataObject(a_rThat)
{
}


RTCRestPolyDataObject::~RTCRestPolyDataObject()
{
}


int RTCRestPolyDataObject::resetToDefault() RT_NOEXCEPT
{
    return RTCRestDataObject::resetToDefault();
}


bool RTCRestPolyDataObject::isChild() const RT_NOEXCEPT
{
    return false;
}


RTCRestPolyDataObject &RTCRestPolyDataObject::operator=(RTCRestPolyDataObject const &a_rThat) RT_NOEXCEPT
{
    RTCRestDataObject::operator=(a_rThat);
    return *this;
}

