/* $Id: fuzzclientcmd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Fuzzing framework API, fuzzed client command.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/fuzz.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/types.h>
#include <iprt/vfs.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef DECLCALLBACKTYPE(int, FNLLVMFUZZERTESTONEINPUT,(const uint8_t *pbData, size_t cbData));
typedef FNLLVMFUZZERTESTONEINPUT *PFNLLVMFUZZERTESTONEINPUT;


/**
 * Fuzzing client command state.
 */
typedef struct RTFUZZCMDCLIENT
{
    /** Our own fuzzing context containing all the data. */
    RTFUZZCTX                 hFuzzCtx;
    /** Consumption callback. */
    PFNFUZZCLIENTCONSUME      pfnConsume;
    /** Opaque user data to pass to the consumption callback. */
    void                      *pvUser;
    /** The LLVM libFuzzer compatible entry point if configured */
    PFNLLVMFUZZERTESTONEINPUT pfnLlvmFuzzerTestOneInput;
    /** The selected input channel. */
    RTFUZZOBSINPUTCHAN        enmInputChan;
    /** Standard input VFS handle. */
    RTVFSIOSTREAM             hVfsStdIn;
    /** Standard output VFS handle. */
    RTVFSIOSTREAM             hVfsStdOut;
} RTFUZZCMDCLIENT;
/** Pointer to a fuzzing client command state. */
typedef RTFUZZCMDCLIENT *PRTFUZZCMDCLIENT;



/**
 * Runs the appropriate consumption callback with the provided data.
 *
 * @returns Status code, 0 for success.
 * @param   pThis               The fuzzing client command state.
 * @param   pvData              The data to consume.
 * @param   cbData              Size of the data in bytes.
 */
static int rtFuzzCmdClientConsume(PRTFUZZCMDCLIENT pThis, const void *pvData, size_t cbData)
{
    if (pThis->pfnLlvmFuzzerTestOneInput)
        return pThis->pfnLlvmFuzzerTestOneInput((const uint8_t *)pvData, cbData);
    else
        return pThis->pfnConsume(pvData, cbData, pThis->pvUser);
}


/**
 * The fuzzing client mainloop.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzing client command state.
 */
static int rtFuzzCmdClientMainloop(PRTFUZZCMDCLIENT pThis)
{
    int rc = VINF_SUCCESS;
    bool fShutdown = false;

    while (   !fShutdown
           && RT_SUCCESS(rc))
    {
        RTFUZZINPUT hFuzzInput;

        rc = RTFuzzCtxInputGenerate(pThis->hFuzzCtx, &hFuzzInput);
        if (RT_SUCCESS(rc))
        {
            void *pv = NULL;
            size_t cb = 0;
            rc = RTFuzzInputQueryBlobData(hFuzzInput, &pv, &cb);
            if (RT_SUCCESS(rc))
            {
                char bResp = '.';
                int rc2 = rtFuzzCmdClientConsume(pThis, pv, cb);
                if (RT_SUCCESS(rc2))
                {
                    rc = RTFuzzInputAddToCtxCorpus(hFuzzInput);
                    bResp = 'A';
                }

                if (RT_SUCCESS(rc))
                    rc = RTVfsIoStrmWrite(pThis->hVfsStdOut, &bResp, 1, true /*fBlocking*/, NULL);
            }

            RTFuzzInputRelease(hFuzzInput);
        }
    }

    return rc;
}


/**
 * Run the fuzzing client.
 *
 * @returns Process exit status.
 * @param   pThis               The fuzzing client command state.
 */
static RTEXITCODE rtFuzzCmdClientRun(PRTFUZZCMDCLIENT pThis)
{
    int rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_INPUT, 0, true /*fLeaveOpen*/, &pThis->hVfsStdIn);
    if (RT_SUCCESS(rc))
    {
        rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_OUTPUT, 0, true /*fLeaveOpen*/, &pThis->hVfsStdOut);
        if (RT_SUCCESS(rc))
        {
            /* Read the initial input fuzzer state from the standard input. */
            uint32_t cbFuzzCtxState;
            rc = RTVfsIoStrmRead(pThis->hVfsStdIn, &cbFuzzCtxState, sizeof(cbFuzzCtxState), true /*fBlocking*/, NULL);
            if (RT_SUCCESS(rc))
            {
                void *pvFuzzCtxState = RTMemAllocZ(cbFuzzCtxState);
                if (RT_LIKELY(pvFuzzCtxState))
                {
                    rc = RTVfsIoStrmRead(pThis->hVfsStdIn, pvFuzzCtxState, cbFuzzCtxState, true /*fBlocking*/, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTFuzzCtxCreateFromStateMem(&pThis->hFuzzCtx, pvFuzzCtxState, cbFuzzCtxState);
                        if (RT_SUCCESS(rc))
                            rc = rtFuzzCmdClientMainloop(pThis);
                    }

                    RTMemFree(pvFuzzCtxState);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }
    }

    if (RT_SUCCESS(rc))
        return RTEXITCODE_SUCCESS;

    return RTEXITCODE_FAILURE;
}


/**
 * Run a single iteration of the fuzzing client and return.
 *
 * @returns Process exit status.
 * @param   pThis               The fuzzing client command state.
 */
static RTEXITCODE rtFuzzCmdClientRunFile(PRTFUZZCMDCLIENT pThis, const char *pszFilename)
{
    void *pv = NULL;
    size_t cbFile = 0;
    int rc = RTFileReadAll(pszFilename, &pv, &cbFile);
    if (RT_SUCCESS(rc))
    {
        rtFuzzCmdClientConsume(pThis, pv, cbFile);
        RTFileReadAllFree(pv, cbFile);
        return RTEXITCODE_SUCCESS;
    }

    return RTEXITCODE_FAILURE;
}


RTR3DECL(RTEXITCODE) RTFuzzCmdFuzzingClient(unsigned cArgs, char **papszArgs, PFNFUZZCLIENTCONSUME pfnConsume, void *pvUser)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--help",                            'h', RTGETOPT_REQ_NOTHING },
        { "--version",                         'V', RTGETOPT_REQ_NOTHING },
        { "--llvm-input",                      'l', RTGETOPT_REQ_STRING  },
        { "--file",                            'f', RTGETOPT_REQ_STRING  },
    };

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_SUCCESS(rc))
    {
        /* Option variables:  */
        RTFUZZCMDCLIENT This;
        RTLDRMOD hLlvmMod = NIL_RTLDRMOD;
        const char *pszFilename = NULL;

        This.pfnConsume   = pfnConsume;
        This.pvUser       = pvUser;
        This.enmInputChan = RTFUZZOBSINPUTCHAN_FUZZING_AWARE_CLIENT;

        /* Argument parsing loop. */
        bool fContinue = true;
        bool fExit = false;
        do
        {
            RTGETOPTUNION ValueUnion;
            int chOpt = RTGetOpt(&GetState, &ValueUnion);
            switch (chOpt)
            {
                case 0:
                    fContinue = false;
                    break;

                case 'f':
                {
                    pszFilename = ValueUnion.psz;
                    This.enmInputChan = RTFUZZOBSINPUTCHAN_FILE;
                    break;
                }

                case 'l':
                {
                    /*
                     * Load the indicated library and try to resolve LLVMFuzzerTestOneInput,
                     * which will act as the input callback.
                     */
                    rc = RTLdrLoad(ValueUnion.psz, &hLlvmMod);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTLdrGetSymbol(hLlvmMod, "LLVMFuzzerTestOneInput", (void **)&This.pfnLlvmFuzzerTestOneInput);
                        if (RT_FAILURE(rc))
                            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to query '%s' from '%s': %Rrc",
                                                    "LLVMFuzzerTestOneInput",
                                                    ValueUnion.psz,
                                                    rc);
                    }
                    break;
                }

                case 'h':
                    RTPrintf("Usage: to be written\nOption dump:\n");
                    for (unsigned i = 0; i < RT_ELEMENTS(s_aOptions); i++)
                        RTPrintf(" -%c,%s\n", s_aOptions[i].iShort, s_aOptions[i].pszLong);
                    fContinue = false;
                    fExit = true;
                    break;

                case 'V':
                    RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                    fContinue = false;
                    fExit = true;
                    break;

                default:
                    rcExit = RTGetOptPrintError(chOpt, &ValueUnion);
                    fContinue = false;
                    break;
            }
        } while (fContinue);

        if (   rcExit == RTEXITCODE_SUCCESS
            && !fExit)
        {
            switch (This.enmInputChan)
            {
                case RTFUZZOBSINPUTCHAN_FUZZING_AWARE_CLIENT:
                    rcExit = rtFuzzCmdClientRun(&This);
                    break;
                case RTFUZZOBSINPUTCHAN_FILE:
                    rcExit = rtFuzzCmdClientRunFile(&This, pszFilename);
                    break;
                default:
                    rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "Input channel unknown/not implemented yet");
            }
        }

        if (hLlvmMod != NIL_RTLDRMOD)
            RTLdrClose(hLlvmMod);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "RTGetOptInit: %Rrc", rc);
    return rcExit;
}

