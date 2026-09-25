/* $Id: init-linux.cpp 115325 2026-09-25 17:49:50Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Init Ring-3, POSIX Specific Code.
 */

/*
 * Copyright (C) 2025-2026 Oracle and/or its affiliates.
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
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/thread.h>
#include "../init.h"

#include "internal/thread.h"

#include <signal.h>
#include <ucontext.h>
#include <link.h>
#include <fcntl.h>
#include <unistd.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Arguments for the iterate shared objects callback.
 */
typedef struct RTITSOARGS
{
    PRTLOGGER pLogger;
    uintptr_t uXcptPC;
} RTITSOARGS;
typedef RTITSOARGS *PRTITSOARGS;
typedef const RTITSOARGS *PCRTITSOARGS;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static struct sigaction g_SigActionSegv;  /**< The default action for SIGSEGV. */
static struct sigaction g_SigActionBus;   /**< The default action for SIGBUS. */
static struct sigaction g_SigActionAbort; /**< The default action for SIGABRT. */



/**
 * Tries to log information about the given shared object.
 */
static int rtR3InitLnxIterateSharedObjects(struct dl_phdr_info *pDlInfo, size_t cbInfo, void *pvUser)
{
    PCRTITSOARGS pArgs = (PCRTITSOARGS)pvUser;
    PRTLOGGER pLogger = pArgs->pLogger;
    uintptr_t const uXcptPC = pArgs->uXcptPC;

    AssertReturn(cbInfo >= sizeof(*pDlInfo), 0);

    /* Iterate over the program headers and dump the executable segments. */
    for (uint32_t i = 0; i < pDlInfo->dlpi_phnum; i++)
    {
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_ARM64)
        const Elf64_Phdr *pPHdr = &pDlInfo->dlpi_phdr[i];
#elif defined(RT_ARCH_X86)
        const Elf32_Phdr *pPHdr = &pDlInfo->dlpi_phdr[i];
#else
# error "Port me"
#endif
        uint32_t const fFlags = pPHdr->p_flags;

        if (fFlags & PF_X)
        {
            char chInd = ' ';
            uintptr_t uAddrStart = pDlInfo->dlpi_addr;
            uintptr_t uAddrEnd   = uAddrStart + (uintptr_t)pPHdr->p_vaddr + pPHdr->p_memsz - 1;

            if (uXcptPC >= uAddrStart && uXcptPC <= uAddrEnd)
                chInd = '*';

            RTLogLoggerWeak(pLogger, NULL, "%p..%p%c  %s\n",
                            uAddrStart, uAddrEnd, chInd, pDlInfo->dlpi_name);
        }
    }

    return 0;
}


/**
 * Signal handler callback.
 *
 * Will try log stuff.
 */
static void rtR3LnxSigSegvBusHandler(int iSignum, siginfo_t *pSigInfo, void *pvContext)
{
    /* Restore the default handler so we do the default action after we finished. */
    struct sigaction *pAction = NULL;
    if (iSignum == SIGSEGV)
        pAction = &g_SigActionSegv;
    else if (iSignum == SIGBUS)
        pAction = &g_SigActionBus;
    else
        pAction = &g_SigActionAbort;
    sigaction(iSignum, pAction, NULL);

    /*
     * Try get the logger and log exception details.
     *
     * Note! We'll be using RTLogLoggerWeak for now, though we should probably add
     *       a less deadlock prone API here and gives up pretty fast if it
     *       cannot get the lock...
     */
    PRTLOGGER pLogger = RTLogRelGetDefaultInstanceWeak();
    if (!pLogger)
        pLogger = RTLogGetDefaultInstanceWeak();
    if (pLogger)
    {
        RTLogLoggerWeak(pLogger, NULL, "\n!!! rtR3LnxSigSegvBusHandler caught an exception on thread %p in %u !!!\n",
                        RTThreadNativeSelf(), RTProcSelf());

        /*
         * Dump the signal info.
         */
        RTLogLoggerWeak(pLogger, NULL,  "\nsi_signo=%RI32 si_code=%RI32 si_pid=%RI32\n"
                                        "si_uid=%RI32 si_addr=%p si_status=%RI32\n",
                        pSigInfo->si_signo, pSigInfo->si_code, pSigInfo->si_pid,
                        pSigInfo->si_uid, pSigInfo->si_addr, pSigInfo->si_status);

        /* Dump stack information. */
        ucontext_t *pCtx = (ucontext_t *)pvContext;
        RTLogLoggerWeak(pLogger, NULL,  "\nuc_stack.ss_sp=%p uc_stack.ss_flags=%RX32 uc_stack.ss_size=%zu\n",
                        pCtx->uc_stack.ss_sp, pCtx->uc_stack.ss_flags, pCtx->uc_stack.ss_size);

        /*
         * Dump the machine context.
         */
        uintptr_t     uXcptPC  = 0;
        uintptr_t     uXcptSP  = 0;
        mcontext_t   *pXcptCtx = &pCtx->uc_mcontext;
#ifdef RT_ARCH_AMD64
        RTLogLoggerWeak(pLogger, NULL, "\ncs:rip=%04x:%016RX64\n",
                        pXcptCtx->gregs[REG_CSGSFS], pXcptCtx->gregs[REG_RIP]);
        RTLogLoggerWeak(pLogger, NULL, "ss:rsp=%04x:%016RX64 rbp=%016RX64\n",
                        pXcptCtx->gregs[REG_CSGSFS], pXcptCtx->gregs[REG_RSP], pXcptCtx->gregs[REG_RBP]);
        RTLogLoggerWeak(pLogger, NULL, "rax=%016RX64 rcx=%016RX64 rdx=%016RX64 rbx=%016RX64\n",
                        pXcptCtx->gregs[REG_RAX], pXcptCtx->gregs[REG_RCX], pXcptCtx->gregs[REG_RDX], pXcptCtx->gregs[REG_RBX]);
        RTLogLoggerWeak(pLogger, NULL, "rsi=%016RX64 rdi=%016RX64 rsp=%016RX64 rbp=%016RX64\n",
                        pXcptCtx->gregs[REG_RSI], pXcptCtx->gregs[REG_RDI], pXcptCtx->gregs[REG_RSP], pXcptCtx->gregs[REG_RBP]);
        RTLogLoggerWeak(pLogger, NULL, "r8 =%016RX64 r9 =%016RX64 r10=%016RX64 r11=%016RX64\n",
                        pXcptCtx->gregs[REG_R8],  pXcptCtx->gregs[REG_R9],  pXcptCtx->gregs[REG_R10], pXcptCtx->gregs[REG_R11]);
        RTLogLoggerWeak(pLogger, NULL, "r12=%016RX64 r13=%016RX64 r14=%016RX64 r15=%016RX64\n",
                        pXcptCtx->gregs[REG_R12],  pXcptCtx->gregs[REG_R13],  pXcptCtx->gregs[REG_R14], pXcptCtx->gregs[REG_R15]);
        RTLogLoggerWeak(pLogger, NULL, "fs=%04x gs=%04x eflags=%08x\n",
                        pXcptCtx->gregs[REG_CSGSFS], pXcptCtx->gregs[REG_CSGSFS], pXcptCtx->gregs[REG_EFL]);

        uXcptSP = pXcptCtx->gregs[REG_RSP];
        uXcptPC = pXcptCtx->gregs[REG_RIP];

#elif defined(RT_ARCH_X86)
        RTLogLoggerWeak(pLogger, NULL, "\ncs:eip=%04x:%08RX32\n", pXcptCtx->gregs[REG_CS], pXcptCtx->gregs[REG_EIP]);
        RTLogLoggerWeak(pLogger, NULL, "ss:esp=%04x:%08RX32 ebp=%08RX32\n",
                        pXcptCtx->gregs[REG_SS], pXcptCtx->gregs[REG_ESP], pXcptCtx->gregs[REG_EBP]);
        RTLogLoggerWeak(pLogger, NULL, "eax=%08RX32 ecx=%08RX32 edx=%08RX32 ebx=%08RX32\n",
                        pXcptCtx->gregs[REG_EAX], pXcptCtx->gregs[REG_ECX], pXcptCtx->gregs[REG_EDX], pXcptCtx->gregs[REG_EBX]);
        RTLogLoggerWeak(pLogger, NULL, "esi=%08RX32 edi=%08RX32 esp=%08RX32 ebp=%08RX32\n",
                        pXcptCtx->gregs[REG_ESI], pXcptCtx->gregs[REG_EDI], pXcptCtx->gregs[REG_ESI], pXcptCtx->gregs[REG_EBP]);
        RTLogLoggerWeak(pLogger, NULL, "ds=%04x es=%04x fs=%04x gs=%04x eflags=%08x\n",
                        pXcptCtx->gregs[REG_DS], pXcptCtx->gregs[REG_ES], pXcptCtx->gregs[REG_FS],
                        pXcptCtx->gregs[REG_GS], pXcptCtx->gregs[REG_EFL]);
        uXcptSP = pXcptCtx->gregs[REG_ESP];
        uXcptPC = pXcptCtx->gregs[REG_EIP];

#elif defined(RT_ARCH_ARM64)
        RTLogLoggerWeak(pLogger, NULL, "\npc=%016RX64 pstate=%016RX64\n", pXcptCtx->pc, pXcptCtx->pstate);
        RTLogLoggerWeak(pLogger, NULL, "sp=%016RX64\n", pXcptCtx->sp);
        RTLogLoggerWeak(pLogger, NULL, "r0=%016RX64 r1=%016RX64 r2=%016RX64 r3=%016RX64\n",
                        pXcptCtx->regs[0], pXcptCtx->regs[1], pXcptCtx->regs[2], pXcptCtx->regs[3]);
        RTLogLoggerWeak(pLogger, NULL, "r4=%016RX64 r5=%016RX64 r6=%016RX64 r7=%016RX64\n",
                        pXcptCtx->regs[4], pXcptCtx->regs[5], pXcptCtx->regs[6], pXcptCtx->regs[7]);
        RTLogLoggerWeak(pLogger, NULL, "r8=%016RX64 r9=%016RX64 r10=%016RX64 r11=%016RX64\n",
                        pXcptCtx->regs[8], pXcptCtx->regs[9], pXcptCtx->regs[10], pXcptCtx->regs[11]);
        RTLogLoggerWeak(pLogger, NULL, "r12=%016RX64 r13=%016RX64 r14=%016RX64 r15=%016RX64\n",
                        pXcptCtx->regs[12], pXcptCtx->regs[13], pXcptCtx->regs[14], pXcptCtx->regs[15]);
        RTLogLoggerWeak(pLogger, NULL, "r16=%016RX64 r17=%016RX64 r18=%016RX64 r19=%016RX64\n",
                        pXcptCtx->regs[16], pXcptCtx->regs[17], pXcptCtx->regs[18], pXcptCtx->regs[19]);
        RTLogLoggerWeak(pLogger, NULL, "r20=%016RX64 r21=%016RX64 r22=%016RX64 r23=%016RX64\n",
                        pXcptCtx->regs[20], pXcptCtx->regs[21], pXcptCtx->regs[22], pXcptCtx->regs[23]);
        RTLogLoggerWeak(pLogger, NULL, "r24=%016RX64 r25=%016RX64 r26=%016RX64 r27=%016RX64\n",
                        pXcptCtx->regs[24], pXcptCtx->regs[25], pXcptCtx->regs[26], pXcptCtx->regs[27]);
        RTLogLoggerWeak(pLogger, NULL, "r28=%016RX64 r29=%016RX64 r30=%016RX64\n",
                        pXcptCtx->regs[28], pXcptCtx->regs[29], pXcptCtx->regs[30]);
        uXcptSP = pXcptCtx->sp;
        uXcptPC = pXcptCtx->pc;
#endif

        /*
         * Dump stack.
         */
        uintptr_t uStack = uXcptSP;
        uStack -= uStack & 15;

        /* Dump at least a page. */
        uint32_t const cbPage = RTSystemGetPageSize();
        size_t cbToDump = cbPage - (uStack & RTSystemGetPageOffsetMask());
        uintptr_t uTop = 0;

        /* Try to figure out the stack top, this doesn't work for adopted or the main thread. */
        RTTHREAD hSelf = RTThreadSelf();
        if (hSelf != NIL_RTTHREAD)
        {
            PRTTHREADINT pThread = rtThreadGet(hSelf);
            if (pThread)
            {
                if (!(pThread->fIntFlags & (RTTHREADINT_FLAGS_ALIEN | RTTHREADINT_FLAGS_MAIN)))
                {
                    uTop = (uintptr_t)rtThreadGetStackBase(pThread);
                    cbToDump = uTop - uStack;
                }
                rtThreadRelease(pThread);
            }
        }

        RTLogLoggerWeak(pLogger, NULL, "\nStack %p, dumping %#zx bytes (top %p)\n", uStack, cbToDump, uTop);
        RTLogLoggerWeak(pLogger, NULL, "%.*RhxD\n", cbToDump, uStack);

        /*
         * Try figure the thread name.
         *
         * Note! This involves the thread db lock, so it may deadlock, which
         *       is why it's at the end.
         */
        RTLogLoggerWeak(pLogger, NULL,  "Thread ID:   %p\n", RTThreadNativeSelf());
        RTLogLoggerWeak(pLogger, NULL,  "Thread name: %s\n", RTThreadSelfName());
        RTLogLoggerWeak(pLogger, NULL,  "Thread IPRT: %p\n", hSelf);

        /*
         * Try dump the load information.
         */
        RTLogLoggerWeak(pLogger, NULL,
                        "\nLoaded Modules:\n"
                        "%-*s[*] Path\n", sizeof(void *) * 4 + 2 - 1, "Address range"
                        );
        RTITSOARGS Args;
        Args.pLogger = pLogger;
        Args.uXcptPC = uXcptPC;
        dl_iterate_phdr(rtR3InitLnxIterateSharedObjects, &Args);

        /** @todo Dump /proc/self/maps ? */

        /*
         * Dump the command line. We do this last in case it crashes.
         */
        int iFd = -1;
        iFd = open("/proc/self/cmdline", O_RDONLY);
        if (iFd != -1)
        {
            /* Keep it simple and use a static buffer. */
            char aszCmdline[_1K];
            ssize_t cbRead = read(iFd, &aszCmdline[0], sizeof(aszCmdline) - 1);
            close(iFd);
            if (cbRead > 0)
            {
                aszCmdline[cbRead] = '\0'; /* Terminate */
                RTLogLoggerWeak(pLogger, NULL, "\nCommandLine: ");

                ssize_t off = 0;
                while (off < cbRead)
                {
                    RTLogLoggerWeak(pLogger, NULL, "%s\n", &aszCmdline[off]);
                    off += strlen(&aszCmdline[off]) + 1;
                }
                RTLogLoggerWeak(pLogger, NULL, "\n");
            }
        }
    }
}


static int rtR3InitNativeObtrusiveWorker(uint32_t fFlags)
{
    RT_NOREF(fFlags);

    /* Install our own SIGSEGV/SIGBUS/SIGABORT handlers. */
    struct sigaction Action; RT_ZERO(Action);
    Action.sa_flags     = SA_SIGINFO;
    Action.sa_sigaction = rtR3LnxSigSegvBusHandler;
    sigaction(SIGSEGV, &Action, &g_SigActionSegv);
    sigaction(SIGBUS,  &Action, &g_SigActionBus);
    sigaction(SIGBUS,  &Action, &g_SigActionAbort);
    /* Ignore errors. */

    return VINF_SUCCESS;
}


DECLHIDDEN(int)  rtR3InitNativeFirst(uint32_t fFlags)
{
    int rc = VINF_SUCCESS;
    if (!(fFlags & RTR3INIT_FLAGS_UNOBTRUSIVE))
        rc = rtR3InitNativeObtrusiveWorker(fFlags);

    return rc;
}


DECLHIDDEN(void) rtR3InitNativeObtrusive(uint32_t fFlags)
{
    rtR3InitNativeObtrusiveWorker(fFlags);
}


DECLHIDDEN(int)  rtR3InitNativeFinal(uint32_t fFlags)
{
    /* Nothing to do here. */
    RT_NOREF_PV(fFlags);
    return VINF_SUCCESS;
}

