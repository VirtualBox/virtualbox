/* $Id: VBoxDTraceWrapper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDTrace - Wrapper that selects the right dtrace implemetation and adds
 *              our library to the search path.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from http://www.virtualbox.org.
 *
 * The contents of this file are subject to the terms of the Common
 * Development and Distribution License Version 1.0 (CDDL) only, as it
 * comes in the "COPYING.CDDL" file of the VirtualBox distribution.
 *
 * SPDX-License-Identifier: CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/buildconfig.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/string.h>

#include <VBox/sup.h>

#include "../../Main/include/ExtPackUtil.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The VBoxDTrace extension pack name.   */
#define VBOX_EXTPACK_VBOXDTRACE_NAME            "Oracle VBoxDTrace Extension Pack"
/** The mangled version of VBOX_EXTPACK_VBOXDTRACE_NAME (also in Config.kmk). */
#define VBOX_EXTPACK_VBOXDTRACE_MANGLED_NAME    "Oracle_VBoxDTrace_Extension_Pack"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** The main function of VBoxDTrace.so/dylib/dll. */
typedef int (RTCALL *PFNVBOXDTRACEMAIN)(int argc, char **argv);


int main(int argc, char **argv)
{
    /*
     * Init IPRT.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Locate a native DTrace command binary.
     */
    bool fIsNativeDTrace = false;
    char szDTraceCmd[RTPATH_MAX];
    szDTraceCmd[0] = '\0';

#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD) || defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    /*
     * 1. Try native first on platforms where it's applicable.
     */
    static const char * const s_apszNativeDTrace[] =
    {
        "/usr/sbin/dtrace",
        "/sbin/dtrace",
        "/usr/bin/dtrace",
        "/bin/dtrace",
        "/usr/local/sbin/dtrace",
        "/usr/local/bin/dtrace"
    };
    if (!RTEnvExist("VBOX_DTRACE_NO_NATIVE"))
        for (uint32_t i = 0; i < RT_ELEMENTS(s_apszNativeDTrace); i++)
            if (RTFileExists(s_apszNativeDTrace[i]))
            {
                fIsNativeDTrace = true;
                strcpy(szDTraceCmd, s_apszNativeDTrace[i]);
# ifdef RT_OS_LINUX
                /** @todo Warn if the dtrace modules haven't been loaded or vboxdrv isn't
                 *        compiled against them. */
# endif
                break;
            }
    if (szDTraceCmd[0] == '\0')
#endif
    {
        /*
         * 2. VBoxDTrace extension pack installed?
         *
         * Note! We cannot use the COM API here because this program is usually
         *       run thru sudo or directly as root, even if the target
         *       VirtualBox process is running as regular user.  This is due to
         *       the privileges required to run dtrace scripts on a host.
         */
        rc = RTPathAppPrivateArch(szDTraceCmd, sizeof(szDTraceCmd));
        if (RT_SUCCESS(rc))
            rc = RTPathAppend(szDTraceCmd, sizeof(szDTraceCmd),
                              VBOX_EXTPACK_INSTALL_DIR RTPATH_SLASH_STR VBOX_EXTPACK_VBOXDTRACE_MANGLED_NAME);
        if (RT_SUCCESS(rc))
            rc = RTPathAppend(szDTraceCmd, sizeof(szDTraceCmd), RTBldCfgTargetDotArch());
        if (RT_SUCCESS(rc))
            rc = RTPathAppend(szDTraceCmd, sizeof(szDTraceCmd), "VBoxDTraceCmd");
        if (RT_SUCCESS(rc))
            rc = RTStrCat(szDTraceCmd, sizeof(szDTraceCmd), RTLdrGetSuff());
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error constructing extension pack path: %Rrc", rc);
        if (!RTFileExists(szDTraceCmd))
            return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                  "Unable to find a DTrace implementation. VBoxDTrace Extension Pack installed?");
        fIsNativeDTrace = false;
    }


    /*
     * Construct a new command line that includes our libary.
     */
    char szDTraceLibDir[RTPATH_MAX];
    rc = RTPathAppPrivateNoArch(szDTraceLibDir, sizeof(szDTraceLibDir));
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szDTraceLibDir, sizeof(szDTraceLibDir), "dtrace" RTPATH_SLASH_STR "lib");
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szDTraceLibDir, sizeof(szDTraceLibDir), RTBldCfgTargetArch());
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error constructing dtrace library path for VBox: %Rrc", rc);

    char **papszArgs = (char **)RTMemAlloc((argc + 3) * sizeof(char *));
    if (!papszArgs)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No memory for argument list.");

    int cArgs    = 1;
    papszArgs[0] = fIsNativeDTrace ? szDTraceCmd : argv[0];
    if (argc > 1)
    {
        papszArgs[cArgs++] = (char *)"-L";
        papszArgs[cArgs++] = szDTraceLibDir;
    }
    for (int i = 1; i < argc; i++)
        papszArgs[cArgs++] = argv[i];
    papszArgs[cArgs] = NULL;
    Assert(cArgs <= argc + 3);


    /*
     * The native DTrace we execute as a sub-process and wait for.
     */
    RTEXITCODE rcExit;
    if (fIsNativeDTrace)
    {
        RTPROCESS hProc;
        rc = RTProcCreate(szDTraceCmd, papszArgs, RTENV_DEFAULT, 0, &hProc);
        if (RT_SUCCESS(rc))
        {
            RTPROCSTATUS Status;
            rc = RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &Status);
            if (RT_SUCCESS(rc))
            {
                if (Status.enmReason == RTPROCEXITREASON_NORMAL)
                    rcExit = (RTEXITCODE)Status.iStatus;
                else
                    rcExit = RTEXITCODE_FAILURE;
            }
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Error waiting for child process: %Rrc", rc);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Error executing '%s': %Rrc", szDTraceCmd, rc);
    }
    /*
     * While the VBoxDTrace we load and call the main function of.
     */
    else
    {
        RTERRINFOSTATIC ErrInfo;
        RTLDRMOD hMod;
        rc = SUPR3HardenedLdrLoadPlugIn(szDTraceCmd, &hMod, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            PFNVBOXDTRACEMAIN pfnVBoxDTraceMain;
            rc = RTLdrGetSymbol(hMod, "VBoxDTraceMain", (void **)&pfnVBoxDTraceMain);
            if (RT_SUCCESS(rc))
                rcExit = (RTEXITCODE)pfnVBoxDTraceMain(cArgs, papszArgs);
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Error locating 'VBoxDTraceMain' in '%s': %Rrc", szDTraceCmd, rc);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Error loading '%s': %Rrc (%s)", szDTraceCmd, rc, ErrInfo.szMsg);
    }
    return rcExit;
}

