/* $Id: tstDarwinSched.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT testcase - darwin scheduling.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <mach/thread_info.h>
#include <mach/host_info.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void thread_print_policies(int fDefault)
{
    thread_extended_policy_data_t           Extended = { 0 };
    thread_time_constraint_policy_data_t    TimeConstraint = { 0, 0, 0, 1 };
    thread_precedence_policy_data_t         Precedence = { 0 };
#ifdef THREAD_AFFINITY_POLICY /* 10.5 */
    thread_affinity_policy_data_t           Affinity = { 0 };
#endif
    boolean_t                               GetDefault;
    mach_msg_type_number_t                  Count;
    kern_return_t                           krc;

    GetDefault = fDefault;
    Count = THREAD_EXTENDED_POLICY_COUNT;
    krc = thread_policy_get(mach_thread_self(), THREAD_EXTENDED_POLICY, (thread_policy_t)&Extended, &Count, &GetDefault);
    printf("THREAD_EXTENDED_POLICY: krc=%#x default=%d timeshare=%d (%#x)\n",
           krc, GetDefault, Extended.timeshare, Extended.timeshare);

    GetDefault = fDefault;
    Count = THREAD_PRECEDENCE_POLICY_COUNT;
    krc = thread_policy_get(mach_thread_self(), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&Precedence, &Count, &GetDefault);
    printf("THREAD_PRECEDENCE_POLICY: krc=%#x default=%d importance=%d (%#x)\n",
           krc, GetDefault, Precedence.importance, Precedence.importance);

    GetDefault = fDefault;
    Count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    krc = thread_policy_get(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&TimeConstraint, &Count, &GetDefault);
    printf("THREAD_TIME_CONSTRAINT_POLICY: krc=%#x default=%d period=%u (%#x) computation=%u (%#x) constraint=%u (%#x) preemptible=%d\n",
           krc, GetDefault, TimeConstraint.period, TimeConstraint.period,
           TimeConstraint.computation, TimeConstraint.computation,
           TimeConstraint.constraint,  TimeConstraint.constraint,
           TimeConstraint.preemptible);

#ifdef THREAD_AFFINITY_POLICY /* 10.5 */
    GetDefault = fDefault;
    Count = THREAD_AFFINITY_POLICY_COUNT;
    krc = thread_policy_get(mach_thread_self(), THREAD_AFFINITY_POLICY, (thread_policy_t)&Affinity, &Count, &GetDefault);
    printf("THREAD_AFFINITY_POLICY: krc=%#x default=%d affinity_tag=%d (%#x)\n",
           krc, GetDefault, Affinity.affinity_tag, Affinity.affinity_tag);
#endif

    if (!fDefault)
    {
        struct sched_param              Param;
        int                             iPolicy = 0;
        struct thread_basic_info        BasicInfo = {{0,0},{0,0},0,0,0,0,0,0};
        struct policy_timeshare_info    TSInfo = {0,0,0,0,0};
        int                             rc;

        memset(&Param, 0, sizeof(Param));
        rc = pthread_getschedparam(pthread_self(), &iPolicy, &Param);
        printf("pthread_getschedparam: rc=%d iPolicy=%d (%#x) sched_priority=%d (%#x) opaque=%d (%#x)\n",
               rc, iPolicy, iPolicy, Param.sched_priority, Param.sched_priority,
#ifdef THREAD_AFFINITY_POLICY /* 10.5 */
               *(int *)&Param.__opaque, *(int *)&Param.__opaque);
#else
               *(int *)&Param.opaque, *(int *)&Param.opaque);
#endif

        Count = THREAD_BASIC_INFO_COUNT;
        krc = thread_info(mach_thread_self(), THREAD_BASIC_INFO, (thread_info_t)&BasicInfo, &Count);
        printf("THREAD_BASIC_INFO: krc=%#x user_time=%d.%06d system_time=%d.%06d cpu_usage=%d policy=%d\n"
               "    run_state=%d flags=%#x suspend_count=%d sleep_time=%d\n",
               krc,
               BasicInfo.user_time.seconds,   BasicInfo.user_time.microseconds,
               BasicInfo.system_time.seconds, BasicInfo.system_time.microseconds,
               BasicInfo.cpu_usage,
               BasicInfo.policy,
               BasicInfo.run_state,
               BasicInfo.flags,
               BasicInfo.suspend_count,
               BasicInfo.sleep_time);

        Count = POLICY_TIMESHARE_INFO_COUNT;
        krc = thread_info(mach_thread_self(), THREAD_SCHED_TIMESHARE_INFO, (thread_info_t)&TSInfo, &Count);
        printf("THREAD_SCHED_TIMESHARE_INFO: krc=%#x max_priority=%d (%#x) base_priority=%d (%#x) cur_priority=%d (%#x)\n"
               "    depressed=%d depress_priority=%d (%#x)\n",
               krc,
               TSInfo.max_priority, TSInfo.max_priority,
               TSInfo.base_priority, TSInfo.base_priority,
               TSInfo.cur_priority, TSInfo.cur_priority,
               TSInfo.depressed,
               TSInfo.depress_priority, TSInfo.depress_priority);
    }
    else
    {
        host_priority_info_data_t   PriorityInfo = {0,0,0,0,0,0,0,0};

        Count = HOST_PRIORITY_INFO_COUNT;
        krc = host_info(mach_host_self(), HOST_PRIORITY_INFO, (host_info_t)&PriorityInfo, &Count);
        printf("HOST_PRIORITY_INFO: krc=%#x \n"
               "        kernel_priority=%2d (%#x)\n"
               "        system_priority=%2d (%#x)\n"
               "        server_priority=%2d (%#x)\n"
               "          user_priority=%2d (%#x)\n"
               "       depress_priority=%2d (%#x)\n"
               "          idle_priority=%2d (%#x)\n"
               "       minimum_priority=%2d (%#x)\n"
               "       maximum_priority=%2d (%#x)\n",
               krc,
               PriorityInfo.kernel_priority,  PriorityInfo.kernel_priority,
               PriorityInfo.system_priority,  PriorityInfo.system_priority,
               PriorityInfo.server_priority,  PriorityInfo.server_priority,
               PriorityInfo.user_priority,    PriorityInfo.user_priority,
               PriorityInfo.depress_priority, PriorityInfo.depress_priority,
               PriorityInfo.idle_priority,    PriorityInfo.idle_priority,
               PriorityInfo.minimum_priority, PriorityInfo.minimum_priority,
               PriorityInfo.maximum_priority, PriorityInfo.maximum_priority);
    }
}

int main()
{
    struct sched_param  Param;
    int                 iPolicy;
    int                 iPriority;
    int                 rc;

    printf("tstDarwinSched: Default policies:\n");
    thread_print_policies(1);

    printf("tstDarwinSched: Current policies:\n");
    thread_print_policies(0);


    printf("tstDarwinSched:\n");
    printf("tstDarwinSched: Trying max priority using pthread API\n");
    iPolicy = SCHED_OTHER;
    memset(&Param, 0, sizeof(Param));
    pthread_getschedparam(pthread_self(), &iPolicy, &Param);
    Param.sched_priority = iPriority = sched_get_priority_max(iPolicy);
    rc = pthread_setschedparam(pthread_self(), iPolicy, &Param);
    if (!rc)
    {
        do
        {
            Param.sched_priority = ++iPriority;
            rc = pthread_setschedparam(pthread_self(), iPolicy, &Param);
        } while (!rc);
        iPriority--;
        rc = 0;
    }
    printf("tstDarwinSched: pthread_setschedparam(iPriority=%d [max=%d]) -> %d\n",
           iPriority, sched_get_priority_max(iPolicy), rc);
    thread_print_policies(0);


    printf("tstDarwinSched:\n");
    printf("tstDarwinSched: Trying min priority using pthread API\n");
    iPolicy = SCHED_OTHER;
    memset(&Param, 0, sizeof(Param));
    pthread_getschedparam(pthread_self(), &iPolicy, &Param);
    Param.sched_priority = iPriority = sched_get_priority_min(iPolicy);
    rc = pthread_setschedparam(pthread_self(), iPolicy, &Param);
    if (!rc)
    {
        do
        {
            Param.sched_priority = --iPriority;
            rc = pthread_setschedparam(pthread_self(), iPolicy, &Param);
        } while (!rc);
        iPriority++;
        rc = 0;
    }
    printf("tstDarwinSched: pthread_setschedparam(iPriority=%d [min=%d]) -> %d\n",
           iPriority, sched_get_priority_min(iPolicy), rc);
    thread_print_policies(0);


    return 0;
}

