/* $Id: EMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EM - Execution Monitor(/Manager) - All contexts
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_EM
#include <VBox/vmm/em.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/stam.h>
#include "EMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/dis.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>




/**
 * Get the current execution manager status.
 *
 * @returns Current status.
 * @param   pVCpu         The cross context virtual CPU structure.
 */
VMM_INT_DECL(EMSTATE) EMGetState(PVMCPU pVCpu)
{
    return pVCpu->em.s.enmState;
}


/**
 * Sets the current execution manager status. (use only when you know what you're doing!)
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   enmNewState The new state, EMSTATE_WAIT_SIPI or EMSTATE_HALTED.
 */
VMM_INT_DECL(void)    EMSetState(PVMCPU pVCpu, EMSTATE enmNewState)
{
    /* Only allowed combination: */
    Assert(pVCpu->em.s.enmState == EMSTATE_WAIT_SIPI && enmNewState == EMSTATE_HALTED);
    pVCpu->em.s.enmState = enmNewState;
}


/**
 * Enables / disable hypercall instructions.
 *
 * This interface is used by GIM to tell the execution monitors whether the
 * hypercall instruction (VMMCALL & VMCALL) are allowed or should \#UD.
 *
 * @param   pVCpu       The cross context virtual CPU structure this applies to.
 * @param   fEnabled    Whether hypercall instructions are enabled (true) or not.
 */
VMMDECL(void) EMSetHypercallInstructionsEnabled(PVMCPU pVCpu, bool fEnabled)
{
    pVCpu->em.s.fHypercallEnabled = fEnabled;
}


/**
 * Checks if hypercall instructions (VMMCALL & VMCALL) are enabled or not.
 *
 * @returns true if enabled, false if not.
 * @param   pVCpu   The cross context virtual CPU structure.
 *
 * @note    If this call becomes a performance factor, we can make the data
 *          field available thru a read-only view in VMCPU.  See VM::cpum.ro.
 */
VMMDECL(bool) EMAreHypercallInstructionsEnabled(PVMCPU pVCpu)
{
    return pVCpu->em.s.fHypercallEnabled;
}


#if !defined(VBOX_VMM_TARGET_ARMV8)
/**
 * Prepare an MWAIT - essentials of the MONITOR instruction.
 *
 * @returns VINF_SUCCESS
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   rax                 The content of RAX.
 * @param   rcx                 The content of RCX.
 * @param   rdx                 The content of RDX.
 * @param   GCPhys              The physical address corresponding to rax.
 */
VMM_INT_DECL(int) EMMonitorWaitPrepare(PVMCPU pVCpu, uint64_t rax, uint64_t rcx, uint64_t rdx, RTGCPHYS GCPhys)
{
    pVCpu->em.s.MWait.uMonitorRAX = rax;
    pVCpu->em.s.MWait.uMonitorRCX = rcx;
    pVCpu->em.s.MWait.uMonitorRDX = rdx;
    pVCpu->em.s.MWait.fWait |= EMMWAIT_FLAG_MONITOR_ACTIVE;
    /** @todo Make use of GCPhys. */
    NOREF(GCPhys);
    /** @todo Complete MONITOR implementation.  */
    return VINF_SUCCESS;
}


/**
 * Checks if the monitor hardware is armed / active.
 *
 * @returns true if armed, false otherwise.
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 */
VMM_INT_DECL(bool) EMMonitorIsArmed(PVMCPU pVCpu)
{
    return RT_BOOL(pVCpu->em.s.MWait.fWait & EMMWAIT_FLAG_MONITOR_ACTIVE);
}


/**
 * Checks if we're in a MWAIT.
 *
 * @retval  1 if regular,
 * @retval  > 1 if MWAIT with EMMWAIT_FLAG_BREAKIRQIF0
 * @retval  0 if not armed
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 */
VMM_INT_DECL(unsigned) EMMonitorWaitIsActive(PVMCPU pVCpu)
{
    uint32_t fWait = pVCpu->em.s.MWait.fWait;
    AssertCompile(EMMWAIT_FLAG_ACTIVE == 1);
    AssertCompile(EMMWAIT_FLAG_BREAKIRQIF0 == 2);
    AssertCompile((EMMWAIT_FLAG_ACTIVE << 1) == EMMWAIT_FLAG_BREAKIRQIF0);
    return fWait & (EMMWAIT_FLAG_ACTIVE | ((fWait & EMMWAIT_FLAG_ACTIVE) << 1));
}


/**
 * Performs an MWAIT.
 *
 * @returns VINF_SUCCESS
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   rax                 The content of RAX.
 * @param   rcx                 The content of RCX.
 */
VMM_INT_DECL(int) EMMonitorWaitPerform(PVMCPU pVCpu, uint64_t rax, uint64_t rcx)
{
    pVCpu->em.s.MWait.uMWaitRAX = rax;
    pVCpu->em.s.MWait.uMWaitRCX = rcx;
    pVCpu->em.s.MWait.fWait |= EMMWAIT_FLAG_ACTIVE;
    if (rcx)
        pVCpu->em.s.MWait.fWait |= EMMWAIT_FLAG_BREAKIRQIF0;
    else
        pVCpu->em.s.MWait.fWait &= ~EMMWAIT_FLAG_BREAKIRQIF0;
    /** @todo not completely correct?? */
    return VINF_EM_HALT;
}


/**
 * Clears any address-range monitoring that is active.
 *
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 */
VMM_INT_DECL(void) EMMonitorWaitClear(PVMCPU pVCpu)
{
    LogFlowFunc(("Clearing MWAIT\n"));
    pVCpu->em.s.MWait.fWait &= ~(EMMWAIT_FLAG_ACTIVE | EMMWAIT_FLAG_BREAKIRQIF0);
}


/**
 * Determine if we should continue execution in HM after encountering an mwait
 * instruction.
 *
 * Clears MWAIT flags if returning @c true.
 *
 * @returns true if we should continue, false if we should halt.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pCtx            Current CPU context.
 */
VMM_INT_DECL(bool) EMMonitorWaitShouldContinue(PVMCPU pVCpu, PCPUMCTX pCtx)
{
    if (CPUMGetGuestGif(pCtx))
    {
        if (   CPUMIsGuestPhysIntrEnabled(pVCpu)
            || (   CPUMIsGuestInNestedHwvirtMode(pCtx)
                && CPUMIsGuestVirtIntrEnabled(pVCpu))
            || (   (pVCpu->em.s.MWait.fWait & (EMMWAIT_FLAG_ACTIVE | EMMWAIT_FLAG_BREAKIRQIF0))
                ==                            (EMMWAIT_FLAG_ACTIVE | EMMWAIT_FLAG_BREAKIRQIF0)) )
        {
            if (VMCPU_FF_IS_ANY_SET(pVCpu, (  VMCPU_FF_UPDATE_APIC | VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC
                                            | VMCPU_FF_INTERRUPT_NESTED_GUEST)))
            {
                pVCpu->em.s.MWait.fWait &= ~(EMMWAIT_FLAG_ACTIVE | EMMWAIT_FLAG_BREAKIRQIF0);
                return true;
            }
        }
    }

    return false;
}


/**
 * Determine if we should continue execution in HM after encountering a hlt
 * instruction.
 *
 * @returns true if we should continue, false if we should halt.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pCtx            Current CPU context.
 */
VMM_INT_DECL(bool) EMShouldContinueAfterHalt(PVMCPU pVCpu, PCPUMCTX pCtx)
{
    if (CPUMGetGuestGif(pCtx))
    {
        if (CPUMIsGuestPhysIntrEnabled(pVCpu))
            return VMCPU_FF_IS_ANY_SET(pVCpu, (VMCPU_FF_UPDATE_APIC | VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC));

        if (   CPUMIsGuestInNestedHwvirtMode(pCtx)
            && CPUMIsGuestVirtIntrEnabled(pVCpu))
            return VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST);
    }
    return false;
}
#endif


/**
 * Unhalts and wakes up the given CPU.
 *
 * This is an API for assisting the KVM hypercall API in implementing KICK_CPU.
 * It sets VMCPU_FF_UNHALT for @a pVCpuDst and makes sure it is woken up.   If
 * the CPU isn't currently in a halt, the next HLT instruction it executes will
 * be affected.
 *
 * @returns GVMMR0SchedWakeUpEx result or VINF_SUCCESS depending on context.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpuDst        The cross context virtual CPU structure of the
 *                          CPU to unhalt and wake up.  This is usually not the
 *                          same as the caller.
 * @thread  EMT
 */
VMM_INT_DECL(int) EMUnhaltAndWakeUp(PVMCC pVM, PVMCPUCC pVCpuDst)
{
    /*
     * Flag the current(/next) HLT to unhalt immediately.
     */
    VMCPU_FF_SET(pVCpuDst, VMCPU_FF_UNHALT);

    /*
     * Wake up the EMT (technically should be abstracted by VMM/VMEmt, but
     * just do it here for now).
     */
#ifdef IN_RING0
    /* We might be here with preemption disabled or enabled (i.e. depending on
       thread-context hooks being used), so don't try obtaining the GVMMR0 used
       lock here. See @bugref{7270#c148}. */
    int rc = GVMMR0SchedWakeUpNoGVMNoLock(pVM, pVCpuDst->idCpu);
    AssertRC(rc);

#elif defined(IN_RING3)
    VMR3NotifyCpuFFU(pVCpuDst->pUVCpu, 0 /*fFlags*/);
    int rc = VINF_SUCCESS;
    RT_NOREF(pVM);

#else
    /* Nothing to do for raw-mode, shouldn't really be used by raw-mode guests anyway. */
    Assert(pVM->cCpus == 1); NOREF(pVM);
    int rc = VINF_SUCCESS;
#endif
    return rc;
}

#ifndef IN_RING3

/**
 * Makes an I/O port write pending for ring-3 processing.
 *
 * @returns VINF_EM_PENDING_R3_IOPORT_READ
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uPort           The I/O port.
 * @param   cbInstr         The instruction length (for RIP updating).
 * @param   cbValue         The write size.
 * @param   uValue          The value being written.
 * @sa      emR3ExecutePendingIoPortWrite
 *
 * @note    Must not be used when I/O port breakpoints are pending or when single stepping.
 */
VMMRZ_INT_DECL(VBOXSTRICTRC)
EMRZSetPendingIoPortWrite(PVMCPU pVCpu, RTIOPORT uPort, uint8_t cbInstr, uint8_t cbValue, uint32_t uValue)
{
    Assert(pVCpu->em.s.PendingIoPortAccess.cbValue == 0);
    pVCpu->em.s.PendingIoPortAccess.uPort     = uPort;
    pVCpu->em.s.PendingIoPortAccess.cbValue   = cbValue;
    pVCpu->em.s.PendingIoPortAccess.cbInstr   = cbInstr;
    pVCpu->em.s.PendingIoPortAccess.uValue    = uValue;
    return VINF_EM_PENDING_R3_IOPORT_WRITE;
}


/**
 * Makes an I/O port read pending for ring-3 processing.
 *
 * @returns VINF_EM_PENDING_R3_IOPORT_READ
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uPort           The I/O port.
 * @param   cbInstr         The instruction length (for RIP updating).
 * @param   cbValue         The read size.
 * @sa      emR3ExecutePendingIoPortRead
 *
 * @note    Must not be used when I/O port breakpoints are pending or when single stepping.
 */
VMMRZ_INT_DECL(VBOXSTRICTRC)
EMRZSetPendingIoPortRead(PVMCPU pVCpu, RTIOPORT uPort, uint8_t cbInstr, uint8_t cbValue)
{
    Assert(pVCpu->em.s.PendingIoPortAccess.cbValue == 0);
    pVCpu->em.s.PendingIoPortAccess.uPort     = uPort;
    pVCpu->em.s.PendingIoPortAccess.cbValue   = cbValue;
    pVCpu->em.s.PendingIoPortAccess.cbInstr   = cbInstr;
    pVCpu->em.s.PendingIoPortAccess.uValue    = UINT32_C(0x52454144); /* 'READ' */
    return VINF_EM_PENDING_R3_IOPORT_READ;
}

#endif /* IN_RING3 */


/**
 * Worker for EMHistoryExec that checks for ring-3 returns and flags
 * continuation of the EMHistoryExec run there.
 */
DECL_FORCE_INLINE(void) emHistoryExecSetContinueExitRecIdx(PVMCPU pVCpu, VBOXSTRICTRC rcStrict, PCEMEXITREC pExitRec)
{
    pVCpu->em.s.idxContinueExitRec = UINT16_MAX;
#ifdef IN_RING3
    RT_NOREF_PV(rcStrict); RT_NOREF_PV(pExitRec);
#else
    switch (VBOXSTRICTRC_VAL(rcStrict))
    {
        case VINF_SUCCESS:
        default:
            break;

        /*
         * Only status codes that EMHandleRCTmpl.h will resume EMHistoryExec with.
         */
        case VINF_IOM_R3_IOPORT_READ:           /* -> emR3ExecuteIOInstruction */
        case VINF_IOM_R3_IOPORT_WRITE:          /* -> emR3ExecuteIOInstruction */
        case VINF_IOM_R3_IOPORT_COMMIT_WRITE:   /* -> VMCPU_FF_IOM -> VINF_EM_RESUME_R3_HISTORY_EXEC -> emR3ExecuteIOInstruction */
        case VINF_IOM_R3_MMIO_READ:             /* -> emR3ExecuteInstruction */
        case VINF_IOM_R3_MMIO_WRITE:            /* -> emR3ExecuteInstruction */
        case VINF_IOM_R3_MMIO_READ_WRITE:       /* -> emR3ExecuteInstruction */
        case VINF_IOM_R3_MMIO_COMMIT_WRITE:     /* -> VMCPU_FF_IOM -> VINF_EM_RESUME_R3_HISTORY_EXEC -> emR3ExecuteIOInstruction */
        case VINF_CPUM_R3_MSR_READ:             /* -> emR3ExecuteInstruction */
        case VINF_CPUM_R3_MSR_WRITE:            /* -> emR3ExecuteInstruction */
        case VINF_GIM_R3_HYPERCALL:             /* -> emR3ExecuteInstruction */
            pVCpu->em.s.idxContinueExitRec = (uint16_t)(pExitRec - &pVCpu->em.s.aExitRecords[0]);
            break;
    }
#endif /* !IN_RING3 */
}


/**
 * Execute using history.
 *
 * This function will be called when EMHistoryAddExit() and friends returns a
 * non-NULL result.  This happens in response to probing or when probing has
 * uncovered adjacent exits which can more effectively be reached by using IEM
 * than restarting execution using the main execution engine and fielding an
 * regular exit.
 *
 * @returns VBox strict status code, see IEMExecForExits.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pExitRec        The exit record return by a previous history add
 *                          or update call.
 * @param   fWillExit       Flags indicating to IEM what will cause exits, TBD.
 */
VMM_INT_DECL(VBOXSTRICTRC) EMHistoryExec(PVMCPUCC pVCpu, PCEMEXITREC pExitRec, uint32_t fWillExit)
{
    Assert(pExitRec);
    VMCPU_ASSERT_EMT(pVCpu);
    IEMEXECFOREXITSTATS ExecStats;
    switch (pExitRec->enmAction)
    {
        /*
         * Executes multiple instruction stopping only when we've gone a given
         * number without perceived exits.
         */
        case EMEXITACTION_EXEC_WITH_MAX:
        {
            STAM_REL_PROFILE_START(&pVCpu->em.s.StatHistoryExec, a);
            LogFlow(("EMHistoryExec/EXEC_WITH_MAX: %RX64, max %u\n", pExitRec->uFlatPC, pExitRec->cMaxInstructionsWithoutExit));
            VBOXSTRICTRC rcStrict = IEMExecForExits(pVCpu, fWillExit,
                                                    pExitRec->cMaxInstructionsWithoutExit /* cMinInstructions*/,
                                                    pVCpu->em.s.cHistoryExecMaxInstructions,
                                                    pExitRec->cMaxInstructionsWithoutExit,
                                                    &ExecStats);
            LogFlow(("EMHistoryExec/EXEC_WITH_MAX: %Rrc cExits=%u cMaxExitDistance=%u cInstructions=%u\n",
                     VBOXSTRICTRC_VAL(rcStrict), ExecStats.cExits, ExecStats.cMaxExitDistance, ExecStats.cInstructions));
            emHistoryExecSetContinueExitRecIdx(pVCpu, rcStrict, pExitRec);

            /* Ignore instructions IEM doesn't know about. */
            if (   (   rcStrict != VERR_IEM_INSTR_NOT_IMPLEMENTED
                    && rcStrict != VERR_IEM_ASPECT_NOT_IMPLEMENTED)
                || ExecStats.cInstructions == 0)
            { /* likely */ }
            else
                rcStrict = VINF_SUCCESS;

            if (ExecStats.cExits > 1)
                STAM_REL_COUNTER_ADD(&pVCpu->em.s.StatHistoryExecSavedExits, ExecStats.cExits - 1);
            STAM_REL_COUNTER_ADD(&pVCpu->em.s.StatHistoryExecInstructions, ExecStats.cInstructions);
            STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatHistoryExec, a);
            return rcStrict;
        }

        /*
         * Probe a exit for close by exits.
         */
        case EMEXITACTION_EXEC_PROBE:
        {
            STAM_REL_PROFILE_START(&pVCpu->em.s.StatHistoryProbe, b);
            LogFlow(("EMHistoryExec/EXEC_PROBE: %RX64\n", pExitRec->uFlatPC));
            PEMEXITREC   pExitRecUnconst = (PEMEXITREC)pExitRec;
            VBOXSTRICTRC rcStrict = IEMExecForExits(pVCpu, fWillExit,
                                                    pVCpu->em.s.cHistoryProbeMinInstructions,
                                                    pVCpu->em.s.cHistoryExecMaxInstructions,
                                                    pVCpu->em.s.cHistoryProbeMaxInstructionsWithoutExit,
                                                    &ExecStats);
            LogFlow(("EMHistoryExec/EXEC_PROBE: %Rrc cExits=%u cMaxExitDistance=%u cInstructions=%u\n",
                     VBOXSTRICTRC_VAL(rcStrict), ExecStats.cExits, ExecStats.cMaxExitDistance, ExecStats.cInstructions));
            emHistoryExecSetContinueExitRecIdx(pVCpu, rcStrict, pExitRecUnconst);
            if (   ExecStats.cExits >= 2
                && RT_SUCCESS(rcStrict))
            {
                Assert(ExecStats.cMaxExitDistance > 0 && ExecStats.cMaxExitDistance <= 32);
                pExitRecUnconst->cMaxInstructionsWithoutExit = ExecStats.cMaxExitDistance;
                pExitRecUnconst->enmAction = EMEXITACTION_EXEC_WITH_MAX;
                LogFlow(("EMHistoryExec/EXEC_PROBE: -> EXEC_WITH_MAX %u\n", ExecStats.cMaxExitDistance));
                STAM_REL_COUNTER_INC(&pVCpu->em.s.StatHistoryProbedExecWithMax);
            }
#ifndef IN_RING3
            else if (   pVCpu->em.s.idxContinueExitRec != UINT16_MAX
                     && RT_SUCCESS(rcStrict))
            {
                STAM_REL_COUNTER_INC(&pVCpu->em.s.StatHistoryProbedToRing3);
                LogFlow(("EMHistoryExec/EXEC_PROBE: -> ring-3\n"));
            }
#endif
            else
            {
                pExitRecUnconst->enmAction = EMEXITACTION_NORMAL_PROBED;
                pVCpu->em.s.idxContinueExitRec = UINT16_MAX;
                LogFlow(("EMHistoryExec/EXEC_PROBE: -> PROBED\n"));
                STAM_REL_COUNTER_INC(&pVCpu->em.s.StatHistoryProbedNormal);
                if (   rcStrict == VERR_IEM_INSTR_NOT_IMPLEMENTED
                    || rcStrict == VERR_IEM_ASPECT_NOT_IMPLEMENTED)
                    rcStrict = VINF_SUCCESS;
            }
            STAM_REL_COUNTER_ADD(&pVCpu->em.s.StatHistoryProbeInstructions, ExecStats.cInstructions);
            STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatHistoryProbe, b);
            return rcStrict;
        }

        /* We shouldn't ever see these here! */
        case EMEXITACTION_FREE_RECORD:
        case EMEXITACTION_NORMAL:
        case EMEXITACTION_NORMAL_PROBED:
            break;

        /* No default case, want compiler warnings. */
    }
    AssertLogRelFailedReturn(VERR_EM_INTERNAL_ERROR);
}


/**
 * Worker for emHistoryAddOrUpdateRecord.
 */
DECL_FORCE_INLINE(PCEMEXITREC) emHistoryRecordInit(PEMEXITREC pExitRec, uint64_t uFlatPC, uint32_t uFlagsAndType, uint64_t uExitNo)
{
    pExitRec->uFlatPC                     = uFlatPC;
    pExitRec->uFlagsAndType               = uFlagsAndType;
    pExitRec->enmAction                   = EMEXITACTION_NORMAL;
    pExitRec->bUnused                     = 0;
    pExitRec->cMaxInstructionsWithoutExit = 64;
    pExitRec->uLastExitNo                 = uExitNo;
    pExitRec->cHits                       = 1;
    return NULL;
}


/**
 * Worker for emHistoryAddOrUpdateRecord.
 */
DECL_FORCE_INLINE(PCEMEXITREC) emHistoryRecordInitNew(PVMCPU pVCpu, PEMEXITENTRY pHistEntry, uintptr_t idxSlot,
                                                      PEMEXITREC pExitRec, uint64_t uFlatPC,
                                                      uint32_t uFlagsAndType, uint64_t uExitNo)
{
    pHistEntry->idxSlot = (uint32_t)idxSlot;
    pVCpu->em.s.cExitRecordUsed++;
    LogFlow(("emHistoryRecordInitNew: [%#x] = %#07x %016RX64; (%u of %u used)\n", idxSlot, uFlagsAndType, uFlatPC,
             pVCpu->em.s.cExitRecordUsed, RT_ELEMENTS(pVCpu->em.s.aExitRecords) ));
    return emHistoryRecordInit(pExitRec, uFlatPC, uFlagsAndType, uExitNo);
}


/**
 * Worker for emHistoryAddOrUpdateRecord.
 */
DECL_FORCE_INLINE(PCEMEXITREC) emHistoryRecordInitReplacement(PEMEXITENTRY pHistEntry, uintptr_t idxSlot,
                                                              PEMEXITREC pExitRec, uint64_t uFlatPC,
                                                              uint32_t uFlagsAndType, uint64_t uExitNo)
{
    pHistEntry->idxSlot = (uint32_t)idxSlot;
    LogFlow(("emHistoryRecordInitReplacement: [%#x] = %#07x %016RX64 replacing %#07x %016RX64 with %u hits, %u exits old\n",
             idxSlot, uFlagsAndType, uFlatPC, pExitRec->uFlagsAndType, pExitRec->uFlatPC, pExitRec->cHits,
             uExitNo - pExitRec->uLastExitNo));
    return emHistoryRecordInit(pExitRec, uFlatPC, uFlagsAndType, uExitNo);
}


/**
 * Adds or updates the EMEXITREC for this PC/type and decide on an action.
 *
 * @returns Pointer to an exit record if special action should be taken using
 *          EMHistoryExec().  Take normal exit action when NULL.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uFlagsAndType   Combined flags and type, EMEXIT_F_KIND_EM set and
 *                          both EMEXIT_F_CS_EIP and EMEXIT_F_UNFLATTENED_PC are clear.
 * @param   uFlatPC         The flattened program counter.
 * @param   pHistEntry      The exit history entry.
 * @param   uExitNo         The current exit number.
 */
static PCEMEXITREC emHistoryAddOrUpdateRecord(PVMCPU pVCpu, uint64_t uFlagsAndType, uint64_t uFlatPC,
                                              PEMEXITENTRY pHistEntry, uint64_t uExitNo)
{
# ifdef IN_RING0
    /* Disregard the hm flag. */
    uFlagsAndType &= ~EMEXIT_F_HM;
# endif

    /*
     * Work the hash table.
     */
    AssertCompile(RT_ELEMENTS(pVCpu->em.s.aExitRecords) == 1024);
# define EM_EXIT_RECORDS_IDX_MASK 0x3ff
    uintptr_t  idxSlot  = ((uintptr_t)uFlatPC >> 1) & EM_EXIT_RECORDS_IDX_MASK;
    PEMEXITREC pExitRec = &pVCpu->em.s.aExitRecords[idxSlot];
    if (pExitRec->uFlatPC == uFlatPC)
    {
        Assert(pExitRec->enmAction != EMEXITACTION_FREE_RECORD);
        pHistEntry->idxSlot = (uint32_t)idxSlot;
        if (pExitRec->uFlagsAndType == uFlagsAndType)
        {
            pExitRec->uLastExitNo = uExitNo;
            STAM_REL_COUNTER_INC(&pVCpu->em.s.aStatHistoryRecHits[0]);
        }
        else
        {
            STAM_REL_COUNTER_INC(&pVCpu->em.s.aStatHistoryRecTypeChanged[0]);
            return emHistoryRecordInit(pExitRec, uFlatPC, uFlagsAndType, uExitNo);
        }
    }
    else if (pExitRec->enmAction == EMEXITACTION_FREE_RECORD)
    {
        STAM_REL_COUNTER_INC(&pVCpu->em.s.aStatHistoryRecNew[0]);
        return emHistoryRecordInitNew(pVCpu, pHistEntry, idxSlot, pExitRec, uFlatPC, uFlagsAndType, uExitNo);
    }
    else
    {
        /*
         * Collision.  We calculate a new hash for stepping away from the first,
         * doing up to 8 steps away before replacing the least recently used record.
         */
        uintptr_t idxOldest     = idxSlot;
        uint64_t  uOldestExitNo = pExitRec->uLastExitNo;
        unsigned  iOldestStep   = 0;
        unsigned  iStep         = 1;
        uintptr_t const idxAdd  = (uintptr_t)(uFlatPC >> 11) & (EM_EXIT_RECORDS_IDX_MASK / 4);
        for (;;)
        {
            Assert(iStep < RT_ELEMENTS(pVCpu->em.s.aStatHistoryRecHits));
            AssertCompile(RT_ELEMENTS(pVCpu->em.s.aStatHistoryRecNew)         == RT_ELEMENTS(pVCpu->em.s.aStatHistoryRecHits));
            AssertCompile(RT_ELEMENTS(pVCpu->em.s.aStatHistoryRecReplaced)    == RT_ELEMENTS(pVCpu->em.s.aStatHistoryRecHits));
            AssertCompile(RT_ELEMENTS(pVCpu->em.s.aStatHistoryRecTypeChanged) == RT_ELEMENTS(pVCpu->em.s.aStatHistoryRecHits));

            /* Step to the next slot. */
            idxSlot += idxAdd;
            idxSlot &= EM_EXIT_RECORDS_IDX_MASK;
            pExitRec = &pVCpu->em.s.aExitRecords[idxSlot];

            /* Does it match? */
            if (pExitRec->uFlatPC == uFlatPC)
            {
                Assert(pExitRec->enmAction != EMEXITACTION_FREE_RECORD);
                pHistEntry->idxSlot = (uint32_t)idxSlot;
                if (pExitRec->uFlagsAndType == uFlagsAndType)
                {
                    pExitRec->uLastExitNo = uExitNo;
                    STAM_REL_COUNTER_INC(&pVCpu->em.s.aStatHistoryRecHits[iStep]);
                    break;
                }
                STAM_REL_COUNTER_INC(&pVCpu->em.s.aStatHistoryRecTypeChanged[iStep]);
                return emHistoryRecordInit(pExitRec, uFlatPC, uFlagsAndType, uExitNo);
            }

            /* Is it free? */
            if (pExitRec->enmAction == EMEXITACTION_FREE_RECORD)
            {
                STAM_REL_COUNTER_INC(&pVCpu->em.s.aStatHistoryRecNew[iStep]);
                return emHistoryRecordInitNew(pVCpu, pHistEntry, idxSlot, pExitRec, uFlatPC, uFlagsAndType, uExitNo);
            }

            /* Is it the least recently used one? */
            if (pExitRec->uLastExitNo < uOldestExitNo)
            {
                uOldestExitNo = pExitRec->uLastExitNo;
                idxOldest     = idxSlot;
                iOldestStep   = iStep;
            }

            /* Next iteration? */
            iStep++;
            Assert(iStep < RT_ELEMENTS(pVCpu->em.s.aStatHistoryRecReplaced));
            if (RT_LIKELY(iStep < 8 + 1))
            { /* likely */ }
            else
            {
                /* Replace the least recently used slot. */
                STAM_REL_COUNTER_INC(&pVCpu->em.s.aStatHistoryRecReplaced[iOldestStep]);
                pExitRec = &pVCpu->em.s.aExitRecords[idxOldest];
                return emHistoryRecordInitReplacement(pHistEntry, idxOldest, pExitRec, uFlatPC, uFlagsAndType, uExitNo);
            }
        }
    }

    /*
     * Found an existing record.
     */
    switch (pExitRec->enmAction)
    {
        case EMEXITACTION_NORMAL:
        {
            uint64_t const cHits = ++pExitRec->cHits;
            if (cHits < 256)
                return NULL;
            LogFlow(("emHistoryAddOrUpdateRecord: [%#x] %#07x %16RX64: -> EXEC_PROBE\n", idxSlot, uFlagsAndType, uFlatPC));
            pExitRec->enmAction = EMEXITACTION_EXEC_PROBE;
            return pExitRec;
        }

        case EMEXITACTION_NORMAL_PROBED:
            pExitRec->cHits += 1;
            return NULL;

        default:
            pExitRec->cHits += 1;
            return pExitRec;

        /* This will happen if the caller ignores or cannot serve the probe
           request (forced to ring-3, whatever).  We retry this 256 times. */
        case EMEXITACTION_EXEC_PROBE:
        {
            uint64_t const cHits = ++pExitRec->cHits;
            if (cHits < 512)
                return pExitRec;
            pExitRec->enmAction = EMEXITACTION_NORMAL_PROBED;
            LogFlow(("emHistoryAddOrUpdateRecord: [%#x] %#07x %16RX64: -> PROBED\n", idxSlot, uFlagsAndType, uFlatPC));
            return NULL;
        }
    }
}


/**
 * Adds an exit to the history for this CPU.
 *
 * @returns Pointer to an exit record if special action should be taken using
 *          EMHistoryExec().  Take normal exit action when NULL.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uFlagsAndType   Combined flags and type (see EMEXIT_MAKE_FT).
 * @param   uFlatPC         The flattened program counter (RIP).  UINT64_MAX if not available.
 * @param   uTimestamp      The TSC value for the exit, 0 if not available.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(PCEMEXITREC) EMHistoryAddExit(PVMCPUCC pVCpu, uint32_t uFlagsAndType, uint64_t uFlatPC, uint64_t uTimestamp)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Add the exit history entry.
     */
    AssertCompile(RT_ELEMENTS(pVCpu->em.s.aExitHistory) == 256);
    uint64_t uExitNo = pVCpu->em.s.iNextExit++;
    PEMEXITENTRY pHistEntry = &pVCpu->em.s.aExitHistory[(uintptr_t)uExitNo & 0xff];
    pHistEntry->uFlatPC       = uFlatPC;
    pHistEntry->uTimestamp    = uTimestamp;
    pHistEntry->uFlagsAndType = uFlagsAndType;
    pHistEntry->idxSlot       = UINT32_MAX;

    /*
     * If common exit type, we will insert/update the exit into the exit record hash table.
     */
    if (   (uFlagsAndType & (EMEXIT_F_KIND_MASK | EMEXIT_F_CS_EIP | EMEXIT_F_UNFLATTENED_PC)) == EMEXIT_F_KIND_EM
#ifdef IN_RING0
        && pVCpu->em.s.fExitOptimizationEnabledR0
        && ( !(uFlagsAndType & EMEXIT_F_HM) || pVCpu->em.s.fExitOptimizationEnabledR0PreemptDisabled)
#else
        && pVCpu->em.s.fExitOptimizationEnabled
#endif
        && uFlatPC != UINT64_MAX
       )
        return emHistoryAddOrUpdateRecord(pVCpu, uFlagsAndType, uFlatPC, pHistEntry, uExitNo);
    return NULL;
}


/**
 * Interface that VT-x uses to supply the PC of an exit when CS:RIP is being read.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uFlatPC         The flattened program counter (RIP).
 * @param   fFlattened      Set if RIP was subjected to CS.BASE, clear if not.
 */
VMM_INT_DECL(void) EMHistoryUpdatePC(PVMCPUCC pVCpu, uint64_t uFlatPC, bool fFlattened)
{
    VMCPU_ASSERT_EMT(pVCpu);

    AssertCompile(RT_ELEMENTS(pVCpu->em.s.aExitHistory) == 256);
    uint64_t     uExitNo    = pVCpu->em.s.iNextExit - 1;
    PEMEXITENTRY pHistEntry = &pVCpu->em.s.aExitHistory[(uintptr_t)uExitNo & 0xff];
    pHistEntry->uFlatPC = uFlatPC;
    if (fFlattened)
        pHistEntry->uFlagsAndType &= ~EMEXIT_F_UNFLATTENED_PC;
    else
        pHistEntry->uFlagsAndType |= EMEXIT_F_UNFLATTENED_PC;
}


/**
 * Interface for convering a engine specific exit to a generic one and get guidance.
 *
 * @returns Pointer to an exit record if special action should be taken using
 *          EMHistoryExec().  Take normal exit action when NULL.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uFlagsAndType   Combined flags and type (see EMEXIT_MAKE_FLAGS_AND_TYPE).
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(PCEMEXITREC) EMHistoryUpdateFlagsAndType(PVMCPUCC pVCpu, uint32_t uFlagsAndType)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Do the updating.
     */
    AssertCompile(RT_ELEMENTS(pVCpu->em.s.aExitHistory) == 256);
    uint64_t     uExitNo    = pVCpu->em.s.iNextExit - 1;
    PEMEXITENTRY pHistEntry = &pVCpu->em.s.aExitHistory[(uintptr_t)uExitNo & 0xff];
    pHistEntry->uFlagsAndType = uFlagsAndType | (pHistEntry->uFlagsAndType & (EMEXIT_F_CS_EIP | EMEXIT_F_UNFLATTENED_PC));

    /*
     * If common exit type, we will insert/update the exit into the exit record hash table.
     */
    if (   (uFlagsAndType & (EMEXIT_F_KIND_MASK | EMEXIT_F_CS_EIP | EMEXIT_F_UNFLATTENED_PC)) == EMEXIT_F_KIND_EM
#ifdef IN_RING0
        && pVCpu->em.s.fExitOptimizationEnabledR0
        && ( !(uFlagsAndType & EMEXIT_F_HM) || pVCpu->em.s.fExitOptimizationEnabledR0PreemptDisabled)
#else
        && pVCpu->em.s.fExitOptimizationEnabled
#endif
        && pHistEntry->uFlatPC != UINT64_MAX
       )
        return emHistoryAddOrUpdateRecord(pVCpu, uFlagsAndType, pHistEntry->uFlatPC, pHistEntry, uExitNo);
    return NULL;
}


/**
 * Interface for convering a engine specific exit to a generic one and get
 * guidance, supplying flattened PC too.
 *
 * @returns Pointer to an exit record if special action should be taken using
 *          EMHistoryExec().  Take normal exit action when NULL.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uFlagsAndType   Combined flags and type (see EMEXIT_MAKE_FLAGS_AND_TYPE).
 * @param   uFlatPC         The flattened program counter (RIP).
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(PCEMEXITREC) EMHistoryUpdateFlagsAndTypeAndPC(PVMCPUCC pVCpu, uint32_t uFlagsAndType, uint64_t uFlatPC)
{
    VMCPU_ASSERT_EMT(pVCpu);
    //Assert(uFlatPC != UINT64_MAX); - disable to make the pc wrapping tests in bs3-cpu-weird-1 work.

    /*
     * Do the updating.
     */
    AssertCompile(RT_ELEMENTS(pVCpu->em.s.aExitHistory) == 256);
    uint64_t     uExitNo    = pVCpu->em.s.iNextExit - 1;
    PEMEXITENTRY pHistEntry = &pVCpu->em.s.aExitHistory[(uintptr_t)uExitNo & 0xff];
    pHistEntry->uFlagsAndType = uFlagsAndType;
    pHistEntry->uFlatPC       = uFlatPC;

    /*
     * If common exit type, we will insert/update the exit into the exit record hash table.
     */
    if (   (uFlagsAndType & (EMEXIT_F_KIND_MASK | EMEXIT_F_CS_EIP | EMEXIT_F_UNFLATTENED_PC)) == EMEXIT_F_KIND_EM
#ifdef IN_RING0
        && pVCpu->em.s.fExitOptimizationEnabledR0
        && ( !(uFlagsAndType & EMEXIT_F_HM) || pVCpu->em.s.fExitOptimizationEnabledR0PreemptDisabled)
#else
        && pVCpu->em.s.fExitOptimizationEnabled
#endif
       )
        return emHistoryAddOrUpdateRecord(pVCpu, uFlagsAndType, uFlatPC, pHistEntry, uExitNo);
    return NULL;
}


/**
 * @callback_method_impl{FNDISREADBYTES}
 */
static DECLCALLBACK(int) emReadBytes(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMinRead, uint8_t cbMaxRead)
{
    PVMCPUCC    pVCpu    = (PVMCPUCC)pDis->pvUser;
    RTUINTPTR   uSrcAddr = pDis->uInstrAddr + offInstr;

    /*
     * Figure how much we can or must read.
     */
    size_t      cbToRead = GUEST_PAGE_SIZE - (uSrcAddr & (GUEST_PAGE_SIZE - 1));
    if (cbToRead > cbMaxRead)
        cbToRead = cbMaxRead;
    else if (cbToRead < cbMinRead)
        cbToRead = cbMinRead;

    int rc = PGMPhysSimpleReadGCPtr(pVCpu, &pDis->Instr.ab[offInstr], uSrcAddr, cbToRead);
    if (RT_FAILURE(rc))
    {
        if (cbToRead > cbMinRead)
        {
            cbToRead = cbMinRead;
            rc = PGMPhysSimpleReadGCPtr(pVCpu, &pDis->Instr.ab[offInstr], uSrcAddr, cbToRead);
        }
        if (RT_FAILURE(rc))
        {
#if defined(VBOX_VMM_TARGET_ARMV8)
            AssertReleaseFailed();
#else
            /*
             * If we fail to find the page via the guest's page tables
             * we invalidate the page in the host TLB (pertaining to
             * the guest in the NestedPaging case). See @bugref{6043}.
             */
            if (rc == VERR_PAGE_TABLE_NOT_PRESENT || rc == VERR_PAGE_NOT_PRESENT)
            {
                HMInvalidatePage(pVCpu, uSrcAddr);
                if (((uSrcAddr + cbToRead - 1) >> GUEST_PAGE_SHIFT) != (uSrcAddr >> GUEST_PAGE_SHIFT))
                    HMInvalidatePage(pVCpu, uSrcAddr + cbToRead - 1);
            }
#endif
        }
    }

    pDis->cbCachedInstr = offInstr + (uint8_t)cbToRead;
    return rc;
}


/**
 * Disassembles the current instruction.
 *
 * @returns VBox status code, see SELMToFlatEx and EMInterpretDisasOneEx for
 *          details.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pDis            Where to return the parsed instruction info.
 * @param   pcbInstr        Where to return the instruction size. (optional)
 */
VMM_INT_DECL(int) EMInterpretDisasCurrent(PVMCPUCC pVCpu, PDISSTATE pDis, unsigned *pcbInstr)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    return EMInterpretDisasOneEx(pVCpu, (RTGCUINTPTR)CPUMGetGuestFlatPC(pVCpu), pDis, pcbInstr);
#else
    PCPUMCTX pCtx = CPUMQueryGuestCtxPtr(pVCpu);
    RTGCPTR  GCPtrInstr;

# if 0
    int rc = SELMToFlatEx(pVCpu, DISSELREG_CS, pCtx, pCtx->rip, 0, &GCPtrInstr);
# else
/** @todo Get the CPU mode as well while we're at it! */
    int rc = SELMValidateAndConvertCSAddr(pVCpu, pCtx->eflags.u, pCtx->ss.Sel, pCtx->cs.Sel, &pCtx->cs, pCtx->rip, &GCPtrInstr);
# endif
    if (RT_SUCCESS(rc))
        return EMInterpretDisasOneEx(pVCpu, (RTGCUINTPTR)GCPtrInstr, pDis, pcbInstr);

    Log(("EMInterpretDisasOne: Failed to convert %RTsel:%RGv (cpl=%d) - rc=%Rrc !!\n",
         pCtx->cs.Sel, (RTGCPTR)pCtx->rip, pCtx->ss.Sel & X86_SEL_RPL, rc));
    return rc;
#endif
}


/**
 * Disassembles one instruction.
 *
 * This is used by internally by the interpreter and by trap/access handlers.
 *
 * @returns VBox status code.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPtrInstr      The flat address of the instruction.
 * @param   pDis            Where to return the parsed instruction info.
 * @param   pcbInstr        Where to return the instruction size. (optional)
 */
VMM_INT_DECL(int) EMInterpretDisasOneEx(PVMCPUCC pVCpu, RTGCUINTPTR GCPtrInstr, PDISSTATE pDis, unsigned *pcbInstr)
{
    DISCPUMODE enmCpuMode = CPUMGetGuestDisMode(pVCpu);
    /** @todo Deal with too long instruction (=> \#GP), opcode read errors (=>
     *        \#PF, \#GP, \#??), undefined opcodes (=> \#UD), and such. */
    int rc = DISInstrWithReader(GCPtrInstr, enmCpuMode, emReadBytes, pVCpu, pDis, pcbInstr);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;
    AssertMsg(rc == VERR_PAGE_NOT_PRESENT || rc == VERR_PAGE_TABLE_NOT_PRESENT, ("DISCoreOne failed to GCPtrInstr=%RGv rc=%Rrc\n", GCPtrInstr, rc));
    return rc;
}


/**
 * Interprets the current instruction.
 *
 * @returns VBox status code.
 * @retval  VINF_*                  Scheduling instructions.
 * @retval  VERR_EM_INTERPRETER     Something we can't cope with.
 * @retval  VERR_*                  Fatal errors.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @remark  Invalid opcode exceptions have a higher priority than \#GP (see
 *          Intel Architecture System Developers Manual, Vol 3, 5.5) so we don't
 *          need to worry about e.g. invalid modrm combinations (!)
 */
VMM_INT_DECL(VBOXSTRICTRC) EMInterpretInstruction(PVMCPUCC pVCpu)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    LogFlow(("EMInterpretInstruction %RGv\n", (RTGCPTR)CPUMGetGuestFlatPC(pVCpu)));
#else
    LogFlow(("EMInterpretInstruction %RGv\n", (RTGCPTR)CPUMGetGuestRIP(pVCpu)));
#endif

    VBOXSTRICTRC rc = IEMExecOneBypassEx(pVCpu, NULL /*pcbWritten*/);
    if (RT_UNLIKELY(   rc == VERR_IEM_ASPECT_NOT_IMPLEMENTED
                    || rc == VERR_IEM_INSTR_NOT_IMPLEMENTED))
        rc = VERR_EM_INTERPRETER;
    if (rc != VINF_SUCCESS)
        Log(("EMInterpretInstruction: returns %Rrc\n", VBOXSTRICTRC_VAL(rc)));

    return rc;
}


/**
 * Interprets the current instruction using the supplied DISSTATE structure.
 *
 * IP/EIP/RIP *IS* updated!
 *
 * @returns VBox strict status code.
 * @retval  VINF_*                  Scheduling instructions. When these are returned, it
 *                                  starts to get a bit tricky to know whether code was
 *                                  executed or not... We'll address this when it becomes a problem.
 * @retval  VERR_EM_INTERPRETER     Something we can't cope with.
 * @retval  VERR_*                  Fatal errors.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pDis        The disassembler cpu state for the instruction to be
 *                      interpreted.
 * @param   rip         The instruction pointer value.
 *
 * @remark  Invalid opcode exceptions have a higher priority than GP (see Intel
 *          Architecture System Developers Manual, Vol 3, 5.5) so we don't need
 *          to worry about e.g. invalid modrm combinations (!)
 *
 * @todo    At this time we do NOT check if the instruction overwrites vital information.
 *          Make sure this can't happen!! (will add some assertions/checks later)
 */
VMM_INT_DECL(VBOXSTRICTRC) EMInterpretInstructionDisasState(PVMCPUCC pVCpu, PDISSTATE pDis, uint64_t rip)
{
    LogFlow(("EMInterpretInstructionDisasState %RGv\n", (RTGCPTR)rip));

    VBOXSTRICTRC rc = IEMExecOneBypassWithPrefetchedByPC(pVCpu, rip, pDis->Instr.ab, pDis->cbCachedInstr);
    if (RT_UNLIKELY(   rc == VERR_IEM_ASPECT_NOT_IMPLEMENTED
                    || rc == VERR_IEM_INSTR_NOT_IMPLEMENTED))
        rc = VERR_EM_INTERPRETER;

    if (rc != VINF_SUCCESS)
        Log(("EMInterpretInstructionDisasState: returns %Rrc\n", VBOXSTRICTRC_VAL(rc)));

    return rc;
}

