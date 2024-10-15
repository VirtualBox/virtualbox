/* $Id: assert.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Assertions, common code.
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
#include <iprt/assert.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#ifdef IPRT_WITH_ASSERT_STACK
# ifndef IN_RING3
#  error "IPRT_WITH_ASSERT_STACK is only for ring-3 at present."
# endif
# include <iprt/dbg.h>
#endif
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#ifdef IN_RING3
# include <iprt/env.h>
# ifndef IPRT_NO_CRT
#  include <stdio.h>
# endif
# ifdef RT_OS_WINDOWS
#  include <iprt/win/windows.h>
#  include "../../r3/win/internal-r3-win.h"
# endif
#endif
#include "internal/assert.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The last assertion message, 1st part. */
RTDATADECL(char)                    g_szRTAssertMsg1[1024];
RT_EXPORT_SYMBOL(g_szRTAssertMsg1);
/** The last assertion message, 2nd part. */
RTDATADECL(char)                    g_szRTAssertMsg2[4096];
RT_EXPORT_SYMBOL(g_szRTAssertMsg2);
#ifdef IPRT_WITH_ASSERT_STACK
/** The last assertion message, stack part. */
RTDATADECL(char)                    g_szRTAssertStack[4096];
RT_EXPORT_SYMBOL(g_szRTAssertStack);
#endif
/** The length of the g_szRTAssertMsg2 content.
 * @remarks Race.  */
static uint32_t volatile            g_cchRTAssertMsg2;
/** The last assertion message, expression. */
RTDATADECL(const char * volatile)   g_pszRTAssertExpr;
RT_EXPORT_SYMBOL(g_pszRTAssertExpr);
/** The last assertion message, function name. */
RTDATADECL(const char *  volatile)  g_pszRTAssertFunction;
RT_EXPORT_SYMBOL(g_pszRTAssertFunction);
/** The last assertion message, file name. */
RTDATADECL(const char * volatile)   g_pszRTAssertFile;
RT_EXPORT_SYMBOL(g_pszRTAssertFile);
/** The last assertion message, line number. */
RTDATADECL(uint32_t volatile)       g_u32RTAssertLine;
RT_EXPORT_SYMBOL(g_u32RTAssertLine);


/** Set if assertions are quiet. */
static bool volatile                g_fQuiet = false;
/** Set if assertions may panic. */
static bool volatile                g_fMayPanic = true;


RTDECL(bool) RTAssertSetQuiet(bool fQuiet)
{
    return ASMAtomicXchgBool(&g_fQuiet, fQuiet);
}
RT_EXPORT_SYMBOL(RTAssertSetQuiet);


RTDECL(bool) RTAssertAreQuiet(void)
{
    return ASMAtomicUoReadBool(&g_fQuiet);
}
RT_EXPORT_SYMBOL(RTAssertAreQuiet);


RTDECL(bool) RTAssertSetMayPanic(bool fMayPanic)
{
    return ASMAtomicXchgBool(&g_fMayPanic, fMayPanic);
}
RT_EXPORT_SYMBOL(RTAssertSetMayPanic);


RTDECL(bool) RTAssertMayPanic(void)
{
    return ASMAtomicUoReadBool(&g_fMayPanic);
}
RT_EXPORT_SYMBOL(RTAssertMayPanic);


RTDECL(void) RTAssertMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    /*
     * Fill in the globals.
     */
    ASMAtomicUoWritePtr(&g_pszRTAssertExpr, pszExpr);
    ASMAtomicUoWritePtr(&g_pszRTAssertFile, pszFile);
    ASMAtomicUoWritePtr(&g_pszRTAssertFunction, pszFunction);
    ASMAtomicUoWriteU32(&g_u32RTAssertLine, uLine);
    RTStrPrintf(g_szRTAssertMsg1, sizeof(g_szRTAssertMsg1),
                "\n!!Assertion Failed!!\n"
                "Expression: %s\n"
                "Location  : %s(%d) %s\n",
                pszExpr, pszFile, uLine, pszFunction);

    /*
     * If not quiet, make noise.
     */
    if (!RTAssertAreQuiet())
    {
        RTERRVARS SavedErrVars;
        RTErrVarsSave(&SavedErrVars);

#ifdef IPRT_WITH_ASSERT_STACK
        /* The stack dump. */
        static volatile bool s_fDumpingStackAlready = false; /* for simple recursion prevention */
        char   szStack[sizeof(g_szRTAssertStack)];
        size_t cchStack = 0;
# if defined(IN_RING3) && defined(RT_OS_WINDOWS) /** @todo make this stack on/off thing more modular. */
        bool   fStack = (!g_pfnIsDebuggerPresent || !g_pfnIsDebuggerPresent()) && !RTEnvExist("IPRT_ASSERT_NO_STACK");
# elif defined(IN_RING3)
        bool   fStack = !RTEnvExist("IPRT_ASSERT_NO_STACK");
# else
        bool   fStack = true;
# endif
        szStack[0] = '\0';
        if (fStack && !s_fDumpingStackAlready)
        {
            s_fDumpingStackAlready = true;
            cchStack = RTDbgStackDumpSelf(szStack, sizeof(szStack), 0);
            s_fDumpingStackAlready = false;
        }
        memcpy(g_szRTAssertStack, szStack, cchStack + 1);
#endif

#ifdef IN_RING0
# ifdef IN_GUEST_R0
        RTLogBackdoorPrintf("\n!!Assertion Failed!!\n"
                            "Expression: %s\n"
                            "Location  : %s(%d) %s\n",
                            pszExpr, pszFile, uLine, pszFunction);
# endif
        /** @todo fully integrate this with the logger... play safe a bit for now.  */
        rtR0AssertNativeMsg1(pszExpr, uLine, pszFile, pszFunction);

#else  /* !IN_RING0 */


# if defined(IN_RING3) && (defined(IN_RT_STATIC) || defined(IPRT_NO_CRT)) /* ugly */
        if (g_pfnRTLogAssert)
            g_pfnRTLogAssert(
# else
        RTLogAssert(
# endif
                             "\n!!Assertion Failed!!\n"
                             "Expression: %s\n"
                             "Location  : %s(%d) %s\n"
# ifdef IPRT_WITH_ASSERT_STACK
                             "Stack     :\n%s\n"
# endif
                             , pszExpr, pszFile, uLine, pszFunction
# ifdef IPRT_WITH_ASSERT_STACK
                             , szStack
# endif
                             );

# ifdef IN_RING3
        /* print to stderr, helps user and gdb debugging. */
#  ifndef IPRT_NO_CRT
        fprintf(stderr,
                "\n!!Assertion Failed!!\n"
                "Expression: %s\n"
                "Location  : %s(%d) %s\n",
                RT_VALID_PTR(pszExpr) ? pszExpr : "<none>",
                RT_VALID_PTR(pszFile) ? pszFile : "<none>",
                uLine,
                RT_VALID_PTR(pszFunction) ? pszFunction : "");
#   ifdef IPRT_WITH_ASSERT_STACK
        fprintf(stderr, "Stack     :\n%s\n", szStack);
#   endif
        fflush(stderr);
#  else
        char szMsg[2048];
        size_t cchMsg = RTStrPrintf(szMsg, sizeof(szMsg),
                                    "\n!!Assertion Failed!!\n"
                                    "Expression: %s\n"
                                    "Location  : %s(%d) %s\n",
                                    RT_VALID_PTR(pszExpr) ? pszExpr : "<none>",
                                    RT_VALID_PTR(pszFile) ? pszFile : "<none>",
                                    uLine,
                                    RT_VALID_PTR(pszFunction) ? pszFunction : "");
        RTLogWriteStdErr(szMsg, cchMsg);
#   ifdef IPRT_WITH_ASSERT_STACK
        RTLogWriteStdErr(RT_STR_TUPLE("Stack     :\n"));
        RTLogWriteStdErr(szStack, strlen(szStack));
        RTLogWriteStdErr(RT_STR_TUPLE("\n"));
#   endif
#  endif
# endif
#endif /* !IN_RING0 */

        RTErrVarsRestore(&SavedErrVars);
    }
}
RT_EXPORT_SYMBOL(RTAssertMsg1);


/**
 * Worker for RTAssertMsg2V and RTAssertMsg2AddV
 *
 * @param   fInitial            True if it's RTAssertMsg2V, otherwise false.
 * @param   pszFormat           The message format string.
 * @param   va                  The format arguments.
 */
static void rtAssertMsg2Worker(bool fInitial, const char *pszFormat, va_list va)
{
    va_list vaCopy;
    size_t  cch;

    /*
     * The global first.
     */
    if (fInitial)
    {
        va_copy(vaCopy, va);
        cch = RTStrPrintfV(g_szRTAssertMsg2, sizeof(g_szRTAssertMsg2), pszFormat, vaCopy);
        ASMAtomicWriteU32(&g_cchRTAssertMsg2, (uint32_t)cch);
        va_end(vaCopy);
    }
    else
    {
        cch = ASMAtomicReadU32(&g_cchRTAssertMsg2);
        if (cch < sizeof(g_szRTAssertMsg2) - 4)
        {
            va_copy(vaCopy, va);
            cch += RTStrPrintfV(&g_szRTAssertMsg2[cch], sizeof(g_szRTAssertMsg2) - cch, pszFormat, vaCopy);
            ASMAtomicWriteU32(&g_cchRTAssertMsg2, (uint32_t)cch);
            va_end(vaCopy);
        }
    }

    /*
     * If not quiet, make some noise.
     */
    if (!RTAssertAreQuiet())
    {
        RTERRVARS SavedErrVars;
        RTErrVarsSave(&SavedErrVars);

#ifdef IN_RING0
# ifdef IN_GUEST_R0
        va_copy(vaCopy, va);
        RTLogBackdoorPrintfV(pszFormat, vaCopy);
        va_end(vaCopy);
# endif
        /** @todo fully integrate this with the logger... play safe a bit for now.  */
        rtR0AssertNativeMsg2V(fInitial, pszFormat, va);

#else  /* !IN_RING0 */

# if defined(IN_RING3) && (defined(IN_RT_STATIC) || defined(IPRT_NO_CRT))
        if (g_pfnRTLogAssert)
# endif
        {
            va_copy(vaCopy, va);
# if defined(IN_RING3) && (defined(IN_RT_STATIC) || defined(IPRT_NO_CRT))
            g_pfnRTLogAssertV(pszFormat, vaCopy);
# else
            RTLogAssertV(pszFormat, vaCopy);
# endif
            va_end(vaCopy);
        }

# ifdef IN_RING3
        /* print to stderr, helps user and gdb debugging. */
        char szMsg[sizeof(g_szRTAssertMsg2)];
        va_copy(vaCopy, va);
        size_t cchMsg = RTStrPrintfV(szMsg, sizeof(szMsg), pszFormat, vaCopy);
        va_end(vaCopy);
#  ifndef IPRT_NO_CRT
        fwrite(szMsg, 1, cchMsg, stderr);
        fflush(stderr);
#  else
        RTLogWriteStdErr(szMsg, cchMsg);
#  endif
# endif
#endif /* !IN_RING0 */

        RTErrVarsRestore(&SavedErrVars);
    }
}


RTDECL(void) RTAssertMsg2V(const char *pszFormat, va_list va)
{
    rtAssertMsg2Worker(true /*fInitial*/, pszFormat, va);
}
RT_EXPORT_SYMBOL(RTAssertMsg2V);


RTDECL(void) RTAssertMsg2AddV(const char *pszFormat, va_list va)
{
    rtAssertMsg2Worker(false /*fInitial*/, pszFormat, va);
}
RT_EXPORT_SYMBOL(RTAssertMsg2AddV);

