/* $Id: the-darwin-kernel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Include all necessary headers for the Darwing kernel.
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

#ifndef IPRT_INCLUDED_SRC_r0drv_darwin_the_darwin_kernel_h
#define IPRT_INCLUDED_SRC_r0drv_darwin_the_darwin_kernel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Problematic header(s) containing conflicts with IPRT first. (FreeBSD has fixed these ages ago.) */
#define __STDC_CONSTANT_MACROS
#define __STDC_LIMIT_MACROS
#include <sys/param.h>
#include <mach/vm_param.h>
#undef ALIGN
#undef MIN
#undef MAX
#undef PAGE_SIZE
#undef PAGE_SHIFT
#undef PVM


/* Include the IPRT definitions of the conflicting #defines & typedefs. */
#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/param.h>


/* After including cdefs, we can check that this really is Darwin. */
#ifndef RT_OS_DARWIN
# error "RT_OS_DARWIN must be defined!"
#endif

#if defined(__clang__) || RT_GNUC_PREREQ(4, 4)
# pragma GCC diagnostic push
#endif
#if defined(__clang__) || RT_GNUC_PREREQ(4, 2)
# pragma GCC diagnostic ignored "-Wc++11-extensions"
# pragma GCC diagnostic ignored "-Wc99-extensions"
# pragma GCC diagnostic ignored "-Wextra-semi"
# pragma GCC diagnostic ignored "-Wzero-length-array"
# pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

/* now we're ready for including the rest of the Darwin headers. */
#include <kern/thread.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <kern/locks.h>
#if defined(RT_ARCH_X86) && MAC_OS_X_VERSION_MIN_REQUIRED < 1060
# include <i386/mp_events.h>
#endif
#include <libkern/libkern.h>
#include <libkern/sysctl.h>
#include <libkern/version.h>
#include <mach/thread_act.h>
#include <mach/vm_map.h>
#include <mach/vm_region.h>
#include <pexpert/pexpert.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <IOKit/IOTypes.h>
#include <IOKit/IOLib.h> /* Note! Has Assert down as a function. */
#define _OS_OSUNSERIALIZE_H /* HACK ALERT! Block importing OSUnserialized.h as it causes compilation trouble with
                               newer clang versions and the 10.15 SDK, and we really don't need it. Sample error:
                               libkern/c++/OSUnserialize.h:72:2: error: use of OSPtr outside of a return type [-Werror,-Wossharedptr-misuse] */
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMapper.h>

#if defined(__clang__) || RT_GNUC_PREREQ(4, 4)
# pragma GCC diagnostic pop
#endif


/* See osfmk/kern/ast.h. */
#ifndef AST_PREEMPT
# define AST_PREEMPT    UINT32_C(1)
# define AST_QUANTUM    UINT32_C(2)
# define AST_URGENT     UINT32_C(4)
#endif

/* This flag was added in 10.6, it seems.  Should be harmless in earlier
   releases... */
#if __MAC_OS_X_VERSION_MAX_ALLOWED < 1060
# define kIOMemoryMapperNone UINT32_C(0x800)
#endif

/* This flag was added in 10.8.2, it seems. */
#if __MAC_OS_X_VERSION_MAX_ALLOWED < 1082
# define kIOMemoryHostPhysicallyContiguous UINT32_C(0x00000080)
#endif

/** @name Macros for preserving EFLAGS.AC (despair / paranoid)
 * @remarks Unlike linux, we have to restore it unconditionally on darwin.
 * @{ */
#include <iprt/asm-amd64-x86.h>
#include <iprt/x86.h>
#define IPRT_DARWIN_SAVE_EFL_AC()                       RTCCUINTREG const fSavedEfl = ASMGetFlags();
#define IPRT_DARWIN_RESTORE_EFL_AC()                    ASMSetFlags(fSavedEfl)
#define IPRT_DARWIN_RESTORE_EFL_ONLY_AC()               ASMChangeFlags(~X86_EFL_AC, fSavedEfl & X86_EFL_AC)
#define IPRT_DARWIN_RESTORE_EFL_ONLY_AC_EX(a_fSavedEfl) ASMChangeFlags(~X86_EFL_AC, (a_fSavedEfl) & X86_EFL_AC)
/** @} */


RT_C_DECLS_BEGIN

/* mach/vm_types.h */
typedef struct pmap *pmap_t;

/* vm/vm_kern.h */
extern vm_map_t kernel_map;

/* vm/pmap.h */
extern pmap_t kernel_pmap;

/* kern/task.h */
extern vm_map_t get_task_map(task_t);

/* osfmk/i386/pmap.h */
extern ppnum_t pmap_find_phys(pmap_t, addr64_t);

/* vm/vm_map.h */
extern kern_return_t vm_map_wire(vm_map_t, vm_map_offset_t, vm_map_offset_t, vm_prot_t, boolean_t);
extern kern_return_t vm_map_unwire(vm_map_t, vm_map_offset_t, vm_map_offset_t, boolean_t);

/* mach/i386/thread_act.h */
extern kern_return_t thread_terminate(thread_t);

/* osfmk/i386/mp.h */
extern void mp_rendezvous(void (*)(void *), void (*)(void *), void (*)(void *), void *);
extern void mp_rendezvous_no_intrs(void (*)(void *), void *);

/* osfmk/i386/cpu_data.h */
struct my_cpu_data_x86
{
    struct my_cpu_data_x86 *cpu_this;
    thread_t                cpu_active_thread;
    void                   *cpu_int_state;
    vm_offset_t             cpu_active_stack;
    vm_offset_t             cpu_kernel_stack;
    vm_offset_t             cpu_int_stack_top;
    int                     cpu_preemption_level;
    int                     cpu_simple_lock_count;
    int                     cpu_interrupt_level;
    int                     cpu_number;
    int                     cpu_phys_number;
    cpu_id_t                cpu_id;
    int                     cpu_signals;
    int                     cpu_mcount_off;
    /*ast_t*/uint32_t       cpu_pending_ast;
    int                     cpu_type;
    int                     cpu_subtype;
    int                     cpu_threadtype;
    int                     cpu_running;
};

/* osfmk/i386/cpu_number.h */
extern int cpu_number(void);

/* osfmk/vm/vm_user.c */
extern kern_return_t vm_protect(vm_map_t, vm_offset_t, vm_size_t, boolean_t, vm_prot_t);
/*extern kern_return_t vm_region(vm_map_t, vm_address_t *, vm_size_t *, vm_region_flavor_t, vm_region_info_t,
                               mach_msg_type_number_t *, mach_port_t *);*/

/* i386/machine_routines.h */
extern int ml_get_max_cpus(void);

RT_C_DECLS_END


/*
 * Internals of the Darwin Ring-0 IPRT.
 */
RT_C_DECLS_BEGIN

/* initterm-r0drv-darwin.cpp. */
typedef uint32_t *  (*PFNR0DARWINASTPENDING)(void);
typedef void        (*PFNR0DARWINCPUINTERRUPT)(int);
extern DECL_HIDDEN_DATA(lck_grp_t *)                 g_pDarwinLockGroup;
extern DECL_HIDDEN_DATA(PFNR0DARWINASTPENDING)       g_pfnR0DarwinAstPending;
extern DECL_HIDDEN_DATA(PFNR0DARWINCPUINTERRUPT)     g_pfnR0DarwinCpuInterrupt;
#ifdef DEBUG /* Used once for debugging memory issues (see #9466). */
typedef kern_return_t (*PFNR0DARWINVMFAULTEXTERNAL)(vm_map_t, vm_map_offset_t, vm_prot_t, boolean_t, int, pmap_t, vm_map_offset_t);
extern DECL_HIDDEN_DATA(PFNR0DARWINVMFAULTEXTERNAL) g_pfnR0DarwinVmFaultExternal;
#endif

/* threadpreempt-r0drv-darwin.cpp */
int  rtThreadPreemptDarwinInit(void);
void rtThreadPreemptDarwinTerm(void);

RT_C_DECLS_END


/**
 * Converts from nanoseconds to Darwin absolute time units.
 * @returns Darwin absolute time.
 * @param   u64Nano     Time interval in nanoseconds
 */
DECLINLINE(uint64_t) rtDarwinAbsTimeFromNano(const uint64_t u64Nano)
{
    uint64_t u64AbsTime;
    nanoseconds_to_absolutetime(u64Nano, &u64AbsTime);
    return u64AbsTime;
}


#include <iprt/err.h>

/**
 * Convert from mach kernel return code to IPRT status code.
 * @todo put this where it belongs! (i.e. in a separate file and prototype in iprt/err.h)
 */
DECLINLINE(int) RTErrConvertFromMachKernReturn(kern_return_t rc)
{
    switch (rc)
    {
        case KERN_SUCCESS:      return VINF_SUCCESS;
        default:                return VERR_GENERAL_FAILURE;
    }
}

#endif /* !IPRT_INCLUDED_SRC_r0drv_darwin_the_darwin_kernel_h */

