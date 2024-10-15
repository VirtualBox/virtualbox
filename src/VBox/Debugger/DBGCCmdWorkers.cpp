/* $Id: DBGCCmdWorkers.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, Command Worker Routines.
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
#define LOG_GROUP LOG_GROUP_DBGC
#include <VBox/dbg.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/assert.h>

#include "DBGCInternal.h"




//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//
//
//
//      B r e a k p o i n t   M a n a g e m e n t
//
//
//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//


/**
 * Adds a breakpoint to the DBGC breakpoint list.
 */
int dbgcBpAdd(PDBGC pDbgc, RTUINT iBp, const char *pszCmd)
{
    /*
     * Check if it already exists.
     */
    PDBGCBP pBp = dbgcBpGet(pDbgc, iBp);
    if (pBp)
        return VERR_DBGC_BP_EXISTS;

    /*
     * Add the breakpoint.
     */
    if (pszCmd)
        pszCmd = RTStrStripL(pszCmd);
    size_t cchCmd = pszCmd ? strlen(pszCmd) : 0;
    pBp = (PDBGCBP)RTMemAlloc(RT_UOFFSETOF_DYN(DBGCBP, szCmd[cchCmd + 1]));
    if (!pBp)
        return VERR_NO_MEMORY;
    if (cchCmd)
        memcpy(pBp->szCmd, pszCmd, cchCmd + 1);
    else
        pBp->szCmd[0] = '\0';
    pBp->cchCmd = cchCmd;
    pBp->iBp    = iBp;
    pBp->pNext  = pDbgc->pFirstBp;
    pDbgc->pFirstBp = pBp;

    return VINF_SUCCESS;
}

/**
 * Updates the a breakpoint.
 *
 * @returns VBox status code.
 * @param   pDbgc       The DBGC instance.
 * @param   iBp         The breakpoint to update.
 * @param   pszCmd      The new command.
 */
int dbgcBpUpdate(PDBGC pDbgc, RTUINT iBp, const char *pszCmd)
{
    /*
     * Find the breakpoint.
     */
    PDBGCBP pBp = dbgcBpGet(pDbgc, iBp);
    if (!pBp)
        return VERR_DBGC_BP_NOT_FOUND;

    /*
     * Do we need to reallocate?
     */
    if (pszCmd)
        pszCmd = RTStrStripL(pszCmd);
    if (!pszCmd || !*pszCmd)
        pBp->szCmd[0] = '\0';
    else
    {
        size_t cchCmd = strlen(pszCmd);
        if (strlen(pBp->szCmd) >= cchCmd)
        {
            memcpy(pBp->szCmd, pszCmd, cchCmd + 1);
            pBp->cchCmd = cchCmd;
        }
        else
        {
            /*
             * Yes, let's do it the simple way...
             */
            int rc = dbgcBpDelete(pDbgc, iBp);
            AssertRC(rc);
            return dbgcBpAdd(pDbgc, iBp, pszCmd);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Deletes a breakpoint.
 *
 * @returns VBox status code.
 * @param   pDbgc       The DBGC instance.
 * @param   iBp         The breakpoint to delete.
 */
int dbgcBpDelete(PDBGC pDbgc, RTUINT iBp)
{
    /*
     * Search thru the list, when found unlink and free it.
     */
    PDBGCBP pBpPrev = NULL;
    PDBGCBP pBp = pDbgc->pFirstBp;
    for (; pBp; pBp = pBp->pNext)
    {
        if (pBp->iBp == iBp)
        {
            if (pBpPrev)
                pBpPrev->pNext = pBp->pNext;
            else
                pDbgc->pFirstBp = pBp->pNext;
            RTMemFree(pBp);
            return VINF_SUCCESS;
        }
        pBpPrev = pBp;
    }

    return VERR_DBGC_BP_NOT_FOUND;
}


/**
 * Get a breakpoint.
 *
 * @returns Pointer to the breakpoint.
 * @returns NULL if the breakpoint wasn't found.
 * @param   pDbgc       The DBGC instance.
 * @param   iBp         The breakpoint to get.
 */
PDBGCBP dbgcBpGet(PDBGC pDbgc, RTUINT iBp)
{
    /*
     * Enumerate the list.
     */
    PDBGCBP pBp = pDbgc->pFirstBp;
    for (; pBp; pBp = pBp->pNext)
        if (pBp->iBp == iBp)
            return pBp;
    return NULL;
}


/**
 * Executes the command of a breakpoint.
 *
 * @returns VINF_DBGC_BP_NO_COMMAND if there is no command associated with the breakpoint.
 * @returns VERR_DBGC_BP_NOT_FOUND if the breakpoint wasn't found.
 * @returns VERR_BUFFER_OVERFLOW if the is not enough space in the scratch buffer for the command.
 * @returns VBox status code from dbgcEvalCommand() otherwise.
 * @param   pDbgc       The DBGC instance.
 * @param   iBp         The breakpoint to execute.
 */
int dbgcBpExec(PDBGC pDbgc, RTUINT iBp)
{
    /*
     * Find the breakpoint.
     */
    PDBGCBP pBp = dbgcBpGet(pDbgc, iBp);
    if (!pBp)
        return VERR_DBGC_BP_NOT_FOUND;

    /*
     * Anything to do?
     */
    if (!pBp->cchCmd)
        return VINF_DBGC_BP_NO_COMMAND;

    /*
     * Execute the command.
     * This means copying it to the scratch buffer and process it as if it
     * were user input. We must save and restore the state of the scratch buffer.
     */
    /* Save the scratch state. */
    char       *pszScratch  = pDbgc->pszScratch;
    unsigned    iArg        = pDbgc->iArg;

    /* Copy the command to the scratch buffer. */
    size_t cbScratch = sizeof(pDbgc->achScratch) - (pDbgc->pszScratch - &pDbgc->achScratch[0]);
    if (pBp->cchCmd >= cbScratch)
        return VERR_BUFFER_OVERFLOW;
    memcpy(pDbgc->pszScratch, pBp->szCmd, pBp->cchCmd + 1);

    /* Execute the command. */
    pDbgc->pszScratch = pDbgc->pszScratch + pBp->cchCmd + 1;
    int rc = dbgcEvalCommands(pDbgc, pszScratch, pBp->cchCmd, false /* fNoExecute */);

    /* Restore the scratch state. */
    pDbgc->iArg         = iArg;
    pDbgc->pszScratch   = pszScratch;

    return rc;
}




//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//
//
//
//      F l o w T r a c e   M a n a g e m e n t
//
//
//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//



/**
 * Returns the trace flow module matching the given id or NULL if not found.
 *
 * @returns Pointer to the trace flow module or NULL if not found.
 * @param   pDbgc         The DBGC instance.
 * @param   iTraceFlowMod The trace flow module identifier.
 */
DECLHIDDEN(PDBGCTFLOW) dbgcFlowTraceModGet(PDBGC pDbgc, uint32_t iTraceFlowMod)
{
    PDBGCTFLOW pIt;
    RTListForEach(&pDbgc->LstTraceFlowMods, pIt, DBGCTFLOW, NdTraceFlow)
    {
        if (pIt->iTraceFlowMod == iTraceFlowMod)
            return pIt;
    }

    return NULL;
}


/**
 * Inserts the given trace flow module into the list.
 *
 * @param   pDbgc         The DBGC instance.
 * @param   pTraceFlow    The trace flow module.
 */
static void dbgcFlowTraceModInsert(PDBGC pDbgc, PDBGCTFLOW pTraceFlow)
{
    PDBGCTFLOW pIt = RTListGetLast(&pDbgc->LstTraceFlowMods, DBGCTFLOW, NdTraceFlow);

    if (   !pIt
        || pIt->iTraceFlowMod < pTraceFlow->iTraceFlowMod)
        RTListAppend(&pDbgc->LstTraceFlowMods, &pTraceFlow->NdTraceFlow);
    else
    {
        RTListForEach(&pDbgc->LstTraceFlowMods, pIt, DBGCTFLOW, NdTraceFlow)
        {
            if (pIt->iTraceFlowMod < pTraceFlow->iTraceFlowMod)
            {
                RTListNodeInsertBefore(&pIt->NdTraceFlow, &pTraceFlow->NdTraceFlow);
                break;
            }
        }
    }
}


/**
 * Returns the smallest free flow trace mod identifier.
 *
 * @returns Free flow trace mod identifier.
 * @param   pDbgc         The DBGC instance.
 */
static uint32_t dbgcFlowTraceModIdFindFree(PDBGC pDbgc)
{
    uint32_t iId = 0;

    PDBGCTFLOW pIt;
    RTListForEach(&pDbgc->LstTraceFlowMods, pIt, DBGCTFLOW, NdTraceFlow)
    {
        PDBGCTFLOW pNext = RTListGetNext(&pDbgc->LstTraceFlowMods, pIt, DBGCTFLOW, NdTraceFlow);
        if (   (   pNext
                && pIt->iTraceFlowMod + 1 != pNext->iTraceFlowMod)
            || !pNext)
        {
            iId = pIt->iTraceFlowMod + 1;
            break;
        }
    }

    return iId;
}


/**
 * Adds a flow trace module to the debugger console.
 *
 * @returns VBox status code.
 * @param   pDbgc         The DBGC instance.
 * @param   hFlowTraceMod The flow trace module to add.
 * @param   hFlow         The control flow graph to add.
 * @param   piId          Where to store the ID of the module on success.
 */
DECLHIDDEN(int) dbgcFlowTraceModAdd(PDBGC pDbgc, DBGFFLOWTRACEMOD hFlowTraceMod, DBGFFLOW hFlow, uint32_t *piId)
{
    /*
     * Add the module.
     */
    PDBGCTFLOW pTraceFlow = (PDBGCTFLOW)RTMemAlloc(sizeof(DBGCTFLOW));
    if (!pTraceFlow)
        return VERR_NO_MEMORY;

    pTraceFlow->hTraceFlowMod = hFlowTraceMod;
    pTraceFlow->hFlow         = hFlow;
    pTraceFlow->iTraceFlowMod = dbgcFlowTraceModIdFindFree(pDbgc);
    dbgcFlowTraceModInsert(pDbgc, pTraceFlow);

    *piId = pTraceFlow->iTraceFlowMod;

    return VINF_SUCCESS;
}


/**
 * Deletes a breakpoint.
 *
 * @returns VBox status code.
 * @param   pDbgc       The DBGC instance.
 * @param   iTraceFlowMod The trace flow module identifier.
 */
DECLHIDDEN(int) dbgcFlowTraceModDelete(PDBGC pDbgc, uint32_t iTraceFlowMod)
{
    int rc = VINF_SUCCESS;
    PDBGCTFLOW pTraceFlow = dbgcFlowTraceModGet(pDbgc, iTraceFlowMod);
    if (pTraceFlow)
    {
        RTListNodeRemove(&pTraceFlow->NdTraceFlow);
        RTMemFree(pTraceFlow);
    }
    else
        rc = VERR_DBGC_BP_NOT_FOUND;

    return rc;
}

