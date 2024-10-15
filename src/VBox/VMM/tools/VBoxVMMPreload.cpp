/* $Id: VBoxVMMPreload.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVMMPreload - Preload VBox the ring-0 modules.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/buildconfig.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <VBox/sup.h>
#include <VBox/version.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Known modules and their associated data (there are only known modules!).
 */
static struct
{
    const char     *pszName;
    bool            fPreload;
    void           *pvImageBase;
} g_aModules[] =
{
    { "VMMR0.r0",       true,  NULL },
    { "VBoxDDR0.r0",    true,  NULL },
};

static uint32_t     g_cVerbose = 1;
static bool         g_fLockDown = false;


/**
 * Parses the options.
 *
 * @returns RTEXITCODE_SUCCESS on success.
 * @param   argc                Argument count .
 * @param   argv                Argument vector.
 * @param   pfExit              Set to @c true if we should exit.
 */
static RTEXITCODE ParseOptions(int argc, char **argv, bool *pfExit)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--only",     'o', RTGETOPT_REQ_STRING  },
        { "--quiet",    'q', RTGETOPT_REQ_NOTHING },
        { "--lock" ,    'l', RTGETOPT_REQ_NOTHING },
        { "--verbose",  'v', RTGETOPT_REQ_NOTHING },
    };

    bool            fAll     = true;

    int             ch;
    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch(ch)
        {
            case 'o':
            {
                uint32_t i;

                if (fAll)
                {
                    fAll = false;
                    for (i = 0; i < RT_ELEMENTS(g_aModules); i++)
                        g_aModules[i].fPreload = false;
                }

                i = RT_ELEMENTS(g_aModules);
                while (i-- > 0)
                    if (!strcmp(ValueUnion.psz, g_aModules[i].pszName))
                    {
                        g_aModules[i].fPreload = true;
                        break;
                    }
                if (i > RT_ELEMENTS(g_aModules))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "No known module '%s'", ValueUnion.psz);
                break;
            }

            case 'v':
                g_cVerbose++;
                break;

            case 'q':
                g_cVerbose = 0;
                break;

            case 'l':
                g_fLockDown = true;
                break;

            case 'h':
                RTPrintf(VBOX_PRODUCT " VMM ring-0 Module Preloader Version " VBOX_VERSION_STRING
                         "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\n"
                         "\n"
                         "Usage: VBoxVMMPreload [-hlqvV] [-o|--only <mod>]\n"
                         "\n");
                *pfExit = true;
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                *pfExit = true;
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    return RTEXITCODE_SUCCESS;
}


/**
 * Loads the modules.
 *
 * @returns RTEXITCODE_SUCCESS on success.
 */
static RTEXITCODE LoadModules(void)
{
    RTERRINFOSTATIC ErrInfo;

    for (uint32_t i = 0; i < RT_ELEMENTS(g_aModules); i++)
    {
        if (g_aModules[i].fPreload)
        {
            char            szPath[RTPATH_MAX];
            int rc = RTPathAppPrivateArch(szPath, sizeof(szPath));
            if (RT_SUCCESS(rc))
                rc = RTPathAppend(szPath, sizeof(szPath), g_aModules[i].pszName);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathAppPrivateArch or RTPathAppend returned %Rrc", rc);

            RTErrInfoInitStatic(&ErrInfo);
            rc = SUPR3LoadModule(szPath, g_aModules[i].pszName, &g_aModules[i].pvImageBase, &ErrInfo.Core);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "SUPR3LoadModule failed for %s (%s): %s (rc=%Rrc)",
                                      g_aModules[i].pszName, szPath, ErrInfo.Core.pszMsg, rc);
            if (g_cVerbose >= 1)
                RTMsgInfo("Loaded '%s' ('%s') at %p\n", szPath, g_aModules[i].pszName, g_aModules[i].pvImageBase);
        }
    }

    if (g_fLockDown)
    {
        RTErrInfoInitStatic(&ErrInfo);
        int rc = SUPR3LockDownLoader(&ErrInfo.Core);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "SUPR3LockDownLoader failed: %s (rc=%Rrc)",
                                  ErrInfo.Core.pszMsg, rc);
        if (g_cVerbose >= 1)
            RTMsgInfo("Locked down module loader interface!\n");
    }

    RTStrmFlush(g_pStdOut);
    return RTEXITCODE_SUCCESS;
}


/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF1(envp);
    bool fExit = false;
    RTEXITCODE rcExit = ParseOptions(argc, argv, &fExit);
    if (rcExit == RTEXITCODE_SUCCESS && !fExit)
    {
        rcExit = LoadModules();
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            for (;;)
                RTThreadSleep(RT_INDEFINITE_WAIT);
        }
    }
    return rcExit;
}


#ifndef VBOX_WITH_HARDENING
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_SUPLIB);
    if (RT_SUCCESS(rc))
        return TrustedMain(argc, argv, envp);
    return RTMsgInitFailure(rc);
}
#endif /* !VBOX_WITH_HARDENING */

