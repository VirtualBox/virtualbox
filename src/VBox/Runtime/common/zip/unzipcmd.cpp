/* $Id: unzipcmd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - A mini UNZIP Command.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include <iprt/zip.h>
#include <iprt/asm.h>
#include <iprt/getopt.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/stream.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * IPRT UNZIP option structure.
 */
typedef struct RTZIPUNZIPCMDOPS
{
    /** The operation. */
    int            iOperation;
    /** The long operation option name. */
    const char     *pszOperation;
    /** The directory to change into when upacking. */
    const char     *pszDirectory;
    /** The unzip file name. */
    const char     *pszFile;
    /** The number of files/directories to be extracted from archive specified. */
    uint32_t       cFiles;
    /** Wether we're verbose or quiet. */
    bool           fVerbose;
    /** Skip the restauration of the modification time for directories. */
    bool           fNoModTimeDirectories;
    /** Skip the restauration of the modification time for files. */
    bool           fNoModTimeFiles;
    /** Array of files/directories, terminated by a NULL entry. */
    const char * const *papszFiles;
} RTZIPUNZIPCMDOPS;
/** Pointer to the UNZIP options. */
typedef RTZIPUNZIPCMDOPS *PRTZIPUNZIPCMDOPS;

/**
 * Callback used by rtZipUnzipDoWithMembers
 *
 * @returns rcExit or RTEXITCODE_FAILURE.
 * @param   pOpts               The Unzip options.
 * @param   hVfsObj             The Unzip object to display
 * @param   pszName             The name.
 * @param   rcExit              The current exit code.
 */
typedef RTEXITCODE (*PFNDOWITHMEMBER)(PRTZIPUNZIPCMDOPS pOpts, RTVFSOBJ hVfsObj, const char *pszName, RTEXITCODE rcExit, PRTFOFF pcBytes);


/**
 *
 */
static RTEXITCODE rtZipUnzipCmdListCallback(PRTZIPUNZIPCMDOPS pOpts, RTVFSOBJ hVfsObj,
                                            const char *pszName, RTEXITCODE rcExit, PRTFOFF pcBytes)
{
    RT_NOREF_PV(pOpts);

    /*
     * Query all the information.
     */
    RTFSOBJINFO UnixInfo;
    int rc = RTVfsObjQueryInfo(hVfsObj, &UnixInfo, RTFSOBJATTRADD_UNIX);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsObjQueryInfo returned %Rrc on '%s'", rc, pszName);

    RTTIME time;
    if (!RTTimeExplode(&time, &UnixInfo.ModificationTime))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Cannot explode time on '%s'", pszName);

    RTPrintf("%9RU64  %04d-%02d-%02d %02d:%02d   %s\n",
             UnixInfo.cbObject,
             time.i32Year, time.u8Month, time.u8MonthDay,
             time.u8Hour, time.u8Minute,
             pszName);

    *pcBytes = UnixInfo.cbObject;
    return rcExit;
}


/**
 * Extracts a file.
 */
static RTEXITCODE rtZipUnzipCmdExtractFile(PRTZIPUNZIPCMDOPS pOpts, RTVFSOBJ hVfsObj, RTEXITCODE rcExit,
                                           const char *pszDst, PCRTFSOBJINFO pUnixInfo)
{
    /*
     * Open the destination file and create a stream object for it.
     */
    uint32_t fOpen = RTFILE_O_READWRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_ACCESS_ATTR_DEFAULT
                   | (pUnixInfo->Attr.fMode << RTFILE_O_CREATE_MODE_SHIFT);
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszDst, fOpen);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error creating file: %Rrc", pszDst, rc);

    RTVFSIOSTREAM hVfsIosDst;
    rc = RTVfsIoStrmFromRTFile(hFile, fOpen, true /*fLeaveOpen*/, &hVfsIosDst);
    if (RT_SUCCESS(rc))
    {
        /*
         * Pump the data thru.
         */
        RTVFSIOSTREAM hVfsIosSrc = RTVfsObjToIoStream(hVfsObj);
        rc = RTVfsUtilPumpIoStreams(hVfsIosSrc, hVfsIosDst, (uint32_t)RT_MIN(pUnixInfo->cbObject, _1M));
        if (RT_SUCCESS(rc))
        {
            /*
             * Correct the file mode and other attributes.
             */
            if (!pOpts->fNoModTimeFiles)
            {
                rc = RTFileSetTimes(hFile, NULL, &pUnixInfo->ModificationTime, NULL, NULL);
                if (RT_FAILURE(rc))
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error setting times: %Rrc", pszDst, rc);
            }
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error writing out file: %Rrc", pszDst, rc);
        RTVfsIoStrmRelease(hVfsIosSrc);
        RTVfsIoStrmRelease(hVfsIosDst);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error creating I/O stream for file: %Rrc", pszDst, rc);

    return rcExit;
}


/**
 *
 */
static RTEXITCODE rtZipUnzipCmdExtractCallback(PRTZIPUNZIPCMDOPS pOpts, RTVFSOBJ hVfsObj,
                                               const char *pszName, RTEXITCODE rcExit, PRTFOFF pcBytes)
{
    if (pOpts->fVerbose)
        RTPrintf("%s\n", pszName);

    /*
     * Query all the information.
     */
    RTFSOBJINFO UnixInfo;
    int rc = RTVfsObjQueryInfo(hVfsObj, &UnixInfo, RTFSOBJATTRADD_UNIX);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsObjQueryInfo returned %Rrc on '%s'", rc, pszName);

    *pcBytes = UnixInfo.cbObject;

    char szDst[RTPATH_MAX];
    rc = RTPathJoin(szDst, sizeof(szDst), pOpts->pszDirectory ? pOpts->pszDirectory : ".", pszName);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Failed to construct destination path for: %Rrc", pszName, rc);

    /*
     * Extract according to the type.
     */
    switch (UnixInfo.Attr.fMode & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_FILE:
            return rtZipUnzipCmdExtractFile(pOpts, hVfsObj, rcExit, szDst, &UnixInfo);

        case RTFS_TYPE_DIRECTORY:
            rc = RTDirCreateFullPath(szDst, UnixInfo.Attr.fMode & RTFS_UNIX_ALL_ACCESS_PERMS);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error creating directory: %Rrc", szDst, rc);
            break;

        default:
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Unknown file type.", pszName);
    }

    if (!pOpts->fNoModTimeDirectories)
    {
        rc = RTPathSetTimesEx(szDst, NULL, &UnixInfo.ModificationTime, NULL, NULL, RTPATH_F_ON_LINK);
        if (RT_FAILURE(rc) && rc != VERR_NOT_SUPPORTED && rc != VERR_NS_SYMLINK_SET_TIME)
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Error changing modification time: %Rrc.", pszName, rc);
    }

    return rcExit;
}


/**
 * Checks if @a pszName is a member of @a papszNames, optionally returning the
 * index.
 *
 * @returns true if the name is in the list, otherwise false.
 * @param   pszName             The name to find.
 * @param   papszNames          The array of names.
 * @param   piName              Where to optionally return the array index.
 */
static bool rtZipUnzipCmdIsNameInArray(const char *pszName, const char * const *papszNames, uint32_t *piName)
{
    for (uint32_t iName = 0; papszNames[iName]; ++iName)
        if (!strcmp(papszNames[iName], pszName))
        {
            if (piName)
                *piName = iName;
            return true;
        }
    return false;
}


/**
 * Opens the input archive specified by the options.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + printed message.
 * @param   pOpts           The options.
 * @param   phVfsFss        Where to return the UNZIP filesystem stream handle.
 */
static RTEXITCODE rtZipUnzipCmdOpenInputArchive(PRTZIPUNZIPCMDOPS pOpts, PRTVFSFSSTREAM phVfsFss)
{
    /*
     * Open the input file.
     */
    RTVFSIOSTREAM   hVfsIos;
    uint32_t        offError = 0;
    RTERRINFOSTATIC ErrInfo;
    int rc = RTVfsChainOpenIoStream(pOpts->pszFile, RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN,
                                    &hVfsIos, &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTVfsChainMsgErrorExitFailure("RTVfsChainOpenIoStream", pOpts->pszFile, rc, offError, &ErrInfo.Core);

    rc = RTZipPkzipFsStreamFromIoStream(hVfsIos, 0 /*fFlags*/, phVfsFss);
    RTVfsIoStrmRelease(hVfsIos);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to open pkzip filesystem stream: %Rrc", rc);

    return RTEXITCODE_SUCCESS;
}


/**
 * Worker for the --list and --extract commands.
 *
 * @returns The appropriate exit code.
 * @param   pOpts               The Unzip options.
 * @param   pfnCallback         The command specific callback.
 */
static RTEXITCODE rtZipUnzipDoWithMembers(PRTZIPUNZIPCMDOPS pOpts, PFNDOWITHMEMBER pfnCallback,
                                          uint32_t *pcFiles, PRTFOFF pcBytes)
{
    /*
     * Allocate a bitmap to go with the file list.  This will be used to
     * indicate which files we've processed and which not.
     */
    uint32_t *pbmFound = NULL;
    if (pOpts->cFiles)
    {
        pbmFound = (uint32_t *)RTMemAllocZ(((pOpts->cFiles + 31) / 32) * sizeof(uint32_t));
        if (!pbmFound)
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to allocate the found-file-bitmap");
    }

    uint32_t cFiles = 0;
    RTFOFF cBytesSum = 0;

    /*
     * Open the input archive.
     */
    RTVFSFSSTREAM hVfsFssIn;
    RTEXITCODE rcExit = rtZipUnzipCmdOpenInputArchive(pOpts, &hVfsFssIn);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Process the stream.
         */
        for (;;)
        {
            /*
             * Retrieve the next object.
             */
            char       *pszName;
            RTVFSOBJ    hVfsObj;
            int rc = RTVfsFsStrmNext(hVfsFssIn, &pszName, NULL, &hVfsObj);
            if (RT_FAILURE(rc))
            {
                if (rc != VERR_EOF)
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsFsStrmNext returned %Rrc", rc);
                break;
            }

            /*
             * Should we process this object?
             */
            uint32_t iFile = UINT32_MAX;
            if (   !pOpts->cFiles
                || rtZipUnzipCmdIsNameInArray(pszName, pOpts->papszFiles, &iFile))
            {
                if (pbmFound)
                    ASMBitSet(pbmFound, iFile);

                RTFOFF cBytes = 0;
                rcExit = pfnCallback(pOpts, hVfsObj, pszName, rcExit, &cBytes);
                if (rcExit != RTEXITCODE_SUCCESS)
                    break;

                cBytesSum += cBytes;
                cFiles++;
            }

            /*
             * Release the current object and string.
             */
            RTVfsObjRelease(hVfsObj);
            RTStrFree(pszName);
        }

        /*
         * Complain about any files we didn't find.
         */
        for (uint32_t iFile = 0; iFile <pOpts->cFiles; iFile++)
            if (!ASMBitTest(pbmFound, iFile))
                rcExit = RTMsgErrorExitFailure("%s: Was not found in the archive", pOpts->papszFiles[iFile]);

        RTVfsFsStrmRelease(hVfsFssIn);
    }

    RTMemFree(pbmFound);

    *pcFiles = cFiles;
    *pcBytes = cBytesSum;

    return rcExit;
}


RTDECL(RTEXITCODE) RTZipUnzipCmd(unsigned cArgs, char **papszArgs)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* options */
        { NULL,            'c', RTGETOPT_REQ_NOTHING }, /* extract files to stdout/stderr */
        { NULL,            'd', RTGETOPT_REQ_STRING  }, /* extract files to this directory */
        { NULL,            'l', RTGETOPT_REQ_NOTHING }, /* list archive files (short format) */
        { NULL,            'p', RTGETOPT_REQ_NOTHING }, /* extract files to stdout */
        { NULL,            't', RTGETOPT_REQ_NOTHING }, /* test archive files */
        { NULL,            'v', RTGETOPT_REQ_NOTHING }, /* verbose */

        /* modifiers */
        { NULL,            'a', RTGETOPT_REQ_NOTHING }, /* convert text files */
        { NULL,            'b', RTGETOPT_REQ_NOTHING }, /* no conversion, treat as binary */
        { NULL,            'D', RTGETOPT_REQ_NOTHING }, /* don't restore timestamps for directories
                                                           (and files) */
    };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOpt failed: %Rrc", rc);

    RTZIPUNZIPCMDOPS Opts;
    RT_ZERO(Opts);

    RTGETOPTUNION  ValueUnion;
    while (   (rc = RTGetOpt(&GetState, &ValueUnion)) != 0
           && rc != VINF_GETOPT_NOT_OPTION)
    {
        switch (rc)
        {
            case 'd':
                if (Opts.pszDirectory)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "You may only specify -d once");
                Opts.pszDirectory = ValueUnion.psz;
                break;

            case 'D':
                if (!Opts.fNoModTimeDirectories)
                    Opts.fNoModTimeDirectories = true; /* -D */
                else
                    Opts.fNoModTimeFiles = true; /* -DD */
                break;

            case 'l':
            case 't': /* treat 'test' like 'list' */
                if (Opts.iOperation)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                          "Conflicting unzip operation (%s already set, now %s)",
                                          Opts.pszOperation, ValueUnion.pDef->pszLong);
                Opts.iOperation   = 'l';
                Opts.pszOperation = ValueUnion.pDef->pszLong;
                break;

            case 'v':
                Opts.fVerbose = true;
                break;

            default:
                Opts.pszFile = ValueUnion.psz;
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    if (rc == VINF_GETOPT_NOT_OPTION)
    {
        Assert((unsigned)GetState.iNext - 1 <= cArgs);
        Opts.pszFile = papszArgs[GetState.iNext - 1];
        if ((unsigned)GetState.iNext <= cArgs)
        {
            Opts.papszFiles = (const char * const *)&papszArgs[GetState.iNext];
            Opts.cFiles     = cArgs - GetState.iNext;
        }
    }

    if (!Opts.pszFile)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No input archive specified");

    RTFOFF cBytes = 0;
    uint32_t cFiles = 0;
    switch (Opts.iOperation)
    {
        case 'l':
        {
            RTPrintf("  Length      Date    Time    Name\n"
                     "---------  ---------- -----   ----\n");
            RTEXITCODE rcExit = rtZipUnzipDoWithMembers(&Opts, rtZipUnzipCmdListCallback, &cFiles, &cBytes);
            RTPrintf("---------                     -------\n"
                     "%9RU64                     %u file%s\n",
                     cBytes, cFiles, cFiles != 1 ? "s" : "");

            return rcExit;
        }

        default:
            return rtZipUnzipDoWithMembers(&Opts, rtZipUnzipCmdExtractCallback, &cFiles, &cBytes);
    }
}
