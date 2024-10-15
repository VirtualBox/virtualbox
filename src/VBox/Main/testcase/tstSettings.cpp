/* $Id: tstSettings.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Settings testcases - No Main API involved.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <typeinfo>
#include <stdexcept>

#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>

#include <VBox/com/string.h>
#include <VBox/settings.h>

using namespace com;
using namespace settings;

/** The current logging verbosity level. */
static unsigned  g_uVerbosity = 0;


/**
 * Tests a single .vbox machine configuration file.
 *
 * @param   strFileSrc          Absolute path of machine configuration file to test.
 *
 * @note    The source configuration file will not be modified (i.e. written to disk).
 */
static void tstFileSingle(const Utf8Str &strFileSrc)
{
    RTTestIPrintf(RTTESTLVL_ALWAYS, "Testing file: %s\n", strFileSrc.c_str());

    char szFileDst[RTPATH_MAX] = { 0 };

    try
    {
        MachineConfigFile fileSrc(&strFileSrc);
        if (fileSrc == fileSrc) /* Only have a == operator. */
        {
        }
        else
            throw "Source file doesn't match itself! == operator bug!?";

        RTTestIPrintf(RTTESTLVL_ALWAYS, "Source file version is: v%u\n", fileSrc.getSettingsVersion());

        RTTESTI_CHECK_RC_OK_RETV(RTPathTemp(szFileDst, sizeof(szFileDst)));
        RTTESTI_CHECK_RC_OK_RETV(RTPathAppend(szFileDst, sizeof(szFileDst), "tstSettings-XXXXXX.vbox"));
        RTTESTI_CHECK_RC_OK_RETV(RTFileCreateTemp(szFileDst, 0600));

#ifdef DEBUG_andy
        RTStrPrintf(szFileDst, sizeof(szFileDst), "/tmp/out.vbox");
#endif
        /* Write source file to a temporary destination file. */
        fileSrc.write(szFileDst);
        RTTestIPrintf(RTTESTLVL_DEBUG, "Destination file written to: %s\n", szFileDst);

        try
        {
            /* Load destination file to see if it works. */
            Utf8Str strFileDst(szFileDst);
            MachineConfigFile fileDst(&strFileDst);
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Destination file version is: v%u\n", fileDst.getSettingsVersion());
            if (fileSrc == fileDst) /* Only have a == operator. */
            {
            }
            else
                throw std::runtime_error("Source and destination files don't match!");
        }
        catch (const std::exception &err)
        {
            RTTestIFailed("Testing destination file failed: %s\n", err.what());
        }
    }
    catch (const std::exception &err)
    {
        RTTestIFailed("Testing source file failed: %s\n", err.what());
    }

#ifndef DEBUG_andy
    /* Clean up. */
    if (strlen(szFileDst))
        RTTESTI_CHECK_RC_OK_RETV(RTFileDelete(szFileDst));
#endif
}

int main(int argc, char *argv[])
{
    RTTEST      hTest;
    RTEXITCODE  rcExit = RTTestInitAndCreate("tstSettings", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * Process options.
     */
    static const RTGETOPTDEF aOpts[] =
    {
        { "--verbose",             'v',               RTGETOPT_REQ_STRING },
    };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, argc, argv, aOpts, RT_ELEMENTS(aOpts), 1 /*idxFirst*/, 0 /*fFlags - must not sort! */);
    AssertRCReturn(rc, RTEXITCODE_INIT);

    unsigned      cFiles = 0;

    int           ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'v':
                g_uVerbosity++;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                cFiles++;
                tstFileSingle(ValueUnion.psz);
                break;
            }

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    if (!cFiles)
        return RTTestSkipAndDestroy(hTest, "At least one .vbox machine file must be specified to test!\n");

    return RTTestSummaryAndDestroy(hTest);
}

