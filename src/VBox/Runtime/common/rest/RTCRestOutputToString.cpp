/* $Id: RTCRestOutputToString.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestOutputToString implementation.
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
#include <iprt/cpp/restoutput.h>

#include <iprt/errcore.h>
#include <iprt/string.h>


RTCRestOutputToString::RTCRestOutputToString(RTCString *a_pDst, bool a_fAppend /*= false*/) RT_NOEXCEPT
    : RTCRestOutputBase()
    , m_pDst(a_pDst)
    , m_fOutOfMemory(false)
{
    if (!a_fAppend)
        m_pDst->setNull();
}


RTCRestOutputToString::~RTCRestOutputToString()
{
    /* We don't own the string, so we don't delete it! */
    m_pDst = NULL;
}


size_t RTCRestOutputToString::output(const char *a_pchString, size_t a_cchToWrite) RT_NOEXCEPT
{
    if (a_cchToWrite)
    {
        RTCString *pDst = m_pDst;
        if (pDst && !m_fOutOfMemory)
        {
            /*
             * Make sure we've got sufficient space available before we append.
             */
            size_t cchCurrent = pDst->length();
            size_t cbCapacity = pDst->capacity();
            size_t cbNeeded   = cchCurrent + a_cchToWrite + 1;
            if (cbNeeded <= cbCapacity)
            { /* likely */ }
            else
            {
                /* Grow it. */
                if (cbNeeded < _16M)
                {
                    if (cbCapacity <= _1K)
                        cbCapacity = _1K;
                    else
                        cbCapacity = RT_ALIGN_Z(cbCapacity, _1K);
                    while (cbCapacity < cbNeeded)
                        cbCapacity <<= 1;
                }
                else
                {
                    cbCapacity = RT_ALIGN_Z(cbCapacity, _2M);
                    while (cbCapacity < cbNeeded)
                        cbCapacity += _2M;
                }
                int rc = pDst->reserveNoThrow(cbCapacity);
                if (RT_SUCCESS(rc))
                {
                    rc = pDst->reserveNoThrow(cbNeeded);
                    if (RT_FAILURE(rc))
                    {
                        m_fOutOfMemory = true;
                        return a_cchToWrite;
                    }
                }
            }

            /*
             * Do the appending.
             */
            pDst->append(a_pchString, a_cchToWrite);
        }
    }
    return a_cchToWrite;
}


RTCString *RTCRestOutputToString::finalize() RT_NOEXCEPT
{
    RTCString *pRet;
    if (!m_fOutOfMemory)
        pRet = m_pDst;
    else
        pRet = NULL;
    m_pDst = NULL;
    return pRet;
}

