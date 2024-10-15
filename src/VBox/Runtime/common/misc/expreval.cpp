/* $Id: expreval.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * expreval - Expressions evaluator.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/expreval.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/md5.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/sha.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max length of a string representation of a number. */
#define EXPR_NUM_LEN  ((sizeof("-9223372036854775802") + 4) & ~3)

/** The max operator stack depth. */
#define EXPR_MAX_OPERATORS      72
/** The max operand depth. */
#define EXPR_MAX_OPERANDS       128
/** the max variable recursion. */
#define EXPR_MAX_VAR_RECURSION  20

/** Check if @a a_ch is a valid separator for a alphabetical binary
 *  operator, omitting isspace. */
#define EXPR_IS_OP_SEPARATOR_NO_SPACE(a_ch) \
    (RT_C_IS_PUNCT((a_ch)) && (a_ch) != '@' && (a_ch) != '_')

/** Check if @a a_ch is a valid separator for a alphabetical binary operator. */
#define EXPR_IS_OP_SEPARATOR(a_ch) \
    (RT_C_IS_SPACE((a_ch)) || EXPR_IS_OP_SEPARATOR_NO_SPACE(a_ch))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** The 64-bit signed integer type we're using. */
typedef int64_t EXPRINT64;

/** Pointer to a evaluator instance. */
typedef struct EXPR *PEXPR;


/**
 * Operand variable type.
 */
typedef enum
{
    /** Invalid zero entry. */
    kExprVar_Invalid = 0,
    /** A number. */
    kExprVar_Num,
    /** A string in need of expanding (perhaps). */
    kExprVar_String,
    /** A simple string that doesn't need expanding. */
    kExprVar_SimpleString,
    /** A quoted string in need of expanding (perhaps). */
    kExprVar_QuotedString,
    /** A simple quoted string that doesn't need expanding. */
    kExprVar_QuotedSimpleString,
    /** The end of the valid variable types. */
    kExprVar_End
} EXPRVARTYPE;

/**
 * Operand variable.
 */
typedef struct
{
    /** The variable type. */
    EXPRVARTYPE enmType;
    /** The variable. */
    union
    {
        /** Pointer to the string. */
        char *psz;
        /** The variable. */
        EXPRINT64 i;
    } uVal;
} EXPRVAR;
/** Pointer to a operand variable. */
typedef EXPRVAR *PEXPRVAR;
/** Pointer to a const operand variable. */
typedef EXPRVAR const *PCEXPRVAR;

/**
 * Operator return statuses.
 */
typedef enum
{
    kExprRet_Error = -1,
    kExprRet_Ok = 0,
    kExprRet_Operator,
    kExprRet_Operand,
    kExprRet_EndOfExpr,
    kExprRet_End
} EXPRRET;

/**
 * Operator.
 */
typedef struct
{
    /** The operator. */
    char szOp[11];
    /** The length of the operator string. */
    uint8_t cchOp;
    /** The pair operator.
     * This is used with '(' and '?'. */
    char chPair;
    /** The precedence. Higher means higher. */
    char iPrecedence;
    /** The number of arguments it takes. */
    signed char cArgs;
    /** Pointer to the method implementing the operator. */
    EXPRRET (*pfn)(PEXPR pThis);
} EXPROP;
/** Pointer to a const operator. */
typedef EXPROP const *PCEXPROP;


/** Magic value for RTEXPREVALINT::u32Magic.
 *  @todo fixme */
#define RTEXPREVAL_MAGIC        UINT32_C(0x12345678)

/**
 * Expression evaluator instance.
 */
typedef struct RTEXPREVALINT
{
    /** Magic number (RTEXPREVAL_MAGIC). */
    uint32_t                    u32Magic;
    /** Reference counter. */
    uint32_t volatile           cRefs;
    /** RTEXPREVAL_XXX.   */
    uint64_t                    fFlags;
    /** Name for logging purposes (copy)   */
    char                       *pszName;
    /** User argument to callbacks. */
    void                       *pvUser;
    /** Callback for getting variables or checking if they exists. */
    PFNRTEXPREVALQUERYVARIABLE  pfnQueryVariable;
} RTEXPREVALINT;

/**
 * An expression being evaluated.
 */
typedef struct EXPR
{
    /** The full expression. */
    const char *pszExpr;
    /** The current location. */
    const char *psz;
    /** Error info keeper. */
    PRTERRINFO pErrInfo;
    /** Pointer to the instance we evaluating under. */
    RTEXPREVALINT *pEvaluator;
    /** Pending binary operator. */
    PCEXPROP pPending;
    /** Top of the operator stack. */
    int iOp;
    /** Top of the operand stack. */
    int iVar;
    /** The operator stack. */
    PCEXPROP apOps[EXPR_MAX_OPERATORS];
    /** The operand stack. */
    EXPRVAR aVars[EXPR_MAX_OPERANDS];
} EXPR;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Operator start character map.
 * This indicates which characters that are starting operators and which aren't.
 *
 * Bit 0: Indicates that this char is used in operators.
 * Bit 1: When bit 0 is clear, this indicates whitespace.
 *        When bit 1 is set, this indicates whether the operator can be used
 *        immediately next to an operand without any clear separation.
 * Bits 2 thru 7: Index into g_aExprOps of the first operator starting with
 *        this character.
 */
static uint8_t g_abOpStartCharMap[256] = {0};
/** Whether we've initialized the map. */
static int g_fExprInitializedMap = 0;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void expr_unget_op(PEXPR pThis);
static EXPRRET expr_get_binary_or_eoe_or_rparen(PEXPR pThis);




/**
 * Displays an error message.
 *
 * The total string length must not exceed 256 bytes.
 *
 * @returns kExprRet_Error
 * @param   pThis       The evaluator instance.
 * @param   pszError    The message format string.
 * @param   ...         The message format args.
 */
static EXPRRET expr_error(PEXPR pThis, const char *pszError, ...)
{
    va_list va;
    va_start(va, pszError);
    RTErrInfoSetV(pThis->pErrInfo, VERR_PARSE_ERROR, pszError, va);
    va_end(va);
    return kExprRet_Error;
}


/**
 * Converts a number to a string.
 *
 * @returns pszDst.
 * @param   pszDst  The string buffer to write into. Assumes length of EXPR_NUM_LEN.
 * @param   iSrc    The number to convert.
 */
static char *expr_num_to_string(char *pszDst, EXPRINT64 iSrc)
{
    char  szTmp[64]; /* RTStrFormatNumber assumes this as a minimum size. */
    AssertCompile(EXPR_NUM_LEN < sizeof(szTmp));
    size_t cchTmp = RTStrFormatNumber(szTmp, iSrc, 10 /*uBase*/, 0 /*cchWidth*/, 0 /*cchPrecision*/,
                                      RTSTR_F_64BIT | RTSTR_F_VALSIGNED);
    return (char *)memcpy(pszDst, szTmp, cchTmp + 1);
}


/**
 * Attempts to convert a (simple) string into a number.
 *
 * @returns status code.
 * @param   pThis   The evaluator instance.
 * @param   piDst   Where to store the numeric value on success.
 * @param   pszSrc  The string to try convert.
 * @param   fQuiet  Whether we should be quiet or grumpy on failure.
 */
static EXPRRET expr_string_to_num(PEXPR pThis, EXPRINT64 *piDst, const char *pszSrc, int fQuiet)
{
    EXPRRET rc = kExprRet_Ok;
    char const *psz = pszSrc;
    EXPRINT64 i;
    unsigned uBase;
    int fNegative;

    /*
     * Skip blanks.
     */
    while (RT_C_IS_BLANK(*psz))
        psz++;
    const char *const pszFirst = psz;

    /*
     * Check for '-'.
     *
     * At this point we will not need to deal with operators, this is
     * just an indicator of negative numbers. If some operator ends up
     * here it's because it came from a string expansion and thus shall
     * not be interpreted. If this turns out to be an stupid restriction
     * it can be fixed, but for now it stays like this.
     */
    fNegative = *psz == '-';
    if (fNegative)
        psz++;

    /*
     * Determin base.
     * Recognize some exsotic prefixes here in addition to the two standard ones.
     */
    uint64_t const fFlags = pThis->pEvaluator->fFlags;
    uBase = fFlags & RTEXPREVAL_F_DEFAULT_BASE_16 ? 16 : 10;
    char const ch0 = psz[0];
    if (ch0 == '0')
    {
        char const ch1 = psz[1];
        switch (ch1)
        {
            case '\0':
                break;

            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': /* C-style octal */
                if (fFlags & RTEXPREVAL_F_C_OCTAL)
                {
                    uBase = 8;
                    psz++;
                }
                break;

            case '8':
            case '9':
                break;

            case 'x':
            case 'X':
                uBase = 16;
                psz += 2;
                break;
            case 'y': case 'Y': /* windbg, VBoxDbg */
            case 'b': case 'B': /* python and others */
                uBase = 2;
                psz += 2;
                break;
            case 'n': case 'N': /* windbg */
            case 'i': case 'I': /* VBoxDbg */
                uBase = 10;
                psz += 2;
                break;
            case 't': case 'T': /* windbg, VBoxDbg */
            case 'o': case 'O': /* python and others */
                uBase = 8;
                psz += 2;
                break;
        }
    }

    /*
     * Convert digits.
     */
    i = 0;
    for (;;)
    {
        unsigned iDigit;
        int ch = *psz;
        switch (ch)
        {
            case '0':   iDigit =  0; break;
            case '1':   iDigit =  1; break;
            case '2':   iDigit =  2; break;
            case '3':   iDigit =  3; break;
            case '4':   iDigit =  4; break;
            case '5':   iDigit =  5; break;
            case '6':   iDigit =  6; break;
            case '7':   iDigit =  7; break;
            case '8':   iDigit =  8; break;
            case '9':   iDigit =  9; break;
            case 'a':
            case 'A':   iDigit = 10; break;
            case 'b':
            case 'B':   iDigit = 11; break;
            case 'c':
            case 'C':   iDigit = 12; break;
            case 'd':
            case 'D':   iDigit = 13; break;
            case 'e':
            case 'E':   iDigit = 14; break;
            case 'F':   iDigit = 15; break;
            case 'f':
                /* Make 'false' -> 0: */
                if (   psz != pszFirst
                    || strncmp(psz + 1, RT_STR_TUPLE("alse")) != 0)
                {
                    iDigit = 15;
                    break;
                }
                psz += sizeof("false") - 1;
                RT_FALL_THROUGH();

            default:
                /* Make 'true' evaluate to 1: */
                if (psz == pszFirst && strncmp(psz, RT_STR_TUPLE("true")) == 0)
                {
                    psz += sizeof("true") - 1;
                    i = 1;
                }

                /*
                 * Is the rest white space?
                 */
                while (RT_C_IS_SPACE(*psz))
                    psz++;
                if (*psz != '\0')
                {
                    iDigit = uBase;
                    break;
                }
                RT_FALL_THROUGH();

            case '\0':
                if (fNegative)
                    i = -i;
                *piDst = i;
                return rc;
        }
        if (iDigit >= uBase)
        {
            if (fNegative)
                i = -i;
            *piDst = i;
            if (!fQuiet)
                expr_error(pThis, "Invalid %u-base number \"%.80s\"", uBase, pszSrc);
            return kExprRet_Error;
        }

        /* add the digit and advance */
        /** @todo check for overflow? */
        i *= uBase;
        i += iDigit;
        psz++;
    }
    /* not reached */
}


/**
 * Checks if the variable is a string or not.
 *
 * @returns 1 if it's a string, 0 otherwise.
 * @param   pVar    The variable.
 */
static int expr_var_is_string(PCEXPRVAR pVar)
{
    return pVar->enmType >= kExprVar_String;
}


/**
 * Checks if the variable contains a string that was quoted
 * in the expression.
 *
 * @returns 1 if if was a quoted string, otherwise 0.
 * @param   pVar    The variable.
 */
static int expr_var_was_quoted(PCEXPRVAR pVar)
{
    return pVar->enmType >= kExprVar_QuotedString;
}


/**
 * Deletes a variable.
 *
 * @param   pVar    The variable.
 */
static void expr_var_delete(PEXPRVAR pVar)
{
    if (expr_var_is_string(pVar))
    {
        RTMemTmpFree(pVar->uVal.psz);
        pVar->uVal.psz = NULL;
    }
    pVar->enmType = kExprVar_Invalid;
}


/**
 * Initializes a new variables with a sub-string value.
 *
 * @returns kExprRet_Ok or kExprRet_Error.
 * @param   pThis   The evaluator expression instance.
 * @param   pVar    The new variable.
 * @param   psz     The start of the string value.
 * @param   cch     The number of chars to copy.
 * @param   enmType The string type.
 */
static EXPRRET expr_var_init_substring(PEXPR pThis, PEXPRVAR pVar, const char *psz, size_t cch, EXPRVARTYPE enmType)
{
    /* convert string needing expanding into simple ones if possible.  */
    if (    enmType == kExprVar_String
        &&  !memchr(psz, '$', cch))
        enmType = kExprVar_SimpleString;
    else if (   enmType == kExprVar_QuotedString
             && !memchr(psz, '$', cch))
        enmType = kExprVar_QuotedSimpleString;

    pVar->enmType = enmType;
    pVar->uVal.psz = (char *)RTMemTmpAlloc(cch + 1);
    if (RT_LIKELY(pVar->uVal.psz))
    {
        memcpy(pVar->uVal.psz, psz, cch);
        pVar->uVal.psz[cch] = '\0';
        return kExprRet_Ok;
    }
    pVar->enmType = kExprVar_End;
    RTErrInfoSetF(pThis->pErrInfo, VERR_NO_TMP_MEMORY, "Failed to allocate %zu bytes", cch + 1);
    return kExprRet_Error;
}


/**
 * Initializes a new variables with a string value.
 *
 * @returns kExprRet_Ok or kExprRet_Error.
 * @param   pThis   The evaluator expression instance.
 * @param   pVar    The new variable.
 * @param   psz     The string value.
 * @param   enmType The string type.
 */
static EXPRRET expr_var_init_string(PEXPR pThis, PEXPRVAR pVar, const char *psz, EXPRVARTYPE enmType)
{
    return expr_var_init_substring(pThis, pVar, psz, strlen(psz), enmType);
}


#if 0  /* unused */
/**
 * Assigns a sub-string value to a variable.
 *
 * @returns kExprRet_Ok or kExprRet_Error.
 * @param   pThis   The evaluator expression instance.
 * @param   pVar    The new variable.
 * @param   psz     The start of the string value.
 * @param   cch     The number of chars to copy.
 * @param   enmType The string type.
 */
static EXPRRET expr_var_assign_substring(PEXPR pThis, PEXPRVAR pVar, const char *psz, size_t cch, EXPRVARTYPE enmType)
{
    expr_var_delete(pVar);
    return expr_var_init_substring(pThis, pVar, psz, cch, enmType);
}
#endif /* unused */


/**
 * Assignes a string value to a variable.
 *
 * @returns kExprRet_Ok or kExprRet_Error.
 * @param   pThis       The evaluator expression instance.
 * @param   pVar    The variable.
 * @param   psz     The string value.
 * @param   enmType The string type.
 */
static EXPRRET expr_var_assign_string(PEXPR pThis, PEXPRVAR pVar, const char *psz, EXPRVARTYPE enmType)
{
    expr_var_delete(pVar);
    return expr_var_init_string(pThis, pVar, psz, enmType);
}


/**
 * Finds the end of the current variable expansion, taking nested expansion
 * into account.
 *
 * This is somewhat similar to the code down in expr_get_unary_or_operand.
 *
 * @returns kExprRet_Ok or kExprRet_Error.
 * @param   pThis       The evaluator expression instance.
 * @param   pchSrc      Pointer to the dollar of the variable expansion.
 * @param   cchSrc      The length of the variable expansion expression.
 * @param   pcchVarRef  Where to return the length of the variable expansion.
 * @param   pfNested    Where to return whether it's a nested (@c true) or plain
 *                      one.
 */
static EXPRRET expr_expand_find_end(PEXPR pThis, const char *pchSrc, size_t cchSrc, size_t *pcchVarRef, bool *pfNested)
{
    const char * const  pchStart = pchSrc;

    /*
     * Push the initial expression.
     */
    Assert(cchSrc >= 2);
    Assert(pchSrc[0] == '$');
    Assert(pchSrc[1] == '{');
    unsigned            cPars = 1;
    pchSrc += 2;
    cchSrc -= 2;

    /*
     * Parse the rest of the string till we've back at cPars == 0.
     */
    *pfNested = false;
    while (cchSrc > 0)
    {
        char const ch = *pchSrc;
        if (   ch == '$'
            && cchSrc >= 2
            && pchSrc[1] == '{')
        {
            if (cPars < EXPR_MAX_VAR_RECURSION)
                cPars++;
            else
            {
                *pcchVarRef = 0;
                return expr_error(pThis, "Too deep nesting of variable expansions");
            }
            *pfNested = true;
            pchSrc += 2;
            cchSrc -= 2;
        }
        else
        {
            pchSrc += 1;
            cchSrc -= 1;
            if (ch == '}')
                if (--cPars == 0)
                {
                    *pcchVarRef = pchSrc - pchStart;
                    return kExprRet_Ok;
                }
        }
    }
    *pcchVarRef = 0;
    return expr_error(pThis, "Unbalanced variable expansions: %.*s", pchStart, pchSrc - pchStart);
}


/**
 * Returns the given string with all variables references replaced.
 *
 * @returns Pointer to expanded string on success (RTMemTmpFree), NULL on
 *          failure (error already set).
 * @param   pThis       The evaluator expression instance.
 * @param   pchSrc      The string to expand.
 * @param   cchSrc      The length of the string to expand.
 * @param   cDepth      The recursion depth, starting at zero.
 */
static char *expr_expand_string(PEXPR pThis, const char *pchSrc, size_t cchSrc, unsigned cDepth)
{
    if (cDepth < EXPR_MAX_VAR_RECURSION)
    {
        size_t cbRetAlloc = RT_ALIGN_Z(cchSrc + 1 + 16, 16);
        char  *pszRet     = (char *)RTMemTmpAlloc(cbRetAlloc);
        if (pszRet)
        {
            size_t offRet = 0;
            while (cchSrc > 0)
            {
                /*
                 * Look for the next potential variable reference.
                 */
                const char *pchDollar = (const char *)memchr(pchSrc, '$', cchSrc);
                size_t      cchPlain  = pchDollar ? pchDollar - pchSrc : cchSrc;
                size_t      cchNext   = cchPlain;

                if (pchDollar)
                {
                    /* Treat lone $ w/o a following { as plain text. */
                    if (   cchPlain + 1 >= cchSrc
                        && pchDollar[0] == '$'
                        && (   cchPlain + 1 == cchSrc
                            || pchDollar[1] != '{') )
                    {
                        cchPlain  += 1;
                        cchNext   += 1;
                        pchDollar += 1;
                    }
                    /* Eat up escaped dollars: $$ -> $ */
                    else
                        while (cchNext + 2 <= cchSrc && pchDollar[1] == '$' && pchDollar[0] == '$')
                        {
                            cchPlain  += 1;
                            cchNext   += 2;
                            pchDollar += 2;
                        }
                }

                /* Finally copy out plain text.*/
                if (cchPlain > 0)
                {
                    if (cchPlain >= cbRetAlloc - offRet)
                    {
                        size_t const cbNeeded = RT_ALIGN_Z(offRet + cchPlain + (!pchDollar ? 1 : offRet <= 64 ? 16 : 64), 16);
                        void        *pvNew    = RTMemTmpAlloc(cbNeeded);
                        if (pvNew)
                            memcpy(pvNew, pszRet, offRet);
                        RTMemTmpFree(pszRet);
                        pszRet = (char *)pvNew;
                        if (pvNew)
                            cbRetAlloc = cbNeeded;
                        else
                        {
                            RTErrInfoSetF(pThis->pErrInfo, VERR_NO_TMP_MEMORY, "Failed to allocate %zu bytes", cbNeeded);
                            return NULL;
                        }
                    }

                    memcpy(&pszRet[offRet], pchSrc, cchPlain);
                    offRet += cchPlain;
                    pszRet[offRet] = '\0';
                    pchSrc += cchNext;
                    cchSrc -= cchNext;
                    if (!cchSrc)
                        break;

                    /* If we don't have ${, just loop. */
                    if (   cchSrc < 2
                        || pchSrc[0] != '$'
                        || pchSrc[1] != '{')
                        continue;
                }

                /*
                 * If we get down here we have a ${ or $( at pchSrc.  The fun part now is
                 * finding the end of it and recursively dealing with any sub-expansions first.
                 */
                Assert(pchSrc[0] == '$' && pchSrc[1] == '{');
                size_t cchVarRef;
                bool   fNested;
                if (expr_expand_find_end(pThis, pchSrc, cchSrc, &cchVarRef, &fNested) == kExprRet_Ok)
                {
                    /* Lookup the variable.  Simple when it's a plain one, for nested ones we
                       first have to expand the variable name itself before looking it up. */
                    char *pszValue;
                    int   vrc;
                    if (!fNested)
                        vrc = pThis->pEvaluator->pfnQueryVariable(&pchSrc[2], cchSrc - 3, pThis->pEvaluator->pvUser, &pszValue);
                    else
                    {
                        char *pszName = expr_expand_string(pThis, &pchSrc[2], cchSrc - 3, cDepth + 1);
                        if (!pszName)
                        {
                            RTMemTmpFree(pszRet);
                            return NULL;
                        }
                        vrc = pThis->pEvaluator->pfnQueryVariable(pszName, strlen(pszName), pThis->pEvaluator->pvUser, &pszValue);
                        RTMemTmpFree(pszName);
                    }

                    /* Treat variables that aren't found as empty strings for now.
                       This may need to become configurable later. */
                    char *pszValueFree = pszValue;
                    static char s_szNotFound[] = "";
                    if (vrc == VERR_NOT_FOUND)
                    {
                        pszValue = s_szNotFound;
                        vrc = VINF_SUCCESS;
                    }

                    if (RT_SUCCESS(vrc))
                    {
                        /*
                         * Append the value to the return string.
                         */
                        size_t cchValue = strlen(pszValue);
                        if (cchValue > 0)
                        {
                            if (cchValue >= cbRetAlloc - offRet)
                            {
                                size_t const cbNeeded = RT_ALIGN_Z(offRet + cchValue + (!pchDollar ? 1 : offRet <= 64 ? 16 : 64),
                                                                   16);
                                void        *pvNew    = RTMemTmpAlloc(cbNeeded);
                                if (pvNew)
                                    memcpy(pvNew, pszRet, offRet);
                                RTMemTmpFree(pszRet);
                                pszRet = (char *)pvNew;
                                if (pvNew)
                                    cbRetAlloc = cbNeeded;
                                else
                                {
                                    RTErrInfoSetF(pThis->pErrInfo, VERR_NO_TMP_MEMORY, "Failed to allocate %zu bytes", cbNeeded);
                                    RTStrFree(pszValueFree);
                                    return NULL;
                                }
                            }

                            memcpy(&pszRet[offRet], pszValue, cchValue);
                            offRet += cchValue;
                            pszRet[offRet] = '\0';
                        }
                        pchSrc += cchVarRef;
                        cchSrc -= cchVarRef;
                        RTStrFree(pszValueFree);
                        continue;
                    }
                }
                RTMemTmpFree(pszRet);
                return NULL;
            }
            return pszRet;
        }
        RTErrInfoSetF(pThis->pErrInfo, VERR_NO_TMP_MEMORY, "Failed to allocate %zu bytes", cbRetAlloc);
    }
    else
        RTErrInfoSet(pThis->pErrInfo, VERR_TOO_MUCH_DATA, "Too deeply nested variable expression");
    return NULL;
}


/**
 * Simplifies a string variable.
 *
 * @returns kExprRet_Ok or kExprRet_Error.
 * @param   pThis   The evaluator expression instance.
 * @param   pVar    The variable.
 */
static EXPRRET expr_var_make_simple_string(PEXPR pThis, PEXPRVAR pVar)
{
    switch (pVar->enmType)
    {
        case kExprVar_Num:
        {
            char *psz = (char *)RTMemTmpAlloc(EXPR_NUM_LEN);
            if (psz)
            {
                expr_num_to_string(psz, pVar->uVal.i);
                pVar->uVal.psz = psz;
                pVar->enmType = kExprVar_SimpleString;
            }
            else
            {
                RTErrInfoSetF(pThis->pErrInfo, VERR_NO_TMP_MEMORY, "Failed to allocate %zu bytes", EXPR_NUM_LEN);
                return kExprRet_Error;
            }
            break;
        }

        case kExprVar_String:
        case kExprVar_QuotedString:
        {
            Assert(strchr(pVar->uVal.psz, '$'));
            char *psz = expr_expand_string(pThis, pVar->uVal.psz, strlen(pVar->uVal.psz), 0);
            if (psz)
            {
                RTMemTmpFree(pVar->uVal.psz);
                pVar->uVal.psz = psz;

                pVar->enmType  = pVar->enmType == kExprVar_String
                               ? kExprVar_SimpleString
                               : kExprVar_QuotedSimpleString;
            }
            else
                return kExprRet_Error;
            break;
        }

        case kExprVar_SimpleString:
        case kExprVar_QuotedSimpleString:
            /* nothing to do. */
            break;

        default:
            AssertMsgFailed(("%d\n", pVar->enmType));
    }
    return kExprRet_Ok;
}


#if 0 /* unused */
/**
 * Turns a variable into a string value.
 *
 * @param   pVar    The variable.
 */
static void expr_var_make_string(PEXPRVAR pVar)
{
    switch (pVar->enmType)
    {
        case kExprVar_Num:
            expr_var_make_simple_string(pVar);
            break;

        case kExprVar_String:
        case kExprVar_SimpleString:
        case kExprVar_QuotedString:
        case kExprVar_QuotedSimpleString:
            /* nothing to do. */
            break;

        default:
            AssertMsgFailed(("%d\n", pVar->enmType));
    }
}
#endif /* unused */


/**
 * Initializes a new variables with a integer value.
 *
 * @param   pVar    The new variable.
 * @param   i       The integer value.
 */
static void expr_var_init_num(PEXPRVAR pVar, EXPRINT64 i)
{
    pVar->enmType = kExprVar_Num;
    pVar->uVal.i = i;
}


/**
 * Assigns a integer value to a variable.
 *
 * @param   pVar    The variable.
 * @param   i       The integer value.
 */
static void expr_var_assign_num(PEXPRVAR pVar, EXPRINT64 i)
{
    expr_var_delete(pVar);
    expr_var_init_num(pVar, i);
}


/**
 * Turns the variable into a number.
 *
 * @returns status code.
 * @param   pThis   The evaluator instance.
 * @param   pVar    The variable.
 */
static EXPRRET expr_var_make_num(PEXPR pThis, PEXPRVAR pVar)
{
    switch (pVar->enmType)
    {
        case kExprVar_Num:
            /* nothing to do. */
            break;

        case kExprVar_String:
        {
            EXPRRET rc = expr_var_make_simple_string(pThis, pVar);
            if (rc != kExprRet_Ok)
                return rc;
            RT_FALL_THROUGH();
        }
        case kExprVar_SimpleString:
        {
            EXPRINT64 i;
            EXPRRET rc = expr_string_to_num(pThis, &i, pVar->uVal.psz, 0 /* fQuiet */);
            if (rc < kExprRet_Ok)
                return rc;
            expr_var_assign_num(pVar, i);
            break;
        }

        case kExprVar_QuotedString:
        case kExprVar_QuotedSimpleString:
            return expr_error(pThis, "Cannot convert a quoted string to a number");

        default:
            AssertMsgFailedReturn(("%d\n", pVar->enmType), kExprRet_Error);
    }

    return kExprRet_Ok;
}


/**
 * Try to turn the variable into a number.
 *
 * @returns status code.
 * @param   pThis   The instance.
 * @param   pVar    The variable.
 */
static EXPRRET expr_var_try_make_num(PEXPR pThis, PEXPRVAR pVar)
{
    EXPRRET rc;
    switch (pVar->enmType)
    {
        case kExprVar_Num:
            /* nothing to do. */
            break;

        case kExprVar_String:
            rc = expr_var_make_simple_string(pThis, pVar);
            if (rc != kExprRet_Ok)
                return rc;
            RT_FALL_THROUGH();
        case kExprVar_SimpleString:
        {
            EXPRINT64 i;
            rc = expr_string_to_num(pThis, &i, pVar->uVal.psz, 1 /* fQuiet */);
            if (rc < kExprRet_Ok)
                return rc;
            expr_var_assign_num(pVar, i);
            break;
        }

        case kExprVar_QuotedString:
        case kExprVar_QuotedSimpleString:
            /* can't do this */
            return kExprRet_Error;

        default:
            AssertMsgFailedReturn(("%d\n", pVar->enmType), kExprRet_Error);
    }

    return kExprRet_Ok;
}


/**
 * Initializes a new variables with a boolean value.
 *
 * @param   pVar    The new variable.
 * @param   f       The boolean value.
 */
static void expr_var_init_bool(PEXPRVAR pVar, int f)
{
    pVar->enmType = kExprVar_Num;
    pVar->uVal.i = !!f;
}


/**
 * Assigns a boolean value to a variable.
 *
 * @param   pVar    The variable.
 * @param   f       The boolean value.
 */
static void expr_var_assign_bool(PEXPRVAR pVar, int f)
{
    expr_var_delete(pVar);
    expr_var_init_bool(pVar, f);
}


/**
 * Turns the variable into an boolean.
 *
 * @returns the boolean interpretation.
 * @param   pThis       The instance.
 * @param   pVar        The variable.
 */
static EXPRRET expr_var_make_bool(PEXPR pThis, PEXPRVAR pVar)
{
    EXPRRET rc = kExprRet_Ok;

    switch (pVar->enmType)
    {
        case kExprVar_Num:
            pVar->uVal.i = !!pVar->uVal.i;
            break;

        case kExprVar_String:
            rc = expr_var_make_simple_string(pThis, pVar);
            if (rc != kExprRet_Ok)
                break;
            RT_FALL_THROUGH();
        case kExprVar_SimpleString:
        {
            /*
             * Try convert it to a number. If that fails, check for 'true' or
             * 'false', if neither then use python / GNU make logic wrt strings.
             */
            EXPRINT64 iVal;
            char const *psz = pVar->uVal.psz;
            while (RT_C_IS_BLANK(*psz))
                psz++;
            if (    *psz
                &&  expr_string_to_num(pThis, &iVal, psz, 1 /* fQuiet */) >= kExprRet_Ok)
                expr_var_assign_bool(pVar, iVal != 0);
            else if (   strncmp(psz, RT_STR_TUPLE("true")) == 0
                     && *RTStrStripL(&psz[sizeof("true") - 1]) == '\0')
                expr_var_assign_bool(pVar, true);
            else if (   strncmp(psz, RT_STR_TUPLE("false")) == 0
                     && *RTStrStripL(&psz[sizeof("false") - 1]) == '\0')
                expr_var_assign_bool(pVar, false);
            else
                expr_var_assign_bool(pVar, *psz != '\0');
            break;
        }

        case kExprVar_QuotedString:
            rc = expr_var_make_simple_string(pThis, pVar);
            if (rc != kExprRet_Ok)
                break;
            RT_FALL_THROUGH();
        case kExprVar_QuotedSimpleString:
            /*
             * Use python / GNU make boolean logic: non-empty string means true.
             * No stripping here, as the string is quoted as should be taken exactly as given.
             */
            expr_var_assign_bool(pVar, *pVar->uVal.psz != '\0');
            break;

        default:
            AssertMsgFailed(("%d\n", pVar->enmType));
    }

    return rc;
}


/**
 * Pops a varable off the stack and deletes it.
 * @param   pThis   The evaluator instance.
 */
static void expr_pop_and_delete_var(PEXPR pThis)
{
    expr_var_delete(&pThis->aVars[pThis->iVar]);
    pThis->iVar--;
}



/**
 * Tries to make the variables the same type.
 *
 * This will not convert numbers to strings, unless one of them
 * is a quoted string.
 *
 * this will try convert both to numbers if neither is quoted. Both
 * conversions will have to suceed for this to be commited.
 *
 * All strings will be simplified.
 *
 * @returns status code. Done complaining on failure.
 *
 * @param   pThis   The evaluator instance.
 * @param   pVar1   The first variable.
 * @param   pVar2   The second variable.
 * @param   pszOp   The operator requesting this (for errors).
 */
static EXPRRET expr_var_unify_types(PEXPR pThis, PEXPRVAR pVar1, PEXPRVAR pVar2, const char *pszOp)
{
/** @todo Add flag for selecting preference here when forcing types */


    /*
     * Try make the variables the same type before comparing.
     */
    if (    !expr_var_was_quoted(pVar1)
        &&  !expr_var_was_quoted(pVar2))
    {
        if (    expr_var_is_string(pVar1)
            ||  expr_var_is_string(pVar2))
        {
            if (!expr_var_is_string(pVar1))
                expr_var_try_make_num(pThis, pVar2);
            else if (!expr_var_is_string(pVar2))
                expr_var_try_make_num(pThis, pVar1);
            else
            {
                /*
                 * Both are strings, simplify them then see if both can be made into numbers.
                 */
                EXPRRET rc = expr_var_make_simple_string(pThis, pVar1);
                if (rc == kExprRet_Ok)
                    rc = expr_var_make_simple_string(pThis, pVar2);
                if (rc == kExprRet_Ok)
                {
                    EXPRINT64 iVar1;
                    EXPRINT64 iVar2;
                    if (    expr_string_to_num(pThis, &iVar1, pVar1->uVal.psz, 1 /* fQuiet */) >= kExprRet_Ok
                        &&  expr_string_to_num(pThis, &iVar2, pVar2->uVal.psz, 1 /* fQuiet */) >= kExprRet_Ok)
                    {
                        expr_var_assign_num(pVar1, iVar1);
                        expr_var_assign_num(pVar2, iVar2);
                    }
                }
                else
                    return rc;
            }
        }
    }
    else
    {
        EXPRRET rc = expr_var_make_simple_string(pThis, pVar1);
        if (rc == kExprRet_Ok)
            rc = expr_var_make_simple_string(pThis, pVar2);
        if (rc == kExprRet_Ok)
        { /* likely */ }
        else
            return rc;
    }

    /*
     * Complain if they aren't the same type now.
     */
    if (expr_var_is_string(pVar1) != expr_var_is_string(pVar2))
        return expr_error(pThis, "Unable to unify types for \"%s\"", pszOp);
    return kExprRet_Ok;
}



/*********************************************************************************************************************************
*   Operators                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Is variable defined, unary.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_defined(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];

    EXPRRET rc = expr_var_make_simple_string(pThis, pVar);
    if (rc == kExprRet_Ok)
    {
        int vrc = pThis->pEvaluator->pfnQueryVariable(pVar->uVal.psz, strlen(pVar->uVal.psz), pThis->pEvaluator->pvUser, NULL);
        expr_var_assign_bool(pVar, vrc != VERR_NOT_FOUND);
    }

    return rc;
}


/**
 * Does file(/dir/whatever) exist, unary.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_exists(PEXPR pThis)
{
    EXPRRET  rc;
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];

    if (pThis->pEvaluator->fFlags & RTEXPREVAL_F_EXISTS_OP)
    {
        rc = expr_var_make_simple_string(pThis, pVar);
        if (rc == kExprRet_Ok)
            expr_var_assign_bool(pVar, RTPathExists(pVar->uVal.psz) == 0);
    }
    else
        rc = expr_error(pThis, "The 'exists' operator is not accessible");

    return rc;
}


/**
 * Convert to boolean.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bool(PEXPR pThis)
{
    return expr_var_make_bool(pThis, &pThis->aVars[pThis->iVar]);
}


/**
 * Convert to number, works on quoted strings too.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_num(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];

    /* unquote the string */
    if (pVar->enmType == kExprVar_QuotedSimpleString)
        pVar->enmType = kExprVar_SimpleString;
    else if (pVar->enmType == kExprVar_QuotedString)
        pVar->enmType = kExprVar_String;

    return expr_var_make_num(pThis, pVar);
}


/**
 * Performs a strlen() on the simplified/converted string argument.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_strlen(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];
    EXPRRET  rc   = expr_var_make_simple_string(pThis, pVar);
    if (rc == kExprRet_Ok)
        expr_var_assign_num(pVar, strlen(pVar->uVal.psz));

    return rc;
}


/**
 * Convert to string (simplified and quoted)
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_str(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];
    EXPRRET  rc   = expr_var_make_simple_string(pThis, pVar);
    if (rc == kExprRet_Ok)
        pVar->enmType = kExprVar_QuotedSimpleString;

    return rc;
}


/**
 * Returns the MD5 digest of the input as a string.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_md5(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];
    EXPRRET  rc   = expr_var_make_simple_string(pThis, pVar);
    if (rc == kExprRet_Ok)
    {
        pVar->enmType = kExprVar_QuotedSimpleString;

        uint8_t abDigest[RTMD5_HASH_SIZE];
        RTMd5(pVar->uVal.psz, strlen(pVar->uVal.psz), abDigest);
        char szHash[RTMD5_DIGEST_LEN + 1];
        RTMd5ToString(abDigest, szHash, sizeof(szHash));

        rc = expr_var_assign_string(pThis, pVar, szHash, kExprVar_QuotedSimpleString);
    }

    return rc;
}


/**
 * Returns the SHA1 digest of the input as a string.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_sha1(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];
    EXPRRET  rc   = expr_var_make_simple_string(pThis, pVar);
    if (rc == kExprRet_Ok)
    {
        pVar->enmType = kExprVar_QuotedSimpleString;

        uint8_t abDigest[RTSHA1_HASH_SIZE];
        RTSha1(pVar->uVal.psz, strlen(pVar->uVal.psz), abDigest);
        char szHash[RTSHA1_DIGEST_LEN + 1];
        RTSha1ToString(abDigest, szHash, sizeof(szHash));

        rc = expr_var_assign_string(pThis, pVar, szHash, kExprVar_QuotedSimpleString);
    }

    return rc;
}


/**
 * Returns the SHA256 digest of the input as a string.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_sha256(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];
    EXPRRET  rc   = expr_var_make_simple_string(pThis, pVar);
    if (rc == kExprRet_Ok)
    {
        pVar->enmType = kExprVar_QuotedSimpleString;

        uint8_t abDigest[RTSHA256_HASH_SIZE];
        RTSha256(pVar->uVal.psz, strlen(pVar->uVal.psz), abDigest);
        char szHash[RTSHA256_DIGEST_LEN + 1];
        RTSha256ToString(abDigest, szHash, sizeof(szHash));

        rc = expr_var_assign_string(pThis, pVar, szHash, kExprVar_QuotedSimpleString);
    }

    return rc;
}


/**
 * Returns the SHA-512 digest of the input as a string.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_sha512(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];
    EXPRRET  rc   = expr_var_make_simple_string(pThis, pVar);
    if (rc == kExprRet_Ok)
    {
        pVar->enmType = kExprVar_QuotedSimpleString;

        uint8_t abDigest[RTSHA512_HASH_SIZE];
        RTSha512(pVar->uVal.psz, strlen(pVar->uVal.psz), abDigest);
        char szHash[RTSHA512_DIGEST_LEN + 1];
        RTSha512ToString(abDigest, szHash, sizeof(szHash));

        rc = expr_var_assign_string(pThis, pVar, szHash, kExprVar_QuotedSimpleString);
    }

    return rc;
}


/**
 * Pluss (dummy / make_integer)
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_pluss(PEXPR pThis)
{
    return expr_var_make_num(pThis, &pThis->aVars[pThis->iVar]);
}


/**
 * Minus (negate)
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_minus(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];
    EXPRRET  rc   = expr_var_make_num(pThis, pVar);
    if (rc >= kExprRet_Ok)
        pVar->uVal.i = -pVar->uVal.i;

    return rc;
}



/**
 * Bitwise NOT.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bitwise_not(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];
    EXPRRET  rc   = expr_var_make_num(pThis, pVar);
    if (rc >= kExprRet_Ok)
        pVar->uVal.i = ~pVar->uVal.i;

    return rc;
}


/**
 * Logical NOT.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_logical_not(PEXPR pThis)
{
    PEXPRVAR pVar = &pThis->aVars[pThis->iVar];
    EXPRRET  rc   = expr_var_make_bool(pThis, pVar);
    if (rc == kExprRet_Ok)
        pVar->uVal.i = !pVar->uVal.i;

    return rc;
}


/**
 * Multiplication.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_multiply(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    EXPRRET  rc    = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i *= pVar2->uVal.i;
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}



/**
 * Division.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_divide(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    EXPRRET  rc    = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i /= pVar2->uVal.i;
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}



/**
 * Modulus.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_modulus(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    EXPRRET  rc    = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i %= pVar2->uVal.i;
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


#if 0 /** @todo not happy with 'strcat' as an operator. Dot doesn't work, so, figure something else out... */
/**
 * Concatnation (string).
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_concat(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET  rc    = kExprRet_Ok;
    if (!expr_var_is_string(pVar1))
        rc = expr_var_make_simple_string(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar2))
            rc = expr_var_make_simple_string(pThis, pVar2);
        if (rc >= kExprRet_Ok)
        {
            /** @todo this may be problematic if we combine simple with quoted strings
             *        if the simple string contains $. */
            if (rc >= kExprRet_Ok)
            {
                size_t cchVar1 = strlen(pVar1->uVal.psz);
                size_t cchVar2 = strlen(pVar2->uVal.psz);
                size_t cchNew  = cchVar1 + cchVar2;
                char  *pszNew  = (char *)RTMemTmpAlloc(cchNew + 1);
                if (pszNew)
                {
                    memcpy(pszNew, pVar1->uVal.psz, cchVar1);
                    memcpy(&pszNew[cchVar1], pVar2->uVal.psz, cchVar2);
                    pszNew[cchNew] = '\0';
                    RTMemTmpFree(pVar1->uVal.psz);
                    pVar1->uVal.psz = pszNew;
                }
                else
                {
                    RTErrInfoSetF(pThis->pErrInfo, VERR_NO_TMP_MEMORY, "Failed to allocate %zu bytes", cchNew + 1);
                    rc = kExprRet_Error;
                }
            }
        }
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}
#endif


/**
 * Addition (numeric).
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_add(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    EXPRRET  rc    = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i += pVar2->uVal.i;
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Subtract (numeric).
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_sub(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    EXPRRET  rc    = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i -= pVar2->uVal.i;
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Bitwise left shift.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_shift_left(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    EXPRRET  rc    = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i <<= pVar2->uVal.i;
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Bitwise right shift.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_shift_right(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    EXPRRET  rc    = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i >>= pVar2->uVal.i;
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Less than or equal, version string.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_ver_less_or_equal_than(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET  rc    = expr_var_unify_types(pThis, pVar1, pVar2, "vle");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i <= pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, RTStrVersionCompare(pVar1->uVal.psz, pVar2->uVal.psz) <= 0);
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Less than or equal.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_less_or_equal_than(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET  rc    = expr_var_unify_types(pThis, pVar1, pVar2, "<=");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i <= pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) <= 0);
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Less than, version string.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_ver_less_than(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET  rc    = expr_var_unify_types(pThis, pVar1, pVar2, "vlt");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i < pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, RTStrVersionCompare(pVar1->uVal.psz, pVar2->uVal.psz) < 0);
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Less than.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_less_than(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET  rc    = expr_var_unify_types(pThis, pVar1, pVar2, "<");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i < pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) < 0);
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Greater or equal than, version string.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_ver_greater_or_equal_than(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET  rc    = expr_var_unify_types(pThis, pVar1, pVar2, "vge");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i >= pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, RTStrVersionCompare(pVar1->uVal.psz, pVar2->uVal.psz) >= 0);
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Greater or equal than.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_greater_or_equal_than(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET  rc    = expr_var_unify_types(pThis, pVar1, pVar2, ">=");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i >= pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) >= 0);
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Greater than, version string.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_ver_greater_than(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET  rc    = expr_var_unify_types(pThis, pVar1, pVar2, "vgt");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i > pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, RTStrVersionCompare(pVar1->uVal.psz, pVar2->uVal.psz) > 0);
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Greater than.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_greater_than(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
    EXPRRET  rc    = expr_var_unify_types(pThis, pVar1, pVar2, ">");
    if (rc >= kExprRet_Ok)
    {
        if (!expr_var_is_string(pVar1))
            expr_var_assign_bool(pVar1, pVar1->uVal.i > pVar2->uVal.i);
        else
            expr_var_assign_bool(pVar1, strcmp(pVar1->uVal.psz, pVar2->uVal.psz) > 0);
    }
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Equal, version strings.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_ver_equal(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];
    int const   fIsString1 = expr_var_is_string(pVar1);

    /*
     * The same type?
     */
    if (fIsString1 == expr_var_is_string(pVar2))
    {
        if (!fIsString1)
            /* numbers are simple */
            expr_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
        else
        {
            /* try a normal string compare. */
            rc = expr_var_make_simple_string(pThis, pVar1);
            if (rc == kExprRet_Ok)
                rc = expr_var_make_simple_string(pThis, pVar2);
            if (rc == kExprRet_Ok)
            {
                if (!RTStrVersionCompare(pVar1->uVal.psz, pVar2->uVal.psz))
                    expr_var_assign_bool(pVar1, 1);
                /* try convert and compare as number instead. */
                else if (   expr_var_try_make_num(pThis, pVar1) >= kExprRet_Ok
                         && expr_var_try_make_num(pThis, pVar2) >= kExprRet_Ok)
                    expr_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
                /* ok, they really aren't equal. */
                else
                    expr_var_assign_bool(pVar1, 0);
            }
        }
    }
    else
    {
        /*
         * If the type differs, there are now two options:
         *  1. Try convert the string to a valid number and compare the numbers.
         *  2. Convert the non-string to a number and compare the strings.
         */
        if (   expr_var_try_make_num(pThis, pVar1) >= kExprRet_Ok
            && expr_var_try_make_num(pThis, pVar2) >= kExprRet_Ok)
            expr_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
        else
        {
            rc = expr_var_make_simple_string(pThis, pVar1);
            if (rc == kExprRet_Ok)
                rc = expr_var_make_simple_string(pThis, pVar2);
            if (rc == kExprRet_Ok)
                expr_var_assign_bool(pVar1, RTStrVersionCompare(pVar1->uVal.psz, pVar2->uVal.psz) == 0);
        }
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Not equal, version string.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_ver_not_equal(PEXPR pThis)
{
    EXPRRET rc = expr_op_ver_equal(pThis);
    if (rc >= kExprRet_Ok)
        rc = expr_op_logical_not(pThis);
    return rc;
}


/**
 * Equal.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_equal(PEXPR pThis)
{
    EXPRRET     rc = kExprRet_Ok;
    PEXPRVAR    pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR    pVar2 = &pThis->aVars[pThis->iVar];
    int const   fIsString1 = expr_var_is_string(pVar1);

    /*
     * The same type?
     */
    if (fIsString1 == expr_var_is_string(pVar2))
    {
        if (!fIsString1)
            /* numbers are simple */
            expr_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
        else
        {
            /* try a normal string compare. */
            rc = expr_var_make_simple_string(pThis, pVar1);
            if (rc == kExprRet_Ok)
                rc = expr_var_make_simple_string(pThis, pVar2);
            if (rc == kExprRet_Ok)
            {
                if (!strcmp(pVar1->uVal.psz, pVar2->uVal.psz))
                    expr_var_assign_bool(pVar1, 1);
                /* try convert and compare as number instead. */
                else if (   expr_var_try_make_num(pThis, pVar1) >= kExprRet_Ok
                         && expr_var_try_make_num(pThis, pVar2) >= kExprRet_Ok)
                    expr_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
                /* ok, they really aren't equal. */
                else
                    expr_var_assign_bool(pVar1, 0);
            }
        }
    }
    else
    {
        /*
         * If the type differs, there are now two options:
         *  1. Convert the string to a valid number and compare the numbers.
         *  2. Convert an empty string to a 'false' boolean value and compare
         *     numerically. This one is a bit questionable, so we don't try this.
         */
        /** @todo this needs to be redone, both because we're hiding alloc errors
         *        here but also because this should be controlled by a flag. */
        if (   expr_var_try_make_num(pThis, pVar1) >= kExprRet_Ok
            && expr_var_try_make_num(pThis, pVar2) >= kExprRet_Ok)
            expr_var_assign_bool(pVar1, pVar1->uVal.i == pVar2->uVal.i);
        else
            rc = expr_error(pThis, "Cannot compare strings and numbers");
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Not equal.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_not_equal(PEXPR pThis)
{
    EXPRRET rc = expr_op_equal(pThis);
    if (rc >= kExprRet_Ok)
        rc = expr_op_logical_not(pThis);
    return rc;
}


/**
 * Bitwise AND.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bitwise_and(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];

    EXPRRET rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i &= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Bitwise XOR.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bitwise_xor(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];

    EXPRRET rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i ^= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Bitwise OR.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_bitwise_or(PEXPR pThis)
{
    PEXPRVAR pVar1 = &pThis->aVars[pThis->iVar - 1];
    PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];

    EXPRRET rc = expr_var_make_num(pThis, pVar1);
    if (rc >= kExprRet_Ok)
    {
        rc = expr_var_make_num(pThis, pVar2);
        if (rc >= kExprRet_Ok)
            pVar1->uVal.i |= pVar2->uVal.i;
    }

    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Logical AND.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_logical_and(PEXPR pThis)
{
    bool     fResult = false;
    PEXPRVAR pVar1   = &pThis->aVars[pThis->iVar - 1];
    EXPRRET  rc = expr_var_make_bool(pThis, pVar1);
    if (   rc == kExprRet_Ok
        && pVar1->uVal.i != 0)
    {
        PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
        rc = expr_var_make_bool(pThis, pVar2);
        if (rc == kExprRet_Ok && pVar2->uVal.i != 0)
            fResult = true;
    }
    expr_var_assign_bool(pVar1, fResult);
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Logical OR.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_logical_or(PEXPR pThis)
{
    bool     fResult = false;
    PEXPRVAR pVar1   = &pThis->aVars[pThis->iVar - 1];
    EXPRRET  rc = expr_var_make_bool(pThis, pVar1);
    if (rc == kExprRet_Ok)
    {
        if (pVar1->uVal.i)
            fResult = true;
        else
        {
            PEXPRVAR pVar2 = &pThis->aVars[pThis->iVar];
            rc = expr_var_make_bool(pThis, pVar2);
            if (rc == kExprRet_Ok && pVar2->uVal.i != 0)
                fResult = true;
        }
    }
    expr_var_assign_bool(pVar1, fResult);
    expr_pop_and_delete_var(pThis);
    return rc;
}


/**
 * Left parenthesis.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_left_parenthesis(PEXPR pThis)
{
    /*
     * There should be a right parenthesis operator lined up for us now,
     * eat it. If not found there is an inbalance.
     */
    EXPRRET rc = expr_get_binary_or_eoe_or_rparen(pThis);
    if (    rc == kExprRet_Operator
        &&  pThis->apOps[pThis->iOp]->szOp[0] == ')')
    {
        /* pop it and get another one which we can leave pending. */
        pThis->iOp--;
        rc = expr_get_binary_or_eoe_or_rparen(pThis);
        if (rc >= kExprRet_Ok)
            expr_unget_op(pThis);
    }
    else
        rc = expr_error(pThis, "Missing ')'");

    return rc;
}


/**
 * Right parenthesis, dummy that's never actually called.
 *
 * @returns Status code.
 * @param   pThis       The instance.
 */
static EXPRRET expr_op_right_parenthesis(PEXPR pThis)
{
    RT_NOREF_PV(pThis);
    AssertFailed();
    return kExprRet_Ok;
}





/**
 * The operator table.
 *
 * This table is NOT ordered by precedence, but for linear search
 * allowing for first match to return the correct operator. This
 * means that || must come before |, or else | will match all.
 */
static const EXPROP g_aExprOps[] =
{
#define EXPR_OP(szOp, iPrecedence, cArgs, pfn)  {  szOp, sizeof(szOp) - 1, '\0', iPrecedence, cArgs, pfn }
    /*      Name, iPrecedence,  cArgs,    pfn    */
    EXPR_OP("defined",     90,      1,    expr_op_defined),
    EXPR_OP("exists",      90,      1,    expr_op_exists),
    EXPR_OP("bool",        90,      1,    expr_op_bool),
    EXPR_OP("md5",         90,      1,    expr_op_md5),
    EXPR_OP("num",         90,      1,    expr_op_num),
    EXPR_OP("sha1",        90,      1,    expr_op_sha1),
    EXPR_OP("sha256",      90,      1,    expr_op_sha256),
    EXPR_OP("sha512",      90,      1,    expr_op_sha512),
    EXPR_OP("strlen",      90,      1,    expr_op_strlen),
    EXPR_OP("str",         90,      1,    expr_op_str),
    EXPR_OP("+",           80,      1,    expr_op_pluss),
    EXPR_OP("-",           80,      1,    expr_op_minus),
    EXPR_OP("~",           80,      1,    expr_op_bitwise_not),
    EXPR_OP("*",           75,      2,    expr_op_multiply),
    EXPR_OP("/",           75,      2,    expr_op_divide),
    EXPR_OP("%",           75,      2,    expr_op_modulus),
    EXPR_OP("+",           70,      2,    expr_op_add),
    EXPR_OP("-",           70,      2,    expr_op_sub),
#if 0  /** @todo not happy with 'strcat' as an operator. Dot doesn't work, so, figure something else out... */
    EXPR_OP("strcat",      70,      2,    expr_op_concat),
#endif
    EXPR_OP("<<",          65,      2,    expr_op_shift_left),
    EXPR_OP(">>",          65,      2,    expr_op_shift_right),
    EXPR_OP("<=",          60,      2,    expr_op_less_or_equal_than),
    EXPR_OP("<",           60,      2,    expr_op_less_than),
    EXPR_OP(">=",          60,      2,    expr_op_greater_or_equal_than),
    EXPR_OP(">",           60,      2,    expr_op_greater_than),
    EXPR_OP("vle",         60,      2,    expr_op_ver_less_or_equal_than),
    EXPR_OP("vlt",         60,      2,    expr_op_ver_less_than),
    EXPR_OP("vge",         60,      2,    expr_op_ver_greater_or_equal_than),
    EXPR_OP("vgt",         60,      2,    expr_op_ver_greater_than),
    EXPR_OP("==",          55,      2,    expr_op_equal),
    EXPR_OP("veq",         55,      2,    expr_op_ver_equal),
    EXPR_OP("!=",          55,      2,    expr_op_not_equal),
    EXPR_OP("vne",         55,      2,    expr_op_ver_not_equal),
    EXPR_OP("!",           80,      1,    expr_op_logical_not),
    EXPR_OP("^",           45,      2,    expr_op_bitwise_xor),
    EXPR_OP("&&",          35,      2,    expr_op_logical_and),
    EXPR_OP("&",           50,      2,    expr_op_bitwise_and),
    EXPR_OP("||",          30,      2,    expr_op_logical_or),
    EXPR_OP("|",           40,      2,    expr_op_bitwise_or),
          { "(", 1, ')',   10,      1,    expr_op_left_parenthesis },
          { ")", 1, '(',   10,      0,    expr_op_right_parenthesis },
 /*       { "?", 1, ':',    5,      2,    expr_op_question },
          { ":", 1, '?',    5,      2,    expr_op_colon }, -- too weird for now. */
#undef EXPR_OP
};

/** Dummy end of expression fake. */
static const EXPROP g_ExprEndOfExpOp =
{
            "", 0, '\0',    0,      0,    NULL
};


/**
 * Initializes the opcode character map if necessary.
 */
static void expr_map_init(void)
{
    unsigned i;
    if (g_fExprInitializedMap)
        return;

    /*
     * Initialize it.
     */
    for (i = 0; i < sizeof(g_aExprOps) / sizeof(g_aExprOps[0]); i++)
    {
        unsigned int ch = (unsigned int)g_aExprOps[i].szOp[0];
        if (!g_abOpStartCharMap[ch])
        {
            g_abOpStartCharMap[ch] = (i << 2) | 1;
            if (!RT_C_IS_ALPHA(ch))
                g_abOpStartCharMap[ch] |= 2; /* Need no clear separation from operands. */
        }
    }

    /* whitespace (assumes C-like locale because I'm lazy): */
#define SET_WHITESPACE(a_ch) do {  \
        Assert(g_abOpStartCharMap[(unsigned char)(a_ch)] == 0); \
        g_abOpStartCharMap[(unsigned char)(a_ch)] |= 2; \
    } while (0)
    SET_WHITESPACE(' ');
    SET_WHITESPACE('\t');
    SET_WHITESPACE('\n');
    SET_WHITESPACE('\r');
    SET_WHITESPACE('\v');
    SET_WHITESPACE('\f');

    g_fExprInitializedMap = 1;
}


/**
 * Looks up a character in the map.
 *
 * @returns the value for that char, see g_abOpStartCharMap for details.
 * @param   ch      The character.
 */
DECLINLINE(unsigned char) expr_map_get(char ch)
{
    return g_abOpStartCharMap[(unsigned char)ch];
}


/**
 * Searches the operator table given a potential operator start char.
 *
 * @returns Pointer to the matching operator. NULL if not found.
 * @param   psz     Pointer to what can be an operator.
 * @param   uchVal  The expr_map_get value.
 * @param   fUnary  Whether it must be an unary operator or not.
 */
static PCEXPROP expr_lookup_op(char const *psz, unsigned char uchVal, int fUnary)
{
    char ch = *psz;
    unsigned i;
    Assert((uchVal & 2) == (RT_C_IS_ALPHA(ch) ? 0 : 2));

    for (i = uchVal >> 2; i < sizeof(g_aExprOps) / sizeof(g_aExprOps[0]); i++)
    {
        /* compare the string... */
        if (g_aExprOps[i].szOp[0] != ch)
            continue;
        switch (g_aExprOps[i].cchOp)
        {
            case 1:
                break;
            case 2:
                if (g_aExprOps[i].szOp[1] != psz[1])
                    continue;
                break;
            default:
                if (strncmp(&g_aExprOps[i].szOp[1], psz + 1, g_aExprOps[i].cchOp - 1))
                    continue;
                break;
        }

        /* ... and the operator type. */
        if (fUnary == (g_aExprOps[i].cArgs == 1))
        {
            /* Check if we've got the needed operand separation: */
            if (   (uchVal & 2)
                || EXPR_IS_OP_SEPARATOR(psz[g_aExprOps[i].cchOp]))
            {
                /* got a match! */
                return &g_aExprOps[i];
            }
        }
    }

    return NULL;
}


/**
 * Ungets a binary operator.
 *
 * The operator is poped from the stack and put in the pending position.
 *
 * @param   pThis       The evaluator instance.
 */
static void expr_unget_op(PEXPR pThis)
{
    Assert(pThis->pPending == NULL);
    Assert(pThis->iOp >= 0);

    pThis->pPending = pThis->apOps[pThis->iOp];
    pThis->apOps[pThis->iOp] = NULL;
    pThis->iOp--;
}



/**
 * Get the next token, it should be a binary operator, or the end of
 * the expression, or a right parenthesis.
 *
 * The operator is pushed onto the stack and the status code indicates
 * which of the two we found.
 *
 * @returns status code. Will grumble on failure.
 * @retval  kExprRet_EndOfExpr if we encountered the end of the expression.
 * @retval  kExprRet_Operator if we encountered a binary operator or right
 *          parenthesis. It's on the operator stack.
 *
 * @param   pThis       The evaluator instance.
 */
static EXPRRET expr_get_binary_or_eoe_or_rparen(PEXPR pThis)
{
    /*
     * See if there is anything pending first.
     */
    PCEXPROP pOp = pThis->pPending;
    if (pOp)
        pThis->pPending = NULL;
    else
    {
        /*
         * Eat more of the expression.
         */
        char const *psz = pThis->psz;

        /* spaces */
        unsigned char uchVal;
        char ch;
        while (((uchVal = expr_map_get((ch = *psz))) & 3) == 2)
            psz++;

        /* see what we've got. */
        if (ch)
        {
            if (uchVal & 1)
                pOp = expr_lookup_op(psz, uchVal, 0 /* fUnary */);
            if (!pOp)
                return expr_error(pThis, "Expected binary operator, found \"%.42s\"...", psz);
            psz += pOp->cchOp;
        }
        else
            pOp = &g_ExprEndOfExpOp;
        pThis->psz = psz;
    }

    /*
     * Push it.
     */
    if (pThis->iOp >= EXPR_MAX_OPERATORS - 1)
        return expr_error(pThis, "Operator stack overflow");
    pThis->apOps[++pThis->iOp] = pOp;

    return pOp->iPrecedence
         ? kExprRet_Operator
         : kExprRet_EndOfExpr;
}



/**
 * Get the next token, it should be an unary operator or an operand.
 *
 * This will fail if encountering the end of the expression since
 * it is implied that there should be something more.
 *
 * The token is pushed onto the respective stack and the status code
 * indicates which it is.
 *
 * @returns status code. On failure we'll be done bitching already.
 * @retval  kExprRet_Operator if we encountered an unary operator.
 *          It's on the operator stack.
 * @retval  kExprRet_Operand if we encountered an operand operator.
 *          It's on the operand stack.
 *
 * @param   pThis       The evaluator instance.
 */
static EXPRRET expr_get_unary_or_operand(PEXPR pThis)
{
    EXPRRET       rc;
    unsigned char uchVal;
    PCEXPROP      pOp;
    char const   *psz = pThis->psz;
    char          ch;

    /*
     * Eat white space and make sure there is something after it.
     */
    while (((uchVal = expr_map_get((ch = *psz))) & 3) == 2)
        psz++;
    if (ch == '\0')
        return expr_error(pThis, "Unexpected end of expression");

    /*
     * Is it an operator?
     */
    pOp = NULL;
    if (uchVal & 1)
        pOp = expr_lookup_op(psz, uchVal, 1 /* fUnary */);
    if (pOp)
    {
        /*
         * Push the operator onto the stack.
         */
        if (pThis->iVar < EXPR_MAX_OPERANDS - 1)
        {
            pThis->apOps[++pThis->iOp] = pOp;
            rc = kExprRet_Operator;
        }
        else
            rc = expr_error(pThis, "Operator stack overflow");
        psz += pOp->cchOp;
    }
    else if (pThis->iVar < EXPR_MAX_OPERANDS - 1)
    {
        /*
         * It's an operand. Figure out where it ends and
         * push it onto the stack.
         */
        const char *pszStart;

        rc = kExprRet_Ok;
        if (ch == '"')
        {
            pszStart = ++psz;
            while ((ch = *psz) != '\0' && ch != '"')
                psz++;
            rc = expr_var_init_substring(pThis, &pThis->aVars[++pThis->iVar], pszStart, psz - pszStart, kExprVar_QuotedString);
            if (ch != '\0')
                psz++;
        }
        else if (ch == '\'')
        {
            pszStart = ++psz;
            while ((ch = *psz) != '\0' && ch != '\'')
                psz++;
            rc = expr_var_init_substring(pThis, &pThis->aVars[++pThis->iVar], pszStart, psz - pszStart,
                                         kExprVar_QuotedSimpleString);
            if (ch != '\0')
                psz++;
        }
        else
        {
            unsigned cPars = 0;
            pszStart = psz;
            while ((ch = *psz) != '\0')
            {
                /* ${asdf} needs special handling. */
                if (   ch == '$'
                    && psz[1] == '{')
                {
                    psz++;
                    if (cPars < EXPR_MAX_VAR_RECURSION)
                        ++cPars;
                    else
                    {
                        rc = expr_error(pThis, "Too deep nesting of variable expansions");
                        break;
                    }
                }
                else if (ch == '}')
                {
                    if (cPars > 0)
                        cPars--;
                }
                else if (cPars == 0)
                {
                    uchVal = expr_map_get(ch);
                    if (uchVal == 0)
                    { /*likely*/ }
                    else if ((uchVal & 3) == 2 /*isspace*/)
                        break;
                    else if (   (uchVal & 1)
                             && psz != pszStart  /* not at the start */
                             && (   (uchVal & 2) /* operator without separator needs */
                                 || EXPR_IS_OP_SEPARATOR_NO_SPACE(psz[-1])))
                    {
                        pOp = expr_lookup_op(psz, uchVal, 0 /* fUnary */);
                        if (pOp)
                            break;
                    }
                }

                /* next */
                psz++;
            }

            if (rc == kExprRet_Ok)
                rc = expr_var_init_substring(pThis, &pThis->aVars[++pThis->iVar], pszStart, psz - pszStart, kExprVar_String);
        }
    }
    else
        rc = expr_error(pThis, "Operand stack overflow");
    pThis->psz = psz;

    return rc;
}


/**
 * Evaluates the current expression.
 *
 * @returns status code.
 *
 * @param   pThis       The instance.
 */
static EXPRRET expr_eval(PEXPR pThis)
{
    EXPRRET  rc;
    PCEXPROP pOp;

    /*
     * The main loop.
     */
    for (;;)
    {
        /*
         * Eat unary operators until we hit an operand.
         */
        do
            rc = expr_get_unary_or_operand(pThis);
        while (rc == kExprRet_Operator);
        if (rc < kExprRet_Ok)
            break;

        /*
         * Look for a binary operator, right parenthesis or end of expression.
         */
        rc = expr_get_binary_or_eoe_or_rparen(pThis);
        if (rc < kExprRet_Ok)
            break;
        expr_unget_op(pThis);

        /*
         * Pop operators and apply them.
         *
         * Parenthesis will be handed via precedence, where the left parenthesis
         * will go pop the right one and make another operator pending.
         */
        while (   pThis->iOp >= 0
               && pThis->apOps[pThis->iOp]->iPrecedence >= pThis->pPending->iPrecedence)
        {
            pOp = pThis->apOps[pThis->iOp--];
            Assert(pThis->iVar + 1 >= pOp->cArgs);
            rc = pOp->pfn(pThis);
            if (rc < kExprRet_Ok)
                break;
        }
        if (rc < kExprRet_Ok)
            break;

        /*
         * Get the next binary operator or end of expression.
         * There should be no right parenthesis here.
         */
        rc = expr_get_binary_or_eoe_or_rparen(pThis);
        if (rc < kExprRet_Ok)
            break;
        pOp = pThis->apOps[pThis->iOp];
        if (!pOp->iPrecedence)
            break;  /* end of expression */
        if (!pOp->cArgs)
        {
            rc = expr_error(pThis, "Unexpected \"%s\"", pOp->szOp);
            break;
        }
    }

    return rc;
}


/**
 * Destroys the given instance.
 *
 * @param   pThis       The instance to destroy.
 */
static void expr_destroy(PEXPR pThis)
{
    while (pThis->iVar >= 0)
    {
        expr_var_delete(pThis->aVars);
        pThis->iVar--;
    }
    RTMemTmpFree(pThis);
}


/**
 * Instantiates an expression evaluator.
 *
 * @returns The instance.
 */
static PEXPR expr_create(RTEXPREVALINT *pThis, const char *pch, size_t cch, PRTERRINFO pErrInfo)
{
    cch = RTStrNLen(pch, cch);

    PEXPR pExpr = (PEXPR)RTMemTmpAllocZ(sizeof(*pExpr) + cch + 1);
    if (pExpr)
    {
        pExpr->psz = pExpr->pszExpr = (char *)memcpy(pExpr + 1, pch, cch);
        pExpr->pErrInfo = pErrInfo;
        pExpr->pEvaluator = pThis;
        pExpr->pPending = NULL;
        pExpr->iVar = -1;
        pExpr->iOp = -1;

        expr_map_init();
    }
    return pExpr;
}



/*********************************************************************************************************************************
*   API                                                                                                                          *
*********************************************************************************************************************************/

/** @callback_method_impl{PFNRTEXPREVALQUERYVARIABLE, Stub}   */
static DECLCALLBACK(int) rtExprEvalDummyQueryVariable(const char *pchName, size_t cchName, void *pvUser, char **ppszValue)
{
    RT_NOREF(pchName, cchName, pvUser);
    if (ppszValue)
        *ppszValue = NULL;
    return VERR_NOT_FOUND;
}


RTDECL(int) RTExprEvalCreate(PRTEXPREVAL phEval, uint64_t fFlags, const char *pszName,
                             void *pvUser, PFNRTEXPREVALQUERYVARIABLE pfnQueryVariable)
{
    AssertPtrReturn(phEval, VERR_INVALID_POINTER);
    *phEval = NULL;
    AssertPtrNullReturn(pfnQueryVariable, VERR_INVALID_POINTER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~0), VERR_INVALID_FLAGS);

    char *pszNameCopy = RTStrDup(pszName);
    if (pszNameCopy)
    {
        RTEXPREVALINT *pThis = (RTEXPREVALINT *)RTMemAllocZ(sizeof(*pThis));
        if (pThis)
        {
            pThis->u32Magic             = RTEXPREVAL_MAGIC;
            pThis->cRefs                = 1;
            pThis->fFlags               = fFlags;
            pThis->pszName              = pszNameCopy;
            pThis->pvUser               = pvUser;
            pThis->pfnQueryVariable     = pfnQueryVariable ? pfnQueryVariable : rtExprEvalDummyQueryVariable;
            *phEval = pThis;
            return VINF_SUCCESS;

        }

        RTStrFree(pszNameCopy);
        return VERR_NO_MEMORY;
    }
    return VERR_NO_STR_MEMORY;
}


RTDECL(uint32_t) RTExprEvalRetain(RTEXPREVAL hEval)
{
    RTEXPREVALINT *pThis = hEval;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTEXPREVAL_MAGIC, UINT32_MAX);
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs > 1);
    Assert(cRefs < 512);
    return cRefs;
}


RTDECL(uint32_t) RTExprEvalRelease(RTEXPREVAL hEval)
{
    RTEXPREVALINT *pThis = hEval;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTEXPREVAL_MAGIC, UINT32_MAX);
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < 512);
    if (cRefs == 0)
    {
        pThis->u32Magic = ~RTEXPREVAL_MAGIC;
        if (pThis->pszName)
        {
            RTStrFree(pThis->pszName);
            pThis->pszName = NULL;
        }
        RTMemFree(pThis);
        return 0;
    }
    return cRefs;
}


RTDECL(int) RTExprEvalToBool(RTEXPREVAL hEval, const char *pch, size_t cch, bool *pfResult, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(pfResult, VERR_INVALID_POINTER);
    *pfResult = false;
    RTEXPREVALINT *pThis = hEval;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTEXPREVAL_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Instantiate the expression evaluator and let it have a go at it.
     */
    int rc;
    PEXPR pExpr = expr_create(pThis, pch, cch, pErrInfo);
    if (pExpr)
    {
        if (expr_eval(pExpr) >= kExprRet_Ok)
        {
            /*
             * Convert the result (on top of the stack) to boolean and
             * set our return value accordingly.
             */
            if (   expr_var_make_bool(pExpr, &pExpr->aVars[0]) == kExprRet_Ok
                && pExpr->aVars[0].uVal.i)
                *pfResult = true;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_PARSE_ERROR; /** @todo better errors? */
        expr_destroy(pExpr);
    }
    else
        rc = VERR_NO_TMP_MEMORY;
    return rc;
}


RTDECL(int) RTExprEvalToInteger(RTEXPREVAL hEval, const char *pch, size_t cch, int64_t *piResult, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(piResult, VERR_INVALID_POINTER);
    *piResult = INT64_MAX;
    RTEXPREVALINT *pThis = hEval;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTEXPREVAL_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Instantiate the expression evaluator and let it have a go at it.
     */
    int rc;
    PEXPR pExpr = expr_create(pThis, pch, cch, pErrInfo);
    if (pExpr)
    {
        if (expr_eval(pExpr) >= kExprRet_Ok)
        {
            /*
             * Convert the result (on top of the stack) to boolean and
             * set our return value accordingly.
             */
            PEXPRVAR pVar = &pExpr->aVars[0];
            EXPRRET rcExpr = expr_var_make_num(pExpr, pVar);
            if (rcExpr >= kExprRet_Ok)
            {
                *piResult = pVar->uVal.i;
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_PARSE_ERROR; /** @todo better error! */
        }
        else
            rc = VERR_PARSE_ERROR; /** @todo better errors? */
        expr_destroy(pExpr);
    }
    else
        rc = VERR_NO_TMP_MEMORY;
    return rc;
}


RTDECL(int) RTExprEvalToString(RTEXPREVAL hEval, const char *pch, size_t cch, char **ppszResult, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(ppszResult, VERR_INVALID_POINTER);
    *ppszResult = NULL;
    RTEXPREVALINT *pThis = hEval;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTEXPREVAL_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Instantiate the expression evaluator and let it have a go at it.
     */
    int rc;
    PEXPR pExpr = expr_create(pThis, pch, cch, pErrInfo);
    if (pExpr)
    {
        if (expr_eval(pExpr) >= kExprRet_Ok)
        {
            /*
             * Convert the result (on top of the stack) to a string
             * and copy it out the variable buffer.
             */
            PEXPRVAR pVar = &pExpr->aVars[0];
            if (expr_var_make_simple_string(pExpr, pVar) == kExprRet_Ok)
                rc = RTStrDupEx(ppszResult, pVar->uVal.psz);
            else
                rc = VERR_NO_TMP_MEMORY;
        }
        else
            rc = VERR_PARSE_ERROR;
        expr_destroy(pExpr);
    }
    else
        rc = VERR_NO_TMP_MEMORY;

    return rc;
}

