/* $Id: RTCRestOutputBase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestOutputBase implementation.
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


RTCRestOutputBase::RTCRestOutputBase() RT_NOEXCEPT
    : m_uState(0)
{
}


RTCRestOutputBase::~RTCRestOutputBase()
{
}


size_t RTCRestOutputBase::vprintf(const char *pszFormat, va_list va) RT_NOEXCEPT
{
    return RTStrFormatV(printfOutputCallback, this, NULL, NULL, pszFormat, va);
}


/*static*/ DECLCALLBACK(size_t) RTCRestOutputBase::printfOutputCallback(void *pvArg, const char *pachChars, size_t cbChars) RT_NOEXCEPT
{
    return ((RTCRestOutputBase *)pvArg)->output(pachChars, cbChars);
}


uint32_t RTCRestOutputBase::beginArray() RT_NOEXCEPT
{
    output(RT_STR_TUPLE("["));
    uint32_t const uOldState = m_uState;
    m_uState = (uOldState & 0xffff) + 1;
    return uOldState;
}


void RTCRestOutputBase::endArray(uint32_t a_uOldState) RT_NOEXCEPT
{
    m_uState = a_uOldState;
    output(RT_STR_TUPLE("]"));
}


uint32_t RTCRestOutputBase::beginObject() RT_NOEXCEPT
{
    output(RT_STR_TUPLE("{"));
    uint32_t const uOldState = m_uState;
    m_uState = (uOldState & 0xffff) + 1;
    return uOldState;
}


void RTCRestOutputBase::endObject(uint32_t a_uOldState) RT_NOEXCEPT
{
    m_uState = a_uOldState;
    output(RT_STR_TUPLE("}"));
}


void RTCRestOutputBase::valueSeparator() RT_NOEXCEPT
{
    if (m_uState & RT_BIT_32(31))
        output(RT_STR_TUPLE(","));
    else
        m_uState |= RT_BIT_32(31);
}


void RTCRestOutputBase::valueSeparatorAndName(const char *a_pszName, size_t a_cchName) RT_NOEXCEPT
{
    RT_NOREF(a_cchName);
    if (m_uState & RT_BIT_32(31))
        printf(",%RMjs:", a_pszName);
    else
    {
        m_uState |= RT_BIT_32(31);
        printf("%RMjs:", a_pszName);
    }
}


void RTCRestOutputBase::nullValue() RT_NOEXCEPT
{
    output(RT_STR_TUPLE("null"));
}

