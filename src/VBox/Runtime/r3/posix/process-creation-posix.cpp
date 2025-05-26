/* $Id: process-creation-posix.cpp 169006 2025-05-26 22:58:20Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Process Creation, POSIX.
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
#define LOG_GROUP RTLOGGROUP_PROCESS
#include <iprt/cdefs.h>
#ifdef RT_OS_LINUX
# define IPRT_WITH_DYNAMIC_CRYPT_R
#endif
#if (defined(RT_OS_LINUX) || defined(RT_OS_OS2)) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE
#endif
#if defined(RT_OS_LINUX) && !defined(_XOPEN_SOURCE)
# define _XOPEN_SOURCE 700 /* for newlocale */
#endif

#ifdef RT_OS_OS2
# define crypt   unistd_crypt
# define setkey  unistd_setkey
# define encrypt unistd_encrypt
# include <unistd.h>
# undef  crypt
# undef  setkey
# undef  encrypt
#else
# include <unistd.h>
#endif
#include <stdlib.h>
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <grp.h>
#include <pwd.h>
#if defined(RT_OS_LINUX) || defined(RT_OS_OS2) || defined(RT_OS_SOLARIS)
# include <crypt.h>
#endif
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
# include <shadow.h>
#endif
#ifdef RT_OS_LINUX
# include <sys/syscall.h>
#endif
#if defined(RT_OS_DARWIN)
# include <xlocale.h> /* for newlocale() */
#endif

#if defined(RT_OS_LINUX) || defined(RT_OS_OS2)
/* While Solaris has posix_spawn() of course we don't want to use it as
 * we need to have the child in a different process contract, no matter
 * whether it is started detached or not. */
# define HAVE_POSIX_SPAWN 1
#endif
#if defined(RT_OS_DARWIN) && defined(MAC_OS_X_VERSION_MIN_REQUIRED)
# if MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
#  define HAVE_POSIX_SPAWN 1
# endif
#endif
#ifdef HAVE_POSIX_SPAWN
# include <spawn.h>
#endif

#if !defined(IPRT_USE_PAM) \
 && !defined(IPRT_WITHOUT_PAM) \
 && ( defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD) || defined(RT_OS_LINUX) || defined(RT_OS_NETBSD) || defined(RT_OS_OPENBSD) || defined(RT_OS_SOLARIS) )
# define IPRT_USE_PAM
#endif
#ifdef IPRT_USE_PAM
# include <security/pam_appl.h>
# include <stdlib.h>
# include <dlfcn.h>
# include <iprt/asm.h>
#endif

#ifdef RT_OS_SOLARIS
# include <limits.h>
# include <sys/ctfs.h>
# include <sys/contract/process.h>
# include <libcontract.h>
#endif

#ifndef RT_OS_SOLARIS
# include <paths.h>
#else
# define _PATH_MAILDIR "/var/mail"
# define _PATH_DEFPATH "/usr/bin:/bin"
# define _PATH_STDPATH "/sbin:/usr/sbin:/bin:/usr/bin"
#endif
#ifndef _PATH_BSHELL
# define _PATH_BSHELL "/bin/sh"
#endif


#include <iprt/process.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/file.h>
#if defined(IPRT_WITH_DYNAMIC_CRYPT_R) || defined(IPRT_USE_PAM)
# include <iprt/ldr.h>
#endif
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/socket.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include "internal/process.h"
#include "internal/path.h"
#include "internal/string.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef IPRT_USE_PAM
/*
 * The PAM library names and version ranges to try.
 */
# ifdef RT_OS_DARWIN
#  include <mach-o/dyld.h>
/** @node libpam.2.dylib was introduced with 10.6.x (OpenPAM); we use
 *        libpam.dylib as that's a symlink to the latest and greatest. */
#  define IPRT_LIBPAM_FILE_1            "libpam.dylib"
#  define IPRT_LIBPAM_FILE_1_FIRST_VER 0
#  define IPRT_LIBPAM_FILE_1_END_VER   0
#  define IPRT_LIBPAM_FILE_2            "libpam.2.dylib"
#  define IPRT_LIBPAM_FILE_2_FIRST_VER 0
#  define IPRT_LIBPAM_FILE_2_END_VER   0
#  define IPRT_LIBPAM_FILE_3            "libpam.1.dylib"
#  define IPRT_LIBPAM_FILE_3_FIRST_VER 0
#  define IPRT_LIBPAM_FILE_3_END_VER   0
# elif RT_OS_LINUX
#  define IPRT_LIBPAM_FILE_1           "libpam.so.0"
#  define IPRT_LIBPAM_FILE_1_FIRST_VER 0
#  define IPRT_LIBPAM_FILE_1_END_VER   0
#  define IPRT_LIBPAM_FILE_2           "libpam.so"
#  define IPRT_LIBPAM_FILE_2_FIRST_VER 16
#  define IPRT_LIBPAM_FILE_2_END_VER   1
# else
#  define IPRT_LIBPAM_FILE_1           "libpam.so"
#  define IPRT_LIBPAM_FILE_1_FIRST_VER 16
#  define IPRT_LIBPAM_FILE_1_END_VER   0
# endif
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#ifdef IPRT_USE_PAM
/** For passing info between rtCheckCredentials and rtPamConv. */
typedef struct RTPROCPAMARGS
{
    const char *pszUser;
    const char *pszPassword;
} RTPROCPAMARGS;
/** Pointer to rtPamConv argument package. */
typedef RTPROCPAMARGS *PRTPROCPAMARGS;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Environment dump marker used with CSH.   */
static const char g_szEnvMarkerBegin[] = "IPRT_EnvEnvEnv_Begin_EnvEnvEnv";
/** Environment dump marker used with CSH.   */
static const char g_szEnvMarkerEnd[]   = "IPRT_EnvEnvEnv_End_EnvEnvEnv";


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtProcPosixCreateInner(const char *pszExec, const char * const *papszArgs, RTENV hEnv, RTENV hEnvToUse,
                                  uint32_t fFlags, const char *pszAsUser, uid_t uid, gid_t gid,
                                  unsigned cRedirFds, int *paRedirFds, void *pvExtraData, PRTPROCESS phProcess);


#ifdef IPRT_USE_PAM
/**
 * Worker for rtCheckCredentials that feeds password and maybe username to PAM.
 *
 * @returns PAM status.
 * @param   cMessages       Number of messages.
 * @param   papMessages     Message vector.
 * @param   ppaResponses    Where to put our responses.
 * @param   pvAppData       Pointer to RTPROCPAMARGS.
 */
#if defined(RT_OS_SOLARIS)
static int rtPamConv(int cMessages, struct pam_message **papMessages, struct pam_response **ppaResponses, void *pvAppData)
#else
static int rtPamConv(int cMessages, const struct pam_message **papMessages, struct pam_response **ppaResponses, void *pvAppData)
#endif
{
    LogFlow(("rtPamConv: cMessages=%d\n", cMessages));
    PRTPROCPAMARGS pArgs = (PRTPROCPAMARGS)pvAppData;
    AssertPtrReturn(pArgs, PAM_CONV_ERR);

    struct pam_response *paResponses = (struct pam_response *)calloc(cMessages, sizeof(paResponses[0]));
    AssertReturn(paResponses,  PAM_CONV_ERR);
    for (int i = 0; i < cMessages; i++)
    {
        LogFlow(("rtPamConv: #%d: msg_style=%d msg=%s\n", i, papMessages[i]->msg_style, papMessages[i]->msg));

        paResponses[i].resp_retcode = 0;
        if (papMessages[i]->msg_style == PAM_PROMPT_ECHO_OFF)
            paResponses[i].resp = strdup(pArgs->pszPassword);
        else if (papMessages[i]->msg_style == PAM_PROMPT_ECHO_ON)
            paResponses[i].resp = strdup(pArgs->pszUser);
        else
        {
            paResponses[i].resp = NULL;
            continue;
        }
        if (paResponses[i].resp == NULL)
        {
            while (i-- > 0)
                free(paResponses[i].resp);
            free(paResponses);
            LogFlow(("rtPamConv: out of memory\n"));
            return PAM_CONV_ERR;
        }
    }

    *ppaResponses = paResponses;
    return PAM_SUCCESS;
}


/**
 * Common PAM driver for rtCheckCredentials and the case where pszAsUser is NULL
 * but RTPROC_FLAGS_PROFILE is set.
 *
 * @returns IPRT status code.
 * @param   pszPamService   The PAM service to use for the run.
 * @param   pszUser         The user.
 * @param   pszPassword     The password.
 * @param   ppapszEnv       Where to return PAM environment variables, NULL is
 *                          fine if no variables to return. Call
 *                          rtProcPosixFreePamEnv to free.  Optional, so NULL
 *                          can be passed in.
 * @param   pfMayFallBack   Where to return whether a fallback to crypt is
 *                          acceptable or if the failure result is due to
 *                          authentication failing.  Optional.
 */
static int rtProcPosixAuthenticateUsingPam(const char *pszPamService, const char *pszUser, const char *pszPassword,
                                           char ***ppapszEnv, bool *pfMayFallBack)
{
    if (pfMayFallBack)
        *pfMayFallBack = true;

    /*
     * Dynamically load pam the first time we go thru here.
     */
    static int     (*s_pfnPamStart)(const char *, const char *, struct pam_conv *, pam_handle_t **);
    static int     (*s_pfnPamAuthenticate)(pam_handle_t *, int);
    static int     (*s_pfnPamAcctMgmt)(pam_handle_t *, int);
    static int     (*s_pfnPamSetItem)(pam_handle_t *, int, const void *);
    static int     (*s_pfnPamSetCred)(pam_handle_t *, int);
    static char ** (*s_pfnPamGetEnvList)(pam_handle_t *);
    static int     (*s_pfnPamOpenSession)(pam_handle_t *, int);
    static int     (*s_pfnPamCloseSession)(pam_handle_t *, int);
    static int     (*s_pfnPamEnd)(pam_handle_t *, int);
    if (   s_pfnPamStart == NULL
        || s_pfnPamAuthenticate == NULL
        || s_pfnPamAcctMgmt == NULL
        || s_pfnPamSetItem == NULL
        || s_pfnPamEnd == NULL)
    {
        RTLDRMOD hModPam = NIL_RTLDRMOD;
        const char *pszLast;
        int rc = RTLdrLoadSystemEx(pszLast = IPRT_LIBPAM_FILE_1, RTLDRLOAD_FLAGS_GLOBAL | RTLDRLOAD_FLAGS_NO_UNLOAD
                                   | RTLDRLOAD_FLAGS_SO_VER_RANGE(IPRT_LIBPAM_FILE_1_FIRST_VER, IPRT_LIBPAM_FILE_1_END_VER),
                                   &hModPam);
# ifdef IPRT_LIBPAM_FILE_2
        if (RT_FAILURE(rc))
            rc = RTLdrLoadSystemEx(pszLast = IPRT_LIBPAM_FILE_2, RTLDRLOAD_FLAGS_GLOBAL | RTLDRLOAD_FLAGS_NO_UNLOAD
                                   | RTLDRLOAD_FLAGS_SO_VER_RANGE(IPRT_LIBPAM_FILE_2_FIRST_VER, IPRT_LIBPAM_FILE_2_END_VER),
                                   &hModPam);
# endif
# ifdef IPRT_LIBPAM_FILE_3
        if (RT_FAILURE(rc))
            rc = RTLdrLoadSystemEx(pszLast = IPRT_LIBPAM_FILE_3, RTLDRLOAD_FLAGS_GLOBAL | RTLDRLOAD_FLAGS_NO_UNLOAD
                                   | RTLDRLOAD_FLAGS_SO_VER_RANGE(IPRT_LIBPAM_FILE_3_FIRST_VER, IPRT_LIBPAM_FILE_3_END_VER),
                                   &hModPam);
# endif
# ifdef RT_OS_DARWIN
        /* Try absolute paths on Darwin (if SIP is enabled). */
        if (RT_FAILURE(rc))
        {
#  define MAKE_ABSOLUTE(a_Lib) "/usr/lib" # a_Lib
            rc = RTLdrLoadEx(pszLast = MAKE_ABSOLUTE(IPRT_LIBPAM_FILE_1), &hModPam, RTLDRLOAD_FLAGS_GLOBAL | RTLDRLOAD_FLAGS_NO_UNLOAD
                             | RTLDRLOAD_FLAGS_SO_VER_RANGE(IPRT_LIBPAM_FILE_1_FIRST_VER, IPRT_LIBPAM_FILE_1_END_VER),
                             NULL);
#  ifdef IPRT_LIBPAM_FILE_2
            if (RT_FAILURE(rc))
                rc = RTLdrLoadEx(pszLast = MAKE_ABSOLUTE(IPRT_LIBPAM_FILE_2), &hModPam, RTLDRLOAD_FLAGS_GLOBAL | RTLDRLOAD_FLAGS_NO_UNLOAD
                                 | RTLDRLOAD_FLAGS_SO_VER_RANGE(IPRT_LIBPAM_FILE_2_FIRST_VER, IPRT_LIBPAM_FILE_2_END_VER),
                                 NULL);
#  endif
#  ifdef IPRT_LIBPAM_FILE_3
            if (RT_FAILURE(rc))
                rc = RTLdrLoadEx(pszLast = MAKE_ABSOLUTE(IPRT_LIBPAM_FILE_3), &hModPam, RTLDRLOAD_FLAGS_GLOBAL | RTLDRLOAD_FLAGS_NO_UNLOAD
                                 | RTLDRLOAD_FLAGS_SO_VER_RANGE(IPRT_LIBPAM_FILE_3_FIRST_VER, IPRT_LIBPAM_FILE_3_END_VER),
                                 NULL);
#  endif
#  undef MAKE_ABSOLUTE
        }
# endif /* RT_OS_DARWIN */

        if (RT_FAILURE(rc))
        {
            LogRelMax(10, ("failed to load %s: %Rrc\n", pszLast, rc));
            return VERR_AUTHENTICATION_FAILURE;
        }

        *(uintptr_t *)&s_pfnPamStart        = (uintptr_t)RTLdrGetFunction(hModPam, "pam_start");
        *(uintptr_t *)&s_pfnPamAuthenticate = (uintptr_t)RTLdrGetFunction(hModPam, "pam_authenticate");
        *(uintptr_t *)&s_pfnPamAcctMgmt     = (uintptr_t)RTLdrGetFunction(hModPam, "pam_acct_mgmt");
        *(uintptr_t *)&s_pfnPamSetItem      = (uintptr_t)RTLdrGetFunction(hModPam, "pam_set_item");
        *(uintptr_t *)&s_pfnPamSetCred      = (uintptr_t)RTLdrGetFunction(hModPam, "pam_setcred");
        *(uintptr_t *)&s_pfnPamGetEnvList   = (uintptr_t)RTLdrGetFunction(hModPam, "pam_getenvlist");
        *(uintptr_t *)&s_pfnPamOpenSession  = (uintptr_t)RTLdrGetFunction(hModPam, "pam_open_session");
        *(uintptr_t *)&s_pfnPamCloseSession = (uintptr_t)RTLdrGetFunction(hModPam, "pam_close_session");
        *(uintptr_t *)&s_pfnPamEnd          = (uintptr_t)RTLdrGetFunction(hModPam, "pam_end");
        ASMCompilerBarrier();

        RTLdrClose(hModPam);

        if (   s_pfnPamStart == NULL
            || s_pfnPamAuthenticate == NULL
            || s_pfnPamAcctMgmt == NULL
            || s_pfnPamSetItem == NULL
            || s_pfnPamEnd == NULL)
        {
            LogRelMax(10, ("failed to resolve symbols: %p %p %p %p %p\n",
                           s_pfnPamStart, s_pfnPamAuthenticate, s_pfnPamAcctMgmt, s_pfnPamSetItem, s_pfnPamEnd));
            return VERR_AUTHENTICATION_FAILURE;
        }
    }

# define pam_start           s_pfnPamStart
# define pam_authenticate    s_pfnPamAuthenticate
# define pam_acct_mgmt       s_pfnPamAcctMgmt
# define pam_set_item        s_pfnPamSetItem
# define pam_setcred         s_pfnPamSetCred
# define pam_getenvlist      s_pfnPamGetEnvList
# define pam_open_session    s_pfnPamOpenSession
# define pam_close_session   s_pfnPamCloseSession
# define pam_end             s_pfnPamEnd

    /*
     * Do the PAM stuff.
     */
    pam_handle_t   *hPam        = NULL;
    RTPROCPAMARGS   PamConvArgs = { pszUser, pszPassword };
    struct pam_conv PamConversation;
    RT_ZERO(PamConversation);
    PamConversation.appdata_ptr = &PamConvArgs;
    PamConversation.conv        = rtPamConv;
    int rc = pam_start(pszPamService, pszUser, &PamConversation, &hPam);
    if (rc == PAM_SUCCESS)
    {
        rc = pam_set_item(hPam, PAM_RUSER, pszUser);
        LogRel2(("rtProcPosixAuthenticateUsingPam(%s): pam_setitem/PAM_RUSER: %s\n", pszPamService, pszUser));
        if (rc == PAM_SUCCESS)
        {
            /*
             * Secure TTY fun ahead (for pam_securetty).
             *
             * We need to set PAM_TTY (if available) to make PAM stacks work which
             * require a secure TTY via pam_securetty (Debian 10 + 11, for example). This
             * is typically an issue when launching as 'root'.  See @bugref{10225}.
             *
             * Note! We only can try (or better: guess) to a certain amount, as it really
             *       depends on the distribution or Administrator which has set up the
             *       system which (and how) things are allowed (see /etc/securetty).
             *
             * Note! We don't acctually try or guess anything about the distro like
             *       suggested by the above note, we just try determine the TTY of
             *       the _parent_ process and hope for the best. (bird)
             */
            char szTTY[64];
            int rc2 = RTEnvGetEx(RTENV_DEFAULT, "DISPLAY", szTTY, sizeof(szTTY), NULL);
            if (RT_FAILURE(rc2))
            {
                /* Virtual terminal hint given? */
                static char const s_szPrefix[] = "tty";
                memcpy(szTTY, s_szPrefix, sizeof(s_szPrefix));
                rc2 = RTEnvGetEx(RTENV_DEFAULT, "XDG_VTNR", &szTTY[sizeof(s_szPrefix) - 1], sizeof(s_szPrefix) - 1, NULL);
            }

            /** @todo Should we - distinguished from the login service - also set the hostname as PAM_TTY?
             *        The pam_access and pam_systemd talk about this. Similarly, SSH and cron use "ssh" and "cron" for PAM_TTY
             *        (see PAM_TTY_KLUDGE). */
#ifdef IPRT_WITH_PAM_TTY_KLUDGE
            if (RT_FAILURE(rc2))
                if (!RTStrICmp(pszPamService, "access")) /* Access management needed? */
                {
                    int err = gethostname(szTTY, sizeof(szTTY));
                    if (err == 0)
                        rc2 = VINF_SUCCESS;
                }
#endif
            /* As a last resort, try stdin's TTY name instead (if any). */
            if (RT_FAILURE(rc2))
            {
                rc2 = ttyname_r(0 /*stdin*/, szTTY, sizeof(szTTY));
                if (rc2 != 0)
                    rc2 = RTErrConvertFromErrno(rc2);
            }

            LogRel2(("rtProcPosixAuthenticateUsingPam(%s): pam_setitem/PAM_TTY: %s, rc2=%Rrc\n", pszPamService, szTTY, rc2));
            if (szTTY[0] == '\0')
                LogRel2(("rtProcPosixAuthenticateUsingPam(%s): Hint: Looks like running as a non-interactive user (no TTY/PTY).\n"
                         "Authentication requiring a secure terminal might fail.\n", pszPamService));

            if (   RT_SUCCESS(rc2)
                && szTTY[0] != '\0') /* Only try using PAM_TTY if we have something to set. */
                rc = pam_set_item(hPam, PAM_TTY, szTTY);

            if (rc == PAM_SUCCESS)
            {
                /* From this point on we don't allow falling back to other auth methods. */
                if (pfMayFallBack)
                    *pfMayFallBack = false;

                rc = pam_authenticate(hPam, 0);
                if (rc == PAM_SUCCESS)
                {
                    rc = pam_acct_mgmt(hPam, 0);
                    if (   rc == PAM_SUCCESS
                        || rc == PAM_AUTHINFO_UNAVAIL /*??*/)
                    {
                        if (   ppapszEnv
                            && s_pfnPamGetEnvList
                            && s_pfnPamSetCred)
                        {
                            /* pam_env.so creates the environment when pam_setcred is called,. */
                            int rcSetCred = pam_setcred(hPam, PAM_ESTABLISH_CRED | PAM_SILENT);
                            /** @todo check pam_setcred status code? */

                            /* Unless it does it during session opening (Ubuntu 21.10).  This
                               unfortunately means we might mount user dir and other crap: */
                            /** @todo do session handling properly   */
                            int rcOpenSession = PAM_ABORT;
                            if (   s_pfnPamOpenSession
                                && s_pfnPamCloseSession)
                                rcOpenSession = pam_open_session(hPam, PAM_SILENT);

                            *ppapszEnv = pam_getenvlist(hPam);
                            LogFlowFunc(("pam_getenvlist -> %p ([0]=%p); rcSetCred=%d rcOpenSession=%d\n",
                                         *ppapszEnv, *ppapszEnv ? **ppapszEnv : NULL, rcSetCred, rcOpenSession)); RT_NOREF(rcSetCred);

                            if (rcOpenSession == PAM_SUCCESS)
                                pam_close_session(hPam, PAM_SILENT);
                            pam_setcred(hPam, PAM_DELETE_CRED);
                        }

                        pam_end(hPam, PAM_SUCCESS);
                        LogFlowFunc(("pam auth (for %s) successful\n", pszPamService));
                        return VINF_SUCCESS;
                    }
                    LogFunc(("pam_acct_mgmt -> %d\n", rc));
                }
                else
                    LogFunc(("pam_authenticate -> %d\n", rc));
            }
            else
                LogFunc(("pam_setitem/PAM_TTY -> %d\n", rc));
        }
        else
            LogFunc(("pam_set_item/PAM_RUSER -> %d\n", rc));
        pam_end(hPam, rc);
    }
    else
        LogFunc(("pam_start(%s) -> %d\n", pszPamService, rc));

    LogRel2(("rtProcPosixAuthenticateUsingPam(%s): Failed authenticating user '%s' with %d\n", pszPamService, pszUser, rc));
    return VERR_AUTHENTICATION_FAILURE;
}


/**
 * Checks if the given service file is present in any of the pam.d directories.
 */
static bool rtProcPosixPamServiceExists(const char *pszService)
{
    char szPath[256];

    /* PAM_CONFIG_D: */
    int rc = RTPathJoin(szPath, sizeof(szPath), "/etc/pam.d/", pszService); AssertRC(rc);
    if (RTFileExists(szPath))
        return true;

    /* PAM_CONFIG_DIST_D: */
    rc = RTPathJoin(szPath, sizeof(szPath), "/usr/lib/pam.d/", pszService); AssertRC(rc);
    if (RTFileExists(szPath))
        return true;

    /* No support for PAM_CONFIG_DIST2_D. */
    return false;
}

#endif /* IPRT_USE_PAM */


#if defined(IPRT_WITH_DYNAMIC_CRYPT_R)
/** Pointer to crypt_r(). */
typedef char *(*PFNCRYPTR)(const char *, const char *, struct crypt_data *);

/**
 * Wrapper for resolving and calling crypt_r dynamically.
 *
 * The reason for this is that fedora 30+ wants to use libxcrypt rather than the
 * glibc libcrypt.  The two libraries has different crypt_data sizes and layout,
 * so we allocate a 256KB data block to be on the safe size (caller does this).
 */
static char *rtProcDynamicCryptR(const char *pszKey, const char *pszSalt, struct crypt_data *pData)
{
    static PFNCRYPTR volatile s_pfnCryptR = NULL;
    PFNCRYPTR pfnCryptR = s_pfnCryptR;
    if (pfnCryptR)
        return pfnCryptR(pszKey, pszSalt, pData);

    pfnCryptR = (PFNCRYPTR)(uintptr_t)RTLdrGetSystemSymbolEx("libcrypt.so", "crypt_r", RTLDRLOAD_FLAGS_SO_VER_RANGE(1, 6));
    if (!pfnCryptR)
        pfnCryptR = (PFNCRYPTR)(uintptr_t)RTLdrGetSystemSymbolEx("libxcrypt.so", "crypt_r", RTLDRLOAD_FLAGS_SO_VER_RANGE(1, 32));
    if (pfnCryptR)
    {
        s_pfnCryptR = pfnCryptR;
        return pfnCryptR(pszKey, pszSalt, pData);
    }

    LogRel(("IPRT/RTProc: Unable to locate crypt_r!\n"));
    return NULL;
}
#endif /* IPRT_WITH_DYNAMIC_CRYPT_R */


/** Free the environment list returned by rtCheckCredentials. */
static void rtProcPosixFreePamEnv(char **papszEnv)
{
    if (papszEnv)
    {
        for (size_t i = 0; papszEnv[i] != NULL; i++)
            free(papszEnv[i]);
        free(papszEnv);
    }
}


/**
 * Check the credentials and return the gid/uid of user.
 *
 * @param    pszUser    The username.
 * @param    pszPasswd  The password to authenticate with.
 * @param    gid        Where to store the GID of the user.
 * @param    uid        Where to store the UID of the user.
 * @param    ppapszEnv  Where to return PAM environment variables, NULL is fine
 *                      if no variables to return. Call rtProcPosixFreePamEnv to
 *                      free. Optional, so NULL can be passed in.
 * @returns IPRT status code
 */
static int rtCheckCredentials(const char *pszUser, const char *pszPasswd, gid_t *pGid, uid_t *pUid, char ***ppapszEnv)
{
    Log(("rtCheckCredentials: pszUser=%s\n", pszUser));
    int rc;

    if (ppapszEnv)
        *ppapszEnv = NULL;

    /*
     * Resolve user to UID and GID.
     */
    char            achBuf[_4K];
    struct passwd   Pw;
    struct passwd  *pPw;
    if (getpwnam_r(pszUser, &Pw, achBuf, sizeof(achBuf), &pPw) != 0)
        return VERR_AUTHENTICATION_FAILURE;
    if (!pPw)
        return VERR_AUTHENTICATION_FAILURE;

    *pUid = pPw->pw_uid;
    *pGid = pPw->pw_gid;

#ifdef IPRT_USE_PAM
    /*
     * Try authenticate using PAM, and falling back on crypto if allowed.
     */
    const char *pszService = "iprt-as-user";
    if (!rtProcPosixPamServiceExists("iprt-as-user"))
# ifdef IPRT_PAM_NATIVE_SERVICE_NAME_AS_USER
        pszService = IPRT_PAM_NATIVE_SERVICE_NAME_AS_USER;
# else
        pszService = "login";
# endif
    bool fMayFallBack = false;
    rc = rtProcPosixAuthenticateUsingPam(pszService, pszUser, pszPasswd, ppapszEnv, &fMayFallBack);
    if (RT_SUCCESS(rc) || !fMayFallBack)
    {
        RTMemWipeThoroughly(achBuf, sizeof(achBuf), 3);
        return rc;
    }
#endif

#if !defined(IPRT_USE_PAM) || defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS) || defined(RT_OS_OS2)
# if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    /*
     * Ditto for /etc/shadow and replace pw_passwd from above if we can access it:
     *
     * Note! On FreeBSD and OS/2 the root user will open /etc/shadow above, so
     *       this getspnam_r step is not necessary.
     */
    struct spwd  ShwPwd;
    char         achBuf2[_4K];
#  if defined(RT_OS_LINUX)
    struct spwd *pShwPwd = NULL;
    if (getspnam_r(pszUser, &ShwPwd, achBuf2, sizeof(achBuf2), &pShwPwd) != 0)
        pShwPwd = NULL;
#  else
    struct spwd *pShwPwd = getspnam_r(pszUser, &ShwPwd, achBuf2, sizeof(achBuf2));
#  endif
    if (pShwPwd != NULL)
        pPw->pw_passwd = pShwPwd->sp_pwdp;
# endif

    /*
     * Encrypt the passed in password and see if it matches.
     */
# if defined(RT_OS_LINUX)
    /* Default fCorrect=true if no password specified. In that case, pPw->pw_passwd
       must be NULL (no password set for this user). Fail if a password is specified
       but the user does not have one assigned. */
    rc = !pszPasswd || !*pszPasswd ? VINF_SUCCESS : VERR_AUTHENTICATION_FAILURE;
    if (pPw->pw_passwd && *pPw->pw_passwd)
# endif
    {
# if defined(RT_OS_LINUX) || defined(RT_OS_OS2)
#  ifdef IPRT_WITH_DYNAMIC_CRYPT_R
        size_t const       cbCryptData = RT_MAX(sizeof(struct crypt_data) * 2, _256K);
#  else
        size_t const       cbCryptData = sizeof(struct crypt_data);
#  endif
        struct crypt_data *pCryptData  = (struct crypt_data *)RTMemTmpAllocZ(cbCryptData);
        if (pCryptData)
        {
#  ifdef IPRT_WITH_DYNAMIC_CRYPT_R
            char *pszEncPasswd = rtProcDynamicCryptR(pszPasswd, pPw->pw_passwd, pCryptData);
#  else
            char *pszEncPasswd = crypt_r(pszPasswd, pPw->pw_passwd, pCryptData);
#  endif
            rc = pszEncPasswd && !strcmp(pszEncPasswd, pPw->pw_passwd) ? VINF_SUCCESS : VERR_AUTHENTICATION_FAILURE;
            RTMemWipeThoroughly(pCryptData, cbCryptData, 3);
            RTMemTmpFree(pCryptData);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
# else
        char *pszEncPasswd = crypt(pszPasswd, pPw->pw_passwd);
        rc = strcmp(pszEncPasswd, pPw->pw_passwd) == 0 ? VINF_SUCCESS : VERR_AUTHENTICATION_FAILURE;
# endif
    }

    /*
     * Return GID and UID on success.  Always wipe stack buffers.
     */
    if (RT_SUCCESS(rc))
    {
        *pGid = pPw->pw_gid;
        *pUid = pPw->pw_uid;
    }
# if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    RTMemWipeThoroughly(achBuf2, sizeof(achBuf2), 3);
# endif
#endif
    RTMemWipeThoroughly(achBuf, sizeof(achBuf), 3);
    return rc;
}

#ifdef RT_OS_SOLARIS

/** @todo the error reporting of the Solaris process contract code could be
 * a lot better, but essentially it is not meant to run into errors after
 * the debugging phase. */
static int rtSolarisContractPreFork(void)
{
    int templateFd = open64(CTFS_ROOT "/process/template", O_RDWR);
    if (templateFd < 0)
        return -1;

    /* Set template parameters and event sets. */
    if (ct_pr_tmpl_set_param(templateFd, CT_PR_PGRPONLY))
    {
        close(templateFd);
        return -1;
    }
    if (ct_pr_tmpl_set_fatal(templateFd, CT_PR_EV_HWERR))
    {
        close(templateFd);
        return -1;
    }
    if (ct_tmpl_set_critical(templateFd, 0))
    {
        close(templateFd);
        return -1;
    }
    if (ct_tmpl_set_informative(templateFd, CT_PR_EV_HWERR))
    {
        close(templateFd);
        return -1;
    }

    /* Make this the active template for the process. */
    if (ct_tmpl_activate(templateFd))
    {
        close(templateFd);
        return -1;
    }

    return templateFd;
}

static void rtSolarisContractPostForkChild(int templateFd)
{
    if (templateFd == -1)
        return;

    /* Clear the active template. */
    ct_tmpl_clear(templateFd);
    close(templateFd);
}

static void rtSolarisContractPostForkParent(int templateFd, pid_t pid)
{
    if (templateFd == -1)
        return;

    /* Clear the active template. */
    int cleared = ct_tmpl_clear(templateFd);
    close(templateFd);

    /* If the clearing failed or the fork failed there's nothing more to do. */
    if (cleared || pid <= 0)
        return;

    /* Look up the contract which was created by this thread. */
    int statFd = open64(CTFS_ROOT "/process/latest", O_RDONLY);
    if (statFd == -1)
        return;
    ct_stathdl_t statHdl;
    if (ct_status_read(statFd, CTD_COMMON, &statHdl))
    {
        close(statFd);
        return;
    }
    ctid_t ctId = ct_status_get_id(statHdl);
    ct_status_free(statHdl);
    close(statFd);
    if (ctId < 0)
        return;

    /* Abandon this contract we just created. */
    char ctlPath[PATH_MAX];
    size_t len = snprintf(ctlPath, sizeof(ctlPath),
                          CTFS_ROOT "/process/%ld/ctl", (long)ctId);
    if (len >= sizeof(ctlPath))
        return;
    int ctlFd = open64(ctlPath, O_WRONLY);
    if (statFd == -1)
        return;
    if (ct_ctl_abandon(ctlFd) < 0)
    {
        close(ctlFd);
        return;
    }
    close(ctlFd);
}

#endif /* RT_OS_SOLARIS */


RTR3DECL(int)   RTProcCreate(const char *pszExec, const char * const *papszArgs, RTENV Env, unsigned fFlags, PRTPROCESS pProcess)
{
    return RTProcCreateEx(pszExec, papszArgs, Env, fFlags,
                          NULL, NULL, NULL,  /* standard handles */
                          NULL /*pszAsUser*/, NULL /* pszPassword*/, NULL /*pvExtraData*/,
                          pProcess);
}


/**
 * Adjust the profile environment after forking the child process and changing
 * the UID.
 *
 * @returns IRPT status code.
 * @param   hEnvToUse       The environment we're going to use with execve.
 * @param   fFlags          The process creation flags.
 * @param   hEnv            The environment passed in by the user.
 */
static int rtProcPosixAdjustProfileEnvFromChild(RTENV hEnvToUse, uint32_t fFlags, RTENV hEnv)
{
    int rc = VINF_SUCCESS;
#ifdef RT_OS_DARWIN
    if (   RT_SUCCESS(rc)
        && (!(fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD) || RTEnvExistEx(hEnv, "TMPDIR")) )
    {
        char szValue[RTPATH_MAX];
        size_t cbNeeded = confstr(_CS_DARWIN_USER_TEMP_DIR, szValue, sizeof(szValue));
        if (cbNeeded > 0 && cbNeeded < sizeof(szValue))
        {
            char *pszTmp;
            rc = RTStrCurrentCPToUtf8(&pszTmp, szValue);
            if (RT_SUCCESS(rc))
            {
                rc = RTEnvSetEx(hEnvToUse, "TMPDIR", pszTmp);
                RTStrFree(pszTmp);
            }
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }
#else
    RT_NOREF_PV(hEnvToUse); RT_NOREF_PV(fFlags); RT_NOREF_PV(hEnv);
#endif
    return rc;
}


/**
 * Undos quoting and escape sequences and looks for stop characters.
 *
 * @returns Where to continue scanning in @a pszString.  This points to the
 *          next character after the stop character, but for the zero terminator
 *          it points to the terminator character.
 * @param   pszString           The string to undo quoting and escaping for.
 *                              This is both input and output as the work is
 *                              done in place.
 * @param   pfStoppedOnEqual    Where to return whether we stopped work on a
 *                              plain equal characater or not.  If this is NULL,
 *                              then the equal character is not a stop
 *                              character, then only newline and the zero
 *                              terminator are.
 */
static char *rtProcPosixProfileEnvUnquoteAndUnescapeString(char *pszString, bool *pfStoppedOnEqual)
{
    if (pfStoppedOnEqual)
        *pfStoppedOnEqual = false;

    enum { kPlain, kSingleQ, kDoubleQ } enmState = kPlain;
    char *pszDst = pszString;
    for (;;)
    {
        char ch = *pszString++;
        switch (ch)
        {
            default:
                *pszDst++ = ch;
                break;

            case '\\':
            {
                char ch2;
                if (   enmState == kSingleQ
                    || (ch2 = *pszString) == '\0'
                    || (enmState == kDoubleQ && strchr("\\$`\"\n", ch2) == NULL) )
                    *pszDst++ = ch;
                else
                {
                    *pszDst++ = ch2;
                    pszString++;
                }
                break;
            }

            case '"':
                if (enmState == kSingleQ)
                    *pszDst++ = ch;
                else
                    enmState = enmState == kPlain ? kDoubleQ : kPlain;
                break;

            case '\'':
                if (enmState == kDoubleQ)
                    *pszDst++ = ch;
                else
                    enmState = enmState == kPlain ? kSingleQ : kPlain;
                break;

            case '\n':
                if (enmState == kPlain)
                {
                    *pszDst = '\0';
                    return pszString;
                }
                *pszDst++ = ch;
                break;

            case '=':
                if (enmState == kPlain && pfStoppedOnEqual)
                {
                    *pszDst = '\0';
                    *pfStoppedOnEqual = true;
                    return pszString;
                }
                *pszDst++ = ch;
                break;

            case '\0':
                Assert(enmState == kPlain);
                *pszDst = '\0';
                return pszString - 1;
        }
    }
}


/**
 * Worker for rtProcPosixProfileEnvRunAndHarvest that parses the environment
 * dump and loads it into hEnvToUse.
 *
 * @note    This isn't entirely correct should any of the profile setup scripts
 *          unset any of the environment variables in the basic initial
 *          enviornment, but since that's unlikely and it's very convenient to
 *          have something half sensible as a basis if don't don't grok the dump
 *          entirely and would skip central stuff like PATH or HOME.
 *
 * @returns IPRT status code.
 * @retval  -VERR_PARSE_ERROR (positive, e.g. warning) if we run into trouble.
 * @retval  -VERR_INVALID_UTF8_ENCODING (positive, e.g. warning) if there are
 *          invalid UTF-8 in the environment.  This isn't unlikely if the
 *          profile doesn't use UTF-8.  This is unfortunately not something we
 *          can guess to accurately up front, so we don't do any guessing and
 *          hope everyone is sensible and use UTF-8.
 *
 * @param   hEnvToUse       The basic environment to extend with what we manage
 *                          to parse here.
 * @param   pszEnvDump      The environment dump to parse.  Nominally in Bourne
 *                          shell 'export -p' format.
 * @param   fWithMarkers    Whether there are markers around the dump (C shell,
 *                          tmux) or not.
 */
static int rtProcPosixProfileEnvHarvest(RTENV hEnvToUse, char *pszEnvDump, bool fWithMarkers)
{
    LogRel3(("**** pszEnvDump start ****\n%s**** pszEnvDump end ****\n", pszEnvDump));
    if (!LogIs3Enabled())
        LogFunc(("**** pszEnvDump start ****\n%s**** pszEnvDump end ****\n", pszEnvDump));

    /*
     * Clip dump at markers if we're using them (C shell).
     */
    if (fWithMarkers)
    {
        char *pszStart = strstr(pszEnvDump, g_szEnvMarkerBegin);
        AssertReturn(pszStart, -VERR_PARSE_ERROR);
        pszStart += sizeof(g_szEnvMarkerBegin) - 1;
        if (*pszStart == '\n')
            pszStart++;
        pszEnvDump = pszStart;

        char *pszEnd = strstr(pszStart, g_szEnvMarkerEnd);
        AssertReturn(pszEnd, -VERR_PARSE_ERROR);
        *pszEnd = '\0';
    }

    /*
     * Since we're using /bin/sh -c "export -p" for all the dumping, we should
     * always get lines on the format:
     *     export VAR1="Value 1"
     *     export VAR2=Value2
     *
     * However, just in case something goes wrong, like bash doesn't think it
     * needs to be posixly correct, try deal with the alternative where
     * "declare -x " replaces the "export".
     */
    const char *pszPrefix;
    if (   strncmp(pszEnvDump, RT_STR_TUPLE("export")) == 0
        && RT_C_IS_BLANK(pszEnvDump[6]))
        pszPrefix = "export ";
    else if (   strncmp(pszEnvDump, RT_STR_TUPLE("declare")) == 0
             && RT_C_IS_BLANK(pszEnvDump[7])
             && pszEnvDump[8] == '-')
        pszPrefix = "declare -x "; /* We only need to care about the non-array, non-function lines. */
    else
        AssertFailedReturn(-VERR_PARSE_ERROR);
    size_t const cchPrefix = strlen(pszPrefix);

    /*
     * Process the lines, ignoring stuff which we don't grok.
     * The shell should quote problematic characters. Bash double quotes stuff
     * by default, whereas almquist's shell does it as needed and only the value
     * side.
     */
    int rc = VINF_SUCCESS;
    while (pszEnvDump && *pszEnvDump != '\0')
    {
        /*
         * Skip the prefixing command.
         */
        if (   cchPrefix == 0
            || strncmp(pszEnvDump, pszPrefix, cchPrefix) == 0)
        {
            pszEnvDump += cchPrefix;
            while (RT_C_IS_BLANK(*pszEnvDump))
                pszEnvDump++;
        }
        else
        {
            /* Oops, must find our bearings for some reason... */
            pszEnvDump = strchr(pszEnvDump, '\n');
            rc = -VERR_PARSE_ERROR;
            continue;
        }

        /*
         * Parse out the variable name using typical bourne shell escaping
         * and quoting rules.
         */
        /** @todo We should throw away lines that aren't propertly quoted, now we
         *        just continue and use what we found. */
        const char *pszVar               = pszEnvDump;
        bool        fStoppedOnPlainEqual = false;
        pszEnvDump = rtProcPosixProfileEnvUnquoteAndUnescapeString(pszEnvDump, &fStoppedOnPlainEqual);
        const char *pszValue             = pszEnvDump;
        if (fStoppedOnPlainEqual)
            pszEnvDump = rtProcPosixProfileEnvUnquoteAndUnescapeString(pszEnvDump, NULL /*pfStoppedOnPlainEqual*/);
        else
            pszValue = "";

        /*
         * Add them if valid UTF-8, otherwise we simply drop them for now.
         * The whole codeset stuff goes seriously wonky here as the environment
         * we're harvesting probably contains it's own LC_CTYPE or LANG variables,
         * so ignore the problem for now.
         */
        if (   RTStrIsValidEncoding(pszVar)
            && RTStrIsValidEncoding(pszValue))
        {
            int rc2 = RTEnvSetEx(hEnvToUse, pszVar, pszValue);
            AssertRCReturn(rc2, rc2);
        }
        else if (rc == VINF_SUCCESS)
            rc = -VERR_INVALID_UTF8_ENCODING;
    }

    return rc;
}


/**
 * Runs the user's shell in login mode with some environment dumping logic and
 * harvests the dump, putting it into hEnvToUse.
 *
 * This is a bit hairy, esp. with regards to codesets.
 *
 * @returns IPRT status code.  Not all error statuses will be returned and the
 *          caller should just continue with whatever is in hEnvToUse.
 *
 * @param   hEnvToUse   On input this is the basic user environment, on success
 *                      in is fleshed out with stuff from the login shell dump.
 * @param   pszAsUser   The user name for the profile.
 * @param   uid         The UID corrsponding to @a pszAsUser, ~0 if current user.
 * @param   gid         The GID corrsponding to @a pszAsUser, ~0 if current user.
 * @param   pszShell    The login shell.  This is a writable string to avoid
 *                      needing to make a copy of it when examining the path
 *                      part, instead we make a temporary change to it which is
 *                      always reverted before returning.
 */
static int rtProcPosixProfileEnvRunAndHarvest(RTENV hEnvToUse, const char *pszAsUser, uid_t uid, gid_t gid, char *pszShell)
{
    LogFlowFunc(("pszAsUser=%s uid=%u gid=%u pszShell=%s; hEnvToUse contains %u variables on entry\n",
                 pszAsUser, uid, gid, pszShell, RTEnvCountEx(hEnvToUse) ));

    /*
     * The three standard handles should be pointed to /dev/null, the 3rd handle
     * is used to dump the environment.
     */
    RTPIPE hPipeR, hPipeW;
    int rc = RTPipeCreate(&hPipeR, &hPipeW, 0);
    if (RT_SUCCESS(rc))
    {
        RTFILE hFileNull;
        rc = RTFileOpenBitBucket(&hFileNull, RTFILE_O_READWRITE);
        if (RT_SUCCESS(rc))
        {
            int aRedirFds[4];
            aRedirFds[0] = aRedirFds[1] = aRedirFds[2] = RTFileToNative(hFileNull);
            aRedirFds[3] = RTPipeToNative(hPipeW);

            /*
             * Allocate a buffer for receiving the environment dump.
             *
             * This is fixed sized for simplicity and safety (creative user script
             * shouldn't be allowed to exhaust our memory or such, after all we're
             * most likely running with root privileges in this code path).
             */
            size_t const  cbEnvDump  = _64K;
            char  * const pszEnvDump = (char *)RTMemTmpAllocZ(cbEnvDump);
            if (pszEnvDump)
            {
                /*
                 * Our default approach is using /bin/sh:
                 */
                const char *pszExec = _PATH_BSHELL;
                const char *apszArgs[8];
                apszArgs[0] = "-sh";        /* First arg must start with a dash for login shells. */
                apszArgs[1] = "-c";
                apszArgs[2] = "POSIXLY_CORRECT=1;export -p >&3";
                apszArgs[3] = NULL;

                /*
                 * But see if we can trust the shell to be a real usable shell.
                 * This would be great as different shell typically has different profile setup
                 * files and we'll endup with the wrong enviornment if we use a different shell.
                 */
                char        szDashShell[32];
                char        szExportArg[128];
                bool        fWithMarkers = false;
                const char *pszShellNm   = RTPathFilename(pszShell);
                if (   pszShellNm
                    && access(pszShellNm, X_OK))
                {
                    /*
                     * First the check that it's a known bin directory:
                     */
                    size_t const cchShellPath = pszShellNm - pszShell;
                    char const   chSaved = pszShell[cchShellPath - 1];
                    pszShell[cchShellPath - 1] = '\0';
                    if (   RTPathCompare(pszShell, "/bin") == 0
                        || RTPathCompare(pszShell, "/usr/bin") == 0
                        || RTPathCompare(pszShell, "/usr/local/bin") == 0)
                    {
                        /*
                         * Then see if we recognize the shell name.
                         */
                        RTStrCopy(&szDashShell[1], sizeof(szDashShell) - 1, pszShellNm);
                        szDashShell[0] = '-';
                        if (   strcmp(pszShellNm, "bash") == 0
                            || strcmp(pszShellNm, "ksh") == 0
                            || strcmp(pszShellNm, "ksh93") == 0
                            || strcmp(pszShellNm, "zsh") == 0
                            || strcmp(pszShellNm, "fish") == 0)
                        {
                            pszExec      = pszShell;
                            apszArgs[0]  = szDashShell;

                            /* Use /bin/sh for doing the environment dumping so we get the same kind
                               of output from everyone and can limit our parsing + testing efforts. */
                            RTStrPrintf(szExportArg, sizeof(szExportArg),
                                        "%s -c 'POSIXLY_CORRECT=1;export -p >&3'", _PATH_BSHELL);
                            apszArgs[2]  = szExportArg;
                        }
                        /* C shell is very annoying in that it closes fd 3 without regard to what
                           we might have put there, so we must use stdout here but with markers so
                           we can find the dump.
                           Seems tmux have similar issues as it doesn't work above, but works fine here. */
                        else if (   strcmp(pszShellNm, "csh") == 0
                                 || strcmp(pszShellNm, "tcsh") == 0
                                 || strcmp(pszShellNm, "tmux") == 0)
                        {
                            pszExec      = pszShell;
                            apszArgs[0]  = szDashShell;

                            fWithMarkers = true;
                            size_t cch = RTStrPrintf(szExportArg, sizeof(szExportArg),
                                                     "%s -c 'set -e;POSIXLY_CORRECT=1;echo %s;export -p;echo %s'",
                                                     _PATH_BSHELL, g_szEnvMarkerBegin, g_szEnvMarkerEnd);
                            Assert(cch < sizeof(szExportArg) - 1); RT_NOREF(cch);
                            apszArgs[2]  = szExportArg;

                            aRedirFds[1] = aRedirFds[3];
                            aRedirFds[3] = -1;
                        }
                    }
                    pszShell[cchShellPath - 1] = chSaved;
                }

                /*
                 * Create the process and wait for the output.
                 */
                LogFunc(("Executing '%s': '%s', '%s', '%s'\n", pszExec, apszArgs[0], apszArgs[1], apszArgs[2]));
                RTPROCESS hProcess = NIL_RTPROCESS;
                rc = rtProcPosixCreateInner(pszExec, apszArgs, hEnvToUse, hEnvToUse, 0 /*fFlags*/,
                                            pszAsUser, uid, gid, RT_ELEMENTS(aRedirFds), aRedirFds, NULL /*pvExtraData*/, &hProcess);
                if (RT_SUCCESS(rc))
                {
                    RTPipeClose(hPipeW);
                    hPipeW = NIL_RTPIPE;

                    size_t         offEnvDump = 0;
                    uint64_t const msStart    = RTTimeMilliTS();
                    for (;;)
                    {
                        size_t cbRead = 0;
                        if (offEnvDump < cbEnvDump - 1)
                        {
                            rc = RTPipeRead(hPipeR, &pszEnvDump[offEnvDump], cbEnvDump - 1 - offEnvDump, &cbRead);
                            if (RT_SUCCESS(rc))
                                offEnvDump += cbRead;
                            else
                            {
                                LogFlowFunc(("Breaking out of read loop: %Rrc\n", rc));
                                if (rc == VERR_BROKEN_PIPE)
                                    rc = VINF_SUCCESS;
                                break;
                            }
                            pszEnvDump[offEnvDump] = '\0';
                        }
                        else
                        {
                            LogFunc(("Too much data in environment dump, dropping it\n"));
                            rc = VERR_TOO_MUCH_DATA;
                            break;
                        }

                        /* Do the timout check. */
                        uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
                        if (cMsElapsed >= RT_MS_15SEC)
                        {
                            LogFunc(("Timed out after %RU64 ms\n", cMsElapsed));
                            rc = VERR_TIMEOUT;
                            break;
                        }

                        /* If we got no data in above wait for more to become ready. */
                        if (!cbRead)
                            RTPipeSelectOne(hPipeR, RT_MS_15SEC - cMsElapsed);
                    }

                    /*
                     * Kill the process and wait for it to avoid leaving zombies behind.
                     */
                    /** @todo do we check the exit code? */
                    int rc2 = RTProcWait(hProcess, RTPROCWAIT_FLAGS_NOBLOCK, NULL);
                    if (RT_SUCCESS(rc2))
                        LogFlowFunc(("First RTProcWait succeeded\n"));
                    else
                    {
                        LogFunc(("First RTProcWait failed (%Rrc), terminating and doing a blocking wait\n", rc2));
                        RTProcTerminate(hProcess);
                        RTProcWait(hProcess, RTPROCWAIT_FLAGS_BLOCK, NULL);
                    }

                    /*
                     * Parse the result.
                     */
                    if (RT_SUCCESS(rc))
                        rc = rtProcPosixProfileEnvHarvest(hEnvToUse, pszEnvDump, fWithMarkers);
                    else
                    {
                        LogFunc(("Ignoring rc=%Rrc from the pipe read loop and continues with basic environment\n", rc));
                        rc = -rc;
                    }
                }
                else
                    LogFunc(("Failed to create process '%s': %Rrc\n", pszExec, rc));
                RTMemTmpFree(pszEnvDump);
            }
            else
            {
                LogFunc(("Failed to allocate %#zx bytes for the dump\n", cbEnvDump));
                rc = VERR_NO_TMP_MEMORY;
            }
            RTFileClose(hFileNull);
        }
        else
            LogFunc(("Failed to open /dev/null: %Rrc\n", rc));
        RTPipeClose(hPipeR);
        RTPipeClose(hPipeW);
    }
    else
        LogFunc(("Failed to create pipe: %Rrc\n", rc));
    LogFlowFunc(("returns %Rrc (hEnvToUse contains %u variables now)\n", rc, RTEnvCountEx(hEnvToUse)));
    return rc;
}


/**
 * Create an environment for the given user.
 *
 * This starts by creating a very basic environment and then tries to do it
 * properly by running the user's shell in login mode with some environment
 * dumping attached.  The latter may fail and we'll ignore that for now and move
 * ahead with the very basic environment.
 *
 * @returns IPRT status code.
 * @param   phEnvToUse  Where to return the created environment.
 * @param   pszAsUser   The user name for the profile.  NULL if the current
 *                      user.
 * @param   uid         The UID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   gid         The GID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   fFlags      RTPROC_FLAGS_XXX
 * @param   papszPamEnv Array of environment variables returned by PAM, if
 *                      it was used for authentication and produced anything.
 *                      Otherwise NULL.
 */
static int rtProcPosixCreateProfileEnv(PRTENV phEnvToUse, const char *pszAsUser, uid_t uid, gid_t gid,
                                       uint32_t fFlags, char **papszPamEnv)
{
    /*
     * Get the passwd entry for the user.
     */
    struct passwd   Pwd;
    struct passwd  *pPwd = NULL;
    char            achBuf[_4K];
    int             rc;
    errno = 0;
    if (pszAsUser)
        rc = getpwnam_r(pszAsUser, &Pwd, achBuf, sizeof(achBuf), &pPwd);
    else
        rc = getpwuid_r(getuid(), &Pwd, achBuf, sizeof(achBuf), &pPwd);
    if (rc == 0 && pPwd)
    {
        /*
         * Convert stuff to UTF-8 since the environment is UTF-8.
         */
        char *pszDir;
        rc = RTStrCurrentCPToUtf8(&pszDir, pPwd->pw_dir);
        if (RT_SUCCESS(rc))
        {
#if 0 /* Enable and modify this to test shells other that your login shell. */
            pPwd->pw_shell = (char *)"/bin/tmux";
#endif
            char *pszShell;
            rc = RTStrCurrentCPToUtf8(&pszShell, pPwd->pw_shell);
            if (RT_SUCCESS(rc))
            {
                char *pszAsUserFree = NULL;
                if (!pszAsUser)
                {
                    rc = RTStrCurrentCPToUtf8(&pszAsUserFree, pPwd->pw_name);
                    if (RT_SUCCESS(rc))
                        pszAsUser = pszAsUserFree;
                }
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Create and populate the environment.
                     */
                    rc = RTEnvCreate(phEnvToUse);
                    if (RT_SUCCESS(rc))
                    {
                        RTENV hEnvToUse = *phEnvToUse;
                        rc = RTEnvSetEx(hEnvToUse, "HOME", pszDir);
                        if (RT_SUCCESS(rc))
                            rc = RTEnvSetEx(hEnvToUse, "SHELL", pszShell);
                        if (RT_SUCCESS(rc))
                            rc = RTEnvSetEx(hEnvToUse, "USER", pszAsUser);
                        if (RT_SUCCESS(rc))
                            rc = RTEnvSetEx(hEnvToUse, "LOGNAME", pszAsUser);
                        if (RT_SUCCESS(rc))
                            rc = RTEnvSetEx(hEnvToUse, "PATH", pPwd->pw_uid == 0 ? _PATH_STDPATH : _PATH_DEFPATH);
                        char szTmpPath[RTPATH_MAX];
                        if (RT_SUCCESS(rc))
                        {
                            RTStrPrintf(szTmpPath, sizeof(szTmpPath), "%s/%s", _PATH_MAILDIR, pszAsUser);
                            rc = RTEnvSetEx(hEnvToUse, "MAIL", szTmpPath);
                        }
#ifdef RT_OS_DARWIN
                        if (RT_SUCCESS(rc))
                        {
                            /* TMPDIR is some unique per user directory under /var/folders on darwin,
                               so get the one for the current user.  If we're launching the process as
                               a different user, rtProcPosixAdjustProfileEnvFromChild will update it
                               again for the actual child process user (provided we set it here). See
                               https://opensource.apple.com/source/Libc/Libc-997.1.1/darwin/_dirhelper.c
                               for the implementation of this query. */
                            size_t cbNeeded = confstr(_CS_DARWIN_USER_TEMP_DIR, szTmpPath, sizeof(szTmpPath));
                            if (cbNeeded > 0 && cbNeeded < sizeof(szTmpPath))
                            {
                                char *pszTmp;
                                rc = RTStrCurrentCPToUtf8(&pszTmp, szTmpPath);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = RTEnvSetEx(hEnvToUse, "TMPDIR", pszTmp);
                                    RTStrFree(pszTmp);
                                }
                            }
                            else
                                rc = VERR_BUFFER_OVERFLOW;
                        }
#endif
                        /*
                         * Add everything from the PAM environment.
                         */
                        if (RT_SUCCESS(rc) && papszPamEnv != NULL)
                            for (size_t i = 0; papszPamEnv[i] != NULL && RT_SUCCESS(rc); i++)
                            {
                                char *pszEnvVar;
                                rc = RTStrCurrentCPToUtf8(&pszEnvVar, papszPamEnv[i]);
                                if (RT_SUCCESS(rc))
                                {
                                    char *pszValue = strchr(pszEnvVar, '=');
                                    if (pszValue)
                                        *pszValue++ = '\0';
                                    rc = RTEnvSetEx(hEnvToUse, pszEnvVar, pszValue ? pszValue : "");
                                    RTStrFree(pszEnvVar);
                                }
                                /* Ignore conversion issue, though LogRel them. */
                                else if (rc != VERR_NO_STR_MEMORY && rc != VERR_NO_MEMORY)
                                {
                                    LogRelMax(256, ("RTStrCurrentCPToUtf8(,%.*Rhxs) -> %Rrc\n", strlen(pszEnvVar), pszEnvVar, rc));
                                    rc = -rc;
                                }
                            }
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Now comes the fun part where we need to try run a shell in login mode
                             * and harvest its final environment to get the proper environment for
                             * the user.  We ignore some failures here so buggy login scrips and
                             * other weird stuff won't trip us up too badly.
                             */
                            if (!(fFlags & RTPROC_FLAGS_ONLY_BASIC_PROFILE))
                                rc = rtProcPosixProfileEnvRunAndHarvest(hEnvToUse, pszAsUser, uid, gid, pszShell);
                        }

                        if (RT_FAILURE(rc))
                            RTEnvDestroy(hEnvToUse);
                    }
                    RTStrFree(pszAsUserFree);
                }
                RTStrFree(pszShell);
            }
            RTStrFree(pszDir);
        }
    }
    else
        rc = errno ? RTErrConvertFromErrno(errno) : VERR_ACCESS_DENIED;
    return rc;
}


/**
 * Converts the arguments to the child's LC_CTYPE charset if necessary.
 *
 * @returns IPRT status code.
 * @param   papszArgs   The arguments (UTF-8).
 * @param   hEnvToUse   The child process environment.
 * @param   ppapszArgs  Where to return the converted arguments.  The array
 *                      entries must be freed by RTStrFree and the array itself
 *                      by RTMemFree.
 */
static int rtProcPosixConvertArgv(const char * const *papszArgs, RTENV hEnvToUse, char ***ppapszArgs)
{
    *ppapszArgs = (char **)papszArgs;

    /*
     * The first thing we need to do here is to try guess the codeset of the
     * child process and check if it's UTF-8 or not.
     */
    const char *pszEncoding;
    char        szEncoding[512];
    if (hEnvToUse == RTENV_DEFAULT)
    {
        /* Same environment as us, assume setlocale is up to date: */
        pszEncoding = rtStrGetLocaleCodeset();
    }
    else
    {
        /*
         * LC_ALL overrides everything else.  The LC_* environment variables are often set
         * to the empty string so move on the next variable if that is the case (that's
         * what setlocale in glibc does).
         */
        const char *pszVar;
        int rc = RTEnvGetEx(hEnvToUse, pszVar = "LC_ALL", szEncoding, sizeof(szEncoding), NULL);
        if (rc == VERR_ENV_VAR_NOT_FOUND || (RT_SUCCESS(rc) && szEncoding[0] == '\0'))
            rc = RTEnvGetEx(hEnvToUse, pszVar = "LC_CTYPE", szEncoding, sizeof(szEncoding), NULL);
        if (rc == VERR_ENV_VAR_NOT_FOUND || (RT_SUCCESS(rc) && szEncoding[0] == '\0'))
            rc = RTEnvGetEx(hEnvToUse, pszVar = "LANG", szEncoding, sizeof(szEncoding), NULL);
        if (RT_SUCCESS(rc) && szEncoding[0] != '\0')
        {
            /*
             * LC_ALL can contain a composite locale consisting of the locales of each of the
             * categories in two different formats depending on the OS. On Solaris, macOS, and
             * *BSD composite locale names use slash ('/') as the separator and the following
             * order for the categories:
             *   LC_CTYPE/LC_NUMERIC/LC_TIME/LC_COLLATE/LC_MONETARY/LC_MESSAGES
             * e.g.:
             *   en_US.UTF-8/POSIX/el_GR.UTF-8/el_CY.UTF-8/en_GB.UTF-8/es_ES.UTF-8
             *
             * On Solaris there is also a leading slash.
             *
             * On Linux and OS/2 the composite locale format is made up of key-value pairs
             * of category names and locales of the form 'name=value' with each element
             * separated by a semicolon in the same order as above with following additional
             * categories included as well:
             *   LC_PAPER/LC_NAME/LC_ADDRESS/LC_TELEPHONE/LC_MEASUREMENT/LC_IDENTIFICATION
             * e.g.
             *   LC_CTYPE=fr_BE;LC_NUMERIC=fr_BE@euro;LC_TIME=fr_BE.utf8;LC_COLLATE=fr_CA;\
             *   LC_MONETARY=fr_CA.utf8;LC_MESSAGES=fr_CH;LC_PAPER=fr_CH.utf8;LC_NAME=fr_FR;\
             *   LC_ADDRESS=fr_FR.utf8;LC_TELEPHONE=fr_LU;LC_MEASUREMENT=fr_LU@euro;\
             *   LC_IDENTIFICATION=fr_LU.utf8
             */
            char *pszEncodingStart = szEncoding;
#if !defined(RT_OS_LINUX) && !defined(RT_OS_OS2)
            if (*pszEncodingStart == '/')
                pszEncodingStart++;
            char *pszSlash = strchr(pszEncodingStart, '/');
            if (pszSlash)
                *pszSlash = '\0';       /* This ASSUMES the first one is LC_CTYPE! */
#else
            char *pszCType = strstr(pszEncodingStart, "LC_CTYPE=");
            if (pszCType)
            {
                pszEncodingStart = pszCType + sizeof("LC_CTYPE=") - 1;

                char *pszSemiColon = strchr(pszEncodingStart, ';');
                if (pszSemiColon)
                    *pszSemiColon = '\0';
            }
#endif

            /*
             * Use newlocale and nl_langinfo_l to determine the default codeset for the locale
             * specified in the child's environment.  These routines have been around since
             * ancient days on Linux and for quite a long time on macOS, Solaris, and *BSD but
             * to ensure their availability check that LC_CTYPE_MASK is defined.
             *
             * Note! The macOS nl_langinfo(3)/nl_langinfo_l(3) routines return a pointer to an
             *       empty string for "short" locale names like en_NZ, it_IT, el_GR, etc. so use
             *       UTF-8 in those cases as it is the default for short name locales on macOS
             *       (see also rtStrGetLocaleCodeset).
             */
#ifdef LC_CTYPE_MASK
            locale_t hLocale = newlocale(LC_CTYPE_MASK, pszEncodingStart, (locale_t)0);
            if (hLocale != (locale_t)0)
            {
                const char *pszCodeset = nl_langinfo_l(CODESET, hLocale);
                Log2Func(("nl_langinfo_l(CODESET, %s=%s) -> %s\n", pszVar, pszEncodingStart, pszCodeset));
                if (!pszCodeset || *pszCodeset == '\0')
# ifdef RT_OS_DARWIN
                    pszEncoding = "UTF-8";
# else
                    pszEncoding = "ASCII";
# endif
                else
                {
                    rc = RTStrCopy(szEncoding, sizeof(szEncoding), pszCodeset);
                    AssertRC(rc); /* cannot possibly overflow */
                }

                freelocale(hLocale);
                pszEncoding = szEncoding;
             }
             else
#endif
             {
                 /* If there is something that ought to be a character set encoding, try use it: */
                 const char *pszDot = strchr(pszEncodingStart, '.');
                 if (pszDot)
                     pszDot = RTStrStripL(pszDot + 1);
                 if (pszDot && *pszDot != '\0')
                 {
                     pszEncoding = pszDot;
                     Log2Func(("%s=%s -> %s (simple)\n", pszVar, szEncoding, pszEncoding));
                 }
                 else
                 {
                     /* This is mostly wrong, but I cannot think of anything better now: */
                     pszEncoding = rtStrGetLocaleCodeset();
                     LogFunc(("No newlocale or it failed (on '%s=%s', errno=%d), falling back on %s that we're using...\n",
                              pszVar, pszEncodingStart, errno, pszEncoding));
                 }
             }
             RT_NOREF_PV(pszVar);
        }
        else
#ifdef RT_OS_DARWIN /* @bugref{10153}: Darwin defaults to UTF-8. */
            pszEncoding = "UTF-8";
#else
            pszEncoding = "ASCII";
#endif
    }

    /*
     * Do nothing if it's UTF-8.
     */
    if (rtStrIsCodesetUtf8(pszEncoding))
    {
        LogFlowFunc(("No conversion needed (%s)\n", pszEncoding));
        return VINF_SUCCESS;
    }


    /*
     * Do the conversion.
     */
    size_t cArgs = 0;
    while (papszArgs[cArgs] != NULL)
        cArgs++;
    LogFunc(("Converting #%u arguments to %s...\n", cArgs, pszEncoding));

    char **papszArgsConverted = (char **)RTMemAllocZ(sizeof(papszArgsConverted[0]) * (cArgs + 2));
    AssertReturn(papszArgsConverted, VERR_NO_MEMORY);

    void *pvConversionCache = NULL;
    rtStrLocalCacheInit(&pvConversionCache);
    for (size_t i = 0; i < cArgs; i++)
    {
        int rc = rtStrLocalCacheConvert(papszArgs[i], strlen(papszArgs[i]), "UTF-8",
                                        &papszArgsConverted[i], 0, pszEncoding, &pvConversionCache);
        if (RT_SUCCESS(rc) && rc != VWRN_NO_TRANSLATION)
        { /* likely */ }
        else
        {
            LogRelMax(100, ("Failed to convert argument #%u '%s' to '%s': %Rrc\n", i, papszArgs[i], pszEncoding, rc));
            while (i-- > 0)
                RTStrFree(papszArgsConverted[i]);
            RTMemFree(papszArgsConverted);
            rtStrLocalCacheDelete(&pvConversionCache);
            return rc == VWRN_NO_TRANSLATION || rc == VERR_NO_TRANSLATION ? VERR_PROC_NO_ARG_TRANSLATION : rc;
        }
    }

    rtStrLocalCacheDelete(&pvConversionCache);
    *ppapszArgs = papszArgsConverted;
    return VINF_SUCCESS;
}


/**
 * The result structure for rtPathFindExec/RTPathTraverseList.
 * @todo move to common path code?
 */
typedef struct RTPATHINTSEARCH
{
    /** For EACCES or EPERM errors that we continued on.
     * @note Must be initialized to VINF_SUCCESS. */
    int  rcSticky;
    /** Buffer containing the filename. */
    char szFound[RTPATH_MAX];
} RTPATHINTSEARCH;
/** Pointer to a rtPathFindExec/RTPathTraverseList result. */
typedef RTPATHINTSEARCH *PRTPATHINTSEARCH;


/**
 * RTPathTraverseList callback used by RTProcCreateEx to locate the executable.
 */
static DECLCALLBACK(int) rtPathFindExec(char const *pchPath, size_t cchPath, void *pvUser1, void *pvUser2)
{
    const char      *pszExec = (const char *)pvUser1;
    PRTPATHINTSEARCH pResult = (PRTPATHINTSEARCH)pvUser2;
    int rc = RTPathJoinEx(pResult->szFound, sizeof(pResult->szFound), pchPath, cchPath, pszExec, RTSTR_MAX,
                          RTPATH_STR_F_STYLE_HOST);
    if (RT_SUCCESS(rc))
    {
        const char *pszNativeExec = NULL;
        rc = rtPathToNative(&pszNativeExec, pResult->szFound, NULL);
        if (RT_SUCCESS(rc))
        {
            if (!access(pszNativeExec, X_OK))
                rc = VINF_SUCCESS;
            else
            {
                if (   errno == EACCES
                    || errno == EPERM)
                    pResult->rcSticky = RTErrConvertFromErrno(errno);
                rc = VERR_TRY_AGAIN;
            }
            rtPathFreeNative(pszNativeExec, pResult->szFound);
        }
        else
            AssertRCStmt(rc, rc = VERR_TRY_AGAIN /* don't stop on this, whatever it is */);
    }
    return rc;
}


RTR3DECL(int)   RTProcCreateEx(const char *pszExec, const char * const *papszArgs, RTENV hEnv, uint32_t fFlags,
                               PCRTHANDLE phStdIn, PCRTHANDLE phStdOut, PCRTHANDLE phStdErr, const char *pszAsUser,
                               const char *pszPassword, void *pvExtraData, PRTPROCESS phProcess)
{
    int rc;
    LogFlow(("RTProcCreateEx: pszExec=%s pszAsUser=%s fFlags=%#x phStdIn=%p phStdOut=%p phStdErr=%p\n",
             pszExec, pszAsUser, fFlags, phStdIn, phStdOut, phStdErr));

    /*
     * Input validation
     */
    AssertPtrReturn(pszExec, VERR_INVALID_POINTER);
    AssertReturn(*pszExec, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTPROC_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & RTPROC_FLAGS_DETACHED) || !phProcess, VERR_INVALID_PARAMETER);
    AssertReturn(hEnv != NIL_RTENV, VERR_INVALID_PARAMETER);
    AssertPtrReturn(papszArgs, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszAsUser, VERR_INVALID_POINTER);
    AssertReturn(!pszAsUser || *pszAsUser, VERR_INVALID_PARAMETER);
    AssertReturn(!pszPassword || pszAsUser, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszPassword, VERR_INVALID_POINTER);
#if defined(RT_OS_OS2)
    if (fFlags & RTPROC_FLAGS_DETACHED)
        return VERR_PROC_DETACH_NOT_SUPPORTED;
#endif

    /* Extra data: */
    if (fFlags & RTPROC_FLAGS_CWD)
        AssertPtrReturn(pvExtraData, VERR_INVALID_POINTER);
    else
        AssertReturn(pvExtraData == NULL, VERR_INVALID_PARAMETER);
    /* Note: Windows-specific flags will be quietly ignored, unless they use pvExtraData. */

    /*
     * Get the file descriptors for the handles we've been passed.
     */
    PCRTHANDLE  paHandles[3] = { phStdIn, phStdOut, phStdErr };
    int         aStdFds[3]   = {      -1,       -1,       -1 };
    for (int i = 0; i < 3; i++)
    {
        if (paHandles[i])
        {
            AssertPtrReturn(paHandles[i], VERR_INVALID_POINTER);
            switch (paHandles[i]->enmType)
            {
                case RTHANDLETYPE_FILE:
                    aStdFds[i] = paHandles[i]->u.hFile != NIL_RTFILE
                               ? (int)RTFileToNative(paHandles[i]->u.hFile)
                               : -2 /* close it */;
                    break;

                case RTHANDLETYPE_PIPE:
                    aStdFds[i] = paHandles[i]->u.hPipe != NIL_RTPIPE
                               ? (int)RTPipeToNative(paHandles[i]->u.hPipe)
                               : -2 /* close it */;
                    break;

                case RTHANDLETYPE_SOCKET:
                    aStdFds[i] = paHandles[i]->u.hSocket != NIL_RTSOCKET
                               ? (int)RTSocketToNative(paHandles[i]->u.hSocket)
                               : -2 /* close it */;
                    break;

                default:
                    AssertMsgFailedReturn(("%d: %d\n", i, paHandles[i]->enmType), VERR_INVALID_PARAMETER);
            }
            /** @todo check the close-on-execness of these handles?  */
        }
    }

    for (int i = 0; i < 3; i++)
        if (aStdFds[i] == i)
            aStdFds[i] = -1;
    LogFlowFunc(("aStdFds={%d, %d, %d}\n", aStdFds[0], aStdFds[1], aStdFds[2]));

    for (int i = 0; i < 3; i++)
        AssertMsgReturn(aStdFds[i] < 0 || aStdFds[i] > i,
                        ("%i := %i not possible because we're lazy\n", i, aStdFds[i]),
                        VERR_NOT_SUPPORTED);

    /*
     * Validate the credentials if a user is specified.
     */
    bool const  fNeedLoginEnv = (fFlags & RTPROC_FLAGS_PROFILE)
                             && ((fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD) || hEnv == RTENV_DEFAULT);
    uid_t       uid           = ~(uid_t)0;
    gid_t       gid           = ~(gid_t)0;
    char      **papszPamEnv   = NULL;
    if (pszAsUser)
    {
        rc = rtCheckCredentials(pszAsUser, pszPassword, &gid, &uid, fNeedLoginEnv ? &papszPamEnv : NULL);
        if (RT_FAILURE(rc))
            return rc;
    }
#ifdef IPRT_USE_PAM
    /*
     * User unchanged, but if PROFILE is request we must try get the PAM
     * environmnet variables.
     *
     * For this to work, we'll need a special PAM service profile which doesn't
     * actually do any authentication, only concerns itself with the enviornment
     * setup.  gdm-launch-environment is such one, and we use it if we haven't
     * got an IPRT specific one there.
     */
    else if (fNeedLoginEnv)
    {
        const char *pszService;
        if (rtProcPosixPamServiceExists("iprt-environment"))
            pszService = "iprt-environment";
# ifdef IPRT_PAM_NATIVE_SERVICE_NAME_ENVIRONMENT
        else if (rtProcPosixPamServiceExists(IPRT_PAM_NATIVE_SERVICE_NAME_ENVIRONMENT))
            pszService = IPRT_PAM_NATIVE_SERVICE_NAME_ENVIRONMENT;
# endif
        else if (rtProcPosixPamServiceExists("gdm-launch-environment"))
            pszService = "gdm-launch-environment";
        else
            pszService = NULL;
        if (pszService)
        {
            char szLoginName[512];
            rc = getlogin_r(szLoginName, sizeof(szLoginName));
            if (rc == 0)
                rc = rtProcPosixAuthenticateUsingPam(pszService, szLoginName, "xxx", &papszPamEnv, NULL);
        }
    }
#endif

    /*
     * Create the child environment if either RTPROC_FLAGS_PROFILE or
     * RTPROC_FLAGS_ENV_CHANGE_RECORD are in effect.
     */
    RTENV hEnvToUse = hEnv;
    if (   (fFlags & (RTPROC_FLAGS_ENV_CHANGE_RECORD | RTPROC_FLAGS_PROFILE))
        && (   (fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD)
            || hEnv == RTENV_DEFAULT) )
    {
        if (fFlags & RTPROC_FLAGS_PROFILE)
            rc = rtProcPosixCreateProfileEnv(&hEnvToUse, pszAsUser, uid, gid, fFlags, papszPamEnv);
        else
            rc = RTEnvClone(&hEnvToUse, RTENV_DEFAULT);
        rtProcPosixFreePamEnv(papszPamEnv);
        papszPamEnv = NULL;
        if (RT_FAILURE(rc))
            return rc;

        if ((fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD) && hEnv != RTENV_DEFAULT)
        {
            rc = RTEnvApplyChanges(hEnvToUse, hEnv);
            if (RT_FAILURE(rc))
            {
                RTEnvDestroy(hEnvToUse);
                return rc;
            }
        }
    }
    Assert(papszPamEnv == NULL);

    /*
     * Check for execute access to the file, searching the PATH if needed.
     */
    const char *pszNativeExec = NULL;
    rc = rtPathToNative(&pszNativeExec, pszExec, NULL);
    if (RT_SUCCESS(rc))
    {
        if (access(pszNativeExec, X_OK) == 0)
            rc = VINF_SUCCESS;
        else
        {
            rc = errno;
            rtPathFreeNative(pszNativeExec, pszExec);

            if (   !(fFlags & RTPROC_FLAGS_SEARCH_PATH)
                || rc != ENOENT
                || RTPathHavePath(pszExec) )
                rc = RTErrConvertFromErrno(rc);
            else
            {
                /* Search the PATH for it: */
                char *pszPath = RTEnvDupEx(hEnvToUse, "PATH");
                if (pszPath)
                {
                    PRTPATHINTSEARCH pResult = (PRTPATHINTSEARCH)alloca(sizeof(*pResult));
                    pResult->rcSticky = VINF_SUCCESS;
                    rc = RTPathTraverseList(pszPath, ':', rtPathFindExec, (void *)pszExec, pResult);
                    RTStrFree(pszPath);
                    if (RT_SUCCESS(rc))
                    {
                        /* Found it. Now, convert to native path: */
                        pszExec = pResult->szFound;
                        rc = rtPathToNative(&pszNativeExec, pszExec, NULL);
                    }
                    else
                        rc = rc != VERR_END_OF_STRING ? rc
                           : pResult->rcSticky == VINF_SUCCESS ? VERR_FILE_NOT_FOUND : pResult->rcSticky;
                }
                else
                    rc = VERR_NO_STR_MEMORY;
            }
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Convert arguments to child codeset if necessary.
             */
            char **papszArgsConverted = (char **)papszArgs;
            if (!(fFlags & RTPROC_FLAGS_UTF8_ARGV))
                rc = rtProcPosixConvertArgv(papszArgs, hEnvToUse, &papszArgsConverted);
            if (RT_SUCCESS(rc))
            {
                /*
                 * The rest of the process creation is reused internally by rtProcPosixCreateProfileEnv.
                 */
                rc = rtProcPosixCreateInner(pszNativeExec, papszArgsConverted, hEnv, hEnvToUse, fFlags, pszAsUser, uid, gid,
                                            RT_ELEMENTS(aStdFds), aStdFds, pvExtraData, phProcess);

            }

            /* Free the translated argv copy, if needed. */
            if (papszArgsConverted != (char **)papszArgs)
            {
                for (size_t i = 0; papszArgsConverted[i] != NULL; i++)
                    RTStrFree(papszArgsConverted[i]);
                RTMemFree(papszArgsConverted);
            }
            rtPathFreeNative(pszNativeExec, pszExec);
        }
    }
    if (hEnvToUse != hEnv)
        RTEnvDestroy(hEnvToUse);
    return rc;
}


/**
 * Creates the fork-status-pipe.
 *
 * This must be create as close-on-exec.
 *
 * @returns IPRT status code.
 * @param   pfdStatusPipeR  Where to return the read end.
 * @param   pfdStatusPipeW  Where to return the write end.
 */
static int rtProcPosixForkStatusPipeCreate(int *pfdStatusPipeR, int *pfdStatusPipeW)
{
    int aFds[2] = { -1, -1 };

#if defined(RT_OS_LINUX) && defined(__NR_pipe2) && defined(O_CLOEXEC)
    /* Try pipe2 first as it is race-free. */
    long rcSyscall = syscall(__NR_pipe2, aFds, O_CLOEXEC);
    if (rcSyscall >= 0)
    {
        *pfdStatusPipeR = aFds[0];
        *pfdStatusPipeW = aFds[1];
        return VINF_SUCCESS;
    }
#endif /** @todo solaris & freebsd has pipe2() */

    if (pipe(aFds) == 0)
    {
        int fOldFlags = fcntl(aFds[0], F_GETFD, 0);
        if (fOldFlags >= 0)
        {
            if (fcntl(aFds[0], F_SETFD, fOldFlags | FD_CLOEXEC) >= 0)
            {
                fOldFlags = fcntl(aFds[1], F_GETFD, 0);
                if (fOldFlags >= 0)
                {
                    if (fcntl(aFds[1], F_SETFD, fOldFlags | FD_CLOEXEC) >= 0)
                    {
                        *pfdStatusPipeR = aFds[0];
                        *pfdStatusPipeW = aFds[1];
                        return VINF_SUCCESS;
                    }
                }
            }
        }
        int iErrNoSaved = errno;
        close(aFds[0]);
        close(aFds[1]);
        errno = iErrNoSaved;
    }
    return RTErrConvertFromErrno(errno);
}


/**
 * Writes status to the fork-status-pipe and exits the process (child side).
 */
static DECL_NO_RETURN(void) rtProcPosixForkStatusPipeWrite(int fdStatusPipeW, int32_t rcStatus, int rcExit, uint32_t fFlags)
{
    ssize_t cbWritten;
    do
        cbWritten = write(fdStatusPipeW, &rcStatus, sizeof(rcStatus));
    while (cbWritten < 0 && errno == EINTR);
    Assert(cbWritten == (ssize_t)sizeof(rcStatus));

    close(fdStatusPipeW);

    if (fFlags & RTPROC_FLAGS_DETACHED)
        _Exit(rcExit);
    else
        exit(rcExit);
}


/**
 * Helper for closing the fork-status-pipe (parent side).
 */
static int rtProcPosixForkStatusPipeCleanupFailure(int rcRet, int fdStatusPipeR, int fdStatusPipeW)
{
    if (fdStatusPipeR >= 0)
        close(fdStatusPipeR);
    if (fdStatusPipeW >= 0)
        close(fdStatusPipeW);
    return rcRet;
}


/**
 * Helper that reads the status from fork-status-pipe and closes it (parent side).
 *
 * @returns Status from the fork-status-pipe.
 * @param   pid             The child pid, in case of failure where we have to
 *                          wait for it to terminate.
 * @param   fdStatusPipeR   The read pipe (read & then closed before returning).
 * @param   fdStatusPipeW   The write pipe (to be closed immediately).
 */
static int rtProcPosixForkStatusPipeReadAndCleanup(pid_t pid, int fdStatusPipeR, int fdStatusPipeW)
{
    /*
     * Should close the write end first to avoid deadlock trouble.
     */
    close(fdStatusPipeW);

    /*
     * Read and wait for the child side to report a problem or a successful execve.
     * In the latter case we end up with a zero byte return as the child end closes.
     */
    union
    {
        int32_t rcForkStatus;
        uint8_t ab[4];
    } uBuf = {{0}};
    ssize_t cbRead;
    do
        cbRead = read(fdStatusPipeR, &uBuf, sizeof(uBuf));
    while (cbRead < 0 && errno == EINTR);
    if (cbRead == 0)
    {
        close(fdStatusPipeR);
        LogFlow(("rtProcPosixForkStatusPipeReadAndCleanup: pid=%u -> VINF_SUCCESS\n", pid));
        return VINF_SUCCESS;
    }

    /*
     * This is almost impossible, but just in case the pipe implementation breaks...
     */
    AssertMsg(cbRead == (ssize_t)sizeof(uBuf), ("cbRead=%zd errno=%d\n", cbRead, errno));
    while ((size_t)cbRead < sizeof(uBuf))
    {
        ssize_t cbRead2;
        do
            cbRead2 = read(fdStatusPipeR, &uBuf.ab[cbRead], sizeof(uBuf) - cbRead);
        while (cbRead2 < 0 && errno == EINTR);
        AssertBreak(cbRead > 0);
        cbRead += cbRead2;
    }

    if (cbRead == (ssize_t)sizeof(uBuf))
        AssertMsgStmt(RT_FAILURE_NP(uBuf.rcForkStatus), ("%#x cbRead=%zu\n", uBuf.rcForkStatus, cbRead),
                      uBuf.rcForkStatus = VERR_INTERNAL_ERROR_4);
    else
        uBuf.rcForkStatus = VERR_INTERNAL_ERROR_5;
    close(fdStatusPipeR);
    LogFlow(("rtProcPosixForkStatusPipeReadAndCleanup: pid=%u -> %Rrc (%d)\n", pid, uBuf.rcForkStatus, uBuf.rcForkStatus));

    /*
     * Wait for the fork-child to terminate so we don't leave any zombies behind.
     */
    int   iStatusIgn = 0;
    pid_t pidWait;
    do
        pidWait = waitpid(pid, &iStatusIgn, 0);
    while (pidWait == -1 && errno == EINTR);

    LogFlow(("rtProcPosixForkStatusPipeReadAndCleanup: pid=%u -> pidWait=%d errno=%d rcForkStatus=%Rrc\n",
             pid, pidWait, errno, uBuf.rcForkStatus));
    return uBuf.rcForkStatus;
}


/**
 * The inner 2nd half of RTProcCreateEx.
 *
 * This is also used by rtProcPosixCreateProfileEnv().
 *
 * @returns IPRT status code.
 * @param   pszNativeExec   The executable to run (absolute path, X_OK).
 *                          Native path.
 * @param   papszArgs       The arguments.  Caller has done codeset conversions.
 * @param   hEnv            The original enviornment request, needed for
 *                          adjustments if starting as different user.
 * @param   hEnvToUse       The environment we should use.
 * @param   fFlags          The process creation flags, RTPROC_FLAGS_XXX.
 * @param   pszAsUser       The user to start the process as, if requested.
 * @param   uid             The UID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   gid             The GID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   cRedirFds       Number of redirection file descriptors.
 * @param   paRedirFds      Pointer to redirection file descriptors.  Entries
 *                          containing -1 are not modified (inherit from parent),
 *                          -2 indicates that the descriptor should be closed in the
 *                          child.
 * @param   phProcess       Where to return the process ID on success.
 */
static int rtProcPosixCreateInner(const char *pszNativeExec, const char * const *papszArgs, RTENV hEnv, RTENV hEnvToUse,
                                  uint32_t fFlags, const char *pszAsUser, uid_t uid, gid_t gid,
                                  unsigned cRedirFds, int *paRedirFds, void *pvExtraData, PRTPROCESS phProcess)
{
    /*
     * Get the environment block.
     */
    const char * const *papszEnv = RTEnvGetExecEnvP(hEnvToUse);
    AssertPtrReturn(papszEnv, VERR_INVALID_HANDLE);

    /*
     * Optimize the redirections.
     */
    while (cRedirFds > 0 && paRedirFds[cRedirFds - 1] == -1)
        cRedirFds--;

    /*
     * Child PID.
     */
    pid_t pid = -1;

    /*
     * Take care of detaching the process.
     *
     * HACK ALERT! Put the process into a new process group with pgid = pid
     * to make sure it differs from that of the parent process to ensure that
     * the IPRT waitpid call doesn't race anyone (read XPCOM) doing group wide
     * waits. setsid() includes the setpgid() functionality.
     * 2010-10-11 XPCOM no longer waits for anything, but it cannot hurt.
     */
#ifndef RT_OS_OS2
    if (fFlags & RTPROC_FLAGS_DETACHED)
    {
# ifdef RT_OS_SOLARIS
        int templateFd = -1;
        if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
        {
            templateFd = rtSolarisContractPreFork();
            if (templateFd == -1)
                return VERR_OPEN_FAILED;
        }
# endif /* RT_OS_SOLARIS */
        pid = fork();
        if (!pid)
        {
# ifdef RT_OS_SOLARIS
            if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
                rtSolarisContractPostForkChild(templateFd);
# endif
            setsid(); /* see comment above */

            pid = -1;
            /* Child falls through to the actual spawn code below. */
        }
        else
        {
# ifdef RT_OS_SOLARIS
            if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
                rtSolarisContractPostForkParent(templateFd, pid);
# endif
            if (pid > 0)
            {
                /* Must wait for the temporary process to avoid a zombie. */
                int status = 0;
                pid_t pidChild = 0;

                /* Restart if we get interrupted. */
                do
                {
                    pidChild = waitpid(pid, &status, 0);
                } while (   pidChild == -1
                         && errno == EINTR);

                /* Assume that something wasn't found. No detailed info. */
                if (status)
                    return VERR_PROCESS_NOT_FOUND;
                if (phProcess)
                    *phProcess = 0;
                return VINF_SUCCESS;
            }
            return RTErrConvertFromErrno(errno);
        }
    }
#endif

    /*
     * Spawn the child.
     *
     * Any spawn code MUST not execute any atexit functions if it is for a
     * detached process. It would lead to running the atexit functions which
     * make only sense for the parent. libORBit e.g. gets confused by multiple
     * execution. Remember, there was only a fork() so far, and until exec()
     * is successfully run there is nothing which would prevent doing anything
     * silly with the (duplicated) file descriptors.
     */
    int rc;
#ifdef HAVE_POSIX_SPAWN
    /** @todo OS/2: implement DETACHED (BACKGROUND stuff), see VbglR3Daemonize.  */
    if (   uid == ~(uid_t)0
        && gid == ~(gid_t)0
        && !(fFlags & RTPROC_FLAGS_CWD))
    {
        /* Spawn attributes. */
        posix_spawnattr_t Attr;
        rc = posix_spawnattr_init(&Attr);
        if (!rc)
        {
            /* Indicate that process group and signal mask are to be changed,
               and that the child should use default signal actions. */
            rc = posix_spawnattr_setflags(&Attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
            Assert(rc == 0);

            /* The child starts in its own process group. */
            if (!rc)
            {
                rc = posix_spawnattr_setpgroup(&Attr, 0 /* pg == child pid */);
                Assert(rc == 0);
            }

            /* Unmask all signals. */
            if (!rc)
            {
                sigset_t SigMask;
                sigemptyset(&SigMask);
                rc = posix_spawnattr_setsigmask(&Attr, &SigMask); Assert(rc == 0);
            }

            /* File changes. */
            posix_spawn_file_actions_t  FileActions;
            posix_spawn_file_actions_t *pFileActions = NULL;
            if (!rc && cRedirFds > 0)
            {
                rc = posix_spawn_file_actions_init(&FileActions);
                if (!rc)
                {
                    pFileActions = &FileActions;
                    for (unsigned i = 0; i < cRedirFds; i++)
                    {
                        int fd = paRedirFds[i];
                        if (fd == -2)
                            rc = posix_spawn_file_actions_addclose(&FileActions, i);
                        else if (fd >= 0 && fd != (int)i)
                        {
                            rc = posix_spawn_file_actions_adddup2(&FileActions, fd, i);
                            if (!rc)
                            {
                                for (unsigned j = i + 1; j < cRedirFds; j++)
                                    if (paRedirFds[j] == fd)
                                    {
                                        fd = -1;
                                        break;
                                    }
                                if (fd >= 0)
                                    rc = posix_spawn_file_actions_addclose(&FileActions, fd);
                            }
                        }
                        if (rc)
                            break;
                    }
                }
            }

            if (!rc)
                rc = posix_spawn(&pid, pszNativeExec, pFileActions, &Attr, (char * const *)papszArgs,
                                 (char * const *)papszEnv);

            /* cleanup */
            int rc2 = posix_spawnattr_destroy(&Attr); Assert(rc2 == 0); NOREF(rc2);
            if (pFileActions)
            {
                rc2 = posix_spawn_file_actions_destroy(pFileActions);
                Assert(rc2 == 0);
            }

            /* return on success.*/
            if (!rc)
            {
                /* For a detached process this happens in the temp process, so
                 * it's not worth doing anything as this process must exit. */
                if (fFlags & RTPROC_FLAGS_DETACHED)
                    _Exit(0);
                if (phProcess)
                    *phProcess = pid;
                return VINF_SUCCESS;
            }
        }
        /* For a detached process this happens in the temp process, so
         * it's not worth doing anything as this process must exit. */
        if (fFlags & RTPROC_FLAGS_DETACHED)
            _Exit(124);
    }
    else
#endif
    {
        int fdStatusPipeR, fdStatusPipeW;
        rc = rtProcPosixForkStatusPipeCreate(&fdStatusPipeR, &fdStatusPipeW);
        if (RT_FAILURE(rc))
            return rc;

#ifdef RT_OS_SOLARIS
        int templateFd = -1;
        if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
        {
            templateFd = rtSolarisContractPreFork();
            if (templateFd == -1)
                return rtProcPosixForkStatusPipeCleanupFailure(VERR_OPEN_FAILED, fdStatusPipeR, fdStatusPipeW);
        }
#endif /* RT_OS_SOLARIS */

        pid = fork();
        if (!pid)
        {
            /*
             * Child:
             */
            close(fdStatusPipeR);
#ifdef RT_OS_SOLARIS
            if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
                rtSolarisContractPostForkChild(templateFd);
#endif /* RT_OS_SOLARIS */
            if (!(fFlags & RTPROC_FLAGS_DETACHED))
                setpgid(0, 0); /* see comment above */

            /*
             * Change group and user if requested.
             */
#if 1 /** @todo This needs more work, see suplib/hardening. */
            if (pszAsUser)
            {
                int ret = initgroups(pszAsUser, gid);
                if (ret)
                    rtProcPosixForkStatusPipeWrite(fdStatusPipeW, RTErrConvertFromErrno(errno), 126, fFlags);
            }
            if (gid != ~(gid_t)0)
            {
                if (setgid(gid))
                    rtProcPosixForkStatusPipeWrite(fdStatusPipeW, RTErrConvertFromErrno(errno), 126, fFlags);
            }

            if (uid != ~(uid_t)0)
            {
                if (setuid(uid))
                    rtProcPosixForkStatusPipeWrite(fdStatusPipeW, RTErrConvertFromErrno(errno), 126, fFlags);
            }
#endif
            if (fFlags & RTPROC_FLAGS_CWD)
            {
                const char *pszCwd = (const char *)pvExtraData;
                if (pszCwd && *pszCwd)
                {
                    rc = RTPathSetCurrent(pszCwd);
                    if (RT_FAILURE(rc))
                        rtProcPosixForkStatusPipeWrite(fdStatusPipeW, rc, 126, fFlags);
                }
            }

            /*
             * Some final profile environment tweaks, if running as user.
             */
            if (   (fFlags & RTPROC_FLAGS_PROFILE)
                && pszAsUser
                && (   (fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD)
                    || hEnv == RTENV_DEFAULT) )
            {
                rc = rtProcPosixAdjustProfileEnvFromChild(hEnvToUse, fFlags, hEnv);
                papszEnv = RTEnvGetExecEnvP(hEnvToUse);
                if (RT_FAILURE(rc) || !papszEnv)
                    rtProcPosixForkStatusPipeWrite(fdStatusPipeW, RT_FAILURE_NP(rc) ? rc : VERR_NO_MEMORY, 126, fFlags);
            }

            /*
             * Unset the signal mask.
             */
            sigset_t SigMask;
            sigemptyset(&SigMask);
            rc = sigprocmask(SIG_SETMASK, &SigMask, NULL);
            Assert(rc == 0);

            /*
             * Apply changes to the standard file descriptor and stuff.
             */
            for (unsigned i = 0; i < cRedirFds; i++)
            {
                int fd = paRedirFds[i];
                if (fd == -2)
                    close(i);
                else if (fd >= 0)
                {
                    int rc2 = dup2(fd, i);
                    if (rc2 != (int)i)
                        rtProcPosixForkStatusPipeWrite(fdStatusPipeW, RTErrConvertFromErrno(errno), 125, fFlags);
                    for (unsigned j = i + 1; j < cRedirFds; j++)
                        if (paRedirFds[j] == fd)
                        {
                            fd = -1;
                            break;
                        }
                    if (fd >= 0)
                        close(fd);
                }
            }

            /*
             * Finally, execute the requested program.
             */
            rc = execve(pszNativeExec, (char * const *)papszArgs, (char * const *)papszEnv);
            if (errno == ENOEXEC)
            {
                /* This can happen when trying to start a shell script without the magic #!/bin/sh */
                RTAssertMsg2Weak("Cannot execute this binary format!\n");
                rtProcPosixForkStatusPipeWrite(fdStatusPipeW, VERR_BAD_EXE_FORMAT, 127, fFlags);
            }
            else if (errno == EACCES)
            {
                /* This can happen if file isn't marked as being executable or SELinux is enabled for prohibits using execve.*/
                RTAssertMsg2Weak("Permission denied executing this binary -- check execute permissions and/or SELinux policies!\n");
                rtProcPosixForkStatusPipeWrite(fdStatusPipeW, VERR_ACCESS_DENIED, 127, fFlags);
            }
            else
            {
                RTAssertMsg2Weak("execve returns %d errno=%d (%s)\n", rc, errno, pszNativeExec);
                rtProcPosixForkStatusPipeWrite(fdStatusPipeW, RTErrConvertFromErrno(errno), 127, fFlags);
            }
            /* end of child */
        }

        /*
         * Parent:
         */
#ifdef RT_OS_SOLARIS
        if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
            rtSolarisContractPostForkParent(templateFd, pid);
#endif /* RT_OS_SOLARIS */
        if (pid > 0)
        {
            /* Get the execv++ status from the pipe. */
            rc = rtProcPosixForkStatusPipeReadAndCleanup(pid, fdStatusPipeR, fdStatusPipeW);
            if (RT_SUCCESS(rc))
            {
                /* For a detached process this happens in the temp process, so
                 * it's not worth doing anything as this process must exit. */
                if (fFlags & RTPROC_FLAGS_DETACHED)
                    _Exit(0);
                if (phProcess)
                    *phProcess = pid;
                return VINF_SUCCESS;
            }
        }
        else
            rc = rtProcPosixForkStatusPipeCleanupFailure(RTErrConvertFromErrno(errno), fdStatusPipeR, fdStatusPipeW);

        /* For a detached process this happens in the temp process, so
         * it's not worth doing anything as this process must exit. */
        if (fFlags & RTPROC_FLAGS_DETACHED)
            _Exit(124);
        return rc;
    }

    return VERR_NOT_IMPLEMENTED;
}


RTR3DECL(int)   RTProcDaemonizeUsingFork(bool fNoChDir, bool fNoClose, const char *pszPidfile)
{
    /*
     * Fork the child process in a new session and quit the parent.
     *
     * - fork once and create a new session (setsid). This will detach us
     *   from the controlling tty meaning that we won't receive the SIGHUP
     *   (or any other signal) sent to that session.
     * - The SIGHUP signal is ignored because the session/parent may throw
     *   us one before we get to the setsid.
     * - When the parent exit(0) we will become an orphan and re-parented to
     *   the init process.
     * - Because of the sometimes unexpected semantics of assigning the
     *   controlling tty automagically when a session leader first opens a tty,
     *   we will fork() once more to get rid of the session leadership role.
     */

    /* We start off by opening the pidfile, so that we can fail straight away
     * if it already exists. */
    int fdPidfile = -1;
    if (pszPidfile != NULL)
    {
        /* @note the exclusive create is not guaranteed on all file
         * systems (e.g. NFSv2) */
        if ((fdPidfile = open(pszPidfile, O_RDWR | O_CREAT | O_EXCL, 0644)) == -1)
            return RTErrConvertFromErrno(errno);
    }

    /* Ignore SIGHUP straight away. */
    struct sigaction OldSigAct;
    struct sigaction SigAct;
    memset(&SigAct, 0, sizeof(SigAct));
    SigAct.sa_handler = SIG_IGN;
    int rcSigAct = sigaction(SIGHUP, &SigAct, &OldSigAct);

    /* First fork, to become independent process. */
    pid_t pid = fork();
    if (pid == -1)
    {
        if (fdPidfile != -1)
            close(fdPidfile);
        return RTErrConvertFromErrno(errno);
    }
    if (pid != 0)
    {
        /* Parent exits, no longer necessary. The child gets reparented
         * to the init process. */
        exit(0);
    }

    /* Create new session, fix up the standard file descriptors and the
     * current working directory. */
    /** @todo r=klaus the webservice uses this function and assumes that the
     * contract id of the daemon is the same as that of the original process.
     * Whenever this code is changed this must still remain possible. */
    pid_t newpgid = setsid();
    int SavedErrno = errno;
    if (rcSigAct != -1)
        sigaction(SIGHUP, &OldSigAct, NULL);
    if (newpgid == -1)
    {
        if (fdPidfile != -1)
            close(fdPidfile);
        return RTErrConvertFromErrno(SavedErrno);
    }

    if (!fNoClose)
    {
        /* Open stdin(0), stdout(1) and stderr(2) as /dev/null. */
        int fd = open("/dev/null", O_RDWR);
        if (fd == -1) /* paranoia */
        {
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            fd = open("/dev/null", O_RDWR);
        }
        if (fd != -1)
        {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2)
                close(fd);
        }
    }

    if (!fNoChDir)
    {
        int rcIgnored = chdir("/");
        NOREF(rcIgnored);
    }

    /* Second fork to lose session leader status. */
    pid = fork();
    if (pid == -1)
    {
        if (fdPidfile != -1)
            close(fdPidfile);
        return RTErrConvertFromErrno(errno);
    }

    if (pid != 0)
    {
        /* Write the pid file, this is done in the parent, before exiting. */
        if (fdPidfile != -1)
        {
            char szBuf[256];
            size_t cbPid = RTStrPrintf(szBuf, sizeof(szBuf), "%d\n", pid);
            ssize_t cbIgnored = write(fdPidfile, szBuf, cbPid); NOREF(cbIgnored);
            close(fdPidfile);
        }
        exit(0);
    }

    if (fdPidfile != -1)
        close(fdPidfile);

    return VINF_SUCCESS;
}

