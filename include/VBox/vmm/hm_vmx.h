/** @file
 * HM - VMX Structures and Definitions. (VMM)
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

#ifndef VBOX_INCLUDED_vmm_hm_vmx_h
#define VBOX_INCLUDED_vmm_hm_vmx_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <iprt/x86.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_hm_vmx    VMX Types and Definitions
 * @ingroup grp_hm
 * @{
 */

/** @name Host-state MSR lazy-restoration flags.
 * @{
 */
/** The host MSRs have been saved. */
#define VMX_LAZY_MSRS_SAVED_HOST                                RT_BIT(0)
/** The guest MSRs are loaded and in effect. */
#define VMX_LAZY_MSRS_LOADED_GUEST                              RT_BIT(1)
/** @} */

/** @name VMX HM-error codes for VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO.
 *  UFC = Unsupported Feature Combination.
 * @{
 */
/** Unsupported pin-based VM-execution controls combo. */
#define VMX_UFC_CTRL_PIN_EXEC                                   1
/** Unsupported processor-based VM-execution controls combo. */
#define VMX_UFC_CTRL_PROC_EXEC                                  2
/** Unsupported move debug register VM-exit combo. */
#define VMX_UFC_CTRL_PROC_MOV_DRX_EXIT                          3
/** Unsupported VM-entry controls combo. */
#define VMX_UFC_CTRL_ENTRY                                      4
/** Unsupported VM-exit controls combo. */
#define VMX_UFC_CTRL_EXIT                                       5
/** MSR storage capacity of the VMCS autoload/store area is not sufficient
 *  for storing host MSRs. */
#define VMX_UFC_INSUFFICIENT_HOST_MSR_STORAGE                   6
/** MSR storage capacity of the VMCS autoload/store area is not sufficient
 *  for storing guest MSRs. */
#define VMX_UFC_INSUFFICIENT_GUEST_MSR_STORAGE                  7
/** Invalid VMCS size. */
#define VMX_UFC_INVALID_VMCS_SIZE                               8
/** Unsupported secondary processor-based VM-execution controls combo. */
#define VMX_UFC_CTRL_PROC_EXEC2                                 9
/** Invalid unrestricted-guest execution controls combo. */
#define VMX_UFC_INVALID_UX_COMBO                                10
/** EPT flush type not supported. */
#define VMX_UFC_EPT_FLUSH_TYPE_UNSUPPORTED                      11
/** EPT paging structure memory type is not write-back. */
#define VMX_UFC_EPT_MEM_TYPE_NOT_WB                             12
/** EPT requires INVEPT instr. support but it's not available. */
#define VMX_UFC_EPT_INVEPT_UNAVAILABLE                          13
/** EPT requires page-walk length of 4. */
#define VMX_UFC_EPT_PAGE_WALK_LENGTH_UNSUPPORTED                14
/** VMX VMWRITE all feature exposed to the guest but not supported on host. */
#define VMX_UFC_GST_HOST_VMWRITE_ALL                            15
/** LBR stack size cannot be determined for the current CPU. */
#define VMX_UFC_LBR_STACK_SIZE_UNKNOWN                          16
/** LBR stack size of the CPU exceeds our buffer size. */
#define VMX_UFC_LBR_STACK_SIZE_OVERFLOW                         17
/** @} */

/** @name VMX HM-error codes for VERR_VMX_VMCS_FIELD_CACHE_INVALID.
 *  VCI = VMCS-field Cache Invalid.
 * @{
 */
/** Cache of VM-entry controls invalid. */
#define VMX_VCI_CTRL_ENTRY                                      300
/** Cache of VM-exit controls invalid. */
#define VMX_VCI_CTRL_EXIT                                       301
/** Cache of pin-based VM-execution controls invalid. */
#define VMX_VCI_CTRL_PIN_EXEC                                   302
/** Cache of processor-based VM-execution controls invalid. */
#define VMX_VCI_CTRL_PROC_EXEC                                  303
/** Cache of secondary processor-based VM-execution controls invalid. */
#define VMX_VCI_CTRL_PROC_EXEC2                                 304
/** Cache of exception bitmap invalid. */
#define VMX_VCI_CTRL_XCPT_BITMAP                                305
/** Cache of TSC offset invalid. */
#define VMX_VCI_CTRL_TSC_OFFSET                                 306
/** Cache of tertiary processor-based VM-execution controls invalid. */
#define VMX_VCI_CTRL_PROC_EXEC3                                 307
/** VirtualBox VMCS magic field mismatch. */
#define VMX_VCI_VMCS_MAGIC_MISMATCH                             308
/** @} */

/** @name VMX HM-error codes for VERR_VMX_INVALID_GUEST_STATE.
 *  IGS = Invalid Guest State.
 * @{
 */
/** An error occurred while checking invalid-guest-state. */
#define VMX_IGS_ERROR                                           500
/** The invalid guest-state checks did not find any reason why. */
#define VMX_IGS_REASON_NOT_FOUND                                501
/** CR0 fixed1 bits invalid. */
#define VMX_IGS_CR0_FIXED1                                      502
/** CR0 fixed0 bits invalid. */
#define VMX_IGS_CR0_FIXED0                                      503
/** CR0.PE and CR0.PE invalid VT-x/host combination. */
#define VMX_IGS_CR0_PG_PE_COMBO                                 504
/** CR4 fixed1 bits invalid. */
#define VMX_IGS_CR4_FIXED1                                      505
/** CR4 fixed0 bits invalid. */
#define VMX_IGS_CR4_FIXED0                                      506
/** Reserved bits in VMCS' DEBUGCTL MSR field not set to 0 when
 *  VMX_VMCS_CTRL_ENTRY_LOAD_DEBUG is used. */
#define VMX_IGS_DEBUGCTL_MSR_RESERVED                           507
/** CR0.PG not set for long-mode when not using unrestricted guest. */
#define VMX_IGS_CR0_PG_LONGMODE                                 508
/** CR4.PAE not set for long-mode guest when not using unrestricted guest. */
#define VMX_IGS_CR4_PAE_LONGMODE                                509
/** CR4.PCIDE set for 32-bit guest. */
#define VMX_IGS_CR4_PCIDE                                       510
/** VMCS' DR7 reserved bits not set to 0. */
#define VMX_IGS_DR7_RESERVED                                    511
/** VMCS' PERF_GLOBAL MSR reserved bits not set to 0. */
#define VMX_IGS_PERF_GLOBAL_MSR_RESERVED                        512
/** VMCS' EFER MSR reserved bits not set to 0. */
#define VMX_IGS_EFER_MSR_RESERVED                               513
/** VMCS' EFER MSR.LMA does not match the IA32e mode guest control. */
#define VMX_IGS_EFER_LMA_GUEST_MODE_MISMATCH                    514
/** VMCS' EFER MSR.LMA does not match EFER.LME of the guest when using paging
 *  without unrestricted guest. */
#define VMX_IGS_EFER_LMA_LME_MISMATCH                           515
/** CS.Attr.P bit invalid. */
#define VMX_IGS_CS_ATTR_P_INVALID                               516
/** CS.Attr reserved bits not set to 0. */
#define VMX_IGS_CS_ATTR_RESERVED                                517
/** CS.Attr.G bit invalid. */
#define VMX_IGS_CS_ATTR_G_INVALID                               518
/** CS is unusable. */
#define VMX_IGS_CS_ATTR_UNUSABLE                                519
/** CS and SS DPL unequal. */
#define VMX_IGS_CS_SS_ATTR_DPL_UNEQUAL                          520
/** CS and SS DPL mismatch. */
#define VMX_IGS_CS_SS_ATTR_DPL_MISMATCH                         521
/** CS Attr.Type invalid. */
#define VMX_IGS_CS_ATTR_TYPE_INVALID                            522
/** CS and SS RPL unequal. */
#define VMX_IGS_SS_CS_RPL_UNEQUAL                               523
/** SS.Attr.DPL and SS RPL unequal. */
#define VMX_IGS_SS_ATTR_DPL_RPL_UNEQUAL                         524
/** SS.Attr.DPL invalid for segment type. */
#define VMX_IGS_SS_ATTR_DPL_INVALID                             525
/** SS.Attr.Type invalid. */
#define VMX_IGS_SS_ATTR_TYPE_INVALID                            526
/** SS.Attr.P bit invalid. */
#define VMX_IGS_SS_ATTR_P_INVALID                               527
/** SS.Attr reserved bits not set to 0. */
#define VMX_IGS_SS_ATTR_RESERVED                                528
/** SS.Attr.G bit invalid. */
#define VMX_IGS_SS_ATTR_G_INVALID                               529
/** DS.Attr.A bit invalid. */
#define VMX_IGS_DS_ATTR_A_INVALID                               530
/** DS.Attr.P bit invalid. */
#define VMX_IGS_DS_ATTR_P_INVALID                               531
/** DS.Attr.DPL and DS RPL unequal. */
#define VMX_IGS_DS_ATTR_DPL_RPL_UNEQUAL                         532
/** DS.Attr reserved bits not set to 0. */
#define VMX_IGS_DS_ATTR_RESERVED                                533
/** DS.Attr.G bit invalid. */
#define VMX_IGS_DS_ATTR_G_INVALID                               534
/** DS.Attr.Type invalid. */
#define VMX_IGS_DS_ATTR_TYPE_INVALID                            535
/** ES.Attr.A bit invalid. */
#define VMX_IGS_ES_ATTR_A_INVALID                               536
/** ES.Attr.P bit invalid. */
#define VMX_IGS_ES_ATTR_P_INVALID                               537
/** ES.Attr.DPL and DS RPL unequal. */
#define VMX_IGS_ES_ATTR_DPL_RPL_UNEQUAL                         538
/** ES.Attr reserved bits not set to 0. */
#define VMX_IGS_ES_ATTR_RESERVED                                539
/** ES.Attr.G bit invalid. */
#define VMX_IGS_ES_ATTR_G_INVALID                               540
/** ES.Attr.Type invalid. */
#define VMX_IGS_ES_ATTR_TYPE_INVALID                            541
/** FS.Attr.A bit invalid. */
#define VMX_IGS_FS_ATTR_A_INVALID                               542
/** FS.Attr.P bit invalid. */
#define VMX_IGS_FS_ATTR_P_INVALID                               543
/** FS.Attr.DPL and DS RPL unequal. */
#define VMX_IGS_FS_ATTR_DPL_RPL_UNEQUAL                         544
/** FS.Attr reserved bits not set to 0. */
#define VMX_IGS_FS_ATTR_RESERVED                                545
/** FS.Attr.G bit invalid. */
#define VMX_IGS_FS_ATTR_G_INVALID                               546
/** FS.Attr.Type invalid. */
#define VMX_IGS_FS_ATTR_TYPE_INVALID                            547
/** GS.Attr.A bit invalid. */
#define VMX_IGS_GS_ATTR_A_INVALID                               548
/** GS.Attr.P bit invalid. */
#define VMX_IGS_GS_ATTR_P_INVALID                               549
/** GS.Attr.DPL and DS RPL unequal. */
#define VMX_IGS_GS_ATTR_DPL_RPL_UNEQUAL                         550
/** GS.Attr reserved bits not set to 0. */
#define VMX_IGS_GS_ATTR_RESERVED                                551
/** GS.Attr.G bit invalid. */
#define VMX_IGS_GS_ATTR_G_INVALID                               552
/** GS.Attr.Type invalid. */
#define VMX_IGS_GS_ATTR_TYPE_INVALID                            553
/** V86 mode CS.Base invalid. */
#define VMX_IGS_V86_CS_BASE_INVALID                             554
/** V86 mode CS.Limit invalid. */
#define VMX_IGS_V86_CS_LIMIT_INVALID                            555
/** V86 mode CS.Attr invalid. */
#define VMX_IGS_V86_CS_ATTR_INVALID                             556
/** V86 mode SS.Base invalid. */
#define VMX_IGS_V86_SS_BASE_INVALID                             557
/** V86 mode SS.Limit invalid. */
#define VMX_IGS_V86_SS_LIMIT_INVALID                            558
/** V86 mode SS.Attr invalid. */
#define VMX_IGS_V86_SS_ATTR_INVALID                             559
/** V86 mode DS.Base invalid. */
#define VMX_IGS_V86_DS_BASE_INVALID                             560
/** V86 mode DS.Limit invalid. */
#define VMX_IGS_V86_DS_LIMIT_INVALID                            561
/** V86 mode DS.Attr invalid. */
#define VMX_IGS_V86_DS_ATTR_INVALID                             562
/** V86 mode ES.Base invalid. */
#define VMX_IGS_V86_ES_BASE_INVALID                             563
/** V86 mode ES.Limit invalid. */
#define VMX_IGS_V86_ES_LIMIT_INVALID                            564
/** V86 mode ES.Attr invalid. */
#define VMX_IGS_V86_ES_ATTR_INVALID                             565
/** V86 mode FS.Base invalid. */
#define VMX_IGS_V86_FS_BASE_INVALID                             566
/** V86 mode FS.Limit invalid. */
#define VMX_IGS_V86_FS_LIMIT_INVALID                            567
/** V86 mode FS.Attr invalid. */
#define VMX_IGS_V86_FS_ATTR_INVALID                             568
/** V86 mode GS.Base invalid. */
#define VMX_IGS_V86_GS_BASE_INVALID                             569
/** V86 mode GS.Limit invalid. */
#define VMX_IGS_V86_GS_LIMIT_INVALID                            570
/** V86 mode GS.Attr invalid. */
#define VMX_IGS_V86_GS_ATTR_INVALID                             571
/** Longmode CS.Base invalid. */
#define VMX_IGS_LONGMODE_CS_BASE_INVALID                        572
/** Longmode SS.Base invalid. */
#define VMX_IGS_LONGMODE_SS_BASE_INVALID                        573
/** Longmode DS.Base invalid. */
#define VMX_IGS_LONGMODE_DS_BASE_INVALID                        574
/** Longmode ES.Base invalid. */
#define VMX_IGS_LONGMODE_ES_BASE_INVALID                        575
/** SYSENTER ESP is not canonical. */
#define VMX_IGS_SYSENTER_ESP_NOT_CANONICAL                      576
/** SYSENTER EIP is not canonical. */
#define VMX_IGS_SYSENTER_EIP_NOT_CANONICAL                      577
/** PAT MSR invalid. */
#define VMX_IGS_PAT_MSR_INVALID                                 578
/** PAT MSR reserved bits not set to 0. */
#define VMX_IGS_PAT_MSR_RESERVED                                579
/** GDTR.Base is not canonical. */
#define VMX_IGS_GDTR_BASE_NOT_CANONICAL                         580
/** IDTR.Base is not canonical. */
#define VMX_IGS_IDTR_BASE_NOT_CANONICAL                         581
/** GDTR.Limit invalid. */
#define VMX_IGS_GDTR_LIMIT_INVALID                              582
/** IDTR.Limit invalid. */
#define VMX_IGS_IDTR_LIMIT_INVALID                              583
/** Longmode RIP is invalid. */
#define VMX_IGS_LONGMODE_RIP_INVALID                            584
/** RFLAGS reserved bits not set to 0. */
#define VMX_IGS_RFLAGS_RESERVED                                 585
/** RFLAGS RA1 reserved bits not set to 1. */
#define VMX_IGS_RFLAGS_RESERVED1                                586
/** RFLAGS.VM (V86 mode) invalid. */
#define VMX_IGS_RFLAGS_VM_INVALID                               587
/** RFLAGS.IF invalid. */
#define VMX_IGS_RFLAGS_IF_INVALID                               588
/** Activity state invalid. */
#define VMX_IGS_ACTIVITY_STATE_INVALID                          589
/** Activity state HLT invalid when SS.Attr.DPL is not zero. */
#define VMX_IGS_ACTIVITY_STATE_HLT_INVALID                      590
/** Activity state ACTIVE invalid when block-by-STI or MOV SS. */
#define VMX_IGS_ACTIVITY_STATE_ACTIVE_INVALID                   591
/** Activity state SIPI WAIT invalid. */
#define VMX_IGS_ACTIVITY_STATE_SIPI_WAIT_INVALID                592
/** Interruptibility state reserved bits not set to 0. */
#define VMX_IGS_INTERRUPTIBILITY_STATE_RESERVED                 593
/** Interruptibility state cannot be block-by-STI -and- MOV SS. */
#define VMX_IGS_INTERRUPTIBILITY_STATE_STI_MOVSS_INVALID        594
/** Interruptibility state block-by-STI invalid for EFLAGS. */
#define VMX_IGS_INTERRUPTIBILITY_STATE_STI_EFL_INVALID          595
/** Interruptibility state invalid while trying to deliver external
 *  interrupt. */
#define VMX_IGS_INTERRUPTIBILITY_STATE_EXT_INT_INVALID          596
/** Interruptibility state block-by-MOVSS invalid while trying to deliver an
 *  NMI. */
#define VMX_IGS_INTERRUPTIBILITY_STATE_MOVSS_INVALID            597
/** Interruptibility state block-by-SMI invalid when CPU is not in SMM. */
#define VMX_IGS_INTERRUPTIBILITY_STATE_SMI_INVALID              598
/** Interruptibility state block-by-SMI invalid when trying to enter SMM. */
#define VMX_IGS_INTERRUPTIBILITY_STATE_SMI_SMM_INVALID          599
/** Interruptibility state block-by-STI (maybe) invalid when trying to
 *  deliver an NMI. */
#define VMX_IGS_INTERRUPTIBILITY_STATE_STI_INVALID              600
/** Interruptibility state block-by-NMI invalid when virtual-NMIs control is
 *  active. */
#define VMX_IGS_INTERRUPTIBILITY_STATE_NMI_INVALID              601
/** Pending debug exceptions reserved bits not set to 0. */
#define VMX_IGS_PENDING_DEBUG_RESERVED                          602
/** Longmode pending debug exceptions reserved bits not set to 0. */
#define VMX_IGS_LONGMODE_PENDING_DEBUG_RESERVED                 603
/** Pending debug exceptions.BS bit is not set when it should be. */
#define VMX_IGS_PENDING_DEBUG_XCPT_BS_NOT_SET                   604
/** Pending debug exceptions.BS bit is not clear when it should be. */
#define VMX_IGS_PENDING_DEBUG_XCPT_BS_NOT_CLEAR                 605
/** VMCS link pointer reserved bits not set to 0. */
#define VMX_IGS_VMCS_LINK_PTR_RESERVED                          606
/** TR cannot index into LDT, TI bit MBZ. */
#define VMX_IGS_TR_TI_INVALID                                   607
/** LDTR cannot index into LDT. TI bit MBZ. */
#define VMX_IGS_LDTR_TI_INVALID                                 608
/** TR.Base is not canonical. */
#define VMX_IGS_TR_BASE_NOT_CANONICAL                           609
/** FS.Base is not canonical. */
#define VMX_IGS_FS_BASE_NOT_CANONICAL                           610
/** GS.Base is not canonical. */
#define VMX_IGS_GS_BASE_NOT_CANONICAL                           611
/** LDTR.Base is not canonical. */
#define VMX_IGS_LDTR_BASE_NOT_CANONICAL                         612
/** TR is unusable. */
#define VMX_IGS_TR_ATTR_UNUSABLE                                613
/** TR.Attr.S bit invalid. */
#define VMX_IGS_TR_ATTR_S_INVALID                               614
/** TR is not present. */
#define VMX_IGS_TR_ATTR_P_INVALID                               615
/** TR.Attr reserved bits not set to 0. */
#define VMX_IGS_TR_ATTR_RESERVED                                616
/** TR.Attr.G bit invalid. */
#define VMX_IGS_TR_ATTR_G_INVALID                               617
/** Longmode TR.Attr.Type invalid. */
#define VMX_IGS_LONGMODE_TR_ATTR_TYPE_INVALID                   618
/** TR.Attr.Type invalid. */
#define VMX_IGS_TR_ATTR_TYPE_INVALID                            619
/** CS.Attr.S invalid. */
#define VMX_IGS_CS_ATTR_S_INVALID                               620
/** CS.Attr.DPL invalid. */
#define VMX_IGS_CS_ATTR_DPL_INVALID                             621
/** PAE PDPTE reserved bits not set to 0. */
#define VMX_IGS_PAE_PDPTE_RESERVED                              623
/** VMCS link pointer does not point to a shadow VMCS. */
#define VMX_IGS_VMCS_LINK_PTR_NOT_SHADOW                        624
/** VMCS link pointer to a shadow VMCS with invalid VMCS revision identifer. */
#define VMX_IGS_VMCS_LINK_PTR_SHADOW_VMCS_ID_INVALID            625
/** @} */

/** @name VMX VMCS-Read cache indices.
 * @{
 */
#define VMX_VMCS_GUEST_ES_BASE_CACHE_IDX                        0
#define VMX_VMCS_GUEST_CS_BASE_CACHE_IDX                        1
#define VMX_VMCS_GUEST_SS_BASE_CACHE_IDX                        2
#define VMX_VMCS_GUEST_DS_BASE_CACHE_IDX                        3
#define VMX_VMCS_GUEST_FS_BASE_CACHE_IDX                        4
#define VMX_VMCS_GUEST_GS_BASE_CACHE_IDX                        5
#define VMX_VMCS_GUEST_LDTR_BASE_CACHE_IDX                      6
#define VMX_VMCS_GUEST_TR_BASE_CACHE_IDX                        7
#define VMX_VMCS_GUEST_GDTR_BASE_CACHE_IDX                      8
#define VMX_VMCS_GUEST_IDTR_BASE_CACHE_IDX                      9
#define VMX_VMCS_GUEST_RSP_CACHE_IDX                            10
#define VMX_VMCS_GUEST_RIP_CACHE_IDX                            11
#define VMX_VMCS_GUEST_SYSENTER_ESP_CACHE_IDX                   12
#define VMX_VMCS_GUEST_SYSENTER_EIP_CACHE_IDX                   13
#define VMX_VMCS_RO_EXIT_QUALIFICATION_CACHE_IDX                14
#define VMX_VMCS_RO_GUEST_LINEAR_ADDR_CACHE_IDX                 15
#define VMX_VMCS_MAX_CACHE_IDX                                  (VMX_VMCS_RO_GUEST_LINEAR_ADDR_CACHE_IDX + 1)
#define VMX_VMCS_GUEST_CR3_CACHE_IDX                            16
#define VMX_VMCS_MAX_NESTED_PAGING_CACHE_IDX                    (VMX_VMCS_GUEST_CR3_CACHE_IDX + 1)
/** @} */


/** @name VMX EPT memory type encodings.
 * @{ */
#define VMX_EPT_MT_UC                                           UINT64_C(0)
#define VMX_EPT_MT_WC                                           UINT64_C(1)
#define VMX_EPT_MT_RSVD_2                                       UINT64_C(2)
#define VMX_EPT_MT_RSVD_3                                       UINT64_C(3)
#define VMX_EPT_MT_WT                                           UINT64_C(4)
#define VMX_EPT_MT_WP                                           UINT64_C(5)
#define VMX_EPT_MT_WB                                           UINT64_C(6)
#define VMX_EPT_MT_RSVD_7                                       UINT64_C(7)
/** @} */


/** @name VMX Extended Page Tables (EPT) Common Bits.
 * @{ */
/** Bit 0 - Readable (we often think of it as present). */
#define EPT_E_BIT_READ                      0
#define EPT_E_READ                          RT_BIT_64(EPT_E_BIT_READ)               /**< @see EPT_E_BIT_READ */
/** Bit 1 - Writable. */
#define EPT_E_BIT_WRITE                     1
#define EPT_E_WRITE                         RT_BIT_64(EPT_E_BIT_WRITE)              /**< @see EPT_E_BIT_WRITE */
/** Bit 2 - Executable.
 * @note This controls supervisor instruction fetching if mode-based
 *       execution control is enabled. */
#define EPT_E_BIT_EXECUTE                   2
#define EPT_E_EXECUTE                       RT_BIT_64(EPT_E_BIT_EXECUTE)            /**< @see EPT_E_BIT_EXECUTE */
/** Bits 3-5 - Memory type mask (leaf only, MBZ).
 * The memory type is only applicable for leaf entries and MBZ for
 * non-leaf (causes miconfiguration exit). */
#define EPT_E_MEMTYPE_MASK                  UINT64_C(0x0038)
/** Bits 3-5 - Memory type shifted mask. */
#define EPT_E_MEMTYPE_SMASK                 UINT64_C(0x0007)
/** Bits 3-5 - Memory type shift count. */
#define EPT_E_MEMTYPE_SHIFT                 3
/** Bits 3-5 - Memory type: UC (Uncacheable). */
#define EPT_E_MEMTYPE_UC                    (VMX_EPT_MT_UC << EPT_E_MEMTYPE_SHIFT)
/** Bits 3-5 - Memory type: WC (Write Combining). */
#define EPT_E_MEMTYPE_WC                    (VMX_EPT_MT_WC << EPT_E_MEMTYPE_SHIFT)
/** Bits 3-5 - Memory type: Invalid (2). */
#define EPT_E_MEMTYPE_INVALID_2             (VMX_EPT_MT_RSVD_2 << EPT_E_MEMTYPE_SHIFT)
/** Bits 3-5 - Memory type: Invalid (3). */
#define EPT_E_MEMTYPE_INVALID_3             (VMX_EPT_MT_RSVD_3 << EPT_E_MEMTYPE_SHIFT)
/** Bits 3-5 - Memory type: WT (Write Through). */
#define EPT_E_MEMTYPE_WT                    (VMX_EPT_MT_WT << EPT_E_MEMTYPE_SHIFT)
/** Bits 3-5 - Memory type: WP (Write Protected). */
#define EPT_E_MEMTYPE_WP                    (VMX_EPT_MT_WP << EPT_E_MEMTYPE_SHIFT)
/** Bits 3-5 - Memory type: WB (Write Back). */
#define EPT_E_MEMTYPE_WB                    (VMX_EPT_MT_WB << EPT_E_MEMTYPE_SHIFT)
/** Bits 3-5 - Memory type: Invalid (7). */
#define EPT_E_MEMTYPE_INVALID_7             (VMX_EPT_MT_RSVD_7 << EPT_E_MEMTYPE_SHIFT)
/** Bit 6 - Ignore page attribute table (leaf, MBZ). */
#define EPT_E_BIT_IGNORE_PAT                6
#define EPT_E_IGNORE_PAT                    RT_BIT_64(EPT_E_BIT_IGNORE_PAT)         /**< @see EPT_E_BIT_IGNORE_PAT */
/** Bit 7 - Leaf entry (MBZ in PML4, ignored in PT). */
#define EPT_E_BIT_LEAF                      7
#define EPT_E_LEAF                          RT_BIT_64(EPT_E_BIT_LEAF)               /**< @see EPT_E_BIT_LEAF */
/** Bit 8 - Accessed (all levels).
 * @note Ignored and not written when EPTP bit 6 is 0. */
#define EPT_E_BIT_ACCESSED                  8
#define EPT_E_ACCESSED                      RT_BIT_64(EPT_E_BIT_ACCESSED)           /**< @see EPT_E_BIT_ACCESSED */
/** Bit 9 - Dirty (leaf only).
 * @note Ignored and not written when EPTP bit 6 is 0. */
#define EPT_E_BIT_DIRTY                     9
#define EPT_E_DIRTY                         RT_BIT_64(EPT_E_BIT_DIRTY)              /**< @see EPT_E_BIT_DIRTY */
/** Bit 10 - Executable for usermode.
 * @note This ignored if mode-based execution control is disabled. */
#define EPT_E_BIT_USER_EXECUTE              10
#define EPT_E_USER_EXECUTE                  RT_BIT_64(EPT_E_BIT_USER_EXECUTE)       /**< @see EPT_E_BIT_USER_EXECUTE */
/* Bit 11 is always ignored. */
/** Bits 12-51 - Physical Page number of the next level. */
#define EPT_E_PG_MASK                       UINT64_C(0x000ffffffffff000)
/** Bit 58 - Page-write access (leaf only, ignored).
 * @note Ignored if EPT page-write control is disabled. */
#define EPT_E_BIT_PAGING_WRITE              58
#define EPT_E_PAGING_WRITE                  RT_BIT_64(EPT_E_BIT_PAGING_WRITE)       /**< @see EPT_E_BIT_PAGING_WRITE*/
/* Bit 59 is always ignored. */
/** Bit 60 - Supervisor shadow stack (leaf only, ignored).
 * @note Ignored if EPT bit 7 is 0. */
#define EPT_E_BIT_SUPER_SHW_STACK           60
#define EPT_E_SUPER_SHW_STACK               RT_BIT_64(EPT_E_BIT_SUPER_SHW_STACK)    /**< @see EPT_E_BIT_SUPER_SHW_STACK */
/** Bit 61 - Sub-page write permission (leaf only, ignored).
 * @note Ignored if sub-page write permission for EPT is disabled. */
#define EPT_E_BIT_SUBPAGE_WRITE_PERM        61
#define EPT_E_SUBPAGE_WRITE_PERM            RT_BIT_64(EPT_E_BIT_SUBPAGE_WRITE_PERM) /**< @see EPT_E_BIT_SUBPAGE_WRITE_PERM*/
/* Bit 62 is always ignored. */
/** Bit 63 - Suppress \#VE (leaf only, ignored).
 * @note Ignored if EPT violation to \#VE conversion is disabled. */
#define EPT_E_BIT_SUPPRESS_VE               63
#define EPT_E_SUPPRESS_VE                   RT_BIT_64(EPT_E_BIT_SUPPRESS_VE)        /**< @see EPT_E_BIT_SUPPRESS_VE */
/** @} */


/**@name Bit fields for common EPT attributes.
 @{ */
/** Read access. */
#define VMX_BF_EPT_PT_READ_SHIFT                        0
#define VMX_BF_EPT_PT_READ_MASK                         UINT64_C(0x0000000000000001)
/** Write access. */
#define VMX_BF_EPT_PT_WRITE_SHIFT                       1
#define VMX_BF_EPT_PT_WRITE_MASK                        UINT64_C(0x0000000000000002)
/** Execute access or execute access for supervisor-mode linear-addresses. */
#define VMX_BF_EPT_PT_EXECUTE_SHIFT                     2
#define VMX_BF_EPT_PT_EXECUTE_MASK                      UINT64_C(0x0000000000000004)
/** EPT memory type. */
#define VMX_BF_EPT_PT_MEMTYPE_SHIFT                     3
#define VMX_BF_EPT_PT_MEMTYPE_MASK                      UINT64_C(0x0000000000000038)
/** Ignore PAT. */
#define VMX_BF_EPT_PT_IGNORE_PAT_SHIFT                  6
#define VMX_BF_EPT_PT_IGNORE_PAT_MASK                   UINT64_C(0x0000000000000040)
/** Ignored (bit 7). */
#define VMX_BF_EPT_PT_IGN_7_SHIFT                       7
#define VMX_BF_EPT_PT_IGN_7_MASK                        UINT64_C(0x0000000000000080)
/** Accessed flag. */
#define VMX_BF_EPT_PT_ACCESSED_SHIFT                    8
#define VMX_BF_EPT_PT_ACCESSED_MASK                     UINT64_C(0x0000000000000100)
/** Dirty flag. */
#define VMX_BF_EPT_PT_DIRTY_SHIFT                       9
#define VMX_BF_EPT_PT_DIRTY_MASK                        UINT64_C(0x0000000000000200)
/** Execute access for user-mode linear addresses. */
#define VMX_BF_EPT_PT_EXECUTE_USER_SHIFT                10
#define VMX_BF_EPT_PT_EXECUTE_USER_MASK                 UINT64_C(0x0000000000000400)
/** Ignored (bit 59:11). */
#define VMX_BF_EPT_PT_IGN_59_11_SHIFT                   11
#define VMX_BF_EPT_PT_IGN_59_11_MASK                    UINT64_C(0x0ffffffffffff800)
/** Supervisor shadow stack. */
#define VMX_BF_EPT_PT_SUPER_SHW_STACK_SHIFT             60
#define VMX_BF_EPT_PT_SUPER_SHW_STACK_MASK              UINT64_C(0x1000000000000000)
/** Ignored (bits 62:61). */
#define VMX_BF_EPT_PT_IGN_62_61_SHIFT                   61
#define VMX_BF_EPT_PT_IGN_62_61_MASK                    UINT64_C(0x6000000000000000)
/** Suppress \#VE. */
#define VMX_BF_EPT_PT_SUPPRESS_VE_SHIFT                 63
#define VMX_BF_EPT_PT_SUPPRESS_VE_MASK                  UINT64_C(0x8000000000000000)
#if !defined(READ) && !defined(WRITE)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EPT_PT_, UINT64_C(0), UINT64_MAX,
                            (READ, WRITE, EXECUTE, MEMTYPE, IGNORE_PAT, IGN_7, ACCESSED, DIRTY, EXECUTE_USER, IGN_59_11,
                            SUPER_SHW_STACK, IGN_62_61, SUPPRESS_VE));
#endif
/** @} */


/** @name VMX Extended Page Tables (EPT) Structures
 * @{
 */

/**
 * Number of page table entries in the EPT. (PDPTE/PDE/PTE)
 */
#define EPT_PG_ENTRIES          X86_PG_PAE_ENTRIES

/**
 * EPT present mask.
 * These are ONLY the common bits in all EPT page-table entries which does
 * not rely on any CPU feature. It isn't necessarily the complete mask (e.g. when
 * mode-based excute control is active).
 */
#define EPT_PRESENT_MASK       (EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE)

/**
 * EPT Page Directory Pointer Entry. Bit view.
 * In accordance with the VT-x spec.
 *
 * @todo uint64_t isn't safe for bitfields (gcc pedantic warnings, and IIRC,
 *       this did cause trouble with one compiler/version).
 */
typedef struct EPTPML4EBITS
{
    /** Present bit. */
    RT_GCC_EXTENSION uint64_t u1Present       : 1;
    /** Writable bit. */
    RT_GCC_EXTENSION uint64_t u1Write         : 1;
    /** Executable bit. */
    RT_GCC_EXTENSION uint64_t u1Execute       : 1;
    /** Reserved (must be 0). */
    RT_GCC_EXTENSION uint64_t u5Reserved      : 5;
    /** Available for software. */
    RT_GCC_EXTENSION uint64_t u4Available     : 4;
    /** Physical address of the next level (PD). Restricted by maximum physical address width of the cpu. */
    RT_GCC_EXTENSION uint64_t u40PhysAddr     : 40;
    /** Available for software. */
    RT_GCC_EXTENSION uint64_t u12Available    : 12;
} EPTPML4EBITS;
AssertCompileSize(EPTPML4EBITS, 8);

/** Bits 12-51 - - EPT - Physical Page number of the next level. */
#define EPT_PML4E_PG_MASK       X86_PML4E_PG_MASK
/** The page shift to get the PML4 index. */
#define EPT_PML4_SHIFT          X86_PML4_SHIFT
/** The PML4 index mask (apply to a shifted page address). */
#define EPT_PML4_MASK           X86_PML4_MASK
/** Bits - - EPT - PML4 MBZ mask. */
#define EPT_PML4E_MBZ_MASK      UINT64_C(0x00000000000000f8)
/** Mask of all possible EPT PML4E attribute bits. */
#define EPT_PML4E_ATTR_MASK     (EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE | EPT_E_ACCESSED | EPT_E_USER_EXECUTE)

/**
 * EPT PML4E.
 * In accordance with the VT-x spec.
 */
typedef union EPTPML4E
{
#ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Normal view. */
    EPTPML4EBITS    n;
#endif
    /** Unsigned integer view. */
    X86PGPAEUINT    u;
    /** 64 bit unsigned integer view. */
    uint64_t        au64[1];
    /** 32 bit unsigned integer view. */
    uint32_t        au32[2];
} EPTPML4E;
AssertCompileSize(EPTPML4E, 8);
/** Pointer to a PML4 table entry. */
typedef EPTPML4E *PEPTPML4E;
/** Pointer to a const PML4 table entry. */
typedef const EPTPML4E *PCEPTPML4E;

/**
 * EPT PML4 Table.
 * In accordance with the VT-x spec.
 */
typedef struct EPTPML4
{
    EPTPML4E    a[EPT_PG_ENTRIES];
} EPTPML4;
AssertCompileSize(EPTPML4, 0x1000);
/** Pointer to an EPT PML4 Table. */
typedef EPTPML4 *PEPTPML4;
/** Pointer to a const EPT PML4 Table. */
typedef const EPTPML4 *PCEPTPML4;


/**
 * EPT Page Directory Pointer Entry. Bit view.
 * In accordance with the VT-x spec.
 */
typedef struct EPTPDPTEBITS
{
    /** Present bit. */
    RT_GCC_EXTENSION uint64_t u1Present       : 1;
    /** Writable bit. */
    RT_GCC_EXTENSION uint64_t u1Write         : 1;
    /** Executable bit. */
    RT_GCC_EXTENSION uint64_t u1Execute       : 1;
    /** Reserved (must be 0). */
    RT_GCC_EXTENSION uint64_t u5Reserved      : 5;
    /** Available for software. */
    RT_GCC_EXTENSION uint64_t u4Available     : 4;
    /** Physical address of the next level (PD). Restricted by maximum physical address width of the cpu. */
    RT_GCC_EXTENSION uint64_t u40PhysAddr     : 40;
    /** Available for software. */
    RT_GCC_EXTENSION uint64_t u12Available    : 12;
} EPTPDPTEBITS;
AssertCompileSize(EPTPDPTEBITS, 8);

/** Bit 7 - - EPT - PDPTE maps a 1GB page. */
#define EPT_PDPTE1G_SIZE_MASK       RT_BIT_64(7)
/** Bits 12-51 - - EPT - Physical Page number of the next level. */
#define EPT_PDPTE_PG_MASK           X86_PDPE_PG_MASK
/** Bits 30-51 - - EPT - Physical Page number of the 1G large page. */
#define EPT_PDPTE1G_PG_MASK         X86_PDPE1G_PG_MASK

/** The page shift to get the PDPT index. */
#define EPT_PDPT_SHIFT              X86_PDPT_SHIFT
/** The PDPT index mask (apply to a shifted page address). */
#define EPT_PDPT_MASK               X86_PDPT_MASK_AMD64
/** Bits 3-7 - - EPT - PDPTE MBZ Mask. */
#define EPT_PDPTE_MBZ_MASK          UINT64_C(0x00000000000000f8)
/** Bits 12-29 - - EPT - 1GB PDPTE MBZ Mask. */
#define EPT_PDPTE1G_MBZ_MASK        UINT64_C(0x000000003ffff000)
/** Mask of all possible EPT PDPTE (1GB) attribute bits. */
#define EPT_PDPTE1G_ATTR_MASK       (  EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE | EPT_E_MEMTYPE_MASK | EPT_E_IGNORE_PAT \
                                     | EPT_E_ACCESSED | EPT_E_DIRTY | EPT_E_USER_EXECUTE)
/** Mask of all possible EPT PDPTE attribute bits. */
#define EPT_PDPTE_ATTR_MASK         (EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE | EPT_E_ACCESSED | EPT_E_USER_EXECUTE)
/** */

/**
 * EPT Page Directory Pointer.
 * In accordance with the VT-x spec.
 */
typedef union EPTPDPTE
{
#ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Normal view. */
    EPTPDPTEBITS    n;
#endif
    /** Unsigned integer view. */
    X86PGPAEUINT    u;
    /** 64 bit unsigned integer view. */
    uint64_t        au64[1];
    /** 32 bit unsigned integer view. */
    uint32_t        au32[2];
} EPTPDPTE;
AssertCompileSize(EPTPDPTE, 8);
/** Pointer to an EPT Page Directory Pointer Entry. */
typedef EPTPDPTE *PEPTPDPTE;
/** Pointer to a const EPT Page Directory Pointer Entry. */
typedef const EPTPDPTE *PCEPTPDPTE;

/**
 * EPT Page Directory Pointer Table.
 * In accordance with the VT-x spec.
 */
typedef struct EPTPDPT
{
    EPTPDPTE    a[EPT_PG_ENTRIES];
} EPTPDPT;
AssertCompileSize(EPTPDPT, 0x1000);
/** Pointer to an EPT Page Directory Pointer Table. */
typedef EPTPDPT *PEPTPDPT;
/** Pointer to a const EPT Page Directory Pointer Table. */
typedef const EPTPDPT *PCEPTPDPT;


/**
 * EPT Page Directory Table Entry. Bit view.
 * In accordance with the VT-x spec.
 */
typedef struct EPTPDEBITS
{
    /** Present bit. */
    RT_GCC_EXTENSION uint64_t u1Present       : 1;
    /** Writable bit. */
    RT_GCC_EXTENSION uint64_t u1Write         : 1;
    /** Executable bit. */
    RT_GCC_EXTENSION uint64_t u1Execute       : 1;
    /** Reserved (must be 0). */
    RT_GCC_EXTENSION uint64_t u4Reserved      : 4;
    /** Big page (must be 0 here). */
    RT_GCC_EXTENSION uint64_t u1Size          : 1;
    /** Available for software. */
    RT_GCC_EXTENSION uint64_t u4Available     : 4;
    /** Physical address of page table. Restricted by maximum physical address width of the cpu. */
    RT_GCC_EXTENSION uint64_t u40PhysAddr     : 40;
    /** Available for software. */
    RT_GCC_EXTENSION uint64_t u12Available    : 12;
} EPTPDEBITS;
AssertCompileSize(EPTPDEBITS, 8);

/** Bits 12-51 - - EPT - Physical Page number of the next level. */
#define EPT_PDE_PG_MASK         X86_PDE_PAE_PG_MASK
/** The page shift to get the PD index. */
#define EPT_PD_SHIFT            X86_PD_PAE_SHIFT
/** The PD index mask (apply to a shifted page address). */
#define EPT_PD_MASK             X86_PD_PAE_MASK
/** Bits 3-7 - EPT - PDE MBZ Mask. */
#define EPT_PDE_MBZ_MASK        UINT64_C(0x00000000000000f8)
/** Mask of all possible EPT PDE (2M) attribute bits. */
#define EPT_PDE2M_ATTR_MASK     (  EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE | EPT_E_MEMTYPE_MASK | EPT_E_IGNORE_PAT \
                                 | EPT_E_ACCESSED | EPT_E_DIRTY | EPT_E_USER_EXECUTE)
/** Mask of all possible EPT PDE attribute bits. */
#define EPT_PDE_ATTR_MASK       (EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE | EPT_E_ACCESSED | EPT_E_USER_EXECUTE)


/**
 * EPT 2MB Page Directory Table Entry. Bit view.
 * In accordance with the VT-x spec.
 */
typedef struct EPTPDE2MBITS
{
    /** Present bit. */
    RT_GCC_EXTENSION uint64_t u1Present       : 1;
    /** Writable bit. */
    RT_GCC_EXTENSION uint64_t u1Write         : 1;
    /** Executable bit. */
    RT_GCC_EXTENSION uint64_t u1Execute       : 1;
    /** EPT Table Memory Type. MBZ for non-leaf nodes. */
    RT_GCC_EXTENSION uint64_t u3EMT           : 3;
    /** Ignore PAT memory type */
    RT_GCC_EXTENSION uint64_t u1IgnorePAT     : 1;
    /** Big page (must be 1 here). */
    RT_GCC_EXTENSION uint64_t u1Size          : 1;
    /** Available for software. */
    RT_GCC_EXTENSION uint64_t u4Available     : 4;
    /** Reserved (must be 0). */
    RT_GCC_EXTENSION uint64_t u9Reserved      : 9;
    /** Physical address of the 2MB page. Restricted by maximum physical address width of the cpu. */
    RT_GCC_EXTENSION uint64_t u31PhysAddr     : 31;
    /** Available for software. */
    RT_GCC_EXTENSION uint64_t u12Available    : 12;
} EPTPDE2MBITS;
AssertCompileSize(EPTPDE2MBITS, 8);

/** Bits 21-51 - - EPT - Physical Page number of the next level. */
#define EPT_PDE2M_PG_MASK       X86_PDE2M_PAE_PG_MASK
/** Bits 20-12 - - EPT - PDE 2M MBZ Mask. */
#define EPT_PDE2M_MBZ_MASK      UINT64_C(0x00000000001ff000)


/**
 * EPT Page Directory Table Entry.
 * In accordance with the VT-x spec.
 */
typedef union EPTPDE
{
#ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Normal view. */
    EPTPDEBITS      n;
    /** 2MB view (big). */
    EPTPDE2MBITS    b;
#endif
    /** Unsigned integer view. */
    X86PGPAEUINT    u;
    /** 64 bit unsigned integer view. */
    uint64_t        au64[1];
    /** 32 bit unsigned integer view. */
    uint32_t        au32[2];
} EPTPDE;
AssertCompileSize(EPTPDE, 8);
/** Pointer to an EPT Page Directory Table Entry. */
typedef EPTPDE *PEPTPDE;
/** Pointer to a const EPT Page Directory Table Entry. */
typedef const EPTPDE *PCEPTPDE;

/**
 * EPT Page Directory Table.
 * In accordance with the VT-x spec.
 */
typedef struct EPTPD
{
    EPTPDE      a[EPT_PG_ENTRIES];
} EPTPD;
AssertCompileSize(EPTPD, 0x1000);
/** Pointer to an EPT Page Directory Table. */
typedef EPTPD *PEPTPD;
/** Pointer to a const EPT Page Directory Table. */
typedef const EPTPD *PCEPTPD;

/**
 * EPT Page Table Entry. Bit view.
 * In accordance with the VT-x spec.
 */
typedef struct EPTPTEBITS
{
    /** 0 - Present bit.
     * @remarks This is a convenience "misnomer". The bit actually indicates read access
     *          and the CPU will consider an entry with any of the first three bits set
     *          as present.  Since all our valid entries will have this bit set, it can
     *          be used as a present indicator and allow some code sharing. */
    RT_GCC_EXTENSION uint64_t u1Present       : 1;
    /** 1 - Writable bit. */
    RT_GCC_EXTENSION uint64_t u1Write         : 1;
    /** 2 - Executable bit. */
    RT_GCC_EXTENSION uint64_t u1Execute       : 1;
    /** 5:3 - EPT Memory Type. MBZ for non-leaf nodes. */
    RT_GCC_EXTENSION uint64_t u3EMT           : 3;
    /** 6 - Ignore PAT memory type */
    RT_GCC_EXTENSION uint64_t u1IgnorePAT     : 1;
    /** 11:7 - Available for software. */
    RT_GCC_EXTENSION uint64_t u5Available     : 5;
    /** 51:12 - Physical address of page. Restricted by maximum physical
     *  address width of the cpu. */
    RT_GCC_EXTENSION uint64_t u40PhysAddr     : 40;
    /** 63:52 - Available for software. */
    RT_GCC_EXTENSION uint64_t u12Available    : 12;
} EPTPTEBITS;
AssertCompileSize(EPTPTEBITS, 8);

/** Bits 12-51 - - EPT - Physical Page number of the next level. */
#define EPT_PTE_PG_MASK         X86_PTE_PAE_PG_MASK
/** The page shift to get the EPT PTE index. */
#define EPT_PT_SHIFT            X86_PT_PAE_SHIFT
/** The EPT PT index mask (apply to a shifted page address). */
#define EPT_PT_MASK             X86_PT_PAE_MASK
/** No bits - - EPT - PTE MBZ bits. */
#define EPT_PTE_MBZ_MASK        UINT64_C(0x0000000000000000)
/** Mask of all possible EPT PTE attribute bits. */
#define EPT_PTE_ATTR_MASK       (  EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE | EPT_E_MEMTYPE_MASK | EPT_E_IGNORE_PAT \
                                 | EPT_E_ACCESSED | EPT_E_USER_EXECUTE)


/**
 * EPT Page Table Entry.
 * In accordance with the VT-x spec.
 */
typedef union EPTPTE
{
#ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Normal view. */
    EPTPTEBITS      n;
#endif
    /** Unsigned integer view. */
    X86PGPAEUINT    u;
    /** 64 bit unsigned integer view. */
    uint64_t        au64[1];
    /** 32 bit unsigned integer view. */
    uint32_t        au32[2];
} EPTPTE;
AssertCompileSize(EPTPTE, 8);
/** Pointer to an EPT Page Directory Table Entry. */
typedef EPTPTE *PEPTPTE;
/** Pointer to a const EPT Page Directory Table Entry. */
typedef const EPTPTE *PCEPTPTE;

/**
 * EPT Page Table.
 * In accordance with the VT-x spec.
 */
typedef struct EPTPT
{
    EPTPTE      a[EPT_PG_ENTRIES];
} EPTPT;
AssertCompileSize(EPTPT, 0x1000);
/** Pointer to an extended page table. */
typedef EPTPT *PEPTPT;
/** Pointer to a const extended table. */
typedef const EPTPT *PCEPTPT;

/** EPTP page mask for the EPT PML4 table. */
#define EPT_EPTP_PG_MASK        X86_CR3_AMD64_PAGE_MASK
/** @} */

/**
 * VMX VPID flush types.
 * Valid enum members are in accordance with the VT-x spec.
 */
typedef enum
{
    /** Invalidate a specific page. */
    VMXTLBFLUSHVPID_INDIV_ADDR                    = 0,
    /** Invalidate one context (specific VPID). */
    VMXTLBFLUSHVPID_SINGLE_CONTEXT                = 1,
    /** Invalidate all contexts (all VPIDs). */
    VMXTLBFLUSHVPID_ALL_CONTEXTS                  = 2,
    /** Invalidate a single VPID context retaining global mappings. */
    VMXTLBFLUSHVPID_SINGLE_CONTEXT_RETAIN_GLOBALS = 3,
    /** Unsupported by VirtualBox. */
    VMXTLBFLUSHVPID_NOT_SUPPORTED                 = 0xbad0,
    /** Unsupported by CPU. */
    VMXTLBFLUSHVPID_NONE                          = 0xbad1
} VMXTLBFLUSHVPID;
AssertCompileSize(VMXTLBFLUSHVPID, 4);
/** Mask of all valid INVVPID flush types.  */
#define VMX_INVVPID_VALID_MASK                    (  VMXTLBFLUSHVPID_INDIV_ADDR \
                                                   | VMXTLBFLUSHVPID_SINGLE_CONTEXT \
                                                   | VMXTLBFLUSHVPID_ALL_CONTEXTS \
                                                   | VMXTLBFLUSHVPID_SINGLE_CONTEXT_RETAIN_GLOBALS)

/**
 * VMX EPT flush types.
 * @note Valid enums values are in accordance with the VT-x spec.
 */
typedef enum
{
    /** Invalidate one context (specific EPT). */
    VMXTLBFLUSHEPT_SINGLE_CONTEXT              = 1,
    /* Invalidate all contexts (all EPTs) */
    VMXTLBFLUSHEPT_ALL_CONTEXTS                = 2,
    /** Unsupported by VirtualBox. */
    VMXTLBFLUSHEPT_NOT_SUPPORTED               = 0xbad0,
    /** Unsupported by CPU. */
    VMXTLBFLUSHEPT_NONE                        = 0xbad1
} VMXTLBFLUSHEPT;
AssertCompileSize(VMXTLBFLUSHEPT, 4);
/** Mask of all valid INVEPT flush types.  */
#define VMX_INVEPT_VALID_MASK                     (  VMXTLBFLUSHEPT_SINGLE_CONTEXT \
                                                   | VMXTLBFLUSHEPT_ALL_CONTEXTS)

/**
 * VMX Posted Interrupt Descriptor.
 * In accordance with the VT-x spec.
 */
typedef struct VMXPOSTEDINTRDESC
{
    uint32_t    aVectorBitmap[8];
    uint32_t    fOutstandingNotification : 1;
    uint32_t    uReserved0               : 31;
    uint8_t     au8Reserved0[28];
} VMXPOSTEDINTRDESC;
AssertCompileMemberSize(VMXPOSTEDINTRDESC, aVectorBitmap, 32);
AssertCompileSize(VMXPOSTEDINTRDESC, 64);
/** Pointer to a posted interrupt descriptor. */
typedef VMXPOSTEDINTRDESC *PVMXPOSTEDINTRDESC;
/** Pointer to a const posted interrupt descriptor. */
typedef const VMXPOSTEDINTRDESC *PCVMXPOSTEDINTRDESC;

/**
 * VMX VMCS revision identifier.
 * In accordance with the VT-x spec.
 */
typedef union
{
    struct
    {
        /** Revision identifier. */
        uint32_t    u31RevisionId : 31;
        /** Whether this is a shadow VMCS. */
        uint32_t    fIsShadowVmcs : 1;
    } n;
    /* The unsigned integer view. */
    uint32_t        u;
} VMXVMCSREVID;
AssertCompileSize(VMXVMCSREVID, 4);
/** Pointer to the VMXVMCSREVID union. */
typedef VMXVMCSREVID *PVMXVMCSREVID;
/** Pointer to a const VMXVMCSREVID union. */
typedef const VMXVMCSREVID *PCVMXVMCSREVID;

/**
 * VMX VM-exit instruction information.
 * In accordance with the VT-x spec.
 */
typedef union
{
    /** Plain unsigned int representation. */
    uint32_t    u;

    /** INS and OUTS information. */
    struct
    {
        uint32_t    u7Reserved0 : 7;
        /** The address size; 0=16-bit, 1=32-bit, 2=64-bit, rest undefined. */
        uint32_t    u3AddrSize  : 3;
        uint32_t    u5Reserved1 : 5;
        /** The segment register (X86_SREG_XXX). */
        uint32_t    iSegReg     : 3;
        uint32_t    uReserved2  : 14;
    } StrIo;

    /** INVEPT, INVPCID, INVVPID information. */
    struct
    {
        /** Scaling; 0=no scaling, 1=scale-by-2, 2=scale-by-4, 3=scale-by-8. */
        uint32_t    u2Scaling       : 2;
        uint32_t    u5Undef0        : 5;
        /** The address size; 0=16-bit, 1=32-bit, 2=64-bit, rest undefined. */
        uint32_t    u3AddrSize      : 3;
        /** Cleared to 0. */
        uint32_t    u1Cleared0      : 1;
        uint32_t    u4Undef0        : 4;
        /** The segment register (X86_SREG_XXX). */
        uint32_t    iSegReg         : 3;
        /** The index register (X86_GREG_XXX). */
        uint32_t    iIdxReg         : 4;
        /** Set if index register is invalid. */
        uint32_t    fIdxRegInvalid  : 1;
        /** The base register (X86_GREG_XXX). */
        uint32_t    iBaseReg        : 4;
        /** Set if base register is invalid. */
        uint32_t    fBaseRegInvalid : 1;
        /** Register 2 (X86_GREG_XXX). */
        uint32_t    iReg2           : 4;
    } Inv;

    /** VMCLEAR, VMPTRLD, VMPTRST, VMXON, XRSTORS, XSAVES information. */
    struct
    {
        /** Scaling; 0=no scaling, 1=scale-by-2, 2=scale-by-4, 3=scale-by-8. */
        uint32_t    u2Scaling       : 2;
        uint32_t    u5Reserved0     : 5;
        /** The address size; 0=16-bit, 1=32-bit, 2=64-bit, rest undefined. */
        uint32_t    u3AddrSize      : 3;
        /** Cleared to 0. */
        uint32_t    u1Cleared0      : 1;
        uint32_t    u4Reserved0     : 4;
        /** The segment register (X86_SREG_XXX). */
        uint32_t    iSegReg         : 3;
        /** The index register (X86_GREG_XXX). */
        uint32_t    iIdxReg         : 4;
        /** Set if index register is invalid. */
        uint32_t    fIdxRegInvalid  : 1;
        /** The base register (X86_GREG_XXX). */
        uint32_t    iBaseReg        : 4;
        /** Set if base register is invalid. */
        uint32_t    fBaseRegInvalid : 1;
        /** Register 2 (X86_GREG_XXX). */
        uint32_t    iReg2           : 4;
    } VmxXsave;

    /** LIDT, LGDT, SIDT, SGDT information. */
    struct
    {
        /** Scaling; 0=no scaling, 1=scale-by-2, 2=scale-by-4, 3=scale-by-8. */
        uint32_t    u2Scaling       : 2;
        uint32_t    u5Undef0        : 5;
        /** The address size; 0=16-bit, 1=32-bit, 2=64-bit, rest undefined. */
        uint32_t    u3AddrSize      : 3;
        /** Always cleared to 0. */
        uint32_t    u1Cleared0      : 1;
        /** Operand size; 0=16-bit, 1=32-bit, undefined for 64-bit. */
        uint32_t    uOperandSize    : 1;
        uint32_t    u3Undef0        : 3;
        /** The segment register (X86_SREG_XXX). */
        uint32_t    iSegReg         : 3;
        /** The index register (X86_GREG_XXX). */
        uint32_t    iIdxReg         : 4;
        /** Set if index register is invalid. */
        uint32_t    fIdxRegInvalid  : 1;
        /** The base register (X86_GREG_XXX). */
        uint32_t    iBaseReg        : 4;
        /** Set if base register is invalid. */
        uint32_t    fBaseRegInvalid : 1;
        /** Instruction identity (VMX_INSTR_ID_XXX). */
        uint32_t    u2InstrId       : 2;
        uint32_t    u2Undef0        : 2;
    } GdtIdt;

    /** LLDT, LTR, SLDT, STR information. */
    struct
    {
        /** Scaling; 0=no scaling, 1=scale-by-2, 2=scale-by-4, 3=scale-by-8. */
        uint32_t    u2Scaling       : 2;
        uint32_t    u1Undef0        : 1;
        /** Register 1 (X86_GREG_XXX). */
        uint32_t    iReg1           : 4;
        /** The address size; 0=16-bit, 1=32-bit, 2=64-bit, rest undefined. */
        uint32_t    u3AddrSize      : 3;
        /** Memory/Register - Always cleared to 0 to indicate memory operand. */
        uint32_t    fIsRegOperand   : 1;
        uint32_t    u4Undef0        : 4;
        /** The segment register (X86_SREG_XXX). */
        uint32_t    iSegReg         : 3;
        /** The index register (X86_GREG_XXX). */
        uint32_t    iIdxReg         : 4;
        /** Set if index register is invalid. */
        uint32_t    fIdxRegInvalid  : 1;
        /** The base register (X86_GREG_XXX). */
        uint32_t    iBaseReg        : 4;
        /** Set if base register is invalid. */
        uint32_t    fBaseRegInvalid : 1;
        /** Instruction identity (VMX_INSTR_ID_XXX). */
        uint32_t    u2InstrId       : 2;
        uint32_t    u2Undef0        : 2;
    } LdtTr;

    /** RDRAND, RDSEED information. */
    struct
    {
        uint32_t    u3Undef0        : 3;
        /** Destination register (X86_GREG_XXX). */
        uint32_t    iReg1           : 4;
        uint32_t    u4Undef0        : 4;
        /** Operand size; 0=16-bit, 1=32-bit, 2=64-bit, 3=unused. */
        uint32_t    u2OperandSize   : 2;
        uint32_t    u19Undef0       : 19;
    } RdrandRdseed;

    /** VMREAD, VMWRITE information. */
    struct
    {
        /** Scaling; 0=no scaling, 1=scale-by-2, 2=scale-by-4, 3=scale-by-8. */
        uint32_t    u2Scaling       : 2;
        uint32_t    u1Undef0        : 1;
        /** Register 1 (X86_GREG_XXX). */
        uint32_t    iReg1           : 4;
        /** The address size; 0=16-bit, 1=32-bit, 2=64-bit, rest undefined. */
        uint32_t    u3AddrSize      : 3;
        /** Memory or register operand. */
        uint32_t    fIsRegOperand   : 1;
        /** Operand size; 0=16-bit, 1=32-bit, 2=64-bit, 3=unused. */
        uint32_t    u4Undef0        : 4;
        /** The segment register (X86_SREG_XXX). */
        uint32_t    iSegReg         : 3;
        /** The index register (X86_GREG_XXX). */
        uint32_t    iIdxReg         : 4;
        /** Set if index register is invalid. */
        uint32_t    fIdxRegInvalid  : 1;
        /** The base register (X86_GREG_XXX). */
        uint32_t    iBaseReg        : 4;
        /** Set if base register is invalid. */
        uint32_t    fBaseRegInvalid : 1;
        /** Register 2 (X86_GREG_XXX). */
        uint32_t    iReg2           : 4;
    } VmreadVmwrite;

    /** LOADIWKEY information. */
    struct
    {
        uint32_t    u2Undef0        : 3;
        /** First XMM register operand. */
        uint32_t    u4XmmReg1       : 4;
        uint32_t    u23Undef1       : 21;
        /** Second XMM register operand. */
        uint32_t    u4XmmReg2       : 4;
    } LoadIwkey;

    /** This is a combination field of all instruction information. Note! Not all field
     *  combinations are valid (e.g., iReg1 is undefined for memory operands) and
     *  specialized fields are overwritten by their generic counterparts (e.g. no
     *  instruction identity field). */
    struct
    {
        /** Scaling; 0=no scaling, 1=scale-by-2, 2=scale-by-4, 3=scale-by-8. */
        uint32_t    u2Scaling       : 2;
        uint32_t    u1Undef0        : 1;
        /** Register 1 (X86_GREG_XXX). */
        uint32_t    iReg1           : 4;
        /** The address size; 0=16-bit, 1=32-bit, 2=64-bit, rest undefined. */
        uint32_t    u3AddrSize      : 3;
        /** Memory/Register - Always cleared to 0 to indicate memory operand. */
        uint32_t    fIsRegOperand   : 1;
        /** Operand size; 0=16-bit, 1=32-bit, 2=64-bit, 3=unused. */
        uint32_t    uOperandSize    : 2;
        uint32_t    u2Undef0        : 2;
        /** The segment register (X86_SREG_XXX). */
        uint32_t    iSegReg         : 3;
        /** The index register (X86_GREG_XXX). */
        uint32_t    iIdxReg         : 4;
        /** Set if index register is invalid. */
        uint32_t    fIdxRegInvalid  : 1;
        /** The base register (X86_GREG_XXX). */
        uint32_t    iBaseReg        : 4;
        /** Set if base register is invalid. */
        uint32_t    fBaseRegInvalid : 1;
        /** Register 2 (X86_GREG_XXX) or instruction identity. */
        uint32_t    iReg2           : 4;
    } All;
} VMXEXITINSTRINFO;
AssertCompileSize(VMXEXITINSTRINFO, 4);
/** Pointer to a VMX VM-exit instruction info. struct. */
typedef VMXEXITINSTRINFO *PVMXEXITINSTRINFO;
/** Pointer to a const VMX VM-exit instruction info. struct. */
typedef const VMXEXITINSTRINFO *PCVMXEXITINSTRINFO;


/** @name VM-entry failure reported in Exit qualification.
 * See Intel spec. 26.7 "VM-entry failures during or after loading guest-state".
 * @{
 */
/** No errors during VM-entry. */
#define VMX_ENTRY_FAIL_QUAL_NO_ERROR                            (0)
/** Not used. */
#define VMX_ENTRY_FAIL_QUAL_NOT_USED                            (1)
/** Error while loading PDPTEs. */
#define VMX_ENTRY_FAIL_QUAL_PDPTE                               (2)
/** NMI injection when blocking-by-STI is set. */
#define VMX_ENTRY_FAIL_QUAL_NMI_INJECT                          (3)
/** Invalid VMCS link pointer. */
#define VMX_ENTRY_FAIL_QUAL_VMCS_LINK_PTR                       (4)
/** @} */


/** @name VMXMSRPM_XXX - VMX MSR-bitmap permissions.
 * These are -not- specified by Intel but used internally by VirtualBox.
 * @{ */
/** Guest software reads of this MSR must not cause a VM-exit. */
#define VMXMSRPM_ALLOW_RD                                       RT_BIT(0)
/** Guest software reads of this MSR must cause a VM-exit. */
#define VMXMSRPM_EXIT_RD                                        RT_BIT(1)
/** Guest software writes to this MSR must not cause a VM-exit. */
#define VMXMSRPM_ALLOW_WR                                       RT_BIT(2)
/** Guest software writes to this MSR must cause a VM-exit. */
#define VMXMSRPM_EXIT_WR                                        RT_BIT(3)
/** Guest software reads or writes of this MSR must not cause a VM-exit. */
#define VMXMSRPM_ALLOW_RD_WR                                    (VMXMSRPM_ALLOW_RD | VMXMSRPM_ALLOW_WR)
/** Guest software reads or writes of this MSR must cause a VM-exit. */
#define VMXMSRPM_EXIT_RD_WR                                     (VMXMSRPM_EXIT_RD  | VMXMSRPM_EXIT_WR)
/** Mask of valid MSR read permissions. */
#define VMXMSRPM_RD_MASK                                        (VMXMSRPM_ALLOW_RD | VMXMSRPM_EXIT_RD)
/** Mask of valid MSR write permissions. */
#define VMXMSRPM_WR_MASK                                        (VMXMSRPM_ALLOW_WR | VMXMSRPM_EXIT_WR)
/** Mask of valid MSR permissions. */
#define VMXMSRPM_MASK                                           (VMXMSRPM_RD_MASK  | VMXMSRPM_WR_MASK)
/** */
/** Gets whether the MSR permission is valid or not. */
#define VMXMSRPM_IS_FLAG_VALID(a_Msrpm)                         (    (a_Msrpm) != 0 \
                                                                 && ((a_Msrpm) & ~VMXMSRPM_MASK) == 0 \
                                                                 && ((a_Msrpm) & VMXMSRPM_RD_MASK) != VMXMSRPM_RD_MASK \
                                                                 && ((a_Msrpm) & VMXMSRPM_WR_MASK) != VMXMSRPM_WR_MASK)
/** @} */

/**
 * VMX MSR autoload/store slot.
 * In accordance with the VT-x spec.
 */
typedef struct VMXAUTOMSR
{
    /** The MSR Id. */
    uint32_t    u32Msr;
    /** Reserved (MBZ). */
    uint32_t    u32Reserved;
    /** The MSR value. */
    uint64_t    u64Value;
} VMXAUTOMSR;
AssertCompileSize(VMXAUTOMSR, 16);
/** Pointer to an MSR load/store element. */
typedef VMXAUTOMSR *PVMXAUTOMSR;
/** Pointer to a const MSR load/store element. */
typedef const VMXAUTOMSR *PCVMXAUTOMSR;

/** VMX auto load-store MSR (VMXAUTOMSR) offset mask. */
#define VMX_AUTOMSR_OFFSET_MASK         0xf

/**
 * VMX tagged-TLB flush types.
 */
typedef enum
{
    VMXTLBFLUSHTYPE_EPT,
    VMXTLBFLUSHTYPE_VPID,
    VMXTLBFLUSHTYPE_EPT_VPID,
    VMXTLBFLUSHTYPE_NONE
} VMXTLBFLUSHTYPE;
/** Pointer to a VMXTLBFLUSHTYPE enum. */
typedef VMXTLBFLUSHTYPE *PVMXTLBFLUSHTYPE;
/** Pointer to a const VMXTLBFLUSHTYPE enum. */
typedef const VMXTLBFLUSHTYPE *PCVMXTLBFLUSHTYPE;

/**
 * VMX controls MSR.
 * In accordance with the VT-x spec.
 */
typedef union
{
    struct
    {
        /** Bits set here -must- be set in the corresponding VM-execution controls. */
        uint32_t        allowed0;
        /** Bits cleared here -must- be cleared in the corresponding VM-execution
         *  controls. */
        uint32_t        allowed1;
    } n;
    uint64_t            u;
} VMXCTLSMSR;
AssertCompileSize(VMXCTLSMSR, 8);
/** Pointer to a VMXCTLSMSR union. */
typedef VMXCTLSMSR *PVMXCTLSMSR;
/** Pointer to a const VMXCTLSMSR union. */
typedef const VMXCTLSMSR *PCVMXCTLSMSR;

/**
 * VMX MSRs.
 */
typedef struct VMXMSRS
{
    /** Basic information. */
    uint64_t        u64Basic;
    /** Pin-based VM-execution controls. */
    VMXCTLSMSR      PinCtls;
    /** Processor-based VM-execution controls. */
    VMXCTLSMSR      ProcCtls;
    /** Secondary processor-based VM-execution controls. */
    VMXCTLSMSR      ProcCtls2;
    /** VM-exit controls. */
    VMXCTLSMSR      ExitCtls;
    /** VM-entry controls. */
    VMXCTLSMSR      EntryCtls;
    /** True pin-based VM-execution controls. */
    VMXCTLSMSR      TruePinCtls;
    /** True processor-based VM-execution controls. */
    VMXCTLSMSR      TrueProcCtls;
    /** True VM-entry controls. */
    VMXCTLSMSR      TrueEntryCtls;
    /** True VM-exit controls. */
    VMXCTLSMSR      TrueExitCtls;
    /** Miscellaneous data. */
    uint64_t        u64Misc;
    /** CR0 fixed-0 - bits set here must be set in VMX operation. */
    uint64_t        u64Cr0Fixed0;
    /** CR0 fixed-1 - bits clear here must be clear in VMX operation. */
    uint64_t        u64Cr0Fixed1;
    /** CR4 fixed-0 - bits set here must be set in VMX operation. */
    uint64_t        u64Cr4Fixed0;
    /** CR4 fixed-1 - bits clear here must be clear in VMX operation. */
    uint64_t        u64Cr4Fixed1;
    /** VMCS enumeration. */
    uint64_t        u64VmcsEnum;
    /** VM Functions. */
    uint64_t        u64VmFunc;
    /** EPT, VPID capabilities. */
    uint64_t        u64EptVpidCaps;
    /** Tertiary processor-based VM-execution controls. */
    uint64_t        u64ProcCtls3;
    /** Secondary VM-exit controls. */
    uint64_t        u64ExitCtls2;
    /** Reserved for future. */
    uint64_t        a_u64Reserved[8];
} VMXMSRS;
AssertCompileSizeAlignment(VMXMSRS, 8);
AssertCompileSize(VMXMSRS, 224);
/** Pointer to a VMXMSRS struct. */
typedef VMXMSRS *PVMXMSRS;
/** Pointer to a const VMXMSRS struct. */
typedef const VMXMSRS *PCVMXMSRS;


/**
 * LBR MSRs.
 */
typedef struct LBRMSRS
{
    /** List of LastBranch-From-IP MSRs. */
    uint64_t    au64BranchFromIpMsr[32];
    /** List of LastBranch-To-IP MSRs. */
    uint64_t    au64BranchToIpMsr[32];
    /** The MSR containing the index to the most recent branch record.  */
    uint64_t    uBranchTosMsr;
} LBRMSRS;
AssertCompileSizeAlignment(LBRMSRS, 8);
/** Pointer to a VMXMSRS struct. */
typedef LBRMSRS *PLBRMSRS;
/** Pointer to a const VMXMSRS struct. */
typedef const LBRMSRS *PCLBRMSRS;


/** @name VMX Basic Exit Reasons.
 * In accordance with the VT-x spec.
 * Update g_aVMExitHandlers if new VM-exit reasons are added.
 * @{
 */
/** Invalid exit code */
#define VMX_EXIT_INVALID                                        (-1)
/** Exception or non-maskable interrupt (NMI). */
#define VMX_EXIT_XCPT_OR_NMI                                    0
/** External interrupt. */
#define VMX_EXIT_EXT_INT                                        1
/** Triple fault. */
#define VMX_EXIT_TRIPLE_FAULT                                   2
/** INIT signal. */
#define VMX_EXIT_INIT_SIGNAL                                    3
/** Start-up IPI (SIPI). */
#define VMX_EXIT_SIPI                                           4
/** I/O system-management interrupt (SMI). */
#define VMX_EXIT_IO_SMI                                         5
/** Other SMI. */
#define VMX_EXIT_SMI                                            6
/** Interrupt window exiting. */
#define VMX_EXIT_INT_WINDOW                                     7
/** NMI window exiting. */
#define VMX_EXIT_NMI_WINDOW                                     8
/** Task switch. */
#define VMX_EXIT_TASK_SWITCH                                    9
/** CPUID. */
#define VMX_EXIT_CPUID                                          10
/** GETSEC. */
#define VMX_EXIT_GETSEC                                         11
/** HLT. */
#define VMX_EXIT_HLT                                            12
/** INVD. */
#define VMX_EXIT_INVD                                           13
/** INVLPG. */
#define VMX_EXIT_INVLPG                                         14
/** RDPMC. */
#define VMX_EXIT_RDPMC                                          15
/** RDTSC. */
#define VMX_EXIT_RDTSC                                          16
/** RSM in SMM. */
#define VMX_EXIT_RSM                                            17
/** VMCALL. */
#define VMX_EXIT_VMCALL                                         18
/** VMCLEAR. */
#define VMX_EXIT_VMCLEAR                                        19
/** VMLAUNCH. */
#define VMX_EXIT_VMLAUNCH                                       20
/** VMPTRLD. */
#define VMX_EXIT_VMPTRLD                                        21
/** VMPTRST. */
#define VMX_EXIT_VMPTRST                                        22
/** VMREAD. */
#define VMX_EXIT_VMREAD                                         23
/** VMRESUME. */
#define VMX_EXIT_VMRESUME                                       24
/** VMWRITE. */
#define VMX_EXIT_VMWRITE                                        25
/** VMXOFF. */
#define VMX_EXIT_VMXOFF                                         26
/** VMXON. */
#define VMX_EXIT_VMXON                                          27
/** Control-register accesses. */
#define VMX_EXIT_MOV_CRX                                        28
/** Debug-register accesses. */
#define VMX_EXIT_MOV_DRX                                        29
/** I/O instruction. */
#define VMX_EXIT_IO_INSTR                                       30
/** RDMSR. */
#define VMX_EXIT_RDMSR                                          31
/** WRMSR. */
#define VMX_EXIT_WRMSR                                          32
/** VM-entry failure due to invalid guest state. */
#define VMX_EXIT_ERR_INVALID_GUEST_STATE                        33
/** VM-entry failure due to MSR loading. */
#define VMX_EXIT_ERR_MSR_LOAD                                   34
/** MWAIT. */
#define VMX_EXIT_MWAIT                                          36
/** VM-exit due to monitor trap flag. */
#define VMX_EXIT_MTF                                            37
/** MONITOR. */
#define VMX_EXIT_MONITOR                                        39
/** PAUSE. */
#define VMX_EXIT_PAUSE                                          40
/** VM-entry failure due to machine-check. */
#define VMX_EXIT_ERR_MACHINE_CHECK                              41
/** TPR below threshold. Guest software executed MOV to CR8. */
#define VMX_EXIT_TPR_BELOW_THRESHOLD                            43
/** VM-exit due to guest accessing physical address in the APIC-access page. */
#define VMX_EXIT_APIC_ACCESS                                    44
/** VM-exit due to EOI virtualization. */
#define VMX_EXIT_VIRTUALIZED_EOI                                45
/** Access to GDTR/IDTR using LGDT, LIDT, SGDT or SIDT. */
#define VMX_EXIT_GDTR_IDTR_ACCESS                               46
/** Access to LDTR/TR due to LLDT, LTR, SLDT, or STR. */
#define VMX_EXIT_LDTR_TR_ACCESS                                 47
/** EPT violation. */
#define VMX_EXIT_EPT_VIOLATION                                  48
/** EPT misconfiguration. */
#define VMX_EXIT_EPT_MISCONFIG                                  49
/** INVEPT. */
#define VMX_EXIT_INVEPT                                         50
/** RDTSCP. */
#define VMX_EXIT_RDTSCP                                         51
/** VMX-preemption timer expired. */
#define VMX_EXIT_PREEMPT_TIMER                                  52
/** INVVPID. */
#define VMX_EXIT_INVVPID                                        53
/** WBINVD. */
#define VMX_EXIT_WBINVD                                         54
/** XSETBV. */
#define VMX_EXIT_XSETBV                                         55
/** Guest completed write to virtual-APIC. */
#define VMX_EXIT_APIC_WRITE                                     56
/** RDRAND. */
#define VMX_EXIT_RDRAND                                         57
/** INVPCID. */
#define VMX_EXIT_INVPCID                                        58
/** VMFUNC. */
#define VMX_EXIT_VMFUNC                                         59
/** ENCLS. */
#define VMX_EXIT_ENCLS                                          60
/** RDSEED. */
#define VMX_EXIT_RDSEED                                         61
/** Page-modification log full. */
#define VMX_EXIT_PML_FULL                                       62
/** XSAVES. */
#define VMX_EXIT_XSAVES                                         63
/** XRSTORS. */
#define VMX_EXIT_XRSTORS                                        64
/** SPP-related event (SPP miss or misconfiguration). */
#define VMX_EXIT_SPP_EVENT                                      66
/* UMWAIT. */
#define VMX_EXIT_UMWAIT                                         67
/** TPAUSE. */
#define VMX_EXIT_TPAUSE                                         68
/** LOADIWKEY. */
#define VMX_EXIT_LOADIWKEY                                      69
/** The maximum VM-exit value (inclusive). */
#define VMX_EXIT_MAX                                            (VMX_EXIT_LOADIWKEY)
/** @} */


/** @name VM Instruction Errors.
 * In accordance with the VT-x spec.
 * See Intel spec. "30.4 VM Instruction Error Numbers"
 * @{
 */
typedef enum
{
    /** VMCALL executed in VMX root operation. */
    VMXINSTRERR_VMCALL_VMXROOTMODE             = 1,
    /** VMCLEAR with invalid physical address. */
    VMXINSTRERR_VMCLEAR_INVALID_PHYSADDR       = 2,
    /** VMCLEAR with VMXON pointer. */
    VMXINSTRERR_VMCLEAR_VMXON_PTR              = 3,
    /** VMLAUNCH with non-clear VMCS. */
    VMXINSTRERR_VMLAUNCH_NON_CLEAR_VMCS        = 4,
    /** VMRESUME with non-launched VMCS. */
    VMXINSTRERR_VMRESUME_NON_LAUNCHED_VMCS     = 5,
    /** VMRESUME after VMXOFF (VMXOFF and VMXON between VMLAUNCH and VMRESUME). */
    VMXINSTRERR_VMRESUME_AFTER_VMXOFF          = 6,
    /** VM-entry with invalid control field(s). */
    VMXINSTRERR_VMENTRY_INVALID_CTLS           = 7,
    /** VM-entry with invalid host-state field(s). */
    VMXINSTRERR_VMENTRY_INVALID_HOST_STATE     = 8,
    /** VMPTRLD with invalid physical address. */
    VMXINSTRERR_VMPTRLD_INVALID_PHYSADDR       = 9,
    /** VMPTRLD with VMXON pointer. */
    VMXINSTRERR_VMPTRLD_VMXON_PTR              = 10,
    /** VMPTRLD with incorrect VMCS revision identifier. */
    VMXINSTRERR_VMPTRLD_INCORRECT_VMCS_REV     = 11,
    /** VMREAD from unsupported VMCS component. */
    VMXINSTRERR_VMREAD_INVALID_COMPONENT       = 12,
    /** VMWRITE to unsupported VMCS component. */
    VMXINSTRERR_VMWRITE_INVALID_COMPONENT      = 12,
    /** VMWRITE to read-only VMCS component. */
    VMXINSTRERR_VMWRITE_RO_COMPONENT           = 13,
    /** VMXON executed in VMX root operation. */
    VMXINSTRERR_VMXON_IN_VMXROOTMODE           = 15,
    /** VM-entry with invalid executive-VMCS pointer. */
    VMXINSTRERR_VMENTRY_EXEC_VMCS_INVALID_PTR  = 16,
    /** VM-entry with non-launched executive VMCS. */
    VMXINSTRERR_VMENTRY_EXEC_VMCS_NON_LAUNCHED = 17,
    /** VM-entry with executive-VMCS pointer not VMXON pointer. */
    VMXINSTRERR_VMENTRY_EXEC_VMCS_PTR          = 18,
    /** VMCALL with non-clear VMCS. */
    VMXINSTRERR_VMCALL_NON_CLEAR_VMCS          = 19,
    /** VMCALL with invalid VM-exit control fields. */
    VMXINSTRERR_VMCALL_INVALID_EXITCTLS        = 20,
    /** VMCALL with incorrect MSEG revision identifier. */
    VMXINSTRERR_VMCALL_INVALID_MSEG_ID         = 22,
    /** VMXOFF under dual-monitor treatment of SMIs and SMM. */
    VMXINSTRERR_VMXOFF_DUAL_MON                = 23,
    /** VMCALL with invalid SMM-monitor features. */
    VMXINSTRERR_VMCALL_INVALID_SMMCTLS         = 24,
    /** VM-entry with invalid VM-execution control fields in executive VMCS. */
    VMXINSTRERR_VMENTRY_EXEC_VMCS_INVALID_CTLS = 25,
    /** VM-entry with events blocked by MOV SS. */
    VMXINSTRERR_VMENTRY_BLOCK_MOVSS            = 26,
    /** Invalid operand to INVEPT/INVVPID. */
    VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND = 28
} VMXINSTRERR;
/** @} */


/** @name VMX abort reasons.
 * In accordance with the VT-x spec.
 * See Intel spec. "27.7 VMX Aborts".
 * Update HMGetVmxAbortDesc() if new reasons are added.
 * @{
 */
typedef enum
{
    /** None - don't use this / uninitialized value. */
    VMXABORT_NONE                  = 0,
    /** VMX abort caused during saving of guest MSRs. */
    VMXABORT_SAVE_GUEST_MSRS       = 1,
    /** VMX abort caused during host PDPTE checks. */
    VMXBOART_HOST_PDPTE            = 2,
    /** VMX abort caused due to current VMCS being corrupted. */
    VMXABORT_CURRENT_VMCS_CORRUPT  = 3,
    /** VMX abort caused during loading of host MSRs. */
    VMXABORT_LOAD_HOST_MSR         = 4,
    /** VMX abort caused due to a machine-check exception during VM-exit. */
    VMXABORT_MACHINE_CHECK_XCPT    = 5,
    /** VMX abort caused due to invalid return from long mode. */
    VMXABORT_HOST_NOT_IN_LONG_MODE = 6,
    /* Type size hack. */
    VMXABORT_32BIT_HACK            = 0x7fffffff
} VMXABORT;
AssertCompileSize(VMXABORT, 4);
/** @} */


/** @name VMX MSR - Basic VMX information.
 * @{
 */
/** VMCS (and related regions) memory type - Uncacheable. */
#define VMX_BASIC_MEM_TYPE_UC                                   0
/** VMCS (and related regions) memory type - Write back. */
#define VMX_BASIC_MEM_TYPE_WB                                   6
/** Width of physical addresses used for VMCS and associated memory regions
 *  (1=32-bit, 0=processor's physical address width). */
#define VMX_BASIC_PHYSADDR_WIDTH_32BIT                          RT_BIT_64(48)

/** Bit fields for MSR_IA32_VMX_BASIC. */
/** VMCS revision identifier used by the processor. */
#define VMX_BF_BASIC_VMCS_ID_SHIFT                              0
#define VMX_BF_BASIC_VMCS_ID_MASK                               UINT64_C(0x000000007fffffff)
/** Bit 31 is reserved and RAZ. */
#define VMX_BF_BASIC_RSVD_32_SHIFT                              31
#define VMX_BF_BASIC_RSVD_32_MASK                               UINT64_C(0x0000000080000000)
/** VMCS size in bytes. */
#define VMX_BF_BASIC_VMCS_SIZE_SHIFT                            32
#define VMX_BF_BASIC_VMCS_SIZE_MASK                             UINT64_C(0x00001fff00000000)
/** Bits 45:47 are reserved. */
#define VMX_BF_BASIC_RSVD_45_47_SHIFT                           45
#define VMX_BF_BASIC_RSVD_45_47_MASK                            UINT64_C(0x0000e00000000000)
/** Width of physical addresses used for the VMCS and associated memory regions
 *  (always 0 on CPUs that support Intel 64 architecture). */
#define VMX_BF_BASIC_PHYSADDR_WIDTH_SHIFT                       48
#define VMX_BF_BASIC_PHYSADDR_WIDTH_MASK                        UINT64_C(0x0001000000000000)
/** Dual-monitor treatment of SMI and SMM supported. */
#define VMX_BF_BASIC_DUAL_MON_SHIFT                             49
#define VMX_BF_BASIC_DUAL_MON_MASK                              UINT64_C(0x0002000000000000)
/** Memory type that must be used for the VMCS and associated memory regions. */
#define VMX_BF_BASIC_VMCS_MEM_TYPE_SHIFT                        50
#define VMX_BF_BASIC_VMCS_MEM_TYPE_MASK                         UINT64_C(0x003c000000000000)
/** VM-exit instruction information for INS/OUTS. */
#define VMX_BF_BASIC_VMCS_INS_OUTS_SHIFT                        54
#define VMX_BF_BASIC_VMCS_INS_OUTS_MASK                         UINT64_C(0x0040000000000000)
/** Whether 'true' VMX controls MSRs are supported for handling of default1 class
 *  bits in VMX control MSRs. */
#define VMX_BF_BASIC_TRUE_CTLS_SHIFT                            55
#define VMX_BF_BASIC_TRUE_CTLS_MASK                             UINT64_C(0x0080000000000000)
/** Whether VM-entry can delivery error code for all hardware exception vectors. */
#define VMX_BF_BASIC_XCPT_ERRCODE_SHIFT                         56
#define VMX_BF_BASIC_XCPT_ERRCODE_MASK                          UINT64_C(0x0100000000000000)
/** Bits 57:63 are reserved and RAZ. */
#define VMX_BF_BASIC_RSVD_56_63_SHIFT                           57
#define VMX_BF_BASIC_RSVD_56_63_MASK                            UINT64_C(0xfe00000000000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_BASIC_, UINT64_C(0), UINT64_MAX,
                            (VMCS_ID, RSVD_32, VMCS_SIZE, RSVD_45_47, PHYSADDR_WIDTH, DUAL_MON, VMCS_MEM_TYPE,
                             VMCS_INS_OUTS, TRUE_CTLS, XCPT_ERRCODE, RSVD_56_63));
/** @} */


/** @name VMX MSR - Miscellaneous data.
 * @{
 */
/** Whether VM-exit stores EFER.LMA into the "IA32e mode guest" field. */
#define VMX_MISC_EXIT_SAVE_EFER_LMA                             RT_BIT(5)
/** Whether Intel PT is supported in VMX operation. */
#define VMX_MISC_INTEL_PT                                       RT_BIT(14)
/** Whether VMWRITE to any valid VMCS field incl. read-only fields, otherwise
 * VMWRITE cannot modify read-only VM-exit information fields. */
#define VMX_MISC_VMWRITE_ALL                                    RT_BIT(29)
/** Whether VM-entry can inject software interrupts, INT1 (ICEBP) with 0-length
 *  instructions. */
#define VMX_MISC_ENTRY_INJECT_SOFT_INT                          RT_BIT(30)
/** Maximum number of MSRs in the auto-load/store MSR areas, (n+1) * 512. */
#define VMX_MISC_MAX_MSRS(a_MiscMsr)                            (512 * (RT_BF_GET((a_MiscMsr), VMX_BF_MISC_MAX_MSRS) + 1))
/** Maximum CR3-target count supported by the CPU. */
#define VMX_MISC_CR3_TARGET_COUNT(a_MiscMsr)                    (((a) >> 16) & 0xff)

/** Bit fields for MSR_IA32_VMX_MISC. */
/** Relationship between the preemption timer and tsc. */
#define VMX_BF_MISC_PREEMPT_TIMER_TSC_SHIFT                     0
#define VMX_BF_MISC_PREEMPT_TIMER_TSC_MASK                      UINT64_C(0x000000000000001f)
/** Whether VM-exit stores EFER.LMA into the "IA32e mode guest" field. */
#define VMX_BF_MISC_EXIT_SAVE_EFER_LMA_SHIFT                    5
#define VMX_BF_MISC_EXIT_SAVE_EFER_LMA_MASK                     UINT64_C(0x0000000000000020)
/** Activity states supported by the implementation. */
#define VMX_BF_MISC_ACTIVITY_STATES_SHIFT                       6
#define VMX_BF_MISC_ACTIVITY_STATES_MASK                        UINT64_C(0x00000000000001c0)
/** Bits 9:13 is reserved and RAZ. */
#define VMX_BF_MISC_RSVD_9_13_SHIFT                             9
#define VMX_BF_MISC_RSVD_9_13_MASK                              UINT64_C(0x0000000000003e00)
/** Whether Intel PT (Processor Trace) can be used in VMX operation. */
#define VMX_BF_MISC_INTEL_PT_SHIFT                              14
#define VMX_BF_MISC_INTEL_PT_MASK                               UINT64_C(0x0000000000004000)
/** Whether RDMSR can be used to read IA32_SMBASE MSR in SMM. */
#define VMX_BF_MISC_SMM_READ_SMBASE_MSR_SHIFT                   15
#define VMX_BF_MISC_SMM_READ_SMBASE_MSR_MASK                    UINT64_C(0x0000000000008000)
/** Number of CR3 target values supported by the processor. (0-256) */
#define VMX_BF_MISC_CR3_TARGET_SHIFT                            16
#define VMX_BF_MISC_CR3_TARGET_MASK                             UINT64_C(0x0000000001ff0000)
/** Maximum number of MSRs in the VMCS. */
#define VMX_BF_MISC_MAX_MSRS_SHIFT                              25
#define VMX_BF_MISC_MAX_MSRS_MASK                               UINT64_C(0x000000000e000000)
/** Whether IA32_SMM_MONITOR_CTL MSR can be modified to allow VMXOFF to block
 *  SMIs. */
#define VMX_BF_MISC_VMXOFF_BLOCK_SMI_SHIFT                      28
#define VMX_BF_MISC_VMXOFF_BLOCK_SMI_MASK                       UINT64_C(0x0000000010000000)
/** Whether VMWRITE to any valid VMCS field incl. read-only fields, otherwise
 * VMWRITE cannot modify read-only VM-exit information fields. */
#define VMX_BF_MISC_VMWRITE_ALL_SHIFT                           29
#define VMX_BF_MISC_VMWRITE_ALL_MASK                            UINT64_C(0x0000000020000000)
/** Whether VM-entry can inject software interrupts, INT1 (ICEBP) with 0-length
 *  instructions. */
#define VMX_BF_MISC_ENTRY_INJECT_SOFT_INT_SHIFT                 30
#define VMX_BF_MISC_ENTRY_INJECT_SOFT_INT_MASK                  UINT64_C(0x0000000040000000)
/** Bit 31 is reserved and RAZ. */
#define VMX_BF_MISC_RSVD_31_SHIFT                               31
#define VMX_BF_MISC_RSVD_31_MASK                                UINT64_C(0x0000000080000000)
/** 32-bit MSEG revision ID used by the processor. */
#define VMX_BF_MISC_MSEG_ID_SHIFT                               32
#define VMX_BF_MISC_MSEG_ID_MASK                                UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_MISC_, UINT64_C(0), UINT64_MAX,
                            (PREEMPT_TIMER_TSC, EXIT_SAVE_EFER_LMA, ACTIVITY_STATES, RSVD_9_13, INTEL_PT, SMM_READ_SMBASE_MSR,
                             CR3_TARGET, MAX_MSRS, VMXOFF_BLOCK_SMI, VMWRITE_ALL, ENTRY_INJECT_SOFT_INT, RSVD_31, MSEG_ID));
/** @} */

/** @name VMX MSR - VMCS enumeration.
 * Bit fields for MSR_IA32_VMX_VMCS_ENUM.
 * @{
 */
/** Bit 0 is reserved and RAZ. */
#define VMX_BF_VMCS_ENUM_RSVD_0_SHIFT                           0
#define VMX_BF_VMCS_ENUM_RSVD_0_MASK                            UINT64_C(0x0000000000000001)
/** Highest index value used in VMCS field encoding. */
#define VMX_BF_VMCS_ENUM_HIGHEST_IDX_SHIFT                      1
#define VMX_BF_VMCS_ENUM_HIGHEST_IDX_MASK                       UINT64_C(0x00000000000003fe)
/** Bit 10:63 is reserved and RAZ. */
#define VMX_BF_VMCS_ENUM_RSVD_10_63_SHIFT                       10
#define VMX_BF_VMCS_ENUM_RSVD_10_63_MASK                        UINT64_C(0xfffffffffffffc00)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_VMCS_ENUM_, UINT64_C(0), UINT64_MAX,
                            (RSVD_0, HIGHEST_IDX, RSVD_10_63));
/** @} */


/** @name VMX MSR - VM Functions.
 * Bit fields for MSR_IA32_VMX_VMFUNC.
 * @{
 */
/** EPTP-switching function changes the value of the EPTP to one chosen from the EPTP list. */
#define VMX_BF_VMFUNC_EPTP_SWITCHING_SHIFT                      0
#define VMX_BF_VMFUNC_EPTP_SWITCHING_MASK                       UINT64_C(0x0000000000000001)
/** Bits 1:63 are reserved and RAZ. */
#define VMX_BF_VMFUNC_RSVD_1_63_SHIFT                           1
#define VMX_BF_VMFUNC_RSVD_1_63_MASK                            UINT64_C(0xfffffffffffffffe)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_VMFUNC_, UINT64_C(0), UINT64_MAX,
                            (EPTP_SWITCHING, RSVD_1_63));
/** @} */


/** @name VMX MSR - EPT/VPID capabilities.
 * @{
 */
/** Supports execute-only translations by EPT. */
#define MSR_IA32_VMX_EPT_VPID_CAP_RWX_X_ONLY                    RT_BIT_64(0)
/** Supports page-walk length of 4. */
#define MSR_IA32_VMX_EPT_VPID_CAP_PAGE_WALK_LENGTH_4            RT_BIT_64(6)
/** Supports page-walk length of 5. */
#define MSR_IA32_VMX_EPT_VPID_CAP_PAGE_WALK_LENGTH_5            RT_BIT_64(7)
/** Supports EPT paging-structure memory type to be uncacheable. */
#define MSR_IA32_VMX_EPT_VPID_CAP_MEMTYPE_UC                    RT_BIT_64(8)
/** Supports EPT paging structure memory type to be write-back. */
#define MSR_IA32_VMX_EPT_VPID_CAP_MEMTYPE_WB                    RT_BIT_64(14)
/** Supports EPT PDE to map a 2 MB page. */
#define MSR_IA32_VMX_EPT_VPID_CAP_PDE_2M                        RT_BIT_64(16)
/** Supports EPT PDPTE to map a 1 GB page. */
#define MSR_IA32_VMX_EPT_VPID_CAP_PDPTE_1G                      RT_BIT_64(17)
/** Supports INVEPT instruction. */
#define MSR_IA32_VMX_EPT_VPID_CAP_INVEPT                        RT_BIT_64(20)
/** Supports accessed and dirty flags for EPT. */
#define MSR_IA32_VMX_EPT_VPID_CAP_ACCESS_DIRTY                  RT_BIT_64(21)
/** Supports advanced VM-exit info. for EPT violations. */
#define MSR_IA32_VMX_EPT_VPID_CAP_ADVEXITINFO_EPT_VIOLATION     RT_BIT_64(22)
/** Supports supervisor shadow-stack control. */
#define MSR_IA32_VMX_EPT_VPID_CAP_SUPER_SHW_STACK               RT_BIT_64(23)
/** Supports single-context INVEPT type. */
#define MSR_IA32_VMX_EPT_VPID_CAP_INVEPT_SINGLE_CONTEXT         RT_BIT_64(25)
/** Supports all-context INVEPT type. */
#define MSR_IA32_VMX_EPT_VPID_CAP_INVEPT_ALL_CONTEXTS           RT_BIT_64(26)
/** Supports INVVPID instruction. */
#define MSR_IA32_VMX_EPT_VPID_CAP_INVVPID                       RT_BIT_64(32)
/** Supports individual-address INVVPID type. */
#define MSR_IA32_VMX_EPT_VPID_CAP_INVVPID_INDIV_ADDR            RT_BIT_64(40)
/** Supports single-context INVVPID type. */
#define MSR_IA32_VMX_EPT_VPID_CAP_INVVPID_SINGLE_CONTEXT        RT_BIT_64(41)
/** Supports all-context INVVPID type. */
#define MSR_IA32_VMX_EPT_VPID_CAP_INVVPID_ALL_CONTEXTS          RT_BIT_64(42)
/** Supports singe-context-retaining-globals INVVPID type. */
#define MSR_IA32_VMX_EPT_VPID_CAP_INVVPID_SINGLE_CONTEXT_RETAIN_GLOBALS  RT_BIT_64(43)

/** Bit fields for MSR_IA32_VMX_EPT_VPID_CAP. */
#define VMX_BF_EPT_VPID_CAP_EXEC_ONLY_SHIFT                     0
#define VMX_BF_EPT_VPID_CAP_EXEC_ONLY_MASK                      UINT64_C(0x0000000000000001)
#define VMX_BF_EPT_VPID_CAP_RSVD_1_5_SHIFT                      1
#define VMX_BF_EPT_VPID_CAP_RSVD_1_5_MASK                       UINT64_C(0x000000000000003e)
#define VMX_BF_EPT_VPID_CAP_PAGE_WALK_LENGTH_4_SHIFT            6
#define VMX_BF_EPT_VPID_CAP_PAGE_WALK_LENGTH_4_MASK             UINT64_C(0x0000000000000040)
#define VMX_BF_EPT_VPID_CAP_RSVD_7_SHIFT                        7
#define VMX_BF_EPT_VPID_CAP_RSVD_7_MASK                         UINT64_C(0x0000000000000080)
#define VMX_BF_EPT_VPID_CAP_MEMTYPE_UC_SHIFT                    8
#define VMX_BF_EPT_VPID_CAP_MEMTYPE_UC_MASK                     UINT64_C(0x0000000000000100)
#define VMX_BF_EPT_VPID_CAP_RSVD_9_13_SHIFT                     9
#define VMX_BF_EPT_VPID_CAP_RSVD_9_13_MASK                      UINT64_C(0x0000000000003e00)
#define VMX_BF_EPT_VPID_CAP_MEMTYPE_WB_SHIFT                    14
#define VMX_BF_EPT_VPID_CAP_MEMTYPE_WB_MASK                     UINT64_C(0x0000000000004000)
#define VMX_BF_EPT_VPID_CAP_RSVD_15_SHIFT                       15
#define VMX_BF_EPT_VPID_CAP_RSVD_15_MASK                        UINT64_C(0x0000000000008000)
#define VMX_BF_EPT_VPID_CAP_PDE_2M_SHIFT                        16
#define VMX_BF_EPT_VPID_CAP_PDE_2M_MASK                         UINT64_C(0x0000000000010000)
#define VMX_BF_EPT_VPID_CAP_PDPTE_1G_SHIFT                      17
#define VMX_BF_EPT_VPID_CAP_PDPTE_1G_MASK                       UINT64_C(0x0000000000020000)
#define VMX_BF_EPT_VPID_CAP_RSVD_18_19_SHIFT                    18
#define VMX_BF_EPT_VPID_CAP_RSVD_18_19_MASK                     UINT64_C(0x00000000000c0000)
#define VMX_BF_EPT_VPID_CAP_INVEPT_SHIFT                        20
#define VMX_BF_EPT_VPID_CAP_INVEPT_MASK                         UINT64_C(0x0000000000100000)
#define VMX_BF_EPT_VPID_CAP_ACCESS_DIRTY_SHIFT                  21
#define VMX_BF_EPT_VPID_CAP_ACCESS_DIRTY_MASK                   UINT64_C(0x0000000000200000)
#define VMX_BF_EPT_VPID_CAP_ADVEXITINFO_EPT_VIOLATION_SHIFT     22
#define VMX_BF_EPT_VPID_CAP_ADVEXITINFO_EPT_VIOLATION_MASK      UINT64_C(0x0000000000400000)
#define VMX_BF_EPT_VPID_CAP_SUPER_SHW_STACK_SHIFT               23
#define VMX_BF_EPT_VPID_CAP_SUPER_SHW_STACK_MASK                UINT64_C(0x0000000000800000)
#define VMX_BF_EPT_VPID_CAP_RSVD_24_SHIFT                       24
#define VMX_BF_EPT_VPID_CAP_RSVD_24_MASK                        UINT64_C(0x0000000001000000)
#define VMX_BF_EPT_VPID_CAP_INVEPT_SINGLE_CTX_SHIFT             25
#define VMX_BF_EPT_VPID_CAP_INVEPT_SINGLE_CTX_MASK              UINT64_C(0x0000000002000000)
#define VMX_BF_EPT_VPID_CAP_INVEPT_ALL_CTX_SHIFT                26
#define VMX_BF_EPT_VPID_CAP_INVEPT_ALL_CTX_MASK                 UINT64_C(0x0000000004000000)
#define VMX_BF_EPT_VPID_CAP_RSVD_27_31_SHIFT                    27
#define VMX_BF_EPT_VPID_CAP_RSVD_27_31_MASK                     UINT64_C(0x00000000f8000000)
#define VMX_BF_EPT_VPID_CAP_INVVPID_SHIFT                       32
#define VMX_BF_EPT_VPID_CAP_INVVPID_MASK                        UINT64_C(0x0000000100000000)
#define VMX_BF_EPT_VPID_CAP_RSVD_33_39_SHIFT                    33
#define VMX_BF_EPT_VPID_CAP_RSVD_33_39_MASK                     UINT64_C(0x000000fe00000000)
#define VMX_BF_EPT_VPID_CAP_INVVPID_INDIV_ADDR_SHIFT            40
#define VMX_BF_EPT_VPID_CAP_INVVPID_INDIV_ADDR_MASK             UINT64_C(0x0000010000000000)
#define VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX_SHIFT            41
#define VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX_MASK             UINT64_C(0x0000020000000000)
#define VMX_BF_EPT_VPID_CAP_INVVPID_ALL_CTX_SHIFT               42
#define VMX_BF_EPT_VPID_CAP_INVVPID_ALL_CTX_MASK                UINT64_C(0x0000040000000000)
#define VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX_RETAIN_GLOBALS_SHIFT 43
#define VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX_RETAIN_GLOBALS_MASK  UINT64_C(0x0000080000000000)
#define VMX_BF_EPT_VPID_CAP_RSVD_44_63_SHIFT                    44
#define VMX_BF_EPT_VPID_CAP_RSVD_44_63_MASK                     UINT64_C(0xfffff00000000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EPT_VPID_CAP_, UINT64_C(0), UINT64_MAX,
                            (EXEC_ONLY, RSVD_1_5, PAGE_WALK_LENGTH_4, RSVD_7, MEMTYPE_UC, RSVD_9_13, MEMTYPE_WB, RSVD_15, PDE_2M,
                             PDPTE_1G, RSVD_18_19, INVEPT, ACCESS_DIRTY, ADVEXITINFO_EPT_VIOLATION, SUPER_SHW_STACK, RSVD_24,
                             INVEPT_SINGLE_CTX, INVEPT_ALL_CTX, RSVD_27_31, INVVPID, RSVD_33_39, INVVPID_INDIV_ADDR,
                             INVVPID_SINGLE_CTX, INVVPID_ALL_CTX, INVVPID_SINGLE_CTX_RETAIN_GLOBALS, RSVD_44_63));
/** @} */


/** @name Extended Page Table Pointer (EPTP)
 * In accordance with the VT-x spec.
 * See Intel spec. 23.6.11 "Extended-Page-Table Pointer (EPTP)".
 * @{
 */
/** EPTP memory type: Uncachable. */
#define VMX_EPTP_MEMTYPE_UC                                     0
/** EPTP memory type: Write Back. */
#define VMX_EPTP_MEMTYPE_WB                                     6
/** Page-walk length for PML4 (4-level paging). */
#define VMX_EPTP_PAGE_WALK_LENGTH_4                             3

/** Bit fields for EPTP. */
#define VMX_BF_EPTP_MEMTYPE_SHIFT                               0
#define VMX_BF_EPTP_MEMTYPE_MASK                                UINT64_C(0x0000000000000007)
#define VMX_BF_EPTP_PAGE_WALK_LENGTH_SHIFT                      3
#define VMX_BF_EPTP_PAGE_WALK_LENGTH_MASK                       UINT64_C(0x0000000000000038)
#define VMX_BF_EPTP_ACCESS_DIRTY_SHIFT                          6
#define VMX_BF_EPTP_ACCESS_DIRTY_MASK                           UINT64_C(0x0000000000000040)
#define VMX_BF_EPTP_SUPER_SHW_STACK_SHIFT                       7
#define VMX_BF_EPTP_SUPER_SHW_STACK_MASK                        UINT64_C(0x0000000000000080)
#define VMX_BF_EPTP_RSVD_8_11_SHIFT                             8
#define VMX_BF_EPTP_RSVD_8_11_MASK                              UINT64_C(0x0000000000000f00)
#define VMX_BF_EPTP_PML4_TABLE_ADDR_SHIFT                       12
#define VMX_BF_EPTP_PML4_TABLE_ADDR_MASK                        UINT64_C(0xfffffffffffff000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EPTP_, UINT64_C(0), UINT64_MAX,
                            (MEMTYPE, PAGE_WALK_LENGTH, ACCESS_DIRTY, SUPER_SHW_STACK, RSVD_8_11, PML4_TABLE_ADDR));

/* Mask of valid EPTP bits sans physically non-addressable bits. */
#define VMX_EPTP_VALID_MASK                                     (  VMX_BF_EPTP_MEMTYPE_MASK          \
                                                                 | VMX_BF_EPTP_PAGE_WALK_LENGTH_MASK \
                                                                 | VMX_BF_EPTP_ACCESS_DIRTY_MASK     \
                                                                 | VMX_BF_EPTP_SUPER_SHW_STACK_MASK  \
                                                                 | VMX_BF_EPTP_PML4_TABLE_ADDR_MASK)
/** @} */


/** @name VMCS fields and encoding.
 *
 *  When adding a new field:
 *    - Always add it to g_aVmcsFields.
 *    - Consider if it needs to be added to VMXVVMCS.
 * @{
 */
/** 16-bit control fields.  */
#define VMX_VMCS16_VPID                                         0x0000
#define VMX_VMCS16_POSTED_INT_NOTIFY_VECTOR                     0x0002
#define VMX_VMCS16_EPTP_INDEX                                   0x0004
#define VMX_VMCS16_HLAT_PREFIX_SIZE                             0x0006
#define VMX_VMCS16_LAST_PID_PTR_INDEX                           0x0008

/** 16-bit guest-state fields.  */
#define VMX_VMCS16_GUEST_ES_SEL                                 0x0800
#define VMX_VMCS16_GUEST_CS_SEL                                 0x0802
#define VMX_VMCS16_GUEST_SS_SEL                                 0x0804
#define VMX_VMCS16_GUEST_DS_SEL                                 0x0806
#define VMX_VMCS16_GUEST_FS_SEL                                 0x0808
#define VMX_VMCS16_GUEST_GS_SEL                                 0x080a
#define VMX_VMCS16_GUEST_LDTR_SEL                               0x080c
#define VMX_VMCS16_GUEST_TR_SEL                                 0x080e
#define VMX_VMCS16_GUEST_INTR_STATUS                            0x0810
#define VMX_VMCS16_GUEST_PML_INDEX                              0x0812
#define VMX_VMCS16_GUEST_UINV                                   0x0814

/** 16-bits host-state fields.  */
#define VMX_VMCS16_HOST_ES_SEL                                  0x0c00
#define VMX_VMCS16_HOST_CS_SEL                                  0x0c02
#define VMX_VMCS16_HOST_SS_SEL                                  0x0c04
#define VMX_VMCS16_HOST_DS_SEL                                  0x0c06
#define VMX_VMCS16_HOST_FS_SEL                                  0x0c08
#define VMX_VMCS16_HOST_GS_SEL                                  0x0c0a
#define VMX_VMCS16_HOST_TR_SEL                                  0x0c0c

/** 64-bit control fields. */
#define VMX_VMCS64_CTRL_IO_BITMAP_A_FULL                        0x2000
#define VMX_VMCS64_CTRL_IO_BITMAP_A_HIGH                        0x2001
#define VMX_VMCS64_CTRL_IO_BITMAP_B_FULL                        0x2002
#define VMX_VMCS64_CTRL_IO_BITMAP_B_HIGH                        0x2003
#define VMX_VMCS64_CTRL_MSR_BITMAP_FULL                         0x2004
#define VMX_VMCS64_CTRL_MSR_BITMAP_HIGH                         0x2005
#define VMX_VMCS64_CTRL_EXIT_MSR_STORE_FULL                     0x2006
#define VMX_VMCS64_CTRL_EXIT_MSR_STORE_HIGH                     0x2007
#define VMX_VMCS64_CTRL_EXIT_MSR_LOAD_FULL                      0x2008
#define VMX_VMCS64_CTRL_EXIT_MSR_LOAD_HIGH                      0x2009
#define VMX_VMCS64_CTRL_ENTRY_MSR_LOAD_FULL                     0x200a
#define VMX_VMCS64_CTRL_ENTRY_MSR_LOAD_HIGH                     0x200b
#define VMX_VMCS64_CTRL_EXEC_VMCS_PTR_FULL                      0x200c
#define VMX_VMCS64_CTRL_EXEC_VMCS_PTR_HIGH                      0x200d
#define VMX_VMCS64_CTRL_EXEC_PML_ADDR_FULL                      0x200e
#define VMX_VMCS64_CTRL_EXEC_PML_ADDR_HIGH                      0x200f
#define VMX_VMCS64_CTRL_TSC_OFFSET_FULL                         0x2010
#define VMX_VMCS64_CTRL_TSC_OFFSET_HIGH                         0x2011
#define VMX_VMCS64_CTRL_VIRT_APIC_PAGEADDR_FULL                 0x2012
#define VMX_VMCS64_CTRL_VIRT_APIC_PAGEADDR_HIGH                 0x2013
#define VMX_VMCS64_CTRL_APIC_ACCESSADDR_FULL                    0x2014
#define VMX_VMCS64_CTRL_APIC_ACCESSADDR_HIGH                    0x2015
#define VMX_VMCS64_CTRL_POSTED_INTR_DESC_FULL                   0x2016
#define VMX_VMCS64_CTRL_POSTED_INTR_DESC_HIGH                   0x2017
#define VMX_VMCS64_CTRL_VMFUNC_CTRLS_FULL                       0x2018
#define VMX_VMCS64_CTRL_VMFUNC_CTRLS_HIGH                       0x2019
#define VMX_VMCS64_CTRL_EPTP_FULL                               0x201a
#define VMX_VMCS64_CTRL_EPTP_HIGH                               0x201b
#define VMX_VMCS64_CTRL_EOI_BITMAP_0_FULL                       0x201c
#define VMX_VMCS64_CTRL_EOI_BITMAP_0_HIGH                       0x201d
#define VMX_VMCS64_CTRL_EOI_BITMAP_1_FULL                       0x201e
#define VMX_VMCS64_CTRL_EOI_BITMAP_1_HIGH                       0x201f
#define VMX_VMCS64_CTRL_EOI_BITMAP_2_FULL                       0x2020
#define VMX_VMCS64_CTRL_EOI_BITMAP_2_HIGH                       0x2021
#define VMX_VMCS64_CTRL_EOI_BITMAP_3_FULL                       0x2022
#define VMX_VMCS64_CTRL_EOI_BITMAP_3_HIGH                       0x2023
#define VMX_VMCS64_CTRL_EPTP_LIST_FULL                          0x2024
#define VMX_VMCS64_CTRL_EPTP_LIST_HIGH                          0x2025
#define VMX_VMCS64_CTRL_VMREAD_BITMAP_FULL                      0x2026
#define VMX_VMCS64_CTRL_VMREAD_BITMAP_HIGH                      0x2027
#define VMX_VMCS64_CTRL_VMWRITE_BITMAP_FULL                     0x2028
#define VMX_VMCS64_CTRL_VMWRITE_BITMAP_HIGH                     0x2029
#define VMX_VMCS64_CTRL_VE_XCPT_INFO_ADDR_FULL                  0x202a
#define VMX_VMCS64_CTRL_VE_XCPT_INFO_ADDR_HIGH                  0x202b
#define VMX_VMCS64_CTRL_XSS_EXITING_BITMAP_FULL                 0x202c
#define VMX_VMCS64_CTRL_XSS_EXITING_BITMAP_HIGH                 0x202d
#define VMX_VMCS64_CTRL_ENCLS_EXITING_BITMAP_FULL               0x202e
#define VMX_VMCS64_CTRL_ENCLS_EXITING_BITMAP_HIGH               0x202f
#define VMX_VMCS64_CTRL_SPPTP_FULL                              0x2030
#define VMX_VMCS64_CTRL_SPPTP_HIGH                              0x2031
#define VMX_VMCS64_CTRL_TSC_MULTIPLIER_FULL                     0x2032
#define VMX_VMCS64_CTRL_TSC_MULTIPLIER_HIGH                     0x2033
#define VMX_VMCS64_CTRL_PROC_EXEC3_FULL                         0x2034
#define VMX_VMCS64_CTRL_PROC_EXEC3_HIGH                         0x2035
#define VMX_VMCS64_CTRL_ENCLV_EXITING_BITMAP_FULL               0x2036
#define VMX_VMCS64_CTRL_ENCLV_EXITING_BITMAP_HIGH               0x2037
#define VMX_VMCS64_CTRL_PCONFIG_EXITING_BITMAP_FULL             0x203e
#define VMX_VMCS64_CTRL_PCONFIG_EXITING_BITMAP_HIGH             0x203f
#define VMX_VMCS64_CTRL_HLAT_PTR_FULL                           0x2040
#define VMX_VMCS64_CTRL_HLAT_PTR_HIGH                           0x2041
#define VMX_VMCS64_CTRL_EXIT2_FULL                              0x2044
#define VMX_VMCS64_CTRL_EXIT2_HIGH                              0x2045
#define VMX_VMCS64_CTRL_SPEC_CTRL_MASK_FULL                     0x204a
#define VMX_VMCS64_CTRL_SPEC_CTRL_MASK_HIGH                     0x204b
#define VMX_VMCS64_CTRL_SPEC_CTRL_SHADOW_FULL                   0x204c
#define VMX_VMCS64_CTRL_SPEC_CTRL_SHADOW_HIGH                   0x204d

/** 64-bit read-only data fields.  */
#define VMX_VMCS64_RO_GUEST_PHYS_ADDR_FULL                      0x2400
#define VMX_VMCS64_RO_GUEST_PHYS_ADDR_HIGH                      0x2401

/** 64-bit guest-state fields.  */
#define VMX_VMCS64_GUEST_VMCS_LINK_PTR_FULL                     0x2800
#define VMX_VMCS64_GUEST_VMCS_LINK_PTR_HIGH                     0x2801
#define VMX_VMCS64_GUEST_DEBUGCTL_FULL                          0x2802
#define VMX_VMCS64_GUEST_DEBUGCTL_HIGH                          0x2803
#define VMX_VMCS64_GUEST_PAT_FULL                               0x2804
#define VMX_VMCS64_GUEST_PAT_HIGH                               0x2805
#define VMX_VMCS64_GUEST_EFER_FULL                              0x2806
#define VMX_VMCS64_GUEST_EFER_HIGH                              0x2807
#define VMX_VMCS64_GUEST_PERF_GLOBAL_CTRL_FULL                  0x2808
#define VMX_VMCS64_GUEST_PERF_GLOBAL_CTRL_HIGH                  0x2809
#define VMX_VMCS64_GUEST_PDPTE0_FULL                            0x280a
#define VMX_VMCS64_GUEST_PDPTE0_HIGH                            0x280b
#define VMX_VMCS64_GUEST_PDPTE1_FULL                            0x280c
#define VMX_VMCS64_GUEST_PDPTE1_HIGH                            0x280d
#define VMX_VMCS64_GUEST_PDPTE2_FULL                            0x280e
#define VMX_VMCS64_GUEST_PDPTE2_HIGH                            0x280f
#define VMX_VMCS64_GUEST_PDPTE3_FULL                            0x2810
#define VMX_VMCS64_GUEST_PDPTE3_HIGH                            0x2811
#define VMX_VMCS64_GUEST_BNDCFGS_FULL                           0x2812
#define VMX_VMCS64_GUEST_BNDCFGS_HIGH                           0x2813
#define VMX_VMCS64_GUEST_RTIT_CTL_FULL                          0x2814
#define VMX_VMCS64_GUEST_RTIT_CTL_HIGH                          0x2815
#define VMX_VMCS64_GUEST_PKRS_FULL                              0x2818
#define VMX_VMCS64_GUEST_PKRS_HIGH                              0x2819

/** 64-bit host-state fields.  */
#define VMX_VMCS64_HOST_PAT_FULL                                0x2c00
#define VMX_VMCS64_HOST_PAT_HIGH                                0x2c01
#define VMX_VMCS64_HOST_EFER_FULL                               0x2c02
#define VMX_VMCS64_HOST_EFER_HIGH                               0x2c03
#define VMX_VMCS64_HOST_PERF_GLOBAL_CTRL_FULL                   0x2c04
#define VMX_VMCS64_HOST_PERF_GLOBAL_CTRL_HIGH                   0x2c05
#define VMX_VMCS64_HOST_PKRS_FULL                               0x2c06
#define VMX_VMCS64_HOST_PKRS_HIGH                               0x2c07

/** 32-bit control fields.  */
#define VMX_VMCS32_CTRL_PIN_EXEC                                0x4000
#define VMX_VMCS32_CTRL_PROC_EXEC                               0x4002
#define VMX_VMCS32_CTRL_EXCEPTION_BITMAP                        0x4004
#define VMX_VMCS32_CTRL_PAGEFAULT_ERROR_MASK                    0x4006
#define VMX_VMCS32_CTRL_PAGEFAULT_ERROR_MATCH                   0x4008
#define VMX_VMCS32_CTRL_CR3_TARGET_COUNT                        0x400a
#define VMX_VMCS32_CTRL_EXIT                                    0x400c
#define VMX_VMCS32_CTRL_EXIT_MSR_STORE_COUNT                    0x400e
#define VMX_VMCS32_CTRL_EXIT_MSR_LOAD_COUNT                     0x4010
#define VMX_VMCS32_CTRL_ENTRY                                   0x4012
#define VMX_VMCS32_CTRL_ENTRY_MSR_LOAD_COUNT                    0x4014
#define VMX_VMCS32_CTRL_ENTRY_INTERRUPTION_INFO                 0x4016
#define VMX_VMCS32_CTRL_ENTRY_EXCEPTION_ERRCODE                 0x4018
#define VMX_VMCS32_CTRL_ENTRY_INSTR_LENGTH                      0x401a
#define VMX_VMCS32_CTRL_TPR_THRESHOLD                           0x401c
#define VMX_VMCS32_CTRL_PROC_EXEC2                              0x401e
#define VMX_VMCS32_CTRL_PLE_GAP                                 0x4020
#define VMX_VMCS32_CTRL_PLE_WINDOW                              0x4022
#define VMX_VMCS32_CTRL_INSTR_TIMEOUT                           0x4024

/** 32-bits read-only fields. */
#define VMX_VMCS32_RO_VM_INSTR_ERROR                            0x4400
#define VMX_VMCS32_RO_EXIT_REASON                               0x4402
#define VMX_VMCS32_RO_EXIT_INTERRUPTION_INFO                    0x4404
#define VMX_VMCS32_RO_EXIT_INTERRUPTION_ERROR_CODE              0x4406
#define VMX_VMCS32_RO_IDT_VECTORING_INFO                        0x4408
#define VMX_VMCS32_RO_IDT_VECTORING_ERROR_CODE                  0x440a
#define VMX_VMCS32_RO_EXIT_INSTR_LENGTH                         0x440c
#define VMX_VMCS32_RO_EXIT_INSTR_INFO                           0x440e

/** 32-bit guest-state fields. */
#define VMX_VMCS32_GUEST_ES_LIMIT                               0x4800
#define VMX_VMCS32_GUEST_CS_LIMIT                               0x4802
#define VMX_VMCS32_GUEST_SS_LIMIT                               0x4804
#define VMX_VMCS32_GUEST_DS_LIMIT                               0x4806
#define VMX_VMCS32_GUEST_FS_LIMIT                               0x4808
#define VMX_VMCS32_GUEST_GS_LIMIT                               0x480a
#define VMX_VMCS32_GUEST_LDTR_LIMIT                             0x480c
#define VMX_VMCS32_GUEST_TR_LIMIT                               0x480e
#define VMX_VMCS32_GUEST_GDTR_LIMIT                             0x4810
#define VMX_VMCS32_GUEST_IDTR_LIMIT                             0x4812
#define VMX_VMCS32_GUEST_ES_ACCESS_RIGHTS                       0x4814
#define VMX_VMCS32_GUEST_CS_ACCESS_RIGHTS                       0x4816
#define VMX_VMCS32_GUEST_SS_ACCESS_RIGHTS                       0x4818
#define VMX_VMCS32_GUEST_DS_ACCESS_RIGHTS                       0x481a
#define VMX_VMCS32_GUEST_FS_ACCESS_RIGHTS                       0x481c
#define VMX_VMCS32_GUEST_GS_ACCESS_RIGHTS                       0x481e
#define VMX_VMCS32_GUEST_LDTR_ACCESS_RIGHTS                     0x4820
#define VMX_VMCS32_GUEST_TR_ACCESS_RIGHTS                       0x4822
#define VMX_VMCS32_GUEST_INT_STATE                              0x4824
#define VMX_VMCS32_GUEST_ACTIVITY_STATE                         0x4826
#define VMX_VMCS32_GUEST_SMBASE                                 0x4828
#define VMX_VMCS32_GUEST_SYSENTER_CS                            0x482a
#define VMX_VMCS32_PREEMPT_TIMER_VALUE                          0x482e

/** 32-bit host-state fields. */
#define VMX_VMCS32_HOST_SYSENTER_CS                             0x4C00

/** Natural-width control fields.  */
#define VMX_VMCS_CTRL_CR0_MASK                                  0x6000
#define VMX_VMCS_CTRL_CR4_MASK                                  0x6002
#define VMX_VMCS_CTRL_CR0_READ_SHADOW                           0x6004
#define VMX_VMCS_CTRL_CR4_READ_SHADOW                           0x6006
#define VMX_VMCS_CTRL_CR3_TARGET_VAL0                           0x6008
#define VMX_VMCS_CTRL_CR3_TARGET_VAL1                           0x600a
#define VMX_VMCS_CTRL_CR3_TARGET_VAL2                           0x600c
#define VMX_VMCS_CTRL_CR3_TARGET_VAL3                           0x600e

/** Natural-width read-only data fields. */
#define VMX_VMCS_RO_EXIT_QUALIFICATION                          0x6400
#define VMX_VMCS_RO_IO_RCX                                      0x6402
#define VMX_VMCS_RO_IO_RSI                                      0x6404
#define VMX_VMCS_RO_IO_RDI                                      0x6406
#define VMX_VMCS_RO_IO_RIP                                      0x6408
#define VMX_VMCS_RO_GUEST_LINEAR_ADDR                           0x640a

/** Natural-width guest-state fields. */
#define VMX_VMCS_GUEST_CR0                                      0x6800
#define VMX_VMCS_GUEST_CR3                                      0x6802
#define VMX_VMCS_GUEST_CR4                                      0x6804
#define VMX_VMCS_GUEST_ES_BASE                                  0x6806
#define VMX_VMCS_GUEST_CS_BASE                                  0x6808
#define VMX_VMCS_GUEST_SS_BASE                                  0x680a
#define VMX_VMCS_GUEST_DS_BASE                                  0x680c
#define VMX_VMCS_GUEST_FS_BASE                                  0x680e
#define VMX_VMCS_GUEST_GS_BASE                                  0x6810
#define VMX_VMCS_GUEST_LDTR_BASE                                0x6812
#define VMX_VMCS_GUEST_TR_BASE                                  0x6814
#define VMX_VMCS_GUEST_GDTR_BASE                                0x6816
#define VMX_VMCS_GUEST_IDTR_BASE                                0x6818
#define VMX_VMCS_GUEST_DR7                                      0x681a
#define VMX_VMCS_GUEST_RSP                                      0x681c
#define VMX_VMCS_GUEST_RIP                                      0x681e
#define VMX_VMCS_GUEST_RFLAGS                                   0x6820
#define VMX_VMCS_GUEST_PENDING_DEBUG_XCPTS                      0x6822
#define VMX_VMCS_GUEST_SYSENTER_ESP                             0x6824
#define VMX_VMCS_GUEST_SYSENTER_EIP                             0x6826
#define VMX_VMCS_GUEST_S_CET                                    0x6828
#define VMX_VMCS_GUEST_SSP                                      0x682a
#define VMX_VMCS_GUEST_INTR_SSP_TABLE_ADDR                      0x682c

/** Natural-width host-state fields. */
#define VMX_VMCS_HOST_CR0                                       0x6c00
#define VMX_VMCS_HOST_CR3                                       0x6c02
#define VMX_VMCS_HOST_CR4                                       0x6c04
#define VMX_VMCS_HOST_FS_BASE                                   0x6c06
#define VMX_VMCS_HOST_GS_BASE                                   0x6c08
#define VMX_VMCS_HOST_TR_BASE                                   0x6c0a
#define VMX_VMCS_HOST_GDTR_BASE                                 0x6c0c
#define VMX_VMCS_HOST_IDTR_BASE                                 0x6c0e
#define VMX_VMCS_HOST_SYSENTER_ESP                              0x6c10
#define VMX_VMCS_HOST_SYSENTER_EIP                              0x6c12
#define VMX_VMCS_HOST_RSP                                       0x6c14
#define VMX_VMCS_HOST_RIP                                       0x6c16
#define VMX_VMCS_HOST_S_CET                                     0x6c18
#define VMX_VMCS_HOST_SSP                                       0x6c1a
#define VMX_VMCS_HOST_INTR_SSP_TABLE_ADDR                       0x6c1c

#define VMX_VMCS16_GUEST_SEG_SEL(a_iSegReg)                     (VMX_VMCS16_GUEST_ES_SEL           + (a_iSegReg) * 2)
#define VMX_VMCS_GUEST_SEG_BASE(a_iSegReg)                      (VMX_VMCS_GUEST_ES_BASE            + (a_iSegReg) * 2)
#define VMX_VMCS32_GUEST_SEG_LIMIT(a_iSegReg)                   (VMX_VMCS32_GUEST_ES_LIMIT         + (a_iSegReg) * 2)
#define VMX_VMCS32_GUEST_SEG_ACCESS_RIGHTS(a_iSegReg)           (VMX_VMCS32_GUEST_ES_ACCESS_RIGHTS + (a_iSegReg) * 2)

/**
 * VMCS field.
 * In accordance with the VT-x spec.
 */
typedef union
{
    struct
    {
        /** The access type; 0=full, 1=high of 64-bit fields. */
        uint32_t    fAccessType  : 1;
        /** The index. */
        uint32_t    u8Index      : 8;
        /** The type; 0=control, 1=VM-exit info, 2=guest-state, 3=host-state. */
        uint32_t    u2Type       : 2;
        /** Reserved (MBZ). */
        uint32_t    u1Reserved0  : 1;
        /** The width; 0=16-bit, 1=64-bit, 2=32-bit, 3=natural-width. */
        uint32_t    u2Width      : 2;
        /** Reserved (MBZ). */
        uint32_t    u18Reserved0 : 18;
    } n;

    /* The unsigned integer view. */
    uint32_t    u;
} VMXVMCSFIELD;
AssertCompileSize(VMXVMCSFIELD, 4);
/** Pointer to a VMCS field. */
typedef VMXVMCSFIELD *PVMXVMCSFIELD;
/** Pointer to a const VMCS field. */
typedef const VMXVMCSFIELD *PCVMXVMCSFIELD;

/** VMCS field: Mask of reserved bits (bits 63:15 MBZ), bit 12 is not included! */
#define VMX_VMCSFIELD_RSVD_MASK                                 UINT64_C(0xffffffffffff8000)

/** Bits fields for a VMCS field. */
#define VMX_BF_VMCSFIELD_ACCESS_TYPE_SHIFT                      0
#define VMX_BF_VMCSFIELD_ACCESS_TYPE_MASK                       UINT32_C(0x00000001)
#define VMX_BF_VMCSFIELD_INDEX_SHIFT                            1
#define VMX_BF_VMCSFIELD_INDEX_MASK                             UINT32_C(0x000003fe)
#define VMX_BF_VMCSFIELD_TYPE_SHIFT                             10
#define VMX_BF_VMCSFIELD_TYPE_MASK                              UINT32_C(0x00000c00)
#define VMX_BF_VMCSFIELD_RSVD_12_SHIFT                          12
#define VMX_BF_VMCSFIELD_RSVD_12_MASK                           UINT32_C(0x00001000)
#define VMX_BF_VMCSFIELD_WIDTH_SHIFT                            13
#define VMX_BF_VMCSFIELD_WIDTH_MASK                             UINT32_C(0x00006000)
#define VMX_BF_VMCSFIELD_RSVD_15_31_SHIFT                       15
#define VMX_BF_VMCSFIELD_RSVD_15_31_MASK                        UINT32_C(0xffff8000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_VMCSFIELD_, UINT32_C(0), UINT32_MAX,
                            (ACCESS_TYPE, INDEX, TYPE, RSVD_12, WIDTH, RSVD_15_31));

/**
 * VMCS field encoding: Access type.
 * In accordance with the VT-x spec.
 */
typedef enum
{
    VMXVMCSFIELDACCESS_FULL = 0,
    VMXVMCSFIELDACCESS_HIGH
} VMXVMCSFIELDACCESS;
AssertCompileSize(VMXVMCSFIELDACCESS, 4);
/** VMCS field encoding type: Full. */
#define VMX_VMCSFIELD_ACCESS_FULL                               0
/** VMCS field encoding type: High. */
#define VMX_VMCSFIELD_ACCESS_HIGH                               1

/**
 * VMCS field encoding: Type.
 * In accordance with the VT-x spec.
 */
typedef enum
{
    VMXVMCSFIELDTYPE_CONTROL = 0,
    VMXVMCSFIELDTYPE_VMEXIT_INFO,
    VMXVMCSFIELDTYPE_GUEST_STATE,
    VMXVMCSFIELDTYPE_HOST_STATE
} VMXVMCSFIELDTYPE;
AssertCompileSize(VMXVMCSFIELDTYPE, 4);
/** VMCS field encoding type: Control. */
#define VMX_VMCSFIELD_TYPE_CONTROL                              0
/** VMCS field encoding type: VM-exit information / read-only fields. */
#define VMX_VMCSFIELD_TYPE_VMEXIT_INFO                          1
/** VMCS field encoding type: Guest-state. */
#define VMX_VMCSFIELD_TYPE_GUEST_STATE                          2
/** VMCS field encoding type: Host-state. */
#define VMX_VMCSFIELD_TYPE_HOST_STATE                           3

/**
 * VMCS field encoding: Width.
 * In accordance with the VT-x spec.
 */
typedef enum
{
    VMXVMCSFIELDWIDTH_16BIT = 0,
    VMXVMCSFIELDWIDTH_64BIT,
    VMXVMCSFIELDWIDTH_32BIT,
    VMXVMCSFIELDWIDTH_NATURAL
} VMXVMCSFIELDWIDTH;
AssertCompileSize(VMXVMCSFIELDWIDTH, 4);
/** VMCS field encoding width: 16-bit. */
#define VMX_VMCSFIELD_WIDTH_16BIT                               0
/** VMCS field encoding width: 64-bit. */
#define VMX_VMCSFIELD_WIDTH_64BIT                               1
/** VMCS field encoding width: 32-bit. */
#define VMX_VMCSFIELD_WIDTH_32BIT                               2
/** VMCS field encoding width: Natural width. */
#define VMX_VMCSFIELD_WIDTH_NATURAL                             3
/** @} */


/** @name VM-entry instruction length.
 * @{ */
/** The maximum valid value for VM-entry instruction length while injecting a
 *  software interrupt, software exception or privileged software exception. */
#define VMX_ENTRY_INSTR_LEN_MAX                                 15
/** @} */


/** @name VM-entry register masks.
 * @{ */
/** CR0 bits ignored on VM-entry while loading guest CR0 (ET, CD, NW, bits 6:15,
 *  bit 17 and bits 19:28).
 *
 *  I don't know the Intel spec. excludes the high bits here while includes them in
 *  the corresponding VM-exit mask. Nonetheless, I'm including the high bits here
 *  (by making it identical to the VM-exit CR0 mask) since they are reserved anyway
 *  and to prevent omission of the high bits with hardware-assisted VMX execution.
 */
#define VMX_ENTRY_GUEST_CR0_IGNORE_MASK                         VMX_EXIT_HOST_CR0_IGNORE_MASK
/** DR7 bits set here are always cleared on VM-entry while loading guest DR7 (bit
 *  12, bits 14:15). */
#define VMX_ENTRY_GUEST_DR7_MBZ_MASK                            UINT64_C(0xd000)
/** DR7 bits set here are always set on VM-entry while loading guest DR7 (bit
 *  10). */
#define VMX_ENTRY_GUEST_DR7_MB1_MASK                            UINT64_C(0x400)
/** @} */


/** @name VM-exit register masks.
 * @{ */
/** CR0 bits ignored on VM-exit while loading host CR0 (ET, CD, NW, bits 6:15,
 *  bit 17, bits 19:28 and bits 32:63). */
#define VMX_EXIT_HOST_CR0_IGNORE_MASK                           UINT64_C(0xffffffff7ffaffd0)
/** @} */


/** @name Pin-based VM-execution controls.
 * @{
 */
/** External interrupt exiting. */
#define VMX_PIN_CTLS_EXT_INT_EXIT                               RT_BIT(0)
/** NMI exiting. */
#define VMX_PIN_CTLS_NMI_EXIT                                   RT_BIT(3)
/** Virtual NMIs. */
#define VMX_PIN_CTLS_VIRT_NMI                                   RT_BIT(5)
/** Activate VMX preemption timer. */
#define VMX_PIN_CTLS_PREEMPT_TIMER                              RT_BIT(6)
/** Process interrupts with the posted-interrupt notification vector. */
#define VMX_PIN_CTLS_POSTED_INT                                 RT_BIT(7)
/** Default1 class when true capability MSRs are not supported. */
#define VMX_PIN_CTLS_DEFAULT1                                   UINT32_C(0x00000016)

/** Bit fields for MSR_IA32_VMX_PINBASED_CTLS and Pin-based VM-execution
 *  controls field in the VMCS. */
#define VMX_BF_PIN_CTLS_EXT_INT_EXIT_SHIFT                      0
#define VMX_BF_PIN_CTLS_EXT_INT_EXIT_MASK                       UINT32_C(0x00000001)
#define VMX_BF_PIN_CTLS_RSVD_1_2_SHIFT                          1
#define VMX_BF_PIN_CTLS_RSVD_1_2_MASK                           UINT32_C(0x00000006)
#define VMX_BF_PIN_CTLS_NMI_EXIT_SHIFT                          3
#define VMX_BF_PIN_CTLS_NMI_EXIT_MASK                           UINT32_C(0x00000008)
#define VMX_BF_PIN_CTLS_RSVD_4_SHIFT                            4
#define VMX_BF_PIN_CTLS_RSVD_4_MASK                             UINT32_C(0x00000010)
#define VMX_BF_PIN_CTLS_VIRT_NMI_SHIFT                          5
#define VMX_BF_PIN_CTLS_VIRT_NMI_MASK                           UINT32_C(0x00000020)
#define VMX_BF_PIN_CTLS_PREEMPT_TIMER_SHIFT                     6
#define VMX_BF_PIN_CTLS_PREEMPT_TIMER_MASK                      UINT32_C(0x00000040)
#define VMX_BF_PIN_CTLS_POSTED_INT_SHIFT                        7
#define VMX_BF_PIN_CTLS_POSTED_INT_MASK                         UINT32_C(0x00000080)
#define VMX_BF_PIN_CTLS_RSVD_8_31_SHIFT                         8
#define VMX_BF_PIN_CTLS_RSVD_8_31_MASK                          UINT32_C(0xffffff00)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_PIN_CTLS_, UINT32_C(0), UINT32_MAX,
                            (EXT_INT_EXIT, RSVD_1_2, NMI_EXIT, RSVD_4, VIRT_NMI, PREEMPT_TIMER, POSTED_INT, RSVD_8_31));
/** @} */


/** @name Processor-based VM-execution controls.
 * @{
 */
/** VM-exit as soon as RFLAGS.IF=1 and no blocking is active. */
#define VMX_PROC_CTLS_INT_WINDOW_EXIT                           RT_BIT(2)
/** Use timestamp counter offset. */
#define VMX_PROC_CTLS_USE_TSC_OFFSETTING                        RT_BIT(3)
/** VM-exit when executing the HLT instruction. */
#define VMX_PROC_CTLS_HLT_EXIT                                  RT_BIT(7)
/** VM-exit when executing the INVLPG instruction. */
#define VMX_PROC_CTLS_INVLPG_EXIT                               RT_BIT(9)
/** VM-exit when executing the MWAIT instruction. */
#define VMX_PROC_CTLS_MWAIT_EXIT                                RT_BIT(10)
/** VM-exit when executing the RDPMC instruction. */
#define VMX_PROC_CTLS_RDPMC_EXIT                                RT_BIT(11)
/** VM-exit when executing the RDTSC/RDTSCP instruction. */
#define VMX_PROC_CTLS_RDTSC_EXIT                                RT_BIT(12)
/** VM-exit when executing the MOV to CR3 instruction. (forced to 1 on the
 *  'first' VT-x capable CPUs; this actually includes the newest Nehalem CPUs) */
#define VMX_PROC_CTLS_CR3_LOAD_EXIT                             RT_BIT(15)
/** VM-exit when executing the MOV from CR3 instruction. (forced to 1 on the
 *  'first' VT-x capable CPUs; this actually includes the newest Nehalem CPUs) */
#define VMX_PROC_CTLS_CR3_STORE_EXIT                            RT_BIT(16)
/** Whether the secondary processor based VM-execution controls are used. */
#define VMX_PROC_CTLS_USE_TERTIARY_CTLS                         RT_BIT(17)
/** VM-exit on CR8 loads. */
#define VMX_PROC_CTLS_CR8_LOAD_EXIT                             RT_BIT(19)
/** VM-exit on CR8 stores. */
#define VMX_PROC_CTLS_CR8_STORE_EXIT                            RT_BIT(20)
/** Use TPR shadow. */
#define VMX_PROC_CTLS_USE_TPR_SHADOW                            RT_BIT(21)
/** VM-exit when virtual NMI blocking is disabled. */
#define VMX_PROC_CTLS_NMI_WINDOW_EXIT                           RT_BIT(22)
/** VM-exit when executing a MOV DRx instruction. */
#define VMX_PROC_CTLS_MOV_DR_EXIT                               RT_BIT(23)
/** VM-exit when executing IO instructions. */
#define VMX_PROC_CTLS_UNCOND_IO_EXIT                            RT_BIT(24)
/** Use IO bitmaps. */
#define VMX_PROC_CTLS_USE_IO_BITMAPS                            RT_BIT(25)
/** Monitor trap flag. */
#define VMX_PROC_CTLS_MONITOR_TRAP_FLAG                         RT_BIT(27)
/** Use MSR bitmaps. */
#define VMX_PROC_CTLS_USE_MSR_BITMAPS                           RT_BIT(28)
/** VM-exit when executing the MONITOR instruction. */
#define VMX_PROC_CTLS_MONITOR_EXIT                              RT_BIT(29)
/** VM-exit when executing the PAUSE instruction. */
#define VMX_PROC_CTLS_PAUSE_EXIT                                RT_BIT(30)
/** Whether the secondary processor based VM-execution controls are used. */
#define VMX_PROC_CTLS_USE_SECONDARY_CTLS                        RT_BIT(31)
/** Default1 class when true-capability MSRs are not supported. */
#define VMX_PROC_CTLS_DEFAULT1                                  UINT32_C(0x0401e172)

/** Bit fields for MSR_IA32_VMX_PROCBASED_CTLS and Processor-based VM-execution
 *  controls field in the VMCS. */
#define VMX_BF_PROC_CTLS_RSVD_0_1_SHIFT                         0
#define VMX_BF_PROC_CTLS_RSVD_0_1_MASK                          UINT32_C(0x00000003)
#define VMX_BF_PROC_CTLS_INT_WINDOW_EXIT_SHIFT                  2
#define VMX_BF_PROC_CTLS_INT_WINDOW_EXIT_MASK                   UINT32_C(0x00000004)
#define VMX_BF_PROC_CTLS_USE_TSC_OFFSETTING_SHIFT               3
#define VMX_BF_PROC_CTLS_USE_TSC_OFFSETTING_MASK                UINT32_C(0x00000008)
#define VMX_BF_PROC_CTLS_RSVD_4_6_SHIFT                         4
#define VMX_BF_PROC_CTLS_RSVD_4_6_MASK                          UINT32_C(0x00000070)
#define VMX_BF_PROC_CTLS_HLT_EXIT_SHIFT                         7
#define VMX_BF_PROC_CTLS_HLT_EXIT_MASK                          UINT32_C(0x00000080)
#define VMX_BF_PROC_CTLS_RSVD_8_SHIFT                           8
#define VMX_BF_PROC_CTLS_RSVD_8_MASK                            UINT32_C(0x00000100)
#define VMX_BF_PROC_CTLS_INVLPG_EXIT_SHIFT                      9
#define VMX_BF_PROC_CTLS_INVLPG_EXIT_MASK                       UINT32_C(0x00000200)
#define VMX_BF_PROC_CTLS_MWAIT_EXIT_SHIFT                       10
#define VMX_BF_PROC_CTLS_MWAIT_EXIT_MASK                        UINT32_C(0x00000400)
#define VMX_BF_PROC_CTLS_RDPMC_EXIT_SHIFT                       11
#define VMX_BF_PROC_CTLS_RDPMC_EXIT_MASK                        UINT32_C(0x00000800)
#define VMX_BF_PROC_CTLS_RDTSC_EXIT_SHIFT                       12
#define VMX_BF_PROC_CTLS_RDTSC_EXIT_MASK                        UINT32_C(0x00001000)
#define VMX_BF_PROC_CTLS_RSVD_13_14_SHIFT                       13
#define VMX_BF_PROC_CTLS_RSVD_13_14_MASK                        UINT32_C(0x00006000)
#define VMX_BF_PROC_CTLS_CR3_LOAD_EXIT_SHIFT                    15
#define VMX_BF_PROC_CTLS_CR3_LOAD_EXIT_MASK                     UINT32_C(0x00008000)
#define VMX_BF_PROC_CTLS_CR3_STORE_EXIT_SHIFT                   16
#define VMX_BF_PROC_CTLS_CR3_STORE_EXIT_MASK                    UINT32_C(0x00010000)
#define VMX_BF_PROC_CTLS_USE_TERTIARY_CTLS_SHIFT                17
#define VMX_BF_PROC_CTLS_USE_TERTIARY_CTLS_MASK                 UINT32_C(0x00020000)
#define VMX_BF_PROC_CTLS_RSVD_18_SHIFT                          18
#define VMX_BF_PROC_CTLS_RSVD_18_MASK                           UINT32_C(0x00040000)
#define VMX_BF_PROC_CTLS_CR8_LOAD_EXIT_SHIFT                    19
#define VMX_BF_PROC_CTLS_CR8_LOAD_EXIT_MASK                     UINT32_C(0x00080000)
#define VMX_BF_PROC_CTLS_CR8_STORE_EXIT_SHIFT                   20
#define VMX_BF_PROC_CTLS_CR8_STORE_EXIT_MASK                    UINT32_C(0x00100000)
#define VMX_BF_PROC_CTLS_USE_TPR_SHADOW_SHIFT                   21
#define VMX_BF_PROC_CTLS_USE_TPR_SHADOW_MASK                    UINT32_C(0x00200000)
#define VMX_BF_PROC_CTLS_NMI_WINDOW_EXIT_SHIFT                  22
#define VMX_BF_PROC_CTLS_NMI_WINDOW_EXIT_MASK                   UINT32_C(0x00400000)
#define VMX_BF_PROC_CTLS_MOV_DR_EXIT_SHIFT                      23
#define VMX_BF_PROC_CTLS_MOV_DR_EXIT_MASK                       UINT32_C(0x00800000)
#define VMX_BF_PROC_CTLS_UNCOND_IO_EXIT_SHIFT                   24
#define VMX_BF_PROC_CTLS_UNCOND_IO_EXIT_MASK                    UINT32_C(0x01000000)
#define VMX_BF_PROC_CTLS_USE_IO_BITMAPS_SHIFT                   25
#define VMX_BF_PROC_CTLS_USE_IO_BITMAPS_MASK                    UINT32_C(0x02000000)
#define VMX_BF_PROC_CTLS_RSVD_26_SHIFT                          26
#define VMX_BF_PROC_CTLS_RSVD_26_MASK                           UINT32_C(0x4000000)
#define VMX_BF_PROC_CTLS_MONITOR_TRAP_FLAG_SHIFT                27
#define VMX_BF_PROC_CTLS_MONITOR_TRAP_FLAG_MASK                 UINT32_C(0x08000000)
#define VMX_BF_PROC_CTLS_USE_MSR_BITMAPS_SHIFT                  28
#define VMX_BF_PROC_CTLS_USE_MSR_BITMAPS_MASK                   UINT32_C(0x10000000)
#define VMX_BF_PROC_CTLS_MONITOR_EXIT_SHIFT                     29
#define VMX_BF_PROC_CTLS_MONITOR_EXIT_MASK                      UINT32_C(0x20000000)
#define VMX_BF_PROC_CTLS_PAUSE_EXIT_SHIFT                       30
#define VMX_BF_PROC_CTLS_PAUSE_EXIT_MASK                        UINT32_C(0x40000000)
#define VMX_BF_PROC_CTLS_USE_SECONDARY_CTLS_SHIFT               31
#define VMX_BF_PROC_CTLS_USE_SECONDARY_CTLS_MASK                UINT32_C(0x80000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_PROC_CTLS_, UINT32_C(0), UINT32_MAX,
                            (RSVD_0_1, INT_WINDOW_EXIT, USE_TSC_OFFSETTING, RSVD_4_6, HLT_EXIT, RSVD_8, INVLPG_EXIT,
                             MWAIT_EXIT, RDPMC_EXIT, RDTSC_EXIT, RSVD_13_14, CR3_LOAD_EXIT, CR3_STORE_EXIT, USE_TERTIARY_CTLS,
                             RSVD_18, CR8_LOAD_EXIT, CR8_STORE_EXIT, USE_TPR_SHADOW, NMI_WINDOW_EXIT, MOV_DR_EXIT, UNCOND_IO_EXIT,
                             USE_IO_BITMAPS, RSVD_26, MONITOR_TRAP_FLAG, USE_MSR_BITMAPS, MONITOR_EXIT, PAUSE_EXIT,
                             USE_SECONDARY_CTLS));
/** @} */


/** @name Secondary Processor-based VM-execution controls.
 * @{
 */
/** Virtualize APIC accesses. */
#define VMX_PROC_CTLS2_VIRT_APIC_ACCESS                         RT_BIT(0)
/** EPT supported/enabled. */
#define VMX_PROC_CTLS2_EPT                                      RT_BIT(1)
/** Descriptor table instructions cause VM-exits. */
#define VMX_PROC_CTLS2_DESC_TABLE_EXIT                          RT_BIT(2)
/** RDTSCP supported/enabled. */
#define VMX_PROC_CTLS2_RDTSCP                                   RT_BIT(3)
/** Virtualize x2APIC mode. */
#define VMX_PROC_CTLS2_VIRT_X2APIC_MODE                         RT_BIT(4)
/** VPID supported/enabled. */
#define VMX_PROC_CTLS2_VPID                                     RT_BIT(5)
/** VM-exit when executing the WBINVD instruction. */
#define VMX_PROC_CTLS2_WBINVD_EXIT                              RT_BIT(6)
/** Unrestricted guest execution. */
#define VMX_PROC_CTLS2_UNRESTRICTED_GUEST                       RT_BIT(7)
/** APIC register virtualization. */
#define VMX_PROC_CTLS2_APIC_REG_VIRT                            RT_BIT(8)
/** Virtual-interrupt delivery. */
#define VMX_PROC_CTLS2_VIRT_INT_DELIVERY                        RT_BIT(9)
/** A specified number of pause loops cause a VM-exit. */
#define VMX_PROC_CTLS2_PAUSE_LOOP_EXIT                          RT_BIT(10)
/** VM-exit when executing RDRAND instructions. */
#define VMX_PROC_CTLS2_RDRAND_EXIT                              RT_BIT(11)
/** Enables INVPCID instructions. */
#define VMX_PROC_CTLS2_INVPCID                                  RT_BIT(12)
/** Enables VMFUNC instructions. */
#define VMX_PROC_CTLS2_VMFUNC                                   RT_BIT(13)
/** Enables VMCS shadowing. */
#define VMX_PROC_CTLS2_VMCS_SHADOWING                           RT_BIT(14)
/** Enables ENCLS VM-exits. */
#define VMX_PROC_CTLS2_ENCLS_EXIT                               RT_BIT(15)
/** VM-exit when executing RDSEED. */
#define VMX_PROC_CTLS2_RDSEED_EXIT                              RT_BIT(16)
/** Enables page-modification logging. */
#define VMX_PROC_CTLS2_PML                                      RT_BIT(17)
/** Controls whether EPT-violations may cause \#VE instead of exits. */
#define VMX_PROC_CTLS2_EPT_XCPT_VE                              RT_BIT(18)
/** Conceal VMX non-root operation from Intel processor trace (PT). */
#define VMX_PROC_CTLS2_CONCEAL_VMX_FROM_PT                      RT_BIT(19)
/** Enables XSAVES/XRSTORS instructions. */
#define VMX_PROC_CTLS2_XSAVES_XRSTORS                           RT_BIT(20)
/** Enables PASID translation. */
#define VMX_PROC_CTLS2_PASID_TRANSLATE                          RT_BIT(21)
/** Enables supervisor/user mode based EPT execute permission for linear
 *  addresses. */
#define VMX_PROC_CTLS2_MODE_BASED_EPT_PERM                      RT_BIT(22)
/** Enables EPT write permissions to be specified at granularity of 128 bytes. */
#define VMX_PROC_CTLS2_SPP_EPT                                  RT_BIT(23)
/** Intel PT output addresses are treated as guest-physical addresses and
 *  translated using EPT. */
#define VMX_PROC_CTLS2_PT_EPT                                   RT_BIT(24)
/** Use TSC scaling. */
#define VMX_PROC_CTLS2_TSC_SCALING                              RT_BIT(25)
/** Enables TPAUSE, UMONITOR and UMWAIT instructions. */
#define VMX_PROC_CTLS2_USER_WAIT_PAUSE                          RT_BIT(26)
/** Enables PCONFIG instruction. */
#define VMX_PROC_CTLS2_PCONFIG                                  RT_BIT(27)
/** Enables consulting ENCLV-exiting bitmap when executing ENCLV. */
#define VMX_PROC_CTLS2_ENCLV_EXIT                               RT_BIT(28)
/** Enables VMM Bus-lock detection. */
#define VMX_PROC_CTLS2_BUS_LOCK_DETECT                          RT_BIT(30)
/** Enables instruction timeout VM-exits. */
#define VMX_PROC_CTLS2_INSTR_TIMEOUT                            RT_BIT(31)


/** Bit fields for MSR_IA32_VMX_PROCBASED_CTLS2 and Secondary processor-based
 *  VM-execution controls field in the VMCS. */
#define VMX_BF_PROC_CTLS2_VIRT_APIC_ACCESS_SHIFT                0
#define VMX_BF_PROC_CTLS2_VIRT_APIC_ACCESS_MASK                 UINT32_C(0x00000001)
#define VMX_BF_PROC_CTLS2_EPT_SHIFT                             1
#define VMX_BF_PROC_CTLS2_EPT_MASK                              UINT32_C(0x00000002)
#define VMX_BF_PROC_CTLS2_DESC_TABLE_EXIT_SHIFT                 2
#define VMX_BF_PROC_CTLS2_DESC_TABLE_EXIT_MASK                  UINT32_C(0x00000004)
#define VMX_BF_PROC_CTLS2_RDTSCP_SHIFT                          3
#define VMX_BF_PROC_CTLS2_RDTSCP_MASK                           UINT32_C(0x00000008)
#define VMX_BF_PROC_CTLS2_VIRT_X2APIC_MODE_SHIFT                4
#define VMX_BF_PROC_CTLS2_VIRT_X2APIC_MODE_MASK                 UINT32_C(0x00000010)
#define VMX_BF_PROC_CTLS2_VPID_SHIFT                            5
#define VMX_BF_PROC_CTLS2_VPID_MASK                             UINT32_C(0x00000020)
#define VMX_BF_PROC_CTLS2_WBINVD_EXIT_SHIFT                     6
#define VMX_BF_PROC_CTLS2_WBINVD_EXIT_MASK                      UINT32_C(0x00000040)
#define VMX_BF_PROC_CTLS2_UNRESTRICTED_GUEST_SHIFT              7
#define VMX_BF_PROC_CTLS2_UNRESTRICTED_GUEST_MASK               UINT32_C(0x00000080)
#define VMX_BF_PROC_CTLS2_APIC_REG_VIRT_SHIFT                   8
#define VMX_BF_PROC_CTLS2_APIC_REG_VIRT_MASK                    UINT32_C(0x00000100)
#define VMX_BF_PROC_CTLS2_VIRT_INT_DELIVERY_SHIFT               9
#define VMX_BF_PROC_CTLS2_VIRT_INT_DELIVERY_MASK                UINT32_C(0x00000200)
#define VMX_BF_PROC_CTLS2_PAUSE_LOOP_EXIT_SHIFT                 10
#define VMX_BF_PROC_CTLS2_PAUSE_LOOP_EXIT_MASK                  UINT32_C(0x00000400)
#define VMX_BF_PROC_CTLS2_RDRAND_EXIT_SHIFT                     11
#define VMX_BF_PROC_CTLS2_RDRAND_EXIT_MASK                      UINT32_C(0x00000800)
#define VMX_BF_PROC_CTLS2_INVPCID_SHIFT                         12
#define VMX_BF_PROC_CTLS2_INVPCID_MASK                          UINT32_C(0x00001000)
#define VMX_BF_PROC_CTLS2_VMFUNC_SHIFT                          13
#define VMX_BF_PROC_CTLS2_VMFUNC_MASK                           UINT32_C(0x00002000)
#define VMX_BF_PROC_CTLS2_VMCS_SHADOWING_SHIFT                  14
#define VMX_BF_PROC_CTLS2_VMCS_SHADOWING_MASK                   UINT32_C(0x00004000)
#define VMX_BF_PROC_CTLS2_ENCLS_EXIT_SHIFT                      15
#define VMX_BF_PROC_CTLS2_ENCLS_EXIT_MASK                       UINT32_C(0x00008000)
#define VMX_BF_PROC_CTLS2_RDSEED_EXIT_SHIFT                     16
#define VMX_BF_PROC_CTLS2_RDSEED_EXIT_MASK                      UINT32_C(0x00010000)
#define VMX_BF_PROC_CTLS2_PML_SHIFT                             17
#define VMX_BF_PROC_CTLS2_PML_MASK                              UINT32_C(0x00020000)
#define VMX_BF_PROC_CTLS2_EPT_VE_SHIFT                          18
#define VMX_BF_PROC_CTLS2_EPT_VE_MASK                           UINT32_C(0x00040000)
#define VMX_BF_PROC_CTLS2_CONCEAL_VMX_FROM_PT_SHIFT             19
#define VMX_BF_PROC_CTLS2_CONCEAL_VMX_FROM_PT_MASK              UINT32_C(0x00080000)
#define VMX_BF_PROC_CTLS2_XSAVES_XRSTORS_SHIFT                  20
#define VMX_BF_PROC_CTLS2_XSAVES_XRSTORS_MASK                   UINT32_C(0x00100000)
#define VMX_BF_PROC_CTLS2_PASID_TRANSLATE_SHIFT                 21
#define VMX_BF_PROC_CTLS2_PASID_TRANSLATE_MASK                  UINT32_C(0x00200000)
#define VMX_BF_PROC_CTLS2_MODE_BASED_EPT_PERM_SHIFT             22
#define VMX_BF_PROC_CTLS2_MODE_BASED_EPT_PERM_MASK              UINT32_C(0x00400000)
#define VMX_BF_PROC_CTLS2_SPP_EPT_SHIFT                         23
#define VMX_BF_PROC_CTLS2_SPP_EPT_MASK                          UINT32_C(0x00800000)
#define VMX_BF_PROC_CTLS2_PT_EPT_SHIFT                          24
#define VMX_BF_PROC_CTLS2_PT_EPT_MASK                           UINT32_C(0x01000000)
#define VMX_BF_PROC_CTLS2_TSC_SCALING_SHIFT                     25
#define VMX_BF_PROC_CTLS2_TSC_SCALING_MASK                      UINT32_C(0x02000000)
#define VMX_BF_PROC_CTLS2_USER_WAIT_PAUSE_SHIFT                 26
#define VMX_BF_PROC_CTLS2_USER_WAIT_PAUSE_MASK                  UINT32_C(0x04000000)
#define VMX_BF_PROC_CTLS2_PCONFIG_SHIFT                         27
#define VMX_BF_PROC_CTLS2_PCONFIG_MASK                          UINT32_C(0x08000000)
#define VMX_BF_PROC_CTLS2_ENCLV_EXIT_SHIFT                      28
#define VMX_BF_PROC_CTLS2_ENCLV_EXIT_MASK                       UINT32_C(0x10000000)
#define VMX_BF_PROC_CTLS2_RSVD_29_SHIFT                         29
#define VMX_BF_PROC_CTLS2_RSVD_29_MASK                          UINT32_C(0x20000000)
#define VMX_BF_PROC_CTLS2_BUSLOCK_DETECT_SHIFT                  30
#define VMX_BF_PROC_CTLS2_BUSLOCK_DETECT_MASK                   UINT32_C(0x40000000)
#define VMX_BF_PROC_CTLS2_INSTR_TIMEOUT_SHIFT                   31
#define VMX_BF_PROC_CTLS2_INSTR_TIMEOUT_MASK                    UINT32_C(0x80000000)

RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_PROC_CTLS2_, UINT32_C(0), UINT32_MAX,
                            (VIRT_APIC_ACCESS, EPT, DESC_TABLE_EXIT, RDTSCP, VIRT_X2APIC_MODE, VPID, WBINVD_EXIT,
                             UNRESTRICTED_GUEST, APIC_REG_VIRT, VIRT_INT_DELIVERY, PAUSE_LOOP_EXIT, RDRAND_EXIT, INVPCID, VMFUNC,
                             VMCS_SHADOWING, ENCLS_EXIT, RDSEED_EXIT, PML, EPT_VE, CONCEAL_VMX_FROM_PT, XSAVES_XRSTORS,
                             PASID_TRANSLATE, MODE_BASED_EPT_PERM, SPP_EPT, PT_EPT, TSC_SCALING, USER_WAIT_PAUSE, PCONFIG,
                             ENCLV_EXIT, RSVD_29, BUSLOCK_DETECT, INSTR_TIMEOUT));
/** @} */


/** @name Tertiary Processor-based VM-execution controls.
 * @{
 */
/** VM-exit when executing LOADIWKEY. */
#define VMX_PROC_CTLS3_LOADIWKEY_EXIT                           RT_BIT_64(0)
/** Enables hypervisor-managed linear-address translation (HLAT). */
#define VMX_PROC_CTLS3_HLAT                                     RT_BIT_64(1)
/** Enables EPT paging-write control. */
#define VMX_PROC_CTLS3_EPT_PAGING_WRITE                         RT_BIT_64(2)
/** Enables Guest-paging verification. */
#define VMX_PROC_CTLS3_GST_PAGING_VERIFY                        RT_BIT_64(3)
/** Enables IPI virtualization. */
#define VMX_PROC_CTLS3_IPI_VIRT                                 RT_BIT_64(4)
/** Virtualize IA32_SPEC_CTRL. */
#define VMX_PROC_CTLS3_VIRT_SPEC_CTRL                           RT_BIT_64(7)

/** Bit fields for Tertiary processor-based VM-execution controls field in the VMCS. */
#define VMX_BF_PROC_CTLS3_LOADIWKEY_EXIT_SHIFT                  0
#define VMX_BF_PROC_CTLS3_LOADIWKEY_EXIT_MASK                   UINT64_C(0x0000000000000001)
#define VMX_BF_PROC_CTLS3_HLAT_SHIFT                            1
#define VMX_BF_PROC_CTLS3_HLAT_MASK                             UINT64_C(0x0000000000000002)
#define VMX_BF_PROC_CTLS3_EPT_PAGING_WRITE_SHIFT                2
#define VMX_BF_PROC_CTLS3_EPT_PAGING_WRITE_MASK                 UINT64_C(0x0000000000000004)
#define VMX_BF_PROC_CTLS3_GST_PAGING_VERIFY_SHIFT               3
#define VMX_BF_PROC_CTLS3_GST_PAGING_VERIFY_MASK                UINT64_C(0x0000000000000008)
#define VMX_BF_PROC_CTLS3_IPI_VIRT_SHIFT                        4
#define VMX_BF_PROC_CTLS3_IPI_VIRT_MASK                         UINT64_C(0x0000000000000010)
#define VMX_BF_PROC_CTLS3_RSVD_5_6_SHIFT                        5
#define VMX_BF_PROC_CTLS3_RSVD_5_6_MASK                         UINT64_C(0x0000000000000060)
#define VMX_BF_PROC_CTLS3_VIRT_SPEC_CTRL_SHIFT                  7
#define VMX_BF_PROC_CTLS3_VIRT_SPEC_CTRL_MASK                   UINT64_C(0x0000000000000080)
#define VMX_BF_PROC_CTLS3_RSVD_8_63_SHIFT                       8
#define VMX_BF_PROC_CTLS3_RSVD_8_63_MASK                        UINT64_C(0xffffffffffffff00)

RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_PROC_CTLS3_, UINT64_C(0), UINT64_MAX,
                            (LOADIWKEY_EXIT, HLAT, EPT_PAGING_WRITE, GST_PAGING_VERIFY, IPI_VIRT, RSVD_5_6, VIRT_SPEC_CTRL,
                             RSVD_8_63));
/** @} */


/** @name VM-entry controls.
 * @{
 */
/** Load guest debug controls (dr7 & IA32_DEBUGCTL_MSR) (forced to 1 on the
 *  'first' VT-x capable CPUs; this actually includes the newest Nehalem CPUs) */
#define VMX_ENTRY_CTLS_LOAD_DEBUG                               RT_BIT(2)
/** 64-bit guest mode. Must be 0 for CPUs that don't support AMD64. */
#define VMX_ENTRY_CTLS_IA32E_MODE_GUEST                         RT_BIT(9)
/** In SMM mode after VM-entry. */
#define VMX_ENTRY_CTLS_ENTRY_TO_SMM                             RT_BIT(10)
/** Disable dual treatment of SMI and SMM; must be zero for VM-entry outside of SMM. */
#define VMX_ENTRY_CTLS_DEACTIVATE_DUAL_MON                      RT_BIT(11)
/** Whether the guest IA32_PERF_GLOBAL_CTRL MSR is loaded on VM-entry. */
#define VMX_ENTRY_CTLS_LOAD_PERF_MSR                            RT_BIT(13)
/** Whether the guest IA32_PAT MSR is loaded on VM-entry. */
#define VMX_ENTRY_CTLS_LOAD_PAT_MSR                             RT_BIT(14)
/** Whether the guest IA32_EFER MSR is loaded on VM-entry. */
#define VMX_ENTRY_CTLS_LOAD_EFER_MSR                            RT_BIT(15)
/** Whether the guest IA32_BNDCFGS MSR is loaded on VM-entry. */
#define VMX_ENTRY_CTLS_LOAD_BNDCFGS_MSR                         RT_BIT(16)
/** Whether to conceal VMX from Intel PT (Processor Trace). */
#define VMX_ENTRY_CTLS_CONCEAL_VMX_FROM_PT                      RT_BIT(17)
/** Whether the guest IA32_RTIT MSR is loaded on VM-entry. */
#define VMX_ENTRY_CTLS_LOAD_RTIT_CTL_MSR                        RT_BIT(18)
/** Whether the guest UINV is loaded on VM-entry. */
#define VMX_ENTRY_CTLS_LOAD_UINV                                RT_BIT(19)
/** Whether the guest CET-related MSRs and SPP are loaded on VM-entry. */
#define VMX_ENTRY_CTLS_LOAD_CET_STATE                           RT_BIT(20)
/** Whether the guest IA32_LBR_CTL MSR is loaded on VM-entry. */
#define VMX_ENTRY_CTLS_LOAD_LBR_CTL_MSR                         RT_BIT(21)
/** Whether the guest IA32_PKRS MSR is loaded on VM-entry. */
#define VMX_ENTRY_CTLS_LOAD_PKRS_MSR                            RT_BIT(22)
/** Default1 class when true-capability MSRs are not supported. */
#define VMX_ENTRY_CTLS_DEFAULT1                                 UINT32_C(0x000011ff)

/** Bit fields for MSR_IA32_VMX_ENTRY_CTLS and VM-entry controls field in the
 *  VMCS. */
#define VMX_BF_ENTRY_CTLS_RSVD_0_1_SHIFT                        0
#define VMX_BF_ENTRY_CTLS_RSVD_0_1_MASK                         UINT32_C(0x00000003)
#define VMX_BF_ENTRY_CTLS_LOAD_DEBUG_SHIFT                      2
#define VMX_BF_ENTRY_CTLS_LOAD_DEBUG_MASK                       UINT32_C(0x00000004)
#define VMX_BF_ENTRY_CTLS_RSVD_3_8_SHIFT                        3
#define VMX_BF_ENTRY_CTLS_RSVD_3_8_MASK                         UINT32_C(0x000001f8)
#define VMX_BF_ENTRY_CTLS_IA32E_MODE_GUEST_SHIFT                9
#define VMX_BF_ENTRY_CTLS_IA32E_MODE_GUEST_MASK                 UINT32_C(0x00000200)
#define VMX_BF_ENTRY_CTLS_ENTRY_SMM_SHIFT                       10
#define VMX_BF_ENTRY_CTLS_ENTRY_SMM_MASK                        UINT32_C(0x00000400)
#define VMX_BF_ENTRY_CTLS_DEACTIVATE_DUAL_MON_SHIFT             11
#define VMX_BF_ENTRY_CTLS_DEACTIVATE_DUAL_MON_MASK              UINT32_C(0x00000800)
#define VMX_BF_ENTRY_CTLS_RSVD_12_SHIFT                         12
#define VMX_BF_ENTRY_CTLS_RSVD_12_MASK                          UINT32_C(0x00001000)
#define VMX_BF_ENTRY_CTLS_LOAD_PERF_MSR_SHIFT                   13
#define VMX_BF_ENTRY_CTLS_LOAD_PERF_MSR_MASK                    UINT32_C(0x00002000)
#define VMX_BF_ENTRY_CTLS_LOAD_PAT_MSR_SHIFT                    14
#define VMX_BF_ENTRY_CTLS_LOAD_PAT_MSR_MASK                     UINT32_C(0x00004000)
#define VMX_BF_ENTRY_CTLS_LOAD_EFER_MSR_SHIFT                   15
#define VMX_BF_ENTRY_CTLS_LOAD_EFER_MSR_MASK                    UINT32_C(0x00008000)
#define VMX_BF_ENTRY_CTLS_LOAD_BNDCFGS_MSR_SHIFT                16
#define VMX_BF_ENTRY_CTLS_LOAD_BNDCFGS_MSR_MASK                 UINT32_C(0x00010000)
#define VMX_BF_ENTRY_CTLS_CONCEAL_VMX_FROM_PT_SHIFT             17
#define VMX_BF_ENTRY_CTLS_CONCEAL_VMX_FROM_PT_MASK              UINT32_C(0x00020000)
#define VMX_BF_ENTRY_CTLS_LOAD_RTIT_CTL_MSR_SHIFT               18
#define VMX_BF_ENTRY_CTLS_LOAD_RTIT_CTL_MSR_MASK                UINT32_C(0x00040000)
#define VMX_BF_ENTRY_CTLS_LOAD_UINV_SHIFT                       19
#define VMX_BF_ENTRY_CTLS_LOAD_UINV_MASK                        UINT32_C(0x00080000)
#define VMX_BF_ENTRY_CTLS_LOAD_CET_SHIFT                        20
#define VMX_BF_ENTRY_CTLS_LOAD_CET_MASK                         UINT32_C(0x00100000)
#define VMX_BF_ENTRY_CTLS_LOAD_LBR_CTL_MSR_SHIFT                21
#define VMX_BF_ENTRY_CTLS_LOAD_LBR_CTL_MSR_MASK                 UINT32_C(0x00200000)
#define VMX_BF_ENTRY_CTLS_LOAD_PKRS_MSR_SHIFT                   22
#define VMX_BF_ENTRY_CTLS_LOAD_PKRS_MSR_MASK                    UINT32_C(0x00400000)
#define VMX_BF_ENTRY_CTLS_RSVD_23_31_SHIFT                      23
#define VMX_BF_ENTRY_CTLS_RSVD_23_31_MASK                       UINT32_C(0xff800000)

RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_ENTRY_CTLS_, UINT32_C(0), UINT32_MAX,
                            (RSVD_0_1, LOAD_DEBUG, RSVD_3_8, IA32E_MODE_GUEST, ENTRY_SMM, DEACTIVATE_DUAL_MON, RSVD_12,
                             LOAD_PERF_MSR, LOAD_PAT_MSR, LOAD_EFER_MSR, LOAD_BNDCFGS_MSR, CONCEAL_VMX_FROM_PT,
                             LOAD_RTIT_CTL_MSR, LOAD_UINV, LOAD_CET, LOAD_LBR_CTL_MSR, LOAD_PKRS_MSR, RSVD_23_31));
/** @} */


/** @name VM-exit controls.
 * @{
 */
/** Save guest debug controls (dr7 & IA32_DEBUGCTL_MSR) (forced to 1 on the
 *  'first' VT-x capable CPUs; this actually includes the newest Nehalem CPUs) */
#define VMX_EXIT_CTLS_SAVE_DEBUG                                RT_BIT(2)
/** Return to long mode after a VM-exit. */
#define VMX_EXIT_CTLS_HOST_ADDR_SPACE_SIZE                      RT_BIT(9)
/** Whether the host IA32_PERF_GLOBAL_CTRL MSR is loaded on VM-exit. */
#define VMX_EXIT_CTLS_LOAD_PERF_MSR                             RT_BIT(12)
/** Acknowledge external interrupts with the irq controller if one caused a VM-exit. */
#define VMX_EXIT_CTLS_ACK_EXT_INT                               RT_BIT(15)
/** Whether the guest IA32_PAT MSR is saved on VM-exit. */
#define VMX_EXIT_CTLS_SAVE_PAT_MSR                              RT_BIT(18)
/** Whether the host IA32_PAT MSR is loaded on VM-exit. */
#define VMX_EXIT_CTLS_LOAD_PAT_MSR                              RT_BIT(19)
/** Whether the guest IA32_EFER MSR is saved on VM-exit. */
#define VMX_EXIT_CTLS_SAVE_EFER_MSR                             RT_BIT(20)
/** Whether the host IA32_EFER MSR is loaded on VM-exit. */
#define VMX_EXIT_CTLS_LOAD_EFER_MSR                             RT_BIT(21)
/** Whether the value of the VMX preemption timer is saved on every VM-exit. */
#define VMX_EXIT_CTLS_SAVE_PREEMPT_TIMER                        RT_BIT(22)
/** Whether IA32_BNDCFGS MSR is cleared on VM-exit. */
#define VMX_EXIT_CTLS_CLEAR_BNDCFGS_MSR                         RT_BIT(23)
/** Whether to conceal VMX from Intel PT. */
#define VMX_EXIT_CTLS_CONCEAL_VMX_FROM_PT                       RT_BIT(24)
/** Whether IA32_RTIT_CTL MSR is cleared on VM-exit. */
#define VMX_EXIT_CTLS_CLEAR_RTIT_CTL_MSR                        RT_BIT(25)
/** Whether IA32_LBR_CTL MSR is cleared on VM-exit. */
#define VMX_EXIT_CTLS_CLEAR_LBR_CTL_MSR                         RT_BIT(26)
/** Whether UINV is cleared on VM-exit. */
#define VMX_EXIT_CTLS_CLEAR_UINV                                RT_BIT(27)
/** Whether CET-related MSRs and SPP are loaded on VM-exit. */
#define VMX_EXIT_CTLS_LOAD_CET_STATE                            RT_BIT(28)
/** Whether the host IA32_PKRS MSR is loaded on VM-exit. */
#define VMX_EXIT_CTLS_LOAD_PKRS_MSR                             RT_BIT(29)
/** Whether the host IA32_PERF_GLOBAL_CTRL MSR is saved on VM-exit. */
#define VMX_EXIT_CTLS_SAVE_PERF_MSR                             RT_BIT(30)
/** Whether secondary VM-exit controls are used. */
#define VMX_EXIT_CTLS_USE_SECONDARY_CTLS                        RT_BIT(31)
/** Default1 class when true-capability MSRs are not supported. */
#define VMX_EXIT_CTLS_DEFAULT1                                  UINT32_C(0x00036dff)

/** Bit fields for MSR_IA32_VMX_EXIT_CTLS and VM-exit controls field in the
 *  VMCS. */
#define VMX_BF_EXIT_CTLS_RSVD_0_1_SHIFT                         0
#define VMX_BF_EXIT_CTLS_RSVD_0_1_MASK                          UINT32_C(0x00000003)
#define VMX_BF_EXIT_CTLS_SAVE_DEBUG_SHIFT                       2
#define VMX_BF_EXIT_CTLS_SAVE_DEBUG_MASK                        UINT32_C(0x00000004)
#define VMX_BF_EXIT_CTLS_RSVD_3_8_SHIFT                         3
#define VMX_BF_EXIT_CTLS_RSVD_3_8_MASK                          UINT32_C(0x000001f8)
#define VMX_BF_EXIT_CTLS_HOST_ADDR_SPACE_SIZE_SHIFT             9
#define VMX_BF_EXIT_CTLS_HOST_ADDR_SPACE_SIZE_MASK              UINT32_C(0x00000200)
#define VMX_BF_EXIT_CTLS_RSVD_10_11_SHIFT                       10
#define VMX_BF_EXIT_CTLS_RSVD_10_11_MASK                        UINT32_C(0x00000c00)
#define VMX_BF_EXIT_CTLS_LOAD_PERF_MSR_SHIFT                    12
#define VMX_BF_EXIT_CTLS_LOAD_PERF_MSR_MASK                     UINT32_C(0x00001000)
#define VMX_BF_EXIT_CTLS_RSVD_13_14_SHIFT                       13
#define VMX_BF_EXIT_CTLS_RSVD_13_14_MASK                        UINT32_C(0x00006000)
#define VMX_BF_EXIT_CTLS_ACK_EXT_INT_SHIFT                      15
#define VMX_BF_EXIT_CTLS_ACK_EXT_INT_MASK                       UINT32_C(0x00008000)
#define VMX_BF_EXIT_CTLS_RSVD_16_17_SHIFT                       16
#define VMX_BF_EXIT_CTLS_RSVD_16_17_MASK                        UINT32_C(0x00030000)
#define VMX_BF_EXIT_CTLS_SAVE_PAT_MSR_SHIFT                     18
#define VMX_BF_EXIT_CTLS_SAVE_PAT_MSR_MASK                      UINT32_C(0x00040000)
#define VMX_BF_EXIT_CTLS_LOAD_PAT_MSR_SHIFT                     19
#define VMX_BF_EXIT_CTLS_LOAD_PAT_MSR_MASK                      UINT32_C(0x00080000)
#define VMX_BF_EXIT_CTLS_SAVE_EFER_MSR_SHIFT                    20
#define VMX_BF_EXIT_CTLS_SAVE_EFER_MSR_MASK                     UINT32_C(0x00100000)
#define VMX_BF_EXIT_CTLS_LOAD_EFER_MSR_SHIFT                    21
#define VMX_BF_EXIT_CTLS_LOAD_EFER_MSR_MASK                     UINT32_C(0x00200000)
#define VMX_BF_EXIT_CTLS_SAVE_PREEMPT_TIMER_SHIFT               22
#define VMX_BF_EXIT_CTLS_SAVE_PREEMPT_TIMER_MASK                UINT32_C(0x00400000)
#define VMX_BF_EXIT_CTLS_CLEAR_BNDCFGS_MSR_SHIFT                23
#define VMX_BF_EXIT_CTLS_CLEAR_BNDCFGS_MSR_MASK                 UINT32_C(0x00800000)
#define VMX_BF_EXIT_CTLS_CONCEAL_VMX_FROM_PT_SHIFT              24
#define VMX_BF_EXIT_CTLS_CONCEAL_VMX_FROM_PT_MASK               UINT32_C(0x01000000)
#define VMX_BF_EXIT_CTLS_CLEAR_RTIT_CTL_MSR_SHIFT               25
#define VMX_BF_EXIT_CTLS_CLEAR_RTIT_CTL_MSR_MASK                UINT32_C(0x02000000)
#define VMX_BF_EXIT_CTLS_CLEAR_LBR_CTL_MSR_SHIFT                26
#define VMX_BF_EXIT_CTLS_CLEAR_LBR_CTL_MSR_MASK                 UINT32_C(0x04000000)
#define VMX_BF_EXIT_CTLS_CLEAR_UINV_SHIFT                       27
#define VMX_BF_EXIT_CTLS_CLEAR_UINV_MASK                        UINT32_C(0x08000000)
#define VMX_BF_EXIT_CTLS_LOAD_CET_SHIFT                         28
#define VMX_BF_EXIT_CTLS_LOAD_CET_MASK                          UINT32_C(0x10000000)
#define VMX_BF_EXIT_CTLS_LOAD_PKRS_MSR_SHIFT                    29
#define VMX_BF_EXIT_CTLS_LOAD_PKRS_MSR_MASK                     UINT32_C(0x20000000)
#define VMX_BF_EXIT_CTLS_SAVE_PERF_MSR_SHIFT                    30
#define VMX_BF_EXIT_CTLS_SAVE_PERF_MSR_MASK                     UINT32_C(0x40000000)
#define VMX_BF_EXIT_CTLS_USE_SECONDARY_CTLS_SHIFT               31
#define VMX_BF_EXIT_CTLS_USE_SECONDARY_CTLS_MASK                UINT32_C(0x80000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_CTLS_, UINT32_C(0), UINT32_MAX,
                            (RSVD_0_1, SAVE_DEBUG, RSVD_3_8, HOST_ADDR_SPACE_SIZE, RSVD_10_11, LOAD_PERF_MSR, RSVD_13_14,
                             ACK_EXT_INT, RSVD_16_17, SAVE_PAT_MSR, LOAD_PAT_MSR, SAVE_EFER_MSR, LOAD_EFER_MSR,
                             SAVE_PREEMPT_TIMER, CLEAR_BNDCFGS_MSR, CONCEAL_VMX_FROM_PT, CLEAR_RTIT_CTL_MSR, CLEAR_LBR_CTL_MSR,
                             CLEAR_UINV, LOAD_CET, LOAD_PKRS_MSR, SAVE_PERF_MSR, USE_SECONDARY_CTLS));
/** @} */


/** @name VM-exit reason.
 * @{
 */
#define VMX_EXIT_REASON_BASIC(a)                                ((a) & 0xffff)
#define VMX_EXIT_REASON_HAS_ENTRY_FAILED(a)                     (((a) >> 31) & 1)
#define VMX_EXIT_REASON_ENTRY_FAILED                            RT_BIT(31)

/** Bit fields for VM-exit reason. */
/** The exit reason. */
#define VMX_BF_EXIT_REASON_BASIC_SHIFT                          0
#define VMX_BF_EXIT_REASON_BASIC_MASK                           UINT32_C(0x0000ffff)
/** Bits 16:26 are reseved and MBZ. */
#define VMX_BF_EXIT_REASON_RSVD_16_26_SHIFT                     16
#define VMX_BF_EXIT_REASON_RSVD_16_26_MASK                      UINT32_C(0x07ff0000)
/** Whether the VM-exit was incident to enclave mode. */
#define VMX_BF_EXIT_REASON_ENCLAVE_MODE_SHIFT                   27
#define VMX_BF_EXIT_REASON_ENCLAVE_MODE_MASK                    UINT32_C(0x08000000)
/** Pending MTF (Monitor Trap Flag) during VM-exit (only applicable in SMM mode). */
#define VMX_BF_EXIT_REASON_SMM_PENDING_MTF_SHIFT                28
#define VMX_BF_EXIT_REASON_SMM_PENDING_MTF_MASK                 UINT32_C(0x10000000)
/** VM-exit from VMX root operation (only possible with SMM). */
#define VMX_BF_EXIT_REASON_VMX_ROOT_MODE_SHIFT                  29
#define VMX_BF_EXIT_REASON_VMX_ROOT_MODE_MASK                   UINT32_C(0x20000000)
/** Bit 30 is reserved and MBZ. */
#define VMX_BF_EXIT_REASON_RSVD_30_SHIFT                        30
#define VMX_BF_EXIT_REASON_RSVD_30_MASK                         UINT32_C(0x40000000)
/** Whether VM-entry failed (currently only happens during loading guest-state
 *  or MSRs or machine check exceptions). */
#define VMX_BF_EXIT_REASON_ENTRY_FAILED_SHIFT                   31
#define VMX_BF_EXIT_REASON_ENTRY_FAILED_MASK                    UINT32_C(0x80000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_REASON_, UINT32_C(0), UINT32_MAX,
                            (BASIC, RSVD_16_26, ENCLAVE_MODE, SMM_PENDING_MTF, VMX_ROOT_MODE, RSVD_30, ENTRY_FAILED));
/** @} */


/** @name VM-entry interruption information.
 * @{
 */
#define VMX_ENTRY_INT_INFO_IS_VALID(a)                          (((a) >> 31) & 1)
#define VMX_ENTRY_INT_INFO_VECTOR(a)                            ((a) & 0xff)
#define VMX_ENTRY_INT_INFO_TYPE_SHIFT                           8
#define VMX_ENTRY_INT_INFO_TYPE(a)                              (((a) >> 8) & 7)
#define VMX_ENTRY_INT_INFO_ERROR_CODE_VALID                     RT_BIT(11)
#define VMX_ENTRY_INT_INFO_IS_ERROR_CODE_VALID(a)               (((a) >> 11) & 1)
#define VMX_ENTRY_INT_INFO_NMI_UNBLOCK_IRET                     12
#define VMX_ENTRY_INT_INFO_IS_NMI_UNBLOCK_IRET(a)               (((a) >> 12) & 1)
#define VMX_ENTRY_INT_INFO_VALID                                RT_BIT(31)
#define VMX_ENTRY_INT_INFO_IS_VALID(a)                          (((a) >> 31) & 1)
/** Construct an VM-entry interruption information field from a VM-exit interruption
 *  info value (same except that bit 12 is reserved). */
#define VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(a)                ((a) & ~RT_BIT(12))
/** Construct a VM-entry interruption information field from an IDT-vectoring
 *  information field (same except that bit 12 is reserved). */
#define VMX_ENTRY_INT_INFO_FROM_EXIT_IDT_INFO(a)                ((a) & ~RT_BIT(12))
/** If the VM-entry interruption information field indicates a page-fault. */
#define VMX_ENTRY_INT_INFO_IS_XCPT_PF(a)                        (((a) & (  VMX_BF_ENTRY_INT_INFO_VALID_MASK \
                                                                         | VMX_BF_ENTRY_INT_INFO_TYPE_MASK \
                                                                         | VMX_BF_ENTRY_INT_INFO_VECTOR_MASK)) \
                                                                     == (  RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,  1) \
                                                                         | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,   VMX_ENTRY_INT_INFO_TYPE_HW_XCPT) \
                                                                         | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR, X86_XCPT_PF)))
/** If the VM-entry interruption information field indicates an external
 *  interrupt. */
#define VMX_ENTRY_INT_INFO_IS_EXT_INT(a)                        (((a) & (  VMX_BF_ENTRY_INT_INFO_VALID_MASK \
                                                                         | VMX_BF_ENTRY_INT_INFO_TYPE_MASK)) \
                                                                     == (  RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID, 1) \
                                                                         | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,  VMX_ENTRY_INT_INFO_TYPE_EXT_INT)))
/** If the VM-entry interruption information field indicates an NMI. */
#define VMX_ENTRY_INT_INFO_IS_XCPT_NMI(a)                       (((a) & (  VMX_BF_ENTRY_INT_INFO_VALID_MASK \
                                                                         | VMX_BF_ENTRY_INT_INFO_TYPE_MASK \
                                                                         | VMX_BF_ENTRY_INT_INFO_VECTOR_MASK)) \
                                                                     == (  RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,  1) \
                                                                         | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,   VMX_ENTRY_INT_INFO_TYPE_NMI) \
                                                                         | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR, X86_XCPT_NMI)))

/** Bit fields for VM-entry interruption information. */
/** The VM-entry interruption vector. */
#define VMX_BF_ENTRY_INT_INFO_VECTOR_SHIFT                      0
#define VMX_BF_ENTRY_INT_INFO_VECTOR_MASK                       UINT32_C(0x000000ff)
/** The VM-entry interruption type (see VMX_ENTRY_INT_INFO_TYPE_XXX). */
#define VMX_BF_ENTRY_INT_INFO_TYPE_SHIFT                        8
#define VMX_BF_ENTRY_INT_INFO_TYPE_MASK                         UINT32_C(0x00000700)
/** Whether this event has an error code. */
#define VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID_SHIFT              11
#define VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID_MASK               UINT32_C(0x00000800)
/** Bits 12:30 are reserved and MBZ. */
#define VMX_BF_ENTRY_INT_INFO_RSVD_12_30_SHIFT                  12
#define VMX_BF_ENTRY_INT_INFO_RSVD_12_30_MASK                   UINT32_C(0x7ffff000)
/** Whether this VM-entry interruption info is valid. */
#define VMX_BF_ENTRY_INT_INFO_VALID_SHIFT                       31
#define VMX_BF_ENTRY_INT_INFO_VALID_MASK                        UINT32_C(0x80000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_ENTRY_INT_INFO_, UINT32_C(0), UINT32_MAX,
                            (VECTOR, TYPE, ERR_CODE_VALID, RSVD_12_30, VALID));
/** @} */


/** @name VM-entry exception error code.
 * @{ */
/** Error code valid mask. */
/** @todo r=ramshankar: Intel spec. 26.2.1.3 "VM-Entry Control Fields" states that
 *        bits 31:15 MBZ. However, Intel spec. 6.13 "Error Code" states "To keep the
 *        stack aligned for doubleword pushes, the upper half of the error code is
 *        reserved" which implies bits 31:16 MBZ (and not 31:15) which is what we
 *        use below. */
#define VMX_ENTRY_INT_XCPT_ERR_CODE_VALID_MASK                  UINT32_C(0xffff)
/** @} */

/** @name VM-entry interruption information types.
 * @{
 */
#define VMX_ENTRY_INT_INFO_TYPE_EXT_INT                         0
#define VMX_ENTRY_INT_INFO_TYPE_RSVD                            1
#define VMX_ENTRY_INT_INFO_TYPE_NMI                             2
#define VMX_ENTRY_INT_INFO_TYPE_HW_XCPT                         3
#define VMX_ENTRY_INT_INFO_TYPE_SW_INT                          4
#define VMX_ENTRY_INT_INFO_TYPE_PRIV_SW_XCPT                    5
#define VMX_ENTRY_INT_INFO_TYPE_SW_XCPT                         6
#define VMX_ENTRY_INT_INFO_TYPE_OTHER_EVENT                     7
/** @} */


/** @name VM-entry interruption information vector types for
 *        VMX_ENTRY_INT_INFO_TYPE_OTHER_EVENT.
 * @{ */
#define VMX_ENTRY_INT_INFO_VECTOR_MTF                           0
/** @} */


/** @name VM-exit interruption information.
 * @{
 */
#define VMX_EXIT_INT_INFO_VECTOR(a)                             ((a) & 0xff)
#define VMX_EXIT_INT_INFO_TYPE_SHIFT                            8
#define VMX_EXIT_INT_INFO_TYPE(a)                               (((a) >> 8) & 7)
#define VMX_EXIT_INT_INFO_ERROR_CODE_VALID                      RT_BIT(11)
#define VMX_EXIT_INT_INFO_IS_ERROR_CODE_VALID(a)                (((a) >> 11) & 1)
#define VMX_EXIT_INT_INFO_NMI_UNBLOCK_IRET                      12
#define VMX_EXIT_INT_INFO_IS_NMI_UNBLOCK_IRET(a)                (((a) >> 12) & 1)
#define VMX_EXIT_INT_INFO_VALID                                 RT_BIT(31)
#define VMX_EXIT_INT_INFO_IS_VALID(a)                           (((a) >> 31) & 1)

/** If the VM-exit interruption information field indicates an page-fault. */
#define VMX_EXIT_INT_INFO_IS_XCPT_PF(a)                         (((a) & (  VMX_BF_EXIT_INT_INFO_VALID_MASK \
                                                                         | VMX_BF_EXIT_INT_INFO_TYPE_MASK \
                                                                         | VMX_BF_EXIT_INT_INFO_VECTOR_MASK)) \
                                                                     == (  RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VALID,  1) \
                                                                         | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_TYPE,   VMX_EXIT_INT_INFO_TYPE_HW_XCPT) \
                                                                         | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VECTOR, X86_XCPT_PF)))
/** If the VM-exit interruption information field indicates an double-fault. */
#define VMX_EXIT_INT_INFO_IS_XCPT_DF(a)                         (((a) & (  VMX_BF_EXIT_INT_INFO_VALID_MASK \
                                                                         | VMX_BF_EXIT_INT_INFO_TYPE_MASK \
                                                                         | VMX_BF_EXIT_INT_INFO_VECTOR_MASK)) \
                                                                     == (  RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VALID,  1) \
                                                                         | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_TYPE,   VMX_EXIT_INT_INFO_TYPE_HW_XCPT) \
                                                                         | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VECTOR, X86_XCPT_DF)))
/** If the VM-exit interruption information field indicates an NMI. */
#define VMX_EXIT_INT_INFO_IS_XCPT_NMI(a)                        (((a) & (  VMX_BF_EXIT_INT_INFO_VALID_MASK \
                                                                         | VMX_BF_EXIT_INT_INFO_TYPE_MASK \
                                                                         | VMX_BF_EXIT_INT_INFO_VECTOR_MASK)) \
                                                                     == (  RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VALID,  1) \
                                                                         | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_TYPE,   VMX_EXIT_INT_INFO_TYPE_NMI) \
                                                                         | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VECTOR, X86_XCPT_NMI)))


/** Bit fields for VM-exit interruption infomration. */
/** The VM-exit interruption vector. */
#define VMX_BF_EXIT_INT_INFO_VECTOR_SHIFT                       0
#define VMX_BF_EXIT_INT_INFO_VECTOR_MASK                        UINT32_C(0x000000ff)
/** The VM-exit interruption type (see VMX_EXIT_INT_INFO_TYPE_XXX). */
#define VMX_BF_EXIT_INT_INFO_TYPE_SHIFT                         8
#define VMX_BF_EXIT_INT_INFO_TYPE_MASK                          UINT32_C(0x00000700)
/** Whether this event has an error code. */
#define VMX_BF_EXIT_INT_INFO_ERR_CODE_VALID_SHIFT               11
#define VMX_BF_EXIT_INT_INFO_ERR_CODE_VALID_MASK                UINT32_C(0x00000800)
/** Whether NMI-unblocking due to IRET is active. */
#define VMX_BF_EXIT_INT_INFO_NMI_UNBLOCK_IRET_SHIFT             12
#define VMX_BF_EXIT_INT_INFO_NMI_UNBLOCK_IRET_MASK              UINT32_C(0x00001000)
/** Bits 13:30 is reserved (MBZ). */
#define VMX_BF_EXIT_INT_INFO_RSVD_13_30_SHIFT                   13
#define VMX_BF_EXIT_INT_INFO_RSVD_13_30_MASK                    UINT32_C(0x7fffe000)
/** Whether this VM-exit interruption info is valid. */
#define VMX_BF_EXIT_INT_INFO_VALID_SHIFT                        31
#define VMX_BF_EXIT_INT_INFO_VALID_MASK                         UINT32_C(0x80000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_INT_INFO_, UINT32_C(0), UINT32_MAX,
                            (VECTOR, TYPE, ERR_CODE_VALID, NMI_UNBLOCK_IRET, RSVD_13_30, VALID));
/** @} */


/** @name VM-exit interruption information types.
 * @{
 */
#define VMX_EXIT_INT_INFO_TYPE_EXT_INT                          0
#define VMX_EXIT_INT_INFO_TYPE_NMI                              2
#define VMX_EXIT_INT_INFO_TYPE_HW_XCPT                          3
#define VMX_EXIT_INT_INFO_TYPE_SW_INT                           4
#define VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT                     5
#define VMX_EXIT_INT_INFO_TYPE_SW_XCPT                          6
#define VMX_EXIT_INT_INFO_TYPE_UNUSED                           7
/** @} */


/** @name VM-exit instruction identity.
 *
 * These are found in VM-exit instruction information fields for certain
 * instructions.
 * @{ */
typedef uint32_t VMXINSTRID;
/** Whether the instruction ID field is valid. */
#define VMXINSTRID_VALID                                        RT_BIT_32(31)
/** Whether the instruction's primary operand in the Mod R/M byte (bits 0:3) is a
 *  read or write. */
#define VMXINSTRID_MODRM_PRIMARY_OP_W                           RT_BIT_32(30)
/** Gets whether the instruction ID is valid or not. */
#define VMXINSTRID_IS_VALID(a)                                  (((a) >> 31) & 1)
#define VMXINSTRID_IS_MODRM_PRIMARY_OP_W(a)                     (((a) >> 30) & 1)
/** Gets the instruction ID. */
#define VMXINSTRID_GET_ID(a)                                    ((a) & ~(VMXINSTRID_VALID | VMXINSTRID_MODRM_PRIMARY_OP_W))
/** No instruction ID info. */
#define VMXINSTRID_NONE                                         0

/** The OR'd rvalues are from the VT-x spec (valid bit is VBox specific): */
#define VMXINSTRID_SGDT                                         (0x0 | VMXINSTRID_VALID | VMXINSTRID_MODRM_PRIMARY_OP_W)
#define VMXINSTRID_SIDT                                         (0x1 | VMXINSTRID_VALID | VMXINSTRID_MODRM_PRIMARY_OP_W)
#define VMXINSTRID_LGDT                                         (0x2 | VMXINSTRID_VALID)
#define VMXINSTRID_LIDT                                         (0x3 | VMXINSTRID_VALID)

#define VMXINSTRID_SLDT                                         (0x0 | VMXINSTRID_VALID | VMXINSTRID_MODRM_PRIMARY_OP_W)
#define VMXINSTRID_STR                                          (0x1 | VMXINSTRID_VALID | VMXINSTRID_MODRM_PRIMARY_OP_W)
#define VMXINSTRID_LLDT                                         (0x2 | VMXINSTRID_VALID)
#define VMXINSTRID_LTR                                          (0x3 | VMXINSTRID_VALID)

/** The following IDs are used internally (some for logging, others for conveying
 *  the ModR/M primary operand write bit): */
#define VMXINSTRID_VMLAUNCH                                     (0x10 | VMXINSTRID_VALID)
#define VMXINSTRID_VMRESUME                                     (0x11 | VMXINSTRID_VALID)
#define VMXINSTRID_VMREAD                                       (0x12 | VMXINSTRID_VALID)
#define VMXINSTRID_VMWRITE                                      (0x13 | VMXINSTRID_VALID | VMXINSTRID_MODRM_PRIMARY_OP_W)
#define VMXINSTRID_IO_IN                                        (0x14 | VMXINSTRID_VALID)
#define VMXINSTRID_IO_INS                                       (0x15 | VMXINSTRID_VALID)
#define VMXINSTRID_IO_OUT                                       (0x16 | VMXINSTRID_VALID)
#define VMXINSTRID_IO_OUTS                                      (0x17 | VMXINSTRID_VALID)
#define VMXINSTRID_MOV_TO_DRX                                   (0x18 | VMXINSTRID_VALID)
#define VMXINSTRID_MOV_FROM_DRX                                 (0x19 | VMXINSTRID_VALID)
#define VMXINSTRID_RDSEED                                       (0x20 | VMXINSTRID_VALID | VMXINSTRID_MODRM_PRIMARY_OP_W)
#define VMXINSTRID_RDRAND                                       (0x21 | VMXINSTRID_VALID | VMXINSTRID_MODRM_PRIMARY_OP_W)
/** @} */


/** @name IDT-vectoring information.
 * @{
 */
#define VMX_IDT_VECTORING_INFO_VECTOR(a)                        ((a) & 0xff)
#define VMX_IDT_VECTORING_INFO_TYPE_SHIFT                       8
#define VMX_IDT_VECTORING_INFO_TYPE(a)                          (((a) >> 8) & 7)
#define VMX_IDT_VECTORING_INFO_ERROR_CODE_VALID                 RT_BIT(11)
#define VMX_IDT_VECTORING_INFO_IS_ERROR_CODE_VALID(a)           (((a) >> 11) & 1)
#define VMX_IDT_VECTORING_INFO_IS_VALID(a)                      (((a) >> 31) & 1)
#define VMX_IDT_VECTORING_INFO_VALID                            RT_BIT(31)

/** Construct an IDT-vectoring information field from an VM-entry interruption
 *  information field (same except that bit 12 is reserved). */
#define VMX_IDT_VECTORING_INFO_FROM_ENTRY_INT_INFO(a)           ((a) & ~RT_BIT(12))
/** If the IDT-vectoring information field indicates a page-fault. */
#define VMX_IDT_VECTORING_INFO_IS_XCPT_PF(a)                    (((a) & (  VMX_BF_IDT_VECTORING_INFO_VALID_MASK \
                                                                         | VMX_BF_IDT_VECTORING_INFO_TYPE_MASK \
                                                                         | VMX_BF_IDT_VECTORING_INFO_VECTOR_MASK)) \
                                                                     == (  RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_VALID,  1) \
                                                                         | RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_TYPE,   VMX_IDT_VECTORING_INFO_TYPE_HW_XCPT) \
                                                                         | RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_VECTOR, X86_XCPT_PF)))
/** If the IDT-vectoring information field indicates an NMI. */
#define VMX_IDT_VECTORING_INFO_IS_XCPT_NMI(a)                   (((a) & (  VMX_BF_IDT_VECTORING_INFO_VALID_MASK \
                                                                         | VMX_BF_IDT_VECTORING_INFO_TYPE_MASK \
                                                                         | VMX_BF_IDT_VECTORING_INFO_VECTOR_MASK)) \
                                                                     == (  RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_VALID,  1) \
                                                                         | RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_TYPE,   VMX_IDT_VECTORING_INFO_TYPE_NMI) \
                                                                         | RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_VECTOR, X86_XCPT_NMI)))


/** Bit fields for IDT-vectoring information. */
/** The IDT-vectoring info vector. */
#define VMX_BF_IDT_VECTORING_INFO_VECTOR_SHIFT                  0
#define VMX_BF_IDT_VECTORING_INFO_VECTOR_MASK                   UINT32_C(0x000000ff)
/** The IDT-vectoring info type (see VMX_IDT_VECTORING_INFO_TYPE_XXX). */
#define VMX_BF_IDT_VECTORING_INFO_TYPE_SHIFT                    8
#define VMX_BF_IDT_VECTORING_INFO_TYPE_MASK                     UINT32_C(0x00000700)
/** Whether the event has an error code. */
#define VMX_BF_IDT_VECTORING_INFO_ERR_CODE_VALID_SHIFT          11
#define VMX_BF_IDT_VECTORING_INFO_ERR_CODE_VALID_MASK           UINT32_C(0x00000800)
/** Bit 12 is undefined. */
#define VMX_BF_IDT_VECTORING_INFO_UNDEF_12_SHIFT                12
#define VMX_BF_IDT_VECTORING_INFO_UNDEF_12_MASK                 UINT32_C(0x00001000)
/** Bits 13:30 is reserved (MBZ). */
#define VMX_BF_IDT_VECTORING_INFO_RSVD_13_30_SHIFT              13
#define VMX_BF_IDT_VECTORING_INFO_RSVD_13_30_MASK               UINT32_C(0x7fffe000)
/** Whether this IDT-vectoring info is valid. */
#define VMX_BF_IDT_VECTORING_INFO_VALID_SHIFT                   31
#define VMX_BF_IDT_VECTORING_INFO_VALID_MASK                    UINT32_C(0x80000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_IDT_VECTORING_INFO_, UINT32_C(0), UINT32_MAX,
                            (VECTOR, TYPE, ERR_CODE_VALID, UNDEF_12, RSVD_13_30, VALID));
/** @} */


/** @name IDT-vectoring information vector types.
 * @{
 */
#define VMX_IDT_VECTORING_INFO_TYPE_EXT_INT                     0
#define VMX_IDT_VECTORING_INFO_TYPE_NMI                         2
#define VMX_IDT_VECTORING_INFO_TYPE_HW_XCPT                     3
#define VMX_IDT_VECTORING_INFO_TYPE_SW_INT                      4
#define VMX_IDT_VECTORING_INFO_TYPE_PRIV_SW_XCPT                5
#define VMX_IDT_VECTORING_INFO_TYPE_SW_XCPT                     6
#define VMX_IDT_VECTORING_INFO_TYPE_UNUSED                      7
/** @} */


/** @name TPR threshold.
 * @{ */
/** Mask of the TPR threshold field (bits 31:4 MBZ). */
#define VMX_TPR_THRESHOLD_MASK                                   UINT32_C(0xf)

/** Bit fields for TPR threshold. */
#define VMX_BF_TPR_THRESHOLD_TPR_SHIFT                           0
#define VMX_BF_TPR_THRESHOLD_TPR_MASK                            UINT32_C(0x0000000f)
#define VMX_BF_TPR_THRESHOLD_RSVD_4_31_SHIFT                     4
#define VMX_BF_TPR_THRESHOLD_RSVD_4_31_MASK                      UINT32_C(0xfffffff0)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_TPR_THRESHOLD_, UINT32_C(0), UINT32_MAX,
                            (TPR, RSVD_4_31));
/** @} */


/** @name Guest-activity states.
 * @{
 */
/** The logical processor is active. */
#define VMX_VMCS_GUEST_ACTIVITY_ACTIVE                          0x0
/** The logical processor is inactive, because it executed a HLT instruction. */
#define VMX_VMCS_GUEST_ACTIVITY_HLT                             0x1
/** The logical processor is inactive, because of a triple fault or other serious error. */
#define VMX_VMCS_GUEST_ACTIVITY_SHUTDOWN                        0x2
/** The logical processor is inactive, because it's waiting for a startup-IPI */
#define VMX_VMCS_GUEST_ACTIVITY_SIPI_WAIT                       0x3
/** @} */


/** @name Guest-interruptibility states.
 * @{
 */
#define VMX_VMCS_GUEST_INT_STATE_BLOCK_STI                      RT_BIT(0)
#define VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS                    RT_BIT(1)
#define VMX_VMCS_GUEST_INT_STATE_BLOCK_SMI                      RT_BIT(2)
#define VMX_VMCS_GUEST_INT_STATE_BLOCK_NMI                      RT_BIT(3)
#define VMX_VMCS_GUEST_INT_STATE_ENCLAVE                        RT_BIT(4)

/** Mask of the guest-interruptibility state field (bits 31:5 MBZ). */
#define VMX_VMCS_GUEST_INT_STATE_MASK                           UINT32_C(0x1f)
/** @} */


/** @name Exit qualification for debug exceptions.
 * @{
 */
/** Hardware breakpoint 0 was met. */
#define VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BP0                       RT_BIT_64(0)
/** Hardware breakpoint 1 was met. */
#define VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BP1                       RT_BIT_64(1)
/** Hardware breakpoint 2 was met. */
#define VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BP2                       RT_BIT_64(2)
/** Hardware breakpoint 3 was met. */
#define VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BP3                       RT_BIT_64(3)
/** Debug register access detected. */
#define VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BD                        RT_BIT_64(13)
/** A debug exception would have been triggered by single-step execution mode. */
#define VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BS                        RT_BIT_64(14)
/** Mask of all valid bits. */
#define VMX_VMCS_EXIT_QUAL_VALID_MASK                           (  VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BP0 \
                                                                 | VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BP1 \
                                                                 | VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BP2 \
                                                                 | VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BP3 \
                                                                 | VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BD  \
                                                                 | VMX_VMCS_EXIT_QUAL_DEBUG_XCPT_BS)

/** Bit fields for Exit qualifications due to debug exceptions. */
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BP0_SHIFT                   0
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BP0_MASK                    UINT64_C(0x0000000000000001)
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BP1_SHIFT                   1
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BP1_MASK                    UINT64_C(0x0000000000000002)
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BP2_SHIFT                   2
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BP2_MASK                    UINT64_C(0x0000000000000004)
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BP3_SHIFT                   3
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BP3_MASK                    UINT64_C(0x0000000000000008)
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_RSVD_4_12_SHIFT             4
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_RSVD_4_12_MASK              UINT64_C(0x0000000000001ff0)
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BD_SHIFT                    13
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BD_MASK                     UINT64_C(0x0000000000002000)
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BS_SHIFT                    14
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_BS_MASK                     UINT64_C(0x0000000000004000)
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_RSVD_15_63_SHIFT            15
#define VMX_BF_EXIT_QUAL_DEBUG_XCPT_RSVD_15_63_MASK             UINT64_C(0xffffffffffff8000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_QUAL_DEBUG_XCPT_, UINT64_C(0), UINT64_MAX,
                            (BP0, BP1, BP2, BP3, RSVD_4_12, BD, BS, RSVD_15_63));
/** @} */

/** @name Exit qualification for Mov DRx.
 * @{
 */
/** 0-2:  Debug register number */
#define VMX_EXIT_QUAL_DRX_REGISTER(a)                           ((a) & 7)
/** 3:    Reserved; cleared to 0. */
#define VMX_EXIT_QUAL_DRX_RES1(a)                               (((a) >> 3) & 1)
/** 4:    Direction of move (0 = write, 1 = read) */
#define VMX_EXIT_QUAL_DRX_DIRECTION(a)                          (((a) >> 4) & 1)
/** 5-7:  Reserved; cleared to 0. */
#define VMX_EXIT_QUAL_DRX_RES2(a)                               (((a) >> 5) & 7)
/** 8-11: General purpose register number. */
#define VMX_EXIT_QUAL_DRX_GENREG(a)                             (((a) >> 8) & 0xf)

/** Bit fields for Exit qualification due to Mov DRx. */
#define VMX_BF_EXIT_QUAL_DRX_REGISTER_SHIFT                     0
#define VMX_BF_EXIT_QUAL_DRX_REGISTER_MASK                      UINT64_C(0x0000000000000007)
#define VMX_BF_EXIT_QUAL_DRX_RSVD_1_SHIFT                       3
#define VMX_BF_EXIT_QUAL_DRX_RSVD_1_MASK                        UINT64_C(0x0000000000000008)
#define VMX_BF_EXIT_QUAL_DRX_DIRECTION_SHIFT                    4
#define VMX_BF_EXIT_QUAL_DRX_DIRECTION_MASK                     UINT64_C(0x0000000000000010)
#define VMX_BF_EXIT_QUAL_DRX_RSVD_5_7_SHIFT                     5
#define VMX_BF_EXIT_QUAL_DRX_RSVD_5_7_MASK                      UINT64_C(0x00000000000000e0)
#define VMX_BF_EXIT_QUAL_DRX_GENREG_SHIFT                       8
#define VMX_BF_EXIT_QUAL_DRX_GENREG_MASK                        UINT64_C(0x0000000000000f00)
#define VMX_BF_EXIT_QUAL_DRX_RSVD_12_63_SHIFT                   12
#define VMX_BF_EXIT_QUAL_DRX_RSVD_12_63_MASK                    UINT64_C(0xfffffffffffff000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_QUAL_DRX_, UINT64_C(0), UINT64_MAX,
                            (REGISTER, RSVD_1, DIRECTION, RSVD_5_7, GENREG, RSVD_12_63));
/** @} */


/** @name Exit qualification for debug exceptions types.
 * @{
 */
#define VMX_EXIT_QUAL_DRX_DIRECTION_WRITE                       0
#define VMX_EXIT_QUAL_DRX_DIRECTION_READ                        1
/** @} */


/** @name Exit qualification for control-register accesses.
 * @{
 */
/** 0-3:   Control register number (0 for CLTS & LMSW) */
#define VMX_EXIT_QUAL_CRX_REGISTER(a)                           ((a) & 0xf)
/** 4-5:   Access type. */
#define VMX_EXIT_QUAL_CRX_ACCESS(a)                             (((a) >> 4) & 3)
/** 6:     LMSW operand type memory (1 for memory, 0 for register). */
#define VMX_EXIT_QUAL_CRX_LMSW_OP_MEM(a)                        (((a) >> 6) & 1)
/** 7:     Reserved; cleared to 0. */
#define VMX_EXIT_QUAL_CRX_RES1(a)                               (((a) >> 7) & 1)
/** 8-11:  General purpose register number (0 for CLTS & LMSW). */
#define VMX_EXIT_QUAL_CRX_GENREG(a)                             (((a) >> 8) & 0xf)
/** 12-15: Reserved; cleared to 0. */
#define VMX_EXIT_QUAL_CRX_RES2(a)                               (((a) >> 12) & 0xf)
/** 16-31: LMSW source data (else 0). */
#define VMX_EXIT_QUAL_CRX_LMSW_DATA(a)                          (((a) >> 16) & 0xffff)

/** Bit fields for Exit qualification for control-register accesses. */
#define VMX_BF_EXIT_QUAL_CRX_REGISTER_SHIFT                     0
#define VMX_BF_EXIT_QUAL_CRX_REGISTER_MASK                      UINT64_C(0x000000000000000f)
#define VMX_BF_EXIT_QUAL_CRX_ACCESS_SHIFT                       4
#define VMX_BF_EXIT_QUAL_CRX_ACCESS_MASK                        UINT64_C(0x0000000000000030)
#define VMX_BF_EXIT_QUAL_CRX_LMSW_OP_SHIFT                      6
#define VMX_BF_EXIT_QUAL_CRX_LMSW_OP_MASK                       UINT64_C(0x0000000000000040)
#define VMX_BF_EXIT_QUAL_CRX_RSVD_7_SHIFT                       7
#define VMX_BF_EXIT_QUAL_CRX_RSVD_7_MASK                        UINT64_C(0x0000000000000080)
#define VMX_BF_EXIT_QUAL_CRX_GENREG_SHIFT                       8
#define VMX_BF_EXIT_QUAL_CRX_GENREG_MASK                        UINT64_C(0x0000000000000f00)
#define VMX_BF_EXIT_QUAL_CRX_RSVD_12_15_SHIFT                   12
#define VMX_BF_EXIT_QUAL_CRX_RSVD_12_15_MASK                    UINT64_C(0x000000000000f000)
#define VMX_BF_EXIT_QUAL_CRX_LMSW_DATA_SHIFT                    16
#define VMX_BF_EXIT_QUAL_CRX_LMSW_DATA_MASK                     UINT64_C(0x00000000ffff0000)
#define VMX_BF_EXIT_QUAL_CRX_RSVD_32_63_SHIFT                   32
#define VMX_BF_EXIT_QUAL_CRX_RSVD_32_63_MASK                    UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_QUAL_CRX_, UINT64_C(0), UINT64_MAX,
                            (REGISTER, ACCESS, LMSW_OP, RSVD_7, GENREG, RSVD_12_15, LMSW_DATA, RSVD_32_63));
/** @} */


/** @name Exit qualification for control-register access types.
 * @{
 */
#define VMX_EXIT_QUAL_CRX_ACCESS_WRITE                          0
#define VMX_EXIT_QUAL_CRX_ACCESS_READ                           1
#define VMX_EXIT_QUAL_CRX_ACCESS_CLTS                           2
#define VMX_EXIT_QUAL_CRX_ACCESS_LMSW                           3
/** @} */


/** @name Exit qualification for task switch.
 * @{
 */
#define VMX_EXIT_QUAL_TASK_SWITCH_SELECTOR(a)                   ((a) & 0xffff)
#define VMX_EXIT_QUAL_TASK_SWITCH_TYPE(a)                       (((a) >> 30) & 0x3)
/** Task switch caused by a call instruction. */
#define VMX_EXIT_QUAL_TASK_SWITCH_TYPE_CALL                     0
/** Task switch caused by an iret instruction. */
#define VMX_EXIT_QUAL_TASK_SWITCH_TYPE_IRET                     1
/** Task switch caused by a jmp instruction. */
#define VMX_EXIT_QUAL_TASK_SWITCH_TYPE_JMP                      2
/** Task switch caused by an interrupt gate. */
#define VMX_EXIT_QUAL_TASK_SWITCH_TYPE_IDT                      3

/** Bit fields for Exit qualification for task switches. */
#define VMX_BF_EXIT_QUAL_TASK_SWITCH_NEW_TSS_SHIFT              0
#define VMX_BF_EXIT_QUAL_TASK_SWITCH_NEW_TSS_MASK               UINT64_C(0x000000000000ffff)
#define VMX_BF_EXIT_QUAL_TASK_SWITCH_RSVD_16_29_SHIFT           16
#define VMX_BF_EXIT_QUAL_TASK_SWITCH_RSVD_16_29_MASK            UINT64_C(0x000000003fff0000)
#define VMX_BF_EXIT_QUAL_TASK_SWITCH_SOURCE_SHIFT               30
#define VMX_BF_EXIT_QUAL_TASK_SWITCH_SOURCE_MASK                UINT64_C(0x00000000c0000000)
#define VMX_BF_EXIT_QUAL_TASK_SWITCH_RSVD_32_63_SHIFT           32
#define VMX_BF_EXIT_QUAL_TASK_SWITCH_RSVD_32_63_MASK            UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_QUAL_TASK_SWITCH_, UINT64_C(0), UINT64_MAX,
                            (NEW_TSS, RSVD_16_29, SOURCE, RSVD_32_63));
/** @} */


/** @name Exit qualification for EPT violations.
 * @{
 */
/** Set if acess causing the violation was a data read. */
#define VMX_EXIT_QUAL_EPT_ACCESS_READ                           RT_BIT_64(0)
/** Set if acess causing the violation was a data write. */
#define VMX_EXIT_QUAL_EPT_ACCESS_WRITE                          RT_BIT_64(1)
/** Set if the violation was caused by an instruction fetch. */
#define VMX_EXIT_QUAL_EPT_ACCESS_INSTR_FETCH                    RT_BIT_64(2)
/** AND of the read bit of all EPT structures. */
#define VMX_EXIT_QUAL_EPT_ENTRY_READ                            RT_BIT_64(3)
/** AND of the write bit of all EPT structures. */
#define VMX_EXIT_QUAL_EPT_ENTRY_WRITE                           RT_BIT_64(4)
/** AND of the execute bit of all EPT structures. */
#define VMX_EXIT_QUAL_EPT_ENTRY_EXECUTE                         RT_BIT_64(5)
/** And of the execute bit of all EPT structures for user-mode addresses
 *  (requires mode-based execute control). */
#define VMX_EXIT_QUAL_EPT_ENTRY_EXECUTE_USER                    RT_BIT_64(6)
/** Set if the guest linear address field is valid. */
#define VMX_EXIT_QUAL_EPT_LINEAR_ADDR_VALID                     RT_BIT_64(7)
/** If bit 7 is one: (reserved otherwise)
 *  1 - violation due to physical address access.
 *  0 - violation caused by page walk or access/dirty bit updates.
 */
#define VMX_EXIT_QUAL_EPT_LINEAR_TO_PHYS_ADDR                   RT_BIT_64(8)
/** If bit 7, 8 and advanced VM-exit info. for EPT is one: (reserved otherwise)
 *  1 - linear address is user-mode address.
 *  0 - linear address is supervisor-mode address.
 */
#define VMX_EXIT_QUAL_EPT_LINEAR_ADDR_USER                      RT_BIT_64(9)
/** If bit 7, 8 and advanced VM-exit info. for EPT is one: (reserved otherwise)
 *  1 - linear address translates to read-only page.
 *  0 - linear address translates to read-write page.
 */
#define VMX_EXIT_QUAL_EPT_LINEAR_ADDR_RO                        RT_BIT_64(10)
/** If bit 7, 8 and advanced VM-exit info. for EPT is one: (reserved otherwise)
 *  1 - linear address translates to executable-disabled page.
 *  0 - linear address translates to executable page.
 */
#define VMX_EXIT_QUAL_EPT_LINEAR_ADDR_XD                        RT_BIT_64(11)
/** NMI unblocking due to IRET. */
#define VMX_EXIT_QUAL_EPT_NMI_UNBLOCK_IRET                      RT_BIT_64(12)
/** Set if acess causing the violation was a shadow-stack access. */
#define VMX_EXIT_QUAL_EPT_ACCESS_SHW_STACK                      RT_BIT_64(13)
/** If supervisor-shadow stack is enabled: (reserved otherwise)
 *  1 - supervisor shadow-stack access allowed.
 *  0 - supervisor shadow-stack access disallowed.
 */
#define VMX_EXIT_QUAL_EPT_ENTRY_SHW_STACK_SUPER                 RT_BIT_64(14)
/** Set if access is related to trace output by Intel PT (reserved otherwise). */
#define VMX_EXIT_QUAL_EPT_ACCESS_PT_TRACE                       RT_BIT_64(16)

/** Checks whether NMI unblocking due to IRET. */
#define VMX_EXIT_QUAL_EPT_IS_NMI_UNBLOCK_IRET(a)                (((a) >> 12) & 1)

/** Bit fields for Exit qualification for EPT violations. */
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_READ_SHIFT                  0
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_READ_MASK                   UINT64_C(0x0000000000000001)
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_WRITE_SHIFT                 1
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_WRITE_MASK                  UINT64_C(0x0000000000000002)
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_INSTR_FETCH_SHIFT           2
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_INSTR_FETCH_MASK            UINT64_C(0x0000000000000004)
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_READ_SHIFT                   3
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_READ_MASK                    UINT64_C(0x0000000000000008)
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_WRITE_SHIFT                  4
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_WRITE_MASK                   UINT64_C(0x0000000000000010)
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_EXECUTE_SHIFT                5
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_EXECUTE_MASK                 UINT64_C(0x0000000000000020)
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_EXECUTE_USER_SHIFT           6
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_EXECUTE_USER_MASK            UINT64_C(0x0000000000000040)
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_VALID_SHIFT            7
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_VALID_MASK             UINT64_C(0x0000000000000080)
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_TO_PHYS_ADDR_SHIFT          8
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_TO_PHYS_ADDR_MASK           UINT64_C(0x0000000000000100)
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_USER_SHIFT             9
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_USER_MASK              UINT64_C(0x0000000000000200)
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_RO_SHIFT               10
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_RO_MASK                UINT64_C(0x0000000000000400)
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_XD_SHIFT               11
#define VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_XD_MASK                UINT64_C(0x0000000000000800)
#define VMX_BF_EXIT_QUAL_EPT_NMI_UNBLOCK_IRET_SHIFT             12
#define VMX_BF_EXIT_QUAL_EPT_NMI_UNBLOCK_IRET_MASK              UINT64_C(0x0000000000001000)
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_SHW_STACK_SHIFT             13
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_SHW_STACK_MASK              UINT64_C(0x0000000000002000)
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_SHW_STACK_SUPER_SHIFT         14
#define VMX_BF_EXIT_QUAL_EPT_ENTRY_SHW_STACK_SUPER_MASK          UINT64_C(0x0000000000004000)
#define VMX_BF_EXIT_QUAL_EPT_RSVD_15_SHIFT                      15
#define VMX_BF_EXIT_QUAL_EPT_RSVD_15_MASK                       UINT64_C(0x0000000000008000)
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_PT_TRACE_SHIFT              16
#define VMX_BF_EXIT_QUAL_EPT_ACCESS_PT_TRACE_MASK               UINT64_C(0x0000000000010000)
#define VMX_BF_EXIT_QUAL_EPT_RSVD_17_63_SHIFT                   17
#define VMX_BF_EXIT_QUAL_EPT_RSVD_17_63_MASK                    UINT64_C(0xfffffffffffe0000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_QUAL_EPT_, UINT64_C(0), UINT64_MAX,
                            (ACCESS_READ, ACCESS_WRITE, ACCESS_INSTR_FETCH, ENTRY_READ, ENTRY_WRITE, ENTRY_EXECUTE,
                             ENTRY_EXECUTE_USER, LINEAR_ADDR_VALID, LINEAR_TO_PHYS_ADDR, LINEAR_ADDR_USER, LINEAR_ADDR_RO,
                             LINEAR_ADDR_XD, NMI_UNBLOCK_IRET, ACCESS_SHW_STACK, ENTRY_SHW_STACK_SUPER, RSVD_15,
                             ACCESS_PT_TRACE, RSVD_17_63));
/** @} */


/** @name Exit qualification for I/O instructions.
 * @{
 */
/** 0-2:   IO operation size 0(=1 byte), 1(=2 bytes) and 3(=4 bytes). */
#define VMX_EXIT_QUAL_IO_SIZE(a)                                ((a) & 7)
/** 3:     IO operation direction. */
#define VMX_EXIT_QUAL_IO_DIRECTION(a)                           (((a) >> 3) & 1)
/** 4:     String IO operation (INS / OUTS). */
#define VMX_EXIT_QUAL_IO_IS_STRING(a)                           (((a) >> 4) & 1)
/** 5:     Repeated IO operation. */
#define VMX_EXIT_QUAL_IO_IS_REP(a)                              (((a) >> 5) & 1)
/** 6:     Operand encoding. */
#define VMX_EXIT_QUAL_IO_ENCODING(a)                            (((a) >> 6) & 1)
/** 16-31: IO Port (0-0xffff). */
#define VMX_EXIT_QUAL_IO_PORT(a)                                (((a) >> 16) & 0xffff)

/** Bit fields for Exit qualification for I/O instructions. */
#define VMX_BF_EXIT_QUAL_IO_WIDTH_SHIFT                         0
#define VMX_BF_EXIT_QUAL_IO_WIDTH_MASK                          UINT64_C(0x0000000000000007)
#define VMX_BF_EXIT_QUAL_IO_DIRECTION_SHIFT                     3
#define VMX_BF_EXIT_QUAL_IO_DIRECTION_MASK                      UINT64_C(0x0000000000000008)
#define VMX_BF_EXIT_QUAL_IO_IS_STRING_SHIFT                     4
#define VMX_BF_EXIT_QUAL_IO_IS_STRING_MASK                      UINT64_C(0x0000000000000010)
#define VMX_BF_EXIT_QUAL_IO_IS_REP_SHIFT                        5
#define VMX_BF_EXIT_QUAL_IO_IS_REP_MASK                         UINT64_C(0x0000000000000020)
#define VMX_BF_EXIT_QUAL_IO_ENCODING_SHIFT                      6
#define VMX_BF_EXIT_QUAL_IO_ENCODING_MASK                       UINT64_C(0x0000000000000040)
#define VMX_BF_EXIT_QUAL_IO_RSVD_7_15_SHIFT                     7
#define VMX_BF_EXIT_QUAL_IO_RSVD_7_15_MASK                      UINT64_C(0x000000000000ff80)
#define VMX_BF_EXIT_QUAL_IO_PORT_SHIFT                          16
#define VMX_BF_EXIT_QUAL_IO_PORT_MASK                           UINT64_C(0x00000000ffff0000)
#define VMX_BF_EXIT_QUAL_IO_RSVD_32_63_SHIFT                    32
#define VMX_BF_EXIT_QUAL_IO_RSVD_32_63_MASK                     UINT64_C(0xffffffff00000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_QUAL_IO_, UINT64_C(0), UINT64_MAX,
                            (WIDTH, DIRECTION, IS_STRING, IS_REP, ENCODING, RSVD_7_15, PORT, RSVD_32_63));
/** @} */


/** @name Exit qualification for I/O instruction types.
 * @{
 */
#define VMX_EXIT_QUAL_IO_DIRECTION_OUT                          0
#define VMX_EXIT_QUAL_IO_DIRECTION_IN                           1
/** @} */


/** @name Exit qualification for I/O instruction encoding.
 * @{
 */
#define VMX_EXIT_QUAL_IO_ENCODING_DX                            0
#define VMX_EXIT_QUAL_IO_ENCODING_IMM                           1
/** @} */


/** @name Exit qualification for APIC-access VM-exits from linear and
 *        guest-physical accesses.
 * @{
 */
/** 0-11: If the APIC-access VM-exit is due to a linear access, the offset of
 *  access within the APIC page. */
#define VMX_EXIT_QUAL_APIC_ACCESS_OFFSET(a)                     ((a) & 0xfff)
/** 12-15: Access type. */
#define VMX_EXIT_QUAL_APIC_ACCESS_TYPE(a)                       (((a) & 0xf000) >> 12)
/* Rest reserved. */

/** Bit fields for Exit qualification for APIC-access VM-exits. */
#define VMX_BF_EXIT_QUAL_APIC_ACCESS_OFFSET_SHIFT               0
#define VMX_BF_EXIT_QUAL_APIC_ACCESS_OFFSET_MASK                UINT64_C(0x0000000000000fff)
#define VMX_BF_EXIT_QUAL_APIC_ACCESS_TYPE_SHIFT                 12
#define VMX_BF_EXIT_QUAL_APIC_ACCESS_TYPE_MASK                  UINT64_C(0x000000000000f000)
#define VMX_BF_EXIT_QUAL_APIC_ACCESS_RSVD_16_63_SHIFT           16
#define VMX_BF_EXIT_QUAL_APIC_ACCESS_RSVD_16_63_MASK            UINT64_C(0xffffffffffff0000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_EXIT_QUAL_APIC_ACCESS_, UINT64_C(0), UINT64_MAX,
                            (OFFSET, TYPE, RSVD_16_63));
/** @} */


/** @name Exit qualification for linear address APIC-access types.
 * @{
 */
/** Linear access for a data read during instruction execution. */
#define VMX_APIC_ACCESS_TYPE_LINEAR_READ                        0
/** Linear access for a data write during instruction execution. */
#define VMX_APIC_ACCESS_TYPE_LINEAR_WRITE                       1
/** Linear access for an instruction fetch. */
#define VMX_APIC_ACCESS_TYPE_LINEAR_INSTR_FETCH                 2
/** Linear read/write access during event delivery. */
#define VMX_APIC_ACCESS_TYPE_LINEAR_EVENT_DELIVERY              3
/** Physical read/write access during event delivery. */
#define VMX_APIC_ACCESS_TYPE_PHYSICAL_EVENT_DELIVERY            10
/** Physical access for an instruction fetch or during instruction execution. */
#define VMX_APIC_ACCESS_TYPE_PHYSICAL_INSTR                     15

/**
 * APIC-access type.
 * In accordance with the VT-x spec.
 */
typedef enum
{
    VMXAPICACCESS_LINEAR_READ             = VMX_APIC_ACCESS_TYPE_LINEAR_READ,
    VMXAPICACCESS_LINEAR_WRITE            = VMX_APIC_ACCESS_TYPE_LINEAR_WRITE,
    VMXAPICACCESS_LINEAR_INSTR_FETCH      = VMX_APIC_ACCESS_TYPE_LINEAR_INSTR_FETCH,
    VMXAPICACCESS_LINEAR_EVENT_DELIVERY   = VMX_APIC_ACCESS_TYPE_LINEAR_EVENT_DELIVERY,
    VMXAPICACCESS_PHYSICAL_EVENT_DELIVERY = VMX_APIC_ACCESS_TYPE_PHYSICAL_EVENT_DELIVERY,
    VMXAPICACCESS_PHYSICAL_INSTR          = VMX_APIC_ACCESS_TYPE_PHYSICAL_INSTR
} VMXAPICACCESS;
AssertCompileSize(VMXAPICACCESS, 4);
/** @} */


/** @name VMX_BF_XXTR_INSINFO_XXX - VMX_EXIT_XDTR_ACCESS instruction information.
 * Found in VMX_VMCS32_RO_EXIT_INSTR_INFO.
 * @{
 */
/** Address calculation scaling field (powers of two). */
#define VMX_BF_XDTR_INSINFO_SCALE_SHIFT                         0
#define VMX_BF_XDTR_INSINFO_SCALE_MASK                          UINT32_C(0x00000003)
/** Bits 2 thru 6 are undefined. */
#define VMX_BF_XDTR_INSINFO_UNDEF_2_6_SHIFT                     2
#define VMX_BF_XDTR_INSINFO_UNDEF_2_6_MASK                      UINT32_C(0x0000007c)
/** Address size, only 0(=16), 1(=32) and 2(=64) are defined.
 * @remarks anyone's guess why this is a 3 bit field... */
#define VMX_BF_XDTR_INSINFO_ADDR_SIZE_SHIFT                     7
#define VMX_BF_XDTR_INSINFO_ADDR_SIZE_MASK                      UINT32_C(0x00000380)
/** Bit 10 is defined as zero. */
#define VMX_BF_XDTR_INSINFO_ZERO_10_SHIFT                       10
#define VMX_BF_XDTR_INSINFO_ZERO_10_MASK                        UINT32_C(0x00000400)
/** Operand size, either (1=)32-bit or (0=)16-bit, but get this, it's undefined
 * for exits from 64-bit code as the operand size there is fixed. */
#define VMX_BF_XDTR_INSINFO_OP_SIZE_SHIFT                       11
#define VMX_BF_XDTR_INSINFO_OP_SIZE_MASK                        UINT32_C(0x00000800)
/** Bits 12 thru 14 are undefined. */
#define VMX_BF_XDTR_INSINFO_UNDEF_12_14_SHIFT                   12
#define VMX_BF_XDTR_INSINFO_UNDEF_12_14_MASK                    UINT32_C(0x00007000)
/** Applicable segment register (X86_SREG_XXX values). */
#define VMX_BF_XDTR_INSINFO_SREG_SHIFT                          15
#define VMX_BF_XDTR_INSINFO_SREG_MASK                           UINT32_C(0x00038000)
/** Index register (X86_GREG_XXX values). Undefined if HAS_INDEX_REG is clear. */
#define VMX_BF_XDTR_INSINFO_INDEX_REG_SHIFT                     18
#define VMX_BF_XDTR_INSINFO_INDEX_REG_MASK                      UINT32_C(0x003c0000)
/** Is VMX_BF_XDTR_INSINFO_INDEX_REG_XXX valid (=1) or not (=0). */
#define VMX_BF_XDTR_INSINFO_HAS_INDEX_REG_SHIFT                 22
#define VMX_BF_XDTR_INSINFO_HAS_INDEX_REG_MASK                  UINT32_C(0x00400000)
/** Base register (X86_GREG_XXX values). Undefined if HAS_BASE_REG is clear. */
#define VMX_BF_XDTR_INSINFO_BASE_REG_SHIFT                      23
#define VMX_BF_XDTR_INSINFO_BASE_REG_MASK                       UINT32_C(0x07800000)
/** Is VMX_XDTR_INSINFO_BASE_REG_XXX valid (=1) or not (=0). */
#define VMX_BF_XDTR_INSINFO_HAS_BASE_REG_SHIFT                  27
#define VMX_BF_XDTR_INSINFO_HAS_BASE_REG_MASK                   UINT32_C(0x08000000)
/** The instruction identity (VMX_XDTR_INSINFO_II_XXX values). */
#define VMX_BF_XDTR_INSINFO_INSTR_ID_SHIFT                      28
#define VMX_BF_XDTR_INSINFO_INSTR_ID_MASK                       UINT32_C(0x30000000)
#define VMX_XDTR_INSINFO_II_SGDT                                0 /**< Instruction ID: SGDT */
#define VMX_XDTR_INSINFO_II_SIDT                                1 /**< Instruction ID: SIDT */
#define VMX_XDTR_INSINFO_II_LGDT                                2 /**< Instruction ID: LGDT */
#define VMX_XDTR_INSINFO_II_LIDT                                3 /**< Instruction ID: LIDT */
/** Bits 30 & 31 are undefined. */
#define VMX_BF_XDTR_INSINFO_UNDEF_30_31_SHIFT                   30
#define VMX_BF_XDTR_INSINFO_UNDEF_30_31_MASK                    UINT32_C(0xc0000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_XDTR_INSINFO_, UINT32_C(0), UINT32_MAX,
                            (SCALE, UNDEF_2_6, ADDR_SIZE, ZERO_10, OP_SIZE, UNDEF_12_14, SREG, INDEX_REG, HAS_INDEX_REG,
                             BASE_REG, HAS_BASE_REG, INSTR_ID, UNDEF_30_31));
/** @} */


/** @name VMX_BF_YYTR_INSINFO_XXX - VMX_EXIT_TR_ACCESS instruction information.
 * Found in VMX_VMCS32_RO_EXIT_INSTR_INFO.
 * This is similar to VMX_BF_XDTR_INSINFO_XXX.
 * @{
 */
/** Address calculation scaling field (powers of two). */
#define VMX_BF_YYTR_INSINFO_SCALE_SHIFT                         0
#define VMX_BF_YYTR_INSINFO_SCALE_MASK                          UINT32_C(0x00000003)
/** Bit 2 is undefined. */
#define VMX_BF_YYTR_INSINFO_UNDEF_2_SHIFT                       2
#define VMX_BF_YYTR_INSINFO_UNDEF_2_MASK                        UINT32_C(0x00000004)
/** Register operand 1. Undefined if VMX_YYTR_INSINFO_HAS_REG1 is clear. */
#define VMX_BF_YYTR_INSINFO_REG1_SHIFT                          3
#define VMX_BF_YYTR_INSINFO_REG1_MASK                           UINT32_C(0x00000078)
/** Address size, only 0(=16), 1(=32) and 2(=64) are defined.
 * @remarks anyone's guess why this is a 3 bit field... */
#define VMX_BF_YYTR_INSINFO_ADDR_SIZE_SHIFT                     7
#define VMX_BF_YYTR_INSINFO_ADDR_SIZE_MASK                      UINT32_C(0x00000380)
/** Is VMX_YYTR_INSINFO_REG1_XXX valid (=1) or not (=0). */
#define VMX_BF_YYTR_INSINFO_HAS_REG1_SHIFT                      10
#define VMX_BF_YYTR_INSINFO_HAS_REG1_MASK                       UINT32_C(0x00000400)
/** Bits 11 thru 14 are undefined. */
#define VMX_BF_YYTR_INSINFO_UNDEF_11_14_SHIFT                   11
#define VMX_BF_YYTR_INSINFO_UNDEF_11_14_MASK                    UINT32_C(0x00007800)
/** Applicable segment register (X86_SREG_XXX values). */
#define VMX_BF_YYTR_INSINFO_SREG_SHIFT                          15
#define VMX_BF_YYTR_INSINFO_SREG_MASK                           UINT32_C(0x00038000)
/** Index register (X86_GREG_XXX values). Undefined if HAS_INDEX_REG is clear. */
#define VMX_BF_YYTR_INSINFO_INDEX_REG_SHIFT                     18
#define VMX_BF_YYTR_INSINFO_INDEX_REG_MASK                      UINT32_C(0x003c0000)
/** Is VMX_YYTR_INSINFO_INDEX_REG_XXX valid (=1) or not (=0). */
#define VMX_BF_YYTR_INSINFO_HAS_INDEX_REG_SHIFT                 22
#define VMX_BF_YYTR_INSINFO_HAS_INDEX_REG_MASK                  UINT32_C(0x00400000)
/** Base register (X86_GREG_XXX values). Undefined if HAS_BASE_REG is clear. */
#define VMX_BF_YYTR_INSINFO_BASE_REG_SHIFT                      23
#define VMX_BF_YYTR_INSINFO_BASE_REG_MASK                       UINT32_C(0x07800000)
/** Is VMX_YYTR_INSINFO_BASE_REG_XXX valid (=1) or not (=0). */
#define VMX_BF_YYTR_INSINFO_HAS_BASE_REG_SHIFT                  27
#define VMX_BF_YYTR_INSINFO_HAS_BASE_REG_MASK                   UINT32_C(0x08000000)
/** The instruction identity (VMX_YYTR_INSINFO_II_XXX values) */
#define VMX_BF_YYTR_INSINFO_INSTR_ID_SHIFT                      28
#define VMX_BF_YYTR_INSINFO_INSTR_ID_MASK                       UINT32_C(0x30000000)
#define VMX_YYTR_INSINFO_II_SLDT                                0 /**< Instruction ID: SLDT */
#define VMX_YYTR_INSINFO_II_STR                                 1 /**< Instruction ID: STR */
#define VMX_YYTR_INSINFO_II_LLDT                                2 /**< Instruction ID: LLDT */
#define VMX_YYTR_INSINFO_II_LTR                                 3 /**< Instruction ID: LTR */
/** Bits 30 & 31 are undefined. */
#define VMX_BF_YYTR_INSINFO_UNDEF_30_31_SHIFT                   30
#define VMX_BF_YYTR_INSINFO_UNDEF_30_31_MASK                    UINT32_C(0xc0000000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_YYTR_INSINFO_, UINT32_C(0), UINT32_MAX,
                            (SCALE, UNDEF_2, REG1, ADDR_SIZE, HAS_REG1, UNDEF_11_14, SREG, INDEX_REG, HAS_INDEX_REG,
                             BASE_REG, HAS_BASE_REG, INSTR_ID, UNDEF_30_31));
/** @} */


/** @name Format of Pending-Debug-Exceptions.
 * Bits 4-11, 13, 15 and 17-63 are reserved.
 * Similar to DR6 except bit 12 (breakpoint enabled) and bit 16 (RTM) are both
 * possibly valid here but not in DR6.
 * @{
 */
/** Hardware breakpoint 0 was met. */
#define VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BP0                   RT_BIT_64(0)
/** Hardware breakpoint 1 was met. */
#define VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BP1                   RT_BIT_64(1)
/** Hardware breakpoint 2 was met. */
#define VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BP2                   RT_BIT_64(2)
/** Hardware breakpoint 3 was met. */
#define VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BP3                   RT_BIT_64(3)
/** At least one data or IO breakpoint was hit. */
#define VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_EN_BP                 RT_BIT_64(12)
/** A debug exception would have been triggered by single-step execution mode. */
#define VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BS                    RT_BIT_64(14)
/** A debug exception occurred inside an RTM region. */
#define VMX_VMCS_GUEST_PENDING_DEBUG_RTM                        RT_BIT_64(16)
/** Mask of valid bits. */
#define VMX_VMCS_GUEST_PENDING_DEBUG_VALID_MASK                 (  VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BP0 \
                                                                 | VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BP1 \
                                                                 | VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BP2 \
                                                                 | VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BP3 \
                                                                 | VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_EN_BP \
                                                                 | VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BS \
                                                                 | VMX_VMCS_GUEST_PENDING_DEBUG_RTM)
#define VMX_VMCS_GUEST_PENDING_DEBUG_RTM_MASK                   (  VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_EN_BP \
                                                                 | VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BS \
                                                                 | VMX_VMCS_GUEST_PENDING_DEBUG_RTM)
/** Bit fields for Pending debug exceptions. */
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BP0_SHIFT                  0
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BP0_MASK                   UINT64_C(0x0000000000000001)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BP1_SHIFT                  1
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BP1_MASK                   UINT64_C(0x0000000000000002)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BP2_SHIFT                  2
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BP2_MASK                   UINT64_C(0x0000000000000004)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BP3_SHIFT                  3
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BP3_MASK                   UINT64_C(0x0000000000000008)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RSVD_4_11_SHIFT            4
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RSVD_4_11_MASK             UINT64_C(0x0000000000000ff0)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_EN_BP_SHIFT                12
#define VMX_BF_VMCS_PENDING_DBG_XCPT_EN_BP_MASK                 UINT64_C(0x0000000000001000)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RSVD_13_SHIFT              13
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RSVD_13_MASK               UINT64_C(0x0000000000002000)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BS_SHIFT                   14
#define VMX_BF_VMCS_PENDING_DBG_XCPT_BS_MASK                    UINT64_C(0x0000000000004000)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RSVD_15_SHIFT              15
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RSVD_15_MASK               UINT64_C(0x0000000000008000)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RTM_SHIFT                  16
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RTM_MASK                   UINT64_C(0x0000000000010000)
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RSVD_17_63_SHIFT           17
#define VMX_BF_VMCS_PENDING_DBG_XCPT_RSVD_17_63_MASK            UINT64_C(0xfffffffffffe0000)
RT_BF_ASSERT_COMPILE_CHECKS(VMX_BF_VMCS_PENDING_DBG_XCPT_, UINT64_C(0), UINT64_MAX,
                            (BP0, BP1, BP2, BP3, RSVD_4_11, EN_BP, RSVD_13, BS, RSVD_15, RTM, RSVD_17_63));
/** @} */


/**
 * VM-exit auxiliary information.
 *
 * This includes information that isn't necessarily stored in the guest-CPU
 * context but provided as part of VM-exits.
 */
typedef struct
{
    /** The VM-exit reason. */
    uint32_t                uReason;
    /** The Exit qualification field. */
    uint64_t                u64Qual;
    /** The Guest-linear address field. */
    uint64_t                u64GuestLinearAddr;
    /** The Guest-physical address field. */
    uint64_t                u64GuestPhysAddr;
    /** The guest pending-debug exceptions. */
    uint64_t                u64GuestPendingDbgXcpts;
    /** The VM-exit instruction length. */
    uint32_t                cbInstr;
    /** The VM-exit instruction information. */
    VMXEXITINSTRINFO        InstrInfo;
    /** VM-exit interruption information. */
    uint32_t                uExitIntInfo;
    /** VM-exit interruption error code. */
    uint32_t                uExitIntErrCode;
    /** IDT-vectoring information. */
    uint32_t                uIdtVectoringInfo;
    /** IDT-vectoring error code. */
    uint32_t                uIdtVectoringErrCode;
} VMXEXITAUX;
/** Pointer to a VMXEXITAUX struct. */
typedef VMXEXITAUX *PVMXEXITAUX;
/** Pointer to a const VMXEXITAUX struct. */
typedef const VMXEXITAUX *PCVMXEXITAUX;


/** @defgroup grp_hm_vmx_virt    VMX virtualization.
 * @{
 */

/** @name Virtual VMX MSR - Miscellaneous data.
 * @{ */
/** Number of CR3-target values supported. */
#define VMX_V_CR3_TARGET_COUNT                                  4
/** Activity states supported. */
#define VMX_V_GUEST_ACTIVITY_STATE_MASK                         (VMX_VMCS_GUEST_ACTIVITY_HLT | VMX_VMCS_GUEST_ACTIVITY_SHUTDOWN)
/** VMX preemption-timer shift (Core i7-2600 taken as reference). */
#define VMX_V_PREEMPT_TIMER_SHIFT                               5
/** Maximum number of MSRs in the auto-load/store MSR areas, (n+1) * 512. */
#define VMX_V_AUTOMSR_COUNT_MAX                                 0
/** SMM MSEG revision ID. */
#define VMX_V_MSEG_REV_ID                                       0
/** @} */

/** @name VMX_V_VMCS_STATE_XXX - Virtual VMCS launch state.
 * @{ */
/** VMCS launch state clear legacy value -- deprecated!!!. */
#define VMX_V_VMCS_LAUNCH_STATE_CLEAR_LEGACY                    RT_BIT(0)
/** VMCS launch state clear. */
#define VMX_V_VMCS_LAUNCH_STATE_CLEAR                           (0)
/* Bit 1 used to be "active", no longer used. */
/* Bit 2 used to be "current", no longer used. */
/** VMCS launch state launched. */
#define VMX_V_VMCS_LAUNCH_STATE_LAUNCHED                        RT_BIT(3)
/** @} */

/** CR0 bits set here must always be set when in VMX operation. */
#define VMX_V_CR0_FIXED0                                        (X86_CR0_PE | X86_CR0_NE | X86_CR0_PG)
/** CR0 bits set here must always be set when in VMX non-root operation with
 *  unrestricted-guest control enabled. */
#define VMX_V_CR0_FIXED0_UX                                     (X86_CR0_NE)
/** CR0 bits cleared here must always be cleared when in VMX operation. */
#define VMX_V_CR0_FIXED1                                        UINT32_C(0xffffffff)
/** CR4 bits set here must always be set when in VMX operation. */
#define VMX_V_CR4_FIXED0                                        (X86_CR4_VMXE)

/** Virtual VMCS revision ID. Bump this arbitarily chosen identifier if incompatible
 *  changes to the layout of VMXVVMCS is done. Bit 31 MBZ. */
#define VMX_V_VMCS_REVISION_ID                                  UINT32_C(0x40000001)
AssertCompile(!(VMX_V_VMCS_REVISION_ID & RT_BIT(31)));

/** The size of the virtual VMCS region (we use the maximum allowed size to avoid
 *  complications when teleporation may be implemented). */
#define VMX_V_VMCS_SIZE                                         X86_PAGE_4K_SIZE
/** The size of the virtual VMCS region (in pages). */
#define VMX_V_VMCS_PAGES                                        1

/** The size of the virtual shadow VMCS region. */
#define VMX_V_SHADOW_VMCS_SIZE                                  VMX_V_VMCS_SIZE
/** The size of the virtual shadow VMCS region (in pages). */
#define VMX_V_SHADOW_VMCS_PAGES                                 VMX_V_VMCS_PAGES

/** The size of the Virtual-APIC page (in bytes). */
#define VMX_V_VIRT_APIC_SIZE                                    X86_PAGE_4K_SIZE
/** The size of the Virtual-APIC page (in pages). */
#define VMX_V_VIRT_APIC_PAGES                                   1

/** The size of the VMREAD/VMWRITE bitmap (in bytes). */
#define VMX_V_VMREAD_VMWRITE_BITMAP_SIZE                        X86_PAGE_4K_SIZE
/** The size of the VMREAD/VMWRITE-bitmap (in pages). */
#define VMX_V_VMREAD_VMWRITE_BITMAP_PAGES                       1

/** The size of the MSR bitmap (in bytes). */
#define VMX_V_MSR_BITMAP_SIZE                                   X86_PAGE_4K_SIZE
/** The size of the MSR bitmap (in pages). */
#define VMX_V_MSR_BITMAP_PAGES                                  1

/** The size of I/O bitmap A (in bytes). */
#define VMX_V_IO_BITMAP_A_SIZE                                  X86_PAGE_4K_SIZE
/** The size of I/O bitmap A (in pages). */
#define VMX_V_IO_BITMAP_A_PAGES                                 1

/** The size of I/O bitmap B (in bytes). */
#define VMX_V_IO_BITMAP_B_SIZE                                  X86_PAGE_4K_SIZE
/** The size of I/O bitmap B (in pages). */
#define VMX_V_IO_BITMAP_B_PAGES                                 1

/** The size of the auto-load/store MSR area (in bytes). */
#define VMX_V_AUTOMSR_AREA_SIZE                                 ((512 * (VMX_V_AUTOMSR_COUNT_MAX + 1)) * sizeof(VMXAUTOMSR))
/* Assert that the size is page aligned or adjust the VMX_V_AUTOMSR_AREA_PAGES macro below. */
AssertCompile(RT_ALIGN_Z(VMX_V_AUTOMSR_AREA_SIZE, X86_PAGE_4K_SIZE) == VMX_V_AUTOMSR_AREA_SIZE);
/** The size of the auto-load/store MSR area (in pages). */
#define VMX_V_AUTOMSR_AREA_PAGES                                ((VMX_V_AUTOMSR_AREA_SIZE) >> X86_PAGE_4K_SHIFT)

/** The highest index value used for supported virtual VMCS field encoding. */
#define VMX_V_VMCS_MAX_INDEX                                    RT_BF_GET(VMX_VMCS64_CTRL_EXIT2_HIGH, VMX_BF_VMCSFIELD_INDEX)

/**
 * Virtual VM-exit information.
 *
 * This is a convenience structure that bundles some VM-exit information related
 * fields together.
 */
typedef struct
{
    /** The VM-exit reason. */
    uint32_t                uReason;
    /** The VM-exit instruction length. */
    uint32_t                cbInstr;
    /** The VM-exit instruction information. */
    VMXEXITINSTRINFO        InstrInfo;
    /** The VM-exit instruction ID. */
    VMXINSTRID              uInstrId;

    /** The Exit qualification field. */
    uint64_t                u64Qual;
    /** The Guest-linear address field. */
    uint64_t                u64GuestLinearAddr;
    /** The Guest-physical address field. */
    uint64_t                u64GuestPhysAddr;
    /** The guest pending-debug exceptions. */
    uint64_t                u64GuestPendingDbgXcpts;
    /** The effective guest-linear address if @a InstrInfo indicates a memory-based
     *  instruction VM-exit. */
    RTGCPTR                 GCPtrEffAddr;
} VMXVEXITINFO;
/** Pointer to the VMXVEXITINFO struct. */
typedef VMXVEXITINFO *PVMXVEXITINFO;
/** Pointer to a const VMXVEXITINFO struct. */
typedef const VMXVEXITINFO *PCVMXVEXITINFO;
AssertCompileMemberAlignment(VMXVEXITINFO, u64Qual, 8);

/** Initialize a VMXVEXITINFO structure from only an exit reason. */
#define VMXVEXITINFO_INIT_ONLY_REASON(a_uReason) \
    { (a_uReason), 0, { 0 }, VMXINSTRID_NONE, 0, 0, 0, 0, 0 }

/** Initialize a VMXVEXITINFO structure from exit reason and instruction length (no info). */
#define VMXVEXITINFO_INIT_WITH_INSTR_LEN(a_uReason, a_cbInstr) \
    { (a_uReason), (a_cbInstr), { 0 }, VMXINSTRID_NONE, 0, 0, 0, 0, 0 }

/** Initialize a VMXVEXITINFO structure from exit reason and exit qualification. */
#define VMXVEXITINFO_INIT_WITH_QUAL(a_uReason, a_uQual) \
    { (a_uReason), 0, { 0 }, VMXINSTRID_NONE, (a_uQual), 0, 0, 0, 0 }

/** Initialize a VMXVEXITINFO structure from exit reason, exit qualification,
 *  instruction info and length. */
#define VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO(a_uReason, a_uQual, a_uInstrInfo, a_cbInstr) \
    { (a_uReason), (a_cbInstr), { a_uInstrInfo }, VMXINSTRID_NONE, (a_uQual), 0, 0, 0, 0 }

/** Initialize a VMXVEXITINFO structure from exit reason, exit qualification,
 *  instruction info and length all copied from a VMXTRANSIENT structure. */
#define VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(a_pVmxTransient) \
    VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO((a_pVmxTransient)->uExitReason, \
                                               (a_pVmxTransient)->uExitQual, \
                                               (a_pVmxTransient)->ExitInstrInfo.u, \
                                               (a_pVmxTransient)->cbExitInstr)

/** Initialize a VMXVEXITINFO structure from exit reason, exit qualification,
 *  instruction length (no info). */
#define VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(a_uReason, a_uQual, a_cbInstr) \
    { (a_uReason), (a_cbInstr), { 0 }, VMXINSTRID_NONE, (a_uQual), 0, 0, 0, 0 }

/** Initialize a VMXVEXITINFO structure from exit reason, exit qualification and
 *  instruction length (no info) all copied from a VMXTRANSIENT structure. */
#define VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(a_pVmxTransient) \
    VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN((a_pVmxTransient)->uExitReason, \
                                              (a_pVmxTransient)->uExitQual, \
                                              (a_pVmxTransient)->cbExitInstr)

/** Initialize a VMXVEXITINFO structure from exit reason, exit qualification,
 *  instruction info, instruction length and guest linear address. */
#define VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_AND_LIN_ADDR(a_uReason, a_uQual, a_uInstrInfo, \
                                                                a_cbInstr, a_uGstLinAddr) \
    { (a_uReason), (a_cbInstr), { (a_uInstrInfo) }, VMXINSTRID_NONE, (a_uQual), (a_uGstLinAddr), 0, 0, 0 }

/** Initialize a VMXVEXITINFO structure from exit reason, exit qualification,
 *  instruction info, instruction length and guest linear address all copied
 *  from a VMXTRANSIENT structure. */
#define VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_AND_LIN_ADDR_FROM_TRANSIENT(a_pVmxTransient) \
    VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_AND_LIN_ADDR((a_pVmxTransient)->uExitReason, \
                                                            (a_pVmxTransient)->uExitQual, \
                                                            (a_pVmxTransient)->ExitInstrInfo.u, \
                                                            (a_pVmxTransient)->cbExitInstr, \
                                                            (a_pVmxTransient)->uGuestLinearAddr)

/** Initialize a VMXVEXITINFO structure from exit reason and pending debug
 *  exceptions. */
#define VMXVEXITINFO_INIT_WITH_DBG_XCPTS(a_uReason, a_uPendingDbgXcpts) \
    { (a_uReason), 0, { 0 }, VMXINSTRID_NONE, 0, 0, 0, (a_uPendingDbgXcpts), 0 }

/** Initialize a VMXVEXITINFO structure from exit reason and pending debug
 *  exceptions both copied from a VMXTRANSIENT structure. */
#define VMXVEXITINFO_INIT_WITH_DBG_XCPTS_FROM_TRANSIENT(a_pVmxTransient) \
    VMXVEXITINFO_INIT_WITH_DBG_XCPTS((a_pVmxTransient)->uExitReason, (a_pVmxTransient)->uGuestPendingDbgXcpts)


/** Initialize a VMXVEXITINFO structure from exit reason, exit qualification,
 *  instruction length, guest linear address and guest physical address. */
#define VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_AND_GST_ADDRESSES(a_uReason, a_uQual, a_cbInstr, \
                                                                    a_uGstLinAddr, a_uGstPhysAddr) \
    { (a_uReason), (a_cbInstr), { 0 }, VMXINSTRID_NONE, (a_uQual), (a_uGstLinAddr), (a_uGstPhysAddr), 0, 0 }


/**
 * Virtual VM-exit information for events.
 *
 * This is a convenience structure that bundles some event-based VM-exit information
 * related fields together that are not included in VMXVEXITINFO.
 *
 * This is kept as a separate structure and not included in VMXVEXITINFO, to make it
 * easier to distinguish that IEM VM-exit handlers will set one or more of the
 * following fields in the virtual VMCS. Including it in the VMXVEXITINFO will not
 * make it ovbious which fields may get set (or cleared).
 */
typedef struct
{
    /** VM-exit interruption information. */
    uint32_t                uExitIntInfo;
    /** VM-exit interruption error code. */
    uint32_t                uExitIntErrCode;
    /** IDT-vectoring information. */
    uint32_t                uIdtVectoringInfo;
    /** IDT-vectoring error code. */
    uint32_t                uIdtVectoringErrCode;
} VMXVEXITEVENTINFO;
/** Pointer to the VMXVEXITEVENTINFO struct. */
typedef VMXVEXITEVENTINFO *PVMXVEXITEVENTINFO;
/** Pointer to a const VMXVEXITEVENTINFO struct. */
typedef const VMXVEXITEVENTINFO *PCVMXVEXITEVENTINFO;

/** Initialize a VMXVEXITEVENTINFO. */
#define VMXVEXITEVENTINFO_INIT(a_uExitIntInfo, a_uExitIntErrCode, a_uIdtVectoringInfo, a_uIdtVectoringErrCode) \
    { (a_uExitIntInfo), (a_uExitIntErrCode), (a_uIdtVectoringInfo), (a_uIdtVectoringErrCode) }

/** Initialize a VMXVEXITEVENTINFO with VM-exit interruption info and VM-exit
 *  interruption error code. */
#define VMXVEXITEVENTINFO_INIT_ONLY_INT(a_uExitIntInfo, a_uExitIntErrCode) \
    VMXVEXITEVENTINFO_INIT(a_uExitIntInfo, a_uExitIntErrCode, 0, 0)

/** Initialize a VMXVEXITEVENTINFO with IDT vectoring info and IDT
 *  vectoring error code. */
#define VMXVEXITEVENTINFO_INIT_ONLY_IDT(a_uIdtVectoringInfo, a_uIdtVectoringErrCode) \
    VMXVEXITEVENTINFO_INIT(0, 0, a_uIdtVectoringInfo, a_uIdtVectoringErrCode)

/**
 * Virtual VMCS.
 *
 * This is our custom format. Relevant fields from this VMCS will be merged into the
 * actual/shadow VMCS when we execute nested-guest code using hardware-assisted
 * VMX.
 *
 * The first 8 bytes must be in accordance with the Intel VT-x spec.
 * See Intel spec. 24.2 "Format of the VMCS Region".
 *
 * The offset and size of the VMCS state field (@a fVmcsState) is also fixed (not by
 * the Intel spec. but for our own requirements) as we use it to offset into guest
 * memory.
 *
 * Although the guest is supposed to access the VMCS only through the execution of
 * VMX instructions (VMREAD, VMWRITE etc.), since the VMCS may reside in guest
 * memory (e.g, active but not current VMCS), for saved-states compatibility, and
 * for teleportation purposes, any newly added fields should be added to the
 * appropriate reserved sections or at the end of the structure.
 *
 * We always treat natural-width fields as 64-bit in our implementation since
 * it's easier, allows for teleporation in the future and does not affect guest
 * software.
 *
 * @note Any fields that are added or modified here, make sure to update the
 *       corresponding fields in IEM (g_aoffVmcsMap), the corresponding saved
 *       state structure in CPUM (g_aVmxHwvirtVmcs) and bump the SSM version.
 *       Also consider updating CPUMIsGuestVmxVmcsFieldValid and cpumR3InfoVmxVmcs.
 */
#pragma pack(1)
typedef struct
{
    /** @name Header.
     * @{
     */
    VMXVMCSREVID    u32VmcsRevId;                /**< 0x000 - VMX VMCS revision identifier. */
    VMXABORT        enmVmxAbort;                 /**< 0x004 - VMX-abort indicator. */
    uint8_t         fVmcsState;                  /**< 0x008 - VMCS launch state, see VMX_V_VMCS_LAUNCH_STATE_XXX. */
    uint8_t         au8Padding0[3];              /**< 0x009 - Reserved for future. */
    uint32_t        u32RestoreProcCtls2;         /**< 0x00c - Secondary VM-execution controls to restore, see iemVmxVmentryCheckCtls(). */
    uint32_t        au32Reserved0[11];           /**< 0x010 - Reserved for future. */
    /** @} */

    /** @name Read-only fields.
     * @{ */
    /** 16-bit fields. */
    uint16_t        u16Reserved0[14];            /**< 0x03c - Reserved for future. */

    /** 32-bit fields. */
    uint32_t        u32RoVmInstrError;           /**< 0x058 - VM-instruction error. */
    uint32_t        u32RoExitReason;             /**< 0x05c - VM-exit reason. */
    uint32_t        u32RoExitIntInfo;            /**< 0x060 - VM-exit interruption information. */
    uint32_t        u32RoExitIntErrCode;         /**< 0x064 - VM-exit interruption error code. */
    uint32_t        u32RoIdtVectoringInfo;       /**< 0x068 - IDT-vectoring information. */
    uint32_t        u32RoIdtVectoringErrCode;    /**< 0x06c - IDT-vectoring error code. */
    uint32_t        u32RoExitInstrLen;           /**< 0x070 - VM-exit instruction length. */
    uint32_t        u32RoExitInstrInfo;          /**< 0x074 - VM-exit instruction information. */
    uint32_t        au32RoReserved2[16];         /**< 0x078 - Reserved for future. */

    /** 64-bit fields. */
    RTUINT64U       u64RoGuestPhysAddr;          /**< 0x0b8 - Guest-physical address. */
    RTUINT64U       au64Reserved1[8];            /**< 0x0c0 - Reserved for future. */

    /** Natural-width fields. */
    RTUINT64U       u64RoExitQual;               /**< 0x100 - Exit qualification. */
    RTUINT64U       u64RoIoRcx;                  /**< 0x108 - I/O RCX. */
    RTUINT64U       u64RoIoRsi;                  /**< 0x110 - I/O RSI. */
    RTUINT64U       u64RoIoRdi;                  /**< 0x118 - I/O RDI. */
    RTUINT64U       u64RoIoRip;                  /**< 0x120 - I/O RIP. */
    RTUINT64U       u64RoGuestLinearAddr;        /**< 0x128 - Guest-linear address. */
    RTUINT64U       au64Reserved5[16];           /**< 0x130 - Reserved for future. */
    /** @} */

    /** @name Control fields.
     * @{ */
    /** 16-bit fields. */
    uint16_t        u16Vpid;                     /**< 0x1b0 - Virtual processor ID. */
    uint16_t        u16PostIntNotifyVector;      /**< 0x1b2 - Posted interrupt notify vector. */
    uint16_t        u16EptpIndex;                /**< 0x1b4 - EPTP index. */
    uint16_t        u16HlatPrefixSize;           /**< 0x1b6 - HLAT prefix size. */
    uint16_t        au16Reserved0[12];           /**< 0x1b8 - Reserved for future. */

    /** 32-bit fields. */
    uint32_t        u32PinCtls;                  /**< 0x1d0 - Pin-based VM-execution controls. */
    uint32_t        u32ProcCtls;                 /**< 0x1d4 - Processor-based VM-execution controls. */
    uint32_t        u32XcptBitmap;               /**< 0x1d8 - Exception bitmap. */
    uint32_t        u32XcptPFMask;               /**< 0x1dc - Page-fault exception error mask. */
    uint32_t        u32XcptPFMatch;              /**< 0x1e0 - Page-fault exception error match. */
    uint32_t        u32Cr3TargetCount;           /**< 0x1e4 - CR3-target count. */
    uint32_t        u32ExitCtls;                 /**< 0x1e8 - VM-exit controls. */
    uint32_t        u32ExitMsrStoreCount;        /**< 0x1ec - VM-exit MSR store count. */
    uint32_t        u32ExitMsrLoadCount;         /**< 0x1f0 - VM-exit MSR load count. */
    uint32_t        u32EntryCtls;                /**< 0x1f4 - VM-entry controls. */
    uint32_t        u32EntryMsrLoadCount;        /**< 0x1f8 - VM-entry MSR load count. */
    uint32_t        u32EntryIntInfo;             /**< 0x1fc - VM-entry interruption information. */
    uint32_t        u32EntryXcptErrCode;         /**< 0x200 - VM-entry exception error code. */
    uint32_t        u32EntryInstrLen;            /**< 0x204 - VM-entry instruction length. */
    uint32_t        u32TprThreshold;             /**< 0x208 - TPR-threshold. */
    uint32_t        u32ProcCtls2;                /**< 0x20c - Secondary-processor based VM-execution controls. */
    uint32_t        u32PleGap;                   /**< 0x210 - Pause-loop exiting Gap. */
    uint32_t        u32PleWindow;                /**< 0x214 - Pause-loop exiting Window. */
    uint32_t        au32Reserved1[16];           /**< 0x218 - Reserved for future. */

    /** 64-bit fields. */
    RTUINT64U       u64AddrIoBitmapA;            /**< 0x258 - I/O bitmap A address. */
    RTUINT64U       u64AddrIoBitmapB;            /**< 0x260 - I/O bitmap B address. */
    RTUINT64U       u64AddrMsrBitmap;            /**< 0x268 - MSR bitmap address. */
    RTUINT64U       u64AddrExitMsrStore;         /**< 0x270 - VM-exit MSR-store area address. */
    RTUINT64U       u64AddrExitMsrLoad;          /**< 0x278 - VM-exit MSR-load area address. */
    RTUINT64U       u64AddrEntryMsrLoad;         /**< 0x280 - VM-entry MSR-load area address. */
    RTUINT64U       u64ExecVmcsPtr;              /**< 0x288 - Executive-VMCS pointer. */
    RTUINT64U       u64AddrPml;                  /**< 0x290 - Page-modification log address (PML). */
    RTUINT64U       u64TscOffset;                /**< 0x298 - TSC offset. */
    RTUINT64U       u64AddrVirtApic;             /**< 0x2a0 - Virtual-APIC address. */
    RTUINT64U       u64AddrApicAccess;           /**< 0x2a8 - APIC-access address. */
    RTUINT64U       u64AddrPostedIntDesc;        /**< 0x2b0 - Posted-interrupt descriptor address. */
    RTUINT64U       u64VmFuncCtls;               /**< 0x2b8 - VM-functions control. */
    RTUINT64U       u64EptPtr;                   /**< 0x2c0 - EPT pointer. */
    RTUINT64U       u64EoiExitBitmap0;           /**< 0x2c8 - EOI-exit bitmap 0. */
    RTUINT64U       u64EoiExitBitmap1;           /**< 0x2d0 - EOI-exit bitmap 1. */
    RTUINT64U       u64EoiExitBitmap2;           /**< 0x2d8 - EOI-exit bitmap 2. */
    RTUINT64U       u64EoiExitBitmap3;           /**< 0x2e0 - EOI-exit bitmap 3. */
    RTUINT64U       u64AddrEptpList;             /**< 0x2e8 - EPTP-list address. */
    RTUINT64U       u64AddrVmreadBitmap;         /**< 0x2f0 - VMREAD-bitmap address. */
    RTUINT64U       u64AddrVmwriteBitmap;        /**< 0x2f8 - VMWRITE-bitmap address. */
    RTUINT64U       u64AddrXcptVeInfo;           /**< 0x300 - Virtualization-exception information address. */
    RTUINT64U       u64XssExitBitmap;            /**< 0x308 - XSS-exiting bitmap. */
    RTUINT64U       u64EnclsExitBitmap;          /**< 0x310 - ENCLS-exiting bitmap address. */
    RTUINT64U       u64SppTablePtr;              /**< 0x318 - Sub-page-permission-table pointer (SPPTP). */
    RTUINT64U       u64TscMultiplier;            /**< 0x320 - TSC multiplier. */
    RTUINT64U       u64ProcCtls3;                /**< 0x328 - Tertiary-Processor based VM-execution controls. */
    RTUINT64U       u64EnclvExitBitmap;          /**< 0x330 - ENCLV-exiting bitmap. */
    RTUINT64U       u64PconfigExitBitmap;        /**< 0x338 - PCONFIG-exiting bitmap. */
    RTUINT64U       u64HlatPtr;                  /**< 0x340 - HLAT pointer. */
    RTUINT64U       u64ExitCtls2;                /**< 0x348 - Secondary VM-exit controls. */
    RTUINT64U       au64Reserved0[10];           /**< 0x350 - Reserved for future. */

    /** Natural-width fields. */
    RTUINT64U       u64Cr0Mask;                  /**< 0x3a0 - CR0 guest/host Mask. */
    RTUINT64U       u64Cr4Mask;                  /**< 0x3a8 - CR4 guest/host Mask. */
    RTUINT64U       u64Cr0ReadShadow;            /**< 0x3b0 - CR0 read shadow. */
    RTUINT64U       u64Cr4ReadShadow;            /**< 0x3b8 - CR4 read shadow. */
    RTUINT64U       u64Cr3Target0;               /**< 0x3c0 - CR3-target value 0. */
    RTUINT64U       u64Cr3Target1;               /**< 0x3c8 - CR3-target value 1. */
    RTUINT64U       u64Cr3Target2;               /**< 0x3d0 - CR3-target value 2. */
    RTUINT64U       u64Cr3Target3;               /**< 0x3d8 - CR3-target value 3. */
    RTUINT64U       au64Reserved4[32];           /**< 0x3e0 - Reserved for future. */
    /** @} */

    /** @name Host-state fields.
     * @{ */
    /** 16-bit fields. */
    /* Order of [Es..Gs] fields below must match [X86_SREG_ES..X86_SREG_GS]. */
    RTSEL           HostEs;                      /**< 0x4e0 - Host ES selector. */
    RTSEL           HostCs;                      /**< 0x4e2 - Host CS selector. */
    RTSEL           HostSs;                      /**< 0x4e4 - Host SS selector. */
    RTSEL           HostDs;                      /**< 0x4e6 - Host DS selector. */
    RTSEL           HostFs;                      /**< 0x4e8 - Host FS selector. */
    RTSEL           HostGs;                      /**< 0x4ea - Host GS selector. */
    RTSEL           HostTr;                      /**< 0x4ec - Host TR selector. */
    uint16_t        au16Reserved2[13];           /**< 0x4ee - Reserved for future. */

    /** 32-bit fields. */
    uint32_t        u32HostSysenterCs;           /**< 0x508 - Host SYSENTER CS. */
    uint32_t        au32Reserved4[11];           /**< 0x50c - Reserved for future. */

    /** 64-bit fields. */
    RTUINT64U       u64HostPatMsr;               /**< 0x538 - Host PAT MSR. */
    RTUINT64U       u64HostEferMsr;              /**< 0x540 - Host EFER MSR. */
    RTUINT64U       u64HostPerfGlobalCtlMsr;     /**< 0x548 - Host global performance-control MSR. */
    RTUINT64U       u64HostPkrsMsr;              /**< 0x550 - Host PKRS MSR. */
    RTUINT64U       au64Reserved3[15];           /**< 0x558 - Reserved for future. */

    /** Natural-width fields. */
    RTUINT64U       u64HostCr0;                  /**< 0x5d0 - Host CR0. */
    RTUINT64U       u64HostCr3;                  /**< 0x5d8 - Host CR3. */
    RTUINT64U       u64HostCr4;                  /**< 0x5e0 - Host CR4. */
    RTUINT64U       u64HostFsBase;               /**< 0x5e8 - Host FS base. */
    RTUINT64U       u64HostGsBase;               /**< 0x5f0 - Host GS base. */
    RTUINT64U       u64HostTrBase;               /**< 0x5f8 - Host TR base. */
    RTUINT64U       u64HostGdtrBase;             /**< 0x600 - Host GDTR base. */
    RTUINT64U       u64HostIdtrBase;             /**< 0x608 - Host IDTR base. */
    RTUINT64U       u64HostSysenterEsp;          /**< 0x610 - Host SYSENTER ESP base. */
    RTUINT64U       u64HostSysenterEip;          /**< 0x618 - Host SYSENTER ESP base. */
    RTUINT64U       u64HostRsp;                  /**< 0x620 - Host RSP. */
    RTUINT64U       u64HostRip;                  /**< 0x628 - Host RIP. */
    RTUINT64U       u64HostSCetMsr;              /**< 0x630 - Host S_CET MSR. */
    RTUINT64U       u64HostSsp;                  /**< 0x638 - Host SSP. */
    RTUINT64U       u64HostIntrSspTableAddrMsr;  /**< 0x640 - Host Interrupt SSP table address MSR. */
    RTUINT64U       au64Reserved7[29];           /**< 0x648 - Reserved for future. */
    /** @} */

    /** @name Guest-state fields.
     * @{ */
    /** 16-bit fields. */
    /* Order of [Es..Gs] fields below must match [X86_SREG_ES..X86_SREG_GS]. */
    RTSEL           GuestEs;                     /**< 0x730 - Guest ES selector. */
    RTSEL           GuestCs;                     /**< 0x732 - Guest ES selector. */
    RTSEL           GuestSs;                     /**< 0x734 - Guest ES selector. */
    RTSEL           GuestDs;                     /**< 0x736 - Guest ES selector. */
    RTSEL           GuestFs;                     /**< 0x738 - Guest ES selector. */
    RTSEL           GuestGs;                     /**< 0x73a - Guest ES selector. */
    RTSEL           GuestLdtr;                   /**< 0x73c - Guest LDTR selector. */
    RTSEL           GuestTr;                     /**< 0x73e - Guest TR selector. */
    uint16_t        u16GuestIntStatus;           /**< 0x740 - Guest interrupt status (virtual-interrupt delivery). */
    uint16_t        u16PmlIndex;                 /**< 0x742 - PML index. */
    uint16_t        au16Reserved1[14];           /**< 0x744 - Reserved for future. */

    /** 32-bit fields. */
    /* Order of [Es..Gs] fields below must match [X86_SREG_ES..X86_SREG_GS]. */
    uint32_t        u32GuestEsLimit;             /**< 0x760 - Guest ES limit. */
    uint32_t        u32GuestCsLimit;             /**< 0x764 - Guest CS limit. */
    uint32_t        u32GuestSsLimit;             /**< 0x768 - Guest SS limit. */
    uint32_t        u32GuestDsLimit;             /**< 0x76c - Guest DS limit. */
    uint32_t        u32GuestFsLimit;             /**< 0x770 - Guest FS limit. */
    uint32_t        u32GuestGsLimit;             /**< 0x774 - Guest GS limit. */
    uint32_t        u32GuestLdtrLimit;           /**< 0x778 - Guest LDTR limit. */
    uint32_t        u32GuestTrLimit;             /**< 0x77c - Guest TR limit. */
    uint32_t        u32GuestGdtrLimit;           /**< 0x780 - Guest GDTR limit. */
    uint32_t        u32GuestIdtrLimit;           /**< 0x784 - Guest IDTR limit. */
    uint32_t        u32GuestEsAttr;              /**< 0x788 - Guest ES attributes. */
    uint32_t        u32GuestCsAttr;              /**< 0x78c - Guest CS attributes. */
    uint32_t        u32GuestSsAttr;              /**< 0x790 - Guest SS attributes. */
    uint32_t        u32GuestDsAttr;              /**< 0x794 - Guest DS attributes. */
    uint32_t        u32GuestFsAttr;              /**< 0x798 - Guest FS attributes. */
    uint32_t        u32GuestGsAttr;              /**< 0x79c - Guest GS attributes. */
    uint32_t        u32GuestLdtrAttr;            /**< 0x7a0 - Guest LDTR attributes. */
    uint32_t        u32GuestTrAttr;              /**< 0x7a4 - Guest TR attributes. */
    uint32_t        u32GuestIntrState;           /**< 0x7a8 - Guest interruptibility state. */
    uint32_t        u32GuestActivityState;       /**< 0x7ac - Guest activity state. */
    uint32_t        u32GuestSmBase;              /**< 0x7b0 - Guest SMBASE. */
    uint32_t        u32GuestSysenterCS;          /**< 0x7b4 - Guest SYSENTER CS. */
    uint32_t        u32PreemptTimer;             /**< 0x7b8 - Preemption timer value. */
    uint32_t        au32Reserved3[11];           /**< 0x7bc - Reserved for future. */

    /** 64-bit fields. */
    RTUINT64U       u64VmcsLinkPtr;              /**< 0x7e8 - VMCS link pointer. */
    RTUINT64U       u64GuestDebugCtlMsr;         /**< 0x7f0 - Guest debug-control MSR. */
    RTUINT64U       u64GuestPatMsr;              /**< 0x7f8 - Guest PAT MSR. */
    RTUINT64U       u64GuestEferMsr;             /**< 0x800 - Guest EFER MSR. */
    RTUINT64U       u64GuestPerfGlobalCtlMsr;    /**< 0x808 - Guest global performance-control MSR. */
    RTUINT64U       u64GuestPdpte0;              /**< 0x810 - Guest PDPTE 0. */
    RTUINT64U       u64GuestPdpte1;              /**< 0x818 - Guest PDPTE 0. */
    RTUINT64U       u64GuestPdpte2;              /**< 0x820 - Guest PDPTE 1. */
    RTUINT64U       u64GuestPdpte3;              /**< 0x828 - Guest PDPTE 2. */
    RTUINT64U       u64GuestBndcfgsMsr;          /**< 0x830 - Guest Bounds config MPX MSR (Intel Memory Protection Extensions). */
    RTUINT64U       u64GuestRtitCtlMsr;          /**< 0x838 - Guest RTIT control MSR (Intel Real Time Instruction Trace). */
    RTUINT64U       u64GuestPkrsMsr;             /**< 0x840 - Guest PKRS MSR. */
    RTUINT64U       au64Reserved2[31];           /**< 0x848 - Reserved for future. */

    /** Natural-width fields. */
    RTUINT64U       u64GuestCr0;                 /**< 0x940 - Guest CR0. */
    RTUINT64U       u64GuestCr3;                 /**< 0x948 - Guest CR3. */
    RTUINT64U       u64GuestCr4;                 /**< 0x950 - Guest CR4. */
    RTUINT64U       u64GuestEsBase;              /**< 0x958 - Guest ES base. */
    RTUINT64U       u64GuestCsBase;              /**< 0x960 - Guest CS base. */
    RTUINT64U       u64GuestSsBase;              /**< 0x968 - Guest SS base. */
    RTUINT64U       u64GuestDsBase;              /**< 0x970 - Guest DS base. */
    RTUINT64U       u64GuestFsBase;              /**< 0x978 - Guest FS base. */
    RTUINT64U       u64GuestGsBase;              /**< 0x980 - Guest GS base. */
    RTUINT64U       u64GuestLdtrBase;            /**< 0x988 - Guest LDTR base. */
    RTUINT64U       u64GuestTrBase;              /**< 0x990 - Guest TR base. */
    RTUINT64U       u64GuestGdtrBase;            /**< 0x998 - Guest GDTR base. */
    RTUINT64U       u64GuestIdtrBase;            /**< 0x9a0 - Guest IDTR base. */
    RTUINT64U       u64GuestDr7;                 /**< 0x9a8 - Guest DR7. */
    RTUINT64U       u64GuestRsp;                 /**< 0x9b0 - Guest RSP. */
    RTUINT64U       u64GuestRip;                 /**< 0x9b8 - Guest RIP. */
    RTUINT64U       u64GuestRFlags;              /**< 0x9c0 - Guest RFLAGS. */
    RTUINT64U       u64GuestPendingDbgXcpts;     /**< 0x9c8 - Guest pending debug exceptions. */
    RTUINT64U       u64GuestSysenterEsp;         /**< 0x9d0 - Guest SYSENTER ESP. */
    RTUINT64U       u64GuestSysenterEip;         /**< 0x9d8 - Guest SYSENTER EIP. */
    RTUINT64U       u64GuestSCetMsr;             /**< 0x9e0 - Guest S_CET MSR. */
    RTUINT64U       u64GuestSsp;                 /**< 0x9e8 - Guest SSP. */
    RTUINT64U       u64GuestIntrSspTableAddrMsr; /**< 0x9f0 - Guest Interrupt SSP table address MSR. */
    RTUINT64U       au64Reserved6[29];           /**< 0x9f8 - Reserved for future. */
    /** @} */

    /** 0xae0 - Padding / reserved for future use. */
    uint8_t         abPadding[X86_PAGE_4K_SIZE - 0xae0];
} VMXVVMCS;
#pragma pack()
/** Pointer to the VMXVVMCS struct. */
typedef VMXVVMCS *PVMXVVMCS;
/** Pointer to a const VMXVVMCS struct. */
typedef const VMXVVMCS *PCVMXVVMCS;
AssertCompileSize(VMXVVMCS, X86_PAGE_4K_SIZE);
AssertCompileMemberSize(VMXVVMCS, fVmcsState, sizeof(uint8_t));
AssertCompileMemberOffset(VMXVVMCS, enmVmxAbort,        0x004);
AssertCompileMemberOffset(VMXVVMCS, fVmcsState,         0x008);
AssertCompileMemberOffset(VMXVVMCS, u32RoVmInstrError,  0x058);
AssertCompileMemberOffset(VMXVVMCS, u64RoGuestPhysAddr, 0x0b8);
AssertCompileMemberOffset(VMXVVMCS, u64RoExitQual,      0x100);
AssertCompileMemberOffset(VMXVVMCS, u16Vpid,            0x1b0);
AssertCompileMemberOffset(VMXVVMCS, u32PinCtls,         0x1d0);
AssertCompileMemberOffset(VMXVVMCS, u64AddrIoBitmapA,   0x258);
AssertCompileMemberOffset(VMXVVMCS, u64Cr0Mask,         0x3a0);
AssertCompileMemberOffset(VMXVVMCS, HostEs,             0x4e0);
AssertCompileMemberOffset(VMXVVMCS, u32HostSysenterCs,  0x508);
AssertCompileMemberOffset(VMXVVMCS, u64HostPatMsr,      0x538);
AssertCompileMemberOffset(VMXVVMCS, u64HostCr0,         0x5d0);
AssertCompileMemberOffset(VMXVVMCS, GuestEs,            0x730);
AssertCompileMemberOffset(VMXVVMCS, u32GuestEsLimit,    0x760);
AssertCompileMemberOffset(VMXVVMCS, u64VmcsLinkPtr,     0x7e8);
AssertCompileMemberOffset(VMXVVMCS, u64GuestCr0,        0x940);

/**
 * Virtual VMX-instruction and VM-exit diagnostics.
 *
 * These are not the same as VM instruction errors that are enumerated in the Intel
 * spec. These are purely internal, fine-grained definitions used for diagnostic
 * purposes and are not reported to guest software under the VM-instruction error
 * field in its VMCS.
 *
 * @note Members of this enum are used as array indices, so no gaps are allowed.
 *       Please update g_apszVmxVDiagDesc when you add new fields to this enum.
 */
typedef enum
{
    /* Internal processing errors. */
    kVmxVDiag_None = 0,
    kVmxVDiag_Ipe_1,
    kVmxVDiag_Ipe_2,
    kVmxVDiag_Ipe_3,
    kVmxVDiag_Ipe_4,
    kVmxVDiag_Ipe_5,
    kVmxVDiag_Ipe_6,
    kVmxVDiag_Ipe_7,
    kVmxVDiag_Ipe_8,
    kVmxVDiag_Ipe_9,
    kVmxVDiag_Ipe_10,
    kVmxVDiag_Ipe_11,
    kVmxVDiag_Ipe_12,
    kVmxVDiag_Ipe_13,
    kVmxVDiag_Ipe_14,
    kVmxVDiag_Ipe_15,
    kVmxVDiag_Ipe_16,
    /* VMXON. */
    kVmxVDiag_Vmxon_A20M,
    kVmxVDiag_Vmxon_Cpl,
    kVmxVDiag_Vmxon_Cr0Fixed0,
    kVmxVDiag_Vmxon_Cr0Fixed1,
    kVmxVDiag_Vmxon_Cr4Fixed0,
    kVmxVDiag_Vmxon_Cr4Fixed1,
    kVmxVDiag_Vmxon_Intercept,
    kVmxVDiag_Vmxon_LongModeCS,
    kVmxVDiag_Vmxon_MsrFeatCtl,
    kVmxVDiag_Vmxon_PtrAbnormal,
    kVmxVDiag_Vmxon_PtrAlign,
    kVmxVDiag_Vmxon_PtrMap,
    kVmxVDiag_Vmxon_PtrReadPhys,
    kVmxVDiag_Vmxon_PtrWidth,
    kVmxVDiag_Vmxon_RealOrV86Mode,
    kVmxVDiag_Vmxon_ShadowVmcs,
    kVmxVDiag_Vmxon_VmxAlreadyRoot,
    kVmxVDiag_Vmxon_Vmxe,
    kVmxVDiag_Vmxon_VmcsRevId,
    kVmxVDiag_Vmxon_VmxRootCpl,
    /* VMXOFF. */
    kVmxVDiag_Vmxoff_Cpl,
    kVmxVDiag_Vmxoff_Intercept,
    kVmxVDiag_Vmxoff_LongModeCS,
    kVmxVDiag_Vmxoff_RealOrV86Mode,
    kVmxVDiag_Vmxoff_Vmxe,
    kVmxVDiag_Vmxoff_VmxRoot,
    /* VMPTRLD. */
    kVmxVDiag_Vmptrld_Cpl,
    kVmxVDiag_Vmptrld_LongModeCS,
    kVmxVDiag_Vmptrld_PtrAbnormal,
    kVmxVDiag_Vmptrld_PtrAlign,
    kVmxVDiag_Vmptrld_PtrMap,
    kVmxVDiag_Vmptrld_PtrReadPhys,
    kVmxVDiag_Vmptrld_PtrVmxon,
    kVmxVDiag_Vmptrld_PtrWidth,
    kVmxVDiag_Vmptrld_RealOrV86Mode,
    kVmxVDiag_Vmptrld_RevPtrReadPhys,
    kVmxVDiag_Vmptrld_ShadowVmcs,
    kVmxVDiag_Vmptrld_VmcsRevId,
    kVmxVDiag_Vmptrld_VmxRoot,
    /* VMPTRST. */
    kVmxVDiag_Vmptrst_Cpl,
    kVmxVDiag_Vmptrst_LongModeCS,
    kVmxVDiag_Vmptrst_PtrMap,
    kVmxVDiag_Vmptrst_RealOrV86Mode,
    kVmxVDiag_Vmptrst_VmxRoot,
    /* VMCLEAR. */
    kVmxVDiag_Vmclear_Cpl,
    kVmxVDiag_Vmclear_LongModeCS,
    kVmxVDiag_Vmclear_PtrAbnormal,
    kVmxVDiag_Vmclear_PtrAlign,
    kVmxVDiag_Vmclear_PtrMap,
    kVmxVDiag_Vmclear_PtrReadPhys,
    kVmxVDiag_Vmclear_PtrVmxon,
    kVmxVDiag_Vmclear_PtrWidth,
    kVmxVDiag_Vmclear_RealOrV86Mode,
    kVmxVDiag_Vmclear_VmxRoot,
    /* VMWRITE. */
    kVmxVDiag_Vmwrite_Cpl,
    kVmxVDiag_Vmwrite_FieldInvalid,
    kVmxVDiag_Vmwrite_FieldRo,
    kVmxVDiag_Vmwrite_LinkPtrInvalid,
    kVmxVDiag_Vmwrite_LongModeCS,
    kVmxVDiag_Vmwrite_PtrInvalid,
    kVmxVDiag_Vmwrite_PtrMap,
    kVmxVDiag_Vmwrite_RealOrV86Mode,
    kVmxVDiag_Vmwrite_VmxRoot,
    /* VMREAD. */
    kVmxVDiag_Vmread_Cpl,
    kVmxVDiag_Vmread_FieldInvalid,
    kVmxVDiag_Vmread_LinkPtrInvalid,
    kVmxVDiag_Vmread_LongModeCS,
    kVmxVDiag_Vmread_PtrInvalid,
    kVmxVDiag_Vmread_PtrMap,
    kVmxVDiag_Vmread_RealOrV86Mode,
    kVmxVDiag_Vmread_VmxRoot,
    /* INVVPID. */
    kVmxVDiag_Invvpid_Cpl,
    kVmxVDiag_Invvpid_DescRsvd,
    kVmxVDiag_Invvpid_LongModeCS,
    kVmxVDiag_Invvpid_RealOrV86Mode,
    kVmxVDiag_Invvpid_TypeInvalid,
    kVmxVDiag_Invvpid_Type0InvalidAddr,
    kVmxVDiag_Invvpid_Type0InvalidVpid,
    kVmxVDiag_Invvpid_Type1InvalidVpid,
    kVmxVDiag_Invvpid_Type3InvalidVpid,
    kVmxVDiag_Invvpid_VmxRoot,
    /* INVEPT. */
    kVmxVDiag_Invept_Cpl,
    kVmxVDiag_Invept_DescRsvd,
    kVmxVDiag_Invept_EptpInvalid,
    kVmxVDiag_Invept_LongModeCS,
    kVmxVDiag_Invept_RealOrV86Mode,
    kVmxVDiag_Invept_TypeInvalid,
    kVmxVDiag_Invept_VmxRoot,
    /* VMLAUNCH/VMRESUME. */
    kVmxVDiag_Vmentry_AddrApicAccess,
    kVmxVDiag_Vmentry_AddrApicAccessEqVirtApic,
    kVmxVDiag_Vmentry_AddrApicAccessHandlerReg,
    kVmxVDiag_Vmentry_AddrEntryMsrLoad,
    kVmxVDiag_Vmentry_AddrExitMsrLoad,
    kVmxVDiag_Vmentry_AddrExitMsrStore,
    kVmxVDiag_Vmentry_AddrIoBitmapA,
    kVmxVDiag_Vmentry_AddrIoBitmapB,
    kVmxVDiag_Vmentry_AddrMsrBitmap,
    kVmxVDiag_Vmentry_AddrVirtApicPage,
    kVmxVDiag_Vmentry_AddrVmcsLinkPtr,
    kVmxVDiag_Vmentry_AddrVmreadBitmap,
    kVmxVDiag_Vmentry_AddrVmwriteBitmap,
    kVmxVDiag_Vmentry_ApicRegVirt,
    kVmxVDiag_Vmentry_BlocKMovSS,
    kVmxVDiag_Vmentry_Cpl,
    kVmxVDiag_Vmentry_Cr3TargetCount,
    kVmxVDiag_Vmentry_EntryCtlsAllowed1,
    kVmxVDiag_Vmentry_EntryCtlsDisallowed0,
    kVmxVDiag_Vmentry_EntryInstrLen,
    kVmxVDiag_Vmentry_EntryInstrLenZero,
    kVmxVDiag_Vmentry_EntryIntInfoErrCodePe,
    kVmxVDiag_Vmentry_EntryIntInfoErrCodeVec,
    kVmxVDiag_Vmentry_EntryIntInfoTypeVecRsvd,
    kVmxVDiag_Vmentry_EntryXcptErrCodeRsvd,
    kVmxVDiag_Vmentry_EptpAccessDirty,
    kVmxVDiag_Vmentry_EptpPageWalkLength,
    kVmxVDiag_Vmentry_EptpMemType,
    kVmxVDiag_Vmentry_EptpRsvd,
    kVmxVDiag_Vmentry_ExitCtlsAllowed1,
    kVmxVDiag_Vmentry_ExitCtlsDisallowed0,
    kVmxVDiag_Vmentry_GuestActStateHlt,
    kVmxVDiag_Vmentry_GuestActStateRsvd,
    kVmxVDiag_Vmentry_GuestActStateShutdown,
    kVmxVDiag_Vmentry_GuestActStateSsDpl,
    kVmxVDiag_Vmentry_GuestActStateStiMovSs,
    kVmxVDiag_Vmentry_GuestCr0Fixed0,
    kVmxVDiag_Vmentry_GuestCr0Fixed1,
    kVmxVDiag_Vmentry_GuestCr0PgPe,
    kVmxVDiag_Vmentry_GuestCr3,
    kVmxVDiag_Vmentry_GuestCr4Fixed0,
    kVmxVDiag_Vmentry_GuestCr4Fixed1,
    kVmxVDiag_Vmentry_GuestDebugCtl,
    kVmxVDiag_Vmentry_GuestDr7,
    kVmxVDiag_Vmentry_GuestEferMsr,
    kVmxVDiag_Vmentry_GuestEferMsrRsvd,
    kVmxVDiag_Vmentry_GuestGdtrBase,
    kVmxVDiag_Vmentry_GuestGdtrLimit,
    kVmxVDiag_Vmentry_GuestIdtrBase,
    kVmxVDiag_Vmentry_GuestIdtrLimit,
    kVmxVDiag_Vmentry_GuestIntStateEnclave,
    kVmxVDiag_Vmentry_GuestIntStateExtInt,
    kVmxVDiag_Vmentry_GuestIntStateNmi,
    kVmxVDiag_Vmentry_GuestIntStateRFlagsSti,
    kVmxVDiag_Vmentry_GuestIntStateRsvd,
    kVmxVDiag_Vmentry_GuestIntStateSmi,
    kVmxVDiag_Vmentry_GuestIntStateStiMovSs,
    kVmxVDiag_Vmentry_GuestIntStateVirtNmi,
    kVmxVDiag_Vmentry_GuestPae,
    kVmxVDiag_Vmentry_GuestPatMsr,
    kVmxVDiag_Vmentry_GuestPcide,
    kVmxVDiag_Vmentry_GuestPdpte,
    kVmxVDiag_Vmentry_GuestPndDbgXcptBsNoTf,
    kVmxVDiag_Vmentry_GuestPndDbgXcptBsTf,
    kVmxVDiag_Vmentry_GuestPndDbgXcptRsvd,
    kVmxVDiag_Vmentry_GuestPndDbgXcptRtm,
    kVmxVDiag_Vmentry_GuestRip,
    kVmxVDiag_Vmentry_GuestRipRsvd,
    kVmxVDiag_Vmentry_GuestRFlagsIf,
    kVmxVDiag_Vmentry_GuestRFlagsRsvd,
    kVmxVDiag_Vmentry_GuestRFlagsVm,
    kVmxVDiag_Vmentry_GuestSegAttrCsDefBig,
    kVmxVDiag_Vmentry_GuestSegAttrCsDplEqSs,
    kVmxVDiag_Vmentry_GuestSegAttrCsDplLtSs,
    kVmxVDiag_Vmentry_GuestSegAttrCsDplZero,
    kVmxVDiag_Vmentry_GuestSegAttrCsType,
    kVmxVDiag_Vmentry_GuestSegAttrCsTypeRead,
    kVmxVDiag_Vmentry_GuestSegAttrDescTypeCs,
    kVmxVDiag_Vmentry_GuestSegAttrDescTypeDs,
    kVmxVDiag_Vmentry_GuestSegAttrDescTypeEs,
    kVmxVDiag_Vmentry_GuestSegAttrDescTypeFs,
    kVmxVDiag_Vmentry_GuestSegAttrDescTypeGs,
    kVmxVDiag_Vmentry_GuestSegAttrDescTypeSs,
    kVmxVDiag_Vmentry_GuestSegAttrDplRplCs,
    kVmxVDiag_Vmentry_GuestSegAttrDplRplDs,
    kVmxVDiag_Vmentry_GuestSegAttrDplRplEs,
    kVmxVDiag_Vmentry_GuestSegAttrDplRplFs,
    kVmxVDiag_Vmentry_GuestSegAttrDplRplGs,
    kVmxVDiag_Vmentry_GuestSegAttrDplRplSs,
    kVmxVDiag_Vmentry_GuestSegAttrGranCs,
    kVmxVDiag_Vmentry_GuestSegAttrGranDs,
    kVmxVDiag_Vmentry_GuestSegAttrGranEs,
    kVmxVDiag_Vmentry_GuestSegAttrGranFs,
    kVmxVDiag_Vmentry_GuestSegAttrGranGs,
    kVmxVDiag_Vmentry_GuestSegAttrGranSs,
    kVmxVDiag_Vmentry_GuestSegAttrLdtrDescType,
    kVmxVDiag_Vmentry_GuestSegAttrLdtrGran,
    kVmxVDiag_Vmentry_GuestSegAttrLdtrPresent,
    kVmxVDiag_Vmentry_GuestSegAttrLdtrRsvd,
    kVmxVDiag_Vmentry_GuestSegAttrLdtrType,
    kVmxVDiag_Vmentry_GuestSegAttrPresentCs,
    kVmxVDiag_Vmentry_GuestSegAttrPresentDs,
    kVmxVDiag_Vmentry_GuestSegAttrPresentEs,
    kVmxVDiag_Vmentry_GuestSegAttrPresentFs,
    kVmxVDiag_Vmentry_GuestSegAttrPresentGs,
    kVmxVDiag_Vmentry_GuestSegAttrPresentSs,
    kVmxVDiag_Vmentry_GuestSegAttrRsvdCs,
    kVmxVDiag_Vmentry_GuestSegAttrRsvdDs,
    kVmxVDiag_Vmentry_GuestSegAttrRsvdEs,
    kVmxVDiag_Vmentry_GuestSegAttrRsvdFs,
    kVmxVDiag_Vmentry_GuestSegAttrRsvdGs,
    kVmxVDiag_Vmentry_GuestSegAttrRsvdSs,
    kVmxVDiag_Vmentry_GuestSegAttrSsDplEqRpl,
    kVmxVDiag_Vmentry_GuestSegAttrSsDplZero,
    kVmxVDiag_Vmentry_GuestSegAttrSsType,
    kVmxVDiag_Vmentry_GuestSegAttrTrDescType,
    kVmxVDiag_Vmentry_GuestSegAttrTrGran,
    kVmxVDiag_Vmentry_GuestSegAttrTrPresent,
    kVmxVDiag_Vmentry_GuestSegAttrTrRsvd,
    kVmxVDiag_Vmentry_GuestSegAttrTrType,
    kVmxVDiag_Vmentry_GuestSegAttrTrUnusable,
    kVmxVDiag_Vmentry_GuestSegAttrTypeAccCs,
    kVmxVDiag_Vmentry_GuestSegAttrTypeAccDs,
    kVmxVDiag_Vmentry_GuestSegAttrTypeAccEs,
    kVmxVDiag_Vmentry_GuestSegAttrTypeAccFs,
    kVmxVDiag_Vmentry_GuestSegAttrTypeAccGs,
    kVmxVDiag_Vmentry_GuestSegAttrTypeAccSs,
    kVmxVDiag_Vmentry_GuestSegAttrV86Cs,
    kVmxVDiag_Vmentry_GuestSegAttrV86Ds,
    kVmxVDiag_Vmentry_GuestSegAttrV86Es,
    kVmxVDiag_Vmentry_GuestSegAttrV86Fs,
    kVmxVDiag_Vmentry_GuestSegAttrV86Gs,
    kVmxVDiag_Vmentry_GuestSegAttrV86Ss,
    kVmxVDiag_Vmentry_GuestSegBaseCs,
    kVmxVDiag_Vmentry_GuestSegBaseDs,
    kVmxVDiag_Vmentry_GuestSegBaseEs,
    kVmxVDiag_Vmentry_GuestSegBaseFs,
    kVmxVDiag_Vmentry_GuestSegBaseGs,
    kVmxVDiag_Vmentry_GuestSegBaseLdtr,
    kVmxVDiag_Vmentry_GuestSegBaseSs,
    kVmxVDiag_Vmentry_GuestSegBaseTr,
    kVmxVDiag_Vmentry_GuestSegBaseV86Cs,
    kVmxVDiag_Vmentry_GuestSegBaseV86Ds,
    kVmxVDiag_Vmentry_GuestSegBaseV86Es,
    kVmxVDiag_Vmentry_GuestSegBaseV86Fs,
    kVmxVDiag_Vmentry_GuestSegBaseV86Gs,
    kVmxVDiag_Vmentry_GuestSegBaseV86Ss,
    kVmxVDiag_Vmentry_GuestSegLimitV86Cs,
    kVmxVDiag_Vmentry_GuestSegLimitV86Ds,
    kVmxVDiag_Vmentry_GuestSegLimitV86Es,
    kVmxVDiag_Vmentry_GuestSegLimitV86Fs,
    kVmxVDiag_Vmentry_GuestSegLimitV86Gs,
    kVmxVDiag_Vmentry_GuestSegLimitV86Ss,
    kVmxVDiag_Vmentry_GuestSegSelCsSsRpl,
    kVmxVDiag_Vmentry_GuestSegSelLdtr,
    kVmxVDiag_Vmentry_GuestSegSelTr,
    kVmxVDiag_Vmentry_GuestSysenterEspEip,
    kVmxVDiag_Vmentry_VmcsLinkPtrCurVmcs,
    kVmxVDiag_Vmentry_VmcsLinkPtrReadPhys,
    kVmxVDiag_Vmentry_VmcsLinkPtrRevId,
    kVmxVDiag_Vmentry_VmcsLinkPtrShadow,
    kVmxVDiag_Vmentry_HostCr0Fixed0,
    kVmxVDiag_Vmentry_HostCr0Fixed1,
    kVmxVDiag_Vmentry_HostCr3,
    kVmxVDiag_Vmentry_HostCr4Fixed0,
    kVmxVDiag_Vmentry_HostCr4Fixed1,
    kVmxVDiag_Vmentry_HostCr4Pae,
    kVmxVDiag_Vmentry_HostCr4Pcide,
    kVmxVDiag_Vmentry_HostCsTr,
    kVmxVDiag_Vmentry_HostEferMsr,
    kVmxVDiag_Vmentry_HostEferMsrRsvd,
    kVmxVDiag_Vmentry_HostGuestLongMode,
    kVmxVDiag_Vmentry_HostGuestLongModeNoCpu,
    kVmxVDiag_Vmentry_HostLongMode,
    kVmxVDiag_Vmentry_HostPatMsr,
    kVmxVDiag_Vmentry_HostRip,
    kVmxVDiag_Vmentry_HostRipRsvd,
    kVmxVDiag_Vmentry_HostSel,
    kVmxVDiag_Vmentry_HostSegBase,
    kVmxVDiag_Vmentry_HostSs,
    kVmxVDiag_Vmentry_HostSysenterEspEip,
    kVmxVDiag_Vmentry_IoBitmapAPtrReadPhys,
    kVmxVDiag_Vmentry_IoBitmapBPtrReadPhys,
    kVmxVDiag_Vmentry_LongModeCS,
    kVmxVDiag_Vmentry_MsrBitmapPtrReadPhys,
    kVmxVDiag_Vmentry_MsrLoad,
    kVmxVDiag_Vmentry_MsrLoadCount,
    kVmxVDiag_Vmentry_MsrLoadPtrReadPhys,
    kVmxVDiag_Vmentry_MsrLoadRing3,
    kVmxVDiag_Vmentry_MsrLoadRsvd,
    kVmxVDiag_Vmentry_NmiWindowExit,
    kVmxVDiag_Vmentry_PinCtlsAllowed1,
    kVmxVDiag_Vmentry_PinCtlsDisallowed0,
    kVmxVDiag_Vmentry_ProcCtlsAllowed1,
    kVmxVDiag_Vmentry_ProcCtlsDisallowed0,
    kVmxVDiag_Vmentry_ProcCtls2Allowed1,
    kVmxVDiag_Vmentry_ProcCtls2Disallowed0,
    kVmxVDiag_Vmentry_PtrInvalid,
    kVmxVDiag_Vmentry_PtrShadowVmcs,
    kVmxVDiag_Vmentry_RealOrV86Mode,
    kVmxVDiag_Vmentry_SavePreemptTimer,
    kVmxVDiag_Vmentry_TprThresholdRsvd,
    kVmxVDiag_Vmentry_TprThresholdVTpr,
    kVmxVDiag_Vmentry_VirtApicPagePtrReadPhys,
    kVmxVDiag_Vmentry_VirtIntDelivery,
    kVmxVDiag_Vmentry_VirtNmi,
    kVmxVDiag_Vmentry_VirtX2ApicTprShadow,
    kVmxVDiag_Vmentry_VirtX2ApicVirtApic,
    kVmxVDiag_Vmentry_VmcsClear,
    kVmxVDiag_Vmentry_VmcsLaunch,
    kVmxVDiag_Vmentry_VmreadBitmapPtrReadPhys,
    kVmxVDiag_Vmentry_VmwriteBitmapPtrReadPhys,
    kVmxVDiag_Vmentry_VmxRoot,
    kVmxVDiag_Vmentry_Vpid,
    kVmxVDiag_Vmexit_HostPdpte,
    kVmxVDiag_Vmexit_MsrLoad,
    kVmxVDiag_Vmexit_MsrLoadCount,
    kVmxVDiag_Vmexit_MsrLoadPtrReadPhys,
    kVmxVDiag_Vmexit_MsrLoadRing3,
    kVmxVDiag_Vmexit_MsrLoadRsvd,
    kVmxVDiag_Vmexit_MsrStore,
    kVmxVDiag_Vmexit_MsrStoreCount,
    kVmxVDiag_Vmexit_MsrStorePtrReadPhys,
    kVmxVDiag_Vmexit_MsrStorePtrWritePhys,
    kVmxVDiag_Vmexit_MsrStoreRing3,
    kVmxVDiag_Vmexit_MsrStoreRsvd,
    kVmxVDiag_Vmexit_VirtApicPagePtrWritePhys,
    /* Last member for determining array index limit. */
    kVmxVDiag_End
} VMXVDIAG;
AssertCompileSize(VMXVDIAG, 4);

/** @} */

/** @} */

#endif /* !VBOX_INCLUDED_vmm_hm_vmx_h */

