/* $Id: VBoxExtPackHelperApp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Extension Pack Helper Application, usually set-uid-to-root.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "../include/ExtPackUtil.h"

#include <iprt/buildconfig.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/fs.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/manifest.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/thread.h>
#include <iprt/utf16.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>
#include <iprt/cpp/ministring.h>

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/sup.h>
#include <VBox/version.h>

#ifdef RT_OS_WINDOWS
# define _WIN32_WINNT 0x0501
# include <iprt/win/windows.h>          /* ShellExecuteEx, ++ */
# include <iprt/win/objbase.h>                   /* CoInitializeEx */
# ifdef DEBUG
#  include <Sddl.h>
# endif
#endif

#ifdef RT_OS_DARWIN
# include <Security/Authorization.h>
# include <Security/AuthorizationTags.h>
# include <CoreFoundation/CoreFoundation.h>
#endif

#if !defined(RT_OS_OS2)
# include <stdio.h>
# include <errno.h>
# if !defined(RT_OS_WINDOWS)
#  include <sys/types.h>
#  include <unistd.h>                   /* geteuid */
# endif
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Enable elevation on Windows and Darwin. */
#if !defined(RT_OS_OS2) || defined(DOXYGEN_RUNNING)
# define WITH_ELEVATION
#endif


/** @name Command and option names
 * @{ */
#define CMD_INSTALL         1000
#define CMD_UNINSTALL       1001
#define CMD_CLEANUP         1002
#ifdef WITH_ELEVATION
# define OPT_ELEVATED       1090
# define OPT_STDOUT         1091
# define OPT_STDERR         1092
#endif
#define OPT_DISP_INFO_HACK  1093
/** @}  */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef RT_OS_WINDOWS
static HINSTANCE g_hInstance;
#endif

#ifdef IN_RT_R3
/* Override RTAssertShouldPanic to prevent gdb process creation. */
RTDECL(bool) RTAssertShouldPanic(void)
{
    return true;
}
#endif



/**
 * Handle the special standard options when these are specified after the
 * command.
 *
 * @param   ch          The option character.
 */
static RTEXITCODE DoStandardOption(int ch)
{
    switch (ch)
    {
        case 'h':
        {
            RTMsgInfo(VBOX_PRODUCT " Extension Pack Helper App\n"
                      "Copyright (C) " VBOX_C_YEAR " " VBOX_VENDOR "\n"
                      "\n"
                      "This NOT intended for general use, please use VBoxManage instead\n"
                      "or call the IExtPackManager API directly.\n"
                      "\n"
                      "Usage: %s <command> [options]\n"
                      "Commands:\n"
                      "    install --base-dir <dir> --cert-dir <dir> --name <name> \\\n"
                      "        --tarball <tarball> --tarball-fd <fd>\n"
                      "    uninstall --base-dir <dir> --name <name>\n"
                      "    cleanup --base-dir <dir>\n"
                      , RTProcShortName());
            return RTEXITCODE_SUCCESS;
        }

        case 'V':
            RTPrintf("%sr%d\n", VBOX_VERSION_STRING, RTBldCfgRevision());
            return RTEXITCODE_SUCCESS;

        default:
            AssertFailedReturn(RTEXITCODE_FAILURE);
    }
}


/**
 * Checks if the cerficiate directory is valid.
 *
 * @returns true if it is valid, false if it isn't.
 * @param   pszCertDir          The certificate directory to validate.
 */
static bool IsValidCertificateDir(const char *pszCertDir)
{
    /*
     * Just be darn strict for now.
     */
    char szCorrect[RTPATH_MAX];
    int vrc = RTPathAppPrivateNoArch(szCorrect, sizeof(szCorrect));
    if (RT_FAILURE(vrc))
        return false;
    vrc = RTPathAppend(szCorrect, sizeof(szCorrect), VBOX_EXTPACK_CERT_DIR);
    if (RT_FAILURE(vrc))
        return false;

    return RTPathCompare(szCorrect, pszCertDir) == 0;
}


/**
 * Checks if the base directory is valid.
 *
 * @returns true if it is valid, false if it isn't.
 * @param   pszBaesDir          The base directory to validate.
 */
static bool IsValidBaseDir(const char *pszBaseDir)
{
    /*
     * Just be darn strict for now.
     */
    char szCorrect[RTPATH_MAX];
    int vrc = RTPathAppPrivateArchTop(szCorrect, sizeof(szCorrect));
    if (RT_FAILURE(vrc))
        return false;
    vrc = RTPathAppend(szCorrect, sizeof(szCorrect), VBOX_EXTPACK_INSTALL_DIR);
    if (RT_FAILURE(vrc))
        return false;

    return RTPathCompare(szCorrect, pszBaseDir) == 0;
}


/**
 * Cleans up a temporary extension pack directory.
 *
 * This is used by 'uninstall', 'cleanup' and in the failure path of 'install'.
 *
 * @returns The program exit code.
 * @param   pszDir          The directory to clean up.  The caller is
 *                          responsible for making sure this is valid.
 * @param   fTemporary      Whether this is a temporary install directory or
 *                          not.
 */
static RTEXITCODE RemoveExtPackDir(const char *pszDir, bool fTemporary)
{
    /** @todo May have to undo 555 modes here later.  */
    int vrc = RTDirRemoveRecursive(pszDir, RTDIRRMREC_F_CONTENT_AND_DIR);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              "Failed to delete the %sextension pack directory: %Rrc ('%s')",
                              fTemporary ? "temporary " : "", vrc, pszDir);
    return RTEXITCODE_SUCCESS;
}


/**
 * Wrapper around RTDirRename that may retry the operation for up to 15 seconds
 * on windows to deal with AV software.
 */
static int CommonDirRenameWrapper(const char *pszSrc, const char *pszDst, uint32_t fFlags)
{
#ifdef RT_OS_WINDOWS
    uint64_t nsNow = RTTimeNanoTS();
    for (;;)
    {
        int vrc = RTDirRename(pszSrc, pszDst, fFlags);
        if (   (   vrc != VERR_ACCESS_DENIED
                && vrc != VERR_SHARING_VIOLATION)
            || RTTimeNanoTS() - nsNow > RT_NS_15SEC)
            return vrc;
        RTThreadSleep(128);
    }
#else
    return RTDirRename(pszSrc, pszDst, fFlags);
#endif
}

/**
 * Common uninstall worker used by both uninstall and install --replace.
 *
 * @returns success or failure, message displayed on failure.
 * @param   pszExtPackDir   The extension pack directory name.
 */
static RTEXITCODE CommonUninstallWorker(const char *pszExtPackDir)
{
    /* Rename the extension pack directory before deleting it to prevent new
       VM processes from picking it up. */
    char szExtPackUnInstDir[RTPATH_MAX];
    int vrc = RTStrCopy(szExtPackUnInstDir, sizeof(szExtPackUnInstDir), pszExtPackDir);
    if (RT_SUCCESS(vrc))
        vrc = RTStrCat(szExtPackUnInstDir, sizeof(szExtPackUnInstDir), "-_-uninst");
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to construct temporary extension pack path: %Rrc", vrc);

    vrc = CommonDirRenameWrapper(pszExtPackDir, szExtPackUnInstDir, RTPATHRENAME_FLAGS_NO_REPLACE);
    if (vrc == VERR_ALREADY_EXISTS)
    {
        /* Automatic cleanup and try again.  It's in theory possible that we're
           racing another cleanup operation here, so just ignore errors and try
           again. (There is no installation race due to the exclusive temporary
           installation directory.) */
        RemoveExtPackDir(szExtPackUnInstDir, false /*fTemporary*/);
        vrc = CommonDirRenameWrapper(pszExtPackDir, szExtPackUnInstDir, RTPATHRENAME_FLAGS_NO_REPLACE);
    }
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              "Failed to rename the extension pack directory: %Rrc\n"
                              "If the problem persists, try running the command: VBoxManage extpack cleanup", vrc);

    /* Recursively delete the directory content. */
    return RemoveExtPackDir(szExtPackUnInstDir, false /*fTemporary*/);
}


/**
 * Wrapper around VBoxExtPackOpenTarFss.
 *
 * @returns success or failure, message displayed on failure.
 * @param   hTarballFile    The handle to the tarball file.
 * @param   phTarFss        Where to return the filesystem stream handle.
 */
static RTEXITCODE OpenTarFss(RTFILE hTarballFile, PRTVFSFSSTREAM phTarFss)
{
    char szError[8192];
    int vrc = VBoxExtPackOpenTarFss(hTarballFile, szError, sizeof(szError), phTarFss, NULL);
    if (RT_FAILURE(vrc))
    {
        Assert(szError[0]);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s", szError);
    }
    Assert(!szError[0]);
    return RTEXITCODE_SUCCESS;
}


/**
 * Sets the permissions of the temporary extension pack directory just before
 * renaming it.
 *
 * By default the temporary directory is only accessible by root, this function
 * will make it world readable and browseable.
 *
 * @returns The program exit code.
 * @param   pszDir              The temporary extension pack directory.
 */
static RTEXITCODE SetExtPackPermissions(const char *pszDir)
{
    RTMsgInfo("Setting permissions...");
#if !defined(RT_OS_WINDOWS)
     int vrc = RTPathSetMode(pszDir, 0755);
     if (RT_FAILURE(vrc))
         return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to set directory permissions: %Rrc ('%s')", vrc, pszDir);
#else
     /** @todo TrustedInstaller? */
     RT_NOREF1(pszDir);
#endif

    return RTEXITCODE_SUCCESS;
}


/**
 * Wrapper around VBoxExtPackValidateMember.
 *
 * @returns Program exit code, failure with message.
 * @param   pszName             The name of the directory.
 * @param   enmType             The object type.
 * @param   hVfsObj             The VFS object.
 */
static RTEXITCODE ValidateMemberOfExtPack(const char *pszName, RTVFSOBJTYPE enmType, RTVFSOBJ hVfsObj)
{
    char szError[8192];
    int vrc = VBoxExtPackValidateMember(pszName, enmType, hVfsObj, szError, sizeof(szError));
    if (RT_FAILURE(vrc))
    {
        Assert(szError[0]);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s", szError);
    }
    Assert(!szError[0]);
    return RTEXITCODE_SUCCESS;
}


/**
 * Validates the extension pack tarball prior to unpacking.
 *
 * Operations performed:
 *      - Hardening checks.
 *
 * @returns The program exit code.
 * @param   pszDir              The directory where the extension pack has been
 *                              unpacked.
 * @param   pszExtPackName      The expected extension pack name.
 * @param   pszTarball          The name of the tarball in case we have to
 *                              complain about something.
 */
static RTEXITCODE ValidateUnpackedExtPack(const char *pszDir, const char *pszTarball, const char *pszExtPackName)
{
    RT_NOREF2(pszTarball, pszExtPackName);
    RTMsgInfo("Validating unpacked extension pack...");

    RTERRINFOSTATIC ErrInfo;
    RTErrInfoInitStatic(&ErrInfo);
    int vrc = SUPR3HardenedVerifyDir(pszDir, true /*fRecursive*/, true /*fCheckFiles*/, &ErrInfo.Core);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Hardening check failed with %Rrc: %s", vrc, ErrInfo.Core.pszMsg);
    return RTEXITCODE_SUCCESS;
}


/**
 * Unpacks a directory from an extension pack tarball.
 *
 * @returns Program exit code, failure with message.
 * @param   pszDstDirName   The name of the unpacked directory.
 * @param   hVfsObj         The source object for the directory.
 */
static RTEXITCODE UnpackExtPackDir(const char *pszDstDirName, RTVFSOBJ hVfsObj)
{
    /*
     * Get the mode mask before creating the directory.
     */
    RTFSOBJINFO ObjInfo;
    int vrc = RTVfsObjQueryInfo(hVfsObj, &ObjInfo, RTFSOBJATTRADD_NOTHING);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsObjQueryInfo failed on '%s': %Rrc", pszDstDirName, vrc);
    ObjInfo.Attr.fMode &= ~(RTFS_UNIX_IWOTH | RTFS_UNIX_IWGRP);

    vrc = RTDirCreate(pszDstDirName, ObjInfo.Attr.fMode, 0);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create directory '%s': %Rrc", pszDstDirName, vrc);

#ifndef RT_OS_WINDOWS
    /*
     * Because of umask, we have to apply the mode again.
     */
    vrc = RTPathSetMode(pszDstDirName, ObjInfo.Attr.fMode);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to set directory permissions on '%s': %Rrc", pszDstDirName, vrc);
#else
    /** @todo Ownership tricks on windows? */
#endif
    return RTEXITCODE_SUCCESS;
}


/**
 * Unpacks a file from an extension pack tarball.
 *
 * @returns Program exit code, failure with message.
 * @param   pszName         The name in the tarball.
 * @param   pszDstFilename  The name of the unpacked file.
 * @param   hVfsIosSrc      The source stream for the file.
 * @param   hUnpackManifest The manifest to add the file digest to.
 */
static RTEXITCODE UnpackExtPackFile(const char *pszName, const char *pszDstFilename,
                                    RTVFSIOSTREAM hVfsIosSrc, RTMANIFEST hUnpackManifest)
{
    /*
     * Query the object info, we'll need it for buffer sizing as well as
     * setting the file mode.
     */
    RTFSOBJINFO ObjInfo;
    int vrc = RTVfsIoStrmQueryInfo(hVfsIosSrc, &ObjInfo, RTFSOBJATTRADD_NOTHING);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsIoStrmQueryInfo failed with %Rrc on '%s'", vrc, pszDstFilename);

    /*
     * Create the file.
     */
    uint32_t fFlags = RTFILE_O_WRITE | RTFILE_O_DENY_ALL | RTFILE_O_CREATE | (0600 << RTFILE_O_CREATE_MODE_SHIFT);
    RTFILE   hFile;
    vrc = RTFileOpen(&hFile, pszDstFilename, fFlags);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create '%s': %Rrc", pszDstFilename, vrc);

    /*
     * Create a I/O stream for the destination file, stack a manifest entry
     * creator on top of it.
     */
    RTVFSIOSTREAM hVfsIosDst2;
    vrc = RTVfsIoStrmFromRTFile(hFile, fFlags, true /*fLeaveOpen*/, &hVfsIosDst2);
    if (RT_SUCCESS(vrc))
    {
        RTVFSIOSTREAM hVfsIosDst;
        vrc = RTManifestEntryAddPassthruIoStream(hUnpackManifest, hVfsIosDst2, pszName,
                                                 RTMANIFEST_ATTR_SIZE | RTMANIFEST_ATTR_SHA256,
                                                 false /*fReadOrWrite*/, &hVfsIosDst);
        RTVfsIoStrmRelease(hVfsIosDst2);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Pump the data thru.
             */
            vrc = RTVfsUtilPumpIoStreams(hVfsIosSrc, hVfsIosDst, (uint32_t)RT_MIN(ObjInfo.cbObject, _1G));
            if (RT_SUCCESS(vrc))
            {
                vrc = RTManifestPtIosAddEntryNow(hVfsIosDst);
                if (RT_SUCCESS(vrc))
                {
                    RTVfsIoStrmRelease(hVfsIosDst);
                    hVfsIosDst = NIL_RTVFSIOSTREAM;

                    /*
                     * Set the mode mask.
                     */
                    ObjInfo.Attr.fMode &= ~(RTFS_UNIX_IWOTH | RTFS_UNIX_IWGRP);
                    vrc = RTFileSetMode(hFile, ObjInfo.Attr.fMode);
                    /** @todo Windows needs to do more here, I think. */
                    if (RT_SUCCESS(vrc))
                    {
                        RTFileClose(hFile);
                        return RTEXITCODE_SUCCESS;
                    }

                    RTMsgError("Failed to set the mode of '%s' to %RTfmode: %Rrc", pszDstFilename, ObjInfo.Attr.fMode, vrc);
                }
                else
                    RTMsgError("RTManifestPtIosAddEntryNow failed for '%s': %Rrc", pszDstFilename, vrc);
            }
            else
                RTMsgError("RTVfsUtilPumpIoStreams failed for '%s': %Rrc", pszDstFilename, vrc);
            RTVfsIoStrmRelease(hVfsIosDst);
        }
        else
            RTMsgError("RTManifestEntryAddPassthruIoStream failed: %Rrc", vrc);
    }
    else
        RTMsgError("RTVfsIoStrmFromRTFile failed: %Rrc", vrc);
    RTFileClose(hFile);
    return RTEXITCODE_FAILURE;
}


/**
 * Unpacks the extension pack into the specified directory.
 *
 * This will apply ownership and permission changes to all the content, the
 * exception is @a pszDirDst which will be handled by SetExtPackPermissions.
 *
 * @returns The program exit code.
 * @param   hTarballFile        The tarball to unpack.
 * @param   pszDirDst           Where to unpack it.
 * @param   hValidManifest      The manifest we've validated.
 * @param   pszTarball          The name of the tarball in case we have to
 *                              complain about something.
 */
static RTEXITCODE UnpackExtPack(RTFILE hTarballFile, const char *pszDirDst, RTMANIFEST hValidManifest,
                                const char *pszTarball)
{
    RT_NOREF1(pszTarball);
    RTMsgInfo("Unpacking extension pack into '%s'...", pszDirDst);

    /*
     * Set up the destination path.
     */
    char szDstPath[RTPATH_MAX];
    int vrc = RTPathAbs(pszDirDst, szDstPath, sizeof(szDstPath) - VBOX_EXTPACK_MAX_MEMBER_NAME_LENGTH - 2);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathAbs('%s',,) failed: %Rrc", pszDirDst, vrc);
    size_t offDstPath = RTPathStripTrailingSlash(szDstPath);
    szDstPath[offDstPath++] = '/';
    szDstPath[offDstPath]   = '\0';

    /*
     * Open the tar.gz filesystem stream and set up an manifest in-memory file.
     */
    RTVFSFSSTREAM hTarFss;
    RTEXITCODE rcExit = OpenTarFss(hTarballFile, &hTarFss);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    RTMANIFEST hUnpackManifest;
    vrc = RTManifestCreate(0 /*fFlags*/, &hUnpackManifest);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Process the tarball (would be nice to move this to a function).
         */
        for (;;)
        {
            /*
             * Get the next stream object.
             */
            char           *pszName;
            RTVFSOBJ        hVfsObj;
            RTVFSOBJTYPE    enmType;
            vrc = RTVfsFsStrmNext(hTarFss, &pszName, &enmType, &hVfsObj);
            if (RT_FAILURE(vrc))
            {
                if (vrc != VERR_EOF)
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsFsStrmNext failed: %Rrc", vrc);
                break;
            }
            const char     *pszAdjName = pszName[0] == '.' && pszName[1] == '/' ? &pszName[2] : pszName;

            /*
             * Check the type & name validity then unpack it.
             */
            rcExit = ValidateMemberOfExtPack(pszName, enmType, hVfsObj);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                szDstPath[offDstPath] = '\0';
                vrc = RTStrCopy(&szDstPath[offDstPath], sizeof(szDstPath) - offDstPath, pszAdjName);
                if (RT_SUCCESS(vrc))
                {
                    if (   enmType == RTVFSOBJTYPE_FILE
                        || enmType == RTVFSOBJTYPE_IO_STREAM)
                    {
                        RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
                        rcExit = UnpackExtPackFile(pszAdjName, szDstPath, hVfsIos, hUnpackManifest);
                        RTVfsIoStrmRelease(hVfsIos);
                    }
                    else if (*pszAdjName && strcmp(pszAdjName, "."))
                        rcExit = UnpackExtPackDir(szDstPath, hVfsObj);
                }
                else
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Name is too long: '%s' (%Rrc)", pszAdjName, vrc);
            }

            /*
             * Clean up and break out on failure.
             */
            RTVfsObjRelease(hVfsObj);
            RTStrFree(pszName);
            if (rcExit != RTEXITCODE_SUCCESS)
                break;
        }

        /*
         * Check that what we just extracted matches the already verified
         * manifest.
         */
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            char szError[RTPATH_MAX];
            vrc = RTManifestEqualsEx(hUnpackManifest, hValidManifest, NULL /*papszIgnoreEntries*/, NULL /*papszIgnoreAttr*/,
                                     0 /*fFlags*/, szError, sizeof(szError));
            if (RT_SUCCESS(vrc))
                rcExit = RTEXITCODE_SUCCESS;
            else if (vrc == VERR_NOT_EQUAL && szError[0])
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Manifest mismatch: %s", szError);
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTManifestEqualsEx failed: %Rrc", vrc);
        }
#if 0
        RTVFSIOSTREAM hVfsIosStdOut = NIL_RTVFSIOSTREAM;
        RTVfsIoStrmFromStdHandle(RTHANDLESTD_OUTPUT, RTFILE_O_WRITE, true, &hVfsIosStdOut);
        RTVfsIoStrmWrite(hVfsIosStdOut, "Unpack:\n", sizeof("Unpack:\n") - 1, true, NULL);
        RTManifestWriteStandard(hUnpackManifest, hVfsIosStdOut);
        RTVfsIoStrmWrite(hVfsIosStdOut, "Valid:\n", sizeof("Valid:\n") - 1, true, NULL);
        RTManifestWriteStandard(hValidManifest, hVfsIosStdOut);
#endif
        RTManifestRelease(hUnpackManifest);
    }
    RTVfsFsStrmRelease(hTarFss);

    return rcExit;
}



/**
 * Wrapper around VBoxExtPackValidateTarball.
 *
 * @returns The program exit code.
 * @param   hTarballFile        The handle to open the @a pszTarball file.
 * @param   pszExtPackName      The name of the extension pack name.
 * @param   pszTarball          The name of the tarball in case we have to
 *                              complain about something.
 * @param   pszTarballDigest    The SHA-256 digest of the tarball.
 * @param   phValidManifest     Where to return the handle to fully validated
 *                              the manifest for the extension pack.  This
 *                              includes all files.
 */
static RTEXITCODE ValidateExtPackTarball(RTFILE hTarballFile, const char *pszExtPackName, const char *pszTarball,
                                         const char *pszTarballDigest, PRTMANIFEST phValidManifest)
{
    *phValidManifest = NIL_RTMANIFEST;
    RTMsgInfo("Validating extension pack '%s' ('%s')...", pszTarball, pszExtPackName);
    Assert(pszTarballDigest && *pszTarballDigest);

    char szError[8192];
    int vrc = VBoxExtPackValidateTarball(hTarballFile, pszExtPackName, pszTarball, pszTarballDigest,
                                         szError, sizeof(szError), phValidManifest, NULL /*phXmlFile*/, NULL /*pStrDigest*/);
    if (RT_FAILURE(vrc))
    {
        Assert(szError[0]);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s", szError);
    }
    Assert(!szError[0]);
    return RTEXITCODE_SUCCESS;
}


/**
 * The 2nd part of the installation process.
 *
 * @returns The program exit code.
 * @param   pszBaseDir          The base directory.
 * @param   pszCertDir          The certificat directory.
 * @param   pszTarball          The tarball name.
 * @param   pszTarballDigest    The SHA-256 digest of the tarball.  Empty string
 *                              if no digest available.
 * @param   hTarballFile        The handle to open the @a pszTarball file.
 * @param   hTarballFileOpt     The tarball file handle (optional).
 * @param   pszName             The extension pack name.
 * @param   pszMangledName      The mangled extension pack name.
 * @param   fReplace            Whether to replace any existing ext pack.
 */
static RTEXITCODE DoInstall2(const char *pszBaseDir, const char *pszCertDir, const char *pszTarball,
                             const char *pszTarballDigest, RTFILE hTarballFile, RTFILE hTarballFileOpt,
                             const char *pszName, const char *pszMangledName, bool fReplace)
{
    RT_NOREF1(pszCertDir);

    /*
     * Do some basic validation of the tarball file.
     */
    RTFSOBJINFO ObjInfo;
    int vrc = RTFileQueryInfo(hTarballFile, &ObjInfo, RTFSOBJATTRADD_UNIX);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTFileQueryInfo failed with %Rrc on '%s'", vrc, pszTarball);
    if (!RTFS_IS_FILE(ObjInfo.Attr.fMode))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Not a regular file: %s", pszTarball);

    if (hTarballFileOpt != NIL_RTFILE)
    {
        RTFSOBJINFO ObjInfo2;
        vrc = RTFileQueryInfo(hTarballFileOpt, &ObjInfo2, RTFSOBJATTRADD_UNIX);
        if (RT_FAILURE(vrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTFileQueryInfo failed with %Rrc on --tarball-fd", vrc);
        if (   ObjInfo.Attr.u.Unix.INodeIdDevice != ObjInfo2.Attr.u.Unix.INodeIdDevice
            || ObjInfo.Attr.u.Unix.INodeId       != ObjInfo2.Attr.u.Unix.INodeId)
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "--tarball and --tarball-fd does not match");
    }

    /*
     * Construct the paths to the two directories we'll be using.
     */
    char szFinalPath[RTPATH_MAX];
    vrc = RTPathJoin(szFinalPath, sizeof(szFinalPath), pszBaseDir, pszMangledName);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              "Failed to construct the path to the final extension pack directory: %Rrc", vrc);

    char szTmpPath[RTPATH_MAX];
    vrc = RTPathJoin(szTmpPath, sizeof(szTmpPath) - 64, pszBaseDir, pszMangledName);
    if (RT_SUCCESS(vrc))
    {
        size_t cchTmpPath = strlen(szTmpPath);
        RTStrPrintf(&szTmpPath[cchTmpPath], sizeof(szTmpPath) - cchTmpPath, "-_-inst-%u", (uint32_t)RTProcSelf());
    }
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              "Failed to construct the path to the temporary extension pack directory: %Rrc", vrc);

    /*
     * Check that they don't exist at this point in time, unless fReplace=true.
     */
    vrc = RTPathQueryInfoEx(szFinalPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(vrc) && RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
    {
        if (!fReplace)
            return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                  "The extension pack is already installed. You must uninstall the old one first.");
    }
    else if (RT_SUCCESS(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
                              "Found non-directory file system object where the extension pack would be installed ('%s')",
                              szFinalPath);
    else if (vrc != VERR_FILE_NOT_FOUND && vrc != VERR_PATH_NOT_FOUND)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Unexpected RTPathQueryInfoEx status code %Rrc for '%s'", vrc, szFinalPath);

    vrc = RTPathQueryInfoEx(szTmpPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (vrc != VERR_FILE_NOT_FOUND && vrc != VERR_PATH_NOT_FOUND)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Unexpected RTPathQueryInfoEx status code %Rrc for '%s'", vrc, szFinalPath);

    /*
     * Create the temporary directory and prepare the extension pack within it.
     * If all checks out correctly, rename it to the final directory.
     */
    RTDirCreate(pszBaseDir, 0755, 0);
#ifndef RT_OS_WINDOWS
    /*
     * Because of umask, we have to apply the mode again.
     */
    vrc = RTPathSetMode(pszBaseDir, 0755);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to set directory permissions on '%s': %Rrc", pszBaseDir, vrc);
#else
    /** @todo Ownership tricks on windows? */
#endif
    vrc = RTDirCreate(szTmpPath, 0700, 0);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create temporary directory: %Rrc ('%s')", vrc, szTmpPath);

    RTMANIFEST hValidManifest = NIL_RTMANIFEST;
    RTEXITCODE rcExit = ValidateExtPackTarball(hTarballFile, pszName, pszTarball, pszTarballDigest, &hValidManifest);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = UnpackExtPack(hTarballFile, szTmpPath, hValidManifest, pszTarball);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = ValidateUnpackedExtPack(szTmpPath, pszTarball, pszName);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = SetExtPackPermissions(szTmpPath);
    RTManifestRelease(hValidManifest);

    if (rcExit == RTEXITCODE_SUCCESS)
    {
        vrc = CommonDirRenameWrapper(szTmpPath, szFinalPath, RTPATHRENAME_FLAGS_NO_REPLACE);
        if (   RT_FAILURE(vrc)
            && fReplace
            && RTDirExists(szFinalPath))
        {
            /* Automatic uninstall if --replace was given. */
            rcExit = CommonUninstallWorker(szFinalPath);
            if (rcExit == RTEXITCODE_SUCCESS)
                vrc = CommonDirRenameWrapper(szTmpPath, szFinalPath, RTPATHRENAME_FLAGS_NO_REPLACE);
        }
        if (RT_SUCCESS(vrc))
            RTMsgInfo("Successfully installed '%s' (%s)", pszName, pszTarball);
        else if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE,
                                    "Failed to rename the temporary directory to the final one: %Rrc ('%s' -> '%s')",
                                    vrc, szTmpPath, szFinalPath);
    }

    /*
     * Clean up the temporary directory on failure.
     */
    if (rcExit != RTEXITCODE_SUCCESS)
        RemoveExtPackDir(szTmpPath, true /*fTemporary*/);

    return rcExit;
}


/**
 * Implements the 'install' command.
 *
 * @returns The program exit code.
 * @param   argc            The number of program arguments.
 * @param   argv            The program arguments.
 */
static RTEXITCODE DoInstall(int argc, char **argv)
{
    /*
     * Parse the parameters.
     *
     * Note! The --base-dir and --cert-dir are only for checking that the
     *       caller and this help applications have the same idea of where
     *       things are.  Likewise, the --name is for verifying assumptions
     *       the caller made about the name.  The optional --tarball-fd option
     *       is just for easing the paranoia on the user side.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--base-dir",     'b',   RTGETOPT_REQ_STRING  },
        { "--cert-dir",     'c',   RTGETOPT_REQ_STRING  },
        { "--name",         'n',   RTGETOPT_REQ_STRING  },
        { "--tarball",      't',   RTGETOPT_REQ_STRING  },
        { "--tarball-fd",   'd',   RTGETOPT_REQ_UINT64  },
        { "--replace",      'r',   RTGETOPT_REQ_NOTHING },
        { "--sha-256",      's',   RTGETOPT_REQ_STRING  }
    };
    RTGETOPTSTATE   GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /*fFlags*/);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc\n", vrc);

    const char     *pszBaseDir          = NULL;
    const char     *pszCertDir          = NULL;
    const char     *pszName             = NULL;
    const char     *pszTarball          = NULL;
    const char     *pszTarballDigest    = NULL;
    RTFILE          hTarballFileOpt     = NIL_RTFILE;
    bool            fReplace            = false;
    RTGETOPTUNION   ValueUnion;
    int             ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'b':
                if (pszBaseDir)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --base-dir options");
                pszBaseDir = ValueUnion.psz;
                if (!IsValidBaseDir(pszBaseDir))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid base directory: '%s'", pszBaseDir);
                break;

            case 'c':
                if (pszCertDir)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --cert-dir options");
                pszCertDir = ValueUnion.psz;
                if (!IsValidCertificateDir(pszCertDir))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid certificate directory: '%s'", pszCertDir);
                break;

            case 'n':
                if (pszName)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --name options");
                pszName = ValueUnion.psz;
                if (!VBoxExtPackIsValidName(pszName))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid extension pack name: '%s'", pszName);
                break;

            case 't':
                if (pszTarball)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --tarball options");
                pszTarball = ValueUnion.psz;
                break;

            case 'd':
            {
                if (hTarballFileOpt != NIL_RTFILE)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --tarball-fd options");
                RTHCUINTPTR hNative = (RTHCUINTPTR)ValueUnion.u64;
                if (hNative != ValueUnion.u64)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "The --tarball-fd value is out of range: %#RX64", ValueUnion.u64);
                vrc = RTFileFromNative(&hTarballFileOpt, hNative);
                if (RT_FAILURE(vrc))
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "RTFileFromNative failed on --target-fd value: %Rrc", vrc);
                break;
            }

            case 'r':
                fReplace = true;
                break;

            case 's':
            {
                if (pszTarballDigest)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --sha-256 options");
                pszTarballDigest = ValueUnion.psz;

                uint8_t abDigest[RTSHA256_HASH_SIZE];
                vrc = RTSha256FromString(pszTarballDigest, abDigest);
                if (RT_FAILURE(vrc))
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Bad SHA-256 string: %Rrc", vrc);
                break;
            }

            case 'h':
            case 'V':
                return DoStandardOption(ch);

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszName)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --name option");
    if (!pszBaseDir)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --base-dir option");
    if (!pszCertDir)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --cert-dir option");
    if (!pszTarball)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --tarball option");
    if (!pszTarballDigest)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --sha-256 option");

    /*
     * Ok, down to business.
     */
    RTCString *pstrMangledName = VBoxExtPackMangleName(pszName);
    if (!pstrMangledName)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to mangle name ('%s)", pszName);

    RTEXITCODE  rcExit;
    RTFILE      hTarballFile;
    vrc = RTFileOpen(&hTarballFile, pszTarball, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(vrc))
    {
        rcExit = DoInstall2(pszBaseDir, pszCertDir, pszTarball, pszTarballDigest, hTarballFile, hTarballFileOpt,
                            pszName, pstrMangledName->c_str(), fReplace);
        RTFileClose(hTarballFile);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to open the extension pack tarball: %Rrc ('%s')", vrc, pszTarball);

    delete pstrMangledName;
    return rcExit;
}


/**
 * Implements the 'uninstall' command.
 *
 * @returns The program exit code.
 * @param   argc            The number of program arguments.
 * @param   argv            The program arguments.
 */
static RTEXITCODE DoUninstall(int argc, char **argv)
{
    /*
     * Parse the parameters.
     *
     * Note! The --base-dir is only for checking that the caller and this help
     *       applications have the same idea of where things are.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--base-dir",     'b',   RTGETOPT_REQ_STRING },
        { "--name",         'n',   RTGETOPT_REQ_STRING },
        { "--forced",       'f',   RTGETOPT_REQ_NOTHING },
    };
    RTGETOPTSTATE   GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /*fFlags*/);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc\n", vrc);

    const char     *pszBaseDir = NULL;
    const char     *pszName    = NULL;
    RTGETOPTUNION   ValueUnion;
    int             ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'b':
                if (pszBaseDir)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --base-dir options");
                pszBaseDir = ValueUnion.psz;
                if (!IsValidBaseDir(pszBaseDir))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid base directory: '%s'", pszBaseDir);
                break;

            case 'n':
                if (pszName)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --name options");
                pszName = ValueUnion.psz;
                if (!VBoxExtPackIsValidName(pszName))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid extension pack name: '%s'", pszName);
                break;

            case 'f':
                /* ignored */
                break;

            case 'h':
            case 'V':
                return DoStandardOption(ch);

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszName)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --name option");
    if (!pszBaseDir)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --base-dir option");

    /*
     * Mangle the name so we can construct the directory names.
     */
    RTCString *pstrMangledName = VBoxExtPackMangleName(pszName);
    if (!pstrMangledName)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to mangle name ('%s)", pszName);
    RTCString strMangledName(*pstrMangledName);
    delete pstrMangledName;

    /*
     * Ok, down to business.
     */
    /* Check that it exists. */
    char szExtPackDir[RTPATH_MAX];
    vrc = RTPathJoin(szExtPackDir, sizeof(szExtPackDir), pszBaseDir, strMangledName.c_str());
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to construct extension pack path: %Rrc", vrc);

    if (!RTDirExists(szExtPackDir))
    {
        RTMsgInfo("Extension pack not installed. Nothing to do.");
        return RTEXITCODE_SUCCESS;
    }

    RTEXITCODE rcExit = CommonUninstallWorker(szExtPackDir);
    if (rcExit == RTEXITCODE_SUCCESS)
        RTMsgInfo("Successfully removed extension pack '%s'\n", pszName);

    return rcExit;
}

/**
 * Implements the 'cleanup' command.
 *
 * @returns The program exit code.
 * @param   argc            The number of program arguments.
 * @param   argv            The program arguments.
 */
static RTEXITCODE DoCleanup(int argc, char **argv)
{
    /*
     * Parse the parameters.
     *
     * Note! The --base-dir is only for checking that the caller and this help
     *       applications have the same idea of where things are.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--base-dir",     'b',   RTGETOPT_REQ_STRING },
    };
    RTGETOPTSTATE   GetState;
    int vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /*fFlags*/);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc\n", vrc);

    const char     *pszBaseDir = NULL;
    RTGETOPTUNION   ValueUnion;
    int             ch;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'b':
                if (pszBaseDir)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Too many --base-dir options");
                pszBaseDir = ValueUnion.psz;
                if (!IsValidBaseDir(pszBaseDir))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Invalid base directory: '%s'", pszBaseDir);
                break;

            case 'h':
            case 'V':
                return DoStandardOption(ch);

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (!pszBaseDir)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Missing --base-dir option");

    /*
     * Ok, down to business.
     */
    RTDIR hDir;
    vrc = RTDirOpen(&hDir, pszBaseDir);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed open the base directory: %Rrc ('%s')", vrc, pszBaseDir);

    uint32_t    cCleaned = 0;
    RTEXITCODE  rcExit = RTEXITCODE_SUCCESS;
    for (;;)
    {
        RTDIRENTRYEX Entry;
        vrc = RTDirReadEx(hDir, &Entry, NULL /*pcbDirEntry*/, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
        if (RT_FAILURE(vrc))
        {
            if (vrc != VERR_NO_MORE_FILES)
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDirReadEx returns %Rrc", vrc);
            break;
        }

        /*
         * Only directories which conform with our temporary install/uninstall
         * naming scheme are candidates for cleaning.
         */
        if (   RTFS_IS_DIRECTORY(Entry.Info.Attr.fMode)
            && strcmp(Entry.szName, ".")  != 0
            && strcmp(Entry.szName, "..") != 0)
        {
            bool fCandidate = false;
            char *pszMarker = strstr(Entry.szName, "-_-");
            if (   pszMarker
                && (   !strcmp(pszMarker, "-_-uninst")
                    || !strncmp(pszMarker, RT_STR_TUPLE("-_-inst"))))
                fCandidate = VBoxExtPackIsValidMangledName(Entry.szName, pszMarker - &Entry.szName[0]);
            if (fCandidate)
            {
                /*
                 * Recursive delete, safe.
                 */
                char szPath[RTPATH_MAX];
                vrc = RTPathJoin(szPath, sizeof(szPath), pszBaseDir, Entry.szName);
                if (RT_SUCCESS(vrc))
                {
                    RTEXITCODE rcExit2 = RemoveExtPackDir(szPath, true /*fTemporary*/);
                    if (rcExit2 == RTEXITCODE_SUCCESS)
                        RTMsgInfo("Successfully removed '%s'.", Entry.szName);
                    else if (rcExit == RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                }
                else
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathJoin failed with %Rrc for '%s'", vrc, Entry.szName);
                cCleaned++;
            }
        }
    }
    RTDirClose(hDir);
    if (!cCleaned)
        RTMsgInfo("Nothing to clean.");
    return rcExit;
}

#ifdef WITH_ELEVATION

#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_DARWIN)
/**
 * Looks in standard locations for a suitable exec tool.
 *
 * @returns true if found, false if not.
 * @param   pszPath             Where to store the path to the tool on
 *                              successs.
 * @param   cbPath              The size of the buffer @a pszPath points to.
 * @param   pszName             The name of the tool we're looking for.
 */
static bool FindExecTool(char *pszPath, size_t cbPath, const char *pszName)
{
    static const char * const s_apszPaths[] =
    {
        "/bin",
        "/usr/bin",
        "/usr/local/bin",
        "/sbin",
        "/usr/sbin",
        "/usr/local/sbin",
#ifdef RT_OS_SOLARIS
        "/usr/sfw/bin",
        "/usr/gnu/bin",
        "/usr/xpg4/bin",
        "/usr/xpg6/bin",
        "/usr/openwin/bin",
        "/usr/ucb"
#endif
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_apszPaths); i++)
    {
        int vrc = RTPathJoin(pszPath, cbPath, s_apszPaths[i], pszName);
        if (RT_SUCCESS(vrc))
        {
            RTFSOBJINFO ObjInfo;
            vrc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_UNIX, RTPATH_F_FOLLOW_LINK);
            if (RT_SUCCESS(vrc))
            {
                if (!(ObjInfo.Attr.fMode & RTFS_UNIX_IWOTH))
                    return true;
            }
        }
    }
    return false;
}
#endif


/**
 * Copies the content of a file to a stream.
 *
 * @param   hSrc                The source file.
 * @param   pDst                The destination stream.
 * @param   fComplain           Whether to complain about errors (i.e. is this
 *                              stderr, if not keep the trap shut because it
 *                              may be missing when running under VBoxSVC.)
 */
static void CopyFileToStdXxx(RTFILE hSrc, PRTSTREAM pDst, bool fComplain)
{
    int vrc;
    for (;;)
    {
        char abBuf[0x1000];
        size_t cbRead;
        vrc = RTFileRead(hSrc, abBuf, sizeof(abBuf), &cbRead);
        if (RT_FAILURE(vrc))
        {
            RTMsgError("RTFileRead failed: %Rrc", vrc);
            break;
        }
        if (!cbRead)
            break;
        vrc = RTStrmWrite(pDst, abBuf, cbRead);
        if (RT_FAILURE(vrc))
        {
            if (fComplain)
                RTMsgError("RTStrmWrite failed: %Rrc", vrc);
            break;
        }
    }
    vrc = RTStrmFlush(pDst);
    if (RT_FAILURE(vrc) && fComplain)
        RTMsgError("RTStrmFlush failed: %Rrc", vrc);
}


/**
 * Relaunches ourselves as a elevated process using platform specific facilities.
 *
 * @returns Program exit code.
 * @param   pszExecPath         The executable path.
 * @param   papszArgs           The arguments.
 * @param   cSuArgs             The number of argument entries reserved for the
 *                              'su' like programs at the start of papszArgs.
 * @param   cMyArgs             The number of arguments following @a cSuArgs.
 * @param   iCmd                The command that is being executed. (For
 *                              selecting messages.)
 * @param   pszDisplayInfoHack  Display information hack.  Platform specific++.
 */
static RTEXITCODE RelaunchElevatedNative(const char *pszExecPath, const char **papszArgs, int cSuArgs, int cMyArgs,
                                         int iCmd, const char *pszDisplayInfoHack)
{
    RT_NOREF1(cMyArgs);
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
#ifdef RT_OS_WINDOWS
    NOREF(iCmd);

    MSG Msg;
    PeekMessage(&Msg, NULL, 0, 0, PM_NOREMOVE);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    SHELLEXECUTEINFOW   Info;

    Info.cbSize = sizeof(Info);
    Info.fMask  = SEE_MASK_NOCLOSEPROCESS;
    Info.hwnd   = NULL;
    Info.lpVerb = L"runas";
    int vrc = RTStrToUtf16(pszExecPath, (PRTUTF16 *)&Info.lpFile);
    if (RT_SUCCESS(vrc))
    {
        char *pszCmdLine;
        vrc = RTGetOptArgvToString(&pszCmdLine, &papszArgs[cSuArgs + 1], RTGETOPTARGV_CNV_QUOTE_MS_CRT);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTStrToUtf16(pszCmdLine, (PRTUTF16 *)&Info.lpParameters);
            if (RT_SUCCESS(vrc))
            {
                Info.lpDirectory = NULL;
                Info.nShow       = SW_SHOWMAXIMIZED;
                Info.hInstApp    = NULL;
                Info.lpIDList    = NULL;
                Info.lpClass     = NULL;
                Info.hkeyClass   = NULL;
                Info.dwHotKey    = 0;
                Info.hMonitor    = NULL;
                Info.hProcess    = INVALID_HANDLE_VALUE;

                /* Apply display hacks. */
                if (pszDisplayInfoHack)
                {
                    const char *pszArg = strstr(pszDisplayInfoHack, "hwnd=");
                    if (pszArg)
                    {
                        uint64_t u64Hwnd;
                        vrc = RTStrToUInt64Ex(pszArg + sizeof("hwnd=") - 1, NULL, 0, &u64Hwnd);
                        if (RT_SUCCESS(vrc))
                        {
                            HWND hwnd = (HWND)(uintptr_t)u64Hwnd;
                            Info.hwnd = hwnd;
                            Info.hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                        }
                    }
                }
                if (Info.hMonitor == NULL)
                {
                    POINT Pt = {0,0};
                    Info.hMonitor = MonitorFromPoint(Pt, MONITOR_DEFAULTTOPRIMARY);
                }
                if (Info.hMonitor != NULL)
                    Info.fMask |= SEE_MASK_HMONITOR;

                if (ShellExecuteExW(&Info))
                {
                    if (Info.hProcess != INVALID_HANDLE_VALUE)
                    {
                        /*
                         * Wait for the process, make sure the deal with messages.
                         */
                        for (;;)
                        {
                            DWORD dwRc = MsgWaitForMultipleObjects(1, &Info.hProcess, FALSE, 5000/*ms*/, QS_ALLEVENTS);
                            if (dwRc == WAIT_OBJECT_0)
                                break;
                            if (   dwRc != WAIT_TIMEOUT
                                && dwRc != WAIT_OBJECT_0 + 1)
                            {
                                RTMsgError("MsgWaitForMultipleObjects returned: %#x (%d), err=%u", dwRc, dwRc, GetLastError());
                                break;
                            }
                            while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
                            {
                                TranslateMessage(&Msg);
                                DispatchMessageW(&Msg);
                            }
                        }

                        DWORD dwExitCode;
                        if (GetExitCodeProcess(Info.hProcess, &dwExitCode))
                        {
                            if (dwExitCode < 128)
                                rcExit = (RTEXITCODE)dwExitCode;
                            else
                                rcExit = RTEXITCODE_FAILURE;
                        }
                        CloseHandle(Info.hProcess);
                    }
                    else
                        RTMsgError("ShellExecuteExW return INVALID_HANDLE_VALUE as Info.hProcess");
                }
                else
                    RTMsgError("ShellExecuteExW failed: %u (%#x)", GetLastError(), GetLastError());


                RTUtf16Free((PRTUTF16)Info.lpParameters);
            }
            RTStrFree(pszCmdLine);
        }

        RTUtf16Free((PRTUTF16)Info.lpFile);
    }
    else
        RTMsgError("RTStrToUtf16 failed: %Rc", vrc);

#elif defined(RT_OS_DARWIN)
    RT_NOREF(pszDisplayInfoHack);
    char szIconName[RTPATH_MAX];
    int vrc = RTPathAppPrivateArch(szIconName, sizeof(szIconName));
    if (RT_SUCCESS(vrc))
        vrc = RTPathAppend(szIconName, sizeof(szIconName), "../Resources/virtualbox.png");
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to construct icon path: %Rrc", vrc);

    AuthorizationRef AuthRef;
    OSStatus orc = AuthorizationCreate(NULL, 0, kAuthorizationFlagDefaults, &AuthRef);
    if (orc == errAuthorizationSuccess)
    {
        /*
         * Preautorize the privileged execution of ourselves.
         */
        AuthorizationItem   AuthItem        = { kAuthorizationRightExecute, 0, NULL, 0 };
        AuthorizationRights AuthRights      = { 1, &AuthItem };

        NOREF(iCmd);
        static char         s_szPrompt[]    = "VirtualBox needs further rights to make changes to your installation.\n\n";
        AuthorizationItem   aAuthEnvItems[] =
        {
            { kAuthorizationEnvironmentPrompt, strlen(s_szPrompt), s_szPrompt, 0 },
            { kAuthorizationEnvironmentIcon,   strlen(szIconName), szIconName, 0 }
        };
        AuthorizationEnvironment AuthEnv    = { RT_ELEMENTS(aAuthEnvItems), aAuthEnvItems };

        orc = AuthorizationCopyRights(AuthRef, &AuthRights, &AuthEnv,
                                      kAuthorizationFlagPreAuthorize | kAuthorizationFlagInteractionAllowed
                                      | kAuthorizationFlagExtendRights,
                                      NULL);
        if (orc == errAuthorizationSuccess)
        {
            /*
             * Execute with extra permissions
             */
            FILE *pSocketStrm;
#if defined(__clang__) || RT_GNUC_PREREQ(4, 4)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            orc = AuthorizationExecuteWithPrivileges(AuthRef, pszExecPath, kAuthorizationFlagDefaults,
                                                     (char * const *)&papszArgs[cSuArgs + 3],
                                                     &pSocketStrm);
#if defined(__clang__) || RT_GNUC_PREREQ(4, 4)
# pragma GCC diagnostic pop
#endif
            if (orc == errAuthorizationSuccess)
            {
                /*
                 * Read the output of the tool, the read will fail when it quits.
                 */
                for (;;)
                {
                    char achBuf[1024];
                    size_t cbRead = fread(achBuf, 1, sizeof(achBuf), pSocketStrm);
                    if (!cbRead)
                        break;
                    fwrite(achBuf, 1, cbRead, stdout);
                }
                rcExit = RTEXITCODE_SUCCESS;
                fclose(pSocketStrm);
            }
            else
                RTMsgError("AuthorizationExecuteWithPrivileges failed: %d", orc);
        }
        else if (orc == errAuthorizationCanceled)
            RTMsgError("Authorization canceled by the user");
        else
            RTMsgError("AuthorizationCopyRights failed: %d", orc);
        AuthorizationFree(AuthRef, kAuthorizationFlagDefaults);
    }
    else
        RTMsgError("AuthorizationCreate failed: %d", orc);

#else

    RT_NOREF2(pszExecPath, pszDisplayInfoHack);

    /*
     * Several of the alternatives below will require a command line.
     */
    char *pszCmdLine;
    int vrc = RTGetOptArgvToString(&pszCmdLine, &papszArgs[cSuArgs], RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptArgvToString failed: %Rrc", vrc);

    /*
     * Look for various standard stuff for executing a program as root.
     *
     * N.B. When adding new arguments, please make 100% sure RelaunchElevated
     *      allocates enough array entries.
     *
     * TODO: Feel free to contribute code for using PolicyKit directly.
     */
    bool        fHaveDisplayVar = RTEnvExist("DISPLAY");
    int         iSuArg          = cSuArgs;
    char        szExecTool[260];
    char        szXterm[260];

    /*
     * kdesudo is available on KDE3/KDE4
     */
    if (fHaveDisplayVar && FindExecTool(szExecTool, sizeof(szExecTool), "kdesudo"))
    {
        iSuArg = cSuArgs - 4;
        papszArgs[cSuArgs - 4] = szExecTool;
        papszArgs[cSuArgs - 3] = "--comment";
        papszArgs[cSuArgs - 2] = iCmd == CMD_INSTALL
                               ? "VirtualBox extension pack installer"
                               : iCmd == CMD_UNINSTALL
                               ? "VirtualBox extension pack uninstaller"
                               : "VirtualBox extension pack maintainer";
        papszArgs[cSuArgs - 1] = "--";
    }
    /*
     * gksu is our favorite as it is very well integrated.
     */
    else if (fHaveDisplayVar && FindExecTool(szExecTool, sizeof(szExecTool), "gksu"))
    {
#if 0 /* older gksu does not grok --description nor '--' and multiple args. */
        iSuArg = cSuArgs - 4;
        papszArgs[cSuArgs - 4] = szExecTool;
        papszArgs[cSuArgs - 3] = "--description";
        papszArgs[cSuArgs - 2] = iCmd == CMD_INSTALL
                               ? "VirtualBox extension pack installer"
                               : iCmd == CMD_UNINSTALL
                               ? "VirtualBox extension pack uninstaller"
                               : "VirtualBox extension pack maintainer";
        papszArgs[cSuArgs - 1] = "--";
#elif defined(RT_OS_SOLARIS) /* Force it not to use pfexec as it won't wait then. */
        iSuArg = cSuArgs - 4;
        papszArgs[cSuArgs - 4] = szExecTool;
        papszArgs[cSuArgs - 3] = "-au";
        papszArgs[cSuArgs - 2] = "root";
        papszArgs[cSuArgs - 1] = pszCmdLine;
        papszArgs[cSuArgs] = NULL;
#else
        iSuArg = cSuArgs - 2;
        papszArgs[cSuArgs - 2] = szExecTool;
        papszArgs[cSuArgs - 1] = pszCmdLine;
        papszArgs[cSuArgs] = NULL;
#endif
    }
    /*
     * pkexec may work for ssh console sessions as well if the right agents
     * are installed.  However it is very generic and does not allow for any
     * custom messages.  Thus it comes after gksu.
     */
    else if (FindExecTool(szExecTool, sizeof(szExecTool), "pkexec"))
    {
        iSuArg = cSuArgs - 1;
        papszArgs[cSuArgs - 1] = szExecTool;
    }
    /*
     * The ultimate fallback is running 'su -' within an xterm.  We use the
     * title of the xterm to tell what is going on.
     */
    else if (   fHaveDisplayVar
             && FindExecTool(szExecTool, sizeof(szExecTool), "su")
             && FindExecTool(szXterm, sizeof(szXterm), "xterm"))
    {
        iSuArg = cSuArgs - 9;
        papszArgs[cSuArgs - 9] = szXterm;
        papszArgs[cSuArgs - 8] = "-T";
        papszArgs[cSuArgs - 7] = iCmd == CMD_INSTALL
                               ? "VirtualBox extension pack installer - su"
                               : iCmd == CMD_UNINSTALL
                               ? "VirtualBox extension pack uninstaller - su"
                               : "VirtualBox extension pack maintainer - su";
        papszArgs[cSuArgs - 6] = "-e";
        papszArgs[cSuArgs - 5] = szExecTool;
        papszArgs[cSuArgs - 4] = "-";
        papszArgs[cSuArgs - 3] = "root";
        papszArgs[cSuArgs - 2] = "-c";
        papszArgs[cSuArgs - 1] = pszCmdLine;
        papszArgs[cSuArgs] = NULL;
    }
    else if (fHaveDisplayVar)
        RTMsgError("Unable to locate 'pkexec', 'gksu' or 'su+xterm'. Try perform the operation using VBoxManage running as root");
    else
        RTMsgError("Unable to locate 'pkexec'. Try perform the operation using VBoxManage running as root");
    if (iSuArg != cSuArgs)
    {
        AssertRelease(iSuArg >= 0);

        /*
         * Argument list constructed, execute it and wait for the exec
         * program to complete.
         */
        RTPROCESS hProcess;
        vrc = RTProcCreateEx(papszArgs[iSuArg], &papszArgs[iSuArg], RTENV_DEFAULT, 0 /*fFlags*/, NULL /*phStdIn*/,
                             NULL /*phStdOut*/, NULL /*phStdErr*/, NULL /*pszAsUser*/, NULL /*pszPassword*/, NULL /* pvExtraData*/,
                             &hProcess);
        if (RT_SUCCESS(vrc))
        {
            RTPROCSTATUS Status;
            vrc = RTProcWait(hProcess, RTPROCWAIT_FLAGS_BLOCK, &Status);
            if (RT_SUCCESS(vrc))
            {
                if (Status.enmReason == RTPROCEXITREASON_NORMAL)
                    rcExit = (RTEXITCODE)Status.iStatus;
                else
                    rcExit = RTEXITCODE_FAILURE;
            }
            else
                RTMsgError("Error while waiting for '%s': %Rrc", papszArgs[iSuArg], vrc);
        }
        else
            RTMsgError("Failed to execute '%s': %Rrc", papszArgs[iSuArg], vrc);
    }
    RTStrFree(pszCmdLine);

#endif
    return rcExit;
}


/**
 * Relaunches ourselves as a elevated process using platform specific facilities.
 *
 * @returns Program exit code.
 * @param   argc                The number of arguments.
 * @param   argv                The arguments.
 * @param   iCmd                The command that is being executed.
 * @param   pszDisplayInfoHack  Display information hack.  Platform specific++.
 */
static RTEXITCODE RelaunchElevated(int argc, char **argv, int iCmd, const char *pszDisplayInfoHack)
{
    /*
     * We need the executable name later, so get it now when it's easy to quit.
     */
    char szExecPath[RTPATH_MAX];
    if (!RTProcGetExecutablePath(szExecPath,sizeof(szExecPath)))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTProcGetExecutablePath failed");

    /*
     * Create a couple of temporary files for stderr and stdout.
     */
    char szTempDir[RTPATH_MAX - sizeof("/stderr")];
    int vrc = RTPathTemp(szTempDir, sizeof(szTempDir));
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathTemp failed: %Rrc", vrc);
    vrc = RTPathAppend(szTempDir, sizeof(szTempDir), "VBoxExtPackHelper-XXXXXX");
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathAppend failed: %Rrc", vrc);
    vrc = RTDirCreateTemp(szTempDir, 0700);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDirCreateTemp failed: %Rrc", vrc);

    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    char szStdOut[RTPATH_MAX];
    char szStdErr[RTPATH_MAX];
    vrc = RTPathJoin(szStdOut, sizeof(szStdOut), szTempDir, "stdout");
    if (RT_SUCCESS(vrc))
        vrc = RTPathJoin(szStdErr, sizeof(szStdErr), szTempDir, "stderr");
    if (RT_SUCCESS(vrc))
    {
        RTFILE hStdOut;
        vrc = RTFileOpen(&hStdOut, szStdOut, RTFILE_O_READWRITE | RTFILE_O_CREATE | RTFILE_O_DENY_NONE
                         | (0600 << RTFILE_O_CREATE_MODE_SHIFT));
        if (RT_SUCCESS(vrc))
        {
            RTFILE hStdErr;
            vrc = RTFileOpen(&hStdErr, szStdErr, RTFILE_O_READWRITE | RTFILE_O_CREATE | RTFILE_O_DENY_NONE
                             | (0600 << RTFILE_O_CREATE_MODE_SHIFT));
            if (RT_SUCCESS(vrc))
            {
                /*
                 * Insert the --elevated and stdout/err names into the argument
                 * list.  Note that darwin skips the --stdout bit, so don't
                 * change the order here.
                 */
                int const    cSuArgs   = 12;
                int          cArgs     = argc + 5 + 1;
                char const **papszArgs = (char const **)RTMemTmpAllocZ((cSuArgs + cArgs + 1) * sizeof(const char *));
                if (papszArgs)
                {
                    int iDst = cSuArgs;
                    papszArgs[iDst++] = argv[0];
                    papszArgs[iDst++] = "--stdout";
                    papszArgs[iDst++] = szStdOut;
                    papszArgs[iDst++] = "--stderr";
                    papszArgs[iDst++] = szStdErr;
                    papszArgs[iDst++] = "--elevated";
                    for (int iSrc = 1; iSrc <= argc; iSrc++)
                        papszArgs[iDst++] = argv[iSrc];

                    /*
                     * Do the platform specific process execution (waiting included).
                     */
                    rcExit = RelaunchElevatedNative(szExecPath, papszArgs, cSuArgs, cArgs, iCmd, pszDisplayInfoHack);

                    /*
                     * Copy the standard files to our standard handles.
                     */
                    CopyFileToStdXxx(hStdErr, g_pStdErr, true /*fComplain*/);
                    CopyFileToStdXxx(hStdOut, g_pStdOut, false);

                    RTMemTmpFree(papszArgs);
                }

                RTFileClose(hStdErr);
                RTFileDelete(szStdErr);
            }
            RTFileClose(hStdOut);
            RTFileDelete(szStdOut);
        }
    }
    RTDirRemove(szTempDir);

    return rcExit;
}


/**
 * Checks if the process is elevated or not.
 *
 * @returns RTEXITCODE_SUCCESS if preconditions are fine,
 *          otherwise error message + RTEXITCODE_FAILURE.
 * @param   pfElevated      Where to store the elevation indicator.
 */
static RTEXITCODE ElevationCheck(bool *pfElevated)
{
    *pfElevated = false;

# if defined(RT_OS_WINDOWS)
    /** @todo This should probably check if UAC is diabled and if we are
     *  Administrator first. Also needs to check for Vista+ first, probably.
     */
    DWORD       cb;
    RTEXITCODE  rcExit = RTEXITCODE_SUCCESS;
    HANDLE      hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "OpenProcessToken failed: %u (%#x)", GetLastError(), GetLastError());

    /*
     * Check if we're member of the Administrators group. If we aren't, there
     * is no way to elevate ourselves to system admin.
     * N.B. CheckTokenMembership does not do the job here (due to attributes?).
     */
    BOOL                        fIsAdmin    = FALSE;
    SID_IDENTIFIER_AUTHORITY    NtAuthority = SECURITY_NT_AUTHORITY;
    PSID                        pAdminGrpSid;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminGrpSid))
    {
# ifdef DEBUG
        char *pszAdminGrpSid = NULL;
        ConvertSidToStringSid(pAdminGrpSid, &pszAdminGrpSid);
# endif

        if (   !GetTokenInformation(hToken, TokenGroups, NULL, 0, &cb)
            && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            PTOKEN_GROUPS pTokenGroups = (PTOKEN_GROUPS)RTMemAllocZ(cb);
            if (GetTokenInformation(hToken, TokenGroups, pTokenGroups, cb, &cb))
            {
                for (DWORD iGrp = 0; iGrp < pTokenGroups->GroupCount; iGrp++)
                {
# ifdef DEBUG
                    char *pszGrpSid = NULL;
                    ConvertSidToStringSid(pTokenGroups->Groups[iGrp].Sid, &pszGrpSid);
# endif
                    if (EqualSid(pAdminGrpSid, pTokenGroups->Groups[iGrp].Sid))
                    {
                        /* That it's listed is enough I think, ignore attributes. */
                        fIsAdmin = TRUE;
                        break;
                    }
                }
            }
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "GetTokenInformation(TokenGroups,cb) failed: %u (%#x)", GetLastError(), GetLastError());
            RTMemFree(pTokenGroups);
        }
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "GetTokenInformation(TokenGroups,0) failed: %u (%#x)", GetLastError(), GetLastError());

        FreeSid(pAdminGrpSid);
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "AllocateAndInitializeSid failed: %u (%#x)", GetLastError(), GetLastError());
    if (fIsAdmin)
    {
        /*
         * Check the integrity level (Vista / UAC).
         */
# define MY_SECURITY_MANDATORY_HIGH_RID 0x00003000L
# define MY_TokenIntegrityLevel         ((TOKEN_INFORMATION_CLASS)25)
        if (   !GetTokenInformation(hToken, MY_TokenIntegrityLevel, NULL, 0, &cb)
            && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            PSID_AND_ATTRIBUTES pSidAndAttr = (PSID_AND_ATTRIBUTES)RTMemAlloc(cb);
            if (GetTokenInformation(hToken, MY_TokenIntegrityLevel, pSidAndAttr, cb, &cb))
            {
                DWORD dwIntegrityLevel = *GetSidSubAuthority(pSidAndAttr->Sid, *GetSidSubAuthorityCount(pSidAndAttr->Sid) - 1U);

                if (dwIntegrityLevel >= MY_SECURITY_MANDATORY_HIGH_RID)
                    *pfElevated = true;
            }
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "GetTokenInformation failed: %u (%#x)", GetLastError(), GetLastError());
            RTMemFree(pSidAndAttr);
        }
        else if (   GetLastError() == ERROR_INVALID_PARAMETER
                 || GetLastError() == ERROR_NOT_SUPPORTED)
            *pfElevated = true; /* Older Windows version. */
        else
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "GetTokenInformation failed: %u (%#x)", GetLastError(), GetLastError());
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Membership in the Administrators group is required to perform this action");

    CloseHandle(hToken);
    return rcExit;

# else
    /*
     * On Unixy systems, we check if the executable and the current user is
     * the same.  This heuristic works fine for both hardened and development
     * builds.
     */
    char szExecPath[RTPATH_MAX];
    if (RTProcGetExecutablePath(szExecPath, sizeof(szExecPath)) == NULL)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTProcGetExecutablePath failed");

    RTFSOBJINFO ObjInfo;
    int vrc = RTPathQueryInfoEx(szExecPath, &ObjInfo, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathQueryInfoEx failed");

    *pfElevated = ObjInfo.Attr.u.Unix.uid == geteuid()
               || ObjInfo.Attr.u.Unix.uid == getuid();
    return RTEXITCODE_SUCCESS;
# endif
}

#endif /* WITH_ELEVATION */

int main(int argc, char **argv)
{
    /*
     * Initialize IPRT and check that we're correctly installed.
     */
#ifdef RT_OS_WINDOWS
    int vrc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_UTF8_ARGV); /* WinMain gives us UTF-8, see below. */
#else
    int vrc = RTR3InitExe(argc, &argv, 0);
#endif
    if (RT_FAILURE(vrc))
        return RTMsgInitFailure(vrc);

    SUPR3HardenedVerifyInit();
    RTERRINFOSTATIC ErrInfo;
    RTErrInfoInitStatic(&ErrInfo);
    vrc = SUPR3HardenedVerifySelf(argv[0], true /*fInternal*/, &ErrInfo.Core);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s", ErrInfo.Core.pszMsg);

    /*
     * Elevation check.
     */
    const char *pszDisplayInfoHack = NULL;
    RTEXITCODE  rcExit;
#ifdef WITH_ELEVATION
    bool        fElevated;
    rcExit = ElevationCheck(&fElevated);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
#endif

    /*
     * Parse the top level arguments until we find a command.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "install",                CMD_INSTALL,        RTGETOPT_REQ_NOTHING },
        { "uninstall",              CMD_UNINSTALL,      RTGETOPT_REQ_NOTHING },
        { "cleanup",                CMD_CLEANUP,        RTGETOPT_REQ_NOTHING },
#ifdef WITH_ELEVATION
        { "--elevated",             OPT_ELEVATED,       RTGETOPT_REQ_NOTHING },
        { "--stdout",               OPT_STDOUT,         RTGETOPT_REQ_STRING  },
        { "--stderr",               OPT_STDERR,         RTGETOPT_REQ_STRING  },
#endif
        { "--display-info-hack",    OPT_DISP_INFO_HACK, RTGETOPT_REQ_STRING  },
    };
    RTGETOPTSTATE GetState;
    vrc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /*fFlags*/);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptInit failed: %Rrc\n", vrc);
    for (;;)
    {
        RTGETOPTUNION ValueUnion;
        int ch = RTGetOpt(&GetState, &ValueUnion);
        switch (ch)
        {
            case 0:
                return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No command specified");

            case CMD_INSTALL:
            case CMD_UNINSTALL:
            case CMD_CLEANUP:
            {
#ifdef WITH_ELEVATION
                if (!fElevated)
                    return RelaunchElevated(argc, argv, ch, pszDisplayInfoHack);
#endif
                int         cCmdargs     = argc - GetState.iNext;
                char      **papszCmdArgs = argv + GetState.iNext;
                switch (ch)
                {
                    case CMD_INSTALL:
                        rcExit = DoInstall(  cCmdargs, papszCmdArgs);
                        break;
                    case CMD_UNINSTALL:
                        rcExit = DoUninstall(cCmdargs, papszCmdArgs);
                        break;
                    case CMD_CLEANUP:
                        rcExit = DoCleanup(  cCmdargs, papszCmdArgs);
                        break;
                    default:
                        AssertReleaseFailedReturn(RTEXITCODE_FAILURE);
                }

                /*
                 * Standard error should end with rcExit=RTEXITCODE_SUCCESS on
                 * success since the exit code may otherwise get lost in the
                 * process elevation fun.
                 */
                RTStrmFlush(g_pStdOut);
                RTStrmFlush(g_pStdErr);
                switch (rcExit)
                {
                    case RTEXITCODE_SUCCESS:
                        RTStrmPrintf(g_pStdErr, "rcExit=RTEXITCODE_SUCCESS\n");
                        break;
                    default:
                        RTStrmPrintf(g_pStdErr, "rcExit=%d\n", rcExit);
                        break;
                }
                RTStrmFlush(g_pStdErr);
                RTStrmFlush(g_pStdOut);
                return rcExit;
            }

#ifdef WITH_ELEVATION
            case OPT_ELEVATED:
                fElevated = true;
                break;

            case OPT_STDERR:
            case OPT_STDOUT:
            {
# ifdef RT_OS_WINDOWS
                PRTUTF16 pwszName = NULL;
                vrc = RTStrToUtf16(ValueUnion.psz, &pwszName);
                if (RT_FAILURE(vrc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error converting '%s' to UTF-16: %Rrc\n", ValueUnion.psz, vrc);
                FILE *pFile = _wfreopen(pwszName, L"r+", ch == OPT_STDOUT ? stdout : stderr);
                RTUtf16Free(pwszName);
# else
                FILE *pFile = freopen(ValueUnion.psz, "r+", ch == OPT_STDOUT ? stdout : stderr);
# endif
                if (!pFile)
                {
                    vrc = RTErrConvertFromErrno(errno);
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "freopen on '%s': %Rrc", ValueUnion.psz, vrc);
                }
                break;
            }
#endif

            case OPT_DISP_INFO_HACK:
                if (pszDisplayInfoHack)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "--display-info-hack shall only occur once");
                pszDisplayInfoHack = ValueUnion.psz;
                break;

            case 'h':
            case 'V':
                return DoStandardOption(ch);

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
        /* not currently reached */
    }
    /* not reached */
}


#ifdef RT_OS_WINDOWS
extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    g_hInstance = hInstance;
    NOREF(hPrevInstance); NOREF(nShowCmd); NOREF(lpCmdLine);

    int vrc = RTR3InitExeNoArguments(0);
    if (RT_FAILURE(vrc))
        return RTMsgInitFailure(vrc);

    LPWSTR pwszCmdLine = GetCommandLineW();
    if (!pwszCmdLine)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "GetCommandLineW failed");

    char *pszCmdLine;
    vrc = RTUtf16ToUtf8(pwszCmdLine, &pszCmdLine); /* leaked */
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to convert the command line: %Rrc", vrc);

    int    cArgs;
    char **papszArgs;
    vrc = RTGetOptArgvFromString(&papszArgs, &cArgs, pszCmdLine, RTGETOPTARGV_CNV_QUOTE_MS_CRT, NULL);
    if (RT_SUCCESS(vrc))
    {
        vrc = main(cArgs, papszArgs);

        RTGetOptArgvFree(papszArgs);
    }
    else
        vrc = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTGetOptArgvFromString failed: %Rrc", vrc);
    RTStrFree(pszCmdLine);

    return vrc;
}
#endif

