/** @file
 * VM - The Virtual Machine, data.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_vmm_vm_h
#define VBOX_INCLUDED_vmm_vm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_FOR_DTRACE_LIB
# ifndef USING_VMM_COMMON_DEFS
#  error "Compile job does not include VMM_COMMON_DEFS from src/VBox/VMM/Config.kmk - make sure you really need to include this file!"
# endif
# include <iprt/param.h>
# include <VBox/param.h>
# include <VBox/types.h>
# include <VBox/vmm/cpum.h>
# include <VBox/vmm/stam.h>
# include <VBox/vmm/vmapi.h>
# include <VBox/vmm/vmm.h>
# include <VBox/param.h>
# include <VBox/sup.h>
#else
# pragma D depends_on library vbox-types.d
# pragma D depends_on library CPUMInternal.d
# define VMM_INCLUDED_SRC_include_CPUMInternal_h
# define VBOX_VMM_TARGET_AGNOSTIC
#endif

#if !defined(VBOX_VMM_TARGET_AGNOSTIC) \
 && !defined(VBOX_VMM_TARGET_X86) \
 && !defined(VBOX_VMM_TARGET_ARMV8)
# error "VMM target not defined"
#endif



/** @defgroup grp_vm    The Virtual Machine
 * @ingroup grp_vmm
 * @{
 */

/**
 * The state of a Virtual CPU.
 *
 * The basic state indicated here is whether the CPU has been started or not. In
 * addition, there are sub-states when started for assisting scheduling (GVMM
 * mostly).
 *
 * The transition out of the STOPPED state is done by a vmR3PowerOn.
 * The transition back to the STOPPED state is done by vmR3PowerOff.
 *
 * (Alternatively we could let vmR3PowerOn start CPU 0 only and let the SPIP
 * handling switch on the other CPUs. Then vmR3Reset would stop all but CPU 0.)
 */
typedef enum VMCPUSTATE
{
    /** The customary invalid zero. */
    VMCPUSTATE_INVALID = 0,

    /** Virtual CPU has not yet been started.  */
    VMCPUSTATE_STOPPED,

    /** CPU started. */
    VMCPUSTATE_STARTED,
    /** CPU started in HM context. */
    VMCPUSTATE_STARTED_HM,
    /** Executing guest code and can be poked (RC or STI bits of HM). */
    VMCPUSTATE_STARTED_EXEC,
    /** Executing guest code using NEM. */
    VMCPUSTATE_STARTED_EXEC_NEM,
    VMCPUSTATE_STARTED_EXEC_NEM_WAIT,
    VMCPUSTATE_STARTED_EXEC_NEM_CANCELED,
    /** Halted. */
    VMCPUSTATE_STARTED_HALTED,

    /** The end of valid virtual CPU states. */
    VMCPUSTATE_END,

    /** Ensure 32-bit type. */
    VMCPUSTATE_32BIT_HACK = 0x7fffffff
} VMCPUSTATE;

/** Enables 64-bit FFs. */
#define VMCPU_WITH_64_BIT_FFS


/**
 * The cross context virtual CPU structure.
 *
 * Run 'kmk run-struct-tests' (from src/VBox/VMM if you like) after updating!
 */
typedef struct VMCPU
{
    /** @name Volatile per-cpu data.
     * @{ */
    /** Per CPU forced action.
     * See the VMCPU_FF_* \#defines. Updated atomically. */
#ifdef VMCPU_WITH_64_BIT_FFS
    uint64_t volatile       fLocalForcedActions;
#else
    uint32_t volatile       fLocalForcedActions;
    uint32_t                fForLocalForcedActionsExpansion;
#endif
    /** The CPU state. */
    VMCPUSTATE volatile     enmState;

#ifdef VBOX_VMM_TARGET_ARMV8
    uint32_t                u32Alignment0;
    /** The number of nano seconds when the vTimer of the associated vCPU is supposed to activate
     *  required to get out of a halt (due to wfi/wfe).
     *
     * @note This actually should go into TMCPU but this drags in a whole lot of padding changes
     *       and I'm not sure yet whether this will remain in this form anyway.
     */
    uint64_t                cNsVTimerActivate;
    /** Padding up to 64 bytes. */
    uint8_t                 abAlignment0[64 - 12 - 8 - 4];
#else
    /** Padding up to 64 bytes. */
    uint8_t                 abAlignment0[64 - 12];
#endif
    /** @} */

    /** IEM part.
     * @remarks This comes first as it allows the use of 8-bit immediates for the
     *          first 64 bytes of the structure, reducing code size a wee bit. */
#if defined(VMM_INCLUDED_SRC_include_IEMInternal_h) || defined(VMM_INCLUDED_SRC_include_IEMInternal_armv8_h) /* For PDB hacking. */
    union VMCPUUNIONIEMFULL
#else
    union VMCPUUNIONIEMSTUB
#endif
    {
#if defined(VMM_INCLUDED_SRC_include_IEMInternal_h) || defined(VMM_INCLUDED_SRC_include_IEMInternal_armv8_h)
        struct IEMCPU       s;
#endif
        uint8_t             padding[  129984    /* The common base size. */
#ifdef RT_ARCH_AMD64
                                    + 32768     /* For 256 entries per TLBs. */
#else
                                    + 1048576   /* For 8192 entries per TLBs. */
#endif
        ]; /* multiple of 64 */
    } iem;

    /** @name Static per-cpu data.
     * (Putting this after IEM, hoping that it's less frequently used than it.)
     * @{ */
    /** Ring-3 Host Context VM Pointer. */
    PVMR3                   pVMR3;
    /** Ring-0 Host Context VM Pointer, currently used by VTG/dtrace. */
    RTR0PTR                 pVCpuR0ForVtg;
    /** Raw-mode Context VM Pointer. */
    uint32_t                pVMRC;
    /** Padding for new raw-mode (long mode).   */
    uint32_t                pVMRCPadding;
    /** Pointer to the ring-3 UVMCPU structure. */
    PUVMCPU                 pUVCpu;
    /** The native thread handle. */
    RTNATIVETHREAD          hNativeThread;
    /** The native R0 thread handle. (different from the R3 handle!) */
    RTNATIVETHREAD          hNativeThreadR0;
    /** The IPRT thread handle (for VMMDevTesting). */
    RTTHREAD                hThread;
    /** The CPU ID.
     * This is the index into the VM::aCpu array. */
#ifdef IN_RING0
    VMCPUID                 idCpuUnsafe;
#else
    VMCPUID                 idCpu;
#endif
    /** The VM target platform architecture.
     * Same as VM::enmTarget, GVM::enmTarget and GVMCPU::enmTarget. */
#ifdef IN_RING0
    VMTARGET                enmTargetUnsafe;
#else
    VMTARGET                enmTarget;
#endif
    /** @} */

    /** HM part. */
    union VMCPUUNIONHM
    {
#ifdef VMM_INCLUDED_SRC_include_HMInternal_h
        struct HMCPU    s;
#endif
        uint8_t             padding[9984];      /* multiple of 64 */
    } hm;

    /** NEM part. */
    union VMCPUUNIONNEM
    {
#ifdef VMM_INCLUDED_SRC_include_NEMInternal_h
        struct NEMCPU       s;
#endif
        uint8_t             padding[4608];      /* multiple of 64 */
    } nem;

    /** TRPM part. */
    union VMCPUUNIONTRPM
    {
#ifdef VMM_INCLUDED_SRC_include_TRPMInternal_h
        struct TRPMCPU      s;
#endif
        uint8_t             padding[128];       /* multiple of 64 */
    } trpm;

    /** TM part. */
    union VMCPUUNIONTM
    {
#ifdef VMM_INCLUDED_SRC_include_TMInternal_h
        struct TMCPU        s;
#endif
        uint8_t             padding[5760];      /* multiple of 64 */
    } tm;

    /** VMM part. */
    union VMCPUUNIONVMM
    {
#ifdef VMM_INCLUDED_SRC_include_VMMInternal_h
        struct VMMCPU       s;
#endif
        uint8_t             padding[9536];       /* multiple of 64 */
    } vmm;

    /** PDM part. */
    union VMCPUUNIONPDM
    {
#ifdef VMM_INCLUDED_SRC_include_PDMInternal_h
        struct PDMCPU       s;
#endif
        uint8_t             padding[256];       /* multiple of 64 */
    } pdm;

    /** IOM part. */
    union VMCPUUNIONIOM
    {
#ifdef VMM_INCLUDED_SRC_include_IOMInternal_h
        struct IOMCPU       s;
#endif
        uint8_t             padding[512];       /* multiple of 64 */
    } iom;

    /** DBGF part.
     * @todo Combine this with other tiny structures. */
    union VMCPUUNIONDBGF
    {
#ifdef VMM_INCLUDED_SRC_include_DBGFInternal_h
        struct DBGFCPU      s;
#endif
        uint8_t             padding[512];       /* multiple of 64 */
    } dbgf;

    /** GIM part. */
    union VMCPUUNIONGIM
    {
#ifdef VMM_INCLUDED_SRC_include_GIMInternal_h
        struct GIMCPU s;
#endif
        uint8_t             padding[512];       /* multiple of 64 */
    } gim;

    /* Interrupt controller, target specific. */
    RT_GCC_EXTENSION
    union
    {
#if defined(VBOX_VMM_TARGET_ARMV8) || defined(VBOX_VMM_TARGET_AGNOSTIC)
        /** GIC part. */
        union
        {
# ifdef VMM_INCLUDED_SRC_include_GICInternal_h
            struct GICCPU       s;
# endif
            uint8_t             padding[3840];      /* multiple of 64 */
        } gic;
#endif
#if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
        /** APIC part. */
        union
        {
# if defined(VMM_INCLUDED_SRC_include_APICInternal_h)
            struct APICCPU      s;
# elif defined(VMM_INCLUDED_SRC_include_APICHvInternal_h)
            struct HVAPICCPU    s;
# elif defined(VMM_INCLUDED_SRC_include_APICKvmInternal_h)
            struct KVMAPICCPU   s;
# endif
            uint8_t             padding[3840];      /* multiple of 64 */
        } apic;
#endif
    };

    /*
     * Some less frequently used global members that doesn't need to take up
     * precious space at the head of the structure.
     */

    /** Trace groups enable flags.  */
    uint32_t                fTraceGroups;                           /* 64 / 44 */
    /** Number of collisions hashing the ring-0 EMT handle. */
    uint8_t                 cEmtHashCollisions;
    uint8_t                 abAdHoc[3];
    /** Profiling samples for use by ad hoc profiling. */
    STAMPROFILEADV          aStatAdHoc[8];                          /* size: 40*8 = 320 */

    /** Align the following members on page boundary. */
    uint8_t                 abAlignment2[1848];

    /** PGM part. */
    union VMCPUUNIONPGM
    {
#ifdef VMM_INCLUDED_SRC_include_PGMInternal_h
        struct PGMCPU       s;
#endif
        uint8_t             padding[36864];     /* multiple of 4096 */
    } pgm;

    /** CPUM part. */
    union VMCPUUNIONCPUM
    {
#if defined(VMM_INCLUDED_SRC_include_CPUMInternal_h) || defined(VMM_INCLUDED_SRC_include_CPUMInternal_armv8_h)
        struct CPUMCPU      s;
#endif
#ifdef VMCPU_INCL_CPUM_GST_CTX
        /** The guest CPUM context for direct use by execution engines.
         * This is not for general consumption, but for HM, REM, IEM, and maybe a few
         * others.  The rest will use the function based CPUM API. */
        CPUMCTX             GstCtx;
#endif
        uint8_t             padding[102400];    /* multiple of 4096 */
    } cpum;

    /** EM part. */
    union VMCPUUNIONEM
    {
#ifdef VMM_INCLUDED_SRC_include_EMInternal_h
        struct EMCPU        s;
#endif
        uint8_t             padding[41024];     /* multiple of 4096 */
    } em;
    uint8_t abPadding[12224];
} VMCPU;


#ifndef VBOX_FOR_DTRACE_LIB
# ifndef IN_TSTVMSTRUCT
/* Make sure the structure size is aligned on a 16384 boundary for arm64 purposes. */
AssertCompileSizeAlignment(VMCPU, 16384);
# endif

/** @name Operations on VMCPU::enmState
 * @{ */
/** Gets the VMCPU state. */
#define VMCPU_GET_STATE(pVCpu)              ( (pVCpu)->enmState )
/** Sets the VMCPU state. */
#define VMCPU_SET_STATE(pVCpu, enmNewState) \
    ASMAtomicWriteU32((uint32_t volatile *)&(pVCpu)->enmState, (enmNewState))
/** Cmpares and sets the VMCPU state. */
#define VMCPU_CMPXCHG_STATE(pVCpu, enmNewState, enmOldState) \
    ASMAtomicCmpXchgU32((uint32_t volatile *)&(pVCpu)->enmState, (enmNewState), (enmOldState))
/** Checks the VMCPU state. */
#ifdef VBOX_STRICT
# define VMCPU_ASSERT_STATE(pVCpu, enmExpectedState) \
    do { \
        VMCPUSTATE enmState = VMCPU_GET_STATE(pVCpu); \
        AssertMsg(enmState == (enmExpectedState), \
                  ("enmState=%d  enmExpectedState=%d idCpu=%u\n", \
                  enmState, enmExpectedState, (pVCpu)->idCpu)); \
    } while (0)

# define VMCPU_ASSERT_STATE_2(pVCpu, enmExpectedState, a_enmExpectedState2) \
    do { \
        VMCPUSTATE enmState = VMCPU_GET_STATE(pVCpu); \
        AssertMsg(   enmState == (enmExpectedState) \
                  || enmState == (a_enmExpectedState2), \
                  ("enmState=%d  enmExpectedState=%d enmExpectedState2=%d idCpu=%u\n", \
                  enmState, enmExpectedState, a_enmExpectedState2, (pVCpu)->idCpu)); \
    } while (0)
#else
# define VMCPU_ASSERT_STATE(pVCpu, enmExpectedState) do { } while (0)
# define VMCPU_ASSERT_STATE_2(pVCpu, enmExpectedState, a_enmExpectedState2) do { } while (0)
#endif
/** Tests if the state means that the CPU is started. */
#define VMCPUSTATE_IS_STARTED(enmState)     ( (enmState) > VMCPUSTATE_STOPPED )
/** Tests if the state means that the CPU is stopped. */
#define VMCPUSTATE_IS_STOPPED(enmState)     ( (enmState) == VMCPUSTATE_STOPPED )
/** @} */


/** The name of the raw-mode context VMM Core module. */
#define VMMRC_MAIN_MODULE_NAME          "VMMRC.rc"
/** The name of the ring-0 context VMM Core module. */
#define VMMR0_MAIN_MODULE_NAME          "VMMR0.r0"


/** VM Forced Action Flags.
 *
 * Use the VM_FF_SET() and VM_FF_CLEAR() macros to change the force
 * action mask of a VM.
 *
 * Available VM bits:
 *      5, 6, 7, 13, 14, 15, 16, 17, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30
 *
 *
 * Available VMCPU bits:
 *      14, 15, 36 to 63
 *
 * @todo If we run low on VMCPU, we may consider merging the SELM bits
 *
 * @{
 */
/* Bit 0, bit 1: Reserved and must not be reused.  The recompiler ASSUMES it
   can OR the local and global FFs together and keept the two
   VMCPU_FF_INTERRUPT_XXX flags uncorrupted.  */
/** The virtual sync clock has been stopped, go to TM until it has been
 *  restarted... */
#define VM_FF_TM_VIRTUAL_SYNC               RT_BIT_32(VM_FF_TM_VIRTUAL_SYNC_BIT)
#define VM_FF_TM_VIRTUAL_SYNC_BIT           2
/** PDM Queues are pending. */
#define VM_FF_PDM_QUEUES                    RT_BIT_32(VM_FF_PDM_QUEUES_BIT)
/** The bit number for VM_FF_PDM_QUEUES. */
#define VM_FF_PDM_QUEUES_BIT                3
/** PDM DMA transfers are pending. */
#define VM_FF_PDM_DMA                       RT_BIT_32(VM_FF_PDM_DMA_BIT)
/** The bit number for VM_FF_PDM_DMA. */
#define VM_FF_PDM_DMA_BIT                   4
/** This action forces the VM to call DBGF so DBGF can service debugger
 * requests in the emulation thread.
 * This action flag stays asserted till DBGF clears it.*/
#define VM_FF_DBGF                          RT_BIT_32(VM_FF_DBGF_BIT)
/** The bit number for VM_FF_DBGF. */
#define VM_FF_DBGF_BIT                      8
/** This action forces the VM to service pending requests from other
 * thread or requests which must be executed in another context. */
#define VM_FF_REQUEST                       RT_BIT_32(VM_FF_REQUEST_BIT)
#define VM_FF_REQUEST_BIT                   9
/** Check for VM state changes and take appropriate action. */
#define VM_FF_CHECK_VM_STATE                RT_BIT_32(VM_FF_CHECK_VM_STATE_BIT)
/** The bit number for VM_FF_CHECK_VM_STATE. */
#define VM_FF_CHECK_VM_STATE_BIT            10
/** Reset the VM. (postponed) */
#define VM_FF_RESET                         RT_BIT_32(VM_FF_RESET_BIT)
/** The bit number for VM_FF_RESET. */
#define VM_FF_RESET_BIT                     11
/** EMT rendezvous in VMM. */
#define VM_FF_EMT_RENDEZVOUS                RT_BIT_32(VM_FF_EMT_RENDEZVOUS_BIT)
/** The bit number for VM_FF_EMT_RENDEZVOUS. */
#define VM_FF_EMT_RENDEZVOUS_BIT            12

/** PGM needs to allocate handy pages. */
#define VM_FF_PGM_NEED_HANDY_PAGES          RT_BIT_32(VM_FF_PGM_NEED_HANDY_PAGES_BIT)
#define VM_FF_PGM_NEED_HANDY_PAGES_BIT      18
/** PGM is out of memory.
 * Abandon all loops and code paths which can be resumed and get up to the EM
 * loops. */
#define VM_FF_PGM_NO_MEMORY                 RT_BIT_32(VM_FF_PGM_NO_MEMORY_BIT)
#define VM_FF_PGM_NO_MEMORY_BIT             19
 /** PGM is about to perform a lightweight pool flush
  *  Guest SMP: all EMT threads should return to ring 3
  */
#define VM_FF_PGM_POOL_FLUSH_PENDING        RT_BIT_32(VM_FF_PGM_POOL_FLUSH_PENDING_BIT)
#define VM_FF_PGM_POOL_FLUSH_PENDING_BIT    20
/** Suspend the VM - debug only. */
#define VM_FF_DEBUG_SUSPEND                 RT_BIT_32(VM_FF_DEBUG_SUSPEND_BIT)
#define VM_FF_DEBUG_SUSPEND_BIT             31


#if defined(VBOX_VMM_TARGET_ARMV8) || defined(VBOX_VMM_TARGET_AGNOSTIC)
/** This action forces the VM to inject an IRQ into the guest. */
# define VMCPU_FF_INTERRUPT_IRQ             RT_BIT_64(VMCPU_FF_INTERRUPT_IRQ_BIT)
# define VMCPU_FF_INTERRUPT_IRQ_BIT         0
/** This action forces the VM to inject an FIQ into the guest. */
# define VMCPU_FF_INTERRUPT_FIQ             RT_BIT_64(VMCPU_FF_INTERRUPT_FIQ_BIT)
# define VMCPU_FF_INTERRUPT_FIQ_BIT         1
#endif
#if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
/** This action forces the VM to check any pending interrupts on the APIC. */
# define VMCPU_FF_INTERRUPT_APIC            RT_BIT_64(VMCPU_FF_INTERRUPT_APIC_BIT)
# define VMCPU_FF_INTERRUPT_APIC_BIT        0
/** This action forces the VM to check any pending interrups on the PIC. */
# define VMCPU_FF_INTERRUPT_PIC             RT_BIT_64(VMCPU_FF_INTERRUPT_PIC_BIT)
# define VMCPU_FF_INTERRUPT_PIC_BIT         1
#endif
/** This action forces the VM to schedule and run pending timer (TM).
 * @remarks Don't move - PATM compatibility.  */
#define VMCPU_FF_TIMER                      RT_BIT_64(VMCPU_FF_TIMER_BIT)
#define VMCPU_FF_TIMER_BIT                  2
/** This action forces the VM to check any pending NMIs. */
#define VMCPU_FF_INTERRUPT_NMI              RT_BIT_64(VMCPU_FF_INTERRUPT_NMI_BIT)
#define VMCPU_FF_INTERRUPT_NMI_BIT          3
/** This action forces the VM to check any pending SMIs. */
#define VMCPU_FF_INTERRUPT_SMI              RT_BIT_64(VMCPU_FF_INTERRUPT_SMI_BIT)
#define VMCPU_FF_INTERRUPT_SMI_BIT          4
/** PDM critical section unlocking is pending, process promptly upon return to R3. */
#define VMCPU_FF_PDM_CRITSECT               RT_BIT_64(VMCPU_FF_PDM_CRITSECT_BIT)
#define VMCPU_FF_PDM_CRITSECT_BIT           5
/** Special EM internal force flag that is used by EMUnhaltAndWakeUp() to force
 * the virtual CPU out of the next (/current) halted state.  It is not processed
 * nor cleared by emR3ForcedActions (similar to VMCPU_FF_BLOCK_NMIS), instead it
 * is cleared the next time EM leaves the HALTED state. */
#define VMCPU_FF_UNHALT                     RT_BIT_64(VMCPU_FF_UNHALT_BIT)
#define VMCPU_FF_UNHALT_BIT                 6
/** Pending IEM action (mask). */
#define VMCPU_FF_IEM                        RT_BIT_64(VMCPU_FF_IEM_BIT)
/** Pending IEM action (bit number). */
#define VMCPU_FF_IEM_BIT                    7
/** Pending APIC action (bit number). */
#define VMCPU_FF_UPDATE_APIC_BIT            8
/** This action forces the VM to update APIC's asynchronously arrived
 *  interrupts as pending interrupts. */
#define VMCPU_FF_UPDATE_APIC                RT_BIT_64(VMCPU_FF_UPDATE_APIC_BIT)
/** This action forces the VM to service pending requests from other
 * thread or requests which must be executed in another context. */
#define VMCPU_FF_REQUEST                    RT_BIT_64(VMCPU_FF_REQUEST_BIT)
#define VMCPU_FF_REQUEST_BIT                9
/** Pending DBGF event (alternative to passing VINF_EM_DBG_EVENT around).  */
#define VMCPU_FF_DBGF                       RT_BIT_64(VMCPU_FF_DBGF_BIT)
/** The bit number for VMCPU_FF_DBGF. */
#define VMCPU_FF_DBGF_BIT                   10
/** Hardware virtualized nested-guest interrupt pending. */
#define VMCPU_FF_INTERRUPT_NESTED_GUEST     RT_BIT_64(VMCPU_FF_INTERRUPT_NESTED_GUEST_BIT)
#define VMCPU_FF_INTERRUPT_NESTED_GUEST_BIT 11
/** This action forces PGM to update changes to CR3 when the guest was in HM mode
 *  (when using nested paging). */
#define VMCPU_FF_HM_UPDATE_CR3              RT_BIT_64(VMCPU_FF_HM_UPDATE_CR3_BIT)
#define VMCPU_FF_HM_UPDATE_CR3_BIT          12
#if defined(VBOX_VMM_TARGET_ARMV8) || defined(VBOX_VMM_TARGET_AGNOSTIC)
# define VMCPU_FF_VTIMER_ACTIVATED          RT_BIT_64(VMCPU_FF_VTIMER_ACTIVATED_BIT)
# define VMCPU_FF_VTIMER_ACTIVATED_BIT      13
#endif
#if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
/* Bit 13 used to be VMCPU_FF_HM_UPDATE_PAE_PDPES. */
#endif
/** This action forces the VM to resync the page tables before going
 * back to execute guest code. (GLOBAL FLUSH) */
#define VMCPU_FF_PGM_SYNC_CR3               RT_BIT_64(VMCPU_FF_PGM_SYNC_CR3_BIT)
#define VMCPU_FF_PGM_SYNC_CR3_BIT           16
/** Same as VM_FF_PGM_SYNC_CR3 except that global pages can be skipped.
 * (NON-GLOBAL FLUSH) */
#define VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL    RT_BIT_64(VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL_BIT)
#define VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL_BIT 17
/** Check for pending TLB shootdown actions (deprecated)
 * Reserved for future HM re-use if necessary / safe.
 * Consumer: HM */
#define VMCPU_FF_TLB_SHOOTDOWN_UNUSED       RT_BIT_64(VMCPU_FF_TLB_SHOOTDOWN_UNUSED_BIT)
#define VMCPU_FF_TLB_SHOOTDOWN_UNUSED_BIT   18
/** Check for pending TLB flush action.
 * Consumer: HM
 * @todo rename to VMCPU_FF_HM_TLB_FLUSH  */
#define VMCPU_FF_TLB_FLUSH                  RT_BIT_64(VMCPU_FF_TLB_FLUSH_BIT)
/** The bit number for VMCPU_FF_TLB_FLUSH. */
#define VMCPU_FF_TLB_FLUSH_BIT              19
/* 20 used to be VMCPU_FF_TRPM_SYNC_IDT (raw-mode only). */
/* 21 used to be VMCPU_FF_SELM_SYNC_TSS (raw-mode only). */
/* 22 used to be VMCPU_FF_SELM_SYNC_GDT (raw-mode only). */
/* 23 used to be VMCPU_FF_SELM_SYNC_LDT (raw-mode only). */
/* 24 used to be VMCPU_FF_INHIBIT_INTERRUPTS, which moved to CPUMCTX::eflags.uBoth in v7.0.4. */
/* 25 used to be VMCPU_FF_BLOCK_NMIS, which moved to CPUMCTX::eflags.uBoth in v7.0.4. */
/** Force return to Ring-3. */
#define VMCPU_FF_TO_R3                      RT_BIT_64(VMCPU_FF_TO_R3_BIT)
#define VMCPU_FF_TO_R3_BIT                  28
/** Force return to ring-3 to service pending I/O or MMIO write.
 * This is a backup for mechanism VINF_IOM_R3_IOPORT_COMMIT_WRITE and
 * VINF_IOM_R3_MMIO_COMMIT_WRITE, allowing VINF_EM_DBG_BREAKPOINT and similar
 * status codes to be propagated at the same time without loss. */
#define VMCPU_FF_IOM                        RT_BIT_64(VMCPU_FF_IOM_BIT)
#define VMCPU_FF_IOM_BIT                    29
/* 30 used to be VMCPU_FF_CPUM */
/** VMX-preemption timer expired. */
#define VMCPU_FF_VMX_PREEMPT_TIMER          RT_BIT_64(VMCPU_FF_VMX_PREEMPT_TIMER_BIT)
#define VMCPU_FF_VMX_PREEMPT_TIMER_BIT      31
/** Pending MTF (Monitor Trap Flag) event. */
#define VMCPU_FF_VMX_MTF                    RT_BIT_64(VMCPU_FF_VMX_MTF_BIT)
#define VMCPU_FF_VMX_MTF_BIT                32
/** VMX APIC-write emulation pending.
 * @todo possible candidate for internal EFLAGS, or maybe just a summary bit
 *       (see also VMCPU_FF_VMX_INT_WINDOW). */
#define VMCPU_FF_VMX_APIC_WRITE             RT_BIT_64(VMCPU_FF_VMX_APIC_WRITE_BIT)
#define VMCPU_FF_VMX_APIC_WRITE_BIT         33
/** VMX interrupt-window event pending.
 *
 * "Pending" is misleading here, it would be better to say that the event need
 * to be generated at the next opportunity and that this flag causes it to be
 * polled for on every instruction boundrary and such.
 *
 * @todo Change the IEM side of this to not poll but to track down the places
 *       where it can be generated and set an internal EFLAGS bit that causes it
 *       to be checked out when finishing the current instruction. */
#define VMCPU_FF_VMX_INT_WINDOW             RT_BIT_64(VMCPU_FF_VMX_INT_WINDOW_BIT)
#define VMCPU_FF_VMX_INT_WINDOW_BIT         34
/** VMX NMI-window event pending.
 * Same "pending" comment and todo in VMCPU_FF_VMX_INT_WINDOW. */
#define VMCPU_FF_VMX_NMI_WINDOW             RT_BIT_64(VMCPU_FF_VMX_NMI_WINDOW_BIT)
#define VMCPU_FF_VMX_NMI_WINDOW_BIT         35
#if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
/** VMX EOI bitmap needs to be recalculated due to a change in the IO-APIC. */
#define VMCPU_FF_VMX_EOI_BITMAP_UPDATE      RT_BIT_64(VMCPU_FF_VMX_EOI_BITMAP_UPDATE_BIT)
#define VMCPU_FF_VMX_EOI_BITMAP_UPDATE_BIT  36
#endif


/** Externally VM forced actions. Used to quit the idle/wait loop. */
#define VM_FF_EXTERNAL_SUSPENDED_MASK           (  VM_FF_CHECK_VM_STATE | VM_FF_DBGF | VM_FF_REQUEST | VM_FF_EMT_RENDEZVOUS )
/** Externally VMCPU forced actions. Used to quit the idle/wait loop. */
#define VMCPU_FF_EXTERNAL_SUSPENDED_MASK        (  VMCPU_FF_REQUEST  | VMCPU_FF_DBGF )

/** Externally forced VM actions. Used to quit the idle/wait loop. */
#define VM_FF_EXTERNAL_HALTED_MASK              (  VM_FF_CHECK_VM_STATE | VM_FF_DBGF    | VM_FF_REQUEST \
                                                 | VM_FF_PDM_QUEUES     | VM_FF_PDM_DMA | VM_FF_EMT_RENDEZVOUS )

#ifndef VBOX_VMM_TARGET_AGNOSTIC
/** Externally forced VMCPU actions. Used to quit the idle/wait loop. */
# if defined(VBOX_VMM_TARGET_ARMV8)
#  define VMCPU_FF_EXTERNAL_HALTED_MASK         (  VMCPU_FF_INTERRUPT_IRQ | VMCPU_FF_INTERRUPT_FIQ \
                                                 | VMCPU_FF_REQUEST       | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI \
                                                 | VMCPU_FF_UNHALT        | VMCPU_FF_TIMER          | VMCPU_FF_DBGF \
                                                 | VMCPU_FF_VTIMER_ACTIVATED)
# else
#  define VMCPU_FF_EXTERNAL_HALTED_MASK         (  VMCPU_FF_UPDATE_APIC | VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC \
                                                 | VMCPU_FF_REQUEST     | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI \
                                                 | VMCPU_FF_UNHALT      | VMCPU_FF_TIMER          | VMCPU_FF_DBGF \
                                                 | VMCPU_FF_INTERRUPT_NESTED_GUEST)
# endif
#endif

/** High priority VM pre-execution actions. */
#define VM_FF_HIGH_PRIORITY_PRE_MASK            (  VM_FF_CHECK_VM_STATE | VM_FF_DBGF                 | VM_FF_TM_VIRTUAL_SYNC \
                                                 | VM_FF_DEBUG_SUSPEND  | VM_FF_PGM_NEED_HANDY_PAGES | VM_FF_PGM_NO_MEMORY \
                                                 | VM_FF_EMT_RENDEZVOUS )
#ifndef VBOX_VMM_TARGET_AGNOSTIC
/** High priority VMCPU pre-execution actions. */
# if defined(VBOX_VMM_TARGET_ARMV8)
#  define VMCPU_FF_HIGH_PRIORITY_PRE_MASK       (  VMCPU_FF_TIMER        | VMCPU_FF_INTERRUPT_IRQ     | VMCPU_FF_INTERRUPT_FIQ \
                                                 | VMCPU_FF_DBGF         | VMCPU_FF_VTIMER_ACTIVATED)
# else
#  define VMCPU_FF_HIGH_PRIORITY_PRE_MASK       (  VMCPU_FF_TIMER        | VMCPU_FF_INTERRUPT_APIC     | VMCPU_FF_INTERRUPT_PIC \
                                                 | VMCPU_FF_UPDATE_APIC  | VMCPU_FF_DBGF \
                                                 | VMCPU_FF_PGM_SYNC_CR3 | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL \
                                                 | VMCPU_FF_INTERRUPT_NESTED_GUEST | VMCPU_FF_VMX_MTF  | VMCPU_FF_VMX_APIC_WRITE \
                                                 | VMCPU_FF_VMX_PREEMPT_TIMER | VMCPU_FF_VMX_NMI_WINDOW | VMCPU_FF_VMX_INT_WINDOW )
# endif
#endif

/** High priority VM pre raw-mode execution mask. */
#define VM_FF_HIGH_PRIORITY_PRE_RAW_MASK        (  VM_FF_PGM_NEED_HANDY_PAGES | VM_FF_PGM_NO_MEMORY )
/** High priority VMCPU pre raw-mode execution mask. */
#define VMCPU_FF_HIGH_PRIORITY_PRE_RAW_MASK     (  VMCPU_FF_PGM_SYNC_CR3 | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL )

/** High priority post-execution actions. */
#define VM_FF_HIGH_PRIORITY_POST_MASK           (  VM_FF_PGM_NO_MEMORY )
/** High priority post-execution actions. */
#define VMCPU_FF_HIGH_PRIORITY_POST_MASK        (  VMCPU_FF_PDM_CRITSECT | VMCPU_FF_HM_UPDATE_CR3 | VMCPU_FF_IEM | VMCPU_FF_IOM )

/** Normal priority VM post-execution actions. */
#define VM_FF_NORMAL_PRIORITY_POST_MASK         (  VM_FF_CHECK_VM_STATE | VM_FF_DBGF | VM_FF_RESET \
                                                 | VM_FF_PGM_NO_MEMORY  | VM_FF_EMT_RENDEZVOUS)
/** Normal priority VMCPU post-execution actions. */
#define VMCPU_FF_NORMAL_PRIORITY_POST_MASK      ( VMCPU_FF_DBGF )

/** Normal priority VM actions. */
#define VM_FF_NORMAL_PRIORITY_MASK              (  VM_FF_REQUEST | VM_FF_PDM_QUEUES | VM_FF_PDM_DMA | VM_FF_EMT_RENDEZVOUS)
/** Normal priority VMCPU actions. */
#define VMCPU_FF_NORMAL_PRIORITY_MASK           (  VMCPU_FF_REQUEST )

/** Flags to clear before resuming guest execution. */
#define VMCPU_FF_RESUME_GUEST_MASK              (  VMCPU_FF_TO_R3 )


/** VM flags that cause the REP[|NE|E] STRINS loops to yield immediately. */
#define VM_FF_HIGH_PRIORITY_POST_REPSTR_MASK    (  VM_FF_TM_VIRTUAL_SYNC | VM_FF_PGM_NEED_HANDY_PAGES   | VM_FF_PGM_NO_MEMORY \
                                                 | VM_FF_EMT_RENDEZVOUS  | VM_FF_PGM_POOL_FLUSH_PENDING | VM_FF_RESET)
/** VM flags that cause the REP[|NE|E] STRINS loops to yield. */
#define VM_FF_YIELD_REPSTR_MASK                 (  VM_FF_HIGH_PRIORITY_POST_REPSTR_MASK \
                                                 | VM_FF_PDM_QUEUES | VM_FF_PDM_DMA | VM_FF_DBGF | VM_FF_DEBUG_SUSPEND )
/** VMCPU flags that cause the REP[|NE|E] STRINS loops to yield immediately. */
#ifdef IN_RING3
# define VMCPU_FF_HIGH_PRIORITY_POST_REPSTR_MASK (VMCPU_FF_DBGF | VMCPU_FF_VMX_MTF)
#else
# define VMCPU_FF_HIGH_PRIORITY_POST_REPSTR_MASK (VMCPU_FF_TO_R3 | VMCPU_FF_IEM | VMCPU_FF_IOM | VMCPU_FF_DBGF | VMCPU_FF_VMX_MTF)
#endif

#if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
/** VMCPU flags that cause the REP[|NE|E] STRINS loops to yield, interrupts
 *  enabled. */
# define VMCPU_FF_YIELD_REPSTR_MASK              (  VMCPU_FF_HIGH_PRIORITY_POST_REPSTR_MASK \
                                                  | VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_UPDATE_APIC   | VMCPU_FF_INTERRUPT_PIC \
                                                  | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI | VMCPU_FF_PDM_CRITSECT \
                                                  | VMCPU_FF_TIMER          | VMCPU_FF_REQUEST       \
                                                  | VMCPU_FF_INTERRUPT_NESTED_GUEST )
/** VMCPU flags that cause the REP[|NE|E] STRINS loops to yield, interrupts
 *  disabled. */
# define VMCPU_FF_YIELD_REPSTR_NOINT_MASK        (  VMCPU_FF_YIELD_REPSTR_MASK \
                                                  & ~(  VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_UPDATE_APIC | VMCPU_FF_INTERRUPT_PIC \
                                                      | VMCPU_FF_INTERRUPT_NESTED_GUEST) )
#endif

/** VM Flags that cause the HM loops to go back to ring-3. */
#define VM_FF_HM_TO_R3_MASK                     (  VM_FF_TM_VIRTUAL_SYNC | VM_FF_PGM_NEED_HANDY_PAGES | VM_FF_PGM_NO_MEMORY \
                                                 | VM_FF_PDM_QUEUES | VM_FF_EMT_RENDEZVOUS)
/** VMCPU Flags that cause the HM loops to go back to ring-3. */
#define VMCPU_FF_HM_TO_R3_MASK                  (  VMCPU_FF_TO_R3 | VMCPU_FF_TIMER | VMCPU_FF_PDM_CRITSECT \
                                                 | VMCPU_FF_IEM   | VMCPU_FF_IOM)

/** High priority ring-0 VM pre HM-mode execution mask. */
#define VM_FF_HP_R0_PRE_HM_MASK                 (VM_FF_HM_TO_R3_MASK | VM_FF_REQUEST | VM_FF_PGM_POOL_FLUSH_PENDING | VM_FF_PDM_DMA)
/** High priority ring-0 VMCPU pre HM-mode execution mask. */
#define VMCPU_FF_HP_R0_PRE_HM_MASK              (  VMCPU_FF_HM_TO_R3_MASK | VMCPU_FF_PGM_SYNC_CR3 \
                                                 | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL | VMCPU_FF_REQUEST \
                                                 | VMCPU_FF_VMX_APIC_WRITE | VMCPU_FF_VMX_MTF | VMCPU_FF_VMX_PREEMPT_TIMER)
/** High priority ring-0 VM pre HM-mode execution mask, single stepping. */
#define VM_FF_HP_R0_PRE_HM_STEP_MASK            (VM_FF_HP_R0_PRE_HM_MASK & ~(  VM_FF_TM_VIRTUAL_SYNC | VM_FF_PDM_QUEUES  \
                                                                             | VM_FF_EMT_RENDEZVOUS | VM_FF_REQUEST \
                                                                             | VM_FF_PDM_DMA) )
/** High priority ring-0 VMCPU pre HM-mode execution mask, single stepping. */
#define VMCPU_FF_HP_R0_PRE_HM_STEP_MASK         (VMCPU_FF_HP_R0_PRE_HM_MASK & ~(  VMCPU_FF_TO_R3 | VMCPU_FF_TIMER \
                                                                                | VMCPU_FF_PDM_CRITSECT | VMCPU_FF_REQUEST) )

/** All the VMX nested-guest flags. */
#define VMCPU_FF_VMX_ALL_MASK                   (  VMCPU_FF_VMX_PREEMPT_TIMER | VMCPU_FF_VMX_MTF | VMCPU_FF_VMX_APIC_WRITE \
                                                 | VMCPU_FF_VMX_INT_WINDOW | VMCPU_FF_VMX_NMI_WINDOW )

/** All the forced VM flags. */
#define VM_FF_ALL_MASK                          (UINT32_MAX)
/** All the forced VMCPU flags. */
#define VMCPU_FF_ALL_MASK                       (  UINT32_MAX \
                                                 | VMCPU_FF_VMX_MTF | VMCPU_FF_VMX_APIC_WRITE | VMCPU_FF_VMX_INT_WINDOW \
                                                 | VMCPU_FF_VMX_NMI_WINDOW )

/** All the forced VM flags except those related to raw-mode and hardware
 * assisted execution. */
#define VM_FF_ALL_REM_MASK                      (~(VM_FF_HIGH_PRIORITY_PRE_RAW_MASK) | VM_FF_PGM_NEED_HANDY_PAGES | VM_FF_PGM_NO_MEMORY)
/** All the forced VMCPU flags except those related to raw-mode and hardware
 * assisted execution. */
#define VMCPU_FF_ALL_REM_MASK                   (~(VMCPU_FF_HIGH_PRIORITY_PRE_RAW_MASK | VMCPU_FF_PDM_CRITSECT | VMCPU_FF_TLB_FLUSH))

#ifndef VBOX_FOR_DTRACE_LIB
AssertCompile(  ((VM_FF_HIGH_PRIORITY_POST_REPSTR_MASK | VM_FF_YIELD_REPSTR_MASK)
               & (VM_FF_HIGH_PRIORITY_PRE_RAW_MASK & ~VM_FF_ALL_REM_MASK)) == 0);
AssertCompile((VMCPU_FF_HIGH_PRIORITY_POST_REPSTR_MASK & (VMCPU_FF_HIGH_PRIORITY_PRE_RAW_MASK & ~VMCPU_FF_ALL_REM_MASK)) == 0);
#endif

/** @} */

/** @def VM_FF_SET
 * Sets a single force action flag.
 *
 * @param   pVM     The cross context VM structure.
 * @param   fFlag   The flag to set.
 */
#define VM_FF_SET(pVM, fFlag) do { \
        AssertCompile(RT_IS_POWER_OF_TWO(fFlag)); \
        AssertCompile((fFlag) == RT_BIT_32(fFlag##_BIT)); \
        ASMAtomicOrU32(&(pVM)->fGlobalForcedActions, (fFlag)); \
    } while (0)

/** @def VMCPU_FF_SET
 * Sets a single force action flag for the given VCPU.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fFlag   The flag to set.
 * @sa      VMCPU_FF_SET_MASK
 */
#ifdef VMCPU_WITH_64_BIT_FFS
# define VMCPU_FF_SET(pVCpu, fFlag) do { \
        AssertCompile(RT_IS_POWER_OF_TWO(fFlag)); \
        AssertCompile((fFlag) == RT_BIT_64(fFlag##_BIT)); \
        ASMAtomicBitSet(&(pVCpu)->fLocalForcedActions, fFlag##_BIT); \
    } while (0)
#else
# define VMCPU_FF_SET(pVCpu, fFlag) do { \
        AssertCompile(RT_IS_POWER_OF_TWO(fFlag)); \
        AssertCompile((fFlag) == RT_BIT_32(fFlag##_BIT)); \
        ASMAtomicOrU32(&(pVCpu)->fLocalForcedActions, (fFlag)); \
    } while (0)
#endif

/** @def VMCPU_FF_SET_MASK
 * Sets a two or more force action flag for the given VCPU.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fFlags  The flags to set.
 * @sa      VMCPU_FF_SET
 */
#ifdef VMCPU_WITH_64_BIT_FFS
# if ARCH_BITS > 32
#  define VMCPU_FF_SET_MASK(pVCpu, fFlags) \
    do { ASMAtomicOrU64(&pVCpu->fLocalForcedActions, (fFlags)); } while (0)
# else
#  define VMCPU_FF_SET_MASK(pVCpu, fFlags) do { \
        if (!((fFlags) >> 32)) ASMAtomicOrU32((uint32_t volatile *)&pVCpu->fLocalForcedActions, (uint32_t)(fFlags)); \
        else ASMAtomicOrU64(&pVCpu->fLocalForcedActions, (fFlags)); \
    } while (0)
# endif
#else
# define VMCPU_FF_SET_MASK(pVCpu, fFlags) \
    do { ASMAtomicOrU32(&pVCpu->fLocalForcedActions, (fFlags)); } while (0)
#endif

/** @def VM_FF_CLEAR
 * Clears a single force action flag.
 *
 * @param   pVM     The cross context VM structure.
 * @param   fFlag   The flag to clear.
 */
#define VM_FF_CLEAR(pVM, fFlag) do { \
        AssertCompile(RT_IS_POWER_OF_TWO(fFlag)); \
        AssertCompile((fFlag) == RT_BIT_32(fFlag##_BIT)); \
        ASMAtomicAndU32(&(pVM)->fGlobalForcedActions, ~(fFlag)); \
    } while (0)

/** @def VMCPU_FF_CLEAR
 * Clears a single force action flag for the given VCPU.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fFlag   The flag to clear.
 */
#ifdef VMCPU_WITH_64_BIT_FFS
# define VMCPU_FF_CLEAR(pVCpu, fFlag) do { \
        AssertCompile(RT_IS_POWER_OF_TWO(fFlag)); \
        AssertCompile((fFlag) == RT_BIT_64(fFlag##_BIT)); \
        ASMAtomicBitClear(&(pVCpu)->fLocalForcedActions, fFlag##_BIT); \
    } while (0)
#else
# define VMCPU_FF_CLEAR(pVCpu, fFlag) do { \
        AssertCompile(RT_IS_POWER_OF_TWO(fFlag)); \
        AssertCompile((fFlag) == RT_BIT_32(fFlag##_BIT)); \
        ASMAtomicAndU32(&(pVCpu)->fLocalForcedActions, ~(fFlag)); \
    } while (0)
#endif

/** @def VMCPU_FF_CLEAR_MASK
 * Clears two or more force action flags for the given VCPU.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fFlags  The flags to clear.
 */
#ifdef VMCPU_WITH_64_BIT_FFS
# if ARCH_BITS > 32
# define VMCPU_FF_CLEAR_MASK(pVCpu, fFlags) \
    do { ASMAtomicAndU64(&(pVCpu)->fLocalForcedActions, ~(fFlags)); } while (0)
# else
# define VMCPU_FF_CLEAR_MASK(pVCpu, fFlags) do { \
        if (!((fFlags) >> 32)) ASMAtomicAndU32((uint32_t volatile *)&(pVCpu)->fLocalForcedActions, ~(uint32_t)(fFlags)); \
        else ASMAtomicAndU64(&(pVCpu)->fLocalForcedActions, ~(fFlags)); \
    } while (0)
# endif
#else
# define VMCPU_FF_CLEAR_MASK(pVCpu, fFlags) \
    do { ASMAtomicAndU32(&(pVCpu)->fLocalForcedActions, ~(fFlags)); } while (0)
#endif

/** @def VM_FF_IS_SET
 * Checks if single a force action flag is set.
 *
 * @param   pVM     The cross context VM structure.
 * @param   fFlag   The flag to check.
 * @sa      VM_FF_IS_ANY_SET
 */
#if !defined(VBOX_STRICT) || !defined(RT_COMPILER_SUPPORTS_LAMBDA)
# define VM_FF_IS_SET(pVM, fFlag)           RT_BOOL((pVM)->fGlobalForcedActions & (fFlag))
#else
# define VM_FF_IS_SET(pVM, fFlag) \
    ([](PVM a_pVM) -> bool \
    { \
        AssertCompile(RT_IS_POWER_OF_TWO(fFlag)); \
        AssertCompile((fFlag) == RT_BIT_32(fFlag##_BIT)); \
        return RT_BOOL(a_pVM->fGlobalForcedActions & (fFlag)); \
    }(pVM))
#endif

/** @def VMCPU_FF_IS_SET
 * Checks if a single force action flag is set for the given VCPU.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fFlag   The flag to check.
 * @sa      VMCPU_FF_IS_ANY_SET
 */
#if !defined(VBOX_STRICT) || !defined(RT_COMPILER_SUPPORTS_LAMBDA)
# define VMCPU_FF_IS_SET(pVCpu, fFlag)      RT_BOOL((pVCpu)->fLocalForcedActions & (fFlag))
#else
# define VMCPU_FF_IS_SET(pVCpu, fFlag) \
    ([](PCVMCPU a_pVCpu) -> bool \
    { \
        AssertCompile(RT_IS_POWER_OF_TWO(fFlag)); \
        AssertCompile((fFlag) == RT_BIT_64(fFlag##_BIT)); \
        return RT_BOOL(a_pVCpu->fLocalForcedActions & (fFlag)); \
    }(pVCpu))
#endif

/** @def VM_FF_IS_ANY_SET
 * Checks if one or more force action in the specified set is pending.
 *
 * @param   pVM     The cross context VM structure.
 * @param   fFlags  The flags to check for.
 * @sa      VM_FF_IS_SET
 */
#define VM_FF_IS_ANY_SET(pVM, fFlags)       RT_BOOL((pVM)->fGlobalForcedActions & (fFlags))

/** @def VMCPU_FF_IS_ANY_SET
 * Checks if two or more force action flags in the specified set is set for the given VCPU.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fFlags  The flags to check for.
 * @sa      VMCPU_FF_IS_SET
 */
#define VMCPU_FF_IS_ANY_SET(pVCpu, fFlags)  RT_BOOL((pVCpu)->fLocalForcedActions & (fFlags))

/** @def VM_FF_TEST_AND_CLEAR
 * Checks if one (!) force action in the specified set is pending and clears it atomically
 *
 * @returns true if the bit was set.
 * @returns false if the bit was clear.
 * @param   pVM     The cross context VM structure.
 * @param   fFlag   Flag constant to check and clear (_BIT is appended).
 */
#define VM_FF_TEST_AND_CLEAR(pVM, fFlag)    (ASMAtomicBitTestAndClear(&(pVM)->fGlobalForcedActions, fFlag##_BIT))

/** @def VMCPU_FF_TEST_AND_CLEAR
 * Checks if one (!) force action in the specified set is pending and clears it atomically
 *
 * @returns true if the bit was set.
 * @returns false if the bit was clear.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   fFlag   Flag constant to check and clear (_BIT is appended).
 */
#define VMCPU_FF_TEST_AND_CLEAR(pVCpu, fFlag) (ASMAtomicBitTestAndClear(&(pVCpu)->fLocalForcedActions, fFlag##_BIT))

/** @def VM_FF_IS_PENDING_EXCEPT
 * Checks if one or more force action in the specified set is pending while one
 * or more other ones are not.
 *
 * @param   pVM     The cross context VM structure.
 * @param   fFlags  The flags to check for.
 * @param   fExcpt  The flags that should not be set.
 */
#define VM_FF_IS_PENDING_EXCEPT(pVM, fFlags, fExcpt) \
    ( ((pVM)->fGlobalForcedActions & (fFlags)) && !((pVM)->fGlobalForcedActions & (fExcpt)) )

/** @def VM_IS_EMT
 * Checks if the current thread is the emulation thread (EMT).
 *
 * @remark  The ring-0 variation will need attention if we expand the ring-0
 *          code to let threads other than EMT mess around with the VM.
 */
#ifdef IN_RC
# define VM_IS_EMT(pVM)                     true
#else
# define VM_IS_EMT(pVM)                     (VMMGetCpu(pVM) != NULL)
#endif

/** @def VMCPU_IS_EMT
 * Checks if the current thread is the emulation thread (EMT) for the specified
 * virtual CPU.
 */
#ifdef IN_RC
# define VMCPU_IS_EMT(pVCpu)                true
#else
# define VMCPU_IS_EMT(pVCpu)                ((pVCpu) && ((pVCpu) == VMMGetCpu((pVCpu)->CTX_SUFF(pVM))))
#endif

/** @def VM_ASSERT_EMT
 * Asserts that the current thread IS the emulation thread (EMT).
 */
#ifdef IN_RC
# define VM_ASSERT_EMT(pVM)                 Assert(VM_IS_EMT(pVM))
#elif defined(IN_RING0)
# define VM_ASSERT_EMT(pVM)                 Assert(VM_IS_EMT(pVM))
#else
# define VM_ASSERT_EMT(pVM) \
    AssertMsg(VM_IS_EMT(pVM), \
        ("Not emulation thread! Thread=%RTnthrd ThreadEMT=%RTnthrd\n", RTThreadNativeSelf(), VMR3GetVMCPUNativeThread(pVM)))
#endif

/** @def VMCPU_ASSERT_EMT
 * Asserts that the current thread IS the emulation thread (EMT) of the
 * specified virtual CPU.
 */
#ifdef IN_RC
# define VMCPU_ASSERT_EMT(pVCpu)            Assert(VMCPU_IS_EMT(pVCpu))
#elif defined(IN_RING0)
# define VMCPU_ASSERT_EMT(pVCpu)            AssertMsg(VMCPU_IS_EMT(pVCpu), \
                                                      ("Not emulation thread! Thread=%RTnthrd ThreadEMT=%RTnthrd idCpu=%u\n", \
                                                      RTThreadNativeSelf(), (pVCpu) ? (pVCpu)->hNativeThreadR0 : 0, \
                                                      (pVCpu) ? (pVCpu)->idCpu : 0))
#else
# define VMCPU_ASSERT_EMT(pVCpu)            AssertMsg(VMCPU_IS_EMT(pVCpu), \
                                                      ("Not emulation thread! Thread=%RTnthrd ThreadEMT=%RTnthrd idCpu=%#x\n", \
                                                      RTThreadNativeSelf(), (pVCpu)->hNativeThread, (pVCpu)->idCpu))
#endif

/** @def VM_ASSERT_EMT_RETURN
 * Asserts that the current thread IS the emulation thread (EMT) and returns if it isn't.
 */
#ifdef IN_RC
# define VM_ASSERT_EMT_RETURN(pVM, rc)      AssertReturn(VM_IS_EMT(pVM), (rc))
#elif defined(IN_RING0)
# define VM_ASSERT_EMT_RETURN(pVM, rc)      AssertReturn(VM_IS_EMT(pVM), (rc))
#else
# define VM_ASSERT_EMT_RETURN(pVM, rc) \
    AssertMsgReturn(VM_IS_EMT(pVM), \
        ("Not emulation thread! Thread=%RTnthrd ThreadEMT=%RTnthrd\n", RTThreadNativeSelf(), VMR3GetVMCPUNativeThread(pVM)), \
        (rc))
#endif

/** @def VMCPU_ASSERT_EMT_RETURN
 * Asserts that the current thread IS the emulation thread (EMT) and returns if it isn't.
 */
#ifdef IN_RC
# define VMCPU_ASSERT_EMT_RETURN(pVCpu, rc) AssertReturn(VMCPU_IS_EMT(pVCpu), (rc))
#elif defined(IN_RING0)
# define VMCPU_ASSERT_EMT_RETURN(pVCpu, rc) AssertReturn(VMCPU_IS_EMT(pVCpu), (rc))
#else
# define VMCPU_ASSERT_EMT_RETURN(pVCpu, rc) \
    AssertMsgReturn(VMCPU_IS_EMT(pVCpu), \
                    ("Not emulation thread! Thread=%RTnthrd ThreadEMT=%RTnthrd idCpu=%#x\n", \
                     RTThreadNativeSelf(), (pVCpu)->hNativeThread, (pVCpu)->idCpu), \
                    (rc))
#endif

/** @def VMCPU_ASSERT_EMT_OR_GURU
 * Asserts that the current thread IS the emulation thread (EMT) of the
 * specified virtual CPU.
 */
#if defined(IN_RC) || defined(IN_RING0)
# define VMCPU_ASSERT_EMT_OR_GURU(pVCpu)    Assert(   VMCPU_IS_EMT(pVCpu) \
                                                   || pVCpu->CTX_SUFF(pVM)->enmVMState == VMSTATE_GURU_MEDITATION \
                                                   || pVCpu->CTX_SUFF(pVM)->enmVMState == VMSTATE_GURU_MEDITATION_LS )
#else
# define VMCPU_ASSERT_EMT_OR_GURU(pVCpu) \
    AssertMsg(   VMCPU_IS_EMT(pVCpu) \
              || pVCpu->CTX_SUFF(pVM)->enmVMState == VMSTATE_GURU_MEDITATION \
              || pVCpu->CTX_SUFF(pVM)->enmVMState == VMSTATE_GURU_MEDITATION_LS, \
              ("Not emulation thread! Thread=%RTnthrd ThreadEMT=%RTnthrd idCpu=%#x\n", \
               RTThreadNativeSelf(), (pVCpu)->hNativeThread, (pVCpu)->idCpu))
#endif

/** @def VMCPU_ASSERT_EMT_OR_NOT_RUNNING
 * Asserts that the current thread IS the emulation thread (EMT) of the
 * specified virtual CPU or the VM is not running.
 */
#if defined(IN_RC) || defined(IN_RING0)
# define VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu) \
    Assert(    VMCPU_IS_EMT(pVCpu) \
           || !VM_IS_RUNNING_FOR_ASSERTIONS_ONLY((pVCpu)->CTX_SUFF(pVM)) )
#else
# define VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu) \
    AssertMsg(    VMCPU_IS_EMT(pVCpu) \
              || !VM_IS_RUNNING_FOR_ASSERTIONS_ONLY((pVCpu)->CTX_SUFF(pVM)), \
              ("Not emulation thread! Thread=%RTnthrd ThreadEMT=%RTnthrd idCpu=%#x\n", \
               RTThreadNativeSelf(), (pVCpu)->hNativeThread, (pVCpu)->idCpu))
#endif

/** @def VMSTATE_IS_RUNNING
 * Checks if the given state indicates a running VM.
 */
#define VMSTATE_IS_RUNNING(a_enmVMState) \
    (   (a_enmVMState) == VMSTATE_RUNNING \
     || (a_enmVMState) == VMSTATE_RUNNING_LS )

/** @def VM_IS_RUNNING_FOR_ASSERTIONS_ONLY
 * Checks if the VM is running.
 * @note This is only for pure debug assertions.  No AssertReturn or similar!
 * @sa  VMSTATE_IS_RUNNING
 */
#define VM_IS_RUNNING_FOR_ASSERTIONS_ONLY(pVM) \
    (   (pVM)->enmVMState == VMSTATE_RUNNING \
     || (pVM)->enmVMState == VMSTATE_RUNNING_LS )


/** @def VMSTATE_IS_POWERED_ON
 * Checks if the given state indicates the VM is powered on.
 *
 * @note Excludes all error states, so a powered on VM that hit a fatal error,
 *       guru meditation, state load failure or similar will not be considered
 *       powered on by this test.
 */
#define VMSTATE_IS_POWERED_ON(a_enmVMState) \
    ( (a_enmVMState) >= VMSTATE_RESUMING && (a_enmVMState) < VMSTATE_POWERING_OFF )

/** @def VM_ASSERT_IS_NOT_RUNNING
 * Asserts that the VM is not running.
 */
#if defined(IN_RC) || defined(IN_RING0)
#define VM_ASSERT_IS_NOT_RUNNING(pVM)       Assert(!VM_IS_RUNNING_FOR_ASSERTIONS_ONLY(pVM))
#else
#define VM_ASSERT_IS_NOT_RUNNING(pVM)       AssertMsg(!VM_IS_RUNNING_FOR_ASSERTIONS_ONLY(pVM), \
                                                      ("VM is running. enmVMState=%d\n", (pVM)->enmVMState))
#endif

/** @def VM_ASSERT_EMT0
 * Asserts that the current thread IS emulation thread \#0 (EMT0).
 */
#ifdef IN_RING3
# define VM_ASSERT_EMT0(a_pVM)              VMCPU_ASSERT_EMT((a_pVM)->apCpusR3[0])
#else
# define VM_ASSERT_EMT0(a_pVM)              VMCPU_ASSERT_EMT(&(a_pVM)->aCpus[0])
#endif

/** @def VM_ASSERT_EMT0_RETURN
 * Asserts that the current thread IS emulation thread \#0 (EMT0) and returns if
 * it isn't.
 */
#ifdef IN_RING3
# define VM_ASSERT_EMT0_RETURN(pVM, rc)     VMCPU_ASSERT_EMT_RETURN((pVM)->apCpusR3[0], (rc))
#else
# define VM_ASSERT_EMT0_RETURN(pVM, rc)     VMCPU_ASSERT_EMT_RETURN(&(pVM)->aCpus[0], (rc))
#endif


/**
 * Asserts that the current thread is NOT the emulation thread.
 */
#define VM_ASSERT_OTHER_THREAD(pVM) \
    AssertMsg(!VM_IS_EMT(pVM), ("Not other thread!!\n"))


/** @def VM_ASSERT_STATE
 * Asserts a certain VM state.
 */
#define VM_ASSERT_STATE(pVM, _enmState) \
        AssertMsg((pVM)->enmVMState == (_enmState), \
                  ("state %s, expected %s\n", VMGetStateName((pVM)->enmVMState), VMGetStateName(_enmState)))

/** @def VM_ASSERT_STATE_RETURN
 * Asserts a certain VM state and returns if it doesn't match.
 */
#define VM_ASSERT_STATE_RETURN(pVM, _enmState, rc) \
        AssertMsgReturn((pVM)->enmVMState == (_enmState), \
                        ("state %s, expected %s\n", VMGetStateName((pVM)->enmVMState), VMGetStateName(_enmState)), \
                        (rc))

/** @def VM_IS_VALID_EXT
 * Check that a VM handle is valid for external access, i.e. not being destroy
 * or terminated and matching the target platform architecture (ring-3). */
#ifdef VMTARGET_DEFAULT
# define VM_IS_VALID_EXT(pVM) \
        (   RT_VALID_ALIGNED_PTR(pVM, HOST_PAGE_SIZE_DYNAMIC) \
         && (   (unsigned)(pVM)->enmVMState < (unsigned)VMSTATE_DESTROYING \
             || (   (unsigned)(pVM)->enmVMState == (unsigned)VMSTATE_DESTROYING \
                 && VM_IS_EMT(pVM))) \
         && (pVM)->enmTarget == VMTARGET_DEFAULT)
#else
# define VM_IS_VALID_EXT(pVM) \
        (   RT_VALID_ALIGNED_PTR(pVM, HOST_PAGE_SIZE_DYNAMIC) \
         && (   (unsigned)(pVM)->enmVMState < (unsigned)VMSTATE_DESTROYING \
             || (   (unsigned)(pVM)->enmVMState == (unsigned)VMSTATE_DESTROYING \
                 && VM_IS_EMT(pVM))) )
#endif

/** @def VM_ASSERT_VALID_EXT_RETURN
 * Asserts that a VM handle is valid for external access, i.e. not being destroy
 * or terminated.
 */
#define VM_ASSERT_VALID_EXT_RETURN(pVM, rc) \
        AssertMsgReturn(VM_IS_VALID_EXT(pVM), \
                        ("pVM=%p state %s enmTarget=%#x\n", (pVM), RT_VALID_ALIGNED_PTR(pVM, HOST_PAGE_SIZE_DYNAMIC) \
                         ? VMGetStateName(pVM->enmVMState) : "", (pVM)->enmTarget), \
                        (rc))

/** @def VMCPU_IS_VALID_EXT
 * Checks that a VMCPU handle is valid for external access, i.e. not being
 * destroy or terminated and matching the target platform architecture (r3). */
#ifdef VMTARGET_DEFAULT
# define VMCPU_IS_VALID_EXT(a_pVCpu) \
        (   RT_VALID_ALIGNED_PTR(a_pVCpu, 64) \
         && RT_VALID_ALIGNED_PTR((a_pVCpu)->CTX_SUFF(pVM), HOST_PAGE_SIZE_DYNAMIC) \
         && (unsigned)(a_pVCpu)->CTX_SUFF(pVM)->enmVMState < (unsigned)VMSTATE_DESTROYING \
         && (pVM)->enmTarget == VMTARGET_DEFAULT)
#else
# define VMCPU_IS_VALID_EXT(a_pVCpu) \
        (   RT_VALID_ALIGNED_PTR(a_pVCpu, 64) \
         && RT_VALID_ALIGNED_PTR((a_pVCpu)->CTX_SUFF(pVM), HOST_PAGE_SIZE_DYNAMIC) \
         && (unsigned)(a_pVCpu)->CTX_SUFF(pVM)->enmVMState < (unsigned)VMSTATE_DESTROYING)
#endif

/** @def VMCPU_ASSERT_VALID_EXT_RETURN
 * Asserts that a VMCPU handle is valid for external access, i.e. not being
 * destroy or terminated and matching the target platform architecutre (r3).
 */
#define VMCPU_ASSERT_VALID_EXT_RETURN(pVCpu, rc) \
        AssertMsgReturn(VMCPU_IS_VALID_EXT(pVCpu), \
                        ("pVCpu=%p pVM=%p state %s enmTarget=%#x\n", (pVCpu), \
                        RT_VALID_ALIGNED_PTR(pVCpu, 64) ? (pVCpu)->CTX_SUFF(pVM) : NULL, \
                         RT_VALID_ALIGNED_PTR(pVCpu, 64) && RT_VALID_ALIGNED_PTR((pVCpu)->CTX_SUFF(pVM), HOST_PAGE_SIZE_DYNAMIC) \
                         ? VMGetStateName((pVCpu)->pVMR3->enmVMState) : "", (pVCpu)->enmTarget), \
                        (rc))

#if defined(USING_VMM_COMMON_DEFS) || defined(DOXYGEN_RUNNING)
/* Some VMM_COMMON_DEFS defines that actively changes the VM/VMCPU structures
   that we bake into the VM_STRUCT_VERSION value. */
# ifdef VBOX_WITH_MINIMAL_R0
#  define VM_STRUCT_VERSION_F_31    RT_BIT_32(31)
# else
#  define VM_STRUCT_VERSION_F_31    UINT32_C(0)
# endif
# ifdef VBOX_WITH_ONLY_PGM_NEM_MODE
#  define VM_STRUCT_VERSION_F_30    RT_BIT_32(30)
# else
#  define VM_STRUCT_VERSION_F_30    UINT32_C(0)
# endif
# ifdef VBOX_WITH_PGM_NEM_MODE
#  define VM_STRUCT_VERSION_F_29    RT_BIT_32(29)
# else
#  define VM_STRUCT_VERSION_F_29    UINT32_C(0)
# endif
# ifdef VBOX_WITH_HWVIRT
#  define VM_STRUCT_VERSION_F_28    RT_BIT_32(28)
# else
#  define VM_STRUCT_VERSION_F_28    UINT32_C(0)
# endif

/** @def VM_STRUCT_VERSION
 * The current VM structure version number.  */
# define VM_STRUCT_VERSION          (  UINT32_C(2) \
                                     | VM_STRUCT_VERSION_F_31 \
                                     | VM_STRUCT_VERSION_F_30 \
                                     | VM_STRUCT_VERSION_F_29 \
                                     | VM_STRUCT_VERSION_F_28 )

# if (defined(RT_ARCH_AMD64) && defined(VBOX_WITH_VIRT_ARMV8) && defined(IN_RING0)) || defined(DOXYGEN_RUNNING)
/** @def VM_STRUCT_VERSION_NON_NATIVE_TARGETS
 * The current VM structure version for the other architecture (hack).
 *
 * Currently the VBoxVMMArm.dll/so/dylib on x86 differs from VM_STRUCT_VERSION
 * in that it will have VBOX_WITH_ONLY_PGM_NEM_MODE & VBOX_WITH_MINIMAL_R0
 * defined but not VBOX_WITH_HWVIRT.  This is to get the stuff off the ground
 * quickly by emulating how it's built on win.arm64 hosts. */
#  define VM_STRUCT_VERSION_NON_NATIVE_TARGETS \
        (  (  VM_STRUCT_VERSION \
            | RT_BIT_32(31) /*VBOX_WITH_MINIMAL_R0*/ \
            | RT_BIT_32(30) /*VBOX_WITH_ONLY_PGM_NEM_MODE*/ ) \
         & ~RT_BIT_32(28) /*VBOX_WITH_HWVIRT*/ )
# endif

/** @def VM_IS_NON_NATIVE_WITH_LIMITED_R0
 * Whether the is a non-default targeted VM and should have the limited ring-0
 * presence hack applied.
 *
 * This is typically used in ring-0 code to skip VM init and termination code.
 *
 * @param g_GVM  The ring-0 VM structure. */
# ifdef VM_STRUCT_VERSION_NON_NATIVE_TARGETS
#  define VM_IS_NON_NATIVE_WITH_LIMITED_R0(g_GVM)   (pGVM->enmTarget != VMTARGET_NATIVE)
# else
#  define VM_IS_NON_NATIVE_WITH_LIMITED_R0(g_GVM)   (false)
# endif

#endif
#endif /* !VBOX_FOR_DTRACE_LIB */


/**
 * Helper that HM and NEM uses for safely modifying VM::bMainExecutionEngine.
 *
 * ONLY HM and NEM MAY USE THIS!
 *
 * @param   a_pVM       The cross context VM structure.
 * @param   a_bValue    The new value.
 * @internal
 */
#define VM_SET_MAIN_EXECUTION_ENGINE(a_pVM, a_bValue) \
    do { \
        *const_cast<uint8_t *>(&(a_pVM)->bMainExecutionEngine) = (a_bValue); \
        ASMCompilerBarrier(); /* just to be on the safe side */ \
    } while (0)

/**
 * Checks whether iem-executes-all-mode is used.
 *
 * @retval  true if IEM is used.
 * @retval  false if not.
 *
 * @param   a_pVM       The cross context VM structure.
 * @sa      VM_IS_HM_OR_NEM_ENABLED, VM_IS_HM_ENABLED, VM_IS_NEM_ENABLED.
 * @internal
 */
#define VM_IS_EXEC_ENGINE_IEM(a_pVM)      ((a_pVM)->bMainExecutionEngine == VM_EXEC_ENGINE_IEM)

/**
 * Checks whether HM (VT-x/AMD-V) or NEM is being used by this VM.
 *
 * @retval  true if either is used.
 * @retval  false if software virtualization (raw-mode) is used.
 *
 * @param   a_pVM       The cross context VM structure.
 * @sa      VM_IS_EXEC_ENGINE_IEM, VM_IS_HM_ENABLED, VM_IS_NEM_ENABLED.
 * @internal
 */
#define VM_IS_HM_OR_NEM_ENABLED(a_pVM)      ((a_pVM)->bMainExecutionEngine != VM_EXEC_ENGINE_IEM)

/**
 * Checks whether HM is being used by this VM.
 *
 * @retval  true if HM (VT-x/AMD-v) is used.
 * @retval  false if not.
 *
 * @param   a_pVM       The cross context VM structure.
 * @sa      VM_IS_NEM_ENABLED, VM_IS_EXEC_ENGINE_IEM, VM_IS_HM_OR_NEM_ENABLED.
 * @internal
 */
#define VM_IS_HM_ENABLED(a_pVM)             ((a_pVM)->bMainExecutionEngine == VM_EXEC_ENGINE_HW_VIRT)

/**
 * Checks whether NEM is being used by this VM.
 *
 * @retval  true if a native hypervisor API is used.
 * @retval  false if not.
 *
 * @param   a_pVM       The cross context VM structure.
 * @sa      VM_IS_HM_ENABLED, VM_IS_EXEC_ENGINE_IEM, VM_IS_HM_OR_NEM_ENABLED.
 * @internal
 */
#define VM_IS_NEM_ENABLED(a_pVM)             ((a_pVM)->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API)


/**
 * The cross context VM structure.
 *
 * It contains all the VM data which have to be available in all contexts.
 * Even if it contains all the data the idea is to use APIs not to modify all
 * the members all around the place.  Therefore we make use of unions to hide
 * everything which isn't local to the current source module.  This means we'll
 * have to pay a little bit of attention when adding new members to structures
 * in the unions and make sure to keep the padding sizes up to date.
 *
 * Run 'kmk run-struct-tests' (from src/VBox/VMM if you like) after updating!
 */
typedef struct VM
{
    /** The state of the VM.
     * This field is read only to everyone except the VM and EM. */
    VMSTATE volatile            enmVMState;
    /** Forced action flags.
     * See the VM_FF_* \#defines. Updated atomically.
     */
    volatile uint32_t           fGlobalForcedActions;
    /** Pointer to the array of page descriptors for the VM structure allocation. */
    R3PTRTYPE(PSUPPAGE)         paVMPagesR3;
    /** Session handle. For use when calling SUPR0 APIs. */
#ifdef IN_RING0
    PSUPDRVSESSION              pSessionUnsafe;
#else
    PSUPDRVSESSION              pSession;
#endif
    /** Pointer to the ring-3 VM structure. */
    PUVM                        pUVM;
    /** Ring-3 Host Context VM Pointer. */
#ifdef IN_RING0
    R3PTRTYPE(struct VM *)      pVMR3Unsafe;
#else
    R3PTRTYPE(struct VM *)      pVMR3;
#endif
    /** Ring-0 Host Context VM pointer for making ring-0 calls. */
    R0PTRTYPE(struct VM *)      pVMR0ForCall;
    /** Raw-mode Context VM Pointer. */
    uint32_t                    pVMRC;
    /** Padding for new raw-mode (long mode).   */
    uint32_t                    pVMRCPadding;

    /** The GVM VM handle. Only the GVM should modify this field. */
#ifdef IN_RING0
    uint32_t                    hSelfUnsafe;
#else
    uint32_t                    hSelf;
#endif
    /** Number of virtual CPUs. */
#ifdef IN_RING0
    uint32_t                    cCpusUnsafe;
#else
    uint32_t                    cCpus;
#endif
    /** The VM target platform architecture. */
#ifdef IN_RING0
    VMTARGET                    enmTargetUnsafe;
#else
    VMTARGET                    enmTarget;
#endif
    /** CPU excution cap (1-100) */
    uint32_t                    uCpuExecutionCap;

    /** Size of the VM structure. */
    uint32_t                    cbSelf;
    /** Size of the VMCPU structure. */
    uint32_t                    cbVCpu;
    /** Structure version number (VM_STRUCT_VERSION). */
    uint32_t                    uStructVersion;

    /** @name Various items that are frequently accessed.
     * @{ */
    /** The main execution engine, VM_EXEC_ENGINE_XXX.
     * This is set early during vmR3InitRing3 by HM or NEM.  */
    uint8_t const               bMainExecutionEngine;

    /** Hardware VM support is available and enabled.
     * Determined very early during init.
     * This is placed here for performance reasons.
     * @todo obsoleted by bMainExecutionEngine, eliminate. */
    bool                        fHMEnabled;
    /** @} */

    /** Alignment padding. */
    uint8_t                     uPadding1[2];

    /** @name Debugging
     * @{ */
    /** Ring-3 Host Context VM Pointer. */
    R3PTRTYPE(RTTRACEBUF)       hTraceBufR3;
    /** Ring-0 Host Context VM Pointer. */
    R0PTRTYPE(RTTRACEBUF)       hTraceBufR0;
    /** @} */

    /** Max EMT hash lookup collisions (in GVMM). */
    uint8_t                     cMaxEmtHashCollisions;

    /** Padding - the unions must be aligned on a 64 bytes boundary. */
    uint8_t                     abAlignment3[23];

    /** CPUM part. */
    union
    {
#if defined(VMM_INCLUDED_SRC_include_CPUMInternal_h) || defined(VMM_INCLUDED_SRC_include_CPUMInternal_armv8_h)
        struct CPUM s;
#endif
#ifdef VBOX_INCLUDED_vmm_cpum_h
        /** Read only info exposed about the host and guest CPUs. */
        struct
        {
            /** Guest CPU feature information. */
            CPUMFEATURES            GuestFeatures;
        } const ro;
#endif
        /** @todo this is rather bloated because of static MSR range allocation.
         *        Probably a good idea to move it to a separate R0 allocation... */
        uint8_t     padding[8832 + 128*8192 + 0x1d00]; /* multiple of 64 */
    } cpum;

    /** PGM part.
     * @note Aligned on 16384 boundrary for zero and mmio page storage. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_PGMInternal_h
        struct PGM  s;
#endif
        uint8_t     padding[129728];     /* multiple of 64 */
    } pgm;

    /** VMM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_VMMInternal_h
        struct VMM  s;
#endif
        uint8_t     padding[1600];      /* multiple of 64 */
    } vmm;

    /** HM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_HMInternal_h
        struct HM s;
#endif
        uint8_t     padding[5504];      /* multiple of 64 */
    } hm;

    /** TRPM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_TRPMInternal_h
        struct TRPM s;
#endif
        uint8_t     padding[2048];      /* multiple of 64 */
    } trpm;

    /** SELM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_SELMInternal_h
        struct SELM s;
#endif
        uint8_t     padding[768];       /* multiple of 64 */
    } selm;

    /** MM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_MMInternal_h
        struct MM   s;
#endif
        uint8_t     padding[192];       /* multiple of 64 */
    } mm;

    /** PDM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_PDMInternal_h
        struct PDM s;
#endif
        uint8_t     padding[22784];     /* multiple of 64 */
    } pdm;

    /** IOM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_IOMInternal_h
        struct IOM s;
#endif
        uint8_t     padding[1152];      /* multiple of 64 */
    } iom;

    /** EM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_EMInternal_h
        struct EM   s;
#endif
        uint8_t     padding[256];       /* multiple of 64 */
    } em;

    /** NEM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_NEMInternal_h
        struct NEM  s;
#endif
        uint8_t     padding[4608];       /* multiple of 64 */
    } nem;

    /** TM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_TMInternal_h
        struct TM   s;
#endif
        uint8_t     padding[10112];      /* multiple of 64 */
    } tm;

    /** DBGF part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_DBGFInternal_h
        struct DBGF s;
#endif
#ifdef VBOX_INCLUDED_vmm_dbgf_h
        /** Read only info exposed about interrupt breakpoints and selected events. */
        struct
        {
            /** Bitmap of enabled hardware interrupt breakpoints. */
            uint32_t                    bmHardIntBreakpoints[256 / 32];
            /** Bitmap of enabled software interrupt breakpoints. */
            uint32_t                    bmSoftIntBreakpoints[256 / 32];
            /** Bitmap of selected events.
             * This includes non-selectable events too for simplicity, we maintain the
             * state for some of these, as it may come in handy. */
            uint64_t                    bmSelectedEvents[(DBGFEVENT_END + 63) / 64];
            /** Enabled hardware interrupt breakpoints. */
            uint32_t                    cHardIntBreakpoints;
            /** Enabled software interrupt breakpoints. */
            uint32_t                    cSoftIntBreakpoints;
            /** The number of selected events. */
            uint32_t                    cSelectedEvents;
            /** The number of enabled hardware breakpoints. */
            uint8_t                     cEnabledHwBreakpoints;
            /** The number of enabled hardware I/O breakpoints. */
            uint8_t                     cEnabledHwIoBreakpoints;
            uint8_t                     au8Alignment1[2]; /**< Alignment padding. */
            /** The number of enabled software breakpoints. */
            uint32_t volatile           cEnabledSwBreakpoints;
        } const     ro;
#endif
        uint8_t     padding[2432];      /* multiple of 64 */
    } dbgf;

    /** SSM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_SSMInternal_h
        struct SSM  s;
#endif
        uint8_t     padding[128];       /* multiple of 64 */
    } ssm;

    union
    {
#ifdef VMM_INCLUDED_SRC_include_GIMInternal_h
        struct GIM s;
#endif
        uint8_t     padding[448];       /* multiple of 64 */
    } gim;

    /** Interrupt controller, target specific. */
    RT_GCC_EXTENSION
    union
    {
#if defined(VBOX_VMM_TARGET_ARMV8) || defined(VBOX_VMM_TARGET_AGNOSTIC)
        union
        {
# ifdef VMM_INCLUDED_SRC_include_GICInternal_h
            struct GIC  s;
# endif
            uint8_t     padding[320];   /* multiple of 8 */
        } gic;
#endif
#if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
        union
        {
# if defined(VMM_INCLUDED_SRC_include_APICInternal_h)
            struct APIC s;
# elif defined(VMM_INCLUDED_SRC_include_APICHvInternal_h)
            struct HVAPIC s;
# elif defined(VMM_INCLUDED_SRC_include_APICKvmInternal_h)
            struct KVMAPIC s;
# endif
            uint8_t     padding[320];   /* multiple of 8 */
        } apic;
#endif
    };

    /* ---- begin small stuff ---- */

    /** VM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_VMInternal_h
        struct VMINT s;
#endif
        uint8_t     padding[32];        /* multiple of 8 */
    } vm;

    /** CFGM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_CFGMInternal_h
        struct CFGM s;
#endif
        uint8_t     padding[8];         /* multiple of 8 */
    } cfgm;

    /** IEM part. */
    union
    {
#ifdef VMM_INCLUDED_SRC_include_IEMInternal_h
        struct IEM  s;
#endif
        uint8_t     padding[16];         /* multiple of 8 */
    } iem;

    /** Statistics for ring-0 only components. */
    struct
    {
        /** GMMR0 stats. */
        struct
        {
            /** Chunk TLB hits. */
            uint64_t    cChunkTlbHits;
            /** Chunk TLB misses. */
            uint64_t    cChunkTlbMisses;
        } gmm;
        uint64_t    au64Padding[6];     /* probably more comming here... */
    } R0Stats;

    union
    {
#ifdef VMM_INCLUDED_SRC_include_GCMInternal_h
        struct GCM  s;
#endif
        uint8_t     padding[8];         /* multiple of 8 */
    } gcm;

    /** Padding for aligning the structure size on a page boundrary. */
    uint8_t         abAlignment2[0x3840 - sizeof(PVMCPUR3) * VMM_MAX_CPU_COUNT];

    /* ---- end small stuff ---- */

    /** Array of VMCPU ring-3 pointers. */
    PVMCPUR3        apCpusR3[VMM_MAX_CPU_COUNT];

    /* This point is aligned on a 16384 boundrary (for arm64 purposes). */
} VM;
#ifndef VBOX_FOR_DTRACE_LIB
//AssertCompileSizeAlignment(VM, 16384);
#endif


#ifdef IN_RC
RT_C_DECLS_BEGIN

/** The VM structure.
 * This is imported from the VMMRCBuiltin module, i.e. it's a one of those magic
 * globals which we should avoid using.
 */
extern DECLIMPORT(VM)   g_VM;

/** The VMCPU structure for virtual CPU \#0.
 * This is imported from the VMMRCBuiltin module, i.e. it's a one of those magic
 * globals which we should avoid using.
 */
extern DECLIMPORT(VMCPU) g_VCpu0;

RT_C_DECLS_END
#endif

/** @} */

#endif /* !VBOX_INCLUDED_vmm_vm_h */

