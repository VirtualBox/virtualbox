/* $Id: errinfo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Error Info, Setters.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/errcore.h>

#include <iprt/assert.h>
#include <iprt/string.h>


RTDECL(int) RTErrInfoSet(PRTERRINFO pErrInfo, int rc, const char *pszMsg)
{
    if (pErrInfo)
    {
        AssertPtr(pErrInfo);
        Assert((pErrInfo->fFlags & RTERRINFO_FLAGS_MAGIC_MASK) == RTERRINFO_FLAGS_MAGIC);

        RTStrCopy(pErrInfo->pszMsg, pErrInfo->cbMsg, pszMsg);
        pErrInfo->rc      = rc;
        pErrInfo->fFlags |= RTERRINFO_FLAGS_SET;
    }
    return rc;
}


RTDECL(int) RTErrInfoSetF(PRTERRINFO pErrInfo, int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTErrInfoSetV(pErrInfo, rc, pszFormat, va);
    va_end(va);
    return rc;
}


RTDECL(int) RTErrInfoSetV(PRTERRINFO pErrInfo, int rc, const char *pszFormat, va_list va)
{
    if (pErrInfo)
    {
        AssertPtr(pErrInfo);
        Assert((pErrInfo->fFlags & RTERRINFO_FLAGS_MAGIC_MASK) == RTERRINFO_FLAGS_MAGIC);

        RTStrPrintfV(pErrInfo->pszMsg, pErrInfo->cbMsg, pszFormat, va);
        pErrInfo->rc      = rc;
        pErrInfo->fFlags |= RTERRINFO_FLAGS_SET;
    }
    return rc;
}


RTDECL(int) RTErrInfoAdd(PRTERRINFO pErrInfo, int rc, const char *pszMsg)
{
    if (pErrInfo)
    {
        AssertPtr(pErrInfo);
        if (pErrInfo->fFlags & RTERRINFO_FLAGS_SET)
            RTStrCat(pErrInfo->pszMsg, pErrInfo->cbMsg, pszMsg);
        else
        {
            while (*pszMsg == ' ')
                pszMsg++;
            return RTErrInfoSet(pErrInfo, rc, pszMsg);
        }
    }
    return rc;
}


RTDECL(int) RTErrInfoAddF(PRTERRINFO pErrInfo, int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTErrInfoAddV(pErrInfo, rc, pszFormat, va);
    va_end(va);
    return rc;
}


RTDECL(int) RTErrInfoAddV(PRTERRINFO pErrInfo, int rc, const char *pszFormat, va_list va)
{
    if (pErrInfo)
    {
        AssertPtr(pErrInfo);
        Assert((pErrInfo->fFlags & RTERRINFO_FLAGS_MAGIC_MASK) == RTERRINFO_FLAGS_MAGIC);
        if (pErrInfo->fFlags & RTERRINFO_FLAGS_SET)
        {
            char *pszOut = (char *)memchr(pErrInfo->pszMsg, '\0', pErrInfo->cbMsg - 2);
            if (pszOut)
                RTStrPrintfV(pszOut, &pErrInfo->pszMsg[pErrInfo->cbMsg] - pszOut, pszFormat, va);
        }
        else
        {
            while (*pszFormat == ' ')
                pszFormat++;
            return RTErrInfoSetV(pErrInfo, rc, pszFormat, va);
        }
    }
    return rc;
}

