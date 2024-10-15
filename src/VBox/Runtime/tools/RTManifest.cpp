/* $Id: RTManifest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Manifest Utility.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/manifest.h>

#include <iprt/buildconfig.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/vfs.h>


/**
 * Verify a manifest.
 *
 * @returns Program exit code, failures with error message.
 * @param   pszManifest         The manifest file. NULL if standard input.
 * @param   fStdFormat          Whether to expect standard format (true) or
 *                              java format (false).
 * @param   pszChDir            The directory to change into before processing
 *                              the files in the manifest.
 */
static RTEXITCODE rtManifestDoVerify(const char *pszManifest, bool fStdFormat, const char *pszChDir)
{
    RT_NOREF_PV(pszChDir); /** @todo implement pszChDir! */

    /*
     * Open the manifest.
     */
    int             rc;
    RTVFSIOSTREAM   hVfsIos;
    if (!pszManifest)
    {
        rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_INPUT, RTFILE_O_READ, false /*fLeaveOpen*/, &hVfsIos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to prepare standard input for reading: %Rrc", rc);
    }
    else
    {
        uint32_t        offError = 0;
        RTERRINFOSTATIC ErrInfo;
        rc = RTVfsChainOpenIoStream(pszManifest, RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN,
                                    &hVfsIos, &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
            return RTVfsChainMsgErrorExitFailure("RTVfsChainOpenIoStream", pszManifest, rc, offError, &ErrInfo.Core);
    }

    /*
     * Read it.
     */
    RTMANIFEST hManifest;
    rc = RTManifestCreate(0 /*fFlags*/, &hManifest);
    if (RT_SUCCESS(rc))
    {
        if (fStdFormat)
        {
            char szErr[4096 + 1024];
            rc = RTManifestReadStandardEx(hManifest, hVfsIos, szErr, sizeof(szErr));
            if (RT_SUCCESS(rc))
            {
                RTVfsIoStrmRelease(hVfsIos);
                hVfsIos = NIL_RTVFSIOSTREAM;

                /*
                 * Do the verification.
                 */
                /** @todo We're missing some enumeration APIs here! */
                RTMsgError("The manifest read fine, but the actual verification code is yet to be written. Sorry.");
                rc = VERR_NOT_IMPLEMENTED;
#if 1 /* For now, just write the manifest to stdout so we can test the read routine. */
                RTVFSIOSTREAM hVfsIosOut;
                rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_OUTPUT, RTFILE_O_WRITE, false /*fLeaveOpen*/, &hVfsIosOut);
                if (RT_SUCCESS(rc))
                {
                    RTManifestWriteStandard(hManifest, hVfsIosOut);
                    RTVfsIoStrmRelease(hVfsIosOut);
                }
#endif
            }
            else if (szErr[0])
                RTMsgError("Error reading manifest: %s", szErr);
            else
                RTMsgError("Error reading manifest: %Rrc", rc);
        }
        else
        {
            RTMsgError("Support for Java manifest files is not implemented yet");
            rc = VERR_NOT_IMPLEMENTED;
        }
        RTManifestRelease(hManifest);
    }

    RTVfsIoStrmRelease(hVfsIos);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Adds a file to the manifest.
 *
 * @returns IPRT status code, failures with error message.
 * @param   hManifest           The manifest to add it to.
 * @param   pszFilename         The name of the file to add.
 * @param   fAttr               The manifest attributes to add.
 */
static int rtManifestAddFileToManifest(RTMANIFEST hManifest, const char *pszFilename, uint32_t fAttr)
{
    RTVFSIOSTREAM   hVfsIos;
    uint32_t        offError = 0;
    RTERRINFOSTATIC ErrInfo;
    int rc = RTVfsChainOpenIoStream(pszFilename, RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN,
                                    &hVfsIos, &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
    {
        RTVfsChainMsgError("RTVfsChainOpenIoStream", pszFilename, rc, offError, &ErrInfo.Core);
        return rc;
    }

    rc = RTManifestEntryAddIoStream(hManifest, hVfsIos, pszFilename, fAttr);
    if (RT_FAILURE(rc))
        RTMsgError("RTManifestEntryAddIoStream failed for '%s': %Rrc", pszFilename, rc);

    RTVfsIoStrmRelease(hVfsIos);
    return rc;
}


/**
 * Create a manifest from the specified input files.
 *
 * @returns Program exit code, failures with error message.
 * @param   pszManifest         The name of the output manifest file.  NULL if
 *                              it should be written to standard output.
 * @param   fStdFormat          Whether to expect standard format (true) or
 *                              java format (false).
 * @param   pszChDir            The directory to change into before processing
 *                              the file arguments.
 * @param   fAttr               The file attributes to put in the manifest.
 * @param   pGetState           The RTGetOpt state.
 * @param   pUnion              What the last RTGetOpt() call returned.
 * @param   chOpt               What the last RTGetOpt() call returned.
 */
static RTEXITCODE rtManifestDoCreate(const char *pszManifest, bool fStdFormat, const char *pszChDir, uint32_t fAttr,
                                     PRTGETOPTSTATE pGetState, PRTGETOPTUNION pUnion, int chOpt)
{
    /*
     * Open the manifest file.
     */
    int rc;
    RTVFSIOSTREAM hVfsIos;
    if (!pszManifest)
    {
        rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_OUTPUT, RTFILE_O_WRITE, false /*fLeaveOpen*/, &hVfsIos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to prepare standard output for writing: %Rrc", rc);
    }
    else
    {
        RTERRINFOSTATIC ErrInfo;
        uint32_t        offError;
        rc = RTVfsChainOpenIoStream(pszManifest, RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE,
                                    &hVfsIos, &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
            return RTVfsChainMsgErrorExitFailure("RTVfsChainOpenIoStream", pszManifest, rc, offError, &ErrInfo.Core);
    }

    /*
     * Create the internal manifest.
     */
    RTMANIFEST hManifest;
    rc = RTManifestCreate(0 /*fFlags*/, &hManifest);
    if (RT_SUCCESS(rc))
    {
        /*
         * Change directory and start processing the specified files.
         */
        if (pszChDir)
        {
            rc = RTPathSetCurrent(pszChDir);
            if (RT_FAILURE(rc))
                RTMsgError("Failed to change directory to '%s': %Rrc", pszChDir, rc);
        }
        if (RT_SUCCESS(rc))
        {
            while (chOpt == VINF_GETOPT_NOT_OPTION)
            {
                rc = rtManifestAddFileToManifest(hManifest, pUnion->psz, fAttr);
                if (RT_FAILURE(rc))
                    break;

                /* next */
                chOpt = RTGetOpt(pGetState, pUnion);
            }
            if (RT_SUCCESS(rc) && chOpt != 0)
            {
                RTGetOptPrintError(chOpt, pUnion);
                rc = chOpt < 0 ? chOpt : -chOpt;
            }
        }

        /*
         * Write the manifest.
         */
        if (RT_SUCCESS(rc))
        {
            if (fStdFormat)
            {
                rc = RTManifestWriteStandard(hManifest, hVfsIos);
                if (RT_FAILURE(rc))
                    RTMsgError("RTManifestWriteStandard failed: %Rrc", rc);
            }
            else
            {
                RTMsgError("Support for Java manifest files is not implemented yet");
                rc = VERR_NOT_IMPLEMENTED;
            }
        }

        RTManifestRelease(hManifest);
    }

    RTVfsIoStrmRelease(hVfsIos);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Parse arguments.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--manifest",     'm', RTGETOPT_REQ_STRING  },
        { "--java",         'j', RTGETOPT_REQ_NOTHING },
        { "--chdir",        'C', RTGETOPT_REQ_STRING  },
        { "--attribute",    'a', RTGETOPT_REQ_STRING  },
        { "--verify",       'v', RTGETOPT_REQ_NOTHING },
    };

    bool            fVerify     = false;
    bool            fStdFormat  = true;
    const char     *pszManifest = NULL;
    const char     *pszChDir    = NULL;
    uint32_t        fAttr       = RTMANIFEST_ATTR_UNKNOWN;

    RTGETOPTSTATE GetState;
    rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc", rc);

    RTGETOPTUNION ValueUnion;
    while (   (rc = RTGetOpt(&GetState, &ValueUnion)) != 0
           &&  rc != VINF_GETOPT_NOT_OPTION)
    {
        switch (rc)
        {
            case 'a':
            {
                static struct
                {
                    const char *pszAttr;
                    uint32_t    fAttr;
                } s_aAttributes[] =
                {
                    { "size",   RTMANIFEST_ATTR_SIZE    },
                    { "md5",    RTMANIFEST_ATTR_MD5     },
                    { "sha1",   RTMANIFEST_ATTR_SHA1    },
                    { "sha256", RTMANIFEST_ATTR_SHA256  },
                    { "sha512", RTMANIFEST_ATTR_SHA512  }
                };
                uint32_t fThisAttr = RTMANIFEST_ATTR_UNKNOWN;
                for (unsigned i = 0; i < RT_ELEMENTS(s_aAttributes); i++)
                    if (!RTStrICmp(s_aAttributes[i].pszAttr, ValueUnion.psz))
                    {
                        fThisAttr = s_aAttributes[i].fAttr;
                        break;
                    }
                if (fThisAttr == RTMANIFEST_ATTR_UNKNOWN)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown attribute type '%s'", ValueUnion.psz);

                if (fAttr == RTMANIFEST_ATTR_UNKNOWN)
                    fAttr = fThisAttr;
                else
                    fAttr |= fThisAttr;
                break;
            }

            case 'j':
                fStdFormat = false;
                break;

            case 'm':
                if (pszManifest)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Only one manifest can be specified");
                pszManifest = ValueUnion.psz;
                break;

            case 'v':
                fVerify = true;
                break;

            case 'C':
                if (pszChDir)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Only one directory change can be specified");
                pszChDir = ValueUnion.psz;
                break;

            case 'h':
                RTPrintf("Usage: %s [--manifest <file>] [--chdir <dir>] [--attribute <attrib-name> [..]] <files>\n"
                         "   or  %s --verify [--manifest <file>] [--chdir <dir>]\n"
                         "\n"
                         "attrib-name: size, md5, sha1, sha256 or sha512\n"
                         , RTProcShortName(), RTProcShortName());
                return RTEXITCODE_SUCCESS;

#ifndef IN_BLD_PROG  /* RTBldCfgVersion or RTBldCfgRevision in build time IPRT lib. */
            case 'V':
                RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                return RTEXITCODE_SUCCESS;
#endif

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Take action.
     */
    RTEXITCODE rcExit;
    if (!fVerify)
    {
        if (rc != VINF_GETOPT_NOT_OPTION)
            RTMsgWarning("No files specified, the manifest will be empty.");
        if (fAttr == RTMANIFEST_ATTR_UNKNOWN)
            fAttr = RTMANIFEST_ATTR_SIZE | RTMANIFEST_ATTR_MD5
                  | RTMANIFEST_ATTR_SHA1 | RTMANIFEST_ATTR_SHA256 | RTMANIFEST_ATTR_SHA512;
        rcExit = rtManifestDoCreate(pszManifest, fStdFormat, pszChDir, fAttr, &GetState, &ValueUnion, rc);
    }
    else
    {
        if (rc == VINF_GETOPT_NOT_OPTION)
            return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                  "No files should be specified when verifying a manifest (--verfiy), "
                                  "only a manifest via the --manifest option");
        if (fAttr != RTMANIFEST_ATTR_UNKNOWN)
            return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                  "The --attribute (-a) option does not combine with --verify (-v)");


        rcExit = rtManifestDoVerify(pszManifest, fStdFormat, pszChDir);
    }

    return rcExit;
}

