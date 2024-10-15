/* $Id: errmsg-sorter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Status code messages, sorter build program.
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
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <VBox/err.h>

#include <stdio.h>
#include <stdlib.h>


/*
 * Include the string table code.
 */
#define BLDPROG_STRTAB_MAX_STRLEN           512
#define BLDPROG_STRTAB_WITH_COMPRESSION
#define BLDPROG_STRTAB_PURE_ASCII
#define BLDPROG_STRTAB_WITH_CAMEL_WORDS
#include <iprt/bldprog-strtab-template.cpp.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Used for raw-input and sorting. */
typedef struct RTSTATUSMSGINT1
{
    /** Pointer to the short message string. */
    const char     *pszMsgShort;
    /** Pointer to the full message string. */
    const char     *pszMsgFull;
    /** Pointer to the define string. */
    const char     *pszDefine;
    /** Status code number. */
    int             iCode;
    /** Set if duplicate. */
    bool            fDuplicate;
} RTSTATUSMSGINT1;
typedef RTSTATUSMSGINT1 *PRTSTATUSMSGINT1;


/** This is used when building the string table and printing it. */
typedef struct RTSTATUSMSGINT2
{
    /** The short message string. */
    BLDPROGSTRING   MsgShort;
    /** The full message string. */
    BLDPROGSTRING   MsgFull;
    /** The define string. */
    BLDPROGSTRING   Define;
    /** Pointer to the define string. */
    const char     *pszDefine;
    /** Status code number. */
    int             iCode;
    /** Index into the primary table (for multiple passes). */
    unsigned        idx1;
} RTSTATUSMSGINT2;
typedef RTSTATUSMSGINT2 *PRTSTATUSMSGINT2;


/** This used to determin minimum field sizes. */
typedef struct RTSTATUSMSGSTATS
{
    unsigned offMax;
    unsigned cchMax;
    unsigned cBitsOffset;
    unsigned cBitsLength;
} RTSTATUSMSGSTATS;
typedef RTSTATUSMSGSTATS *PRTSTATUSMSGSTATS;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char *g_pszProgName = "errmsg-sorter";
static RTSTATUSMSGINT1 g_aStatusMsgs[] =
{
#if !defined(IPRT_NO_ERROR_DATA) && !defined(DOXYGEN_RUNNING)
# include "errmsgdata.h"
#else
    { "Success.", "Success.", "VINF_SUCCESS", 0, false },
#endif
};


static RTEXITCODE error(const char *pszFormat,  ...)
{
    va_list va;
    va_start(va, pszFormat);
    fprintf(stderr, "%s: error: ", g_pszProgName);
    vfprintf(stderr, pszFormat, va);
    va_end(va);
    return RTEXITCODE_FAILURE;
}


/** qsort callback. */
static int CompareErrMsg(const void *pv1, const void *pv2) RT_NOTHROW_DEF
{
    PRTSTATUSMSGINT1 p1 = (PRTSTATUSMSGINT1)pv1;
    PRTSTATUSMSGINT1 p2 = (PRTSTATUSMSGINT1)pv2;
    int iDiff;
    if (p1->iCode < p2->iCode)
        iDiff = -1;
    else if (p1->iCode > p2->iCode)
        iDiff = 1;
    else
        iDiff = 0;
    return iDiff;
}


/**
 * Checks whether @a pszDefine is a deliberate duplicate define that should be
 * omitted.
 */
static bool IgnoreDuplicateDefine(const char *pszDefine)
{
    size_t const cchDefine = strlen(pszDefine);

    static const RTSTRTUPLE s_aTails[] =
    {
        { RT_STR_TUPLE("_FIRST") },
        { RT_STR_TUPLE("_LAST") },
        { RT_STR_TUPLE("_HIGEST") },
        { RT_STR_TUPLE("_LOWEST") },
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aTails); i++)
        if (   cchDefine > s_aTails[i].cch
            && memcmp(&pszDefine[cchDefine - s_aTails[i].cch], s_aTails[i].psz, s_aTails[i].cch) == 0)
            return true;

    static const RTSTRTUPLE s_aDeliberateOrSilly[] =
    {
        { RT_STR_TUPLE("VERR_VRDP_TIMEOUT") },
        { RT_STR_TUPLE("VINF_VRDP_SUCCESS") },
        { RT_STR_TUPLE("VWRN_CONTINUE_RECOMPILE") },
        { RT_STR_TUPLE("VWRN_PATM_CONTINUE_SEARCH") },
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aDeliberateOrSilly); i++)
        if (   cchDefine == s_aDeliberateOrSilly[i].cch
            && memcmp(pszDefine, s_aDeliberateOrSilly[i].psz, cchDefine) == 0)
            return true;

    return false;
}


DECLINLINE(void) GatherStringStats(PRTSTATUSMSGSTATS pStats, PBLDPROGSTRING pString)
{
    if (pStats->offMax < pString->offStrTab)
        pStats->offMax = pString->offStrTab;
    if (pStats->cchMax < pString->cchString)
        pStats->cchMax = (unsigned)pString->cchString;
}


DECLINLINE(unsigned) CalcBitsForValue(size_t uValue)
{
    unsigned cBits = 1;
    while (RT_BIT_64(cBits) < uValue && cBits < 64)
        cBits++;
    return cBits;
}


static void CalcBitsForStringStats(PRTSTATUSMSGSTATS pStats)
{
    pStats->cBitsOffset = CalcBitsForValue(pStats->offMax);
    pStats->cBitsLength = CalcBitsForValue(pStats->cchMax);
}


int main(int argc, char **argv)
{
    /*
     * Parse arguments.
     */
    enum { kMode_All, kMode_NoFullMsg, kMode_OnlyDefines } enmMode;
    if (argc == 3 && strcmp(argv[1], "--all") == 0)
        enmMode = kMode_All;
    else if (argc == 3 && strcmp(argv[1], "--no-full-msg") == 0)
        enmMode = kMode_NoFullMsg;
    else if (argc == 3 && strcmp(argv[1], "--only-defines") == 0)
        enmMode = kMode_OnlyDefines;
    else
    {
        fprintf(stderr,
                "syntax error!\n"
                "Usage: %s <--all|--no-full-msg|--only-defines> <outfile>\n", argv[0]);
        return RTEXITCODE_SYNTAX;
    }
    const char * const pszOutFile = argv[2];

    /*
     * Sort the table and mark duplicates.
     */
    qsort(g_aStatusMsgs, RT_ELEMENTS(g_aStatusMsgs), sizeof(g_aStatusMsgs[0]), CompareErrMsg);

    int rcExit = RTEXITCODE_SUCCESS;
    int iPrev  = INT32_MAX;
    for (size_t i = 0; i < RT_ELEMENTS(g_aStatusMsgs); i++)
    {
        /* Deal with duplicates, trying to eliminate unnecessary *_FIRST, *_LAST,
           *_LOWEST, and *_HIGHEST entries as well as some deliberate duplicate entries.
           This means we need to look forward and backwards here. */
        PRTSTATUSMSGINT1 pMsg = &g_aStatusMsgs[i];
        if (pMsg->iCode == iPrev && i != 0)
        {
            if (IgnoreDuplicateDefine(pMsg->pszDefine))
            {
                pMsg->fDuplicate = true;
                continue;
            }
            PRTSTATUSMSGINT1 pPrev = &g_aStatusMsgs[i - 1];
            rcExit = error("Duplicate value %d - %s and %s\n", iPrev, pMsg->pszDefine, pPrev->pszDefine);
        }
        else if (i + 1 < RT_ELEMENTS(g_aStatusMsgs))
        {
            PRTSTATUSMSGINT1 pNext = &g_aStatusMsgs[i];
            if (   pMsg->iCode == pNext->iCode
                && IgnoreDuplicateDefine(pMsg->pszDefine))
            {
                pMsg->fDuplicate = true;
                continue;
            }
        }
        iPrev = pMsg->iCode;
        pMsg->fDuplicate = false;
    }

    /*
     * Create a string table for it all.
     */
    BLDPROGSTRTAB StrTab;
    if (!BldProgStrTab_Init(&StrTab, RT_ELEMENTS(g_aStatusMsgs) * 3))
        return error("Out of memory!\n");

    static RTSTATUSMSGINT2 s_aStatusMsgs2[RT_ELEMENTS(g_aStatusMsgs)];
    unsigned               cStatusMsgs = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aStatusMsgs); i++)
        if (!g_aStatusMsgs[i].fDuplicate)
        {
            s_aStatusMsgs2[cStatusMsgs].idx1      = i;
            s_aStatusMsgs2[cStatusMsgs].iCode     = g_aStatusMsgs[i].iCode;
            s_aStatusMsgs2[cStatusMsgs].pszDefine = g_aStatusMsgs[i].pszDefine;
            BldProgStrTab_AddStringDup(&StrTab, &s_aStatusMsgs2[cStatusMsgs].Define, g_aStatusMsgs[i].pszDefine);
            cStatusMsgs++;
        }

    if (enmMode != kMode_OnlyDefines)
        for (size_t i = 0; i < cStatusMsgs; i++)
            BldProgStrTab_AddStringDup(&StrTab, &s_aStatusMsgs2[i].MsgShort, g_aStatusMsgs[s_aStatusMsgs2[i].idx1].pszMsgShort);

    if (enmMode == kMode_All)
        for (size_t i = 0; i < cStatusMsgs; i++)
            BldProgStrTab_AddStringDup(&StrTab, &s_aStatusMsgs2[i].MsgFull, g_aStatusMsgs[s_aStatusMsgs2[i].idx1].pszMsgFull);

    if (!BldProgStrTab_CompileIt(&StrTab, true))
        return error("BldProgStrTab_CompileIt failed!\n");

    /*
     * Prepare output file.
     */
    FILE *pOut = fopen(pszOutFile, "wt");
    if (pOut)
    {
        /*
         * .
         */
        RTSTATUSMSGSTATS Defines  = {0, 0, 0, 0};
        RTSTATUSMSGSTATS MsgShort = {0, 0, 0, 0};
        RTSTATUSMSGSTATS MsgFull  = {0, 0, 0, 0};
        for (size_t i = 0; i < cStatusMsgs; i++)
        {
            GatherStringStats(&Defines,  &s_aStatusMsgs2[i].Define);
            GatherStringStats(&MsgShort, &s_aStatusMsgs2[i].MsgShort);
            GatherStringStats(&MsgFull,  &s_aStatusMsgs2[i].MsgFull);
        }
        CalcBitsForStringStats(&Defines);
        CalcBitsForStringStats(&MsgShort);
        CalcBitsForStringStats(&MsgFull);
        printf(" Defines: max offset %#x -> %u bits, max length %#x -> bits %u\n",
               Defines.offMax, Defines.cBitsOffset, (unsigned)Defines.cchMax, Defines.cBitsLength);
        if (enmMode != kMode_OnlyDefines)
            printf("MsgShort: max offset %#x -> %u bits, max length %#x -> bits %u\n",
                   MsgShort.offMax, MsgShort.cBitsOffset, (unsigned)MsgShort.cchMax, MsgShort.cBitsLength);
        if (enmMode == kMode_All)
            printf(" MsgFull: max offset %#x -> %u bits, max length %#x -> bits %u\n",
                   MsgFull.offMax, MsgFull.cBitsOffset, (unsigned)MsgFull.cchMax, MsgFull.cBitsLength);

        unsigned cBitsCodePos = CalcBitsForValue((size_t)s_aStatusMsgs2[cStatusMsgs - 1].iCode);
        unsigned cBitsCodeNeg = CalcBitsForValue((size_t)-s_aStatusMsgs2[0].iCode);
        unsigned cBitsCode    = RT_MAX(cBitsCodePos, cBitsCodeNeg) + 1;
        printf("Statuses: min %d, max %d -> %u bits\n",
               s_aStatusMsgs2[0].iCode, s_aStatusMsgs2[cStatusMsgs - 1].iCode, cBitsCode);

        /*
         * Print the table.
         */
        fprintf(pOut,
                "\n"
                "#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)\n"
                "# pragma pack(1)\n"
                "#endif\n"
                "typedef struct RTMSGENTRYINT\n"
                "{\n");
        /* 16 + 16 + 8 */
        bool fOptimalLayout = true;
        if (   enmMode == kMode_OnlyDefines
            && cBitsCode <= 16
            && Defines.cBitsOffset <= 16
            && Defines.cBitsLength <= 8)
            fprintf(pOut,
                    "    uint16_t offDefine; /* need %2u bits, max %#x */\n"
                    "    uint8_t  cchDefine; /* need %2u bits, max %#x */\n"
                    "    int16_t  iCode;     /* need %2u bits */\n",
                    Defines.cBitsOffset, Defines.offMax, Defines.cBitsLength, Defines.cchMax, cBitsCode);
        else if (   enmMode == kMode_NoFullMsg
                 && cBitsCode + Defines.cBitsOffset + Defines.cBitsLength + MsgShort.cBitsOffset + MsgShort.cBitsLength <= 64)
            fprintf(pOut,
                    "    uint64_t offDefine   : %2u; /* max %#x */\n"
                    "    uint64_t cchDefine   : %2u; /* max %#x */\n"
                    "    uint64_t offMsgShort : %2u; /* max %#x */\n"
                    "    uint64_t cchMsgShort : %2u; /* max %#x */\n"
                    "    int64_t  iCode       : %2u;\n",
                    Defines.cBitsOffset,  Defines.offMax,
                    Defines.cBitsLength,  Defines.cchMax,
                    MsgShort.cBitsOffset, MsgShort.offMax,
                    MsgShort.cBitsLength, MsgShort.cchMax,
                    cBitsCode);
        else if (   enmMode == kMode_All
                 &&   Defines.cBitsOffset  + Defines.cBitsLength
                    + MsgShort.cBitsOffset + MsgShort.cBitsLength
                    + MsgFull.cBitsOffset  + MsgFull.cBitsLength
                    + cBitsCode <= 96
                 && cBitsCode + Defines.cBitsLength + MsgShort.cBitsLength <= 32)
            fprintf(pOut,
                    "    uint64_t offDefine   : %2u; /* max %#x */\n"
                    "    uint64_t offMsgShort : %2u; /* max %#x */\n"
                    "    uint64_t offMsgFull  : %2u; /* max %#x */\n"
                    "    uint64_t cchMsgFull  : %2u; /* max %#x */\n"
                    "    int32_t  iCode       : %2u;\n"
                    "    uint32_t cchDefine   : %2u; /* max %#x */\n"
                    "    uint32_t cchMsgShort : %2u; /* max %#x */\n",
                    Defines.cBitsOffset,  Defines.offMax,
                    MsgShort.cBitsOffset, MsgShort.offMax,
                    MsgFull.cBitsOffset,  MsgFull.offMax,
                    MsgFull.cBitsLength,  MsgFull.cchMax,
                    cBitsCode,
                    Defines.cBitsLength,  Defines.cchMax,
                    MsgShort.cBitsLength, MsgShort.cchMax);
        else
        {
            fprintf(stderr, "%s: warning: Optimized structure layouts needs readjusting...\n", g_pszProgName);
            fOptimalLayout = false;
            fprintf(pOut,
                    "    uint32_t offDefine   : 23; /* need %u bits, max %#x */\n"
                    "    uint32_t cchDefine   :  9; /* need %u bits, max %#x */\n",
                    Defines.cBitsOffset, Defines.offMax, Defines.cBitsLength, Defines.cchMax);
            if (enmMode != kMode_OnlyDefines)
                fprintf(pOut,
                        "    uint32_t offMsgShort : 23; /* need %u bits, max %#x */\n"
                        "    uint32_t cchMsgShort :  9; /* need %u bits, max %#x */\n",
                        MsgShort.cBitsOffset, MsgShort.offMax, MsgShort.cBitsLength, MsgShort.offMax);
            if (enmMode == kMode_All)
                fprintf(pOut,
                        "    uint32_t offMsgFull  : 23; /* need %u bits, max %#x */\n"
                        "    uint32_t cchMsgFull  :  9; /* need %u bits, max %#x */\n",
                        MsgFull.cBitsOffset, MsgFull.offMax, MsgFull.cBitsLength, MsgFull.cchMax);
            fprintf(pOut,
                    "    int32_t  iCode; /* need %u bits */\n", cBitsCode);
        }
        fprintf(pOut,
                "} RTMSGENTRYINT;\n"
                "typedef RTMSGENTRYINT *PCRTMSGENTRYINT;\n"
                "#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)\n"
                "# pragma pack()\n"
                "#endif\n"
                "\n"
                "static const RTMSGENTRYINT g_aStatusMsgs[ /*%lu*/ ] =\n"
                "{\n"
                ,
                (unsigned long)cStatusMsgs);

        if (enmMode == kMode_All && fOptimalLayout)
            for (size_t i = 0; i < cStatusMsgs; i++)
                fprintf(pOut, "    { %#08x, %#08x, %#08x, %3u, %6d, %3u, %3u }, /* %s */\n",
                        s_aStatusMsgs2[i].Define.offStrTab,
                        s_aStatusMsgs2[i].MsgShort.offStrTab,
                        s_aStatusMsgs2[i].MsgFull.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].MsgFull.cchString,
                        s_aStatusMsgs2[i].iCode,
                        (unsigned)s_aStatusMsgs2[i].Define.cchString,
                        (unsigned)s_aStatusMsgs2[i].MsgShort.cchString,
                        s_aStatusMsgs2[i].pszDefine);
        else if (enmMode == kMode_All)
            for (size_t i = 0; i < cStatusMsgs; i++)
                fprintf(pOut, "    { %#08x, %3u, %#08x, %3u, %#08x, %3u, %8d }, /* %s */\n",
                        s_aStatusMsgs2[i].Define.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].Define.cchString,
                        s_aStatusMsgs2[i].MsgShort.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].MsgShort.cchString,
                        s_aStatusMsgs2[i].MsgFull.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].MsgFull.cchString,
                        s_aStatusMsgs2[i].iCode,
                        s_aStatusMsgs2[i].pszDefine);
        else if (enmMode == kMode_NoFullMsg)
            for (size_t i = 0; i < cStatusMsgs; i++)
                fprintf(pOut, "    { %#08x, %3u, %#08x, %3u, %8d }, /* %s */\n",
                        s_aStatusMsgs2[i].Define.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].Define.cchString,
                        s_aStatusMsgs2[i].MsgShort.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].MsgShort.cchString,
                        s_aStatusMsgs2[i].iCode,
                        s_aStatusMsgs2[i].pszDefine);
        else if (enmMode == kMode_OnlyDefines)
            for (size_t i = 0; i < cStatusMsgs; i++)
                fprintf(pOut, "    { %#08x, %3u, %8d }, /* %s */\n",
                        s_aStatusMsgs2[i].Define.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].Define.cchString,
                        s_aStatusMsgs2[i].iCode,
                        s_aStatusMsgs2[i].pszDefine);
        else
            return error("Unsupported message selection (%d)!\n", enmMode);
        fprintf(pOut,
                "};\n"
                "\n");

        BldProgStrTab_WriteStringTable(&StrTab, pOut, "static ", "g_", "StatusMsgStrTab");

        /*
         * Close the output file and we're done.
         */
        fflush(pOut);
        if (ferror(pOut))
            rcExit = error("Error writing '%s'!\n", pszOutFile);
        if (fclose(pOut) != 0)
            rcExit = error("Failed to close '%s' after writing it!\n", pszOutFile);
    }
    else
        rcExit = error("Failed to open '%s' for writing!\n", pszOutFile);
    return rcExit;
}

