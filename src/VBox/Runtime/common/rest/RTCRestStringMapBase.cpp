/* $Id: RTCRestStringMapBase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestStringMapBase implementation.
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
#include <iprt/cpp/reststringmap.h>

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/cpp/restoutput.h>


/**
 * Default destructor.
 */
RTCRestStringMapBase::RTCRestStringMapBase() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_Map(NULL)
    , m_cEntries(0)
{
    RTListInit(&m_ListHead);
}


#if 0 /* trigger link error for now. */
/** Copy constructor. */
RTCRestStringMapBase::RTCRestStringMapBase(RTCRestStringMapBase const &a_rThat);
#endif


/**
 * Destructor.
 */
RTCRestStringMapBase::~RTCRestStringMapBase()
{
    clear();
}



#if 0 /* trigger link error for now. */
/** Copy assignment operator. */
RTCRestStringMapBase &RTCRestStringMapBase::operator=(RTCRestStringMapBase const &a_rThat);
#endif


/*********************************************************************************************************************************
*   Overridden base object methods                                                                                               *
*********************************************************************************************************************************/

RTCRestObjectBase *RTCRestStringMapBase::baseClone() const RT_NOEXCEPT
{
    RTCRestStringMapBase *pClone = createClone();
    if (pClone)
    {
        int rc = pClone->copyMapWorkerNoThrow(*this);
        if (RT_SUCCESS(rc))
            return pClone;
        delete pClone;
    }
    return NULL;
}


int RTCRestStringMapBase::resetToDefault() RT_NOEXCEPT
{
    /* Default is an empty map. */
    clear();
    m_fNullIndicator = false;
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestStringMapBase::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
    {
        uint32_t const uOldState = a_rDst.beginObject();
        MapEntry const *pCur;
        RTListForEachCpp(&m_ListHead, pCur, MapEntry, ListEntry)
        {
            a_rDst.valueSeparatorAndName(pCur->strKey.c_str(), pCur->strKey.length());
            pCur->pValue->serializeAsJson(a_rDst);
        }
        a_rDst.endObject(uOldState);
    }
    else
        a_rDst.nullValue();
    return a_rDst;
}


int RTCRestStringMapBase::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    if (RTJsonValueGetType(a_rCursor.m_hValue) == RTJSONVALTYPE_NULL)
    {
        setNull();
        return VINF_SUCCESS;
    }

    /*
     * Make sure the object starts out with an empty map.
     */
    if (m_cEntries > 0)
        clear();
    m_fNullIndicator = false;

    /*
     * Iterate the object values.
     */
    RTJSONIT hIterator;
    int rcRet = RTJsonIteratorBeginObject(a_rCursor.m_hValue, &hIterator);
    if (RT_SUCCESS(rcRet))
    {
        for (;;)
        {
            /* Set up the sub-cursor. */
            RTCRestJsonCursor SubCursor(a_rCursor);
            int rc = RTJsonIteratorQueryValue(hIterator, &SubCursor.m_hValue, &SubCursor.m_pszName);
            if (RT_SUCCESS(rc))
            {
                /* Call the static deserializeInstanceFromJson method of the value class.  */
                RTCRestObjectBase *pObj = NULL;
                rc = deserializeValueInstanceFromJson(SubCursor, &pObj);
                if (RT_SUCCESS(rc))
                    Assert(pObj);
                else if (RT_SUCCESS(rcRet))
                    rcRet = rc;
                if (pObj)
                {
                    /* Insert the value. */
                    rc = putWorker(SubCursor.m_pszName, pObj, true /*a_fReplace*/);
                    if (rc == VINF_SUCCESS)
                    { /* likely */ }
                    else if (RT_SUCCESS(rc))
                    {
                        a_rCursor.m_pPrimary->addError(a_rCursor, rc, "warning %Rrc inserting '%s' into map",
                                                       rc, SubCursor.m_pszName);
                        if (rcRet == VINF_SUCCESS)
                            rcRet = rc;
                    }
                    else
                    {
                        rcRet = a_rCursor.m_pPrimary->addError(a_rCursor, rc, "Failed to insert '%s' into map: %Rrc",
                                                               SubCursor.m_pszName, rc);
                        delete pObj;
                    }
                }
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
    else if (rcRet == VERR_JSON_IS_EMPTY)
        rcRet = VINF_SUCCESS;
    else if (   rcRet == VERR_JSON_VALUE_INVALID_TYPE
             && RTJsonValueGetType(a_rCursor.m_hValue) == RTJSONVALTYPE_NULL)
    {
        m_fNullIndicator = true;
        rcRet = VINF_SUCCESS;
    }
    else
        rcRet = a_rCursor.m_pPrimary->addError(a_rCursor, rcRet, "RTJsonIteratorBegin failed: %Rrc (type %s)",
                                               rcRet, RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
    return rcRet;
}

// later?
//    virtual int RTCRestStringMapBase::toString(RTCString *a_pDst, uint32_t a_fFlags = kCollectionFormat_Unspecified) const ;
//    virtual int RTCRestStringMapBase::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo = NULL,
//                           uint32_t a_fFlags = kCollectionFormat_Unspecified) ;
//


RTCRestObjectBase::kTypeClass RTCRestStringMapBase::typeClass(void) const RT_NOEXCEPT
{
    return kTypeClass_StringMap;
}


const char *RTCRestStringMapBase::typeName(void) const RT_NOEXCEPT
{
    return "RTCRestStringMap<ValueType>";
}


/*********************************************************************************************************************************
*   Generic map methods                                                                                                          *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK}
 */
/*static*/ DECLCALLBACK(int) RTCRestStringMapBase::stringSpaceDestructorCallback(PRTSTRSPACECORE pStr, void *pvUser) RT_NOEXCEPT
{
    MapEntry *pNode = (MapEntry *)pStr;
    if (pNode->pValue)
    {
        delete pNode->pValue;
        pNode->pValue = NULL;
    }
    pNode->strKey.setNull();
    delete pNode;

    RT_NOREF(pvUser);
    return VINF_SUCCESS;
}


void RTCRestStringMapBase::clear() RT_NOEXCEPT
{
    RTStrSpaceDestroy(&m_Map, stringSpaceDestructorCallback, NULL);
    RTListInit(&m_ListHead);
    m_cEntries = 0;
    m_fNullIndicator = false;
}


size_t RTCRestStringMapBase::size() const RT_NOEXCEPT
{
    return m_cEntries;
}


bool RTCRestStringMapBase::containsKey(const char *a_pszKey) const RT_NOEXCEPT
{
    if (isNull())
        return false;

    return RTStrSpaceGet((PRTSTRSPACE)&m_Map, a_pszKey) != NULL;
}


bool RTCRestStringMapBase::containsKey(RTCString const &a_rStrKey) const RT_NOEXCEPT
{
    return containsKey(a_rStrKey.c_str());
}


bool RTCRestStringMapBase::remove(const char *a_pszKey) RT_NOEXCEPT
{
    if (isNull())
        return false;

    MapEntry *pRemoved = (MapEntry *)RTStrSpaceRemove(&m_Map, a_pszKey);
    if (pRemoved)
    {
        m_cEntries--;
        RTListNodeRemove(&pRemoved->ListEntry);
        stringSpaceDestructorCallback(&pRemoved->Core, NULL);
        return true;
    }
    return false;
}


bool RTCRestStringMapBase::remove(RTCString const &a_rStrKey) RT_NOEXCEPT
{
    return remove(a_rStrKey.c_str());
}


int RTCRestStringMapBase::putNewValue(RTCRestObjectBase **a_ppValue, const char *a_pszKey, size_t a_cchKey /*= RTSTR_MAX*/,
                                      bool a_fReplace /*= false*/) RT_NOEXCEPT
{
    RTCRestObjectBase *pValue = createValue();
    if (pValue)
    {
        int rc = putWorker(a_pszKey, pValue, a_fReplace, a_cchKey);
        if (RT_SUCCESS(rc))
            *a_ppValue = pValue;
        else
        {
            delete pValue;
            *a_ppValue = NULL;
        }
        return rc;
    }
    *a_ppValue = NULL;
    return VERR_NO_MEMORY;
}


int RTCRestStringMapBase::putNewValue(RTCRestObjectBase **a_ppValue, RTCString const &a_rStrKey, bool a_fReplace /*= false*/) RT_NOEXCEPT
{
    return putNewValue(a_ppValue, a_rStrKey.c_str(), a_rStrKey.length(), a_fReplace);
}


/*********************************************************************************************************************************
*   Protected methods                                                                                                            *
*********************************************************************************************************************************/

int RTCRestStringMapBase::copyMapWorkerNoThrow(RTCRestStringMapBase const &a_rThat) RT_NOEXCEPT
{
    Assert(this != &a_rThat);
    clear();
    m_fNullIndicator = a_rThat.m_fNullIndicator;

    if (!a_rThat.m_fNullIndicator)
    {
        MapEntry const *pCur;
        RTListForEachCpp(&a_rThat.m_ListHead, pCur, MapEntry, ListEntry)
        {
            int rc = putCopyWorker(pCur->strKey.c_str(), *pCur->pValue, true /*a_fReplace*/);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
                return rc;
        }
    }

    return VINF_SUCCESS;
}


void RTCRestStringMapBase::copyMapWorkerMayThrow(RTCRestStringMapBase const &a_rThat)
{
    int rc = copyMapWorkerNoThrow(a_rThat);
    if (RT_SUCCESS(rc))
        return;
    throw std::bad_alloc();
}


int RTCRestStringMapBase::putWorker(const char *a_pszKey, RTCRestObjectBase *a_pValue, bool a_fReplace,
                                    size_t a_cchKey /*= RTSTR_MAX*/) RT_NOEXCEPT
{
    int rc;
    MapEntry *pEntry =  new (std::nothrow) MapEntry;
    if (pEntry)
    {
        rc = pEntry->strKey.assignNoThrow(a_pszKey, a_cchKey);
        if (RT_SUCCESS(rc))
        {
            pEntry->Core.pszString = pEntry->strKey.c_str();
            pEntry->Core.cchString = pEntry->strKey.length();
            pEntry->pValue = a_pValue;
            if (RTStrSpaceInsert(&m_Map, &pEntry->Core))
            {
                RTListAppend(&m_ListHead, &pEntry->ListEntry);
                m_cEntries++;
                m_fNullIndicator = false;
                return VINF_SUCCESS;
            }

            Assert(!m_fNullIndicator);
            if (!a_fReplace)
                rc = VERR_ALREADY_EXISTS;
            else
            {
                /* Just replace the pValue in the existing entry. */
                MapEntry *pCollision = (MapEntry *)RTStrSpaceGet(&m_Map, a_pszKey);
                if (pCollision)
                {
                    if (pCollision->pValue)
                        delete pCollision->pValue;
                    pCollision->pValue = a_pValue;
                    pEntry->pValue = NULL; /* paranoia */
                    rc = VWRN_ALREADY_EXISTS;
                }
                else
                    rc = VERR_INTERNAL_ERROR;
            }
        }
        delete pEntry;
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


int RTCRestStringMapBase::putCopyWorker(const char *a_pszKey, RTCRestObjectBase const &a_rValue, bool a_fReplace,
                                        size_t a_cchKey /*= RTSTR_MAX*/) RT_NOEXCEPT
{
    int rc;
    RTCRestObjectBase *pValueCopy = a_rValue.baseClone();
    if (pValueCopy)
    {
        rc = putWorker(a_pszKey, pValueCopy, a_fReplace, a_cchKey);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            delete pValueCopy;
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


RTCRestObjectBase *RTCRestStringMapBase::getWorker(const char *a_pszKey) RT_NOEXCEPT
{
    if (isNull())
        return NULL;

    MapEntry *pHit = (MapEntry *)RTStrSpaceGet(&m_Map, a_pszKey);
    if (pHit)
        return pHit->pValue;
    return NULL;
}


RTCRestObjectBase const *RTCRestStringMapBase::getWorker(const char *a_pszKey) const RT_NOEXCEPT
{
    if (isNull())
        return NULL;

    MapEntry const *pHit = (MapEntry const *)RTStrSpaceGet((PRTSTRSPACE)&m_Map, a_pszKey);
    if (pHit)
        return pHit->pValue;
    return NULL;
}

