/* $Id: HMSVMR0.cpp 107963 2025-01-20 16:26:58Z knut.osmundsen@oracle.com $ */
/** @file
 * HM SVM (AMD-V) - Host Context Ring-0.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_HM
#define VMCPU_INCL_CPUM_GST_CTX
#include <iprt/asm-amd64-x86.h>
#include <iprt/thread.h>

#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/gcm.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/apic.h>
#include "HMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include "HMSVMR0.h"
#include "dtrace/VBoxVMM.h"

#ifdef DEBUG_ramshankar
# define HMSVM_SYNC_FULL_GUEST_STATE
# define HMSVM_ALWAYS_TRAP_ALL_XCPTS
# define HMSVM_ALWAYS_TRAP_PF
# define HMSVM_ALWAYS_TRAP_TASK_SWITCH
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_STATISTICS
# define HMSVM_EXITCODE_STAM_COUNTER_INC(u64ExitCode) do { \
        STAM_COUNTER_INC(&pVCpu->hm.s.StatExitAll); \
        if ((u64ExitCode) == SVM_EXIT_NPF) \
            STAM_COUNTER_INC(&pVCpu->hm.s.StatExitReasonNpf); \
        else \
            STAM_COUNTER_INC(&pVCpu->hm.s.aStatExitReason[(u64ExitCode) & MASK_EXITREASON_STAT]); \
        } while (0)

# define HMSVM_DEBUG_EXITCODE_STAM_COUNTER_INC(u64ExitCode) do { \
        STAM_COUNTER_INC(&pVCpu->hm.s.StatDebugExitAll); \
        if ((u64ExitCode) == SVM_EXIT_NPF) \
            STAM_COUNTER_INC(&pVCpu->hm.s.StatExitReasonNpf); \
        else \
            STAM_COUNTER_INC(&pVCpu->hm.s.aStatExitReason[(u64ExitCode) & MASK_EXITREASON_STAT]); \
        } while (0)

# define HMSVM_NESTED_EXITCODE_STAM_COUNTER_INC(u64ExitCode) do { \
        STAM_COUNTER_INC(&pVCpu->hm.s.StatNestedExitAll); \
        if ((u64ExitCode) == SVM_EXIT_NPF) \
            STAM_COUNTER_INC(&pVCpu->hm.s.StatNestedExitReasonNpf); \
        else \
            STAM_COUNTER_INC(&pVCpu->hm.s.aStatNestedExitReason[(u64ExitCode) & MASK_EXITREASON_STAT]); \
        } while (0)
#else
# define HMSVM_EXITCODE_STAM_COUNTER_INC(u64ExitCode)           do { } while (0)
# define HMSVM_DEBUG_EXITCODE_STAM_COUNTER_INC(u64ExitCode)     do { } while (0)
# define HMSVM_NESTED_EXITCODE_STAM_COUNTER_INC(u64ExitCode)    do { } while (0)
#endif /* !VBOX_WITH_STATISTICS */

/** If we decide to use a function table approach this can be useful to
 *  switch to a "static DECLCALLBACK(int)". */
#define HMSVM_EXIT_DECL                 static VBOXSTRICTRC

/**
 * Subset of the guest-CPU state that is kept by SVM R0 code while executing the
 * guest using hardware-assisted SVM.
 *
 * This excludes state like TSC AUX, GPRs (other than RSP, RAX) which are always
 * are swapped and restored across the world-switch and also registers like
 * EFER, PAT MSR etc. which cannot be modified by the guest without causing a
 * \#VMEXIT.
 */
#define HMSVM_CPUMCTX_EXTRN_ALL         (  CPUMCTX_EXTRN_RIP            \
                                         | CPUMCTX_EXTRN_RFLAGS         \
                                         | CPUMCTX_EXTRN_RAX            \
                                         | CPUMCTX_EXTRN_RSP            \
                                         | CPUMCTX_EXTRN_SREG_MASK      \
                                         | CPUMCTX_EXTRN_CR0            \
                                         | CPUMCTX_EXTRN_CR2            \
                                         | CPUMCTX_EXTRN_CR3            \
                                         | CPUMCTX_EXTRN_TABLE_MASK     \
                                         | CPUMCTX_EXTRN_DR6            \
                                         | CPUMCTX_EXTRN_DR7            \
                                         | CPUMCTX_EXTRN_KERNEL_GS_BASE \
                                         | CPUMCTX_EXTRN_SYSCALL_MSRS   \
                                         | CPUMCTX_EXTRN_SYSENTER_MSRS  \
                                         | CPUMCTX_EXTRN_HWVIRT         \
                                         | CPUMCTX_EXTRN_INHIBIT_INT    \
                                         | CPUMCTX_EXTRN_HM_SVM_MASK)

/**
 * Subset of the guest-CPU state that is shared between the guest and host.
 */
#define HMSVM_CPUMCTX_SHARED_STATE      CPUMCTX_EXTRN_DR_MASK

/** Macro for importing guest state from the VMCB back into CPUMCTX.  */
#define HMSVM_CPUMCTX_IMPORT_STATE(a_pVCpu, a_fWhat) \
    do { \
        if ((a_pVCpu)->cpum.GstCtx.fExtrn & (a_fWhat)) \
            hmR0SvmImportGuestState((a_pVCpu), (a_fWhat)); \
    } while (0)

/** Assert that the required state bits are fetched. */
#define HMSVM_CPUMCTX_ASSERT(a_pVCpu, a_fExtrnMbz)      AssertMsg(!((a_pVCpu)->cpum.GstCtx.fExtrn & (a_fExtrnMbz)), \
                                                                  ("fExtrn=%#RX64 fExtrnMbz=%#RX64\n", \
                                                                  (a_pVCpu)->cpum.GstCtx.fExtrn, (a_fExtrnMbz)))

/** Assert that preemption is disabled or covered by thread-context hooks. */
#define HMSVM_ASSERT_PREEMPT_SAFE(a_pVCpu)              Assert(   VMMR0ThreadCtxHookIsEnabled((a_pVCpu)) \
                                                               || !RTThreadPreemptIsEnabled(NIL_RTTHREAD));

/** Assert that we haven't migrated CPUs when thread-context hooks are not
 *  used. */
#define HMSVM_ASSERT_CPU_SAFE(a_pVCpu)                  AssertMsg(   VMMR0ThreadCtxHookIsEnabled((a_pVCpu)) \
                                                                  || (a_pVCpu)->hmr0.s.idEnteredCpu == RTMpCpuId(), \
                                                                  ("Illegal migration! Entered on CPU %u Current %u\n", \
                                                                   (a_pVCpu)->hmr0.s.idEnteredCpu, RTMpCpuId()));

/** Assert that we're not executing a nested-guest. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
# define HMSVM_ASSERT_NOT_IN_NESTED_GUEST(a_pCtx)       Assert(!CPUMIsGuestInSvmNestedHwVirtMode((a_pCtx)))
#else
# define HMSVM_ASSERT_NOT_IN_NESTED_GUEST(a_pCtx)       do { NOREF((a_pCtx)); } while (0)
#endif

/** Assert that we're executing a nested-guest. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
# define HMSVM_ASSERT_IN_NESTED_GUEST(a_pCtx)           Assert(CPUMIsGuestInSvmNestedHwVirtMode((a_pCtx)))
#else
# define HMSVM_ASSERT_IN_NESTED_GUEST(a_pCtx)           do { NOREF((a_pCtx)); } while (0)
#endif

/** Macro for checking and returning from the using function for
 * \#VMEXIT intercepts that maybe caused during delivering of another
 * event in the guest. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
# define HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(a_pVCpu, a_pSvmTransient) \
    do \
    { \
        int rc = hmR0SvmCheckExitDueToEventDelivery((a_pVCpu), (a_pSvmTransient)); \
        if (RT_LIKELY(rc == VINF_SUCCESS))        { /* continue #VMEXIT handling */ } \
        else if (     rc == VINF_HM_DOUBLE_FAULT) { return VINF_SUCCESS;            } \
        else if (     rc == VINF_EM_RESET \
                 &&   CPUMIsGuestSvmCtrlInterceptSet((a_pVCpu), &(a_pVCpu)->cpum.GstCtx, SVM_CTRL_INTERCEPT_SHUTDOWN)) \
        { \
            HMSVM_CPUMCTX_IMPORT_STATE((a_pVCpu), HMSVM_CPUMCTX_EXTRN_ALL); \
            return IEMExecSvmVmexit((a_pVCpu), SVM_EXIT_SHUTDOWN, 0, 0); \
        } \
        else \
            return rc; \
    } while (0)
#else
# define HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(a_pVCpu, a_pSvmTransient) \
    do \
    { \
        int rc = hmR0SvmCheckExitDueToEventDelivery((a_pVCpu), (a_pSvmTransient)); \
        if (RT_LIKELY(rc == VINF_SUCCESS))        { /* continue #VMEXIT handling */ } \
        else if (     rc == VINF_HM_DOUBLE_FAULT) { return VINF_SUCCESS;            } \
        else \
            return rc; \
    } while (0)
#endif

/** Macro for upgrading a @a a_rc to VINF_EM_DBG_STEPPED after emulating an
 * instruction that exited. */
#define HMSVM_CHECK_SINGLE_STEP(a_pVCpu, a_rc) \
    do { \
        if ((a_pVCpu)->hm.s.fSingleInstruction && (a_rc) == VINF_SUCCESS) \
            (a_rc) = VINF_EM_DBG_STEPPED; \
    } while (0)

/** Validate segment descriptor granularity bit. */
#ifdef VBOX_STRICT
# define HMSVM_ASSERT_SEG_GRANULARITY(a_pCtx, reg) \
    AssertMsg(   !(a_pCtx)->reg.Attr.n.u1Present \
              || (   (a_pCtx)->reg.Attr.n.u1Granularity \
                  ? ((a_pCtx)->reg.u32Limit & 0xfff) == 0xfff \
                  :  (a_pCtx)->reg.u32Limit <= UINT32_C(0xfffff)), \
              ("Invalid Segment Attributes Limit=%#RX32 Attr=%#RX32 Base=%#RX64\n", (a_pCtx)->reg.u32Limit, \
              (a_pCtx)->reg.Attr.u, (a_pCtx)->reg.u64Base))
#else
# define HMSVM_ASSERT_SEG_GRANULARITY(a_pCtx, reg)      do { } while (0)
#endif

/**
 * Exception bitmap mask for all contributory exceptions.
 *
 * Page fault is deliberately excluded here as it's conditional as to whether
 * it's contributory or benign. Page faults are handled separately.
 */
#define HMSVM_CONTRIBUTORY_XCPT_MASK  (  RT_BIT(X86_XCPT_GP) | RT_BIT(X86_XCPT_NP) | RT_BIT(X86_XCPT_SS) | RT_BIT(X86_XCPT_TS) \
                                       | RT_BIT(X86_XCPT_DE))

/**
 * Mandatory/unconditional guest control intercepts.
 *
 * SMIs can and do happen in normal operation. We need not intercept them
 * while executing the guest (or nested-guest).
 */
#define HMSVM_MANDATORY_GUEST_CTRL_INTERCEPTS           (  SVM_CTRL_INTERCEPT_INTR          \
                                                         | SVM_CTRL_INTERCEPT_NMI           \
                                                         | SVM_CTRL_INTERCEPT_INIT          \
                                                         | SVM_CTRL_INTERCEPT_RDPMC         \
                                                         | SVM_CTRL_INTERCEPT_CPUID         \
                                                         | SVM_CTRL_INTERCEPT_RSM           \
                                                         | SVM_CTRL_INTERCEPT_HLT           \
                                                         | SVM_CTRL_INTERCEPT_IOIO_PROT     \
                                                         | SVM_CTRL_INTERCEPT_MSR_PROT      \
                                                         | SVM_CTRL_INTERCEPT_INVLPGA       \
                                                         | SVM_CTRL_INTERCEPT_SHUTDOWN      \
                                                         | SVM_CTRL_INTERCEPT_FERR_FREEZE   \
                                                         | SVM_CTRL_INTERCEPT_VMRUN         \
                                                         | SVM_CTRL_INTERCEPT_SKINIT        \
                                                         | SVM_CTRL_INTERCEPT_WBINVD        \
                                                         | SVM_CTRL_INTERCEPT_MONITOR       \
                                                         | SVM_CTRL_INTERCEPT_MWAIT         \
                                                         | SVM_CTRL_INTERCEPT_CR0_SEL_WRITE \
                                                         | SVM_CTRL_INTERCEPT_XSETBV)

/** @name VMCB Clean Bits.
 *
 * These flags are used for VMCB-state caching. A set VMCB Clean bit indicates
 * AMD-V doesn't need to reload the corresponding value(s) from the VMCB in
 * memory.
 *
 * @{ */
/** All intercepts vectors, TSC offset, PAUSE filter counter. */
#define HMSVM_VMCB_CLEAN_INTERCEPTS             RT_BIT(0)
/** I/O permission bitmap, MSR permission bitmap. */
#define HMSVM_VMCB_CLEAN_IOPM_MSRPM             RT_BIT(1)
/** ASID.  */
#define HMSVM_VMCB_CLEAN_ASID                   RT_BIT(2)
/** TRP: V_TPR, V_IRQ, V_INTR_PRIO, V_IGN_TPR, V_INTR_MASKING,
V_INTR_VECTOR. */
#define HMSVM_VMCB_CLEAN_INT_CTRL               RT_BIT(3)
/** Nested Paging: Nested CR3 (nCR3), PAT. */
#define HMSVM_VMCB_CLEAN_NP                     RT_BIT(4)
/** Control registers (CR0, CR3, CR4, EFER). */
#define HMSVM_VMCB_CLEAN_CRX_EFER               RT_BIT(5)
/** Debug registers (DR6, DR7). */
#define HMSVM_VMCB_CLEAN_DRX                    RT_BIT(6)
/** GDT, IDT limit and base. */
#define HMSVM_VMCB_CLEAN_DT                     RT_BIT(7)
/** Segment register: CS, SS, DS, ES limit and base. */
#define HMSVM_VMCB_CLEAN_SEG                    RT_BIT(8)
/** CR2.*/
#define HMSVM_VMCB_CLEAN_CR2                    RT_BIT(9)
/** Last-branch record (DbgCtlMsr, br_from, br_to, lastint_from, lastint_to) */
#define HMSVM_VMCB_CLEAN_LBR                    RT_BIT(10)
/** AVIC (AVIC APIC_BAR; AVIC APIC_BACKING_PAGE, AVIC
PHYSICAL_TABLE and AVIC LOGICAL_TABLE Pointers). */
#define HMSVM_VMCB_CLEAN_AVIC                   RT_BIT(11)
/** Mask of all valid VMCB Clean bits. */
#define HMSVM_VMCB_CLEAN_ALL                    (  HMSVM_VMCB_CLEAN_INTERCEPTS  \
                                                 | HMSVM_VMCB_CLEAN_IOPM_MSRPM  \
                                                 | HMSVM_VMCB_CLEAN_ASID        \
                                                 | HMSVM_VMCB_CLEAN_INT_CTRL    \
                                                 | HMSVM_VMCB_CLEAN_NP          \
                                                 | HMSVM_VMCB_CLEAN_CRX_EFER    \
                                                 | HMSVM_VMCB_CLEAN_DRX         \
                                                 | HMSVM_VMCB_CLEAN_DT          \
                                                 | HMSVM_VMCB_CLEAN_SEG         \
                                                 | HMSVM_VMCB_CLEAN_CR2         \
                                                 | HMSVM_VMCB_CLEAN_LBR         \
                                                 | HMSVM_VMCB_CLEAN_AVIC)
/** @} */

/**
 * MSRPM (MSR permission bitmap) read permissions (for guest RDMSR).
 */
typedef enum SVMMSREXITREAD
{
    /** Reading this MSR causes a \#VMEXIT. */
    SVMMSREXIT_INTERCEPT_READ = 0xb,
    /** Reading this MSR does not cause a \#VMEXIT. */
    SVMMSREXIT_PASSTHRU_READ
} SVMMSREXITREAD;

/**
 * MSRPM (MSR permission bitmap) write permissions (for guest WRMSR).
 */
typedef enum SVMMSREXITWRITE
{
    /** Writing to this MSR causes a \#VMEXIT. */
    SVMMSREXIT_INTERCEPT_WRITE = 0xd,
    /** Writing to this MSR does not cause a \#VMEXIT. */
    SVMMSREXIT_PASSTHRU_WRITE
} SVMMSREXITWRITE;

/**
 * SVM \#VMEXIT handler.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM-transient structure.
 */
typedef VBOXSTRICTRC FNSVMEXITHANDLER(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient);


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void hmR0SvmPendingEventToTrpmTrap(PVMCPUCC pVCpu);
static void hmR0SvmLeave(PVMCPUCC pVCpu, bool fImportState);


/** @name \#VMEXIT handlers.
 * @{
 */
static FNSVMEXITHANDLER hmR0SvmExitIntr;
static FNSVMEXITHANDLER hmR0SvmExitWbinvd;
static FNSVMEXITHANDLER hmR0SvmExitInvd;
static FNSVMEXITHANDLER hmR0SvmExitCpuid;
static FNSVMEXITHANDLER hmR0SvmExitRdtsc;
static FNSVMEXITHANDLER hmR0SvmExitRdtscp;
static FNSVMEXITHANDLER hmR0SvmExitRdpmc;
static FNSVMEXITHANDLER hmR0SvmExitInvlpg;
static FNSVMEXITHANDLER hmR0SvmExitHlt;
static FNSVMEXITHANDLER hmR0SvmExitMonitor;
static FNSVMEXITHANDLER hmR0SvmExitMwait;
static FNSVMEXITHANDLER hmR0SvmExitShutdown;
static FNSVMEXITHANDLER hmR0SvmExitUnexpected;
static FNSVMEXITHANDLER hmR0SvmExitReadCRx;
static FNSVMEXITHANDLER hmR0SvmExitWriteCRx;
static FNSVMEXITHANDLER hmR0SvmExitMsr;
static FNSVMEXITHANDLER hmR0SvmExitReadDRx;
static FNSVMEXITHANDLER hmR0SvmExitWriteDRx;
static FNSVMEXITHANDLER hmR0SvmExitXsetbv;
static FNSVMEXITHANDLER hmR0SvmExitIOInstr;
static FNSVMEXITHANDLER hmR0SvmExitNestedPF;
static FNSVMEXITHANDLER hmR0SvmExitVIntr;
static FNSVMEXITHANDLER hmR0SvmExitTaskSwitch;
static FNSVMEXITHANDLER hmR0SvmExitVmmCall;
static FNSVMEXITHANDLER hmR0SvmExitPause;
static FNSVMEXITHANDLER hmR0SvmExitFerrFreeze;
static FNSVMEXITHANDLER hmR0SvmExitIret;
static FNSVMEXITHANDLER hmR0SvmExitXcptDE;
static FNSVMEXITHANDLER hmR0SvmExitXcptPF;
static FNSVMEXITHANDLER hmR0SvmExitXcptUD;
static FNSVMEXITHANDLER hmR0SvmExitXcptMF;
static FNSVMEXITHANDLER hmR0SvmExitXcptDB;
static FNSVMEXITHANDLER hmR0SvmExitXcptAC;
static FNSVMEXITHANDLER hmR0SvmExitXcptBP;
static FNSVMEXITHANDLER hmR0SvmExitXcptGP;
static FNSVMEXITHANDLER hmR0SvmExitXcptGeneric;
static FNSVMEXITHANDLER hmR0SvmExitSwInt;
static FNSVMEXITHANDLER hmR0SvmExitTrRead;
static FNSVMEXITHANDLER hmR0SvmExitTrWrite;
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
static FNSVMEXITHANDLER hmR0SvmExitClgi;
static FNSVMEXITHANDLER hmR0SvmExitStgi;
static FNSVMEXITHANDLER hmR0SvmExitVmload;
static FNSVMEXITHANDLER hmR0SvmExitVmsave;
static FNSVMEXITHANDLER hmR0SvmExitInvlpga;
static FNSVMEXITHANDLER hmR0SvmExitVmrun;
static FNSVMEXITHANDLER hmR0SvmNestedExitXcptDB;
static FNSVMEXITHANDLER hmR0SvmNestedExitXcptBP;
#endif
/** @} */

static VBOXSTRICTRC hmR0SvmHandleExit(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient);
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
static VBOXSTRICTRC hmR0SvmHandleExitNested(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient);
#endif
static VBOXSTRICTRC hmR0SvmRunGuestCodeDebug(PVMCPUCC pVCpu, uint32_t *pcLoops);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Ring-0 memory object for the IO bitmap. */
static RTR0MEMOBJ           g_hMemObjIOBitmap = NIL_RTR0MEMOBJ;
/** Physical address of the IO bitmap. */
static RTHCPHYS             g_HCPhysIOBitmap;
/** Pointer to the IO bitmap. */
static R0PTRTYPE(void *)    g_pvIOBitmap;

#ifdef VBOX_STRICT
# define HMSVM_LOG_RBP_RSP      RT_BIT_32(0)
# define HMSVM_LOG_CR_REGS      RT_BIT_32(1)
# define HMSVM_LOG_CS           RT_BIT_32(2)
# define HMSVM_LOG_SS           RT_BIT_32(3)
# define HMSVM_LOG_FS           RT_BIT_32(4)
# define HMSVM_LOG_GS           RT_BIT_32(5)
# define HMSVM_LOG_LBR          RT_BIT_32(6)
# define HMSVM_LOG_ALL          (  HMSVM_LOG_RBP_RSP \
                                 | HMSVM_LOG_CR_REGS \
                                 | HMSVM_LOG_CS \
                                 | HMSVM_LOG_SS \
                                 | HMSVM_LOG_FS \
                                 | HMSVM_LOG_GS \
                                 | HMSVM_LOG_LBR)

/**
 * Dumps virtual CPU state and additional info. to the logger for diagnostics.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 * @param   pszPrefix   Log prefix.
 * @param   fFlags      Log flags, see HMSVM_LOG_XXX.
 * @param   uVerbose    The verbosity level, currently unused.
 */
static void hmR0SvmLogState(PVMCPUCC pVCpu, PCSVMVMCB pVmcb, const char *pszPrefix, uint32_t fFlags, uint8_t uVerbose)
{
    RT_NOREF2(pVCpu, uVerbose);
    PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;

    HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS);
    Log4(("%s: cs:rip=%04x:%RX64 efl=%#RX64\n", pszPrefix, pCtx->cs.Sel, pCtx->rip, pCtx->rflags.u));

    if (fFlags & HMSVM_LOG_RBP_RSP)
    {
        HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_RSP | CPUMCTX_EXTRN_RBP);
        Log4(("%s: rsp=%#RX64 rbp=%#RX64\n", pszPrefix, pCtx->rsp, pCtx->rbp));
    }

    if (fFlags & HMSVM_LOG_CR_REGS)
    {
        HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4);
        Log4(("%s: cr0=%#RX64 cr3=%#RX64 cr4=%#RX64\n", pszPrefix, pCtx->cr0, pCtx->cr3, pCtx->cr4));
    }

    if (fFlags & HMSVM_LOG_CS)
    {
        HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CS);
        Log4(("%s: cs={%04x base=%016RX64 limit=%08x flags=%08x}\n", pszPrefix, pCtx->cs.Sel, pCtx->cs.u64Base,
              pCtx->cs.u32Limit, pCtx->cs.Attr.u));
    }
    if (fFlags & HMSVM_LOG_SS)
    {
        HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SS);
        Log4(("%s: ss={%04x base=%016RX64 limit=%08x flags=%08x}\n", pszPrefix, pCtx->ss.Sel, pCtx->ss.u64Base,
              pCtx->ss.u32Limit, pCtx->ss.Attr.u));
    }
    if (fFlags & HMSVM_LOG_FS)
    {
        HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_FS);
        Log4(("%s: fs={%04x base=%016RX64 limit=%08x flags=%08x}\n", pszPrefix, pCtx->fs.Sel, pCtx->fs.u64Base,
              pCtx->fs.u32Limit, pCtx->fs.Attr.u));
    }
    if (fFlags & HMSVM_LOG_GS)
    {
        HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_GS);
        Log4(("%s: gs={%04x base=%016RX64 limit=%08x flags=%08x}\n", pszPrefix, pCtx->gs.Sel, pCtx->gs.u64Base,
              pCtx->gs.u32Limit, pCtx->gs.Attr.u));
    }

    PCSVMVMCBSTATESAVE pVmcbGuest = &pVmcb->guest;
    if (fFlags & HMSVM_LOG_LBR)
    {
        Log4(("%s: br_from=%#RX64 br_to=%#RX64 lastxcpt_from=%#RX64 lastxcpt_to=%#RX64\n", pszPrefix, pVmcbGuest->u64BR_FROM,
              pVmcbGuest->u64BR_TO, pVmcbGuest->u64LASTEXCPFROM, pVmcbGuest->u64LASTEXCPTO));
    }
    NOREF(pszPrefix); NOREF(pVmcbGuest); NOREF(pCtx);
}
#endif  /* VBOX_STRICT */


/**
 * Sets up and activates AMD-V on the current CPU.
 *
 * @returns VBox status code.
 * @param   pHostCpu        The HM physical-CPU structure.
 * @param   pVM             The cross context VM structure. Can be
 *                          NULL after a resume!
 * @param   pvCpuPage       Pointer to the global CPU page.
 * @param   HCPhysCpuPage   Physical address of the global CPU page.
 * @param   fEnabledByHost  Whether the host OS has already initialized AMD-V.
 * @param   pHwvirtMsrs     Pointer to the hardware-virtualization MSRs (currently
 *                          unused).
 */
VMMR0DECL(int) SVMR0EnableCpu(PHMPHYSCPU pHostCpu, PVMCC pVM, void *pvCpuPage, RTHCPHYS HCPhysCpuPage, bool fEnabledByHost,
                              PCSUPHWVIRTMSRS pHwvirtMsrs)
{
    Assert(!fEnabledByHost);
    Assert(HCPhysCpuPage && HCPhysCpuPage != NIL_RTHCPHYS);
    Assert(RT_ALIGN_T(HCPhysCpuPage, _4K, RTHCPHYS) == HCPhysCpuPage);
    Assert(pvCpuPage); NOREF(pvCpuPage);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    RT_NOREF2(fEnabledByHost, pHwvirtMsrs);

    /* Paranoid: Disable interrupt as, in theory, interrupt handlers might mess with EFER. */
    RTCCUINTREG const fEFlags = ASMIntDisableFlags();

    /*
     * We must turn on AMD-V and setup the host state physical address, as those MSRs are per CPU.
     */
    uint64_t u64HostEfer = ASMRdMsr(MSR_K6_EFER);
    if (u64HostEfer & MSR_K6_EFER_SVME)
    {
        /* If the VBOX_HWVIRTEX_IGNORE_SVM_IN_USE is active, then we blindly use AMD-V. */
        if (   pVM
            && pVM->hm.s.svm.fIgnoreInUseError)
            pHostCpu->fIgnoreAMDVInUseError = true;

        if (!pHostCpu->fIgnoreAMDVInUseError)
        {
            ASMSetFlags(fEFlags);
            return VERR_SVM_IN_USE;
        }
    }

    /* Turn on AMD-V in the EFER MSR. */
    ASMWrMsr(MSR_K6_EFER, u64HostEfer | MSR_K6_EFER_SVME);

    /* Write the physical page address where the CPU will store the host state while executing the VM. */
    ASMWrMsr(MSR_K8_VM_HSAVE_PA, HCPhysCpuPage);

    /* Restore interrupts. */
    ASMSetFlags(fEFlags);

    /*
     * Theoretically, other hypervisors may have used ASIDs, ideally we should flush all
     * non-zero ASIDs when enabling SVM. AMD doesn't have an SVM instruction to flush all
     * ASIDs (flushing is done upon VMRUN). Therefore, flag that we need to flush the TLB
     * entirely with before executing any guest code.
     */
    pHostCpu->fFlushAsidBeforeUse = true;

    /*
     * Ensure each VCPU scheduled on this CPU gets a new ASID on resume. See @bugref{6255}.
     */
    ++pHostCpu->cTlbFlushes;

    return VINF_SUCCESS;
}


/**
 * Deactivates AMD-V on the current CPU.
 *
 * @returns VBox status code.
 * @param   pHostCpu        The HM physical-CPU structure.
 * @param   pvCpuPage       Pointer to the global CPU page.
 * @param   HCPhysCpuPage   Physical address of the global CPU page.
 */
VMMR0DECL(int) SVMR0DisableCpu(PHMPHYSCPU pHostCpu, void *pvCpuPage, RTHCPHYS HCPhysCpuPage)
{
    RT_NOREF1(pHostCpu);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    AssertReturn(   HCPhysCpuPage
                 && HCPhysCpuPage != NIL_RTHCPHYS, VERR_INVALID_PARAMETER);
    AssertReturn(pvCpuPage, VERR_INVALID_PARAMETER);

    /* Paranoid: Disable interrupts as, in theory, interrupt handlers might mess with EFER. */
    RTCCUINTREG const fEFlags = ASMIntDisableFlags();

    /* Turn off AMD-V in the EFER MSR. */
    uint64_t u64HostEfer = ASMRdMsr(MSR_K6_EFER);
    ASMWrMsr(MSR_K6_EFER, u64HostEfer & ~MSR_K6_EFER_SVME);

    /* Invalidate host state physical address. */
    ASMWrMsr(MSR_K8_VM_HSAVE_PA, 0);

    /* Restore interrupts. */
    ASMSetFlags(fEFlags);

    return VINF_SUCCESS;
}


/**
 * Does global AMD-V initialization (called during module initialization).
 *
 * @returns VBox status code.
 */
VMMR0DECL(int) SVMR0GlobalInit(void)
{
    /*
     * Allocate 12 KB (3 pages) for the IO bitmap. Since this is non-optional and we always
     * intercept all IO accesses, it's done once globally here instead of per-VM.
     */
    Assert(g_hMemObjIOBitmap == NIL_RTR0MEMOBJ);
    int rc = RTR0MemObjAllocCont(&g_hMemObjIOBitmap, SVM_IOPM_PAGES << X86_PAGE_4K_SHIFT,
                                 NIL_RTHCPHYS /*PhysHighest*/, false /* fExecutable */);
    if (RT_FAILURE(rc))
        return rc;

    g_pvIOBitmap     = RTR0MemObjAddress(g_hMemObjIOBitmap);
    g_HCPhysIOBitmap = RTR0MemObjGetPagePhysAddr(g_hMemObjIOBitmap, 0 /* iPage */);

    /* Set all bits to intercept all IO accesses. */
    ASMMemFill32(g_pvIOBitmap, SVM_IOPM_PAGES << X86_PAGE_4K_SHIFT, UINT32_C(0xffffffff));

    return VINF_SUCCESS;
}


/**
 * Does global AMD-V termination (called during module termination).
 */
VMMR0DECL(void) SVMR0GlobalTerm(void)
{
    if (g_hMemObjIOBitmap != NIL_RTR0MEMOBJ)
    {
        RTR0MemObjFree(g_hMemObjIOBitmap, true /* fFreeMappings */);
        g_pvIOBitmap      = NULL;
        g_HCPhysIOBitmap  = 0;
        g_hMemObjIOBitmap = NIL_RTR0MEMOBJ;
    }
}


/**
 * Frees any allocated per-VCPU structures for a VM.
 *
 * @param   pVM     The cross context VM structure.
 */
DECLINLINE(void) hmR0SvmFreeStructs(PVMCC pVM)
{
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPUCC pVCpu = VMCC_GET_CPU(pVM, idCpu);
        AssertPtr(pVCpu);

        if (pVCpu->hmr0.s.svm.hMemObjVmcbHost != NIL_RTR0MEMOBJ)
        {
            RTR0MemObjFree(pVCpu->hmr0.s.svm.hMemObjVmcbHost, false);
            pVCpu->hmr0.s.svm.HCPhysVmcbHost   = 0;
            pVCpu->hmr0.s.svm.hMemObjVmcbHost  = NIL_RTR0MEMOBJ;
        }

        if (pVCpu->hmr0.s.svm.hMemObjVmcb != NIL_RTR0MEMOBJ)
        {
            RTR0MemObjFree(pVCpu->hmr0.s.svm.hMemObjVmcb, false);
            pVCpu->hmr0.s.svm.pVmcb            = NULL;
            pVCpu->hmr0.s.svm.HCPhysVmcb       = 0;
            pVCpu->hmr0.s.svm.hMemObjVmcb      = NIL_RTR0MEMOBJ;
        }

        if (pVCpu->hmr0.s.svm.hMemObjMsrBitmap != NIL_RTR0MEMOBJ)
        {
            RTR0MemObjFree(pVCpu->hmr0.s.svm.hMemObjMsrBitmap, false);
            pVCpu->hmr0.s.svm.pvMsrBitmap      = NULL;
            pVCpu->hmr0.s.svm.HCPhysMsrBitmap  = 0;
            pVCpu->hmr0.s.svm.hMemObjMsrBitmap = NIL_RTR0MEMOBJ;
        }
    }
}


/**
 * Sets pfnVMRun to the best suited variant.
 *
 * This must be called whenever anything changes relative to the SVMR0VMRun
 * variant selection:
 *      - pVCpu->hm.s.fLoadSaveGuestXcr0
 *      - CPUMCTX_WSF_IBPB_ENTRY in pVCpu->cpum.GstCtx.fWorldSwitcher
 *      - CPUMCTX_WSF_IBPB_EXIT  in pVCpu->cpum.GstCtx.fWorldSwitcher
 *      - Perhaps: CPUMIsGuestFPUStateActive() (windows only)
 *      - Perhaps: CPUMCTX.fXStateMask (windows only)
 *
 * We currently ASSUME that neither CPUMCTX_WSF_IBPB_ENTRY nor
 * CPUMCTX_WSF_IBPB_EXIT cannot be changed at runtime.
 */
static void hmR0SvmUpdateVmRunFunction(PVMCPUCC pVCpu)
{
    static const struct CLANGWORKAROUND { PFNHMSVMVMRUN pfn; } s_aHmR0SvmVmRunFunctions[] =
    {
        { hmR0SvmVmRun_SansXcr0_SansIbpbEntry_SansIbpbExit_SansSpecCtrl },
        { hmR0SvmVmRun_WithXcr0_SansIbpbEntry_SansIbpbExit_SansSpecCtrl },
        { hmR0SvmVmRun_SansXcr0_WithIbpbEntry_SansIbpbExit_SansSpecCtrl },
        { hmR0SvmVmRun_WithXcr0_WithIbpbEntry_SansIbpbExit_SansSpecCtrl },
        { hmR0SvmVmRun_SansXcr0_SansIbpbEntry_WithIbpbExit_SansSpecCtrl },
        { hmR0SvmVmRun_WithXcr0_SansIbpbEntry_WithIbpbExit_SansSpecCtrl },
        { hmR0SvmVmRun_SansXcr0_WithIbpbEntry_WithIbpbExit_SansSpecCtrl },
        { hmR0SvmVmRun_WithXcr0_WithIbpbEntry_WithIbpbExit_SansSpecCtrl },
        { hmR0SvmVmRun_SansXcr0_SansIbpbEntry_SansIbpbExit_WithSpecCtrl },
        { hmR0SvmVmRun_WithXcr0_SansIbpbEntry_SansIbpbExit_WithSpecCtrl },
        { hmR0SvmVmRun_SansXcr0_WithIbpbEntry_SansIbpbExit_WithSpecCtrl },
        { hmR0SvmVmRun_WithXcr0_WithIbpbEntry_SansIbpbExit_WithSpecCtrl },
        { hmR0SvmVmRun_SansXcr0_SansIbpbEntry_WithIbpbExit_WithSpecCtrl },
        { hmR0SvmVmRun_WithXcr0_SansIbpbEntry_WithIbpbExit_WithSpecCtrl },
        { hmR0SvmVmRun_SansXcr0_WithIbpbEntry_WithIbpbExit_WithSpecCtrl },
        { hmR0SvmVmRun_WithXcr0_WithIbpbEntry_WithIbpbExit_WithSpecCtrl },
    };
    uintptr_t const idx = (pVCpu->hmr0.s.fLoadSaveGuestXcr0                 ? 1 : 0)
                        | (pVCpu->hmr0.s.fWorldSwitcher & HM_WSF_IBPB_ENTRY ? 2 : 0)
                        | (pVCpu->hmr0.s.fWorldSwitcher & HM_WSF_IBPB_EXIT  ? 4 : 0)
                        | (pVCpu->hmr0.s.fWorldSwitcher & HM_WSF_SPEC_CTRL  ? 8 : 0);
    PFNHMSVMVMRUN const pfnVMRun = s_aHmR0SvmVmRunFunctions[idx].pfn;
    if (pVCpu->hmr0.s.svm.pfnVMRun != pfnVMRun)
        pVCpu->hmr0.s.svm.pfnVMRun = pfnVMRun;
}


/**
 * Selector FNHMSVMVMRUN implementation.
 */
static DECLCALLBACK(int) hmR0SvmVMRunSelector(PVMCC pVM, PVMCPUCC pVCpu, RTHCPHYS HCPhysVMCB)
{
    hmR0SvmUpdateVmRunFunction(pVCpu);
    return pVCpu->hmr0.s.svm.pfnVMRun(pVM, pVCpu, HCPhysVMCB);
}


/**
 * Does per-VM AMD-V initialization.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR0DECL(int) SVMR0InitVM(PVMCC pVM)
{
    int rc = VERR_INTERNAL_ERROR_5;

    /*
     * Check for an AMD CPU erratum which requires us to flush the TLB before every world-switch.
     */
    uint32_t u32Family;
    uint32_t u32Model;
    uint32_t u32Stepping;
    if (HMIsSubjectToSvmErratum170(&u32Family, &u32Model, &u32Stepping))
    {
        Log4Func(("AMD cpu with erratum 170 family %#x model %#x stepping %#x\n", u32Family, u32Model, u32Stepping));
        pVM->hmr0.s.svm.fAlwaysFlushTLB = true;
    }

    /*
     * Initialize the R0 memory objects up-front so we can properly cleanup on allocation failures.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPUCC pVCpu = VMCC_GET_CPU(pVM, idCpu);
        pVCpu->hmr0.s.svm.hMemObjVmcbHost  = NIL_RTR0MEMOBJ;
        pVCpu->hmr0.s.svm.hMemObjVmcb      = NIL_RTR0MEMOBJ;
        pVCpu->hmr0.s.svm.hMemObjMsrBitmap = NIL_RTR0MEMOBJ;
    }

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPUCC pVCpu = VMCC_GET_CPU(pVM, idCpu);

        /*
         * Initialize the hardware-assisted SVM guest-execution handler.
         * We now use a single handler for both 32-bit and 64-bit guests, see @bugref{6208#c73}.
         */
        pVCpu->hmr0.s.svm.pfnVMRun = hmR0SvmVMRunSelector;

        /*
         * Allocate one page for the host-context VM control block (VMCB). This is used for additional host-state (such as
         * FS, GS, Kernel GS Base, etc.) apart from the host-state save area specified in MSR_K8_VM_HSAVE_PA.
         */
        rc = RTR0MemObjAllocCont(&pVCpu->hmr0.s.svm.hMemObjVmcbHost, SVM_VMCB_PAGES << HOST_PAGE_SHIFT,
                                 NIL_RTHCPHYS /*PhysHighest*/, false /* fExecutable */);
        if (RT_FAILURE(rc))
            goto failure_cleanup;

        void *pvVmcbHost                    = RTR0MemObjAddress(pVCpu->hmr0.s.svm.hMemObjVmcbHost);
        pVCpu->hmr0.s.svm.HCPhysVmcbHost    = RTR0MemObjGetPagePhysAddr(pVCpu->hmr0.s.svm.hMemObjVmcbHost, 0 /* iPage */);
        RT_BZERO(pvVmcbHost, HOST_PAGE_SIZE);

        /*
         * Allocate one page for the guest-state VMCB.
         */
        rc = RTR0MemObjAllocCont(&pVCpu->hmr0.s.svm.hMemObjVmcb, SVM_VMCB_PAGES << HOST_PAGE_SHIFT,
                                 NIL_RTHCPHYS /*PhysHighest*/, false /* fExecutable */);
        if (RT_FAILURE(rc))
            goto failure_cleanup;

        pVCpu->hmr0.s.svm.pVmcb             = (PSVMVMCB)RTR0MemObjAddress(pVCpu->hmr0.s.svm.hMemObjVmcb);
        pVCpu->hmr0.s.svm.HCPhysVmcb        = RTR0MemObjGetPagePhysAddr(pVCpu->hmr0.s.svm.hMemObjVmcb, 0 /* iPage */);
        RT_BZERO(pVCpu->hmr0.s.svm.pVmcb, HOST_PAGE_SIZE);

        /*
         * Allocate two pages (8 KB) for the MSR permission bitmap. There doesn't seem to be a way to convince
         * SVM to not require one.
         */
        rc = RTR0MemObjAllocCont(&pVCpu->hmr0.s.svm.hMemObjMsrBitmap, SVM_MSRPM_PAGES << HOST_PAGE_SHIFT,
                                 NIL_RTHCPHYS /*PhysHighest*/, false /* fExecutable */);
        if (RT_FAILURE(rc))
            goto failure_cleanup;

        pVCpu->hmr0.s.svm.pvMsrBitmap       = RTR0MemObjAddress(pVCpu->hmr0.s.svm.hMemObjMsrBitmap);
        pVCpu->hmr0.s.svm.HCPhysMsrBitmap   = RTR0MemObjGetPagePhysAddr(pVCpu->hmr0.s.svm.hMemObjMsrBitmap, 0 /* iPage */);
        /* Set all bits to intercept all MSR accesses (changed later on). */
        ASMMemFill32(pVCpu->hmr0.s.svm.pvMsrBitmap, SVM_MSRPM_PAGES << HOST_PAGE_SHIFT, UINT32_C(0xffffffff));
   }

    return VINF_SUCCESS;

failure_cleanup:
    hmR0SvmFreeStructs(pVM);
    return rc;
}


/**
 * Does per-VM AMD-V termination.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR0DECL(int) SVMR0TermVM(PVMCC pVM)
{
    hmR0SvmFreeStructs(pVM);
    return VINF_SUCCESS;
}


/**
 * Returns whether the VMCB Clean Bits feature is supported.
 *
 * @returns @c true if supported, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fIsNestedGuest  Whether we are currently executing the nested-guest.
 */
DECL_FORCE_INLINE(bool) hmR0SvmSupportsVmcbCleanBits(PVMCPUCC pVCpu, bool fIsNestedGuest)
{
    PCVMCC pVM = pVCpu->CTX_SUFF(pVM);
    bool const fHostVmcbCleanBits = RT_BOOL(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_VMCB_CLEAN);
    if (!fIsNestedGuest)
        return fHostVmcbCleanBits;
    return fHostVmcbCleanBits && pVM->cpum.ro.GuestFeatures.fSvmVmcbClean;
}


/**
 * Returns whether the decode assists feature is supported.
 *
 * @returns @c true if supported, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(bool) hmR0SvmSupportsDecodeAssists(PVMCPUCC pVCpu)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx))
        return (g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_DECODE_ASSISTS)
            &&  pVM->cpum.ro.GuestFeatures.fSvmDecodeAssists;
#endif
    return RT_BOOL(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_DECODE_ASSISTS);
}


/**
 * Returns whether the NRIP_SAVE feature is supported.
 *
 * @returns @c true if supported, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(bool) hmR0SvmSupportsNextRipSave(PVMCPUCC pVCpu)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx))
        return (g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_NRIP_SAVE)
            &&  pVM->cpum.ro.GuestFeatures.fSvmNextRipSave;
#endif
    return RT_BOOL(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_NRIP_SAVE);
}


/**
 * Sets the permission bits for the specified MSR in the MSRPM bitmap.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pbMsrBitmap     Pointer to the MSR bitmap.
 * @param   idMsr           The MSR for which the permissions are being set.
 * @param   enmRead         MSR read permissions.
 * @param   enmWrite        MSR write permissions.
 *
 * @remarks This function does -not- clear the VMCB clean bits for MSRPM. The
 *          caller needs to take care of this.
 */
static void hmR0SvmSetMsrPermission(PVMCPUCC pVCpu, uint8_t *pbMsrBitmap, uint32_t idMsr, SVMMSREXITREAD enmRead,
                                    SVMMSREXITWRITE enmWrite)
{
    bool const  fInNestedGuestMode = CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx);
    uint16_t    offMsrpm;
    uint8_t     uMsrpmBit;
    int rc = CPUMGetSvmMsrpmOffsetAndBit(idMsr, &offMsrpm, &uMsrpmBit);
    AssertRC(rc);

    Assert(uMsrpmBit == 0 || uMsrpmBit == 2 || uMsrpmBit == 4 || uMsrpmBit == 6);
    Assert(offMsrpm < SVM_MSRPM_PAGES << X86_PAGE_4K_SHIFT);

    pbMsrBitmap += offMsrpm;
    if (enmRead == SVMMSREXIT_INTERCEPT_READ)
        *pbMsrBitmap |= RT_BIT(uMsrpmBit);
    else
    {
        if (!fInNestedGuestMode)
            *pbMsrBitmap &= ~RT_BIT(uMsrpmBit);
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        else
        {
            /* Only clear the bit if the nested-guest is also not intercepting the MSR read.*/
            if (!(pVCpu->cpum.GstCtx.hwvirt.svm.abMsrBitmap[offMsrpm] & RT_BIT(uMsrpmBit)))
                *pbMsrBitmap &= ~RT_BIT(uMsrpmBit);
            else
                Assert(*pbMsrBitmap & RT_BIT(uMsrpmBit));
        }
#endif
    }

    if (enmWrite == SVMMSREXIT_INTERCEPT_WRITE)
        *pbMsrBitmap |= RT_BIT(uMsrpmBit + 1);
    else
    {
        if (!fInNestedGuestMode)
            *pbMsrBitmap &= ~RT_BIT(uMsrpmBit + 1);
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        else
        {
            /* Only clear the bit if the nested-guest is also not intercepting the MSR write.*/
            if (!(pVCpu->cpum.GstCtx.hwvirt.svm.abMsrBitmap[offMsrpm] & RT_BIT(uMsrpmBit + 1)))
                *pbMsrBitmap &= ~RT_BIT(uMsrpmBit + 1);
            else
                Assert(*pbMsrBitmap & RT_BIT(uMsrpmBit + 1));
        }
#endif
    }
}


/**
 * Sets up AMD-V for the specified VM.
 * This function is only called once per-VM during initalization.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR0DECL(int) SVMR0SetupVM(PVMCC pVM)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    AssertReturn(pVM, VERR_INVALID_PARAMETER);

    /*
     * Validate and copy over some parameters.
     */
    AssertReturn(pVM->hm.s.svm.fSupported, VERR_INCOMPATIBLE_CONFIG);
    bool const fNestedPaging = pVM->hm.s.fNestedPagingCfg;
    AssertReturn(!fNestedPaging || (g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_NESTED_PAGING), VERR_INCOMPATIBLE_CONFIG);
    pVM->hmr0.s.fNestedPaging = fNestedPaging;
    pVM->hmr0.s.fAllow64BitGuests = pVM->hm.s.fAllow64BitGuestsCfg;

    /*
     * Determin some configuration parameters.
     */
    bool const fPauseFilter          = RT_BOOL(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_PAUSE_FILTER);
    bool const fPauseFilterThreshold = RT_BOOL(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_PAUSE_FILTER_THRESHOLD);
    bool const fUsePauseFilter       = fPauseFilter && pVM->hm.s.svm.cPauseFilter;

    bool const fLbrVirt              = RT_BOOL(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_LBR_VIRT);
    bool const fUseLbrVirt           = fLbrVirt && pVM->hm.s.svm.fLbrVirt; /** @todo IEM implementation etc. */

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    bool const fVirtVmsaveVmload     = RT_BOOL(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_VIRT_VMSAVE_VMLOAD);
    bool const fUseVirtVmsaveVmload  = fVirtVmsaveVmload && pVM->hm.s.svm.fVirtVmsaveVmload && fNestedPaging;

    bool const fVGif                 = RT_BOOL(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_VGIF);
    bool const fUseVGif              = fVGif && pVM->hm.s.svm.fVGif;
#endif

    PVMCPUCC     pVCpu0 = VMCC_GET_CPU_0(pVM);
    PSVMVMCB     pVmcb0 = pVCpu0->hmr0.s.svm.pVmcb;
    AssertMsgReturn(RT_VALID_PTR(pVmcb0), ("Invalid pVmcb (%p) for vcpu[0]\n", pVmcb0), VERR_SVM_INVALID_PVMCB);
    PSVMVMCBCTRL pVmcbCtrl0 = &pVmcb0->ctrl;

    /* Always trap #AC for reasons of security. */
    pVmcbCtrl0->u32InterceptXcpt |= RT_BIT_32(X86_XCPT_AC);

    /* Always trap #DB for reasons of security. */
    pVmcbCtrl0->u32InterceptXcpt |= RT_BIT_32(X86_XCPT_DB);

    /* Trap exceptions unconditionally (debug purposes). */
#ifdef HMSVM_ALWAYS_TRAP_PF
    pVmcbCtrl0->u32InterceptXcpt |= RT_BIT_32(X86_XCPT_PF);
#endif
#ifdef HMSVM_ALWAYS_TRAP_ALL_XCPTS
    /* If you add any exceptions here, make sure to update hmR0SvmHandleExit(). */
    pVmcbCtrl0->u32InterceptXcpt |= RT_BIT_32(X86_XCPT_BP)
                                 | RT_BIT_32(X86_XCPT_DE)
                                 | RT_BIT_32(X86_XCPT_NM)
                                 | RT_BIT_32(X86_XCPT_UD)
                                 | RT_BIT_32(X86_XCPT_NP)
                                 | RT_BIT_32(X86_XCPT_SS)
                                 | RT_BIT_32(X86_XCPT_GP)
                                 | RT_BIT_32(X86_XCPT_PF)
                                 | RT_BIT_32(X86_XCPT_MF)
                                 ;
#endif

    /* Apply the exceptions intercepts needed by the GIM provider. */
    if (pVCpu0->hm.s.fGIMTrapXcptUD || pVCpu0->hm.s.svm.fEmulateLongModeSysEnterExit)
        pVmcbCtrl0->u32InterceptXcpt |= RT_BIT(X86_XCPT_UD);

    /* Apply the exceptions intercepts needed by the GCM fixers. */
    if (pVCpu0->hm.s.fGCMTrapXcptDE)
        pVmcbCtrl0->u32InterceptXcpt |= RT_BIT(X86_XCPT_DE);

    /* The mesa 3d driver hack needs #GP. */
    if (pVCpu0->hm.s.fTrapXcptGpForLovelyMesaDrv)
        pVmcbCtrl0->u32InterceptXcpt |= RT_BIT(X86_XCPT_GP);

    /* Set up unconditional intercepts and conditions. */
    pVmcbCtrl0->u64InterceptCtrl = HMSVM_MANDATORY_GUEST_CTRL_INTERCEPTS
                                 | SVM_CTRL_INTERCEPT_VMMCALL
                                 | SVM_CTRL_INTERCEPT_VMSAVE
                                 | SVM_CTRL_INTERCEPT_VMLOAD
                                 | SVM_CTRL_INTERCEPT_CLGI
                                 | SVM_CTRL_INTERCEPT_STGI;

#ifdef HMSVM_ALWAYS_TRAP_TASK_SWITCH
    pVmcbCtrl0->u64InterceptCtrl |= SVM_CTRL_INTERCEPT_TASK_SWITCH;
#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (pVCpu0->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fSvm)
    {
        /* Virtualized VMSAVE/VMLOAD. */
        if (fUseVirtVmsaveVmload)
        {
            pVmcbCtrl0->LbrVirt.n.u1VirtVmsaveVmload = 1;
            pVmcbCtrl0->u64InterceptCtrl &= ~(  SVM_CTRL_INTERCEPT_VMSAVE
                                              | SVM_CTRL_INTERCEPT_VMLOAD);
        }
        else
            Assert(!pVmcbCtrl0->LbrVirt.n.u1VirtVmsaveVmload);

        /* Virtual GIF. */
        if (fUseVGif)
        {
            pVmcbCtrl0->IntCtrl.n.u1VGifEnable = 1;
            pVmcbCtrl0->u64InterceptCtrl &= ~(  SVM_CTRL_INTERCEPT_CLGI
                                              | SVM_CTRL_INTERCEPT_STGI);
        }
        else
            Assert(!pVmcbCtrl0->IntCtrl.n.u1VGifEnable);
    }
    else
#endif
    {
        Assert(!pVCpu0->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fSvm);
        Assert(!pVmcbCtrl0->LbrVirt.n.u1VirtVmsaveVmload);
        Assert(!pVmcbCtrl0->IntCtrl.n.u1VGifEnable);
    }

    /* CR4 writes must always be intercepted for tracking PGM mode changes and
       AVX (for XCR0 syncing during worlds switching). */
    pVmcbCtrl0->u16InterceptWrCRx = RT_BIT(4);

    /* Intercept all DRx reads and writes by default. Changed later on. */
    pVmcbCtrl0->u16InterceptRdDRx = 0xffff;
    pVmcbCtrl0->u16InterceptWrDRx = 0xffff;

    /* Virtualize masking of INTR interrupts. (reads/writes from/to CR8 go to the V_TPR register) */
    pVmcbCtrl0->IntCtrl.n.u1VIntrMasking = 1;

    /* Ignore the priority in the virtual TPR. This is necessary for delivering PIC style (ExtInt) interrupts
       and we currently deliver both PIC and APIC interrupts alike, see hmR0SvmEvaluatePendingEvent() */
    pVmcbCtrl0->IntCtrl.n.u1IgnoreTPR = 1;

    /* Set the IO permission bitmap physical addresses. */
    pVmcbCtrl0->u64IOPMPhysAddr = g_HCPhysIOBitmap;

    /* LBR virtualization. */
    pVmcbCtrl0->LbrVirt.n.u1LbrVirt = fUseLbrVirt;

    /* The host ASID MBZ, for the guest start with 1. */
    pVmcbCtrl0->TLBCtrl.n.u32ASID = 1;

    /* Setup Nested Paging. This doesn't change throughout the execution time of the VM. */
    pVmcbCtrl0->NestedPagingCtrl.n.u1NestedPaging = fNestedPaging;

    /* Without Nested Paging, we need additionally intercepts. */
    if (!fNestedPaging)
    {
        /* CR3 reads/writes must be intercepted; our shadow values differ from the guest values. */
        pVmcbCtrl0->u16InterceptRdCRx |= RT_BIT(3);
        pVmcbCtrl0->u16InterceptWrCRx |= RT_BIT(3);

        /* Intercept INVLPG and task switches (may change CR3, EFLAGS, LDT). */
        pVmcbCtrl0->u64InterceptCtrl |= SVM_CTRL_INTERCEPT_INVLPG
                                     |  SVM_CTRL_INTERCEPT_TASK_SWITCH;

        /* Page faults must be intercepted to implement shadow paging. */
        pVmcbCtrl0->u32InterceptXcpt |= RT_BIT(X86_XCPT_PF);
    }

    /* Workaround for missing OS/2 TLB flush, see ticketref:20625. */
    if (pVM->hm.s.fMissingOS2TlbFlushWorkaround)
        pVmcbCtrl0->u64InterceptCtrl |= SVM_CTRL_INTERCEPT_TR_WRITES;

    /* Setup Pause Filter for guest pause-loop (spinlock) exiting. */
    if (fUsePauseFilter)
    {
        Assert(pVM->hm.s.svm.cPauseFilter > 0);
        pVmcbCtrl0->u16PauseFilterCount = pVM->hm.s.svm.cPauseFilter;
        if (fPauseFilterThreshold)
            pVmcbCtrl0->u16PauseFilterThreshold = pVM->hm.s.svm.cPauseFilterThresholdTicks;
        pVmcbCtrl0->u64InterceptCtrl |= SVM_CTRL_INTERCEPT_PAUSE;
    }

    /*
     * Setup the MSR permission bitmap.
     * The following MSRs are saved/restored automatically during the world-switch.
     * Don't intercept guest read/write accesses to these MSRs.
     */
    uint8_t *pbMsrBitmap0 = (uint8_t *)pVCpu0->hmr0.s.svm.pvMsrBitmap;
    hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_K8_LSTAR,          SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
    hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_K8_CSTAR,          SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
    hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_K6_STAR,           SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
    hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_K8_SF_MASK,        SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
    hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_K8_FS_BASE,        SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
    hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_K8_GS_BASE,        SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
    hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_K8_KERNEL_GS_BASE, SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
    if (!pVCpu0->hm.s.svm.fEmulateLongModeSysEnterExit)
    {
        hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_IA32_SYSENTER_CS,  SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
        hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_IA32_SYSENTER_ESP, SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
        hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_IA32_SYSENTER_EIP, SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
    }
    else
    {
        hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_IA32_SYSENTER_CS,  SVMMSREXIT_INTERCEPT_READ, SVMMSREXIT_INTERCEPT_WRITE);
        hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_IA32_SYSENTER_ESP, SVMMSREXIT_INTERCEPT_READ, SVMMSREXIT_INTERCEPT_WRITE);
        hmR0SvmSetMsrPermission(pVCpu0, pbMsrBitmap0, MSR_IA32_SYSENTER_EIP, SVMMSREXIT_INTERCEPT_READ, SVMMSREXIT_INTERCEPT_WRITE);
    }
    pVmcbCtrl0->u64MSRPMPhysAddr = pVCpu0->hmr0.s.svm.HCPhysMsrBitmap;

    /* Initially all VMCB clean bits MBZ indicating that everything should be loaded from the VMCB in memory. */
    Assert(pVmcbCtrl0->u32VmcbCleanBits == 0);

    for (VMCPUID idCpu = 1; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPUCC     pVCpuCur = VMCC_GET_CPU(pVM, idCpu);
        PSVMVMCB     pVmcbCur = pVCpuCur->hmr0.s.svm.pVmcb;
        AssertMsgReturn(RT_VALID_PTR(pVmcbCur), ("Invalid pVmcb (%p) for vcpu[%u]\n", pVmcbCur, idCpu), VERR_SVM_INVALID_PVMCB);
        PSVMVMCBCTRL pVmcbCtrlCur = &pVmcbCur->ctrl;

        /* Copy the VMCB control area. */
        memcpy(pVmcbCtrlCur, pVmcbCtrl0, sizeof(*pVmcbCtrlCur));

        /* Copy the MSR bitmap and setup the VCPU-specific host physical address. */
        uint8_t *pbMsrBitmapCur = (uint8_t *)pVCpuCur->hmr0.s.svm.pvMsrBitmap;
        memcpy(pbMsrBitmapCur, pbMsrBitmap0, SVM_MSRPM_PAGES << X86_PAGE_4K_SHIFT);
        pVmcbCtrlCur->u64MSRPMPhysAddr = pVCpuCur->hmr0.s.svm.HCPhysMsrBitmap;

        /* Initially all VMCB clean bits MBZ indicating that everything should be loaded from the VMCB in memory. */
        Assert(pVmcbCtrlCur->u32VmcbCleanBits == 0);

        /* Verify our assumption that GIM providers trap #UD uniformly across VCPUs initially. */
        Assert(pVCpuCur->hm.s.fGIMTrapXcptUD == pVCpu0->hm.s.fGIMTrapXcptUD);
        /* Same for GCM, #DE trapping should be uniform across VCPUs. */
        Assert(pVCpuCur->hm.s.fGCMTrapXcptDE == pVCpu0->hm.s.fGCMTrapXcptDE);
    }

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    LogRel(("HM: fUsePauseFilter=%RTbool fUseLbrVirt=%RTbool fUseVGif=%RTbool fUseVirtVmsaveVmload=%RTbool\n", fUsePauseFilter,
            fUseLbrVirt, fUseVGif, fUseVirtVmsaveVmload));
#else
    LogRel(("HM: fUsePauseFilter=%RTbool fUseLbrVirt=%RTbool\n", fUsePauseFilter, fUseLbrVirt));
#endif
    return VINF_SUCCESS;
}


/**
 * Gets a pointer to the currently active guest (or nested-guest) VMCB.
 *
 * @returns Pointer to the current context VMCB.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
DECLINLINE(PSVMVMCB) hmR0SvmGetCurrentVmcb(PVMCPUCC pVCpu)
{
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx))
        return &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb;
#endif
    return pVCpu->hmr0.s.svm.pVmcb;
}


/**
 * Gets a pointer to the nested-guest VMCB cache.
 *
 * @returns Pointer to the nested-guest VMCB cache.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
DECLINLINE(PSVMNESTEDVMCBCACHE) hmR0SvmGetNestedVmcbCache(PVMCPUCC pVCpu)
{
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    Assert(pVCpu->hm.s.svm.NstGstVmcbCache.fCacheValid);
    return &pVCpu->hm.s.svm.NstGstVmcbCache;
#else
    RT_NOREF(pVCpu);
    return NULL;
#endif
}


/**
 * Invalidates a guest page by guest virtual address.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCVirt      Guest virtual address of the page to invalidate.
 */
VMMR0DECL(int) SVMR0InvalidatePage(PVMCPUCC pVCpu, RTGCPTR GCVirt)
{
    Assert(pVCpu->CTX_SUFF(pVM)->hm.s.svm.fSupported);

    bool const fFlushPending = VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_TLB_FLUSH) || pVCpu->CTX_SUFF(pVM)->hmr0.s.svm.fAlwaysFlushTLB;

    /* Skip it if a TLB flush is already pending. */
    if (!fFlushPending)
    {
        Log4Func(("%#RGv\n", GCVirt));

        PSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
        AssertMsgReturn(pVmcb, ("Invalid pVmcb!\n"), VERR_SVM_INVALID_PVMCB);

        SVMR0InvlpgA(GCVirt, pVmcb->ctrl.TLBCtrl.n.u32ASID);
        STAM_COUNTER_INC(&pVCpu->hm.s.StatFlushTlbInvlpgVirt);
    }
    return VINF_SUCCESS;
}


/**
 * Flushes the appropriate tagged-TLB entries.
 *
 * @param   pHostCpu    The HM physical-CPU structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 */
static void hmR0SvmFlushTaggedTlb(PHMPHYSCPU pHostCpu, PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    /*
     * Force a TLB flush for the first world switch if the current CPU differs from the one
     * we ran on last. This can happen both for start & resume due to long jumps back to
     * ring-3.
     *
     * We also force a TLB flush every time when executing a nested-guest VCPU as there is no
     * correlation between it and the physical CPU.
     *
     * If the TLB flush count changed, another VM (VCPU rather) has hit the ASID limit while
     * flushing the TLB, so we cannot reuse the ASIDs without flushing.
     */
    bool fNewAsid = false;
    Assert(pHostCpu->idCpu != NIL_RTCPUID);
    if (   pVCpu->hmr0.s.idLastCpu   != pHostCpu->idCpu
        || pVCpu->hmr0.s.cTlbFlushes != pHostCpu->cTlbFlushes
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        || CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx)
#endif
        )
    {
        STAM_COUNTER_INC(&pVCpu->hm.s.StatFlushTlbWorldSwitch);
        pVCpu->hmr0.s.fForceTLBFlush = true;
        fNewAsid = true;
    }

    /* Set TLB flush state as checked until we return from the world switch. */
    ASMAtomicUoWriteBool(&pVCpu->hm.s.fCheckedTLBFlush, true);

    /* Check for explicit TLB flushes. */
    if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_TLB_FLUSH))
    {
        pVCpu->hmr0.s.fForceTLBFlush = true;
        STAM_COUNTER_INC(&pVCpu->hm.s.StatFlushTlb);
    }

    /*
     * If the AMD CPU erratum 170, We need to flush the entire TLB for each world switch. Sad.
     * This Host CPU requirement takes precedence.
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (pVM->hmr0.s.svm.fAlwaysFlushTLB)
    {
        pHostCpu->uCurrentAsid           = 1;
        pVCpu->hmr0.s.uCurrentAsid       = 1;
        pVCpu->hmr0.s.cTlbFlushes        = pHostCpu->cTlbFlushes;
        pVCpu->hmr0.s.idLastCpu          = pHostCpu->idCpu;
        pVmcb->ctrl.TLBCtrl.n.u8TLBFlush = SVM_TLB_FLUSH_ENTIRE;

        /* Clear the VMCB Clean Bit for NP while flushing the TLB. See @bugref{7152}. */
        pVmcb->ctrl.u32VmcbCleanBits    &= ~HMSVM_VMCB_CLEAN_NP;
    }
    else
    {
        pVmcb->ctrl.TLBCtrl.n.u8TLBFlush = SVM_TLB_FLUSH_NOTHING;
        if (pVCpu->hmr0.s.fForceTLBFlush)
        {
            /* Clear the VMCB Clean Bit for NP while flushing the TLB. See @bugref{7152}. */
            pVmcb->ctrl.u32VmcbCleanBits    &= ~HMSVM_VMCB_CLEAN_NP;

            if (fNewAsid)
            {
                ++pHostCpu->uCurrentAsid;

                bool fHitASIDLimit = false;
                if (pHostCpu->uCurrentAsid >= g_uHmMaxAsid)
                {
                    pHostCpu->uCurrentAsid = 1;      /* Wraparound at 1; host uses 0 */
                    pHostCpu->cTlbFlushes++;         /* All VCPUs that run on this host CPU must use a new ASID. */
                    fHitASIDLimit          = true;
                }

                if (   fHitASIDLimit
                    || pHostCpu->fFlushAsidBeforeUse)
                {
                    pVmcb->ctrl.TLBCtrl.n.u8TLBFlush = SVM_TLB_FLUSH_ENTIRE;
                    pHostCpu->fFlushAsidBeforeUse = false;
                }

                pVCpu->hmr0.s.uCurrentAsid = pHostCpu->uCurrentAsid;
                pVCpu->hmr0.s.idLastCpu    = pHostCpu->idCpu;
                pVCpu->hmr0.s.cTlbFlushes  = pHostCpu->cTlbFlushes;
            }
            else
            {
                if (g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_FLUSH_BY_ASID)
                    pVmcb->ctrl.TLBCtrl.n.u8TLBFlush = SVM_TLB_FLUSH_SINGLE_CONTEXT;
                else
                    pVmcb->ctrl.TLBCtrl.n.u8TLBFlush = SVM_TLB_FLUSH_ENTIRE;
            }

            pVCpu->hmr0.s.fForceTLBFlush = false;
        }
    }

    /* Update VMCB with the ASID. */
    if (pVmcb->ctrl.TLBCtrl.n.u32ASID != pVCpu->hmr0.s.uCurrentAsid)
    {
        pVmcb->ctrl.TLBCtrl.n.u32ASID = pVCpu->hmr0.s.uCurrentAsid;
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_ASID;
    }

    AssertMsg(pVCpu->hmr0.s.idLastCpu == pHostCpu->idCpu,
              ("vcpu idLastCpu=%u hostcpu idCpu=%u\n", pVCpu->hmr0.s.idLastCpu, pHostCpu->idCpu));
    AssertMsg(pVCpu->hmr0.s.cTlbFlushes == pHostCpu->cTlbFlushes,
              ("Flush count mismatch for cpu %u (%u vs %u)\n", pHostCpu->idCpu, pVCpu->hmr0.s.cTlbFlushes, pHostCpu->cTlbFlushes));
    AssertMsg(pHostCpu->uCurrentAsid >= 1 && pHostCpu->uCurrentAsid < g_uHmMaxAsid,
              ("cpu%d uCurrentAsid = %x\n", pHostCpu->idCpu, pHostCpu->uCurrentAsid));
    AssertMsg(pVCpu->hmr0.s.uCurrentAsid >= 1 && pVCpu->hmr0.s.uCurrentAsid < g_uHmMaxAsid,
              ("cpu%d VM uCurrentAsid = %x\n", pHostCpu->idCpu, pVCpu->hmr0.s.uCurrentAsid));

#ifdef VBOX_WITH_STATISTICS
    if (pVmcb->ctrl.TLBCtrl.n.u8TLBFlush == SVM_TLB_FLUSH_NOTHING)
        STAM_COUNTER_INC(&pVCpu->hm.s.StatNoFlushTlbWorldSwitch);
    else if (   pVmcb->ctrl.TLBCtrl.n.u8TLBFlush == SVM_TLB_FLUSH_SINGLE_CONTEXT
             || pVmcb->ctrl.TLBCtrl.n.u8TLBFlush == SVM_TLB_FLUSH_SINGLE_CONTEXT_RETAIN_GLOBALS)
    {
        STAM_COUNTER_INC(&pVCpu->hm.s.StatFlushAsid);
    }
    else
    {
        Assert(pVmcb->ctrl.TLBCtrl.n.u8TLBFlush == SVM_TLB_FLUSH_ENTIRE);
        STAM_COUNTER_INC(&pVCpu->hm.s.StatFlushEntire);
    }
#endif
}


/**
 * Sets an exception intercept in the specified VMCB.
 *
 * @param   pVmcb       Pointer to the VM control block.
 * @param   uXcpt       The exception (X86_XCPT_*).
 */
DECLINLINE(void) hmR0SvmSetXcptIntercept(PSVMVMCB pVmcb, uint8_t uXcpt)
{
    if (!(pVmcb->ctrl.u32InterceptXcpt & RT_BIT(uXcpt)))
    {
        pVmcb->ctrl.u32InterceptXcpt |= RT_BIT(uXcpt);
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;
    }
}


/**
 * Clears an exception intercept in the specified VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 * @param   uXcpt       The exception (X86_XCPT_*).
 *
 * @remarks This takes into account if we're executing a nested-guest and only
 *          removes the exception intercept if both the guest -and- nested-guest
 *          are not intercepting it.
 */
DECLINLINE(void) hmR0SvmClearXcptIntercept(PVMCPUCC pVCpu, PSVMVMCB pVmcb, uint8_t uXcpt)
{
    Assert(uXcpt != X86_XCPT_DB);
    Assert(uXcpt != X86_XCPT_AC);
    Assert(uXcpt != X86_XCPT_GP);
#ifndef HMSVM_ALWAYS_TRAP_ALL_XCPTS
    if (pVmcb->ctrl.u32InterceptXcpt & RT_BIT(uXcpt))
    {
        bool fRemove = true;
# ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        /* Only remove the intercept if the nested-guest is also not intercepting it! */
        PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
        if (CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
        {
            PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = hmR0SvmGetNestedVmcbCache(pVCpu);
            fRemove = !(pVmcbNstGstCache->u32InterceptXcpt & RT_BIT(uXcpt));
        }
# else
        RT_NOREF(pVCpu);
# endif
        if (fRemove)
        {
            pVmcb->ctrl.u32InterceptXcpt &= ~RT_BIT(uXcpt);
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;
        }
    }
#else
    RT_NOREF3(pVCpu, pVmcb, uXcpt);
#endif
}


/**
 * Sets a control intercept in the specified VMCB.
 *
 * @param   pVmcb           Pointer to the VM control block.
 * @param   fCtrlIntercept  The control intercept (SVM_CTRL_INTERCEPT_*).
 */
DECLINLINE(void) hmR0SvmSetCtrlIntercept(PSVMVMCB pVmcb, uint64_t fCtrlIntercept)
{
    if (!(pVmcb->ctrl.u64InterceptCtrl & fCtrlIntercept))
    {
        pVmcb->ctrl.u64InterceptCtrl |= fCtrlIntercept;
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;
    }
}


/**
 * Clears a control intercept in the specified VMCB.
 *
 * @returns @c true if the intercept is still set, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmcb           Pointer to the VM control block.
 * @param   fCtrlIntercept  The control intercept (SVM_CTRL_INTERCEPT_*).
 *
 * @remarks This takes into account if we're executing a nested-guest and only
 *          removes the control intercept if both the guest -and- nested-guest
 *          are not intercepting it.
 */
static bool hmR0SvmClearCtrlIntercept(PVMCPUCC pVCpu, PSVMVMCB pVmcb, uint64_t fCtrlIntercept)
{
    if (pVmcb->ctrl.u64InterceptCtrl & fCtrlIntercept)
    {
        bool fRemove = true;
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        /* Only remove the control intercept if the nested-guest is also not intercepting it! */
        if (CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx))
        {
            PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = hmR0SvmGetNestedVmcbCache(pVCpu);
            fRemove = !(pVmcbNstGstCache->u64InterceptCtrl & fCtrlIntercept);
        }
#else
        RT_NOREF(pVCpu);
#endif
        if (fRemove)
        {
            pVmcb->ctrl.u64InterceptCtrl &= ~fCtrlIntercept;
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;
        }
    }

    return RT_BOOL(pVmcb->ctrl.u64InterceptCtrl & fCtrlIntercept);
}


/**
 * Exports the guest (or nested-guest) CR0 into the VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks This assumes we always pre-load the guest FPU.
 * @remarks No-long-jump zone!!!
 */
static void hmR0SvmExportGuestCR0(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    PCPUMCTX       pCtx = &pVCpu->cpum.GstCtx;
    uint64_t const uGuestCr0  = pCtx->cr0;
    uint64_t       uShadowCr0 = uGuestCr0;

    /* Always enable caching. */
    uShadowCr0 &= ~(X86_CR0_CD | X86_CR0_NW);

    /* When Nested Paging is not available use shadow page tables and intercept #PFs (latter done in SVMR0SetupVM()). */
    if (!pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging)
    {
        uShadowCr0 |= X86_CR0_PG      /* Use shadow page tables. */
                   |  X86_CR0_WP;     /* Guest CPL 0 writes to its read-only pages should cause a #PF #VMEXIT. */
    }

    /*
     * Use the #MF style of legacy-FPU error reporting for now. Although AMD-V has MSRs that
     * lets us isolate the host from it, IEM/REM still needs work to emulate it properly,
     * see @bugref{7243#c103}.
     */
    if (!(uGuestCr0 & X86_CR0_NE))
    {
        uShadowCr0 |= X86_CR0_NE;
        hmR0SvmSetXcptIntercept(pVmcb, X86_XCPT_MF);
    }
    else
        hmR0SvmClearXcptIntercept(pVCpu, pVmcb, X86_XCPT_MF);

    /*
     * If the shadow and guest CR0 are identical we can avoid intercepting CR0 reads.
     *
     * CR0 writes still needs interception as PGM requires tracking paging mode changes,
     * see @bugref{6944}.
     *
     * We also don't ever want to honor weird things like cache disable from the guest.
     * However, we can avoid intercepting changes to the TS & MP bits by clearing the CR0
     * write intercept below and keeping SVM_CTRL_INTERCEPT_CR0_SEL_WRITE instead.
     */
    if (uShadowCr0 == uGuestCr0)
    {
        if (!CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
        {
            pVmcb->ctrl.u16InterceptRdCRx &= ~RT_BIT(0);
            pVmcb->ctrl.u16InterceptWrCRx &= ~RT_BIT(0);
            Assert(pVmcb->ctrl.u64InterceptCtrl & SVM_CTRL_INTERCEPT_CR0_SEL_WRITE);
        }
        else
        {
            /* If the nested-hypervisor intercepts CR0 reads/writes, we need to continue intercepting them. */
            PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = hmR0SvmGetNestedVmcbCache(pVCpu);
            pVmcb->ctrl.u16InterceptRdCRx = (pVmcb->ctrl.u16InterceptRdCRx       & ~RT_BIT(0))
                                          | (pVmcbNstGstCache->u16InterceptRdCRx &  RT_BIT(0));
            pVmcb->ctrl.u16InterceptWrCRx = (pVmcb->ctrl.u16InterceptWrCRx       & ~RT_BIT(0))
                                          | (pVmcbNstGstCache->u16InterceptWrCRx &  RT_BIT(0));
        }
    }
    else
    {
        pVmcb->ctrl.u16InterceptRdCRx |= RT_BIT(0);
        pVmcb->ctrl.u16InterceptWrCRx |= RT_BIT(0);
    }
    pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;

    Assert(!RT_HI_U32(uShadowCr0));
    if (pVmcb->guest.u64CR0 != uShadowCr0)
    {
        pVmcb->guest.u64CR0 = uShadowCr0;
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_CRX_EFER;
    }
}


/**
 * Exports the guest (or nested-guest) CR3 into the VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 */
static void hmR0SvmExportGuestCR3(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    PVMCC    pVM  = pVCpu->CTX_SUFF(pVM);
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    if (pVM->hmr0.s.fNestedPaging)
    {
        pVmcb->ctrl.u64NestedPagingCR3 = PGMGetHyperCR3(pVCpu);
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_NP;
        pVmcb->guest.u64CR3 = pCtx->cr3;
        Assert(pVmcb->ctrl.u64NestedPagingCR3);
    }
    else
        pVmcb->guest.u64CR3 = PGMGetHyperCR3(pVCpu);

    pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_CRX_EFER;
}


/**
 * Exports the guest (or nested-guest) CR4 into the VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 */
static int hmR0SvmExportGuestCR4(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    uint64_t uShadowCr4 = pCtx->cr4;
    if (!pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging)
    {
        switch (pVCpu->hm.s.enmShadowMode)
        {
            case PGMMODE_REAL:
            case PGMMODE_PROTECTED:     /* Protected mode, no paging. */
                return VERR_PGM_UNSUPPORTED_SHADOW_PAGING_MODE;

            case PGMMODE_32_BIT:        /* 32-bit paging. */
                uShadowCr4 &= ~X86_CR4_PAE;
                break;

            case PGMMODE_PAE:           /* PAE paging. */
            case PGMMODE_PAE_NX:        /* PAE paging with NX enabled. */
                /** Must use PAE paging as we could use physical memory > 4 GB */
                uShadowCr4 |= X86_CR4_PAE;
                break;

            case PGMMODE_AMD64:         /* 64-bit AMD paging (long mode). */
            case PGMMODE_AMD64_NX:      /* 64-bit AMD paging (long mode) with NX enabled. */
#ifdef VBOX_WITH_64_BITS_GUESTS
                break;
#else
                return VERR_PGM_UNSUPPORTED_SHADOW_PAGING_MODE;
#endif

            default:                    /* shut up gcc */
                return VERR_PGM_UNSUPPORTED_SHADOW_PAGING_MODE;
        }
    }

    /* Whether to save/load/restore XCR0 during world switch depends on CR4.OSXSAVE and host+guest XCR0. */
    bool const fLoadSaveGuestXcr0 = (pCtx->cr4 & X86_CR4_OSXSAVE) && pCtx->aXcr[0] != ASMGetXcr0();
    if (fLoadSaveGuestXcr0 != pVCpu->hmr0.s.fLoadSaveGuestXcr0)
    {
        pVCpu->hmr0.s.fLoadSaveGuestXcr0 = fLoadSaveGuestXcr0;
        hmR0SvmUpdateVmRunFunction(pVCpu);
    }

    /* Avoid intercepting CR4 reads if the guest and shadow CR4 values are identical. */
    if (uShadowCr4 == pCtx->cr4)
    {
        if (!CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
            pVmcb->ctrl.u16InterceptRdCRx &= ~RT_BIT(4);
        else
        {
            /* If the nested-hypervisor intercepts CR4 reads, we need to continue intercepting them. */
            PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = hmR0SvmGetNestedVmcbCache(pVCpu);
            pVmcb->ctrl.u16InterceptRdCRx = (pVmcb->ctrl.u16InterceptRdCRx       & ~RT_BIT(4))
                                          | (pVmcbNstGstCache->u16InterceptRdCRx &  RT_BIT(4));
        }
    }
    else
        pVmcb->ctrl.u16InterceptRdCRx |= RT_BIT(4);

    /* CR4 writes are always intercepted (both guest, nested-guest) for tracking
       PGM mode changes and AVX (for XCR0 syncing during worlds switching). */
    Assert(pVmcb->ctrl.u16InterceptWrCRx & RT_BIT(4));

    /* Update VMCB with the shadow CR4 the appropriate VMCB clean bits. */
    Assert(!RT_HI_U32(uShadowCr4));
    pVmcb->guest.u64CR4 = uShadowCr4;
    pVmcb->ctrl.u32VmcbCleanBits &= ~(HMSVM_VMCB_CLEAN_CRX_EFER | HMSVM_VMCB_CLEAN_INTERCEPTS);

    return VINF_SUCCESS;
}


/**
 * Exports the guest (or nested-guest) control registers into the VMCB.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 */
static int hmR0SvmExportGuestControlRegs(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_CR_MASK)
    {
        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_CR0)
            hmR0SvmExportGuestCR0(pVCpu, pVmcb);

        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_CR2)
        {
            pVmcb->guest.u64CR2 = pVCpu->cpum.GstCtx.cr2;
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_CR2;
        }

        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_CR3)
            hmR0SvmExportGuestCR3(pVCpu, pVmcb);

        /* CR4 re-loading is ASSUMED to be done everytime we get in from ring-3! (XCR0) */
        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_CR4)
        {
            int rc = hmR0SvmExportGuestCR4(pVCpu, pVmcb);
            if (RT_FAILURE(rc))
                return rc;
        }

        pVCpu->hm.s.fCtxChanged &= ~HM_CHANGED_GUEST_CR_MASK;
    }
    return VINF_SUCCESS;
}


/**
 * Exports the guest (or nested-guest) segment registers into the VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 */
static void hmR0SvmExportGuestSegmentRegs(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;

    /* Guest segment registers. */
    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_SREG_MASK)
    {
        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_CS)
            HMSVM_SEG_REG_COPY_TO_VMCB(pCtx, &pVmcb->guest, CS, cs);

        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_SS)
        {
            HMSVM_SEG_REG_COPY_TO_VMCB(pCtx, &pVmcb->guest, SS, ss);
            pVmcb->guest.u8CPL = pCtx->ss.Attr.n.u2Dpl;
        }

        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_DS)
            HMSVM_SEG_REG_COPY_TO_VMCB(pCtx, &pVmcb->guest, DS, ds);

        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_ES)
            HMSVM_SEG_REG_COPY_TO_VMCB(pCtx, &pVmcb->guest, ES, es);

        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_FS)
            HMSVM_SEG_REG_COPY_TO_VMCB(pCtx, &pVmcb->guest, FS, fs);

        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_GS)
            HMSVM_SEG_REG_COPY_TO_VMCB(pCtx, &pVmcb->guest, GS, gs);

        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_SEG;
    }

    /* Guest TR. */
    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_TR)
        HMSVM_SEG_REG_COPY_TO_VMCB(pCtx, &pVmcb->guest, TR, tr);

    /* Guest LDTR. */
    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_LDTR)
        HMSVM_SEG_REG_COPY_TO_VMCB(pCtx, &pVmcb->guest, LDTR, ldtr);

    /* Guest GDTR. */
    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_GDTR)
    {
        pVmcb->guest.GDTR.u32Limit = pCtx->gdtr.cbGdt;
        pVmcb->guest.GDTR.u64Base  = pCtx->gdtr.pGdt;
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_DT;
    }

    /* Guest IDTR. */
    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_IDTR)
    {
        pVmcb->guest.IDTR.u32Limit = pCtx->idtr.cbIdt;
        pVmcb->guest.IDTR.u64Base  = pCtx->idtr.pIdt;
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_DT;
    }

    pVCpu->hm.s.fCtxChanged &= ~(  HM_CHANGED_GUEST_SREG_MASK
                                 | HM_CHANGED_GUEST_TABLE_MASK);
}


/**
 * Exports the guest (or nested-guest) MSRs into the VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 */
static void hmR0SvmExportGuestMsrs(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;

    /* Guest Sysenter MSRs. */
    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_SYSENTER_MSR_MASK)
    {
        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_SYSENTER_CS_MSR)
            pVmcb->guest.u64SysEnterCS  = pCtx->SysEnter.cs;

        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_SYSENTER_EIP_MSR)
            pVmcb->guest.u64SysEnterEIP = pCtx->SysEnter.eip;

        if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_SYSENTER_ESP_MSR)
            pVmcb->guest.u64SysEnterESP = pCtx->SysEnter.esp;
    }

    /*
     * Guest EFER MSR.
     * AMD-V requires guest EFER.SVME to be set. Weird.
     * See AMD spec. 15.5.1 "Basic Operation" | "Canonicalization and Consistency Checks".
     */
    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_EFER_MSR)
    {
        pVmcb->guest.u64EFER = pCtx->msrEFER | MSR_K6_EFER_SVME;
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_CRX_EFER;
    }

    /* If the guest isn't in 64-bit mode, clear MSR_K6_LME bit, otherwise SVM expects amd64 shadow paging. */
    if (   !CPUMIsGuestInLongModeEx(pCtx)
        && (pCtx->msrEFER & MSR_K6_EFER_LME))
    {
        pVmcb->guest.u64EFER &= ~MSR_K6_EFER_LME;
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_CRX_EFER;
    }

    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_SYSCALL_MSRS)
    {
        pVmcb->guest.u64STAR   = pCtx->msrSTAR;
        pVmcb->guest.u64LSTAR  = pCtx->msrLSTAR;
        pVmcb->guest.u64CSTAR  = pCtx->msrCSTAR;
        pVmcb->guest.u64SFMASK = pCtx->msrSFMASK;
    }

    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_KERNEL_GS_BASE)
        pVmcb->guest.u64KernelGSBase = pCtx->msrKERNELGSBASE;

    pVCpu->hm.s.fCtxChanged &= ~(  HM_CHANGED_GUEST_SYSENTER_MSR_MASK
                                 | HM_CHANGED_GUEST_EFER_MSR
                                 | HM_CHANGED_GUEST_SYSCALL_MSRS
                                 | HM_CHANGED_GUEST_KERNEL_GS_BASE);

    /*
     * Setup the PAT MSR (applicable for Nested Paging only).
     *
     * The default value should be MSR_IA32_CR_PAT_INIT_VAL, but we treat all guest memory
     * as WB, so choose type 6 for all PAT slots, see @bugref{9634}.
     *
     * While guests can modify and see the modified values through the shadow values,
     * we shall not honor any guest modifications of this MSR to ensure caching is always
     * enabled similar to how we clear CR0.CD and NW bits.
     *
     * For nested-guests this needs to always be set as well, see @bugref{7243#c109}.
     */
    pVmcb->guest.u64PAT = UINT64_C(0x0006060606060606);

    /* Enable the last branch record bit if LBR virtualization is enabled. */
    if (pVmcb->ctrl.LbrVirt.n.u1LbrVirt)
        pVmcb->guest.u64DBGCTL = MSR_IA32_DEBUGCTL_LBR;
}


/**
 * Exports the guest (or nested-guest) debug state into the VMCB and programs
 * the necessary intercepts accordingly.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 * @remarks Requires EFLAGS to be up-to-date in the VMCB!
 */
static void hmR0SvmExportSharedDebugState(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;

    /** @todo Figure out stepping with nested-guest. */
    if (CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
    {
        /*
         * We don't want to always intercept DRx read/writes for nested-guests as it causes
         * problems when the nested hypervisor isn't intercepting them, see @bugref{10080}.
         * Instead, they are strictly only requested when the nested hypervisor intercepts
         * them -- handled while merging VMCB controls.
         *
         * If neither the outer nor the nested-hypervisor is intercepting DRx read/writes,
         * then the nested-guest debug state should be actively loaded on the host so that
         * nested-guest reads/writes its own debug registers without causing VM-exits.
         */
        if (   (   pVmcb->ctrl.u16InterceptRdDRx != 0xffff
                || pVmcb->ctrl.u16InterceptWrDRx != 0xffff)
            && !CPUMIsGuestDebugStateActive(pVCpu))
        {
           CPUMR0LoadGuestDebugState(pVCpu, true /* include DR6 */);
           STAM_COUNTER_INC(&pVCpu->hm.s.StatDRxArmed);
           Assert(!CPUMIsHyperDebugStateActive(pVCpu));
           Assert(CPUMIsGuestDebugStateActive(pVCpu));
        }

        pVmcb->guest.u64DR6 = pCtx->dr[6];
        pVmcb->guest.u64DR7 = pCtx->dr[7];
        return;
    }

    /*
     * Anyone single stepping on the host side? If so, we'll have to use the
     * trap flag in the guest EFLAGS since AMD-V doesn't have a trap flag on
     * the VMM level like the VT-x implementations does.
     */
    bool       fInterceptMovDRx = false;
    bool const fStepping = pVCpu->hm.s.fSingleInstruction || DBGFIsStepping(pVCpu);
    if (fStepping)
    {
        pVCpu->hmr0.s.fClearTrapFlag = true;
        pVmcb->guest.u64RFlags |= X86_EFL_TF;
        fInterceptMovDRx = true; /* Need clean DR6, no guest mess. */
    }

    if (   fStepping
        || (CPUMGetHyperDR7(pVCpu) & X86_DR7_ENABLED_MASK))
    {
        /*
         * Use the combined guest and host DRx values found in the hypervisor
         * register set because the debugger has breakpoints active or someone
         * is single stepping on the host side.
         *
         * Note! DBGF expects a clean DR6 state before executing guest code.
         */
        if (!CPUMIsHyperDebugStateActive(pVCpu))
        {
            CPUMR0LoadHyperDebugState(pVCpu, false /* include DR6 */);
            Assert(!CPUMIsGuestDebugStateActive(pVCpu));
            Assert(CPUMIsHyperDebugStateActive(pVCpu));
        }

        /* Update DR6 & DR7. (The other DRx values are handled by CPUM one way or the other.) */
        if (   pVmcb->guest.u64DR6 != X86_DR6_INIT_VAL
            || pVmcb->guest.u64DR7 != CPUMGetHyperDR7(pVCpu))
        {
            pVmcb->guest.u64DR7 = CPUMGetHyperDR7(pVCpu);
            pVmcb->guest.u64DR6 = X86_DR6_INIT_VAL;
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_DRX;
        }

        /** @todo If we cared, we could optimize to allow the guest to read registers
         *        with the same values. */
        fInterceptMovDRx = true;
        pVCpu->hmr0.s.fUsingHyperDR7 = true;
        Log5(("hmR0SvmExportSharedDebugState: Loaded hyper DRx\n"));
    }
    else
    {
        /*
         * Update DR6, DR7 with the guest values if necessary.
         */
        if (   pVmcb->guest.u64DR7 != pCtx->dr[7]
            || pVmcb->guest.u64DR6 != pCtx->dr[6])
        {
            pVmcb->guest.u64DR7 = pCtx->dr[7];
            pVmcb->guest.u64DR6 = pCtx->dr[6];
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_DRX;
        }
        pVCpu->hmr0.s.fUsingHyperDR7 = false;

        /*
         * If the guest has enabled debug registers, we need to load them prior to
         * executing guest code so they'll trigger at the right time.
         */
        if (pCtx->dr[7] & (X86_DR7_ENABLED_MASK | X86_DR7_GD)) /** @todo Why GD? */
        {
            if (!CPUMIsGuestDebugStateActive(pVCpu))
            {
                CPUMR0LoadGuestDebugState(pVCpu, false /* include DR6 */);
                STAM_COUNTER_INC(&pVCpu->hm.s.StatDRxArmed);
                Assert(!CPUMIsHyperDebugStateActive(pVCpu));
                Assert(CPUMIsGuestDebugStateActive(pVCpu));
            }
            Log5(("hmR0SvmExportSharedDebugState: Loaded guest DRx\n"));
        }
        /*
         * If no debugging enabled, we'll lazy load DR0-3. We don't need to
         * intercept #DB as DR6 is updated in the VMCB.
         *
         * Note! If we cared and dared, we could skip intercepting \#DB here.
         *       However, \#DB shouldn't be performance critical, so we'll play safe
         *       and keep the code similar to the VT-x code and always intercept it.
         */
        else if (!CPUMIsGuestDebugStateActive(pVCpu))
            fInterceptMovDRx = true;
    }

    Assert(pVmcb->ctrl.u32InterceptXcpt & RT_BIT_32(X86_XCPT_DB));
    if (fInterceptMovDRx)
    {
        if (   pVmcb->ctrl.u16InterceptRdDRx != 0xffff
            || pVmcb->ctrl.u16InterceptWrDRx != 0xffff)
        {
            pVmcb->ctrl.u16InterceptRdDRx = 0xffff;
            pVmcb->ctrl.u16InterceptWrDRx = 0xffff;
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;
        }
    }
    else
    {
        if (   pVmcb->ctrl.u16InterceptRdDRx
            || pVmcb->ctrl.u16InterceptWrDRx)
        {
            pVmcb->ctrl.u16InterceptRdDRx = 0;
            pVmcb->ctrl.u16InterceptWrDRx = 0;
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;
        }
    }
    Log4Func(("DR6=%#RX64 DR7=%#RX64\n", pCtx->dr[6], pCtx->dr[7]));
}

/**
 * Exports the hardware virtualization state into the nested-guest
 * VMCB.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pVmcb   Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 */
static void hmR0SvmExportGuestHwvirtState(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_HWVIRT)
    {
        if (pVmcb->ctrl.IntCtrl.n.u1VGifEnable)
        {
            PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
            PCVMCC    pVM  = pVCpu->CTX_SUFF(pVM);

            HMSVM_ASSERT_NOT_IN_NESTED_GUEST(pCtx);                       /* Nested VGIF is not supported yet. */
            Assert(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_VGIF);    /* Physical hardware supports VGIF. */
            Assert(HMIsSvmVGifActive(pVM));                               /* Outer VM has enabled VGIF. */
            NOREF(pVM);

            pVmcb->ctrl.IntCtrl.n.u1VGif = CPUMGetGuestGif(pCtx);
        }

        /*
         * Ensure the nested-guest pause-filter counters don't exceed the outer guest values esp.
         * since SVM doesn't have a preemption timer.
         *
         * We do this here rather than in hmR0SvmSetupVmcbNested() as we may have been executing the
         * nested-guest in IEM incl. PAUSE instructions which would update the pause-filter counters
         * and may continue execution in SVM R0 without a nested-guest #VMEXIT in between.
         */
        PVMCC          pVM = pVCpu->CTX_SUFF(pVM);
        PSVMVMCBCTRL   pVmcbCtrl = &pVmcb->ctrl;
        uint16_t const uGuestPauseFilterCount     = pVM->hm.s.svm.cPauseFilter;
        uint16_t const uGuestPauseFilterThreshold = pVM->hm.s.svm.cPauseFilterThresholdTicks;
        if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, &pVCpu->cpum.GstCtx, SVM_CTRL_INTERCEPT_PAUSE))
        {
            PCCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
            pVmcbCtrl->u16PauseFilterCount     = RT_MIN(pCtx->hwvirt.svm.cPauseFilter, uGuestPauseFilterCount);
            pVmcbCtrl->u16PauseFilterThreshold = RT_MIN(pCtx->hwvirt.svm.cPauseFilterThreshold, uGuestPauseFilterThreshold);
        }
        else
        {
            /** @todo r=ramshankar: We can turn these assignments into assertions. */
            pVmcbCtrl->u16PauseFilterCount     = uGuestPauseFilterCount;
            pVmcbCtrl->u16PauseFilterThreshold = uGuestPauseFilterThreshold;
        }
        pVmcbCtrl->u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;

        pVCpu->hm.s.fCtxChanged &= ~HM_CHANGED_GUEST_HWVIRT;
    }
}


/**
 * Exports the guest APIC TPR state into the VMCB.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 */
static int hmR0SvmExportGuestApicTpr(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    HMSVM_ASSERT_NOT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx);

    if (ASMAtomicUoReadU64(&pVCpu->hm.s.fCtxChanged) & HM_CHANGED_GUEST_APIC_TPR)
    {
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        if (   PDMHasApic(pVM)
            && APICIsEnabled(pVCpu))
        {
            bool    fPendingIntr;
            uint8_t u8Tpr;
            int rc = APICGetTpr(pVCpu, &u8Tpr, &fPendingIntr, NULL /* pu8PendingIrq */);
            AssertRCReturn(rc, rc);

            /* Assume that we need to trap all TPR accesses and thus need not check on
               every #VMEXIT if we should update the TPR. */
            Assert(pVmcb->ctrl.IntCtrl.n.u1VIntrMasking);
            pVCpu->hmr0.s.svm.fSyncVTpr = false;

            if (!pVM->hm.s.fTprPatchingActive)
            {
                /* Bits 3-0 of the VTPR field correspond to bits 7-4 of the TPR (which is the Task-Priority Class). */
                pVmcb->ctrl.IntCtrl.n.u8VTPR = (u8Tpr >> 4);

                /* If there are interrupts pending, intercept CR8 writes to evaluate ASAP if we
                   can deliver the interrupt to the guest. */
                if (fPendingIntr)
                    pVmcb->ctrl.u16InterceptWrCRx |= RT_BIT(8);
                else
                {
                    pVmcb->ctrl.u16InterceptWrCRx &= ~RT_BIT(8);
                    pVCpu->hmr0.s.svm.fSyncVTpr = true;
                }

                pVmcb->ctrl.u32VmcbCleanBits &= ~(HMSVM_VMCB_CLEAN_INTERCEPTS | HMSVM_VMCB_CLEAN_INT_CTRL);
            }
            else
            {
                /* 32-bit guests uses LSTAR MSR for patching guest code which touches the TPR. */
                pVmcb->guest.u64LSTAR = u8Tpr;
                uint8_t *pbMsrBitmap = (uint8_t *)pVCpu->hmr0.s.svm.pvMsrBitmap;

                /* If there are interrupts pending, intercept LSTAR writes, otherwise don't intercept reads or writes. */
                if (fPendingIntr)
                    hmR0SvmSetMsrPermission(pVCpu, pbMsrBitmap, MSR_K8_LSTAR, SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_INTERCEPT_WRITE);
                else
                {
                    hmR0SvmSetMsrPermission(pVCpu, pbMsrBitmap, MSR_K8_LSTAR, SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
                    pVCpu->hmr0.s.svm.fSyncVTpr = true;
                }
                pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_IOPM_MSRPM;
            }
        }
        ASMAtomicUoAndU64(&pVCpu->hm.s.fCtxChanged, ~HM_CHANGED_GUEST_APIC_TPR);
    }
    return VINF_SUCCESS;
}


/**
 * Sets up the exception interrupts required for guest execution in the VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 */
static void hmR0SvmExportGuestXcptIntercepts(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    HMSVM_ASSERT_NOT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx);

    /* If we modify intercepts from here, please check & adjust hmR0SvmMergeVmcbCtrlsNested() if required. */
    if (ASMAtomicUoReadU64(&pVCpu->hm.s.fCtxChanged) & HM_CHANGED_SVM_XCPT_INTERCEPTS)
    {
        /* Trap #UD for GIM provider (e.g. for hypercalls). */
        if (pVCpu->hm.s.fGIMTrapXcptUD || pVCpu->hm.s.svm.fEmulateLongModeSysEnterExit)
            hmR0SvmSetXcptIntercept(pVmcb, X86_XCPT_UD);
        else
            hmR0SvmClearXcptIntercept(pVCpu, pVmcb, X86_XCPT_UD);

        /* Trap #BP for INT3 debug breakpoints set by the VM debugger. */
        if (pVCpu->CTX_SUFF(pVM)->dbgf.ro.cEnabledInt3Breakpoints)
            hmR0SvmSetXcptIntercept(pVmcb, X86_XCPT_BP);
        else
            hmR0SvmClearXcptIntercept(pVCpu, pVmcb, X86_XCPT_BP);

        /* The remaining intercepts are handled elsewhere, e.g. in hmR0SvmExportGuestCR0(). */
        ASMAtomicUoAndU64(&pVCpu->hm.s.fCtxChanged, ~HM_CHANGED_SVM_XCPT_INTERCEPTS);
    }
}


#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
/**
 * Merges guest and nested-guest intercepts for executing the nested-guest using
 * hardware-assisted SVM.
 *
 * This merges the guest and nested-guest intercepts in a way that if the outer
 * guest intercept is set we need to intercept it in the nested-guest as
 * well.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmcbNstGst     Pointer to the nested-guest VM control block.
 */
static void hmR0SvmMergeVmcbCtrlsNested(PVMCPUCC pVCpu)
{
    PVMCC        pVM             = pVCpu->CTX_SUFF(pVM);
    PCSVMVMCB    pVmcb           = pVCpu->hmr0.s.svm.pVmcb;
    PSVMVMCB     pVmcbNstGst     = &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb;
    PSVMVMCBCTRL pVmcbNstGstCtrl = &pVmcbNstGst->ctrl;

    /* Merge the guest's CR intercepts into the nested-guest VMCB. */
    pVmcbNstGstCtrl->u16InterceptRdCRx |= pVmcb->ctrl.u16InterceptRdCRx;
    pVmcbNstGstCtrl->u16InterceptWrCRx |= pVmcb->ctrl.u16InterceptWrCRx;

    /* Always intercept CR4 writes for tracking PGM mode changes and AVX (for
       XCR0 syncing during worlds switching). */
    pVmcbNstGstCtrl->u16InterceptWrCRx |= RT_BIT(4);

    /* Without nested paging, intercept CR3 reads and writes as we load shadow page tables. */
    if (!pVM->hmr0.s.fNestedPaging)
    {
        pVmcbNstGstCtrl->u16InterceptRdCRx |= RT_BIT(3);
        pVmcbNstGstCtrl->u16InterceptWrCRx |= RT_BIT(3);
    }

    /* Merge the guest's DR intercepts into the nested-guest VMCB. */
    pVmcbNstGstCtrl->u16InterceptRdDRx |= pVmcb->ctrl.u16InterceptRdDRx;
    pVmcbNstGstCtrl->u16InterceptWrDRx |= pVmcb->ctrl.u16InterceptWrDRx;

    /*
     * Merge the guest's exception intercepts into the nested-guest VMCB.
     *
     * - #UD: Exclude these as the outer guest's GIM hypercalls are not applicable
     *   while executing the nested-guest.
     *
     * - #BP: Exclude breakpoints set by the VM debugger for the outer guest. This can
     *   be tweaked later depending on how we wish to implement breakpoints.
     *
     * - #GP: Exclude these as it's the inner VMMs problem to get vmsvga 3d drivers
     *   loaded into their guests, not ours.
     *
     * Warning!! This ASSUMES we only intercept \#UD for hypercall purposes and \#BP
     * for VM debugger breakpoints, see hmR0SvmExportGuestXcptIntercepts().
     */
#ifndef HMSVM_ALWAYS_TRAP_ALL_XCPTS
    pVmcbNstGstCtrl->u32InterceptXcpt  |= pVmcb->ctrl.u32InterceptXcpt
                                       & ~(  RT_BIT(X86_XCPT_UD)
                                           | RT_BIT(X86_XCPT_BP)
                                           | (pVCpu->hm.s.fTrapXcptGpForLovelyMesaDrv ? RT_BIT(X86_XCPT_GP) : 0));
#else
    pVmcbNstGstCtrl->u32InterceptXcpt  |= pVmcb->ctrl.u32InterceptXcpt;
#endif

    /*
     * Adjust intercepts while executing the nested-guest that differ from the
     * outer guest intercepts.
     *
     * - VINTR: Exclude the outer guest intercept as we don't need to cause VINTR #VMEXITs
     *   that belong to the nested-guest to the outer guest.
     *
     * - VMMCALL: Exclude the outer guest intercept as when it's also not intercepted by
     *   the nested-guest, the physical CPU raises a \#UD exception as expected.
     */
    pVmcbNstGstCtrl->u64InterceptCtrl  |= (pVmcb->ctrl.u64InterceptCtrl & ~(  SVM_CTRL_INTERCEPT_VINTR
                                                                            | SVM_CTRL_INTERCEPT_VMMCALL))
                                       |  HMSVM_MANDATORY_GUEST_CTRL_INTERCEPTS;

    Assert(   (pVmcbNstGstCtrl->u64InterceptCtrl & HMSVM_MANDATORY_GUEST_CTRL_INTERCEPTS)
           == HMSVM_MANDATORY_GUEST_CTRL_INTERCEPTS);

    /* Finally, update the VMCB clean bits. */
    pVmcbNstGstCtrl->u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;
}
#endif


/**
 * Enters the AMD-V session.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0DECL(int) SVMR0Enter(PVMCPUCC pVCpu)
{
    AssertPtr(pVCpu);
    Assert(pVCpu->CTX_SUFF(pVM)->hm.s.svm.fSupported);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    LogFlowFunc(("pVCpu=%p\n", pVCpu));
    Assert((pVCpu->hm.s.fCtxChanged & (HM_CHANGED_HOST_CONTEXT | HM_CHANGED_SVM_HOST_GUEST_SHARED_STATE))
                                   == (HM_CHANGED_HOST_CONTEXT | HM_CHANGED_SVM_HOST_GUEST_SHARED_STATE));

    pVCpu->hmr0.s.fLeaveDone = false;
    return VINF_SUCCESS;
}


/**
 * Thread-context callback for AMD-V.
 *
 * This is used together with RTThreadCtxHookCreate() on platforms which
 * supports it, and directly from VMMR0EmtPrepareForBlocking() and
 * VMMR0EmtResumeAfterBlocking() on platforms which don't.
 *
 * @param   enmEvent        The thread-context event.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fGlobalInit     Whether global VT-x/AMD-V init. is used.
 * @thread  EMT(pVCpu)
 */
VMMR0DECL(void) SVMR0ThreadCtxCallback(RTTHREADCTXEVENT enmEvent, PVMCPUCC pVCpu, bool fGlobalInit)
{
    NOREF(fGlobalInit);

    switch (enmEvent)
    {
        case RTTHREADCTXEVENT_OUT:
        {
            Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
            VMCPU_ASSERT_EMT(pVCpu);

            /* No longjmps (log-flush, locks) in this fragile context. */
            VMMRZCallRing3Disable(pVCpu);

            if (!pVCpu->hmr0.s.fLeaveDone)
            {
                hmR0SvmLeave(pVCpu, false /* fImportState */);
                pVCpu->hmr0.s.fLeaveDone = true;
            }

            /* Leave HM context, takes care of local init (term). */
            int rc = HMR0LeaveCpu(pVCpu);
            AssertRC(rc); NOREF(rc);

            /* Restore longjmp state. */
            VMMRZCallRing3Enable(pVCpu);
            STAM_REL_COUNTER_INC(&pVCpu->hm.s.StatSwitchPreempt);
            break;
        }

        case RTTHREADCTXEVENT_IN:
        {
            Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
            VMCPU_ASSERT_EMT(pVCpu);

            /* No longjmps (log-flush, locks) in this fragile context. */
            VMMRZCallRing3Disable(pVCpu);

            /*
             * Initialize the bare minimum state required for HM. This takes care of
             * initializing AMD-V if necessary (onlined CPUs, local init etc.)
             */
            int rc = hmR0EnterCpu(pVCpu);
            AssertRC(rc); NOREF(rc);
            Assert(   (pVCpu->hm.s.fCtxChanged & (HM_CHANGED_HOST_CONTEXT | HM_CHANGED_SVM_HOST_GUEST_SHARED_STATE))
                   ==                            (HM_CHANGED_HOST_CONTEXT | HM_CHANGED_SVM_HOST_GUEST_SHARED_STATE));

            pVCpu->hmr0.s.fLeaveDone = false;

            /* Restore longjmp state. */
            VMMRZCallRing3Enable(pVCpu);
            break;
        }

        default:
            break;
    }
}


/**
 * Saves the host state.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @remarks No-long-jump zone!!!
 */
VMMR0DECL(int) SVMR0ExportHostState(PVMCPUCC pVCpu)
{
    NOREF(pVCpu);

    /* Nothing to do here. AMD-V does this for us automatically during the world-switch. */
    ASMAtomicUoAndU64(&pVCpu->hm.s.fCtxChanged, ~HM_CHANGED_HOST_CONTEXT);
    return VINF_SUCCESS;
}


/**
 * Exports the guest or nested-guest state from the virtual-CPU context into the
 * VMCB.
 *
 * Also sets up the appropriate VMRUN function to execute guest or nested-guest
 * code based on the virtual-CPU mode.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM-transient structure.
 *
 * @remarks No-long-jump zone!!!
 */
static int hmR0SvmExportGuestState(PVMCPUCC pVCpu, PCSVMTRANSIENT pSvmTransient)
{
    STAM_PROFILE_ADV_START(&pVCpu->hm.s.StatExportGuestState, x);

    PSVMVMCB  pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    PCCPUMCTX pCtx  = &pVCpu->cpum.GstCtx;
    Assert(pVmcb);

    pVmcb->guest.u64RIP    = pCtx->rip;
    pVmcb->guest.u64RSP    = pCtx->rsp;
    pVmcb->guest.u64RFlags = pCtx->eflags.u;
    pVmcb->guest.u64RAX    = pCtx->rax;

    bool const fIsNestedGuest = pSvmTransient->fIsNestedGuest;
    RTCCUINTREG const fEFlags = ASMIntDisableFlags();

    int rc = hmR0SvmExportGuestControlRegs(pVCpu, pVmcb);
    AssertRCReturnStmt(rc, ASMSetFlags(fEFlags), rc);
    hmR0SvmExportGuestSegmentRegs(pVCpu, pVmcb);
    hmR0SvmExportGuestMsrs(pVCpu, pVmcb);
    hmR0SvmExportGuestHwvirtState(pVCpu, pVmcb);

    ASMSetFlags(fEFlags);

    if (!fIsNestedGuest)
    {
        /* hmR0SvmExportGuestApicTpr() must be called -after- hmR0SvmExportGuestMsrs() as we
           otherwise we would overwrite the LSTAR MSR that we use for TPR patching. */
        hmR0SvmExportGuestApicTpr(pVCpu, pVmcb);
        hmR0SvmExportGuestXcptIntercepts(pVCpu, pVmcb);
    }

    /* Clear any bits that may be set but exported unconditionally or unused/reserved bits. */
    uint64_t fUnusedMask = HM_CHANGED_GUEST_RIP
                         | HM_CHANGED_GUEST_RFLAGS
                         | HM_CHANGED_GUEST_GPRS_MASK
                         | HM_CHANGED_GUEST_X87
                         | HM_CHANGED_GUEST_SSE_AVX
                         | HM_CHANGED_GUEST_OTHER_XSAVE
                         | HM_CHANGED_GUEST_XCRx
                         | HM_CHANGED_GUEST_TSC_AUX
                         | HM_CHANGED_GUEST_OTHER_MSRS;
    if (fIsNestedGuest)
        fUnusedMask |= HM_CHANGED_SVM_XCPT_INTERCEPTS
                    |  HM_CHANGED_GUEST_APIC_TPR;

    ASMAtomicUoAndU64(&pVCpu->hm.s.fCtxChanged, ~(  fUnusedMask
                                                  | (HM_CHANGED_KEEPER_STATE_MASK & ~HM_CHANGED_SVM_MASK)));

#ifdef VBOX_STRICT
    /*
     * All of the guest-CPU state and SVM keeper bits should be exported here by now,
     * except for the host-context and/or shared host-guest context bits.
     */
    uint64_t const fCtxChanged = ASMAtomicUoReadU64(&pVCpu->hm.s.fCtxChanged);
    AssertMsg(!(fCtxChanged & (HM_CHANGED_ALL_GUEST & ~HM_CHANGED_SVM_HOST_GUEST_SHARED_STATE)),
              ("fCtxChanged=%#RX64\n", fCtxChanged));

    /*
     * If we need to log state that isn't always imported, we'll need to import them here.
     * See hmR0SvmPostRunGuest() for which part of the state is imported uncondtionally.
     */
    hmR0SvmLogState(pVCpu, pVmcb, "hmR0SvmExportGuestState", 0 /* fFlags */, 0 /* uVerbose */);
#endif

    STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatExportGuestState, x);
    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM

/**
 * Merges the guest and nested-guest MSR permission bitmap.
 *
 * If the guest is intercepting an MSR we need to intercept it regardless of
 * whether the nested-guest is intercepting it or not.
 *
 * @param   pHostCpu    The HM physical-CPU structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @remarks No-long-jmp zone!!!
 */
DECLINLINE(void) hmR0SvmMergeMsrpmNested(PHMPHYSCPU pHostCpu, PVMCPUCC pVCpu)
{
    uint64_t const *pu64GstMsrpm    = (uint64_t const *)pVCpu->hmr0.s.svm.pvMsrBitmap;
    uint64_t const *pu64NstGstMsrpm = (uint64_t const *)&pVCpu->cpum.GstCtx.hwvirt.svm.abMsrBitmap[0];
    uint64_t       *pu64DstMsrpm    = (uint64_t *)pHostCpu->n.svm.pvNstGstMsrpm;

    /* MSRPM bytes from offset 0x1800 are reserved, so we stop merging there. */
    uint32_t const offRsvdQwords = 0x1800 >> 3;
    for (uint32_t i = 0; i < offRsvdQwords; i++)
        pu64DstMsrpm[i] = pu64NstGstMsrpm[i] | pu64GstMsrpm[i];
}


/**
 * Caches the nested-guest VMCB fields before we modify them for execution using
 * hardware-assisted SVM.
 *
 * @returns true if the VMCB was previously already cached, false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure.
 *
 * @sa      HMNotifySvmNstGstVmexit.
 */
static bool hmR0SvmCacheVmcbNested(PVMCPUCC pVCpu)
{
    /*
     * Cache the nested-guest programmed VMCB fields if we have not cached it yet.
     * Otherwise we risk re-caching the values we may have modified, see @bugref{7243#c44}.
     *
     * Nested-paging CR3 is not saved back into the VMCB on #VMEXIT, hence no need to
     * cache and restore it, see AMD spec. 15.25.4 "Nested Paging and VMRUN/#VMEXIT".
     */
    PSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    bool const fWasCached = pVmcbNstGstCache->fCacheValid;
    if (!fWasCached)
    {
        PCSVMVMCB      pVmcbNstGst    = &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb;
        PCSVMVMCBCTRL pVmcbNstGstCtrl = &pVmcbNstGst->ctrl;
        pVmcbNstGstCache->u16InterceptRdCRx       = pVmcbNstGstCtrl->u16InterceptRdCRx;
        pVmcbNstGstCache->u16InterceptWrCRx       = pVmcbNstGstCtrl->u16InterceptWrCRx;
        pVmcbNstGstCache->u16InterceptRdDRx       = pVmcbNstGstCtrl->u16InterceptRdDRx;
        pVmcbNstGstCache->u16InterceptWrDRx       = pVmcbNstGstCtrl->u16InterceptWrDRx;
        pVmcbNstGstCache->u16PauseFilterThreshold = pVmcbNstGstCtrl->u16PauseFilterThreshold;
        pVmcbNstGstCache->u16PauseFilterCount     = pVmcbNstGstCtrl->u16PauseFilterCount;
        pVmcbNstGstCache->u32InterceptXcpt        = pVmcbNstGstCtrl->u32InterceptXcpt;
        pVmcbNstGstCache->u64InterceptCtrl        = pVmcbNstGstCtrl->u64InterceptCtrl;
        pVmcbNstGstCache->u64TSCOffset            = pVmcbNstGstCtrl->u64TSCOffset;
        pVmcbNstGstCache->fVIntrMasking           = pVmcbNstGstCtrl->IntCtrl.n.u1VIntrMasking;
        pVmcbNstGstCache->fNestedPaging           = pVmcbNstGstCtrl->NestedPagingCtrl.n.u1NestedPaging;
        pVmcbNstGstCache->fLbrVirt                = pVmcbNstGstCtrl->LbrVirt.n.u1LbrVirt;
        pVmcbNstGstCache->fCacheValid             = true;
        Log4Func(("Cached VMCB fields\n"));
    }

    return fWasCached;
}


/**
 * Sets up the nested-guest VMCB for execution using hardware-assisted SVM.
 *
 * This is done the first time we enter nested-guest execution using SVM R0
 * until the nested-guest \#VMEXIT (not to be confused with physical CPU
 * \#VMEXITs which may or may not cause a corresponding nested-guest \#VMEXIT).
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static void hmR0SvmSetupVmcbNested(PVMCPUCC pVCpu)
{
    PSVMVMCB     pVmcbNstGst     = &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb;
    PSVMVMCBCTRL pVmcbNstGstCtrl = &pVmcbNstGst->ctrl;

    HMSVM_ASSERT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx);

    /*
     * First cache the nested-guest VMCB fields we may potentially modify.
     */
    bool const fVmcbCached = hmR0SvmCacheVmcbNested(pVCpu);
    if (!fVmcbCached)
    {
        /*
         * The IOPM of the nested-guest can be ignored because the the guest always
         * intercepts all IO port accesses. Thus, we'll swap to the guest IOPM rather
         * than the nested-guest IOPM and swap the field back on the #VMEXIT.
         */
        pVmcbNstGstCtrl->u64IOPMPhysAddr = g_HCPhysIOBitmap;

        /*
         * Use the same nested-paging as the outer guest. We can't dynamically switch off
         * nested-paging suddenly while executing a VM (see assertion at the end of
         * Trap0eHandler() in PGMAllBth.h).
         */
        pVmcbNstGstCtrl->NestedPagingCtrl.n.u1NestedPaging = pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging;

        /* Always enable V_INTR_MASKING as we do not want to allow access to the physical APIC TPR. */
        pVmcbNstGstCtrl->IntCtrl.n.u1VIntrMasking = 1;

        /*
         * Turn off TPR syncing on #VMEXIT for nested-guests as CR8 intercepts are subject
         * to the nested-guest intercepts and we always run with V_INTR_MASKING.
         */
        pVCpu->hmr0.s.svm.fSyncVTpr = false;

# ifdef DEBUG_ramshankar
        /* For debugging purposes - copy the LBR info. from outer guest VMCB. */
        pVmcbNstGstCtrl->LbrVirt.n.u1LbrVirt = pVmcb->ctrl.LbrVirt.n.u1LbrVirt;
# endif

        /*
         * If we don't expose Virtualized-VMSAVE/VMLOAD feature to the outer guest, we
         * need to intercept VMSAVE/VMLOAD instructions executed by the nested-guest.
         */
        if (!pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fSvmVirtVmsaveVmload)
            pVmcbNstGstCtrl->u64InterceptCtrl |= SVM_CTRL_INTERCEPT_VMSAVE
                                              |  SVM_CTRL_INTERCEPT_VMLOAD;

        /*
         * If we don't expose Virtual GIF feature to the outer guest, we need to intercept
         * CLGI/STGI instructions executed by the nested-guest.
         */
        if (!pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fSvmVGif)
            pVmcbNstGstCtrl->u64InterceptCtrl |= SVM_CTRL_INTERCEPT_CLGI
                                              |  SVM_CTRL_INTERCEPT_STGI;

        /* Merge the guest and nested-guest intercepts. */
        hmR0SvmMergeVmcbCtrlsNested(pVCpu);

        /* Update the VMCB clean bits. */
        pVmcbNstGstCtrl->u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;
    }
    else
    {
        Assert(!pVCpu->hmr0.s.svm.fSyncVTpr);
        Assert(pVmcbNstGstCtrl->u64IOPMPhysAddr == g_HCPhysIOBitmap);
        Assert(RT_BOOL(pVmcbNstGstCtrl->NestedPagingCtrl.n.u1NestedPaging) == pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);
        Assert(pVCpu->CTX_SUFF(pVM)->hm.s.fNestedPagingCfg == pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);
    }
}

#endif /* VBOX_WITH_NESTED_HWVIRT_SVM */

/**
 * Exports the state shared between the host and guest (or nested-guest) into
 * the VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 */
static void hmR0SvmExportSharedState(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(!VMMRZCallRing3IsEnabled(pVCpu));

    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_GUEST_DR_MASK)
        hmR0SvmExportSharedDebugState(pVCpu, pVmcb);

    pVCpu->hm.s.fCtxChanged &= ~HM_CHANGED_GUEST_DR_MASK;
    AssertMsg(!(pVCpu->hm.s.fCtxChanged & HM_CHANGED_SVM_HOST_GUEST_SHARED_STATE),
              ("fCtxChanged=%#RX64\n", pVCpu->hm.s.fCtxChanged));
}


/**
 * Worker for SVMR0ImportStateOnDemand.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fWhat   What to import, CPUMCTX_EXTRN_XXX.
 */
static void hmR0SvmImportGuestState(PVMCPUCC pVCpu, uint64_t fWhat)
{
    STAM_PROFILE_ADV_START(&pVCpu->hm.s.StatImportGuestState, x);

    PCPUMCTX           pCtx       = &pVCpu->cpum.GstCtx;
    PCSVMVMCB          pVmcb      = hmR0SvmGetCurrentVmcb(pVCpu);
    PCSVMVMCBSTATESAVE pVmcbGuest = &pVmcb->guest;
    PCSVMVMCBCTRL      pVmcbCtrl  = &pVmcb->ctrl;

    /*
     * We disable interrupts to make the updating of the state and in particular
     * the fExtrn modification atomic wrt to preemption hooks.
     */
    RTCCUINTREG const fEFlags = ASMIntDisableFlags();

    fWhat &= pCtx->fExtrn;
    if (fWhat)
    {
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        if (fWhat & CPUMCTX_EXTRN_HWVIRT)
        {
            if (pVmcbCtrl->IntCtrl.n.u1VGifEnable)
            {
                Assert(!CPUMIsGuestInSvmNestedHwVirtMode(pCtx));    /* We don't yet support passing VGIF feature to the guest. */
                Assert(HMIsSvmVGifActive(pVCpu->CTX_SUFF(pVM)));    /* VM has configured it. */
                CPUMSetGuestGif(pCtx, pVmcbCtrl->IntCtrl.n.u1VGif);
            }
        }

        if (fWhat & CPUMCTX_EXTRN_HM_SVM_HWVIRT_VIRQ)
        {
            if (  !pVmcbCtrl->IntCtrl.n.u1VIrqPending
                && VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST))
                VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST);
        }
#endif

        if (fWhat & CPUMCTX_EXTRN_INHIBIT_INT)
            CPUMUpdateInterruptShadowEx(pCtx, pVmcbCtrl->IntShadow.n.u1IntShadow, pVmcbGuest->u64RIP);

        if (fWhat & CPUMCTX_EXTRN_RIP)
            pCtx->rip = pVmcbGuest->u64RIP;

        if (fWhat & CPUMCTX_EXTRN_RFLAGS)
        {
            pCtx->eflags.u = pVmcbGuest->u64RFlags;
            if (pVCpu->hmr0.s.fClearTrapFlag)
            {
                pVCpu->hmr0.s.fClearTrapFlag = false;
                pCtx->eflags.Bits.u1TF = 0;
            }
        }

        if (fWhat & CPUMCTX_EXTRN_RSP)
            pCtx->rsp = pVmcbGuest->u64RSP;

        if (fWhat & CPUMCTX_EXTRN_RAX)
            pCtx->rax = pVmcbGuest->u64RAX;

        if (fWhat & CPUMCTX_EXTRN_SREG_MASK)
        {
            if (fWhat & CPUMCTX_EXTRN_CS)
            {
                HMSVM_SEG_REG_COPY_FROM_VMCB(pCtx, pVmcbGuest, CS, cs);
                /* Correct the CS granularity bit. Haven't seen it being wrong in any other register (yet). */
                /** @todo SELM might need to be fixed as it too should not care about the
                 *        granularity bit. See @bugref{6785}. */
                if (   !pCtx->cs.Attr.n.u1Granularity
                    &&  pCtx->cs.Attr.n.u1Present
                    &&  pCtx->cs.u32Limit > UINT32_C(0xfffff))
                {
                    Assert((pCtx->cs.u32Limit & 0xfff) == 0xfff);
                    pCtx->cs.Attr.n.u1Granularity = 1;
                }
                HMSVM_ASSERT_SEG_GRANULARITY(pCtx, cs);
            }
            if (fWhat & CPUMCTX_EXTRN_SS)
            {
                HMSVM_SEG_REG_COPY_FROM_VMCB(pCtx, pVmcbGuest, SS, ss);
                HMSVM_ASSERT_SEG_GRANULARITY(pCtx, ss);
                /*
                 * Sync the hidden SS DPL field. AMD CPUs have a separate CPL field in the
                 * VMCB and uses that and thus it's possible that when the CPL changes during
                 * guest execution that the SS DPL isn't updated by AMD-V. Observed on some
                 * AMD Fusion CPUs with 64-bit guests.
                 *
                 * See AMD spec. 15.5.1 "Basic operation".
                 */
                Assert(!(pVmcbGuest->u8CPL & ~0x3));
                uint8_t const uCpl = pVmcbGuest->u8CPL;
                if (pCtx->ss.Attr.n.u2Dpl != uCpl)
                    pCtx->ss.Attr.n.u2Dpl = uCpl & 0x3;
            }
            if (fWhat & CPUMCTX_EXTRN_DS)
            {
                HMSVM_SEG_REG_COPY_FROM_VMCB(pCtx, pVmcbGuest, DS, ds);
                HMSVM_ASSERT_SEG_GRANULARITY(pCtx, ds);
            }
            if (fWhat & CPUMCTX_EXTRN_ES)
            {
                HMSVM_SEG_REG_COPY_FROM_VMCB(pCtx, pVmcbGuest, ES, es);
                HMSVM_ASSERT_SEG_GRANULARITY(pCtx, es);
            }
            if (fWhat & CPUMCTX_EXTRN_FS)
            {
                HMSVM_SEG_REG_COPY_FROM_VMCB(pCtx, pVmcbGuest, FS, fs);
                HMSVM_ASSERT_SEG_GRANULARITY(pCtx, fs);
            }
            if (fWhat & CPUMCTX_EXTRN_GS)
            {
                HMSVM_SEG_REG_COPY_FROM_VMCB(pCtx, pVmcbGuest, GS, gs);
                HMSVM_ASSERT_SEG_GRANULARITY(pCtx, gs);
            }
        }

        if (fWhat & CPUMCTX_EXTRN_TABLE_MASK)
        {
            if (fWhat & CPUMCTX_EXTRN_TR)
            {
                /*
                 * Fixup TR attributes so it's compatible with Intel. Important when saved-states
                 * are used between Intel and AMD, see @bugref{6208#c39}.
                 * ASSUME that it's normally correct and that we're in 32-bit or 64-bit mode.
                 */
                HMSVM_SEG_REG_COPY_FROM_VMCB(pCtx, pVmcbGuest, TR, tr);
                if (pCtx->tr.Attr.n.u4Type != X86_SEL_TYPE_SYS_386_TSS_BUSY)
                {
                    if (   pCtx->tr.Attr.n.u4Type == X86_SEL_TYPE_SYS_386_TSS_AVAIL
                        || CPUMIsGuestInLongModeEx(pCtx))
                        pCtx->tr.Attr.n.u4Type = X86_SEL_TYPE_SYS_386_TSS_BUSY;
                    else if (pCtx->tr.Attr.n.u4Type == X86_SEL_TYPE_SYS_286_TSS_AVAIL)
                        pCtx->tr.Attr.n.u4Type = X86_SEL_TYPE_SYS_286_TSS_BUSY;
                }
            }

            if (fWhat & CPUMCTX_EXTRN_LDTR)
                HMSVM_SEG_REG_COPY_FROM_VMCB(pCtx, pVmcbGuest, LDTR, ldtr);

            if (fWhat & CPUMCTX_EXTRN_GDTR)
            {
                pCtx->gdtr.cbGdt = pVmcbGuest->GDTR.u32Limit;
                pCtx->gdtr.pGdt  = pVmcbGuest->GDTR.u64Base;
            }

            if (fWhat & CPUMCTX_EXTRN_IDTR)
            {
                pCtx->idtr.cbIdt = pVmcbGuest->IDTR.u32Limit;
                pCtx->idtr.pIdt  = pVmcbGuest->IDTR.u64Base;
            }
        }

        if (fWhat & CPUMCTX_EXTRN_SYSCALL_MSRS)
        {
            pCtx->msrSTAR   = pVmcbGuest->u64STAR;
            pCtx->msrLSTAR  = pVmcbGuest->u64LSTAR;
            pCtx->msrCSTAR  = pVmcbGuest->u64CSTAR;
            pCtx->msrSFMASK = pVmcbGuest->u64SFMASK;
        }

        if (   (fWhat & CPUMCTX_EXTRN_SYSENTER_MSRS)
            && !pVCpu->hm.s.svm.fEmulateLongModeSysEnterExit /* Intercepted. AMD-V would clear the high 32 bits of EIP & ESP. */)
        {
            pCtx->SysEnter.cs  = pVmcbGuest->u64SysEnterCS;
            pCtx->SysEnter.eip = pVmcbGuest->u64SysEnterEIP;
            pCtx->SysEnter.esp = pVmcbGuest->u64SysEnterESP;
        }

        if (fWhat & CPUMCTX_EXTRN_KERNEL_GS_BASE)
            pCtx->msrKERNELGSBASE = pVmcbGuest->u64KernelGSBase;

        if (fWhat & CPUMCTX_EXTRN_DR_MASK)
        {
            if (fWhat & CPUMCTX_EXTRN_DR6)
            {
                if (!pVCpu->hmr0.s.fUsingHyperDR7)
                    pCtx->dr[6] = pVmcbGuest->u64DR6;
                else
                    CPUMSetHyperDR6(pVCpu, pVmcbGuest->u64DR6);
            }

            if (fWhat & CPUMCTX_EXTRN_DR7)
            {
                if (!pVCpu->hmr0.s.fUsingHyperDR7)
                    pCtx->dr[7] = pVmcbGuest->u64DR7;
                else
                    Assert(pVmcbGuest->u64DR7 == CPUMGetHyperDR7(pVCpu));
            }
        }

        if (fWhat & CPUMCTX_EXTRN_CR_MASK)
        {
            if (fWhat & CPUMCTX_EXTRN_CR0)
            {
                /* We intercept changes to all CR0 bits except maybe TS & MP bits. */
                uint64_t const uCr0 = (pCtx->cr0          & ~(X86_CR0_TS | X86_CR0_MP))
                                    | (pVmcbGuest->u64CR0 &  (X86_CR0_TS | X86_CR0_MP));
                VMMRZCallRing3Disable(pVCpu); /* Calls into PGM which has Log statements. */
                CPUMSetGuestCR0(pVCpu, uCr0);
                VMMRZCallRing3Enable(pVCpu);
            }

            if (fWhat & CPUMCTX_EXTRN_CR2)
                pCtx->cr2 = pVmcbGuest->u64CR2;

            if (fWhat & CPUMCTX_EXTRN_CR3)
            {
                if (   pVmcbCtrl->NestedPagingCtrl.n.u1NestedPaging
                    && pCtx->cr3 != pVmcbGuest->u64CR3)
                {
                    CPUMSetGuestCR3(pVCpu, pVmcbGuest->u64CR3);
                    VMCPU_FF_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3);
                }
            }

            /* Changes to CR4 are always intercepted. */
        }

        /* Update fExtrn. */
        pCtx->fExtrn &= ~fWhat;

        /* If everything has been imported, clear the HM keeper bit. */
        if (!(pCtx->fExtrn & HMSVM_CPUMCTX_EXTRN_ALL))
        {
            pCtx->fExtrn &= ~CPUMCTX_EXTRN_KEEPER_HM;
            Assert(!pCtx->fExtrn);
        }
    }
    else
        Assert(!pCtx->fExtrn || (pCtx->fExtrn & HMSVM_CPUMCTX_EXTRN_ALL));

    ASMSetFlags(fEFlags);

    STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatImportGuestState, x);

    /*
     * Honor any pending CR3 updates.
     *
     * Consider this scenario: #VMEXIT -> VMMRZCallRing3Enable() -> do stuff that causes a longjmp
     * -> SVMR0CallRing3Callback() -> VMMRZCallRing3Disable() -> hmR0SvmImportGuestState()
     * -> Sets VMCPU_FF_HM_UPDATE_CR3 pending -> return from the longjmp -> continue with #VMEXIT
     * handling -> hmR0SvmImportGuestState() and here we are.
     *
     * The reason for such complicated handling is because VM-exits that call into PGM expect
     * CR3 to be up-to-date and thus any CR3-saves -before- the VM-exit (longjmp) would've
     * postponed the CR3 update via the force-flag and cleared CR3 from fExtrn. Any SVM R0
     * VM-exit handler that requests CR3 to be saved will end up here and we call PGMUpdateCR3().
     *
     * The longjmp exit path can't check these CR3 force-flags and call code that takes a lock again,
     * and does not process force-flag like regular exits to ring-3 either, we cover for it here.
     */
    if (   VMMRZCallRing3IsEnabled(pVCpu)
        && VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3))
    {
        AssertMsg(pCtx->cr3 == pVmcbGuest->u64CR3, ("cr3=%#RX64 vmcb_cr3=%#RX64\n", pCtx->cr3, pVmcbGuest->u64CR3));
        PGMUpdateCR3(pVCpu, pCtx->cr3);
    }
}


/**
 * Saves the guest (or nested-guest) state from the VMCB into the guest-CPU
 * context.
 *
 * Currently there is no residual state left in the CPU that is not updated in the
 * VMCB.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fWhat   What to import, CPUMCTX_EXTRN_XXX.
 */
VMMR0DECL(int) SVMR0ImportStateOnDemand(PVMCPUCC pVCpu, uint64_t fWhat)
{
    hmR0SvmImportGuestState(pVCpu, fWhat);
    return VINF_SUCCESS;
}


/**
 * Gets SVM \#VMEXIT auxiliary information.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmExitAux     Where to store the auxiliary info.
 */
VMMR0DECL(int) SVMR0GetExitAuxInfo(PVMCPUCC pVCpu, PSVMEXITAUX pSvmExitAux)
{
    PCSVMTRANSIENT pSvmTransient = pVCpu->hmr0.s.svm.pSvmTransient;
    if (RT_LIKELY(pSvmTransient))
    {
        PCSVMVMCB pVmcb = pSvmTransient->pVmcb;
        if (RT_LIKELY(pVmcb))
        {
            pSvmExitAux->u64ExitCode  = pVmcb->ctrl.u64ExitCode;
            pSvmExitAux->u64ExitInfo1 = pVmcb->ctrl.u64ExitInfo1;
            pSvmExitAux->u64ExitInfo2 = pVmcb->ctrl.u64ExitInfo2;
            pSvmExitAux->ExitIntInfo  = pVmcb->ctrl.ExitIntInfo;
            return VINF_SUCCESS;
        }
        return VERR_SVM_IPE_5;
    }
    return VERR_NOT_AVAILABLE;
}


/**
 * Does the necessary state syncing before returning to ring-3 for any reason
 * (longjmp, preemption, voluntary exits to ring-3) from AMD-V.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fImportState    Whether to import the guest state from the VMCB back
 *                          to the guest-CPU context.
 *
 * @remarks No-long-jmp zone!!!
 */
static void hmR0SvmLeave(PVMCPUCC pVCpu, bool fImportState)
{
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(!VMMRZCallRing3IsEnabled(pVCpu));

    /*
     * !!! IMPORTANT !!!
     * If you modify code here, make sure to check whether SVMR0CallRing3Callback() needs to be updated too.
     */

    /* Save the guest state if necessary. */
    if (fImportState)
        hmR0SvmImportGuestState(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);

    /* Restore host FPU state if necessary and resync on next R0 reentry. */
    CPUMR0FpuStateMaybeSaveGuestAndRestoreHost(pVCpu);
    Assert(!CPUMIsGuestFPUStateActive(pVCpu));

    /*
     * Restore host debug registers if necessary and resync on next R0 reentry.
     */
#ifdef VBOX_STRICT
    if (CPUMIsHyperDebugStateActive(pVCpu))
    {
        PSVMVMCB pVmcb = pVCpu->hmr0.s.svm.pVmcb; /** @todo nested-guest. */
        Assert(pVmcb->ctrl.u16InterceptRdDRx == 0xffff);
        Assert(pVmcb->ctrl.u16InterceptWrDRx == 0xffff);
    }
#endif
    CPUMR0DebugStateMaybeSaveGuestAndRestoreHost(pVCpu, false /* save DR6 */);
    Assert(!CPUMIsHyperDebugStateActive(pVCpu));
    Assert(!CPUMIsGuestDebugStateActive(pVCpu));

    STAM_PROFILE_ADV_SET_STOPPED(&pVCpu->hm.s.StatEntry);
    STAM_PROFILE_ADV_SET_STOPPED(&pVCpu->hm.s.StatImportGuestState);
    STAM_PROFILE_ADV_SET_STOPPED(&pVCpu->hm.s.StatExportGuestState);
    STAM_PROFILE_ADV_SET_STOPPED(&pVCpu->hm.s.StatPreExit);
    STAM_PROFILE_ADV_SET_STOPPED(&pVCpu->hm.s.StatExitHandling);
    STAM_PROFILE_ADV_SET_STOPPED(&pVCpu->hm.s.StatExitVmentry);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchLongJmpToR3);

    VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_HM, VMCPUSTATE_STARTED_EXEC);
}


/**
 * Leaves the AMD-V session.
 *
 * Only used while returning to ring-3 either due to longjump or exits to
 * ring-3.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static int hmR0SvmLeaveSession(PVMCPUCC pVCpu)
{
    HM_DISABLE_PREEMPT(pVCpu);
    Assert(!VMMRZCallRing3IsEnabled(pVCpu));
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    /* When thread-context hooks are used, we can avoid doing the leave again if we had been preempted before
       and done this from the SVMR0ThreadCtxCallback(). */
    if (!pVCpu->hmr0.s.fLeaveDone)
    {
        hmR0SvmLeave(pVCpu, true /* fImportState */);
        pVCpu->hmr0.s.fLeaveDone = true;
    }

    /*
     * !!! IMPORTANT !!!
     * If you modify code here, make sure to check whether SVMR0CallRing3Callback() needs to be updated too.
     */

    /** @todo eliminate the need for calling VMMR0ThreadCtxHookDisable here!  */
    /* Deregister hook now that we've left HM context before re-enabling preemption. */
    VMMR0ThreadCtxHookDisable(pVCpu);

    /* Leave HM context. This takes care of local init (term). */
    int rc = HMR0LeaveCpu(pVCpu);

    HM_RESTORE_PREEMPT();
    return rc;
}


/**
 * VMMRZCallRing3() callback wrapper which saves the guest state (or restores
 * any remaining host state) before we go back to ring-3 due to an assertion.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
VMMR0DECL(int) SVMR0AssertionCallback(PVMCPUCC pVCpu)
{
    /*
     * !!! IMPORTANT !!!
     * If you modify code here, make sure to check whether hmR0SvmLeave() and hmR0SvmLeaveSession() needs
     * to be updated too. This is a stripped down version which gets out ASAP trying to not trigger any assertion.
     */
    VMMR0AssertionRemoveNotification(pVCpu);
    VMMRZCallRing3Disable(pVCpu);
    HM_DISABLE_PREEMPT(pVCpu);

    /* Import the entire guest state. */
    hmR0SvmImportGuestState(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);

    /* Restore host FPU state if necessary and resync on next R0 reentry. */
    CPUMR0FpuStateMaybeSaveGuestAndRestoreHost(pVCpu);

    /* Restore host debug registers if necessary and resync on next R0 reentry. */
    CPUMR0DebugStateMaybeSaveGuestAndRestoreHost(pVCpu, false /* save DR6 */);

    /* Deregister the hook now that we've left HM context before re-enabling preemption. */
    /** @todo eliminate the need for calling VMMR0ThreadCtxHookDisable here!  */
    VMMR0ThreadCtxHookDisable(pVCpu);

    /* Leave HM context. This takes care of local init (term). */
    HMR0LeaveCpu(pVCpu);

    HM_RESTORE_PREEMPT();
    return VINF_SUCCESS;
}


/**
 * Take necessary actions before going back to ring-3.
 *
 * An action requires us to go back to ring-3. This function does the necessary
 * steps before we can safely return to ring-3. This is not the same as longjmps
 * to ring-3, this is voluntary.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   rcExit      The reason for exiting to ring-3. Can be
 *                      VINF_VMM_UNKNOWN_RING3_CALL.
 */
static VBOXSTRICTRC hmR0SvmExitToRing3(PVMCPUCC pVCpu, VBOXSTRICTRC rcExit)
{
    Assert(pVCpu);
    HMSVM_ASSERT_PREEMPT_SAFE(pVCpu);

    /* Please, no longjumps here (any logging shouldn't flush jump back to ring-3). NO LOGGING BEFORE THIS POINT! */
    VMMRZCallRing3Disable(pVCpu);
    Log4Func(("rcExit=%d LocalFF=%#RX64 GlobalFF=%#RX32\n", VBOXSTRICTRC_VAL(rcExit), (uint64_t)pVCpu->fLocalForcedActions,
              pVCpu->CTX_SUFF(pVM)->fGlobalForcedActions));

    /* We need to do this only while truly exiting the "inner loop" back to ring-3 and -not- for any longjmp to ring3. */
    if (pVCpu->hm.s.Event.fPending)
    {
        hmR0SvmPendingEventToTrpmTrap(pVCpu);
        Assert(!pVCpu->hm.s.Event.fPending);
    }

    /* Sync. the necessary state for going back to ring-3. */
    hmR0SvmLeaveSession(pVCpu);
    STAM_COUNTER_DEC(&pVCpu->hm.s.StatSwitchLongJmpToR3);

    /* Thread-context hooks are unregistered at this point!!! */
    /* Ring-3 callback notifications are unregistered at this point!!! */

    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_TO_R3);
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_SYSENTER_MSR
                             | CPUM_CHANGED_LDTR
                             | CPUM_CHANGED_GDTR
                             | CPUM_CHANGED_IDTR
                             | CPUM_CHANGED_TR
                             | CPUM_CHANGED_HIDDEN_SEL_REGS);
    if (   pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging
        && CPUMIsGuestPagingEnabledEx(&pVCpu->cpum.GstCtx))
    {
        CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_GLOBAL_TLB_FLUSH);
    }

    /* Update the exit-to-ring 3 reason. */
    pVCpu->hm.s.rcLastExitToR3 = VBOXSTRICTRC_VAL(rcExit);

    /* On our way back from ring-3, reload the guest-CPU state if it may change while in ring-3. */
    if (   rcExit != VINF_EM_RAW_INTERRUPT
        || CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx))
    {
        Assert(!(pVCpu->cpum.GstCtx.fExtrn & HMSVM_CPUMCTX_EXTRN_ALL));
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_ALL_GUEST);
    }

    STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchExitToR3);
    VMMRZCallRing3Enable(pVCpu);

    /*
     * If we're emulating an instruction, we shouldn't have any TRPM traps pending
     * and if we're injecting an event we should have a TRPM trap pending.
     */
    AssertReturnStmt(rcExit != VINF_EM_RAW_INJECT_TRPM_EVENT || TRPMHasTrap(pVCpu),
                     pVCpu->hm.s.u32HMError = VBOXSTRICTRC_VAL(rcExit),
                     VERR_SVM_IPE_5);
    AssertReturnStmt(rcExit != VINF_EM_RAW_EMULATE_INSTR || !TRPMHasTrap(pVCpu),
                     pVCpu->hm.s.u32HMError = VBOXSTRICTRC_VAL(rcExit),
                     VERR_SVM_IPE_4);

    return rcExit;
}


/**
 * Updates the use of TSC offsetting mode for the CPU and adjusts the necessary
 * intercepts.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks No-long-jump zone!!!
 */
static void hmR0SvmUpdateTscOffsetting(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    /*
     * Avoid intercepting RDTSC/RDTSCP if we determined the host TSC (++) is stable
     * and in case of a nested-guest, if the nested-VMCB specifies it is not intercepting
     * RDTSC/RDTSCP as well.
     */
    bool       fParavirtTsc;
    uint64_t   uTscOffset;
    bool const fCanUseRealTsc = TMCpuTickCanUseRealTSC(pVCpu->CTX_SUFF(pVM), pVCpu, &uTscOffset, &fParavirtTsc);

    bool fIntercept;
    if (fCanUseRealTsc)
         fIntercept = hmR0SvmClearCtrlIntercept(pVCpu, pVmcb, SVM_CTRL_INTERCEPT_RDTSC | SVM_CTRL_INTERCEPT_RDTSCP);
    else
    {
        hmR0SvmSetCtrlIntercept(pVmcb, SVM_CTRL_INTERCEPT_RDTSC | SVM_CTRL_INTERCEPT_RDTSCP);
        fIntercept = true;
    }

    if (!fIntercept)
    {
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        /* Apply the nested-guest VMCB's TSC offset over the guest TSC offset. */
        if (CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx))
            uTscOffset = CPUMApplyNestedGuestTscOffset(pVCpu, uTscOffset);
#endif

        /* Update the TSC offset in the VMCB and the relevant clean bits. */
        pVmcb->ctrl.u64TSCOffset = uTscOffset;
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;
    }

    /* Currently neither Hyper-V nor KVM need to update their paravirt. TSC
       information before every VM-entry, hence we have nothing to do here at the moment. */
    if (fParavirtTsc)
        STAM_COUNTER_INC(&pVCpu->hm.s.StatTscParavirt);
}


/**
 * Sets an event as a pending event to be injected into the guest.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   pEvent              Pointer to the SVM event.
 * @param   GCPtrFaultAddress   The fault-address (CR2) in case it's a
 *                              page-fault.
 *
 * @remarks Statistics counter assumes this is a guest event being reflected to
 *          the guest i.e. 'StatInjectPendingReflect' is incremented always.
 */
DECLINLINE(void) hmR0SvmSetPendingEvent(PVMCPUCC pVCpu, PSVMEVENT pEvent, RTGCUINTPTR GCPtrFaultAddress)
{
    Assert(!pVCpu->hm.s.Event.fPending);
    Assert(pEvent->n.u1Valid);

    pVCpu->hm.s.Event.u64IntInfo        = pEvent->u;
    pVCpu->hm.s.Event.fPending          = true;
    pVCpu->hm.s.Event.GCPtrFaultAddress = GCPtrFaultAddress;

    Log4Func(("u=%#RX64 u8Vector=%#x Type=%#x ErrorCodeValid=%RTbool ErrorCode=%#RX32\n", pEvent->u, pEvent->n.u8Vector,
              (uint8_t)pEvent->n.u3Type, !!pEvent->n.u1ErrorCodeValid, pEvent->n.u32ErrorCode));
}


/**
 * Sets an divide error (\#DE) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(void) hmR0SvmSetPendingXcptDE(PVMCPUCC pVCpu)
{
    SVMEVENT Event;
    Event.u          = 0;
    Event.n.u1Valid  = 1;
    Event.n.u3Type   = SVM_EVENT_EXCEPTION;
    Event.n.u8Vector = X86_XCPT_DE;
    hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
}


/**
 * Sets an invalid-opcode (\#UD) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(void) hmR0SvmSetPendingXcptUD(PVMCPUCC pVCpu)
{
    SVMEVENT Event;
    Event.u          = 0;
    Event.n.u1Valid  = 1;
    Event.n.u3Type   = SVM_EVENT_EXCEPTION;
    Event.n.u8Vector = X86_XCPT_UD;
    hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
}


/**
 * Sets a debug (\#DB) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(void) hmR0SvmSetPendingXcptDB(PVMCPUCC pVCpu)
{
    SVMEVENT Event;
    Event.u          = 0;
    Event.n.u1Valid  = 1;
    Event.n.u3Type   = SVM_EVENT_EXCEPTION;
    Event.n.u8Vector = X86_XCPT_DB;
    hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
}


/**
 * Sets a page fault (\#PF) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   u32ErrCode      The error-code for the page-fault.
 * @param   uFaultAddress   The page fault address (CR2).
 *
 * @remarks This updates the guest CR2 with @a uFaultAddress!
 */
DECLINLINE(void) hmR0SvmSetPendingXcptPF(PVMCPUCC pVCpu, uint32_t u32ErrCode, RTGCUINTPTR uFaultAddress)
{
    SVMEVENT Event;
    Event.u                  = 0;
    Event.n.u1Valid          = 1;
    Event.n.u3Type           = SVM_EVENT_EXCEPTION;
    Event.n.u8Vector         = X86_XCPT_PF;
    Event.n.u1ErrorCodeValid = 1;
    Event.n.u32ErrorCode     = u32ErrCode;

    /* Update CR2 of the guest. */
    HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR2);
    if (pVCpu->cpum.GstCtx.cr2 != uFaultAddress)
    {
        pVCpu->cpum.GstCtx.cr2 = uFaultAddress;
        /* The VMCB clean bit for CR2 will be updated while re-loading the guest state. */
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_CR2);
    }

    hmR0SvmSetPendingEvent(pVCpu, &Event, uFaultAddress);
}


/**
 * Sets a math-fault (\#MF) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(void) hmR0SvmSetPendingXcptMF(PVMCPUCC pVCpu)
{
    SVMEVENT Event;
    Event.u          = 0;
    Event.n.u1Valid  = 1;
    Event.n.u3Type   = SVM_EVENT_EXCEPTION;
    Event.n.u8Vector = X86_XCPT_MF;
    hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
}


/**
 * Sets a double fault (\#DF) exception as pending-for-injection into the VM.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(void) hmR0SvmSetPendingXcptDF(PVMCPUCC pVCpu)
{
    SVMEVENT Event;
    Event.u                  = 0;
    Event.n.u1Valid          = 1;
    Event.n.u3Type           = SVM_EVENT_EXCEPTION;
    Event.n.u8Vector         = X86_XCPT_DF;
    Event.n.u1ErrorCodeValid = 1;
    Event.n.u32ErrorCode     = 0;
    hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
}


/**
 * Injects an event into the guest upon VMRUN by updating the relevant field
 * in the VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the guest VM control block.
 * @param   pEvent      Pointer to the event.
 *
 * @remarks No-long-jump zone!!!
 * @remarks Requires CR0!
 */
DECLINLINE(void) hmR0SvmInjectEventVmcb(PVMCPUCC pVCpu, PSVMVMCB pVmcb, PSVMEVENT pEvent)
{
    Assert(!pVmcb->ctrl.EventInject.n.u1Valid);
    pVmcb->ctrl.EventInject.u = pEvent->u;
    if (   pVmcb->ctrl.EventInject.n.u3Type == SVM_EVENT_EXCEPTION
        || pVmcb->ctrl.EventInject.n.u3Type == SVM_EVENT_NMI)
    {
        Assert(pEvent->n.u8Vector <= X86_XCPT_LAST);
        STAM_COUNTER_INC(&pVCpu->hm.s.aStatInjectedXcpts[pEvent->n.u8Vector]);
    }
    else
        STAM_COUNTER_INC(&pVCpu->hm.s.aStatInjectedIrqs[pEvent->n.u8Vector & MASK_INJECT_IRQ_STAT]);
    RT_NOREF(pVCpu);

    Log4Func(("u=%#RX64 u8Vector=%#x Type=%#x ErrorCodeValid=%RTbool ErrorCode=%#RX32\n", pEvent->u, pEvent->n.u8Vector,
              (uint8_t)pEvent->n.u3Type, !!pEvent->n.u1ErrorCodeValid, pEvent->n.u32ErrorCode));
}



/**
 * Converts any TRPM trap into a pending HM event. This is typically used when
 * entering from ring-3 (not longjmp returns).
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static void hmR0SvmTrpmTrapToPendingEvent(PVMCPUCC pVCpu)
{
    Assert(TRPMHasTrap(pVCpu));
    Assert(!pVCpu->hm.s.Event.fPending);

    uint8_t     uVector;
    TRPMEVENT   enmTrpmEvent;
    uint32_t    uErrCode;
    RTGCUINTPTR GCPtrFaultAddress;
    uint8_t     cbInstr;

    int rc = TRPMQueryTrapAll(pVCpu, &uVector, &enmTrpmEvent, &uErrCode, &GCPtrFaultAddress, &cbInstr, NULL /* pfIcebp */);
    AssertRC(rc);

    SVMEVENT Event;
    Event.u          = 0;
    Event.n.u1Valid  = 1;
    Event.n.u8Vector = uVector;

    /* Refer AMD spec. 15.20 "Event Injection" for the format. */
    if (enmTrpmEvent == TRPM_TRAP)
    {
        Event.n.u3Type = SVM_EVENT_EXCEPTION;
        switch (uVector)
        {
            case X86_XCPT_BP:
            case X86_XCPT_OF:
                AssertMsgFailed(("Invalid TRPM vector %d for event type %d\n", uVector, enmTrpmEvent));
                RT_FALL_THRU();

            case X86_XCPT_PF:
            case X86_XCPT_DF:
            case X86_XCPT_TS:
            case X86_XCPT_NP:
            case X86_XCPT_SS:
            case X86_XCPT_GP:
            case X86_XCPT_AC:
            {
                Event.n.u1ErrorCodeValid = 1;
                Event.n.u32ErrorCode     = uErrCode;
                break;
            }
        }
    }
    else if (enmTrpmEvent == TRPM_HARDWARE_INT)
        Event.n.u3Type = SVM_EVENT_EXTERNAL_IRQ;
    else if (enmTrpmEvent == TRPM_SOFTWARE_INT)
        Event.n.u3Type = SVM_EVENT_SOFTWARE_INT;
    else if (enmTrpmEvent == TRPM_NMI)
        Event.n.u3Type = SVM_EVENT_NMI;
    else
        AssertMsgFailed(("Invalid TRPM event type %d\n", enmTrpmEvent));

    rc = TRPMResetTrap(pVCpu);
    AssertRC(rc);

    Log4(("TRPM->HM event: u=%#RX64 u8Vector=%#x uErrorCodeValid=%RTbool uErrorCode=%#RX32\n", Event.u, Event.n.u8Vector,
          !!Event.n.u1ErrorCodeValid, Event.n.u32ErrorCode));

    hmR0SvmSetPendingEvent(pVCpu, &Event, GCPtrFaultAddress);
}


/**
 * Converts any pending SVM event into a TRPM trap. Typically used when leaving
 * AMD-V to execute any instruction.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static void hmR0SvmPendingEventToTrpmTrap(PVMCPUCC pVCpu)
{
    Assert(pVCpu->hm.s.Event.fPending);
    Assert(TRPMQueryTrap(pVCpu, NULL /* pu8TrapNo */, NULL /* pEnmType */) == VERR_TRPM_NO_ACTIVE_TRAP);

    SVMEVENT Event;
    Event.u = pVCpu->hm.s.Event.u64IntInfo;

    uint8_t   uVector     = Event.n.u8Vector;
    TRPMEVENT enmTrapType = HMSvmEventToTrpmEventType(&Event, uVector);

    Log4(("HM event->TRPM: uVector=%#x enmTrapType=%d\n", uVector, Event.n.u3Type));

    int rc = TRPMAssertTrap(pVCpu, uVector, enmTrapType);
    AssertRC(rc);

    if (Event.n.u1ErrorCodeValid)
        TRPMSetErrorCode(pVCpu, Event.n.u32ErrorCode);

    if (   enmTrapType == TRPM_TRAP
        && uVector     == X86_XCPT_PF)
    {
        TRPMSetFaultAddress(pVCpu, pVCpu->hm.s.Event.GCPtrFaultAddress);
        Assert(pVCpu->hm.s.Event.GCPtrFaultAddress == CPUMGetGuestCR2(pVCpu));
    }
    else if (enmTrapType == TRPM_SOFTWARE_INT)
        TRPMSetInstrLength(pVCpu, pVCpu->hm.s.Event.cbInstr);
    pVCpu->hm.s.Event.fPending = false;
}


/**
 * Sets the virtual interrupt intercept control in the VMCB.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pVmcb   Pointer to the VM control block.
 */
static void hmR0SvmSetIntWindowExiting(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    HMSVM_ASSERT_NOT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx); NOREF(pVCpu);

    /*
     * When AVIC isn't supported, set up an interrupt window to cause a #VMEXIT when the guest
     * is ready to accept interrupts. At #VMEXIT, we then get the interrupt from the APIC
     * (updating ISR at the right time) and inject the interrupt.
     *
     * With AVIC is supported, we could make use of the asynchronously delivery without
     * #VMEXIT and we would be passing the AVIC page to SVM.
     *
     * In AMD-V, an interrupt window is achieved using a combination of V_IRQ (an interrupt
     * is pending), V_IGN_TPR (ignore TPR priorities) and the VINTR intercept all being set.
     */
    Assert(pVmcb->ctrl.IntCtrl.n.u1IgnoreTPR);
    pVmcb->ctrl.IntCtrl.n.u1VIrqPending = 1;
    pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INT_CTRL;
    hmR0SvmSetCtrlIntercept(pVmcb, SVM_CTRL_INTERCEPT_VINTR);
    Log4(("Set VINTR intercept\n"));
}


/**
 * Clears the virtual interrupt intercept control in the VMCB as
 * we are figured the guest is unable process any interrupts
 * at this point of time.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pVmcb   Pointer to the VM control block.
 */
static void hmR0SvmClearIntWindowExiting(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    HMSVM_ASSERT_NOT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx); NOREF(pVCpu);

    PSVMVMCBCTRL pVmcbCtrl = &pVmcb->ctrl;
    if (    pVmcbCtrl->IntCtrl.n.u1VIrqPending
        || (pVmcbCtrl->u64InterceptCtrl & SVM_CTRL_INTERCEPT_VINTR))
    {
        pVmcbCtrl->IntCtrl.n.u1VIrqPending = 0;
        pVmcbCtrl->u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INT_CTRL;
        hmR0SvmClearCtrlIntercept(pVCpu, pVmcb, SVM_CTRL_INTERCEPT_VINTR);
        Log4(("Cleared VINTR intercept\n"));
    }
}


/**
 * Evaluates the event to be delivered to the guest and sets it as the pending
 * event.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM transient structure.
 */
static VBOXSTRICTRC hmR0SvmEvaluatePendingEvent(PVMCPUCC pVCpu, PCSVMTRANSIENT pSvmTransient)
{
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_HWVIRT
                              | CPUMCTX_EXTRN_RFLAGS
                              | CPUMCTX_EXTRN_INHIBIT_INT
                              | CPUMCTX_EXTRN_HM_SVM_HWVIRT_VIRQ);

    Assert(!pVCpu->hm.s.Event.fPending);
    PSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    Assert(pVmcb);

    bool const fGif        = CPUMGetGuestGif(pCtx);
    bool const fIntShadow  = CPUMIsInInterruptShadowWithUpdate(pCtx);
    bool const fBlockNmi   = CPUMAreInterruptsInhibitedByNmi(pCtx);

    Log4Func(("fGif=%RTbool fBlockNmi=%RTbool fIntShadow=%RTbool fIntPending=%RTbool fNmiPending=%RTbool\n",
              fGif, fBlockNmi, fIntShadow, VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC),
              VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI)));

    /** @todo SMI. SMIs take priority over NMIs. */

    /*
     * Check if the guest or nested-guest can receive NMIs.
     * Nested NMIs are not allowed, see AMD spec. 8.1.4 "Masking External Interrupts".
     * NMIs take priority over maskable interrupts, see AMD spec. 8.5 "Priorities".
     */
    if (    VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI)
        && !fBlockNmi)
    {
        if (    fGif
            && !fIntShadow)
        {
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_NMI))
            {
                Log4(("Intercepting NMI -> #VMEXIT\n"));
                HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
                return IEMExecSvmVmexit(pVCpu, SVM_EXIT_NMI, 0, 0);
            }
#endif
            Log4(("Setting NMI pending for injection\n"));
            SVMEVENT Event;
            Event.u = 0;
            Event.n.u1Valid  = 1;
            Event.n.u8Vector = X86_XCPT_NMI;
            Event.n.u3Type   = SVM_EVENT_NMI;
            hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NMI);
        }
        else if (!fGif)
            hmR0SvmSetCtrlIntercept(pVmcb, SVM_CTRL_INTERCEPT_STGI);
        else if (!pSvmTransient->fIsNestedGuest)
            hmR0SvmSetIntWindowExiting(pVCpu, pVmcb);
        /* else: for nested-guests, interrupt-window exiting will be picked up when merging VMCB controls. */
    }
    /*
     * Check if the guest can receive external interrupts (PIC/APIC). Once PDMGetInterrupt()
     * returns a valid interrupt we -must- deliver the interrupt. We can no longer re-request
     * it from the APIC device.
     *
     * For nested-guests, physical interrupts always take priority over virtual interrupts.
     * We don't need to inject nested-guest virtual interrupts here, we can let the hardware
     * do that work when we execute nested-guest code esp. since all the required information
     * is in the VMCB, unlike physical interrupts where we need to fetch the interrupt from
     * the virtual interrupt controller.
     *
     * See AMD spec. 15.21.4 "Injecting Virtual (INTR) Interrupts".
     */
    else if (   VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)
             && !pVCpu->hm.s.fSingleInstruction)
    {
        bool const fBlockInt = !pSvmTransient->fIsNestedGuest ? !(pCtx->eflags.u & X86_EFL_IF)
                                                              : CPUMIsGuestSvmPhysIntrEnabled(pVCpu, pCtx);
        if (    fGif
            && !fBlockInt
            && !fIntShadow)
        {
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_INTR))
            {
                Log4(("Intercepting INTR -> #VMEXIT\n"));
                HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
                return IEMExecSvmVmexit(pVCpu, SVM_EXIT_INTR, 0, 0);
            }
#endif
            uint8_t u8Interrupt;
            int rc = PDMGetInterrupt(pVCpu, &u8Interrupt);
            if (RT_SUCCESS(rc))
            {
                Log4(("Setting external interrupt %#x pending for injection\n", u8Interrupt));
                SVMEVENT Event;
                Event.u = 0;
                Event.n.u1Valid  = 1;
                Event.n.u8Vector = u8Interrupt;
                Event.n.u3Type   = SVM_EVENT_EXTERNAL_IRQ;
                hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
            }
            else if (rc == VERR_APIC_INTR_MASKED_BY_TPR)
            {
                /*
                 * AMD-V has no TPR thresholding feature. TPR and the force-flag will be
                 * updated eventually when the TPR is written by the guest.
                 */
                STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchTprMaskedIrq);
            }
            else
                STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchGuestIrq);
        }
        else if (!fGif)
            hmR0SvmSetCtrlIntercept(pVmcb, SVM_CTRL_INTERCEPT_STGI);
        else if (!pSvmTransient->fIsNestedGuest)
            hmR0SvmSetIntWindowExiting(pVCpu, pVmcb);
        /* else: for nested-guests, interrupt-window exiting will be picked up when merging VMCB controls. */
    }

    return VINF_SUCCESS;
}


/**
 * Injects any pending events into the guest (or nested-guest).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 *
 * @remarks Must only be called when we are guaranteed to enter
 *          hardware-assisted SVM execution and not return to ring-3
 *          prematurely.
 */
static void hmR0SvmInjectPendingEvent(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    Assert(!TRPMHasTrap(pVCpu));
    Assert(!VMMRZCallRing3IsEnabled(pVCpu));

    bool const fIntShadow = CPUMIsInInterruptShadowWithUpdate(&pVCpu->cpum.GstCtx);
#ifdef VBOX_STRICT
    PCCPUMCTX  pCtx       = &pVCpu->cpum.GstCtx;
    bool const fGif       = CPUMGetGuestGif(pCtx);
    bool       fAllowInt  = fGif;
    if (fGif)
    {
        /*
         * For nested-guests we have no way to determine if we're injecting a physical or
         * virtual interrupt at this point. Hence the partial verification below.
         */
        if (CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
            fAllowInt = CPUMIsGuestSvmPhysIntrEnabled(pVCpu, pCtx) || CPUMIsGuestSvmVirtIntrEnabled(pVCpu, pCtx);
        else
            fAllowInt = RT_BOOL(pCtx->eflags.u & X86_EFL_IF);
    }
#endif

    if (pVCpu->hm.s.Event.fPending)
    {
        SVMEVENT Event;
        Event.u = pVCpu->hm.s.Event.u64IntInfo;
        Assert(Event.n.u1Valid);

        /*
         * Validate event injection pre-conditions.
         */
        if (Event.n.u3Type == SVM_EVENT_EXTERNAL_IRQ)
        {
            Assert(fAllowInt);
            Assert(!fIntShadow);
        }
        else if (Event.n.u3Type == SVM_EVENT_NMI)
        {
            Assert(fGif);
            Assert(!fIntShadow);
        }

        /*
         * Before injecting an NMI we must set VMCPU_FF_BLOCK_NMIS to prevent nested NMIs. We
         * do this only when we are surely going to inject the NMI as otherwise if we return
         * to ring-3 prematurely we could leave NMIs blocked indefinitely upon re-entry into
         * SVM R0.
         *
         * With VT-x, this is handled by the Guest interruptibility information VMCS field
         * which will set the VMCS field after actually delivering the NMI which we read on
         * VM-exit to determine the state.
         */
        if (   Event.n.u3Type   == SVM_EVENT_NMI
            && Event.n.u8Vector == X86_XCPT_NMI)
            CPUMSetInterruptInhibitingByNmi(&pVCpu->cpum.GstCtx);

        /*
         * Inject it (update VMCB for injection by the hardware).
         */
        Log4(("Injecting pending HM event\n"));
        hmR0SvmInjectEventVmcb(pVCpu, pVmcb, &Event);
        pVCpu->hm.s.Event.fPending = false;

        if (Event.n.u3Type == SVM_EVENT_EXTERNAL_IRQ)
            STAM_COUNTER_INC(&pVCpu->hm.s.StatInjectInterrupt);
        else
            STAM_COUNTER_INC(&pVCpu->hm.s.StatInjectXcpt);
    }
    else
        Assert(pVmcb->ctrl.EventInject.n.u1Valid == 0);

    /*
     * We could have injected an NMI through IEM and continue guest execution using
     * hardware-assisted SVM. In which case, we would not have any events pending (above)
     * but we still need to intercept IRET in order to eventually clear NMI inhibition.
     */
    if (CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx))
        hmR0SvmSetCtrlIntercept(pVmcb, SVM_CTRL_INTERCEPT_IRET);

    /*
     * Update the guest interrupt shadow in the guest (or nested-guest) VMCB.
     *
     * For nested-guests: We need to update it too for the scenario where IEM executes
     * the nested-guest but execution later continues here with an interrupt shadow active.
     */
    pVmcb->ctrl.IntShadow.n.u1IntShadow = fIntShadow;
}


/**
 * Reports world-switch error and dumps some useful debug info.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   rcVMRun         The return code from VMRUN (or
 *                          VERR_SVM_INVALID_GUEST_STATE for invalid
 *                          guest-state).
 */
static void hmR0SvmReportWorldSwitchError(PVMCPUCC pVCpu, int rcVMRun)
{
    HMSVM_ASSERT_PREEMPT_SAFE(pVCpu);
    HMSVM_ASSERT_NOT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);

    if (rcVMRun == VERR_SVM_INVALID_GUEST_STATE)
    {
#ifdef VBOX_STRICT
        hmR0DumpRegs(pVCpu, HM_DUMP_REG_FLAGS_ALL);
        PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
        Log4(("ctrl.u32VmcbCleanBits                 %#RX32\n",   pVmcb->ctrl.u32VmcbCleanBits));
        Log4(("ctrl.u16InterceptRdCRx                %#x\n",      pVmcb->ctrl.u16InterceptRdCRx));
        Log4(("ctrl.u16InterceptWrCRx                %#x\n",      pVmcb->ctrl.u16InterceptWrCRx));
        Log4(("ctrl.u16InterceptRdDRx                %#x\n",      pVmcb->ctrl.u16InterceptRdDRx));
        Log4(("ctrl.u16InterceptWrDRx                %#x\n",      pVmcb->ctrl.u16InterceptWrDRx));
        Log4(("ctrl.u32InterceptXcpt                 %#x\n",      pVmcb->ctrl.u32InterceptXcpt));
        Log4(("ctrl.u64InterceptCtrl                 %#RX64\n",   pVmcb->ctrl.u64InterceptCtrl));
        Log4(("ctrl.u64IOPMPhysAddr                  %#RX64\n",   pVmcb->ctrl.u64IOPMPhysAddr));
        Log4(("ctrl.u64MSRPMPhysAddr                 %#RX64\n",   pVmcb->ctrl.u64MSRPMPhysAddr));
        Log4(("ctrl.u64TSCOffset                     %#RX64\n",   pVmcb->ctrl.u64TSCOffset));

        Log4(("ctrl.TLBCtrl.u32ASID                  %#x\n",      pVmcb->ctrl.TLBCtrl.n.u32ASID));
        Log4(("ctrl.TLBCtrl.u8TLBFlush               %#x\n",      pVmcb->ctrl.TLBCtrl.n.u8TLBFlush));
        Log4(("ctrl.TLBCtrl.u24Reserved              %#x\n",      pVmcb->ctrl.TLBCtrl.n.u24Reserved));

        Log4(("ctrl.IntCtrl.u8VTPR                   %#x\n",      pVmcb->ctrl.IntCtrl.n.u8VTPR));
        Log4(("ctrl.IntCtrl.u1VIrqPending            %#x\n",      pVmcb->ctrl.IntCtrl.n.u1VIrqPending));
        Log4(("ctrl.IntCtrl.u1VGif                   %#x\n",      pVmcb->ctrl.IntCtrl.n.u1VGif));
        Log4(("ctrl.IntCtrl.u6Reserved0              %#x\n",      pVmcb->ctrl.IntCtrl.n.u6Reserved));
        Log4(("ctrl.IntCtrl.u4VIntrPrio              %#x\n",      pVmcb->ctrl.IntCtrl.n.u4VIntrPrio));
        Log4(("ctrl.IntCtrl.u1IgnoreTPR              %#x\n",      pVmcb->ctrl.IntCtrl.n.u1IgnoreTPR));
        Log4(("ctrl.IntCtrl.u3Reserved               %#x\n",      pVmcb->ctrl.IntCtrl.n.u3Reserved));
        Log4(("ctrl.IntCtrl.u1VIntrMasking           %#x\n",      pVmcb->ctrl.IntCtrl.n.u1VIntrMasking));
        Log4(("ctrl.IntCtrl.u1VGifEnable             %#x\n",      pVmcb->ctrl.IntCtrl.n.u1VGifEnable));
        Log4(("ctrl.IntCtrl.u5Reserved1              %#x\n",      pVmcb->ctrl.IntCtrl.n.u5Reserved));
        Log4(("ctrl.IntCtrl.u8VIntrVector            %#x\n",      pVmcb->ctrl.IntCtrl.n.u8VIntrVector));
        Log4(("ctrl.IntCtrl.u24Reserved              %#x\n",      pVmcb->ctrl.IntCtrl.n.u24Reserved));

        Log4(("ctrl.IntShadow.u1IntShadow            %#x\n",      pVmcb->ctrl.IntShadow.n.u1IntShadow));
        Log4(("ctrl.IntShadow.u1GuestIntMask         %#x\n",      pVmcb->ctrl.IntShadow.n.u1GuestIntMask));
        Log4(("ctrl.u64ExitCode                      %#RX64\n",   pVmcb->ctrl.u64ExitCode));
        Log4(("ctrl.u64ExitInfo1                     %#RX64\n",   pVmcb->ctrl.u64ExitInfo1));
        Log4(("ctrl.u64ExitInfo2                     %#RX64\n",   pVmcb->ctrl.u64ExitInfo2));
        Log4(("ctrl.ExitIntInfo.u8Vector             %#x\n",      pVmcb->ctrl.ExitIntInfo.n.u8Vector));
        Log4(("ctrl.ExitIntInfo.u3Type               %#x\n",      pVmcb->ctrl.ExitIntInfo.n.u3Type));
        Log4(("ctrl.ExitIntInfo.u1ErrorCodeValid     %#x\n",      pVmcb->ctrl.ExitIntInfo.n.u1ErrorCodeValid));
        Log4(("ctrl.ExitIntInfo.u19Reserved          %#x\n",      pVmcb->ctrl.ExitIntInfo.n.u19Reserved));
        Log4(("ctrl.ExitIntInfo.u1Valid              %#x\n",      pVmcb->ctrl.ExitIntInfo.n.u1Valid));
        Log4(("ctrl.ExitIntInfo.u32ErrorCode         %#x\n",      pVmcb->ctrl.ExitIntInfo.n.u32ErrorCode));
        Log4(("ctrl.NestedPagingCtrl.u1NestedPaging  %#x\n",      pVmcb->ctrl.NestedPagingCtrl.n.u1NestedPaging));
        Log4(("ctrl.NestedPagingCtrl.u1Sev           %#x\n",      pVmcb->ctrl.NestedPagingCtrl.n.u1Sev));
        Log4(("ctrl.NestedPagingCtrl.u1SevEs         %#x\n",      pVmcb->ctrl.NestedPagingCtrl.n.u1SevEs));
        Log4(("ctrl.EventInject.u8Vector             %#x\n",      pVmcb->ctrl.EventInject.n.u8Vector));
        Log4(("ctrl.EventInject.u3Type               %#x\n",      pVmcb->ctrl.EventInject.n.u3Type));
        Log4(("ctrl.EventInject.u1ErrorCodeValid     %#x\n",      pVmcb->ctrl.EventInject.n.u1ErrorCodeValid));
        Log4(("ctrl.EventInject.u19Reserved          %#x\n",      pVmcb->ctrl.EventInject.n.u19Reserved));
        Log4(("ctrl.EventInject.u1Valid              %#x\n",      pVmcb->ctrl.EventInject.n.u1Valid));
        Log4(("ctrl.EventInject.u32ErrorCode         %#x\n",      pVmcb->ctrl.EventInject.n.u32ErrorCode));

        Log4(("ctrl.u64NestedPagingCR3               %#RX64\n",   pVmcb->ctrl.u64NestedPagingCR3));

        Log4(("ctrl.LbrVirt.u1LbrVirt                %#x\n",      pVmcb->ctrl.LbrVirt.n.u1LbrVirt));
        Log4(("ctrl.LbrVirt.u1VirtVmsaveVmload       %#x\n",      pVmcb->ctrl.LbrVirt.n.u1VirtVmsaveVmload));

        Log4(("guest.CS.u16Sel                       %RTsel\n",   pVmcb->guest.CS.u16Sel));
        Log4(("guest.CS.u16Attr                      %#x\n",      pVmcb->guest.CS.u16Attr));
        Log4(("guest.CS.u32Limit                     %#RX32\n",   pVmcb->guest.CS.u32Limit));
        Log4(("guest.CS.u64Base                      %#RX64\n",   pVmcb->guest.CS.u64Base));
        Log4(("guest.DS.u16Sel                       %#RTsel\n",  pVmcb->guest.DS.u16Sel));
        Log4(("guest.DS.u16Attr                      %#x\n",      pVmcb->guest.DS.u16Attr));
        Log4(("guest.DS.u32Limit                     %#RX32\n",   pVmcb->guest.DS.u32Limit));
        Log4(("guest.DS.u64Base                      %#RX64\n",   pVmcb->guest.DS.u64Base));
        Log4(("guest.ES.u16Sel                       %RTsel\n",   pVmcb->guest.ES.u16Sel));
        Log4(("guest.ES.u16Attr                      %#x\n",      pVmcb->guest.ES.u16Attr));
        Log4(("guest.ES.u32Limit                     %#RX32\n",   pVmcb->guest.ES.u32Limit));
        Log4(("guest.ES.u64Base                      %#RX64\n",   pVmcb->guest.ES.u64Base));
        Log4(("guest.FS.u16Sel                       %RTsel\n",   pVmcb->guest.FS.u16Sel));
        Log4(("guest.FS.u16Attr                      %#x\n",      pVmcb->guest.FS.u16Attr));
        Log4(("guest.FS.u32Limit                     %#RX32\n",   pVmcb->guest.FS.u32Limit));
        Log4(("guest.FS.u64Base                      %#RX64\n",   pVmcb->guest.FS.u64Base));
        Log4(("guest.GS.u16Sel                       %RTsel\n",   pVmcb->guest.GS.u16Sel));
        Log4(("guest.GS.u16Attr                      %#x\n",      pVmcb->guest.GS.u16Attr));
        Log4(("guest.GS.u32Limit                     %#RX32\n",   pVmcb->guest.GS.u32Limit));
        Log4(("guest.GS.u64Base                      %#RX64\n",   pVmcb->guest.GS.u64Base));

        Log4(("guest.GDTR.u32Limit                   %#RX32\n",   pVmcb->guest.GDTR.u32Limit));
        Log4(("guest.GDTR.u64Base                    %#RX64\n",   pVmcb->guest.GDTR.u64Base));

        Log4(("guest.LDTR.u16Sel                     %RTsel\n",   pVmcb->guest.LDTR.u16Sel));
        Log4(("guest.LDTR.u16Attr                    %#x\n",      pVmcb->guest.LDTR.u16Attr));
        Log4(("guest.LDTR.u32Limit                   %#RX32\n",   pVmcb->guest.LDTR.u32Limit));
        Log4(("guest.LDTR.u64Base                    %#RX64\n",   pVmcb->guest.LDTR.u64Base));

        Log4(("guest.IDTR.u32Limit                   %#RX32\n",   pVmcb->guest.IDTR.u32Limit));
        Log4(("guest.IDTR.u64Base                    %#RX64\n",   pVmcb->guest.IDTR.u64Base));

        Log4(("guest.TR.u16Sel                       %RTsel\n",   pVmcb->guest.TR.u16Sel));
        Log4(("guest.TR.u16Attr                      %#x\n",      pVmcb->guest.TR.u16Attr));
        Log4(("guest.TR.u32Limit                     %#RX32\n",   pVmcb->guest.TR.u32Limit));
        Log4(("guest.TR.u64Base                      %#RX64\n",   pVmcb->guest.TR.u64Base));

        Log4(("guest.u8CPL                           %#x\n",      pVmcb->guest.u8CPL));
        Log4(("guest.u64CR0                          %#RX64\n",   pVmcb->guest.u64CR0));
        Log4(("guest.u64CR2                          %#RX64\n",   pVmcb->guest.u64CR2));
        Log4(("guest.u64CR3                          %#RX64\n",   pVmcb->guest.u64CR3));
        Log4(("guest.u64CR4                          %#RX64\n",   pVmcb->guest.u64CR4));
        Log4(("guest.u64DR6                          %#RX64\n",   pVmcb->guest.u64DR6));
        Log4(("guest.u64DR7                          %#RX64\n",   pVmcb->guest.u64DR7));

        Log4(("guest.u64RIP                          %#RX64\n",   pVmcb->guest.u64RIP));
        Log4(("guest.u64RSP                          %#RX64\n",   pVmcb->guest.u64RSP));
        Log4(("guest.u64RAX                          %#RX64\n",   pVmcb->guest.u64RAX));
        Log4(("guest.u64RFlags                       %#RX64\n",   pVmcb->guest.u64RFlags));

        Log4(("guest.u64SysEnterCS                   %#RX64\n",   pVmcb->guest.u64SysEnterCS));
        Log4(("guest.u64SysEnterEIP                  %#RX64\n",   pVmcb->guest.u64SysEnterEIP));
        Log4(("guest.u64SysEnterESP                  %#RX64\n",   pVmcb->guest.u64SysEnterESP));

        Log4(("guest.u64EFER                         %#RX64\n",   pVmcb->guest.u64EFER));
        Log4(("guest.u64STAR                         %#RX64\n",   pVmcb->guest.u64STAR));
        Log4(("guest.u64LSTAR                        %#RX64\n",   pVmcb->guest.u64LSTAR));
        Log4(("guest.u64CSTAR                        %#RX64\n",   pVmcb->guest.u64CSTAR));
        Log4(("guest.u64SFMASK                       %#RX64\n",   pVmcb->guest.u64SFMASK));
        Log4(("guest.u64KernelGSBase                 %#RX64\n",   pVmcb->guest.u64KernelGSBase));
        Log4(("guest.u64PAT                          %#RX64\n",   pVmcb->guest.u64PAT));
        Log4(("guest.u64DBGCTL                       %#RX64\n",   pVmcb->guest.u64DBGCTL));
        Log4(("guest.u64BR_FROM                      %#RX64\n",   pVmcb->guest.u64BR_FROM));
        Log4(("guest.u64BR_TO                        %#RX64\n",   pVmcb->guest.u64BR_TO));
        Log4(("guest.u64LASTEXCPFROM                 %#RX64\n",   pVmcb->guest.u64LASTEXCPFROM));
        Log4(("guest.u64LASTEXCPTO                   %#RX64\n",   pVmcb->guest.u64LASTEXCPTO));

        NOREF(pVmcb);
#endif  /* VBOX_STRICT */
    }
    else
        Log4Func(("rcVMRun=%d\n", rcVMRun));
}


/**
 * Check per-VM and per-VCPU force flag actions that require us to go back to
 * ring-3 for one reason or another.
 *
 * @returns Strict VBox status code (information status code included).
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
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static VBOXSTRICTRC hmR0SvmCheckForceFlags(PVMCPUCC pVCpu)
{
    Assert(VMMRZCallRing3IsEnabled(pVCpu));

    /* Could happen as a result of longjump. */
    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3))
        PGMUpdateCR3(pVCpu, CPUMGetGuestCR3(pVCpu));

    /* Update pending interrupts into the APIC's IRR. */
    if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_UPDATE_APIC))
        APICUpdatePendingInterrupts(pVCpu);

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (   VM_FF_IS_ANY_SET(pVM, !pVCpu->hm.s.fSingleInstruction
                            ? VM_FF_HP_R0_PRE_HM_MASK : VM_FF_HP_R0_PRE_HM_STEP_MASK)
        || VMCPU_FF_IS_ANY_SET(pVCpu, !pVCpu->hm.s.fSingleInstruction
                               ? VMCPU_FF_HP_R0_PRE_HM_MASK : VMCPU_FF_HP_R0_PRE_HM_STEP_MASK) )
    {
        /* Pending PGM C3 sync. */
        if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3 | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL))
        {
            int rc = PGMSyncCR3(pVCpu, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr3, pVCpu->cpum.GstCtx.cr4,
                                VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3));
            if (rc != VINF_SUCCESS)
            {
                Log4Func(("PGMSyncCR3 forcing us back to ring-3. rc=%d\n", rc));
                return rc;
            }
        }

        /* Pending HM-to-R3 operations (critsects, timers, EMT rendezvous etc.) */
        /* -XXX- what was that about single stepping?  */
        if (   VM_FF_IS_ANY_SET(pVM, VM_FF_HM_TO_R3_MASK)
            || VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HM_TO_R3_MASK))
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchHmToR3FF);
            int rc = RT_LIKELY(!VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)) ? VINF_EM_RAW_TO_R3 : VINF_EM_NO_MEMORY;
            Log4Func(("HM_TO_R3 forcing us back to ring-3. rc=%d\n", rc));
            return rc;
        }

        /* Pending VM request packets, such as hardware interrupts. */
        if (   VM_FF_IS_SET(pVM, VM_FF_REQUEST)
            || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_REQUEST))
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchVmReq);
            Log4Func(("Pending VM request forcing us back to ring-3\n"));
            return VINF_EM_PENDING_REQUEST;
        }

        /* Pending PGM pool flushes. */
        if (VM_FF_IS_SET(pVM, VM_FF_PGM_POOL_FLUSH_PENDING))
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchPgmPoolFlush);
            Log4Func(("PGM pool flush pending forcing us back to ring-3\n"));
            return VINF_PGM_POOL_FLUSH_PENDING;
        }

        /* Pending DMA requests. */
        if (VM_FF_IS_SET(pVM, VM_FF_PDM_DMA))
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchDma);
            Log4Func(("Pending DMA request forcing us back to ring-3\n"));
            return VINF_EM_RAW_TO_R3;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Does the preparations before executing guest code in AMD-V.
 *
 * This may cause longjmps to ring-3 and may even result in rescheduling to the
 * recompiler. We must be cautious what we do here regarding committing
 * guest-state information into the VMCB assuming we assuredly execute the guest
 * in AMD-V. If we fall back to the recompiler after updating the VMCB and
 * clearing the common-state (TRPM/forceflags), we must undo those changes so
 * that the recompiler can (and should) use them when it resumes guest
 * execution. Otherwise such operations must be done when we can no longer
 * exit to ring-3.
 *
 * @returns Strict VBox status code (informational status codes included).
 * @retval VINF_SUCCESS if we can proceed with running the guest.
 * @retval VINF_* scheduling changes, we have to go back to ring-3.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM transient structure.
 */
static VBOXSTRICTRC hmR0SvmPreRunGuest(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_ASSERT_PREEMPT_SAFE(pVCpu);

#ifdef VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM
    if (pSvmTransient->fIsNestedGuest)
    {
        Log2(("hmR0SvmPreRunGuest: Rescheduling to IEM due to nested-hwvirt or forced IEM exec -> VINF_EM_RESCHEDULE_REM\n"));
        return VINF_EM_RESCHEDULE_REM;
    }
#endif

    /* Check force flag actions that might require us to go back to ring-3. */
    VBOXSTRICTRC rc = hmR0SvmCheckForceFlags(pVCpu);
    if (rc != VINF_SUCCESS)
        return rc;

    if (TRPMHasTrap(pVCpu))
        hmR0SvmTrpmTrapToPendingEvent(pVCpu);
    else if (!pVCpu->hm.s.Event.fPending)
    {
        rc = hmR0SvmEvaluatePendingEvent(pVCpu, pSvmTransient);
        if (   rc != VINF_SUCCESS
            || pSvmTransient->fIsNestedGuest != CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx))
        {
            /* If a nested-guest VM-exit occurred, bail. */
            if (pSvmTransient->fIsNestedGuest)
                STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchNstGstVmexit);
            return rc;
        }
    }

    /*
     * On the oldest AMD-V systems, we may not get enough information to reinject an NMI.
     * Just do it in software, see @bugref{8411}.
     * NB: If we could continue a task switch exit we wouldn't need to do this.
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (RT_UNLIKELY(   !g_fHmSvmFeatures
                    &&  pVCpu->hm.s.Event.fPending
                    &&  SVM_EVENT_GET_TYPE(pVCpu->hm.s.Event.u64IntInfo) == SVM_EVENT_NMI))
        return VINF_EM_RAW_INJECT_TRPM_EVENT;

#ifdef HMSVM_SYNC_FULL_GUEST_STATE
    Assert(!(pVCpu->cpum.GstCtx.fExtrn & HMSVM_CPUMCTX_EXTRN_ALL));
    ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_ALL_GUEST);
#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    /*
     * Set up the nested-guest VMCB for execution using hardware-assisted SVM.
     */
    if (pSvmTransient->fIsNestedGuest)
        hmR0SvmSetupVmcbNested(pVCpu);
#endif

    /*
     * Export the guest state bits that are not shared with the host in any way as we can
     * longjmp or get preempted in the midst of exporting some of the state.
     */
    rc = hmR0SvmExportGuestState(pVCpu, pSvmTransient);
    AssertRCReturn(rc, rc);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExportFull);

    /* Ensure we've cached (and hopefully modified) the nested-guest VMCB for execution using hardware-assisted SVM. */
    Assert(!pSvmTransient->fIsNestedGuest || pVCpu->hm.s.svm.NstGstVmcbCache.fCacheValid);

    /*
     * If we're not intercepting TPR changes in the guest, save the guest TPR before the
     * world-switch so we can update it on the way back if the guest changed the TPR.
     */
    if (pVCpu->hmr0.s.svm.fSyncVTpr)
    {
        Assert(!pSvmTransient->fIsNestedGuest);
        PCSVMVMCB pVmcb = pVCpu->hmr0.s.svm.pVmcb;
        if (pVM->hm.s.fTprPatchingActive)
            pSvmTransient->u8GuestTpr = pVmcb->guest.u64LSTAR;
        else
            pSvmTransient->u8GuestTpr = pVmcb->ctrl.IntCtrl.n.u8VTPR;
    }

    /*
     * No longjmps to ring-3 from this point on!!!
     *
     * Asserts() will still longjmp to ring-3 (but won't return), which is intentional,
     * better than a kernel panic. This also disables flushing of the R0-logger instance.
     */
    VMMRZCallRing3Disable(pVCpu);

    /*
     * We disable interrupts so that we don't miss any interrupts that would flag preemption
     * (IPI/timers etc.) when thread-context hooks aren't used and we've been running with
     * preemption disabled for a while.  Since this is purly to aid the
     * RTThreadPreemptIsPending() code, it doesn't matter that it may temporarily reenable and
     * disable interrupt on NT.
     *
     * We need to check for force-flags that could've possible been altered since we last
     * checked them (e.g. by PDMGetInterrupt() leaving the PDM critical section,
     * see @bugref{6398}).
     *
     * We also check a couple of other force-flags as a last opportunity to get the EMT back
     * to ring-3 before executing guest code.
     */
    pSvmTransient->fEFlags = ASMIntDisableFlags();
    if (   VM_FF_IS_ANY_SET(pVM, VM_FF_EMT_RENDEZVOUS | VM_FF_TM_VIRTUAL_SYNC)
        || VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HM_TO_R3_MASK))
    {
        ASMSetFlags(pSvmTransient->fEFlags);
        VMMRZCallRing3Enable(pVCpu);
        STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchHmToR3FF);
        return VINF_EM_RAW_TO_R3;
    }
    if (RTThreadPreemptIsPending(NIL_RTTHREAD))
    {
        ASMSetFlags(pSvmTransient->fEFlags);
        VMMRZCallRing3Enable(pVCpu);
        STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchPendingHostIrq);
        return VINF_EM_RAW_INTERRUPT;
    }

    return VINF_SUCCESS;
}


/**
 * Prepares to run guest (or nested-guest) code in AMD-V and we've committed to
 * doing so.
 *
 * This means there is no backing out to ring-3 or anywhere else at this point.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM transient structure.
 *
 * @remarks Called with preemption disabled.
 * @remarks No-long-jump zone!!!
 */
static void hmR0SvmPreRunGuestCommitted(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    Assert(!VMMRZCallRing3IsEnabled(pVCpu));
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    VMCPU_ASSERT_STATE(pVCpu, VMCPUSTATE_STARTED_HM);
    VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC);            /* Indicate the start of guest execution. */

    PVMCC      pVM = pVCpu->CTX_SUFF(pVM);
    PSVMVMCB pVmcb = pSvmTransient->pVmcb;

    hmR0SvmInjectPendingEvent(pVCpu, pVmcb);

    if (!CPUMIsGuestFPUStateActive(pVCpu))
    {
        STAM_PROFILE_ADV_START(&pVCpu->hm.s.StatLoadGuestFpuState, x);
        CPUMR0LoadGuestFPU(pVM, pVCpu);     /* (Ignore rc, no need to set HM_CHANGED_HOST_CONTEXT for SVM.) */
        STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatLoadGuestFpuState, x);
        STAM_COUNTER_INC(&pVCpu->hm.s.StatLoadGuestFpu);
    }

    /* Load the state shared between host and guest (FPU, debug). */
    if (pVCpu->hm.s.fCtxChanged & HM_CHANGED_SVM_HOST_GUEST_SHARED_STATE)
        hmR0SvmExportSharedState(pVCpu, pVmcb);

    pVCpu->hm.s.fCtxChanged &= ~HM_CHANGED_HOST_CONTEXT;        /* Preemption might set this, nothing to do on AMD-V. */
    AssertMsg(!pVCpu->hm.s.fCtxChanged, ("fCtxChanged=%#RX64\n", pVCpu->hm.s.fCtxChanged));

    PHMPHYSCPU    pHostCpu         = hmR0GetCurrentCpu();
    RTCPUID const idHostCpu        = pHostCpu->idCpu;
    bool const    fMigratedHostCpu = idHostCpu != pVCpu->hmr0.s.idLastCpu;

    /* Setup TSC offsetting. */
    if (   pSvmTransient->fUpdateTscOffsetting
        || fMigratedHostCpu)
    {
        hmR0SvmUpdateTscOffsetting(pVCpu, pVmcb);
        pSvmTransient->fUpdateTscOffsetting = false;
    }

    /* Record statistics of how often we use TSC offsetting as opposed to intercepting RDTSC/P. */
    if (!(pVmcb->ctrl.u64InterceptCtrl & (SVM_CTRL_INTERCEPT_RDTSC | SVM_CTRL_INTERCEPT_RDTSCP)))
        STAM_COUNTER_INC(&pVCpu->hm.s.StatTscOffset);
    else
        STAM_COUNTER_INC(&pVCpu->hm.s.StatTscIntercept);

    /* If we've migrating CPUs, mark the VMCB Clean bits as dirty. */
    if (fMigratedHostCpu)
        pVmcb->ctrl.u32VmcbCleanBits = 0;

    /* Store status of the shared guest-host state at the time of VMRUN. */
    pSvmTransient->fWasGuestDebugStateActive = CPUMIsGuestDebugStateActive(pVCpu);
    pSvmTransient->fWasHyperDebugStateActive = CPUMIsHyperDebugStateActive(pVCpu);

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    uint8_t *pbMsrBitmap;
    if (!pSvmTransient->fIsNestedGuest)
        pbMsrBitmap = (uint8_t *)pVCpu->hmr0.s.svm.pvMsrBitmap;
    else
    {
        /** @todo We could perhaps optimize this by monitoring if the guest modifies its
         *        MSRPM and only perform this if it changed also use EVEX.POR when it
         *        does. */
        hmR0SvmMergeMsrpmNested(pHostCpu, pVCpu);

        /* Update the nested-guest VMCB with the newly merged MSRPM (clean bits updated below). */
        pVmcb->ctrl.u64MSRPMPhysAddr = pHostCpu->n.svm.HCPhysNstGstMsrpm;
        pbMsrBitmap = (uint8_t *)pHostCpu->n.svm.pvNstGstMsrpm;
    }
#else
    uint8_t *pbMsrBitmap = (uint8_t *)pVCpu->hm.s.svm.pvMsrBitmap;
#endif

    ASMAtomicUoWriteBool(&pVCpu->hm.s.fCheckedTLBFlush, true);  /* Used for TLB flushing, set this across the world switch. */
    /* Flush the appropriate tagged-TLB entries. */
    hmR0SvmFlushTaggedTlb(pHostCpu,  pVCpu, pVmcb);
    Assert(pVCpu->hmr0.s.idLastCpu == idHostCpu);

    STAM_PROFILE_ADV_STOP_START(&pVCpu->hm.s.StatEntry, &pVCpu->hm.s.StatInGC, x);

    TMNotifyStartOfExecution(pVM, pVCpu);                       /* Finally, notify TM to resume its clocks as we're about
                                                                   to start executing. */

    /*
     * Save the current Host TSC_AUX and write the guest TSC_AUX to the host, so that RDTSCPs
     * (that don't cause exits) reads the guest MSR, see @bugref{3324}.
     *
     * This should be done -after- any RDTSCPs for obtaining the host timestamp (TM, STAM etc).
     */
    if (   g_CpumHostFeatures.s.fRdTscP
        && !(pVmcb->ctrl.u64InterceptCtrl & SVM_CTRL_INTERCEPT_RDTSCP))
    {
        uint64_t const uGuestTscAux = CPUMGetGuestTscAux(pVCpu);
        pVCpu->hmr0.s.svm.u64HostTscAux = ASMRdMsr(MSR_K8_TSC_AUX);
        if (uGuestTscAux != pVCpu->hmr0.s.svm.u64HostTscAux)
            ASMWrMsr(MSR_K8_TSC_AUX, uGuestTscAux);
        hmR0SvmSetMsrPermission(pVCpu, pbMsrBitmap, MSR_K8_TSC_AUX, SVMMSREXIT_PASSTHRU_READ, SVMMSREXIT_PASSTHRU_WRITE);
        pSvmTransient->fRestoreTscAuxMsr = true;
    }
    else
    {
        hmR0SvmSetMsrPermission(pVCpu, pbMsrBitmap, MSR_K8_TSC_AUX, SVMMSREXIT_INTERCEPT_READ, SVMMSREXIT_INTERCEPT_WRITE);
        pSvmTransient->fRestoreTscAuxMsr = false;
    }
    pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_IOPM_MSRPM;

    /*
     * If VMCB Clean bits isn't supported by the CPU or exposed to the guest in the nested
     * virtualization case, mark all state-bits as dirty indicating to the CPU to re-load
     * from the VMCB.
     */
    bool const fSupportsVmcbCleanBits = hmR0SvmSupportsVmcbCleanBits(pVCpu, pSvmTransient->fIsNestedGuest);
    if (!fSupportsVmcbCleanBits)
        pVmcb->ctrl.u32VmcbCleanBits = 0;
}


/**
 * Wrapper for running the guest (or nested-guest) code in AMD-V.
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   HCPhysVmcb  The host physical address of the VMCB.
 *
 * @remarks No-long-jump zone!!!
 */
DECLINLINE(int) hmR0SvmRunGuest(PVMCPUCC pVCpu, RTHCPHYS HCPhysVmcb)
{
    /* Mark that HM is the keeper of all guest-CPU registers now that we're going to execute guest code. */
    pVCpu->cpum.GstCtx.fExtrn |= HMSVM_CPUMCTX_EXTRN_ALL | CPUMCTX_EXTRN_KEEPER_HM;
    return pVCpu->hmr0.s.svm.pfnVMRun(pVCpu->CTX_SUFF(pVM), pVCpu, HCPhysVmcb);
}


/**
 * Performs some essential restoration of state after running guest (or
 * nested-guest) code in AMD-V.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM transient structure.
 * @param   rcVMRun         Return code of VMRUN.
 *
 * @remarks Called with interrupts disabled.
 * @remarks No-long-jump zone!!! This function will however re-enable longjmps
 *          unconditionally when it is safe to do so.
 */
static void hmR0SvmPostRunGuest(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient, VBOXSTRICTRC rcVMRun)
{
    Assert(!VMMRZCallRing3IsEnabled(pVCpu));

    ASMAtomicUoWriteBool(&pVCpu->hm.s.fCheckedTLBFlush, false); /* See HMInvalidatePageOnAllVCpus(): used for TLB flushing. */
    ASMAtomicIncU32(&pVCpu->hmr0.s.cWorldSwitchExits);          /* Initialized in vmR3CreateUVM(): used for EMT poking. */

    PSVMVMCB     pVmcb     = pSvmTransient->pVmcb;
    PSVMVMCBCTRL pVmcbCtrl = &pVmcb->ctrl;

    /* TSC read must be done early for maximum accuracy. */
    if (!(pVmcbCtrl->u64InterceptCtrl & SVM_CTRL_INTERCEPT_RDTSC))
    {
        if (!pSvmTransient->fIsNestedGuest)
            TMCpuTickSetLastSeen(pVCpu, pVCpu->hmr0.s.uTscExit + pVmcbCtrl->u64TSCOffset);
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        else
        {
            /* The nested-guest VMCB TSC offset shall eventually be restored on #VMEXIT via HMNotifySvmNstGstVmexit(). */
            uint64_t const uGstTsc = CPUMRemoveNestedGuestTscOffset(pVCpu, pVCpu->hmr0.s.uTscExit + pVmcbCtrl->u64TSCOffset);
            TMCpuTickSetLastSeen(pVCpu, uGstTsc);
        }
#endif
    }

    if (pSvmTransient->fRestoreTscAuxMsr)
    {
        uint64_t u64GuestTscAuxMsr = ASMRdMsr(MSR_K8_TSC_AUX);
        CPUMSetGuestTscAux(pVCpu, u64GuestTscAuxMsr);
        if (u64GuestTscAuxMsr != pVCpu->hmr0.s.svm.u64HostTscAux)
            ASMWrMsr(MSR_K8_TSC_AUX, pVCpu->hmr0.s.svm.u64HostTscAux);
    }

    STAM_PROFILE_ADV_STOP_START(&pVCpu->hm.s.StatInGC, &pVCpu->hm.s.StatPreExit, x);
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    TMNotifyEndOfExecution(pVM, pVCpu, pVCpu->hmr0.s.uTscExit); /* Notify TM that the guest is no longer running. */
    VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STARTED_HM);

    Assert(!(ASMGetFlags() & X86_EFL_IF));
    ASMSetFlags(pSvmTransient->fEFlags);                        /* Enable interrupts. */
    VMMRZCallRing3Enable(pVCpu);                                /* It is now safe to do longjmps to ring-3!!! */

    /* If VMRUN failed, we can bail out early. This does -not- cover SVM_EXIT_INVALID. */
    if (RT_UNLIKELY(rcVMRun != VINF_SUCCESS))
    {
        Log4Func(("VMRUN failure: rcVMRun=%Rrc\n", VBOXSTRICTRC_VAL(rcVMRun)));
        return;
    }

    pSvmTransient->u64ExitCode        = pVmcbCtrl->u64ExitCode; /* Save the #VMEXIT reason. */
    pSvmTransient->fVectoringDoublePF = false;                  /* Vectoring double page-fault needs to be determined later. */
    pSvmTransient->fVectoringPF       = false;                  /* Vectoring page-fault needs to be determined later. */
    pVmcbCtrl->u32VmcbCleanBits       = HMSVM_VMCB_CLEAN_ALL;   /* Mark the VMCB-state cache as unmodified by VMM. */

#ifdef HMSVM_SYNC_FULL_GUEST_STATE
    hmR0SvmImportGuestState(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
    Assert(!(pVCpu->cpum.GstCtx.fExtrn & HMSVM_CPUMCTX_EXTRN_ALL));
#else
    /*
     * Always import the following:
     *
     *   - RIP for exit optimizations and evaluating event injection on re-entry.
     *   - RFLAGS for evaluating event injection on VM re-entry and for exporting shared debug
     *     state on preemption.
     *   - Interrupt shadow, GIF for evaluating event injection on VM re-entry.
     *   - CS for exit optimizations.
     *   - RAX, RSP for simplifying assumptions on GPRs. All other GPRs are swapped by the
     *     assembly switcher code.
     *   - Shared state (only DR7 currently) for exporting shared debug state on preemption.
     */
    hmR0SvmImportGuestState(pVCpu, CPUMCTX_EXTRN_RIP
                                 | CPUMCTX_EXTRN_RFLAGS
                                 | CPUMCTX_EXTRN_RAX
                                 | CPUMCTX_EXTRN_RSP
                                 | CPUMCTX_EXTRN_CS
                                 | CPUMCTX_EXTRN_HWVIRT
                                 | CPUMCTX_EXTRN_INHIBIT_INT
                                 | CPUMCTX_EXTRN_HM_SVM_HWVIRT_VIRQ
                                 | HMSVM_CPUMCTX_SHARED_STATE);
#endif

    if (   pSvmTransient->u64ExitCode != SVM_EXIT_INVALID
        && pVCpu->hmr0.s.svm.fSyncVTpr)
    {
        Assert(!pSvmTransient->fIsNestedGuest);
        /* TPR patching (for 32-bit guests) uses LSTAR MSR for holding the TPR value, otherwise uses the VTPR. */
        if (   pVM->hm.s.fTprPatchingActive
            && (pVmcb->guest.u64LSTAR & 0xff) != pSvmTransient->u8GuestTpr)
        {
            int rc = APICSetTpr(pVCpu, pVmcb->guest.u64LSTAR & 0xff);
            AssertRC(rc);
            ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_APIC_TPR);
        }
        /* Sync TPR when we aren't intercepting CR8 writes. */
        else if (pSvmTransient->u8GuestTpr != pVmcbCtrl->IntCtrl.n.u8VTPR)
        {
            int rc = APICSetTpr(pVCpu, pVmcbCtrl->IntCtrl.n.u8VTPR << 4);
            AssertRC(rc);
            ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_APIC_TPR);
        }
    }

#ifdef DEBUG_ramshankar
    if (CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx))
    {
        hmR0SvmImportGuestState(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
        hmR0SvmLogState(pVCpu, pVmcb, pVCpu->cpum.GstCtx, "hmR0SvmPostRunGuestNested", HMSVM_LOG_ALL & ~HMSVM_LOG_LBR,
                        0 /* uVerbose */);
    }
#endif

    HMSVM_CPUMCTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP);
    EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_SVM, pSvmTransient->u64ExitCode & EMEXIT_F_TYPE_MASK),
                     pVCpu->cpum.GstCtx.cs.u64Base + pVCpu->cpum.GstCtx.rip, pVCpu->hmr0.s.uTscExit);
}


/**
 * Runs the guest code using AMD-V.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pcLoops     Pointer to the number of executed loops.
 */
static VBOXSTRICTRC hmR0SvmRunGuestCodeNormal(PVMCPUCC pVCpu, uint32_t *pcLoops)
{
    uint32_t const cMaxResumeLoops = pVCpu->CTX_SUFF(pVM)->hmr0.s.cMaxResumeLoops;
    Assert(pcLoops);
    Assert(*pcLoops <= cMaxResumeLoops);

    SVMTRANSIENT SvmTransient;
    RT_ZERO(SvmTransient);
    SvmTransient.fUpdateTscOffsetting = true;
    SvmTransient.pVmcb = pVCpu->hmr0.s.svm.pVmcb;

    VBOXSTRICTRC rc = VERR_INTERNAL_ERROR_5;
    for (;;)
    {
        Assert(!HMR0SuspendPending());
        HMSVM_ASSERT_CPU_SAFE(pVCpu);

        /* Preparatory work for running nested-guest code, this may force us to return to
           ring-3.  This bugger disables interrupts on VINF_SUCCESS! */
        STAM_PROFILE_ADV_START(&pVCpu->hm.s.StatEntry, x);
        rc = hmR0SvmPreRunGuest(pVCpu, &SvmTransient);
        if (rc != VINF_SUCCESS)
            break;

        /*
         * No longjmps to ring-3 from this point on!!!
         *
         * Asserts() will still longjmp to ring-3 (but won't return), which is intentional,
         * better than a kernel panic. This also disables flushing of the R0-logger instance.
         */
        hmR0SvmPreRunGuestCommitted(pVCpu, &SvmTransient);
        rc = hmR0SvmRunGuest(pVCpu, pVCpu->hmr0.s.svm.HCPhysVmcb);

        /* Restore any residual host-state and save any bits shared between host and guest
           into the guest-CPU state.  Re-enables interrupts! */
        hmR0SvmPostRunGuest(pVCpu, &SvmTransient, rc);

        if (RT_UNLIKELY(   rc != VINF_SUCCESS                               /* Check for VMRUN errors. */
                        || SvmTransient.u64ExitCode == SVM_EXIT_INVALID))   /* Check for invalid guest-state errors. */
        {
            if (rc == VINF_SUCCESS)
                rc = VERR_SVM_INVALID_GUEST_STATE;
            STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatPreExit, x);
            hmR0SvmReportWorldSwitchError(pVCpu, VBOXSTRICTRC_VAL(rc));
            break;
        }

        /* Handle the #VMEXIT. */
        HMSVM_EXITCODE_STAM_COUNTER_INC(SvmTransient.u64ExitCode);
        STAM_PROFILE_ADV_STOP_START(&pVCpu->hm.s.StatPreExit, &pVCpu->hm.s.StatExitHandling, x);
        VBOXVMM_R0_HMSVM_VMEXIT(pVCpu, &pVCpu->cpum.GstCtx, SvmTransient.u64ExitCode, pVCpu->hmr0.s.svm.pVmcb);
        rc = hmR0SvmHandleExit(pVCpu, &SvmTransient);
        STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatExitHandling, x);
        if (rc != VINF_SUCCESS)
            break;
        if (++(*pcLoops) >= cMaxResumeLoops)
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchMaxResumeLoops);
            rc = VINF_EM_RAW_INTERRUPT;
            break;
        }
    }

    STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatEntry, x);
    return rc;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
/**
 * Runs the nested-guest code using AMD-V.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pcLoops     Pointer to the number of executed loops. If we're switching
 *                      from the guest-code execution loop to this nested-guest
 *                      execution loop pass the remainder value, else pass 0.
 */
static VBOXSTRICTRC hmR0SvmRunGuestCodeNested(PVMCPUCC pVCpu, uint32_t *pcLoops)
{
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    HMSVM_ASSERT_IN_NESTED_GUEST(pCtx);
    Assert(pcLoops);
    Assert(*pcLoops <= pVCpu->CTX_SUFF(pVM)->hmr0.s.cMaxResumeLoops);
    /** @todo r=bird: Sharing this with ring-3 isn't safe in the long run, I fear... */
    RTHCPHYS const HCPhysVmcb = GVMMR0ConvertGVMPtr2HCPhys(pVCpu->pGVM, &pCtx->hwvirt.svm.Vmcb);

    SVMTRANSIENT SvmTransient;
    RT_ZERO(SvmTransient);
    SvmTransient.fUpdateTscOffsetting = true;
    SvmTransient.pVmcb = &pCtx->hwvirt.svm.Vmcb;
    SvmTransient.fIsNestedGuest = true;

    /* Setup pointer so PGM/IEM can query #VMEXIT auxiliary info. on demand in ring-0. */
    pVCpu->hmr0.s.svm.pSvmTransient = &SvmTransient;

    VBOXSTRICTRC rc = VERR_INTERNAL_ERROR_4;
    for (;;)
    {
        Assert(!HMR0SuspendPending());
        HMSVM_ASSERT_CPU_SAFE(pVCpu);

        /* Preparatory work for running nested-guest code, this may force us to return to
           ring-3.  This bugger disables interrupts on VINF_SUCCESS! */
        STAM_PROFILE_ADV_START(&pVCpu->hm.s.StatEntry, x);
        rc = hmR0SvmPreRunGuest(pVCpu, &SvmTransient);
        if (    rc != VINF_SUCCESS
            || !CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
            break;

        /*
         * No longjmps to ring-3 from this point on!!!
         *
         * Asserts() will still longjmp to ring-3 (but won't return), which is intentional,
         * better than a kernel panic. This also disables flushing of the R0-logger instance.
         */
        hmR0SvmPreRunGuestCommitted(pVCpu, &SvmTransient);

        rc = hmR0SvmRunGuest(pVCpu, HCPhysVmcb);

        /* Restore any residual host-state and save any bits shared between host and guest
           into the guest-CPU state.  Re-enables interrupts! */
        hmR0SvmPostRunGuest(pVCpu, &SvmTransient, rc);

        if (RT_LIKELY(   rc == VINF_SUCCESS
                      && SvmTransient.u64ExitCode != SVM_EXIT_INVALID))
        { /* extremely likely */ }
        else
        {
            /* VMRUN failed, shouldn't really happen, Guru. */
            if (rc != VINF_SUCCESS)
                break;

            /* Invalid nested-guest state. Cause a #VMEXIT but assert on strict builds. */
            HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
            AssertMsgFailed(("Invalid nested-guest state. rc=%Rrc u64ExitCode=%#RX64\n", rc, SvmTransient.u64ExitCode));
            rc = IEMExecSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0, 0);
            break;
        }

        /* Handle the #VMEXIT. */
        HMSVM_NESTED_EXITCODE_STAM_COUNTER_INC(SvmTransient.u64ExitCode);
        STAM_PROFILE_ADV_STOP_START(&pVCpu->hm.s.StatPreExit, &pVCpu->hm.s.StatExitHandling, x);
        VBOXVMM_R0_HMSVM_VMEXIT(pVCpu, pCtx, SvmTransient.u64ExitCode, &pCtx->hwvirt.svm.Vmcb);
        rc = hmR0SvmHandleExitNested(pVCpu, &SvmTransient);
        STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatExitHandling, x);
        if (rc == VINF_SUCCESS)
        {
            if (!CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
            {
                STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchNstGstVmexit);
                rc = VINF_SVM_VMEXIT;
            }
            else
            {
                if (++(*pcLoops) <= pVCpu->CTX_SUFF(pVM)->hmr0.s.cMaxResumeLoops)
                    continue;
                STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchMaxResumeLoops);
                rc = VINF_EM_RAW_INTERRUPT;
            }
        }
        else
            Assert(rc != VINF_SVM_VMEXIT);
        break;
        /** @todo NSTSVM: handle single-stepping. */
    }

    /* Ensure #VMEXIT auxiliary info. is no longer available. */
    pVCpu->hmr0.s.svm.pSvmTransient = NULL;

    STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatEntry, x);
    return rc;
}
#endif /* VBOX_WITH_NESTED_HWVIRT_SVM */


/**
 * Checks if any expensive dtrace probes are enabled and we should go to the
 * debug loop.
 *
 * @returns true if we should use debug loop, false if not.
 */
static bool hmR0SvmAnyExpensiveProbesEnabled(void)
{
    /* It's probably faster to OR the raw 32-bit counter variables together.
       Since the variables are in an array and the probes are next to one
       another (more or less), we have good locality.  So, better read
       eight-nine cache lines ever time and only have one conditional, than
       128+ conditionals, right? */
    return (  VBOXVMM_R0_HMSVM_VMEXIT_ENABLED_RAW() /* expensive too due to context */
            | VBOXVMM_XCPT_DE_ENABLED_RAW()
            | VBOXVMM_XCPT_DB_ENABLED_RAW()
            | VBOXVMM_XCPT_BP_ENABLED_RAW()
            | VBOXVMM_XCPT_OF_ENABLED_RAW()
            | VBOXVMM_XCPT_BR_ENABLED_RAW()
            | VBOXVMM_XCPT_UD_ENABLED_RAW()
            | VBOXVMM_XCPT_NM_ENABLED_RAW()
            | VBOXVMM_XCPT_DF_ENABLED_RAW()
            | VBOXVMM_XCPT_TS_ENABLED_RAW()
            | VBOXVMM_XCPT_NP_ENABLED_RAW()
            | VBOXVMM_XCPT_SS_ENABLED_RAW()
            | VBOXVMM_XCPT_GP_ENABLED_RAW()
            | VBOXVMM_XCPT_PF_ENABLED_RAW()
            | VBOXVMM_XCPT_MF_ENABLED_RAW()
            | VBOXVMM_XCPT_AC_ENABLED_RAW()
            | VBOXVMM_XCPT_XF_ENABLED_RAW()
            | VBOXVMM_XCPT_VE_ENABLED_RAW()
            | VBOXVMM_XCPT_SX_ENABLED_RAW()
            | VBOXVMM_INT_SOFTWARE_ENABLED_RAW()
            | VBOXVMM_INT_HARDWARE_ENABLED_RAW()
           ) != 0
        || (  VBOXVMM_INSTR_HALT_ENABLED_RAW()
            | VBOXVMM_INSTR_MWAIT_ENABLED_RAW()
            | VBOXVMM_INSTR_MONITOR_ENABLED_RAW()
            | VBOXVMM_INSTR_CPUID_ENABLED_RAW()
            | VBOXVMM_INSTR_INVD_ENABLED_RAW()
            | VBOXVMM_INSTR_WBINVD_ENABLED_RAW()
            | VBOXVMM_INSTR_INVLPG_ENABLED_RAW()
            | VBOXVMM_INSTR_RDTSC_ENABLED_RAW()
            | VBOXVMM_INSTR_RDTSCP_ENABLED_RAW()
            | VBOXVMM_INSTR_RDPMC_ENABLED_RAW()
            | VBOXVMM_INSTR_RDMSR_ENABLED_RAW()
            | VBOXVMM_INSTR_WRMSR_ENABLED_RAW()
            | VBOXVMM_INSTR_CRX_READ_ENABLED_RAW()
            | VBOXVMM_INSTR_CRX_WRITE_ENABLED_RAW()
            | VBOXVMM_INSTR_DRX_READ_ENABLED_RAW()
            | VBOXVMM_INSTR_DRX_WRITE_ENABLED_RAW()
            | VBOXVMM_INSTR_PAUSE_ENABLED_RAW()
            | VBOXVMM_INSTR_XSETBV_ENABLED_RAW()
            | VBOXVMM_INSTR_SIDT_ENABLED_RAW()
            | VBOXVMM_INSTR_LIDT_ENABLED_RAW()
            | VBOXVMM_INSTR_SGDT_ENABLED_RAW()
            | VBOXVMM_INSTR_LGDT_ENABLED_RAW()
            | VBOXVMM_INSTR_SLDT_ENABLED_RAW()
            | VBOXVMM_INSTR_LLDT_ENABLED_RAW()
            | VBOXVMM_INSTR_STR_ENABLED_RAW()
            | VBOXVMM_INSTR_LTR_ENABLED_RAW()
            //| VBOXVMM_INSTR_GETSEC_ENABLED_RAW()
            | VBOXVMM_INSTR_RSM_ENABLED_RAW()
            //| VBOXVMM_INSTR_RDRAND_ENABLED_RAW()
            //| VBOXVMM_INSTR_RDSEED_ENABLED_RAW()
            //| VBOXVMM_INSTR_XSAVES_ENABLED_RAW()
            //| VBOXVMM_INSTR_XRSTORS_ENABLED_RAW()
            | VBOXVMM_INSTR_VMM_CALL_ENABLED_RAW()
            | VBOXVMM_INSTR_SVM_VMRUN_ENABLED_RAW()
            | VBOXVMM_INSTR_SVM_VMLOAD_ENABLED_RAW()
            | VBOXVMM_INSTR_SVM_VMSAVE_ENABLED_RAW()
            | VBOXVMM_INSTR_SVM_STGI_ENABLED_RAW()
            | VBOXVMM_INSTR_SVM_CLGI_ENABLED_RAW()
           ) != 0
        || (  VBOXVMM_EXIT_TASK_SWITCH_ENABLED_RAW()
            | VBOXVMM_EXIT_HALT_ENABLED_RAW()
            | VBOXVMM_EXIT_MWAIT_ENABLED_RAW()
            | VBOXVMM_EXIT_MONITOR_ENABLED_RAW()
            | VBOXVMM_EXIT_CPUID_ENABLED_RAW()
            | VBOXVMM_EXIT_INVD_ENABLED_RAW()
            | VBOXVMM_EXIT_WBINVD_ENABLED_RAW()
            | VBOXVMM_EXIT_INVLPG_ENABLED_RAW()
            | VBOXVMM_EXIT_RDTSC_ENABLED_RAW()
            | VBOXVMM_EXIT_RDTSCP_ENABLED_RAW()
            | VBOXVMM_EXIT_RDPMC_ENABLED_RAW()
            | VBOXVMM_EXIT_RDMSR_ENABLED_RAW()
            | VBOXVMM_EXIT_WRMSR_ENABLED_RAW()
            | VBOXVMM_EXIT_CRX_READ_ENABLED_RAW()
            | VBOXVMM_EXIT_CRX_WRITE_ENABLED_RAW()
            | VBOXVMM_EXIT_DRX_READ_ENABLED_RAW()
            | VBOXVMM_EXIT_DRX_WRITE_ENABLED_RAW()
            | VBOXVMM_EXIT_PAUSE_ENABLED_RAW()
            | VBOXVMM_EXIT_XSETBV_ENABLED_RAW()
            | VBOXVMM_EXIT_SIDT_ENABLED_RAW()
            | VBOXVMM_EXIT_LIDT_ENABLED_RAW()
            | VBOXVMM_EXIT_SGDT_ENABLED_RAW()
            | VBOXVMM_EXIT_LGDT_ENABLED_RAW()
            | VBOXVMM_EXIT_SLDT_ENABLED_RAW()
            | VBOXVMM_EXIT_LLDT_ENABLED_RAW()
            | VBOXVMM_EXIT_STR_ENABLED_RAW()
            | VBOXVMM_EXIT_LTR_ENABLED_RAW()
            //| VBOXVMM_EXIT_GETSEC_ENABLED_RAW()
            | VBOXVMM_EXIT_RSM_ENABLED_RAW()
            //| VBOXVMM_EXIT_RDRAND_ENABLED_RAW()
            //| VBOXVMM_EXIT_RDSEED_ENABLED_RAW()
            //| VBOXVMM_EXIT_XSAVES_ENABLED_RAW()
            //| VBOXVMM_EXIT_XRSTORS_ENABLED_RAW()
            | VBOXVMM_EXIT_VMM_CALL_ENABLED_RAW()
            | VBOXVMM_EXIT_SVM_VMRUN_ENABLED_RAW()
            | VBOXVMM_EXIT_SVM_VMLOAD_ENABLED_RAW()
            | VBOXVMM_EXIT_SVM_VMSAVE_ENABLED_RAW()
            | VBOXVMM_EXIT_SVM_STGI_ENABLED_RAW()
            | VBOXVMM_EXIT_SVM_CLGI_ENABLED_RAW()
           ) != 0;
}


/**
 * Runs the guest code using AMD-V.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR0DECL(VBOXSTRICTRC) SVMR0RunGuestCode(PVMCPUCC pVCpu)
{
    AssertPtr(pVCpu);
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    Assert(VMMRZCallRing3IsEnabled(pVCpu));
    Assert(!ASMAtomicUoReadU64(&pCtx->fExtrn));
    HMSVM_ASSERT_PREEMPT_SAFE(pVCpu);

    uint32_t cLoops = 0;
    VBOXSTRICTRC rc;
    for (;;)
    {
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        bool const fInNestedGuestMode = CPUMIsGuestInSvmNestedHwVirtMode(pCtx);
#else
        NOREF(pCtx);
        bool const fInNestedGuestMode = false;
#endif
        if (!fInNestedGuestMode)
        {
            if (   !pVCpu->hm.s.fUseDebugLoop
                && (!VBOXVMM_ANY_PROBES_ENABLED() || !hmR0SvmAnyExpensiveProbesEnabled())
                && !DBGFIsStepping(pVCpu)
                && !pVCpu->CTX_SUFF(pVM)->dbgf.ro.cEnabledInt3Breakpoints)
                rc = hmR0SvmRunGuestCodeNormal(pVCpu, &cLoops);
            else
                rc = hmR0SvmRunGuestCodeDebug(pVCpu, &cLoops);
        }
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
        else
            rc = hmR0SvmRunGuestCodeNested(pVCpu, &cLoops);

        if (rc == VINF_SVM_VMRUN)
        {
            Assert(CPUMIsGuestInSvmNestedHwVirtMode(pCtx));
            continue;
        }
        if (rc == VINF_SVM_VMEXIT)
        {
            Assert(!CPUMIsGuestInSvmNestedHwVirtMode(pCtx));
            continue;
        }
#endif
        break;
    }

    /* Fixup error codes. */
    if (rc == VERR_EM_INTERPRETER)
        rc = VINF_EM_RAW_EMULATE_INSTR;
    else if (rc == VINF_EM_RESET)
        rc = VINF_EM_TRIPLE_FAULT;

    /* Prepare to return to ring-3. This will remove longjmp notifications. */
    rc = hmR0SvmExitToRing3(pVCpu, rc);
    Assert(!ASMAtomicUoReadU64(&pCtx->fExtrn));
    Assert(!VMMR0AssertionIsNotificationSet(pVCpu));
    return rc;
}

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM

/**
 * Determines whether the given I/O access should cause a nested-guest \#VMEXIT.
 *
 * @param   pvIoBitmap      Pointer to the nested-guest IO bitmap.
 * @param   pIoExitInfo     Pointer to the SVMIOIOEXITINFO.
 */
static bool hmR0SvmIsIoInterceptSet(void *pvIoBitmap, PSVMIOIOEXITINFO pIoExitInfo)
{
    const uint16_t    u16Port       = pIoExitInfo->n.u16Port;
    const SVMIOIOTYPE enmIoType     = (SVMIOIOTYPE)pIoExitInfo->n.u1Type;
    const uint8_t     cbReg         = (pIoExitInfo->u  >> SVM_IOIO_OP_SIZE_SHIFT)   & 7;
    const uint8_t     cAddrSizeBits = ((pIoExitInfo->u >> SVM_IOIO_ADDR_SIZE_SHIFT) & 7) << 4;
    const uint8_t     iEffSeg       = pIoExitInfo->n.u3Seg;
    const bool        fRep          = pIoExitInfo->n.u1Rep;
    const bool        fStrIo        = pIoExitInfo->n.u1Str;

    return CPUMIsSvmIoInterceptSet(pvIoBitmap, u16Port, enmIoType, cbReg, cAddrSizeBits, iEffSeg, fRep, fStrIo,
                                   NULL /* pIoExitInfo */);
}


/**
 * Handles a nested-guest \#VMEXIT (for all EXITCODE values except
 * SVM_EXIT_INVALID).
 *
 * @returns VBox status code (informational status codes included).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM transient structure.
 */
static VBOXSTRICTRC hmR0SvmHandleExitNested(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_ASSERT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx);
    Assert(pSvmTransient->u64ExitCode != SVM_EXIT_INVALID);
    Assert(pSvmTransient->u64ExitCode <= SVM_EXIT_MAX);

    /*
     * We import the complete state here because we use separate VMCBs for the guest and the
     * nested-guest, and the guest's VMCB is used after the #VMEXIT. We can only save/restore
     * the #VMEXIT specific state if we used the same VMCB for both guest and nested-guest.
     */
#define NST_GST_VMEXIT_CALL_RET(a_pVCpu, a_uExitCode, a_uExitInfo1, a_uExitInfo2) \
    do { \
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL); \
        return IEMExecSvmVmexit((a_pVCpu), (a_uExitCode), (a_uExitInfo1), (a_uExitInfo2)); \
    } while (0)

    /*
     * For all the #VMEXITs here we primarily figure out if the #VMEXIT is expected by the
     * nested-guest. If it isn't, it should be handled by the (outer) guest.
     */
    PSVMVMCB       pVmcbNstGst     = &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb;
    PCCPUMCTX      pCtx            = &pVCpu->cpum.GstCtx;
    PSVMVMCBCTRL   pVmcbNstGstCtrl = &pVmcbNstGst->ctrl;
    uint64_t const uExitCode       = pVmcbNstGstCtrl->u64ExitCode;
    uint64_t const uExitInfo1      = pVmcbNstGstCtrl->u64ExitInfo1;
    uint64_t const uExitInfo2      = pVmcbNstGstCtrl->u64ExitInfo2;

    Assert(uExitCode == pVmcbNstGstCtrl->u64ExitCode);
    switch (uExitCode)
    {
        case SVM_EXIT_CPUID:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_CPUID))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitCpuid(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_RDTSC:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_RDTSC))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitRdtsc(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_RDTSCP:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_RDTSCP))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitRdtscp(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_MONITOR:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_MONITOR))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitMonitor(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_MWAIT:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_MWAIT))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitMwait(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_HLT:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_HLT))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitHlt(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_MSR:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_MSR_PROT))
            {
                uint32_t const idMsr = pVCpu->cpum.GstCtx.ecx;
                uint16_t offMsrpm;
                uint8_t  uMsrpmBit;
                int rc = CPUMGetSvmMsrpmOffsetAndBit(idMsr, &offMsrpm, &uMsrpmBit);
                if (RT_SUCCESS(rc))
                {
                    Assert(uMsrpmBit == 0 || uMsrpmBit == 2 || uMsrpmBit == 4 || uMsrpmBit == 6);
                    Assert(offMsrpm < SVM_MSRPM_PAGES << X86_PAGE_4K_SHIFT);

                    uint8_t const * const pbMsrBitmap = &pVCpu->cpum.GstCtx.hwvirt.svm.abMsrBitmap[offMsrpm];
                    bool const fInterceptRead  = RT_BOOL(*pbMsrBitmap & RT_BIT(uMsrpmBit));
                    bool const fInterceptWrite = RT_BOOL(*pbMsrBitmap & RT_BIT(uMsrpmBit + 1));

                    if (   (fInterceptWrite && pVmcbNstGstCtrl->u64ExitInfo1 == SVM_EXIT1_MSR_WRITE)
                        || (fInterceptRead  && pVmcbNstGstCtrl->u64ExitInfo1 == SVM_EXIT1_MSR_READ))
                    {
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    }
                }
                else
                {
                    /*
                     * MSRs not covered by the MSRPM automatically cause an #VMEXIT.
                     * See AMD-V spec. "15.11 MSR Intercepts".
                     */
                    Assert(rc == VERR_OUT_OF_RANGE);
                    NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                }
            }
            return hmR0SvmExitMsr(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_IOIO:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_IOIO_PROT))
            {
                SVMIOIOEXITINFO IoExitInfo;
                IoExitInfo.u = pVmcbNstGst->ctrl.u64ExitInfo1;
                bool const fIntercept = hmR0SvmIsIoInterceptSet(pVCpu->cpum.GstCtx.hwvirt.svm.abIoBitmap, &IoExitInfo);
                if (fIntercept)
                    NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            }
            return hmR0SvmExitIOInstr(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_XCPT_PF:
        {
            PVMCC pVM = pVCpu->CTX_SUFF(pVM);
            if (pVM->hmr0.s.fNestedPaging)
            {
                uint32_t const u32ErrCode    = pVmcbNstGstCtrl->u64ExitInfo1;
                uint64_t const uFaultAddress = pVmcbNstGstCtrl->u64ExitInfo2;

                /* If the nested-guest is intercepting #PFs, cause a #PF #VMEXIT. */
                if (CPUMIsGuestSvmXcptInterceptSet(pVCpu, pCtx, X86_XCPT_PF))
                    NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, u32ErrCode, uFaultAddress);

                /* If the nested-guest is not intercepting #PFs, forward the #PF to the guest. */
                HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_CR2);
                hmR0SvmSetPendingXcptPF(pVCpu, u32ErrCode, uFaultAddress);
                return VINF_SUCCESS;
            }
            return hmR0SvmExitXcptPF(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_XCPT_UD:
        {
            if (CPUMIsGuestSvmXcptInterceptSet(pVCpu, pCtx, X86_XCPT_UD))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            hmR0SvmSetPendingXcptUD(pVCpu);
            return VINF_SUCCESS;
        }

        case SVM_EXIT_XCPT_MF:
        {
            if (CPUMIsGuestSvmXcptInterceptSet(pVCpu, pCtx, X86_XCPT_MF))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitXcptMF(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_XCPT_DB:
        {
            if (CPUMIsGuestSvmXcptInterceptSet(pVCpu, pCtx, X86_XCPT_DB))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmNestedExitXcptDB(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_XCPT_AC:
        {
            if (CPUMIsGuestSvmXcptInterceptSet(pVCpu, pCtx, X86_XCPT_AC))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitXcptAC(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_XCPT_BP:
        {
            if (CPUMIsGuestSvmXcptInterceptSet(pVCpu, pCtx, X86_XCPT_BP))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmNestedExitXcptBP(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_READ_CR0:
        case SVM_EXIT_READ_CR3:
        case SVM_EXIT_READ_CR4:
        {
            uint8_t const uCr = uExitCode - SVM_EXIT_READ_CR0;
            if (CPUMIsGuestSvmReadCRxInterceptSet(pVCpu, pCtx, uCr))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitReadCRx(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_CR0_SEL_WRITE:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_CR0_SEL_WRITE))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitWriteCRx(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_WRITE_CR0:
        case SVM_EXIT_WRITE_CR3:
        case SVM_EXIT_WRITE_CR4:
        case SVM_EXIT_WRITE_CR8:    /* CR8 writes would go to the V_TPR rather than here, since we run with V_INTR_MASKING. */
        {
            uint8_t const uCr = uExitCode - SVM_EXIT_WRITE_CR0;
            Log4Func(("Write CR%u: uExitInfo1=%#RX64 uExitInfo2=%#RX64\n", uCr, uExitInfo1, uExitInfo2));

            if (CPUMIsGuestSvmWriteCRxInterceptSet(pVCpu, pCtx, uCr))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitWriteCRx(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_PAUSE:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_PAUSE))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitPause(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_VINTR:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_VINTR))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitUnexpected(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_INTR:
        case SVM_EXIT_NMI:
        case SVM_EXIT_SMI:
        case SVM_EXIT_XCPT_NMI:     /* Should not occur, SVM_EXIT_NMI is used instead. */
        {
            /*
             * We shouldn't direct physical interrupts, NMIs, SMIs to the nested-guest.
             *
             * Although we don't intercept SMIs, the nested-guest might. Therefore, we might
             * get an SMI #VMEXIT here so simply ignore rather than causing a corresponding
             * nested-guest #VMEXIT.
             *
             * We shall import the complete state here as we may cause #VMEXITs from ring-3
             * while trying to inject interrupts, see comment at the top of this function.
             */
            HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_ALL);
            return hmR0SvmExitIntr(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_FERR_FREEZE:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_FERR_FREEZE))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitFerrFreeze(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_INVLPG:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_INVLPG))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitInvlpg(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_WBINVD:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_WBINVD))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitWbinvd(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_INVD:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_INVD))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitInvd(pVCpu, pSvmTransient);
        }

        case SVM_EXIT_RDPMC:
        {
            if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_RDPMC))
                NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
            return hmR0SvmExitRdpmc(pVCpu, pSvmTransient);
        }

        default:
        {
            switch (uExitCode)
            {
                case SVM_EXIT_READ_DR0:     case SVM_EXIT_READ_DR1:     case SVM_EXIT_READ_DR2:     case SVM_EXIT_READ_DR3:
                case SVM_EXIT_READ_DR6:     case SVM_EXIT_READ_DR7:     case SVM_EXIT_READ_DR8:     case SVM_EXIT_READ_DR9:
                case SVM_EXIT_READ_DR10:    case SVM_EXIT_READ_DR11:    case SVM_EXIT_READ_DR12:    case SVM_EXIT_READ_DR13:
                case SVM_EXIT_READ_DR14:    case SVM_EXIT_READ_DR15:
                {
                    uint8_t const uDr = uExitCode - SVM_EXIT_READ_DR0;
                    if (CPUMIsGuestSvmReadDRxInterceptSet(pVCpu, pCtx, uDr))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitReadDRx(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_WRITE_DR0:    case SVM_EXIT_WRITE_DR1:    case SVM_EXIT_WRITE_DR2:    case SVM_EXIT_WRITE_DR3:
                case SVM_EXIT_WRITE_DR6:    case SVM_EXIT_WRITE_DR7:    case SVM_EXIT_WRITE_DR8:    case SVM_EXIT_WRITE_DR9:
                case SVM_EXIT_WRITE_DR10:   case SVM_EXIT_WRITE_DR11:   case SVM_EXIT_WRITE_DR12:   case SVM_EXIT_WRITE_DR13:
                case SVM_EXIT_WRITE_DR14:   case SVM_EXIT_WRITE_DR15:
                {
                    uint8_t const uDr = uExitCode - SVM_EXIT_WRITE_DR0;
                    if (CPUMIsGuestSvmWriteDRxInterceptSet(pVCpu, pCtx, uDr))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitWriteDRx(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_XCPT_DE:
                /*   SVM_EXIT_XCPT_DB: */       /* Handled above. */
                /*   SVM_EXIT_XCPT_NMI: */      /* Handled above. */
                /*   SVM_EXIT_XCPT_BP: */       /* Handled above. */
                case SVM_EXIT_XCPT_OF:
                case SVM_EXIT_XCPT_BR:
                /*   SVM_EXIT_XCPT_UD: */       /* Handled above. */
                case SVM_EXIT_XCPT_NM:
                case SVM_EXIT_XCPT_DF:
                case SVM_EXIT_XCPT_CO_SEG_OVERRUN:
                case SVM_EXIT_XCPT_TS:
                case SVM_EXIT_XCPT_NP:
                case SVM_EXIT_XCPT_SS:
                case SVM_EXIT_XCPT_GP:
                /*   SVM_EXIT_XCPT_PF: */       /* Handled above. */
                case SVM_EXIT_XCPT_15:          /* Reserved.      */
                /*   SVM_EXIT_XCPT_MF: */       /* Handled above. */
                /*   SVM_EXIT_XCPT_AC: */       /* Handled above. */
                case SVM_EXIT_XCPT_MC:
                case SVM_EXIT_XCPT_XF:
                case SVM_EXIT_XCPT_20: case SVM_EXIT_XCPT_21: case SVM_EXIT_XCPT_22: case SVM_EXIT_XCPT_23:
                case SVM_EXIT_XCPT_24: case SVM_EXIT_XCPT_25: case SVM_EXIT_XCPT_26: case SVM_EXIT_XCPT_27:
                case SVM_EXIT_XCPT_28: case SVM_EXIT_XCPT_29: case SVM_EXIT_XCPT_30: case SVM_EXIT_XCPT_31:
                {
                    uint8_t const uVector = uExitCode - SVM_EXIT_XCPT_0;
                    if (CPUMIsGuestSvmXcptInterceptSet(pVCpu, pCtx, uVector))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitXcptGeneric(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_XSETBV:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_XSETBV))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitXsetbv(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_TASK_SWITCH:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_TASK_SWITCH))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitTaskSwitch(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_IRET:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_IRET))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitIret(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_SHUTDOWN:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_SHUTDOWN))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitShutdown(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_VMMCALL:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_VMMCALL))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitVmmCall(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_CLGI:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_CLGI))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                     return hmR0SvmExitClgi(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_STGI:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_STGI))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                     return hmR0SvmExitStgi(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_VMLOAD:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_VMLOAD))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitVmload(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_VMSAVE:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_VMSAVE))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitVmsave(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_INVLPGA:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_INVLPGA))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitInvlpga(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_VMRUN:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_VMRUN))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    return hmR0SvmExitVmrun(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_RSM:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_RSM))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    hmR0SvmSetPendingXcptUD(pVCpu);
                    return VINF_SUCCESS;
                }

                case SVM_EXIT_SKINIT:
                {
                    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, pCtx, SVM_CTRL_INTERCEPT_SKINIT))
                        NST_GST_VMEXIT_CALL_RET(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
                    hmR0SvmSetPendingXcptUD(pVCpu);
                    return VINF_SUCCESS;
                }

                case SVM_EXIT_NPF:
                {
                    Assert(pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);
                    return hmR0SvmExitNestedPF(pVCpu, pSvmTransient);
                }

                case SVM_EXIT_INIT:  /* We shouldn't get INIT signals while executing a nested-guest. */
                    return hmR0SvmExitUnexpected(pVCpu, pSvmTransient);

                default:
                {
                    AssertMsgFailed(("hmR0SvmHandleExitNested: Unknown exit code %#x\n", pSvmTransient->u64ExitCode));
                    pVCpu->hm.s.u32HMError = pSvmTransient->u64ExitCode;
                    return VERR_SVM_UNKNOWN_EXIT;
                }
            }
        }
    }
    /* not reached */

# undef NST_GST_VMEXIT_CALL_RET
}

#endif /* VBOX_WITH_NESTED_HWVIRT_SVM */

/** @def VMEXIT_CALL_RET
 * Used by hmR0SvmHandleExit and hmR0SvmDebugHandleExit
 */
#ifdef DEBUG_ramshankar
# define VMEXIT_CALL_RET(a_fDbg, a_CallExpr) \
        do { \
            if ((a_fDbg) == 1) \
                HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL); \
            int rc = a_CallExpr; \
            if ((a_fDbg) == 1) \
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_ALL_GUEST); \
            return rc; \
        } while (0)
#else
# define VMEXIT_CALL_RET(a_fDbg, a_CallExpr) return a_CallExpr
#endif

/**
 * Handles a guest \#VMEXIT (for all EXITCODE values except SVM_EXIT_INVALID).
 *
 * @returns Strict VBox status code (informational status codes included).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM transient structure.
 */
static VBOXSTRICTRC hmR0SvmHandleExit(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    Assert(pSvmTransient->u64ExitCode != SVM_EXIT_INVALID);
    Assert(pSvmTransient->u64ExitCode <= SVM_EXIT_MAX);

    /*
     * The ordering of the case labels is based on most-frequently-occurring #VMEXITs
     * for most guests under normal workloads (for some definition of "normal").
     */
    uint64_t const uExitCode = pSvmTransient->u64ExitCode;
    switch (uExitCode)
    {
        case SVM_EXIT_NPF:          VMEXIT_CALL_RET(0, hmR0SvmExitNestedPF(pVCpu, pSvmTransient));
        case SVM_EXIT_IOIO:         VMEXIT_CALL_RET(0, hmR0SvmExitIOInstr(pVCpu, pSvmTransient));
        case SVM_EXIT_RDTSC:        VMEXIT_CALL_RET(0, hmR0SvmExitRdtsc(pVCpu, pSvmTransient));
        case SVM_EXIT_RDTSCP:       VMEXIT_CALL_RET(0, hmR0SvmExitRdtscp(pVCpu, pSvmTransient));
        case SVM_EXIT_CPUID:        VMEXIT_CALL_RET(0, hmR0SvmExitCpuid(pVCpu, pSvmTransient));
        case SVM_EXIT_XCPT_PF:      VMEXIT_CALL_RET(0, hmR0SvmExitXcptPF(pVCpu, pSvmTransient));
        case SVM_EXIT_MSR:          VMEXIT_CALL_RET(0, hmR0SvmExitMsr(pVCpu, pSvmTransient));
        case SVM_EXIT_MONITOR:      VMEXIT_CALL_RET(0, hmR0SvmExitMonitor(pVCpu, pSvmTransient));
        case SVM_EXIT_MWAIT:        VMEXIT_CALL_RET(0, hmR0SvmExitMwait(pVCpu, pSvmTransient));
        case SVM_EXIT_HLT:          VMEXIT_CALL_RET(0, hmR0SvmExitHlt(pVCpu, pSvmTransient));

        case SVM_EXIT_XCPT_NMI:     /* Should not occur, SVM_EXIT_NMI is used instead. */
        case SVM_EXIT_INTR:
        case SVM_EXIT_NMI:          VMEXIT_CALL_RET(0, hmR0SvmExitIntr(pVCpu, pSvmTransient));

        case SVM_EXIT_READ_CR0:
        case SVM_EXIT_READ_CR3:
        case SVM_EXIT_READ_CR4:     VMEXIT_CALL_RET(0, hmR0SvmExitReadCRx(pVCpu, pSvmTransient));

        case SVM_EXIT_CR0_SEL_WRITE:
        case SVM_EXIT_WRITE_CR0:
        case SVM_EXIT_WRITE_CR3:
        case SVM_EXIT_WRITE_CR4:
        case SVM_EXIT_WRITE_CR8:    VMEXIT_CALL_RET(0, hmR0SvmExitWriteCRx(pVCpu, pSvmTransient));

        case SVM_EXIT_VINTR:        VMEXIT_CALL_RET(0, hmR0SvmExitVIntr(pVCpu, pSvmTransient));
        case SVM_EXIT_PAUSE:        VMEXIT_CALL_RET(0, hmR0SvmExitPause(pVCpu, pSvmTransient));
        case SVM_EXIT_VMMCALL:      VMEXIT_CALL_RET(0, hmR0SvmExitVmmCall(pVCpu, pSvmTransient));
        case SVM_EXIT_INVLPG:       VMEXIT_CALL_RET(0, hmR0SvmExitInvlpg(pVCpu, pSvmTransient));
        case SVM_EXIT_WBINVD:       VMEXIT_CALL_RET(0, hmR0SvmExitWbinvd(pVCpu, pSvmTransient));
        case SVM_EXIT_INVD:         VMEXIT_CALL_RET(0, hmR0SvmExitInvd(pVCpu, pSvmTransient));
        case SVM_EXIT_RDPMC:        VMEXIT_CALL_RET(0, hmR0SvmExitRdpmc(pVCpu, pSvmTransient));
        case SVM_EXIT_IRET:         VMEXIT_CALL_RET(0, hmR0SvmExitIret(pVCpu, pSvmTransient));
        case SVM_EXIT_XCPT_DE:      VMEXIT_CALL_RET(0, hmR0SvmExitXcptDE(pVCpu, pSvmTransient));
        case SVM_EXIT_XCPT_UD:      VMEXIT_CALL_RET(0, hmR0SvmExitXcptUD(pVCpu, pSvmTransient));
        case SVM_EXIT_XCPT_MF:      VMEXIT_CALL_RET(0, hmR0SvmExitXcptMF(pVCpu, pSvmTransient));
        case SVM_EXIT_XCPT_DB:      VMEXIT_CALL_RET(0, hmR0SvmExitXcptDB(pVCpu, pSvmTransient));
        case SVM_EXIT_XCPT_AC:      VMEXIT_CALL_RET(0, hmR0SvmExitXcptAC(pVCpu, pSvmTransient));
        case SVM_EXIT_XCPT_BP:      VMEXIT_CALL_RET(0, hmR0SvmExitXcptBP(pVCpu, pSvmTransient));
        case SVM_EXIT_XCPT_GP:      VMEXIT_CALL_RET(0, hmR0SvmExitXcptGP(pVCpu, pSvmTransient));
        case SVM_EXIT_XSETBV:       VMEXIT_CALL_RET(0, hmR0SvmExitXsetbv(pVCpu, pSvmTransient));
        case SVM_EXIT_FERR_FREEZE:  VMEXIT_CALL_RET(0, hmR0SvmExitFerrFreeze(pVCpu, pSvmTransient));

        default:
        {
            switch (pSvmTransient->u64ExitCode)
            {
                case SVM_EXIT_READ_DR0:     case SVM_EXIT_READ_DR1:     case SVM_EXIT_READ_DR2:     case SVM_EXIT_READ_DR3:
                case SVM_EXIT_READ_DR6:     case SVM_EXIT_READ_DR7:     case SVM_EXIT_READ_DR8:     case SVM_EXIT_READ_DR9:
                case SVM_EXIT_READ_DR10:    case SVM_EXIT_READ_DR11:    case SVM_EXIT_READ_DR12:    case SVM_EXIT_READ_DR13:
                case SVM_EXIT_READ_DR14:    case SVM_EXIT_READ_DR15:
                    VMEXIT_CALL_RET(0, hmR0SvmExitReadDRx(pVCpu, pSvmTransient));

                case SVM_EXIT_WRITE_DR0:    case SVM_EXIT_WRITE_DR1:    case SVM_EXIT_WRITE_DR2:    case SVM_EXIT_WRITE_DR3:
                case SVM_EXIT_WRITE_DR6:    case SVM_EXIT_WRITE_DR7:    case SVM_EXIT_WRITE_DR8:    case SVM_EXIT_WRITE_DR9:
                case SVM_EXIT_WRITE_DR10:   case SVM_EXIT_WRITE_DR11:   case SVM_EXIT_WRITE_DR12:   case SVM_EXIT_WRITE_DR13:
                case SVM_EXIT_WRITE_DR14:   case SVM_EXIT_WRITE_DR15:
                    VMEXIT_CALL_RET(0, hmR0SvmExitWriteDRx(pVCpu, pSvmTransient));

                case SVM_EXIT_TASK_SWITCH:  VMEXIT_CALL_RET(0, hmR0SvmExitTaskSwitch(pVCpu, pSvmTransient));
                case SVM_EXIT_SHUTDOWN:     VMEXIT_CALL_RET(0, hmR0SvmExitShutdown(pVCpu, pSvmTransient));

                case SVM_EXIT_SMI:
                case SVM_EXIT_INIT:
                {
                    /*
                     * We don't intercept SMIs. As for INIT signals, it really shouldn't ever happen here.
                     * If it ever does, we want to know about it so log the exit code and bail.
                     */
                    VMEXIT_CALL_RET(0, hmR0SvmExitUnexpected(pVCpu, pSvmTransient));
                }

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
                case SVM_EXIT_CLGI:     VMEXIT_CALL_RET(0, hmR0SvmExitClgi(pVCpu, pSvmTransient));
                case SVM_EXIT_STGI:     VMEXIT_CALL_RET(0, hmR0SvmExitStgi(pVCpu, pSvmTransient));
                case SVM_EXIT_VMLOAD:   VMEXIT_CALL_RET(0, hmR0SvmExitVmload(pVCpu, pSvmTransient));
                case SVM_EXIT_VMSAVE:   VMEXIT_CALL_RET(0, hmR0SvmExitVmsave(pVCpu, pSvmTransient));
                case SVM_EXIT_INVLPGA:  VMEXIT_CALL_RET(0, hmR0SvmExitInvlpga(pVCpu, pSvmTransient));
                case SVM_EXIT_VMRUN:    VMEXIT_CALL_RET(0, hmR0SvmExitVmrun(pVCpu, pSvmTransient));
#else
                case SVM_EXIT_CLGI:
                case SVM_EXIT_STGI:
                case SVM_EXIT_VMLOAD:
                case SVM_EXIT_VMSAVE:
                case SVM_EXIT_INVLPGA:
                case SVM_EXIT_VMRUN:
#endif
                case SVM_EXIT_RSM:
                case SVM_EXIT_SKINIT:
                {
                    hmR0SvmSetPendingXcptUD(pVCpu);
                    return VINF_SUCCESS;
                }

                /*
                 * The remaining should only be possible when debugging or dtracing.
                 */
                case SVM_EXIT_XCPT_DE:
                /*   SVM_EXIT_XCPT_DB: */       /* Handled above. */
                /*   SVM_EXIT_XCPT_NMI: */      /* Handled above. */
                /*   SVM_EXIT_XCPT_BP: */       /* Handled above. */
                case SVM_EXIT_XCPT_OF:
                case SVM_EXIT_XCPT_BR:
                /*   SVM_EXIT_XCPT_UD: */       /* Handled above. */
                case SVM_EXIT_XCPT_NM:
                case SVM_EXIT_XCPT_DF:
                case SVM_EXIT_XCPT_CO_SEG_OVERRUN:
                case SVM_EXIT_XCPT_TS:
                case SVM_EXIT_XCPT_NP:
                case SVM_EXIT_XCPT_SS:
                /*   SVM_EXIT_XCPT_GP: */       /* Handled above. */
                /*   SVM_EXIT_XCPT_PF: */
                case SVM_EXIT_XCPT_15:          /* Reserved. */
                /*   SVM_EXIT_XCPT_MF: */       /* Handled above. */
                /*   SVM_EXIT_XCPT_AC: */       /* Handled above. */
                case SVM_EXIT_XCPT_MC:
                case SVM_EXIT_XCPT_XF:
                case SVM_EXIT_XCPT_20: case SVM_EXIT_XCPT_21: case SVM_EXIT_XCPT_22: case SVM_EXIT_XCPT_23:
                case SVM_EXIT_XCPT_24: case SVM_EXIT_XCPT_25: case SVM_EXIT_XCPT_26: case SVM_EXIT_XCPT_27:
                case SVM_EXIT_XCPT_28: case SVM_EXIT_XCPT_29: case SVM_EXIT_XCPT_30: case SVM_EXIT_XCPT_31:
                    VMEXIT_CALL_RET(0, hmR0SvmExitXcptGeneric(pVCpu, pSvmTransient));

                case SVM_EXIT_SWINT:    VMEXIT_CALL_RET(0, hmR0SvmExitSwInt(pVCpu, pSvmTransient));
                case SVM_EXIT_TR_READ:  VMEXIT_CALL_RET(0, hmR0SvmExitTrRead(pVCpu, pSvmTransient));
                case SVM_EXIT_TR_WRITE: VMEXIT_CALL_RET(0, hmR0SvmExitTrWrite(pVCpu, pSvmTransient)); /* Also OS/2 TLB workaround. */

                default:
                {
                    AssertMsgFailed(("hmR0SvmHandleExit: Unknown exit code %#RX64\n", uExitCode));
                    pVCpu->hm.s.u32HMError = uExitCode;
                    return VERR_SVM_UNKNOWN_EXIT;
                }
            }
        }
    }
    /* not reached */
}


/** @name Execution loop for single stepping, DBGF events and expensive Dtrace probes.
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
typedef struct SVMRUNDBGSTATE
{
    /** The initial SVMVMCBCTRL::u64InterceptCtrl value (helps with restore). */
    uint64_t    bmInterceptInitial;
    /** The initial SVMVMCBCTRL::u32InterceptXcpt value (helps with restore). */
    uint32_t    bmXcptInitial;
    /** The initial SVMVMCBCTRL::u16InterceptRdCRx value (helps with restore). */
    uint16_t    bmInterceptRdCRxInitial;
    /** The initial SVMVMCBCTRL::u16InterceptWrCRx value (helps with restore). */
    uint16_t    bmInterceptWrCRxInitial;
    /** The initial SVMVMCBCTRL::u16InterceptRdDRx value (helps with restore). */
    uint16_t    bmInterceptRdDRxInitial;
    /** The initial SVMVMCBCTRL::u16InterceptWrDRx value (helps with restore). */
    uint16_t    bmInterceptWrDRxInitial;

    /** Whether we've actually modified the intercept control qword. */
    bool        fModifiedInterceptCtrl : 1;
    /** Whether we've actually modified the exception bitmap. */
    bool        fModifiedXcptBitmap : 1;
    /** Whether we've actually modified SVMVMCBCTRL::u16InterceptRdCRx. */
    bool        fModifiedInterceptRdCRx : 1;
    /** Whether we've actually modified SVMVMCBCTRL::u16InterceptWrCRx. */
    bool        fModifiedInterceptWrCRx : 1;
    /** Whether we've actually modified SVMVMCBCTRL::u16InterceptRdDRx. */
    bool        fModifiedInterceptRdDRx : 1;
    /** Whether we've actually modified SVMVMCBCTRL::u16InterceptWrDRx. */
    bool        fModifiedInterceptWrDRx : 1;

    /** The CS we started executing with.  */
    uint16_t    uCsStart;
    /** The RIP we started executing at.  This is for detecting that we stepped.  */
    uint64_t    uRipStart;

    /** The sequence number of the Dtrace provider settings the state was
     *  configured against. */
    uint32_t    uDtraceSettingsSeqNo;
    /** Extra stuff we need in SVMVMCBCTRL::u32InterceptXcpt. */
    uint32_t    bmXcptExtra;
    /** Extra stuff we need in SVMVMCBCTRL::u64InterceptCtrl. */
    uint64_t    bmInterceptExtra;
    /** Extra stuff we need in SVMVMCBCTRL::u16InterceptRdCRx. */
    uint16_t    bmInterceptRdCRxExtra;
    /** Extra stuff we need in SVMVMCBCTRL::u16InterceptWrCRx. */
    uint16_t    bmInterceptWrCRxExtra;
    /** Extra stuff we need in SVMVMCBCTRL::u16InterceptRdDRx. */
    uint16_t    bmInterceptRdDRxExtra;
    /** Extra stuff we need in SVMVMCBCTRL::u16InterceptWrDRx. */
    uint16_t    bmInterceptWrDRxExtra;
    /** VM-exits to check (one bit per VM-exit). */
    uint32_t    bmExitsToCheck[33];
} SVMRUNDBGSTATE;
AssertCompileMemberSize(SVMRUNDBGSTATE, bmExitsToCheck, (SVM_EXIT_MAX + 1 + 31) / 32 * 4);
typedef SVMRUNDBGSTATE *PSVMRUNDBGSTATE;


/**
 * Initializes the SVMRUNDBGSTATE structure.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   pSvmTransient   The SVM-transient structure.
 * @param   pDbgState       The debug state to initialize.
 */
static void hmR0SvmRunDebugStateInit(PVMCPUCC pVCpu, PCSVMTRANSIENT pSvmTransient, PSVMRUNDBGSTATE pDbgState)
{
    PSVMVMCB pVmcb = pSvmTransient->pVmcb;
    pDbgState->bmInterceptInitial       = pVmcb->ctrl.u64InterceptCtrl;
    pDbgState->bmXcptInitial            = pVmcb->ctrl.u32InterceptXcpt;
    pDbgState->bmInterceptRdCRxInitial  = pVmcb->ctrl.u16InterceptRdCRx;
    pDbgState->bmInterceptWrCRxInitial  = pVmcb->ctrl.u16InterceptWrCRx;
    pDbgState->bmInterceptRdDRxInitial  = pVmcb->ctrl.u16InterceptRdDRx;
    pDbgState->bmInterceptWrDRxInitial  = pVmcb->ctrl.u16InterceptWrDRx;

    pDbgState->fModifiedInterceptCtrl   = false;
    pDbgState->fModifiedXcptBitmap      = false;
    pDbgState->fModifiedInterceptRdCRx  = false;
    pDbgState->fModifiedInterceptWrCRx  = false;
    pDbgState->fModifiedInterceptRdDRx  = false;
    pDbgState->fModifiedInterceptWrDRx  = false;

    pDbgState->uCsStart                 = pVCpu->cpum.GstCtx.cs.Sel;
    pDbgState->uRipStart                = pVCpu->cpum.GstCtx.rip;

    /* We don't really need to zero these. */
    pDbgState->bmInterceptExtra         = 0;
    pDbgState->bmXcptExtra              = 0;
    pDbgState->bmInterceptRdCRxExtra    = 0;
    pDbgState->bmInterceptWrCRxExtra    = 0;
    pDbgState->bmInterceptRdDRxExtra    = 0;
    pDbgState->bmInterceptWrDRxExtra    = 0;
}


/**
 * Updates the VMCB fields with changes requested by @a pDbgState.
 *
 * This is performed after hmR0SvmPreRunGuestDebugStateUpdate as well
 * immediately before executing guest code, i.e. when interrupts are disabled.
 * We don't check status codes here as we cannot easily assert or return in the
 * latter case.
 *
 * @param   pSvmTransient   The SVM-transient structure.
 * @param   pDbgState       The debug state.
 */
static void hmR0SvmPreRunGuestDebugStateApply(PSVMTRANSIENT pSvmTransient, PSVMRUNDBGSTATE pDbgState)
{
    /*
     * Ensure desired flags in VMCS control fields are set.
     */
    PSVMVMCB const pVmcb = pSvmTransient->pVmcb;
#define ADD_EXTRA_INTERCEPTS(a_VmcbCtrlField, a_bmExtra, a_fModified) do { \
        if ((pVmcb->ctrl. a_VmcbCtrlField & (a_bmExtra)) != (a_bmExtra)) \
        { \
            pVmcb->ctrl. a_VmcbCtrlField |= (a_bmExtra); \
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS; \
            Log6Func((#a_VmcbCtrlField ": %#RX64\n", pVmcb->ctrl. a_VmcbCtrlField)); \
            (a_fModified) = true; \
        } \
    } while (0)
    ADD_EXTRA_INTERCEPTS(u64InterceptCtrl,  pDbgState->bmInterceptExtra,        pDbgState->fModifiedInterceptCtrl);
    ADD_EXTRA_INTERCEPTS(u32InterceptXcpt,  pDbgState->bmXcptExtra,             pDbgState->fModifiedXcptBitmap);
    ADD_EXTRA_INTERCEPTS(u16InterceptRdCRx, pDbgState->bmInterceptRdCRxExtra,   pDbgState->fModifiedInterceptRdCRx);
    ADD_EXTRA_INTERCEPTS(u16InterceptWrCRx, pDbgState->bmInterceptWrCRxExtra,   pDbgState->fModifiedInterceptWrCRx);
    ADD_EXTRA_INTERCEPTS(u16InterceptRdDRx, pDbgState->bmInterceptRdDRxExtra,   pDbgState->fModifiedInterceptRdDRx);
    ADD_EXTRA_INTERCEPTS(u16InterceptWrDRx, pDbgState->bmInterceptWrDRxExtra,   pDbgState->fModifiedInterceptWrDRx);
#undef ADD_EXTRA_INTERCEPTS
}


/**
 * Restores VMCB fields that were changed by hmR0SvmPreRunGuestDebugStateApply
 * for re-entry next time around.
 *
 * @param   pSvmTransient   The SVM-transient structure.
 * @param   pDbgState       The debug state.
 */
static void hmR0SvmRunDebugStateRevert(PSVMTRANSIENT pSvmTransient, PSVMRUNDBGSTATE pDbgState)
{
    /*
     * Restore VM-exit control settings as we may not reenter this function the
     * next time around.
     */
    PSVMVMCB const pVmcb = pSvmTransient->pVmcb;

#define RESTORE_INTERCEPTS(a_VmcbCtrlField, a_bmInitial, a_fModified) do { \
        if ((a_fModified)) \
        { \
            pVmcb->ctrl. a_VmcbCtrlField = (a_bmInitial); \
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS; \
        } \
    } while (0)
    RESTORE_INTERCEPTS(u64InterceptCtrl,  pDbgState->bmInterceptInitial,      pDbgState->fModifiedInterceptCtrl);
    RESTORE_INTERCEPTS(u32InterceptXcpt,  pDbgState->bmXcptInitial,           pDbgState->fModifiedXcptBitmap);
    RESTORE_INTERCEPTS(u16InterceptRdCRx, pDbgState->bmInterceptRdCRxInitial, pDbgState->fModifiedInterceptRdCRx);
    RESTORE_INTERCEPTS(u16InterceptWrCRx, pDbgState->bmInterceptWrCRxInitial, pDbgState->fModifiedInterceptWrCRx);
    RESTORE_INTERCEPTS(u16InterceptRdDRx, pDbgState->bmInterceptRdDRxInitial, pDbgState->fModifiedInterceptRdDRx);
    RESTORE_INTERCEPTS(u16InterceptWrDRx, pDbgState->bmInterceptWrDRxInitial, pDbgState->fModifiedInterceptWrDRx);
#undef RESTORE_INTERCEPTS
}


/**
 * Configures VM-exit controls for current DBGF and DTrace settings.
 *
 * This updates @a pDbgState and the VMCB execution control fields (in the debug
 * state) to reflect the necessary VM-exits demanded by DBGF and DTrace.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   The SVM-transient structure. May update
 *                          fUpdatedTscOffsettingAndPreemptTimer.
 * @param   pDbgState       The debug state.
 */
static void hmR0SvmPreRunGuestDebugStateUpdate(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient, PSVMRUNDBGSTATE pDbgState)
{
    /*
     * Take down the dtrace serial number so we can spot changes.
     */
    pDbgState->uDtraceSettingsSeqNo = VBOXVMM_GET_SETTINGS_SEQ_NO();
    ASMCompilerBarrier();

    /*
     * Clear data members that we'll be rebuilding here.
     */
    pDbgState->bmXcptExtra              = 0;
    pDbgState->bmInterceptExtra         = 0;
    pDbgState->bmInterceptRdCRxExtra    = 0;
    pDbgState->bmInterceptWrCRxExtra    = 0;
    pDbgState->bmInterceptRdDRxExtra    = 0;
    pDbgState->bmInterceptWrDRxExtra    = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(pDbgState->bmExitsToCheck); i++)
        pDbgState->bmExitsToCheck[i]    = 0;

    /*
     * Software interrupts (INT XXh)
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (   DBGF_IS_EVENT_ENABLED(pVM, DBGFEVENT_INTERRUPT_SOFTWARE)
        || VBOXVMM_INT_SOFTWARE_ENABLED())
    {
        pDbgState->bmInterceptExtra |= SVM_CTRL_INTERCEPT_INTN;
        ASMBitSet(pDbgState->bmExitsToCheck, SVM_EXIT_SWINT);
    }

    /*
     * INT3 breakpoints - triggered by #BP exceptions.
     */
    if (pVM->dbgf.ro.cEnabledInt3Breakpoints > 0)
        pDbgState->bmXcptExtra |= RT_BIT_32(X86_XCPT_BP);

    /*
     * Exception bitmap and XCPT events+probes.
     */
#define SET_XCPT(a_iXcpt) do { \
        pDbgState->bmXcptExtra |= RT_BIT_32(a_iXcpt); \
        ASMBitSet(pDbgState->bmExitsToCheck, SVM_EXIT_XCPT_0 + (a_iXcpt)); \
    } while (0)

    for (int iXcpt = 0; iXcpt < (DBGFEVENT_XCPT_LAST - DBGFEVENT_XCPT_FIRST + 1); iXcpt++)
        if (DBGF_IS_EVENT_ENABLED(pVM, (DBGFEVENTTYPE)(DBGFEVENT_XCPT_FIRST + iXcpt)))
            SET_XCPT(iXcpt);

    if (VBOXVMM_XCPT_DE_ENABLED())  SET_XCPT(X86_XCPT_DE);
    if (VBOXVMM_XCPT_DB_ENABLED())  SET_XCPT(X86_XCPT_DB);
    if (VBOXVMM_XCPT_BP_ENABLED())  SET_XCPT(X86_XCPT_BP);
    if (VBOXVMM_XCPT_OF_ENABLED())  SET_XCPT(X86_XCPT_OF);
    if (VBOXVMM_XCPT_BR_ENABLED())  SET_XCPT(X86_XCPT_BR);
    if (VBOXVMM_XCPT_UD_ENABLED())  SET_XCPT(X86_XCPT_UD);
    if (VBOXVMM_XCPT_NM_ENABLED())  SET_XCPT(X86_XCPT_NM);
    if (VBOXVMM_XCPT_DF_ENABLED())  SET_XCPT(X86_XCPT_DF);
    if (VBOXVMM_XCPT_TS_ENABLED())  SET_XCPT(X86_XCPT_TS);
    if (VBOXVMM_XCPT_NP_ENABLED())  SET_XCPT(X86_XCPT_NP);
    if (VBOXVMM_XCPT_SS_ENABLED())  SET_XCPT(X86_XCPT_SS);
    if (VBOXVMM_XCPT_GP_ENABLED())  SET_XCPT(X86_XCPT_GP);
    if (VBOXVMM_XCPT_PF_ENABLED())  SET_XCPT(X86_XCPT_PF);
    if (VBOXVMM_XCPT_MF_ENABLED())  SET_XCPT(X86_XCPT_MF);
    if (VBOXVMM_XCPT_AC_ENABLED())  SET_XCPT(X86_XCPT_AC);
    if (VBOXVMM_XCPT_XF_ENABLED())  SET_XCPT(X86_XCPT_XF);
    if (VBOXVMM_XCPT_VE_ENABLED())  SET_XCPT(X86_XCPT_VE);
    if (VBOXVMM_XCPT_SX_ENABLED())  SET_XCPT(X86_XCPT_SX);

#undef SET_XCPT

    /*
     * Process events and probes for VM-exits, making sure we get the wanted VM-exits.
     *
     * Note! This is the reverse of what hmR0SvmHandleExitDtraceEvents does.
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
#define SET_INCP_XBM_IF_EITHER_EN(a_EventSubName, a_uExit, a_fInterceptCtrl) \
        if (IS_EITHER_ENABLED(pVM, a_EventSubName)) \
        { \
            (pDbgState)->bmInterceptExtra |= (a_fInterceptCtrl); \
            AssertCompile((unsigned)(a_uExit) < sizeof(pDbgState->bmExitsToCheck) * 8); \
            ASMBitSet((pDbgState)->bmExitsToCheck, a_uExit); \
        } else do { } while (0)

    /** @todo double check these */
    /** @todo Check what more AMD-V specific we can intercept.   */
    //SET_INCP_XBM_IF_EITHER_EN(EXIT_TASK_SWITCH,         SVM_EXIT_TASK_SWITCH,       SVM_CTRL_INTERCEPT_TASK_SWITCH);
    SET_ONLY_XBM_IF_EITHER_EN(EXIT_TASK_SWITCH,         SVM_EXIT_TASK_SWITCH);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_VMM_CALL,           SVM_EXIT_VMMCALL,           SVM_CTRL_INTERCEPT_VMMCALL);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_VMM_CALL,           SVM_EXIT_VMMCALL);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_SVM_VMRUN,          SVM_EXIT_VMRUN,             SVM_CTRL_INTERCEPT_VMRUN);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SVM_VMRUN,          SVM_EXIT_VMRUN);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_SVM_VMLOAD,         SVM_EXIT_VMLOAD,            SVM_CTRL_INTERCEPT_VMLOAD);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SVM_VMLOAD,         SVM_EXIT_VMLOAD);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_SVM_VMSAVE,         SVM_EXIT_VMSAVE,            SVM_CTRL_INTERCEPT_VMSAVE);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SVM_VMSAVE,         SVM_EXIT_VMSAVE);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_SVM_STGI,           SVM_EXIT_STGI,              SVM_CTRL_INTERCEPT_STGI);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SVM_STGI,           SVM_EXIT_STGI);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_SVM_CLGI,           SVM_EXIT_CLGI,              SVM_CTRL_INTERCEPT_CLGI);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SVM_CLGI,           SVM_EXIT_CLGI);

    SET_INCP_XBM_IF_EITHER_EN(INSTR_CPUID,              SVM_EXIT_CPUID,             SVM_CTRL_INTERCEPT_CPUID);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_CPUID,              SVM_EXIT_CPUID);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_HALT,               SVM_EXIT_HLT,               SVM_CTRL_INTERCEPT_HLT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_HALT,               SVM_EXIT_HLT);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_INVD,               SVM_EXIT_INVD,              SVM_CTRL_INTERCEPT_INVD);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_INVD,               SVM_EXIT_INVD);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_INVLPG,             SVM_EXIT_INVLPG,            SVM_CTRL_INTERCEPT_INVLPG);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_INVLPG,             SVM_EXIT_INVLPG);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_RDPMC,              SVM_EXIT_RDPMC,             SVM_CTRL_INTERCEPT_RDPMC);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDPMC,              SVM_EXIT_RDPMC);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_RDTSC,              SVM_EXIT_RDTSC,             SVM_CTRL_INTERCEPT_RDTSC);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDTSC,              SVM_EXIT_RDTSC);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_RDTSCP,             SVM_EXIT_RDTSCP,            SVM_CTRL_INTERCEPT_RDTSCP);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDTSCP,             SVM_EXIT_RDTSCP);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_RSM,                SVM_EXIT_RSM,               SVM_CTRL_INTERCEPT_RSM);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RSM,                SVM_EXIT_RSM);

    if (IS_EITHER_ENABLED(pVM, INSTR_CRX_READ))
        pDbgState->bmInterceptRdCRxExtra = 0xffff;
    if (IS_EITHER_ENABLED(pVM, INSTR_CRX_READ) || IS_EITHER_ENABLED(pVM, EXIT_CRX_READ))
        ASMBitSetRange(pDbgState->bmExitsToCheck, SVM_EXIT_READ_CR0, SVM_EXIT_READ_CR15 + 1);

    if (IS_EITHER_ENABLED(pVM, INSTR_CRX_WRITE))
        pDbgState->bmInterceptWrCRxExtra = 0xffff;
    if (IS_EITHER_ENABLED(pVM, INSTR_CRX_WRITE) || IS_EITHER_ENABLED(pVM, EXIT_CRX_WRITE))
    {
        ASMBitSetRange(pDbgState->bmExitsToCheck, SVM_EXIT_WRITE_CR0, SVM_EXIT_WRITE_CR15 + 1);
        ASMBitSet(pDbgState->bmExitsToCheck, SVM_EXIT_CR0_SEL_WRITE);
    }

    if (IS_EITHER_ENABLED(pVM, INSTR_DRX_READ))
        pDbgState->bmInterceptRdDRxExtra = 0xffff;
    if (IS_EITHER_ENABLED(pVM, INSTR_DRX_READ) || IS_EITHER_ENABLED(pVM, EXIT_DRX_READ))
        ASMBitSetRange(pDbgState->bmExitsToCheck, SVM_EXIT_READ_DR0, SVM_EXIT_READ_DR15 + 1);

    if (IS_EITHER_ENABLED(pVM, INSTR_DRX_WRITE))
        pDbgState->bmInterceptWrDRxExtra = 0xffff;
    if (IS_EITHER_ENABLED(pVM, INSTR_DRX_WRITE) || IS_EITHER_ENABLED(pVM, EXIT_DRX_WRITE))
        ASMBitSetRange(pDbgState->bmExitsToCheck, SVM_EXIT_WRITE_DR0, SVM_EXIT_WRITE_DR15 + 1);

    SET_ONLY_XBM_IF_EITHER_EN(INSTR_RDMSR,              SVM_EXIT_MSR); /** @todo modify bitmap to intercept almost everything? (Clearing MSR_PROT just means no intercepts.) */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_RDMSR,              SVM_EXIT_MSR);
    SET_ONLY_XBM_IF_EITHER_EN(INSTR_WRMSR,              SVM_EXIT_MSR); /** @todo ditto */
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_WRMSR,              SVM_EXIT_MSR);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_MWAIT,              SVM_EXIT_MWAIT,         SVM_CTRL_INTERCEPT_MWAIT);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_MWAIT,              SVM_EXIT_MWAIT);
    if (ASMBitTest(pDbgState->bmExitsToCheck, SVM_EXIT_MWAIT))
        ASMBitSet(pDbgState->bmExitsToCheck, SVM_EXIT_MWAIT_ARMED);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_MONITOR,            SVM_EXIT_MONITOR,       SVM_CTRL_INTERCEPT_MONITOR);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_MONITOR,            SVM_EXIT_MONITOR);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_PAUSE,              SVM_EXIT_PAUSE,         SVM_CTRL_INTERCEPT_PAUSE);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_PAUSE,              SVM_EXIT_PAUSE);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_SIDT,               SVM_EXIT_IDTR_READ,     SVM_CTRL_INTERCEPT_IDTR_READS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SIDT,               SVM_EXIT_IDTR_READ);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_LIDT,               SVM_EXIT_IDTR_WRITE,    SVM_CTRL_INTERCEPT_IDTR_WRITES);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_LIDT,               SVM_EXIT_IDTR_WRITE);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_SGDT,               SVM_EXIT_GDTR_READ,     SVM_CTRL_INTERCEPT_GDTR_READS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SGDT,               SVM_EXIT_GDTR_READ);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_LGDT,               SVM_EXIT_GDTR_WRITE,    SVM_CTRL_INTERCEPT_GDTR_WRITES);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_LGDT,               SVM_EXIT_GDTR_WRITE);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_SLDT,               SVM_EXIT_LDTR_READ,     SVM_CTRL_INTERCEPT_LDTR_READS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_SLDT,               SVM_EXIT_LDTR_READ);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_LLDT,               SVM_EXIT_LDTR_WRITE,    SVM_CTRL_INTERCEPT_LDTR_WRITES);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_LLDT,               SVM_EXIT_LDTR_WRITE);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_STR,                SVM_EXIT_TR_READ,       SVM_CTRL_INTERCEPT_TR_READS);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_STR,                SVM_EXIT_TR_READ);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_LTR,                SVM_EXIT_TR_WRITE,      SVM_CTRL_INTERCEPT_TR_WRITES);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_LTR,                SVM_EXIT_TR_WRITE);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_WBINVD,             SVM_EXIT_WBINVD,        SVM_CTRL_INTERCEPT_WBINVD);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_WBINVD,             SVM_EXIT_WBINVD);
    SET_INCP_XBM_IF_EITHER_EN(INSTR_XSETBV,             SVM_EXIT_XSETBV,        SVM_CTRL_INTERCEPT_XSETBV);
    SET_ONLY_XBM_IF_EITHER_EN( EXIT_XSETBV,             SVM_EXIT_XSETBV);

    if (DBGF_IS_EVENT_ENABLED(pVM, DBGFEVENT_TRIPLE_FAULT))
        ASMBitSet(pDbgState->bmExitsToCheck, SVM_EXIT_SHUTDOWN);

#undef IS_EITHER_ENABLED
#undef SET_ONLY_XBM_IF_EITHER_EN
#undef SET_INCP_XBM_IF_EITHER_EN

    /*
     * Sanitize the control stuff.
     */
    /** @todo filter out unsupported stuff? */
    if (   pVCpu->hmr0.s.fDebugWantRdTscExit
        != RT_BOOL(pDbgState->bmInterceptExtra & (SVM_CTRL_INTERCEPT_RDTSC | SVM_CTRL_INTERCEPT_RDTSCP)))
    {
        pVCpu->hmr0.s.fDebugWantRdTscExit ^= true;
        /// @todo pVmxTransient->fUpdatedTscOffsettingAndPreemptTimer = false;
        RT_NOREF(pSvmTransient);
    }

    Log6(("HM: debug state: bmInterceptExtra=%#RX64 bmXcptExtra=%#RX32%s%s%s%s bmExitsToCheck=%08RX32'%08RX32'%08RX32'%08RX32'%08RX32\n",
          pDbgState->bmInterceptExtra, pDbgState->bmXcptExtra,
          pDbgState->bmInterceptRdCRxExtra ? " rd-cr" : "",
          pDbgState->bmInterceptWrCRxExtra ? " wr-cr" : "",
          pDbgState->bmInterceptRdDRxExtra ? " rd-dr" : "",
          pDbgState->bmInterceptWrDRxExtra ? " wr-dr" : "",
          pDbgState->bmExitsToCheck[0],
          pDbgState->bmExitsToCheck[1],
          pDbgState->bmExitsToCheck[2],
          pDbgState->bmExitsToCheck[3],
          pDbgState->bmExitsToCheck[4]));
}


/**
 * Fires off DBGF events and dtrace probes for a VM-exit, when it's
 * appropriate.
 *
 * The caller has checked the VM-exit against the SVMRUNDBGSTATE::bmExitsToCheck
 * bitmap.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   The SVM-transient structure.
 * @param   uExitCode       The VM-exit code.
 *
 * @remarks The name of this function is displayed by dtrace, so keep it short
 *          and to the point. No longer than 33 chars long, please.
 */
static VBOXSTRICTRC hmR0SvmHandleExitDtraceEvents(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient, uint64_t uExitCode)
{
    /*
     * Translate the event into a DBGF event (enmEvent + uEventArg) and at the
     * same time check whether any corresponding Dtrace event is enabled (fDtrace).
     *
     * Note! This is the reverse operation of what hmR0SvmPreRunGuestDebugStateUpdate
     *       does.  Must add/change/remove both places.  Same ordering, please.
     *
     *       Added/removed events must also be reflected in the next section
     *       where we dispatch dtrace events.
     */
    bool            fDtrace1   = false;
    bool            fDtrace2   = false;
    DBGFEVENTTYPE   enmEvent1  = DBGFEVENT_END;
    DBGFEVENTTYPE   enmEvent2  = DBGFEVENT_END;
    uint64_t        uEventArg  = 0;
#define SET_XCPT(a_XcptName) \
        do { \
            enmEvent2 = RT_CONCAT(DBGFEVENT_XCPT_, a_XcptName); \
            fDtrace2  = RT_CONCAT3(VBOXVMM_XCPT_, a_XcptName, _ENABLED)(); \
        } while (0)
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
    switch (uExitCode)
    {
        case SVM_EXIT_SWINT:
            enmEvent2 = DBGFEVENT_INTERRUPT_SOFTWARE;
            fDtrace2  = VBOXVMM_INT_SOFTWARE_ENABLED();
            uEventArg = pSvmTransient->pVmcb->ctrl.u64ExitInfo1;
            break;

        case SVM_EXIT_XCPT_DE:      SET_XCPT(DE); break;
        case SVM_EXIT_XCPT_DB:      SET_XCPT(DB); break;
        case SVM_EXIT_XCPT_BP:      SET_XCPT(BP); break;
        case SVM_EXIT_XCPT_OF:      SET_XCPT(OF); break;
        case SVM_EXIT_XCPT_BR:      SET_XCPT(BR); break;
        case SVM_EXIT_XCPT_UD:      SET_XCPT(UD); break;
        case SVM_EXIT_XCPT_NM:      SET_XCPT(NM); break;
        case SVM_EXIT_XCPT_DF:      SET_XCPT(DF); break;
        case SVM_EXIT_XCPT_TS:      SET_XCPT(TS); uEventArg = pSvmTransient->pVmcb->ctrl.u64ExitInfo1; break;
        case SVM_EXIT_XCPT_NP:      SET_XCPT(NP); uEventArg = pSvmTransient->pVmcb->ctrl.u64ExitInfo1; break;
        case SVM_EXIT_XCPT_SS:      SET_XCPT(SS); uEventArg = pSvmTransient->pVmcb->ctrl.u64ExitInfo1; break;
        case SVM_EXIT_XCPT_GP:      SET_XCPT(GP); uEventArg = pSvmTransient->pVmcb->ctrl.u64ExitInfo1; break;
        case SVM_EXIT_XCPT_PF:      SET_XCPT(PF); uEventArg = pSvmTransient->pVmcb->ctrl.u64ExitInfo1; break;
        case SVM_EXIT_XCPT_MF:      SET_XCPT(MF); break;
        case SVM_EXIT_XCPT_AC:      SET_XCPT(AC); break;
        case SVM_EXIT_XCPT_XF:      SET_XCPT(XF); break;
        case SVM_EXIT_XCPT_VE:      SET_XCPT(VE); break;
        case SVM_EXIT_XCPT_SX:      SET_XCPT(SX); uEventArg = pSvmTransient->pVmcb->ctrl.u64ExitInfo1; break;

        case SVM_EXIT_XCPT_2:       enmEvent2 = DBGFEVENT_XCPT_02; break;
        case SVM_EXIT_XCPT_9:       enmEvent2 = DBGFEVENT_XCPT_09; break;
        case SVM_EXIT_XCPT_15:      enmEvent2 = DBGFEVENT_XCPT_0f; break;
        case SVM_EXIT_XCPT_18:      enmEvent2 = DBGFEVENT_XCPT_MC; break;
        case SVM_EXIT_XCPT_21:      enmEvent2 = DBGFEVENT_XCPT_15; break;
        case SVM_EXIT_XCPT_22:      enmEvent2 = DBGFEVENT_XCPT_16; break;
        case SVM_EXIT_XCPT_23:      enmEvent2 = DBGFEVENT_XCPT_17; break;
        case SVM_EXIT_XCPT_24:      enmEvent2 = DBGFEVENT_XCPT_18; break;
        case SVM_EXIT_XCPT_25:      enmEvent2 = DBGFEVENT_XCPT_19; break;
        case SVM_EXIT_XCPT_26:      enmEvent2 = DBGFEVENT_XCPT_1a; break;
        case SVM_EXIT_XCPT_27:      enmEvent2 = DBGFEVENT_XCPT_1b; break;
        case SVM_EXIT_XCPT_28:      enmEvent2 = DBGFEVENT_XCPT_1c; break;
        case SVM_EXIT_XCPT_29:      enmEvent2 = DBGFEVENT_XCPT_1d; break;
        case SVM_EXIT_XCPT_31:      enmEvent2 = DBGFEVENT_XCPT_1f; break;

        case SVM_EXIT_TASK_SWITCH:  SET_EXIT(TASK_SWITCH); break;
        case SVM_EXIT_VMMCALL:      SET_BOTH(VMM_CALL); break;
        case SVM_EXIT_VMRUN:        SET_BOTH(SVM_VMRUN); break;
        case SVM_EXIT_VMLOAD:       SET_BOTH(SVM_VMLOAD); break;
        case SVM_EXIT_VMSAVE:       SET_BOTH(SVM_VMSAVE); break;
        case SVM_EXIT_STGI:         SET_BOTH(SVM_STGI); break;
        case SVM_EXIT_CLGI:         SET_BOTH(SVM_CLGI); break;
        case SVM_EXIT_CPUID:        SET_BOTH(CPUID); break;
        case SVM_EXIT_HLT:          SET_BOTH(HALT); break;
        case SVM_EXIT_INVD:         SET_BOTH(INVD); break;
        case SVM_EXIT_INVLPG:       SET_BOTH(INVLPG); break;
        case SVM_EXIT_RDPMC:        SET_BOTH(RDPMC); break;
        case SVM_EXIT_RDTSC:        SET_BOTH(RDTSC); break;
        case SVM_EXIT_RDTSCP:       SET_BOTH(RDTSCP); break;
        case SVM_EXIT_RSM:          SET_BOTH(RSM); break;

        case SVM_EXIT_READ_CR0:   case SVM_EXIT_READ_CR1:   case SVM_EXIT_READ_CR2:   case SVM_EXIT_READ_CR3:
        case SVM_EXIT_READ_CR4:   case SVM_EXIT_READ_CR5:   case SVM_EXIT_READ_CR6:   case SVM_EXIT_READ_CR7:
        case SVM_EXIT_READ_CR8:   case SVM_EXIT_READ_CR9:   case SVM_EXIT_READ_CR10:  case SVM_EXIT_READ_CR11:
        case SVM_EXIT_READ_CR12:  case SVM_EXIT_READ_CR13:  case SVM_EXIT_READ_CR14:  case SVM_EXIT_READ_CR15:
            SET_BOTH(CRX_READ);
            uEventArg = uExitCode - SVM_EXIT_READ_CR0;
            break;
        case SVM_EXIT_WRITE_CR0:  case SVM_EXIT_WRITE_CR1:  case SVM_EXIT_WRITE_CR2:  case SVM_EXIT_WRITE_CR3:
        case SVM_EXIT_WRITE_CR4:  case SVM_EXIT_WRITE_CR5:  case SVM_EXIT_WRITE_CR6:  case SVM_EXIT_WRITE_CR7:
        case SVM_EXIT_WRITE_CR8:  case SVM_EXIT_WRITE_CR9:  case SVM_EXIT_WRITE_CR10: case SVM_EXIT_WRITE_CR11:
        case SVM_EXIT_WRITE_CR12: case SVM_EXIT_WRITE_CR13: case SVM_EXIT_WRITE_CR14: case SVM_EXIT_WRITE_CR15:
        case SVM_EXIT_CR0_SEL_WRITE:
            SET_BOTH(CRX_WRITE);
            uEventArg = uExitCode - SVM_EXIT_WRITE_CR0;
            break;
        case SVM_EXIT_READ_DR0:   case SVM_EXIT_READ_DR1:   case SVM_EXIT_READ_DR2:   case SVM_EXIT_READ_DR3:
        case SVM_EXIT_READ_DR4:   case SVM_EXIT_READ_DR5:   case SVM_EXIT_READ_DR6:   case SVM_EXIT_READ_DR7:
        case SVM_EXIT_READ_DR8:   case SVM_EXIT_READ_DR9:   case SVM_EXIT_READ_DR10:  case SVM_EXIT_READ_DR11:
        case SVM_EXIT_READ_DR12:  case SVM_EXIT_READ_DR13:  case SVM_EXIT_READ_DR14:  case SVM_EXIT_READ_DR15:
            SET_BOTH(DRX_READ);
            uEventArg = uExitCode - SVM_EXIT_READ_DR0;
            break;
        case SVM_EXIT_WRITE_DR0:  case SVM_EXIT_WRITE_DR1:  case SVM_EXIT_WRITE_DR2:  case SVM_EXIT_WRITE_DR3:
        case SVM_EXIT_WRITE_DR4:  case SVM_EXIT_WRITE_DR5:  case SVM_EXIT_WRITE_DR6:  case SVM_EXIT_WRITE_DR7:
        case SVM_EXIT_WRITE_DR8:  case SVM_EXIT_WRITE_DR9:  case SVM_EXIT_WRITE_DR10: case SVM_EXIT_WRITE_DR11:
        case SVM_EXIT_WRITE_DR12: case SVM_EXIT_WRITE_DR13: case SVM_EXIT_WRITE_DR14: case SVM_EXIT_WRITE_DR15:
            SET_BOTH(DRX_WRITE);
            uEventArg = uExitCode - SVM_EXIT_WRITE_DR0;
            break;
        case SVM_EXIT_MSR:
            if (pSvmTransient->pVmcb->ctrl.u64ExitInfo1 == SVM_EXIT1_MSR_WRITE)
                SET_BOTH(WRMSR);
            else
                SET_BOTH(RDMSR);
            break;
        case SVM_EXIT_MWAIT_ARMED:
        case SVM_EXIT_MWAIT:          SET_BOTH(MWAIT); break;
        case SVM_EXIT_MONITOR:        SET_BOTH(MONITOR); break;
        case SVM_EXIT_PAUSE:          SET_BOTH(PAUSE); break;
        case SVM_EXIT_IDTR_READ:      SET_BOTH(SIDT); break;
        case SVM_EXIT_IDTR_WRITE:     SET_BOTH(LIDT); break;
        case SVM_EXIT_GDTR_READ:      SET_BOTH(SGDT); break;
        case SVM_EXIT_GDTR_WRITE:     SET_BOTH(LGDT); break;
        case SVM_EXIT_LDTR_READ:      SET_BOTH(SLDT); break;
        case SVM_EXIT_LDTR_WRITE:     SET_BOTH(LLDT); break;
        case SVM_EXIT_TR_READ:        SET_BOTH(STR); break;
        case SVM_EXIT_TR_WRITE:       SET_BOTH(LTR); break;
        case SVM_EXIT_WBINVD:         SET_BOTH(WBINVD); break;
        case SVM_EXIT_XSETBV:         SET_BOTH(XSETBV); break;

        case SVM_EXIT_SHUTDOWN:
            enmEvent1 = DBGFEVENT_TRIPLE_FAULT;
            //fDtrace1  = VBOXVMM_EXIT_TRIPLE_FAULT_ENABLED();
            break;

        default:
            AssertMsgFailed(("Unexpected VM-exit=%#x\n", uExitCode));
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
        hmR0SvmImportGuestState(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
        PCPUMCTX pCtx = &pVCpu->cpum.GstCtx; RT_NOREF(pCtx); /* Shut up Clang 13. */
        switch (enmEvent1)
        {
            /** @todo consider which extra parameters would be helpful for each probe.   */
            case DBGFEVENT_END: break;
            case DBGFEVENT_INTERRUPT_SOFTWARE:      VBOXVMM_INT_SOFTWARE(pVCpu, pCtx, (uint8_t)uEventArg); break;
            case DBGFEVENT_XCPT_DE:                 VBOXVMM_XCPT_DE(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_DB:                 VBOXVMM_XCPT_DB(pVCpu, pCtx, pCtx->dr[6]); break;
            case DBGFEVENT_XCPT_BP:                 VBOXVMM_XCPT_BP(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_OF:                 VBOXVMM_XCPT_OF(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_BR:                 VBOXVMM_XCPT_BR(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_UD:                 VBOXVMM_XCPT_UD(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_NM:                 VBOXVMM_XCPT_NM(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_DF:                 VBOXVMM_XCPT_DF(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_TS:                 VBOXVMM_XCPT_TS(pVCpu, pCtx, (uint32_t)uEventArg); break;
            case DBGFEVENT_XCPT_NP:                 VBOXVMM_XCPT_NP(pVCpu, pCtx, (uint32_t)uEventArg); break;
            case DBGFEVENT_XCPT_SS:                 VBOXVMM_XCPT_SS(pVCpu, pCtx, (uint32_t)uEventArg); break;
            case DBGFEVENT_XCPT_GP:                 VBOXVMM_XCPT_GP(pVCpu, pCtx, (uint32_t)uEventArg); break;
            case DBGFEVENT_XCPT_PF:                 VBOXVMM_XCPT_PF(pVCpu, pCtx, (uint32_t)uEventArg, pCtx->cr2); break;
            case DBGFEVENT_XCPT_MF:                 VBOXVMM_XCPT_MF(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_AC:                 VBOXVMM_XCPT_AC(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_XF:                 VBOXVMM_XCPT_XF(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_VE:                 VBOXVMM_XCPT_VE(pVCpu, pCtx); break;
            case DBGFEVENT_XCPT_SX:                 VBOXVMM_XCPT_SX(pVCpu, pCtx, (uint32_t)uEventArg); break;
            case DBGFEVENT_INSTR_CPUID:             VBOXVMM_INSTR_CPUID(pVCpu, pCtx, pCtx->eax, pCtx->ecx); break;
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
            case DBGFEVENT_INSTR_VMM_CALL:          VBOXVMM_INSTR_VMM_CALL(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_SVM_VMRUN:         VBOXVMM_INSTR_SVM_VMRUN(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_SVM_VMLOAD:        VBOXVMM_INSTR_SVM_VMLOAD(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_SVM_VMSAVE:        VBOXVMM_INSTR_SVM_VMSAVE(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_SVM_STGI:          VBOXVMM_INSTR_SVM_STGI(pVCpu, pCtx); break;
            case DBGFEVENT_INSTR_SVM_CLGI:          VBOXVMM_INSTR_SVM_CLGI(pVCpu, pCtx); break;
            default: AssertMsgFailed(("enmEvent1=%d uExitCode=%d\n", enmEvent1, uExitCode)); break;
        }
        switch (enmEvent2)
        {
            /** @todo consider which extra parameters would be helpful for each probe. */
            case DBGFEVENT_END: break;
            case DBGFEVENT_EXIT_TASK_SWITCH:        VBOXVMM_EXIT_TASK_SWITCH(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_CPUID:              VBOXVMM_EXIT_CPUID(pVCpu, pCtx, pCtx->eax, pCtx->ecx); break;
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
            case DBGFEVENT_EXIT_VMM_CALL:           VBOXVMM_EXIT_VMM_CALL(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_SVM_VMRUN:          VBOXVMM_EXIT_SVM_VMRUN(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_SVM_VMLOAD:         VBOXVMM_EXIT_SVM_VMLOAD(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_SVM_VMSAVE:         VBOXVMM_EXIT_SVM_VMSAVE(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_SVM_STGI:           VBOXVMM_EXIT_SVM_STGI(pVCpu, pCtx); break;
            case DBGFEVENT_EXIT_SVM_CLGI:           VBOXVMM_EXIT_SVM_CLGI(pVCpu, pCtx); break;
            default: AssertMsgFailed(("enmEvent2=%d uExitCode=%d\n", enmEvent2, uExitCode)); break;
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
    PVMCC        pVM = pVCpu->CTX_SUFF(pVM);
    VBOXSTRICTRC rcStrict;
    if (   enmEvent1 != DBGFEVENT_END
        && DBGF_IS_EVENT_ENABLED(pVM, enmEvent1))
    {
        hmR0SvmImportGuestState(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP);
        rcStrict = DBGFEventGenericWithArgs(pVM, pVCpu, enmEvent1, DBGFEVENTCTX_HM, 1, uEventArg);
    }
    else if (   enmEvent2 != DBGFEVENT_END
             && DBGF_IS_EVENT_ENABLED(pVM, enmEvent2))
    {
        hmR0SvmImportGuestState(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP);
        rcStrict = DBGFEventGenericWithArgs(pVM, pVCpu, enmEvent2, DBGFEVENTCTX_HM, 1, uEventArg);
    }
    else
        rcStrict = VINF_SUCCESS;
    return rcStrict;
}


/**
 * Handles a guest \#VMEXIT (for all EXITCODE values except SVM_EXIT_INVALID),
 * debug variant.
 *
 * @returns Strict VBox status code (informational status codes included).
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM transient structure.
 * @param   pDbgState       The runtime debug state.
 */
static VBOXSTRICTRC hmR0SvmDebugHandleExit(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient, PSVMRUNDBGSTATE pDbgState)
{
    Assert(pSvmTransient->u64ExitCode != SVM_EXIT_INVALID);
    Assert(pSvmTransient->u64ExitCode <= SVM_EXIT_MAX);

    /*
     * Expensive (saves context) generic dtrace VM-exit probe.
     */
    uint64_t const uExitCode = pSvmTransient->u64ExitCode;
    if (!VBOXVMM_R0_HMSVM_VMEXIT_ENABLED())
    { /* more likely */ }
    else
    {
        hmR0SvmImportGuestState(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
        VBOXVMM_R0_HMSVM_VMEXIT(pVCpu, &pVCpu->cpum.GstCtx, uExitCode, pSvmTransient->pVmcb);
    }

    /*
     * Check for single stepping event if we're stepping.
     */
    if (pVCpu->hm.s.fSingleInstruction)
    {
        switch (uExitCode)
        {
            /* Various events: */
            case SVM_EXIT_XCPT_0:  case SVM_EXIT_XCPT_1:  case SVM_EXIT_XCPT_2:  case SVM_EXIT_XCPT_3:
            case SVM_EXIT_XCPT_4:  case SVM_EXIT_XCPT_5:  case SVM_EXIT_XCPT_6:  case SVM_EXIT_XCPT_7:
            case SVM_EXIT_XCPT_8:  case SVM_EXIT_XCPT_9:  case SVM_EXIT_XCPT_10: case SVM_EXIT_XCPT_11:
            case SVM_EXIT_XCPT_12: case SVM_EXIT_XCPT_13: case SVM_EXIT_XCPT_14: case SVM_EXIT_XCPT_15:
            case SVM_EXIT_XCPT_16: case SVM_EXIT_XCPT_17: case SVM_EXIT_XCPT_18: case SVM_EXIT_XCPT_19:
            case SVM_EXIT_XCPT_20: case SVM_EXIT_XCPT_21: case SVM_EXIT_XCPT_22: case SVM_EXIT_XCPT_23:
            case SVM_EXIT_XCPT_24: case SVM_EXIT_XCPT_25: case SVM_EXIT_XCPT_26: case SVM_EXIT_XCPT_27:
            case SVM_EXIT_XCPT_28: case SVM_EXIT_XCPT_29: case SVM_EXIT_XCPT_30: case SVM_EXIT_XCPT_31:
            case SVM_EXIT_INTR:
            case SVM_EXIT_NMI:
            case SVM_EXIT_VINTR:
            case SVM_EXIT_NPF:
            case SVM_EXIT_AVIC_NOACCEL:

            /* Instruction specific VM-exits: */
            case SVM_EXIT_READ_CR0:   case SVM_EXIT_READ_CR1:   case SVM_EXIT_READ_CR2:   case SVM_EXIT_READ_CR3:
            case SVM_EXIT_READ_CR4:   case SVM_EXIT_READ_CR5:   case SVM_EXIT_READ_CR6:   case SVM_EXIT_READ_CR7:
            case SVM_EXIT_READ_CR8:   case SVM_EXIT_READ_CR9:   case SVM_EXIT_READ_CR10:  case SVM_EXIT_READ_CR11:
            case SVM_EXIT_READ_CR12:  case SVM_EXIT_READ_CR13:  case SVM_EXIT_READ_CR14:  case SVM_EXIT_READ_CR15:
            case SVM_EXIT_WRITE_CR0:  case SVM_EXIT_WRITE_CR1:  case SVM_EXIT_WRITE_CR2:  case SVM_EXIT_WRITE_CR3:
            case SVM_EXIT_WRITE_CR4:  case SVM_EXIT_WRITE_CR5:  case SVM_EXIT_WRITE_CR6:  case SVM_EXIT_WRITE_CR7:
            case SVM_EXIT_WRITE_CR8:  case SVM_EXIT_WRITE_CR9:  case SVM_EXIT_WRITE_CR10: case SVM_EXIT_WRITE_CR11:
            case SVM_EXIT_WRITE_CR12: case SVM_EXIT_WRITE_CR13: case SVM_EXIT_WRITE_CR14: case SVM_EXIT_WRITE_CR15:
            case SVM_EXIT_READ_DR0:   case SVM_EXIT_READ_DR1:   case SVM_EXIT_READ_DR2:   case SVM_EXIT_READ_DR3:
            case SVM_EXIT_READ_DR4:   case SVM_EXIT_READ_DR5:   case SVM_EXIT_READ_DR6:   case SVM_EXIT_READ_DR7:
            case SVM_EXIT_READ_DR8:   case SVM_EXIT_READ_DR9:   case SVM_EXIT_READ_DR10:  case SVM_EXIT_READ_DR11:
            case SVM_EXIT_READ_DR12:  case SVM_EXIT_READ_DR13:  case SVM_EXIT_READ_DR14:  case SVM_EXIT_READ_DR15:
            case SVM_EXIT_WRITE_DR0:  case SVM_EXIT_WRITE_DR1:  case SVM_EXIT_WRITE_DR2:  case SVM_EXIT_WRITE_DR3:
            case SVM_EXIT_WRITE_DR4:  case SVM_EXIT_WRITE_DR5:  case SVM_EXIT_WRITE_DR6:  case SVM_EXIT_WRITE_DR7:
            case SVM_EXIT_WRITE_DR8:  case SVM_EXIT_WRITE_DR9:  case SVM_EXIT_WRITE_DR10: case SVM_EXIT_WRITE_DR11:
            case SVM_EXIT_WRITE_DR12: case SVM_EXIT_WRITE_DR13: case SVM_EXIT_WRITE_DR14: case SVM_EXIT_WRITE_DR15:
            case SVM_EXIT_CR0_SEL_WRITE:
            case SVM_EXIT_IDTR_READ:
            case SVM_EXIT_GDTR_READ:
            case SVM_EXIT_LDTR_READ:
            case SVM_EXIT_TR_READ:
            case SVM_EXIT_IDTR_WRITE:
            case SVM_EXIT_GDTR_WRITE:
            case SVM_EXIT_LDTR_WRITE:
            case SVM_EXIT_TR_WRITE:
            case SVM_EXIT_RDTSC:
            case SVM_EXIT_RDPMC:
            case SVM_EXIT_PUSHF:
            case SVM_EXIT_POPF:
            case SVM_EXIT_CPUID:
            case SVM_EXIT_RSM:
            case SVM_EXIT_IRET:
            case SVM_EXIT_SWINT:
            case SVM_EXIT_INVD:
            case SVM_EXIT_PAUSE:
            case SVM_EXIT_HLT:
            case SVM_EXIT_INVLPG:
            case SVM_EXIT_INVLPGA:
            case SVM_EXIT_IOIO:
            case SVM_EXIT_MSR:
            case SVM_EXIT_TASK_SWITCH:
            case SVM_EXIT_VMRUN:
            case SVM_EXIT_VMMCALL:
            case SVM_EXIT_VMLOAD:
            case SVM_EXIT_VMSAVE:
            case SVM_EXIT_STGI:
            case SVM_EXIT_CLGI:
            case SVM_EXIT_SKINIT:
            case SVM_EXIT_RDTSCP:
            case SVM_EXIT_ICEBP:
            case SVM_EXIT_WBINVD:
            case SVM_EXIT_MONITOR:
            case SVM_EXIT_MWAIT:
            case SVM_EXIT_MWAIT_ARMED:
            case SVM_EXIT_XSETBV:
            case SVM_EXIT_RDPRU:
            case SVM_EXIT_WRITE_EFER_TRAP:
            case SVM_EXIT_WRITE_CR0_TRAP:  case SVM_EXIT_WRITE_CR1_TRAP:  case SVM_EXIT_WRITE_CR2_TRAP:  case SVM_EXIT_WRITE_CR3_TRAP:
            case SVM_EXIT_WRITE_CR4_TRAP:  case SVM_EXIT_WRITE_CR5_TRAP:  case SVM_EXIT_WRITE_CR6_TRAP:  case SVM_EXIT_WRITE_CR7_TRAP:
            case SVM_EXIT_WRITE_CR8_TRAP:  case SVM_EXIT_WRITE_CR9_TRAP:  case SVM_EXIT_WRITE_CR10_TRAP: case SVM_EXIT_WRITE_CR11_TRAP:
            case SVM_EXIT_WRITE_CR12_TRAP: case SVM_EXIT_WRITE_CR13_TRAP: case SVM_EXIT_WRITE_CR14_TRAP: case SVM_EXIT_WRITE_CR15_TRAP:
            case SVM_EXIT_MCOMMIT:
            {
                hmR0SvmImportGuestState(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP);
                if (   pVCpu->cpum.GstCtx.rip    != pDbgState->uRipStart
                    || pVCpu->cpum.GstCtx.cs.Sel != pDbgState->uCsStart)
                {
                    Log6Func(("VINF_EM_DBG_STEPPED: %04x:%08RX64 (exit %u)\n",
                              pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uExitCode));
                    return VINF_EM_DBG_STEPPED;
                }
                break;
            }

            /* Errors and unexpected events: */
            case SVM_EXIT_FERR_FREEZE:
            case SVM_EXIT_SHUTDOWN:
            case SVM_EXIT_AVIC_INCOMPLETE_IPI:
                break;

            case SVM_EXIT_SMI:
            case SVM_EXIT_INIT:
            default:
                AssertMsgFailed(("Unexpected VM-exit=%#x\n", uExitCode));
                break;
        }
    }

    /*
     * Check for debugger event breakpoints and dtrace probes.
     */
    if (   uExitCode < sizeof(pDbgState->bmExitsToCheck) * 8U
        && ASMBitTest(pDbgState->bmExitsToCheck, uExitCode) )
    {
        VBOXSTRICTRC rcStrict = hmR0SvmHandleExitDtraceEvents(pVCpu, pSvmTransient, uExitCode);
        if (rcStrict != VINF_SUCCESS)
        {
            Log6Func(("%04x:%08RX64 (exit %u) -> %Rrc\n",
                      pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uExitCode, VBOXSTRICTRC_VAL(rcStrict) ));
            return rcStrict;
        }
    }

    /*
     * Normal processing.
     */
    return hmR0SvmHandleExit(pVCpu, pSvmTransient);
}


/**
 * Runs the guest code using AMD-V in single step mode.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pcLoops     Pointer to the number of executed loops.
 */
static VBOXSTRICTRC hmR0SvmRunGuestCodeDebug(PVMCPUCC pVCpu, uint32_t *pcLoops)
{
    uint32_t const cMaxResumeLoops = pVCpu->CTX_SUFF(pVM)->hmr0.s.cMaxResumeLoops;
    Assert(pcLoops);
    Assert(*pcLoops <= cMaxResumeLoops);

    SVMTRANSIENT SvmTransient;
    RT_ZERO(SvmTransient);
    SvmTransient.fUpdateTscOffsetting = true;
    SvmTransient.pVmcb = pVCpu->hmr0.s.svm.pVmcb;

    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;

    /* Set HMCPU indicators.  */
    bool const fSavedSingleInstruction = pVCpu->hm.s.fSingleInstruction;
    pVCpu->hm.s.fSingleInstruction     = pVCpu->hm.s.fSingleInstruction || DBGFIsStepping(pVCpu);
    pVCpu->hmr0.s.fDebugWantRdTscExit  = false;
    pVCpu->hmr0.s.fUsingDebugLoop      = true;

    /* State we keep to help modify and later restore the VMCS fields we alter, and for detecting steps.  */
    SVMRUNDBGSTATE DbgState;
    hmR0SvmRunDebugStateInit(pVCpu, &SvmTransient, &DbgState);
    hmR0SvmPreRunGuestDebugStateUpdate(pVCpu, &SvmTransient, &DbgState);

    /*
     * The loop.
     */
    VBOXSTRICTRC rc = VERR_INTERNAL_ERROR_5;
    for (;;)
    {
        Assert(!HMR0SuspendPending());
        AssertMsg(pVCpu->hmr0.s.idEnteredCpu == RTMpCpuId(),
                  ("Illegal migration! Entered on CPU %u Current %u cLoops=%u\n", (unsigned)pVCpu->hmr0.s.idEnteredCpu,
                  (unsigned)RTMpCpuId(), *pcLoops));
        bool fStepping = pVCpu->hm.s.fSingleInstruction;

        /* Set up VM-execution controls the next two can respond to. */
        hmR0SvmPreRunGuestDebugStateApply(&SvmTransient, &DbgState);

        /* Preparatory work for running nested-guest code, this may force us to return to
           ring-3.  This bugger disables interrupts on VINF_SUCCESS! */
        STAM_PROFILE_ADV_START(&pVCpu->hm.s.StatEntry, x);
        rc = hmR0SvmPreRunGuest(pVCpu, &SvmTransient);
        if (rc != VINF_SUCCESS)
            break;

        /*
         * No longjmps to ring-3 from this point on!!!
         *
         * Asserts() will still longjmp to ring-3 (but won't return), which is intentional,
         * better than a kernel panic. This also disables flushing of the R0-logger instance.
         */
        hmR0SvmPreRunGuestCommitted(pVCpu, &SvmTransient);

        /* Override any obnoxious code in the above two calls. */
        hmR0SvmPreRunGuestDebugStateApply(&SvmTransient, &DbgState);
#if 0
        Log(("%04x:%08RX64 ds=%04x %04x:%08RX64 i=%#RX64\n",
             SvmTransient.pVmcb->guest.CS.u16Sel, SvmTransient.pVmcb->guest.u64RIP, SvmTransient.pVmcb->guest.DS.u16Sel,
             SvmTransient.pVmcb->guest.SS.u16Sel, SvmTransient.pVmcb->guest.u64RSP, SvmTransient.pVmcb->ctrl.EventInject.u));
#endif

        /*
         * Finally execute guest code.
         */
        rc = hmR0SvmRunGuest(pVCpu, pVCpu->hmr0.s.svm.HCPhysVmcb);

        /* Restore any residual host-state and save any bits shared between host and guest
           into the guest-CPU state.  Re-enables interrupts! */
        hmR0SvmPostRunGuest(pVCpu, &SvmTransient, rc);
#if 0
        Log(("%04x:%08RX64 ds=%04x %04x:%08RX64 i=%#RX64 exit=%d\n",
             SvmTransient.pVmcb->guest.CS.u16Sel, SvmTransient.pVmcb->guest.u64RIP, SvmTransient.pVmcb->guest.DS.u16Sel,
             SvmTransient.pVmcb->guest.SS.u16Sel, SvmTransient.pVmcb->guest.u64RSP, SvmTransient.pVmcb->ctrl.EventInject.u, SvmTransient.u64ExitCode));
#endif

        if (RT_LIKELY(   rc == VINF_SUCCESS                               /* Check for VMRUN errors. */
                      && SvmTransient.u64ExitCode != SVM_EXIT_INVALID))   /* Check for invalid guest-state errors. */
        { /* very likely*/ }
        else
        {
            if (rc == VINF_SUCCESS)
                rc = VERR_SVM_INVALID_GUEST_STATE;
            STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatPreExit, x);
            hmR0SvmReportWorldSwitchError(pVCpu, VBOXSTRICTRC_VAL(rc));
            return rc;
        }

        /* Handle the #VMEXIT. */
        HMSVM_DEBUG_EXITCODE_STAM_COUNTER_INC(SvmTransient.u64ExitCode);
        STAM_PROFILE_ADV_STOP_START(&pVCpu->hm.s.StatPreExit, &pVCpu->hm.s.StatExitHandling, x);
        VBOXVMM_R0_HMSVM_VMEXIT(pVCpu, pCtx, SvmTransient.u64ExitCode, pVCpu->hmr0.s.svm.pVmcb);
        rc = hmR0SvmDebugHandleExit(pVCpu, &SvmTransient, &DbgState);
        STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatExitHandling, x);
        if (rc != VINF_SUCCESS)
            break;
        if (++(*pcLoops) >= cMaxResumeLoops)
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchMaxResumeLoops);
            rc = VINF_EM_RAW_INTERRUPT;
            break;
        }

        /*
         * Stepping: Did the RIP change, if so, consider it a single step.
         * Otherwise, make sure one of the TFs gets set.
         */
        if (fStepping)
        {
            hmR0SvmImportGuestState(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP);
            if (   pVCpu->cpum.GstCtx.rip    != DbgState.uRipStart
                || pVCpu->cpum.GstCtx.cs.Sel != DbgState.uCsStart)
            {
                Log6Func(("VINF_EM_DBG_STEPPED: %04x:%08RX64 (exit %u)\n",
                          pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, SvmTransient.u64ExitCode));
                rc = VINF_EM_DBG_STEPPED;
                break;
            }
            ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_DR7);
        }

        /*
         * Update when dtrace settings changes (DBGF kicks us, so no need to check).
         * Revert the state changes afterware so we can drop intercepts no longer needed.
         */
        if (VBOXVMM_GET_SETTINGS_SEQ_NO() != DbgState.uDtraceSettingsSeqNo)
        {
            hmR0SvmPreRunGuestDebugStateUpdate(pVCpu, &SvmTransient, &DbgState);
            hmR0SvmRunDebugStateRevert(&SvmTransient, &DbgState);
        }
    }

    /*
     * Clear the X86_EFL_TF if necessary.
     */
    if (pVCpu->hmr0.s.fClearTrapFlag)
    {
        pVCpu->hmr0.s.fClearTrapFlag = false;
        pCtx->eflags.Bits.u1TF = 0;
    }

    /* Restore HMCPU indicators. */
    pVCpu->hmr0.s.fUsingDebugLoop     = false;
    pVCpu->hmr0.s.fDebugWantRdTscExit = false;
    pVCpu->hm.s.fSingleInstruction    = fSavedSingleInstruction;

    /* Restore all controls applied by hmR0SvmPreRunGuestDebugStateApply above. */
    hmR0SvmRunDebugStateRevert(&SvmTransient, &DbgState);

    STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatEntry, x);
    return rc;
}

/** @}  */

#undef VMEXIT_CALL_RET


#ifdef VBOX_STRICT
/* Is there some generic IPRT define for this that are not in Runtime/internal/\* ?? */
# define HMSVM_ASSERT_PREEMPT_CPUID_VAR() \
    RTCPUID const idAssertCpu = RTThreadPreemptIsEnabled(NIL_RTTHREAD) ? NIL_RTCPUID : RTMpCpuId()

# define HMSVM_ASSERT_PREEMPT_CPUID() \
    do \
    { \
         RTCPUID const idAssertCpuNow = RTThreadPreemptIsEnabled(NIL_RTTHREAD) ? NIL_RTCPUID : RTMpCpuId(); \
         AssertMsg(idAssertCpu == idAssertCpuNow, ("SVM %#x, %#x\n", idAssertCpu, idAssertCpuNow)); \
    } while (0)

# define HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(a_pVCpu, a_pSvmTransient) \
    do { \
        AssertPtr((a_pVCpu)); \
        AssertPtr((a_pSvmTransient)); \
        Assert(ASMIntAreEnabled()); \
        HMSVM_ASSERT_PREEMPT_SAFE((a_pVCpu)); \
        HMSVM_ASSERT_PREEMPT_CPUID_VAR(); \
        Log4Func(("vcpu[%u] -v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-v-\n", (a_pVCpu)->idCpu)); \
        HMSVM_ASSERT_PREEMPT_SAFE((a_pVCpu)); \
        if (!VMMRZCallRing3IsEnabled((a_pVCpu))) \
            HMSVM_ASSERT_PREEMPT_CPUID(); \
    } while (0)
#else
# define HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(a_pVCpu, a_pSvmTransient) \
    do { \
        RT_NOREF2(a_pVCpu, a_pSvmTransient); \
    } while (0)
#endif


/**
 * Gets the IEM exception flags for the specified SVM event.
 *
 * @returns The IEM exception flags.
 * @param   pEvent      Pointer to the SVM event.
 *
 * @remarks This function currently only constructs flags required for
 *          IEMEvaluateRecursiveXcpt and not the complete flags (e.g. error-code
 *          and CR2 aspects of an exception are not included).
 */
static uint32_t hmR0SvmGetIemXcptFlags(PCSVMEVENT pEvent)
{
    uint8_t const uEventType = pEvent->n.u3Type;
    uint32_t      fIemXcptFlags;
    switch (uEventType)
    {
        case SVM_EVENT_EXCEPTION:
            /*
             * Only INT3 and INTO instructions can raise #BP and #OF exceptions.
             * See AMD spec. Table 8-1. "Interrupt Vector Source and Cause".
             */
            if (pEvent->n.u8Vector == X86_XCPT_BP)
            {
                fIemXcptFlags = IEM_XCPT_FLAGS_T_SOFT_INT | IEM_XCPT_FLAGS_BP_INSTR;
                break;
            }
            if (pEvent->n.u8Vector == X86_XCPT_OF)
            {
                fIemXcptFlags = IEM_XCPT_FLAGS_T_SOFT_INT | IEM_XCPT_FLAGS_OF_INSTR;
                break;
            }
            /** @todo How do we distinguish ICEBP \#DB from the regular one? */
            RT_FALL_THRU();
        case SVM_EVENT_NMI:
            fIemXcptFlags = IEM_XCPT_FLAGS_T_CPU_XCPT;
            break;

        case SVM_EVENT_EXTERNAL_IRQ:
            fIemXcptFlags = IEM_XCPT_FLAGS_T_EXT_INT;
            break;

        case SVM_EVENT_SOFTWARE_INT:
            fIemXcptFlags = IEM_XCPT_FLAGS_T_SOFT_INT;
            break;

        default:
            fIemXcptFlags = 0;
            AssertMsgFailed(("Unexpected event type! uEventType=%#x uVector=%#x", uEventType, pEvent->n.u8Vector));
            break;
    }
    return fIemXcptFlags;
}


/**
 * Handle a condition that occurred while delivering an event through the guest
 * IDT.
 *
 * @returns VBox status code (informational error codes included).
 * @retval VINF_SUCCESS if we should continue handling the \#VMEXIT.
 * @retval VINF_HM_DOUBLE_FAULT if a \#DF condition was detected and we ought to
 *         continue execution of the guest which will delivery the \#DF.
 * @retval VINF_EM_RESET if we detected a triple-fault condition.
 * @retval VERR_EM_GUEST_CPU_HANG if we detected a guest CPU hang.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pSvmTransient   Pointer to the SVM transient structure.
 *
 * @remarks No-long-jump zone!!!
 */
static int hmR0SvmCheckExitDueToEventDelivery(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    /** @todo r=bird: Looks like this is called on many exits and we start by
     * loading CR2 on the offchance that we actually have work to do here.
     *
     * HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY can surely check
     * pVmcb->ctrl.ExitIntInfo.n.u1Valid, can't it?
     *
     * Also, what's the deal with hmR0SvmGetCurrentVmcb() vs pSvmTransient->pVmcb?
     */
    int rc = VINF_SUCCESS;
    PSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_CR2);

    Log4(("EXITINTINFO: Pending vectoring event %#RX64 Valid=%RTbool ErrValid=%RTbool Err=%#RX32 Type=%u Vector=%u\n",
          pVmcb->ctrl.ExitIntInfo.u, !!pVmcb->ctrl.ExitIntInfo.n.u1Valid, !!pVmcb->ctrl.ExitIntInfo.n.u1ErrorCodeValid,
          pVmcb->ctrl.ExitIntInfo.n.u32ErrorCode, pVmcb->ctrl.ExitIntInfo.n.u3Type, pVmcb->ctrl.ExitIntInfo.n.u8Vector));

    /*
     * The EXITINTINFO (if valid) contains the prior exception (IDT vector) that was trying to
     * be delivered to the guest which caused a #VMEXIT which was intercepted (Exit vector).
     *
     * See AMD spec. 15.7.3 "EXITINFO Pseudo-Code".
     */
    if (pVmcb->ctrl.ExitIntInfo.n.u1Valid)
    {
        IEMXCPTRAISE     enmRaise;
        IEMXCPTRAISEINFO fRaiseInfo;
        bool const       fExitIsHwXcpt  = pSvmTransient->u64ExitCode - SVM_EXIT_XCPT_0 <= SVM_EXIT_XCPT_31;
        uint8_t const    uIdtVector     = pVmcb->ctrl.ExitIntInfo.n.u8Vector;
        if (fExitIsHwXcpt)
        {
            uint8_t  const uExitVector      = pSvmTransient->u64ExitCode - SVM_EXIT_XCPT_0;
            uint32_t const fIdtVectorFlags  = hmR0SvmGetIemXcptFlags(&pVmcb->ctrl.ExitIntInfo);
            uint32_t const fExitVectorFlags = IEM_XCPT_FLAGS_T_CPU_XCPT;
            enmRaise = IEMEvaluateRecursiveXcpt(pVCpu, fIdtVectorFlags, uIdtVector, fExitVectorFlags, uExitVector, &fRaiseInfo);
        }
        else
        {
            /*
             * If delivery of an event caused a #VMEXIT that is not an exception (e.g. #NPF)
             * then we end up here.
             *
             * If the event was:
             *   - a software interrupt, we can re-execute the instruction which will
             *     regenerate the event.
             *   - an NMI, we need to clear NMI blocking and re-inject the NMI.
             *   - a hardware exception or external interrupt, we re-inject it.
             */
            fRaiseInfo = IEMXCPTRAISEINFO_NONE;
            if (pVmcb->ctrl.ExitIntInfo.n.u3Type == SVM_EVENT_SOFTWARE_INT)
                enmRaise = IEMXCPTRAISE_REEXEC_INSTR;
            else
                enmRaise = IEMXCPTRAISE_PREV_EVENT;
        }

        switch (enmRaise)
        {
            case IEMXCPTRAISE_CURRENT_XCPT:
            case IEMXCPTRAISE_PREV_EVENT:
            {
                /* For software interrupts, we shall re-execute the instruction. */
                if (!(fRaiseInfo & IEMXCPTRAISEINFO_SOFT_INT_XCPT))
                {
                    RTGCUINTPTR GCPtrFaultAddress = 0;

                    /* If we are re-injecting an NMI, clear NMI blocking. */
                    if (pVmcb->ctrl.ExitIntInfo.n.u3Type == SVM_EVENT_NMI)
                        CPUMClearInterruptInhibitingByNmi(&pVCpu->cpum.GstCtx);

                    /* Determine a vectoring #PF condition, see comment in hmR0SvmExitXcptPF(). */
                    if (fRaiseInfo & (IEMXCPTRAISEINFO_EXT_INT_PF | IEMXCPTRAISEINFO_NMI_PF))
                    {
                        pSvmTransient->fVectoringPF = true;
                        Log4Func(("IDT: Pending vectoring #PF due to delivery of Ext-Int/NMI. uCR2=%#RX64\n",
                                  pVCpu->cpum.GstCtx.cr2));
                    }
                    else if (   pVmcb->ctrl.ExitIntInfo.n.u3Type == SVM_EVENT_EXCEPTION
                             && uIdtVector == X86_XCPT_PF)
                    {
                        /*
                         * If the previous exception was a #PF, we need to recover the CR2 value.
                         * This can't happen with shadow paging.
                         */
                        GCPtrFaultAddress = pVCpu->cpum.GstCtx.cr2;
                    }

                    /*
                     * Without nested paging, when uExitVector is #PF, CR2 value will be updated from the VMCB's
                     * exit info. fields, if it's a guest #PF, see hmR0SvmExitXcptPF().
                     */
                    Assert(pVmcb->ctrl.ExitIntInfo.n.u3Type != SVM_EVENT_SOFTWARE_INT);
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatInjectReflect);
                    hmR0SvmSetPendingEvent(pVCpu, &pVmcb->ctrl.ExitIntInfo, GCPtrFaultAddress);

                    Log4Func(("IDT: Pending vectoring event %#RX64 ErrValid=%RTbool Err=%#RX32 GCPtrFaultAddress=%#RX64\n",
                              pVmcb->ctrl.ExitIntInfo.u, RT_BOOL(pVmcb->ctrl.ExitIntInfo.n.u1ErrorCodeValid),
                              pVmcb->ctrl.ExitIntInfo.n.u32ErrorCode, GCPtrFaultAddress));
                }
                break;
            }

            case IEMXCPTRAISE_REEXEC_INSTR:
            {
                Assert(rc == VINF_SUCCESS);
                break;
            }

            case IEMXCPTRAISE_DOUBLE_FAULT:
            {
                /*
                 * Determing a vectoring double #PF condition. Used later, when PGM evaluates
                 * the second #PF as a guest #PF (and not a shadow #PF) and needs to be
                 * converted into a #DF.
                 */
                if (fRaiseInfo & IEMXCPTRAISEINFO_PF_PF)
                {
                    Log4Func(("IDT: Pending vectoring double #PF uCR2=%#RX64\n", pVCpu->cpum.GstCtx.cr2));
                    pSvmTransient->fVectoringDoublePF = true;
                    Assert(rc == VINF_SUCCESS);
                }
                else
                {
                    STAM_COUNTER_INC(&pVCpu->hm.s.StatInjectConvertDF);
                    hmR0SvmSetPendingXcptDF(pVCpu);
                    rc = VINF_HM_DOUBLE_FAULT;
                }
                break;
            }

            case IEMXCPTRAISE_TRIPLE_FAULT:
            {
                rc = VINF_EM_RESET;
                break;
            }

            case IEMXCPTRAISE_CPU_HANG:
            {
                rc = VERR_EM_GUEST_CPU_HANG;
                break;
            }

            default:
                AssertMsgFailedBreakStmt(("Bogus enmRaise value: %d (%#x)\n", enmRaise, enmRaise), rc = VERR_SVM_IPE_2);
        }
    }
    Assert(rc == VINF_SUCCESS || rc == VINF_HM_DOUBLE_FAULT || rc == VINF_EM_RESET || rc == VERR_EM_GUEST_CPU_HANG);
    return rc;
}


/**
 * Advances the guest RIP by the number of bytes specified in @a cb.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cb          RIP increment value in bytes.
 */
DECLINLINE(void) hmR0SvmAdvanceRip(PVMCPUCC pVCpu, uint32_t cb)
{
    pVCpu->cpum.GstCtx.rip += cb;
    CPUMClearInterruptShadow(&pVCpu->cpum.GstCtx);
    /** @todo clear RF. */
}


/* -=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */
/* -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- #VMEXIT handlers -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */
/* -=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */

/** @name \#VMEXIT handlers.
 * @{
 */

/**
 * \#VMEXIT handler for external interrupts, NMIs, FPU assertion freeze and INIT
 * signals (SVM_EXIT_INTR, SVM_EXIT_NMI, SVM_EXIT_FERR_FREEZE, SVM_EXIT_INIT).
 */
HMSVM_EXIT_DECL hmR0SvmExitIntr(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    if (pSvmTransient->u64ExitCode == SVM_EXIT_NMI)
        STAM_REL_COUNTER_INC(&pVCpu->hm.s.StatExitHostNmiInGC);
    else if (pSvmTransient->u64ExitCode == SVM_EXIT_INTR)
        STAM_COUNTER_INC(&pVCpu->hm.s.StatExitExtInt);

    /*
     * AMD-V has no preemption timer and the generic periodic preemption timer has no way to
     * signal -before- the timer fires if the current interrupt is our own timer or a some
     * other host interrupt. We also cannot examine what interrupt it is until the host
     * actually take the interrupt.
     *
     * Going back to executing guest code here unconditionally causes random scheduling
     * problems (observed on an AMD Phenom 9850 Quad-Core on Windows 64-bit host).
     */
    return VINF_EM_RAW_INTERRUPT;
}


/**
 * \#VMEXIT handler for WBINVD (SVM_EXIT_WBINVD). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitWbinvd(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK);
        PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedWbinvd(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for INVD (SVM_EXIT_INVD). Unconditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitInvd(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK);
        PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedInvd(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for INVD (SVM_EXIT_CPUID). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitCpuid(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RCX);
    VBOXSTRICTRC rcStrict;
    PCEMEXITREC pExitRec = EMHistoryUpdateFlagsAndTypeAndPC(pVCpu,
                                                            EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_CPUID),
                                                            pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base);
    if (!pExitRec)
    {
        bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
        if (fSupportsNextRipSave)
        {
            PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
            uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
            rcStrict = IEMExecDecodedCpuid(pVCpu, cbInstr);
        }
        else
        {
            HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
            rcStrict = IEMExecOne(pVCpu);
        }

        if (rcStrict == VINF_IEM_RAISED_XCPT)
        {
            CPUM_ASSERT_NOT_EXTRN(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);
            ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
            rcStrict = VINF_SUCCESS;
        }
        HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    }
    else
    {
        /*
         * Frequent exit or something needing probing.  Get state and call EMHistoryExec.
         */
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);

        Log4(("CpuIdExit/%u: %04x:%08RX64: %#x/%#x -> EMHistoryExec\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.eax, pVCpu->cpum.GstCtx.ecx));

        rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);

        Log4(("CpuIdExit/%u: %04x:%08RX64: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    }
    return rcStrict;
}


/**
 * \#VMEXIT handler for RDTSC (SVM_EXIT_RDTSC). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitRdtsc(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_CR4);
        PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedRdtsc(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_SUCCESS)
        pSvmTransient->fUpdateTscOffsetting = true;
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for RDTSCP (SVM_EXIT_RDTSCP). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitRdtscp(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_TSC_AUX);
        PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedRdtscp(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_SUCCESS)
        pSvmTransient->fUpdateTscOffsetting = true;
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for RDPMC (SVM_EXIT_RDPMC). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitRdpmc(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_CR4);
        PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedRdpmc(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for INVLPG (SVM_EXIT_INVLPG). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitInvlpg(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    Assert(!pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsDecodeAssists = hmR0SvmSupportsDecodeAssists(pVCpu);
    bool const fSupportsNextRipSave   = hmR0SvmSupportsNextRipSave(pVCpu);
    if (   fSupportsDecodeAssists
        && fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK);
        PCSVMVMCB     pVmcb     = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr   = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        RTGCPTR const GCPtrPage = pVmcb->ctrl.u64ExitInfo1;
        rcStrict = IEMExecDecodedInvlpg(pVCpu, cbInstr, GCPtrPage);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return VBOXSTRICTRC_VAL(rcStrict);
}


/**
 * \#VMEXIT handler for HLT (SVM_EXIT_HLT). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitHlt(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK);
        PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedHlt(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (   rcStrict == VINF_EM_HALT
        || rcStrict == VINF_SUCCESS)
        rcStrict = EMShouldContinueAfterHalt(pVCpu, &pVCpu->cpum.GstCtx) ? VINF_SUCCESS : VINF_EM_HALT;
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    if (rcStrict != VINF_SUCCESS)
        STAM_COUNTER_INC(&pVCpu->hm.s.StatSwitchHltToR3);
    return VBOXSTRICTRC_VAL(rcStrict);;
}


/**
 * \#VMEXIT handler for MONITOR (SVM_EXIT_MONITOR). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitMonitor(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    /*
     * If the instruction length is supplied by the CPU is 3 bytes, we can be certain that no
     * segment override prefix is present (and thus use the default segment DS). Otherwise, a
     * segment override prefix or other prefixes might be used, in which case we fallback to
     * IEMExecOne() to figure out.
     */
    VBOXSTRICTRC  rcStrict;
    PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
    uint8_t const cbInstr = hmR0SvmSupportsNextRipSave(pVCpu) ? pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip : 0;
    if (cbInstr)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_DS);
        rcStrict = IEMExecDecodedMonitor(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for MWAIT (SVM_EXIT_MWAIT). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitMwait(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK);
        PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedMwait(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (   rcStrict == VINF_EM_HALT
        && EMMonitorWaitShouldContinue(pVCpu, &pVCpu->cpum.GstCtx))
        rcStrict = VINF_SUCCESS;
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for shutdown (triple-fault) (SVM_EXIT_SHUTDOWN). Conditional
 * \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitShutdown(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
    return VINF_EM_RESET;
}


/**
 * \#VMEXIT handler for unexpected exits. Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitUnexpected(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
    AssertMsgFailed(("hmR0SvmExitUnexpected: ExitCode=%#RX64 uExitInfo1=%#RX64 uExitInfo2=%#RX64\n", pSvmTransient->u64ExitCode,
                     pVmcb->ctrl.u64ExitInfo1, pVmcb->ctrl.u64ExitInfo2));
    RT_NOREF(pVmcb);
    pVCpu->hm.s.u32HMError = (uint32_t)pSvmTransient->u64ExitCode;
    return VERR_SVM_UNEXPECTED_EXIT;
}


/**
 * \#VMEXIT handler for CRx reads (SVM_EXIT_READ_CR*). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitReadCRx(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    Log4Func(("CS:RIP=%04x:%RX64\n", pCtx->cs.Sel, pCtx->rip));
#ifdef VBOX_WITH_STATISTICS
    switch (pSvmTransient->u64ExitCode)
    {
        case SVM_EXIT_READ_CR0: STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR0Read); break;
        case SVM_EXIT_READ_CR2: STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR2Read); break;
        case SVM_EXIT_READ_CR3: STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR3Read); break;
        case SVM_EXIT_READ_CR4: STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR4Read); break;
        case SVM_EXIT_READ_CR8: STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR8Read); break;
    }
#endif

    bool const fSupportsDecodeAssists = hmR0SvmSupportsDecodeAssists(pVCpu);
    bool const fSupportsNextRipSave   = hmR0SvmSupportsNextRipSave(pVCpu);
    if (   fSupportsDecodeAssists
        && fSupportsNextRipSave)
    {
        PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
        bool const fMovCRx = RT_BOOL(pVmcb->ctrl.u64ExitInfo1 & SVM_EXIT1_MOV_CRX_MASK);
        if (fMovCRx)
        {
            HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_CR_MASK
                                            | CPUMCTX_EXTRN_APIC_TPR);
            uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pCtx->rip;
            uint8_t const iCrReg  = pSvmTransient->u64ExitCode - SVM_EXIT_READ_CR0;
            uint8_t const iGReg   = pVmcb->ctrl.u64ExitInfo1 & SVM_EXIT1_MOV_CRX_GPR_NUMBER;
            VBOXSTRICTRC rcStrict = IEMExecDecodedMovCRxRead(pVCpu, cbInstr, iGReg, iCrReg);
            HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
            return VBOXSTRICTRC_VAL(rcStrict);
        }
        /* else: SMSW instruction, fall back below to IEM for this. */
    }

    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
    VBOXSTRICTRC rcStrict = IEMExecOne(pVCpu);
    AssertMsg(   rcStrict == VINF_SUCCESS
              || rcStrict == VINF_PGM_SYNC_CR3
              || rcStrict == VINF_IEM_RAISED_XCPT,
              ("hmR0SvmExitReadCRx: IEMExecOne failed rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    Assert((pSvmTransient->u64ExitCode - SVM_EXIT_READ_CR0) <= 15);
    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for CRx writes (SVM_EXIT_WRITE_CR*). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitWriteCRx(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    uint64_t const uExitCode = pSvmTransient->u64ExitCode;
    uint8_t  const iCrReg    = uExitCode == SVM_EXIT_CR0_SEL_WRITE ? 0 : (pSvmTransient->u64ExitCode - SVM_EXIT_WRITE_CR0);
    Assert(iCrReg <= 15);

    VBOXSTRICTRC rcStrict = VERR_SVM_IPE_5;
    PCPUMCTX     pCtx = &pVCpu->cpum.GstCtx;
    bool         fDecodedInstr = false;
    bool const   fSupportsDecodeAssists = hmR0SvmSupportsDecodeAssists(pVCpu);
    bool const   fSupportsNextRipSave   = hmR0SvmSupportsNextRipSave(pVCpu);
    if (   fSupportsDecodeAssists
        && fSupportsNextRipSave)
    {
        PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
        bool const fMovCRx = RT_BOOL(pVmcb->ctrl.u64ExitInfo1 & SVM_EXIT1_MOV_CRX_MASK);
        if (fMovCRx)
        {
            HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4
                                            | CPUMCTX_EXTRN_APIC_TPR);
            uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pCtx->rip;
            uint8_t const iGReg   = pVmcb->ctrl.u64ExitInfo1 & SVM_EXIT1_MOV_CRX_GPR_NUMBER;
            Log4Func(("Mov CR%u w/ iGReg=%#x\n", iCrReg, iGReg));
            rcStrict = IEMExecDecodedMovCRxWrite(pVCpu, cbInstr, iCrReg, iGReg);
            fDecodedInstr = true;
        }
        /* else: LMSW or CLTS instruction, fall back below to IEM for this. */
    }

    if (!fDecodedInstr)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        Log4Func(("iCrReg=%#x\n", iCrReg));
        rcStrict = IEMExecOne(pVCpu);
        if (RT_UNLIKELY(   rcStrict == VERR_IEM_ASPECT_NOT_IMPLEMENTED
                        || rcStrict == VERR_IEM_INSTR_NOT_IMPLEMENTED))
            rcStrict = VERR_EM_INTERPRETER;
    }

    if (rcStrict == VINF_SUCCESS)
    {
        switch (iCrReg)
        {
            case 0:
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_CR0);
                STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR0Write);
                break;

            case 2:
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_CR2);
                STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR2Write);
                break;

            case 3:
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_CR3);
                STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR3Write);
                break;

            case 4:
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_CR4);
                STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR4Write);
                break;

            case 8:
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_APIC_TPR);
                STAM_COUNTER_INC(&pVCpu->hm.s.StatExitCR8Write);
                break;

            default:
            {
                AssertMsgFailed(("hmR0SvmExitWriteCRx: Invalid/Unexpected Write-CRx exit. u64ExitCode=%#RX64 %#x\n",
                                 pSvmTransient->u64ExitCode, iCrReg));
                break;
            }
        }
        HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
        rcStrict = VINF_SUCCESS;
    }
    else
        Assert(rcStrict == VERR_EM_INTERPRETER || rcStrict == VINF_PGM_SYNC_CR3);
    return rcStrict;
}


/**
 * \#VMEXIT helper for read MSRs, see hmR0SvmExitMsr.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 */
static VBOXSTRICTRC hmR0SvmExitReadMsr(PVMCPUCC pVCpu, PSVMVMCB pVmcb)
{
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitRdmsr);
    Log4Func(("idMsr=%#RX32\n", pVCpu->cpum.GstCtx.ecx));

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        /** @todo Optimize this: Only retrieve the MSR bits we need here. CPUMAllMsrs.cpp
         *  can ask for what it needs instead of using CPUMCTX_EXTRN_ALL_MSRS. */
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_ALL_MSRS);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedRdmsr(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_ALL_MSRS);
        rcStrict = IEMExecOne(pVCpu);
    }

    AssertMsg(   rcStrict == VINF_SUCCESS
              || rcStrict == VINF_IEM_RAISED_XCPT
              || rcStrict == VINF_CPUM_R3_MSR_READ,
              ("hmR0SvmExitReadMsr: Unexpected status %Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT helper for write MSRs, see hmR0SvmExitMsr.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pVmcb       Pointer to the VM control block.
 * @param   pSvmTransient   Pointer to the SVM-transient structure.
 */
static VBOXSTRICTRC hmR0SvmExitWriteMsr(PVMCPUCC pVCpu, PSVMVMCB pVmcb, PSVMTRANSIENT pSvmTransient)
{
    PCPUMCTX pCtx  = &pVCpu->cpum.GstCtx;
    uint32_t const idMsr = pCtx->ecx;
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitWrmsr);
    Log4Func(("idMsr=%#RX32\n", idMsr));

    /*
     * Handle TPR patching MSR writes.
     * We utilitize the LSTAR MSR for patching.
     */
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (   idMsr == MSR_K8_LSTAR
        && pVCpu->CTX_SUFF(pVM)->hm.s.fTprPatchingActive)
    {
        unsigned cbInstr;
        if (fSupportsNextRipSave)
            cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        else
        {
            PDISSTATE pDis = &pVCpu->hmr0.s.svm.Dis;
            int rc = EMInterpretDisasCurrent(pVCpu, pDis, &cbInstr);
            if (   rc == VINF_SUCCESS
                && pDis->pCurInstr->uOpcode == OP_WRMSR)
                Assert(cbInstr > 0);
            else
                cbInstr = 0;
        }

        /* Our patch code uses LSTAR for TPR caching for 32-bit guests. */
        if ((pCtx->eax & 0xff) != pSvmTransient->u8GuestTpr)
        {
            int rc = APICSetTpr(pVCpu, pCtx->eax & 0xff);
            AssertRCReturn(rc, rc);
            ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_APIC_TPR);
        }

        int rc = VINF_SUCCESS;
        hmR0SvmAdvanceRip(pVCpu, cbInstr);
        HMSVM_CHECK_SINGLE_STEP(pVCpu, rc);
        return rc;
    }

    /*
     * Handle regular MSR writes.
     */
    VBOXSTRICTRC rcStrict;
    if (fSupportsNextRipSave)
    {
        /** @todo Optimize this: We don't need to get much of the MSR state here
         * since we're only updating.  CPUMAllMsrs.cpp can ask for what it needs and
         * clear the applicable extern flags. */
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_ALL_MSRS);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedWrmsr(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_ALL_MSRS);
        rcStrict = IEMExecOne(pVCpu);
    }

    AssertMsg(   rcStrict == VINF_SUCCESS
              || rcStrict == VINF_IEM_RAISED_XCPT
              || rcStrict == VINF_CPUM_R3_MSR_WRITE,
              ("hmR0SvmExitWriteMsr: Unexpected status %Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

    if (rcStrict == VINF_SUCCESS)
    {
        /* If this is an X2APIC WRMSR access, update the APIC TPR state. */
        if (   idMsr >= MSR_IA32_X2APIC_START
            && idMsr <= MSR_IA32_X2APIC_END)
        {
            /*
             * We've already saved the APIC related guest-state (TPR) in hmR0SvmPostRunGuest().
             * When full APIC register virtualization is implemented we'll have to make sure
             * APIC state is saved from the VMCB before IEM changes it.
             */
            ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_APIC_TPR);
        }
        else
        {
            switch (idMsr)
            {
                case MSR_IA32_TSC:          pSvmTransient->fUpdateTscOffsetting = true;                                     break;
                case MSR_K6_EFER:           ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_EFER_MSR);          break;
                case MSR_K8_FS_BASE:        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_FS);                break;
                case MSR_K8_GS_BASE:        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_GS);                break;
                case MSR_IA32_SYSENTER_CS:  ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_SYSENTER_CS_MSR);   break;
                case MSR_IA32_SYSENTER_EIP: ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_SYSENTER_EIP_MSR);  break;
                case MSR_IA32_SYSENTER_ESP: ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_SYSENTER_ESP_MSR);  break;
            }
        }
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for MSR read and writes (SVM_EXIT_MSR). Conditional
 * \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitMsr(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    PSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    if (pVmcb->ctrl.u64ExitInfo1 == SVM_EXIT1_MSR_READ)
        return hmR0SvmExitReadMsr(pVCpu, pVmcb);

    Assert(pVmcb->ctrl.u64ExitInfo1 == SVM_EXIT1_MSR_WRITE);
    return hmR0SvmExitWriteMsr(pVCpu, pVmcb, pSvmTransient);
}


/**
 * \#VMEXIT handler for DRx read (SVM_EXIT_READ_DRx). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitReadDRx(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);

    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitDRxRead);

    /** @todo Stepping with nested-guest. */
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    if (!CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
    {
        /* We should -not- get this #VMEXIT if the guest's debug registers were active. */
        if (pSvmTransient->fWasGuestDebugStateActive)
        {
            AssertMsgFailed(("hmR0SvmExitReadDRx: Unexpected exit %#RX32\n", (uint32_t)pSvmTransient->u64ExitCode));
            pVCpu->hm.s.u32HMError = (uint32_t)pSvmTransient->u64ExitCode;
            return VERR_SVM_UNEXPECTED_EXIT;
        }

        /*
         * Lazy DR0-3 loading.
         */
        if (!pSvmTransient->fWasHyperDebugStateActive)
        {
            Assert(!DBGFIsStepping(pVCpu)); Assert(!pVCpu->hm.s.fSingleInstruction);
            Log5(("hmR0SvmExitReadDRx: Lazy loading guest debug registers\n"));

            /* Don't intercept DRx read and writes. */
            PSVMVMCB pVmcb = pVCpu->hmr0.s.svm.pVmcb;
            pVmcb->ctrl.u16InterceptRdDRx = 0;
            pVmcb->ctrl.u16InterceptWrDRx = 0;
            pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_INTERCEPTS;

            /* We're playing with the host CPU state here, make sure we don't preempt or longjmp. */
            VMMRZCallRing3Disable(pVCpu);
            HM_DISABLE_PREEMPT(pVCpu);

            /* Save the host & load the guest debug state, restart execution of the MOV DRx instruction. */
            CPUMR0LoadGuestDebugState(pVCpu, false /* include DR6 */);
            Assert(CPUMIsGuestDebugStateActive(pVCpu));

            HM_RESTORE_PREEMPT();
            VMMRZCallRing3Enable(pVCpu);

            STAM_COUNTER_INC(&pVCpu->hm.s.StatDRxContextSwitch);
            return VINF_SUCCESS;
        }
    }

    /*
     * Interpret the read/writing of DRx.
     */
    /** @todo Decode assist.  */
    VBOXSTRICTRC rc = EMInterpretInstruction(pVCpu);
    Log5(("hmR0SvmExitReadDRx: Emulated DRx access: rc=%Rrc\n", VBOXSTRICTRC_VAL(rc)));
    if (RT_LIKELY(rc == VINF_SUCCESS))
    {
        /* Not necessary for read accesses but whatever doesn't hurt for now, will be fixed with decode assist. */
        /** @todo CPUM should set this flag! */
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_DR_MASK);
        HMSVM_CHECK_SINGLE_STEP(pVCpu, rc);
    }
    else
        Assert(rc == VERR_EM_INTERPRETER);
    return rc;
}


/**
 * \#VMEXIT handler for DRx write (SVM_EXIT_WRITE_DRx). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitWriteDRx(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    /* For now it's the same since we interpret the instruction anyway. Will change when using of Decode Assist is implemented. */
    VBOXSTRICTRC rc = hmR0SvmExitReadDRx(pVCpu, pSvmTransient);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitDRxWrite);
    STAM_COUNTER_DEC(&pVCpu->hm.s.StatExitDRxRead);
    return rc;
}


/**
 * \#VMEXIT handler for XCRx write (SVM_EXIT_XSETBV). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXsetbv(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);

    /** @todo decode assists... */
    VBOXSTRICTRC rcStrict = IEMExecOne(pVCpu);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
        bool const fLoadSaveGuestXcr0 = (pCtx->cr4 & X86_CR4_OSXSAVE) && pCtx->aXcr[0] != ASMGetXcr0();
        Log4Func(("New XCR0=%#RX64 fLoadSaveGuestXcr0=%RTbool (cr4=%#RX64)\n", pCtx->aXcr[0], fLoadSaveGuestXcr0, pCtx->cr4));
        if (fLoadSaveGuestXcr0 != pVCpu->hmr0.s.fLoadSaveGuestXcr0)
        {
            pVCpu->hmr0.s.fLoadSaveGuestXcr0 = fLoadSaveGuestXcr0;
            hmR0SvmUpdateVmRunFunction(pVCpu);
        }
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for I/O instructions (SVM_EXIT_IOIO). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitIOInstr(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_SREG_MASK);

    /* I/O operation lookup arrays. */
    static uint32_t const s_aIOSize[8]  = { 0, 1, 2, 0, 4, 0, 0, 0 };                   /* Size of the I/O accesses in bytes. */
    static uint32_t const s_aIOOpAnd[8] = { 0, 0xff, 0xffff, 0, 0xffffffff, 0, 0, 0 };  /* AND masks for saving
                                                                                           the result (in AL/AX/EAX). */
    PVMCC    pVM   = pVCpu->CTX_SUFF(pVM);
    PCPUMCTX pCtx  = &pVCpu->cpum.GstCtx;
    PSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);

    Log4Func(("CS:RIP=%04x:%RX64\n", pCtx->cs.Sel, pCtx->rip));

    /* Refer AMD spec. 15.10.2 "IN and OUT Behaviour" and Figure 15-2. "EXITINFO1 for IOIO Intercept" for the format. */
    SVMIOIOEXITINFO IoExitInfo;
    IoExitInfo.u       = (uint32_t)pVmcb->ctrl.u64ExitInfo1;
    uint32_t uIOWidth  = (IoExitInfo.u >> 4) & 0x7;
    uint32_t cbValue   = s_aIOSize[uIOWidth];
    uint32_t uAndVal   = s_aIOOpAnd[uIOWidth];

    if (RT_UNLIKELY(!cbValue))
    {
        AssertMsgFailed(("hmR0SvmExitIOInstr: Invalid IO operation. uIOWidth=%u\n", uIOWidth));
        return VERR_EM_INTERPRETER;
    }

    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS);
    VBOXSTRICTRC rcStrict;
    PCEMEXITREC pExitRec = NULL;
    if (   !pVCpu->hm.s.fSingleInstruction
        && !pVCpu->cpum.GstCtx.eflags.Bits.u1TF)
        pExitRec = EMHistoryUpdateFlagsAndTypeAndPC(pVCpu,
                                                    !IoExitInfo.n.u1Str
                                                    ? IoExitInfo.n.u1Type == SVM_IOIO_READ
                                                    ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_IO_PORT_READ)
                                                    : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_IO_PORT_WRITE)
                                                    : IoExitInfo.n.u1Type == SVM_IOIO_READ
                                                    ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_IO_PORT_STR_READ)
                                                    : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_IO_PORT_STR_WRITE),
                                                    pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base);
    if (!pExitRec)
    {
        bool fUpdateRipAlready = false;
        if (IoExitInfo.n.u1Str)
        {
            /* INS/OUTS - I/O String instruction. */
            /** @todo Huh? why can't we use the segment prefix information given by AMD-V
             *        in EXITINFO1? Investigate once this thing is up and running. */
            Log4Func(("CS:RIP=%04x:%08RX64 %#06x/%u %c str\n", pCtx->cs.Sel, pCtx->rip, IoExitInfo.n.u16Port, cbValue,
                      IoExitInfo.n.u1Type == SVM_IOIO_WRITE ? 'w' : 'r'));
            AssertReturn(pCtx->dx == IoExitInfo.n.u16Port, VERR_SVM_IPE_2);
            static IEMMODE const s_aenmAddrMode[8] =
            {
                (IEMMODE)-1, IEMMODE_16BIT, IEMMODE_32BIT, (IEMMODE)-1, IEMMODE_64BIT, (IEMMODE)-1, (IEMMODE)-1, (IEMMODE)-1
            };
            IEMMODE enmAddrMode = s_aenmAddrMode[(IoExitInfo.u >> 7) & 0x7];
            if (enmAddrMode != (IEMMODE)-1)
            {
                uint64_t cbInstr = pVmcb->ctrl.u64ExitInfo2 - pCtx->rip;
                if (cbInstr <= 15 && cbInstr >= 1)
                {
                    Assert(cbInstr >= 1U + IoExitInfo.n.u1Rep);
                    if (IoExitInfo.n.u1Type == SVM_IOIO_WRITE)
                    {
                        /* Don't know exactly how to detect whether u3Seg is valid, currently
                           only enabling it for Bulldozer and later with NRIP.  OS/2 broke on
                           2384 Opterons when only checking NRIP. */
                        bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
                        if (   fSupportsNextRipSave
                            && pVM->cpum.ro.GuestFeatures.enmMicroarch >= kCpumMicroarch_AMD_15h_First)
                        {
                            AssertMsg(IoExitInfo.n.u3Seg == X86_SREG_DS || cbInstr > 1U + IoExitInfo.n.u1Rep,
                                      ("u32Seg=%d cbInstr=%d u1REP=%d", IoExitInfo.n.u3Seg, cbInstr, IoExitInfo.n.u1Rep));
                            rcStrict = IEMExecStringIoWrite(pVCpu, cbValue, enmAddrMode, IoExitInfo.n.u1Rep, (uint8_t)cbInstr,
                                                            IoExitInfo.n.u3Seg, true /*fIoChecked*/);
                        }
                        else if (cbInstr == 1U + IoExitInfo.n.u1Rep)
                            rcStrict = IEMExecStringIoWrite(pVCpu, cbValue, enmAddrMode, IoExitInfo.n.u1Rep, (uint8_t)cbInstr,
                                                            X86_SREG_DS, true /*fIoChecked*/);
                        else
                            rcStrict = IEMExecOne(pVCpu);
                        STAM_COUNTER_INC(&pVCpu->hm.s.StatExitIOStringWrite);
                    }
                    else
                    {
                        AssertMsg(IoExitInfo.n.u3Seg == X86_SREG_ES /*=0*/, ("%#x\n", IoExitInfo.n.u3Seg));
                        rcStrict = IEMExecStringIoRead(pVCpu, cbValue, enmAddrMode, IoExitInfo.n.u1Rep, (uint8_t)cbInstr,
                                                       true /*fIoChecked*/);
                        STAM_COUNTER_INC(&pVCpu->hm.s.StatExitIOStringRead);
                    }
                }
                else
                {
                    AssertMsgFailed(("rip=%RX64 nrip=%#RX64 cbInstr=%#RX64\n", pCtx->rip, pVmcb->ctrl.u64ExitInfo2, cbInstr));
                    rcStrict = IEMExecOne(pVCpu);
                }
            }
            else
            {
                AssertMsgFailed(("IoExitInfo=%RX64\n", IoExitInfo.u));
                rcStrict = IEMExecOne(pVCpu);
            }
            fUpdateRipAlready = true;
            if (rcStrict == VINF_IEM_RAISED_XCPT)
            {
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
                rcStrict = VINF_SUCCESS;
            }
        }
        else
        {
            /* IN/OUT - I/O instruction. */
            Assert(!IoExitInfo.n.u1Rep);

            uint8_t const cbInstr = pVmcb->ctrl.u64ExitInfo2 - pCtx->rip;
            if (IoExitInfo.n.u1Type == SVM_IOIO_WRITE)
            {
                rcStrict = IOMIOPortWrite(pVM, pVCpu, IoExitInfo.n.u16Port, pCtx->eax & uAndVal, cbValue);
                if (    rcStrict == VINF_IOM_R3_IOPORT_WRITE
                    && !pCtx->eflags.Bits.u1TF)
                    rcStrict = EMRZSetPendingIoPortWrite(pVCpu, IoExitInfo.n.u16Port, cbInstr, cbValue, pCtx->eax & uAndVal);
                STAM_COUNTER_INC(&pVCpu->hm.s.StatExitIOWrite);
            }
            else
            {
                rcStrict = VERR_GCM_NOT_HANDLED;
                if (GCMIsInterceptingIOPortRead(pVCpu, IoExitInfo.n.u16Port, cbValue))
                {
                    rcStrict = GCMInterceptedIOPortRead(pVCpu, pCtx, IoExitInfo.n.u16Port, cbValue);
                    if (rcStrict == VINF_GCM_HANDLED_ADVANCE_RIP || rcStrict == VINF_GCM_HANDLED)
                    {
                        fUpdateRipAlready = rcStrict != VINF_GCM_HANDLED_ADVANCE_RIP;
                        rcStrict = VINF_SUCCESS;
                    }
                    else
                        Assert(rcStrict == VERR_GCM_NOT_HANDLED);
                }

                if (RT_LIKELY(rcStrict == VERR_GCM_NOT_HANDLED))
                {
                    uint32_t u32Val = 0;
                    rcStrict = IOMIOPortRead(pVM, pVCpu, IoExitInfo.n.u16Port, &u32Val, cbValue);
                    if (IOM_SUCCESS(rcStrict))
                    {
                        /* Save result of I/O IN instr. in AL/AX/EAX. */
                        /** @todo r=bird: 32-bit op size should clear high bits of rax! */
                        pCtx->eax = (pCtx->eax & ~uAndVal) | (u32Val & uAndVal);
                    }
                    else if (    rcStrict == VINF_IOM_R3_IOPORT_READ
                             && !pCtx->eflags.Bits.u1TF)
                        rcStrict = EMRZSetPendingIoPortRead(pVCpu, IoExitInfo.n.u16Port, cbInstr, cbValue);
                }

                STAM_COUNTER_INC(&pVCpu->hm.s.StatExitIORead);
            }
        }

        if (IOM_SUCCESS(rcStrict))
        {
            /* AMD-V saves the RIP of the instruction following the IO instruction in EXITINFO2. */
            if (!fUpdateRipAlready)
                pCtx->rip = pVmcb->ctrl.u64ExitInfo2;

            /*
             * If any I/O breakpoints are armed, we need to check if one triggered
             * and take appropriate action.
             * Note that the I/O breakpoint type is undefined if CR4.DE is 0.
             */
            /** @todo Optimize away the DBGFBpIsHwIoArmed call by having DBGF tell the
             *  execution engines about whether hyper BPs and such are pending. */
            HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_DR7);
            uint32_t const uDr7 = pCtx->dr[7];
            if (RT_UNLIKELY(   (   (uDr7 & X86_DR7_ENABLED_MASK)
                                && X86_DR7_ANY_RW_IO(uDr7)
                                && (pCtx->cr4 & X86_CR4_DE))
                            || DBGFBpIsHwIoArmed(pVM)))
            {
                /* We're playing with the host CPU state here, make sure we don't preempt or longjmp. */
                VMMRZCallRing3Disable(pVCpu);
                HM_DISABLE_PREEMPT(pVCpu);

                STAM_COUNTER_INC(&pVCpu->hm.s.StatDRxIoCheck);
                CPUMR0DebugStateMaybeSaveGuest(pVCpu, false /*fDr6*/);

                VBOXSTRICTRC rcStrict2 = DBGFBpCheckIo(pVM, pVCpu, &pVCpu->cpum.GstCtx, IoExitInfo.n.u16Port, cbValue);
                if (rcStrict2 == VINF_EM_RAW_GUEST_TRAP)
                {
                    /* Raise #DB. */
                    pVmcb->guest.u64DR6 = pCtx->dr[6];
                    pVmcb->guest.u64DR7 = pCtx->dr[7];
                    pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_DRX;
                    hmR0SvmSetPendingXcptDB(pVCpu);
                }
                /* rcStrict is VINF_SUCCESS, VINF_IOM_R3_IOPORT_COMMIT_WRITE, or in [VINF_EM_FIRST..VINF_EM_LAST],
                   however we can ditch VINF_IOM_R3_IOPORT_COMMIT_WRITE as it has VMCPU_FF_IOM as backup. */
                else if (   rcStrict2 != VINF_SUCCESS
                         && (rcStrict == VINF_SUCCESS || rcStrict2 < rcStrict))
                    rcStrict = rcStrict2;
                AssertCompile(VINF_EM_LAST < VINF_IOM_R3_IOPORT_COMMIT_WRITE);

                HM_RESTORE_PREEMPT();
                VMMRZCallRing3Enable(pVCpu);
            }

            HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
        }
#ifdef VBOX_STRICT
        if (   rcStrict == VINF_IOM_R3_IOPORT_READ
            || rcStrict == VINF_EM_PENDING_R3_IOPORT_READ)
            Assert(IoExitInfo.n.u1Type == SVM_IOIO_READ);
        else if (   rcStrict == VINF_IOM_R3_IOPORT_WRITE
                 || rcStrict == VINF_IOM_R3_IOPORT_COMMIT_WRITE
                 || rcStrict == VINF_EM_PENDING_R3_IOPORT_WRITE)
            Assert(IoExitInfo.n.u1Type == SVM_IOIO_WRITE);
        else
        {
            /** @todo r=bird: This is missing a bunch of VINF_EM_FIRST..VINF_EM_LAST
             *        statuses, that the VMM device and some others may return. See
             *        IOM_SUCCESS() for guidance. */
            AssertMsg(   RT_FAILURE(rcStrict)
                      || rcStrict == VINF_SUCCESS
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR
                      || rcStrict == VINF_EM_DBG_BREAKPOINT
                      || rcStrict == VINF_EM_RAW_GUEST_TRAP
                      || rcStrict == VINF_EM_DBG_STEPPED
                      || rcStrict == VINF_EM_RAW_TO_R3
                      || rcStrict == VINF_EM_TRIPLE_FAULT, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
        }
#endif
    }
    else
    {
        /*
         * Frequent exit or something needing probing.  Get state and call EMHistoryExec.
         */
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
        STAM_COUNTER_INC(!IoExitInfo.n.u1Str
                         ? IoExitInfo.n.u1Type == SVM_IOIO_WRITE ? &pVCpu->hm.s.StatExitIOWrite : &pVCpu->hm.s.StatExitIORead
                         : IoExitInfo.n.u1Type == SVM_IOIO_WRITE ? &pVCpu->hm.s.StatExitIOStringWrite : &pVCpu->hm.s.StatExitIOStringRead);
        Log4(("IOExit/%u: %04x:%08RX64: %s%s%s %#x LB %u -> EMHistoryExec\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, IoExitInfo.n.u1Rep ? "REP " : "",
              IoExitInfo.n.u1Type == SVM_IOIO_WRITE ? "OUT" : "IN", IoExitInfo.n.u1Str ? "S" : "", IoExitInfo.n.u16Port, uIOWidth));

        rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_ALL_GUEST);

        Log4(("IOExit/%u: %04x:%08RX64: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
              pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
              VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    }
    return rcStrict;
}


/**
 * \#VMEXIT handler for Nested Page-faults (SVM_EXIT_NPF). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitNestedPF(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);

    PVMCC      pVM  = pVCpu->CTX_SUFF(pVM);
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    Assert(pVM->hmr0.s.fNestedPaging);

    /* See AMD spec. 15.25.6 "Nested versus Guest Page Faults, Fault Ordering" for VMCB details for #NPF. */
    PSVMVMCB pVmcb           = hmR0SvmGetCurrentVmcb(pVCpu);
    RTGCPHYS GCPhysFaultAddr = pVmcb->ctrl.u64ExitInfo2;
    uint32_t u32ErrCode      = pVmcb->ctrl.u64ExitInfo1;    /* Note! High bits in EXITINFO1 may contain additional info and are
                                                               thus intentionally not copied into u32ErrCode. */

    Log4Func(("#NPF at CS:RIP=%04x:%RX64 GCPhysFaultAddr=%RGp ErrCode=%#x cbInstrFetched=%u %.15Rhxs\n", pCtx->cs.Sel, pCtx->rip, GCPhysFaultAddr,
              u32ErrCode, pVmcb->ctrl.cbInstrFetched, pVmcb->ctrl.abInstr));

    /*
     * TPR patching for 32-bit guests, using the reserved bit in the page tables for MMIO regions.
     */
    if (   pVM->hm.s.fTprPatchingAllowed
        && (GCPhysFaultAddr & GUEST_PAGE_OFFSET_MASK) == XAPIC_OFF_TPR
        && (   !(u32ErrCode & X86_TRAP_PF_P)                                                             /* Not present */
            || (u32ErrCode & (X86_TRAP_PF_P | X86_TRAP_PF_RSVD)) == (X86_TRAP_PF_P | X86_TRAP_PF_RSVD))  /* MMIO page. */
        && !CPUMIsGuestInSvmNestedHwVirtMode(pCtx)
        && !CPUMIsGuestInLongModeEx(pCtx)
        && !CPUMGetGuestCPL(pVCpu)
        && pVM->hm.s.cPatches < RT_ELEMENTS(pVM->hm.s.aPatches))
    {
        RTGCPHYS GCPhysApicBase = APICGetBaseMsrNoCheck(pVCpu);
        GCPhysApicBase &= ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;

        if (GCPhysFaultAddr == GCPhysApicBase + XAPIC_OFF_TPR)
        {
            /* Only attempt to patch the instruction once. */
            PHMTPRPATCH pPatch = (PHMTPRPATCH)RTAvloU32Get(&pVM->hm.s.PatchTree, (AVLOU32KEY)pCtx->eip);
            if (!pPatch)
                return VINF_EM_HM_PATCH_TPR_INSTR;
        }
    }

    /*
     * Determine the nested paging mode.
     */
/** @todo r=bird: Gotta love this nested paging hacking we're still carrying with us... (Split PGM_TYPE_NESTED.) */
    PGMMODE const enmNestedPagingMode = PGMGetHostMode(pVM);

    /*
     * MMIO optimization using the reserved (RSVD) bit in the guest page tables for MMIO pages.
     */
    Assert((u32ErrCode & (X86_TRAP_PF_RSVD | X86_TRAP_PF_P)) != X86_TRAP_PF_RSVD);
    if ((u32ErrCode & (X86_TRAP_PF_RSVD | X86_TRAP_PF_P)) == (X86_TRAP_PF_RSVD | X86_TRAP_PF_P))
    {
        /*
         * If event delivery causes an MMIO #NPF, go back to instruction emulation as otherwise
         * injecting the original pending event would most likely cause the same MMIO #NPF.
         */
        if (pVCpu->hm.s.Event.fPending)
        {
            STAM_COUNTER_INC(&pVCpu->hm.s.StatInjectInterpret);
            return VINF_EM_RAW_INJECT_TRPM_EVENT;
        }

        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_RIP);
        VBOXSTRICTRC rcStrict;
        PCEMEXITREC pExitRec = EMHistoryUpdateFlagsAndTypeAndPC(pVCpu,
                                                                EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM | EMEXIT_F_HM, EMEXITTYPE_MMIO),
                                                                pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base);
        if (!pExitRec)
        {

            rcStrict = PGMR0Trap0eHandlerNPMisconfig(pVM, pVCpu, enmNestedPagingMode, pCtx, GCPhysFaultAddr, u32ErrCode);

            /*
             * If we succeed, resume guest execution.
             *
             * If we fail in interpreting the instruction because we couldn't get the guest
             * physical address of the page containing the instruction via the guest's page
             * tables (we would invalidate the guest page in the host TLB), resume execution
             * which would cause a guest page fault to let the guest handle this weird case.
             *
             * See @bugref{6043}.
             */
            if (   rcStrict == VINF_SUCCESS
                || rcStrict == VERR_PAGE_TABLE_NOT_PRESENT
                || rcStrict == VERR_PAGE_NOT_PRESENT)
            {
                /* Successfully handled MMIO operation. */
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_APIC_TPR);
                rcStrict = VINF_SUCCESS;
            }
        }
        else
        {
            /*
             * Frequent exit or something needing probing.  Get state and call EMHistoryExec.
             */
            Assert(pCtx == &pVCpu->cpum.GstCtx);
            HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
            Log4(("EptMisscfgExit/%u: %04x:%08RX64: %RGp -> EMHistoryExec\n",
                  pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, GCPhysFaultAddr));

            rcStrict = EMHistoryExec(pVCpu, pExitRec, 0);
            ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_ALL_GUEST);

            Log4(("EptMisscfgExit/%u: %04x:%08RX64: EMHistoryExec -> %Rrc + %04x:%08RX64\n",
                  pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
                  VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
        }
        return rcStrict;
    }

    /*
     * Nested page-fault.
     */
    TRPMAssertXcptPF(pVCpu, GCPhysFaultAddr, u32ErrCode);
    int rc = PGMR0Trap0eHandlerNestedPaging(pVM, pVCpu, enmNestedPagingMode, u32ErrCode, pCtx, GCPhysFaultAddr);
    TRPMResetTrap(pVCpu);

    Log4Func(("#NPF: PGMR0Trap0eHandlerNestedPaging returns %Rrc CS:RIP=%04x:%RX64\n", rc, pCtx->cs.Sel, pCtx->rip));

    /*
     * Same case as PGMR0Trap0eHandlerNPMisconfig(). See comment above, @bugref{6043}.
     */
    if (   rc == VINF_SUCCESS
        || rc == VERR_PAGE_TABLE_NOT_PRESENT
        || rc == VERR_PAGE_NOT_PRESENT)
    {
        /* We've successfully synced our shadow page tables. */
        STAM_COUNTER_INC(&pVCpu->hm.s.StatExitShadowPF);
        rc = VINF_SUCCESS;
    }

    /*
     * If delivering an event causes an #NPF (and not MMIO), we shall resolve the fault and
     * re-inject the original event.
     */
    if (pVCpu->hm.s.Event.fPending)
    {
        STAM_COUNTER_INC(&pVCpu->hm.s.StatInjectReflectNPF);

        /*
         * If the #NPF handler requested emulation of the instruction, ignore it.
         * We need to re-inject the original event so as to not lose it.
         * Reproducible when booting ReactOS 0.4.12 with BTRFS (installed using BootCD,
         * LiveCD is broken for other reasons).
         */
        if (rc == VINF_EM_RAW_EMULATE_INSTR)
            rc = VINF_EM_RAW_INJECT_TRPM_EVENT;
    }

    return rc;
}


/**
 * \#VMEXIT handler for virtual interrupt (SVM_EXIT_VINTR). Conditional
 * \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitVIntr(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_ASSERT_NOT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx);

    /* Indicate that we no longer need to #VMEXIT when the guest is ready to receive NMIs, it is now ready. */
    PSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    hmR0SvmClearIntWindowExiting(pVCpu, pVmcb);

    /* Deliver the pending interrupt via hmR0SvmEvaluatePendingEvent() and resume guest execution. */
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitIntWindow);
    return VINF_SUCCESS;
}


/**
 * \#VMEXIT handler for task switches (SVM_EXIT_TASK_SWITCH). Conditional
 * \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitTaskSwitch(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);

#ifndef HMSVM_ALWAYS_TRAP_TASK_SWITCH
    Assert(!pVCpu->CTX_SUFF(pVM)->hmr0.s.fNestedPaging);
#endif

    /* Check if this task-switch occurred while delivering an event through the guest IDT. */
    if (pVCpu->hm.s.Event.fPending)  /* Can happen with exceptions/NMI. See @bugref{8411}. */
    {
        /*
         * AMD-V provides us with the exception which caused the TS; we collect
         * the information in the call to hmR0SvmCheckExitDueToEventDelivery().
         */
        Log4Func(("TS occurred during event delivery\n"));
        STAM_COUNTER_INC(&pVCpu->hm.s.StatExitTaskSwitch);
        return VINF_EM_RAW_INJECT_TRPM_EVENT;
    }

    /** @todo Emulate task switch someday, currently just going back to ring-3 for
     *        emulation. */
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitTaskSwitch);
    return VERR_EM_INTERPRETER;
}


/**
 * \#VMEXIT handler for VMMCALL (SVM_EXIT_VMMCALL). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitVmmCall(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (pVM->hm.s.fTprPatchingAllowed)
    {
        int rc = hmEmulateSvmMovTpr(pVM, pVCpu);
        if (rc != VERR_NOT_FOUND)
        {
            Log4Func(("hmEmulateSvmMovTpr returns %Rrc\n", rc));
            return rc;
        }
    }

    if (EMAreHypercallInstructionsEnabled(pVCpu))
    {
        unsigned cbInstr;
        if (hmR0SvmSupportsNextRipSave(pVCpu))
        {
            PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
            cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        }
        else
        {
            PDISSTATE pDis = &pVCpu->hmr0.s.svm.Dis;
            int rc = EMInterpretDisasCurrent(pVCpu, pDis, &cbInstr);
            if (   rc == VINF_SUCCESS
                && pDis->pCurInstr->uOpcode == OP_VMMCALL)
                Assert(cbInstr > 0);
            else
                cbInstr = 0;
        }

        VBOXSTRICTRC rcStrict = GIMHypercall(pVCpu, &pVCpu->cpum.GstCtx);
        if (RT_SUCCESS(rcStrict))
        {
            /* Only update the RIP if we're continuing guest execution and not in the case
               of say VINF_GIM_R3_HYPERCALL. */
            if (rcStrict == VINF_SUCCESS)
                hmR0SvmAdvanceRip(pVCpu, cbInstr);

            return VBOXSTRICTRC_VAL(rcStrict);
        }
        else
            Log4Func(("GIMHypercall returns %Rrc -> #UD\n", VBOXSTRICTRC_VAL(rcStrict)));
    }

    hmR0SvmSetPendingXcptUD(pVCpu);
    return VINF_SUCCESS;
}


/**
 * \#VMEXIT handler for VMMCALL (SVM_EXIT_VMMCALL). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitPause(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    unsigned cbInstr;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
        cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
    }
    else
    {
        PDISSTATE pDis = &pVCpu->hmr0.s.svm.Dis;
        int rc = EMInterpretDisasCurrent(pVCpu, pDis, &cbInstr);
        if (   rc == VINF_SUCCESS
            && pDis->pCurInstr->uOpcode == OP_PAUSE)
            Assert(cbInstr > 0);
        else
            cbInstr = 0;
    }

    /** @todo The guest has likely hit a contended spinlock. We might want to
     *        poke a schedule different guest VCPU. */
    hmR0SvmAdvanceRip(pVCpu, cbInstr);
    return VINF_EM_RAW_INTERRUPT;
}


/**
 * \#VMEXIT handler for FERR intercept (SVM_EXIT_FERR_FREEZE). Conditional
 * \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitFerrFreeze(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_CR0);
    Assert(!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_NE));

    Log4Func(("Raising IRQ 13 in response to #FERR\n"));
    return PDMIsaSetIrq(pVCpu->CTX_SUFF(pVM), 13 /* u8Irq */, 1 /* u8Level */, 0 /* uTagSrc */);
}


/**
 * \#VMEXIT handler for IRET (SVM_EXIT_IRET). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitIret(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    /* Indicate that we no longer need to #VMEXIT when the guest is ready to receive NMIs, it is now (almost) ready. */
    PSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    hmR0SvmClearCtrlIntercept(pVCpu, pVmcb, SVM_CTRL_INTERCEPT_IRET);

    /* Emulate the IRET. We have to execute the IRET before an NMI, but must potentially
     * deliver a pending NMI right after. If the IRET faults, an NMI can come before the
     * handler executes. Yes, x86 is ugly.
     */
    return VINF_EM_RAW_EMULATE_INSTR;
}


/**
 * \#VMEXIT handler for page-fault exceptions (SVM_EXIT_XCPT_14).
 * Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXcptPF(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);

    /* See AMD spec. 15.12.15 "#PF (Page Fault)". */
    PVMCC             pVM           = pVCpu->CTX_SUFF(pVM);
    PCPUMCTX        pCtx          = &pVCpu->cpum.GstCtx;
    PSVMVMCB        pVmcb         = hmR0SvmGetCurrentVmcb(pVCpu);
    uint32_t        uErrCode      = pVmcb->ctrl.u64ExitInfo1;
    uint64_t const  uFaultAddress = pVmcb->ctrl.u64ExitInfo2;

#if defined(HMSVM_ALWAYS_TRAP_ALL_XCPTS) || defined(HMSVM_ALWAYS_TRAP_PF)
    if (pVM->hmr0.s.fNestedPaging)
    {
        pVCpu->hm.s.Event.fPending = false;     /* In case it's a contributory or vectoring #PF. */
        if (   !pSvmTransient->fVectoringDoublePF
            || CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
        {
            /* A genuine guest #PF, reflect it to the guest. */
            hmR0SvmSetPendingXcptPF(pVCpu, uErrCode, uFaultAddress);
            Log4Func(("#PF: Guest page fault at %04X:%RGv FaultAddr=%RX64 ErrCode=%#x\n", pCtx->cs.Sel, (RTGCPTR)pCtx->rip,
                      uFaultAddress, uErrCode));
        }
        else
        {
            /* A guest page-fault occurred during delivery of a page-fault. Inject #DF. */
            hmR0SvmSetPendingXcptDF(pVCpu);
            Log4Func(("Pending #DF due to vectoring #PF. NP\n"));
        }
        STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestPF);
        return VINF_SUCCESS;
    }
#endif

    Assert(!pVM->hmr0.s.fNestedPaging);

    /*
     * TPR patching shortcut for APIC TPR reads and writes; only applicable to 32-bit guests.
     */
    if (   pVM->hm.s.fTprPatchingAllowed
        && (uFaultAddress & 0xfff) == XAPIC_OFF_TPR
        && !(uErrCode & X86_TRAP_PF_P)                /* Not present. */
        && !CPUMIsGuestInSvmNestedHwVirtMode(pCtx)
        && !CPUMIsGuestInLongModeEx(pCtx)
        && !CPUMGetGuestCPL(pVCpu)
        && pVM->hm.s.cPatches < RT_ELEMENTS(pVM->hm.s.aPatches))
    {
        RTGCPHYS GCPhysApicBase;
        GCPhysApicBase  = APICGetBaseMsrNoCheck(pVCpu);
        GCPhysApicBase &= ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;

        /* Check if the page at the fault-address is the APIC base. */
        PGMPTWALK Walk;
        int rc2 = PGMGstGetPage(pVCpu, (RTGCPTR)uFaultAddress, &Walk);
        if (   rc2 == VINF_SUCCESS
            && Walk.GCPhys == GCPhysApicBase)
        {
            /* Only attempt to patch the instruction once. */
            PHMTPRPATCH pPatch = (PHMTPRPATCH)RTAvloU32Get(&pVM->hm.s.PatchTree, (AVLOU32KEY)pCtx->eip);
            if (!pPatch)
                return VINF_EM_HM_PATCH_TPR_INSTR;
        }
    }

    Log4Func(("#PF: uFaultAddress=%#RX64 CS:RIP=%#04x:%#RX64 uErrCode %#RX32 cr3=%#RX64\n", uFaultAddress, pCtx->cs.Sel,
              pCtx->rip, uErrCode, pCtx->cr3));

    /*
     * If it's a vectoring #PF, emulate injecting the original event injection as
     * PGMTrap0eHandler() is incapable of differentiating between instruction emulation and
     * event injection that caused a #PF. See @bugref{6607}.
     */
    if (pSvmTransient->fVectoringPF)
    {
        Assert(pVCpu->hm.s.Event.fPending);
        return VINF_EM_RAW_INJECT_TRPM_EVENT;
    }

    TRPMAssertXcptPF(pVCpu, uFaultAddress, uErrCode);
    int rc = PGMTrap0eHandler(pVCpu, uErrCode, pCtx, (RTGCPTR)uFaultAddress);

    Log4Func(("#PF: rc=%Rrc\n", rc));

    if (rc == VINF_SUCCESS)
    {
        /* Successfully synced shadow pages tables or emulated an MMIO instruction. */
        TRPMResetTrap(pVCpu);
        STAM_COUNTER_INC(&pVCpu->hm.s.StatExitShadowPF);
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_ALL_GUEST);
        return rc;
    }

    if (rc == VINF_EM_RAW_GUEST_TRAP)
    {
        pVCpu->hm.s.Event.fPending = false;     /* In case it's a contributory or vectoring #PF. */

        /*
         * If a nested-guest delivers a #PF and that causes a #PF which is -not- a shadow #PF,
         * we should simply forward the #PF to the guest and is up to the nested-hypervisor to
         * determine whether it is a nested-shadow #PF or a #DF, see @bugref{7243#c121}.
         */
        if (  !pSvmTransient->fVectoringDoublePF
            || CPUMIsGuestInSvmNestedHwVirtMode(pCtx))
        {
            /* It's a guest (or nested-guest) page fault and needs to be reflected. */
            uErrCode = TRPMGetErrorCode(pVCpu);        /* The error code might have been changed. */
            TRPMResetTrap(pVCpu);

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
            /* If the nested-guest is intercepting #PFs, cause a #PF #VMEXIT. */
            if (   CPUMIsGuestInSvmNestedHwVirtMode(pCtx)
                && CPUMIsGuestSvmXcptInterceptSet(pVCpu, pCtx, X86_XCPT_PF))
                return IEMExecSvmVmexit(pVCpu, SVM_EXIT_XCPT_PF, uErrCode, uFaultAddress);
#endif

            hmR0SvmSetPendingXcptPF(pVCpu, uErrCode, uFaultAddress);
        }
        else
        {
            /* A guest page-fault occurred during delivery of a page-fault. Inject #DF. */
            TRPMResetTrap(pVCpu);
            hmR0SvmSetPendingXcptDF(pVCpu);
            Log4Func(("#PF: Pending #DF due to vectoring #PF\n"));
        }

        STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestPF);
        return VINF_SUCCESS;
    }

    TRPMResetTrap(pVCpu);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitShadowPFEM);
    return rc;
}



/**
 * \#VMEXIT handler for division overflow exceptions (SVM_EXIT_XCPT_1).
 * Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXcptDE(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_ASSERT_NOT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestDE);

    /* Paranoia; Ensure we cannot be called as a result of event delivery. */
    PSVMVMCB        pVmcb         = hmR0SvmGetCurrentVmcb(pVCpu);
    Assert(!pVmcb->ctrl.ExitIntInfo.n.u1Valid);  NOREF(pVmcb);

    int rc = VERR_SVM_UNEXPECTED_XCPT_EXIT;
    if (pVCpu->hm.s.fGCMTrapXcptDE)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
        rc = GCMXcptDE(pVCpu, &pVCpu->cpum.GstCtx);
        AssertMsg(rc == VINF_SUCCESS /* restart */ || rc == VERR_NOT_FOUND /* deliver exception */, ("rc=%Rrc\n", rc));
    }

    /* If the GCM #DE exception handler didn't succeed or wasn't needed, raise #DE. */
    if (RT_FAILURE(rc))
        hmR0SvmSetPendingXcptDE(pVCpu);

    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestDE);
    return VINF_SUCCESS;
}


/**
 * \#VMEXIT handler for undefined opcode (SVM_EXIT_XCPT_6).
 * Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXcptUD(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_ASSERT_NOT_IN_NESTED_GUEST(&pVCpu->cpum.GstCtx);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestUD);

    /* Paranoia; Ensure we cannot be called as a result of event delivery. */
    PSVMVMCB pVmcb = pVCpu->hmr0.s.svm.pVmcb;
    Assert(!pVmcb->ctrl.ExitIntInfo.n.u1Valid);  NOREF(pVmcb);

    /** @todo if we accumulate more optional stuff here, we ought to combine the
     *        reading of opcode bytes to avoid doing more than once.  */

    VBOXSTRICTRC rcStrict = VERR_SVM_UNEXPECTED_XCPT_EXIT;
    if (pVCpu->hm.s.fGIMTrapXcptUD)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
        uint8_t cbInstr = 0;
        rcStrict = GIMXcptUD(pVCpu, &pVCpu->cpum.GstCtx, NULL /* pDis */, &cbInstr);
        if (rcStrict == VINF_SUCCESS)
        {
            /* #UD #VMEXIT does not have valid NRIP information, manually advance RIP. See @bugref{7270#c170}. */
            hmR0SvmAdvanceRip(pVCpu, cbInstr);
            rcStrict = VINF_SUCCESS;
            HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
        }
        else if (rcStrict == VINF_GIM_HYPERCALL_CONTINUING)
            rcStrict = VINF_SUCCESS;
        else if (rcStrict == VINF_GIM_R3_HYPERCALL)
            rcStrict = VINF_GIM_R3_HYPERCALL;
        else
        {
            Assert(RT_FAILURE(VBOXSTRICTRC_VAL(rcStrict)));
            rcStrict = VERR_SVM_UNEXPECTED_XCPT_EXIT;
        }
    }

    if (pVCpu->hm.s.svm.fEmulateLongModeSysEnterExit)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_SS | CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS
                                        | CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_EFER);
        if (CPUMIsGuestInLongModeEx(&pVCpu->cpum.GstCtx))
        {
            /* Ideally, IEM should just handle all these special #UD situations, but
               we don't quite trust things to behave optimially when doing that.  So,
               for now we'll restrict ourselves to a handful of possible sysenter and
               sysexit encodings that we filter right here. */
            uint8_t abInstr[SVM_CTRL_GUEST_INSTR_BYTES_MAX];
            uint8_t cbInstr = pVmcb->ctrl.cbInstrFetched;
            uint32_t const uCpl = CPUMGetGuestCPL(pVCpu);
            uint8_t const cbMin = uCpl != 0 ? 2 : 1 + 2;
            RTGCPTR const GCPtrInstr = pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base;
            if (cbInstr < cbMin || cbInstr > SVM_CTRL_GUEST_INSTR_BYTES_MAX)
            {
                cbInstr = cbMin;
                int rc2 = PGMPhysSimpleReadGCPtr(pVCpu, abInstr, GCPtrInstr, cbInstr);
                AssertRCStmt(rc2, cbInstr = 0);
            }
            else
                memcpy(abInstr, pVmcb->ctrl.abInstr, cbInstr); /* unlikely */
            if (   cbInstr == 0 /* read error */
                || (cbInstr >= 2 && abInstr[0] == 0x0f && abInstr[1] == 0x34) /* sysenter */
                || (   uCpl == 0
                    && (   (   cbInstr >= 2 && abInstr[0] == 0x0f && abInstr[1] == 0x35) /* sysexit */
                        || (   cbInstr >= 3 && abInstr[1] == 0x0f && abInstr[2] == 0x35  /* rex.w sysexit */
                            && (abInstr[0] & (X86_OP_REX_W | 0xf0)) == X86_OP_REX_W))))
            {
                HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK
                                                | CPUMCTX_EXTRN_SREG_MASK /* without ES+DS+GS the app will #GP later - go figure */);
                Log6(("hmR0SvmExitXcptUD: sysenter/sysexit: %.*Rhxs at %#llx CPL=%u\n", cbInstr, abInstr, GCPtrInstr, uCpl));
                rcStrict = IEMExecOneWithPrefetchedByPC(pVCpu, GCPtrInstr, abInstr, cbInstr);
                Log6(("hmR0SvmExitXcptUD: sysenter/sysexit: rcStrict=%Rrc %04x:%08RX64 %08RX64 %04x:%08RX64\n",
                     VBOXSTRICTRC_VAL(rcStrict), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags.u,
                     pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.rsp));
                STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestUD);
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK); /** @todo Lazy bird. */
                if (rcStrict == VINF_IEM_RAISED_XCPT)
                    rcStrict = VINF_SUCCESS;
                return rcStrict;
            }
            Log6(("hmR0SvmExitXcptUD: not sysenter/sysexit: %.*Rhxs at %#llx CPL=%u\n", cbInstr, abInstr, GCPtrInstr, uCpl));
        }
        else
            Log6(("hmR0SvmExitXcptUD: not in long mode at %04x:%llx\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    }

    /* If the GIM #UD exception handler didn't succeed for some reason or wasn't needed, raise #UD. */
    if (RT_FAILURE(rcStrict))
    {
        hmR0SvmSetPendingXcptUD(pVCpu);
        rcStrict = VINF_SUCCESS;
    }

    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestUD);
    return rcStrict;
}


/**
 * \#VMEXIT handler for math-fault exceptions (SVM_EXIT_XCPT_16).
 * Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXcptMF(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestMF);

    PCPUMCTX pCtx  = &pVCpu->cpum.GstCtx;
    PSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);

    /* Paranoia; Ensure we cannot be called as a result of event delivery. */
    Assert(!pVmcb->ctrl.ExitIntInfo.n.u1Valid); NOREF(pVmcb);

    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestMF);

    if (!(pCtx->cr0 & X86_CR0_NE))
    {
        PDISSTATE pDis = &pVCpu->hmr0.s.svm.Dis;
        unsigned  cbInstr;
        int rc = EMInterpretDisasCurrent(pVCpu, pDis, &cbInstr);
        if (RT_SUCCESS(rc))
        {
            /* Convert a #MF into a FERR -> IRQ 13. See @bugref{6117}. */
            rc = PDMIsaSetIrq(pVCpu->CTX_SUFF(pVM), 13 /* u8Irq */, 1 /* u8Level */, 0 /* uTagSrc */);
            if (RT_SUCCESS(rc))
                hmR0SvmAdvanceRip(pVCpu, cbInstr);
        }
        else
            Log4Func(("EMInterpretDisasCurrent returned %Rrc uOpCode=%#x\n", rc, pDis->pCurInstr->uOpcode));
        return rc;
    }

    hmR0SvmSetPendingXcptMF(pVCpu);
    return VINF_SUCCESS;
}


/**
 * \#VMEXIT handler for debug exceptions (SVM_EXIT_XCPT_1). Conditional
 * \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXcptDB(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestDB);

    if (RT_UNLIKELY(pVCpu->hm.s.Event.fPending))
    {
        STAM_COUNTER_INC(&pVCpu->hm.s.StatInjectInterpret);
        return VINF_EM_RAW_INJECT_TRPM_EVENT;
    }

    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestDB);

    /*
     * This can be a fault-type #DB (instruction breakpoint) or a trap-type #DB (data
     * breakpoint). However, for both cases DR6 and DR7 are updated to what the exception
     * handler expects. See AMD spec. 15.12.2 "#DB (Debug)".
     */
    PVMCC    pVM   = pVCpu->CTX_SUFF(pVM);
    PSVMVMCB pVmcb = pVCpu->hmr0.s.svm.pVmcb;
    int rc = DBGFTrap01Handler(pVM, pVCpu, &pVCpu->cpum.GstCtx, pVmcb->guest.u64DR6, pVCpu->hm.s.fSingleInstruction);
    if (rc == VINF_EM_RAW_GUEST_TRAP)
    {
        Log5(("hmR0SvmExitXcptDB: DR6=%#RX64 -> guest trap\n", pVmcb->guest.u64DR6));
        if (CPUMIsHyperDebugStateActive(pVCpu))
            CPUMSetGuestDR6(pVCpu, CPUMGetGuestDR6(pVCpu) | pVmcb->guest.u64DR6);

        /* Reflect the exception back to the guest. */
        hmR0SvmSetPendingXcptDB(pVCpu);
        rc = VINF_SUCCESS;
    }

    /*
     * Update DR6.
     */
    if (CPUMIsHyperDebugStateActive(pVCpu))
    {
        Log5(("hmR0SvmExitXcptDB: DR6=%#RX64 -> %Rrc\n", pVmcb->guest.u64DR6, rc));
        pVmcb->guest.u64DR6 = X86_DR6_INIT_VAL;
        pVmcb->ctrl.u32VmcbCleanBits &= ~HMSVM_VMCB_CLEAN_DRX;
    }
    else
    {
        AssertMsg(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc));
        Assert(!pVCpu->hm.s.fSingleInstruction && !DBGFIsStepping(pVCpu));
    }

    return rc;
}


/**
 * \#VMEXIT handler for alignment check exceptions (SVM_EXIT_XCPT_17).
 * Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXcptAC(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);
    STAM_REL_COUNTER_INC(&pVCpu->hm.s.StatExitGuestAC);

    SVMEVENT Event;
    Event.u          = 0;
    Event.n.u1Valid  = 1;
    Event.n.u3Type   = SVM_EVENT_EXCEPTION;
    Event.n.u8Vector = X86_XCPT_AC;
    Event.n.u1ErrorCodeValid = 1;
    hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
    return VINF_SUCCESS;
}


/**
 * \#VMEXIT handler for breakpoint exceptions (SVM_EXIT_XCPT_3).
 * Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXcptBP(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestBP);

    VBOXSTRICTRC rc = DBGFTrap03Handler(pVCpu->CTX_SUFF(pVM), pVCpu, &pVCpu->cpum.GstCtx);
    if (rc == VINF_EM_RAW_GUEST_TRAP)
    {
        SVMEVENT Event;
        Event.u          = 0;
        Event.n.u1Valid  = 1;
        Event.n.u3Type   = SVM_EVENT_EXCEPTION;
        Event.n.u8Vector = X86_XCPT_BP;
        hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
        rc = VINF_SUCCESS;
    }

    Assert(rc == VINF_SUCCESS || rc == VINF_EM_DBG_BREAKPOINT);
    return rc;
}


/**
 * Hacks its way around the lovely mesa driver's backdoor accesses.
 *
 * @sa hmR0VmxHandleMesaDrvGp
 */
static int hmR0SvmHandleMesaDrvGp(PVMCPUCC pVCpu, PCPUMCTX pCtx, PCSVMVMCB pVmcb)
{
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_CS  | CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_GPRS_MASK);
    Log(("hmR0SvmHandleMesaDrvGp: at %04x:%08RX64 rcx=%RX64 rbx=%RX64\n",
         pVmcb->guest.CS.u16Sel, pVmcb->guest.u64RIP, pCtx->rcx, pCtx->rbx));
    RT_NOREF(pCtx, pVmcb);

    /* For now we'll just skip the instruction. */
    hmR0SvmAdvanceRip(pVCpu, 1);
    return VINF_SUCCESS;
}


/**
 * Checks if the \#GP'ing instruction is the mesa driver doing it's lovely
 * backdoor logging w/o checking what it is running inside.
 *
 * This recognizes an "IN EAX,DX" instruction executed in flat ring-3, with the
 * backdoor port and magic numbers loaded in registers.
 *
 * @returns true if it is, false if it isn't.
 * @sa      hmR0VmxIsMesaDrvGp
 */
DECLINLINE(bool) hmR0SvmIsMesaDrvGp(PVMCPUCC pVCpu, PCPUMCTX pCtx, PCSVMVMCB pVmcb)
{
    /* Check magic and port. */
    Assert(!(pCtx->fExtrn & (CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_RCX)));
    /*Log8(("hmR0SvmIsMesaDrvGp: rax=%RX64 rdx=%RX64\n", pCtx->fExtrn & CPUMCTX_EXTRN_RAX ? pVmcb->guest.u64RAX : pCtx->rax, pCtx->rdx));*/
    if (pCtx->dx != UINT32_C(0x5658))
        return false;
    if ((pCtx->fExtrn & CPUMCTX_EXTRN_RAX ? pVmcb->guest.u64RAX : pCtx->rax) != UINT32_C(0x564d5868))
        return false;

    /* Check that it is #GP(0). */
    if (pVmcb->ctrl.u64ExitInfo1 != 0)
        return false;

    /* Flat ring-3 CS. */
    /*Log8(("hmR0SvmIsMesaDrvGp: u8CPL=%d base=%RX64\n", pVmcb->guest.u8CPL, pCtx->fExtrn & CPUMCTX_EXTRN_CS ? pVmcb->guest.CS.u64Base : pCtx->cs.u64Base));*/
    if (pVmcb->guest.u8CPL != 3)
        return false;
    if ((pCtx->fExtrn & CPUMCTX_EXTRN_CS ? pVmcb->guest.CS.u64Base : pCtx->cs.u64Base) != 0)
        return false;

    /* 0xed:  IN eAX,dx */
    if (pVmcb->ctrl.cbInstrFetched < 1) /* unlikely, it turns out. */
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, CPUMCTX_EXTRN_CS  | CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_GPRS_MASK
                                        | CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_EFER);
        uint8_t abInstr[1];
        int rc = PGMPhysSimpleReadGCPtr(pVCpu, abInstr, pCtx->rip, sizeof(abInstr));
        /*Log8(("hmR0SvmIsMesaDrvGp: PGMPhysSimpleReadGCPtr -> %Rrc %#x\n", rc, abInstr[0])); */
        if (RT_FAILURE(rc))
            return false;
        if (abInstr[0] != 0xed)
            return false;
    }
    else
    {
        /*Log8(("hmR0SvmIsMesaDrvGp: %#x\n", pVmcb->ctrl.abInstr));*/
        if (pVmcb->ctrl.abInstr[0] != 0xed)
            return false;
    }
    return true;
}


/**
 * \#VMEXIT handler for general protection faults (SVM_EXIT_XCPT_BP).
 * Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXcptGP(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);
    STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestGP);

    PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    Assert(pSvmTransient->u64ExitCode == pVmcb->ctrl.u64ExitCode);

    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    if (   !pVCpu->hm.s.fTrapXcptGpForLovelyMesaDrv
        || !hmR0SvmIsMesaDrvGp(pVCpu, pCtx, pVmcb))
    {
        SVMEVENT Event;
        Event.u                  = 0;
        Event.n.u1Valid          = 1;
        Event.n.u3Type           = SVM_EVENT_EXCEPTION;
        Event.n.u8Vector         = X86_XCPT_GP;
        Event.n.u1ErrorCodeValid = 1;
        Event.n.u32ErrorCode     = (uint32_t)pVmcb->ctrl.u64ExitInfo1;
        hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
        return VINF_SUCCESS;
    }
    return hmR0SvmHandleMesaDrvGp(pVCpu, pCtx, pVmcb);
}


/**
 * \#VMEXIT handler for generic exceptions. Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitXcptGeneric(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);

    PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    uint8_t const  uVector  = pVmcb->ctrl.u64ExitCode - SVM_EXIT_XCPT_0;
    uint32_t const uErrCode = pVmcb->ctrl.u64ExitInfo1;
    Assert(pSvmTransient->u64ExitCode == pVmcb->ctrl.u64ExitCode);
    Assert(uVector <= X86_XCPT_LAST);
    Log4Func(("uVector=%#x uErrCode=%u\n", uVector, uErrCode));

    SVMEVENT Event;
    Event.u          = 0;
    Event.n.u1Valid  = 1;
    Event.n.u3Type   = SVM_EVENT_EXCEPTION;
    Event.n.u8Vector = uVector;
    switch (uVector)
    {
        /* Shouldn't be here for reflecting #PFs (among other things, the fault address isn't passed along). */
        case X86_XCPT_PF:   AssertMsgFailed(("hmR0SvmExitXcptGeneric: Unexpected exception")); return VERR_SVM_IPE_5;
        case X86_XCPT_DF:
        case X86_XCPT_TS:
        case X86_XCPT_NP:
        case X86_XCPT_SS:
        case X86_XCPT_GP:
        case X86_XCPT_AC:
        {
            Event.n.u1ErrorCodeValid = 1;
            Event.n.u32ErrorCode     = uErrCode;
            break;
        }
    }

#ifdef VBOX_WITH_STATISTICS
    switch (uVector)
    {
        case X86_XCPT_DE:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestDE);     break;
        case X86_XCPT_DB:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestDB);     break;
        case X86_XCPT_BP:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestBP);     break;
        case X86_XCPT_OF:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestOF);     break;
        case X86_XCPT_BR:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestBR);     break;
        case X86_XCPT_UD:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestUD);     break;
        case X86_XCPT_NM:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestOF);     break;
        case X86_XCPT_DF:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestDF);     break;
        case X86_XCPT_TS:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestTS);     break;
        case X86_XCPT_NP:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestNP);     break;
        case X86_XCPT_SS:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestSS);     break;
        case X86_XCPT_GP:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestGP);     break;
        case X86_XCPT_PF:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestPF);     break;
        case X86_XCPT_MF:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestMF);     break;
        case X86_XCPT_AC:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestAC);     break;
        case X86_XCPT_XF:   STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestXF);     break;
        default:
            STAM_COUNTER_INC(&pVCpu->hm.s.StatExitGuestXcpUnk);
            break;
    }
#endif

    hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
    return VINF_SUCCESS;
}


/**
 * \#VMEXIT handler for software interrupt (INTn). Conditional \#VMEXIT (debug).
 */
HMSVM_EXIT_DECL hmR0SvmExitSwInt(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);

    PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    SVMEVENT  Event;
    Event.u          = 0;
    Event.n.u1Valid  = 1;
    Event.n.u3Type   = SVM_EVENT_SOFTWARE_INT;
    Event.n.u8Vector = pVmcb->ctrl.u64ExitInfo1 & 0xff;
    Log4Func(("uVector=%#x\n", Event.n.u8Vector));
    hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
    return VINF_SUCCESS;
}


/**
 * Generic exit handler that interprets the current instruction
 *
 * Useful exit that only gets triggered by dtrace and the debugger.  Caller does
 * the exit logging, and this function does the rest.
 */
static VBOXSTRICTRC hmR0SvmExitInterpretInstruction(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient,
                                                    uint64_t fExtraImport, uint64_t fHmChanged)
{
#if 1
    RT_NOREF(pSvmTransient);
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK | fExtraImport);
    VBOXSTRICTRC rcStrict = IEMExecOne(pVCpu);
    if (rcStrict == VINF_SUCCESS)
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, fHmChanged | HM_CHANGED_GUEST_RFLAGS | HM_CHANGED_GUEST_RIP);
    else
    {
        Log4Func(("IEMExecOne -> %Rrc\n", VBOXSTRICTRC_VAL(rcStrict) ));
        if (rcStrict == VINF_IEM_RAISED_XCPT)
        {
            ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK | fHmChanged);
            rcStrict = VINF_SUCCESS;
        }
        else
            ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, fHmChanged);
    }
    return rcStrict;
#else
    RT_NOREF(pVCpu, pSvmTransient, fExtraImport, fHmChanged);
    return VINF_EM_RAW_EMULATE_INSTR;
#endif
}


/**
 * \#VMEXIT handler for STR. Conditional \#VMEXIT (debug).
 */
HMSVM_EXIT_DECL hmR0SvmExitTrRead(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    Log4Func(("%04x:%08RX64\n", pSvmTransient->pVmcb->guest.CS.u16Sel, pSvmTransient->pVmcb->guest.u64RIP));
    return hmR0SvmExitInterpretInstruction(pVCpu, pSvmTransient, CPUMCTX_EXTRN_TR, 0);
}


/**
 * \#VMEXIT handler for LTR. Conditional \#VMEXIT (OS/2 TLB workaround, debug).
 */
HMSVM_EXIT_DECL hmR0SvmExitTrWrite(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    /* Workaround for lack of TLB flushing in OS/2 when returning to protected
       mode after a real mode call (like a BIOS call).  See ticketref:20625
       comment 14. */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (pVM->hm.s.fMissingOS2TlbFlushWorkaround)
    {
        Log4Func(("%04x:%08RX64 TLB flush\n", pSvmTransient->pVmcb->guest.CS.u16Sel, pSvmTransient->pVmcb->guest.u64RIP));
        VMCPU_FF_SET(pVCpu, VMCPU_FF_TLB_FLUSH);
    }
    else
        Log4Func(("%04x:%08RX64\n", pSvmTransient->pVmcb->guest.CS.u16Sel, pSvmTransient->pVmcb->guest.u64RIP));

    return hmR0SvmExitInterpretInstruction(pVCpu, pSvmTransient, CPUMCTX_EXTRN_TR | CPUMCTX_EXTRN_GDTR, HM_CHANGED_GUEST_TR);
}


#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
/**
 * \#VMEXIT handler for CLGI (SVM_EXIT_CLGI). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitClgi(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    Assert(pVmcb);
    Assert(!pVmcb->ctrl.IntCtrl.n.u1VGifEnable);

    VBOXSTRICTRC   rcStrict;
    bool const     fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    uint64_t const fImport = CPUMCTX_EXTRN_HWVIRT;
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | fImport);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedClgi(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK | fImport);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_SUCCESS)
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_HWVIRT);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        rcStrict = VINF_SUCCESS;
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for STGI (SVM_EXIT_STGI). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitStgi(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    /*
     * When VGIF is not used we always intercept STGI instructions. When VGIF is used,
     * we only intercept STGI when events are pending for GIF to become 1.
     */
    PSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    if (pVmcb->ctrl.IntCtrl.n.u1VGifEnable)
        hmR0SvmClearCtrlIntercept(pVCpu, pVmcb, SVM_CTRL_INTERCEPT_STGI);

    VBOXSTRICTRC   rcStrict;
    bool const     fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    uint64_t const fImport = CPUMCTX_EXTRN_HWVIRT;
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | fImport);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedStgi(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK | fImport);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_SUCCESS)
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_HWVIRT);
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for VMLOAD (SVM_EXIT_VMLOAD). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitVmload(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    Assert(pVmcb);
    Assert(!pVmcb->ctrl.LbrVirt.n.u1VirtVmsaveVmload);

    VBOXSTRICTRC   rcStrict;
    bool const     fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    uint64_t const fImport = CPUMCTX_EXTRN_FS   | CPUMCTX_EXTRN_GS   | CPUMCTX_EXTRN_KERNEL_GS_BASE
                           | CPUMCTX_EXTRN_TR   | CPUMCTX_EXTRN_LDTR | CPUMCTX_EXTRN_SYSCALL_MSRS
                           | CPUMCTX_EXTRN_SYSENTER_MSRS;
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | fImport);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedVmload(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK | fImport);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_SUCCESS)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_FS              | HM_CHANGED_GUEST_GS
                                                 | HM_CHANGED_GUEST_TR              | HM_CHANGED_GUEST_LDTR
                                                 | HM_CHANGED_GUEST_KERNEL_GS_BASE  | HM_CHANGED_GUEST_SYSCALL_MSRS
                                                 | HM_CHANGED_GUEST_SYSENTER_MSR_MASK);
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for VMSAVE (SVM_EXIT_VMSAVE). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitVmsave(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    PCSVMVMCB pVmcb = hmR0SvmGetCurrentVmcb(pVCpu);
    Assert(!pVmcb->ctrl.LbrVirt.n.u1VirtVmsaveVmload);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedVmsave(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for INVLPGA (SVM_EXIT_INVLPGA). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitInvlpga(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    if (fSupportsNextRipSave)
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK);
        PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedInvlpga(pVCpu, cbInstr);
    }
    else
    {
        HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
    }

    if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * \#VMEXIT handler for STGI (SVM_EXIT_VMRUN). Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmExitVmrun(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    /* We shall import the entire state here, just in case we enter and continue execution of
       the nested-guest with hardware-assisted SVM in ring-0, we would be switching VMCBs and
       could lose lose part of CPU state. */
    HMSVM_CPUMCTX_IMPORT_STATE(pVCpu, HMSVM_CPUMCTX_EXTRN_ALL);

    VBOXSTRICTRC rcStrict;
    bool const fSupportsNextRipSave = hmR0SvmSupportsNextRipSave(pVCpu);
    STAM_PROFILE_ADV_START(&pVCpu->hm.s.StatExitVmentry, z);
    if (fSupportsNextRipSave)
    {
        PCSVMVMCB     pVmcb   = hmR0SvmGetCurrentVmcb(pVCpu);
        uint8_t const cbInstr = pVmcb->ctrl.u64NextRIP - pVCpu->cpum.GstCtx.rip;
        rcStrict = IEMExecDecodedVmrun(pVCpu, cbInstr);
    }
    else
    {
        /* We use IEMExecOneBypassEx() here as it suppresses attempt to continue emulating any
           instruction(s) when interrupt inhibition is set as part of emulating the VMRUN
           instruction itself, see @bugref{7243#c126} */
        rcStrict = IEMExecOneBypassEx(pVCpu, NULL /* pcbWritten */);
    }
    STAM_PROFILE_ADV_STOP(&pVCpu->hm.s.StatExitVmentry, z);

    if (rcStrict == VINF_SUCCESS)
    {
        rcStrict = VINF_SVM_VMRUN;
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_SVM_VMRUN_MASK);
    }
    else if (rcStrict == VINF_IEM_RAISED_XCPT)
    {
        ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_RAISED_XCPT_MASK);
        rcStrict = VINF_SUCCESS;
    }
    HMSVM_CHECK_SINGLE_STEP(pVCpu, rcStrict);
    return rcStrict;
}


/**
 * Nested-guest \#VMEXIT handler for debug exceptions (SVM_EXIT_XCPT_1).
 * Unconditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmNestedExitXcptDB(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);

    if (pVCpu->hm.s.Event.fPending)
    {
        STAM_COUNTER_INC(&pVCpu->hm.s.StatInjectInterpret);
        return VINF_EM_RAW_INJECT_TRPM_EVENT;
    }

    hmR0SvmSetPendingXcptDB(pVCpu);
    return VINF_SUCCESS;
}


/**
 * Nested-guest \#VMEXIT handler for breakpoint exceptions (SVM_EXIT_XCPT_3).
 * Conditional \#VMEXIT.
 */
HMSVM_EXIT_DECL hmR0SvmNestedExitXcptBP(PVMCPUCC pVCpu, PSVMTRANSIENT pSvmTransient)
{
    HMSVM_VALIDATE_EXIT_HANDLER_PARAMS(pVCpu, pSvmTransient);
    HMSVM_CHECK_EXIT_DUE_TO_EVENT_DELIVERY(pVCpu, pSvmTransient);

    SVMEVENT Event;
    Event.u          = 0;
    Event.n.u1Valid  = 1;
    Event.n.u3Type   = SVM_EVENT_EXCEPTION;
    Event.n.u8Vector = X86_XCPT_BP;
    hmR0SvmSetPendingEvent(pVCpu, &Event, 0 /* GCPtrFaultAddress */);
    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_NESTED_HWVIRT_SVM */

/** @} */

