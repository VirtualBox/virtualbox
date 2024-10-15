/* $Id: RTProcSignalName-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTProcSignalName, generic implementation.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#include <iprt/process.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/string.h>

#include <signal.h>


RTDECL(const char *) RTProcSignalName(int iSignal)
{
#if !defined(IPRT_NO_CRT) || !defined(RT_OS_WINDOWS)
    switch (iSignal)
    {
        /*
         * Typical bsd/xnu ones:
         */
# ifdef SIGHUP
        RT_CASE_RET_STR(SIGHUP);
# endif
# ifdef SIGINT
        RT_CASE_RET_STR(SIGINT);
# endif
# ifdef SIGQUIT
        RT_CASE_RET_STR(SIGQUIT);
# endif
# ifdef SIGILL
        RT_CASE_RET_STR(SIGILL);
# endif
# ifdef SIGTRAP
        RT_CASE_RET_STR(SIGTRAP);
# endif
# ifdef SIGABRT
        RT_CASE_RET_STR(SIGABRT);
# endif
# ifdef SIGEMT
        RT_CASE_RET_STR(SIGEMT);
# endif
# ifdef SIGPOLL
        RT_CASE_RET_STR(SIGPOLL);
# endif
# ifdef SIGFPE
        RT_CASE_RET_STR(SIGFPE);
# endif
# ifdef SIGKILL
        RT_CASE_RET_STR(SIGKILL);
# endif
# ifdef SIGBUS
        RT_CASE_RET_STR(SIGBUS);
# endif
# ifdef SIGSEGV
        RT_CASE_RET_STR(SIGSEGV);
# endif
# ifdef SIGSYS
        RT_CASE_RET_STR(SIGSYS);
# endif
# ifdef SIGPIPE
        RT_CASE_RET_STR(SIGPIPE);
# endif
# ifdef SIGALRM
        RT_CASE_RET_STR(SIGALRM);
# endif
# ifdef SIGTERM
        RT_CASE_RET_STR(SIGTERM);
# endif
# ifdef SIGURG
        RT_CASE_RET_STR(SIGURG);
# endif
# ifdef SIGSTOP
        RT_CASE_RET_STR(SIGSTOP);
# endif
# ifdef SIGTSTP
        RT_CASE_RET_STR(SIGTSTP);
# endif
# ifdef SIGCONT
        RT_CASE_RET_STR(SIGCONT);
# endif
# ifdef SIGCHLD
        RT_CASE_RET_STR(SIGCHLD);
# endif
# ifdef SIGTTIN
        RT_CASE_RET_STR(SIGTTIN);
# endif
# ifdef SIGTTOU
        RT_CASE_RET_STR(SIGTTOU);
# endif
# ifdef SIGIO
#  if !defined(SIGPOLL) || (SIGPOLL+0) != SIGIO
        RT_CASE_RET_STR(SIGIO);
#  endif
# endif
# ifdef SIGXCPU
        RT_CASE_RET_STR(SIGXCPU);
# endif
# ifdef SIGXFSZ
        RT_CASE_RET_STR(SIGXFSZ);
# endif
# ifdef SIGVTALRM
        RT_CASE_RET_STR(SIGVTALRM);
# endif
# ifdef SIGPROF
        RT_CASE_RET_STR(SIGPROF);
# endif
# ifdef SIGWINCH
        RT_CASE_RET_STR(SIGWINCH);
# endif
# ifdef SIGINFO
        RT_CASE_RET_STR(SIGINFO);
# endif
# ifdef SIGUSR1
        RT_CASE_RET_STR(SIGUSR1);
# endif
# ifdef SIGUSR2
        RT_CASE_RET_STR(SIGUSR2);
# endif
# ifdef SIGTHR
        RT_CASE_RET_STR(SIGTHR);
# endif
# ifdef SIGLIBRT
        RT_CASE_RET_STR(SIGLIBRT);
# endif

        /*
         * Additional linux ones:
         */
# ifdef SIGIOT
#  if !defined(SIGABRT) || (SIGABRT+0) != SIGIOT
        RT_CASE_RET_STR(SIGIOT);
#  endif
# endif
# ifdef SIGSTKFLT
        RT_CASE_RET_STR(SIGSTKFLT);
# endif
# ifdef SIGLOST
        RT_CASE_RET_STR(SIGLOST);
# endif
# ifdef SIGPWR
        RT_CASE_RET_STR(SIGPWR);
# endif
# ifdef SIGUNUSED
#  if !defined(SIGSYS) || (SIGSYS+0) != SIGUNUSED
        RT_CASE_RET_STR(SIGUNUSED);
#  endif
# endif
    }
#endif

#if defined(SIGRTMIN) && defined(SIGRTMAX)
    /*
     * Real time signals.
     *
     * This cannot be done in the switch/case setup because glibc maps SIGRTMIN
     * and SIGRTMAX to internal libc calls that dynamically resolves their values.
     */
    static char const s_szSigRt[] =
        "SIGRT00\0" "SIGRT01\0" "SIGRT02\0" "SIGRT03\0" "SIGRT04\0" "SIGRT05\0" "SIGRT06\0" "SIGRT07\0" "SIGRT08\0" "SIGRT09\0"
        "SIGRT10\0" "SIGRT11\0" "SIGRT12\0" "SIGRT13\0" "SIGRT14\0" "SIGRT15\0" "SIGRT16\0" "SIGRT17\0" "SIGRT18\0" "SIGRT19\0"
        "SIGRT20\0" "SIGRT21\0" "SIGRT22\0" "SIGRT23\0" "SIGRT24\0" "SIGRT25\0" "SIGRT26\0" "SIGRT27\0" "SIGRT28\0" "SIGRT29\0"
        "SIGRT30\0" "SIGRT31\0" "SIGRT32\0" "SIGRT33\0" "SIGRT34\0" "SIGRT35\0" "SIGRT36\0" "SIGRT37\0" "SIGRT38\0" "SIGRT39\0"
        "SIGRT40\0" "SIGRT41\0" "SIGRT42\0" "SIGRT43\0" "SIGRT44\0" "SIGRT45\0" "SIGRT46\0" "SIGRT47\0" "SIGRT48\0" "SIGRT49\0"
        "SIGRT50\0" "SIGRT51\0" "SIGRT52\0" "SIGRT53\0" "SIGRT54\0" "SIGRT55\0" "SIGRT56\0" "SIGRT57\0" "SIGRT58\0" "SIGRT59\0"
        "SIGRT60\0" "SIGRT61\0" "SIGRT62\0" "SIGRT63\0" "SIGRT64\0";
    int const iSigRtMin = SIGRTMIN;
    if (iSignal >= iSigRtMin && iSignal <= SIGRTMAX)
    {
        unsigned uRtSig = (unsigned)(iSignal - iSigRtMin);
        if (uRtSig < (sizeof(s_szSigRt) - 1) / sizeof("SIGRT00"))
            return &s_szSigRt[uRtSig * sizeof("SIGRT00")];
    }
#endif /* SIGRTMIN */

    /*
     * Do fallback: SIG+nnnn.
     */
    static struct { char sz[16]; } s_aFallback[16];
    static uint32_t volatile       s_iFallback = 0;
    uint32_t const iFallback = ASMAtomicIncU32(&s_iFallback) % RT_ELEMENTS(s_aFallback);
    s_aFallback[iFallback].sz[0] = 'S';
    s_aFallback[iFallback].sz[1] = 'I';
    s_aFallback[iFallback].sz[2] = 'G';
    RTStrFormatU32(&s_aFallback[iFallback].sz[3], sizeof(s_aFallback[iFallback].sz) - 3, iSignal,
                   10, 0, 0, RTSTR_F_PLUS | RTSTR_F_VALSIGNED);
    return s_aFallback[iFallback].sz;
}
RT_EXPORT_SYMBOL(RTProcSignalName);

