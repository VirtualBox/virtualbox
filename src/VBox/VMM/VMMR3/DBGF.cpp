/* $Id: DBGF.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility.
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


/** @page   pg_dbgf     DBGF - The Debugger Facility
 *
 * The purpose of the DBGF is to provide an interface for debuggers to
 * manipulate the VMM without having to mess up the source code for each of
 * them. The DBGF is always built in and will always work when a debugger
 * attaches to the VM. The DBGF provides the basic debugger features, such as
 * halting execution, handling breakpoints, single step execution, instruction
 * disassembly, info querying, OS specific diggers, symbol and module
 * management.
 *
 * The interface is working in a manner similar to the win32, linux and os2
 * debugger interfaces. The interface has an asynchronous nature. This comes
 * from the fact that the VMM and the Debugger are running in different threads.
 * They are referred to as the "emulation thread" and the "debugger thread", or
 * as the "ping thread" and the "pong thread, respectivly. (The last set of
 * names comes from the use of the Ping-Pong synchronization construct from the
 * RTSem API.)
 *
 * @see grp_dbgf
 *
 *
 * @section sec_dbgf_scenario   Usage Scenario
 *
 * The debugger starts by attaching to the VM. For practical reasons we limit the
 * number of concurrently attached debuggers to 1 per VM. The action of
 * attaching to the VM causes the VM to check and generate debug events.
 *
 * The debugger then will wait/poll for debug events and issue commands.
 *
 * The waiting and polling is done by the DBGFEventWait() function. It will wait
 * for the emulation thread to send a ping, thus indicating that there is an
 * event waiting to be processed.
 *
 * An event can be a response to a command issued previously, the hitting of a
 * breakpoint, or running into a bad/fatal VMM condition. The debugger now has
 * the ping and must respond to the event at hand - the VMM is waiting. This
 * usually means that the user of the debugger must do something, but it doesn't
 * have to. The debugger is free to call any DBGF function (nearly at least)
 * while processing the event.
 *
 * Typically the user will issue a request for the execution to be resumed, so
 * the debugger calls DBGFResume() and goes back to waiting/polling for events.
 *
 * When the user eventually terminates the debugging session or selects another
 * VM, the debugger detaches from the VM. This means that breakpoints are
 * disabled and that the emulation thread no longer polls for debugger commands.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/nem.h>
#include "DBGFInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/asm.h>
#include <iprt/time.h>
#include <iprt/assert.h>
#include <iprt/stream.h>
#include <iprt/env.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Instruction type returned by dbgfStepGetCurInstrType.
 */
typedef enum DBGFSTEPINSTRTYPE
{
    DBGFSTEPINSTRTYPE_INVALID = 0,
    DBGFSTEPINSTRTYPE_OTHER,
    DBGFSTEPINSTRTYPE_RET,
    DBGFSTEPINSTRTYPE_CALL,
    DBGFSTEPINSTRTYPE_END,
    DBGFSTEPINSTRTYPE_32BIT_HACK = 0x7fffffff
} DBGFSTEPINSTRTYPE;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(int) dbgfR3SendEventWait(PVM pVM, PVMCPU pVCpu, DBGFEVENTTYPE enmType, DBGFEVENTCTX enmCtx);
DECLINLINE(DBGFCMD) dbgfR3CpuGetCmd(PUVMCPU pUVCpu);
static int dbgfR3CpuWait(PVMCPU pVCpu);
static int dbgfR3CpuCmd(PVMCPU pVCpu, DBGFCMD enmCmd, PDBGFCMDDATA pCmdData, bool *pfResumeExecution);
static DBGFSTEPINSTRTYPE dbgfStepGetCurInstrType(PVM pVM, PVMCPU pVCpu);
static bool dbgfStepAreWeThereYet(PVM pVM, PVMCPU pVCpu);
static int dbgfR3EventHaltAllVCpus(PVM pVM, PVMCPU pVCpuExclude);



/**
 * Initializes the DBGF.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(int) DBGFR3Init(PVM pVM)
{
    PUVM pUVM = pVM->pUVM;
    AssertCompile(sizeof(pUVM->dbgf.s)          <= sizeof(pUVM->dbgf.padding));
    AssertCompile(sizeof(pUVM->aCpus[0].dbgf.s) <= sizeof(pUVM->aCpus[0].dbgf.padding));

    pVM->dbgf.s.SteppingFilter.idCpu = NIL_VMCPUID;

    /*
     * The usual sideways mountain climbing style of init:
     */
    int rc = dbgfR3InfoInit(pUVM); /* (First, initalizes the shared critical section.) */
    if (RT_SUCCESS(rc))
    {
        rc = dbgfR3TraceInit(pVM);
        if (RT_SUCCESS(rc))
        {
            rc = dbgfR3RegInit(pUVM);
            if (RT_SUCCESS(rc))
            {
                rc = dbgfR3AsInit(pUVM);
                if (RT_SUCCESS(rc))
                {
                    rc = dbgfR3BpInit(pUVM);
                    if (RT_SUCCESS(rc))
                    {
                        rc = dbgfR3OSInit(pUVM);
                        if (RT_SUCCESS(rc))
                        {
                            rc = dbgfR3PlugInInit(pUVM);
                            if (RT_SUCCESS(rc))
                            {
                                rc = dbgfR3BugCheckInit(pVM);
                                if (RT_SUCCESS(rc))
                                {
#ifdef VBOX_WITH_DBGF_TRACING
                                    rc = dbgfR3TracerInit(pVM);
#endif
                                    if (RT_SUCCESS(rc))
                                    {
                                        return VINF_SUCCESS;
                                    }
                                }
                                dbgfR3PlugInTerm(pUVM);
                            }
                            dbgfR3OSTermPart1(pUVM);
                            dbgfR3OSTermPart2(pUVM);
                        }
                        dbgfR3BpTerm(pUVM);
                    }
                    dbgfR3AsTerm(pUVM);
                }
                dbgfR3RegTerm(pUVM);
            }
            dbgfR3TraceTerm(pVM);
        }
        dbgfR3InfoTerm(pUVM);
    }
    return rc;
}


/**
 * Terminates and cleans up resources allocated by the DBGF.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(int) DBGFR3Term(PVM pVM)
{
    PUVM pUVM = pVM->pUVM;

#ifdef VBOX_WITH_DBGF_TRACING
    dbgfR3TracerTerm(pVM);
#endif
    dbgfR3OSTermPart1(pUVM);
    dbgfR3PlugInTerm(pUVM);
    dbgfR3OSTermPart2(pUVM);
    dbgfR3BpTerm(pUVM);
    dbgfR3AsTerm(pUVM);
    dbgfR3RegTerm(pUVM);
    dbgfR3TraceTerm(pVM);
    dbgfR3InfoTerm(pUVM);

    return VINF_SUCCESS;
}


/**
 * This is for tstCFGM and others to avoid trigger leak detection.
 *
 * @param   pUVM    The user mode VM structure.
 */
VMMR3DECL(void) DBGFR3TermUVM(PUVM pUVM)
{
    dbgfR3InfoTerm(pUVM);
}


/**
 * Called when the VM is powered off to detach debuggers.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) DBGFR3PowerOff(PVM pVM)
{
    /*
     * Send a termination event to any attached debugger.
     */
    if (pVM->dbgf.s.fAttached)
    {
        PVMCPU pVCpu = VMMGetCpu(pVM);
        int rc = dbgfR3SendEventWait(pVM, pVCpu, DBGFEVENT_POWERING_OFF, DBGFEVENTCTX_OTHER);
        AssertLogRelRC(rc);

        /*
         * Clear the FF so we won't get confused later on.
         */
        VM_FF_CLEAR(pVM, VM_FF_DBGF);
    }
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * @param   pVM         The cross context VM structure.
 * @param   offDelta    Relocation delta relative to old location.
 */
VMMR3_INT_DECL(void) DBGFR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
    dbgfR3TraceRelocate(pVM);
    dbgfR3AsRelocate(pVM->pUVM, offDelta);
}


/**
 * Waits a little while for a debuggger to attach.
 *
 * @returns True is a debugger have attached.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context per CPU structure.
 * @param   enmEvent    Event.
 *
 * @thread  EMT(pVCpu)
 */
static bool dbgfR3WaitForAttach(PVM pVM, PVMCPU pVCpu, DBGFEVENTTYPE enmEvent)
{
    /*
     * First a message.
     */
#if !defined(DEBUG)
    int cWait = 10;
#else
    int cWait = RTEnvExist("VBOX_DBGF_NO_WAIT_FOR_ATTACH")
             || (   (   enmEvent == DBGFEVENT_ASSERTION_HYPER
                     || enmEvent == DBGFEVENT_FATAL_ERROR)
                 && !RTEnvExist("VBOX_DBGF_WAIT_FOR_ATTACH"))
              ? 10
              : 150;
#endif
    RTStrmPrintf(g_pStdErr,
                 "DBGF: No debugger attached, waiting %d second%s for one to attach (event=%d)\n"
#ifdef DEBUG
                 "      Set VBOX_DBGF_NO_WAIT_FOR_ATTACH=1 for short wait or VBOX_DBGF_WAIT_FOR_ATTACH=1 longer.\n"
#endif
                 ,
                 cWait / 10, cWait != 10 ? "s" : "", enmEvent);
    RTStrmFlush(g_pStdErr);
    while (cWait > 0)
    {
        RTThreadSleep(100);
        if (pVM->dbgf.s.fAttached)
        {
            RTStrmPrintf(g_pStdErr, "Attached!\n");
            RTStrmFlush(g_pStdErr);
            return true;
        }

        /* Process rendezvous (debugger attaching involves such). */
        if (VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS))
        {
            int rc = VMMR3EmtRendezvousFF(pVM, pVCpu); AssertRC(rc);
            if (rc != VINF_SUCCESS)
            {
                /** @todo Ignoring these could be bad. */
                RTStrmPrintf(g_pStdErr, "[rcRendezvous=%Rrc, ignored!]", rc);
                RTStrmFlush(g_pStdErr);
            }
        }

        /* Process priority stuff. */
        if (   VM_FF_IS_SET(pVM, VM_FF_REQUEST)
            || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_REQUEST))
        {
            int rc = VMR3ReqProcessU(pVM->pUVM, VMCPUID_ANY, true /*fPriorityOnly*/);
            if (rc == VINF_SUCCESS)
                rc = VMR3ReqProcessU(pVM->pUVM, pVCpu->idCpu, true /*fPriorityOnly*/);
            if (rc != VINF_SUCCESS)
            {
                /** @todo Ignoring these could be bad. */
                RTStrmPrintf(g_pStdErr, "[rcReq=%Rrc, ignored!]", rc);
                RTStrmFlush(g_pStdErr);
            }
        }

        /* next */
        if (!(cWait % 10))
        {
            RTStrmPrintf(g_pStdErr, "%d.", cWait / 10);
            RTStrmFlush(g_pStdErr);
        }
        cWait--;
    }

    RTStrmPrintf(g_pStdErr, "Stopping the VM!\n");
    RTStrmFlush(g_pStdErr);
    return false;
}


/**
 * Forced action callback.
 *
 * The VMM will call this from it's main loop when either VM_FF_DBGF or
 * VMCPU_FF_DBGF are set.
 *
 * The function checks for and executes pending commands from the debugger.
 * Then it checks for pending debug events and serves these.
 *
 * @returns VINF_SUCCESS normally.
 * @returns VERR_DBGF_RAISE_FATAL_ERROR to pretend a fatal error happened.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context per CPU structure.
 */
VMMR3_INT_DECL(int) DBGFR3VMMForcedAction(PVM pVM, PVMCPU pVCpu)
{
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;

    /*
     * Dispatch pending events.
     */
    if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_DBGF))
    {
        if (   pVCpu->dbgf.s.cEvents > 0
            && pVCpu->dbgf.s.aEvents[pVCpu->dbgf.s.cEvents - 1].enmState == DBGFEVENTSTATE_CURRENT)
        {
            rcStrict = DBGFR3EventHandlePending(pVM, pVCpu);
            /** @todo may end up with VERR_DBGF_NOT_ATTACHED here, which will prove fatal... */
        }

        /*
         * Command pending? Process it.
         */
        PUVMCPU pUVCpu = pVCpu->pUVCpu;
        if (pUVCpu->dbgf.s.enmDbgfCmd != DBGFCMD_NO_COMMAND)
        {
            bool            fResumeExecution;
            DBGFCMDDATA     CmdData = pUVCpu->dbgf.s.DbgfCmdData;
            DBGFCMD         enmCmd = dbgfR3CpuGetCmd(pUVCpu);
            VBOXSTRICTRC rcStrict2 = dbgfR3CpuCmd(pVCpu, enmCmd, &CmdData, &fResumeExecution);
            if (!fResumeExecution)
                rcStrict2 = dbgfR3CpuWait(pVCpu);
            if (   rcStrict2 != VINF_SUCCESS
                && (   rcStrict == VINF_SUCCESS
                    || RT_FAILURE(rcStrict2)
                    || rcStrict2 < rcStrict) ) /** @todo oversimplified? */
                rcStrict = rcStrict2;
        }
    }

    return VBOXSTRICTRC_TODO(rcStrict);
}


/**
 * Try to determine the event context.
 *
 * @returns debug event context.
 * @param   pVCpu       The cross context vCPU structure.
 */
static DBGFEVENTCTX dbgfR3FigureEventCtx(PVMCPU pVCpu)
{
    switch (EMGetState(pVCpu))
    {
        case EMSTATE_HM:
        case EMSTATE_NEM:
        case EMSTATE_DEBUG_GUEST_HM:
        case EMSTATE_DEBUG_GUEST_NEM:
            return DBGFEVENTCTX_HM;

        case EMSTATE_IEM:
        case EMSTATE_DEBUG_GUEST_IEM:
        case EMSTATE_DEBUG_GUEST_RAW:
            return DBGFEVENTCTX_RAW;


        case EMSTATE_RECOMPILER:
        case EMSTATE_DEBUG_GUEST_RECOMPILER:
            return DBGFEVENTCTX_REM;

        case EMSTATE_DEBUG_HYPER:
        case EMSTATE_GURU_MEDITATION:
            return DBGFEVENTCTX_HYPER;

        default:
            return DBGFEVENTCTX_OTHER;
    }
}


/**
 * Sends the event to the debugger (i.e. adds it to the event ring buffer).
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The CPU sending the event.
 * @param   enmType     The event type to send.
 * @param   enmCtx      The event context, DBGFEVENTCTX_INVALID will be resolved.
 * @param   pvPayload   Event payload (DBGFEVENT::u data), optional.
 * @param   cbPayload   The size of the event payload, optional.
 */
static int dbgfR3SendEventWorker(PVM pVM, PVMCPU pVCpu, DBGFEVENTTYPE enmType, DBGFEVENTCTX enmCtx,
                                 void const *pvPayload, size_t cbPayload)
{
    PUVM pUVM = pVM->pUVM;
    pVM->dbgf.s.SteppingFilter.idCpu = NIL_VMCPUID; /** @todo per vCPU stepping filter. */

    /*
     * Massage the input a little.
     */
    AssertStmt(cbPayload <= RT_SIZEOFMEMB(DBGFEVENT, u), cbPayload = RT_SIZEOFMEMB(DBGFEVENT, u));
    if (enmCtx == DBGFEVENTCTX_INVALID)
        enmCtx = dbgfR3FigureEventCtx(pVCpu);

    /*
     * Put the event into the ring buffer.
     */
    RTSemFastMutexRequest(pUVM->dbgf.s.hMtxDbgEvtWr);

    uint32_t const cDbgEvtMax     = RT_MAX(1, pUVM->dbgf.s.cDbgEvtMax);
    uint32_t const idxDbgEvtWrite = ASMAtomicReadU32(&pUVM->dbgf.s.idxDbgEvtWrite);
    uint32_t const idxDbgEvtRead  = ASMAtomicReadU32(&pUVM->dbgf.s.idxDbgEvtRead);
    /** @todo Handle full buffer. */ RT_NOREF(idxDbgEvtRead);

    PDBGFEVENT pEvent = &pUVM->dbgf.s.paDbgEvts[idxDbgEvtWrite % cDbgEvtMax];

#ifdef DEBUG
    ASMMemFill32(pEvent, sizeof(*pEvent), UINT32_C(0xdeadbeef));
#endif
    pEvent->enmType   = enmType;
    pEvent->enmCtx    = enmCtx;
    pEvent->idCpu     = pVCpu->idCpu;
    pEvent->uReserved = 0;
    if (cbPayload)
        memcpy(&pEvent->u, pvPayload, cbPayload);

    ASMAtomicWriteU32(&pUVM->dbgf.s.idxDbgEvtWrite, (idxDbgEvtWrite + 1) % cDbgEvtMax);

    RTSemFastMutexRelease(pUVM->dbgf.s.hMtxDbgEvtWr);

    /*
     * Signal the debugger.
     */
    return RTSemEventSignal(pUVM->dbgf.s.hEvtWait);
}


/**
 * Send event and wait for the debugger to respond.
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The CPU sending the event.
 * @param   enmType     The event type to send.
 * @param   enmCtx      The event context, DBGFEVENTCTX_INVALID will be resolved.
 */
DECLINLINE(int) dbgfR3SendEventWait(PVM pVM, PVMCPU pVCpu, DBGFEVENTTYPE enmType, DBGFEVENTCTX enmCtx)
{
    int rc = dbgfR3SendEventWorker(pVM, pVCpu, enmType, enmCtx, NULL, 0);
    if (RT_SUCCESS(rc))
        rc = dbgfR3CpuWait(pVCpu);
    return rc;
}


/**
 * Send event and wait for the debugger to respond, extended version.
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The CPU sending the event.
 * @param   enmType     The event type to send.
 * @param   enmCtx      The event context, DBGFEVENTCTX_INVALID will be resolved.
 * @param   pvPayload   Event payload (DBGFEVENT::u data), optional.
 * @param   cbPayload   The size of the event payload, optional.
 */
DECLINLINE(int) dbgfR3SendEventWaitEx(PVM pVM, PVMCPU pVCpu, DBGFEVENTTYPE enmType, DBGFEVENTCTX enmCtx,
                                      void const *pvPayload, size_t cbPayload)
{
    int rc = dbgfR3SendEventWorker(pVM, pVCpu, enmType, enmCtx, pvPayload, cbPayload);
    if (RT_SUCCESS(rc))
        rc = dbgfR3CpuWait(pVCpu);
    return rc;
}


/**
 * Send event but do NOT wait for the debugger.
 *
 * Currently only used by dbgfR3CpuCmd().
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The CPU sending the event.
 * @param   enmType     The event type to send.
 * @param   enmCtx      The event context, DBGFEVENTCTX_INVALID will be resolved.
 */
DECLINLINE(int) dbgfR3SendEventNoWait(PVM pVM, PVMCPU pVCpu, DBGFEVENTTYPE enmType, DBGFEVENTCTX enmCtx)
{
    return dbgfR3SendEventWorker(pVM, pVCpu, enmType, enmCtx, NULL, 0);
}


/**
 * The common event prologue code.
 *
 * It will make sure someone is attached, and perhaps process any high priority
 * pending actions (none yet).
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The vCPU cross context structure.
 * @param   enmEvent    The event to be sent.
 */
static int dbgfR3EventPrologue(PVM pVM, PVMCPU pVCpu, DBGFEVENTTYPE enmEvent)
{
    /*
     * Check if a debugger is attached.
     */
    if (    !pVM->dbgf.s.fAttached
        &&  !dbgfR3WaitForAttach(pVM, pVCpu, enmEvent))
    {
        Log(("dbgfR3EventPrologue: enmEvent=%d - debugger not attached\n", enmEvent));
        return VERR_DBGF_NOT_ATTACHED;
    }

    /*
     * Look thru pending commands and finish those which make sense now.
     */
    /** @todo Process/purge pending commands. */
    //int rc = DBGFR3VMMForcedAction(pVM);
    return VINF_SUCCESS;
}


/**
 * Processes a pending event on the current CPU.
 *
 * This is called by EM in response to VINF_EM_DBG_EVENT.
 *
 * @returns Strict VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context per CPU structure.
 *
 * @thread  EMT(pVCpu)
 */
VMMR3_INT_DECL(VBOXSTRICTRC) DBGFR3EventHandlePending(PVM pVM, PVMCPU pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_DBGF);

    /*
     * Check that we've got an event first.
     */
    AssertReturn(pVCpu->dbgf.s.cEvents > 0, VINF_SUCCESS);
    AssertReturn(pVCpu->dbgf.s.aEvents[pVCpu->dbgf.s.cEvents - 1].enmState == DBGFEVENTSTATE_CURRENT, VINF_SUCCESS);
    PDBGFEVENT pEvent = &pVCpu->dbgf.s.aEvents[pVCpu->dbgf.s.cEvents - 1].Event;

    /*
     * Make sure we've got a debugger and is allowed to speak to it.
     */
    int rc = dbgfR3EventPrologue(pVM, pVCpu, pEvent->enmType);
    if (RT_FAILURE(rc))
    {
        /** @todo drop them events?   */
        return rc; /** @todo this will cause trouble if we're here via an FF! */
    }

    /*
     * Send the event and mark it as ignore.
     * ASSUMES no new events get generate while dbgfR3CpuWait is executing!
     */
    VBOXSTRICTRC rcStrict = dbgfR3SendEventWaitEx(pVM, pVCpu, pEvent->enmType, pEvent->enmCtx, &pEvent->u, sizeof(pEvent->u));
    pVCpu->dbgf.s.aEvents[pVCpu->dbgf.s.cEvents - 1].enmState = DBGFEVENTSTATE_IGNORE;
    return rcStrict;
}


/**
 * Send a generic debugger event which takes no data.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmEvent    The event to send.
 * @internal
 */
VMMR3DECL(int) DBGFR3Event(PVM pVM, DBGFEVENTTYPE enmEvent)
{
    PVMCPU pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);

    /*
     * Do stepping filtering.
     */
    /** @todo Would be better if we did some of this inside the execution
     *        engines. */
    if (   enmEvent == DBGFEVENT_STEPPED
        || enmEvent == DBGFEVENT_STEPPED_HYPER)
    {
        if (!dbgfStepAreWeThereYet(pVM, pVCpu))
            return VINF_EM_DBG_STEP;
    }

    int rc = dbgfR3EventPrologue(pVM, pVCpu, enmEvent);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Send the event and process the reply communication.
     */
    return dbgfR3SendEventWait(pVM, pVCpu, enmEvent, DBGFEVENTCTX_INVALID);
}


/**
 * Send a debugger event which takes the full source file location.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmEvent    The event to send.
 * @param   pszFile     Source file.
 * @param   uLine       Line number in source file.
 * @param   pszFunction Function name.
 * @param   pszFormat   Message which accompanies the event.
 * @param   ...         Message arguments.
 * @internal
 */
VMMR3DECL(int) DBGFR3EventSrc(PVM pVM, DBGFEVENTTYPE enmEvent, const char *pszFile, unsigned uLine, const char *pszFunction, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    int rc = DBGFR3EventSrcV(pVM, enmEvent, pszFile, uLine, pszFunction, pszFormat, args);
    va_end(args);
    return rc;
}


/**
 * Send a debugger event which takes the full source file location.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmEvent    The event to send.
 * @param   pszFile     Source file.
 * @param   uLine       Line number in source file.
 * @param   pszFunction Function name.
 * @param   pszFormat   Message which accompanies the event.
 * @param   args        Message arguments.
 * @internal
 */
VMMR3DECL(int) DBGFR3EventSrcV(PVM pVM, DBGFEVENTTYPE enmEvent, const char *pszFile, unsigned uLine, const char *pszFunction, const char *pszFormat, va_list args)
{
    PVMCPU pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);

    int rc = dbgfR3EventPrologue(pVM, pVCpu, enmEvent);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Stop other CPUs for some messages so we can inspect the state accross
     * all CPUs as best as possible.
     */
    /** @todo This isn't entirely sane as we'd need a wait to back out of this
     *        if the debugger goes fishing and such. */
    switch (enmEvent)
    {
        default:
            break;
        case DBGFEVENT_DEV_STOP:
            rc = dbgfR3EventHaltAllVCpus(pVM, pVCpu);
            if (RT_SUCCESS(rc))
                break;
            return rc;
    }

    /*
     * Format the message.
     */
    char   *pszMessage = NULL;
    char    szMessage[8192];
    if (pszFormat && *pszFormat)
    {
        pszMessage = &szMessage[0];
        RTStrPrintfV(szMessage, sizeof(szMessage), pszFormat, args);
    }

    /*
     * Send the event and process the reply communication.
     */
    DBGFEVENT DbgEvent;  /** @todo split up DBGFEVENT so we can skip the dead wait on the stack? */
    DbgEvent.u.Src.pszFile      = pszFile;
    DbgEvent.u.Src.uLine        = uLine;
    DbgEvent.u.Src.pszFunction  = pszFunction;
    DbgEvent.u.Src.pszMessage   = pszMessage;
    return dbgfR3SendEventWaitEx(pVM, pVCpu, enmEvent, DBGFEVENTCTX_INVALID, &DbgEvent.u, sizeof(DbgEvent.u.Src));
}


/**
 * Send a debugger event which takes the two assertion messages.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmEvent    The event to send.
 * @param   pszMsg1     First assertion message.
 * @param   pszMsg2     Second assertion message.
 */
VMMR3_INT_DECL(int) DBGFR3EventAssertion(PVM pVM, DBGFEVENTTYPE enmEvent, const char *pszMsg1, const char *pszMsg2)
{
    PVMCPU pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);

    int rc = dbgfR3EventPrologue(pVM, pVCpu, enmEvent);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Send the event and process the reply communication.
     */
    DBGFEVENT DbgEvent;
    DbgEvent.u.Assert.pszMsg1 = pszMsg1;
    DbgEvent.u.Assert.pszMsg2 = pszMsg2;
    return dbgfR3SendEventWaitEx(pVM, pVCpu, enmEvent, DBGFEVENTCTX_INVALID, &DbgEvent.u, sizeof(DbgEvent.u.Assert));
}


/**
 * Breakpoint was hit somewhere.
 * Figure out which breakpoint it is and notify the debugger.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmEvent    DBGFEVENT_BREAKPOINT_HYPER or DBGFEVENT_BREAKPOINT.
 */
VMMR3_INT_DECL(int) DBGFR3EventBreakpoint(PVM pVM, DBGFEVENTTYPE enmEvent)
{
    PVMCPU pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu, VERR_VM_THREAD_NOT_EMT);

    int rc = dbgfR3EventPrologue(pVM, pVCpu, enmEvent);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Halt all other vCPUs as well to give the user the ability to inspect other
     * vCPU states as well.
     */
    rc = dbgfR3EventHaltAllVCpus(pVM, pVCpu);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Send the event and process the reply communication.
     */
    DBGFEVENT DbgEvent;
    DbgEvent.u.Bp.hBp = pVCpu->dbgf.s.hBpActive;
    pVCpu->dbgf.s.hBpActive = NIL_DBGFBP;
    if (DbgEvent.u.Bp.hBp != NIL_DBGFBP)
    {
        DbgEvent.enmCtx = DBGFEVENTCTX_RAW;
        return dbgfR3SendEventWaitEx(pVM, pVCpu, enmEvent, DBGFEVENTCTX_RAW, &DbgEvent.u, sizeof(DbgEvent.u.Bp));
    }

    return VERR_DBGF_IPE_1;
}


/**
 * Returns whether the given vCPU is waiting for the debugger.
 *
 * @returns Flags whether the vCPU is currently waiting for the debugger.
 * @param   pUVCpu              The user mode vCPU structure.
 */
DECLINLINE(bool) dbgfR3CpuIsHalted(PUVMCPU pUVCpu)
{
    return ASMAtomicReadBool(&pUVCpu->dbgf.s.fStopped);
}


/**
 * Checks whether the given vCPU is waiting in the debugger.
 *
 * @returns Flag whether the indicated vCPU is halted, when VMCPUID_ALL
 *          is given true is returned when at least one vCPU is halted.
 * @param   pUVM        The user mode VM structure.
 * @param   idCpu       The CPU ID to check, VMCPUID_ALL to check all vCPUs.
 */
DECLINLINE(bool) dbgfR3CpuAreAnyHaltedByCpuId(PUVM pUVM, VMCPUID idCpu)
{
    AssertReturn(idCpu < pUVM->cCpus || idCpu == VMCPUID_ALL, false);

    /* Check that either the given vCPU or all are actually halted. */
    if (idCpu != VMCPUID_ALL)
        return dbgfR3CpuIsHalted(&pUVM->aCpus[idCpu]);

    for (VMCPUID i = 0; i < pUVM->cCpus; i++)
        if (dbgfR3CpuIsHalted(&pUVM->aCpus[i]))
            return true;
    return false;
}


/**
 * Gets the pending debug command for this EMT/CPU, replacing it with
 * DBGFCMD_NO_COMMAND.
 *
 * @returns Pending command.
 * @param   pUVCpu          The user mode virtual CPU structure.
 * @thread  EMT(pUVCpu)
 */
DECLINLINE(DBGFCMD) dbgfR3CpuGetCmd(PUVMCPU pUVCpu)
{
    DBGFCMD enmCmd = (DBGFCMD)ASMAtomicXchgU32((uint32_t volatile *)(void *)&pUVCpu->dbgf.s.enmDbgfCmd, DBGFCMD_NO_COMMAND);
    Log2(("DBGF: Getting command: %d\n", enmCmd));
    return enmCmd;
}


/**
 * Send a debug command to a CPU, making sure to notify it.
 *
 * @returns VBox status code.
 * @param   pUVCpu      The user mode virtual CPU structure.
 * @param   enmCmd      The command to submit to the CPU.
 */
DECLINLINE(int) dbgfR3CpuSetCmdAndNotify(PUVMCPU pUVCpu, DBGFCMD enmCmd)
{
    Log2(("DBGF: Setting command to %d\n", enmCmd));
    Assert(enmCmd != DBGFCMD_NO_COMMAND);
    AssertMsg(pUVCpu->dbgf.s.enmDbgfCmd == DBGFCMD_NO_COMMAND, ("enmCmd=%d enmDbgfCmd=%d\n", enmCmd, pUVCpu->dbgf.s.enmDbgfCmd));

    ASMAtomicWriteU32((uint32_t volatile *)(void *)&pUVCpu->dbgf.s.enmDbgfCmd, enmCmd);
    VMCPU_FF_SET(pUVCpu->pVCpu, VMCPU_FF_DBGF);

    VMR3NotifyCpuFFU(pUVCpu, 0 /*fFlags*/);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3EventHaltEmtWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    RT_NOREF(pvUser);

    VMCPU_ASSERT_EMT(pVCpu);
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    PUVMCPU pUVCpu = pVCpu->pUVCpu;
    if (   pVCpu != (PVMCPU)pvUser
        && !dbgfR3CpuIsHalted(pUVCpu))
        dbgfR3CpuSetCmdAndNotify(pUVCpu, DBGFCMD_HALT);

    return VINF_SUCCESS;
}


/**
 * Halts all vCPUs of the given VM except for the given one.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpuExclude    The vCPU cross context structure of the vCPU to exclude.
 */
static int dbgfR3EventHaltAllVCpus(PVM pVM, PVMCPU pVCpuExclude)
{
    return VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3EventHaltEmtWorker, pVCpuExclude);
}


/**
 * Waits for the debugger to respond.
 *
 * @returns VBox status code. (clearify)
 * @param   pVCpu               The cross context vCPU structure.
 */
static int dbgfR3CpuWait(PVMCPU pVCpu)
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);
    PUVMCPU pUVCpu = pVCpu->pUVCpu;

    LogFlow(("dbgfR3CpuWait:\n"));
    int rcRet = VINF_SUCCESS;

    ASMAtomicWriteBool(&pUVCpu->dbgf.s.fStopped, true);

    /*
     * Waits for the debugger to reply (i.e. issue an command).
     */
    for (;;)
    {
        /*
         * Wait.
         */
        for (;;)
        {
            /*
             * Process forced flags before we go sleep.
             */
            if (   VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_DBGF | VMCPU_FF_REQUEST)
                || VM_FF_IS_ANY_SET(pVM, VM_FF_EMT_RENDEZVOUS | VMCPU_FF_REQUEST | VM_FF_CHECK_VM_STATE))
            {
                if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_DBGF))
                    break;

                int rc;
                if (VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS))
                    rc = VMMR3EmtRendezvousFF(pVM, pVCpu);
                else if (   VM_FF_IS_SET(pVM, VM_FF_REQUEST)
                         || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_REQUEST))
                {
                    LogFlow(("dbgfR3CpuWait: Processes requests...\n"));
                    rc = VMR3ReqProcessU(pVM->pUVM, VMCPUID_ANY, false /*fPriorityOnly*/);
                    if (rc == VINF_SUCCESS)
                        rc = VMR3ReqProcessU(pVM->pUVM, pVCpu->idCpu, false /*fPriorityOnly*/);
                    LogFlow(("dbgfR3CpuWait: VMR3ReqProcess -> %Rrc rcRet=%Rrc\n", rc, rcRet));
                }
                else if (VM_FF_IS_SET(pVM, VM_FF_CHECK_VM_STATE))
                {
                    VMSTATE enmState = VMR3GetState(pVM);
                    switch (enmState)
                    {
                        case VMSTATE_FATAL_ERROR:
                        case VMSTATE_FATAL_ERROR_LS:
                        case VMSTATE_GURU_MEDITATION:
                        case VMSTATE_GURU_MEDITATION_LS:
                            rc = VINF_EM_SUSPEND;
                            break;
                        case VMSTATE_DESTROYING:
                            rc = VINF_EM_TERMINATE;
                            break;
                        default:
                            rc = VERR_DBGF_IPE_1;
                            AssertMsgFailed(("%s\n", VMGetStateName(enmState)));
                    }
                }
                else
                    rc = VINF_SUCCESS;
                if (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST)
                {
                    switch (rc)
                    {
                        case VINF_EM_DBG_BREAKPOINT:
                        case VINF_EM_DBG_STEPPED:
                        case VINF_EM_DBG_STEP:
                        case VINF_EM_DBG_STOP:
                        case VINF_EM_DBG_EVENT:
                            AssertMsgFailed(("rc=%Rrc\n", rc));
                            break;

                        /* return straight away */
                        case VINF_EM_TERMINATE:
                        case VINF_EM_OFF:
                            LogFlow(("dbgfR3CpuWait: returns %Rrc\n", rc));
                            ASMAtomicWriteBool(&pUVCpu->dbgf.s.fStopped, false);
                            return rc;

                        /* remember return code. */
                        default:
                            AssertReleaseMsgFailed(("rc=%Rrc is not in the switch!\n", rc));
                            RT_FALL_THRU();
                        case VINF_EM_RESET:
                        case VINF_EM_SUSPEND:
                        case VINF_EM_HALT:
                        case VINF_EM_RESUME:
                        case VINF_EM_RESCHEDULE:
                        case VINF_EM_RESCHEDULE_REM:
                            if (rc < rcRet || rcRet == VINF_SUCCESS)
                                rcRet = rc;
                            break;
                    }
                }
                else if (RT_FAILURE(rc))
                {
                    LogFlow(("dbgfR3CpuWait: returns %Rrc\n", rc));
                    ASMAtomicWriteBool(&pUVCpu->dbgf.s.fStopped, false);
                    return rc;
                }
            }
            else if (pVM->dbgf.s.fAttached)
            {
                int rc = VMR3WaitU(pUVCpu);
                if (RT_FAILURE(rc))
                {
                    LogFlow(("dbgfR3CpuWait: returns %Rrc (VMR3WaitU)\n", rc));
                    ASMAtomicWriteBool(&pUVCpu->dbgf.s.fStopped, false);
                    return rc;
                }
            }
            else
            {
                LogFlow(("dbgfR3CpuWait: Debugger detached, continuing normal execution (%Rrc)\n", rcRet));
                ASMAtomicWriteBool(&pUVCpu->dbgf.s.fStopped, false);
                return rcRet;
            }
        }

        /*
         * Process the command.
         */
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_DBGF);
        bool            fResumeExecution;
        DBGFCMDDATA     CmdData = pUVCpu->dbgf.s.DbgfCmdData;
        DBGFCMD         enmCmd = dbgfR3CpuGetCmd(pUVCpu);
        int rc = dbgfR3CpuCmd(pVCpu, enmCmd, &CmdData, &fResumeExecution);
        if (fResumeExecution)
        {
            if (RT_FAILURE(rc))
                rcRet = rc;
            else if (    rc >= VINF_EM_FIRST
                     &&  rc <= VINF_EM_LAST
                     &&  (rc < rcRet || rcRet == VINF_SUCCESS))
                rcRet = rc;
            LogFlow(("dbgfR3CpuWait: returns %Rrc\n", rcRet));
            ASMAtomicWriteBool(&pUVCpu->dbgf.s.fStopped, false);
            return rcRet;
        }
    }
}


/**
 * Executes command from debugger.
 *
 * The caller is responsible for waiting or resuming execution based on the
 * value returned in the *pfResumeExecution indicator.
 *
 * @returns VBox status code. (clearify!)
 * @param   pVCpu               The cross context vCPU structure.
 * @param   enmCmd              The command in question.
 * @param   pCmdData            Pointer to the command data.
 * @param   pfResumeExecution   Where to store the resume execution / continue waiting indicator.
 */
static int dbgfR3CpuCmd(PVMCPU pVCpu, DBGFCMD enmCmd, PDBGFCMDDATA pCmdData, bool *pfResumeExecution)
{
    RT_NOREF(pCmdData); /* for later */

    /*
     * The cases in this switch returns directly if no event to send.
     */
    DBGFEVENTTYPE enmEvent;
    DBGFEVENTCTX  enmCtx = DBGFEVENTCTX_INVALID;
    switch (enmCmd)
    {
        /*
         * Halt is answered by an event say that we've halted.
         */
        case DBGFCMD_HALT:
        {
            *pfResumeExecution = false;
            enmEvent = DBGFEVENT_HALT_DONE;
            break;
        }


        /*
         * Resume is not answered, we just resume execution.
         */
        case DBGFCMD_GO:
        {
            pVCpu->dbgf.s.fSingleSteppingRaw = false;
            *pfResumeExecution = true;
            return VINF_SUCCESS;
        }

        /** @todo implement (and define) the rest of the commands. */

        /*
         * Single step, with trace into.
         */
        case DBGFCMD_SINGLE_STEP:
        {
            Log2(("Single step\n"));
            PVM pVM = pVCpu->CTX_SUFF(pVM);
            if (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_OVER)
            {
                if (dbgfStepGetCurInstrType(pVM, pVCpu) == DBGFSTEPINSTRTYPE_CALL)
                    pVM->dbgf.s.SteppingFilter.uCallDepth++;
            }
            if (pVM->dbgf.s.SteppingFilter.cMaxSteps > 0)
            {
                pVCpu->dbgf.s.fSingleSteppingRaw = true;
                *pfResumeExecution = true;
                return VINF_EM_DBG_STEP;
            }
            /* Stop after zero steps. Nonsense, but whatever. */
            pVM->dbgf.s.SteppingFilter.idCpu = NIL_VMCPUID;
            *pfResumeExecution = false;
            enmCtx   = dbgfR3FigureEventCtx(pVCpu);
            enmEvent = enmCtx != DBGFEVENTCTX_HYPER ? DBGFEVENT_STEPPED : DBGFEVENT_STEPPED_HYPER;
            break;
        }

        /*
         * Default is to send an invalid command event.
         */
        default:
        {
            *pfResumeExecution = false;
            enmEvent = DBGFEVENT_INVALID_COMMAND;
            break;
        }
    }

    /*
     * Send the pending event.
     */
    Log2(("DBGF: Emulation thread: sending event %d\n", enmEvent));
    int rc = dbgfR3SendEventNoWait(pVCpu->CTX_SUFF(pVM), pVCpu, enmEvent, enmCtx);
    AssertRCStmt(rc, *pfResumeExecution = true);
    return rc;
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS,
 * EMT rendezvous worker for DBGFR3Attach - only called on one EMT.}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3Attach(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    PUVM pUVM = pVM->pUVM;
    int *prcAttach = (int *)pvUser;
    RT_NOREF(pVCpu);

    if (pVM->dbgf.s.fAttached)
    {
        Log(("dbgfR3Attach: Debugger already attached\n"));
        *prcAttach = VERR_DBGF_ALREADY_ATTACHED;
        return VINF_SUCCESS;
    }

    /*
     * The per-CPU bits.
     */
    for (uint32_t i = 0; i < pUVM->cCpus; i++)
    {
        PUVMCPU pUVCpu = &pUVM->aCpus[i];

        pUVCpu->dbgf.s.enmDbgfCmd = DBGFCMD_NO_COMMAND;
        RT_ZERO(pUVCpu->dbgf.s.DbgfCmdData);
    }

    /*
     * Init of the VM -> Debugger communication part living in the global VM structure.
     */
    pUVM->dbgf.s.cDbgEvtMax      = pVM->cCpus * 5 + 10; /* Initial size of event ring, increased when being full. */
    pUVM->dbgf.s.idxDbgEvtWrite  = 0;
    pUVM->dbgf.s.idxDbgEvtRead   = 0;
    pUVM->dbgf.s.hEvtWait        = NIL_RTSEMEVENT;
    pUVM->dbgf.s.hEvtRingBufFull = NIL_RTSEMEVENTMULTI;
    pUVM->dbgf.s.hMtxDbgEvtWr    = NIL_RTSEMFASTMUTEX;
    int rc;
    pUVM->dbgf.s.paDbgEvts       = (PDBGFEVENT)MMR3HeapAllocU(pUVM, MM_TAG_DBGF, pUVM->dbgf.s.cDbgEvtMax * sizeof(DBGFEVENT));
    if (pUVM->dbgf.s.paDbgEvts)
    {
        rc = RTSemEventCreate(&pUVM->dbgf.s.hEvtWait);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemFastMutexCreate(&pUVM->dbgf.s.hMtxDbgEvtWr);
            if (RT_SUCCESS(rc))
            {
                rc = RTSemEventMultiCreate(&pUVM->dbgf.s.hEvtRingBufFull);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * At last, set the attached flag.
                     */
                    ASMAtomicWriteBool(&pVM->dbgf.s.fAttached, true);
                    *prcAttach = VINF_SUCCESS;
                    return VINF_SUCCESS;
                }

                RTSemFastMutexDestroy(pUVM->dbgf.s.hMtxDbgEvtWr);
                pUVM->dbgf.s.hMtxDbgEvtWr = NIL_RTSEMFASTMUTEX;
            }
            RTSemEventDestroy(pUVM->dbgf.s.hEvtWait);
            pUVM->dbgf.s.hEvtWait = NIL_RTSEMEVENT;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    *prcAttach = rc;
    return VINF_SUCCESS;
}


/**
 * Attaches a debugger to the specified VM.
 *
 * Only one debugger at a time.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(int) DBGFR3Attach(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Call the VM, use EMT rendezvous for serialization.
     */
    int rcAttach = VERR_IPE_UNINITIALIZED_STATUS;
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE | VMMEMTRENDEZVOUS_FLAGS_PRIORITY, dbgfR3Attach, &rcAttach);
    if (RT_SUCCESS(rc))
        rc = rcAttach;

    return rc;
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS,
 * EMT rendezvous worker for DBGFR3Detach - called on all EMTs (why?).}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3Detach(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    if (pVCpu->idCpu == 0)
    {
        PUVM pUVM = (PUVM)pvUser;

        /*
         * Per-CPU cleanup.
         */
        for (VMCPUID i = 0; i < pUVM->cCpus; i++)
        {
            PUVMCPU pUVCpu = &pUVM->aCpus[i];

            pUVCpu->dbgf.s.enmDbgfCmd = DBGFCMD_NO_COMMAND;
            RT_ZERO(pUVCpu->dbgf.s.DbgfCmdData);
        }

        /*
         * De-init of the VM -> Debugger communication part living in the global VM structure.
         */
        if (pUVM->dbgf.s.paDbgEvts)
        {
            MMR3HeapFree(pUVM->dbgf.s.paDbgEvts);
            pUVM->dbgf.s.paDbgEvts = NULL;
        }

        if (pUVM->dbgf.s.hEvtWait != NIL_RTSEMEVENT)
        {
            RTSemEventDestroy(pUVM->dbgf.s.hEvtWait);
            pUVM->dbgf.s.hEvtWait = NIL_RTSEMEVENT;
        }

        if (pUVM->dbgf.s.hMtxDbgEvtWr != NIL_RTSEMFASTMUTEX)
        {
            RTSemFastMutexDestroy(pUVM->dbgf.s.hMtxDbgEvtWr);
            pUVM->dbgf.s.hMtxDbgEvtWr = NIL_RTSEMFASTMUTEX;
        }

        if (pUVM->dbgf.s.hEvtRingBufFull != NIL_RTSEMEVENTMULTI)
        {
            RTSemEventMultiDestroy(pUVM->dbgf.s.hEvtRingBufFull);
            pUVM->dbgf.s.hEvtRingBufFull = NIL_RTSEMEVENTMULTI;
        }

        pUVM->dbgf.s.cDbgEvtMax      = 0;
        pUVM->dbgf.s.idxDbgEvtWrite  = 0;
        pUVM->dbgf.s.idxDbgEvtRead   = 0;
        pUVM->dbgf.s.hEvtWait        = NIL_RTSEMEVENT;
        pUVM->dbgf.s.hEvtRingBufFull = NIL_RTSEMEVENTMULTI;
        pUVM->dbgf.s.hMtxDbgEvtWr    = NIL_RTSEMFASTMUTEX;

        ASMAtomicWriteBool(&pVM->dbgf.s.fAttached, false);
    }

    return VINF_SUCCESS;
}


/**
 * Detaches a debugger from the specified VM.
 *
 * Caller must be attached to the VM.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(int) DBGFR3Detach(PUVM pUVM)
{
    LogFlow(("DBGFR3Detach:\n"));

    /*
     * Validate input. The UVM handle shall be valid, the VM handle might be
     * in the processes of being destroyed already, so deal quietly with that.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    if (!VM_IS_VALID_EXT(pVM))
        return VERR_INVALID_VM_HANDLE;

    /*
     * Check if attached.
     */
    if (!pVM->dbgf.s.fAttached)
        return VERR_DBGF_NOT_ATTACHED;

    return VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE | VMMEMTRENDEZVOUS_FLAGS_PRIORITY, dbgfR3Detach, pUVM);
}


/**
 * Wait for a debug event.
 *
 * @returns VBox status code. Will not return VBOX_INTERRUPTED.
 * @param   pUVM        The user mode VM handle.
 * @param   cMillies    Number of millis to wait.
 * @param   pEvent      Where to store the event data.
 */
VMMR3DECL(int) DBGFR3EventWait(PUVM pUVM, RTMSINTERVAL cMillies, PDBGFEVENT pEvent)
{
    /*
     * Check state.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(pVM->dbgf.s.fAttached, VERR_DBGF_NOT_ATTACHED);

    RT_BZERO(pEvent, sizeof(*pEvent));

    /*
     * Wait for an event to arrive if there are none.
     */
    int rc = VINF_SUCCESS;
    uint32_t idxDbgEvtRead = ASMAtomicReadU32(&pUVM->dbgf.s.idxDbgEvtRead);
    if (idxDbgEvtRead == ASMAtomicReadU32(&pUVM->dbgf.s.idxDbgEvtWrite))
    {
        do
        {
            rc = RTSemEventWait(pUVM->dbgf.s.hEvtWait, cMillies);
        } while (   RT_SUCCESS(rc)
                 && idxDbgEvtRead == ASMAtomicReadU32(&pUVM->dbgf.s.idxDbgEvtWrite));
    }

    if (RT_SUCCESS(rc))
    {
        Assert(idxDbgEvtRead != ASMAtomicReadU32(&pUVM->dbgf.s.idxDbgEvtWrite));

        uint32_t const cDbgEvtMax = RT_MAX(1, pUVM->dbgf.s.cDbgEvtMax);
        memcpy(pEvent, &pUVM->dbgf.s.paDbgEvts[idxDbgEvtRead % cDbgEvtMax], sizeof(*pEvent));
        ASMAtomicWriteU32(&pUVM->dbgf.s.idxDbgEvtRead, (idxDbgEvtRead + 1) % cDbgEvtMax);
    }

    Log2(("DBGFR3EventWait: rc=%Rrc (event type %d)\n", rc, pEvent->enmType));
    return rc;
}


/**
 * Halts VM execution.
 *
 * After calling this the VM isn't actually halted till an DBGFEVENT_HALT_DONE
 * arrives. Until that time it's not possible to issue any new commands.
 *
 * @returns VBox status code.
 * @retval  VWRN_DBGF_ALREADY_HALTED if @a idCpu is VMCPUID_ALL and all vCPUs
 *          are halted.
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The vCPU to halt, VMCPUID_ALL halts all still running vCPUs.
 */
VMMR3DECL(int) DBGFR3Halt(PUVM pUVM, VMCPUID idCpu)
{
    /*
     * Check state.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(pVM->dbgf.s.fAttached, VERR_DBGF_NOT_ATTACHED);
    AssertReturn(idCpu == VMCPUID_ALL || idCpu < pVM->cCpus, VERR_INVALID_CPU_ID);

    /*
     * Halt the requested CPUs as needed.
     */
    int rc;
    if (idCpu != VMCPUID_ALL)
    {
        PUVMCPU pUVCpu = &pUVM->aCpus[idCpu];
        if (!dbgfR3CpuIsHalted(pUVCpu))
        {
            dbgfR3CpuSetCmdAndNotify(pUVCpu, DBGFCMD_HALT);
            rc = VINF_SUCCESS;
        }
        else
            rc = VWRN_DBGF_ALREADY_HALTED;
    }
    else
    {
        rc = VWRN_DBGF_ALREADY_HALTED;
        for (VMCPUID i = 0; i < pUVM->cCpus; i++)
        {
            PUVMCPU pUVCpu = &pUVM->aCpus[i];
            if (!dbgfR3CpuIsHalted(pUVCpu))
            {
                dbgfR3CpuSetCmdAndNotify(pUVCpu, DBGFCMD_HALT);
                rc = VINF_SUCCESS;
            }
        }
    }

    return rc;
}


/**
 * Checks if any of the specified vCPUs have been halted by the debugger.
 *
 * @returns True if at least one halted vCPUs.
 * @returns False if no halted vCPUs.
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The CPU id to check for, VMCPUID_ALL will return true if
 *                      at least a single vCPU is halted in the debugger.
 */
VMMR3DECL(bool) DBGFR3IsHalted(PUVM pUVM, VMCPUID idCpu)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    AssertReturn(pVM->dbgf.s.fAttached, false);

    return dbgfR3CpuAreAnyHaltedByCpuId(pUVM, idCpu);
}


/**
 * Checks if the debugger can wait for events or not.
 *
 * This function is only used by lazy, multiplexing debuggers. :-)
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if waitable.
 * @retval  VERR_SEM_OUT_OF_TURN if not waitable.
 * @retval  VERR_INVALID_VM_HANDLE if the VM is being (/ has been) destroyed
 *          (not asserted) or if the handle is invalid (asserted).
 * @retval  VERR_DBGF_NOT_ATTACHED if not attached.
 *
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(int) DBGFR3QueryWaitable(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /* Note! There is a slight race here, unfortunately. */
    PVM pVM = pUVM->pVM;
    if (!RT_VALID_PTR(pVM))
        return VERR_INVALID_VM_HANDLE;
    if (pVM->enmVMState >= VMSTATE_DESTROYING)
        return VERR_INVALID_VM_HANDLE;
    if (!pVM->dbgf.s.fAttached)
        return VERR_DBGF_NOT_ATTACHED;

    /** @todo was: if (!RTSemPongShouldWait(...)) return VERR_SEM_OUT_OF_TURN; */
    return VINF_SUCCESS;
}


/**
 * Resumes VM execution.
 *
 * There is no receipt event on this command.
 *
 * @returns VBox status code.
 * @retval  VWRN_DBGF_ALREADY_RUNNING if the specified vCPUs are all running.
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The vCPU to resume, VMCPUID_ALL resumes all still halted vCPUs.
 */
VMMR3DECL(int) DBGFR3Resume(PUVM pUVM, VMCPUID idCpu)
{
    /*
     * Validate input and attachment state.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(pVM->dbgf.s.fAttached, VERR_DBGF_NOT_ATTACHED);

    /*
     * Ping the halted emulation threads, telling them to run.
     */
    int rc = VWRN_DBGF_ALREADY_RUNNING;
    if (idCpu != VMCPUID_ALL)
    {
        PUVMCPU pUVCpu = &pUVM->aCpus[idCpu];
        if (dbgfR3CpuIsHalted(pUVCpu))
        {
            rc = dbgfR3CpuSetCmdAndNotify(pUVCpu, DBGFCMD_GO);
            AssertRC(rc);
        }
    }
    else
    {
        for (VMCPUID i = 0; i < pUVM->cCpus; i++)
        {
            PUVMCPU pUVCpu = &pUVM->aCpus[i];
            if (dbgfR3CpuIsHalted(pUVCpu))
            {
                int rc2 = dbgfR3CpuSetCmdAndNotify(pUVCpu, DBGFCMD_GO);
                AssertRC(rc2);
                if (rc == VWRN_DBGF_ALREADY_RUNNING || RT_FAILURE(rc2))
                    rc = rc2;
            }
        }
    }

    return rc;
}


/**
 * Classifies the current instruction.
 *
 * @returns Type of instruction.
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The current CPU.
 * @thread  EMT(pVCpu)
 */
static DBGFSTEPINSTRTYPE dbgfStepGetCurInstrType(PVM pVM, PVMCPU pVCpu)
{
    /*
     * Read the instruction.
     */
    size_t   cbRead   = 0;
    uint8_t  abOpcode[16] = { 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0 };
    int rc = PGMR3DbgReadGCPtr(pVM, abOpcode, CPUMGetGuestFlatPC(pVCpu), sizeof(abOpcode) - 1, 0 /*fFlags*/, &cbRead);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do minimal parsing.  No real need to involve the disassembler here.
         */
        uint8_t *pb = abOpcode;
        for (;;)
        {
            switch (*pb++)
            {
                default:
                    return DBGFSTEPINSTRTYPE_OTHER;

                case 0xe8: /* call rel16/32 */
                case 0x9a: /* call farptr */
                case 0xcc: /* int3 */
                case 0xcd: /* int xx */
                // case 0xce: /* into */
                    return DBGFSTEPINSTRTYPE_CALL;

                case 0xc2: /* ret xx */
                case 0xc3: /* ret */
                case 0xca: /* retf xx */
                case 0xcb: /* retf */
                case 0xcf: /* iret */
                    return DBGFSTEPINSTRTYPE_RET;

                case 0xff:
                    if (   ((*pb >> X86_MODRM_REG_SHIFT) & X86_MODRM_REG_SMASK) == 2  /* call indir */
                        || ((*pb >> X86_MODRM_REG_SHIFT) & X86_MODRM_REG_SMASK) == 3) /* call indir-farptr */
                        return DBGFSTEPINSTRTYPE_CALL;
                    return DBGFSTEPINSTRTYPE_OTHER;

                case 0x0f:
                    switch (*pb++)
                    {
                        case 0x05: /* syscall */
                        case 0x34: /* sysenter */
                            return DBGFSTEPINSTRTYPE_CALL;
                        case 0x07: /* sysret */
                        case 0x35: /* sysexit */
                            return DBGFSTEPINSTRTYPE_RET;
                    }
                    break;

                /* Must handle some REX prefixes. So we do all normal prefixes. */
                case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
                case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
                    if (!CPUMIsGuestIn64BitCode(pVCpu))
                        return DBGFSTEPINSTRTYPE_OTHER;
                    break;

                case 0x2e: /* CS */
                case 0x36: /* SS */
                case 0x3e: /* DS */
                case 0x26: /* ES */
                case 0x64: /* FS */
                case 0x65: /* GS */
                case 0x66: /* op size */
                case 0x67: /* addr size */
                case 0xf0: /* lock */
                case 0xf2: /* REPNZ */
                case 0xf3: /* REPZ */
                    break;
            }
        }
    }

    return DBGFSTEPINSTRTYPE_INVALID;
}


/**
 * Checks if the stepping has reached a stop point.
 *
 * Called when raising a stepped event.
 *
 * @returns true if the event should be raised, false if we should take one more
 *          step first.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context per CPU structure of the calling EMT.
 * @thread  EMT(pVCpu)
 */
static bool dbgfStepAreWeThereYet(PVM pVM, PVMCPU pVCpu)
{
    /*
     * Check valid pVCpu and that it matches the CPU one stepping.
     */
    if (pVCpu)
    {
        if (pVCpu->idCpu == pVM->dbgf.s.SteppingFilter.idCpu)
        {
            /*
             * Increase the number of steps and see if we've reached the max.
             */
            pVM->dbgf.s.SteppingFilter.cSteps++;
            if (pVM->dbgf.s.SteppingFilter.cSteps < pVM->dbgf.s.SteppingFilter.cMaxSteps)
            {
                /*
                 * Check PC and SP address filtering.
                 */
                if (pVM->dbgf.s.SteppingFilter.fFlags & (DBGF_STEP_F_STOP_ON_ADDRESS | DBGF_STEP_F_STOP_ON_STACK_POP))
                {
                    if (   (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_STOP_ON_ADDRESS)
                        && pVM->dbgf.s.SteppingFilter.AddrPc == CPUMGetGuestFlatPC(pVCpu))
                        return true;
                    if (   (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_STOP_ON_STACK_POP)
                        &&   CPUMGetGuestFlatSP(pVCpu) - pVM->dbgf.s.SteppingFilter.AddrStackPop
                           < pVM->dbgf.s.SteppingFilter.cbStackPop)
                        return true;
                }

                /*
                 * Do step-over filtering separate from the step-into one.
                 */
                if (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_OVER)
                {
                    DBGFSTEPINSTRTYPE enmType = dbgfStepGetCurInstrType(pVM, pVCpu);
                    switch (enmType)
                    {
                        default:
                            if (   pVM->dbgf.s.SteppingFilter.uCallDepth != 0
                                || (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_STOP_FILTER_MASK))
                                break;
                            return true;
                        case DBGFSTEPINSTRTYPE_CALL:
                            if (   (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_STOP_ON_CALL)
                                && pVM->dbgf.s.SteppingFilter.uCallDepth == 0)
                                return true;
                            pVM->dbgf.s.SteppingFilter.uCallDepth++;
                            break;
                        case DBGFSTEPINSTRTYPE_RET:
                            if (pVM->dbgf.s.SteppingFilter.uCallDepth == 0)
                            {
                                if (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_STOP_ON_RET)
                                    return true;
                                /* If after return, we use the cMaxStep limit to stop the next time. */
                                if (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_STOP_AFTER_RET)
                                    pVM->dbgf.s.SteppingFilter.cMaxSteps = pVM->dbgf.s.SteppingFilter.cSteps + 1;
                            }
                            else if (pVM->dbgf.s.SteppingFilter.uCallDepth > 0)
                                pVM->dbgf.s.SteppingFilter.uCallDepth--;
                            break;
                    }
                    return false;
                }
                /*
                 * Filtered step-into.
                 */
                else if (  pVM->dbgf.s.SteppingFilter.fFlags
                         & (DBGF_STEP_F_STOP_ON_CALL | DBGF_STEP_F_STOP_ON_RET | DBGF_STEP_F_STOP_AFTER_RET))
                {
                    DBGFSTEPINSTRTYPE enmType = dbgfStepGetCurInstrType(pVM, pVCpu);
                    switch (enmType)
                    {
                        default:
                            break;
                        case DBGFSTEPINSTRTYPE_CALL:
                            if (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_STOP_ON_CALL)
                                return true;
                            break;
                        case DBGFSTEPINSTRTYPE_RET:
                            if (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_STOP_ON_RET)
                                return true;
                            /* If after return, we use the cMaxStep limit to stop the next time. */
                            if (pVM->dbgf.s.SteppingFilter.fFlags & DBGF_STEP_F_STOP_AFTER_RET)
                                pVM->dbgf.s.SteppingFilter.cMaxSteps = pVM->dbgf.s.SteppingFilter.cSteps + 1;
                            break;
                    }
                    return false;
                }
            }
        }
    }

    return true;
}


/**
 * Step Into.
 *
 * A single step event is generated from this command.
 * The current implementation is not reliable, so don't rely on the event coming.
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM handle.
 * @param   idCpu   The ID of the CPU to single step on.
 */
VMMR3DECL(int) DBGFR3Step(PUVM pUVM, VMCPUID idCpu)
{
    return DBGFR3StepEx(pUVM, idCpu, DBGF_STEP_F_INTO, NULL, NULL, 0, 1);
}


/**
 * Full fleged step.
 *
 * This extended stepping API allows for doing multiple steps before raising an
 * event, helping implementing step over, step out and other more advanced
 * features.
 *
 * Like the DBGFR3Step() API, this will normally generate a DBGFEVENT_STEPPED or
 * DBGFEVENT_STEPPED_EVENT.  However the stepping may be interrupted by other
 * events, which will abort the stepping.
 *
 * The stop on pop area feature is for safeguarding step out.
 *
 * Please note though, that it will always use stepping and never breakpoints.
 * While this allows for a much greater flexibility it can at times be rather
 * slow.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   idCpu           The ID of the CPU to single step on.
 * @param   fFlags          Flags controlling the stepping, DBGF_STEP_F_XXX.
 *                          Either DBGF_STEP_F_INTO or DBGF_STEP_F_OVER must
 *                          always be specified.
 * @param   pStopPcAddr     Address to stop executing at.  Completely ignored
 *                          unless DBGF_STEP_F_STOP_ON_ADDRESS is specified.
 * @param   pStopPopAddr    Stack address that SP must be lower than when
 *                          performing DBGF_STEP_F_STOP_ON_STACK_POP filtering.
 * @param   cbStopPop       The range starting at @a pStopPopAddr which is
 *                          considered to be within the same thread stack. Note
 *                          that the API allows @a pStopPopAddr and @a cbStopPop
 *                          to form an area that wraps around and it will
 *                          consider the part starting at 0 as included.
 * @param   cMaxSteps       The maximum number of steps to take.  This is to
 *                          prevent stepping for ever, so passing UINT32_MAX is
 *                          not recommended.
 *
 * @remarks The two address arguments must be guest context virtual addresses,
 *          or HMA.  The code doesn't make much of a point of out HMA, though.
 */
VMMR3DECL(int) DBGFR3StepEx(PUVM pUVM, VMCPUID idCpu, uint32_t fFlags, PCDBGFADDRESS pStopPcAddr,
                            PCDBGFADDRESS pStopPopAddr, RTGCUINTPTR cbStopPop, uint32_t cMaxSteps)
{
    /*
     * Check state.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pVM->cCpus, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~DBGF_STEP_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(RT_BOOL(fFlags & DBGF_STEP_F_INTO) != RT_BOOL(fFlags & DBGF_STEP_F_OVER), VERR_INVALID_FLAGS);
    if (fFlags & DBGF_STEP_F_STOP_ON_ADDRESS)
    {
        AssertReturn(RT_VALID_PTR(pStopPcAddr), VERR_INVALID_POINTER);
        AssertReturn(DBGFADDRESS_IS_VALID(pStopPcAddr), VERR_INVALID_PARAMETER);
        AssertReturn(DBGFADDRESS_IS_VIRT_GC(pStopPcAddr), VERR_INVALID_PARAMETER);
    }
    AssertReturn(!(fFlags & DBGF_STEP_F_STOP_ON_STACK_POP) || RT_VALID_PTR(pStopPopAddr), VERR_INVALID_POINTER);
    if (fFlags & DBGF_STEP_F_STOP_ON_STACK_POP)
    {
        AssertReturn(RT_VALID_PTR(pStopPopAddr), VERR_INVALID_POINTER);
        AssertReturn(DBGFADDRESS_IS_VALID(pStopPopAddr), VERR_INVALID_PARAMETER);
        AssertReturn(DBGFADDRESS_IS_VIRT_GC(pStopPopAddr), VERR_INVALID_PARAMETER);
        AssertReturn(cbStopPop > 0, VERR_INVALID_PARAMETER);
    }

    AssertReturn(pVM->dbgf.s.fAttached, VERR_DBGF_NOT_ATTACHED);
    PUVMCPU pUVCpu = &pUVM->aCpus[idCpu];
    if (RT_LIKELY(dbgfR3CpuIsHalted(pUVCpu)))
    { /* likely */ }
    else
        return VERR_SEM_OUT_OF_TURN;
    Assert(pVM->dbgf.s.SteppingFilter.idCpu == NIL_VMCPUID);

    /*
     * Send the emulation thread a single-step command.
     */
    if (fFlags == DBGF_STEP_F_INTO)
        pVM->dbgf.s.SteppingFilter.idCpu = NIL_VMCPUID;
    else
        pVM->dbgf.s.SteppingFilter.idCpu = idCpu;
    pVM->dbgf.s.SteppingFilter.fFlags = fFlags;
    if (fFlags & DBGF_STEP_F_STOP_ON_ADDRESS)
        pVM->dbgf.s.SteppingFilter.AddrPc = pStopPcAddr->FlatPtr;
    else
        pVM->dbgf.s.SteppingFilter.AddrPc = 0;
    if (fFlags & DBGF_STEP_F_STOP_ON_STACK_POP)
    {
        pVM->dbgf.s.SteppingFilter.AddrStackPop = pStopPopAddr->FlatPtr;
        pVM->dbgf.s.SteppingFilter.cbStackPop   = cbStopPop;
    }
    else
    {
        pVM->dbgf.s.SteppingFilter.AddrStackPop = 0;
        pVM->dbgf.s.SteppingFilter.cbStackPop   = RTGCPTR_MAX;
    }

    pVM->dbgf.s.SteppingFilter.cMaxSteps    = cMaxSteps;
    pVM->dbgf.s.SteppingFilter.cSteps       = 0;
    pVM->dbgf.s.SteppingFilter.uCallDepth   = 0;

    Assert(dbgfR3CpuIsHalted(pUVCpu));
    return dbgfR3CpuSetCmdAndNotify(pUVCpu, DBGFCMD_SINGLE_STEP);
}



/**
 * dbgfR3EventConfigEx argument packet.
 */
typedef struct DBGFR3EVENTCONFIGEXARGS
{
    PCDBGFEVENTCONFIG   paConfigs;
    size_t              cConfigs;
    int                 rc;
} DBGFR3EVENTCONFIGEXARGS;
/** Pointer to a dbgfR3EventConfigEx argument packet. */
typedef DBGFR3EVENTCONFIGEXARGS *PDBGFR3EVENTCONFIGEXARGS;


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS, Worker for DBGFR3EventConfigEx.}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3EventConfigEx(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    if (pVCpu->idCpu == 0)
    {
        PDBGFR3EVENTCONFIGEXARGS        pArgs = (PDBGFR3EVENTCONFIGEXARGS)pvUser;
        DBGFEVENTCONFIG volatile const *paConfigs = pArgs->paConfigs;
        size_t                          cConfigs  = pArgs->cConfigs;

        /*
         * Apply the changes.
         */
        unsigned cChanges = 0;
        for (uint32_t i = 0; i < cConfigs; i++)
        {
            DBGFEVENTTYPE enmType = paConfigs[i].enmType;
            AssertReturn(enmType >= DBGFEVENT_FIRST_SELECTABLE && enmType < DBGFEVENT_END, VERR_INVALID_PARAMETER);
            if (paConfigs[i].fEnabled)
                cChanges += ASMAtomicBitTestAndSet(&pVM->dbgf.s.bmSelectedEvents, enmType) == false;
            else
                cChanges += ASMAtomicBitTestAndClear(&pVM->dbgf.s.bmSelectedEvents, enmType) == true;
        }

        /*
         * Inform HM about changes.
         */
        if (cChanges > 0)
        {
            if (HMIsEnabled(pVM))
            {
                HMR3NotifyDebugEventChanged(pVM);
                HMR3NotifyDebugEventChangedPerCpu(pVM, pVCpu);
            }
            else if (VM_IS_NEM_ENABLED(pVM))
            {
                NEMR3NotifyDebugEventChanged(pVM);
                NEMR3NotifyDebugEventChangedPerCpu(pVM, pVCpu);
            }
        }
    }
    else if (HMIsEnabled(pVM))
        HMR3NotifyDebugEventChangedPerCpu(pVM, pVCpu);
    else if (VM_IS_NEM_ENABLED(pVM))
        NEMR3NotifyDebugEventChangedPerCpu(pVM, pVCpu);

    return VINF_SUCCESS;
}


/**
 * Configures (enables/disables) multiple selectable debug events.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   paConfigs   The event to configure and their new state.
 * @param   cConfigs    Number of entries in @a paConfigs.
 */
VMMR3DECL(int) DBGFR3EventConfigEx(PUVM pUVM, PCDBGFEVENTCONFIG paConfigs, size_t cConfigs)
{
    /*
     * Validate input.
     */
    size_t i = cConfigs;
    while (i-- > 0)
    {
        AssertReturn(paConfigs[i].enmType >= DBGFEVENT_FIRST_SELECTABLE, VERR_INVALID_PARAMETER);
        AssertReturn(paConfigs[i].enmType <  DBGFEVENT_END, VERR_INVALID_PARAMETER);
    }
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Apply the changes in EMT(0) and rendezvous with the other CPUs so they
     * can sync their data and execution with new debug state.
     */
    DBGFR3EVENTCONFIGEXARGS Args = { paConfigs, cConfigs, VINF_SUCCESS };
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING | VMMEMTRENDEZVOUS_FLAGS_PRIORITY,
                                dbgfR3EventConfigEx, &Args);
    if (RT_SUCCESS(rc))
        rc = Args.rc;
    return rc;
}


/**
 * Enables or disables a selectable debug event.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   enmEvent    The selectable debug event.
 * @param   fEnabled    The new state.
 */
VMMR3DECL(int) DBGFR3EventConfig(PUVM pUVM, DBGFEVENTTYPE enmEvent, bool fEnabled)
{
    /*
     * Convert to an array call.
     */
    DBGFEVENTCONFIG EvtCfg = { enmEvent, fEnabled };
    return DBGFR3EventConfigEx(pUVM, &EvtCfg, 1);
}


/**
 * Checks if the given selectable event is enabled.
 *
 * @returns true if enabled, false if not or invalid input.
 * @param   pUVM        The user mode VM handle.
 * @param   enmEvent    The selectable debug event.
 * @sa      DBGFR3EventQuery
 */
VMMR3DECL(bool) DBGFR3EventIsEnabled(PUVM pUVM, DBGFEVENTTYPE enmEvent)
{
    /*
     * Validate input.
     */
    AssertReturn(   enmEvent >= DBGFEVENT_HALT_DONE
                 && enmEvent <  DBGFEVENT_END, false);
    Assert(   enmEvent >= DBGFEVENT_FIRST_SELECTABLE
           || enmEvent == DBGFEVENT_BREAKPOINT
           || enmEvent == DBGFEVENT_BREAKPOINT_IO
           || enmEvent == DBGFEVENT_BREAKPOINT_MMIO);

    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);

    /*
     * Check the event status.
     */
    return ASMBitTest(&pVM->dbgf.s.bmSelectedEvents, enmEvent);
}


/**
 * Queries the status of a set of events.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   paConfigs   The events to query and where to return the state.
 * @param   cConfigs    The number of elements in @a paConfigs.
 * @sa      DBGFR3EventIsEnabled, DBGF_IS_EVENT_ENABLED
 */
VMMR3DECL(int) DBGFR3EventQuery(PUVM pUVM, PDBGFEVENTCONFIG paConfigs, size_t cConfigs)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    for (size_t i = 0; i < cConfigs; i++)
    {
        DBGFEVENTTYPE enmType = paConfigs[i].enmType;
        AssertReturn(   enmType >= DBGFEVENT_HALT_DONE
                     && enmType <  DBGFEVENT_END, VERR_INVALID_PARAMETER);
        Assert(   enmType >= DBGFEVENT_FIRST_SELECTABLE
               || enmType == DBGFEVENT_BREAKPOINT
               || enmType == DBGFEVENT_BREAKPOINT_IO
               || enmType == DBGFEVENT_BREAKPOINT_MMIO);
        paConfigs[i].fEnabled = ASMBitTest(&pVM->dbgf.s.bmSelectedEvents, paConfigs[i].enmType);
    }

    return VINF_SUCCESS;
}


/**
 * dbgfR3InterruptConfigEx argument packet.
 */
typedef struct DBGFR3INTERRUPTCONFIGEXARGS
{
    PCDBGFINTERRUPTCONFIG   paConfigs;
    size_t                  cConfigs;
    int                     rc;
} DBGFR3INTERRUPTCONFIGEXARGS;
/** Pointer to a dbgfR3InterruptConfigEx argument packet. */
typedef DBGFR3INTERRUPTCONFIGEXARGS *PDBGFR3INTERRUPTCONFIGEXARGS;

/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS,
 *      Worker for DBGFR3InterruptConfigEx.}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3InterruptConfigEx(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    if (pVCpu->idCpu == 0)
    {
        PDBGFR3INTERRUPTCONFIGEXARGS    pArgs = (PDBGFR3INTERRUPTCONFIGEXARGS)pvUser;
        PCDBGFINTERRUPTCONFIG           paConfigs = pArgs->paConfigs;
        size_t                          cConfigs  = pArgs->cConfigs;

        /*
         * Apply the changes.
         */
        bool fChanged = false;
        bool fThis;
        for (uint32_t i = 0; i < cConfigs; i++)
        {
            /*
             * Hardware interrupts.
             */
            if (paConfigs[i].enmHardState == DBGFINTERRUPTSTATE_ENABLED)
            {
                fChanged |= fThis = ASMAtomicBitTestAndSet(&pVM->dbgf.s.bmHardIntBreakpoints, paConfigs[i].iInterrupt) == false;
                if (fThis)
                {
                    Assert(pVM->dbgf.s.cHardIntBreakpoints < 256);
                    pVM->dbgf.s.cHardIntBreakpoints++;
                }
            }
            else if (paConfigs[i].enmHardState == DBGFINTERRUPTSTATE_DISABLED)
            {
                fChanged |= fThis = ASMAtomicBitTestAndClear(&pVM->dbgf.s.bmHardIntBreakpoints, paConfigs[i].iInterrupt) == true;
                if (fThis)
                {
                    Assert(pVM->dbgf.s.cHardIntBreakpoints > 0);
                    pVM->dbgf.s.cHardIntBreakpoints--;
                }
            }

            /*
             * Software interrupts.
             */
            if (paConfigs[i].enmHardState == DBGFINTERRUPTSTATE_ENABLED)
            {
                fChanged |= fThis = ASMAtomicBitTestAndSet(&pVM->dbgf.s.bmSoftIntBreakpoints, paConfigs[i].iInterrupt) == false;
                if (fThis)
                {
                    Assert(pVM->dbgf.s.cSoftIntBreakpoints < 256);
                    pVM->dbgf.s.cSoftIntBreakpoints++;
                }
            }
            else if (paConfigs[i].enmSoftState == DBGFINTERRUPTSTATE_DISABLED)
            {
                fChanged |= fThis = ASMAtomicBitTestAndClear(&pVM->dbgf.s.bmSoftIntBreakpoints, paConfigs[i].iInterrupt) == true;
                if (fThis)
                {
                    Assert(pVM->dbgf.s.cSoftIntBreakpoints > 0);
                    pVM->dbgf.s.cSoftIntBreakpoints--;
                }
            }
        }

        /*
         * Update the event bitmap entries.
         */
        if (pVM->dbgf.s.cHardIntBreakpoints > 0)
            fChanged |= ASMAtomicBitTestAndSet(&pVM->dbgf.s.bmSelectedEvents, DBGFEVENT_INTERRUPT_HARDWARE) == false;
        else
            fChanged |= ASMAtomicBitTestAndClear(&pVM->dbgf.s.bmSelectedEvents, DBGFEVENT_INTERRUPT_HARDWARE) == true;

        if (pVM->dbgf.s.cSoftIntBreakpoints > 0)
            fChanged |= ASMAtomicBitTestAndSet(&pVM->dbgf.s.bmSelectedEvents, DBGFEVENT_INTERRUPT_SOFTWARE) == false;
        else
            fChanged |= ASMAtomicBitTestAndClear(&pVM->dbgf.s.bmSelectedEvents, DBGFEVENT_INTERRUPT_SOFTWARE) == true;

        /*
         * Inform HM about changes.
         */
        if (fChanged)
        {
            if (HMIsEnabled(pVM))
            {
                HMR3NotifyDebugEventChanged(pVM);
                HMR3NotifyDebugEventChangedPerCpu(pVM, pVCpu);
            }
            else if (VM_IS_NEM_ENABLED(pVM))
            {
                NEMR3NotifyDebugEventChanged(pVM);
                NEMR3NotifyDebugEventChangedPerCpu(pVM, pVCpu);
            }
        }
    }
    else if (HMIsEnabled(pVM))
        HMR3NotifyDebugEventChangedPerCpu(pVM, pVCpu);
    else if (VM_IS_NEM_ENABLED(pVM))
        NEMR3NotifyDebugEventChangedPerCpu(pVM, pVCpu);

    return VINF_SUCCESS;
}


/**
 * Changes
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   paConfigs   The events to query and where to return the state.
 * @param   cConfigs    The number of elements in @a paConfigs.
 * @sa      DBGFR3InterruptConfigHardware, DBGFR3InterruptConfigSoftware
 */
VMMR3DECL(int) DBGFR3InterruptConfigEx(PUVM pUVM, PCDBGFINTERRUPTCONFIG paConfigs, size_t cConfigs)
{
    /*
     * Validate input.
     */
    size_t i = cConfigs;
    while (i-- > 0)
    {
        AssertReturn(paConfigs[i].enmHardState <= DBGFINTERRUPTSTATE_DONT_TOUCH, VERR_INVALID_PARAMETER);
        AssertReturn(paConfigs[i].enmSoftState <= DBGFINTERRUPTSTATE_DONT_TOUCH, VERR_INVALID_PARAMETER);
    }

    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Apply the changes in EMT(0) and rendezvous with the other CPUs so they
     * can sync their data and execution with new debug state.
     */
    DBGFR3INTERRUPTCONFIGEXARGS Args = { paConfigs, cConfigs, VINF_SUCCESS };
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING | VMMEMTRENDEZVOUS_FLAGS_PRIORITY,
                                dbgfR3InterruptConfigEx, &Args);
    if (RT_SUCCESS(rc))
        rc = Args.rc;
    return rc;
}


/**
 * Configures interception of a hardware interrupt.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   iInterrupt  The interrupt number.
 * @param   fEnabled    Whether interception is enabled or not.
 * @sa      DBGFR3InterruptSoftwareConfig, DBGFR3InterruptConfigEx
 */
VMMR3DECL(int) DBGFR3InterruptHardwareConfig(PUVM pUVM, uint8_t iInterrupt, bool fEnabled)
{
    /*
     * Convert to DBGFR3InterruptConfigEx call.
     */
    DBGFINTERRUPTCONFIG IntCfg = { iInterrupt, (uint8_t)fEnabled, DBGFINTERRUPTSTATE_DONT_TOUCH };
    return DBGFR3InterruptConfigEx(pUVM, &IntCfg, 1);
}


/**
 * Configures interception of a software interrupt.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   iInterrupt  The interrupt number.
 * @param   fEnabled    Whether interception is enabled or not.
 * @sa      DBGFR3InterruptHardwareConfig, DBGFR3InterruptConfigEx
 */
VMMR3DECL(int) DBGFR3InterruptSoftwareConfig(PUVM pUVM, uint8_t iInterrupt, bool fEnabled)
{
    /*
     * Convert to DBGFR3InterruptConfigEx call.
     */
    DBGFINTERRUPTCONFIG IntCfg = { iInterrupt, DBGFINTERRUPTSTATE_DONT_TOUCH, (uint8_t)fEnabled };
    return DBGFR3InterruptConfigEx(pUVM, &IntCfg, 1);
}


/**
 * Checks whether interception is enabled for a hardware interrupt.
 *
 * @returns true if enabled, false if not or invalid input.
 * @param   pUVM        The user mode VM handle.
 * @param   iInterrupt  The interrupt number.
 * @sa      DBGFR3InterruptSoftwareIsEnabled, DBGF_IS_HARDWARE_INT_ENABLED,
 *          DBGF_IS_SOFTWARE_INT_ENABLED
 */
VMMR3DECL(int) DBGFR3InterruptHardwareIsEnabled(PUVM pUVM, uint8_t iInterrupt)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);

    /*
     * Check it.
     */
    return ASMBitTest(&pVM->dbgf.s.bmHardIntBreakpoints, iInterrupt);
}


/**
 * Checks whether interception is enabled for a software interrupt.
 *
 * @returns true if enabled, false if not or invalid input.
 * @param   pUVM        The user mode VM handle.
 * @param   iInterrupt  The interrupt number.
 * @sa      DBGFR3InterruptHardwareIsEnabled, DBGF_IS_SOFTWARE_INT_ENABLED,
 *          DBGF_IS_HARDWARE_INT_ENABLED,
 */
VMMR3DECL(int) DBGFR3InterruptSoftwareIsEnabled(PUVM pUVM, uint8_t iInterrupt)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);

    /*
     * Check it.
     */
    return ASMBitTest(&pVM->dbgf.s.bmSoftIntBreakpoints, iInterrupt);
}



/**
 * Call this to single step programmatically.
 *
 * You must pass down the return code to the EM loop! That's
 * where the actual single stepping take place (at least in the
 * current implementation).
 *
 * @returns VINF_EM_DBG_STEP
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @thread  VCpu EMT
 * @internal
 */
VMMR3_INT_DECL(int) DBGFR3PrgStep(PVMCPU pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);

    pVCpu->dbgf.s.fSingleSteppingRaw = true;
    return VINF_EM_DBG_STEP;
}


/**
 * Inject an NMI into a running VM (only VCPU 0!)
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM structure.
 * @param   idCpu   The ID of the CPU to inject the NMI on.
 */
VMMR3DECL(int) DBGFR3InjectNMI(PUVM pUVM, VMCPUID idCpu)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pVM->cCpus, VERR_INVALID_CPU_ID);

    /** @todo Implement generic NMI injection. */
    /** @todo NEM: NMI injection   */
    if (!HMIsEnabled(pVM))
        return VERR_NOT_SUP_BY_NEM;

    VMCPU_FF_SET(pVM->apCpusR3[idCpu], VMCPU_FF_INTERRUPT_NMI);
    return VINF_SUCCESS;
}

