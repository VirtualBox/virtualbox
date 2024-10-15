/* $Id: VBoxIntnetPcap.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxIntnetPcap - packet capture for VirtualBox internal networks
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include "IntNetIf.h"
#include "Pcap.h"

#include <iprt/buildconfig.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/process.h>
#include <iprt/stream.h>

#include <iprt/cpp/ministring.h>

#include <VBox/version.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static INTNETIFCTX  g_hIntNetCtx;
static PRTSTREAM    g_pStrmOut;
static uint64_t     g_StartNanoTS;
static bool         g_fPacketBuffered;
static uint64_t     g_cCountDown;
static size_t       g_cbSnapLen = 0xffff;

static const RTGETOPTDEF g_aGetOptDef[] =
{
    { "--count",                'c',   RTGETOPT_REQ_UINT64 },
    { "--network",              'i',   RTGETOPT_REQ_STRING },
    { "--snaplen",              's',   RTGETOPT_REQ_UINT32 },
    { "--packet-buffered",      'U',   RTGETOPT_REQ_NOTHING },
    { "--write",                'w',   RTGETOPT_REQ_STRING },
};


static void checkCaptureLimit(void)
{
    if (g_cCountDown > 0)
    {
        if (g_cCountDown-- == 1)
            IntNetR3IfWaitAbort(g_hIntNetCtx);
    }
}


static DECLCALLBACK(void) captureFrame(void *pvUser, void *pvFrame, uint32_t cbFrame)
{
    RT_NOREF(pvUser);

    int rc = PcapStreamFrame(g_pStrmOut, g_StartNanoTS, pvFrame, cbFrame, g_cbSnapLen);
    if (RT_FAILURE(rc))
    {
        RTMsgError("write: %Rrf", rc);
        IntNetR3IfWaitAbort(g_hIntNetCtx);
    }

    if (g_fPacketBuffered)
        RTStrmFlush(g_pStrmOut);

    checkCaptureLimit();
}


static DECLCALLBACK(void) captureGSO(void *pvUser, PCPDMNETWORKGSO pcGso, uint32_t cbFrame)
{
    RT_NOREF(pvUser, pcGso, cbFrame);

    checkCaptureLimit();
}


int
main(int argc, char *argv[])
{
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_SUPLIB);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse options
     */
    RTGETOPTSTATE State;
    rc = RTGetOptInit(&State, argc, argv, g_aGetOptDef, RT_ELEMENTS(g_aGetOptDef), 1, 0);
    AssertRC(rc);

    const char *pszNetworkName = NULL;
    const char *pszPcapFile    = NULL;

    int ch;
    RTGETOPTUNION Val;
    while ((ch = RTGetOpt(&State, &Val)) != 0)
    {
        switch (ch)
        {
            case 'c':           /* --count */
                if (Val.u64 == 0)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "--count must be greater than zero");
                g_cCountDown = Val.u64;
                break;

            case 'i':           /* --network */
                if (Val.psz[0] == '\0')
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "empty --network option");
                pszNetworkName = Val.psz;
                break;

            case 's':           /* --snaplen */
                if (Val.u32 == 0)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "--snaplen must be greater than zero");
                g_cbSnapLen = Val.u32;
                break;

            case 'U':           /* --packet-buffered */
                g_fPacketBuffered = true;
                break;

            case 'w':           /* --write */
                if (Val.psz[0] == '\0')
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "empty --write option");
                pszPcapFile = Val.psz;
                break;


            /*
             * Standard options recognized by RTGetOpt()
             */
            case 'V':           /* --version */
                RTPrintf("%sr%u\n", RTBldCfgVersion(), RTBldCfgRevision());
                return RTEXITCODE_SUCCESS;

            case 'h':           /* --help */
                RTPrintf("%s Version %sr%u\n"
                         "Copyright (C) 2009-" VBOX_C_YEAR " " VBOX_VENDOR "\n"
                         "\n"
                         "Usage: %s <options>\n"
                         "\n"
                         "Options:\n",
                         RTProcShortName(), RTBldCfgVersion(), RTBldCfgRevision(),
                         RTProcShortName());
                for (size_t i = 0; i < RT_ELEMENTS(g_aGetOptDef); ++i)
                    RTPrintf("    -%c, %s\n",
                             g_aGetOptDef[i].iShort, g_aGetOptDef[i].pszLong);
                return RTEXITCODE_SUCCESS;

            default:
            case VINF_GETOPT_NOT_OPTION:
                return RTGetOptPrintError(ch, &Val);
        }
    }
    if (!pszNetworkName)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No network specified. Please use the --network option");
    if (!pszPcapFile)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No output file specified. Please use the --write option");

    /*
     * Open the output file.
     */
    if (strcmp(pszPcapFile, "-") == 0)
        g_pStrmOut = g_pStdOut;
    else
    {
        rc = RTStrmOpen(pszPcapFile, "wb", &g_pStrmOut);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("%s: %Rrf", pszPcapFile, rc);
    }

    rc = IntNetR3IfCreate(&g_hIntNetCtx, pszNetworkName);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Opening the internal network '%s' failed with %Rrc\n", pszNetworkName, rc);

    rc = IntNetR3IfSetPromiscuous(g_hIntNetCtx, true /*fPromiscuous*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Enabling promiscuous mode on the internal network '%s' failed with %Rrc\n", pszNetworkName, rc);

    rc = IntNetR3IfSetActive(g_hIntNetCtx, true /*fActive*/);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("Activating interface on the internal network '%s' failed with %Rrc\n", pszNetworkName, rc);

    /*
     * Snoop traffic.
     */
    g_StartNanoTS = RTTimeNanoTS();
    rc = PcapStreamHdr(g_pStrmOut, g_StartNanoTS);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("write: %Rrf", rc);
    if (g_fPacketBuffered)
        RTStrmFlush(g_pStrmOut);

    rc = IntNetR3IfPumpPkts(g_hIntNetCtx, captureFrame, NULL /*pvUser*/,
                            captureGSO, NULL /*pvUserGso*/);

    RTEXITCODE rcExit = RT_SUCCESS(RTStrmError(g_pStrmOut)) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
    rc = RTStrmClose(g_pStrmOut);
    if (RT_FAILURE(rc))
        rcExit = RTMsgErrorExitFailure("close: %Rrf", rc);
    return rcExit;
}
