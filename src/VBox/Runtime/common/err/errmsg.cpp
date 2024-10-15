/* $Id: errmsg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Status code messages.
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
#include <iprt/err.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/string.h>
#include <VBox/err.h>

#include <iprt/bldprog-strtab.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if (defined(IN_RT_STATIC) || defined(IPRT_ERRMSG_DEFINES_ONLY)) && !defined(IPRT_ERRMSG_NO_FULL_MSG)
/** Skip the full message in static builds to save space.
 * This is defined when IPRT_ERRMSG_DEFINES_ONLY is defined. */
# define IPRT_ERRMSG_NO_FULL_MSG
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IPRT_NO_ERROR_DATA
/* Cook data just for VINF_SUCCESS so that code below compiles fine. */
static const char            g_achStrTabData[] = { "VINF_SUCCESS" };
static const RTBLDPROGSTRTAB g_StatusMsgStrTab = { g_achStrTabData, sizeof(g_achStrTabData) - 1, 0, NULL };
static const struct
{
    int16_t     iCode;
    uint8_t     offDefine;
    uint8_t     cchDefine;
    uint8_t     offMsgShort;
    uint8_t     cchMsgShort;
    uint8_t     offMsgFull;
    uint8_t     cchMsgFull;
} g_aStatusMsgs[] =
{
    { VINF_SUCCESS, 0, 12, 0, 12, 0, 12, },
};
#elif defined(IPRT_ERRMSG_DEFINES_ONLY)
# include "errmsgdata-only-defines.h"
#elif defined(IPRT_ERRMSG_NO_FULL_MSG)
# include "errmsgdata-no-full-msg.h"
#else
# include "errmsgdata-all.h"
#endif


/**
 * Looks up the message table entry for @a rc.
 *
 * @returns index into g_aStatusMsgs on success, ~(size_t)0 if not found.
 * @param   rc      The status code to locate the entry for.
 */
static size_t rtErrLookup(int rc)
{
    /*
     * Perform binary search (duplicate code in rtErrWinLookup).
     */
    size_t iStart = 0;
    size_t iEnd   = RT_ELEMENTS(g_aStatusMsgs);
    for (;;)
    {
        size_t i = iStart + (iEnd - iStart) / 2;
        int const iCode = g_aStatusMsgs[i].iCode;
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
    for (size_t i = 0; i < RT_ELEMENTS(g_aStatusMsgs); i++)
        Assert(g_aStatusMsgs[i].iCode != rc);
#endif

    return ~(size_t)0;
}


RTDECL(bool)  RTErrIsKnown(int rc)
{
    return rtErrLookup(rc) != ~(size_t)0;
}
RT_EXPORT_SYMBOL(RTErrIsKnown);


RTDECL(ssize_t) RTErrQueryDefine(int rc, char *pszBuf, size_t cbBuf, bool fFailIfUnknown)
{
    size_t idx = rtErrLookup(rc);
    if (idx != ~(size_t)0)
        return RTBldProgStrTabQueryString(&g_StatusMsgStrTab,
                                          g_aStatusMsgs[idx].offDefine, g_aStatusMsgs[idx].cchDefine,
                                          pszBuf, cbBuf);
    if (fFailIfUnknown)
        return VERR_NOT_FOUND;
    return RTStrFormatU32(pszBuf, cbBuf, rc, 10, 0, 0, RTSTR_F_VALSIGNED);
}
RT_EXPORT_SYMBOL(RTErrQueryDefine);


/**
 * Helper for rtErrQueryMsgNotFound.
 */
DECLINLINE(ssize_t) rtErrQueryCopyHelper(char **ppszBuf, size_t *pcbBuf, const char *pszSrc, size_t cchSrc, ssize_t cchRet)
{
    char   *pszDst = *ppszBuf;
    size_t  cbDst  = *pcbBuf;
    if (cbDst > cchSrc)
    {
        memcpy(pszDst, pszSrc, cchSrc);
        cbDst   -= cchSrc;
        pszDst  += cchSrc;
        cchRet  += cchSrc;
        *pszDst  = '\0';
    }
    else
    {
        while (cbDst > 1 && cchSrc > 0)
        {
            *pszDst++ = *pszSrc++;
            cchSrc--;
            cbDst--;
        }
        if (cbDst > 0)
            *pszDst = '\0';
        cchRet = VERR_BUFFER_OVERFLOW;
    }
    *ppszBuf = pszDst;
    *pcbBuf  = cbDst;
    return cchRet;
}


/**
 * RTErrQueryMsgShort & RTErrQueryMsgFull helper.
 */
DECL_NO_INLINE(static, ssize_t) rtErrQueryMsgNotFound(int rc, char *pszBuf, size_t cbBuf)
{
    /* Unknown Status %d (%#x) */
    ssize_t cchRet = rtErrQueryCopyHelper(&pszBuf, &cbBuf, RT_STR_TUPLE("Unknown Status "), 0);
    char   szValue[64];
    size_t cchValue = (size_t)RTStrFormatU32(szValue, sizeof(szValue), rc, 10, 0, 0, RTSTR_F_VALSIGNED);
    cchRet = rtErrQueryCopyHelper(&pszBuf, &cbBuf, szValue, cchValue, cchRet);
    cchRet = rtErrQueryCopyHelper(&pszBuf, &cbBuf, RT_STR_TUPLE(" ("), cchRet);
    cchValue = (size_t)RTStrFormatU32(szValue, sizeof(szValue), rc, 16, 0, 0, RTSTR_F_SPECIAL);
    cchRet = rtErrQueryCopyHelper(&pszBuf, &cbBuf, szValue, cchValue, cchRet);
    cchRet = rtErrQueryCopyHelper(&pszBuf, &cbBuf, RT_STR_TUPLE(")"), cchRet);
    return cchRet;
}


RTDECL(ssize_t) RTErrQueryMsgShort(int rc, char *pszBuf, size_t cbBuf, bool fFailIfUnknown)
{
    size_t idx = rtErrLookup(rc);
    if (idx != ~(size_t)0)
#ifdef IPRT_ERRMSG_DEFINES_ONLY
        return RTBldProgStrTabQueryString(&g_StatusMsgStrTab,
                                          g_aStatusMsgs[idx].offDefine, g_aStatusMsgs[idx].cchDefine,
                                          pszBuf, cbBuf);
#else
    return RTBldProgStrTabQueryString(&g_StatusMsgStrTab,
                                      g_aStatusMsgs[idx].offMsgShort, g_aStatusMsgs[idx].cchMsgShort,
                                      pszBuf, cbBuf);
#endif
    if (fFailIfUnknown)
        return VERR_NOT_FOUND;
    return rtErrQueryMsgNotFound(rc, pszBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTErrQueryMsgShort);


RTDECL(ssize_t) RTErrQueryMsgFull(int rc, char *pszBuf, size_t cbBuf, bool fFailIfUnknown)
{
#if defined(IPRT_ERRMSG_DEFINES_ONLY) || defined(IPRT_ERRMSG_NO_FULL_MSG)
    return RTErrQueryMsgShort(rc, pszBuf, cbBuf, fFailIfUnknown);
#else
    size_t idx = rtErrLookup(rc);
    if (idx != ~(size_t)0)
        return RTBldProgStrTabQueryString(&g_StatusMsgStrTab,
                                          g_aStatusMsgs[idx].offMsgFull, g_aStatusMsgs[idx].cchMsgFull,
                                          pszBuf, cbBuf);
    if (fFailIfUnknown)
        return VERR_NOT_FOUND;
    return rtErrQueryMsgNotFound(rc, pszBuf, cbBuf);
#endif
}
RT_EXPORT_SYMBOL(RTErrQueryMsgShort);


RTDECL(size_t) RTErrFormatDefine(int rc, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, char *pszTmp, size_t cbTmp)
{
    size_t idx = rtErrLookup(rc);
    if (idx != ~(size_t)0)
        return RTBldProgStrTabQueryOutput(&g_StatusMsgStrTab,
                                          g_aStatusMsgs[idx].offDefine, g_aStatusMsgs[idx].cchDefine,
                                          pfnOutput, pvArgOutput);
    size_t cchValue = (size_t)RTStrFormatU32(pszTmp, cbTmp, rc, 10, 0, 0, RTSTR_F_VALSIGNED);
    return pfnOutput(pvArgOutput, pszTmp, cchValue);
}
RT_EXPORT_SYMBOL(RTErrFormatDefine);


/**
 * RTErrFormatMsgShort & RTErrFormatMsgFull helper.
 */
static size_t rtErrFormatMsgNotFound(int rc, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, char *pszTmp, size_t cbTmp)
{
    size_t cchRet = pfnOutput(pvArgOutput, RT_STR_TUPLE("Unknown Status "));
    size_t cchValue = (size_t)RTStrFormatU32(pszTmp, cbTmp, rc, 10, 0, 0, RTSTR_F_VALSIGNED);
    cchRet  += pfnOutput(pvArgOutput, pszTmp, cchValue);
    cchRet  += pfnOutput(pvArgOutput, RT_STR_TUPLE(" ("));
    cchValue = (size_t)RTStrFormatU32(pszTmp, cbTmp, rc, 16, 0, 0, RTSTR_F_SPECIAL);
    cchRet  += pfnOutput(pvArgOutput, pszTmp, cchValue);
    cchRet  += pfnOutput(pvArgOutput, RT_STR_TUPLE(")"));
    return cchRet;
}


RTDECL(size_t) RTErrFormatMsgShort(int rc, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, char *pszTmp, size_t cbTmp)
{
    size_t idx = rtErrLookup(rc);
    if (idx != ~(size_t)0)
#ifndef IPRT_ERRMSG_DEFINES_ONLY
        return RTBldProgStrTabQueryOutput(&g_StatusMsgStrTab,
                                          g_aStatusMsgs[idx].offMsgShort, g_aStatusMsgs[idx].cchMsgShort,
                                          pfnOutput, pvArgOutput);
#else
        return RTBldProgStrTabQueryOutput(&g_StatusMsgStrTab,
                                          g_aStatusMsgs[idx].offDefine, g_aStatusMsgs[idx].cchDefine,
                                          pfnOutput, pvArgOutput);
#endif
    return rtErrFormatMsgNotFound(rc, pfnOutput, pvArgOutput, pszTmp, cbTmp);
}
RT_EXPORT_SYMBOL(RTErrFormatMsgShort);


RTDECL(size_t) RTErrFormatMsgFull(int rc, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, char *pszTmp, size_t cbTmp)
{
#ifdef IPRT_ERRMSG_NO_FULL_MSG
    return RTErrFormatMsgShort(rc, pfnOutput, pvArgOutput, pszTmp, cbTmp);
#else
    size_t idx = rtErrLookup(rc);
    if (idx != ~(size_t)0)
        return RTBldProgStrTabQueryOutput(&g_StatusMsgStrTab,
                                          g_aStatusMsgs[idx].offMsgFull, g_aStatusMsgs[idx].cchMsgFull,
                                          pfnOutput, pvArgOutput);
    return rtErrFormatMsgNotFound(rc, pfnOutput, pvArgOutput, pszTmp, cbTmp);
#endif
}
RT_EXPORT_SYMBOL(RTErrFormatMsgFull);


RTDECL(size_t) RTErrFormatMsgAll(int rc, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, char *pszTmp, size_t cbTmp)
{
    size_t idx = rtErrLookup(rc);
    if (idx != ~(size_t)0)
    {
        size_t cchValue;
        size_t cchRet = RTBldProgStrTabQueryOutput(&g_StatusMsgStrTab,
                                                   g_aStatusMsgs[idx].offDefine, g_aStatusMsgs[idx].cchDefine,
                                                   pfnOutput, pvArgOutput);
        cchRet += pfnOutput(pvArgOutput, RT_STR_TUPLE(" ("));
        cchValue = (size_t)RTStrFormatU32(pszTmp, cbTmp, rc, 10, 0, 0, RTSTR_F_VALSIGNED);
        cchRet += pfnOutput(pvArgOutput, pszTmp, cchValue);
#ifdef IPRT_ERRMSG_DEFINES_ONLY
        cchRet += pfnOutput(pvArgOutput, RT_STR_TUPLE(")"));
#elif defined(IPRT_ERRMSG_NO_FULL_MSG)
        cchRet += pfnOutput(pvArgOutput, RT_STR_TUPLE(") - "));
        cchRet += RTBldProgStrTabQueryOutput(&g_StatusMsgStrTab,
                                             g_aStatusMsgs[idx].offMsgShort, g_aStatusMsgs[idx].cchMsgShort,
                                             pfnOutput, pvArgOutput);
#else
        cchRet += pfnOutput(pvArgOutput, RT_STR_TUPLE(") - "));
        cchRet += RTBldProgStrTabQueryOutput(&g_StatusMsgStrTab,
                                             g_aStatusMsgs[idx].offMsgFull, g_aStatusMsgs[idx].cchMsgFull,
                                             pfnOutput, pvArgOutput);
#endif
        return cchRet;
    }
    return rtErrFormatMsgNotFound(rc, pfnOutput, pvArgOutput, pszTmp, cbTmp);
}
RT_EXPORT_SYMBOL(RTErrFormatMsgAll);

