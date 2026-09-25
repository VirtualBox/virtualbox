/* $Id: init-darwin.cpp 115325 2026-09-25 17:49:50Z knut.osmundsen@oracle.com $ */
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
#define _XOPEN_SOURCE
#include <ucontext.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <mach/mach_vm.h>
#include <sys/types.h>
#include <sys/sysctl.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* Define any missing VM_MEMORY_XXX constants to make sure this compiles with older SDKs. */
#ifndef VM_MEMORY_MALLOC_LARGE_REUSABLE
# define VM_MEMORY_MALLOC_LARGE_REUSABLE                8
#endif
#ifndef VM_MEMORY_MALLOC_LARGE_REUSED
# define VM_MEMORY_MALLOC_LARGE_REUSED                  9
#endif
#ifndef VM_MEMORY_MALLOC_NANO
# define VM_MEMORY_MALLOC_NANO                          11
#endif
#ifndef VM_MEMORY_MALLOC_MEDIUM
# define VM_MEMORY_MALLOC_MEDIUM                        12
#endif
#ifndef VM_MEMORY_MALLOC_PROB_GUARD
# define VM_MEMORY_MALLOC_PROB_GUARD                    13
#endif
#ifndef VM_MEMORY_OBJC_DISPATCHERS
# define VM_MEMORY_OBJC_DISPATCHERS                     34
#endif
#ifndef VM_MEMORY_UNSHARED_PMAP
# define VM_MEMORY_UNSHARED_PMAP                        35
#endif
#ifndef VM_MEMORY_CORESERVICES
# define VM_MEMORY_CORESERVICES                         43
#endif
#ifndef VM_MEMORY_COREDATA
# define VM_MEMORY_COREDATA                             45
#endif
#ifndef VM_MEMORY_COREDATA_OBJECTIDS
# define VM_MEMORY_COREDATA_OBJECTIDS                   46
#endif
#ifndef VM_MEMORY_LAYERKIT
# define VM_MEMORY_LAYERKIT                             51
#endif
#ifndef VM_MEMORY_CGIMAGE
# define VM_MEMORY_CGIMAGE                              52
#endif
#ifndef VM_MEMORY_TCMALLOC
# define VM_MEMORY_TCMALLOC                             53
#endif
#ifndef VM_MEMORY_COREGRAPHICS_DATA
# define VM_MEMORY_COREGRAPHICS_DATA                    54
#endif
#ifndef VM_MEMORY_COREGRAPHICS_SHARED
# define VM_MEMORY_COREGRAPHICS_SHARED                  55
#endif
#ifndef VM_MEMORY_COREGRAPHICS_FRAMEBUFFERS
# define VM_MEMORY_COREGRAPHICS_FRAMEBUFFERS            56
#endif
#ifndef VM_MEMORY_COREGRAPHICS_BACKINGSTORES
# define VM_MEMORY_COREGRAPHICS_BACKINGSTORES           57
#endif
#ifndef VM_MEMORY_COREGRAPHICS_XALLOC
# define VM_MEMORY_COREGRAPHICS_XALLOC                  58
#endif
#ifndef VM_MEMORY_SQLITE
# define VM_MEMORY_SQLITE                               62
#endif
#ifndef VM_MEMORY_JAVASCRIPT_CORE
# define VM_MEMORY_JAVASCRIPT_CORE                      63
#endif
#ifndef VM_MEMORY_JAVASCRIPT_JIT_EXECUTABLE_ALLOCATOR
# define VM_MEMORY_JAVASCRIPT_JIT_EXECUTABLE_ALLOCATOR  64
#endif
#ifndef VM_MEMORY_JAVASCRIPT_JIT_REGISTER_FILE
# define VM_MEMORY_JAVASCRIPT_JIT_REGISTER_FILE         65
#endif
#ifndef VM_MEMORY_GLSL
# define VM_MEMORY_GLSL                                 66
#endif
#ifndef VM_MEMORY_OPENCL
# define VM_MEMORY_OPENCL                               67
#endif
#ifndef VM_MEMORY_COREIMAGE
# define VM_MEMORY_COREIMAGE                            68
#endif
#ifndef VM_MEMORY_WEBCORE_PURGEABLE_BUFFERS
# define VM_MEMORY_WEBCORE_PURGEABLE_BUFFERS            69
#endif
#ifndef VM_MEMORY_IMAGEIO
# define VM_MEMORY_IMAGEIO                              70
#endif
#ifndef VM_MEMORY_COREPROFILE
# define VM_MEMORY_COREPROFILE                          71
#endif
#ifndef VM_MEMORY_ASSETSD
# define VM_MEMORY_ASSETSD                              72
#endif
#ifndef VM_MEMORY_OS_ALLOC_ONCE
# define VM_MEMORY_OS_ALLOC_ONCE                        73
#endif
#ifndef VM_MEMORY_LIBDISPATCH
# define VM_MEMORY_LIBDISPATCH                          74
#endif
#ifndef VM_MEMORY_ACCELERATE
# define VM_MEMORY_ACCELERATE                           75
#endif
#ifndef VM_MEMORY_COREUI
# define VM_MEMORY_COREUI                               76
#endif
#ifndef VM_MEMORY_COREUIFILE
# define VM_MEMORY_COREUIFILE                           77
#endif
#ifndef VM_MEMORY_GENEALOGY
# define VM_MEMORY_GENEALOGY                            78
#endif
#ifndef VM_MEMORY_RAWCAMERA
# define VM_MEMORY_RAWCAMERA                            79
#endif
#ifndef VM_MEMORY_CORPSEINFO
# define VM_MEMORY_CORPSEINFO                           80
#endif
#ifndef VM_MEMORY_ASL
# define VM_MEMORY_ASL                                  81
#endif
#ifndef VM_MEMORY_SWIFT_RUNTIME
# define VM_MEMORY_SWIFT_RUNTIME                        82
#endif
#ifndef VM_MEMORY_SWIFT_METADATA
# define VM_MEMORY_SWIFT_METADATA                       83
#endif
#ifndef VM_MEMORY_DHMM
# define VM_MEMORY_DHMM                                 84
#endif
#ifndef VM_MEMORY_SCENEKIT
# define VM_MEMORY_SCENEKIT                             86
#endif
#ifndef VM_MEMORY_SKYWALK
# define VM_MEMORY_SKYWALK                              87
#endif
#ifndef VM_MEMORY_IOSURFACE
# define VM_MEMORY_IOSURFACE                            88
#endif
#ifndef VM_MEMORY_LIBNETWORK
# define VM_MEMORY_LIBNETWORK                           89
#endif
#ifndef VM_MEMORY_AUDIO
# define VM_MEMORY_AUDIO                                90
#endif
#ifndef VM_MEMORY_VIDEOBITSTREAM
# define VM_MEMORY_VIDEOBITSTREAM                       91
#endif
#ifndef VM_MEMORY_CM_XPC
# define VM_MEMORY_CM_XPC                               92
#endif
#ifndef VM_MEMORY_CM_RPC
# define VM_MEMORY_CM_RPC                               93
#endif
#ifndef VM_MEMORY_CM_MEMORYPOOL
# define VM_MEMORY_CM_MEMORYPOOL                        94
#endif
#ifndef VM_MEMORY_CM_READCACHE
# define VM_MEMORY_CM_READCACHE                         95
#endif
#ifndef VM_MEMORY_CM_CRABS
# define VM_MEMORY_CM_CRABS                             96
#endif
#ifndef VM_MEMORY_QUICKLOOK_THUMBNAILS
# define VM_MEMORY_QUICKLOOK_THUMBNAILS                 97
#endif
#ifndef VM_MEMORY_ACCOUNTS
# define VM_MEMORY_ACCOUNTS                             98
#endif
#ifndef VM_MEMORY_SANITIZER
# define VM_MEMORY_SANITIZER                            99
#endif
#ifndef VM_MEMORY_IOACCELERATOR
# define VM_MEMORY_IOACCELERATOR                        100
#endif
#ifndef VM_MEMORY_CM_REGWARP
# define VM_MEMORY_CM_REGWARP                           101
#endif
#ifndef VM_MEMORY_EAR_DECODER
# define VM_MEMORY_EAR_DECODER                          102
#endif
#ifndef VM_MEMORY_COREUI_CACHED_IMAGE_DATA
# define VM_MEMORY_COREUI_CACHED_IMAGE_DATA             103
#endif
#ifndef VM_MEMORY_COLORSYNC
# define VM_MEMORY_COLORSYNC                            104
#endif
#ifndef VM_MEMORY_BTINFO
# define VM_MEMORY_BTINFO                               105
#endif
#ifndef VM_MEMORY_CM_HLS
# define VM_MEMORY_CM_HLS                               106
#endif
#ifndef VM_MEMORY_ROSETTA
# define VM_MEMORY_ROSETTA                              230
#endif
#ifndef VM_MEMORY_ROSETTA_THREAD_CONTEXT
# define VM_MEMORY_ROSETTA_THREAD_CONTEXT               231
#endif
#ifndef VM_MEMORY_ROSETTA_INDIRECT_BRANCH_MAP
# define VM_MEMORY_ROSETTA_INDIRECT_BRANCH_MAP          232
#endif
#ifndef VM_MEMORY_ROSETTA_RETURN_STACK
# define VM_MEMORY_ROSETTA_RETURN_STACK                 233
#endif
#ifndef VM_MEMORY_ROSETTA_EXECUTABLE_HEAP
# define VM_MEMORY_ROSETTA_EXECUTABLE_HEAP              234
#endif
#ifndef VM_MEMORY_ROSETTA_USER_LDT
# define VM_MEMORY_ROSETTA_USER_LDT                     235
#endif
#ifndef VM_MEMORY_ROSETTA_ARENA
# define VM_MEMORY_ROSETTA_ARENA                        236
#endif
#ifndef VM_MEMORY_ROSETTA_10
# define VM_MEMORY_ROSETTA_10                           239
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static struct sigaction g_SigActionSegv;  /**< The default action for SIGSEGV. */
static struct sigaction g_SigActionBus;   /**< The default action for SIGBUS. */
static struct sigaction g_SigActionAbort; /**< The default action for SIGABRT. */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Returns a description of the given VM user tag if known.
 */
static const char *rtR3DarwinVmUserTagStringify(unsigned int uTag, char *pszTmp, size_t cbTmp)
{
    switch (uTag)
    {
        case VM_MEMORY_MALLOC:                              return "MALLOC";
        case VM_MEMORY_MALLOC_SMALL:                        return "MALLOC_SMALL";
        case VM_MEMORY_MALLOC_LARGE:                        return "MALLOC_LARGE";
        case VM_MEMORY_MALLOC_HUGE:                         return "MALLOC_HUGE";
        case VM_MEMORY_SBRK:                                return "SBRK";
        case VM_MEMORY_REALLOC:                             return "REALLOC";
        case VM_MEMORY_MALLOC_TINY:                         return "MALLOC_TINY";
        case VM_MEMORY_MALLOC_LARGE_REUSABLE:               return "MALLOC_LARGE_REUSABLE";
        case VM_MEMORY_MALLOC_LARGE_REUSED:                 return "MALLOC_LARGE_REUSED";
        case VM_MEMORY_ANALYSIS_TOOL:                       return "ANALYSIS_TOOL";
        case VM_MEMORY_MALLOC_NANO:                         return "MALLOC_NANO";
        case VM_MEMORY_MALLOC_MEDIUM:                       return "MALLOC_MEDIUM";
        case VM_MEMORY_MALLOC_PROB_GUARD:                   return "MALLOC_PROB_GUARD";
        case VM_MEMORY_MACH_MSG:                            return "MACH_MSG";
        case VM_MEMORY_IOKIT:                               return "IOKIT";
        case VM_MEMORY_STACK:                               return "STACK";
        case VM_MEMORY_GUARD:                               return "GUARD";
        case VM_MEMORY_SHARED_PMAP:                         return "SHARED_PMAP";
        case VM_MEMORY_DYLIB:                               return "DYLIB";
        case VM_MEMORY_OBJC_DISPATCHERS:                    return "OBJC_DISPATCHERS";
        case VM_MEMORY_UNSHARED_PMAP:                       return "UNSHARED_PMAP";
        case VM_MEMORY_APPKIT:                              return "APPKIT";
        case VM_MEMORY_FOUNDATION:                          return "FOUNDATION";
        case VM_MEMORY_COREGRAPHICS:                        return "CoreGraphics";
        case VM_MEMORY_CORESERVICES:                        return "CoreServices";
        case VM_MEMORY_JAVA:                                return "Java";
        case VM_MEMORY_COREDATA:                            return "CoreData";
        case VM_MEMORY_COREDATA_OBJECTIDS:                  return "CoreData Object Ids";
        case VM_MEMORY_ATS:                                 return "ATS";
        case VM_MEMORY_LAYERKIT:                            return "LayerKit";
        case VM_MEMORY_CGIMAGE:                             return "CGImage";
        case VM_MEMORY_TCMALLOC:                            return "TCMalloc";
        case VM_MEMORY_COREGRAPHICS_DATA:                   return "CoreGaphics Data";
        case VM_MEMORY_COREGRAPHICS_SHARED:                 return "CoreGaphics Shared";
        case VM_MEMORY_COREGRAPHICS_FRAMEBUFFERS:           return "CoreGaphics Framebuffers";
        case VM_MEMORY_COREGRAPHICS_BACKINGSTORES:          return "CoreGaphics BackingStores";
        case VM_MEMORY_COREGRAPHICS_XALLOC:                 return "CoreGaphics XAlloc";
        case VM_MEMORY_DYLD:                                return "Dyld";
        case VM_MEMORY_DYLD_MALLOC:                         return "Dyld Malloc";
        case VM_MEMORY_SQLITE:                              return "SQlite";
        case VM_MEMORY_JAVASCRIPT_CORE:                     return "Javascript Core";
        case VM_MEMORY_JAVASCRIPT_JIT_EXECUTABLE_ALLOCATOR: return "Javascript JIT ExecAlloc";
        case VM_MEMORY_JAVASCRIPT_JIT_REGISTER_FILE:        return "Javascript JIT RegFile";
        case VM_MEMORY_GLSL:                                return "GLSL";
        case VM_MEMORY_OPENCL:                              return "OpenCL";
        case VM_MEMORY_COREIMAGE:                           return "CoreImage";
        case VM_MEMORY_WEBCORE_PURGEABLE_BUFFERS:           return "WebCore (Purgable Buffers)";
        case VM_MEMORY_IMAGEIO:                             return "ImageIO";
        case VM_MEMORY_COREPROFILE:                         return "CoreProfile";
        case VM_MEMORY_ASSETSD:                             return "AssetSD";
        case VM_MEMORY_OS_ALLOC_ONCE:                       return "OS Alloc Once";
        case VM_MEMORY_LIBDISPATCH:                         return "libdispatch";
        case VM_MEMORY_ACCELERATE:                          return "Accelerate";
        case VM_MEMORY_COREUI:                              return "CoreUI";
        case VM_MEMORY_COREUIFILE:                          return "CoreUIFile";
        case VM_MEMORY_GENEALOGY:                           return "Genealogy";
        case VM_MEMORY_RAWCAMERA:                           return "RawCamera";
        case VM_MEMORY_CORPSEINFO:                          return "CorpseInfo";
        case VM_MEMORY_ASL:                                 return "ASL";
        case VM_MEMORY_SWIFT_RUNTIME:                       return "Swift Runtime";
        case VM_MEMORY_SWIFT_METADATA:                      return "Swift Metadata";
        case VM_MEMORY_DHMM:                                return "DHMM";
        case VM_MEMORY_SCENEKIT:                            return "SceneKit";
        case VM_MEMORY_SKYWALK:                             return "SkyWalk";
        case VM_MEMORY_IOSURFACE:                           return "IOSurface";
        case VM_MEMORY_LIBNETWORK:                          return "libnetwork";
        case VM_MEMORY_AUDIO:                               return "Audio";
        case VM_MEMORY_VIDEOBITSTREAM:                      return "Video Bitstream";
        case VM_MEMORY_CM_XPC:                              return "CoreMedia XPC";
        case VM_MEMORY_CM_RPC:                              return "CoreMedia RPC";
        case VM_MEMORY_CM_MEMORYPOOL:                       return "CoreMedia memory pool";
        case VM_MEMORY_CM_READCACHE:                        return "CoreMedia read cache";
        case VM_MEMORY_CM_CRABS:                            return "CoreMedia crabs";
        case VM_MEMORY_QUICKLOOK_THUMBNAILS:                return "QuickLook thumbnails";
        case VM_MEMORY_ACCOUNTS:                            return "Accounts";
        case VM_MEMORY_SANITIZER:                           return "Sanitizer";
        case VM_MEMORY_IOACCELERATOR:                       return "IOAccelerator";
        case VM_MEMORY_CM_REGWARP:                          return "CoreMedia Regwarp";
        case VM_MEMORY_EAR_DECODER:                         return "EmbeddedAcousticRecognition decoder";
        case VM_MEMORY_COREUI_CACHED_IMAGE_DATA:            return "CoreUI cached image data";
        case VM_MEMORY_COLORSYNC:                           return "ColorSync";
        case VM_MEMORY_BTINFO:                              return "backtrace info";
        case VM_MEMORY_CM_HLS:                              return "CoreMedia HLS";
        case VM_MEMORY_ROSETTA:                             return "Rosetta";
        case VM_MEMORY_ROSETTA_THREAD_CONTEXT:              return "Rosetta thread context";
        case VM_MEMORY_ROSETTA_INDIRECT_BRANCH_MAP:         return "Rosetta indirect branch map";
        case VM_MEMORY_ROSETTA_RETURN_STACK:                return "Rosetta return stack";
        case VM_MEMORY_ROSETTA_EXECUTABLE_HEAP:             return "Rosetta exectuable heap";
        case VM_MEMORY_ROSETTA_USER_LDT:                    return "Rosetta user LDT";
        case VM_MEMORY_ROSETTA_ARENA:                       return "Rosetta arena";
        case VM_MEMORY_ROSETTA_10:                          return "ROSETTA_10";
        default: break;
    }

    RTStrPrintf(pszTmp, cbTmp,  "tag=%#x", uTag);
    return pszTmp;
}


/**
 * Signal handler callback.
 *
 * Will try log stuff.
 */
static void rtR3DarwinSigSegvBusHandler(int iSignum, siginfo_t *pSigInfo, void *pvContext)
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
        RTLogLoggerWeak(pLogger, NULL, "\n!!! rtR3DarwinSigSegvBusHandler caught an exception on thread %p in %u !!!\n",
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
        uintptr_t     uXcptAddr = (uintptr_t)pSigInfo->si_addr;
        uintptr_t     uXcptPC   = 0;
        uintptr_t     uXcptSP   = 0;
        mcontext_t    pXcptCtx = pCtx->uc_mcontext;
#ifdef RT_ARCH_AMD64
        RTLogLoggerWeak(pLogger, NULL, "\ncs:rip=%04x:%016RX64\n",
                        pXcptCtx->__ss.__cs, pXcptCtx->__ss.__rip);
        RTLogLoggerWeak(pLogger, NULL, "rsp=%016RX64 rbp=%016RX64\n",
                        pXcptCtx->__ss.__rsp, pXcptCtx->__ss.__rbp);
        RTLogLoggerWeak(pLogger, NULL, "rax=%016RX64 rcx=%016RX64 rdx=%016RX64 rbx=%016RX64\n",
                        pXcptCtx->__ss.__rax, pXcptCtx->__ss.__rcx, pXcptCtx->__ss.__rdx, pXcptCtx->__ss.__rbx);
        RTLogLoggerWeak(pLogger, NULL, "rsi=%016RX64 rdi=%016RX64 rsp=%016RX64 rbp=%016RX64\n",
                        pXcptCtx->__ss.__rsi, pXcptCtx->__ss.__rdi, pXcptCtx->__ss.__rsp, pXcptCtx->__ss.__rbp);
        RTLogLoggerWeak(pLogger, NULL, "r8 =%016RX64 r9 =%016RX64 r10=%016RX64 r11=%016RX64\n",
                        pXcptCtx->__ss.__r8,  pXcptCtx->__ss.__r9,  pXcptCtx->__ss.__r10, pXcptCtx->__ss.__r11);
        RTLogLoggerWeak(pLogger, NULL, "r12=%016RX64 r13=%016RX64 r14=%016RX64 r15=%016RX64\n",
                        pXcptCtx->__ss.__r12,  pXcptCtx->__ss.__r13,  pXcptCtx->__ss.__r14, pXcptCtx->__ss.__r15);
        RTLogLoggerWeak(pLogger, NULL, "fs=%04x gs=%04x eflags=%08x\n",
                        pXcptCtx->__ss.__fs, pXcptCtx->__ss.__gs,
                        pXcptCtx->__ss.__rflags);
        uXcptSP = pXcptCtx->__ss.__rsp;
        uXcptPC = pXcptCtx->__ss.__rip;

#elif defined(RT_ARCH_X86)
        /** @todo Only useful for the guest additions which aren't officially supported, so not worth the hassle right now. */
#elif defined(RT_ARCH_ARM64)
        uXcptSP = arm_thread_state64_get_sp(pXcptCtx->__ss);
        uXcptPC = arm_thread_state64_get_pc(pXcptCtx->__ss);

        RTLogLoggerWeak(pLogger, NULL, "\npc=%016RX64 pstate=%08RX32\n", uXcptPC, pXcptCtx->__ss.__cpsr);
        RTLogLoggerWeak(pLogger, NULL, "sp=%016RX64\n", uXcptSP);
        RTLogLoggerWeak(pLogger, NULL, "r0=%016RX64 r1=%016RX64 r2=%016RX64 r3=%016RX64\n",
                        pXcptCtx->__ss.__x[0], pXcptCtx->__ss.__x[1], pXcptCtx->__ss.__x[2], pXcptCtx->__ss.__x[3]);
        RTLogLoggerWeak(pLogger, NULL, "r4=%016RX64 r5=%016RX64 r6=%016RX64 r7=%016RX64\n",
                        pXcptCtx->__ss.__x[4], pXcptCtx->__ss.__x[5], pXcptCtx->__ss.__x[6], pXcptCtx->__ss.__x[7]);
        RTLogLoggerWeak(pLogger, NULL, "r8=%016RX64 r9=%016RX64 r10=%016RX64 r11=%016RX64\n",
                        pXcptCtx->__ss.__x[8], pXcptCtx->__ss.__x[9], pXcptCtx->__ss.__x[10], pXcptCtx->__ss.__x[11]);
        RTLogLoggerWeak(pLogger, NULL, "r12=%016RX64 r13=%016RX64 r14=%016RX64 r15=%016RX64\n",
                        pXcptCtx->__ss.__x[12], pXcptCtx->__ss.__x[13], pXcptCtx->__ss.__x[14], pXcptCtx->__ss.__x[15]);
        RTLogLoggerWeak(pLogger, NULL, "r16=%016RX64 r17=%016RX64 r18=%016RX64 r19=%016RX64\n",
                        pXcptCtx->__ss.__x[16], pXcptCtx->__ss.__x[17], pXcptCtx->__ss.__x[18], pXcptCtx->__ss.__x[19]);
        RTLogLoggerWeak(pLogger, NULL, "r20=%016RX64 r21=%016RX64 r22=%016RX64 r23=%016RX64\n",
                        pXcptCtx->__ss.__x[20], pXcptCtx->__ss.__x[21], pXcptCtx->__ss.__x[22], pXcptCtx->__ss.__x[23]);
        RTLogLoggerWeak(pLogger, NULL, "r24=%016RX64 r25=%016RX64 r26=%016RX64 r27=%016RX64\n",
                        pXcptCtx->__ss.__x[24], pXcptCtx->__ss.__x[25], pXcptCtx->__ss.__x[26], pXcptCtx->__ss.__x[27]);
        RTLogLoggerWeak(pLogger, NULL, "r28=%016RX64 r29=%016RX64 r30=%016RX64\n",
                        pXcptCtx->__ss.__x[28], arm_thread_state64_get_fp(pXcptCtx->__ss), arm_thread_state64_get_lr(pXcptCtx->__ss));
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

        /* Dump the address space of our process. */
        RTLogLoggerWeak(pLogger, NULL,
                        "\nAddress space:\n"
                        "%-*s[*] SHM             PROT  Type\n", sizeof(void *) * 4 + 2 - 1, "Address range"
                        );

        mach_port_t hTask = mach_task_self();
        kern_return_t krc = KERN_SUCCESS;
        vm_address_t VmAddrCur = 0;
        vm_size_t    cbCur = 0;
        natural_t    uDepth = 0;
        for (;;)
        {
            struct vm_region_submap_info_64 VmInfo;
            mach_msg_type_number_t uCnt = VM_REGION_SUBMAP_INFO_COUNT_64;

            krc = vm_region_recurse_64(hTask, &VmAddrCur, &cbCur, &uDepth, (vm_region_recurse_info_t)&VmInfo, &uCnt);
            if (krc == KERN_INVALID_ADDRESS)
                break;
            /** @todo r=bird: May use uninitialized VmInfo here if krc is a failure...   */

            const char *pszShareMode;
            switch (VmInfo.share_mode)
            {
                case SM_COW:             pszShareMode = "COW            "; break;
                case SM_PRIVATE:         pszShareMode = "PRIVATE        "; break;
                case SM_EMPTY:           pszShareMode = "EMPTY          "; break;
                case SM_SHARED:          pszShareMode = "SHARED         "; break;
                case SM_TRUESHARED:      pszShareMode = "TRUESHARED     "; break;
                case SM_PRIVATE_ALIASED: pszShareMode = "PRIVATE_ALIASED"; break;
                case SM_SHARED_ALIASED:  pszShareMode = "SHARED_ALIASED "; break;
#ifdef SM_LARGE_PAGE
                case SM_LARGE_PAGE:      pszShareMode = "LARGE_PAGE     "; break;
#endif
                default:                 pszShareMode = "<INVALID>      "; break;
            }

            char chXcpt = ' ';
            if (   uXcptAddr >= VmAddrCur
                && uXcptAddr < (VmAddrCur + cbCur))
                chXcpt = '*';

            char szProt[4] = "---";
            if (VmInfo.protection & VM_PROT_READ)
                szProt[0] = 'r';
            if (VmInfo.protection & VM_PROT_WRITE)
                szProt[1] = 'w';
            if (VmInfo.protection & VM_PROT_EXECUTE)
                szProt[2] = 'x';

            char szTmp[32];
            RTLogLoggerWeak(pLogger, NULL, "%*s %p..%p%c  %s [%s] %s %s\n",
                            uDepth * 4, " ", (uintptr_t)VmAddrCur, (uintptr_t)VmAddrCur + cbCur - 1, chXcpt,
                            pszShareMode, szProt, rtR3DarwinVmUserTagStringify(VmInfo.user_tag, szTmp, sizeof(szTmp)), "");

            if (VmInfo.is_submap)
                uDepth++;
            else
            {
                VmAddrCur += cbCur;
            }
        }

        /*
         * Try dump the load information.
         */
        RTLogLoggerWeak(pLogger, NULL,
                        "\nLoaded Modules:\n"
                        "%-*s[*] Path\n", sizeof(void *) * 4 + 2 - 1, "Address range"
                        );

#if 0
        struct task_dyld_info DyldInfo;
        mach_msg_type_number_t uCnt = TASK_DYLD_INFO_COUNT;
        krc = task_info(hTask, TASK_DYLD_INFO, (task_info_t)&DyldInfo, &uCnt);
        if (krc == KERN_SUCCESS)
        {
            struct dyld_all_image_infos *pImageInfos = (struct dyld_all_image_infos *)DyldInfo.all_image_info_addr;
            for (uint32_t i = 0; i < pImageInfos->infoArrayCount; i++)
            {
                const struct dyld_image_info *pImg = &pImageInfos->infoArray[i];
                RTLogLoggerWeak(pLogger, NULL, "%p..%p%c  %s\n",
                                pImg->imageLoadAddress, 0, ' ', pImg->imageFilePath);
            }
        }
        else
            RTLogLoggerWeak(pLogger, NULL, "Failed to obtain DYLD information with %#x\n", krc);
#else
        uint32_t const cImages = _dyld_image_count();
        for (uint32_t i = 0; i < cImages; i++)
        {
            char chInd = ' ';

            /*
             * Go through the load commands of the mach-o file and find the text segment to apply
             * the slide to in order to get at the final boundaries of the executable code of the shared
             * library.
             */
# if ARCH_BITS == 64
#  define MY_MACHO_HEADER          mach_header_64
#  define MY_MACHO_LC_SEGMENT      LC_SEGMENT_64
#  define MY_MACHO_SEGMENT_COMMAND segment_command_64
# elif ARCH_BITS == 32
#  ifdef LC_SEGMENT_32
#   define MY_MACHO_HEADER          mach_header_32
#   define MY_MACHO_LC_SEGMENT      LC_SEGMENT_32
#   define MY_MACHO_SEGMENT_COMMAND segment_command_32
#  else
#   define MY_MACHO_HEADER          mach_header
#   define MY_MACHO_LC_SEGMENT      LC_SEGMENT
#   define MY_MACHO_SEGMENT_COMMAND segment_command
#  endif
# else
#  error "Port me"
# endif
            intptr_t VmSlide = _dyld_get_image_vmaddr_slide(i);
            const struct MY_MACHO_HEADER *pHdr = (const struct MY_MACHO_HEADER *)_dyld_get_image_header(i);
            const struct load_command *pLoadCmd = (struct load_command *)(pHdr + 1);
            size_t cbText = 0;
            uintptr_t PtrText = 0;
            for (uint32_t iCmd = 0; iCmd < pHdr->ncmds; iCmd++)
            {
                if (pLoadCmd->cmd == MY_MACHO_LC_SEGMENT)
                {
                    const struct MY_MACHO_SEGMENT_COMMAND *pSeg = (const struct MY_MACHO_SEGMENT_COMMAND *)pLoadCmd;
                    if (!strcmp(pSeg->segname, "__TEXT"))
                    {
                        PtrText = pSeg->vmaddr + VmSlide;
                        cbText  = pSeg->vmsize;
                        break;
                    }
                }

                pLoadCmd = (struct load_command *)((uint8_t *)pLoadCmd + pLoadCmd->cmdsize);
            }

            if (uXcptPC >= PtrText && uXcptPC < PtrText + cbText)
                chInd = '*';

            RTLogLoggerWeak(pLogger, NULL, "%p..%p%c  %s\n",
                            PtrText, PtrText + cbText, chInd, _dyld_get_image_name(i));
        }
#endif

        /*
         * Dump the command line - avoiding dynamic memory allocations.
         */
        int aMib[3] = { 0 };
        size_t cb = 0;
        char abStr[_16K];
        aMib[0] = CTL_KERN;
        aMib[1] = KERN_PROCARGS2;
        aMib[2] = RTProcSelf();
        int rcDarwin = sysctl(&aMib[0], sizeof(aMib), NULL, &cb, NULL, 0);
        if (   !rcDarwin
            && cb < sizeof(abStr))
        {
            rcDarwin = sysctl(&aMib[0], sizeof(aMib), &abStr[0], &cb, NULL, 0);
            if (   !rcDarwin
                && cb >= sizeof(int))
            {
                RTLogLoggerWeak(pLogger, NULL, "\nCommandLine: ");

                /*
                 * First comes the argument count, then the executable path,
                 * then comes the raw string area.
                 */
                int cArgs = *(int *)abStr;

                cb -= sizeof(int);
                const char *pb = &abStr[sizeof(cArgs)];

                size_t cch = strlen(pb) + 1;
                pb += cch;
                cb -= cch;

                /* Find the begining of the string area. */
                while (cb && *pb == '\0')
                {
                    cb--;
                    pb++;
                }

                while (cArgs && cb)
                {
                    RTLogLoggerWeak(pLogger, NULL, " %s", pb);
                    cch = strlen(pb) + 1;
                    cArgs--;
                    cb -= cch;
                    pb += cch;
                }

                RTLogLoggerWeak(pLogger, NULL, "\n");

                /* Try dumping the raw string area which might contain interesting environment variables. */
                RTLogLoggerWeak(pLogger, NULL, "\nDumping string area\n", uStack, cbToDump, uTop);
                RTLogLoggerWeak(pLogger, NULL, "%.*RhxD\n", cb, abStr);
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
    Action.sa_sigaction = rtR3DarwinSigSegvBusHandler;
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

