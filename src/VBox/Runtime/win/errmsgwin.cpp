/* $Id: errmsgwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Status code messages, Windows.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <iprt/win/windows.h>

#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/string.h>

#include <iprt/bldprog-strtab.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(IPRT_NO_ERROR_DATA) || defined(IPRT_NO_WIN_ERROR_DATA)
/* Cook data just for VINF_SUCCESS so that code below compiles fine. */
static const char            g_achWinStrTabData[] = { "ERROR_SUCCESS" };
static const RTBLDPROGSTRTAB g_WinMsgStrTab = { g_achWinStrTabData, sizeof(g_achWinStrTabData) - 1, 0, NULL };
static const struct
{
    int16_t     iCode;
    uint8_t     offDefine;
    uint8_t     cchDefine;
    uint8_t     offMsgFull;
    uint8_t     cchMsgFull;
} g_aWinMsgs[] =
{
    { 0, 0, 13, 0, 13, },
};
#else
# include "errmsgwindata-only-defines.h"
#endif


/**
 * Looks up the message table entry for @a rc.
 *
 * @returns index into g_aWinMsgs on success, ~(size_t)0 if not found.
 * @param   rc      The status code to locate the entry for.
 */
static size_t rtErrWinLookup(long rc)
{
    /*
     * Perform binary search (duplicate code in rtErrLookup).
     */
    size_t iStart = 0;
    size_t iEnd   = RT_ELEMENTS(g_aWinMsgs);
    for (;;)
    {
        size_t i = iStart + (iEnd - iStart) / 2;
        long const iCode = g_aWinMsgs[i].iCode;
        if (rc < iCode)
        {
            if (iStart < i)
                iEnd = i;
            else
                break;
        }
        else if (rc > iCode)
        {
            i++;
            if (i < iEnd)
                iStart = i;
            else
                break;
        }
        else
            return i;
    }

#ifdef RT_STRICT
    for (size_t i = 0; i < RT_ELEMENTS(g_aWinMsgs); i++)
        Assert(g_aWinMsgs[i].iCode != rc);
#endif

    return ~(size_t)0;
}


RTDECL(bool)    RTErrWinIsKnown(long rc)
{
    if (rtErrWinLookup(rc) != ~(size_t)0)
        return true;
    if (SCODE_FACILITY(rc) == FACILITY_WIN32)
    {
        if (rtErrWinLookup(HRESULT_CODE(rc)) != ~(size_t)0)
            return true;
    }
    return false;
}


RTDECL(ssize_t) RTErrWinQueryDefine(long rc, char *pszBuf, size_t cbBuf, bool fFailIfUnknown)
{
    size_t idx = rtErrWinLookup(rc);
    if (idx != ~(size_t)0)
        return RTBldProgStrTabQueryString(&g_WinMsgStrTab,
                                          g_aWinMsgs[idx].offDefine, g_aWinMsgs[idx].cchDefine,
                                          pszBuf, cbBuf);

    /*
     * If FACILITY_WIN32 kind of status, look up the win32 code.
     */
    if (   SCODE_FACILITY(rc) == FACILITY_WIN32
        && (idx = rtErrWinLookup(HRESULT_CODE(rc))) != ~(size_t)0)
    {
        /* Append the incoming rc, so we know it's not a regular WIN32 status: */
        ssize_t cchRet = RTBldProgStrTabQueryString(&g_WinMsgStrTab,
                                                    g_aWinMsgs[idx].offDefine, g_aWinMsgs[idx].cchDefine,
                                                    pszBuf, cbBuf);
        if (cchRet > 0)
        {
            pszBuf[cchRet++] = '/';
            return RTStrFormatU32(pszBuf + cchRet, cbBuf - cchRet, rc, 16, 0, 0, RTSTR_F_SPECIAL);
        }
        return VERR_BUFFER_OVERFLOW;
    }

    if (fFailIfUnknown)
        return VERR_NOT_FOUND;
    return RTStrFormatU32(pszBuf, cbBuf, rc, 16, 0, 0, RTSTR_F_SPECIAL);
}


RTDECL(size_t)  RTErrWinFormatDefine(long rc, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, char *pszTmp, size_t cbTmp)
{
    RT_NOREF(pszTmp, cbTmp);
    size_t idx = rtErrWinLookup(rc);
    if (idx != ~(size_t)0)
        return RTBldProgStrTabQueryOutput(&g_WinMsgStrTab,
                                          g_aWinMsgs[idx].offDefine, g_aWinMsgs[idx].cchDefine,
                                          pfnOutput, pvArgOutput);

    /*
     * If FACILITY_WIN32 kind of status, look up the win32 code.
     */
    size_t cchRet = 0;
    if (   SCODE_FACILITY(rc) == FACILITY_WIN32
        && (idx = rtErrWinLookup(HRESULT_CODE(rc))) != ~(size_t)0)
    {
        /* Append the incoming rc, so we know it's not a regular WIN32 status: */
        cchRet  = RTBldProgStrTabQueryOutput(&g_WinMsgStrTab,
                                             g_aWinMsgs[idx].offDefine, g_aWinMsgs[idx].cchDefine,
                                             pfnOutput, pvArgOutput);
        cchRet += pfnOutput(pvArgOutput, RT_STR_TUPLE("/"));
    }

    ssize_t cchValue = RTStrFormatU32(pszTmp, cbTmp, rc, 16, 0, 0, RTSTR_F_SPECIAL);
    Assert(cchValue > 0);
    cchRet += pfnOutput(pvArgOutput, pszTmp, cchValue);
    return cchRet;
}


RTDECL(size_t)  RTErrWinFormatMsg(long rc, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, char *pszTmp, size_t cbTmp)
{
    return RTErrWinFormatDefine(rc, pfnOutput, pvArgOutput, pszTmp, cbTmp);
}


RTDECL(size_t)  RTErrWinFormatMsgAll(long rc, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, char *pszTmp, size_t cbTmp)
{
    RT_NOREF(pszTmp, cbTmp);
    size_t cchRet;
    size_t idx = rtErrWinLookup(rc);
    if (   idx != ~(size_t)0
        || (   SCODE_FACILITY(rc) == FACILITY_WIN32
            && (idx = rtErrWinLookup(HRESULT_CODE(rc))) != ~(size_t)0))
    {
        cchRet  = RTBldProgStrTabQueryOutput(&g_WinMsgStrTab,
                                             g_aWinMsgs[idx].offDefine, g_aWinMsgs[idx].cchDefine,
                                             pfnOutput, pvArgOutput);
        cchRet += pfnOutput(pvArgOutput, RT_STR_TUPLE(" ("));
    }
    else
        cchRet  = pfnOutput(pvArgOutput, RT_STR_TUPLE("Unknown Status "));

    ssize_t cchValue = RTStrFormatU32(pszTmp, cbTmp, rc, 16, 0, 0, RTSTR_F_SPECIAL);
    Assert(cchValue > 0);
    cchRet += pfnOutput(pvArgOutput, pszTmp, cchValue);

    if (idx != ~(size_t)0)
        cchRet += pfnOutput(pvArgOutput, RT_STR_TUPLE(")"));

    return cchRet;
}

