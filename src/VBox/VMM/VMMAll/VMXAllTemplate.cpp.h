/* $Id: VMXAllTemplate.cpp.h 110113 2025-07-04 08:55:57Z ramshankar.venkataraman@oracle.com $ */
/** @file
 * HM VMX (Intel VT-x) - Code template for our own hypervisor and the NEM darwin backend using Apple's Hypervisor.framework.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if !defined(VMX_VMCS_WRITE_16) || !defined(VMX_VMCS_WRITE_32) || !defined(VMX_VMCS_WRITE_64) || !defined(VMX_VMCS_WRITE_64)
# error "At least one of the VMX_VMCS_WRITE_16, VMX_VMCS_WRITE_32, VMX_VMCS_WRITE_64 or VMX_VMCS_WRITE_64 is missing"
#endif


#if !defined(VMX_VMCS_READ_16) || !defined(VMX_VMCS_READ_32) || !defined(VMX_VMCS_READ_64) || !defined(VMX_VMCS_READ_64)
# error "At least one of the VMX_VMCS_READ_16, VMX_VMCS_READ_32, VMX_VMCS_READ_64 or VMX_VMCS_READ_64 is missing"
#endif

/** Enables condensing of VMREAD instructions, see vmxHCReadToTransient(). */
#define HMVMX_WITH_CONDENSED_VMREADS

/** Use the function table. */
#define HMVMX_USE_FUNCTION_TABLE

/** Determine which tagged-TLB flush handler to use. */
#define HMVMX_FLUSH_TAGGED_TLB_EPT_VPID             0
#define HMVMX_FLUSH_TAGGED_TLB_EPT                  1
#define HMVMX_FLUSH_TAGGED_TLB_VPID                 2
#define HMVMX_FLUSH_TAGGED_TLB_NONE                 3

/** Assert that all the given fields have been read from the VMCS. */
#ifdef VBOX_STRICT
# define HMVMX_ASSERT_READ(a_pVmxTransient, a_fReadFields) \
        do { \
            uint32_t const fVmcsFieldRead = ASMAtomicUoReadU32(&pVmxTransient->fVmcsFieldsRead); \
            Assert((fVmcsFieldRead & (a_fReadFields)) == (a_fReadFields)); \
        } while (0)
#else
# define HMVMX_ASSERT_READ(a_pVmxTransient, a_fReadFields) do { } while (0)
#endif

/**
 * Subset of the guest-CPU state that is kept by VMX R0 code while executing the
 * guest using hardware-assisted VMX.
 *
 * This excludes state like GPRs (other than RSP) which are always are
 * swapped and restored across the world-switch and also registers like EFER,
 * MSR which cannot be modified by the guest without causing a VM-exit.
 */
#define HMVMX_CPUMCTX_EXTRN_ALL      (  CPUMCTX_EXTRN_RIP             \
                                      | CPUMCTX_EXTRN_RFLAGS          \
                                      | CPUMCTX_EXTRN_RSP             \
                                      | CPUMCTX_EXTRN_SREG_MASK       \
                                      | CPUMCTX_EXTRN_TABLE_MASK      \
                                      | CPUMCTX_EXTRN_KERNEL_GS_BASE  \
                                      | CPUMCTX_EXTRN_SYSCALL_MSRS    \
                                      | CPUMCTX_EXTRN_SYSENTER_MSRS   \
                                      | CPUMCTX_EXTRN_TSC_AUX         \
                                      | CPUMCTX_EXTRN_OTHER_MSRS      \
                                      | CPUMCTX_EXTRN_CR0             \
                                      | CPUMCTX_EXTRN_CR3             \
                                      | CPUMCTX_EXTRN_CR4             \
                                      | CPUMCTX_EXTRN_DR7             \
                                      | CPUMCTX_EXTRN_HWVIRT          \
                                      | CPUMCTX_EXTRN_INHIBIT_INT     \
                                      | CPUMCTX_EXTRN_INHIBIT_NMI)

/**
 * Guest-CPU state required for split-lock \#AC handling VM-exits.
 */
#define HMVMX_CPUMCTX_XPCT_AC        (  CPUMCTX_EXTRN_CR0 \
                                      | CPUMCTX_EXTRN_RFLAGS \
                                      | CPUMCTX_EXTRN_SS \
                                      | CPUMCTX_EXTRN_CS)

/**
 * Exception bitmap mask for real-mode guests (real-on-v86).
 *
 * We need to intercept all exceptions manually except:
 * - \#AC and \#DB are always intercepted to prevent the CPU from deadlocking
 *   due to bugs in Intel CPUs.
 * - \#PF need not be intercepted even in real-mode if we have nested paging
 * support.
 */
#define HMVMX_REAL_MODE_XCPT_MASK    (  RT_BIT(X86_XCPT_DE)  /* always: | RT_BIT(X86_XCPT_DB) */ | RT_BIT(X86_XCPT_NMI)   \
                                      | RT_BIT(X86_XCPT_BP)             | RT_BIT(X86_XCPT_OF)    | RT_BIT(X86_XCPT_BR)    \
                                      | RT_BIT(X86_XCPT_UD)             | RT_BIT(X86_XCPT_NM)    | RT_BIT(X86_XCPT_DF)    \
                                      | RT_BIT(X86_XCPT_CO_SEG_OVERRUN) | RT_BIT(X86_XCPT_TS)    | RT_BIT(X86_XCPT_NP)    \
                                      | RT_BIT(X86_XCPT_SS)             | RT_BIT(X86_XCPT_GP)   /* RT_BIT(X86_XCPT_PF) */ \
                                      | RT_BIT(X86_XCPT_MF)  /* always: | RT_BIT(X86_XCPT_AC) */ | RT_BIT(X86_XCPT_MC)    \
                                      | RT_BIT(X86_XCPT_XF))

/** Maximum VM-instruction error number. */
#define HMVMX_INSTR_ERROR_MAX        28

/** Profiling macro. */
#ifdef HM_PROFILE_EXIT_DISPATCH
# define HMVMX_START_EXIT_DISPATCH_PROF()           STAM_PROFILE_ADV_START(&VCPU_2_VMXSTATS(pVCpu).StatExitDispatch, ed)
# define HMVMX_STOP_EXIT_DISPATCH_PROF()            STAM_PROFILE_ADV_STOP(&VCPU_2_VMXSTATS(pVCpu).StatExitDispatch, ed)
#else
# define HMVMX_START_EXIT_DISPATCH_PROF()           do { } while (0)
# define HMVMX_STOP_EXIT_DISPATCH_PROF()            do { } while (0)
#endif

#ifndef IN_NEM_DARWIN
/** Assert that preemption is disabled or covered by thread-context hooks. */
# define HMVMX_ASSERT_PREEMPT_SAFE(a_pVCpu)          Assert(   VMMR0ThreadCtxHookIsEnabled((a_pVCpu))   \
                                                            || !RTThreadPreemptIsEnabled(NIL_RTTHREAD))

/** Assert that we haven't migrated CPUs when thread-context hooks are not
 *  used. */
# define HMVMX_ASSERT_CPU_SAFE(a_pVCpu)              AssertMsg(   VMMR0ThreadCtxHookIsEnabled((a_pVCpu)) \
                                                               || (a_pVCpu)->hmr0.s.idEnteredCpu == RTMpCpuId(), \
                                                               ("Illegal migration! Entered on CPU %u Current %u\n", \
                                                               (a_pVCpu)->hmr0.s.idEnteredCpu, RTMpCpuId()))
#else
# define HMVMX_ASSERT_PREEMPT_SAFE(a_pVCpu)          do { } while (0)
# define HMVMX_ASSERT_CPU_SAFE(a_pVCpu)              do { } while (0)
#endif

/** Asserts that the given CPUMCTX_EXTRN_XXX bits are present in the guest-CPU
 *  context. */
#define HMVMX_CPUMCTX_ASSERT(a_pVCpu, a_fExtrnMbz)  AssertMsg(!((a_pVCpu)->cpum.GstCtx.fExtrn & (a_fExtrnMbz)), \
                                                              ("fExtrn=%#RX64 fExtrnMbz=%#RX64\n", \
                                                              (a_pVCpu)->cpum.GstCtx.fExtrn, (a_fExtrnMbz)))

/** Log the VM-exit reason with an easily visible marker to identify it in a
 *  potential sea of logging data. */
#define HMVMX_LOG_EXIT(a_pVCpu, a_uExitReason) \
    do { \
        Log4(("VM-exit: vcpu[%RU32] %85s -v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-\n", (a_pVCpu)->idCpu, \
             HMGetVmxExitName(a_uExitReason))); \
    } while (0) \


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Memory operand read or write access.
 */
typedef enum VMXMEMACCESS
{
    VMXMEMACCESS_READ  = 0,
    VMXMEMACCESS_WRITE = 1
} VMXMEMACCESS;


/**
 * VMX VM-exit handler.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
#ifndef HMVMX_USE_FUNCTION_TABLE
typedef VBOXSTRICTRC               FNVMXEXITHANDLER(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient);
#else
typedef DECLCALLBACKTYPE(VBOXSTRICTRC, FNVMXEXITHANDLER,(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient));
/** Pointer to VM-exit handler. */
typedef FNVMXEXITHANDLER          *PFNVMXEXITHANDLER;
#endif

/**
 * VMX VM-exit handler, non-strict status code.
 *
 * This is generally the same as FNVMXEXITHANDLER, the NSRC bit is just FYI.
 *
 * @returns VBox status code, no informational status code returned.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks This is not used on anything returning VERR_EM_INTERPRETER as the
 *          use of that status code will be replaced with VINF_EM_SOMETHING
 *          later when switching over to IEM.
 */
#ifndef HMVMX_USE_FUNCTION_TABLE
typedef int                        FNVMXEXITHANDLERNSRC(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient);
#else
typedef FNVMXEXITHANDLER           FNVMXEXITHANDLERNSRC;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifndef HMVMX_USE_FUNCTION_TABLE
DECLINLINE(VBOXSTRICTRC)           vmxHCHandleExit(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient);
# define HMVMX_EXIT_DECL           DECLINLINE(VBOXSTRICTRC)
# define HMVMX_EXIT_NSRC_DECL      DECLINLINE(int)
#else
# define HMVMX_EXIT_DECL           static DECLCALLBACK(VBOXSTRICTRC)
# define HMVMX_EXIT_NSRC_DECL      HMVMX_EXIT_DECL
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
DECLINLINE(VBOXSTRICTRC)           vmxHCHandleExitNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient);
#endif

static int vmxHCImportGuestStateEx(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, uint64_t fWhat);

/** @name VM-exit handler prototypes.
 * @{
 */
static FNVMXEXITHANDLER            vmxHCExitXcptOrNmi;
static FNVMXEXITHANDLER            vmxHCExitExtInt;
static FNVMXEXITHANDLER            vmxHCExitTripleFault;
static FNVMXEXITHANDLERNSRC        vmxHCExitIntWindow;
static FNVMXEXITHANDLERNSRC        vmxHCExitNmiWindow;
static FNVMXEXITHANDLER            vmxHCExitTaskSwitch;
static FNVMXEXITHANDLER            vmxHCExitCpuid;
static FNVMXEXITHANDLER            vmxHCExitGetsec;
static FNVMXEXITHANDLER            vmxHCExitHlt;
static FNVMXEXITHANDLERNSRC        vmxHCExitInvd;
static FNVMXEXITHANDLER            vmxHCExitInvlpg;
static FNVMXEXITHANDLER            vmxHCExitRdpmc;
static FNVMXEXITHANDLER            vmxHCExitVmcall;
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
static FNVMXEXITHANDLER            vmxHCExitVmclear;
static FNVMXEXITHANDLER            vmxHCExitVmlaunch;
static FNVMXEXITHANDLER            vmxHCExitVmptrld;
static FNVMXEXITHANDLER            vmxHCExitVmptrst;
static FNVMXEXITHANDLER            vmxHCExitVmread;
static FNVMXEXITHANDLER            vmxHCExitVmresume;
static FNVMXEXITHANDLER            vmxHCExitVmwrite;
static FNVMXEXITHANDLER            vmxHCExitVmxoff;
static FNVMXEXITHANDLER            vmxHCExitVmxon;
static FNVMXEXITHANDLER            vmxHCExitInvvpid;
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
static FNVMXEXITHANDLER            vmxHCExitInvept;
# endif
#endif
static FNVMXEXITHANDLER            vmxHCExitRdtsc;
static FNVMXEXITHANDLER            vmxHCExitMovCRx;
static FNVMXEXITHANDLER            vmxHCExitMovDRx;
static FNVMXEXITHANDLER            vmxHCExitIoInstr;
static FNVMXEXITHANDLER            vmxHCExitRdmsr;
static FNVMXEXITHANDLER            vmxHCExitWrmsr;
static FNVMXEXITHANDLER            vmxHCExitMwait;
static FNVMXEXITHANDLER            vmxHCExitMtf;
static FNVMXEXITHANDLER            vmxHCExitMonitor;
static FNVMXEXITHANDLER            vmxHCExitPause;
static FNVMXEXITHANDLERNSRC        vmxHCExitTprBelowThreshold;
static FNVMXEXITHANDLER            vmxHCExitApicAccess;
static FNVMXEXITHANDLER            vmxHCExitEptViolation;
static FNVMXEXITHANDLER            vmxHCExitEptMisconfig;
static FNVMXEXITHANDLER            vmxHCExitRdtscp;
static FNVMXEXITHANDLER            vmxHCExitPreemptTimer;
static FNVMXEXITHANDLERNSRC        vmxHCExitWbinvd;
static FNVMXEXITHANDLER            vmxHCExitXsetbv;
static FNVMXEXITHANDLER            vmxHCExitInvpcid;
#ifndef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
static FNVMXEXITHANDLERNSRC        vmxHCExitSetPendingXcptUD;
#endif
static FNVMXEXITHANDLERNSRC        vmxHCExitErrInvalidGuestState;
static FNVMXEXITHANDLERNSRC        vmxHCExitErrUnexpected;
/** @} */

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/** @name Nested-guest VM-exit handler prototypes.
 * @{
 */
static FNVMXEXITHANDLER            vmxHCExitXcptOrNmiNested;
static FNVMXEXITHANDLER            vmxHCExitTripleFaultNested;
static FNVMXEXITHANDLERNSRC        vmxHCExitIntWindowNested;
static FNVMXEXITHANDLERNSRC        vmxHCExitNmiWindowNested;
static FNVMXEXITHANDLER            vmxHCExitTaskSwitchNested;
static FNVMXEXITHANDLER            vmxHCExitHltNested;
static FNVMXEXITHANDLER            vmxHCExitInvlpgNested;
static FNVMXEXITHANDLER            vmxHCExitRdpmcNested;
static FNVMXEXITHANDLER            vmxHCExitVmreadVmwriteNested;
static FNVMXEXITHANDLER            vmxHCExitRdtscNested;
static FNVMXEXITHANDLER            vmxHCExitMovCRxNested;
static FNVMXEXITHANDLER            vmxHCExitMovDRxNested;
static FNVMXEXITHANDLER            vmxHCExitIoInstrNested;
static FNVMXEXITHANDLER            vmxHCExitRdmsrNested;
static FNVMXEXITHANDLER            vmxHCExitWrmsrNested;
static FNVMXEXITHANDLER            vmxHCExitMwaitNested;
static FNVMXEXITHANDLER            vmxHCExitMtfNested;
static FNVMXEXITHANDLER            vmxHCExitMonitorNested;
static FNVMXEXITHANDLER            vmxHCExitPauseNested;
static FNVMXEXITHANDLERNSRC        vmxHCExitTprBelowThresholdNested;
static FNVMXEXITHANDLER            vmxHCExitApicAccessNested;
static FNVMXEXITHANDLER            vmxHCExitApicWriteNested;
static FNVMXEXITHANDLER            vmxHCExitVirtEoiNested;
static FNVMXEXITHANDLER            vmxHCExitRdtscpNested;
static FNVMXEXITHANDLERNSRC        vmxHCExitWbinvdNested;
static FNVMXEXITHANDLER            vmxHCExitInvpcidNested;
static FNVMXEXITHANDLERNSRC        vmxHCExitErrInvalidGuestStateNested;
static FNVMXEXITHANDLER            vmxHCExitInstrNested;
static FNVMXEXITHANDLER            vmxHCExitInstrWithInfoNested;
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
static FNVMXEXITHANDLER            vmxHCExitEptViolationNested;
static FNVMXEXITHANDLER            vmxHCExitEptMisconfigNested;
# endif
/** @} */
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/**
 * Array of all VMCS fields.
 * Any fields added to the VT-x spec. should be added here.
 *
 * Currently only used to derive shadow VMCS fields for hardware-assisted execution
 * of nested-guests.
 */
static const uint32_t g_aVmcsFields[] =
{
    /* 16-bit control fields. */
    VMX_VMCS16_VPID,
    VMX_VMCS16_POSTED_INT_NOTIFY_VECTOR,
    VMX_VMCS16_EPTP_INDEX,
    VMX_VMCS16_HLAT_PREFIX_SIZE,
    VMX_VMCS16_LAST_PID_PTR_INDEX,

    /* 16-bit guest-state fields. */
    VMX_VMCS16_GUEST_ES_SEL,
    VMX_VMCS16_GUEST_CS_SEL,
    VMX_VMCS16_GUEST_SS_SEL,
    VMX_VMCS16_GUEST_DS_SEL,
    VMX_VMCS16_GUEST_FS_SEL,
    VMX_VMCS16_GUEST_GS_SEL,
    VMX_VMCS16_GUEST_LDTR_SEL,
    VMX_VMCS16_GUEST_TR_SEL,
    VMX_VMCS16_GUEST_INTR_STATUS,
    VMX_VMCS16_GUEST_PML_INDEX,
    VMX_VMCS16_GUEST_UINV,

    /* 16-bits host-state fields. */
    VMX_VMCS16_HOST_ES_SEL,
    VMX_VMCS16_HOST_CS_SEL,
    VMX_VMCS16_HOST_SS_SEL,
    VMX_VMCS16_HOST_DS_SEL,
    VMX_VMCS16_HOST_FS_SEL,
    VMX_VMCS16_HOST_GS_SEL,
    VMX_VMCS16_HOST_TR_SEL,

    /* 64-bit control fields. */
    VMX_VMCS64_CTRL_IO_BITMAP_A_FULL,
    VMX_VMCS64_CTRL_IO_BITMAP_A_HIGH,
    VMX_VMCS64_CTRL_IO_BITMAP_B_FULL,
    VMX_VMCS64_CTRL_IO_BITMAP_B_HIGH,
    VMX_VMCS64_CTRL_MSR_BITMAP_FULL,
    VMX_VMCS64_CTRL_MSR_BITMAP_HIGH,
    VMX_VMCS64_CTRL_EXIT_MSR_STORE_FULL,
    VMX_VMCS64_CTRL_EXIT_MSR_STORE_HIGH,
    VMX_VMCS64_CTRL_EXIT_MSR_LOAD_FULL,
    VMX_VMCS64_CTRL_EXIT_MSR_LOAD_HIGH,
    VMX_VMCS64_CTRL_ENTRY_MSR_LOAD_FULL,
    VMX_VMCS64_CTRL_ENTRY_MSR_LOAD_HIGH,
    VMX_VMCS64_CTRL_EXEC_VMCS_PTR_FULL,
    VMX_VMCS64_CTRL_EXEC_VMCS_PTR_HIGH,
    VMX_VMCS64_CTRL_EXEC_PML_ADDR_FULL,
    VMX_VMCS64_CTRL_EXEC_PML_ADDR_HIGH,
    VMX_VMCS64_CTRL_TSC_OFFSET_FULL,
    VMX_VMCS64_CTRL_TSC_OFFSET_HIGH,
    VMX_VMCS64_CTRL_VIRT_APIC_PAGEADDR_FULL,
    VMX_VMCS64_CTRL_VIRT_APIC_PAGEADDR_HIGH,
    VMX_VMCS64_CTRL_APIC_ACCESSADDR_FULL,
    VMX_VMCS64_CTRL_APIC_ACCESSADDR_HIGH,
    VMX_VMCS64_CTRL_POSTED_INTR_DESC_FULL,
    VMX_VMCS64_CTRL_POSTED_INTR_DESC_HIGH,
    VMX_VMCS64_CTRL_VMFUNC_CTRLS_FULL,
    VMX_VMCS64_CTRL_VMFUNC_CTRLS_HIGH,
    VMX_VMCS64_CTRL_EPTP_FULL,
    VMX_VMCS64_CTRL_EPTP_HIGH,
    VMX_VMCS64_CTRL_EOI_BITMAP_0_FULL,
    VMX_VMCS64_CTRL_EOI_BITMAP_0_HIGH,
    VMX_VMCS64_CTRL_EOI_BITMAP_1_FULL,
    VMX_VMCS64_CTRL_EOI_BITMAP_1_HIGH,
    VMX_VMCS64_CTRL_EOI_BITMAP_2_FULL,
    VMX_VMCS64_CTRL_EOI_BITMAP_2_HIGH,
    VMX_VMCS64_CTRL_EOI_BITMAP_3_FULL,
    VMX_VMCS64_CTRL_EOI_BITMAP_3_HIGH,
    VMX_VMCS64_CTRL_EPTP_LIST_FULL,
    VMX_VMCS64_CTRL_EPTP_LIST_HIGH,
    VMX_VMCS64_CTRL_VMREAD_BITMAP_FULL,
    VMX_VMCS64_CTRL_VMREAD_BITMAP_HIGH,
    VMX_VMCS64_CTRL_VMWRITE_BITMAP_FULL,
    VMX_VMCS64_CTRL_VMWRITE_BITMAP_HIGH,
    VMX_VMCS64_CTRL_VE_XCPT_INFO_ADDR_FULL,
    VMX_VMCS64_CTRL_VE_XCPT_INFO_ADDR_HIGH,
    VMX_VMCS64_CTRL_XSS_EXITING_BITMAP_FULL,
    VMX_VMCS64_CTRL_XSS_EXITING_BITMAP_HIGH,
    VMX_VMCS64_CTRL_ENCLS_EXITING_BITMAP_FULL,
    VMX_VMCS64_CTRL_ENCLS_EXITING_BITMAP_HIGH,
    VMX_VMCS64_CTRL_SPPTP_FULL,
    VMX_VMCS64_CTRL_SPPTP_HIGH,
    VMX_VMCS64_CTRL_TSC_MULTIPLIER_FULL,
    VMX_VMCS64_CTRL_TSC_MULTIPLIER_HIGH,
    VMX_VMCS64_CTRL_PROC_EXEC3_FULL,
    VMX_VMCS64_CTRL_PROC_EXEC3_HIGH,
    VMX_VMCS64_CTRL_ENCLV_EXITING_BITMAP_FULL,
    VMX_VMCS64_CTRL_ENCLV_EXITING_BITMAP_HIGH,
    VMX_VMCS64_CTRL_PCONFIG_EXITING_BITMAP_FULL,
    VMX_VMCS64_CTRL_PCONFIG_EXITING_BITMAP_HIGH,
    VMX_VMCS64_CTRL_HLAT_PTR_FULL,
    VMX_VMCS64_CTRL_HLAT_PTR_HIGH,
    VMX_VMCS64_CTRL_EXIT2_FULL,
    VMX_VMCS64_CTRL_EXIT2_HIGH,
    VMX_VMCS64_CTRL_SPEC_CTRL_MASK_FULL,
    VMX_VMCS64_CTRL_SPEC_CTRL_MASK_HIGH,
    VMX_VMCS64_CTRL_SPEC_CTRL_SHADOW_FULL,
    VMX_VMCS64_CTRL_SPEC_CTRL_SHADOW_HIGH,

    /* 64-bit read-only data fields. */
    VMX_VMCS64_RO_GUEST_PHYS_ADDR_FULL,
    VMX_VMCS64_RO_GUEST_PHYS_ADDR_HIGH,

    /* 64-bit guest-state fields. */
    VMX_VMCS64_GUEST_VMCS_LINK_PTR_FULL,
    VMX_VMCS64_GUEST_VMCS_LINK_PTR_HIGH,
    VMX_VMCS64_GUEST_DEBUGCTL_FULL,
    VMX_VMCS64_GUEST_DEBUGCTL_HIGH,
    VMX_VMCS64_GUEST_PAT_FULL,
    VMX_VMCS64_GUEST_PAT_HIGH,
    VMX_VMCS64_GUEST_EFER_FULL,
    VMX_VMCS64_GUEST_EFER_HIGH,
    VMX_VMCS64_GUEST_PERF_GLOBAL_CTRL_FULL,
    VMX_VMCS64_GUEST_PERF_GLOBAL_CTRL_HIGH,
    VMX_VMCS64_GUEST_PDPTE0_FULL,
    VMX_VMCS64_GUEST_PDPTE0_HIGH,
    VMX_VMCS64_GUEST_PDPTE1_FULL,
    VMX_VMCS64_GUEST_PDPTE1_HIGH,
    VMX_VMCS64_GUEST_PDPTE2_FULL,
    VMX_VMCS64_GUEST_PDPTE2_HIGH,
    VMX_VMCS64_GUEST_PDPTE3_FULL,
    VMX_VMCS64_GUEST_PDPTE3_HIGH,
    VMX_VMCS64_GUEST_BNDCFGS_FULL,
    VMX_VMCS64_GUEST_BNDCFGS_HIGH,
    VMX_VMCS64_GUEST_RTIT_CTL_FULL,
    VMX_VMCS64_GUEST_RTIT_CTL_HIGH,
    VMX_VMCS64_GUEST_PKRS_FULL,
    VMX_VMCS64_GUEST_PKRS_HIGH,

    /* 64-bit host-state fields. */
    VMX_VMCS64_HOST_PAT_FULL,
    VMX_VMCS64_HOST_PAT_HIGH,
    VMX_VMCS64_HOST_EFER_FULL,
    VMX_VMCS64_HOST_EFER_HIGH,
    VMX_VMCS64_HOST_PERF_GLOBAL_CTRL_FULL,
    VMX_VMCS64_HOST_PERF_GLOBAL_CTRL_HIGH,
    VMX_VMCS64_HOST_PKRS_FULL,
    VMX_VMCS64_HOST_PKRS_HIGH,

    /* 32-bit control fields. */
    VMX_VMCS32_CTRL_PIN_EXEC,
    VMX_VMCS32_CTRL_PROC_EXEC,
    VMX_VMCS32_CTRL_EXCEPTION_BITMAP,
    VMX_VMCS32_CTRL_PAGEFAULT_ERROR_MASK,
    VMX_VMCS32_CTRL_PAGEFAULT_ERROR_MATCH,
    VMX_VMCS32_CTRL_CR3_TARGET_COUNT,
    VMX_VMCS32_CTRL_EXIT,
    VMX_VMCS32_CTRL_EXIT_MSR_STORE_COUNT,
    VMX_VMCS32_CTRL_EXIT_MSR_LOAD_COUNT,
    VMX_VMCS32_CTRL_ENTRY,
    VMX_VMCS32_CTRL_ENTRY_MSR_LOAD_COUNT,
    VMX_VMCS32_CTRL_ENTRY_INTERRUPTION_INFO,
    VMX_VMCS32_CTRL_ENTRY_EXCEPTION_ERRCODE,
    VMX_VMCS32_CTRL_ENTRY_INSTR_LENGTH,
    VMX_VMCS32_CTRL_TPR_THRESHOLD,
    VMX_VMCS32_CTRL_PROC_EXEC2,
    VMX_VMCS32_CTRL_PLE_GAP,
    VMX_VMCS32_CTRL_PLE_WINDOW,
    VMX_VMCS32_CTRL_INSTR_TIMEOUT,

    /* 32-bits read-only fields. */
    VMX_VMCS32_RO_VM_INSTR_ERROR,
    VMX_VMCS32_RO_EXIT_REASON,
    VMX_VMCS32_RO_EXIT_INTERRUPTION_INFO,
    VMX_VMCS32_RO_EXIT_INTERRUPTION_ERROR_CODE,
    VMX_VMCS32_RO_IDT_VECTORING_INFO,
    VMX_VMCS32_RO_IDT_VECTORING_ERROR_CODE,
    VMX_VMCS32_RO_EXIT_INSTR_LENGTH,
    VMX_VMCS32_RO_EXIT_INSTR_INFO,

    /* 32-bit guest-state fields. */
    VMX_VMCS32_GUEST_ES_LIMIT,
    VMX_VMCS32_GUEST_CS_LIMIT,
    VMX_VMCS32_GUEST_SS_LIMIT,
    VMX_VMCS32_GUEST_DS_LIMIT,
    VMX_VMCS32_GUEST_FS_LIMIT,
    VMX_VMCS32_GUEST_GS_LIMIT,
    VMX_VMCS32_GUEST_LDTR_LIMIT,
    VMX_VMCS32_GUEST_TR_LIMIT,
    VMX_VMCS32_GUEST_GDTR_LIMIT,
    VMX_VMCS32_GUEST_IDTR_LIMIT,
    VMX_VMCS32_GUEST_ES_ACCESS_RIGHTS,
    VMX_VMCS32_GUEST_CS_ACCESS_RIGHTS,
    VMX_VMCS32_GUEST_SS_ACCESS_RIGHTS,
    VMX_VMCS32_GUEST_DS_ACCESS_RIGHTS,
    VMX_VMCS32_GUEST_FS_ACCESS_RIGHTS,
    VMX_VMCS32_GUEST_GS_ACCESS_RIGHTS,
    VMX_VMCS32_GUEST_LDTR_ACCESS_RIGHTS,
    VMX_VMCS32_GUEST_TR_ACCESS_RIGHTS,
    VMX_VMCS32_GUEST_INT_STATE,
    VMX_VMCS32_GUEST_ACTIVITY_STATE,
    VMX_VMCS32_GUEST_SMBASE,
    VMX_VMCS32_GUEST_SYSENTER_CS,
    VMX_VMCS32_PREEMPT_TIMER_VALUE,

    /* 32-bit host-state fields. */
    VMX_VMCS32_HOST_SYSENTER_CS,

    /* Natural-width control fields. */
    VMX_VMCS_CTRL_CR0_MASK,
    VMX_VMCS_CTRL_CR4_MASK,
    VMX_VMCS_CTRL_CR0_READ_SHADOW,
    VMX_VMCS_CTRL_CR4_READ_SHADOW,
    VMX_VMCS_CTRL_CR3_TARGET_VAL0,
    VMX_VMCS_CTRL_CR3_TARGET_VAL1,
    VMX_VMCS_CTRL_CR3_TARGET_VAL2,
    VMX_VMCS_CTRL_CR3_TARGET_VAL3,

    /* Natural-width read-only data fields. */
    VMX_VMCS_RO_EXIT_QUALIFICATION,
    VMX_VMCS_RO_IO_RCX,
    VMX_VMCS_RO_IO_RSI,
    VMX_VMCS_RO_IO_RDI,
    VMX_VMCS_RO_IO_RIP,
    VMX_VMCS_RO_GUEST_LINEAR_ADDR,

    /* Natural-width guest-state field */
    VMX_VMCS_GUEST_CR0,
    VMX_VMCS_GUEST_CR3,
    VMX_VMCS_GUEST_CR4,
    VMX_VMCS_GUEST_ES_BASE,
    VMX_VMCS_GUEST_CS_BASE,
    VMX_VMCS_GUEST_SS_BASE,
    VMX_VMCS_GUEST_DS_BASE,
    VMX_VMCS_GUEST_FS_BASE,
    VMX_VMCS_GUEST_GS_BASE,
    VMX_VMCS_GUEST_LDTR_BASE,
    VMX_VMCS_GUEST_TR_BASE,
    VMX_VMCS_GUEST_GDTR_BASE,
    VMX_VMCS_GUEST_IDTR_BASE,
    VMX_VMCS_GUEST_DR7,
    VMX_VMCS_GUEST_RSP,
    VMX_VMCS_GUEST_RIP,
    VMX_VMCS_GUEST_RFLAGS,
    VMX_VMCS_GUEST_PENDING_DEBUG_XCPTS,
    VMX_VMCS_GUEST_SYSENTER_ESP,
    VMX_VMCS_GUEST_SYSENTER_EIP,
    VMX_VMCS_GUEST_S_CET,
    VMX_VMCS_GUEST_SSP,
    VMX_VMCS_GUEST_INTR_SSP_TABLE_ADDR,

    /* Natural-width host-state fields */
    VMX_VMCS_HOST_CR0,
    VMX_VMCS_HOST_CR3,
    VMX_VMCS_HOST_CR4,
    VMX_VMCS_HOST_FS_BASE,
    VMX_VMCS_HOST_GS_BASE,
    VMX_VMCS_HOST_TR_BASE,
    VMX_VMCS_HOST_GDTR_BASE,
    VMX_VMCS_HOST_IDTR_BASE,
    VMX_VMCS_HOST_SYSENTER_ESP,
    VMX_VMCS_HOST_SYSENTER_EIP,
    VMX_VMCS_HOST_RSP,
    VMX_VMCS_HOST_RIP,
    VMX_VMCS_HOST_S_CET,
    VMX_VMCS_HOST_SSP,
    VMX_VMCS_HOST_INTR_SSP_TABLE_ADDR
};
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */

#ifdef HMVMX_USE_FUNCTION_TABLE
/**
 * VMX_EXIT dispatch table.
 */
static const struct CLANG11NOTHROWWEIRDNESS { PFNVMXEXITHANDLER pfn; } g_aVMExitHandlers[VMX_EXIT_MAX + 1] =
{
    /*  0  VMX_EXIT_XCPT_OR_NMI             */  { vmxHCExitXcptOrNmi },
    /*  1  VMX_EXIT_EXT_INT                 */  { vmxHCExitExtInt },
    /*  2  VMX_EXIT_TRIPLE_FAULT            */  { vmxHCExitTripleFault },
    /*  3  VMX_EXIT_INIT_SIGNAL             */  { vmxHCExitErrUnexpected },
    /*  4  VMX_EXIT_SIPI                    */  { vmxHCExitErrUnexpected },
    /*  5  VMX_EXIT_IO_SMI                  */  { vmxHCExitErrUnexpected },
    /*  6  VMX_EXIT_SMI                     */  { vmxHCExitErrUnexpected },
    /*  7  VMX_EXIT_INT_WINDOW              */  { vmxHCExitIntWindow },
    /*  8  VMX_EXIT_NMI_WINDOW              */  { vmxHCExitNmiWindow },
    /*  9  VMX_EXIT_TASK_SWITCH             */  { vmxHCExitTaskSwitch },
    /* 10  VMX_EXIT_CPUID                   */  { vmxHCExitCpuid },
    /* 11  VMX_EXIT_GETSEC                  */  { vmxHCExitGetsec },
    /* 12  VMX_EXIT_HLT                     */  { vmxHCExitHlt },
    /* 13  VMX_EXIT_INVD                    */  { vmxHCExitInvd },
    /* 14  VMX_EXIT_INVLPG                  */  { vmxHCExitInvlpg },
    /* 15  VMX_EXIT_RDPMC                   */  { vmxHCExitRdpmc },
    /* 16  VMX_EXIT_RDTSC                   */  { vmxHCExitRdtsc },
    /* 17  VMX_EXIT_RSM                     */  { vmxHCExitErrUnexpected },
    /* 18  VMX_EXIT_VMCALL                  */  { vmxHCExitVmcall },
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /* 19  VMX_EXIT_VMCLEAR                 */  { vmxHCExitVmclear },
    /* 20  VMX_EXIT_VMLAUNCH                */  { vmxHCExitVmlaunch },
    /* 21  VMX_EXIT_VMPTRLD                 */  { vmxHCExitVmptrld },
    /* 22  VMX_EXIT_VMPTRST                 */  { vmxHCExitVmptrst },
    /* 23  VMX_EXIT_VMREAD                  */  { vmxHCExitVmread },
    /* 24  VMX_EXIT_VMRESUME                */  { vmxHCExitVmresume },
    /* 25  VMX_EXIT_VMWRITE                 */  { vmxHCExitVmwrite },
    /* 26  VMX_EXIT_VMXOFF                  */  { vmxHCExitVmxoff },
    /* 27  VMX_EXIT_VMXON                   */  { vmxHCExitVmxon },
#else
    /* 19  VMX_EXIT_VMCLEAR                 */  { vmxHCExitSetPendingXcptUD },
    /* 20  VMX_EXIT_VMLAUNCH                */  { vmxHCExitSetPendingXcptUD },
    /* 21  VMX_EXIT_VMPTRLD                 */  { vmxHCExitSetPendingXcptUD },
    /* 22  VMX_EXIT_VMPTRST                 */  { vmxHCExitSetPendingXcptUD },
    /* 23  VMX_EXIT_VMREAD                  */  { vmxHCExitSetPendingXcptUD },
    /* 24  VMX_EXIT_VMRESUME                */  { vmxHCExitSetPendingXcptUD },
    /* 25  VMX_EXIT_VMWRITE                 */  { vmxHCExitSetPendingXcptUD },
    /* 26  VMX_EXIT_VMXOFF                  */  { vmxHCExitSetPendingXcptUD },
    /* 27  VMX_EXIT_VMXON                   */  { vmxHCExitSetPendingXcptUD },
#endif
    /* 28  VMX_EXIT_MOV_CRX                 */  { vmxHCExitMovCRx },
    /* 29  VMX_EXIT_MOV_DRX                 */  { vmxHCExitMovDRx },
    /* 30  VMX_EXIT_IO_INSTR                */  { vmxHCExitIoInstr },
    /* 31  VMX_EXIT_RDMSR                   */  { vmxHCExitRdmsr },
    /* 32  VMX_EXIT_WRMSR                   */  { vmxHCExitWrmsr },
    /* 33  VMX_EXIT_ERR_INVALID_GUEST_STATE */  { vmxHCExitErrInvalidGuestState },
    /* 34  VMX_EXIT_ERR_MSR_LOAD            */  { vmxHCExitErrUnexpected },
    /* 35  UNDEFINED                        */  { vmxHCExitErrUnexpected },
    /* 36  VMX_EXIT_MWAIT                   */  { vmxHCExitMwait },
    /* 37  VMX_EXIT_MTF                     */  { vmxHCExitMtf },
    /* 38  UNDEFINED                        */  { vmxHCExitErrUnexpected },
    /* 39  VMX_EXIT_MONITOR                 */  { vmxHCExitMonitor },
    /* 40  VMX_EXIT_PAUSE                   */  { vmxHCExitPause },
    /* 41  VMX_EXIT_ERR_MACHINE_CHECK       */  { vmxHCExitErrUnexpected },
    /* 42  UNDEFINED                        */  { vmxHCExitErrUnexpected },
    /* 43  VMX_EXIT_TPR_BELOW_THRESHOLD     */  { vmxHCExitTprBelowThreshold },
    /* 44  VMX_EXIT_APIC_ACCESS             */  { vmxHCExitApicAccess },
    /* 45  VMX_EXIT_VIRTUALIZED_EOI         */  { vmxHCExitErrUnexpected },
    /* 46  VMX_EXIT_GDTR_IDTR_ACCESS        */  { vmxHCExitErrUnexpected },
    /* 47  VMX_EXIT_LDTR_TR_ACCESS          */  { vmxHCExitErrUnexpected },
    /* 48  VMX_EXIT_EPT_VIOLATION           */  { vmxHCExitEptViolation },
    /* 49  VMX_EXIT_EPT_MISCONFIG           */  { vmxHCExitEptMisconfig },
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    /* 50  VMX_EXIT_INVEPT                  */  { vmxHCExitInvept },
#else
    /* 50  VMX_EXIT_INVEPT                  */  { vmxHCExitSetPendingXcptUD },
#endif
    /* 51  VMX_EXIT_RDTSCP                  */  { vmxHCExitRdtscp },
    /* 52  VMX_EXIT_PREEMPT_TIMER           */  { vmxHCExitPreemptTimer },
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /* 53  VMX_EXIT_INVVPID                 */  { vmxHCExitInvvpid },
#else
    /* 53  VMX_EXIT_INVVPID                 */  { vmxHCExitSetPendingXcptUD },
#endif
    /* 54  VMX_EXIT_WBINVD                  */  { vmxHCExitWbinvd },
    /* 55  VMX_EXIT_XSETBV                  */  { vmxHCExitXsetbv },
    /* 56  VMX_EXIT_APIC_WRITE              */  { vmxHCExitErrUnexpected },
    /* 57  VMX_EXIT_RDRAND                  */  { vmxHCExitErrUnexpected },
    /* 58  VMX_EXIT_INVPCID                 */  { vmxHCExitInvpcid },
    /* 59  VMX_EXIT_VMFUNC                  */  { vmxHCExitErrUnexpected },
    /* 60  VMX_EXIT_ENCLS                   */  { vmxHCExitErrUnexpected },
    /* 61  VMX_EXIT_RDSEED                  */  { vmxHCExitErrUnexpected },
    /* 62  VMX_EXIT_PML_FULL                */  { vmxHCExitErrUnexpected },
    /* 63  VMX_EXIT_XSAVES                  */  { vmxHCExitErrUnexpected },
    /* 64  VMX_EXIT_XRSTORS                 */  { vmxHCExitErrUnexpected },
    /* 65  UNDEFINED                        */  { vmxHCExitErrUnexpected },
    /* 66  VMX_EXIT_SPP_EVENT               */  { vmxHCExitErrUnexpected },
    /* 67  VMX_EXIT_UMWAIT                  */  { vmxHCExitErrUnexpected },
    /* 68  VMX_EXIT_TPAUSE                  */  { vmxHCExitErrUnexpected },
    /* 69  VMX_EXIT_LOADIWKEY               */  { vmxHCExitErrUnexpected },
};
#endif /* HMVMX_USE_FUNCTION_TABLE */

#if defined(VBOX_STRICT) && defined(LOG_ENABLED)
static const char * const g_apszVmxInstrErrors[HMVMX_INSTR_ERROR_MAX + 1] =
{
    /*  0 */ "(Not Used)",
    /*  1 */ "VMCALL executed in VMX root operation.",
    /*  2 */ "VMCLEAR with invalid physical address.",
    /*  3 */ "VMCLEAR with VMXON pointer.",
    /*  4 */ "VMLAUNCH with non-clear VMCS.",
    /*  5 */ "VMRESUME with non-launched VMCS.",
    /*  6 */ "VMRESUME after VMXOFF",
    /*  7 */ "VM-entry with invalid control fields.",
    /*  8 */ "VM-entry with invalid host state fields.",
    /*  9 */ "VMPTRLD with invalid physical address.",
    /* 10 */ "VMPTRLD with VMXON pointer.",
    /* 11 */ "VMPTRLD with incorrect revision identifier.",
    /* 12 */ "VMREAD/VMWRITE from/to unsupported VMCS component.",
    /* 13 */ "VMWRITE to read-only VMCS component.",
    /* 14 */ "(Not Used)",
    /* 15 */ "VMXON executed in VMX root operation.",
    /* 16 */ "VM-entry with invalid executive-VMCS pointer.",
    /* 17 */ "VM-entry with non-launched executing VMCS.",
    /* 18 */ "VM-entry with executive-VMCS pointer not VMXON pointer.",
    /* 19 */ "VMCALL with non-clear VMCS.",
    /* 20 */ "VMCALL with invalid VM-exit control fields.",
    /* 21 */ "(Not Used)",
    /* 22 */ "VMCALL with incorrect MSEG revision identifier.",
    /* 23 */ "VMXOFF under dual monitor treatment of SMIs and SMM.",
    /* 24 */ "VMCALL with invalid SMM-monitor features.",
    /* 25 */ "VM-entry with invalid VM-execution control fields in executive VMCS.",
    /* 26 */ "VM-entry with events blocked by MOV SS.",
    /* 27 */ "(Not Used)",
    /* 28 */ "Invalid operand to INVEPT/INVVPID."
};
#endif /* VBOX_STRICT && LOG_ENABLED */


/**
 * Gets the CR0 guest/host mask.
 *
 * These bits typically does not change through the lifetime of a VM. Any bit set in
 * this mask is owned by the host/hypervisor and would cause a VM-exit when modified
 * by the guest.
 *
 * @returns The CR0 guest/host mask.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static uint64_t vmxHCGetFixedCr0Mask(PCVMCPUCC pVCpu)
{
    /*
     * Modifications to CR0 bits that VT-x ignores saving/restoring (CD, ET, NW) and
     * to CR0 bits that we require for shadow paging (PG) by the guest must cause VM-exits.
     *
     * Furthermore, modifications to any bits that are reserved/unspecified currently
     * by the Intel spec. must also cause a VM-exit. This prevents unpredictable behavior
     * when future CPUs specify and use currently reserved/unspecified bits.
     */
    /** @todo Avoid intercepting CR0.PE with unrestricted guest execution. Fix PGM
     *        enmGuestMode to be in-sync with the current mode. See @bugref{6398}
     *        and @bugref{6944}. */
    PCVMCC pVM = pVCpu->CTX_SUFF(pVM);
    AssertCompile(RT_HI_U32(VMX_EXIT_HOST_CR0_IGNORE_MASK) == UINT32_C(0xffffffff));    /* Paranoia. */
    return (  X86_CR0_PE
            | X86_CR0_NE
            | (VM_IS_VMX_NESTED_PAGING(pVM) ? 0 : X86_CR0_WP)
            | X86_CR0_PG
            | VMX_EXIT_HOST_CR0_IGNORE_MASK);
}


/**
 * Gets the CR4 guest/host mask.
 *
 * These bits typically does not change through the lifetime of a VM. Any bit set in
 * this mask is owned by the host/hypervisor and would cause a VM-exit when modified
 * by the guest.
 *
 * @returns The CR4 guest/host mask.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static uint64_t vmxHCGetFixedCr4Mask(PCVMCPUCC pVCpu)
{
    /*
     * We construct a mask of all CR4 bits that the guest can modify without causing
     * a VM-exit. Then invert this mask to obtain all CR4 bits that should cause
     * a VM-exit when the guest attempts to modify them when executing using
     * hardware-assisted VMX.
     *
     * When a feature is not exposed to the guest (and may be present on the host),
     * we want to intercept guest modifications to the bit so we can emulate proper
     * behavior (e.g., #GP).
     *
     * Furthermore, only modifications to those bits that don't require immediate
     * emulation is allowed. For e.g., PCIDE is excluded because the behavior
     * depends on CR3 which might not always be the guest value while executing
     * using hardware-assisted VMX.
     */
    PCVMCC pVM = pVCpu->CTX_SUFF(pVM);
    bool fFsGsBase    = pVM->cpum.ro.GuestFeatures.fFsGsBase;
#ifdef IN_NEM_DARWIN
    bool fXSaveRstor  = pVM->cpum.ro.GuestFeatures.fXSaveRstor;
#endif
    bool fFxSaveRstor = pVM->cpum.ro.GuestFeatures.fFxSaveRstor;

    /*
     * Paranoia.
     * Ensure features exposed to the guest are present on the host.
     */
    AssertStmt(!fFsGsBase    || g_CpumHostFeatures.s.fFsGsBase,    fFsGsBase = 0);
#ifdef IN_NEM_DARWIN
    AssertStmt(!fXSaveRstor  || g_CpumHostFeatures.s.fXSaveRstor,  fXSaveRstor = 0);
#endif
    AssertStmt(!fFxSaveRstor || g_CpumHostFeatures.s.fFxSaveRstor, fFxSaveRstor = 0);

    uint64_t const fGstMask = X86_CR4_PVI
                            | X86_CR4_TSD
                            | X86_CR4_DE
                            | X86_CR4_MCE
                            | X86_CR4_PCE
                            | X86_CR4_OSXMMEEXCPT
                            | (fFsGsBase    ? X86_CR4_FSGSBASE : 0)
#ifdef IN_NEM_DARWIN /* On native VT-x setting OSXSAVE must exit as we need to load guest XCR0 (see
                        fLoadSaveGuestXcr0). These exits are not needed on Darwin as that's not our problem. */
                            | (fXSaveRstor  ? X86_CR4_OSXSAVE  : 0)
#endif
                            | (fFxSaveRstor ? X86_CR4_OSFXSR   : 0);
    return ~fGstMask;
}


/**
 * Checks whether an \#AC exception generated while executing a guest (or
 * nested-guest) was due to a split-lock memory access.
 *
 * @returns @c true if split-lock triggered the \#AC, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECL_FORCE_INLINE(bool) vmxHCIsSplitLockAcXcpt(PVMCPU pVCpu)
{
    HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_SS);
    if (   !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_AM)      /* 1. If 486-style alignment checks aren't enabled, this must be a split-lock #AC. */
        || !(pVCpu->cpum.GstCtx.eflags.u & X86_EFL_AC) /* 2. When the EFLAGS.AC != 0 this can only be a split-lock case. */
        ||  CPUMGetGuestCPL(pVCpu) != 3)               /* 3. #AC cannot happen in rings 0-2 except for split-lock detection. */
        return true;
    return false;
}


/**
 * Adds one or more exceptions to the exception bitmap and commits it to the current
 * VMCS.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 * @param   uXcptMask       The exception(s) to add.
 */
static void vmxHCAddXcptInterceptMask(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient, uint32_t uXcptMask)
{
    PVMXVMCSINFO pVmcsInfo   = pVmxTransient->pVmcsInfo;
    uint32_t     uXcptBitmap = pVmcsInfo->u32XcptBitmap;
    if ((uXcptBitmap & uXcptMask) != uXcptMask)
    {
        uXcptBitmap |= uXcptMask;
        int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_EXCEPTION_BITMAP, uXcptBitmap);
        AssertRC(rc);
        pVmcsInfo->u32XcptBitmap = uXcptBitmap;
    }
}


/**
 * Adds an exception to the exception bitmap and commits it to the current VMCS.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 * @param   uXcpt           The exception to add.
 */
static void vmxHCAddXcptIntercept(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient, uint8_t uXcpt)
{
    Assert(uXcpt <= X86_XCPT_LAST);
    vmxHCAddXcptInterceptMask(pVCpu, pVmxTransient, RT_BIT_32(uXcpt));
}


/**
 * Remove one or more exceptions from the exception bitmap and commits it to the
 * current VMCS.
 *
 * This takes care of not removing the exception intercept if a nested-guest
 * requires the exception to be intercepted.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 * @param   uXcptMask       The exception(s) to remove.
 */
static int vmxHCRemoveXcptInterceptMask(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient, uint32_t uXcptMask)
{
    PVMXVMCSINFO pVmcsInfo   = pVmxTransient->pVmcsInfo;
    uint32_t     uXcptBitmap = pVmcsInfo->u32XcptBitmap;
    if (uXcptBitmap & uXcptMask)
    {
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
        if (!pVmxTransient->fIsNestedGuest)
        { /* likely */ }
        else
            uXcptMask &= ~pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32XcptBitmap;
#endif
#ifdef HMVMX_ALWAYS_TRAP_ALL_XCPTS
        uXcptMask &= ~(  RT_BIT(X86_XCPT_BP)
                       | RT_BIT(X86_XCPT_DE)
                       | RT_BIT(X86_XCPT_NM)
                       | RT_BIT(X86_XCPT_TS)
                       | RT_BIT(X86_XCPT_UD)
                       | RT_BIT(X86_XCPT_NP)
                       | RT_BIT(X86_XCPT_SS)
                       | RT_BIT(X86_XCPT_GP)
                       | RT_BIT(X86_XCPT_PF)
                       | RT_BIT(X86_XCPT_MF));
#elif defined(HMVMX_ALWAYS_TRAP_PF)
        uXcptMask &= ~RT_BIT(X86_XCPT_PF);
#endif
        if (uXcptMask)
        {
            /* Validate we are not removing any essential exception intercepts. */
#ifndef IN_NEM_DARWIN
            Assert(pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging || !(uXcptMask & RT_BIT(X86_XCPT_PF)));
#else
            Assert(!(uXcptMask & RT_BIT(X86_XCPT_PF)));
#endif
            NOREF(pVCpu);
            Assert(!(uXcptMask & RT_BIT(X86_XCPT_DB)));
            Assert(!(uXcptMask & RT_BIT(X86_XCPT_AC)));

            /* Remove it from the exception bitmap. */
            uXcptBitmap &= ~uXcptMask;

            /* Commit and update the cache if necessary. */
            if (pVmcsInfo->u32XcptBitmap != uXcptBitmap)
            {
                int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_EXCEPTION_BITMAP, uXcptBitmap);
                AssertRC(rc);
                pVmcsInfo->u32XcptBitmap = uXcptBitmap;
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * Remove an exceptions from the exception bitmap and commits it to the current
 * VMCS.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 * @param   uXcpt           The exception to remove.
 */
static int vmxHCRemoveXcptIntercept(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient, uint8_t uXcpt)
{
    return vmxHCRemoveXcptInterceptMask(pVCpu, pVmxTransient, RT_BIT(uXcpt));
}

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX

/**
 * Loads the shadow VMCS specified by the VMCS info. object.
 *
 * @returns VBox status code.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks Can be called with interrupts disabled.
 */
static int vmxHCLoadShadowVmcs(PVMXVMCSINFO pVmcsInfo)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(pVmcsInfo->HCPhysShadowVmcs != 0 && pVmcsInfo->HCPhysShadowVmcs != NIL_RTHCPHYS);

    return VMXLoadVmcs(pVmcsInfo->HCPhysShadowVmcs);
}


/**
 * Clears the shadow VMCS specified by the VMCS info. object.
 *
 * @returns VBox status code.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks Can be called with interrupts disabled.
 */
static int vmxHCClearShadowVmcs(PVMXVMCSINFO pVmcsInfo)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(pVmcsInfo->HCPhysShadowVmcs != 0 && pVmcsInfo->HCPhysShadowVmcs != NIL_RTHCPHYS);

    int rc = VMXClearVmcs(pVmcsInfo->HCPhysShadowVmcs);
    if (RT_SUCCESS(rc))
        pVmcsInfo->fShadowVmcsState = VMX_V_VMCS_LAUNCH_STATE_CLEAR;
    return rc;
}


/**
 * Switches from and to the specified VMCSes.
 *
 * @returns VBox status code.
 * @param   pVmcsInfoFrom   The VMCS info. object we are switching from.
 * @param   pVmcsInfoTo     The VMCS info. object we are switching to.
 *
 * @remarks Called with interrupts disabled.
 */
static int vmxHCSwitchVmcs(PVMXVMCSINFO pVmcsInfoFrom, PVMXVMCSINFO pVmcsInfoTo)
{
    /*
     * Clear the VMCS we are switching out if it has not already been cleared.
     * This will sync any CPU internal data back to the VMCS.
     */
    if (pVmcsInfoFrom->fVmcsState != VMX_V_VMCS_LAUNCH_STATE_CLEAR)
    {
        int rc = hmR0VmxClearVmcs(pVmcsInfoFrom);
        if (RT_SUCCESS(rc))
        {
            /*
             * The shadow VMCS, if any, would not be active at this point since we
             * would have cleared it while importing the virtual hardware-virtualization
             * state as part the VMLAUNCH/VMRESUME VM-exit. Hence, there's no need to
             * clear the shadow VMCS here, just assert for safety.
             */
            Assert(!pVmcsInfoFrom->pvShadowVmcs || pVmcsInfoFrom->fShadowVmcsState == VMX_V_VMCS_LAUNCH_STATE_CLEAR);
        }
        else
            return rc;
    }

    /*
     * Clear the VMCS we are switching to if it has not already been cleared.
     * This will initialize the VMCS launch state to "clear" required for loading it.
     *
     * See Intel spec. 31.6 "Preparation And Launching A Virtual Machine".
     */
    if (pVmcsInfoTo->fVmcsState != VMX_V_VMCS_LAUNCH_STATE_CLEAR)
    {
        int rc = hmR0VmxClearVmcs(pVmcsInfoTo);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            return rc;
    }

    /*
     * Finally, load the VMCS we are switching to.
     */
    return hmR0VmxLoadVmcs(pVmcsInfoTo);
}


/**
 * Switches between the guest VMCS and the nested-guest VMCS as specified by the
 * caller.
 *
 * @returns VBox status code.
 * @param   pVCpu                   The cross context virtual CPU structure.
 * @param   fSwitchToNstGstVmcs     Whether to switch to the nested-guest VMCS (pass
 *                                  true) or guest VMCS (pass false).
 */
static int vmxHCSwitchToGstOrNstGstVmcs(PVMCPUCC pVCpu, bool fSwitchToNstGstVmcs)
{
    /* Ensure we have synced everything from the guest-CPU context to the VMCS before switching. */
    HMVMX_CPUMCTX_ASSERT(pVCpu, HMVMX_CPUMCTX_EXTRN_ALL);

    PVMXVMCSINFO pVmcsInfoFrom;
    PVMXVMCSINFO pVmcsInfoTo;
    if (fSwitchToNstGstVmcs)
    {
        pVmcsInfoFrom = &pVCpu->hmr0.s.vmx.VmcsInfo;
        pVmcsInfoTo   = &pVCpu->hmr0.s.vmx.VmcsInfoNstGst;
        Assert(!pVCpu->hm.s.vmx.fMergedNstGstCtls);
    }
    else
    {
        pVmcsInfoFrom = &pVCpu->hmr0.s.vmx.VmcsInfoNstGst;
        pVmcsInfoTo   = &pVCpu->hmr0.s.vmx.VmcsInfo;
    }

    /*
     * Disable interrupts to prevent being preempted while we switch the current VMCS as the
     * preemption hook code path acquires the current VMCS.
     */
    RTCCUINTREG const fEFlags = ASMIntDisableFlags();

    int rc = vmxHCSwitchVmcs(pVmcsInfoFrom, pVmcsInfoTo);
    if (RT_SUCCESS(rc))
    {
        pVCpu->hmr0.s.vmx.fSwitchedToNstGstVmcs           = fSwitchToNstGstVmcs;
        pVCpu->hm.s.vmx.fSwitchedToNstGstVmcsCopyForRing3 = fSwitchToNstGstVmcs;

        /*
         * If we are switching to a VMCS that was executed on a different host CPU or was
         * never executed before, flag that we need to export the host state before executing
         * guest/nested-guest code using hardware-assisted VMX.
         *
         * This could probably be done in a preemptible context since the preemption hook
         * will flag the necessary change in host context. However, since preemption is
         * already disabled and to avoid making assumptions about host specific code in
         * RTMpCpuId when called with preemption enabled, we'll do this while preemption is
         * disabled.
         */
        if (pVmcsInfoTo->idHostCpuState == RTMpCpuId())
        { /* likely */ }
        else
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_HOST_CONTEXT | HM_CHANGED_VMX_HOST_GUEST_SHARED_STATE);

        /*
         * We use a different VM-exit MSR-store areas for the guest and nested-guest. Hence,
         * flag that we need to update the host MSR values there. Even if we decide in the
         * future to share the VM-exit MSR-store area page between the guest and nested-guest,
         * if its content differs, we would have to update the host MSRs anyway.
         */
        pVCpu->hmr0.s.vmx.fUpdatedHostAutoMsrs = false;

#ifdef HMVMX_VERIFY_VMCS_MAGIC
        AssertRC(hmR0VmxCheckVmcsMagic(fSwitchToNstGstVmcs));
#endif
    }

    ASMSetFlags(fEFlags);
    AssertRC(rc);
    return rc;
}

#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */
#ifdef VBOX_STRICT

/**
 * Reads the VM-entry interruption-information field from the VMCS into the VMX
 * transient structure.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
DECLINLINE(void) vmxHCReadEntryIntInfoVmcs(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    int rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_ENTRY_INTERRUPTION_INFO, &pVmxTransient->uEntryIntInfo);
    AssertRC(rc);
}


/**
 * Reads the VM-entry exception error code field from the VMCS into
 * the VMX transient structure.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
DECLINLINE(void) vmxHCReadEntryXcptErrorCodeVmcs(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    int rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_ENTRY_EXCEPTION_ERRCODE, &pVmxTransient->uEntryXcptErrorCode);
    AssertRC(rc);
}


/**
 * Reads the VM-entry exception error code field from the VMCS into
 * the VMX transient structure.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
DECLINLINE(void) vmxHCReadEntryInstrLenVmcs(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    int rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_ENTRY_INSTR_LENGTH, &pVmxTransient->cbEntryInstr);
    AssertRC(rc);
}

#endif /* VBOX_STRICT */


/**
 * Reads VMCS fields into the VMXTRANSIENT structure, slow path version.
 *
 * Don't call directly unless the it's likely that some or all of the fields
 * given in @a a_fReadMask have already been read.
 *
 * @tparam  a_fReadMask     The fields to read.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
template<uint32_t const a_fReadMask>
static void vmxHCReadToTransientSlow(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    AssertCompile((a_fReadMask & ~(  HMVMX_READ_EXIT_QUALIFICATION
                                   | HMVMX_READ_EXIT_INSTR_LEN
                                   | HMVMX_READ_EXIT_INSTR_INFO
                                   | HMVMX_READ_IDT_VECTORING_INFO
                                   | HMVMX_READ_IDT_VECTORING_ERROR_CODE
                                   | HMVMX_READ_EXIT_INTERRUPTION_INFO
                                   | HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE
                                   | HMVMX_READ_GUEST_LINEAR_ADDR
                                   | HMVMX_READ_GUEST_PHYSICAL_ADDR
                                   | HMVMX_READ_GUEST_PENDING_DBG_XCPTS
                                   )) == 0);

    if ((pVmxTransient->fVmcsFieldsRead & a_fReadMask) != a_fReadMask)
    {
        uint32_t const fVmcsFieldsRead = pVmxTransient->fVmcsFieldsRead;

        if (   (a_fReadMask      & HMVMX_READ_EXIT_QUALIFICATION)
            && !(fVmcsFieldsRead & HMVMX_READ_EXIT_QUALIFICATION))
        {
            int const rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_RO_EXIT_QUALIFICATION,          &pVmxTransient->uExitQual);
            AssertRC(rc);
        }
        if (    (a_fReadMask     & HMVMX_READ_EXIT_INSTR_LEN)
            && !(fVmcsFieldsRead & HMVMX_READ_EXIT_INSTR_LEN))
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INSTR_LENGTH,         &pVmxTransient->cbExitInstr);
            AssertRC(rc);
        }
        if (   (a_fReadMask      & HMVMX_READ_EXIT_INSTR_INFO)
            && !(fVmcsFieldsRead & HMVMX_READ_EXIT_INSTR_INFO))
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INSTR_INFO,           &pVmxTransient->ExitInstrInfo.u);
            AssertRC(rc);
        }
        if (   (a_fReadMask      & HMVMX_READ_IDT_VECTORING_INFO)
            && !(fVmcsFieldsRead & HMVMX_READ_IDT_VECTORING_INFO))
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_IDT_VECTORING_INFO,        &pVmxTransient->uIdtVectoringInfo);
            AssertRC(rc);
        }
        if (   (a_fReadMask      & HMVMX_READ_IDT_VECTORING_ERROR_CODE)
            && !(fVmcsFieldsRead & HMVMX_READ_IDT_VECTORING_ERROR_CODE))
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_IDT_VECTORING_ERROR_CODE,  &pVmxTransient->uIdtVectoringErrorCode);
            AssertRC(rc);
        }
        if (   (a_fReadMask      & HMVMX_READ_EXIT_INTERRUPTION_INFO)
            && !(fVmcsFieldsRead & HMVMX_READ_EXIT_INTERRUPTION_INFO))
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INTERRUPTION_INFO,    &pVmxTransient->uExitIntInfo);
            AssertRC(rc);
        }
        if (    (a_fReadMask     & HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE)
            && !(fVmcsFieldsRead & HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE))
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INTERRUPTION_ERROR_CODE, &pVmxTransient->uExitIntErrorCode);
            AssertRC(rc);
        }
        if (   (a_fReadMask      & HMVMX_READ_GUEST_LINEAR_ADDR)
            && !(fVmcsFieldsRead & HMVMX_READ_GUEST_LINEAR_ADDR))
        {
            int const rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_RO_GUEST_LINEAR_ADDR,           &pVmxTransient->uGuestLinearAddr);
            AssertRC(rc);
        }
        if (   (a_fReadMask      & HMVMX_READ_GUEST_PHYSICAL_ADDR)
            && !(fVmcsFieldsRead & HMVMX_READ_GUEST_PHYSICAL_ADDR))
        {
            int const rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_RO_GUEST_PHYS_ADDR_FULL,      &pVmxTransient->uGuestPhysicalAddr);
            AssertRC(rc);
        }
        if (   (a_fReadMask      & HMVMX_READ_GUEST_PENDING_DBG_XCPTS)
            && !(fVmcsFieldsRead & HMVMX_READ_GUEST_PENDING_DBG_XCPTS))
        {
            int const rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_PENDING_DEBUG_XCPTS,      &pVmxTransient->uGuestPendingDbgXcpts);
            AssertRC(rc);
        }

        pVmxTransient->fVmcsFieldsRead |= a_fReadMask;
    }
}


/**
 * Reads VMCS fields into the VMXTRANSIENT structure.
 *
 * This optimizes for the case where none of @a a_fReadMask has been read yet,
 * generating an optimized read sequences w/o any conditionals between in
 * non-strict builds.
 *
 * @tparam  a_fReadMask     The fields to read.  One or more of the
 *                          HMVMX_READ_XXX fields ORed together.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
template<uint32_t const a_fReadMask>
DECLINLINE(void) vmxHCReadToTransient(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    AssertCompile((a_fReadMask & ~(  HMVMX_READ_EXIT_QUALIFICATION
                                   | HMVMX_READ_EXIT_INSTR_LEN
                                   | HMVMX_READ_EXIT_INSTR_INFO
                                   | HMVMX_READ_IDT_VECTORING_INFO
                                   | HMVMX_READ_IDT_VECTORING_ERROR_CODE
                                   | HMVMX_READ_EXIT_INTERRUPTION_INFO
                                   | HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE
                                   | HMVMX_READ_GUEST_LINEAR_ADDR
                                   | HMVMX_READ_GUEST_PHYSICAL_ADDR
                                   | HMVMX_READ_GUEST_PENDING_DBG_XCPTS
                                   )) == 0);

    if (RT_LIKELY(!(pVmxTransient->fVmcsFieldsRead & a_fReadMask)))
    {
        if (a_fReadMask & HMVMX_READ_EXIT_QUALIFICATION)
        {
            int const rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_RO_EXIT_QUALIFICATION,          &pVmxTransient->uExitQual);
            AssertRC(rc);
        }
        if (a_fReadMask & HMVMX_READ_EXIT_INSTR_LEN)
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INSTR_LENGTH,         &pVmxTransient->cbExitInstr);
            AssertRC(rc);
        }
        if (a_fReadMask & HMVMX_READ_EXIT_INSTR_INFO)
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INSTR_INFO,           &pVmxTransient->ExitInstrInfo.u);
            AssertRC(rc);
        }
        if (a_fReadMask & HMVMX_READ_IDT_VECTORING_INFO)
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_IDT_VECTORING_INFO,        &pVmxTransient->uIdtVectoringInfo);
            AssertRC(rc);
        }
        if (a_fReadMask & HMVMX_READ_IDT_VECTORING_ERROR_CODE)
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_IDT_VECTORING_ERROR_CODE,  &pVmxTransient->uIdtVectoringErrorCode);
            AssertRC(rc);
        }
        if (a_fReadMask & HMVMX_READ_EXIT_INTERRUPTION_INFO)
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INTERRUPTION_INFO,    &pVmxTransient->uExitIntInfo);
            AssertRC(rc);
        }
        if (a_fReadMask & HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE)
        {
            int const rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INTERRUPTION_ERROR_CODE, &pVmxTransient->uExitIntErrorCode);
            AssertRC(rc);
        }
        if (a_fReadMask & HMVMX_READ_GUEST_LINEAR_ADDR)
        {
            int const rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_RO_GUEST_LINEAR_ADDR,           &pVmxTransient->uGuestLinearAddr);
            AssertRC(rc);
        }
        if (a_fReadMask & HMVMX_READ_GUEST_PHYSICAL_ADDR)
        {
            int const rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_RO_GUEST_PHYS_ADDR_FULL,      &pVmxTransient->uGuestPhysicalAddr);
            AssertRC(rc);
        }
        if (a_fReadMask & HMVMX_READ_GUEST_PENDING_DBG_XCPTS)
        {
            int const rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_PENDING_DEBUG_XCPTS,      &pVmxTransient->uGuestPendingDbgXcpts);
            AssertRC(rc);
        }

        pVmxTransient->fVmcsFieldsRead |= a_fReadMask;
    }
    else
    {
        STAM_REL_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatReadToTransientFallback);
        Log11Func(("a_fReadMask=%#x fVmcsFieldsRead=%#x => %#x - Taking inefficient code path!\n",
                   a_fReadMask, pVmxTransient->fVmcsFieldsRead, a_fReadMask & pVmxTransient->fVmcsFieldsRead));
        vmxHCReadToTransientSlow<a_fReadMask>(pVCpu, pVmxTransient);
    }
}


#ifdef HMVMX_ALWAYS_SAVE_RO_GUEST_STATE
/**
 * Reads all relevant read-only VMCS fields into the VMX transient structure.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
static void vmxHCReadAllRoFieldsVmcs(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    int rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_RO_EXIT_QUALIFICATION,             &pVmxTransient->uExitQual);
    rc    |= VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INSTR_LENGTH,            &pVmxTransient->cbExitInstr);
    rc    |= VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INSTR_INFO,              &pVmxTransient->ExitInstrInfo.u);
    rc    |= VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_IDT_VECTORING_INFO,           &pVmxTransient->uIdtVectoringInfo);
    rc    |= VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_IDT_VECTORING_ERROR_CODE,     &pVmxTransient->uIdtVectoringErrorCode);
    rc    |= VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INTERRUPTION_INFO,       &pVmxTransient->uExitIntInfo);
    rc    |= VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_RO_EXIT_INTERRUPTION_ERROR_CODE, &pVmxTransient->uExitIntErrorCode);
    rc    |= VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_RO_GUEST_LINEAR_ADDR,              &pVmxTransient->uGuestLinearAddr);
    rc    |= VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_RO_GUEST_PHYS_ADDR_FULL,         &pVmxTransient->uGuestPhysicalAddr);
    AssertRC(rc);
    pVmxTransient->fVmcsFieldsRead |= HMVMX_READ_EXIT_QUALIFICATION
                                   |  HMVMX_READ_EXIT_INSTR_LEN
                                   |  HMVMX_READ_EXIT_INSTR_INFO
                                   |  HMVMX_READ_IDT_VECTORING_INFO
                                   |  HMVMX_READ_IDT_VECTORING_ERROR_CODE
                                   |  HMVMX_READ_EXIT_INTERRUPTION_INFO
                                   |  HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE
                                   |  HMVMX_READ_GUEST_LINEAR_ADDR
                                   |  HMVMX_READ_GUEST_PHYSICAL_ADDR;
}
#endif

/**
 * Verifies that our cached values of the VMCS fields are all consistent with
 * what's actually present in the VMCS.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if all our caches match their respective VMCS fields.
 * @retval  VERR_VMX_VMCS_FIELD_CACHE_INVALID if a cache field doesn't match the
 *                                            VMCS content. HMCPU error-field is
 *                                            updated, see VMX_VCI_XXX.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmcsInfo       The VMCS info. object.
 * @param   fIsNstGstVmcs   Whether this is a nested-guest VMCS.
 */
static int vmxHCCheckCachedVmcsCtls(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo, bool fIsNstGstVmcs)
{
    const char * const pcszVmcs = fIsNstGstVmcs ? "Nested-guest VMCS" : "VMCS";

#ifdef HMVMX_VERIFY_VMCS_MAGIC
    {
        uint64_t       uVmcsMagic = 0;
        uint64_t const uExpectedMagic = fIsNstGstVmcs ? HMVMX_NST_GST_VMCS_MAGIC : HMVMX_GST_VMCS_MAGIC;
        int const rcMagic = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_CTRL_EXEC_VMCS_PTR_FULL, &uVmcsMagic);
        AssertRC(rcMagic);
        AssertMsgReturnStmt(uVmcsMagic == uExpectedMagic,
                            ("%s VMCS magic mismatch: Expected=%#RX32 VMCS=%#RX32\n", pcszVmcs, uExpectedMagic, uVmcsMagic),
                            pVCpu->hm.s.u32HMError = VMX_VCI_VMCS_MAGIC_MISMATCH,
                            VERR_VMX_VMCS_FIELD_CACHE_INVALID);
    }
#endif

    uint32_t u32Val;
    int rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_ENTRY, &u32Val);
    AssertRC(rc);
    AssertMsgReturnStmt(pVmcsInfo->u32EntryCtls == u32Val,
                        ("%s entry controls mismatch: Cache=%#RX32 VMCS=%#RX32\n", pcszVmcs, pVmcsInfo->u32EntryCtls, u32Val),
                        VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_VCI_CTRL_ENTRY,
                        VERR_VMX_VMCS_FIELD_CACHE_INVALID);

    rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_EXIT, &u32Val);
    AssertRC(rc);
    AssertMsgReturnStmt(pVmcsInfo->u32ExitCtls == u32Val,
                        ("%s exit controls mismatch: Cache=%#RX32 VMCS=%#RX32\n", pcszVmcs, pVmcsInfo->u32ExitCtls, u32Val),
                        VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_VCI_CTRL_EXIT,
                        VERR_VMX_VMCS_FIELD_CACHE_INVALID);

    rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_PIN_EXEC, &u32Val);
    AssertRC(rc);
    AssertMsgReturnStmt(pVmcsInfo->u32PinCtls == u32Val,
                        ("%s pin controls mismatch: Cache=%#RX32 VMCS=%#RX32\n", pcszVmcs, pVmcsInfo->u32PinCtls, u32Val),
                        VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_VCI_CTRL_PIN_EXEC,
                        VERR_VMX_VMCS_FIELD_CACHE_INVALID);

    rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, &u32Val);
    AssertRC(rc);
    AssertMsgReturnStmt(pVmcsInfo->u32ProcCtls == u32Val,
                        ("%s proc controls mismatch: Cache=%#RX32 VMCS=%#RX32\n", pcszVmcs, pVmcsInfo->u32ProcCtls, u32Val),
                        VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_VCI_CTRL_PROC_EXEC,
                        VERR_VMX_VMCS_FIELD_CACHE_INVALID);

    if (pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_SECONDARY_CTLS)
    {
        rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC2, &u32Val);
        AssertRC(rc);
        AssertMsgReturnStmt(pVmcsInfo->u32ProcCtls2 == u32Val,
                            ("%s proc2 controls mismatch: Cache=%#RX32 VMCS=%#RX32\n", pcszVmcs, pVmcsInfo->u32ProcCtls2, u32Val),
                            VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_VCI_CTRL_PROC_EXEC2,
                            VERR_VMX_VMCS_FIELD_CACHE_INVALID);
    }

    uint64_t u64Val;
    if (pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TERTIARY_CTLS)
    {
        rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_CTRL_PROC_EXEC3_FULL, &u64Val);
        AssertRC(rc);
        AssertMsgReturnStmt(pVmcsInfo->u64ProcCtls3 == u64Val,
                            ("%s proc3 controls mismatch: Cache=%#RX32 VMCS=%#RX64\n", pcszVmcs, pVmcsInfo->u64ProcCtls3, u64Val),
                            VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_VCI_CTRL_PROC_EXEC3,
                            VERR_VMX_VMCS_FIELD_CACHE_INVALID);
    }

    rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_EXCEPTION_BITMAP, &u32Val);
    AssertRC(rc);
    AssertMsgReturnStmt(pVmcsInfo->u32XcptBitmap == u32Val,
                        ("%s exception bitmap mismatch: Cache=%#RX32 VMCS=%#RX32\n", pcszVmcs, pVmcsInfo->u32XcptBitmap, u32Val),
                        VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_VCI_CTRL_XCPT_BITMAP,
                        VERR_VMX_VMCS_FIELD_CACHE_INVALID);

    /*
     * The TSC offset will only be used when RDTSC is not intercepted.
     * Since we don't actively clear it while switching between intercepting or not,
     * the value here could be stale.
     */
    if (!(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_RDTSC_EXIT))
    {
        rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_CTRL_TSC_OFFSET_FULL, &u64Val);
        AssertRC(rc);
        AssertMsgReturnStmt(pVmcsInfo->u64TscOffset == u64Val,
                            ("%s TSC offset mismatch: Cache=%#RX64 VMCS=%#RX64\n", pcszVmcs, pVmcsInfo->u64TscOffset, u64Val),
                            VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_VCI_CTRL_TSC_OFFSET,
                            VERR_VMX_VMCS_FIELD_CACHE_INVALID);
    }

    NOREF(pcszVmcs);
    return VINF_SUCCESS;
}


/**
 * Exports the guest state with appropriate VM-entry and VM-exit controls in the
 * VMCS.
 *
 * This is typically required when the guest changes paging mode.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks Requires EFER.
 * @remarks No-long-jump zone!!!
 */
static int vmxHCExportGuestEntryExitCtls(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient)
{
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_VMX_ENTRY_EXIT_CTLS)
    {
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        PVMXVMCSINFO pVmcsInfo      = pVmxTransient->pVmcsInfo;

        /*
         * VM-entry controls.
         */
        {
            uint32_t       fVal = g_HmMsrs.u.vmx.EntryCtls.n.allowed0;    /* Bits set here must be set in the VMCS. */
            uint32_t const fZap = g_HmMsrs.u.vmx.EntryCtls.n.allowed1;    /* Bits cleared here must be cleared in the VMCS. */

            /*
             * Load the guest debug controls (DR7 and IA32_DEBUGCTL MSR) on VM-entry.
             * The first VT-x capable CPUs only supported the 1-setting of this bit.
             *
             * For nested-guests, this is a mandatory VM-entry control. It's also
             * required because we do not want to leak host bits to the nested-guest.
             */
            fVal |= VMX_ENTRY_CTLS_LOAD_DEBUG;

            /*
             * Set if the guest is in long mode. This will set/clear the EFER.LMA bit on VM-entry.
             *
             * For nested-guests, the "IA-32e mode guest" control we initialize with what is
             * required to get the nested-guest working with hardware-assisted VMX execution.
             * It depends on the nested-guest's IA32_EFER.LMA bit. Remember, a nested hypervisor
             * can skip intercepting changes to the EFER MSR. This is why it needs to be done
             * here rather than while merging the guest VMCS controls.
             */
            if (CPUMIsGuestInLongModeEx(&pVCpu->cpum.GstCtx))
            {
                Assert(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_LME);
                fVal |= VMX_ENTRY_CTLS_IA32E_MODE_GUEST;
            }
            else
                Assert(!(fVal & VMX_ENTRY_CTLS_IA32E_MODE_GUEST));

            /*
             * If the CPU supports the newer VMCS controls for managing guest/host EFER, use it.
             *
             * For nested-guests, we use the "load IA32_EFER" if the hardware supports it,
             * regardless of whether the nested-guest VMCS specifies it because we are free to
             * load whatever MSRs we require and we do not need to modify the guest visible copy
             * of the VM-entry MSR load area.
             */
            if (   g_fHmVmxSupportsVmcsEfer
#ifndef IN_NEM_DARWIN
                && hmR0VmxShouldSwapEferMsr(pVCpu, pVmxTransient)
#endif
                )
                fVal |= VMX_ENTRY_CTLS_LOAD_EFER_MSR;
            else
                Assert(!(fVal & VMX_ENTRY_CTLS_LOAD_EFER_MSR));

            /*
             * The following should -not- be set (since we're not in SMM mode):
             * - VMX_ENTRY_CTLS_ENTRY_TO_SMM
             * - VMX_ENTRY_CTLS_DEACTIVATE_DUAL_MON
             */

            /** @todo VMX_ENTRY_CTLS_LOAD_PERF_MSR,
             *        VMX_ENTRY_CTLS_LOAD_PAT_MSR. */

            if ((fVal & fZap) == fVal)
            { /* likely */ }
            else
            {
                Log4Func(("Invalid VM-entry controls combo! Cpu=%#RX32 fVal=%#RX32 fZap=%#RX32\n",
                          g_HmMsrs.u.vmx.EntryCtls.n.allowed0, fVal, fZap));
                VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_UFC_CTRL_ENTRY;
                return VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO;
            }

            /* Commit it to the VMCS. */
            if (pVmcsInfo->u32EntryCtls != fVal)
            {
                int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_ENTRY, fVal);
                AssertRC(rc);
                pVmcsInfo->u32EntryCtls = fVal;
            }
        }

        /*
         * VM-exit controls.
         */
        {
            uint32_t       fVal = g_HmMsrs.u.vmx.ExitCtls.n.allowed0;     /* Bits set here must be set in the VMCS. */
            uint32_t const fZap = g_HmMsrs.u.vmx.ExitCtls.n.allowed1;     /* Bits cleared here must be cleared in the VMCS. */

            /*
             * Save debug controls (DR7 & IA32_DEBUGCTL_MSR). The first VT-x CPUs only
             * supported the 1-setting of this bit.
             *
             * For nested-guests, we set the "save debug controls" as the converse
             * "load debug controls" is mandatory for nested-guests anyway.
             */
            fVal |= VMX_EXIT_CTLS_SAVE_DEBUG;

            /*
             * Set the host long mode active (EFER.LMA) bit (which Intel calls
             * "Host address-space size") if necessary. On VM-exit, VT-x sets both the
             * host EFER.LMA and EFER.LME bit to this value. See assertion in
             * vmxHCExportHostMsrs().
             *
             * For nested-guests, we always set this bit as we do not support 32-bit
             * hosts.
             */
            fVal |= VMX_EXIT_CTLS_HOST_ADDR_SPACE_SIZE;

#ifndef IN_NEM_DARWIN
            /*
             * If the VMCS EFER MSR fields are supported by the hardware, we use it.
             *
             * For nested-guests, we should use the "save IA32_EFER" control if we also
             * used the "load IA32_EFER" control while exporting VM-entry controls.
             */
            if (   g_fHmVmxSupportsVmcsEfer
                && hmR0VmxShouldSwapEferMsr(pVCpu, pVmxTransient))
            {
                fVal |= VMX_EXIT_CTLS_SAVE_EFER_MSR
                     |  VMX_EXIT_CTLS_LOAD_EFER_MSR;
            }
#endif

            /*
             * Enable saving of the VMX-preemption timer value on VM-exit.
             * For nested-guests, currently not exposed/used.
             */
            /** @todo r=bird: Measure performance hit because of this vs. always rewriting
             *        the timer value. */
            if (VM_IS_VMX_PREEMPT_TIMER_USED(pVM))
            {
                Assert(g_HmMsrs.u.vmx.ExitCtls.n.allowed1 & VMX_EXIT_CTLS_SAVE_PREEMPT_TIMER);
                fVal |= VMX_EXIT_CTLS_SAVE_PREEMPT_TIMER;
            }

            /* Don't acknowledge external interrupts on VM-exit. We want to let the host do that. */
            Assert(!(fVal & VMX_EXIT_CTLS_ACK_EXT_INT));

            /** @todo VMX_EXIT_CTLS_LOAD_PERF_MSR,
             *        VMX_EXIT_CTLS_SAVE_PAT_MSR,
             *        VMX_EXIT_CTLS_LOAD_PAT_MSR. */

            if ((fVal & fZap) == fVal)
            { /* likely */ }
            else
            {
                Log4Func(("Invalid VM-exit controls combo! cpu=%#RX32 fVal=%#RX32 fZap=%#RX32\n",
                          g_HmMsrs.u.vmx.ExitCtls.n.allowed0, fVal, fZap));
                VCPU_2_VMXSTATE(pVCpu).u32HMError = VMX_UFC_CTRL_EXIT;
                return VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO;
            }

            /* Commit it to the VMCS. */
            if (pVmcsInfo->u32ExitCtls != fVal)
            {
                int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_EXIT, fVal);
                AssertRC(rc);
                pVmcsInfo->u32ExitCtls = fVal;
            }
        }

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_VMX_ENTRY_EXIT_CTLS);
    }
    return VINF_SUCCESS;
}


/**
 * Sets the TPR threshold in the VMCS.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   pVmcsInfo           The VMCS info. object.
 * @param   u32TprThreshold     The TPR threshold (task-priority class only).
 */
DECLINLINE(void) vmxHCApicSetTprThreshold(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, uint32_t u32TprThreshold)
{
    Assert(!(u32TprThreshold & ~VMX_TPR_THRESHOLD_MASK));         /* Bits 31:4 MBZ. */
    Assert(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW);
    RT_NOREF(pVmcsInfo);
    int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_TPR_THRESHOLD, u32TprThreshold);
    AssertRC(rc);
}


/**
 * Exports the guest APIC TPR state into the VMCS.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks No-long-jump zone!!!
 */
static void vmxHCExportGuestApicTpr(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient)
{
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_APIC_TPR)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_APIC_TPR);

        PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
        if (!pVmxTransient->fIsNestedGuest)
        {
            if (   PDMHasApic(pVCpu->CTX_SUFF(pVM))
                && APICIsEnabled(pVCpu))
            {
                /*
                 * Setup TPR shadowing.
                 */
                if (pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW)
                {
                    bool    fPendingIntr  = false;
                    uint8_t u8Tpr         = 0;
                    uint8_t u8PendingIntr = 0;
                    int rc = APICGetTpr(pVCpu, &u8Tpr, &fPendingIntr, &u8PendingIntr);
                    AssertRC(rc);

                    /*
                     * If there are interrupts pending but masked by the TPR, instruct VT-x to
                     * cause a TPR-below-threshold VM-exit when the guest lowers its TPR below the
                     * priority of the pending interrupt so we can deliver the interrupt. If there
                     * are no interrupts pending, set threshold to 0 to not cause any
                     * TPR-below-threshold VM-exits.
                     */
                    uint32_t u32TprThreshold = 0;
                    if (fPendingIntr)
                    {
                        /* Bits 3:0 of the TPR threshold field correspond to bits 7:4 of the TPR
                           (which is the Task-Priority Class). */
                        const uint8_t u8PendingPriority = u8PendingIntr >> 4;
                        const uint8_t u8TprPriority     = u8Tpr >> 4;
                        if (u8PendingPriority <= u8TprPriority)
                            u32TprThreshold = u8PendingPriority;
                    }

                    vmxHCApicSetTprThreshold(pVCpu, pVmcsInfo, u32TprThreshold);
                }
            }
        }
        /* else: the TPR threshold has already been updated while merging the nested-guest VMCS. */
        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_APIC_TPR);
    }
}


/**
 * Gets the guest interruptibility-state and updates related internal eflags
 * inhibition state.
 *
 * @returns Guest's interruptibility-state.
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @remarks No-long-jump zone!!!
 */
static uint32_t vmxHCGetGuestIntrStateWithUpdate(PVMCPUCC pVCpu)
{
    uint32_t fIntrState;

    /*
     * Check if we should inhibit interrupt delivery due to instructions like STI and MOV SS.
     */
    if (!CPUMIsInInterruptShadowWithUpdate(&pVCpu->cpum.GstCtx))
        fIntrState = 0;
    else
    {
        /* If inhibition is active, RIP should've been imported from the VMCS already. */
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_RIP);

        if (CPUMIsInInterruptShadowAfterSs(&pVCpu->cpum.GstCtx))
            fIntrState = VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS;
        else
        {
            fIntrState = VMX_VMCS_GUEST_INT_STATE_BLOCK_STI;

            /* Block-by-STI must not be set when interrupts are disabled. */
            AssertStmt(pVCpu->cpum.GstCtx.eflags.Bits.u1IF, fIntrState = VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS);
        }
    }

    /*
     * Check if we should inhibit NMI delivery.
     */
    if (!CPUMAreInterruptsInhibitedByNmiEx(&pVCpu->cpum.GstCtx))
    { /* likely */ }
    else
        fIntrState |= VMX_VMCS_GUEST_INT_STATE_BLOCK_NMI;

    /*
     * Validate.
     */
    /* We don't support block-by-SMI yet.*/
    Assert(!(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_SMI));

    return fIntrState;
}


/**
 * Exports the exception intercepts required for guest execution in the VMCS.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks No-long-jump zone!!!
 */
static void vmxHCExportGuestXcptIntercepts(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient)
{
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_VMX_XCPT_INTERCEPTS)
    {
        /* When executing a nested-guest, we do not need to trap GIM hypercalls by intercepting #UD. */
        if (!pVmxTransient->fIsNestedGuest)
        {
            if (VCPU_2_VMXSTATE(pVCpu).fGIMTrapXcptUD)
                vmxHCAddXcptIntercept(pVCpu, pVmxTransient, X86_XCPT_UD);
            else
                vmxHCRemoveXcptIntercept(pVCpu, pVmxTransient, X86_XCPT_UD);
        }

        /* Other exception intercepts are handled elsewhere, e.g. while exporting guest CR0. */
        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_VMX_XCPT_INTERCEPTS);
    }
}


/**
 * Exports the guest's RIP into the guest-state area in the VMCS.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @remarks No-long-jump zone!!!
 */
static void vmxHCExportGuestRip(PVMCPUCC pVCpu)
{
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_RIP)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_RIP);

        int rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_RIP, pVCpu->cpum.GstCtx.rip);
        AssertRC(rc);

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_RIP);
        Log4Func(("rip=%#RX64\n", pVCpu->cpum.GstCtx.rip));
    }
}


/**
 * Exports the guest's RFLAGS into the guest-state area in the VMCS.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks No-long-jump zone!!!
 */
static void vmxHCExportGuestRflags(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient)
{
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_RFLAGS)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_RFLAGS);

        /* Intel spec. 2.3.1 "System Flags and Fields in IA-32e Mode" claims the upper 32-bits
           of RFLAGS are reserved (MBZ).  We use bits 63:24 for internal purposes, so no need
           to assert this, the CPUMX86EFLAGS/CPUMX86RFLAGS union masks these off for us.
           Use 32-bit VMWRITE. */
        uint32_t fEFlags = pVCpu->cpum.GstCtx.eflags.u;
        Assert((fEFlags & X86_EFL_RA1_MASK) == X86_EFL_RA1_MASK);
        AssertMsg(!(fEFlags & ~(X86_EFL_LIVE_MASK | X86_EFL_RA1_MASK)), ("%#x\n", fEFlags));

#ifndef IN_NEM_DARWIN
        /*
         * If we're emulating real-mode using Virtual 8086 mode, save the real-mode eflags so
         * we can restore them on VM-exit. Modify the real-mode guest's eflags so that VT-x
         * can run the real-mode guest code under Virtual 8086 mode.
         */
        PVMXVMCSINFOSHARED pVmcsInfo = pVmxTransient->pVmcsInfo->pShared;
        if (pVmcsInfo->RealMode.fRealOnV86Active)
        {
            Assert(pVCpu->CTX_SUFF(pVM)->hm.s.vmx.pRealModeTSS);
            Assert(PDMVmmDevHeapIsEnabled(pVCpu->CTX_SUFF(pVM)));
            Assert(!pVmxTransient->fIsNestedGuest);
            pVmcsInfo->RealMode.Eflags.u32 = fEFlags;        /* Save the original eflags of the real-mode guest. */
            fEFlags |= X86_EFL_VM;                           /* Set the Virtual 8086 mode bit. */
            fEFlags &= ~X86_EFL_IOPL;                        /* Change IOPL to 0, otherwise certain instructions won't fault. */
        }
#else
        RT_NOREF(pVmxTransient);
#endif

        int rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_RFLAGS, fEFlags);
        AssertRC(rc);

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_RFLAGS);
        Log4Func(("eflags=%#RX32\n", fEFlags));
    }
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/**
 * Copies the nested-guest VMCS to the shadow VMCS.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks No-long-jump zone!!!
 */
static int vmxHCCopyNstGstToShadowVmcs(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    PVMCC      const pVM         = pVCpu->CTX_SUFF(pVM);
    PCVMXVVMCS const pVmcsNstGst = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /*
     * Disable interrupts so we don't get preempted while the shadow VMCS is the
     * current VMCS, as we may try saving guest lazy MSRs.
     *
     * Strictly speaking the lazy MSRs are not in the VMCS, but I'd rather not risk
     * calling the import VMCS code which is currently performing the guest MSR reads
     * (on 64-bit hosts) and accessing the auto-load/store MSR area on 32-bit hosts
     * and the rest of the VMX leave session machinery.
     */
    RTCCUINTREG const fEFlags = ASMIntDisableFlags();

    int rc = vmxHCLoadShadowVmcs(pVmcsInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Copy all guest read/write VMCS fields.
         *
         * We don't check for VMWRITE failures here for performance reasons and
         * because they are not expected to fail, barring irrecoverable conditions
         * like hardware errors.
         */
        uint32_t const cShadowVmcsFields = pVM->hmr0.s.vmx.cShadowVmcsFields;
        for (uint32_t i = 0; i < cShadowVmcsFields; i++)
        {
            uint64_t       u64Val;
            uint32_t const uVmcsField = pVM->hmr0.s.vmx.paShadowVmcsFields[i];
            IEMReadVmxVmcsField(pVmcsNstGst, uVmcsField, &u64Val);
            VMX_VMCS_WRITE_64(pVCpu, uVmcsField, u64Val);
        }

        /*
         * If the host CPU supports writing all VMCS fields, copy the guest read-only
         * VMCS fields, so the guest can VMREAD them without causing a VM-exit.
         */
        if (g_HmMsrs.u.vmx.u64Misc & VMX_MISC_VMWRITE_ALL)
        {
            uint32_t const cShadowVmcsRoFields = pVM->hmr0.s.vmx.cShadowVmcsRoFields;
            for (uint32_t i = 0; i < cShadowVmcsRoFields; i++)
            {
                uint64_t       u64Val;
                uint32_t const uVmcsField = pVM->hmr0.s.vmx.paShadowVmcsRoFields[i];
                IEMReadVmxVmcsField(pVmcsNstGst, uVmcsField, &u64Val);
                VMX_VMCS_WRITE_64(pVCpu, uVmcsField, u64Val);
            }
        }

        rc  = vmxHCClearShadowVmcs(pVmcsInfo);
        rc |= hmR0VmxLoadVmcs(pVmcsInfo);
    }

    ASMSetFlags(fEFlags);
    return rc;
}


/**
 * Copies the shadow VMCS to the nested-guest VMCS.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks Called with interrupts disabled.
 */
static int vmxHCCopyShadowToNstGstVmcs(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    PVMCC const     pVM         = pVCpu->CTX_SUFF(pVM);
    PVMXVVMCS const pVmcsNstGst = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    int rc = vmxHCLoadShadowVmcs(pVmcsInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Copy guest read/write fields from the shadow VMCS.
         * Guest read-only fields cannot be modified, so no need to copy them.
         *
         * We don't check for VMREAD failures here for performance reasons and
         * because they are not expected to fail, barring irrecoverable conditions
         * like hardware errors.
         */
        uint32_t const cShadowVmcsFields = pVM->hmr0.s.vmx.cShadowVmcsFields;
        for (uint32_t i = 0; i < cShadowVmcsFields; i++)
        {
            uint64_t       u64Val;
            uint32_t const uVmcsField = pVM->hmr0.s.vmx.paShadowVmcsFields[i];
            VMX_VMCS_READ_64(pVCpu, uVmcsField, &u64Val);
            IEMWriteVmxVmcsField(pVmcsNstGst, uVmcsField, u64Val);
        }

        rc  = vmxHCClearShadowVmcs(pVmcsInfo);
        rc |= hmR0VmxLoadVmcs(pVmcsInfo);
    }
    return rc;
}


/**
 * Enables VMCS shadowing for the given VMCS info. object.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks No-long-jump zone!!!
 */
static void vmxHCEnableVmcsShadowing(PCVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    uint32_t uProcCtls2 = pVmcsInfo->u32ProcCtls2;
    if (!(uProcCtls2 & VMX_PROC_CTLS2_VMCS_SHADOWING))
    {
        Assert(pVmcsInfo->HCPhysShadowVmcs != 0 && pVmcsInfo->HCPhysShadowVmcs != NIL_RTHCPHYS);
        uProcCtls2 |= VMX_PROC_CTLS2_VMCS_SHADOWING;
        int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC2, uProcCtls2);                            AssertRC(rc);
        rc     = VMX_VMCS_WRITE_64(pVCpu, VMX_VMCS64_GUEST_VMCS_LINK_PTR_FULL, pVmcsInfo->HCPhysShadowVmcs);  AssertRC(rc);
        pVmcsInfo->u32ProcCtls2   = uProcCtls2;
        pVmcsInfo->u64VmcsLinkPtr = pVmcsInfo->HCPhysShadowVmcs;
        Log4Func(("Enabled\n"));
    }
}


/**
 * Disables VMCS shadowing for the given VMCS info. object.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks No-long-jump zone!!!
 */
static void vmxHCDisableVmcsShadowing(PCVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    /*
     * We want all VMREAD and VMWRITE instructions to cause VM-exits, so we clear the
     * VMCS shadowing control. However, VM-entry requires the shadow VMCS indicator bit
     * to match the VMCS shadowing control if the VMCS link pointer is not NIL_RTHCPHYS.
     * Hence, we must also reset the VMCS link pointer to ensure VM-entry does not fail.
     *
     * See Intel spec. 26.2.1.1 "VM-Execution Control Fields".
     * See Intel spec. 26.3.1.5 "Checks on Guest Non-Register State".
     */
    uint32_t uProcCtls2 = pVmcsInfo->u32ProcCtls2;
    if (uProcCtls2 & VMX_PROC_CTLS2_VMCS_SHADOWING)
    {
        uProcCtls2 &= ~VMX_PROC_CTLS2_VMCS_SHADOWING;
        int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC2, uProcCtls2);                AssertRC(rc);
        rc     = VMX_VMCS_WRITE_64(pVCpu, VMX_VMCS64_GUEST_VMCS_LINK_PTR_FULL, NIL_RTHCPHYS);     AssertRC(rc);
        pVmcsInfo->u32ProcCtls2   = uProcCtls2;
        pVmcsInfo->u64VmcsLinkPtr = NIL_RTHCPHYS;
        Log4Func(("Disabled\n"));
    }
}
#endif


/**
 * Exports the guest CR0 control register into the guest-state area in the VMCS.
 *
 * The guest FPU state is always pre-loaded hence we don't need to bother about
 * sharing FPU related CR0 bits between the guest and host.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks No-long-jump zone!!!
 */
static int vmxHCExportGuestCR0(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient)
{
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_CR0)
    {
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;

        uint64_t       fSetCr0 = g_HmMsrs.u.vmx.u64Cr0Fixed0;
        uint64_t const fZapCr0 = g_HmMsrs.u.vmx.u64Cr0Fixed1;
        if (VM_IS_VMX_UNRESTRICTED_GUEST(pVM))
            fSetCr0 &= ~(uint64_t)(X86_CR0_PE | X86_CR0_PG);
        else
            Assert((fSetCr0 & (X86_CR0_PE | X86_CR0_PG)) == (X86_CR0_PE | X86_CR0_PG));

        if (!pVmxTransient->fIsNestedGuest)
        {
            HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);
            uint64_t       u64GuestCr0  = pVCpu->cpum.GstCtx.cr0;
            uint64_t const u64ShadowCr0 = u64GuestCr0;
            Assert(!RT_HI_U32(u64GuestCr0));

            /*
             * Setup VT-x's view of the guest CR0.
             */
            uint32_t uProcCtls = pVmcsInfo->u32ProcCtls;
            if (VM_IS_VMX_NESTED_PAGING(pVM))
            {
#ifndef HMVMX_ALWAYS_INTERCEPT_CR3_ACCESS
                if (CPUMIsGuestPagingEnabled(pVCpu))
                {
                    /* The guest has paging enabled, let it access CR3 without causing a VM-exit if supported. */
                    uProcCtls &= ~(  VMX_PROC_CTLS_CR3_LOAD_EXIT
                                   | VMX_PROC_CTLS_CR3_STORE_EXIT);
                }
                else
                {
                    /* The guest doesn't have paging enabled, make CR3 access cause a VM-exit to update our shadow. */
                    uProcCtls |= VMX_PROC_CTLS_CR3_LOAD_EXIT
                              |  VMX_PROC_CTLS_CR3_STORE_EXIT;
                }

                /* If we have unrestricted guest execution, we never have to intercept CR3 reads. */
                if (VM_IS_VMX_UNRESTRICTED_GUEST(pVM))
                    uProcCtls &= ~VMX_PROC_CTLS_CR3_STORE_EXIT;
#endif
            }
            else
            {
                /* Guest CPL 0 writes to its read-only pages should cause a #PF VM-exit. */
                u64GuestCr0 |= X86_CR0_WP;
            }

            /*
             * Guest FPU bits.
             *
             * Since we pre-load the guest FPU always before VM-entry there is no need to track lazy state
             * using CR0.TS.
             *
             * Intel spec. 23.8 "Restrictions on VMX operation" mentions that CR0.NE bit must always be
             * set on the first CPUs to support VT-x and no mention of with regards to UX in VM-entry checks.
             */
            u64GuestCr0 |= X86_CR0_NE;

            /* If CR0.NE isn't set, we need to intercept #MF exceptions and report them to the guest differently. */
            bool const fInterceptMF = !(u64ShadowCr0 & X86_CR0_NE);

            /*
             * Update exception intercepts.
             */
            uint32_t uXcptBitmap = pVmcsInfo->u32XcptBitmap;
#ifndef IN_NEM_DARWIN
            if (pVmcsInfo->pShared->RealMode.fRealOnV86Active)
            {
                Assert(PDMVmmDevHeapIsEnabled(pVM));
                Assert(pVM->hm.s.vmx.pRealModeTSS);
                uXcptBitmap |= HMVMX_REAL_MODE_XCPT_MASK;
            }
            else
#endif
            {
                /* For now, cleared here as mode-switches can happen outside HM/VT-x. See @bugref{7626#c11}. */
                uXcptBitmap &= ~HMVMX_REAL_MODE_XCPT_MASK;
                if (fInterceptMF)
                    uXcptBitmap |= RT_BIT(X86_XCPT_MF);
            }

            /* Additional intercepts for debugging, define these yourself explicitly. */
#ifdef HMVMX_ALWAYS_TRAP_ALL_XCPTS
            uXcptBitmap |= 0
                        |  RT_BIT(X86_XCPT_BP)
                        |  RT_BIT(X86_XCPT_DE)
                        |  RT_BIT(X86_XCPT_NM)
                        |  RT_BIT(X86_XCPT_TS)
                        |  RT_BIT(X86_XCPT_UD)
                        |  RT_BIT(X86_XCPT_NP)
                        |  RT_BIT(X86_XCPT_SS)
                        |  RT_BIT(X86_XCPT_GP)
                        |  RT_BIT(X86_XCPT_PF)
                        |  RT_BIT(X86_XCPT_MF)
                        ;
#elif defined(HMVMX_ALWAYS_TRAP_PF)
            uXcptBitmap |= RT_BIT(X86_XCPT_PF);
#endif
            if (VCPU_2_VMXSTATE(pVCpu).fTrapXcptGpForLovelyMesaDrv)
                uXcptBitmap |= RT_BIT(X86_XCPT_GP);
            if (VCPU_2_VMXSTATE(pVCpu).fGCMTrapXcptDE)
                uXcptBitmap |= RT_BIT(X86_XCPT_DE);
            Assert(VM_IS_VMX_NESTED_PAGING(pVM) || (uXcptBitmap & RT_BIT(X86_XCPT_PF)));

            /* Apply the hardware specified CR0 fixed bits and enable caching. */
            u64GuestCr0 |= fSetCr0;
            u64GuestCr0 &= fZapCr0;
            u64GuestCr0 &= ~(uint64_t)(X86_CR0_CD | X86_CR0_NW);

            Assert(!RT_HI_U32(u64GuestCr0));
            Assert(u64GuestCr0 & X86_CR0_NE);

            /* Commit the CR0 and related fields to the guest VMCS. */
            int rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_CR0, u64GuestCr0);               AssertRC(rc);
            rc     = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_CTRL_CR0_READ_SHADOW, u64ShadowCr0);   AssertRC(rc);
            if (uProcCtls != pVmcsInfo->u32ProcCtls)
            {
                rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, uProcCtls);
                AssertRC(rc);
            }
            if (uXcptBitmap != pVmcsInfo->u32XcptBitmap)
            {
                rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_EXCEPTION_BITMAP, uXcptBitmap);
                AssertRC(rc);
            }

            /* Update our caches. */
            pVmcsInfo->u32ProcCtls   = uProcCtls;
            pVmcsInfo->u32XcptBitmap = uXcptBitmap;

            Log4Func(("cr0=%#RX64 shadow=%#RX64 set=%#RX64 zap=%#RX64\n", u64GuestCr0, u64ShadowCr0, fSetCr0, fZapCr0));
        }
        else
        {
            /*
             * With nested-guests, we may have extended the guest/host mask here since we
             * merged in the outer guest's mask. Thus, the merged mask can include more bits
             * (to read from the nested-guest CR0 read-shadow) than the nested hypervisor
             * originally supplied. We must copy those bits from the nested-guest CR0 into
             * the nested-guest CR0 read-shadow.
             */
            HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);
            uint64_t       u64GuestCr0  = pVCpu->cpum.GstCtx.cr0;
            uint64_t const u64ShadowCr0 = CPUMGetGuestVmxMaskedCr0(&pVCpu->cpum.GstCtx, pVmcsInfo->u64Cr0Mask);

            /* Apply the hardware specified CR0 fixed bits and enable caching. */
            u64GuestCr0 |= fSetCr0;
            u64GuestCr0 &= fZapCr0;
            u64GuestCr0 &= ~(uint64_t)(X86_CR0_CD | X86_CR0_NW);

            Assert(!RT_HI_U32(u64GuestCr0));
            Assert(u64GuestCr0 & X86_CR0_NE);

            /* Commit the CR0 and CR0 read-shadow to the nested-guest VMCS. */
            int rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_CR0, u64GuestCr0);               AssertRC(rc);
            rc     = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_CTRL_CR0_READ_SHADOW, u64ShadowCr0);   AssertRC(rc);

            Log4Func(("cr0=%#RX64 shadow=%#RX64 vmcs_read_shw=%#RX64 (set=%#RX64 zap=%#RX64)\n", u64GuestCr0, u64ShadowCr0,
                      pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr0ReadShadow.u, fSetCr0, fZapCr0));
        }

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_CR0);
    }

    return VINF_SUCCESS;
}


/**
 * Exports the guest control registers (CR3, CR4) into the guest-state area
 * in the VMCS.
 *
 * @returns VBox strict status code.
 * @retval  VINF_EM_RESCHEDULE_REM if we try to emulate non-paged guest code
 *          without unrestricted guest access and the VMMDev is not presently
 *          mapped (e.g. EFI32).
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks No-long-jump zone!!!
 */
static VBOXSTRICTRC vmxHCExportGuestCR3AndCR4(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient)
{
    int rc  = VINF_SUCCESS;
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Guest CR2.
     * It's always loaded in the assembler code. Nothing to do here.
     */

    /*
     * Guest CR3.
     */
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_CR3)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR3);

        if (VM_IS_VMX_NESTED_PAGING(pVM))
        {
#ifndef IN_NEM_DARWIN
            PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
            pVmcsInfo->HCPhysEPTP = PGMGetHyperCR3(pVCpu);

            /* Validate. See Intel spec. 28.2.2 "EPT Translation Mechanism" and 24.6.11 "Extended-Page-Table Pointer (EPTP)" */
            Assert(pVmcsInfo->HCPhysEPTP != NIL_RTHCPHYS);
            Assert(!(pVmcsInfo->HCPhysEPTP & UINT64_C(0xfff0000000000000)));
            Assert(!(pVmcsInfo->HCPhysEPTP & 0xfff));

            /* VMX_EPT_MEMTYPE_WB support is already checked in vmxHCSetupTaggedTlb(). */
            pVmcsInfo->HCPhysEPTP |= RT_BF_MAKE(VMX_BF_EPTP_MEMTYPE,          VMX_EPTP_MEMTYPE_WB)
                                  |  RT_BF_MAKE(VMX_BF_EPTP_PAGE_WALK_LENGTH, VMX_EPTP_PAGE_WALK_LENGTH_4);

            /* Validate. See Intel spec. 26.2.1 "Checks on VMX Controls" */
            AssertMsg(   ((pVmcsInfo->HCPhysEPTP >> 3) & 0x07) == 3      /* Bits 3:5 (EPT page walk length - 1) must be 3. */
                      && ((pVmcsInfo->HCPhysEPTP >> 7) & 0x1f) == 0,     /* Bits 7:11 MBZ. */
                         ("EPTP %#RX64\n", pVmcsInfo->HCPhysEPTP));
            AssertMsg(  !((pVmcsInfo->HCPhysEPTP >> 6) & 0x01)           /* Bit 6 (EPT accessed & dirty bit). */
                      || (g_HmMsrs.u.vmx.u64EptVpidCaps & MSR_IA32_VMX_EPT_VPID_CAP_ACCESS_DIRTY),
                         ("EPTP accessed/dirty bit not supported by CPU but set %#RX64\n", pVmcsInfo->HCPhysEPTP));

            rc = VMX_VMCS_WRITE_64(pVCpu, VMX_VMCS64_CTRL_EPTP_FULL, pVmcsInfo->HCPhysEPTP);
            AssertRC(rc);
#endif

            PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
            uint64_t  u64GuestCr3 = pCtx->cr3;
            if (   VM_IS_VMX_UNRESTRICTED_GUEST(pVM)
                || CPUMIsGuestPagingEnabledEx(pCtx))
            {
                /* If the guest is in PAE mode, pass the PDPEs to VT-x using the VMCS fields. */
                if (CPUMIsGuestInPAEModeEx(pCtx))
                {
                    rc = VMX_VMCS_WRITE_64(pVCpu, VMX_VMCS64_GUEST_PDPTE0_FULL, pCtx->aPaePdpes[0].u);     AssertRC(rc);
                    rc = VMX_VMCS_WRITE_64(pVCpu, VMX_VMCS64_GUEST_PDPTE1_FULL, pCtx->aPaePdpes[1].u);     AssertRC(rc);
                    rc = VMX_VMCS_WRITE_64(pVCpu, VMX_VMCS64_GUEST_PDPTE2_FULL, pCtx->aPaePdpes[2].u);     AssertRC(rc);
                    rc = VMX_VMCS_WRITE_64(pVCpu, VMX_VMCS64_GUEST_PDPTE3_FULL, pCtx->aPaePdpes[3].u);     AssertRC(rc);
                }

                /*
                 * The guest's view of its CR3 is unblemished with nested paging when the
                 * guest is using paging or we have unrestricted guest execution to handle
                 * the guest when it's not using paging.
                 */
            }
#ifndef IN_NEM_DARWIN
            else
            {
                /*
                 * The guest is not using paging, but the CPU (VT-x) has to. While the guest
                 * thinks it accesses physical memory directly, we use our identity-mapped
                 * page table to map guest-linear to guest-physical addresses. EPT takes care
                 * of translating it to host-physical addresses.
                 */
                RTGCPHYS GCPhys;
                Assert(pVM->hm.s.vmx.pNonPagingModeEPTPageTable);

                /* We obtain it here every time as the guest could have relocated this PCI region. */
                rc = PDMVmmDevHeapR3ToGCPhys(pVM, pVM->hm.s.vmx.pNonPagingModeEPTPageTable, &GCPhys);
                if (RT_SUCCESS(rc))
                { /* likely */ }
                else if (rc == VERR_PDM_DEV_HEAP_R3_TO_GCPHYS)
                {
                    Log4Func(("VERR_PDM_DEV_HEAP_R3_TO_GCPHYS -> VINF_EM_RESCHEDULE_REM\n"));
                    return VINF_EM_RESCHEDULE_REM;  /* We cannot execute now, switch to REM/IEM till the guest maps in VMMDev. */
                }
                else
                    AssertMsgFailedReturn(("%Rrc\n",  rc), rc);

                u64GuestCr3 = GCPhys;
            }
#endif

            Log4Func(("guest_cr3=%#RX64 (GstN)\n", u64GuestCr3));
            rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_CR3, u64GuestCr3);
            AssertRC(rc);
        }
        else
        {
            Assert(!pVmxTransient->fIsNestedGuest);
            /* Non-nested paging case, just use the hypervisor's CR3. */
            RTHCPHYS const HCPhysGuestCr3 = PGMGetHyperCR3(pVCpu);

            Log4Func(("guest_cr3=%#RX64 (HstN)\n", HCPhysGuestCr3));
            rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_CR3, HCPhysGuestCr3);
            AssertRC(rc);
        }

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_CR3);
    }

    /*
     * Guest CR4.
     * ASSUMES this is done everytime we get in from ring-3! (XCR0)
     */
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_CR4)
    {
        PCPUMCTX     pCtx      = &pVCpu->cpum.GstCtx;
        PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;

        uint64_t const fSetCr4 = g_HmMsrs.u.vmx.u64Cr4Fixed0;
        uint64_t const fZapCr4 = g_HmMsrs.u.vmx.u64Cr4Fixed1;

        /*
         * With nested-guests, we may have extended the guest/host mask here (since we
         * merged in the outer guest's mask, see hmR0VmxMergeVmcsNested). This means, the
         * mask can include more bits (to read from the nested-guest CR4 read-shadow) than
         * the nested hypervisor originally supplied. Thus, we should, in essence, copy
         * those bits from the nested-guest CR4 into the nested-guest CR4 read-shadow.
         */
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4);
        uint64_t       u64GuestCr4  = pCtx->cr4;
        uint64_t const u64ShadowCr4 = !pVmxTransient->fIsNestedGuest
                                    ? pCtx->cr4
                                    : CPUMGetGuestVmxMaskedCr4(pCtx, pVmcsInfo->u64Cr4Mask);
        Assert(!RT_HI_U32(u64GuestCr4));

#ifndef IN_NEM_DARWIN
        /*
         * Setup VT-x's view of the guest CR4.
         *
         * If we're emulating real-mode using virtual-8086 mode, we want to redirect software
         * interrupts to the 8086 program interrupt handler. Clear the VME bit (the interrupt
         * redirection bitmap is already all 0, see hmR3InitFinalizeR0())
         *
         * See Intel spec. 20.2 "Software Interrupt Handling Methods While in Virtual-8086 Mode".
         */
        if (pVmcsInfo->pShared->RealMode.fRealOnV86Active)
        {
            Assert(pVM->hm.s.vmx.pRealModeTSS);
            Assert(PDMVmmDevHeapIsEnabled(pVM));
            u64GuestCr4 &= ~(uint64_t)X86_CR4_VME;
        }
#endif

        if (VM_IS_VMX_NESTED_PAGING(pVM))
        {
            if (   !CPUMIsGuestPagingEnabledEx(pCtx)
                && !VM_IS_VMX_UNRESTRICTED_GUEST(pVM))
            {
                /* We use 4 MB pages in our identity mapping page table when the guest doesn't have paging. */
                u64GuestCr4 |= X86_CR4_PSE;
                /* Our identity mapping is a 32-bit page directory. */
                u64GuestCr4 &= ~(uint64_t)X86_CR4_PAE;
            }
            /* else use guest CR4.*/
        }
        else
        {
            Assert(!pVmxTransient->fIsNestedGuest);

            /*
             * The shadow paging modes and guest paging modes are different, the shadow is in accordance with the host
             * paging mode and thus we need to adjust VT-x's view of CR4 depending on our shadow page tables.
             */
            switch (VCPU_2_VMXSTATE(pVCpu).enmShadowMode)
            {
                case PGMMODE_REAL:              /* Real-mode. */
                case PGMMODE_PROTECTED:         /* Protected mode without paging. */
                case PGMMODE_32_BIT:            /* 32-bit paging. */
                {
                    u64GuestCr4 &= ~(uint64_t)X86_CR4_PAE;
                    break;
                }

                case PGMMODE_PAE:               /* PAE paging. */
                case PGMMODE_PAE_NX:            /* PAE paging with NX. */
                {
                    u64GuestCr4 |= X86_CR4_PAE;
                    break;
                }

                case PGMMODE_AMD64:             /* 64-bit AMD paging (long mode). */
                case PGMMODE_AMD64_NX:          /* 64-bit AMD paging (long mode) with NX enabled. */
                {
#ifdef VBOX_WITH_64_BITS_GUESTS
                    /* For our assumption in vmxHCShouldSwapEferMsr. */
                    Assert(u64GuestCr4 & X86_CR4_PAE);
                    break;
#endif
                }
                default:
                    AssertFailed();
                    return VERR_PGM_UNSUPPORTED_SHADOW_PAGING_MODE;
            }
        }

        /* Apply the hardware specified CR4 fixed bits (mainly CR4.VMXE). */
        u64GuestCr4 |= fSetCr4;
        u64GuestCr4 &= fZapCr4;

        Assert(!RT_HI_U32(u64GuestCr4));
        Assert(u64GuestCr4 & X86_CR4_VMXE);

        /* Commit the CR4 and CR4 read-shadow to the guest VMCS. */
        rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_CR4, u64GuestCr4);               AssertRC(rc);
        rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_CTRL_CR4_READ_SHADOW, u64ShadowCr4);   AssertRC(rc);

#ifndef IN_NEM_DARWIN
        /* Whether to save/load/restore XCR0 during world switch depends on CR4.OSXSAVE and host+guest XCR0. */
        bool const fLoadSaveGuestXcr0 = (pCtx->cr4 & X86_CR4_OSXSAVE) && pCtx->aXcr[0] != ASMGetXcr0();
        if (fLoadSaveGuestXcr0 != pVCpu->hmr0.s.fLoadSaveGuestXcr0)
        {
            pVCpu->hmr0.s.fLoadSaveGuestXcr0 = fLoadSaveGuestXcr0;
            hmR0VmxUpdateStartVmFunction(pVCpu);
        }
#endif

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_CR4);

        Log4Func(("cr4=%#RX64 shadow=%#RX64 (set=%#RX64 zap=%#RX64)\n", u64GuestCr4, u64ShadowCr4, fSetCr4, fZapCr4));
    }
    return rc;
}


#ifdef VBOX_STRICT
/**
 * Strict function to validate segment registers.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks Will import guest CR0 on strict builds during validation of
 *          segments.
 */
static void vmxHCValidateSegmentRegs(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    /*
     * Validate segment registers. See Intel spec. 26.3.1.2 "Checks on Guest Segment Registers".
     *
     * The reason we check for attribute value 0 in this function and not just the unusable bit is
     * because vmxHCExportGuestSegReg() only updates the VMCS' copy of the value with the
     * unusable bit and doesn't change the guest-context value.
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    vmxHCImportGuestStateEx(pVCpu, pVmcsInfo, CPUMCTX_EXTRN_CR0);
    if (   !VM_IS_VMX_UNRESTRICTED_GUEST(pVM)
        && (   !CPUMIsGuestInRealModeEx(pCtx)
            && !CPUMIsGuestInV86ModeEx(pCtx)))
    {
        /* Protected mode checks */
        /* CS */
        Assert(pCtx->cs.Attr.n.u1Present);
        Assert(!(pCtx->cs.Attr.u & 0xf00));
        Assert(!(pCtx->cs.Attr.u & 0xfffe0000));
        Assert(   (pCtx->cs.u32Limit & 0xfff) == 0xfff
               || !(pCtx->cs.Attr.n.u1Granularity));
        Assert(   !(pCtx->cs.u32Limit & 0xfff00000)
               || (pCtx->cs.Attr.n.u1Granularity));
        /* CS cannot be loaded with NULL in protected mode. */
        Assert(pCtx->cs.Attr.u && !(pCtx->cs.Attr.u & X86DESCATTR_UNUSABLE)); /** @todo is this really true even for 64-bit CS? */
        if (pCtx->cs.Attr.n.u4Type == 9 || pCtx->cs.Attr.n.u4Type == 11)
            Assert(pCtx->cs.Attr.n.u2Dpl == pCtx->ss.Attr.n.u2Dpl);
        else if (pCtx->cs.Attr.n.u4Type == 13 || pCtx->cs.Attr.n.u4Type == 15)
            Assert(pCtx->cs.Attr.n.u2Dpl <= pCtx->ss.Attr.n.u2Dpl);
        else
            AssertMsgFailed(("Invalid CS Type %#x\n", pCtx->cs.Attr.n.u2Dpl));
        /* SS */
        Assert((pCtx->ss.Sel & X86_SEL_RPL) == (pCtx->cs.Sel & X86_SEL_RPL));
        Assert(pCtx->ss.Attr.n.u2Dpl == (pCtx->ss.Sel & X86_SEL_RPL));
        if (   !(pCtx->cr0 & X86_CR0_PE)
            || pCtx->cs.Attr.n.u4Type == 3)
        {
            Assert(!pCtx->ss.Attr.n.u2Dpl);
        }
        if (pCtx->ss.Attr.u && !(pCtx->ss.Attr.u & X86DESCATTR_UNUSABLE))
        {
            Assert((pCtx->ss.Sel & X86_SEL_RPL) == (pCtx->cs.Sel & X86_SEL_RPL));
            Assert(pCtx->ss.Attr.n.u4Type == 3 || pCtx->ss.Attr.n.u4Type == 7);
            Assert(pCtx->ss.Attr.n.u1Present);
            Assert(!(pCtx->ss.Attr.u & 0xf00));
            Assert(!(pCtx->ss.Attr.u & 0xfffe0000));
            Assert(   (pCtx->ss.u32Limit & 0xfff) == 0xfff
                   || !(pCtx->ss.Attr.n.u1Granularity));
            Assert(   !(pCtx->ss.u32Limit & 0xfff00000)
                   || (pCtx->ss.Attr.n.u1Granularity));
        }
        /* DS, ES, FS, GS - only check for usable selectors, see vmxHCExportGuestSegReg(). */
        if (pCtx->ds.Attr.u && !(pCtx->ds.Attr.u & X86DESCATTR_UNUSABLE))
        {
            Assert(pCtx->ds.Attr.n.u4Type & X86_SEL_TYPE_ACCESSED);
            Assert(pCtx->ds.Attr.n.u1Present);
            Assert(pCtx->ds.Attr.n.u4Type > 11 || pCtx->ds.Attr.n.u2Dpl >= (pCtx->ds.Sel & X86_SEL_RPL));
            Assert(!(pCtx->ds.Attr.u & 0xf00));
            Assert(!(pCtx->ds.Attr.u & 0xfffe0000));
            Assert(   (pCtx->ds.u32Limit & 0xfff) == 0xfff
                   || !(pCtx->ds.Attr.n.u1Granularity));
            Assert(   !(pCtx->ds.u32Limit & 0xfff00000)
                   || (pCtx->ds.Attr.n.u1Granularity));
            Assert(   !(pCtx->ds.Attr.n.u4Type & X86_SEL_TYPE_CODE)
                   || (pCtx->ds.Attr.n.u4Type & X86_SEL_TYPE_READ));
        }
        if (pCtx->es.Attr.u && !(pCtx->es.Attr.u & X86DESCATTR_UNUSABLE))
        {
            Assert(pCtx->es.Attr.n.u4Type & X86_SEL_TYPE_ACCESSED);
            Assert(pCtx->es.Attr.n.u1Present);
            Assert(pCtx->es.Attr.n.u4Type > 11 || pCtx->es.Attr.n.u2Dpl >= (pCtx->es.Sel & X86_SEL_RPL));
            Assert(!(pCtx->es.Attr.u & 0xf00));
            Assert(!(pCtx->es.Attr.u & 0xfffe0000));
            Assert(   (pCtx->es.u32Limit & 0xfff) == 0xfff
                   || !(pCtx->es.Attr.n.u1Granularity));
            Assert(   !(pCtx->es.u32Limit & 0xfff00000)
                   || (pCtx->es.Attr.n.u1Granularity));
            Assert(   !(pCtx->es.Attr.n.u4Type & X86_SEL_TYPE_CODE)
                   || (pCtx->es.Attr.n.u4Type & X86_SEL_TYPE_READ));
        }
        if (pCtx->fs.Attr.u && !(pCtx->fs.Attr.u & X86DESCATTR_UNUSABLE))
        {
            Assert(pCtx->fs.Attr.n.u4Type & X86_SEL_TYPE_ACCESSED);
            Assert(pCtx->fs.Attr.n.u1Present);
            Assert(pCtx->fs.Attr.n.u4Type > 11 || pCtx->fs.Attr.n.u2Dpl >= (pCtx->fs.Sel & X86_SEL_RPL));
            Assert(!(pCtx->fs.Attr.u & 0xf00));
            Assert(!(pCtx->fs.Attr.u & 0xfffe0000));
            Assert(   (pCtx->fs.u32Limit & 0xfff) == 0xfff
                   || !(pCtx->fs.Attr.n.u1Granularity));
            Assert(   !(pCtx->fs.u32Limit & 0xfff00000)
                   || (pCtx->fs.Attr.n.u1Granularity));
            Assert(   !(pCtx->fs.Attr.n.u4Type & X86_SEL_TYPE_CODE)
                   || (pCtx->fs.Attr.n.u4Type & X86_SEL_TYPE_READ));
        }
        if (pCtx->gs.Attr.u && !(pCtx->gs.Attr.u & X86DESCATTR_UNUSABLE))
        {
            Assert(pCtx->gs.Attr.n.u4Type & X86_SEL_TYPE_ACCESSED);
            Assert(pCtx->gs.Attr.n.u1Present);
            Assert(pCtx->gs.Attr.n.u4Type > 11 || pCtx->gs.Attr.n.u2Dpl >= (pCtx->gs.Sel & X86_SEL_RPL));
            Assert(!(pCtx->gs.Attr.u & 0xf00));
            Assert(!(pCtx->gs.Attr.u & 0xfffe0000));
            Assert(   (pCtx->gs.u32Limit & 0xfff) == 0xfff
                   || !(pCtx->gs.Attr.n.u1Granularity));
            Assert(   !(pCtx->gs.u32Limit & 0xfff00000)
                   || (pCtx->gs.Attr.n.u1Granularity));
            Assert(   !(pCtx->gs.Attr.n.u4Type & X86_SEL_TYPE_CODE)
                   || (pCtx->gs.Attr.n.u4Type & X86_SEL_TYPE_READ));
        }
        /* 64-bit capable CPUs. */
        Assert(!RT_HI_U32(pCtx->cs.u64Base));
        Assert(!pCtx->ss.Attr.u || !RT_HI_U32(pCtx->ss.u64Base));
        Assert(!pCtx->ds.Attr.u || !RT_HI_U32(pCtx->ds.u64Base));
        Assert(!pCtx->es.Attr.u || !RT_HI_U32(pCtx->es.u64Base));
    }
    else if (   CPUMIsGuestInV86ModeEx(pCtx)
             || (   CPUMIsGuestInRealModeEx(pCtx)
                 && !VM_IS_VMX_UNRESTRICTED_GUEST(pVM)))
    {
        /* Real and v86 mode checks. */
        /* vmxHCExportGuestSegReg() writes the modified in VMCS. We want what we're feeding to VT-x. */
        uint32_t u32CSAttr, u32SSAttr, u32DSAttr, u32ESAttr, u32FSAttr, u32GSAttr;
#ifndef IN_NEM_DARWIN
        if (pVmcsInfo->pShared->RealMode.fRealOnV86Active)
        {
            u32CSAttr = 0xf3; u32SSAttr = 0xf3; u32DSAttr = 0xf3;
            u32ESAttr = 0xf3; u32FSAttr = 0xf3; u32GSAttr = 0xf3;
        }
        else
#endif
        {
            u32CSAttr = pCtx->cs.Attr.u; u32SSAttr = pCtx->ss.Attr.u; u32DSAttr = pCtx->ds.Attr.u;
            u32ESAttr = pCtx->es.Attr.u; u32FSAttr = pCtx->fs.Attr.u; u32GSAttr = pCtx->gs.Attr.u;
        }

        /* CS */
        AssertMsg((pCtx->cs.u64Base == (uint64_t)pCtx->cs.Sel << 4), ("CS base %#x %#x\n", pCtx->cs.u64Base, pCtx->cs.Sel));
        Assert(pCtx->cs.u32Limit == 0xffff);
        AssertMsg(u32CSAttr == 0xf3, ("cs=%#x %#x ", pCtx->cs.Sel, u32CSAttr));
        /* SS */
        Assert(pCtx->ss.u64Base == (uint64_t)pCtx->ss.Sel << 4);
        Assert(pCtx->ss.u32Limit == 0xffff);
        Assert(u32SSAttr == 0xf3);
        /* DS */
        Assert(pCtx->ds.u64Base == (uint64_t)pCtx->ds.Sel << 4);
        Assert(pCtx->ds.u32Limit == 0xffff);
        Assert(u32DSAttr == 0xf3);
        /* ES */
        Assert(pCtx->es.u64Base == (uint64_t)pCtx->es.Sel << 4);
        Assert(pCtx->es.u32Limit == 0xffff);
        Assert(u32ESAttr == 0xf3);
        /* FS */
        Assert(pCtx->fs.u64Base == (uint64_t)pCtx->fs.Sel << 4);
        Assert(pCtx->fs.u32Limit == 0xffff);
        Assert(u32FSAttr == 0xf3);
        /* GS */
        Assert(pCtx->gs.u64Base == (uint64_t)pCtx->gs.Sel << 4);
        Assert(pCtx->gs.u32Limit == 0xffff);
        Assert(u32GSAttr == 0xf3);
        /* 64-bit capable CPUs. */
        Assert(!RT_HI_U32(pCtx->cs.u64Base));
        Assert(!u32SSAttr || !RT_HI_U32(pCtx->ss.u64Base));
        Assert(!u32DSAttr || !RT_HI_U32(pCtx->ds.u64Base));
        Assert(!u32ESAttr || !RT_HI_U32(pCtx->es.u64Base));
    }
}
#endif /* VBOX_STRICT */


/**
 * Exports a guest segment register into the guest-state area in the VMCS.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 * @param   iSegReg     The segment register number (X86_SREG_XXX).
 * @param   pSelReg     Pointer to the segment selector.
 *
 * @remarks No-long-jump zone!!!
 */
static int vmxHCExportGuestSegReg(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo, uint32_t iSegReg, PCCPUMSELREG pSelReg)
{
    Assert(iSegReg < X86_SREG_COUNT);

    uint32_t u32Access = pSelReg->Attr.u;
#ifndef IN_NEM_DARWIN
    if (!pVmcsInfo->pShared->RealMode.fRealOnV86Active)
#endif
    {
        /*
         * The way to differentiate between whether this is really a null selector or was just
         * a selector loaded with 0 in real-mode is using the segment attributes. A selector
         * loaded in real-mode with the value 0 is valid and usable in protected-mode and we
         * should -not- mark it as an unusable segment. Both the recompiler & VT-x ensures
         * NULL selectors loaded in protected-mode have their attribute as 0.
         */
        if (u32Access)
        { }
        else
            u32Access = X86DESCATTR_UNUSABLE;
    }
#ifndef IN_NEM_DARWIN
    else
    {
        /* VT-x requires our real-using-v86 mode hack to override the segment access-right bits. */
        u32Access = 0xf3;
        Assert(pVCpu->CTX_SUFF(pVM)->hm.s.vmx.pRealModeTSS);
        Assert(PDMVmmDevHeapIsEnabled(pVCpu->CTX_SUFF(pVM)));
        RT_NOREF_PV(pVCpu);
    }
#else
    RT_NOREF(pVmcsInfo);
#endif

    /* Validate segment access rights. Refer to Intel spec. "26.3.1.2 Checks on Guest Segment Registers". */
    AssertMsg((u32Access & X86DESCATTR_UNUSABLE) || (u32Access & X86_SEL_TYPE_ACCESSED),
              ("Access bit not set for usable segment. %.2s sel=%#x attr %#x\n", "ESCSSSDSFSGS" + iSegReg * 2, pSelReg, pSelReg->Attr.u));

    /*
     * Commit it to the VMCS.
     */
    int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS16_GUEST_SEG_SEL(iSegReg),           pSelReg->Sel);      AssertRC(rc);
    rc     = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_SEG_LIMIT(iSegReg),         pSelReg->u32Limit); AssertRC(rc);
    rc     = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_SEG_BASE(iSegReg),            pSelReg->u64Base);  AssertRC(rc);
    rc     = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_SEG_ACCESS_RIGHTS(iSegReg), u32Access);         AssertRC(rc);
    return VINF_SUCCESS;
}


/**
 * Exports the guest segment registers, GDTR, IDTR, LDTR, TR into the guest-state
 * area in the VMCS.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks Will import guest CR0 on strict builds during validation of
 *          segments.
 * @remarks No-long-jump zone!!!
 */
static int vmxHCExportGuestSegRegsXdtr(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient)
{
    int                 rc              = VERR_INTERNAL_ERROR_5;
#ifndef IN_NEM_DARWIN
    PVMCC               pVM             = pVCpu->CTX_SUFF(pVM);
#endif
    PCCPUMCTX           pCtx            = &pVCpu->cpum.GstCtx;
    PVMXVMCSINFO        pVmcsInfo       = pVmxTransient->pVmcsInfo;
#ifndef IN_NEM_DARWIN
    PVMXVMCSINFOSHARED  pVmcsInfoShared = pVmcsInfo->pShared;
#endif

    /*
     * Guest Segment registers: CS, SS, DS, ES, FS, GS.
     */
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_SREG_MASK)
    {
        if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_CS)
        {
            HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CS);
#ifndef IN_NEM_DARWIN
            if (pVmcsInfoShared->RealMode.fRealOnV86Active)
                pVmcsInfoShared->RealMode.AttrCS.u = pCtx->cs.Attr.u;
#endif
            rc = vmxHCExportGuestSegReg(pVCpu, pVmcsInfo, X86_SREG_CS, &pCtx->cs);
            AssertRC(rc);
            ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_CS);
        }

        if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_SS)
        {
            HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SS);
#ifndef IN_NEM_DARWIN
            if (pVmcsInfoShared->RealMode.fRealOnV86Active)
                pVmcsInfoShared->RealMode.AttrSS.u = pCtx->ss.Attr.u;
#endif
            rc = vmxHCExportGuestSegReg(pVCpu, pVmcsInfo, X86_SREG_SS, &pCtx->ss);
            AssertRC(rc);
            ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_SS);
        }

        if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_DS)
        {
            HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_DS);
#ifndef IN_NEM_DARWIN
            if (pVmcsInfoShared->RealMode.fRealOnV86Active)
                pVmcsInfoShared->RealMode.AttrDS.u = pCtx->ds.Attr.u;
#endif
            rc = vmxHCExportGuestSegReg(pVCpu, pVmcsInfo, X86_SREG_DS, &pCtx->ds);
            AssertRC(rc);
            ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_DS);
        }

        if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_ES)
        {
            HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_ES);
#ifndef IN_NEM_DARWIN
            if (pVmcsInfoShared->RealMode.fRealOnV86Active)
                pVmcsInfoShared->RealMode.AttrES.u = pCtx->es.Attr.u;
#endif
            rc = vmxHCExportGuestSegReg(pVCpu, pVmcsInfo, X86_SREG_ES, &pCtx->es);
            AssertRC(rc);
            ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_ES);
        }

        if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_FS)
        {
            HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_FS);
#ifndef IN_NEM_DARWIN
            if (pVmcsInfoShared->RealMode.fRealOnV86Active)
                pVmcsInfoShared->RealMode.AttrFS.u = pCtx->fs.Attr.u;
#endif
            rc = vmxHCExportGuestSegReg(pVCpu, pVmcsInfo, X86_SREG_FS, &pCtx->fs);
            AssertRC(rc);
            ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_FS);
        }

        if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_GS)
        {
            HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_GS);
#ifndef IN_NEM_DARWIN
            if (pVmcsInfoShared->RealMode.fRealOnV86Active)
                pVmcsInfoShared->RealMode.AttrGS.u = pCtx->gs.Attr.u;
#endif
            rc = vmxHCExportGuestSegReg(pVCpu, pVmcsInfo, X86_SREG_GS, &pCtx->gs);
            AssertRC(rc);
            ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_GS);
        }

#ifdef VBOX_STRICT
        vmxHCValidateSegmentRegs(pVCpu, pVmcsInfo);
#endif
        Log4Func(("cs={%#04x base=%#RX64 limit=%#RX32 attr=%#RX32}\n", pCtx->cs.Sel, pCtx->cs.u64Base, pCtx->cs.u32Limit,
                  pCtx->cs.Attr.u));
    }

    /*
     * Guest TR.
     */
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_TR)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_TR);

        /*
         * Real-mode emulation using virtual-8086 mode with CR4.VME. Interrupt redirection is
         * achieved using the interrupt redirection bitmap (all bits cleared to let the guest
         * handle INT-n's) in the TSS. See hmR3InitFinalizeR0() to see how pRealModeTSS is setup.
         */
        uint16_t u16Sel;
        uint32_t u32Limit;
        uint64_t u64Base;
        uint32_t u32AccessRights;
#ifndef IN_NEM_DARWIN
        if (!pVmcsInfoShared->RealMode.fRealOnV86Active)
#endif
        {
            u16Sel          = pCtx->tr.Sel;
            u32Limit        = pCtx->tr.u32Limit;
            u64Base         = pCtx->tr.u64Base;
            u32AccessRights = pCtx->tr.Attr.u;
        }
#ifndef IN_NEM_DARWIN
        else
        {
            Assert(!pVmxTransient->fIsNestedGuest);
            Assert(pVM->hm.s.vmx.pRealModeTSS);
            Assert(PDMVmmDevHeapIsEnabled(pVM));    /* Guaranteed by HMCanExecuteGuest() -XXX- what about inner loop changes? */

            /* We obtain it here every time as PCI regions could be reconfigured in the guest, changing the VMMDev base. */
            RTGCPHYS GCPhys;
            rc = PDMVmmDevHeapR3ToGCPhys(pVM, pVM->hm.s.vmx.pRealModeTSS, &GCPhys);
            AssertRCReturn(rc, rc);

            X86DESCATTR DescAttr;
            DescAttr.u           = 0;
            DescAttr.n.u1Present = 1;
            DescAttr.n.u4Type    = X86_SEL_TYPE_SYS_386_TSS_BUSY;

            u16Sel          = 0;
            u32Limit        = HM_VTX_TSS_SIZE;
            u64Base         = GCPhys;
            u32AccessRights = DescAttr.u;
        }
#endif

        /* Validate. */
        Assert(!(u16Sel & RT_BIT(2)));
        AssertMsg(   (u32AccessRights & 0xf) == X86_SEL_TYPE_SYS_386_TSS_BUSY
                  || (u32AccessRights & 0xf) == X86_SEL_TYPE_SYS_286_TSS_BUSY, ("TSS is not busy!? %#x\n", u32AccessRights));
        AssertMsg(!(u32AccessRights & X86DESCATTR_UNUSABLE), ("TR unusable bit is not clear!? %#x\n", u32AccessRights));
        Assert(!(u32AccessRights & RT_BIT(4)));                 /* System MBZ.*/
        Assert(u32AccessRights & RT_BIT(7));                    /* Present MB1.*/
        Assert(!(u32AccessRights & 0xf00));                     /* 11:8 MBZ. */
        Assert(!(u32AccessRights & 0xfffe0000));                /* 31:17 MBZ. */
        Assert(   (u32Limit & 0xfff) == 0xfff
               || !(u32AccessRights & RT_BIT(15)));             /* Granularity MBZ. */
        Assert(   !(pCtx->tr.u32Limit & 0xfff00000)
               || (u32AccessRights & RT_BIT(15)));              /* Granularity MB1. */

        rc = VMX_VMCS_WRITE_16(pVCpu, VMX_VMCS16_GUEST_TR_SEL,           u16Sel);             AssertRC(rc);
        rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_TR_LIMIT,         u32Limit);           AssertRC(rc);
        rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_TR_ACCESS_RIGHTS, u32AccessRights);    AssertRC(rc);
        rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_TR_BASE,            u64Base);            AssertRC(rc);

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_TR);
        Log4Func(("tr base=%#RX64 limit=%#RX32\n", pCtx->tr.u64Base, pCtx->tr.u32Limit));
    }

    /*
     * Guest GDTR.
     */
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_GDTR)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_GDTR);

        rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_GDTR_LIMIT, pCtx->gdtr.cbGdt);     AssertRC(rc);
        rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_GDTR_BASE,  pCtx->gdtr.pGdt);        AssertRC(rc);

        /* Validate. */
        Assert(!(pCtx->gdtr.cbGdt & 0xffff0000));          /* Bits 31:16 MBZ. */

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_GDTR);
        Log4Func(("gdtr base=%#RX64 limit=%#RX32\n", pCtx->gdtr.pGdt, pCtx->gdtr.cbGdt));
    }

    /*
     * Guest LDTR.
     */
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_LDTR)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_LDTR);

        /* The unusable bit is specific to VT-x, if it's a null selector mark it as an unusable segment. */
        uint32_t u32Access;
        if (   !pVmxTransient->fIsNestedGuest
            && !pCtx->ldtr.Attr.u)
            u32Access = X86DESCATTR_UNUSABLE;
        else
            u32Access = pCtx->ldtr.Attr.u;

        rc = VMX_VMCS_WRITE_16(pVCpu, VMX_VMCS16_GUEST_LDTR_SEL,           pCtx->ldtr.Sel);       AssertRC(rc);
        rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_LDTR_LIMIT,         pCtx->ldtr.u32Limit);  AssertRC(rc);
        rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_LDTR_ACCESS_RIGHTS, u32Access);            AssertRC(rc);
        rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_LDTR_BASE,            pCtx->ldtr.u64Base);   AssertRC(rc);

        /* Validate. */
        if (!(u32Access & X86DESCATTR_UNUSABLE))
        {
            Assert(!(pCtx->ldtr.Sel & RT_BIT(2)));              /* TI MBZ. */
            Assert(pCtx->ldtr.Attr.n.u4Type == 2);              /* Type MB2 (LDT). */
            Assert(!pCtx->ldtr.Attr.n.u1DescType);              /* System MBZ. */
            Assert(pCtx->ldtr.Attr.n.u1Present == 1);           /* Present MB1. */
            Assert(!pCtx->ldtr.Attr.n.u4LimitHigh);             /* 11:8 MBZ. */
            Assert(!(pCtx->ldtr.Attr.u & 0xfffe0000));          /* 31:17 MBZ. */
            Assert(   (pCtx->ldtr.u32Limit & 0xfff) == 0xfff
                   || !pCtx->ldtr.Attr.n.u1Granularity);        /* Granularity MBZ. */
            Assert(   !(pCtx->ldtr.u32Limit & 0xfff00000)
                   || pCtx->ldtr.Attr.n.u1Granularity);         /* Granularity MB1. */
        }

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_LDTR);
        Log4Func(("ldtr base=%#RX64 limit=%#RX32\n", pCtx->ldtr.u64Base, pCtx->ldtr.u32Limit));
    }

    /*
     * Guest IDTR.
     */
    if (ASMAtomicUoReadU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged) & HM_CHANGED_GUEST_IDTR)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_IDTR);

        rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_IDTR_LIMIT, pCtx->idtr.cbIdt);     AssertRC(rc);
        rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_IDTR_BASE,  pCtx->idtr.pIdt);        AssertRC(rc);

        /* Validate. */
        Assert(!(pCtx->idtr.cbIdt & 0xffff0000));          /* Bits 31:16 MBZ. */

        ASMAtomicUoAndU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, ~HM_CHANGED_GUEST_IDTR);
        Log4Func(("idtr base=%#RX64 limit=%#RX32\n", pCtx->idtr.pIdt, pCtx->idtr.cbIdt));
    }

    return VINF_SUCCESS;
}


/**
 * Gets the IEM exception flags for the specified vector and IDT vectoring /
 * VM-exit interruption info type.
 *
 * @returns The IEM exception flags.
 * @param   uVector         The event vector.
 * @param   uVmxEventType   The VMX event type.
 *
 * @remarks This function currently only constructs flags required for
 *          IEMEvaluateRecursiveXcpt and not the complete flags (e.g, error-code
 *          and CR2 aspects of an exception are not included).
 */
static uint32_t vmxHCGetIemXcptFlags(uint8_t uVector, uint32_t uVmxEventType)
{
    uint32_t fIemXcptFlags;
    switch (uVmxEventType)
    {
        case VMX_IDT_VECTORING_INFO_TYPE_HW_XCPT:
        case VMX_IDT_VECTORING_INFO_TYPE_NMI:
            fIemXcptFlags = IEM_XCPT_FLAGS_T_CPU_XCPT;
            break;

        case VMX_IDT_VECTORING_INFO_TYPE_EXT_INT:
            fIemXcptFlags = IEM_XCPT_FLAGS_T_EXT_INT;
            break;

        case VMX_IDT_VECTORING_INFO_TYPE_PRIV_SW_XCPT:
            fIemXcptFlags = IEM_XCPT_FLAGS_T_SOFT_INT | IEM_XCPT_FLAGS_ICEBP_INSTR;
            break;

        case VMX_IDT_VECTORING_INFO_TYPE_SW_XCPT:
        {
            fIemXcptFlags = IEM_XCPT_FLAGS_T_SOFT_INT;
            if (uVector == X86_XCPT_BP)
                fIemXcptFlags |= IEM_XCPT_FLAGS_BP_INSTR;
            else if (uVector == X86_XCPT_OF)
                fIemXcptFlags |= IEM_XCPT_FLAGS_OF_INSTR;
            else
            {
                fIemXcptFlags = 0;
                AssertMsgFailed(("Unexpected vector for software exception. uVector=%#x", uVector));
            }
            break;
        }

        case VMX_IDT_VECTORING_INFO_TYPE_SW_INT:
            fIemXcptFlags = IEM_XCPT_FLAGS_T_SOFT_INT;
            break;

        default:
            fIemXcptFlags = 0;
            AssertMsgFailed(("Unexpected vector type! uVmxEventType=%#x uVector=%#x", uVmxEventType, uVector));
            break;
    }
    return fIemXcptFlags;
}


/**
 * Sets an event as a pending event to be injected into the guest.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   u32IntInfo          The VM-entry interruption-information field.
 * @param   cbInstr             The VM-entry instruction length in bytes (for
 *                              software interrupts, exceptions and privileged
 *                              software exceptions).
 * @param   u32ErrCode          The VM-entry exception error code.
 * @param   GCPtrFaultAddress   The fault-address (CR2) in case it's a
 *                              page-fault.
 */
DECLINLINE(void) vmxHCSetPendingEvent(PVMCPUCC pVCpu, uint32_t u32IntInfo, uint32_t cbInstr, uint32_t u32ErrCode,
                                      RTGCUINTPTR GCPtrFaultAddress)
{
    Assert(!VCPU_2_VMXSTATE(pVCpu).Event.fPending);
    VCPU_2_VMXSTATE(pVCpu).Event.fPending          = true;
    VCPU_2_VMXSTATE(pVCpu).Event.u64IntInfo        = u32IntInfo;
    VCPU_2_VMXSTATE(pVCpu).Event.u32ErrCode        = u32ErrCode;
    VCPU_2_VMXSTATE(pVCpu).Event.cbInstr           = cbInstr;
    VCPU_2_VMXSTATE(pVCpu).Event.GCPtrFaultAddress = GCPtrFaultAddress;
}


/**
 * Sets an external interrupt as pending-for-injection into the VM.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   u8Interrupt     The external interrupt vector.
 */
DECLINLINE(void) vmxHCSetPendingExtInt(PVMCPUCC pVCpu, uint8_t u8Interrupt)
{
    uint32_t const u32IntInfo = RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VECTOR,          u8Interrupt)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,           VMX_ENTRY_INT_INFO_TYPE_EXT_INT)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID, 0)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,          1);
    vmxHCSetPendingEvent(pVCpu, u32IntInfo, 0 /* cbInstr */, 0 /* u32ErrCode */, 0 /* GCPtrFaultAddress */);
    Log4Func(("External interrupt (%#x) pending injection\n", u8Interrupt));
}


/**
 * Sets an NMI (\#NMI) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECLINLINE(void) vmxHCSetPendingXcptNmi(PVMCPUCC pVCpu)
{
    uint32_t const u32IntInfo = RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR,         X86_XCPT_NMI)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,           VMX_ENTRY_INT_INFO_TYPE_NMI)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID, 0)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,          1);
    vmxHCSetPendingEvent(pVCpu, u32IntInfo, 0 /* cbInstr */, 0 /* u32ErrCode */, 0 /* GCPtrFaultAddress */);
    Log4Func(("NMI pending injection\n"));
}


/**
 * Sets a double-fault (\#DF) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECLINLINE(void) vmxHCSetPendingXcptDF(PVMCPUCC pVCpu)
{
    uint32_t const u32IntInfo = RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR,         X86_XCPT_DF)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,           VMX_EXIT_INT_INFO_TYPE_HW_XCPT)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID, 1)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,          1);
    vmxHCSetPendingEvent(pVCpu, u32IntInfo, 0 /* cbInstr */, 0 /* u32ErrCode */, 0 /* GCPtrFaultAddress */);
}


/**
 * Sets an invalid-opcode (\#UD) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECLINLINE(void) vmxHCSetPendingXcptUD(PVMCPUCC pVCpu)
{
    uint32_t const u32IntInfo = RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR,         X86_XCPT_UD)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,           VMX_EXIT_INT_INFO_TYPE_HW_XCPT)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID, 0)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,          1);
    vmxHCSetPendingEvent(pVCpu, u32IntInfo, 0 /* cbInstr */, 0 /* u32ErrCode */, 0 /* GCPtrFaultAddress */);
}


/**
 * Sets a debug (\#DB) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECLINLINE(void) vmxHCSetPendingXcptDB(PVMCPUCC pVCpu)
{
    uint32_t const u32IntInfo = RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR,         X86_XCPT_DB)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,           VMX_EXIT_INT_INFO_TYPE_HW_XCPT)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID, 0)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,          1);
    vmxHCSetPendingEvent(pVCpu, u32IntInfo, 0 /* cbInstr */, 0 /* u32ErrCode */, 0 /* GCPtrFaultAddress */);
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/**
 * Sets a general-protection (\#GP) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   u32ErrCode  The error code for the general-protection exception.
 */
DECLINLINE(void) vmxHCSetPendingXcptGP(PVMCPUCC pVCpu, uint32_t u32ErrCode)
{
    uint32_t const u32IntInfo = RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR,         X86_XCPT_GP)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,           VMX_EXIT_INT_INFO_TYPE_HW_XCPT)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID, 1)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,          1);
    vmxHCSetPendingEvent(pVCpu, u32IntInfo, 0 /* cbInstr */, u32ErrCode, 0 /* GCPtrFaultAddress */);
}


/**
 * Sets a stack (\#SS) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   u32ErrCode  The error code for the stack exception.
 */
DECLINLINE(void) vmxHCSetPendingXcptSS(PVMCPUCC pVCpu, uint32_t u32ErrCode)
{
    uint32_t const u32IntInfo = RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR,         X86_XCPT_SS)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,           VMX_EXIT_INT_INFO_TYPE_HW_XCPT)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID, 1)
                              | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,          1);
    vmxHCSetPendingEvent(pVCpu, u32IntInfo, 0 /* cbInstr */, u32ErrCode, 0 /* GCPtrFaultAddress */);
}
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */


/**
 * Fixes up attributes for the specified segment register.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pSelReg     The segment register that needs fixing.
 * @param   pszRegName  The register name (for logging and assertions).
 */
static void vmxHCFixUnusableSegRegAttr(PVMCPUCC pVCpu, PCPUMSELREG pSelReg, const char *pszRegName)
{
    Assert(pSelReg->Attr.u & X86DESCATTR_UNUSABLE);

    /*
     * If VT-x marks the segment as unusable, most other bits remain undefined:
     *   - For CS the L, D and G bits have meaning.
     *   - For SS the DPL has meaning (it -is- the CPL for Intel and VBox).
     *   - For the remaining data segments no bits are defined.
     *
     * The present bit and the unusable bit has been observed to be set at the
     * same time (the selector was supposed to be invalid as we started executing
     * a V8086 interrupt in ring-0).
     *
     * What should be important for the rest of the VBox code, is that the P bit is
     * cleared.  Some of the other VBox code recognizes the unusable bit, but
     * AMD-V certainly don't, and REM doesn't really either.  So, to be on the
     * safe side here, we'll strip off P and other bits we don't care about.  If
     * any code breaks because Attr.u != 0 when Sel < 4, it should be fixed.
     *
     * See Intel spec. 27.3.2 "Saving Segment Registers and Descriptor-Table Registers".
     */
#ifdef VBOX_STRICT
    uint32_t const uAttr = pSelReg->Attr.u;
#endif

    /* Masking off: X86DESCATTR_P, X86DESCATTR_LIMIT_HIGH, and X86DESCATTR_AVL. The latter two are really irrelevant. */
    pSelReg->Attr.u &= X86DESCATTR_UNUSABLE | X86DESCATTR_L    | X86DESCATTR_D  | X86DESCATTR_G
                     | X86DESCATTR_DPL      | X86DESCATTR_TYPE | X86DESCATTR_DT;

#ifdef VBOX_STRICT
# ifndef IN_NEM_DARWIN
    VMMRZCallRing3Disable(pVCpu);
# endif
    Log4Func(("Unusable %s: sel=%#x attr=%#x -> %#x\n", pszRegName, pSelReg->Sel, uAttr, pSelReg->Attr.u));
# ifdef DEBUG_bird
    AssertMsg((uAttr & ~X86DESCATTR_P) == pSelReg->Attr.u,
              ("%s: %#x != %#x (sel=%#x base=%#llx limit=%#x)\n",
               pszRegName, uAttr, pSelReg->Attr.u, pSelReg->Sel, pSelReg->u64Base, pSelReg->u32Limit));
# endif
# ifndef IN_NEM_DARWIN
    VMMRZCallRing3Enable(pVCpu);
# endif
    NOREF(uAttr);
#endif
    RT_NOREF2(pVCpu, pszRegName);
}


/**
 * Imports a guest segment register from the current VMCS into the guest-CPU
 * context.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @tparam  a_iSegReg   The segment register number (X86_SREG_XXX).
 *
 * @remarks Called with interrupts and/or preemption disabled.
 */
template<uint32_t const a_iSegReg>
DECLINLINE(void) vmxHCImportGuestSegReg(PVMCPUCC pVCpu)
{
    AssertCompile(a_iSegReg < X86_SREG_COUNT);
    /* Check that the macros we depend upon here and in the export parenter function works: */
#define MY_SEG_VMCS_FIELD(a_FieldPrefix, a_FieldSuff) \
        (  a_iSegReg == X86_SREG_ES ? a_FieldPrefix ## ES ## a_FieldSuff \
         : a_iSegReg == X86_SREG_CS ? a_FieldPrefix ## CS ## a_FieldSuff \
         : a_iSegReg == X86_SREG_SS ? a_FieldPrefix ## SS ## a_FieldSuff \
         : a_iSegReg == X86_SREG_DS ? a_FieldPrefix ## DS ## a_FieldSuff \
         : a_iSegReg == X86_SREG_FS ? a_FieldPrefix ## FS ## a_FieldSuff \
         : a_iSegReg == X86_SREG_GS ? a_FieldPrefix ## GS ## a_FieldSuff : 0)
    AssertCompile(VMX_VMCS_GUEST_SEG_BASE(a_iSegReg)             == MY_SEG_VMCS_FIELD(VMX_VMCS_GUEST_,_BASE));
    AssertCompile(VMX_VMCS16_GUEST_SEG_SEL(a_iSegReg)            == MY_SEG_VMCS_FIELD(VMX_VMCS16_GUEST_,_SEL));
    AssertCompile(VMX_VMCS32_GUEST_SEG_LIMIT(a_iSegReg)          == MY_SEG_VMCS_FIELD(VMX_VMCS32_GUEST_,_LIMIT));
    AssertCompile(VMX_VMCS32_GUEST_SEG_ACCESS_RIGHTS(a_iSegReg)  == MY_SEG_VMCS_FIELD(VMX_VMCS32_GUEST_,_ACCESS_RIGHTS));

    PCPUMSELREG pSelReg = &pVCpu->cpum.GstCtx.aSRegs[a_iSegReg];

    uint16_t u16Sel;
    int rc = VMX_VMCS_READ_16(pVCpu, VMX_VMCS16_GUEST_SEG_SEL(a_iSegReg), &u16Sel);   AssertRC(rc);
    pSelReg->Sel      = u16Sel;
    pSelReg->ValidSel = u16Sel;

    rc     = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_SEG_LIMIT(a_iSegReg), &pSelReg->u32Limit); AssertRC(rc);
    rc     = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_SEG_BASE(a_iSegReg), &pSelReg->u64Base);     AssertRC(rc);

    uint32_t u32Attr;
    rc     = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_SEG_ACCESS_RIGHTS(a_iSegReg), &u32Attr);   AssertRC(rc);
    pSelReg->Attr.u   = u32Attr;
    if (u32Attr & X86DESCATTR_UNUSABLE)
        vmxHCFixUnusableSegRegAttr(pVCpu, pSelReg, "ES\0CS\0SS\0DS\0FS\0GS" + a_iSegReg * 3);

    pSelReg->fFlags   = CPUMSELREG_FLAGS_VALID;
}


/**
 * Imports the guest LDTR from the VMCS into the guest-CPU context.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @remarks Called with interrupts and/or preemption disabled.
 */
DECL_FORCE_INLINE(void) vmxHCImportGuestLdtr(PVMCPUCC pVCpu)
{
    uint16_t u16Sel;
    uint64_t u64Base;
    uint32_t u32Limit, u32Attr;
    int rc = VMX_VMCS_READ_16(pVCpu, VMX_VMCS16_GUEST_LDTR_SEL,           &u16Sel);       AssertRC(rc);
    rc     = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_LDTR_LIMIT,         &u32Limit);     AssertRC(rc);
    rc     = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_LDTR_ACCESS_RIGHTS, &u32Attr);      AssertRC(rc);
    rc     = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_LDTR_BASE,            &u64Base);      AssertRC(rc);

    pVCpu->cpum.GstCtx.ldtr.Sel      = u16Sel;
    pVCpu->cpum.GstCtx.ldtr.ValidSel = u16Sel;
    pVCpu->cpum.GstCtx.ldtr.fFlags   = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.ldtr.u32Limit = u32Limit;
    pVCpu->cpum.GstCtx.ldtr.u64Base  = u64Base;
    pVCpu->cpum.GstCtx.ldtr.Attr.u   = u32Attr;
    if (u32Attr & X86DESCATTR_UNUSABLE)
        vmxHCFixUnusableSegRegAttr(pVCpu, &pVCpu->cpum.GstCtx.ldtr, "LDTR");
}


/**
 * Imports the guest TR from the VMCS into the guest-CPU context.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @remarks Called with interrupts and/or preemption disabled.
 */
DECL_FORCE_INLINE(void) vmxHCImportGuestTr(PVMCPUCC pVCpu)
{
    uint16_t u16Sel;
    uint64_t u64Base;
    uint32_t u32Limit, u32Attr;
    int rc = VMX_VMCS_READ_16(pVCpu, VMX_VMCS16_GUEST_TR_SEL,           &u16Sel);     AssertRC(rc);
    rc     = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_TR_LIMIT,         &u32Limit);   AssertRC(rc);
    rc     = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_TR_ACCESS_RIGHTS, &u32Attr);    AssertRC(rc);
    rc     = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_TR_BASE,            &u64Base);    AssertRC(rc);

    pVCpu->cpum.GstCtx.tr.Sel      = u16Sel;
    pVCpu->cpum.GstCtx.tr.ValidSel = u16Sel;
    pVCpu->cpum.GstCtx.tr.fFlags   = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.tr.u32Limit = u32Limit;
    pVCpu->cpum.GstCtx.tr.u64Base  = u64Base;
    pVCpu->cpum.GstCtx.tr.Attr.u   = u32Attr;
    /* TR is the only selector that can never be unusable. */
    Assert(!(u32Attr & X86DESCATTR_UNUSABLE));
}


/**
 * Core: Imports the guest RIP from the VMCS into the guest-CPU context.
 *
 * @returns The RIP value.
 * @param   pVCpu               The cross context virtual CPU structure.
 *
 * @remarks Called with interrupts and/or preemption disabled, should not assert!
 * @remarks Do -not- call this function directly!
 */
DECL_FORCE_INLINE(uint64_t) vmxHCImportGuestCoreRip(PVMCPUCC pVCpu)
{
    uint64_t u64Val;
    int const rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_RIP, &u64Val);
    AssertRC(rc);

    pVCpu->cpum.GstCtx.rip = u64Val;

    return u64Val;
}


/**
 * Imports the guest RIP from the VMCS into the guest-CPU context.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @remarks Called with interrupts and/or preemption disabled, should not assert!
 * @remarks Do -not- call this function directly, use vmxHCImportGuestState()
 *          instead!!!
 */
DECL_FORCE_INLINE(void) vmxHCImportGuestRip(PVMCPUCC pVCpu)
{
    if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_RIP)
    {
        EMHistoryUpdatePC(pVCpu, vmxHCImportGuestCoreRip(pVCpu), false);
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_RIP;
    }
}


/**
 * Core: Imports the guest RFLAGS from the VMCS into the guest-CPU context.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks Called with interrupts and/or preemption disabled, should not assert!
 * @remarks Do -not- call this function directly!
 */
DECL_FORCE_INLINE(void) vmxHCImportGuestCoreRFlags(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo)
{
    uint64_t fRFlags;
    int const rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_RFLAGS, &fRFlags);
    AssertRC(rc);

    Assert((fRFlags & X86_EFL_RA1_MASK) == X86_EFL_RA1_MASK);
    Assert((fRFlags & ~(uint64_t)(X86_EFL_1 | X86_EFL_LIVE_MASK)) == 0);

    pVCpu->cpum.GstCtx.rflags.u = fRFlags;
#ifndef IN_NEM_DARWIN
    PCVMXVMCSINFOSHARED pVmcsInfoShared = pVmcsInfo->pShared;
    if (!pVmcsInfoShared->RealMode.fRealOnV86Active)
    { /* mostly likely */ }
    else
    {
        pVCpu->cpum.GstCtx.eflags.Bits.u1VM   = 0;
        pVCpu->cpum.GstCtx.eflags.Bits.u2IOPL = pVmcsInfoShared->RealMode.Eflags.Bits.u2IOPL;
    }
#else
    RT_NOREF(pVmcsInfo);
#endif
}


/**
 * Imports the guest RFLAGS from the VMCS into the guest-CPU context.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks Called with interrupts and/or preemption disabled, should not assert!
 * @remarks Do -not- call this function directly, use vmxHCImportGuestState()
 *          instead!!!
 */
DECL_FORCE_INLINE(void) vmxHCImportGuestRFlags(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo)
{
    if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_RFLAGS)
    {
        vmxHCImportGuestCoreRFlags(pVCpu, pVmcsInfo);
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_RFLAGS;
    }
}


#ifndef IN_NEM_DARWIN
/**
 * Imports the guest TSX AUX and certain other MSRs from the VMCS into the guest-CPU
 * context.
 *
 * The other MSRs are in the VM-exit MSR-store.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 * @param   fEFlags     Saved EFLAGS for restoring the interrupt flag (in case of
 *                      unexpected errors). Ignored in NEM/darwin context.
 */
DECL_FORCE_INLINE(int) vmxHCImportGuestTscAuxAndOtherMsrs(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo, uint32_t fEFlags)
{
    PVMXVMCSINFOSHARED pVmcsInfoShared = pVmcsInfo->pShared;
    PCVMXAUTOMSR       pMsrs           = (PCVMXAUTOMSR)pVmcsInfo->pvGuestMsrStore;
    uint32_t const     cMsrs           = pVmcsInfo->cExitMsrStore;
    Assert(pMsrs);
    Assert(cMsrs <= VMX_MISC_MAX_MSRS(g_HmMsrs.u.vmx.u64Misc));
    Assert(sizeof(*pMsrs) * cMsrs <= X86_PAGE_4K_SIZE);
    for (uint32_t i = 0; i < cMsrs; i++)
    {
        uint32_t const idMsr = pMsrs[i].u32Msr;
        switch (idMsr)
        {
            case MSR_K8_TSC_AUX:        CPUMSetGuestTscAux(pVCpu, pMsrs[i].u64Value);     break;
            case MSR_IA32_SPEC_CTRL:    CPUMSetGuestSpecCtrl(pVCpu, pMsrs[i].u64Value);   break;
            case MSR_K6_EFER:           /* Can't be changed without causing a VM-exit */  break;
            default:
            {
                uint32_t idxLbrMsr;
                PVMCC const pVM = pVCpu->CTX_SUFF(pVM);
                if (VM_IS_VMX_LBR(pVM))
                {
                    if (hmR0VmxIsLbrBranchFromMsr(pVM, idMsr, &idxLbrMsr))
                    {
                        Assert(idxLbrMsr < RT_ELEMENTS(pVmcsInfoShared->au64LbrFromIpMsr));
                        pVmcsInfoShared->au64LbrFromIpMsr[idxLbrMsr] = pMsrs[i].u64Value;
                        break;
                    }
                    if (hmR0VmxIsLbrBranchToMsr(pVM, idMsr, &idxLbrMsr))
                    {
                        Assert(idxLbrMsr < RT_ELEMENTS(pVmcsInfoShared->au64LbrFromIpMsr));
                        pVmcsInfoShared->au64LbrToIpMsr[idxLbrMsr] = pMsrs[i].u64Value;
                        break;
                    }
                    if (idMsr == pVM->hmr0.s.vmx.idLbrTosMsr)
                    {
                        pVmcsInfoShared->u64LbrTosMsr = pMsrs[i].u64Value;
                        break;
                    }
                    /* Fallthru (no break) */
                }
                pVCpu->cpum.GstCtx.fExtrn = 0;
                VCPU_2_VMXSTATE(pVCpu).u32HMError = pMsrs->u32Msr;
                ASMSetFlags(fEFlags);
                AssertMsgFailed(("Unexpected MSR in auto-load/store area. idMsr=%#RX32 cMsrs=%u\n", idMsr, cMsrs));
                return VERR_HM_UNEXPECTED_LD_ST_MSR;
            }
        }
    }
    return VINF_SUCCESS;
}
#endif /* !IN_NEM_DARWIN */


/**
 * Imports the guest CR0 from the VMCS into the guest-CPU context.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
DECL_FORCE_INLINE(void) vmxHCImportGuestCr0(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo)
{
    uint64_t u64Cr0;
    uint64_t u64Shadow;
    int rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_CR0,            &u64Cr0);       AssertRC(rc);
    rc     = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_CTRL_CR0_READ_SHADOW, &u64Shadow);    AssertRC(rc);
#ifndef VBOX_WITH_NESTED_HWVIRT_VMX
    u64Cr0 = (u64Cr0    & ~pVmcsInfo->u64Cr0Mask)
           | (u64Shadow &  pVmcsInfo->u64Cr0Mask);
#else
    if (!CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.GstCtx))
    {
        u64Cr0 = (u64Cr0    & ~pVmcsInfo->u64Cr0Mask)
               | (u64Shadow &  pVmcsInfo->u64Cr0Mask);
    }
    else
    {
        /*
         * We've merged the guest and nested-guest's CR0 guest/host mask while executing
         * the nested-guest using hardware-assisted VMX. Accordingly we need to
         * re-construct CR0. See @bugref{9180#c95} for details.
         */
        PCVMXVMCSINFO const pVmcsInfoGst = &pVCpu->hmr0.s.vmx.VmcsInfo;
        PVMXVVMCS const     pVmcsNstGst  = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
        u64Cr0 = (u64Cr0                     & ~(pVmcsInfoGst->u64Cr0Mask & pVmcsNstGst->u64Cr0Mask.u))
               | (pVmcsNstGst->u64GuestCr0.u &   pVmcsNstGst->u64Cr0Mask.u)
               | (u64Shadow                  &  (pVmcsInfoGst->u64Cr0Mask & ~pVmcsNstGst->u64Cr0Mask.u));
        Assert(u64Cr0 & X86_CR0_NE);
    }
#endif

#ifndef IN_NEM_DARWIN
    VMMRZCallRing3Disable(pVCpu);   /* May call into PGM which has Log statements. */
#endif
    CPUMSetGuestCR0(pVCpu, u64Cr0);
#ifndef IN_NEM_DARWIN
    VMMRZCallRing3Enable(pVCpu);
#endif
}


/**
 * Imports the guest CR3 from the VMCS into the guest-CPU context.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECL_FORCE_INLINE(void) vmxHCImportGuestCr3(PVMCPUCC pVCpu)
{
    PVMCC const pVM  = pVCpu->CTX_SUFF(pVM);
    PCPUMCTX    pCtx = &pVCpu->cpum.GstCtx;

    /* CR0.PG bit changes are always intercepted, so it's up to date. */
    if (   VM_IS_VMX_UNRESTRICTED_GUEST(pVM)
        || (   VM_IS_VMX_NESTED_PAGING(pVM)
            && CPUMIsGuestPagingEnabledEx(pCtx)))
    {
        uint64_t u64Cr3;
        int rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_CR3, &u64Cr3);  AssertRC(rc);
        if (pCtx->cr3 != u64Cr3)
        {
            pCtx->cr3 = u64Cr3;
            VMCPU_FF_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3);
        }

        /*
         * If the guest is in PAE mode, sync back the PDPE's into the guest state.
         * CR4.PAE, CR0.PG, EFER MSR changes are always intercepted, so they're up to date.
         */
        if (CPUMIsGuestInPAEModeEx(pCtx))
        {
            X86PDPE aPaePdpes[4];
            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PDPTE0_FULL, &aPaePdpes[0].u);     AssertRC(rc);
            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PDPTE1_FULL, &aPaePdpes[1].u);     AssertRC(rc);
            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PDPTE2_FULL, &aPaePdpes[2].u);     AssertRC(rc);
            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PDPTE3_FULL, &aPaePdpes[3].u);     AssertRC(rc);
            if (memcmp(&aPaePdpes[0], &pCtx->aPaePdpes[0], sizeof(aPaePdpes)))
            {
                memcpy(&pCtx->aPaePdpes[0], &aPaePdpes[0], sizeof(aPaePdpes));
                /* PGM now updates PAE PDPTEs while updating CR3. */
                VMCPU_FF_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3);
            }
        }
    }
}


/**
 * Imports the guest CR4 from the VMCS into the guest-CPU context.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
DECL_FORCE_INLINE(void) vmxHCImportGuestCr4(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo)
{
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    uint64_t u64Cr4;
    uint64_t u64Shadow;
    int rc  = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_CR4,            &u64Cr4);      AssertRC(rc);
    rc      = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_CTRL_CR4_READ_SHADOW, &u64Shadow);   AssertRC(rc);
#ifndef VBOX_WITH_NESTED_HWVIRT_VMX
    u64Cr4 = (u64Cr4    & ~pVmcsInfo->u64Cr4Mask)
           | (u64Shadow &  pVmcsInfo->u64Cr4Mask);
#else
    if (!CPUMIsGuestInVmxNonRootMode(pCtx))
    {
        u64Cr4 = (u64Cr4    & ~pVmcsInfo->u64Cr4Mask)
               | (u64Shadow &  pVmcsInfo->u64Cr4Mask);
    }
    else
    {
        /*
         * We've merged the guest and nested-guest's CR4 guest/host mask while executing
         * the nested-guest using hardware-assisted VMX. Accordingly we need to
         * re-construct CR4. See @bugref{9180#c95} for details.
         */
        PCVMXVMCSINFO const pVmcsInfoGst = &pVCpu->hmr0.s.vmx.VmcsInfo;
        PVMXVVMCS const     pVmcsNstGst  = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
        u64Cr4 = (u64Cr4                     & ~(pVmcsInfo->u64Cr4Mask & pVmcsNstGst->u64Cr4Mask.u))
               | (pVmcsNstGst->u64GuestCr4.u &   pVmcsNstGst->u64Cr4Mask.u)
               | (u64Shadow                  &  (pVmcsInfoGst->u64Cr4Mask & ~pVmcsNstGst->u64Cr4Mask.u));
        Assert(u64Cr4 & X86_CR4_VMXE);
    }
#endif
    pCtx->cr4 = u64Cr4;
}


/**
 * Worker for vmxHCImportGuestIntrState that handles the case where any of the
 * relevant VMX_VMCS32_GUEST_INT_STATE bits are set.
 */
DECL_NO_INLINE(static,void) vmxHCImportGuestIntrStateSlow(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo, uint32_t fGstIntState)
{
    /*
     * We must import RIP here to set our EM interrupt-inhibited state.
     * We also import RFLAGS as our code that evaluates pending interrupts
     * before VM-entry requires it.
     */
    vmxHCImportGuestRip(pVCpu);
    vmxHCImportGuestRFlags(pVCpu, pVmcsInfo);

    CPUMUpdateInterruptShadowSsStiEx(&pVCpu->cpum.GstCtx,
                                     RT_BOOL(fGstIntState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS),
                                     RT_BOOL(fGstIntState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI),
                                     pVCpu->cpum.GstCtx.rip);
    CPUMUpdateInterruptInhibitingByNmiEx(&pVCpu->cpum.GstCtx, RT_BOOL(fGstIntState & VMX_VMCS_GUEST_INT_STATE_BLOCK_NMI));
}


/**
 * Imports the guest interruptibility-state from the VMCS into the guest-CPU
 * context.
 *
 * @note    May import RIP and RFLAGS if interrupt or NMI are blocked.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks Called with interrupts and/or preemption disabled, try not to assert and
 *          do not log!
 * @remarks Do -not- call this function directly, use vmxHCImportGuestState()
 *          instead!!!
 */
DECLINLINE(void) vmxHCImportGuestIntrState(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo)
{
    uint32_t u32Val;
    int rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_INT_STATE, &u32Val);    AssertRC(rc);
    Assert((u32Val & (VMX_VMCS_GUEST_INT_STATE_BLOCK_STI | VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS))
                  != (VMX_VMCS_GUEST_INT_STATE_BLOCK_STI | VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS));
    if (!u32Val)
    {
        CPUMClearInterruptShadow(&pVCpu->cpum.GstCtx);
        CPUMClearInterruptInhibitingByNmiEx(&pVCpu->cpum.GstCtx);
    }
    else
        vmxHCImportGuestIntrStateSlow(pVCpu, pVmcsInfo, u32Val);
}


/**
 * Worker for VMXR0ImportStateOnDemand.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 * @param   fWhat       What to import, CPUMCTX_EXTRN_XXX.
 */
static int vmxHCImportGuestStateEx(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, uint64_t fWhat)
{
    int      rc   = VINF_SUCCESS;
    PVMCC    pVM  = pVCpu->CTX_SUFF(pVM);
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    uint32_t u32Val;

    /*
     * Note! This is hack to workaround a mysterious BSOD observed with release builds
     *       on Windows 10 64-bit hosts. Profile and debug builds are not affected and
     *       neither are other host platforms.
     *
     *       Committing this temporarily as it prevents BSOD.
     *
     * Update: This is very likely a compiler optimization bug, see @bugref{9180}.
     */
#ifdef RT_OS_WINDOWS
    if (pVM == 0 || pVM == (void *)(uintptr_t)-1)
        return VERR_HM_IPE_1;
#endif

    STAM_PROFILE_ADV_START(&VCPU_2_VMXSTATS(pVCpu).StatImportGuestState, x);

#ifndef IN_NEM_DARWIN
    /*
     * We disable interrupts to make the updating of the state and in particular
     * the fExtrn modification atomic wrt to preemption hooks.
     */
    RTCCUINTREG const fEFlags = ASMIntDisableFlags();
#endif

    fWhat &= pCtx->fExtrn;
    if (fWhat)
    {
        do
        {
            if (fWhat & CPUMCTX_EXTRN_RIP)
                vmxHCImportGuestRip(pVCpu);

            if (fWhat & CPUMCTX_EXTRN_RFLAGS)
                vmxHCImportGuestRFlags(pVCpu, pVmcsInfo);

            /* Note! vmxHCImportGuestIntrState may also include RIP and RFLAGS and update fExtrn. */
            if (fWhat & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI))
                vmxHCImportGuestIntrState(pVCpu, pVmcsInfo);

            if (fWhat & CPUMCTX_EXTRN_RSP)
            {
                rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_RSP, &pCtx->rsp);
                AssertRC(rc);
            }

            if (fWhat & CPUMCTX_EXTRN_SREG_MASK)
            {
                PVMXVMCSINFOSHARED pVmcsInfoShared = pVmcsInfo->pShared;
#ifndef IN_NEM_DARWIN
                bool const fRealOnV86Active = pVmcsInfoShared->RealMode.fRealOnV86Active;
#else
                bool const fRealOnV86Active = false; /* HV supports only unrestricted guest execution. */
#endif
                if (fWhat & CPUMCTX_EXTRN_CS)
                {
                    vmxHCImportGuestSegReg<X86_SREG_CS>(pVCpu);
                    vmxHCImportGuestRip(pVCpu); /** @todo WTF? */
                    if (fRealOnV86Active)
                        pCtx->cs.Attr.u = pVmcsInfoShared->RealMode.AttrCS.u;
                    EMHistoryUpdatePC(pVCpu, pCtx->cs.u64Base + pCtx->rip, true /* fFlattened */);
                }
                if (fWhat & CPUMCTX_EXTRN_SS)
                {
                    vmxHCImportGuestSegReg<X86_SREG_SS>(pVCpu);
                    if (fRealOnV86Active)
                        pCtx->ss.Attr.u = pVmcsInfoShared->RealMode.AttrSS.u;
                }
                if (fWhat & CPUMCTX_EXTRN_DS)
                {
                    vmxHCImportGuestSegReg<X86_SREG_DS>(pVCpu);
                    if (fRealOnV86Active)
                        pCtx->ds.Attr.u = pVmcsInfoShared->RealMode.AttrDS.u;
                }
                if (fWhat & CPUMCTX_EXTRN_ES)
                {
                    vmxHCImportGuestSegReg<X86_SREG_ES>(pVCpu);
                    if (fRealOnV86Active)
                        pCtx->es.Attr.u = pVmcsInfoShared->RealMode.AttrES.u;
                }
                if (fWhat & CPUMCTX_EXTRN_FS)
                {
                    vmxHCImportGuestSegReg<X86_SREG_FS>(pVCpu);
                    if (fRealOnV86Active)
                        pCtx->fs.Attr.u = pVmcsInfoShared->RealMode.AttrFS.u;
                }
                if (fWhat & CPUMCTX_EXTRN_GS)
                {
                    vmxHCImportGuestSegReg<X86_SREG_GS>(pVCpu);
                    if (fRealOnV86Active)
                        pCtx->gs.Attr.u = pVmcsInfoShared->RealMode.AttrGS.u;
                }
            }

            if (fWhat & CPUMCTX_EXTRN_TABLE_MASK)
            {
                if (fWhat & CPUMCTX_EXTRN_LDTR)
                    vmxHCImportGuestLdtr(pVCpu);

                if (fWhat & CPUMCTX_EXTRN_GDTR)
                {
                    rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_GDTR_BASE,    &pCtx->gdtr.pGdt);  AssertRC(rc);
                    rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_GDTR_LIMIT, &u32Val);           AssertRC(rc);
                    pCtx->gdtr.cbGdt = u32Val;
                }

                /* Guest IDTR. */
                if (fWhat & CPUMCTX_EXTRN_IDTR)
                {
                    rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_IDTR_BASE,    &pCtx->idtr.pIdt);  AssertRC(rc);
                    rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_IDTR_LIMIT, &u32Val);           AssertRC(rc);
                    pCtx->idtr.cbIdt = u32Val;
                }

                /* Guest TR. */
                if (fWhat & CPUMCTX_EXTRN_TR)
                {
#ifndef IN_NEM_DARWIN
                    /* Real-mode emulation using virtual-8086 mode has the fake TSS (pRealModeTSS) in TR,
                       don't need to import that one. */
                    if (!pVmcsInfo->pShared->RealMode.fRealOnV86Active)
#endif
                        vmxHCImportGuestTr(pVCpu);
                }
            }

            if (fWhat & CPUMCTX_EXTRN_DR7)
            {
#ifndef IN_NEM_DARWIN
                if (!pVCpu->hmr0.s.fUsingHyperDR7)
#endif
                {
                    rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_DR7, &pCtx->dr[7]);
                    AssertRC(rc);
                }
            }

            if (fWhat & CPUMCTX_EXTRN_SYSENTER_MSRS)
            {
                rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_SYSENTER_EIP,  &pCtx->SysEnter.eip);  AssertRC(rc);
                rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_SYSENTER_ESP,  &pCtx->SysEnter.esp);  AssertRC(rc);
                rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_SYSENTER_CS, &u32Val);              AssertRC(rc);
                pCtx->SysEnter.cs = u32Val;
            }

#ifndef IN_NEM_DARWIN
            if (fWhat & CPUMCTX_EXTRN_KERNEL_GS_BASE)
            {
                if (   pVM->hmr0.s.fAllow64BitGuests
                    && (pVCpu->hmr0.s.vmx.fLazyMsrs & VMX_LAZY_MSRS_LOADED_GUEST))
                    pCtx->msrKERNELGSBASE = ASMRdMsr(MSR_K8_KERNEL_GS_BASE);
            }

            if (fWhat & CPUMCTX_EXTRN_SYSCALL_MSRS)
            {
                if (   pVM->hmr0.s.fAllow64BitGuests
                    && (pVCpu->hmr0.s.vmx.fLazyMsrs & VMX_LAZY_MSRS_LOADED_GUEST))
                {
                    pCtx->msrLSTAR  = ASMRdMsr(MSR_K8_LSTAR);
                    pCtx->msrSTAR   = ASMRdMsr(MSR_K6_STAR);
                    pCtx->msrSFMASK = ASMRdMsr(MSR_K8_SF_MASK);
                }
            }

            if (fWhat & (CPUMCTX_EXTRN_TSC_AUX | CPUMCTX_EXTRN_OTHER_MSRS))
            {
                rc = vmxHCImportGuestTscAuxAndOtherMsrs(pVCpu, pVmcsInfo, fEFlags);
                AssertRCReturn(rc, rc);
            }
#else
            NOREF(pVM);
#endif

            if (fWhat & CPUMCTX_EXTRN_CR_MASK)
            {
                if (fWhat & CPUMCTX_EXTRN_CR0)
                    vmxHCImportGuestCr0(pVCpu, pVmcsInfo);

                if (fWhat & CPUMCTX_EXTRN_CR4)
                    vmxHCImportGuestCr4(pVCpu, pVmcsInfo);

                if (fWhat & CPUMCTX_EXTRN_CR3)
                    vmxHCImportGuestCr3(pVCpu);
            }

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
            if (fWhat & CPUMCTX_EXTRN_HWVIRT)
            {
                if (   (pVmcsInfo->u32ProcCtls2 & VMX_PROC_CTLS2_VMCS_SHADOWING)
                    && !CPUMIsGuestInVmxNonRootMode(pCtx))
                {
                    Assert(CPUMIsGuestInVmxRootMode(pCtx));
                    rc = vmxHCCopyShadowToNstGstVmcs(pVCpu, pVmcsInfo);
                    pVCpu->hm.s.vmx.fCopiedNstGstToShadowVmcs = false;
                    if (RT_SUCCESS(rc))
                    { /* likely */ }
                    else
                        break;
                }
            }
#endif
        } while (0);

        if (RT_SUCCESS(rc))
        {
            /* Update fExtrn. */
            pCtx->fExtrn &= ~fWhat;

            /* If everything has been imported, clear the HM keeper bit. */
            if (!(pCtx->fExtrn & HMVMX_CPUMCTX_EXTRN_ALL))
            {
#ifndef IN_NEM_DARWIN
                pCtx->fExtrn &= ~CPUMCTX_EXTRN_KEEPER_HM;
#else
                pCtx->fExtrn &= ~CPUMCTX_EXTRN_KEEPER_NEM;
#endif
                Assert(!pCtx->fExtrn);
            }
        }
    }
#ifndef IN_NEM_DARWIN
    else
        AssertMsg(!pCtx->fExtrn || (pCtx->fExtrn & HMVMX_CPUMCTX_EXTRN_ALL), ("%#RX64\n", pCtx->fExtrn));

    /*
     * Restore interrupts.
     */
    ASMSetFlags(fEFlags);
#endif

    STAM_PROFILE_ADV_STOP(&VCPU_2_VMXSTATS(pVCpu).StatImportGuestState, x);

    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
        return rc;

    /*
     * Honor any pending CR3 updates.
     *
     * Consider this scenario: VM-exit -> VMMRZCallRing3Enable() -> do stuff that causes a longjmp -> VMXR0CallRing3Callback()
     * -> VMMRZCallRing3Disable() -> vmxHCImportGuestState() -> Sets VMCPU_FF_HM_UPDATE_CR3 pending -> return from the longjmp
     * -> continue with VM-exit handling -> vmxHCImportGuestState() and here we are.
     *
     * The reason for such complicated handling is because VM-exits that call into PGM expect CR3 to be up-to-date and thus
     * if any CR3-saves -before- the VM-exit (longjmp) postponed the CR3 update via the force-flag, any VM-exit handler that
     * calls into PGM when it re-saves CR3 will end up here and we call PGMUpdateCR3(). This is why the code below should
     * -NOT- check if CPUMCTX_EXTRN_CR3 is set!
     *
     * The longjmp exit path can't check these CR3 force-flags and call code that takes a lock again. We cover for it here.
     *
     * The force-flag is checked first as it's cheaper for potential superfluous calls to this function.
     */
    if (   VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3)
#ifndef IN_NEM_DARWIN
        && VMMRZCallRing3IsEnabled(pVCpu)
#endif
        )
    {
        Assert(!(ASMAtomicUoReadU64(&pCtx->fExtrn) & CPUMCTX_EXTRN_CR3));
        PGMUpdateCR3(pVCpu, CPUMGetGuestCR3(pVCpu));
        Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3));
    }

    return VINF_SUCCESS;
}


/**
 * Internal state fetcher, inner version where we fetch all of a_fWhat.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 * @param   fEFlags     Saved EFLAGS for restoring the interrupt flag. Ignored
 *                      in NEM/darwin context.
 * @tparam  a_fWhat     What to import, zero or more bits from
 *                      HMVMX_CPUMCTX_EXTRN_ALL.
 */
template<uint64_t const a_fWhat>
static int vmxHCImportGuestStateInner(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, uint32_t fEFlags)
{
    Assert(a_fWhat != 0); /* No AssertCompile as the assertion probably kicks in before the compiler (clang) discards it. */
    AssertCompile(!(a_fWhat & ~HMVMX_CPUMCTX_EXTRN_ALL));
    Assert(   (pVCpu->cpum.GstCtx.fExtrn & a_fWhat) == a_fWhat
           || (pVCpu->cpum.GstCtx.fExtrn & a_fWhat) == (a_fWhat & ~(CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS)));

    STAM_PROFILE_ADV_STOP(&VCPU_2_VMXSTATS(pVCpu).StatImportGuestState, x);

    PVMCC const pVM  = pVCpu->CTX_SUFF(pVM);

    /* RIP and RFLAGS may have been imported already by the post exit code
       together with the CPUMCTX_EXTRN_INHIBIT_INT/NMI state, so this part
       of the code is skipping this part of the code. */
    if (   (a_fWhat & (CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS))
        && pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS))
    {
        if (a_fWhat & CPUMCTX_EXTRN_RFLAGS)
            vmxHCImportGuestCoreRFlags(pVCpu, pVmcsInfo);

        if (a_fWhat & CPUMCTX_EXTRN_RIP)
        {
            if (!(a_fWhat & CPUMCTX_EXTRN_CS))
                EMHistoryUpdatePC(pVCpu, vmxHCImportGuestCoreRip(pVCpu), false);
            else
                vmxHCImportGuestCoreRip(pVCpu);
        }
    }

    /* Note! vmxHCImportGuestIntrState may also include RIP and RFLAGS and update fExtrn. */
    if (a_fWhat & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI))
        vmxHCImportGuestIntrState(pVCpu, pVmcsInfo);

    if (a_fWhat & (CPUMCTX_EXTRN_SREG_MASK | CPUMCTX_EXTRN_TR))
    {
        if (a_fWhat & CPUMCTX_EXTRN_CS)
        {
            vmxHCImportGuestSegReg<X86_SREG_CS>(pVCpu);
            /** @todo try get rid of this carp, it smells and is probably never ever
             *        used: */
            if (   !(a_fWhat & CPUMCTX_EXTRN_RIP)
                && (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_RIP))
            {
                vmxHCImportGuestCoreRip(pVCpu);
                pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_RIP;
            }
            EMHistoryUpdatePC(pVCpu, pVCpu->cpum.GstCtx.cs.u64Base + pVCpu->cpum.GstCtx.rip, true /* fFlattened */);
        }
        if (a_fWhat & CPUMCTX_EXTRN_SS)
            vmxHCImportGuestSegReg<X86_SREG_SS>(pVCpu);
        if (a_fWhat & CPUMCTX_EXTRN_DS)
            vmxHCImportGuestSegReg<X86_SREG_DS>(pVCpu);
        if (a_fWhat & CPUMCTX_EXTRN_ES)
            vmxHCImportGuestSegReg<X86_SREG_ES>(pVCpu);
        if (a_fWhat & CPUMCTX_EXTRN_FS)
            vmxHCImportGuestSegReg<X86_SREG_FS>(pVCpu);
        if (a_fWhat & CPUMCTX_EXTRN_GS)
            vmxHCImportGuestSegReg<X86_SREG_GS>(pVCpu);

        /* Guest TR.
           Real-mode emulation using virtual-8086 mode has the fake TSS
           (pRealModeTSS) in TR, don't need to import that one. */
#ifndef IN_NEM_DARWIN
        PVMXVMCSINFOSHARED const pVmcsInfoShared  = pVmcsInfo->pShared;
        bool const               fRealOnV86Active = pVmcsInfoShared->RealMode.fRealOnV86Active;
        if ((a_fWhat & CPUMCTX_EXTRN_TR) && !fRealOnV86Active)
#else
        if (a_fWhat & CPUMCTX_EXTRN_TR)
#endif
            vmxHCImportGuestTr(pVCpu);

#ifndef IN_NEM_DARWIN /* NEM/Darwin: HV supports only unrestricted guest execution. */
        if (fRealOnV86Active)
        {
            if (a_fWhat & CPUMCTX_EXTRN_CS)
                pVCpu->cpum.GstCtx.cs.Attr.u = pVmcsInfoShared->RealMode.AttrCS.u;
            if (a_fWhat & CPUMCTX_EXTRN_SS)
                pVCpu->cpum.GstCtx.ss.Attr.u = pVmcsInfoShared->RealMode.AttrSS.u;
            if (a_fWhat & CPUMCTX_EXTRN_DS)
                pVCpu->cpum.GstCtx.ds.Attr.u = pVmcsInfoShared->RealMode.AttrDS.u;
            if (a_fWhat & CPUMCTX_EXTRN_ES)
                pVCpu->cpum.GstCtx.es.Attr.u = pVmcsInfoShared->RealMode.AttrES.u;
            if (a_fWhat & CPUMCTX_EXTRN_FS)
                pVCpu->cpum.GstCtx.fs.Attr.u = pVmcsInfoShared->RealMode.AttrFS.u;
            if (a_fWhat & CPUMCTX_EXTRN_GS)
                pVCpu->cpum.GstCtx.gs.Attr.u = pVmcsInfoShared->RealMode.AttrGS.u;
        }
#endif
    }

    if (a_fWhat & CPUMCTX_EXTRN_RSP)
    {
        int const rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_RSP, &pVCpu->cpum.GstCtx.rsp);
        AssertRC(rc);
    }

    if (a_fWhat & CPUMCTX_EXTRN_LDTR)
        vmxHCImportGuestLdtr(pVCpu);

    if (a_fWhat & CPUMCTX_EXTRN_GDTR)
    {
        int const rc1 = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_GDTR_BASE,    &pVCpu->cpum.GstCtx.gdtr.pGdt); AssertRC(rc1);
        uint32_t u32Val;
        int const rc2 = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_GDTR_LIMIT, &u32Val); AssertRC(rc2);
        pVCpu->cpum.GstCtx.gdtr.cbGdt = (uint16_t)u32Val;
    }

    /* Guest IDTR. */
    if (a_fWhat & CPUMCTX_EXTRN_IDTR)
    {
        int const rc1 = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_IDTR_BASE,    &pVCpu->cpum.GstCtx.idtr.pIdt); AssertRC(rc1);
        uint32_t u32Val;
        int const rc2 = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_IDTR_LIMIT, &u32Val); AssertRC(rc2);
        pVCpu->cpum.GstCtx.idtr.cbIdt = (uint64_t)u32Val;
    }

    if (a_fWhat & CPUMCTX_EXTRN_DR7)
    {
#ifndef IN_NEM_DARWIN
        if (!pVCpu->hmr0.s.fUsingHyperDR7)
#endif
        {
            int rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_DR7, &pVCpu->cpum.GstCtx.dr[7]);
            AssertRC(rc);
        }
    }

    if (a_fWhat & CPUMCTX_EXTRN_SYSENTER_MSRS)
    {
        int const rc1 = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_SYSENTER_EIP,  &pVCpu->cpum.GstCtx.SysEnter.eip); AssertRC(rc1);
        int const rc2 = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_SYSENTER_ESP,  &pVCpu->cpum.GstCtx.SysEnter.esp); AssertRC(rc2);
        uint32_t u32Val;
        int const rc3 = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_SYSENTER_CS, &u32Val); AssertRC(rc3);
        pVCpu->cpum.GstCtx.SysEnter.cs = u32Val;
    }

#ifndef IN_NEM_DARWIN
    if (a_fWhat & CPUMCTX_EXTRN_KERNEL_GS_BASE)
    {
        if (   (pVCpu->hmr0.s.vmx.fLazyMsrs & VMX_LAZY_MSRS_LOADED_GUEST)
            && pVM->hmr0.s.fAllow64BitGuests)
            pVCpu->cpum.GstCtx.msrKERNELGSBASE = ASMRdMsr(MSR_K8_KERNEL_GS_BASE);
    }

    if (a_fWhat & CPUMCTX_EXTRN_SYSCALL_MSRS)
    {
        if (   (pVCpu->hmr0.s.vmx.fLazyMsrs & VMX_LAZY_MSRS_LOADED_GUEST)
            && pVM->hmr0.s.fAllow64BitGuests)
        {
            pVCpu->cpum.GstCtx.msrLSTAR  = ASMRdMsr(MSR_K8_LSTAR);
            pVCpu->cpum.GstCtx.msrSTAR   = ASMRdMsr(MSR_K6_STAR);
            pVCpu->cpum.GstCtx.msrSFMASK = ASMRdMsr(MSR_K8_SF_MASK);
        }
    }

    if (a_fWhat & (CPUMCTX_EXTRN_TSC_AUX | CPUMCTX_EXTRN_OTHER_MSRS))
    {
        int const rc1 = vmxHCImportGuestTscAuxAndOtherMsrs(pVCpu, pVmcsInfo, fEFlags);
        AssertRCReturn(rc1, rc1);
    }
#else
    NOREF(pVM);
#endif

    if (a_fWhat & CPUMCTX_EXTRN_CR0)
        vmxHCImportGuestCr0(pVCpu, pVmcsInfo);

    if (a_fWhat & CPUMCTX_EXTRN_CR4)
        vmxHCImportGuestCr4(pVCpu, pVmcsInfo);

    if (a_fWhat & CPUMCTX_EXTRN_CR3)
        vmxHCImportGuestCr3(pVCpu);

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (a_fWhat & CPUMCTX_EXTRN_HWVIRT)
    {
        if (   (pVmcsInfo->u32ProcCtls2 & VMX_PROC_CTLS2_VMCS_SHADOWING)
            && !CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.GstCtx))
        {
            Assert(CPUMIsGuestInVmxRootMode(&pVCpu->cpum.GstCtx));
            int const rc = vmxHCCopyShadowToNstGstVmcs(pVCpu, pVmcsInfo);
            AssertRCReturn(rc, rc);
            pVCpu->hm.s.vmx.fCopiedNstGstToShadowVmcs = false;
        }
    }
#endif

    /* Update fExtrn. */
    pVCpu->cpum.GstCtx.fExtrn &= ~a_fWhat;

    /* If everything has been imported, clear the HM keeper bit. */
    if (!(pVCpu->cpum.GstCtx.fExtrn & HMVMX_CPUMCTX_EXTRN_ALL))
    {
#ifndef IN_NEM_DARWIN
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_KEEPER_HM;
#else
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_KEEPER_NEM;
#endif
        Assert(!pVCpu->cpum.GstCtx.fExtrn);
    }

    STAM_PROFILE_ADV_STOP(&VCPU_2_VMXSTATS(pVCpu).StatImportGuestState, x);

    /*
     * Honor any pending CR3 updates.
     *
     * Consider this scenario: VM-exit -> VMMRZCallRing3Enable() -> do stuff that causes a longjmp -> VMXR0CallRing3Callback()
     * -> VMMRZCallRing3Disable() -> vmxHCImportGuestState() -> Sets VMCPU_FF_HM_UPDATE_CR3 pending -> return from the longjmp
     * -> continue with VM-exit handling -> vmxHCImportGuestState() and here we are.
     *
     * The reason for such complicated handling is because VM-exits that call into PGM expect CR3 to be up-to-date and thus
     * if any CR3-saves -before- the VM-exit (longjmp) postponed the CR3 update via the force-flag, any VM-exit handler that
     * calls into PGM when it re-saves CR3 will end up here and we call PGMUpdateCR3(). This is why the code below should
     * -NOT- check if CPUMCTX_EXTRN_CR3 is set!
     *
     * The longjmp exit path can't check these CR3 force-flags and call code that takes a lock again. We cover for it here.
     *
     * The force-flag is checked first as it's cheaper for potential superfluous calls to this function.
     */
#ifndef IN_NEM_DARWIN
    if (!(a_fWhat & CPUMCTX_EXTRN_CR3)
        ? RT_LIKELY(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3) || !VMMRZCallRing3IsEnabled(pVCpu))
        :           !VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3) || !VMMRZCallRing3IsEnabled(pVCpu) )
        return VINF_SUCCESS;
    ASMSetFlags(fEFlags);
#else
    if (!(a_fWhat & CPUMCTX_EXTRN_CR3)
        ? RT_LIKELY(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3))
        :           !VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3) )
        return VINF_SUCCESS;
    RT_NOREF_PV(fEFlags);
#endif

    Assert(!(ASMAtomicUoReadU64(&pVCpu->cpum.GstCtx.fExtrn) & CPUMCTX_EXTRN_CR3));
    PGMUpdateCR3(pVCpu, CPUMGetGuestCR3(pVCpu));
    Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3));
    return VINF_SUCCESS;
}


/**
 * Internal state fetcher.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmcsInfo       The VMCS info. object.
 * @param   pszCaller       For logging.
 * @tparam  a_fWhat         What needs to be imported, CPUMCTX_EXTRN_XXX.
 * @tparam  a_fDoneLocal    What's ASSUMED to have been retrieved locally
 *                          already.  This is ORed together with @a a_fWhat when
 *                          calculating what needs fetching (just for safety).
 * @tparam  a_fDonePostExit What's ASSUMED to been been retrieved by
 *                          hmR0VmxPostRunGuest()/nemR3DarwinHandleExitCommon()
 *                          already.  This is ORed together with @a a_fWhat when
 *                          calculating what needs fetching (just for safety).
 */
template<uint64_t const a_fWhat,
    uint64_t const a_fDoneLocal = 0,
    uint64_t const a_fDonePostExit = 0
#ifndef IN_NEM_DARWIN
                                   | CPUMCTX_EXTRN_INHIBIT_INT
                                   | CPUMCTX_EXTRN_INHIBIT_NMI
# if defined(HMVMX_ALWAYS_SYNC_FULL_GUEST_STATE) || defined(HMVMX_ALWAYS_SAVE_FULL_GUEST_STATE)
                                   | HMVMX_CPUMCTX_EXTRN_ALL
# elif defined(HMVMX_ALWAYS_SAVE_GUEST_RFLAGS)
                                   | CPUMCTX_EXTRN_RFLAGS
# endif
#else  /* IN_NEM_DARWIN */
                                   | CPUMCTX_EXTRN_ALL /** @todo optimize */
#endif /* IN_NEM_DARWIN */
>
DECLINLINE(int) vmxHCImportGuestState(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, const char *pszCaller)
{
    RT_NOREF_PV(pszCaller);
    if ((a_fWhat | a_fDoneLocal | a_fDonePostExit) & HMVMX_CPUMCTX_EXTRN_ALL)
    {
#ifndef IN_NEM_DARWIN
        /*
         * We disable interrupts to make the updating of the state and in particular
         * the fExtrn modification atomic wrt to preemption hooks.
         */
        RTCCUINTREG const fEFlags   = ASMIntDisableFlags();
#else
        RTCCUINTREG const fEFlags   = 0;
#endif

        /*
         * We combine all three parameters and take the (probably) inlined optimized
         * code path for the new things specified in a_fWhat.
         *
         * As a tweak to deal with exits that have INHIBIT_INT/NMI active, causing
         * vmxHCImportGuestIntrState to automatically fetch both RIP & RFLAGS, we
         * also take the streamlined path when both of these are cleared in fExtrn
         * already. vmxHCImportGuestStateInner checks fExtrn before fetching.  This
         * helps with MWAIT and HLT exits that always inhibit IRQs on many platforms.
         */
        uint64_t const    fWhatToDo = pVCpu->cpum.GstCtx.fExtrn
                                    & ((a_fWhat | a_fDoneLocal | a_fDonePostExit) & HMVMX_CPUMCTX_EXTRN_ALL);
        if (RT_LIKELY(   (   fWhatToDo ==   (a_fWhat & HMVMX_CPUMCTX_EXTRN_ALL & ~(a_fDoneLocal | a_fDonePostExit))
                          || fWhatToDo == (  a_fWhat & HMVMX_CPUMCTX_EXTRN_ALL & ~(a_fDoneLocal | a_fDonePostExit)
                                           & ~(CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS)) /* fetch with INHIBIT_INT/NMI */))
            && (a_fWhat & HMVMX_CPUMCTX_EXTRN_ALL & ~(a_fDoneLocal | a_fDonePostExit)) != 0 /* just in case */)
        {
            int const rc = vmxHCImportGuestStateInner<  a_fWhat
                                                      & HMVMX_CPUMCTX_EXTRN_ALL
                                                      & ~(a_fDoneLocal | a_fDonePostExit)>(pVCpu, pVmcsInfo, fEFlags);
#ifndef IN_NEM_DARWIN
            ASMSetFlags(fEFlags);
#endif
            return rc;
        }

#ifndef IN_NEM_DARWIN
        ASMSetFlags(fEFlags);
#endif

        /*
         * We shouldn't normally get here, but it may happen when executing
         * in the debug run-loops.  Typically, everything should already have
         * been fetched then.  Otherwise call the fallback state import function.
         */
        if (fWhatToDo == 0)
        { /* hope the cause was the debug loop or something similar */ }
        else
        {
            STAM_REL_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatImportGuestStateFallback);
            Log11Func(("a_fWhat=%#RX64/%#RX64/%#RX64 fExtrn=%#RX64 => %#RX64 - Taking inefficient code path from %s!\n",
                       a_fWhat & HMVMX_CPUMCTX_EXTRN_ALL, a_fDoneLocal & HMVMX_CPUMCTX_EXTRN_ALL,
                       a_fDonePostExit & HMVMX_CPUMCTX_EXTRN_ALL, pVCpu->cpum.GstCtx.fExtrn, fWhatToDo, pszCaller));
            return vmxHCImportGuestStateEx(pVCpu, pVmcsInfo, a_fWhat | a_fDoneLocal | a_fDonePostExit);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Check per-VM and per-VCPU force flag actions that require us to go back to
 * ring-3 for one reason or another.
 *
 * @returns Strict VBox status code (i.e. informational status codes too)
 * @retval VINF_SUCCESS if we don't have any actions that require going back to
 *         ring-3.
 * @retval VINF_PGM_SYNC_CR3 if we have pending PGM CR3 sync.
 * @retval VINF_EM_PENDING_REQUEST if we have pending requests (like hardware
 *         interrupts)
 * @retval VINF_PGM_POOL_FLUSH_PENDING if PGM is doing a pool flush and requires
 *         all EMTs to be in ring-3.
 * @retval VINF_EM_RAW_TO_R3 if there is pending DMA requests.
 * @retval VINF_EM_NO_MEMORY PGM is out of memory, we need to return
 *         to the EM loop.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fIsNestedGuest  Flag whether this is for a for a pending nested guest event.
 * @param   fStepping       Whether we are single-stepping the guest using the
 *                          hypervisor debugger.
 *
 * @remarks This might cause nested-guest VM-exits, caller must check if the guest
 *          is no longer in VMX non-root mode.
 */
static VBOXSTRICTRC vmxHCCheckForceFlags(PVMCPUCC pVCpu, bool fIsNestedGuest, bool fStepping)
{
#ifndef IN_NEM_DARWIN
    Assert(VMMRZCallRing3IsEnabled(pVCpu));
#endif

    /*
     * Update pending interrupts into the APIC's IRR.
     */
    if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_UPDATE_APIC))
        APICUpdatePendingInterrupts(pVCpu);

    /*
     * Anything pending?  Should be more likely than not if we're doing a good job.
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (  !fStepping
        ?    !VM_FF_IS_ANY_SET(pVM, VM_FF_HP_R0_PRE_HM_MASK)
          && !VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HP_R0_PRE_HM_MASK)
        :    !VM_FF_IS_ANY_SET(pVM, VM_FF_HP_R0_PRE_HM_STEP_MASK)
          && !VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HP_R0_PRE_HM_STEP_MASK) )
        return VINF_SUCCESS;

    /* Pending PGM C3 sync. */
    if (VMCPU_FF_IS_ANY_SET(pVCpu,VMCPU_FF_PGM_SYNC_CR3 | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL))
    {
        PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
        Assert(!(ASMAtomicUoReadU64(&pCtx->fExtrn) & (CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4)));
        VBOXSTRICTRC rcStrict = PGMSyncCR3(pVCpu, pCtx->cr0, pCtx->cr3, pCtx->cr4,
                                           VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3));
        if (rcStrict != VINF_SUCCESS)
        {
            AssertRC(VBOXSTRICTRC_VAL(rcStrict));
            Log4Func(("PGMSyncCR3 forcing us back to ring-3. rc2=%d\n", VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }

    /* Pending HM-to-R3 operations (critsects, timers, EMT rendezvous etc.) */
    if (   VM_FF_IS_ANY_SET(pVM, VM_FF_HM_TO_R3_MASK)
        || VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HM_TO_R3_MASK))
    {
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatSwitchHmToR3FF);
        int rc = RT_LIKELY(!VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)) ? VINF_EM_RAW_TO_R3 : VINF_EM_NO_MEMORY;
        Log4Func(("HM_TO_R3 forcing us back to ring-3. rc=%d (fVM=%#RX64 fCpu=%#RX64)\n",
                  rc, pVM->fGlobalForcedActions, pVCpu->fLocalForcedActions));
        return rc;
    }

    /* Pending VM request packets, such as hardware interrupts. */
    if (   VM_FF_IS_SET(pVM, VM_FF_REQUEST)
        || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_REQUEST))
    {
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatSwitchVmReq);
        Log4Func(("Pending VM request forcing us back to ring-3\n"));
        return VINF_EM_PENDING_REQUEST;
    }

    /* Pending PGM pool flushes. */
    if (VM_FF_IS_SET(pVM, VM_FF_PGM_POOL_FLUSH_PENDING))
    {
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatSwitchPgmPoolFlush);
        Log4Func(("PGM pool flush pending forcing us back to ring-3\n"));
        return VINF_PGM_POOL_FLUSH_PENDING;
    }

    /* Pending DMA requests. */
    if (VM_FF_IS_SET(pVM, VM_FF_PDM_DMA))
    {
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatSwitchDma);
        Log4Func(("Pending DMA request forcing us back to ring-3\n"));
        return VINF_EM_RAW_TO_R3;
    }

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /*
     * Pending nested-guest events.
     *
     * Please note the priority of these events are specified and important.
     * See Intel spec. 29.4.3.2 "APIC-Write Emulation".
     * See Intel spec. 6.9 "Priority Among Simultaneous Exceptions And Interrupts".
     *
     * Interrupt-window and NMI-window VM-exits for the nested-guest need not be
     * handled here. They'll be handled by the hardware while executing the nested-guest
     * or by us when we injecting events that are not part of VM-entry of the nested-guest.
     */
    if (fIsNestedGuest)
    {
        /* Pending nested-guest APIC-write (may or may not cause a VM-exit). */
        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_APIC_WRITE))
        {
            Log4Func(("Pending nested-guest APIC-write\n"));
            VBOXSTRICTRC rcStrict = IEMExecVmxVmexitApicWrite(pVCpu);
            Assert(rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE);
            if (    rcStrict == VINF_SUCCESS
                && !CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.GstCtx))
                return rcStrict;
        }

        /* Pending nested-guest monitor-trap flag (MTF). */
        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_MTF))
        {
            Log4Func(("Pending nested-guest MTF\n"));
            VBOXSTRICTRC rcStrict = IEMExecVmxVmexit(pVCpu, VMX_EXIT_MTF, 0 /* uExitQual */);
            Assert(rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE);
            return rcStrict;
        }

        /* Pending nested-guest VMX-preemption timer expired. */
        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_PREEMPT_TIMER))
        {
            Log4Func(("Pending nested-guest preempt timer\n"));
            VBOXSTRICTRC rcStrict = IEMExecVmxVmexitPreemptTimer(pVCpu);
            Assert(rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE);
            return rcStrict;
        }
    }
#else
    NOREF(fIsNestedGuest);
#endif

    return VINF_SUCCESS;
}


/**
 * Converts any TRPM trap into a pending HM event. This is typically used when
 * entering from ring-3 (not longjmp returns).
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void vmxHCTrpmTrapToPendingEvent(PVMCPUCC pVCpu)
{
    Assert(TRPMHasTrap(pVCpu));
    Assert(!VCPU_2_VMXSTATE(pVCpu).Event.fPending);

    uint8_t     uVector;
    TRPMEVENT   enmTrpmEvent;
    uint32_t    uErrCode;
    RTGCUINTPTR GCPtrFaultAddress;
    uint8_t     cbInstr;
    bool        fIcebp;

    int rc = TRPMQueryTrapAll(pVCpu, &uVector, &enmTrpmEvent, &uErrCode, &GCPtrFaultAddress, &cbInstr, &fIcebp);
    AssertRC(rc);

    uint32_t u32IntInfo;
    u32IntInfo  = uVector | VMX_IDT_VECTORING_INFO_VALID;
    u32IntInfo |= HMTrpmEventTypeToVmxEventType(uVector, enmTrpmEvent, fIcebp);

    rc = TRPMResetTrap(pVCpu);
    AssertRC(rc);
    Log4(("TRPM->HM event: u32IntInfo=%#RX32 enmTrpmEvent=%d cbInstr=%u uErrCode=%#RX32 GCPtrFaultAddress=%#RGv\n",
          u32IntInfo, enmTrpmEvent, cbInstr, uErrCode, GCPtrFaultAddress));

    vmxHCSetPendingEvent(pVCpu, u32IntInfo, cbInstr, uErrCode, GCPtrFaultAddress);
}


/**
 * Converts the pending HM event into a TRPM trap.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void vmxHCPendingEventToTrpmTrap(PVMCPUCC pVCpu)
{
    Assert(VCPU_2_VMXSTATE(pVCpu).Event.fPending);

    /* If a trap was already pending, we did something wrong! */
    Assert(TRPMQueryTrap(pVCpu, NULL /* pu8TrapNo */, NULL /* pEnmType */) == VERR_TRPM_NO_ACTIVE_TRAP);

    uint32_t const  u32IntInfo  = VCPU_2_VMXSTATE(pVCpu).Event.u64IntInfo;
    uint32_t const  uVector     = VMX_IDT_VECTORING_INFO_VECTOR(u32IntInfo);
    TRPMEVENT const enmTrapType = HMVmxEventTypeToTrpmEventType(u32IntInfo);

    Log4(("HM event->TRPM: uVector=%#x enmTrapType=%d\n", uVector, enmTrapType));

    int rc = TRPMAssertTrap(pVCpu, uVector, enmTrapType);
    AssertRC(rc);

    if (VMX_IDT_VECTORING_INFO_IS_ERROR_CODE_VALID(u32IntInfo))
        TRPMSetErrorCode(pVCpu, VCPU_2_VMXSTATE(pVCpu).Event.u32ErrCode);

    if (VMX_IDT_VECTORING_INFO_IS_XCPT_PF(u32IntInfo))
        TRPMSetFaultAddress(pVCpu, VCPU_2_VMXSTATE(pVCpu).Event.GCPtrFaultAddress);
    else
    {
        uint8_t const uVectorType = VMX_IDT_VECTORING_INFO_TYPE(u32IntInfo);
        switch (uVectorType)
        {
            case VMX_IDT_VECTORING_INFO_TYPE_PRIV_SW_XCPT:
                TRPMSetTrapDueToIcebp(pVCpu);
                RT_FALL_THRU();
            case VMX_IDT_VECTORING_INFO_TYPE_SW_INT:
            case VMX_IDT_VECTORING_INFO_TYPE_SW_XCPT:
            {
                AssertMsg(   uVectorType == VMX_IDT_VECTORING_INFO_TYPE_SW_INT
                          || (   uVector == X86_XCPT_BP /* INT3 */
                              || uVector == X86_XCPT_OF /* INTO */
                              || uVector == X86_XCPT_DB /* INT1 (ICEBP) */),
                          ("Invalid vector: uVector=%#x uVectorType=%#x\n", uVector, uVectorType));
                TRPMSetInstrLength(pVCpu, VCPU_2_VMXSTATE(pVCpu).Event.cbInstr);
                break;
            }
        }
    }

    /* We're now done converting the pending event. */
    VCPU_2_VMXSTATE(pVCpu).Event.fPending = false;
}


/**
 * Sets the interrupt-window exiting control in the VMCS which instructs VT-x to
 * cause a VM-exit as soon as the guest is in a state to receive interrupts.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
static void vmxHCSetIntWindowExitVmcs(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    if (g_HmMsrs.u.vmx.ProcCtls.n.allowed1 & VMX_PROC_CTLS_INT_WINDOW_EXIT)
    {
        if (!(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_INT_WINDOW_EXIT))
        {
            pVmcsInfo->u32ProcCtls |= VMX_PROC_CTLS_INT_WINDOW_EXIT;
            int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, pVmcsInfo->u32ProcCtls);
            AssertRC(rc);
        }
        Log4Func(("Enabled interrupt-window exiting\n"));
    } /* else we will deliver interrupts whenever the guest Vm-exits next and is in a state to receive the interrupt. */
}


/**
 * Clears the interrupt-window exiting control in the VMCS.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
DECLINLINE(void) vmxHCClearIntWindowExitVmcs(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    if (pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_INT_WINDOW_EXIT)
    {
        pVmcsInfo->u32ProcCtls &= ~VMX_PROC_CTLS_INT_WINDOW_EXIT;
        int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, pVmcsInfo->u32ProcCtls);
        AssertRC(rc);
        Log4Func(("Disabled interrupt-window exiting\n"));
    }
}


/**
 * Sets the NMI-window exiting control in the VMCS which instructs VT-x to
 * cause a VM-exit as soon as the guest is in a state to receive NMIs.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
static void vmxHCSetNmiWindowExitVmcs(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    if (g_HmMsrs.u.vmx.ProcCtls.n.allowed1 & VMX_PROC_CTLS_NMI_WINDOW_EXIT)
    {
        if (!(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_NMI_WINDOW_EXIT))
        {
            pVmcsInfo->u32ProcCtls |= VMX_PROC_CTLS_NMI_WINDOW_EXIT;
            int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, pVmcsInfo->u32ProcCtls);
            AssertRC(rc);
            Log4Func(("Enabled NMI-window exiting\n"));
        }
    } /* else we will deliver NMIs whenever we VM-exit next, even possibly nesting NMIs. Can't be helped on ancient CPUs. */
}


/**
 * Clears the NMI-window exiting control in the VMCS.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 */
DECLINLINE(void) vmxHCClearNmiWindowExitVmcs(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo)
{
    if (pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_NMI_WINDOW_EXIT)
    {
        pVmcsInfo->u32ProcCtls &= ~VMX_PROC_CTLS_NMI_WINDOW_EXIT;
        int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, pVmcsInfo->u32ProcCtls);
        AssertRC(rc);
        Log4Func(("Disabled NMI-window exiting\n"));
    }
}


/**
 * Injects an event into the guest upon VM-entry by updating the relevant fields
 * in the VM-entry area in the VMCS.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @retval  VINF_SUCCESS if the event is successfully injected into the VMCS.
 * @retval  VINF_EM_RESET if event injection resulted in a triple-fault.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmcsInfo       The VMCS info object.
 * @param   fIsNestedGuest  Flag whether this is for a for a pending nested guest event.
 * @param   pEvent          The event being injected.
 * @param   pfIntrState     Pointer to the VT-x guest-interruptibility-state. This
 *                          will be updated if necessary. This cannot not be NULL.
 * @param   fStepping       Whether we're single-stepping guest execution and should
 *                          return VINF_EM_DBG_STEPPED if the event is injected
 *                          directly (registers modified by us, not by hardware on
 *                          VM-entry).
 */
static VBOXSTRICTRC vmxHCInjectEventVmcs(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, bool fIsNestedGuest, PCHMEVENT pEvent,
                                         bool fStepping, uint32_t *pfIntrState)
{
    /* Intel spec. 24.8.3 "VM-Entry Controls for Event Injection" specifies the interruption-information field to be 32-bits. */
    AssertMsg(!RT_HI_U32(pEvent->u64IntInfo), ("%#RX64\n", pEvent->u64IntInfo));
    Assert(pfIntrState);

#ifdef IN_NEM_DARWIN
    RT_NOREF(fIsNestedGuest, fStepping, pfIntrState);
#endif

    PCPUMCTX          pCtx       = &pVCpu->cpum.GstCtx;
    uint32_t          u32IntInfo = pEvent->u64IntInfo;
    uint32_t const    u32ErrCode = pEvent->u32ErrCode;
    uint32_t const    cbInstr    = pEvent->cbInstr;
    RTGCUINTPTR const GCPtrFault = pEvent->GCPtrFaultAddress;
    uint8_t const     uVector    = VMX_ENTRY_INT_INFO_VECTOR(u32IntInfo);
    uint32_t const    uIntType   = VMX_ENTRY_INT_INFO_TYPE(u32IntInfo);

#ifdef VBOX_STRICT
    /*
     * Validate the error-code-valid bit for hardware exceptions.
     * No error codes for exceptions in real-mode.
     *
     * See Intel spec. 20.1.4 "Interrupt and Exception Handling"
     */
    if (   uIntType == VMX_EXIT_INT_INFO_TYPE_HW_XCPT
        && !CPUMIsGuestInRealModeEx(pCtx))
    {
        switch (uVector)
        {
            case X86_XCPT_PF:
            case X86_XCPT_DF:
            case X86_XCPT_TS:
            case X86_XCPT_NP:
            case X86_XCPT_SS:
            case X86_XCPT_GP:
            case X86_XCPT_AC:
                AssertMsg(VMX_ENTRY_INT_INFO_IS_ERROR_CODE_VALID(u32IntInfo),
                          ("Error-code-valid bit not set for exception that has an error code uVector=%#x\n", uVector));
                RT_FALL_THRU();
            default:
                break;
        }
    }

    /* Cannot inject an NMI when block-by-MOV SS is in effect. */
    Assert(   uIntType != VMX_EXIT_INT_INFO_TYPE_NMI
           || !(*pfIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS));
#endif

    RT_NOREF(uVector);
    if (   uIntType == VMX_EXIT_INT_INFO_TYPE_HW_XCPT
        || uIntType == VMX_EXIT_INT_INFO_TYPE_NMI
        || uIntType == VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT
        || uIntType == VMX_EXIT_INT_INFO_TYPE_SW_XCPT)
    {
        Assert(uVector <= X86_XCPT_LAST);
        Assert(uIntType != VMX_EXIT_INT_INFO_TYPE_NMI          || uVector == X86_XCPT_NMI);
        Assert(uIntType != VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT || uVector == X86_XCPT_DB);
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).aStatInjectedXcpts[uVector]);
    }
    else
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).aStatInjectedIrqs[uVector & MASK_INJECT_IRQ_STAT]);

    /*
     * Hardware interrupts & exceptions cannot be delivered through the software interrupt
     * redirection bitmap to the real mode task in virtual-8086 mode. We must jump to the
     * interrupt handler in the (real-mode) guest.
     *
     * See Intel spec. 20.3 "Interrupt and Exception handling in Virtual-8086 Mode".
     * See Intel spec. 20.1.4 "Interrupt and Exception Handling" for real-mode interrupt handling.
     */
    if (CPUMIsGuestInRealModeEx(pCtx))     /* CR0.PE bit changes are always intercepted, so it's up to date. */
    {
#ifndef IN_NEM_DARWIN
        if (pVCpu->CTX_SUFF(pVM)->hmr0.s.vmx.fUnrestrictedGuest)
#endif
        {
            /*
             * For CPUs with unrestricted guest execution enabled and with the guest
             * in real-mode, we must not set the deliver-error-code bit.
             *
             * See Intel spec. 26.2.1.3 "VM-Entry Control Fields".
             */
            u32IntInfo &= ~VMX_ENTRY_INT_INFO_ERROR_CODE_VALID;
        }
#ifndef IN_NEM_DARWIN
        else
        {
            PVMCC pVM = pVCpu->CTX_SUFF(pVM);
            Assert(PDMVmmDevHeapIsEnabled(pVM));
            Assert(pVM->hm.s.vmx.pRealModeTSS);
            Assert(!CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.GstCtx));

            /* We require RIP, RSP, RFLAGS, CS, IDTR, import them. */
            int rc2 = vmxHCImportGuestStateEx(pVCpu, pVmcsInfo, CPUMCTX_EXTRN_SREG_MASK | CPUMCTX_EXTRN_TABLE_MASK
                                                                | CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RSP | CPUMCTX_EXTRN_RFLAGS);
            AssertRCReturn(rc2, rc2);

            /* Check if the interrupt handler is present in the IVT (real-mode IDT). IDT limit is (4N - 1). */
            size_t const cbIdtEntry = sizeof(X86IDTR16);
            if (uVector * cbIdtEntry + (cbIdtEntry - 1) > pCtx->idtr.cbIdt)
            {
                /* If we are trying to inject a #DF with no valid IDT entry, return a triple-fault. */
                if (uVector == X86_XCPT_DF)
                    return VINF_EM_RESET;

                /* If we're injecting a #GP with no valid IDT entry, inject a double-fault.
                   No error codes for exceptions in real-mode. */
                if (uVector == X86_XCPT_GP)
                {
                    static HMEVENT const s_EventXcptDf
                        = HMEVENT_INIT_ONLY_INT_INFO(  RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR, X86_XCPT_DF)
                                                     | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,   VMX_ENTRY_INT_INFO_TYPE_HW_XCPT)
                                                     | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID, 0)
                                                     | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,          1));
                    return vmxHCInjectEventVmcs(pVCpu, pVmcsInfo, fIsNestedGuest, &s_EventXcptDf, fStepping, pfIntrState);
                }

                /*
                 * If we're injecting an event with no valid IDT entry, inject a #GP.
                 * No error codes for exceptions in real-mode.
                 *
                 * See Intel spec. 20.1.4 "Interrupt and Exception Handling"
                 */
                static HMEVENT const s_EventXcptGp
                    = HMEVENT_INIT_ONLY_INT_INFO(  RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR, X86_XCPT_GP)
                                                 | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,   VMX_ENTRY_INT_INFO_TYPE_HW_XCPT)
                                                 | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID, 0)
                                                 | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,          1));
                return vmxHCInjectEventVmcs(pVCpu, pVmcsInfo, fIsNestedGuest, &s_EventXcptGp, fStepping, pfIntrState);
            }

            /* Software exceptions (#BP and #OF exceptions thrown as a result of INT3 or INTO) */
            uint16_t uGuestIp = pCtx->ip;
            if (uIntType == VMX_ENTRY_INT_INFO_TYPE_SW_XCPT)
            {
                Assert(uVector == X86_XCPT_BP || uVector == X86_XCPT_OF);
                /* #BP and #OF are both benign traps, we need to resume the next instruction. */
                uGuestIp = pCtx->ip + (uint16_t)cbInstr;
            }
            else if (uIntType == VMX_ENTRY_INT_INFO_TYPE_SW_INT)
                uGuestIp = pCtx->ip + (uint16_t)cbInstr;

            /* Get the code segment selector and offset from the IDT entry for the interrupt handler. */
            X86IDTR16 IdtEntry;
            RTGCPHYS const GCPhysIdtEntry = (RTGCPHYS)pCtx->idtr.pIdt + uVector * cbIdtEntry;
            rc2 = PGMPhysSimpleReadGCPhys(pVM, &IdtEntry, GCPhysIdtEntry, cbIdtEntry);
            AssertRCReturn(rc2, rc2);

            /* Construct the stack frame for the interrupt/exception handler. */
            VBOXSTRICTRC rcStrict;
            rcStrict = hmR0VmxRealModeGuestStackPush(pVCpu, (uint16_t)pCtx->eflags.u);
            if (rcStrict == VINF_SUCCESS)
            {
                rcStrict = hmR0VmxRealModeGuestStackPush(pVCpu, pCtx->cs.Sel);
                if (rcStrict == VINF_SUCCESS)
                    rcStrict = hmR0VmxRealModeGuestStackPush(pVCpu, uGuestIp);
            }

            /* Clear the required eflag bits and jump to the interrupt/exception handler. */
            if (rcStrict == VINF_SUCCESS)
            {
                pCtx->eflags.u   &= ~(X86_EFL_IF | X86_EFL_TF | X86_EFL_RF | X86_EFL_AC);
                pCtx->rip         = IdtEntry.offSel;
                pCtx->cs.Sel      = IdtEntry.uSel;
                pCtx->cs.ValidSel = IdtEntry.uSel;
                pCtx->cs.u64Base  = IdtEntry.uSel << cbIdtEntry;
                if (   uIntType == VMX_ENTRY_INT_INFO_TYPE_HW_XCPT
                    && uVector  == X86_XCPT_PF)
                    pCtx->cr2 = GCPtrFault;

                ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_CS  | HM_CHANGED_GUEST_CR2
                                                         | HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS
                                                         | HM_CHANGED_GUEST_RSP);

                /*
                 * If we delivered a hardware exception (other than an NMI) and if there was
                 * block-by-STI in effect, we should clear it.
                 */
                if (*pfIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI)
                {
                    Assert(   uIntType != VMX_ENTRY_INT_INFO_TYPE_NMI
                           && uIntType != VMX_ENTRY_INT_INFO_TYPE_EXT_INT);
                    Log4Func(("Clearing inhibition due to STI\n"));
                    *pfIntrState &= ~VMX_VMCS_GUEST_INT_STATE_BLOCK_STI;
                }

                Log4(("Injected real-mode: u32IntInfo=%#x u32ErrCode=%#x cbInstr=%#x Eflags=%#x CS:EIP=%04x:%04x\n",
                      u32IntInfo, u32ErrCode, cbInstr, pCtx->eflags.u, pCtx->cs.Sel, pCtx->eip));

                /*
                 * The event has been truly dispatched to the guest. Mark it as no longer pending so
                 * we don't attempt to undo it if we are returning to ring-3 before executing guest code.
                 */
                VCPU_2_VMXSTATE(pVCpu).Event.fPending = false;

                /*
                 * If we eventually support nested-guest execution without unrestricted guest execution,
                 * we should set fInterceptEvents here.
                 */
                Assert(!fIsNestedGuest);

                /* If we're stepping and we've changed cs:rip above, bail out of the VMX R0 execution loop. */
                if (fStepping)
                    rcStrict = VINF_EM_DBG_STEPPED;
            }
            AssertMsg(rcStrict == VINF_SUCCESS || rcStrict == VINF_EM_RESET || (rcStrict == VINF_EM_DBG_STEPPED && fStepping),
                      ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
#else
        RT_NOREF(pVmcsInfo);
#endif
    }

    /*
     * Validate.
     */
    Assert(VMX_ENTRY_INT_INFO_IS_VALID(u32IntInfo));                     /* Bit 31 (Valid bit) must be set by caller. */
    Assert(!(u32IntInfo & VMX_BF_ENTRY_INT_INFO_RSVD_12_30_MASK));       /* Bits 30:12 MBZ. */

    /*
     * Inject the event into the VMCS.
     */
    int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_ENTRY_INTERRUPTION_INFO, u32IntInfo);
    if (VMX_ENTRY_INT_INFO_IS_ERROR_CODE_VALID(u32IntInfo))
        rc |= VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_ENTRY_EXCEPTION_ERRCODE, u32ErrCode);
    rc |= VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_ENTRY_INSTR_LENGTH, cbInstr);
    AssertRC(rc);

    /*
     * Update guest CR2 if this is a page-fault.
     */
    if (VMX_ENTRY_INT_INFO_IS_XCPT_PF(u32IntInfo))
        pCtx->cr2 = GCPtrFault;

    Log4(("Injecting u32IntInfo=%#x u32ErrCode=%#x cbInstr=%#x CR2=%#RX64\n", u32IntInfo, u32ErrCode, cbInstr, pCtx->cr2));
    return VINF_SUCCESS;
}


/**
 * Evaluates the event to be delivered to the guest and sets it as the pending
 * event.
 *
 * Toggling of interrupt force-flags here is safe since we update TRPM on premature
 * exits to ring-3 before executing guest code, see vmxHCExitToRing3(). We must
 * NOT restore these force-flags.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmcsInfo       The VMCS information structure.
 * @param   pfIntrState     Where to store the updated VMX guest-interruptibility
 *                          state.
 */
static VBOXSTRICTRC vmxHCEvaluatePendingEvent(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, uint32_t *pfIntrState)
{
    Assert(pfIntrState);
    Assert(!TRPMHasTrap(pVCpu));

    *pfIntrState = vmxHCGetGuestIntrStateWithUpdate(pVCpu);

    /*
     * Evaluate if a new event needs to be injected.
     * An event that's already pending has already performed all necessary checks.
     */
    if (   !VCPU_2_VMXSTATE(pVCpu).Event.fPending
        && !CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx))
    {
        /** @todo SMI. SMIs take priority over NMIs. */

        /*
         * NMIs.
         * NMIs take priority over external interrupts.
         */
        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI))
        {
            if (!CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx))
            {
                /* Finally, inject the NMI and we're done. */
                vmxHCSetPendingXcptNmi(pVCpu);
                VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NMI);
                vmxHCClearNmiWindowExitVmcs(pVCpu, pVmcsInfo);
                return VINF_SUCCESS;
            }
            vmxHCSetNmiWindowExitVmcs(pVCpu, pVmcsInfo);
        }
        else
            vmxHCClearNmiWindowExitVmcs(pVCpu, pVmcsInfo);

        /*
         * External interrupts (PIC/APIC).
         */
        if (    VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)
            && !VCPU_2_VMXSTATE(pVCpu).fSingleInstruction)
        {
            Assert(!DBGFIsStepping(pVCpu));
            int rc = vmxHCImportGuestStateEx(pVCpu, pVmcsInfo, CPUMCTX_EXTRN_RFLAGS);
            AssertRC(rc);

            if (pVCpu->cpum.GstCtx.eflags.u & X86_EFL_IF)
            {
                /*
                 * Once PDMGetInterrupt() returns an interrupt we -must- deliver it.
                 * We cannot re-request the interrupt from the controller again.
                 */
                uint8_t u8Interrupt;
                rc = PDMGetInterrupt(pVCpu, &u8Interrupt);
                if (RT_SUCCESS(rc))
                    vmxHCSetPendingExtInt(pVCpu, u8Interrupt);
                else if (rc == VERR_APIC_INTR_MASKED_BY_TPR)
                {
                    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatSwitchTprMaskedIrq);
                    if (pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW)
                        vmxHCApicSetTprThreshold(pVCpu, pVmcsInfo, u8Interrupt >> 4);
                    /*
                     * If the CPU doesn't have TPR shadowing, we will always get a VM-exit on TPR changes and
                     * APICSetTpr() will end up setting the VMCPU_FF_INTERRUPT_APIC if required, so there is no
                     * need to re-set this force-flag here.
                     */
                }
                else
                    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatSwitchGuestIrq);

                vmxHCClearIntWindowExitVmcs(pVCpu, pVmcsInfo);
                return VINF_SUCCESS;
            }
            vmxHCSetIntWindowExitVmcs(pVCpu, pVmcsInfo);
        }
        else
            vmxHCClearIntWindowExitVmcs(pVCpu, pVmcsInfo);
    }
    else
    {
        /*
         * An event is being injected or we are in an interrupt shadow.
         * If another event is pending currently, instruct VT-x to cause a VM-exit as
         * soon as the guest is ready to accept it.
         */
        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI))
            vmxHCSetNmiWindowExitVmcs(pVCpu, pVmcsInfo);
        else
        {
            Assert(!(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_NMI_WINDOW_EXIT));
            if (    VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)
                && !VCPU_2_VMXSTATE(pVCpu).fSingleInstruction)
                vmxHCSetIntWindowExitVmcs(pVCpu, pVmcsInfo);
            else
            {
                /* It's possible that interrupt-window exiting is still active, clear it as it's now unnecessary. */
                vmxHCClearIntWindowExitVmcs(pVCpu, pVmcsInfo);
            }
        }
    }

    return VINF_SUCCESS;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/**
 * Evaluates the event to be delivered to the nested-guest and sets it as the
 * pending event.
 *
 * Toggling of interrupt force-flags here is safe since we update TRPM on premature
 * exits to ring-3 before executing guest code, see vmxHCExitToRing3(). We must
 * NOT restore these force-flags.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmcsInfo       The VMCS information structure.
 * @param   pfIntrState     Where to store the updated VMX guest-interruptibility
 *                          state.
 *
 * @remarks The guest must be in VMX non-root mode.
 */
static VBOXSTRICTRC vmxHCEvaluatePendingEventNested(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, uint32_t *pfIntrState)
{
    PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;

    Assert(pfIntrState);
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    Assert(!TRPMHasTrap(pVCpu));

    *pfIntrState = vmxHCGetGuestIntrStateWithUpdate(pVCpu);

    /*
     * If we are injecting an event, all necessary checks have been performed.
     * Any interrupt-window or NMI-window exiting would have been setup by the
     * nested-guest while we merged controls.
     */
    if (VCPU_2_VMXSTATE(pVCpu).Event.fPending)
        return VINF_SUCCESS;

    /*
     * An event injected by VMLAUNCH/VMRESUME instruction emulation should've been
     * made pending (TRPM to HM event) and would be handled above if we resumed
     * execution in HM. If somehow we fell back to emulation after the
     * VMLAUNCH/VMRESUME instruction, it would have been handled in iemRaiseXcptOrInt
     * (calling iemVmxVmexitEvent). Thus, if we get here the nested-hypervisor's VMX
     * intercepts should be active and any events pending here have been generated
     * while executing the guest in VMX non-root mode after virtual VM-entry completed.
     */
    Assert(CPUMIsGuestVmxInterceptEvents(pCtx));

    /*
     * Interrupt shadows MAY block NMIs.
     * They also blocks external-interrupts and MAY block external-interrupt VM-exits.
     *
     * See Intel spec. 24.4.2 "Guest Non-Register State".
     * See Intel spec. 25.4.1 "Event Blocking".
     */
    if (!CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx))
    { /* likely */ }
    else
        return VINF_SUCCESS;

    /** @todo SMI. SMIs take priority over NMIs. */

    /*
     * NMIs.
     * NMIs take priority over interrupts.
     */
    if (!CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx))
    {
        /*
         * Nested-guest NMI-window exiting.
         * The NMI-window exit must happen regardless of whether an NMI is pending
         * provided virtual-NMI blocking is not in effect.
         *
         * See Intel spec. 25.2 "Other Causes Of VM Exits".
         */
        if (    VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_NMI_WINDOW)
            && !CPUMIsGuestVmxVirtNmiBlocking(pCtx))
        {
            Assert(CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_NMI_WINDOW_EXIT));
            return IEMExecVmxVmexit(pVCpu, VMX_EXIT_NMI_WINDOW, 0 /* u64ExitQual */);
        }

        /*
         * For a nested-guest, the FF always indicates the outer guest's ability to
         * receive an NMI while the guest-interruptibility state bit depends on whether
         * the nested-hypervisor is using virtual-NMIs.
         *
         * It is very important that we also clear the force-flag if we are causing
         * an NMI VM-exit as it is the responsibility of the nested-hypervisor to deal
         * with re-injecting or discarding the NMI. This fixes the bug that showed up
         * with SMP Windows Server 2008 R2 with Hyper-V enabled, see @bugref{10318#c19}.
         */
        if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NMI))
        {
            if (CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_NMI_EXIT))
                return IEMExecVmxVmexitXcptNmi(pVCpu);
            vmxHCSetPendingXcptNmi(pVCpu);
            return VINF_SUCCESS;
        }
    }

    /*
     * Nested-guest interrupt-window exiting.
     *
     * We must cause the interrupt-window exit regardless of whether an interrupt is pending
     * provided virtual interrupts are enabled.
     *
     * See Intel spec. 25.2 "Other Causes Of VM Exits".
     * See Intel spec. 26.7.5 "Interrupt-Window Exiting and Virtual-Interrupt Delivery".
     */
    if (   VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_INT_WINDOW)
        && CPUMIsGuestVmxVirtIntrEnabled(pCtx))
    {
        Assert(CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_INT_WINDOW_EXIT));
        return IEMExecVmxVmexit(pVCpu, VMX_EXIT_INT_WINDOW, 0 /* u64ExitQual */);
    }

    /*
     * External interrupts (PIC/APIC).
     *
     * When "External interrupt exiting" is set the VM-exit happens regardless of RFLAGS.IF.
     * When it isn't set, RFLAGS.IF controls delivery of the interrupt as always.
     * This fixes a nasty SMP hang while executing nested-guest VCPUs on spinlocks which aren't rescued
     * by other VM-exits (like a preemption timer), see @bugref{9562#c18}.
     *
     * NMIs block external interrupts as they are dispatched through the interrupt gate (vector 2)
     * which automatically clears EFLAGS.IF. Also it's possible an NMI handler could enable interrupts
     * and thus we should not check for NMI inhibition here.
     *
     * See Intel spec. 25.4.1 "Event Blocking".
     * See Intel spec. 6.8.1 "Masking Maskable Hardware Interrupts".
     */
    if (    VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)
        && !VCPU_2_VMXSTATE(pVCpu).fSingleInstruction)
    {
        Assert(!DBGFIsStepping(pVCpu));
        int rc = vmxHCImportGuestStateEx(pVCpu, pVmcsInfo, CPUMCTX_EXTRN_RFLAGS);
        AssertRC(rc);
        if (CPUMIsGuestVmxPhysIntrEnabled(pCtx))
        {
            /* Nested-guest external interrupt VM-exit. */
            if (    CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_EXT_INT_EXIT)
                && !CPUMIsGuestVmxExitCtlsSet(pCtx, VMX_EXIT_CTLS_ACK_EXT_INT))
            {
                VBOXSTRICTRC rcStrict = IEMExecVmxVmexitExtInt(pVCpu, 0 /* uVector */, true /* fIntPending */);
                Assert(rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE);
                return rcStrict;
            }

            /*
             * Fetch the external interrupt from the interrupt controller.
             * Once PDMGetInterrupt() returns an interrupt we -must- deliver it or pass it to
             * the nested-hypervisor. We cannot re-request the interrupt from the controller again.
             */
            uint8_t u8Interrupt;
            rc = PDMGetInterrupt(pVCpu, &u8Interrupt);
            if (RT_SUCCESS(rc))
            {
                /* Nested-guest external interrupt VM-exit when the "acknowledge interrupt on exit" is enabled. */
                if (CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_EXT_INT_EXIT))
                {
                    Assert(CPUMIsGuestVmxExitCtlsSet(pCtx, VMX_EXIT_CTLS_ACK_EXT_INT));
                    VBOXSTRICTRC rcStrict = IEMExecVmxVmexitExtInt(pVCpu, u8Interrupt, false /* fIntPending */);
                    Assert(rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE);
                    return rcStrict;
                }
                vmxHCSetPendingExtInt(pVCpu, u8Interrupt);
                return VINF_SUCCESS;
            }
        }
    }
    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */


/**
 * Injects any pending events into the guest if the guest is in a state to
 * receive them.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmcsInfo       The VMCS information structure.
 * @param   fIsNestedGuest  Flag whether the event injection happens for a nested guest.
 * @param   fIntrState      The VT-x guest-interruptibility state.
 * @param   fStepping       Whether we are single-stepping the guest using the
 *                          hypervisor debugger and should return
 *                          VINF_EM_DBG_STEPPED if the event was dispatched
 *                          directly.
 */
static VBOXSTRICTRC vmxHCInjectPendingEvent(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, bool fIsNestedGuest,
                                            uint32_t fIntrState, bool fStepping)
{
    HMVMX_ASSERT_PREEMPT_SAFE(pVCpu);
#ifndef IN_NEM_DARWIN
    Assert(VMMRZCallRing3IsEnabled(pVCpu));
#endif

#ifdef VBOX_STRICT
    /*
     * Verify guest-interruptibility state.
     *
     * We put this in a scoped block so we do not accidentally use fBlockSti or fBlockMovSS,
     * since injecting an event may modify the interruptibility state and we must thus always
     * use fIntrState.
     */
    {
        bool const fBlockMovSS = RT_BOOL(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS);
        bool const fBlockSti   = RT_BOOL(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI);
        Assert(!fBlockSti || !(ASMAtomicUoReadU64(&pVCpu->cpum.GstCtx.fExtrn) & CPUMCTX_EXTRN_RFLAGS));
        Assert(!fBlockSti || pVCpu->cpum.GstCtx.eflags.Bits.u1IF);     /* Cannot set block-by-STI when interrupts are disabled. */
        Assert(!(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_SMI));    /* We don't support block-by-SMI yet.*/
        Assert(!TRPMHasTrap(pVCpu));
        NOREF(fBlockMovSS); NOREF(fBlockSti);
    }
#endif

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    if (VCPU_2_VMXSTATE(pVCpu).Event.fPending)
    {
        /*
         * Do -not- clear any interrupt-window exiting control here. We might have an interrupt
         * pending even while injecting an event and in this case, we want a VM-exit as soon as
         * the guest is ready for the next interrupt, see @bugref{6208#c45}.
         *
         * See Intel spec. 26.6.5 "Interrupt-Window Exiting and Virtual-Interrupt Delivery".
         */
        uint32_t const uIntType = VMX_ENTRY_INT_INFO_TYPE(VCPU_2_VMXSTATE(pVCpu).Event.u64IntInfo);
#ifdef VBOX_STRICT
        if (uIntType == VMX_ENTRY_INT_INFO_TYPE_EXT_INT)
        {
            Assert(pVCpu->cpum.GstCtx.eflags.u & X86_EFL_IF);
            Assert(!(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI));
            Assert(!(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS));
        }
        else if (uIntType == VMX_ENTRY_INT_INFO_TYPE_NMI)
        {
            Assert(!(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_NMI));
            Assert(!(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI));
            Assert(!(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS));
        }
#endif
        Log4(("Injecting pending event vcpu[%RU32] u64IntInfo=%#RX64 Type=%#RX32\n", pVCpu->idCpu, VCPU_2_VMXSTATE(pVCpu).Event.u64IntInfo,
              uIntType));

        /*
         * Inject the event and get any changes to the guest-interruptibility state.
         *
         * The guest-interruptibility state may need to be updated if we inject the event
         * into the guest IDT ourselves (for real-on-v86 guest injecting software interrupts).
         */
        rcStrict = vmxHCInjectEventVmcs(pVCpu, pVmcsInfo, fIsNestedGuest, &VCPU_2_VMXSTATE(pVCpu).Event, fStepping, &fIntrState);
        AssertRCReturn(VBOXSTRICTRC_VAL(rcStrict), rcStrict);

        if (uIntType == VMX_ENTRY_INT_INFO_TYPE_EXT_INT)
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatInjectInterrupt);
        else
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatInjectXcpt);
    }

    /*
     * Deliver any pending debug exceptions if the guest is single-stepping using EFLAGS.TF and
     * is an interrupt shadow (block-by-STI or block-by-MOV SS).
     */
    if (   (fIntrState & (VMX_VMCS_GUEST_INT_STATE_BLOCK_STI | VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS))
        && !fIsNestedGuest)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_RFLAGS);

        if (!VCPU_2_VMXSTATE(pVCpu).fSingleInstruction)
        {
            /*
             * Set or clear the BS bit depending on whether the trap flag is active or not. We need
             * to do both since we clear the BS bit from the VMCS while exiting to ring-3.
             */
            Assert(!DBGFIsStepping(pVCpu));
            uint8_t const fTrapFlag = !!(pVCpu->cpum.GstCtx.eflags.u & X86_EFL_TF);
            int rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_PENDING_DEBUG_XCPTS,
                                       fTrapFlag << VMX_BF_VMCS_PENDING_DBG_XCPT_BS_SHIFT);
            AssertRC(rc);
        }
        else
        {
            /*
             * We must not deliver a debug exception when single-stepping over STI/Mov-SS in the
             * hypervisor debugger using EFLAGS.TF but rather clear interrupt inhibition. However,
             * we take care of this case in vmxHCExportSharedDebugState and also the case if
             * we use MTF, so just make sure it's called before executing guest-code.
             */
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_DR_MASK);
        }
    }
    /* else: for nested-guest currently handling while merging controls. */

    /*
     * Finally, update the guest-interruptibility state.
     *
     * This is required for the real-on-v86 software interrupt injection, for
     * pending debug exceptions as well as updates to the guest state from ring-3 (IEM).
     */
    int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_INT_STATE, fIntrState);
    AssertRC(rc);

    /*
     * There's no need to clear the VM-entry interruption-information field here if we're not
     * injecting anything. VT-x clears the valid bit on every VM-exit.
     *
     * See Intel spec. 24.8.3 "VM-Entry Controls for Event Injection".
     */

    Assert(rcStrict == VINF_SUCCESS || rcStrict == VINF_EM_RESET || (rcStrict == VINF_EM_DBG_STEPPED && fStepping));
    return rcStrict;
}


/**
 * Tries to determine what part of the guest-state VT-x has deemed as invalid
 * and update error record fields accordingly.
 *
 * @returns VMX_IGS_* error codes.
 * @retval VMX_IGS_REASON_NOT_FOUND if this function could not find anything
 *         wrong with the guest state.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcsInfo   The VMCS info. object.
 *
 * @remarks This function assumes our cache of the VMCS controls
 *          are valid, i.e. vmxHCCheckCachedVmcsCtls() succeeded.
 */
static uint32_t vmxHCCheckGuestState(PVMCPUCC pVCpu, PCVMXVMCSINFO pVmcsInfo)
{
#define HMVMX_ERROR_BREAK(err)              { uError = (err); break; }
#define HMVMX_CHECK_BREAK(expr, err)        if (!(expr)) { uError = (err); break; } else do { } while (0)

    PCPUMCTX pCtx   = &pVCpu->cpum.GstCtx;
    uint32_t uError = VMX_IGS_ERROR;
    uint32_t u32IntrState = 0;
#ifndef IN_NEM_DARWIN
    PVMCC    pVM    = pVCpu->CTX_SUFF(pVM);
    bool const fUnrestrictedGuest = VM_IS_VMX_UNRESTRICTED_GUEST(pVM);
#else
    bool const fUnrestrictedGuest = true;
#endif
    do
    {
        int rc;

        /*
         * Guest-interruptibility state.
         *
         * Read this first so that any check that fails prior to those that actually
         * require the guest-interruptibility state would still reflect the correct
         * VMCS value and avoids causing further confusion.
         */
        rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_INT_STATE, &u32IntrState);
        AssertRC(rc);

        uint32_t u32Val;
        uint64_t u64Val;

        /*
         * CR0.
         */
        /** @todo Why do we need to OR and AND the fixed-0 and fixed-1 bits below? */
        uint64_t       fSetCr0 = (g_HmMsrs.u.vmx.u64Cr0Fixed0 & g_HmMsrs.u.vmx.u64Cr0Fixed1);
        uint64_t const fZapCr0 = (g_HmMsrs.u.vmx.u64Cr0Fixed0 | g_HmMsrs.u.vmx.u64Cr0Fixed1);
        /* Exceptions for unrestricted guest execution for CR0 fixed bits (PE, PG).
           See Intel spec. 26.3.1 "Checks on Guest Control Registers, Debug Registers and MSRs." */
        if (fUnrestrictedGuest)
            fSetCr0 &= ~(uint64_t)(X86_CR0_PE | X86_CR0_PG);

        uint64_t u64GuestCr0;
        rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_CR0, &u64GuestCr0);
        AssertRC(rc);
        HMVMX_CHECK_BREAK((u64GuestCr0 & fSetCr0) == fSetCr0, VMX_IGS_CR0_FIXED1);
        HMVMX_CHECK_BREAK(!(u64GuestCr0 & ~fZapCr0), VMX_IGS_CR0_FIXED0);
        if (   !fUnrestrictedGuest
            &&  (u64GuestCr0 & X86_CR0_PG)
            && !(u64GuestCr0 & X86_CR0_PE))
            HMVMX_ERROR_BREAK(VMX_IGS_CR0_PG_PE_COMBO);

        /*
         * CR4.
         */
        /** @todo Why do we need to OR and AND the fixed-0 and fixed-1 bits below? */
        uint64_t const fSetCr4 = (g_HmMsrs.u.vmx.u64Cr4Fixed0 & g_HmMsrs.u.vmx.u64Cr4Fixed1);
        uint64_t const fZapCr4 = (g_HmMsrs.u.vmx.u64Cr4Fixed0 | g_HmMsrs.u.vmx.u64Cr4Fixed1);

        uint64_t u64GuestCr4;
        rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_CR4, &u64GuestCr4);
        AssertRC(rc);
        HMVMX_CHECK_BREAK((u64GuestCr4 & fSetCr4) == fSetCr4, VMX_IGS_CR4_FIXED1);
        HMVMX_CHECK_BREAK(!(u64GuestCr4 & ~fZapCr4), VMX_IGS_CR4_FIXED0);

        /*
         * IA32_DEBUGCTL MSR.
         */
        rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_DEBUGCTL_FULL, &u64Val);
        AssertRC(rc);
        if (   (pVmcsInfo->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_DEBUG)
            && (u64Val & 0xfffffe3c))                           /* Bits 31:9, bits 5:2 MBZ. */
        {
            HMVMX_ERROR_BREAK(VMX_IGS_DEBUGCTL_MSR_RESERVED);
        }
        uint64_t u64DebugCtlMsr = u64Val;

#ifdef VBOX_STRICT
        rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_ENTRY, &u32Val);
        AssertRC(rc);
        Assert(u32Val == pVmcsInfo->u32EntryCtls);
#endif
        bool const fLongModeGuest = RT_BOOL(pVmcsInfo->u32EntryCtls & VMX_ENTRY_CTLS_IA32E_MODE_GUEST);

        /*
         * RIP and RFLAGS.
         */
        rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_RIP, &u64Val);
        AssertRC(rc);
        /* pCtx->rip can be different than the one in the VMCS (e.g. run guest code and VM-exits that don't update it). */
        if (   !fLongModeGuest
            || !pCtx->cs.Attr.n.u1Long)
        {
            HMVMX_CHECK_BREAK(!(u64Val & UINT64_C(0xffffffff00000000)), VMX_IGS_LONGMODE_RIP_INVALID);
        }
        /** @todo If the processor supports N < 64 linear-address bits, bits 63:N
         *        must be identical if the "IA-32e mode guest" VM-entry
         *        control is 1 and CS.L is 1. No check applies if the
         *        CPU supports 64 linear-address bits. */

        /* Flags in pCtx can be different (real-on-v86 for instance). We are only concerned about the VMCS contents here. */
        rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_RFLAGS, &u64Val);
        AssertRC(rc);
        HMVMX_CHECK_BREAK(!(u64Val & UINT64_C(0xffffffffffc08028)),                     /* Bit 63:22, Bit 15, 5, 3 MBZ. */
                          VMX_IGS_RFLAGS_RESERVED);
        HMVMX_CHECK_BREAK((u64Val & X86_EFL_RA1_MASK), VMX_IGS_RFLAGS_RESERVED1);       /* Bit 1 MB1. */
        uint32_t const u32Eflags = u64Val;

        if (   fLongModeGuest
            || (   fUnrestrictedGuest
                && !(u64GuestCr0 & X86_CR0_PE)))
        {
            HMVMX_CHECK_BREAK(!(u32Eflags & X86_EFL_VM), VMX_IGS_RFLAGS_VM_INVALID);
        }

        uint32_t u32EntryInfo;
        rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_CTRL_ENTRY_INTERRUPTION_INFO, &u32EntryInfo);
        AssertRC(rc);
        if (VMX_ENTRY_INT_INFO_IS_EXT_INT(u32EntryInfo))
        {
            HMVMX_CHECK_BREAK(u32Eflags & X86_EFL_IF, VMX_IGS_RFLAGS_IF_INVALID);
        }

        /*
         * 64-bit checks.
         */
        if (fLongModeGuest)
        {
            HMVMX_CHECK_BREAK(u64GuestCr0 & X86_CR0_PG,  VMX_IGS_CR0_PG_LONGMODE);
            HMVMX_CHECK_BREAK(u64GuestCr4 & X86_CR4_PAE, VMX_IGS_CR4_PAE_LONGMODE);
        }

        if (   !fLongModeGuest
            && (u64GuestCr4 & X86_CR4_PCIDE))
            HMVMX_ERROR_BREAK(VMX_IGS_CR4_PCIDE);

        /** @todo CR3 field must be such that bits 63:52 and bits in the range
         *        51:32 beyond the processor's physical-address width are 0. */

        if (   (pVmcsInfo->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_DEBUG)
            && (pCtx->dr[7] & X86_DR7_MBZ_MASK))
            HMVMX_ERROR_BREAK(VMX_IGS_DR7_RESERVED);

#ifndef IN_NEM_DARWIN
        rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_HOST_SYSENTER_ESP, &u64Val);
        AssertRC(rc);
        HMVMX_CHECK_BREAK(X86_IS_CANONICAL(u64Val), VMX_IGS_SYSENTER_ESP_NOT_CANONICAL);

        rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_HOST_SYSENTER_EIP, &u64Val);
        AssertRC(rc);
        HMVMX_CHECK_BREAK(X86_IS_CANONICAL(u64Val), VMX_IGS_SYSENTER_EIP_NOT_CANONICAL);
#endif

        /*
         * PERF_GLOBAL MSR.
         */
        if (pVmcsInfo->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_PERF_MSR)
        {
            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PERF_GLOBAL_CTRL_FULL, &u64Val);
            AssertRC(rc);
            HMVMX_CHECK_BREAK(!(u64Val & UINT64_C(0xfffffff8fffffffc)),
                              VMX_IGS_PERF_GLOBAL_MSR_RESERVED);        /* Bits 63:35, bits 31:2 MBZ. */
        }

        /*
         * PAT MSR.
         */
        if (pVmcsInfo->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_PAT_MSR)
        {
            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PAT_FULL, &u64Val);
            AssertRC(rc);
            HMVMX_CHECK_BREAK(!(u64Val & UINT64_C(0x707070707070707)), VMX_IGS_PAT_MSR_RESERVED);
            for (unsigned i = 0; i < 8; i++)
            {
                uint8_t u8Val = (u64Val & 0xff);
                if (   u8Val > MSR_IA32_PAT_MT_UCD
                    || u8Val == MSR_IA32_PAT_MT_RSVD_2
                    || u8Val == MSR_IA32_PAT_MT_RSVD_3)
                    HMVMX_ERROR_BREAK(VMX_IGS_PAT_MSR_INVALID);
                u64Val >>= 8;
            }
        }

        /*
         * EFER MSR.
         */
        if (pVmcsInfo->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_EFER_MSR)
        {
            Assert(g_fHmVmxSupportsVmcsEfer);
            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_EFER_FULL, &u64Val);
            AssertRC(rc);
            HMVMX_CHECK_BREAK(!(u64Val & UINT64_C(0xfffffffffffff2fe)),
                              VMX_IGS_EFER_MSR_RESERVED);               /* Bits 63:12, bit 9, bits 7:1 MBZ. */
            HMVMX_CHECK_BREAK(RT_BOOL(u64Val & MSR_K6_EFER_LMA) == RT_BOOL(  pVmcsInfo->u32EntryCtls
                                                                           & VMX_ENTRY_CTLS_IA32E_MODE_GUEST),
                              VMX_IGS_EFER_LMA_GUEST_MODE_MISMATCH);
            /** @todo r=ramshankar: Unrestricted check here is probably wrong, see
             *        iemVmxVmentryCheckGuestState(). */
            HMVMX_CHECK_BREAK(   fUnrestrictedGuest
                              || !(u64GuestCr0 & X86_CR0_PG)
                              || RT_BOOL(u64Val & MSR_K6_EFER_LMA) == RT_BOOL(u64Val & MSR_K6_EFER_LME),
                              VMX_IGS_EFER_LMA_LME_MISMATCH);
        }

        /*
         * Segment registers.
         */
        HMVMX_CHECK_BREAK(   (pCtx->ldtr.Attr.u & X86DESCATTR_UNUSABLE)
                          || !(pCtx->ldtr.Sel & X86_SEL_LDT), VMX_IGS_LDTR_TI_INVALID);
        if (!(u32Eflags & X86_EFL_VM))
        {
            /* CS */
            HMVMX_CHECK_BREAK(pCtx->cs.Attr.n.u1Present, VMX_IGS_CS_ATTR_P_INVALID);
            HMVMX_CHECK_BREAK(!(pCtx->cs.Attr.u & 0xf00), VMX_IGS_CS_ATTR_RESERVED);
            HMVMX_CHECK_BREAK(!(pCtx->cs.Attr.u & 0xfffe0000), VMX_IGS_CS_ATTR_RESERVED);
            HMVMX_CHECK_BREAK(   (pCtx->cs.u32Limit & 0xfff) == 0xfff
                              || !(pCtx->cs.Attr.n.u1Granularity), VMX_IGS_CS_ATTR_G_INVALID);
            HMVMX_CHECK_BREAK(   !(pCtx->cs.u32Limit & 0xfff00000)
                              || (pCtx->cs.Attr.n.u1Granularity), VMX_IGS_CS_ATTR_G_INVALID);
            /* CS cannot be loaded with NULL in protected mode. */
            HMVMX_CHECK_BREAK(pCtx->cs.Attr.u && !(pCtx->cs.Attr.u & X86DESCATTR_UNUSABLE), VMX_IGS_CS_ATTR_UNUSABLE);
            HMVMX_CHECK_BREAK(pCtx->cs.Attr.n.u1DescType, VMX_IGS_CS_ATTR_S_INVALID);
            if (pCtx->cs.Attr.n.u4Type == 9 || pCtx->cs.Attr.n.u4Type == 11)
                HMVMX_CHECK_BREAK(pCtx->cs.Attr.n.u2Dpl == pCtx->ss.Attr.n.u2Dpl, VMX_IGS_CS_SS_ATTR_DPL_UNEQUAL);
            else if (pCtx->cs.Attr.n.u4Type == 13 || pCtx->cs.Attr.n.u4Type == 15)
                HMVMX_CHECK_BREAK(pCtx->cs.Attr.n.u2Dpl <= pCtx->ss.Attr.n.u2Dpl, VMX_IGS_CS_SS_ATTR_DPL_MISMATCH);
            else if (fUnrestrictedGuest && pCtx->cs.Attr.n.u4Type == 3)
                HMVMX_CHECK_BREAK(pCtx->cs.Attr.n.u2Dpl == 0, VMX_IGS_CS_ATTR_DPL_INVALID);
            else
                HMVMX_ERROR_BREAK(VMX_IGS_CS_ATTR_TYPE_INVALID);

            /* SS */
            HMVMX_CHECK_BREAK(   fUnrestrictedGuest
                              || (pCtx->ss.Sel & X86_SEL_RPL) == (pCtx->cs.Sel & X86_SEL_RPL), VMX_IGS_SS_CS_RPL_UNEQUAL);
            HMVMX_CHECK_BREAK(pCtx->ss.Attr.n.u2Dpl == (pCtx->ss.Sel & X86_SEL_RPL), VMX_IGS_SS_ATTR_DPL_RPL_UNEQUAL);
            if (   !(pCtx->cr0 & X86_CR0_PE)
                || pCtx->cs.Attr.n.u4Type == 3)
            {
                HMVMX_CHECK_BREAK(!pCtx->ss.Attr.n.u2Dpl, VMX_IGS_SS_ATTR_DPL_INVALID);
            }

            if (!(pCtx->ss.Attr.u & X86DESCATTR_UNUSABLE))
            {
                HMVMX_CHECK_BREAK(pCtx->ss.Attr.n.u4Type == 3 || pCtx->ss.Attr.n.u4Type == 7, VMX_IGS_SS_ATTR_TYPE_INVALID);
                HMVMX_CHECK_BREAK(pCtx->ss.Attr.n.u1Present, VMX_IGS_SS_ATTR_P_INVALID);
                HMVMX_CHECK_BREAK(!(pCtx->ss.Attr.u & 0xf00), VMX_IGS_SS_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(!(pCtx->ss.Attr.u & 0xfffe0000), VMX_IGS_SS_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(   (pCtx->ss.u32Limit & 0xfff) == 0xfff
                                  || !(pCtx->ss.Attr.n.u1Granularity), VMX_IGS_SS_ATTR_G_INVALID);
                HMVMX_CHECK_BREAK(   !(pCtx->ss.u32Limit & 0xfff00000)
                                  || (pCtx->ss.Attr.n.u1Granularity), VMX_IGS_SS_ATTR_G_INVALID);
            }

            /* DS, ES, FS, GS - only check for usable selectors, see vmxHCExportGuestSReg(). */
            if (!(pCtx->ds.Attr.u & X86DESCATTR_UNUSABLE))
            {
                HMVMX_CHECK_BREAK(pCtx->ds.Attr.n.u4Type & X86_SEL_TYPE_ACCESSED, VMX_IGS_DS_ATTR_A_INVALID);
                HMVMX_CHECK_BREAK(pCtx->ds.Attr.n.u1Present, VMX_IGS_DS_ATTR_P_INVALID);
                HMVMX_CHECK_BREAK(   fUnrestrictedGuest
                                  || pCtx->ds.Attr.n.u4Type > 11
                                  || pCtx->ds.Attr.n.u2Dpl >= (pCtx->ds.Sel & X86_SEL_RPL), VMX_IGS_DS_ATTR_DPL_RPL_UNEQUAL);
                HMVMX_CHECK_BREAK(!(pCtx->ds.Attr.u & 0xf00), VMX_IGS_DS_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(!(pCtx->ds.Attr.u & 0xfffe0000), VMX_IGS_DS_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(   (pCtx->ds.u32Limit & 0xfff) == 0xfff
                                  || !(pCtx->ds.Attr.n.u1Granularity), VMX_IGS_DS_ATTR_G_INVALID);
                HMVMX_CHECK_BREAK(   !(pCtx->ds.u32Limit & 0xfff00000)
                                  || (pCtx->ds.Attr.n.u1Granularity), VMX_IGS_DS_ATTR_G_INVALID);
                HMVMX_CHECK_BREAK(   !(pCtx->ds.Attr.n.u4Type & X86_SEL_TYPE_CODE)
                                  || (pCtx->ds.Attr.n.u4Type & X86_SEL_TYPE_READ), VMX_IGS_DS_ATTR_TYPE_INVALID);
            }
            if (!(pCtx->es.Attr.u & X86DESCATTR_UNUSABLE))
            {
                HMVMX_CHECK_BREAK(pCtx->es.Attr.n.u4Type & X86_SEL_TYPE_ACCESSED, VMX_IGS_ES_ATTR_A_INVALID);
                HMVMX_CHECK_BREAK(pCtx->es.Attr.n.u1Present, VMX_IGS_ES_ATTR_P_INVALID);
                HMVMX_CHECK_BREAK(   fUnrestrictedGuest
                                  || pCtx->es.Attr.n.u4Type > 11
                                  || pCtx->es.Attr.n.u2Dpl >= (pCtx->es.Sel & X86_SEL_RPL), VMX_IGS_DS_ATTR_DPL_RPL_UNEQUAL);
                HMVMX_CHECK_BREAK(!(pCtx->es.Attr.u & 0xf00), VMX_IGS_ES_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(!(pCtx->es.Attr.u & 0xfffe0000), VMX_IGS_ES_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(   (pCtx->es.u32Limit & 0xfff) == 0xfff
                                  || !(pCtx->es.Attr.n.u1Granularity), VMX_IGS_ES_ATTR_G_INVALID);
                HMVMX_CHECK_BREAK(   !(pCtx->es.u32Limit & 0xfff00000)
                                  || (pCtx->es.Attr.n.u1Granularity), VMX_IGS_ES_ATTR_G_INVALID);
                HMVMX_CHECK_BREAK(   !(pCtx->es.Attr.n.u4Type & X86_SEL_TYPE_CODE)
                                  || (pCtx->es.Attr.n.u4Type & X86_SEL_TYPE_READ), VMX_IGS_ES_ATTR_TYPE_INVALID);
            }
            if (!(pCtx->fs.Attr.u & X86DESCATTR_UNUSABLE))
            {
                HMVMX_CHECK_BREAK(pCtx->fs.Attr.n.u4Type & X86_SEL_TYPE_ACCESSED, VMX_IGS_FS_ATTR_A_INVALID);
                HMVMX_CHECK_BREAK(pCtx->fs.Attr.n.u1Present, VMX_IGS_FS_ATTR_P_INVALID);
                HMVMX_CHECK_BREAK(   fUnrestrictedGuest
                                  || pCtx->fs.Attr.n.u4Type > 11
                                  || pCtx->fs.Attr.n.u2Dpl >= (pCtx->fs.Sel & X86_SEL_RPL), VMX_IGS_FS_ATTR_DPL_RPL_UNEQUAL);
                HMVMX_CHECK_BREAK(!(pCtx->fs.Attr.u & 0xf00), VMX_IGS_FS_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(!(pCtx->fs.Attr.u & 0xfffe0000), VMX_IGS_FS_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(   (pCtx->fs.u32Limit & 0xfff) == 0xfff
                                  || !(pCtx->fs.Attr.n.u1Granularity), VMX_IGS_FS_ATTR_G_INVALID);
                HMVMX_CHECK_BREAK(   !(pCtx->fs.u32Limit & 0xfff00000)
                                  || (pCtx->fs.Attr.n.u1Granularity), VMX_IGS_FS_ATTR_G_INVALID);
                HMVMX_CHECK_BREAK(   !(pCtx->fs.Attr.n.u4Type & X86_SEL_TYPE_CODE)
                                  || (pCtx->fs.Attr.n.u4Type & X86_SEL_TYPE_READ), VMX_IGS_FS_ATTR_TYPE_INVALID);
            }
            if (!(pCtx->gs.Attr.u & X86DESCATTR_UNUSABLE))
            {
                HMVMX_CHECK_BREAK(pCtx->gs.Attr.n.u4Type & X86_SEL_TYPE_ACCESSED, VMX_IGS_GS_ATTR_A_INVALID);
                HMVMX_CHECK_BREAK(pCtx->gs.Attr.n.u1Present, VMX_IGS_GS_ATTR_P_INVALID);
                HMVMX_CHECK_BREAK(   fUnrestrictedGuest
                                  || pCtx->gs.Attr.n.u4Type > 11
                                  || pCtx->gs.Attr.n.u2Dpl >= (pCtx->gs.Sel & X86_SEL_RPL), VMX_IGS_GS_ATTR_DPL_RPL_UNEQUAL);
                HMVMX_CHECK_BREAK(!(pCtx->gs.Attr.u & 0xf00), VMX_IGS_GS_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(!(pCtx->gs.Attr.u & 0xfffe0000), VMX_IGS_GS_ATTR_RESERVED);
                HMVMX_CHECK_BREAK(   (pCtx->gs.u32Limit & 0xfff) == 0xfff
                                  || !(pCtx->gs.Attr.n.u1Granularity), VMX_IGS_GS_ATTR_G_INVALID);
                HMVMX_CHECK_BREAK(   !(pCtx->gs.u32Limit & 0xfff00000)
                                  || (pCtx->gs.Attr.n.u1Granularity), VMX_IGS_GS_ATTR_G_INVALID);
                HMVMX_CHECK_BREAK(   !(pCtx->gs.Attr.n.u4Type & X86_SEL_TYPE_CODE)
                                  || (pCtx->gs.Attr.n.u4Type & X86_SEL_TYPE_READ), VMX_IGS_GS_ATTR_TYPE_INVALID);
            }
            /* 64-bit capable CPUs. */
            HMVMX_CHECK_BREAK(X86_IS_CANONICAL(pCtx->fs.u64Base), VMX_IGS_FS_BASE_NOT_CANONICAL);
            HMVMX_CHECK_BREAK(X86_IS_CANONICAL(pCtx->gs.u64Base), VMX_IGS_GS_BASE_NOT_CANONICAL);
            HMVMX_CHECK_BREAK(   (pCtx->ldtr.Attr.u & X86DESCATTR_UNUSABLE)
                              || X86_IS_CANONICAL(pCtx->ldtr.u64Base), VMX_IGS_LDTR_BASE_NOT_CANONICAL);
            HMVMX_CHECK_BREAK(!RT_HI_U32(pCtx->cs.u64Base), VMX_IGS_LONGMODE_CS_BASE_INVALID);
            HMVMX_CHECK_BREAK((pCtx->ss.Attr.u & X86DESCATTR_UNUSABLE) || !RT_HI_U32(pCtx->ss.u64Base),
                              VMX_IGS_LONGMODE_SS_BASE_INVALID);
            HMVMX_CHECK_BREAK((pCtx->ds.Attr.u & X86DESCATTR_UNUSABLE) || !RT_HI_U32(pCtx->ds.u64Base),
                              VMX_IGS_LONGMODE_DS_BASE_INVALID);
            HMVMX_CHECK_BREAK((pCtx->es.Attr.u & X86DESCATTR_UNUSABLE) || !RT_HI_U32(pCtx->es.u64Base),
                              VMX_IGS_LONGMODE_ES_BASE_INVALID);
        }
        else
        {
            /* V86 mode checks. */
            uint32_t u32CSAttr, u32SSAttr, u32DSAttr, u32ESAttr, u32FSAttr, u32GSAttr;
            if (pVmcsInfo->pShared->RealMode.fRealOnV86Active)
            {
                u32CSAttr = 0xf3;   u32SSAttr = 0xf3;
                u32DSAttr = 0xf3;   u32ESAttr = 0xf3;
                u32FSAttr = 0xf3;   u32GSAttr = 0xf3;
            }
            else
            {
                u32CSAttr = pCtx->cs.Attr.u;   u32SSAttr = pCtx->ss.Attr.u;
                u32DSAttr = pCtx->ds.Attr.u;   u32ESAttr = pCtx->es.Attr.u;
                u32FSAttr = pCtx->fs.Attr.u;   u32GSAttr = pCtx->gs.Attr.u;
            }

            /* CS */
            HMVMX_CHECK_BREAK((pCtx->cs.u64Base == (uint64_t)pCtx->cs.Sel << 4), VMX_IGS_V86_CS_BASE_INVALID);
            HMVMX_CHECK_BREAK(pCtx->cs.u32Limit == 0xffff, VMX_IGS_V86_CS_LIMIT_INVALID);
            HMVMX_CHECK_BREAK(u32CSAttr == 0xf3, VMX_IGS_V86_CS_ATTR_INVALID);
            /* SS */
            HMVMX_CHECK_BREAK((pCtx->ss.u64Base == (uint64_t)pCtx->ss.Sel << 4), VMX_IGS_V86_SS_BASE_INVALID);
            HMVMX_CHECK_BREAK(pCtx->ss.u32Limit == 0xffff, VMX_IGS_V86_SS_LIMIT_INVALID);
            HMVMX_CHECK_BREAK(u32SSAttr == 0xf3, VMX_IGS_V86_SS_ATTR_INVALID);
            /* DS */
            HMVMX_CHECK_BREAK((pCtx->ds.u64Base == (uint64_t)pCtx->ds.Sel << 4), VMX_IGS_V86_DS_BASE_INVALID);
            HMVMX_CHECK_BREAK(pCtx->ds.u32Limit == 0xffff, VMX_IGS_V86_DS_LIMIT_INVALID);
            HMVMX_CHECK_BREAK(u32DSAttr == 0xf3, VMX_IGS_V86_DS_ATTR_INVALID);
            /* ES */
            HMVMX_CHECK_BREAK((pCtx->es.u64Base == (uint64_t)pCtx->es.Sel << 4), VMX_IGS_V86_ES_BASE_INVALID);
            HMVMX_CHECK_BREAK(pCtx->es.u32Limit == 0xffff, VMX_IGS_V86_ES_LIMIT_INVALID);
            HMVMX_CHECK_BREAK(u32ESAttr == 0xf3, VMX_IGS_V86_ES_ATTR_INVALID);
            /* FS */
            HMVMX_CHECK_BREAK((pCtx->fs.u64Base == (uint64_t)pCtx->fs.Sel << 4), VMX_IGS_V86_FS_BASE_INVALID);
            HMVMX_CHECK_BREAK(pCtx->fs.u32Limit == 0xffff, VMX_IGS_V86_FS_LIMIT_INVALID);
            HMVMX_CHECK_BREAK(u32FSAttr == 0xf3, VMX_IGS_V86_FS_ATTR_INVALID);
            /* GS */
            HMVMX_CHECK_BREAK((pCtx->gs.u64Base == (uint64_t)pCtx->gs.Sel << 4), VMX_IGS_V86_GS_BASE_INVALID);
            HMVMX_CHECK_BREAK(pCtx->gs.u32Limit == 0xffff, VMX_IGS_V86_GS_LIMIT_INVALID);
            HMVMX_CHECK_BREAK(u32GSAttr == 0xf3, VMX_IGS_V86_GS_ATTR_INVALID);
            /* 64-bit capable CPUs. */
            HMVMX_CHECK_BREAK(X86_IS_CANONICAL(pCtx->fs.u64Base), VMX_IGS_FS_BASE_NOT_CANONICAL);
            HMVMX_CHECK_BREAK(X86_IS_CANONICAL(pCtx->gs.u64Base), VMX_IGS_GS_BASE_NOT_CANONICAL);
            HMVMX_CHECK_BREAK(   (pCtx->ldtr.Attr.u & X86DESCATTR_UNUSABLE)
                              || X86_IS_CANONICAL(pCtx->ldtr.u64Base), VMX_IGS_LDTR_BASE_NOT_CANONICAL);
            HMVMX_CHECK_BREAK(!RT_HI_U32(pCtx->cs.u64Base), VMX_IGS_LONGMODE_CS_BASE_INVALID);
            HMVMX_CHECK_BREAK((pCtx->ss.Attr.u & X86DESCATTR_UNUSABLE) || !RT_HI_U32(pCtx->ss.u64Base),
                              VMX_IGS_LONGMODE_SS_BASE_INVALID);
            HMVMX_CHECK_BREAK((pCtx->ds.Attr.u & X86DESCATTR_UNUSABLE) || !RT_HI_U32(pCtx->ds.u64Base),
                              VMX_IGS_LONGMODE_DS_BASE_INVALID);
            HMVMX_CHECK_BREAK((pCtx->es.Attr.u & X86DESCATTR_UNUSABLE) || !RT_HI_U32(pCtx->es.u64Base),
                              VMX_IGS_LONGMODE_ES_BASE_INVALID);
        }

        /*
         * TR.
         */
        HMVMX_CHECK_BREAK(!(pCtx->tr.Sel & X86_SEL_LDT), VMX_IGS_TR_TI_INVALID);
        /* 64-bit capable CPUs. */
        HMVMX_CHECK_BREAK(X86_IS_CANONICAL(pCtx->tr.u64Base), VMX_IGS_TR_BASE_NOT_CANONICAL);
        if (fLongModeGuest)
            HMVMX_CHECK_BREAK(pCtx->tr.Attr.n.u4Type == 11,           /* 64-bit busy TSS. */
                              VMX_IGS_LONGMODE_TR_ATTR_TYPE_INVALID);
        else
            HMVMX_CHECK_BREAK(   pCtx->tr.Attr.n.u4Type == 3          /* 16-bit busy TSS. */
                              || pCtx->tr.Attr.n.u4Type == 11,        /* 32-bit busy TSS.*/
                              VMX_IGS_TR_ATTR_TYPE_INVALID);
        HMVMX_CHECK_BREAK(!pCtx->tr.Attr.n.u1DescType, VMX_IGS_TR_ATTR_S_INVALID);
        HMVMX_CHECK_BREAK(pCtx->tr.Attr.n.u1Present, VMX_IGS_TR_ATTR_P_INVALID);
        HMVMX_CHECK_BREAK(!(pCtx->tr.Attr.u & 0xf00), VMX_IGS_TR_ATTR_RESERVED);   /* Bits 11:8 MBZ. */
        HMVMX_CHECK_BREAK(   (pCtx->tr.u32Limit & 0xfff) == 0xfff
                          || !(pCtx->tr.Attr.n.u1Granularity), VMX_IGS_TR_ATTR_G_INVALID);
        HMVMX_CHECK_BREAK(   !(pCtx->tr.u32Limit & 0xfff00000)
                          || (pCtx->tr.Attr.n.u1Granularity), VMX_IGS_TR_ATTR_G_INVALID);
        HMVMX_CHECK_BREAK(!(pCtx->tr.Attr.u & X86DESCATTR_UNUSABLE), VMX_IGS_TR_ATTR_UNUSABLE);

        /*
         * GDTR and IDTR (64-bit capable checks).
         */
        rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_GDTR_BASE, &u64Val);
        AssertRC(rc);
        HMVMX_CHECK_BREAK(X86_IS_CANONICAL(u64Val), VMX_IGS_GDTR_BASE_NOT_CANONICAL);

        rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_IDTR_BASE, &u64Val);
        AssertRC(rc);
        HMVMX_CHECK_BREAK(X86_IS_CANONICAL(u64Val), VMX_IGS_IDTR_BASE_NOT_CANONICAL);

        rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_GDTR_LIMIT, &u32Val);
        AssertRC(rc);
        HMVMX_CHECK_BREAK(!(u32Val & 0xffff0000), VMX_IGS_GDTR_LIMIT_INVALID);      /* Bits 31:16 MBZ. */

        rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_IDTR_LIMIT, &u32Val);
        AssertRC(rc);
        HMVMX_CHECK_BREAK(!(u32Val & 0xffff0000), VMX_IGS_IDTR_LIMIT_INVALID);      /* Bits 31:16 MBZ. */

        /*
         * Guest Non-Register State.
         */
        /* Activity State. */
        uint32_t u32ActivityState;
        rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_ACTIVITY_STATE, &u32ActivityState);
        AssertRC(rc);
        HMVMX_CHECK_BREAK(   !u32ActivityState
                          || (u32ActivityState & RT_BF_GET(g_HmMsrs.u.vmx.u64Misc, VMX_BF_MISC_ACTIVITY_STATES)),
                             VMX_IGS_ACTIVITY_STATE_INVALID);
        HMVMX_CHECK_BREAK(   !(pCtx->ss.Attr.n.u2Dpl)
                          || u32ActivityState != VMX_VMCS_GUEST_ACTIVITY_HLT, VMX_IGS_ACTIVITY_STATE_HLT_INVALID);

        if (   u32IntrState == VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS
            || u32IntrState == VMX_VMCS_GUEST_INT_STATE_BLOCK_STI)
        {
            HMVMX_CHECK_BREAK(u32ActivityState == VMX_VMCS_GUEST_ACTIVITY_ACTIVE, VMX_IGS_ACTIVITY_STATE_ACTIVE_INVALID);
        }

        /** @todo Activity state and injecting interrupts. Left as a todo since we
         *        currently don't use activity states but ACTIVE. */

        HMVMX_CHECK_BREAK(   !(pVmcsInfo->u32EntryCtls & VMX_ENTRY_CTLS_ENTRY_TO_SMM)
                          || u32ActivityState != VMX_VMCS_GUEST_ACTIVITY_SIPI_WAIT, VMX_IGS_ACTIVITY_STATE_SIPI_WAIT_INVALID);

        /* Guest interruptibility-state. */
        HMVMX_CHECK_BREAK(!(u32IntrState & 0xffffffe0), VMX_IGS_INTERRUPTIBILITY_STATE_RESERVED);
        HMVMX_CHECK_BREAK((u32IntrState & (VMX_VMCS_GUEST_INT_STATE_BLOCK_STI | VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS))
                                       != (VMX_VMCS_GUEST_INT_STATE_BLOCK_STI | VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS),
                          VMX_IGS_INTERRUPTIBILITY_STATE_STI_MOVSS_INVALID);
        HMVMX_CHECK_BREAK(   (u32Eflags & X86_EFL_IF)
                          || !(u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI),
                          VMX_IGS_INTERRUPTIBILITY_STATE_STI_EFL_INVALID);
        if (VMX_ENTRY_INT_INFO_IS_EXT_INT(u32EntryInfo))
        {
            HMVMX_CHECK_BREAK(   !(u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI)
                              && !(u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS),
                              VMX_IGS_INTERRUPTIBILITY_STATE_EXT_INT_INVALID);
        }
        else if (VMX_ENTRY_INT_INFO_IS_XCPT_NMI(u32EntryInfo))
        {
            HMVMX_CHECK_BREAK(!(u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS),
                              VMX_IGS_INTERRUPTIBILITY_STATE_MOVSS_INVALID);
            HMVMX_CHECK_BREAK(!(u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI),
                              VMX_IGS_INTERRUPTIBILITY_STATE_STI_INVALID);
        }
        /** @todo Assumes the processor is not in SMM. */
        HMVMX_CHECK_BREAK(!(u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_SMI),
                          VMX_IGS_INTERRUPTIBILITY_STATE_SMI_INVALID);
        HMVMX_CHECK_BREAK(   !(pVmcsInfo->u32EntryCtls & VMX_ENTRY_CTLS_ENTRY_TO_SMM)
                          || (u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_SMI),
                             VMX_IGS_INTERRUPTIBILITY_STATE_SMI_SMM_INVALID);
        if (   (pVmcsInfo->u32PinCtls & VMX_PIN_CTLS_VIRT_NMI)
            && VMX_ENTRY_INT_INFO_IS_XCPT_NMI(u32EntryInfo))
        {
            HMVMX_CHECK_BREAK(!(u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_NMI), VMX_IGS_INTERRUPTIBILITY_STATE_NMI_INVALID);
        }

        /* Pending debug exceptions. */
        rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_PENDING_DEBUG_XCPTS, &u64Val);
        AssertRC(rc);
        /* Bits 63:15, Bit 13, Bits 11:4 MBZ. */
        HMVMX_CHECK_BREAK(!(u64Val & UINT64_C(0xffffffffffffaff0)), VMX_IGS_LONGMODE_PENDING_DEBUG_RESERVED);
        u32Val = u64Val;    /* For pending debug exceptions checks below. */

        if (   (u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI)
            || (u32IntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS)
            || u32ActivityState == VMX_VMCS_GUEST_ACTIVITY_HLT)
        {
            if (   (u32Eflags & X86_EFL_TF)
                && !(u64DebugCtlMsr & RT_BIT_64(1)))    /* Bit 1 is IA32_DEBUGCTL.BTF. */
            {
                /* Bit 14 is PendingDebug.BS. */
                HMVMX_CHECK_BREAK(u32Val & RT_BIT(14), VMX_IGS_PENDING_DEBUG_XCPT_BS_NOT_SET);
            }
            if (   !(u32Eflags & X86_EFL_TF)
                || (u64DebugCtlMsr & RT_BIT_64(1)))     /* Bit 1 is IA32_DEBUGCTL.BTF. */
            {
                /* Bit 14 is PendingDebug.BS. */
                HMVMX_CHECK_BREAK(!(u32Val & RT_BIT(14)), VMX_IGS_PENDING_DEBUG_XCPT_BS_NOT_CLEAR);
            }
        }

#ifndef IN_NEM_DARWIN
        /* VMCS link pointer. */
        rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_VMCS_LINK_PTR_FULL, &u64Val);
        AssertRC(rc);
        if (u64Val != UINT64_C(0xffffffffffffffff))
        {
            HMVMX_CHECK_BREAK(!(u64Val & 0xfff), VMX_IGS_VMCS_LINK_PTR_RESERVED);
            /** @todo Bits beyond the processor's physical-address width MBZ. */
            /** @todo SMM checks. */
            Assert(pVmcsInfo->HCPhysShadowVmcs == u64Val);
            Assert(pVmcsInfo->pvShadowVmcs);
            VMXVMCSREVID VmcsRevId;
            VmcsRevId.u = *(uint32_t *)pVmcsInfo->pvShadowVmcs;
            HMVMX_CHECK_BREAK(VmcsRevId.n.u31RevisionId == RT_BF_GET(g_HmMsrs.u.vmx.u64Basic, VMX_BF_BASIC_VMCS_ID),
                              VMX_IGS_VMCS_LINK_PTR_SHADOW_VMCS_ID_INVALID);
            HMVMX_CHECK_BREAK(VmcsRevId.n.fIsShadowVmcs == (uint32_t)!!(pVmcsInfo->u32ProcCtls2 & VMX_PROC_CTLS2_VMCS_SHADOWING),
                              VMX_IGS_VMCS_LINK_PTR_NOT_SHADOW);
        }

        /** @todo Checks on Guest Page-Directory-Pointer-Table Entries when guest is
         *        not using nested paging? */
        if (   VM_IS_VMX_NESTED_PAGING(pVM)
            && !fLongModeGuest
            && CPUMIsGuestInPAEModeEx(pCtx))
        {
            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PDPTE0_FULL, &u64Val);
            AssertRC(rc);
            HMVMX_CHECK_BREAK(!(u64Val & X86_PDPE_PAE_MBZ_MASK), VMX_IGS_PAE_PDPTE_RESERVED);

            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PDPTE1_FULL, &u64Val);
            AssertRC(rc);
            HMVMX_CHECK_BREAK(!(u64Val & X86_PDPE_PAE_MBZ_MASK), VMX_IGS_PAE_PDPTE_RESERVED);

            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PDPTE2_FULL, &u64Val);
            AssertRC(rc);
            HMVMX_CHECK_BREAK(!(u64Val & X86_PDPE_PAE_MBZ_MASK), VMX_IGS_PAE_PDPTE_RESERVED);

            rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_GUEST_PDPTE3_FULL, &u64Val);
            AssertRC(rc);
            HMVMX_CHECK_BREAK(!(u64Val & X86_PDPE_PAE_MBZ_MASK), VMX_IGS_PAE_PDPTE_RESERVED);
        }
#endif

        /* Shouldn't happen but distinguish it from AssertRCBreak() errors. */
        if (uError == VMX_IGS_ERROR)
            uError = VMX_IGS_REASON_NOT_FOUND;
    } while (0);

    VCPU_2_VMXSTATE(pVCpu).u32HMError = uError;
    VCPU_2_VMXSTATE(pVCpu).vmx.LastError.u32GuestIntrState = u32IntrState;
    return uError;

#undef HMVMX_ERROR_BREAK
#undef HMVMX_CHECK_BREAK
}


#ifndef HMVMX_USE_FUNCTION_TABLE
/**
 * Handles a guest VM-exit from hardware-assisted VMX execution.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
DECLINLINE(VBOXSTRICTRC) vmxHCHandleExit(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
#ifdef DEBUG_ramshankar
# define VMEXIT_CALL_RET(a_fSave, a_CallExpr) \
       do { \
            if (a_fSave != 0) \
                vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__); \
            VBOXSTRICTRC rcStrict = a_CallExpr; \
            if (a_fSave != 0) \
                ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST); \
            return rcStrict; \
        } while (0)
#else
# define VMEXIT_CALL_RET(a_fSave, a_CallExpr) return a_CallExpr
#endif
    uint32_t const uExitReason = pVmxTransient->uExitReason;
    switch (uExitReason)
    {
        case VMX_EXIT_EPT_MISCONFIG:           VMEXIT_CALL_RET(0, vmxHCExitEptMisconfig(pVCpu, pVmxTransient));
        case VMX_EXIT_EPT_VIOLATION:           VMEXIT_CALL_RET(0, vmxHCExitEptViolation(pVCpu, pVmxTransient));
        case VMX_EXIT_IO_INSTR:                VMEXIT_CALL_RET(0, vmxHCExitIoInstr(pVCpu, pVmxTransient));
        case VMX_EXIT_CPUID:                   VMEXIT_CALL_RET(0, vmxHCExitCpuid(pVCpu, pVmxTransient));
        case VMX_EXIT_RDTSC:                   VMEXIT_CALL_RET(0, vmxHCExitRdtsc(pVCpu, pVmxTransient));
        case VMX_EXIT_RDTSCP:                  VMEXIT_CALL_RET(0, vmxHCExitRdtscp(pVCpu, pVmxTransient));
        case VMX_EXIT_APIC_ACCESS:             VMEXIT_CALL_RET(0, vmxHCExitApicAccess(pVCpu, pVmxTransient));
        case VMX_EXIT_XCPT_OR_NMI:             VMEXIT_CALL_RET(0, vmxHCExitXcptOrNmi(pVCpu, pVmxTransient));
        case VMX_EXIT_MOV_CRX:                 VMEXIT_CALL_RET(0, vmxHCExitMovCRx(pVCpu, pVmxTransient));
        case VMX_EXIT_EXT_INT:                 VMEXIT_CALL_RET(0, vmxHCExitExtInt(pVCpu, pVmxTransient));
        case VMX_EXIT_INT_WINDOW:              VMEXIT_CALL_RET(0, vmxHCExitIntWindow(pVCpu, pVmxTransient));
        case VMX_EXIT_TPR_BELOW_THRESHOLD:     VMEXIT_CALL_RET(0, vmxHCExitTprBelowThreshold(pVCpu, pVmxTransient));
        case VMX_EXIT_MWAIT:                   VMEXIT_CALL_RET(0, vmxHCExitMwait(pVCpu, pVmxTransient));
        case VMX_EXIT_MONITOR:                 VMEXIT_CALL_RET(0, vmxHCExitMonitor(pVCpu, pVmxTransient));
        case VMX_EXIT_TASK_SWITCH:             VMEXIT_CALL_RET(0, vmxHCExitTaskSwitch(pVCpu, pVmxTransient));
        case VMX_EXIT_PREEMPT_TIMER:           VMEXIT_CALL_RET(0, vmxHCExitPreemptTimer(pVCpu, pVmxTransient));
        case VMX_EXIT_RDMSR:                   VMEXIT_CALL_RET(0, vmxHCExitRdmsr(pVCpu, pVmxTransient));
        case VMX_EXIT_WRMSR:                   VMEXIT_CALL_RET(0, vmxHCExitWrmsr(pVCpu, pVmxTransient));
        case VMX_EXIT_VMCALL:                  VMEXIT_CALL_RET(0, vmxHCExitVmcall(pVCpu, pVmxTransient));
        case VMX_EXIT_MOV_DRX:                 VMEXIT_CALL_RET(0, vmxHCExitMovDRx(pVCpu, pVmxTransient));
        case VMX_EXIT_HLT:                     VMEXIT_CALL_RET(0, vmxHCExitHlt(pVCpu, pVmxTransient));
        case VMX_EXIT_INVD:                    VMEXIT_CALL_RET(0, vmxHCExitInvd(pVCpu, pVmxTransient));
        case VMX_EXIT_INVLPG:                  VMEXIT_CALL_RET(0, vmxHCExitInvlpg(pVCpu, pVmxTransient));
        case VMX_EXIT_MTF:                     VMEXIT_CALL_RET(0, vmxHCExitMtf(pVCpu, pVmxTransient));
        case VMX_EXIT_PAUSE:                   VMEXIT_CALL_RET(0, vmxHCExitPause(pVCpu, pVmxTransient));
        case VMX_EXIT_WBINVD:                  VMEXIT_CALL_RET(0, vmxHCExitWbinvd(pVCpu, pVmxTransient));
        case VMX_EXIT_XSETBV:                  VMEXIT_CALL_RET(0, vmxHCExitXsetbv(pVCpu, pVmxTransient));
        case VMX_EXIT_INVPCID:                 VMEXIT_CALL_RET(0, vmxHCExitInvpcid(pVCpu, pVmxTransient));
        case VMX_EXIT_GETSEC:                  VMEXIT_CALL_RET(0, vmxHCExitGetsec(pVCpu, pVmxTransient));
        case VMX_EXIT_RDPMC:                   VMEXIT_CALL_RET(0, vmxHCExitRdpmc(pVCpu, pVmxTransient));
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
        case VMX_EXIT_VMCLEAR:                 VMEXIT_CALL_RET(0, vmxHCExitVmclear(pVCpu, pVmxTransient));
        case VMX_EXIT_VMLAUNCH:                VMEXIT_CALL_RET(0, vmxHCExitVmlaunch(pVCpu, pVmxTransient));
        case VMX_EXIT_VMPTRLD:                 VMEXIT_CALL_RET(0, vmxHCExitVmptrld(pVCpu, pVmxTransient));
        case VMX_EXIT_VMPTRST:                 VMEXIT_CALL_RET(0, vmxHCExitVmptrst(pVCpu, pVmxTransient));
        case VMX_EXIT_VMREAD:                  VMEXIT_CALL_RET(0, vmxHCExitVmread(pVCpu, pVmxTransient));
        case VMX_EXIT_VMRESUME:                VMEXIT_CALL_RET(0, vmxHCExitVmwrite(pVCpu, pVmxTransient));
        case VMX_EXIT_VMWRITE:                 VMEXIT_CALL_RET(0, vmxHCExitVmresume(pVCpu, pVmxTransient));
        case VMX_EXIT_VMXOFF:                  VMEXIT_CALL_RET(0, vmxHCExitVmxoff(pVCpu, pVmxTransient));
        case VMX_EXIT_VMXON:                   VMEXIT_CALL_RET(0, vmxHCExitVmxon(pVCpu, pVmxTransient));
        case VMX_EXIT_INVVPID:                 VMEXIT_CALL_RET(0, vmxHCExitInvvpid(pVCpu, pVmxTransient));
#else
        case VMX_EXIT_VMCLEAR:
        case VMX_EXIT_VMLAUNCH:
        case VMX_EXIT_VMPTRLD:
        case VMX_EXIT_VMPTRST:
        case VMX_EXIT_VMREAD:
        case VMX_EXIT_VMRESUME:
        case VMX_EXIT_VMWRITE:
        case VMX_EXIT_VMXOFF:
        case VMX_EXIT_VMXON:
        case VMX_EXIT_INVVPID:
            return vmxHCExitSetPendingXcptUD(pVCpu, pVmxTransient);
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        case VMX_EXIT_INVEPT:                  VMEXIT_CALL_RET(0, vmxHCExitInvept(pVCpu, pVmxTransient));
#else
        case VMX_EXIT_INVEPT:                  return vmxHCExitSetPendingXcptUD(pVCpu, pVmxTransient);
#endif

        case VMX_EXIT_TRIPLE_FAULT:            return vmxHCExitTripleFault(pVCpu, pVmxTransient);
        case VMX_EXIT_NMI_WINDOW:              return vmxHCExitNmiWindow(pVCpu, pVmxTransient);
        case VMX_EXIT_ERR_INVALID_GUEST_STATE: return vmxHCExitErrInvalidGuestState(pVCpu, pVmxTransient);

        case VMX_EXIT_INIT_SIGNAL:
        case VMX_EXIT_SIPI:
        case VMX_EXIT_IO_SMI:
        case VMX_EXIT_SMI:
        case VMX_EXIT_ERR_MSR_LOAD:
        case VMX_EXIT_ERR_MACHINE_CHECK:
        case VMX_EXIT_PML_FULL:
        case VMX_EXIT_VIRTUALIZED_EOI:
        case VMX_EXIT_GDTR_IDTR_ACCESS:
        case VMX_EXIT_LDTR_TR_ACCESS:
        case VMX_EXIT_APIC_WRITE:
        case VMX_EXIT_RDRAND:
        case VMX_EXIT_RSM:
        case VMX_EXIT_VMFUNC:
        case VMX_EXIT_ENCLS:
        case VMX_EXIT_RDSEED:
        case VMX_EXIT_XSAVES:
        case VMX_EXIT_XRSTORS:
        case VMX_EXIT_UMWAIT:
        case VMX_EXIT_TPAUSE:
        case VMX_EXIT_LOADIWKEY:
        default:
            return vmxHCExitErrUnexpected(pVCpu, pVmxTransient);
    }
#undef VMEXIT_CALL_RET
}
#endif /* !HMVMX_USE_FUNCTION_TABLE */


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/**
 * Handles a nested-guest VM-exit from hardware-assisted VMX execution.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 */
DECLINLINE(VBOXSTRICTRC) vmxHCHandleExitNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
#ifdef DEBUG_ramshankar
# define VMEXIT_CALL_RET(a_fSave, a_CallExpr) \
       do { \
            if (a_fSave != 0) \
                vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__); \
            VBOXSTRICTRC rcStrict = a_CallExpr; \
            return rcStrict; \
        } while (0)
#else
# define VMEXIT_CALL_RET(a_fSave, a_CallExpr) return a_CallExpr
#endif

    uint32_t const uExitReason = pVmxTransient->uExitReason;
    switch (uExitReason)
    {
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        case VMX_EXIT_EPT_MISCONFIG:            VMEXIT_CALL_RET(0, vmxHCExitEptMisconfigNested(pVCpu, pVmxTransient));
        case VMX_EXIT_EPT_VIOLATION:            VMEXIT_CALL_RET(0, vmxHCExitEptViolationNested(pVCpu, pVmxTransient));
# else
        case VMX_EXIT_EPT_MISCONFIG:            VMEXIT_CALL_RET(0, vmxHCExitEptMisconfig(pVCpu, pVmxTransient));
        case VMX_EXIT_EPT_VIOLATION:            VMEXIT_CALL_RET(0, vmxHCExitEptViolation(pVCpu, pVmxTransient));
# endif
        case VMX_EXIT_XCPT_OR_NMI:              VMEXIT_CALL_RET(0, vmxHCExitXcptOrNmiNested(pVCpu, pVmxTransient));
        case VMX_EXIT_IO_INSTR:                 VMEXIT_CALL_RET(0, vmxHCExitIoInstrNested(pVCpu, pVmxTransient));
        case VMX_EXIT_HLT:                      VMEXIT_CALL_RET(0, vmxHCExitHltNested(pVCpu, pVmxTransient));

        /*
         * We shouldn't direct host physical interrupts to the nested-guest.
         */
        case VMX_EXIT_EXT_INT:                  VMEXIT_CALL_RET(0, vmxHCExitExtInt(pVCpu, pVmxTransient));

        /*
         * Instructions that cause VM-exits unconditionally or the condition is
         * always taken solely from the nested hypervisor (meaning if the VM-exit
         * happens, it's guaranteed to be a nested-guest VM-exit).
         *
         *   - Provides VM-exit instruction length ONLY.
         */
        case VMX_EXIT_CPUID:              /* Unconditional. */
        case VMX_EXIT_VMCALL:
        case VMX_EXIT_GETSEC:
        case VMX_EXIT_INVD:
        case VMX_EXIT_XSETBV:
        case VMX_EXIT_VMLAUNCH:
        case VMX_EXIT_VMRESUME:
        case VMX_EXIT_VMXOFF:
        case VMX_EXIT_ENCLS:              /* Condition specified solely by nested hypervisor. */
        case VMX_EXIT_VMFUNC:
            VMEXIT_CALL_RET(0, vmxHCExitInstrNested(pVCpu, pVmxTransient));

        /*
         * Instructions that cause VM-exits unconditionally or the condition is
         * always taken solely from the nested hypervisor (meaning if the VM-exit
         * happens, it's guaranteed to be a nested-guest VM-exit).
         *
         *   - Provides VM-exit instruction length.
         *   - Provides VM-exit information.
         *   - Optionally provides Exit qualification.
         *
         * Since Exit qualification is 0 for all VM-exits where it is not
         * applicable, reading and passing it to the guest should produce
         * defined behavior.
         *
         * See Intel spec. 27.2.1 "Basic VM-Exit Information".
         */
        case VMX_EXIT_INVEPT:             /* Unconditional. */
        case VMX_EXIT_INVVPID:
        case VMX_EXIT_VMCLEAR:
        case VMX_EXIT_VMPTRLD:
        case VMX_EXIT_VMPTRST:
        case VMX_EXIT_VMXON:
        case VMX_EXIT_GDTR_IDTR_ACCESS:   /* Condition specified solely by nested hypervisor. */
        case VMX_EXIT_LDTR_TR_ACCESS:
        case VMX_EXIT_RDRAND:
        case VMX_EXIT_RDSEED:
        case VMX_EXIT_XSAVES:
        case VMX_EXIT_XRSTORS:
        case VMX_EXIT_UMWAIT:
        case VMX_EXIT_TPAUSE:
            VMEXIT_CALL_RET(0, vmxHCExitInstrWithInfoNested(pVCpu, pVmxTransient));

        case VMX_EXIT_RDTSC:                    VMEXIT_CALL_RET(0, vmxHCExitRdtscNested(pVCpu, pVmxTransient));
        case VMX_EXIT_RDTSCP:                   VMEXIT_CALL_RET(0, vmxHCExitRdtscpNested(pVCpu, pVmxTransient));
        case VMX_EXIT_RDMSR:                    VMEXIT_CALL_RET(0, vmxHCExitRdmsrNested(pVCpu, pVmxTransient));
        case VMX_EXIT_WRMSR:                    VMEXIT_CALL_RET(0, vmxHCExitWrmsrNested(pVCpu, pVmxTransient));
        case VMX_EXIT_INVLPG:                   VMEXIT_CALL_RET(0, vmxHCExitInvlpgNested(pVCpu, pVmxTransient));
        case VMX_EXIT_INVPCID:                  VMEXIT_CALL_RET(0, vmxHCExitInvpcidNested(pVCpu, pVmxTransient));
        case VMX_EXIT_TASK_SWITCH:              VMEXIT_CALL_RET(0, vmxHCExitTaskSwitchNested(pVCpu, pVmxTransient));
        case VMX_EXIT_WBINVD:                   VMEXIT_CALL_RET(0, vmxHCExitWbinvdNested(pVCpu, pVmxTransient));
        case VMX_EXIT_MTF:                      VMEXIT_CALL_RET(0, vmxHCExitMtfNested(pVCpu, pVmxTransient));
        case VMX_EXIT_APIC_ACCESS:              VMEXIT_CALL_RET(0, vmxHCExitApicAccessNested(pVCpu, pVmxTransient));
        case VMX_EXIT_APIC_WRITE:               VMEXIT_CALL_RET(0, vmxHCExitApicWriteNested(pVCpu, pVmxTransient));
        case VMX_EXIT_VIRTUALIZED_EOI:          VMEXIT_CALL_RET(0, vmxHCExitVirtEoiNested(pVCpu, pVmxTransient));
        case VMX_EXIT_MOV_CRX:                  VMEXIT_CALL_RET(0, vmxHCExitMovCRxNested(pVCpu, pVmxTransient));
        case VMX_EXIT_INT_WINDOW:               VMEXIT_CALL_RET(0, vmxHCExitIntWindowNested(pVCpu, pVmxTransient));
        case VMX_EXIT_NMI_WINDOW:               VMEXIT_CALL_RET(0, vmxHCExitNmiWindowNested(pVCpu, pVmxTransient));
        case VMX_EXIT_TPR_BELOW_THRESHOLD:      VMEXIT_CALL_RET(0, vmxHCExitTprBelowThresholdNested(pVCpu, pVmxTransient));
        case VMX_EXIT_MWAIT:                    VMEXIT_CALL_RET(0, vmxHCExitMwaitNested(pVCpu, pVmxTransient));
        case VMX_EXIT_MONITOR:                  VMEXIT_CALL_RET(0, vmxHCExitMonitorNested(pVCpu, pVmxTransient));
        case VMX_EXIT_PAUSE:                    VMEXIT_CALL_RET(0, vmxHCExitPauseNested(pVCpu, pVmxTransient));

        case VMX_EXIT_PREEMPT_TIMER:
        {
            /** @todo NSTVMX: Preempt timer. */
            VMEXIT_CALL_RET(0, vmxHCExitPreemptTimer(pVCpu, pVmxTransient));
        }

        case VMX_EXIT_MOV_DRX:                  VMEXIT_CALL_RET(0, vmxHCExitMovDRxNested(pVCpu, pVmxTransient));
        case VMX_EXIT_RDPMC:                    VMEXIT_CALL_RET(0, vmxHCExitRdpmcNested(pVCpu, pVmxTransient));

        case VMX_EXIT_VMREAD:
        case VMX_EXIT_VMWRITE:                  VMEXIT_CALL_RET(0, vmxHCExitVmreadVmwriteNested(pVCpu, pVmxTransient));

        case VMX_EXIT_TRIPLE_FAULT:             VMEXIT_CALL_RET(0, vmxHCExitTripleFaultNested(pVCpu, pVmxTransient));
        case VMX_EXIT_ERR_INVALID_GUEST_STATE:  VMEXIT_CALL_RET(0, vmxHCExitErrInvalidGuestStateNested(pVCpu, pVmxTransient));

        case VMX_EXIT_INIT_SIGNAL:
        case VMX_EXIT_SIPI:
        case VMX_EXIT_IO_SMI:
        case VMX_EXIT_SMI:
        case VMX_EXIT_ERR_MSR_LOAD:
        case VMX_EXIT_ERR_MACHINE_CHECK:
        case VMX_EXIT_PML_FULL:
        case VMX_EXIT_RSM:
        default:
            return vmxHCExitErrUnexpected(pVCpu, pVmxTransient);
    }
#undef VMEXIT_CALL_RET
}
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */


/** @name VM-exit helpers.
 * @{
 */
/* -=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */
/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= VM-exit helpers -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */
/* -=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */

/** Macro for VM-exits called unexpectedly. */
#define HMVMX_UNEXPECTED_EXIT_RET(a_pVCpu, a_HmError) \
    do { \
        VCPU_2_VMXSTATE((a_pVCpu)).u32HMError = (a_HmError); \
        return VERR_VMX_UNEXPECTED_EXIT; \
    } while (0)

#ifdef VBOX_STRICT
# ifndef IN_NEM_DARWIN
/* Is there some generic IPRT define for this that are not in Runtime/internal/\* ?? */
# define HMVMX_ASSERT_PREEMPT_CPUID_VAR() \
    RTCPUID const idAssertCpu = RTThreadPreemptIsEnabled(NIL_RTTHREAD) ? NIL_RTCPUID : RTMpCpuId()

# define HMVMX_ASSERT_PREEMPT_CPUID() \
    do { \
         RTCPUID const idAssertCpuNow = RTThreadPreemptIsEnabled(NIL_RTTHREAD) ? NIL_RTCPUID : RTMpCpuId(); \
         AssertMsg(idAssertCpu == idAssertCpuNow,  ("VMX %#x, %#x\n", idAssertCpu, idAssertCpuNow)); \
    } while (0)

# define HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(a_pVCpu, a_pVmxTransient) \
    do { \
        AssertPtr((a_pVCpu)); \
        AssertPtr((a_pVmxTransient)); \
        Assert(   (a_pVmxTransient)->fVMEntryFailed == false \
               || (a_pVmxTransient)->uExitReason == VMX_EXIT_ERR_INVALID_GUEST_STATE \
               || (a_pVmxTransient)->uExitReason == VMX_EXIT_ERR_MSR_LOAD \
               || (a_pVmxTransient)->uExitReason == VMX_EXIT_ERR_MACHINE_CHECK); \
        Assert((a_pVmxTransient)->pVmcsInfo); \
        Assert(ASMIntAreEnabled()); \
        HMVMX_ASSERT_PREEMPT_SAFE(a_pVCpu); \
        HMVMX_ASSERT_PREEMPT_CPUID_VAR(); \
        Log4Func(("vcpu[%RU32]\n", (a_pVCpu)->idCpu)); \
        HMVMX_ASSERT_PREEMPT_SAFE(a_pVCpu); \
        if (!VMMRZCallRing3IsEnabled((a_pVCpu))) \
            HMVMX_ASSERT_PREEMPT_CPUID(); \
        HMVMX_STOP_EXIT_DISPATCH_PROF(); \
    } while (0)
# else
# define HMVMX_ASSERT_PREEMPT_CPUID_VAR()   do { } while(0)
# define HMVMX_ASSERT_PREEMPT_CPUID()       do { } while(0)
# define HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(a_pVCpu, a_pVmxTransient) \
    do { \
        AssertPtr((a_pVCpu)); \
        AssertPtr((a_pVmxTransient)); \
        Assert(   (a_pVmxTransient)->fVMEntryFailed == false \
               || (a_pVmxTransient)->uExitReason == VMX_EXIT_ERR_INVALID_GUEST_STATE \
               || (a_pVmxTransient)->uExitReason == VMX_EXIT_ERR_MSR_LOAD \
               || (a_pVmxTransient)->uExitReason == VMX_EXIT_ERR_MACHINE_CHECK); \
        Assert((a_pVmxTransient)->pVmcsInfo); \
        Log4Func(("vcpu[%RU32]\n", (a_pVCpu)->idCpu)); \
        HMVMX_STOP_EXIT_DISPATCH_PROF(); \
    } while (0)
# endif

# define HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(a_pVCpu, a_pVmxTransient) \
    do { \
        HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(a_pVCpu, a_pVmxTransient); \
        Assert((a_pVmxTransient)->fIsNestedGuest); \
    } while (0)

# define HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(a_pVCpu, a_pVmxTransient) \
    do { \
        Log4Func(("\n")); \
    } while (0)
#else
# define HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(a_pVCpu, a_pVmxTransient) \
    do { \
        HMVMX_STOP_EXIT_DISPATCH_PROF(); \
        NOREF((a_pVCpu)); NOREF((a_pVmxTransient)); \
    } while (0)

# define HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(a_pVCpu, a_pVmxTransient) \
    do { HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(a_pVCpu, a_pVmxTransient); } while (0)

# define HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(a_pVCpu, a_pVmxTransient)      do { } while (0)
#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/** Macro that does the necessary privilege checks and intercepted VM-exits for
 *  guests that attempted to execute a VMX instruction. */
# define HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(a_pVCpu, a_uExitReason) \
    do \
    { \
        VBOXSTRICTRC rcStrictTmp = vmxHCCheckExitDueToVmxInstr((a_pVCpu), (a_uExitReason)); \
        if (rcStrictTmp == VINF_SUCCESS) \
        { /* likely */ } \
        else if (rcStrictTmp == VINF_HM_PENDING_XCPT) \
        { \
            Assert((a_pVCpu)->hm.s.Event.fPending); \
            Log4Func(("Privilege checks failed -> %#x\n", VMX_ENTRY_INT_INFO_VECTOR((a_pVCpu)->hm.s.Event.u64IntInfo))); \
            return VINF_SUCCESS; \
        } \
        else \
        { \
            int rcTmp = VBOXSTRICTRC_VAL(rcStrictTmp); \
            AssertMsgFailedReturn(("Unexpected failure. rc=%Rrc", rcTmp), rcTmp); \
        } \
    } while (0)

/** Macro that decodes a memory operand for an VM-exit caused by an instruction. */
# define HMVMX_DECODE_MEM_OPERAND(a_pVCpu, a_uExitInstrInfo, a_uExitQual, a_enmMemAccess, a_pGCPtrEffAddr) \
    do \
    { \
        VBOXSTRICTRC rcStrictTmp = vmxHCDecodeMemOperand((a_pVCpu), (a_uExitInstrInfo), (a_uExitQual), (a_enmMemAccess), \
                                                           (a_pGCPtrEffAddr)); \
        if (rcStrictTmp == VINF_SUCCESS) \
        { /* likely */ } \
        else if (rcStrictTmp == VINF_HM_PENDING_XCPT) \
        { \
            uint8_t const uXcptTmp = VMX_ENTRY_INT_INFO_VECTOR((a_pVCpu)->hm.s.Event.u64IntInfo); \
            Log4Func(("Memory operand decoding failed, raising xcpt %#x\n", uXcptTmp)); \
            NOREF(uXcptTmp); \
            return VINF_SUCCESS; \
        } \
        else \
        { \
            Log4Func(("vmxHCDecodeMemOperand failed. rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrictTmp))); \
            return rcStrictTmp; \
        } \
    } while (0)
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */


/**
 * Advances the guest RIP by the specified number of bytes.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     Number of bytes to advance the RIP by.
 *
 * @remarks No-long-jump zone!!!
 */
DECLINLINE(void) vmxHCAdvanceGuestRipBy(PVMCPUCC pVCpu, uint32_t cbInstr)
{
    CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);

    /*
     * Advance RIP.
     *
     * The upper 32 bits are only set when in 64-bit mode, so we have to detect
     * when the addition causes a "carry" into the upper half and check whether
     * we're in 64-bit and can go on with it or wether we should zap the top
     * half. (Note! The 8086, 80186 and 80286 emulation is done exclusively in
     * IEM, so we don't need to bother with pre-386 16-bit wraparound.)
     *
     * See PC wrap around tests in bs3-cpu-weird-1.
     */
    uint64_t const uRipPrev = pVCpu->cpum.GstCtx.rip;
    uint64_t const uRipNext = uRipPrev + cbInstr;
    if (RT_LIKELY(   !((uRipNext ^ uRipPrev) & RT_BIT_64(32))
                  || CPUMIsGuestIn64BitCodeEx(&pVCpu->cpum.GstCtx)))
        pVCpu->cpum.GstCtx.rip = uRipNext;
    else
        pVCpu->cpum.GstCtx.rip = (uint32_t)uRipNext;

    /*
     * Clear RF and interrupt shadowing.
     */
    if (RT_LIKELY(!(pVCpu->cpum.GstCtx.eflags.uBoth & (X86_EFL_RF | X86_EFL_TF))))
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~CPUMCTX_INHIBIT_SHADOW;
    else
    {
        if ((pVCpu->cpum.GstCtx.eflags.uBoth & (X86_EFL_RF | X86_EFL_TF)) == X86_EFL_TF)
        {
            /** @todo \#DB - single step. */
        }
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~(X86_EFL_RF | CPUMCTX_INHIBIT_SHADOW);
    }
    AssertCompile(CPUMCTX_INHIBIT_SHADOW < UINT32_MAX);

    /* Mark both RIP and RFLAGS as updated. */
    ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
}


/**
 * Advances the guest RIP after reading it from the VMCS.
 *
 * @returns VBox status code, no informational status codes.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks No-long-jump zone!!!
 */
static int vmxHCAdvanceGuestRip(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    vmxHCReadToTransientSlow<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    /** @todo consider template here after checking callers.   */
    int rc = vmxHCImportGuestStateEx(pVCpu, pVmxTransient->pVmcsInfo, CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS);
    AssertRCReturn(rc, rc);

    vmxHCAdvanceGuestRipBy(pVCpu, pVmxTransient->cbExitInstr);
    return VINF_SUCCESS;
}


/**
 * Handle a condition that occurred while delivering an event through the guest or
 * nested-guest IDT.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @retval  VINF_SUCCESS if we should continue handling the VM-exit.
 * @retval  VINF_HM_DOUBLE_FAULT if a \#DF condition was detected and we ought
 *          to continue execution of the guest which will delivery the \#DF.
 * @retval  VINF_EM_RESET if we detected a triple-fault condition.
 * @retval  VERR_EM_GUEST_CPU_HANG if we detected a guest CPU hang.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 *
 * @remarks Requires all fields in HMVMX_READ_XCPT_INFO to be read from the VMCS.
 *          Additionally, HMVMX_READ_EXIT_QUALIFICATION is required if the VM-exit
 *          is due to an EPT violation, PML full or SPP-related event.
 *
 * @remarks No-long-jump zone!!!
 */
static VBOXSTRICTRC vmxHCCheckExitDueToEventDelivery(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    Assert(!VCPU_2_VMXSTATE(pVCpu).Event.fPending);
    HMVMX_ASSERT_READ(pVmxTransient, HMVMX_READ_XCPT_INFO);
    if (   pVmxTransient->uExitReason == VMX_EXIT_EPT_VIOLATION
        || pVmxTransient->uExitReason == VMX_EXIT_PML_FULL
        || pVmxTransient->uExitReason == VMX_EXIT_SPP_EVENT)
        HMVMX_ASSERT_READ(pVmxTransient, HMVMX_READ_EXIT_QUALIFICATION);

    VBOXSTRICTRC   rcStrict       = VINF_SUCCESS;
    PCVMXVMCSINFO  pVmcsInfo      = pVmxTransient->pVmcsInfo;
    uint32_t const uIdtVectorInfo = pVmxTransient->uIdtVectoringInfo;
    uint32_t const uExitIntInfo   = pVmxTransient->uExitIntInfo;
    if (VMX_IDT_VECTORING_INFO_IS_VALID(uIdtVectorInfo))
    {
        uint32_t const uIdtVector     = VMX_IDT_VECTORING_INFO_VECTOR(uIdtVectorInfo);
        uint32_t const uIdtVectorType = VMX_IDT_VECTORING_INFO_TYPE(uIdtVectorInfo);

        /*
         * If the event was a software interrupt (generated with INT n) or a software exception
         * (generated by INT3/INTO) or a privileged software exception (generated by INT1), we
         * can handle the VM-exit and continue guest execution which will re-execute the
         * instruction rather than re-injecting the exception, as that can cause premature
         * trips to ring-3 before injection and involve TRPM which currently has no way of
         * storing that these exceptions were caused by these instructions (ICEBP's #DB poses
         * the problem).
         */
        IEMXCPTRAISE     enmRaise;
        IEMXCPTRAISEINFO fRaiseInfo;
        if (   uIdtVectorType == VMX_IDT_VECTORING_INFO_TYPE_SW_INT
            || uIdtVectorType == VMX_IDT_VECTORING_INFO_TYPE_SW_XCPT
            || uIdtVectorType == VMX_IDT_VECTORING_INFO_TYPE_PRIV_SW_XCPT)
        {
            enmRaise   = IEMXCPTRAISE_REEXEC_INSTR;
            fRaiseInfo = IEMXCPTRAISEINFO_NONE;
        }
        else if (VMX_EXIT_INT_INFO_IS_VALID(uExitIntInfo))
        {
            uint32_t const uExitVectorType = VMX_EXIT_INT_INFO_TYPE(uExitIntInfo);
            uint8_t const  uExitVector     = VMX_EXIT_INT_INFO_VECTOR(uExitIntInfo);
            Assert(uExitVectorType == VMX_EXIT_INT_INFO_TYPE_HW_XCPT);

            uint32_t const fIdtVectorFlags  = vmxHCGetIemXcptFlags(uIdtVector, uIdtVectorType);
            uint32_t const fExitVectorFlags = vmxHCGetIemXcptFlags(uExitVector, uExitVectorType);

            enmRaise = IEMEvaluateRecursiveXcpt(pVCpu, fIdtVectorFlags, uIdtVector, fExitVectorFlags, uExitVector, &fRaiseInfo);

            /* Determine a vectoring #PF condition, see comment in vmxHCExitXcptPF(). */
            if (fRaiseInfo & (IEMXCPTRAISEINFO_EXT_INT_PF | IEMXCPTRAISEINFO_NMI_PF))
            {
                pVmxTransient->fVectoringPF = true;
                enmRaise = IEMXCPTRAISE_PREV_EVENT;
            }
        }
        else
        {
            /*
             * If an exception or hardware interrupt delivery caused an EPT violation/misconfig or APIC access
             * VM-exit, then the VM-exit interruption-information will not be valid and we end up here.
             * It is sufficient to reflect the original event to the guest after handling the VM-exit.
             */
            Assert(   uIdtVectorType == VMX_IDT_VECTORING_INFO_TYPE_HW_XCPT
                   || uIdtVectorType == VMX_IDT_VECTORING_INFO_TYPE_NMI
                   || uIdtVectorType == VMX_IDT_VECTORING_INFO_TYPE_EXT_INT);
            enmRaise   = IEMXCPTRAISE_PREV_EVENT;
            fRaiseInfo = IEMXCPTRAISEINFO_NONE;
        }

        /*
         * On CPUs that support Virtual NMIs, if this VM-exit (be it an exception or EPT violation/misconfig
         * etc.) occurred while delivering the NMI, we need to clear the block-by-NMI field in the guest
         * interruptibility-state before re-delivering the NMI after handling the VM-exit. Otherwise the
         * subsequent VM-entry would fail, see @bugref{7445}.
         *
         * See Intel spec. 30.7.1.2 "Resuming Guest Software after Handling an Exception".
         */
        if (   uIdtVectorType == VMX_IDT_VECTORING_INFO_TYPE_NMI
            && enmRaise == IEMXCPTRAISE_PREV_EVENT
            && (pVmcsInfo->u32PinCtls & VMX_PIN_CTLS_VIRT_NMI)
            && CPUMAreInterruptsInhibitedByNmiEx(&pVCpu->cpum.GstCtx))
            CPUMClearInterruptInhibitingByNmiEx(&pVCpu->cpum.GstCtx);

        switch (enmRaise)
        {
            case IEMXCPTRAISE_CURRENT_XCPT:
            {
                Log4Func(("IDT: Pending secondary Xcpt: idtinfo=%#RX64 exitinfo=%#RX64\n", uIdtVectorInfo, uExitIntInfo));
                Assert(rcStrict == VINF_SUCCESS);
                break;
            }

            case IEMXCPTRAISE_PREV_EVENT:
            {
                uint32_t u32ErrCode;
                if (VMX_IDT_VECTORING_INFO_IS_ERROR_CODE_VALID(uIdtVectorInfo))
                    u32ErrCode = pVmxTransient->uIdtVectoringErrorCode;
                else
                    u32ErrCode = 0;

                /* If uExitVector is #PF, CR2 value will be updated from the VMCS if it's a guest #PF, see vmxHCExitXcptPF(). */
                STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatInjectReflect);
                vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_IDT_INFO(uIdtVectorInfo), 0 /* cbInstr */, u32ErrCode,
                                     pVCpu->cpum.GstCtx.cr2);

                Log4Func(("IDT: Pending vectoring event %#RX64 Err=%#RX32\n", VCPU_2_VMXSTATE(pVCpu).Event.u64IntInfo,
                          VCPU_2_VMXSTATE(pVCpu).Event.u32ErrCode));
                Assert(rcStrict == VINF_SUCCESS);
                break;
            }

            case IEMXCPTRAISE_REEXEC_INSTR:
                Assert(rcStrict == VINF_SUCCESS);
                break;

            case IEMXCPTRAISE_DOUBLE_FAULT:
            {
                /*
                 * Determine a vectoring double #PF condition. Used later, when PGM evaluates the
                 * second #PF as a guest #PF (and not a shadow #PF) and needs to be converted into a #DF.
                 */
                if (fRaiseInfo & IEMXCPTRAISEINFO_PF_PF)
                {
                    pVmxTransient->fVectoringDoublePF = true;
                    Log4Func(("IDT: Vectoring double #PF %#RX64 cr2=%#RX64\n", VCPU_2_VMXSTATE(pVCpu).Event.u64IntInfo,
                          pVCpu->cpum.GstCtx.cr2));
                    rcStrict = VINF_SUCCESS;
                }
                else
                {
                    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatInjectConvertDF);
                    vmxHCSetPendingXcptDF(pVCpu);
                    Log4Func(("IDT: Pending vectoring #DF %#RX64 uIdtVector=%#x uExitVector=%#x\n", VCPU_2_VMXSTATE(pVCpu).Event.u64IntInfo,
                              uIdtVector, VMX_EXIT_INT_INFO_VECTOR(uExitIntInfo)));
                    rcStrict = VINF_HM_DOUBLE_FAULT;
                }
                break;
            }

            case IEMXCPTRAISE_TRIPLE_FAULT:
            {
                Log4Func(("IDT: Pending vectoring triple-fault uIdt=%#x uExit=%#x\n", uIdtVector,
                          VMX_EXIT_INT_INFO_VECTOR(uExitIntInfo)));
                rcStrict = VINF_EM_RESET;
                break;
            }

            case IEMXCPTRAISE_CPU_HANG:
            {
                Log4Func(("IDT: Bad guest! Entering CPU hang. fRaiseInfo=%#x\n", fRaiseInfo));
                rcStrict = VERR_EM_GUEST_CPU_HANG;
                break;
            }

            default:
            {
                AssertMsgFailed(("IDT: vcpu[%RU32] Unexpected/invalid value! enmRaise=%#x\n", pVCpu->idCpu, enmRaise));
                rcStrict = VERR_VMX_IPE_2;
                break;
            }
        }
    }
    else if (   (pVmcsInfo->u32PinCtls & VMX_PIN_CTLS_VIRT_NMI)
             && !CPUMAreInterruptsInhibitedByNmiEx(&pVCpu->cpum.GstCtx))
    {
        if (    VMX_EXIT_INT_INFO_IS_VALID(uExitIntInfo)
             && VMX_EXIT_INT_INFO_VECTOR(uExitIntInfo) != X86_XCPT_DF
             && VMX_EXIT_INT_INFO_IS_NMI_UNBLOCK_IRET(uExitIntInfo))
        {
            /*
             * Execution of IRET caused a fault when NMI blocking was in effect (i.e we're in
             * the guest or nested-guest NMI handler). We need to set the block-by-NMI field so
             * that virtual NMIs remain blocked until the IRET execution is completed.
             *
             * See Intel spec. 31.7.1.2 "Resuming Guest Software After Handling An Exception".
             */
            CPUMSetInterruptInhibitingByNmiEx(&pVCpu->cpum.GstCtx);
            Log4Func(("Set NMI blocking. uExitReason=%u\n", pVmxTransient->uExitReason));
        }
        else if (   pVmxTransient->uExitReason == VMX_EXIT_EPT_VIOLATION
                 || pVmxTransient->uExitReason == VMX_EXIT_PML_FULL
                 || pVmxTransient->uExitReason == VMX_EXIT_SPP_EVENT)
        {
            /*
             * Execution of IRET caused an EPT violation, page-modification log-full event or
             * SPP-related event VM-exit when NMI blocking was in effect (i.e. we're in the
             * guest or nested-guest NMI handler). We need to set the block-by-NMI field so
             * that virtual NMIs remain blocked until the IRET execution is completed.
             *
             * See Intel spec. 27.2.3 "Information about NMI unblocking due to IRET"
             */
            if (VMX_EXIT_QUAL_EPT_IS_NMI_UNBLOCK_IRET(pVmxTransient->uExitQual))
            {
                CPUMSetInterruptInhibitingByNmiEx(&pVCpu->cpum.GstCtx);
                Log4Func(("Set NMI blocking. uExitReason=%u\n", pVmxTransient->uExitReason));
            }
        }
    }

    Assert(   rcStrict == VINF_SUCCESS  || rcStrict == VINF_HM_DOUBLE_FAULT
           || rcStrict == VINF_EM_RESET || rcStrict == VERR_EM_GUEST_CPU_HANG);
    return rcStrict;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/**
 * Perform the relevant VMX instruction checks for VM-exits that occurred due to the
 * guest attempting to execute a VMX instruction.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @retval  VINF_SUCCESS if we should continue handling the VM-exit.
 * @retval  VINF_HM_PENDING_XCPT if an exception was raised.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason.
 *
 * @todo    NSTVMX: Document other error codes when VM-exit is implemented.
 * @remarks No-long-jump zone!!!
 */
static VBOXSTRICTRC vmxHCCheckExitDueToVmxInstr(PVMCPUCC pVCpu, uint32_t uExitReason)
{
    HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_SS
                              | CPUMCTX_EXTRN_CS  | CPUMCTX_EXTRN_EFER);

    /*
     * The physical CPU would have already checked the CPU mode/code segment.
     * We shall just assert here for paranoia.
     * See Intel spec. 25.1.1 "Relative Priority of Faults and VM Exits".
     */
    Assert(!CPUMIsGuestInRealOrV86ModeEx(&pVCpu->cpum.GstCtx));
    Assert(   !CPUMIsGuestInLongModeEx(&pVCpu->cpum.GstCtx)
           ||  CPUMIsGuestIn64BitCodeEx(&pVCpu->cpum.GstCtx));

    if (uExitReason == VMX_EXIT_VMXON)
    {
        HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4);

        /*
         * We check CR4.VMXE because it is required to be always set while in VMX operation
         * by physical CPUs and our CR4 read-shadow is only consulted when executing specific
         * instructions (CLTS, LMSW, MOV CR, and SMSW) and thus doesn't affect CPU operation
         * otherwise (i.e. physical CPU won't automatically #UD if Cr4Shadow.VMXE is 0).
         */
        if (!CPUMIsGuestVmxEnabled(&pVCpu->cpum.GstCtx))
        {
            Log4Func(("CR4.VMXE is not set -> #UD\n"));
            vmxHCSetPendingXcptUD(pVCpu);
            return VINF_HM_PENDING_XCPT;
        }
    }
    else if (!CPUMIsGuestInVmxRootMode(&pVCpu->cpum.GstCtx))
    {
        /*
         * The guest has not entered VMX operation but attempted to execute a VMX instruction
         * (other than VMXON), we need to raise a #UD.
         */
        Log4Func(("Not in VMX root mode -> #UD\n"));
        vmxHCSetPendingXcptUD(pVCpu);
        return VINF_HM_PENDING_XCPT;
    }

    /* All other checks (including VM-exit intercepts) are handled by IEM instruction emulation. */
    return VINF_SUCCESS;
}


/**
 * Decodes the memory operand of an instruction that caused a VM-exit.
 *
 * The Exit qualification field provides the displacement field for memory
 * operand instructions, if any.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @retval  VINF_SUCCESS if the operand was successfully decoded.
 * @retval  VINF_HM_PENDING_XCPT if an exception was raised while decoding the
 *          operand.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitInstrInfo  The VM-exit instruction information field.
 * @param   enmMemAccess    The memory operand's access type (read or write).
 * @param   GCPtrDisp       The instruction displacement field, if any. For
 *                          RIP-relative addressing pass RIP + displacement here.
 * @param   pGCPtrMem       Where to store the effective destination memory address.
 *
 * @remarks Warning! This function ASSUMES the instruction cannot be used in real or
 *          virtual-8086 mode hence skips those checks while verifying if the
 *          segment is valid.
 */
static VBOXSTRICTRC vmxHCDecodeMemOperand(PVMCPUCC pVCpu, uint32_t uExitInstrInfo, RTGCPTR GCPtrDisp, VMXMEMACCESS enmMemAccess,
                                            PRTGCPTR pGCPtrMem)
{
    Assert(pGCPtrMem);
    Assert(!CPUMIsGuestInRealOrV86Mode(pVCpu));
    HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RSP | CPUMCTX_EXTRN_SREG_MASK | CPUMCTX_EXTRN_EFER
                              | CPUMCTX_EXTRN_CR0);

    static uint64_t const s_auAddrSizeMasks[]   = { UINT64_C(0xffff), UINT64_C(0xffffffff), UINT64_C(0xffffffffffffffff) };
    static uint64_t const s_auAccessSizeMasks[] = { sizeof(uint16_t), sizeof(uint32_t), sizeof(uint64_t) };
    AssertCompile(RT_ELEMENTS(s_auAccessSizeMasks) == RT_ELEMENTS(s_auAddrSizeMasks));

    VMXEXITINSTRINFO ExitInstrInfo;
    ExitInstrInfo.u = uExitInstrInfo;
    uint8_t const   uAddrSize     =  ExitInstrInfo.All.u3AddrSize;
    uint8_t const   iSegReg       =  ExitInstrInfo.All.iSegReg;
    bool const      fIdxRegValid  = !ExitInstrInfo.All.fIdxRegInvalid;
    uint8_t const   iIdxReg       =  ExitInstrInfo.All.iIdxReg;
    uint8_t const   uScale        =  ExitInstrInfo.All.u2Scaling;
    bool const      fBaseRegValid = !ExitInstrInfo.All.fBaseRegInvalid;
    uint8_t const   iBaseReg      =  ExitInstrInfo.All.iBaseReg;
    bool const      fIsMemOperand = !ExitInstrInfo.All.fIsRegOperand;
    bool const      fIsLongMode   =  CPUMIsGuestInLongModeEx(&pVCpu->cpum.GstCtx);

    /*
     * Validate instruction information.
     * This shouldn't happen on real hardware but useful while testing our nested hardware-virtualization code.
     */
    AssertLogRelMsgReturn(uAddrSize < RT_ELEMENTS(s_auAddrSizeMasks),
                          ("Invalid address size. ExitInstrInfo=%#RX32\n", ExitInstrInfo.u), VERR_VMX_IPE_1);
    AssertLogRelMsgReturn(iSegReg  < X86_SREG_COUNT,
                          ("Invalid segment register. ExitInstrInfo=%#RX32\n", ExitInstrInfo.u), VERR_VMX_IPE_2);
    AssertLogRelMsgReturn(fIsMemOperand,
                          ("Expected memory operand. ExitInstrInfo=%#RX32\n", ExitInstrInfo.u), VERR_VMX_IPE_3);

    /*
     * Compute the complete effective address.
     *
     * See AMD instruction spec. 1.4.2 "SIB Byte Format"
     * See AMD spec. 4.5.2 "Segment Registers".
     */
    RTGCPTR GCPtrMem = GCPtrDisp;
    if (fBaseRegValid)
        GCPtrMem += pVCpu->cpum.GstCtx.aGRegs[iBaseReg].u64;
    if (fIdxRegValid)
        GCPtrMem += pVCpu->cpum.GstCtx.aGRegs[iIdxReg].u64 << uScale;

    RTGCPTR const GCPtrOff = GCPtrMem;
    if (   !fIsLongMode
        || iSegReg >= X86_SREG_FS)
        GCPtrMem += pVCpu->cpum.GstCtx.aSRegs[iSegReg].u64Base;
    GCPtrMem &= s_auAddrSizeMasks[uAddrSize];

    /*
     * Validate effective address.
     * See AMD spec. 4.5.3 "Segment Registers in 64-Bit Mode".
     */
    uint8_t const cbAccess = s_auAccessSizeMasks[uAddrSize];
    Assert(cbAccess > 0);
    if (fIsLongMode)
    {
        if (X86_IS_CANONICAL(GCPtrMem))
        {
            *pGCPtrMem = GCPtrMem;
            return VINF_SUCCESS;
        }

        /** @todo r=ramshankar: We should probably raise \#SS or \#GP. See AMD spec. 4.12.2
         *        "Data Limit Checks in 64-bit Mode". */
        Log4Func(("Long mode effective address is not canonical GCPtrMem=%#RX64\n", GCPtrMem));
        vmxHCSetPendingXcptGP(pVCpu, 0);
        return VINF_HM_PENDING_XCPT;
    }

    /*
     * This is a watered down version of iemMemApplySegment().
     * Parts that are not applicable for VMX instructions like real-or-v8086 mode
     * and segment CPL/DPL checks are skipped.
     */
    RTGCPTR32 const GCPtrFirst32 = (RTGCPTR32)GCPtrOff;
    RTGCPTR32 const GCPtrLast32  = GCPtrFirst32 + cbAccess - 1;
    PCCPUMSELREG    pSel         = &pVCpu->cpum.GstCtx.aSRegs[iSegReg];

    /* Check if the segment is present and usable. */
    if (    pSel->Attr.n.u1Present
        && !pSel->Attr.n.u1Unusable)
    {
        Assert(pSel->Attr.n.u1DescType);
        if (!(pSel->Attr.n.u4Type & X86_SEL_TYPE_CODE))
        {
            /* Check permissions for the data segment. */
            if (   enmMemAccess == VMXMEMACCESS_WRITE
                && !(pSel->Attr.n.u4Type & X86_SEL_TYPE_WRITE))
            {
                Log4Func(("Data segment access invalid. iSegReg=%#x Attr=%#RX32\n", iSegReg, pSel->Attr.u));
                vmxHCSetPendingXcptGP(pVCpu, iSegReg);
                return VINF_HM_PENDING_XCPT;
            }

            /* Check limits if it's a normal data segment. */
            if (!(pSel->Attr.n.u4Type & X86_SEL_TYPE_DOWN))
            {
                if (   GCPtrFirst32 > pSel->u32Limit
                    || GCPtrLast32  > pSel->u32Limit)
                {
                    Log4Func(("Data segment limit exceeded. "
                              "iSegReg=%#x GCPtrFirst32=%#RX32 GCPtrLast32=%#RX32 u32Limit=%#RX32\n", iSegReg, GCPtrFirst32,
                              GCPtrLast32, pSel->u32Limit));
                    if (iSegReg == X86_SREG_SS)
                        vmxHCSetPendingXcptSS(pVCpu, 0);
                    else
                        vmxHCSetPendingXcptGP(pVCpu, 0);
                    return VINF_HM_PENDING_XCPT;
                }
            }
            else
            {
               /* Check limits if it's an expand-down data segment.
                  Note! The upper boundary is defined by the B bit, not the G bit! */
               if (   GCPtrFirst32 < pSel->u32Limit + UINT32_C(1)
                   || GCPtrLast32  > (pSel->Attr.n.u1DefBig ? UINT32_MAX : UINT32_C(0xffff)))
               {
                   Log4Func(("Expand-down data segment limit exceeded. "
                             "iSegReg=%#x GCPtrFirst32=%#RX32 GCPtrLast32=%#RX32 u32Limit=%#RX32\n", iSegReg, GCPtrFirst32,
                             GCPtrLast32, pSel->u32Limit));
                   if (iSegReg == X86_SREG_SS)
                       vmxHCSetPendingXcptSS(pVCpu, 0);
                   else
                       vmxHCSetPendingXcptGP(pVCpu, 0);
                   return VINF_HM_PENDING_XCPT;
               }
            }
        }
        else
        {
            /* Check permissions for the code segment. */
            if (   enmMemAccess == VMXMEMACCESS_WRITE
                || (   enmMemAccess == VMXMEMACCESS_READ
                    && !(pSel->Attr.n.u4Type & X86_SEL_TYPE_READ)))
            {
                Log4Func(("Code segment access invalid. Attr=%#RX32\n", pSel->Attr.u));
                Assert(!CPUMIsGuestInRealOrV86ModeEx(&pVCpu->cpum.GstCtx));
                vmxHCSetPendingXcptGP(pVCpu, 0);
                return VINF_HM_PENDING_XCPT;
            }

            /* Check limits for the code segment (normal/expand-down not applicable for code segments). */
            if (   GCPtrFirst32 > pSel->u32Limit
                || GCPtrLast32  > pSel->u32Limit)
            {
                Log4Func(("Code segment limit exceeded. GCPtrFirst32=%#RX32 GCPtrLast32=%#RX32 u32Limit=%#RX32\n",
                          GCPtrFirst32, GCPtrLast32, pSel->u32Limit));
                if (iSegReg == X86_SREG_SS)
                    vmxHCSetPendingXcptSS(pVCpu, 0);
                else
                    vmxHCSetPendingXcptGP(pVCpu, 0);
                return VINF_HM_PENDING_XCPT;
            }
        }
    }
    else
    {
        Log4Func(("Not present or unusable segment. iSegReg=%#x Attr=%#RX32\n", iSegReg, pSel->Attr.u));
        vmxHCSetPendingXcptGP(pVCpu, 0);
        return VINF_HM_PENDING_XCPT;
    }

    *pGCPtrMem = GCPtrMem;
    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */


/**
 * VM-exit helper for LMSW.
 */
static VBOXSTRICTRC vmxHCExitLmsw(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, uint8_t cbInstr, uint16_t uMsw, RTGCPTR GCPtrEffDst)
{
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedLmsw(pVCpu, cbInstr, uMsw, GCPtrEffDst);
    AssertMsg(   rcStrict == VINF_SUCCESS
              || rcStrict == VINF_IEM_RAISED_XCPT, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

    ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_CR0);
    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }

    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitLmsw);
    Log4Func(("rcStrict=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * VM-exit helper for CLTS.
 */
static VBOXSTRICTRC vmxHCExitClts(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, uint8_t cbInstr)
{
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedClts(pVCpu, cbInstr);
    AssertMsg(   rcStrict == VINF_SUCCESS
              || rcStrict == VINF_IEM_RAISED_XCPT, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

    ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_CR0);
    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }

    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitClts);
    Log4Func(("rcStrict=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * VM-exit helper for MOV from CRx (CRx read).
 */
static VBOXSTRICTRC vmxHCExitMovFromCrX(PVMCPUCC pVCpu, PVMXVMCSINFO pVmcsInfo, uint8_t cbInstr, uint8_t iGReg, uint8_t iCrReg)
{
    Assert(iCrReg < 16);
    Assert(iGReg < RT_ELEMENTS(pVCpu->cpum.GstCtx.aGRegs));

    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedMovCRxRead(pVCpu, cbInstr, iGReg, iCrReg);
    AssertMsg(   rcStrict == VINF_SUCCESS
              || rcStrict == VINF_IEM_RAISED_XCPT, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

    if (iGReg == X86_GREG_xSP)
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_RSP);
    else
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
#ifdef VBOX_WITH_STATISTICS
    switch (iCrReg)
    {
        case 0: STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR0Read); break;
        case 2: STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR2Read); break;
        case 3: STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR3Read); break;
        case 4: STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR4Read); break;
        case 8: STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR8Read); break;
    }
#endif
    Log4Func(("CR%d Read access rcStrict=%Rrc\n", iCrReg, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * VM-exit helper for MOV to CRx (CRx write).
 */
static VBOXSTRICTRC vmxHCExitMovToCrX(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iGReg, uint8_t iCrReg)
{
    HMVMX_CPUMCTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);

    VBOXSTRICTRC rcStrict = IEMExecDecodedMovCRxWrite(pVCpu, cbInstr, iCrReg, iGReg);
    AssertMsg(   rcStrict == VINF_SUCCESS
              || rcStrict == VINF_IEM_RAISED_XCPT
              || rcStrict == VINF_PGM_SYNC_CR3, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

    switch (iCrReg)
    {
        case 0:
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_CR0
                                                     | HM_CHANGED_GUEST_EFER_MSR | HM_CHANGED_VMX_ENTRY_EXIT_CTLS);
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR0Write);
            Log4Func(("CR0 write. rcStrict=%Rrc CR0=%#RX64\n", VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cr0));
            break;

        case 2:
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR2Write);
            /* Nothing to do here, CR2 it's not part of the VMCS. */
            break;

        case 3:
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_CR3);
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR3Write);
            Log4Func(("CR3 write. rcStrict=%Rrc CR3=%#RX64\n", VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cr3));
            break;

        case 4:
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_CR4);
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR4Write);
#ifndef IN_NEM_DARWIN
            Log4Func(("CR4 write. rc=%Rrc CR4=%#RX64 fLoadSaveGuestXcr0=%u\n", VBOXSTRICTRC_VAL(rcStrict),
                      pVCpu->cpum.GstCtx.cr4, pVCpu->hmr0.s.fLoadSaveGuestXcr0));
#else
            Log4Func(("CR4 write. rc=%Rrc CR4=%#RX64\n", VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cr4));
#endif
            break;

        case 8:
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged,
                             HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_APIC_TPR);
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitCR8Write);
            break;

        default:
            AssertMsgFailed(("Invalid CRx register %#x\n", iCrReg));
            break;
    }

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit exception handler for \#PF (Page-fault exception).
 *
 * @remarks Requires all fields in HMVMX_READ_XCPT_INFO to be read from the VMCS.
 */
static VBOXSTRICTRC vmxHCExitXcptPF(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);

#ifndef IN_NEM_DARWIN
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (!VM_IS_VMX_NESTED_PAGING(pVM))
    { /* likely */ }
    else
#endif
    {
#if !defined(HMVMX_ALWAYS_TRAP_ALL_XCPTS) && !defined(HMVMX_ALWAYS_TRAP_PF) && !defined(IN_NEM_DARWIN)
        Assert(pVmxTransient->fIsNestedGuest || pVCpu->hmr0.s.fUsingDebugLoop);
#endif
        VCPU_2_VMXSTATE(pVCpu).Event.fPending = false;                  /* In case it's a contributory or vectoring #PF. */
        if (!pVmxTransient->fVectoringDoublePF)
        {
            vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(pVmxTransient->uExitIntInfo), 0 /* cbInstr */,
                                   pVmxTransient->uExitIntErrorCode, pVmxTransient->uExitQual);
        }
        else
        {
            /* A guest page-fault occurred during delivery of a page-fault. Inject #DF. */
            Assert(!pVmxTransient->fIsNestedGuest);
            vmxHCSetPendingXcptDF(pVCpu);
            Log4Func(("Pending #DF due to vectoring #PF w/ NestedPaging\n"));
        }
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestPF);
        return VINF_SUCCESS;
    }

    Assert(!pVmxTransient->fIsNestedGuest);

    /* If it's a vectoring #PF, emulate injecting the original event injection as PGMTrap0eHandler() is incapable
       of differentiating between instruction emulation and event injection that caused a #PF. See @bugref{6607}. */
    if (pVmxTransient->fVectoringPF)
    {
        Assert(VCPU_2_VMXSTATE(pVCpu).Event.fPending);
        return VINF_EM_RAW_INJECT_TRPM_EVENT;
    }

    int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    Log4Func(("#PF: cs:rip=%#04x:%08RX64 err_code=%#RX32 exit_qual=%#RX64 cr3=%#RX64\n", pVCpu->cpum.GstCtx.cs.Sel,
              pVCpu->cpum.GstCtx.rip, pVmxTransient->uExitIntErrorCode, pVmxTransient->uExitQual, pVCpu->cpum.GstCtx.cr3));

    TRPMAssertXcptPF(pVCpu, pVmxTransient->uExitQual, (RTGCUINT)pVmxTransient->uExitIntErrorCode);
    rc = PGMTrap0eHandler(pVCpu, pVmxTransient->uExitIntErrorCode, &pVCpu->cpum.GstCtx, (RTGCPTR)pVmxTransient->uExitQual);

    Log4Func(("#PF: rc=%Rrc\n", rc));
    if (rc == VINF_SUCCESS)
    {
        /*
         * This is typically a shadow page table sync or a MMIO instruction. But we may have
         * emulated something like LTR or a far jump. Any part of the CPU context may have changed.
         */
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);
        TRPMResetTrap(pVCpu);
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitShadowPF);
        return rc;
    }

    if (rc == VINF_EM_RAW_GUEST_TRAP)
    {
        if (!pVmxTransient->fVectoringDoublePF)
        {
            /* It's a guest page fault and needs to be reflected to the guest. */
            uint32_t const uGstErrorCode = TRPMGetErrorCode(pVCpu);
            TRPMResetTrap(pVCpu);
            VCPU_2_VMXSTATE(pVCpu).Event.fPending = false;                 /* In case it's a contributory #PF. */
            vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(pVmxTransient->uExitIntInfo), 0 /* cbInstr */,
                                   uGstErrorCode, pVmxTransient->uExitQual);
        }
        else
        {
            /* A guest page-fault occurred during delivery of a page-fault. Inject #DF. */
            TRPMResetTrap(pVCpu);
            VCPU_2_VMXSTATE(pVCpu).Event.fPending = false;     /* Clear pending #PF to replace it with #DF. */
            vmxHCSetPendingXcptDF(pVCpu);
            Log4Func(("#PF: Pending #DF due to vectoring #PF\n"));
        }

        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestPF);
        return VINF_SUCCESS;
    }

    TRPMResetTrap(pVCpu);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitShadowPFEM);
    return rc;
}


/**
 * VM-exit exception handler for \#MF (Math Fault: floating point exception).
 *
 * @remarks Requires all fields in HMVMX_READ_XCPT_INFO to be read from the VMCS.
 */
static VBOXSTRICTRC vmxHCExitXcptMF(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestMF);

    int rc = vmxHCImportGuestState<CPUMCTX_EXTRN_CR0>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_NE))
    {
        /* Convert a #MF into a FERR -> IRQ 13. See @bugref{6117}. */
        rc = PDMIsaSetIrq(pVCpu->CTX_SUFF(pVM), 13, 1, 0 /* uTagSrc */);

        /** @todo r=ramshankar: The Intel spec. does -not- specify that this VM-exit
         *        provides VM-exit instruction length. If this causes problem later,
         *        disassemble the instruction like it's done on AMD-V. */
        int rc2 = vmxHCAdvanceGuestRip(pVCpu, pVmxTransient);
        AssertRCReturn(rc2, rc2);
        return rc;
    }

    vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(pVmxTransient->uExitIntInfo), pVmxTransient->cbExitInstr,
                           pVmxTransient->uExitIntErrorCode, 0 /* GCPtrFaultAddress */);
    return VINF_SUCCESS;
}


/**
 * VM-exit exception handler for \#BP (Breakpoint exception).
 *
 * @remarks Requires all fields in HMVMX_READ_XCPT_INFO to be read from the VMCS.
 */
static VBOXSTRICTRC vmxHCExitXcptBP(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestBP);

    int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict;
    if (!pVmxTransient->fIsNestedGuest)
        rcStrict = DBGFTrap03Handler(pVCpu->CTX_SUFF(pVM), pVCpu, &pVCpu->cpum.GstCtx);
    else
        rcStrict = VINF_EM_RAW_GUEST_TRAP;

    if (rcStrict == VINF_EM_RAW_GUEST_TRAP)
    {
        vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(pVmxTransient->uExitIntInfo),
                               pVmxTransient->cbExitInstr, pVmxTransient->uExitIntErrorCode, 0 /* GCPtrFaultAddress */);
        rcStrict = VINF_SUCCESS;
    }

    Assert(rcStrict == VINF_SUCCESS || rcStrict == VINF_EM_DBG_BREAKPOINT);
    return rcStrict;
}


/**
 * VM-exit helper for split-lock access triggered \#AC exceptions.
 */
static VBOXSTRICTRC vmxHCHandleSplitLockAcXcpt(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    /*
     * Check for debug/trace events and import state accordingly.
     */
    if (!pVmxTransient->fIsNestedGuest)
        STAM_REL_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestACSplitLock);
    else
        STAM_REL_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatNestedExitACSplitLock);
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (   !DBGF_IS_EVENT_ENABLED(pVM, DBGFEVENT_VMX_SPLIT_LOCK)
#ifndef IN_NEM_DARWIN
        && !VBOXVMM_VMX_SPLIT_LOCK_ENABLED()
#endif
        )
    {
        if (pVM->cCpus == 1)
        {
#if 0 /** @todo r=bird: This is potentially wrong.  Might have to just do a whole state sync above and mark everything changed to be safe... */
            int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK,
                                           HMVMX_CPUMCTX_XPCT_AC>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
#else
            int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL,
                                           HMVMX_CPUMCTX_XPCT_AC>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
#endif
            AssertRCReturn(rc, rc);
        }
    }
    else
    {
        int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL,
                                       HMVMX_CPUMCTX_XPCT_AC>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
        AssertRCReturn(rc, rc);

        VBOXVMM_XCPT_DF(pVCpu, &pVCpu->cpum.GstCtx);

        if (DBGF_IS_EVENT_ENABLED(pVM, DBGFEVENT_VMX_SPLIT_LOCK))
        {
            VBOXSTRICTRC rcStrict = DBGFEventGenericWithArgs(pVM, pVCpu, DBGFEVENT_VMX_SPLIT_LOCK, DBGFEVENTCTX_HM, 0);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
        }
    }

    /*
     * Emulate the instruction.
     *
     * We have to ignore the LOCK prefix here as we must not retrigger the
     * detection on the host.  This isn't all that satisfactory, though...
     */
    if (pVM->cCpus == 1)
    {
        Log8Func(("cs:rip=%#04x:%08RX64 rflags=%#RX64 cr0=%#RX64 split-lock #AC\n", pVCpu->cpum.GstCtx.cs.Sel,
                  pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags, pVCpu->cpum.GstCtx.cr0));

        /** @todo For SMP configs we should do a rendezvous here. */
        VBOXSTRICTRC rcStrict = IEMExecOneIgnoreLock(pVCpu);
        if (rcStrict == VINF_SUCCESS)
#if 0 /** @todo r=bird: This is potentially wrong.  Might have to just do a whole state sync above and mark everything changed to be safe... */
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged,
                               HM_CHANGED_GUEST_RIP
                             | HM_CHANGED_GUEST_RFLAGS
                             | HM_CHANGED_GUEST_GPRS_MASK
                             | HM_CHANGED_GUEST_CS
                             | HM_CHANGED_GUEST_SS);
#else
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);
#endif
        else if (rcStrict == VINF_IEM_RAISED_XCPT)
        {
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
            rcStrict = VINF_SUCCESS;
        }
        return rcStrict;
    }
    Log8Func(("cs:rip=%#04x:%08RX64 rflags=%#RX64 cr0=%#RX64 split-lock #AC -> VINF_EM_EMULATE_SPLIT_LOCK\n",
              pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags, pVCpu->cpum.GstCtx.cr0));
    return VINF_EM_EMULATE_SPLIT_LOCK;
}


/**
 * VM-exit exception handler for \#AC (Alignment-check exception).
 *
 * @remarks Requires all fields in HMVMX_READ_XCPT_INFO to be read from the VMCS.
 */
static VBOXSTRICTRC vmxHCExitXcptAC(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /*
     * Detect #ACs caused by host having enabled split-lock detection.
     * Emulate such instructions.
     */
    int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_XPCT_AC>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);
    /** @todo detect split lock in cpu feature?   */
    /** @todo r=ramshankar: is cpu feature detection really necessary since we are able
     *        to detect the split-lock \#AC condition without it? More so since the
     *        feature isn't cleanly detectable, see @bugref{10318#c125}. */
    if (vmxHCIsSplitLockAcXcpt(pVCpu))
        return vmxHCHandleSplitLockAcXcpt(pVCpu, pVmxTransient);

    STAM_REL_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestAC);
    Log8Func(("cs:rip=%#04x:%08RX64 rflags=%#RX64 cr0=%#RX64 cpl=%d -> #AC\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              pVCpu->cpum.GstCtx.rflags, pVCpu->cpum.GstCtx.cr0, CPUMGetGuestCPL(pVCpu) ));

    /* Re-inject it. We'll detect any nesting before getting here. */
    vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(pVmxTransient->uExitIntInfo),
                           pVmxTransient->cbExitInstr, pVmxTransient->uExitIntErrorCode, 0 /* GCPtrFaultAddress */);
    return VINF_SUCCESS;
}


/**
 * VM-exit exception handler for \#DB (Debug exception).
 *
 * @remarks Requires all fields in HMVMX_READ_XCPT_INFO to be read from the VMCS.
 */
static VBOXSTRICTRC vmxHCExitXcptDB(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestDB);

    /*
     * Get the DR6-like values from the Exit qualification and pass it to DBGF for processing.
     */
    vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);

    /* Refer Intel spec. Table 27-1. "Exit Qualifications for debug exceptions" for the format. */
    uint64_t const uDR6 = X86_DR6_INIT_VAL
                        | (pVmxTransient->uExitQual & (  X86_DR6_B0 | X86_DR6_B1 | X86_DR6_B2 | X86_DR6_B3
                                                       | X86_DR6_BD | X86_DR6_BS));
    Log6Func(("uDR6=%#RX64 uExitQual=%#RX64\n", uDR6, pVmxTransient->uExitQual));

    int rc;
    if (!pVmxTransient->fIsNestedGuest)
    {
        rc = DBGFTrap01Handler(pVCpu->CTX_SUFF(pVM), pVCpu, &pVCpu->cpum.GstCtx, uDR6, VCPU_2_VMXSTATE(pVCpu).fSingleInstruction);

        /*
         * Prevents stepping twice over the same instruction when the guest is stepping using
         * EFLAGS.TF and the hypervisor debugger is stepping using MTF.
         * Testcase: DOSQEMM, break (using "ba x 1") at cs:rip 0x70:0x774 and step (using "t").
         */
        if (   rc == VINF_EM_DBG_STEPPED
            && (pVmxTransient->pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_MONITOR_TRAP_FLAG))
        {
            Assert(VCPU_2_VMXSTATE(pVCpu).fSingleInstruction);
            rc = VINF_EM_RAW_GUEST_TRAP;
        }
    }
    else
        rc = VINF_EM_RAW_GUEST_TRAP;
    Log6Func(("rc=%Rrc\n", rc));
    if (rc == VINF_EM_RAW_GUEST_TRAP)
    {
        /*
         * The exception was for the guest.  Update DR6, DR7.GD and
         * IA32_DEBUGCTL.LBR before forwarding it.
         * See Intel spec. 27.1 "Architectural State before a VM-Exit"
         * and @sdmv3{077,622,17.2.3,Debug Status Register (DR6)}.
         */
#ifndef IN_NEM_DARWIN
        VMMRZCallRing3Disable(pVCpu);
        HM_DISABLE_PREEMPT(pVCpu);

        pVCpu->cpum.GstCtx.dr[6] &= ~X86_DR6_B_MASK;
        pVCpu->cpum.GstCtx.dr[6] |= uDR6;
        if (CPUMIsGuestDebugStateActive(pVCpu))
            ASMSetDR6(pVCpu->cpum.GstCtx.dr[6]);

        HM_RESTORE_PREEMPT();
        VMMRZCallRing3Enable(pVCpu);
#else
        /** @todo */
#endif

        rc = vmxHCImportGuestState<CPUMCTX_EXTRN_DR7>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
        AssertRCReturn(rc, rc);

        /* X86_DR7_GD will be cleared if DRx accesses should be trapped inside the guest. */
        pVCpu->cpum.GstCtx.dr[7] &= ~(uint64_t)X86_DR7_GD;

        /* Paranoia. */
        pVCpu->cpum.GstCtx.dr[7] &= ~(uint64_t)X86_DR7_RAZ_MASK;
        pVCpu->cpum.GstCtx.dr[7] |= X86_DR7_RA1_MASK;

        rc = VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_GUEST_DR7, pVCpu->cpum.GstCtx.dr[7]);
        AssertRC(rc);

        /*
         * Raise #DB in the guest.
         *
         * It is important to reflect exactly what the VM-exit gave us (preserving the
         * interruption-type) rather than use vmxHCSetPendingXcptDB() as the #DB could've
         * been raised while executing ICEBP (INT1) and not the regular #DB. Thus it may
         * trigger different handling in the CPU (like skipping DPL checks), see @bugref{6398}.
         *
         * Intel re-documented ICEBP/INT1 on May 2018 previously documented as part of
         * Intel 386, see Intel spec. 24.8.3 "VM-Entry Controls for Event Injection".
         */
        vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(pVmxTransient->uExitIntInfo),
                             pVmxTransient->cbExitInstr, pVmxTransient->uExitIntErrorCode, 0 /* GCPtrFaultAddress */);
        return VINF_SUCCESS;
    }

    /*
     * Not a guest trap, must be a hypervisor related debug event then.
     * Update DR6 in case someone is interested in it.
     */
    AssertMsg(rc == VINF_EM_DBG_STEPPED || rc == VINF_EM_DBG_BREAKPOINT, ("%Rrc\n", rc));
    AssertReturn(pVmxTransient->fWasHyperDebugStateActive, VERR_HM_IPE_5);
    CPUMSetHyperDR6(pVCpu, uDR6);

    return rc;
}


/**
 * Hacks its way around the lovely mesa driver's backdoor accesses.
 *
 * @sa hmR0SvmHandleMesaDrvGp.
 */
static int vmxHCHandleMesaDrvGp(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient, PCPUMCTX pCtx)
{
    LogFunc(("cs:rip=%#04x:%08RX64 rcx=%#RX64 rbx=%#RX64\n", pCtx->cs.Sel, pCtx->rip, pCtx->rcx, pCtx->rbx));
    RT_NOREF(pCtx);

    /* For now we'll just skip the instruction. */
    return vmxHCAdvanceGuestRip(pVCpu, pVmxTransient);
}


/**
 * Checks if the \#GP'ing instruction is the mesa driver doing it's lovely
 * backdoor logging w/o checking what it is running inside.
 *
 * This recognizes an "IN EAX,DX" instruction executed in flat ring-3, with the
 * backdoor port and magic numbers loaded in registers.
 *
 * @returns true if it is, false if it isn't.
 * @sa      hmR0SvmIsMesaDrvGp.
 */
DECLINLINE(bool) vmxHCIsMesaDrvGp(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient, PCPUMCTX pCtx)
{
    /* 0xed:  IN eAX,dx */
    uint8_t abInstr[1];
    if (pVmxTransient->cbExitInstr != sizeof(abInstr))
        return false;

    /* Check that it is #GP(0). */
    if (pVmxTransient->uExitIntErrorCode != 0)
        return false;

    /* Check magic and port. */
    Assert(!(pCtx->fExtrn & (CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_RCX)));
    /*Log(("vmxHCIsMesaDrvGp: rax=%RX64 rdx=%RX64\n", pCtx->rax, pCtx->rdx));*/
    if (pCtx->rax != UINT32_C(0x564d5868))
        return false;
    if (pCtx->dx != UINT32_C(0x5658))
        return false;

    /* Flat ring-3 CS. */
    AssertCompile(HMVMX_CPUMCTX_EXTRN_ALL & CPUMCTX_EXTRN_CS);
    Assert(!(pCtx->fExtrn & CPUMCTX_EXTRN_CS));
    /*Log(("vmxHCIsMesaDrvGp: cs.Attr.n.u2Dpl=%d base=%Rx64\n", pCtx->cs.Attr.n.u2Dpl, pCtx->cs.u64Base));*/
    if (pCtx->cs.Attr.n.u2Dpl != 3)
        return false;
    if (pCtx->cs.u64Base != 0)
        return false;

    /* Check opcode. */
    AssertCompile(HMVMX_CPUMCTX_EXTRN_ALL & CPUMCTX_EXTRN_RIP);
    Assert(!(pCtx->fExtrn & CPUMCTX_EXTRN_RIP));
    int rc = PGMPhysSimpleReadGCPtr(pVCpu, abInstr, pCtx->rip, sizeof(abInstr));
    /*Log(("vmxHCIsMesaDrvGp: PGMPhysSimpleReadGCPtr -> %Rrc %#x\n", rc, abInstr[0]));*/
    if (RT_FAILURE(rc))
        return false;
    if (abInstr[0] != 0xed)
        return false;

    return true;
}


/**
 * VM-exit exception handler for \#GP (General-protection exception).
 *
 * @remarks Requires all fields in HMVMX_READ_XCPT_INFO to be read from the VMCS.
 */
static VBOXSTRICTRC vmxHCExitXcptGP(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestGP);

    PCPUMCTX            pCtx            = &pVCpu->cpum.GstCtx;
    PVMXVMCSINFO        pVmcsInfo       = pVmxTransient->pVmcsInfo;
#ifndef IN_NEM_DARWIN
    PVMXVMCSINFOSHARED  pVmcsInfoShared = pVmcsInfo->pShared;
    if (pVmcsInfoShared->RealMode.fRealOnV86Active)
    { /* likely */ }
    else
#endif
    {
#ifndef HMVMX_ALWAYS_TRAP_ALL_XCPTS
# ifndef IN_NEM_DARWIN
        Assert(pVCpu->hmr0.s.fUsingDebugLoop || VCPU_2_VMXSTATE(pVCpu).fTrapXcptGpForLovelyMesaDrv || pVmxTransient->fIsNestedGuest);
# else
        Assert(/*pVCpu->hmr0.s.fUsingDebugLoop ||*/ VCPU_2_VMXSTATE(pVCpu).fTrapXcptGpForLovelyMesaDrv || pVmxTransient->fIsNestedGuest);
# endif
#endif
        /*
         * If the guest is not in real-mode or we have unrestricted guest execution support, or if we are
         * executing a nested-guest, reflect #GP to the guest or nested-guest.
         */
        int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmcsInfo, __FUNCTION__);
        AssertRCReturn(rc, rc);
        Log4Func(("Gst: cs:rip=%#04x:%08RX64 ErrorCode=%#x cr0=%#RX64 cpl=%u tr=%#04x\n", pCtx->cs.Sel, pCtx->rip,
                  pVmxTransient->uExitIntErrorCode, pCtx->cr0, CPUMGetGuestCPL(pVCpu), pCtx->tr.Sel));

        if (    pVmxTransient->fIsNestedGuest
            || !VCPU_2_VMXSTATE(pVCpu).fTrapXcptGpForLovelyMesaDrv
            || !vmxHCIsMesaDrvGp(pVCpu, pVmxTransient, pCtx))
            vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(pVmxTransient->uExitIntInfo),
                                   pVmxTransient->cbExitInstr, pVmxTransient->uExitIntErrorCode, 0 /* GCPtrFaultAddress */);
        else
            rc = vmxHCHandleMesaDrvGp(pVCpu, pVmxTransient, pCtx);
        return rc;
    }

#ifndef IN_NEM_DARWIN
    Assert(CPUMIsGuestInRealModeEx(pCtx));
    Assert(!pVCpu->CTX_SUFF(pVM)->hmr0.s.vmx.fUnrestrictedGuest);
    Assert(!pVmxTransient->fIsNestedGuest);

    int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecOne(pVCpu);
    if (rcStrict == VINF_SUCCESS)
    {
        if (!CPUMIsGuestInRealModeEx(pCtx))
        {
            /*
             * The guest is no longer in real-mode, check if we can continue executing the
             * guest using hardware-assisted VMX. Otherwise, fall back to emulation.
             */
            pVmcsInfoShared->RealMode.fRealOnV86Active = false;
            if (HMCanExecuteVmxGuest(pVCpu->CTX_SUFF(pVM), pVCpu, pCtx))
            {
                Log4Func(("Mode changed but guest still suitable for executing using hardware-assisted VMX\n"));
                ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);
            }
            else
            {
                Log4Func(("Mode changed -> VINF_EM_RESCHEDULE\n"));
                rcStrict = VINF_EM_RESCHEDULE;
            }
        }
        else
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        rcStrict = VINF_SUCCESS;
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
    }
    return VBOXSTRICTRC_VAL(rcStrict);
#endif
}


/**
 * VM-exit exception handler for \#DE (Divide Error).
 *
 * @remarks Requires all fields in HMVMX_READ_XCPT_INFO to be read from the VMCS.
 */
static VBOXSTRICTRC vmxHCExitXcptDE(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestDE);

    int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    if (VCPU_2_VMXSTATE(pVCpu).fGCMTrapXcptDE)
    {
        rc = GCMXcptDE(pVCpu, &pVCpu->cpum.GstCtx);
        Assert(rc == VINF_SUCCESS /* restart instr */ || rc == VERR_NOT_FOUND /* deliver exception */);
    }
    else
        rc = VERR_VMX_UNEXPECTED_INTERRUPTION_EXIT_TYPE;

    /* If the GCM #DE exception handler didn't succeed or wasn't needed, raise #DE. */
    if (RT_FAILURE(rc))
        vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(pVmxTransient->uExitIntInfo),
                             pVmxTransient->cbExitInstr, pVmxTransient->uExitIntErrorCode, 0 /* GCPtrFaultAddress */);
    return VINF_SUCCESS;
}


/**
 * VM-exit exception handler wrapper for all other exceptions that are not handled
 * by a specific handler.
 *
 * This simply re-injects the exception back into the VM without any special
 * processing.
 *
 * @remarks Requires all fields in HMVMX_READ_XCPT_INFO to be read from the VMCS.
 */
static VBOXSTRICTRC vmxHCExitXcptOthers(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_XCPT_HANDLER_PARAMS(pVCpu, pVmxTransient);

#ifndef HMVMX_ALWAYS_TRAP_ALL_XCPTS
# ifndef IN_NEM_DARWIN
    PCVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    AssertMsg(pVCpu->hmr0.s.fUsingDebugLoop || pVmcsInfo->pShared->RealMode.fRealOnV86Active || pVmxTransient->fIsNestedGuest,
              ("uVector=%#x u32XcptBitmap=%#X32\n",
               VMX_EXIT_INT_INFO_VECTOR(pVmxTransient->uExitIntInfo), pVmcsInfo->u32XcptBitmap));
    NOREF(pVmcsInfo);
# endif
#endif

    /*
     * Re-inject the exception into the guest. This cannot be a double-fault condition which
     * would have been handled while checking exits due to event delivery.
     */
    uint8_t const uVector = VMX_EXIT_INT_INFO_VECTOR(pVmxTransient->uExitIntInfo);

#ifdef HMVMX_ALWAYS_TRAP_ALL_XCPTS
    int rc = vmxHCImportGuestState<CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);
    Log4Func(("Reinjecting Xcpt. uVector=%#x cs:rip=%#04x:%08RX64\n", uVector, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
#endif

#ifdef VBOX_WITH_STATISTICS
    switch (uVector)
    {
        case X86_XCPT_DE:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestDE);     break;
        case X86_XCPT_DB:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestDB);     break;
        case X86_XCPT_BP:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestBP);     break;
        case X86_XCPT_OF:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestOF);     break;
        case X86_XCPT_BR:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestBR);     break;
        case X86_XCPT_UD:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestUD);     break;
        case X86_XCPT_NM:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestOF);     break;
        case X86_XCPT_DF:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestDF);     break;
        case X86_XCPT_TS:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestTS);     break;
        case X86_XCPT_NP:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestNP);     break;
        case X86_XCPT_SS:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestSS);     break;
        case X86_XCPT_GP:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestGP);     break;
        case X86_XCPT_PF:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestPF);     break;
        case X86_XCPT_MF:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestMF);     break;
        case X86_XCPT_AC:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestAC);     break;
        case X86_XCPT_XF:   STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestXF);     break;
        default:
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitGuestXcpUnk);
            break;
    }
#endif

    /* We should never call this function for a page-fault, we'd need to pass on the fault address below otherwise. */
    Assert(!VMX_EXIT_INT_INFO_IS_XCPT_PF(pVmxTransient->uExitIntInfo));
    NOREF(uVector);

    /* Re-inject the original exception into the guest. */
    vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_INT_INFO(pVmxTransient->uExitIntInfo),
                           pVmxTransient->cbExitInstr, pVmxTransient->uExitIntErrorCode, 0 /* GCPtrFaultAddress */);
    return VINF_SUCCESS;
}


/**
 * VM-exit exception handler for all exceptions (except NMIs!).
 *
 * @remarks This may be called for both guests and nested-guests. Take care to not
 *          make assumptions and avoid doing anything that is not relevant when
 *          executing a nested-guest (e.g., Mesa driver hacks).
 */
static VBOXSTRICTRC vmxHCExitXcpt(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_ASSERT_READ(pVmxTransient, HMVMX_READ_XCPT_INFO);

    /*
     * If this VM-exit occurred while delivering an event through the guest IDT, take
     * action based on the return code and additional hints (e.g. for page-faults)
     * that will be updated in the VMX transient structure.
     */
    VBOXSTRICTRC rcStrict = vmxHCCheckExitDueToEventDelivery(pVCpu, pVmxTransient);
    if (rcStrict == VINF_SUCCESS)
    {
        /*
         * If an exception caused a VM-exit due to delivery of an event, the original
         * event may have to be re-injected into the guest. We shall reinject it and
         * continue guest execution. However, page-fault is a complicated case and
         * needs additional processing done in vmxHCExitXcptPF().
         */
        Assert(VMX_EXIT_INT_INFO_IS_VALID(pVmxTransient->uExitIntInfo));
        uint8_t const uVector = VMX_EXIT_INT_INFO_VECTOR(pVmxTransient->uExitIntInfo);
        if (   !VCPU_2_VMXSTATE(pVCpu).Event.fPending
            || uVector == X86_XCPT_PF)
        {
            switch (uVector)
            {
                case X86_XCPT_PF: return vmxHCExitXcptPF(pVCpu, pVmxTransient);
                case X86_XCPT_GP: return vmxHCExitXcptGP(pVCpu, pVmxTransient);
                case X86_XCPT_MF: return vmxHCExitXcptMF(pVCpu, pVmxTransient);
                case X86_XCPT_DB: return vmxHCExitXcptDB(pVCpu, pVmxTransient);
                case X86_XCPT_BP: return vmxHCExitXcptBP(pVCpu, pVmxTransient);
                case X86_XCPT_AC: return vmxHCExitXcptAC(pVCpu, pVmxTransient);
                case X86_XCPT_DE: return vmxHCExitXcptDE(pVCpu, pVmxTransient);
                default:
                    return vmxHCExitXcptOthers(pVCpu, pVmxTransient);
            }
        }
        /* else: inject pending event before resuming guest execution. */
    }
    else if (rcStrict == VINF_HM_DOUBLE_FAULT)
    {
        Assert(VCPU_2_VMXSTATE(pVCpu).Event.fPending);
        rcStrict = VINF_SUCCESS;
    }

    return rcStrict;
}
/** @} */


/** @name VM-exit handlers.
 * @{
 */
/* -=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */
/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- VM-exit handlers -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */
/* -=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */

/**
 * VM-exit handler for external interrupts (VMX_EXIT_EXT_INT).
 */
HMVMX_EXIT_DECL vmxHCExitExtInt(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitExtInt);

#ifndef IN_NEM_DARWIN
    /* Windows hosts (32-bit and 64-bit) have DPC latency issues. See @bugref{6853}. */
    if (VMMR0ThreadCtxHookIsEnabled(pVCpu))
        return VINF_SUCCESS;
    return VINF_EM_RAW_INTERRUPT;
#else
    return VINF_SUCCESS;
#endif
}


/**
 * VM-exit handler for exceptions or NMIs (VMX_EXIT_XCPT_OR_NMI). Conditional
 * VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitXcptOrNmi(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_PROFILE_ADV_START(&VCPU_2_VMXSTATS(pVCpu).StatExitXcptNmi, y3);

    vmxHCReadToTransient<HMVMX_READ_EXIT_INTERRUPTION_INFO>(pVCpu, pVmxTransient);

    uint32_t const uExitIntType = VMX_EXIT_INT_INFO_TYPE(pVmxTransient->uExitIntInfo);
    uint8_t const  uVector      = VMX_EXIT_INT_INFO_VECTOR(pVmxTransient->uExitIntInfo);
    Assert(VMX_EXIT_INT_INFO_IS_VALID(pVmxTransient->uExitIntInfo));

    PCVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    Assert(   !(pVmcsInfo->u32ExitCtls & VMX_EXIT_CTLS_ACK_EXT_INT)
           && uExitIntType != VMX_EXIT_INT_INFO_TYPE_EXT_INT);
    NOREF(pVmcsInfo);

    VBOXSTRICTRC rcStrict;
    switch (uExitIntType)
    {
#ifndef IN_NEM_DARWIN /* NMIs should never reach R3. */
        /*
         * Host physical NMIs:
         *     This cannot be a guest NMI as the only way for the guest to receive an NMI is if we
         *     injected it ourselves and anything we inject is not going to cause a VM-exit directly
         *     for the event being injected[1]. Go ahead and dispatch the NMI to the host[2].
         *
         *     See Intel spec. 27.2.3 "Information for VM Exits During Event Delivery".
         *     See Intel spec. 27.5.5 "Updating Non-Register State".
         */
        case VMX_EXIT_INT_INFO_TYPE_NMI:
        {
            rcStrict = hmR0VmxExitHostNmi(pVCpu, pVmcsInfo);
            break;
        }
#endif

        /*
         * Privileged software exceptions (#DB from ICEBP),
         * Software exceptions (#BP and #OF),
         * Hardware exceptions:
         *     Process the required exceptions and resume guest execution if possible.
         */
        case VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT:
            Assert(uVector == X86_XCPT_DB);
            RT_FALL_THRU();
        case VMX_EXIT_INT_INFO_TYPE_SW_XCPT:
            Assert(uVector == X86_XCPT_BP || uVector == X86_XCPT_OF || uExitIntType == VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT);
            RT_FALL_THRU();
        case VMX_EXIT_INT_INFO_TYPE_HW_XCPT:
        {
            NOREF(uVector);
            vmxHCReadToTransient<  HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE
                                 | HMVMX_READ_EXIT_INSTR_LEN
                                 | HMVMX_READ_IDT_VECTORING_INFO
                                 | HMVMX_READ_IDT_VECTORING_ERROR_CODE>(pVCpu, pVmxTransient);
            rcStrict = vmxHCExitXcpt(pVCpu, pVmxTransient);
            break;
        }

        default:
        {
            VCPU_2_VMXSTATE(pVCpu).u32HMError = pVmxTransient->uExitIntInfo;
            rcStrict = VERR_VMX_UNEXPECTED_INTERRUPTION_EXIT_TYPE;
            AssertMsgFailed(("Invalid/unexpected VM-exit interruption info %#x\n", pVmxTransient->uExitIntInfo));
            break;
        }
    }

    STAM_PROFILE_ADV_STOP(&VCPU_2_VMXSTATS(pVCpu).StatExitXcptNmi, y3);
    return rcStrict;
}


/**
 * VM-exit handler for interrupt-window exiting (VMX_EXIT_INT_WINDOW).
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitIntWindow(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /* Indicate that we no longer need to VM-exit when the guest is ready to receive interrupts, it is now ready. */
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCClearIntWindowExitVmcs(pVCpu, pVmcsInfo);

    /* Evaluate and deliver pending events and resume guest execution. */
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitIntWindow);
    return VINF_SUCCESS;
}


/**
 * VM-exit handler for NMI-window exiting (VMX_EXIT_NMI_WINDOW).
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitNmiWindow(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    if (RT_UNLIKELY(!(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_NMI_WINDOW_EXIT))) /** @todo NSTVMX: Turn this into an assertion. */
    {
        AssertMsgFailed(("Unexpected NMI-window exit.\n"));
        HMVMX_UNEXPECTED_EXIT_RET(pVCpu, pVmxTransient->uExitReason);
    }

    Assert(!CPUMAreInterruptsInhibitedByNmiEx(&pVCpu->cpum.GstCtx));

    /*
     * If block-by-STI is set when we get this VM-exit, it means the CPU doesn't block NMIs following STI.
     * It is therefore safe to unblock STI and deliver the NMI ourselves. See @bugref{7445}.
     */
    uint32_t fIntrState;
    int rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_INT_STATE, &fIntrState);
    AssertRC(rc);
    Assert(!(fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS));
    if (fIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI)
    {
        CPUMClearInterruptShadow(&pVCpu->cpum.GstCtx);

        fIntrState &= ~VMX_VMCS_GUEST_INT_STATE_BLOCK_STI;
        rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_GUEST_INT_STATE, fIntrState);
        AssertRC(rc);
    }

    /* Indicate that we no longer need to VM-exit when the guest is ready to receive NMIs, it is now ready. */
    vmxHCClearNmiWindowExitVmcs(pVCpu, pVmcsInfo);

    /* Evaluate and deliver pending events and resume guest execution. */
    return VINF_SUCCESS;
}


/**
 * VM-exit handler for WBINVD (VMX_EXIT_WBINVD). Conditional VM-exit.
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitWbinvd(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    return vmxHCAdvanceGuestRip(pVCpu, pVmxTransient);
}


/**
 * VM-exit handler for INVD (VMX_EXIT_INVD). Unconditional VM-exit.
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitInvd(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    return vmxHCAdvanceGuestRip(pVCpu, pVmxTransient);
}


/**
 * VM-exit handler for CPUID (VMX_EXIT_CPUID). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitCpuid(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /*
     * Get the state we need and update the exit history entry.
     */
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict;
    PCEMEXITREC pExitRec = EMHistoryUpdateFlagsAndTypeAndPC(pVCpu,
                                                            EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_CPUID),
                                                            pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base);
    if (!pExitRec)
    {
        /*
         * Regular CPUID instruction execution.
         */
        rcStrict = IEMExecDecodedCpuid(pVCpu, pVmxTransient->cbExitInstr);
        if (rcStrict == VINF_SUCCESS)
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
        else if (rcStrict == VINF_IEM_RAISED_XCPT)
        {
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
            rcStrict = VINF_SUCCESS;
        }
    }
    else
    {
        /*
         * Frequent exit or something needing probing.  Get state and call EMHistoryExec.
         */
        int rc2 = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL,
                                        IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
        AssertRCReturn(rc2, rc2);

        Log4(("CpuIdExit/%u: %04x:%08RX64: %#x/%#x -> EMHistoryExec\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.eax, pVCpu->cpum.GstCtx.ecx));

        rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);

        Log4(("CpuIdExit/%u: %04x:%08RX64: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    }
    return rcStrict;
}


/**
 * VM-exit handler for GETSEC (VMX_EXIT_GETSEC). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitGetsec(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    int rc = vmxHCImportGuestState<CPUMCTX_EXTRN_CR4>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_SMXE)
        return VINF_EM_RAW_EMULATE_INSTR;

    AssertMsgFailed(("vmxHCExitGetsec: Unexpected VM-exit when CR4.SMXE is 0.\n"));
    HMVMX_UNEXPECTED_EXIT_RET(pVCpu, pVmxTransient->uExitReason);
}


/**
 * VM-exit handler for RDTSC (VMX_EXIT_RDTSC). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitRdtsc(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedRdtsc(pVCpu, pVmxTransient->cbExitInstr);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        /* If we get a spurious VM-exit when TSC offsetting is enabled,
           we must reset offsetting on VM-entry. See @bugref{6634}. */
        if (pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TSC_OFFSETTING)
            pVmxTransient->fUpdatedTscOffsettingAndPreemptTimer = false;
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for RDTSCP (VMX_EXIT_RDTSCP). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitRdtscp(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_TSC_AUX>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedRdtscp(pVCpu, pVmxTransient->cbExitInstr);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        /* If we get a spurious VM-exit when TSC offsetting is enabled,
           we must reset offsetting on VM-reentry. See @bugref{6634}. */
        if (pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TSC_OFFSETTING)
            pVmxTransient->fUpdatedTscOffsettingAndPreemptTimer = false;
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for RDPMC (VMX_EXIT_RDPMC). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitRdpmc(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_CR4>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedRdpmc(pVCpu, pVmxTransient->cbExitInstr);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for VMCALL (VMX_EXIT_VMCALL). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmcall(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    VBOXSTRICTRC rcStrict = VERR_VMX_IPE_3;
    if (EMAreHypercallInstructionsEnabled(pVCpu))
    {
        PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
        int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_RIP
                                       | CPUMCTX_EXTRN_RFLAGS
                                       | CPUMCTX_EXTRN_CR0
                                       | CPUMCTX_EXTRN_SS
                                       | CPUMCTX_EXTRN_CS
                                       | CPUMCTX_EXTRN_EFER>(pVCpu, pVmcsInfo, __FUNCTION__);
        AssertRCReturn(rc, rc);

        /* Perform the hypercall. */
        rcStrict = GIMHypercall(pVCpu, &pVCpu->cpum.GstCtx);
        if (rcStrict == VINF_SUCCESS)
        {
            rc = vmxHCAdvanceGuestRip(pVCpu, pVmxTransient);
            AssertRCReturn(rc, rc);
        }
        else
            Assert(   rcStrict == VINF_GIM_R3_HYPERCALL
                   || rcStrict == VINF_GIM_HYPERCALL_CONTINUING
                   || RT_FAILURE(rcStrict));

        /* If the hypercall changes anything other than guest's general-purpose registers,
           we would need to reload the guest changed bits here before VM-entry. */
    }
    else
        Log4Func(("Hypercalls not enabled\n"));

    /* If hypercalls are disabled or the hypercall failed for some reason, raise #UD and continue. */
    if (RT_FAILURE(rcStrict))
    {
        vmxHCSetPendingXcptUD(pVCpu);
        rcStrict = VINF_SUCCESS;
    }

    return rcStrict;
}


/**
 * VM-exit handler for INVLPG (VMX_EXIT_INVLPG). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitInvlpg(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
#ifndef IN_NEM_DARWIN
    Assert(!pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging || pVCpu->hmr0.s.fUsingDebugLoop);
#endif

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedInvlpg(pVCpu, pVmxTransient->cbExitInstr, pVmxTransient->uExitQual);

    if (rcStrict == VINF_SUCCESS || rcStrict == VINF_PGM_SYNC_CR3)
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    else
        AssertMsgFailed(("Unexpected IEMExecDecodedInvlpg(%#RX64) status: %Rrc\n", pVmxTransient->uExitQual,
                         VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * VM-exit handler for MONITOR (VMX_EXIT_MONITOR). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitMonitor(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_DS>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedMonitor(pVCpu, pVmxTransient->cbExitInstr);
    if (rcStrict == VINF_SUCCESS)
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }

    return rcStrict;
}


/**
 * VM-exit handler for MWAIT (VMX_EXIT_MWAIT). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitMwait(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedMwait(pVCpu, pVmxTransient->cbExitInstr);
    if (RT_SUCCESS(rcStrict))
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
        if (EMMonitorWaitShouldContinue(pVCpu, &pVCpu->cpum.GstCtx))
            rcStrict = VINF_SUCCESS;
    }

    return rcStrict;
}


/**
 * VM-exit handler for triple faults (VMX_EXIT_TRIPLE_FAULT). Unconditional
 * VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitTripleFault(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    return VINF_EM_RESET;
}


/**
 * VM-exit handler for HLT (VMX_EXIT_HLT). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitHlt(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    int rc = vmxHCAdvanceGuestRip(pVCpu, pVmxTransient);
    AssertRCReturn(rc, rc);

    HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_RFLAGS);            /* Advancing the RIP above should've imported eflags. */
    if (EMShouldContinueAfterHalt(pVCpu, &pVCpu->cpum.GstCtx))    /* Requires eflags. */
        rc = VINF_SUCCESS;
    else
        rc = VINF_EM_HALT;

    if (rc != VINF_SUCCESS)
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatSwitchHltToR3);
    return rc;
}


#ifndef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * VM-exit handler for instructions that result in a \#UD exception delivered to
 * the guest.
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitSetPendingXcptUD(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    vmxHCSetPendingXcptUD(pVCpu);
    return VINF_SUCCESS;
}
#endif


/**
 * VM-exit handler for expiry of the VMX-preemption timer.
 */
HMVMX_EXIT_DECL vmxHCExitPreemptTimer(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /* If the VMX-preemption timer has expired, reinitialize the preemption timer on next VM-entry. */
    pVmxTransient->fUpdatedTscOffsettingAndPreemptTimer = false;
Log12(("vmxHCExitPreemptTimer:\n"));

    /* If there are any timer events pending, fall back to ring-3, otherwise resume guest execution. */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    bool fTimersPending = TMTimerPollBool(pVM, pVCpu);
    STAM_REL_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitPreemptTimer);
    return fTimersPending ? VINF_EM_RAW_TIMER_PENDING : VINF_SUCCESS;
}


/**
 * VM-exit handler for XSETBV (VMX_EXIT_XSETBV). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitXsetbv(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_CR4>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedXsetbv(pVCpu, pVmxTransient->cbExitInstr);
    ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, rcStrict != VINF_IEM_RAISED_XCPT ? HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS
                                                                                : HM_CHANGED_RAISED_XCPT_MASK);

#ifndef IN_NEM_DARWIN
    PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    bool const fLoadSaveGuestXcr0 = (pCtx->cr4 & X86_CR4_OSXSAVE) && pCtx->aXcr[0] != ASMGetXcr0();
    if (fLoadSaveGuestXcr0 != pVCpu->hmr0.s.fLoadSaveGuestXcr0)
    {
        pVCpu->hmr0.s.fLoadSaveGuestXcr0 = fLoadSaveGuestXcr0;
        hmR0VmxUpdateStartVmFunction(pVCpu);
    }
#endif

    return rcStrict;
}


/**
 * VM-exit handler for INVPCID (VMX_EXIT_INVPCID). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitInvpcid(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /** @todo Enable the new code after finding a reliably guest test-case. */
#if 1
    return VERR_EM_INTERPRETER;
#else
    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState(pVCpu, pVmxTransient->pVmcsInfo, CPUMCTX_EXTRN_RSP | CPUMCTX_EXTRN_SREG_MASK
                                                                    | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK);
    AssertRCReturn(rc, rc);

    /* Paranoia. Ensure this has a memory operand. */
    Assert(!pVmxTransient->ExitInstrInfo.Inv.u1Cleared0);

    uint8_t const iGReg = pVmxTransient->ExitInstrInfo.VmreadVmwrite.iReg2;
    Assert(iGReg < RT_ELEMENTS(pVCpu->cpum.GstCtx.aGRegs));
    uint64_t const uType = CPUMIsGuestIn64BitCode(pVCpu) ? pVCpu->cpum.GstCtx.aGRegs[iGReg].u64
                                                         : pVCpu->cpum.GstCtx.aGRegs[iGReg].u32;

    RTGCPTR GCPtrDesc;
    HMVMX_DECODE_MEM_OPERAND(pVCpu, pVmxTransient->ExitInstrInfo.u, pVmxTransient->uExitQual, VMXMEMACCESS_READ, &GCPtrDesc);

    VBOXSTRICTRC rcStrict = IEMExecDecodedInvpcid(pVCpu, pVmxTransient->cbExitInstr, pVmxTransient->ExitInstrInfo.Inv.iSegReg,
                                                  GCPtrDesc, uType);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
#endif
}


/**
 * VM-exit handler for invalid-guest-state (VMX_EXIT_ERR_INVALID_GUEST_STATE). Error
 * VM-exit.
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitErrInvalidGuestState(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    int rc = vmxHCImportGuestStateEx(pVCpu, pVmcsInfo, HMVMX_CPUMCTX_EXTRN_ALL);
    AssertRCReturn(rc, rc);

    rc = vmxHCCheckCachedVmcsCtls(pVCpu, pVmcsInfo, pVmxTransient->fIsNestedGuest);
    if (RT_FAILURE(rc))
        return rc;

    uint32_t const uInvalidReason = vmxHCCheckGuestState(pVCpu, pVmcsInfo);
    NOREF(uInvalidReason);

#ifdef VBOX_STRICT
    uint32_t fIntrState;
    uint64_t u64Val;
    vmxHCReadToTransient<  HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    vmxHCReadEntryXcptErrorCodeVmcs(pVCpu, pVmxTransient);

    Log4(("uInvalidReason                             %u\n",     uInvalidReason));
    Log4(("VMX_VMCS32_CTRL_ENTRY_INTERRUPTION_INFO    %#RX32\n", pVmxTransient->uEntryIntInfo));
    Log4(("VMX_VMCS32_CTRL_ENTRY_EXCEPTION_ERRCODE    %#RX32\n", pVmxTransient->uEntryXcptErrorCode));
    Log4(("VMX_VMCS32_CTRL_ENTRY_INSTR_LENGTH         %#RX32\n", pVmxTransient->cbEntryInstr));

    rc = VMX_VMCS_READ_32(pVCpu, VMX_VMCS32_GUEST_INT_STATE, &fIntrState);            AssertRC(rc);
    Log4(("VMX_VMCS32_GUEST_INT_STATE                 %#RX32\n", fIntrState));
    rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_GUEST_CR0, &u64Val);                        AssertRC(rc);
    Log4(("VMX_VMCS_GUEST_CR0                         %#RX64\n", u64Val));
    rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_CTRL_CR0_MASK, &u64Val);                    AssertRC(rc);
    Log4(("VMX_VMCS_CTRL_CR0_MASK                     %#RX64\n", u64Val));
    rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_CTRL_CR0_READ_SHADOW, &u64Val);             AssertRC(rc);
    Log4(("VMX_VMCS_CTRL_CR4_READ_SHADOW              %#RX64\n", u64Val));
    rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_CTRL_CR4_MASK, &u64Val);                    AssertRC(rc);
    Log4(("VMX_VMCS_CTRL_CR4_MASK                     %#RX64\n", u64Val));
    rc = VMX_VMCS_READ_NW(pVCpu, VMX_VMCS_CTRL_CR4_READ_SHADOW, &u64Val);             AssertRC(rc);
    Log4(("VMX_VMCS_CTRL_CR4_READ_SHADOW              %#RX64\n", u64Val));
# ifndef IN_NEM_DARWIN
    if (pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging)
    {
        rc = VMX_VMCS_READ_64(pVCpu, VMX_VMCS64_CTRL_EPTP_FULL, &u64Val);             AssertRC(rc);
        Log4(("VMX_VMCS64_CTRL_EPTP_FULL                  %#RX64\n", u64Val));
    }

    hmR0DumpRegs(pVCpu, HM_DUMP_REG_FLAGS_ALL);
# endif
#endif

    return VERR_VMX_INVALID_GUEST_STATE;
}

/**
 * VM-exit handler for all undefined/unexpected reasons. Should never happen.
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitErrUnexpected(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    /*
     * Cumulative notes of all recognized but unexpected VM-exits.
     *
     * 1. This does -not- cover scenarios like a page-fault VM-exit occurring when
     *    nested-paging is used.
     *
     * 2. Any instruction that causes a VM-exit unconditionally (for e.g. VMXON) must be
     *    emulated or a #UD must be raised in the guest. Therefore, we should -not- be using
     *    this function (and thereby stop VM execution) for handling such instructions.
     *
     *
     * VMX_EXIT_INIT_SIGNAL:
     *    INIT signals are blocked in VMX root operation by VMXON and by SMI in SMM.
     *    It is -NOT- blocked in VMX non-root operation so we can, in theory, still get these
     *    VM-exits. However, we should not receive INIT signals VM-exit while executing a VM.
     *
     *    See Intel spec. 33.14.1 Default Treatment of SMI Delivery"
     *    See Intel spec. 29.3 "VMX Instructions" for "VMXON".
     *    See Intel spec. "23.8 Restrictions on VMX operation".
     *
     * VMX_EXIT_SIPI:
     *    SIPI exits can only occur in VMX non-root operation when the "wait-for-SIPI" guest
     *    activity state is used. We don't make use of it as our guests don't have direct
     *    access to the host local APIC.
     *
     *    See Intel spec. 25.3 "Other Causes of VM-exits".
     *
     * VMX_EXIT_IO_SMI:
     * VMX_EXIT_SMI:
     *    This can only happen if we support dual-monitor treatment of SMI, which can be
     *    activated by executing VMCALL in VMX root operation. Only an STM (SMM transfer
     *    monitor) would get this VM-exit when we (the executive monitor) execute a VMCALL in
     *    VMX root mode or receive an SMI. If we get here, something funny is going on.
     *
     *    See Intel spec. 33.15.6 "Activating the Dual-Monitor Treatment"
     *    See Intel spec. 25.3 "Other Causes of VM-Exits"
     *
     * VMX_EXIT_ERR_MSR_LOAD:
     *    Failures while loading MSRs are part of the VM-entry MSR-load area are unexpected
     *    and typically indicates a bug in the hypervisor code. We thus cannot not resume
     *    execution.
     *
     *    See Intel spec. 26.7 "VM-Entry Failures During Or After Loading Guest State".
     *
     * VMX_EXIT_ERR_MACHINE_CHECK:
     *    Machine check exceptions indicates a fatal/unrecoverable hardware condition
     *    including but not limited to system bus, ECC, parity, cache and TLB errors. A
     *    #MC exception abort class exception is raised. We thus cannot assume a
     *    reasonable chance of continuing any sort of execution and we bail.
     *
     *    See Intel spec. 15.1 "Machine-check Architecture".
     *    See Intel spec. 27.1 "Architectural State Before A VM Exit".
     *
     * VMX_EXIT_PML_FULL:
     * VMX_EXIT_VIRTUALIZED_EOI:
     * VMX_EXIT_APIC_WRITE:
     *    We do not currently support any of these features and thus they are all unexpected
     *    VM-exits.
     *
     * VMX_EXIT_GDTR_IDTR_ACCESS:
     * VMX_EXIT_LDTR_TR_ACCESS:
     * VMX_EXIT_RDRAND:
     * VMX_EXIT_RSM:
     * VMX_EXIT_VMFUNC:
     * VMX_EXIT_ENCLS:
     * VMX_EXIT_RDSEED:
     * VMX_EXIT_XSAVES:
     * VMX_EXIT_XRSTORS:
     * VMX_EXIT_UMWAIT:
     * VMX_EXIT_TPAUSE:
     * VMX_EXIT_LOADIWKEY:
     *    These VM-exits are -not- caused unconditionally by execution of the corresponding
     *    instruction. Any VM-exit for these instructions indicate a hardware problem,
     *    unsupported CPU modes (like SMM) or potentially corrupt VMCS controls.
     *
     *    See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    AssertMsgFailed(("Unexpected VM-exit %u\n", pVmxTransient->uExitReason));
    HMVMX_UNEXPECTED_EXIT_RET(pVCpu, pVmxTransient->uExitReason);
}


/**
 * VM-exit handler for RDMSR (VMX_EXIT_RDMSR).
 */
HMVMX_EXIT_DECL vmxHCExitRdmsr(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);

    /** @todo Optimize this: We currently drag in the whole MSR state
     * (CPUMCTX_EXTRN_ALL_MSRS) here.  We should optimize this to only get
     * MSRs required.  That would require changes to IEM and possibly CPUM too.
     * (Should probably do it lazy fashion from CPUMAllMsrs.cpp). */
    PVMXVMCSINFO   pVmcsInfo = pVmxTransient->pVmcsInfo;
    uint32_t const idMsr     = pVCpu->cpum.GstCtx.ecx;
    int            rc;
    switch (idMsr)
    {
        default:
            rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_ALL_MSRS>(pVCpu, pVmcsInfo,
                                                                                                            __FUNCTION__);
            AssertRCReturn(rc, rc);
            break;
        case MSR_K8_FS_BASE:
            rc = vmxHCImportGuestState<  IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_ALL_MSRS
                                       | CPUMCTX_EXTRN_FS>(pVCpu, pVmcsInfo, __FUNCTION__);
            AssertRCReturn(rc, rc);
            break;
        case MSR_K8_GS_BASE:
            rc = vmxHCImportGuestState<  IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_ALL_MSRS
                                       | CPUMCTX_EXTRN_GS>(pVCpu, pVmcsInfo, __FUNCTION__);
            AssertRCReturn(rc, rc);
            break;
    }

    Log4Func(("ecx=%#RX32\n", idMsr));

#if defined(VBOX_STRICT) && !defined(IN_NEM_DARWIN)
    if (pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_MSR_BITMAPS)
    {
        if (   hmR0VmxIsAutoLoadGuestMsr(pVmcsInfo, idMsr)
            && idMsr != MSR_K6_EFER)
        {
            AssertMsgFailed(("Unexpected RDMSR for an MSR in the auto-load/store area in the VMCS. ecx=%#RX32\n", idMsr));
            HMVMX_UNEXPECTED_EXIT_RET(pVCpu, idMsr);
        }
        if (hmR0VmxIsLazyGuestMsr(pVCpu, idMsr))
        {
            Assert(pVmcsInfo->pvMsrBitmap);
            uint32_t fMsrpm = CPUMGetVmxMsrPermission(pVmcsInfo->pvMsrBitmap, idMsr);
            if (fMsrpm & VMXMSRPM_ALLOW_RD)
            {
                AssertMsgFailed(("Unexpected RDMSR for a passthru lazy-restore MSR. ecx=%#RX32\n", idMsr));
                HMVMX_UNEXPECTED_EXIT_RET(pVCpu, idMsr);
            }
        }
    }
#endif

    VBOXSTRICTRC rcStrict = IEMExecDecodedRdmsr(pVCpu, pVmxTransient->cbExitInstr);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitRdmsr);
    if (rcStrict == VINF_SUCCESS)
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    else
        AssertMsg(rcStrict == VINF_CPUM_R3_MSR_READ || rcStrict == VINF_EM_TRIPLE_FAULT,
                  ("Unexpected IEMExecDecodedRdmsr rc (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));

    return rcStrict;
}


/**
 * VM-exit handler for WRMSR (VMX_EXIT_WRMSR).
 */
HMVMX_EXIT_DECL vmxHCExitWrmsr(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);

    /*
     * The FS and GS base MSRs are not part of the above all-MSRs mask.
     * Although we don't need to fetch the base as it will be overwritten shortly, while
     * loading guest-state we would also load the entire segment register including limit
     * and attributes and thus we need to load them here.
     */
    /** @todo Optimize this: We currently drag in the whole MSR state
     * (CPUMCTX_EXTRN_ALL_MSRS) here.  We should optimize this to only get
     * MSRs required.  That would require changes to IEM and possibly CPUM too.
     * (Should probably do it lazy fashion from CPUMAllMsrs.cpp). */
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    uint32_t const idMsr   = pVCpu->cpum.GstCtx.ecx;
    int            rc;
    switch (idMsr)
    {
        default:
            rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_ALL_MSRS>(pVCpu, pVmcsInfo,
                                                                                                            __FUNCTION__);
            AssertRCReturn(rc, rc);
            break;

        case MSR_K8_FS_BASE:
            rc = vmxHCImportGuestState<  IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_ALL_MSRS
                                       | CPUMCTX_EXTRN_FS>(pVCpu, pVmcsInfo, __FUNCTION__);
            AssertRCReturn(rc, rc);
            break;
        case MSR_K8_GS_BASE:
            rc = vmxHCImportGuestState<  IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_ALL_MSRS
                                       | CPUMCTX_EXTRN_GS>(pVCpu, pVmcsInfo, __FUNCTION__);
            AssertRCReturn(rc, rc);
            break;
    }
    Log4Func(("ecx=%#RX32 edx:eax=%#RX32:%#RX32\n", idMsr, pVCpu->cpum.GstCtx.edx, pVCpu->cpum.GstCtx.eax));

    VBOXSTRICTRC rcStrict = IEMExecDecodedWrmsr(pVCpu, pVmxTransient->cbExitInstr);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitWrmsr);

    if (rcStrict == VINF_SUCCESS)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);

        /* If this is an X2APIC WRMSR access, update the APIC state as well. */
        if (    idMsr == MSR_IA32_APICBASE
            || (   idMsr >= MSR_IA32_X2APIC_START
                && idMsr <= MSR_IA32_X2APIC_END))
        {
            /*
             * We've already saved the APIC related guest-state (TPR) in post-run phase.
             * When full APIC register virtualization is implemented we'll have to make
             * sure APIC state is saved from the VMCS before IEM changes it.
             */
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_APIC_TPR);
        }
        else if (idMsr == MSR_IA32_TSC)        /* Windows 7 does this during bootup. See @bugref{6398}. */
            pVmxTransient->fUpdatedTscOffsettingAndPreemptTimer = false;
        else if (idMsr == MSR_K6_EFER)
        {
            /*
             * If the guest touches the EFER MSR we need to update the VM-Entry and VM-Exit controls
             * as well, even if it is -not- touching bits that cause paging mode changes (LMA/LME).
             * We care about the other bits as well, SCE and NXE. See @bugref{7368}.
             */
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_EFER_MSR | HM_CHANGED_VMX_ENTRY_EXIT_CTLS);
        }

        /* Update MSRs that are part of the VMCS and auto-load/store area when MSR-bitmaps are not used. */
        if (!(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_MSR_BITMAPS))
        {
            switch (idMsr)
            {
                case MSR_IA32_SYSENTER_CS:  ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_SYSENTER_CS_MSR);  break;
                case MSR_IA32_SYSENTER_EIP: ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_SYSENTER_EIP_MSR); break;
                case MSR_IA32_SYSENTER_ESP: ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_SYSENTER_ESP_MSR); break;
                case MSR_K8_FS_BASE:        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_FS);               break;
                case MSR_K8_GS_BASE:        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_GS);               break;
                case MSR_K6_EFER:           /* Nothing to do, already handled above. */                                    break;
                default:
                {
#ifndef IN_NEM_DARWIN
                    if (hmR0VmxIsLazyGuestMsr(pVCpu, idMsr))
                        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_VMX_GUEST_LAZY_MSRS);
                    else if (hmR0VmxIsAutoLoadGuestMsr(pVmcsInfo, idMsr))
                        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_VMX_GUEST_AUTO_MSRS);
#else
                    AssertMsgFailed(("TODO\n"));
#endif
                    break;
                }
            }
        }
#if defined(VBOX_STRICT) && !defined(IN_NEM_DARWIN)
        else
        {
            /* Paranoia. Validate that MSRs in the MSR-bitmaps with write-passthru are not intercepted. */
            switch (idMsr)
            {
                case MSR_IA32_SYSENTER_CS:
                case MSR_IA32_SYSENTER_EIP:
                case MSR_IA32_SYSENTER_ESP:
                case MSR_K8_FS_BASE:
                case MSR_K8_GS_BASE:
                {
                    AssertMsgFailed(("Unexpected WRMSR for an MSR in the VMCS. ecx=%#RX32\n", idMsr));
                    HMVMX_UNEXPECTED_EXIT_RET(pVCpu, idMsr);
                }

                /* Writes to MSRs in auto-load/store area/swapped MSRs, shouldn't cause VM-exits with MSR-bitmaps. */
                default:
                {
                    if (hmR0VmxIsAutoLoadGuestMsr(pVmcsInfo, idMsr))
                    {
                        /* EFER MSR writes are always intercepted. */
                        if (idMsr != MSR_K6_EFER)
                        {
                            AssertMsgFailed(("Unexpected WRMSR for an MSR in the auto-load/store area in the VMCS. ecx=%#RX32\n",
                                             idMsr));
                            HMVMX_UNEXPECTED_EXIT_RET(pVCpu, idMsr);
                        }
                    }

                    if (hmR0VmxIsLazyGuestMsr(pVCpu, idMsr))
                    {
                        Assert(pVmcsInfo->pvMsrBitmap);
                        uint32_t fMsrpm = CPUMGetVmxMsrPermission(pVmcsInfo->pvMsrBitmap, idMsr);
                        if (fMsrpm & VMXMSRPM_ALLOW_WR)
                        {
                            AssertMsgFailed(("Unexpected WRMSR for passthru, lazy-restore MSR. ecx=%#RX32\n", idMsr));
                            HMVMX_UNEXPECTED_EXIT_RET(pVCpu, idMsr);
                        }
                    }
                    break;
                }
            }
        }
#endif  /* VBOX_STRICT */
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    else
        AssertMsg(rcStrict == VINF_CPUM_R3_MSR_WRITE || rcStrict == VINF_EM_TRIPLE_FAULT,
                  ("Unexpected IEMExecDecodedWrmsr rc (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));

    return rcStrict;
}


/**
 * VM-exit handler for PAUSE (VMX_EXIT_PAUSE). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitPause(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /** @todo The guest has likely hit a contended spinlock. We might want to
     *        poke a schedule different guest VCPU. */
    int rc = vmxHCAdvanceGuestRip(pVCpu, pVmxTransient);
    if (RT_SUCCESS(rc))
        return VINF_EM_RAW_INTERRUPT;

    AssertMsgFailed(("vmxHCExitPause: Failed to increment RIP. rc=%Rrc\n", rc));
    return rc;
}


/**
 * VM-exit handler for when the TPR value is lowered below the specified
 * threshold (VMX_EXIT_TPR_BELOW_THRESHOLD). Conditional VM-exit.
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitTprBelowThreshold(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    Assert(pVmxTransient->pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW);

    /*
     * The TPR shadow would've been synced with the APIC TPR in the post-run phase.
     * We'll re-evaluate pending interrupts and inject them before the next VM
     * entry so we can just continue execution here.
     */
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitTprBelowThreshold);
    return VINF_SUCCESS;
}


/**
 * VM-exit handler for control-register accesses (VMX_EXIT_MOV_CRX). Conditional
 * VM-exit.
 *
 * @retval VINF_SUCCESS when guest execution can continue.
 * @retval VINF_PGM_SYNC_CR3 CR3 sync is required, back to ring-3.
 * @retval VERR_EM_RESCHEDULE_REM when we need to return to ring-3 due to
 *         incompatible guest state for VMX execution (real-on-v86 case).
 */
HMVMX_EXIT_DECL vmxHCExitMovCRx(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_PROFILE_ADV_START(&VCPU_2_VMXSTATS(pVCpu).StatExitMovCRx, y2);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);

    VBOXSTRICTRC rcStrict;
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    uint64_t const uExitQual   = pVmxTransient->uExitQual;
    uint32_t const uAccessType = VMX_EXIT_QUAL_CRX_ACCESS(uExitQual);
    switch (uAccessType)
    {
        /*
         * MOV to CRx.
         */
        case VMX_EXIT_QUAL_CRX_ACCESS_WRITE:
        {
            /*
             * When PAE paging is used, the CPU will reload PAE PDPTEs from CR3 when the guest
             * changes certain bits even in CR0, CR4 (and not just CR3). We are currently fine
             * since IEM_CPUMCTX_EXTRN_MUST_MASK (used below) includes CR3 which will import
             * PAE PDPTEs as well.
             */
            int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
            AssertRCReturn(rc, rc);

            HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);
#ifndef IN_NEM_DARWIN
            uint32_t const uOldCr0 = pVCpu->cpum.GstCtx.cr0;
#endif
            uint8_t const  iGReg   = VMX_EXIT_QUAL_CRX_GENREG(uExitQual);
            uint8_t const  iCrReg  = VMX_EXIT_QUAL_CRX_REGISTER(uExitQual);

            /*
             * MOV to CR3 only cause a VM-exit when one or more of the following are true:
             *   - When nested paging isn't used.
             *   - If the guest doesn't have paging enabled (intercept CR3 to update shadow page tables).
             *   - We are executing in the VM debug loop.
             */
#ifndef HMVMX_ALWAYS_INTERCEPT_CR3_ACCESS
# ifndef IN_NEM_DARWIN
            Assert(   iCrReg != 3
                   || !VM_IS_VMX_NESTED_PAGING(pVM)
                   || !CPUMIsGuestPagingEnabledEx(&pVCpu->cpum.GstCtx)
                   || pVCpu->hmr0.s.fUsingDebugLoop);
# else
            Assert(   iCrReg != 3
                   || !CPUMIsGuestPagingEnabledEx(&pVCpu->cpum.GstCtx));
# endif
#endif

            /* MOV to CR8 writes only cause VM-exits when TPR shadow is not used. */
            Assert(   iCrReg != 8
                   || !(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW));

            rcStrict = vmxHCExitMovToCrX(pVCpu, pVmxTransient->cbExitInstr, iGReg, iCrReg);
            AssertMsg(   rcStrict == VINF_SUCCESS
                      || rcStrict == VINF_PGM_SYNC_CR3, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

#ifndef IN_NEM_DARWIN
            /*
             * This is a kludge for handling switches back to real mode when we try to use
             * V86 mode to run real mode code directly.  Problem is that V86 mode cannot
             * deal with special selector values, so we have to return to ring-3 and run
             * there till the selector values are V86 mode compatible.
             *
             * Note! Using VINF_EM_RESCHEDULE_REM here rather than VINF_EM_RESCHEDULE since the
             *       latter is an alias for VINF_IEM_RAISED_XCPT which is asserted at the end of
             *       this function.
             */
            if (   iCrReg == 0
                && rcStrict == VINF_SUCCESS
                && !VM_IS_VMX_UNRESTRICTED_GUEST(pVM)
                && CPUMIsGuestInRealModeEx(&pVCpu->cpum.GstCtx)
                && (uOldCr0 & X86_CR0_PE)
                && !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE))
            {
                /** @todo Check selectors rather than returning all the time.  */
                Assert(!pVmxTransient->fIsNestedGuest);
                Log4Func(("CR0 write, back to real mode -> VINF_EM_RESCHEDULE_REM\n"));
                rcStrict = VINF_EM_RESCHEDULE_REM;
            }
#endif

            break;
        }

        /*
         * MOV from CRx.
         */
        case VMX_EXIT_QUAL_CRX_ACCESS_READ:
        {
            uint8_t const iGReg  = VMX_EXIT_QUAL_CRX_GENREG(uExitQual);
            uint8_t const iCrReg = VMX_EXIT_QUAL_CRX_REGISTER(uExitQual);

            /*
             * MOV from CR3 only cause a VM-exit when one or more of the following are true:
             *   - When nested paging isn't used.
             *   - If the guest doesn't have paging enabled (pass guest's CR3 rather than our identity mapped CR3).
             *   - We are executing in the VM debug loop.
             */
#ifndef HMVMX_ALWAYS_INTERCEPT_CR3_ACCESS
# ifndef IN_NEM_DARWIN
            Assert(   iCrReg != 3
                   || !VM_IS_VMX_NESTED_PAGING(pVM)
                   || !CPUMIsGuestPagingEnabledEx(&pVCpu->cpum.GstCtx)
                   || pVCpu->hmr0.s.fLeaveDone);
# else
            Assert(   iCrReg != 3
                   || !CPUMIsGuestPagingEnabledEx(&pVCpu->cpum.GstCtx));
# endif
#endif

            /* MOV from CR8 reads only cause a VM-exit when the TPR shadow feature isn't enabled. */
            Assert(   iCrReg != 8
                   || !(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW));

            rcStrict = vmxHCExitMovFromCrX(pVCpu, pVmcsInfo, pVmxTransient->cbExitInstr, iGReg, iCrReg);
            break;
        }

        /*
         * CLTS (Clear Task-Switch Flag in CR0).
         */
        case VMX_EXIT_QUAL_CRX_ACCESS_CLTS:
        {
            rcStrict = vmxHCExitClts(pVCpu, pVmcsInfo, pVmxTransient->cbExitInstr);
            break;
        }

        /*
         * LMSW (Load Machine-Status Word into CR0).
         * LMSW cannot clear CR0.PE, so no fRealOnV86Active kludge needed here.
         */
        case VMX_EXIT_QUAL_CRX_ACCESS_LMSW:
        {
            RTGCPTR        GCPtrEffDst;
            uint8_t const  cbInstr     = pVmxTransient->cbExitInstr;
            uint16_t const uMsw        = VMX_EXIT_QUAL_CRX_LMSW_DATA(uExitQual);
            bool const     fMemOperand = VMX_EXIT_QUAL_CRX_LMSW_OP_MEM(uExitQual);
            if (fMemOperand)
            {
                vmxHCReadToTransient<HMVMX_READ_GUEST_LINEAR_ADDR>(pVCpu, pVmxTransient);
                GCPtrEffDst = pVmxTransient->uGuestLinearAddr;
            }
            else
                GCPtrEffDst = NIL_RTGCPTR;
            rcStrict = vmxHCExitLmsw(pVCpu, pVmcsInfo, cbInstr, uMsw, GCPtrEffDst);
            break;
        }

        default:
        {
            AssertMsgFailed(("Unrecognized Mov CRX access type %#x\n", uAccessType));
            HMVMX_UNEXPECTED_EXIT_RET(pVCpu, uAccessType);
        }
    }

    Assert((VCPU_2_VMXSTATE(pVCpu).fCtxChanged & (HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS))
                                   == (HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS));
    Assert(rcStrict != VINF_IEM_RAISED_XCPT);

    STAM_PROFILE_ADV_STOP(&VCPU_2_VMXSTATS(pVCpu).StatExitMovCRx, y2);
    NOREF(pVM);
    return rcStrict;
}


/**
 * VM-exit handler for I/O instructions (VMX_EXIT_IO_INSTR). Conditional
 * VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitIoInstr(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_PROFILE_ADV_START(&VCPU_2_VMXSTATS(pVCpu).StatExitIO, y1);

    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
#define VMX_HC_EXIT_IO_INSTR_INITIAL_REGS   (IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_SREG_MASK | CPUMCTX_EXTRN_EFER)
    /* EFER MSR also required for longmode checks in EMInterpretDisasCurrent(), but it's always up-to-date. */
    int rc = vmxHCImportGuestState<VMX_HC_EXIT_IO_INSTR_INITIAL_REGS>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    /* Refer Intel spec. 27-5. "Exit Qualifications for I/O Instructions" for the format. */
    uint32_t const uIOPort      = VMX_EXIT_QUAL_IO_PORT(pVmxTransient->uExitQual);
    uint8_t  const uIOSize      = VMX_EXIT_QUAL_IO_SIZE(pVmxTransient->uExitQual);
    bool     const fIOWrite     = (VMX_EXIT_QUAL_IO_DIRECTION(pVmxTransient->uExitQual) == VMX_EXIT_QUAL_IO_DIRECTION_OUT);
    bool     const fIOString    = VMX_EXIT_QUAL_IO_IS_STRING(pVmxTransient->uExitQual);
    bool     const fGstStepping = RT_BOOL(pCtx->eflags.Bits.u1TF);
    bool     const fDbgStepping = VCPU_2_VMXSTATE(pVCpu).fSingleInstruction;
    AssertReturn(uIOSize <= 3 && uIOSize != 2, VERR_VMX_IPE_1);

    /*
     * Update exit history to see if this exit can be optimized.
     */
    VBOXSTRICTRC rcStrict;
    PCEMEXITREC  pExitRec = NULL;
    if (   !fGstStepping
        && !fDbgStepping)
        pExitRec = EMHistoryUpdateFlagsAndTypeAndPC(pVCpu,
                                                    !fIOString
                                                    ? !fIOWrite
                                                    ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_IO_PORT_READ)
                                                    : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_IO_PORT_WRITE)
                                                    : !fIOWrite
                                                    ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_IO_PORT_STR_READ)
                                                    : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_IO_PORT_STR_WRITE),
                                                    pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base);
    if (!pExitRec)
    {
        static uint32_t const s_aIOSizes[4] = { 1, 2, 0, 4 };                    /* Size of the I/O accesses in bytes. */
        static uint32_t const s_aIOOpAnd[4] = { 0xff, 0xffff, 0, 0xffffffff };   /* AND masks for saving result in AL/AX/EAX. */

        uint32_t const cbValue  = s_aIOSizes[uIOSize];
        uint32_t const cbInstr  = pVmxTransient->cbExitInstr;
        bool  fUpdateRipAlready = false; /* ugly hack, should be temporary. */
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        if (fIOString)
        {
            /*
             * INS/OUTS - I/O String instruction.
             *
             * Use instruction-information if available, otherwise fall back on
             * interpreting the instruction.
             */
            Log4Func(("cs:rip=%#04x:%08RX64 %#06x/%u %c str\n", pCtx->cs.Sel, pCtx->rip, uIOPort, cbValue, fIOWrite ? 'w' : 'r'));
            AssertReturn(pCtx->dx == uIOPort, VERR_VMX_IPE_2);
            bool const fInsOutsInfo = RT_BF_GET(g_HmMsrs.u.vmx.u64Basic, VMX_BF_BASIC_VMCS_INS_OUTS);
            if (fInsOutsInfo)
            {
                vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_INFO>(pVCpu, pVmxTransient);
                AssertReturn(pVmxTransient->ExitInstrInfo.StrIo.u3AddrSize <= 2, VERR_VMX_IPE_3);
                AssertCompile(IEMMODE_16BIT == 0 && IEMMODE_32BIT == 1 && IEMMODE_64BIT == 2);
                IEMMODE const enmAddrMode = (IEMMODE)pVmxTransient->ExitInstrInfo.StrIo.u3AddrSize;
                bool const fRep           = VMX_EXIT_QUAL_IO_IS_REP(pVmxTransient->uExitQual);
                if (fIOWrite)
                    rcStrict = IEMExecStringIoWrite(pVCpu, cbValue, enmAddrMode, fRep, cbInstr,
                                                    pVmxTransient->ExitInstrInfo.StrIo.iSegReg, true /*fIoChecked*/);
                else
                {
                    /*
                     * The segment prefix for INS cannot be overridden and is always ES. We can safely assume X86_SREG_ES.
                     * Hence "iSegReg" field is undefined in the instruction-information field in VT-x for INS.
                     * See Intel Instruction spec. for "INS".
                     * See Intel spec. Table 27-8 "Format of the VM-Exit Instruction-Information Field as Used for INS and OUTS".
                     */
                    rcStrict = IEMExecStringIoRead(pVCpu, cbValue, enmAddrMode, fRep, cbInstr, true /*fIoChecked*/);
                }
            }
            else
                rcStrict = IEMExecOne(pVCpu);

            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP);
            fUpdateRipAlready = true;
        }
        else
        {
            /*
             * IN/OUT - I/O instruction.
             */
            Log4Func(("cs:rip=%04x:%08RX64 %#06x/%u %c\n", pCtx->cs.Sel, pCtx->rip, uIOPort, cbValue, fIOWrite ? 'w' : 'r'));
            uint32_t const uAndVal = s_aIOOpAnd[uIOSize];
            Assert(!VMX_EXIT_QUAL_IO_IS_REP(pVmxTransient->uExitQual));
            if (fIOWrite)
            {
                rcStrict = IOMIOPortWrite(pVM, pVCpu, uIOPort, pCtx->eax & uAndVal, cbValue);
                STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitIOWrite);
#ifndef IN_NEM_DARWIN
                if (    rcStrict == VINF_IOM_R3_IOPORT_WRITE
                    && !pCtx->eflags.Bits.u1TF)
                    rcStrict = EMRZSetPendingIoPortWrite(pVCpu, uIOPort, cbInstr, cbValue, pCtx->eax & uAndVal);
#endif
            }
            else
            {
                rcStrict = VERR_GCM_NOT_HANDLED;
                if (GCMIsInterceptingIOPortRead(pVCpu, uIOPort, cbValue))
                {
                    rcStrict = GCMInterceptedIOPortRead(pVCpu, pCtx, uIOPort, cbValue);
                    if (rcStrict == VINF_GCM_HANDLED_ADVANCE_RIP || rcStrict == VINF_GCM_HANDLED)
                    {
                        /* ASSUMES we don't need to update fCtxChanged when regular GPRs change here. */
                        fUpdateRipAlready = rcStrict == VINF_GCM_HANDLED;
                        if (rcStrict == VINF_GCM_HANDLED)
                            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP);
                        rcStrict = VINF_SUCCESS;
                    }
                    else
                        Assert(rcStrict == VERR_GCM_NOT_HANDLED);
                }

                if (rcStrict == VERR_GCM_NOT_HANDLED)
                {
                    uint32_t u32Result = 0;
                    rcStrict = IOMIOPortRead(pVM, pVCpu, uIOPort, &u32Result, cbValue);
                    if (IOM_SUCCESS(rcStrict))
                    {
                        /* Save result of I/O IN instr. in AL/AX/EAX. */
                        /** @todo r=bird: 32-bit op size should clear high bits of rax! */
                        pCtx->eax = (pCtx->eax & ~uAndVal) | (u32Result & uAndVal);
                    }
#ifndef IN_NEM_DARWIN
                    if (    rcStrict == VINF_IOM_R3_IOPORT_READ
                        && !pCtx->eflags.Bits.u1TF)
                        rcStrict = EMRZSetPendingIoPortRead(pVCpu, uIOPort, cbInstr, cbValue);
#endif
                }
                STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitIORead);
            }
        }

        if (IOM_SUCCESS(rcStrict))
        {
            if (!fUpdateRipAlready)
            {
                vmxHCAdvanceGuestRipBy(pVCpu, cbInstr);
                ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP);
            }

            /*
             * INS/OUTS with REP prefix updates RFLAGS, can be observed with triple-fault guru
             * while booting Fedora 17 64-bit guest.
             *
             * See Intel Instruction reference for REP/REPE/REPZ/REPNE/REPNZ.
             */
            if (fIOString)
                ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RFLAGS);

            /*
             * If any I/O breakpoints are armed, we need to check if one triggered
             * and take appropriate action.
             * Note that the I/O breakpoint type is undefined if CR4.DE is 0.
             */
#if 1
            AssertCompile(VMX_HC_EXIT_IO_INSTR_INITIAL_REGS & CPUMCTX_EXTRN_DR7);
#else
            AssertCompile(!(VMX_HC_EXIT_IO_INSTR_INITIAL_REGS & CPUMCTX_EXTRN_DR7));
            rc = vmxHCImportGuestState<CPUMCTX_EXTRN_DR7>(pVCpu, pVmcsInfo);
            AssertRCReturn(rc, rc);
#endif

            /** @todo Optimize away the DBGFBpIsHwIoArmed call by having DBGF tell the
             *  execution engines about whether hyper BPs and such are pending. */
            uint32_t const uDr7 = pCtx->dr[7];
            if (RT_UNLIKELY(   (   (uDr7 & X86_DR7_ENABLED_MASK)
                                && X86_DR7_ANY_RW_IO(uDr7)
                                && (pCtx->cr4 & X86_CR4_DE))
                            || DBGFBpIsHwIoArmed(pVM)))
            {
                STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatDRxIoCheck);

#ifndef IN_NEM_DARWIN
                /* We're playing with the host CPU state here, make sure we don't preempt or longjmp. */
                VMMRZCallRing3Disable(pVCpu);
                HM_DISABLE_PREEMPT(pVCpu);

                bool fIsGuestDbgActive = CPUMR0DebugStateMaybeSaveGuest(pVCpu, true /* fDr6 */);

                VBOXSTRICTRC rcStrict2 = DBGFBpCheckIo(pVM, pVCpu, pCtx, uIOPort, cbValue);
                if (rcStrict2 == VINF_EM_RAW_GUEST_TRAP)
                {
                    /* Raise #DB. */
                    if (fIsGuestDbgActive)
                        ASMSetDR6(pCtx->dr[6]);
                    if (pCtx->dr[7] != uDr7)
                        VCPU_2_VMXSTATE(pVCpu).fCtxChanged |= HM_CHANGED_GUEST_DR7;

                    vmxHCSetPendingXcptDB(pVCpu);
                }
                /* rcStrict is VINF_SUCCESS, VINF_IOM_R3_IOPORT_COMMIT_WRITE, or in [VINF_EM_FIRST..VINF_EM_LAST],
                   however we can ditch VINF_IOM_R3_IOPORT_COMMIT_WRITE as it has VMCPU_FF_IOM as backup. */
                else if (   rcStrict2 != VINF_SUCCESS
                         && (rcStrict == VINF_SUCCESS || rcStrict2 < rcStrict))
                    rcStrict = rcStrict2;
                AssertCompile(VINF_EM_LAST < VINF_IOM_R3_IOPORT_COMMIT_WRITE);

                HM_RESTORE_PREEMPT();
                VMMRZCallRing3Enable(pVCpu);
#else
                /** @todo */
#endif
            }
        }

#ifdef VBOX_STRICT
        if (   rcStrict == VINF_IOM_R3_IOPORT_READ
            || rcStrict == VINF_EM_PENDING_R3_IOPORT_READ)
            Assert(!fIOWrite);
        else if (   rcStrict == VINF_IOM_R3_IOPORT_WRITE
                 || rcStrict == VINF_IOM_R3_IOPORT_COMMIT_WRITE
                 || rcStrict == VINF_EM_PENDING_R3_IOPORT_WRITE)
            Assert(fIOWrite);
        else
        {
# if 0 /** @todo r=bird: This is missing a bunch of VINF_EM_FIRST..VINF_EM_LAST
           *        statuses, that the VMM device and some others may return. See
           *        IOM_SUCCESS() for guidance. */
            AssertMsg(   RT_FAILURE(rcStrict)
                      || rcStrict == VINF_SUCCESS
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR
                      || rcStrict == VINF_EM_DBG_BREAKPOINT
                      || rcStrict == VINF_EM_RAW_GUEST_TRAP
                      || rcStrict == VINF_EM_RAW_TO_R3, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
# endif
        }
#endif
        STAM_PROFILE_ADV_STOP(&VCPU_2_VMXSTATS(pVCpu).StatExitIO, y1);
    }
    else
    {
        /*
         * Frequent exit or something needing probing.  Get state and call EMHistoryExec.
         */
        int rc2 = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL,
                                        VMX_HC_EXIT_IO_INSTR_INITIAL_REGS>(pVCpu, pVmcsInfo, __FUNCTION__);
        AssertRCReturn(rc2, rc2);
        STAM_COUNTER_INC(!fIOString ? fIOWrite ? &VCPU_2_VMXSTATS(pVCpu).StatExitIOWrite : &VCPU_2_VMXSTATS(pVCpu).StatExitIORead
                         : fIOWrite ? &VCPU_2_VMXSTATS(pVCpu).StatExitIOStringWrite : &VCPU_2_VMXSTATS(pVCpu).StatExitIOStringRead);
        Log4(("IOExit/%u: %04x:%08RX64: %s%s%s %#x LB %u -> EMHistoryExec\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              VMX_EXIT_QUAL_IO_IS_REP(pVmxTransient->uExitQual) ? "REP " : "",
              fIOWrite ? "OUT" : "IN", fIOString ? "S" : "", uIOPort, uIOSize));

        rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);

        Log4(("IOExit/%u: %04x:%08RX64: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    }
    return rcStrict;
}


/**
 * VM-exit handler for task switches (VMX_EXIT_TASK_SWITCH). Unconditional
 * VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitTaskSwitch(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /* Check if this task-switch occurred while delivery an event through the guest IDT. */
    vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);
    if (VMX_EXIT_QUAL_TASK_SWITCH_TYPE(pVmxTransient->uExitQual) == VMX_EXIT_QUAL_TASK_SWITCH_TYPE_IDT)
    {
        vmxHCReadToTransient<HMVMX_READ_IDT_VECTORING_INFO>(pVCpu, pVmxTransient);
        if (VMX_IDT_VECTORING_INFO_IS_VALID(pVmxTransient->uIdtVectoringInfo))
        {
            uint32_t uErrCode;
            if (VMX_IDT_VECTORING_INFO_IS_ERROR_CODE_VALID(pVmxTransient->uIdtVectoringInfo))
            {
                vmxHCReadToTransient<HMVMX_READ_IDT_VECTORING_ERROR_CODE>(pVCpu, pVmxTransient);
                uErrCode = pVmxTransient->uIdtVectoringErrorCode;
            }
            else
                uErrCode = 0;

            RTGCUINTPTR GCPtrFaultAddress;
            if (VMX_IDT_VECTORING_INFO_IS_XCPT_PF(pVmxTransient->uIdtVectoringInfo))
                GCPtrFaultAddress = pVCpu->cpum.GstCtx.cr2;
            else
                GCPtrFaultAddress = 0;

            vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);

            vmxHCSetPendingEvent(pVCpu, VMX_ENTRY_INT_INFO_FROM_EXIT_IDT_INFO(pVmxTransient->uIdtVectoringInfo),
                                 pVmxTransient->cbExitInstr, uErrCode, GCPtrFaultAddress);

            Log4Func(("Pending event. uIntType=%#x uVector=%#x\n", VMX_IDT_VECTORING_INFO_TYPE(pVmxTransient->uIdtVectoringInfo),
                      VMX_IDT_VECTORING_INFO_VECTOR(pVmxTransient->uIdtVectoringInfo)));
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitTaskSwitch);
            return VINF_EM_RAW_INJECT_TRPM_EVENT;
        }
    }

    /* Fall back to the interpreter to emulate the task-switch. */
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitTaskSwitch);
    return VERR_EM_INTERPRETER;
}


/**
 * VM-exit handler for monitor-trap-flag (VMX_EXIT_MTF). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitMtf(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    pVmcsInfo->u32ProcCtls &= ~VMX_PROC_CTLS_MONITOR_TRAP_FLAG;
    int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, pVmcsInfo->u32ProcCtls);
    AssertRC(rc);
    return VINF_EM_DBG_STEPPED;
}


/**
 * VM-exit handler for APIC access (VMX_EXIT_APIC_ACCESS). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitApicAccess(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitApicAccess);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_LEN
                         | HMVMX_READ_EXIT_INTERRUPTION_INFO
                         | HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE
                         | HMVMX_READ_IDT_VECTORING_INFO
                         | HMVMX_READ_IDT_VECTORING_ERROR_CODE>(pVCpu, pVmxTransient);

    /*
     * If this VM-exit occurred while delivering an event through the guest IDT, handle it accordingly.
     */
    VBOXSTRICTRC rcStrict = vmxHCCheckExitDueToEventDelivery(pVCpu, pVmxTransient);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        /* For some crazy guest, if an event delivery causes an APIC-access VM-exit, go to instruction emulation. */
        if (RT_UNLIKELY(VCPU_2_VMXSTATE(pVCpu).Event.fPending))
        {
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatInjectInterpret);
            return VINF_EM_RAW_INJECT_TRPM_EVENT;
        }
    }
    else
    {
        Assert(rcStrict != VINF_HM_DOUBLE_FAULT);
        return rcStrict;
    }

    /* IOMMIOPhysHandler() below may call into IEM, save the necessary state. */
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    /* See Intel spec. 27-6 "Exit Qualifications for APIC-access VM-exits from Linear Accesses & Guest-Phyiscal Addresses" */
    uint32_t const uAccessType = VMX_EXIT_QUAL_APIC_ACCESS_TYPE(pVmxTransient->uExitQual);
    switch (uAccessType)
    {
#ifndef IN_NEM_DARWIN
        case VMX_APIC_ACCESS_TYPE_LINEAR_WRITE:
        case VMX_APIC_ACCESS_TYPE_LINEAR_READ:
        {
            AssertMsg(   !(pVmcsInfo->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW)
                      || VMX_EXIT_QUAL_APIC_ACCESS_OFFSET(pVmxTransient->uExitQual) != XAPIC_OFF_TPR,
                      ("vmxHCExitApicAccess: can't access TPR offset while using TPR shadowing.\n"));

            RTGCPHYS GCPhys = VCPU_2_VMXSTATE(pVCpu).vmx.u64GstMsrApicBase;    /* Always up-to-date, as it is not part of the VMCS. */
            GCPhys &= ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
            GCPhys += VMX_EXIT_QUAL_APIC_ACCESS_OFFSET(pVmxTransient->uExitQual);
            Log4Func(("Linear access uAccessType=%#x GCPhys=%#RGp Off=%#x\n", uAccessType, GCPhys,
                 VMX_EXIT_QUAL_APIC_ACCESS_OFFSET(pVmxTransient->uExitQual)));

            rcStrict = IOMR0MmioPhysHandler(pVCpu->CTX_SUFF(pVM), pVCpu,
                                            uAccessType == VMX_APIC_ACCESS_TYPE_LINEAR_READ ? 0 : X86_TRAP_PF_RW, GCPhys);
            Log4Func(("IOMR0MmioPhysHandler returned %Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
            if (   rcStrict == VINF_SUCCESS
                || rcStrict == VERR_PAGE_TABLE_NOT_PRESENT
                || rcStrict == VERR_PAGE_NOT_PRESENT)
            {
                ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RSP | HM_CHANGED_GUEST_RFLAGS
                                                         | HM_CHANGED_GUEST_APIC_TPR);
                rcStrict = VINF_SUCCESS;
            }
            break;
        }
#else
        /** @todo */
#endif

        default:
        {
            Log4Func(("uAccessType=%#x\n", uAccessType));
            rcStrict = VINF_EM_RAW_EMULATE_INSTR;
            break;
        }
    }

    if (rcStrict != VINF_SUCCESS)
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatSwitchApicAccessToR3);
    return rcStrict;
}


/**
 * VM-exit handler for debug-register accesses (VMX_EXIT_MOV_DRX). Conditional
 * VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitMovDRx(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;

    /*
     * We might also get this VM-exit if the nested-guest isn't intercepting MOV DRx accesses.
     * In such a case, rather than disabling MOV DRx intercepts and resuming execution, we
     * must emulate the MOV DRx access.
     */
    if (!pVmxTransient->fIsNestedGuest)
    {
        /* We should -not- get this VM-exit if the guest's debug registers were active. */
        if (   pVmxTransient->fWasGuestDebugStateActive
#ifdef VMX_WITH_MAYBE_ALWAYS_INTERCEPT_MOV_DRX
            && !pVCpu->CTX_SUFF(pVM)->hmr0.s.vmx.fAlwaysInterceptMovDRx
#endif
           )
        {
            AssertMsgFailed(("Unexpected MOV DRx exit\n"));
            HMVMX_UNEXPECTED_EXIT_RET(pVCpu, pVmxTransient->uExitReason);
        }

        if (   !VCPU_2_VMXSTATE(pVCpu).fSingleInstruction
            && !pVmxTransient->fWasHyperDebugStateActive)
        {
            Assert(!DBGFIsStepping(pVCpu));
            Assert(pVmcsInfo->u32XcptBitmap & RT_BIT(X86_XCPT_DB));

            /* Whether we disable intercepting MOV DRx instructions and resume
               the current one, or emulate it and keep intercepting them is
               configurable.  Though it usually comes down to whether there are
               any new DR6 & DR7 bits (RTM) we want to hide from the guest. */
#ifdef VMX_WITH_MAYBE_ALWAYS_INTERCEPT_MOV_DRX
            bool const fResumeInstruction = !pVCpu->CTX_SUFF(pVM)->hmr0.s.vmx.fAlwaysInterceptMovDRx;
#else
            bool const fResumeInstruction = true;
#endif
            if (fResumeInstruction)
            {
                pVmcsInfo->u32ProcCtls &= ~VMX_PROC_CTLS_MOV_DR_EXIT;
                int rc = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, pVmcsInfo->u32ProcCtls);
                AssertRC(rc);
            }

#ifndef IN_NEM_DARWIN
            /* We're playing with the host CPU state here, make sure we can't preempt or longjmp. */
            VMMRZCallRing3Disable(pVCpu);
            HM_DISABLE_PREEMPT(pVCpu);

            /* Save the host & load the guest debug state, restart execution of the MOV DRx instruction. */
            CPUMR0LoadGuestDebugState(pVCpu, true /* include DR6 */);
            Assert(CPUMIsGuestDebugStateActive(pVCpu));

            HM_RESTORE_PREEMPT();
            VMMRZCallRing3Enable(pVCpu);
#else
            CPUMR3NemActivateGuestDebugState(pVCpu);
            Assert(CPUMIsGuestDebugStateActive(pVCpu));
            Assert(!CPUMIsHyperDebugStateActive(pVCpu));
#endif

            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatDRxContextSwitch);
            if (fResumeInstruction)
            {
#ifdef VBOX_WITH_STATISTICS
                vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);
                if (VMX_EXIT_QUAL_DRX_DIRECTION(pVmxTransient->uExitQual) == VMX_EXIT_QUAL_DRX_DIRECTION_WRITE)
                    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitDRxWrite);
                else
                    STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitDRxRead);
#endif
                return VINF_SUCCESS;
            }
        }
    }

    /*
     * Import state.  We must have DR7 loaded here as it's always consulted,
     * both for reading and writing.  The other debug registers are never
     * exported as such.
     */
    vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK
                                   | CPUMCTX_EXTRN_GPRS_MASK
                                   | CPUMCTX_EXTRN_DR7>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    uint8_t const iGReg  = VMX_EXIT_QUAL_DRX_GENREG(pVmxTransient->uExitQual);
    uint8_t const iDrReg = VMX_EXIT_QUAL_DRX_REGISTER(pVmxTransient->uExitQual);
    Log4Func(("cs:rip=%#04x:%08RX64 r%d %s dr%d\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, iGReg,
              VMX_EXIT_QUAL_DRX_DIRECTION(pVmxTransient->uExitQual) == VMX_EXIT_QUAL_DRX_DIRECTION_WRITE ? "->" : "<-", iDrReg));

    VBOXSTRICTRC  rcStrict;
    if (VMX_EXIT_QUAL_DRX_DIRECTION(pVmxTransient->uExitQual) == VMX_EXIT_QUAL_DRX_DIRECTION_WRITE)
    {
        /*
         * Write DRx register.
         */
        rcStrict = IEMExecDecodedMovDRxWrite(pVCpu, pVmxTransient->cbExitInstr, iDrReg, iGReg);
        AssertMsg(   rcStrict == VINF_SUCCESS
                  || rcStrict == VINF_IEM_RAISED_XCPT, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

        if (rcStrict == VINF_SUCCESS)
       {
            /** @todo r=bird: Not sure why we always flag DR7 as modified here, but I've
             * kept it for now to avoid breaking something non-obvious. */
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS
                                                                | HM_CHANGED_GUEST_DR7);
            /* Update the DR6 register if guest debug state is active, otherwise we'll
               trash it when calling CPUMR0DebugStateMaybeSaveGuestAndRestoreHost. */
            if (iDrReg == 6 && CPUMIsGuestDebugStateActive(pVCpu))
                ASMSetDR6(pVCpu->cpum.GstCtx.dr[6]);
            Log4Func(("r%d=%#RX64 => dr%d=%#RX64\n", iGReg, pVCpu->cpum.GstCtx.aGRegs[iGReg].u,
                      iDrReg, pVCpu->cpum.GstCtx.dr[iDrReg]));
        }
        else if (rcStrict == VINF_IEM_RAISED_XCPT)
        {
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
            rcStrict = VINF_SUCCESS;
        }

        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitDRxWrite);
    }
    else
    {
        /*
         * Read DRx register into a general purpose register.
         */
        rcStrict = IEMExecDecodedMovDRxRead(pVCpu, pVmxTransient->cbExitInstr, iGReg, iDrReg);
        AssertMsg(   rcStrict == VINF_SUCCESS
                  || rcStrict == VINF_IEM_RAISED_XCPT, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

        if (rcStrict == VINF_SUCCESS)
        {
            if (iGReg == X86_GREG_xSP)
                ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS
                                                                    | HM_CHANGED_GUEST_RSP);
            else
                ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
        }
        else if (rcStrict == VINF_IEM_RAISED_XCPT)
        {
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
            rcStrict = VINF_SUCCESS;
        }

        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitDRxRead);
    }

    return rcStrict;
}


/**
 * VM-exit handler for EPT misconfiguration (VMX_EXIT_EPT_MISCONFIG).
 * Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitEptMisconfig(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

#ifndef IN_NEM_DARWIN
    Assert(pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_INSTR_LEN
                         | HMVMX_READ_EXIT_INTERRUPTION_INFO
                         | HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE
                         | HMVMX_READ_IDT_VECTORING_INFO
                         | HMVMX_READ_IDT_VECTORING_ERROR_CODE
                         | HMVMX_READ_GUEST_PHYSICAL_ADDR>(pVCpu, pVmxTransient);

    /*
     * If this VM-exit occurred while delivering an event through the guest IDT, handle it accordingly.
     */
    VBOXSTRICTRC rcStrict = vmxHCCheckExitDueToEventDelivery(pVCpu, pVmxTransient);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        /*
         * In the unlikely case where delivering an event causes an EPT misconfig (MMIO), go back to
         * instruction emulation to inject the original event. Otherwise, injecting the original event
         * using hardware-assisted VMX would trigger the same EPT misconfig VM-exit again.
         */
        if (!VCPU_2_VMXSTATE(pVCpu).Event.fPending)
        { /* likely */ }
        else
        {
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatInjectInterpret);
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX
            /** @todo NSTVMX: Think about how this should be handled. */
            if (pVmxTransient->fIsNestedGuest)
                return VERR_VMX_IPE_3;
# endif
            return VINF_EM_RAW_INJECT_TRPM_EVENT;
        }
    }
    else
    {
        Assert(rcStrict != VINF_HM_DOUBLE_FAULT);
        return rcStrict;
    }

    /*
     * Get sufficient state and update the exit history entry.
     */
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    RTGCPHYS const GCPhys = pVmxTransient->uGuestPhysicalAddr;
    PCEMEXITREC pExitRec = EMHistoryUpdateFlagsAndTypeAndPC(pVCpu,
                                                            EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_MMIO),
                                                            pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base);
    if (!pExitRec)
    {
        /*
         * If we succeed, resume guest execution.
         * If we fail in interpreting the instruction because we couldn't get the guest physical address
         * of the page containing the instruction via the guest's page tables (we would invalidate the guest page
         * in the host TLB), resume execution which would cause a guest page fault to let the guest handle this
         * weird case. See @bugref{6043}.
         */
        PVMCC    pVM  = pVCpu->CTX_SUFF(pVM);
/** @todo bird: We can probably just go straight to IOM here and assume that
 *        it's MMIO, then fall back on PGM if that hunch didn't work out so
 *        well.  However, we need to address that aliasing workarounds that
 *        PGMR0Trap0eHandlerNPMisconfig implements.  So, some care is needed.
 *
 *        Might also be interesting to see if we can get this done more or
 *        less locklessly inside IOM.  Need to consider the lookup table
 *        updating and use a bit more carefully first (or do all updates via
 *        rendezvous) */
        rcStrict = PGMR0Trap0eHandlerNPMisconfig(pVM, pVCpu, PGMMODE_EPT, &pVCpu->cpum.GstCtx, GCPhys, UINT32_MAX);
        Log4Func(("At %#RGp RIP=%#RX64 rc=%Rrc\n", GCPhys, pVCpu->cpum.GstCtx.rip, VBOXSTRICTRC_VAL(rcStrict)));
        if (   rcStrict == VINF_SUCCESS
            || rcStrict == VERR_PAGE_TABLE_NOT_PRESENT
            || rcStrict == VERR_PAGE_NOT_PRESENT)
        {
            /* Successfully handled MMIO operation. */
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RSP | HM_CHANGED_GUEST_RFLAGS
                                                     | HM_CHANGED_GUEST_APIC_TPR);
            rcStrict = VINF_SUCCESS;
        }
    }
    else
    {
        /*
         * Frequent exit or something needing probing. Call EMHistoryExec.
         */
        int rc2 = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL, IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
        AssertRCReturn(rc2, rc2);
        Log4(("EptMisscfgExit/%u: %04x:%08RX64: %RGp -> EMHistoryExec\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, GCPhys));

        rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);

        Log4(("EptMisscfgExit/%u: %04x:%08RX64: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    }
    return rcStrict;
#else
    AssertFailed();
    return VERR_VMX_IPE_3; /* Should never happen with Apple HV in R3. */
#endif
}


/**
 * VM-exit handler for EPT violation (VMX_EXIT_EPT_VIOLATION). Conditional
 * VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitEptViolation(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
#ifndef IN_NEM_DARWIN
    Assert(pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_LEN
                         | HMVMX_READ_EXIT_INTERRUPTION_INFO
                         | HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE
                         | HMVMX_READ_IDT_VECTORING_INFO
                         | HMVMX_READ_IDT_VECTORING_ERROR_CODE
                         | HMVMX_READ_GUEST_PHYSICAL_ADDR>(pVCpu, pVmxTransient);

    /*
     * If this VM-exit occurred while delivering an event through the guest IDT, handle it accordingly.
     */
    VBOXSTRICTRC rcStrict = vmxHCCheckExitDueToEventDelivery(pVCpu, pVmxTransient);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        /*
         * If delivery of an event causes an EPT violation (true nested #PF and not MMIO),
         * we shall resolve the nested #PF and re-inject the original event.
         */
        if (VCPU_2_VMXSTATE(pVCpu).Event.fPending)
            STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatInjectReflectNPF);
    }
    else
    {
        Assert(rcStrict != VINF_HM_DOUBLE_FAULT);
        return rcStrict;
    }

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    int rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    RTGCPHYS const GCPhys    = pVmxTransient->uGuestPhysicalAddr;
    uint64_t const uExitQual = pVmxTransient->uExitQual;
    AssertMsg(((pVmxTransient->uExitQual >> 7) & 3) != 2, ("%#RX64", uExitQual));

    RTGCUINT uErrorCode = 0;
    if (uExitQual & VMX_EXIT_QUAL_EPT_ACCESS_INSTR_FETCH)
        uErrorCode |= X86_TRAP_PF_ID;
    if (uExitQual & VMX_EXIT_QUAL_EPT_ACCESS_WRITE)
        uErrorCode |= X86_TRAP_PF_RW;
    if (uExitQual & (VMX_EXIT_QUAL_EPT_ENTRY_READ | VMX_EXIT_QUAL_EPT_ENTRY_WRITE | VMX_EXIT_QUAL_EPT_ENTRY_EXECUTE))
        uErrorCode |= X86_TRAP_PF_P;

    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    Log4Func(("at %#RX64 (%#RX64 errcode=%#x) cs:rip=%#04x:%08RX64\n", GCPhys, uExitQual, uErrorCode, pCtx->cs.Sel, pCtx->rip));

    PVMCC    pVM  = pVCpu->CTX_SUFF(pVM);

    /*
     * Handle the pagefault trap for the nested shadow table.
     */
    TRPMAssertXcptPF(pVCpu, GCPhys, uErrorCode);
    rcStrict = PGMR0Trap0eHandlerNestedPaging(pVM, pVCpu, PGMMODE_EPT, uErrorCode, pCtx, GCPhys);
    TRPMResetTrap(pVCpu);

    /* Same case as PGMR0Trap0eHandlerNPMisconfig(). See comment above, @bugref{6043}. */
    if (   rcStrict == VINF_SUCCESS
        || rcStrict == VERR_PAGE_TABLE_NOT_PRESENT
        || rcStrict == VERR_PAGE_NOT_PRESENT)
    {
        /* Successfully synced our nested page tables. */
        STAM_COUNTER_INC(&VCPU_2_VMXSTATS(pVCpu).StatExitReasonNpf);
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RSP | HM_CHANGED_GUEST_RFLAGS);
        return VINF_SUCCESS;
    }
    Log4Func(("EPT return to ring-3 rcStrict=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;

#else /* IN_NEM_DARWIN */
    PVM pVM = pVCpu->CTX_SUFF(pVM);
    uint64_t const uHostTsc = ASMReadTSC(); RT_NOREF(uHostTsc);
    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_GUEST_PHYSICAL_ADDR>(pVCpu, pVmxTransient);
    vmxHCImportGuestRip(pVCpu);
    vmxHCImportGuestSegReg<X86_SREG_CS>(pVCpu);

    /*
     * Ask PGM for information about the given GCPhys.  We need to check if we're
     * out of sync first.
     */
    NEMHCDARWINHMACPCCSTATE State = { RT_BOOL(pVmxTransient->uExitQual & VMX_EXIT_QUAL_EPT_ACCESS_WRITE),
                                      false,
                                      false };
    PGMPHYSNEMPAGEINFO      Info;
    int rc = PGMPhysNemPageInfoChecker(pVM, pVCpu, pVmxTransient->uGuestPhysicalAddr, State.fWriteAccess, &Info,
                                       nemR3DarwinHandleMemoryAccessPageCheckerCallback, &State);
    if (RT_SUCCESS(rc))
    {
        if (Info.fNemProt & (  RT_BOOL(pVmxTransient->uExitQual & VMX_EXIT_QUAL_EPT_ACCESS_WRITE)
                             ? NEM_PAGE_PROT_WRITE : NEM_PAGE_PROT_READ))
        {
            if (State.fCanResume)
            {
                Log4(("MemExit/%u: %04x:%08RX64: %RGp (=>%RHp) %s fProt=%u%s%s%s; restarting\n",
                      pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
                      pVmxTransient->uGuestPhysicalAddr, Info.HCPhys, g_apszPageStates[Info.u2NemState], Info.fNemProt,
                      Info.fHasHandlers ? " handlers" : "", Info.fZeroPage    ? " zero-pg" : "",
                      State.fDidSomething ? "" : " no-change"));
                EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_MEMORY_ACCESS),
                                 pVCpu->cpum.GstCtx.cs.u64Base + pVCpu->cpum.GstCtx.rip, uHostTsc);
                return VINF_SUCCESS;
            }
        }

        Log4(("MemExit/%u: %04x:%08RX64: %RGp (=>%RHp) %s fProt=%u%s%s%s; emulating\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              pVmxTransient->uGuestPhysicalAddr, Info.HCPhys, g_apszPageStates[Info.u2NemState], Info.fNemProt,
              Info.fHasHandlers ? " handlers" : "", Info.fZeroPage    ? " zero-pg" : "",
              State.fDidSomething ? "" : " no-change"));
    }
    else
        Log4(("MemExit/%u: %04x:%08RX64: %RGp rc=%Rrc%s; emulating\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              pVmxTransient->uGuestPhysicalAddr, rc, State.fDidSomething ? " modified-backing" : ""));

    /*
     * Emulate the memory access, either access handler or special memory.
     */
    PCEMEXITREC pExitRec = EMHistoryAddExit(pVCpu,
                                              RT_BOOL(pVmxTransient->uExitQual & VMX_EXIT_QUAL_EPT_ACCESS_WRITE)
                                            ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_WRITE)
                                            : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_READ),
                                            pVCpu->cpum.GstCtx.cs.u64Base + pVCpu->cpum.GstCtx.rip, uHostTsc);

    rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict;
    if (!pExitRec)
        rcStrict = IEMExecOne(pVCpu);
    else
    {
        /* Frequent access or probing. */
        rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
        Log4(("MemExit/%u: %04x:%08RX64: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    }

    ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);

    Log4Func(("EPT return rcStrict=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
#endif /* IN_NEM_DARWIN */
}

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX

/**
 * VM-exit handler for VMCLEAR (VMX_EXIT_VMCLEAR). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmclear(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_RSP
                                   | CPUMCTX_EXTRN_SREG_MASK
                                   | CPUMCTX_EXTRN_HWVIRT
                                   | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    VMXVEXITINFO ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
    HMVMX_DECODE_MEM_OPERAND(pVCpu, ExitInfo.InstrInfo.u, ExitInfo.u64Qual, VMXMEMACCESS_READ, &ExitInfo.GCPtrEffAddr);

    VBOXSTRICTRC rcStrict = IEMExecDecodedVmclear(pVCpu, &ExitInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_HWVIRT);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for VMLAUNCH (VMX_EXIT_VMLAUNCH). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmlaunch(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /* Import the entire VMCS state for now as we would be switching VMCS on successful VMLAUNCH,
       otherwise we could import just IEM_CPUMCTX_EXTRN_VMX_VMENTRY_MASK. */
    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    STAM_PROFILE_ADV_START(&VCPU_2_VMXSTATS(pVCpu).StatExitVmentry, z);
    VBOXSTRICTRC rcStrict = IEMExecDecodedVmlaunchVmresume(pVCpu, pVmxTransient->cbExitInstr, VMXINSTRID_VMLAUNCH);
    STAM_PROFILE_ADV_STOP(&VCPU_2_VMXSTATS(pVCpu).StatExitVmentry, z);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);
        if (CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.GstCtx))
            rcStrict = VINF_VMX_VMLAUNCH_VMRESUME;
    }
    Assert(rcStrict != VINF_IEM_RAISED_XCPT);
    return rcStrict;
}


/**
 * VM-exit handler for VMPTRLD (VMX_EXIT_VMPTRLD). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmptrld(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_RSP
                                   | CPUMCTX_EXTRN_SREG_MASK
                                   | CPUMCTX_EXTRN_HWVIRT
                                   | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    VMXVEXITINFO ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
    HMVMX_DECODE_MEM_OPERAND(pVCpu, ExitInfo.InstrInfo.u, ExitInfo.u64Qual, VMXMEMACCESS_READ, &ExitInfo.GCPtrEffAddr);

    VBOXSTRICTRC rcStrict = IEMExecDecodedVmptrld(pVCpu, &ExitInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_HWVIRT);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for VMPTRST (VMX_EXIT_VMPTRST). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmptrst(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_RSP
                                   | CPUMCTX_EXTRN_SREG_MASK
                                   | CPUMCTX_EXTRN_HWVIRT
                                   | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    VMXVEXITINFO ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
    HMVMX_DECODE_MEM_OPERAND(pVCpu, ExitInfo.InstrInfo.u, ExitInfo.u64Qual, VMXMEMACCESS_WRITE, &ExitInfo.GCPtrEffAddr);

    VBOXSTRICTRC rcStrict = IEMExecDecodedVmptrst(pVCpu, &ExitInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for VMREAD (VMX_EXIT_VMREAD). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmread(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /*
     * Strictly speaking we should not get VMREAD VM-exits for shadow VMCS fields and
     * thus might not need to import the shadow VMCS state, it's safer just in case
     * code elsewhere dares look at unsynced VMCS fields.
     */
    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_RSP
                                   | CPUMCTX_EXTRN_SREG_MASK
                                   | CPUMCTX_EXTRN_HWVIRT
                                   | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    VMXVEXITINFO ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
    if (!ExitInfo.InstrInfo.VmreadVmwrite.fIsRegOperand)
        HMVMX_DECODE_MEM_OPERAND(pVCpu, ExitInfo.InstrInfo.u, ExitInfo.u64Qual, VMXMEMACCESS_WRITE, &ExitInfo.GCPtrEffAddr);

    VBOXSTRICTRC rcStrict = IEMExecDecodedVmread(pVCpu, &ExitInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);

# if 0 //ndef IN_NEM_DARWIN /** @todo this needs serious tuning still, slows down things enormously. */
        /* Try for exit optimization.  This is on the following instruction
           because it would be a waste of time to have to reinterpret the
           already decoded vmwrite instruction. */
        PCEMEXITREC pExitRec = EMHistoryUpdateFlagsAndType(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_VMREAD));
        if (pExitRec)
        {
            /* Frequent access or probing. */
            rc = vmxHCImportGuestState(pVCpu, pVmxTransient->pVmcsInfo, HMVMX_CPUMCTX_EXTRN_ALL);
            AssertRCReturn(rc, rc);

            rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
            Log4(("vmread/%u: %04x:%08RX64: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
                  pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
                  VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);
        }
# endif
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for VMRESUME (VMX_EXIT_VMRESUME). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmresume(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /* Import the entire VMCS state for now as we would be switching VMCS on successful VMRESUME,
       otherwise we could import just IEM_CPUMCTX_EXTRN_VMX_VMENTRY_MASK. */
    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    STAM_PROFILE_ADV_START(&VCPU_2_VMXSTATS(pVCpu).StatExitVmentry, z);
    VBOXSTRICTRC rcStrict = IEMExecDecodedVmlaunchVmresume(pVCpu, pVmxTransient->cbExitInstr, VMXINSTRID_VMRESUME);
    STAM_PROFILE_ADV_STOP(&VCPU_2_VMXSTATS(pVCpu).StatExitVmentry, z);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);
        if (CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.GstCtx))
            rcStrict = VINF_VMX_VMLAUNCH_VMRESUME;
    }
    Assert(rcStrict != VINF_IEM_RAISED_XCPT);
    return rcStrict;
}


/**
 * VM-exit handler for VMWRITE (VMX_EXIT_VMWRITE). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmwrite(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /*
     * Although we should not get VMWRITE VM-exits for shadow VMCS fields, since our HM hook
     * gets invoked when IEM's VMWRITE instruction emulation modifies the current VMCS and it
     * flags re-loading the entire shadow VMCS, we should save the entire shadow VMCS here.
     */
    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_RSP
                                   | CPUMCTX_EXTRN_SREG_MASK
                                   | CPUMCTX_EXTRN_HWVIRT
                                   | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    VMXVEXITINFO ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
    if (!ExitInfo.InstrInfo.VmreadVmwrite.fIsRegOperand)
        HMVMX_DECODE_MEM_OPERAND(pVCpu, ExitInfo.InstrInfo.u, ExitInfo.u64Qual, VMXMEMACCESS_READ, &ExitInfo.GCPtrEffAddr);

    VBOXSTRICTRC rcStrict = IEMExecDecodedVmwrite(pVCpu, &ExitInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_HWVIRT);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for VMXOFF (VMX_EXIT_VMXOFF). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmxoff(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_CR4
                                   | CPUMCTX_EXTRN_HWVIRT
                                   | IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    VBOXSTRICTRC rcStrict = IEMExecDecodedVmxoff(pVCpu, pVmxTransient->cbExitInstr);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_HWVIRT);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for VMXON (VMX_EXIT_VMXON). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmxon(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_RSP
                                   | CPUMCTX_EXTRN_SREG_MASK
                                   | CPUMCTX_EXTRN_HWVIRT
                                   | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    VMXVEXITINFO ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
    HMVMX_DECODE_MEM_OPERAND(pVCpu, ExitInfo.InstrInfo.u, ExitInfo.u64Qual, VMXMEMACCESS_READ, &ExitInfo.GCPtrEffAddr);

    VBOXSTRICTRC rcStrict = IEMExecDecodedVmxon(pVCpu, &ExitInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_HWVIRT);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * VM-exit handler for INVVPID (VMX_EXIT_INVVPID). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitInvvpid(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_RSP
                                   | CPUMCTX_EXTRN_SREG_MASK
                                   | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    VMXVEXITINFO ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
    HMVMX_DECODE_MEM_OPERAND(pVCpu, ExitInfo.InstrInfo.u, ExitInfo.u64Qual, VMXMEMACCESS_READ, &ExitInfo.GCPtrEffAddr);

    VBOXSTRICTRC rcStrict = IEMExecDecodedInvvpid(pVCpu, &ExitInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * VM-exit handler for INVEPT (VMX_EXIT_INVEPT). Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitInvept(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_INFO
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    int rc = vmxHCImportGuestState<  CPUMCTX_EXTRN_RSP
                                   | CPUMCTX_EXTRN_SREG_MASK
                                   | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
    AssertRCReturn(rc, rc);

    HMVMX_CHECK_EXIT_DUE_TO_VMX_INSTR(pVCpu, pVmxTransient->uExitReason);

    VMXVEXITINFO ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
    HMVMX_DECODE_MEM_OPERAND(pVCpu, ExitInfo.InstrInfo.u, ExitInfo.u64Qual, VMXMEMACCESS_READ, &ExitInfo.GCPtrEffAddr);

    VBOXSTRICTRC rcStrict = IEMExecDecodedInvept(pVCpu, &ExitInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_RIP | HM_CHANGED_GUEST_RFLAGS);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}
# endif /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */
/** @} */


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/** @name Nested-guest VM-exit handlers.
 * @{
 */
/* -=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */
/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- Nested-guest VM-exit handlers -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */
/* -=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */

/**
 * Nested-guest VM-exit handler for exceptions or NMIs (VMX_EXIT_XCPT_OR_NMI).
 * Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitXcptOrNmiNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<HMVMX_READ_EXIT_INTERRUPTION_INFO>(pVCpu, pVmxTransient);

    uint64_t const uExitIntInfo = pVmxTransient->uExitIntInfo;
    uint32_t const uExitIntType = VMX_EXIT_INT_INFO_TYPE(uExitIntInfo);
    Assert(VMX_EXIT_INT_INFO_IS_VALID(uExitIntInfo));

    switch (uExitIntType)
    {
# ifndef IN_NEM_DARWIN
        /*
         * Physical NMIs:
         *     We shouldn't direct host physical NMIs to the nested-guest. Dispatch it to the host.
         */
        case VMX_EXIT_INT_INFO_TYPE_NMI:
            return hmR0VmxExitHostNmi(pVCpu, pVmxTransient->pVmcsInfo);
# endif

        /*
         * Hardware exceptions,
         * Software exceptions,
         * Privileged software exceptions:
         *     Figure out if the exception must be delivered to the guest or the nested-guest.
         */
        case VMX_EXIT_INT_INFO_TYPE_SW_XCPT:
        case VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT:
        case VMX_EXIT_INT_INFO_TYPE_HW_XCPT:
        {
            vmxHCReadToTransient<  HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE
                                 | HMVMX_READ_EXIT_INSTR_LEN
                                 | HMVMX_READ_IDT_VECTORING_INFO
                                 | HMVMX_READ_IDT_VECTORING_ERROR_CODE>(pVCpu, pVmxTransient);

            PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
            uint8_t const uVector = VMX_EXIT_INT_INFO_VECTOR(uExitIntInfo);
            if (CPUMIsGuestVmxXcptInterceptSet(pCtx, uVector, pVmxTransient->uExitIntErrorCode))
            {
                /*
                 * Split-lock triggered #ACs should not be injected into the nested-guest
                 * since we don't support split-lock detection for nested-guests yet.
                 */
                if (   uVector == X86_XCPT_AC
                    && uExitIntType == VMX_EXIT_INT_INFO_TYPE_HW_XCPT)
                {
                    int const rc = vmxHCImportGuestState<HMVMX_CPUMCTX_XPCT_AC>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
                    AssertRCReturn(rc, rc);
                    if (vmxHCIsSplitLockAcXcpt(pVCpu))
                    {
                        VBOXSTRICTRC rcStrict = vmxHCCheckExitDueToEventDelivery(pVCpu, pVmxTransient);
                        if (    rcStrict == VINF_SUCCESS
                            && !VCPU_2_VMXSTATE(pVCpu).Event.fPending)
                            return vmxHCHandleSplitLockAcXcpt(pVCpu, pVmxTransient);
                        if (rcStrict == VINF_HM_DOUBLE_FAULT)
                        {
                            Assert(VCPU_2_VMXSTATE(pVCpu).Event.fPending);
                            rcStrict = VINF_SUCCESS;
                        }
                        return rcStrict;
                    }
                }

                /* Exit qualification is required for debug and page-fault exceptions. */
                vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);

                /*
                 * For VM-exits due to software exceptions (those generated by INT3 or INTO) and privileged
                 * software exceptions (those generated by INT1/ICEBP) we need to supply the VM-exit instruction
                 * length. However, if delivery of a software interrupt, software exception or privileged
                 * software exception causes a VM-exit, that too provides the VM-exit instruction length.
                 */
                VMXVEXITINFO const      ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(pVmxTransient);
                VMXVEXITEVENTINFO const ExitEventInfo = VMXVEXITEVENTINFO_INIT(pVmxTransient->uExitIntInfo,
                                                                               pVmxTransient->uExitIntErrorCode,
                                                                               pVmxTransient->uIdtVectoringInfo,
                                                                               pVmxTransient->uIdtVectoringErrorCode);
#ifdef DEBUG_ramshankar
                vmxHCImportGuestStateEx(pVCpu, pVmxTransient->pVmcsInfo, HMVMX_CPUMCTX_EXTRN_ALL);
                Log4Func(("exit_int_info=%#RX32 err_code=%#RX32 exit_qual=%#RX64\n",
                          pVmxTransient->uExitIntInfo, pVmxTransient->uExitIntErrorCode, pVmxTransient->uExitQual));
                if (VMX_IDT_VECTORING_INFO_IS_VALID(pVmxTransient->uIdtVectoringInfo))
                    Log4Func(("idt_info=%#RX32 idt_errcode=%#RX32 cr2=%#RX64\n",
                              pVmxTransient->uIdtVectoringInfo, pVmxTransient->uIdtVectoringErrorCode, pCtx->cr2));
#endif
                return IEMExecVmxVmexitXcpt(pVCpu, &ExitInfo, &ExitEventInfo);
            }

            /* Nested paging is currently a requirement, otherwise we would need to handle shadow #PFs in vmxHCExitXcptPF. */
            Assert(pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);
            return vmxHCExitXcpt(pVCpu, pVmxTransient);
        }

        /*
         * Software interrupts:
         *    VM-exits cannot be caused by software interrupts.
         *
         * External interrupts:
         *    This should only happen when "acknowledge external interrupts on VM-exit"
         *    control is set. However, we never set this when executing a guest or
         *    nested-guest. For nested-guests it is emulated while injecting interrupts into
         *    the guest.
         */
        case VMX_EXIT_INT_INFO_TYPE_SW_INT:
        case VMX_EXIT_INT_INFO_TYPE_EXT_INT:
        default:
        {
            VCPU_2_VMXSTATE(pVCpu).u32HMError = pVmxTransient->uExitIntInfo;
            return VERR_VMX_UNEXPECTED_INTERRUPTION_EXIT_TYPE;
        }
    }
}


/**
 * Nested-guest VM-exit handler for triple faults (VMX_EXIT_TRIPLE_FAULT).
 * Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitTripleFaultNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    return IEMExecVmxVmexitTripleFault(pVCpu);
}


/**
 * Nested-guest VM-exit handler for interrupt-window exiting (VMX_EXIT_INT_WINDOW).
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitIntWindowNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_INT_WINDOW_EXIT))
        return IEMExecVmxVmexit(pVCpu, pVmxTransient->uExitReason, 0 /* uExitQual */);
    return vmxHCExitIntWindow(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for NMI-window exiting (VMX_EXIT_NMI_WINDOW).
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitNmiWindowNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_NMI_WINDOW_EXIT))
        return IEMExecVmxVmexit(pVCpu, pVmxTransient->uExitReason, 0 /* uExitQual */);
    return vmxHCExitNmiWindow(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for task switches (VMX_EXIT_TASK_SWITCH).
 * Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitTaskSwitchNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_LEN
                         | HMVMX_READ_IDT_VECTORING_INFO
                         | HMVMX_READ_IDT_VECTORING_ERROR_CODE>(pVCpu, pVmxTransient);

    VMXVEXITINFO const      ExitInfo      = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(pVmxTransient);
    VMXVEXITEVENTINFO const ExitEventInfo = VMXVEXITEVENTINFO_INIT_ONLY_IDT(pVmxTransient->uIdtVectoringInfo,
                                                                            pVmxTransient->uIdtVectoringErrorCode);
    return IEMExecVmxVmexitTaskSwitch(pVCpu, &ExitInfo, &ExitEventInfo);
}


/**
 * Nested-guest VM-exit handler for HLT (VMX_EXIT_HLT). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitHltNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_HLT_EXIT))
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }
    return vmxHCExitHlt(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for INVLPG (VMX_EXIT_INVLPG). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitInvlpgNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_INVLPG_EXIT))
    {
        vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                             | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(pVmxTransient);
        return IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }
    return vmxHCExitInvlpg(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for RDPMC (VMX_EXIT_RDPMC). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitRdpmcNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_RDPMC_EXIT))
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }
    return vmxHCExitRdpmc(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for VMREAD (VMX_EXIT_VMREAD) and VMWRITE
 * (VMX_EXIT_VMWRITE). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVmreadVmwriteNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    Assert(   pVmxTransient->uExitReason == VMX_EXIT_VMREAD
           || pVmxTransient->uExitReason == VMX_EXIT_VMWRITE);

    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_INFO>(pVCpu, pVmxTransient);

    uint8_t const iGReg = pVmxTransient->ExitInstrInfo.VmreadVmwrite.iReg2;
    Assert(iGReg < RT_ELEMENTS(pVCpu->cpum.GstCtx.aGRegs));
    uint64_t u64VmcsField = pVCpu->cpum.GstCtx.aGRegs[iGReg].u64;

    HMVMX_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_EFER);
    if (!CPUMIsGuestInLongModeEx(&pVCpu->cpum.GstCtx))
        u64VmcsField &= UINT64_C(0xffffffff);

    if (CPUMIsGuestVmxVmreadVmwriteInterceptSet(pVCpu, pVmxTransient->uExitReason, u64VmcsField))
    {
        vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                             | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
        return IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }

    if (pVmxTransient->uExitReason == VMX_EXIT_VMREAD)
        return vmxHCExitVmread(pVCpu, pVmxTransient);
    return vmxHCExitVmwrite(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for RDTSC (VMX_EXIT_RDTSC). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitRdtscNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_RDTSC_EXIT))
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }

    return vmxHCExitRdtsc(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for control-register accesses (VMX_EXIT_MOV_CRX).
 * Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitMovCRxNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);

    VBOXSTRICTRC rcStrict;
    uint32_t const uAccessType = VMX_EXIT_QUAL_CRX_ACCESS(pVmxTransient->uExitQual);
    switch (uAccessType)
    {
        case VMX_EXIT_QUAL_CRX_ACCESS_WRITE:
        {
            uint8_t const iCrReg   = VMX_EXIT_QUAL_CRX_REGISTER(pVmxTransient->uExitQual);
            uint8_t const iGReg    = VMX_EXIT_QUAL_CRX_GENREG(pVmxTransient->uExitQual);
            Assert(iGReg < RT_ELEMENTS(pVCpu->cpum.GstCtx.aGRegs));
            uint64_t const uNewCrX = pVCpu->cpum.GstCtx.aGRegs[iGReg].u64;

            bool fIntercept;
            switch (iCrReg)
            {
                case 0:
                case 4:
                    fIntercept = CPUMIsGuestVmxMovToCr0Cr4InterceptSet(&pVCpu->cpum.GstCtx, iCrReg, uNewCrX);
                    break;

                case 3:
                    fIntercept = CPUMIsGuestVmxMovToCr3InterceptSet(pVCpu, uNewCrX);
                    break;

                case 8:
                    fIntercept = CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_CR8_LOAD_EXIT);
                    break;

                default:
                    fIntercept = false;
                    break;
            }
            if (fIntercept)
            {
                VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(pVmxTransient);
                rcStrict = IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
            }
            else
            {
                int const rc = vmxHCImportGuestState<IEM_CPUMCTX_EXTRN_MUST_MASK>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
                AssertRCReturn(rc, rc);
                rcStrict = vmxHCExitMovToCrX(pVCpu, pVmxTransient->cbExitInstr, iGReg, iCrReg);
            }
            break;
        }

        case VMX_EXIT_QUAL_CRX_ACCESS_READ:
        {
            /*
             * CR0/CR4 reads do not cause VM-exits, the read-shadow is used (subject to masking).
             * CR2 reads do not cause a VM-exit.
             * CR3 reads cause a VM-exit depending on the "CR3 store exiting" control.
             * CR8 reads cause a VM-exit depending on the "CR8 store exiting" control.
             */
            uint8_t const iCrReg = VMX_EXIT_QUAL_CRX_REGISTER(pVmxTransient->uExitQual);
            if (   iCrReg == 3
                || iCrReg == 8)
            {
                static const uint32_t s_auCrXReadIntercepts[] = { 0, 0, 0, VMX_PROC_CTLS_CR3_STORE_EXIT, 0,
                                                                  0, 0, 0, VMX_PROC_CTLS_CR8_STORE_EXIT };
                uint32_t const uIntercept = s_auCrXReadIntercepts[iCrReg];
                if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, uIntercept))
                {
                    VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(pVmxTransient);
                    rcStrict = IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
                }
                else
                {
                    uint8_t const iGReg = VMX_EXIT_QUAL_CRX_GENREG(pVmxTransient->uExitQual);
                    rcStrict = vmxHCExitMovFromCrX(pVCpu, pVmxTransient->pVmcsInfo, pVmxTransient->cbExitInstr, iGReg, iCrReg);
                }
            }
            else
            {
                AssertMsgFailed(("MOV from CR%d VM-exit must not happen\n", iCrReg));
                HMVMX_UNEXPECTED_EXIT_RET(pVCpu, iCrReg);
            }
            break;
        }

        case VMX_EXIT_QUAL_CRX_ACCESS_CLTS:
        {
            PCVMXVVMCS const pVmcsNstGst  = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
            uint64_t const   uGstHostMask = pVmcsNstGst->u64Cr0Mask.u;
            uint64_t const   uReadShadow  = pVmcsNstGst->u64Cr0ReadShadow.u;
            if (   (uGstHostMask & X86_CR0_TS)
                && (uReadShadow  & X86_CR0_TS))
            {
                VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(pVmxTransient);
                rcStrict = IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
            }
            else
                rcStrict = vmxHCExitClts(pVCpu, pVmxTransient->pVmcsInfo, pVmxTransient->cbExitInstr);
            break;
        }

        case VMX_EXIT_QUAL_CRX_ACCESS_LMSW:        /* LMSW (Load Machine-Status Word into CR0) */
        {
            RTGCPTR        GCPtrEffDst;
            uint16_t const uNewMsw     = VMX_EXIT_QUAL_CRX_LMSW_DATA(pVmxTransient->uExitQual);
            bool const     fMemOperand = VMX_EXIT_QUAL_CRX_LMSW_OP_MEM(pVmxTransient->uExitQual);
            if (fMemOperand)
            {
                vmxHCReadToTransient<HMVMX_READ_GUEST_LINEAR_ADDR>(pVCpu, pVmxTransient);
                GCPtrEffDst = pVmxTransient->uGuestLinearAddr;
            }
            else
                GCPtrEffDst = NIL_RTGCPTR;

            if (CPUMIsGuestVmxLmswInterceptSet(&pVCpu->cpum.GstCtx, uNewMsw))
            {
                VMXVEXITINFO ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(pVmxTransient);
                ExitInfo.u64GuestLinearAddr = GCPtrEffDst;
                rcStrict = IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
            }
            else
                rcStrict = vmxHCExitLmsw(pVCpu, pVmxTransient->pVmcsInfo, pVmxTransient->cbExitInstr, uNewMsw, GCPtrEffDst);
            break;
        }

        default:
        {
            AssertMsgFailed(("Unrecognized Mov CRX access type %#x\n", uAccessType));
            HMVMX_UNEXPECTED_EXIT_RET(pVCpu, uAccessType);
        }
    }

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * Nested-guest VM-exit handler for debug-register accesses (VMX_EXIT_MOV_DRX).
 * Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitMovDRxNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_MOV_DR_EXIT))
    {
        vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                             | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(pVmxTransient);
        return IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }
    return vmxHCExitMovDRx(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for I/O instructions (VMX_EXIT_IO_INSTR).
 * Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitIoInstrNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);

    uint32_t const uIOPort = VMX_EXIT_QUAL_IO_PORT(pVmxTransient->uExitQual);
    uint8_t  const uIOSize = VMX_EXIT_QUAL_IO_SIZE(pVmxTransient->uExitQual);
    AssertReturn(uIOSize <= 3 && uIOSize != 2, VERR_VMX_IPE_1);

    static uint32_t const s_aIOSizes[4] = { 1, 2, 0, 4 };   /* Size of the I/O accesses in bytes. */
    uint8_t const cbAccess = s_aIOSizes[uIOSize];
    if (CPUMIsGuestVmxIoInterceptSet(pVCpu, uIOPort, cbAccess))
    {
        /*
         * IN/OUT instruction:
         *   - Provides VM-exit instruction length.
         *
         * INS/OUTS instruction:
         *   - Provides VM-exit instruction length.
         *   - Provides Guest-linear address.
         *   - Optionally provides VM-exit instruction info (depends on CPU feature).
         */
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);

        /* Make sure we don't use stale/uninitialized VMX-transient info. below. */
        pVmxTransient->ExitInstrInfo.u  = 0;
        pVmxTransient->uGuestLinearAddr = 0;

        bool const fVmxInsOutsInfo = pVM->cpum.ro.GuestFeatures.fVmxInsOutInfo;
        bool const fIOString       = VMX_EXIT_QUAL_IO_IS_STRING(pVmxTransient->uExitQual);
        if (fIOString)
        {
            vmxHCReadToTransient<HMVMX_READ_GUEST_LINEAR_ADDR>(pVCpu, pVmxTransient);
            if (fVmxInsOutsInfo)
            {
                Assert(RT_BF_GET(g_HmMsrs.u.vmx.u64Basic, VMX_BF_BASIC_VMCS_INS_OUTS)); /* Paranoia. */
                vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_INFO>(pVCpu, pVmxTransient);
            }
        }

        VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_AND_LIN_ADDR_FROM_TRANSIENT(pVmxTransient);
        return IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }
    return vmxHCExitIoInstr(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for RDMSR (VMX_EXIT_RDMSR).
 */
HMVMX_EXIT_DECL vmxHCExitRdmsrNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    uint32_t fMsrpm;
    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_USE_MSR_BITMAPS))
        fMsrpm = CPUMGetVmxMsrPermission(pVCpu->cpum.GstCtx.hwvirt.vmx.abMsrBitmap, pVCpu->cpum.GstCtx.ecx);
    else
        fMsrpm = VMXMSRPM_EXIT_RD;

    if (fMsrpm & VMXMSRPM_EXIT_RD)
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }
    return vmxHCExitRdmsr(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for WRMSR (VMX_EXIT_WRMSR).
 */
HMVMX_EXIT_DECL vmxHCExitWrmsrNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    uint32_t fMsrpm;
    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_USE_MSR_BITMAPS))
        fMsrpm = CPUMGetVmxMsrPermission(pVCpu->cpum.GstCtx.hwvirt.vmx.abMsrBitmap, pVCpu->cpum.GstCtx.ecx);
    else
        fMsrpm = VMXMSRPM_EXIT_WR;

    if (fMsrpm & VMXMSRPM_EXIT_WR)
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }
    return vmxHCExitWrmsr(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for MWAIT (VMX_EXIT_MWAIT). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitMwaitNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_MWAIT_EXIT))
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }
    return vmxHCExitMwait(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for monitor-trap-flag (VMX_EXIT_MTF). Conditional
 * VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitMtfNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /** @todo NSTVMX: Should consider debugging nested-guests using VM debugger. */
    vmxHCReadToTransient<HMVMX_READ_GUEST_PENDING_DBG_XCPTS>(pVCpu, pVmxTransient);
    VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_DBG_XCPTS_FROM_TRANSIENT(pVmxTransient);
    return IEMExecVmxVmexitTrapLike(pVCpu, &ExitInfo);
}


/**
 * Nested-guest VM-exit handler for MONITOR (VMX_EXIT_MONITOR). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitMonitorNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_MONITOR_EXIT))
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }
    return vmxHCExitMonitor(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for PAUSE (VMX_EXIT_PAUSE). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitPauseNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /** @todo NSTVMX: Think about this more. Does the outer guest need to intercept
     *        PAUSE when executing a nested-guest? If it does not, we would not need
     *        to check for the intercepts here. Just call VM-exit... */

    /* The CPU would have already performed the necessary CPL checks for PAUSE-loop exiting. */
    if (   CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_PAUSE_EXIT)
        || CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS2_PAUSE_LOOP_EXIT))
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }
    return vmxHCExitPause(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for when the TPR value is lowered below the
 * specified threshold (VMX_EXIT_TPR_BELOW_THRESHOLD). Conditional VM-exit.
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitTprBelowThresholdNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_USE_TPR_SHADOW))
    {
        vmxHCReadToTransient<HMVMX_READ_GUEST_PENDING_DBG_XCPTS>(pVCpu, pVmxTransient);
        VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_DBG_XCPTS_FROM_TRANSIENT(pVmxTransient);
        return IEMExecVmxVmexitTrapLike(pVCpu, &ExitInfo);
    }
    return vmxHCExitTprBelowThreshold(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for APIC access (VMX_EXIT_APIC_ACCESS). Conditional
 * VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitApicAccessNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_LEN
                         | HMVMX_READ_IDT_VECTORING_INFO
                         | HMVMX_READ_IDT_VECTORING_ERROR_CODE>(pVCpu, pVmxTransient);

    Assert(CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS2_VIRT_APIC_ACCESS));

    Log4Func(("at offset %#x type=%u\n", VMX_EXIT_QUAL_APIC_ACCESS_OFFSET(pVmxTransient->uExitQual),
              VMX_EXIT_QUAL_APIC_ACCESS_TYPE(pVmxTransient->uExitQual)));

    VMXVEXITINFO const      ExitInfo      = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_FROM_TRANSIENT(pVmxTransient);
    VMXVEXITEVENTINFO const ExitEventInfo = VMXVEXITEVENTINFO_INIT_ONLY_IDT(pVmxTransient->uIdtVectoringInfo,
                                                                            pVmxTransient->uIdtVectoringErrorCode);
    return IEMExecVmxVmexitApicAccess(pVCpu, &ExitInfo, &ExitEventInfo);
}


/**
 * Nested-guest VM-exit handler for APIC write emulation (VMX_EXIT_APIC_WRITE).
 * Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitApicWriteNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    Assert(CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS2_APIC_REG_VIRT));
    vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);
    return IEMExecVmxVmexit(pVCpu, pVmxTransient->uExitReason, pVmxTransient->uExitQual);
}


/**
 * Nested-guest VM-exit handler for virtualized EOI (VMX_EXIT_VIRTUALIZED_EOI).
 * Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitVirtEoiNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    Assert(CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS2_VIRT_INT_DELIVERY));
    vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);
    return IEMExecVmxVmexit(pVCpu, pVmxTransient->uExitReason, pVmxTransient->uExitQual);
}


/**
 * Nested-guest VM-exit handler for RDTSCP (VMX_EXIT_RDTSCP). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitRdtscpNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_RDTSC_EXIT))
    {
        Assert(CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS2_RDTSCP));
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }
    return vmxHCExitRdtscp(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for WBINVD (VMX_EXIT_WBINVD). Conditional VM-exit.
 */
HMVMX_EXIT_NSRC_DECL vmxHCExitWbinvdNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS2_WBINVD_EXIT))
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
    }
    return vmxHCExitWbinvd(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for INVPCID (VMX_EXIT_INVPCID). Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitInvpcidNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    if (CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_INVLPG_EXIT))
    {
        Assert(CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS2_INVPCID));
        vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                             | HMVMX_READ_EXIT_INSTR_INFO
                             | HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
        VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
        return IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }
    return vmxHCExitInvpcid(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for invalid-guest state
 * (VMX_EXIT_ERR_INVALID_GUEST_STATE). Error VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitErrInvalidGuestStateNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

    /*
     * Currently this should never happen because we fully emulate VMLAUNCH/VMRESUME in IEM.
     * So if it does happen, it indicates a bug possibly in the hardware-assisted VMX code.
     * Handle it like it's in an invalid guest state of the outer guest.
     *
     * When the fast path is implemented, this should be changed to cause the corresponding
     * nested-guest VM-exit.
     */
    return vmxHCExitErrInvalidGuestState(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for instructions that cause VM-exits unconditionally
 * and only provide the instruction length.
 *
 * Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitInstrNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

#ifdef VBOX_STRICT
    PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    switch (pVmxTransient->uExitReason)
    {
        case VMX_EXIT_ENCLS:
            Assert(CPUMIsGuestVmxProcCtls2Set(pCtx, VMX_PROC_CTLS2_ENCLS_EXIT));
            break;

        case VMX_EXIT_VMFUNC:
            Assert(CPUMIsGuestVmxProcCtls2Set(pCtx, VMX_PROC_CTLS2_VMFUNC));
            break;
    }
#endif

    vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_LEN>(pVCpu, pVmxTransient);
    return IEMExecVmxVmexitInstr(pVCpu, pVmxTransient->uExitReason, pVmxTransient->cbExitInstr);
}


/**
 * Nested-guest VM-exit handler for instructions that provide instruction length as
 * well as more information.
 *
 * Unconditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitInstrWithInfoNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_NESTED_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);

# ifdef VBOX_STRICT
    PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    switch (pVmxTransient->uExitReason)
    {
        case VMX_EXIT_GDTR_IDTR_ACCESS:
        case VMX_EXIT_LDTR_TR_ACCESS:
            Assert(CPUMIsGuestVmxProcCtls2Set(pCtx, VMX_PROC_CTLS2_DESC_TABLE_EXIT));
            break;

        case VMX_EXIT_RDRAND:
            Assert(CPUMIsGuestVmxProcCtls2Set(pCtx, VMX_PROC_CTLS2_RDRAND_EXIT));
            break;

        case VMX_EXIT_RDSEED:
            Assert(CPUMIsGuestVmxProcCtls2Set(pCtx, VMX_PROC_CTLS2_RDSEED_EXIT));
            break;

        case VMX_EXIT_XSAVES:
        case VMX_EXIT_XRSTORS:
            /** @todo NSTVMX: Verify XSS-bitmap. */
            Assert(CPUMIsGuestVmxProcCtls2Set(pCtx, VMX_PROC_CTLS2_XSAVES_XRSTORS));
            break;

        case VMX_EXIT_UMWAIT:
        case VMX_EXIT_TPAUSE:
            Assert(CPUMIsGuestVmxProcCtlsSet(pCtx, VMX_PROC_CTLS_RDTSC_EXIT));
            Assert(CPUMIsGuestVmxProcCtls2Set(pCtx, VMX_PROC_CTLS2_USER_WAIT_PAUSE));
            break;

        case VMX_EXIT_LOADIWKEY:
            Assert(CPUMIsGuestVmxProcCtls3Set(pCtx, VMX_PROC_CTLS3_LOADIWKEY_EXIT));
            break;
    }
# endif

    vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                         | HMVMX_READ_EXIT_INSTR_LEN
                         | HMVMX_READ_EXIT_INSTR_INFO>(pVCpu, pVmxTransient);
    VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_FROM_TRANSIENT(pVmxTransient);
    return IEMExecVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
}

# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT

/**
 * Nested-guest VM-exit handler for EPT violation (VMX_EXIT_EPT_VIOLATION).
 * Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitEptViolationNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    Assert(pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    if (CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS2_EPT))
    {
        vmxHCReadToTransient<  HMVMX_READ_EXIT_QUALIFICATION
                             | HMVMX_READ_EXIT_INSTR_LEN
                             | HMVMX_READ_EXIT_INTERRUPTION_INFO
                             | HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE
                             | HMVMX_READ_IDT_VECTORING_INFO
                             | HMVMX_READ_IDT_VECTORING_ERROR_CODE
                             | HMVMX_READ_GUEST_PHYSICAL_ADDR>(pVCpu, pVmxTransient);
        int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmcsInfo, __FUNCTION__);
        AssertRCReturn(rc, rc);

        /*
         * If it's our VMEXIT, we're responsible for re-injecting any event which delivery
         * might have triggered this VMEXIT.  If we forward the problem to the inner VMM,
         * it's its problem to deal with that issue and we'll clear the recovered event.
         */
        VBOXSTRICTRC rcStrict = vmxHCCheckExitDueToEventDelivery(pVCpu, pVmxTransient);
        if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        { /*likely*/ }
        else
        {
            Assert(rcStrict != VINF_HM_DOUBLE_FAULT);
            return rcStrict;
        }
        uint32_t const fClearEventOnForward = VCPU_2_VMXSTATE(pVCpu).Event.fPending; /* paranoia. should not inject events below.  */

        RTGCPHYS const GCPhysNestedFault = pVmxTransient->uGuestPhysicalAddr;
        uint64_t const uExitQual         = pVmxTransient->uExitQual;

        RTGCPTR GCPtrNestedFault;
        bool const fIsLinearAddrValid = RT_BOOL(uExitQual & VMX_EXIT_QUAL_EPT_LINEAR_ADDR_VALID);
        if (fIsLinearAddrValid)
        {
            vmxHCReadToTransient<HMVMX_READ_GUEST_LINEAR_ADDR>(pVCpu, pVmxTransient);
            GCPtrNestedFault = pVmxTransient->uGuestLinearAddr;
        }
        else
            GCPtrNestedFault = 0;

        RTGCUINT const uErr = ((uExitQual & VMX_EXIT_QUAL_EPT_ACCESS_INSTR_FETCH) ? X86_TRAP_PF_ID : 0)
                            | ((uExitQual & VMX_EXIT_QUAL_EPT_ACCESS_WRITE)       ? X86_TRAP_PF_RW : 0)
                            | ((uExitQual & (  VMX_EXIT_QUAL_EPT_ENTRY_READ
                                             | VMX_EXIT_QUAL_EPT_ENTRY_WRITE
                                             | VMX_EXIT_QUAL_EPT_ENTRY_EXECUTE))  ? X86_TRAP_PF_P  : 0);

        PGMPTWALK Walk;
        PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
        rcStrict = PGMR0NestedTrap0eHandlerNestedPaging(pVCpu, PGMMODE_EPT, uErr, pCtx, GCPhysNestedFault,
                                                        fIsLinearAddrValid, GCPtrNestedFault, &Walk);
        Log7Func(("PGM (uExitQual=%#RX64, %RGp, %RGv) -> %Rrc (fFailed=%d)\n",
                  uExitQual, GCPhysNestedFault, GCPtrNestedFault, VBOXSTRICTRC_VAL(rcStrict), Walk.fFailed));
        if (RT_SUCCESS(rcStrict))
        {
            if (rcStrict == VINF_IOM_R3_MMIO_COMMIT_WRITE)
            {
                Assert(!fClearEventOnForward);
                Assert(VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IOM));
                rcStrict = VINF_EM_RESCHEDULE_REM;
            }
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_ALL_GUEST);
            return rcStrict;
        }

        if (fClearEventOnForward)
            VCPU_2_VMXSTATE(pVCpu).Event.fPending = false;

        VMXVEXITEVENTINFO const ExitEventInfo = VMXVEXITEVENTINFO_INIT_ONLY_IDT(pVmxTransient->uIdtVectoringInfo,
                                                                                pVmxTransient->uIdtVectoringErrorCode);
        if (Walk.fFailed & PGM_WALKFAIL_EPT_VIOLATION)
        {
            VMXVEXITINFO const ExitInfo
                = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN_AND_GST_ADDRESSES(VMX_EXIT_EPT_VIOLATION,
                                                                              pVmxTransient->uExitQual,
                                                                              pVmxTransient->cbExitInstr,
                                                                              pVmxTransient->uGuestLinearAddr,
                                                                              pVmxTransient->uGuestPhysicalAddr);
            return IEMExecVmxVmexitEptViolation(pVCpu, &ExitInfo, &ExitEventInfo);
        }

        AssertMsgReturn(Walk.fFailed & PGM_WALKFAIL_EPT_MISCONFIG,
                        ("uErr=%#RX32 uExitQual=%#RX64 GCPhysNestedFault=%#RGp GCPtrNestedFault=%#RGv\n",
                         (uint32_t)uErr, uExitQual, GCPhysNestedFault, GCPtrNestedFault),
                        rcStrict);
        return IEMExecVmxVmexitEptMisconfig(pVCpu, pVmxTransient->uGuestPhysicalAddr, &ExitEventInfo);
    }

    return vmxHCExitEptViolation(pVCpu, pVmxTransient);
}


/**
 * Nested-guest VM-exit handler for EPT misconfiguration (VMX_EXIT_EPT_MISCONFIG).
 * Conditional VM-exit.
 */
HMVMX_EXIT_DECL vmxHCExitEptMisconfigNested(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient)
{
    HMVMX_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pVmxTransient);
    Assert(pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);

    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    if (CPUMIsGuestVmxProcCtls2Set(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS2_EPT))
    {
        vmxHCReadToTransient<HMVMX_READ_GUEST_PHYSICAL_ADDR>(pVCpu, pVmxTransient);
        int rc = vmxHCImportGuestState<CPUMCTX_EXTRN_ALL>(pVCpu, pVmcsInfo, __FUNCTION__);
        AssertRCReturn(rc, rc);

        PGMPTWALK Walk;
        PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
        RTGCPHYS const GCPhysNestedFault = pVmxTransient->uGuestPhysicalAddr;
        VBOXSTRICTRC rcStrict = PGMR0NestedTrap0eHandlerNestedPaging(pVCpu, PGMMODE_EPT, X86_TRAP_PF_RSVD, pCtx,
                                                                     GCPhysNestedFault, false /* fIsLinearAddrValid */,
                                                                     0 /* GCPtrNestedFault */, &Walk);
        if (RT_SUCCESS(rcStrict))
        {
            AssertMsgFailed(("Shouldn't happen with the way we have programmed the EPT shadow tables\n"));
            return rcStrict;
        }

        AssertMsg(Walk.fFailed & PGM_WALKFAIL_EPT_MISCONFIG, ("GCPhysNestedFault=%#RGp\n", GCPhysNestedFault));
        vmxHCReadToTransient<  HMVMX_READ_IDT_VECTORING_INFO
                             | HMVMX_READ_IDT_VECTORING_ERROR_CODE>(pVCpu, pVmxTransient);

        VMXVEXITEVENTINFO const ExitEventInfo = VMXVEXITEVENTINFO_INIT_ONLY_IDT(pVmxTransient->uIdtVectoringInfo,
                                                                                pVmxTransient->uIdtVectoringErrorCode);
        return IEMExecVmxVmexitEptMisconfig(pVCpu, pVmxTransient->uGuestPhysicalAddr, &ExitEventInfo);
    }

    return vmxHCExitEptMisconfig(pVCpu, pVmxTransient);
}

# endif /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */

/** @} */
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */


/** @name Execution loop for single stepping, DBGF events and expensive Dtrace
 *  probes.
 *
 * The following few functions and associated structure contains the bloat
 * necessary for providing detailed debug events and dtrace probes as well as
 * reliable host side single stepping.  This works on the principle of
 * "subclassing" the normal execution loop and workers.  We replace the loop
 * method completely and override selected helpers to add necessary adjustments
 * to their core operation.
 *
 * The goal is to keep the "parent" code lean and mean, so as not to sacrifice
 * any performance for debug and analysis features.
 *
 * @{
 */

/**
 * Transient per-VCPU debug state of VMCS and related info. we save/restore in
 * the debug run loop.
 */
typedef struct VMXRUNDBGSTATE
{
    /** The RIP we started executing at.  This is for detecting that we stepped.  */
    uint64_t    uRipStart;
    /** The CS we started executing with.  */
    uint16_t    uCsStart;

    /** Whether we've actually modified the 1st execution control field. */
    bool        fModifiedProcCtls : 1;
    /** Whether we've actually modified the 2nd execution control field. */
    bool        fModifiedProcCtls2 : 1;
    /** Whether we've actually modified the exception bitmap. */
    bool        fModifiedXcptBitmap : 1;

    /** We desire the modified the CR0 mask to be cleared. */
    bool        fClearCr0Mask : 1;
    /** We desire the modified the CR4 mask to be cleared. */
    bool        fClearCr4Mask : 1;
    /** Stuff we need in VMX_VMCS32_CTRL_PROC_EXEC. */
    uint32_t    fCpe1Extra;
    /** Stuff we do not want in VMX_VMCS32_CTRL_PROC_EXEC. */
    uint32_t    fCpe1Unwanted;
    /** Stuff we need in VMX_VMCS32_CTRL_PROC_EXEC2. */
    uint32_t    fCpe2Extra;
    /** Extra stuff we need in VMX_VMCS32_CTRL_EXCEPTION_BITMAP. */
    uint32_t    bmXcptExtra;
    /** The sequence number of the Dtrace provider settings the state was
     *  configured against. */
    uint32_t    uDtraceSettingsSeqNo;
    /** VM-exits to check (one bit per VM-exit). */
    uint32_t    bmExitsToCheck[3];

    /** The initial VMX_VMCS32_CTRL_PROC_EXEC value (helps with restore). */
    uint32_t    fProcCtlsInitial;
    /** The initial VMX_VMCS32_CTRL_PROC_EXEC2 value (helps with restore). */
    uint32_t    fProcCtls2Initial;
    /** The initial VMX_VMCS32_CTRL_EXCEPTION_BITMAP value (helps with restore). */
    uint32_t    bmXcptInitial;
} VMXRUNDBGSTATE;
AssertCompileMemberSize(VMXRUNDBGSTATE, bmExitsToCheck, (VMX_EXIT_MAX + 1 + 31) / 32 * 4);
typedef VMXRUNDBGSTATE *PVMXRUNDBGSTATE;


/**
 * Initializes the VMXRUNDBGSTATE structure.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   pVmxTransient   The VMX-transient structure.
 * @param   pDbgState       The debug state to initialize.
 */
static void vmxHCRunDebugStateInit(PVMCPUCC pVCpu, PCVMXTRANSIENT pVmxTransient, PVMXRUNDBGSTATE pDbgState)
{
    pDbgState->uRipStart            = pVCpu->cpum.GstCtx.rip;
    pDbgState->uCsStart             = pVCpu->cpum.GstCtx.cs.Sel;

    pDbgState->fModifiedProcCtls    = false;
    pDbgState->fModifiedProcCtls2   = false;
    pDbgState->fModifiedXcptBitmap  = false;
    pDbgState->fClearCr0Mask        = false;
    pDbgState->fClearCr4Mask        = false;
    pDbgState->fCpe1Extra           = 0;
    pDbgState->fCpe1Unwanted        = 0;
    pDbgState->fCpe2Extra           = 0;
    pDbgState->bmXcptExtra          = 0;
    pDbgState->fProcCtlsInitial     = pVmxTransient->pVmcsInfo->u32ProcCtls;
    pDbgState->fProcCtls2Initial    = pVmxTransient->pVmcsInfo->u32ProcCtls2;
    pDbgState->bmXcptInitial        = pVmxTransient->pVmcsInfo->u32XcptBitmap;
}


/**
 * Updates the VMSC fields with changes requested by @a pDbgState.
 *
 * This is performed after hmR0VmxPreRunGuestDebugStateUpdate as well
 * immediately before executing guest code, i.e. when interrupts are disabled.
 * We don't check status codes here as we cannot easily assert or return in the
 * latter case.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 * @param   pDbgState       The debug state.
 */
static void vmxHCPreRunGuestDebugStateApply(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient, PVMXRUNDBGSTATE pDbgState)
{
    /*
     * Ensure desired flags in VMCS control fields are set.
     * (Ignoring write failure here, as we're committed and it's just debug extras.)
     *
     * Note! We load the shadow CR0 & CR4 bits when we flag the clearing, so
     *       there should be no stale data in pCtx at this point.
     */
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;
    if (   (pVmcsInfo->u32ProcCtls & pDbgState->fCpe1Extra) != pDbgState->fCpe1Extra
        || (pVmcsInfo->u32ProcCtls & pDbgState->fCpe1Unwanted))
    {
        pVmcsInfo->u32ProcCtls |= pDbgState->fCpe1Extra;
        pVmcsInfo->u32ProcCtls &= ~pDbgState->fCpe1Unwanted;
        VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, pVmcsInfo->u32ProcCtls);
        Log6Func(("VMX_VMCS32_CTRL_PROC_EXEC: %#RX32\n", pVmcsInfo->u32ProcCtls));
        pDbgState->fModifiedProcCtls   = true;
    }

    if ((pVmcsInfo->u32ProcCtls2 & pDbgState->fCpe2Extra) != pDbgState->fCpe2Extra)
    {
        pVmcsInfo->u32ProcCtls2  |= pDbgState->fCpe2Extra;
        VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC2, pVmcsInfo->u32ProcCtls2);
        Log6Func(("VMX_VMCS32_CTRL_PROC_EXEC2: %#RX32\n", pVmcsInfo->u32ProcCtls2));
        pDbgState->fModifiedProcCtls2  = true;
    }

    if ((pVmcsInfo->u32XcptBitmap & pDbgState->bmXcptExtra) != pDbgState->bmXcptExtra)
    {
        pVmcsInfo->u32XcptBitmap |= pDbgState->bmXcptExtra;
        VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_EXCEPTION_BITMAP, pVmcsInfo->u32XcptBitmap);
        Log6Func(("VMX_VMCS32_CTRL_EXCEPTION_BITMAP: %#RX32\n", pVmcsInfo->u32XcptBitmap));
        pDbgState->fModifiedXcptBitmap = true;
    }

    if (pDbgState->fClearCr0Mask && pVmcsInfo->u64Cr0Mask != 0)
    {
        pVmcsInfo->u64Cr0Mask = 0;
        VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_CTRL_CR0_MASK, 0);
        Log6Func(("VMX_VMCS_CTRL_CR0_MASK: 0\n"));
    }

    if (pDbgState->fClearCr4Mask && pVmcsInfo->u64Cr4Mask != 0)
    {
        pVmcsInfo->u64Cr4Mask = 0;
        VMX_VMCS_WRITE_NW(pVCpu, VMX_VMCS_CTRL_CR4_MASK, 0);
        Log6Func(("VMX_VMCS_CTRL_CR4_MASK: 0\n"));
    }

    NOREF(pVCpu);
}


/**
 * Restores VMCS fields that were changed by hmR0VmxPreRunGuestDebugStateApply for
 * re-entry next time around.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 * @param   pDbgState       The debug state.
 * @param   rcStrict        The return code from executing the guest using single
 *                          stepping.
 */
static VBOXSTRICTRC vmxHCRunDebugStateRevert(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient, PVMXRUNDBGSTATE pDbgState,
                                             VBOXSTRICTRC rcStrict)
{
    /*
     * Restore VM-exit control settings as we may not reenter this function the
     * next time around.
     */
    PVMXVMCSINFO pVmcsInfo = pVmxTransient->pVmcsInfo;

    /* We reload the initial value, trigger what we can of recalculations the
       next time around.  From the looks of things, that's all that's required atm. */
    if (pDbgState->fModifiedProcCtls)
    {
        if (!(pDbgState->fProcCtlsInitial & VMX_PROC_CTLS_MOV_DR_EXIT) && CPUMIsHyperDebugStateActive(pVCpu))
            pDbgState->fProcCtlsInitial |= VMX_PROC_CTLS_MOV_DR_EXIT; /* Avoid assertion in hmR0VmxLeave */
        int rc2 = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC, pDbgState->fProcCtlsInitial);
        AssertRC(rc2);
        pVmcsInfo->u32ProcCtls = pDbgState->fProcCtlsInitial;
    }

    /* We're currently the only ones messing with this one, so just restore the
       cached value and reload the field. */
    if (   pDbgState->fModifiedProcCtls2
        && pVmcsInfo->u32ProcCtls2 != pDbgState->fProcCtls2Initial)
    {
        int rc2 = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_PROC_EXEC2, pDbgState->fProcCtls2Initial);
        AssertRC(rc2);
        pVmcsInfo->u32ProcCtls2 = pDbgState->fProcCtls2Initial;
    }

    /* If we've modified the exception bitmap, we restore it and trigger
       reloading and partial recalculation the next time around. */
    if (pDbgState->fModifiedXcptBitmap)
    {
        int rc2 = VMX_VMCS_WRITE_32(pVCpu, VMX_VMCS32_CTRL_EXCEPTION_BITMAP, pDbgState->bmXcptInitial);
        AssertRC(rc2);
        pVmcsInfo->u32XcptBitmap = pDbgState->bmXcptInitial;
    }

    return rcStrict;
}


/**
 * Configures VM-exit controls for current DBGF and DTrace settings.
 *
 * This updates @a pDbgState and the VMCS execution control fields to reflect
 * the necessary VM-exits demanded by DBGF and DTrace.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure. May update
 *                          fUpdatedTscOffsettingAndPreemptTimer.
 * @param   pDbgState       The debug state.
 */
static void vmxHCPreRunGuestDebugStateUpdate(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient, PVMXRUNDBGSTATE pDbgState)
{
#ifndef IN_NEM_DARWIN
    /*
     * Take down the dtrace serial number so we can spot changes.
     */
    pDbgState->uDtraceSettingsSeqNo = VBOXVMM_GET_SETTINGS_SEQ_NO();
    ASMCompilerBarrier();
#endif

    /*
     * We'll rebuild most of the middle block of data members (holding the
     * current settings) as we go along here, so start by clearing it all.
     */
    pDbgState->bmXcptExtra      = 0;
    pDbgState->fCpe1Extra       = 0;
    pDbgState->fCpe1Unwanted    = 0;
    pDbgState->fCpe2Extra       = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(pDbgState->bmExitsToCheck); i++)
        pDbgState->bmExitsToCheck[i] = 0;

    /*
     * Software interrupts (INT XXh) - no idea how to trigger these...
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (   DBGF_IS_EVENT_ENABLED(pVM, DBGFEVENT_INTERRUPT_SOFTWARE)
        || VBOXVMM_INT_SOFTWARE_ENABLED())
    {
        ASMBitSet(pDbgState->bmExitsToCheck, VMX_EXIT_XCPT_OR_NMI);
    }

    /*
     * INT3 breakpoints - triggered by #BP exceptions.
     */
    if (pVM->dbgf.ro.cEnabledInt3Breakpoints > 0)
        pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_BP);

    /*
     * Exception bitmap and XCPT events+probes.
     */
    for (int iXcpt = 0; iXcpt < (DBGFEVENT_XCPT_LAST - DBGFEVENT_XCPT_FIRST + 1); iXcpt++)
        if (DBGF_IS_EVENT_ENABLED(pVM, (DBGFEVENTTYPE)(DBGFEVENT_XCPT_FIRST + iXcpt)))
            pDbgState->bmXcptExtra |= RT_BIT_32(iXcpt);

    if (VBOXVMM_XCPT_DE_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_DE);
    if (VBOXVMM_XCPT_DB_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_DB);
    if (VBOXVMM_XCPT_BP_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_BP);
    if (VBOXVMM_XCPT_OF_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_OF);
    if (VBOXVMM_XCPT_BR_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_BR);
    if (VBOXVMM_XCPT_UD_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_UD);
    if (VBOXVMM_XCPT_NM_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_NM);
    if (VBOXVMM_XCPT_DF_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_DF);
    if (VBOXVMM_XCPT_TS_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_TS);
    if (VBOXVMM_XCPT_NP_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_NP);
    if (VBOXVMM_XCPT_SS_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_SS);
    if (VBOXVMM_XCPT_GP_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_GP);
    if (VBOXVMM_XCPT_PF_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_PF);
    if (VBOXVMM_XCPT_MF_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_MF);
    if (VBOXVMM_XCPT_AC_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_AC);
    if (VBOXVMM_XCPT_XF_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_XF);
    if (VBOXVMM_XCPT_VE_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_VE);
    if (VBOXVMM_XCPT_SX_ENABLED())  pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_SX);

    if (pDbgState->bmXcptExtra)
        ASMBitSet(pDbgState->bmExitsToCheck, VMX_EXIT_XCPT_OR_NMI);

    /*
     * Process events and probes for VM-exits, making sure we get the wanted VM-exits.
     *
     * Note! This is the reverse of what hmR0VmxHandleExitDtraceEvents does.
     *       So, when adding/changing/removing please don't forget to update it.
     *
     * Some of the macros are picking up local variables to save horizontal space,
     * (being able to see it in a table is the lesser evil here).
     */
#define IS_EITHER_ENABLED(a_pVM, a_EventSubName) \
        (    DBGF_IS_EVENT_ENABLED(a_pVM, RT_CONCAT(DBGFEVENT_, a_EventSubName)) \
         ||  RT_CONCAT3(VBOXVMM_, a_EventSubName, _ENABLED)() )
#define SET_ONLY_XBM_IF_EITHER_EN(a_EventSubName, a_uExit) \
        if (IS_EITHER_ENABLED(pVM, a_EventSubName)) \
        {   AssertCompile((unsigned)(a_uExit) < sizeof(pDbgState->bmExitsToCheck) * 8); \
            ASMBitSet((pDbgState)->bmExitsToCheck, a_uExit); \
        } else do { } while (0)
#define SET_CPE1_XBM_IF_EITHER_EN(a_EventSubName, a_uExit, a_fCtrlProcExec) \
        if (IS_EITHER_ENABLED(pVM, a_EventSubName)) \
        { \
            (pDbgState)->fCpe1Extra |= (a_fCtrlProcExec); \
            AssertCompile((unsigned)(a_uExit) < sizeof(pDbgState->bmExitsToCheck) * 8); \
            ASMBitSet((pDbgState)->bmExitsToCheck, a_uExit); \
        } else do { } while (0)
#define SET_CPEU_XBM_IF_EITHER_EN(a_EventSubName, a_uExit, a_fUnwantedCtrlProcExec) \
        if (IS_EITHER_ENABLED(pVM, a_EventSubName)) \
        { \
            (pDbgState)->fCpe1Unwanted |= (a_fUnwantedCtrlProcExec); \
            AssertCompile((unsigned)(a_uExit) < sizeof(pDbgState->bmExitsToCheck) * 8); \
            ASMBitSet((pDbgState)->bmExitsToCheck, a_uExit); \
        } else do { } while (0)
#define SET_CPE2_XBM_IF_EITHER_EN(a_EventSubName, a_uExit, a_fCtrlProcExec2) \
        if (IS_EITHER_ENABLED(pVM, a_EventSubName)) \
        { \
            (pDbgState)->fCpe2Extra |= (a_fCtrlProcExec2); \
            AssertCompile((unsigned)(a_uExit) < sizeof(pDbgState->bmExitsToCheck) * 8); \
            ASMBitSet((pDbgState)->bmExitsToCheck, a_uExit); \
        } else do { } while (0)

    SET_ONLY_XBM_IF_EITHER_EN(EXIT_TASK_SWITCH,         VMX_EXIT_TASK_SWITCH);   /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN(EXIT_VMX_EPT_VIOLATION,   VMX_EXIT_EPT_VIOLATION); /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN(EXIT_VMX_EPT_MISCONFIG,   VMX_EXIT_EPT_MISCONFIG); /* unconditional (unless #VE) */
    SET_ONLY_XBM_IF_EITHER_EN(EXIT_VMX_VAPIC_ACCESS,    VMX_EXIT_APIC_ACCESS);   /* feature dependent, nothing to enable here */
    SET_ONLY_XBM_IF_EITHER_EN(EXIT_VMX_VAPIC_WRITE,     VMX_EXIT_APIC_WRITE);    /* feature dependent, nothing to enable here */

    SET_ONLY_XBM_IF_EITHER_EN(INSTR_CPUID,              VMX_EXIT_CPUID);         /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_CPUID,              VMX_EXIT_CPUID);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_GETSEC,             VMX_EXIT_GETSEC);        /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_GETSEC,             VMX_EXIT_GETSEC);
    SET_CPE1_XBM_IF_EITHER_EN(INSTR_HALT,               VMX_EXIT_HLT,      VMX_PROC_CTLS_HLT_EXIT); /* paranoia */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_HALT,               VMX_EXIT_HLT);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_INVD,               VMX_EXIT_INVD);          /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_INVD,               VMX_EXIT_INVD);
    SET_CPE1_XBM_IF_EITHER_EN(INSTR_INVLPG,             VMX_EXIT_INVLPG,   VMX_PROC_CTLS_INVLPG_EXIT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_INVLPG,             VMX_EXIT_INVLPG);
    SET_CPE1_XBM_IF_EITHER_EN(INSTR_RDPMC,              VMX_EXIT_RDPMC,    VMX_PROC_CTLS_RDPMC_EXIT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDPMC,              VMX_EXIT_RDPMC);
    SET_CPE1_XBM_IF_EITHER_EN(INSTR_RDTSC,              VMX_EXIT_RDTSC,    VMX_PROC_CTLS_RDTSC_EXIT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDTSC,              VMX_EXIT_RDTSC);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_RSM,                VMX_EXIT_RSM);           /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RSM,                VMX_EXIT_RSM);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMM_CALL,           VMX_EXIT_VMCALL);        /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMM_CALL,           VMX_EXIT_VMCALL);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMCLEAR,        VMX_EXIT_VMCLEAR);       /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMCLEAR,        VMX_EXIT_VMCLEAR);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMLAUNCH,       VMX_EXIT_VMLAUNCH);      /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMLAUNCH,       VMX_EXIT_VMLAUNCH);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMPTRLD,        VMX_EXIT_VMPTRLD);       /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMPTRLD,        VMX_EXIT_VMPTRLD);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMPTRST,        VMX_EXIT_VMPTRST);       /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMPTRST,        VMX_EXIT_VMPTRST);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMREAD,         VMX_EXIT_VMREAD);        /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMREAD,         VMX_EXIT_VMREAD);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMRESUME,       VMX_EXIT_VMRESUME);      /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMRESUME,       VMX_EXIT_VMRESUME);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMWRITE,        VMX_EXIT_VMWRITE);       /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMWRITE,        VMX_EXIT_VMWRITE);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMXOFF,         VMX_EXIT_VMXOFF);        /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMXOFF,         VMX_EXIT_VMXOFF);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMXON,          VMX_EXIT_VMXON);         /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMXON,          VMX_EXIT_VMXON);

    if (   IS_EITHER_ENABLED(pVM, INSTR_CRX_READ)
        || IS_EITHER_ENABLED(pVM, INSTR_CRX_WRITE))
    {
        int rc = vmxHCImportGuestStateEx(pVCpu, pVmxTransient->pVmcsInfo,
                                         CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_APIC_TPR);
        AssertRC(rc);

#if 0 /** @todo fix me */
        pDbgState->fClearCr0Mask = true;
        pDbgState->fClearCr4Mask = true;
#endif
        if (IS_EITHER_ENABLED(pVM, INSTR_CRX_READ))
            pDbgState->fCpe1Extra |= VMX_PROC_CTLS_CR3_STORE_EXIT | VMX_PROC_CTLS_CR8_STORE_EXIT;
        if (IS_EITHER_ENABLED(pVM, INSTR_CRX_WRITE))
            pDbgState->fCpe1Extra |= VMX_PROC_CTLS_CR3_LOAD_EXIT | VMX_PROC_CTLS_CR8_LOAD_EXIT;
        pDbgState->fCpe1Unwanted |= VMX_PROC_CTLS_USE_TPR_SHADOW; /* risky? */
        /* Note! We currently don't use VMX_VMCS32_CTRL_CR3_TARGET_COUNT.  It would
                 require clearing here and in the loop if we start using it. */
        ASMBitSet(pDbgState->bmExitsToCheck, VMX_EXIT_MOV_CRX);
    }
    else
    {
        if (pDbgState->fClearCr0Mask)
        {
            pDbgState->fClearCr0Mask = false;
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_CR0);
        }
        if (pDbgState->fClearCr4Mask)
        {
            pDbgState->fClearCr4Mask = false;
            ASMAtomicUoOrU64(&VCPU_2_VMXSTATE(pVCpu).fCtxChanged, HM_CHANGED_GUEST_CR4);
        }
    }
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_CRX_READ,           VMX_EXIT_MOV_CRX);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_CRX_WRITE,          VMX_EXIT_MOV_CRX);

    if (   IS_EITHER_ENABLED(pVM, INSTR_DRX_READ)
        || IS_EITHER_ENABLED(pVM, INSTR_DRX_WRITE))
    {
        /** @todo later, need to fix handler as it assumes this won't usually happen. */
        ASMBitSet(pDbgState->bmExitsToCheck, VMX_EXIT_MOV_DRX);
    }
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_DRX_READ,           VMX_EXIT_MOV_DRX);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_DRX_WRITE,          VMX_EXIT_MOV_DRX);

    SET_CPEU_XBM_IF_EITHER_EN(INSTR_RDMSR,              VMX_EXIT_RDMSR,    VMX_PROC_CTLS_USE_MSR_BITMAPS); /* risky clearing this? */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDMSR,              VMX_EXIT_RDMSR);
    SET_CPEU_XBM_IF_EITHER_EN(INSTR_WRMSR,              VMX_EXIT_WRMSR,    VMX_PROC_CTLS_USE_MSR_BITMAPS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_WRMSR,              VMX_EXIT_WRMSR);
    SET_CPE1_XBM_IF_EITHER_EN(INSTR_MWAIT,              VMX_EXIT_MWAIT,    VMX_PROC_CTLS_MWAIT_EXIT);   /* paranoia */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_MWAIT,              VMX_EXIT_MWAIT);
    SET_CPE1_XBM_IF_EITHER_EN(INSTR_MONITOR,            VMX_EXIT_MONITOR,  VMX_PROC_CTLS_MONITOR_EXIT); /* paranoia */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_MONITOR,            VMX_EXIT_MONITOR);
#if 0 /** @todo too slow, fix handler. */
    SET_CPE1_XBM_IF_EITHER_EN(INSTR_PAUSE,              VMX_EXIT_PAUSE,    VMX_PROC_CTLS_PAUSE_EXIT);
#endif
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_PAUSE,              VMX_EXIT_PAUSE);

    if (   IS_EITHER_ENABLED(pVM, INSTR_SGDT)
        || IS_EITHER_ENABLED(pVM, INSTR_SIDT)
        || IS_EITHER_ENABLED(pVM, INSTR_LGDT)
        || IS_EITHER_ENABLED(pVM, INSTR_LIDT))
    {
        pDbgState->fCpe2Extra |= VMX_PROC_CTLS2_DESC_TABLE_EXIT;
        ASMBitSet(pDbgState->bmExitsToCheck, VMX_EXIT_GDTR_IDTR_ACCESS);
    }
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SGDT,               VMX_EXIT_GDTR_IDTR_ACCESS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SIDT,               VMX_EXIT_GDTR_IDTR_ACCESS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_LGDT,               VMX_EXIT_GDTR_IDTR_ACCESS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_LIDT,               VMX_EXIT_GDTR_IDTR_ACCESS);

    if (   IS_EITHER_ENABLED(pVM, INSTR_SLDT)
        || IS_EITHER_ENABLED(pVM, INSTR_STR)
        || IS_EITHER_ENABLED(pVM, INSTR_LLDT)
        || IS_EITHER_ENABLED(pVM, INSTR_LTR))
    {
        pDbgState->fCpe2Extra |= VMX_PROC_CTLS2_DESC_TABLE_EXIT;
        ASMBitSet(pDbgState->bmExitsToCheck, VMX_EXIT_LDTR_TR_ACCESS);
    }
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SLDT,               VMX_EXIT_LDTR_TR_ACCESS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_STR,                VMX_EXIT_LDTR_TR_ACCESS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_LLDT,               VMX_EXIT_LDTR_TR_ACCESS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_LTR,                VMX_EXIT_LDTR_TR_ACCESS);

    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_INVEPT,         VMX_EXIT_INVEPT);        /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_INVEPT,         VMX_EXIT_INVEPT);
    SET_CPE1_XBM_IF_EITHER_EN(INSTR_RDTSCP,             VMX_EXIT_RDTSCP,   VMX_PROC_CTLS_RDTSC_EXIT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDTSCP,             VMX_EXIT_RDTSCP);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_INVVPID,        VMX_EXIT_INVVPID);       /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_INVVPID,        VMX_EXIT_INVVPID);
    SET_CPE2_XBM_IF_EITHER_EN(INSTR_WBINVD,             VMX_EXIT_WBINVD,   VMX_PROC_CTLS2_WBINVD_EXIT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_WBINVD,             VMX_EXIT_WBINVD);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_XSETBV,             VMX_EXIT_XSETBV);        /* unconditional */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_XSETBV,             VMX_EXIT_XSETBV);
    SET_CPE2_XBM_IF_EITHER_EN(INSTR_RDRAND,             VMX_EXIT_RDRAND,   VMX_PROC_CTLS2_RDRAND_EXIT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDRAND,             VMX_EXIT_RDRAND);
    SET_CPE1_XBM_IF_EITHER_EN(INSTR_VMX_INVPCID,        VMX_EXIT_INVPCID,  VMX_PROC_CTLS_INVLPG_EXIT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_INVPCID,        VMX_EXIT_INVPCID);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_VMX_VMFUNC,         VMX_EXIT_VMFUNC);        /* unconditional for the current setup */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMX_VMFUNC,         VMX_EXIT_VMFUNC);
    SET_CPE2_XBM_IF_EITHER_EN(INSTR_RDSEED,             VMX_EXIT_RDSEED,   VMX_PROC_CTLS2_RDSEED_EXIT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDSEED,             VMX_EXIT_RDSEED);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_XSAVES,             VMX_EXIT_XSAVES);        /* unconditional (enabled by host, guest cfg) */
    SET_ONLY_XBM_IF_EITHER_EN(EXIT_XSAVES,              VMX_EXIT_XSAVES);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_XRSTORS,            VMX_EXIT_XRSTORS);       /* unconditional (enabled by host, guest cfg) */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_XRSTORS,            VMX_EXIT_XRSTORS);

#undef IS_EITHER_ENABLED
#undef SET_ONLY_XBM_IF_EITHER_EN
#undef SET_CPE1_XBM_IF_EITHER_EN
#undef SET_CPEU_XBM_IF_EITHER_EN
#undef SET_CPE2_XBM_IF_EITHER_EN

    /*
     * Sanitize the control stuff.
     */
    pDbgState->fCpe2Extra       &= g_HmMsrs.u.vmx.ProcCtls2.n.allowed1;
    if (pDbgState->fCpe2Extra)
        pDbgState->fCpe1Extra   |= VMX_PROC_CTLS_USE_SECONDARY_CTLS;
    pDbgState->fCpe1Extra       &= g_HmMsrs.u.vmx.ProcCtls.n.allowed1;
    pDbgState->fCpe1Unwanted    &= ~g_HmMsrs.u.vmx.ProcCtls.n.allowed0;
#ifndef IN_NEM_DARWIN
    if (pVCpu->hmr0.s.fDebugWantRdTscExit != RT_BOOL(pDbgState->fCpe1Extra & VMX_PROC_CTLS_RDTSC_EXIT))
    {
        pVCpu->hmr0.s.fDebugWantRdTscExit ^= true;
        pVmxTransient->fUpdatedTscOffsettingAndPreemptTimer = false;
    }
#else
    if (pVCpu->nem.s.fDebugWantRdTscExit != RT_BOOL(pDbgState->fCpe1Extra & VMX_PROC_CTLS_RDTSC_EXIT))
    {
        pVCpu->nem.s.fDebugWantRdTscExit ^= true;
        pVmxTransient->fUpdatedTscOffsettingAndPreemptTimer = false;
    }
#endif

    Log6(("HM: debug state: cpe1=%#RX32 cpeu=%#RX32 cpe2=%#RX32%s%s\n",
          pDbgState->fCpe1Extra, pDbgState->fCpe1Unwanted, pDbgState->fCpe2Extra,
          pDbgState->fClearCr0Mask ? " clr-cr0" : "",
          pDbgState->fClearCr4Mask ? " clr-cr4" : ""));
}


/**
 * Fires off DBGF events and dtrace probes for a VM-exit, when it's
 * appropriate.
 *
 * The caller has checked the VM-exit against the
 * VMXRUNDBGSTATE::bmExitsToCheck bitmap. The caller has checked for NMIs
 * already, so we don't have to do that either.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmxTransient   The VMX-transient structure.
 * @param   uExitReason     The VM-exit reason.
 *
 * @remarks The name of this function is displayed by dtrace, so keep it short
 *          and to the point. No longer than 33 chars long, please.
 */
static VBOXSTRICTRC vmxHCHandleExitDtraceEvents(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient, uint32_t uExitReason)
{
    /*
     * Translate the event into a DBGF event (enmEvent + uEventArg) and at the
     * same time check whether any corresponding Dtrace event is enabled (fDtrace).
     *
     * Note! This is the reverse operation of what hmR0VmxPreRunGuestDebugStateUpdate
     *       does.  Must add/change/remove both places.  Same ordering, please.
     *
     *       Added/removed events must also be reflected in the next section
     *       where we dispatch dtrace events.
     */
    bool            fDtrace1   = false;
    bool            fDtrace2   = false;
    DBGFEVENTTYPE   enmEvent1  = DBGFEVENT_END;
    DBGFEVENTTYPE   enmEvent2  = DBGFEVENT_END;
    uint32_t        uEventArg  = 0;
#define SET_EXIT(a_EventSubName) \
        do { \
            enmEvent2 = RT_CONCAT(DBGFEVENT_EXIT_,  a_EventSubName); \
            fDtrace2  = RT_CONCAT3(VBOXVMM_EXIT_,   a_EventSubName, _ENABLED)(); \
        } while (0)
#define SET_BOTH(a_EventSubName) \
        do { \
            enmEvent1 = RT_CONCAT(DBGFEVENT_INSTR_, a_EventSubName); \
            enmEvent2 = RT_CONCAT(DBGFEVENT_EXIT_,  a_EventSubName); \
            fDtrace1  = RT_CONCAT3(VBOXVMM_INSTR_,  a_EventSubName, _ENABLED)(); \
            fDtrace2  = RT_CONCAT3(VBOXVMM_EXIT_,   a_EventSubName, _ENABLED)(); \
        } while (0)
    switch (uExitReason)
    {
        case VMX_EXIT_MTF:
            return vmxHCExitMtf(pVCpu, pVmxTransient);

        case VMX_EXIT_XCPT_OR_NMI:
        {
            uint8_t const idxVector = VMX_EXIT_INT_INFO_VECTOR(pVmxTransient->uExitIntInfo);
            switch (VMX_EXIT_INT_INFO_TYPE(pVmxTransient->uExitIntInfo))
            {
                case VMX_EXIT_INT_INFO_TYPE_HW_XCPT:
                case VMX_EXIT_INT_INFO_TYPE_SW_XCPT:
                case VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT:
                    if (idxVector <= (unsigned)(DBGFEVENT_XCPT_LAST - DBGFEVENT_XCPT_FIRST))
                    {
                        if (VMX_EXIT_INT_INFO_IS_ERROR_CODE_VALID(pVmxTransient->uExitIntInfo))
                        {
                            vmxHCReadToTransient<HMVMX_READ_EXIT_INTERRUPTION_ERROR_CODE>(pVCpu, pVmxTransient);
                            uEventArg = pVmxTransient->uExitIntErrorCode;
                        }
                        enmEvent1 = (DBGFEVENTTYPE)(DBGFEVENT_XCPT_FIRST + idxVector);
                        switch (enmEvent1)
                        {
                            case DBGFEVENT_XCPT_DE: fDtrace1 = VBOXVMM_XCPT_DE_ENABLED(); break;
                            case DBGFEVENT_XCPT_DB: fDtrace1 = VBOXVMM_XCPT_DB_ENABLED(); break;
                            case DBGFEVENT_XCPT_BP: fDtrace1 = VBOXVMM_XCPT_BP_ENABLED(); break;
                            case DBGFEVENT_XCPT_OF: fDtrace1 = VBOXVMM_XCPT_OF_ENABLED(); break;
                            case DBGFEVENT_XCPT_BR: fDtrace1 = VBOXVMM_XCPT_BR_ENABLED(); break;
                            case DBGFEVENT_XCPT_UD: fDtrace1 = VBOXVMM_XCPT_UD_ENABLED(); break;
                            case DBGFEVENT_XCPT_NM: fDtrace1 = VBOXVMM_XCPT_NM_ENABLED(); break;
                            case DBGFEVENT_XCPT_DF: fDtrace1 = VBOXVMM_XCPT_DF_ENABLED(); break;
                            case DBGFEVENT_XCPT_TS: fDtrace1 = VBOXVMM_XCPT_TS_ENABLED(); break;
                            case DBGFEVENT_XCPT_NP: fDtrace1 = VBOXVMM_XCPT_NP_ENABLED(); break;
                            case DBGFEVENT_XCPT_SS: fDtrace1 = VBOXVMM_XCPT_SS_ENABLED(); break;
                            case DBGFEVENT_XCPT_GP: fDtrace1 = VBOXVMM_XCPT_GP_ENABLED(); break;
                            case DBGFEVENT_XCPT_PF: fDtrace1 = VBOXVMM_XCPT_PF_ENABLED(); break;
                            case DBGFEVENT_XCPT_MF: fDtrace1 = VBOXVMM_XCPT_MF_ENABLED(); break;
                            case DBGFEVENT_XCPT_AC: fDtrace1 = VBOXVMM_XCPT_AC_ENABLED(); break;
                            case DBGFEVENT_XCPT_XF: fDtrace1 = VBOXVMM_XCPT_XF_ENABLED(); break;
                            case DBGFEVENT_XCPT_VE: fDtrace1 = VBOXVMM_XCPT_VE_ENABLED(); break;
                            case DBGFEVENT_XCPT_SX: fDtrace1 = VBOXVMM_XCPT_SX_ENABLED(); break;
                            default:                                                      break;
                        }
                    }
                    else
                        AssertFailed();
                    break;

                case VMX_EXIT_INT_INFO_TYPE_SW_INT:
                    uEventArg = idxVector;
                    enmEvent1 = DBGFEVENT_INTERRUPT_SOFTWARE;
                    fDtrace1  = VBOXVMM_INT_SOFTWARE_ENABLED();
                    break;
            }
            break;
        }

        case VMX_EXIT_TRIPLE_FAULT:
            enmEvent1 = DBGFEVENT_TRIPLE_FAULT;
            //fDtrace1  = VBOXVMM_EXIT_TRIPLE_FAULT_ENABLED();
            break;
        case VMX_EXIT_TASK_SWITCH:      SET_EXIT(TASK_SWITCH); break;
        case VMX_EXIT_EPT_VIOLATION:    SET_EXIT(VMX_EPT_VIOLATION); break;
        case VMX_EXIT_EPT_MISCONFIG:    SET_EXIT(VMX_EPT_MISCONFIG); break;
        case VMX_EXIT_APIC_ACCESS:      SET_EXIT(VMX_VAPIC_ACCESS); break;
        case VMX_EXIT_APIC_WRITE:       SET_EXIT(VMX_VAPIC_WRITE); break;

        /* Instruction specific VM-exits: */
        case VMX_EXIT_CPUID:            SET_BOTH(CPUID); break;
        case VMX_EXIT_GETSEC:           SET_BOTH(GETSEC); break;
        case VMX_EXIT_HLT:              SET_BOTH(HALT); break;
        case VMX_EXIT_INVD:             SET_BOTH(INVD); break;
        case VMX_EXIT_INVLPG:           SET_BOTH(INVLPG); break;
        case VMX_EXIT_RDPMC:            SET_BOTH(RDPMC); break;
        case VMX_EXIT_RDTSC:            SET_BOTH(RDTSC); break;
        case VMX_EXIT_RSM:              SET_BOTH(RSM); break;
        case VMX_EXIT_VMCALL:           SET_BOTH(VMM_CALL); break;
        case VMX_EXIT_VMCLEAR:          SET_BOTH(VMX_VMCLEAR); break;
        case VMX_EXIT_VMLAUNCH:         SET_BOTH(VMX_VMLAUNCH); break;
        case VMX_EXIT_VMPTRLD:          SET_BOTH(VMX_VMPTRLD); break;
        case VMX_EXIT_VMPTRST:          SET_BOTH(VMX_VMPTRST); break;
        case VMX_EXIT_VMREAD:           SET_BOTH(VMX_VMREAD); break;
        case VMX_EXIT_VMRESUME:         SET_BOTH(VMX_VMRESUME); break;
        case VMX_EXIT_VMWRITE:          SET_BOTH(VMX_VMWRITE); break;
        case VMX_EXIT_VMXOFF:           SET_BOTH(VMX_VMXOFF); break;
        case VMX_EXIT_VMXON:            SET_BOTH(VMX_VMXON); break;
        case VMX_EXIT_MOV_CRX:
            vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);
            if (VMX_EXIT_QUAL_CRX_ACCESS(pVmxTransient->uExitQual) == VMX_EXIT_QUAL_CRX_ACCESS_READ)
                SET_BOTH(CRX_READ);
            else
                SET_BOTH(CRX_WRITE);
            uEventArg = VMX_EXIT_QUAL_CRX_REGISTER(pVmxTransient->uExitQual);
            break;
        case VMX_EXIT_MOV_DRX:
            vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);
            if (   VMX_EXIT_QUAL_DRX_DIRECTION(pVmxTransient->uExitQual)
                == VMX_EXIT_QUAL_DRX_DIRECTION_READ)
                SET_BOTH(DRX_READ);
            else
                SET_BOTH(DRX_WRITE);
            uEventArg = VMX_EXIT_QUAL_DRX_REGISTER(pVmxTransient->uExitQual);
            break;
        case VMX_EXIT_RDMSR:            SET_BOTH(RDMSR); break;
        case VMX_EXIT_WRMSR:            SET_BOTH(WRMSR); break;
        case VMX_EXIT_MWAIT:            SET_BOTH(MWAIT); break;
        case VMX_EXIT_MONITOR:          SET_BOTH(MONITOR); break;
        case VMX_EXIT_PAUSE:            SET_BOTH(PAUSE); break;
        case VMX_EXIT_GDTR_IDTR_ACCESS:
            vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_INFO>(pVCpu, pVmxTransient);
            switch (RT_BF_GET(pVmxTransient->ExitInstrInfo.u, VMX_BF_XDTR_INSINFO_INSTR_ID))
            {
                case VMX_XDTR_INSINFO_II_SGDT: SET_BOTH(SGDT); break;
                case VMX_XDTR_INSINFO_II_SIDT: SET_BOTH(SIDT); break;
                case VMX_XDTR_INSINFO_II_LGDT: SET_BOTH(LGDT); break;
                case VMX_XDTR_INSINFO_II_LIDT: SET_BOTH(LIDT); break;
            }
            break;

        case VMX_EXIT_LDTR_TR_ACCESS:
            vmxHCReadToTransient<HMVMX_READ_EXIT_INSTR_INFO>(pVCpu, pVmxTransient);
            switch (RT_BF_GET(pVmxTransient->ExitInstrInfo.u, VMX_BF_YYTR_INSINFO_INSTR_ID))
            {
                case VMX_YYTR_INSINFO_II_SLDT: SET_BOTH(SLDT); break;
                case VMX_YYTR_INSINFO_II_STR:  SET_BOTH(STR); break;
                case VMX_YYTR_INSINFO_II_LLDT: SET_BOTH(LLDT); break;
                case VMX_YYTR_INSINFO_II_LTR:  SET_BOTH(LTR); break;
            }
            break;

        case VMX_EXIT_INVEPT:           SET_BOTH(VMX_INVEPT); break;
        case VMX_EXIT_RDTSCP:           SET_BOTH(RDTSCP); break;
        case VMX_EXIT_INVVPID:          SET_BOTH(VMX_INVVPID); break;
        case VMX_EXIT_WBINVD:           SET_BOTH(WBINVD); break;
        case VMX_EXIT_XSETBV:           SET_BOTH(XSETBV); break;
        case VMX_EXIT_RDRAND:           SET_BOTH(RDRAND); break;
        case VMX_EXIT_INVPCID:          SET_BOTH(VMX_INVPCID); break;
        case VMX_EXIT_VMFUNC:           SET_BOTH(VMX_VMFUNC); break;
        case VMX_EXIT_RDSEED:           SET_BOTH(RDSEED); break;
        case VMX_EXIT_XSAVES:           SET_BOTH(XSAVES); break;
        case VMX_EXIT_XRSTORS:          SET_BOTH(XRSTORS); break;

        /* Events that aren't relevant at this point. */
        case VMX_EXIT_EXT_INT:
        case VMX_EXIT_INT_WINDOW:
        case VMX_EXIT_NMI_WINDOW:
        case VMX_EXIT_TPR_BELOW_THRESHOLD:
        case VMX_EXIT_PREEMPT_TIMER:
        case VMX_EXIT_IO_INSTR:
            break;

        /* Errors and unexpected events. */
        case VMX_EXIT_INIT_SIGNAL:
        case VMX_EXIT_SIPI:
        case VMX_EXIT_IO_SMI:
        case VMX_EXIT_SMI:
        case VMX_EXIT_ERR_INVALID_GUEST_STATE:
        case VMX_EXIT_ERR_MSR_LOAD:
        case VMX_EXIT_ERR_MACHINE_CHECK:
        case VMX_EXIT_PML_FULL:
        case VMX_EXIT_VIRTUALIZED_EOI:
            break;

        default:
            AssertMsgFailed(("Unexpected VM-exit=%#x\n", uExitReason));
            break;
    }
#undef SET_BOTH
#undef SET_EXIT

    /*
     * Dtrace tracepoints go first.   We do them here at once so we don't
     * have to copy the guest state saving and stuff a few dozen times.
     * Down side is that we've got to repeat the switch, though this time
     * we use enmEvent since the probes are a subset of what DBGF does.
     */
    if (fDtrace1 || fDtrace2)
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);
        vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
        PCPUMCTX pCtx = &pVCpu->cpum.GstCtx; RT_NOREF(pCtx); /* Shut up Clang 13. */
        switch (enmEvent1)
        {
            /** @todo consider which extra parameters would be helpful for each probe.   */
            case DBGFEVENT_END: break;
            case DBGFEVENT_XCPT_DE:                 VBOXVMM_XCPT_DE(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_DB:                 VBOXVMM_XCPT_DB(pVCpu, pCtx, pCtx->dr[6]); break;
            case DBGFEVENT_XCPT_BP:                 VBOXVMM_XCPT_BP(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_OF:                 VBOXVMM_XCPT_OF(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_BR:                 VBOXVMM_XCPT_BR(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_UD:                 VBOXVMM_XCPT_UD(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_NM:                 VBOXVMM_XCPT_NM(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_DF:                 VBOXVMM_XCPT_DF(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_TS:                 VBOXVMM_XCPT_TS(pVCpu, pCtx, uEventArg); break;
            case DBGFEVENT_XCPT_NP:                 VBOXVMM_XCPT_NP(pVCpu, pCtx, uEventArg); break;
            case DBGFEVENT_XCPT_SS:                 VBOXVMM_XCPT_SS(pVCpu, pCtx, uEventArg); break;
            case DBGFEVENT_XCPT_GP:                 VBOXVMM_XCPT_GP(pVCpu, pCtx, uEventArg); break;
            case DBGFEVENT_XCPT_PF:                 VBOXVMM_XCPT_PF(pVCpu, pCtx, uEventArg, pCtx->cr2); break;
            case DBGFEVENT_XCPT_MF:                 VBOXVMM_XCPT_MF(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_AC:                 VBOXVMM_XCPT_AC(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_XF:                 VBOXVMM_XCPT_XF(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_VE:                 VBOXVMM_XCPT_VE(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_SX:                 VBOXVMM_XCPT_SX(pVCpu, pCtx, uEventArg); break;
            case DBGFEVENT_INTERRUPT_SOFTWARE:      VBOXVMM_INT_SOFTWARE(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_INSTR_CPUID:             VBOXVMM_INSTR_CPUID(pVCpu, pCtx, pCtx->eax, pCtx->ecx); break;
            case DBGFEVENT_INSTR_GETSEC:            VBOXVMM_INSTR_GETSEC(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_HALT:              VBOXVMM_INSTR_HALT(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_INVD:              VBOXVMM_INSTR_INVD(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_INVLPG:            VBOXVMM_INSTR_INVLPG(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_RDPMC:             VBOXVMM_INSTR_RDPMC(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_RDTSC:             VBOXVMM_INSTR_RDTSC(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_RSM:               VBOXVMM_INSTR_RSM(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_CRX_READ:          VBOXVMM_INSTR_CRX_READ(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_INSTR_CRX_WRITE:         VBOXVMM_INSTR_CRX_WRITE(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_INSTR_DRX_READ:          VBOXVMM_INSTR_DRX_READ(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_INSTR_DRX_WRITE:         VBOXVMM_INSTR_DRX_WRITE(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_INSTR_RDMSR:             VBOXVMM_INSTR_RDMSR(pVCpu, pCtx, pCtx->ecx); break;
            case DBGFEVENT_INSTR_WRMSR:             VBOXVMM_INSTR_WRMSR(pVCpu, pCtx, pCtx->ecx,
                                                                        RT_MAKE_U64(pCtx->eax, pCtx->edx)); break;
            case DBGFEVENT_INSTR_MWAIT:             VBOXVMM_INSTR_MWAIT(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_MONITOR:           VBOXVMM_INSTR_MONITOR(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_PAUSE:             VBOXVMM_INSTR_PAUSE(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_SGDT:              VBOXVMM_INSTR_SGDT(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_SIDT:              VBOXVMM_INSTR_SIDT(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_LGDT:              VBOXVMM_INSTR_LGDT(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_LIDT:              VBOXVMM_INSTR_LIDT(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_SLDT:              VBOXVMM_INSTR_SLDT(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_STR:               VBOXVMM_INSTR_STR(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_LLDT:              VBOXVMM_INSTR_LLDT(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_LTR:               VBOXVMM_INSTR_LTR(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_RDTSCP:            VBOXVMM_INSTR_RDTSCP(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_WBINVD:            VBOXVMM_INSTR_WBINVD(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_XSETBV:            VBOXVMM_INSTR_XSETBV(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_RDRAND:            VBOXVMM_INSTR_RDRAND(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_RDSEED:            VBOXVMM_INSTR_RDSEED(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_XSAVES:            VBOXVMM_INSTR_XSAVES(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_XRSTORS:           VBOXVMM_INSTR_XRSTORS(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMM_CALL:          VBOXVMM_INSTR_VMM_CALL(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMCLEAR:       VBOXVMM_INSTR_VMX_VMCLEAR(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMLAUNCH:      VBOXVMM_INSTR_VMX_VMLAUNCH(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMPTRLD:       VBOXVMM_INSTR_VMX_VMPTRLD(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMPTRST:       VBOXVMM_INSTR_VMX_VMPTRST(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMREAD:        VBOXVMM_INSTR_VMX_VMREAD(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMRESUME:      VBOXVMM_INSTR_VMX_VMRESUME(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMWRITE:       VBOXVMM_INSTR_VMX_VMWRITE(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMXOFF:        VBOXVMM_INSTR_VMX_VMXOFF(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMXON:         VBOXVMM_INSTR_VMX_VMXON(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_INVEPT:        VBOXVMM_INSTR_VMX_INVEPT(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_INVVPID:       VBOXVMM_INSTR_VMX_INVVPID(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_INVPCID:       VBOXVMM_INSTR_VMX_INVPCID(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_VMX_VMFUNC:        VBOXVMM_INSTR_VMX_VMFUNC(pVCpu, pCtx); break;
            default: AssertMsgFailed(("enmEvent1=%d uExitReason=%d\n", enmEvent1, uExitReason)); break;
        }
        switch (enmEvent2)
        {
            /** @todo consider which extra parameters would be helpful for each probe. */
            case DBGFEVENT_END: break;
            case DBGFEVENT_EXIT_TASK_SWITCH:        VBOXVMM_EXIT_TASK_SWITCH(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_CPUID:              VBOXVMM_EXIT_CPUID(pVCpu, pCtx, pCtx->eax, pCtx->ecx); break;
            case DBGFEVENT_EXIT_GETSEC:             VBOXVMM_EXIT_GETSEC(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_HALT:               VBOXVMM_EXIT_HALT(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_INVD:               VBOXVMM_EXIT_INVD(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_INVLPG:             VBOXVMM_EXIT_INVLPG(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_RDPMC:              VBOXVMM_EXIT_RDPMC(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_RDTSC:              VBOXVMM_EXIT_RDTSC(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_RSM:                VBOXVMM_EXIT_RSM(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_CRX_READ:           VBOXVMM_EXIT_CRX_READ(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_EXIT_CRX_WRITE:          VBOXVMM_EXIT_CRX_WRITE(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_EXIT_DRX_READ:           VBOXVMM_EXIT_DRX_READ(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_EXIT_DRX_WRITE:          VBOXVMM_EXIT_DRX_WRITE(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_EXIT_RDMSR:              VBOXVMM_EXIT_RDMSR(pVCpu, pCtx, pCtx->ecx); break;
            case DBGFEVENT_EXIT_WRMSR:              VBOXVMM_EXIT_WRMSR(pVCpu, pCtx, pCtx->ecx,
                                                                       RT_MAKE_U64(pCtx->eax, pCtx->edx)); break;
            case DBGFEVENT_EXIT_MWAIT:              VBOXVMM_EXIT_MWAIT(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_MONITOR:            VBOXVMM_EXIT_MONITOR(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_PAUSE:              VBOXVMM_EXIT_PAUSE(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_SGDT:               VBOXVMM_EXIT_SGDT(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_SIDT:               VBOXVMM_EXIT_SIDT(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_LGDT:               VBOXVMM_EXIT_LGDT(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_LIDT:               VBOXVMM_EXIT_LIDT(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_SLDT:               VBOXVMM_EXIT_SLDT(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_STR:                VBOXVMM_EXIT_STR(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_LLDT:               VBOXVMM_EXIT_LLDT(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_LTR:                VBOXVMM_EXIT_LTR(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_RDTSCP:             VBOXVMM_EXIT_RDTSCP(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_WBINVD:             VBOXVMM_EXIT_WBINVD(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_XSETBV:             VBOXVMM_EXIT_XSETBV(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_RDRAND:             VBOXVMM_EXIT_RDRAND(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_RDSEED:             VBOXVMM_EXIT_RDSEED(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_XSAVES:             VBOXVMM_EXIT_XSAVES(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_XRSTORS:            VBOXVMM_EXIT_XRSTORS(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMM_CALL:           VBOXVMM_EXIT_VMM_CALL(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMCLEAR:        VBOXVMM_EXIT_VMX_VMCLEAR(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMLAUNCH:       VBOXVMM_EXIT_VMX_VMLAUNCH(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMPTRLD:        VBOXVMM_EXIT_VMX_VMPTRLD(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMPTRST:        VBOXVMM_EXIT_VMX_VMPTRST(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMREAD:         VBOXVMM_EXIT_VMX_VMREAD(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMRESUME:       VBOXVMM_EXIT_VMX_VMRESUME(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMWRITE:        VBOXVMM_EXIT_VMX_VMWRITE(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMXOFF:         VBOXVMM_EXIT_VMX_VMXOFF(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMXON:          VBOXVMM_EXIT_VMX_VMXON(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_INVEPT:         VBOXVMM_EXIT_VMX_INVEPT(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_INVVPID:        VBOXVMM_EXIT_VMX_INVVPID(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_INVPCID:        VBOXVMM_EXIT_VMX_INVPCID(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VMFUNC:         VBOXVMM_EXIT_VMX_VMFUNC(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_EPT_MISCONFIG:  VBOXVMM_EXIT_VMX_EPT_MISCONFIG(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_EPT_VIOLATION:  VBOXVMM_EXIT_VMX_EPT_VIOLATION(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VAPIC_ACCESS:   VBOXVMM_EXIT_VMX_VAPIC_ACCESS(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_VMX_VAPIC_WRITE:    VBOXVMM_EXIT_VMX_VAPIC_WRITE(pVCpu, pCtx); break;
            default: AssertMsgFailed(("enmEvent2=%d uExitReason=%d\n", enmEvent2, uExitReason)); break;
        }
    }

    /*
     * Fire of the DBGF event, if enabled (our check here is just a quick one,
     * the DBGF call will do a full check).
     *
     * Note! DBGF sets DBGFEVENT_INTERRUPT_SOFTWARE in the bitmap.
     * Note! If we have to events, we prioritize the first, i.e. the instruction
     *       one, in order to avoid event nesting.
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (   enmEvent1 != DBGFEVENT_END
        && DBGF_IS_EVENT_ENABLED(pVM, enmEvent1))
    {
        vmxHCImportGuestState<CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
        VBOXSTRICTRC rcStrict = DBGFEventGenericWithArgs(pVM, pVCpu, enmEvent1, DBGFEVENTCTX_HM, 1, uEventArg);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }
    else if (   enmEvent2 != DBGFEVENT_END
             && DBGF_IS_EVENT_ENABLED(pVM, enmEvent2))
    {
        vmxHCImportGuestState<CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
        VBOXSTRICTRC rcStrict = DBGFEventGenericWithArgs(pVM, pVCpu, enmEvent2, DBGFEVENTCTX_HM, 1, uEventArg);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    return VINF_SUCCESS;
}


/**
 * Single-stepping VM-exit filtering.
 *
 * This is preprocessing the VM-exits and deciding whether we've gotten far
 * enough to return VINF_EM_DBG_STEPPED already.  If not, normal VM-exit
 * handling is performed.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pVmxTransient   The VMX-transient structure.
 * @param   pDbgState       The debug state.
 */
DECLINLINE(VBOXSTRICTRC) vmxHCRunDebugHandleExit(PVMCPUCC pVCpu, PVMXTRANSIENT pVmxTransient, PVMXRUNDBGSTATE pDbgState)
{
    /*
     * Expensive (saves context) generic dtrace VM-exit probe.
     */
    uint32_t const uExitReason = pVmxTransient->uExitReason;
    if (!VBOXVMM_R0_HMVMX_VMEXIT_ENABLED())
    { /* more likely */ }
    else
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_QUALIFICATION>(pVCpu, pVmxTransient);
        int rc = vmxHCImportGuestState<HMVMX_CPUMCTX_EXTRN_ALL>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
        AssertRC(rc);
        VBOXVMM_R0_HMVMX_VMEXIT(pVCpu, &pVCpu->cpum.GstCtx, pVmxTransient->uExitReason, pVmxTransient->uExitQual);
    }

#ifndef IN_NEM_DARWIN
    /*
     * Check for host NMI, just to get that out of the way.
     */
    if (uExitReason != VMX_EXIT_XCPT_OR_NMI)
    { /* normally likely */ }
    else
    {
        vmxHCReadToTransient<HMVMX_READ_EXIT_INTERRUPTION_INFO>(pVCpu, pVmxTransient);
        uint32_t const uIntType = VMX_EXIT_INT_INFO_TYPE(pVmxTransient->uExitIntInfo);
        if (uIntType == VMX_EXIT_INT_INFO_TYPE_NMI)
            return hmR0VmxExitHostNmi(pVCpu, pVmxTransient->pVmcsInfo);
    }
#endif

    /*
     * Check for single stepping event if we're stepping.
     */
    if (VCPU_2_VMXSTATE(pVCpu).fSingleInstruction)
    {
        switch (uExitReason)
        {
            case VMX_EXIT_MTF:
                return vmxHCExitMtf(pVCpu, pVmxTransient);

            /* Various events: */
            case VMX_EXIT_XCPT_OR_NMI:
            case VMX_EXIT_EXT_INT:
            case VMX_EXIT_TRIPLE_FAULT:
            case VMX_EXIT_INT_WINDOW:
            case VMX_EXIT_NMI_WINDOW:
            case VMX_EXIT_TASK_SWITCH:
            case VMX_EXIT_TPR_BELOW_THRESHOLD:
            case VMX_EXIT_APIC_ACCESS:
            case VMX_EXIT_EPT_VIOLATION:
            case VMX_EXIT_EPT_MISCONFIG:
            case VMX_EXIT_PREEMPT_TIMER:

            /* Instruction specific VM-exits: */
            case VMX_EXIT_CPUID:
            case VMX_EXIT_GETSEC:
            case VMX_EXIT_HLT:
            case VMX_EXIT_INVD:
            case VMX_EXIT_INVLPG:
            case VMX_EXIT_RDPMC:
            case VMX_EXIT_RDTSC:
            case VMX_EXIT_RSM:
            case VMX_EXIT_VMCALL:
            case VMX_EXIT_VMCLEAR:
            case VMX_EXIT_VMLAUNCH:
            case VMX_EXIT_VMPTRLD:
            case VMX_EXIT_VMPTRST:
            case VMX_EXIT_VMREAD:
            case VMX_EXIT_VMRESUME:
            case VMX_EXIT_VMWRITE:
            case VMX_EXIT_VMXOFF:
            case VMX_EXIT_VMXON:
            case VMX_EXIT_MOV_CRX:
            case VMX_EXIT_MOV_DRX:
            case VMX_EXIT_IO_INSTR:
            case VMX_EXIT_RDMSR:
            case VMX_EXIT_WRMSR:
            case VMX_EXIT_MWAIT:
            case VMX_EXIT_MONITOR:
            case VMX_EXIT_PAUSE:
            case VMX_EXIT_GDTR_IDTR_ACCESS:
            case VMX_EXIT_LDTR_TR_ACCESS:
            case VMX_EXIT_INVEPT:
            case VMX_EXIT_RDTSCP:
            case VMX_EXIT_INVVPID:
            case VMX_EXIT_WBINVD:
            case VMX_EXIT_XSETBV:
            case VMX_EXIT_RDRAND:
            case VMX_EXIT_INVPCID:
            case VMX_EXIT_VMFUNC:
            case VMX_EXIT_RDSEED:
            case VMX_EXIT_XSAVES:
            case VMX_EXIT_XRSTORS:
            {
                int rc = vmxHCImportGuestState<CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP>(pVCpu, pVmxTransient->pVmcsInfo, __FUNCTION__);
                AssertRCReturn(rc, rc);
                if (   pVCpu->cpum.GstCtx.rip    != pDbgState->uRipStart
                    || pVCpu->cpum.GstCtx.cs.Sel != pDbgState->uCsStart)
                    return VINF_EM_DBG_STEPPED;
                break;
            }

            /* Errors and unexpected events: */
            case VMX_EXIT_INIT_SIGNAL:
            case VMX_EXIT_SIPI:
            case VMX_EXIT_IO_SMI:
            case VMX_EXIT_SMI:
            case VMX_EXIT_ERR_INVALID_GUEST_STATE:
            case VMX_EXIT_ERR_MSR_LOAD:
            case VMX_EXIT_ERR_MACHINE_CHECK:
            case VMX_EXIT_PML_FULL:
            case VMX_EXIT_VIRTUALIZED_EOI:
            case VMX_EXIT_APIC_WRITE:  /* Some talk about this being fault like, so I guess we must process it? */
                break;

            default:
                AssertMsgFailed(("Unexpected VM-exit=%#x\n", uExitReason));
                break;
        }
    }

    /*
     * Check for debugger event breakpoints and dtrace probes.
     */
    if (   uExitReason < RT_ELEMENTS(pDbgState->bmExitsToCheck) * 32U
        && ASMBitTest(pDbgState->bmExitsToCheck, uExitReason) )
    {
        VBOXSTRICTRC rcStrict = vmxHCHandleExitDtraceEvents(pVCpu, pVmxTransient, uExitReason);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    /*
     * Normal processing.
     */
#ifdef HMVMX_USE_FUNCTION_TABLE
    return g_aVMExitHandlers[uExitReason].pfn(pVCpu, pVmxTransient);
#else
    return vmxHCHandleExit(pVCpu, pVmxTransient, uExitReason);
#endif
}

/** @} */
