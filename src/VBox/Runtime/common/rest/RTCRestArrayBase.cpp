/* $Id: RTCRestArrayBase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestArrayBase implementation.
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
#include <iprt/cpp/restarray.h>

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/cpp/restoutput.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Separator characters. */
static char const g_szSep[RTCRestObjectBase::kCollectionFormat_Mask + 1] = ",, \t|,,";


/**
 * Default destructor.
 */
RTCRestArrayBase::RTCRestArrayBase() RT_NOEXCEPT
    : RTCRestObjectBase()
    , m_papElements(NULL)
    , m_cElements(0)
    , m_cCapacity(0)
{
}


#if 0 /* should not be used */
/**
 * Copy constructor.
 */
RTCRestArrayBase::RTCRestArrayBase(RTCRestArrayBase const &a_rThat);
#endif

/**
 * Destructor.
 */
RTCRestArrayBase::~RTCRestArrayBase()
{
    clear();

    if (m_papElements)
    {
        RTMemFree(m_papElements);
        m_papElements = NULL;
        m_cCapacity = 0;
    }
}


#if 0 /* should not be used */
/**
 * Copy assignment operator.
 */
RTCRestArrayBase &RTCRestArrayBase::operator=(RTCRestArrayBase const &a_rThat);
#endif


/*********************************************************************************************************************************
*   Overridden methods                                                                                                           *
*********************************************************************************************************************************/

RTCRestObjectBase *RTCRestArrayBase::baseClone() const RT_NOEXCEPT
{
    RTCRestArrayBase *pClone = createClone();
    if (pClone)
    {
        int rc = pClone->copyArrayWorkerNoThrow(*this);
        if (RT_SUCCESS(rc))
            return pClone;
        delete pClone;
    }
    return NULL;
}


int RTCRestArrayBase::resetToDefault() RT_NOEXCEPT
{
    /* The default state of an array is empty. At least for now. */
    clear();
    m_fNullIndicator = false;
    return VINF_SUCCESS;
}


RTCRestOutputBase &RTCRestArrayBase::serializeAsJson(RTCRestOutputBase &a_rDst) const RT_NOEXCEPT
{
    if (!m_fNullIndicator)
    {
        uint32_t const uOldState = a_rDst.beginArray();
        for (size_t i = 0; i < m_cElements; i++)
        {
            a_rDst.valueSeparator();
            m_papElements[i]->serializeAsJson(a_rDst);
        }
        a_rDst.endArray(uOldState);
    }
    else
        a_rDst.nullValue();
    return a_rDst;
}


int RTCRestArrayBase::deserializeFromJson(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    /*
     * Make sure the object starts out with an empty map.
     */
    if (m_cElements > 0)
        clear();
    m_fNullIndicator = false;

    /*
     * Iterate the array values.
     */
    RTJSONIT hIterator;
    int rcRet = RTJsonIteratorBeginArray(a_rCursor.m_hValue, &hIterator);
    if (RT_SUCCESS(rcRet))
    {
        for (size_t idxName = 0;; idxName++)
        {
            /* Setup sub-cursor. */
            RTCRestJsonCursor SubCursor(a_rCursor);
            int rc = RTJsonIteratorQueryValue(hIterator, &SubCursor.m_hValue, &SubCursor.m_pszName);
            if (RT_SUCCESS(rc))
            {
                char szName[32];
                RTStrPrintf(szName, sizeof(szName), "[%u]", idxName);
                SubCursor.m_pszName = szName;

                /* Call the static deserializeInstanceFromJson method of the value class.  */
                RTCRestObjectBase *pObj = NULL;
                rc = deserializeValueInstanceFromJson(SubCursor, &pObj);
                if (RT_SUCCESS(rc))
                    Assert(pObj);
                else if (RT_SUCCESS(rcRet))
                    rcRet = rc;
                if (pObj)
                {
                    rc = insertWorker(~(size_t)0, pObj, false /*a_fReplace*/);
                    if (RT_SUCCESS(rc))
                    { /* likely */ }
                    else
                    {
                        rcRet = a_rCursor.m_pPrimary->addError(a_rCursor, rc, "Array insert failed (index %zu): %Rrc",
                                                               idxName, rc);
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
        rcRet = a_rCursor.m_pPrimary->addError(a_rCursor, rcRet,
                                               "RTJsonIteratorBeginrray failed: %Rrc (type %s)",
                                               rcRet, RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
    return rcRet;

}


int RTCRestArrayBase::toString(RTCString *a_pDst, uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) const RT_NOEXCEPT
{
    int rc;
    if (!m_fNullIndicator)
    {
        if (m_cElements)
        {
            char const chSep = g_szSep[a_fFlags & kCollectionFormat_Mask];

            rc = m_papElements[0]->toString(a_pDst, a_fFlags);
            for (size_t i = 1; RT_SUCCESS(rc) && i < m_cElements; i++)
            {
                rc = a_pDst->appendNoThrow(chSep);
                if (RT_SUCCESS(rc))
                    rc = m_papElements[i]->toString(a_pDst, a_fFlags | kToString_Append);
            }
        }
        else
        {
            if (!(a_fFlags & kToString_Append))
                a_pDst->setNull();
            rc = VINF_SUCCESS;
        }
    }
    else if (a_fFlags & kToString_Append)
        rc = a_pDst->appendNoThrow(RT_STR_TUPLE("null"));
    else
        rc = a_pDst->appendNoThrow(RT_STR_TUPLE("null"));

    return rc;
}


int RTCRestArrayBase::fromString(RTCString const &a_rValue, const char *a_pszName, PRTERRINFO a_pErrInfo /*= NULL*/,
                                 uint32_t a_fFlags /*= kCollectionFormat_Unspecified*/) RT_NOEXCEPT
{
    /*
     * Clear the array.  If the string is empty, we have an empty array and is done.
     */
    if (!(a_fFlags & kToString_Append))
        clear();
    if (a_rValue.isEmpty())
        return VINF_SUCCESS;

    /*
     * Look for a separator so we don't mistake a initial null element for a null array.
     */
    char const chSep = g_szSep[a_fFlags & kCollectionFormat_Mask];
    size_t offSep = a_rValue.find(chSep);
    if (   offSep != RTCString::npos
        || !a_rValue.startsWithWord("null", RTCString::CaseInsensitive))
    {
        RTCString strTmp;
        size_t    offStart = 0;
        int       rcRet    = VINF_SUCCESS;
        for (;;)
        {
            /* Copy the element value into its own string buffer. */
            int rc = strTmp.assignNoThrow(a_rValue, offStart, (offSep == RTCString::npos ? a_rValue.length() : offSep) - offStart);
            AssertRCReturn(rc, rc);

            /* Create a new element, insert it and pass it the value string. */
            RTCRestObjectBase *pObj = createValue();
            AssertPtrReturn(pObj, VERR_NO_MEMORY);

            rc = insertWorker(~(size_t)0, pObj, false);
            AssertRCReturnStmt(rc, delete pObj, rc);

            char szName[128];
            RTStrPrintf(szName, sizeof(szName), "%.*s[%zu]", 116, a_pszName ? a_pszName : "", size());
            rc = pObj->fromString(strTmp, a_pszName, a_pErrInfo, 0);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else if (RT_SUCCESS(rcRet))
                rcRet = rc;

            /*
             * Done? Otherwise advance.
             */
            if (offSep == RTCString::npos)
                break;
            offStart = offSep + 1;
            offSep = a_rValue.find(chSep, offStart);
        }
        return rcRet;
    }

    /*
     * Consider this a null array even if it could also be an array with a single
     * null element.  This is just an artifact of an imperfect serialization format.
     */
    setNull();
    return VINF_SUCCESS;
}


RTCRestObjectBase::kTypeClass RTCRestArrayBase::typeClass(void) const RT_NOEXCEPT
{
    return kTypeClass_Array;
}


const char *RTCRestArrayBase::typeName(void) const RT_NOEXCEPT
{
    return "RTCRestArray<ElementType>";
}



/*********************************************************************************************************************************
*   Array methods                                                                                                                *
*********************************************************************************************************************************/

void RTCRestArrayBase::clear() RT_NOEXCEPT
{
    size_t i = m_cElements;
    while (i-- > 0)
    {
        delete m_papElements[i];
        m_papElements[i] = NULL;
    }
    m_cElements = 0;
    m_fNullIndicator = false;
}


bool RTCRestArrayBase::removeAt(size_t a_idx) RT_NOEXCEPT
{
    if (a_idx == ~(size_t)0)
        a_idx = m_cElements - 1;
    if (a_idx < m_cElements)
    {
        delete m_papElements[a_idx];
        m_papElements[a_idx] = NULL;

        m_cElements--;
        if (a_idx < m_cElements)
            memmove(&m_papElements[a_idx], &m_papElements[a_idx + 1], (m_cElements - a_idx) * sizeof(m_papElements[0]));
    }
    return false;
}


int RTCRestArrayBase::ensureCapacity(size_t a_cEnsureCapacity) RT_NOEXCEPT
{
    if (m_cCapacity < a_cEnsureCapacity)
    {
        if (a_cEnsureCapacity < 512)
            a_cEnsureCapacity = RT_ALIGN_Z(a_cEnsureCapacity, 16);
        else if (a_cEnsureCapacity < 16384)
            a_cEnsureCapacity = RT_ALIGN_Z(a_cEnsureCapacity, 128);
        else
            a_cEnsureCapacity = RT_ALIGN_Z(a_cEnsureCapacity, 512);

        void *pvNew = RTMemRealloc(m_papElements, sizeof(m_papElements[0]) * a_cEnsureCapacity);
        if (pvNew)
        {
            m_papElements = (RTCRestObjectBase **)pvNew;
            memset(&m_papElements[m_cCapacity], 0, (a_cEnsureCapacity - m_cCapacity) * sizeof(sizeof(m_papElements[0])));
            m_cCapacity   = a_cEnsureCapacity;
        }
        else
            return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}


int RTCRestArrayBase::copyArrayWorkerNoThrow(RTCRestArrayBase const &a_rThat) RT_NOEXCEPT
{
    int rc;
    clear();
    if (a_rThat.m_cElements == 0)
    {
        m_fNullIndicator = a_rThat.m_fNullIndicator;
        rc = VINF_SUCCESS;
    }
    else
    {
        Assert(!a_rThat.m_fNullIndicator);
        rc = ensureCapacity(a_rThat.m_cElements);
        if (RT_SUCCESS(rc))
        {
            for (size_t i = 0; i < a_rThat.m_cElements; i++)
            {
                AssertPtr(a_rThat.m_papElements[i]);
                rc = insertCopyWorker(i, *a_rThat.m_papElements[i], false);
                if (RT_SUCCESS(rc))
                { /* likely */ }
                else
                    return rc;
            }
        }
    }
    return rc;
}

void RTCRestArrayBase::copyArrayWorkerMayThrow(RTCRestArrayBase const &a_rThat)
{
    int rc = copyArrayWorkerNoThrow(a_rThat);
    if (RT_SUCCESS(rc))
        return;
    throw std::bad_alloc();
}


int RTCRestArrayBase::insertWorker(size_t a_idx, RTCRestObjectBase *a_pValue, bool a_fReplace) RT_NOEXCEPT
{
    AssertPtrReturn(a_pValue, VERR_INVALID_POINTER);

    if (a_idx == ~(size_t)0)
        a_idx = m_cElements;

    if (a_idx <= m_cElements)
    {
        if (a_idx == m_cElements || !a_fReplace)
        {
            /* Make sure we've got array space. */
            if (m_cElements + 1 < m_cCapacity)
            { /* kind of likely */ }
            else
            {
                int rc = ensureCapacity(m_cElements + 1);
                if (RT_SUCCESS(rc))
                { /* likely */ }
                else
                    return rc;
            }

            /* Shift following elements before inserting. */
            if (a_idx < m_cElements)
                memmove(&m_papElements[a_idx + 1], &m_papElements[a_idx], (m_cElements - a_idx) * sizeof(m_papElements[0]));
            m_papElements[a_idx] = a_pValue;
            m_cElements++;
#ifdef RT_STRICT
            for (size_t i = 0; i < m_cElements; i++)
                AssertPtr(m_papElements[i]);
#endif
            m_fNullIndicator = false;
            return VINF_SUCCESS;
        }

        /* Replace element. */
        delete m_papElements[a_idx];
        m_papElements[a_idx] = a_pValue;
        m_fNullIndicator = false;
        return VWRN_ALREADY_EXISTS;
    }
    return VERR_OUT_OF_RANGE;
}


int RTCRestArrayBase::insertCopyWorker(size_t a_idx, RTCRestObjectBase const &a_rValue, bool a_fReplace) RT_NOEXCEPT
{
    int rc;
    RTCRestObjectBase *pValueCopy = a_rValue.baseClone();
    if (pValueCopy)
    {
        rc = insertWorker(a_idx, pValueCopy, a_fReplace);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            delete pValueCopy;
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}

