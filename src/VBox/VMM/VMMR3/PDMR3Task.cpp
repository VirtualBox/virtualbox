/* $Id: PDMR3Task.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Task - Asynchronous user mode tasks.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_PDM_TASK
#include "PDMInternal.h"
#include <VBox/vmm/pdmtask.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <VBox/sup.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>


/**
 * @callback_method_impl{FNDBGFINFOARGVINT}
 */
static DECLCALLBACK(void) pdmR3TaskInfo(PVM pVM, PCDBGFINFOHLP pHlp, int cArgs, char **papszArgs)
{
    RT_NOREF(cArgs, papszArgs); /* for now. */

    uint32_t cSetsDisplayed = 0;
    for (size_t i = 0; i < RT_ELEMENTS(pVM->pdm.s.apTaskSets); i++)
    {
        PPDMTASKSET pTaskSet = pVM->pdm.s.apTaskSets[i];
        if (   pTaskSet
            && (   pTaskSet->cAllocated > 0
                || ASMAtomicReadU64(&pTaskSet->fTriggered)))
        {
            if (cSetsDisplayed > 0)
                pHlp->pfnPrintf(pHlp, "\n");
            pHlp->pfnPrintf(pHlp,
                            "Task set #%u - handle base %u, pending %#RX64%s%s, running %d, %u of %u allocated:\n"
                          /*  123: triggered internal 0123456789abcdef 0123456789abcdef 0x0000 SomeFunctionName */
                            " Hnd:   State     Type   pfnCallback      pvUser           Flags  Name\n",
                            i, pTaskSet->uHandleBase, ASMAtomicReadU64(&pTaskSet->fTriggered),
                            pTaskSet->fRZEnabled ? " RZ-enabled" : "", pTaskSet->hThread != NIL_RTTHREAD ? "" : " no-thread",
                            (int)ASMAtomicReadU32(&pTaskSet->idxRunning), pTaskSet->cAllocated, RT_ELEMENTS(pTaskSet->aTasks));
            for (unsigned j = 0; j < RT_ELEMENTS(pTaskSet->aTasks); j++)
            {
                PPDMTASK pTask = &pTaskSet->aTasks[j];
                if (pTask->pvOwner)
                {
                    const char *pszType;
                    switch (pTask->enmType)
                    {
                        case PDMTASKTYPE_DEV:       pszType = " device "; break;
                        case PDMTASKTYPE_DRV:       pszType = " driver "; break;
                        case PDMTASKTYPE_USB:       pszType = " usbdev "; break;
                        case PDMTASKTYPE_INTERNAL:  pszType = "internal"; break;
                        default:                    pszType = "unknown "; break;
                    }
                    pHlp->pfnPrintf(pHlp, " %3u: %s %s %p %p %#06x %s\n", pTaskSet->uHandleBase + j,
                                    ASMBitTest(&pTaskSet->fTriggered, j) ? "triggered"
                                    : ASMAtomicReadU32(&pTaskSet->idxRunning) == j  ? " running " : "  idle   ",
                                    pszType, pTask->pfnCallback, pTask->pvUser, pTask->fFlags, pTask->pszName);
                }
            }

            cSetsDisplayed++;
        }
    }
}


/**
 * Initializes the ring-0 capable tasks during VM construction.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
int pdmR3TaskInit(PVM pVM)
{
    for (size_t i = 0; i < RT_ELEMENTS(pVM->pdm.s.aTaskSets); i++)
    {
        PPDMTASKSET pTaskSet = &pVM->pdm.s.aTaskSets[i];

        pTaskSet->u32Magic      = PDMTASKSET_MAGIC;
        pTaskSet->fRZEnabled    = true;
        //pTaskSet->cAllocated  = 0;
        pTaskSet->uHandleBase   = (uint16_t)(i * RT_ELEMENTS(pTaskSet->aTasks));
        pTaskSet->hThread       = NIL_RTTHREAD;
        int rc = SUPSemEventCreate(pVM->pSession, &pTaskSet->hEventR0);
        AssertRCReturn(rc, rc);
        pTaskSet->hEventR3      = NIL_RTSEMEVENT;
        //pTaskSet->fTriggered  = 0;
        pTaskSet->idxRunning    = UINT8_MAX;
        //pTaskSet->fShutdown   = false;
        pTaskSet->pVM           = pVM;

        pVM->pdm.s.apTaskSets[i] = pTaskSet;
    }

    int rc = DBGFR3InfoRegisterInternalArgv(pVM, "tasks", "PDM tasks", pdmR3TaskInfo, 0 /*fFlags*/);
    AssertRC(rc);

    return VINF_SUCCESS;
}


/**
 * Terminates task threads when the VM is destroyed.
 *
 * @param   pVM     The cross context VM structure.
 */
void pdmR3TaskTerm(PVM pVM)
{
    /*
     * Signal all the threads first.
     */
    for (size_t i = 0; i < RT_ELEMENTS(pVM->pdm.s.apTaskSets); i++)
    {
        PPDMTASKSET pTaskSet = pVM->pdm.s.apTaskSets[i];
        if (pTaskSet)
        {
            /*
             * Set the shutdown indicator and signal the thread.
             */
            ASMAtomicWriteBool(&pTaskSet->fShutdown, true);

            if (pTaskSet->hEventR0 != NIL_SUPSEMEVENT)
            {
                int rc = SUPSemEventSignal(pVM->pSession, pTaskSet->hEventR0);
                AssertRC(rc);
            }

            if (pTaskSet->hEventR3 != NIL_RTSEMEVENT)
            {
                int rc = RTSemEventSignal(pTaskSet->hEventR3);
                AssertRC(rc);
            }
        }
    }

    /*
     * Wait for them to terminate and clean up semaphores.
     */
    for (size_t i = 0; i < RT_ELEMENTS(pVM->pdm.s.apTaskSets); i++)
    {
        PPDMTASKSET pTaskSet = pVM->pdm.s.apTaskSets[i];
        if (pTaskSet)
        {
            /*
             * Wait for the thread to terminate.
             */
            if (pTaskSet->hThread != NIL_RTTHREAD)
            {
                int rc = RTThreadWait(pTaskSet->hThread, RT_MS_30SEC, NULL);
                AssertLogRelMsg(RT_SUCCESS(rc), ("pTaskSet %u: thread wait failed: %Rrc\n", i, rc));
                if (RT_SUCCESS(rc))
                    pTaskSet->hThread = NIL_RTTHREAD;
            }

            /*
             * Destroy the semaphore.
             */
            if (pTaskSet->hEventR0 != NIL_SUPSEMEVENT)
            {
                int rc = SUPSemEventClose(pVM->pSession, pTaskSet->hEventR0);
                AssertRC(rc);
                pTaskSet->hEventR0 = NIL_SUPSEMEVENT;
            }

            if (pTaskSet->hEventR3 != NIL_RTSEMEVENT)
            {
                int rc = RTSemEventDestroy(pTaskSet->hEventR3);
                AssertRC(rc);
                pTaskSet->hEventR3 = NIL_RTSEMEVENT;
            }
        }
    }
}


/**
 * @callback_method_impl{FNRTTHREAD,
 *      PDM Asynchronous Task Executor Thread}
 */
static DECLCALLBACK(int) pdmR3TaskThread(RTTHREAD ThreadSelf, void *pvUser)
{
    PPDMTASKSET const pTaskSet = (PPDMTASKSET)pvUser;
    AssertPtr(pTaskSet);
    Assert(pTaskSet->u32Magic == PDMTASKSET_MAGIC);
    RT_NOREF(ThreadSelf);

    /*
     * Process stuff until we're told to terminate.
     */
    while (!ASMAtomicReadBool(&pTaskSet->fShutdown))
    {
        /*
         * Process pending tasks.
         *
         * The outer loop runs till there are no more pending tasks.
         *
         * The inner loop takes one snapshot of fTriggered and processes all
         * pending bits in the snaphot.   This ensure fairness.
         */
        for (;;)
        {
            uint64_t fTriggered = ASMAtomicReadU64(&pTaskSet->fTriggered);
            unsigned iTask      = ASMBitFirstSetU64(fTriggered);
            if (iTask == 0)
                break;
            uint32_t cShutdown = 3;
            do
            {
                iTask--;
                AssertBreak(iTask < RT_ELEMENTS(pTaskSet->aTasks));

                if (ASMAtomicBitTestAndClear(&pTaskSet->fTriggered, iTask))
                {
                    PPDMTASK  pTask = &pTaskSet->aTasks[iTask];

                    /* Copy out the data we need here to try avoid destruction race trouble. */
                    PDMTASKTYPE const enmType     = pTask->enmType;
                    PFNRT  const      pfnCallback = pTask->pfnCallback;
                    void * const      pvOwner     = pTask->pvOwner;
                    void * const      pvTaskUser  = pTask->pvUser;

                    ASMAtomicWriteU32(&pTaskSet->idxRunning, iTask);

                    if (   pvOwner
                        && pfnCallback
                        && pvOwner     == pTask->pvOwner
                        && pfnCallback == pTask->pfnCallback
                        && pvTaskUser  == pTask->pvUser
                        && enmType     == pTask->enmType)
                    {
                        pTask->cRuns += 1;
                        switch (pTask->enmType)
                        {
                            case PDMTASKTYPE_DEV:
                                Log2(("pdmR3TaskThread: Runs dev task %s (%#x)\n", pTask->pszName, iTask + pTaskSet->uHandleBase));
                                ((PFNPDMTASKDEV)(pfnCallback))((PPDMDEVINS)pvOwner, pvTaskUser);
                                break;
                            case PDMTASKTYPE_DRV:
                                Log2(("pdmR3TaskThread: Runs drv task %s (%#x)\n", pTask->pszName, iTask + pTaskSet->uHandleBase));
                                ((PFNPDMTASKDRV)(pfnCallback))((PPDMDRVINS)pvOwner, pvTaskUser);
                                break;
                            case PDMTASKTYPE_USB:
                                Log2(("pdmR3TaskThread: Runs USB task %s (%#x)\n", pTask->pszName, iTask + pTaskSet->uHandleBase));
                                ((PFNPDMTASKUSB)(pfnCallback))((PPDMUSBINS)pvOwner, pvTaskUser);
                                break;
                            case PDMTASKTYPE_INTERNAL:
                                Log2(("pdmR3TaskThread: Runs int task %s (%#x)\n", pTask->pszName, iTask + pTaskSet->uHandleBase));
                                ((PFNPDMTASKINT)(pfnCallback))((PVM)pvOwner, pvTaskUser);
                                break;
                            default:
                                AssertFailed();
                        }
                    }
                    else /* Note! There might be a race here during destruction. */
                        AssertMsgFailed(("%d %p %p %p\n", enmType, pvOwner, pfnCallback, pvTaskUser));

                    ASMAtomicWriteU32(&pTaskSet->idxRunning, UINT32_MAX);
                }

                /* Next pending task. */
                fTriggered &= ~RT_BIT_64(iTask);
                iTask = ASMBitFirstSetU64(fTriggered);
            } while (iTask != 0);

            /*
             * If we're shutting down, we'll try drain the pending tasks by
             * looping three more times before just quitting.  We don't want
             * to get stuck here if some stuff is misbehaving.
             */
            if (!ASMAtomicReadBool(&pTaskSet->fShutdown))
            { /* likely */ }
            else if (--cShutdown == 0)
                break;
        }

        /*
         * Wait unless we're shutting down.
         */
        if (!ASMAtomicReadBool(&pTaskSet->fShutdown))
        {
            if (pTaskSet->fRZEnabled)
                SUPSemEventWaitNoResume(pTaskSet->pVM->pSession, pTaskSet->hEventR0, RT_MS_15SEC);
            else
                RTSemEventWaitNoResume(pTaskSet->hEventR3, RT_MS_15SEC);
        }
    }

    /*
     * Complain about pending tasks.
     */
    uint64_t const fTriggered = ASMAtomicReadU64(&pTaskSet->fTriggered);
    AssertLogRelMsg(fTriggered == 0, ("fTriggered=%#RX64 - %u %s\n", fTriggered, ASMBitFirstSetU64(fTriggered) - 1,
                                      pTaskSet->aTasks[ASMBitFirstSetU64(fTriggered) - 1].pszName));

    return VINF_SUCCESS;
}


/**
 * Worker for PDMR3TaskCreate().
 */
DECLINLINE(PPDMTASK) pdmR3TaskAllocInSet(PPDMTASKSET pTaskSet)
{
    if (pTaskSet->cAllocated < RT_ELEMENTS(pTaskSet->aTasks))
    {
        for (size_t j = 0; j < RT_ELEMENTS(pTaskSet->aTasks); j++)
            if (pTaskSet->aTasks[j].pvOwner == NULL)
                return &pTaskSet->aTasks[j];
        AssertFailed();
    }
    return NULL;
}

/**
 * Creates a task.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   fFlags      PDMTASK_F_XXX.
 * @param   pszName     The task name (function name ++).
 * @param   enmType     The task owner type.
 * @param   pvOwner     The task owner pointer.
 * @param   pfnCallback The task callback.
 * @param   pvUser      The user argument for the callback.
 * @param   phTask      Where to return the task handle.
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(int) PDMR3TaskCreate(PVM pVM, uint32_t fFlags, const char *pszName, PDMTASKTYPE enmType, void *pvOwner,
                                    PFNRT pfnCallback, void *pvUser, PDMTASKHANDLE *phTask)
{
    /*
     * Validate input.
     */
    AssertReturn(!(fFlags & ~PDMTASK_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(pvOwner, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT); /* implicit serialization by requiring EMT(0) */
    switch (enmType)
    {
        case PDMTASKTYPE_DEV:
        case PDMTASKTYPE_DRV:
        case PDMTASKTYPE_USB:
            break;
        case PDMTASKTYPE_INTERNAL:
            AssertReturn(pvOwner == (void *)pVM, VERR_INVALID_PARAMETER);
            break;
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    /*
     * If the callback must be ring-0 triggerable, we are restricted to the
     * task sets living the VM structure.  Otherwise, pick from the dynamically
     * allocated sets living on ring-3 heap.
     */
    PPDMTASKSET pTaskSet = NULL;
    PPDMTASK    pTask    = NULL;
    if (fFlags & PDMTASK_F_RZ)
    {
        for (size_t i = 0; i < RT_ELEMENTS(pVM->pdm.s.aTaskSets); i++)
        {
            pTaskSet = &pVM->pdm.s.aTaskSets[i];
            pTask = pdmR3TaskAllocInSet(pTaskSet);
            if (pTask)
                break;
        }
    }
    else
    {
        for (size_t i = RT_ELEMENTS(pVM->pdm.s.aTaskSets); i < RT_ELEMENTS(pVM->pdm.s.apTaskSets); i++)
        {
            pTaskSet = pVM->pdm.s.apTaskSets[i];
            if (pTaskSet)
            {
                pTask = pdmR3TaskAllocInSet(pTaskSet);
                if (pTask)
                    break;
            }
            else
            {
                /*
                 * Try allocate a new set.
                 */
                LogFlow(("PDMR3TaskCreate: Allocating new task set (%#u)...\n", i));
                pTaskSet = (PPDMTASKSET)MMR3HeapAllocZ(pVM, MM_TAG_PDM, sizeof(*pTaskSet));
                AssertReturn(pTaskSet, VERR_NO_MEMORY);

                pTaskSet->u32Magic      = PDMTASKSET_MAGIC;
                //pTaskSet->fRZEnabled  = false;
                //pTaskSet->cAllocated  = 0;
                pTaskSet->uHandleBase   = (uint16_t)(i * RT_ELEMENTS(pTaskSet->aTasks));
                pTaskSet->hThread       = NIL_RTTHREAD;
                pTaskSet->hEventR0      = NIL_SUPSEMEVENT;
                int rc = RTSemEventCreate(&pTaskSet->hEventR3);
                AssertRCReturnStmt(rc, MMR3HeapFree(pTaskSet), rc);
                //pTaskSet->fTriggered  = 0;
                pTaskSet->idxRunning    = UINT8_MAX;
                //pTaskSet->fShutdown   = false;
                pTaskSet->pVM           = pVM;

                pVM->pdm.s.apTaskSets[i] = pTaskSet;
                pTask = &pTaskSet->aTasks[0];
                break;
            }
        }
    }
    AssertLogRelReturn(pTask, VERR_OUT_OF_RESOURCES);

    /*
     * Do we need to start a worker thread?  Do this first as it can fail.
     */
    if (pTaskSet->hThread == NIL_RTTHREAD)
    {
        int rc = RTThreadCreateF(&pTaskSet->hThread, pdmR3TaskThread, pTaskSet, 0 /*cbStack*/, RTTHREADTYPE_IO,
                                 RTTHREADFLAGS_WAITABLE, "TaskSet%u", pTaskSet->uHandleBase / RT_ELEMENTS(pTaskSet->aTasks));
        AssertLogRelRCReturn(rc, rc);
    }

    /*
     * Complete the allocation.
     */
    pTask->enmType     = enmType;
    pTask->fFlags      = fFlags;
    pTask->pvUser      = pvUser;
    pTask->pfnCallback = pfnCallback;
    pTask->pszName     = pszName;
    ASMAtomicWritePtr(&pTask->pvOwner, pvOwner);
    pTaskSet->cAllocated += 1;

    uint32_t const hTask = pTaskSet->uHandleBase + (uint32_t)(pTask - &pTaskSet->aTasks[0]);
    *phTask = hTask;

    STAMR3RegisterF(pVM, &pTask->cRuns, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                    "Number of times the task has been executed.", "/PDM/Tasks/%03u-%s-runs", hTask, pszName);
    STAMR3RegisterF(pVM, (void *)&pTask->cAlreadyTrigged, STAMTYPE_U32_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                    "Number of times the task was re-triggered.", "/PDM/Tasks/%03u-%s-retriggered", hTask, pszName);

    LogFlow(("PDMR3TaskCreate: Allocated %u for %s\n", hTask, pszName));
    return VINF_SUCCESS;
}


/**
 * Creates an internal task.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   fFlags      PDMTASK_F_XXX.
 * @param   pszName     The task name (function name ++).
 * @param   pfnCallback The task callback.
 * @param   pvUser      The user argument for the callback.
 * @param   phTask      Where to return the task handle.
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(int) PDMR3TaskCreateInternal(PVM pVM, uint32_t fFlags, const char *pszName,
                                            PFNPDMTASKINT pfnCallback, void *pvUser, PDMTASKHANDLE *phTask)
{
    return PDMR3TaskCreate(pVM, fFlags, pszName, PDMTASKTYPE_INTERNAL, pVM, (PFNRT)pfnCallback, pvUser, phTask);
}


/**
 * Worker for PDMR3TaskDestroyAllByOwner() and PDMR3TaskDestroySpecific().
 */
static void pdmR3TaskDestroyOne(PVM pVM, PPDMTASKSET pTaskSet, PPDMTASK pTask, size_t iTask)
{
    AssertPtr(pTask->pvOwner);

    /*
     * Delay if busy.
     */
    uint32_t cYields = 64;
    while (   ASMAtomicReadU32(&pTaskSet->idxRunning) == iTask
           && cYields > 0
           && pTaskSet->hThread != NIL_RTTHREAD)
    {
        ASMNopPause();
        RTThreadYield();
    }

    /*
     * Zap it (very noisy, but whatever).
     */
    LogFlow(("pdmR3TaskDestroyOne: Destroying %zu %s\n", iTask + pTaskSet->uHandleBase, pTask->pszName));
    AssertPtr(pTask->pvOwner);

    char szPrefix[64];
    RTStrPrintf(szPrefix, sizeof(szPrefix), "/PDM/Tasks/%03zu-", iTask + pTaskSet->uHandleBase);
    STAMR3DeregisterByPrefix(pVM->pUVM, szPrefix);

    AssertPtr(pTask->pvOwner);
    ASMAtomicWriteNullPtr(&pTask->pvOwner);
    pTask->enmType     = (PDMTASKTYPE)0;
    pTask->fFlags      = 0;
    ASMAtomicWriteNullPtr((void **)&pTask->pfnCallback);
    ASMAtomicWriteNullPtr(&pTask->pvUser);
    ASMAtomicWriteNullPtr(&pTask->pszName);

    AssertReturnVoid(pTaskSet->cAllocated > 0);
    pTaskSet->cAllocated -= 1;
}


/**
 * Destroys all tasks belonging to @a pvOwner.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmType     The owner type.
 * @param   pvOwner     The owner.
 */
VMMR3_INT_DECL(int) PDMR3TaskDestroyAllByOwner(PVM pVM, PDMTASKTYPE enmType, void *pvOwner)
{
    /*
     * Validate input.
     */
    AssertReturn(enmType >= PDMTASKTYPE_DEV && enmType < PDMTASKTYPE_INTERNAL, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvOwner, VERR_INVALID_POINTER);
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT); /* implicit serialization by requiring EMT(0) */

    /*
     * Scan all the task sets.
     */
    for (size_t i = 0; i < RT_ELEMENTS(pVM->pdm.s.apTaskSets); i++)
    {
        PPDMTASKSET pTaskSet = pVM->pdm.s.apTaskSets[i];
        if (pTaskSet)
        {
            ssize_t cLeft = pTaskSet->cAllocated;
            for (size_t j = 0; j < RT_ELEMENTS(pTaskSet->aTasks) && cLeft > 0; j++)
            {
                PPDMTASK     pTask       = &pTaskSet->aTasks[j];
                void * const pvTaskOwner = pTask->pvOwner;
                if (pvTaskOwner)
                {
                    if (   pvTaskOwner == pvOwner
                        && pTask->enmType == enmType)
                        pdmR3TaskDestroyOne(pVM, pTaskSet, pTask, j);
                    else
                        Assert(pvTaskOwner != pvOwner);
                    cLeft--;
                }
            }
        }
        else
            break;
    }

    return VINF_SUCCESS;
}


/**
 * Destroys the task @a hTask.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmType     The owner type.
 * @param   pvOwner     The owner.
 * @param   hTask       Handle to the task to destroy.
 */
VMMR3_INT_DECL(int) PDMR3TaskDestroySpecific(PVM pVM, PDMTASKTYPE enmType, void *pvOwner, PDMTASKHANDLE hTask)
{
    /*
     * Validate the input.
     */
    AssertReturn(enmType >= PDMTASKTYPE_DEV && enmType <= PDMTASKTYPE_INTERNAL, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvOwner, VERR_INVALID_POINTER);

    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    size_t const      iTask    = hTask % RT_ELEMENTS(pVM->pdm.s.apTaskSets[0]->aTasks);
    size_t const      iTaskSet = hTask / RT_ELEMENTS(pVM->pdm.s.apTaskSets[0]->aTasks);
    AssertReturn(iTaskSet < RT_ELEMENTS(pVM->pdm.s.apTaskSets), VERR_INVALID_HANDLE);
    PPDMTASKSET const pTaskSet = pVM->pdm.s.apTaskSets[iTaskSet];
    AssertPtrReturn(pTaskSet, VERR_INVALID_HANDLE);
    AssertReturn(pTaskSet->u32Magic == PDMTASKSET_MAGIC, VERR_INVALID_MAGIC);
    PPDMTASK const    pTask    = &pTaskSet->aTasks[iTask];

    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT); /* implicit serialization by requiring EMT(0) */

    AssertReturn(pTask->pvOwner == pvOwner, VERR_NOT_OWNER);
    AssertReturn(pTask->enmType == enmType, VERR_NOT_OWNER);

    /*
     * Do the job.
     */
    pdmR3TaskDestroyOne(pVM, pTaskSet, pTask, iTask);

    return VINF_SUCCESS;
}


/**
 * Destroys the internal task @a hTask.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   hTask       Handle to the task to destroy.
 */
VMMR3_INT_DECL(int) PDMR3TaskDestroyInternal(PVM pVM, PDMTASKHANDLE hTask)
{
    return PDMR3TaskDestroySpecific(pVM, PDMTASKTYPE_INTERNAL, pVM, hTask);
}

