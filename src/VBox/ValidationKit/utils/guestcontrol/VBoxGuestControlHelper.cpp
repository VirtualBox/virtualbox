/* $Id: VBoxGuestControlHelper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Helper binary for Guest Control tests.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/getopt.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif

#include <package-generated.h>
#include "product-generated.h"

#include <VBox/version.h>
#include <VBox/log.h>



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#ifdef RT_OS_WINDOWS
 /* kernel32.dll: */
 typedef BOOL (WINAPI *PFNPROCESSIDTOSESSIONID)(DWORD, DWORD *);

 /* kernel32.dll: */
 static PFNPROCESSIDTOSESSIONID g_pfnProcessIdToSessionId = NULL;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Guest Control test helper command table entry.
 */
typedef struct GSTCTLHLPCMD
{
    /** The command name. */
    const char     *pszCommand;
    /** The command handler.   */
    DECLCALLBACKMEMBER(RTEXITCODE, pfnHandler,(PRTGETOPTSTATE pGetState));

    /** Command description.   */
    const char     *pszDesc;
    /** Options array.  */
    PCRTGETOPTDEF   paOptions;
    /** Number of options in the option array. */
    size_t          cOptions;
    /** Gets help for an option. */
    DECLCALLBACKMEMBER(const char *, pfnOptionHelp,(PCRTGETOPTDEF pOpt));
} GSTCTLHLPCMD;
/** Pointer to a const GSTCTLHLPCMD command entry. */
typedef GSTCTLHLPCMD const *PCGSTCTLHLPCMD;

/**
 * Long option values for the 'show' command.
 */
enum
{
    GSTCTLHLP_SHOW_OPT_VERSION        = 900,
    GSTCTLHLP_SHOW_OPT_WIN_SESSION_ID
};

/**
 * Command line parameters for test mode.
 */
static const RTGETOPTDEF g_aCmdShowOptions[] =
{
    { "version",                  GSTCTLHLP_SHOW_OPT_VERSION,                                 RTGETOPT_REQ_NOTHING  },
#ifdef RT_OS_WINDOWS
    { "win-session-id",           GSTCTLHLP_SHOW_OPT_WIN_SESSION_ID,                          RTGETOPT_REQ_NOTHING  },
#endif
};

/**
 * Shows tool version.
 */
static RTEXITCODE gstCtlHlpVersion(void)
{
    RTPrintf("%s\n", RTBldCfgRevisionStr());
    return RTEXITCODE_SUCCESS;
}

#ifdef RT_OS_WINDOWS
/**
 * Resolves impports for Windows platforms.
 */
static void gstCtlHlpWinResolveImports(void)
{
    RTLDRMOD hLdrMod;

    /* kernel32: */
    int rc = RTLdrLoadSystem("kernel32.dll", true /*fNoUnload*/, &hLdrMod);
    if (RT_SUCCESS(rc))
        RTLdrGetSymbol(hLdrMod, "ProcessIdToSessionId", (void **)&g_pfnProcessIdToSessionId);
}

/**
 * Shows the Windows session ID.
 *
 * @returns 1000 + "session ID" on success as exit code.
 * @returns RTEXITCODE on failure.
 */
static DECLCALLBACK(RTEXITCODE) gstCtlHlpCmdShowWinSessionId(void)
{
    DWORD dwSessionId = 0;
    if (g_pfnProcessIdToSessionId)
    {
        if (g_pfnProcessIdToSessionId(GetCurrentProcessId(), &dwSessionId))
        {
            RTPrintf("Session ID: %ld\n", dwSessionId);
            return RTEXITCODE(1000 + dwSessionId);
        }
        else
        {
            DWORD const dwErr = GetLastError();
            RTMsgError("Getting session ID failed: %Rrc (%#x)\n", RTErrConvertFromWin32(dwErr), dwErr);
        }
    }
    else
        RTMsgError("Getting session ID not available on this OS\n");

    return RTEXITCODE_FAILURE;
}
#endif /* RT_OS_WINDOWS */

/**
 * Handles the "show" command.
 *
 * @returns RTEXITCODE
 * @param   pGetState           GetOpt state to use.
 */
static DECLCALLBACK(RTEXITCODE) gstCtlHlpCmdShow(PRTGETOPTSTATE pGetState)
{
    int           ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(pGetState, &ValueUnion)))
    {
        switch (ch)
        {
            case GSTCTLHLP_SHOW_OPT_VERSION:
                return gstCtlHlpVersion();
#ifdef RT_OS_WINDOWS
            case GSTCTLHLP_SHOW_OPT_WIN_SESSION_ID:
                return gstCtlHlpCmdShowWinSessionId();
#endif /* RT_OS_WINDOWS */

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    return RTEXITCODE_SUCCESS;
}

/**
 * Option help for the 'show' command.
 *
 * @returns Help text if found, or NULL if not found.
 * @param   pOpt                GetOpt definition to return help text for.
 */
static DECLCALLBACK(const char *) gstCtlHlpCmdShowHelp(PCRTGETOPTDEF pOpt)
{
    switch (pOpt->iShort)
    {
        case GSTCTLHLP_SHOW_OPT_VERSION:        return "Shows the program version.\n";
#ifdef RT_OS_WINDOWS
        case GSTCTLHLP_SHOW_OPT_WIN_SESSION_ID: return "Shows this process Windows session ID.\n"
                                                       "    Exit code is 1000 + <session ID>.";
#endif
        default:
            break;
    }
    return NULL;
}

/**
 * Option defintions for the 'show' command.
 */
const GSTCTLHLPCMD g_CmdShow =
{
    "show",
    gstCtlHlpCmdShow,
    "Shows various information and exits.",
    g_aCmdShowOptions,
    RT_ELEMENTS(g_aCmdShowOptions),
    gstCtlHlpCmdShowHelp
};

/**
 * Commands.
 */
static const GSTCTLHLPCMD * const g_apCommands[] =
{
    &g_CmdShow
};

/**
 * Shows tool usage text.
 */
static RTEXITCODE gstCtlHlpUsage(PRTSTREAM pStrm, PCGSTCTLHLPCMD pOnlyCmd)
{
    RTStrmPrintf(pStrm, "usage: %s [global options] <command> [command-options]\n", RTProcShortName());
    RTStrmPrintf(pStrm,
                 "\n"
                 "Global Options:\n"
                 "  -V, --version\n"
                 "    Displays version\n"
                 "  -h, -?, --help\n"
                 "    Displays help\n"
                 );

    for (uintptr_t iCmd = 0; iCmd < RT_ELEMENTS(g_apCommands); iCmd++)
    {
        PCGSTCTLHLPCMD const pCmd = g_apCommands[iCmd];
        if (!pOnlyCmd || pCmd == pOnlyCmd)
        {
            RTStrmPrintf(pStrm,
                         "\n"
                         "Command '%s':\n"
                         "    %s\n"
                         "Options for '%s':\n",
                         pCmd->pszCommand, pCmd->pszDesc, pCmd->pszCommand);
            PCRTGETOPTDEF const paOptions = pCmd->paOptions;
            for (unsigned i = 0; i < pCmd->cOptions; i++)
            {
                if (RT_C_IS_PRINT(paOptions[i].iShort))
                    RTStrmPrintf(pStrm, "  -%c, %s\n", paOptions[i].iShort, paOptions[i].pszLong);
                else
                    RTStrmPrintf(pStrm, "  %s\n", paOptions[i].pszLong);

                const char *pszHelp = NULL;
                if (pCmd->pfnOptionHelp)
                    pszHelp = pCmd->pfnOptionHelp(&paOptions[i]);
                if (pszHelp)
                    RTStrmPrintf(pStrm, "    %s\n", pszHelp);
            }
        }
    }

    return RTEXITCODE_SUCCESS;
}

/**
 * Lists the commands and their descriptions.
 */
static RTEXITCODE gstCtlHlplLstCommands(PRTSTREAM pStrm)
{
    RTStrmPrintf(pStrm, "Commands:\n");
    for (uintptr_t iCmd = 0; iCmd < RT_ELEMENTS(g_apCommands); iCmd++)
        RTStrmPrintf(pStrm, "%8s - %s\n", g_apCommands[iCmd]->pszCommand, g_apCommands[iCmd]->pszDesc);
    return RTEXITCODE_SUCCESS;
}

/**
 * Common command line parameters.
 */
static const RTGETOPTDEF g_aCmdCommonOptions[] =
{
    { "--help",             'h',                                        RTGETOPT_REQ_NOTHING },
    { "--version",          'V',                                        RTGETOPT_REQ_NOTHING }
};

int main(int argc, char **argv)
{
    /*
     * Init IPRT.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTGETOPTSTATE GetState;
    rc = RTGetOptInit(&GetState, argc, argv, g_aCmdCommonOptions,
                      RT_ELEMENTS(g_aCmdCommonOptions), 1 /*idxFirst*/, 0 /*fFlags - must not sort! */);
    AssertRCReturn(rc, RTEXITCODE_INIT);

#ifdef RT_OS_WINDOWS
    gstCtlHlpWinResolveImports();
#endif

    int           ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'V':
                return gstCtlHlpVersion();

            case 'h':
                return gstCtlHlpUsage(g_pStdOut, NULL);

            case VINF_GETOPT_NOT_OPTION:
            {
                for (uintptr_t iCmd = 0; iCmd < RT_ELEMENTS(g_apCommands); iCmd++)
                {
                    PCGSTCTLHLPCMD const pCmd = g_apCommands[iCmd];
                    if (strcmp(ValueUnion.psz, pCmd->pszCommand) == 0)
                    {
                        /* Count the combined option definitions:  */
                        size_t cCombinedOptions  = pCmd->cOptions;

                        /* Combine the option definitions: */
                        PRTGETOPTDEF paCombinedOptions = (PRTGETOPTDEF)RTMemAlloc(cCombinedOptions * sizeof(RTGETOPTDEF));
                        if (paCombinedOptions)
                        {
                            uint32_t idxOpts = 0;

                            memcpy(&paCombinedOptions[idxOpts], pCmd->paOptions, pCmd->cOptions * sizeof(RTGETOPTDEF));
                            idxOpts += (uint32_t)pCmd->cOptions;

                            /* Re-initialize the option getter state and pass it to the command handler. */
                            rc = RTGetOptInit(&GetState, argc, argv, paCombinedOptions, cCombinedOptions,
                                              GetState.iNext /*idxFirst*/, RTGETOPTINIT_FLAGS_OPTS_FIRST);
                            if (RT_SUCCESS(rc))
                            {
                                RTEXITCODE rcExit = pCmd->pfnHandler(&GetState);
                                RTMemFree(paCombinedOptions);
                                return rcExit;
                            }
                            RTMemFree(paCombinedOptions);
                            return RTMsgErrorExitFailure("RTGetOptInit failed for '%s': %Rrc", ValueUnion.psz, rc);
                        }
                        return RTMsgErrorExitFailure("Out of memory!");
                    }
                }
                RTMsgError("Unknown command '%s'!\n", ValueUnion.psz);
                gstCtlHlplLstCommands(g_pStdErr);
                break;
            }

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    RTMsgInfo("No command given. Try '%s --help'.\n", RTProcShortName());
    return RTEXITCODE_SUCCESS;
}
