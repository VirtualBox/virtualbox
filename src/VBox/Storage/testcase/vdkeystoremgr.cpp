/* $Id: vdkeystoremgr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Keystore utility for debugging.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <VBox/vd.h>
#include <iprt/errcore.h>
#include <VBox/version.h>
#include <iprt/initterm.h>
#include <iprt/base64.h>
#include <iprt/buildconfig.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/stream.h>
#include <iprt/message.h>
#include <iprt/getopt.h>
#include <iprt/assert.h>

#include "../VDKeyStore.h"

/** command handler argument */
struct HandlerArg
{
    int argc;
    char **argv;
};

static const char *g_pszProgName = "";
static void printUsage(PRTSTREAM pStrm)
{
    RTStrmPrintf(pStrm,
                 "Usage: %s\n"
                 "   create       --password <password>\n"
                 "                --cipher <cipher>\n"
                 "                --dek <dek in base64>\n"
                 "\n"
                 "   dump         --keystore <keystore data in base64>\n"
                 "                [--password <password to decrypt the DEK inside]\n",
                 g_pszProgName);
}

static void showLogo(PRTSTREAM pStrm)
{
    static bool s_fShown; /* show only once */

    if (!s_fShown)
    {
        RTStrmPrintf(pStrm, VBOX_PRODUCT " VD Keystore Mgr " VBOX_VERSION_STRING "\n"
                     "Copyright (C) 2016-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n");
        s_fShown = true;
    }
}

/**
 * Print a usage synopsis and the syntax error message.
 */
static int errorSyntax(const char *pszFormat, ...)
{
    va_list args;
    showLogo(g_pStdErr); // show logo even if suppressed
    va_start(args, pszFormat);
    RTStrmPrintf(g_pStdErr, "\nSyntax error: %N\n", pszFormat, &args);
    va_end(args);
    printUsage(g_pStdErr);
    return 1;
}

static int errorRuntime(const char *pszFormat, ...)
{
    va_list args;

    va_start(args, pszFormat);
    RTMsgErrorV(pszFormat, args);
    va_end(args);
    return 1;
}

static DECLCALLBACK(int) handleCreate(HandlerArg *pArgs)
{
    const char *pszPassword = NULL;
    const char *pszCipher = NULL;
    const char *pszDek = NULL;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--password", 'p', RTGETOPT_REQ_STRING },
        { "--cipher"  , 'c', RTGETOPT_REQ_STRING },
        { "--dek",      'd', RTGETOPT_REQ_STRING }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'p':   // --password
                pszPassword = ValueUnion.psz;
                break;
            case 'c':   // --cipher
                pszCipher = ValueUnion.psz;
                break;
            case 'd':   // --dek
                pszDek = ValueUnion.psz;
                break;
            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszPassword)
        return errorSyntax("Mandatory --password option missing\n");
    if (!pszCipher)
        return errorSyntax("Mandatory --cipher option missing\n");
    if (!pszDek)
        return errorSyntax("Mandatory --dek option missing\n");

    /* Get the size of the decoded DEK. */
    ssize_t cbDekDec = RTBase64DecodedSize(pszDek, NULL);
    if (cbDekDec == -1)
        return errorRuntime("The encoding of the base64 DEK is bad\n");

    uint8_t *pbDek = (uint8_t *)RTMemAllocZ(cbDekDec);
    size_t cbDek = cbDekDec;
    if (!pbDek)
        return errorRuntime("Failed to allocate memory for the DEK\n");

    int rc = RTBase64Decode(pszDek, pbDek, cbDek, &cbDek, NULL);
    if (RT_SUCCESS(rc))
    {
        char *pszKeyStoreEnc = NULL;
        rc = vdKeyStoreCreate(pszPassword, pbDek, cbDek, pszCipher, &pszKeyStoreEnc);
        if (RT_SUCCESS(rc))
        {
            RTPrintf("Successfully created keystore\n"
                     "Keystore (base64): \n"
                     "%s\n", pszKeyStoreEnc);
            RTMemFree(pszKeyStoreEnc);
        }
        else
            errorRuntime("Failed to create keystore with %Rrc\n", rc);
    }
    else
        errorRuntime("Failed to decode the DEK with %Rrc\n", rc);

    RTMemFree(pbDek);
    return VERR_NOT_IMPLEMENTED;
}

static DECLCALLBACK(int) handleDump(HandlerArg *pArgs)
{
    return VERR_NOT_IMPLEMENTED;
}

int main(int argc, char *argv[])
{
    int exitcode = 0;

    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_STANDALONE_APP);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    g_pszProgName = RTPathFilename(argv[0]);

    bool fShowLogo = false;
    int  iCmd      = 1;
    int  iCmdArg;

    /* global options */
    for (int i = 1; i < argc || argc <= iCmd; i++)
    {
        if (    argc <= iCmd
            ||  !strcmp(argv[i], "help")
            ||  !strcmp(argv[i], "-?")
            ||  !strcmp(argv[i], "-h")
            ||  !strcmp(argv[i], "-help")
            ||  !strcmp(argv[i], "--help"))
        {
            showLogo(g_pStdOut);
            printUsage(g_pStdOut);
            return 0;
        }

        if (   !strcmp(argv[i], "-v")
            || !strcmp(argv[i], "-version")
            || !strcmp(argv[i], "-Version")
            || !strcmp(argv[i], "--version"))
        {
            /* Print version number, and do nothing else. */
            RTPrintf("%sr%d\n", VBOX_VERSION_STRING, RTBldCfgRevision());
            return 0;
        }

        if (   !strcmp(argv[i], "--nologo")
            || !strcmp(argv[i], "-nologo")
            || !strcmp(argv[i], "-q"))
        {
            /* suppress the logo */
            fShowLogo = false;
            iCmd++;
        }
        else
        {
            break;
        }
    }

    iCmdArg = iCmd + 1;

    if (fShowLogo)
        showLogo(g_pStdOut);

    /*
     * All registered command handlers
     */
    static const struct
    {
        const char *command;
        DECLR3CALLBACKMEMBER(int, handler, (HandlerArg *a));
    } s_commandHandlers[] =
    {
        { "create",       handleCreate       },
        { "dump",         handleDump         },
        { NULL,           NULL               }
    };

    HandlerArg handlerArg = { 0, NULL };
    int commandIndex;
    for (commandIndex = 0; s_commandHandlers[commandIndex].command != NULL; commandIndex++)
    {
        if (!strcmp(s_commandHandlers[commandIndex].command, argv[iCmd]))
        {
            handlerArg.argc = argc - iCmdArg;
            handlerArg.argv = &argv[iCmdArg];

            exitcode = s_commandHandlers[commandIndex].handler(&handlerArg);
            break;
        }
    }
    if (!s_commandHandlers[commandIndex].command)
    {
        errorSyntax("Invalid command '%s'", argv[iCmd]);
        return 1;
    }

    return exitcode;
}

/* dummy stub for RuntimeR3 */
#ifndef RT_OS_WINDOWS
RTDECL(bool) RTAssertShouldPanic(void)
{
    return true;
}
#endif
