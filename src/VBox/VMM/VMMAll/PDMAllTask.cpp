/* $Id: PDMAllTask.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Task - Asynchronous user mode tasks, all context code.
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
#include <VBox/vmm/gvm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <VBox/sup.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>



/**
 * Triggers a task.
 *
 * @returns VBox status code.
 * @retval  VINF_ALREADY_POSTED is the task is already pending.
 *
 * @param   pVM         The cross context VM structure.
 * @param   enmType     The task owner type.
 * @param   pvOwner     The task owner.
 * @param   hTask       The task to trigger.
 * @thread  Any
 */
VMM_INT_DECL(int)   PDMTaskTrigger(PVMCC pVM, PDMTASKTYPE enmType, RTR3PTR pvOwner, PDMTASKHANDLE hTask)
{
    /*
     * Validate input and translate the handle to a task.
     */
    AssertReturn(pvOwner, VERR_NOT_OWNER);
    AssertReturn(enmType >= PDMTASKTYPE_DEV && enmType <= PDMTASKTYPE_INTERNAL, VERR_NOT_OWNER);

    size_t const iTask    = hTask % RT_ELEMENTS(pVM->pdm.s.aTaskSets[0].aTasks);
    size_t const iTaskSet = hTask / RT_ELEMENTS(pVM->pdm.s.aTaskSets[0].aTasks);
#ifdef IN_RING3
    AssertReturn(iTaskSet < RT_ELEMENTS(pVM->pdm.s.apTaskSets), VERR_INVALID_HANDLE);
    PPDMTASKSET pTaskSet  = pVM->pdm.s.apTaskSets[iTaskSet];
    AssertReturn(pTaskSet, VERR_INVALID_HANDLE);
#else
    AssertReturn(iTaskSet < RT_ELEMENTS(pVM->pdm.s.aTaskSets),
                 iTaskSet < RT_ELEMENTS(pVM->pdm.s.apTaskSets) ? VERR_INVALID_CONTEXT : VERR_INVALID_HANDLE);
    PPDMTASKSET pTaskSet  = &pVM->pdm.s.aTaskSets[iTaskSet];
#endif
    AssertReturn(pTaskSet->u32Magic == PDMTASKSET_MAGIC, VERR_INVALID_MAGIC);
    PPDMTASK pTask = &pTaskSet->aTasks[iTask];

    /*
     * Check task ownership.
     */
    AssertReturn(pvOwner == pTask->pvOwner, VERR_NOT_OWNER);
    AssertReturn(enmType == pTask->enmType, VERR_NOT_OWNER);

    /*
     * Trigger the task, wake up the thread if the task isn't triggered yet.
     */
    if (!ASMAtomicBitTestAndSet(&pTaskSet->fTriggered, (uint32_t)iTask))
    {
        Log(("PDMTaskTrigger: Triggered %RU64 (%s)\n", hTask, R3STRING(pTask->pszName)));
#ifdef IN_RING3
        if (pTaskSet->hEventR3 != NIL_RTSEMEVENT)
        {
            int rc = RTSemEventSignal(pTaskSet->hEventR3);
            AssertLogRelRCReturn(rc, rc);
        }
        else
#endif
        {
            int rc = SUPSemEventSignal(pVM->pSession, pTaskSet->hEventR0);
            AssertLogRelRCReturn(rc, rc);
        }
        return VINF_SUCCESS;
    }
    ASMAtomicIncU32(&pTask->cAlreadyTrigged);
    Log(("PDMTaskTrigger: %RU64 (%s) was already triggered\n", hTask, R3STRING(pTask->pszName)));
    return VINF_ALREADY_POSTED;
}


/**
 * Triggers an internal task.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   hTask       The task to trigger.
 * @thread  Any
 */
VMM_INT_DECL(int)   PDMTaskTriggerInternal(PVMCC pVM, PDMTASKHANDLE hTask)
{
    return PDMTaskTrigger(pVM, PDMTASKTYPE_INTERNAL, pVM->pVMR3, hTask);
}

