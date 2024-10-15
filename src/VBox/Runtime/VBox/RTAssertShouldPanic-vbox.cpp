/* $Id: RTAssertShouldPanic-vbox.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Assertions, generic RTAssertShouldPanic.
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
#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/string.h>

/** @def VBOX_RTASSERT_WITH_GDB
 * Enables the 'gdb' VBOX_ASSERT option.
 */
#if defined(DOXYGEN_RUNNING) \
 || (   !defined(VBOX_RTASSERT_WITH_GDB) \
     && !defined(IN_GUEST) \
     && !defined(IN_RT_STATIC) /* valkit too big, sorry */ \
     && !defined(RT_OS_OS2) \
     && !defined(RT_OS_WINDOWS))
# define VBOX_RTASSERT_WITH_GDB
#endif

/** @def VBOX_RTASSERT_WITH_WAIT
 * Enables the 'wait' VBOX_ASSERT option.
 */
#if defined(DOXYGEN_RUNNING) \
 || (   !defined(VBOX_RTASSERT_WITH_WAIT) \
     && defined(IN_RING3) \
     && !defined(RT_OS_OS2) \
     && !defined(RT_OS_WINDOWS))
# define VBOX_RTASSERT_WITH_WAIT
#endif

#ifdef VBOX_RTASSERT_WITH_GDB
# include <iprt/process.h>
# include <iprt/path.h>
# include <iprt/thread.h>
# include <iprt/asm.h>
#endif

#ifdef VBOX_RTASSERT_WITH_WAIT
# include <signal.h>
# include <unistd.h>
#endif

/**
 * Worker that we can wrap with error variable saving and restoring.
 */
static bool rtAssertShouldPanicWorker(void)
{
    /*
     * Check for the VBOX_ASSERT variable.
     */
    const char *psz = RTEnvGet("VBOX_ASSERT");

    /* not defined => default behaviour. */
    if (!psz)
        return true;

    /* 'breakpoint' or 'panic' means default behaviour. */
    if (!strcmp(psz, "breakpoint") || !strcmp(psz, "panic"))
        return true;

    /* 'disabled' does not trigger a breakpoint. */
    if (!strcmp(psz, "disabled"))
        return false;

#ifdef VBOX_RTASSERT_WITH_WAIT
    /* 'wait' - execute a sigwait(3) while a debugger is attached. */
    if (!strcmp(psz, "wait"))
    {
        sigset_t signalMask, oldMask;
        int      iSignal;
        static pid_t lastPid = -1;

        /* Only wait on the first assertion we hit per process fork, assuming
         * the user will attach the debugger at once if they want to do so.
         * For a clever solution to detect an attached debugger, see:
         *   http://stackoverflow.com/a/8135517
         * For now I preferred to keep things simple and hopefully reliable. */
        if (lastPid == getpid())
            return true;
        lastPid = getpid();
        /* Register signal that we are waiting for */
        sigemptyset(&signalMask);
        sigaddset(&signalMask, SIGUSR2);
        RTAssertMsg2("Attach debugger (pid: %ld) and resume with SIGUSR2.\n", (long)lastPid);
        pthread_sigmask(SIG_BLOCK, &signalMask, &oldMask);
        /* Ignoring return status */
        sigwait(&signalMask, &iSignal);
        pthread_sigmask(SIG_SETMASK, &oldMask, NULL);
        /* Breakpoint no longer needed. */
        return false;
    }
#endif

#ifdef VBOX_RTASSERT_WITH_GDB
    /* 'gdb' - means try launch a gdb session in xterm. */
    if (!strcmp(psz, "gdb"))
    {
        /* Did we already fire up gdb? If so, just hit the breakpoint. */
        static bool volatile s_fAlreadyLaunchedGdb = false;
        if (ASMAtomicUoReadBool(&s_fAlreadyLaunchedGdb))
            return true;

        /* Try find a suitable terminal program. */
        const char *pszTerm = RTEnvGet("VBOX_ASSERT_TERM");
        if (    !pszTerm
            ||  !RTPathExists(pszTerm))
        {
            pszTerm = "/usr/bin/gnome-terminal";
            if (!RTPathExists(pszTerm))
            {
                pszTerm = "/usr/X11R6/bin/xterm";
                if (!RTPathExists(pszTerm))
                {
                    pszTerm ="/usr/bin/xterm";
                    if (!RTPathExists(pszTerm))
                        return true;
                }
            }
        }

        /* And find gdb. */
        const char *pszGdb = RTEnvGet("VBOX_ASSERT_GDB");
        if (    !pszGdb
            ||  !RTPathExists(pszGdb))
        {
            pszGdb = "/usr/bin/gdb";
            if (!RTPathExists(pszGdb))
                pszGdb = "gdb";
        }

        /* Try spawn the process. */
        char    szCmd[512];
        size_t  cch = RTStrPrintf(szCmd, sizeof(szCmd), "%s -p %d ", pszGdb, RTProcSelf());
        if (cch < sizeof(szCmd))
        {
            char *pszExecName = &szCmd[cch];
            if (!RTProcGetExecutablePath(pszExecName, sizeof(szCmd) - cch))
                *pszExecName = '\0';
        }
        const char *apszArgs[] =
        {
            pszTerm,
            "-e",
            szCmd,
            NULL
        };
        RTPROCESS Process;
        int rc = RTProcCreate(apszArgs[0], &apszArgs[0], RTENV_DEFAULT, 0, &Process);
        if (RT_FAILURE(rc))
            return false;

        ASMAtomicWriteBool(&s_fAlreadyLaunchedGdb, true);

        /* Wait for gdb to attach. */
        RTThreadSleep(15000);
        return true;
    }
#endif

    /* '*' - don't hit the breakpoint. */
    return false;
}


RTDECL(bool) RTAssertShouldPanic(void)
{
    /*
     * Check if panicing is excluded by the RTAssert settings first.
     */
    if (!RTAssertMayPanic())
        return false;

    /*
     * Preserve error state variables.
     */
    RTERRVARS SavedErrVars;
    RTErrVarsSave(&SavedErrVars);

    bool fRc = rtAssertShouldPanicWorker();

    RTErrVarsRestore(&SavedErrVars);
    return fRc;
}

