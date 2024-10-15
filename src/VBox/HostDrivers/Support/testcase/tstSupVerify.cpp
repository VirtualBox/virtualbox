/* $Id: tstSupVerify.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SUP Testcase - Test SUPR3HardenedVerifyPlugIn.
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
#include <VBox/sup.h>
#include <iprt/errcore.h>

#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>


//#define DYNAMIC
#ifdef DYNAMIC
# include <iprt/win/windows.h>

# define DYNAMIC_IMPORTS() \
    ONE_IMPORT(RTR3InitExe); \
    ONE_IMPORT(RTMsgInitFailure); \
    ONE_IMPORT(RTGetOpt); \
    ONE_IMPORT(RTGetOptInit); \
    ONE_IMPORT(RTGetOptPrintError); \
    ONE_IMPORT(RTMsgError); \
    ONE_IMPORT(RTMsgErrorExit); \
    ONE_IMPORT(RTMsgInfo); \
    ONE_IMPORT(RTPrintf); \
    ONE_IMPORT(SUPR3HardenedVerifyInit); \
    ONE_IMPORT(SUPR3HardenedVerifyPlugIn)

# define ONE_IMPORT(a_fnName) static decltype(a_fnName) *g_pfn##a_fnName
DYNAMIC_IMPORTS();
# undef ONE_IMPORT

static void resolve(void)
{
    HMODULE hmod = LoadLibrary("VBoxRT.dll");
    DWORD cbWritten = 0;

# define ONE_IMPORT(a_fnName) do { \
            g_pfn##a_fnName = (decltype(a_fnName) *)GetProcAddress(hmod, #a_fnName); \
            if (!g_pfn##a_fnName) \
                WriteFile(GetStdHandle(STD_ERROR_HANDLE), RT_STR_TUPLE("Failed to resolve: " #a_fnName "\r\n"), &cbWritten, NULL); \
        } while (0)
    DYNAMIC_IMPORTS();
# undef ONE_IMPORT
}

#define RTR3InitExe                  g_pfnRTR3InitExe
#define RTMsgInitFailure             g_pfnRTMsgInitFailure
#define RTGetOpt                     g_pfnRTGetOpt
#define RTGetOptInit                 g_pfnRTGetOptInit
#define RTGetOptPrintError           g_pfnRTGetOptPrintError
#define RTMsgError                   g_pfnRTMsgError
#define RTMsgErrorExit               g_pfnRTMsgErrorExit
#define RTMsgInfo                    g_pfnRTMsgInfo
#define RTPrintf                     g_pfnRTPrintf
#define SUPR3HardenedVerifyInit      g_pfnSUPR3HardenedVerifyInit
#define SUPR3HardenedVerifyPlugIn    g_pfnSUPR3HardenedVerifyPlugIn

#endif /* DYNAMIC */

int main(int argc, char **argv)
{
    /*
     * Init.
     */
#ifdef DYNAMIC
    resolve();
#endif
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);
    rc = SUPR3HardenedVerifyInit();
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "SUPR3HardenedVerifyInit failed: %Rrc", rc);

    /*
     * Process arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--dummy",            'd', RTGETOPT_REQ_NOTHING },
    };

    //bool fKeepLoaded = false;

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case VINF_GETOPT_NOT_OPTION:
            {
                RTERRINFOSTATIC ErrInfo;
                RTErrInfoInitStatic(&ErrInfo);
                rc = SUPR3HardenedVerifyPlugIn(ValueUnion.psz, &ErrInfo.Core);
                if (RT_SUCCESS(rc))
                    RTMsgInfo("SUPR3HardenedVerifyPlugIn: %Rrc for '%s'\n", rc, ValueUnion.psz);
                else
                    RTMsgError("SUPR3HardenedVerifyPlugIn: %Rrc for '%s'  ErrInfo: %s\n",
                               rc, ValueUnion.psz, ErrInfo.Core.pszMsg);
                break;
            }

            case 'h':
                RTPrintf("%s [dll1 [dll2...]]\n", argv[0]);
                return 1;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return 0;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    return 0;
}

