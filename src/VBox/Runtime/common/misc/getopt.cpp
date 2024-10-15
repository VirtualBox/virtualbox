/* $Id: getopt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Command Line Parsing
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
#include <iprt/cidr.h>
#include <iprt/net.h>                   /* must come before getopt.h */
#include <iprt/getopt.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/message.h>
#include <iprt/string.h>
#include <iprt/uni.h>
#include <iprt/uuid.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef IN_RT_STATIC /* We don't need full unicode case insensitive if we ASSUME basic latin only. */
# define RTUniCpToLower(a_uc)   ((RTUNICP)RT_C_TO_LOWER(a_uc))
# define RTUniCpToUpper(a_uc)   ((RTUNICP)RT_C_TO_UPPER(a_uc))
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Standard options that gets included unless RTGETOPTINIT_FLAGS_NO_STD_OPTS is
 * set.
 */
static RTGETOPTDEF const g_aStdOptions[] =
{
    {  "--help",        'h',    RTGETOPT_REQ_NOTHING },
    {  "-help",         'h',    RTGETOPT_REQ_NOTHING },
    {  "--version",     'V',    RTGETOPT_REQ_NOTHING },
    {  "-version",      'V',    RTGETOPT_REQ_NOTHING },
};
/** The index of --help in g_aStdOptions.  Used for some trickery.  */
#define RTGETOPT_STD_OPTIONS_HELP_IDX   0



RTDECL(int) RTGetOptInit(PRTGETOPTSTATE pState, int argc, char **argv,
                         PCRTGETOPTDEF paOptions, size_t cOptions,
                         int iFirst, uint32_t fFlags)
{
    AssertReturn(!(fFlags & ~(RTGETOPTINIT_FLAGS_OPTS_FIRST | RTGETOPTINIT_FLAGS_NO_STD_OPTS)), VERR_INVALID_PARAMETER);

    pState->argv         = argv;
    pState->argc         = argc;
    pState->paOptions    = paOptions;
    pState->cOptions     = cOptions;
    pState->iNext        = iFirst;
    pState->pszNextShort = NULL;
    pState->pDef         = NULL;
    pState->uIndex       = UINT32_MAX;
    pState->fFlags       = fFlags;
    pState->cNonOptions  = 0;

#ifdef RT_STRICT
    /* validate the options. */
    for (size_t i = 0; i < cOptions; i++)
    {
        Assert(!(paOptions[i].fFlags & ~RTGETOPT_VALID_MASK));
        Assert(   !(paOptions[i].fFlags & (RTGETOPT_FLAG_INDEX_DEF_MASK | RTGETOPT_FLAG_INDEX_DEF_DASH))
               || (paOptions[i].fFlags & RTGETOPT_FLAG_INDEX) );
        Assert(paOptions[i].iShort > 0);
        Assert(paOptions[i].iShort != VINF_GETOPT_NOT_OPTION);
        Assert(paOptions[i].iShort != '-');
        if (paOptions[i].fFlags & RTGETOPT_FLAG_ICASE)
        {
            const char   *psz = paOptions[i].pszLong;
            unsigned char ch;
            while ((ch = *psz++) != '\0')
                Assert(ch <= 0x7f); /* ASSUMPTION that we can use also RTStrICmpAscii and RTStrNICmpAscii for static builds
                                       and that we don't need to use RTStrGetCp on pszLong strings. */
        }
    }
#endif

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTGetOptInit);

#ifndef IPRT_GETOPT_WITHOUT_NETWORK_ADDRESSES

/**
 * Converts an stringified IPv4 address into the RTNETADDRIPV4 representation.
 *
 * @returns VINF_SUCCESS on success, VERR_GETOPT_INVALID_ARGUMENT_FORMAT on
 *          failure.
 *
 * @param   pszValue        The value to convert.
 * @param   pAddr           Where to store the result.
 */
static int rtgetoptConvertIPv4Addr(const char *pszValue, PRTNETADDRIPV4 pAddr)
{
    if (RT_FAILURE(RTNetStrToIPv4Addr(pszValue, pAddr)))
        return VERR_GETOPT_INVALID_ARGUMENT_FORMAT;
    return VINF_SUCCESS;
}


/**
 * Converts an stringified Ethernet MAC address into the RTMAC representation.
 *
 * @returns VINF_SUCCESS on success, VERR_GETOPT_INVALID_ARGUMENT_FORMAT on
 *          failure.
 *
 * @param   pszValue        The value to convert.
 * @param   pAddr           Where to store the result.
 */
static int rtgetoptConvertMacAddr(const char *pszValue, PRTMAC pAddr)
{

    int rc = RTNetStrToMacAddr(pszValue, pAddr);
    if (RT_FAILURE(rc))
        return VERR_GETOPT_INVALID_ARGUMENT_FORMAT;

    return VINF_SUCCESS;
}

#endif /* IPRT_GETOPT_WITHOUT_NETWORK_ADDRESSES */


/**
 * Checks if the given unicode codepoint is an alternative dash or not.
 *
 * ASSUMES caller checks for the ascii hypen-minus.
 *
 * @returns true / false.
 * @param   uc      The codepoint to check.
 */
DECLINLINE(bool) rtGetOptIsAlternativeDash(RTUNICP uc)
{
    return uc == 0x2011 /* non-breaking hypen */
        || uc == 0x2010 /* hypen */
        || uc == 0x2012 /* figure dash */
        || uc == 0x2013 /* en dash */
        || uc == 0x2014 /* em dash */
        || uc == 0x2212 /* minus sign */
        || uc == 0xfe58 /* small em dash */
        || uc == 0xfe63 /* small em hypen-minus */
        || uc == 0xff0d /* fullwidth hypen-minus */
        ;
}


/**
 * Checks if the given unicode codepoint is an any kind of dash.
 *
 * @returns true / false.
 * @param   uc      The codepoint to check.
 */
DECLINLINE(bool) rtGetOptIsDash(RTUNICP uc)
{
    return uc == (RTUNICP)'-' || rtGetOptIsAlternativeDash(uc);
}


/**
 * Compares a user specific argument with a RTGETOPTDEF long string, taking
 * RTGETOPT_FLAG_ICASE into account.
 *
 * This differs from strcmp/RTStrICmp in that it matches the minus-hyphen
 * (U+002D) codepoint in a RTGETOPTDEF string with non-breaking hyphen (U+2011)
 * and some others.  This allows copy & paste from manuals, documentation and
 * similar that needs to use the non-breaking variant for typographical reasons.
 *
 * @returns true if matches, false if not.
 * @param   pszUserArg          Ther user specified argument string.
 * @param   pszOption           The RTGETOPTDEF::pszLong string.
 * @param   fOptFlags           The RTGETOPTDEF::fFlags.
 * @param   cchMax              RT_STR_MAX if full string compare, otherwise
 *                              number of @a pszOption characters to compare.
 * @param   poffUserArgNext     Where to return offset of the first character
 *                              after the match in @a pszUserArg. Optional.
 */
static bool rtGetOptLongStrEquals(const char *pszUserArg, const char *pszOption, size_t cchMax, uint32_t fOptFlags,
                                  size_t *poffUserArgNext)
{
    const char * const pszUserArgStart = pszUserArg;
    while (cchMax-- > 0)
    {
        char const ch1 = *pszUserArg++;
        char const ch2 = *pszOption++;
        if (ch1 == ch2)
        {
            if (!ch1)
                break;
        }
        /* If the long option contains a dash, consider alternative unicode dashes
           like non-breaking and such. */
        else if (ch2 == '-')
        {
            if (!((unsigned char)ch1 & 0x80))
                return false;

            pszUserArg--;
            RTUNICP uc;
            int rc = RTStrGetCpEx(&pszUserArg, &uc);
            AssertRCReturn(rc, false);

            if (!rtGetOptIsAlternativeDash(uc))
                return false;
        }
        /* Do case folding if configured for this option. */
        else if (!(fOptFlags & RTGETOPT_FLAG_ICASE))
            return false;
        else
        {
            pszUserArg--;
            RTUNICP uc;
            int rc = RTStrGetCpEx(&pszUserArg, &uc);
            AssertRCReturn(rc, false);

            if (   RTUniCpToLower(uc) != (RTUNICP)RT_C_TO_LOWER(ch2)
                && RTUniCpToUpper(uc) != (RTUNICP)RT_C_TO_UPPER(ch2))
                return false;
        }
    }
    if (poffUserArgNext)
        *poffUserArgNext = (size_t)(pszUserArg - pszUserArgStart);
    return true;
}


/**
 * Skips the optional dash for options with the RTGETOPT_FLAG_INDEX_DEF_DASH
 * flag.
 */
static size_t rtGetOptSkipIndexDefDash(const char *pszOption, size_t cchLong)
{
    const char *pszNext = &pszOption[cchLong];
    RTUNICP     uc;
    int rc = RTStrGetCpEx(&pszNext, &uc);
    AssertRCReturn(rc, cchLong);

    /* given "--long" we match "--long-1" but not "--long-". */
    if (   rtGetOptIsDash(uc)
        && RT_C_IS_DIGIT(*pszNext))
        return (size_t)(pszNext - pszOption);

    return cchLong;
}


/**
 * Searches for a long option.
 *
 * @returns Pointer to a matching option.
 * @param   pszOption       The alleged long option.
 * @param   paOptions       Option array.
 * @param   cOptions        Number of items in the array.
 * @param   fFlags          Init flags.
 * @param   pcchMatch       Where to return the length of the matching option
 *                          section, including any RTGETOPT_FLAG_INDEX_DEF_DASH
 *                          dash, but _not_ the actual index or value separator.
 */
static PCRTGETOPTDEF rtGetOptSearchLong(const char *pszOption, PCRTGETOPTDEF paOptions, size_t cOptions, uint32_t fFlags,
                                        size_t *pcchMatch)
{
    PCRTGETOPTDEF pOpt = paOptions;
    while (cOptions-- > 0)
    {
        if (pOpt->pszLong)
        {
            uint32_t const fOptFlags = pOpt->fFlags;
            if ((fOptFlags & RTGETOPT_REQ_MASK) != RTGETOPT_REQ_NOTHING)
            {
                /*
                 * A value is required with the argument. We're trying to be
                 * understanding here and will permit any of the following:
                 *      --long12:value,  --long12=value, --long12 value,
                 *      --long:value,    --long=value,   --long value,
                 *
                 * If the option is index, then all trailing chars must be
                 * digits.  For error reporting reasons we also match where
                 * there is no index.
                 */
                size_t cchLong = strlen(pOpt->pszLong);
                if (rtGetOptLongStrEquals(pszOption, pOpt->pszLong, cchLong, fOptFlags, &cchLong))
                {
                    if (fOptFlags & RTGETOPT_FLAG_INDEX_DEF_DASH)
                        cchLong = rtGetOptSkipIndexDefDash(pszOption, cchLong);
                    size_t const cchMatch = cchLong;
                    if (fOptFlags & RTGETOPT_FLAG_INDEX)
                        while (RT_C_IS_DIGIT(pszOption[cchLong]))
                            cchLong++;
                    if (   pszOption[cchLong] == '\0'
                        || pszOption[cchLong] == ':'
                        || pszOption[cchLong] == '=')
                    {
                        *pcchMatch = cchMatch;
                        return pOpt;
                    }
                }
            }
            else if (fOptFlags & RTGETOPT_FLAG_INDEX)
            {
                /*
                 * The option takes an index but no value.
                 * As above, we also match where there is no index.
                 */
                size_t cchLong = strlen(pOpt->pszLong);
                if (rtGetOptLongStrEquals(pszOption, pOpt->pszLong, cchLong, fOptFlags, &cchLong))
                {
                    if (fOptFlags & RTGETOPT_FLAG_INDEX_DEF_DASH)
                        cchLong = rtGetOptSkipIndexDefDash(pszOption, cchLong);
                    size_t const cchMatch = cchLong;
                    while (RT_C_IS_DIGIT(pszOption[cchLong]))
                        cchLong++;
                    if (pszOption[cchLong] == '\0')
                    {
                        *pcchMatch = cchMatch;
                        return pOpt;
                    }
                }
            }
            else if (rtGetOptLongStrEquals(pszOption, pOpt->pszLong, RTSTR_MAX, fOptFlags, pcchMatch))
                return pOpt;
        }
        pOpt++;
    }

    if (!(fFlags & RTGETOPTINIT_FLAGS_NO_STD_OPTS))
        for (uint32_t i = 0; i < RT_ELEMENTS(g_aStdOptions); i++)
        {
            if (rtGetOptLongStrEquals(pszOption, g_aStdOptions[i].pszLong, RTSTR_MAX, g_aStdOptions[i].fFlags, pcchMatch))
                return &g_aStdOptions[i];
        }

    *pcchMatch = 0;
    return NULL;
}


/**
 * Searches for a matching short option.
 *
 * @returns Pointer to a matching option.
 * @param   chOption        The option char.
 * @param   paOptions       Option array.
 * @param   cOptions        Number of items in the array.
 * @param   fFlags          Init flags.
 */
static PCRTGETOPTDEF rtGetOptSearchShort(int chOption, PCRTGETOPTDEF paOptions, size_t cOptions, uint32_t fFlags)
{
    PCRTGETOPTDEF pOpt = paOptions;
    while (cOptions-- > 0)
    {
        if (pOpt->iShort == chOption)
            return pOpt;
        pOpt++;
    }

    if (!(fFlags & RTGETOPTINIT_FLAGS_NO_STD_OPTS))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(g_aStdOptions); i++)
            if (g_aStdOptions[i].iShort == chOption)
                return &g_aStdOptions[i];
        if (chOption == '?')
            return &g_aStdOptions[RTGETOPT_STD_OPTIONS_HELP_IDX];
    }
    return NULL;
}


/**
 * Value string -> Value union.
 *
 * @returns IPRT status code.
 * @param   fFlags              The value flags.
 * @param   pszValue            The value string.
 * @param   pValueUnion         Where to return the processed value.
 */
static int rtGetOptProcessValue(uint32_t fFlags, const char *pszValue, PRTGETOPTUNION pValueUnion)
{
    /*
     * Transform into a option value as requested.
     * If decimal conversion fails, we'll check for "0x<xdigit>" and
     * try a 16 based conversion. We will not interpret any of the
     * generic ints as octals.
     */
    uint32_t const fSwitchValue =  fFlags & (  RTGETOPT_REQ_MASK
                                             | RTGETOPT_FLAG_HEX
                                             | RTGETOPT_FLAG_DEC
                                             | RTGETOPT_FLAG_OCT);
    switch (fSwitchValue)
    {
        case RTGETOPT_REQ_STRING:
            pValueUnion->psz = pszValue;
            break;

        case RTGETOPT_REQ_BOOL:
        {
            static struct RTGETOPTUPPERLOWERBOOL
            {
                bool        f;
                const char *pszLower;
                const char *pszUpper;
                const char *pszMarkers; /**< characters with '+' are okay to stop after, '-' aren't.*/
            } const s_aKeywords[] =
            {
                { true,  "true",      "TRUE",       "+--+"     },
                { true,  "yes",       "YES",        "+-+"      },
                { true,  "enabled",   "ENABLED",    "++---++"  },
                { true,  "on",        "ON",         "-+"       },
                { true,  "1",         "1",          "+"        },
                { false, "false",     "FALSE",      "+---+"    },
                { false, "no",        "NO",         "++"       },
                { false, "disabled",  "DISABLED",   "+-+---++" },
                { false, "off",       "OFF",        "--+"      },
                { false, "0",         "0",          "+"        },
            };
            for (size_t i = 0; i < RT_ELEMENTS(s_aKeywords); i++)
                for (size_t off = 0;; off++)
                {
                    char const ch = pszValue[off];
                    if (!ch)
                    {
                        if (off > 0 && s_aKeywords[i].pszMarkers[off - 1] == '+')
                        {
                            pValueUnion->f = s_aKeywords[i].f;
                            return VINF_SUCCESS;
                        }
                        break;
                    }
                    if (   s_aKeywords[i].pszLower[off] != ch
                        && s_aKeywords[i].pszUpper[off] != ch)
                        break;
                }
            pValueUnion->psz = pszValue;
            return VERR_GETOPT_UNKNOWN_OPTION;
        }

        case RTGETOPT_REQ_BOOL_ONOFF:
            if (   (pszValue[0] == 'o' || pszValue[0] == 'O')
                && (pszValue[1] == 'n' || pszValue[1] == 'N')
                &&  pszValue[2] == '\0')
                pValueUnion->f = true;
            else if (   (pszValue[0] == 'o' || pszValue[0] == 'O')
                     && (pszValue[1] == 'f' || pszValue[1] == 'F')
                     && (pszValue[2] == 'f' || pszValue[2] == 'F')
                     &&  pszValue[3] == '\0')
                pValueUnion->f = false;
            else
            {
                pValueUnion->psz = pszValue;
                return VERR_GETOPT_UNKNOWN_OPTION;
            }
            break;

#define MY_INT_CASE(req, type, memb, convfn) \
            case req: \
            { \
                type Value; \
                if (    convfn(pszValue, 10, &Value) != VINF_SUCCESS \
                    &&  (   pszValue[0] != '0' \
                         || (pszValue[1] != 'x' && pszValue[1] != 'X') \
                         || !RT_C_IS_XDIGIT(pszValue[2]) \
                         || convfn(pszValue, 16, &Value) != VINF_SUCCESS ) ) \
                    return VERR_GETOPT_INVALID_ARGUMENT_FORMAT; \
                pValueUnion->memb = Value; \
                break; \
            }
#define MY_BASE_INT_CASE(req, type, memb, convfn, base) \
            case req: \
            { \
                type Value; \
                if (convfn(pszValue, base, &Value) != VINF_SUCCESS) \
                    return VERR_GETOPT_INVALID_ARGUMENT_FORMAT; \
                pValueUnion->memb = Value; \
                break; \
            }

        MY_INT_CASE(RTGETOPT_REQ_INT8,   int8_t,   i8,  RTStrToInt8Full)
        MY_INT_CASE(RTGETOPT_REQ_INT16,  int16_t,  i16, RTStrToInt16Full)
        MY_INT_CASE(RTGETOPT_REQ_INT32,  int32_t,  i32, RTStrToInt32Full)
        MY_INT_CASE(RTGETOPT_REQ_INT64,  int64_t,  i64, RTStrToInt64Full)
        MY_INT_CASE(RTGETOPT_REQ_UINT8,  uint8_t,  u8,  RTStrToUInt8Full)
        MY_INT_CASE(RTGETOPT_REQ_UINT16, uint16_t, u16, RTStrToUInt16Full)
        MY_INT_CASE(RTGETOPT_REQ_UINT32, uint32_t, u32, RTStrToUInt32Full)
        MY_INT_CASE(RTGETOPT_REQ_UINT64, uint64_t, u64, RTStrToUInt64Full)

        MY_BASE_INT_CASE(RTGETOPT_REQ_INT8   | RTGETOPT_FLAG_HEX, int8_t,   i8,  RTStrToInt8Full,   16)
        MY_BASE_INT_CASE(RTGETOPT_REQ_INT16  | RTGETOPT_FLAG_HEX, int16_t,  i16, RTStrToInt16Full,  16)
        MY_BASE_INT_CASE(RTGETOPT_REQ_INT32  | RTGETOPT_FLAG_HEX, int32_t,  i32, RTStrToInt32Full,  16)
        MY_BASE_INT_CASE(RTGETOPT_REQ_INT64  | RTGETOPT_FLAG_HEX, int64_t,  i64, RTStrToInt64Full,  16)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT8  | RTGETOPT_FLAG_HEX, uint8_t,  u8,  RTStrToUInt8Full,  16)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT16 | RTGETOPT_FLAG_HEX, uint16_t, u16, RTStrToUInt16Full, 16)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_HEX, uint32_t, u32, RTStrToUInt32Full, 16)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_HEX, uint64_t, u64, RTStrToUInt64Full, 16)

        MY_BASE_INT_CASE(RTGETOPT_REQ_INT8   | RTGETOPT_FLAG_DEC, int8_t,   i8,  RTStrToInt8Full,   10)
        MY_BASE_INT_CASE(RTGETOPT_REQ_INT16  | RTGETOPT_FLAG_DEC, int16_t,  i16, RTStrToInt16Full,  10)
        MY_BASE_INT_CASE(RTGETOPT_REQ_INT32  | RTGETOPT_FLAG_DEC, int32_t,  i32, RTStrToInt32Full,  10)
        MY_BASE_INT_CASE(RTGETOPT_REQ_INT64  | RTGETOPT_FLAG_DEC, int64_t,  i64, RTStrToInt64Full,  10)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT8  | RTGETOPT_FLAG_DEC, uint8_t,  u8,  RTStrToUInt8Full,  10)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT16 | RTGETOPT_FLAG_DEC, uint16_t, u16, RTStrToUInt16Full, 10)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_DEC, uint32_t, u32, RTStrToUInt32Full, 10)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_DEC, uint64_t, u64, RTStrToUInt64Full, 10)

        MY_BASE_INT_CASE(RTGETOPT_REQ_INT8   | RTGETOPT_FLAG_OCT, int8_t,   i8,  RTStrToInt8Full,   8)
        MY_BASE_INT_CASE(RTGETOPT_REQ_INT16  | RTGETOPT_FLAG_OCT, int16_t,  i16, RTStrToInt16Full,  8)
        MY_BASE_INT_CASE(RTGETOPT_REQ_INT32  | RTGETOPT_FLAG_OCT, int32_t,  i32, RTStrToInt32Full,  8)
        MY_BASE_INT_CASE(RTGETOPT_REQ_INT64  | RTGETOPT_FLAG_OCT, int64_t,  i64, RTStrToInt64Full,  8)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT8  | RTGETOPT_FLAG_OCT, uint8_t,  u8,  RTStrToUInt8Full,  8)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT16 | RTGETOPT_FLAG_OCT, uint16_t, u16, RTStrToUInt16Full, 8)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_OCT, uint32_t, u32, RTStrToUInt32Full, 8)
        MY_BASE_INT_CASE(RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_OCT, uint64_t, u64, RTStrToUInt64Full, 8)

#undef MY_INT_CASE
#undef MY_BASE_INT_CASE

#ifndef IPRT_GETOPT_WITHOUT_NETWORK_ADDRESSES

        case RTGETOPT_REQ_IPV4ADDR:
        {
            RTNETADDRIPV4 Addr;
            if (rtgetoptConvertIPv4Addr(pszValue, &Addr) != VINF_SUCCESS)
                return VERR_GETOPT_INVALID_ARGUMENT_FORMAT;
            pValueUnion->IPv4Addr = Addr;
            break;
        }

        case RTGETOPT_REQ_IPV4CIDR:
        {
            RTNETADDRIPV4 network;
            RTNETADDRIPV4 netmask;
            if (RT_FAILURE(RTCidrStrToIPv4(pszValue, &network, &netmask)))
              return VERR_GETOPT_INVALID_ARGUMENT_FORMAT;
            pValueUnion->CidrIPv4.IPv4Network.u = network.u;
            pValueUnion->CidrIPv4.IPv4Netmask.u = netmask.u;
            break;
        }

        case RTGETOPT_REQ_MACADDR:
        {
            RTMAC Addr;
            if (rtgetoptConvertMacAddr(pszValue, &Addr) != VINF_SUCCESS)
                return VERR_GETOPT_INVALID_ARGUMENT_FORMAT;
            pValueUnion->MacAddr = Addr;
            break;
        }

#endif /* IPRT_GETOPT_WITHOUT_NETWORK_ADDRESSES */

        case RTGETOPT_REQ_UUID:
        {
            RTUUID Uuid;
            if (RTUuidFromStr(&Uuid, pszValue) != VINF_SUCCESS)
                return VERR_GETOPT_INVALID_ARGUMENT_FORMAT;
            pValueUnion->Uuid = Uuid;
            break;
        }

#define MY_INT_PAIR_CASE(a_fReqValue, a_fReqValueOptional, a_Type, a_MemberPrefix, a_fnConv, a_ConvBase, a_DefaultValue) \
            case a_fReqValue: \
            case a_fReqValueOptional: \
            { \
                /* First value: */ \
                a_Type Value1; \
                char *pszNext = NULL; \
                unsigned uBase =   pszValue[0] == '0' \
                                && (pszValue[1] == 'x' || pszValue[1] == 'X') \
                                && RT_C_IS_XDIGIT(pszValue[2]) \
                              ? 16 : a_ConvBase; \
                int rc = a_fnConv(pszValue, &pszNext, uBase, &Value1); \
                if (rc == VINF_SUCCESS || rc == VWRN_TRAILING_CHARS || rc == VWRN_TRAILING_SPACES) \
                { \
                    /* The second value, could be optional: */ \
                    a_Type Value2 = a_DefaultValue; \
                    pszValue = pszNext;\
                    if (pszValue) \
                    { \
                        while (RT_C_IS_BLANK(*pszValue)) \
                            pszValue++; \
                        if (*pszValue == ':' || *pszValue == '/' || *pszValue == '|') \
                            do pszValue++; \
                            while (RT_C_IS_BLANK(*pszValue)); \
                        if (pszValue != pszNext) \
                        { \
                            uBase =    pszValue[0] == '0' \
                                    && (pszValue[1] == 'x' || pszValue[1] == 'X') \
                                    && RT_C_IS_XDIGIT(pszValue[2]) \
                                  ? 16 : a_ConvBase; \
                            rc = a_fnConv(pszValue, &pszNext, uBase, &Value2); \
                            if (rc == VINF_SUCCESS) \
                            { /* likely */ } \
                            else \
                                AssertMsgFailedReturn(("z rc=%Rrc: '%s' '%s' uBase=%d\n", rc, pszValue, pszNext, uBase), \
                                                       VERR_GETOPT_INVALID_ARGUMENT_FORMAT); \
                        } \
                        else if (fSwitchValue != (a_fReqValueOptional)) \
                            AssertMsgFailedReturn(("x\n"), VERR_GETOPT_INVALID_ARGUMENT_FORMAT); \
                    } \
                    else if (fSwitchValue != (a_fReqValueOptional)) \
                        AssertMsgFailedReturn(("y\n"), VERR_GETOPT_INVALID_ARGUMENT_FORMAT); \
                    pValueUnion->a_MemberPrefix##Second = Value2; \
                    pValueUnion->a_MemberPrefix##First  = Value1; \
                    break; \
                } \
                return VERR_GETOPT_INVALID_ARGUMENT_FORMAT; \
            }

        MY_INT_PAIR_CASE(RTGETOPT_REQ_UINT32_PAIR,                      RTGETOPT_REQ_UINT32_OPTIONAL_PAIR,
                         uint32_t, PairU32.u, RTStrToUInt32Ex, 10, UINT32_MAX)
        MY_INT_PAIR_CASE(RTGETOPT_REQ_UINT32_PAIR | RTGETOPT_FLAG_DEC,  RTGETOPT_REQ_UINT32_OPTIONAL_PAIR | RTGETOPT_FLAG_DEC,
                         uint32_t, PairU32.u, RTStrToUInt32Ex, 10, UINT32_MAX)
        MY_INT_PAIR_CASE(RTGETOPT_REQ_UINT32_PAIR | RTGETOPT_FLAG_HEX,  RTGETOPT_REQ_UINT32_OPTIONAL_PAIR | RTGETOPT_FLAG_HEX,
                         uint32_t, PairU32.u, RTStrToUInt32Ex, 16, UINT32_MAX)
        MY_INT_PAIR_CASE(RTGETOPT_REQ_UINT32_PAIR | RTGETOPT_FLAG_OCT,  RTGETOPT_REQ_UINT32_OPTIONAL_PAIR | RTGETOPT_FLAG_OCT,
                         uint32_t, PairU32.u, RTStrToUInt32Ex,  8, UINT32_MAX)

        MY_INT_PAIR_CASE(RTGETOPT_REQ_UINT64_PAIR,                      RTGETOPT_REQ_UINT64_OPTIONAL_PAIR,
                         uint64_t, PairU64.u, RTStrToUInt64Ex, 10, UINT64_MAX)
        MY_INT_PAIR_CASE(RTGETOPT_REQ_UINT64_PAIR | RTGETOPT_FLAG_DEC,  RTGETOPT_REQ_UINT64_OPTIONAL_PAIR | RTGETOPT_FLAG_DEC,
                         uint64_t, PairU64.u, RTStrToUInt64Ex, 10, UINT64_MAX)
        MY_INT_PAIR_CASE(RTGETOPT_REQ_UINT64_PAIR | RTGETOPT_FLAG_HEX,  RTGETOPT_REQ_UINT64_OPTIONAL_PAIR | RTGETOPT_FLAG_HEX,
                         uint64_t, PairU64.u, RTStrToUInt64Ex, 16, UINT64_MAX)
        MY_INT_PAIR_CASE(RTGETOPT_REQ_UINT64_PAIR | RTGETOPT_FLAG_OCT,  RTGETOPT_REQ_UINT64_OPTIONAL_PAIR | RTGETOPT_FLAG_OCT,
                         uint64_t, PairU64.u, RTStrToUInt64Ex,  8, UINT64_MAX)

        default:
            AssertMsgFailed(("f=%#x\n", fFlags));
            return VERR_INTERNAL_ERROR;
    }

    return VINF_SUCCESS;
}


/**
 * Moves one argv option entries.
 *
 * @param   papszTo             Destination.
 * @param   papszFrom           Source.
 */
static void rtGetOptMoveArgvEntries(char **papszTo, char **papszFrom)
{
    if (papszTo != papszFrom)
    {
        Assert((uintptr_t)papszTo < (uintptr_t)papszFrom);
        char * const pszMoved = papszFrom[0];
        memmove(&papszTo[1], &papszTo[0], (uintptr_t)papszFrom - (uintptr_t)papszTo);
        papszTo[0] = pszMoved;
    }
}


RTDECL(int) RTGetOpt(PRTGETOPTSTATE pState, PRTGETOPTUNION pValueUnion)
{
    /*
     * Reset the variables kept in state.
     */
    pState->pDef = NULL;
    pState->uIndex = UINT32_MAX;

    /*
     * Make sure the union is completely cleared out, whatever happens below.
     */
    pValueUnion->u64 = 0;
    pValueUnion->pDef = NULL;

    /*
     * The next option.
     */
    bool            fShort;
    int             iThis;
    const char     *pszArgThis;
    size_t          cchMatch;
    PCRTGETOPTDEF   pOpt;

    if (pState->pszNextShort)
    {
        /*
         * We've got short options left over from the previous call.
         */
        pOpt = rtGetOptSearchShort(*pState->pszNextShort, pState->paOptions, pState->cOptions, pState->fFlags);
        if (!pOpt)
        {
            pValueUnion->psz = pState->pszNextShort;
            return VERR_GETOPT_UNKNOWN_OPTION;
        }
        pszArgThis = pState->pszNextShort++;
        cchMatch = 1;
        iThis = pState->iNext;
        fShort = true;
    }
    else
    {
        /*
         * Pop off the next argument.  Sorting options and dealing with the
         * dash-dash makes this a little extra complicated.
         */
        for (;;)
        {
            if (pState->iNext >= pState->argc)
                return 0;

            if (pState->cNonOptions)
            {
                if (pState->cNonOptions == INT32_MAX)
                {
                    pValueUnion->psz = pState->argv[pState->iNext++];
                    return VINF_GETOPT_NOT_OPTION;
                }

                if (pState->iNext + pState->cNonOptions >= pState->argc)
                {
                    pState->cNonOptions = INT32_MAX;
                    continue;
                }
            }

            iThis = pState->iNext++;
            pszArgThis = pState->argv[iThis + pState->cNonOptions];

            /*
             * Do a long option search first and then a short option one.
             * This way we can make sure single dash long options doesn't
             * get mixed up with short ones.
             */
            pOpt = rtGetOptSearchLong(pszArgThis, pState->paOptions, pState->cOptions, pState->fFlags, &cchMatch);
            fShort = false;
            if (!pOpt)
            {
                const char *pszNext = pszArgThis;
                RTUNICP     uc;
                int         vrc = RTStrGetCpEx(&pszNext, &uc);
                AssertRCReturn(vrc, vrc);
                char        chNext;
                if (   rtGetOptIsDash(uc)
                    && (chNext = *pszNext) != '-'
                    && chNext != '\0'
                    && !((unsigned char)chNext & 0x80))
                {
                    pOpt = rtGetOptSearchShort(chNext, pState->paOptions, pState->cOptions, pState->fFlags);
                    if (pOpt)
                    {
                        cchMatch = (size_t)(&pszNext[1] - pszArgThis);
                        fShort  = true;
                    }
                }
            }

            /* Look for dash-dash. */
            if (!pOpt && rtGetOptLongStrEquals(pszArgThis, "--", RTSTR_MAX, 0, NULL))
            {
                rtGetOptMoveArgvEntries(&pState->argv[iThis], &pState->argv[iThis + pState->cNonOptions]);
                pState->cNonOptions = INT32_MAX;
                continue;
            }

            /* Options first hacks. */
            if (pState->fFlags & RTGETOPTINIT_FLAGS_OPTS_FIRST)
            {
                if (pOpt)
                    rtGetOptMoveArgvEntries(&pState->argv[iThis], &pState->argv[iThis + pState->cNonOptions]);
                else if (rtGetOptIsDash(RTStrGetCp(pszArgThis)))
                {
                    pValueUnion->psz = pszArgThis;
                    return VERR_GETOPT_UNKNOWN_OPTION;
                }
                else
                {
                    /* not an option, add it to the non-options and try again. */
                    pState->iNext--;
                    pState->cNonOptions++;

                    /* Switch to returning non-options if we've reached the end. */
                    if (pState->iNext + pState->cNonOptions >= pState->argc)
                        pState->cNonOptions = INT32_MAX;
                    continue;
                }
            }

            /* done */
            break;
        }
    }

    if (pOpt)
    {
        pValueUnion->pDef = pOpt; /* in case of no value or error. */

        uint32_t const fOptFlags = pOpt->fFlags;
        if ((fOptFlags & RTGETOPT_REQ_MASK) != RTGETOPT_REQ_NOTHING)
        {
            /*
             * Find the argument value.
             *
             * A value is required with the argument. We're trying to be
             * understanding here and will permit any of the following:
             *      -svalue, -s value, -s:value and -s=value
             * (Ditto for long options.)
             */
            const char *pszValue;
            if (fShort)
            {
                if (pszArgThis[cchMatch] == '\0')
                {
                    if (iThis + 1 >= pState->argc)
                        return VERR_GETOPT_REQUIRED_ARGUMENT_MISSING;
                    pszValue = pState->argv[iThis + pState->cNonOptions + 1];
                    rtGetOptMoveArgvEntries(&pState->argv[iThis + 1], &pState->argv[iThis + pState->cNonOptions + 1]);
                    pState->iNext++;
                }
                else /* same argument. */
                    pszValue = &pszArgThis[cchMatch + (pszArgThis[cchMatch] == ':' || pszArgThis[cchMatch] == '=')];
                if (pState->pszNextShort)
                {
                    pState->pszNextShort = NULL;
                    pState->iNext++;
                }
            }
            else
            {
                if (fOptFlags & RTGETOPT_FLAG_INDEX)
                {
                    if (   pszArgThis[cchMatch] != '\0'
                        || (fOptFlags & RTGETOPT_FLAG_INDEX_DEF_MASK))
                    {
                        const char *pszNext = &pszArgThis[cchMatch];
                        uint32_t    uIndex;
                        int rc = RTStrToUInt32Ex(pszNext, (char **)&pszNext, 10, &uIndex);
                        if (   rc == VERR_NO_DIGITS
                            && (fOptFlags & RTGETOPT_FLAG_INDEX_DEF_MASK))
                        {
                            uIndex = ((fOptFlags & RTGETOPT_FLAG_INDEX_DEF_MASK) >> RTGETOPT_FLAG_INDEX_DEF_SHIFT) - 1;
                            rc = pszNext[0] == '\0' ? VINF_SUCCESS : VWRN_TRAILING_CHARS;
                        }
                        if (rc == VWRN_TRAILING_CHARS)
                        {
                            if (   pszNext[0] != ':'
                                && pszNext[0] != '=')
                                return VERR_GETOPT_INVALID_ARGUMENT_FORMAT;
                            pState->uIndex = uIndex;
                            pszValue = pszNext + 1;
                        }
                        else if (rc == VINF_SUCCESS)
                        {
                            if (iThis + 1 + pState->cNonOptions >= pState->argc)
                                return VERR_GETOPT_REQUIRED_ARGUMENT_MISSING;
                            pState->uIndex = uIndex;
                            pszValue = pState->argv[iThis + pState->cNonOptions + 1];
                            rtGetOptMoveArgvEntries(&pState->argv[iThis + 1], &pState->argv[iThis + pState->cNonOptions + 1]);
                            pState->iNext++;
                        }
                        else
                            AssertMsgFailedReturn(("%s\n", pszArgThis), VERR_GETOPT_INVALID_ARGUMENT_FORMAT); /* search bug */
                    }
                    else
                        return VERR_GETOPT_INDEX_MISSING;
                }
                else
                {
                    if (pszArgThis[cchMatch] == '\0')
                    {
                        if (iThis + 1 + pState->cNonOptions >= pState->argc)
                            return VERR_GETOPT_REQUIRED_ARGUMENT_MISSING;
                        pszValue = pState->argv[iThis + pState->cNonOptions + 1];
                        rtGetOptMoveArgvEntries(&pState->argv[iThis + 1], &pState->argv[iThis + pState->cNonOptions + 1]);
                        pState->iNext++;
                    }
                    else /* same argument. */
                        pszValue = &pszArgThis[cchMatch + 1];
                }
            }

            /*
             * Set up the ValueUnion.
             */
            int rc = rtGetOptProcessValue(fOptFlags, pszValue, pValueUnion);
            if (RT_FAILURE(rc))
                return rc;
        }
        else if (fShort)
        {
            /*
             * Deal with "compressed" short option lists, correcting the next
             * state variables for the start and end cases.
             */
            if (pszArgThis[cchMatch])
            {
                if (!pState->pszNextShort)
                {
                    /* start */
                    pState->pszNextShort = &pszArgThis[cchMatch];
                    pState->iNext--;
                }
            }
            else if (pState->pszNextShort)
            {
                /* end */
                pState->pszNextShort = NULL;
                pState->iNext++;
            }
        }
        else if (fOptFlags & RTGETOPT_FLAG_INDEX)
        {
            uint32_t uIndex;
            if (pszArgThis[cchMatch] != '\0')
            {
                if (RTStrToUInt32Full(&pszArgThis[cchMatch], 10, &uIndex) == VINF_SUCCESS)
                    pState->uIndex = uIndex;
                else
                    AssertMsgFailedReturn(("%s\n", pszArgThis), VERR_GETOPT_INVALID_ARGUMENT_FORMAT); /* search bug */
            }
            else if (fOptFlags & RTGETOPT_FLAG_INDEX_DEF_MASK)
                uIndex = ((fOptFlags & RTGETOPT_FLAG_INDEX_DEF_MASK) >> RTGETOPT_FLAG_INDEX_DEF_SHIFT) - 1;
            else
                return VERR_GETOPT_INDEX_MISSING;
        }

        pState->pDef = pOpt;
        return pOpt->iShort;
    }

    /*
     * Not a known option argument. If it starts with a switch char (-) we'll
     * fail with unknown option, and if it doesn't we'll return it as a non-option.
     */
    if (*pszArgThis == '-')
    {
        pValueUnion->psz = pszArgThis;
        return VERR_GETOPT_UNKNOWN_OPTION;
    }

    pValueUnion->psz = pszArgThis;
    return VINF_GETOPT_NOT_OPTION;
}
RT_EXPORT_SYMBOL(RTGetOpt);


RTDECL(int) RTGetOptFetchValue(PRTGETOPTSTATE pState, PRTGETOPTUNION pValueUnion, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    PCRTGETOPTDEF pOpt = pState->pDef;
    AssertReturn(!(fFlags & ~RTGETOPT_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn((fFlags & RTGETOPT_REQ_MASK) != RTGETOPT_REQ_NOTHING, VERR_INVALID_PARAMETER);

    /*
     * Make sure the union is completely cleared out, whatever happens below.
     */
    pValueUnion->u64 = 0;
    pValueUnion->pDef = NULL;

    /*
     * Pop off the next argument and convert it into a value union.
     */
    if (pState->iNext >= pState->argc)
        return VERR_GETOPT_REQUIRED_ARGUMENT_MISSING;
    int         iThis    = pState->iNext++;
    const char *pszValue = pState->argv[iThis + (pState->cNonOptions != INT32_MAX ? pState->cNonOptions : 0)];
    pValueUnion->pDef    = pOpt; /* in case of no value or error. */

    if (pState->cNonOptions && pState->cNonOptions != INT32_MAX)
        rtGetOptMoveArgvEntries(&pState->argv[iThis], &pState->argv[iThis + pState->cNonOptions]);

    return rtGetOptProcessValue(fFlags, pszValue, pValueUnion);
}
RT_EXPORT_SYMBOL(RTGetOptFetchValue);


RTDECL(char **) RTGetOptNonOptionArrayPtr(PRTGETOPTSTATE pState)
{
    AssertReturn(pState->fFlags & RTGETOPTINIT_FLAGS_OPTS_FIRST, NULL);
    return &pState->argv[pState->iNext - 1];
}
RT_EXPORT_SYMBOL(RTGetOptNonOptionArrayPtr);


RTDECL(RTEXITCODE) RTGetOptPrintError(int ch, PCRTGETOPTUNION pValueUnion)
{
    if (ch == VINF_GETOPT_NOT_OPTION)
        RTMsgError("Invalid parameter: %s", pValueUnion->psz);
    else if (ch > 0)
    {
        if (RT_C_IS_GRAPH(ch))
            RTMsgError("Unhandled option: -%c", ch);
        else
            RTMsgError("Unhandled option: %i (%#x)", ch, ch);
    }
    else if (ch == VERR_GETOPT_UNKNOWN_OPTION)
        RTMsgError("Unknown option: '%s'", pValueUnion->psz);
    else if (pValueUnion->pDef && ch == VERR_GETOPT_INVALID_ARGUMENT_FORMAT)
        /** @todo r=klaus not really ideal, as the value isn't available */
        RTMsgError("The value given '%s' has an invalid format.", pValueUnion->pDef->pszLong);
    else if (pValueUnion->pDef)
        RTMsgError("%s: %Rrs\n", pValueUnion->pDef->pszLong, ch);
    else
        RTMsgError("%Rrs\n", ch);

    return RTEXITCODE_SYNTAX;
}
RT_EXPORT_SYMBOL(RTGetOptPrintError);


RTDECL(ssize_t) RTGetOptFormatError(char *pszBuf, size_t cbBuf, int ch, PCRTGETOPTUNION pValueUnion)
{
    ssize_t cchRet;
    if (ch == VINF_GETOPT_NOT_OPTION)
        cchRet = RTStrPrintf2(pszBuf, cbBuf, "Invalid parameter: %s", pValueUnion->psz);
    else if (ch > 0)
    {
        if (RT_C_IS_GRAPH(ch))
            cchRet = RTStrPrintf2(pszBuf, cbBuf, "Unhandled option: -%c", ch);
        else
            cchRet = RTStrPrintf2(pszBuf, cbBuf, "Unhandled option: %i (%#x)", ch, ch);
    }
    else if (ch == VERR_GETOPT_UNKNOWN_OPTION)
        cchRet = RTStrPrintf2(pszBuf, cbBuf, "Unknown option: '%s'", pValueUnion->psz);
    else if (pValueUnion->pDef && ch == VERR_GETOPT_INVALID_ARGUMENT_FORMAT)
        /** @todo r=klaus not really ideal, as the value isn't available */
        cchRet = RTStrPrintf2(pszBuf, cbBuf, "The value given '%s' has an invalid format.", pValueUnion->pDef->pszLong);
    else if (pValueUnion->pDef)
        cchRet = RTStrPrintf2(pszBuf, cbBuf, "%s: %Rrs\n", pValueUnion->pDef->pszLong, ch);
    else
        cchRet = RTStrPrintf2(pszBuf, cbBuf, "%Rrs\n", ch);

    return cchRet;
}
RT_EXPORT_SYMBOL(RTGetOptFormatError);

