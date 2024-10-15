/* $Id: RTCRestOutputPrettyBase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestOutputPrettyBase implementation.
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


RTCRestOutputPrettyBase::RTCRestOutputPrettyBase() RT_NOEXCEPT
    : RTCRestOutputBase()
{
}


RTCRestOutputPrettyBase::~RTCRestOutputPrettyBase()
{
}


uint32_t RTCRestOutputPrettyBase::beginArray() RT_NOEXCEPT
{
    output(RT_STR_TUPLE("["));
    uint32_t const uOldState = m_uState;
    m_uState = (uOldState & 0xffff) + 1;
    return uOldState;
}


void RTCRestOutputPrettyBase::endArray(uint32_t a_uOldState) RT_NOEXCEPT
{
    m_uState = a_uOldState;
    output(RT_STR_TUPLE("\n"));
    outputIndentation();
    output(RT_STR_TUPLE("]"));
}


uint32_t RTCRestOutputPrettyBase::beginObject() RT_NOEXCEPT
{
    output(RT_STR_TUPLE("{"));
    uint32_t const uOldState = m_uState;
    m_uState = (uOldState & 0xffff) + 1;
    return uOldState;
}


void RTCRestOutputPrettyBase::endObject(uint32_t a_uOldState) RT_NOEXCEPT
{
    m_uState = a_uOldState;
    output(RT_STR_TUPLE("\n"));
    outputIndentation();
    output(RT_STR_TUPLE("}"));
}


void RTCRestOutputPrettyBase::valueSeparator() RT_NOEXCEPT
{
    if (m_uState & RT_BIT_32(31))
        output(RT_STR_TUPLE(",\n"));
    else
    {
        m_uState |= RT_BIT_32(31);
        output(RT_STR_TUPLE("\n"));
    }
    outputIndentation();
}


void RTCRestOutputPrettyBase::valueSeparatorAndName(const char *a_pszName, size_t a_cchName) RT_NOEXCEPT
{
    RT_NOREF(a_cchName);
    if (m_uState & RT_BIT_32(31))
        output(RT_STR_TUPLE(",\n"));
    else
    {
        m_uState |= RT_BIT_32(31);
        output(RT_STR_TUPLE("\n"));
    }
    outputIndentation();
    printf("%RMjs: ", a_pszName);
}


void RTCRestOutputPrettyBase::outputIndentation() RT_NOEXCEPT
{
    static char const s_szSpaces[] = "                                                                                         ";
    size_t cchIndent = (m_uState & 0xffff) << 1;
    while (cchIndent > 0)
    {
        size_t cbToWrite = RT_MIN(cchIndent, sizeof(s_szSpaces) - 1);
        output(s_szSpaces, cbToWrite);
        cchIndent -= cbToWrite;
    }
}

