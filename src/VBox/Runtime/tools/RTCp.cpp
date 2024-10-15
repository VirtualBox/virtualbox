/* $Id: RTCp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - cp like utility.
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
#include <iprt/file.h>
#include <iprt/fs.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * CAT command options.
 */
typedef struct RTCMDCPOPTS
{
    /** -v, --verbose. */
    bool            fVerbose;

    /** -H */
    bool            fFollowCommandLineSymlinks;

    /** Set if recursive copy. */
    bool            fRecursive;
    /** -x, --one-filesystem. */
    bool            fOneFileSystem;

    /** Special --no-replace-nor-trucate hack for basic NTFS write support. */
    bool            fNoReplaceNorTruncate;

    /** Number of sources. */
    size_t          cSources;
    /** Source files/dirs. */
    const char    **papszSources;
    /** Destination dir/file. */
    const char     *pszDestination;
} RTCMDCPOPTS;
/** Pointer to const CAT options. */
typedef RTCMDCPOPTS const *PCRTCMDCPOPTS;



/**
 * Does the copying, source by source.
 *
 * @returns exit code.
 * @param   pOpts       Options.
 */
static RTEXITCODE rtCmdCpDoIt(PCRTCMDCPOPTS pOpts)
{
    /*
     * Check out what the destination is.
     */
/** @todo need to cache + share VFS chain elements here! */
    RTERRINFOSTATIC ErrInfo;
    uint32_t        offError;
    RTFSOBJINFO     DstObjInfo;
    int rc = RTVfsChainQueryInfo(pOpts->pszDestination, &DstObjInfo, RTFSOBJATTRADD_UNIX, RTPATH_F_FOLLOW_LINK,
                                 &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        if (pOpts->cSources > 1 && !RTFS_IS_DIRECTORY(DstObjInfo.Attr.fMode))
            return RTMsgErrorExitFailure("Mutiple files to copy and destination is not a directory!");
    }
    else if (rc != VERR_FILE_NOT_FOUND)
        return RTVfsChainMsgErrorExitFailure("RTVfsChainQueryInfo", pOpts->pszDestination, rc, offError, &ErrInfo.Core);
    else
        RT_ZERO(DstObjInfo);
#if !RT_GNUC_PREREQ(8,2) || RT_GNUC_PREREQ(8,3) /* GCC 8.2 produces a tautological compare warning/error here. */
    AssertCompile(!RTFS_IS_DIRECTORY(0));
#endif

    /*
     * Process the sources.
     */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    for (size_t iSrc = 0; iSrc < pOpts->cSources; iSrc++)
    {
        const char  *pszSrc = pOpts->papszSources[iSrc];
        RTFSOBJINFO  SrcObjInfo;
        RT_ZERO(SrcObjInfo);
        rc = RTVfsChainQueryInfo(pszSrc, &SrcObjInfo, RTFSOBJATTRADD_UNIX,
                                 pOpts->fFollowCommandLineSymlinks ? RTPATH_F_FOLLOW_LINK : RTPATH_F_ON_LINK,
                                 &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
        {
            rcExit = RTVfsChainMsgErrorExitFailure("RTVfsChainQueryInfo", pszSrc, rc, offError, &ErrInfo.Core);
            continue;
        }

        /*
         * Regular file.
         */
        if (RTFS_IS_FILE(SrcObjInfo.Attr.fMode))
        {
            /* Open source. */
            RTVFSFILE hVfsSrc;
            rc = RTVfsChainOpenFile(pszSrc, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                    &hVfsSrc, &offError, RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(rc))
            {
                /* Make destination name if necessary and open destination.
                   Note! RTFILE_O_READ needed for VFS chains.  */
                char szDstPath[RTPATH_MAX];
                const char *pszDst = pOpts->pszDestination;
                if (RTFS_IS_DIRECTORY(DstObjInfo.Attr.fMode))
                {
                    rc = RTPathJoin(szDstPath, sizeof(szDstPath), pszDst, RTPathFilename(pszSrc));
                    pszDst = szDstPath;
                }
                if (RT_SUCCESS(rc))
                {
                    RTVFSFILE hVfsDst;
                    uint64_t fDstFlags = (pOpts->fNoReplaceNorTruncate ? RTFILE_O_OPEN_CREATE : RTFILE_O_CREATE_REPLACE)
                                       | RTFILE_O_READWRITE | RTFILE_O_DENY_WRITE | (0666 << RTFILE_O_CREATE_MODE_SHIFT);
                    rc = RTVfsChainOpenFile(pszDst, fDstFlags, &hVfsDst, &offError, RTErrInfoInitStatic(&ErrInfo));
                    if (RT_SUCCESS(rc))
                    {
                        /* Copy the bytes. */
                        RTVFSIOSTREAM hVfsIosSrc = RTVfsFileToIoStream(hVfsSrc);
                        RTVFSIOSTREAM hVfsIosDst = RTVfsFileToIoStream(hVfsDst);

                        rc = RTVfsUtilPumpIoStreams(hVfsIosSrc, hVfsIosDst, 0);

                        if (RT_SUCCESS(rc))
                        {
                            if (pOpts->fVerbose)
                                RTPrintf("'%s' -> '%s'\n", pszSrc, pszDst);
                        }
                        else
                            rcExit = RTMsgErrorExitFailure("RTVfsUtilPumpIoStreams failed for '%s' -> '%s': %Rrc",
                                                           pszSrc, pszDst, rc);
                        RTVfsIoStrmRelease(hVfsIosSrc);
                        RTVfsIoStrmRelease(hVfsIosDst);
                        RTVfsFileRelease(hVfsDst);
                    }
                    else
                        rcExit = RTVfsChainMsgErrorExitFailure("RTVfsChainOpenFile", pszDst, rc, offError, &ErrInfo.Core);
                }
                else
                    rcExit = RTMsgErrorExitFailure("Destination path too long for source #%u (%Rrc): %s", iSrc, pszSrc, rc);
                RTVfsFileRelease(hVfsSrc);
            }
            else
                rcExit = RTVfsChainMsgErrorExitFailure("RTVfsChainOpenFile", pszSrc, rc, offError, &ErrInfo.Core);
        }
        /*
         * Copying a directory requires the -R option to be active.
         */
        else if (RTFS_IS_DIRECTORY(SrcObjInfo.Attr.fMode))
        {
            if (pOpts->fRecursive)
            {
                /** @todo recursive copy */
                rcExit = RTMsgErrorExitFailure("Recursion not implemented yet!");
            }
            else
                rcExit = RTMsgErrorExitFailure("Source #%u is a directory: %s", iSrc + 1, pszSrc);
        }
        /*
         * We currently don't support copying any other file types.
         */
        else
            rcExit = RTMsgErrorExitFailure("Source #%u neither a file nor a directory: %s", iSrc + 1, pszSrc);
    }
    return rcExit;
}


/**
 * A /bin/cp clone.
 *
 * @returns Program exit code.
 *
 * @param   cArgs               The number of arguments.
 * @param   papszArgs           The argument vector.  (Note that this may be
 *                              reordered, so the memory must be writable.)
 */
static RTEXITCODE RTCmdCp(unsigned cArgs, char **papszArgs)
{

    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--archive",                          'a', RTGETOPT_REQ_NOTHING },
        { "--backup",                           'B', RTGETOPT_REQ_STRING  },
        { "",                                   'b', RTGETOPT_REQ_NOTHING },
        { "--copy-contents",                   1024, RTGETOPT_REQ_NOTHING },
        { "",                                   'd', RTGETOPT_REQ_NOTHING },
        { "--no-dereference",                   'P', RTGETOPT_REQ_NOTHING },
        { "--force",                            'f', RTGETOPT_REQ_NOTHING },
        { "",                                   'H', RTGETOPT_REQ_NOTHING },
        { "--link",                             'l', RTGETOPT_REQ_NOTHING },
        { "--dereference",                      'L', RTGETOPT_REQ_NOTHING },
        { "",                                   'p', RTGETOPT_REQ_NOTHING },
        { "--preserve",                        1026, RTGETOPT_REQ_STRING  },
        { "--no-preserve",                     1027, RTGETOPT_REQ_STRING  },
        { "--recursive",                        'R', RTGETOPT_REQ_NOTHING },
        { "--remove-destination",              1028, RTGETOPT_REQ_NOTHING },
        { "--reply",                           1029, RTGETOPT_REQ_STRING  },
        { "--sparse",                          1030, RTGETOPT_REQ_STRING  },
        { "--strip-trailing-slashes",          1031, RTGETOPT_REQ_NOTHING },
        { "--symbolic-links",                   's', RTGETOPT_REQ_NOTHING },
        { "--suffix",                           'S', RTGETOPT_REQ_STRING  },
        { "--target-directory",                 't', RTGETOPT_REQ_STRING  },
        { "--no-target-directory",              'T', RTGETOPT_REQ_NOTHING },
        { "--update",                           'u', RTGETOPT_REQ_NOTHING },
        { "--verbose",                          'v', RTGETOPT_REQ_NOTHING },
        { "--one-file-system",                  'x', RTGETOPT_REQ_NOTHING },
        { "--no-replace-nor-trucate",          1032, RTGETOPT_REQ_NOTHING },
    };

    RTCMDCPOPTS Opts;
    Opts.fVerbose                   = false;
    Opts.fFollowCommandLineSymlinks = false;
    Opts.fRecursive                 = false;
    Opts.fOneFileSystem             = false;
    Opts.fNoReplaceNorTruncate      = false;
    Opts.pszDestination             = NULL;
    Opts.cSources                   = 0;
    Opts.papszSources               = (const char **)RTMemAllocZ(sizeof(const char *) * (cArgs + 2));
    AssertReturn(Opts.papszSources, RTEXITCODE_FAILURE);

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

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
                case 0:
                    if (Opts.pszDestination == NULL && Opts.cSources == 0)
                        rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing source and destination");
                    else if (Opts.pszDestination == NULL && Opts.cSources == 1)
                        rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing destination");
                    else if (Opts.pszDestination != NULL && Opts.cSources == 0)
                        rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing source");
                    else
                    {
                        if (Opts.pszDestination == NULL && Opts.cSources > 0)
                            Opts.pszDestination = Opts.papszSources[--Opts.cSources];
                        Assert(Opts.cSources > 0);
                        rcExit = rtCmdCpDoIt(&Opts);
                    }
                    fContinue = false;
                    break;

                case VINF_GETOPT_NOT_OPTION:
                    Assert(Opts.cSources < cArgs);
                    Opts.papszSources[Opts.cSources++] = ValueUnion.psz;
                    break;

                case 'H':
                    Opts.fFollowCommandLineSymlinks = true;
                    break;

                case 'R':
                    Opts.fRecursive = true;
                    break;
                case 'x':
                    Opts.fOneFileSystem = true;
                    break;

                case 'v':
                    Opts.fVerbose = true;
                    break;

                case 1032:
                    Opts.fNoReplaceNorTruncate = true;
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
    RTMemFree(Opts.papszSources);
    return rcExit;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);
    return RTCmdCp(argc, argv);
}

