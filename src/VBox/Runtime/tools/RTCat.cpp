/* $Id: RTCat.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - cat like utility.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include <iprt/vfs.h>

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


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * CAT command options.
 */
typedef struct RTCMDCATOPTS
{
    bool fShowEnds;                 /**< -E */
    bool fShowNonPrinting;          /**< -v */
    bool fShowTabs;                 /**< -T */
    bool fSqueezeBlankLines;        /**< -s */
    bool fNumberLines;              /**< -n */
    bool fNumberNonBlankLines;      /**< -b */
    bool fAdvisoryOutputLock;       /**< -l */
    bool fUnbufferedOutput;         /**< -u */
} RTCMDCATOPTS;
/** Pointer to const CAT options. */
typedef RTCMDCATOPTS const *PCRTCMDCATOPTS;



/**
 * Outputs the source raw.
 *
 * @returns Command exit, error messages written using RTMsg*.
 * @param   hVfsOutput          The output I/O stream.
 * @param   hVfsSrc             The input I/O stream.
 * @param   pszSrc              The input name.
 */
static RTEXITCODE rtCmdCatShowRaw(RTVFSIOSTREAM hVfsOutput, RTVFSIOSTREAM hVfsSrc, const char *pszSrc)
{
    int rc = RTVfsUtilPumpIoStreams(hVfsSrc, hVfsOutput, 0 /*cbBufHint*/);
    if (RT_SUCCESS(rc))
        return RTEXITCODE_SUCCESS;
    return RTMsgErrorExitFailure("Error catting '%s': %Rrc", pszSrc, rc);
}


/**
 * Outputs the source with complicated formatting.
 *
 * @returns Command exit, error messages written using RTMsg*.
 * @param   hVfsOutput          The output I/O stream.
 * @param   hVfsSrc             The input I/O stream.
 * @param   pszSrc              The input name.
 */
static RTEXITCODE rtCmdCatShowComplicated(RTVFSIOSTREAM hVfsOutput, RTVFSIOSTREAM hVfsSrc, const char *pszSrc,
                                          PCRTCMDCATOPTS pOpts)
{
    if (pOpts->fShowEnds)
        RTMsgWarning("--show-ends is not implemented\n");
    if (pOpts->fShowTabs)
        RTMsgWarning("--show-tabs is not implemented\n");
    if (pOpts->fShowNonPrinting)
        RTMsgWarning("--show-nonprinting is not implemented\n");
    if (pOpts->fSqueezeBlankLines)
        RTMsgWarning("--squeeze-blank is not implemented\n");
    if (pOpts->fNumberLines)
        RTMsgWarning("--number is not implemented\n");
    if (pOpts->fNumberNonBlankLines)
        RTMsgWarning("--number-nonblank is not implemented\n");
    return rtCmdCatShowRaw(hVfsOutput, hVfsSrc, pszSrc);
}


/**
 * Opens the input file.
 *
 * @returns Command exit, error messages written using RTMsg*.
 *
 * @param   pszFile             The input filename.
 * @param   phVfsIos            Where to return the input stream handle.
 */
static RTEXITCODE rtCmdCatOpenInput(const char *pszFile, PRTVFSIOSTREAM phVfsIos)
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
 * A /bin/cat clone.
 *
 * @returns Program exit code.
 *
 * @param   cArgs               The number of arguments.
 * @param   papszArgs           The argument vector.  (Note that this may be
 *                              reordered, so the memory must be writable.)
 */
static RTEXITCODE RTCmdCat(unsigned cArgs, char **papszArgs)
{

    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--show-all",                         'A', RTGETOPT_REQ_NOTHING },
        { "--number-nonblanks",                 'b', RTGETOPT_REQ_NOTHING },
        { "--show-ends-and-nonprinting",        'e', RTGETOPT_REQ_NOTHING },
        { "--show-ends",                        'E', RTGETOPT_REQ_NOTHING },
        { "--advisory-output-lock",             'l', RTGETOPT_REQ_NOTHING },
        { "--number",                           'n', RTGETOPT_REQ_NOTHING },
        { "--squeeze-blank",                    's', RTGETOPT_REQ_NOTHING },
        { "--show-tabs-and-nonprinting",        't', RTGETOPT_REQ_NOTHING },
        { "--show-tabs",                        'T', RTGETOPT_REQ_NOTHING },
        { "--unbuffered-output",                'u', RTGETOPT_REQ_NOTHING },
        { "--show-nonprinting",                 'v', RTGETOPT_REQ_NOTHING },
    };

    RTCMDCATOPTS Opts;
    Opts.fShowEnds              = false;
    Opts.fShowNonPrinting       = false;
    Opts.fShowTabs              = false;
    Opts.fSqueezeBlankLines     = false;
    Opts.fNumberLines           = false;
    Opts.fNumberNonBlankLines   = false;
    Opts.fAdvisoryOutputLock    = false;
    Opts.fUnbufferedOutput      = false;

    RTEXITCODE      rcExit      = RTEXITCODE_SUCCESS;
    unsigned        cProcessed  = 0;
    RTVFSIOSTREAM   hVfsOutput  = NIL_RTVFSIOSTREAM;
    int rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_OUTPUT, RTFILE_O_WRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                      true /*fLeaveOpen*/, &hVfsOutput);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTVfsIoStrmFromStdHandle: %Rrc", rc);

    RTGETOPTSTATE GetState;
    rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
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
                case 0:
                    /*
                     * If we've processed any files we're done.  Otherwise take
                     * input from stdin and write the output to stdout.
                     */
                    if (cProcessed > 0)
                    {
                        fContinue = false;
                        break;
                    }
                    ValueUnion.psz = "-";
                    RT_FALL_THRU();
                case VINF_GETOPT_NOT_OPTION:
                {
                    RTVFSIOSTREAM hVfsSrc;
                    RTEXITCODE rcExit2 = rtCmdCatOpenInput(ValueUnion.psz, &hVfsSrc);
                    if (rcExit2 == RTEXITCODE_SUCCESS)
                    {
                        if (   Opts.fShowEnds
                            || Opts.fShowTabs
                            || Opts.fShowNonPrinting
                            || Opts.fSqueezeBlankLines
                            || Opts.fNumberLines
                            || Opts.fNumberNonBlankLines)
                            rcExit2 = rtCmdCatShowComplicated(hVfsOutput, hVfsSrc, ValueUnion.psz, &Opts);
                        else
                            rcExit2 = rtCmdCatShowRaw(hVfsOutput, hVfsSrc, ValueUnion.psz);
                        RTVfsIoStrmRelease(hVfsSrc);
                    }
                    if (rcExit2 != RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                    cProcessed++;
                    break;
                }

                case 'A':
                    Opts.fShowNonPrinting       = true;
                    Opts.fShowEnds              = true;
                    Opts.fShowTabs              = true;
                    break;

                case 'b':
                    Opts.fNumberNonBlankLines   = true;
                    break;

                case 'e':
                    Opts.fShowNonPrinting       = true;
                    RT_FALL_THRU();
                case 'E':
                    Opts.fShowEnds              = true;
                    break;

                case 'l':
                    Opts.fAdvisoryOutputLock    = true;
                    break;

                case 'n':
                    Opts.fNumberLines           = true;
                    Opts.fNumberNonBlankLines   = false;
                    break;

                case 's':
                    Opts.fSqueezeBlankLines     = true;
                    break;

                case 't':
                    Opts.fShowNonPrinting       = true;
                    RT_FALL_THRU();
                case 'T':
                    Opts.fShowTabs              = true;
                    break;

                case 'u': /* currently ignored */
                    Opts.fUnbufferedOutput      = true;
                    break;

                case 'v':
                    Opts.fShowNonPrinting       = true;
                    break;

                case 'h':
                    RTPrintf("Usage: to be written\nOption dump:\n");
                    for (unsigned i = 0; i < RT_ELEMENTS(s_aOptions); i++)
                        RTPrintf(" -%c,%s\n", s_aOptions[i].iShort, s_aOptions[i].pszLong);
                    fContinue = false;
                    break;

                case 'V':
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
    RTVfsIoStrmRelease(hVfsOutput);
    return rcExit;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);
    return RTCmdCat(argc, argv);
}

