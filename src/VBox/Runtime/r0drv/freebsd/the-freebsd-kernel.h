/* $Id: the-freebsd-kernel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Ring-0 Driver, The FreeBSD Kernel Headers.
 */

/*
 * Contributed by knut st. osmundsen.
 *
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef IPRT_INCLUDED_SRC_r0drv_freebsd_the_freebsd_kernel_h
#define IPRT_INCLUDED_SRC_r0drv_freebsd_the_freebsd_kernel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

/* Deal with conflicts first. */
#include <sys/param.h>
#undef PVM
#include <sys/bus.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/libkern.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/limits.h>
#include <sys/unistd.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#if __FreeBSD_version >= 1000030
#include <sys/rwlock.h>
#endif
#include <sys/mutex.h>
#include <sys/sched.h>
#include <sys/callout.h>
#include <sys/cpu.h>
#include <sys/smp.h>
#include <sys/sleepqueue.h>
#include <sys/sx.h>
#include <vm/vm.h>
#include <vm/pmap.h>            /* for vtophys */
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>        /* KERN_SUCCESS ++ */
#include <vm/vm_page.h>
#include <vm/vm_phys.h>         /* vm_phys_alloc_* */
#include <vm/vm_extern.h>       /* kmem_alloc_attr */
#include <vm/vm_pageout.h>      /* vm_contig_grow_cache */
#include <sys/vmmeter.h>        /* cnt */
#include <sys/resourcevar.h>
#include <machine/cpu.h>

/**
 * Wrappers around the sleepq_ KPI.
 */
#if __FreeBSD_version >= 800026
# define SLEEPQ_TIMEDWAIT(EventInt) sleepq_timedwait(EventInt, 0)
# define SLEEPQ_TIMEDWAIT_SIG(EventInt) sleepq_timedwait_sig(EventInt, 0)
# define SLEEPQ_WAIT(EventInt) sleepq_wait(EventInt, 0)
# define SLEEPQ_WAIT_SIG(EventInt) sleepq_wait_sig(EventInt, 0)
#else
# define SLEEPQ_TIMEDWAIT(EventInt) sleepq_timedwait(EventInt)
# define SLEEPQ_TIMEDWAIT_SIG(EventInt) sleepq_timedwait_sig(EventInt)
# define SLEEPQ_WAIT(EventInt) sleepq_wait(EventInt)
# define SLEEPQ_WAIT_SIG(EventInt) sleepq_wait_sig(EventInt)
#endif

/**
 * Our pmap_enter version
 */
#if __FreeBSD_version >= 701105
# define MY_PMAP_ENTER(pPhysMap, AddrR3, pPage, fProt, fWired) \
    pmap_enter(pPhysMap, AddrR3, VM_PROT_NONE, pPage, fProt, fWired)
#else
# define MY_PMAP_ENTER(pPhysMap, AddrR3, pPage, fProt, fWired) \
    pmap_enter(pPhysMap, AddrR3, pPage, fProt, fWired)
#endif

/**
 * The VM object lock/unlock wrappers for older kernels.
 */
#if __FreeBSD_version < 1000030
# define VM_OBJECT_WLOCK(a_pObject) VM_OBJECT_LOCK((a_pObject))
# define VM_OBJECT_WUNLOCK(a_pObject) VM_OBJECT_UNLOCK((a_pObject))
#endif

#if __FreeBSD_version >= 1100077
# define MY_LIM_MAX_PROC(a_pProc, a_Limit) lim_max_proc((a_pProc), (a_Limit))
#else
# define MY_LIM_MAX_PROC(a_pProc, a_Limit) lim_max((a_pProc), (a_Limit))
#endif

/**
 * Check whether we can use kmem_alloc_attr for low allocs.
 */
#if    (__FreeBSD_version >= 900011) \
    || (__FreeBSD_version < 900000 && __FreeBSD_version >= 800505) \
    || (__FreeBSD_version < 800000 && __FreeBSD_version >= 703101)
# define USE_KMEM_ALLOC_ATTR
#endif

/**
 * Check whether we can use kmem_alloc_prot.
 */
#if 0 /** @todo Not available yet. */
# define USE_KMEM_ALLOC_PROT
#endif

#endif /* !IPRT_INCLUDED_SRC_r0drv_freebsd_the_freebsd_kernel_h */
