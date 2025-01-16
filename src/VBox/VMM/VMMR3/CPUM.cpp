/* $Id: CPUM.cpp 107898 2025-01-16 10:36:42Z ramshankar.venkataraman@oracle.com $ */
/** @file
 * CPUM - CPU Monitor / Manager.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

/** @page pg_cpum CPUM - CPU Monitor / Manager
 *
 * The CPU Monitor / Manager keeps track of all the CPU registers. It is
 * also responsible for lazy FPU handling and some of the context loading
 * in raw mode.
 *
 * There are three CPU contexts, the most important one is the guest one (GC).
 * When running in raw-mode (RC) there is a special hyper context for the VMM
 * part that floats around inside the guest address space. When running in
 * raw-mode, CPUM also maintains a host context for saving and restoring
 * registers across world switches. This latter is done in cooperation with the
 * world switcher (@see pg_vmm).
 *
 * @see grp_cpum
 *
 * @section sec_cpum_fpu        FPU / SSE / AVX / ++ state.
 *
 * TODO: proper write up, currently just some notes.
 *
 * The ring-0 FPU handling per OS:
 *
 *      - 64-bit Windows uses XMM registers in the kernel as part of the calling
 *        convention (Visual C++ doesn't seem to have a way to disable
 *        generating such code either), so CR0.TS/EM are always zero from what I
 *        can tell.  We are also forced to always load/save the guest XMM0-XMM15
 *        registers when entering/leaving guest context.  Interrupt handlers
 *        using FPU/SSE will offically have call save and restore functions
 *        exported by the kernel, if the really really have to use the state.
 *
 *      - 32-bit windows does lazy FPU handling, I think, probably including
 *        lazying saving.  The Windows Internals book states that it's a bad
 *        idea to use the FPU in kernel space. However, it looks like it will
 *        restore the FPU state of the current thread in case of a kernel \#NM.
 *        Interrupt handlers should be same as for 64-bit.
 *
 *      - Darwin allows taking \#NM in kernel space, restoring current thread's
 *        state if I read the code correctly.  It saves the FPU state of the
 *        outgoing thread, and uses CR0.TS to lazily load the state of the
 *        incoming one.  No idea yet how the FPU is treated by interrupt
 *        handlers, i.e. whether they are allowed to disable the state or
 *        something.
 *
 *      - Linux also allows \#NM in kernel space (don't know since when), and
 *        uses CR0.TS for lazy loading.  Saves outgoing thread's state, lazy
 *        loads the incoming unless configured to agressivly load it.  Interrupt
 *        handlers can ask whether they're allowed to use the FPU, and may
 *        freely trash the state if Linux thinks it has saved the thread's state
 *        already.  This is a problem.
 *
 *      - Solaris will, from what I can tell, panic if it gets an \#NM in kernel
 *        context.  When switching threads, the kernel will save the state of
 *        the outgoing thread and lazy load the incoming one using CR0.TS.
 *        There are a few routines in seeblk.s which uses the SSE unit in ring-0
 *        to do stuff, HAT are among the users.  The routines there will
 *        manually clear CR0.TS and save the XMM registers they use only if
 *        CR0.TS was zero upon entry.  They will skip it when not, because as
 *        mentioned above, the FPU state is saved when switching away from a
 *        thread and CR0.TS set to 1, so when CR0.TS is 1 there is nothing to
 *        preserve.  This is a problem if we restore CR0.TS to 1 after loading
 *        the guest state.
 *
 *      - FreeBSD - no idea yet.
 *
 *      - OS/2 does not allow \#NMs in kernel space IIRC.  Does lazy loading,
 *        possibly also lazy saving.  Interrupts must preserve the CR0.TS+EM &
 *        FPU states.
 *
 * Up to r107425 (2016-05-24) we would only temporarily modify CR0.TS/EM while
 * saving and restoring the host and guest states.  The motivation for this
 * change is that we want to be able to emulate SSE instruction in ring-0 (IEM).
 *
 * Starting with that change, we will leave CR0.TS=EM=0 after saving the host
 * state and only restore it once we've restore the host FPU state. This has the
 * accidental side effect of triggering Solaris to preserve XMM registers in
 * sseblk.s. When CR0 was changed by saving the FPU state, CPUM must now inform
 * the VT-x (HMVMX) code about it as it caches the CR0 value in the VMCS.
 *
 *
 * @section sec_cpum_logging        Logging Level Assignments.
 *
 * Following log level assignments:
 *      - Log6 is used for FPU state management.
 *      - Log7 is used for FPU state actualization.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_CPUM
#define CPUM_WITH_NONCONST_HOST_FEATURES
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/cpumdis.h>
#include <VBox/vmm/cpumctx-v1_6.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/hmvmxinline.h>
#include <VBox/vmm/ssm.h>
#include "CPUMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmcc.h>

#include <VBox/param.h>
#include <VBox/dis.h>
#include <VBox/err.h>
#include <VBox/log.h>
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/cpuset.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/rand.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * This was used in the saved state up to the early life of version 14.
 *
 * It indicates that we may have some out-of-sync hidden segement registers.
 * It is only relevant for raw-mode.
 */
#define CPUM_CHANGED_HIDDEN_SEL_REGS_INVALID    RT_BIT(12)


/** For saved state only: Block injection of non-maskable interrupts to the guest.
 * @note This flag was moved to CPUMCTX::eflags.uBoth in v7.0.4. */
#define CPUM_OLD_VMCPU_FF_BLOCK_NMIS            RT_BIT_64(25)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * What kind of cpu info dump to perform.
 */
typedef enum CPUMDUMPTYPE
{
    CPUMDUMPTYPE_TERSE,
    CPUMDUMPTYPE_DEFAULT,
    CPUMDUMPTYPE_VERBOSE
} CPUMDUMPTYPE;
/** Pointer to a cpu info dump type. */
typedef CPUMDUMPTYPE *PCPUMDUMPTYPE;

/**
 * Map of variable-range MTRRs.
 */
typedef struct CPUMMTRRMAP
{
    /** The index of the next available MTRR. */
    uint8_t     idxMtrr;
    /** The number of usable MTRRs. */
    uint8_t     cMtrrs;
    /** Alignment padding. */
    uint16_t    uAlign;
    /** The number of bytes to map via these MTRRs (not including UC regions). */
    uint64_t    cbToMap;
    /** The number of bytes mapped via these MTRRs (not including UC regions). */
    uint64_t    cbMapped;
    /** The variable-range MTRRs. */
    X86MTRRVAR  aMtrrs[CPUMCTX_MAX_MTRRVAR_COUNT];
} CPUMMTRRMAP;
/** Pointer to a CPUM variable-range MTRR structure. */
typedef CPUMMTRRMAP *PCPUMMTRRMAP;
/** Pointer to a const CPUM variable-range MTRR structure. */
typedef CPUMMTRRMAP const *PCCPUMMTRRMAP;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  cpumR3LiveExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass);
static DECLCALLBACK(int)  cpumR3SaveExec(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)  cpumR3LoadPrep(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)  cpumR3LoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
static DECLCALLBACK(int)  cpumR3LoadDone(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(void) cpumR3InfoAll(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) cpumR3InfoGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) cpumR3InfoGuestHwvirt(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) cpumR3InfoGuestInstr(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) cpumR3InfoHyper(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) cpumR3InfoHost(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
/** Host CPU features. */
DECL_HIDDEN_DATA(CPUHOSTFEATURES) g_CpumHostFeatures;
#endif

/** Saved state field descriptors for CPUMCTX. */
static const SSMFIELD g_aCpumCtxFields[] =
{
    SSMFIELD_ENTRY(         CPUMCTX, rdi),
    SSMFIELD_ENTRY(         CPUMCTX, rsi),
    SSMFIELD_ENTRY(         CPUMCTX, rbp),
    SSMFIELD_ENTRY(         CPUMCTX, rax),
    SSMFIELD_ENTRY(         CPUMCTX, rbx),
    SSMFIELD_ENTRY(         CPUMCTX, rdx),
    SSMFIELD_ENTRY(         CPUMCTX, rcx),
    SSMFIELD_ENTRY(         CPUMCTX, rsp),
    SSMFIELD_ENTRY(         CPUMCTX, rflags),
    SSMFIELD_ENTRY(         CPUMCTX, rip),
    SSMFIELD_ENTRY(         CPUMCTX, r8),
    SSMFIELD_ENTRY(         CPUMCTX, r9),
    SSMFIELD_ENTRY(         CPUMCTX, r10),
    SSMFIELD_ENTRY(         CPUMCTX, r11),
    SSMFIELD_ENTRY(         CPUMCTX, r12),
    SSMFIELD_ENTRY(         CPUMCTX, r13),
    SSMFIELD_ENTRY(         CPUMCTX, r14),
    SSMFIELD_ENTRY(         CPUMCTX, r15),
    SSMFIELD_ENTRY(         CPUMCTX, es.Sel),
    SSMFIELD_ENTRY(         CPUMCTX, es.ValidSel),
    SSMFIELD_ENTRY(         CPUMCTX, es.fFlags),
    SSMFIELD_ENTRY(         CPUMCTX, es.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, es.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, es.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, cs.Sel),
    SSMFIELD_ENTRY(         CPUMCTX, cs.ValidSel),
    SSMFIELD_ENTRY(         CPUMCTX, cs.fFlags),
    SSMFIELD_ENTRY(         CPUMCTX, cs.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, cs.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, cs.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, ss.Sel),
    SSMFIELD_ENTRY(         CPUMCTX, ss.ValidSel),
    SSMFIELD_ENTRY(         CPUMCTX, ss.fFlags),
    SSMFIELD_ENTRY(         CPUMCTX, ss.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, ss.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, ss.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, ds.Sel),
    SSMFIELD_ENTRY(         CPUMCTX, ds.ValidSel),
    SSMFIELD_ENTRY(         CPUMCTX, ds.fFlags),
    SSMFIELD_ENTRY(         CPUMCTX, ds.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, ds.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, ds.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, fs.Sel),
    SSMFIELD_ENTRY(         CPUMCTX, fs.ValidSel),
    SSMFIELD_ENTRY(         CPUMCTX, fs.fFlags),
    SSMFIELD_ENTRY(         CPUMCTX, fs.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, fs.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, fs.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, gs.Sel),
    SSMFIELD_ENTRY(         CPUMCTX, gs.ValidSel),
    SSMFIELD_ENTRY(         CPUMCTX, gs.fFlags),
    SSMFIELD_ENTRY(         CPUMCTX, gs.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, gs.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, gs.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, cr0),
    SSMFIELD_ENTRY(         CPUMCTX, cr2),
    SSMFIELD_ENTRY(         CPUMCTX, cr3),
    SSMFIELD_ENTRY(         CPUMCTX, cr4),
    SSMFIELD_ENTRY(         CPUMCTX, dr[0]),
    SSMFIELD_ENTRY(         CPUMCTX, dr[1]),
    SSMFIELD_ENTRY(         CPUMCTX, dr[2]),
    SSMFIELD_ENTRY(         CPUMCTX, dr[3]),
    SSMFIELD_ENTRY(         CPUMCTX, dr[6]),
    SSMFIELD_ENTRY(         CPUMCTX, dr[7]),
    SSMFIELD_ENTRY(         CPUMCTX, gdtr.cbGdt),
    SSMFIELD_ENTRY(         CPUMCTX, gdtr.pGdt),
    SSMFIELD_ENTRY(         CPUMCTX, idtr.cbIdt),
    SSMFIELD_ENTRY(         CPUMCTX, idtr.pIdt),
    SSMFIELD_ENTRY(         CPUMCTX, SysEnter.cs),
    SSMFIELD_ENTRY(         CPUMCTX, SysEnter.eip),
    SSMFIELD_ENTRY(         CPUMCTX, SysEnter.esp),
    SSMFIELD_ENTRY(         CPUMCTX, msrEFER),
    SSMFIELD_ENTRY(         CPUMCTX, msrSTAR),
    SSMFIELD_ENTRY(         CPUMCTX, msrPAT),
    SSMFIELD_ENTRY(         CPUMCTX, msrLSTAR),
    SSMFIELD_ENTRY(         CPUMCTX, msrCSTAR),
    SSMFIELD_ENTRY(         CPUMCTX, msrSFMASK),
    SSMFIELD_ENTRY(         CPUMCTX, msrKERNELGSBASE),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.Sel),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.ValidSel),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.fFlags),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, tr.Sel),
    SSMFIELD_ENTRY(         CPUMCTX, tr.ValidSel),
    SSMFIELD_ENTRY(         CPUMCTX, tr.fFlags),
    SSMFIELD_ENTRY(         CPUMCTX, tr.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, tr.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, tr.Attr),
    SSMFIELD_ENTRY_VER(     CPUMCTX, aXcr[0],                           CPUM_SAVED_STATE_VERSION_XSAVE),
    SSMFIELD_ENTRY_VER(     CPUMCTX, aXcr[1],                           CPUM_SAVED_STATE_VERSION_XSAVE),
    SSMFIELD_ENTRY_VER(     CPUMCTX, fXStateMask,                       CPUM_SAVED_STATE_VERSION_XSAVE),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for SVM nested hardware-virtualization
 *  Host State. */
static const SSMFIELD g_aSvmHwvirtHostState[] =
{
    SSMFIELD_ENTRY(       SVMHOSTSTATE, uEferMsr),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, uCr0),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, uCr4),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, uCr3),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, uRip),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, uRsp),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, uRax),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, rflags),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, es.Sel),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, es.ValidSel),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, es.fFlags),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, es.u64Base),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, es.u32Limit),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, es.Attr),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, cs.Sel),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, cs.ValidSel),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, cs.fFlags),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, cs.u64Base),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, cs.u32Limit),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, cs.Attr),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ss.Sel),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ss.ValidSel),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ss.fFlags),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ss.u64Base),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ss.u32Limit),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ss.Attr),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ds.Sel),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ds.ValidSel),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ds.fFlags),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ds.u64Base),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ds.u32Limit),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, ds.Attr),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, gdtr.cbGdt),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, gdtr.pGdt),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, idtr.cbIdt),
    SSMFIELD_ENTRY(       SVMHOSTSTATE, idtr.pIdt),
    SSMFIELD_ENTRY_IGNORE(SVMHOSTSTATE, abPadding),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for VMX nested hardware-virtualization
 *  VMCS. */
static const SSMFIELD g_aVmxHwvirtVmcs[] =
{
    SSMFIELD_ENTRY(       VMXVVMCS, u32VmcsRevId),
    SSMFIELD_ENTRY(       VMXVVMCS, enmVmxAbort),
    SSMFIELD_ENTRY(       VMXVVMCS, fVmcsState),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au8Padding0),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u32RestoreProcCtls2,         CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_4),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au32Reserved0),

    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, u16Reserved0),

    SSMFIELD_ENTRY(       VMXVVMCS, u32RoVmInstrError),
    SSMFIELD_ENTRY(       VMXVVMCS, u32RoExitReason),
    SSMFIELD_ENTRY(       VMXVVMCS, u32RoExitIntInfo),
    SSMFIELD_ENTRY(       VMXVVMCS, u32RoExitIntErrCode),
    SSMFIELD_ENTRY(       VMXVVMCS, u32RoIdtVectoringInfo),
    SSMFIELD_ENTRY(       VMXVVMCS, u32RoIdtVectoringErrCode),
    SSMFIELD_ENTRY(       VMXVVMCS, u32RoExitInstrLen),
    SSMFIELD_ENTRY(       VMXVVMCS, u32RoExitInstrInfo),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au32RoReserved2),

    SSMFIELD_ENTRY(       VMXVVMCS, u64RoGuestPhysAddr),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au64Reserved1),

    SSMFIELD_ENTRY(       VMXVVMCS, u64RoExitQual),
    SSMFIELD_ENTRY(       VMXVVMCS, u64RoIoRcx),
    SSMFIELD_ENTRY(       VMXVVMCS, u64RoIoRsi),
    SSMFIELD_ENTRY(       VMXVVMCS, u64RoIoRdi),
    SSMFIELD_ENTRY(       VMXVVMCS, u64RoIoRip),
    SSMFIELD_ENTRY(       VMXVVMCS, u64RoGuestLinearAddr),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au64Reserved5),

    SSMFIELD_ENTRY(       VMXVVMCS, u16Vpid),
    SSMFIELD_ENTRY(       VMXVVMCS, u16PostIntNotifyVector),
    SSMFIELD_ENTRY(       VMXVVMCS, u16EptpIndex),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u16HlatPrefixSize,           CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_3),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au16Reserved0),

    SSMFIELD_ENTRY(       VMXVVMCS, u32PinCtls),
    SSMFIELD_ENTRY(       VMXVVMCS, u32ProcCtls),
    SSMFIELD_ENTRY(       VMXVVMCS, u32XcptBitmap),
    SSMFIELD_ENTRY(       VMXVVMCS, u32XcptPFMask),
    SSMFIELD_ENTRY(       VMXVVMCS, u32XcptPFMatch),
    SSMFIELD_ENTRY(       VMXVVMCS, u32Cr3TargetCount),
    SSMFIELD_ENTRY(       VMXVVMCS, u32ExitCtls),
    SSMFIELD_ENTRY(       VMXVVMCS, u32ExitMsrStoreCount),
    SSMFIELD_ENTRY(       VMXVVMCS, u32ExitMsrLoadCount),
    SSMFIELD_ENTRY(       VMXVVMCS, u32EntryCtls),
    SSMFIELD_ENTRY(       VMXVVMCS, u32EntryMsrLoadCount),
    SSMFIELD_ENTRY(       VMXVVMCS, u32EntryIntInfo),
    SSMFIELD_ENTRY(       VMXVVMCS, u32EntryXcptErrCode),
    SSMFIELD_ENTRY(       VMXVVMCS, u32EntryInstrLen),
    SSMFIELD_ENTRY(       VMXVVMCS, u32TprThreshold),
    SSMFIELD_ENTRY(       VMXVVMCS, u32ProcCtls2),
    SSMFIELD_ENTRY(       VMXVVMCS, u32PleGap),
    SSMFIELD_ENTRY(       VMXVVMCS, u32PleWindow),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au32Reserved1),

    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrIoBitmapA),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrIoBitmapB),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrMsrBitmap),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrExitMsrStore),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrExitMsrLoad),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrEntryMsrLoad),
    SSMFIELD_ENTRY(       VMXVVMCS, u64ExecVmcsPtr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrPml),
    SSMFIELD_ENTRY(       VMXVVMCS, u64TscOffset),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrVirtApic),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrApicAccess),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrPostedIntDesc),
    SSMFIELD_ENTRY(       VMXVVMCS, u64VmFuncCtls),
    SSMFIELD_ENTRY(       VMXVVMCS, u64EptPtr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64EoiExitBitmap0),
    SSMFIELD_ENTRY(       VMXVVMCS, u64EoiExitBitmap1),
    SSMFIELD_ENTRY(       VMXVVMCS, u64EoiExitBitmap2),
    SSMFIELD_ENTRY(       VMXVVMCS, u64EoiExitBitmap3),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrEptpList),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrVmreadBitmap),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrVmwriteBitmap),
    SSMFIELD_ENTRY(       VMXVVMCS, u64AddrXcptVeInfo),
    SSMFIELD_ENTRY(       VMXVVMCS, u64XssExitBitmap),
    SSMFIELD_ENTRY(       VMXVVMCS, u64EnclsExitBitmap),
    SSMFIELD_ENTRY(       VMXVVMCS, u64SppTablePtr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64TscMultiplier),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64ProcCtls3,                CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64EnclvExitBitmap,          CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64PconfigExitBitmap,        CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_3),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64HlatPtr,                  CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_3),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64ExitCtls2,                CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_3),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au64Reserved0),

    SSMFIELD_ENTRY(       VMXVVMCS, u64Cr0Mask),
    SSMFIELD_ENTRY(       VMXVVMCS, u64Cr4Mask),
    SSMFIELD_ENTRY(       VMXVVMCS, u64Cr0ReadShadow),
    SSMFIELD_ENTRY(       VMXVVMCS, u64Cr4ReadShadow),
    SSMFIELD_ENTRY(       VMXVVMCS, u64Cr3Target0),
    SSMFIELD_ENTRY(       VMXVVMCS, u64Cr3Target1),
    SSMFIELD_ENTRY(       VMXVVMCS, u64Cr3Target2),
    SSMFIELD_ENTRY(       VMXVVMCS, u64Cr3Target3),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au64Reserved4),

    SSMFIELD_ENTRY(       VMXVVMCS, HostEs),
    SSMFIELD_ENTRY(       VMXVVMCS, HostCs),
    SSMFIELD_ENTRY(       VMXVVMCS, HostSs),
    SSMFIELD_ENTRY(       VMXVVMCS, HostDs),
    SSMFIELD_ENTRY(       VMXVVMCS, HostFs),
    SSMFIELD_ENTRY(       VMXVVMCS, HostGs),
    SSMFIELD_ENTRY(       VMXVVMCS, HostTr),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au16Reserved2),

    SSMFIELD_ENTRY(       VMXVVMCS, u32HostSysenterCs),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au32Reserved4),

    SSMFIELD_ENTRY(       VMXVVMCS, u64HostPatMsr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostEferMsr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostPerfGlobalCtlMsr),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64HostPkrsMsr,              CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au64Reserved3),

    SSMFIELD_ENTRY(       VMXVVMCS, u64HostCr0),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostCr3),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostCr4),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostFsBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostGsBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostTrBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostGdtrBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostIdtrBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostSysenterEsp),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostSysenterEip),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostRsp),
    SSMFIELD_ENTRY(       VMXVVMCS, u64HostRip),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64HostSCetMsr,              CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64HostSsp,                  CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64HostIntrSspTableAddrMsr,  CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au64Reserved7),

    SSMFIELD_ENTRY(       VMXVVMCS, GuestEs),
    SSMFIELD_ENTRY(       VMXVVMCS, GuestCs),
    SSMFIELD_ENTRY(       VMXVVMCS, GuestSs),
    SSMFIELD_ENTRY(       VMXVVMCS, GuestDs),
    SSMFIELD_ENTRY(       VMXVVMCS, GuestFs),
    SSMFIELD_ENTRY(       VMXVVMCS, GuestGs),
    SSMFIELD_ENTRY(       VMXVVMCS, GuestLdtr),
    SSMFIELD_ENTRY(       VMXVVMCS, GuestTr),
    SSMFIELD_ENTRY(       VMXVVMCS, u16GuestIntStatus),
    SSMFIELD_ENTRY(       VMXVVMCS, u16PmlIndex),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au16Reserved1),

    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestEsLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestCsLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestSsLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestDsLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestFsLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestGsLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestLdtrLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestTrLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestGdtrLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestIdtrLimit),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestEsAttr),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestCsAttr),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestSsAttr),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestDsAttr),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestFsAttr),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestGsAttr),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestLdtrAttr),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestTrAttr),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestIntrState),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestActivityState),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestSmBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u32GuestSysenterCS),
    SSMFIELD_ENTRY(       VMXVVMCS, u32PreemptTimer),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au32Reserved3),

    SSMFIELD_ENTRY(       VMXVVMCS, u64VmcsLinkPtr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestDebugCtlMsr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestPatMsr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestEferMsr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestPerfGlobalCtlMsr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestPdpte0),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestPdpte1),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestPdpte2),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestPdpte3),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestBndcfgsMsr),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestRtitCtlMsr),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64GuestPkrsMsr,             CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au64Reserved2),

    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestCr0),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestCr3),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestCr4),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestEsBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestCsBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestSsBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestDsBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestFsBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestGsBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestLdtrBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestTrBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestGdtrBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestIdtrBase),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestDr7),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestRsp),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestRip),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestRFlags),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestPendingDbgXcpts),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestSysenterEsp),
    SSMFIELD_ENTRY(       VMXVVMCS, u64GuestSysenterEip),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64GuestSCetMsr,             CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64GuestSsp,                 CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_VER(   VMXVVMCS, u64GuestIntrSspTableAddrMsr, CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2),
    SSMFIELD_ENTRY_IGNORE(VMXVVMCS, au64Reserved6),

    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for CPUMCTX. */
static const SSMFIELD g_aCpumX87Fields[] =
{
    SSMFIELD_ENTRY(         X86FXSTATE, FCW),
    SSMFIELD_ENTRY(         X86FXSTATE, FSW),
    SSMFIELD_ENTRY(         X86FXSTATE, FTW),
    SSMFIELD_ENTRY(         X86FXSTATE, FOP),
    SSMFIELD_ENTRY(         X86FXSTATE, FPUIP),
    SSMFIELD_ENTRY(         X86FXSTATE, CS),
    SSMFIELD_ENTRY(         X86FXSTATE, Rsrvd1),
    SSMFIELD_ENTRY(         X86FXSTATE, FPUDP),
    SSMFIELD_ENTRY(         X86FXSTATE, DS),
    SSMFIELD_ENTRY(         X86FXSTATE, Rsrvd2),
    SSMFIELD_ENTRY(         X86FXSTATE, MXCSR),
    SSMFIELD_ENTRY(         X86FXSTATE, MXCSR_MASK),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[0]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[1]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[2]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[3]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[4]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[5]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[6]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[7]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[0]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[1]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[2]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[3]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[4]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[5]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[6]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[7]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[8]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[9]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[10]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[11]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[12]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[13]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[14]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[15]),
    SSMFIELD_ENTRY_VER(     X86FXSTATE, au32RsrvdForSoftware[0],        CPUM_SAVED_STATE_VERSION_XSAVE), /* 32-bit/64-bit hack */
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for X86XSAVEHDR. */
static const SSMFIELD g_aCpumXSaveHdrFields[] =
{
    SSMFIELD_ENTRY(         X86XSAVEHDR,  bmXState),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for X86XSAVEYMMHI. */
static const SSMFIELD g_aCpumYmmHiFields[] =
{
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[0]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[1]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[2]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[3]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[4]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[5]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[6]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[7]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[8]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[9]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[10]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[11]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[12]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[13]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[14]),
    SSMFIELD_ENTRY(         X86XSAVEYMMHI, aYmmHi[15]),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for X86XSAVEBNDREGS. */
static const SSMFIELD g_aCpumBndRegsFields[] =
{
    SSMFIELD_ENTRY(         X86XSAVEBNDREGS, aRegs[0]),
    SSMFIELD_ENTRY(         X86XSAVEBNDREGS, aRegs[1]),
    SSMFIELD_ENTRY(         X86XSAVEBNDREGS, aRegs[2]),
    SSMFIELD_ENTRY(         X86XSAVEBNDREGS, aRegs[3]),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for X86XSAVEBNDCFG. */
static const SSMFIELD g_aCpumBndCfgFields[] =
{
    SSMFIELD_ENTRY(         X86XSAVEBNDCFG, fConfig),
    SSMFIELD_ENTRY(         X86XSAVEBNDCFG, fStatus),
    SSMFIELD_ENTRY_TERM()
};

#if 0 /** @todo */
/** Saved state field descriptors for X86XSAVEOPMASK. */
static const SSMFIELD g_aCpumOpmaskFields[] =
{
    SSMFIELD_ENTRY(         X86XSAVEOPMASK, aKRegs[0]),
    SSMFIELD_ENTRY(         X86XSAVEOPMASK, aKRegs[1]),
    SSMFIELD_ENTRY(         X86XSAVEOPMASK, aKRegs[2]),
    SSMFIELD_ENTRY(         X86XSAVEOPMASK, aKRegs[3]),
    SSMFIELD_ENTRY(         X86XSAVEOPMASK, aKRegs[4]),
    SSMFIELD_ENTRY(         X86XSAVEOPMASK, aKRegs[5]),
    SSMFIELD_ENTRY(         X86XSAVEOPMASK, aKRegs[6]),
    SSMFIELD_ENTRY(         X86XSAVEOPMASK, aKRegs[7]),
    SSMFIELD_ENTRY_TERM()
};
#endif

/** Saved state field descriptors for X86XSAVEZMMHI256. */
static const SSMFIELD g_aCpumZmmHi256Fields[] =
{
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[0]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[1]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[2]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[3]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[4]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[5]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[6]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[7]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[8]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[9]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[10]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[11]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[12]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[13]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[14]),
    SSMFIELD_ENTRY(         X86XSAVEZMMHI256, aHi256Regs[15]),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for X86XSAVEZMM16HI. */
static const SSMFIELD g_aCpumZmm16HiFields[] =
{
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[0]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[1]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[2]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[3]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[4]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[5]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[6]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[7]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[8]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[9]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[10]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[11]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[12]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[13]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[14]),
    SSMFIELD_ENTRY(         X86XSAVEZMM16HI, aRegs[15]),
    SSMFIELD_ENTRY_TERM()
};



/** Saved state field descriptors for CPUMCTX in V4.1 before the hidden selector
 * registeres changed. */
static const SSMFIELD g_aCpumX87FieldsMem[] =
{
    SSMFIELD_ENTRY(         X86FXSTATE, FCW),
    SSMFIELD_ENTRY(         X86FXSTATE, FSW),
    SSMFIELD_ENTRY(         X86FXSTATE, FTW),
    SSMFIELD_ENTRY(         X86FXSTATE, FOP),
    SSMFIELD_ENTRY(         X86FXSTATE, FPUIP),
    SSMFIELD_ENTRY(         X86FXSTATE, CS),
    SSMFIELD_ENTRY(         X86FXSTATE, Rsrvd1),
    SSMFIELD_ENTRY(         X86FXSTATE, FPUDP),
    SSMFIELD_ENTRY(         X86FXSTATE, DS),
    SSMFIELD_ENTRY(         X86FXSTATE, Rsrvd2),
    SSMFIELD_ENTRY(         X86FXSTATE, MXCSR),
    SSMFIELD_ENTRY(         X86FXSTATE, MXCSR_MASK),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[0]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[1]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[2]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[3]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[4]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[5]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[6]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[7]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[0]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[1]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[2]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[3]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[4]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[5]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[6]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[7]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[8]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[9]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[10]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[11]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[12]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[13]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[14]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[15]),
    SSMFIELD_ENTRY_IGNORE(  X86FXSTATE, au32RsrvdRest),
    SSMFIELD_ENTRY_IGNORE(  X86FXSTATE, au32RsrvdForSoftware),
};

/** Saved state field descriptors for CPUMCTX in V4.1 before the hidden selector
 * registeres changed. */
static const SSMFIELD g_aCpumCtxFieldsMem[] =
{
    SSMFIELD_ENTRY(         CPUMCTX, rdi),
    SSMFIELD_ENTRY(         CPUMCTX, rsi),
    SSMFIELD_ENTRY(         CPUMCTX, rbp),
    SSMFIELD_ENTRY(         CPUMCTX, rax),
    SSMFIELD_ENTRY(         CPUMCTX, rbx),
    SSMFIELD_ENTRY(         CPUMCTX, rdx),
    SSMFIELD_ENTRY(         CPUMCTX, rcx),
    SSMFIELD_ENTRY(         CPUMCTX, rsp),
    SSMFIELD_ENTRY_OLD(              lss_esp, sizeof(uint32_t)),
    SSMFIELD_ENTRY(         CPUMCTX, ss.Sel),
    SSMFIELD_ENTRY_OLD(              ssPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(         CPUMCTX, gs.Sel),
    SSMFIELD_ENTRY_OLD(              gsPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(         CPUMCTX, fs.Sel),
    SSMFIELD_ENTRY_OLD(              fsPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(         CPUMCTX, es.Sel),
    SSMFIELD_ENTRY_OLD(              esPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(         CPUMCTX, ds.Sel),
    SSMFIELD_ENTRY_OLD(              dsPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(         CPUMCTX, cs.Sel),
    SSMFIELD_ENTRY_OLD(              csPadding, sizeof(uint16_t)*3),
    SSMFIELD_ENTRY(         CPUMCTX, rflags),
    SSMFIELD_ENTRY(         CPUMCTX, rip),
    SSMFIELD_ENTRY(         CPUMCTX, r8),
    SSMFIELD_ENTRY(         CPUMCTX, r9),
    SSMFIELD_ENTRY(         CPUMCTX, r10),
    SSMFIELD_ENTRY(         CPUMCTX, r11),
    SSMFIELD_ENTRY(         CPUMCTX, r12),
    SSMFIELD_ENTRY(         CPUMCTX, r13),
    SSMFIELD_ENTRY(         CPUMCTX, r14),
    SSMFIELD_ENTRY(         CPUMCTX, r15),
    SSMFIELD_ENTRY(         CPUMCTX, es.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, es.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, es.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, cs.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, cs.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, cs.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, ss.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, ss.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, ss.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, ds.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, ds.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, ds.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, fs.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, fs.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, fs.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, gs.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, gs.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, gs.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, cr0),
    SSMFIELD_ENTRY(         CPUMCTX, cr2),
    SSMFIELD_ENTRY(         CPUMCTX, cr3),
    SSMFIELD_ENTRY(         CPUMCTX, cr4),
    SSMFIELD_ENTRY(         CPUMCTX, dr[0]),
    SSMFIELD_ENTRY(         CPUMCTX, dr[1]),
    SSMFIELD_ENTRY(         CPUMCTX, dr[2]),
    SSMFIELD_ENTRY(         CPUMCTX, dr[3]),
    SSMFIELD_ENTRY_OLD(              dr[4], sizeof(uint64_t)),
    SSMFIELD_ENTRY_OLD(              dr[5], sizeof(uint64_t)),
    SSMFIELD_ENTRY(         CPUMCTX, dr[6]),
    SSMFIELD_ENTRY(         CPUMCTX, dr[7]),
    SSMFIELD_ENTRY(         CPUMCTX, gdtr.cbGdt),
    SSMFIELD_ENTRY(         CPUMCTX, gdtr.pGdt),
    SSMFIELD_ENTRY_OLD(              gdtrPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(         CPUMCTX, idtr.cbIdt),
    SSMFIELD_ENTRY(         CPUMCTX, idtr.pIdt),
    SSMFIELD_ENTRY_OLD(              idtrPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.Sel),
    SSMFIELD_ENTRY_OLD(              ldtrPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(         CPUMCTX, tr.Sel),
    SSMFIELD_ENTRY_OLD(              trPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(         CPUMCTX, SysEnter.cs),
    SSMFIELD_ENTRY(         CPUMCTX, SysEnter.eip),
    SSMFIELD_ENTRY(         CPUMCTX, SysEnter.esp),
    SSMFIELD_ENTRY(         CPUMCTX, msrEFER),
    SSMFIELD_ENTRY(         CPUMCTX, msrSTAR),
    SSMFIELD_ENTRY(         CPUMCTX, msrPAT),
    SSMFIELD_ENTRY(         CPUMCTX, msrLSTAR),
    SSMFIELD_ENTRY(         CPUMCTX, msrCSTAR),
    SSMFIELD_ENTRY(         CPUMCTX, msrSFMASK),
    SSMFIELD_ENTRY(         CPUMCTX, msrKERNELGSBASE),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, ldtr.Attr),
    SSMFIELD_ENTRY(         CPUMCTX, tr.u64Base),
    SSMFIELD_ENTRY(         CPUMCTX, tr.u32Limit),
    SSMFIELD_ENTRY(         CPUMCTX, tr.Attr),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for CPUMCTX_VER1_6. */
static const SSMFIELD g_aCpumX87FieldsV16[] =
{
    SSMFIELD_ENTRY(         X86FXSTATE, FCW),
    SSMFIELD_ENTRY(         X86FXSTATE, FSW),
    SSMFIELD_ENTRY(         X86FXSTATE, FTW),
    SSMFIELD_ENTRY(         X86FXSTATE, FOP),
    SSMFIELD_ENTRY(         X86FXSTATE, FPUIP),
    SSMFIELD_ENTRY(         X86FXSTATE, CS),
    SSMFIELD_ENTRY(         X86FXSTATE, Rsrvd1),
    SSMFIELD_ENTRY(         X86FXSTATE, FPUDP),
    SSMFIELD_ENTRY(         X86FXSTATE, DS),
    SSMFIELD_ENTRY(         X86FXSTATE, Rsrvd2),
    SSMFIELD_ENTRY(         X86FXSTATE, MXCSR),
    SSMFIELD_ENTRY(         X86FXSTATE, MXCSR_MASK),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[0]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[1]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[2]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[3]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[4]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[5]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[6]),
    SSMFIELD_ENTRY(         X86FXSTATE, aRegs[7]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[0]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[1]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[2]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[3]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[4]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[5]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[6]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[7]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[8]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[9]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[10]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[11]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[12]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[13]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[14]),
    SSMFIELD_ENTRY(         X86FXSTATE, aXMM[15]),
    SSMFIELD_ENTRY_IGNORE(  X86FXSTATE, au32RsrvdRest),
    SSMFIELD_ENTRY_IGNORE(  X86FXSTATE, au32RsrvdForSoftware),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for CPUMCTX_VER1_6. */
static const SSMFIELD g_aCpumCtxFieldsV16[] =
{
    SSMFIELD_ENTRY(             CPUMCTX, rdi),
    SSMFIELD_ENTRY(             CPUMCTX, rsi),
    SSMFIELD_ENTRY(             CPUMCTX, rbp),
    SSMFIELD_ENTRY(             CPUMCTX, rax),
    SSMFIELD_ENTRY(             CPUMCTX, rbx),
    SSMFIELD_ENTRY(             CPUMCTX, rdx),
    SSMFIELD_ENTRY(             CPUMCTX, rcx),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, rsp),
    SSMFIELD_ENTRY(             CPUMCTX, ss.Sel),
    SSMFIELD_ENTRY_OLD(                  ssPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY_OLD(         CPUMCTX, sizeof(uint64_t) /*rsp_notused*/),
    SSMFIELD_ENTRY(             CPUMCTX, gs.Sel),
    SSMFIELD_ENTRY_OLD(                  gsPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(             CPUMCTX, fs.Sel),
    SSMFIELD_ENTRY_OLD(                  fsPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(             CPUMCTX, es.Sel),
    SSMFIELD_ENTRY_OLD(                  esPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(             CPUMCTX, ds.Sel),
    SSMFIELD_ENTRY_OLD(                  dsPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(             CPUMCTX, cs.Sel),
    SSMFIELD_ENTRY_OLD(                  csPadding, sizeof(uint16_t)*3),
    SSMFIELD_ENTRY(             CPUMCTX, rflags),
    SSMFIELD_ENTRY(             CPUMCTX, rip),
    SSMFIELD_ENTRY(             CPUMCTX, r8),
    SSMFIELD_ENTRY(             CPUMCTX, r9),
    SSMFIELD_ENTRY(             CPUMCTX, r10),
    SSMFIELD_ENTRY(             CPUMCTX, r11),
    SSMFIELD_ENTRY(             CPUMCTX, r12),
    SSMFIELD_ENTRY(             CPUMCTX, r13),
    SSMFIELD_ENTRY(             CPUMCTX, r14),
    SSMFIELD_ENTRY(             CPUMCTX, r15),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, es.u64Base),
    SSMFIELD_ENTRY(             CPUMCTX, es.u32Limit),
    SSMFIELD_ENTRY(             CPUMCTX, es.Attr),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, cs.u64Base),
    SSMFIELD_ENTRY(             CPUMCTX, cs.u32Limit),
    SSMFIELD_ENTRY(             CPUMCTX, cs.Attr),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, ss.u64Base),
    SSMFIELD_ENTRY(             CPUMCTX, ss.u32Limit),
    SSMFIELD_ENTRY(             CPUMCTX, ss.Attr),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, ds.u64Base),
    SSMFIELD_ENTRY(             CPUMCTX, ds.u32Limit),
    SSMFIELD_ENTRY(             CPUMCTX, ds.Attr),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, fs.u64Base),
    SSMFIELD_ENTRY(             CPUMCTX, fs.u32Limit),
    SSMFIELD_ENTRY(             CPUMCTX, fs.Attr),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, gs.u64Base),
    SSMFIELD_ENTRY(             CPUMCTX, gs.u32Limit),
    SSMFIELD_ENTRY(             CPUMCTX, gs.Attr),
    SSMFIELD_ENTRY(             CPUMCTX, cr0),
    SSMFIELD_ENTRY(             CPUMCTX, cr2),
    SSMFIELD_ENTRY(             CPUMCTX, cr3),
    SSMFIELD_ENTRY(             CPUMCTX, cr4),
    SSMFIELD_ENTRY_OLD(                  cr8, sizeof(uint64_t)),
    SSMFIELD_ENTRY(             CPUMCTX, dr[0]),
    SSMFIELD_ENTRY(             CPUMCTX, dr[1]),
    SSMFIELD_ENTRY(             CPUMCTX, dr[2]),
    SSMFIELD_ENTRY(             CPUMCTX, dr[3]),
    SSMFIELD_ENTRY_OLD(                  dr[4], sizeof(uint64_t)),
    SSMFIELD_ENTRY_OLD(                  dr[5], sizeof(uint64_t)),
    SSMFIELD_ENTRY(             CPUMCTX, dr[6]),
    SSMFIELD_ENTRY(             CPUMCTX, dr[7]),
    SSMFIELD_ENTRY(             CPUMCTX, gdtr.cbGdt),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, gdtr.pGdt),
    SSMFIELD_ENTRY_OLD(                  gdtrPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY_OLD(                  gdtrPadding64, sizeof(uint64_t)),
    SSMFIELD_ENTRY(             CPUMCTX, idtr.cbIdt),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, idtr.pIdt),
    SSMFIELD_ENTRY_OLD(                  idtrPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY_OLD(                  idtrPadding64, sizeof(uint64_t)),
    SSMFIELD_ENTRY(             CPUMCTX, ldtr.Sel),
    SSMFIELD_ENTRY_OLD(                  ldtrPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(             CPUMCTX, tr.Sel),
    SSMFIELD_ENTRY_OLD(                  trPadding, sizeof(uint16_t)),
    SSMFIELD_ENTRY(             CPUMCTX, SysEnter.cs),
    SSMFIELD_ENTRY(             CPUMCTX, SysEnter.eip),
    SSMFIELD_ENTRY(             CPUMCTX, SysEnter.esp),
    SSMFIELD_ENTRY(             CPUMCTX, msrEFER),
    SSMFIELD_ENTRY(             CPUMCTX, msrSTAR),
    SSMFIELD_ENTRY(             CPUMCTX, msrPAT),
    SSMFIELD_ENTRY(             CPUMCTX, msrLSTAR),
    SSMFIELD_ENTRY(             CPUMCTX, msrCSTAR),
    SSMFIELD_ENTRY(             CPUMCTX, msrSFMASK),
    SSMFIELD_ENTRY_OLD(                  msrFSBASE, sizeof(uint64_t)),
    SSMFIELD_ENTRY_OLD(                  msrGSBASE, sizeof(uint64_t)),
    SSMFIELD_ENTRY(             CPUMCTX, msrKERNELGSBASE),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, ldtr.u64Base),
    SSMFIELD_ENTRY(             CPUMCTX, ldtr.u32Limit),
    SSMFIELD_ENTRY(             CPUMCTX, ldtr.Attr),
    SSMFIELD_ENTRY_U32_ZX_U64(  CPUMCTX, tr.u64Base),
    SSMFIELD_ENTRY(             CPUMCTX, tr.u32Limit),
    SSMFIELD_ENTRY(             CPUMCTX, tr.Attr),
    SSMFIELD_ENTRY_OLD(                  padding, sizeof(uint32_t)*2),
    SSMFIELD_ENTRY_TERM()
};


#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
/**
 * Checks for partial/leaky FXSAVE/FXRSTOR handling on AMD CPUs.
 *
 * AMD K7, K8 and newer AMD CPUs do not save/restore the x87 error pointers
 * (last instruction pointer, last data pointer, last opcode) except when the ES
 * bit (Exception Summary) in x87 FSW (FPU Status Word) is set. Thus if we don't
 * clear these registers there is potential, local FPU leakage from a process
 * using the FPU to another.
 *
 * See AMD Instruction Reference for FXSAVE, FXRSTOR.
 *
 * @param   pVM     The cross context VM structure.
 */
static void cpumR3CheckLeakyFpu(PVM pVM)
{
    uint32_t u32CpuVersion = ASMCpuId_EAX(1);
    uint32_t const u32Family = u32CpuVersion >> 8;
    if (   u32Family >= 6      /* K7 and higher */
        && (ASMIsAmdCpu() || ASMIsHygonCpu()) )
    {
        uint32_t cExt = ASMCpuId_EAX(0x80000000);
        if (RTX86IsValidExtRange(cExt))
        {
            uint32_t fExtFeaturesEDX = ASMCpuId_EDX(0x80000001);
            if (fExtFeaturesEDX & X86_CPUID_AMD_FEATURE_EDX_FFXSR)
            {
                for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                {
                    PVMCPU pVCpu = pVM->apCpusR3[idCpu];
                    pVCpu->cpum.s.fUseFlags |= CPUM_USE_FFXSR_LEAKY;
                }
                Log(("CPUM: Host CPU has leaky fxsave/fxrstor behaviour\n"));
            }
        }
    }
}
#endif


/**
 * Initialize the SVM hardware virtualization state.
 *
 * @param   pVM     The cross context VM structure.
 */
static void cpumR3InitSvmHwVirtState(PVM pVM)
{
    LogRel(("CPUM: AMD-V nested-guest init\n"));
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];
        PCPUMCTX pCtx  = &pVCpu->cpum.s.Guest;

        /* Initialize that SVM hardware virtualization is available. */
        pCtx->hwvirt.enmHwvirt = CPUMHWVIRT_SVM;

        AssertCompile(sizeof(pCtx->hwvirt.svm.Vmcb) == SVM_VMCB_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.svm.abMsrBitmap) == SVM_MSRPM_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.svm.abIoBitmap) == SVM_IOPM_PAGES * X86_PAGE_SIZE);

        /* Initialize non-zero values. */
        pCtx->hwvirt.svm.GCPhysVmcb = NIL_RTGCPHYS;
    }
}


/**
 * Resets per-VCPU SVM hardware virtualization state.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECLINLINE(void) cpumR3ResetSvmHwVirtState(PVMCPU pVCpu)
{
    PCPUMCTX pCtx = &pVCpu->cpum.s.Guest;
    Assert(pCtx->hwvirt.enmHwvirt == CPUMHWVIRT_SVM);

    RT_ZERO(pCtx->hwvirt.svm.Vmcb);
    RT_ZERO(pCtx->hwvirt.svm.HostState);
    RT_ZERO(pCtx->hwvirt.svm.abMsrBitmap);
    RT_ZERO(pCtx->hwvirt.svm.abIoBitmap);

    pCtx->hwvirt.svm.uMsrHSavePa           = 0;
    pCtx->hwvirt.svm.uPrevPauseTick        = 0;
    pCtx->hwvirt.svm.GCPhysVmcb            = NIL_RTGCPHYS;
    pCtx->hwvirt.svm.cPauseFilter          = 0;
    pCtx->hwvirt.svm.cPauseFilterThreshold = 0;
    pCtx->hwvirt.svm.fInterceptEvents      = false;
}


/**
 * Initializes the VMX hardware virtualization state.
 *
 * @param   pVM     The cross context VM structure.
 */
static void cpumR3InitVmxHwVirtState(PVM pVM)
{
    LogRel(("CPUM: VT-x nested-guest init\n"));
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU   pVCpu = pVM->apCpusR3[i];
        PCPUMCTX pCtx  = &pVCpu->cpum.s.Guest;

        /* Initialize that VMX hardware virtualization is available. */
        pCtx->hwvirt.enmHwvirt = CPUMHWVIRT_VMX;

        AssertCompile(sizeof(pCtx->hwvirt.vmx.Vmcs) == VMX_V_VMCS_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.Vmcs) == VMX_V_VMCS_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.ShadowVmcs) == VMX_V_SHADOW_VMCS_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.ShadowVmcs) == VMX_V_SHADOW_VMCS_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.abVmreadBitmap) == VMX_V_VMREAD_VMWRITE_BITMAP_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.abVmreadBitmap) == VMX_V_VMREAD_VMWRITE_BITMAP_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.abVmwriteBitmap) == VMX_V_VMREAD_VMWRITE_BITMAP_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.abVmwriteBitmap) == VMX_V_VMREAD_VMWRITE_BITMAP_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.aEntryMsrLoadArea) == VMX_V_AUTOMSR_AREA_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.aEntryMsrLoadArea) == VMX_V_AUTOMSR_AREA_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.aExitMsrStoreArea) == VMX_V_AUTOMSR_AREA_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.aExitMsrStoreArea) == VMX_V_AUTOMSR_AREA_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.aExitMsrLoadArea) == VMX_V_AUTOMSR_AREA_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.aExitMsrLoadArea) == VMX_V_AUTOMSR_AREA_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.abMsrBitmap) == VMX_V_MSR_BITMAP_PAGES * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.abMsrBitmap) == VMX_V_MSR_BITMAP_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.abIoBitmap) == (VMX_V_IO_BITMAP_A_PAGES + VMX_V_IO_BITMAP_B_PAGES) * X86_PAGE_SIZE);
        AssertCompile(sizeof(pCtx->hwvirt.vmx.abIoBitmap) == VMX_V_IO_BITMAP_A_SIZE + VMX_V_IO_BITMAP_B_SIZE);

        /* Initialize non-zero values. */
        pCtx->hwvirt.vmx.GCPhysVmxon       = NIL_RTGCPHYS;
        pCtx->hwvirt.vmx.GCPhysShadowVmcs  = NIL_RTGCPHYS;
        pCtx->hwvirt.vmx.GCPhysVmcs        = NIL_RTGCPHYS;
    }
}


/**
 * Resets per-VCPU VMX hardware virtualization state.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECLINLINE(void) cpumR3ResetVmxHwVirtState(PVMCPU pVCpu)
{
    PCPUMCTX pCtx = &pVCpu->cpum.s.Guest;
    Assert(pCtx->hwvirt.enmHwvirt == CPUMHWVIRT_VMX);

    RT_ZERO(pCtx->hwvirt.vmx.Vmcs);
    RT_ZERO(pCtx->hwvirt.vmx.ShadowVmcs);
    RT_ZERO(pCtx->hwvirt.vmx.abVmreadBitmap);
    RT_ZERO(pCtx->hwvirt.vmx.abVmwriteBitmap);
    RT_ZERO(pCtx->hwvirt.vmx.aEntryMsrLoadArea);
    RT_ZERO(pCtx->hwvirt.vmx.aExitMsrStoreArea);
    RT_ZERO(pCtx->hwvirt.vmx.aExitMsrLoadArea);
    RT_ZERO(pCtx->hwvirt.vmx.abMsrBitmap);
    RT_ZERO(pCtx->hwvirt.vmx.abIoBitmap);

    pCtx->hwvirt.vmx.GCPhysVmxon       = NIL_RTGCPHYS;
    pCtx->hwvirt.vmx.GCPhysShadowVmcs  = NIL_RTGCPHYS;
    pCtx->hwvirt.vmx.GCPhysVmcs        = NIL_RTGCPHYS;
    pCtx->hwvirt.vmx.fInVmxRootMode    = false;
    pCtx->hwvirt.vmx.fInVmxNonRootMode = false;
    /* Don't reset diagnostics here. */

    pCtx->hwvirt.vmx.fInterceptEvents    = false;
    pCtx->hwvirt.vmx.fNmiUnblockingIret  = false;
    pCtx->hwvirt.vmx.uFirstPauseLoopTick = 0;
    pCtx->hwvirt.vmx.uPrevPauseTick      = 0;
    pCtx->hwvirt.vmx.uEntryTick          = 0;
    pCtx->hwvirt.vmx.offVirtApicWrite    = 0;
    pCtx->hwvirt.vmx.fVirtNmiBlocking    = false;

    /* Stop any VMX-preemption timer. */
    CPUMStopGuestVmxPremptTimer(pVCpu);

    /* Clear all nested-guest FFs. */
    VMCPU_FF_CLEAR_MASK(pVCpu, VMCPU_FF_VMX_ALL_MASK);
}


/**
 * Displays the host and guest VMX features.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     "terse", "default" or "verbose".
 */
static DECLCALLBACK(void) cpumR3InfoVmxFeatures(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PCCPUMFEATURES pHostFeatures  = &pVM->cpum.s.HostFeatures;
    PCCPUMFEATURES pGuestFeatures = &pVM->cpum.s.GuestFeatures;
    if (   pHostFeatures->enmCpuVendor == CPUMCPUVENDOR_INTEL
        || pHostFeatures->enmCpuVendor == CPUMCPUVENDOR_VIA
        || pHostFeatures->enmCpuVendor == CPUMCPUVENDOR_SHANGHAI)
    {
#define VMXFEATDUMP(a_szDesc, a_Var) \
        pHlp->pfnPrintf(pHlp, "  %s = %u (%u)\n", a_szDesc, pGuestFeatures->a_Var, pHostFeatures->a_Var)

        pHlp->pfnPrintf(pHlp, "Nested hardware virtualization - VMX features\n");
        pHlp->pfnPrintf(pHlp, "  Mnemonic - Description                                  = guest (host)\n");
        VMXFEATDUMP("VMX - Virtual-Machine Extensions                       ", fVmx);
        /* Basic. */
        VMXFEATDUMP("InsOutInfo - INS/OUTS instruction info.                ", fVmxInsOutInfo);

        /* Pin-based controls. */
        VMXFEATDUMP("ExtIntExit - External interrupt exiting                ", fVmxExtIntExit);
        VMXFEATDUMP("NmiExit - NMI exiting                                  ", fVmxNmiExit);
        VMXFEATDUMP("VirtNmi - Virtual NMIs                                 ", fVmxVirtNmi);
        VMXFEATDUMP("PreemptTimer - VMX preemption timer                    ", fVmxPreemptTimer);
        VMXFEATDUMP("PostedInt - Posted interrupts                          ", fVmxPostedInt);

        /* Processor-based controls. */
        VMXFEATDUMP("IntWindowExit - Interrupt-window exiting               ", fVmxIntWindowExit);
        VMXFEATDUMP("TscOffsetting - TSC offsetting                         ", fVmxTscOffsetting);
        VMXFEATDUMP("HltExit - HLT exiting                                  ", fVmxHltExit);
        VMXFEATDUMP("InvlpgExit - INVLPG exiting                            ", fVmxInvlpgExit);
        VMXFEATDUMP("MwaitExit - MWAIT exiting                              ", fVmxMwaitExit);
        VMXFEATDUMP("RdpmcExit - RDPMC exiting                              ", fVmxRdpmcExit);
        VMXFEATDUMP("RdtscExit - RDTSC exiting                              ", fVmxRdtscExit);
        VMXFEATDUMP("Cr3LoadExit - CR3-load exiting                         ", fVmxCr3LoadExit);
        VMXFEATDUMP("Cr3StoreExit - CR3-store exiting                       ", fVmxCr3StoreExit);
        VMXFEATDUMP("TertiaryExecCtls - Activate tertiary controls          ", fVmxTertiaryExecCtls);
        VMXFEATDUMP("Cr8LoadExit  - CR8-load exiting                        ", fVmxCr8LoadExit);
        VMXFEATDUMP("Cr8StoreExit - CR8-store exiting                       ", fVmxCr8StoreExit);
        VMXFEATDUMP("UseTprShadow - Use TPR shadow                          ", fVmxUseTprShadow);
        VMXFEATDUMP("NmiWindowExit - NMI-window exiting                     ", fVmxNmiWindowExit);
        VMXFEATDUMP("MovDRxExit - Mov-DR exiting                            ", fVmxMovDRxExit);
        VMXFEATDUMP("UncondIoExit - Unconditional I/O exiting               ", fVmxUncondIoExit);
        VMXFEATDUMP("UseIoBitmaps - Use I/O bitmaps                         ", fVmxUseIoBitmaps);
        VMXFEATDUMP("MonitorTrapFlag - Monitor Trap Flag                    ", fVmxMonitorTrapFlag);
        VMXFEATDUMP("UseMsrBitmaps - MSR bitmaps                            ", fVmxUseMsrBitmaps);
        VMXFEATDUMP("MonitorExit - MONITOR exiting                          ", fVmxMonitorExit);
        VMXFEATDUMP("PauseExit - PAUSE exiting                              ", fVmxPauseExit);
        VMXFEATDUMP("SecondaryExecCtl - Activate secondary controls         ", fVmxSecondaryExecCtls);

        /* Secondary processor-based controls. */
        VMXFEATDUMP("VirtApic - Virtualize-APIC accesses                    ", fVmxVirtApicAccess);
        VMXFEATDUMP("Ept - Extended Page Tables                             ", fVmxEpt);
        VMXFEATDUMP("DescTableExit - Descriptor-table exiting               ", fVmxDescTableExit);
        VMXFEATDUMP("Rdtscp - Enable RDTSCP                                 ", fVmxRdtscp);
        VMXFEATDUMP("VirtX2ApicMode - Virtualize-x2APIC mode                ", fVmxVirtX2ApicMode);
        VMXFEATDUMP("Vpid - Enable VPID                                     ", fVmxVpid);
        VMXFEATDUMP("WbinvdExit - WBINVD exiting                            ", fVmxWbinvdExit);
        VMXFEATDUMP("UnrestrictedGuest - Unrestricted guest                 ", fVmxUnrestrictedGuest);
        VMXFEATDUMP("ApicRegVirt - APIC-register virtualization             ", fVmxApicRegVirt);
        VMXFEATDUMP("VirtIntDelivery - Virtual-interrupt delivery           ", fVmxVirtIntDelivery);
        VMXFEATDUMP("PauseLoopExit - PAUSE-loop exiting                     ", fVmxPauseLoopExit);
        VMXFEATDUMP("RdrandExit - RDRAND exiting                            ", fVmxRdrandExit);
        VMXFEATDUMP("Invpcid - Enable INVPCID                               ", fVmxInvpcid);
        VMXFEATDUMP("VmFuncs - Enable VM Functions                          ", fVmxVmFunc);
        VMXFEATDUMP("VmcsShadowing - VMCS shadowing                         ", fVmxVmcsShadowing);
        VMXFEATDUMP("RdseedExiting - RDSEED exiting                         ", fVmxRdseedExit);
        VMXFEATDUMP("PML - Page-Modification Log                            ", fVmxPml);
        VMXFEATDUMP("EptVe - EPT violations can cause #VE                   ", fVmxEptXcptVe);
        VMXFEATDUMP("ConcealVmxFromPt - Conceal VMX from Processor Trace    ", fVmxConcealVmxFromPt);
        VMXFEATDUMP("XsavesXRstors - Enable XSAVES/XRSTORS                  ", fVmxXsavesXrstors);
        VMXFEATDUMP("PasidTranslate - PASID translation                     ", fVmxPasidTranslate);
        VMXFEATDUMP("ModeBasedExecuteEpt - Mode-based execute permissions   ", fVmxModeBasedExecuteEpt);
        VMXFEATDUMP("SppEpt - Sub-page page write permissions for EPT       ", fVmxSppEpt);
        VMXFEATDUMP("PtEpt - Processor Trace address' translatable by EPT   ", fVmxPtEpt);
        VMXFEATDUMP("UseTscScaling - Use TSC scaling                        ", fVmxUseTscScaling);
        VMXFEATDUMP("UserWaitPause - Enable TPAUSE, UMONITOR and UMWAIT     ", fVmxUserWaitPause);
        VMXFEATDUMP("Pconfig - Enable PCONFIG                               ", fVmxPconfig);
        VMXFEATDUMP("EnclvExit - ENCLV exiting                              ", fVmxEnclvExit);
        VMXFEATDUMP("BusLockDetect - VMM Bus-Lock detection                 ", fVmxBusLockDetect);
        VMXFEATDUMP("InstrTimeout - Instruction timeout                     ", fVmxInstrTimeout);

        /* Tertiary processor-based controls. */
        VMXFEATDUMP("LoadIwKeyExit - LOADIWKEY exiting                      ", fVmxLoadIwKeyExit);
        VMXFEATDUMP("HLAT - Hypervisor-managed linear-address translation   ", fVmxHlat);
        VMXFEATDUMP("EptPagingWrite - EPT paging-write                      ", fVmxEptPagingWrite);
        VMXFEATDUMP("GstPagingVerify - Guest-paging verification            ", fVmxGstPagingVerify);
        VMXFEATDUMP("IpiVirt - IPI virtualization                           ", fVmxIpiVirt);
        VMXFEATDUMP("VirtSpecCtrl - Virtualize IA32_SPEC_CTRL               ", fVmxVirtSpecCtrl);

        /* VM-entry controls. */
        VMXFEATDUMP("EntryLoadDebugCtls - Load debug controls on VM-entry   ", fVmxEntryLoadDebugCtls);
        VMXFEATDUMP("Ia32eModeGuest - IA-32e mode guest                     ", fVmxIa32eModeGuest);
        VMXFEATDUMP("EntryLoadEferMsr - Load IA32_EFER MSR on VM-entry      ", fVmxEntryLoadEferMsr);
        VMXFEATDUMP("EntryLoadPatMsr - Load IA32_PAT MSR on VM-entry        ", fVmxEntryLoadPatMsr);

        /* VM-exit controls. */
        VMXFEATDUMP("ExitSaveDebugCtls - Save debug controls on VM-exit     ", fVmxExitSaveDebugCtls);
        VMXFEATDUMP("HostAddrSpaceSize - Host address-space size            ", fVmxHostAddrSpaceSize);
        VMXFEATDUMP("ExitAckExtInt - Acknowledge interrupt on VM-exit       ", fVmxExitAckExtInt);
        VMXFEATDUMP("ExitSavePatMsr - Save IA32_PAT MSR on VM-exit          ", fVmxExitSavePatMsr);
        VMXFEATDUMP("ExitLoadPatMsr - Load IA32_PAT MSR on VM-exit          ", fVmxExitLoadPatMsr);
        VMXFEATDUMP("ExitSaveEferMsr - Save IA32_EFER MSR on VM-exit        ", fVmxExitSaveEferMsr);
        VMXFEATDUMP("ExitLoadEferMsr - Load IA32_EFER MSR on VM-exit        ", fVmxExitLoadEferMsr);
        VMXFEATDUMP("SavePreemptTimer - Save VMX-preemption timer           ", fVmxSavePreemptTimer);
        VMXFEATDUMP("SecondaryExitCtls - Secondary VM-exit controls         ", fVmxSecondaryExitCtls);

        /* Miscellaneous data. */
        VMXFEATDUMP("ExitSaveEferLma - Save IA32_EFER.LMA on VM-exit        ", fVmxExitSaveEferLma);
        VMXFEATDUMP("IntelPt - Intel Processor Trace in VMX operation       ", fVmxPt);
        VMXFEATDUMP("VmwriteAll - VMWRITE to any supported VMCS field       ", fVmxVmwriteAll);
        VMXFEATDUMP("EntryInjectSoftInt - Inject softint. with 0-len instr. ", fVmxEntryInjectSoftInt);
#undef VMXFEATDUMP
    }
    else
        pHlp->pfnPrintf(pHlp, "No VMX features present - requires an Intel or compatible CPU.\n");
}


/**
 * Checks whether nested-guest execution using hardware-assisted VMX (e.g, using HM
 * or NEM) is allowed.
 *
 * @returns @c true if hardware-assisted nested-guest execution is allowed, @c false
 *          otherwise.
 * @param   pVM     The cross context VM structure.
 */
static bool cpumR3IsHwAssistNstGstExecAllowed(PVM pVM)
{
    AssertMsg(pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NOT_SET, ("Calling this function too early!\n"));
#ifndef VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM
    if (   pVM->bMainExecutionEngine == VM_EXEC_ENGINE_HW_VIRT
        || pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API)
        return true;
#else
    NOREF(pVM);
#endif
    return false;
}


/**
 * Initializes the VMX guest MSRs from guest CPU features based on the host MSRs.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pHostVmxMsrs    The host VMX MSRs. Pass NULL when fully emulating VMX
 *                          and no hardware-assisted nested-guest execution is
 *                          possible for this VM.
 * @param   pGuestFeatures  The guest features to use (only VMX features are
 *                          accessed).
 * @param   pGuestVmxMsrs   Where to store the initialized guest VMX MSRs.
 *
 * @remarks This function ASSUMES the VMX guest-features are already exploded!
 */
static void cpumR3InitVmxGuestMsrs(PVM pVM, PCVMXMSRS pHostVmxMsrs, PCCPUMFEATURES pGuestFeatures, PVMXMSRS pGuestVmxMsrs)
{
    bool const fIsNstGstHwExecAllowed = cpumR3IsHwAssistNstGstExecAllowed(pVM);

    Assert(!fIsNstGstHwExecAllowed || pHostVmxMsrs);
    Assert(pGuestFeatures->fVmx);

    /* Basic information. */
    uint8_t const fTrueVmxMsrs = 1;
    {
        uint64_t const u64Basic = RT_BF_MAKE(VMX_BF_BASIC_VMCS_ID,         VMX_V_VMCS_REVISION_ID        )
                                | RT_BF_MAKE(VMX_BF_BASIC_VMCS_SIZE,       VMX_V_VMCS_SIZE               )
                                | RT_BF_MAKE(VMX_BF_BASIC_PHYSADDR_WIDTH,  !pGuestFeatures->fLongMode    )
                                | RT_BF_MAKE(VMX_BF_BASIC_DUAL_MON,        0                             )
                                | RT_BF_MAKE(VMX_BF_BASIC_VMCS_MEM_TYPE,   VMX_BASIC_MEM_TYPE_WB         )
                                | RT_BF_MAKE(VMX_BF_BASIC_VMCS_INS_OUTS,   pGuestFeatures->fVmxInsOutInfo)
                                | RT_BF_MAKE(VMX_BF_BASIC_TRUE_CTLS,       fTrueVmxMsrs                  );
        pGuestVmxMsrs->u64Basic = u64Basic;
    }

    /* Pin-based VM-execution controls. */
    {
        uint32_t const fFeatures = (pGuestFeatures->fVmxExtIntExit   << VMX_BF_PIN_CTLS_EXT_INT_EXIT_SHIFT )
                                 | (pGuestFeatures->fVmxNmiExit      << VMX_BF_PIN_CTLS_NMI_EXIT_SHIFT     )
                                 | (pGuestFeatures->fVmxVirtNmi      << VMX_BF_PIN_CTLS_VIRT_NMI_SHIFT     )
                                 | (pGuestFeatures->fVmxPreemptTimer << VMX_BF_PIN_CTLS_PREEMPT_TIMER_SHIFT)
                                 | (pGuestFeatures->fVmxPostedInt    << VMX_BF_PIN_CTLS_POSTED_INT_SHIFT   );
        uint32_t const fAllowed0 = VMX_PIN_CTLS_DEFAULT1;
        uint32_t const fAllowed1 = fFeatures | VMX_PIN_CTLS_DEFAULT1;
        AssertMsg((fAllowed0 & fAllowed1) == fAllowed0, ("fAllowed0=%#RX32 fAllowed1=%#RX32 fFeatures=%#RX32\n",
                                                         fAllowed0, fAllowed1, fFeatures));
        pGuestVmxMsrs->PinCtls.u = RT_MAKE_U64(fAllowed0, fAllowed1);

        /* True pin-based VM-execution controls. */
        if (fTrueVmxMsrs)
        {
            /* VMX_PIN_CTLS_DEFAULT1 contains MB1 reserved bits and must be reserved MB1 in true pin-based controls as well. */
            pGuestVmxMsrs->TruePinCtls.u = pGuestVmxMsrs->PinCtls.u;
        }
    }

    /* Processor-based VM-execution controls. */
    {
        uint32_t const fFeatures = (pGuestFeatures->fVmxIntWindowExit     << VMX_BF_PROC_CTLS_INT_WINDOW_EXIT_SHIFT   )
                                 | (pGuestFeatures->fVmxTscOffsetting     << VMX_BF_PROC_CTLS_USE_TSC_OFFSETTING_SHIFT)
                                 | (pGuestFeatures->fVmxHltExit           << VMX_BF_PROC_CTLS_HLT_EXIT_SHIFT          )
                                 | (pGuestFeatures->fVmxInvlpgExit        << VMX_BF_PROC_CTLS_INVLPG_EXIT_SHIFT       )
                                 | (pGuestFeatures->fVmxMwaitExit         << VMX_BF_PROC_CTLS_MWAIT_EXIT_SHIFT        )
                                 | (pGuestFeatures->fVmxRdpmcExit         << VMX_BF_PROC_CTLS_RDPMC_EXIT_SHIFT        )
                                 | (pGuestFeatures->fVmxRdtscExit         << VMX_BF_PROC_CTLS_RDTSC_EXIT_SHIFT        )
                                 | (pGuestFeatures->fVmxCr3LoadExit       << VMX_BF_PROC_CTLS_CR3_LOAD_EXIT_SHIFT     )
                                 | (pGuestFeatures->fVmxCr3StoreExit      << VMX_BF_PROC_CTLS_CR3_STORE_EXIT_SHIFT    )
                                 | (pGuestFeatures->fVmxTertiaryExecCtls  << VMX_BF_PROC_CTLS_USE_TERTIARY_CTLS_SHIFT )
                                 | (pGuestFeatures->fVmxCr8LoadExit       << VMX_BF_PROC_CTLS_CR8_LOAD_EXIT_SHIFT     )
                                 | (pGuestFeatures->fVmxCr8StoreExit      << VMX_BF_PROC_CTLS_CR8_STORE_EXIT_SHIFT    )
                                 | (pGuestFeatures->fVmxUseTprShadow      << VMX_BF_PROC_CTLS_USE_TPR_SHADOW_SHIFT    )
                                 | (pGuestFeatures->fVmxNmiWindowExit     << VMX_BF_PROC_CTLS_NMI_WINDOW_EXIT_SHIFT   )
                                 | (pGuestFeatures->fVmxMovDRxExit        << VMX_BF_PROC_CTLS_MOV_DR_EXIT_SHIFT       )
                                 | (pGuestFeatures->fVmxUncondIoExit      << VMX_BF_PROC_CTLS_UNCOND_IO_EXIT_SHIFT    )
                                 | (pGuestFeatures->fVmxUseIoBitmaps      << VMX_BF_PROC_CTLS_USE_IO_BITMAPS_SHIFT    )
                                 | (pGuestFeatures->fVmxMonitorTrapFlag   << VMX_BF_PROC_CTLS_MONITOR_TRAP_FLAG_SHIFT )
                                 | (pGuestFeatures->fVmxUseMsrBitmaps     << VMX_BF_PROC_CTLS_USE_MSR_BITMAPS_SHIFT   )
                                 | (pGuestFeatures->fVmxMonitorExit       << VMX_BF_PROC_CTLS_MONITOR_EXIT_SHIFT      )
                                 | (pGuestFeatures->fVmxPauseExit         << VMX_BF_PROC_CTLS_PAUSE_EXIT_SHIFT        )
                                 | (pGuestFeatures->fVmxSecondaryExecCtls << VMX_BF_PROC_CTLS_USE_SECONDARY_CTLS_SHIFT);
        uint32_t const fAllowed0 = VMX_PROC_CTLS_DEFAULT1;
        uint32_t const fAllowed1 = fFeatures | VMX_PROC_CTLS_DEFAULT1;
        AssertMsg((fAllowed0 & fAllowed1) == fAllowed0, ("fAllowed0=%#RX32 fAllowed1=%#RX32 fFeatures=%#RX32\n", fAllowed0,
                                                         fAllowed1, fFeatures));
        pGuestVmxMsrs->ProcCtls.u = RT_MAKE_U64(fAllowed0, fAllowed1);

        /* True processor-based VM-execution controls. */
        if (fTrueVmxMsrs)
        {
            /* VMX_PROC_CTLS_DEFAULT1 contains MB1 reserved bits but the following are not really reserved. */
            uint32_t const fTrueAllowed0 = VMX_PROC_CTLS_DEFAULT1 & ~(  VMX_BF_PROC_CTLS_CR3_LOAD_EXIT_MASK
                                                                      | VMX_BF_PROC_CTLS_CR3_STORE_EXIT_MASK);
            uint32_t const fTrueAllowed1 = fFeatures | fTrueAllowed0;
            pGuestVmxMsrs->TrueProcCtls.u = RT_MAKE_U64(fTrueAllowed0, fTrueAllowed1);
        }
    }

    /* Secondary processor-based VM-execution controls. */
    if (pGuestFeatures->fVmxSecondaryExecCtls)
    {
        uint32_t const fFeatures = (pGuestFeatures->fVmxVirtApicAccess        << VMX_BF_PROC_CTLS2_VIRT_APIC_ACCESS_SHIFT   )
                                 | (pGuestFeatures->fVmxEpt                   << VMX_BF_PROC_CTLS2_EPT_SHIFT                )
                                 | (pGuestFeatures->fVmxDescTableExit         << VMX_BF_PROC_CTLS2_DESC_TABLE_EXIT_SHIFT    )
                                 | (pGuestFeatures->fVmxRdtscp                << VMX_BF_PROC_CTLS2_RDTSCP_SHIFT             )
                                 | (pGuestFeatures->fVmxVirtX2ApicMode        << VMX_BF_PROC_CTLS2_VIRT_X2APIC_MODE_SHIFT   )
                                 | (pGuestFeatures->fVmxVpid                  << VMX_BF_PROC_CTLS2_VPID_SHIFT               )
                                 | (pGuestFeatures->fVmxWbinvdExit            << VMX_BF_PROC_CTLS2_WBINVD_EXIT_SHIFT        )
                                 | (pGuestFeatures->fVmxUnrestrictedGuest     << VMX_BF_PROC_CTLS2_UNRESTRICTED_GUEST_SHIFT )
                                 | (pGuestFeatures->fVmxApicRegVirt           << VMX_BF_PROC_CTLS2_APIC_REG_VIRT_SHIFT      )
                                 | (pGuestFeatures->fVmxVirtIntDelivery       << VMX_BF_PROC_CTLS2_VIRT_INT_DELIVERY_SHIFT  )
                                 | (pGuestFeatures->fVmxPauseLoopExit         << VMX_BF_PROC_CTLS2_PAUSE_LOOP_EXIT_SHIFT    )
                                 | (pGuestFeatures->fVmxRdrandExit            << VMX_BF_PROC_CTLS2_RDRAND_EXIT_SHIFT        )
                                 | (pGuestFeatures->fVmxInvpcid               << VMX_BF_PROC_CTLS2_INVPCID_SHIFT            )
                                 | (pGuestFeatures->fVmxVmFunc                << VMX_BF_PROC_CTLS2_VMFUNC_SHIFT             )
                                 | (pGuestFeatures->fVmxVmcsShadowing         << VMX_BF_PROC_CTLS2_VMCS_SHADOWING_SHIFT     )
                                 | (pGuestFeatures->fVmxRdseedExit            << VMX_BF_PROC_CTLS2_RDSEED_EXIT_SHIFT        )
                                 | (pGuestFeatures->fVmxPml                   << VMX_BF_PROC_CTLS2_PML_SHIFT                )
                                 | (pGuestFeatures->fVmxEptXcptVe             << VMX_BF_PROC_CTLS2_EPT_VE_SHIFT             )
                                 | (pGuestFeatures->fVmxConcealVmxFromPt      << VMX_BF_PROC_CTLS2_CONCEAL_VMX_FROM_PT_SHIFT)
                                 | (pGuestFeatures->fVmxXsavesXrstors         << VMX_BF_PROC_CTLS2_XSAVES_XRSTORS_SHIFT     )
                                 | (pGuestFeatures->fVmxPasidTranslate        << VMX_BF_PROC_CTLS2_PASID_TRANSLATE_SHIFT    )
                                 | (pGuestFeatures->fVmxModeBasedExecuteEpt   << VMX_BF_PROC_CTLS2_MODE_BASED_EPT_PERM_SHIFT)
                                 | (pGuestFeatures->fVmxSppEpt                << VMX_BF_PROC_CTLS2_SPP_EPT_SHIFT            )
                                 | (pGuestFeatures->fVmxPtEpt                 << VMX_BF_PROC_CTLS2_PT_EPT_SHIFT             )
                                 | (pGuestFeatures->fVmxUseTscScaling         << VMX_BF_PROC_CTLS2_TSC_SCALING_SHIFT        )
                                 | (pGuestFeatures->fVmxUserWaitPause         << VMX_BF_PROC_CTLS2_USER_WAIT_PAUSE_SHIFT    )
                                 | (pGuestFeatures->fVmxPconfig               << VMX_BF_PROC_CTLS2_PCONFIG_SHIFT            )
                                 | (pGuestFeatures->fVmxEnclvExit             << VMX_BF_PROC_CTLS2_ENCLV_EXIT_SHIFT         )
                                 | (pGuestFeatures->fVmxBusLockDetect         << VMX_BF_PROC_CTLS2_BUSLOCK_DETECT_SHIFT     )
                                 | (pGuestFeatures->fVmxInstrTimeout          << VMX_BF_PROC_CTLS2_INSTR_TIMEOUT_SHIFT      );
        uint32_t const fAllowed0 = 0;
        uint32_t const fAllowed1 = fFeatures;
        pGuestVmxMsrs->ProcCtls2.u = RT_MAKE_U64(fAllowed0, fAllowed1);
    }

    /* Tertiary processor-based VM-execution controls. */
    if (pGuestFeatures->fVmxTertiaryExecCtls)
    {
        pGuestVmxMsrs->u64ProcCtls3 = (pGuestFeatures->fVmxLoadIwKeyExit   << VMX_BF_PROC_CTLS3_LOADIWKEY_EXIT_SHIFT)
                                    | (pGuestFeatures->fVmxHlat            << VMX_BF_PROC_CTLS3_HLAT_SHIFT)
                                    | (pGuestFeatures->fVmxEptPagingWrite  << VMX_BF_PROC_CTLS3_EPT_PAGING_WRITE_SHIFT)
                                    | (pGuestFeatures->fVmxGstPagingVerify << VMX_BF_PROC_CTLS3_GST_PAGING_VERIFY_SHIFT)
                                    | (pGuestFeatures->fVmxIpiVirt         << VMX_BF_PROC_CTLS3_IPI_VIRT_SHIFT)
                                    | (pGuestFeatures->fVmxVirtSpecCtrl    << VMX_BF_PROC_CTLS3_VIRT_SPEC_CTRL_SHIFT);
    }

    /* VM-exit controls. */
    {
        uint32_t const fFeatures = (pGuestFeatures->fVmxExitSaveDebugCtls << VMX_BF_EXIT_CTLS_SAVE_DEBUG_SHIFT          )
                                 | (pGuestFeatures->fVmxHostAddrSpaceSize << VMX_BF_EXIT_CTLS_HOST_ADDR_SPACE_SIZE_SHIFT)
                                 | (pGuestFeatures->fVmxExitAckExtInt     << VMX_BF_EXIT_CTLS_ACK_EXT_INT_SHIFT         )
                                 | (pGuestFeatures->fVmxExitSavePatMsr    << VMX_BF_EXIT_CTLS_SAVE_PAT_MSR_SHIFT        )
                                 | (pGuestFeatures->fVmxExitLoadPatMsr    << VMX_BF_EXIT_CTLS_LOAD_PAT_MSR_SHIFT        )
                                 | (pGuestFeatures->fVmxExitSaveEferMsr   << VMX_BF_EXIT_CTLS_SAVE_EFER_MSR_SHIFT       )
                                 | (pGuestFeatures->fVmxExitLoadEferMsr   << VMX_BF_EXIT_CTLS_LOAD_EFER_MSR_SHIFT       )
                                 | (pGuestFeatures->fVmxSavePreemptTimer  << VMX_BF_EXIT_CTLS_SAVE_PREEMPT_TIMER_SHIFT  )
                                 | (pGuestFeatures->fVmxSecondaryExitCtls << VMX_BF_EXIT_CTLS_USE_SECONDARY_CTLS_SHIFT  );
        /* Set the default1 class bits. See Intel spec. A.4 "VM-exit Controls". */
        uint32_t const fAllowed0 = VMX_EXIT_CTLS_DEFAULT1;
        uint32_t const fAllowed1 = fFeatures | VMX_EXIT_CTLS_DEFAULT1;
        AssertMsg((fAllowed0 & fAllowed1) == fAllowed0, ("fAllowed0=%#RX32 fAllowed1=%#RX32 fFeatures=%#RX32\n", fAllowed0,
                                                         fAllowed1, fFeatures));
        pGuestVmxMsrs->ExitCtls.u = RT_MAKE_U64(fAllowed0, fAllowed1);

        /* True VM-exit controls. */
        if (fTrueVmxMsrs)
        {
            /* VMX_EXIT_CTLS_DEFAULT1 contains MB1 reserved bits but the following are not really reserved */
            uint32_t const fTrueAllowed0 = VMX_EXIT_CTLS_DEFAULT1 & ~VMX_BF_EXIT_CTLS_SAVE_DEBUG_MASK;
            uint32_t const fTrueAllowed1 = fFeatures | fTrueAllowed0;
            pGuestVmxMsrs->TrueExitCtls.u = RT_MAKE_U64(fTrueAllowed0, fTrueAllowed1);
        }
    }

    /* VM-entry controls. */
    {
        uint32_t const fFeatures = (pGuestFeatures->fVmxEntryLoadDebugCtls << VMX_BF_ENTRY_CTLS_LOAD_DEBUG_SHIFT      )
                                 | (pGuestFeatures->fVmxIa32eModeGuest     << VMX_BF_ENTRY_CTLS_IA32E_MODE_GUEST_SHIFT)
                                 | (pGuestFeatures->fVmxEntryLoadEferMsr   << VMX_BF_ENTRY_CTLS_LOAD_EFER_MSR_SHIFT   )
                                 | (pGuestFeatures->fVmxEntryLoadPatMsr    << VMX_BF_ENTRY_CTLS_LOAD_PAT_MSR_SHIFT    );
        uint32_t const fAllowed0 = VMX_ENTRY_CTLS_DEFAULT1;
        uint32_t const fAllowed1 = fFeatures | VMX_ENTRY_CTLS_DEFAULT1;
        AssertMsg((fAllowed0 & fAllowed1) == fAllowed0, ("fAllowed0=%#RX32 fAllowed0=%#RX32 fFeatures=%#RX32\n", fAllowed0,
                                                         fAllowed1, fFeatures));
        pGuestVmxMsrs->EntryCtls.u = RT_MAKE_U64(fAllowed0, fAllowed1);

        /* True VM-entry controls. */
        if (fTrueVmxMsrs)
        {
            /* VMX_ENTRY_CTLS_DEFAULT1 contains MB1 reserved bits but the following are not really reserved */
            uint32_t const fTrueAllowed0 = VMX_ENTRY_CTLS_DEFAULT1 & ~(  VMX_BF_ENTRY_CTLS_LOAD_DEBUG_MASK
                                                                       | VMX_BF_ENTRY_CTLS_IA32E_MODE_GUEST_MASK
                                                                       | VMX_BF_ENTRY_CTLS_ENTRY_SMM_MASK
                                                                       | VMX_BF_ENTRY_CTLS_DEACTIVATE_DUAL_MON_MASK);
            uint32_t const fTrueAllowed1 = fFeatures | fTrueAllowed0;
            pGuestVmxMsrs->TrueEntryCtls.u = RT_MAKE_U64(fTrueAllowed0, fTrueAllowed1);
        }
    }

    /* Miscellaneous data. */
    {
        uint64_t const uHostMsr = fIsNstGstHwExecAllowed ? pHostVmxMsrs->u64Misc : 0;

        uint8_t const  cMaxMsrs       = RT_MIN(RT_BF_GET(uHostMsr, VMX_BF_MISC_MAX_MSRS), VMX_V_AUTOMSR_COUNT_MAX);
        uint8_t const  fActivityState = RT_BF_GET(uHostMsr, VMX_BF_MISC_ACTIVITY_STATES) & VMX_V_GUEST_ACTIVITY_STATE_MASK;
        pGuestVmxMsrs->u64Misc = RT_BF_MAKE(VMX_BF_MISC_PREEMPT_TIMER_TSC,      VMX_V_PREEMPT_TIMER_SHIFT             )
                               | RT_BF_MAKE(VMX_BF_MISC_EXIT_SAVE_EFER_LMA,     pGuestFeatures->fVmxExitSaveEferLma   )
                               | RT_BF_MAKE(VMX_BF_MISC_ACTIVITY_STATES,        fActivityState                        )
                               | RT_BF_MAKE(VMX_BF_MISC_INTEL_PT,               pGuestFeatures->fVmxPt                )
                               | RT_BF_MAKE(VMX_BF_MISC_SMM_READ_SMBASE_MSR,    0                                     )
                               | RT_BF_MAKE(VMX_BF_MISC_CR3_TARGET,             VMX_V_CR3_TARGET_COUNT                )
                               | RT_BF_MAKE(VMX_BF_MISC_MAX_MSRS,               cMaxMsrs                              )
                               | RT_BF_MAKE(VMX_BF_MISC_VMXOFF_BLOCK_SMI,       0                                     )
                               | RT_BF_MAKE(VMX_BF_MISC_VMWRITE_ALL,            pGuestFeatures->fVmxVmwriteAll        )
                               | RT_BF_MAKE(VMX_BF_MISC_ENTRY_INJECT_SOFT_INT,  pGuestFeatures->fVmxEntryInjectSoftInt)
                               | RT_BF_MAKE(VMX_BF_MISC_MSEG_ID,                VMX_V_MSEG_REV_ID                     );
    }

    /* CR0 Fixed-0 (we report this fixed value regardless of whether UX is supported as it does on real hardware). */
    pGuestVmxMsrs->u64Cr0Fixed0 = VMX_V_CR0_FIXED0;

    /* CR0 Fixed-1. */
    {
        /*
         * All CPUs I've looked at so far report CR0 fixed-1 bits as 0xffffffff.
         * This is different from CR4 fixed-1 bits which are reported as per the
         * CPU features and/or micro-architecture/generation. Why? Ask Intel.
         */
        pGuestVmxMsrs->u64Cr0Fixed1 = fIsNstGstHwExecAllowed ? pHostVmxMsrs->u64Cr0Fixed1 : VMX_V_CR0_FIXED1;

        /* Make sure the CR0 MB1 bits are not clear. */
        Assert((pGuestVmxMsrs->u64Cr0Fixed1 & pGuestVmxMsrs->u64Cr0Fixed0) == pGuestVmxMsrs->u64Cr0Fixed0);
    }

    /* CR4 Fixed-0. */
    pGuestVmxMsrs->u64Cr4Fixed0 = VMX_V_CR4_FIXED0;

    /* CR4 Fixed-1. */
    {
        pGuestVmxMsrs->u64Cr4Fixed1 = CPUMGetGuestCR4ValidMask(pVM) & pHostVmxMsrs->u64Cr4Fixed1;

        /* Make sure the CR4 MB1 bits are not clear. */
        Assert((pGuestVmxMsrs->u64Cr4Fixed1 & pGuestVmxMsrs->u64Cr4Fixed0) == pGuestVmxMsrs->u64Cr4Fixed0);

        /* Make sure bits that must always be set are set. */
        Assert(pGuestVmxMsrs->u64Cr4Fixed1 & X86_CR4_PAE);
        Assert(pGuestVmxMsrs->u64Cr4Fixed1 & X86_CR4_VMXE);
    }

    /* VMCS Enumeration. */
    pGuestVmxMsrs->u64VmcsEnum = VMX_V_VMCS_MAX_INDEX << VMX_BF_VMCS_ENUM_HIGHEST_IDX_SHIFT;

    /* VPID and EPT Capabilities. */
    if (pGuestFeatures->fVmxEpt)
    {
        /*
         * INVVPID instruction always causes a VM-exit unconditionally, so we are free to fake
         * and emulate any INVVPID flush type. However, it only makes sense to expose the types
         * when INVVPID instruction is supported just to be more compatible with guest
         * hypervisors that may make assumptions by only looking at this MSR even though they
         * are technically supposed to refer to VMX_PROC_CTLS2_VPID first.
         *
         * See Intel spec. 25.1.2 "Instructions That Cause VM Exits Unconditionally".
         * See Intel spec. 30.3 "VMX Instructions".
         */
        uint64_t const uHostMsr = fIsNstGstHwExecAllowed ? pHostVmxMsrs->u64EptVpidCaps : UINT64_MAX;
        uint8_t const  fVpid    = pGuestFeatures->fVmxVpid;

        uint8_t const  fExecOnly         = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_EXEC_ONLY);
        uint8_t const  fPml4             = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_PAGE_WALK_LENGTH_4);
        uint8_t const  fMemTypeUc        = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_MEMTYPE_UC);
        uint8_t const  fMemTypeWb        = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_MEMTYPE_WB);
        uint8_t const  f2MPage           = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_PDE_2M);
        uint8_t const  fInvept           = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_INVEPT);
        /** @todo Nested VMX: Support accessed/dirty bits, see @bugref{10092#c25}. */
        /* uint8_t const  fAccessDirty      = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_ACCESS_DIRTY); */
        uint8_t const  fEptSingle        = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_INVEPT_SINGLE_CTX);
        uint8_t const  fEptAll           = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_INVEPT_ALL_CTX);
        uint8_t const  fVpidIndiv        = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_INVVPID_INDIV_ADDR);
        uint8_t const  fVpidSingle       = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX);
        uint8_t const  fVpidAll          = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_INVVPID_ALL_CTX);
        uint8_t const  fVpidSingleGlobal = RT_BF_GET(uHostMsr, VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX_RETAIN_GLOBALS);
        pGuestVmxMsrs->u64EptVpidCaps = RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_EXEC_ONLY,                         fExecOnly)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_PAGE_WALK_LENGTH_4,                fPml4)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_MEMTYPE_UC,                        fMemTypeUc)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_MEMTYPE_WB,                        fMemTypeWb)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_PDE_2M,                            f2MPage)
                                    //| RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_PDPTE_1G,                          0)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_INVEPT,                            fInvept)
                                    //| RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_ACCESS_DIRTY,                      0)
                                    //| RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_ADVEXITINFO_EPT_VIOLATION,         0)
                                    //| RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_SUPER_SHW_STACK,                   0)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_INVEPT_SINGLE_CTX,                 fEptSingle)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_INVEPT_ALL_CTX,                    fEptAll)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_INVVPID,                           fVpid)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_INVVPID_INDIV_ADDR,                fVpid & fVpidIndiv)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX,                fVpid & fVpidSingle)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_INVVPID_ALL_CTX,                   fVpid & fVpidAll)
                                      | RT_BF_MAKE(VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX_RETAIN_GLOBALS, fVpid & fVpidSingleGlobal);
    }

    /* VM Functions. */
    if (pGuestFeatures->fVmxVmFunc)
        pGuestVmxMsrs->u64VmFunc = RT_BF_MAKE(VMX_BF_VMFUNC_EPTP_SWITCHING, 1);
}


/**
 * Checks whether the given guest CPU VMX features are compatible with the provided
 * base features.
 *
 * @returns @c true if compatible, @c false otherwise.
 * @param   pVM     The cross context VM structure.
 * @param   pBase   The base VMX CPU features.
 * @param   pGst    The guest VMX CPU features.
 *
 * @remarks Only VMX feature bits are examined.
 */
static bool cpumR3AreVmxCpuFeaturesCompatible(PVM pVM, PCCPUMFEATURES pBase, PCCPUMFEATURES pGst)
{
    if (!cpumR3IsHwAssistNstGstExecAllowed(pVM))
        return false;

#define CPUM_VMX_FEAT_SHIFT(a_pFeat, a_FeatName, a_cShift)  ((uint64_t)(a_pFeat->a_FeatName) << (a_cShift))
#define CPUM_VMX_MAKE_FEATURES_1(a_pFeat)   (  CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxInsOutInfo         ,  0) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxExtIntExit         ,  1) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxNmiExit            ,  2) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxVirtNmi            ,  3) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxPreemptTimer       ,  4) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxPostedInt          ,  5) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxIntWindowExit      ,  6) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxTscOffsetting      ,  7) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxHltExit            ,  8) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxInvlpgExit         ,  9) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxMwaitExit          , 10) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxRdpmcExit          , 12) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxRdtscExit          , 13) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxCr3LoadExit        , 14) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxCr3StoreExit       , 15) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxTertiaryExecCtls   , 16) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxCr8LoadExit        , 17) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxCr8StoreExit       , 18) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxUseTprShadow       , 19) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxNmiWindowExit      , 20) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxMovDRxExit         , 21) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxUncondIoExit       , 22) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxUseIoBitmaps       , 23) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxMonitorTrapFlag    , 24) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxUseMsrBitmaps      , 25) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxMonitorExit        , 26) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxPauseExit          , 27) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxSecondaryExecCtls  , 28) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxVirtApicAccess     , 29) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxEpt                , 30) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxDescTableExit      , 31) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxRdtscp             , 32) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxVirtX2ApicMode     , 33) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxVpid               , 34) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxWbinvdExit         , 35) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxUnrestrictedGuest  , 36) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxApicRegVirt        , 37) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxVirtIntDelivery    , 38) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxPauseLoopExit      , 39) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxRdrandExit         , 40) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxInvpcid            , 41) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxVmFunc             , 42) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxVmcsShadowing      , 43) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxRdseedExit         , 44) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxPml                , 45) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxEptXcptVe          , 46) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxConcealVmxFromPt   , 47) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxXsavesXrstors      , 48) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxPasidTranslate     , 49) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxModeBasedExecuteEpt, 50) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxSppEpt             , 51) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxPtEpt              , 52) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxUseTscScaling      , 53) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxUserWaitPause      , 54) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxPconfig            , 55) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxEnclvExit          , 56) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxBusLockDetect      , 57) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxInstrTimeout       , 58) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxLoadIwKeyExit      , 59) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxHlat               , 60) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxEptPagingWrite     , 61) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxGstPagingVerify    , 62) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxIpiVirt            , 63))

#define CPUM_VMX_MAKE_FEATURES_2(a_pFeat)   (  CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxVirtSpecCtrl       ,  0) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxEntryLoadDebugCtls ,  1) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxIa32eModeGuest     ,  2) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxEntryLoadEferMsr   ,  3) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxEntryLoadPatMsr    ,  4) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxExitSaveDebugCtls  ,  5) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxHostAddrSpaceSize  ,  6) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxExitAckExtInt      ,  7) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxExitSavePatMsr     ,  8) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxExitLoadPatMsr     ,  9) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxExitSaveEferMsr    , 10) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxExitLoadEferMsr    , 12) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxSavePreemptTimer   , 13) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxSecondaryExitCtls  , 14) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxExitSaveEferLma    , 15) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxPt                 , 16) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxVmwriteAll         , 17) \
                                             | CPUM_VMX_FEAT_SHIFT(a_pFeat, fVmxEntryInjectSoftInt , 18))

    /* Check first set of feature bits. */
    {
        uint64_t const fBase = CPUM_VMX_MAKE_FEATURES_1(pBase);
        uint64_t const fGst  = CPUM_VMX_MAKE_FEATURES_1(pGst);
        if ((fBase | fGst) != fBase)
        {
            uint64_t const fDiff = fBase ^ fGst;
            LogRel(("CPUM: VMX features (1) now exposed to the guest are incompatible with those from the saved state. fBase=%#RX64 fGst=%#RX64 fDiff=%#RX64\n",
                    fBase, fGst, fDiff));
            return false;
        }
    }

    /* Check second set of feature bits. */
    {
        uint64_t const fBase = CPUM_VMX_MAKE_FEATURES_2(pBase);
        uint64_t const fGst  = CPUM_VMX_MAKE_FEATURES_2(pGst);
        if ((fBase | fGst) != fBase)
        {
            uint64_t const fDiff = fBase ^ fGst;
            LogRel(("CPUM: VMX features (2) now exposed to the guest are incompatible with those from the saved state. fBase=%#RX64 fGst=%#RX64 fDiff=%#RX64\n",
                    fBase, fGst, fDiff));
            return false;
        }
    }
#undef CPUM_VMX_FEAT_SHIFT
#undef CPUM_VMX_MAKE_FEATURES_1
#undef CPUM_VMX_MAKE_FEATURES_2

    return true;
}


/**
 * Initializes VMX guest features and MSRs.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pCpumCfg        The CPUM CFGM configuration node.
 * @param   pHostVmxMsrs    The host VMX MSRs. Pass NULL when fully emulating VMX
 *                          and no hardware-assisted nested-guest execution is
 *                          possible for this VM.
 * @param   pGuestVmxMsrs   Where to store the initialized guest VMX MSRs.
 */
void cpumR3InitVmxGuestFeaturesAndMsrs(PVM pVM, PCFGMNODE pCpumCfg, PCVMXMSRS pHostVmxMsrs, PVMXMSRS pGuestVmxMsrs)
{
    Assert(pVM);
    Assert(pCpumCfg);
    Assert(pGuestVmxMsrs);

    /*
     * Query VMX features from CFGM.
     */
    bool fVmxPreemptTimer;
    bool fVmxEpt;
    bool fVmxUnrestrictedGuest;
    {
        /** @cfgm{/CPUM/NestedVmxPreemptTimer, bool, true}
         * Whether to expose the VMX-preemption timer feature to the guest (if also
         * supported by the host hardware). When disabled will prevent exposing the
         * VMX-preemption timer feature to the guest even if the host supports it.
         *
         * @todo Currently disabled, see @bugref{9180#c108}.
         */
        int rc = CFGMR3QueryBoolDef(pCpumCfg, "NestedVmxPreemptTimer", &fVmxPreemptTimer, false);
        AssertLogRelRCReturnVoid(rc);

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        /** @cfgm{/CPUM/NestedVmxEpt, bool, true}
         * Whether to expose the EPT feature to the guest. The default is true.
         * When disabled will automatically prevent exposing features that rely
         * on it.  This is dependent upon nested paging being enabled for the VM.
         */
        rc = CFGMR3QueryBoolDef(pCpumCfg, "NestedVmxEpt", &fVmxEpt, true);
        AssertLogRelRCReturnVoid(rc);

        /** @cfgm{/CPUM/NestedVmxUnrestrictedGuest, bool, true}
         * Whether to expose the Unrestricted Guest feature to the guest. The
         * default is the same a /CPUM/Nested/VmxEpt. When disabled will
         * automatically prevent exposing features that rely on it.
         */
        rc = CFGMR3QueryBoolDef(pCpumCfg, "NestedVmxUnrestrictedGuest", &fVmxUnrestrictedGuest, fVmxEpt);
        AssertLogRelRCReturnVoid(rc);
#else
        fVmxEpt = fVmxUnrestrictedGuest = false;
#endif
    }

    if (fVmxEpt)
    {
        const char *pszWhy = NULL;
        if (!VM_IS_HM_ENABLED(pVM) && !VM_IS_EXEC_ENGINE_IEM(pVM))
            pszWhy = "execution engine is neither HM nor IEM";
        else if (VM_IS_HM_ENABLED(pVM) && !HMIsNestedPagingActive(pVM))
            pszWhy = "nested paging is not enabled for the VM or it is not supported by the host";
        else if (VM_IS_HM_ENABLED(pVM) && !pVM->cpum.s.HostFeatures.fNoExecute)
            pszWhy = "NX is not available on the host";
        if (pszWhy)
        {
            LogRel(("CPUM: Warning! EPT not exposed to the guest because %s\n", pszWhy));
            fVmxEpt = false;
        }
    }
    else if (fVmxUnrestrictedGuest)
    {
        LogRel(("CPUM: Warning! Can't expose \"Unrestricted Guest\" to the guest when EPT is not exposed!\n"));
        fVmxUnrestrictedGuest = false;
    }

    /*
     * Initialize the set of VMX features we emulate.
     *
     * Note! Some bits might be reported as 1 always if they fall under the
     * default1 class bits (e.g. fVmxEntryLoadDebugCtls), see @bugref{9180#c5}.
     */
    CPUMFEATURES EmuFeat;
    RT_ZERO(EmuFeat);
    EmuFeat.fVmx                      = 1;
    EmuFeat.fVmxInsOutInfo            = 1;
    EmuFeat.fVmxExtIntExit            = 1;
    EmuFeat.fVmxNmiExit               = 1;
    EmuFeat.fVmxVirtNmi               = 1;
    EmuFeat.fVmxPreemptTimer          = fVmxPreemptTimer;
    EmuFeat.fVmxPostedInt             = 0;
    EmuFeat.fVmxIntWindowExit         = 1;
    EmuFeat.fVmxTscOffsetting         = 1;
    EmuFeat.fVmxHltExit               = 1;
    EmuFeat.fVmxInvlpgExit            = 1;
    EmuFeat.fVmxMwaitExit             = 1;
    EmuFeat.fVmxRdpmcExit             = 1;
    EmuFeat.fVmxRdtscExit             = 1;
    EmuFeat.fVmxCr3LoadExit           = 1;
    EmuFeat.fVmxCr3StoreExit          = 1;
    EmuFeat.fVmxTertiaryExecCtls      = 0;
    EmuFeat.fVmxCr8LoadExit           = 1;
    EmuFeat.fVmxCr8StoreExit          = 1;
    EmuFeat.fVmxUseTprShadow          = 1;
    EmuFeat.fVmxNmiWindowExit         = 1;
    EmuFeat.fVmxMovDRxExit            = 1;
    EmuFeat.fVmxUncondIoExit          = 1;
    EmuFeat.fVmxUseIoBitmaps          = 1;
    EmuFeat.fVmxMonitorTrapFlag       = 0;
    EmuFeat.fVmxUseMsrBitmaps         = 1;
    EmuFeat.fVmxMonitorExit           = 1;
    EmuFeat.fVmxPauseExit             = 1;
    EmuFeat.fVmxSecondaryExecCtls     = 1;
    EmuFeat.fVmxVirtApicAccess        = 1;
    EmuFeat.fVmxEpt                   = fVmxEpt;
    EmuFeat.fVmxDescTableExit         = 1;
    EmuFeat.fVmxRdtscp                = 1;
    EmuFeat.fVmxVirtX2ApicMode        = 0;
    EmuFeat.fVmxVpid                  = 1;
    EmuFeat.fVmxWbinvdExit            = 1;
    EmuFeat.fVmxUnrestrictedGuest     = fVmxUnrestrictedGuest;
    EmuFeat.fVmxApicRegVirt           = 0;
    EmuFeat.fVmxVirtIntDelivery       = 0;
    EmuFeat.fVmxPauseLoopExit         = 1;
    EmuFeat.fVmxRdrandExit            = 1;
    EmuFeat.fVmxInvpcid               = 1;
    EmuFeat.fVmxVmFunc                = 0;
    EmuFeat.fVmxVmcsShadowing         = 0;
    EmuFeat.fVmxRdseedExit            = 1;
    EmuFeat.fVmxPml                   = 0;
    EmuFeat.fVmxEptXcptVe             = 0;
    EmuFeat.fVmxConcealVmxFromPt      = 0;
    EmuFeat.fVmxXsavesXrstors         = 0;
    EmuFeat.fVmxPasidTranslate        = 0;
    EmuFeat.fVmxModeBasedExecuteEpt   = 0;
    EmuFeat.fVmxSppEpt                = 0;
    EmuFeat.fVmxPtEpt                 = 0;
    EmuFeat.fVmxUseTscScaling         = 0;
    EmuFeat.fVmxUserWaitPause         = 0;
    EmuFeat.fVmxPconfig               = 0;
    EmuFeat.fVmxEnclvExit             = 0;
    EmuFeat.fVmxBusLockDetect         = 0;
    EmuFeat.fVmxInstrTimeout          = 0;
    EmuFeat.fVmxLoadIwKeyExit         = 0;
    EmuFeat.fVmxHlat                  = 0;
    EmuFeat.fVmxEptPagingWrite        = 0;
    EmuFeat.fVmxGstPagingVerify       = 0;
    EmuFeat.fVmxIpiVirt               = 0;
    EmuFeat.fVmxVirtSpecCtrl          = 0;
    EmuFeat.fVmxEntryLoadDebugCtls    = 1;
    EmuFeat.fVmxIa32eModeGuest        = 1;
    EmuFeat.fVmxEntryLoadEferMsr      = 1;
    EmuFeat.fVmxEntryLoadPatMsr       = 1;
    EmuFeat.fVmxExitSaveDebugCtls     = 1;
    EmuFeat.fVmxHostAddrSpaceSize     = 1;
    EmuFeat.fVmxExitAckExtInt         = 1;
    EmuFeat.fVmxExitSavePatMsr        = 1;
    EmuFeat.fVmxExitLoadPatMsr        = 1;
    EmuFeat.fVmxExitSaveEferMsr       = 1;
    EmuFeat.fVmxExitLoadEferMsr       = 1;
    EmuFeat.fVmxSavePreemptTimer      = 0 & fVmxPreemptTimer;       /* Cannot be enabled if VMX-preemption timer is disabled. */
    EmuFeat.fVmxSecondaryExitCtls     = 0;
    EmuFeat.fVmxExitSaveEferLma       = 1 | fVmxUnrestrictedGuest;  /* Cannot be disabled if unrestricted guest is enabled. */
    EmuFeat.fVmxPt                    = 0;
    EmuFeat.fVmxVmwriteAll            = 0;  /** @todo NSTVMX: enable this when nested VMCS shadowing is enabled. */
    EmuFeat.fVmxEntryInjectSoftInt    = 1;

    /*
     * Merge guest features.
     *
     * When hardware-assisted VMX may be used, any feature we emulate must also be supported
     * by the hardware, hence we merge our emulated features with the host features below.
     */
    PCCPUMFEATURES pBaseFeat  = cpumR3IsHwAssistNstGstExecAllowed(pVM) ? &pVM->cpum.s.HostFeatures : &EmuFeat;
    PCPUMFEATURES  pGuestFeat = &pVM->cpum.s.GuestFeatures;
    Assert(pBaseFeat->fVmx);
#define CPUMVMX_SET_GST_FEAT(a_Feat) \
    do { \
        pGuestFeat->a_Feat = (pBaseFeat->a_Feat & EmuFeat.a_Feat); \
    } while (0)

    CPUMVMX_SET_GST_FEAT(fVmxInsOutInfo);
    CPUMVMX_SET_GST_FEAT(fVmxExtIntExit);
    CPUMVMX_SET_GST_FEAT(fVmxNmiExit);
    CPUMVMX_SET_GST_FEAT(fVmxVirtNmi);
    CPUMVMX_SET_GST_FEAT(fVmxPreemptTimer);
    CPUMVMX_SET_GST_FEAT(fVmxPostedInt);
    CPUMVMX_SET_GST_FEAT(fVmxIntWindowExit);
    CPUMVMX_SET_GST_FEAT(fVmxTscOffsetting);
    CPUMVMX_SET_GST_FEAT(fVmxHltExit);
    CPUMVMX_SET_GST_FEAT(fVmxInvlpgExit);
    CPUMVMX_SET_GST_FEAT(fVmxMwaitExit);
    CPUMVMX_SET_GST_FEAT(fVmxRdpmcExit);
    CPUMVMX_SET_GST_FEAT(fVmxRdtscExit);
    CPUMVMX_SET_GST_FEAT(fVmxCr3LoadExit);
    CPUMVMX_SET_GST_FEAT(fVmxCr3StoreExit);
    CPUMVMX_SET_GST_FEAT(fVmxTertiaryExecCtls);
    CPUMVMX_SET_GST_FEAT(fVmxCr8LoadExit);
    CPUMVMX_SET_GST_FEAT(fVmxCr8StoreExit);
    CPUMVMX_SET_GST_FEAT(fVmxUseTprShadow);
    CPUMVMX_SET_GST_FEAT(fVmxNmiWindowExit);
    CPUMVMX_SET_GST_FEAT(fVmxMovDRxExit);
    CPUMVMX_SET_GST_FEAT(fVmxUncondIoExit);
    CPUMVMX_SET_GST_FEAT(fVmxUseIoBitmaps);
    CPUMVMX_SET_GST_FEAT(fVmxMonitorTrapFlag);
    CPUMVMX_SET_GST_FEAT(fVmxUseMsrBitmaps);
    CPUMVMX_SET_GST_FEAT(fVmxMonitorExit);
    CPUMVMX_SET_GST_FEAT(fVmxPauseExit);
    CPUMVMX_SET_GST_FEAT(fVmxSecondaryExecCtls);
    CPUMVMX_SET_GST_FEAT(fVmxVirtApicAccess);
    CPUMVMX_SET_GST_FEAT(fVmxEpt);
    CPUMVMX_SET_GST_FEAT(fVmxDescTableExit);
    CPUMVMX_SET_GST_FEAT(fVmxRdtscp);
    CPUMVMX_SET_GST_FEAT(fVmxVirtX2ApicMode);
    CPUMVMX_SET_GST_FEAT(fVmxVpid);
    CPUMVMX_SET_GST_FEAT(fVmxWbinvdExit);
    CPUMVMX_SET_GST_FEAT(fVmxUnrestrictedGuest);
    CPUMVMX_SET_GST_FEAT(fVmxApicRegVirt);
    CPUMVMX_SET_GST_FEAT(fVmxVirtIntDelivery);
    CPUMVMX_SET_GST_FEAT(fVmxPauseLoopExit);
    CPUMVMX_SET_GST_FEAT(fVmxRdrandExit);
    CPUMVMX_SET_GST_FEAT(fVmxInvpcid);
    CPUMVMX_SET_GST_FEAT(fVmxVmFunc);
    CPUMVMX_SET_GST_FEAT(fVmxVmcsShadowing);
    CPUMVMX_SET_GST_FEAT(fVmxRdseedExit);
    CPUMVMX_SET_GST_FEAT(fVmxPml);
    CPUMVMX_SET_GST_FEAT(fVmxEptXcptVe);
    CPUMVMX_SET_GST_FEAT(fVmxConcealVmxFromPt);
    CPUMVMX_SET_GST_FEAT(fVmxXsavesXrstors);
    CPUMVMX_SET_GST_FEAT(fVmxPasidTranslate);
    CPUMVMX_SET_GST_FEAT(fVmxModeBasedExecuteEpt);
    CPUMVMX_SET_GST_FEAT(fVmxSppEpt);
    CPUMVMX_SET_GST_FEAT(fVmxPtEpt);
    CPUMVMX_SET_GST_FEAT(fVmxUseTscScaling);
    CPUMVMX_SET_GST_FEAT(fVmxUserWaitPause);
    CPUMVMX_SET_GST_FEAT(fVmxPconfig);
    CPUMVMX_SET_GST_FEAT(fVmxEnclvExit);
    CPUMVMX_SET_GST_FEAT(fVmxBusLockDetect);
    CPUMVMX_SET_GST_FEAT(fVmxInstrTimeout);
    CPUMVMX_SET_GST_FEAT(fVmxLoadIwKeyExit);
    CPUMVMX_SET_GST_FEAT(fVmxHlat);
    CPUMVMX_SET_GST_FEAT(fVmxEptPagingWrite);
    CPUMVMX_SET_GST_FEAT(fVmxGstPagingVerify);
    CPUMVMX_SET_GST_FEAT(fVmxIpiVirt);
    CPUMVMX_SET_GST_FEAT(fVmxVirtSpecCtrl);
    CPUMVMX_SET_GST_FEAT(fVmxEntryLoadDebugCtls);
    CPUMVMX_SET_GST_FEAT(fVmxIa32eModeGuest);
    CPUMVMX_SET_GST_FEAT(fVmxEntryLoadEferMsr);
    CPUMVMX_SET_GST_FEAT(fVmxEntryLoadPatMsr);
    CPUMVMX_SET_GST_FEAT(fVmxExitSaveDebugCtls);
    CPUMVMX_SET_GST_FEAT(fVmxHostAddrSpaceSize);
    CPUMVMX_SET_GST_FEAT(fVmxExitAckExtInt);
    CPUMVMX_SET_GST_FEAT(fVmxExitSavePatMsr);
    CPUMVMX_SET_GST_FEAT(fVmxExitLoadPatMsr);
    CPUMVMX_SET_GST_FEAT(fVmxExitSaveEferMsr);
    CPUMVMX_SET_GST_FEAT(fVmxExitLoadEferMsr);
    CPUMVMX_SET_GST_FEAT(fVmxSavePreemptTimer);
    CPUMVMX_SET_GST_FEAT(fVmxSecondaryExitCtls);
    CPUMVMX_SET_GST_FEAT(fVmxExitSaveEferLma);
    CPUMVMX_SET_GST_FEAT(fVmxPt);
    CPUMVMX_SET_GST_FEAT(fVmxVmwriteAll);
    CPUMVMX_SET_GST_FEAT(fVmxEntryInjectSoftInt);

#undef CPUMVMX_SET_GST_FEAT

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    /* Don't expose VMX preemption timer if host is subject to VMX-preemption timer erratum. */
    if (   pGuestFeat->fVmxPreemptTimer
        && HMIsSubjectToVmxPreemptTimerErratum())
    {
        LogRel(("CPUM: Warning! VMX-preemption timer not exposed to guest due to host CPU erratum\n"));
        pGuestFeat->fVmxPreemptTimer     = 0;
        pGuestFeat->fVmxSavePreemptTimer = 0;
    }
#endif

    /* Sanity checking. */
    if (!pGuestFeat->fVmxSecondaryExecCtls)
    {
        Assert(!pGuestFeat->fVmxVirtApicAccess);
        Assert(!pGuestFeat->fVmxEpt);
        Assert(!pGuestFeat->fVmxDescTableExit);
        Assert(!pGuestFeat->fVmxRdtscp);
        Assert(!pGuestFeat->fVmxVirtX2ApicMode);
        Assert(!pGuestFeat->fVmxVpid);
        Assert(!pGuestFeat->fVmxWbinvdExit);
        Assert(!pGuestFeat->fVmxUnrestrictedGuest);
        Assert(!pGuestFeat->fVmxApicRegVirt);
        Assert(!pGuestFeat->fVmxVirtIntDelivery);
        Assert(!pGuestFeat->fVmxPauseLoopExit);
        Assert(!pGuestFeat->fVmxRdrandExit);
        Assert(!pGuestFeat->fVmxInvpcid);
        Assert(!pGuestFeat->fVmxVmFunc);
        Assert(!pGuestFeat->fVmxVmcsShadowing);
        Assert(!pGuestFeat->fVmxRdseedExit);
        Assert(!pGuestFeat->fVmxPml);
        Assert(!pGuestFeat->fVmxEptXcptVe);
        Assert(!pGuestFeat->fVmxConcealVmxFromPt);
        Assert(!pGuestFeat->fVmxXsavesXrstors);
        Assert(!pGuestFeat->fVmxModeBasedExecuteEpt);
        Assert(!pGuestFeat->fVmxSppEpt);
        Assert(!pGuestFeat->fVmxPtEpt);
        Assert(!pGuestFeat->fVmxUseTscScaling);
        Assert(!pGuestFeat->fVmxUserWaitPause);
        Assert(!pGuestFeat->fVmxEnclvExit);
    }
    else if (pGuestFeat->fVmxUnrestrictedGuest)
    {
        /* See footnote in Intel spec. 27.2 "Recording VM-Exit Information And Updating VM-entry Control Fields". */
        Assert(pGuestFeat->fVmxExitSaveEferLma);
        /* Unrestricted guest execution requires EPT. See Intel spec. 25.2.1.1 "VM-Execution Control Fields". */
        Assert(pGuestFeat->fVmxEpt);
    }

    if (!pGuestFeat->fVmxTertiaryExecCtls)
    {
        Assert(!pGuestFeat->fVmxLoadIwKeyExit);
        Assert(!pGuestFeat->fVmxHlat);
        Assert(!pGuestFeat->fVmxEptPagingWrite);
        Assert(!pGuestFeat->fVmxGstPagingVerify);
        Assert(!pGuestFeat->fVmxIpiVirt);
        Assert(!pGuestFeat->fVmxVirtSpecCtrl);
    }

    /*
     * Finally initialize the VMX guest MSRs.
     */
    cpumR3InitVmxGuestMsrs(pVM, pHostVmxMsrs, pGuestFeat, pGuestVmxMsrs);
}


/**
 * Gets the host hardware-virtualization MSRs.
 *
 * @returns VBox status code.
 * @param   pMsrs       Where to store the MSRs.
 */
static int cpumR3GetHostHwvirtMsrs(PCPUMMSRS pMsrs)
{
    Assert(pMsrs);

    uint32_t fCaps = 0;
    int rc = SUPR3QueryVTCaps(&fCaps);
    if (RT_SUCCESS(rc))
    {
        if (fCaps & (SUPVTCAPS_VT_X | SUPVTCAPS_AMD_V))
        {
            SUPHWVIRTMSRS HwvirtMsrs;
            rc = SUPR3GetHwvirtMsrs(&HwvirtMsrs, false /* fForceRequery */);
            if (RT_SUCCESS(rc))
            {
                if (fCaps & SUPVTCAPS_VT_X)
                    HMGetVmxMsrsFromHwvirtMsrs(&HwvirtMsrs, &pMsrs->hwvirt.vmx);
                else
                    HMGetSvmMsrsFromHwvirtMsrs(&HwvirtMsrs, &pMsrs->hwvirt.svm);
                return VINF_SUCCESS;
            }

            LogRel(("CPUM: Querying hardware-virtualization MSRs failed. rc=%Rrc\n", rc));
            return rc;
        }

        LogRel(("CPUM: Querying hardware-virtualization capability succeeded but did not find VT-x or AMD-V\n"));
        return VERR_INTERNAL_ERROR_5;
    }

    LogRel(("CPUM: No hardware-virtualization capability detected\n"));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNTMTIMERINT,
 *  Callback that fires when the nested VMX-preemption timer expired.}
 */
static DECLCALLBACK(void) cpumR3VmxPreemptTimerCallback(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser)
{
    RT_NOREF(pVM, hTimer);
    PVMCPU pVCpu = (PVMCPUR3)pvUser;
    AssertPtr(pVCpu);
    VMCPU_FF_SET(pVCpu, VMCPU_FF_VMX_PREEMPT_TIMER);
}


/**
 * Initializes the CPUM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) CPUMR3Init(PVM pVM)
{
    LogFlow(("CPUMR3Init\n"));

    /*
     * Assert alignment, sizes and tables.
     */
    AssertCompileMemberAlignment(VM, cpum.s, 32);
    AssertCompile(sizeof(pVM->cpum.s) <= sizeof(pVM->cpum.padding));
    AssertCompileSizeAlignment(CPUMCTX, 64);
    AssertCompileSizeAlignment(CPUMCTXMSRS, 64);
    AssertCompileSizeAlignment(CPUMHOSTCTX, 64);
    AssertCompileMemberAlignment(VM, cpum, 64);
    AssertCompileMemberAlignment(VMCPU, cpum.s, 64);
#ifdef VBOX_STRICT
    int rc2 = cpumR3MsrStrictInitChecks();
    AssertRCReturn(rc2, rc2);
#endif

    /*
     * Gather info about the host CPU.
     */
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    if (!ASMHasCpuId())
    {
        LogRel(("The CPU doesn't support CPUID!\n"));
        return VERR_UNSUPPORTED_CPU;
    }

    pVM->cpum.s.fHostMxCsrMask = CPUMR3DeterminHostMxCsrMask();
#endif

    CPUMMSRS HostMsrs;
    RT_ZERO(HostMsrs);
    int rc = cpumR3GetHostHwvirtMsrs(&HostMsrs);
    AssertLogRelRCReturn(rc, rc);

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    /* Use the host features detected by CPUMR0ModuleInit if available. */
    if (pVM->cpum.s.HostFeatures.enmCpuVendor != CPUMCPUVENDOR_INVALID)
        g_CpumHostFeatures.s = pVM->cpum.s.HostFeatures;
    else
    {
        PCPUMCPUIDLEAF  paLeaves;
        uint32_t        cLeaves;
        rc = CPUMCpuIdCollectLeavesX86(&paLeaves, &cLeaves);
        AssertLogRelRCReturn(rc, rc);

        rc = cpumCpuIdExplodeFeaturesX86(paLeaves, cLeaves, &HostMsrs, &g_CpumHostFeatures.s);
        RTMemFree(paLeaves);
        AssertLogRelRCReturn(rc, rc);
    }
    pVM->cpum.s.HostFeatures               = g_CpumHostFeatures.s;
    pVM->cpum.s.GuestFeatures.enmCpuVendor = pVM->cpum.s.HostFeatures.enmCpuVendor;

#elif defined(RT_ARCH_ARM64)
    /** @todo we shouldn't be using the x86/AMD64 CPUMFEATURES for HostFeatures,
     *        but it's too much work to fix that now.  So, instead we just set
     *        the bits we think are important for CPUMR3CpuId...  This must
     *        correspond to what IEM can emulate on ARM64. */
    pVM->cpum.s.HostFeatures.fCmpXchg8b  = true;
    pVM->cpum.s.HostFeatures.fCmpXchg16b = true;
    pVM->cpum.s.HostFeatures.fPopCnt     = true;
    pVM->cpum.s.HostFeatures.fAbm        = true;
    pVM->cpum.s.HostFeatures.fBmi1       = true;
    pVM->cpum.s.HostFeatures.fBmi2       = true;
    pVM->cpum.s.HostFeatures.fAdx        = true;
    pVM->cpum.s.HostFeatures.fSse        = true;
    pVM->cpum.s.HostFeatures.fSse2       = true;
    pVM->cpum.s.HostFeatures.fSse3       = true;
    pVM->cpum.s.HostFeatures.fSse41      = true;
    pVM->cpum.s.HostFeatures.fSse42      = true;
    pVM->cpum.s.HostFeatures.fLahfSahf   = true;
    pVM->cpum.s.HostFeatures.fMovBe      = true;
    pVM->cpum.s.HostFeatures.fXSaveRstor = true;
    pVM->cpum.s.HostFeatures.fOpSysXSaveRstor = true;
    /** @todo r=aeichner Keep AVX/AVX2 disabled for now, too many missing instruction emulations. */
# if 1
    pVM->cpum.s.HostFeatures.cbMaxExtendedState = RT_UOFFSETOF(X86XSAVEAREA, u.YmmHi);
# else
    pVM->cpum.s.HostFeatures.cbMaxExtendedState = RT_UOFFSETOF(X86XSAVEAREA, u.YmmHi) + sizeof(X86XSAVEYMMHI);
    pVM->cpum.s.HostFeatures.fAvx               = false;
    pVM->cpum.s.HostFeatures.fAvx2              = false;
# endif

    /* We must strongly discourage the guest from doing unnecessary stuff with the
       page tables to avoid exploits, as that's expensive and doesn't apply to us. */
    pVM->cpum.s.HostFeatures.fArchRdclNo             = true;
    pVM->cpum.s.HostFeatures.fArchIbrsAll            = true;
    //pVM->cpum.s.HostFeatures.fArchRsbOverride        = true;
    pVM->cpum.s.HostFeatures.fArchVmmNeedNotFlushL1d = true;
    pVM->cpum.s.HostFeatures.fArchMdsNo              = true;
    VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->cpum.s.GuestMsrs.msr.ArchCaps = MSR_IA32_ARCH_CAP_F_RDCL_NO
                                                                       | MSR_IA32_ARCH_CAP_F_IBRS_ALL
                                                                       //| MSR_IA32_ARCH_CAP_F_RSBO
                                                                       | MSR_IA32_ARCH_CAP_F_VMM_NEED_NOT_FLUSH_L1D
                                                                       | MSR_IA32_ARCH_CAP_F_SSB_NO
                                                                       | MSR_IA32_ARCH_CAP_F_MDS_NO
                                                                       | MSR_IA32_ARCH_CAP_F_IF_PSCHANGE_MC_NO
                                                                       //| MSR_IA32_ARCH_CAP_F_TSX_CTRL
                                                                       //| MSR_IA32_ARCH_CAP_F_TAA_NO
                                                                       //| MSR_IA32_ARCH_CAP_F_MISC_PACKAGE_CTRLS
                                                                       //| MSR_IA32_ARCH_CAP_F_ENERGY_FILTERING_CTL
                                                                       //| MSR_IA32_ARCH_CAP_F_DOITM
                                                                       | MSR_IA32_ARCH_CAP_F_SBDR_SSDP_NO
                                                                       | MSR_IA32_ARCH_CAP_F_FBSDP_NO
                                                                       | MSR_IA32_ARCH_CAP_F_PSDP_NO
                                                                       //| MSR_IA32_ARCH_CAP_F_FB_CLEAR
                                                                       //| MSR_IA32_ARCH_CAP_F_FB_CLEAR_CTRL
                                                                       //| MSR_IA32_ARCH_CAP_F_RRSBA
                                                                       | MSR_IA32_ARCH_CAP_F_BHI_NO
                                                                       //| MSR_IA32_ARCH_CAP_F_XAPIC_DISABLE_STATUS
                                                                       //| MSR_IA32_ARCH_CAP_F_OVERCLOCKING_STATUS
                                                                       | MSR_IA32_ARCH_CAP_F_PBRSB_NO
                                                                       //| MSR_IA32_ARCH_CAP_F_GDS_CTRL
                                                                       | MSR_IA32_ARCH_CAP_F_GDS_NO
                                                                       | MSR_IA32_ARCH_CAP_F_RFDS_NO
                                                                       //| MSR_IA32_ARCH_CAP_F_RFDS_CLEAR
                             );
#endif

    /*
     * Check that the CPU supports the minimum features we require.
     */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    if (!pVM->cpum.s.HostFeatures.fFxSaveRstor)
        return VMSetError(pVM, VERR_UNSUPPORTED_CPU, RT_SRC_POS, "Host CPU does not support the FXSAVE/FXRSTOR instruction.");
    if (!pVM->cpum.s.HostFeatures.fMmx)
        return VMSetError(pVM, VERR_UNSUPPORTED_CPU, RT_SRC_POS, "Host CPU does not support MMX.");
    if (!pVM->cpum.s.HostFeatures.fTsc)
        return VMSetError(pVM, VERR_UNSUPPORTED_CPU, RT_SRC_POS, "Host CPU does not support RDTSC.");
#endif

    /*
     * Setup the CR4 AND and OR masks used in the raw-mode switcher.
     */
    pVM->cpum.s.CR4.AndMask = X86_CR4_OSXMMEEXCPT | X86_CR4_PVI | X86_CR4_VME;
    pVM->cpum.s.CR4.OrMask  = X86_CR4_OSFXSR;

    /*
     * Figure out which XSAVE/XRSTOR features are available on the host.
     */
    uint64_t fXcr0Host = 0;
    uint64_t fXStateHostMask = 0;
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    if (   pVM->cpum.s.HostFeatures.fXSaveRstor
        && pVM->cpum.s.HostFeatures.fOpSysXSaveRstor)
    {
        fXStateHostMask  = fXcr0Host = ASMGetXcr0();
        fXStateHostMask &= XSAVE_C_X87 | XSAVE_C_SSE | XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI;
        AssertLogRelMsgStmt((fXStateHostMask & (XSAVE_C_X87 | XSAVE_C_SSE)) == (XSAVE_C_X87 | XSAVE_C_SSE),
                            ("%#llx\n", fXStateHostMask), fXStateHostMask = 0);
    }
#elif defined(RT_ARCH_ARM64)
    /** @todo r=aeichner Keep AVX/AVX2 disabled for now, too many missing instruction emulations. */
    fXStateHostMask = XSAVE_C_X87 | XSAVE_C_SSE /*| XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI*/;
#endif
    pVM->cpum.s.fXStateHostMask = fXStateHostMask;
    LogRel(("CPUM: fXStateHostMask=%#llx; initial: %#llx; host XCR0=%#llx\n",
            pVM->cpum.s.fXStateHostMask, fXStateHostMask, fXcr0Host));

    /*
     * Initialize the host XSAVE/XRSTOR mask.
     */
    uint32_t cbMaxXState = pVM->cpum.s.HostFeatures.cbMaxExtendedState;
    cbMaxXState = RT_ALIGN(cbMaxXState, 128);
    AssertLogRelReturn(   pVM->cpum.s.HostFeatures.cbMaxExtendedState >= sizeof(X86FXSTATE)
                       && pVM->cpum.s.HostFeatures.cbMaxExtendedState <= sizeof(pVM->apCpusR3[0]->cpum.s.Host.abXState)
                       && pVM->cpum.s.HostFeatures.cbMaxExtendedState <= sizeof(pVM->apCpusR3[0]->cpum.s.Guest.abXState)
                       , VERR_CPUM_IPE_2);

    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];

        pVCpu->cpum.s.Host.fXStateMask       = fXStateHostMask;
        pVCpu->cpum.s.hNestedVmxPreemptTimer = NIL_TMTIMERHANDLE;
    }

    /*
     * Register saved state data item.
     */
    rc = SSMR3RegisterInternal(pVM, "cpum", 1, CPUM_SAVED_STATE_VERSION, sizeof(CPUM),
                               NULL, cpumR3LiveExec, NULL,
                               NULL, cpumR3SaveExec, NULL,
                               cpumR3LoadPrep, cpumR3LoadExec, cpumR3LoadDone);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register info handlers and registers with the debugger facility.
     */
    DBGFR3InfoRegisterInternalEx(pVM, "cpum",             "Displays the all the cpu states.",
                                 &cpumR3InfoAll, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "cpumguest",        "Displays the guest cpu state.",
                                 &cpumR3InfoGuest, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "cpumguesthwvirt",   "Displays the guest hwvirt. cpu state.",
                                 &cpumR3InfoGuestHwvirt, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "cpumhyper",        "Displays the hypervisor cpu state.",
                                 &cpumR3InfoHyper, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "cpumhost",         "Displays the host cpu state.",
                                 &cpumR3InfoHost, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "cpumguestinstr",   "Displays the current guest instruction.",
                                 &cpumR3InfoGuestInstr, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternal(  pVM, "cpuid",            "Displays the guest cpuid leaves.",
                                 &cpumR3CpuIdInfo);
    DBGFR3InfoRegisterInternal(  pVM, "cpumvmxfeat",      "Displays the host and guest VMX hwvirt. features.",
                                 &cpumR3InfoVmxFeatures);

    rc = cpumR3DbgInit(pVM);
    if (RT_FAILURE(rc))
        return rc;

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    /*
     * Check if we need to workaround partial/leaky FPU handling.
     */
    cpumR3CheckLeakyFpu(pVM);
#endif

    /*
     * Initialize the Guest CPUID and MSR states.
     */
    rc = cpumR3InitCpuIdAndMsrs(pVM, &HostMsrs);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Generate the RFLAGS cookie.
     */
    pVM->cpum.s.fReservedRFlagsCookie = RTRandU64() & ~(CPUMX86EFLAGS_HW_MASK_64 | CPUMX86EFLAGS_INT_MASK_64);

    /*
     * Init the VMX/SVM state.
     *
     * This must be done after initializing CPUID/MSR features as we access the
     * the VMX/SVM guest features below.
     *
     * In the case of nested VT-x, we also need to create the per-VCPU
     * VMX preemption timers.
     */
    if (pVM->cpum.s.GuestFeatures.fVmx)
        cpumR3InitVmxHwVirtState(pVM);
    else if (pVM->cpum.s.GuestFeatures.fSvm)
        cpumR3InitSvmHwVirtState(pVM);
    else
        Assert(pVM->apCpusR3[0]->cpum.s.Guest.hwvirt.enmHwvirt == CPUMHWVIRT_NONE);

    /*
     * Initialize the general guest CPU state.
     */
    CPUMR3Reset(pVM);

    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * The CPUM will update the addresses used by the switcher.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(void) CPUMR3Relocate(PVM pVM)
{
    RT_NOREF(pVM);
}


/**
 * Terminates the CPUM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) CPUMR3Term(PVM pVM)
{
#ifdef VBOX_WITH_CRASHDUMP_MAGIC
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        memset(pVCpu->cpum.s.aMagic, 0, sizeof(pVCpu->cpum.s.aMagic));
        pVCpu->cpum.s.uMagic      = 0;
        pvCpu->cpum.s.Guest.dr[5] = 0;
    }
#endif

    if (pVM->cpum.s.GuestFeatures.fVmx)
    {
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU pVCpu = pVM->apCpusR3[idCpu];
            if (pVCpu->cpum.s.hNestedVmxPreemptTimer != NIL_TMTIMERHANDLE)
            {
                int rc = TMR3TimerDestroy(pVM, pVCpu->cpum.s.hNestedVmxPreemptTimer); AssertRC(rc);
                pVCpu->cpum.s.hNestedVmxPreemptTimer = NIL_TMTIMERHANDLE;
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * Resets a virtual CPU.
 *
 * Used by CPUMR3Reset and CPU hot plugging.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure of the CPU that is
 *                  being reset.  This may differ from the current EMT.
 */
VMMR3DECL(void) CPUMR3ResetCpu(PVM pVM, PVMCPU pVCpu)
{
    /** @todo anything different for VCPU > 0? */
    PCPUMCTX pCtx = &pVCpu->cpum.s.Guest;

    /*
     * Initialize everything to ZERO first.
     */
    uint32_t fUseFlags              =  pVCpu->cpum.s.fUseFlags & ~CPUM_USED_FPU_SINCE_REM;

    RT_BZERO(pCtx, RT_UOFFSETOF(CPUMCTX, aoffXState));

    pVCpu->cpum.s.fUseFlags         = fUseFlags;

    pCtx->cr0                       = X86_CR0_CD | X86_CR0_NW | X86_CR0_ET;  //0x60000010
    pCtx->eip                       = 0x0000fff0;
    pCtx->edx                       = 0x00000600;   /* P6 processor */

    Assert((pVM->cpum.s.fReservedRFlagsCookie & (X86_EFL_LIVE_MASK | X86_EFL_RAZ_LO_MASK | X86_EFL_RA1_MASK)) == 0);
    pCtx->rflags.uBoth              = pVM->cpum.s.fReservedRFlagsCookie | X86_EFL_RA1_MASK;

    pCtx->cs.Sel                    = 0xf000;
    pCtx->cs.ValidSel               = 0xf000;
    pCtx->cs.fFlags                 = CPUMSELREG_FLAGS_VALID;
    pCtx->cs.u64Base                = UINT64_C(0xffff0000);
    pCtx->cs.u32Limit               = 0x0000ffff;
    pCtx->cs.Attr.n.u1DescType      = 1; /* code/data segment */
    pCtx->cs.Attr.n.u1Present       = 1;
    pCtx->cs.Attr.n.u4Type          = X86_SEL_TYPE_ER_ACC;

    pCtx->ds.fFlags                 = CPUMSELREG_FLAGS_VALID;
    pCtx->ds.u32Limit               = 0x0000ffff;
    pCtx->ds.Attr.n.u1DescType      = 1; /* code/data segment */
    pCtx->ds.Attr.n.u1Present       = 1;
    pCtx->ds.Attr.n.u4Type          = X86_SEL_TYPE_RW_ACC;

    pCtx->es.fFlags                 = CPUMSELREG_FLAGS_VALID;
    pCtx->es.u32Limit               = 0x0000ffff;
    pCtx->es.Attr.n.u1DescType      = 1; /* code/data segment */
    pCtx->es.Attr.n.u1Present       = 1;
    pCtx->es.Attr.n.u4Type          = X86_SEL_TYPE_RW_ACC;

    pCtx->fs.fFlags                 = CPUMSELREG_FLAGS_VALID;
    pCtx->fs.u32Limit               = 0x0000ffff;
    pCtx->fs.Attr.n.u1DescType      = 1; /* code/data segment */
    pCtx->fs.Attr.n.u1Present       = 1;
    pCtx->fs.Attr.n.u4Type          = X86_SEL_TYPE_RW_ACC;

    pCtx->gs.fFlags                 = CPUMSELREG_FLAGS_VALID;
    pCtx->gs.u32Limit               = 0x0000ffff;
    pCtx->gs.Attr.n.u1DescType      = 1; /* code/data segment */
    pCtx->gs.Attr.n.u1Present       = 1;
    pCtx->gs.Attr.n.u4Type          = X86_SEL_TYPE_RW_ACC;

    pCtx->ss.fFlags                 = CPUMSELREG_FLAGS_VALID;
    pCtx->ss.u32Limit               = 0x0000ffff;
    pCtx->ss.Attr.n.u1Present       = 1;
    pCtx->ss.Attr.n.u1DescType      = 1; /* code/data segment */
    pCtx->ss.Attr.n.u4Type          = X86_SEL_TYPE_RW_ACC;

    pCtx->idtr.cbIdt                = 0xffff;
    pCtx->gdtr.cbGdt                = 0xffff;

    pCtx->ldtr.fFlags               = CPUMSELREG_FLAGS_VALID;
    pCtx->ldtr.u32Limit             = 0xffff;
    pCtx->ldtr.Attr.n.u1Present     = 1;
    pCtx->ldtr.Attr.n.u4Type        = X86_SEL_TYPE_SYS_LDT;

    pCtx->tr.fFlags                 = CPUMSELREG_FLAGS_VALID;
    pCtx->tr.u32Limit               = 0xffff;
    pCtx->tr.Attr.n.u1Present       = 1;
    pCtx->tr.Attr.n.u4Type          = X86_SEL_TYPE_SYS_386_TSS_BUSY;    /* Deduction, not properly documented by Intel. */

    pCtx->dr[6]                     = X86_DR6_INIT_VAL;
    pCtx->dr[7]                     = X86_DR7_INIT_VAL;

    PX86FXSTATE pFpuCtx = &pCtx->XState.x87;
    pFpuCtx->FTW                    = 0x00;         /* All empty (abbridged tag reg edition). */
    pFpuCtx->FCW                    = 0x37f;

    /* Intel 64 and IA-32 Architectures Software Developer's Manual Volume 3A, Table 8-1.
       IA-32 Processor States Following Power-up, Reset, or INIT */
    pFpuCtx->MXCSR                  = 0x1F80;
    pFpuCtx->MXCSR_MASK             = pVM->cpum.s.GuestInfo.fMxCsrMask; /** @todo check if REM messes this up... */

    pCtx->aXcr[0]                   = XSAVE_C_X87;
    if (pVM->cpum.s.HostFeatures.cbMaxExtendedState >= RT_UOFFSETOF(X86XSAVEAREA, Hdr))
    {
        /* The entire FXSAVE state needs loading when we switch to XSAVE/XRSTOR
           as we don't know what happened before.  (Bother optimize later?) */
        pCtx->XState.Hdr.bmXState   = XSAVE_C_X87 | XSAVE_C_SSE;
    }

    /*
     * MSRs.
     */
    /* Init PAT MSR */
    pCtx->msrPAT                    = MSR_IA32_CR_PAT_INIT_VAL;

    /* EFER MBZ; see AMD64 Architecture Programmer's Manual Volume 2: Table 14-1. Initial Processor State.
     * The Intel docs don't mention it. */
    Assert(!pCtx->msrEFER);

    /* IA32_MISC_ENABLE - not entirely sure what the init/reset state really
       is supposed to be here, just trying provide useful/sensible values. */
    PCPUMMSRRANGE pRange = cpumLookupMsrRange(pVM, MSR_IA32_MISC_ENABLE);
    if (pRange)
    {
        pVCpu->cpum.s.GuestMsrs.msr.MiscEnable = MSR_IA32_MISC_ENABLE_BTS_UNAVAIL
                                               | MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL
                                               | (pVM->cpum.s.GuestFeatures.fMonitorMWait ? MSR_IA32_MISC_ENABLE_MONITOR : 0)
                                               | MSR_IA32_MISC_ENABLE_FAST_STRINGS;
        pRange->fWrIgnMask |= MSR_IA32_MISC_ENABLE_BTS_UNAVAIL
                            | MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL;
        pRange->fWrGpMask  &= ~pVCpu->cpum.s.GuestMsrs.msr.MiscEnable;
    }

    /** @todo Wire IA32_MISC_ENABLE bit 22 to our NT 4 CPUID trick. */

    /** @todo r=ramshankar: Currently broken for SMP as TMCpuTickSet() expects to be
     *        called from each EMT while we're getting called by CPUMR3Reset()
     *        iteratively on the same thread. Fix later.  */
#if 0 /** @todo r=bird: This we will do in TM, not here. */
    /* TSC must be 0. Intel spec. Table 9-1. "IA-32 Processor States Following Power-up, Reset, or INIT." */
    CPUMSetGuestMsr(pVCpu, MSR_IA32_TSC, 0);
#endif


    /* C-state control. Guesses. */
    pVCpu->cpum.s.GuestMsrs.msr.PkgCStateCfgCtrl = 1 /*C1*/ | RT_BIT_32(25) | RT_BIT_32(26) | RT_BIT_32(27) | RT_BIT_32(28);
    /* For Nehalem+ and Atoms, the 0xE2 MSR (MSR_PKG_CST_CONFIG_CONTROL) is documented. For Core 2,
     * it's undocumented but exists as MSR_PMG_CST_CONFIG_CONTROL and has similar but not identical
     * functionality. The default value must be different due to incompatible write mask.
     */
    if (CPUMMICROARCH_IS_INTEL_CORE2(pVM->cpum.s.GuestFeatures.enmMicroarch))
        pVCpu->cpum.s.GuestMsrs.msr.PkgCStateCfgCtrl = 0x202a01;    /* From Mac Pro Harpertown, unlocked. */
    else if (pVM->cpum.s.GuestFeatures.enmMicroarch == kCpumMicroarch_Intel_Core_Yonah)
        pVCpu->cpum.s.GuestMsrs.msr.PkgCStateCfgCtrl = 0x26740c;    /* From MacBookPro1,1. */

    /*
     * Hardware virtualization state.
     */
    CPUMSetGuestGif(pCtx, true);
    Assert(!pVM->cpum.s.GuestFeatures.fVmx || !pVM->cpum.s.GuestFeatures.fSvm);   /* Paranoia. */
    if (pVM->cpum.s.GuestFeatures.fVmx)
        cpumR3ResetVmxHwVirtState(pVCpu);
    else if (pVM->cpum.s.GuestFeatures.fSvm)
        cpumR3ResetSvmHwVirtState(pVCpu);
}


/**
 * Resets the CPU.
 *
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(void) CPUMR3Reset(PVM pVM)
{
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        CPUMR3ResetCpu(pVM, pVCpu);

#ifdef VBOX_WITH_CRASHDUMP_MAGIC

        /* Magic marker for searching in crash dumps. */
        strcpy((char *)pVCpu->.cpum.s.aMagic, "CPUMCPU Magic");
        pVCpu->cpum.s.uMagic       = UINT64_C(0xDEADBEEFDEADBEEF);
        pVCpu->cpum.s.Guest->dr[5] = UINT64_C(0xDEADBEEFDEADBEEF);
#endif
    }
}




/**
 * Pass 0 live exec callback.
 *
 * @returns VINF_SSM_DONT_CALL_AGAIN.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 * @param   uPass               The pass (0).
 */
static DECLCALLBACK(int) cpumR3LiveExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass)
{
    AssertReturn(uPass == 0, VERR_SSM_UNEXPECTED_PASS);
    cpumR3SaveCpuId(pVM, pSSM);
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) cpumR3SaveExec(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Save.
     */
    SSMR3PutU32(pSSM, pVM->cCpus);
    SSMR3PutU32(pSSM, sizeof(pVM->apCpusR3[0]->cpum.s.GuestMsrs.msr));
    CPUMCTX DummyHyperCtx;
    RT_ZERO(DummyHyperCtx);
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU const   pVCpu   = pVM->apCpusR3[idCpu];
        PCPUMCTX const pGstCtx = &pVCpu->cpum.s.Guest;

        /** @todo ditch this the next time we change the saved state. */
        SSMR3PutStructEx(pSSM, &DummyHyperCtx,           sizeof(DummyHyperCtx),           0, g_aCpumCtxFields, NULL);

        uint64_t const fSavedRFlags = pGstCtx->rflags.uBoth;
        pGstCtx->rflags.uBoth &= CPUMX86EFLAGS_HW_MASK_64; /* Temporarily clear the non-hardware bits in RFLAGS while saving. */
        SSMR3PutStructEx(pSSM, pGstCtx,                  sizeof(*pGstCtx),                0, g_aCpumCtxFields, NULL);
        pGstCtx->rflags.uBoth  = fSavedRFlags;

        SSMR3PutStructEx(pSSM, &pGstCtx->XState.x87,     sizeof(pGstCtx->XState.x87),     0, g_aCpumX87Fields, NULL);
        if (pGstCtx->fXStateMask != 0)
            SSMR3PutStructEx(pSSM, &pGstCtx->XState.Hdr, sizeof(pGstCtx->XState.Hdr),     0, g_aCpumXSaveHdrFields, NULL);
        if (pGstCtx->fXStateMask & XSAVE_C_YMM)
        {
            PCX86XSAVEYMMHI pYmmHiCtx = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_YMM_BIT, PCX86XSAVEYMMHI);
            SSMR3PutStructEx(pSSM, pYmmHiCtx, sizeof(*pYmmHiCtx), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumYmmHiFields, NULL);
        }
        if (pGstCtx->fXStateMask & XSAVE_C_BNDREGS)
        {
            PCX86XSAVEBNDREGS pBndRegs = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_BNDREGS_BIT, PCX86XSAVEBNDREGS);
            SSMR3PutStructEx(pSSM, pBndRegs, sizeof(*pBndRegs), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumBndRegsFields, NULL);
        }
        if (pGstCtx->fXStateMask & XSAVE_C_BNDCSR)
        {
            PCX86XSAVEBNDCFG pBndCfg = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_BNDCSR_BIT, PCX86XSAVEBNDCFG);
            SSMR3PutStructEx(pSSM, pBndCfg, sizeof(*pBndCfg), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumBndCfgFields, NULL);
        }
        if (pGstCtx->fXStateMask & XSAVE_C_ZMM_HI256)
        {
            PCX86XSAVEZMMHI256 pZmmHi256 = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_ZMM_HI256_BIT, PCX86XSAVEZMMHI256);
            SSMR3PutStructEx(pSSM, pZmmHi256, sizeof(*pZmmHi256), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumZmmHi256Fields, NULL);
        }
        if (pGstCtx->fXStateMask & XSAVE_C_ZMM_16HI)
        {
            PCX86XSAVEZMM16HI pZmm16Hi = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_ZMM_16HI_BIT, PCX86XSAVEZMM16HI);
            SSMR3PutStructEx(pSSM, pZmm16Hi, sizeof(*pZmm16Hi), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumZmm16HiFields, NULL);
        }
        SSMR3PutU64(pSSM, pGstCtx->aPaePdpes[0].u);
        SSMR3PutU64(pSSM, pGstCtx->aPaePdpes[1].u);
        SSMR3PutU64(pSSM, pGstCtx->aPaePdpes[2].u);
        SSMR3PutU64(pSSM, pGstCtx->aPaePdpes[3].u);
        if (pVM->cpum.s.GuestFeatures.fSvm)
        {
            SSMR3PutU64(pSSM,    pGstCtx->hwvirt.svm.uMsrHSavePa);
            SSMR3PutGCPhys(pSSM, pGstCtx->hwvirt.svm.GCPhysVmcb);
            SSMR3PutU64(pSSM,    pGstCtx->hwvirt.svm.uPrevPauseTick);
            SSMR3PutU16(pSSM,    pGstCtx->hwvirt.svm.cPauseFilter);
            SSMR3PutU16(pSSM,    pGstCtx->hwvirt.svm.cPauseFilterThreshold);
            SSMR3PutBool(pSSM,   pGstCtx->hwvirt.svm.fInterceptEvents);
            SSMR3PutStructEx(pSSM, &pGstCtx->hwvirt.svm.HostState, sizeof(pGstCtx->hwvirt.svm.HostState), 0 /* fFlags */,
                             g_aSvmHwvirtHostState, NULL /* pvUser */);
            SSMR3PutMem(pSSM,   &pGstCtx->hwvirt.svm.Vmcb,           sizeof(pGstCtx->hwvirt.svm.Vmcb));
            SSMR3PutMem(pSSM,   &pGstCtx->hwvirt.svm.abMsrBitmap[0], sizeof(pGstCtx->hwvirt.svm.abMsrBitmap));
            SSMR3PutMem(pSSM,   &pGstCtx->hwvirt.svm.abIoBitmap[0],  sizeof(pGstCtx->hwvirt.svm.abIoBitmap));
            /* This is saved in the old VMCPUM_FF format.  Change if more flags are added. */
            SSMR3PutU32(pSSM,    pGstCtx->hwvirt.fSavedInhibit & CPUMCTX_INHIBIT_NMI ? CPUM_OLD_VMCPU_FF_BLOCK_NMIS : 0);
            SSMR3PutBool(pSSM,   pGstCtx->hwvirt.fGif);
        }
        if (pVM->cpum.s.GuestFeatures.fVmx)
        {
            SSMR3PutGCPhys(pSSM,   pGstCtx->hwvirt.vmx.GCPhysVmxon);
            SSMR3PutGCPhys(pSSM,   pGstCtx->hwvirt.vmx.GCPhysVmcs);
            SSMR3PutGCPhys(pSSM,   pGstCtx->hwvirt.vmx.GCPhysShadowVmcs);
            SSMR3PutBool(pSSM,     pGstCtx->hwvirt.vmx.fInVmxRootMode);
            SSMR3PutBool(pSSM,     pGstCtx->hwvirt.vmx.fInVmxNonRootMode);
            SSMR3PutBool(pSSM,     pGstCtx->hwvirt.vmx.fInterceptEvents);
            SSMR3PutBool(pSSM,     pGstCtx->hwvirt.vmx.fNmiUnblockingIret);
            SSMR3PutStructEx(pSSM, &pGstCtx->hwvirt.vmx.Vmcs, sizeof(pGstCtx->hwvirt.vmx.Vmcs), 0, g_aVmxHwvirtVmcs, NULL);
            SSMR3PutStructEx(pSSM, &pGstCtx->hwvirt.vmx.ShadowVmcs, sizeof(pGstCtx->hwvirt.vmx.ShadowVmcs),
                             0, g_aVmxHwvirtVmcs, NULL);
            SSMR3PutMem(pSSM,     &pGstCtx->hwvirt.vmx.abVmreadBitmap[0],    sizeof(pGstCtx->hwvirt.vmx.abVmreadBitmap));
            SSMR3PutMem(pSSM,     &pGstCtx->hwvirt.vmx.abVmwriteBitmap[0],   sizeof(pGstCtx->hwvirt.vmx.abVmwriteBitmap));
            SSMR3PutMem(pSSM,     &pGstCtx->hwvirt.vmx.aEntryMsrLoadArea[0], sizeof(pGstCtx->hwvirt.vmx.aEntryMsrLoadArea));
            SSMR3PutMem(pSSM,     &pGstCtx->hwvirt.vmx.aExitMsrStoreArea[0], sizeof(pGstCtx->hwvirt.vmx.aExitMsrStoreArea));
            SSMR3PutMem(pSSM,     &pGstCtx->hwvirt.vmx.aExitMsrLoadArea[0],  sizeof(pGstCtx->hwvirt.vmx.aExitMsrLoadArea));
            SSMR3PutMem(pSSM,     &pGstCtx->hwvirt.vmx.abMsrBitmap[0],       sizeof(pGstCtx->hwvirt.vmx.abMsrBitmap));
            SSMR3PutMem(pSSM,     &pGstCtx->hwvirt.vmx.abIoBitmap[0],        sizeof(pGstCtx->hwvirt.vmx.abIoBitmap));
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.uFirstPauseLoopTick);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.uPrevPauseTick);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.uEntryTick);
            SSMR3PutU16(pSSM,      pGstCtx->hwvirt.vmx.offVirtApicWrite);
            SSMR3PutBool(pSSM,     pGstCtx->hwvirt.vmx.fVirtNmiBlocking);
            SSMR3PutU64(pSSM,      MSR_IA32_FEATURE_CONTROL_LOCK | MSR_IA32_FEATURE_CONTROL_VMXON); /* Deprecated since 2021/09/22. Value kept backwards compatibile with 6.1.26. */
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64Basic);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.PinCtls.u);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.ProcCtls.u);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.ProcCtls2.u);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.ExitCtls.u);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.EntryCtls.u);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.TruePinCtls.u);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.TrueProcCtls.u);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.TrueEntryCtls.u);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.TrueExitCtls.u);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64Misc);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64Cr0Fixed0);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64Cr0Fixed1);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64Cr4Fixed0);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64Cr4Fixed1);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64VmcsEnum);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64VmFunc);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64EptVpidCaps);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64ProcCtls3);
            SSMR3PutU64(pSSM,      pGstCtx->hwvirt.vmx.Msrs.u64ExitCtls2);
        }
        SSMR3PutU32(pSSM, pVCpu->cpum.s.fUseFlags);
        SSMR3PutU32(pSSM, pVCpu->cpum.s.fChanged);
        AssertCompileSizeAlignment(pVCpu->cpum.s.GuestMsrs.msr, sizeof(uint64_t));
        SSMR3PutMem(pSSM, &pVCpu->cpum.s.GuestMsrs, sizeof(pVCpu->cpum.s.GuestMsrs.msr));
    }

    cpumR3SaveCpuId(pVM, pSSM);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMINTLOADPREP}
 */
static DECLCALLBACK(int) cpumR3LoadPrep(PVM pVM, PSSMHANDLE pSSM)
{
    NOREF(pSSM);
    pVM->cpum.s.fPendingRestore = true;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMINTLOADEXEC}
 */
static DECLCALLBACK(int) cpumR3LoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    int rc; /* Only for AssertRCReturn use. */

    /*
     * Validate version.
     */
    if (    uVersion != CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_4
        &&  uVersion != CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_3
        &&  uVersion != CPUM_SAVED_STATE_VERSION_PAE_PDPES
        &&  uVersion != CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2
        &&  uVersion != CPUM_SAVED_STATE_VERSION_HWVIRT_VMX
        &&  uVersion != CPUM_SAVED_STATE_VERSION_HWVIRT_SVM
        &&  uVersion != CPUM_SAVED_STATE_VERSION_XSAVE
        &&  uVersion != CPUM_SAVED_STATE_VERSION_GOOD_CPUID_COUNT
        &&  uVersion != CPUM_SAVED_STATE_VERSION_BAD_CPUID_COUNT
        &&  uVersion != CPUM_SAVED_STATE_VERSION_PUT_STRUCT
        &&  uVersion != CPUM_SAVED_STATE_VERSION_MEM
        &&  uVersion != CPUM_SAVED_STATE_VERSION_NO_MSR_SIZE
        &&  uVersion != CPUM_SAVED_STATE_VERSION_VER3_2
        &&  uVersion != CPUM_SAVED_STATE_VERSION_VER3_0
        &&  uVersion != CPUM_SAVED_STATE_VERSION_VER2_1_NOMSR
        &&  uVersion != CPUM_SAVED_STATE_VERSION_VER2_0
        &&  uVersion != CPUM_SAVED_STATE_VERSION_VER1_6)
    {
        AssertMsgFailed(("cpumR3LoadExec: Invalid version uVersion=%d!\n", uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    if (uPass == SSM_PASS_FINAL)
    {
        /*
         * Set the size of RTGCPTR for SSMR3GetGCPtr. (Only necessary for
         * really old SSM file versions.)
         */
        if (uVersion == CPUM_SAVED_STATE_VERSION_VER1_6)
            SSMR3HandleSetGCPtrSize(pSSM, sizeof(RTGCPTR32));
        else if (uVersion <= CPUM_SAVED_STATE_VERSION_VER3_0)
            SSMR3HandleSetGCPtrSize(pSSM, sizeof(RTGCPTR));

        /*
         * Figure x86 and ctx field definitions to use for older states.
         */
        uint32_t const  fLoad = uVersion > CPUM_SAVED_STATE_VERSION_MEM ? 0 : SSMSTRUCT_FLAGS_MEM_BAND_AID_RELAXED;
        PCSSMFIELD      paCpumCtx1Fields = g_aCpumX87Fields;
        PCSSMFIELD      paCpumCtx2Fields = g_aCpumCtxFields;
        if (uVersion == CPUM_SAVED_STATE_VERSION_VER1_6)
        {
            paCpumCtx1Fields = g_aCpumX87FieldsV16;
            paCpumCtx2Fields = g_aCpumCtxFieldsV16;
        }
        else if (uVersion <= CPUM_SAVED_STATE_VERSION_MEM)
        {
            paCpumCtx1Fields = g_aCpumX87FieldsMem;
            paCpumCtx2Fields = g_aCpumCtxFieldsMem;
        }

        /*
         * The hyper state used to preceed the CPU count.  Starting with
         * XSAVE it was moved down till after we've got the count.
         */
        CPUMCTX HyperCtxIgnored;
        if (uVersion < CPUM_SAVED_STATE_VERSION_XSAVE)
        {
            for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
            {
                X86FXSTATE Ign;
                SSMR3GetStructEx(pSSM, &Ign, sizeof(Ign), fLoad | SSMSTRUCT_FLAGS_NO_TAIL_MARKER, paCpumCtx1Fields, NULL);
                SSMR3GetStructEx(pSSM, &HyperCtxIgnored, sizeof(HyperCtxIgnored),
                                 fLoad | SSMSTRUCT_FLAGS_NO_LEAD_MARKER, paCpumCtx2Fields, NULL);
            }
        }

        if (uVersion >= CPUM_SAVED_STATE_VERSION_VER2_1_NOMSR)
        {
            uint32_t cCpus;
            rc = SSMR3GetU32(pSSM, &cCpus); AssertRCReturn(rc, rc);
            AssertLogRelMsgReturn(cCpus == pVM->cCpus, ("Mismatching CPU counts: saved: %u; configured: %u \n", cCpus, pVM->cCpus),
                                  VERR_SSM_UNEXPECTED_DATA);
        }
        AssertLogRelMsgReturn(   uVersion > CPUM_SAVED_STATE_VERSION_VER2_0
                              || pVM->cCpus == 1,
                              ("cCpus=%u\n", pVM->cCpus),
                              VERR_SSM_UNEXPECTED_DATA);

        uint32_t cbMsrs = 0;
        if (uVersion > CPUM_SAVED_STATE_VERSION_NO_MSR_SIZE)
        {
            rc = SSMR3GetU32(pSSM, &cbMsrs); AssertRCReturn(rc, rc);
            AssertLogRelMsgReturn(RT_ALIGN(cbMsrs, sizeof(uint64_t)) == cbMsrs, ("Size of MSRs is misaligned: %#x\n", cbMsrs),
                                  VERR_SSM_UNEXPECTED_DATA);
            AssertLogRelMsgReturn(cbMsrs <= sizeof(CPUMCTXMSRS) && cbMsrs > 0,  ("Size of MSRs is out of range: %#x\n", cbMsrs),
                                  VERR_SSM_UNEXPECTED_DATA);
        }

        /*
         * Do the per-CPU restoring.
         */
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU   pVCpu   = pVM->apCpusR3[idCpu];
            PCPUMCTX pGstCtx = &pVCpu->cpum.s.Guest;

            if (uVersion >= CPUM_SAVED_STATE_VERSION_XSAVE)
            {
                /*
                 * The XSAVE saved state layout moved the hyper state down here.
                 */
                rc = SSMR3GetStructEx(pSSM, &HyperCtxIgnored,         sizeof(HyperCtxIgnored),         0, g_aCpumCtxFields, NULL);
                AssertRCReturn(rc, rc);

                /*
                 * Start by restoring the CPUMCTX structure and the X86FXSAVE bits of the extended state.
                 */
                rc = SSMR3GetStructEx(pSSM, pGstCtx,                  sizeof(*pGstCtx),                0, g_aCpumCtxFields, NULL);
                rc = SSMR3GetStructEx(pSSM, &pGstCtx->XState.x87,     sizeof(pGstCtx->XState.x87),     0, g_aCpumX87Fields, NULL);
                AssertRCReturn(rc, rc);

                /* Check that the xsave/xrstor mask is valid (invalid results in #GP). */
                if (pGstCtx->fXStateMask != 0)
                {
                    AssertLogRelMsgReturn(!(pGstCtx->fXStateMask & ~pVM->cpum.s.fXStateGuestMask),
                                          ("fXStateMask=%#RX64 fXStateGuestMask=%#RX64\n",
                                           pGstCtx->fXStateMask, pVM->cpum.s.fXStateGuestMask),
                                          VERR_CPUM_INCOMPATIBLE_XSAVE_COMP_MASK);
                    AssertLogRelMsgReturn(pGstCtx->fXStateMask & XSAVE_C_X87,
                                          ("fXStateMask=%#RX64\n", pGstCtx->fXStateMask), VERR_CPUM_INVALID_XSAVE_COMP_MASK);
                    AssertLogRelMsgReturn((pGstCtx->fXStateMask & (XSAVE_C_SSE | XSAVE_C_YMM)) != XSAVE_C_YMM,
                                          ("fXStateMask=%#RX64\n", pGstCtx->fXStateMask), VERR_CPUM_INVALID_XSAVE_COMP_MASK);
                    AssertLogRelMsgReturn(   (pGstCtx->fXStateMask & (XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI)) == 0
                                          ||    (pGstCtx->fXStateMask & (XSAVE_C_SSE | XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI))
                                             ==                         (XSAVE_C_SSE | XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI),
                                          ("fXStateMask=%#RX64\n", pGstCtx->fXStateMask), VERR_CPUM_INVALID_XSAVE_COMP_MASK);
                }

                /* Check that the XCR0 mask is valid (invalid results in #GP). */
                AssertLogRelMsgReturn(pGstCtx->aXcr[0] & XSAVE_C_X87, ("xcr0=%#RX64\n", pGstCtx->aXcr[0]), VERR_CPUM_INVALID_XCR0);
                if (pGstCtx->aXcr[0] != XSAVE_C_X87)
                {
                    AssertLogRelMsgReturn(!(pGstCtx->aXcr[0] & ~(pGstCtx->fXStateMask | XSAVE_C_X87)),
                                          ("xcr0=%#RX64 fXStateMask=%#RX64\n", pGstCtx->aXcr[0], pGstCtx->fXStateMask),
                                          VERR_CPUM_INVALID_XCR0);
                    AssertLogRelMsgReturn(pGstCtx->aXcr[0] & XSAVE_C_X87,
                                          ("xcr0=%#RX64\n", pGstCtx->aXcr[0]), VERR_CPUM_INVALID_XSAVE_COMP_MASK);
                    AssertLogRelMsgReturn((pGstCtx->aXcr[0] & (XSAVE_C_SSE | XSAVE_C_YMM)) != XSAVE_C_YMM,
                                          ("xcr0=%#RX64\n", pGstCtx->aXcr[0]), VERR_CPUM_INVALID_XSAVE_COMP_MASK);
                    AssertLogRelMsgReturn(   (pGstCtx->aXcr[0] & (XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI)) == 0
                                          ||    (pGstCtx->aXcr[0] & (XSAVE_C_SSE | XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI))
                                             ==                     (XSAVE_C_SSE | XSAVE_C_YMM | XSAVE_C_OPMASK | XSAVE_C_ZMM_HI256 | XSAVE_C_ZMM_16HI),
                                          ("xcr0=%#RX64\n", pGstCtx->aXcr[0]), VERR_CPUM_INVALID_XSAVE_COMP_MASK);
                }

                /* Check that the XCR1 is zero, as we don't implement it yet. */
                AssertLogRelMsgReturn(!pGstCtx->aXcr[1], ("xcr1=%#RX64\n", pGstCtx->aXcr[1]), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

                /*
                 * Restore the individual extended state components we support.
                 */
                if (pGstCtx->fXStateMask != 0)
                {
                    rc = SSMR3GetStructEx(pSSM, &pGstCtx->XState.Hdr, sizeof(pGstCtx->XState.Hdr),
                                          0, g_aCpumXSaveHdrFields, NULL);
                    AssertRCReturn(rc, rc);
                    AssertLogRelMsgReturn(!(pGstCtx->XState.Hdr.bmXState & ~pGstCtx->fXStateMask),
                                          ("bmXState=%#RX64 fXStateMask=%#RX64\n",
                                           pGstCtx->XState.Hdr.bmXState, pGstCtx->fXStateMask),
                                          VERR_CPUM_INVALID_XSAVE_HDR);
                }
                if (pGstCtx->fXStateMask & XSAVE_C_YMM)
                {
                    PX86XSAVEYMMHI pYmmHiCtx = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_YMM_BIT, PX86XSAVEYMMHI);
                    SSMR3GetStructEx(pSSM, pYmmHiCtx, sizeof(*pYmmHiCtx), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumYmmHiFields, NULL);
                }
                if (pGstCtx->fXStateMask & XSAVE_C_BNDREGS)
                {
                    PX86XSAVEBNDREGS pBndRegs = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_BNDREGS_BIT, PX86XSAVEBNDREGS);
                    SSMR3GetStructEx(pSSM, pBndRegs, sizeof(*pBndRegs), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumBndRegsFields, NULL);
                }
                if (pGstCtx->fXStateMask & XSAVE_C_BNDCSR)
                {
                    PX86XSAVEBNDCFG pBndCfg = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_BNDCSR_BIT, PX86XSAVEBNDCFG);
                    SSMR3GetStructEx(pSSM, pBndCfg, sizeof(*pBndCfg), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumBndCfgFields, NULL);
                }
                if (pGstCtx->fXStateMask & XSAVE_C_ZMM_HI256)
                {
                    PX86XSAVEZMMHI256 pZmmHi256 = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_ZMM_HI256_BIT, PX86XSAVEZMMHI256);
                    SSMR3GetStructEx(pSSM, pZmmHi256, sizeof(*pZmmHi256), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumZmmHi256Fields, NULL);
                }
                if (pGstCtx->fXStateMask & XSAVE_C_ZMM_16HI)
                {
                    PX86XSAVEZMM16HI pZmm16Hi = CPUMCTX_XSAVE_C_PTR(pGstCtx, XSAVE_C_ZMM_16HI_BIT, PX86XSAVEZMM16HI);
                    SSMR3GetStructEx(pSSM, pZmm16Hi, sizeof(*pZmm16Hi), SSMSTRUCT_FLAGS_FULL_STRUCT, g_aCpumZmm16HiFields, NULL);
                }
                if (uVersion >= CPUM_SAVED_STATE_VERSION_PAE_PDPES)
                {
                    SSMR3GetU64(pSSM, &pGstCtx->aPaePdpes[0].u);
                    SSMR3GetU64(pSSM, &pGstCtx->aPaePdpes[1].u);
                    SSMR3GetU64(pSSM, &pGstCtx->aPaePdpes[2].u);
                    SSMR3GetU64(pSSM, &pGstCtx->aPaePdpes[3].u);
                }
                if (uVersion >= CPUM_SAVED_STATE_VERSION_HWVIRT_SVM)
                {
                    if (pVM->cpum.s.GuestFeatures.fSvm)
                    {
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.svm.uMsrHSavePa);
                        SSMR3GetGCPhys(pSSM,   &pGstCtx->hwvirt.svm.GCPhysVmcb);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.svm.uPrevPauseTick);
                        SSMR3GetU16(pSSM,      &pGstCtx->hwvirt.svm.cPauseFilter);
                        SSMR3GetU16(pSSM,      &pGstCtx->hwvirt.svm.cPauseFilterThreshold);
                        SSMR3GetBool(pSSM,     &pGstCtx->hwvirt.svm.fInterceptEvents);
                        SSMR3GetStructEx(pSSM, &pGstCtx->hwvirt.svm.HostState, sizeof(pGstCtx->hwvirt.svm.HostState),
                                         0 /* fFlags */, g_aSvmHwvirtHostState, NULL /* pvUser */);
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.svm.Vmcb,           sizeof(pGstCtx->hwvirt.svm.Vmcb));
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.svm.abMsrBitmap[0], sizeof(pGstCtx->hwvirt.svm.abMsrBitmap));
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.svm.abIoBitmap[0],  sizeof(pGstCtx->hwvirt.svm.abIoBitmap));

                        uint32_t fSavedLocalFFs = 0;
                        rc = SSMR3GetU32(pSSM,      &fSavedLocalFFs);
                        AssertRCReturn(rc, rc);
                        Assert(fSavedLocalFFs == 0 || fSavedLocalFFs == CPUM_OLD_VMCPU_FF_BLOCK_NMIS);
                        pGstCtx->hwvirt.fSavedInhibit = fSavedLocalFFs & CPUM_OLD_VMCPU_FF_BLOCK_NMIS ? CPUMCTX_INHIBIT_NMI : 0;

                        SSMR3GetBool(pSSM,     &pGstCtx->hwvirt.fGif);
                    }
                }
                if (uVersion >= CPUM_SAVED_STATE_VERSION_HWVIRT_VMX)
                {
                    if (pVM->cpum.s.GuestFeatures.fVmx)
                    {
                        SSMR3GetGCPhys(pSSM,   &pGstCtx->hwvirt.vmx.GCPhysVmxon);
                        SSMR3GetGCPhys(pSSM,   &pGstCtx->hwvirt.vmx.GCPhysVmcs);
                        SSMR3GetGCPhys(pSSM,   &pGstCtx->hwvirt.vmx.GCPhysShadowVmcs);
                        SSMR3GetBool(pSSM,     &pGstCtx->hwvirt.vmx.fInVmxRootMode);
                        SSMR3GetBool(pSSM,     &pGstCtx->hwvirt.vmx.fInVmxNonRootMode);
                        SSMR3GetBool(pSSM,     &pGstCtx->hwvirt.vmx.fInterceptEvents);
                        SSMR3GetBool(pSSM,     &pGstCtx->hwvirt.vmx.fNmiUnblockingIret);
                        SSMR3GetStructEx(pSSM, &pGstCtx->hwvirt.vmx.Vmcs, sizeof(pGstCtx->hwvirt.vmx.Vmcs),
                                         0, g_aVmxHwvirtVmcs, NULL);
                        SSMR3GetStructEx(pSSM, &pGstCtx->hwvirt.vmx.ShadowVmcs, sizeof(pGstCtx->hwvirt.vmx.ShadowVmcs),
                                         0, g_aVmxHwvirtVmcs, NULL);
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.vmx.abVmreadBitmap[0],    sizeof(pGstCtx->hwvirt.vmx.abVmreadBitmap));
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.vmx.abVmwriteBitmap[0],   sizeof(pGstCtx->hwvirt.vmx.abVmwriteBitmap));
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.vmx.aEntryMsrLoadArea[0], sizeof(pGstCtx->hwvirt.vmx.aEntryMsrLoadArea));
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.vmx.aExitMsrStoreArea[0], sizeof(pGstCtx->hwvirt.vmx.aExitMsrStoreArea));
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.vmx.aExitMsrLoadArea[0],  sizeof(pGstCtx->hwvirt.vmx.aExitMsrLoadArea));
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.vmx.abMsrBitmap[0],       sizeof(pGstCtx->hwvirt.vmx.abMsrBitmap));
                        SSMR3GetMem(pSSM,      &pGstCtx->hwvirt.vmx.abIoBitmap[0],        sizeof(pGstCtx->hwvirt.vmx.abIoBitmap));
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.uFirstPauseLoopTick);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.uPrevPauseTick);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.uEntryTick);
                        SSMR3GetU16(pSSM,      &pGstCtx->hwvirt.vmx.offVirtApicWrite);
                        SSMR3GetBool(pSSM,     &pGstCtx->hwvirt.vmx.fVirtNmiBlocking);
                        SSMR3Skip(pSSM,        sizeof(uint64_t)); /* Unused - used to be IA32_FEATURE_CONTROL, see @bugref{10106}. */
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.u64Basic);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.PinCtls.u);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.ProcCtls.u);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.ProcCtls2.u);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.ExitCtls.u);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.EntryCtls.u);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.TruePinCtls.u);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.TrueProcCtls.u);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.TrueEntryCtls.u);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.TrueExitCtls.u);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.u64Misc);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.u64Cr0Fixed0);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.u64Cr0Fixed1);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.u64Cr4Fixed0);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.u64Cr4Fixed1);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.u64VmcsEnum);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.u64VmFunc);
                        SSMR3GetU64(pSSM,      &pGstCtx->hwvirt.vmx.Msrs.u64EptVpidCaps);
                        if (uVersion >= CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_2)
                            SSMR3GetU64(pSSM,  &pGstCtx->hwvirt.vmx.Msrs.u64ProcCtls3);
                        if (uVersion >= CPUM_SAVED_STATE_VERSION_HWVIRT_VMX_3)
                            SSMR3GetU64(pSSM,  &pGstCtx->hwvirt.vmx.Msrs.u64ExitCtls2);
                    }
                }
            }
            else
            {
                /*
                 * Pre XSAVE saved state.
                 */
                SSMR3GetStructEx(pSSM, &pGstCtx->XState.x87, sizeof(pGstCtx->XState.x87),
                                 fLoad | SSMSTRUCT_FLAGS_NO_TAIL_MARKER, paCpumCtx1Fields, NULL);
                SSMR3GetStructEx(pSSM, pGstCtx, sizeof(*pGstCtx), fLoad | SSMSTRUCT_FLAGS_NO_LEAD_MARKER, paCpumCtx2Fields, NULL);
            }

            /*
             * Restore a couple of flags and the MSRs.
             */
            uint32_t fIgnoredUsedFlags = 0;
            rc = SSMR3GetU32(pSSM, &fIgnoredUsedFlags); /* we're recalc the two relevant flags after loading state. */
            AssertRCReturn(rc, rc);
            SSMR3GetU32(pSSM, &pVCpu->cpum.s.fChanged);

            rc = VINF_SUCCESS;
            if (uVersion > CPUM_SAVED_STATE_VERSION_NO_MSR_SIZE)
                rc = SSMR3GetMem(pSSM, &pVCpu->cpum.s.GuestMsrs.au64[0], cbMsrs);
            else if (uVersion >= CPUM_SAVED_STATE_VERSION_VER3_0)
            {
                SSMR3GetMem(pSSM, &pVCpu->cpum.s.GuestMsrs.au64[0], 2 * sizeof(uint64_t)); /* Restore two MSRs. */
                rc = SSMR3Skip(pSSM, 62 * sizeof(uint64_t));
            }
            AssertRCReturn(rc, rc);

            /* Deal with the reusing of reserved RFLAGS bits. */
            pGstCtx->rflags.uBoth |= pVM->cpum.s.fReservedRFlagsCookie;

            /* REM and other may have cleared must-be-one fields in DR6 and
               DR7, fix these. */
            pGstCtx->dr[6] &= ~(X86_DR6_RAZ_MASK | X86_DR6_MBZ_MASK);
            pGstCtx->dr[6] |= X86_DR6_RA1_MASK;
            pGstCtx->dr[7] &= ~(X86_DR7_RAZ_MASK | X86_DR7_MBZ_MASK);
            pGstCtx->dr[7] |= X86_DR7_RA1_MASK;
        }

        /* Older states does not have the internal selector register flags
           and valid selector value.  Supply those. */
        if (uVersion <= CPUM_SAVED_STATE_VERSION_MEM)
        {
            for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
            {
                PVMCPU      pVCpu  = pVM->apCpusR3[idCpu];
                bool const  fValid = true /*!VM_IS_RAW_MODE_ENABLED(pVM)*/
                                  || (   uVersion > CPUM_SAVED_STATE_VERSION_VER3_2
                                      && !(pVCpu->cpum.s.fChanged & CPUM_CHANGED_HIDDEN_SEL_REGS_INVALID));
                PCPUMSELREG paSelReg = CPUMCTX_FIRST_SREG(&pVCpu->cpum.s.Guest);
                if (fValid)
                {
                    for (uint32_t iSelReg = 0; iSelReg < X86_SREG_COUNT; iSelReg++)
                    {
                        paSelReg[iSelReg].fFlags   = CPUMSELREG_FLAGS_VALID;
                        paSelReg[iSelReg].ValidSel = paSelReg[iSelReg].Sel;
                    }

                    pVCpu->cpum.s.Guest.ldtr.fFlags   = CPUMSELREG_FLAGS_VALID;
                    pVCpu->cpum.s.Guest.ldtr.ValidSel = pVCpu->cpum.s.Guest.ldtr.Sel;
                }
                else
                {
                    for (uint32_t iSelReg = 0; iSelReg < X86_SREG_COUNT; iSelReg++)
                    {
                        paSelReg[iSelReg].fFlags   = 0;
                        paSelReg[iSelReg].ValidSel = 0;
                    }

                    /* This might not be 104% correct, but I think it's close
                       enough for all practical purposes...  (REM always loaded
                       LDTR registers.) */
                    pVCpu->cpum.s.Guest.ldtr.fFlags   = CPUMSELREG_FLAGS_VALID;
                    pVCpu->cpum.s.Guest.ldtr.ValidSel = pVCpu->cpum.s.Guest.ldtr.Sel;
                }
                pVCpu->cpum.s.Guest.tr.fFlags     = CPUMSELREG_FLAGS_VALID;
                pVCpu->cpum.s.Guest.tr.ValidSel   = pVCpu->cpum.s.Guest.tr.Sel;
            }
        }

        /* Clear CPUM_CHANGED_HIDDEN_SEL_REGS_INVALID. */
        if (   uVersion >  CPUM_SAVED_STATE_VERSION_VER3_2
            && uVersion <= CPUM_SAVED_STATE_VERSION_MEM)
            for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
            {
                PVMCPU pVCpu = pVM->apCpusR3[idCpu];
                pVCpu->cpum.s.fChanged &= CPUM_CHANGED_HIDDEN_SEL_REGS_INVALID;
            }

        /*
         * A quick sanity check.
         */
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU pVCpu = pVM->apCpusR3[idCpu];
            AssertLogRelReturn(!(pVCpu->cpum.s.Guest.es.fFlags & ~CPUMSELREG_FLAGS_VALID_MASK), VERR_SSM_UNEXPECTED_DATA);
            AssertLogRelReturn(!(pVCpu->cpum.s.Guest.cs.fFlags & ~CPUMSELREG_FLAGS_VALID_MASK), VERR_SSM_UNEXPECTED_DATA);
            AssertLogRelReturn(!(pVCpu->cpum.s.Guest.ss.fFlags & ~CPUMSELREG_FLAGS_VALID_MASK), VERR_SSM_UNEXPECTED_DATA);
            AssertLogRelReturn(!(pVCpu->cpum.s.Guest.ds.fFlags & ~CPUMSELREG_FLAGS_VALID_MASK), VERR_SSM_UNEXPECTED_DATA);
            AssertLogRelReturn(!(pVCpu->cpum.s.Guest.fs.fFlags & ~CPUMSELREG_FLAGS_VALID_MASK), VERR_SSM_UNEXPECTED_DATA);
            AssertLogRelReturn(!(pVCpu->cpum.s.Guest.gs.fFlags & ~CPUMSELREG_FLAGS_VALID_MASK), VERR_SSM_UNEXPECTED_DATA);
        }
    }

    pVM->cpum.s.fPendingRestore = false;

    /*
     * Guest CPUIDs (and VMX MSR features).
     */
    if (uVersion >= CPUM_SAVED_STATE_VERSION_VER3_2)
    {
        CPUMMSRS GuestMsrs;
        RT_ZERO(GuestMsrs);

        CPUMFEATURES BaseFeatures;
        bool const fVmxGstFeat = pVM->cpum.s.GuestFeatures.fVmx;
        if (fVmxGstFeat)
        {
            /*
             * At this point the MSRs in the guest CPU-context are loaded with the guest VMX MSRs from the saved state.
             * However the VMX sub-features have not been exploded yet. So cache the base (host derived) VMX features
             * here so we can compare them for compatibility after exploding guest features.
             */
            BaseFeatures = pVM->cpum.s.GuestFeatures;

            /* Use the VMX MSR features from the saved state while exploding guest features. */
            GuestMsrs.hwvirt.vmx = pVM->apCpusR3[0]->cpum.s.Guest.hwvirt.vmx.Msrs;
        }

        /* Load CPUID and explode guest features. */
        rc = cpumR3LoadCpuId(pVM, pSSM, uVersion, &GuestMsrs);
        if (fVmxGstFeat)
        {
            /*
             * Check if the exploded VMX features from the saved state are compatible with the host-derived features
             * we cached earlier (above). The is required if we use hardware-assisted nested-guest execution with
             * VMX features presented to the guest.
             */
            bool const fIsCompat = cpumR3AreVmxCpuFeaturesCompatible(pVM, &BaseFeatures, &pVM->cpum.s.GuestFeatures);
            if (!fIsCompat)
                return VERR_CPUM_INVALID_HWVIRT_FEAT_COMBO;
        }
        return rc;
    }
    return cpumR3LoadCpuIdPre32(pVM, pSSM, uVersion);
}


/**
 * @callback_method_impl{FNSSMINTLOADDONE}
 */
static DECLCALLBACK(int) cpumR3LoadDone(PVM pVM, PSSMHANDLE pSSM)
{
    if (RT_FAILURE(SSMR3HandleGetStatus(pSSM)))
        return VINF_SUCCESS;

    /* just check this since we can. */ /** @todo Add a SSM unit flag for indicating that it's mandatory during a restore.  */
    if (pVM->cpum.s.fPendingRestore)
    {
        LogRel(("CPUM: Missing state!\n"));
        return VERR_INTERNAL_ERROR_2;
    }

    bool const fSupportsLongMode = VMR3IsLongModeAllowed(pVM);
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        /* Notify PGM of the NXE states in case they've changed. */
        PGMNotifyNxeChanged(pVCpu, RT_BOOL(pVCpu->cpum.s.Guest.msrEFER & MSR_K6_EFER_NXE));

        /* During init. this is done in CPUMR3InitCompleted(). */
        if (fSupportsLongMode)
            pVCpu->cpum.s.fUseFlags |= CPUM_USE_SUPPORTS_LONGMODE;

        /* Recalc the CPUM_USE_DEBUG_REGS_HYPER value. */
        CPUMRecalcHyperDRx(pVCpu, UINT8_MAX);
    }
    return VINF_SUCCESS;
}


/**
 * Checks if the CPUM state restore is still pending.
 *
 * @returns true / false.
 * @param   pVM                 The cross context VM structure.
 */
VMMDECL(bool) CPUMR3IsStateRestorePending(PVM pVM)
{
    return pVM->cpum.s.fPendingRestore;
}


/**
 * Gets the variable-range MTRR physical address mask given an address range.
 *
 * @returns The MTRR physical address mask.
 * @param   pVM             The cross context VM structure.
 * @param   GCPhysFirst     The first guest-physical address of the memory range
 *                          (inclusive).
 * @param   GCPhysLast      The last guest-physical address of the memory range
 *                          (inclusive).
 */
static uint64_t cpumR3GetVarMtrrMask(PVM pVM, RTGCPHYS GCPhysFirst, RTGCPHYS GCPhysLast)
{
    RTGCPHYS const GCPhysLength  = GCPhysLast - GCPhysFirst;
    uint64_t const fInvPhysMask  = ~(RT_BIT_64(pVM->cpum.s.GuestFeatures.cMaxPhysAddrWidth) - 1U);
    RTGCPHYS const GCPhysMask    = (~(GCPhysLength - 1) & ~fInvPhysMask) & X86_PAGE_BASE_MASK;
#ifdef VBOX_STRICT
    AssertMsg(GCPhysLast == ((GCPhysFirst | ~GCPhysMask) & ~fInvPhysMask),
              ("last=%RGp first=%RGp mask=%RGp inv_mask=%RGp\n", GCPhysLast, GCPhysFirst, GCPhysMask, fInvPhysMask));
    AssertMsg(((GCPhysLast & GCPhysMask) == (GCPhysFirst & GCPhysMask)),
              ("last=%RGp first=%RGp mask=%RGp inv_mask=%RGp\n", GCPhysLast, GCPhysFirst, GCPhysMask, fInvPhysMask));
    AssertMsg(((GCPhysLast + 1) & GCPhysMask) != (GCPhysFirst & GCPhysMask),
              ("last=%RGp first=%RGp mask=%RGp inv_mask=%RGp\n", GCPhysLast, GCPhysFirst, GCPhysMask, fInvPhysMask));

    uint64_t const cbRange = GCPhysLast - GCPhysFirst + 1;
    AssertMsg(cbRange >= _4K, ("last=%RGp first=%RGp mask=%RGp inv_mask=%RGp cb=%RU64\n",
                               GCPhysLast, GCPhysFirst, GCPhysMask, fInvPhysMask, cbRange));
    AssertMsg(RT_IS_POWER_OF_TWO(cbRange), ("last=%RGp first=%RGp mask=%RGp inv_mask=%RGp cb=%RU64\n",
                                            GCPhysLast, GCPhysFirst, GCPhysMask, fInvPhysMask, cbRange));
    AssertMsg(GCPhysFirst == 0 || cbRange <= GCPhysFirst, ("last=%RGp first=%RGp mask=%RGp inv_mask=%RGp cb=%RU64\n",
                                                           GCPhysLast, GCPhysFirst, GCPhysMask, fInvPhysMask, cbRange));
#endif
    return GCPhysMask;
}


/**
 * Gets the first and last guest-physical address for the given variable-range
 * MTRR.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pMtrrVar        The variable-range MTRR.
 * @param   pGCPhysFirst    Where to store the first guest-physical address of the
 *                          memory range (inclusive).
 * @param   pGCPhysLast     Where to store the last guest-physical address of the
 *                          memory range (inclusive).
 */
static void cpumR3GetVarMtrrAddrs(PVM pVM, PCX86MTRRVAR pMtrrVar, PRTGCPHYS pGCPhysFirst, PRTGCPHYS pGCPhysLast)
{
    Assert(pMtrrVar);
    Assert(pGCPhysFirst);
    Assert(pGCPhysLast);
    uint64_t const fInvPhysMask = ~(RT_BIT_64(pVM->cpum.s.GuestFeatures.cMaxPhysAddrWidth) - 1U);
    RTGCPHYS const GCPhysMask   = pMtrrVar->MtrrPhysMask & X86_PAGE_BASE_MASK;
    RTGCPHYS const GCPhysFirst  = pMtrrVar->MtrrPhysBase & X86_PAGE_BASE_MASK;
    RTGCPHYS const GCPhysLast   = (GCPhysFirst | ~GCPhysMask) & ~fInvPhysMask;
    Assert((GCPhysLast & GCPhysMask)       == (GCPhysFirst & GCPhysMask));
    Assert(((GCPhysLast + 1) & GCPhysMask) != (GCPhysFirst & GCPhysMask));
    *pGCPhysFirst = GCPhysFirst;
    *pGCPhysLast  = GCPhysLast;
}


/**
 * Gets the previous power of two for a given value.
 *
 * @returns Previous power of two.
 * @param   uVal  The value (must not be zero).
 */
static uint64_t cpumR3GetPrevPowerOfTwo(uint64_t uVal)
{
    Assert(uVal > 1);
    uint8_t const cBits = sizeof(uVal) << 3;
    return RT_BIT_64(cBits - 1 - ASMCountLeadingZerosU64(uVal));
}


/**
 * Gets the next power of two for a given value.
 *
 * @returns Next power of two.
 * @param   uVal  The value (must not be zero).
 */
static uint64_t cpumR3GetNextPowerOfTwo(uint64_t uVal)
{
    Assert(uVal > 1);
    uint8_t const cBits = sizeof(uVal) << 3;
    return RT_BIT_64(cBits - ASMCountLeadingZerosU64(uVal));
}


/**
 * Gets the MTRR memory type description.
 *
 * @returns The MTRR memory type description.
 * @param   fType   The MTRR memory type.
 */
static const char *cpumR3GetVarMtrrMemType(uint8_t fType)
{
    switch (fType)
    {
        case X86_MTRR_MT_UC: return "UC";
        case X86_MTRR_MT_WC: return "WC";
        case X86_MTRR_MT_WT: return "WT";
        case X86_MTRR_MT_WP: return "WP";
        case X86_MTRR_MT_WB: return "WB";
        default:             return "--";
    }
}


/**
 * Adds a memory region to the given MTRR map.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the map could accommodate a memory region being
 *          added.
 * @retval  VERR_OUT_OF_RESOURCES when the map ran out of room while adding the
 *          memory region.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pMtrrMap        The variable-range MTRR map to add to.
 * @param   GCPhysFirst     The first guest-physical address in the memory region.
 * @param   GCPhysLast      The last guest-physical address in the memory region.
 * @param   fType           The MTRR memory type of the memory region being added.
 */
static int cpumR3MtrrMapAddRegion(PVM pVM, PCPUMMTRRMAP pMtrrMap, RTGCPHYS GCPhysFirst, RTGCPHYS GCPhysLast, uint8_t fType)
{
    Assert(fType < 7 && fType != 2 && fType != 3);
    if (pMtrrMap->idxMtrr < pMtrrMap->cMtrrs)
    {
        /*
         * We must ensure the physical-address does not exceed the maximum guest-physical address width.
         * Otherwise, the MTRR physical mask computation gets totally busted rather than returning 0 to
         * indicate such mapping is impossible.
         */
        RTGCPHYS const GCPhysLastMax = RT_BIT_64(pVM->cpum.s.GuestFeatures.cMaxPhysAddrWidth) - 1U;
        if (GCPhysLast <= GCPhysLastMax)
        {
            pMtrrMap->aMtrrs[pMtrrMap->idxMtrr].MtrrPhysBase = GCPhysFirst | fType;
            pMtrrMap->aMtrrs[pMtrrMap->idxMtrr].MtrrPhysMask = cpumR3GetVarMtrrMask(pVM, GCPhysFirst, GCPhysLast)
                                                             | MSR_IA32_MTRR_PHYSMASK_VALID;
            ++pMtrrMap->idxMtrr;

            uint64_t const cbRange = GCPhysLast - GCPhysFirst + 1;
            if (fType != X86_MTRR_MT_UC)
                pMtrrMap->cbMapped += cbRange;
            else
            {
                Assert(pMtrrMap->cbMapped >= cbRange);
                pMtrrMap->cbMapped -= cbRange;
            }
            return VINF_SUCCESS;
        }
    }
    return VERR_OUT_OF_RESOURCES;
}


/**
 * Adds an MTRR to the given MTRR map.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the map could accommodate the MTRR being added.
 * @retval  VERR_OUT_OF_RESOURCES when the map ran out of room while adding the
 *          MTRR.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pMtrrMap    The variable-range MTRR map to add to.
 * @param   pVarMtrr    The variable-range MTRR to add from.
 */
static int cpumR3MtrrMapAddMtrr(PVM pVM, PCPUMMTRRMAP pMtrrMap, PCX86MTRRVAR pVarMtrr)
{
    RTGCPHYS GCPhysFirst;
    RTGCPHYS GCPhysLast;
    cpumR3GetVarMtrrAddrs(pVM, pVarMtrr, &GCPhysFirst, &GCPhysLast);
    uint8_t const fType = pVarMtrr->MtrrPhysBase & MSR_IA32_MTRR_PHYSBASE_MT_MASK;
    return cpumR3MtrrMapAddRegion(pVM, pMtrrMap, GCPhysFirst, GCPhysLast, fType);
}


/**
 * Adds a source MTRR map to the given destination MTRR map.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the map could fully accommodate the map being added.
 * @retval  VERR_OUT_OF_RESOURCES when the map ran out of room while adding the
 *          specified map.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pMtrrMapDst     The variable-range MTRR map to add to (destination).
 * @param   pMtrrMapSrc     The variable-range MTRR map to add from (source).
 */
static int cpumR3MtrrMapAddMap(PVM pVM, PCPUMMTRRMAP pMtrrMapDst, PCCPUMMTRRMAP pMtrrMapSrc)
{
    Assert(pMtrrMapDst);
    Assert(pMtrrMapSrc);
    for (uint8_t i = 0 ; i < pMtrrMapSrc->idxMtrr; i++)
    {
        int const rc = cpumR3MtrrMapAddMtrr(pVM, pMtrrMapDst, &pMtrrMapSrc->aMtrrs[i]);
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Maps memory using an additive method using variable-range MTRRs.
 *
 * The additive method fits as many valid MTRR WB (write-back) sub-regions to map
 * the specified memory size. For instance, 3584 MB is mapped as 2048 MB, 1024 MB
 * and 512 MB of WB memory, requiring 3 MTRRs.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the requested memory could be fully mapped within the
 *          given number of MTRRs.
 * @retval  VERR_OUT_OF_RESOURCES when the requested memory could not be fully
 *          mapped within the given number of MTRRs.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhysRegionFirst   The guest-physical address in the region being
 *                              mapped.
 * @param   cb                  The number of bytes being mapped.
 * @param   pMtrrMap            The variable-range MTRR map to populate.
 */
static int cpumR3MapMtrrsAdditive(PVM pVM, RTGCPHYS GCPhysRegionFirst, uint64_t cb, PCPUMMTRRMAP pMtrrMap)
{
    Assert(pMtrrMap);
    Assert(pMtrrMap->cMtrrs > 1);
    Assert(cb >= _4K);
    Assert(!(GCPhysRegionFirst & X86_PAGE_4K_OFFSET_MASK));

    uint64_t cbLeft    = cb;
    uint64_t offRegion = GCPhysRegionFirst;
    while (cbLeft > 0)
    {
        uint64_t const cbRegion = !RT_IS_POWER_OF_TWO(cbLeft) ? cpumR3GetPrevPowerOfTwo(cbLeft) : cbLeft;

        Log3(("CPUM: MTRR: Add[%u]: %Rhcb (%RU64 bytes)\n", pMtrrMap->idxMtrr, cbRegion, cbRegion));
        int const rc = cpumR3MtrrMapAddRegion(pVM, pMtrrMap, offRegion, offRegion + cbRegion - 1, X86_MTRR_MT_WB);
        if (RT_FAILURE(rc))
            return rc;

        cbLeft -= RT_MIN(cbRegion, cbLeft);
        offRegion += cbRegion;
    }
    return VINF_SUCCESS;
}


/**
 * Maps memory using a subtractive method using variable-range MTRRs.
 *
 * The subtractive method rounds up the memory region using WB (write-back) memory
 * type and then "subtracts" sub-regions using UC (uncacheable) memory type. For
 * instance, 3584 MB is mapped as 4096 MB of WB minus 512 MB of UC, requiring 2
 * MTRRs.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the requested memory could be fully mapped within the
 *          given number of MTRRs.
 * @retval  VERR_OUT_OF_RESOURCES when the requested memory could not be fully
 *          mapped within the given number of MTRRs.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhysRegionFirst   The guest-physical address in the region being
 *                              mapped.
 * @param   cb                  The number of bytes being mapped.
 * @param   pMtrrMap            The variable-range MTRR map to populate.
 */
static int cpumR3MapMtrrsSubtractive(PVM pVM, RTGCPHYS GCPhysRegionFirst, uint64_t cb, PCPUMMTRRMAP pMtrrMap)
{
    Assert(pMtrrMap);
    Assert(pMtrrMap->cMtrrs > 1);
    Assert(cb >= _4K);
    Assert(!(GCPhysRegionFirst & X86_PAGE_4K_OFFSET_MASK));

    uint64_t const cbRegion = !RT_IS_POWER_OF_TWO(cb) ? cpumR3GetNextPowerOfTwo(cb) : cb;
    Assert(cbRegion >= cb);

    Log3(("CPUM: MTRR: Sub[%u]: %Rhcb (%RU64 bytes) [WB]\n", pMtrrMap->idxMtrr, cbRegion, cbRegion));
    int rc = cpumR3MtrrMapAddRegion(pVM, pMtrrMap, GCPhysRegionFirst, GCPhysRegionFirst + cbRegion - 1, X86_MTRR_MT_WB);
    if (RT_FAILURE(rc))
        return rc;

    uint64_t cbLeft = cbRegion - cb;
    RTGCPHYS offRegion = GCPhysRegionFirst + cbRegion;
    while (cbLeft > 0)
    {
        uint64_t const cbSubRegion = cpumR3GetPrevPowerOfTwo(cbLeft);

        Log3(("CPUM: MTRR: Sub[%u]: %Rhcb (%RU64 bytes) [UC]\n", pMtrrMap->idxMtrr, cbSubRegion, cbSubRegion));
        rc = cpumR3MtrrMapAddRegion(pVM, pMtrrMap, offRegion - cbSubRegion, offRegion - 1, X86_MTRR_MT_UC);
        if (RT_FAILURE(rc))
            return rc;

        cbLeft -= RT_MIN(cbSubRegion, cbLeft);
        offRegion -= cbSubRegion;
    }
    return rc;
}


/**
 * Optimally maps RAM when it's not necessarily aligned to a power of two using
 * variable-range MTRRs.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the requested memory could be fully mapped within the
 *          given number of MTRRs.
 * @retval  VERR_OUT_OF_RESOURCES when the requested memory could not be fully
 *          mapped within the given number of MTRRs.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhysRegionFirst   The guest-physical address in the region being
 *                              mapped.
 * @param   cb                  The number of bytes being mapped.
 * @param   pMtrrMap            The variable-range MTRR map to populate.
 */
static int cpumR3MapMtrrsOptimal(PVM pVM, RTGCPHYS GCPhysRegionFirst, uint64_t cb, PCPUMMTRRMAP pMtrrMap)
{
    Assert(pMtrrMap);
    Assert(pMtrrMap->cMtrrs > 1);
    Assert(cb >= _4K);
    Assert(!(GCPhysRegionFirst & X86_PAGE_4K_OFFSET_MASK));

    /*
     * Additive method.
     */
    CPUMMTRRMAP MtrrMapAdd;
    RT_ZERO(MtrrMapAdd);
    MtrrMapAdd.cMtrrs  = pMtrrMap->cMtrrs;
    MtrrMapAdd.cbToMap = cb;
    int rcAdd;
    {
        rcAdd = cpumR3MapMtrrsAdditive(pVM, GCPhysRegionFirst, cb, &MtrrMapAdd);
        if (RT_SUCCESS(rcAdd))
        {
            Assert(MtrrMapAdd.idxMtrr > 0);
            Assert(MtrrMapAdd.idxMtrr <= MtrrMapAdd.cMtrrs);
            Assert(MtrrMapAdd.cbMapped == MtrrMapAdd.cbToMap);
            Log3(("CPUM: MTRR: Mapped %u regions using additive method\n", MtrrMapAdd.idxMtrr));

            /*
             * If we were able to map memory using 2 or fewer MTRRs, don't bother with trying
             * to map using the subtractive method as that requires at least 2 MTRRs anyway.
             */
            if (MtrrMapAdd.idxMtrr <= 2)
                return cpumR3MtrrMapAddMap(pVM, pMtrrMap, &MtrrMapAdd);
        }
        else
            Log3(("CPUM: MTRR: Partially mapped %u regions using additive method\n", MtrrMapAdd.idxMtrr));
    }

    /*
     * Subtractive method.
     */
    CPUMMTRRMAP MtrrMapSub;
    RT_ZERO(MtrrMapSub);
    MtrrMapSub.cMtrrs  = pMtrrMap->cMtrrs;
    MtrrMapSub.cbToMap = cb;
    int rcSub;
    {
        rcSub = cpumR3MapMtrrsSubtractive(pVM, GCPhysRegionFirst, cb, &MtrrMapSub);
        if (RT_SUCCESS(rcSub))
        {
            Assert(MtrrMapSub.idxMtrr > 0);
            Assert(MtrrMapSub.idxMtrr <= MtrrMapSub.cMtrrs);
            Assert(MtrrMapSub.cbMapped == MtrrMapSub.cbToMap);
            Log3(("CPUM: MTRR: Mapped %u regions using subtractive method\n", MtrrMapSub.idxMtrr));
        }
        else
            Log3(("CPUM: MTRR: Partially mapped %u regions using subtractive method\n", MtrrMapAdd.idxMtrr));
    }

    /*
     * Pick whichever method requires fewer MTRRs to map the memory.
     */
    PCCPUMMTRRMAP pMtrrMapOptimal;
    if (   RT_SUCCESS(rcAdd)
        && RT_SUCCESS(rcSub))
    {
        Assert(MtrrMapAdd.cbMapped == MtrrMapSub.cbMapped);
        if (MtrrMapSub.idxMtrr < MtrrMapAdd.idxMtrr)
            pMtrrMapOptimal = &MtrrMapSub;
        else
            pMtrrMapOptimal = &MtrrMapAdd;
    }
    else if (RT_SUCCESS(rcAdd))
        pMtrrMapOptimal = &MtrrMapAdd;
    else if (RT_SUCCESS(rcSub))
        pMtrrMapOptimal = &MtrrMapSub;
    else
    {
        /*
         * If both methods fail, use the additive method as it gives partially mapped
         * memory as opposed to memory that isn't present.
         */
        pMtrrMapOptimal = &MtrrMapAdd;
    }

    int const rc = cpumR3MtrrMapAddMap(pVM, pMtrrMap, pMtrrMapOptimal);
    if (   RT_SUCCESS(rc)
        && pMtrrMapOptimal->cbMapped == pMtrrMapOptimal->cbToMap) /* Required to distinguish full vs overflow state. */
        return VINF_SUCCESS;
    return VERR_OUT_OF_RESOURCES;
}


/**
 * Maps RAM above 4GB using variable-range MTRRs.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the requested memory could be fully mapped within the
 *          given number of MTRRs.
 * @retval  VERR_OUT_OF_RESOURCES when the requested memory could not be fully
 *          mapped within the given number of MTRRs.
 *
 * @param   pVM         The cross context VM structure.
 * @param   cb          The number of bytes above 4GB to map.
 * @param   pMtrrMap    The variable-range MTRR map to populate.
 */
static int cpumR3MapMtrrsAbove4GB(PVM pVM, uint64_t cb, PCPUMMTRRMAP pMtrrMap)
{
    Assert(pMtrrMap);
    Assert(pMtrrMap->cMtrrs > 1);
    Assert(cb >= _4K);

    /*
     * Map regions at incremental powers of two offsets and sizes.
     * Note: We cannot map an 8GB region in a 4GB offset.
     */
    uint64_t cbLeft    = cb;
    uint64_t offRegion = _4G;
    while (cbLeft > offRegion)
    {
        uint64_t const cbRegion = offRegion;

        Log3(("CPUM: MTRR: [%u]: %Rhcb (%RU64 bytes)\n", pMtrrMap->idxMtrr, cbRegion, cbRegion));
        int const rc = cpumR3MtrrMapAddRegion(pVM, pMtrrMap, offRegion, offRegion + cbRegion - 1, X86_MTRR_MT_WB);
        if (RT_FAILURE(rc))
            return rc;

        offRegion <<= 1;
        cbLeft    -= RT_MIN(cbRegion, cbLeft);
    }

    /*
     * Optimally try and map any remaining memory that is smaller than
     * the last power of two offset (size) above.
     */
    if (cbLeft > 0)
    {
        Assert(pMtrrMap->cMtrrs - pMtrrMap->idxMtrr > 0);
        return cpumR3MapMtrrsOptimal(pVM, offRegion, cbLeft, pMtrrMap);
    }
    return VINF_SUCCESS;
}


/**
 * Maps guest RAM via MTRRs.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
static int cpumR3MapMtrrs(PVM pVM)
{
    /*
     * The RAM size configured for the VM does NOT include the RAM hole!
     * We cannot make ANY assumptions about the RAM size or the RAM hole size
     * of the VM since it is configurable by the user. Hence, we must check for
     * atypical sizes.
     */
    uint64_t cbRam;
    int rc = CFGMR3QueryU64(CFGMR3GetRoot(pVM), "RamSize", &cbRam);
    if (RT_FAILURE(rc))
    {
        LogRel(("CPUM: Cannot map RAM via MTRRs since the RAM size is not configured for the VM\n"));
        return VINF_SUCCESS;
    }
    if (!(cbRam & ~X86_PAGE_4K_BASE_MASK))
    { /* likely */ }
    else
    {
        LogRel(("CPUM: WARNING! RAM size %RU64 bytes is not 4K aligned, using %RU64 bytes\n", cbRam, cbRam & X86_PAGE_4K_BASE_MASK));
        cbRam &= X86_PAGE_4K_BASE_MASK;
    }

    /*
     * Map the RAM below 1MB.
     */
    if (cbRam >= _1M)
    {
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PCPUMCTXMSRS pCtxMsrs = &pVM->apCpusR3[idCpu]->cpum.s.GuestMsrs;
            pCtxMsrs->msr.MtrrFix64K_00000 = 0x0606060606060606;
            pCtxMsrs->msr.MtrrFix16K_80000 = 0x0606060606060606;
            pCtxMsrs->msr.MtrrFix16K_A0000 = 0;
            pCtxMsrs->msr.MtrrFix4K_C0000  = 0x0505050505050505;
            pCtxMsrs->msr.MtrrFix4K_C8000  = 0x0505050505050505;
            pCtxMsrs->msr.MtrrFix4K_D0000  = 0x0505050505050505;
            pCtxMsrs->msr.MtrrFix4K_D8000  = 0x0505050505050505;
            pCtxMsrs->msr.MtrrFix4K_E0000  = 0x0505050505050505;
            pCtxMsrs->msr.MtrrFix4K_E8000  = 0x0505050505050505;
            pCtxMsrs->msr.MtrrFix4K_F0000  = 0x0505050505050505;
            pCtxMsrs->msr.MtrrFix4K_F8000  = 0x0505050505050505;
        }
        LogRel(("CPUM: Mapped %Rhcb (%u bytes) of RAM using fixed-range MTRRs\n", _1M, _1M));
    }
    else
    {
        LogRel(("CPUM: WARNING! Cannot map RAM via MTRRs since the RAM size is below 1M\n"));
        return VINF_SUCCESS;
    }

    if (cbRam > _1M + _4K)
    { /* likely */ }
    else
    {
        LogRel(("CPUM: WARNING! Cannot map RAM above 1M via MTRRs since the RAM size above 1M is below 4K\n"));
        return VINF_SUCCESS;
    }

    /*
     * Check if there is at least 1 MTRR available in addition to MTRRs reserved
     * for use by software for mapping guest memory, see @bugref{10498#c34}.
     *
     * Intel Pentium Pro Processor's BIOS Writers Guide and our EFI code reserves
     * 2 MTRRs for use by software and thus we reserve the same here.
     */
    uint8_t const cMtrrsMax  = pVM->apCpusR3[0]->cpum.s.GuestMsrs.msr.MtrrCap & MSR_IA32_MTRR_CAP_VCNT_MASK;
    uint8_t const cMtrrsRsvd = 2;
    if (cMtrrsMax < cMtrrsRsvd + 1)
    {
        LogRel(("CPUM: WARNING! Variable-range MTRRs (%u) insufficient to map RAM since %u of them are reserved for software\n",
                cMtrrsMax, cMtrrsRsvd));
        return VINF_SUCCESS;
    }

    CPUMMTRRMAP MtrrMap;
    RT_ZERO(MtrrMap);
    uint8_t const cMtrrsMappable = cMtrrsMax - cMtrrsRsvd;
    Assert(cMtrrsMappable > 0);  /* Paranoia. */
    AssertLogRelMsgReturn(cMtrrsMappable <= RT_ELEMENTS(MtrrMap.aMtrrs),
                          ("Mappable variable-range MTRRs (%u) exceed MTRRs available (%u)\n", cMtrrsMappable,
                           RT_ELEMENTS(MtrrMap.aMtrrs)),
                          VERR_CPUM_IPE_1);
    MtrrMap.cMtrrs  = cMtrrsMappable;
    MtrrMap.cbToMap = cbRam;

    /*
     * Get the RAM hole size configured for the VM.
     * Since MM has already validated it, we only debug assert the same constraints here.
     *
     * Although it is not required by the MTRR mapping code that the RAM hole size be a
     * power of 2, it is highly recommended to keep it this way in order to drastically
     * reduce the number of MTRRs used.
     */
    uint32_t const cbRamHole = MMR3PhysGet4GBRamHoleSize(pVM);
    AssertMsg(cbRamHole <= 4032U * _1M, ("RAM hole size (%RU32 bytes) is too large\n", cbRamHole));
    AssertMsg(cbRamHole > 16 * _1M,     ("RAM hole size (%RU32 bytes) is too small\n", cbRamHole));
    AssertMsg(!(cbRamHole & (_4M - 1)), ("RAM hole size (%RU32 bytes) must be 4MB aligned\n", cbRamHole));

    /*
     * Paranoia.
     * Ensure the maximum physical-address width can accommodate the specified RAM size.
     */
    RTGCPHYS const GCPhysEndMax = RT_BIT_64(pVM->cpum.s.GuestFeatures.cMaxPhysAddrWidth);
    RTGCPHYS const GCPhysEnd    = cbRam + cbRamHole;
    if (GCPhysEnd <= GCPhysEndMax)
    { /* likely */ }
    else
    {
        LogRel(("CPUM: WARNING! Cannot fully map RAM of %Rhcb (%RU64 bytes) as it exceeds maximum physical-address (%#RX64)\n",
                GCPhysEnd, GCPhysEnd, GCPhysEndMax - 1));
    }

    /*
     * Map the RAM (and RAM hole) below 4GB.
     */
    uint64_t const cbBelow4GB = RT_MIN(cbRam, (uint64_t)_4G - cbRamHole);
    rc = cpumR3MapMtrrsOptimal(pVM, 0 /* GCPhysFirst */, cbBelow4GB, &MtrrMap);
    if (RT_SUCCESS(rc))
    {
        Assert(MtrrMap.idxMtrr > 0);
        Assert(MtrrMap.idxMtrr <= MtrrMap.cMtrrs);
        Assert(MtrrMap.cbMapped == cbBelow4GB);

        /*
         * Map the RAM above 4GB.
         */
        uint64_t const cbAbove4GB = cbRam + cbRamHole > _4G ? cbRam + cbRamHole - _4G : 0;
        if (cbAbove4GB)
        {
            rc = cpumR3MapMtrrsAbove4GB(pVM, cbAbove4GB, &MtrrMap);
            if (RT_SUCCESS(rc))
                Assert(MtrrMap.cbMapped == MtrrMap.cbToMap);
        }
        LogRel(("CPUM: Mapped %Rhcb (%RU64 bytes) of RAM using %u variable-range MTRRs\n", MtrrMap.cbMapped, MtrrMap.cbMapped,
                MtrrMap.idxMtrr));
    }

    /*
     * Check if we ran out of MTRRs while mapping the memory.
     */
    if (MtrrMap.cbMapped < cbRam)
    {
        Assert(rc == VERR_OUT_OF_RESOURCES);
        Assert(MtrrMap.idxMtrr == cMtrrsMappable);
        Assert(MtrrMap.idxMtrr == MtrrMap.cMtrrs);
        uint64_t const cbLost = cbRam - MtrrMap.cbMapped;
        LogRel(("CPUM: WARNING! Could not map %Rhcb (%RU64 bytes) of RAM using %u variable-range MTRRs\n", cbLost, cbLost,
                MtrrMap.cMtrrs));
    }

    /*
     * Copy mapped MTRRs to all VCPUs.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PCPUMCTXMSRS pCtxMsrs = &pVM->apCpusR3[idCpu]->cpum.s.GuestMsrs;
        Assert(sizeof(pCtxMsrs->msr.aMtrrVarMsrs) == sizeof(MtrrMap.aMtrrs));
        memcpy(&pCtxMsrs->msr.aMtrrVarMsrs[0], &MtrrMap.aMtrrs[0], sizeof(MtrrMap.aMtrrs));
    }

    return VINF_SUCCESS;
}


/**
 * Formats the EFLAGS value into mnemonics.
 *
 * @param   pszEFlags   Where to write the mnemonics. (Assumes sufficient buffer space.)
 * @param   efl         The EFLAGS value with both guest hardware and VBox
 *                      internal bits included.
 */
static void cpumR3InfoFormatFlags(char *pszEFlags, uint32_t efl)
{
    /*
     * Format the flags.
     */
    static const struct
    {
        const char *pszSet; const char *pszClear; uint32_t fFlag;
    }   s_aFlags[] =
    {
        { "vip",NULL, X86_EFL_VIP },
        { "vif",NULL, X86_EFL_VIF },
        { "ac", NULL, X86_EFL_AC },
        { "vm", NULL, X86_EFL_VM },
        { "rf", NULL, X86_EFL_RF },
        { "nt", NULL, X86_EFL_NT },
        { "ov", "nv", X86_EFL_OF },
        { "dn", "up", X86_EFL_DF },
        { "ei", "di", X86_EFL_IF },
        { "tf", NULL, X86_EFL_TF },
        { "nt", "pl", X86_EFL_SF },
        { "nz", "zr", X86_EFL_ZF },
        { "ac", "na", X86_EFL_AF },
        { "po", "pe", X86_EFL_PF },
        { "cy", "nc", X86_EFL_CF },
        { "inh-ss",  NULL, CPUMCTX_INHIBIT_SHADOW_SS },
        { "inh-sti", NULL, CPUMCTX_INHIBIT_SHADOW_STI },
        { "inh-nmi", NULL, CPUMCTX_INHIBIT_NMI },
    };
    char *psz = pszEFlags;
    for (unsigned i = 0; i < RT_ELEMENTS(s_aFlags); i++)
    {
        const char *pszAdd = s_aFlags[i].fFlag & efl ? s_aFlags[i].pszSet : s_aFlags[i].pszClear;
        if (pszAdd)
        {
            strcpy(psz, pszAdd);
            psz += strlen(pszAdd);
            *psz++ = ' ';
        }
    }
    psz[-1] = '\0';
}


/**
 * Formats a full register dump.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pHlp        Output functions.
 * @param   enmType     The dump type.
 * @param   pszPrefix   Register name prefix.
 */
static void cpumR3InfoOne(PVM pVM, PCVMCPU pVCpu, PCDBGFINFOHLP pHlp, CPUMDUMPTYPE enmType, const char *pszPrefix)
{
    PCCPUMCTX pCtx = &pVCpu->cpum.s.Guest;

    /*
     * Format the EFLAGS.
     */
    char szEFlags[80];
    cpumR3InfoFormatFlags(&szEFlags[0], pCtx->eflags.uBoth);

    /*
     * Format the registers.
     */
    uint32_t const efl = pCtx->eflags.u;
    switch (enmType)
    {
        case CPUMDUMPTYPE_TERSE:
            if (CPUMIsGuestIn64BitCodeEx(pCtx))
                pHlp->pfnPrintf(pHlp,
                    "%srax=%016RX64 %srbx=%016RX64 %srcx=%016RX64 %srdx=%016RX64\n"
                    "%srsi=%016RX64 %srdi=%016RX64 %sr8 =%016RX64 %sr9 =%016RX64\n"
                    "%sr10=%016RX64 %sr11=%016RX64 %sr12=%016RX64 %sr13=%016RX64\n"
                    "%sr14=%016RX64 %sr15=%016RX64\n"
                    "%srip=%016RX64 %srsp=%016RX64 %srbp=%016RX64 %siopl=%d %*s\n"
                    "%scs=%04x %sss=%04x %sds=%04x %ses=%04x %sfs=%04x %sgs=%04x                %seflags=%08x\n",
                    pszPrefix, pCtx->rax, pszPrefix, pCtx->rbx, pszPrefix, pCtx->rcx, pszPrefix, pCtx->rdx, pszPrefix, pCtx->rsi, pszPrefix, pCtx->rdi,
                    pszPrefix, pCtx->r8, pszPrefix, pCtx->r9, pszPrefix, pCtx->r10, pszPrefix, pCtx->r11, pszPrefix, pCtx->r12, pszPrefix, pCtx->r13,
                    pszPrefix, pCtx->r14, pszPrefix, pCtx->r15,
                    pszPrefix, pCtx->rip, pszPrefix, pCtx->rsp, pszPrefix, pCtx->rbp, pszPrefix, X86_EFL_GET_IOPL(efl), *pszPrefix ? 33 : 31, szEFlags,
                    pszPrefix, pCtx->cs.Sel, pszPrefix, pCtx->ss.Sel, pszPrefix, pCtx->ds.Sel, pszPrefix, pCtx->es.Sel,
                    pszPrefix, pCtx->fs.Sel, pszPrefix, pCtx->gs.Sel, pszPrefix, efl);
            else
                pHlp->pfnPrintf(pHlp,
                    "%seax=%08x %sebx=%08x %secx=%08x %sedx=%08x %sesi=%08x %sedi=%08x\n"
                    "%seip=%08x %sesp=%08x %sebp=%08x %siopl=%d %*s\n"
                    "%scs=%04x %sss=%04x %sds=%04x %ses=%04x %sfs=%04x %sgs=%04x                %seflags=%08x\n",
                    pszPrefix, pCtx->eax, pszPrefix, pCtx->ebx, pszPrefix, pCtx->ecx, pszPrefix, pCtx->edx, pszPrefix, pCtx->esi, pszPrefix, pCtx->edi,
                    pszPrefix, pCtx->eip, pszPrefix, pCtx->esp, pszPrefix, pCtx->ebp, pszPrefix, X86_EFL_GET_IOPL(efl), *pszPrefix ? 33 : 31, szEFlags,
                    pszPrefix, pCtx->cs.Sel, pszPrefix, pCtx->ss.Sel, pszPrefix, pCtx->ds.Sel, pszPrefix, pCtx->es.Sel,
                    pszPrefix, pCtx->fs.Sel, pszPrefix, pCtx->gs.Sel, pszPrefix, efl);
            break;

        case CPUMDUMPTYPE_DEFAULT:
            if (CPUMIsGuestIn64BitCodeEx(pCtx))
                pHlp->pfnPrintf(pHlp,
                    "%srax=%016RX64 %srbx=%016RX64 %srcx=%016RX64 %srdx=%016RX64\n"
                    "%srsi=%016RX64 %srdi=%016RX64 %sr8 =%016RX64 %sr9 =%016RX64\n"
                    "%sr10=%016RX64 %sr11=%016RX64 %sr12=%016RX64 %sr13=%016RX64\n"
                    "%sr14=%016RX64 %sr15=%016RX64\n"
                    "%srip=%016RX64 %srsp=%016RX64 %srbp=%016RX64 %siopl=%d %*s\n"
                    "%scs=%04x %sss=%04x %sds=%04x %ses=%04x %sfs=%04x %sgs=%04x %str=%04x      %seflags=%08x\n"
                    "%scr0=%08RX64 %scr2=%08RX64 %scr3=%08RX64 %scr4=%08RX64 %sgdtr=%016RX64:%04x %sldtr=%04x\n"
                    ,
                    pszPrefix, pCtx->rax, pszPrefix, pCtx->rbx, pszPrefix, pCtx->rcx, pszPrefix, pCtx->rdx, pszPrefix, pCtx->rsi, pszPrefix, pCtx->rdi,
                    pszPrefix, pCtx->r8, pszPrefix, pCtx->r9, pszPrefix, pCtx->r10, pszPrefix, pCtx->r11, pszPrefix, pCtx->r12, pszPrefix, pCtx->r13,
                    pszPrefix, pCtx->r14, pszPrefix, pCtx->r15,
                    pszPrefix, pCtx->rip, pszPrefix, pCtx->rsp, pszPrefix, pCtx->rbp, pszPrefix, X86_EFL_GET_IOPL(efl), *pszPrefix ? 33 : 31, szEFlags,
                    pszPrefix, pCtx->cs.Sel, pszPrefix, pCtx->ss.Sel, pszPrefix, pCtx->ds.Sel, pszPrefix, pCtx->es.Sel,
                    pszPrefix, pCtx->fs.Sel, pszPrefix, pCtx->gs.Sel, pszPrefix, pCtx->tr.Sel, pszPrefix, efl,
                    pszPrefix, pCtx->cr0, pszPrefix, pCtx->cr2, pszPrefix, pCtx->cr3, pszPrefix, pCtx->cr4,
                    pszPrefix, pCtx->gdtr.pGdt, pCtx->gdtr.cbGdt, pszPrefix, pCtx->ldtr.Sel);
            else
                pHlp->pfnPrintf(pHlp,
                    "%seax=%08x %sebx=%08x %secx=%08x %sedx=%08x %sesi=%08x %sedi=%08x\n"
                    "%seip=%08x %sesp=%08x %sebp=%08x %siopl=%d %*s\n"
                    "%scs=%04x %sss=%04x %sds=%04x %ses=%04x %sfs=%04x %sgs=%04x %str=%04x      %seflags=%08x\n"
                    "%scr0=%08RX64 %scr2=%08RX64 %scr3=%08RX64 %scr4=%08RX64 %sgdtr=%08RX64:%04x %sldtr=%04x\n"
                    ,
                    pszPrefix, pCtx->eax, pszPrefix, pCtx->ebx, pszPrefix, pCtx->ecx, pszPrefix, pCtx->edx, pszPrefix, pCtx->esi, pszPrefix, pCtx->edi,
                    pszPrefix, pCtx->eip, pszPrefix, pCtx->esp, pszPrefix, pCtx->ebp, pszPrefix, X86_EFL_GET_IOPL(efl), *pszPrefix ? 33 : 31, szEFlags,
                    pszPrefix, pCtx->cs.Sel, pszPrefix, pCtx->ss.Sel, pszPrefix, pCtx->ds.Sel, pszPrefix, pCtx->es.Sel,
                    pszPrefix, pCtx->fs.Sel, pszPrefix, pCtx->gs.Sel, pszPrefix, pCtx->tr.Sel, pszPrefix, efl,
                    pszPrefix, pCtx->cr0, pszPrefix, pCtx->cr2, pszPrefix, pCtx->cr3, pszPrefix, pCtx->cr4,
                    pszPrefix, pCtx->gdtr.pGdt, pCtx->gdtr.cbGdt, pszPrefix, pCtx->ldtr.Sel);
            break;

        case CPUMDUMPTYPE_VERBOSE:
            if (CPUMIsGuestIn64BitCodeEx(pCtx))
                pHlp->pfnPrintf(pHlp,
                    "%srax=%016RX64 %srbx=%016RX64 %srcx=%016RX64 %srdx=%016RX64\n"
                    "%srsi=%016RX64 %srdi=%016RX64 %sr8 =%016RX64 %sr9 =%016RX64\n"
                    "%sr10=%016RX64 %sr11=%016RX64 %sr12=%016RX64 %sr13=%016RX64\n"
                    "%sr14=%016RX64 %sr15=%016RX64\n"
                    "%srip=%016RX64 %srsp=%016RX64 %srbp=%016RX64 %siopl=%d %*s\n"
                    "%scs={%04x base=%016RX64 limit=%08x flags=%08x}\n"
                    "%sds={%04x base=%016RX64 limit=%08x flags=%08x}\n"
                    "%ses={%04x base=%016RX64 limit=%08x flags=%08x}\n"
                    "%sfs={%04x base=%016RX64 limit=%08x flags=%08x}\n"
                    "%sgs={%04x base=%016RX64 limit=%08x flags=%08x}\n"
                    "%sss={%04x base=%016RX64 limit=%08x flags=%08x}\n"
                    "%scr0=%016RX64 %scr2=%016RX64 %scr3=%016RX64 %scr4=%016RX64\n"
                    "%sdr0=%016RX64 %sdr1=%016RX64 %sdr2=%016RX64 %sdr3=%016RX64\n"
                    "%sdr4=%016RX64 %sdr5=%016RX64 %sdr6=%016RX64 %sdr7=%016RX64\n"
                    "%sgdtr=%016RX64:%04x  %sidtr=%016RX64:%04x  %seflags=%08x\n"
                    "%sldtr={%04x base=%08RX64 limit=%08x flags=%08x}\n"
                    "%str  ={%04x base=%08RX64 limit=%08x flags=%08x}\n"
                    "%sSysEnter={cs=%04llx eip=%016RX64 esp=%016RX64}\n"
                    ,
                    pszPrefix, pCtx->rax, pszPrefix, pCtx->rbx, pszPrefix, pCtx->rcx, pszPrefix, pCtx->rdx, pszPrefix, pCtx->rsi, pszPrefix, pCtx->rdi,
                    pszPrefix, pCtx->r8, pszPrefix, pCtx->r9, pszPrefix, pCtx->r10, pszPrefix, pCtx->r11, pszPrefix, pCtx->r12, pszPrefix, pCtx->r13,
                    pszPrefix, pCtx->r14, pszPrefix, pCtx->r15,
                    pszPrefix, pCtx->rip, pszPrefix, pCtx->rsp, pszPrefix, pCtx->rbp, pszPrefix, X86_EFL_GET_IOPL(efl), *pszPrefix ? 33 : 31, szEFlags,
                    pszPrefix, pCtx->cs.Sel, pCtx->cs.u64Base, pCtx->cs.u32Limit, pCtx->cs.Attr.u,
                    pszPrefix, pCtx->ds.Sel, pCtx->ds.u64Base, pCtx->ds.u32Limit, pCtx->ds.Attr.u,
                    pszPrefix, pCtx->es.Sel, pCtx->es.u64Base, pCtx->es.u32Limit, pCtx->es.Attr.u,
                    pszPrefix, pCtx->fs.Sel, pCtx->fs.u64Base, pCtx->fs.u32Limit, pCtx->fs.Attr.u,
                    pszPrefix, pCtx->gs.Sel, pCtx->gs.u64Base, pCtx->gs.u32Limit, pCtx->gs.Attr.u,
                    pszPrefix, pCtx->ss.Sel, pCtx->ss.u64Base, pCtx->ss.u32Limit, pCtx->ss.Attr.u,
                    pszPrefix, pCtx->cr0,  pszPrefix, pCtx->cr2, pszPrefix, pCtx->cr3,  pszPrefix, pCtx->cr4,
                    pszPrefix, pCtx->dr[0],  pszPrefix, pCtx->dr[1], pszPrefix, pCtx->dr[2],  pszPrefix, pCtx->dr[3],
                    pszPrefix, pCtx->dr[4],  pszPrefix, pCtx->dr[5], pszPrefix, pCtx->dr[6],  pszPrefix, pCtx->dr[7],
                    pszPrefix, pCtx->gdtr.pGdt, pCtx->gdtr.cbGdt, pszPrefix, pCtx->idtr.pIdt, pCtx->idtr.cbIdt, pszPrefix, efl,
                    pszPrefix, pCtx->ldtr.Sel, pCtx->ldtr.u64Base, pCtx->ldtr.u32Limit, pCtx->ldtr.Attr.u,
                    pszPrefix, pCtx->tr.Sel, pCtx->tr.u64Base, pCtx->tr.u32Limit, pCtx->tr.Attr.u,
                    pszPrefix, pCtx->SysEnter.cs, pCtx->SysEnter.eip, pCtx->SysEnter.esp);
            else
                pHlp->pfnPrintf(pHlp,
                    "%seax=%08x %sebx=%08x %secx=%08x %sedx=%08x %sesi=%08x %sedi=%08x\n"
                    "%seip=%08x %sesp=%08x %sebp=%08x %siopl=%d %*s\n"
                    "%scs={%04x base=%016RX64 limit=%08x flags=%08x} %sdr0=%08RX64 %sdr1=%08RX64\n"
                    "%sds={%04x base=%016RX64 limit=%08x flags=%08x} %sdr2=%08RX64 %sdr3=%08RX64\n"
                    "%ses={%04x base=%016RX64 limit=%08x flags=%08x} %sdr4=%08RX64 %sdr5=%08RX64\n"
                    "%sfs={%04x base=%016RX64 limit=%08x flags=%08x} %sdr6=%08RX64 %sdr7=%08RX64\n"
                    "%sgs={%04x base=%016RX64 limit=%08x flags=%08x} %scr0=%08RX64 %scr2=%08RX64\n"
                    "%sss={%04x base=%016RX64 limit=%08x flags=%08x} %scr3=%08RX64 %scr4=%08RX64\n"
                    "%sgdtr=%016RX64:%04x  %sidtr=%016RX64:%04x  %seflags=%08x\n"
                    "%sldtr={%04x base=%08RX64 limit=%08x flags=%08x}\n"
                    "%str  ={%04x base=%08RX64 limit=%08x flags=%08x}\n"
                    "%sSysEnter={cs=%04llx eip=%08llx esp=%08llx}\n"
                    ,
                    pszPrefix, pCtx->eax, pszPrefix, pCtx->ebx, pszPrefix, pCtx->ecx, pszPrefix, pCtx->edx, pszPrefix, pCtx->esi, pszPrefix, pCtx->edi,
                    pszPrefix, pCtx->eip, pszPrefix, pCtx->esp, pszPrefix, pCtx->ebp, pszPrefix, X86_EFL_GET_IOPL(efl), *pszPrefix ? 33 : 31, szEFlags,
                    pszPrefix, pCtx->cs.Sel, pCtx->cs.u64Base, pCtx->cs.u32Limit, pCtx->cs.Attr.u, pszPrefix, pCtx->dr[0],  pszPrefix, pCtx->dr[1],
                    pszPrefix, pCtx->ds.Sel, pCtx->ds.u64Base, pCtx->ds.u32Limit, pCtx->ds.Attr.u, pszPrefix, pCtx->dr[2],  pszPrefix, pCtx->dr[3],
                    pszPrefix, pCtx->es.Sel, pCtx->es.u64Base, pCtx->es.u32Limit, pCtx->es.Attr.u, pszPrefix, pCtx->dr[4],  pszPrefix, pCtx->dr[5],
                    pszPrefix, pCtx->fs.Sel, pCtx->fs.u64Base, pCtx->fs.u32Limit, pCtx->fs.Attr.u, pszPrefix, pCtx->dr[6],  pszPrefix, pCtx->dr[7],
                    pszPrefix, pCtx->gs.Sel, pCtx->gs.u64Base, pCtx->gs.u32Limit, pCtx->gs.Attr.u, pszPrefix, pCtx->cr0,  pszPrefix, pCtx->cr2,
                    pszPrefix, pCtx->ss.Sel, pCtx->ss.u64Base, pCtx->ss.u32Limit, pCtx->ss.Attr.u, pszPrefix, pCtx->cr3,  pszPrefix, pCtx->cr4,
                    pszPrefix, pCtx->gdtr.pGdt, pCtx->gdtr.cbGdt, pszPrefix, pCtx->idtr.pIdt, pCtx->idtr.cbIdt, pszPrefix, efl,
                    pszPrefix, pCtx->ldtr.Sel, pCtx->ldtr.u64Base, pCtx->ldtr.u32Limit, pCtx->ldtr.Attr.u,
                    pszPrefix, pCtx->tr.Sel, pCtx->tr.u64Base, pCtx->tr.u32Limit, pCtx->tr.Attr.u,
                    pszPrefix, pCtx->SysEnter.cs, pCtx->SysEnter.eip, pCtx->SysEnter.esp);

            pHlp->pfnPrintf(pHlp, "%sxcr=%016RX64 %sxcr1=%016RX64 %sxss=%016RX64 (fXStateMask=%016RX64)\n",
                            pszPrefix, pCtx->aXcr[0], pszPrefix, pCtx->aXcr[1],
                            pszPrefix, UINT64_C(0) /** @todo XSS */, pCtx->fXStateMask);
            {
                PCX86FXSTATE pFpuCtx = &pCtx->XState.x87;
                pHlp->pfnPrintf(pHlp,
                    "%sFCW=%04x %sFSW=%04x %sFTW=%04x %sFOP=%04x %sMXCSR=%08x %sMXCSR_MASK=%08x\n"
                    "%sFPUIP=%08x %sCS=%04x %sRsrvd1=%04x  %sFPUDP=%08x %sDS=%04x %sRsvrd2=%04x\n"
                    ,
                    pszPrefix, pFpuCtx->FCW,   pszPrefix, pFpuCtx->FSW, pszPrefix, pFpuCtx->FTW, pszPrefix, pFpuCtx->FOP,
                    pszPrefix, pFpuCtx->MXCSR, pszPrefix, pFpuCtx->MXCSR_MASK,
                    pszPrefix, pFpuCtx->FPUIP, pszPrefix, pFpuCtx->CS,  pszPrefix, pFpuCtx->Rsrvd1,
                    pszPrefix, pFpuCtx->FPUDP, pszPrefix, pFpuCtx->DS,  pszPrefix, pFpuCtx->Rsrvd2
                    );
                /*
                 * The FSAVE style memory image contains ST(0)-ST(7) at increasing addresses,
                 * not (FP)R0-7 as Intel SDM suggests.
                 */
                unsigned iShift = (pFpuCtx->FSW >> 11) & 7;
                for (unsigned iST = 0; iST < RT_ELEMENTS(pFpuCtx->aRegs); iST++)
                {
                    unsigned iFPR        = (iST + iShift) % RT_ELEMENTS(pFpuCtx->aRegs);
                    unsigned uTag        = (pFpuCtx->FTW >> (2 * iFPR)) & 3;
                    char     chSign      = pFpuCtx->aRegs[iST].au16[4] & 0x8000 ? '-' : '+';
                    unsigned iInteger    = (unsigned)(pFpuCtx->aRegs[iST].au64[0] >> 63);
                    uint64_t u64Fraction = pFpuCtx->aRegs[iST].au64[0] & UINT64_C(0x7fffffffffffffff);
                    int      iExponent   = pFpuCtx->aRegs[iST].au16[4] & 0x7fff;
                    iExponent -= 16383; /* subtract bias */
                    /** @todo This isn't entirenly correct and needs more work! */
                    pHlp->pfnPrintf(pHlp,
                                    "%sST(%u)=%sFPR%u={%04RX16'%08RX32'%08RX32} t%d %c%u.%022llu * 2 ^ %d (*)",
                                    pszPrefix, iST, pszPrefix, iFPR,
                                    pFpuCtx->aRegs[iST].au16[4], pFpuCtx->aRegs[iST].au32[1], pFpuCtx->aRegs[iST].au32[0],
                                    uTag, chSign, iInteger, u64Fraction, iExponent);
                    if (pFpuCtx->aRegs[iST].au16[5] || pFpuCtx->aRegs[iST].au16[6] || pFpuCtx->aRegs[iST].au16[7])
                        pHlp->pfnPrintf(pHlp, " res={%04RX16,%04RX16,%04RX16}\n",
                                        pFpuCtx->aRegs[iST].au16[5], pFpuCtx->aRegs[iST].au16[6], pFpuCtx->aRegs[iST].au16[7]);
                    else
                        pHlp->pfnPrintf(pHlp, "\n");
                }

                /* XMM/YMM/ZMM registers. */
                if (pCtx->fXStateMask & XSAVE_C_YMM)
                {
                    PCX86XSAVEYMMHI pYmmHiCtx = CPUMCTX_XSAVE_C_PTR(pCtx, XSAVE_C_YMM_BIT, PCX86XSAVEYMMHI);
                    if (!(pCtx->fXStateMask & XSAVE_C_ZMM_HI256))
                        for (unsigned i = 0; i < RT_ELEMENTS(pFpuCtx->aXMM); i++)
                            pHlp->pfnPrintf(pHlp, "%sYMM%u%s=%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32\n",
                                            pszPrefix, i, i < 10 ? " " : "",
                                            pYmmHiCtx->aYmmHi[i].au32[3],
                                            pYmmHiCtx->aYmmHi[i].au32[2],
                                            pYmmHiCtx->aYmmHi[i].au32[1],
                                            pYmmHiCtx->aYmmHi[i].au32[0],
                                            pFpuCtx->aXMM[i].au32[3],
                                            pFpuCtx->aXMM[i].au32[2],
                                            pFpuCtx->aXMM[i].au32[1],
                                            pFpuCtx->aXMM[i].au32[0]);
                    else
                    {
                        PCX86XSAVEZMMHI256 pZmmHi256 = CPUMCTX_XSAVE_C_PTR(pCtx, XSAVE_C_ZMM_HI256_BIT, PCX86XSAVEZMMHI256);
                        for (unsigned i = 0; i < RT_ELEMENTS(pFpuCtx->aXMM); i++)
                            pHlp->pfnPrintf(pHlp,
                                            "%sZMM%u%s=%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32''%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32\n",
                                            pszPrefix, i, i < 10 ? " " : "",
                                            pZmmHi256->aHi256Regs[i].au32[7],
                                            pZmmHi256->aHi256Regs[i].au32[6],
                                            pZmmHi256->aHi256Regs[i].au32[5],
                                            pZmmHi256->aHi256Regs[i].au32[4],
                                            pZmmHi256->aHi256Regs[i].au32[3],
                                            pZmmHi256->aHi256Regs[i].au32[2],
                                            pZmmHi256->aHi256Regs[i].au32[1],
                                            pZmmHi256->aHi256Regs[i].au32[0],
                                            pYmmHiCtx->aYmmHi[i].au32[3],
                                            pYmmHiCtx->aYmmHi[i].au32[2],
                                            pYmmHiCtx->aYmmHi[i].au32[1],
                                            pYmmHiCtx->aYmmHi[i].au32[0],
                                            pFpuCtx->aXMM[i].au32[3],
                                            pFpuCtx->aXMM[i].au32[2],
                                            pFpuCtx->aXMM[i].au32[1],
                                            pFpuCtx->aXMM[i].au32[0]);

                        PCX86XSAVEZMM16HI pZmm16Hi = CPUMCTX_XSAVE_C_PTR(pCtx, XSAVE_C_ZMM_16HI_BIT, PCX86XSAVEZMM16HI);
                        for (unsigned i = 0; i < RT_ELEMENTS(pZmm16Hi->aRegs); i++)
                            pHlp->pfnPrintf(pHlp,
                                            "%sZMM%u=%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32''%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32'%08RX32\n",
                                            pszPrefix, i + 16,
                                            pZmm16Hi->aRegs[i].au32[15],
                                            pZmm16Hi->aRegs[i].au32[14],
                                            pZmm16Hi->aRegs[i].au32[13],
                                            pZmm16Hi->aRegs[i].au32[12],
                                            pZmm16Hi->aRegs[i].au32[11],
                                            pZmm16Hi->aRegs[i].au32[10],
                                            pZmm16Hi->aRegs[i].au32[9],
                                            pZmm16Hi->aRegs[i].au32[8],
                                            pZmm16Hi->aRegs[i].au32[7],
                                            pZmm16Hi->aRegs[i].au32[6],
                                            pZmm16Hi->aRegs[i].au32[5],
                                            pZmm16Hi->aRegs[i].au32[4],
                                            pZmm16Hi->aRegs[i].au32[3],
                                            pZmm16Hi->aRegs[i].au32[2],
                                            pZmm16Hi->aRegs[i].au32[1],
                                            pZmm16Hi->aRegs[i].au32[0]);
                    }
                }
                else
                    for (unsigned i = 0; i < RT_ELEMENTS(pFpuCtx->aXMM); i++)
                        pHlp->pfnPrintf(pHlp,
                                        i & 1
                                        ? "%sXMM%u%s=%08RX32'%08RX32'%08RX32'%08RX32\n"
                                        : "%sXMM%u%s=%08RX32'%08RX32'%08RX32'%08RX32  ",
                                        pszPrefix, i, i < 10 ? " " : "",
                                        pFpuCtx->aXMM[i].au32[3],
                                        pFpuCtx->aXMM[i].au32[2],
                                        pFpuCtx->aXMM[i].au32[1],
                                        pFpuCtx->aXMM[i].au32[0]);

                if (pCtx->fXStateMask & XSAVE_C_OPMASK)
                {
                    PCX86XSAVEOPMASK pOpMask = CPUMCTX_XSAVE_C_PTR(pCtx, XSAVE_C_OPMASK_BIT, PCX86XSAVEOPMASK);
                    for (unsigned i = 0; i < RT_ELEMENTS(pOpMask->aKRegs); i += 4)
                        pHlp->pfnPrintf(pHlp, "%sK%u=%016RX64  %sK%u=%016RX64  %sK%u=%016RX64  %sK%u=%016RX64\n",
                                        pszPrefix, i + 0, pOpMask->aKRegs[i + 0],
                                        pszPrefix, i + 1, pOpMask->aKRegs[i + 1],
                                        pszPrefix, i + 2, pOpMask->aKRegs[i + 2],
                                        pszPrefix, i + 3, pOpMask->aKRegs[i + 3]);
                }

                if (pCtx->fXStateMask & XSAVE_C_BNDREGS)
                {
                    PCX86XSAVEBNDREGS pBndRegs = CPUMCTX_XSAVE_C_PTR(pCtx, XSAVE_C_BNDREGS_BIT, PCX86XSAVEBNDREGS);
                    for (unsigned i = 0; i < RT_ELEMENTS(pBndRegs->aRegs); i += 2)
                        pHlp->pfnPrintf(pHlp, "%sBNDREG%u=%016RX64/%016RX64  %sBNDREG%u=%016RX64/%016RX64\n",
                                        pszPrefix, i, pBndRegs->aRegs[i].uLowerBound, pBndRegs->aRegs[i].uUpperBound,
                                        pszPrefix, i + 1, pBndRegs->aRegs[i + 1].uLowerBound, pBndRegs->aRegs[i + 1].uUpperBound);
                }

                if (pCtx->fXStateMask & XSAVE_C_BNDCSR)
                {
                    PCX86XSAVEBNDCFG pBndCfg = CPUMCTX_XSAVE_C_PTR(pCtx, XSAVE_C_BNDCSR_BIT, PCX86XSAVEBNDCFG);
                    pHlp->pfnPrintf(pHlp, "%sBNDCFG.CONFIG=%016RX64 %sBNDCFG.STATUS=%016RX64\n",
                                    pszPrefix, pBndCfg->fConfig, pszPrefix, pBndCfg->fStatus);
                }

                for (unsigned i = 0; i < RT_ELEMENTS(pFpuCtx->au32RsrvdRest); i++)
                    if (pFpuCtx->au32RsrvdRest[i])
                        pHlp->pfnPrintf(pHlp, "%sRsrvdRest[%u]=%RX32 (offset=%#x)\n",
                                        pszPrefix, i, pFpuCtx->au32RsrvdRest[i], RT_UOFFSETOF_DYN(X86FXSTATE, au32RsrvdRest[i]) );
            }

            pHlp->pfnPrintf(pHlp,
                "%sEFER         =%016RX64\n"
                "%sPAT          =%016RX64\n"
                "%sSTAR         =%016RX64\n"
                "%sCSTAR        =%016RX64\n"
                "%sLSTAR        =%016RX64\n"
                "%sSFMASK       =%016RX64\n"
                "%sKERNELGSBASE =%016RX64\n",
                pszPrefix, pCtx->msrEFER,
                pszPrefix, pCtx->msrPAT,
                pszPrefix, pCtx->msrSTAR,
                pszPrefix, pCtx->msrCSTAR,
                pszPrefix, pCtx->msrLSTAR,
                pszPrefix, pCtx->msrSFMASK,
                pszPrefix, pCtx->msrKERNELGSBASE);

            if (CPUMIsGuestInPAEModeEx(pCtx))
                for (unsigned i = 0; i < RT_ELEMENTS(pCtx->aPaePdpes); i++)
                    pHlp->pfnPrintf(pHlp, "%sPAE PDPTE %u  =%016RX64\n", pszPrefix, i, pCtx->aPaePdpes[i]);

            /*
             * MTRRs.
             */
            if (pVM->cpum.s.GuestFeatures.fMtrr)
            {
                pHlp->pfnPrintf(pHlp,
                    "%sMTRR_CAP          =%016RX64\n"
                    "%sMTRR_DEF_TYPE     =%016RX64\n"
                    "%sMTRR_FIX64K_00000 =%016RX64\n"
                    "%sMTRR_FIX16K_80000 =%016RX64\n"
                    "%sMTRR_FIX16K_A0000 =%016RX64\n"
                    "%sMTRR_FIX4K_C0000  =%016RX64\n"
                    "%sMTRR_FIX4K_C8000  =%016RX64\n"
                    "%sMTRR_FIX4K_D0000  =%016RX64\n"
                    "%sMTRR_FIX4K_D8000  =%016RX64\n"
                    "%sMTRR_FIX4K_E0000  =%016RX64\n"
                    "%sMTRR_FIX4K_E8000  =%016RX64\n"
                    "%sMTRR_FIX4K_F0000  =%016RX64\n"
                    "%sMTRR_FIX4K_F8000  =%016RX64\n",
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrCap,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrDefType,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix64K_00000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix16K_80000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix16K_A0000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix4K_C0000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix4K_C8000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix4K_D0000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix4K_D8000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix4K_E0000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix4K_E8000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix4K_F0000,
                    pszPrefix, pVCpu->cpum.s.GuestMsrs.msr.MtrrFix4K_F8000);

                for (uint8_t iRange = 0; iRange < RT_ELEMENTS(pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs); iRange++)
                {
                    PCX86MTRRVAR pMtrrVar = &pVCpu->cpum.s.GuestMsrs.msr.aMtrrVarMsrs[iRange];
                    bool const   fIsValid = RT_BOOL(pMtrrVar->MtrrPhysMask & MSR_IA32_MTRR_PHYSMASK_VALID);
                    if (fIsValid)
                    {
                        RTGCPHYS GCPhysFirst;
                        RTGCPHYS GCPhysLast;
                        cpumR3GetVarMtrrAddrs(pVM, pMtrrVar, &GCPhysFirst, &GCPhysLast);
                        uint8_t const fType    = pMtrrVar->MtrrPhysBase & MSR_IA32_MTRR_PHYSBASE_MT_MASK;
                        const char *pszType    = cpumR3GetVarMtrrMemType(fType);
                        uint64_t const cbRange = GCPhysLast - GCPhysFirst + 1;
                        pHlp->pfnPrintf(pHlp,
                                        "%sMTRR_PHYSBASE[%2u] =%016RX64 First=%016RX64 %6RU64 MB [%s]\n"
                                        "%sMTRR_PHYSMASK[%2u] =%016RX64 Last =%016RX64 %6RU64 MB [%RU64 MB]\n",
                                        pszPrefix, iRange, pMtrrVar->MtrrPhysBase, GCPhysFirst, GCPhysFirst / _1M, pszType,
                                        pszPrefix, iRange, pMtrrVar->MtrrPhysMask, GCPhysLast,  GCPhysLast  / _1M, cbRange / (uint64_t)_1M);
                    }
                    else
                        pHlp->pfnPrintf(pHlp,
                                        "%sMTRR_PHYSBASE[%2u] =%016RX64\n"
                                        "%sMTRR_PHYSMASK[%2u] =%016RX64\n",
                                        pszPrefix, iRange, pMtrrVar->MtrrPhysBase,
                                        pszPrefix, iRange, pMtrrVar->MtrrPhysMask);
                }
            }
            break;
    }
}


/**
 * Display all cpu states and any other cpum info.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoAll(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    cpumR3InfoGuest(pVM, pHlp, pszArgs);
    cpumR3InfoGuestInstr(pVM, pHlp, pszArgs);
    cpumR3InfoGuestHwvirt(pVM, pHlp, pszArgs);
    cpumR3InfoHyper(pVM, pHlp, pszArgs);
    cpumR3InfoHost(pVM, pHlp, pszArgs);
}


/**
 * Parses the info argument.
 *
 * The argument starts with 'verbose', 'terse' or 'default' and then
 * continues with the comment string.
 *
 * @param   pszArgs         The pointer to the argument string.
 * @param   penmType        Where to store the dump type request.
 * @param   ppszComment     Where to store the pointer to the comment string.
 */
static void cpumR3InfoParseArg(const char *pszArgs, CPUMDUMPTYPE *penmType, const char **ppszComment)
{
    if (!pszArgs)
    {
        *penmType = CPUMDUMPTYPE_DEFAULT;
        *ppszComment = "";
    }
    else
    {
        if (!strncmp(pszArgs, RT_STR_TUPLE("verbose")))
        {
            pszArgs += 7;
            *penmType = CPUMDUMPTYPE_VERBOSE;
        }
        else if (!strncmp(pszArgs, RT_STR_TUPLE("terse")))
        {
            pszArgs += 5;
            *penmType = CPUMDUMPTYPE_TERSE;
        }
        else if (!strncmp(pszArgs, RT_STR_TUPLE("default")))
        {
            pszArgs += 7;
            *penmType = CPUMDUMPTYPE_DEFAULT;
        }
        else
            *penmType = CPUMDUMPTYPE_DEFAULT;
        *ppszComment = RTStrStripL(pszArgs);
    }
}


/**
 * Display the guest cpu state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments.
 */
static DECLCALLBACK(void) cpumR3InfoGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    CPUMDUMPTYPE enmType;
    const char *pszComment;
    cpumR3InfoParseArg(pszArgs, &enmType, &pszComment);

    PCVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    pHlp->pfnPrintf(pHlp, "Guest CPUM (VCPU %d) state: %s\n", pVCpu->idCpu, pszComment);

    cpumR3InfoOne(pVM, pVCpu, pHlp, enmType, "");
}


/**
 * Displays an SVM VMCB control area.
 *
 * @param   pHlp        The info helper functions.
 * @param   pVmcbCtrl   Pointer to a SVM VMCB controls area.
 * @param   pszPrefix   Caller specified string prefix.
 */
static void cpumR3InfoSvmVmcbCtrl(PCDBGFINFOHLP pHlp, PCSVMVMCBCTRL pVmcbCtrl, const char *pszPrefix)
{
    AssertReturnVoid(pHlp);
    AssertReturnVoid(pVmcbCtrl);

    pHlp->pfnPrintf(pHlp, "%sCRX-read intercepts        = %#RX16\n",    pszPrefix, pVmcbCtrl->u16InterceptRdCRx);
    pHlp->pfnPrintf(pHlp, "%sCRX-write intercepts       = %#RX16\n",    pszPrefix, pVmcbCtrl->u16InterceptWrCRx);
    pHlp->pfnPrintf(pHlp, "%sDRX-read intercepts        = %#RX16\n",    pszPrefix, pVmcbCtrl->u16InterceptRdDRx);
    pHlp->pfnPrintf(pHlp, "%sDRX-write intercepts       = %#RX16\n",    pszPrefix, pVmcbCtrl->u16InterceptWrDRx);
    pHlp->pfnPrintf(pHlp, "%sException intercepts       = %#RX32\n",    pszPrefix, pVmcbCtrl->u32InterceptXcpt);
    pHlp->pfnPrintf(pHlp, "%sControl intercepts         = %#RX64\n",    pszPrefix, pVmcbCtrl->u64InterceptCtrl);
    pHlp->pfnPrintf(pHlp, "%sPause-filter threshold     = %#RX16\n",    pszPrefix, pVmcbCtrl->u16PauseFilterThreshold);
    pHlp->pfnPrintf(pHlp, "%sPause-filter count         = %#RX16\n",    pszPrefix, pVmcbCtrl->u16PauseFilterCount);
    pHlp->pfnPrintf(pHlp, "%sIOPM bitmap physaddr       = %#RX64\n",    pszPrefix, pVmcbCtrl->u64IOPMPhysAddr);
    pHlp->pfnPrintf(pHlp, "%sMSRPM bitmap physaddr      = %#RX64\n",    pszPrefix, pVmcbCtrl->u64MSRPMPhysAddr);
    pHlp->pfnPrintf(pHlp, "%sTSC offset                 = %#RX64\n",    pszPrefix, pVmcbCtrl->u64TSCOffset);
    pHlp->pfnPrintf(pHlp, "%sTLB Control\n",                            pszPrefix);
    pHlp->pfnPrintf(pHlp, "  %sASID                       = %#RX32\n",  pszPrefix, pVmcbCtrl->TLBCtrl.n.u32ASID);
    pHlp->pfnPrintf(pHlp, "  %sTLB-flush type             = %u\n",      pszPrefix, pVmcbCtrl->TLBCtrl.n.u8TLBFlush);
    pHlp->pfnPrintf(pHlp, "%sInterrupt Control\n",                      pszPrefix);
    pHlp->pfnPrintf(pHlp, "  %sVTPR                       = %#RX8 (%u)\n", pszPrefix, pVmcbCtrl->IntCtrl.n.u8VTPR, pVmcbCtrl->IntCtrl.n.u8VTPR);
    pHlp->pfnPrintf(pHlp, "  %sVIRQ (Pending)             = %RTbool\n", pszPrefix, pVmcbCtrl->IntCtrl.n.u1VIrqPending);
    pHlp->pfnPrintf(pHlp, "  %sVINTR vector               = %#RX8\n",   pszPrefix, pVmcbCtrl->IntCtrl.n.u8VIntrVector);
    pHlp->pfnPrintf(pHlp, "  %sVGIF                       = %u\n",      pszPrefix, pVmcbCtrl->IntCtrl.n.u1VGif);
    pHlp->pfnPrintf(pHlp, "  %sVINTR priority             = %#RX8\n",   pszPrefix, pVmcbCtrl->IntCtrl.n.u4VIntrPrio);
    pHlp->pfnPrintf(pHlp, "  %sIgnore TPR                 = %RTbool\n", pszPrefix, pVmcbCtrl->IntCtrl.n.u1IgnoreTPR);
    pHlp->pfnPrintf(pHlp, "  %sVINTR masking              = %RTbool\n", pszPrefix, pVmcbCtrl->IntCtrl.n.u1VIntrMasking);
    pHlp->pfnPrintf(pHlp, "  %sVGIF enable                = %RTbool\n", pszPrefix, pVmcbCtrl->IntCtrl.n.u1VGifEnable);
    pHlp->pfnPrintf(pHlp, "  %sAVIC enable                = %RTbool\n", pszPrefix, pVmcbCtrl->IntCtrl.n.u1AvicEnable);
    pHlp->pfnPrintf(pHlp, "%sInterrupt Shadow\n",                       pszPrefix);
    pHlp->pfnPrintf(pHlp, "  %sInterrupt shadow           = %RTbool\n", pszPrefix, pVmcbCtrl->IntShadow.n.u1IntShadow);
    pHlp->pfnPrintf(pHlp, "  %sGuest-interrupt Mask       = %RTbool\n", pszPrefix, pVmcbCtrl->IntShadow.n.u1GuestIntMask);
    pHlp->pfnPrintf(pHlp, "%sExit Code                  = %#RX64\n",    pszPrefix, pVmcbCtrl->u64ExitCode);
    pHlp->pfnPrintf(pHlp, "%sEXITINFO1                  = %#RX64\n",    pszPrefix, pVmcbCtrl->u64ExitInfo1);
    pHlp->pfnPrintf(pHlp, "%sEXITINFO2                  = %#RX64\n",    pszPrefix, pVmcbCtrl->u64ExitInfo2);
    pHlp->pfnPrintf(pHlp, "%sExit Interrupt Info\n",                    pszPrefix);
    pHlp->pfnPrintf(pHlp, "  %sValid                      = %RTbool\n", pszPrefix, pVmcbCtrl->ExitIntInfo.n.u1Valid);
    pHlp->pfnPrintf(pHlp, "  %sVector                     = %#RX8 (%u)\n", pszPrefix, pVmcbCtrl->ExitIntInfo.n.u8Vector, pVmcbCtrl->ExitIntInfo.n.u8Vector);
    pHlp->pfnPrintf(pHlp, "  %sType                       = %u\n",      pszPrefix, pVmcbCtrl->ExitIntInfo.n.u3Type);
    pHlp->pfnPrintf(pHlp, "  %sError-code valid           = %RTbool\n", pszPrefix, pVmcbCtrl->ExitIntInfo.n.u1ErrorCodeValid);
    pHlp->pfnPrintf(pHlp, "  %sError-code                 = %#RX32\n",  pszPrefix, pVmcbCtrl->ExitIntInfo.n.u32ErrorCode);
    pHlp->pfnPrintf(pHlp, "%sNested paging and SEV\n",                  pszPrefix);
    pHlp->pfnPrintf(pHlp, "  %sNested paging              = %RTbool\n", pszPrefix, pVmcbCtrl->NestedPagingCtrl.n.u1NestedPaging);
    pHlp->pfnPrintf(pHlp, "  %sSEV (Secure Encrypted VM)  = %RTbool\n", pszPrefix, pVmcbCtrl->NestedPagingCtrl.n.u1Sev);
    pHlp->pfnPrintf(pHlp, "  %sSEV-ES (Encrypted State)   = %RTbool\n", pszPrefix, pVmcbCtrl->NestedPagingCtrl.n.u1SevEs);
    pHlp->pfnPrintf(pHlp, "%sEvent Inject\n",                           pszPrefix);
    pHlp->pfnPrintf(pHlp, "  %sValid                      = %RTbool\n", pszPrefix, pVmcbCtrl->EventInject.n.u1Valid);
    pHlp->pfnPrintf(pHlp, "  %sVector                     = %#RX32 (%u)\n", pszPrefix, pVmcbCtrl->EventInject.n.u8Vector, pVmcbCtrl->EventInject.n.u8Vector);
    pHlp->pfnPrintf(pHlp, "  %sType                       = %u\n",      pszPrefix, pVmcbCtrl->EventInject.n.u3Type);
    pHlp->pfnPrintf(pHlp, "  %sError-code valid           = %RTbool\n", pszPrefix, pVmcbCtrl->EventInject.n.u1ErrorCodeValid);
    pHlp->pfnPrintf(pHlp, "  %sError-code                 = %#RX32\n",  pszPrefix, pVmcbCtrl->EventInject.n.u32ErrorCode);
    pHlp->pfnPrintf(pHlp, "%sNested-paging CR3          = %#RX64\n",    pszPrefix, pVmcbCtrl->u64NestedPagingCR3);
    pHlp->pfnPrintf(pHlp, "%sLBR Virtualization\n",                     pszPrefix);
    pHlp->pfnPrintf(pHlp, "  %sLBR virt                   = %RTbool\n", pszPrefix, pVmcbCtrl->LbrVirt.n.u1LbrVirt);
    pHlp->pfnPrintf(pHlp, "  %sVirt. VMSAVE/VMLOAD        = %RTbool\n", pszPrefix, pVmcbCtrl->LbrVirt.n.u1VirtVmsaveVmload);
    pHlp->pfnPrintf(pHlp, "%sVMCB Clean Bits            = %#RX32\n",    pszPrefix, pVmcbCtrl->u32VmcbCleanBits);
    pHlp->pfnPrintf(pHlp, "%sNext-RIP                   = %#RX64\n",    pszPrefix, pVmcbCtrl->u64NextRIP);
    pHlp->pfnPrintf(pHlp, "%sInstruction bytes fetched  = %u\n",        pszPrefix, pVmcbCtrl->cbInstrFetched);
    pHlp->pfnPrintf(pHlp, "%sInstruction bytes          = %.*Rhxs\n",   pszPrefix, sizeof(pVmcbCtrl->abInstr), pVmcbCtrl->abInstr);
    pHlp->pfnPrintf(pHlp, "%sAVIC\n",                                   pszPrefix);
    pHlp->pfnPrintf(pHlp, "  %sBar addr                   = %#RX64\n",  pszPrefix, pVmcbCtrl->AvicBar.n.u40Addr);
    pHlp->pfnPrintf(pHlp, "  %sBacking page addr          = %#RX64\n",  pszPrefix, pVmcbCtrl->AvicBackingPagePtr.n.u40Addr);
    pHlp->pfnPrintf(pHlp, "  %sLogical table addr         = %#RX64\n",  pszPrefix, pVmcbCtrl->AvicLogicalTablePtr.n.u40Addr);
    pHlp->pfnPrintf(pHlp, "  %sPhysical table addr        = %#RX64\n",  pszPrefix, pVmcbCtrl->AvicPhysicalTablePtr.n.u40Addr);
    pHlp->pfnPrintf(pHlp, "  %sLast guest core Id         = %u\n",      pszPrefix, pVmcbCtrl->AvicPhysicalTablePtr.n.u8LastGuestCoreId);
}


/**
 * Helper for dumping the SVM VMCB selector registers.
 *
 * @param   pHlp        The info helper functions.
 * @param   pSel        Pointer to the SVM selector register.
 * @param   pszName     Name of the selector.
 * @param   pszPrefix   Caller specified string prefix.
 */
DECLINLINE(void) cpumR3InfoSvmVmcbSelReg(PCDBGFINFOHLP pHlp, PCSVMSELREG pSel, const char *pszName, const char *pszPrefix)
{
    /* The string width of 4 used below is to handle 'LDTR'. Change later if longer register names are used. */
    pHlp->pfnPrintf(pHlp, "%s%-4s                       = {%04x base=%016RX64 limit=%08x flags=%04x}\n", pszPrefix,
                    pszName, pSel->u16Sel, pSel->u64Base, pSel->u32Limit, pSel->u16Attr);
}


/**
 * Helper for dumping the SVM VMCB GDTR/IDTR registers.
 *
 * @param   pHlp        The info helper functions.
 * @param   pXdtr       Pointer to the descriptor table register.
 * @param   pszName     Name of the descriptor table register.
 * @param   pszPrefix   Caller specified string prefix.
 */
DECLINLINE(void) cpumR3InfoSvmVmcbXdtr(PCDBGFINFOHLP pHlp, PCSVMXDTR pXdtr, const char *pszName, const char *pszPrefix)
{
    /* The string width of 4 used below is to cover 'GDTR', 'IDTR'. Change later if longer register names are used. */
    pHlp->pfnPrintf(pHlp, "%s%-4s                       = %016RX64:%04x\n", pszPrefix, pszName, pXdtr->u64Base, pXdtr->u32Limit);
}


/**
 * Displays an SVM VMCB state-save area.
 *
 * @param   pHlp            The info helper functions.
 * @param   pVmcbStateSave  Pointer to a SVM VMCB controls area.
 * @param   pszPrefix       Caller specified string prefix.
 */
static void cpumR3InfoSvmVmcbStateSave(PCDBGFINFOHLP pHlp, PCSVMVMCBSTATESAVE pVmcbStateSave, const char *pszPrefix)
{
    AssertReturnVoid(pHlp);
    AssertReturnVoid(pVmcbStateSave);

    char szEFlags[80];
    cpumR3InfoFormatFlags(&szEFlags[0], pVmcbStateSave->u64RFlags);

    cpumR3InfoSvmVmcbSelReg(pHlp, &pVmcbStateSave->CS,   "CS",   pszPrefix);
    cpumR3InfoSvmVmcbSelReg(pHlp, &pVmcbStateSave->SS,   "SS",   pszPrefix);
    cpumR3InfoSvmVmcbSelReg(pHlp, &pVmcbStateSave->ES,   "ES",   pszPrefix);
    cpumR3InfoSvmVmcbSelReg(pHlp, &pVmcbStateSave->DS,   "DS",   pszPrefix);
    cpumR3InfoSvmVmcbSelReg(pHlp, &pVmcbStateSave->FS,   "FS",   pszPrefix);
    cpumR3InfoSvmVmcbSelReg(pHlp, &pVmcbStateSave->GS,   "GS",   pszPrefix);
    cpumR3InfoSvmVmcbSelReg(pHlp, &pVmcbStateSave->LDTR, "LDTR", pszPrefix);
    cpumR3InfoSvmVmcbSelReg(pHlp, &pVmcbStateSave->TR,   "TR",   pszPrefix);
    cpumR3InfoSvmVmcbXdtr(pHlp, &pVmcbStateSave->GDTR,   "GDTR", pszPrefix);
    cpumR3InfoSvmVmcbXdtr(pHlp, &pVmcbStateSave->IDTR,   "IDTR", pszPrefix);
    pHlp->pfnPrintf(pHlp, "%sCPL                        = %u\n",     pszPrefix, pVmcbStateSave->u8CPL);
    pHlp->pfnPrintf(pHlp, "%sEFER                       = %#RX64\n", pszPrefix, pVmcbStateSave->u64EFER);
    pHlp->pfnPrintf(pHlp, "%sCR4                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64CR4);
    pHlp->pfnPrintf(pHlp, "%sCR3                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64CR3);
    pHlp->pfnPrintf(pHlp, "%sCR0                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64CR0);
    pHlp->pfnPrintf(pHlp, "%sDR7                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64DR7);
    pHlp->pfnPrintf(pHlp, "%sDR6                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64DR6);
    pHlp->pfnPrintf(pHlp, "%sRFLAGS                     = %#RX64 %31s\n", pszPrefix, pVmcbStateSave->u64RFlags, szEFlags);
    pHlp->pfnPrintf(pHlp, "%sRIP                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64RIP);
    pHlp->pfnPrintf(pHlp, "%sRSP                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64RSP);
    pHlp->pfnPrintf(pHlp, "%sRAX                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64RAX);
    pHlp->pfnPrintf(pHlp, "%sSTAR                       = %#RX64\n", pszPrefix, pVmcbStateSave->u64STAR);
    pHlp->pfnPrintf(pHlp, "%sLSTAR                      = %#RX64\n", pszPrefix, pVmcbStateSave->u64LSTAR);
    pHlp->pfnPrintf(pHlp, "%sCSTAR                      = %#RX64\n", pszPrefix, pVmcbStateSave->u64CSTAR);
    pHlp->pfnPrintf(pHlp, "%sSFMASK                     = %#RX64\n", pszPrefix, pVmcbStateSave->u64SFMASK);
    pHlp->pfnPrintf(pHlp, "%sKERNELGSBASE               = %#RX64\n", pszPrefix, pVmcbStateSave->u64KernelGSBase);
    pHlp->pfnPrintf(pHlp, "%sSysEnter CS                = %#RX64\n", pszPrefix, pVmcbStateSave->u64SysEnterCS);
    pHlp->pfnPrintf(pHlp, "%sSysEnter EIP               = %#RX64\n", pszPrefix, pVmcbStateSave->u64SysEnterEIP);
    pHlp->pfnPrintf(pHlp, "%sSysEnter ESP               = %#RX64\n", pszPrefix, pVmcbStateSave->u64SysEnterESP);
    pHlp->pfnPrintf(pHlp, "%sCR2                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64CR2);
    pHlp->pfnPrintf(pHlp, "%sPAT                        = %#RX64\n", pszPrefix, pVmcbStateSave->u64PAT);
    pHlp->pfnPrintf(pHlp, "%sDBGCTL                     = %#RX64\n", pszPrefix, pVmcbStateSave->u64DBGCTL);
    pHlp->pfnPrintf(pHlp, "%sBR_FROM                    = %#RX64\n", pszPrefix, pVmcbStateSave->u64BR_FROM);
    pHlp->pfnPrintf(pHlp, "%sBR_TO                      = %#RX64\n", pszPrefix, pVmcbStateSave->u64BR_TO);
    pHlp->pfnPrintf(pHlp, "%sLASTXCPT_FROM              = %#RX64\n", pszPrefix, pVmcbStateSave->u64LASTEXCPFROM);
    pHlp->pfnPrintf(pHlp, "%sLASTXCPT_TO                = %#RX64\n", pszPrefix, pVmcbStateSave->u64LASTEXCPTO);
}


/**
 * Displays a virtual-VMCS.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pHlp        The info helper functions.
 * @param   pVmcs       Pointer to a virtual VMCS.
 * @param   pszPrefix   Caller specified string prefix.
 */
static void cpumR3InfoVmxVmcs(PVMCPU pVCpu, PCDBGFINFOHLP pHlp, PCVMXVVMCS pVmcs, const char *pszPrefix)
{
    AssertReturnVoid(pHlp);
    AssertReturnVoid(pVmcs);

    /* The string width of -4 used in the macros below to cover 'LDTR', 'GDTR', 'IDTR. */
#define CPUMVMX_DUMP_HOST_XDTR(a_pHlp, a_pVmcs, a_Seg, a_SegName, a_pszPrefix) \
    do { \
        (a_pHlp)->pfnPrintf((a_pHlp), "  %s%-4s                       = {base=%016RX64}\n", \
                            (a_pszPrefix), (a_SegName), (a_pVmcs)->u64Host##a_Seg##Base.u); \
    } while (0)

#define CPUMVMX_DUMP_HOST_FS_GS_TR(a_pHlp, a_pVmcs, a_Seg, a_SegName, a_pszPrefix) \
    do { \
        (a_pHlp)->pfnPrintf((a_pHlp), "  %s%-4s                       = {%04x base=%016RX64}\n", \
                            (a_pszPrefix), (a_SegName), (a_pVmcs)->Host##a_Seg, (a_pVmcs)->u64Host##a_Seg##Base.u); \
    } while (0)

#define CPUMVMX_DUMP_GUEST_SEGREG(a_pHlp, a_pVmcs, a_Seg, a_SegName, a_pszPrefix) \
    do { \
        (a_pHlp)->pfnPrintf((a_pHlp), "  %s%-4s                       = {%04x base=%016RX64 limit=%08x flags=%04x}\n", \
                            (a_pszPrefix), (a_SegName), (a_pVmcs)->Guest##a_Seg, (a_pVmcs)->u64Guest##a_Seg##Base.u, \
                            (a_pVmcs)->u32Guest##a_Seg##Limit, (a_pVmcs)->u32Guest##a_Seg##Attr); \
    } while (0)

#define CPUMVMX_DUMP_GUEST_XDTR(a_pHlp, a_pVmcs, a_Seg, a_SegName, a_pszPrefix) \
    do { \
        (a_pHlp)->pfnPrintf((a_pHlp), "  %s%-4s                       = {base=%016RX64 limit=%08x}\n", \
                            (a_pszPrefix), (a_SegName), (a_pVmcs)->u64Guest##a_Seg##Base.u, (a_pVmcs)->u32Guest##a_Seg##Limit); \
    } while (0)

    /* Header. */
    {
        pHlp->pfnPrintf(pHlp, "%sHeader:\n", pszPrefix);
        pHlp->pfnPrintf(pHlp, "  %sVMCS revision id           = %#RX32\n",   pszPrefix, pVmcs->u32VmcsRevId);
        pHlp->pfnPrintf(pHlp, "  %sVMX-abort id               = %#RX32 (%s)\n", pszPrefix, pVmcs->enmVmxAbort, VMXGetAbortDesc(pVmcs->enmVmxAbort));
        pHlp->pfnPrintf(pHlp, "  %sVMCS state                 = %#x (%s)\n", pszPrefix, pVmcs->fVmcsState, VMXGetVmcsStateDesc(pVmcs->fVmcsState));
    }

    /* Control fields. */
    {
        /* 16-bit. */
        pHlp->pfnPrintf(pHlp, "%sControl:\n", pszPrefix);
        pHlp->pfnPrintf(pHlp, "  %sVPID                       = %#RX16\n",   pszPrefix, pVmcs->u16Vpid);
        pHlp->pfnPrintf(pHlp, "  %sPosted intr notify vector  = %#RX16\n",   pszPrefix, pVmcs->u16PostIntNotifyVector);
        pHlp->pfnPrintf(pHlp, "  %sEPTP index                 = %#RX16\n",   pszPrefix, pVmcs->u16EptpIndex);
        pHlp->pfnPrintf(pHlp, "  %sHLAT prefix size           = %#RX16\n",   pszPrefix, pVmcs->u16HlatPrefixSize);

        /* 32-bit. */
        pHlp->pfnPrintf(pHlp, "  %sPin ctls                   = %#RX32\n",   pszPrefix, pVmcs->u32PinCtls);
        pHlp->pfnPrintf(pHlp, "  %sProcessor ctls             = %#RX32\n",   pszPrefix, pVmcs->u32ProcCtls);
        pHlp->pfnPrintf(pHlp, "  %sSecondary processor ctls   = %#RX32\n",   pszPrefix, pVmcs->u32ProcCtls2);
        pHlp->pfnPrintf(pHlp, "  %sVM-exit ctls               = %#RX32\n",   pszPrefix, pVmcs->u32ExitCtls);
        pHlp->pfnPrintf(pHlp, "  %sVM-entry ctls              = %#RX32\n",   pszPrefix, pVmcs->u32EntryCtls);
        pHlp->pfnPrintf(pHlp, "  %sException bitmap           = %#RX32\n",   pszPrefix, pVmcs->u32XcptBitmap);
        pHlp->pfnPrintf(pHlp, "  %sPage-fault mask            = %#RX32\n",   pszPrefix, pVmcs->u32XcptPFMask);
        pHlp->pfnPrintf(pHlp, "  %sPage-fault match           = %#RX32\n",   pszPrefix, pVmcs->u32XcptPFMatch);
        pHlp->pfnPrintf(pHlp, "  %sCR3-target count           = %RU32\n",    pszPrefix, pVmcs->u32Cr3TargetCount);
        pHlp->pfnPrintf(pHlp, "  %sVM-exit MSR store count    = %RU32\n",    pszPrefix, pVmcs->u32ExitMsrStoreCount);
        pHlp->pfnPrintf(pHlp, "  %sVM-exit MSR load count     = %RU32\n",    pszPrefix, pVmcs->u32ExitMsrLoadCount);
        pHlp->pfnPrintf(pHlp, "  %sVM-entry MSR load count    = %RU32\n",    pszPrefix, pVmcs->u32EntryMsrLoadCount);
        pHlp->pfnPrintf(pHlp, "  %sVM-entry interruption info = %#RX32\n",   pszPrefix, pVmcs->u32EntryIntInfo);
        {
            uint32_t const fInfo = pVmcs->u32EntryIntInfo;
            uint8_t  const uType = VMX_ENTRY_INT_INFO_TYPE(fInfo);
            pHlp->pfnPrintf(pHlp, "    %sValid                      = %RTbool\n", pszPrefix, VMX_ENTRY_INT_INFO_IS_VALID(fInfo));
            pHlp->pfnPrintf(pHlp, "    %sType                       = %#x (%s)\n", pszPrefix, uType, VMXGetEntryIntInfoTypeDesc(uType));
            pHlp->pfnPrintf(pHlp, "    %sVector                     = %#x\n",     pszPrefix, VMX_ENTRY_INT_INFO_VECTOR(fInfo));
            pHlp->pfnPrintf(pHlp, "    %sNMI-unblocking-IRET        = %RTbool\n", pszPrefix, VMX_ENTRY_INT_INFO_IS_NMI_UNBLOCK_IRET(fInfo));
            pHlp->pfnPrintf(pHlp, "    %sError-code valid           = %RTbool\n", pszPrefix, VMX_ENTRY_INT_INFO_IS_ERROR_CODE_VALID(fInfo));
        }
        pHlp->pfnPrintf(pHlp, "  %sVM-entry xcpt error-code   = %#RX32\n",   pszPrefix, pVmcs->u32EntryXcptErrCode);
        pHlp->pfnPrintf(pHlp, "  %sVM-entry instr length      = %u byte(s)\n", pszPrefix, pVmcs->u32EntryInstrLen);
        pHlp->pfnPrintf(pHlp, "  %sTPR threshold              = %#RX32\n",   pszPrefix, pVmcs->u32TprThreshold);
        pHlp->pfnPrintf(pHlp, "  %sPLE gap                    = %#RX32\n",   pszPrefix, pVmcs->u32PleGap);
        pHlp->pfnPrintf(pHlp, "  %sPLE window                 = %#RX32\n",   pszPrefix, pVmcs->u32PleWindow);

        /* 64-bit. */
        pHlp->pfnPrintf(pHlp, "  %sIO-bitmap A addr           = %#RX64\n",   pszPrefix, pVmcs->u64AddrIoBitmapA.u);
        pHlp->pfnPrintf(pHlp, "  %sIO-bitmap B addr           = %#RX64\n",   pszPrefix, pVmcs->u64AddrIoBitmapB.u);
        pHlp->pfnPrintf(pHlp, "  %sMSR-bitmap addr            = %#RX64\n",   pszPrefix, pVmcs->u64AddrMsrBitmap.u);
        pHlp->pfnPrintf(pHlp, "  %sVM-exit MSR store addr     = %#RX64\n",   pszPrefix, pVmcs->u64AddrExitMsrStore.u);
        pHlp->pfnPrintf(pHlp, "  %sVM-exit MSR load addr      = %#RX64\n",   pszPrefix, pVmcs->u64AddrExitMsrLoad.u);
        pHlp->pfnPrintf(pHlp, "  %sVM-entry MSR load addr     = %#RX64\n",   pszPrefix, pVmcs->u64AddrEntryMsrLoad.u);
        pHlp->pfnPrintf(pHlp, "  %sExecutive VMCS ptr         = %#RX64\n",   pszPrefix, pVmcs->u64ExecVmcsPtr.u);
        pHlp->pfnPrintf(pHlp, "  %sPML addr                   = %#RX64\n",   pszPrefix, pVmcs->u64AddrPml.u);
        pHlp->pfnPrintf(pHlp, "  %sTSC offset                 = %#RX64\n",   pszPrefix, pVmcs->u64TscOffset.u);
        pHlp->pfnPrintf(pHlp, "  %sVirtual-APIC addr          = %#RX64\n",   pszPrefix, pVmcs->u64AddrVirtApic.u);
        pHlp->pfnPrintf(pHlp, "  %sAPIC-access addr           = %#RX64\n",   pszPrefix, pVmcs->u64AddrApicAccess.u);
        pHlp->pfnPrintf(pHlp, "  %sPosted-intr desc addr      = %#RX64\n",   pszPrefix, pVmcs->u64AddrPostedIntDesc.u);
        pHlp->pfnPrintf(pHlp, "  %sVM-functions control       = %#RX64\n",   pszPrefix, pVmcs->u64VmFuncCtls.u);
        pHlp->pfnPrintf(pHlp, "  %sEPTP ptr                   = %#RX64\n",   pszPrefix, pVmcs->u64EptPtr.u);
        pHlp->pfnPrintf(pHlp, "  %sEOI-exit bitmap 0          = %#RX64\n",   pszPrefix, pVmcs->u64EoiExitBitmap0.u);
        pHlp->pfnPrintf(pHlp, "  %sEOI-exit bitmap 1          = %#RX64\n",   pszPrefix, pVmcs->u64EoiExitBitmap1.u);
        pHlp->pfnPrintf(pHlp, "  %sEOI-exit bitmap 2          = %#RX64\n",   pszPrefix, pVmcs->u64EoiExitBitmap2.u);
        pHlp->pfnPrintf(pHlp, "  %sEOI-exit bitmap 3          = %#RX64\n",   pszPrefix, pVmcs->u64EoiExitBitmap3.u);
        pHlp->pfnPrintf(pHlp, "  %sEPTP-list addr             = %#RX64\n",   pszPrefix, pVmcs->u64AddrEptpList.u);
        pHlp->pfnPrintf(pHlp, "  %sVMREAD-bitmap addr         = %#RX64\n",   pszPrefix, pVmcs->u64AddrVmreadBitmap.u);
        pHlp->pfnPrintf(pHlp, "  %sVMWRITE-bitmap addr        = %#RX64\n",   pszPrefix, pVmcs->u64AddrVmwriteBitmap.u);
        pHlp->pfnPrintf(pHlp, "  %sVirt-Xcpt info addr        = %#RX64\n",   pszPrefix, pVmcs->u64AddrXcptVeInfo.u);
        pHlp->pfnPrintf(pHlp, "  %sXSS-exiting bitmap         = %#RX64\n",   pszPrefix, pVmcs->u64XssExitBitmap.u);
        pHlp->pfnPrintf(pHlp, "  %sENCLS-exiting bitmap       = %#RX64\n",   pszPrefix, pVmcs->u64EnclsExitBitmap.u);
        pHlp->pfnPrintf(pHlp, "  %sSPP-table ptr              = %#RX64\n",   pszPrefix, pVmcs->u64SppTablePtr.u);
        pHlp->pfnPrintf(pHlp, "  %sTSC multiplier             = %#RX64\n",   pszPrefix, pVmcs->u64TscMultiplier.u);
        pHlp->pfnPrintf(pHlp, "  %sTertiary processor ctls    = %#RX64\n",   pszPrefix, pVmcs->u64ProcCtls3.u);
        pHlp->pfnPrintf(pHlp, "  %sENCLV-exiting bitmap       = %#RX64\n",   pszPrefix, pVmcs->u64EnclvExitBitmap.u);
        pHlp->pfnPrintf(pHlp, "  %sPCONFIG-exiting bitmap     = %#RX64\n",   pszPrefix, pVmcs->u64PconfigExitBitmap.u);
        pHlp->pfnPrintf(pHlp, "  %sHLAT ptr                   = %#RX64\n",   pszPrefix, pVmcs->u64HlatPtr.u);
        pHlp->pfnPrintf(pHlp, "  %sSecondary VM-exit controls = %#RX64\n",   pszPrefix, pVmcs->u64ExitCtls2.u);

        /* Natural width. */
        pHlp->pfnPrintf(pHlp, "  %sCR0 guest/host mask        = %#RX64\n",   pszPrefix, pVmcs->u64Cr0Mask.u);
        pHlp->pfnPrintf(pHlp, "  %sCR4 guest/host mask        = %#RX64\n",   pszPrefix, pVmcs->u64Cr4Mask.u);
        pHlp->pfnPrintf(pHlp, "  %sCR0 read shadow            = %#RX64\n",   pszPrefix, pVmcs->u64Cr0ReadShadow.u);
        pHlp->pfnPrintf(pHlp, "  %sCR4 read shadow            = %#RX64\n",   pszPrefix, pVmcs->u64Cr4ReadShadow.u);
        pHlp->pfnPrintf(pHlp, "  %sCR3-target 0               = %#RX64\n",   pszPrefix, pVmcs->u64Cr3Target0.u);
        pHlp->pfnPrintf(pHlp, "  %sCR3-target 1               = %#RX64\n",   pszPrefix, pVmcs->u64Cr3Target1.u);
        pHlp->pfnPrintf(pHlp, "  %sCR3-target 2               = %#RX64\n",   pszPrefix, pVmcs->u64Cr3Target2.u);
        pHlp->pfnPrintf(pHlp, "  %sCR3-target 3               = %#RX64\n",   pszPrefix, pVmcs->u64Cr3Target3.u);
    }

    /* Guest state. */
    {
        char szEFlags[80];
        cpumR3InfoFormatFlags(&szEFlags[0], pVmcs->u64GuestRFlags.u);
        pHlp->pfnPrintf(pHlp, "%sGuest state:\n", pszPrefix);

        /* 16-bit. */
        CPUMVMX_DUMP_GUEST_SEGREG(pHlp, pVmcs, Cs,   "CS",   pszPrefix);
        CPUMVMX_DUMP_GUEST_SEGREG(pHlp, pVmcs, Ss,   "SS",   pszPrefix);
        CPUMVMX_DUMP_GUEST_SEGREG(pHlp, pVmcs, Es,   "ES",   pszPrefix);
        CPUMVMX_DUMP_GUEST_SEGREG(pHlp, pVmcs, Ds,   "DS",   pszPrefix);
        CPUMVMX_DUMP_GUEST_SEGREG(pHlp, pVmcs, Fs,   "FS",   pszPrefix);
        CPUMVMX_DUMP_GUEST_SEGREG(pHlp, pVmcs, Gs,   "GS",   pszPrefix);
        CPUMVMX_DUMP_GUEST_SEGREG(pHlp, pVmcs, Ldtr, "LDTR", pszPrefix);
        CPUMVMX_DUMP_GUEST_SEGREG(pHlp, pVmcs, Tr,   "TR",   pszPrefix);
        CPUMVMX_DUMP_GUEST_XDTR(pHlp,   pVmcs, Gdtr, "GDTR", pszPrefix);
        CPUMVMX_DUMP_GUEST_XDTR(pHlp,   pVmcs, Idtr, "IDTR", pszPrefix);
        pHlp->pfnPrintf(pHlp, "  %sInterrupt status           = %#RX16\n",   pszPrefix, pVmcs->u16GuestIntStatus);
        pHlp->pfnPrintf(pHlp, "  %sPML index                  = %#RX16\n",   pszPrefix, pVmcs->u16PmlIndex);

        /* 32-bit. */
        pHlp->pfnPrintf(pHlp, "  %sInterruptibility state     = %#RX32\n",   pszPrefix, pVmcs->u32GuestIntrState);
        pHlp->pfnPrintf(pHlp, "  %sActivity state             = %#RX32\n",   pszPrefix, pVmcs->u32GuestActivityState);
        pHlp->pfnPrintf(pHlp, "  %sSMBASE                     = %#RX32\n",   pszPrefix, pVmcs->u32GuestSmBase);
        pHlp->pfnPrintf(pHlp, "  %sSysEnter CS                = %#RX32\n",   pszPrefix, pVmcs->u32GuestSysenterCS);
        pHlp->pfnPrintf(pHlp, "  %sVMX-preemption timer value = %#RX32\n",   pszPrefix, pVmcs->u32PreemptTimer);

        /* 64-bit. */
        pHlp->pfnPrintf(pHlp, "  %sVMCS link ptr              = %#RX64\n",   pszPrefix, pVmcs->u64VmcsLinkPtr.u);
        pHlp->pfnPrintf(pHlp, "  %sDBGCTL                     = %#RX64\n",   pszPrefix, pVmcs->u64GuestDebugCtlMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sPAT                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestPatMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sEFER                       = %#RX64\n",   pszPrefix, pVmcs->u64GuestEferMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sPERFGLOBALCTRL             = %#RX64\n",   pszPrefix, pVmcs->u64GuestPerfGlobalCtlMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sPDPTE 0                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestPdpte0.u);
        pHlp->pfnPrintf(pHlp, "  %sPDPTE 1                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestPdpte1.u);
        pHlp->pfnPrintf(pHlp, "  %sPDPTE 2                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestPdpte2.u);
        pHlp->pfnPrintf(pHlp, "  %sPDPTE 3                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestPdpte3.u);
        pHlp->pfnPrintf(pHlp, "  %sBNDCFGS                    = %#RX64\n",   pszPrefix, pVmcs->u64GuestBndcfgsMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sRTIT_CTL                   = %#RX64\n",   pszPrefix, pVmcs->u64GuestRtitCtlMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sPKRS                       = %#RX64\n",   pszPrefix, pVmcs->u64GuestPkrsMsr.u);

        /* Natural width. */
        pHlp->pfnPrintf(pHlp, "  %sCR0                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestCr0.u);
        pHlp->pfnPrintf(pHlp, "  %sCR3                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestCr3.u);
        pHlp->pfnPrintf(pHlp, "  %sCR4                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestCr4.u);
        pHlp->pfnPrintf(pHlp, "  %sDR7                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestDr7.u);
        pHlp->pfnPrintf(pHlp, "  %sRSP                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestRsp.u);
        pHlp->pfnPrintf(pHlp, "  %sRIP                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestRip.u);
        pHlp->pfnPrintf(pHlp, "  %sRFLAGS                     = %#RX64 %31s\n",pszPrefix, pVmcs->u64GuestRFlags.u, szEFlags);
        pHlp->pfnPrintf(pHlp, "  %sPending debug xcpts        = %#RX64\n",   pszPrefix, pVmcs->u64GuestPendingDbgXcpts.u);
        pHlp->pfnPrintf(pHlp, "  %sSysEnter ESP               = %#RX64\n",   pszPrefix, pVmcs->u64GuestSysenterEsp.u);
        pHlp->pfnPrintf(pHlp, "  %sSysEnter EIP               = %#RX64\n",   pszPrefix, pVmcs->u64GuestSysenterEip.u);
        pHlp->pfnPrintf(pHlp, "  %sS_CET                      = %#RX64\n",   pszPrefix, pVmcs->u64GuestSCetMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sSSP                        = %#RX64\n",   pszPrefix, pVmcs->u64GuestSsp.u);
        pHlp->pfnPrintf(pHlp, "  %sINTERRUPT_SSP_TABLE_ADDR   = %#RX64\n",   pszPrefix, pVmcs->u64GuestIntrSspTableAddrMsr.u);
    }

    /* Host state. */
    {
        pHlp->pfnPrintf(pHlp, "%sHost state:\n", pszPrefix);

        /* 16-bit. */
        pHlp->pfnPrintf(pHlp, "  %sCS                         = %#RX16\n",   pszPrefix, pVmcs->HostCs);
        pHlp->pfnPrintf(pHlp, "  %sSS                         = %#RX16\n",   pszPrefix, pVmcs->HostSs);
        pHlp->pfnPrintf(pHlp, "  %sDS                         = %#RX16\n",   pszPrefix, pVmcs->HostDs);
        pHlp->pfnPrintf(pHlp, "  %sES                         = %#RX16\n",   pszPrefix, pVmcs->HostEs);
        CPUMVMX_DUMP_HOST_FS_GS_TR(pHlp, pVmcs, Fs, "FS", pszPrefix);
        CPUMVMX_DUMP_HOST_FS_GS_TR(pHlp, pVmcs, Gs, "GS", pszPrefix);
        CPUMVMX_DUMP_HOST_FS_GS_TR(pHlp, pVmcs, Tr, "TR", pszPrefix);
        CPUMVMX_DUMP_HOST_XDTR(pHlp, pVmcs, Gdtr, "GDTR", pszPrefix);
        CPUMVMX_DUMP_HOST_XDTR(pHlp, pVmcs, Idtr, "IDTR", pszPrefix);

        /* 32-bit. */
        pHlp->pfnPrintf(pHlp, "  %sSysEnter CS                = %#RX32\n",   pszPrefix, pVmcs->u32HostSysenterCs);

        /* 64-bit. */
        pHlp->pfnPrintf(pHlp, "  %sEFER                       = %#RX64\n",   pszPrefix, pVmcs->u64HostEferMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sPAT                        = %#RX64\n",   pszPrefix, pVmcs->u64HostPatMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sPERFGLOBALCTRL             = %#RX64\n",   pszPrefix, pVmcs->u64HostPerfGlobalCtlMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sPKRS                       = %#RX64\n",   pszPrefix, pVmcs->u64HostPkrsMsr.u);

        /* Natural width. */
        pHlp->pfnPrintf(pHlp, "  %sCR0                        = %#RX64\n",   pszPrefix, pVmcs->u64HostCr0.u);
        pHlp->pfnPrintf(pHlp, "  %sCR3                        = %#RX64\n",   pszPrefix, pVmcs->u64HostCr3.u);
        pHlp->pfnPrintf(pHlp, "  %sCR4                        = %#RX64\n",   pszPrefix, pVmcs->u64HostCr4.u);
        pHlp->pfnPrintf(pHlp, "  %sSysEnter ESP               = %#RX64\n",   pszPrefix, pVmcs->u64HostSysenterEsp.u);
        pHlp->pfnPrintf(pHlp, "  %sSysEnter EIP               = %#RX64\n",   pszPrefix, pVmcs->u64HostSysenterEip.u);
        pHlp->pfnPrintf(pHlp, "  %sRSP                        = %#RX64\n",   pszPrefix, pVmcs->u64HostRsp.u);
        pHlp->pfnPrintf(pHlp, "  %sRIP                        = %#RX64\n",   pszPrefix, pVmcs->u64HostRip.u);
        pHlp->pfnPrintf(pHlp, "  %sS_CET                      = %#RX64\n",   pszPrefix, pVmcs->u64HostSCetMsr.u);
        pHlp->pfnPrintf(pHlp, "  %sSSP                        = %#RX64\n",   pszPrefix, pVmcs->u64HostSsp.u);
        pHlp->pfnPrintf(pHlp, "  %sINTERRUPT_SSP_TABLE_ADDR   = %#RX64\n",   pszPrefix, pVmcs->u64HostIntrSspTableAddrMsr.u);
    }

    /* Read-only fields. */
    {
        pHlp->pfnPrintf(pHlp, "%sRead-only data fields:\n", pszPrefix);

        /* 16-bit (none currently). */

        /* 32-bit. */
        pHlp->pfnPrintf(pHlp, "  %sExit reason                = %u (%s)\n",  pszPrefix, pVmcs->u32RoExitReason, HMGetVmxExitName(pVmcs->u32RoExitReason));
        pHlp->pfnPrintf(pHlp, "  %sExit qualification         = %#RX64\n",   pszPrefix, pVmcs->u64RoExitQual.u);
        pHlp->pfnPrintf(pHlp, "  %sVM-instruction error       = %#RX32\n",   pszPrefix, pVmcs->u32RoVmInstrError);
        pHlp->pfnPrintf(pHlp, "  %sVM-exit intr info          = %#RX32\n",   pszPrefix, pVmcs->u32RoExitIntInfo);
        {
            uint32_t const fInfo = pVmcs->u32RoExitIntInfo;
            uint8_t  const uType = VMX_EXIT_INT_INFO_TYPE(fInfo);
            pHlp->pfnPrintf(pHlp, "    %sValid                      = %RTbool\n", pszPrefix, VMX_EXIT_INT_INFO_IS_VALID(fInfo));
            pHlp->pfnPrintf(pHlp, "    %sType                       = %#x (%s)\n",     pszPrefix, uType, VMXGetExitIntInfoTypeDesc(uType));
            pHlp->pfnPrintf(pHlp, "    %sVector                     = %#x\n",     pszPrefix, VMX_EXIT_INT_INFO_VECTOR(fInfo));
            pHlp->pfnPrintf(pHlp, "    %sNMI-unblocking-IRET        = %RTbool\n", pszPrefix, VMX_EXIT_INT_INFO_IS_NMI_UNBLOCK_IRET(fInfo));
            pHlp->pfnPrintf(pHlp, "    %sError-code valid           = %RTbool\n", pszPrefix, VMX_EXIT_INT_INFO_IS_ERROR_CODE_VALID(fInfo));
        }
        pHlp->pfnPrintf(pHlp, "  %sVM-exit intr error-code    = %#RX32\n",   pszPrefix, pVmcs->u32RoExitIntErrCode);
        pHlp->pfnPrintf(pHlp, "  %sIDT-vectoring info         = %#RX32\n",   pszPrefix, pVmcs->u32RoIdtVectoringInfo);
        {
            uint32_t const fInfo = pVmcs->u32RoIdtVectoringInfo;
            uint8_t  const uType = VMX_IDT_VECTORING_INFO_TYPE(fInfo);
            pHlp->pfnPrintf(pHlp, "    %sValid                      = %RTbool\n", pszPrefix, VMX_IDT_VECTORING_INFO_IS_VALID(fInfo));
            pHlp->pfnPrintf(pHlp, "    %sType                       = %#x (%s)\n",     pszPrefix, uType, VMXGetIdtVectoringInfoTypeDesc(uType));
            pHlp->pfnPrintf(pHlp, "    %sVector                     = %#x\n",     pszPrefix, VMX_IDT_VECTORING_INFO_VECTOR(fInfo));
            pHlp->pfnPrintf(pHlp, "    %sError-code valid           = %RTbool\n", pszPrefix, VMX_IDT_VECTORING_INFO_IS_ERROR_CODE_VALID(fInfo));
        }
        pHlp->pfnPrintf(pHlp, "  %sIDT-vectoring error-code   = %#RX32\n",   pszPrefix, pVmcs->u32RoIdtVectoringErrCode);
        pHlp->pfnPrintf(pHlp, "  %sVM-exit instruction length = %u byte(s)\n", pszPrefix, pVmcs->u32RoExitInstrLen);
        pHlp->pfnPrintf(pHlp, "  %sVM-exit instruction info   = %#RX64\n",   pszPrefix, pVmcs->u32RoExitInstrInfo);

        /* 64-bit. */
        pHlp->pfnPrintf(pHlp, "  %sGuest-physical addr        = %#RX64\n",   pszPrefix, pVmcs->u64RoGuestPhysAddr.u);

        /* Natural width. */
        pHlp->pfnPrintf(pHlp, "  %sI/O RCX                    = %#RX64\n",   pszPrefix, pVmcs->u64RoIoRcx.u);
        pHlp->pfnPrintf(pHlp, "  %sI/O RSI                    = %#RX64\n",   pszPrefix, pVmcs->u64RoIoRsi.u);
        pHlp->pfnPrintf(pHlp, "  %sI/O RDI                    = %#RX64\n",   pszPrefix, pVmcs->u64RoIoRdi.u);
        pHlp->pfnPrintf(pHlp, "  %sI/O RIP                    = %#RX64\n",   pszPrefix, pVmcs->u64RoIoRip.u);
        pHlp->pfnPrintf(pHlp, "  %sGuest-linear addr          = %#RX64\n",   pszPrefix, pVmcs->u64RoGuestLinearAddr.u);
    }

#ifdef DEBUG_ramshankar
    if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW)
    {
        void *pvPage = RTMemTmpAllocZ(VMX_V_VIRT_APIC_SIZE);
        Assert(pvPage);
        RTGCPHYS const GCPhysVirtApic = pVmcs->u64AddrVirtApic.u;
        int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), pvPage, GCPhysVirtApic, VMX_V_VIRT_APIC_SIZE);
        if (RT_SUCCESS(rc))
        {
            pHlp->pfnPrintf(pHlp, "  %sVirtual-APIC page\n", pszPrefix);
            pHlp->pfnPrintf(pHlp, "%.*Rhxs\n", VMX_V_VIRT_APIC_SIZE, pvPage);
            pHlp->pfnPrintf(pHlp, "\n");
        }
        RTMemTmpFree(pvPage);
    }
#else
    NOREF(pVCpu);
#endif

#undef CPUMVMX_DUMP_HOST_XDTR
#undef CPUMVMX_DUMP_HOST_FS_GS_TR
#undef CPUMVMX_DUMP_GUEST_SEGREG
#undef CPUMVMX_DUMP_GUEST_XDTR
}


/**
 * Display the guest's hardware-virtualization cpu state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoGuestHwvirt(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    PCCPUMCTX pCtx = &pVCpu->cpum.s.Guest;
    bool const fSvm = pVM->cpum.s.GuestFeatures.fSvm;
    bool const fVmx = pVM->cpum.s.GuestFeatures.fVmx;

    pHlp->pfnPrintf(pHlp, "VCPU[%u] hardware virtualization state:\n", pVCpu->idCpu);
    pHlp->pfnPrintf(pHlp, "fSavedInhibit                = %#RX32\n",  pCtx->hwvirt.fSavedInhibit);
    pHlp->pfnPrintf(pHlp, "In nested-guest hwvirt mode  = %RTbool\n", CPUMIsGuestInNestedHwvirtMode(pCtx));

    if (fSvm)
    {
        pHlp->pfnPrintf(pHlp, "SVM hwvirt state:\n");
        pHlp->pfnPrintf(pHlp, "  fGif                       = %RTbool\n", pCtx->hwvirt.fGif);

        char szEFlags[80];
        cpumR3InfoFormatFlags(&szEFlags[0], pCtx->hwvirt.svm.HostState.rflags.u);
        pHlp->pfnPrintf(pHlp, "  uMsrHSavePa                = %#RX64\n",    pCtx->hwvirt.svm.uMsrHSavePa);
        pHlp->pfnPrintf(pHlp, "  GCPhysVmcb                 = %#RGp\n",     pCtx->hwvirt.svm.GCPhysVmcb);
        pHlp->pfnPrintf(pHlp, "  VmcbCtrl:\n");
        cpumR3InfoSvmVmcbCtrl(pHlp, &pCtx->hwvirt.svm.Vmcb.ctrl,       "    " /* pszPrefix */);
        pHlp->pfnPrintf(pHlp, "  VmcbStateSave:\n");
        cpumR3InfoSvmVmcbStateSave(pHlp, &pCtx->hwvirt.svm.Vmcb.guest, "    " /* pszPrefix */);
        pHlp->pfnPrintf(pHlp, "  HostState:\n");
        pHlp->pfnPrintf(pHlp, "    uEferMsr                   = %#RX64\n",  pCtx->hwvirt.svm.HostState.uEferMsr);
        pHlp->pfnPrintf(pHlp, "    uCr0                       = %#RX64\n",  pCtx->hwvirt.svm.HostState.uCr0);
        pHlp->pfnPrintf(pHlp, "    uCr4                       = %#RX64\n",  pCtx->hwvirt.svm.HostState.uCr4);
        pHlp->pfnPrintf(pHlp, "    uCr3                       = %#RX64\n",  pCtx->hwvirt.svm.HostState.uCr3);
        pHlp->pfnPrintf(pHlp, "    uRip                       = %#RX64\n",  pCtx->hwvirt.svm.HostState.uRip);
        pHlp->pfnPrintf(pHlp, "    uRsp                       = %#RX64\n",  pCtx->hwvirt.svm.HostState.uRsp);
        pHlp->pfnPrintf(pHlp, "    uRax                       = %#RX64\n",  pCtx->hwvirt.svm.HostState.uRax);
        pHlp->pfnPrintf(pHlp, "    rflags                     = %#RX64 %31s\n", pCtx->hwvirt.svm.HostState.rflags.u64, szEFlags);
        PCCPUMSELREG pSelEs = &pCtx->hwvirt.svm.HostState.es;
        pHlp->pfnPrintf(pHlp, "    es                         = {%04x base=%016RX64 limit=%08x flags=%08x}\n",
                        pSelEs->Sel, pSelEs->u64Base, pSelEs->u32Limit, pSelEs->Attr.u);
        PCCPUMSELREG pSelCs = &pCtx->hwvirt.svm.HostState.cs;
        pHlp->pfnPrintf(pHlp, "    cs                         = {%04x base=%016RX64 limit=%08x flags=%08x}\n",
                        pSelCs->Sel, pSelCs->u64Base, pSelCs->u32Limit, pSelCs->Attr.u);
        PCCPUMSELREG pSelSs = &pCtx->hwvirt.svm.HostState.ss;
        pHlp->pfnPrintf(pHlp, "    ss                         = {%04x base=%016RX64 limit=%08x flags=%08x}\n",
                        pSelSs->Sel, pSelSs->u64Base, pSelSs->u32Limit, pSelSs->Attr.u);
        PCCPUMSELREG pSelDs = &pCtx->hwvirt.svm.HostState.ds;
        pHlp->pfnPrintf(pHlp, "    ds                         = {%04x base=%016RX64 limit=%08x flags=%08x}\n",
                        pSelDs->Sel, pSelDs->u64Base, pSelDs->u32Limit, pSelDs->Attr.u);
        pHlp->pfnPrintf(pHlp, "    gdtr                       = %016RX64:%04x\n", pCtx->hwvirt.svm.HostState.gdtr.pGdt,
                        pCtx->hwvirt.svm.HostState.gdtr.cbGdt);
        pHlp->pfnPrintf(pHlp, "    idtr                       = %016RX64:%04x\n", pCtx->hwvirt.svm.HostState.idtr.pIdt,
                        pCtx->hwvirt.svm.HostState.idtr.cbIdt);
        pHlp->pfnPrintf(pHlp, "  cPauseFilter               = %RU16\n",     pCtx->hwvirt.svm.cPauseFilter);
        pHlp->pfnPrintf(pHlp, "  cPauseFilterThreshold      = %RU32\n",     pCtx->hwvirt.svm.cPauseFilterThreshold);
        pHlp->pfnPrintf(pHlp, "  fInterceptEvents           = %u\n",        pCtx->hwvirt.svm.fInterceptEvents);
    }
    else if (fVmx)
    {
        pHlp->pfnPrintf(pHlp, "VMX hwvirt state:\n");
        pHlp->pfnPrintf(pHlp, "  GCPhysVmxon                = %#RGp\n",     pCtx->hwvirt.vmx.GCPhysVmxon);
        pHlp->pfnPrintf(pHlp, "  GCPhysVmcs                 = %#RGp\n",     pCtx->hwvirt.vmx.GCPhysVmcs);
        pHlp->pfnPrintf(pHlp, "  GCPhysShadowVmcs           = %#RGp\n",     pCtx->hwvirt.vmx.GCPhysShadowVmcs);
        pHlp->pfnPrintf(pHlp, "  enmDiag                    = %u (%s)\n",   pCtx->hwvirt.vmx.enmDiag, HMGetVmxDiagDesc(pCtx->hwvirt.vmx.enmDiag));
        pHlp->pfnPrintf(pHlp, "  uDiagAux                   = %#RX64\n",    pCtx->hwvirt.vmx.uDiagAux);
        pHlp->pfnPrintf(pHlp, "  enmAbort                   = %u (%s)\n",   pCtx->hwvirt.vmx.enmAbort, VMXGetAbortDesc(pCtx->hwvirt.vmx.enmAbort));
        pHlp->pfnPrintf(pHlp, "  uAbortAux                  = %u (%#x)\n",  pCtx->hwvirt.vmx.uAbortAux, pCtx->hwvirt.vmx.uAbortAux);
        pHlp->pfnPrintf(pHlp, "  fInVmxRootMode             = %RTbool\n",   pCtx->hwvirt.vmx.fInVmxRootMode);
        pHlp->pfnPrintf(pHlp, "  fInVmxNonRootMode          = %RTbool\n",   pCtx->hwvirt.vmx.fInVmxNonRootMode);
        pHlp->pfnPrintf(pHlp, "  fInterceptEvents           = %RTbool\n",   pCtx->hwvirt.vmx.fInterceptEvents);
        pHlp->pfnPrintf(pHlp, "  fNmiUnblockingIret         = %RTbool\n",   pCtx->hwvirt.vmx.fNmiUnblockingIret);
        pHlp->pfnPrintf(pHlp, "  uFirstPauseLoopTick        = %RX64\n",     pCtx->hwvirt.vmx.uFirstPauseLoopTick);
        pHlp->pfnPrintf(pHlp, "  uPrevPauseTick             = %RX64\n",     pCtx->hwvirt.vmx.uPrevPauseTick);
        pHlp->pfnPrintf(pHlp, "  uEntryTick                 = %RX64\n",     pCtx->hwvirt.vmx.uEntryTick);
        pHlp->pfnPrintf(pHlp, "  offVirtApicWrite           = %#RX16\n",    pCtx->hwvirt.vmx.offVirtApicWrite);
        pHlp->pfnPrintf(pHlp, "  fVirtNmiBlocking           = %RTbool\n",   pCtx->hwvirt.vmx.fVirtNmiBlocking);
        pHlp->pfnPrintf(pHlp, "  VMCS cache:\n");
        cpumR3InfoVmxVmcs(pVCpu, pHlp, &pCtx->hwvirt.vmx.Vmcs, "  " /* pszPrefix */);
    }
    else
        pHlp->pfnPrintf(pHlp, "Hwvirt state disabled.\n");

#undef CPUMHWVIRTDUMP_NONE
#undef CPUMHWVIRTDUMP_COMMON
#undef CPUMHWVIRTDUMP_SVM
#undef CPUMHWVIRTDUMP_VMX
#undef CPUMHWVIRTDUMP_LAST
#undef CPUMHWVIRTDUMP_ALL
}

/**
 * Display the current guest instruction
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoGuestInstr(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    char szInstruction[256];
    szInstruction[0] = '\0';
    DBGFR3DisasInstrCurrent(pVCpu, szInstruction, sizeof(szInstruction));
    pHlp->pfnPrintf(pHlp, "\nCPUM%u: %s\n\n", pVCpu->idCpu, szInstruction);
}


/**
 * Display the hypervisor cpu state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoHyper(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    CPUMDUMPTYPE enmType;
    const char *pszComment;
    cpumR3InfoParseArg(pszArgs, &enmType, &pszComment);
    pHlp->pfnPrintf(pHlp, "Hypervisor CPUM state: %s\n", pszComment);

    pHlp->pfnPrintf(pHlp,
                    ".dr0=%016RX64 .dr1=%016RX64 .dr2=%016RX64 .dr3=%016RX64\n"
                    ".dr4=%016RX64 .dr5=%016RX64 .dr6=%016RX64 .dr7=%016RX64\n",
                    pVCpu->cpum.s.Hyper.dr[0], pVCpu->cpum.s.Hyper.dr[1], pVCpu->cpum.s.Hyper.dr[2], pVCpu->cpum.s.Hyper.dr[3],
                    pVCpu->cpum.s.Hyper.dr[4], pVCpu->cpum.s.Hyper.dr[5], pVCpu->cpum.s.Hyper.dr[6], pVCpu->cpum.s.Hyper.dr[7]);
    pHlp->pfnPrintf(pHlp, "CR4OrMask=%#x CR4AndMask=%#x\n", pVM->cpum.s.CR4.OrMask, pVM->cpum.s.CR4.AndMask);
}


/**
 * Display the host cpu state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoHost(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    CPUMDUMPTYPE enmType;
    const char *pszComment;
    cpumR3InfoParseArg(pszArgs, &enmType, &pszComment);
    pHlp->pfnPrintf(pHlp, "Host CPUM state: %s\n", pszComment);

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    PCPUMHOSTCTX pCtx = &pVCpu->cpum.s.Host;

    /*
     * Format the EFLAGS.
     */
    uint64_t efl = pCtx->rflags;
    char szEFlags[80];
    cpumR3InfoFormatFlags(&szEFlags[0], efl);

    /*
     * Format the registers.
     */
    pHlp->pfnPrintf(pHlp,
        "rax=xxxxxxxxxxxxxxxx rbx=%016RX64 rcx=xxxxxxxxxxxxxxxx\n"
        "rdx=xxxxxxxxxxxxxxxx rsi=%016RX64 rdi=%016RX64\n"
        "rip=xxxxxxxxxxxxxxxx rsp=%016RX64 rbp=%016RX64\n"
        " r8=xxxxxxxxxxxxxxxx  r9=xxxxxxxxxxxxxxxx r10=%016RX64\n"
        "r11=%016RX64 r12=%016RX64 r13=%016RX64\n"
        "r14=%016RX64 r15=%016RX64\n"
        "iopl=%d  %31s\n"
        "cs=%04x  ds=%04x  es=%04x  fs=%04x  gs=%04x                   eflags=%08RX64\n"
        "cr0=%016RX64 cr2=xxxxxxxxxxxxxxxx cr3=%016RX64\n"
        "cr4=%016RX64 ldtr=%04x tr=%04x\n"
        "dr[0]=%016RX64 dr[1]=%016RX64 dr[2]=%016RX64\n"
        "dr[3]=%016RX64 dr[6]=%016RX64 dr[7]=%016RX64\n"
        "gdtr=%016RX64:%04x  idtr=%016RX64:%04x\n"
        "SysEnter={cs=%04x eip=%08x esp=%08x}\n"
        "FSbase=%016RX64 GSbase=%016RX64 efer=%08RX64\n"
        ,
        /*pCtx->rax,*/ pCtx->rbx, /*pCtx->rcx,
        pCtx->rdx,*/ pCtx->rsi, pCtx->rdi,
        /*pCtx->rip,*/ pCtx->rsp, pCtx->rbp,
        /*pCtx->r8,  pCtx->r9,*/  pCtx->r10,
        pCtx->r11, pCtx->r12, pCtx->r13,
        pCtx->r14, pCtx->r15,
        X86_EFL_GET_IOPL(efl), szEFlags,
        pCtx->cs, pCtx->ds, pCtx->es, pCtx->fs, pCtx->gs, efl,
        pCtx->cr0, /*pCtx->cr2,*/ pCtx->cr3,
        pCtx->cr4, pCtx->ldtr, pCtx->tr,
        pCtx->dr0, pCtx->dr1, pCtx->dr2,
        pCtx->dr3, pCtx->dr6, pCtx->dr7,
        pCtx->gdtr.uAddr, pCtx->gdtr.cb, pCtx->idtr.uAddr, pCtx->idtr.cb,
        pCtx->SysEnter.cs, pCtx->SysEnter.eip, pCtx->SysEnter.esp,
        pCtx->FSbase, pCtx->GSbase, pCtx->efer);
}

/**
 * Structure used when disassembling and instructions in DBGF.
 * This is used so the reader function can get the stuff it needs.
 */
typedef struct CPUMDISASSTATE
{
    /** Pointer to the CPU structure. */
    PDISSTATE       pDis;
    /** Pointer to the VM. */
    PVM             pVM;
    /** Pointer to the VMCPU. */
    PVMCPU          pVCpu;
    /** Pointer to the first byte in the segment. */
    RTGCUINTPTR     GCPtrSegBase;
    /** Pointer to the byte after the end of the segment. (might have wrapped!) */
    RTGCUINTPTR     GCPtrSegEnd;
    /** The size of the segment minus 1. */
    RTGCUINTPTR     cbSegLimit;
    /** Pointer to the current page - R3 Ptr. */
    void const     *pvPageR3;
    /** Pointer to the current page - GC Ptr. */
    RTGCPTR         pvPageGC;
    /** The lock information that PGMPhysReleasePageMappingLock needs. */
    PGMPAGEMAPLOCK  PageMapLock;
    /** Whether the PageMapLock is valid or not. */
    bool            fLocked;
    /** 64 bits mode or not. */
    bool            f64Bits;
} CPUMDISASSTATE, *PCPUMDISASSTATE;


/**
 * @callback_method_impl{FNDISREADBYTES}
 */
static DECLCALLBACK(int) cpumR3DisasInstrRead(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMinRead, uint8_t cbMaxRead)
{
    PCPUMDISASSTATE pState = (PCPUMDISASSTATE)pDis->pvUser;
    for (;;)
    {
        RTGCUINTPTR GCPtr = pDis->uInstrAddr + offInstr + pState->GCPtrSegBase;

        /*
         * Need to update the page translation?
         */
        if (   !pState->pvPageR3
            || (GCPtr >> GUEST_PAGE_SHIFT) != (pState->pvPageGC >> GUEST_PAGE_SHIFT))
        {
            /* translate the address */
            pState->pvPageGC = GCPtr & ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK;

            /* Release mapping lock previously acquired. */
            if (pState->fLocked)
                PGMPhysReleasePageMappingLock(pState->pVM, &pState->PageMapLock);
            int rc = PGMPhysGCPtr2CCPtrReadOnly(pState->pVCpu, pState->pvPageGC, &pState->pvPageR3, &pState->PageMapLock);
            if (RT_SUCCESS(rc))
                pState->fLocked  = true;
            else
            {
                pState->fLocked  = false;
                pState->pvPageR3 = NULL;
                return rc;
            }
        }

        /*
         * Check the segment limit.
         */
        if (!pState->f64Bits && pDis->uInstrAddr + offInstr > pState->cbSegLimit)
            return VERR_OUT_OF_SELECTOR_BOUNDS;

        /*
         * Calc how much we can read.
         */
        uint32_t cb = GUEST_PAGE_SIZE - (GCPtr & GUEST_PAGE_OFFSET_MASK);
        if (!pState->f64Bits)
        {
            RTGCUINTPTR cbSeg = pState->GCPtrSegEnd - GCPtr;
            if (cb > cbSeg && cbSeg)
                cb = cbSeg;
        }
        if (cb > cbMaxRead)
            cb = cbMaxRead;

        /*
         * Read and advance or exit.
         */
        memcpy(&pDis->Instr.ab[offInstr], (uint8_t *)pState->pvPageR3 + (GCPtr & GUEST_PAGE_OFFSET_MASK), cb);
        offInstr  += (uint8_t)cb;
        if (cb >= cbMinRead)
        {
            pDis->cbCachedInstr = offInstr;
            return VINF_SUCCESS;
        }
        cbMinRead -= (uint8_t)cb;
        cbMaxRead -= (uint8_t)cb;
    }
}


/**
 * Disassemble an instruction and return the information in the provided structure.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pCtx        Pointer to the guest CPU context.
 * @param   GCPtrPC     Program counter (relative to CS) to disassemble from.
 * @param   pDis        Disassembly state.
 * @param   pszPrefix   String prefix for logging (debug only).
 *
 */
VMMR3DECL(int) CPUMR3DisasmInstrCPU(PVM pVM, PVMCPU pVCpu, PCPUMCTX pCtx, RTGCPTR GCPtrPC, PDISSTATE pDis,
                                    const char *pszPrefix)
{
    CPUMDISASSTATE  State;
    int             rc;

    const PGMMODE enmMode = PGMGetGuestMode(pVCpu);
    State.pDis            = pDis;
    State.pvPageGC        = 0;
    State.pvPageR3        = NULL;
    State.pVM             = pVM;
    State.pVCpu           = pVCpu;
    State.fLocked         = false;
    State.f64Bits         = false;

    /*
     * Get selector information.
     */
    DISCPUMODE enmDisCpuMode;
    if (    (pCtx->cr0 & X86_CR0_PE)
        &&   pCtx->eflags.Bits.u1VM == 0)
    {
        if (!CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pCtx->cs))
            return VERR_CPUM_HIDDEN_CS_LOAD_ERROR;
        State.f64Bits         = enmMode >= PGMMODE_AMD64 && pCtx->cs.Attr.n.u1Long;
        State.GCPtrSegBase    = pCtx->cs.u64Base;
        State.GCPtrSegEnd     = pCtx->cs.u32Limit + 1 + (RTGCUINTPTR)pCtx->cs.u64Base;
        State.cbSegLimit      = pCtx->cs.u32Limit;
        enmDisCpuMode         = (State.f64Bits)
                              ? DISCPUMODE_64BIT
                              : pCtx->cs.Attr.n.u1DefBig
                              ? DISCPUMODE_32BIT
                              : DISCPUMODE_16BIT;
    }
    else
    {
        /* real or V86 mode */
        enmDisCpuMode         = DISCPUMODE_16BIT;
        State.GCPtrSegBase    = pCtx->cs.Sel * 16;
        State.GCPtrSegEnd     = 0xFFFFFFFF;
        State.cbSegLimit      = 0xFFFFFFFF;
    }

    /*
     * Disassemble the instruction.
     */
    uint32_t cbInstr;
#ifndef LOG_ENABLED
    RT_NOREF_PV(pszPrefix);
    rc = DISInstrWithReader(GCPtrPC, enmDisCpuMode, cpumR3DisasInstrRead, &State, pDis, &cbInstr);
    if (RT_SUCCESS(rc))
    {
#else
    char szOutput[160];
    rc = DISInstrToStrWithReader(GCPtrPC, enmDisCpuMode, cpumR3DisasInstrRead, &State,
                                 pDis, &cbInstr, szOutput, sizeof(szOutput));
    if (RT_SUCCESS(rc))
    {
        /* log it */
        if (pszPrefix)
            Log(("%s-CPU%d: %s", pszPrefix, pVCpu->idCpu, szOutput));
        else
            Log(("%s", szOutput));
#endif
        rc = VINF_SUCCESS;
    }
    else
        Log(("CPUMR3DisasmInstrCPU: DISInstr failed for %04X:%RGv rc=%Rrc\n", pCtx->cs.Sel, GCPtrPC, rc));

    /* Release mapping lock acquired in cpumR3DisasInstrRead. */
    if (State.fLocked)
        PGMPhysReleasePageMappingLock(pVM, &State.PageMapLock);

    return rc;
}



/**
 * API for controlling a few of the CPU features found in CR4.
 *
 * Currently only X86_CR4_TSD is accepted as input.
 *
 * @returns VBox status code.
 *
 * @param   pVM     The cross context VM structure.
 * @param   fOr     The CR4 OR mask.
 * @param   fAnd    The CR4 AND mask.
 */
VMMR3DECL(int) CPUMR3SetCR4Feature(PVM pVM, RTHCUINTREG fOr, RTHCUINTREG fAnd)
{
    AssertMsgReturn(!(fOr & ~(X86_CR4_TSD)), ("%#x\n", fOr), VERR_INVALID_PARAMETER);
    AssertMsgReturn((fAnd & ~(X86_CR4_TSD)) == ~(X86_CR4_TSD), ("%#x\n", fAnd), VERR_INVALID_PARAMETER);

    pVM->cpum.s.CR4.OrMask &= fAnd;
    pVM->cpum.s.CR4.OrMask |= fOr;

    return VINF_SUCCESS;
}


/**
 * Called when the ring-3 init phase completes.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   enmWhat             Which init phase.
 */
VMMR3DECL(int) CPUMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    switch (enmWhat)
    {
        case VMINITCOMPLETED_RING3:
        {
            /*
             * Figure out if the guest uses 32-bit or 64-bit FPU state at runtime for 64-bit capable VMs.
             * Only applicable/used on 64-bit hosts, refer CPUMR0A.asm. See @bugref{7138}.
             */
            bool const fSupportsLongMode = VMR3IsLongModeAllowed(pVM);
            for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
            {
                PVMCPU pVCpu = pVM->apCpusR3[idCpu];

                /* While loading a saved-state we fix it up in, cpumR3LoadDone(). */
                if (fSupportsLongMode)
                    pVCpu->cpum.s.fUseFlags |= CPUM_USE_SUPPORTS_LONGMODE;
            }

            /* Register statistic counters for MSRs. */
            cpumR3MsrRegStats(pVM);

            /* There shouldn't be any more calls to CPUMR3SetGuestCpuIdFeature and
               CPUMR3ClearGuestCpuIdFeature now, so do some final CPUID polishing (NX). */
            cpumR3CpuIdRing3InitDone(pVM);

            /* Create VMX-preemption timer for nested guests if required.  Must be
               done here as CPUM is initialized before TM. */
            if (pVM->cpum.s.GuestFeatures.fVmx)
            {
                for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                {
                    PVMCPU pVCpu = pVM->apCpusR3[idCpu];
                    char szName[32];
                    RTStrPrintf(szName, sizeof(szName), "Nested VMX-preemption %u", idCpu);
                    int rc = TMR3TimerCreate(pVM, TMCLOCK_VIRTUAL_SYNC, cpumR3VmxPreemptTimerCallback, pVCpu,
                                             TMTIMER_FLAGS_RING0, szName, &pVCpu->cpum.s.hNestedVmxPreemptTimer);
                    AssertLogRelRCReturn(rc, rc);
                }
            }

            /*
             * Map guest RAM via MTRRs.
             */
            if (pVM->cpum.s.fMtrrRead)
            {
                int const rc = cpumR3MapMtrrs(pVM);
                if (RT_SUCCESS(rc))
                { /* likely */ }
                else
                    return rc;
            }
            break;
        }

        default:
            break;
    }
    return VINF_SUCCESS;
}


/**
 * Called when the ring-0 init phases completed.
 *
 * @param   pVM                 The cross context VM structure.
 */
VMMR3DECL(void) CPUMR3LogCpuIdAndMsrFeatures(PVM pVM)
{
    /*
     * Enable log buffering as we're going to log a lot of lines.
     */
    bool const fOldBuffered = RTLogRelSetBuffering(true /*fBuffered*/);

    /*
     * Log the cpuid.
     */
    RTCPUSET OnlineSet;
    LogRel(("CPUM: Logical host processors: %u present, %u max, %u online, online mask: %016RX64\n",
                (unsigned)RTMpGetPresentCount(), (unsigned)RTMpGetCount(), (unsigned)RTMpGetOnlineCount(),
                RTCpuSetToU64(RTMpGetOnlineSet(&OnlineSet)) ));
    RTCPUID cCores = RTMpGetCoreCount();
    if (cCores)
        LogRel(("CPUM: Physical host cores: %u\n", (unsigned)cCores));
    LogRel(("************************* CPUID dump ************************\n"));
    DBGFR3Info(pVM->pUVM, "cpuid", "verbose", DBGFR3InfoLogRelHlp());
    LogRel(("\n"));
    DBGFR3_INFO_LOG_SAFE(pVM, "cpuid", "verbose"); /* macro */
    LogRel(("******************** End of CPUID dump **********************\n"));

    /*
     * Log VT-x extended features.
     *
     * SVM features are currently all covered under CPUID so there is nothing
     * to do here for SVM.
     */
    if (pVM->cpum.s.HostFeatures.fVmx)
    {
        LogRel(("*********************** VT-x features ***********************\n"));
        DBGFR3Info(pVM->pUVM, "cpumvmxfeat", "default", DBGFR3InfoLogRelHlp());
        LogRel(("\n"));
        LogRel(("******************* End of VT-x features ********************\n"));
    }

    /*
     * Restore the log buffering state to what it was previously.
     */
    RTLogRelSetBuffering(fOldBuffered);
}


/**
 * Marks the guest debug state as active.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @note This is used solely by NEM (hence the name) to set the correct flags here
 *       without loading the host's DRx registers, which is not possible from ring-3 anyway.
 *       The specific NEM backends have to make sure to load the correct values.
 */
VMMR3_INT_DECL(void) CPUMR3NemActivateGuestDebugState(PVMCPUCC pVCpu)
{
    ASMAtomicAndU32(&pVCpu->cpum.s.fUseFlags, ~CPUM_USED_DEBUG_REGS_HYPER);
    ASMAtomicOrU32(&pVCpu->cpum.s.fUseFlags, CPUM_USED_DEBUG_REGS_GUEST);
}


/**
 * Marks the hyper debug state as active.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @note This is used solely by NEM (hence the name) to set the correct flags here
 *       without loading the host's DRx registers, which is not possible from ring-3 anyway.
 *       The specific NEM backends have to make sure to load the correct values.
 */
VMMR3_INT_DECL(void) CPUMR3NemActivateHyperDebugState(PVMCPUCC pVCpu)
{
    /*
     * Make sure the hypervisor values are up to date.
     */
    CPUMRecalcHyperDRx(pVCpu, UINT8_MAX /* no loading, please */);

    ASMAtomicAndU32(&pVCpu->cpum.s.fUseFlags, ~CPUM_USED_DEBUG_REGS_GUEST);
    ASMAtomicOrU32(&pVCpu->cpum.s.fUseFlags, CPUM_USED_DEBUG_REGS_HYPER);
}
