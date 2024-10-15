/* $Id: RTChMod.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Changes the mode/attributes of a file system object.
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
#include <iprt/buildconfig.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/vfs.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** What to clear we all bits are being set. */
#define RTCHMOD_SET_ALL_MASK  (~(  RTFS_TYPE_MASK  \
                                 | RTFS_DOS_NT_ENCRYPTED \
                                 | RTFS_DOS_NT_COMPRESSED \
                                 | RTFS_DOS_NT_REPARSE_POINT \
                                 | RTFS_DOS_NT_SPARSE_FILE \
                                 | RTFS_DOS_NT_DEVICE \
                                 | RTFS_DOS_DIRECTORY))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef enum RTCMDCHMODNOISE
{
    kRTCmdChModNoise_Quiet,
    kRTCmdChModNoise_Default,
    kRTCmdChModNoise_Changes,
    kRTCmdChModNoise_Verbose
} RTCMDCHMODNOISE;


typedef struct RTCMDCHMODOPTS
{
    /** The noise level. */
    RTCMDCHMODNOISE enmNoiseLevel;
    /** -R, --recursive   */
    bool            fRecursive;
    /** --preserve-root / --no-preserve-root (don't allow recursion from root). */
    bool            fPreserveRoot;
    /** Whether to always use the VFS chain API (for testing). */
    bool            fAlwaysUseChainApi;
    /** Which mode bits to set. */
    RTFMODE         fModeSet;
    /** Which mode bits to clear. */
    RTFMODE         fModeClear;
} RTCMDCHMODOPTS;



/**
 * Calculates the new file mode.
 *
 * @returns New mode mask.
 * @param   pOpts               The chmod options.
 * @param   fMode               The current file mode.
 */
static RTFMODE rtCmdMkModCalcNewMode(RTCMDCHMODOPTS const *pOpts, RTFMODE fMode)
{
    fMode &= ~pOpts->fModeClear;
    fMode |= pOpts->fModeSet;
    /** @todo do 'X'   */
    return fMode;
}


/**
 * Changes the file mode of one file system object.
 *
 * @returns exit code
 * @param   pOpts               The chmod options.
 * @param   pszPath             The path to the file system object to change the
 *                              file mode of.
 */
static RTEXITCODE rtCmdChModOne(RTCMDCHMODOPTS const *pOpts, const char *pszPath)
{
    int         rc;
    RTFSOBJINFO ObjInfo;
    bool        fChanges = false;
    if (!pOpts->fAlwaysUseChainApi && !RTVfsChainIsSpec(pszPath) )
    {
        rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
        if (RT_SUCCESS(rc))
        {
            RTFMODE fNewMode = rtCmdMkModCalcNewMode(pOpts, ObjInfo.Attr.fMode);
            fChanges = fNewMode != ObjInfo.Attr.fMode;
            if (fChanges)
            {
                rc = RTPathSetMode(pszPath, fNewMode);
                if (RT_FAILURE(rc))
                    RTMsgError("RTPathSetMode failed on '%s' with fNewMode=%#x: %Rrc", pszPath, fNewMode, rc);
            }
        }
        else
            RTMsgError("RTPathQueryInfoEx failed on '%s': %Rrc", pszPath, rc);
    }
    else
    {
        RTVFSOBJ        hVfsObj;
        uint32_t        offError;
        RTERRINFOSTATIC ErrInfo;
        rc = RTVfsChainOpenObj(pszPath, RTFILE_O_ACCESS_ATTR_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                               RTVFSOBJ_F_OPEN_ANY | RTVFSOBJ_F_CREATE_NOTHING | RTPATH_F_FOLLOW_LINK,
                               &hVfsObj, &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
        {
            rc = RTVfsObjQueryInfo(hVfsObj, &ObjInfo, RTFSOBJATTRADD_NOTHING);
            if (RT_SUCCESS(rc))
            {
                RTFMODE fNewMode = rtCmdMkModCalcNewMode(pOpts, ObjInfo.Attr.fMode);
                fChanges = fNewMode != ObjInfo.Attr.fMode;
                if (fChanges)
                {
                    rc = RTVfsObjSetMode(hVfsObj, fNewMode, RTCHMOD_SET_ALL_MASK);
                    if (RT_FAILURE(rc))
                        RTMsgError("RTVfsObjSetMode failed on '%s' with fNewMode=%#x: %Rrc", pszPath, fNewMode, rc);
                }
            }
            else
                RTVfsChainMsgError("RTVfsObjQueryInfo", pszPath, rc, offError, &ErrInfo.Core);
            RTVfsObjRelease(hVfsObj);
        }
        else
            RTVfsChainMsgError("RTVfsChainOpenObject", pszPath, rc, offError, &ErrInfo.Core);
    }

    if (RT_SUCCESS(rc))
    {
        if (pOpts->enmNoiseLevel >= (fChanges ? kRTCmdChModNoise_Changes : kRTCmdChModNoise_Verbose))
            RTPrintf("%s\n", pszPath);
        return RTEXITCODE_SUCCESS;
    }
    return RTEXITCODE_FAILURE;
}


/**
 * Recursively changes the file mode.
 *
 * @returns exit code
 * @param   pOpts               The mkdir option.
 * @param   pszPath             The path to start changing the mode of.
 */
static int rtCmdChModRecursive(RTCMDCHMODOPTS const *pOpts, const char *pszPath)
{
    /*
     * Check if it's a directory first.  If not, join the non-recursive code.
     */
    int             rc;
    uint32_t        offError;
    RTFSOBJINFO     ObjInfo;
    RTERRINFOSTATIC ErrInfo;
    bool const      fUseChainApi = pOpts->fAlwaysUseChainApi || RTVfsChainIsSpec(pszPath);
    if (!fUseChainApi)
    {
        rc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTPathQueryInfoEx failed on '%s': %Rrc", pszPath, rc);
    }
    else
    {
        rc = RTVfsChainQueryInfo(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK,
                                 &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
            return RTVfsChainMsgErrorExitFailure("RTVfsChainQueryInfo", pszPath, rc, offError, &ErrInfo.Core);
    }

    if (!RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
    {
        /*
         * Don't bother redoing the above work if its not necessary.
         */
        RTFMODE fNewMode = rtCmdMkModCalcNewMode(pOpts, ObjInfo.Attr.fMode);
        if (fNewMode != ObjInfo.Attr.fMode)
            return rtCmdChModOne(pOpts, pszPath);
        if (pOpts->enmNoiseLevel >= kRTCmdChModNoise_Verbose)
            RTPrintf("%s\n", pszPath);
        return RTEXITCODE_SUCCESS;
    }

    /*
     * For recursion we always use the VFS layer.
     */
    RTVFSDIR hVfsDir;
    if (!fUseChainApi)
    {
        rc = RTVfsDirOpenNormal(pszPath, 0 /** @todo write attrib flag*/, &hVfsDir);
        if (RT_FAILURE(rc))
            return RTMsgErrorExitFailure("RTVfsDirOpenNormal failed on '%s': %Rrc", pszPath, rc);
    }
    else
    {
        rc = RTVfsChainOpenDir(pszPath, 0 /** @todo write attrib flag*/, &hVfsDir, &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
            return RTVfsChainMsgErrorExitFailure("RTVfsChainQueryInfo", pszPath, rc, offError, &ErrInfo.Core);
    }

    RTMsgError("Recursion is not yet implemented\n");
    RTVfsDirRelease(hVfsDir);
    rc = VERR_NOT_IMPLEMENTED;

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE RTCmdChMod(unsigned cArgs,  char **papszArgs)
{
    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* operations */
        { "--recursive",                'R', RTGETOPT_REQ_NOTHING },
        { "--preserve-root",            'x', RTGETOPT_REQ_NOTHING },
        { "--no-preserve-root",         'X', RTGETOPT_REQ_NOTHING },
        { "--changes",                  'c', RTGETOPT_REQ_NOTHING },
        { "--quiet",                    'f', RTGETOPT_REQ_NOTHING },
        { "--silent",                   'f', RTGETOPT_REQ_NOTHING },
        { "--verbose",                  'v', RTGETOPT_REQ_NOTHING },
        { "--reference",                'Z', RTGETOPT_REQ_NOTHING },
        { "--always-use-vfs-chain-api", 'A', RTGETOPT_REQ_NOTHING },

    };

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOpt failed: %Rrc", rc);

    RTCMDCHMODOPTS Opts;
    Opts.enmNoiseLevel      = kRTCmdChModNoise_Default;
    Opts.fPreserveRoot      = false;
    Opts.fRecursive         = false;
    Opts.fAlwaysUseChainApi = false;
    Opts.fModeClear         = 0;
    Opts.fModeSet           = 0;

    RTGETOPTUNION ValueUnion;
    while (   (rc = RTGetOpt(&GetState, &ValueUnion)) != 0
           && rc != VINF_GETOPT_NOT_OPTION)
    {
        switch (rc)
        {
            case 'R':
                Opts.fRecursive = true;
                break;

            case 'x':
                Opts.fPreserveRoot = true;
                break;
            case 'X':
                Opts.fPreserveRoot = false;
                break;

            case 'f':
                Opts.enmNoiseLevel = kRTCmdChModNoise_Quiet;
                break;
            case 'c':
                Opts.enmNoiseLevel = kRTCmdChModNoise_Changes;
                break;
            case 'v':
                Opts.enmNoiseLevel = kRTCmdChModNoise_Verbose;
                break;

            case 'Z':
            {
                RTFSOBJINFO     ObjInfo;
                RTERRINFOSTATIC ErrInfo;
                uint32_t        offError;
                rc = RTVfsChainQueryInfo(ValueUnion.psz, &ObjInfo,RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK,
                                         &offError, RTErrInfoInitStatic(&ErrInfo));
                if (RT_FAILURE(rc))
                    return RTVfsChainMsgErrorExitFailure("RTVfsChainQueryInfo", ValueUnion.psz, rc, offError, &ErrInfo.Core);
                Opts.fModeClear = RTCHMOD_SET_ALL_MASK;
                Opts.fModeSet   = ObjInfo.Attr.fMode & RTCHMOD_SET_ALL_MASK;
                break;
            }

            case 'A':
                Opts.fAlwaysUseChainApi = true;
                break;

            case 'h':
                RTPrintf("Usage: %s [options] <mode> <file> [..]\n"
                         "\n"
                         "Options:\n"
                         "  -f, --silent, --quiet\n"
                         "  -c, --changes\n"
                         "  -v, --verbose\n"
                         "      Noise level selection.\n"
                         "  -R, --recursive\n"
                         "      Recurse into directories.\n"
                         "  --preserve-root, --no-preserve-root\n"
                         "      Whether to allow recursion from the root (default: yes).\n"
                         "  --reference <file>\n"
                         "      Take mode mask to use from <file> instead of <mode>.\n"
                         "\n"
                         "The <mode> part isn't fully implemented, so only numerical octal notation\n"
                         "works.  Prefix the number(s) with 0x to use hexadecimal.  There are two forms\n"
                         "of the numerical notation: <SET> and <SET>:<CLEAR>\n"
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
     * The MODE.
     */
    if (   Opts.fModeClear == 0
        && Opts.fModeSet   == 0)
    {
        if (rc != VINF_GETOPT_NOT_OPTION)
            return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No mode change specified.\n");

        char *pszNext;
        if (   ValueUnion.psz[0] == '0'
            && (ValueUnion.psz[1] == 'x' || ValueUnion.psz[1] == 'X'))
            rc = RTStrToUInt32Ex(ValueUnion.psz, &pszNext, 16, &Opts.fModeSet);
        else
            rc = RTStrToUInt32Ex(ValueUnion.psz, &pszNext, 8, &Opts.fModeSet);
        if (   rc != VINF_SUCCESS
            && (rc != VWRN_TRAILING_CHARS || *pszNext != ':'))
            return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unable to parse mode mask: %s\n", ValueUnion.psz);
        Opts.fModeSet &= RTCHMOD_SET_ALL_MASK;

        if (rc == VINF_SUCCESS)
            Opts.fModeClear = RTCHMOD_SET_ALL_MASK;
        else
        {
            pszNext++;
            if (   pszNext[0] == '0'
                && (pszNext[1] == 'x' || pszNext[1] == 'X'))
                rc = RTStrToUInt32Ex(pszNext, &pszNext, 16, &Opts.fModeClear);
            else
                rc = RTStrToUInt32Ex(pszNext, &pszNext, 8, &Opts.fModeClear);
            if (rc != VINF_SUCCESS)
                return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unable to parse mode mask: %s\n", ValueUnion.psz);
            Opts.fModeClear &= RTCHMOD_SET_ALL_MASK;
        }

        rc = RTGetOpt(&GetState, &ValueUnion);
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
        if (Opts.fRecursive)
            rc = rtCmdChModRecursive(&Opts, ValueUnion.psz);
        else
            rc = rtCmdChModOne(&Opts, ValueUnion.psz);
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
    return RTCmdChMod(argc, argv);
}

