/* $Id: tstRTGetOptArgv.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTGetOptArgv*.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/path.h>

#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/ldr.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const struct
{
    /** The input string, bourne shell. */
    const char *pszInBourne;
    /** The input string, MS CRT. */
    const char *pszInMsCrt;
    /** Separators, NULL if default. */
    const char *pszSeparators;
    /** The number of arguments. */
    int         cArgs;
    /** Expected argument vector. */
    const char *apszArgs[16];
    /** Expected quoted string, bourne shell. */
    const char *pszOutBourneSh;
    /** Expected quoted string, MS CRT. */
    const char *pszOutMsCrt;
} g_aTests[] =
{
    {
        "0 1 \"\"2'' '3' 4 5 '''''6' 7 8 9 10 11",
        "0 1 \"\"2 3 4 5 \"6\" 7 8 \"\"9\"\" 10 11",
        NULL,
        12,
        {
             "0",
             "1",
             "2",
             "3",
             "4",
             "5",
             "6",
             "7",
             "8",
             "9",
             "10",
             "11",
             NULL, NULL, NULL, NULL,
        },
        "0 1 2 3 4 5 6 7 8 9 10 11",
        "0 1 2 3 4 5 6 7 8 9 10 11"
    },
    {
        "\t\" asdf \"  '\"'xyz  \"\t\"  '\n'    '\"'    \"'\"\n\r  \\\"xyz",
        /* Note! Two things here to make CommandLineArgW happy. First, it doesn't use IFS including newline/return, so
                 we skip that bit of the test.  Second, it uses pre-2008 doubledouble quoting rules, unlike the CRT and IPRT
                 which uses the post-2008 rules. We work around that by putting that test last.
                 See http://www.daviddeley.com/autohotkey/parameters/parameters.htm */
        "\t\" asdf \"  \\\"xyz  \"\t\"  \"\n\"  \"\\\"\"  '  \"\"\"xyz\"",
        NULL,
        7,
        {
            " asdf ",
            "\"xyz",
            "\t",
            "\n",
            "\"",
            "\'",
            "\"xyz",
            NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "' asdf ' '\"xyz' '\t' '\n' '\"' ''\"'\"'' '\"xyz'",
        "\" asdf \" \"\\\"xyz\" \"\t\" \"\n\" \"\\\"\" ' \"\\\"xyz\""
    },
    {
        ":0::1::::2:3:4:5:",
        ":0::1::::2:3:4:5:",
        ":",
        6,
        {
            "0",
            "1",
            "2",
            "3",
            "4",
            "5",
            NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "0 1 2 3 4 5",
        "0 1 2 3 4 5"
    },
    {
        "0:1;2:3;4:5",
        "0:1;2:3;4:5",
        ";;;;;;;;;;;;;;;;;;;;;;:",
        6,
        {
            "0",
            "1",
            "2",
            "3",
            "4",
            "5",
            NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "0 1 2 3 4 5",
        "0 1 2 3 4 5"
    },
    {
        "abcd 'a ' ' b' ' c '",
        "abcd \"a \" \" b\" \" c \"",
        NULL,
        4,
        {
            "abcd",
            "a ",
            " b",
            " c ",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "abcd 'a ' ' b' ' c '",
        "abcd \"a \" \" b\" \" c \""
    },
    {
        "'a\n\\b' 'de'\"'\"'fg' h ''\"'\"''",
        "\"a\n\\b\" de'fg h     \"'\"    ",
        NULL,
        4,
        {
            "a\n\\b",
            "de'fg",
            "h",
            "'",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "'a\n\\b' 'de'\"'\"'fg' h ''\"'\"''",
        "\"a\n\\b\" de'fg h '"
    },
    {
        "arg1 \"arg2=\\\"zyx\\\"\"  'arg3=\\\\\\'",
        "arg1 arg2=\\\"zyx\\\"  arg3=\\\\\\",
        NULL,
        3,
        {
            "arg1",
            "arg2=\"zyx\"",
            "arg3=\\\\\\",
            NULL,  NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "arg1 'arg2=\"zyx\"' 'arg3=\\\\\\'",
        "arg1 \"arg2=\\\"zyx\\\"\" arg3=\\\\\\"
    },
    {
        " a\\\\\\\\b  d\"e f\"g h ij\t",
        " a\\\\b  d\"e f\"g h ij\t",
        NULL,
        4,
        {
            "a\\\\b",
            "de fg",
            "h",
            "ij",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "'a\\\\b' 'de fg' h ij",
        "a\\\\b \"de fg\" h ij",
    }
};



static void tstCheckNativeMsCrtToArgv(const char *pszCmdLine, int cExpectedArgs, const char * const *papszExpectedArgs)
{
#ifdef RT_OS_WINDOWS
    /*
     * Resolve APIs.
     */
    static void     *(__stdcall * s_pfnLocalFree)(void *pvFree);
    static PRTUTF16 *(__stdcall * s_pfnCommandLineToArgvW)(PCRTUTF16 pwszCmdLine, int *pcArgs);
    if (!s_pfnCommandLineToArgvW)
    {
        *(void **)&s_pfnLocalFree = RTLdrGetSystemSymbol("kernel32.dll", "LocalFree");
        RTTESTI_CHECK_RETV(s_pfnLocalFree != NULL);
        *(void **)&s_pfnCommandLineToArgvW = RTLdrGetSystemSymbol("shell32.dll", "CommandLineToArgvW");
        RTTESTI_CHECK_RETV(s_pfnCommandLineToArgvW != NULL);
    }

    /*
     * Calc expected arguments if needed.
     */
    if (cExpectedArgs == -1)
        for (cExpectedArgs = 0; papszExpectedArgs[cExpectedArgs]; cExpectedArgs++)
        { /* nothing */ }

    /*
     * Convert input command line to UTF-16 and call native API.
     */
    RTUTF16 wszCmdLine[1024];
    PRTUTF16 pwszCmdLine = &wszCmdLine[1];
    RTTESTI_CHECK_RC_RETV(RTStrToUtf16Ex(pszCmdLine, RTSTR_MAX, &pwszCmdLine, 1023, NULL), VINF_SUCCESS);
    wszCmdLine[0] = ' ';

    int cArgs = -2;
    PRTUTF16 *papwszArgs = s_pfnCommandLineToArgvW(wszCmdLine, &cArgs);

    /*
     * Check the result.
     */
    if (cArgs - 1 != cExpectedArgs)
        RTTestIFailed("Native returns cArgs=%d, expected %d (cmdline=|%s|)", cArgs - 1, cExpectedArgs, pszCmdLine);
    int cArgsCheck = RT_MIN(cArgs - 1, cExpectedArgs);
    for (int i = 0; i < cArgsCheck; i++)
    {
        char *pszArg = NULL;
        RTTESTI_CHECK_RC_RETV(RTUtf16ToUtf8(papwszArgs[i + 1], &pszArg), VINF_SUCCESS);
        if (strcmp(pszArg, papszExpectedArgs[i]))
            RTTestIFailed("Native returns argv[%i]='%s', expected '%s' (cmdline=|%s|)",
                          i, pszArg, papszExpectedArgs[i], pszCmdLine);
        RTStrFree(pszArg);
    }

    if (papwszArgs)
        s_pfnLocalFree(papwszArgs);
#else
    NOREF(pszCmdLine);
    NOREF(cExpectedArgs);
    NOREF(papszExpectedArgs);
#endif
}


static void tst4(void)
{
    /*
     * Microsoft CRT round-tripping.
     */
    RTTestISub("Round-trips / MS_CRT");
    for (unsigned i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        /* First */
        char **papszArgs1 = NULL;
        int    cArgs1     = -1;
        int rc = RTGetOptArgvFromString(&papszArgs1, &cArgs1, g_aTests[i].pszInMsCrt,
                                        RTGETOPTARGV_CNV_QUOTE_MS_CRT, g_aTests[i].pszSeparators);
        if (rc == VINF_SUCCESS)
        {
            if (cArgs1 != g_aTests[i].cArgs)
                RTTestIFailed("g_aTests[%i]: #1=%d, expected %d", i, cArgs1, g_aTests[i].cArgs);
            for (int iArg = 0; iArg < cArgs1; iArg++)
                if (strcmp(papszArgs1[iArg], g_aTests[i].apszArgs[iArg]) != 0)
                    RTTestIFailed("g_aTests[%i]/1: argv[%i] differs: got '%s', expected '%s' (RTGetOptArgvFromString(,,'%s', '%s'))",
                                  i, iArg, papszArgs1[iArg], g_aTests[i].apszArgs[iArg],
                                  g_aTests[i].pszInMsCrt, g_aTests[i].pszSeparators);
            RTTESTI_CHECK_RETV(papszArgs1[cArgs1] == NULL);
            if (g_aTests[i].pszSeparators == NULL)
                tstCheckNativeMsCrtToArgv(g_aTests[i].pszInMsCrt, g_aTests[i].cArgs, g_aTests[i].apszArgs);

            /* Second */
            char *pszArgs2 = NULL;
            rc = RTGetOptArgvToString(&pszArgs2, papszArgs1, RTGETOPTARGV_CNV_QUOTE_MS_CRT);
            if (rc == VINF_SUCCESS)
            {
                if (strcmp(pszArgs2, g_aTests[i].pszOutMsCrt))
                    RTTestIFailed("g_aTests[%i]/2: '%s', expected '%s'", i, pszArgs2, g_aTests[i].pszOutMsCrt);

                /*
                 * Third
                 */
                char **papszArgs3 = NULL;
                int    cArgs3     = -1;
                rc = RTGetOptArgvFromString(&papszArgs3, &cArgs3, pszArgs2, RTGETOPTARGV_CNV_QUOTE_MS_CRT, NULL);
                if (rc == VINF_SUCCESS)
                {
                    if (cArgs3 != g_aTests[i].cArgs)
                        RTTestIFailed("g_aTests[%i]/3: %d, expected %d", i, cArgs3, g_aTests[i].cArgs);
                    for (int iArg = 0; iArg < cArgs3; iArg++)
                        if (strcmp(papszArgs3[iArg], g_aTests[i].apszArgs[iArg]) != 0)
                            RTTestIFailed("g_aTests[%i]/3: argv[%i] differs: got '%s', expected '%s' (RTGetOptArgvFromString(,,'%s',))",
                                          i, iArg, papszArgs3[iArg], g_aTests[i].apszArgs[iArg], pszArgs2);
                    RTTESTI_CHECK_RETV(papszArgs3[cArgs3] == NULL);
                    if (g_aTests[i].pszSeparators == NULL)
                        tstCheckNativeMsCrtToArgv(pszArgs2, g_aTests[i].cArgs, g_aTests[i].apszArgs);

                    /*
                     * Fourth
                     */
                    char *pszArgs4 = NULL;
                    rc = RTGetOptArgvToString(&pszArgs4, papszArgs3, RTGETOPTARGV_CNV_QUOTE_MS_CRT);
                    if (rc == VINF_SUCCESS)
                    {
                        if (strcmp(pszArgs4, pszArgs2))
                            RTTestIFailed("g_aTests[%i]/4: '%s' does not match #4='%s'", i, pszArgs2, pszArgs4);
                        RTStrFree(pszArgs4);
                    }
                    else
                        RTTestIFailed("g_aTests[%i]/4: RTGetOptArgvToString() -> %Rrc", i, rc);
                    RTGetOptArgvFree(papszArgs3);
                }
                else
                    RTTestIFailed("g_aTests[%i]/3: RTGetOptArgvFromString() -> %Rrc", i, rc);
                RTStrFree(pszArgs2);
            }
            else
                RTTestIFailed("g_aTests[%i]/2: RTGetOptArgvToString() -> %Rrc", i, rc);
            RTGetOptArgvFree(papszArgs1);
        }
        else
            RTTestIFailed("g_aTests[%i]/1: RTGetOptArgvFromString(,,'%s', '%s') -> %Rrc",
                          i, g_aTests[i].pszInMsCrt, g_aTests[i].pszSeparators, rc);
    }

}



static void tst3(void)
{
    /*
     * Bourne shell round-tripping.
     */
    RTTestISub("Round-trips / BOURNE_SH");
    for (unsigned i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        /* First */
        char **papszArgs1 = NULL;
        int    cArgs1     = -1;
        int rc = RTGetOptArgvFromString(&papszArgs1, &cArgs1, g_aTests[i].pszInBourne,
                                        RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, g_aTests[i].pszSeparators);
        if (rc == VINF_SUCCESS)
        {
            if (cArgs1 != g_aTests[i].cArgs)
                RTTestIFailed("g_aTests[%i]: #1=%d, expected %d", i, cArgs1, g_aTests[i].cArgs);
            for (int iArg = 0; iArg < cArgs1; iArg++)
                if (strcmp(papszArgs1[iArg], g_aTests[i].apszArgs[iArg]) != 0)
                    RTTestIFailed("g_aTests[%i]/1: argv[%i] differs: got '%s', expected '%s' (RTGetOptArgvFromString(,,'%s', '%s'))",
                                  i, iArg, papszArgs1[iArg], g_aTests[i].apszArgs[iArg],
                                  g_aTests[i].pszInBourne, g_aTests[i].pszSeparators);
            RTTESTI_CHECK_RETV(papszArgs1[cArgs1] == NULL);

            /* Second */
            char *pszArgs2 = NULL;
            rc = RTGetOptArgvToString(&pszArgs2, papszArgs1, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
            if (rc == VINF_SUCCESS)
            {
                if (strcmp(pszArgs2, g_aTests[i].pszOutBourneSh))
                    RTTestIFailed("g_aTests[%i]/2: '%s', expected '%s'", i, pszArgs2, g_aTests[i].pszOutBourneSh);

                /*
                 * Third
                 */
                char **papszArgs3 = NULL;
                int    cArgs3     = -1;
                rc = RTGetOptArgvFromString(&papszArgs3, &cArgs3, pszArgs2, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, NULL);
                if (rc == VINF_SUCCESS)
                {
                    if (cArgs3 != g_aTests[i].cArgs)
                        RTTestIFailed("g_aTests[%i]/3: %d, expected %d", i, cArgs3, g_aTests[i].cArgs);
                    for (int iArg = 0; iArg < cArgs3; iArg++)
                        if (strcmp(papszArgs3[iArg], g_aTests[i].apszArgs[iArg]) != 0)
                            RTTestIFailed("g_aTests[%i]/3: argv[%i] differs: got '%s', expected '%s' (RTGetOptArgvFromString(,,'%s',))",
                                          i, iArg, papszArgs3[iArg], g_aTests[i].apszArgs[iArg], pszArgs2);
                    RTTESTI_CHECK_RETV(papszArgs3[cArgs3] == NULL);

                    /*
                     * Fourth
                     */
                    char *pszArgs4 = NULL;
                    rc = RTGetOptArgvToString(&pszArgs4, papszArgs3, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
                    if (rc == VINF_SUCCESS)
                    {
                        if (strcmp(pszArgs4, pszArgs2))
                            RTTestIFailed("g_aTests[%i]/4: '%s' does not match #4='%s'", i, pszArgs2, pszArgs4);
                        RTStrFree(pszArgs4);
                    }
                    else
                        RTTestIFailed("g_aTests[%i]/4: RTGetOptArgvToString() -> %Rrc", i, rc);
                    RTGetOptArgvFree(papszArgs3);
                }
                else
                    RTTestIFailed("g_aTests[%i]/3: RTGetOptArgvFromString() -> %Rrc", i, rc);
                RTStrFree(pszArgs2);
            }
            else
                RTTestIFailed("g_aTests[%i]/2: RTGetOptArgvToString() -> %Rrc", i, rc);
            RTGetOptArgvFree(papszArgs1);
        }
        else
            RTTestIFailed("g_aTests[%i]/1: RTGetOptArgvFromString(,,'%s', '%s') -> %Rrc",
                          i, g_aTests[i].pszInBourne, g_aTests[i].pszSeparators, rc);
    }
}


/* Global to avoid weird C4640 warning about "construction of local static object is not thread-safe". */
static const struct
{
    const char * const      apszArgs[5];
    const char             *pszCmdLine;
} g_aMscCrtTests[] =
{
    {
        { "abcd", "a ", " b", " c ", NULL },
        "abcd \"a \" \" b\" \" c \""
    },
    {
        { "a\\\\\\b", "de fg", "h", NULL, NULL },
        "a\\\\\\b \"de fg\" h"
    },
    {
        { "a\\\"b", "c", "d", "\"", NULL },
        "\"a\\\\\\\"b\" c d \"\\\"\""
    },
    {
        { "a\\\\b c", "d", "e", " \\", NULL },
        "\"a\\\\b c\" d e \" \\\\\""
    },
};

static void tst2(void)
{
    RTTestISub("RTGetOptArgvToString / MS_CRT");

    for (size_t i = 0; i < RT_ELEMENTS(g_aMscCrtTests); i++)
    {
        char *pszCmdLine = NULL;
        int rc = RTGetOptArgvToString(&pszCmdLine, g_aMscCrtTests[i].apszArgs, RTGETOPTARGV_CNV_QUOTE_MS_CRT);
        RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);
        if (!strcmp(g_aMscCrtTests[i].pszCmdLine, pszCmdLine))
            tstCheckNativeMsCrtToArgv(pszCmdLine, -1, g_aMscCrtTests[i].apszArgs);
        else
            RTTestIFailed("g_aTest[%i] failed:\n"
                          " got      '%s'\n"
                          " expected '%s'\n",
                          i, pszCmdLine, g_aMscCrtTests[i].pszCmdLine);
        RTStrFree(pszCmdLine);
    }

    for (size_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        char *pszCmdLine = NULL;
        int rc = RTGetOptArgvToString(&pszCmdLine, g_aTests[i].apszArgs, RTGETOPTARGV_CNV_QUOTE_MS_CRT);
        RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);
        if (!strcmp(g_aTests[i].pszOutMsCrt, pszCmdLine))
            tstCheckNativeMsCrtToArgv(pszCmdLine, g_aTests[i].cArgs, g_aTests[i].apszArgs);
        else
            RTTestIFailed("g_aTests[%i] failed:\n"
                          " got      |%s|\n"
                          " expected |%s|\n",
                          i, pszCmdLine, g_aTests[i].pszOutMsCrt);
        RTStrFree(pszCmdLine);
    }



    RTTestISub("RTGetOptArgvToString / BOURNE_SH");

    for (size_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        char *pszCmdLine = NULL;
        int rc = RTGetOptArgvToString(&pszCmdLine, g_aTests[i].apszArgs, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
        RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);
        if (strcmp(g_aTests[i].pszOutBourneSh, pszCmdLine))
            RTTestIFailed("g_aTests[%i] failed:\n"
                          " got      |%s|\n"
                          " expected |%s|\n",
                          i, pszCmdLine, g_aTests[i].pszOutBourneSh);
        RTStrFree(pszCmdLine);
    }
}

static void tst1(void)
{
    RTTestISub("RTGetOptArgvFromString");
    char **papszArgs = NULL;
    int    cArgs = -1;
    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, "", RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 0);
    RTTESTI_CHECK_RETV(papszArgs);
    RTTESTI_CHECK_RETV(!papszArgs[0]);
    RTGetOptArgvFree(papszArgs);

    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, "0 1 \"\"2'' '3' 4 5 '''''6' 7 8 9 10 11",
                                                 RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 12);
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[0], "0"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[1], "1"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[2], "2"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[3], "3"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[4], "4"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[5], "5"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[6], "6"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[7], "7"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[8], "8"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[9], "9"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[10], "10"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[11], "11"));
    RTTESTI_CHECK_RETV(!papszArgs[12]);
    RTGetOptArgvFree(papszArgs);

    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, "\t\" asdf \"  '\"'xyz  \"\t\"  '\n'  '\"'  \"'\"\n\r ",
                                                 RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 6);
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[0], " asdf "));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[1], "\"xyz"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[2], "\t"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[3], "\n"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[4], "\""));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[5], "\'"));
    RTTESTI_CHECK_RETV(!papszArgs[6]);
    RTGetOptArgvFree(papszArgs);

    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, ":0::1::::2:3:4:5:",
                                                 RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, ":"), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 6);
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[0], "0"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[1], "1"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[2], "2"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[3], "3"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[4], "4"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[5], "5"));
    RTTESTI_CHECK_RETV(!papszArgs[6]);
    RTGetOptArgvFree(papszArgs);

    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, "0:1;2:3;4:5", RTGETOPTARGV_CNV_QUOTE_BOURNE_SH,
                                                 ";;;;;;;;;;;;;;;;;;;;;;:"), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 6);
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[0], "0"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[1], "1"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[2], "2"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[3], "3"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[4], "4"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[5], "5"));
    RTTESTI_CHECK_RETV(!papszArgs[6]);
    RTGetOptArgvFree(papszArgs);

    /*
     * Tests from the list.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        papszArgs = NULL;
        cArgs = -1;
        int rc = RTGetOptArgvFromString(&papszArgs, &cArgs, g_aTests[i].pszInBourne, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH,
                                        g_aTests[i].pszSeparators);
        if (rc == VINF_SUCCESS)
        {
            if (cArgs == g_aTests[i].cArgs)
            {
                for (int iArg = 0; iArg < cArgs; iArg++)
                    if (strcmp(papszArgs[iArg], g_aTests[i].apszArgs[iArg]) != 0)
                        RTTestIFailed("g_aTests[%i]: argv[%i] differs: got '%s', expected '%s' (RTGetOptArgvFromString(,,'%s', '%s'))",
                                      i, iArg, papszArgs[iArg], g_aTests[i].apszArgs[iArg],
                                      g_aTests[i].pszInBourne, g_aTests[i].pszSeparators);
                RTTESTI_CHECK_RETV(papszArgs[cArgs] == NULL);
            }
            else
                RTTestIFailed("g_aTests[%i]: cArgs=%u, expected %u for RTGetOptArgvFromString(,,'%s', '%s')",
                              i, cArgs, g_aTests[i].cArgs, g_aTests[i].pszInBourne, g_aTests[i].pszSeparators);
            RTGetOptArgvFree(papszArgs);
        }
        else
            RTTestIFailed("g_aTests[%i]: RTGetOptArgvFromString(,,'%s', '%s') -> %Rrc",
                          i, g_aTests[i].pszInBourne, g_aTests[i].pszSeparators, rc);
    }
}


int main()
{
    /*
     * Init RT+Test.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTGetOptArgv", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * The test.
     */
    tst1();
    tst2();
    tst4();
    tst3();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

