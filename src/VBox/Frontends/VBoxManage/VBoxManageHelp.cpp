/* $Id: VBoxManageHelp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - help and other message output.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/version.h>

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/stream.h>
#include <iprt/message.h>
#include <iprt/uni.h>

#include "VBoxManage.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** If the usage is the given number of length long or longer, the error is
 * repeated so the user can actually see it. */
#define ERROR_REPEAT_AFTER_USAGE_LENGTH 16


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
DECLARE_TRANSLATION_CONTEXT(Help);

static enum HELP_CMD_VBOXMANAGE    g_enmCurCommand = HELP_CMD_COMMON;
/** The scope mask for the current subcommand. */
static uint64_t                    g_fCurSubcommandScope = RTMSGREFENTRYSTR_SCOPE_GLOBAL;

/**
 * Sets the current command.
 *
 * This affects future calls to error and help functions.
 *
 * @param   enmCommand          The command.
 */
void setCurrentCommand(enum HELP_CMD_VBOXMANAGE enmCommand)
{
    Assert(g_enmCurCommand == HELP_CMD_COMMON);
    g_enmCurCommand       = enmCommand;
    g_fCurSubcommandScope = RTMSGREFENTRYSTR_SCOPE_GLOBAL;
}


/**
 * Sets the current subcommand.
 *
 * This affects future calls to error and help functions.
 *
 * @param   fSubcommandScope    The subcommand scope.
 */
void setCurrentSubcommand(uint64_t fSubcommandScope)
{
    g_fCurSubcommandScope = fSubcommandScope;
}


/**
 * Takes first char and make it uppercase.
 *
 * @returns pointer to string starting from next char.
 * @param   pszSrc      Source string.
 * @param   pszDst      Pointer to buffer to place first char uppercase.
 */
static const char *captialize(const char *pszSrc, char *pszDst)
{
    *RTStrPutCp(pszDst, RTUniCpToUpper(RTStrGetCp(pszSrc))) = '\0';
    return RTStrNextCp(pszSrc);
}


/**
 * Prints brief help for a command or subcommand.
 *
 * @returns Number of lines written.
 * @param   enmCommand          The command.
 * @param   fSubcommandScope    The subcommand scope, REFENTRYSTR_SCOPE_GLOBAL
 *                              for all.
 * @param   pStrm               The output stream.
 */
static uint32_t printBriefCommandOrSubcommandHelp(enum HELP_CMD_VBOXMANAGE enmCommand, uint64_t fSubcommandScope, PRTSTREAM pStrm)
{
    /*
     * Try to find translated, falling back untranslated.
     */
    uint32_t                  cLinesWritten       = 0;
    uint32_t                  cPendingBlankLines  = 0;
    uint32_t                  cFound              = 0;
    PCHELP_LANG_ENTRY_T const apHelpLangEntries[] =
    {
        ASMAtomicUoReadPtrT(&g_pHelpLangEntry, PCHELP_LANG_ENTRY_T),
#ifdef VBOX_WITH_VBOXMANAGE_NLS
        &g_aHelpLangEntries[0]
#endif
    };
    for (uint32_t k = 0; k < RT_ELEMENTS(apHelpLangEntries) && cFound == 0; k++)
    {
        /* skip if english is used */
        if (k > 0 && apHelpLangEntries[k] == apHelpLangEntries[0])
            break;
        uint32_t const cHelpEntries = *apHelpLangEntries[k]->pcHelpEntries;
        for (uint32_t i = 0; i < cHelpEntries; i++)
        {
            PCRTMSGREFENTRY pHelp = apHelpLangEntries[k]->papHelpEntries[i];
            if (   pHelp->idInternal == (int64_t)enmCommand
                || enmCommand == HELP_CMD_COMMON)
            {
                cFound++;
                if (cFound == 1)
                {
                    if (fSubcommandScope == RTMSGREFENTRYSTR_SCOPE_GLOBAL)
                    {
                        char szFirstChar[8];
                        RTStrmPrintf(pStrm, Help::tr("Usage - %s%s:\n"), szFirstChar, captialize(pHelp->pszBrief, szFirstChar));
                    }
                    else
                        RTStrmPrintf(pStrm, Help::tr("Usage:\n"));
                }
                RTMsgRefEntryPrintStringTable(pStrm, &pHelp->Synopsis, fSubcommandScope, &cPendingBlankLines, &cLinesWritten);
                if (!cPendingBlankLines)
                    cPendingBlankLines = 1;
            }
        }
    }
    Assert(cFound > 0);
    return cLinesWritten;
}


/**
 * Prints the brief usage information for the current (sub)command.
 *
 * @param   pStrm               The output stream.
 */
void printUsage(PRTSTREAM pStrm)
{
    printBriefCommandOrSubcommandHelp(g_enmCurCommand, g_fCurSubcommandScope, pStrm);
}


/**
 * Prints full help for a command or subcommand.
 *
 * @param   enmCommand          The command.
 * @param   fSubcommandScope    The subcommand scope, REFENTRYSTR_SCOPE_GLOBAL
 *                              for all.
 * @param   pStrm               The output stream.
 */
static void printFullCommandOrSubcommandHelp(enum HELP_CMD_VBOXMANAGE enmCommand, uint64_t fSubcommandScope, PRTSTREAM pStrm)
{
    /* Try to find translated, then untranslated */
    uint32_t                  cPendingBlankLines  = 0;
    uint32_t                  cFound              = 0;
    PCHELP_LANG_ENTRY_T const apHelpLangEntries[] =
    {
        ASMAtomicUoReadPtrT(&g_pHelpLangEntry, PCHELP_LANG_ENTRY_T),
#ifdef VBOX_WITH_VBOXMANAGE_NLS
        &g_aHelpLangEntries[0]
#endif
    };
    for (uint32_t k = 0; k < RT_ELEMENTS(apHelpLangEntries) && cFound == 0; k++)
    {
        /* skip if english is used */
        if (k > 0 && apHelpLangEntries[k] == apHelpLangEntries[0])
            break;
        uint32_t const cHelpEntries = *apHelpLangEntries[k]->pcHelpEntries;
        for (uint32_t i = 0; i < cHelpEntries; i++)
        {
            PCRTMSGREFENTRY pHelp = apHelpLangEntries[k]->papHelpEntries[i];

            if (   pHelp->idInternal == (int64_t)enmCommand
                || enmCommand == HELP_CMD_COMMON)
            {
                cFound++;
                RTMsgRefEntryPrintStringTable(pStrm, &pHelp->Help, fSubcommandScope, &cPendingBlankLines, NULL /*pcLinesWritten*/);
                if (cPendingBlankLines < 2)
                    cPendingBlankLines = 2;
            }
        }
    }
    Assert(cFound > 0);
}


/**
 * Prints the full help for the current (sub)command.
 *
 * @param   pStrm               The output stream.
 */
void printHelp(PRTSTREAM pStrm)
{
    printFullCommandOrSubcommandHelp(g_enmCurCommand, g_fCurSubcommandScope, pStrm);
}


/**
 * Display no subcommand error message and current command usage.
 *
 * @returns RTEXITCODE_SYNTAX.
 */
RTEXITCODE errorNoSubcommand(void)
{
    Assert(g_enmCurCommand != HELP_CMD_VBOXMANAGE_INVALID);
    Assert(g_fCurSubcommandScope == RTMSGREFENTRYSTR_SCOPE_GLOBAL);

    return errorSyntax(Help::tr("No subcommand specified"));
}


/**
 * Display unknown subcommand error message and current command usage.
 *
 * May show full command help instead if the subcommand is a common help option.
 *
 * @returns RTEXITCODE_SYNTAX, or RTEXITCODE_SUCCESS if common help option.
 * @param   pszSubcommand       The name of the alleged subcommand.
 */
RTEXITCODE errorUnknownSubcommand(const char *pszSubcommand)
{
    Assert(g_enmCurCommand != HELP_CMD_VBOXMANAGE_INVALID);
    Assert(g_fCurSubcommandScope == RTMSGREFENTRYSTR_SCOPE_GLOBAL);

    /* check if help was requested. */
    if (   strcmp(pszSubcommand, "--help") == 0
        || strcmp(pszSubcommand, "-h") == 0
        || strcmp(pszSubcommand, "-?") == 0)
    {
        printFullCommandOrSubcommandHelp(g_enmCurCommand, g_fCurSubcommandScope, g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    return errorSyntax(Help::tr("Unknown subcommand: %s"), pszSubcommand);
}


/**
 * Display too many parameters error message and current command usage.
 *
 * May show full command help instead if the subcommand is a common help option.
 *
 * @returns RTEXITCODE_SYNTAX, or RTEXITCODE_SUCCESS if common help option.
 * @param   papszArgs           The first unwanted parameter.  Terminated by
 *                              NULL entry.
 */
RTEXITCODE errorTooManyParameters(char **papszArgs)
{
    Assert(g_enmCurCommand != HELP_CMD_VBOXMANAGE_INVALID);
    Assert(g_fCurSubcommandScope != RTMSGREFENTRYSTR_SCOPE_GLOBAL);

    /* check if help was requested. */
    if (papszArgs)
    {
        for (uint32_t i = 0; papszArgs[i]; i++)
            if (   strcmp(papszArgs[i], "--help") == 0
                || strcmp(papszArgs[i], "-h") == 0
                || strcmp(papszArgs[i], "-?") == 0)
            {
                printFullCommandOrSubcommandHelp(g_enmCurCommand, g_fCurSubcommandScope, g_pStdOut);
                return RTEXITCODE_SUCCESS;
            }
            else if (!strcmp(papszArgs[i], "--"))
                break;
    }

    return errorSyntax(Help::tr("Too many parameters"));
}


/**
 * Display current (sub)command usage and the custom error message.
 *
 * @returns RTEXITCODE_SYNTAX.
 * @param   pszFormat           Custom error message format string.
 * @param   va                  Format arguments.
 */
RTEXITCODE errorSyntaxV(const char *pszFormat, va_list va)
{
    Assert(g_enmCurCommand != HELP_CMD_VBOXMANAGE_INVALID);

    showLogo(g_pStdErr);

    va_list vaCopy;
    va_copy(vaCopy, va);
    RTMsgErrorV(pszFormat, vaCopy);
    va_end(vaCopy);

    RTStrmPutCh(g_pStdErr, '\n');
    if (   printBriefCommandOrSubcommandHelp(g_enmCurCommand, g_fCurSubcommandScope, g_pStdErr)
        >= ERROR_REPEAT_AFTER_USAGE_LENGTH)
    {
        /* Usage was very long, repeat the error message. */
        RTStrmPutCh(g_pStdErr, '\n');
        RTMsgErrorV(pszFormat, va);
    }
    return RTEXITCODE_SYNTAX;
}


/**
 * Display current (sub)command usage and the custom error message.
 *
 * @returns RTEXITCODE_SYNTAX.
 * @param   pszFormat           Custom error message format string.
 * @param   ...                 Format arguments.
 */
RTEXITCODE errorSyntax(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTEXITCODE rcExit = errorSyntaxV(pszFormat, va);
    va_end(va);
    return rcExit;
}


/**
 * Display current (sub)command usage and the custom error message.
 *
 * @returns E_INVALIDARG
 * @param   pszFormat           Custom error message format string.
 * @param   ...                 Format arguments.
 */
HRESULT errorSyntaxHr(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    errorSyntaxV(pszFormat, va);
    va_end(va);
    return E_INVALIDARG;
}


/**
 * Print an error message without the syntax stuff.
 *
 * @returns RTEXITCODE_SYNTAX.
 */
RTEXITCODE errorArgument(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    RTMsgErrorV(pszFormat, args);
    va_end(args);
    return RTEXITCODE_SYNTAX;
}


/**
 * Print an error message without the syntax stuff.
 *
 * @returns E_INVALIDARG.
 */
HRESULT errorArgumentHr(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    RTMsgErrorV(pszFormat, args);
    va_end(args);
    return E_INVALIDARG;
}


/**
 * Worker for errorGetOpt.
 *
 * @param   rcGetOpt            The RTGetOpt return value.
 * @param   pValueUnion         The value union returned by RTGetOpt.
 */
static void errorGetOptWorker(int rcGetOpt, union RTGETOPTUNION const *pValueUnion)
{
    if (rcGetOpt == VINF_GETOPT_NOT_OPTION)
        RTMsgError(Help::tr("Invalid parameter '%s'"), pValueUnion->psz);
    else if (rcGetOpt > 0)
    {
        if (RT_C_IS_PRINT(rcGetOpt))
            RTMsgError(Help::tr("Invalid option -%c"), rcGetOpt);
        else
            RTMsgError(Help::tr("Invalid option case %i"), rcGetOpt);
    }
    else if (rcGetOpt == VERR_GETOPT_UNKNOWN_OPTION)
        RTMsgError(Help::tr("Unknown option: %s"), pValueUnion->psz);
    else if (rcGetOpt == VERR_GETOPT_INVALID_ARGUMENT_FORMAT)
        RTMsgError(Help::tr("Invalid argument format: %s"), pValueUnion->psz);
    else if (pValueUnion->pDef)
        RTMsgError("%s: %Rrs", pValueUnion->pDef->pszLong, rcGetOpt);
    else
        RTMsgError("%Rrs", rcGetOpt);
}


/**
 * For use to deal with RTGetOptFetchValue failures.
 *
 * @retval  RTEXITCODE_SYNTAX
 * @param   iValueNo            The value number being fetched, counting the
 *                              RTGetOpt value as zero and the first
 *                              RTGetOptFetchValue call as one.
 * @param   pszOption           The option being parsed.
 * @param   rcGetOptFetchValue  The status returned by RTGetOptFetchValue.
 * @param   pValueUnion         The value union returned by the fetch.
 */
RTEXITCODE errorFetchValue(int iValueNo, const char *pszOption, int rcGetOptFetchValue, union RTGETOPTUNION const *pValueUnion)
{
    Assert(g_enmCurCommand != HELP_CMD_VBOXMANAGE_INVALID);
    showLogo(g_pStdErr);
    if (rcGetOptFetchValue == VERR_GETOPT_REQUIRED_ARGUMENT_MISSING)
        RTMsgError(Help::tr("Missing the %u%s value for option %s"),
                   iValueNo,
                   iValueNo == 1 ? Help::tr("st")
                    : iValueNo == 2 ? Help::tr("nd")
                    : iValueNo == 3 ? Help::tr("rd")
                    : Help::tr("th"),
                   pszOption);
    else
        errorGetOptWorker(rcGetOptFetchValue, pValueUnion);
    return RTEXITCODE_SYNTAX;

}


/**
 * Handled an RTGetOpt error or common option.
 *
 * This implements the 'V' and 'h' cases.  It reports appropriate syntax errors
 * for other @a rcGetOpt values.
 *
 * @retval  RTEXITCODE_SUCCESS if help or version request.
 * @retval  RTEXITCODE_SYNTAX if not help or version request.
 * @param   rcGetOpt            The RTGetOpt return value.
 * @param   pValueUnion         The value union returned by RTGetOpt.
 */
RTEXITCODE errorGetOpt(int rcGetOpt, union RTGETOPTUNION const *pValueUnion)
{
    Assert(g_enmCurCommand != HELP_CMD_VBOXMANAGE_INVALID);

    /*
     * Check if it is an unhandled standard option.
     */
    if (rcGetOpt == 'V')
    {
        RTPrintf("%sr%d\n", VBOX_VERSION_STRING, RTBldCfgRevision());
        return RTEXITCODE_SUCCESS;
    }

    if (rcGetOpt == 'h')
    {
        printFullCommandOrSubcommandHelp(g_enmCurCommand, g_fCurSubcommandScope, g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    /*
     * We failed.
     */
    showLogo(g_pStdErr);
    errorGetOptWorker(rcGetOpt, pValueUnion);
    if (   printBriefCommandOrSubcommandHelp(g_enmCurCommand, g_fCurSubcommandScope, g_pStdErr)
        >= ERROR_REPEAT_AFTER_USAGE_LENGTH)
    {
        /* Usage was very long, repeat the error message. */
        RTStrmPutCh(g_pStdErr, '\n');
        errorGetOptWorker(rcGetOpt, pValueUnion);
    }
    return RTEXITCODE_SYNTAX;
}


void showLogo(PRTSTREAM pStrm)
{
    static bool s_fShown; /* show only once */

    if (!s_fShown)
    {
        RTStrmPrintf(pStrm, VBOX_PRODUCT " Command Line Management Interface Version "
                     VBOX_VERSION_STRING "\n"
                     "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n");
        s_fShown = true;
    }
}
