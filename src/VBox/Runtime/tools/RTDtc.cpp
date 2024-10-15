/* $Id: RTDtc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - dtc (devicetree compiler) like utility.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#include <iprt/fdt.h>

#include <iprt/buildconfig.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/vfs.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * CAT command options.
 */
typedef struct RTCMDDTCOPTS
{
    /** The input format. */
    RTFDTTYPE           enmInType;
    /** The output format. */
    RTFDTTYPE           enmOutType;
    /** The output filename */
    const char          *pszOutFile;
    /** Output blob version. */
    uint32_t            u32VersionBlobOut;
} RTCMDDTCOPTS;
/** Pointer to const DTC options. */
typedef RTCMDDTCOPTS const *PCRTCMDDTCOPTS;


/**
 * Parses the given input type string returning the matching enum.
 *
 * @returns The matching RTFDTTYPE enum or RTFDTTYPE_INVALID if not matching.
 * @param   pszType     The FDT tpe as string.
 */
DECLINLINE(RTFDTTYPE) rtCmdDtcParseInputType(const char *pszType)
{
    if (!strcmp(pszType, "dtb"))
        return RTFDTTYPE_DTB;
    else if (!strcmp(pszType, "dts"))
        return RTFDTTYPE_DTS;

    return RTFDTTYPE_INVALID;
}


/**
 * Parses the given output type string returning the matching enum.
 *
 * @returns The matching RTFDTTYPE enum or RTFDTTYPE_INVALID if not matching.
 * @param   pszType     The FDT tpe as string.
 */
DECLINLINE(RTFDTTYPE) rtCmdDtcParseOutputType(const char *pszType)
{
    if (!strcmp(pszType, "dtb"))
        return RTFDTTYPE_DTB;
    else if (!strcmp(pszType, "dts"))
        return RTFDTTYPE_DTS;

    return RTFDTTYPE_INVALID;
}


/**
 * Opens the input file.
 *
 * @returns Command exit, error messages written using RTMsg*.
 *
 * @param   pszFile             The input filename.
 * @param   phVfsIos            Where to return the input stream handle.
 */
static RTEXITCODE rtCmdDtcOpenInput(const char *pszFile, PRTVFSIOSTREAM phVfsIos)
{
    int rc;

    if (!strcmp(pszFile, "-"))
    {
        rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_INPUT,
                                      RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                      true /*fLeaveOpen*/,
                                      phVfsIos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("Error opening standard input: %Rrc", rc);
    }
    else
    {
        uint32_t        offError = 0;
        RTERRINFOSTATIC ErrInfo;
        rc = RTVfsChainOpenIoStream(pszFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                    phVfsIos, &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
            return RTVfsChainMsgErrorExitFailure("RTVfsChainOpenIoStream", pszFile, rc, offError, &ErrInfo.Core);
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Opens the output file.
 *
 * @returns IPRT status code.
 *
 * @param   pszFile             The input filename.
 * @param   phVfsIos            Where to return the input stream handle.
 */
static int rtCmdDtcOpenOutput(const char *pszFile, PRTVFSIOSTREAM phVfsIos)
{
    int rc;

    if (!strcmp(pszFile, "-"))
    {
        rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_OUTPUT,
                                      RTFILE_O_WRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                      true /*fLeaveOpen*/,
                                      phVfsIos);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(rc, "Error opening standard output: %Rrc", rc);
    }
    else
    {
        uint32_t        offError = 0;
        RTERRINFOSTATIC ErrInfo;
        rc = RTVfsChainOpenIoStream(pszFile, RTFILE_O_WRITE | RTFILE_O_CREATE | RTFILE_O_DENY_NONE,
                                    phVfsIos, &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
        {
            RTVfsChainMsgError("RTVfsChainOpenIoStream", pszFile, rc, offError, &ErrInfo.Core);
            return rc;
        }
    }

    return VINF_SUCCESS;

}


/**
 * Processes the given input according to the options.
 *
 * @returns Command exit code, error messages written using RTMsg*.
 * @param   pOpts               The command options.
 * @param   hVfsSrc             VFS I/O stream handle of the input.
 */
static RTEXITCODE rtCmdDtcProcess(PCRTCMDDTCOPTS pOpts, RTVFSIOSTREAM hVfsSrc)
{
    if (pOpts->enmInType == RTFDTTYPE_INVALID)
        return RTMsgErrorExitFailure("Devicetree input format wasn't given");
    if (pOpts->enmOutType == RTFDTTYPE_INVALID)
        return RTMsgErrorExitFailure("Devicetree output format wasn't given");

    RTFDT hFdt = NIL_RTFDT;
    RTERRINFOSTATIC ErrInfo;
    int rc = RTFdtCreateFromVfsIoStrm(&hFdt, hVfsSrc, pOpts->enmInType, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        RTVFSIOSTREAM hVfsIosDst = NIL_RTVFSIOSTREAM;
        rc = rtCmdDtcOpenOutput(pOpts->pszOutFile, &hVfsIosDst);
        if (RT_SUCCESS(rc))
        {
            rc = RTFdtDumpToVfsIoStrm(hFdt, pOpts->enmOutType, 0 /*fFlags*/, hVfsIosDst, RTErrInfoInitStatic(&ErrInfo));
            if (RT_FAILURE(rc) && RTErrInfoIsSet(&ErrInfo.Core))
                rc =  RTMsgErrorRc(rc, "Writing the devicetree failed: %Rrc - %s", rc, ErrInfo.Core.pszMsg);
            else if (RT_FAILURE(rc))
                rc = RTMsgErrorRc(rc, "Writing the devicetree failed: %Rrc\n", rc);
            RTVfsIoStrmRelease(hVfsIosDst);
        }
        RTFdtDestroy(hFdt);
    }
    else
    {
        if (RTErrInfoIsSet(&ErrInfo.Core))
            rc = RTMsgErrorRc(rc, "Loading the devicetree blob failed: %Rrc - %s", rc, ErrInfo.Core.pszMsg);
        else
            rc = RTMsgErrorRc(rc, "Loading the devicetree blob failed: %Rrc\n", rc);
    }

    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    return RTEXITCODE_SUCCESS;
}


/**
 * A dtc clone.
 *
 * @returns Program exit code.
 *
 * @param   cArgs               The number of arguments.
 * @param   papszArgs           The argument vector.  (Note that this may be
 *                              reordered, so the memory must be writable.)
 */
static RTEXITCODE RTCmdDtc(unsigned cArgs, char **papszArgs)
{

    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--in-format",                        'I', RTGETOPT_REQ_STRING  },
        { "--out",                              'o', RTGETOPT_REQ_STRING  },
        { "--out-format",                       'O', RTGETOPT_REQ_STRING  },
        { "--out-version",                      'V', RTGETOPT_REQ_UINT32  },
        { "--help",                             'h', RTGETOPT_REQ_NOTHING },
        { "--version",                          'v', RTGETOPT_REQ_NOTHING },

    };

    RTCMDDTCOPTS Opts;
    Opts.enmInType              = RTFDTTYPE_INVALID;
    Opts.enmOutType             = RTFDTTYPE_INVALID;

    RTEXITCODE      rcExit      = RTEXITCODE_SUCCESS;
    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_SUCCESS(rc))
    {
        bool fContinue = true;
        do
        {
            RTGETOPTUNION ValueUnion;
            int chOpt = RTGetOpt(&GetState, &ValueUnion);
            switch (chOpt)
            {
                case VINF_GETOPT_NOT_OPTION:
                {
                    RTVFSIOSTREAM hVfsSrc;
                    RTEXITCODE rcExit2 = rtCmdDtcOpenInput(ValueUnion.psz, &hVfsSrc);
                    if (rcExit2 == RTEXITCODE_SUCCESS)
                    {
                        rcExit2 = rtCmdDtcProcess(&Opts, hVfsSrc);
                        RTVfsIoStrmRelease(hVfsSrc);
                    }
                    if (rcExit2 != RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                    fContinue = false;
                    break;
                }

                case 'I':
                    Opts.enmInType = rtCmdDtcParseInputType(ValueUnion.psz);
                    if (Opts.enmInType == RTFDTTYPE_INVALID)
                    {
                        RTMsgErrorExitFailure("Invalid input type given: %s", ValueUnion.psz);
                        fContinue = false;
                    }
                    break;

                case 'o':
                    Opts.pszOutFile = ValueUnion.psz;
                    break;

                case 'O':
                    Opts.enmOutType = rtCmdDtcParseOutputType(ValueUnion.psz);
                    if (Opts.enmOutType == RTFDTTYPE_INVALID)
                    {
                        RTMsgErrorExitFailure("Invalid output type given: %s", ValueUnion.psz);
                        fContinue = false;
                    }
                    break;

                case 'h':
                    RTPrintf("Usage: to be written\nOption dump:\n");
                    for (unsigned i = 0; i < RT_ELEMENTS(s_aOptions); i++)
                        RTPrintf(" -%c,%s\n", s_aOptions[i].iShort, s_aOptions[i].pszLong);
                    fContinue = false;
                    break;

                case 'v':
                    RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                    fContinue = false;
                    break;

                default:
                    rcExit = RTGetOptPrintError(chOpt, &ValueUnion);
                    fContinue = false;
                    break;
            }
        } while (fContinue);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "RTGetOptInit: %Rrc", rc);
    return rcExit;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);
    return RTCmdDtc(argc, argv);
}

