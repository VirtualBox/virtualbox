/* $Id: GIMHvInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - Hyper-V, Internal header file.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VMM_INCLUDED_SRC_include_GIMHvInternal_h
#define VMM_INCLUDED_SRC_include_GIMHvInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/gim.h>
#include <VBox/vmm/cpum.h>

#include <iprt/net.h>

/** @name Hyper-V base feature identification.
 * Features based on current partition privileges (per-VM).
 * @{
 */
/** Virtual processor runtime MSR available. */
#define GIM_HV_BASE_FEAT_VP_RUNTIME_MSR                     RT_BIT(0)
/** Partition reference counter MSR available. */
#define GIM_HV_BASE_FEAT_PART_TIME_REF_COUNT_MSR            RT_BIT(1)
/** Basic Synthetic Interrupt Controller MSRs available. */
#define GIM_HV_BASE_FEAT_BASIC_SYNIC_MSRS                   RT_BIT(2)
/** Synthetic Timer MSRs available. */
#define GIM_HV_BASE_FEAT_STIMER_MSRS                        RT_BIT(3)
/** APIC access MSRs (EOI, ICR, TPR) available. */
#define GIM_HV_BASE_FEAT_APIC_ACCESS_MSRS                   RT_BIT(4)
/** Hypercall MSRs available. */
#define GIM_HV_BASE_FEAT_HYPERCALL_MSRS                     RT_BIT(5)
/** Access to VCPU index MSR available. */
#define GIM_HV_BASE_FEAT_VP_ID_MSR                          RT_BIT(6)
/** Virtual system reset MSR available. */
#define GIM_HV_BASE_FEAT_VIRT_SYS_RESET_MSR                 RT_BIT(7)
/** Statistic pages MSRs available. */
#define GIM_HV_BASE_FEAT_STAT_PAGES_MSR                     RT_BIT(8)
/** Paritition reference TSC MSR available. */
#define GIM_HV_BASE_FEAT_PART_REF_TSC_MSR                   RT_BIT(9)
/** Virtual guest idle state MSR available. */
#define GIM_HV_BASE_FEAT_GUEST_IDLE_STATE_MSR               RT_BIT(10)
/** Timer frequency MSRs (TSC and APIC) available. */
#define GIM_HV_BASE_FEAT_TIMER_FREQ_MSRS                    RT_BIT(11)
/** Debug MSRs available. */
#define GIM_HV_BASE_FEAT_DEBUG_MSRS                         RT_BIT(12)
/** @}  */

/** @name Hyper-V partition-creation feature identification.
 * Indicates flags specified during partition creation.
 * @{
 */
/** Create partitions. */
#define GIM_HV_PART_FLAGS_CREATE_PART                       RT_BIT(0)
/** Access partition Id. */
#define GIM_HV_PART_FLAGS_ACCESS_PART_ID                    RT_BIT(1)
/** Access memory pool. */
#define GIM_HV_PART_FLAGS_ACCESS_MEMORY_POOL                RT_BIT(2)
/** Adjust message buffers. */
#define GIM_HV_PART_FLAGS_ADJUST_MSG_BUFFERS                RT_BIT(3)
/** Post messages. */
#define GIM_HV_PART_FLAGS_POST_MSGS                         RT_BIT(4)
/** Signal events. */
#define GIM_HV_PART_FLAGS_SIGNAL_EVENTS                     RT_BIT(5)
/** Create port. */
#define GIM_HV_PART_FLAGS_CREATE_PORT                       RT_BIT(6)
/** Connect port. */
#define GIM_HV_PART_FLAGS_CONNECT_PORT                      RT_BIT(7)
/** Access statistics. */
#define GIM_HV_PART_FLAGS_ACCESS_STATS                      RT_BIT(8)
/** Debugging.*/
#define GIM_HV_PART_FLAGS_DEBUGGING                         RT_BIT(11)
/** CPU management. */
#define GIM_HV_PART_FLAGS_CPU_MGMT                          RT_BIT(12)
/** CPU profiler. */
#define GIM_HV_PART_FLAGS_CPU_PROFILER                      RT_BIT(13)
/** Enable expanded stack walking. */
#define GIM_HV_PART_FLAGS_EXPANDED_STACK_WALK               RT_BIT(14)
/** Access VSM. */
#define GIM_HV_PART_FLAGS_ACCESS_VSM                        RT_BIT(16)
/** Access VP registers. */
#define GIM_HV_PART_FLAGS_ACCESS_VP_REGS                    RT_BIT(17)
/** Enable extended hypercalls. */
#define GIM_HV_PART_FLAGS_EXTENDED_HYPERCALLS               RT_BIT(20)
/** Start virtual processor. */
#define GIM_HV_PART_FLAGS_START_VP                          RT_BIT(21)
/** @}  */

/** @name Hyper-V power management feature identification.
 * @{
 */
/** Maximum CPU power state C0. */
#define GIM_HV_PM_MAX_CPU_POWER_STATE_C0                    RT_BIT(0)
/** Maximum CPU power state C1. */
#define GIM_HV_PM_MAX_CPU_POWER_STATE_C1                    RT_BIT(1)
/** Maximum CPU power state C2. */
#define GIM_HV_PM_MAX_CPU_POWER_STATE_C2                    RT_BIT(2)
/** Maximum CPU power state C3. */
#define GIM_HV_PM_MAX_CPU_POWER_STATE_C3                    RT_BIT(3)
/** HPET is required to enter C3 power state. */
#define GIM_HV_PM_HPET_REQD_FOR_C3                          RT_BIT(4)
/** @}  */

/** @name Hyper-V miscellaneous feature identification.
 * Miscellaneous features available for the current partition.
 * @{
 */
/** MWAIT instruction available. */
#define GIM_HV_MISC_FEAT_MWAIT                              RT_BIT(0)
/** Guest debugging support available. */
#define GIM_HV_MISC_FEAT_GUEST_DEBUGGING                    RT_BIT(1)
/** Performance monitor support is available. */
#define GIM_HV_MISC_FEAT_PERF_MON                           RT_BIT(2)
/** Support for physical CPU dynamic partitioning events. */
#define GIM_HV_MISC_FEAT_PCPU_DYN_PART_EVENT                RT_BIT(3)
/** Support for passing hypercall input parameter block via XMM registers. */
#define GIM_HV_MISC_FEAT_XMM_HYPERCALL_INPUT                RT_BIT(4)
/** Support for virtual guest idle state. */
#define GIM_HV_MISC_FEAT_GUEST_IDLE_STATE                   RT_BIT(5)
/** Support for hypervisor sleep state. */
#define GIM_HV_MISC_FEAT_HYPERVISOR_SLEEP_STATE             RT_BIT(6)
/** Support for querying NUMA distances. */
#define GIM_HV_MISC_FEAT_QUERY_NUMA_DISTANCE                RT_BIT(7)
/** Support for determining timer frequencies. */
#define GIM_HV_MISC_FEAT_TIMER_FREQ                         RT_BIT(8)
/** Support for injecting synthetic machine checks. */
#define GIM_HV_MISC_FEAT_INJECT_SYNMC_XCPT                  RT_BIT(9)
/** Support for guest crash MSRs. */
#define GIM_HV_MISC_FEAT_GUEST_CRASH_MSRS                   RT_BIT(10)
/** Support for debug MSRs. */
#define GIM_HV_MISC_FEAT_DEBUG_MSRS                         RT_BIT(11)
/** Npiep1 Available */ /** @todo What the heck is this? */
#define GIM_HV_MISC_FEAT_NPIEP1                             RT_BIT(12)
/** Disable hypervisor available. */
#define GIM_HV_MISC_FEAT_DISABLE_HYPERVISOR                 RT_BIT(13)
/** Extended GVA ranges for FlushVirtualAddressList available. */
#define GIM_HV_MISC_FEAT_EXT_GVA_RANGE_FOR_FLUSH_VA_LIST    RT_BIT(14)
/** Support for returning hypercall output via XMM registers. */
#define GIM_HV_MISC_FEAT_HYPERCALL_OUTPUT_XMM               RT_BIT(15)
/** Synthetic interrupt source polling mode available. */
#define GIM_HV_MISC_FEAT_SINT_POLLING_MODE                  RT_BIT(17)
/** Hypercall MSR lock available. */
#define GIM_HV_MISC_FEAT_HYPERCALL_MSR_LOCK                 RT_BIT(18)
/** Use direct synthetic MSRs. */
#define GIM_HV_MISC_FEAT_USE_DIRECT_SYNTH_MSRS              RT_BIT(19)
/** @}  */

/** @name Hyper-V implementation recommendations.
 * Recommendations from the hypervisor for the guest for optimal performance.
 * @{
 */
/** Use hypercall for address space switches rather than MOV CR3. */
#define GIM_HV_HINT_HYPERCALL_FOR_PROCESS_SWITCH            RT_BIT(0)
/** Use hypercall for local TLB flushes rather than INVLPG/MOV CR3. */
#define GIM_HV_HINT_HYPERCALL_FOR_TLB_FLUSH                 RT_BIT(1)
/** Use hypercall for inter-CPU TLB flushes rather than IPIs. */
#define GIM_HV_HINT_HYPERCALL_FOR_TLB_SHOOTDOWN             RT_BIT(2)
/** Use MSRs for APIC access (EOI, ICR, TPR) rather than MMIO. */
#define GIM_HV_HINT_MSR_FOR_APIC_ACCESS                     RT_BIT(3)
/** Use hypervisor provided MSR for a system reset. */
#define GIM_HV_HINT_MSR_FOR_SYS_RESET                       RT_BIT(4)
/** Relax timer-related checks (watchdogs/deadman timeouts) that rely on
 *  timely deliver of external interrupts. */
#define GIM_HV_HINT_RELAX_TIME_CHECKS                       RT_BIT(5)
/** Recommend using DMA remapping. */
#define GIM_HV_HINT_DMA_REMAPPING                           RT_BIT(6)
/** Recommend using interrupt remapping. */
#define GIM_HV_HINT_INTERRUPT_REMAPPING                     RT_BIT(7)
/** Recommend using X2APIC MSRs rather than MMIO. */
#define GIM_HV_HINT_X2APIC_MSRS                             RT_BIT(8)
/** Recommend deprecating Auto EOI (end of interrupt). */
#define GIM_HV_HINT_DEPRECATE_AUTO_EOI                      RT_BIT(9)
/** Recommend using SyntheticClusterIpi hypercall. */
#define GIM_HV_HINT_SYNTH_CLUSTER_IPI_HYPERCALL             RT_BIT(10)
/** Recommend using newer ExProcessMasks interface. */
#define GIM_HV_HINT_EX_PROC_MASKS_INTERFACE                 RT_BIT(11)
/** Indicate that Hyper-V is nested within a Hyper-V partition. */
#define GIM_HV_HINT_NESTED_HYPERV                           RT_BIT(12)
/** Recommend using INT for MBEC system calls. */
#define GIM_HV_HINT_INT_FOR_MBEC_SYSCALLS                   RT_BIT(13)
/** Recommend using enlightened VMCS interfacea and nested enlightenments. */
#define GIM_HV_HINT_NESTED_ENLIGHTENED_VMCS_INTERFACE       RT_BIT(14)
/** @}  */


/** @name Hyper-V implementation hardware features.
 * Which hardware features are in use by the hypervisor.
 * @{
 */
/** APIC overlay is used. */
#define GIM_HV_HOST_FEAT_AVIC                               RT_BIT(0)
/** MSR bitmaps is used. */
#define GIM_HV_HOST_FEAT_MSR_BITMAP                         RT_BIT(1)
/** Architectural performance counter supported. */
#define GIM_HV_HOST_FEAT_PERF_COUNTER                       RT_BIT(2)
/** Nested paging is used. */
#define GIM_HV_HOST_FEAT_NESTED_PAGING                      RT_BIT(3)
/** DMA remapping is used. */
#define GIM_HV_HOST_FEAT_DMA_REMAPPING                      RT_BIT(4)
/** Interrupt remapping is used. */
#define GIM_HV_HOST_FEAT_INTERRUPT_REMAPPING                RT_BIT(5)
/** Memory patrol scrubber is present. */
#define GIM_HV_HOST_FEAT_MEM_PATROL_SCRUBBER                RT_BIT(6)
/** DMA protection is in use. */
#define GIM_HV_HOST_FEAT_DMA_PROT_IN_USE                    RT_BIT(7)
/** HPET is requested. */
#define GIM_HV_HOST_FEAT_HPET_REQUESTED                     RT_BIT(8)
/** Synthetic timers are volatile. */
#define GIM_HV_HOST_FEAT_STIMER_VOLATILE                    RT_BIT(9)
/** @}  */


/** @name Hyper-V MSRs.
 * @{
 */
/** Start of range 0. */
#define MSR_GIM_HV_RANGE0_FIRST                   UINT32_C(0x40000000)
/** Guest OS identification (R/W) */
#define MSR_GIM_HV_GUEST_OS_ID                    UINT32_C(0x40000000)
/** Enable hypercall interface (R/W) */
#define MSR_GIM_HV_HYPERCALL                      UINT32_C(0x40000001)
/** Virtual processor's (VCPU) index (R) */
#define MSR_GIM_HV_VP_INDEX                       UINT32_C(0x40000002)
/** Reset operation (R/W) */
#define MSR_GIM_HV_RESET                          UINT32_C(0x40000003)
/** End of range 0. */
#define MSR_GIM_HV_RANGE0_LAST                    MSR_GIM_HV_RESET

/** Start of range 1. */
#define MSR_GIM_HV_RANGE1_FIRST                   UINT32_C(0x40000010)
/** Virtual processor's (VCPU) runtime (R) */
#define MSR_GIM_HV_VP_RUNTIME                     UINT32_C(0x40000010)
/** End of range 1. */
#define MSR_GIM_HV_RANGE1_LAST                    MSR_GIM_HV_VP_RUNTIME

/** Start of range 2. */
#define MSR_GIM_HV_RANGE2_FIRST                   UINT32_C(0x40000020)
/** Per-VM reference counter (R) */
#define MSR_GIM_HV_TIME_REF_COUNT                 UINT32_C(0x40000020)
/** Per-VM TSC page (R/W) */
#define MSR_GIM_HV_REF_TSC                        UINT32_C(0x40000021)
/** Frequency of TSC in Hz as reported by the hypervisor (R) */
#define MSR_GIM_HV_TSC_FREQ                       UINT32_C(0x40000022)
/** Frequency of LAPIC in Hz as reported by the hypervisor (R) */
#define MSR_GIM_HV_APIC_FREQ                      UINT32_C(0x40000023)
/** End of range 2. */
#define MSR_GIM_HV_RANGE2_LAST                    MSR_GIM_HV_APIC_FREQ

/** Start of range 3. */
#define MSR_GIM_HV_RANGE3_FIRST                   UINT32_C(0x40000070)
/** Access to APIC EOI (End-Of-Interrupt) register (W) */
#define MSR_GIM_HV_EOI                            UINT32_C(0x40000070)
/** Access to APIC ICR (Interrupt Command) register (R/W) */
#define MSR_GIM_HV_ICR                            UINT32_C(0x40000071)
/** Access to APIC TPR (Task Priority) register (R/W) */
#define MSR_GIM_HV_TPR                            UINT32_C(0x40000072)
/** Enables lazy EOI processing (R/W) */
#define MSR_GIM_HV_APIC_ASSIST_PAGE               UINT32_C(0x40000073)
/** End of range 3. */
#define MSR_GIM_HV_RANGE3_LAST                    MSR_GIM_HV_APIC_ASSIST_PAGE

/** Start of range 4. */
#define MSR_GIM_HV_RANGE4_FIRST                   UINT32_C(0x40000080)
/** Control behaviour of synthetic interrupt controller (R/W) */
#define MSR_GIM_HV_SCONTROL                       UINT32_C(0x40000080)
/** Synthetic interrupt controller version (R) */
#define MSR_GIM_HV_SVERSION                       UINT32_C(0x40000081)
/** Base address of synthetic interrupt event flag (R/W) */
#define MSR_GIM_HV_SIEFP                          UINT32_C(0x40000082)
/** Base address of synthetic interrupt message page (R/W) */
#define MSR_GIM_HV_SIMP                           UINT32_C(0x40000083)
/** End-Of-Message in synthetic interrupt parameter page (W) */
#define MSR_GIM_HV_EOM                            UINT32_C(0x40000084)
/** End of range 4. */
#define MSR_GIM_HV_RANGE4_LAST                    MSR_GIM_HV_EOM

/** Start of range 5. */
#define MSR_GIM_HV_RANGE5_FIRST                   UINT32_C(0x40000090)
/** Configures synthetic interrupt source 0 (R/W) */
#define MSR_GIM_HV_SINT0                          UINT32_C(0x40000090)
/** Configures synthetic interrupt source 1 (R/W) */
#define MSR_GIM_HV_SINT1                          UINT32_C(0x40000091)
/** Configures synthetic interrupt source 2 (R/W) */
#define MSR_GIM_HV_SINT2                          UINT32_C(0x40000092)
/** Configures synthetic interrupt source 3 (R/W) */
#define MSR_GIM_HV_SINT3                          UINT32_C(0x40000093)
/** Configures synthetic interrupt source 4 (R/W) */
#define MSR_GIM_HV_SINT4                          UINT32_C(0x40000094)
/** Configures synthetic interrupt source 5 (R/W) */
#define MSR_GIM_HV_SINT5                          UINT32_C(0x40000095)
/** Configures synthetic interrupt source 6 (R/W) */
#define MSR_GIM_HV_SINT6                          UINT32_C(0x40000096)
/** Configures synthetic interrupt source 7 (R/W) */
#define MSR_GIM_HV_SINT7                          UINT32_C(0x40000097)
/** Configures synthetic interrupt source 8 (R/W) */
#define MSR_GIM_HV_SINT8                          UINT32_C(0x40000098)
/** Configures synthetic interrupt source 9 (R/W) */
#define MSR_GIM_HV_SINT9                          UINT32_C(0x40000099)
/** Configures synthetic interrupt source 10 (R/W) */
#define MSR_GIM_HV_SINT10                         UINT32_C(0x4000009A)
/** Configures synthetic interrupt source 11 (R/W) */
#define MSR_GIM_HV_SINT11                         UINT32_C(0x4000009B)
/** Configures synthetic interrupt source 12 (R/W) */
#define MSR_GIM_HV_SINT12                         UINT32_C(0x4000009C)
/** Configures synthetic interrupt source 13 (R/W) */
#define MSR_GIM_HV_SINT13                         UINT32_C(0x4000009D)
/** Configures synthetic interrupt source 14 (R/W) */
#define MSR_GIM_HV_SINT14                         UINT32_C(0x4000009E)
/** Configures synthetic interrupt source 15 (R/W) */
#define MSR_GIM_HV_SINT15                         UINT32_C(0x4000009F)
/** End of range 5. */
#define MSR_GIM_HV_RANGE5_LAST                    MSR_GIM_HV_SINT15

/** Start of range 6. */
#define MSR_GIM_HV_RANGE6_FIRST                   UINT32_C(0x400000B0)
/** Configures register for synthetic timer 0 (R/W) */
#define MSR_GIM_HV_STIMER0_CONFIG                 UINT32_C(0x400000B0)
/** Expiration time or period for synthetic timer 0 (R/W) */
#define MSR_GIM_HV_STIMER0_COUNT                  UINT32_C(0x400000B1)
/** Configures register for synthetic timer 1 (R/W) */
#define MSR_GIM_HV_STIMER1_CONFIG                 UINT32_C(0x400000B2)
/** Expiration time or period for synthetic timer 1 (R/W) */
#define MSR_GIM_HV_STIMER1_COUNT                  UINT32_C(0x400000B3)
/** Configures register for synthetic timer 2 (R/W) */
#define MSR_GIM_HV_STIMER2_CONFIG                 UINT32_C(0x400000B4)
/** Expiration time or period for synthetic timer 2 (R/W) */
#define MSR_GIM_HV_STIMER2_COUNT                  UINT32_C(0x400000B5)
/** Configures register for synthetic timer 3 (R/W) */
#define MSR_GIM_HV_STIMER3_CONFIG                 UINT32_C(0x400000B6)
/** Expiration time or period for synthetic timer 3 (R/W) */
#define MSR_GIM_HV_STIMER3_COUNT                  UINT32_C(0x400000B7)
/** End of range 6. */
#define MSR_GIM_HV_RANGE6_LAST                    MSR_GIM_HV_STIMER3_COUNT

/** Start of range 7. */
#define MSR_GIM_HV_RANGE7_FIRST                   UINT32_C(0x400000C1)
/** Trigger to transition to power state C1 (R) */
#define MSR_GIM_HV_POWER_STATE_TRIGGER_C1         UINT32_C(0x400000C1)
/** Trigger to transition to power state C2 (R) */
#define MSR_GIM_HV_POWER_STATE_TRIGGER_C2         UINT32_C(0x400000C2)
/** Trigger to transition to power state C3 (R) */
#define MSR_GIM_HV_POWER_STATE_TRIGGER_C3         UINT32_C(0x400000C3)
/** End of range 7. */
#define MSR_GIM_HV_RANGE7_LAST                    MSR_GIM_HV_POWER_STATE_TRIGGER_C3

/** Start of range 8. */
#define MSR_GIM_HV_RANGE8_FIRST                   UINT32_C(0x400000D1)
/** Configure the recipe for power state transitions to C1 (R/W) */
#define MSR_GIM_HV_POWER_STATE_CONFIG_C1          UINT32_C(0x400000D1)
/** Configure the recipe for power state transitions to C2 (R/W) */
#define MSR_GIM_HV_POWER_STATE_CONFIG_C2          UINT32_C(0x400000D2)
/** Configure the recipe for power state transitions to C3 (R/W) */
#define MSR_GIM_HV_POWER_STATE_CONFIG_C3          UINT32_C(0x400000D3)
/** End of range 8. */
#define MSR_GIM_HV_RANGE8_LAST                    MSR_GIM_HV_POWER_STATE_CONFIG_C3

/** Start of range 9. */
#define MSR_GIM_HV_RANGE9_FIRST                   UINT32_C(0x400000E0)
/** Map the guest's retail partition stats page (R/W) */
#define MSR_GIM_HV_STATS_PART_RETAIL_PAGE         UINT32_C(0x400000E0)
/** Map the guest's internal partition stats page (R/W) */
#define MSR_GIM_HV_STATS_PART_INTERNAL_PAGE       UINT32_C(0x400000E1)
/** Map the guest's retail VP stats page (R/W) */
#define MSR_GIM_HV_STATS_VP_RETAIL_PAGE           UINT32_C(0x400000E2)
/** Map the guest's internal VP stats page (R/W) */
#define MSR_GIM_HV_STATS_VP_INTERNAL_PAGE         UINT32_C(0x400000E3)
/** End of range 9. */
#define MSR_GIM_HV_RANGE9_LAST                    MSR_GIM_HV_STATS_VP_INTERNAL_PAGE

/** Start of range 10. */
#define MSR_GIM_HV_RANGE10_FIRST                  UINT32_C(0x400000F0)
/** Trigger the guest's transition to idle power state (R) */
#define MSR_GIM_HV_GUEST_IDLE                     UINT32_C(0x400000F0)
/** Synthetic debug control. */
#define MSR_GIM_HV_SYNTH_DEBUG_CONTROL            UINT32_C(0x400000F1)
/** Synthetic debug status. */
#define MSR_GIM_HV_SYNTH_DEBUG_STATUS             UINT32_C(0x400000F2)
/** Synthetic debug send buffer. */
#define MSR_GIM_HV_SYNTH_DEBUG_SEND_BUFFER        UINT32_C(0x400000F3)
/** Synthetic debug receive buffer. */
#define MSR_GIM_HV_SYNTH_DEBUG_RECEIVE_BUFFER     UINT32_C(0x400000F4)
/** Synthetic debug pending buffer. */
#define MSR_GIM_HV_SYNTH_DEBUG_PENDING_BUFFER     UINT32_C(0x400000F5)
/** End of range 10. */
#define MSR_GIM_HV_RANGE10_LAST                   MSR_GIM_HV_SYNTH_DEBUG_PENDING_BUFFER

/** Start of range 11. */
#define MSR_GIM_HV_RANGE11_FIRST                  UINT32_C(0x400000FF)
/** Undocumented debug options MSR. */
#define MSR_GIM_HV_DEBUG_OPTIONS_MSR              UINT32_C(0x400000FF)
/** End of range 11. */
#define MSR_GIM_HV_RANGE11_LAST                   MSR_GIM_HV_DEBUG_OPTIONS_MSR

/** Start of range 12. */
#define MSR_GIM_HV_RANGE12_FIRST                  UINT32_C(0x40000100)
/** Guest crash MSR 0. */
#define MSR_GIM_HV_CRASH_P0                       UINT32_C(0x40000100)
/** Guest crash MSR 1. */
#define MSR_GIM_HV_CRASH_P1                       UINT32_C(0x40000101)
/** Guest crash MSR 2. */
#define MSR_GIM_HV_CRASH_P2                       UINT32_C(0x40000102)
/** Guest crash MSR 3. */
#define MSR_GIM_HV_CRASH_P3                       UINT32_C(0x40000103)
/** Guest crash MSR 4. */
#define MSR_GIM_HV_CRASH_P4                       UINT32_C(0x40000104)
/** Guest crash control. */
#define MSR_GIM_HV_CRASH_CTL                      UINT32_C(0x40000105)
/** End of range 12. */
#define MSR_GIM_HV_RANGE12_LAST                   MSR_GIM_HV_CRASH_CTL
/** @} */

AssertCompile(MSR_GIM_HV_RANGE0_FIRST  <= MSR_GIM_HV_RANGE0_LAST);
AssertCompile(MSR_GIM_HV_RANGE1_FIRST  <= MSR_GIM_HV_RANGE1_LAST);
AssertCompile(MSR_GIM_HV_RANGE2_FIRST  <= MSR_GIM_HV_RANGE2_LAST);
AssertCompile(MSR_GIM_HV_RANGE3_FIRST  <= MSR_GIM_HV_RANGE3_LAST);
AssertCompile(MSR_GIM_HV_RANGE4_FIRST  <= MSR_GIM_HV_RANGE4_LAST);
AssertCompile(MSR_GIM_HV_RANGE5_FIRST  <= MSR_GIM_HV_RANGE5_LAST);
AssertCompile(MSR_GIM_HV_RANGE6_FIRST  <= MSR_GIM_HV_RANGE6_LAST);
AssertCompile(MSR_GIM_HV_RANGE7_FIRST  <= MSR_GIM_HV_RANGE7_LAST);
AssertCompile(MSR_GIM_HV_RANGE8_FIRST  <= MSR_GIM_HV_RANGE8_LAST);
AssertCompile(MSR_GIM_HV_RANGE9_FIRST  <= MSR_GIM_HV_RANGE9_LAST);
AssertCompile(MSR_GIM_HV_RANGE10_FIRST <= MSR_GIM_HV_RANGE10_LAST);
AssertCompile(MSR_GIM_HV_RANGE11_FIRST <= MSR_GIM_HV_RANGE11_LAST);

/** @name Hyper-V MSR - Reset (MSR_GIM_HV_RESET).
 * @{
 */
/** The reset enable mask. */
#define MSR_GIM_HV_RESET_ENABLE                       RT_BIT_64(0)
/** Whether the reset MSR is enabled. */
#define MSR_GIM_HV_RESET_IS_ENABLED(a)                RT_BOOL((a) & MSR_GIM_HV_RESET_ENABLE)
/** @} */

/** @name Hyper-V MSR - Hypercall (MSR_GIM_HV_HYPERCALL).
 * @{
 */
/** Guest-physical page frame number of the hypercall-page. */
#define MSR_GIM_HV_HYPERCALL_GUEST_PFN(a)         ((a) >> 12)
/** The hypercall enable mask. */
#define MSR_GIM_HV_HYPERCALL_PAGE_ENABLE          RT_BIT_64(0)
/** Whether the hypercall-page is enabled or not. */
#define MSR_GIM_HV_HYPERCALL_PAGE_IS_ENABLED(a)   RT_BOOL((a) & MSR_GIM_HV_HYPERCALL_PAGE_ENABLE)
/** @} */

/** @name Hyper-V MSR - Reference TSC (MSR_GIM_HV_REF_TSC).
 * @{
 */
/** Guest-physical page frame number of the TSC-page. */
#define MSR_GIM_HV_REF_TSC_GUEST_PFN(a)           ((a) >> 12)
/** The TSC-page enable mask. */
#define MSR_GIM_HV_REF_TSC_ENABLE                 RT_BIT_64(0)
/** Whether the TSC-page is enabled or not. */
#define MSR_GIM_HV_REF_TSC_IS_ENABLED(a)          RT_BOOL((a) & MSR_GIM_HV_REF_TSC_ENABLE)
/** @} */

/** @name Hyper-V MSR - Guest crash control (MSR_GIM_HV_CRASH_CTL).
 * @{
 */
/** The Crash Control notify mask. */
#define MSR_GIM_HV_CRASH_CTL_NOTIFY               RT_BIT_64(63)
/** @} */

/** @name Hyper-V MSR - Guest OS ID (MSR_GIM_HV_GUEST_OS_ID).
 * @{
 */
/** An open-source operating system. */
#define MSR_GIM_HV_GUEST_OS_ID_IS_OPENSOURCE(a)   RT_BOOL((a) & RT_BIT_64(63))
/** Vendor ID. */
#define MSR_GIM_HV_GUEST_OS_ID_VENDOR(a)          (uint32_t)(((a) >> 48) & 0xfff)
/** Guest OS variant, depending on the vendor ID.  */
#define MSR_GIM_HV_GUEST_OS_ID_OS_VARIANT(a)      (uint32_t)(((a) >> 40) & 0xff)
/** Guest OS major version. */
#define MSR_GIM_HV_GUEST_OS_ID_MAJOR_VERSION(a)   (uint32_t)(((a) >> 32) & 0xff)
/** Guest OS minor version. */
#define MSR_GIM_HV_GUEST_OS_ID_MINOR_VERSION(a)   (uint32_t)(((a) >> 24) & 0xff)
/** Guest OS service version (e.g. service pack number in case of Windows). */
#define MSR_GIM_HV_GUEST_OS_ID_SERVICE_VERSION(a) (uint32_t)(((a) >> 16) & 0xff)
/** Guest OS build number. */
#define MSR_GIM_HV_GUEST_OS_ID_BUILD(a)           (uint32_t)((a) & 0xffff)
/** @} */

/** @name Hyper-V MSR - APIC-assist page (MSR_GIM_HV_APIC_ASSIST_PAGE).
 * @{
 */
/** Guest-physical page frame number of the APIC-assist page. */
#define MSR_GIM_HV_APICASSIST_GUEST_PFN(a)        ((a) >> 12)
/** The APIC-assist page enable mask. */
#define MSR_GIM_HV_APICASSIST_PAGE_ENABLE         RT_BIT_64(0)
/** Whether the APIC-assist page is enabled or not. */
#define MSR_GIM_HV_APICASSIST_PAGE_IS_ENABLED(a)  RT_BOOL((a) & MSR_GIM_HV_APICASSIST_PAGE_ENABLE)
/** @} */

/** @name Hyper-V MSR - Synthetic Interrupt Event Flags page
 *        (MSR_GIM_HV_SIEFP).
 * @{
 */
/** Guest-physical page frame number of the APIC-assist page. */
#define MSR_GIM_HV_SIEF_GUEST_PFN(a)              ((a) >> 12)
/** The SIEF enable mask. */
#define MSR_GIM_HV_SIEF_PAGE_ENABLE               RT_BIT_64(0)
/** Whether the SIEF page is enabled or not. */
#define MSR_GIM_HV_SIEF_PAGE_IS_ENABLED(a)        RT_BOOL((a) & MSR_GIM_HV_SIEF_PAGE_ENABLE)
/** @} */

/** @name Hyper-V MSR - Synthetic Interrupt Control (MSR_GIM_HV_CONTROL).
 * @{
 */
/** The SControl enable mask. */
#define MSR_GIM_HV_SCONTROL_ENABLE                RT_BIT_64(0)
/** Whether SControl is enabled or not. */
#define MSR_GIM_HV_SCONTROL_IS_ENABLED(a)         RT_BOOL((a) & MSR_GIM_HV_SCONTROL_ENABLE)
/** @} */

/** @name Hyper-V MSR - Synthetic Timer Config (MSR_GIM_HV_STIMER_CONFIG).
 * @{
 */
/** The Stimer enable mask. */
#define MSR_GIM_HV_STIMER_ENABLE                  RT_BIT_64(0)
/** Whether Stimer is enabled or not. */
#define MSR_GIM_HV_STIMER_IS_ENABLED(a)           RT_BOOL((a) & MSR_GIM_HV_STIMER_ENABLE)
/** The Stimer periodic mask. */
#define MSR_GIM_HV_STIMER_PERIODIC                RT_BIT_64(1)
/** Whether Stimer is enabled or not. */
#define MSR_GIM_HV_STIMER_IS_PERIODIC(a)          RT_BOOL((a) & MSR_GIM_HV_STIMER_PERIODIC)
/** The Stimer lazy mask. */
#define MSR_GIM_HV_STIMER_LAZY                    RT_BIT_64(2)
/** Whether Stimer is enabled or not. */
#define MSR_GIM_HV_STIMER_IS_LAZY(a)              RT_BOOL((a) & MSR_GIM_HV_STIMER_LAZY)
/** The Stimer auto-enable mask. */
#define MSR_GIM_HV_STIMER_AUTO_ENABLE             RT_BIT_64(3)
/** Whether Stimer is enabled or not. */
#define MSR_GIM_HV_STIMER_IS_AUTO_ENABLED(a)      RT_BOOL((a) & MSR_GIM_HV_STIMER_AUTO_ENABLE)
/** The Stimer SINTx mask (bits 16:19). */
#define MSR_GIM_HV_STIMER_SINTX                   UINT64_C(0xf0000)
/** Gets the Stimer synthetic interrupt source. */
#define MSR_GIM_HV_STIMER_GET_SINTX(a)            (((a) >> 16) & 0xf)
/** The Stimer valid read/write mask. */
#define MSR_GIM_HV_STIMER_RW_VALID                (  MSR_GIM_HV_STIMER_ENABLE | MSR_GIM_HV_STIMER_PERIODIC    \
                                                   | MSR_GIM_HV_STIMER_LAZY   | MSR_GIM_HV_STIMER_AUTO_ENABLE \
                                                   | MSR_GIM_HV_STIMER_SINTX)
/** @} */


/** Hyper-V page size.  */
#define GIM_HV_PAGE_SIZE                          4096
/** Hyper-V page shift. */
#define GIM_HV_PAGE_SHIFT                         12

/** Microsoft Hyper-V vendor signature. */
#define GIM_HV_VENDOR_MICROSOFT                   "Microsoft Hv"

/**
 * Hyper-V APIC-assist (HV_REFERENCE_TSC_PAGE) structure placed in the TSC
 * reference page.
 */
typedef struct GIMHVAPICASSIST
{
    uint32_t fNoEoiRequired : 1;
    uint32_t u31Reserved0   : 31;
} GIMHVAPICASSIST;
/** Pointer to Hyper-V reference TSC. */
typedef GIMHVAPICASSIST *PGIMHVAPICASSIST;
/** Pointer to a const Hyper-V reference TSC. */
typedef GIMHVAPICASSIST const *PCGIMHVAPICASSIST;
AssertCompileSize(GIMHVAPICASSIST, 4);

/**
 * Hypercall parameter type.
 */
typedef enum GIMHVHYPERCALLPARAM
{
    GIMHVHYPERCALLPARAM_IN = 0,
    GIMHVHYPERCALLPARAM_OUT
} GIMHVHYPERCALLPARAM;


/** @name Hyper-V hypercall op codes.
 * @{
 */
/** Post message to hypervisor or VMs. */
#define GIM_HV_HYPERCALL_OP_POST_MESSAGE          0x5C
/** Post debug data to hypervisor. */
#define GIM_HV_HYPERCALL_OP_POST_DEBUG_DATA       0x69
/** Retreive debug data from hypervisor. */
#define GIM_HV_HYPERCALL_OP_RETREIVE_DEBUG_DATA   0x6A
/** Reset debug session. */
#define GIM_HV_HYPERCALL_OP_RESET_DEBUG_SESSION   0x6B
/** @} */

/** @name Hyper-V extended hypercall op codes.
 * @{
 */
/** Query extended hypercall capabilities. */
#define GIM_HV_EXT_HYPERCALL_OP_QUERY_CAP                0x8001
/** Query guest physical address range that has zero'd filled memory. */
#define GIM_HV_EXT_HYPERCALL_OP_GET_BOOT_ZEROED_MEM      0x8002
/** @} */


/** @name Hyper-V Extended hypercall - HvExtCallQueryCapabilities.
 * @{
 */
/** Boot time zeroed pages. */
#define GIM_HV_EXT_HYPERCALL_CAP_ZERO_MEM                       RT_BIT_64(0)
/** Whether boot time zeroed pages capability is enabled. */
#define GIM_HV_EXT_HYPERCALL_CAP_IS_ZERO_MEM_ENABLED(a)         RT_BOOL((a) & GIM_HV_EXT_HYPERCALL_CAP_ZERO_MEM)
/** @} */


/** @name Hyper-V hypercall inputs.
 * @{
 */
/** The hypercall call operation code. */
#define GIM_HV_HYPERCALL_IN_CALL_CODE(a)         ((a) & UINT64_C(0xffff))
/** Whether it's a fast (register based) hypercall or not (memory-based). */
#define GIM_HV_HYPERCALL_IN_IS_FAST(a)           RT_BOOL((a) & RT_BIT_64(16))
/** Total number of reps for a rep hypercall. */
#define GIM_HV_HYPERCALL_IN_REP_COUNT(a)         (((a) << 32) & UINT64_C(0xfff))
/** Rep start index for a rep hypercall. */
#define GIM_HV_HYPERCALL_IN_REP_START_IDX(a)     (((a) << 48) & UINT64_C(0xfff))
/** Reserved bits range 1. */
#define GIM_HV_HYPERCALL_IN_RSVD_1(a)            (((a) << 17) & UINT64_C(0x7fff))
/** Reserved bits range 2. */
#define GIM_HV_HYPERCALL_IN_RSVD_2(a)            (((a) << 44) & UINT64_C(0xf))
/** Reserved bits range 3. */
#define GIM_HV_HYPERCALL_IN_RSVD_3(a)            (((a) << 60) & UINT64_C(0x7))
/** @} */


/** @name Hyper-V hypercall status codes.
 * @{
 */
/** Success. */
#define GIM_HV_STATUS_SUCCESS                                        0x00
/** Unrecognized hypercall. */
#define GIM_HV_STATUS_INVALID_HYPERCALL_CODE                         0x02
/** Invalid hypercall input (rep count, rsvd bits). */
#define GIM_HV_STATUS_INVALID_HYPERCALL_INPUT                        0x03
/** Hypercall guest-physical address not 8-byte aligned or crosses page boundary. */
#define GIM_HV_STATUS_INVALID_ALIGNMENT                              0x04
/** Invalid hypercall parameters. */
#define GIM_HV_STATUS_INVALID_PARAMETER                              0x05
/** Access denied. */
#define GIM_HV_STATUS_ACCESS_DENIED                                  0x06
/** The partition state not valid for specified op. */
#define GIM_HV_STATUS_INVALID_PARTITION_STATE                        0x07
/** The hypercall operation could not be performed. */
#define GIM_HV_STATUS_OPERATION_DENIED                               0x08
/** Specified partition property ID not recognized. */
#define GIM_HV_STATUS_UNKNOWN_PROPERTY                               0x09
/** Specified partition property value not within range. */
#define GIM_HV_STATUS_PROPERTY_VALUE_OUT_OF_RANGE                    0x0a
/** Insufficient memory for performing the hypercall. */
#define GIM_HV_STATUS_INSUFFICIENT_MEMORY                            0x0b
/** Maximum partition depth has been exceeded for the partition hierarchy. */
#define GIM_HV_STATUS_PARTITION_TOO_DEEP                             0x0c
/** The specified partition ID is not valid. */
#define GIM_HV_STATUS_INVALID_PARTITION_ID                           0x0d
/** The specified virtual processor index in invalid. */
#define GIM_HV_STATUS_INVALID_VP_INDEX                               0x0e
/** The specified port ID is not unique or doesn't exist. */
#define GIM_HV_STATUS_INVALID_PORT_ID                                0x11
/** The specified connection ID is not unique or doesn't exist. */
#define GIM_HV_STATUS_INVALID_CONNECTION_ID                          0x12
/** The target port doesn't have sufficient buffers for the caller to post a message. */
#define GIM_HV_STATUS_INSUFFICIENT_BUFFERS                           0x13
/** External interrupt not acknowledged.*/
#define GIM_HV_STATUS_NOT_ACKNOWLEDGED                               0x14
/** External interrupt acknowledged. */
#define GIM_HV_STATUS_ACKNOWLEDGED                                   0x16
/** Invalid state due to misordering Hv[Save|Restore]PartitionState. */
#define GIM_HV_STATUS_INVALID_SAVE_RESTORE_STATE                     0x17
/** Operation not perform due to a required feature of SynIc was disabled. */
#define GIM_HV_STATUS_INVALID_SYNIC_STATE                            0x18
/** Object or value already in use. */
#define GIM_HV_STATUS_OBJECT_IN_USE                                  0x19
/** Invalid proximity domain information. */
#define GIM_HV_STATUS_INVALID_PROXIMITY_DOMAIN_INFO                  0x1A
/** Attempt to retrieve data failed. */
#define GIM_HV_STATUS_NO_DATA                                        0x1B
/** Debug connection has not recieved any new data since the last time. */
#define GIM_HV_STATUS_INACTIVE                                       0x1C
/** A resource is unavailable for allocation. */
#define GIM_HV_STATUS_NO_RESOURCES                                   0x1D
/** A hypervisor feature is not available to the caller. */
#define GIM_HV_STATUS_FEATURE_UNAVAILABLE                            0x1E
/** The debug packet returned is partial due to an I/O error. */
#define GIM_HV_STATUS_PARTIAL_PACKET                                 0x1F
/** Processor feature SSE3 unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_SSE3_NOT_SUPPORTED                   0x20
/** Processor feature LAHSAHF unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_LAHSAHF_NOT_SUPPORTED                0x21
/** Processor feature SSSE3 unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_SSSE3_NOT_SUPPORTED                  0x22
/** Processor feature SSE4.1 unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_SSE4_1_NOT_SUPPORTED                 0x23
/** Processor feature SSE4.2 unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_SSE4_2_NOT_SUPPORTED                 0x24
/** Processor feature SSE4A unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_SSE4A_NOT_SUPPORTED                  0x25
/** Processor feature XOP unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_XOP_NOT_SUPPORTED                    0x26
/** Processor feature POPCNT unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_POPCNT_NOT_SUPPORTED                 0x27
/** Processor feature CMPXCHG16B unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_CMPXCHG16B_NOT_SUPPORTED             0x28
/** Processor feature ALTMOVCR8 unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_ALTMOVCR8_NOT_SUPPORTED              0x29
/** Processor feature LZCNT unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_LZCNT_NOT_SUPPORTED                  0x2A
/** Processor feature misaligned SSE unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_MISALIGNED_SSE_NOT_SUPPORTED         0x2B
/** Processor feature MMX extensions unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_MMX_EXT_NOT_SUPPORTED                0x2C
/** Processor feature 3DNow! unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_3DNOW_NOT_SUPPORTED                  0x2D
/** Processor feature Extended 3DNow! unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_EXTENDED_3DNOW_NOT_SUPPORTED         0x2E
/** Processor feature 1GB large page unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_PAGE_1GB_NOT_SUPPORTED               0x2F
/** Processor cache line flush size incompatible. */
#define GIM_HV_STATUS_PROC_CACHE_LINE_FLUSH_SIZE_INCOMPATIBLE        0x30
/** Processor feature XSAVE unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_XSAVE_NOT_SUPPORTED                  0x31
/** Processor feature XSAVEOPT unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_XSAVEOPT_NOT_SUPPORTED               0x32
/** The specified buffer was too small for all requested data. */
#define GIM_HV_STATUS_INSUFFICIENT_BUFFER                            0x33
/** Processor feature XSAVEOPT unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_XSAVE_AVX_NOT_SUPPORTED              0x34
/** Processor feature XSAVEOPT unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_XSAVE_FEAT_NOT_SUPPORTED             0x35   /** Huh, isn't this same as 0x31? */
/** Processor feature XSAVEOPT unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_PAGE_XSAVE_SAVE_AREA_INCOMPATIBLE    0x36
/** Processor architecture unsupoorted. */
#define GIM_HV_STATUS_INCOMPATIBLE_PROCESSOR                         0x37
/** Max. domains for platform I/O remapping reached. */
#define GIM_HV_STATUS_INSUFFICIENT_DEVICE_DOMAINS                    0x38
/** Processor feature AES unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_AES_NOT_SUPPORTED                    0x39
/** Processor feature PCMULQDQ unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_PCMULQDQ_NOT_SUPPORTED               0x3A
/** Processor feature XSAVE features unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_XSAVE_FEATURES_INCOMPATIBLE          0x3B
/** Generic CPUID validation error. */
#define GIM_HV_STATUS_CPUID_FEAT_VALIDATION_ERROR                    0x3C
/** XSAVE CPUID validation error. */
#define GIM_HV_STATUS_CPUID_XSAVE_FEAT_VALIDATION_ERROR              0x3D
/** Processor startup timed out. */
#define GIM_HV_STATUS_PROCESSOR_STARTUP_TIMEOUT                      0x3E
/** SMX enabled by the BIOS. */
#define GIM_HV_STATUS_SMX_ENABLED                                    0x3F
/** Processor feature PCID unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_PCID_NOT_SUPPORTED                   0x40
/** Invalid LP index. */
#define GIM_HV_STATUS_INVALID_LP_INDEX                               0x41
/** Processor feature PCID unsupported. */
#define GIM_HV_STATUS_FEAT_FMA4_NOT_SUPPORTED                        0x42
/** Processor feature PCID unsupported. */
#define GIM_HV_STATUS_FEAT_F16C_NOT_SUPPORTED                        0x43
/** Processor feature PCID unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_RDRAND_NOT_SUPPORTED                 0x44
/** Processor feature RDWRFSGS unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_RDWRFSGS_NOT_SUPPORTED               0x45
/** Processor feature SMEP unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_SMEP_NOT_SUPPORTED                   0x46
/** Processor feature enhanced fast string unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_ENHANCED_FAST_STRING_NOT_SUPPORTED   0x47
/** Processor feature MOVBE unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_MOVBE_NOT_SUPPORTED                  0x48
/** Processor feature BMI1 unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_BMI1_NOT_SUPPORTED                   0x49
/** Processor feature BMI2 unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_BMI2_NOT_SUPPORTED                   0x4A
/** Processor feature HLE unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_HLE_NOT_SUPPORTED                    0x4B
/** Processor feature RTM unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_RTM_NOT_SUPPORTED                    0x4C
/** Processor feature XSAVE FMA unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_XSAVE_FMA_NOT_SUPPORTED              0x4D
/** Processor feature XSAVE AVX2 unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_XSAVE_AVX2_NOT_SUPPORTED             0x4E
/** Processor feature NPIEP1 unsupported. */
#define GIM_HV_STATUS_PROC_FEAT_NPIEP1_NOT_SUPPORTED                 0x4F
/** @} */


/** @name Hyper-V MSR - Debug control (MSR_GIM_HV_SYNTH_DEBUG_CONTROL).
 * @{
 */
/** Perform debug write. */
#define MSR_GIM_HV_SYNTH_DEBUG_CONTROL_IS_WRITE(a)     RT_BOOL((a) & RT_BIT_64(0))
/** Perform debug read. */
#define MSR_GIM_HV_SYNTH_DEBUG_CONTROL_IS_READ(a)      RT_BOOL((a) & RT_BIT_64(1))
/** Returns length of the debug write buffer. */
#define MSR_GIM_HV_SYNTH_DEBUG_CONTROL_W_LEN(a)        (((a) & UINT64_C(0xffff0000)) >> 16)
/** @} */


/** @name Hyper-V MSR - Debug status (MSR_GIM_HV_SYNTH_DEBUG_STATUS).
 * @{
 */
/** Debug send buffer operation success. */
#define MSR_GIM_HV_SYNTH_DEBUG_STATUS_W_SUCCESS        RT_BIT_64(0)
/** Debug receive buffer operation success. */
#define MSR_GIM_HV_SYNTH_DEBUG_STATUS_R_SUCCESS        RT_BIT_64(2)
/** Debug connection was reset. */
#define MSR_GIM_HV_SYNTH_DEBUG_STATUS_CONN_RESET       RT_BIT_64(3)
/** @} */


/** @name Hyper-V MSR - synthetic interrupt (MSR_GIM_HV_SINTx).
 * @{
 */
/** The interrupt masked mask. */
#define MSR_GIM_HV_SINT_MASKED                         RT_BIT_64(16)
/** Whether the interrupt source is masked. */
#define MSR_GIM_HV_SINT_IS_MASKED(a)                   RT_BOOL((a) & MSR_GIM_HV_SINT_MASKED)
/** Gets the interrupt vector. */
#define MSR_GIM_HV_SINT_GET_VECTOR(a)                  ((a) & UINT64_C(0xff))
/** The AutoEoi mask. */
#define MSR_GIM_HV_SINT_AUTOEOI                        RT_BIT_64(17)
/** Gets whether AutoEoi is enabled for the synthetic interrupt. */
#define MSR_GIM_HV_SINT_IS_AUTOEOI(a)                  RT_BOOL((a) & MSR_GIM_HV_SINT_AUTOEOI)
/** @} */


/** @name Hyper-V MSR - synthetic interrupt message page (MSR_GIM_HV_SIMP).
 * @{
 */
/** The SIMP enable mask. */
#define MSR_GIM_HV_SIMP_ENABLE                         RT_BIT_64(0)
/** Whether the SIMP is enabled. */
#define MSR_GIM_HV_SIMP_IS_ENABLED(a)                  RT_BOOL((a) & MSR_GIM_HV_SIMP_ENABLE)
/** The SIMP guest-physical address. */
#define MSR_GIM_HV_SIMP_GPA(a)                         ((a) & UINT64_C(0xfffffffffffff000))
/** @} */


/** @name Hyper-V hypercall debug options.
 * @{ */
/** Maximum debug data payload size in bytes. */
#define GIM_HV_DEBUG_MAX_DATA_SIZE                4088

/** The undocumented bit for MSR_GIM_HV_DEBUG_OPTIONS_MSR that makes it all
 *  work. */
#define GIM_HV_DEBUG_OPTIONS_USE_HYPERCALLS       RT_BIT(2)

/** Guest will perform the HvPostDebugData hypercall until completion. */
#define GIM_HV_DEBUG_POST_LOOP                    RT_BIT_32(0)
/** Mask of valid HvPostDebugData options. */
#define GIM_HV_DEBUG_POST_OPTIONS_MASK            RT_BIT_32(0)

/** Guest will perform the HvRetrieveDebugData hypercall until completion. */
#define GIM_HV_DEBUG_RETREIVE_LOOP                RT_BIT_32(0)
/** Guest checks if any global debug session is active. */
#define GIM_HV_DEBUG_RETREIVE_TEST_ACTIVITY       RT_BIT_32(1)
/** Mask of valid HvRetrieveDebugData options. */
#define GIM_HV_DEBUG_RETREIVE_OPTIONS_MASK        RT_BIT_32(0) | RT_BIT_32(1)

/** Guest requests purging of incoming debug data. */
#define GIM_HV_DEBUG_PURGE_INCOMING_DATA          RT_BIT_32(0)
/** Guest requests purging of outgoing debug data. */
#define GIM_HV_DEBUG_PURGE_OUTGOING_DATA          RT_BIT_32(1)
/** @} */


/** @name VMBus.
 *  These are just arbitrary definitions made up by Microsoft without
 *  any publicly available specification behind it.
 * @{ */
/** VMBus connection ID. */
#define GIM_HV_VMBUS_MSG_CONNECTION_ID            1
/** VMBus synthetic interrupt source (see VMBUS_MESSAGE_SINT in linux
 *  sources). */
#define GIM_HV_VMBUS_MSG_SINT                     2
/** @} */

/** @name SynIC.
 *  Synthetic Interrupt Controller definitions.
 * @{ */
/** SynIC version register. */
#define GIM_HV_SVERSION                           1
/** Number of synthetic interrupt sources (warning, fixed in saved-states!). */
#define GIM_HV_SINT_COUNT                         16
/** Lowest valid vector for synthetic interrupt. */
#define GIM_HV_SINT_VECTOR_VALID_MIN              16
/** Highest valid vector for synthetic interrupt. */
#define GIM_HV_SINT_VECTOR_VALID_MAX              255
/** Number of synthetic timers. */
#define GIM_HV_STIMER_COUNT                       4
/** @} */

/** @name Hyper-V synthetic interrupt message type.
 * See 14.8.2 "SynIC Message Types"
 * @{
 */
typedef enum GIMHVMSGTYPE
{
    GIMHVMSGTYPE_NONE                 = 0,              /* Common messages */
    GIMHVMSGTYPE_VMBUS                = 1,              /* Guest messages */
    GIMHVMSGTYPE_UNMAPPEDGPA          = 0x80000000,     /* Hypervisor messages */
    GIMHVMSGTYPE_GPAINTERCEPT         = 0x80000001,
    GIMHVMSGTYPE_TIMEREXPIRED         = 0x80000010,
    GIMHVMSGTYPE_INVALIDVPREGVAL      = 0x80000020,
    GIMHVMSGTYPE_UNRECOVERABLEXCPT    = 0x80000021,
    GIMHVMSGTYPE_UNSUPPORTEDFEAT      = 0x80000022,
    GIMHVMSGTYPE_APICEOI              = 0x80000030,
    GIMHVMSGTYPE_X64LEGACYFPERROR     = 0x80000031,
    GIMHVMSGTYPE_EVENTLOGBUFSCOMPLETE = 0x80000040,
    GIMHVMSGTYPE_X64IOPORTINTERCEPT   = 0x80010000,
    GIMHVMSGTYPE_X64MSRINTERCEPT      = 0x80010001,
    GIMHVMSGTYPE_X64CPUIDINTERCEPT    = 0x80010002,
    GIMHVMSGTYPE_X64XCPTINTERCEPT     = 0x80010003
} GIMHVMSGTYPE;
AssertCompileSize(GIMHVMSGTYPE, 4);
/** @} */


/** @name Hyper-V synthetic interrupt message format.
 * @{ */
#define GIM_HV_MSG_SIZE                           256
#define GIM_HV_MSG_MAX_PAYLOAD_SIZE               240
#define GIM_HV_MSG_MAX_PAYLOAD_UNITS               30

/**
 * Synthetic interrupt message flags.
 */
typedef union GIMHVMSGFLAGS
{
    struct
    {
        uint8_t  u1Pending  : 1;
        uint8_t  u7Reserved : 7;
    } n;
    uint8_t u;
} GIMHVMSGFLAGS;
AssertCompileSize(GIMHVMSGFLAGS, sizeof(uint8_t));

/**
 * Synthetic interrupt message header.
 *
 * @remarks The layout of this structure differs from
 *          the Hyper-V spec. Aug 8, 2013 v4.0a. Layout
 *          in accordance w/ VMBus client expectations.
 */
typedef struct GIMHVMSGHDR
{
    GIMHVMSGTYPE    enmMessageType;
    uint8_t         cbPayload;
    GIMHVMSGFLAGS   MessageFlags;
    uint16_t        uRsvd;
    union
    {
        uint64_t    uOriginatorId;
        uint64_t    uPartitionId;
        uint64_t    uPortId;
    } msgid;
} GIMHVMSGHDR;
/** Pointer to a synthetic interrupt message header. */
typedef GIMHVMSGHDR *PGIMHVMSGHDR;
AssertCompileMemberOffset(GIMHVMSGHDR, cbPayload,    4);
AssertCompileMemberOffset(GIMHVMSGHDR, MessageFlags, 5);
AssertCompileMemberOffset(GIMHVMSGHDR, msgid,        8);
AssertCompileSize(GIMHVMSGHDR, GIM_HV_MSG_SIZE - GIM_HV_MSG_MAX_PAYLOAD_SIZE);

/**
 * Synthetic interrupt message.
 */
typedef struct GIMHVMSG
{
    GIMHVMSGHDR     MsgHdr;
    uint64_t        aPayload[GIM_HV_MSG_MAX_PAYLOAD_UNITS];
} GIMHVMSG;
/** Pointer to a synthetic interrupt message. */
typedef GIMHVMSG *PGIMHVMSG;
AssertCompileSize(GIMHVMSG, GIM_HV_MSG_SIZE);
/** @} */


/** @name Hyper-V hypercall parameters.
 * @{ */
/**
 * HvPostMessage hypercall input.
 */
typedef struct GIMHVPOSTMESSAGEIN
{
    uint32_t      uConnectionId;
    uint32_t      uPadding;
    GIMHVMSGTYPE  enmMessageType;
    uint32_t      cbPayload;
} GIMHVPOSTMESSAGEIN;
/** Pointer to a HvPostMessage input struct. */
typedef GIMHVPOSTMESSAGEIN *PGIMHVPOSTMESSAGEIN;
AssertCompileSize(GIMHVPOSTMESSAGEIN, 16);

/**
 * HvResetDebugData hypercall input.
 */
typedef struct GIMHVDEBUGRESETIN
{
    uint32_t fFlags;
    uint32_t uPadding;
} GIMHVDEBUGRESETIN;
/** Pointer to a HvResetDebugData input struct. */
typedef GIMHVDEBUGRESETIN *PGIMHVDEBUGRESETIN;
AssertCompileSize(GIMHVDEBUGRESETIN, 8);

/**
 * HvPostDebugData hypercall input.
 */
typedef struct GIMHVDEBUGPOSTIN
{
    uint32_t cbWrite;
    uint32_t fFlags;
} GIMHVDEBUGPOSTIN;
/** Pointer to a HvPostDebugData input struct. */
typedef GIMHVDEBUGPOSTIN *PGIMHVDEBUGPOSTIN;
AssertCompileSize(GIMHVDEBUGPOSTIN, 8);

/**
 * HvPostDebugData hypercall output.
 */
typedef struct GIMHVDEBUGPOSTOUT
{
    uint32_t cbPending;
    uint32_t uPadding;
} GIMHVDEBUGPOSTOUT;
/** Pointer to a HvPostDebugData output struct. */
typedef GIMHVDEBUGPOSTOUT *PGIMHVDEBUGPOSTOUT;
AssertCompileSize(GIMHVDEBUGPOSTOUT, 8);

/**
 * HvRetrieveDebugData hypercall input.
 */
typedef struct GIMHVDEBUGRETRIEVEIN
{
    uint32_t cbRead;
    uint32_t fFlags;
    uint64_t u64Timeout;
} GIMHVDEBUGRETRIEVEIN;
/** Pointer to a HvRetrieveDebugData input struct. */
typedef GIMHVDEBUGRETRIEVEIN *PGIMHVDEBUGRETRIEVEIN;
AssertCompileSize(GIMHVDEBUGRETRIEVEIN, 16);

/**
 * HvRetriveDebugData hypercall output.
 */
typedef struct GIMHVDEBUGRETRIEVEOUT
{
    uint32_t cbRead;
    uint32_t cbRemaining;
} GIMHVDEBUGRETRIEVEOUT;
/** Pointer to a HvRetrieveDebugData output struct. */
typedef GIMHVDEBUGRETRIEVEOUT *PGIMHVDEBUGRETRIEVEOUT;
AssertCompileSize(GIMHVDEBUGRETRIEVEOUT, 8);

/**
 * HvExtCallQueryCapabilities hypercall output.
 */
typedef struct GIMHVEXTQUERYCAP
{
    uint64_t fCapabilities;
} GIMHVEXTQUERYCAP;
/** Pointer to a HvExtCallQueryCapabilities output struct. */
typedef GIMHVEXTQUERYCAP *PGIMHVEXTQUERYCAP;
AssertCompileSize(GIMHVEXTQUERYCAP, 8);

/**
 * Hyper-V memory range for HvExtCallGetBootZeroedMemory.
 */
typedef struct GIMHVEXTMEMRANGE
{
    RTGCPHYS GCPhysStart;
    uint64_t cPages;
} GIMHVEXTMEMRANGE;

/** Maximum number of zeroed memory ranges supported by Hyper-V. */
#define GIM_HV_MAX_BOOT_ZEROED_MEM_RANGES         255

/**
 * HvExtCallGetBootZeroedMemory hypercall output.
 */
typedef struct GIMHVEXTGETBOOTZEROMEM
{
    uint64_t            cRanges;
    GIMHVEXTMEMRANGE    aRanges[GIM_HV_MAX_BOOT_ZEROED_MEM_RANGES];
} GIMHVEXTGETBOOTZEROMEM;
/** Pointer to a HvExtCallGetBootZeroedMemory output struct. */
typedef GIMHVEXTGETBOOTZEROMEM *PGIMHVEXTGETBOOTZEROMEM;
/** Pointer to a const HvExtCallGetBootZeroedMemory output struct. */
typedef GIMHVEXTGETBOOTZEROMEM const *PCGIMHVEXTGETBOOTZEROMEM;
AssertCompileSize(GIMHVEXTGETBOOTZEROMEM, 4088);
AssertCompile(sizeof(GIMHVEXTGETBOOTZEROMEM) <= GIM_HV_PAGE_SIZE);
/** @} */


/**
 * MMIO2 region indices.
 */
/** The hypercall page region. */
#define GIM_HV_HYPERCALL_PAGE_REGION_IDX          UINT8_C(0)
/** The TSC page region. */
#define GIM_HV_REF_TSC_PAGE_REGION_IDX            UINT8_C(1)
/** The maximum region index (must be <= UINT8_MAX). */
#define GIM_HV_REGION_IDX_MAX                     GIM_HV_REF_TSC_PAGE_REGION_IDX

/**
 * Hyper-V TSC (HV_REFERENCE_TSC_PAGE) structure placed in the TSC reference
 * page.
 */
typedef struct GIMHVREFTSC
{
    uint32_t u32TscSequence;
    uint32_t uReserved0;
    uint64_t u64TscScale;
    int64_t  i64TscOffset;
} GIMHVTSCPAGE;
/** Pointer to Hyper-V reference TSC. */
typedef GIMHVREFTSC *PGIMHVREFTSC;
/** Pointer to a const Hyper-V reference TSC. */
typedef GIMHVREFTSC const *PCGIMHVREFTSC;

/**
 * Type of the next reply to be sent to the debug connection of the guest.
 *
 * @remarks This is saved as part of saved-state, so don't re-order or
 *          alter the size!
 */
typedef enum GIMHVDEBUGREPLY
{
    /** Send UDP packet. */
    GIMHVDEBUGREPLY_UDP = 0,
    /** Send DHCP offer for DHCP discover. */
    GIMHVDEBUGREPLY_DHCP_OFFER,
    /** DHCP offer sent. */
    GIMHVDEBUGREPLY_DHCP_OFFER_SENT,
    /** Send DHCP acknowledgement for DHCP request. */
    GIMHVDEBUGREPLY_DHCP_ACK,
    /** DHCP acknowledgement sent.  */
    GIMHVDEBUGREPLY_DHCP_ACK_SENT,
    /** Sent ARP reply. */
    GIMHVDEBUGREPLY_ARP_REPLY,
    /** ARP reply sent. */
    GIMHVDEBUGREPLY_ARP_REPLY_SENT,
    /** Customary 32-bit type hack. */
    GIMHVDEBUGREPLY_32BIT_HACK = 0x7fffffff
} GIMHVDEBUGREPLY;
AssertCompileSize(GIMHVDEBUGREPLY, sizeof(uint32_t));

/**
 * GIM Hyper-V VM instance data.
 * Changes to this must checked against the padding of the gim union in VM!
 */
typedef struct GIMHV
{
    /** @name Primary MSRs.
     * @{ */
    /** Guest OS identity MSR. */
    uint64_t                    u64GuestOsIdMsr;
    /** Hypercall MSR. */
    uint64_t                    u64HypercallMsr;
    /** Reference TSC page MSR. */
    uint64_t                    u64TscPageMsr;
    /** @}  */

    /** @name CPUID features.
     * @{ */
    /** Basic features. */
    uint32_t                    uBaseFeat;
    /** Partition flags. */
    uint32_t                    uPartFlags;
    /** Power management. */
    uint32_t                    uPowMgmtFeat;
    /** Miscellaneous. */
    uint32_t                    uMiscFeat;
    /** Hypervisor hints to the guest. */
    uint32_t                    uHyperHints;
    /** Hypervisor capabilities. */
    uint32_t                    uHyperCaps;
    /** @} */

    /** @name Guest Crash MSRs.
     * @{
     */
    /** Guest crash control MSR. */
    uint64_t                    uCrashCtlMsr;
    /** Guest crash parameter 0 MSR. */
    uint64_t                    uCrashP0Msr;
    /** Guest crash parameter 1 MSR. */
    uint64_t                    uCrashP1Msr;
    /** Guest crash parameter 2 MSR. */
    uint64_t                    uCrashP2Msr;
    /** Guest crash parameter 3 MSR. */
    uint64_t                    uCrashP3Msr;
    /** Guest crash parameter 4 MSR. */
    uint64_t                    uCrashP4Msr;
    /** @} */

    /** @name Time management.
     * @{ */
    /** Per-VM R0 Spinlock for protecting EMT writes to the TSC page. */
    RTSPINLOCK                  hSpinlockR0;
    /** The TSC frequency (in HZ) reported to the guest. */
    uint64_t                    cTscTicksPerSecond;
    /** @} */

    /** @name Hypercalls.
     * @{ */
    /** Guest address of the hypercall input parameter page. */
    RTGCPHYS                    GCPhysHypercallIn;
    /** Guest address of the hypercall output parameter page. */
    RTGCPHYS                    GCPhysHypercallOut;
    /** Pointer to the hypercall input parameter page - R3. */
    R3PTRTYPE(uint8_t *)        pbHypercallIn;
    /** Pointer to the hypercall output parameter page - R3. */
    R3PTRTYPE(uint8_t *)        pbHypercallOut;
    /** @} */

    /** @name Guest debugging.
     * @{ */
    /** Whether we're posing as the Microsoft vendor. */
    bool                        fIsVendorMsHv;
    /** Whether we're posing as the Microsoft virtualization service. */
    bool                        fIsInterfaceVs;
    /** Whether debugging support is enabled. */
    bool                        fDbgEnabled;
    /** Whether we should suggest a hypercall-based debug interface to the guest. */
    bool                        fDbgHypercallInterface;
    bool                        afAlignment0[4];
    /** The action to take while sending replies. */
    GIMHVDEBUGREPLY             enmDbgReply;
    /** The IP address chosen by/assigned to the guest. */
    RTNETADDRIPV4               DbgGuestIp4Addr;
    /** Transaction ID for the BOOTP+DHCP sequence. */
    uint32_t                    uDbgBootpXId;
    /** The source UDP port used by the guest while sending debug packets. */
    uint16_t                    uUdpGuestSrcPort;
    /** The destination UDP port used by the guest while sending debug packets. */
    uint16_t                    uUdpGuestDstPort;
    /** Debug send buffer MSR. */
    uint64_t                    uDbgSendBufferMsr;
    /** Debug receive buffer MSR. */
    uint64_t                    uDbgRecvBufferMsr;
    /** Debug pending buffer MSR. */
    uint64_t                    uDbgPendingBufferMsr;
    /** Debug status MSR. */
    uint64_t                    uDbgStatusMsr;
    /** Intermediate debug I/O buffer (GIM_HV_PAGE_SIZE). */
    R3PTRTYPE(void *)           pvDbgBuffer;
    R3PTRTYPE(void *)           pvAlignment0;
    /** @} */

    /** Array of MMIO2 regions. */
    GIMMMIO2REGION              aMmio2Regions[GIM_HV_REGION_IDX_MAX + 1];
} GIMHV;
/** Pointer to per-VM GIM Hyper-V instance data. */
typedef GIMHV *PGIMHV;
/** Pointer to const per-VM GIM Hyper-V instance data. */
typedef GIMHV const *PCGIMHV;
AssertCompileMemberAlignment(GIMHV, aMmio2Regions, 8);
AssertCompileMemberAlignment(GIMHV, hSpinlockR0, sizeof(uintptr_t));

/**
 * Hyper-V per-VCPU synthetic timer.
 */
typedef struct GIMHVSTIMER
{
    /** Synthetic timer handle. */
    TMTIMERHANDLE               hTimer;
    /** Virtual CPU ID this timer belongs to (for reverse mapping). */
    VMCPUID                     idCpu;
    /** The index of this timer in the auStimers array (for reverse mapping). */
    uint32_t                    idxStimer;
    /** Synthetic timer config MSR. */
    uint64_t                    uStimerConfigMsr;
    /** Synthetic timer count MSR. */
    uint64_t                    uStimerCountMsr;
} GIMHVSTIMER;
/** Pointer to per-VCPU Hyper-V synthetic timer. */
typedef GIMHVSTIMER *PGIMHVSTIMER;
/** Pointer to a const per-VCPU Hyper-V synthetic timer. */
typedef GIMHVSTIMER const *PCGIMHVSTIMER;
AssertCompileSizeAlignment(GIMHVSTIMER, 8);

/**
 * Hyper-V VCPU instance data.
 * Changes to this must checked against the padding of the gim union in VMCPU!
 */
typedef struct GIMHVCPU
{
    /** @name Synthetic interrupt MSRs.
     * @{ */
    /** Synthetic interrupt message page MSR. */
    uint64_t                    uSimpMsr;
    /** Interrupt source MSRs. */
    uint64_t                    auSintMsrs[GIM_HV_SINT_COUNT];
    /** Synethtic interrupt events flag page MSR. */
    uint64_t                    uSiefpMsr;
    /** APIC-assist page MSR. */
    uint64_t                    uApicAssistPageMsr;
    /** Synthetic interrupt control MSR. */
    uint64_t                    uSControlMsr;
    /** Synthetic timers. */
    GIMHVSTIMER                 aStimers[GIM_HV_STIMER_COUNT];
    /** @} */

    /** @name Statistics.
     * @{ */
    STAMCOUNTER                 aStatStimerFired[GIM_HV_STIMER_COUNT];
    /** @} */
} GIMHVCPU;
/** Pointer to per-VCPU GIM Hyper-V instance data. */
typedef GIMHVCPU *PGIMHVCPU;
/** Pointer to const per-VCPU GIM Hyper-V instance data. */
typedef GIMHVCPU const *PCGIMHVCPU;


RT_C_DECLS_BEGIN

#ifdef IN_RING0
VMMR0_INT_DECL(int)             gimR0HvInitVM(PVMCC pVM);
VMMR0_INT_DECL(int)             gimR0HvTermVM(PVMCC pVM);
VMMR0_INT_DECL(int)             gimR0HvUpdateParavirtTsc(PVMCC pVM, uint64_t u64Offset);
#endif /* IN_RING0 */

#ifdef IN_RING3
VMMR3_INT_DECL(int)             gimR3HvInit(PVM pVM, PCFGMNODE pGimCfg);
VMMR3_INT_DECL(int)             gimR3HvInitCompleted(PVM pVM);
VMMR3_INT_DECL(int)             gimR3HvTerm(PVM pVM);
VMMR3_INT_DECL(void)            gimR3HvRelocate(PVM pVM, RTGCINTPTR offDelta);
VMMR3_INT_DECL(void)            gimR3HvReset(PVM pVM);
VMMR3_INT_DECL(int)             gimR3HvSave(PVM pVM, PSSMHANDLE pSSM);
VMMR3_INT_DECL(int)             gimR3HvLoad(PVM pVM, PSSMHANDLE pSSM);
VMMR3_INT_DECL(int)             gimR3HvLoadDone(PVM pVM, PSSMHANDLE pSSM);
VMMR3_INT_DECL(int)             gimR3HvGetDebugSetup(PVM pVM, PGIMDEBUGSETUP pDbgSetup);

VMMR3_INT_DECL(int)             gimR3HvDisableSiefPage(PVMCPU pVCpu);
VMMR3_INT_DECL(int)             gimR3HvEnableSiefPage(PVMCPU pVCpu, RTGCPHYS GCPhysSiefPage);
VMMR3_INT_DECL(int)             gimR3HvEnableSimPage(PVMCPU pVCpu, RTGCPHYS GCPhysSimPage);
VMMR3_INT_DECL(int)             gimR3HvDisableSimPage(PVMCPU pVCpu);
VMMR3_INT_DECL(int)             gimR3HvDisableApicAssistPage(PVMCPU pVCpu);
VMMR3_INT_DECL(int)             gimR3HvEnableApicAssistPage(PVMCPU pVCpu, RTGCPHYS GCPhysTscPage);
VMMR3_INT_DECL(int)             gimR3HvDisableTscPage(PVM pVM);
VMMR3_INT_DECL(int)             gimR3HvEnableTscPage(PVM pVM, RTGCPHYS GCPhysTscPage, bool fUseThisTscSeq, uint32_t uTscSeq);
VMMR3_INT_DECL(int)             gimR3HvDisableHypercallPage(PVM pVM);
VMMR3_INT_DECL(int)             gimR3HvEnableHypercallPage(PVM pVM, RTGCPHYS GCPhysHypercallPage);

VMMR3_INT_DECL(int)             gimR3HvHypercallPostDebugData(PVM pVM, int *prcHv);
VMMR3_INT_DECL(int)             gimR3HvHypercallRetrieveDebugData(PVM pVM, int *prcHv);
VMMR3_INT_DECL(int)             gimR3HvDebugWrite(PVM pVM, void *pvData, uint32_t cbWrite, uint32_t *pcbWritten, bool fUdpPkt);
VMMR3_INT_DECL(int)             gimR3HvDebugRead(PVM pVM, void *pvBuf, uint32_t cbBuf, uint32_t cbRead, uint32_t *pcbRead,
                                                 uint32_t cMsTimeout, bool fUdpPkt);
VMMR3_INT_DECL(int)             gimR3HvHypercallExtQueryCap(PVM pVM, int *prcHv);
VMMR3_INT_DECL(int)             gimR3HvHypercallExtGetBootZeroedMem(PVM pVM, int *prcHv);

#endif /* IN_RING3 */

VMM_INT_DECL(PGIMMMIO2REGION)   gimHvGetMmio2Regions(PVM pVM, uint32_t *pcRegions);
VMM_INT_DECL(bool)              gimHvIsParavirtTscEnabled(PVM pVM);
VMM_INT_DECL(bool)              gimHvAreHypercallsEnabled(PCVM pVM);
VMM_INT_DECL(bool)              gimHvShouldTrapXcptUD(PVMCPU pVCpu);
VMM_INT_DECL(VBOXSTRICTRC)      gimHvXcptUD(PVMCPUCC pVCpu, PCPUMCTX pCtx, PDISSTATE pDis, uint8_t *pcbInstr);
VMM_INT_DECL(VBOXSTRICTRC)      gimHvHypercall(PVMCPUCC pVCpu, PCPUMCTX pCtx);
VMM_INT_DECL(VBOXSTRICTRC)      gimHvHypercallEx(PVMCPUCC pVCpu, PCPUMCTX pCtx, unsigned uDisOpcode, uint8_t cbInstr);
#if !defined(VBOX_VMM_TARGET_ARMV8)
VMM_INT_DECL(VBOXSTRICTRC)      gimHvReadMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t *puValue);
VMM_INT_DECL(VBOXSTRICTRC)      gimHvWriteMsr(PVMCPUCC pVCpu, uint32_t idMsr, PCCPUMMSRRANGE pRange, uint64_t uRawValue);
#endif

VMM_INT_DECL(void)              gimHvStartStimer(PVMCPUCC pVCpu, PCGIMHVSTIMER pHvStimer);

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_GIMHvInternal_h */

