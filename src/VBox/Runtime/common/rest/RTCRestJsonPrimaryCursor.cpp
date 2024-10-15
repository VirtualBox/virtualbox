/* $Id: RTCRestJsonPrimaryCursor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - C++ REST, RTCRestJsonPrimaryCursor implementation.
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

#include <iprt/errcore.h>
#include <iprt/string.h>


char *RTCRestJsonPrimaryCursor::getPath(RTCRestJsonCursor const &a_rCursor, char *pszDst, size_t cbDst) const RT_NOEXCEPT
{
    AssertReturn(cbDst > 0, NULL);

    /*
     * To avoid using recursion, we need to first do a pass to figure sizes
     * and depth.  To keep things simple, with the exception of the top name
     * we only copy out full names.
     */
    /* Special case: Insufficient space for the top name. */
    size_t const cchTopName = strlen(a_rCursor.m_pszName);
    if (cchTopName >= cbDst)
    {
        memcpy(pszDst, a_rCursor.m_pszName, cbDst - 1);
        pszDst[cbDst - 1] = '\0';
    }
    else
    {
        /* Determin how deep we should go and the resulting length. */
        size_t iMaxDepth = 0;
        size_t cchTotal  = cchTopName;
        for (RTCRestJsonCursor const *pCur = a_rCursor.m_pParent; pCur; pCur = pCur->m_pParent)
        {
            size_t const cchName = strlen(pCur->m_pszName);
            size_t cchNewTotal = cchName + 1 + cchTotal;
            if (cchNewTotal < cbDst)
                cchTotal = cchNewTotal;
            else
                break;
            iMaxDepth++;
        }

        /* Produce the string, in reverse. */
        char *psz = &pszDst[cchTotal];
        *psz = '\0';
        psz -= cchTopName;
        memcpy(psz, a_rCursor.m_pszName, cchTopName);
        for (RTCRestJsonCursor const *pCur = a_rCursor.m_pParent; pCur && iMaxDepth > 0; pCur = pCur->m_pParent)
        {
            psz -= 1;
            *psz = '.';

            size_t const cchName = strlen(pCur->m_pszName);
            psz -= cchName;
            memcpy(psz, pCur->m_pszName, cchName);

            iMaxDepth--;
        }
        Assert(psz == pszDst);
    }
    return pszDst;
}


int RTCRestJsonPrimaryCursor::addError(RTCRestJsonCursor const &a_rCursor, int a_rc, const char *a_pszFormat, ...) RT_NOEXCEPT
{
    va_list va;
    va_start(va, a_pszFormat);
    char szPath[128];
    a_rc = RTErrInfoAddF(m_pErrInfo, a_rc, "%s: %N\n", getPath(a_rCursor, szPath, sizeof(szPath)), a_pszFormat, &va);
    va_end(va);
    return a_rc;
}


int RTCRestJsonPrimaryCursor::unknownField(RTCRestJsonCursor const &a_rCursor) RT_NOEXCEPT
{
    char szPath[128];
    return RTErrInfoAddF(m_pErrInfo, VWRN_NOT_FOUND, "%s: unknown field (type %s)\n",
                         getPath(a_rCursor, szPath, sizeof(szPath)),
                         RTJsonValueTypeName(RTJsonValueGetType(a_rCursor.m_hValue)));
}

