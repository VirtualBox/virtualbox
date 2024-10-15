/* $Id: expreval.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Expression Evaluator.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_expreval_h
#define IPRT_INCLUDED_expreval_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_expr_eval  RTExprEval - Expression Evaluator
 * @ingroup grp_rt
 * @{ */

/** Handle to an expression evaluator. */
typedef struct RTEXPREVALINT *RTEXPREVAL;
/** Pointer to an expression evaluator handle. */
typedef RTEXPREVAL *PRTEXPREVAL;
/** NIL expression evaluator handle. */
#define NIL_RTEXPREVAL            ((RTEXPREVAL)~(uintptr_t)0)

/**
 * Variable getter (supplied by user).
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if the variable does not exist.
 */
typedef DECLCALLBACKTYPE(int, FNRTEXPREVALQUERYVARIABLE,(const char *pchName, size_t cchName, void *pvUser, char **ppszValue));
/** Pointer to a variable getter. */
typedef FNRTEXPREVALQUERYVARIABLE *PFNRTEXPREVALQUERYVARIABLE;

/** @name Expression evaluator flags.
 * @sa RTExprEvalCreate
 * @{ */
/** Default to hexadecimal instead of decimal numbers. */
#define RTEXPREVAL_F_DEFAULT_BASE_16    RT_BIT_64(0)
/** Enables C-ish octal style, i.e. 0777 be read as 0x1ff (in hex). */
#define RTEXPREVAL_F_C_OCTAL            RT_BIT_64(1)
/** Enables the 'exists' operator that can be used to check if a path exists.
 * @sa RTPathExists */
#define RTEXPREVAL_F_EXISTS_OP          RT_BIT_64(2)
/** Valid mask. */
#define RTEXPREVAL_F_VALID_MASK         UINT64_MAX(3)
/** @} */

/**
 * Creates an expression evaluator.
 *
 * @returns IPRT status code.
 * @param   phEval              Where to return the handle to the evaluator.
 * @param   fFlags              RTEXPREVAL_F_XXX.
 * @param   pszName             The evaluator name (for logging).
 * @param   pvUser              User argument for callbacks.
 * @param   pfnQueryVariable    Callback for querying variables. Optional.
 */
RTDECL(int) RTExprEvalCreate(PRTEXPREVAL phEval, uint64_t fFlags, const char *pszName,
                             void *pvUser, PFNRTEXPREVALQUERYVARIABLE pfnQueryVariable);

/**
 * Retains a reference to the evaluator.
 *
 * @returns New reference count, UINT32_MAX if @a hEval is not valid.
 * @param   hEval               Handle to the evaluator.
 */
RTDECL(uint32_t) RTExprEvalRetain(RTEXPREVAL hEval);

/**
 * Releases a reference to the evaluator.
 *
 * @returns New reference count, UINT32_MAX if @a hEval is not valid.  (The
 *          evaluator was destroyed when 0 is returned.)
 * @param   hEval               Handle to the evaluator.
 */
RTDECL(uint32_t) RTExprEvalRelease(RTEXPREVAL hEval);

/**
 * Evaluates the given if expression to a boolean result.
 *
 * @returns IPRT status code
 * @param   hEval       Handle to the evaluator.
 * @param   pch         The expression string.  Does not need to be zero
 *                      terminated.
 * @param   cch         The length of the expression.  Pass RTSTR_MAX if not
 *                      known.
 * @param   pfResult    Where to return the result.
 * @param   pErrInfo    Where to return additional error info.
 */
RTDECL(int) RTExprEvalToBool(RTEXPREVAL hEval, const char *pch, size_t cch, bool *pfResult, PRTERRINFO pErrInfo);

/**
 * Evaluates the given if expression to an integer (signed 64-bit) result.
 *
 * @returns IPRT status code
 * @param   hEval       Handle to the evaluator.
 * @param   pch         The expression string.  Does not need to be zero
 *                      terminated.
 * @param   cch         The length of the expression.  Pass RTSTR_MAX if not
 *                      known.
 * @param   piResult    Where to return the result.
 * @param   pErrInfo    Where to return additional error info.
 */
RTDECL(int) RTExprEvalToInteger(RTEXPREVAL hEval, const char *pch, size_t cch, int64_t *piResult, PRTERRINFO pErrInfo);

/**
 * Evaluates the given if expression to a string result.
 *
 * @returns IPRT status code
 * @param   hEval       Handle to the evaluator.
 * @param   pch         The expression string.  Does not need to be zero
 *                      terminated.
 * @param   cch         The length of the expression.  Pass RTSTR_MAX if not
 *                      known.
 * @param   ppszResult  Where to return the result.  This must be freed using
 *                      RTStrFree.
 * @param   pErrInfo    Where to return additional error info.
 */
RTDECL(int) RTExprEvalToString(RTEXPREVAL hEval, const char *pch, size_t cch, char **ppszResult, PRTERRINFO pErrInfo);

/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_expreval_h */

