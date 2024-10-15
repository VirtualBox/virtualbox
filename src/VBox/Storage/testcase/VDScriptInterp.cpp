/* $Id: VDScriptInterp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox HDD container test utility - scripting engine, interpreter.
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
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include <VBox/log.h>

#include "VDScriptAst.h"
#include "VDScriptStack.h"
#include "VDScriptInternal.h"

/**
 * Interpreter variable.
 */
typedef struct VDSCRIPTINTERPVAR
{
    /** String space core. */
    RTSTRSPACECORE              Core;
    /** Value. */
    VDSCRIPTARG                 Value;
} VDSCRIPTINTERPVAR;
/** Pointer to an interpreter variable. */
typedef VDSCRIPTINTERPVAR *PVDSCRIPTINTERPVAR;

/**
 * Block scope.
 */
typedef struct VDSCRIPTINTERPSCOPE
{
    /** Pointer to the enclosing scope if available. */
    struct VDSCRIPTINTERPSCOPE *pParent;
    /** String space of declared variables in this scope. */
    RTSTRSPACE                  hStrSpaceVar;
} VDSCRIPTINTERPSCOPE;
/** Pointer to a scope block. */
typedef VDSCRIPTINTERPSCOPE *PVDSCRIPTINTERPSCOPE;

/**
 * Function call.
 */
typedef struct VDSCRIPTINTERPFNCALL
{
    /** Pointer to the caller of this function. */
    struct VDSCRIPTINTERPFNCALL *pCaller;
    /** Root scope of this function. */
    VDSCRIPTINTERPSCOPE          ScopeRoot;
    /** Current scope in this function. */
    PVDSCRIPTINTERPSCOPE         pScopeCurr;
} VDSCRIPTINTERPFNCALL;
/** Pointer to a function call. */
typedef VDSCRIPTINTERPFNCALL *PVDSCRIPTINTERPFNCALL;

/**
 * Interpreter context.
 */
typedef struct VDSCRIPTINTERPCTX
{
    /** Pointer to the script context. */
    PVDSCRIPTCTXINT             pScriptCtx;
    /** Current function call entry. */
    PVDSCRIPTINTERPFNCALL       pFnCallCurr;
    /** Stack of calculated values. */
    VDSCRIPTSTACK               StackValues;
    /** Evaluation control stack. */
    VDSCRIPTSTACK               StackCtrl;
} VDSCRIPTINTERPCTX;
/** Pointer to an interpreter context. */
typedef VDSCRIPTINTERPCTX *PVDSCRIPTINTERPCTX;

/**
 * Interpreter control type.
 */
typedef enum VDSCRIPTINTERPCTRLTYPE
{
    VDSCRIPTINTERPCTRLTYPE_INVALID = 0,
    /** Function call to evaluate now, all values are computed
     * and are stored on the value stack.
     */
    VDSCRIPTINTERPCTRLTYPE_FN_CALL,
    /** Cleanup the function call, deleting the scope and restoring the previous one. */
    VDSCRIPTINTERPCTRLTYPE_FN_CALL_CLEANUP,
    /** If statement to evaluate now, the guard is on the stack. */
    VDSCRIPTINTERPCTRLTYPE_IF,
    /** While or for statement. */
    VDSCRIPTINTERPCTRLTYPE_LOOP,
    /** switch statement. */
    VDSCRIPTINTERPCTRLTYPE_SWITCH,
    /** Compound statement. */
    VDSCRIPTINTERPCTRLTYPE_COMPOUND,
    /** 32bit blowup. */
    VDSCRIPTINTERPCTRLTYPE_32BIT_HACK = 0x7fffffff
} VDSCRIPTINTERPCTRLTYPE;
/** Pointer to a control type. */
typedef VDSCRIPTINTERPCTRLTYPE *PVDSCRIPTINTERPCTRLTYPE;

/**
 * Interpreter stack control entry.
 */
typedef struct VDSCRIPTINTERPCTRL
{
    /** Flag whether this entry points to an AST node to evaluate. */
    bool                           fEvalAst;
    /** Flag dependent data. */
    union
    {
        /** Pointer to the AST node to interprete. */
        PVDSCRIPTASTCORE           pAstNode;
        /** Interpreter control structure. */
        struct
        {
            /** Type of control. */
            VDSCRIPTINTERPCTRLTYPE enmCtrlType;
            /** Function call data. */
            struct
            {
                /** Function to call. */
                PVDSCRIPTFN        pFn;
            } FnCall;
            /** Compound statement. */
            struct
            {
                /** The compound statement node. */
                PVDSCRIPTASTSTMT   pStmtCompound;
                /** Current statement evaluated. */
                PVDSCRIPTASTSTMT   pStmtCurr;
            } Compound;
            /** Pointer to an AST node. */
            PVDSCRIPTASTCORE       pAstNode;
        } Ctrl;
    };
} VDSCRIPTINTERPCTRL;
/** Pointer to an exec stack control entry. */
typedef VDSCRIPTINTERPCTRL *PVDSCRIPTINTERPCTRL;

/**
 * Record an error while interpreting.
 *
 * @returns VBox status code passed.
 * @param   pThis      The interpreter context.
 * @param   rc         The status code to record.
 * @param   RT_SRC_POS Position in the source code.
 * @param   pszFmt     Format string.
 */
static int vdScriptInterpreterError(PVDSCRIPTINTERPCTX pThis, int rc, RT_SRC_POS_DECL, const char *pszFmt, ...)
{
    RT_NOREF1(pThis); RT_SRC_POS_NOREF();
    va_list va;
    va_start(va, pszFmt);
    RTPrintfV(pszFmt, va);
    va_end(va);
    return rc;
}

/**
 * Pops the topmost value from the value stack.
 *
 * @param   pThis      The interpreter context.
 * @param   pVal       Where to store the value.
 */
DECLINLINE(void) vdScriptInterpreterPopValue(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTARG pVal)
{
    PVDSCRIPTARG pValStack = (PVDSCRIPTARG)vdScriptStackGetUsed(&pThis->StackValues);
    if (!pValStack)
    {
        RT_ZERO(*pVal);
        AssertPtrReturnVoid(pValStack);
    }

    *pVal = *pValStack;
    vdScriptStackPop(&pThis->StackValues);
}

/**
 * Pushes a given value onto the value stack.
 */
DECLINLINE(int) vdScriptInterpreterPushValue(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTARG pVal)
{
    PVDSCRIPTARG pValStack = (PVDSCRIPTARG)vdScriptStackGetUnused(&pThis->StackValues);
    if (!pValStack)
        return vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory pushing a value on the value stack");

    *pValStack = *pVal;
    vdScriptStackPush(&pThis->StackValues);
    return VINF_SUCCESS;
}

/**
 * Pushes an AST node onto the control stack.
 *
 * @returns VBox status code.
 * @param   pThis          The interpreter context.
 * @param   enmCtrlType    The control entry type.
 */
DECLINLINE(int) vdScriptInterpreterPushAstEntry(PVDSCRIPTINTERPCTX pThis,
                                                PVDSCRIPTASTCORE pAstNode)
{
    PVDSCRIPTINTERPCTRL pCtrl = NULL;

    pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUnused(&pThis->StackCtrl);

    if (pCtrl)
    {
        pCtrl->fEvalAst = true;
        pCtrl->pAstNode = pAstNode;
        vdScriptStackPush(&pThis->StackCtrl);
        return VINF_SUCCESS;
    }

    return vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory adding an entry on the control stack");
}

/**
 * Pushes a control entry without additional data onto the stack.
 *
 * @returns VBox status code.
 * @param   pThis          The interpreter context.
 * @param   enmCtrlType    The control entry type.
 */
DECLINLINE(int) vdScriptInterpreterPushNonDataCtrlEntry(PVDSCRIPTINTERPCTX pThis,
                                                        VDSCRIPTINTERPCTRLTYPE enmCtrlType)
{
    PVDSCRIPTINTERPCTRL pCtrl = NULL;

    pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUnused(&pThis->StackCtrl);
    if (pCtrl)
    {
        pCtrl->fEvalAst = false;
        pCtrl->Ctrl.enmCtrlType = enmCtrlType;
        vdScriptStackPush(&pThis->StackCtrl);
        return VINF_SUCCESS;
    }

    return vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory adding an entry on the control stack");
}

/**
 * Pushes a compound statement control entry onto the stack.
 *
 * @returns VBox status code.
 * @param   pThis          The interpreter context.
 * @param   pStmtFirst     The first statement of the compound statement
 */
DECLINLINE(int) vdScriptInterpreterPushCompoundCtrlEntry(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTASTSTMT pStmt)
{
    PVDSCRIPTINTERPCTRL pCtrl = NULL;

    pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUnused(&pThis->StackCtrl);
    if (pCtrl)
    {
        pCtrl->fEvalAst = false;
        pCtrl->Ctrl.enmCtrlType = VDSCRIPTINTERPCTRLTYPE_COMPOUND;
        pCtrl->Ctrl.Compound.pStmtCompound = pStmt;
        pCtrl->Ctrl.Compound.pStmtCurr = RTListGetFirst(&pStmt->Compound.ListStmts, VDSCRIPTASTSTMT, Core.ListNode);
        vdScriptStackPush(&pThis->StackCtrl);
        return VINF_SUCCESS;
    }

    return vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory adding an entry on the control stack");
}

/**
 * Pushes a while statement control entry onto the stack.
 *
 * @returns VBox status code.
 * @param   pThis          The interpreter context.
 * @param   pStmt          The while statement.
 */
DECLINLINE(int) vdScriptInterpreterPushWhileCtrlEntry(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTASTSTMT pStmt)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTINTERPCTRL pCtrl = NULL;

    pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUnused(&pThis->StackCtrl);
    if (pCtrl)
    {
        pCtrl->fEvalAst = false;
        pCtrl->Ctrl.enmCtrlType = VDSCRIPTINTERPCTRLTYPE_LOOP;
        pCtrl->Ctrl.pAstNode    = &pStmt->Core;
        vdScriptStackPush(&pThis->StackCtrl);

        rc = vdScriptInterpreterPushAstEntry(pThis, &pStmt->While.pCond->Core);
        if (   RT_SUCCESS(rc)
            && pStmt->While.fDoWhile)
        {
            /* Push the statement to execute for do ... while loops because they run at least once. */
            rc = vdScriptInterpreterPushAstEntry(pThis, &pStmt->While.pStmt->Core);
            if (RT_FAILURE(rc))
            {
                /* Cleanup while control statement and AST node. */
                vdScriptStackPop(&pThis->StackCtrl);
                vdScriptStackPop(&pThis->StackCtrl);
            }
        }
        else if (RT_FAILURE(rc))
            vdScriptStackPop(&pThis->StackCtrl); /* Cleanup the while control statement. */
    }
    else
        rc = vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory adding an entry on the control stack");

    return rc;
}

/**
 * Pushes an if statement control entry onto the stack.
 *
 * @returns VBox status code.
 * @param   pThis          The interpreter context.
 * @param   pStmt          The if statement.
 */
static int vdScriptInterpreterPushIfCtrlEntry(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTASTSTMT pStmt)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTINTERPCTRL pCtrl = NULL;

    pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUnused(&pThis->StackCtrl);
    if (pCtrl)
    {
        pCtrl->fEvalAst = false;
        pCtrl->Ctrl.enmCtrlType = VDSCRIPTINTERPCTRLTYPE_IF;
        pCtrl->Ctrl.pAstNode    = &pStmt->Core;
        vdScriptStackPush(&pThis->StackCtrl);

        rc = vdScriptInterpreterPushAstEntry(pThis, &pStmt->If.pCond->Core);
    }
    else
        rc = vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory adding an entry on the control stack");

    return rc;
}

/**
 * Pushes a for statement control entry onto the stack.
 *
 * @returns VBox status code.
 * @param   pThis          The interpreter context.
 * @param   pStmt          The while statement.
 */
DECLINLINE(int) vdScriptInterpreterPushForCtrlEntry(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTASTSTMT pStmt)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTINTERPCTRL pCtrl = NULL;

    pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUnused(&pThis->StackCtrl);
    if (pCtrl)
    {
        pCtrl->fEvalAst = false;
        pCtrl->Ctrl.enmCtrlType = VDSCRIPTINTERPCTRLTYPE_LOOP;
        pCtrl->Ctrl.pAstNode    = &pStmt->Core;
        vdScriptStackPush(&pThis->StackCtrl);

        /* Push the conditional first and the initializer .*/
        rc = vdScriptInterpreterPushAstEntry(pThis, &pStmt->For.pExprCond->Core);
        if (RT_SUCCESS(rc))
        {
            rc = vdScriptInterpreterPushAstEntry(pThis, &pStmt->For.pExprStart->Core);
            if (RT_FAILURE(rc))
                vdScriptStackPop(&pThis->StackCtrl);
        }
    }
    else
        rc = vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory adding an entry on the control stack");

    return rc;
}

/**
 * Destroy variable string space callback.
 */
static DECLCALLBACK(int) vdScriptInterpreterVarSpaceDestroy(PRTSTRSPACECORE pStr, void *pvUser)
{
    RT_NOREF1(pvUser);
    RTMemFree(pStr);
    return VINF_SUCCESS;
}

/**
 * Setsup a new scope in the current function call.
 *
 * @returns VBox status code.
 * @param   pThis          The interpreter context.
 */
static int vdScriptInterpreterScopeCreate(PVDSCRIPTINTERPCTX pThis)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTINTERPSCOPE pScope = (PVDSCRIPTINTERPSCOPE)RTMemAllocZ(sizeof(VDSCRIPTINTERPSCOPE));
    if (pScope)
    {
        pScope->pParent = pThis->pFnCallCurr->pScopeCurr;
        pScope->hStrSpaceVar = NULL;
        pThis->pFnCallCurr->pScopeCurr = pScope;
    }
    else
        rc = vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory allocating new scope");

    return rc;
}

/**
 * Destroys the current scope.
 *
 * @param   pThis          The interpreter context.
 */
static void vdScriptInterpreterScopeDestroyCurr(PVDSCRIPTINTERPCTX pThis)
{
    AssertMsgReturnVoid(pThis->pFnCallCurr->pScopeCurr != &pThis->pFnCallCurr->ScopeRoot,
                        ("Current scope is root scope of function call\n"));

    PVDSCRIPTINTERPSCOPE pScope = pThis->pFnCallCurr->pScopeCurr;
    pThis->pFnCallCurr->pScopeCurr = pScope->pParent;
    RTStrSpaceDestroy(&pScope->hStrSpaceVar, vdScriptInterpreterVarSpaceDestroy, NULL);
    RTMemFree(pScope);
}

/**
 * Get the content of the given variable identifier from the current or parent scope.
 */
static PVDSCRIPTINTERPVAR vdScriptInterpreterGetVar(PVDSCRIPTINTERPCTX pThis, const char *pszVar)
{
    PVDSCRIPTINTERPSCOPE pScopeCurr = pThis->pFnCallCurr->pScopeCurr;
    PVDSCRIPTINTERPVAR pVar = NULL;

    while (   !pVar
           && pScopeCurr)
    {
        pVar = (PVDSCRIPTINTERPVAR)RTStrSpaceGet(&pScopeCurr->hStrSpaceVar, pszVar);
        if (pVar)
            break;
        pScopeCurr = pScopeCurr->pParent;
    }


    return pVar;
}

/**
 * Evaluate an expression.
 *
 * @returns VBox status code.
 * @param   pThis      The interpreter context.
 * @param   pExpr      The expression to evaluate.
 */
static int vdScriptInterpreterEvaluateExpression(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTASTEXPR pExpr)
{
    int rc = VINF_SUCCESS;

    switch (pExpr->enmType)
    {
        case VDSCRIPTEXPRTYPE_PRIMARY_NUMCONST:
        {
            /* Push the numerical constant on the value stack. */
            VDSCRIPTARG NumConst;
            NumConst.enmType = VDSCRIPTTYPE_UINT64;
            NumConst.u64     = pExpr->u64;
            rc = vdScriptInterpreterPushValue(pThis, &NumConst);
            break;
        }
        case VDSCRIPTEXPRTYPE_PRIMARY_STRINGCONST:
        {
            /* Push the string literal on the value stack. */
            VDSCRIPTARG StringConst;
            StringConst.enmType = VDSCRIPTTYPE_STRING;
            StringConst.psz     = pExpr->pszStr;
            rc = vdScriptInterpreterPushValue(pThis, &StringConst);
            break;
        }
        case VDSCRIPTEXPRTYPE_PRIMARY_BOOLEAN:
        {
            VDSCRIPTARG BoolConst;
            BoolConst.enmType = VDSCRIPTTYPE_BOOL;
            BoolConst.f       = pExpr->f;
            rc = vdScriptInterpreterPushValue(pThis, &BoolConst);
            break;
        }
        case VDSCRIPTEXPRTYPE_PRIMARY_IDENTIFIER:
        {
            /* Look it up and push the value onto the value stack. */
            PVDSCRIPTINTERPVAR pVar = vdScriptInterpreterGetVar(pThis, pExpr->pIde->aszIde);

            AssertPtrReturn(pVar, VERR_IPE_UNINITIALIZED_STATUS);
            rc = vdScriptInterpreterPushValue(pThis, &pVar->Value);
            break;
        }
        case VDSCRIPTEXPRTYPE_POSTFIX_INCREMENT:
        case VDSCRIPTEXPRTYPE_POSTFIX_DECREMENT:
            AssertMsgFailed(("TODO\n"));
            RT_FALL_THRU();
        case VDSCRIPTEXPRTYPE_POSTFIX_FNCALL:
        {
            PVDSCRIPTFN pFn = (PVDSCRIPTFN)RTStrSpaceGet(&pThis->pScriptCtx->hStrSpaceFn, pExpr->FnCall.pFnIde->pIde->aszIde);
            if (pFn)
            {
                /* Push a function call control entry on the stack. */
                PVDSCRIPTINTERPCTRL pCtrlFn = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUnused(&pThis->StackCtrl);
                if (pCtrlFn)
                {
                    pCtrlFn->fEvalAst = false;
                    pCtrlFn->Ctrl.enmCtrlType = VDSCRIPTINTERPCTRLTYPE_FN_CALL;
                    pCtrlFn->Ctrl.FnCall.pFn = pFn;
                    vdScriptStackPush(&pThis->StackCtrl);

                    /* Push parameter expressions on the stack. */
                    PVDSCRIPTASTEXPR pArg = RTListGetFirst(&pExpr->FnCall.ListArgs, VDSCRIPTASTEXPR, Core.ListNode);
                    while (pArg)
                    {
                        rc = vdScriptInterpreterPushAstEntry(pThis, &pArg->Core);
                        if (RT_FAILURE(rc))
                            break;
                        pArg = RTListGetNext(&pExpr->FnCall.ListArgs, pArg, VDSCRIPTASTEXPR, Core.ListNode);
                    }
                }
            }
            else
                AssertMsgFailed(("Invalid program given, unknown function: %s\n", pExpr->FnCall.pFnIde->pIde->aszIde));
            break;
        }
        case VDSCRIPTEXPRTYPE_UNARY_INCREMENT:
        case VDSCRIPTEXPRTYPE_UNARY_DECREMENT:
        case VDSCRIPTEXPRTYPE_UNARY_POSSIGN:
        case VDSCRIPTEXPRTYPE_UNARY_NEGSIGN:
        case VDSCRIPTEXPRTYPE_UNARY_INVERT:
        case VDSCRIPTEXPRTYPE_UNARY_NEGATE:
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
        case VDSCRIPTEXPRTYPE_ASSIGNMENT_LIST:
            AssertMsgFailed(("TODO\n"));
            RT_FALL_THRU();
        default:
            AssertMsgFailed(("Invalid expression type: %d\n", pExpr->enmType));
    }
    return rc;
}

/**
 * Evaluate a statement.
 *
 * @returns VBox status code.
 * @param   pThis      The interpreter context.
 * @param   pStmt      The statement to evaluate.
 */
static int vdScriptInterpreterEvaluateStatement(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTASTSTMT pStmt)
{
    int rc = VINF_SUCCESS;

    switch (pStmt->enmStmtType)
    {
        case VDSCRIPTSTMTTYPE_COMPOUND:
        {
            /* Setup new scope. */
            rc = vdScriptInterpreterScopeCreate(pThis);
            if (RT_SUCCESS(rc))
            {
                /** @todo Declarations */
                rc = vdScriptInterpreterPushCompoundCtrlEntry(pThis, pStmt);
            }
            break;
        }
        case VDSCRIPTSTMTTYPE_EXPRESSION:
        {
            rc = vdScriptInterpreterPushAstEntry(pThis, &pStmt->pExpr->Core);
            break;
        }
        case VDSCRIPTSTMTTYPE_IF:
        {
            rc = vdScriptInterpreterPushIfCtrlEntry(pThis, pStmt);
            break;
        }
        case VDSCRIPTSTMTTYPE_SWITCH:
            AssertMsgFailed(("TODO\n"));
            break;
        case VDSCRIPTSTMTTYPE_WHILE:
        {
            rc = vdScriptInterpreterPushWhileCtrlEntry(pThis, pStmt);
            break;
        }
        case VDSCRIPTSTMTTYPE_RETURN:
        {
            /* Walk up the control stack until we reach a function cleanup entry. */
            PVDSCRIPTINTERPCTRL pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUsed(&pThis->StackCtrl);
            while (   pCtrl
                   && (   pCtrl->fEvalAst
                       || pCtrl->Ctrl.enmCtrlType != VDSCRIPTINTERPCTRLTYPE_FN_CALL_CLEANUP))
            {
                /* Cleanup up any compound statement scope. */
                if (   !pCtrl->fEvalAst
                    && pCtrl->Ctrl.enmCtrlType == VDSCRIPTINTERPCTRLTYPE_COMPOUND)
                    vdScriptInterpreterScopeDestroyCurr(pThis);

                vdScriptStackPop(&pThis->StackCtrl);
                pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUsed(&pThis->StackCtrl);
            }
            AssertMsg(RT_VALID_PTR(pCtrl), ("Incorrect program, return outside of function\n"));
            break;
        }
        case VDSCRIPTSTMTTYPE_FOR:
        {
            rc = vdScriptInterpreterPushForCtrlEntry(pThis, pStmt);
            break;
        }
        case VDSCRIPTSTMTTYPE_CONTINUE:
        {
            /* Remove everything up to a loop control entry. */
            PVDSCRIPTINTERPCTRL pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUsed(&pThis->StackCtrl);
            while (   pCtrl
                   && (   pCtrl->fEvalAst
                       || pCtrl->Ctrl.enmCtrlType != VDSCRIPTINTERPCTRLTYPE_LOOP))
            {
                /* Cleanup up any compound statement scope. */
                if (   !pCtrl->fEvalAst
                    && pCtrl->Ctrl.enmCtrlType == VDSCRIPTINTERPCTRLTYPE_COMPOUND)
                    vdScriptInterpreterScopeDestroyCurr(pThis);

                vdScriptStackPop(&pThis->StackCtrl);
                pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUsed(&pThis->StackCtrl);
            }
            AssertMsg(RT_VALID_PTR(pCtrl), ("Incorrect program, continue outside of loop\n"));

            /* Put the conditionals for while and for loops onto the control stack again. */
            PVDSCRIPTASTSTMT pLoopStmt = (PVDSCRIPTASTSTMT)pCtrl->Ctrl.pAstNode;

            AssertMsg(   pLoopStmt->enmStmtType == VDSCRIPTSTMTTYPE_WHILE
                      || pLoopStmt->enmStmtType == VDSCRIPTSTMTTYPE_FOR,
                      ("Invalid statement type, must be for or while loop\n"));

            if (pLoopStmt->enmStmtType == VDSCRIPTSTMTTYPE_FOR)
                rc = vdScriptInterpreterPushAstEntry(pThis, &pLoopStmt->For.pExprCond->Core);
            else if (!pLoopStmt->While.fDoWhile)
                rc = vdScriptInterpreterPushAstEntry(pThis, &pLoopStmt->While.pCond->Core);
            break;
        }
        case VDSCRIPTSTMTTYPE_BREAK:
        {
            /* Remove everything including the loop control statement. */
            PVDSCRIPTINTERPCTRL pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUsed(&pThis->StackCtrl);
            while (   pCtrl
                   && (   pCtrl->fEvalAst
                       || pCtrl->Ctrl.enmCtrlType != VDSCRIPTINTERPCTRLTYPE_LOOP))
            {
                /* Cleanup up any compound statement scope. */
                if (   !pCtrl->fEvalAst
                    && pCtrl->Ctrl.enmCtrlType == VDSCRIPTINTERPCTRLTYPE_COMPOUND)
                    vdScriptInterpreterScopeDestroyCurr(pThis);

                vdScriptStackPop(&pThis->StackCtrl);
                pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUsed(&pThis->StackCtrl);
            }
            AssertMsg(RT_VALID_PTR(pCtrl), ("Incorrect program, break outside of loop\n"));
            vdScriptStackPop(&pThis->StackCtrl); /* Remove loop control statement. */
            break;
        }
        case VDSCRIPTSTMTTYPE_CASE:
        case VDSCRIPTSTMTTYPE_DEFAULT:
            AssertMsgFailed(("TODO\n"));
            break;
        default:
            AssertMsgFailed(("Invalid statement type: %d\n", pStmt->enmStmtType));
    }

    return rc;
}

/**
 * Evaluates the given AST node.
 *
 * @returns VBox statuse code.
 * @param   pThis      The interpreter context.
 * @param   pAstNode   The AST node to interpret.
 */
static int vdScriptInterpreterEvaluateAst(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTASTCORE pAstNode)
{
    int rc = VERR_NOT_IMPLEMENTED;

    switch (pAstNode->enmClass)
    {
        case VDSCRIPTASTCLASS_DECLARATION:
        {
            AssertMsgFailed(("TODO\n"));
            break;
        }
        case VDSCRIPTASTCLASS_STATEMENT:
        {
            rc = vdScriptInterpreterEvaluateStatement(pThis, (PVDSCRIPTASTSTMT)pAstNode);
            break;
        }
        case VDSCRIPTASTCLASS_EXPRESSION:
        {
            rc = vdScriptInterpreterEvaluateExpression(pThis, (PVDSCRIPTASTEXPR)pAstNode);
            break;
        }
        /* These should never ever appear here. */
        case VDSCRIPTASTCLASS_IDENTIFIER:
        case VDSCRIPTASTCLASS_FUNCTION:
        case VDSCRIPTASTCLASS_FUNCTIONARG:
        case VDSCRIPTASTCLASS_INVALID:
        default:
            AssertMsgFailed(("Invalid AST node class: %d\n", pAstNode->enmClass));
    }

    return rc;
}

/**
 * Evaluate a function call.
 *
 * @returns VBox status code.
 * @param   pThis      The interpreter context.
 * @param   pFn        The function execute.
 */
static int vdScriptInterpreterFnCall(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTFN pFn)
{
    int rc = VINF_SUCCESS;

    if (!pFn->fExternal)
    {
        PVDSCRIPTASTFN pAstFn = pFn->Type.Internal.pAstFn;

        /* Add function call cleanup marker on the stack first. */
        rc = vdScriptInterpreterPushNonDataCtrlEntry(pThis, VDSCRIPTINTERPCTRLTYPE_FN_CALL_CLEANUP);
        if (RT_SUCCESS(rc))
        {
            /* Create function call frame and set it up. */
            PVDSCRIPTINTERPFNCALL pFnCall = (PVDSCRIPTINTERPFNCALL)RTMemAllocZ(sizeof(VDSCRIPTINTERPFNCALL));
            if (pFnCall)
            {
                pFnCall->pCaller = pThis->pFnCallCurr;
                pFnCall->ScopeRoot.pParent = NULL;
                pFnCall->ScopeRoot.hStrSpaceVar = NULL;
                pFnCall->pScopeCurr = &pFnCall->ScopeRoot;

                /* Add the variables, remember order. The first variable in the argument has the value at the top of the value stack. */
                PVDSCRIPTASTFNARG pArg = RTListGetFirst(&pAstFn->ListArgs, VDSCRIPTASTFNARG, Core.ListNode);
                for (unsigned i = 0; i < pAstFn->cArgs; i++)
                {
                    PVDSCRIPTINTERPVAR pVar = (PVDSCRIPTINTERPVAR)RTMemAllocZ(sizeof(VDSCRIPTINTERPVAR));
                    if (pVar)
                    {
                        pVar->Core.pszString = pArg->pArgIde->aszIde;
                        pVar->Core.cchString = pArg->pArgIde->cchIde;
                        vdScriptInterpreterPopValue(pThis, &pVar->Value);
                        bool fInserted = RTStrSpaceInsert(&pFnCall->ScopeRoot.hStrSpaceVar, &pVar->Core);
                        Assert(fInserted); RT_NOREF_PV(fInserted);
                    }
                    else
                    {
                        rc = vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory creating a variable");
                        break;
                    }
                    pArg = RTListGetNext(&pAstFn->ListArgs, pArg, VDSCRIPTASTFNARG, Core.ListNode);
                }

                if (RT_SUCCESS(rc))
                {
                    /*
                     * Push compount statement on the control stack and make the newly created
                     * call frame the current one.
                     */
                    rc = vdScriptInterpreterPushAstEntry(pThis, &pAstFn->pCompoundStmts->Core);
                    if (RT_SUCCESS(rc))
                        pThis->pFnCallCurr = pFnCall;
                }

                if (RT_FAILURE(rc))
                {
                    RTStrSpaceDestroy(&pFnCall->ScopeRoot.hStrSpaceVar, vdScriptInterpreterVarSpaceDestroy, NULL);
                    RTMemFree(pFnCall);
                }
            }
            else
                rc = vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory creating a call frame");
        }
    }
    else
    {
        /* External function call, build the argument list. */
        if (pFn->cArgs)
        {
            PVDSCRIPTARG paArgs = (PVDSCRIPTARG)RTMemAllocZ(pFn->cArgs * sizeof(VDSCRIPTARG));
            if (paArgs)
            {
                for (unsigned i = 0; i < pFn->cArgs; i++)
                    vdScriptInterpreterPopValue(pThis, &paArgs[i]);

                rc = pFn->Type.External.pfnCallback(paArgs, pFn->Type.External.pvUser);
                RTMemFree(paArgs);
            }
            else
                rc = vdScriptInterpreterError(pThis, VERR_NO_MEMORY, RT_SRC_POS,
                                              "Out of memory creating argument array for external function call");
        }
        else
            rc = pFn->Type.External.pfnCallback(NULL, pFn->Type.External.pvUser);
    }

    return rc;
}

/**
 * Evaluate interpreter control statement.
 *
 * @returns VBox status code.
 * @param   pThis      The interpreter context.
 * @param   pCtrl      The control entry to evaluate.
 */
static int vdScriptInterpreterEvaluateCtrlEntry(PVDSCRIPTINTERPCTX pThis, PVDSCRIPTINTERPCTRL pCtrl)
{
    int rc = VINF_SUCCESS;

    Assert(!pCtrl->fEvalAst);
    switch (pCtrl->Ctrl.enmCtrlType)
    {
        case VDSCRIPTINTERPCTRLTYPE_FN_CALL:
        {
            PVDSCRIPTFN pFn = pCtrl->Ctrl.FnCall.pFn;

            vdScriptStackPop(&pThis->StackCtrl);
            rc = vdScriptInterpreterFnCall(pThis, pFn);
            break;
        }
        case VDSCRIPTINTERPCTRLTYPE_FN_CALL_CLEANUP:
        {
            vdScriptStackPop(&pThis->StackCtrl);

            /* Delete function call entry. */
            AssertPtr(pThis->pFnCallCurr);
            PVDSCRIPTINTERPFNCALL pFnCallFree = pThis->pFnCallCurr;

            pThis->pFnCallCurr = pFnCallFree->pCaller;
            Assert(pFnCallFree->pScopeCurr == &pFnCallFree->ScopeRoot);
            RTStrSpaceDestroy(&pFnCallFree->ScopeRoot.hStrSpaceVar, vdScriptInterpreterVarSpaceDestroy, NULL);
            RTMemFree(pFnCallFree);
            break;
        }
        case VDSCRIPTINTERPCTRLTYPE_COMPOUND:
        {
            if (!pCtrl->Ctrl.Compound.pStmtCurr)
            {
                /* Evaluated last statement, cleanup and remove the control statement from the stack. */
                vdScriptInterpreterScopeDestroyCurr(pThis);
                vdScriptStackPop(&pThis->StackCtrl);
            }
            else
            {
                /* Push the current statement onto the control stack and move on. */
                rc = vdScriptInterpreterPushAstEntry(pThis, &pCtrl->Ctrl.Compound.pStmtCurr->Core);
                if (RT_SUCCESS(rc))
                {
                    pCtrl->Ctrl.Compound.pStmtCurr = RTListGetNext(&pCtrl->Ctrl.Compound.pStmtCompound->Compound.ListStmts,
                                                                   pCtrl->Ctrl.Compound.pStmtCurr, VDSCRIPTASTSTMT, Core.ListNode);
                }
            }
            break;
        }
        case VDSCRIPTINTERPCTRLTYPE_LOOP:
        {
            PVDSCRIPTASTSTMT pLoopStmt = (PVDSCRIPTASTSTMT)pCtrl->Ctrl.pAstNode;

            /* Check whether the condition passed. */
            VDSCRIPTARG Cond;
            vdScriptInterpreterPopValue(pThis, &Cond);
            AssertMsg(Cond.enmType == VDSCRIPTTYPE_BOOL,
                      ("Value on stack is not of boolean type\n"));

            if (Cond.f)
            {
                /* Execute the loop another round. */
                if (pLoopStmt->enmStmtType == VDSCRIPTSTMTTYPE_WHILE)
                {
                    rc = vdScriptInterpreterPushAstEntry(pThis, &pLoopStmt->While.pCond->Core);
                    if (RT_SUCCESS(rc))
                    {
                        rc = vdScriptInterpreterPushAstEntry(pThis, &pLoopStmt->While.pStmt->Core);
                        if (RT_FAILURE(rc))
                            vdScriptStackPop(&pThis->StackCtrl);
                    }
                }
                else
                {
                    AssertMsg(pLoopStmt->enmStmtType == VDSCRIPTSTMTTYPE_FOR,
                              ("Not a for statement\n"));

                    rc = vdScriptInterpreterPushAstEntry(pThis, &pLoopStmt->For.pExprCond->Core);
                    if (RT_SUCCESS(rc))
                    {
                        rc = vdScriptInterpreterPushAstEntry(pThis, &pLoopStmt->For.pExpr3->Core);
                        if (RT_SUCCESS(rc))
                        {
                            rc = vdScriptInterpreterPushAstEntry(pThis, &pLoopStmt->For.pStmt->Core);
                            if (RT_FAILURE(rc))
                                vdScriptStackPop(&pThis->StackCtrl);
                        }

                        if (RT_FAILURE(rc))
                            vdScriptStackPop(&pThis->StackCtrl);
                    }
                }
            }
            else
                vdScriptStackPop(&pThis->StackCtrl); /* Remove loop control statement. */
            break;
        }
        case VDSCRIPTINTERPCTRLTYPE_IF:
        {
            PVDSCRIPTASTSTMT pIfStmt = (PVDSCRIPTASTSTMT)pCtrl->Ctrl.pAstNode;

            vdScriptStackPop(&pThis->StackCtrl); /* Remove if control statement. */

            /* Check whether the condition passed. */
            VDSCRIPTARG Cond;
            vdScriptInterpreterPopValue(pThis, &Cond);
            AssertMsg(Cond.enmType == VDSCRIPTTYPE_BOOL,
                      ("Value on stack is not of boolean type\n"));

            if (Cond.f)
            {
                /* Execute the true branch. */
                rc = vdScriptInterpreterPushAstEntry(pThis, &pIfStmt->If.pTrueStmt->Core);
            }
            else if (pIfStmt->If.pElseStmt)
                rc = vdScriptInterpreterPushAstEntry(pThis, &pIfStmt->If.pElseStmt->Core);

            break;
        }
        default:
            AssertMsgFailed(("Invalid evaluation control type on the stack: %d\n",
                             pCtrl->Ctrl.enmCtrlType));
    }

    return rc;
}

/**
 * The interpreter evaluation core loop.
 *
 * @returns VBox status code.
 * @param   pThis      The interpreter context.
 */
static int vdScriptInterpreterEvaluate(PVDSCRIPTINTERPCTX pThis)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTINTERPCTRL pCtrl = NULL;

    pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUsed(&pThis->StackCtrl);
    while (pCtrl)
    {
        if (pCtrl->fEvalAst)
        {
            PVDSCRIPTASTCORE pAstNode = pCtrl->pAstNode;
            vdScriptStackPop(&pThis->StackCtrl);

            rc = vdScriptInterpreterEvaluateAst(pThis, pAstNode);
        }
        else
            rc = vdScriptInterpreterEvaluateCtrlEntry(pThis, pCtrl);

        pCtrl = (PVDSCRIPTINTERPCTRL)vdScriptStackGetUsed(&pThis->StackCtrl);
    }

    return rc;
}

DECLHIDDEN(int) vdScriptCtxInterprete(PVDSCRIPTCTXINT pThis, const char *pszFn,
                                      PVDSCRIPTARG paArgs, unsigned cArgs,
                                      PVDSCRIPTARG pRet)
{
    RT_NOREF1(pRet);
    int rc = VINF_SUCCESS;
    VDSCRIPTINTERPCTX InterpCtx;
    PVDSCRIPTFN pFn = NULL;

    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pszFn, VERR_INVALID_POINTER);
    AssertReturn(   (!cArgs && !paArgs)
                 || (cArgs && paArgs), VERR_INVALID_PARAMETER);

    InterpCtx.pScriptCtx  = pThis;
    InterpCtx.pFnCallCurr = NULL;
    vdScriptStackInit(&InterpCtx.StackValues, sizeof(VDSCRIPTARG));
    vdScriptStackInit(&InterpCtx.StackCtrl, sizeof(VDSCRIPTINTERPCTRL));

    pFn = (PVDSCRIPTFN)RTStrSpaceGet(&pThis->hStrSpaceFn, pszFn);
    if (pFn)
    {
        if (cArgs == pFn->cArgs)
        {
            /* Push the arguments onto the stack. */
            /** @todo Check expected and given argument types. */
            for (unsigned i = 0; i < cArgs; i++)
            {
                PVDSCRIPTARG pArg = (PVDSCRIPTARG)vdScriptStackGetUnused(&InterpCtx.StackValues);
                *pArg = paArgs[i];
                vdScriptStackPush(&InterpCtx.StackValues);
            }

            if (RT_SUCCESS(rc))
            {
                /* Setup function call frame and parameters. */
                rc = vdScriptInterpreterFnCall(&InterpCtx, pFn);
                if (RT_SUCCESS(rc))
                {
                    /* Run the interpreter. */
                    rc = vdScriptInterpreterEvaluate(&InterpCtx);
                    vdScriptStackDestroy(&InterpCtx.StackValues);
                    vdScriptStackDestroy(&InterpCtx.StackCtrl);
                }
            }
        }
        else
            rc = vdScriptInterpreterError(&InterpCtx, VERR_INVALID_PARAMETER, RT_SRC_POS, "Invalid number of parameters, expected %d got %d", pFn->cArgs, cArgs);
    }
    else
        rc = vdScriptInterpreterError(&InterpCtx, VERR_NOT_FOUND, RT_SRC_POS, "Function with identifier \"%s\" not found", pszFn);


    return rc;
}
