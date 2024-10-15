/* $Id: VDScriptAst.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox HDD container test utility - scripting engine AST node related functions.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#define LOGGROUP LOGGROUP_DEFAULT
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#include <VBox/log.h>

#include "VDScriptAst.h"

/**
 * Put all child nodes of the given expression AST node onto the given to free list.
 *
 * @param   pList    The free list to append everything to.
 * @param   pAstNode The expression node to free.
 */
static void vdScriptAstNodeExpressionPutOnFreeList(PRTLISTANCHOR pList, PVDSCRIPTASTCORE pAstNode)
{
    AssertMsgReturnVoid(pAstNode->enmClass == VDSCRIPTASTCLASS_EXPRESSION,
                        ("Given AST node is not a statement\n"));

    PVDSCRIPTASTEXPR pExpr = (PVDSCRIPTASTEXPR)pAstNode;
    switch (pExpr->enmType)
    {
        case VDSCRIPTEXPRTYPE_PRIMARY_NUMCONST:
        case VDSCRIPTEXPRTYPE_PRIMARY_BOOLEAN:
            break;
        case VDSCRIPTEXPRTYPE_PRIMARY_STRINGCONST:
            RTStrFree((char *)pExpr->pszStr);
            break;
        case VDSCRIPTEXPRTYPE_PRIMARY_IDENTIFIER:
        {
            RTListAppend(pList, &pExpr->pIde->Core.ListNode);
            break;
        }
        case VDSCRIPTEXPRTYPE_ASSIGNMENT_LIST:
        {
            while (!RTListIsEmpty(&pExpr->ListExpr))
            {
                PVDSCRIPTASTCORE pNode = RTListGetFirst(&pExpr->ListExpr, VDSCRIPTASTCORE, ListNode);
                RTListNodeRemove(&pNode->ListNode);
                RTListAppend(pList, &pNode->ListNode);
            }
            break;
        }
        case VDSCRIPTEXPRTYPE_POSTFIX_FNCALL:
        {
            RTListAppend(pList, &pExpr->FnCall.pFnIde->Core.ListNode);
            while (!RTListIsEmpty(&pExpr->FnCall.ListArgs))
            {
                PVDSCRIPTASTCORE pNode = RTListGetFirst(&pExpr->FnCall.ListArgs, VDSCRIPTASTCORE, ListNode);
                RTListNodeRemove(&pNode->ListNode);
                RTListAppend(pList, &pNode->ListNode);
            }
            break;
        }
        case VDSCRIPTEXPRTYPE_POSTFIX_DEREFERENCE:
        case VDSCRIPTEXPRTYPE_POSTFIX_DOT:
        {
            RTListAppend(pList, &pExpr->Deref.pIde->Core.ListNode);
            RTListAppend(pList, &pExpr->Deref.pExpr->Core.ListNode);
            break;
        }
        case VDSCRIPTEXPRTYPE_POSTFIX_INCREMENT:
        case VDSCRIPTEXPRTYPE_POSTFIX_DECREMENT:
        case VDSCRIPTEXPRTYPE_UNARY_INCREMENT:
        case VDSCRIPTEXPRTYPE_UNARY_DECREMENT:
        case VDSCRIPTEXPRTYPE_UNARY_POSSIGN:
        case VDSCRIPTEXPRTYPE_UNARY_NEGSIGN:
        case VDSCRIPTEXPRTYPE_UNARY_INVERT:
        case VDSCRIPTEXPRTYPE_UNARY_NEGATE:
        case VDSCRIPTEXPRTYPE_UNARY_REFERENCE:
        case VDSCRIPTEXPRTYPE_UNARY_DEREFERENCE:
        {
            RTListAppend(pList, &pExpr->pExpr->Core.ListNode);
            break;
        }
        case VDSCRIPTEXPRTYPE_MULTIPLICATION:
        case VDSCRIPTEXPRTYPE_DIVISION:
        case VDSCRIPTEXPRTYPE_MODULUS:
        case VDSCRIPTEXPRTYPE_ADDITION:
        case VDSCRIPTEXPRTYPE_SUBTRACTION:
        case VDSCRIPTEXPRTYPE_LSR:
        case VDSCRIPTEXPRTYPE_LSL:
        case VDSCRIPTEXPRTYPE_LOWER:
        case VDSCRIPTEXPRTYPE_HIGHER:
        case VDSCRIPTEXPRTYPE_LOWEREQUAL:
        case VDSCRIPTEXPRTYPE_HIGHEREQUAL:
        case VDSCRIPTEXPRTYPE_EQUAL:
        case VDSCRIPTEXPRTYPE_NOTEQUAL:
        case VDSCRIPTEXPRTYPE_BITWISE_AND:
        case VDSCRIPTEXPRTYPE_BITWISE_XOR:
        case VDSCRIPTEXPRTYPE_BITWISE_OR:
        case VDSCRIPTEXPRTYPE_LOGICAL_AND:
        case VDSCRIPTEXPRTYPE_LOGICAL_OR:
        case VDSCRIPTEXPRTYPE_ASSIGN:
        case VDSCRIPTEXPRTYPE_ASSIGN_MULT:
        case VDSCRIPTEXPRTYPE_ASSIGN_DIV:
        case VDSCRIPTEXPRTYPE_ASSIGN_MOD:
        case VDSCRIPTEXPRTYPE_ASSIGN_ADD:
        case VDSCRIPTEXPRTYPE_ASSIGN_SUB:
        case VDSCRIPTEXPRTYPE_ASSIGN_LSL:
        case VDSCRIPTEXPRTYPE_ASSIGN_LSR:
        case VDSCRIPTEXPRTYPE_ASSIGN_AND:
        case VDSCRIPTEXPRTYPE_ASSIGN_XOR:
        case VDSCRIPTEXPRTYPE_ASSIGN_OR:
        {
            RTListAppend(pList, &pExpr->BinaryOp.pLeftExpr->Core.ListNode);
            RTListAppend(pList, &pExpr->BinaryOp.pRightExpr->Core.ListNode);
            break;
        }
        case VDSCRIPTEXPRTYPE_INVALID:
        default:
            AssertMsgFailedReturnVoid(("Invalid AST node expression type %d\n",
                                       pExpr->enmType));
    }
}

/**
 * Free a given statement AST node and put everything on the given to free list.
 *
 * @param   pList    The free list to append everything to.
 * @param   pAstNode The statement node to free.
 */
static void vdScriptAstNodeStatmentPutOnFreeList(PRTLISTANCHOR pList, PVDSCRIPTASTCORE pAstNode)
{
    AssertMsgReturnVoid(pAstNode->enmClass == VDSCRIPTASTCLASS_STATEMENT,
                        ("Given AST node is not a statement\n"));

    PVDSCRIPTASTSTMT pStmt = (PVDSCRIPTASTSTMT)pAstNode;
    switch (pStmt->enmStmtType)
    {
        case VDSCRIPTSTMTTYPE_COMPOUND:
        {
            /* Put all declarations on the to free list. */
            while (!RTListIsEmpty(&pStmt->Compound.ListDecls))
            {
                PVDSCRIPTASTCORE pNode = RTListGetFirst(&pStmt->Compound.ListDecls, VDSCRIPTASTCORE, ListNode);
                RTListNodeRemove(&pNode->ListNode);
                RTListAppend(pList, &pNode->ListNode);
            }

            /* Put all statements on the to free list. */
            while (!RTListIsEmpty(&pStmt->Compound.ListStmts))
            {
                PVDSCRIPTASTCORE pNode = RTListGetFirst(&pStmt->Compound.ListStmts, VDSCRIPTASTCORE, ListNode);
                RTListNodeRemove(&pNode->ListNode);
                RTListAppend(pList, &pNode->ListNode);
            }
            break;
        }
        case VDSCRIPTSTMTTYPE_EXPRESSION:
        {
            if (pStmt->pExpr)
                RTListAppend(pList, &pStmt->pExpr->Core.ListNode);
            break;
        }
        case VDSCRIPTSTMTTYPE_IF:
        {
            RTListAppend(pList, &pStmt->If.pCond->Core.ListNode);
            RTListAppend(pList, &pStmt->If.pTrueStmt->Core.ListNode);
            if (pStmt->If.pElseStmt)
                RTListAppend(pList, &pStmt->If.pElseStmt->Core.ListNode);
            break;
        }
        case VDSCRIPTSTMTTYPE_SWITCH:
        {
            RTListAppend(pList, &pStmt->Switch.pCond->Core.ListNode);
            RTListAppend(pList, &pStmt->Switch.pStmt->Core.ListNode);
            break;
        }
        case VDSCRIPTSTMTTYPE_WHILE:
        {
            RTListAppend(pList, &pStmt->While.pCond->Core.ListNode);
            RTListAppend(pList, &pStmt->While.pStmt->Core.ListNode);
            break;
        }
        case VDSCRIPTSTMTTYPE_FOR:
        {
            RTListAppend(pList, &pStmt->For.pExprStart->Core.ListNode);
            RTListAppend(pList, &pStmt->For.pExprCond->Core.ListNode);
            RTListAppend(pList, &pStmt->For.pExpr3->Core.ListNode);
            RTListAppend(pList, &pStmt->For.pStmt->Core.ListNode);
            break;
        }
        case VDSCRIPTSTMTTYPE_RETURN:
        {
            if (pStmt->pExpr)
                RTListAppend(pList, &pStmt->pExpr->Core.ListNode);
            break;
        }
        case VDSCRIPTSTMTTYPE_CASE:
        {
            RTListAppend(pList, &pStmt->Case.pExpr->Core.ListNode);
            RTListAppend(pList, &pStmt->Case.pStmt->Core.ListNode);
            break;
        }
        case VDSCRIPTSTMTTYPE_DEFAULT:
        {
            RTListAppend(pList, &pStmt->Case.pStmt->Core.ListNode);
            break;
        }
        case VDSCRIPTSTMTTYPE_CONTINUE:
        case VDSCRIPTSTMTTYPE_BREAK:
            break;
        case VDSCRIPTSTMTTYPE_INVALID:
        default:
            AssertMsgFailedReturnVoid(("Invalid AST node statement type %d\n",
                                       pStmt->enmStmtType));
    }
}

DECLHIDDEN(void) vdScriptAstNodeFree(PVDSCRIPTASTCORE pAstNode)
{
    RTLISTANCHOR ListFree;

    /*
     * The node is not allowed to be part of a list because we need it
     * for the nodes to free list.
     */
    Assert(RTListIsEmpty(&pAstNode->ListNode));
    RTListInit(&ListFree);
    RTListAppend(&ListFree, &pAstNode->ListNode);

    do
    {
        pAstNode = RTListGetFirst(&ListFree, VDSCRIPTASTCORE, ListNode);
        RTListNodeRemove(&pAstNode->ListNode);

        switch (pAstNode->enmClass)
        {
            case VDSCRIPTASTCLASS_FUNCTION:
            {
                PVDSCRIPTASTFN pFn = (PVDSCRIPTASTFN)pAstNode;

                if (pFn->pRetType)
                    RTListAppend(&ListFree, &pFn->pRetType->Core.ListNode);
                if (pFn->pFnIde)
                    RTListAppend(&ListFree, &pFn->pFnIde->Core.ListNode);

                /* Put argument list on the to free list. */
                while (!RTListIsEmpty(&pFn->ListArgs))
                {
                    PVDSCRIPTASTCORE pArg = RTListGetFirst(&pFn->ListArgs, VDSCRIPTASTCORE, ListNode);
                    RTListNodeRemove(&pArg->ListNode);
                    RTListAppend(&ListFree, &pArg->ListNode);
                }

                /* Put compound statement onto the list. */
                RTListAppend(&ListFree, &pFn->pCompoundStmts->Core.ListNode);
                break;
            }
            case VDSCRIPTASTCLASS_FUNCTIONARG:
            {
                PVDSCRIPTASTFNARG pAstNodeArg = (PVDSCRIPTASTFNARG)pAstNode;
                if (pAstNodeArg->pType)
                    RTListAppend(&ListFree, &pAstNodeArg->pType->Core.ListNode);
                if (pAstNodeArg->pArgIde)
                    RTListAppend(&ListFree, &pAstNodeArg->pArgIde->Core.ListNode);
                break;
            }
            case VDSCRIPTASTCLASS_IDENTIFIER:
                break;
            case VDSCRIPTASTCLASS_DECLARATION:
            case VDSCRIPTASTCLASS_TYPENAME:
            {
                AssertMsgFailed(("TODO\n"));
                break;
            }
            case VDSCRIPTASTCLASS_STATEMENT:
            {
                vdScriptAstNodeStatmentPutOnFreeList(&ListFree, pAstNode);
                break;
            }
            case VDSCRIPTASTCLASS_EXPRESSION:
            {
                vdScriptAstNodeExpressionPutOnFreeList(&ListFree, pAstNode);
                break;
            }
            case VDSCRIPTASTCLASS_INVALID:
            default:
                AssertMsgFailedReturnVoid(("Invalid AST node class given %d\n", pAstNode->enmClass));
        }

        RTMemFree(pAstNode);
    } while (!RTListIsEmpty(&ListFree));

}

DECLHIDDEN(PVDSCRIPTASTCORE) vdScriptAstNodeAlloc(VDSCRIPTASTCLASS enmClass)
{
    size_t cbAlloc = 0;

    switch (enmClass)
    {
        case VDSCRIPTASTCLASS_FUNCTION:
            cbAlloc = sizeof(VDSCRIPTASTFN);
            break;
        case VDSCRIPTASTCLASS_FUNCTIONARG:
            cbAlloc = sizeof(VDSCRIPTASTFNARG);
            break;
        case VDSCRIPTASTCLASS_DECLARATION:
            cbAlloc = sizeof(VDSCRIPTASTDECL);
            break;
        case VDSCRIPTASTCLASS_STATEMENT:
            cbAlloc = sizeof(VDSCRIPTASTSTMT);
            break;
        case VDSCRIPTASTCLASS_EXPRESSION:
            cbAlloc = sizeof(VDSCRIPTASTEXPR);
            break;
        case VDSCRIPTASTCLASS_TYPENAME:
            cbAlloc = sizeof(VDSCRIPTASTTYPENAME);
            break;
        case VDSCRIPTASTCLASS_IDENTIFIER:
        case VDSCRIPTASTCLASS_INVALID:
        default:
            AssertMsgFailedReturn(("Invalid AST node class given %d\n", enmClass), NULL);
    }

    PVDSCRIPTASTCORE pAstNode = (PVDSCRIPTASTCORE)RTMemAllocZ(cbAlloc);
    if (pAstNode)
    {
        pAstNode->enmClass = enmClass;
        RTListInit(&pAstNode->ListNode);
    }

    return pAstNode;
}

DECLHIDDEN(PVDSCRIPTASTIDE) vdScriptAstNodeIdeAlloc(size_t cchIde)
{
    PVDSCRIPTASTIDE pAstNode = (PVDSCRIPTASTIDE)RTMemAllocZ(RT_UOFFSETOF_DYN(VDSCRIPTASTIDE, aszIde[cchIde + 1]));
    if (pAstNode)
    {
        pAstNode->Core.enmClass = VDSCRIPTASTCLASS_IDENTIFIER;
        RTListInit(&pAstNode->Core.ListNode);
    }

    return pAstNode;
}
