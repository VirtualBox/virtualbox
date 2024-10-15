/* $Id: bs3-cmn-StrFormatV.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3StrFormatV
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
#include "bs3kit-template-header.h"
#include <iprt/ctype.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define STR_F_CAPITAL           0x0001
#define STR_F_LEFT              0x0002
#define STR_F_ZEROPAD           0x0004
#define STR_F_SPECIAL           0x0008
#define STR_F_VALSIGNED         0x0010
#define STR_F_PLUS              0x0020
#define STR_F_BLANK             0x0040
#define STR_F_WIDTH             0x0080
#define STR_F_PRECISION         0x0100
#define STR_F_THOUSAND_SEP      0x0200
#define STR_F_NEGATIVE          0x0400 /**< Used to indicated '-' must be printed. */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Size of the temporary buffer. */
#define BS3FMT_TMP_SIZE    64

/**
 * BS3kit string format state.
 */
typedef struct BS3FMTSTATE
{
    /** The output function. */
    PFNBS3STRFORMATOUTPUT pfnOutput;
    /** User argument for pfnOutput. */
    void BS3_FAR   *pvUser;

    /** STR_F_XXX flags.   */
    unsigned        fFlags;
    /** The width when STR_F_WIDTH is specific. */
    int             cchWidth;
    /** The width when STR_F_PRECISION is specific. */
    int             cchPrecision;
    /** The number format base. */
    unsigned        uBase;
    /** Temporary buffer. */
    char            szTmp[BS3FMT_TMP_SIZE];
} BS3FMTSTATE;
/** Pointer to a BS3Kit string formatter state. */
typedef BS3FMTSTATE BS3_FAR *PBS3FMTSTATE;



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#if ARCH_BITS != 64
static size_t bs3StrFormatU32(PBS3FMTSTATE pState, uint32_t uValue);
#endif



/**
 * Formats a number string.
 *
 * @returns Number of chars printed.
 * @param   pState              The string formatter state.
 * @param   pszNumber           The formatted number string.
 * @param   cchNumber           The length of the number.
 */
static size_t bs3StrFormatNumberString(PBS3FMTSTATE pState, char const BS3_FAR *pszNumber, size_t cchNumber)
{
    /*
     * Calc the length of the core number with prefixes.
     */
    size_t cchActual = 0;
    size_t cchRet = cchNumber;

    /* Accunt for sign char. */
    cchRet += !!(pState->fFlags & (STR_F_NEGATIVE | STR_F_PLUS | STR_F_BLANK));

    /* Account for the hex prefix: '0x' or '0X' */
    if (pState->fFlags & STR_F_SPECIAL)
    {
        cchRet += 2;
        BS3_ASSERT(pState->uBase == 16);
    }

    /* Account for thousand separators (applied while printing). */
    if (pState->fFlags & STR_F_THOUSAND_SEP)
        cchRet += (cchNumber - 1) / (pState->uBase == 10 ? 3 : 8);

    /*
     * Do left blank padding.
     */
    if ((pState->fFlags & (STR_F_ZEROPAD | STR_F_LEFT | STR_F_WIDTH)) == STR_F_WIDTH)
        while (cchRet < pState->cchWidth)
        {
            cchActual += pState->pfnOutput(' ', pState->pvUser);
            cchRet++;
        }

    /*
     * Sign indicator / space.
     */
    if (pState->fFlags & (STR_F_NEGATIVE | STR_F_PLUS | STR_F_BLANK))
    {
        char ch;
        if (pState->fFlags & STR_F_NEGATIVE)
            ch = '-';
        else if (pState->fFlags & STR_F_PLUS)
            ch = '+';
        else
            ch = ' ';
        cchActual += pState->pfnOutput(ch, pState->pvUser);
    }

    /*
     * Hex prefix.
     */
    if (pState->fFlags & STR_F_SPECIAL)
    {
        cchActual += pState->pfnOutput('0', pState->pvUser);
        cchActual += pState->pfnOutput(!(pState->fFlags & STR_F_CAPITAL) ? 'x' : 'X', pState->pvUser);
    }

    /*
     * Zero padding.
     */
    if (pState->fFlags & STR_F_ZEROPAD)
        while (cchRet < pState->cchWidth)
        {
            cchActual += pState->pfnOutput('0', pState->pvUser);
            cchRet++;
        }

    /*
     * Output the number.
     */
    if (   !(pState->fFlags & STR_F_THOUSAND_SEP)
        || cchNumber < 4)
        while (cchNumber-- > 0)
            cchActual += pState->pfnOutput(*pszNumber++, pState->pvUser);
    else
    {
        char const      chSep    = pState->uBase == 10 ? ' ' : '\'';
        unsigned const  cchEvery = pState->uBase == 10 ? 3   : 8;
        unsigned        cchLeft  = --cchNumber % cchEvery;

        cchActual += pState->pfnOutput(*pszNumber++, pState->pvUser);
        while (cchNumber-- > 0)
        {
            if (cchLeft == 0)
            {
                cchActual += pState->pfnOutput(chSep, pState->pvUser);
                cchLeft = cchEvery;
            }
            cchLeft--;
            cchActual += pState->pfnOutput(*pszNumber++, pState->pvUser);
        }
    }

    /*
     * Do right blank padding.
     */
    if ((pState->fFlags & (STR_F_ZEROPAD | STR_F_LEFT | STR_F_WIDTH)) == (STR_F_WIDTH | STR_F_LEFT))
        while (cchRet < pState->cchWidth)
        {
            cchActual += pState->pfnOutput(' ', pState->pvUser);
            cchRet++;
        }

    return cchActual;
}


/**
 * Format a 64-bit number.
 *
 * @returns Number of characters.
 * @param   pState          The string formatter state.
 * @param   uValue          The value.
 */
static size_t bs3StrFormatU64(PBS3FMTSTATE pState, uint64_t uValue)
{
#if ARCH_BITS != 64
    /* Avoid 64-bit division by formatting 64-bit numbers as hex if they're higher than _4G. */
    if (pState->uBase == 10)
    {
        if (!(uValue >> 32)) /* uValue <= UINT32_MAX does not work, trouble with 64-bit compile time math! */
            return bs3StrFormatU32(pState, uValue);
        pState->fFlags |= STR_F_SPECIAL;
        pState->uBase   = 16;
    }
#endif

    {
        const char BS3_FAR *pachDigits = !(pState->fFlags & STR_F_CAPITAL) ? g_achBs3HexDigits : g_achBs3HexDigitsUpper;
        char       BS3_FAR *psz = &pState->szTmp[BS3FMT_TMP_SIZE];

        *--psz = '\0';
#if ARCH_BITS == 64
        if (pState->uBase == 10)
        {
            do
            {
                *--psz = pachDigits[uValue % 10];
                uValue /= 10;
            } while (uValue > 0);
        }
        else
#endif
        {
            BS3_ASSERT(pState->uBase == 16);
            do
            {
                *--psz = pachDigits[uValue & 0xf];
                uValue >>= 4;
            } while (uValue > 0);
        }
        return bs3StrFormatNumberString(pState, psz, &pState->szTmp[BS3FMT_TMP_SIZE - 1] - psz);
    }
}


/**
 * Format a 32-bit number.
 *
 * @returns Number of characters.
 * @param   pState          The string formatter state.
 * @param   uValue          The value.
 */
static size_t bs3StrFormatU32(PBS3FMTSTATE pState, uint32_t uValue)
{
#if ARCH_BITS < 64
    const char BS3_FAR *pachDigits = !(pState->fFlags & STR_F_CAPITAL) ? g_achBs3HexDigits : g_achBs3HexDigitsUpper;
    char       BS3_FAR *psz = &pState->szTmp[BS3FMT_TMP_SIZE];

    *--psz = '\0';
    if (pState->uBase == 10)
    {
        do
        {
            *--psz = pachDigits[uValue % 10];
            uValue /= 10;
        } while (uValue > 0);
    }
    else
    {
        BS3_ASSERT(pState->uBase == 16);
        do
        {
            *--psz = pachDigits[uValue & 0xf];
            uValue >>= 4;
        } while (uValue > 0);
    }
    return bs3StrFormatNumberString(pState, psz, &pState->szTmp[BS3FMT_TMP_SIZE - 1] - psz);

#else
    /* We've got native 64-bit division, save space. */
    return bs3StrFormatU64(pState, uValue);
#endif
}


#if ARCH_BITS == 16
/**
 * Format a 16-bit number.
 *
 * @returns Number of characters.
 * @param   pState          The string formatter state.
 * @param   uValue          The value.
 */
static size_t bs3StrFormatU16(PBS3FMTSTATE pState, uint16_t uValue)
{
    if (pState->uBase == 10)
    {
        const char BS3_FAR *pachDigits = !(pState->fFlags & STR_F_CAPITAL)
                                       ? g_achBs3HexDigits : g_achBs3HexDigitsUpper;
        char       BS3_FAR *psz = &pState->szTmp[BS3FMT_TMP_SIZE];

        *--psz = '\0';
        do
        {
            *--psz = pachDigits[uValue % 10];
            uValue /= 10;
        } while (uValue > 0);
        return bs3StrFormatNumberString(pState, psz, &pState->szTmp[BS3FMT_TMP_SIZE - 1] - psz);
    }

    /*
     * 32-bit shifting is reasonably cheap and inlined, so combine with 32-bit.
     */
    return bs3StrFormatU32(pState, uValue);
}
#endif


static size_t bs3StrFormatS64(PBS3FMTSTATE pState, int32_t iValue)
{
    if (iValue < 0)
    {
        iValue = -iValue;
        pState->fFlags |= STR_F_NEGATIVE;
    }
    return bs3StrFormatU64(pState, iValue);
}


static size_t bs3StrFormatS32(PBS3FMTSTATE pState, int32_t iValue)
{
    if (iValue < 0)
    {
        iValue = -iValue;
        pState->fFlags |= STR_F_NEGATIVE;
    }
    return bs3StrFormatU32(pState, iValue);
}


#if ARCH_BITS == 16
static size_t bs3StrFormatS16(PBS3FMTSTATE pState, int16_t iValue)
{
    if (iValue < 0)
    {
        iValue = -iValue;
        pState->fFlags |= STR_F_NEGATIVE;
    }
    return bs3StrFormatU16(pState, iValue);
}
#endif


#undef Bs3StrFormatV
BS3_CMN_DEF(size_t, Bs3StrFormatV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va,
                                   PFNBS3STRFORMATOUTPUT pfnOutput, void BS3_FAR *pvUser))
{
    BS3FMTSTATE State;
    size_t      cchRet = 0;
    char        ch;
#if ARCH_BITS == 16
    typedef int SIZE_CHECK_TYPE1[sizeof(va) == 4 && sizeof(va[0]) == 4];
#endif

    State.pfnOutput = pfnOutput;
    State.pvUser    = pvUser;

    while ((ch = *pszFormat++) != '\0')
    {
        char chArgSize;

        /*
         * Deal with plain chars.
         */
        if (ch != '%')
        {
            cchRet += State.pfnOutput(ch, State.pvUser);
            continue;
        }

        ch = *pszFormat++;
        if (ch == '%')
        {
            cchRet += State.pfnOutput(ch, State.pvUser);
            continue;
        }

        /*
         * Flags.
         */
        State.fFlags = 0;
        for (;;)
        {
            unsigned int fThis;
            switch (ch)
            {
                default:    fThis = 0;                  break;
                case '#':   fThis = STR_F_SPECIAL;      break;
                case '-':   fThis = STR_F_LEFT;         break;
                case '+':   fThis = STR_F_PLUS;         break;
                case ' ':   fThis = STR_F_BLANK;        break;
                case '0':   fThis = STR_F_ZEROPAD;      break;
                case '\'':  fThis = STR_F_THOUSAND_SEP; break;
            }
            if (!fThis)
                break;
            State.fFlags |= fThis;
            ch = *pszFormat++;
        }

        /*
         * Width.
         */
        State.cchWidth = 0;
        if (RT_C_IS_DIGIT(ch))
        {
            do
            {
                State.cchWidth *= 10;
                State.cchWidth += ch - '0';
                ch = *pszFormat++;
            } while (RT_C_IS_DIGIT(ch));
            State.fFlags |= STR_F_WIDTH;
        }
        else if (ch == '*')
        {
            State.cchWidth = va_arg(va, int);
            if (State.cchWidth < 0)
            {
                State.cchWidth = -State.cchWidth;
                State.fFlags |= STR_F_LEFT;
            }
            State.fFlags |= STR_F_WIDTH;
            ch = *pszFormat++;
        }

        /*
         * Precision
         */
        State.cchPrecision = 0;
        if (ch == '.')
        {
            ch = *pszFormat++;
            if (RT_C_IS_DIGIT(ch))
            {
                do
                {
                    State.cchPrecision *= 10;
                    State.cchPrecision += ch - '0';
                    ch = *pszFormat++;
                } while (RT_C_IS_DIGIT(ch));
                State.fFlags |= STR_F_PRECISION;
            }
            else if (ch == '*')
            {
                State.cchPrecision = va_arg(va, int);
                if (State.cchPrecision < 0)
                    State.cchPrecision = 0;
                State.fFlags |= STR_F_PRECISION;
                ch = *pszFormat++;
            }
        }

        /*
         * Argument size.
         */
        chArgSize = ch;
        switch (ch)
        {
            default:
                chArgSize = 0;
                break;

            case 'z':
            case 'L':
            case 'j':
            case 't':
                ch = *pszFormat++;
                break;

            case 'l':
                ch = *pszFormat++;
                if (ch == 'l')
                {
                    chArgSize = 'L';
                    ch = *pszFormat++;
                }
                break;

            case 'h':
                ch = *pszFormat++;
                if (ch == 'h')
                {
                    chArgSize = 'H';
                    ch = *pszFormat++;
                }
                break;
        }

        /*
         * The type.
         */
        switch (ch)
        {
            /*
             * Char
             */
            case 'c':
            {
                char ch = va_arg(va, int /*char*/);
                cchRet += State.pfnOutput(ch, State.pvUser);
                break;
            }

            /*
             * String.
             */
            case 's':
            {
                const char BS3_FAR *psz = va_arg(va, const char BS3_FAR *);
                size_t              cch;
                if (psz != NULL)
                    cch = Bs3StrNLen(psz, State.fFlags & STR_F_PRECISION ? RT_ABS(State.cchPrecision) : ~(size_t)0);
                else
                {
                    psz = "<NULL>";
                    cch = 6;
                }

                if ((State.fFlags & (STR_F_LEFT | STR_F_WIDTH)) == STR_F_WIDTH)
                    while (--State.cchWidth >= cch)
                        cchRet += State.pfnOutput(' ', State.pvUser);

                while (cch-- > 0)
                    cchRet += State.pfnOutput(*psz++, State.pvUser);

                if ((State.fFlags & (STR_F_LEFT | STR_F_WIDTH)) == (STR_F_LEFT | STR_F_WIDTH))
                    while (--State.cchWidth >= cch)
                        cchRet += State.pfnOutput(' ', State.pvUser);
                break;
            }

            /*
             * Signed integers.
             */
            case 'i':
            case 'd':
                State.fFlags &= ~STR_F_SPECIAL;
                State.fFlags |= STR_F_VALSIGNED;
                State.uBase   = 10;
                switch (chArgSize)
                {
                    case 0:
                    case 'h': /* signed short should be promoted to int or be the same as int */
                    case 'H': /* signed char should be promoted to int. */
                    {
                        signed int iValue = va_arg(va, signed int);
#if ARCH_BITS == 16
                        cchRet += bs3StrFormatS16(&State, iValue);
#else
                        cchRet += bs3StrFormatS32(&State, iValue);
#endif
                        break;
                    }
                    case 'l':
                    {
                        signed long lValue = va_arg(va, signed long);
                        if (sizeof(lValue) == 4)
                            cchRet += bs3StrFormatS32(&State, lValue);
                        else
                            cchRet += bs3StrFormatS64(&State, lValue);
                        break;
                    }
                    case 'L':
                    {
                        unsigned long long ullValue = va_arg(va, unsigned long long);
                        cchRet += bs3StrFormatS64(&State, ullValue);
                        break;
                    }
                }
                break;

            /*
             * Unsigned integers.
             */
            case 'X':
                State.fFlags |= STR_F_CAPITAL;
            case 'x':
            case 'u':
            {
                if (ch == 'u')
                {
                    State.uBase   = 10;
                    State.fFlags &= ~(STR_F_PLUS | STR_F_BLANK | STR_F_SPECIAL);
                }
                else
                {
                    State.uBase   = 16;
                    State.fFlags &= ~(STR_F_PLUS | STR_F_BLANK);
                }
                switch (chArgSize)
                {
                    case 0:
                    case 'h': /* unsigned short should be promoted to int or be the same as int */
                    case 'H': /* unsigned char should be promoted to int. */
                    {
                        unsigned int uValue = va_arg(va, unsigned int);
#if ARCH_BITS == 16
                        cchRet += bs3StrFormatU16(&State, uValue);
#else
                        cchRet += bs3StrFormatU32(&State, uValue);
#endif
                        break;
                    }
                    case 'l':
                    {
                        unsigned long ulValue = va_arg(va, unsigned long);
                        if (sizeof(ulValue) == 4)
                            cchRet += bs3StrFormatU32(&State, ulValue);
                        else
                            cchRet += bs3StrFormatU64(&State, ulValue);
                        break;
                    }
                    case 'L':
                    {
                        unsigned long long ullValue = va_arg(va, unsigned long long);
                        cchRet += bs3StrFormatU64(&State, ullValue);
                        break;
                    }
                }
                break;
            }

            /*
             * Our stuff.
             */
            case 'R':
            {
                ch = *pszFormat++;
                switch (ch)
                {
                    case 'I':
                        State.fFlags |= STR_F_VALSIGNED;
                        State.uBase  &= ~STR_F_SPECIAL;
                        State.uBase   = 10;
                        break;
                    case 'U':
                        State.fFlags &= ~(STR_F_PLUS | STR_F_BLANK | STR_F_SPECIAL);
                        State.uBase   = 10;
                        break;
                    case 'X':
                        State.fFlags &= ~(STR_F_PLUS | STR_F_BLANK);
                        State.uBase   = 16;
                        break;
                    case 'h':
                        ch = *pszFormat++;
                        if (ch == 'x')
                        {
                            /* Hex dumping. */
                            uint8_t const BS3_FAR *pbHex = va_arg(va, uint8_t const BS3_FAR *);
                            if (State.cchPrecision < 0)
                                State.cchPrecision = 16;
                            ch = *pszFormat++;
                            if (ch == 's' || ch == 'd')
                            {
                                /* %Rhxd is currently implemented as %Rhxs. */
                                while (State.cchPrecision-- > 0)
                                {
                                    uint8_t b = *pbHex++;
                                    State.pfnOutput(g_achBs3HexDigits[b >> 4], State.pvUser);
                                    State.pfnOutput(g_achBs3HexDigits[b & 0x0f], State.pvUser);
                                    if (State.cchPrecision)
                                        State.pfnOutput(' ', State.pvUser);
                                }
                            }
                        }
                        State.uBase   = 0;
                        break;
                    default:
                        State.uBase   = 0;
                        break;
                }
                if (State.uBase)
                {
                    ch = *pszFormat++;
                    switch (ch)
                    {
#if ARCH_BITS != 16
                        case '3':
                        case '1': /* Will an unsigned 16-bit value always be promoted
                                     to a 16-bit unsigned int. It certainly will be promoted to a 32-bit int. */
                            pszFormat++; /* Assumes (1)'6' or (3)'2' */
#else
                        case '1':
                            pszFormat++; /* Assumes (1)'6' */
#endif
                        case '8': /* An unsigned 8-bit value should be promoted to int, which is at least 16-bit. */
                        {
                            unsigned int uValue = va_arg(va, unsigned int);
#if ARCH_BITS == 16
                            cchRet += bs3StrFormatU16(&State, uValue);
#else
                            cchRet += bs3StrFormatU32(&State, uValue);
#endif
                            break;
                        }
#if ARCH_BITS == 16
                        case '3':
                        {
                            uint32_t uValue = va_arg(va, uint32_t);
                            pszFormat++;
                            cchRet += bs3StrFormatU32(&State, uValue);
                            break;
                        }
#endif
                        case '6':
                        {
                            uint64_t uValue = va_arg(va, uint64_t);
                            pszFormat++;
                            cchRet += bs3StrFormatU64(&State, uValue);
                            break;
                        }
                    }
                }
                break;
            }

            /*
             * Pointers.
             */
            case 'P':
                State.fFlags |= STR_F_CAPITAL;
                RT_FALL_THRU();
            case 'p':
            {
                void BS3_FAR *pv = va_arg(va, void BS3_FAR *);
                State.uBase   = 16;
                State.fFlags &= ~(STR_F_PLUS | STR_F_BLANK);
#if ARCH_BITS == 16
                State.fFlags |= STR_F_ZEROPAD;
                State.cchWidth = State.fFlags & STR_F_SPECIAL ? 6: 4;
                cchRet += bs3StrFormatU16(&State, BS3_FP_SEG(pv));
                cchRet += State.pfnOutput(':', State.pvUser);
                cchRet += bs3StrFormatU16(&State, BS3_FP_OFF(pv));
#elif ARCH_BITS == 32
                State.fFlags |= STR_F_SPECIAL | STR_F_ZEROPAD;
                State.cchWidth = 10;
                cchRet += bs3StrFormatU32(&State, (uintptr_t)pv);
#elif ARCH_BITS == 64
                State.fFlags |= STR_F_SPECIAL | STR_F_ZEROPAD | STR_F_THOUSAND_SEP;
                State.cchWidth = 19;
                cchRet += bs3StrFormatU64(&State, (uintptr_t)pv);
#else
# error "Undefined or invalid ARCH_BITS."
#endif
                break;
            }

        }
    }

    /*
     * Termination call.
     */
    cchRet += State.pfnOutput(0, State.pvUser);

    return cchRet;
}

