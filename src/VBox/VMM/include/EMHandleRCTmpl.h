/* $Id: EMHandleRCTmpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EM - emR3[Raw|Hm|Nem]HandleRC template.
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

#ifndef VMM_INCLUDED_SRC_include_EMHandleRCTmpl_h
#define VMM_INCLUDED_SRC_include_EMHandleRCTmpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if defined(EMHANDLERC_WITH_PATM) + defined(EMHANDLERC_WITH_HM) + defined(EMHANDLERC_WITH_NEM) != 1
# error "Exactly one of these must be defined: EMHANDLERC_WITH_PATM, EMHANDLERC_WITH_HM, EMHANDLERC_WITH_NEM"
#endif


/**
 * Process a subset of the raw-mode, HM and NEM return codes.
 *
 * Since we have to share this with raw-mode single stepping, this inline
 * function has been created to avoid code duplication.
 *
 * @returns VINF_SUCCESS if it's ok to continue raw mode.
 * @returns VBox status code to return to the EM main loop.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   rc      The return code.
 */
#if defined(EMHANDLERC_WITH_HM) || defined(DOXYGEN_RUNNING)
int emR3HmHandleRC(PVM pVM, PVMCPU pVCpu, int rc)
#elif defined(EMHANDLERC_WITH_NEM)
int emR3NemHandleRC(PVM pVM, PVMCPU pVCpu, int rc)
#endif
{
    switch (rc)
    {
        /*
         * Common & simple ones.
         */
        case VINF_SUCCESS:
            break;
        case VINF_EM_RESCHEDULE_EXEC_ENGINE:
        case VINF_EM_RAW_INTERRUPT:
        case VINF_EM_RAW_TO_R3:
        case VINF_EM_RAW_TIMER_PENDING:
        case VINF_EM_PENDING_REQUEST:
            rc = VINF_SUCCESS;
            break;

#ifndef EMHANDLERC_WITH_NEM
        /*
         * Conflict or out of page tables.
         *
         * VM_FF_PGM_SYNC_CR3 is set by the hypervisor and all we need to
         * do here is to execute the pending forced actions.
         */
        case VINF_PGM_SYNC_CR3:
            AssertMsg(VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3 | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL),
                      ("VINF_PGM_SYNC_CR3 and no VMCPU_FF_PGM_SYNC_CR3*!\n"));
            rc = VINF_SUCCESS;
            break;

        /*
         * PGM pool flush pending (guest SMP only).
         */
        /** @todo jumping back and forth between ring 0 and 3 can burn a lot of cycles
         * if the EMT thread that's supposed to handle the flush is currently not active
         * (e.g. waiting to be scheduled) -> fix this properly!
         *
         * bird: Since the clearing is global and done via a rendezvous any CPU can do
         *       it. They would have to choose who to call VMMR3EmtRendezvous and send
         *       the rest to VMMR3EmtRendezvousFF ... Hmm ... that's not going to work
         *       all that well since the latter will race the setup done by the
         *       first.  Guess that means we need some new magic in that area for
         *       handling this case. :/
         */
        case VINF_PGM_POOL_FLUSH_PENDING:
            rc = VINF_SUCCESS;
            break;
#endif /* !EMHANDLERC_WITH_NEM */

        /*
         * I/O Port access - emulate the instruction.
         */
        case VINF_IOM_R3_IOPORT_READ:
        case VINF_IOM_R3_IOPORT_WRITE:
        case VINF_EM_RESUME_R3_HISTORY_EXEC: /* Resume EMHistoryExec after VMCPU_FF_IOM. */
            rc = emR3ExecuteIOInstruction(pVM, pVCpu);
            break;

#if !defined(VBOX_VMM_TARGET_ARMV8)
        /*
         * Execute pending I/O Port access.
         */
        case VINF_EM_PENDING_R3_IOPORT_WRITE:
            rc = VBOXSTRICTRC_TODO(emR3ExecutePendingIoPortWrite(pVM, pVCpu));
            break;
        case VINF_EM_PENDING_R3_IOPORT_READ:
            rc = VBOXSTRICTRC_TODO(emR3ExecutePendingIoPortRead(pVM, pVCpu));
            break;
#endif

        /*
         * Memory mapped I/O access - emulate the instruction.
         */
        case VINF_IOM_R3_MMIO_READ:
        case VINF_IOM_R3_MMIO_WRITE:
        case VINF_IOM_R3_MMIO_READ_WRITE:
            rc = emR3ExecuteInstruction(pVM, pVCpu, "MMIO");
            break;

        /*
         * Machine specific register access - emulate the instruction.
         */
        case VINF_CPUM_R3_MSR_READ:
        case VINF_CPUM_R3_MSR_WRITE:
            rc = emR3ExecuteInstruction(pVM, pVCpu, "MSR");
            break;

        /*
         * GIM hypercall.
         */
        case VINF_GIM_R3_HYPERCALL:
            rc = emR3ExecuteInstruction(pVM, pVCpu, "Hypercall");
            break;

#ifdef EMHANDLERC_WITH_HM
        case VINF_EM_HM_PATCH_TPR_INSTR:
            rc = HMR3PatchTprInstr(pVM, pVCpu);
            break;
#endif

        case VINF_EM_RAW_GUEST_TRAP:
        case VINF_EM_RAW_EMULATE_INSTR:
            AssertMsg(!TRPMHasTrap(pVCpu), ("trap=%#x\n", TRPMGetTrapNo(pVCpu))); /* We're directly executing instructions below without respecting any pending traps! */
            rc = emR3ExecuteInstruction(pVM, pVCpu, "EMUL: ");
            break;

        case VINF_EM_RAW_INJECT_TRPM_EVENT:
            CPUM_IMPORT_EXTRN_RET(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);
            rc = VBOXSTRICTRC_VAL(IEMInjectTrpmEvent(pVCpu));
            /* The following condition should be removed when IEM_IMPLEMENTS_TASKSWITCH becomes true. */
            if (rc == VERR_IEM_ASPECT_NOT_IMPLEMENTED)
                rc = emR3ExecuteInstruction(pVM, pVCpu, "EVENT: ");
            break;

#if !defined(VBOX_VMM_TARGET_ARMV8)
        case VINF_EM_EMULATE_SPLIT_LOCK:
            rc = VBOXSTRICTRC_TODO(emR3ExecuteSplitLockInstruction(pVM, pVCpu));
            break;
#endif


        /*
         * Up a level.
         */
        case VINF_EM_TERMINATE:
        case VINF_EM_OFF:
        case VINF_EM_RESET:
        case VINF_EM_SUSPEND:
        case VINF_EM_HALT:
        case VINF_EM_RESUME:
        case VINF_EM_NO_MEMORY:
        case VINF_EM_RESCHEDULE:
        case VINF_EM_RESCHEDULE_REM:
        case VINF_EM_WAIT_SIPI:
            break;

        /*
         * Up a level and invoke the debugger.
         */
        case VINF_EM_DBG_STEPPED:
        case VINF_EM_DBG_BREAKPOINT:
        case VINF_EM_DBG_STEP:
        case VINF_EM_DBG_HYPER_BREAKPOINT:
        case VINF_EM_DBG_HYPER_STEPPED:
        case VINF_EM_DBG_HYPER_ASSERTION:
        case VINF_EM_DBG_STOP:
        case VINF_EM_DBG_EVENT:
            break;

        /*
         * Up a level, dump and debug.
         */
        case VERR_TRPM_DONT_PANIC:
        case VERR_TRPM_PANIC:
        case VERR_VMM_RING0_ASSERTION:
        case VINF_EM_TRIPLE_FAULT:
        case VERR_VMM_HYPER_CR3_MISMATCH:
        case VERR_VMM_RING3_CALL_DISABLED:
        case VERR_IEM_INSTR_NOT_IMPLEMENTED:
        case VERR_IEM_ASPECT_NOT_IMPLEMENTED:
        case VERR_EM_GUEST_CPU_HANG:
            break;

#ifdef EMHANDLERC_WITH_HM
        /*
         * Up a level, after Hm have done some release logging.
         */
        case VERR_VMX_INVALID_VMCS_FIELD:
        case VERR_VMX_INVALID_VMCS_PTR:
        case VERR_VMX_INVALID_VMXON_PTR:
        case VERR_VMX_UNEXPECTED_INTERRUPTION_EXIT_TYPE:
        case VERR_VMX_UNEXPECTED_EXCEPTION:
        case VERR_VMX_UNEXPECTED_EXIT:
        case VERR_VMX_INVALID_GUEST_STATE:
        case VERR_VMX_UNABLE_TO_START_VM:
        case VERR_SVM_UNKNOWN_EXIT:
        case VERR_SVM_UNEXPECTED_EXIT:
        case VERR_SVM_UNEXPECTED_PATCH_TYPE:
        case VERR_SVM_UNEXPECTED_XCPT_EXIT:
            HMR3CheckError(pVM, rc);
            break;

        /* Up a level; fatal */
        case VERR_VMX_IN_VMX_ROOT_MODE:
        case VERR_SVM_IN_USE:
        case VERR_SVM_UNABLE_TO_START_VM:
            break;
#endif

#ifdef EMHANDLERC_WITH_NEM
        /* Fatal stuff, up a level. */
        case VERR_NEM_IPE_0:
        case VERR_NEM_IPE_1:
        case VERR_NEM_IPE_2:
        case VERR_NEM_IPE_3:
        case VERR_NEM_IPE_4:
        case VERR_NEM_IPE_5:
        case VERR_NEM_IPE_6:
        case VERR_NEM_IPE_7:
        case VERR_NEM_IPE_8:
        case VERR_NEM_IPE_9:
            break;
#endif

        /*
         * These two should be handled via the force flag already, but just in
         * case they end up here deal with it.
         */
        case VINF_IOM_R3_IOPORT_COMMIT_WRITE:
        case VINF_IOM_R3_MMIO_COMMIT_WRITE:
            AssertFailed();
            rc = VBOXSTRICTRC_TODO(IOMR3ProcessForceFlag(pVM, pVCpu, rc));
            break;

        /*
         * Anything which is not known to us means an internal error
         * and the termination of the VM!
         */
        default:
            AssertMsgFailed(("Unknown GC return code: %Rra\n", rc));
            break;
    }
    return rc;
}

#endif /* !VMM_INCLUDED_SRC_include_EMHandleRCTmpl_h */

