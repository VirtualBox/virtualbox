/* $Id: the-haiku-kernel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Include all necessary headers for the Haiku kernel.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_r0drv_haiku_the_haiku_kernel_h
#define IPRT_INCLUDED_SRC_r0drv_haiku_the_haiku_kernel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <stdlib.h>

#include <OS.h>
#include <KernelExport.h>

#include <iprt/cdefs.h>
#include <iprt/err.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/* headers/private/kernel/smp.h */

extern int32 smp_get_num_cpus(void);
extern int32 smp_get_current_cpu(void);

/* headers/private/kernel/vm/vm.h */
extern status_t vm_unreserve_address_range(team_id team, void *address, addr_t size);
extern status_t vm_reserve_address_range(team_id team, void **_address, uint32 addressSpec, addr_t size, uint32 flags);
extern area_id vm_clone_area(team_id team, const char *name, void **address, uint32 addressSpec, uint32 protection,
                             uint32 mapping, area_id sourceArea, bool kernel);

/* headers/private/kernel/thread_type.h */

extern spinlock gThreadSpinlock;
#define GRAB_THREAD_LOCK()    acquire_spinlock(&gThreadSpinlock)
#define RELEASE_THREAD_LOCK() release_spinlock(&gThreadSpinlock)
typedef struct
{
    int32            flags;            /* summary of events relevant in interrupt handlers (signals pending, user debugging
                                          enabled, etc.) */
#if 0
    Thread            *all_next;
    Thread            *team_next;
    Thread            *queue_next;    /* i.e. run queue, release queue, etc. */
    timer            alarm;
    thread_id        id;
    char            name[B_OS_NAME_LENGTH];
    int32            priority;
    int32            next_priority;
    int32            io_priority;
    int32            state;
    int32            next_state;
#endif
    // and a lot more...
} Thread;

/* headers/private/kernel/thread.h */

extern Thread* thread_get_thread_struct(thread_id id);
extern Thread* thread_get_thread_struct_locked(thread_id id);

extern void thread_yield(bool force);

RT_C_DECLS_END

/**
 * Convert from Haiku kernel return code to IPRT status code.
 * @todo put this where it belongs! (i.e. in a separate file and prototype in iprt/err.h)
 * Or as generic call since it's not r0 specific.
 */
DECLINLINE(int) RTErrConvertFromHaikuKernReturn(status_t rc)
{
    switch (rc)
    {
        case B_OK:               return VINF_SUCCESS;
        case B_BAD_SEM_ID:       return VERR_SEM_ERROR;
        case B_NO_MORE_SEMS:     return VERR_TOO_MANY_SEMAPHORES;
        case B_BAD_THREAD_ID:    return VERR_INVALID_PARAMETER;
        case B_NO_MORE_THREADS:  return VERR_MAX_THRDS_REACHED;
        case B_BAD_TEAM_ID:      return VERR_INVALID_PARAMETER;
        case B_NO_MORE_TEAMS:    return VERR_MAX_PROCS_REACHED;
            //default:               return VERR_GENERAL_FAILURE;
            /** POSIX Errors are defined as a subset of system errors. */
        default:                 return RTErrConvertFromErrno(rc);
    }
}

#endif /* !IPRT_INCLUDED_SRC_r0drv_haiku_the_haiku_kernel_h */

