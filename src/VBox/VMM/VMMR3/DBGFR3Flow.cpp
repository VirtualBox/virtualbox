/* $Id: DBGFR3Flow.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Control Flow Graph Interface (CFG).
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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


/** @page pg_dbgf_cfg    DBGFR3Flow - Control Flow Graph Interface
 *
 * The control flow graph interface provides an API to disassemble
 * guest code providing the result in a control flow graph.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgf.h>
#include "DBGFInternal.h"
#include <VBox/vmm/mm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vm.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/param.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/sort.h>
#include <iprt/strcache.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Internal control flow graph state.
 */
typedef struct DBGFFLOWINT
{
    /** Reference counter. */
    uint32_t volatile       cRefs;
    /** Internal reference counter for basic blocks. */
    uint32_t volatile       cRefsBb;
    /** Flags during creation. */
    uint32_t                fFlags;
    /** List of all basic blocks. */
    RTLISTANCHOR            LstFlowBb;
    /** List of identified branch tables. */
    RTLISTANCHOR            LstBranchTbl;
    /** Number of basic blocks in this control flow graph. */
    uint32_t                cBbs;
    /** Number of branch tables in this control flow graph. */
    uint32_t                cBranchTbls;
    /** Number of call instructions in this control flow graph. */
    uint32_t                cCallInsns;
    /** The lowest addres of a basic block. */
    DBGFADDRESS             AddrLowest;
    /** The highest address of a basic block. */
    DBGFADDRESS             AddrHighest;
    /** String cache for disassembled instructions. */
    RTSTRCACHE              hStrCacheInstr;
} DBGFFLOWINT;
/** Pointer to an internal control flow graph state. */
typedef DBGFFLOWINT *PDBGFFLOWINT;

/**
 * Instruction record
 */
typedef struct DBGFFLOWBBINSTR
{
    /** Instruction address. */
    DBGFADDRESS             AddrInstr;
    /** Size of instruction. */
    uint32_t                cbInstr;
    /** Disassembled instruction string. */
    const char              *pszInstr;
} DBGFFLOWBBINSTR;
/** Pointer to an instruction record. */
typedef DBGFFLOWBBINSTR *PDBGFFLOWBBINSTR;


/**
 * A branch table identified by the graph processor.
 */
typedef struct DBGFFLOWBRANCHTBLINT
{
    /** Node for the list of branch tables. */
    RTLISTNODE              NdBranchTbl;
    /** The owning control flow graph. */
    PDBGFFLOWINT            pFlow;
    /** Reference counter. */
    uint32_t volatile       cRefs;
    /** The general register index holding the bracnh table base. */
    uint8_t                 idxGenRegBase;
    /** Start address of the branch table. */
    DBGFADDRESS             AddrStart;
    /** Number of valid entries in the branch table. */
    uint32_t                cSlots;
    /** The addresses contained in the branch table - variable in size. */
    DBGFADDRESS             aAddresses[1];
} DBGFFLOWBRANCHTBLINT;
/** Pointer to a branch table structure. */
typedef DBGFFLOWBRANCHTBLINT *PDBGFFLOWBRANCHTBLINT;


/**
 * Internal control flow graph basic block state.
 */
typedef struct DBGFFLOWBBINT
{
    /** Node for the list of all basic blocks. */
    RTLISTNODE               NdFlowBb;
    /** The control flow graph the basic block belongs to. */
    PDBGFFLOWINT             pFlow;
    /** Reference counter. */
    uint32_t volatile        cRefs;
    /** Basic block end type. */
    DBGFFLOWBBENDTYPE        enmEndType;
    /** Start address of this basic block. */
    DBGFADDRESS              AddrStart;
    /** End address of this basic block. */
    DBGFADDRESS              AddrEnd;
    /** Address of the block succeeding.
     *  This is valid for conditional jumps
     * (the other target is referenced by AddrEnd+1) and
     * unconditional jumps (not ret, iret, etc.) except
     * if we can't infer the jump target (jmp *eax for example). */
    DBGFADDRESS              AddrTarget;
    /** The indirect branch table identified for indirect branches. */
    PDBGFFLOWBRANCHTBLINT    pFlowBranchTbl;
    /** Last status error code if DBGF_FLOW_BB_F_INCOMPLETE_ERR is set. */
    int                      rcError;
    /** Error message if DBGF_FLOW_BB_F_INCOMPLETE_ERR is set. */
    char                     *pszErr;
    /** Flags for this basic block. */
    uint32_t                 fFlags;
    /** Number of instructions in this basic block. */
    uint32_t                 cInstr;
    /** Maximum number of instruction records for this basic block. */
    uint32_t                 cInstrMax;
    /** Instruction records, variable in size. */
    DBGFFLOWBBINSTR          aInstr[1];
} DBGFFLOWBBINT;
/** Pointer to an internal control flow graph basic block state. */
typedef DBGFFLOWBBINT *PDBGFFLOWBBINT;


/**
 * Control flow graph iterator state.
 */
typedef struct DBGFFLOWITINT
{
    /** Pointer to the control flow graph (holding a reference). */
    PDBGFFLOWINT             pFlow;
    /** Next basic block to return. */
    uint32_t                 idxBbNext;
    /** Array of basic blocks sorted by the specified order - variable in size. */
    PDBGFFLOWBBINT           apBb[1];
} DBGFFLOWITINT;
/** Pointer to the internal control flow graph iterator state. */
typedef DBGFFLOWITINT *PDBGFFLOWITINT;


/**
 * Control flow graph branch table iterator state.
 */
typedef struct DBGFFLOWBRANCHTBLITINT
{
    /** Pointer to the control flow graph (holding a reference). */
    PDBGFFLOWINT             pFlow;
    /** Next branch table to return. */
    uint32_t                 idxTblNext;
    /** Array of branch table pointers sorted by the specified order - variable in size. */
    PDBGFFLOWBRANCHTBLINT    apBranchTbl[1];
} DBGFFLOWBRANCHTBLITINT;
/** Pointer to the internal control flow graph branch table iterator state. */
typedef DBGFFLOWBRANCHTBLITINT *PDBGFFLOWBRANCHTBLITINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

static uint32_t dbgfR3FlowBbReleaseInt(PDBGFFLOWBBINT pFlowBb, bool fMayDestroyFlow);
static void dbgfR3FlowBranchTblDestroy(PDBGFFLOWBRANCHTBLINT pFlowBranchTbl);


/**
 * Checks whether both addresses are equal.
 *
 * @returns true if both addresses point to the same location, false otherwise.
 * @param   pAddr1              First address.
 * @param   pAddr2              Second address.
 */
static bool dbgfR3FlowAddrEqual(PDBGFADDRESS pAddr1, PDBGFADDRESS pAddr2)
{
    return    pAddr1->Sel == pAddr2->Sel
           && pAddr1->off == pAddr2->off;
}


/**
 * Checks whether the first given address is lower than the second one.
 *
 * @returns true if both addresses point to the same location, false otherwise.
 * @param   pAddr1              First address.
 * @param   pAddr2              Second address.
 */
static bool dbgfR3FlowAddrLower(PDBGFADDRESS pAddr1, PDBGFADDRESS pAddr2)
{
    return    pAddr1->Sel == pAddr2->Sel
           && pAddr1->off < pAddr2->off;
}


/**
 * Checks whether the given basic block and address intersect.
 *
 * @returns true if they intersect, false otherwise.
 * @param   pFlowBb             The basic block to check.
 * @param   pAddr               The address to check for.
 */
static bool dbgfR3FlowAddrIntersect(PDBGFFLOWBBINT pFlowBb, PDBGFADDRESS pAddr)
{
    return    (pFlowBb->AddrStart.Sel == pAddr->Sel)
           && (pFlowBb->AddrStart.off <= pAddr->off)
           && (pFlowBb->AddrEnd.off >= pAddr->off);
}


/**
 * Returns the distance of the two given addresses.
 *
 * @returns Distance of the addresses.
 * @param   pAddr1              The first address.
 * @param   pAddr2              The second address.
 */
static RTGCUINTPTR dbgfR3FlowAddrGetDistance(PDBGFADDRESS pAddr1, PDBGFADDRESS pAddr2)
{
    if (pAddr1->Sel == pAddr2->Sel)
    {
        if (pAddr1->off >= pAddr2->off)
            return pAddr1->off - pAddr2->off;
        else
            return pAddr2->off - pAddr1->off;
    }
    else
        AssertFailed();

    return 0;
}


/**
 * Creates a new basic block.
 *
 * @returns Pointer to the basic block on success or NULL if out of memory.
 * @param   pThis               The control flow graph.
 * @param   pAddrStart          The start of the basic block.
 * @param   fFlowBbFlags        Additional flags for this bascic block.
 * @param   cInstrMax           Maximum number of instructions this block can hold initially.
 */
static PDBGFFLOWBBINT dbgfR3FlowBbCreate(PDBGFFLOWINT pThis, PDBGFADDRESS pAddrStart, uint32_t fFlowBbFlags,
                                         uint32_t cInstrMax)
{
    PDBGFFLOWBBINT pFlowBb = (PDBGFFLOWBBINT)RTMemAllocZ(RT_UOFFSETOF_DYN(DBGFFLOWBBINT, aInstr[cInstrMax]));
    if (RT_LIKELY(pFlowBb))
    {
        RTListInit(&pFlowBb->NdFlowBb);
        pFlowBb->cRefs          = 1;
        pFlowBb->enmEndType     = DBGFFLOWBBENDTYPE_INVALID;
        pFlowBb->pFlow          = pThis;
        pFlowBb->fFlags         = DBGF_FLOW_BB_F_EMPTY | fFlowBbFlags;
        pFlowBb->AddrStart      = *pAddrStart;
        pFlowBb->AddrEnd        = *pAddrStart;
        pFlowBb->rcError        = VINF_SUCCESS;
        pFlowBb->pszErr         = NULL;
        pFlowBb->cInstr         = 0;
        pFlowBb->cInstrMax      = cInstrMax;
        pFlowBb->pFlowBranchTbl = NULL;
        ASMAtomicIncU32(&pThis->cRefsBb);
    }

    return pFlowBb;
}


/**
 * Creates an empty branch table with the given size.
 *
 * @returns Pointer to the empty branch table on success or NULL if out of memory.
 * @param   pThis               The control flow graph.
 * @param   pAddrStart          The start of the branch table.
 * @param   idxGenRegBase       The general register index holding the base address.
 * @param   cSlots              Number of slots the table has.
 */
static PDBGFFLOWBRANCHTBLINT
dbgfR3FlowBranchTblCreate(PDBGFFLOWINT pThis, PDBGFADDRESS pAddrStart, uint8_t idxGenRegBase,  uint32_t cSlots)
{
    PDBGFFLOWBRANCHTBLINT pBranchTbl = (PDBGFFLOWBRANCHTBLINT)RTMemAllocZ(RT_UOFFSETOF_DYN(DBGFFLOWBRANCHTBLINT,
                                                                                           aAddresses[cSlots]));
    if (RT_LIKELY(pBranchTbl))
    {
        RTListInit(&pBranchTbl->NdBranchTbl);
        pBranchTbl->pFlow         = pThis;
        pBranchTbl->idxGenRegBase = idxGenRegBase;
        pBranchTbl->AddrStart     = *pAddrStart;
        pBranchTbl->cSlots        = cSlots;
        pBranchTbl->cRefs         = 1;
    }

    return pBranchTbl;
}


/**
 * Destroys a control flow graph.
 *
 * @param   pThis               The control flow graph to destroy.
 */
static void dbgfR3FlowDestroy(PDBGFFLOWINT pThis)
{
    /* Defer destruction if there are still basic blocks referencing us. */
    PDBGFFLOWBBINT pFlowBb;
    PDBGFFLOWBBINT pFlowBbNext;
    RTListForEachSafe(&pThis->LstFlowBb, pFlowBb, pFlowBbNext, DBGFFLOWBBINT, NdFlowBb)
    {
        dbgfR3FlowBbReleaseInt(pFlowBb, false /*fMayDestroyFlow*/);
    }

    Assert(!pThis->cRefs);
    if (!pThis->cRefsBb)
    {
        /* Destroy the branch tables. */
        PDBGFFLOWBRANCHTBLINT pTbl = NULL;
        PDBGFFLOWBRANCHTBLINT pTblNext = NULL;
        RTListForEachSafe(&pThis->LstBranchTbl, pTbl, pTblNext, DBGFFLOWBRANCHTBLINT, NdBranchTbl)
        {
            dbgfR3FlowBranchTblDestroy(pTbl);
        }

        RTStrCacheDestroy(pThis->hStrCacheInstr);
        RTMemFree(pThis);
    }
}


/**
 * Destroys a basic block.
 *
 * @param   pFlowBb              The basic block to destroy.
 * @param   fMayDestroyFlow      Flag whether the control flow graph container
 *                               should be destroyed when there is nothing referencing it.
 */
static void dbgfR3FlowBbDestroy(PDBGFFLOWBBINT pFlowBb, bool fMayDestroyFlow)
{
    PDBGFFLOWINT pThis = pFlowBb->pFlow;

    RTListNodeRemove(&pFlowBb->NdFlowBb);
    pThis->cBbs--;
    for (uint32_t idxInstr = 0; idxInstr < pFlowBb->cInstr; idxInstr++)
        RTStrCacheRelease(pThis->hStrCacheInstr, pFlowBb->aInstr[idxInstr].pszInstr);
    uint32_t cRefsBb = ASMAtomicDecU32(&pThis->cRefsBb);
    RTMemFree(pFlowBb);

    if (!cRefsBb && !pThis->cRefs && fMayDestroyFlow)
        dbgfR3FlowDestroy(pThis);
}


/**
 * Destroys a given branch table.
 *
 * @param   pFlowBranchTbl      The flow branch table to destroy.
 */
static void dbgfR3FlowBranchTblDestroy(PDBGFFLOWBRANCHTBLINT pFlowBranchTbl)
{
    RTListNodeRemove(&pFlowBranchTbl->NdBranchTbl);
    RTMemFree(pFlowBranchTbl);
}


/**
 * Internal basic block release worker.
 *
 * @returns New reference count of the released basic block, on 0
 *          it is destroyed.
 * @param   pFlowBb              The basic block to release.
 * @param   fMayDestroyFlow      Flag whether the control flow graph container
 *                               should be destroyed when there is nothing referencing it.
 */
static uint32_t dbgfR3FlowBbReleaseInt(PDBGFFLOWBBINT pFlowBb, bool fMayDestroyFlow)
{
    uint32_t cRefs = ASMAtomicDecU32(&pFlowBb->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p %d\n", cRefs, pFlowBb, pFlowBb->enmEndType));
    if (cRefs == 0)
        dbgfR3FlowBbDestroy(pFlowBb, fMayDestroyFlow);
    return cRefs;
}


/**
 * Links the given basic block into the control flow graph.
 *
 * @param   pThis               The control flow graph to link into.
 * @param   pFlowBb             The basic block to link.
 */
DECLINLINE(void) dbgfR3FlowLink(PDBGFFLOWINT pThis, PDBGFFLOWBBINT pFlowBb)
{
    RTListAppend(&pThis->LstFlowBb, &pFlowBb->NdFlowBb);
    pThis->cBbs++;
}


/**
 * Links the given branch table into the control flow graph.
 *
 * @param   pThis               The control flow graph to link into.
 * @param   pBranchTbl          The branch table to link.
 */
DECLINLINE(void) dbgfR3FlowBranchTblLink(PDBGFFLOWINT pThis, PDBGFFLOWBRANCHTBLINT pBranchTbl)
{
    RTListAppend(&pThis->LstBranchTbl, &pBranchTbl->NdBranchTbl);
    pThis->cBranchTbls++;
}


/**
 * Returns the first unpopulated basic block of the given control flow graph.
 *
 * @returns The first unpopulated control flow graph or NULL if not found.
 * @param   pThis               The control flow graph.
 */
DECLINLINE(PDBGFFLOWBBINT) dbgfR3FlowGetUnpopulatedBb(PDBGFFLOWINT pThis)
{
    PDBGFFLOWBBINT pFlowBb;
    RTListForEach(&pThis->LstFlowBb, pFlowBb, DBGFFLOWBBINT, NdFlowBb)
    {
        if (pFlowBb->fFlags & DBGF_FLOW_BB_F_EMPTY)
            return pFlowBb;
    }

    return NULL;
}


/**
 * Returns the branch table with the given address if it exists.
 *
 * @returns Pointer to the branch table record or NULL if not found.
 * @param   pThis               The control flow graph.
 * @param   pAddrTbl            The branch table address.
 */
DECLINLINE(PDBGFFLOWBRANCHTBLINT) dbgfR3FlowBranchTblFindByAddr(PDBGFFLOWINT pThis, PDBGFADDRESS pAddrTbl)
{
    PDBGFFLOWBRANCHTBLINT pTbl;
    RTListForEach(&pThis->LstBranchTbl, pTbl, DBGFFLOWBRANCHTBLINT, NdBranchTbl)
    {
        if (dbgfR3FlowAddrEqual(&pTbl->AddrStart, pAddrTbl))
            return pTbl;
    }

    return NULL;
}


/**
 * Sets the given error status for the basic block.
 *
 * @param   pFlowBb              The basic block causing the error.
 * @param   rcError             The error to set.
 * @param   pszFmt              Format string of the error description.
 * @param   ...                 Arguments for the format string.
 */
static void dbgfR3FlowBbSetError(PDBGFFLOWBBINT pFlowBb, int rcError, const char *pszFmt, ...)
{
    va_list va;
    va_start(va, pszFmt);

    Assert(!(pFlowBb->fFlags & DBGF_FLOW_BB_F_INCOMPLETE_ERR));
    pFlowBb->fFlags |= DBGF_FLOW_BB_F_INCOMPLETE_ERR;
    pFlowBb->fFlags &= ~DBGF_FLOW_BB_F_EMPTY;
    pFlowBb->rcError = rcError;
    pFlowBb->pszErr = RTStrAPrintf2V(pszFmt, va);
    va_end(va);
}


/**
 * Checks whether the given control flow graph contains a basic block
 * with the given start address.
 *
 * @returns true if there is a basic block with the start address, false otherwise.
 * @param   pThis               The control flow graph.
 * @param   pAddr               The address to check for.
 */
static bool dbgfR3FlowHasBbWithStartAddr(PDBGFFLOWINT pThis, PDBGFADDRESS pAddr)
{
    PDBGFFLOWBBINT pFlowBb;
    RTListForEach(&pThis->LstFlowBb, pFlowBb, DBGFFLOWBBINT, NdFlowBb)
    {
        if (dbgfR3FlowAddrEqual(&pFlowBb->AddrStart, pAddr))
            return true;
    }
    return false;
}


/**
 * Splits a given basic block into two at the given address.
 *
 * @returns VBox status code.
 * @param   pThis               The control flow graph.
 * @param   pFlowBb              The basic block to split.
 * @param   pAddr               The address to split at.
 */
static int dbgfR3FlowBbSplit(PDBGFFLOWINT pThis, PDBGFFLOWBBINT pFlowBb, PDBGFADDRESS pAddr)
{
    int rc = VINF_SUCCESS;
    uint32_t idxInstrSplit;

    /* If the block is empty it will get populated later so there is nothing to split,
     * same if the start address equals. */
    if (   pFlowBb->fFlags & DBGF_FLOW_BB_F_EMPTY
        || dbgfR3FlowAddrEqual(&pFlowBb->AddrStart, pAddr))
        return VINF_SUCCESS;

    /* Find the instruction to split at. */
    for (idxInstrSplit = 1; idxInstrSplit < pFlowBb->cInstr; idxInstrSplit++)
        if (dbgfR3FlowAddrEqual(&pFlowBb->aInstr[idxInstrSplit].AddrInstr, pAddr))
            break;

    Assert(idxInstrSplit > 0);

    /*
     * Given address might not be on instruction boundary, this is not supported
     * so far and results in an error.
     */
    if (idxInstrSplit < pFlowBb->cInstr)
    {
        /* Create new basic block. */
        uint32_t cInstrNew = pFlowBb->cInstr - idxInstrSplit;
        PDBGFFLOWBBINT pFlowBbNew = dbgfR3FlowBbCreate(pThis, &pFlowBb->aInstr[idxInstrSplit].AddrInstr,
                                                       0 /*fFlowBbFlags*/, cInstrNew);
        if (pFlowBbNew)
        {
            /* Move instructions over. */
            pFlowBbNew->cInstr         = cInstrNew;
            pFlowBbNew->AddrEnd        = pFlowBb->AddrEnd;
            pFlowBbNew->enmEndType     = pFlowBb->enmEndType;
            pFlowBbNew->AddrTarget     = pFlowBb->AddrTarget;
            pFlowBbNew->fFlags         = pFlowBb->fFlags & ~DBGF_FLOW_BB_F_ENTRY;
            pFlowBbNew->pFlowBranchTbl = pFlowBb->pFlowBranchTbl;
            pFlowBb->pFlowBranchTbl    = NULL;

            /* Move any error to the new basic block and clear them in the old basic block. */
            pFlowBbNew->rcError    = pFlowBb->rcError;
            pFlowBbNew->pszErr     = pFlowBb->pszErr;
            pFlowBb->rcError       = VINF_SUCCESS;
            pFlowBb->pszErr        = NULL;
            pFlowBb->fFlags       &= ~DBGF_FLOW_BB_F_INCOMPLETE_ERR;

            memcpy(&pFlowBbNew->aInstr[0], &pFlowBb->aInstr[idxInstrSplit], cInstrNew * sizeof(DBGFFLOWBBINSTR));
            pFlowBb->cInstr     = idxInstrSplit;
            pFlowBb->enmEndType = DBGFFLOWBBENDTYPE_UNCOND;
            pFlowBb->AddrEnd    = pFlowBb->aInstr[idxInstrSplit-1].AddrInstr;
            pFlowBb->AddrTarget = pFlowBbNew->AddrStart;
            DBGFR3AddrAdd(&pFlowBb->AddrEnd, pFlowBb->aInstr[idxInstrSplit-1].cbInstr - 1);
            RT_BZERO(&pFlowBb->aInstr[idxInstrSplit], cInstrNew * sizeof(DBGFFLOWBBINSTR));

            dbgfR3FlowLink(pThis, pFlowBbNew);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        AssertFailedStmt(rc = VERR_INVALID_STATE); /** @todo Proper status code. */

    return rc;
}


/**
 * Makes sure there is an successor at the given address splitting already existing
 * basic blocks if they intersect.
 *
 * @returns VBox status code.
 * @param   pThis               The control flow graph.
 * @param   pAddrSucc           The guest address the new successor should start at.
 * @param   fNewBbFlags         Flags for the new basic block.
 * @param   pBranchTbl          Branch table candidate for this basic block.
 */
static int dbgfR3FlowBbSuccessorAdd(PDBGFFLOWINT pThis, PDBGFADDRESS pAddrSucc,
                                    uint32_t fNewBbFlags, PDBGFFLOWBRANCHTBLINT pBranchTbl)
{
    PDBGFFLOWBBINT pFlowBb;
    RTListForEach(&pThis->LstFlowBb, pFlowBb, DBGFFLOWBBINT, NdFlowBb)
    {
        /*
         * The basic block must be split if it intersects with the given address
         * and the start address does not equal the given one.
         */
        if (dbgfR3FlowAddrIntersect(pFlowBb, pAddrSucc))
            return dbgfR3FlowBbSplit(pThis, pFlowBb, pAddrSucc);
    }

    int rc = VINF_SUCCESS;
    pFlowBb = dbgfR3FlowBbCreate(pThis, pAddrSucc, fNewBbFlags, 10);
    if (pFlowBb)
    {
        pFlowBb->pFlowBranchTbl = pBranchTbl;
        dbgfR3FlowLink(pThis, pFlowBb);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Returns whether the parameter indicates an indirect branch.
 *
 * @returns Flag whether this is an indirect branch.
 * @param   pDisParam           The parameter from the disassembler.
 */
DECLINLINE(bool) dbgfR3FlowBranchTargetIsIndirect(PDISOPPARAM pDisParam)
{
    bool fIndirect = true;

    if (   pDisParam->fUse & (DISUSE_IMMEDIATE8 | DISUSE_IMMEDIATE16 | DISUSE_IMMEDIATE32 | DISUSE_IMMEDIATE64)
        || pDisParam->fUse & (DISUSE_IMMEDIATE8_REL | DISUSE_IMMEDIATE16_REL | DISUSE_IMMEDIATE32_REL | DISUSE_IMMEDIATE64_REL))
        fIndirect = false;

    return fIndirect;
}


/**
 * Resolves the direct branch target address if possible from the given instruction address
 * and instruction parameter.
 *
 * @returns VBox status code.
 * @param   pUVM                The usermode VM handle.
 * @param   idCpu               CPU id for resolving the address.
 * @param   pDisParam           The parameter from the disassembler.
 * @param   pAddrInstr          The instruction address.
 * @param   cbInstr             Size of instruction in bytes.
 * @param   fRelJmp             Flag whether this is a reltive jump.
 * @param   pAddrJmpTarget      Where to store the address to the jump target on success.
 */
static int dbgfR3FlowQueryDirectBranchTarget(PUVM pUVM, VMCPUID idCpu, PDISOPPARAM pDisParam, PDBGFADDRESS pAddrInstr,
                                             uint32_t cbInstr, bool fRelJmp, PDBGFADDRESS pAddrJmpTarget)
{
    int rc = VINF_SUCCESS;

    Assert(!dbgfR3FlowBranchTargetIsIndirect(pDisParam));

    /* Relative jumps are always from the beginning of the next instruction. */
    *pAddrJmpTarget = *pAddrInstr;
    DBGFR3AddrAdd(pAddrJmpTarget, cbInstr);

    if (fRelJmp)
    {
        RTGCINTPTR iRel = 0;
        if (pDisParam->fUse & DISUSE_IMMEDIATE8_REL)
            iRel = (int8_t)pDisParam->uValue;
        else if (pDisParam->fUse & DISUSE_IMMEDIATE16_REL)
            iRel = (int16_t)pDisParam->uValue;
        else if (pDisParam->fUse & DISUSE_IMMEDIATE32_REL)
            iRel = (int32_t)pDisParam->uValue;
        else if (pDisParam->fUse & DISUSE_IMMEDIATE64_REL)
            iRel = (int64_t)pDisParam->uValue;
        else
            AssertFailedStmt(rc = VERR_NOT_SUPPORTED);

        if (iRel < 0)
            DBGFR3AddrSub(pAddrJmpTarget, -iRel);
        else
            DBGFR3AddrAdd(pAddrJmpTarget, iRel);
    }
    else
    {
        if (pDisParam->fUse & (DISUSE_IMMEDIATE8 | DISUSE_IMMEDIATE16 | DISUSE_IMMEDIATE32 | DISUSE_IMMEDIATE64))
        {
            if (DBGFADDRESS_IS_FLAT(pAddrInstr))
                DBGFR3AddrFromFlat(pUVM, pAddrJmpTarget, pDisParam->uValue);
            else
                DBGFR3AddrFromSelOff(pUVM, idCpu, pAddrJmpTarget, pAddrInstr->Sel, pDisParam->uValue);
        }
        else
            AssertFailedStmt(rc = VERR_INVALID_STATE);
    }

    return rc;
}


#if 0 /* unused */
/**
 * Returns the CPU mode based on the given assembler flags.
 *
 * @returns CPU mode.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 * @param   fFlagsDisasm        The flags used for disassembling.
 */
static CPUMMODE dbgfR3FlowGetDisasCpuMode(PUVM pUVM, VMCPUID idCpu, uint32_t fFlagsDisasm)
{
    CPUMMODE enmMode = CPUMMODE_INVALID;
    uint32_t fDisasMode = fFlagsDisasm & DBGF_DISAS_FLAGS_MODE_MASK;
    if (fDisasMode == DBGF_DISAS_FLAGS_DEFAULT_MODE)
        enmMode = DBGFR3CpuGetMode(pUVM, idCpu);
    else if (   fDisasMode == DBGF_DISAS_FLAGS_16BIT_MODE
             || fDisasMode == DBGF_DISAS_FLAGS_16BIT_REAL_MODE)
        enmMode = CPUMMODE_REAL;
    else if (fDisasMode == DBGF_DISAS_FLAGS_32BIT_MODE)
        enmMode = CPUMMODE_PROTECTED;
    else if (fDisasMode == DBGF_DISAS_FLAGS_64BIT_MODE)
        enmMode = CPUMMODE_LONG;
    else
        AssertFailed();

    return enmMode;
}
#endif


/**
 * Returns the (PC) pointer size based on given assembler flags.
 *
 * @returns 2, 4, or 8.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 * @param   fFlagsDisasm        The flags used for disassembling.
 */
DECLINLINE(uint32_t) dbgfR3FlowGetDisasPtrSize(PUVM pUVM, VMCPUID idCpu, uint32_t fFlagsDisasm)
{
    switch (fFlagsDisasm & DBGF_DISAS_FLAGS_MODE_MASK)
    {
        case DBGF_DISAS_FLAGS_16BIT_MODE:
        case DBGF_DISAS_FLAGS_16BIT_REAL_MODE:
            return sizeof(uint16_t);
        case DBGF_DISAS_FLAGS_32BIT_MODE:
            return sizeof(uint32_t);
        case DBGF_DISAS_FLAGS_64BIT_MODE:
            return sizeof(uint64_t);
        default:
            AssertMsgFailed(("%#x\n", fFlagsDisasm));
            RT_FALL_THRU();
        case DBGF_DISAS_FLAGS_DEFAULT_MODE:
        {
            CPUMMODE const enmMode = DBGFR3CpuGetMode(pUVM, idCpu);
            switch (enmMode)
            {
                case CPUMMODE_REAL:
                    return sizeof(uint16_t);
                case CPUMMODE_PROTECTED:
                case CPUMMODE_ARMV8_AARCH32:
                    return sizeof(uint32_t);
                default:
                    AssertFailed();
                    RT_FALL_THRU();
                case CPUMMODE_LONG:
                case CPUMMODE_ARMV8_AARCH64:
                    return sizeof(uint64_t);
            }
            break;
        }
    }
}


/**
 * Searches backwards in the given basic block starting the given instruction index for
 * a mov instruction with the given register as the target where the constant looks like
 * a pointer.
 *
 * @returns Flag whether a candidate was found.
 * @param   pFlowBb             The basic block containing the indirect branch.
 * @param   idxRegTgt           The general register the mov targets.
 * @param   cbPtr               The pointer size to look for.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 * @param   fFlagsDisasm        The flags to use for disassembling.
 * @param   pidxInstrStart      The instruction index to start searching for on input,
 *                              The last instruction evaluated on output.
 * @param   pAddrDest           Where to store the candidate address on success.
 */
static bool dbgfR3FlowSearchMovWithConstantPtrSizeBackwards(PDBGFFLOWBBINT pFlowBb, uint8_t idxRegTgt, uint32_t cbPtr,
                                                            PUVM pUVM, VMCPUID idCpu, uint32_t fFlagsDisasm,
                                                            uint32_t *pidxInstrStart, PDBGFADDRESS pAddrDest)
{
    bool fFound = false;
    uint32_t idxInstrCur = *pidxInstrStart;
    uint32_t cInstrCheck = idxInstrCur + 1;

    for (;;)
    {
        /** @todo Avoid to disassemble again. */
        PDBGFFLOWBBINSTR pInstr = &pFlowBb->aInstr[idxInstrCur];
        DBGFDISSTATE DisState;
        char szOutput[_4K];

        int rc = dbgfR3DisasInstrStateEx(pUVM, idCpu, &pInstr->AddrInstr, fFlagsDisasm,
                                         &szOutput[0], sizeof(szOutput), &DisState);
        if (RT_SUCCESS(rc))
        {
            if (   DisState.pCurInstr->uOpcode == OP_MOV
                && (DisState.Param1.fUse & (DISUSE_REG_GEN16 | DISUSE_REG_GEN32 | DISUSE_REG_GEN64))
                && DisState.Param1.x86.Base.idxGenReg == idxRegTgt
                /*&& DisState.Param1.cb == cbPtr*/
                && DisState.Param2.x86.cb == cbPtr
                && (DisState.Param2.fUse & (DISUSE_IMMEDIATE16 | DISUSE_IMMEDIATE32 | DISUSE_IMMEDIATE64)))
            {
                /* Found possible candidate. */
                fFound = true;
                if (DBGFADDRESS_IS_FLAT(&pInstr->AddrInstr))
                    DBGFR3AddrFromFlat(pUVM, pAddrDest, DisState.Param2.uValue);
                else
                    DBGFR3AddrFromSelOff(pUVM, idCpu, pAddrDest, pInstr->AddrInstr.Sel, DisState.Param2.uValue);
                break;
            }
        }
        else
            break;

        cInstrCheck--;
        if (!cInstrCheck)
            break;

        idxInstrCur--;
    }

    *pidxInstrStart = idxInstrCur;
    return fFound;
}


/**
 * Verifies the given branch table candidate and adds it to the control flow graph on success.
 *
 * @returns VBox status code.
 * @param   pThis               The flow control graph.
 * @param   pFlowBb             The basic block causing the indirect branch.
 * @param   pAddrBranchTbl      Address of the branch table location.
 * @param   idxGenRegBase       The general register holding the base address.
 * @param   cbPtr               Guest pointer size.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 *
 * @todo Handle branch tables greater than 4KB (lazy coder).
 */
static int dbgfR3FlowBranchTblVerifyAdd(PDBGFFLOWINT pThis, PDBGFFLOWBBINT pFlowBb, PDBGFADDRESS pAddrBranchTbl,
                                        uint8_t idxGenRegBase, uint32_t cbPtr, PUVM pUVM, VMCPUID idCpu)
{
    int rc = VINF_SUCCESS;
    PDBGFFLOWBRANCHTBLINT pBranchTbl = dbgfR3FlowBranchTblFindByAddr(pThis, pAddrBranchTbl);

    if (!pBranchTbl)
    {
        uint32_t cSlots = 0;
        uint8_t abBuf[_4K];

        rc = DBGFR3MemRead(pUVM, idCpu, pAddrBranchTbl, &abBuf[0], sizeof(abBuf));
        if (RT_SUCCESS(rc))
        {
            uint8_t *pbBuf = &abBuf[0];
            while (pbBuf < &abBuf[0] + sizeof(abBuf))
            {
                DBGFADDRESS AddrDest;
                RTGCUINTPTR GCPtr =   cbPtr == sizeof(uint64_t)
                                    ? *(uint64_t *)pbBuf
                                    : cbPtr == sizeof(uint32_t)
                                    ? *(uint32_t *)pbBuf
                                    : *(uint16_t *)pbBuf;
                pbBuf += cbPtr;

                if (DBGFADDRESS_IS_FLAT(pAddrBranchTbl))
                    DBGFR3AddrFromFlat(pUVM, &AddrDest, GCPtr);
                else
                    DBGFR3AddrFromSelOff(pUVM, idCpu, &AddrDest, pAddrBranchTbl->Sel, GCPtr);

                if (dbgfR3FlowAddrGetDistance(&AddrDest, &pFlowBb->AddrEnd) > _512K)
                    break;

                cSlots++;
            }

            /* If there are any slots use it. */
            if (cSlots)
            {
                pBranchTbl = dbgfR3FlowBranchTblCreate(pThis, pAddrBranchTbl, idxGenRegBase, cSlots);
                if (pBranchTbl)
                {
                    /* Get the addresses. */
                    for (unsigned i = 0; i < cSlots && RT_SUCCESS(rc); i++)
                    {
                        RTGCUINTPTR GCPtr =   cbPtr == sizeof(uint64_t)
                                            ? *(uint64_t *)&abBuf[i * cbPtr]
                                            : cbPtr == sizeof(uint32_t)
                                            ? *(uint32_t *)&abBuf[i * cbPtr]
                                            : *(uint16_t *)&abBuf[i * cbPtr];

                        if (DBGFADDRESS_IS_FLAT(pAddrBranchTbl))
                            DBGFR3AddrFromFlat(pUVM, &pBranchTbl->aAddresses[i], GCPtr);
                        else
                            DBGFR3AddrFromSelOff(pUVM, idCpu, &pBranchTbl->aAddresses[i],
                                                 pAddrBranchTbl->Sel, GCPtr);
                        rc = dbgfR3FlowBbSuccessorAdd(pThis, &pBranchTbl->aAddresses[i], DBGF_FLOW_BB_F_BRANCH_TABLE,
                                                      pBranchTbl);
                    }
                    dbgfR3FlowBranchTblLink(pThis, pBranchTbl);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }
    }

    if (pBranchTbl)
        pFlowBb->pFlowBranchTbl = pBranchTbl;

    return rc;
}


/**
 * Checks whether the location for the branch target candidate contains a valid code address.
 *
 * @returns VBox status code.
 * @param   pThis               The flow control graph.
 * @param   pFlowBb             The basic block causing the indirect branch.
 * @param   pAddrBranchTgt      Address of the branch target location.
 * @param   idxGenRegBase       The general register holding the address of the location.
 * @param   cbPtr               Guest pointer size.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 * @param   fBranchTbl          Flag whether this is a possible branch table containing multiple
 *                              targets.
 */
static int dbgfR3FlowCheckBranchTargetLocation(PDBGFFLOWINT pThis, PDBGFFLOWBBINT pFlowBb, PDBGFADDRESS pAddrBranchTgt,
                                               uint8_t idxGenRegBase, uint32_t cbPtr, PUVM pUVM, VMCPUID idCpu, bool fBranchTbl)
{
    int rc;
    if (!fBranchTbl)
    {
        RTUINT64U uVal = { 0 };
        Assert(cbPtr <= sizeof(uVal));
        rc = DBGFR3MemRead(pUVM, idCpu, pAddrBranchTgt, &uVal, cbPtr); /* (parfait wrong about potential buffer overflow, cbPtr is known) */
        if (RT_SUCCESS(rc))
        {
            DBGFADDRESS AddrTgt;
            RTGCUINTPTR GCPtr =   cbPtr == sizeof(uint64_t) /* (We could use uVal directly on little endian machines.) */
                                ? uVal.au64[0]
                                : cbPtr == sizeof(uint32_t)
                                ? uVal.au32[0]
                                : uVal.au16[0];
            if (DBGFADDRESS_IS_FLAT(pAddrBranchTgt))
                DBGFR3AddrFromFlat(pUVM, &AddrTgt, GCPtr);
            else
                DBGFR3AddrFromSelOff(pUVM, idCpu, &AddrTgt, pAddrBranchTgt->Sel, GCPtr);

            if (dbgfR3FlowAddrGetDistance(&AddrTgt, &pFlowBb->AddrEnd) <= _128K)
            {
                /* Finish the basic block. */
                pFlowBb->AddrTarget = AddrTgt;
                rc = dbgfR3FlowBbSuccessorAdd(pThis, &AddrTgt,
                                              pFlowBb->fFlags & DBGF_FLOW_BB_F_BRANCH_TABLE,
                                              pFlowBb->pFlowBranchTbl);
            }
            else
                rc = VERR_NOT_FOUND;
        }
    }
    else
        rc = dbgfR3FlowBranchTblVerifyAdd(pThis, pFlowBb, pAddrBranchTgt,
                                          idxGenRegBase, cbPtr, pUVM, idCpu);

    return rc;
}


/**
 * Tries to resolve the indirect branch.
 *
 * @returns VBox status code.
 * @param   pThis               The flow control graph.
 * @param   pFlowBb             The basic block causing the indirect branch.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 * @param   pDisParam           The parameter from the disassembler.
 * @param   fFlagsDisasm        Flags for the disassembler.
 */
static int dbgfR3FlowTryResolveIndirectBranch(PDBGFFLOWINT pThis, PDBGFFLOWBBINT pFlowBb, PUVM pUVM,
                                              VMCPUID idCpu, PDISOPPARAM pDisParam, uint32_t fFlagsDisasm)
{
    Assert(dbgfR3FlowBranchTargetIsIndirect(pDisParam));

    if (pDisParam->fUse & DISUSE_BASE)
    {
        uint8_t const idxRegBase = pDisParam->x86.Base.idxGenReg;
        uint32_t const cbPtr = dbgfR3FlowGetDisasPtrSize(pUVM, idCpu, fFlagsDisasm);

        /* Check that the used register size and the pointer size match. */
        if (   ((pDisParam->fUse & DISUSE_REG_GEN16) && cbPtr == sizeof(uint16_t))
            || ((pDisParam->fUse & DISUSE_REG_GEN32) && cbPtr == sizeof(uint32_t))
            || ((pDisParam->fUse & DISUSE_REG_GEN64) && cbPtr == sizeof(uint64_t)))
        {
            /*
             * Search all instructions backwards until a move to the used general register
             * is detected with a constant using the pointer size.
             */
            uint32_t idxInstrStart = pFlowBb->cInstr - 1 - 1; /* Don't look at the branch. */
            bool fCandidateFound = false;
            bool fBranchTbl = RT_BOOL(pDisParam->fUse & DISUSE_INDEX);
            DBGFADDRESS AddrBranchTgt;
            do
            {
                fCandidateFound = dbgfR3FlowSearchMovWithConstantPtrSizeBackwards(pFlowBb, idxRegBase, cbPtr,
                                                                                  pUVM, idCpu, fFlagsDisasm,
                                                                                  &idxInstrStart, &AddrBranchTgt);
                if (fCandidateFound)
                {
                    /* Check that the address is not too far away from the instruction address. */
                    RTGCUINTPTR offPtr = dbgfR3FlowAddrGetDistance(&AddrBranchTgt, &pFlowBb->AddrEnd);
                    if (offPtr <= 20 * _1M)
                    {
                        /* Read the content at the address and check that it is near this basic block too. */
                        int rc = dbgfR3FlowCheckBranchTargetLocation(pThis, pFlowBb, &AddrBranchTgt, idxRegBase,
                                                                     cbPtr, pUVM, idCpu, fBranchTbl);
                        if (RT_SUCCESS(rc))
                            break;
                        fCandidateFound = false;
                    }

                    if (idxInstrStart > 0)
                        idxInstrStart--;
                }
            } while (idxInstrStart > 0 && !fCandidateFound);
        }
        else
            dbgfR3FlowBbSetError(pFlowBb, VERR_INVALID_STATE,
                                 "The base register size and selected pointer size do not match (fUse=%#x cbPtr=%u)",
                                 pDisParam->fUse, cbPtr);
    }

    return VINF_SUCCESS;
}


/**
 * Tries to resolve the indirect branch.
 *
 * @returns VBox status code.
 * @param   pThis               The flow control graph.
 * @param   pFlowBb             The basic block causing the indirect branch.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 * @param   pDisParam           The parameter from the disassembler.
 * @param   fFlagsDisasm        Flags for the disassembler.
 */
static int dbgfR3FlowBbCheckBranchTblCandidate(PDBGFFLOWINT pThis, PDBGFFLOWBBINT pFlowBb, PUVM pUVM,
                                               VMCPUID idCpu, PDISOPPARAM pDisParam, uint32_t fFlagsDisasm)
{
    int rc = VINF_SUCCESS;

    Assert(pFlowBb->fFlags & DBGF_FLOW_BB_F_BRANCH_TABLE && pFlowBb->pFlowBranchTbl);

    if (pDisParam->fUse & DISUSE_BASE)
    {
        uint8_t const idxRegBase = pDisParam->x86.Base.idxGenReg;
        uint32_t const cbPtr = dbgfR3FlowGetDisasPtrSize(pUVM, idCpu, fFlagsDisasm);

        /* Check that the used register size and the pointer size match. */
        if (   ((pDisParam->fUse & DISUSE_REG_GEN16) && cbPtr == sizeof(uint16_t))
            || ((pDisParam->fUse & DISUSE_REG_GEN32) && cbPtr == sizeof(uint32_t))
            || ((pDisParam->fUse & DISUSE_REG_GEN64) && cbPtr == sizeof(uint64_t)))
        {
            if (idxRegBase != pFlowBb->pFlowBranchTbl->idxGenRegBase)
            {
                /* Try to find the new branch table. */
                pFlowBb->pFlowBranchTbl = NULL;
                rc = dbgfR3FlowTryResolveIndirectBranch(pThis, pFlowBb, pUVM, idCpu, pDisParam, fFlagsDisasm);
            }
            /** @todo else check that the base register is not modified in this basic block. */
        }
        else
            dbgfR3FlowBbSetError(pFlowBb, VERR_INVALID_STATE,
                                 "The base register size and selected pointer size do not match (fUse=%#x cbPtr=%u)",
                                 pDisParam->fUse, cbPtr);
    }
    else
        dbgfR3FlowBbSetError(pFlowBb, VERR_INVALID_STATE,
                             "The instruction does not use a register");

    return rc;
}


/**
 * Processes and fills one basic block.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 * @param   pThis               The control flow graph to populate.
 * @param   pFlowBb             The basic block to fill.
 * @param   cbDisasmMax         The maximum amount to disassemble.
 * @param   fFlags              Combination of DBGF_DISAS_FLAGS_*.
 */
static int dbgfR3FlowBbProcess(PUVM pUVM, VMCPUID idCpu, PDBGFFLOWINT pThis, PDBGFFLOWBBINT pFlowBb,
                               uint32_t cbDisasmMax, uint32_t fFlags)
{
    int rc = VINF_SUCCESS;
    uint32_t cbDisasmLeft = cbDisasmMax ? cbDisasmMax : UINT32_MAX;
    DBGFADDRESS AddrDisasm = pFlowBb->AddrEnd;

    Assert(pFlowBb->fFlags & DBGF_FLOW_BB_F_EMPTY);

    /*
     * Disassemble instruction by instruction until we get a conditional or
     * unconditional jump or some sort of return.
     */
    while (   cbDisasmLeft
           && RT_SUCCESS(rc))
    {
        DBGFDISSTATE DisState;
        char szOutput[_4K];

        /*
         * Before disassembling we have to check whether the address belongs
         * to another basic block and stop here.
         */
        if (   !(pFlowBb->fFlags & DBGF_FLOW_BB_F_EMPTY)
            && dbgfR3FlowHasBbWithStartAddr(pThis, &AddrDisasm))
        {
            pFlowBb->AddrTarget = AddrDisasm;
            pFlowBb->enmEndType = DBGFFLOWBBENDTYPE_UNCOND;
            break;
        }

        rc = dbgfR3DisasInstrStateEx(pUVM, idCpu, &AddrDisasm, fFlags,
                                     &szOutput[0], sizeof(szOutput), &DisState);
        if (RT_SUCCESS(rc))
        {
            if (   pThis->fFlags & DBGF_FLOW_CREATE_F_CALL_INSN_SEPARATE_BB
                && DisState.pCurInstr->uOpcode == OP_CALL
                && !(pFlowBb->fFlags & DBGF_FLOW_BB_F_EMPTY))
            {
                /*
                 * If the basic block is not empty, the basic block is terminated and the successor is added
                 * which will contain the call instruction.
                 */
                pFlowBb->AddrTarget = AddrDisasm;
                pFlowBb->enmEndType = DBGFFLOWBBENDTYPE_UNCOND;
                rc = dbgfR3FlowBbSuccessorAdd(pThis, &AddrDisasm,
                                              (pFlowBb->fFlags & DBGF_FLOW_BB_F_BRANCH_TABLE),
                                              pFlowBb->pFlowBranchTbl);
                if (RT_FAILURE(rc))
                    dbgfR3FlowBbSetError(pFlowBb, rc, "Adding successor blocks failed with %Rrc", rc);
                break;
            }

            pFlowBb->fFlags &= ~DBGF_FLOW_BB_F_EMPTY;
            cbDisasmLeft -= DisState.cbInstr;

            if (pFlowBb->cInstr == pFlowBb->cInstrMax)
            {
                /* Reallocate. */
                RTListNodeRemove(&pFlowBb->NdFlowBb);
                PDBGFFLOWBBINT pFlowBbNew = (PDBGFFLOWBBINT)RTMemRealloc(pFlowBb,
                                                                         RT_UOFFSETOF_DYN(DBGFFLOWBBINT, aInstr[pFlowBb->cInstrMax + 10]));
                if (pFlowBbNew)
                {
                    pFlowBbNew->cInstrMax += 10;
                    pFlowBb = pFlowBbNew;
                }
                else
                    rc = VERR_NO_MEMORY;
                RTListAppend(&pThis->LstFlowBb, &pFlowBb->NdFlowBb);
            }

            if (RT_SUCCESS(rc))
            {
                PDBGFFLOWBBINSTR pInstr = &pFlowBb->aInstr[pFlowBb->cInstr];

                pInstr->AddrInstr = AddrDisasm;
                pInstr->cbInstr   = DisState.cbInstr;
                pInstr->pszInstr  = RTStrCacheEnter(pThis->hStrCacheInstr, &szOutput[0]);
                pFlowBb->cInstr++;

                pFlowBb->AddrEnd = AddrDisasm;
                DBGFR3AddrAdd(&pFlowBb->AddrEnd, pInstr->cbInstr - 1);
                DBGFR3AddrAdd(&AddrDisasm, pInstr->cbInstr);

                /*
                 * Check control flow instructions and create new basic blocks
                 * marking the current one as complete.
                 */
                if (DisState.pCurInstr->fOpType & DISOPTYPE_CONTROLFLOW)
                {
                    uint16_t uOpc = DisState.pCurInstr->uOpcode;

                    if (uOpc == OP_CALL)
                        pThis->cCallInsns++;

                    if (   uOpc == OP_RETN || uOpc == OP_RETF || uOpc == OP_IRET
                        || uOpc == OP_SYSEXIT || uOpc == OP_SYSRET)
                        pFlowBb->enmEndType = DBGFFLOWBBENDTYPE_EXIT;
                    else if (uOpc == OP_JMP)
                    {
                        Assert(DisState.pCurInstr->fOpType & DISOPTYPE_UNCOND_CONTROLFLOW);

                        if (dbgfR3FlowBranchTargetIsIndirect(&DisState.Param1))
                        {
                            pFlowBb->enmEndType = DBGFFLOWBBENDTYPE_UNCOND_INDIRECT_JMP;

                            if (pFlowBb->fFlags & DBGF_FLOW_BB_F_BRANCH_TABLE)
                            {
                                Assert(pThis->fFlags & DBGF_FLOW_CREATE_F_TRY_RESOLVE_INDIRECT_BRANCHES);

                                /*
                                 * This basic block was already discovered by parsing a jump table and
                                 * there should be a candidate for the branch table. Check whether it uses the
                                 * same branch table.
                                 */
                                rc = dbgfR3FlowBbCheckBranchTblCandidate(pThis, pFlowBb, pUVM, idCpu,
                                                                         &DisState.Param1, fFlags);
                            }
                            else
                            {
                                if (pThis->fFlags & DBGF_FLOW_CREATE_F_TRY_RESOLVE_INDIRECT_BRANCHES)
                                    rc = dbgfR3FlowTryResolveIndirectBranch(pThis, pFlowBb, pUVM, idCpu,
                                                                            &DisState.Param1, fFlags);
                                else
                                    dbgfR3FlowBbSetError(pFlowBb, VERR_NOT_SUPPORTED,
                                                         "Detected indirect branch and resolving it not being enabled");
                            }
                        }
                        else
                        {
                            pFlowBb->enmEndType = DBGFFLOWBBENDTYPE_UNCOND_JMP;

                            /* Create one new basic block with the jump target address. */
                            rc = dbgfR3FlowQueryDirectBranchTarget(pUVM, idCpu, &DisState.Param1, &pInstr->AddrInstr, pInstr->cbInstr,
                                                                   RT_BOOL(DisState.pCurInstr->fOpType & DISOPTYPE_RELATIVE_CONTROLFLOW),
                                                                   &pFlowBb->AddrTarget);
                            if (RT_SUCCESS(rc))
                                rc = dbgfR3FlowBbSuccessorAdd(pThis, &pFlowBb->AddrTarget,
                                                              (pFlowBb->fFlags & DBGF_FLOW_BB_F_BRANCH_TABLE),
                                                              pFlowBb->pFlowBranchTbl);
                        }
                    }
                    else if (uOpc != OP_CALL)
                    {
                        Assert(DisState.pCurInstr->fOpType & DISOPTYPE_COND_CONTROLFLOW);
                        pFlowBb->enmEndType = DBGFFLOWBBENDTYPE_COND;

                        /*
                         * Create two new basic blocks, one with the jump target address
                         * and one starting after the current instruction.
                         */
                        rc = dbgfR3FlowBbSuccessorAdd(pThis, &AddrDisasm,
                                                      (pFlowBb->fFlags & DBGF_FLOW_BB_F_BRANCH_TABLE),
                                                      pFlowBb->pFlowBranchTbl);
                        if (RT_SUCCESS(rc))
                        {
                            rc = dbgfR3FlowQueryDirectBranchTarget(pUVM, idCpu, &DisState.Param1, &pInstr->AddrInstr, pInstr->cbInstr,
                                                                   RT_BOOL(DisState.pCurInstr->fOpType & DISOPTYPE_RELATIVE_CONTROLFLOW),
                                                                   &pFlowBb->AddrTarget);
                            if (RT_SUCCESS(rc))
                                rc = dbgfR3FlowBbSuccessorAdd(pThis, &pFlowBb->AddrTarget,
                                                              (pFlowBb->fFlags & DBGF_FLOW_BB_F_BRANCH_TABLE),
                                                              pFlowBb->pFlowBranchTbl);
                        }
                    }
                    else if (pThis->fFlags & DBGF_FLOW_CREATE_F_CALL_INSN_SEPARATE_BB)
                    {
                        pFlowBb->enmEndType = DBGFFLOWBBENDTYPE_UNCOND;
                        pFlowBb->fFlags    |= DBGF_FLOW_BB_F_CALL_INSN;

                        /* Add new basic block coming after the call instruction. */
                        rc = dbgfR3FlowBbSuccessorAdd(pThis, &AddrDisasm,
                                                      (pFlowBb->fFlags & DBGF_FLOW_BB_F_BRANCH_TABLE),
                                                      pFlowBb->pFlowBranchTbl);
                        if (   RT_SUCCESS(rc)
                            && !dbgfR3FlowBranchTargetIsIndirect(&DisState.Param1))
                        {
                            /* Resolve the branch target. */
                            rc = dbgfR3FlowQueryDirectBranchTarget(pUVM, idCpu, &DisState.Param1, &pInstr->AddrInstr, pInstr->cbInstr,
                                                                   RT_BOOL(DisState.pCurInstr->fOpType & DISOPTYPE_RELATIVE_CONTROLFLOW),
                                                                   &pFlowBb->AddrTarget);
                            if (RT_SUCCESS(rc))
                                pFlowBb->fFlags |= DBGF_FLOW_BB_F_CALL_INSN_TARGET_KNOWN;
                        }
                    }

                    if (RT_FAILURE(rc))
                        dbgfR3FlowBbSetError(pFlowBb, rc, "Adding successor blocks failed with %Rrc", rc);

                    /* Quit disassembling. */
                    if (   (   uOpc != OP_CALL
                            || (pThis->fFlags & DBGF_FLOW_CREATE_F_CALL_INSN_SEPARATE_BB))
                        || RT_FAILURE(rc))
                        break;
                }
            }
            else
                dbgfR3FlowBbSetError(pFlowBb, rc, "Increasing basic block failed with %Rrc", rc);
        }
        else
            dbgfR3FlowBbSetError(pFlowBb, rc, "Disassembling the instruction failed with %Rrc", rc);
    }

    return VINF_SUCCESS;
}

/**
 * Populate all empty basic blocks.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 * @param   pThis               The control flow graph to populate.
 * @param   cbDisasmMax         The maximum amount to disassemble.
 * @param   fFlags              Combination of DBGF_DISAS_FLAGS_*.
 */
static int dbgfR3FlowPopulate(PUVM pUVM, VMCPUID idCpu, PDBGFFLOWINT pThis, uint32_t cbDisasmMax, uint32_t fFlags)
{
    int rc = VINF_SUCCESS;
    PDBGFFLOWBBINT pFlowBb = dbgfR3FlowGetUnpopulatedBb(pThis);

    while (pFlowBb != NULL)
    {
        rc = dbgfR3FlowBbProcess(pUVM, idCpu, pThis, pFlowBb, cbDisasmMax, fFlags);
        if (RT_FAILURE(rc))
            break;

        pFlowBb = dbgfR3FlowGetUnpopulatedBb(pThis);
    }

    return rc;
}

/**
 * Creates a new control flow graph from the given start address.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               CPU id for disassembling.
 * @param   pAddressStart       Where to start creating the control flow graph.
 * @param   cbDisasmMax         Limit the amount of bytes to disassemble, 0 for no limit.
 * @param   fFlagsFlow          Combination of DBGF_FLOW_CREATE_F_* to control the creation of the flow graph.
 * @param   fFlagsDisasm        Combination of DBGF_DISAS_FLAGS_* controlling the style of the disassembled
 *                              instructions.
 * @param   phFlow              Where to store the handle to the control flow graph on success.
 */
VMMR3DECL(int) DBGFR3FlowCreate(PUVM pUVM, VMCPUID idCpu, PDBGFADDRESS pAddressStart, uint32_t cbDisasmMax,
                                uint32_t fFlagsFlow, uint32_t fFlagsDisasm, PDBGFFLOW phFlow)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);
    AssertPtrReturn(pAddressStart, VERR_INVALID_POINTER);
    AssertReturn(!(fFlagsDisasm & ~DBGF_DISAS_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn((fFlagsDisasm & DBGF_DISAS_FLAGS_MODE_MASK) <= DBGF_DISAS_FLAGS_64BIT_MODE, VERR_INVALID_PARAMETER);

    /* Create the control flow graph container. */
    int rc = VINF_SUCCESS;
    PDBGFFLOWINT pThis = (PDBGFFLOWINT)RTMemAllocZ(sizeof(DBGFFLOWINT));
    if (RT_LIKELY(pThis))
    {
        rc = RTStrCacheCreate(&pThis->hStrCacheInstr, "DBGFFLOW");
        if (RT_SUCCESS(rc))
        {
            pThis->cRefs       = 1;
            pThis->cRefsBb     = 0;
            pThis->cBbs        = 0;
            pThis->cBranchTbls = 0;
            pThis->cCallInsns  = 0;
            pThis->fFlags      = fFlagsFlow;
            RTListInit(&pThis->LstFlowBb);
            RTListInit(&pThis->LstBranchTbl);
            /* Create the entry basic block and start the work. */

            PDBGFFLOWBBINT pFlowBb = dbgfR3FlowBbCreate(pThis, pAddressStart, DBGF_FLOW_BB_F_ENTRY, 10);
            if (RT_LIKELY(pFlowBb))
            {
                dbgfR3FlowLink(pThis, pFlowBb);
                rc = dbgfR3FlowPopulate(pUVM, idCpu, pThis, cbDisasmMax, fFlagsDisasm);
                if (RT_SUCCESS(rc))
                {
                    *phFlow = pThis;
                    return VINF_SUCCESS;
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }

        ASMAtomicDecU32(&pThis->cRefs);
        dbgfR3FlowDestroy(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Retains the control flow graph handle.
 *
 * @returns Current reference count.
 * @param   hFlow                The control flow graph handle to retain.
 */
VMMR3DECL(uint32_t) DBGFR3FlowRetain(DBGFFLOW hFlow)
{
    PDBGFFLOWINT pThis = hFlow;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    return cRefs;
}


/**
 * Releases the control flow graph handle.
 *
 * @returns Current reference count, on 0 the control flow graph will be destroyed.
 * @param   hFlow                The control flow graph handle to release.
 */
VMMR3DECL(uint32_t) DBGFR3FlowRelease(DBGFFLOW hFlow)
{
    PDBGFFLOWINT pThis = hFlow;
    if (!pThis)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 0)
        dbgfR3FlowDestroy(pThis);
    return cRefs;
}


/**
 * Queries the basic block denoting the entry point into the control flow graph.
 *
 * @returns VBox status code.
 * @param   hFlow                The control flow graph handle.
 * @param   phFlowBb             Where to store the basic block handle on success.
 */
VMMR3DECL(int) DBGFR3FlowQueryStartBb(DBGFFLOW hFlow, PDBGFFLOWBB phFlowBb)
{
    PDBGFFLOWINT pThis = hFlow;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    PDBGFFLOWBBINT pFlowBb;
    RTListForEach(&pThis->LstFlowBb, pFlowBb, DBGFFLOWBBINT, NdFlowBb)
    {
        if (pFlowBb->fFlags & DBGF_FLOW_BB_F_ENTRY)
        {
            *phFlowBb = pFlowBb;
            return VINF_SUCCESS;
        }
    }

    AssertFailed(); /* Should never get here. */
    return VERR_INTERNAL_ERROR;
}


/**
 * Queries a basic block in the given control flow graph which covers the given
 * address.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if there is no basic block intersecting with the address.
 * @param   hFlow               The control flow graph handle.
 * @param   pAddr               The address to look for.
 * @param   phFlowBb            Where to store the basic block handle on success.
 */
VMMR3DECL(int) DBGFR3FlowQueryBbByAddress(DBGFFLOW hFlow, PDBGFADDRESS pAddr, PDBGFFLOWBB phFlowBb)
{
    PDBGFFLOWINT pThis = hFlow;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pAddr, VERR_INVALID_POINTER);
    AssertPtrReturn(phFlowBb, VERR_INVALID_POINTER);

    PDBGFFLOWBBINT pFlowBb;
    RTListForEach(&pThis->LstFlowBb, pFlowBb, DBGFFLOWBBINT, NdFlowBb)
    {
        if (dbgfR3FlowAddrIntersect(pFlowBb, pAddr))
        {
            DBGFR3FlowBbRetain(pFlowBb);
            *phFlowBb = pFlowBb;
            return VINF_SUCCESS;
        }
    }

    return VERR_NOT_FOUND;
}


/**
 * Queries a branch table in the given control flow graph by the given address.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if there is no branch table with the given address.
 * @param   hFlow               The control flow graph handle.
 * @param   pAddr               The address of the branch table.
 * @param   phFlowBranchTbl     Where to store the handle to branch table on success.
 *
 * @note Call DBGFR3FlowBranchTblRelease() when the handle is not required anymore.
 */
VMMR3DECL(int) DBGFR3FlowQueryBranchTblByAddress(DBGFFLOW hFlow, PDBGFADDRESS pAddr, PDBGFFLOWBRANCHTBL phFlowBranchTbl)
{
    PDBGFFLOWINT pThis = hFlow;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pAddr, VERR_INVALID_POINTER);
    AssertPtrReturn(phFlowBranchTbl, VERR_INVALID_POINTER);

    PDBGFFLOWBRANCHTBLINT pBranchTbl = dbgfR3FlowBranchTblFindByAddr(pThis, pAddr);
    if (pBranchTbl)
    {
        DBGFR3FlowBranchTblRetain(pBranchTbl);
        *phFlowBranchTbl = pBranchTbl;
        return VINF_SUCCESS;
    }

    return VERR_NOT_FOUND;
}


/**
 * Returns the number of basic blcoks inside the control flow graph.
 *
 * @returns Number of basic blocks.
 * @param   hFlow                The control flow graph handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowGetBbCount(DBGFFLOW hFlow)
{
    PDBGFFLOWINT pThis = hFlow;
    AssertPtrReturn(pThis, 0);

    return pThis->cBbs;
}


/**
 * Returns the number of branch tables inside the control flow graph.
 *
 * @returns Number of basic blocks.
 * @param   hFlow                The control flow graph handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowGetBranchTblCount(DBGFFLOW hFlow)
{
    PDBGFFLOWINT pThis = hFlow;
    AssertPtrReturn(pThis, 0);

    return pThis->cBranchTbls;
}


/**
 * Returns the number of call instructions encountered in the given
 * control flow graph.
 *
 * @returns Number of call instructions.
 * @param   hFlow                The control flow graph handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowGetCallInsnCount(DBGFFLOW hFlow)
{
    PDBGFFLOWINT pThis = hFlow;
    AssertPtrReturn(pThis, 0);

    return pThis->cCallInsns;
}


/**
 * Retains the basic block handle.
 *
 * @returns Current reference count.
 * @param   hFlowBb              The basic block handle to retain.
 */
VMMR3DECL(uint32_t) DBGFR3FlowBbRetain(DBGFFLOWBB hFlowBb)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pFlowBb->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p %d\n", cRefs, pFlowBb, pFlowBb->enmEndType));
    return cRefs;
}


/**
 * Releases the basic block handle.
 *
 * @returns Current reference count, on 0 the basic block will be destroyed.
 * @param   hFlowBb              The basic block handle to release.
 */
VMMR3DECL(uint32_t) DBGFR3FlowBbRelease(DBGFFLOWBB hFlowBb)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    if (!pFlowBb)
        return 0;

    return dbgfR3FlowBbReleaseInt(pFlowBb, true /* fMayDestroyFlow */);
}


/**
 * Returns the start address of the basic block.
 *
 * @returns Pointer to DBGF adress containing the start address of the basic block.
 * @param   hFlowBb              The basic block handle.
 * @param   pAddrStart          Where to store the start address of the basic block.
 */
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBbGetStartAddress(DBGFFLOWBB hFlowBb, PDBGFADDRESS pAddrStart)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, NULL);
    AssertPtrReturn(pAddrStart, NULL);

    *pAddrStart = pFlowBb->AddrStart;
    return pAddrStart;
}


/**
 * Returns the end address of the basic block (inclusive).
 *
 * @returns Pointer to DBGF adress containing the end address of the basic block.
 * @param   hFlowBb              The basic block handle.
 * @param   pAddrEnd            Where to store the end address of the basic block.
 */
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBbGetEndAddress(DBGFFLOWBB hFlowBb, PDBGFADDRESS pAddrEnd)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, NULL);
    AssertPtrReturn(pAddrEnd, NULL);

    *pAddrEnd = pFlowBb->AddrEnd;
    return pAddrEnd;
}


/**
 * Returns the address the last instruction in the basic block branches to.
 *
 * @returns Pointer to DBGF adress containing the branch address of the basic block.
 * @param   hFlowBb             The basic block handle.
 * @param   pAddrTarget         Where to store the branch address of the basic block.
 *
 * @note This is only valid for unconditional or conditional branches, or for a basic block
 *       containing only a call instruction when DBGF_FLOW_CREATE_F_CALL_INSN_SEPARATE_BB was given
 *       during creation and the branch target could be deduced as indicated by the DBGF_FLOW_BB_F_CALL_INSN_TARGET_KNOWN
 *       flag for the basic block. This method will assert for every other basic block type.
 * @note For indirect unconditional branches using a branch table this will return the start address
 *       of the branch table.
 */
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBbGetBranchAddress(DBGFFLOWBB hFlowBb, PDBGFADDRESS pAddrTarget)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, NULL);
    AssertPtrReturn(pAddrTarget, NULL);
    AssertReturn(   pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_UNCOND_JMP
                 || pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_COND
                 || pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_UNCOND_INDIRECT_JMP
                 || (   pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_UNCOND
                     && (pFlowBb->fFlags & DBGF_FLOW_BB_F_CALL_INSN_TARGET_KNOWN)),
                 NULL);

    if (   pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_UNCOND_INDIRECT_JMP
        && pFlowBb->pFlowBranchTbl)
        *pAddrTarget = pFlowBb->pFlowBranchTbl->AddrStart;
    else
        *pAddrTarget = pFlowBb->AddrTarget;
    return pAddrTarget;
}


/**
 * Returns the address of the next block following this one in the instruction stream.
 * (usually end address + 1).
 *
 * @returns Pointer to DBGF adress containing the following address of the basic block.
 * @param   hFlowBb              The basic block handle.
 * @param   pAddrFollow         Where to store the following address of the basic block.
 *
 * @note This is only valid for conditional branches and if the last instruction in the
 *       given basic block doesn't change the control flow but the blocks were split
 *       because the successor is referenced by multiple other blocks as an entry point.
 */
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBbGetFollowingAddress(DBGFFLOWBB hFlowBb, PDBGFADDRESS pAddrFollow)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, NULL);
    AssertPtrReturn(pAddrFollow, NULL);
    AssertReturn(   pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_UNCOND
                 || pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_COND,
                 NULL);

    *pAddrFollow = pFlowBb->AddrEnd;
    DBGFR3AddrAdd(pAddrFollow, 1);
    return pAddrFollow;
}


/**
 * Returns the type of the last instruction in the basic block.
 *
 * @returns Last instruction type.
 * @param   hFlowBb              The basic block handle.
 */
VMMR3DECL(DBGFFLOWBBENDTYPE) DBGFR3FlowBbGetType(DBGFFLOWBB hFlowBb)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, DBGFFLOWBBENDTYPE_INVALID);

    return pFlowBb->enmEndType;
}


/**
 * Get the number of instructions contained in the basic block.
 *
 * @returns Number of instructions in the basic block.
 * @param   hFlowBb              The basic block handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowBbGetInstrCount(DBGFFLOWBB hFlowBb)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, 0);

    return pFlowBb->cInstr;
}


/**
 * Get flags for the given basic block.
 *
 * @returns Combination of DBGF_FLOW_BB_F_*
 * @param   hFlowBb              The basic block handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowBbGetFlags(DBGFFLOWBB hFlowBb)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, 0);

    return pFlowBb->fFlags;
}


/**
 * Queries the branch table used if the given basic block ends with an indirect branch
 * and has a branch table referenced.
 *
 * @returns VBox status code.
 * @param   hFlowBb              The basic block handle.
 * @param   phBranchTbl          Where to store the branch table handle on success.
 *
 * @note Release the branch table reference with DBGFR3FlowBranchTblRelease() when not required
 *       anymore.
 */
VMMR3DECL(int) DBGFR3FlowBbQueryBranchTbl(DBGFFLOWBB hFlowBb, PDBGFFLOWBRANCHTBL phBranchTbl)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, VERR_INVALID_HANDLE);
    AssertReturn(pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_UNCOND_INDIRECT_JMP, VERR_INVALID_STATE);
    AssertPtrReturn(pFlowBb->pFlowBranchTbl, VERR_INVALID_STATE);
    AssertPtrReturn(phBranchTbl, VERR_INVALID_POINTER);

    DBGFR3FlowBranchTblRetain(pFlowBb->pFlowBranchTbl);
    *phBranchTbl = pFlowBb->pFlowBranchTbl;
    return VINF_SUCCESS;
}


/**
 * Returns the error status and message if the given basic block has an error.
 *
 * @returns VBox status code of the error for the basic block.
 * @param   hFlowBb              The basic block handle.
 * @param   ppszErr             Where to store the pointer to the error message - optional.
 */
VMMR3DECL(int) DBGFR3FlowBbQueryError(DBGFFLOWBB hFlowBb, const char **ppszErr)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, VERR_INVALID_HANDLE);

    if (ppszErr)
        *ppszErr = pFlowBb->pszErr;

    return pFlowBb->rcError;
}


/**
 * Store the disassembled instruction as a string in the given output buffer.
 *
 * @returns VBox status code.
 * @param   hFlowBb              The basic block handle.
 * @param   idxInstr            The instruction to query.
 * @param   pAddrInstr          Where to store the guest instruction address on success, optional.
 * @param   pcbInstr            Where to store the instruction size on success, optional.
 * @param   ppszInstr           Where to store the pointer to the disassembled instruction string, optional.
 */
VMMR3DECL(int) DBGFR3FlowBbQueryInstr(DBGFFLOWBB hFlowBb, uint32_t idxInstr, PDBGFADDRESS pAddrInstr,
                                     uint32_t *pcbInstr, const char **ppszInstr)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, VERR_INVALID_POINTER);
    AssertReturn(idxInstr < pFlowBb->cInstr, VERR_INVALID_PARAMETER);

    if (pAddrInstr)
        *pAddrInstr = pFlowBb->aInstr[idxInstr].AddrInstr;
    if (pcbInstr)
        *pcbInstr = pFlowBb->aInstr[idxInstr].cbInstr;
    if (ppszInstr)
        *ppszInstr = pFlowBb->aInstr[idxInstr].pszInstr;

    return VINF_SUCCESS;
}


/**
 * Queries the successors of the basic block.
 *
 * @returns VBox status code.
 * @param   hFlowBb              The basic block handle.
 * @param   phFlowBbFollow       Where to store the handle to the basic block following
 *                              this one (optional).
 * @param   phFlowBbTarget       Where to store the handle to the basic block being the
 *                              branch target for this one (optional).
 */
VMMR3DECL(int) DBGFR3FlowBbQuerySuccessors(DBGFFLOWBB hFlowBb, PDBGFFLOWBB phFlowBbFollow, PDBGFFLOWBB phFlowBbTarget)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, VERR_INVALID_POINTER);

    if (   phFlowBbFollow
        && (   pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_UNCOND
            || pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_COND))
    {
        DBGFADDRESS AddrStart = pFlowBb->AddrEnd;
        DBGFR3AddrAdd(&AddrStart, 1);
        int rc = DBGFR3FlowQueryBbByAddress(pFlowBb->pFlow, &AddrStart, phFlowBbFollow);
        AssertRC(rc);
    }

    if (   phFlowBbTarget
        && (   pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_UNCOND_JMP
            || pFlowBb->enmEndType == DBGFFLOWBBENDTYPE_COND))
    {
        int rc = DBGFR3FlowQueryBbByAddress(pFlowBb->pFlow, &pFlowBb->AddrTarget, phFlowBbTarget);
        AssertRC(rc);
    }

    return VINF_SUCCESS;
}


/**
 * Returns the number of basic blocks referencing this basic block as a target.
 *
 * @returns Number of other basic blocks referencing this one.
 * @param   hFlowBb              The basic block handle.
 *
 * @note If the given basic block references itself (loop, etc.) this will be counted as well.
 */
VMMR3DECL(uint32_t) DBGFR3FlowBbGetRefBbCount(DBGFFLOWBB hFlowBb)
{
    PDBGFFLOWBBINT pFlowBb = hFlowBb;
    AssertPtrReturn(pFlowBb, 0);

    uint32_t cRefsBb = 0;
    PDBGFFLOWBBINT pFlowBbCur;
    RTListForEach(&pFlowBb->pFlow->LstFlowBb, pFlowBbCur, DBGFFLOWBBINT, NdFlowBb)
    {
        if (pFlowBbCur->fFlags & DBGF_FLOW_BB_F_INCOMPLETE_ERR)
            continue;

        if (   pFlowBbCur->enmEndType == DBGFFLOWBBENDTYPE_UNCOND
            || pFlowBbCur->enmEndType == DBGFFLOWBBENDTYPE_COND)
        {
            DBGFADDRESS AddrStart = pFlowBb->AddrEnd;
            DBGFR3AddrAdd(&AddrStart, 1);
            if (dbgfR3FlowAddrEqual(&pFlowBbCur->AddrStart, &AddrStart))
                cRefsBb++;
        }

        if (   (   pFlowBbCur->enmEndType == DBGFFLOWBBENDTYPE_UNCOND_JMP
                || pFlowBbCur->enmEndType == DBGFFLOWBBENDTYPE_COND)
            && dbgfR3FlowAddrEqual(&pFlowBbCur->AddrStart, &pFlowBb->AddrTarget))
            cRefsBb++;
    }
    return cRefsBb;
}


/**
 * Returns the basic block handles referencing the given basic block.
 *
 * @returns VBox status code.
 * @retval  VERR_BUFFER_OVERFLOW if the array can't hold all the basic blocks.
 * @param   hFlowBb             The basic block handle.
 * @param   paFlowBbRef         Pointer to the array containing the referencing basic block handles on success.
 * @param   cRef                Number of entries in the given array.
 */
VMMR3DECL(int) DBGFR3FlowBbGetRefBb(DBGFFLOWBB hFlowBb, PDBGFFLOWBB paFlowBbRef, uint32_t cRef)
{
    RT_NOREF3(hFlowBb, paFlowBbRef, cRef);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Retains a reference for the given control flow graph branch table.
 *
 * @returns new reference count.
 * @param   hFlowBranchTbl      The branch table handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowBranchTblRetain(DBGFFLOWBRANCHTBL hFlowBranchTbl)
{
    PDBGFFLOWBRANCHTBLINT pFlowBranchTbl = hFlowBranchTbl;
    AssertPtrReturn(pFlowBranchTbl, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pFlowBranchTbl->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pFlowBranchTbl));
    return cRefs;
}


/**
 * Releases a given branch table handle.
 *
 * @returns the new reference count of the given branch table, on 0 it is destroyed.
 * @param   hFlowBranchTbl      The branch table handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowBranchTblRelease(DBGFFLOWBRANCHTBL hFlowBranchTbl)
{
    PDBGFFLOWBRANCHTBLINT pFlowBranchTbl = hFlowBranchTbl;
    if (!pFlowBranchTbl)
        return 0;
    AssertPtrReturn(pFlowBranchTbl, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pFlowBranchTbl->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pFlowBranchTbl));
    if (cRefs == 0)
        dbgfR3FlowBranchTblDestroy(pFlowBranchTbl);
    return cRefs;
}


/**
 * Return the number of slots the branch table has.
 *
 * @returns Number of slots in the branch table.
 * @param   hFlowBranchTbl      The branch table handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowBranchTblGetSlots(DBGFFLOWBRANCHTBL hFlowBranchTbl)
{
    PDBGFFLOWBRANCHTBLINT pFlowBranchTbl = hFlowBranchTbl;
    AssertPtrReturn(pFlowBranchTbl, 0);

    return pFlowBranchTbl->cSlots;
}


/**
 * Returns the start address of the branch table in the guest.
 *
 * @returns Pointer to start address of the branch table (pAddrStart).
 * @param   hFlowBranchTbl      The branch table handle.
 * @param   pAddrStart          Where to store the branch table address.
 */
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBranchTblGetStartAddress(DBGFFLOWBRANCHTBL hFlowBranchTbl, PDBGFADDRESS pAddrStart)
{
    PDBGFFLOWBRANCHTBLINT pFlowBranchTbl = hFlowBranchTbl;
    AssertPtrReturn(pFlowBranchTbl, NULL);
    AssertPtrReturn(pAddrStart, NULL);

    *pAddrStart = pFlowBranchTbl->AddrStart;
    return pAddrStart;
}


/**
 * Returns one address in the branch table at the given slot index.
 *
 * @return Pointer to the address at the given slot in the given branch table.
 * @param  hFlowBranchTbl       The branch table handle.
 * @param  idxSlot              The slot the address should be returned from.
 * @param  pAddrSlot            Where to store the address.
 */
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowBranchTblGetAddrAtSlot(DBGFFLOWBRANCHTBL hFlowBranchTbl, uint32_t idxSlot, PDBGFADDRESS pAddrSlot)
{
    PDBGFFLOWBRANCHTBLINT pFlowBranchTbl = hFlowBranchTbl;
    AssertPtrReturn(pFlowBranchTbl, NULL);
    AssertPtrReturn(pAddrSlot, NULL);
    AssertReturn(idxSlot < pFlowBranchTbl->cSlots, NULL);

    *pAddrSlot = pFlowBranchTbl->aAddresses[idxSlot];
    return pAddrSlot;
}


/**
 * Query all addresses contained in the given branch table.
 *
 * @returns VBox status code.
 * @retval  VERR_BUFFER_OVERFLOW if there is not enough space in the array to hold all addresses.
 * @param   hFlowBranchTbl      The branch table handle.
 * @param   paAddrs             Where to store the addresses on success.
 * @param   cAddrs              Number of entries the array can hold.
 */
VMMR3DECL(int) DBGFR3FlowBranchTblQueryAddresses(DBGFFLOWBRANCHTBL hFlowBranchTbl, PDBGFADDRESS paAddrs, uint32_t cAddrs)
{
    PDBGFFLOWBRANCHTBLINT pFlowBranchTbl = hFlowBranchTbl;
    AssertPtrReturn(pFlowBranchTbl, VERR_INVALID_HANDLE);
    AssertPtrReturn(paAddrs, VERR_INVALID_POINTER);
    AssertReturn(cAddrs > 0, VERR_INVALID_PARAMETER);

    if (cAddrs < pFlowBranchTbl->cSlots)
        return VERR_BUFFER_OVERFLOW;

    memcpy(paAddrs, &pFlowBranchTbl->aAddresses[0], pFlowBranchTbl->cSlots * sizeof(DBGFADDRESS));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTSORTCMP}
 */
static DECLCALLBACK(int) dbgfR3FlowItSortCmp(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PDBGFFLOWITORDER penmOrder = (PDBGFFLOWITORDER)pvUser;
    PDBGFFLOWBBINT pFlowBb1 = *(PDBGFFLOWBBINT *)pvElement1;
    PDBGFFLOWBBINT pFlowBb2 = *(PDBGFFLOWBBINT *)pvElement2;

    if (dbgfR3FlowAddrEqual(&pFlowBb1->AddrStart, &pFlowBb2->AddrStart))
        return 0;

    if (*penmOrder == DBGFFLOWITORDER_BY_ADDR_LOWEST_FIRST)
    {
        if (dbgfR3FlowAddrLower(&pFlowBb1->AddrStart, &pFlowBb2->AddrStart))
            return -1;
        else
            return 1;
    }
    else
    {
        if (dbgfR3FlowAddrLower(&pFlowBb1->AddrStart, &pFlowBb2->AddrStart))
            return 1;
        else
            return -1;
    }
}


/**
 * Creates a new iterator for the given control flow graph.
 *
 * @returns VBox status code.
 * @param   hFlow               The control flow graph handle.
 * @param   enmOrder            The order in which the basic blocks are enumerated.
 * @param   phFlowIt            Where to store the handle to the iterator on success.
 */
VMMR3DECL(int) DBGFR3FlowItCreate(DBGFFLOW hFlow, DBGFFLOWITORDER enmOrder, PDBGFFLOWIT phFlowIt)
{
    int rc = VINF_SUCCESS;
    PDBGFFLOWINT pFlow = hFlow;
    AssertPtrReturn(pFlow, VERR_INVALID_POINTER);
    AssertPtrReturn(phFlowIt, VERR_INVALID_POINTER);
    AssertReturn(enmOrder > DBGFFLOWITORDER_INVALID && enmOrder < DBGFFLOWITORDER_BREADTH_FIRST,
                 VERR_INVALID_PARAMETER);
    AssertReturn(enmOrder < DBGFFLOWITORDER_DEPTH_FRIST, VERR_NOT_IMPLEMENTED); /** @todo */

    PDBGFFLOWITINT pIt = (PDBGFFLOWITINT)RTMemAllocZ(RT_UOFFSETOF_DYN(DBGFFLOWITINT, apBb[pFlow->cBbs]));
    if (RT_LIKELY(pIt))
    {
        DBGFR3FlowRetain(hFlow);
        pIt->pFlow      = pFlow;
        pIt->idxBbNext = 0;
        /* Fill the list and then sort. */
        uint32_t idxBb = 0;
        PDBGFFLOWBBINT pFlowBb;
        RTListForEach(&pFlow->LstFlowBb, pFlowBb, DBGFFLOWBBINT, NdFlowBb)
        {
            DBGFR3FlowBbRetain(pFlowBb);
            pIt->apBb[idxBb++] = pFlowBb;
        }

        /* Sort the blocks by address. */
        RTSortShell(&pIt->apBb[0], pFlow->cBbs, sizeof(PDBGFFLOWBBINT), dbgfR3FlowItSortCmp, &enmOrder);

        *phFlowIt = pIt;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Destroys a given control flow graph iterator.
 *
 * @param   hFlowIt              The control flow graph iterator handle.
 */
VMMR3DECL(void) DBGFR3FlowItDestroy(DBGFFLOWIT hFlowIt)
{
    PDBGFFLOWITINT pIt = hFlowIt;
    AssertPtrReturnVoid(pIt);

    for (unsigned i = 0; i < pIt->pFlow->cBbs; i++)
        DBGFR3FlowBbRelease(pIt->apBb[i]);

    DBGFR3FlowRelease(pIt->pFlow);
    RTMemFree(pIt);
}


/**
 * Returns the next basic block in the iterator or NULL if there is no
 * basic block left.
 *
 * @returns Handle to the next basic block in the iterator or NULL if the end
 *          was reached.
 * @param   hFlowIt              The iterator handle.
 *
 * @note If a valid handle is returned it must be release with DBGFR3FlowBbRelease()
 *       when not required anymore.
 */
VMMR3DECL(DBGFFLOWBB) DBGFR3FlowItNext(DBGFFLOWIT hFlowIt)
{
    PDBGFFLOWITINT pIt = hFlowIt;
    AssertPtrReturn(pIt, NULL);

    PDBGFFLOWBBINT pFlowBb = NULL;
    if (pIt->idxBbNext < pIt->pFlow->cBbs)
    {
        pFlowBb = pIt->apBb[pIt->idxBbNext++];
        DBGFR3FlowBbRetain(pFlowBb);
    }

    return pFlowBb;
}


/**
 * Resets the given iterator to the beginning.
 *
 * @returns VBox status code.
 * @param   hFlowIt              The iterator handle.
 */
VMMR3DECL(int) DBGFR3FlowItReset(DBGFFLOWIT hFlowIt)
{
    PDBGFFLOWITINT pIt = hFlowIt;
    AssertPtrReturn(pIt, VERR_INVALID_HANDLE);

    pIt->idxBbNext = 0;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTSORTCMP}
 */
static DECLCALLBACK(int) dbgfR3FlowBranchTblItSortCmp(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PDBGFFLOWITORDER penmOrder = (PDBGFFLOWITORDER)pvUser;
    PDBGFFLOWBRANCHTBLINT pTbl1 = *(PDBGFFLOWBRANCHTBLINT *)pvElement1;
    PDBGFFLOWBRANCHTBLINT pTbl2 = *(PDBGFFLOWBRANCHTBLINT *)pvElement2;

    if (dbgfR3FlowAddrEqual(&pTbl1->AddrStart, &pTbl2->AddrStart))
        return 0;

    if (*penmOrder == DBGFFLOWITORDER_BY_ADDR_LOWEST_FIRST)
    {
        if (dbgfR3FlowAddrLower(&pTbl1->AddrStart, &pTbl2->AddrStart))
            return -1;
        else
            return 1;
    }
    else
    {
        if (dbgfR3FlowAddrLower(&pTbl1->AddrStart, &pTbl2->AddrStart))
            return 1;
        else
            return -1;
    }
}


/**
 * Creates a new branch table iterator for the given control flow graph.
 *
 * @returns VBox status code.
 * @param   hFlow               The control flow graph handle.
 * @param   enmOrder            The order in which the basic blocks are enumerated.
 * @param   phFlowBranchTblIt   Where to store the handle to the iterator on success.
 */
VMMR3DECL(int) DBGFR3FlowBranchTblItCreate(DBGFFLOW hFlow, DBGFFLOWITORDER enmOrder,
                                           PDBGFFLOWBRANCHTBLIT phFlowBranchTblIt)
{
    int rc = VINF_SUCCESS;
    PDBGFFLOWINT pFlow = hFlow;
    AssertPtrReturn(pFlow, VERR_INVALID_POINTER);
    AssertPtrReturn(phFlowBranchTblIt, VERR_INVALID_POINTER);
    AssertReturn(enmOrder > DBGFFLOWITORDER_INVALID && enmOrder < DBGFFLOWITORDER_BREADTH_FIRST,
                 VERR_INVALID_PARAMETER);
    AssertReturn(enmOrder < DBGFFLOWITORDER_DEPTH_FRIST, VERR_NOT_SUPPORTED);

    PDBGFFLOWBRANCHTBLITINT pIt = (PDBGFFLOWBRANCHTBLITINT)RTMemAllocZ(RT_UOFFSETOF_DYN(DBGFFLOWBRANCHTBLITINT,
                                                                                        apBranchTbl[pFlow->cBranchTbls]));
    if (RT_LIKELY(pIt))
    {
        DBGFR3FlowRetain(hFlow);
        pIt->pFlow      = pFlow;
        pIt->idxTblNext = 0;
        /* Fill the list and then sort. */
        uint32_t idxTbl = 0;
        PDBGFFLOWBRANCHTBLINT pFlowBranchTbl;
        RTListForEach(&pFlow->LstBranchTbl, pFlowBranchTbl, DBGFFLOWBRANCHTBLINT, NdBranchTbl)
        {
            DBGFR3FlowBranchTblRetain(pFlowBranchTbl);
            pIt->apBranchTbl[idxTbl++] = pFlowBranchTbl;
        }

        /* Sort the blocks by address. */
        RTSortShell(&pIt->apBranchTbl[0], pFlow->cBranchTbls, sizeof(PDBGFFLOWBRANCHTBLINT), dbgfR3FlowBranchTblItSortCmp, &enmOrder);

        *phFlowBranchTblIt = pIt;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Destroys a given control flow graph branch table iterator.
 *
 * @param   hFlowBranchTblIt              The control flow graph branch table iterator handle.
 */
VMMR3DECL(void) DBGFR3FlowBranchTblItDestroy(DBGFFLOWBRANCHTBLIT hFlowBranchTblIt)
{
    PDBGFFLOWBRANCHTBLITINT pIt = hFlowBranchTblIt;
    AssertPtrReturnVoid(pIt);

    for (unsigned i = 0; i < pIt->pFlow->cBranchTbls; i++)
        DBGFR3FlowBranchTblRelease(pIt->apBranchTbl[i]);

    DBGFR3FlowRelease(pIt->pFlow);
    RTMemFree(pIt);
}


/**
 * Returns the next branch table in the iterator or NULL if there is no
 * branch table left.
 *
 * @returns Handle to the next basic block in the iterator or NULL if the end
 *          was reached.
 * @param   hFlowBranchTblIt    The iterator handle.
 *
 * @note If a valid handle is returned it must be release with DBGFR3FlowBranchTblRelease()
 *       when not required anymore.
 */
VMMR3DECL(DBGFFLOWBRANCHTBL) DBGFR3FlowBranchTblItNext(DBGFFLOWBRANCHTBLIT hFlowBranchTblIt)
{
    PDBGFFLOWBRANCHTBLITINT pIt = hFlowBranchTblIt;
    AssertPtrReturn(pIt, NULL);

    PDBGFFLOWBRANCHTBLINT pTbl = NULL;
    if (pIt->idxTblNext < pIt->pFlow->cBranchTbls)
    {
        pTbl = pIt->apBranchTbl[pIt->idxTblNext++];
        DBGFR3FlowBranchTblRetain(pTbl);
    }

    return pTbl;
}


/**
 * Resets the given iterator to the beginning.
 *
 * @returns VBox status code.
 * @param   hFlowBranchTblIt    The iterator handle.
 */
VMMR3DECL(int) DBGFR3FlowBranchTblItReset(DBGFFLOWBRANCHTBLIT hFlowBranchTblIt)
{
    PDBGFFLOWBRANCHTBLITINT pIt = hFlowBranchTblIt;
    AssertPtrReturn(pIt, VERR_INVALID_HANDLE);

    pIt->idxTblNext = 0;
    return VINF_SUCCESS;
}
