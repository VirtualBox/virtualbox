/* $Id: RTRmDir.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Removes directory.
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
typedef struct RTCMDRMDIROPTS
{
    /** -v, --verbose */
    bool        fVerbose;
    /** -p, --parents */
    bool        fParents;
    /** Don't fail if directories that aren't empty. */
    bool        fIgnoreNotEmpty;
    /** Don't fail a directory doesn't exist (i.e. has already been removed). */
    bool        fIgnoreNonExisting;
    /** Whether to always use the VFS chain API (for testing). */
    bool        fAlwaysUseChainApi;
} RTCMDRMDIROPTS;


/**
 * Create one directory and any missing parent directories.
 *
 * @returns exit code
 * @param   pOpts               The mkdir option.
 * @param   pszDir              The path to the new directory.
 */
static int rtCmdRmDirOneWithParents(RTCMDRMDIROPTS const *pOpts, const char *pszDir)
{
    /* We need a copy we can work with here. */
    char *pszCopy = RTStrDup(pszDir);
    if (!pszCopy)
        return RTMsgErrorExitFailure("Out of string memory!");

    int rc;
    if (!pOpts->fAlwaysUseChainApi && !RTVfsChainIsSpec(pszDir) )
    {
        size_t cchCopy = strlen(pszCopy);
        do
        {
            rc = RTDirRemove(pszCopy);
            if (RT_SUCCESS(rc))
            {
                if (pOpts->fVerbose)
                    RTPrintf("%s\n", pszCopy);
            }
            else if ((rc == VERR_PATH_NOT_FOUND || rc == VERR_FILE_NOT_FOUND) && pOpts->fIgnoreNonExisting)
                rc = VINF_SUCCESS;
            else
            {
                if ((rc == VERR_DIR_NOT_EMPTY || rc == VERR_SHARING_VIOLATION) && pOpts->fIgnoreNotEmpty)
                    rc = VINF_SUCCESS;
                else
                    RTMsgError("Failed to remove directory '%s': %Rrc", pszCopy, rc);
                break;
            }

            /* Strip off a component. */
            while (cchCopy > 0 && RTPATH_IS_SLASH(pszCopy[cchCopy - 1]))
                cchCopy--;
            while (cchCopy > 0 && !RTPATH_IS_SLASH(pszCopy[cchCopy - 1]))
                cchCopy--;
            while (cchCopy > 0 && RTPATH_IS_SLASH(pszCopy[cchCopy - 1]))
                cchCopy--;
            pszCopy[cchCopy] = '\0';
        } while (cchCopy > 0);
    }
    else
    {
        /*
         * Strip the final path element from the pszDir spec.
         */
        char       *pszFinalPath;
        char       *pszSpec;
        uint32_t    offError;
        rc = RTVfsChainSplitOffFinalPath(pszCopy, &pszSpec, &pszFinalPath, &offError);
        if (RT_SUCCESS(rc))
        {
            /*
             * Open the root director/whatever.
             */
            RTERRINFOSTATIC ErrInfo;
            RTVFSDIR hVfsBaseDir;
            if (pszSpec)
            {
                rc = RTVfsChainOpenDir(pszSpec, 0 /*fOpen*/, &hVfsBaseDir, &offError, RTErrInfoInitStatic(&ErrInfo));
                if (RT_FAILURE(rc))
                    RTVfsChainMsgError("RTVfsChainOpenDir", pszSpec, rc, offError, &ErrInfo.Core);
                else if (!pszFinalPath)
                    pszFinalPath = RTStrEnd(pszSpec, RTSTR_MAX);
            }
            else if (!RTPathStartsWithRoot(pszFinalPath))
            {
                rc = RTVfsDirOpenNormal(".", 0 /*fOpen*/, &hVfsBaseDir);
                if (RT_FAILURE(rc))
                    RTMsgError("Failed to open '.' (for %s): %Rrc", rc, pszFinalPath);
            }
            else
            {
                char *pszRoot = pszFinalPath;
                pszFinalPath = RTPathSkipRootSpec(pszFinalPath);
                char const chSaved = *pszFinalPath;
                *pszFinalPath = '\0';
                rc = RTVfsDirOpenNormal(pszRoot, 0 /*fOpen*/, &hVfsBaseDir);
                *pszFinalPath = chSaved;
                if (RT_FAILURE(rc))
                    RTMsgError("Failed to open root dir for '%s': %Rrc", rc, pszRoot);
            }

            /*
             * Walk the path component by component, starting at the end.
             */
            if (RT_SUCCESS(rc))
            {
                size_t cchFinalPath = strlen(pszFinalPath);
                while (RT_SUCCESS(rc) && cchFinalPath > 0)
                {
                    rc = RTVfsDirRemoveDir(hVfsBaseDir, pszFinalPath, 0 /*fFlags*/);
                    if (RT_SUCCESS(rc))
                    {
                        if (pOpts->fVerbose)
                            RTPrintf("%s\n", pszCopy);
                    }
                    else if ((rc == VERR_PATH_NOT_FOUND || rc == VERR_FILE_NOT_FOUND) && pOpts->fIgnoreNonExisting)
                        rc = VINF_SUCCESS;
                    else
                    {
                        if ((rc == VERR_DIR_NOT_EMPTY || rc == VERR_SHARING_VIOLATION) && pOpts->fIgnoreNotEmpty)
                            rc = VINF_SUCCESS;
                        else if (pszSpec)
                            RTMsgError("Failed to remove directory '%s:%s': %Rrc", pszSpec, pszFinalPath, rc);
                        else
                            RTMsgError("Failed to remove directory '%s': %Rrc", pszFinalPath, rc);
                        break;
                    }

                    /* Strip off a component. */
                    while (cchFinalPath > 0 && RTPATH_IS_SLASH(pszFinalPath[cchFinalPath - 1]))
                        cchFinalPath--;
                    while (cchFinalPath > 0 && !RTPATH_IS_SLASH(pszFinalPath[cchFinalPath - 1]))
                        cchFinalPath--;
                    while (cchFinalPath > 0 && RTPATH_IS_SLASH(pszFinalPath[cchFinalPath - 1]))
                        cchFinalPath--;
                    pszFinalPath[cchFinalPath] = '\0';
                }

                RTVfsDirRelease(hVfsBaseDir);
            }
        }
        else
            RTVfsChainMsgError("RTVfsChainOpenParentDir", pszCopy, rc, offError, NULL);
    }
    RTStrFree(pszCopy);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Removes one directory.
 *
 * @returns exit code
 * @param   pOpts               The mkdir option.
 * @param   pszDir              The path to the new directory.
 */
static RTEXITCODE rtCmdRmDirOne(RTCMDRMDIROPTS const *pOpts, const char *pszDir)
{
    int rc;
    if (!pOpts->fAlwaysUseChainApi && !RTVfsChainIsSpec(pszDir) )
        rc = RTDirRemove(pszDir);
    else
    {
        RTVFSDIR        hVfsDir;
        const char     *pszChild;
        uint32_t        offError;
        RTERRINFOSTATIC ErrInfo;
        rc = RTVfsChainOpenParentDir(pszDir, 0 /*fOpen*/, &hVfsDir, &pszChild, &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            rc = RTVfsDirRemoveDir(hVfsDir, pszChild, 0 /*fFlags*/);
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
    if ((rc == VERR_DIR_NOT_EMPTY || rc == VERR_SHARING_VIOLATION) && pOpts->fIgnoreNotEmpty)
        return RTEXITCODE_SUCCESS; /** @todo be verbose about this? */
    if ((rc == VERR_PATH_NOT_FOUND || rc == VERR_FILE_NOT_FOUND) && pOpts->fIgnoreNonExisting)
        return RTEXITCODE_SUCCESS; /** @todo be verbose about this? */
    return RTMsgErrorExitFailure("Failed to remove '%s': %Rrc", pszDir, rc);
}


static RTEXITCODE RTCmdRmDir(unsigned cArgs,  char **papszArgs)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* operations */
        { "--parents",                  'p', RTGETOPT_REQ_NOTHING },
        { "--ignore-fail-on-non-empty", 'F', RTGETOPT_REQ_NOTHING },
        { "--ignore-non-existing",      'E', RTGETOPT_REQ_NOTHING },
        { "--always-use-vfs-chain-api", 'A', RTGETOPT_REQ_NOTHING },
        { "--verbose",                  'v', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOpt failed: %Rrc", rc);

    RTCMDRMDIROPTS Opts;
    Opts.fVerbose               = false;
    Opts.fParents               = false;
    Opts.fIgnoreNotEmpty        = false;
    Opts.fIgnoreNonExisting     = false;
    Opts.fAlwaysUseChainApi     = false;

    RTGETOPTUNION ValueUnion;
    while (   (rc = RTGetOpt(&GetState, &ValueUnion)) != 0
           && rc != VINF_GETOPT_NOT_OPTION)
    {
        switch (rc)
        {
            case 'p':
                Opts.fParents = true;
                break;

            case 'v':
                Opts.fVerbose = true;
                break;

            case 'A':
                Opts.fAlwaysUseChainApi = true;
                break;

            case 'E':
                Opts.fIgnoreNonExisting = true;
                break;

            case 'F':
                Opts.fIgnoreNotEmpty = true;
                break;

            case 'h':
                RTPrintf("Usage: %s [options] <dir> [..]\n"
                         "\n"
                         "Removes empty directories.\n"
                         "\n"
                         "Options:\n"
                         "  -p, --parent\n"
                         "      Remove specified parent directories too.\n"
                         "  -F, --ignore-fail-on-non-empty\n"
                         "      Do not fail if a directory is not empty, just ignore it.\n"
                         "      This is really handy with the -p option.\n"
                         "  -E, --ignore-non-existing\n"
                         "      Do not fail if a specified directory is not there.\n"
                         "  -v, --verbose\n"
                         "      Tell which directories get remove.\n"
                         "  -A, --always-use-vfs-chain-api\n"
                         "      Always use the VFS API.\n"
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
            rc = rtCmdRmDirOneWithParents(&Opts, ValueUnion.psz);
        else
            rc = rtCmdRmDirOne(&Opts, ValueUnion.psz);
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
    return RTCmdRmDir(argc, argv);
}

