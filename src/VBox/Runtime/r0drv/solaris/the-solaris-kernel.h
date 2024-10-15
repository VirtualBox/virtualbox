/* $Id: the-solaris-kernel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Include all necessary headers for the Solaris kernel.
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

#ifndef IPRT_INCLUDED_SRC_r0drv_solaris_the_solaris_kernel_h
#define IPRT_INCLUDED_SRC_r0drv_solaris_the_solaris_kernel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <sys/kmem.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sdt.h>
#include <sys/schedctl.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/vmsystm.h>
#include <sys/cyclic.h>
#include <sys/class.h>
#include <sys/cpuvar.h>
#include <sys/archsystm.h>
#include <sys/x_call.h> /* in platform dir */
#include <sys/x86_archext.h>
#include <vm/hat.h>
#include <vm/seg_vn.h>
#include <vm/seg_kmem.h>
#include <vm/page.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/spl.h>
#include <sys/archsystm.h>
#include <sys/callo.h>
#include <sys/kobj.h>
#include <sys/ctf_api.h>
#include <sys/modctl.h>
#include <sys/proc.h>
#include <sys/t_lock.h>

#undef u /* /usr/include/sys/user.h:249:1 is where this is defined to (curproc->p_user). very cool. */

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/dbg.h>

RT_C_DECLS_BEGIN

/* IPRT functions. */
DECLHIDDEN(void *)   rtR0SolMemAlloc(uint64_t cbPhysHi, uint64_t *puPhys, size_t cb, uint64_t cbAlign, bool fContig);
DECLHIDDEN(void)     rtR0SolMemFree(void *pv, size_t cb);


/* Solaris functions. */
typedef callout_id_t (*PFNSOL_timeout_generic)(int type, void (*func)(void *),
                                               void *arg, hrtime_t expiration,
                                               hrtime_t resultion, int flags);
typedef hrtime_t     (*PFNSOL_untimeout_generic)(callout_id_t id, int nowait);
typedef int          (*PFNSOL_cyclic_reprogram)(cyclic_id_t id, hrtime_t expiration);
typedef void         (*PFNSOL_contig_free)(void *addr, size_t size);
typedef int          (*PFNSOL_page_noreloc_supported)(size_t cbPageSize);

/* IPRT globals. */
extern bool                            g_frtSolSplSetsEIF;
extern RTCPUSET                        g_rtMpSolCpuSet;
extern PFNSOL_timeout_generic          g_pfnrtR0Sol_timeout_generic;
extern PFNSOL_untimeout_generic        g_pfnrtR0Sol_untimeout_generic;
extern PFNSOL_cyclic_reprogram         g_pfnrtR0Sol_cyclic_reprogram;
extern PFNSOL_contig_free              g_pfnrtR0Sol_contig_free;
extern PFNSOL_page_noreloc_supported   g_pfnrtR0Sol_page_noreloc_supported;
extern size_t                          g_offrtSolThreadPreempt;
extern size_t                          g_offrtSolThreadIntrThread;
extern size_t                          g_offrtSolThreadLock;
extern size_t                          g_offrtSolThreadProc;
extern size_t                          g_offrtSolThreadId;
extern size_t                          g_offrtSolCpuPreempt;
extern size_t                          g_offrtSolCpuForceKernelPreempt;
extern bool                            g_frtSolInitDone;
extern RTDBGKRNLINFO                   g_hKrnlDbgInfo;

/*
 * Workarounds for running on old versions of solaris with different cross call
 * interfaces. If we find xc_init_cpu() in the kernel, then just use the
 * defined interfaces for xc_call() from the include file where the xc_call()
 * interfaces just takes a pointer to a ulong_t array. The array must be long
 * enough to hold "ncpus" bits at runtime.

 * The reason for the hacks is that using the type "cpuset_t" is pretty much
 * impossible from code built outside the Solaris source repository that wants
 * to run on multiple releases of Solaris.
 *
 * For old style xc_call()s, 32 bit solaris and older 64 bit versions use
 * "ulong_t" as cpuset_t.
 *
 * Later versions of 64 bit Solaris used: struct {ulong_t words[x];}
 * where "x" depends on NCPU.
 *
 * We detect the difference in 64 bit support by checking the kernel value of
 * max_cpuid, which always holds the compiled value of NCPU - 1.
 *
 * If Solaris increases NCPU to more than 256, VBox will continue to work on
 * all versions of Solaris as long as the number of installed CPUs in the
 * machine is <= IPRT_SOLARIS_NCPUS. If IPRT_SOLARIS_NCPUS is increased, this
 * code has to be re-written some to provide compatibility with older Solaris
 * which expects cpuset_t to be based on NCPU==256 -- or we discontinue
 * support of old Nevada/S10.
 */
#define IPRT_SOL_NCPUS          256
#define IPRT_SOL_SET_WORDS      (IPRT_SOL_NCPUS / (sizeof(ulong_t) * 8))
#define IPRT_SOL_X_CALL_HIPRI   (2) /* for Old Solaris interface */
typedef struct RTSOLCPUSET
{
    ulong_t                     auCpus[IPRT_SOL_SET_WORDS];
} RTSOLCPUSET;
typedef RTSOLCPUSET *PRTSOLCPUSET;

/* Avoid warnings even if it means more typing... */
typedef struct RTR0FNSOLXCCALL
{
    union
    {
        void *(*pfnSol_xc_call)          (xc_arg_t, xc_arg_t, xc_arg_t, ulong_t *, xc_func_t);
        void *(*pfnSol_xc_call_old)      (xc_arg_t, xc_arg_t, xc_arg_t, int, RTSOLCPUSET, xc_func_t);
        void *(*pfnSol_xc_call_old_ulong)(xc_arg_t, xc_arg_t, xc_arg_t, int, ulong_t, xc_func_t);
    } u;
} RTR0FNSOLXCCALL;
typedef RTR0FNSOLXCCALL *PRTR0FNSOLXCCALL;

extern RTR0FNSOLXCCALL          g_rtSolXcCall;
extern bool                     g_frtSolOldIPI;
extern bool                     g_frtSolOldIPIUlong;

/*
 * Thread-context hooks.
 * Workarounds for older Solaris versions that did not have the exitctx() callback.
 */
typedef struct RTR0FNSOLTHREADCTX
{
    union
    {
        void *(*pfnSol_installctx)        (kthread_t *pThread, void *pvArg,
                                           void (*pfnSave)(void *pvArg),
                                           void (*pfnRestore)(void *pvArg),
                                           void (*pfnFork)(void *pvThread, void *pvThreadFork),
                                           void (*pfnLwpCreate)(void *pvThread, void *pvThreadCreate),
                                           void (*pfnExit)(void *pvThread),
                                           void (*pfnFree)(void *pvArg, int fIsExec));

        void *(*pfnSol_installctx_old)    (kthread_t *pThread, void *pvArg,
                                           void (*pfnSave)(void *pvArg),
                                           void (*pfnRestore)(void *pvArg),
                                           void (*pfnFork)(void *pvThread, void *pvThreadFork),
                                           void (*pfnLwpCreate)(void *pvThread, void *pvThreadCreate),
                                           void (*pfnFree)(void *pvArg, int fIsExec));
    } Install;

    union
    {
        int (*pfnSol_removectx)           (kthread_t *pThread, void *pvArg,
                                           void (*pfnSave)(void *pvArg),
                                           void (*pfnRestore)(void *pvArg),
                                           void (*pfnFork)(void *pvThread, void *pvThreadFork),
                                           void (*pfnLwpCreate)(void *pvThread, void *pvThreadCreate),
                                           void (*pfnExit)(void *pvThread),
                                           void (*pfnFree)(void *pvArg, int fIsExec));

        int (*pfnSol_removectx_old)       (kthread_t *pThread, void *pvArg,
                                           void (*pfnSave)(void *pvArg),
                                           void (*pfnRestore)(void *pvArg),
                                           void (*pfnFork)(void *pvThread, void *pvThreadFork),
                                           void (*pfnLwpCreate)(void *pvThread, void *pvThreadCreate),
                                           void (*pfnFree)(void *pvArg, int fIsExec));
    } Remove;
} RTR0FNSOLTHREADCTX;
typedef RTR0FNSOLTHREADCTX *PRTR0FNSOLTHREADCTX;

extern RTR0FNSOLTHREADCTX       g_rtSolThreadCtx;
extern bool                     g_frtSolOldThreadCtx;

/*
 * Workaround for older Solaris versions which called map_addr()/choose_addr()/
 * map_addr_proc() with an 'alignment' argument that was removed in Solaris
 * 11.4.
 */
typedef struct RTR0FNSOLMAPADDR
{
    union
    {
        void *(*pfnSol_map_addr)          (caddr_t *, size_t, offset_t, uint_t);
        void *(*pfnSol_map_addr_old)      (caddr_t *, size_t, offset_t, int, uint_t);
    } u;
} RTR0FNSOLMAPADDR;
typedef RTR0FNSOLMAPADDR *PRTR0FNSOLMAPADDR;

extern RTR0FNSOLMAPADDR         g_rtSolMapAddr;
extern bool                     g_frtSolOldMapAddr;

/* Solaris globals. */
extern uintptr_t                kernelbase;

/* Misc stuff from newer kernels. */
#ifndef CALLOUT_FLAG_ABSOLUTE
# define CALLOUT_FLAG_ABSOLUTE 2
#endif

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_SRC_r0drv_solaris_the_solaris_kernel_h */

