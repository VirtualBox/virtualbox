/* $Id: EMHM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EM - Execution Monitor / Manager - hardware virtualization
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
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/em.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmqueue.h>
#include <VBox/vmm/hm.h>
#include "EMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/cpumdis.h>
#include <VBox/dis.h>
#include <VBox/err.h>
#include <VBox/vmm/dbgf.h>
#include "VMMTracing.h"

#include <iprt/asm.h>

#include "EMInline.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int      emR3HmHandleRC(PVM pVM, PVMCPU pVCpu, int rc);
DECLINLINE(int) emR3HmExecuteInstruction(PVM pVM, PVMCPU pVCpu, const char *pszPrefix, int rcGC = VINF_SUCCESS);
static int      emR3HmExecuteIOInstruction(PVM pVM, PVMCPU pVCpu);
static int      emR3HmForcedActions(PVM pVM, PVMCPU pVCpu);

#define EMHANDLERC_WITH_HM
#define emR3ExecuteInstruction   emR3HmExecuteInstruction
#define emR3ExecuteIOInstruction emR3HmExecuteIOInstruction
#include "EMHandleRCTmpl.h"


/**
 * Executes instruction in HM mode if we can.
 *
 * This is somewhat comparable to REMR3EmulateInstruction.
 *
 * @returns VBox strict status code.
 * @retval  VINF_EM_DBG_STEPPED on success.
 * @retval  VERR_EM_CANNOT_EXEC_GUEST if we cannot execute guest instructions in
 *          HM right now.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure for the calling EMT.
 * @param   fFlags  Combinations of EM_ONE_INS_FLAGS_XXX.
 * @thread  EMT.
 */
VMMR3_INT_DECL(VBOXSTRICTRC) EMR3HmSingleInstruction(PVM pVM, PVMCPU pVCpu, uint32_t fFlags)
{
    Assert(!(fFlags & ~EM_ONE_INS_FLAGS_MASK));

    if (!HMCanExecuteGuest(pVM, pVCpu, &pVCpu->cpum.GstCtx))
        return VINF_EM_RESCHEDULE;

    uint64_t const uOldRip = pVCpu->cpum.GstCtx.rip;
    for (;;)
    {
        /*
         * Service necessary FFs before going into HM.
         */
        if (   VM_FF_IS_ANY_SET(pVM, VM_FF_HIGH_PRIORITY_PRE_RAW_MASK)
            || VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HIGH_PRIORITY_PRE_RAW_MASK))
        {
            VBOXSTRICTRC rcStrict = emR3HmForcedActions(pVM, pVCpu);
            if (rcStrict != VINF_SUCCESS)
            {
                Log(("EMR3HmSingleInstruction: FFs before -> %Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
                return rcStrict;
            }
        }

        /*
         * Go execute it.
         */
        bool fOld = HMSetSingleInstruction(pVM, pVCpu, true);
        VBOXSTRICTRC rcStrict = VMMR3HmRunGC(pVM, pVCpu);
        HMSetSingleInstruction(pVM, pVCpu, fOld);
        LogFlow(("EMR3HmSingleInstruction: %Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

        /*
         * Handle high priority FFs and informational status codes.  We don't do
         * normal FF processing the caller or the next call can deal with them.
         */
        VMCPU_FF_CLEAR_MASK(pVCpu, VMCPU_FF_RESUME_GUEST_MASK);
        if (   VM_FF_IS_ANY_SET(pVM, VM_FF_HIGH_PRIORITY_POST_MASK)
            || VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HIGH_PRIORITY_POST_MASK))
        {
            rcStrict = emR3HighPriorityPostForcedActions(pVM, pVCpu, rcStrict);
            LogFlow(("EMR3HmSingleInstruction: FFs after -> %Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
        }

        if (rcStrict != VINF_SUCCESS && (rcStrict < VINF_EM_FIRST || rcStrict > VINF_EM_LAST))
        {
            rcStrict = emR3HmHandleRC(pVM, pVCpu, VBOXSTRICTRC_TODO(rcStrict));
            Log(("EMR3HmSingleInstruction: emR3HmHandleRC -> %Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
        }

        /*
         * Done?
         */
        if (   (rcStrict != VINF_SUCCESS && rcStrict != VINF_EM_DBG_STEPPED)
            || !(fFlags & EM_ONE_INS_FLAGS_RIP_CHANGE)
            || pVCpu->cpum.GstCtx.rip != uOldRip)
        {
            if (rcStrict == VINF_SUCCESS && pVCpu->cpum.GstCtx.rip != uOldRip)
                rcStrict = VINF_EM_DBG_STEPPED;
            Log(("EMR3HmSingleInstruction: returns %Rrc (rip %llx -> %llx)\n", VBOXSTRICTRC_VAL(rcStrict), uOldRip, pVCpu->cpum.GstCtx.rip));
            CPUM_IMPORT_EXTRN_RET(pVCpu, ~CPUMCTX_EXTRN_KEEPER_MASK);
            return rcStrict;
        }
    }
}


/**
 * Executes one (or perhaps a few more) instruction(s).
 *
 * @returns VBox status code suitable for EM.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   rcRC        Return code from RC.
 * @param   pszPrefix   Disassembly prefix. If not NULL we'll disassemble the
 *                      instruction and prefix the log output with this text.
 */
#if defined(LOG_ENABLED) || defined(DOXYGEN_RUNNING)
static int emR3HmExecuteInstructionWorker(PVM pVM, PVMCPU pVCpu, int rcRC, const char *pszPrefix)
#else
static int emR3HmExecuteInstructionWorker(PVM pVM, PVMCPU pVCpu, int rcRC)
#endif
{
    RT_NOREF(rcRC, pVM);

#ifdef LOG_ENABLED
    /*
     * Log it.
     */
    Log(("EMINS: %04x:%RGv RSP=%RGv\n", pVCpu->cpum.GstCtx.cs.Sel, (RTGCPTR)pVCpu->cpum.GstCtx.rip, (RTGCPTR)pVCpu->cpum.GstCtx.rsp));
    if (pszPrefix)
    {
        DBGFR3_INFO_LOG(pVM, pVCpu, "cpumguest", pszPrefix);
        DBGFR3_DISAS_INSTR_CUR_LOG(pVCpu, pszPrefix);
    }
#endif

    /*
     * Use IEM and fallback on REM if the functionality is missing.
     * Once IEM gets mature enough, nothing should ever fall back.
     */
    STAM_PROFILE_START(&pVCpu->em.s.StatIEMEmu, a);
    VBOXSTRICTRC rcStrict;
    uint32_t     idxContinueExitRec = pVCpu->em.s.idxContinueExitRec;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    if (idxContinueExitRec >= RT_ELEMENTS(pVCpu->em.s.aExitRecords))
    {
        CPUM_IMPORT_EXTRN_RET(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = VBOXSTRICTRC_TODO(IEMExecOne(pVCpu));
    }
    else
    {
        RT_UNTRUSTED_VALIDATED_FENCE();
        rcStrict = EMHistoryExec(pVCpu, &pVCpu->em.s.aExitRecords[idxContinueExitRec], 0);
        LogFlow(("emR3HmExecuteInstruction: %Rrc (EMHistoryExec)\n", VBOXSTRICTRC_VAL(rcStrict)));
    }
    STAM_PROFILE_STOP(&pVCpu->em.s.StatIEMEmu, a);

    return VBOXSTRICTRC_TODO(rcStrict);
}


/**
 * Executes one (or perhaps a few more) instruction(s).
 * This is just a wrapper for discarding pszPrefix in non-logging builds.
 *
 * @returns VBox status code suitable for EM.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszPrefix   Disassembly prefix. If not NULL we'll disassemble the
 *                      instruction and prefix the log output with this text.
 * @param   rcGC        GC return code
 */
DECLINLINE(int) emR3HmExecuteInstruction(PVM pVM, PVMCPU pVCpu, const char *pszPrefix, int rcGC)
{
#ifdef LOG_ENABLED
    return emR3HmExecuteInstructionWorker(pVM, pVCpu, rcGC, pszPrefix);
#else
    RT_NOREF_PV(pszPrefix);
    return emR3HmExecuteInstructionWorker(pVM, pVCpu, rcGC);
#endif
}


/**
 * Executes one (or perhaps a few more) IO instruction(s).
 *
 * @returns VBox status code suitable for EM.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static int emR3HmExecuteIOInstruction(PVM pVM, PVMCPU pVCpu)
{
    RT_NOREF(pVM);
    STAM_PROFILE_START(&pVCpu->em.s.StatIOEmu, a);

    VBOXSTRICTRC rcStrict;
    uint32_t     idxContinueExitRec = pVCpu->em.s.idxContinueExitRec;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    if (idxContinueExitRec >= RT_ELEMENTS(pVCpu->em.s.aExitRecords))
    {
        /*
         * Hand it over to the interpreter.
         */
        CPUM_IMPORT_EXTRN_RET(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = IEMExecOne(pVCpu);
        LogFlow(("emR3HmExecuteIOInstruction: %Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    }
    else
    {
        RT_UNTRUSTED_VALIDATED_FENCE();
        CPUM_IMPORT_EXTRN_RET(pVCpu, IEM_CPUMCTX_EXTRN_MUST_MASK);
        rcStrict = EMHistoryExec(pVCpu, &pVCpu->em.s.aExitRecords[idxContinueExitRec], 0);
        LogFlow(("emR3HmExecuteIOInstruction: %Rrc (EMHistoryExec)\n", VBOXSTRICTRC_VAL(rcStrict)));
        STAM_COUNTER_INC(&pVCpu->em.s.StatIoRestarted);
    }

    STAM_COUNTER_INC(&pVCpu->em.s.StatIoIem);
    STAM_PROFILE_STOP(&pVCpu->em.s.StatIOEmu, a);
    return VBOXSTRICTRC_TODO(rcStrict);
}


/**
 * Process HM specific forced actions.
 *
 * This function is called when any FFs in the VM_FF_HIGH_PRIORITY_PRE_RAW_MASK
 * or/and VMCPU_FF_HIGH_PRIORITY_PRE_RAW_MASK are pending.
 *
 * @returns VBox status code. May return VINF_EM_NO_MEMORY but none of the other
 *          EM statuses.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static int emR3HmForcedActions(PVM pVM, PVMCPU pVCpu)
{
    /*
     * Sync page directory.
     */
    if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3 | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL))
    {
        CPUM_IMPORT_EXTRN_RET(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4);
        Assert(pVCpu->em.s.enmState != EMSTATE_WAIT_SIPI);
        int rc = PGMSyncCR3(pVCpu, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr3, pVCpu->cpum.GstCtx.cr4, VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3));
        if (RT_FAILURE(rc))
            return rc;

        /* Prefetch pages for EIP and ESP. */
        /** @todo This is rather expensive. Should investigate if it really helps at all. */
        /** @todo this should be skipped! */
        CPUM_IMPORT_EXTRN_RET(pVCpu, CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_SS);
        rc = PGMPrefetchPage(pVCpu, SELMToFlat(pVCpu, X86_SREG_CS, &pVCpu->cpum.GstCtx, pVCpu->cpum.GstCtx.rip));
        if (rc == VINF_SUCCESS)
            rc = PGMPrefetchPage(pVCpu, SELMToFlat(pVCpu, X86_SREG_SS, &pVCpu->cpum.GstCtx, pVCpu->cpum.GstCtx.rsp));
        if (rc != VINF_SUCCESS)
        {
            if (rc != VINF_PGM_SYNC_CR3)
            {
                AssertLogRelMsgReturn(RT_FAILURE(rc), ("%Rrc\n", rc), VERR_IPE_UNEXPECTED_INFO_STATUS);
                return rc;
            }
            rc = PGMSyncCR3(pVCpu, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr3, pVCpu->cpum.GstCtx.cr4, VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3));
            if (RT_FAILURE(rc))
                return rc;
        }
        /** @todo maybe prefetch the supervisor stack page as well */
    }

    /*
     * Allocate handy pages (just in case the above actions have consumed some pages).
     */
    if (VM_FF_IS_PENDING_EXCEPT(pVM, VM_FF_PGM_NEED_HANDY_PAGES, VM_FF_PGM_NO_MEMORY))
    {
        int rc = PGMR3PhysAllocateHandyPages(pVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Check whether we're out of memory now.
     *
     * This may stem from some of the above actions or operations that has been executed
     * since we ran FFs. The allocate handy pages must for instance always be followed by
     * this check.
     */
    if (VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
        return VINF_EM_NO_MEMORY;

    return VINF_SUCCESS;
}


/**
 * Executes hardware accelerated raw code. (Intel VT-x & AMD-V)
 *
 * This function contains the inner EM execution loop for HM (the outer loop
 * being in EMR3ExecuteVM()).
 *
 * @returns VBox status code. The most important ones are: VINF_EM_RESCHEDULE,
 *          VINF_EM_RESCHEDULE_REM, VINF_EM_SUSPEND, VINF_EM_RESET and VINF_EM_TERMINATE.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pfFFDone    Where to store an indicator telling whether or not
 *                      FFs were done before returning.
 */
int emR3HmExecute(PVM pVM, PVMCPU pVCpu, bool *pfFFDone)
{
    int      rc = VERR_IPE_UNINITIALIZED_STATUS;

    LogFlow(("emR3HmExecute%d: (cs:eip=%04x:%RGv)\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, (RTGCPTR)pVCpu->cpum.GstCtx.rip));
    *pfFFDone = false;

    STAM_REL_COUNTER_INC(&pVCpu->em.s.StatHMExecuteCalled);

    /*
     * Spin till we get a forced action which returns anything but VINF_SUCCESS.
     */
    for (;;)
    {
        STAM_PROFILE_ADV_START(&pVCpu->em.s.StatHMEntry, a);

        /* Check if a forced reschedule is pending. */
        if (HMR3IsRescheduleRequired(pVM, &pVCpu->cpum.GstCtx))
        {
            rc = VINF_EM_RESCHEDULE;
            break;
        }

        /*
         * Process high priority pre-execution raw-mode FFs.
         */
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_HIGH_PRIORITY_PRE_RAW_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HIGH_PRIORITY_PRE_RAW_MASK))
        {
            rc = emR3HmForcedActions(pVM, pVCpu);
            if (rc != VINF_SUCCESS)
                break;
        }

#ifdef LOG_ENABLED
        /*
         * Log important stuff before entering GC.
         */
        if (TRPMHasTrap(pVCpu))
            Log(("CPU%d: Pending hardware interrupt=0x%x cs:rip=%04X:%RGv\n", pVCpu->idCpu, TRPMGetTrapNo(pVCpu), pVCpu->cpum.GstCtx.cs.Sel, (RTGCPTR)pVCpu->cpum.GstCtx.rip));

        uint32_t cpl = CPUMGetGuestCPL(pVCpu);
        if (pVM->cCpus == 1)
        {
            if (pVCpu->cpum.GstCtx.eflags.Bits.u1VM)
                Log(("HWV86: %08X IF=%d\n", pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.eflags.Bits.u1IF));
            else if (CPUMIsGuestIn64BitCodeEx(&pVCpu->cpum.GstCtx))
                Log(("HWR%d: %04X:%RGv ESP=%RGv IF=%d IOPL=%d CR0=%x CR4=%x EFER=%x\n", cpl, pVCpu->cpum.GstCtx.cs.Sel, (RTGCPTR)pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rsp, pVCpu->cpum.GstCtx.eflags.Bits.u1IF, pVCpu->cpum.GstCtx.eflags.Bits.u2IOPL, (uint32_t)pVCpu->cpum.GstCtx.cr0, (uint32_t)pVCpu->cpum.GstCtx.cr4, (uint32_t)pVCpu->cpum.GstCtx.msrEFER));
            else
                Log(("HWR%d: %04X:%08X ESP=%08X IF=%d IOPL=%d CR0=%x CR4=%x EFER=%x\n", cpl, pVCpu->cpum.GstCtx.cs.Sel,          pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.esp, pVCpu->cpum.GstCtx.eflags.Bits.u1IF, pVCpu->cpum.GstCtx.eflags.Bits.u2IOPL, (uint32_t)pVCpu->cpum.GstCtx.cr0, (uint32_t)pVCpu->cpum.GstCtx.cr4, (uint32_t)pVCpu->cpum.GstCtx.msrEFER));
        }
        else
        {
            if (pVCpu->cpum.GstCtx.eflags.Bits.u1VM)
                Log(("HWV86-CPU%d: %08X IF=%d\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.eflags.Bits.u1IF));
            else if (CPUMIsGuestIn64BitCodeEx(&pVCpu->cpum.GstCtx))
                Log(("HWR%d-CPU%d: %04X:%RGv ESP=%RGv IF=%d IOPL=%d CR0=%x CR4=%x EFER=%x\n", cpl, pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, (RTGCPTR)pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rsp, pVCpu->cpum.GstCtx.eflags.Bits.u1IF, pVCpu->cpum.GstCtx.eflags.Bits.u2IOPL, (uint32_t)pVCpu->cpum.GstCtx.cr0, (uint32_t)pVCpu->cpum.GstCtx.cr4, (uint32_t)pVCpu->cpum.GstCtx.msrEFER));
            else
                Log(("HWR%d-CPU%d: %04X:%08X ESP=%08X IF=%d IOPL=%d CR0=%x CR4=%x EFER=%x\n", cpl, pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel,          pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.esp, pVCpu->cpum.GstCtx.eflags.Bits.u1IF, pVCpu->cpum.GstCtx.eflags.Bits.u2IOPL, (uint32_t)pVCpu->cpum.GstCtx.cr0, (uint32_t)pVCpu->cpum.GstCtx.cr4, (uint32_t)pVCpu->cpum.GstCtx.msrEFER));
        }
#endif /* LOG_ENABLED */

        /*
         * Execute the code.
         */
        STAM_PROFILE_ADV_STOP(&pVCpu->em.s.StatHMEntry, a);

        if (RT_LIKELY(emR3IsExecutionAllowed(pVM, pVCpu)))
        {
            STAM_REL_PROFILE_START(&pVCpu->em.s.StatHMExec, x);
            rc = VMMR3HmRunGC(pVM, pVCpu);
            STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatHMExec, x);
        }
        else
        {
            /* Give up this time slice; virtual time continues */
            STAM_REL_PROFILE_ADV_START(&pVCpu->em.s.StatCapped, u);
            RTThreadSleep(5);
            STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatCapped, u);
            rc = VINF_SUCCESS;
        }


        /*
         * Deal with high priority post execution FFs before doing anything else.
         */
        VMCPU_FF_CLEAR_MASK(pVCpu, VMCPU_FF_RESUME_GUEST_MASK);
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_HIGH_PRIORITY_POST_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HIGH_PRIORITY_POST_MASK))
            rc = VBOXSTRICTRC_TODO(emR3HighPriorityPostForcedActions(pVM, pVCpu, rc));

        /*
         * Process the returned status code.
         */
        if (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST)
            break;

        rc = emR3HmHandleRC(pVM, pVCpu, rc);
        if (rc != VINF_SUCCESS)
            break;

        /*
         * Check and execute forced actions.
         */
#ifdef VBOX_HIGH_RES_TIMERS_HACK
        TMTimerPollVoid(pVM, pVCpu);
#endif
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_ALL_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_ALL_MASK))
        {
            rc = emR3ForcedActions(pVM, pVCpu, rc);
            VBOXVMM_EM_FF_ALL_RET(pVCpu, rc);
            if (    rc != VINF_SUCCESS
                &&  rc != VINF_EM_RESCHEDULE_EXEC_ENGINE)
            {
                *pfFFDone = true;
                break;
            }
        }
    }

    /*
     * Return to outer loop.
     */
#if defined(LOG_ENABLED) && defined(DEBUG)
    RTLogFlush(NULL);
#endif
    return rc;
}

