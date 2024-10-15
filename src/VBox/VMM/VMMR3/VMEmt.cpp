/* $Id: VMEmt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VM - Virtual Machine, The Emulation Thread.
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
#define LOG_GROUP LOG_GROUP_VM
#include <VBox/vmm/tm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/tm.h>
#include "VMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
int vmR3EmulationThreadWithId(RTTHREAD hThreadSelf, PUVMCPU pUVCpu, VMCPUID idCpu);


/**
 * The emulation thread main function.
 *
 * @returns Thread exit code.
 * @param   hThreadSelf The handle to the executing thread.
 * @param   pvArgs      Pointer to the user mode per-VCpu structure (UVMPCU).
 */
DECLCALLBACK(int) vmR3EmulationThread(RTTHREAD hThreadSelf, void *pvArgs)
{
    PUVMCPU pUVCpu = (PUVMCPU)pvArgs;
    return vmR3EmulationThreadWithId(hThreadSelf, pUVCpu, pUVCpu->idCpu);
}


/**
 * The emulation thread main function, with Virtual CPU ID for debugging.
 *
 * @returns Thread exit code.
 * @param   hThreadSelf The handle to the executing thread.
 * @param   pUVCpu      Pointer to the user mode per-VCpu structure.
 * @param   idCpu       The virtual CPU ID, for backtrace purposes.
 */
int vmR3EmulationThreadWithId(RTTHREAD hThreadSelf, PUVMCPU pUVCpu, VMCPUID idCpu)
{
    PUVM    pUVM = pUVCpu->pUVM;
    int     rc;
    RT_NOREF_PV(hThreadSelf);

    AssertReleaseMsg(RT_VALID_PTR(pUVM) && pUVM->u32Magic == UVM_MAGIC,
                     ("Invalid arguments to the emulation thread!\n"));

    rc = RTTlsSet(pUVM->vm.s.idxTLS, pUVCpu);
    AssertReleaseMsgRCReturn(rc, ("RTTlsSet %x failed with %Rrc\n", pUVM->vm.s.idxTLS, rc), rc);

    if (   pUVM->pVmm2UserMethods
        && pUVM->pVmm2UserMethods->pfnNotifyEmtInit)
        pUVM->pVmm2UserMethods->pfnNotifyEmtInit(pUVM->pVmm2UserMethods, pUVM, pUVCpu);

    /*
     * The request loop.
     */
    rc = VINF_SUCCESS;
    Log(("vmR3EmulationThread: Emulation thread starting the days work... Thread=%#x pUVM=%p\n", hThreadSelf, pUVM));
    VMSTATE enmBefore = VMSTATE_CREATED; /* (only used for logging atm.) */
    ASMAtomicIncU32(&pUVM->vm.s.cActiveEmts);
    for (;;)
    {
        /*
         * During early init there is no pVM and/or pVCpu, so make a special path
         * for that to keep things clearly separate.
         */
        PVM    pVM   = pUVM->pVM;
        PVMCPU pVCpu = pUVCpu->pVCpu;
        if (!pVCpu || !pVM)
        {
            /*
             * Check for termination first.
             */
            if (pUVM->vm.s.fTerminateEMT)
            {
                rc = VINF_EM_TERMINATE;
                break;
            }

            /*
             * Only the first VCPU may initialize the VM during early init
             * and must therefore service all VMCPUID_ANY requests.
             * See also VMR3Create
             */
            if (    (pUVM->vm.s.pNormalReqs || pUVM->vm.s.pPriorityReqs)
                &&  pUVCpu->idCpu == 0)
            {
                /*
                 * Service execute in any EMT request.
                 */
                rc = VMR3ReqProcessU(pUVM, VMCPUID_ANY, false /*fPriorityOnly*/);
                Log(("vmR3EmulationThread: Req rc=%Rrc, VM state %s -> %s\n", rc, VMR3GetStateName(enmBefore), pUVM->pVM ? VMR3GetStateName(pUVM->pVM->enmVMState) : "CREATING"));
            }
            else if (pUVCpu->vm.s.pNormalReqs || pUVCpu->vm.s.pPriorityReqs)
            {
                /*
                 * Service execute in specific EMT request.
                 */
                rc = VMR3ReqProcessU(pUVM, pUVCpu->idCpu, false /*fPriorityOnly*/);
                Log(("vmR3EmulationThread: Req (cpu=%u) rc=%Rrc, VM state %s -> %s\n", pUVCpu->idCpu, rc, VMR3GetStateName(enmBefore), pUVM->pVM ? VMR3GetStateName(pUVM->pVM->enmVMState) : "CREATING"));
            }
            else
            {
                /*
                 * Nothing important is pending, so wait for something.
                 */
                rc = VMR3WaitU(pUVCpu);
                if (RT_FAILURE(rc))
                {
                    AssertLogRelMsgFailed(("VMR3WaitU failed with %Rrc\n", rc));
                    break;
                }
            }
        }
        else
        {
            /*
             * Pending requests which needs servicing?
             *
             * We check for state changes in addition to status codes when
             * servicing requests. (Look after the ifs.)
             */
            enmBefore = pVM->enmVMState;
            if (pUVM->vm.s.fTerminateEMT)
            {
                rc = VINF_EM_TERMINATE;
                break;
            }

            if (VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS))
            {
                rc = VMMR3EmtRendezvousFF(pVM, pVM->apCpusR3[idCpu]);
                Log(("vmR3EmulationThread: Rendezvous rc=%Rrc, VM state %s -> %s\n", rc, VMR3GetStateName(enmBefore), VMR3GetStateName(pVM->enmVMState)));
            }
            else if (pUVM->vm.s.pNormalReqs || pUVM->vm.s.pPriorityReqs)
            {
                /*
                 * Service execute in any EMT request.
                 */
                rc = VMR3ReqProcessU(pUVM, VMCPUID_ANY, false /*fPriorityOnly*/);
                Log(("vmR3EmulationThread: Req rc=%Rrc, VM state %s -> %s\n", rc, VMR3GetStateName(enmBefore), VMR3GetStateName(pVM->enmVMState)));
            }
            else if (pUVCpu->vm.s.pNormalReqs || pUVCpu->vm.s.pPriorityReqs)
            {
                /*
                 * Service execute in specific EMT request.
                 */
                rc = VMR3ReqProcessU(pUVM, pUVCpu->idCpu, false /*fPriorityOnly*/);
                Log(("vmR3EmulationThread: Req (cpu=%u) rc=%Rrc, VM state %s -> %s\n", pUVCpu->idCpu, rc, VMR3GetStateName(enmBefore), VMR3GetStateName(pVM->enmVMState)));
            }
            else if (   VM_FF_IS_SET(pVM, VM_FF_DBGF)
                     || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_DBGF))
            {
                /*
                 * Service the debugger request.
                 */
                rc = DBGFR3VMMForcedAction(pVM, pVCpu);
                Log(("vmR3EmulationThread: Dbg rc=%Rrc, VM state %s -> %s\n", rc, VMR3GetStateName(enmBefore), VMR3GetStateName(pVM->enmVMState)));
            }
            else if (VM_FF_TEST_AND_CLEAR(pVM, VM_FF_RESET))
            {
                /*
                 * Service a delayed reset request.
                 */
                rc = VBOXSTRICTRC_VAL(VMR3ResetFF(pVM));
                VM_FF_CLEAR(pVM, VM_FF_RESET);
                Log(("vmR3EmulationThread: Reset rc=%Rrc, VM state %s -> %s\n", rc, VMR3GetStateName(enmBefore), VMR3GetStateName(pVM->enmVMState)));
            }
            else
            {
                /*
                 * Nothing important is pending, so wait for something.
                 */
                rc = VMR3WaitU(pUVCpu);
                if (RT_FAILURE(rc))
                {
                    AssertLogRelMsgFailed(("VMR3WaitU failed with %Rrc\n", rc));
                    break;
                }
            }

            /*
             * Check for termination requests, these have extremely high priority.
             */
            if (    rc == VINF_EM_TERMINATE
                ||  pUVM->vm.s.fTerminateEMT)
                break;
        }

        /*
         * Some requests (both VMR3Req* and the DBGF) can potentially resume
         * or start the VM, in that case we'll get a change in VM status
         * indicating that we're now running.
         */
        if (RT_SUCCESS(rc))
        {
            pVM = pUVM->pVM;
            if (pVM)
            {
                pVCpu = pVM->apCpusR3[idCpu];
                if (   pVM->enmVMState == VMSTATE_RUNNING
                    && VMCPUSTATE_IS_STARTED(VMCPU_GET_STATE(pVCpu)))
                {
                    rc = EMR3ExecuteVM(pVM, pVCpu);
                    Log(("vmR3EmulationThread: EMR3ExecuteVM() -> rc=%Rrc, enmVMState=%d\n", rc, pVM->enmVMState));
                }
            }
        }

    } /* forever */


    /*
     * Decrement the active EMT count if we haven't done it yet in vmR3Destroy.
     */
    if (!pUVCpu->vm.s.fBeenThruVmDestroy)
        ASMAtomicDecU32(&pUVM->vm.s.cActiveEmts);


    /*
     * Cleanup and exit.
     * EMT0 does the VM destruction after all other EMTs have deregistered and terminated.
     */
    Log(("vmR3EmulationThread: Terminating emulation thread! Thread=%#x pUVM=%p rc=%Rrc enmBefore=%d enmVMState=%d\n",
         hThreadSelf, pUVM, rc, enmBefore, pUVM->pVM ? pUVM->pVM->enmVMState : VMSTATE_TERMINATED));
    PVM pVM;
    if (   idCpu == 0
        && (pVM = pUVM->pVM) != NULL)
    {
        /* Wait for any other EMTs to terminate before we destroy the VM (see vmR3DestroyVM). */
        for (VMCPUID iCpu = 1; iCpu < pUVM->cCpus; iCpu++)
        {
            RTTHREAD hThread;
            ASMAtomicXchgHandle(&pUVM->aCpus[iCpu].vm.s.ThreadEMT, NIL_RTTHREAD, &hThread);
            if (hThread != NIL_RTTHREAD)
            {
                int rc2 = RTThreadWait(hThread, 5 * RT_MS_1SEC, NULL);
                AssertLogRelMsgRC(rc2, ("iCpu=%u rc=%Rrc\n", iCpu, rc2));
                if (RT_FAILURE(rc2))
                    pUVM->aCpus[iCpu].vm.s.ThreadEMT = hThread;
            }
        }

        /* Switch to the terminated state, clearing the VM pointer and finally destroy the VM. */
        vmR3SetTerminated(pVM);

        pUVM->pVM = NULL;
        for (VMCPUID iCpu = 0; iCpu < pUVM->cCpus; iCpu++)
        {
            pUVM->aCpus[iCpu].pVM   = NULL;
            pUVM->aCpus[iCpu].pVCpu = NULL;
        }

        int rc2 = GVMMR3DestroyVM(pUVM, pVM);
        AssertLogRelRC(rc2);
    }
    /* Deregister the EMT with VMMR0. */
    else if (   idCpu != 0
             && (pVM = pUVM->pVM) != NULL)
    {
        int rc2 = GVMMR3DeregisterVCpu(pVM, idCpu);
        AssertLogRelRC(rc2);
    }

    if (   pUVM->pVmm2UserMethods
        && pUVM->pVmm2UserMethods->pfnNotifyEmtTerm)
        pUVM->pVmm2UserMethods->pfnNotifyEmtTerm(pUVM->pVmm2UserMethods, pUVM, pUVCpu);

    pUVCpu->vm.s.NativeThreadEMT = NIL_RTNATIVETHREAD;
    Log(("vmR3EmulationThread: EMT is terminated.\n"));
    return rc;
}


/**
 * Gets the name of a halt method.
 *
 * @returns Pointer to a read only string.
 * @param   enmMethod   The method.
 */
static const char *vmR3GetHaltMethodName(VMHALTMETHOD enmMethod)
{
    switch (enmMethod)
    {
        case VMHALTMETHOD_BOOTSTRAP:    return "bootstrap";
        case VMHALTMETHOD_DEFAULT:      return "default";
        case VMHALTMETHOD_OLD:          return "old";
        case VMHALTMETHOD_1:            return "method1";
        //case VMHALTMETHOD_2:            return "method2";
        case VMHALTMETHOD_GLOBAL_1:     return "global1";
        default:                        return "unknown";
    }
}


/**
 * Signal a fatal wait error.
 *
 * @returns Fatal error code to be propagated up the call stack.
 * @param   pUVCpu              The user mode per CPU structure of the calling
 *                              EMT.
 * @param   pszFmt              The error format with a single %Rrc in it.
 * @param   rcFmt               The status code to format.
 */
static int vmR3FatalWaitError(PUVMCPU pUVCpu, const char *pszFmt, int rcFmt)
{
    /** @todo This is wrong ... raise a fatal error / guru meditation
     *        instead. */
    AssertLogRelMsgFailed((pszFmt, rcFmt));
    ASMAtomicUoWriteBool(&pUVCpu->pUVM->vm.s.fTerminateEMT, true);
    if (pUVCpu->pVM)
        VM_FF_SET(pUVCpu->pVM, VM_FF_CHECK_VM_STATE);
    return VERR_VM_FATAL_WAIT_ERROR;
}


/**
 * The old halt loop.
 */
static DECLCALLBACK(int) vmR3HaltOldDoHalt(PUVMCPU pUVCpu, const uint32_t fMask, uint64_t /* u64Now*/)
{
    /*
     * Halt loop.
     */
    PVM    pVM   = pUVCpu->pVM;
    PVMCPU pVCpu = pUVCpu->pVCpu;

    int rc = VINF_SUCCESS;
    ASMAtomicWriteBool(&pUVCpu->vm.s.fWait, true);
    //unsigned cLoops = 0;
    for (;;)
    {
        /*
         * Work the timers and check if we can exit.
         * The poll call gives us the ticks left to the next event in
         * addition to perhaps set an FF.
         */
        uint64_t const u64StartTimers   = RTTimeNanoTS();
        TMR3TimerQueuesDo(pVM);
        uint64_t const cNsElapsedTimers = RTTimeNanoTS() - u64StartTimers;
        STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltTimers, cNsElapsedTimers);
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_HALTED_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, fMask))
            break;
        uint64_t u64NanoTS;
        TMTimerPollGIP(pVM, pVCpu, &u64NanoTS);
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_HALTED_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, fMask))
            break;

        /*
         * Wait for a while. Someone will wake us up or interrupt the call if
         * anything needs our attention.
         */
        if (u64NanoTS < 50000)
        {
            //RTLogPrintf("u64NanoTS=%RI64 cLoops=%d spin\n", u64NanoTS, cLoops++);
            /* spin */;
        }
        else
        {
            VMMR3YieldStop(pVM);
            //uint64_t u64Start = RTTimeNanoTS();
            if (u64NanoTS <  870000) /* this is a bit speculative... works fine on linux. */
            {
                //RTLogPrintf("u64NanoTS=%RI64 cLoops=%d yield", u64NanoTS, cLoops++);
                uint64_t const u64StartSchedYield   = RTTimeNanoTS();
                RTThreadYield(); /* this is the best we can do here */
                uint64_t const cNsElapsedSchedYield = RTTimeNanoTS() - u64StartSchedYield;
                STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltYield, cNsElapsedSchedYield);
            }
            else if (u64NanoTS < 2000000)
            {
                //RTLogPrintf("u64NanoTS=%RI64 cLoops=%d sleep 1ms", u64NanoTS, cLoops++);
                uint64_t const u64StartSchedHalt   = RTTimeNanoTS();
                rc = RTSemEventWait(pUVCpu->vm.s.EventSemWait, 1);
                uint64_t const cNsElapsedSchedHalt = RTTimeNanoTS() - u64StartSchedHalt;
                STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltBlock, cNsElapsedSchedHalt);
            }
            else
            {
                //RTLogPrintf("u64NanoTS=%RI64 cLoops=%d sleep %dms", u64NanoTS, cLoops++, (uint32_t)RT_MIN((u64NanoTS - 500000) / 1000000, 15));
                uint64_t const u64StartSchedHalt   = RTTimeNanoTS();
                rc = RTSemEventWait(pUVCpu->vm.s.EventSemWait, RT_MIN((u64NanoTS - 1000000) / 1000000, 15));
                uint64_t const cNsElapsedSchedHalt = RTTimeNanoTS() - u64StartSchedHalt;
                STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltBlock, cNsElapsedSchedHalt);
            }
            //uint64_t u64Slept = RTTimeNanoTS() - u64Start;
            //RTLogPrintf(" -> rc=%Rrc in %RU64 ns / %RI64 ns delta\n", rc, u64Slept, u64NanoTS - u64Slept);
        }
        if (rc == VERR_TIMEOUT)
            rc = VINF_SUCCESS;
        else if (RT_FAILURE(rc))
        {
            rc = vmR3FatalWaitError(pUVCpu, "RTSemEventWait->%Rrc\n", rc);
            break;
        }
    }

    ASMAtomicUoWriteBool(&pUVCpu->vm.s.fWait, false);
    return rc;
}


/**
 * Initialize the configuration of halt method 1 & 2.
 *
 * @return VBox status code. Failure on invalid CFGM data.
 * @param   pUVM        The user mode VM structure.
 */
static int vmR3HaltMethod12ReadConfigU(PUVM pUVM)
{
    /*
     * The defaults.
     */
#if 1 /* DEBUGGING STUFF - REMOVE LATER */
    pUVM->vm.s.Halt.Method12.u32LagBlockIntervalDivisorCfg = 4;
    pUVM->vm.s.Halt.Method12.u32MinBlockIntervalCfg =   2*1000000;
    pUVM->vm.s.Halt.Method12.u32MaxBlockIntervalCfg =  75*1000000;
    pUVM->vm.s.Halt.Method12.u32StartSpinningCfg    =  30*1000000;
    pUVM->vm.s.Halt.Method12.u32StopSpinningCfg     =  20*1000000;
#else
    pUVM->vm.s.Halt.Method12.u32LagBlockIntervalDivisorCfg = 4;
    pUVM->vm.s.Halt.Method12.u32MinBlockIntervalCfg =   5*1000000;
    pUVM->vm.s.Halt.Method12.u32MaxBlockIntervalCfg = 200*1000000;
    pUVM->vm.s.Halt.Method12.u32StartSpinningCfg    =  20*1000000;
    pUVM->vm.s.Halt.Method12.u32StopSpinningCfg     =   2*1000000;
#endif

    /*
     * Query overrides.
     *
     * I don't have time to bother with niceties such as invalid value checks
     * here right now. sorry.
     */
    PCFGMNODE pCfg = CFGMR3GetChild(CFGMR3GetRoot(pUVM->pVM), "/VMM/HaltedMethod1");
    if (pCfg)
    {
        uint32_t u32;
        if (RT_SUCCESS(CFGMR3QueryU32(pCfg, "LagBlockIntervalDivisor", &u32)))
            pUVM->vm.s.Halt.Method12.u32LagBlockIntervalDivisorCfg = u32;
        if (RT_SUCCESS(CFGMR3QueryU32(pCfg, "MinBlockInterval", &u32)))
            pUVM->vm.s.Halt.Method12.u32MinBlockIntervalCfg = u32;
        if (RT_SUCCESS(CFGMR3QueryU32(pCfg, "MaxBlockInterval", &u32)))
            pUVM->vm.s.Halt.Method12.u32MaxBlockIntervalCfg = u32;
        if (RT_SUCCESS(CFGMR3QueryU32(pCfg, "StartSpinning", &u32)))
            pUVM->vm.s.Halt.Method12.u32StartSpinningCfg = u32;
        if (RT_SUCCESS(CFGMR3QueryU32(pCfg, "StopSpinning", &u32)))
            pUVM->vm.s.Halt.Method12.u32StopSpinningCfg = u32;
        LogRel(("VMEmt: HaltedMethod1 config: %d/%d/%d/%d/%d\n",
                pUVM->vm.s.Halt.Method12.u32LagBlockIntervalDivisorCfg,
                pUVM->vm.s.Halt.Method12.u32MinBlockIntervalCfg,
                pUVM->vm.s.Halt.Method12.u32MaxBlockIntervalCfg,
                pUVM->vm.s.Halt.Method12.u32StartSpinningCfg,
                pUVM->vm.s.Halt.Method12.u32StopSpinningCfg));
    }

    return VINF_SUCCESS;
}


/**
 * Initialize halt method 1.
 *
 * @return VBox status code.
 * @param   pUVM            Pointer to the user mode VM structure.
 */
static DECLCALLBACK(int) vmR3HaltMethod1Init(PUVM pUVM)
{
    return vmR3HaltMethod12ReadConfigU(pUVM);
}


/**
 * Method 1 - Block whenever possible, and when lagging behind
 * switch to spinning for 10-30ms with occasional blocking until
 * the lag has been eliminated.
 */
static DECLCALLBACK(int) vmR3HaltMethod1Halt(PUVMCPU pUVCpu, const uint32_t fMask, uint64_t u64Now)
{
    PUVM    pUVM    = pUVCpu->pUVM;
    PVMCPU  pVCpu   = pUVCpu->pVCpu;
    PVM     pVM     = pUVCpu->pVM;

    /*
     * To simplify things, we decide up-front whether we should switch to spinning or
     * not. This makes some ASSUMPTIONS about the cause of the spinning (PIT/RTC/PCNet)
     * and that it will generate interrupts or other events that will cause us to exit
     * the halt loop.
     */
    bool fBlockOnce = false;
    bool fSpinning = false;
    uint32_t u32CatchUpPct = TMVirtualSyncGetCatchUpPct(pVM);
    if (u32CatchUpPct /* non-zero if catching up */)
    {
        if (pUVCpu->vm.s.Halt.Method12.u64StartSpinTS)
        {
            fSpinning = TMVirtualSyncGetLag(pVM) >= pUVM->vm.s.Halt.Method12.u32StopSpinningCfg;
            if (fSpinning)
            {
                uint64_t u64Lag = TMVirtualSyncGetLag(pVM);
                fBlockOnce = u64Now - pUVCpu->vm.s.Halt.Method12.u64LastBlockTS
                           > RT_MAX(pUVM->vm.s.Halt.Method12.u32MinBlockIntervalCfg,
                                    RT_MIN(u64Lag / pUVM->vm.s.Halt.Method12.u32LagBlockIntervalDivisorCfg,
                                           pUVM->vm.s.Halt.Method12.u32MaxBlockIntervalCfg));
            }
            else
            {
                //RTLogRelPrintf("Stopped spinning (%u ms)\n", (u64Now - pUVCpu->vm.s.Halt.Method12.u64StartSpinTS) / 1000000);
                pUVCpu->vm.s.Halt.Method12.u64StartSpinTS = 0;
            }
        }
        else
        {
            fSpinning = TMVirtualSyncGetLag(pVM) >= pUVM->vm.s.Halt.Method12.u32StartSpinningCfg;
            if (fSpinning)
                pUVCpu->vm.s.Halt.Method12.u64StartSpinTS = u64Now;
        }
    }
    else if (pUVCpu->vm.s.Halt.Method12.u64StartSpinTS)
    {
        //RTLogRelPrintf("Stopped spinning (%u ms)\n", (u64Now - pUVCpu->vm.s.Halt.Method12.u64StartSpinTS) / 1000000);
        pUVCpu->vm.s.Halt.Method12.u64StartSpinTS = 0;
    }

#if defined(VBOX_VMM_TARGET_ARMV8)
    uint64_t cNsVTimerActivate = TMCpuGetVTimerActivationNano(pVCpu);
    const bool fVTimerActive = cNsVTimerActivate != UINT64_MAX;
#endif

    /*
     * Halt loop.
     */
    int rc = VINF_SUCCESS;
    ASMAtomicWriteBool(&pUVCpu->vm.s.fWait, true);
    unsigned cLoops = 0;
    for (;; cLoops++)
    {
        /*
         * Work the timers and check if we can exit.
         */
        uint64_t const u64StartTimers   = RTTimeNanoTS();
        TMR3TimerQueuesDo(pVM);
        uint64_t const cNsElapsedTimers = RTTimeNanoTS() - u64StartTimers;
        STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltTimers, cNsElapsedTimers);
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_HALTED_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, fMask)
#if defined(VBOX_VMM_TARGET_ARMV8)
            ||  cNsElapsedTimers >= cNsVTimerActivate
#endif
            )
        {
#if defined(VBOX_VMM_TARGET_ARMV8)
            cNsVTimerActivate = 0;
#endif
            break;
        }

#if defined(VBOX_VMM_TARGET_ARMV8)
        cNsVTimerActivate -= cNsElapsedTimers;
#endif

        /*
         * Estimate time left to the next event.
         */
        uint64_t u64NanoTS;
        TMTimerPollGIP(pVM, pVCpu, &u64NanoTS);
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_HALTED_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, fMask))
            break;

#if defined(VBOX_VMM_TARGET_ARMV8)
        u64NanoTS = RT_MIN(cNsVTimerActivate, u64NanoTS);
#endif

        /*
         * Block if we're not spinning and the interval isn't all that small.
         */
        if (    (   !fSpinning
                 || fBlockOnce)
#if 1 /* DEBUGGING STUFF - REMOVE LATER */
            &&  u64NanoTS >= 100000) /* 0.100 ms */
#else
            &&  u64NanoTS >= 250000) /* 0.250 ms */
#endif
        {
            const uint64_t Start = pUVCpu->vm.s.Halt.Method12.u64LastBlockTS = RTTimeNanoTS();
            VMMR3YieldStop(pVM);

            uint32_t cMilliSecs = RT_MIN(u64NanoTS / RT_NS_1MS, 15);
            if (cMilliSecs <= pUVCpu->vm.s.Halt.Method12.cNSBlockedTooLongAvg / RT_NS_1MS)
                cMilliSecs = 1;
            else
                cMilliSecs -= pUVCpu->vm.s.Halt.Method12.cNSBlockedTooLongAvg / RT_NS_1MS;

            //RTLogRelPrintf("u64NanoTS=%RI64 cLoops=%3d sleep %02dms (%7RU64) ", u64NanoTS, cLoops, cMilliSecs, u64NanoTS);
            uint64_t const u64StartSchedHalt   = RTTimeNanoTS();
            rc = RTSemEventWait(pUVCpu->vm.s.EventSemWait, cMilliSecs);
            uint64_t const cNsElapsedSchedHalt = RTTimeNanoTS() - u64StartSchedHalt;
            STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltBlock, cNsElapsedSchedHalt);

            if (rc == VERR_TIMEOUT)
                rc = VINF_SUCCESS;
            else if (RT_FAILURE(rc))
            {
                rc = vmR3FatalWaitError(pUVCpu, "RTSemEventWait->%Rrc\n", rc);
                break;
            }

            /*
             * Calc the statistics.
             * Update averages every 16th time, and flush parts of the history every 64th time.
             */
            const uint64_t Elapsed = RTTimeNanoTS() - Start;
            pUVCpu->vm.s.Halt.Method12.cNSBlocked += Elapsed;
            if (Elapsed > u64NanoTS)
                pUVCpu->vm.s.Halt.Method12.cNSBlockedTooLong += Elapsed - u64NanoTS;
            pUVCpu->vm.s.Halt.Method12.cBlocks++;
            if (!(pUVCpu->vm.s.Halt.Method12.cBlocks & 0xf))
            {
                pUVCpu->vm.s.Halt.Method12.cNSBlockedTooLongAvg = pUVCpu->vm.s.Halt.Method12.cNSBlockedTooLong / pUVCpu->vm.s.Halt.Method12.cBlocks;
                if (!(pUVCpu->vm.s.Halt.Method12.cBlocks & 0x3f))
                {
                    pUVCpu->vm.s.Halt.Method12.cNSBlockedTooLong = pUVCpu->vm.s.Halt.Method12.cNSBlockedTooLongAvg * 0x40;
                    pUVCpu->vm.s.Halt.Method12.cBlocks = 0x40;
                }
            }
            //RTLogRelPrintf(" -> %7RU64 ns / %7RI64 ns delta%s\n", Elapsed, Elapsed - u64NanoTS, fBlockOnce ? " (block once)" : "");

            /*
             * Clear the block once flag if we actually blocked.
             */
            if (    fBlockOnce
                &&  Elapsed > 100000 /* 0.1 ms */)
                fBlockOnce = false;

#if defined(VBOX_VMM_TARGET_ARMV8)
            cNsVTimerActivate -= RT_MIN(cNsVTimerActivate, Elapsed);
            /* Did the vTimer expire? */
            if (!cNsVTimerActivate)
                break;
#endif
        }
    }
    //if (fSpinning) RTLogRelPrintf("spun for %RU64 ns %u loops; lag=%RU64 pct=%d\n", RTTimeNanoTS() - u64Now, cLoops, TMVirtualSyncGetLag(pVM), u32CatchUpPct);

#if defined(VBOX_VMM_TARGET_ARMV8)
    if (fVTimerActive)
    {
        if (!cNsVTimerActivate)
            VMCPU_FF_SET(pVCpu, VMCPU_FF_VTIMER_ACTIVATED);

        TMCpuSetVTimerNextActivation(pVCpu, cNsVTimerActivate);
    }
#endif
    ASMAtomicUoWriteBool(&pUVCpu->vm.s.fWait, false);
    return rc;
}


/**
 * Initialize the global 1 halt method.
 *
 * @return VBox status code.
 * @param   pUVM            Pointer to the user mode VM structure.
 */
static DECLCALLBACK(int) vmR3HaltGlobal1Init(PUVM pUVM)
{
    /*
     * The defaults.
     */
    uint32_t cNsResolution = SUPSemEventMultiGetResolution(pUVM->vm.s.pSession);
    if (cNsResolution > 5*RT_NS_100US)
        pUVM->vm.s.Halt.Global1.cNsSpinBlockThresholdCfg = 50000;
    else if (cNsResolution > RT_NS_100US)
        pUVM->vm.s.Halt.Global1.cNsSpinBlockThresholdCfg = cNsResolution / 4;
    else
        pUVM->vm.s.Halt.Global1.cNsSpinBlockThresholdCfg = 2000;

    /*
     * Query overrides.
     *
     * I don't have time to bother with niceties such as invalid value checks
     * here right now. sorry.
     */
    PCFGMNODE pCfg = CFGMR3GetChild(CFGMR3GetRoot(pUVM->pVM), "/VMM/HaltedGlobal1");
    if (pCfg)
    {
        uint32_t u32;
        if (RT_SUCCESS(CFGMR3QueryU32(pCfg, "SpinBlockThreshold", &u32)))
            pUVM->vm.s.Halt.Global1.cNsSpinBlockThresholdCfg = u32;
    }
    LogRel(("VMEmt: HaltedGlobal1 config: cNsSpinBlockThresholdCfg=%u\n",
            pUVM->vm.s.Halt.Global1.cNsSpinBlockThresholdCfg));
    return VINF_SUCCESS;
}


/**
 * The global 1 halt method - Block in GMM (ring-0) and let it
 * try take care of the global scheduling of EMT threads.
 */
static DECLCALLBACK(int) vmR3HaltGlobal1Halt(PUVMCPU pUVCpu, const uint32_t fMask, uint64_t u64Now)
{
    PUVM    pUVM  = pUVCpu->pUVM;
    PVMCPU  pVCpu = pUVCpu->pVCpu;
    PVM     pVM   = pUVCpu->pVM;
    Assert(VMMGetCpu(pVM) == pVCpu);
    NOREF(u64Now);

    /*
     * Halt loop.
     */
    //uint64_t u64NowLog, u64Start;
    //u64Start = u64NowLog = RTTimeNanoTS();
    int rc = VINF_SUCCESS;
    ASMAtomicWriteBool(&pUVCpu->vm.s.fWait, true);
    unsigned cLoops = 0;
    for (;; cLoops++)
    {
        /*
         * Work the timers and check if we can exit.
         */
        uint64_t const u64StartTimers   = RTTimeNanoTS();
        TMR3TimerQueuesDo(pVM);
        uint64_t const cNsElapsedTimers = RTTimeNanoTS() - u64StartTimers;
        STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltTimers, cNsElapsedTimers);
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_HALTED_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, fMask))
            break;

        /*
         * Estimate time left to the next event.
         */
        //u64NowLog = RTTimeNanoTS();
        uint64_t u64Delta;
        uint64_t u64GipTime = TMTimerPollGIP(pVM, pVCpu, &u64Delta);
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_HALTED_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, fMask))
            break;

        /*
         * Block if we're not spinning and the interval isn't all that small.
         */
        if (u64Delta >= pUVM->vm.s.Halt.Global1.cNsSpinBlockThresholdCfg)
        {
            VMMR3YieldStop(pVM);
            if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_HALTED_MASK)
                ||  VMCPU_FF_IS_ANY_SET(pVCpu, fMask))
                break;

            //RTLogPrintf("loop=%-3d  u64GipTime=%'llu / %'llu   now=%'llu / %'llu\n", cLoops, u64GipTime, u64Delta, u64NowLog, u64GipTime - u64NowLog);
            uint64_t const u64StartSchedHalt   = RTTimeNanoTS();
            rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), pVCpu->idCpu, VMMR0_DO_GVMM_SCHED_HALT, u64GipTime, NULL);
            uint64_t const u64EndSchedHalt     = RTTimeNanoTS();
            uint64_t const cNsElapsedSchedHalt = u64EndSchedHalt - u64StartSchedHalt;
            STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltBlock, cNsElapsedSchedHalt);

            if (rc == VERR_INTERRUPTED)
                rc = VINF_SUCCESS;
            else if (RT_FAILURE(rc))
            {
                rc = vmR3FatalWaitError(pUVCpu, "vmR3HaltGlobal1Halt: VMMR0_DO_GVMM_SCHED_HALT->%Rrc\n", rc);
                break;
            }
            else
            {
                int64_t const cNsOverslept = u64EndSchedHalt - u64GipTime;
                if (cNsOverslept > 50000)
                    STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltBlockOverslept, cNsOverslept);
                else if (cNsOverslept < -50000)
                    STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltBlockInsomnia,  cNsElapsedSchedHalt);
                else
                    STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltBlockOnTime,    cNsElapsedSchedHalt);
            }
        }
        /*
         * When spinning call upon the GVMM and do some wakups once
         * in a while, it's not like we're actually busy or anything.
         */
        else if (!(cLoops & 0x1fff))
        {
            uint64_t const u64StartSchedYield   = RTTimeNanoTS();
            rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), pVCpu->idCpu, VMMR0_DO_GVMM_SCHED_POLL, false /* don't yield */, NULL);
            uint64_t const cNsElapsedSchedYield = RTTimeNanoTS() - u64StartSchedYield;
            STAM_REL_PROFILE_ADD_PERIOD(&pUVCpu->vm.s.StatHaltYield, cNsElapsedSchedYield);
        }
    }
    //RTLogPrintf("*** %u loops %'llu;  lag=%RU64\n", cLoops, u64NowLog - u64Start, TMVirtualSyncGetLag(pVM));

    ASMAtomicUoWriteBool(&pUVCpu->vm.s.fWait, false);
    return rc;
}


/**
 * The global 1 halt method - VMR3Wait() worker.
 *
 * @returns VBox status code.
 * @param   pUVCpu            Pointer to the user mode VMCPU structure.
 */
static DECLCALLBACK(int) vmR3HaltGlobal1Wait(PUVMCPU pUVCpu)
{
    ASMAtomicWriteBool(&pUVCpu->vm.s.fWait, true);

    PVM    pVM   = pUVCpu->pUVM->pVM;
    PVMCPU pVCpu = VMMGetCpu(pVM);
    Assert(pVCpu->idCpu == pUVCpu->idCpu);

    int rc = VINF_SUCCESS;
    for (;;)
    {
        /*
         * Check Relevant FFs.
         */
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_SUSPENDED_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_EXTERNAL_SUSPENDED_MASK))
            break;

        /*
         * Wait for a while. Someone will wake us up or interrupt the call if
         * anything needs our attention.
         */
        rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), pVCpu->idCpu, VMMR0_DO_GVMM_SCHED_HALT, RTTimeNanoTS() + 1000000000 /* +1s */, NULL);
        if (rc == VERR_INTERRUPTED)
            rc = VINF_SUCCESS;
        else if (RT_FAILURE(rc))
        {
            rc = vmR3FatalWaitError(pUVCpu, "vmR3HaltGlobal1Wait: VMMR0_DO_GVMM_SCHED_HALT->%Rrc\n", rc);
            break;
        }
    }

    ASMAtomicUoWriteBool(&pUVCpu->vm.s.fWait, false);
    return rc;
}


/**
 * The global 1 halt method - VMR3NotifyFF() worker.
 *
 * @param   pUVCpu          Pointer to the user mode VMCPU structure.
 * @param   fFlags          Notification flags, VMNOTIFYFF_FLAGS_*.
 */
static DECLCALLBACK(void) vmR3HaltGlobal1NotifyCpuFF(PUVMCPU pUVCpu, uint32_t fFlags)
{
    /*
     * With ring-0 halting, the fWait flag isn't set, so we have to check the
     * CPU state to figure out whether to do a wakeup call.
     */
    PVMCPU pVCpu = pUVCpu->pVCpu;
    if (pVCpu)
    {
        VMCPUSTATE enmState = VMCPU_GET_STATE(pVCpu);
        if (enmState == VMCPUSTATE_STARTED_HALTED || pUVCpu->vm.s.fWait)
        {
            int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pUVCpu->pVM), pUVCpu->idCpu, VMMR0_DO_GVMM_SCHED_WAKE_UP, 0, NULL);
            AssertRC(rc);

        }
        else if (   (fFlags & VMNOTIFYFF_FLAGS_POKE)
                 || !(fFlags & VMNOTIFYFF_FLAGS_DONE_REM))
        {
            if (enmState == VMCPUSTATE_STARTED_EXEC)
            {
                if (fFlags & VMNOTIFYFF_FLAGS_POKE)
                {
                    int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pUVCpu->pVM), pUVCpu->idCpu, VMMR0_DO_GVMM_SCHED_POKE, 0, NULL);
                    AssertRC(rc);
                }
            }
            else if (   enmState == VMCPUSTATE_STARTED_EXEC_NEM
                     || enmState == VMCPUSTATE_STARTED_EXEC_NEM_WAIT)
                NEMR3NotifyFF(pUVCpu->pVM, pVCpu, fFlags);
        }
    }
    /* This probably makes little sense: */
    else if (pUVCpu->vm.s.fWait)
    {
        int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pUVCpu->pVM), pUVCpu->idCpu, VMMR0_DO_GVMM_SCHED_WAKE_UP, 0, NULL);
        AssertRC(rc);
    }
}


/**
 * Bootstrap VMR3Wait() worker.
 *
 * @returns VBox status code.
 * @param   pUVCpu      Pointer to the user mode VMCPU structure.
 */
static DECLCALLBACK(int) vmR3BootstrapWait(PUVMCPU pUVCpu)
{
    PUVM pUVM = pUVCpu->pUVM;

    ASMAtomicWriteBool(&pUVCpu->vm.s.fWait, true);

    int rc = VINF_SUCCESS;
    for (;;)
    {
        /*
         * Check Relevant FFs.
         */
        if (pUVM->vm.s.pNormalReqs   || pUVM->vm.s.pPriorityReqs)   /* global requests pending? */
            break;
        if (pUVCpu->vm.s.pNormalReqs || pUVCpu->vm.s.pPriorityReqs) /* local requests pending? */
            break;

        if (    pUVCpu->pVM
            &&  (   VM_FF_IS_ANY_SET(pUVCpu->pVM, VM_FF_EXTERNAL_SUSPENDED_MASK)
                 || VMCPU_FF_IS_ANY_SET(VMMGetCpu(pUVCpu->pVM), VMCPU_FF_EXTERNAL_SUSPENDED_MASK)
                )
            )
            break;
        if (pUVM->vm.s.fTerminateEMT)
            break;

        /*
         * Wait for a while. Someone will wake us up or interrupt the call if
         * anything needs our attention.
         */
        rc = RTSemEventWait(pUVCpu->vm.s.EventSemWait, 1000);
        if (rc == VERR_TIMEOUT)
            rc = VINF_SUCCESS;
        else if (RT_FAILURE(rc))
        {
            rc = vmR3FatalWaitError(pUVCpu, "RTSemEventWait->%Rrc\n", rc);
            break;
        }
    }

    ASMAtomicUoWriteBool(&pUVCpu->vm.s.fWait, false);
    return rc;
}


/**
 * Bootstrap VMR3NotifyFF() worker.
 *
 * @param   pUVCpu          Pointer to the user mode VMCPU structure.
 * @param   fFlags          Notification flags, VMNOTIFYFF_FLAGS_*.
 */
static DECLCALLBACK(void) vmR3BootstrapNotifyCpuFF(PUVMCPU pUVCpu, uint32_t fFlags)
{
    if (pUVCpu->vm.s.fWait)
    {
        int rc = RTSemEventSignal(pUVCpu->vm.s.EventSemWait);
        AssertRC(rc);
    }
    NOREF(fFlags);
}


/**
 * Default VMR3Wait() worker.
 *
 * @returns VBox status code.
 * @param   pUVCpu          Pointer to the user mode VMCPU structure.
 */
static DECLCALLBACK(int) vmR3DefaultWait(PUVMCPU pUVCpu)
{
    ASMAtomicWriteBool(&pUVCpu->vm.s.fWait, true);

    PVM    pVM   = pUVCpu->pVM;
    PVMCPU pVCpu = pUVCpu->pVCpu;
    int    rc    = VINF_SUCCESS;
    for (;;)
    {
        /*
         * Check Relevant FFs.
         */
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_SUSPENDED_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_EXTERNAL_SUSPENDED_MASK))
            break;

        /*
         * Wait for a while. Someone will wake us up or interrupt the call if
         * anything needs our attention.
         */
        rc = RTSemEventWait(pUVCpu->vm.s.EventSemWait, 1000);
        if (rc == VERR_TIMEOUT)
            rc = VINF_SUCCESS;
        else if (RT_FAILURE(rc))
        {
            rc = vmR3FatalWaitError(pUVCpu, "RTSemEventWait->%Rrc", rc);
            break;
        }
    }

    ASMAtomicUoWriteBool(&pUVCpu->vm.s.fWait, false);
    return rc;
}


/**
 * Default VMR3NotifyFF() worker.
 *
 * @param   pUVCpu          Pointer to the user mode VMCPU structure.
 * @param   fFlags          Notification flags, VMNOTIFYFF_FLAGS_*.
 */
static DECLCALLBACK(void) vmR3DefaultNotifyCpuFF(PUVMCPU pUVCpu, uint32_t fFlags)
{
    if (pUVCpu->vm.s.fWait)
    {
        int rc = RTSemEventSignal(pUVCpu->vm.s.EventSemWait);
        AssertRC(rc);
    }
    else
    {
        PVMCPU pVCpu = pUVCpu->pVCpu;
        if (pVCpu)
        {
            VMCPUSTATE enmState = pVCpu->enmState;
            if (   enmState == VMCPUSTATE_STARTED_EXEC_NEM
                || enmState == VMCPUSTATE_STARTED_EXEC_NEM_WAIT)
                NEMR3NotifyFF(pUVCpu->pVM, pVCpu, fFlags);
        }
    }
}


/**
 * Array with halt method descriptors.
 * VMINT::iHaltMethod contains an index into this array.
 */
static const struct VMHALTMETHODDESC
{
    /** The halt method ID. */
    VMHALTMETHOD                enmHaltMethod;
    /** Set if the method support halting directly in ring-0. */
    bool                        fMayHaltInRing0;
    /** The init function for loading config and initialize variables. */
    DECLR3CALLBACKMEMBER(int,   pfnInit,(PUVM pUVM));
    /** The term function. */
    DECLR3CALLBACKMEMBER(void,  pfnTerm,(PUVM pUVM));
    /** The VMR3WaitHaltedU function. */
    DECLR3CALLBACKMEMBER(int,   pfnHalt,(PUVMCPU pUVCpu, const uint32_t fMask, uint64_t u64Now));
    /** The VMR3WaitU function. */
    DECLR3CALLBACKMEMBER(int,   pfnWait,(PUVMCPU pUVCpu));
    /** The VMR3NotifyCpuFFU function. */
    DECLR3CALLBACKMEMBER(void,  pfnNotifyCpuFF,(PUVMCPU pUVCpu, uint32_t fFlags));
    /** The VMR3NotifyGlobalFFU function. */
    DECLR3CALLBACKMEMBER(void,  pfnNotifyGlobalFF,(PUVM pUVM, uint32_t fFlags));
} g_aHaltMethods[] =
{
    { VMHALTMETHOD_BOOTSTRAP, false, NULL,                NULL,   NULL,                vmR3BootstrapWait,   vmR3BootstrapNotifyCpuFF,   NULL },
    { VMHALTMETHOD_OLD,       false, NULL,                NULL,   vmR3HaltOldDoHalt,   vmR3DefaultWait,     vmR3DefaultNotifyCpuFF,     NULL },
    { VMHALTMETHOD_1,         false, vmR3HaltMethod1Init, NULL,   vmR3HaltMethod1Halt, vmR3DefaultWait,     vmR3DefaultNotifyCpuFF,     NULL },
    { VMHALTMETHOD_GLOBAL_1,   true, vmR3HaltGlobal1Init, NULL,   vmR3HaltGlobal1Halt, vmR3HaltGlobal1Wait, vmR3HaltGlobal1NotifyCpuFF, NULL },
};


/**
 * Notify the emulation thread (EMT) about pending Forced Action (FF).
 *
 * This function is called by thread other than EMT to make
 * sure EMT wakes up and promptly service an FF request.
 *
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   fFlags          Notification flags, VMNOTIFYFF_FLAGS_*.
 * @internal
 */
VMMR3_INT_DECL(void) VMR3NotifyGlobalFFU(PUVM pUVM, uint32_t fFlags)
{
    LogFlow(("VMR3NotifyGlobalFFU:\n"));
    uint32_t iHaltMethod = pUVM->vm.s.iHaltMethod;

    if (g_aHaltMethods[iHaltMethod].pfnNotifyGlobalFF) /** @todo make mandatory. */
        g_aHaltMethods[iHaltMethod].pfnNotifyGlobalFF(pUVM, fFlags);
    else
        for (VMCPUID iCpu = 0; iCpu < pUVM->cCpus; iCpu++)
            g_aHaltMethods[iHaltMethod].pfnNotifyCpuFF(&pUVM->aCpus[iCpu], fFlags);
}


/**
 * Notify the emulation thread (EMT) about pending Forced Action (FF).
 *
 * This function is called by thread other than EMT to make
 * sure EMT wakes up and promptly service an FF request.
 *
 * @param   pUVCpu          Pointer to the user mode per CPU VM structure.
 * @param   fFlags          Notification flags, VMNOTIFYFF_FLAGS_*.
 * @internal
 */
VMMR3_INT_DECL(void) VMR3NotifyCpuFFU(PUVMCPU pUVCpu, uint32_t fFlags)
{
    PUVM pUVM = pUVCpu->pUVM;

    LogFlow(("VMR3NotifyCpuFFU:\n"));
    g_aHaltMethods[pUVM->vm.s.iHaltMethod].pfnNotifyCpuFF(pUVCpu, fFlags);
}


/**
 * Halted VM Wait.
 * Any external event will unblock the thread.
 *
 * @returns VINF_SUCCESS unless a fatal error occurred. In the latter
 *          case an appropriate status code is returned.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   fFlags      Combination of VMWAITHALTED_F_XXX.
 * @thread  The emulation thread.
 * @remarks Made visible for implementing vmsvga sync register.
 * @internal
 */
VMMR3_INT_DECL(int) VMR3WaitHalted(PVM pVM, PVMCPU pVCpu, uint32_t fFlags)
{
    LogFlow(("VMR3WaitHalted: fFlags=%#x\n", fFlags));

    /*
     * Check Relevant FFs.
     */
#if defined(VBOX_VMM_TARGET_ARMV8)
    const uint32_t fMaskInterrupts =   ((fFlags & VMWAITHALTED_F_IGNORE_IRQS) ? VMCPU_FF_INTERRUPT_IRQ : 0)
                                     | ((fFlags & VMWAITHALTED_F_IGNORE_FIQS) ? VMCPU_FF_INTERRUPT_FIQ : 0);
    const uint32_t fMask = VMCPU_FF_EXTERNAL_HALTED_MASK & ~fMaskInterrupts;
#else
    const uint32_t fMask = !(fFlags & VMWAITHALTED_F_IGNORE_IRQS)
        ? VMCPU_FF_EXTERNAL_HALTED_MASK
        : VMCPU_FF_EXTERNAL_HALTED_MASK & ~(VMCPU_FF_UPDATE_APIC | VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC);
#endif

    if (    VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_HALTED_MASK)
        ||  VMCPU_FF_IS_ANY_SET(pVCpu, fMask))
    {
        LogFlow(("VMR3WaitHalted: returns VINF_SUCCESS (FF %#x FFCPU %#RX64)\n", pVM->fGlobalForcedActions, (uint64_t)pVCpu->fLocalForcedActions));
        return VINF_SUCCESS;
    }

    /*
     * The yielder is suspended while we're halting, while TM might have clock(s) running
     * only at certain times and need to be notified..
     */
    if (pVCpu->idCpu == 0)
        VMMR3YieldSuspend(pVM);
    TMNotifyStartOfHalt(pVCpu);

    /*
     * Record halt averages for the last second.
     */
    PUVMCPU pUVCpu = pVCpu->pUVCpu;
    uint64_t u64Now = RTTimeNanoTS();
    int64_t off = u64Now - pUVCpu->vm.s.u64HaltsStartTS;
    if (off > 1000000000)
    {
        if (off > _4G || !pUVCpu->vm.s.cHalts)
        {
            pUVCpu->vm.s.HaltInterval = 1000000000 /* 1 sec */;
            pUVCpu->vm.s.HaltFrequency = 1;
        }
        else
        {
            pUVCpu->vm.s.HaltInterval = (uint32_t)off / pUVCpu->vm.s.cHalts;
            pUVCpu->vm.s.HaltFrequency = ASMMultU64ByU32DivByU32(pUVCpu->vm.s.cHalts, 1000000000, (uint32_t)off);
        }
        pUVCpu->vm.s.u64HaltsStartTS = u64Now;
        pUVCpu->vm.s.cHalts = 0;
    }
    pUVCpu->vm.s.cHalts++;

    /*
     * Do the halt.
     */
    VMCPU_ASSERT_STATE_2(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM);
    VMCPUSTATE enmStateOld = VMCPU_GET_STATE(pVCpu);
    VMCPU_SET_STATE(pVCpu, VMCPUSTATE_STARTED_HALTED);
    PUVM pUVM = pUVCpu->pUVM;
    int rc = g_aHaltMethods[pUVM->vm.s.iHaltMethod].pfnHalt(pUVCpu, fMask, u64Now);
    VMCPU_SET_STATE(pVCpu, enmStateOld);

    /*
     * Notify TM and resume the yielder
     */
    TMNotifyEndOfHalt(pVCpu);
    if (pVCpu->idCpu == 0)
        VMMR3YieldResume(pVM);

    LogFlow(("VMR3WaitHalted: returns %Rrc (FF %#x)\n", rc, pVM->fGlobalForcedActions));
    return rc;
}


/**
 * Suspended VM Wait.
 * Only a handful of forced actions will cause the function to
 * return to the caller.
 *
 * @returns VINF_SUCCESS unless a fatal error occurred. In the latter
 *          case an appropriate status code is returned.
 * @param   pUVCpu          Pointer to the user mode VMCPU structure.
 * @thread  The emulation thread.
 * @internal
 */
VMMR3_INT_DECL(int) VMR3WaitU(PUVMCPU pUVCpu)
{
    LogFlow(("VMR3WaitU:\n"));

    /*
     * Check Relevant FFs.
     */
    PVM    pVM   = pUVCpu->pVM;
    PVMCPU pVCpu = pUVCpu->pVCpu;

    if (    pVM
        &&  (   VM_FF_IS_ANY_SET(pVM, VM_FF_EXTERNAL_SUSPENDED_MASK)
             || VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_EXTERNAL_SUSPENDED_MASK)
            )
        )
    {
        LogFlow(("VMR3Wait: returns VINF_SUCCESS (FF %#x)\n", pVM->fGlobalForcedActions));
        return VINF_SUCCESS;
    }

    /*
     * Do waiting according to the halt method (so VMR3NotifyFF
     * doesn't have to special case anything).
     */
    PUVM pUVM = pUVCpu->pUVM;
    int rc = g_aHaltMethods[pUVM->vm.s.iHaltMethod].pfnWait(pUVCpu);
    LogFlow(("VMR3WaitU: returns %Rrc (FF %#x)\n", rc, pUVM->pVM ? pUVM->pVM->fGlobalForcedActions : 0));
    return rc;
}


/**
 * Interface that PDMR3Suspend, PDMR3PowerOff and PDMR3Reset uses when they wait
 * for the handling of asynchronous notifications to complete.
 *
 * @returns VINF_SUCCESS unless a fatal error occurred. In the latter
 *          case an appropriate status code is returned.
 * @param   pUVCpu              Pointer to the user mode VMCPU structure.
 * @thread  The emulation thread.
 */
VMMR3_INT_DECL(int) VMR3AsyncPdmNotificationWaitU(PUVMCPU pUVCpu)
{
    LogFlow(("VMR3AsyncPdmNotificationWaitU:\n"));
    return VMR3WaitU(pUVCpu);
}


/**
 * Interface that PDM the helper asynchronous notification completed methods
 * uses for EMT0 when it is waiting inside VMR3AsyncPdmNotificationWaitU().
 *
 * @param   pUVM                Pointer to the user mode VM structure.
 */
VMMR3_INT_DECL(void) VMR3AsyncPdmNotificationWakeupU(PUVM pUVM)
{
    LogFlow(("VMR3AsyncPdmNotificationWakeupU:\n"));
    VM_FF_SET(pUVM->pVM, VM_FF_REQUEST); /* this will have to do for now. */
    g_aHaltMethods[pUVM->vm.s.iHaltMethod].pfnNotifyCpuFF(&pUVM->aCpus[0], 0 /*fFlags*/);
}


/**
 * Rendezvous callback that will be called once.
 *
 * @returns VBox strict status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser              The new g_aHaltMethods index.
 */
static DECLCALLBACK(VBOXSTRICTRC) vmR3SetHaltMethodCallback(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    PUVM      pUVM = pVM->pUVM;
    int       rc   = VINF_SUCCESS;
    uintptr_t i    = (uintptr_t)pvUser;
    Assert(i < RT_ELEMENTS(g_aHaltMethods));

    /*
     * Main job is done once on EMT0 (it goes thru here first).
     */
    if (pVCpu->idCpu == 0)
    {
        /*
         * Terminate the old one.
         */
        if (    pUVM->vm.s.enmHaltMethod != VMHALTMETHOD_INVALID
            &&  g_aHaltMethods[pUVM->vm.s.iHaltMethod].pfnTerm)
        {
            g_aHaltMethods[pUVM->vm.s.iHaltMethod].pfnTerm(pUVM);
            pUVM->vm.s.enmHaltMethod = VMHALTMETHOD_INVALID;
        }

        /* Assert that the failure fallback is where we expect. */
        Assert(g_aHaltMethods[0].enmHaltMethod == VMHALTMETHOD_BOOTSTRAP);
        Assert(!g_aHaltMethods[0].pfnTerm && !g_aHaltMethods[0].pfnInit);

        /*
         * Init the new one.
         */
        memset(&pUVM->vm.s.Halt, 0, sizeof(pUVM->vm.s.Halt));
        if (g_aHaltMethods[i].pfnInit)
        {
            rc = g_aHaltMethods[i].pfnInit(pUVM);
            if (RT_FAILURE(rc))
            {
                /* Fall back on the bootstrap method. This requires no
                   init/term (see assertion above), and will always work. */
                AssertLogRelRC(rc);
                i = 0;
            }
        }

        /*
         * Commit it.
         */
        pUVM->vm.s.enmHaltMethod = g_aHaltMethods[i].enmHaltMethod;
        ASMAtomicWriteU32(&pUVM->vm.s.iHaltMethod, i);
    }
    else
        i = pUVM->vm.s.iHaltMethod;

    /*
     * All EMTs must update their ring-0 halt configuration.
     */
    VMMR3SetMayHaltInRing0(pVCpu, g_aHaltMethods[i].fMayHaltInRing0,
                           g_aHaltMethods[i].enmHaltMethod == VMHALTMETHOD_GLOBAL_1
                           ? pUVM->vm.s.Halt.Global1.cNsSpinBlockThresholdCfg : 0);

    return rc;
}


/**
 * Changes the halt method.
 *
 * @returns VBox status code.
 * @param   pUVM            Pointer to the user mode VM structure.
 * @param   enmHaltMethod   The new halt method.
 * @thread  EMT.
 */
int vmR3SetHaltMethodU(PUVM pUVM, VMHALTMETHOD enmHaltMethod)
{
    PVM pVM = pUVM->pVM; Assert(pVM);
    VM_ASSERT_EMT(pVM);
    AssertReturn(enmHaltMethod > VMHALTMETHOD_INVALID && enmHaltMethod < VMHALTMETHOD_END, VERR_INVALID_PARAMETER);

    /*
     * Resolve default (can be overridden in the configuration).
     */
    if (enmHaltMethod == VMHALTMETHOD_DEFAULT)
    {
        uint32_t u32;
        int rc = CFGMR3QueryU32(CFGMR3GetChild(CFGMR3GetRoot(pVM), "VM"), "HaltMethod", &u32);
        if (RT_SUCCESS(rc))
        {
            enmHaltMethod = (VMHALTMETHOD)u32;
            if (enmHaltMethod <= VMHALTMETHOD_INVALID || enmHaltMethod >= VMHALTMETHOD_END)
                return VMSetError(pVM, VERR_INVALID_PARAMETER, RT_SRC_POS, N_("Invalid VM/HaltMethod value %d"), enmHaltMethod);
        }
        else if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_CHILD_NOT_FOUND)
            return VMSetError(pVM, rc, RT_SRC_POS, N_("Failed to Query VM/HaltMethod as uint32_t"));
        else
            enmHaltMethod = VMHALTMETHOD_GLOBAL_1;
            //enmHaltMethod = VMHALTMETHOD_1;
            //enmHaltMethod = VMHALTMETHOD_OLD;
    }

    /*
     * The global halt method doesn't work in driverless mode, so fall back on
     * method #1 instead.
     */
    if (!SUPR3IsDriverless() || enmHaltMethod != VMHALTMETHOD_GLOBAL_1)
        LogRel(("VMEmt: Halt method %s (%d)\n", vmR3GetHaltMethodName(enmHaltMethod), enmHaltMethod));
    else
    {
        LogRel(("VMEmt: Halt method %s (%d) not available in driverless mode, using %s (%d) instead\n",
                vmR3GetHaltMethodName(enmHaltMethod), enmHaltMethod, vmR3GetHaltMethodName(VMHALTMETHOD_1), VMHALTMETHOD_1));
        enmHaltMethod = VMHALTMETHOD_1;
    }


    /*
     * Find the descriptor.
     */
    unsigned i = 0;
    while (     i < RT_ELEMENTS(g_aHaltMethods)
           &&   g_aHaltMethods[i].enmHaltMethod != enmHaltMethod)
        i++;
    AssertReturn(i < RT_ELEMENTS(g_aHaltMethods), VERR_INVALID_PARAMETER);

    /*
     * This needs to be done while the other EMTs are not sleeping or otherwise messing around.
     */
    return VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ASCENDING, vmR3SetHaltMethodCallback, (void *)(uintptr_t)i);
}


/**
 * Special interface for implementing a HLT-like port on a device.
 *
 * This can be called directly from device code, provide the device is trusted
 * to access the VMM directly.  Since we may not have an accurate register set
 * and the caller certainly shouldn't (device code does not access CPU
 * registers), this function will return when interrupts are pending regardless
 * of the actual EFLAGS.IF state.
 *
 * @returns VBox error status (never informational statuses).
 * @param   pVM                 The cross context VM structure.
 * @param   idCpu               The id of the calling EMT.
 */
VMMR3DECL(int) VMR3WaitForDeviceReady(PVM pVM, VMCPUID idCpu)
{
    /*
     * Validate caller and resolve the CPU ID.
     */
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pVM->cCpus, VERR_INVALID_CPU_ID);
    PVMCPU pVCpu = pVM->apCpusR3[idCpu];
    VMCPU_ASSERT_EMT_RETURN(pVCpu, VERR_VM_THREAD_NOT_EMT);

    /*
     * Tag along with the HLT mechanics for now.
     */
    int rc = VMR3WaitHalted(pVM, pVCpu, false /*fIgnoreInterrupts*/);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;
    return rc;
}


/**
 * Wakes up a CPU that has called VMR3WaitForDeviceReady.
 *
 * @returns VBox error status (never informational statuses).
 * @param   pVM                 The cross context VM structure.
 * @param   idCpu               The id of the calling EMT.
 */
VMMR3DECL(int) VMR3NotifyCpuDeviceReady(PVM pVM, VMCPUID idCpu)
{
    /*
     * Validate caller and resolve the CPU ID.
     */
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pVM->cCpus, VERR_INVALID_CPU_ID);
    PVMCPU pVCpu = pVM->apCpusR3[idCpu];

    /*
     * Pretend it was an FF that got set since we've got logic for that already.
     */
    VMR3NotifyCpuFFU(pVCpu->pUVCpu, VMNOTIFYFF_FLAGS_DONE_REM);
    return VINF_SUCCESS;
}


/**
 * Returns the number of active EMTs.
 *
 * This is used by the rendezvous code during VM destruction to avoid waiting
 * for EMTs that aren't around any more.
 *
 * @returns Number of active EMTs.  0 if invalid parameter.
 * @param   pUVM                The user mode VM structure.
 */
VMMR3_INT_DECL(uint32_t) VMR3GetActiveEmts(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, 0);
    return pUVM->vm.s.cActiveEmts;
}

