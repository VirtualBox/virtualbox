/* $Id: tstRTR0Common.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Common header.
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

#ifndef IPRT_INCLUDED_SRC_testcase_tstRTR0Common_h
#define IPRT_INCLUDED_SRC_testcase_tstRTR0Common_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/stdarg.h>
#include <iprt/string.h>
#include "tstRTR0CommonReq.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Global error buffer used by macros and inline functions in this file. */
static char g_szErr[2048];
/** The number of errors reported in this g_szErr. */
static uint32_t volatile g_cErrors;


/**
 * Service request handler prolog.
 *
 * Returns if the input is invalid.  Initializes the return packet as well as
 * the globals (g_szErr, g_cErrors).
 *
 * @param   pReqHdr         The request packet header.
 */
#define RTR0TESTR0_SRV_REQ_PROLOG_RET(pReqHdr) \
    do \
    { \
        if (!RT_VALID_PTR(pReqHdr)) \
            return VERR_INVALID_PARAMETER; \
        \
        PRTTSTR0REQ pReq    = (PRTTSTR0REQ)(pReqHdr); \
        size_t      cchErr  = pReqHdr->cbReq - sizeof(pReq->Hdr); \
        if (cchErr < 32 || cchErr >= 0x10000) \
            return VERR_INVALID_PARAMETER; \
        pReq->szMsg[0] = '\0'; \
        \
        /* Initialize the global buffer. */ \
        memset(&g_szErr[0], 0, sizeof(g_szErr)); \
        ASMAtomicWriteU32(&g_cErrors, 0); \
    } while (0)


/**
 * Service request handler epilog.
 *
 * Copies any errors or messages into the request packet.
 *
 * @param   pReqHdr         The request packet header.
 */
#define RTR0TESTR0_SRV_REQ_EPILOG(pReqHdr) \
    do \
    { \
        PRTTSTR0REQ pReq    = (PRTTSTR0REQ)(pReqHdr); \
        size_t      cbErr   = pReqHdr->cbReq - sizeof(pReq->Hdr); \
        if (g_szErr[0] && pReq->szMsg[0] != '!') \
            RTStrCopyEx(pReq->szMsg, (cbErr), g_szErr, sizeof(g_szErr) - 1); \
    } while (0)


/**
 * Implement the sanity check switch-cases of a service request handler.
 */
#define RTR0TESTR0_IMPLEMENT_SANITY_CASES() \
    case RTTSTR0REQ_SANITY_OK: \
        break; \
    case RTTSTR0REQ_SANITY_FAILURE: \
        RTR0TestR0Error("42failure42%4096s", ""); \
        break

/**
 * Implements the default switch-case of a service request handler.
 * @param   uOperation          The operation.
 */
#define RTR0TESTR0_IMPLEMENT_DEFAULT_CASE(uOperation) \
    default: \
        RTR0TestR0Error("Unknown test #%d", (uOperation)); \
        break


/**
 * Macro for checking the return code of an API in the ring-0 testcase.
 *
 * Similar to RTTESTI_CHECK_RC.
 *
 * @param   rcExpr      The expression producing the return code.  Only
 *                      evaluated once.
 * @param   rcExpect    The expected result.  Evaluated multiple times.
 */
#define RTR0TESTR0_CHECK_RC(rcExpr, rcExpect) \
    do { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) \
            RTR0TestR0Error("line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
    } while (0)

/**
 * Same as RTR0TESTR0_CHECK_RC + break.
 */
#define RTR0TESTR0_CHECK_RC_BREAK(rcExpr, rcExpect) \
    if (1) \
    { \
        int rcCheck = (rcExpr); \
        if (rcCheck != (rcExpect)) \
        { \
            RTR0TestR0Error("line %u: %s: expected %Rrc, got %Rrc", __LINE__, #rcExpr, (rcExpect), rcCheck); \
            break; \
        } \
    } else do { } while (0)

/**
 * Macro for checking the return code of an API in the ring-0 testcase.
 *
 * Similar to RTTESTI_CHECK_MSG
 *
 * @param   expr            The expression to evaluate.
 * @param   DetailsArgs     Format string + arguments - in parenthesis.
 */
#define RTR0TESTR0_CHECK_MSG(expr, DetailsArgs) \
    do { \
        if (!(expr)) \
        { \
            RTR0TestR0Error("line %u: expression failed: %s - ", __LINE__, #expr); \
            RTR0TestR0AppendDetails DetailsArgs; \
        } \
    } while (0)

/**
 * Same as RTR0TESTR0_CHECK_MSG + break.
 */
#define RTR0TESTR0_CHECK_MSG_BREAK(expr, DetailsArgs) \
    if (!(expr)) \
    { \
        RTR0TestR0Error("line %u: expression failed: %s - ", __LINE__, #expr); \
        RTR0TestR0AppendDetails DetailsArgs; \
        break; \
    } else do { } while (0)

/**
 * Same as RTR0TESTR0_CHECK_MSG + return @a rcRete.
 */
#define RTR0TESTR0_CHECK_MSG_RET(expr, DetailsArgs, rcRet) \
    do { \
        if (!(expr)) \
        { \
            RTR0TestR0Error("line %u: expression failed: %s - ", __LINE__, #expr); \
            RTR0TestR0AppendDetails DetailsArgs; \
            return (rcRet); \
        } \
    } while (0)

/**
 * Macro for skipping a test in the ring-0 testcase.
 */
#define RTR0TESTR0_SKIP() \
    do { \
        RTR0TestR0Skip("line %u: SKIPPED", __LINE__); \
    } while (0)

/**
 * Same as RTR0TESTR0_SKIP + break.
 */
#define RTR0TESTR0_SKIP_BREAK() \
    if (1) \
    { \
        RTR0TestR0Skip("line %u: SKIPPED", __LINE__); \
        break; \
    } else do { } while (0)


/**
 * Report an error.
 */
DECLINLINE(void) RTR0TestR0Error(const char *pszFormat, ...)
{
    size_t off = RTStrNLen(g_szErr, sizeof(g_szErr) - 1);
    size_t cbLeft = sizeof(g_szErr) - off;
    if (cbLeft > 10)
    {
        char *psz = &g_szErr[off];
        if (off)
        {
            *psz++  = '\n';
            *psz++  = '\n';
            cbLeft -= 2;
        }
        *psz++ = '!';
        cbLeft--;

        va_list va;
        va_start(va, pszFormat);
        RTStrPrintfV(psz, cbLeft, pszFormat, va);
        va_end(va);
    }
    ASMAtomicIncU32(&g_cErrors);
}


/**
 * Append error details.
 */
static void RTR0TestR0AppendDetails(const char *pszFormat, ...)
{
    size_t      off = RTStrNLen(g_szErr, sizeof(g_szErr) - 1);
    va_list     va;
    va_start(va, pszFormat);
    RTStrPrintfV(&g_szErr[off], sizeof(g_szErr) - off, pszFormat, va);
    va_end(va);
}


/**
 * Informational message.
 */
DECLINLINE(void) RTR0TestR0Info(const char *pszFormat, ...)
{
    size_t off = RTStrNLen(g_szErr, sizeof(g_szErr) - 1);
    size_t cbLeft = sizeof(g_szErr) - off;
    if (cbLeft > 10)
    {
        char *psz = &g_szErr[off];
        if (off)
        {
            *psz++  = '\n';
            *psz++  = '\n';
            cbLeft -= 2;
        }
        *psz++ = '?';
        cbLeft--;

        va_list va;
        va_start(va, pszFormat);
        RTStrPrintfV(psz, cbLeft, pszFormat, va);
        va_end(va);
    }
}


/**
 * Report an error.
 */
DECLINLINE(void) RTR0TestR0Skip(const char *pszFormat, ...)
{
    size_t off = RTStrNLen(g_szErr, sizeof(g_szErr) - 1);
    size_t cbLeft = sizeof(g_szErr) - off;
    if (cbLeft > 10)
    {
        char *psz = &g_szErr[off];
        if (off)
        {
            *psz++  = '\n';
            *psz++  = '\n';
            cbLeft -= 2;
        }
        *psz++ = '$';
        cbLeft--;

        va_list va;
        va_start(va, pszFormat);
        RTStrPrintfV(psz, cbLeft, pszFormat, va);
        va_end(va);
    }
    ASMAtomicIncU32(&g_cErrors);
}


/**
 * Checks if we have any error reports.
 *
 * @returns true if there are errors, false if none.
 */
DECLINLINE(bool) RTR0TestR0HaveErrors(void)
{
    return ASMAtomicUoReadU32(&g_cErrors) > 0;
}

#endif /* !IPRT_INCLUDED_SRC_testcase_tstRTR0Common_h */

