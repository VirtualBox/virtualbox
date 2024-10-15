/* $Id: testi.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Testcase Framework, the implicit test handle API variation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/test.h>
#include <iprt/stdarg.h>


RTR3DECL(int) RTTestIPrintfV(RTTESTLVL enmLevel, const char *pszFormat, va_list va)
{
    return RTTestPrintfV(NIL_RTTEST, enmLevel, pszFormat, va);
}


RTR3DECL(int) RTTestIPrintf(RTTESTLVL enmLevel, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int cch = RTTestPrintfV(NIL_RTTEST, enmLevel, pszFormat, va);
    va_end(va);
    return cch;
}


RTR3DECL(int) RTTestISub(const char *pszSubTest)
{
    return RTTestSub(NIL_RTTEST, pszSubTest);
}


RTR3DECL(int) RTTestISubF(const char *pszSubTestFmt, ...)
{
    va_list va;
    va_start(va, pszSubTestFmt);
    int cch = RTTestSubV(NIL_RTTEST, pszSubTestFmt, va);
    va_end(va);
    return cch;
}


RTR3DECL(int) RTTestISubV(const char *pszSubTestFmt, va_list va)
{
    return RTTestSubV(NIL_RTTEST, pszSubTestFmt, va);
}


RTR3DECL(int) RTTestISubDone(void)
{
    return RTTestSubDone(NIL_RTTEST);
}


RTR3DECL(int) RTTestISubSub(const char *pszSubSubTest)
{
    return RTTestSubSub(NIL_RTTEST, pszSubSubTest);
}


RTR3DECL(int) RTTestISubSubF(const char *pszSubSubTestFmt, ...)
{
    va_list va;
    va_start(va, pszSubSubTestFmt);
    int cch = RTTestSubSubV(NIL_RTTEST, pszSubSubTestFmt, va);
    va_end(va);
    return cch;
}


RTR3DECL(int) RTTestISubSubV(const char *pszSubSubTestFmt, va_list va)
{
    return RTTestSubSubV(NIL_RTTEST, pszSubSubTestFmt, va);
}


RTR3DECL(int) RTTestISubSubDone(void)
{
    return RTTestSubSubDone(NIL_RTTEST);
}


RTR3DECL(int) RTTestIPassedV(const char *pszFormat, va_list va)
{
    return RTTestPassedV(NIL_RTTEST, pszFormat, va);
}


RTR3DECL(int) RTTestIPassed(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int cch = RTTestPassedV(NIL_RTTEST, pszFormat, va);
    va_end(va);
    return cch;
}


RTR3DECL(int) RTTestIValue(const char *pszName, uint64_t u64Value, RTTESTUNIT enmUnit)
{
    return RTTestValue(NIL_RTTEST, pszName, u64Value, enmUnit);
}


RTR3DECL(int) RTTestIValueF(uint64_t u64Value, RTTESTUNIT enmUnit, const char *pszNameFmt, ...)
{
    va_list va;
    va_start(va, pszNameFmt);
    int rc = RTTestValueV(NIL_RTTEST, u64Value, enmUnit, pszNameFmt, va);
    va_end(va);
    return rc;
}


RTR3DECL(int) RTTestIValueV(uint64_t u64Value, RTTESTUNIT enmUnit, const char *pszNameFmt, va_list va)
{
    return RTTestValueV(NIL_RTTEST, u64Value, enmUnit, pszNameFmt, va);
}


RTR3DECL(int) RTTestIErrorInc(void)
{
    return RTTestErrorInc(NIL_RTTEST);
}


RTR3DECL(uint32_t) RTTestIErrorCount(void)
{
    return RTTestErrorCount(NIL_RTTEST);
}


RTR3DECL(int) RTTestIFailedV(const char *pszFormat, va_list va)
{
    return RTTestFailedV(NIL_RTTEST, pszFormat, va);
}


RTR3DECL(int) RTTestIFailed(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int cch = RTTestFailedV(NIL_RTTEST, pszFormat, va);
    va_end(va);
    return cch;
}


RTR3DECL(int) RTTestIFailedRcV(int rcRet, const char *pszFormat, va_list va)
{
    RTTestFailedV(NIL_RTTEST, pszFormat, va);
    return rcRet;
}


RTR3DECL(int) RTTestIFailedRc(int rcRet, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTTestFailedV(NIL_RTTEST, pszFormat, va);
    va_end(va);
    return rcRet;
}


RTR3DECL(int) RTTestIFailureDetailsV(const char *pszFormat, va_list va)
{
    return RTTestFailureDetails(NIL_RTTEST, pszFormat, va);
}


RTR3DECL(int) RTTestIFailureDetails(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int cch = RTTestFailureDetailsV(NIL_RTTEST, pszFormat, va);
    va_end(va);
    return cch;
}


RTR3DECL(int) RTTestIErrContextV(const char *pszFormat, va_list va)
{
    return RTTestErrContextV(NIL_RTTEST, pszFormat, va);
}


RTR3DECL(int) RTTestIErrContext(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = RTTestErrContextV(NIL_RTTEST, pszFormat, va);
    va_end(va);
    return rc;
}


RTR3DECL(int) RTTestIDisableAssertions(void)
{
    return RTTestDisableAssertions(NIL_RTTEST);
}


RTR3DECL(int) RTTestIRestoreAssertions(void)
{
    return RTTestRestoreAssertions(NIL_RTTEST);
}

