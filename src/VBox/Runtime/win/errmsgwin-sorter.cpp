/* $Id: errmsgwin-sorter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Status code messages, Windows, sorter build program.
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
#include <iprt/win/windows.h>

#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/string.h>

#include <stdio.h>
#include <stdlib.h>


/*
 * Include the string table code.
 */
#define BLDPROG_STRTAB_MAX_STRLEN           1024
#define BLDPROG_STRTAB_WITH_COMPRESSION
#define BLDPROG_STRTAB_PURE_ASCII
#define BLDPROG_STRTAB_WITH_CAMEL_WORDS
#include <iprt/bldprog-strtab-template.cpp.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* This is define casts the result to DWORD, whereas HRESULT and RTWINERRMSG
   are using long, causing newer compilers to complain. */
#undef _NDIS_ERROR_TYPEDEF_
#define _NDIS_ERROR_TYPEDEF_(lErr) (long)(lErr)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef long VBOXSTATUSTYPE; /* used by errmsgvboxcomdata.h */

/** Used for raw-input and sorting. */
typedef struct RTWINERRMSGINT1
{
    /** Pointer to the full message string. */
    const char     *pszMsgFull;
    /** Pointer to the define string. */
    const char     *pszDefine;
    /** Status code number. */
    long            iCode;
    /** Set if duplicate. */
    bool            fDuplicate;
} RTWINERRMSGINT1;
typedef RTWINERRMSGINT1 *PRTWINERRMSGINT1;


/** This is used when building the string table and printing it. */
typedef struct RTWINERRMSGINT2
{
    /** The full message string. */
    BLDPROGSTRING   MsgFull;
    /** The define string. */
    BLDPROGSTRING   Define;
    /** Pointer to the define string. */
    const char     *pszDefine;
    /** Status code number. */
    long            iCode;
} RTWINERRMSGINT2;
typedef RTWINERRMSGINT2 *PRTWINERRMSGINT2;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char     *g_pszProgName = "errmsgwin-sorter";
static RTWINERRMSGINT1 g_aStatusMsgs[] =
{
#if !defined(IPRT_NO_ERROR_DATA) && !defined(DOXYGEN_RUNNING)
# include "errmsgwindata.h"
# if defined(VBOX) && !defined(IN_GUEST)
#  include "errmsgvboxcomdata.h"
# endif

/* A few hardcoded items not in winerror.h */
# define HARDCODED_ENTRY(a_Name, aValue) { #a_Name, #a_Name, aValue, false }
    HARDCODED_ENTRY(AUDCLNT_E_NOT_INITIALIZED              , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x01)),
    HARDCODED_ENTRY(AUDCLNT_E_ALREADY_INITIALIZED          , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x02)),
    HARDCODED_ENTRY(AUDCLNT_E_WRONG_ENDPOINT_TYPE          , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x03)),
    HARDCODED_ENTRY(AUDCLNT_E_DEVICE_INVALIDATED           , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x04)),
    HARDCODED_ENTRY(AUDCLNT_E_NOT_STOPPED                  , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x05)),
    HARDCODED_ENTRY(AUDCLNT_E_BUFFER_TOO_LARGE             , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x06)),
    HARDCODED_ENTRY(AUDCLNT_E_OUT_OF_ORDER                 , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x07)),
    HARDCODED_ENTRY(AUDCLNT_E_UNSUPPORTED_FORMAT           , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x08)),
    HARDCODED_ENTRY(AUDCLNT_E_INVALID_SIZE                 , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x09)),
    HARDCODED_ENTRY(AUDCLNT_E_DEVICE_IN_USE                , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x0a)),
    HARDCODED_ENTRY(AUDCLNT_E_BUFFER_OPERATION_PENDING     , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x0b)),
    HARDCODED_ENTRY(AUDCLNT_E_THREAD_NOT_REGISTERED        , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x0c)),
    HARDCODED_ENTRY(AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED   , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x0e)),
    HARDCODED_ENTRY(AUDCLNT_E_ENDPOINT_CREATE_FAILED       , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x0f)),
    HARDCODED_ENTRY(AUDCLNT_E_SERVICE_NOT_RUNNING          , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x10)),
    HARDCODED_ENTRY(AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED     , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x11)),
    HARDCODED_ENTRY(AUDCLNT_E_EXCLUSIVE_MODE_ONLY          , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x12)),
    HARDCODED_ENTRY(AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x13)),
    HARDCODED_ENTRY(AUDCLNT_E_EVENTHANDLE_NOT_SET          , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x14)),
    HARDCODED_ENTRY(AUDCLNT_E_INCORRECT_BUFFER_SIZE        , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x15)),
    HARDCODED_ENTRY(AUDCLNT_E_BUFFER_SIZE_ERROR            , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x16)),
    HARDCODED_ENTRY(AUDCLNT_E_CPUUSAGE_EXCEEDED            , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x17)),
    HARDCODED_ENTRY(AUDCLNT_E_BUFFER_ERROR                 , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x18)),
    HARDCODED_ENTRY(AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED      , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x19)),
    HARDCODED_ENTRY(AUDCLNT_E_INVALID_DEVICE_PERIOD        , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x20)),
    HARDCODED_ENTRY(AUDCLNT_E_INVALID_STREAM_FLAG          , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x21)),
    HARDCODED_ENTRY(AUDCLNT_E_ENDPOINT_OFFLOAD_NOT_CAPABLE , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x22)),
    HARDCODED_ENTRY(AUDCLNT_E_OUT_OF_OFFLOAD_RESOURCES     , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x23)),
    HARDCODED_ENTRY(AUDCLNT_E_OFFLOAD_MODE_ONLY            , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x24)),
    HARDCODED_ENTRY(AUDCLNT_E_NONOFFLOAD_MODE_ONLY         , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x25)),
    HARDCODED_ENTRY(AUDCLNT_E_RESOURCES_INVALIDATED        , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x26)),
    HARDCODED_ENTRY(AUDCLNT_E_RAW_MODE_UNSUPPORTED         , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x27)),
    HARDCODED_ENTRY(AUDCLNT_E_ENGINE_PERIODICITY_LOCKED    , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x28)),
    HARDCODED_ENTRY(AUDCLNT_E_ENGINE_FORMAT_LOCKED         , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x29)),
    HARDCODED_ENTRY(AUDCLNT_E_HEADTRACKING_ENABLED         , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x30)),
    HARDCODED_ENTRY(AUDCLNT_E_HEADTRACKING_UNSUPPORTED     , MAKE_HRESULT(SEVERITY_ERROR, 2185, 0x40)),
    HARDCODED_ENTRY(AUDCLNT_S_BUFFER_EMPTY                 , MAKE_SCODE(SEVERITY_SUCCESS, 2185,     1)),
    HARDCODED_ENTRY(AUDCLNT_S_THREAD_ALREADY_REGISTERED    , MAKE_SCODE(SEVERITY_SUCCESS, 2185,     2)),
    HARDCODED_ENTRY(AUDCLNT_S_POSITION_STALLED             , MAKE_SCODE(SEVERITY_SUCCESS, 2185,     3)),
# undef HARDCODED_ENTRY
#endif
    { "Success.", "ERROR_SUCCESS", 0, false },
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
static int CompareWinErrMsg(const void *pv1, const void *pv2) RT_NOTHROW_DEF
{
    PCRTWINERRMSG p1 = (PCRTWINERRMSG)pv1;
    PCRTWINERRMSG p2 = (PCRTWINERRMSG)pv2;
    int iDiff;
    if (p1->iCode < p2->iCode)
        iDiff = -1;
    else if (p1->iCode > p2->iCode)
        iDiff = 1;
    else
        iDiff = 0;
    return iDiff;
}


int main(int argc, char **argv)
{
    /*
     * Parse arguments.
     */
    enum { kMode_All, kMode_OnlyDefines } enmMode;
    if (argc == 3 && strcmp(argv[1], "--all") == 0)
        enmMode = kMode_All;
    else if (argc == 3 && strcmp(argv[1], "--only-defines") == 0)
        enmMode = kMode_OnlyDefines;
    else
    {
        fprintf(stderr,
                "syntax error!\n"
                "Usage: %s <--all|--only-defines> <outfile>\n", argv[0]);
        return RTEXITCODE_SYNTAX;
    }
    const char * const pszOutFile = argv[2];

    /*
     * Sort the table and mark duplicates.
     */
    qsort(g_aStatusMsgs, RT_ELEMENTS(g_aStatusMsgs), sizeof(g_aStatusMsgs[0]), CompareWinErrMsg);

    int rcExit = RTEXITCODE_SUCCESS;
    long iPrev = (long)0x80000000;
    bool fHaveSuccess = false;
    for (size_t i = 0; i < RT_ELEMENTS(g_aStatusMsgs); i++)
    {
        PRTWINERRMSGINT1 pMsg = &g_aStatusMsgs[i];
        if (pMsg->iCode == iPrev && i != 0)
        {
            pMsg->fDuplicate = true;

            if (iPrev == 0)
                continue;

            PRTWINERRMSGINT1 pPrev = &g_aStatusMsgs[i - 1];
            if (strcmp(pMsg->pszDefine, pPrev->pszDefine) == 0)
                continue;
            rcExit = error("Duplicate value %#lx (%ld) - %s and %s\n",
                           (unsigned long)iPrev, iPrev, pMsg->pszDefine, pPrev->pszDefine);
        }
        else
        {
            pMsg->fDuplicate = false;
            iPrev = pMsg->iCode;
            if (iPrev == 0)
                fHaveSuccess = true;
        }
    }
    if (!fHaveSuccess)
        rcExit = error("No zero / success value in the table!\n");

    /*
     * Create a string table for it all.
     */
    BLDPROGSTRTAB StrTab;
    if (!BldProgStrTab_Init(&StrTab, RT_ELEMENTS(g_aStatusMsgs) * 3))
        return error("Out of memory!\n");

    static RTWINERRMSGINT2 s_aStatusMsgs2[RT_ELEMENTS(g_aStatusMsgs)];
    size_t                 cStatusMsgs = 0;
    for (size_t i = 0; i < RT_ELEMENTS(g_aStatusMsgs); i++)
    {
        if (!g_aStatusMsgs[i].fDuplicate)
        {
            s_aStatusMsgs2[cStatusMsgs].iCode     = g_aStatusMsgs[i].iCode;
            s_aStatusMsgs2[cStatusMsgs].pszDefine = g_aStatusMsgs[i].pszDefine;
            BldProgStrTab_AddStringDup(&StrTab, &s_aStatusMsgs2[cStatusMsgs].Define, g_aStatusMsgs[i].pszDefine);
            if (enmMode != kMode_OnlyDefines)
                BldProgStrTab_AddStringDup(&StrTab, &s_aStatusMsgs2[cStatusMsgs].MsgFull, g_aStatusMsgs[i].pszMsgFull);
            cStatusMsgs++;
        }
    }

    if (!BldProgStrTab_CompileIt(&StrTab, true))
        return error("BldProgStrTab_CompileIt failed!\n");

    /*
     * Prepare output file.
     */
    FILE *pOut = fopen(pszOutFile, "wt");
    if (pOut)
    {
        /*
         * Print the table.
         */
        fprintf(pOut,
                "\n"
                "typedef struct RTMSGWINENTRYINT\n"
                "{\n"
                "    uint32_t offDefine  : 20;\n"
                "    uint32_t cchDefine  : 9;\n"
                "%s"
                "    int32_t  iCode;\n"
                "} RTMSGWINENTRYINT;\n"
                "typedef RTMSGWINENTRYINT *PCRTMSGWINENTRYINT;\n"
                "\n"
                "static const RTMSGWINENTRYINT g_aWinMsgs[ /*%lu*/ ] =\n"
                "{\n"
                ,
                enmMode == kMode_All
                ? "    uint32_t offMsgFull : 23;\n"
                  "    uint32_t cchMsgFull : 9;\n" : "",
                (unsigned long)cStatusMsgs);

        if (enmMode == kMode_All)
            for (size_t i = 0; i < cStatusMsgs; i++)
                fprintf(pOut, "/*%#010lx:*/ { %#08x, %3u, %#08x, %3u, %ld },\n",
                        s_aStatusMsgs2[i].iCode,
                        s_aStatusMsgs2[i].Define.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].Define.cchString,
                        s_aStatusMsgs2[i].MsgFull.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].MsgFull.cchString,
                        s_aStatusMsgs2[i].iCode);
        else if (enmMode == kMode_OnlyDefines)
            for (size_t i = 0; i < cStatusMsgs; i++)
                fprintf(pOut, "/*%#010lx:*/ { %#08x, %3u, %ld },\n",
                        s_aStatusMsgs2[i].iCode,
                        s_aStatusMsgs2[i].Define.offStrTab,
                        (unsigned)s_aStatusMsgs2[i].Define.cchString,
                        s_aStatusMsgs2[i].iCode);
        else
            return error("Unsupported message selection (%d)!\n", enmMode);
        fprintf(pOut,
                "};\n"
                "\n");

        BldProgStrTab_WriteStringTable(&StrTab, pOut, "static ", "g_", "WinMsgStrTab");

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

