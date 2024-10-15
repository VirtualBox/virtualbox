/* $Id: init.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Init Ring-3.
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
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/types.h>                 /* darwin: UINT32_C and others. */

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#else
# include <unistd.h>
# ifndef RT_OS_OS2
#  include <pthread.h>
#  include <signal.h>
#  include <errno.h>
#  define IPRT_USE_SIG_CHILD_DUMMY
# endif
#endif
#ifdef RT_OS_OS2
# include <InnoTekLIBC/fork.h>
# define INCL_DOSMISC
# include <os2.h>
#endif
#ifndef IPRT_NO_CRT
# include <locale.h>
#endif

#include <iprt/initterm.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/time.h>
#include <iprt/string.h>
#include <iprt/param.h>
#ifdef RT_OS_WINDOWS
# include <iprt/getopt.h>
# include <iprt/utf16.h>
#endif
#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
# include <iprt/file.h>
# include <VBox/sup.h>
#endif
#include <stdlib.h>

#include "init.h"
#include "internal/alignmentchecks.h"
#include "internal/initterm.h"
#include "internal/path.h"
#include "internal/process.h"
#include "internal/system.h"
#include "internal/thread.h"
#include "internal/time.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The IPRT init flags. */
static uint32_t             g_fInitFlags;

/** The argument count of the program.  */
static int                  g_crtArgs = -1;
/** The arguments of the program (UTF-8).  This is "leaked". */
static char **              g_papszrtArgs;
/** The original argument vector of the program. */
static char **              g_papszrtOrgArgs;

#ifdef IPRT_WITH_ALIGNMENT_CHECKS
/**
 * Whether alignment checks are enabled.
 * This is set if the environment variable IPRT_ALIGNMENT_CHECKS is 1.
 */
RTDATADECL(bool) g_fRTAlignmentChecks = false;
#endif


#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD) || defined(RT_OS_HAIKU) \
 || defined(RT_OS_LINUX)  || defined(RT_OS_OS2)     || defined(RT_OS_SOLARIS) /** @todo add host init hooks everywhere. */
/* Stubs */
DECLHIDDEN(int)  rtR3InitNativeFirst(uint32_t fFlags)     { RT_NOREF_PV(fFlags); return VINF_SUCCESS; }
DECLHIDDEN(int)  rtR3InitNativeFinal(uint32_t fFlags)     { RT_NOREF_PV(fFlags); return VINF_SUCCESS; }
DECLHIDDEN(void) rtR3InitNativeObtrusive(uint32_t fFlags) { RT_NOREF_PV(fFlags); }
#endif


/**
 * atexit callback.
 *
 * This makes sure any loggers are flushed and will later also work the
 * termination callback chain.
 */
static void rtR3ExitCallback(void) RT_NOTHROW_DEF
{
    ASMAtomicWriteBool(&g_frtAtExitCalled, true);

    if (g_crtR3Users > 0)
    {
        PRTLOGGER pLogger = RTLogGetDefaultInstance();
        if (pLogger)
            RTLogFlush(pLogger);

        pLogger = RTLogRelGetDefaultInstance();
        if (pLogger)
            RTLogFlush(pLogger);
    }
}


#ifndef RT_OS_WINDOWS
/**
 * Fork callback, child context.
 */
static void rtR3ForkChildCallback(void)
{
    g_ProcessSelf = getpid();
}
#endif /* RT_OS_WINDOWS */

#ifdef RT_OS_OS2
/** Fork completion callback for OS/2.  Only called in the child. */
static void rtR3ForkOs2ChildCompletionCallback(void *pvArg, int rc, __LIBC_FORKCTX enmCtx)
{
    Assert(enmCtx == __LIBC_FORK_CTX_CHILD); NOREF(enmCtx);
    NOREF(pvArg);

    if (!rc)
        rtR3ForkChildCallback();
}

/** Low-level fork callback for OS/2.  */
int rtR3ForkOs2Child(__LIBC_PFORKHANDLE pForkHandle, __LIBC_FORKOP enmOperation)
{
    if (enmOperation == __LIBC_FORK_OP_EXEC_CHILD)
        return pForkHandle->pfnCompletionCallback(pForkHandle, rtR3ForkOs2ChildCompletionCallback, NULL, __LIBC_FORK_CTX_CHILD);
    return 0;
}

# define static static volatile /** @todo _FORK_CHILD1 causes unresolved externals in optimized builds. Fix macro. */
_FORK_CHILD1(0, rtR3ForkOs2Child);
# undef static
#endif /* RT_OS_OS2 */



/**
 * Internal worker which initializes or re-initializes the
 * program path, name and directory globals.
 *
 * @returns IPRT status code.
 * @param   pszProgramPath  The program path, NULL if not specified.
 */
static int rtR3InitProgramPath(const char *pszProgramPath)
{
    /*
     * We're reserving 32 bytes here for file names as what not.
     */
    if (!pszProgramPath)
    {
        int rc = rtProcInitExePath(g_szrtProcExePath, sizeof(g_szrtProcExePath) - 32);
        if (RT_FAILURE(rc))
            return rc;
    }
    else
    {
        size_t cch = strlen(pszProgramPath);
        Assert(cch > 1);
        AssertMsgReturn(cch < sizeof(g_szrtProcExePath) - 32, ("%zu\n", cch), VERR_BUFFER_OVERFLOW);
        memcpy(g_szrtProcExePath, pszProgramPath, cch + 1);
    }

    /*
     * Parse the name.
     */
    ssize_t offName;
    g_cchrtProcExePath = RTPathParseSimple(g_szrtProcExePath, &g_cchrtProcExeDir, &offName, NULL);
    g_offrtProcName = offName;
    return VINF_SUCCESS;
}


#ifdef RT_OS_WINDOWS
/**
 * Checks the two argument vectors contains the same strings.
 */
DECLINLINE(bool) rtR3InitArgvEquals(int cArgs, char **papszArgs1, char **papszArgs2)
{
    if (papszArgs1 != papszArgs2)
        while (cArgs-- > 0)
            if (strcmp(papszArgs1[cArgs], papszArgs2[cArgs]) != 0)
                return false;
    return true;
}
#endif


/**
 * Internal worker which initializes or re-initializes the
 * program path, name and directory globals.
 *
 * @returns IPRT status code.
 * @param   fFlags          Flags, see RTR3INIT_XXX.
 * @param   cArgs           Pointer to the argument count.
 * @param   ppapszArgs      Pointer to the argument vector pointer. NULL
 *                          allowed if @a cArgs is 0.
 */
static int rtR3InitArgv(uint32_t fFlags, int cArgs, char ***ppapszArgs)
{
    NOREF(fFlags);
    if (cArgs)
    {
        AssertPtr(ppapszArgs);
        AssertPtr(*ppapszArgs);
        char **papszOrgArgs = *ppapszArgs;

        /*
         * Normally we should only be asked to convert arguments once.  If we
         * are though, it should be the already convered arguments.
         */
        if (g_crtArgs != -1)
        {
            AssertReturn(   g_crtArgs == cArgs
                         && g_papszrtArgs == papszOrgArgs,
                         VERR_WRONG_ORDER); /* only init once! */
            return VINF_SUCCESS;
        }

#if !defined(IPRT_NO_CRT) || !defined(RT_OS_WINDOWS)
        if (!(fFlags & RTR3INIT_FLAGS_UTF8_ARGV))
        {
            /*
             * Convert the arguments.
             */
            char **papszArgs;

# ifdef RT_OS_WINDOWS
            /* HACK ALERT! Try convert from unicode versions if possible.
               Unfortunately for us, __wargv is only initialized if we have a unicode
               main function.  So, use getoptarv.cpp code to do the conversions and
               hope it gives us the same result. (CommandLineToArgvW was not in NT 3.1.) */
            if (   cArgs == __argc
                && rtR3InitArgvEquals(cArgs, papszOrgArgs, __argv))
            {
                char *pszCmdLine = NULL;
                int rc = RTUtf16ToUtf8Tag(GetCommandLineW(), &pszCmdLine, "will-leak:rtR3InitArgv");
                AssertRCReturn(rc, rc);

                int cArgsFromCmdLine = -1;
                rc = RTGetOptArgvFromString(&papszArgs, &cArgsFromCmdLine, pszCmdLine,
                                            RTGETOPTARGV_CNV_QUOTE_MS_CRT | RTGETOPTARGV_CNV_MODIFY_INPUT, NULL);
                AssertMsgRCReturn(rc, ("pszCmdLine='%s' rc=%Rrc\n", pszCmdLine, rc), rc);
                AssertMsg(cArgsFromCmdLine == cArgs,
                          ("cArgsFromCmdLine=%d cArgs=%d pszCmdLine='%s' rc=%Rrc\n", cArgsFromCmdLine, cArgs, pszCmdLine));
            }
            else
# endif
            {
                papszArgs = (char **)RTMemAllocZTag((cArgs + 1) * sizeof(char *), "will-leak:rtR3InitArgv");
                if (!papszArgs)
                    return VERR_NO_MEMORY;

                for (int i = 0; i < cArgs; i++)
                {
                    int rc = RTStrCurrentCPToUtf8(&papszArgs[i], papszOrgArgs[i]);
                    if (RT_FAILURE(rc))
                    {
                        while (i--)
                            RTStrFree(papszArgs[i]);
                        RTMemFree(papszArgs);
                        return rc;
                    }
                }
            }

            papszArgs[cArgs] = NULL;

            g_papszrtOrgArgs = papszOrgArgs;
            g_papszrtArgs    = papszArgs;
            g_crtArgs        = cArgs;

            *ppapszArgs = papszArgs;
        }
        else
#endif /* !IPRT_NO_CRT || !RT_OS_WINDOWS */
        {
            /*
             * The arguments are already UTF-8, no conversion needed.
             */
            g_papszrtOrgArgs = papszOrgArgs;
            g_papszrtArgs    = papszOrgArgs;
            g_crtArgs        = cArgs;
        }
    }

    return VINF_SUCCESS;
}


#ifdef IPRT_USE_SIG_CHILD_DUMMY
/**
 * Dummy SIGCHILD handler.
 *
 * Assigned on rtR3Init only when SIGCHILD handler is set SIGIGN or SIGDEF to
 * ensure waitpid works properly for the terminated processes.
 */
static void rtR3SigChildHandler(int iSignal)
{
    NOREF(iSignal);
}
#endif /* IPRT_USE_SIG_CHILD_DUMMY */


/**
 * rtR3Init worker.
 */
static int rtR3InitBody(uint32_t fFlags, int cArgs, char ***ppapszArgs, const char *pszProgramPath)
{
    /*
     * Early native initialization.
     */
    int rc = rtR3InitNativeFirst(fFlags);
    AssertMsgRCReturn(rc, ("rtR3InitNativeFirst failed with %Rrc\n", rc), rc);

    /*
     * Disable error popups.
     */
#if defined(RT_OS_OS2) /** @todo move to private code. */
    DosError(FERR_DISABLEHARDERR);
#endif

#ifndef IPRT_NO_CRT
    /*
     * Init C runtime locale before we do anything that may end up converting
     * paths or we'll end up using the "C" locale for path conversion.
     */
    setlocale(LC_CTYPE, "");
#endif

    /*
     * The Process ID.
     */
#ifdef _MSC_VER
    g_ProcessSelf = GetCurrentProcessId(); /* since NT 3.1, not 3.51+ as listed on geoffchappell.com */
#else
    g_ProcessSelf = getpid();
#endif

    /*
     * Save the init flags.
     */
    g_fInitFlags |= fFlags;

#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
# ifdef VBOX
    /*
     * This MUST be done as the very first thing, before any file is opened.
     * The log is opened on demand, but the first log entries may be caused
     * by rtThreadInit() below.
     */
    const char *pszDisableHostCache = getenv("VBOX_DISABLE_HOST_DISK_CACHE");
    if (    pszDisableHostCache != NULL
        &&  *pszDisableHostCache
        &&  strcmp(pszDisableHostCache, "0") != 0)
    {
        RTFileSetForceFlags(RTFILE_O_WRITE, RTFILE_O_WRITE_THROUGH, 0);
        RTFileSetForceFlags(RTFILE_O_READWRITE, RTFILE_O_WRITE_THROUGH, 0);
    }
# endif  /* VBOX */
#endif /* !IN_GUEST && !RT_NO_GIP */

#if defined(RT_ARCH_ARM64) && defined(RT_OS_LINUX)
    /*
     * Initialize the page size.
     */
    rc = rtSystemInitPageSize();
    AssertMsgRCReturn(rc, ("Failed to initialize the page size, rc=%Rrc!\n", rc), rc);
#endif

    /*
     * Thread Thread database and adopt the caller thread as 'main'.
     * This must be done before everything else or else we'll call into threading
     * without having initialized TLS entries and suchlike.
     */
    rc = rtThreadInit();
    AssertMsgRCReturn(rc, ("Failed to initialize threads, rc=%Rrc!\n", rc), rc);

    /*
     * The executable path before SUPLib (windows requirement).
     */
    rc = rtR3InitProgramPath(pszProgramPath);
    AssertLogRelMsgRCReturn(rc, ("Failed to get executable directory path, rc=%Rrc!\n", rc), rc);

#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
    /*
     * Initialize SUPLib here so the GIP can get going as early as possible
     * (improves accuracy for the first client).
     */
    if (fFlags & (RTR3INIT_FLAGS_SUPLIB | RTR3INIT_FLAGS_TRY_SUPLIB))
    {
        if (!(fFlags & ((SUPR3INIT_F_UNRESTRICTED | SUPR3INIT_F_LIMITED) << RTR3INIT_FLAGS_SUPLIB_SHIFT)))
            g_fInitFlags |= fFlags |= SUPR3INIT_F_UNRESTRICTED << RTR3INIT_FLAGS_SUPLIB_SHIFT;
        rc = SUPR3InitEx(fFlags >> RTR3INIT_FLAGS_SUPLIB_SHIFT, NULL /*ppSession*/);
        AssertMsgReturn(RT_SUCCESS(rc) || (fFlags & RTR3INIT_FLAGS_TRY_SUPLIB),
                        ("Failed to initialize the support library, rc=%Rrc!\n", rc), rc);
    }
#endif

    /*
     * Convert arguments.
     */
    rc = rtR3InitArgv(fFlags, cArgs, ppapszArgs);
    AssertLogRelMsgRCReturn(rc, ("Failed to convert the arguments, rc=%Rrc!\n", rc), rc);

#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
    /*
     * The threading is initialized, so we can safely sleep a bit if GIP
     * needs some time to start updating itself.  Currently limited to
     * the first mapping of GIP (u32TransactionId <= 4), quite possible we
     * could just ditch this now.
     */
    /** @todo consider dropping this... */
    PSUPGLOBALINFOPAGE pGip;
    if (   (fFlags & (RTR3INIT_FLAGS_SUPLIB | RTR3INIT_FLAGS_TRY_SUPLIB))
        && (pGip = g_pSUPGlobalInfoPage) != NULL
        && pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC)
    {
        PSUPGIPCPU pGipCpu = SUPGetGipCpuPtr(pGip);
        if (   pGipCpu
            && pGipCpu->u32TransactionId <= 4)
        {
            RTThreadSleep(pGip->u32UpdateIntervalNS / RT_NS_1MS + 2);
            RTTimeNanoTS();
        }
    }
#endif

    /*
     * Init the program start timestamp TS.
     * Do that here to be sure that the GIP time was properly updated the 1st time.
     */
    g_u64ProgramStartNanoTS = RTTimeNanoTS();

    /*
     * The remainder cannot easily be undone, so it has to go last.
     */

    /* Fork and exit callbacks. */
#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
    rc = pthread_atfork(NULL, NULL, rtR3ForkChildCallback);
    AssertMsg(rc == 0, ("%d\n", rc));
#endif
    atexit(rtR3ExitCallback);

#ifdef IPRT_USE_SIG_CHILD_DUMMY
    /*
     * SIGCHLD must not be ignored (that's default), otherwise posix compliant waitpid
     * implementations won't work right.
     */
    for (;;)
    {
        struct sigaction saOld;
        rc = sigaction(SIGCHLD, 0, &saOld);         AssertMsg(rc == 0, ("%d/%d\n", rc, errno));
        if (    rc != 0
            ||  (saOld.sa_flags & SA_SIGINFO)
            || (   saOld.sa_handler != SIG_IGN
                && saOld.sa_handler != SIG_DFL)
           )
            break;

        /* Try install dummy handler. */
        struct sigaction saNew = saOld;
        saNew.sa_flags   = SA_NOCLDSTOP | SA_RESTART;
        saNew.sa_handler = rtR3SigChildHandler;
        rc = sigemptyset(&saNew.sa_mask);           AssertMsg(rc == 0, ("%d/%d\n", rc, errno));
        struct sigaction saOld2;
        rc = sigaction(SIGCHLD, &saNew, &saOld2);   AssertMsg(rc == 0, ("%d/%d\n", rc, errno));
        if (    rc != 0
            ||  (   saOld2.sa_handler == saOld.sa_handler
                 && !(saOld2.sa_flags & SA_SIGINFO))
           )
            break;

        /* Race during dynamic load, restore and try again... */
        sigaction(SIGCHLD, &saOld2, NULL);
        RTThreadYield();
    }
#endif /* IPRT_USE_SIG_CHILD_DUMMY */

#ifdef IPRT_WITH_ALIGNMENT_CHECKS
    /*
     * Enable alignment checks.
     */
    const char *pszAlignmentChecks = RTEnvGet("IPRT_ALIGNMENT_CHECKS"); /** @todo add RTEnvGetBool */
    g_fRTAlignmentChecks = pszAlignmentChecks != NULL
                        && pszAlignmentChecks[0] == '1'
                        && pszAlignmentChecks[1] == '\0';
    if (g_fRTAlignmentChecks)
        IPRT_ALIGNMENT_CHECKS_ENABLE();
#endif

    /*
     * Final native initialization.
     */
    rc = rtR3InitNativeFinal(fFlags);
    AssertMsgRCReturn(rc, ("rtR3InitNativeFinal failed with %Rrc\n", rc), rc);

    return VINF_SUCCESS;
}


/**
 * Internal initialization worker.
 *
 * @returns IPRT status code.
 * @param   fFlags          Flags, see RTR3INIT_XXX.
 * @param   cArgs           Pointer to the argument count.
 * @param   ppapszArgs      Pointer to the argument vector pointer. NULL
 *                          allowed if @a cArgs is 0.
 * @param   pszProgramPath  The program path.  Pass NULL if we're to figure it
 *                          out ourselves.
 */
static int rtR3Init(uint32_t fFlags, int cArgs, char ***ppapszArgs, const char *pszProgramPath)
{
    /* no entry log flow, because prefixes and thread may freak out. */
    Assert(!(fFlags & ~RTR3INIT_FLAGS_VALID_MASK));
    Assert(!(fFlags & RTR3INIT_FLAGS_DLL) || cArgs == 0);

    /*
     * Do reference counting, only initialize the first time around.
     *
     * We are ASSUMING that nobody will be able to race RTR3Init* calls when the
     * first one, the real init, is running (second assertion).
     */
    int32_t cUsers = ASMAtomicIncS32(&g_crtR3Users);
    if (cUsers != 1)
    {
        AssertMsg(cUsers > 1, ("%d\n", cUsers));
        Assert(!g_frtR3Initializing);

#if !defined(IN_GUEST) && !defined(RT_NO_GIP)
        /* Initialize the support library if requested. We've always ignored the
           status code here for some reason, making the two flags same. */
        if (fFlags & (RTR3INIT_FLAGS_SUPLIB | RTR3INIT_FLAGS_TRY_SUPLIB))
        {
            if (!(fFlags & ((SUPR3INIT_F_UNRESTRICTED | SUPR3INIT_F_LIMITED) << RTR3INIT_FLAGS_SUPLIB_SHIFT)))
                fFlags |= SUPR3INIT_F_UNRESTRICTED << RTR3INIT_FLAGS_SUPLIB_SHIFT;
            SUPR3InitEx(fFlags >> RTR3INIT_FLAGS_SUPLIB_SHIFT, NULL /*ppSession*/);
            g_fInitFlags |= fFlags & (RTR3INIT_FLAGS_SUPLIB | RTR3INIT_FLAGS_TRY_SUPLIB | RTR3INIT_FLAGS_SUPLIB_MASK);
        }
#endif
        g_fInitFlags |= fFlags & RTR3INIT_FLAGS_UTF8_ARGV;

        if (   !(fFlags      & RTR3INIT_FLAGS_UNOBTRUSIVE)
            && (g_fInitFlags & RTR3INIT_FLAGS_UNOBTRUSIVE))
        {
            g_fInitFlags &= ~RTR3INIT_FLAGS_UNOBTRUSIVE;
            g_fInitFlags |= fFlags & RTR3INIT_FLAGS_STANDALONE_APP;
            rtR3InitNativeObtrusive(g_fInitFlags | fFlags);
            rtThreadReInitObtrusive();
        }
        else
            Assert(!(fFlags & RTR3INIT_FLAGS_STANDALONE_APP) || (g_fInitFlags & RTR3INIT_FLAGS_STANDALONE_APP));

        int rc = VINF_SUCCESS;
        if (pszProgramPath)
            rc = rtR3InitProgramPath(pszProgramPath);
        if (RT_SUCCESS(rc))
            rc = rtR3InitArgv(fFlags, cArgs, ppapszArgs);
        return rc;
    }

    /*
     * Do the initialization.
     */
    ASMAtomicWriteBool(&g_frtR3Initializing, true);
    int rc = rtR3InitBody(fFlags, cArgs, ppapszArgs, pszProgramPath);
    ASMAtomicWriteBool(&g_frtR3Initializing, false);
    if (RT_FAILURE(rc))
    {
        /* failure */
        ASMAtomicDecS32(&g_crtR3Users);
        return rc;
    }

    /* success */
    LogFlow(("rtR3Init: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


RTR3DECL(int) RTR3InitExe(int cArgs, char ***ppapszArgs, uint32_t fFlags)
{
    Assert(!(fFlags & RTR3INIT_FLAGS_DLL));
    return rtR3Init(fFlags, cArgs, ppapszArgs, NULL);
}


RTR3DECL(int) RTR3InitExeNoArguments(uint32_t fFlags)
{
    Assert(!(fFlags & RTR3INIT_FLAGS_DLL));
    return rtR3Init(fFlags, 0, NULL, NULL);
}


RTR3DECL(int) RTR3InitDll(uint32_t fFlags)
{
    Assert(!(fFlags & RTR3INIT_FLAGS_DLL));
    return rtR3Init(fFlags | RTR3INIT_FLAGS_DLL, 0, NULL, NULL);
}


RTR3DECL(int) RTR3InitEx(uint32_t iVersion, uint32_t fFlags, int cArgs, char ***ppapszArgs, const char *pszProgramPath)
{
    AssertReturn(iVersion == RTR3INIT_VER_CUR, VERR_NOT_SUPPORTED);
    return rtR3Init(fFlags, cArgs, ppapszArgs, pszProgramPath);
}


RTR3DECL(bool) RTR3InitIsInitialized(void)
{
    return g_crtR3Users >= 1 && !g_frtR3Initializing;
}


RTR3DECL(bool) RTR3InitIsUnobtrusive(void)
{
    return RT_BOOL(g_fInitFlags & RTR3INIT_FLAGS_UNOBTRUSIVE);
}


#if 0 /** @todo implement RTR3Term. */
RTR3DECL(void) RTR3Term(void)
{
}
#endif

