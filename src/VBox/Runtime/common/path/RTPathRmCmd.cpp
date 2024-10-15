/* $Id: RTPathRmCmd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RM Command.
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

#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/dir.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/symlink.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define RTPATHRMCMD_OPT_INTERACTIVE         1000
#define RTPATHRMCMD_OPT_ONE_FILE_SYSTEM     1001
#define RTPATHRMCMD_OPT_PRESERVE_ROOT       1002
#define RTPATHRMCMD_OPT_NO_PRESERVE_ROOT    1003
#define RTPATHRMCMD_OPT_MACHINE_READABLE    1004

/** The max directory entry size. */
#define RTPATHRM_DIR_MAX_ENTRY_SIZE         (sizeof(RTDIRENTRYEX) + 4096)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Interactive option. */
typedef enum
{
    RTPATHRMCMDINTERACTIVE_NONE = 1,
    RTPATHRMCMDINTERACTIVE_ALL,
    RTPATHRMCMDINTERACTIVE_ONCE
    /** @todo possible that we should by default prompt if removing read-only
     *        files or files owned by someone else. We currently don't. */
} RTPATHRMCMDINTERACTIVE;

/**
 * IPRT rm option structure.
 */
typedef struct RTPATHRMCMDOPTS
{
    /** Whether to delete recursively. */
    bool                    fRecursive;
    /** Whether to delete directories as well as other kinds of files. */
    bool                    fDirsAndOther;
    /** Whether to remove files without prompting and ignoring non-existing
     * files.  */
    bool                    fForce;
    /** Machine readable output. */
    bool                    fMachineReadable;
    /** Don't try remove root ('/') if set, otherwise don't treat root specially. */
    bool                    fPreserveRoot;
    /** Whether to keep to one file system. */
    bool                    fOneFileSystem;
    /** Whether to safely delete files (overwrite 3x before unlinking). */
    bool                    fSafeDelete;
    /** Whether to be verbose about the operation. */
    bool                    fVerbose;
    /** The interactive setting. */
    RTPATHRMCMDINTERACTIVE  enmInteractive;
} RTPATHRMCMDOPTS;
/** Pointer to the IPRT rm options. */
typedef RTPATHRMCMDOPTS *PRTPATHRMCMDOPTS;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** A bunch of zeros. */
static uint8_t const    g_abZeros[16384] = { 0 };
/** A bunch of 0xFF bytes. (lazy init) */
static uint8_t          g_ab0xFF[16384];


static void rtPathRmVerbose(PRTPATHRMCMDOPTS pOpts, const char *pszPath)
{
    if (!pOpts->fMachineReadable)
        RTPrintf("%s\n", pszPath);
}


static int rtPathRmError(PRTPATHRMCMDOPTS pOpts, const char *pszPath, int rc,
                         const char *pszFormat, ...)
{
    if (pOpts->fMachineReadable)
        RTPrintf("fname=%s%crc=%d%c", pszPath, 0, rc, 0);
    else
    {
        va_list va;
        va_start(va, pszFormat);
        RTMsgErrorV(pszFormat, va);
        va_end(va);
    }
    return rc;
}


/**
 * Worker that removes a symbolic link.
 *
 * @returns IPRT status code, errors go via rtPathRmError.
 * @param   pOpts               The RM options.
 * @param   pszPath             The path to the symbolic link.
 */
static int rtPathRmOneSymlink(PRTPATHRMCMDOPTS pOpts, const char *pszPath)
{
    if (pOpts->fVerbose)
        rtPathRmVerbose(pOpts, pszPath);
    int rc = RTSymlinkDelete(pszPath, 0);
    if (RT_FAILURE(rc))
        return rtPathRmError(pOpts, pszPath, rc, "Error removing symbolic link '%s': %Rrc\n", pszPath, rc);
    return rc;
}


/**
 * Worker that removes a file.
 *
 * Currently used to delete both regular and special files.
 *
 * @returns IPRT status code, errors go via rtPathRmError.
 * @param   pOpts               The RM options.
 * @param   pszPath             The path to the file.
 * @param   pObjInfo            The FS object info for the file.
 */
static int rtPathRmOneFile(PRTPATHRMCMDOPTS pOpts, const char *pszPath, PRTFSOBJINFO pObjInfo)
{
    int rc;
    if (pOpts->fVerbose)
        rtPathRmVerbose(pOpts, pszPath);

    /*
     * Wipe the file if requested and possible.
     */
    if (pOpts->fSafeDelete && RTFS_IS_FILE(pObjInfo->Attr.fMode))
    {
        /* Lazy init of the 0xff buffer. */
        if (g_ab0xFF[0] != 0xff || g_ab0xFF[sizeof(g_ab0xFF) - 1] != 0xff)
            memset(g_ab0xFF, 0xff, sizeof(g_ab0xFF));

        RTFILE hFile;
        rc = RTFileOpen(&hFile, pszPath, RTFILE_O_WRITE);
        if (RT_FAILURE(rc))
            return rtPathRmError(pOpts, pszPath, rc, "Opening '%s' for overwriting: %Rrc\n", pszPath, rc);

        for (unsigned iPass = 0; iPass < 3; iPass++)
        {
            uint8_t const *pabFiller = iPass == 1 ? g_abZeros         : g_ab0xFF;
            size_t const   cbFiller  = iPass == 1 ? sizeof(g_abZeros) : sizeof(g_ab0xFF);

            rc = RTFileSeek(hFile, 0, RTFILE_SEEK_BEGIN, NULL);
            if (RT_FAILURE(rc))
            {
                rc = rtPathRmError(pOpts, pszPath, rc, "Error seeking to start of '%s': %Rrc\n", pszPath, rc);
                break;
            }
            for (RTFOFF cbLeft = pObjInfo->cbObject; cbLeft > 0; cbLeft -= cbFiller)
            {
                size_t cbToWrite = cbFiller;
                if (cbLeft < (RTFOFF)cbToWrite)
                    cbToWrite = (size_t)cbLeft;
                rc = RTFileWrite(hFile, pabFiller, cbToWrite, NULL);
                if (RT_FAILURE(rc))
                {
                    rc = rtPathRmError(pOpts, pszPath, rc, "Error writing to '%s': %Rrc\n", pszPath, rc);
                    break;
                }
            }
        }

        int rc2 = RTFileClose(hFile);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            return rtPathRmError(pOpts, pszPath, rc2, "Closing '%s' failed: %Rrc\n", pszPath, rc);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Remove the file.
     */
    rc = RTFileDelete(pszPath);
    if (RT_FAILURE(rc))
        return rtPathRmError(pOpts, pszPath, rc,
                             RTFS_IS_FILE(pObjInfo->Attr.fMode)
                             ? "Error removing regular file '%s': %Rrc\n"
                             : "Error removing special file '%s': %Rrc\n",
                             pszPath, rc);
    return rc;
}


/**
 * Deletes one directory (if it's empty).
 *
 * @returns IPRT status code, errors go via rtPathRmError.
 * @param   pOpts               The RM options.
 * @param   pszPath             The path to the directory.
 */
static int rtPathRmOneDir(PRTPATHRMCMDOPTS pOpts, const char *pszPath)
{
    if (pOpts->fVerbose)
        rtPathRmVerbose(pOpts, pszPath);

    int rc = RTDirRemove(pszPath);
    if (RT_FAILURE(rc))
        return rtPathRmError(pOpts, pszPath, rc, "Error removing directory '%s': %Rrc", pszPath, rc);
    return rc;
}


/**
 * Recursively delete a directory.
 *
 * @returns IPRT status code, errors go via rtPathRmError.
 * @param   pOpts               The RM options.
 * @param   pszPath             Pointer to a writable buffer holding the path to
 *                              the directory.
 * @param   cchPath             The length of the path (avoid strlen).
 * @param   pDirEntry           Pointer to a directory entry buffer that is
 *                              RTPATHRM_DIR_MAX_ENTRY_SIZE bytes big.
 */
static int rtPathRmRecursive(PRTPATHRMCMDOPTS pOpts, char *pszPath, size_t cchPath, PRTDIRENTRYEX pDirEntry)
{
    /*
     * Make sure the path ends with a slash.
     */
    if (!cchPath || !RTPATH_IS_SLASH(pszPath[cchPath - 1]))
    {
        if (cchPath + 1 >= RTPATH_MAX)
            return rtPathRmError(pOpts, pszPath, VERR_BUFFER_OVERFLOW, "Buffer overflow fixing up '%s'.\n", pszPath);
        pszPath[cchPath++] = RTPATH_SLASH;
        pszPath[cchPath]   = '\0';
    }

    /*
     * Traverse the directory.
     */
    RTDIR hDir;
    int rc = RTDirOpen(&hDir, pszPath);
    if (RT_FAILURE(rc))
        return rtPathRmError(pOpts, pszPath, rc, "Error opening directory '%s': %Rrc", pszPath, rc);
    int rcRet = VINF_SUCCESS;
    for (;;)
    {
        /*
         * Read the next entry, constructing an full path for it.
         */
        size_t cbEntry = RTPATHRM_DIR_MAX_ENTRY_SIZE;
        rc = RTDirReadEx(hDir, pDirEntry, &cbEntry, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
        if (rc == VERR_NO_MORE_FILES)
        {
            /*
             * Reached the end of the directory.
             */
            pszPath[cchPath] = '\0';
            rc = RTDirClose(hDir);
            if (RT_FAILURE(rc))
                return rtPathRmError(pOpts, pszPath, rc, "Error closing directory '%s': %Rrc", pszPath, rc);

            /* Delete the directory. */
            int rc2 = rtPathRmOneDir(pOpts, pszPath);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rcRet))
                return rc2;
            return rcRet;
        }

        if (RT_FAILURE(rc))
        {
            rc = rtPathRmError(pOpts, pszPath, rc, "Error reading directory '%s': %Rrc", pszPath, rc);
            break;
        }

        /* Skip '.' and '..'. */
        if (   pDirEntry->szName[0] == '.'
            && (   pDirEntry->cbName == 1
                || (   pDirEntry->cbName == 2
                    && pDirEntry->szName[1] == '.')))
            continue;

        /* Construct full path. */
        if (cchPath + pDirEntry->cbName >= RTPATH_MAX)
        {
            pszPath[cchPath] = '\0';
            rc = rtPathRmError(pOpts, pszPath, VERR_BUFFER_OVERFLOW, "Path buffer overflow in directory '%s'.", pszPath);
            break;
        }
        memcpy(pszPath + cchPath, pDirEntry->szName, pDirEntry->cbName + 1);

        /*
         * Take action according to the type.
         */
        switch (pDirEntry->Info.Attr.fMode & RTFS_TYPE_MASK)
        {
            case RTFS_TYPE_FILE:
                rc = rtPathRmOneFile(pOpts, pszPath, &pDirEntry->Info);
                break;

            case RTFS_TYPE_DIRECTORY:
                rc = rtPathRmRecursive(pOpts, pszPath, cchPath + pDirEntry->cbName, pDirEntry);
                break;

            case RTFS_TYPE_SYMLINK:
                rc = rtPathRmOneSymlink(pOpts, pszPath);
                break;

            case RTFS_TYPE_FIFO:
            case RTFS_TYPE_DEV_CHAR:
            case RTFS_TYPE_DEV_BLOCK:
            case RTFS_TYPE_SOCKET:
                rc = rtPathRmOneFile(pOpts, pszPath, &pDirEntry->Info);
                break;

            case RTFS_TYPE_WHITEOUT:
            default:
                rc = rtPathRmError(pOpts, pszPath, VERR_UNEXPECTED_FS_OBJ_TYPE,
                                   "Object '%s' has an unknown file type: %o\n",
                                   pszPath, pDirEntry->Info.Attr.fMode & RTFS_TYPE_MASK);
                break;
        }
        if (RT_FAILURE(rc) && RT_SUCCESS(rcRet))
            rcRet = rc;
    }

    /*
     * Some error occured, close and return.
     */
    RTDirClose(hDir);
    return rc;
}

/**
 * Validates the specified file or directory.
 *
 * @returns IPRT status code, errors go via rtPathRmError.
 * @param   pOpts               The RM options.
 * @param   pszPath             The path to the file, directory, whatever.
 */
static int rtPathRmOneValidate(PRTPATHRMCMDOPTS pOpts, const char *pszPath)
{
    /*
     * RTPathFilename doesn't do the trailing slash thing the way we need it to.
     * E.g. both '..' and '../' should be rejected.
     */
    size_t cchPath = strlen(pszPath);
    while (cchPath > 0 && RTPATH_IS_SLASH(pszPath[cchPath - 1]))
        cchPath--;

    if (   (  cchPath == 0
            || 0 /** @todo drive letter + UNC crap */)
        && pOpts->fPreserveRoot)
        return rtPathRmError(pOpts, pszPath, VERR_CANT_DELETE_DIRECTORY, "Cannot remove root directory ('%s').\n", pszPath);

    size_t offLast = cchPath - 1;
    while (offLast > 0 && !RTPATH_IS_SEP(pszPath[offLast - 1]))
        offLast--;

    size_t cchLast = cchPath - offLast;
    if (   pszPath[offLast] == '.'
        && (   cchLast == 1
            || (cchLast == 2 && pszPath[offLast + 1] == '.')))
        return rtPathRmError(pOpts, pszPath, VERR_CANT_DELETE_DIRECTORY, "Cannot remove special directory '%s'.\n", pszPath);

    return VINF_SUCCESS;
}


/**
 * Remove one user specified file or directory.
 *
 * @returns IPRT status code, errors go via rtPathRmError.
 * @param   pOpts               The RM options.
 * @param   pszPath             The path to the file, directory, whatever.
 */
static int rtPathRmOne(PRTPATHRMCMDOPTS pOpts, const char *pszPath)
{
    /*
     * RM refuses to delete some directories.
     */
    int rc = rtPathRmOneValidate(pOpts, pszPath);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Query file system object info.
     */
    RTFSOBJINFO ObjInfo;
    rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
    if (RT_FAILURE(rc))
    {
        if (pOpts->fForce && (rc == VERR_FILE_NOT_FOUND || rc == VERR_PATH_NOT_FOUND))
            return VINF_SUCCESS;
        return rtPathRmError(pOpts, pszPath, rc, "Error deleting '%s': %Rrc", pszPath, rc);
    }

    /*
     * Take type specific action.
     */
    switch (ObjInfo.Attr.fMode & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_FILE:
            return rtPathRmOneFile(pOpts, pszPath, &ObjInfo);

        case RTFS_TYPE_DIRECTORY:
            if (pOpts->fRecursive)
            {
                char szPath[RTPATH_MAX];
                rc = RTPathAbs(pszPath, szPath, sizeof(szPath));
                if (RT_FAILURE(rc))
                    return rtPathRmError(pOpts, pszPath, rc, "RTPathAbs failed on '%s': %Rrc\n", pszPath, rc);

                union
                {
                    RTDIRENTRYEX    Core;
                    uint8_t         abPadding[RTPATHRM_DIR_MAX_ENTRY_SIZE];
                } DirEntry;

                return rtPathRmRecursive(pOpts, szPath, strlen(szPath), &DirEntry.Core);
            }
            if (pOpts->fDirsAndOther)
                return rtPathRmOneDir(pOpts, pszPath);
            return rtPathRmError(pOpts, pszPath, VERR_IS_A_DIRECTORY, "Cannot remove '%s': %Rrc\n", pszPath, VERR_IS_A_DIRECTORY);

        case RTFS_TYPE_SYMLINK:
            return rtPathRmOneSymlink(pOpts, pszPath);

        case RTFS_TYPE_FIFO:
        case RTFS_TYPE_DEV_CHAR:
        case RTFS_TYPE_DEV_BLOCK:
        case RTFS_TYPE_SOCKET:
            return rtPathRmOneFile(pOpts, pszPath, &ObjInfo);

        case RTFS_TYPE_WHITEOUT:
        default:
            return rtPathRmError(pOpts, pszPath, VERR_UNEXPECTED_FS_OBJ_TYPE,
                                 "Object '%s' has an unknown file type: %o\n", pszPath, ObjInfo.Attr.fMode & RTFS_TYPE_MASK);

    }
}


RTDECL(RTEXITCODE) RTPathRmCmd(unsigned cArgs, char **papszArgs)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* operations */
        { "--dirs-and-more",        'd', RTGETOPT_REQ_NOTHING },
        { "--force",                'f', RTGETOPT_REQ_NOTHING },
        { "--prompt",               'i', RTGETOPT_REQ_NOTHING },
        { "--prompt-once",          'I', RTGETOPT_REQ_NOTHING },
        { "--interactive",      RTPATHRMCMD_OPT_INTERACTIVE, RTGETOPT_REQ_STRING },
        { "--one-file-system",  RTPATHRMCMD_OPT_ONE_FILE_SYSTEM, RTGETOPT_REQ_NOTHING },
        { "--preserve-root",    RTPATHRMCMD_OPT_PRESERVE_ROOT, RTGETOPT_REQ_NOTHING },
        { "--no-preserve-root", RTPATHRMCMD_OPT_NO_PRESERVE_ROOT, RTGETOPT_REQ_NOTHING },
        { "--recursive",            'R', RTGETOPT_REQ_NOTHING },
        { "--recursive",            'r', RTGETOPT_REQ_NOTHING },
        { "--safe-delete",          'P', RTGETOPT_REQ_NOTHING },
        { "--verbose",              'v', RTGETOPT_REQ_NOTHING },

        /* IPRT extensions */
        { "--machine-readable", RTPATHRMCMD_OPT_MACHINE_READABLE, RTGETOPT_REQ_NOTHING },
        { "--machinereadable",  RTPATHRMCMD_OPT_MACHINE_READABLE, RTGETOPT_REQ_NOTHING }, /* bad long option style */
    };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOpt failed: %Rrc", rc);

    RTPATHRMCMDOPTS Opts;
    RT_ZERO(Opts);
    Opts.fPreserveRoot  = true;
    Opts.enmInteractive = RTPATHRMCMDINTERACTIVE_NONE;

    RTGETOPTUNION   ValueUnion;
    while (   (rc = RTGetOpt(&GetState, &ValueUnion)) != 0
           && rc != VINF_GETOPT_NOT_OPTION)
    {
        switch (rc)
        {
            case 'd':
                Opts.fDirsAndOther = true;
                break;

            case 'f':
                Opts.fForce = true;
                Opts.enmInteractive = RTPATHRMCMDINTERACTIVE_NONE;
                break;

            case 'i':
                Opts.enmInteractive = RTPATHRMCMDINTERACTIVE_ALL;
                break;

            case 'I':
                Opts.enmInteractive = RTPATHRMCMDINTERACTIVE_ONCE;
                break;

            case RTPATHRMCMD_OPT_INTERACTIVE:
                if (!strcmp(ValueUnion.psz, "always"))
                    Opts.enmInteractive = RTPATHRMCMDINTERACTIVE_ALL;
                else if (!strcmp(ValueUnion.psz, "once"))
                    Opts.enmInteractive = RTPATHRMCMDINTERACTIVE_ONCE;
                else
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown --interactive option value: '%s'\n", ValueUnion.psz);
                break;

            case RTPATHRMCMD_OPT_ONE_FILE_SYSTEM:
                Opts.fOneFileSystem = true;
                break;

            case RTPATHRMCMD_OPT_PRESERVE_ROOT:
                Opts.fPreserveRoot = true;
                break;

            case RTPATHRMCMD_OPT_NO_PRESERVE_ROOT:
                Opts.fPreserveRoot = false;
                break;

            case 'R':
            case 'r':
                Opts.fRecursive = true;
                Opts.fDirsAndOther = true;
                break;

            case 'P':
                Opts.fSafeDelete = true;
                break;

            case 'v':
                Opts.fVerbose = true;
                break;

            case RTPATHRMCMD_OPT_MACHINE_READABLE:
                Opts.fMachineReadable = true;
                break;

            case 'h':
                RTPrintf("Usage: to be written\nOption dump:\n");
                for (unsigned i = 0; i < RT_ELEMENTS(s_aOptions); i++)
                    if (RT_C_IS_PRINT(s_aOptions[i].iShort))
                        RTPrintf(" -%c,%s\n", s_aOptions[i].iShort, s_aOptions[i].pszLong);
                    else
                        RTPrintf(" %s\n", s_aOptions[i].pszLong);
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Options we don't support.
     */
    if (Opts.fOneFileSystem)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The --one-file-system option is not yet implemented.\n");
    if (Opts.enmInteractive != RTPATHRMCMDINTERACTIVE_NONE)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "The -i, -I and --interactive options are not implemented yet.\n");

    /*
     * No files means error.
     */
    if (rc != VINF_GETOPT_NOT_OPTION && !Opts.fForce)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No files or directories specified.\n");

    /*
     * Machine readable init + header.
     */
    if (Opts.fMachineReadable)
    {
        int rc2 = RTStrmSetMode(g_pStdOut, true /*fBinary*/, false /*fCurrentCodeSet*/);
        if (RT_FAILURE(rc2))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTStrmSetMode failed: %Rrc.\n", rc2);
        static const char s_achHeader[] = "hdr_id=rm\0hdr_ver=1";
        RTStrmWrite(g_pStdOut, s_achHeader, sizeof(s_achHeader));
    }

    /*
     * Delete the specified files/dirs/whatever.
     */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    while (rc == VINF_GETOPT_NOT_OPTION)
    {
        rc = rtPathRmOne(&Opts, ValueUnion.psz);
        if (RT_FAILURE(rc))
            rcExit = RTEXITCODE_FAILURE;

        /* next */
        rc = RTGetOpt(&GetState, &ValueUnion);
    }
    if (rc != 0)
        rcExit = RTGetOptPrintError(rc, &ValueUnion);

    /*
     * Terminate the machine readable stuff.
     */
    if (Opts.fMachineReadable)
    {
        RTStrmWrite(g_pStdOut, "\0\0\0", 4);
        rc = RTStrmFlush(g_pStdOut);
        if (RT_FAILURE(rc) && rcExit == RTEXITCODE_SUCCESS)
            rcExit = RTEXITCODE_FAILURE;
    }

    return rcExit;
}

