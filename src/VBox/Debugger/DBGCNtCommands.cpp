/* $Id: DBGCNtCommands.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, Windows NT Related Commands.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/errcore.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include "DBGCInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * 64-bit _RTL_BALANCED_NODE
 */
typedef struct NT_RTL_BALANCED_NODE64
{
    uint64_t Left;
    uint64_t Right;

    /**
     * Pointer to the parent node and flags in the least significant bits.
     *
     *  RB: 1 bit: Set if RED, clear if BLACK.
     * AVL: 2 bits: Balance something.
     */
    uint64_t ParentAndFlags;
} NT_RTL_BALANCED_NODE64;

/**
 * 64-bit _RTL_RB_TREE
 */
typedef struct NT_RTL_RB_TREE64
{
    uint64_t Root;  /**< Address of the root node (NT_RTL_BALANCED_NODE64). */
    /** The address of the left-most node (NT_RTL_BALANCED_NODE64).
     *  Bit 0 is used to indicate 'Encoded', meaning that Left and Right are
     *  XORed with the node address and Min is XORed with the address of this
     *  structure. */
    uint64_t Min;
} NT_RTL_RB_TREE64;


/** Initializes a DBGC variable with a GC flat pointer. */
DECLINLINE(PDBGCVAR) dbgCmdNtVarFromGCFlatPtr(PDBGCVAR pVar, RTGCPTR GCFlat)
{
    pVar->enmType      = DBGCVAR_TYPE_GC_FLAT;
    pVar->u.GCFlat     = GCFlat;

    pVar->enmRangeType = DBGCVAR_RANGE_NONE;
    pVar->u64Range     = 0;
    pVar->pDesc        = NULL;
    pVar->pNext        = NULL;
    return pVar;
}


/** Worker for dbgcCmdNtRbTree. */
template<typename TreeType = NT_RTL_RB_TREE64, typename NodeType = NT_RTL_BALANCED_NODE64, typename PtrType = uint64_t>
int dbgCmdNtRbTreeWorker(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PCDBGCVAR pRootAddr)
{
    int           rcRet          = VINF_SUCCESS;
    uint32_t      cErrors        = 0;
    PtrType const fAlignMask     = ~(PtrType)(sizeof(PtrType) - 1);
    PtrType const fAlignInfoMask =  (PtrType)(sizeof(PtrType) - 1);

    /*
     * Read the root structure.
     */
    TreeType Root;
    int rc = DBGCCmdHlpMemRead(pCmdHlp, &Root, sizeof(Root), pRootAddr, NULL /*cbRead*/);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "Failed to read tree structure");

    DBGCCmdHlpPrintf(pCmdHlp,
                     sizeof(PtrType) == sizeof(uint64_t)
                     ? "RB Root %DV: Root=%#016RX64 Min=%#016RX64%s\n"
                     : "RB Root %DV: Root=%#010RX32 Min=%#010RX32%s\n",
                     pRootAddr, Root.Root, Root.Min, Root.Min & 1 ? " Encoded=1" : "");
    if ((Root.Root & fAlignMask) == 0 || (Root.Min & fAlignMask) == 0)
    {
        if ((Root.Root & fAlignMask) == 0 && (Root.Min & fAlignMask) == 0)
            DBGCCmdHlpPrintf(pCmdHlp, "RB Root %DV: Empty\n", pRootAddr);
        else
            DBGCCmdHlpPrintf(pCmdHlp, "RB Root %DV: Bogus root state!\n", pRootAddr);
        return VINF_SUCCESS;
    }
    if (Root.Root & fAlignInfoMask)
    {
        rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd, "Misaligned Root pointer: %#RX64", Root.Root);
        cErrors++;
    }
    if ((Root.Min & fAlignInfoMask) > 1)
    {
        rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd, "Misaligned Min pointer: %#RX64 (bits 1+ should be zero)", Root.Min);
        cErrors++;
    }

    /*
     * To properly validate and avoid unnecessary memory reads, we use a stack
     * track tree traversal progress.   The stack consists of a copy of the node
     * and a boolean indicating whether or not the node has be processed.
     */
    struct
    {
        NodeType Node;      /**< Copy of the node. */
        PtrType  Ptr;       /**< The node address. */
        uint8_t  bState;    /**< 0 for going down left tree, 1 for the right, and 2 for done. */
        uint8_t  cBlacks;   /**< Number of black nodes thus far, including this one. */
    } aStack[28];
    uint32_t    cStackEntries = 0;

    /*
     * Push the root-node onto the stack.
     */
    PtrType     Ptr = Root.Root & fAlignMask;
    DBGCVAR     Var;
    rc = DBGCCmdHlpMemRead(pCmdHlp, &aStack[0].Node, sizeof(aStack[0].Node), dbgCmdNtVarFromGCFlatPtr(&Var, Ptr), NULL);
    if (RT_FAILURE(rc))
        return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "Failed to read the root node!");
    aStack[0].Ptr     = Ptr;
    aStack[0].bState  = 0;
    aStack[0].cBlacks = 1;
    if (aStack[0].Node.ParentAndFlags & fAlignMask)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Root->Parent != NULL: %#RX64", (uint64_t)aStack[0].Node.ParentAndFlags);
    if (aStack[0].Node.ParentAndFlags & 1)
        return DBGCCmdHlpFail(pCmdHlp, pCmd, "Root node is RED! Parent=%#RX64", (uint64_t)aStack[0].Node.ParentAndFlags);
    cStackEntries++;

    uint8_t  cReqBlacks  = 0; /**< Number of black nodes required in each path. Set when we reach the left most node. */
    uint8_t  cchMaxDepth = 32;
    uint32_t idxNode     = 0;
    while (cStackEntries > 0)
    {
        uint32_t const idxCurEntry = cStackEntries - 1;
        switch (aStack[idxCurEntry].bState)
        {
            case 0:
                /*
                 * Descend into the left tree, if any.
                 */
                if (aStack[idxCurEntry].Node.Left & fAlignInfoMask)
                {
                    rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd, "Misaligned Left pointer in node at %#RX64: %#RX64",
                                           (uint64_t)aStack[idxCurEntry].Ptr, (uint64_t)aStack[idxCurEntry].Node.Left);
                    cErrors++;
                }
                Ptr = aStack[idxCurEntry].Node.Left & fAlignMask;
                if (Ptr)
                {
                    if (cStackEntries < RT_ELEMENTS(aStack))
                    {
                        rc = DBGCCmdHlpMemRead(pCmdHlp, &aStack[cStackEntries].Node, sizeof(aStack[cStackEntries].Node),
                                               dbgCmdNtVarFromGCFlatPtr(&Var, Ptr), NULL);
                        if (RT_SUCCESS(rc))
                        {
                            if ((aStack[cStackEntries].Node.ParentAndFlags & fAlignMask) == aStack[idxCurEntry].Ptr)
                            {
                                aStack[idxCurEntry].bState    = 1;

                                aStack[cStackEntries].Ptr     = Ptr;
                                aStack[cStackEntries].cBlacks = aStack[idxCurEntry].cBlacks
                                                              + !(aStack[cStackEntries].Node.ParentAndFlags & 1);
                                aStack[cStackEntries].bState  = 0;
                                cStackEntries++;
                                continue;
                            }
                            rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd,
                                                   "Left node of %#RX64 at #%RX64 has an incorrect parent value: %#RX64 (Left=%#RX64, Right=%#RX64)",
                                                   (uint64_t)aStack[idxCurEntry].Ptr, Ptr, aStack[cStackEntries].Node.ParentAndFlags,
                                                   aStack[cStackEntries].Node.Left, aStack[cStackEntries].Node.Right);
                        }
                        else
                            rcRet = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc,
                                                     "Failed to read the node left of %#RX64 at address %#RX64!",
                                                     (uint64_t)aStack[idxCurEntry].Ptr, Ptr);
                    }
                    else
                        rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd, "Stack overflow descending to the left!");
                    cErrors++;
                }
                else if (idxNode != 0)
                {
                    uint8_t const cBlacksForCur = aStack[idxCurEntry].cBlacks + 1;
                    if (cBlacksForCur != cReqBlacks)
                    {
                        rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd, "Wrong black count to the left of %#RX64: %u, expected %u",
                                               aStack[idxCurEntry].Ptr, cBlacksForCur, cReqBlacks);
                        cErrors++;
                    }
                }
                else
                {
                    cReqBlacks  = aStack[idxCurEntry].cBlacks + 1;
                    cchMaxDepth = RT_MIN(cReqBlacks * 4, RT_ELEMENTS(aStack) * 2 + 2);

                    if ((Root.Min & ~(PtrType)1) != aStack[idxCurEntry].Ptr)
                    {
                        rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd,
                                               "Bogus Min node in tree anchor!  Left most node is %#RU64, expected %#RU64",
                                               aStack[idxCurEntry].Ptr, Root.Min);
                        cErrors++;
                    }
                }
                RT_FALL_THRU();

            case 1:
            {
                /*
                 * Process the current node.
                 */
#if 1
                char     szStack[72];
                uint32_t cchStack = cchMaxDepth - idxCurEntry * 2;
                //memset(szStack, ' ', cchStack);
                for (uint32_t volatile x = 0; x < cchStack; x++)
                    szStack[x] = ' ';

                if (aStack[idxCurEntry].Node.Left & fAlignMask)
                    szStack[cchStack++] = aStack[idxCurEntry].Node.Right & fAlignMask ? '>' : '`';
                else
                    szStack[cchStack++] = aStack[idxCurEntry].Node.Right & fAlignMask ? ',' : ' ';
                szStack[cchStack++] = aStack[idxCurEntry].Node.ParentAndFlags & 1 ? 'r' : 'B';

                uint32_t volatile offLast = cchStack;
                if (idxCurEntry > 0)
                {
                    uint32_t idxTmp = idxCurEntry - 1;
                    uint8_t  bPrev  = aStack[idxTmp].bState;
                    while (idxTmp-- > 0)
                    {
                        cchStack += 2;
                        if (aStack[idxTmp].bState != bPrev)
                        {
                            bPrev = aStack[idxTmp].bState;
                            while (offLast < cchStack)
                                szStack[offLast++] = ' ';
                            //memset(&szStack[offLast], ' ', cchStack - offLast);
                            szStack[cchStack] = '|';
                            offLast = cchStack + 1;
                        }
                    }
                }
                szStack[offLast] = '\0';

                DBGCCmdHlpPrintf(pCmdHlp,
                                 sizeof(PtrType) == sizeof(uint64_t)
                                 ? "#%4u/%2u at %#018RX64: Left=%#018RX64 Right=%#018RX64 Parent=%#018RX64 %s %s\n"
                                 : "#%4u/%2u at %#010RX32: Left=%#010RX32 Right=%#010RX32 Parent=%#010RX32 %s %s\n",
                                 idxNode, cStackEntries, aStack[idxCurEntry].Ptr, aStack[idxCurEntry].Node.Left,
                                 aStack[idxCurEntry].Node.Right, aStack[idxCurEntry].Node.ParentAndFlags & ~(PtrType)1,
                                 aStack[idxCurEntry].Node.ParentAndFlags & 1 ? "Red  " : "Black", szStack);
#else
                DBGCCmdHlpPrintf(pCmdHlp,
                                 sizeof(PtrType) == sizeof(uint64_t)
                                 ? "#%4u/%2u at %#018RX64: Left=%#018RX64 Right=%#018RX64 Parent=%#018RX64 %s %*s%s\n"
                                 : "#%4u/%2u at %#010RX32: Left=%#010RX32 Right=%#010RX32 Parent=%#010RX32 %s %*s%s\n",
                                 idxNode, cStackEntries, aStack[idxCurEntry].Ptr, aStack[idxCurEntry].Node.Left,
                                 aStack[idxCurEntry].Node.Right, aStack[idxCurEntry].Node.ParentAndFlags & ~(PtrType)1,
                                 aStack[idxCurEntry].Node.ParentAndFlags & 1 ? "Red  " : "Black",
                                 (cMaxDepth - idxCurEntry) * 2,
                                 aStack[idxCurEntry].Node.Left & fAlignMask
                                 ? (aStack[idxCurEntry].Node.Right & fAlignMask ? ">" : "`")
                                 : aStack[idxCurEntry].Node.Right & fAlignMask ? "," : "  ",
                                 aStack[idxCurEntry].Node.ParentAndFlags & 1 ? "r" : "B");
#endif
                idxNode++;

                /* Check that there are no RED -> RED sequences. */
                if (   (aStack[idxCurEntry].Node.ParentAndFlags & 1)
                    && idxCurEntry > 0
                    && (aStack[idxCurEntry - 1].Node.ParentAndFlags & 1))
                {
                    rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd, "RED child (%#RX64) with RED (%#RX64) parent!\n",
                                           (uint64_t)aStack[idxCurEntry].Ptr, (uint64_t)aStack[idxCurEntry - 1].Ptr);
                    cErrors++;
                }

                /*
                 * Descend into the right tree, if present.
                 */
                if (aStack[idxCurEntry].Node.Right & fAlignInfoMask)
                {
                    rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd, "Misaligned Right pointer in node at %#RX64: %#RX64",
                                           (uint64_t)aStack[idxCurEntry].Ptr, (uint64_t)aStack[idxCurEntry].Node.Right);
                    cErrors++;
                }
                Ptr = aStack[idxCurEntry].Node.Right & fAlignMask;
                if (Ptr)
                {
                    if (cStackEntries < RT_ELEMENTS(aStack))
                    {
                        rc = DBGCCmdHlpMemRead(pCmdHlp, &aStack[cStackEntries].Node, sizeof(aStack[cStackEntries].Node),
                                               dbgCmdNtVarFromGCFlatPtr(&Var, Ptr), NULL);
                        if (RT_SUCCESS(rc))
                        {
                            if ((aStack[cStackEntries].Node.ParentAndFlags & fAlignMask) == aStack[idxCurEntry].Ptr)
                            {
                                aStack[idxCurEntry].bState    = 2;

                                aStack[cStackEntries].Ptr     = Ptr;
                                aStack[cStackEntries].cBlacks = aStack[idxCurEntry].cBlacks
                                                              + !(aStack[cStackEntries].Node.ParentAndFlags & 1);
                                aStack[cStackEntries].bState  = 0;
                                cStackEntries++;
                                continue;
                            }
                            rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd,
                                                   "Right node of #%RX64 at #%RX64 has an incorrect parent value: %#RX64 (Left=%#RX64, Right=%#RX64)",
                                                   (uint64_t)aStack[idxCurEntry].Ptr, Ptr, aStack[cStackEntries].Node.ParentAndFlags,
                                                   aStack[cStackEntries].Node.Left, aStack[cStackEntries].Node.Right);
                        }
                        else
                            rcRet = DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "Failed to read the node right of %#RX64 at address %#RX64!",
                                                     (uint64_t)aStack[idxCurEntry].Ptr, Ptr);
                    }
                    else
                        rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd, "Stack overflow descending to the right!");
                    cErrors++;
                }
                else
                {
                    uint8_t const cBlacksForCur = aStack[idxCurEntry].cBlacks + 1;
                    if (cBlacksForCur != cReqBlacks)
                    {
                        rcRet = DBGCCmdHlpFail(pCmdHlp, pCmd, "Wrong black count to the right of %#RX64: %u, expected %u",
                                               aStack[idxCurEntry].Ptr, cBlacksForCur, cReqBlacks);
                        cErrors++;
                    }
                }
                RT_FALL_THRU();
            }

            case 2:
                /* We've returned from the right tree and should pop this entry now. */
                cStackEntries--;
                break;

            default:
                AssertFailedReturn(DBGCCmdHlpFail(pCmdHlp, pCmd, "Internal stack state error: [%u] = %u",
                                                  idxCurEntry, aStack[idxCurEntry].bState));
        }

    }

    if (!cErrors)
        DBGCCmdHlpPrintf(pCmdHlp, "No obvious errors\n");
    else
        DBGCCmdHlpPrintf(pCmdHlp, "%u tree errors\n", cErrors);
    return rcRet;
}

/**
 * @callback_method_impl{FNDBGCCMD, The 'ntrbtree' command.}
 */
DECLCALLBACK(int) dbgcCmdNtRbTree(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    /* Check parsing. */
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, cArgs == 1);
    DBGC_CMDHLP_ASSERT_PARSER_RET(pCmdHlp, pCmd, 0, DBGCVAR_ISGCPOINTER(paArgs[0].enmType));
    RT_NOREF(pUVM);

    /* ASSUME 64-bit for now. */
    return dbgCmdNtRbTreeWorker<NT_RTL_RB_TREE64, NT_RTL_BALANCED_NODE64, uint64_t>(pCmd, pCmdHlp, &paArgs[0]);
}

