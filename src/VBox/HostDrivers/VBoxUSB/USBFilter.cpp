/* $Id: USBFilter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB filter abstraction.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <VBox/usbfilter.h>
#include <VBox/usblib.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>


/** @todo split this up for the sake of device drivers and such. */


/**
 * Initializes an USBFILTER structure.
 *
 * @param   pFilter     The filter to initialize.
 * @param   enmType     The filter type. If not valid, the filter will not
 *                      be properly initialized and all other calls will fail.
 */
USBLIB_DECL(void) USBFilterInit(PUSBFILTER pFilter, USBFILTERTYPE enmType)
{
    memset(pFilter, 0, sizeof(*pFilter));
    AssertReturnVoid(enmType > USBFILTERTYPE_INVALID && enmType < USBFILTERTYPE_END);
    pFilter->u32Magic = USBFILTER_MAGIC;
    pFilter->enmType = enmType;
    for (unsigned i = 0; i < RT_ELEMENTS(pFilter->aFields); i++)
        pFilter->aFields[i].enmMatch = USBFILTERMATCH_IGNORE;
}


/**
 * Make a clone of the specified filter.
 *
 * @param   pFilter     The target filter.
 * @param   pToClone    The source filter.
 */
USBLIB_DECL(void) USBFilterClone(PUSBFILTER pFilter, PCUSBFILTER pToClone)
{
    memcpy(pFilter, pToClone, sizeof(*pToClone));
}


/**
 * Deletes (invalidates) an USBFILTER structure.
 *
 * @param pFilter       The filter to delete.
 */
USBLIB_DECL(void) USBFilterDelete(PUSBFILTER pFilter)
{
    pFilter->u32Magic = ~USBFILTER_MAGIC;
    pFilter->enmType = USBFILTERTYPE_INVALID;
    pFilter->offCurEnd = 0xfffff;
}


/**
 * Skips blanks.
 *
 * @returns Next non-blank char in the string.
 * @param   psz         The string.
 */
DECLINLINE(const char *) usbfilterSkipBlanks(const char *psz)
{
    while (RT_C_IS_BLANK(*psz))
        psz++;
    return psz;
}


/**
 * Worker for usbfilterReadNumber that parses a hexadecimal number.
 *
 * @returns Same as usbfilterReadNumber, except for VERR_NO_DIGITS.
 * @param   pszExpr         Where to start converting, first char is a valid digit.
 * @param   ppszExpr        See usbfilterReadNumber.
 * @param   pu16Val         See usbfilterReadNumber.
 */
static int usbfilterReadNumberHex(const char *pszExpr, const char **ppszExpr, uint16_t *pu16Val)
{
    int rc = VINF_SUCCESS;
    uint32_t u32 = 0;
    do
    {
        unsigned uDigit = *pszExpr >= 'a' && *pszExpr <= 'f'
                        ? *pszExpr - 'a' + 10
                        : *pszExpr >= 'A' && *pszExpr <= 'F'
                        ? *pszExpr - 'A' + 10
                        : *pszExpr - '0';
        if (uDigit >= 16)
            break;
        u32 *= 16;
        u32 += uDigit;
        if (u32 > UINT16_MAX)
            rc = VWRN_NUMBER_TOO_BIG;
    } while (*++pszExpr);

    *ppszExpr = usbfilterSkipBlanks(pszExpr);
    *pu16Val = rc == VINF_SUCCESS ? u32 : UINT16_MAX;
    return VINF_SUCCESS;
}


/**
 * Worker for usbfilterReadNumber that parses a decimal number.
 *
 * @returns Same as usbfilterReadNumber, except for VERR_NO_DIGITS.
 * @param   pszExpr         Where to start converting, first char is a valid digit.
 * @param   uBase           The base - 8 or 16.
 * @param   ppszExpr        See usbfilterReadNumber.
 * @param   pu16Val         See usbfilterReadNumber.
 */
static int usbfilterReadNumberDecimal(const char *pszExpr, unsigned uBase, const char **ppszExpr, uint16_t *pu16Val)
{
    int rc = VINF_SUCCESS;
    uint32_t u32 = 0;
    do
    {
        unsigned uDigit = *pszExpr - '0';
        if (uDigit >= uBase)
            break;
        u32 *= uBase;
        u32 += uDigit;
        if (u32 > UINT16_MAX)
            rc = VWRN_NUMBER_TOO_BIG;
    } while (*++pszExpr);

    *ppszExpr = usbfilterSkipBlanks(pszExpr);
    *pu16Val = rc == VINF_SUCCESS ? u32 : UINT16_MAX;
    return rc;
}


/**
 * Reads a number from a numeric expression.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if all is fine. *ppszExpr and *pu16Val are updated.
 * @retval  VWRN_NUMBER_TOO_BIG if the number exceeds unsigned 16-bit, both *ppszExpr and *pu16Val are updated.
 * @retval  VERR_NO_DIGITS if there aren't any digits.
 *
 * @param   ppszExpr        Pointer to the current expression pointer.
 *                          This is advanced past the expression and trailing blanks on success.
 * @param   pu16Val         Where to store the value on success.
 */
static int usbfilterReadNumber(const char **ppszExpr, uint16_t *pu16Val)
{
    const char *pszExpr = usbfilterSkipBlanks(*ppszExpr);
    if (!RT_C_IS_DIGIT(*pszExpr))
        return VERR_NO_DIGITS;

    if (*pszExpr == '0')
    {
        if (pszExpr[1] == 'x' || pszExpr[1] == 'X')
        {
            if (!RT_C_IS_XDIGIT(pszExpr[2]))
                return VERR_NO_DIGITS;
            return usbfilterReadNumberHex(pszExpr + 2, ppszExpr, pu16Val);
        }
        if (RT_C_IS_ODIGIT(pszExpr[1]))
            return usbfilterReadNumberDecimal(pszExpr + 1, 8, ppszExpr, pu16Val);
        /* Solitary 0! */
        if (RT_C_IS_DIGIT(pszExpr[1]))
            return VERR_NO_DIGITS;
    }
    return usbfilterReadNumberDecimal(pszExpr, 10, ppszExpr, pu16Val);
}


/**
 * Validates a numeric expression.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if valid.
 * @retval  VERR_INVALID_PARAMETER if invalid.
 * @retval  VERR_NO_DIGITS if some expression is short of digits.
 *
 * @param   pszExpr         The numeric expression.
 */
static int usbfilterValidateNumExpression(const char *pszExpr)
{
    /*
     * An empty expression is fine.
     */
    if (!*pszExpr)
        return VINF_SUCCESS;

    /*
     * The string format is: "int:((<m>)|([<m>]-[<n>]))(,(<m>)|([<m>]-[<n>]))*"
     * where <m> and <n> are numbers in decimal, hex (0xNNN) or octal (0NNN).
     * Spaces are allowed around <m> and <n>.
     */
    unsigned cSubExpressions = 0;
    while (*pszExpr)
    {
        if (!strncmp(pszExpr, RT_STR_TUPLE("int:")))
            pszExpr += strlen("int:");

        /*
         * Skip remnants of the previous expression and any empty expressions.
         * ('|' is the expression separator.)
         */
        while (*pszExpr == '|' || RT_C_IS_BLANK(*pszExpr) || *pszExpr == '(' || *pszExpr == ')')
            pszExpr++;
        if (!*pszExpr)
            break;

        /*
         * Parse the expression.
         */
        int rc;
        uint16_t u16First = 0;
        uint16_t u16Last = 0;
        if (*pszExpr == '-')
        {
            /* -N */
            pszExpr++;
            rc = usbfilterReadNumber(&pszExpr, &u16Last);
        }
        else
        {
            /* M or M,N or M-N or M- */
            rc = usbfilterReadNumber(&pszExpr, &u16First);
            if (RT_SUCCESS(rc))
            {
                pszExpr = usbfilterSkipBlanks(pszExpr);
                if (*pszExpr == '-')
                {
                    pszExpr++;
                    if (*pszExpr) /* M-N */
                        rc = usbfilterReadNumber(&pszExpr, &u16Last);
                    else /* M- */
                        u16Last = UINT16_MAX;
                }
                else if (*pszExpr == ',')
                {
                    /* M,N */
                    pszExpr++;
                    rc = usbfilterReadNumber(&pszExpr, &u16Last);
                }
                else
                {
                    /* M */
                    u16Last = u16First;
                }
            }
        }
        if (RT_FAILURE(rc))
            return rc;

        /*
         * We should either be at the end of the string, at an expression separator (|),
         * or at the end of an interval filter (')').
         */
        if (*pszExpr && *pszExpr != '|' && *pszExpr != ')')
            return VERR_INVALID_PARAMETER;

        cSubExpressions++;
    }

    return cSubExpressions ? VINF_SUCCESS : VERR_INVALID_PARAMETER;
}


/**
 * Validates a string pattern.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if valid.
 * @retval  VERR_INVALID_PARAMETER if invalid.
 *
 * @param   psz             The string pattern.
 */
static int usbfilterValidateStringPattern(const char *psz)
{
    /*
     * This is only becomes important if we start doing
     * sets ([0-9]) and such like.
     */
    RT_NOREF1(psz);
    return VINF_SUCCESS;
}


/**
 * Thoroughly validates the USB Filter.
 *
 * @returns Appropriate VBox status code.
 * @param   pFilter     The filter to validate.
 */
USBLIB_DECL(int) USBFilterValidate(PCUSBFILTER pFilter)
{
    if (!RT_VALID_PTR(pFilter))
        return VERR_INVALID_POINTER;

    if (pFilter->u32Magic != USBFILTER_MAGIC)
        return VERR_INVALID_MAGIC;

    if (    pFilter->enmType <= USBFILTERTYPE_INVALID
        ||  pFilter->enmType >= USBFILTERTYPE_END)
    {
        Log(("USBFilter: %p - enmType=%d!\n", pFilter, pFilter->enmType));
        return VERR_INVALID_PARAMETER;
    }

    if (pFilter->offCurEnd >= sizeof(pFilter->achStrTab))
    {
        Log(("USBFilter: %p - offCurEnd=%#x!\n", pFilter, pFilter->offCurEnd));
        return VERR_INVALID_PARAMETER;
    }

    /* Validate that string value offsets are inside the string table. */
    for (uint32_t i = 0; i < RT_ELEMENTS(pFilter->aFields); i++)
    {
        if (    USBFilterIsMethodUsingStringValue((USBFILTERMATCH)pFilter->aFields[i].enmMatch)
            &&  pFilter->aFields[i].u16Value > pFilter->offCurEnd)
        {
            Log(("USBFilter: %p - bad offset=%#x\n", pFilter, pFilter->aFields[i].u16Value));
            return VERR_INVALID_PARAMETER;
        }
    }

    /*
     * Validate the string table.
     */
     if (pFilter->achStrTab[0])
    {
        Log(("USBFilter: %p - bad null string\n", pFilter));
        return VERR_INVALID_PARAMETER;
    }

    const char *psz = &pFilter->achStrTab[1];
    while (psz < &pFilter->achStrTab[pFilter->offCurEnd])
    {
        const char *pszEnd = RTStrEnd(psz, &pFilter->achStrTab[sizeof(pFilter->achStrTab)] - psz);
        if (!pszEnd)
        {
            Log(("USBFilter: %p - string at %#x isn't terminated!\n",
                 pFilter, psz - &pFilter->achStrTab[0]));
            return VERR_INVALID_PARAMETER;
        }

        uint16_t off = (uint16_t)(uintptr_t)(psz - &pFilter->achStrTab[0]);
        unsigned i;
        for (i = 0; i < RT_ELEMENTS(pFilter->aFields); i++)
            if (    USBFilterIsMethodUsingStringValue((USBFILTERMATCH)pFilter->aFields[i].enmMatch)
                &&  pFilter->aFields[i].u16Value == off)
                break;
        if (i >= RT_ELEMENTS(pFilter->aFields))
        {
            Log(("USBFilter: %p - string at %#x isn't used by anyone! (%s)\n",
                 pFilter, psz - &pFilter->achStrTab[0], psz));
            return VERR_INVALID_PARAMETER;
        }

        psz = pszEnd + 1;
    }

    if ((uintptr_t)(psz - &pFilter->achStrTab[0] - 1) != pFilter->offCurEnd)
    {
        Log(("USBFilter: %p - offCurEnd=%#x currently at %#x\n",
             pFilter, pFilter->offCurEnd, psz - &pFilter->achStrTab[0] - 1));
        return VERR_INVALID_PARAMETER;
    }

    while (psz < &pFilter->achStrTab[sizeof(pFilter->achStrTab)])
    {
        if (*psz)
        {
            Log(("USBFilter: %p - str tab isn't zero padded! %#x: %c\n",
                 pFilter, psz - &pFilter->achStrTab[0], *psz));
            return VERR_INVALID_PARAMETER;
        }
        psz++;
    }


    /*
     * Validate the fields.
     */
    int rc;
    for (unsigned i = 0; i < RT_ELEMENTS(pFilter->aFields); i++)
    {
        switch (pFilter->aFields[i].enmMatch)
        {
            case USBFILTERMATCH_IGNORE:
            case USBFILTERMATCH_PRESENT:
                if (pFilter->aFields[i].u16Value)
                {
                    Log(("USBFilter: %p - #%d/%d u16Value=%d expected 0!\n",
                         pFilter, i, pFilter->aFields[i].enmMatch, pFilter->aFields[i].u16Value));
                    return VERR_INVALID_PARAMETER;
                }
                break;

            case USBFILTERMATCH_NUM_EXACT:
            case USBFILTERMATCH_NUM_EXACT_NP:
                if (!USBFilterIsNumericField((USBFILTERIDX)i))
                {
                    Log(("USBFilter: %p - #%d / %d - not numeric field\n",
                         pFilter, i, pFilter->aFields[i].enmMatch));
                    return VERR_INVALID_PARAMETER;
                }
                break;

            case USBFILTERMATCH_NUM_EXPRESSION:
            case USBFILTERMATCH_NUM_EXPRESSION_NP:
                if (!USBFilterIsNumericField((USBFILTERIDX)i))
                {
                    Log(("USBFilter: %p - #%d / %d - not numeric field\n",
                         pFilter, i, pFilter->aFields[i].enmMatch));
                    return VERR_INVALID_PARAMETER;
                }
                if (    pFilter->aFields[i].u16Value >= pFilter->offCurEnd
                    &&  pFilter->offCurEnd)
                {
                    Log(("USBFilter: %p - #%d / %d - off=%#x max=%#x\n",
                         pFilter, i, pFilter->aFields[i].enmMatch, pFilter->aFields[i].u16Value, pFilter->offCurEnd));
                    return VERR_INVALID_PARAMETER;
                }
                psz = &pFilter->achStrTab[pFilter->aFields[i].u16Value];
                rc = usbfilterValidateNumExpression(psz);
                if (RT_FAILURE(rc))
                {
                    Log(("USBFilter: %p - #%d / %d - bad num expr: %s (rc=%Rrc)\n",
                         pFilter, i, pFilter->aFields[i].enmMatch, psz, rc));
                    return rc;
                }
                break;

            case USBFILTERMATCH_STR_EXACT:
            case USBFILTERMATCH_STR_EXACT_NP:
                if (!USBFilterIsStringField((USBFILTERIDX)i))
                {
                    Log(("USBFilter: %p - #%d / %d - not string field\n",
                         pFilter, i, pFilter->aFields[i].enmMatch));
                    return VERR_INVALID_PARAMETER;
                }
                if (    pFilter->aFields[i].u16Value >= pFilter->offCurEnd
                    &&  pFilter->offCurEnd)
                {
                    Log(("USBFilter: %p - #%d / %d - off=%#x max=%#x\n",
                         pFilter, i, pFilter->aFields[i].enmMatch, pFilter->aFields[i].u16Value, pFilter->offCurEnd));
                    return VERR_INVALID_PARAMETER;
                }
                break;

            case USBFILTERMATCH_STR_PATTERN:
            case USBFILTERMATCH_STR_PATTERN_NP:
                if (!USBFilterIsStringField((USBFILTERIDX)i))
                {
                    Log(("USBFilter: %p - #%d / %d - not string field\n",
                         pFilter, i, pFilter->aFields[i].enmMatch));
                    return VERR_INVALID_PARAMETER;
                }
                if (    pFilter->aFields[i].u16Value >= pFilter->offCurEnd
                    &&  pFilter->offCurEnd)
                {
                    Log(("USBFilter: %p - #%d / %d - off=%#x max=%#x\n",
                         pFilter, i, pFilter->aFields[i].enmMatch, pFilter->aFields[i].u16Value, pFilter->offCurEnd));
                    return VERR_INVALID_PARAMETER;
                }
                psz = &pFilter->achStrTab[pFilter->aFields[i].u16Value];
                rc = usbfilterValidateStringPattern(psz);
                if (RT_FAILURE(rc))
                {
                    Log(("USBFilter: %p - #%d / %d - bad string pattern: %s (rc=%Rrc)\n",
                         pFilter, i, pFilter->aFields[i].enmMatch, psz, rc));
                    return rc;
                }
                break;

            default:
                Log(("USBFilter: %p - #%d enmMatch=%d!\n", pFilter, i, pFilter->aFields[i].enmMatch));
                return VERR_INVALID_PARAMETER;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Find the specified field in the string table.
 *
 * @returns Pointer to the string in the string table on success.
 *          NULL if the field is invalid or it doesn't have a string value.
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The field index.
 */
DECLINLINE(const char *) usbfilterGetString(PCUSBFILTER pFilter, USBFILTERIDX enmFieldIdx)
{
    if ((unsigned)enmFieldIdx < (unsigned)USBFILTERIDX_END)
    {
        switch (pFilter->aFields[enmFieldIdx].enmMatch)
        {
            case USBFILTERMATCH_NUM_EXPRESSION:
            case USBFILTERMATCH_NUM_EXPRESSION_NP:
            case USBFILTERMATCH_STR_EXACT:
            case USBFILTERMATCH_STR_EXACT_NP:
            case USBFILTERMATCH_STR_PATTERN:
            case USBFILTERMATCH_STR_PATTERN_NP:
                Assert(pFilter->aFields[enmFieldIdx].u16Value < sizeof(pFilter->achStrTab));
                return &pFilter->achStrTab[pFilter->aFields[enmFieldIdx].u16Value];

            default:
                AssertMsgFailed(("%d\n", pFilter->aFields[enmFieldIdx].enmMatch));
            case USBFILTERMATCH_IGNORE:
            case USBFILTERMATCH_PRESENT:
            case USBFILTERMATCH_NUM_EXACT:
            case USBFILTERMATCH_NUM_EXACT_NP:
                break;
        }
    }
    return NULL;
}


/**
 * Gets a number value of a field.
 *
 * The field must contain a numeric value.
 *
 * @returns The field value on success, -1 on failure (invalid input / not numeric).
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The field index.
 */
DECLINLINE(int) usbfilterGetNum(PCUSBFILTER pFilter, USBFILTERIDX enmFieldIdx)
{
    if ((unsigned)enmFieldIdx < (unsigned)USBFILTERIDX_END)
    {
        switch (pFilter->aFields[enmFieldIdx].enmMatch)
        {
            case USBFILTERMATCH_NUM_EXACT:
            case USBFILTERMATCH_NUM_EXACT_NP:
                return pFilter->aFields[enmFieldIdx].u16Value;

            default:
                AssertMsgFailed(("%d\n", pFilter->aFields[enmFieldIdx].enmMatch));
            case USBFILTERMATCH_IGNORE:
            case USBFILTERMATCH_PRESENT:
            case USBFILTERMATCH_NUM_EXPRESSION:
            case USBFILTERMATCH_NUM_EXPRESSION_NP:
            case USBFILTERMATCH_STR_EXACT:
            case USBFILTERMATCH_STR_EXACT_NP:
            case USBFILTERMATCH_STR_PATTERN:
            case USBFILTERMATCH_STR_PATTERN_NP:
                break;
        }
    }
    return -1;
}


/**
 * Performs simple pattern matching.
 *
 * @returns true on match and false on mismatch.
 * @param   pszExpr     The numeric expression.
 * @param   u16Value    The value to match.
 */
static bool usbfilterMatchNumExpression(const char *pszExpr, uint16_t u16Value)
{
    /*
     * The string format is: "int:((<m>)|([<m>]-[<n>]))(,(<m>)|([<m>]-[<n>]))*"
     * where <m> and <n> are numbers in decimal, hex (0xNNN) or octal (0NNN).
     * Spaces are allowed around <m> and <n>.
     */
    while (*pszExpr)
    {
        if (!strncmp(pszExpr, RT_STR_TUPLE("int:")))
            pszExpr += strlen("int:");

        /*
         * Skip remnants of the previous expression and any empty expressions.
         * ('|' is the expression separator.)
         */
        while (*pszExpr == '|' || RT_C_IS_BLANK(*pszExpr) || *pszExpr == '(' || *pszExpr == ')')
            pszExpr++;
        if (!*pszExpr)
            break;

        /*
         * Parse the expression.
         */
        int rc;
        uint16_t u16First = 0;
        uint16_t u16Last = 0;
        if (*pszExpr == '-')
        {
            /* -N */
            pszExpr++;
            rc = usbfilterReadNumber(&pszExpr, &u16Last);
        }
        else
        {
            /* M or M,N or M-N or M- */
            rc = usbfilterReadNumber(&pszExpr, &u16First);
            if (RT_SUCCESS(rc))
            {
                pszExpr = usbfilterSkipBlanks(pszExpr);
                if (*pszExpr == '-')
                {
                    pszExpr++;
                    if (*pszExpr) /* M-N */
                        rc = usbfilterReadNumber(&pszExpr, &u16Last);
                    else /* M- */
                        u16Last = UINT16_MAX;
                }
                else if (*pszExpr == ',')
                {
                    /* M,N */
                    pszExpr++;
                    rc = usbfilterReadNumber(&pszExpr, &u16Last);
                }
                else
                {
                    /* M */
                    u16Last = u16First;
                }
            }
        }

        /* On success, we should either be at the end of the string, at an expression
         * separator (|), or at the end of an interval filter (')').
         */
        if (RT_SUCCESS(rc) && *pszExpr && *pszExpr != '|' && *pszExpr != ')')
            rc = VERR_INVALID_PARAMETER;
        if (RT_SUCCESS(rc))
        {
            /*
             * Swap the values if the order is mixed up.
             */
            if (u16First > u16Last)
            {
                uint16_t u16Tmp = u16First;
                u16First = u16Last;
                u16Last = u16Tmp;
            }

            /*
             * Perform the compare.
             */
            if (    u16Value >= u16First
                &&  u16Value <= u16Last)
                return true;
        }
        else
        {
            /*
             * Skip the bad expression.
             * ('|' is the expression separator.)
             */
            while (*pszExpr && *pszExpr != '|')
                pszExpr++;
        }
    }

    return false;
}


/**
 * Performs simple pattern matching.
 *
 * @returns true on match and false on mismatch.
 * @param   pszPattern  The pattern to match against.
 * @param   psz         The string to match.
 */
static bool usbfilterMatchStringPattern(const char *pszPattern, const char *psz)
{
    char ch;
    while ((ch = *pszPattern++))
    {
        if (ch == '?')
        {
            /*
             * Matches one char or end of string.
             */
            if (*psz)
                psz++;
        }
        else if (ch == '*')
        {
            /*
             * Matches zero or more characters.
             */
            /* skip subsequent wildcards */
            while (     (ch = *pszPattern) == '*'
                   ||   ch == '?')
                pszPattern++;
            if (!ch)
                /* Pattern ends with a '*' and thus matches the rest of psz. */
                return true;

            /* Find the length of the following exact pattern sequence. */
            ssize_t cchMatch = 1;
            while (     (ch = pszPattern[cchMatch]) != '\0'
                   &&   ch != '*'
                   &&   ch != '?')
                cchMatch++;

            /* Check if the exact pattern sequence is too long. */
            ssize_t cch = strlen(psz);
            cch -= cchMatch;
            if (cch < 0)
                return false;

            /* Is the rest an exact match? */
            if (!ch)
                return memcmp(psz + cch, pszPattern, cchMatch) == 0;

            /*
             * This is where things normally starts to get recursive or ugly.
             *
             * Just to make life simple, we'll skip the nasty stuff and say
             * that we will do a maximal wildcard match and forget about any
             * alternative matches.
             *
             * If somebody is bored out of their mind one day, feel free to
             * implement correct matching without using recursion.
             */
            ch = *pszPattern;
            const char *pszMatch = NULL;
            while (     cch-- >= 0
                   &&   *psz)
            {
                if (    *psz == ch
                    &&  !strncmp(psz, pszPattern, cchMatch))
                    pszMatch = psz;
                psz++;
            }
            if (!pszMatch)
                return false;

            /* advance */
            psz = pszMatch + cchMatch;
            pszPattern += cchMatch;
        }
        else
        {
            /* exact match */
            if (ch != *psz)
                return false;
            psz++;
        }
    }

    return *psz == '\0';
}


/**
 * Match a filter against a device.
 *
 * @returns true if they match, false if not.
 *
 * @param   pFilter     The filter to match with.
 * @param   pDevice     The device data. This is a filter (type ignored) that
 *                      contains 'exact' values for all present fields and 'ignore'
 *                      values for the non-present fields.
 *
 * @remark  Both the filter and the device are ASSUMED to be valid because
 *          we don't wish to waste any time in this function.
 */
USBLIB_DECL(bool) USBFilterMatch(PCUSBFILTER pFilter, PCUSBFILTER pDevice)
{
    return USBFilterMatchRated(pFilter, pDevice) > 0;
}


#if 0 /*def IN_RING0*/ /** @todo convert to proper logging. */
extern "C" int printf(const char *format, ...);
# define dprintf(a) printf a
#else
# define dprintf(a) do {} while (0)
#endif

/**
 * Match a filter against a device and rate the result.
 *
 * @returns -1 if no match, matching rate between 1 and 100 (inclusive) if matched.
 *
 * @param   pFilter     The filter to match with.
 * @param   pDevice     The device data. This is a filter (type ignored) that
 *                      contains 'exact' values for all present fields and 'ignore'
 *                      values for the non-present fields.
 *
 * @remark  Both the filter and the device are ASSUMED to be valid because
 *          we don't wish to waste any time in this function.
 */
USBLIB_DECL(int) USBFilterMatchRated(PCUSBFILTER pFilter, PCUSBFILTER pDevice)
{
    unsigned iRate = 0;
dprintf(("USBFilterMatchRated: %p %p\n", pFilter, pDevice));

    for (unsigned i = 0; i < RT_ELEMENTS(pFilter->aFields); i++)
    {
        switch (pFilter->aFields[i].enmMatch)
        {
            case USBFILTERMATCH_IGNORE:
                iRate += 2;
                break;

            case USBFILTERMATCH_PRESENT:
                if (pDevice->aFields[i].enmMatch == USBFILTERMATCH_IGNORE)
                {
dprintf(("filter match[%d]: !present\n", i));
                    return -1;
                }
                iRate += 2;
                break;

            case USBFILTERMATCH_NUM_EXACT:
                if (    pDevice->aFields[i].enmMatch == USBFILTERMATCH_IGNORE
                    ||  pFilter->aFields[i].u16Value != pDevice->aFields[i].u16Value)
                {
if (pDevice->aFields[i].enmMatch == USBFILTERMATCH_IGNORE)
    dprintf(("filter match[%d]: !num_exact device=ignore\n", i));
else
    dprintf(("filter match[%d]: !num_exact %#x (filter) != %#x (device)\n", i, pFilter->aFields[i].u16Value, pDevice->aFields[i].u16Value));
                    return -1;
                }
                iRate += 2;
                break;

            case USBFILTERMATCH_NUM_EXACT_NP:
                if (    pDevice->aFields[i].enmMatch != USBFILTERMATCH_IGNORE
                    &&  pFilter->aFields[i].u16Value != pDevice->aFields[i].u16Value)
                {
dprintf(("filter match[%d]: !num_exact_np %#x (filter) != %#x (device)\n", i, pFilter->aFields[i].u16Value, pDevice->aFields[i].u16Value));
                    return -1;
                }
                iRate += 2;
                break;

            case USBFILTERMATCH_NUM_EXPRESSION:
                if (    pDevice->aFields[i].enmMatch == USBFILTERMATCH_IGNORE
                    ||  !usbfilterMatchNumExpression(usbfilterGetString(pFilter, (USBFILTERIDX)i),
                                                     pDevice->aFields[i].u16Value))
                {
dprintf(("filter match[%d]: !num_expression\n", i));
                    return -1;
                }
                iRate += 1;
                break;

            case USBFILTERMATCH_NUM_EXPRESSION_NP:
                if (    pDevice->aFields[i].enmMatch != USBFILTERMATCH_IGNORE
                    &&  !usbfilterMatchNumExpression(usbfilterGetString(pFilter, (USBFILTERIDX)i),
                                                     pDevice->aFields[i].u16Value))
                {
dprintf(("filter match[%d]: !num_expression_no\n", i));
                    return -1;
                }
                iRate += 1;
                break;

            case USBFILTERMATCH_STR_EXACT:
                if (    pDevice->aFields[i].enmMatch == USBFILTERMATCH_IGNORE
                    ||  strcmp(usbfilterGetString(pFilter, (USBFILTERIDX)i),
                               usbfilterGetString(pDevice, (USBFILTERIDX)i)))
                {
dprintf(("filter match[%d]: !str_exact\n", i));
                    return -1;
                }
                iRate += 2;
                break;

            case USBFILTERMATCH_STR_EXACT_NP:
                if (    pDevice->aFields[i].enmMatch != USBFILTERMATCH_IGNORE
                    &&  strcmp(usbfilterGetString(pFilter, (USBFILTERIDX)i),
                               usbfilterGetString(pDevice, (USBFILTERIDX)i)))
                {
dprintf(("filter match[%d]: !str_exact_np\n", i));
                    return -1;
                }
                iRate += 2;
                break;

            case USBFILTERMATCH_STR_PATTERN:
                if (    pDevice->aFields[i].enmMatch == USBFILTERMATCH_IGNORE
                    ||  !usbfilterMatchStringPattern(usbfilterGetString(pFilter, (USBFILTERIDX)i),
                                                     usbfilterGetString(pDevice, (USBFILTERIDX)i)))
                {
dprintf(("filter match[%d]: !str_pattern\n", i));
                    return -1;
                }
                iRate += 1;
                break;

            case USBFILTERMATCH_STR_PATTERN_NP:
                if (    pDevice->aFields[i].enmMatch != USBFILTERMATCH_IGNORE
                    &&  !usbfilterMatchStringPattern(usbfilterGetString(pFilter, (USBFILTERIDX)i),
                                                     usbfilterGetString(pDevice, (USBFILTERIDX)i)))
                {
dprintf(("filter match[%d]: !str_pattern_np\n", i));
                    return -1;
                }
                iRate += 1;
                break;

            default:
                AssertMsgFailed(("#%d: %d\n", i, pFilter->aFields[i].enmMatch));
                return -1;
        }
    }

    /* iRate is the range 0..2*cFields - recalc to percent. */
dprintf(("filter match: iRate=%d", iRate));
    return iRate == 2 * RT_ELEMENTS(pFilter->aFields)
        ? 100
        : (iRate * 100) / (2 * RT_ELEMENTS(pFilter->aFields));
}


/**
 * Match a filter against a USBDEVICE.
 *
 * @returns true if they match, false if not.
 *
 * @param   pFilter     The filter to match with.
 * @param   pDevice     The device to match.
 *
 * @remark  Both the filter and the device are ASSUMED to be valid because
 *          we don't wish to waste any time in this function.
 */
USBLIB_DECL(bool) USBFilterMatchDevice(PCUSBFILTER pFilter, PUSBDEVICE pDevice)
{
    for (unsigned i = 0; i < RT_ELEMENTS(pFilter->aFields); i++)
    {
        switch (pFilter->aFields[i].enmMatch)
        {
            case USBFILTERMATCH_IGNORE:
                break;

            case USBFILTERMATCH_PRESENT:
            {
                const char *psz;
                switch (i)
                {
                    case USBFILTERIDX_MANUFACTURER_STR:     psz = pDevice->pszManufacturer; break;
                    case USBFILTERIDX_PRODUCT_STR:          psz = pDevice->pszProduct; break;
                    case USBFILTERIDX_SERIAL_NUMBER_STR:    psz = pDevice->pszSerialNumber; break;
                    default:                                psz = ""; break;
                }
                if (!psz)
                    return false;
                break;
            }

            case USBFILTERMATCH_NUM_EXACT:
            case USBFILTERMATCH_NUM_EXACT_NP:
            case USBFILTERMATCH_NUM_EXPRESSION:
            case USBFILTERMATCH_NUM_EXPRESSION_NP:
            {
                uint16_t u16Value;
                switch (i)
                {
                    case USBFILTERIDX_VENDOR_ID:        u16Value = pDevice->idVendor; break;
                    case USBFILTERIDX_PRODUCT_ID:       u16Value = pDevice->idProduct; break;
                    case USBFILTERIDX_DEVICE:           u16Value = pDevice->bcdDevice; break;
                    case USBFILTERIDX_DEVICE_CLASS:     u16Value = pDevice->bDeviceClass; break;
                    case USBFILTERIDX_DEVICE_SUB_CLASS: u16Value = pDevice->bDeviceSubClass; break;
                    case USBFILTERIDX_DEVICE_PROTOCOL:  u16Value = pDevice->bDeviceProtocol; break;
                    case USBFILTERIDX_BUS:              u16Value = pDevice->bBus; break;
                    case USBFILTERIDX_PORT:             u16Value = pDevice->bPort; break;
                    default:                            u16Value = UINT16_MAX; break;

                }
                switch (pFilter->aFields[i].enmMatch)
                {
                    case USBFILTERMATCH_NUM_EXACT:
                    case USBFILTERMATCH_NUM_EXACT_NP:
                        if (pFilter->aFields[i].u16Value != u16Value)
                            return false;
                        break;
                    case USBFILTERMATCH_NUM_EXPRESSION:
                    case USBFILTERMATCH_NUM_EXPRESSION_NP:
                        if (!usbfilterMatchNumExpression(usbfilterGetString(pFilter, (USBFILTERIDX)i), u16Value))
                            return false;
                        break;
                }
                break;
            }

            case USBFILTERMATCH_STR_EXACT:
            case USBFILTERMATCH_STR_EXACT_NP:
            case USBFILTERMATCH_STR_PATTERN:
            case USBFILTERMATCH_STR_PATTERN_NP:
            {
                const char *psz;
                switch (i)
                {
                    case USBFILTERIDX_MANUFACTURER_STR:     psz = pDevice->pszManufacturer; break;
                    case USBFILTERIDX_PRODUCT_STR:          psz = pDevice->pszProduct; break;
                    case USBFILTERIDX_SERIAL_NUMBER_STR:    psz = pDevice->pszSerialNumber; break;
                    default:                                psz = NULL; break;
                }
                switch (pFilter->aFields[i].enmMatch)
                {
                    case USBFILTERMATCH_STR_EXACT:
                        if (    !psz
                            ||  strcmp(usbfilterGetString(pFilter, (USBFILTERIDX)i), psz))
                            return false;
                        break;

                    case USBFILTERMATCH_STR_EXACT_NP:
                        if (    psz
                            &&  strcmp(usbfilterGetString(pFilter, (USBFILTERIDX)i), psz))
                            return false;
                        break;

                    case USBFILTERMATCH_STR_PATTERN:
                        if (    !psz
                            ||  !usbfilterMatchStringPattern(usbfilterGetString(pFilter, (USBFILTERIDX)i), psz))
                            return false;
                        break;

                    case USBFILTERMATCH_STR_PATTERN_NP:
                        if (    psz
                            &&  !usbfilterMatchStringPattern(usbfilterGetString(pFilter, (USBFILTERIDX)i), psz))
                            return false;
                        break;
                }
                break;
            }

            default:
                AssertMsgFailed(("#%d: %d\n", i, pFilter->aFields[i].enmMatch));
                return false;
        }
    }

    return true;
}


/**
 * Checks if the two filters are identical.
 *
 * @returns true if the are identical, false if they aren't.
 * @param   pFilter     The first filter.
 * @param   pFilter2    The second filter.
 */
USBLIB_DECL(bool) USBFilterIsIdentical(PCUSBFILTER pFilter, PCUSBFILTER pFilter2)
{
    /* Lazy works here because we're darn strict with zero padding and such elsewhere. */
    return memcmp(pFilter, pFilter2, sizeof(*pFilter)) == 0;
}



/**
 * Sets the filter type.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_PARAMETER if the filter type is invalid.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter         The filter.
 * @param   enmType         The new filter type.
 */
USBLIB_DECL(int) USBFilterSetFilterType(PUSBFILTER pFilter, USBFILTERTYPE enmType)
{
    AssertReturn(pFilter->u32Magic == USBFILTER_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(enmType > USBFILTERTYPE_INVALID && enmType < USBFILTERTYPE_END, VERR_INVALID_PARAMETER);

    pFilter->enmType = enmType;
    return VINF_SUCCESS;
}


/**
 * Replaces the string value of a field.
 *
 * This will remove any existing string value current held by the field from the
 * string table and then attempt to add the new value. This function can be used
 * to delete any assigned string before changing the type to numeric by passing
 * in an empty string. This works because the first byte in the string table is
 * reserved for the empty (NULL) string.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the string table is full.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx isn't valid.
 * @retval  VERR_INVALID_POINTER if pszString isn't valid.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The field index.
 * @param   pszString       The string to add.
 * @param   fPurge          Purge invalid UTF-8 encoding and control characters
 *                          before setting it.
 */
static int usbfilterSetString(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx, const char *pszString, bool fPurge)
{
    /*
     * Validate input.
     */
    AssertReturn(pFilter->u32Magic == USBFILTER_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn((unsigned)enmFieldIdx < (unsigned)USBFILTERIDX_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszString, VERR_INVALID_POINTER);

    Assert(pFilter->offCurEnd < sizeof(pFilter->achStrTab));
    Assert(pFilter->achStrTab[pFilter->offCurEnd] == '\0');

    /*
     * Remove old string value if any.
     */
    if (    USBFilterIsMethodUsingStringValue((USBFILTERMATCH)pFilter->aFields[enmFieldIdx].enmMatch)
        &&  pFilter->aFields[enmFieldIdx].u16Value != 0)
    {
        uint32_t off = pFilter->aFields[enmFieldIdx].u16Value;
        pFilter->aFields[enmFieldIdx].u16Value = 0;     /* Assign it to the NULL string. */

        unsigned cchShift = (unsigned)strlen(&pFilter->achStrTab[off]) + 1;
        ssize_t cchToMove = (pFilter->offCurEnd + 1) - (off + cchShift);
        Assert(cchToMove >= 0);
        if (cchToMove > 0)
        {
            /* We're not last - must shift the strings. */
            memmove(&pFilter->achStrTab[off], &pFilter->achStrTab[off + cchShift], cchToMove);
            for (unsigned i = 0; i < RT_ELEMENTS(pFilter->aFields); i++)
                if (    pFilter->aFields[i].u16Value >= off
                    &&  USBFilterIsMethodUsingStringValue((USBFILTERMATCH)pFilter->aFields[i].enmMatch))
                    pFilter->aFields[i].u16Value -= cchShift;
        }
        pFilter->offCurEnd -= cchShift;
        Assert(pFilter->offCurEnd < sizeof(pFilter->achStrTab));
        Assert(pFilter->offCurEnd + cchShift <= sizeof(pFilter->achStrTab));

        /* zero the unused string table (to allow lazyness/strictness elsewhere). */
        memset(&pFilter->achStrTab[pFilter->offCurEnd], '\0', cchShift);
    }

    /*
     * Make a special case for the empty string.
     * (This also makes the delete logical above work correctly for the last string.)
     */
    if (!*pszString)
        pFilter->aFields[enmFieldIdx].u16Value = 0;
    else
    {
        size_t cch = strlen(pszString);
        if (pFilter->offCurEnd + cch + 2 > sizeof(pFilter->achStrTab))
            return VERR_BUFFER_OVERFLOW;

        pFilter->aFields[enmFieldIdx].u16Value = pFilter->offCurEnd + 1;
        memcpy(&pFilter->achStrTab[pFilter->offCurEnd + 1], pszString, cch + 1);
        if (fPurge)
            cch = USBLibPurgeEncoding(&pFilter->achStrTab[pFilter->offCurEnd + 1]);
        pFilter->offCurEnd += (uint32_t)cch + 1;
    }

    return VINF_SUCCESS;
}

/**
 * Wrapper around usbfilterSetString() that deletes any string value
 * currently assigned to a field.
 *
 * Upon successful return the field contains a null string, nothing or a number.
 *
 * This function will validate the field index if there isn't any string
 * value to delete, thus preventing any extra validating of the index.
 *
 * @returns VBox status code. See usbfilterSetString.
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The index of the field which string value should be deleted.
 */
static int usbfilterDeleteAnyStringValue(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx)
{
    int rc = VINF_SUCCESS;
    if (    USBFilterIsMethodUsingStringValue((USBFILTERMATCH)pFilter->aFields[enmFieldIdx].enmMatch)
        &&  pFilter->aFields[enmFieldIdx].u16Value != 0)
        rc = usbfilterSetString(pFilter, enmFieldIdx, "", false /*fPurge*/);
    else if ((unsigned)enmFieldIdx >= (unsigned)USBFILTERIDX_END)
        rc = VERR_INVALID_PARAMETER;
    return rc;
}


/**
 * Sets a field to always match (ignore whatever is thrown at it).
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx isn't valid.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter             The filter.
 * @param   enmFieldIdx         The field index. This must be a string field.
 */
USBLIB_DECL(int) USBFilterSetIgnore(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx)
{
    int rc = usbfilterDeleteAnyStringValue(pFilter, enmFieldIdx);
    if (RT_SUCCESS(rc))
    {
        pFilter->aFields[enmFieldIdx].enmMatch = USBFILTERMATCH_IGNORE;
        pFilter->aFields[enmFieldIdx].u16Value = 0;
    }
    return rc;
}


/**
 * Sets a field to match on device field present only.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx isn't valid.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter             The filter.
 * @param   enmFieldIdx         The field index. This must be a string field.
 */
USBLIB_DECL(int) USBFilterSetPresentOnly(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx)
{
    int rc = usbfilterDeleteAnyStringValue(pFilter, enmFieldIdx);
    if (RT_SUCCESS(rc))
    {
        pFilter->aFields[enmFieldIdx].enmMatch = USBFILTERMATCH_PRESENT;
        pFilter->aFields[enmFieldIdx].u16Value = 0;
    }
    return rc;
}


/**
 * Sets a field to exactly match a number.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx isn't valid.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter             The filter.
 * @param   enmFieldIdx         The field index. This must be a string field.
 * @param   u16Value            The string pattern.
 * @param   fMustBePresent      If set, a non-present field on the device will result in a mismatch.
 *                              If clear, a non-present field on the device will match.
 */
USBLIB_DECL(int) USBFilterSetNumExact(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx, uint16_t u16Value, bool fMustBePresent)
{
    int rc = USBFilterIsNumericField(enmFieldIdx) ? VINF_SUCCESS : VERR_INVALID_PARAMETER;
    if (RT_SUCCESS(rc))
    {
        rc = usbfilterDeleteAnyStringValue(pFilter, enmFieldIdx);
        if (RT_SUCCESS(rc))
        {
            pFilter->aFields[enmFieldIdx].u16Value = u16Value;
            pFilter->aFields[enmFieldIdx].enmMatch = fMustBePresent ? USBFILTERMATCH_NUM_EXACT : USBFILTERMATCH_NUM_EXACT_NP;
        }
    }

    return rc;
}


/**
 * Sets a field to match a numeric expression.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the string table is full.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx or the numeric expression aren't valid.
 * @retval  VERR_INVALID_POINTER if pszExpression isn't a valid pointer.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter             The filter.
 * @param   enmFieldIdx         The field index. This must be a string field.
 * @param   pszExpression       The numeric expression.
 * @param   fMustBePresent      If set, a non-present field on the device will result in a mismatch.
 *                              If clear, a non-present field on the device will match.
 */
USBLIB_DECL(int) USBFilterSetNumExpression(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx, const char *pszExpression, bool fMustBePresent)
{
    int rc = USBFilterIsNumericField(enmFieldIdx) ? VINF_SUCCESS : VERR_INVALID_PARAMETER;
    if (RT_SUCCESS(rc))
    {
        /* Strip leading spaces and empty sub expressions (||). */
        while (*pszExpression && (RT_C_IS_BLANK(*pszExpression) || *pszExpression == '|'))
            pszExpression++;

        rc = usbfilterValidateNumExpression(pszExpression);
        if (RT_SUCCESS(rc))
        {
            /* We could optimize the expression further (stripping spaces, convert numbers),
               but it's more work than what it's worth and it could upset some users. */
            rc = usbfilterSetString(pFilter, enmFieldIdx, pszExpression, false /*fPurge*/);
            if (RT_SUCCESS(rc))
                pFilter->aFields[enmFieldIdx].enmMatch = fMustBePresent ? USBFILTERMATCH_NUM_EXPRESSION : USBFILTERMATCH_NUM_EXPRESSION_NP;
            else if (rc == VERR_NO_DIGITS)
                rc = VERR_INVALID_PARAMETER;
        }
    }
    return rc;
}


/**
 * Sets a field to exactly match a string.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the string table is full.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx isn't valid.
 * @retval  VERR_INVALID_POINTER if pszPattern isn't a valid pointer.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter             The filter.
 * @param   enmFieldIdx         The field index. This must be a string field.
 * @param   pszValue            The string value.
 * @param   fMustBePresent      If set, a non-present field on the device will result in a mismatch.
 *                              If clear, a non-present field on the device will match.
 * @param   fPurge              Purge invalid UTF-8 encoding and control
 *                              characters before setting it.
 */
USBLIB_DECL(int) USBFilterSetStringExact(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx, const char *pszValue,
                                         bool fMustBePresent, bool fPurge)
{
    int rc = USBFilterIsStringField(enmFieldIdx) ? VINF_SUCCESS : VERR_INVALID_PARAMETER;
    if (RT_SUCCESS(rc))
    {
        rc = usbfilterSetString(pFilter, enmFieldIdx, pszValue, fPurge);
        if (RT_SUCCESS(rc))
            pFilter->aFields[enmFieldIdx].enmMatch = fMustBePresent ? USBFILTERMATCH_STR_EXACT : USBFILTERMATCH_STR_EXACT_NP;
    }
    return rc;
}


/**
 * Sets a field to match a string pattern.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the string table is full.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx or pattern aren't valid.
 * @retval  VERR_INVALID_POINTER if pszPattern isn't a valid pointer.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter             The filter.
 * @param   enmFieldIdx         The field index. This must be a string field.
 * @param   pszPattern          The string pattern.
 * @param   fMustBePresent      If set, a non-present field on the device will result in a mismatch.
 *                              If clear, a non-present field on the device will match.
 */
USBLIB_DECL(int) USBFilterSetStringPattern(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx, const char *pszPattern, bool fMustBePresent)
{
    int rc = USBFilterIsStringField(enmFieldIdx) ? VINF_SUCCESS : VERR_INVALID_PARAMETER;
    if (RT_SUCCESS(rc))
    {
        rc = usbfilterValidateStringPattern(pszPattern);
        if (RT_SUCCESS(rc))
        {
            rc = usbfilterSetString(pFilter, enmFieldIdx, pszPattern, false /*fPurge*/);
            if (RT_SUCCESS(rc))
                pFilter->aFields[enmFieldIdx].enmMatch = fMustBePresent ? USBFILTERMATCH_STR_PATTERN : USBFILTERMATCH_STR_PATTERN_NP;
        }
    }
    return rc;
}


/**
 * Sets the must-be-present part of a field.
 *
 * This only works on field which already has matching criteria. This means
 * that field marked 'ignore' will not be processed and will result in a
 * warning status code.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VWRN_INVALID_PARAMETER if the field is marked 'ignore'. No assertions.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx or pattern aren't valid.
 * @retval  VERR_INVALID_POINTER if pszPattern isn't a valid pointer.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter             The filter.
 * @param   enmFieldIdx         The field index.
 * @param   fMustBePresent      If set, a non-present field on the device will result in a mismatch.
 *                              If clear, a non-present field on the device will match.
 */
USBLIB_DECL(int) USBFilterSetMustBePresent(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx, bool fMustBePresent)
{
    AssertPtrReturn(pFilter, VERR_INVALID_POINTER);
    AssertReturn(pFilter->u32Magic == USBFILTER_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn((unsigned)enmFieldIdx < (unsigned)USBFILTERIDX_END, VERR_INVALID_PARAMETER);

    USBFILTERMATCH enmMatch = (USBFILTERMATCH)pFilter->aFields[enmFieldIdx].enmMatch;
    if (fMustBePresent)
    {
        switch (enmMatch)
        {
            case USBFILTERMATCH_IGNORE:
                return VWRN_INVALID_PARAMETER;

            case USBFILTERMATCH_PRESENT:
            case USBFILTERMATCH_NUM_EXACT:
            case USBFILTERMATCH_NUM_EXPRESSION:
            case USBFILTERMATCH_STR_EXACT:
            case USBFILTERMATCH_STR_PATTERN:
                break;

            case USBFILTERMATCH_NUM_EXACT_NP:
                enmMatch = USBFILTERMATCH_NUM_EXACT;
                break;
            case USBFILTERMATCH_NUM_EXPRESSION_NP:
                enmMatch = USBFILTERMATCH_NUM_EXPRESSION;
                break;
            case USBFILTERMATCH_STR_EXACT_NP:
                enmMatch = USBFILTERMATCH_STR_EXACT;
                break;
            case USBFILTERMATCH_STR_PATTERN_NP:
                enmMatch = USBFILTERMATCH_STR_PATTERN;
                break;
            default:
                AssertMsgFailedReturn(("%p: enmFieldIdx=%d enmMatch=%d\n", pFilter, enmFieldIdx, enmMatch), VERR_INVALID_MAGIC);
        }
    }
    else
    {
        switch (enmMatch)
        {
            case USBFILTERMATCH_IGNORE:
                return VWRN_INVALID_PARAMETER;

            case USBFILTERMATCH_NUM_EXACT_NP:
            case USBFILTERMATCH_STR_PATTERN_NP:
            case USBFILTERMATCH_STR_EXACT_NP:
            case USBFILTERMATCH_NUM_EXPRESSION_NP:
                break;

            case USBFILTERMATCH_PRESENT:
                enmMatch = USBFILTERMATCH_IGNORE;
                break;
            case USBFILTERMATCH_NUM_EXACT:
                enmMatch = USBFILTERMATCH_NUM_EXACT_NP;
                break;
            case USBFILTERMATCH_NUM_EXPRESSION:
                enmMatch = USBFILTERMATCH_NUM_EXPRESSION_NP;
                break;
            case USBFILTERMATCH_STR_EXACT:
                enmMatch = USBFILTERMATCH_STR_EXACT_NP;
                break;
            case USBFILTERMATCH_STR_PATTERN:
                enmMatch = USBFILTERMATCH_STR_PATTERN_NP;
                break;

            default:
                AssertMsgFailedReturn(("%p: enmFieldIdx=%d enmMatch=%d\n", pFilter, enmFieldIdx, enmMatch), VERR_INVALID_MAGIC);
        }
    }

    pFilter->aFields[enmFieldIdx].enmMatch = enmMatch;
    return VINF_SUCCESS;
}


/**
 * Gets the filter type.
 *
 * @returns The filter type.
 *          USBFILTERTYPE_INVALID if the filter is invalid.
 * @param   pFilter         The filter.
 */
USBLIB_DECL(USBFILTERTYPE) USBFilterGetFilterType(PCUSBFILTER pFilter)
{
    AssertReturn(pFilter->u32Magic == USBFILTER_MAGIC, USBFILTERTYPE_INVALID);
    return pFilter->enmType;
}


/**
 * Gets the matching method for a field.
 *
 * @returns The matching method on success, UBFILTERMATCH_INVALID on invalid field index.
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The field index.
 */
USBLIB_DECL(USBFILTERMATCH) USBFilterGetMatchingMethod(PCUSBFILTER pFilter, USBFILTERIDX enmFieldIdx)
{
    if (    pFilter->u32Magic == USBFILTER_MAGIC
        &&  (unsigned)enmFieldIdx < (unsigned)USBFILTERIDX_END)
        return (USBFILTERMATCH)pFilter->aFields[enmFieldIdx].enmMatch;
    return USBFILTERMATCH_INVALID;
}


/**
 * Gets the numeric value of a field.
 *
 * The field must contain a number, we're not doing any conversions for you.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx isn't valid or if the field doesn't contain a number.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The field index.
 * @param   pu16Value       Where to store the value.
 */
USBLIB_DECL(int) USBFilterQueryNum(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx, uint16_t *pu16Value)
{
    AssertReturn(pFilter->u32Magic == USBFILTER_MAGIC, VERR_INVALID_MAGIC);
    int iValue = usbfilterGetNum(pFilter, enmFieldIdx);
    if (iValue == -1)
        return VERR_INVALID_PARAMETER;
    *pu16Value = (uint16_t)iValue;
    return VINF_SUCCESS;
}


/**
 * Gets the numeric value of a field.
 *
 * The field must contain a number, we're not doing any conversions for you.
 *
 * @returns The field value on success, -1 on failure (invalid input / not numeric).
 *
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The field index.
 */
USBLIB_DECL(int) USBFilterGetNum(PCUSBFILTER pFilter, USBFILTERIDX enmFieldIdx)
{
    AssertReturn(pFilter->u32Magic == USBFILTER_MAGIC, -1);
    return usbfilterGetNum(pFilter, enmFieldIdx);
}


/**
 * Gets the string value of a field.
 *
 * The field must contain a string, we're not doing any conversions for you.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the buffer isn't sufficient to hold the string. The buffer
 *          will be filled with as much of the string that'll fit.
 * @retval  VERR_INVALID_PARAMETER if the enmFieldIdx isn't valid or if the field doesn't contain a string.
 * @retval  VERR_INVALID_MAGIC if pFilter is invalid.
 *
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The field index.
 * @param   pszBuf          Where to store the string.
 * @param   cchBuf          The size of the buffer.
 */
USBLIB_DECL(int) USBFilterQueryString(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx, char *pszBuf, size_t cchBuf)
{
    AssertReturn(pFilter->u32Magic == USBFILTER_MAGIC, VERR_INVALID_MAGIC);

    const char *psz = usbfilterGetString(pFilter, enmFieldIdx);
    if (RT_UNLIKELY(!psz))
        return VERR_INVALID_PARAMETER;

    int rc = VINF_SUCCESS;
    size_t cch = strlen(psz);
    if (cch < cchBuf)
        memcpy(pszBuf, psz, cch + 1);
    else
    {
        rc = VERR_BUFFER_OVERFLOW;
        if (cchBuf)
        {
            memcpy(pszBuf, psz, cchBuf - 1);
            pszBuf[cchBuf - 1] = '\0';
        }
    }

    return rc;
}


/**
 * Gets the string table entry for a field.
 *
 * @returns Pointer to the string. (readonly!)
 *
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The field index.
 */
USBLIB_DECL(const char *) USBFilterGetString(PCUSBFILTER pFilter, USBFILTERIDX enmFieldIdx)
{
    AssertReturn(pFilter->u32Magic == USBFILTER_MAGIC, NULL);

    const char *psz = usbfilterGetString(pFilter, enmFieldIdx);
    if (RT_UNLIKELY(!psz))
        return NULL;
    return psz;
}


/**
 * Gets the string length of a field containing a string.
 *
 * @returns String length on success, -1 on failure (not a string, bad filter).
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The field index.
 */
USBLIB_DECL(ssize_t) USBFilterGetStringLen(PCUSBFILTER pFilter, USBFILTERIDX enmFieldIdx)
{
    if (RT_LIKELY(pFilter->u32Magic == USBFILTER_MAGIC))
    {
        const char *psz = usbfilterGetString(pFilter, enmFieldIdx);
        if (RT_LIKELY(psz))
            return strlen(psz);
    }
    return -1;
}


/**
 * Check if any of the fields are set to something substatial.
 *
 * Consider the fileter a wildcard if this returns false.
 *
 * @returns true / false.
 * @param   pFilter         The filter.
 */
USBLIB_DECL(bool) USBFilterHasAnySubstatialCriteria(PCUSBFILTER pFilter)
{
    AssertReturn(pFilter->u32Magic == USBFILTER_MAGIC, false);

    for (unsigned i = 0; i < RT_ELEMENTS(pFilter->aFields); i++)
    {
        switch (pFilter->aFields[i].enmMatch)
        {
            case USBFILTERMATCH_IGNORE:
            case USBFILTERMATCH_PRESENT:
                break;

            case USBFILTERMATCH_NUM_EXACT:
            case USBFILTERMATCH_NUM_EXACT_NP:
            case USBFILTERMATCH_STR_EXACT:
            case USBFILTERMATCH_STR_EXACT_NP:
                return true;

            case USBFILTERMATCH_NUM_EXPRESSION:
            case USBFILTERMATCH_NUM_EXPRESSION_NP:
            {
                const char *psz = usbfilterGetString(pFilter, (USBFILTERIDX)i);
                if (psz)
                {
                    while (*psz && (*psz == '|' || RT_C_IS_BLANK(*psz)))
                        psz++;
                    if (*psz)
                        return true;
                }
                break;
            }

            case USBFILTERMATCH_STR_PATTERN:
            case USBFILTERMATCH_STR_PATTERN_NP:
            {
                const char *psz = usbfilterGetString(pFilter, (USBFILTERIDX)i);
                if (psz)
                {
                    while (*psz && (*psz == '*' || *psz == '?'))
                        psz++;
                    if (*psz)
                        return true;
                }
                break;
            }
        }
    }

    return false;
}



/**
 * Checks whether the specified field is a numeric field or not.
 *
 * @returns true / false.
 * @param   enmFieldIdx     The field index.
 */
USBLIB_DECL(bool) USBFilterIsNumericField(USBFILTERIDX enmFieldIdx)
{
    switch (enmFieldIdx)
    {
        case USBFILTERIDX_VENDOR_ID:
        case USBFILTERIDX_PRODUCT_ID:
        case USBFILTERIDX_DEVICE:
        case USBFILTERIDX_DEVICE_CLASS:
        case USBFILTERIDX_DEVICE_SUB_CLASS:
        case USBFILTERIDX_DEVICE_PROTOCOL:
        case USBFILTERIDX_BUS:
        case USBFILTERIDX_PORT:
            return true;

        default:
            AssertMsgFailed(("%d\n", enmFieldIdx));
            RT_FALL_THRU();
        case USBFILTERIDX_MANUFACTURER_STR:
        case USBFILTERIDX_PRODUCT_STR:
        case USBFILTERIDX_SERIAL_NUMBER_STR:
            return false;
    }
}


/**
 * Checks whether the specified field is a string field or not.
 *
 * @returns true / false.
 * @param   enmFieldIdx     The field index.
 */
USBLIB_DECL(bool) USBFilterIsStringField(USBFILTERIDX enmFieldIdx)
{
    switch (enmFieldIdx)
    {
        default:
            AssertMsgFailed(("%d\n", enmFieldIdx));
            RT_FALL_THRU();
        case USBFILTERIDX_VENDOR_ID:
        case USBFILTERIDX_PRODUCT_ID:
        case USBFILTERIDX_DEVICE:
        case USBFILTERIDX_DEVICE_CLASS:
        case USBFILTERIDX_DEVICE_SUB_CLASS:
        case USBFILTERIDX_DEVICE_PROTOCOL:
        case USBFILTERIDX_BUS:
        case USBFILTERIDX_PORT:
            return false;

        case USBFILTERIDX_MANUFACTURER_STR:
        case USBFILTERIDX_PRODUCT_STR:
        case USBFILTERIDX_SERIAL_NUMBER_STR:
            return true;
    }
}


/**
 * Checks whether the specified matching method uses a numeric value or not.
 *
 * @returns true / false.
 * @param   enmMatchingMethod   The matching method.
 */
USBLIB_DECL(bool) USBFilterIsMethodUsingNumericValue(USBFILTERMATCH enmMatchingMethod)
{
    switch (enmMatchingMethod)
    {
        default:
            AssertMsgFailed(("%d\n", enmMatchingMethod));
            RT_FALL_THRU();
        case USBFILTERMATCH_IGNORE:
        case USBFILTERMATCH_PRESENT:
        case USBFILTERMATCH_NUM_EXPRESSION:
        case USBFILTERMATCH_NUM_EXPRESSION_NP:
        case USBFILTERMATCH_STR_EXACT:
        case USBFILTERMATCH_STR_EXACT_NP:
        case USBFILTERMATCH_STR_PATTERN:
        case USBFILTERMATCH_STR_PATTERN_NP:
            return false;

        case USBFILTERMATCH_NUM_EXACT:
        case USBFILTERMATCH_NUM_EXACT_NP:
            return true;
    }
}


/**
 * Checks whether the specified matching method uses a string value or not.
 *
 * @returns true / false.
 * @param   enmMatchingMethod   The matching method.
 */
USBLIB_DECL(bool) USBFilterIsMethodUsingStringValue(USBFILTERMATCH enmMatchingMethod)
{
    switch (enmMatchingMethod)
    {
        default:
            AssertMsgFailed(("%d\n", enmMatchingMethod));
            RT_FALL_THRU();
        case USBFILTERMATCH_IGNORE:
        case USBFILTERMATCH_PRESENT:
        case USBFILTERMATCH_NUM_EXACT:
        case USBFILTERMATCH_NUM_EXACT_NP:
            return false;

        case USBFILTERMATCH_NUM_EXPRESSION:
        case USBFILTERMATCH_NUM_EXPRESSION_NP:
        case USBFILTERMATCH_STR_EXACT:
        case USBFILTERMATCH_STR_EXACT_NP:
        case USBFILTERMATCH_STR_PATTERN:
        case USBFILTERMATCH_STR_PATTERN_NP:
            return true;
    }
}


/**
 * Checks if a matching method is for numeric fields or not.
 *
 * @returns true / false.
 * @param   enmMatchingMethod   The matching method.
 */
USBLIB_DECL(bool) USBFilterIsMethodNumeric(USBFILTERMATCH enmMatchingMethod)
{
    return enmMatchingMethod >= USBFILTERMATCH_NUM_FIRST
        && enmMatchingMethod <= USBFILTERMATCH_NUM_LAST;
}

/**
 * Checks if a matching method is for string fields or not.
 *
 * @returns true / false.
 * @param   enmMatchingMethod   The matching method.
 */
USBLIB_DECL(bool) USBFilterIsMethodString(USBFILTERMATCH enmMatchingMethod)
{
    return enmMatchingMethod >= USBFILTERMATCH_STR_FIRST
        && enmMatchingMethod <= USBFILTERMATCH_STR_LAST;
}

