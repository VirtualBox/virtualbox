/* $Id: exceptionsR3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * exceptionsR3 - Tests various ring-3 CPU exceptions.
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
#include <iprt/cdefs.h>
#include <iprt/ctype.h>
#include <iprt/getopt.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/x86.h>

#include <setjmp.h>

#ifndef RT_OS_WINDOWS
# define USE_SIGNALS
# include <signal.h>
# include <stdlib.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Executes a simple test. */
#define TST_XCPT(Trapper, iTrap, uErr) \
    do \
    { \
        RTTestISub(#Trapper); \
        tstXcptReset(); \
        if (!setjmp(g_JmpBuf)) \
        { \
            tstXcptAsm##Trapper(); \
            RTTestIFailed("%s didn't trap (line no %u)", #Trapper, __LINE__); \
        } \
        else if (   (iTrap) != tstXcptCurTrap()  \
                 || (uErr) != tstXcptCurErr()  ) \
            RTTestIFailed("%s trapped with %#x/%#x, expected %#x/%#x (line no %u)", \
                          #Trapper, tstXcptCurTrap(), tstXcptCurErr(), (iTrap), (uErr), __LINE__); \
        else \
            RTTestISubDone(); \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Where to longjmp to when getting a signal/exception. */
jmp_buf             g_JmpBuf;
#ifdef USE_SIGNALS
/** Pending signal.
 * -1 if no signal is pending. */
int32_t volatile    g_iSignal;
/** Pending signal info. */
siginfo_t volatile  g_SigInfo;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLASM(void) tstXcptAsmNullPtrRead(void);
DECLASM(void) tstXcptAsmNullPtrWrite(void);
DECLASM(void) tstXcptAsmSysEnter(void);
DECLASM(void) tstXcptAsmSysCall(void);



#ifdef USE_SIGNALS
/**
 * Generic signal handler.
 */
static void tstXcptSigHandler(int iSignal, siginfo_t *pSigInfo, void *pvCtx)
{
#if 1
    RTStrmPrintf(g_pStdErr, "signal %d pSigInfo=%p pvCtx=%p", iSignal, pSigInfo, pvCtx);
    if (pSigInfo)
        RTStrmPrintf(g_pStdErr, " si_addr=%p si_code=%#x sival_ptr=%p sival_int=%d",
                     pSigInfo->si_addr, pSigInfo->si_code, pSigInfo->si_value.sival_ptr, pSigInfo->si_value.sival_int);
    RTStrmPrintf(g_pStdErr, "\n");
#endif
    if (g_iSignal == -1)
    {
        g_iSignal = iSignal;
        if (pSigInfo)
            memcpy((void *)&g_SigInfo, pSigInfo, sizeof(g_SigInfo));
        longjmp(g_JmpBuf, 1);
    }
    else
    {
        /* we're up the infamous creek... */
        _Exit(2);
    }
}

#elif defined(RT_OS_WINDOWS)
/** @todo  */
//# error "PORTME"

#else
# error "PORTME"
#endif


/** Reset the current exception state and get ready for a new trap. */
static void tstXcptReset(void)
{
#ifdef USE_SIGNALS
    g_iSignal = -1;
    memset((void *)&g_SigInfo, 0, sizeof(g_SigInfo));
#endif
}



/** Get the current intel trap number. Returns -1 if none.  */
static int tstXcptCurTrap(void)
{
#ifdef USE_SIGNALS
    /** @todo this is just a quick sketch. */
    switch (g_iSignal)
    {
        case SIGBUS:
# ifdef RT_OS_DARWIN
            if (g_SigInfo.si_code == 2 /*KERN_PROTECTION_FAILURE*/)
                return X86_XCPT_PF;
# endif
            return X86_XCPT_GP;

        case SIGSEGV:
            return X86_XCPT_GP;
    }
#endif
    return -1;
}


/** Get the exception error code if applicable. */
static uint32_t tstXcptCurErr(void)
{
#ifdef USE_SIGNALS
    /** @todo this is just a quick sketch. */
    switch (g_iSignal)
    {
        case SIGBUS:
# ifdef RT_OS_DARWIN
            if (g_SigInfo.si_code == 2 /*KERN_PROTECTION_FAILURE*/)
                return 0;
# endif
            break;

        case SIGSEGV:
            break;
    }
#endif
    return UINT32_MAX;
}


int main(int argc, char **argv)
{
    /*
     * Prolog.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("exceptionsR3", &hTest);
    if (rc)
        return rc;

    /*
     * Parse options.
     */
    bool volatile fRawMode = false;
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--raw-mode",    'r', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTUNION ValUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((rc = RTGetOpt(&GetState, &ValUnion)))
    {
        switch (rc)
        {
            case 'r':
                fRawMode = true;
                break;

            default:
                return RTGetOptPrintError(rc, &ValUnion);
        }
    }

    /*
     * Test setup.
     */
#ifdef USE_SIGNALS
    struct sigaction Act;
    RT_ZERO(Act);
    Act.sa_sigaction = tstXcptSigHandler;
    Act.sa_flags     = SA_SIGINFO;
    sigfillset(&Act.sa_mask);

    sigaction(SIGILL,  &Act, NULL);
    sigaction(SIGTRAP, &Act, NULL);
# ifdef SIGEMT
    sigaction(SIGEMT,  &Act, NULL);
# endif
    sigaction(SIGFPE,  &Act, NULL);
    sigaction(SIGBUS,  &Act, NULL);
    sigaction(SIGSEGV, &Act, NULL);

#else
    /** @todo Implement this using structured exception handling on Windows and
     *        OS/2. */
#endif

    /*
     * The tests.
     */
    RTTestBanner(hTest);
    TST_XCPT(NullPtrRead,     X86_XCPT_PF, 0);
    TST_XCPT(NullPtrWrite,    X86_XCPT_PF, 0);
    if (fRawMode)
    {
        TST_XCPT(SysEnter,        X86_XCPT_GP, 0);
        TST_XCPT(SysCall,         X86_XCPT_UD, 0);
    }

    /*
     * Epilog.
     */
    return RTTestSummaryAndDestroy(hTest);
}

