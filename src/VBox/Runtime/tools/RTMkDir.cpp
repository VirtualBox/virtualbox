/* $Id: RTMkDir.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Creates directory.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <iprt/path.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/message.h>

#include <iprt/vfs.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/getopt.h>
#include <iprt/buildconfig.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTCMDMKDIROPTS
{
    /** -v, --verbose */
    bool        fVerbose;
    /** -p, --parents */
    bool        fParents;
    /** Whether to always use the VFS chain API (for testing). */
    bool        fAlwaysUseChainApi;
    /** Directory creation flags (RTDIRCREATE_FLAGS_XXX).   */
    uint32_t    fCreateFlags;
    /** The directory mode. */
    RTFMODE     fMode;
} RTCMDMKDIROPTS;


/**
 * Create one directory and any missing parent directories.
 *
 * @returns exit code
 * @param   pOpts               The mkdir option.
 * @param   pszDir              The path to the new directory.
 */
static int rtCmdMkDirOneWithParents(RTCMDMKDIROPTS const *pOpts, const char *pszDir)
{
    int rc;
    if (!pOpts->fAlwaysUseChainApi && !RTVfsChainIsSpec(pszDir) )
    {
        /*
         * Use the API for doing the entire job.  Unfortuantely, this means we
         * can't be very  verbose about what we're doing.
         */
        rc = RTDirCreateFullPath(pszDir, pOpts->fMode);
        if (RT_FAILURE(rc))
            RTMsgError("Failed to create directory '%s' (or a parent): %Rrc", pszDir, rc);
        else if (pOpts->fVerbose)
            RTPrintf("%s\n", pszDir);
    }
    else
    {
        /*
         * Strip the final path element from the pszDir spec.
         */
        char *pszCopy = RTStrDup(pszDir);
        if (!pszCopy)
            return RTMsgErrorExitFailure("Out of string memory!");

        char       *pszFinalPath;
        char       *pszSpec;
        uint32_t    offError;
        rc = RTVfsChainSplitOffFinalPath(pszCopy, &pszSpec, &pszFinalPath, &offError);
        if (RT_SUCCESS(rc))
        {
            const char * const pszFullFinalPath = pszFinalPath;

            /*
             * Open the root director/whatever.
             */
            RTERRINFOSTATIC ErrInfo;
            RTVFSDIR hVfsCurDir;
            if (pszSpec)
            {
                rc = RTVfsChainOpenDir(pszSpec, 0 /*fOpen*/, &hVfsCurDir, &offError, RTErrInfoInitStatic(&ErrInfo));
                if (RT_FAILURE(rc))
                    RTVfsChainMsgError("RTVfsChainOpenDir", pszSpec, rc, offError, &ErrInfo.Core);
                else if (!pszFinalPath)
                    pszFinalPath = RTStrEnd(pszSpec, RTSTR_MAX);
            }
            else if (!RTPathStartsWithRoot(pszFinalPath))
            {
                rc = RTVfsDirOpenNormal(".", 0 /*fOpen*/, &hVfsCurDir);
                if (RT_FAILURE(rc))
                    RTMsgError("Failed to open '.' (for %s): %Rrc", rc, pszFinalPath);
            }
            else
            {
                char *pszRoot = pszFinalPath;
                pszFinalPath = RTPathSkipRootSpec(pszFinalPath);
                char const chSaved = *pszFinalPath;
                *pszFinalPath = '\0';
                rc = RTVfsDirOpenNormal(pszRoot, 0 /*fOpen*/, &hVfsCurDir);
                *pszFinalPath = chSaved;
                if (RT_FAILURE(rc))
                    RTMsgError("Failed to open root dir for '%s': %Rrc", rc, pszRoot);
            }

            /*
             * Walk the path component by component.
             */
            while (RT_SUCCESS(rc))
            {
                /*
                 * Strip leading slashes.
                 */
                while (RTPATH_IS_SLASH(*pszFinalPath))
                    pszFinalPath++;
                if (*pszFinalPath == '\0')
                {
                    RTVfsDirRelease(hVfsCurDir);
                    break;
                }

                /*
                 * Find the end of the next path component.
                 */
                size_t cchComponent = 0;
                char   ch;
                while (   (ch = pszFinalPath[cchComponent]) != '\0'
                       && !RTPATH_IS_SLASH(ch))
                    cchComponent++;

                /*
                 * Open or create the component.
                 */
                pszFinalPath[cchComponent] = '\0';
                RTVFSDIR hVfsNextDir = NIL_RTVFSDIR;
                for (uint32_t cTries = 0; cTries < 8; cTries++)
                {
                    /* Try open it. */
                    rc = RTVfsDirOpenDir(hVfsCurDir, pszFinalPath, 0 /*fFlags*/, &hVfsNextDir);
                    if (RT_SUCCESS(rc))
                        break;
                    if (   rc != VERR_FILE_NOT_FOUND
                        && rc != VERR_PATH_NOT_FOUND)
                    {
                        if (ch == '\0')
                            RTMsgError("Failed opening directory '%s': %Rrc", pszDir, rc);
                        else
                            RTMsgError("Failed opening dir '%s' (for creating '%s'): %Rrc", pszFullFinalPath, pszDir, rc);
                        break;
                    }

                    /* Not found, so try create it. */
                    rc = RTVfsDirCreateDir(hVfsCurDir, pszFinalPath, pOpts->fMode, pOpts->fCreateFlags, &hVfsNextDir);
                    if (rc == VERR_ALREADY_EXISTS)
                        continue; /* We lost a creation race, try again. */
                    if (RT_SUCCESS(rc) && pOpts->fVerbose)
                    {
                        if (pszSpec)
                            RTPrintf("%s:%s\n", pszSpec, pszFullFinalPath);
                        else
                            RTPrintf("%s\n", pszFullFinalPath);
                    }
                    else if (RT_FAILURE(rc))
                    {
                        if (ch == '\0')
                            RTMsgError("Failed creating directory '%s': %Rrc", pszDir, rc);
                        else
                            RTMsgError("Failed creating dir '%s' (for '%s'): %Rrc", pszFullFinalPath, pszDir, rc);
                    }
                    break;
                }
                pszFinalPath[cchComponent] = ch;

                RTVfsDirRelease(hVfsCurDir);
                hVfsCurDir = hVfsNextDir;
                pszFinalPath += cchComponent;
            }
        }
        else
            RTVfsChainMsgError("RTVfsChainOpenParentDir", pszCopy, rc, offError, NULL);
        RTStrFree(pszCopy);
    }
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Create one directory.
 *
 * @returns exit code
 * @param   pOpts               The mkdir option.
 * @param   pszDir              The path to the new directory.
 */
static RTEXITCODE rtCmdMkDirOne(RTCMDMKDIROPTS const *pOpts, const char *pszDir)
{
    int rc;
    if (!pOpts->fAlwaysUseChainApi && !RTVfsChainIsSpec(pszDir) )
        rc = RTDirCreate(pszDir, pOpts->fMode, 0);
    else
    {
        RTVFSDIR        hVfsDir;
        const char     *pszChild;
        uint32_t        offError;
        RTERRINFOSTATIC ErrInfo;
        rc = RTVfsChainOpenParentDir(pszDir, 0 /*fOpen*/, &hVfsDir, &pszChild, &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            rc = RTVfsDirCreateDir(hVfsDir, pszChild, pOpts->fMode, 0 /*fFlags*/, NULL);
            RTVfsDirRelease(hVfsDir);
        }
        else
            return RTVfsChainMsgErrorExitFailure("RTVfsChainOpenParentDir", pszDir, rc, offError, &ErrInfo.Core);
    }
    if (RT_SUCCESS(rc))
    {
        if (pOpts->fVerbose)
            RTPrintf("%s\n", pszDir);
        return RTEXITCODE_SUCCESS;
    }
    return RTMsgErrorExitFailure("Failed to create '%s': %Rrc", pszDir, rc);
}


static RTEXITCODE RTCmdMkDir(unsigned cArgs,  char **papszArgs)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* operations */
        { "--mode",                     'm', RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_OCT },
        { "--parents",                  'p', RTGETOPT_REQ_NOTHING },
        { "--always-use-vfs-chain-api", 'A', RTGETOPT_REQ_NOTHING },
        { "--allow-content-indexing",   'i', RTGETOPT_REQ_NOTHING },
        { "--verbose",                  'v', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOpt failed: %Rrc", rc);

    RTCMDMKDIROPTS Opts;
    Opts.fVerbose               = false;
    Opts.fParents               = false;
    Opts.fAlwaysUseChainApi     = false;
    Opts.fCreateFlags           = RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL | RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_SET;
    Opts.fMode                  = 0775 | RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY;

    RTGETOPTUNION ValueUnion;
    while (   (rc = RTGetOpt(&GetState, &ValueUnion)) != 0
           && rc != VINF_GETOPT_NOT_OPTION)
    {
        switch (rc)
        {
            case 'm':
                /** @todo DOS+NT attributes and symbolic notation. */
                Opts.fMode &= ~07777;
                Opts.fMode |= ValueUnion.u32 & 07777;
                break;

            case 'p':
                Opts.fParents = true;
                break;

            case 'v':
                Opts.fVerbose = true;
                break;

            case 'A':
                Opts.fAlwaysUseChainApi = true;
                break;

            case 'i':
                Opts.fCreateFlags &= ~RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_SET;
                Opts.fCreateFlags |= RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_DONT_SET;
                break;

            case 'h':
                RTPrintf("Usage: %s [options] <dir> [..]\n"
                         "\n"
                         "Options:\n"
                         "  -m <mode>, --mode <mode>\n"
                         "      The creation mode. Default is 0775.\n"
                         "  -p, --parent\n"
                         "      Create parent directories too. Ignore any existing directories.\n"
                         "  -v, --verbose\n"
                         "      Tell which directories get created.\n"
                         "  -A, --always-use-vfs-chain-api\n"
                         "      Always use the VFS API.\n"
                         "  -i, --allow-content-indexing\n"
                         "      Don't set flags to disable context indexing on windows.\n"
                         , papszArgs[0]);
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }


    /*
     * No files means error.
     */
    if (rc != VINF_GETOPT_NOT_OPTION)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No directories specified.\n");

    /*
     * Work thru the specified dirs.
     */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    while (rc == VINF_GETOPT_NOT_OPTION)
    {
        if (Opts.fParents)
            rc = rtCmdMkDirOneWithParents(&Opts, ValueUnion.psz);
        else
            rc = rtCmdMkDirOne(&Opts, ValueUnion.psz);
        if (RT_FAILURE(rc))
            rcExit = RTEXITCODE_FAILURE;

        /* next */
        rc = RTGetOpt(&GetState, &ValueUnion);
    }
    if (rc != 0)
        rcExit = RTGetOptPrintError(rc, &ValueUnion);

    return rcExit;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);
    return RTCmdMkDir(argc, argv);
}

