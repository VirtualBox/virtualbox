/* $Id: SUPR3HardenedVerify.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Library - Verification of Hardened Installation.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#if defined(RT_OS_OS2)
# define INCL_BASE
# define INCL_ERRORS
# include <os2.h>
# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <sys/fcntl.h>
# include <sys/errno.h>
# include <sys/syslimits.h>

#elif defined(RT_OS_WINDOWS)
# include <iprt/nt/nt-and-windows.h>
# ifndef IN_SUP_HARDENED_R3
#  include <stdio.h>
# endif

#else /* UNIXes */
# include <sys/types.h>
# include <stdio.h>
# include <stdlib.h>
# include <dirent.h>
# include <dlfcn.h>
# include <fcntl.h>
# include <limits.h>
# include <errno.h>
# include <unistd.h>
# include <sys/stat.h>
# include <sys/time.h>
# include <sys/fcntl.h>
# include <pwd.h>
# ifdef RT_OS_DARWIN
#  include <mach-o/dyld.h>
# endif

#endif

#include <VBox/sup.h>
#include <VBox/err.h>
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include "SUPLibInternal.h"
#if defined(RT_OS_WINDOWS) && defined(VBOX_WITH_HARDENING)
# define SUPHNTVI_NO_NT_STUFF
# include "win/SUPHardenedVerify-win.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max path length acceptable for a trusted path. */
#define SUPR3HARDENED_MAX_PATH      260U

/** Enable to resolve symlinks using realpath() instead of cooking our own stuff. */
#define SUP_HARDENED_VERIFY_FOLLOW_SYMLINKS_USE_REALPATH 1

#ifdef RT_OS_SOLARIS
# define dirfd(d) ((d)->d_fd)
#endif

/** Compare table file names with externally supplied names. */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# define SUP_COMP_FILENAME  RTStrICmp
#else
# define SUP_COMP_FILENAME  suplibHardenedStrCmp
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * The files that gets verified.
 *
 * @todo This needs reviewing against the linux packages.
 * @todo The excessive use of kSupID_AppSharedLib needs to be reviewed at some point. For
 *       the time being we're building the linux packages with SharedLib pointing to
 *       AppPrivArch (lazy bird).
 *
 * @remarks If you add executables here, you might need to update
 *          g_apszSupNtVpAllowedVmExes in SUPHardenedVerifyProcess-win.cpp.
 */
static SUPINSTFILE const    g_aSupInstallFiles[] =
{
    /*  type,         dir,                       fOpt, "pszFile"              */
    /* ---------------------------------------------------------------------- */
    {   kSupIFT_Dll,  kSupID_AppPrivArch,       false, "VMMR0.r0" },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,       false, "VBoxDDR0.r0" },

#ifdef VBOX_WITH_RAW_MODE
    {   kSupIFT_Rc,   kSupID_AppPrivArch,       false, "VMMRC.rc" },
    {   kSupIFT_Rc,   kSupID_AppPrivArch,       false, "VBoxDDRC.rc" },
#endif

    {   kSupIFT_Dll,  kSupID_AppSharedLib,      false, "VBoxRT" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppSharedLib,      false, "VBoxVMM" SUPLIB_DLL_SUFF },
#if HC_ARCH_BITS == 32
    {   kSupIFT_Dll,  kSupID_AppSharedLib,       true, "VBoxREM32" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppSharedLib,       true, "VBoxREM64" SUPLIB_DLL_SUFF },
#endif
    {   kSupIFT_Dll,  kSupID_AppSharedLib,      false, "VBoxDD" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppSharedLib,      false, "VBoxDD2" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppSharedLib,      false, "VBoxDDU" SUPLIB_DLL_SUFF },
    {   kSupIFT_Exe,  kSupID_AppBin,             true, "VBoxVMMPreload" SUPLIB_EXE_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxVMMPreload" SUPLIB_DLL_SUFF },

//#ifdef VBOX_WITH_DEBUGGER_GUI
    {   kSupIFT_Dll,  kSupID_AppSharedLib,       true, "VBoxDbg" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppSharedLib,       true, "VBoxDbg3" SUPLIB_DLL_SUFF },
//#endif

//#ifdef VBOX_WITH_SHARED_CLIPBOARD
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxSharedClipboard" SUPLIB_DLL_SUFF },
//#endif
//#ifdef VBOX_WITH_SHARED_FOLDERS
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxSharedFolders" SUPLIB_DLL_SUFF },
//#endif
//#ifdef VBOX_WITH_DRAG_AND_DROP
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxDragAndDropSvc" SUPLIB_DLL_SUFF },
//#endif
//#ifdef VBOX_WITH_GUEST_PROPS
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxGuestPropSvc" SUPLIB_DLL_SUFF },
//#endif
//#ifdef VBOX_WITH_GUEST_CONTROL
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxGuestControlSvc" SUPLIB_DLL_SUFF },
//#endif
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxHostChannel" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxSharedCrOpenGL" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxOGLhostcrutil" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxOGLhosterrorspu" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxOGLrenderspu" SUPLIB_DLL_SUFF },

    {   kSupIFT_Exe,  kSupID_AppBin,             true, "VBoxManage" SUPLIB_EXE_SUFF },

#ifdef VBOX_WITH_MAIN
    {   kSupIFT_Exe,  kSupID_AppBin,            false, "VBoxSVC" SUPLIB_EXE_SUFF },
 #ifdef RT_OS_WINDOWS
    {   kSupIFT_Dll,  kSupID_AppSharedLib,      false, "VBoxC" SUPLIB_DLL_SUFF },
 #else
    {   kSupIFT_Dll,  kSupID_AppPrivArch,       false, "VBoxXPCOMIPCD" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppSharedLib,      false, "VBoxXPCOM" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArchComp,   false, "VBoxXPCOMIPCC" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArchComp,   false, "VBoxC" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArchComp,   false, "VBoxSVCM" SUPLIB_DLL_SUFF },
    {   kSupIFT_Data, kSupID_AppPrivArchComp,   false, "VBoxXPCOMBase.xpt" },
 #endif
#endif

    {   kSupIFT_Dll,  kSupID_AppSharedLib,       true, "VRDPAuth" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppSharedLib,       true, "VBoxAuth" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppSharedLib,       true, "VBoxVRDP" SUPLIB_DLL_SUFF },

//#ifdef VBOX_WITH_HEADLESS
    {   kSupIFT_Exe,  kSupID_AppBin,             true, "VBoxHeadless" SUPLIB_EXE_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxHeadless" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxVideoRecFB" SUPLIB_DLL_SUFF },
//#endif

//#ifdef VBOX_WITH_QTGUI
    {   kSupIFT_Exe,  kSupID_AppBin,             true, "VirtualBox" SUPLIB_EXE_SUFF },
# ifdef RT_OS_DARWIN
    {   kSupIFT_Exe,  kSupID_AppMacHelper,       true, "VirtualBoxVM" SUPLIB_EXE_SUFF },
# else
    {   kSupIFT_Exe,  kSupID_AppBin,             true, "VirtualBoxVM" SUPLIB_EXE_SUFF },
# endif
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VirtualBoxVM" SUPLIB_DLL_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "UICommon" SUPLIB_DLL_SUFF },
# if !defined(RT_OS_DARWIN) && !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
    {   kSupIFT_Dll,  kSupID_AppSharedLib,       true, "VBoxKeyboard" SUPLIB_DLL_SUFF },
# endif
//#endif

//#ifdef VBOX_WITH_VBOXSDL
    {   kSupIFT_Exe,  kSupID_AppBin,             true, "VBoxSDL" SUPLIB_EXE_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxSDL" SUPLIB_DLL_SUFF },
//#endif

//#ifdef VBOX_WITH_WEBSERVICES
    {   kSupIFT_Exe,  kSupID_AppBin,             true, "vboxwebsrv" SUPLIB_EXE_SUFF },
//#endif

#ifdef RT_OS_LINUX
    {   kSupIFT_Exe,  kSupID_AppBin,             true, "VBoxTunctl" SUPLIB_EXE_SUFF },
#endif

//#ifdef VBOX_WITH_NETFLT
    {   kSupIFT_Exe,  kSupID_AppBin,             true, "VBoxNetDHCP" SUPLIB_EXE_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxNetDHCP" SUPLIB_DLL_SUFF },
//#endif

//#ifdef VBOX_WITH_LWIP_NAT
    {   kSupIFT_Exe,  kSupID_AppBin,             true, "VBoxNetNAT" SUPLIB_EXE_SUFF },
    {   kSupIFT_Dll,  kSupID_AppPrivArch,        true, "VBoxNetNAT" SUPLIB_DLL_SUFF },
//#endif
#if defined(VBOX_WITH_HARDENING) && defined(RT_OS_WINDOWS)
# define HARDENED_TESTCASE_BIN_ENTRY(a_szName) \
        {   kSupIFT_TestExe, kSupID_AppBin, true, a_szName SUPLIB_EXE_SUFF }, \
        {   kSupIFT_TestDll, kSupID_AppBin, true, a_szName SUPLIB_DLL_SUFF }
    HARDENED_TESTCASE_BIN_ENTRY("tstMicro"),
    HARDENED_TESTCASE_BIN_ENTRY("tstPDMAsyncCompletion"),
    HARDENED_TESTCASE_BIN_ENTRY("tstPDMAsyncCompletionStress"),
    HARDENED_TESTCASE_BIN_ENTRY("tstVMM"),
    HARDENED_TESTCASE_BIN_ENTRY("tstVMREQ"),
# define HARDENED_TESTCASE_ENTRY(a_szName) \
        {   kSupIFT_TestExe, kSupID_Testcase, true, a_szName SUPLIB_EXE_SUFF }, \
        {   kSupIFT_TestDll, kSupID_Testcase, true, a_szName SUPLIB_DLL_SUFF }
    HARDENED_TESTCASE_ENTRY("tstCFGM"),
    HARDENED_TESTCASE_ENTRY("tstGIP-2"),
    HARDENED_TESTCASE_ENTRY("tstIntNet-1"),
    HARDENED_TESTCASE_ENTRY("tstMMHyperHeap"),
    HARDENED_TESTCASE_ENTRY("tstRTR0ThreadPreemptionDriver"),
    HARDENED_TESTCASE_ENTRY("tstRTR0MemUserKernelDriver"),
    HARDENED_TESTCASE_ENTRY("tstRTR0SemMutexDriver"),
    HARDENED_TESTCASE_ENTRY("tstRTR0TimerDriver"),
    HARDENED_TESTCASE_ENTRY("tstSSM"),
#endif
};


/** Array parallel to g_aSupInstallFiles containing per-file status info. */
static SUPVERIFIEDFILE  g_aSupVerifiedFiles[RT_ELEMENTS(g_aSupInstallFiles)];

/** Array index by install directory specifier containing info about verified directories. */
static SUPVERIFIEDDIR   g_aSupVerifiedDirs[kSupID_End];


/**
 * Assembles the path to a directory.
 *
 * @returns VINF_SUCCESS on success, some error code on failure (fFatal
 *          decides whether it returns or not).
 *
 * @param   enmDir              The directory.
 * @param   pszDst              Where to assemble the path.
 * @param   cchDst              The size of the buffer.
 * @param   fFatal              Whether failures should be treated as fatal (true) or not (false).
 * @param   pFile               The file (for darwin helper app paths).
 */
static int supR3HardenedMakePath(SUPINSTDIR enmDir, char *pszDst, size_t cchDst, bool fFatal, PCSUPINSTFILE pFile)
{
    int rc;
    switch (enmDir)
    {
        case kSupID_AppBin:
            rc = supR3HardenedPathAppBin(pszDst, cchDst);
            break;
        case kSupID_AppSharedLib:
            rc = supR3HardenedPathAppSharedLibs(pszDst, cchDst);
            break;
        case kSupID_AppPrivArch:
            rc = supR3HardenedPathAppPrivateArch(pszDst, cchDst);
            break;
        case kSupID_AppPrivArchComp:
            rc = supR3HardenedPathAppPrivateArch(pszDst, cchDst);
            if (RT_SUCCESS(rc))
            {
                size_t off = suplibHardenedStrLen(pszDst);
                if (cchDst - off >= sizeof("/components"))
                    suplibHardenedMemCopy(&pszDst[off], "/components", sizeof("/components"));
                else
                    rc = VERR_BUFFER_OVERFLOW;
            }
            break;
        case kSupID_AppPrivNoArch:
            rc = supR3HardenedPathAppPrivateNoArch(pszDst, cchDst);
            break;
        case kSupID_Testcase:
            rc = supR3HardenedPathAppBin(pszDst, cchDst);
            if (RT_SUCCESS(rc))
            {
                size_t off = suplibHardenedStrLen(pszDst);
                if (cchDst - off >= sizeof("/testcase"))
                    suplibHardenedMemCopy(&pszDst[off], "/testcase", sizeof("/testcase"));
                else
                    rc = VERR_BUFFER_OVERFLOW;
            }
            break;
#ifdef RT_OS_DARWIN
        case kSupID_AppMacHelper:
            rc = supR3HardenedPathAppBin(pszDst, cchDst);
            if (RT_SUCCESS(rc))
            {
                /* Up one level from the VirtualBox.app/Contents/MacOS directory: */
                size_t offDst = suplibHardenedStrLen(pszDst);
                while (offDst > 1 && pszDst[offDst - 1] == '/')
                    offDst--;
                while (offDst > 1 && pszDst[offDst - 1] != '/')
                    offDst--;

                /* Construct the path to the helper application's Contents/MacOS directory: */
                size_t cchFile = suplibHardenedStrLen(pFile->pszFile);
                if (offDst + cchFile + sizeof("Resources/.app/Contents/MacOS") <= cchDst)
                {
                    suplibHardenedMemCopy(&pszDst[offDst], RT_STR_TUPLE("Resources/"));
                    offDst += sizeof("Resources/") - 1;
                    suplibHardenedMemCopy(&pszDst[offDst], pFile->pszFile, cchFile);
                    offDst += cchFile;
                    suplibHardenedMemCopy(&pszDst[offDst], RT_STR_TUPLE(".app/Contents/MacOS") + 1);
                }
                else
                    rc = VERR_BUFFER_OVERFLOW;
            }
            break;
#endif
        default:
            return supR3HardenedError(VERR_INTERNAL_ERROR, fFatal,
                                      "supR3HardenedMakePath: enmDir=%d\n", enmDir);
    }
    if (RT_FAILURE(rc))
        supR3HardenedError(rc, fFatal,
                           "supR3HardenedMakePath: enmDir=%d rc=%d\n", enmDir, rc);
    NOREF(pFile);
    return rc;
}



/**
 * Assembles the path to a file table entry, with or without the actual filename.
 *
 * @returns VINF_SUCCESS on success, some error code on failure (fFatal
 *          decides whether it returns or not).
 *
 * @param   pFile               The file table entry.
 * @param   pszDst              Where to assemble the path.
 * @param   cchDst              The size of the buffer.
 * @param   fWithFilename       If set, the filename is included, otherwise it is omitted (no trailing slash).
 * @param   fFatal              Whether failures should be treated as fatal (true) or not (false).
 */
static int supR3HardenedMakeFilePath(PCSUPINSTFILE pFile, char *pszDst, size_t cchDst, bool fWithFilename, bool fFatal)
{
    /*
     * Combine supR3HardenedMakePath and the filename.
     */
    int rc = supR3HardenedMakePath(pFile->enmDir, pszDst, cchDst, fFatal, pFile);
    if (RT_SUCCESS(rc) && fWithFilename)
    {
        size_t cchFile = suplibHardenedStrLen(pFile->pszFile);
        size_t off = suplibHardenedStrLen(pszDst);
        if (cchDst - off >= cchFile + 2)
        {
            pszDst[off++] = '/';
            suplibHardenedMemCopy(&pszDst[off], pFile->pszFile, cchFile + 1);
        }
        else
            rc = supR3HardenedError(VERR_BUFFER_OVERFLOW, fFatal,
                                    "supR3HardenedMakeFilePath: pszFile=%s off=%lu\n",
                                    pFile->pszFile, (long)off);
    }
    return rc;
}


/**
 * Verifies a directory.
 *
 * @returns VINF_SUCCESS on success. On failure, an error code is returned if
 *          fFatal is clear and if it's set the function wont return.
 * @param   enmDir              The directory specifier.
 * @param   fFatal              Whether validation failures should be treated as
 *                              fatal (true) or not (false).
 * @param   pFile               The file (for darwin helper app paths).
 */
DECLHIDDEN(int) supR3HardenedVerifyFixedDir(SUPINSTDIR enmDir, bool fFatal, PCSUPINSTFILE pFile)
{
    /*
     * Validate the index just to be on the safe side...
     */
    if (enmDir <= kSupID_Invalid || enmDir >= kSupID_End)
        return supR3HardenedError(VERR_INTERNAL_ERROR, fFatal,
                                  "supR3HardenedVerifyDir: enmDir=%d\n", enmDir);

    /*
     * Already validated?
     */
    if (g_aSupVerifiedDirs[enmDir].fValidated)
        return VINF_SUCCESS;  /** @todo revalidate? */

    /* initialize the entry. */
    if (g_aSupVerifiedDirs[enmDir].hDir != 0)
        supR3HardenedError(VERR_INTERNAL_ERROR, fFatal,
                           "supR3HardenedVerifyDir: hDir=%p enmDir=%d\n",
                           (void *)g_aSupVerifiedDirs[enmDir].hDir, enmDir);
    g_aSupVerifiedDirs[enmDir].hDir = -1;
    g_aSupVerifiedDirs[enmDir].fValidated = false;

    /*
     * Make the path and open the directory.
     */
    char szPath[RTPATH_MAX];
    int rc = supR3HardenedMakePath(enmDir, szPath, sizeof(szPath), fFatal, pFile);
    if (RT_SUCCESS(rc))
    {
#if defined(RT_OS_WINDOWS)
        PRTUTF16 pwszPath;
        rc = RTStrToUtf16(szPath, &pwszPath);
        if (RT_SUCCESS(rc))
        {
            HANDLE hDir = CreateFileW(pwszPath,
                                      GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
                                      NULL,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
                                      NULL);
            if (hDir != INVALID_HANDLE_VALUE)
            {
                /** @todo check the type */
                /* That's all on windows, for now at least... */
                g_aSupVerifiedDirs[enmDir].hDir = (intptr_t)hDir;
                g_aSupVerifiedDirs[enmDir].fValidated = true;
            }
            else if (enmDir == kSupID_Testcase)
            {
                g_aSupVerifiedDirs[enmDir].fValidated = true;
                rc = VINF_SUCCESS; /* Optional directory, ignore if missing. */
            }
            else
            {
                int err = RtlGetLastWin32Error();
                rc = supR3HardenedError(VERR_PATH_NOT_FOUND, fFatal,
                                        "supR3HardenedVerifyDir: Failed to open \"%s\": err=%d\n",
                                        szPath, err);
            }
            RTUtf16Free(pwszPath);
        }
        else
            rc = supR3HardenedError(rc, fFatal,
                                    "supR3HardenedVerifyDir: Failed to convert \"%s\" to UTF-16: err=%d\n", szPath, rc);

#else /* UNIXY */
        int fd = open(szPath, O_RDONLY, 0);
        if (fd >= 0)
        {
            /*
             * On unixy systems we'll make sure the directory is owned by root
             * and not writable by the group and user.
             */
            struct stat st;
            if (!fstat(fd, &st))
            {

                if (    st.st_uid == 0
                    &&  !(st.st_mode & (S_IWGRP | S_IWOTH))
                    &&  S_ISDIR(st.st_mode))
                {
                    g_aSupVerifiedDirs[enmDir].hDir = fd;
                    g_aSupVerifiedDirs[enmDir].fValidated = true;
                }
                else
                {
                    if (!S_ISDIR(st.st_mode))
                        rc = supR3HardenedError(VERR_NOT_A_DIRECTORY, fFatal,
                                                "supR3HardenedVerifyDir: \"%s\" is not a directory\n",
                                                szPath, (long)st.st_uid);
                    else if (st.st_uid)
                        rc = supR3HardenedError(VERR_ACCESS_DENIED, fFatal,
                                                "supR3HardenedVerifyDir: Cannot trust the directory \"%s\": not owned by root (st_uid=%ld)\n",
                                                szPath, (long)st.st_uid);
                    else
                        rc = supR3HardenedError(VERR_ACCESS_DENIED, fFatal,
                                                "supR3HardenedVerifyDir: Cannot trust the directory \"%s\": group and/or other writable (st_mode=0%lo)\n",
                                                szPath, (long)st.st_mode);
                    close(fd);
                }
            }
            else
            {
                int err = errno;
                rc = supR3HardenedError(VERR_ACCESS_DENIED, fFatal,
                                        "supR3HardenedVerifyDir: Failed to fstat \"%s\": %s (%d)\n",
                                        szPath, strerror(err), err);
                close(fd);
            }
        }
        else if (enmDir == kSupID_Testcase)
        {
            g_aSupVerifiedDirs[enmDir].fValidated = true;
            rc = VINF_SUCCESS; /* Optional directory, ignore if missing. */
        }
        else
        {
            int err = errno;
            rc = supR3HardenedError(VERR_PATH_NOT_FOUND, fFatal,
                                    "supR3HardenedVerifyDir: Failed to open \"%s\": %s (%d)\n",
                                    szPath, strerror(err), err);
        }
#endif /* UNIXY */
    }

    return rc;
}


#ifdef RT_OS_WINDOWS
/**
 * Opens the file for verification.
 *
 * @returns VINF_SUCCESS on success. On failure, an error code is returned if
 *          fFatal is clear and if it's set the function wont return.
 * @param   pFile               The file entry.
 * @param   fFatal              Whether validation failures should be treated as
 *                              kl  fatal (true) or not (false).
 * @param   phFile              The file handle, set to -1 if we failed to open
 *                              the file.  The function may return VINF_SUCCESS
 *                              and a -1 handle if the file is optional.
 */
static int supR3HardenedVerifyFileOpen(PCSUPINSTFILE pFile, bool fFatal, intptr_t *phFile)
{
    *phFile = -1;

    char szPath[RTPATH_MAX];
    int rc = supR3HardenedMakeFilePath(pFile, szPath, sizeof(szPath), true /*fWithFilename*/, fFatal);
    if (RT_SUCCESS(rc))
    {
        PRTUTF16 pwszPath;
        rc = RTStrToUtf16(szPath, &pwszPath);
        if (RT_SUCCESS(rc))
        {
            HANDLE hFile = CreateFileW(pwszPath,
                                       GENERIC_READ,
                                       FILE_SHARE_READ,
                                       NULL,
                                       OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL,
                                       NULL);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                *phFile = (intptr_t)hFile;
                rc = VINF_SUCCESS;
            }
            else
            {
                int err = RtlGetLastWin32Error();
                if (   !pFile->fOptional
                    || (    err != ERROR_FILE_NOT_FOUND
                        &&  (err != ERROR_PATH_NOT_FOUND || pFile->enmDir != kSupID_Testcase) ) )
                    rc = supR3HardenedError(VERR_PATH_NOT_FOUND, fFatal,
                                            "supR3HardenedVerifyFileInternal: Failed to open '%s': err=%d\n", szPath, err);
            }
            RTUtf16Free(pwszPath);
        }
        else
            rc = supR3HardenedError(rc, fFatal, "supR3HardenedVerifyFileInternal: Failed to convert '%s' to UTF-16: %Rrc\n",
                                    szPath, rc);
    }
    return rc;
}


/**
 * Worker for supR3HardenedVerifyFileInternal.
 *
 * @returns VINF_SUCCESS on success. On failure, an error code is returned if
 *          fFatal is clear and if it's set the function wont return.
 * @param   pFile               The file entry.
 * @param   pVerified           The verification record.
 * @param   fFatal              Whether validation failures should be treated as
 *                              fatal (true) or not (false).
 * @param   fLeaveFileOpen      Whether the file should be left open.
 */
static int supR3HardenedVerifyFileSignature(PCSUPINSTFILE pFile, PSUPVERIFIEDFILE pVerified, bool fFatal, bool fLeaveFileOpen)
{
# if defined(VBOX_WITH_HARDENING) && !defined(IN_SUP_R3_STATIC) /* Latter: Not in VBoxCpuReport and friends. */

    /*
     * Open the file if we have to.
     */
    int rc;
    intptr_t hFileOpened;
    intptr_t hFile = pVerified->hFile;
    if (hFile != -1)
        hFileOpened = -1;
    else
    {
        rc = supR3HardenedVerifyFileOpen(pFile, fFatal, &hFileOpened);
        if (RT_FAILURE(rc))
            return rc;
        hFile = hFileOpened;
    }

    /*
     * Verify the signature.
     */
    char szErr[1024];
    RTERRINFO ErrInfo;
    RTErrInfoInit(&ErrInfo, szErr, sizeof(szErr));

    uint32_t fFlags = SUPHNTVI_F_REQUIRE_BUILD_CERT;
    if (pFile->enmType == kSupIFT_Rc)
        fFlags |= SUPHNTVI_F_RC_IMAGE;

    rc = supHardenedWinVerifyImageByHandleNoName((HANDLE)hFile, fFlags, &ErrInfo);
    if (RT_SUCCESS(rc))
        pVerified->fCheckedSignature = true;
    else
    {
        pVerified->fCheckedSignature = false;
        rc = supR3HardenedError(rc, fFatal, "supR3HardenedVerifyFileInternal: '%s': Image verify error rc=%Rrc: %s\n",
                                pFile->pszFile, rc, szErr);

    }

    /*
     * Close the handle if we opened the file and we should close it.
     */
    if (hFileOpened != -1)
    {
        if (fLeaveFileOpen && RT_SUCCESS(rc))
            pVerified->hFile = hFileOpened;
        else
            NtClose((HANDLE)hFileOpened);
    }

    return rc;

# else  /* Not checking signatures. */
    RT_NOREF4(pFile, pVerified, fFatal, fLeaveFileOpen);
    return VINF_SUCCESS;
# endif /* Not checking signatures. */
}
#endif


/**
 * Verifies a file entry.
 *
 * @returns VINF_SUCCESS on success. On failure, an error code is returned if
 *          fFatal is clear and if it's set the function wont return.
 *
 * @param   iFile               The file table index of the file to be verified.
 * @param   fFatal              Whether validation failures should be treated as
 *                              fatal (true) or not (false).
 * @param   fLeaveFileOpen      Whether the file should be left open.
 * @param   fVerifyAll          Set if this is an verify all call and we will
 *                              postpone signature checking.
 */
static int supR3HardenedVerifyFileInternal(int iFile, bool fFatal, bool fLeaveFileOpen, bool fVerifyAll)
{
#ifndef RT_OS_WINDOWS
    RT_NOREF1(fVerifyAll);
#endif
    PCSUPINSTFILE pFile = &g_aSupInstallFiles[iFile];
    PSUPVERIFIEDFILE pVerified = &g_aSupVerifiedFiles[iFile];

    /*
     * Already done validation?  Do signature validation if we haven't yet.
     */
    if (pVerified->fValidated)
    {
        /** @todo revalidate? Check that the file hasn't been replace or similar. */
#ifdef RT_OS_WINDOWS
        if (!pVerified->fCheckedSignature && !fVerifyAll)
            return supR3HardenedVerifyFileSignature(pFile, pVerified, fFatal, fLeaveFileOpen);
#endif
        return VINF_SUCCESS;
    }


    /* initialize the entry. */
    if (pVerified->hFile != 0)
        supR3HardenedError(VERR_INTERNAL_ERROR, fFatal,
                           "supR3HardenedVerifyFileInternal: hFile=%p (%s)\n",
                           (void *)pVerified->hFile, pFile->pszFile);
    pVerified->hFile = -1;
    pVerified->fValidated = false;
#ifdef RT_OS_WINDOWS
    pVerified->fCheckedSignature = false;
#endif

    /*
     * Verify the directory then proceed to open it.
     * (This'll make sure the directory is opened and that we can (later)
     *  use openat if we wish.)
     */
    int rc = supR3HardenedVerifyFixedDir(pFile->enmDir, fFatal, pFile);
    if (RT_SUCCESS(rc))
    {
#if defined(RT_OS_WINDOWS)
        rc = supR3HardenedVerifyFileOpen(pFile, fFatal, &pVerified->hFile);
        if (RT_SUCCESS(rc))
        {
            if (!fVerifyAll)
                rc = supR3HardenedVerifyFileSignature(pFile, pVerified, fFatal, fLeaveFileOpen);
            if (RT_SUCCESS(rc))
            {
                pVerified->fValidated = true;
                if (!fLeaveFileOpen)
                {
                    NtClose((HANDLE)pVerified->hFile);
                    pVerified->hFile = -1;
                }
            }
        }
#else /* !RT_OS_WINDOWS */
        char szPath[RTPATH_MAX];
        rc = supR3HardenedMakeFilePath(pFile, szPath, sizeof(szPath), true /*fWithFilename*/, fFatal);
        if (RT_SUCCESS(rc))
        {
            int fd = open(szPath, O_RDONLY, 0);
            if (fd >= 0)
            {
                /*
                 * On unixy systems we'll make sure the file is owned by root
                 * and not writable by the group and user.
                 */
                struct stat st;
                if (!fstat(fd, &st))
                {
                    if (    st.st_uid == 0
                        &&  !(st.st_mode & (S_IWGRP | S_IWOTH))
                        &&  S_ISREG(st.st_mode))
                    {
                        /* it's valid. */
                        if (fLeaveFileOpen)
                            pVerified->hFile = fd;
                        else
                            close(fd);
                        pVerified->fValidated = true;
                    }
                    else
                    {
                        if (!S_ISREG(st.st_mode))
                            rc = supR3HardenedError(VERR_IS_A_DIRECTORY, fFatal,
                                                    "supR3HardenedVerifyFileInternal: \"%s\" is not a regular file\n",
                                                    szPath, (long)st.st_uid);
                        else if (st.st_uid)
                            rc = supR3HardenedError(VERR_ACCESS_DENIED, fFatal,
                                                    "supR3HardenedVerifyFileInternal: Cannot trust the file \"%s\": not owned by root (st_uid=%ld)\n",
                                                    szPath, (long)st.st_uid);
                        else
                            rc = supR3HardenedError(VERR_ACCESS_DENIED, fFatal,
                                                    "supR3HardenedVerifyFileInternal: Cannot trust the file \"%s\": group and/or other writable (st_mode=0%lo)\n",
                                                    szPath, (long)st.st_mode);
                        close(fd);
                    }
                }
                else
                {
                    int err = errno;
                    rc = supR3HardenedError(VERR_ACCESS_DENIED, fFatal,
                                            "supR3HardenedVerifyFileInternal: Failed to fstat \"%s\": %s (%d)\n",
                                            szPath, strerror(err), err);
                    close(fd);
                }
            }
            else
            {
                int err = errno;
                if (!pFile->fOptional || err != ENOENT)
                    rc = supR3HardenedError(VERR_PATH_NOT_FOUND, fFatal,
                                            "supR3HardenedVerifyFileInternal: Failed to open \"%s\": %s (%d)\n",
                                            szPath, strerror(err), err);
            }
        }
#endif /* !RT_OS_WINDOWS */
    }

    return rc;
}


/**
 * Verifies that the specified table entry matches the given filename.
 *
 * @returns VINF_SUCCESS if matching. On mismatch fFatal indicates whether an
 *          error is returned or we terminate the application.
 *
 * @param   iFile               The file table index.
 * @param   pszFilename         The filename.
 * @param   fFatal              Whether validation failures should be treated as
 *                              fatal (true) or not (false).
 */
static int supR3HardenedVerifySameFile(int iFile, const char *pszFilename, bool fFatal)
{
    PCSUPINSTFILE pFile = &g_aSupInstallFiles[iFile];

    /*
     * Construct the full path for the file table entry
     * and compare it with the specified file.
     */
    char szName[RTPATH_MAX];
    int rc = supR3HardenedMakeFilePath(pFile, szName, sizeof(szName), true /*fWithFilename*/, fFatal);
    if (RT_FAILURE(rc))
        return rc;
    if (SUP_COMP_FILENAME(szName, pszFilename))
    {
        /*
         * Normalize the two paths and compare again.
         */
        rc = VERR_NOT_SAME_DEVICE;
#if defined(RT_OS_WINDOWS)
        LPSTR pszIgnored;
        char szName2[RTPATH_MAX]; /** @todo Must use UTF-16 here! Code is mixing UTF-8 and native. */
        if (    GetFullPathName(szName, RT_ELEMENTS(szName2), &szName2[0], &pszIgnored)
            &&  GetFullPathName(pszFilename, RT_ELEMENTS(szName), &szName[0], &pszIgnored))
            if (!SUP_COMP_FILENAME(szName2, szName))
                rc = VINF_SUCCESS;
#else
        AssertCompile(RTPATH_MAX >= PATH_MAX);
        char szName2[RTPATH_MAX];
        if (    realpath(szName, szName2) != NULL
            &&  realpath(pszFilename, szName) != NULL)
            if (!SUP_COMP_FILENAME(szName2, szName))
                rc = VINF_SUCCESS;
#endif

        if (RT_FAILURE(rc))
        {
            supR3HardenedMakeFilePath(pFile, szName, sizeof(szName), true /*fWithFilename*/, fFatal);
            return supR3HardenedError(rc, fFatal,
                                      "supR3HardenedVerifySameFile: \"%s\" isn't the same as \"%s\"\n",
                                      pszFilename, szName);
        }
    }

    /*
     * Check more stuff like the stat info if it's an already open file?
     */



    return VINF_SUCCESS;
}


/**
 * Verifies a file.
 *
 * @returns VINF_SUCCESS on success.
 *          VERR_NOT_FOUND if the file isn't in the table, this isn't ever a fatal error.
 *          On verification failure, an error code will be returned when fFatal is clear,
 *          otherwise the program will be terminated.
 *
 * @param   pszFilename         The filename.
 * @param   fFatal              Whether validation failures should be treated as
 *                              fatal (true) or not (false).
 */
DECLHIDDEN(int) supR3HardenedVerifyFixedFile(const char *pszFilename, bool fFatal)
{
    /*
     * Lookup the file and check if it's the same file.
     */
    const char *pszName = supR3HardenedPathFilename(pszFilename);
    for (unsigned iFile = 0; iFile < RT_ELEMENTS(g_aSupInstallFiles); iFile++)
        if (!SUP_COMP_FILENAME(pszName, g_aSupInstallFiles[iFile].pszFile))
        {
            int rc = supR3HardenedVerifySameFile(iFile, pszFilename, fFatal);
            if (RT_SUCCESS(rc))
                rc = supR3HardenedVerifyFileInternal(iFile, fFatal, false /* fLeaveFileOpen */, false /* fVerifyAll */);
            return rc;
        }

    return VERR_NOT_FOUND;
}


/**
 * Verifies a program, worker for supR3HardenedVerifyAll.
 *
 * @returns See supR3HardenedVerifyAll.
 * @param   pszProgName         See supR3HardenedVerifyAll.
 * @param   pszExePath          The path to the executable.
 * @param   fFatal              See supR3HardenedVerifyAll.
 * @param   fLeaveOpen          The leave open setting used by
 *                              supR3HardenedVerifyAll.
 * @param   fMainFlags          Flags supplied to SUPR3HardenedMain.
 */
static int supR3HardenedVerifyProgram(const char *pszProgName, const char *pszExePath, bool fFatal,
                                      bool fLeaveOpen, uint32_t fMainFlags)
{
    /*
     * Search the table looking for the executable and the DLL/DYLIB/SO.
     * Note! On darwin we have a hack in place for VirtualBoxVM helper app
     *       to share VirtualBox.dylib with the VirtualBox app.  This ASSUMES
     *       that cchProgNameDll is equal or shorter to the exe name.
     */
    int             rc = VINF_SUCCESS;
    bool            fExe = false;
    bool            fDll = false;
    size_t const    cchProgNameExe = suplibHardenedStrLen(pszProgName);
#ifndef RT_OS_DARWIN
    size_t const    cchProgNameDll = cchProgNameExe;
    NOREF(fMainFlags);
#else
    size_t const    cchProgNameDll = fMainFlags & SUPSECMAIN_FLAGS_OSX_VM_APP
                                   ? sizeof("VirtualBox") - 1
                                   : cchProgNameExe;
    if (cchProgNameDll > cchProgNameExe)
        return supR3HardenedError(VERR_INTERNAL_ERROR, fFatal,
                                  "supR3HardenedVerifyProgram: SUPSECMAIN_FLAGS_OSX_VM_APP + '%s'", pszProgName);
#endif
    for (unsigned iFile = 0; iFile < RT_ELEMENTS(g_aSupInstallFiles); iFile++)
        if (!suplibHardenedStrNCmp(pszProgName, g_aSupInstallFiles[iFile].pszFile, cchProgNameDll))
        {
            if (   (   g_aSupInstallFiles[iFile].enmType == kSupIFT_Dll
                    || g_aSupInstallFiles[iFile].enmType == kSupIFT_TestDll)
                && !suplibHardenedStrCmp(&g_aSupInstallFiles[iFile].pszFile[cchProgNameDll], SUPLIB_DLL_SUFF))
            {
                /* This only has to be found (once). */
                if (fDll)
                    rc = supR3HardenedError(VERR_INTERNAL_ERROR, fFatal,
                                            "supR3HardenedVerifyProgram: duplicate DLL entry for \"%s\"\n", pszProgName);
                else
                    rc = supR3HardenedVerifyFileInternal(iFile, fFatal, fLeaveOpen,
                                                         true /* fVerifyAll - check sign later, only final process need check it on load. */);
                fDll = true;
            }
            else if (   (   g_aSupInstallFiles[iFile].enmType == kSupIFT_Exe
                         || g_aSupInstallFiles[iFile].enmType == kSupIFT_TestExe)
                     && (   cchProgNameExe == cchProgNameDll
                         || !suplibHardenedStrNCmp(pszProgName, g_aSupInstallFiles[iFile].pszFile, cchProgNameExe))
                     && !suplibHardenedStrCmp(&g_aSupInstallFiles[iFile].pszFile[cchProgNameExe], SUPLIB_EXE_SUFF))
            {
                /* Here we'll have to check that the specific program is the same as the entry. */
                if (fExe)
                    rc = supR3HardenedError(VERR_INTERNAL_ERROR, fFatal,
                                            "supR3HardenedVerifyProgram: duplicate EXE entry for \"%s\"\n", pszProgName);
                else
                    rc = supR3HardenedVerifyFileInternal(iFile, fFatal, fLeaveOpen, false /* fVerifyAll */);
                fExe = true;

                supR3HardenedVerifySameFile(iFile, pszExePath, fFatal);
            }
        }

    /*
     * Check the findings.
     */
    if (RT_SUCCESS(rc))
    {
        if (!fDll && !fExe)
            rc = supR3HardenedError(VERR_NOT_FOUND, fFatal,
                                    "supR3HardenedVerifyProgram: Couldn't find the program \"%s\"\n", pszProgName);
        else if (!fExe)
            rc = supR3HardenedError(VERR_NOT_FOUND, fFatal,
                                    "supR3HardenedVerifyProgram: Couldn't find the EXE entry for \"%s\"\n", pszProgName);
        else if (!fDll)
            rc = supR3HardenedError(VERR_NOT_FOUND, fFatal,
                                    "supR3HardenedVerifyProgram: Couldn't find the DLL entry for \"%s\"\n", pszProgName);
    }
    return rc;
}


/**
 * Verifies all the known files (called from SUPR3HardenedMain).
 *
 * @returns VINF_SUCCESS on success.
 *          On verification failure, an error code will be returned when fFatal is clear,
 *          otherwise the program will be terminated.
 *
 * @param   fFatal              Whether validation failures should be treated as
 *                              fatal (true) or not (false).
 * @param   pszProgName         The program name. This is used to verify that
 *                              both the executable and corresponding
 *                              DLL/DYLIB/SO are valid.
 * @param   pszExePath          The path to the executable.
 * @param   fMainFlags          Flags supplied to SUPR3HardenedMain.
 */
DECLHIDDEN(int) supR3HardenedVerifyAll(bool fFatal, const char *pszProgName, const char *pszExePath, uint32_t fMainFlags)
{
    /*
     * On windows
     */
#if defined(RT_OS_WINDOWS)
    bool fLeaveOpen = true;
#else
    bool fLeaveOpen = false;
#endif

    /*
     * The verify all the files.
     */
    int rc = VINF_SUCCESS;
    for (unsigned iFile = 0; iFile < RT_ELEMENTS(g_aSupInstallFiles); iFile++)
    {
        int rc2 = supR3HardenedVerifyFileInternal(iFile, fFatal, fLeaveOpen, true /* fVerifyAll */);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }

    /*
     * Verify the program name, that is to say, check that it's in the table
     * (thus verified above) and verify the signature on platforms where we
     * sign things.
     */
    int rc2 = supR3HardenedVerifyProgram(pszProgName, pszExePath, fFatal, fLeaveOpen, fMainFlags);
    if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
        rc2 = rc;

    return rc;
}


/**
 * Copies the N messages into the error buffer and returns @a rc.
 *
 * @returns Returns @a rc
 * @param   rc                  The return code.
 * @param   pErrInfo            The error info structure.
 * @param   cMsgs               The number of messages in the ellipsis.
 * @param   ...                 Message parts.
 */
static int supR3HardenedSetErrorN(int rc, PRTERRINFO pErrInfo, unsigned cMsgs, ...)
{
    if (pErrInfo)
    {
        size_t cbErr  = pErrInfo->cbMsg;
        char  *pszErr = pErrInfo->pszMsg;

        va_list va;
        va_start(va, cMsgs);
        while (cMsgs-- > 0 && cbErr > 0)
        {
            const char *pszMsg = va_arg(va,  const char *);
            size_t cchMsg = RT_VALID_PTR(pszMsg) ? suplibHardenedStrLen(pszMsg) : 0;
            if (cchMsg >= cbErr)
                cchMsg = cbErr - 1;
            suplibHardenedMemCopy(pszErr, pszMsg, cchMsg);
            pszErr[cchMsg] = '\0';
            pszErr += cchMsg;
            cbErr -= cchMsg;
        }
        va_end(va);

        pErrInfo->rc      = rc;
        pErrInfo->fFlags |= RTERRINFO_FLAGS_SET;
    }

    return rc;
}


#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX)
/**
 * Copies the four messages into the error buffer and returns @a rc.
 *
 * @returns Returns @a rc
 * @param   rc                  The return code.
 * @param   pErrInfo            The error info structure.
 * @param   pszMsg1             The first message part.
 * @param   pszMsg2             The second message part.
 * @param   pszMsg3             The third message part.
 * @param   pszMsg4             The fourth message part.
 */
static int supR3HardenedSetError4(int rc, PRTERRINFO pErrInfo, const char *pszMsg1,
                                  const char *pszMsg2, const char *pszMsg3, const char *pszMsg4)
{
    return supR3HardenedSetErrorN(rc, pErrInfo, 4, pszMsg1, pszMsg2, pszMsg3, pszMsg4);
}
#endif


/**
 * Copies the three messages into the error buffer and returns @a rc.
 *
 * @returns Returns @a rc
 * @param   rc                  The return code.
 * @param   pErrInfo            The error info structure.
 * @param   pszMsg1             The first message part.
 * @param   pszMsg2             The second message part.
 * @param   pszMsg3             The third message part.
 */
static int supR3HardenedSetError3(int rc, PRTERRINFO pErrInfo, const char *pszMsg1,
                                  const char *pszMsg2, const char *pszMsg3)
{
    return supR3HardenedSetErrorN(rc, pErrInfo, 3, pszMsg1, pszMsg2, pszMsg3);
}


#ifdef SOME_UNUSED_FUNCTION
/**
 * Copies the two messages into the error buffer and returns @a rc.
 *
 * @returns Returns @a rc
 * @param   rc                  The return code.
 * @param   pErrInfo            The error info structure.
 * @param   pszMsg1             The first message part.
 * @param   pszMsg2             The second message part.
 */
static int supR3HardenedSetError2(int rc, PRTERRINFO pErrInfo, const char *pszMsg1,
                                  const char *pszMsg2)
{
    return supR3HardenedSetErrorN(rc, pErrInfo, 2, pszMsg1, pszMsg2);
}
#endif


#ifndef SUP_HARDENED_VERIFY_FOLLOW_SYMLINKS_USE_REALPATH
# if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX)
/**
 * Copies the error message to the error buffer and returns @a rc.
 *
 * @returns Returns @a rc
 * @param   rc                  The return code.
 * @param   pErrInfo            The error info structure.
 * @param   pszMsg              The message.
 */
static int supR3HardenedSetError(int rc, PRTERRINFO pErrInfo, const char *pszMsg)
{
    return supR3HardenedSetErrorN(rc, pErrInfo, 1, pszMsg);
}
# endif
#endif


/**
 * Output from a successfull supR3HardenedVerifyPathSanity call.
 */
typedef struct SUPR3HARDENEDPATHINFO
{
    /** The length of the path in szCopy. */
    uint16_t        cch;
    /** The number of path components. */
    uint16_t        cComponents;
    /** Set if the path ends with slash, indicating that it's a directory
     * reference and not a file reference.  The slash has been removed from
     * the copy. */
    bool            fDirSlash;
    /** The offset where each path component starts, i.e. the char after the
     * slash.  The array has cComponents + 1 entries, where the final one is
     * cch + 1 so that one can always terminate the current component by
     * szPath[aoffComponent[i] - 1] = '\0'. */
    uint16_t        aoffComponents[32+1];
    /** A normalized copy of the path.
     * Reserve some extra space so we can be more relaxed about overflow
     * checks and terminator paddings, especially when recursing. */
    char            szPath[SUPR3HARDENED_MAX_PATH * 2];
} SUPR3HARDENEDPATHINFO;
/** Pointer to a parsed path. */
typedef SUPR3HARDENEDPATHINFO *PSUPR3HARDENEDPATHINFO;


/**
 * Verifies that the path is absolutely sane, it also parses the path.
 *
 * A sane path starts at the root (w/ drive letter on DOS derived systems) and
 * does not have any relative bits (/../) or unnecessary slashes (/bin//ls).
 * Sane paths are less or equal to SUPR3HARDENED_MAX_PATH bytes in length.  UNC
 * paths are not supported.
 *
 * @returns VBox status code.
 * @param   pszPath             The path to check.
 * @param   pErrInfo            The error info structure.
 * @param   pInfo               Where to return a copy of the path along with
 *                              parsing information.
 */
static int supR3HardenedVerifyPathSanity(const char *pszPath, PRTERRINFO pErrInfo, PSUPR3HARDENEDPATHINFO pInfo)
{
    const char *pszSrc = pszPath;
    char       *pszDst = pInfo->szPath;

    /*
     * Check that it's an absolute path and copy the volume/root specifier.
     */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    if (   !RT_C_IS_ALPHA(pszSrc[0])
        || pszSrc[1] != ':'
        || !RTPATH_IS_SLASH(pszSrc[2]))
        return supR3HardenedSetError3(VERR_SUPLIB_PATH_NOT_ABSOLUTE, pErrInfo, "The path is not absolute: '", pszPath, "'");

    *pszDst++ = RT_C_TO_UPPER(pszSrc[0]);
    *pszDst++ = ':';
    *pszDst++ = RTPATH_SLASH;
    pszSrc += 3;

#else
    if (!RTPATH_IS_SLASH(pszSrc[0]))
        return supR3HardenedSetError3(VERR_SUPLIB_PATH_NOT_ABSOLUTE, pErrInfo, "The path is not absolute: '", pszPath, "'");

    *pszDst++ = RTPATH_SLASH;
    pszSrc += 1;
#endif

    /*
     * No path specifying the root or something very shortly thereafter will
     * be approved of.
     */
    if (pszSrc[0] == '\0')
        return supR3HardenedSetError3(VERR_SUPLIB_PATH_IS_ROOT, pErrInfo, "The path is root: '", pszPath, "'");
    if (   pszSrc[1] == '\0'
        || pszSrc[2] == '\0')
        return supR3HardenedSetError3(VERR_SUPLIB_PATH_TOO_SHORT, pErrInfo, "The path is too short: '", pszPath, "'");

#if RTPATH_STYLE == RTPATH_STR_F_STYLE_UNIX
    /*
     * Skip double slashes.
     */
    while (RTPATH_IS_SLASH(*pszSrc))
        pszSrc++;
#else
    /*
     * The root slash should be alone to avoid UNC confusion.
     */
    if (RTPATH_IS_SLASH(pszSrc[0]))
        return supR3HardenedSetError3(VERR_SUPLIB_PATH_NOT_CLEAN, pErrInfo,
                                      "The path is not clean of leading double slashes: '", pszPath, "'");
#endif
    /*
     * Check each component.  No parent references.
     */
    pInfo->cComponents = 0;
    pInfo->fDirSlash   = false;
    while (pszSrc[0])
    {
        /* Sanity checks. */
        if (   pszSrc[0] == '.'
            && pszSrc[1] == '.'
            && RTPATH_IS_SLASH(pszSrc[2]))
            return supR3HardenedSetError3(VERR_SUPLIB_PATH_NOT_ABSOLUTE, pErrInfo,
                                          "The path is not absolute: '", pszPath, "'");

        /* Record the start of the component. */
        if (pInfo->cComponents >= RT_ELEMENTS(pInfo->aoffComponents) - 1)
            return supR3HardenedSetError3(VERR_SUPLIB_PATH_TOO_MANY_COMPONENTS, pErrInfo,
                                          "The path has too many components: '", pszPath, "'");
        pInfo->aoffComponents[pInfo->cComponents++] = pszDst - &pInfo->szPath[0];

        /* Traverse to the end of the component, copying it as we go along. */
        while (pszSrc[0])
        {
            if (RTPATH_IS_SLASH(pszSrc[0]))
            {
                pszSrc++;
                if (*pszSrc)
                    *pszDst++ = RTPATH_SLASH;
                else
                    pInfo->fDirSlash = true;
                break;
            }
            *pszDst++ = *pszSrc++;
            if ((uintptr_t)(pszDst - &pInfo->szPath[0]) >= SUPR3HARDENED_MAX_PATH)
                return supR3HardenedSetError3(VERR_SUPLIB_PATH_TOO_LONG, pErrInfo,
                                              "The path is too long: '", pszPath, "'");
        }

        /* Skip double slashes. */
        while (RTPATH_IS_SLASH(*pszSrc))
            pszSrc++;
    }

    /* Terminate the string and enter its length. */
    pszDst[0] = '\0';
    pszDst[1] = '\0';                   /* for aoffComponents */
    pInfo->cch = (uint16_t)(pszDst - &pInfo->szPath[0]);
    pInfo->aoffComponents[pInfo->cComponents] = pInfo->cch + 1;

    return VINF_SUCCESS;
}


/**
 * The state information collected by supR3HardenedVerifyFsObject.
 *
 * This can be used to verify that a directory we've opened for enumeration is
 * the same as the one that supR3HardenedVerifyFsObject just verified.  It can
 * equally be used to verify a native specfied by the user.
 */
typedef struct SUPR3HARDENEDFSOBJSTATE
{
#ifdef RT_OS_WINDOWS
    /** Not implemented for windows yet. */
    char            chTodo;
#else
    /** The stat output. */
    struct stat     Stat;
#endif
} SUPR3HARDENEDFSOBJSTATE;
/** Pointer to a file system object state. */
typedef SUPR3HARDENEDFSOBJSTATE *PSUPR3HARDENEDFSOBJSTATE;
/** Pointer to a const file system object state. */
typedef SUPR3HARDENEDFSOBJSTATE const *PCSUPR3HARDENEDFSOBJSTATE;


/**
 * Query information about a file system object by path.
 *
 * @returns VBox status code, error buffer filled on failure.
 * @param   pszPath             The path to the object.
 * @param   pFsObjState         Where to return the state information.
 * @param   pErrInfo            The error info structure.
 */
static int supR3HardenedQueryFsObjectByPath(char const *pszPath, PSUPR3HARDENEDFSOBJSTATE pFsObjState, PRTERRINFO pErrInfo)
{
#if defined(RT_OS_WINDOWS)
    /** @todo Windows hardening. */
    pFsObjState->chTodo = 0;
    RT_NOREF2(pszPath, pErrInfo);
    return VINF_SUCCESS;

#else
    /*
     * Stat the object, do not follow links.
     */
    if (lstat(pszPath, &pFsObjState->Stat) != 0)
    {
        /* Ignore access errors */
        if (errno != EACCES)
            return supR3HardenedSetErrorN(VERR_SUPLIB_STAT_FAILED, pErrInfo,
                                          5, "stat failed with ", strerror(errno), " on: '", pszPath, "'");
    }

    /*
     * Read ACLs.
     */
    /** @todo */

    return VINF_SUCCESS;
#endif
}


/**
 * Query information about a file system object by native handle.
 *
 * @returns VBox status code, error buffer filled on failure.
 * @param   hNative             The native handle to the object @a pszPath
 *                              specifies and this should be verified to be the
 *                              same file system object.
 * @param   pFsObjState         Where to return the state information.
 * @param   pszPath             The path to the object. (For the error message
 *                              only.)
 * @param   pErrInfo            The error info structure.
 */
static int supR3HardenedQueryFsObjectByHandle(RTHCUINTPTR hNative, PSUPR3HARDENEDFSOBJSTATE pFsObjState,
                                              char const *pszPath, PRTERRINFO pErrInfo)
{
#if defined(RT_OS_WINDOWS)
    /** @todo Windows hardening. */
    pFsObjState->chTodo = 0;
    RT_NOREF3(hNative, pszPath, pErrInfo);
    return VINF_SUCCESS;

#else
    /*
     * Stat the object, do not follow links.
     */
    if (fstat((int)hNative, &pFsObjState->Stat) != 0)
        return supR3HardenedSetErrorN(VERR_SUPLIB_STAT_FAILED, pErrInfo,
                                      5, "fstat failed with ", strerror(errno), " on '", pszPath, "'");

    /*
     * Read ACLs.
     */
    /** @todo */

    return VINF_SUCCESS;
#endif
}


/**
 * Verifies that the file system object indicated by the native handle is the
 * same as the one @a pFsObjState indicates.
 *
 * @returns VBox status code, error buffer filled on failure.
 * @param   pFsObjState1        File system object information/state by path.
 * @param   pFsObjState2        File system object information/state by handle.
 * @param   pszPath             The path to the object @a pFsObjState
 *                              describes.  (For the error message.)
 * @param   pErrInfo            The error info structure.
 */
static int supR3HardenedIsSameFsObject(PCSUPR3HARDENEDFSOBJSTATE pFsObjState1, PCSUPR3HARDENEDFSOBJSTATE pFsObjState2,
                                       const char *pszPath, PRTERRINFO pErrInfo)
{
#if defined(RT_OS_WINDOWS)
    /** @todo Windows hardening. */
    RT_NOREF4(pFsObjState1, pFsObjState2, pszPath, pErrInfo);
    return VINF_SUCCESS;

#elif defined(RT_OS_OS2)
    RT_NOREF4(pFsObjState1, pFsObjState2, pszPath, pErrInfo);
    return VINF_SUCCESS;

#else
    /*
     * Compare the ino+dev, then the uid+gid and finally the important mode
     * bits.  Technically the first one should be enough, but we're paranoid.
     */
    if (   pFsObjState1->Stat.st_ino != pFsObjState2->Stat.st_ino
        || pFsObjState1->Stat.st_dev != pFsObjState2->Stat.st_dev)
        return supR3HardenedSetError3(VERR_SUPLIB_NOT_SAME_OBJECT, pErrInfo,
                                      "The native handle is not the same as '", pszPath, "' (ino/dev)");
    if (   pFsObjState1->Stat.st_uid != pFsObjState2->Stat.st_uid
        || pFsObjState1->Stat.st_gid != pFsObjState2->Stat.st_gid)
        return supR3HardenedSetError3(VERR_SUPLIB_NOT_SAME_OBJECT, pErrInfo,
                                      "The native handle is not the same as '", pszPath, "' (uid/gid)");
    if (   (pFsObjState1->Stat.st_mode & (S_IFMT | S_IWUSR | S_IWGRP | S_IWOTH))
        != (pFsObjState2->Stat.st_mode & (S_IFMT | S_IWUSR | S_IWGRP | S_IWOTH)))
        return supR3HardenedSetError3(VERR_SUPLIB_NOT_SAME_OBJECT, pErrInfo,
                                      "The native handle is not the same as '", pszPath, "' (mode)");
    return VINF_SUCCESS;
#endif
}


/**
 * Verifies a file system object (file or directory).
 *
 * @returns VBox status code, error buffer filled on failure.
 * @param   pFsObjState         The file system object information/state to be
 *                              verified.
 * @param   fDir                Whether this is a directory or a file.
 * @param   fRelaxed            Whether we can be more relaxed about this
 *                              directory (only used for grand parent
 *                              directories).
 * @param   fSymlinksAllowed    Flag whether symlinks are allowed or not.
 *                              If allowed the symlink object is verified not the target.
 * @param   pszPath             The path to the object. For error messages and
 *                              securing a couple of hacks.
 * @param   pErrInfo            The error info structure.
 */
static int supR3HardenedVerifyFsObject(PCSUPR3HARDENEDFSOBJSTATE pFsObjState, bool fDir, bool fRelaxed,
                                       bool fSymlinksAllowed, const char *pszPath, PRTERRINFO pErrInfo)
{
#if defined(RT_OS_WINDOWS)
    /** @todo Windows hardening. */
    RT_NOREF(pFsObjState, fDir, fRelaxed, fSymlinksAllowed, pszPath, pErrInfo);
    return VINF_SUCCESS;

#elif defined(RT_OS_OS2)
    /* No hardening here - it's a single user system. */
    RT_NOREF(pFsObjState, fDir, fRelaxed, fSymlinksAllowed, pszPath, pErrInfo);
    return VINF_SUCCESS;

#else
    /*
     * The owner must be root.
     *
     * This can be extended to include predefined system users if necessary.
     */
    if (pFsObjState->Stat.st_uid != 0)
        return supR3HardenedSetError3(VERR_SUPLIB_OWNER_NOT_ROOT, pErrInfo, "The owner is not root: '", pszPath, "'");

    /*
     * The object type must be directory or file. It can be a symbolic link
     * if explicitely allowed. Otherwise this and other risky stuff is not allowed
     * (sorry dude, but we're paranoid on purpose here).
     */
    if (   !S_ISLNK(pFsObjState->Stat.st_mode)
        || !fSymlinksAllowed)
    {

        if (   !S_ISDIR(pFsObjState->Stat.st_mode)
            && !S_ISREG(pFsObjState->Stat.st_mode))
        {
            if (S_ISLNK(pFsObjState->Stat.st_mode))
                return supR3HardenedSetError3(VERR_SUPLIB_SYMLINKS_ARE_NOT_PERMITTED, pErrInfo,
                                              "Symlinks are not permitted: '", pszPath, "'");
            return supR3HardenedSetError3(VERR_SUPLIB_NOT_DIR_NOT_FILE, pErrInfo,
                                          "Not regular file or directory: '", pszPath, "'");
        }
        if (fDir != !!S_ISDIR(pFsObjState->Stat.st_mode))
        {
            if (S_ISDIR(pFsObjState->Stat.st_mode))
                return supR3HardenedSetError3(VERR_SUPLIB_IS_DIRECTORY, pErrInfo,
                                              "Expected file but found directory: '", pszPath, "'");
            return supR3HardenedSetError3(VERR_SUPLIB_IS_FILE, pErrInfo,
                                          "Expected directory but found file: '", pszPath, "'");
        }
    }

    /*
     * The group does not matter if it does not have write access, if it has
     * write access it must be group 0 (root/wheel/whatever).
     *
     * This can be extended to include predefined system groups or groups that
     * only root is member of.
     */
    if (   (pFsObjState->Stat.st_mode & S_IWGRP)
        && pFsObjState->Stat.st_gid != 0)
    {
# ifdef RT_OS_DARWIN
        /* HACK ALERT: On Darwin /Applications is root:admin with admin having
           full access. So, to work around we relax the hardening a bit and
           permit grand parents and beyond to be group writable by admin. */
        /** @todo dynamically resolve the admin group? */
        bool fBad = !fRelaxed || pFsObjState->Stat.st_gid != 80 /*admin*/ || suplibHardenedStrCmp(pszPath, "/Applications");

# elif defined(RT_OS_FREEBSD)
        /* HACK ALERT: PC-BSD 9 has group-writable /usr/pib directory which is
           similar to /Applications on OS X (see above).
           On FreeBSD root is normally the only member of this group, on
           PC-BSD the default user is a member. */
        /** @todo dynamically resolve the operator group? */
        bool fBad = !fRelaxed || pFsObjState->Stat.st_gid != 5 /*operator*/ || suplibHardenedStrCmp(pszPath, "/usr/pbi");
        NOREF(fRelaxed);
# elif defined(RT_OS_SOLARIS)
        /* HACK ALERT: Solaris has group-writable /usr/lib/iconv directory from
           which the appropriate module is loaded.
           By default only root and daemon are part of that group.
           . */
        /** @todo dynamically resolve the bin group? */
        bool fBad = !fRelaxed || pFsObjState->Stat.st_gid != 2 /*bin*/ || suplibHardenedStrCmp(pszPath, "/usr/lib/iconv");
# else
        NOREF(fRelaxed);
        bool fBad = true;
# endif
        if (fBad)
            return supR3HardenedSetError3(VERR_SUPLIB_WRITE_NON_SYS_GROUP, pErrInfo,
                                          "An unknown (and thus untrusted) group has write access to '", pszPath,
                                          "' and we therefore cannot trust the directory content or that of any subdirectory");
    }

    /*
     * World must not have write access.  There is no relaxing this rule.
     * Linux exception: Symbolic links are always give permission 0777, there
     *                  is no lchmod or lchown APIs.  The permissions on parent
     *                  directory that contains the symbolic link is what is
     *                  decising wrt to modifying it.  (Caller is expected not
     *                  to allow symbolic links in the first path component.)
     */
    if (   (pFsObjState->Stat.st_mode & S_IWOTH)
# ifdef RT_OS_LINUX
        && (   !S_ISLNK(pFsObjState->Stat.st_mode)
            || !fSymlinksAllowed /* paranoia */)
# endif
       )
        return supR3HardenedSetError3(VERR_SUPLIB_WORLD_WRITABLE, pErrInfo,
                                      "World writable: '", pszPath, "'");

    /*
     * Check the ACLs.
     */
    /** @todo */

    return VINF_SUCCESS;
#endif
}


/**
 * Verifies that the file system object indicated by the native handle is the
 * same as the one @a pFsObjState indicates.
 *
 * @returns VBox status code, error buffer filled on failure.
 * @param   hNative             The native handle to the object @a pszPath
 *                              specifies and this should be verified to be the
 *                              same file system object.
 * @param   pFsObjState         The information/state returned by a previous
 *                              query call.
 * @param   pszPath             The path to the object @a pFsObjState
 *                              describes.  (For the error message.)
 * @param   pErrInfo            The error info structure.
 */
static int supR3HardenedVerifySameFsObject(RTHCUINTPTR hNative, PCSUPR3HARDENEDFSOBJSTATE pFsObjState,
                                           const char *pszPath, PRTERRINFO pErrInfo)
{
    SUPR3HARDENEDFSOBJSTATE FsObjState2;
    int rc = supR3HardenedQueryFsObjectByHandle(hNative, &FsObjState2, pszPath, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = supR3HardenedIsSameFsObject(pFsObjState, &FsObjState2, pszPath, pErrInfo);
    return rc;
}


/**
 * Does the recursive directory enumeration.
 *
 * @returns VBox status code, error buffer filled on failure.
 * @param   pszDirPath          The path buffer containing the subdirectory to
 *                              enumerate followed by a slash (this is never
 *                              the root slash).  The buffer is RTPATH_MAX in
 *                              size and anything starting at @a cchDirPath
 *                              - 1 and beyond is scratch space.
 * @param   cchDirPath          The length of the directory path + slash.
 * @param   pFsObjState         Pointer to the file system object state buffer.
 *                              On input this will hold the stats for
 *                              the directory @a pszDirPath indicates and will
 *                              be used to verified that we're opening the same
 *                              thing.
 * @param   fRecursive          Whether to recurse into subdirectories.
 * @param   pErrInfo            The error info structure.
 */
static int supR3HardenedVerifyDirRecursive(char *pszDirPath, size_t cchDirPath, PSUPR3HARDENEDFSOBJSTATE pFsObjState,
                                           bool fRecursive, PRTERRINFO pErrInfo)
{
#if defined(RT_OS_WINDOWS)
    /** @todo Windows hardening. */
    RT_NOREF5(pszDirPath, cchDirPath, pFsObjState, fRecursive, pErrInfo);
    return VINF_SUCCESS;

#elif defined(RT_OS_OS2)
    /* No hardening here - it's a single user system. */
    RT_NOREF5(pszDirPath, cchDirPath, pFsObjState, fRecursive, pErrInfo);
    return VINF_SUCCESS;

#else
    /*
     * Open the directory.  Now, we could probably eliminate opendir here
     * and go down on kernel API level (open + getdents for instance), however
     * that's not very portable and hopefully not necessary.
     */
    DIR *pDir = opendir(pszDirPath);
    if (!pDir)
    {
        /* Ignore access errors. */
        if (errno == EACCES)
            return VINF_SUCCESS;
        return supR3HardenedSetErrorN(VERR_SUPLIB_DIR_ENUM_FAILED, pErrInfo,
                                      5, "opendir failed with ", strerror(errno), " on '", pszDirPath, "'");
    }
    if (dirfd(pDir) != -1)
    {
        int rc = supR3HardenedVerifySameFsObject(dirfd(pDir), pFsObjState, pszDirPath, pErrInfo);
        if (RT_FAILURE(rc))
        {
            closedir(pDir);
            return rc;
        }
    }

    /*
     * Enumerate the directory, check all the requested bits.
     */
    int rc = VINF_SUCCESS;
    for (;;)
    {
        pszDirPath[cchDirPath] = '\0';  /* for error messages. */

        struct dirent Entry;
        struct dirent *pEntry;
#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        int iErr = readdir_r(pDir, &Entry, &pEntry);
#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic pop
#endif
        if (iErr)
        {
            rc = supR3HardenedSetErrorN(VERR_SUPLIB_DIR_ENUM_FAILED, pErrInfo,
                                        5, "readdir_r failed with ", strerror(iErr), " in '", pszDirPath, "'");
            break;
        }
        if (!pEntry)
            break;

        /*
         * Check the length and copy it into the path buffer so it can be
         * stat()'ed.
         */
        size_t cchName = suplibHardenedStrLen(pEntry->d_name);
        if (cchName + cchDirPath > SUPR3HARDENED_MAX_PATH)
        {
            rc = supR3HardenedSetErrorN(VERR_SUPLIB_PATH_TOO_LONG, pErrInfo,
                                        4, "Path grew too long during recursion: '", pszDirPath, pEntry->d_name, "'");
            break;
        }
        suplibHardenedMemCopy(&pszDirPath[cchDirPath], pEntry->d_name, cchName + 1);

        /*
         * Query the information about the entry and verify it.
         * (We don't bother skipping '.' and '..' at this point, a little bit
         * of extra checks doesn't hurt and neither requires relaxed handling.)
         */
        rc = supR3HardenedQueryFsObjectByPath(pszDirPath, pFsObjState, pErrInfo);
        if (RT_SUCCESS(rc))
            break;
        rc = supR3HardenedVerifyFsObject(pFsObjState, S_ISDIR(pFsObjState->Stat.st_mode), false /*fRelaxed*/,
                                         false /*fSymlinksAllowed*/, pszDirPath, pErrInfo);
        if (RT_FAILURE(rc))
            break;

        /*
         * Recurse into subdirectories if requested.
         */
        if (    fRecursive
            &&  S_ISDIR(pFsObjState->Stat.st_mode)
            &&  suplibHardenedStrCmp(pEntry->d_name, ".")
            &&  suplibHardenedStrCmp(pEntry->d_name, ".."))
        {
            pszDirPath[cchDirPath + cchName]     = RTPATH_SLASH;
            pszDirPath[cchDirPath + cchName + 1] = '\0';

            rc = supR3HardenedVerifyDirRecursive(pszDirPath, cchDirPath + cchName + 1, pFsObjState,
                                                 fRecursive, pErrInfo);
            if (RT_FAILURE(rc))
                break;
        }
    }

    closedir(pDir);
    return rc;
#endif
}


/**
 * Worker for SUPR3HardenedVerifyDir.
 *
 * @returns See SUPR3HardenedVerifyDir.
 * @param   pszDirPath          See SUPR3HardenedVerifyDir.
 * @param   fRecursive          See SUPR3HardenedVerifyDir.
 * @param   fCheckFiles         See SUPR3HardenedVerifyDir.
 * @param   pErrInfo            See SUPR3HardenedVerifyDir.
 */
DECLHIDDEN(int) supR3HardenedVerifyDir(const char *pszDirPath, bool fRecursive, bool fCheckFiles, PRTERRINFO pErrInfo)
{
    /*
     * Validate the input path and parse it.
     */
    SUPR3HARDENEDPATHINFO Info;
    int rc = supR3HardenedVerifyPathSanity(pszDirPath, pErrInfo, &Info);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Verify each component from the root up.
     */
    SUPR3HARDENEDFSOBJSTATE FsObjState;
    uint32_t const          cComponents = Info.cComponents;
    for (uint32_t iComponent = 0; iComponent < cComponents; iComponent++)
    {
        bool fRelaxed = iComponent + 2 < cComponents;
        Info.szPath[Info.aoffComponents[iComponent + 1] - 1] = '\0';
        rc = supR3HardenedQueryFsObjectByPath(Info.szPath, &FsObjState, pErrInfo);
        if (RT_SUCCESS(rc))
            rc = supR3HardenedVerifyFsObject(&FsObjState, true /*fDir*/, fRelaxed,
                                             false /*fSymlinksAllowed*/, Info.szPath, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
        Info.szPath[Info.aoffComponents[iComponent + 1] - 1] = iComponent + 1 != cComponents ? RTPATH_SLASH : '\0';
    }

    /*
     * Check files and subdirectories if requested.
     */
    if (fCheckFiles || fRecursive)
    {
        Info.szPath[Info.cch]     = RTPATH_SLASH;
        Info.szPath[Info.cch + 1] = '\0';
        return supR3HardenedVerifyDirRecursive(Info.szPath, Info.cch + 1, &FsObjState,
                                               fRecursive, pErrInfo);
    }

    return VINF_SUCCESS;
}


/**
 * Verfies a file.
 *
 * @returns VBox status code, error buffer filled on failure.
 * @param   pszFilename         The file to verify.
 * @param   hNativeFile         Handle to the file, verify that it's the same
 *                              as we ended up with when verifying the path.
 *                              RTHCUINTPTR_MAX means NIL here.
 * @param   fMaybe3rdParty      Set if the file is could be a supplied by a
 *                              third party.  Different validation rules may
 *                              apply to 3rd party code on some platforms.
 * @param   pErrInfo            Where to return extended error information.
 *                              Optional.
 */
DECLHIDDEN(int) supR3HardenedVerifyFile(const char *pszFilename, RTHCUINTPTR hNativeFile,
                                        bool fMaybe3rdParty, PRTERRINFO pErrInfo)
{
    /*
     * Validate the input path and parse it.
     */
    SUPR3HARDENEDPATHINFO Info;
    int rc = supR3HardenedVerifyPathSanity(pszFilename, pErrInfo, &Info);
    if (RT_FAILURE(rc))
        return rc;
    if (Info.fDirSlash)
        return supR3HardenedSetError3(VERR_SUPLIB_IS_DIRECTORY, pErrInfo,
                                      "The file path specifies a directory: '", pszFilename, "'");

    /*
     * Verify each component from the root up.
     */
    SUPR3HARDENEDFSOBJSTATE FsObjState;
    uint32_t const          cComponents = Info.cComponents;
    for (uint32_t iComponent = 0; iComponent < cComponents; iComponent++)
    {
        bool fFinal   = iComponent + 1 == cComponents;
        bool fRelaxed = iComponent + 2 < cComponents;
        Info.szPath[Info.aoffComponents[iComponent + 1] - 1] = '\0';
        rc = supR3HardenedQueryFsObjectByPath(Info.szPath, &FsObjState, pErrInfo);
        if (RT_SUCCESS(rc))
            rc = supR3HardenedVerifyFsObject(&FsObjState, !fFinal /*fDir*/, fRelaxed,
                                             false /*fSymlinksAllowed*/, Info.szPath, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
        Info.szPath[Info.aoffComponents[iComponent + 1] - 1] = !fFinal ? RTPATH_SLASH : '\0';
    }

    /*
     * Verify the file handle against the last component, if specified.
     */
    if (hNativeFile != RTHCUINTPTR_MAX)
    {
        rc = supR3HardenedVerifySameFsObject(hNativeFile, &FsObjState, Info.szPath, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
    }

#ifdef RT_OS_WINDOWS
    /*
     * The files shall be signed on windows, verify that.
     */
    rc = VINF_SUCCESS;
    HANDLE hVerify;
    if (hNativeFile == RTHCUINTPTR_MAX)
    {
        PRTUTF16 pwszPath;
        rc = RTStrToUtf16(pszFilename, &pwszPath);
        if (RT_SUCCESS(rc))
        {
            hVerify = CreateFileW(pwszPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            RTUtf16Free(pwszPath);
        }
        else
        {
            rc = RTErrInfoSetF(pErrInfo, rc, "Error converting '%s' to UTF-16: %Rrc", pszFilename, rc);
            hVerify = INVALID_HANDLE_VALUE;
        }
    }
    else
    {
        NTSTATUS rcNt = NtDuplicateObject(NtCurrentProcess(), (HANDLE)hNativeFile, NtCurrentProcess(), &hVerify,
                                          GENERIC_READ, 0 /*HandleAttributes*/, 0 /*Options*/);
        if (!NT_SUCCESS(rcNt))
            hVerify = INVALID_HANDLE_VALUE;
    }
    if (hVerify != INVALID_HANDLE_VALUE)
    {
# ifdef VBOX_WITH_HARDENING
        uint32_t fFlags = SUPHNTVI_F_REQUIRE_KERNEL_CODE_SIGNING;
        if (!fMaybe3rdParty)
            fFlags = SUPHNTVI_F_REQUIRE_BUILD_CERT;
        const char *pszSuffix = RTPathSuffix(pszFilename);
        if (   pszSuffix
            &&                   pszSuffix[0]  == '.'
            && (   RT_C_TO_LOWER(pszSuffix[1]) == 'r'
                || RT_C_TO_LOWER(pszSuffix[1]) == 'g')
            &&     RT_C_TO_LOWER(pszSuffix[2]) == 'c'
            &&                   pszSuffix[3]  == '\0' )
            fFlags |= SUPHNTVI_F_RC_IMAGE;
#  ifndef IN_SUP_R3_STATIC /* Not in VBoxCpuReport and friends. */
        rc = supHardenedWinVerifyImageByHandleNoName(hVerify, fFlags, pErrInfo);
#  endif
# else
        RT_NOREF1(fMaybe3rdParty);
# endif
        NtClose(hVerify);
    }
    else if (RT_SUCCESS(rc))
        rc = RTErrInfoSetF(pErrInfo, RTErrConvertFromWin32(RtlGetLastWin32Error()),
                           "Error %u trying to open (or duplicate handle for) '%s'", RtlGetLastWin32Error(), pszFilename);
    if (RT_FAILURE(rc))
        return rc;
#else
    RT_NOREF1(fMaybe3rdParty);
#endif

    return VINF_SUCCESS;
}


#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX)
/**
 * Verfies a file following symlinks.
 *
 * @returns VBox status code, error buffer filled on failure.
 * @param   pszFilename         The file to verify.
 * @param   hNativeFile         Handle to the file, verify that it's the same
 *                              as we ended up with when verifying the path.
 *                              RTHCUINTPTR_MAX means NIL here.
 * @param   fMaybe3rdParty      Set if the file is could be a supplied by a
 *                              third party.  Different validation rules may
 *                              apply to 3rd party code on some platforms.
 * @param   pErrInfo            Where to return extended error information.
 *                              Optional.
 *
 * @note    This is only used on OS X for libraries loaded with dlopen() because
 *          the frameworks use symbolic links to point to the relevant library.
 *
 * @sa      supR3HardenedVerifyFile
 */
DECLHIDDEN(int) supR3HardenedVerifyFileFollowSymlinks(const char *pszFilename, RTHCUINTPTR hNativeFile, bool fMaybe3rdParty,
                                                      PRTERRINFO pErrInfo)
{
    RT_NOREF1(fMaybe3rdParty);

    /*
     * Validate the input path and parse it.
     */
    SUPR3HARDENEDPATHINFO Info;
    int rc = supR3HardenedVerifyPathSanity(pszFilename, pErrInfo, &Info);
    if (RT_FAILURE(rc))
        return rc;
    if (Info.fDirSlash)
        return supR3HardenedSetError3(VERR_SUPLIB_IS_DIRECTORY, pErrInfo,
                                      "The file path specifies a directory: '", pszFilename, "'");

    /*
     * Verify each component from the root up.
     */
#ifndef SUP_HARDENED_VERIFY_FOLLOW_SYMLINKS_USE_REALPATH
    uint32_t                iLoops = 0;
#endif
    SUPR3HARDENEDFSOBJSTATE FsObjState;
    uint32_t                iComponent = 0;
    while (iComponent < Info.cComponents)
    {
        bool fFinal   = iComponent + 1 == Info.cComponents;
        bool fRelaxed = iComponent + 2 < Info.cComponents;
        Info.szPath[Info.aoffComponents[iComponent + 1] - 1] = '\0';
        rc = supR3HardenedQueryFsObjectByPath(Info.szPath, &FsObjState, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            /*
             * In case the component is a symlink expand it and start from the beginning after
             * verifying it has the proper access rights.
             * Furthermore only allow symlinks which don't contain any .. or . in the target
             * (enforced by supR3HardenedVerifyPathSanity).
             */
            rc = supR3HardenedVerifyFsObject(&FsObjState, !fFinal /*fDir*/, fRelaxed,
                                             true /*fSymlinksAllowed*/, Info.szPath, pErrInfo);
            if (   RT_SUCCESS(rc)
                && S_ISLNK(FsObjState.Stat.st_mode))
            {
#if SUP_HARDENED_VERIFY_FOLLOW_SYMLINKS_USE_REALPATH /* Another approach using realpath() and verifying the result when encountering a symlink. */
                char *pszFilenameResolved = realpath(pszFilename, NULL);
                if (pszFilenameResolved)
                {
                    rc = supR3HardenedVerifyFile(pszFilenameResolved, hNativeFile, fMaybe3rdParty, pErrInfo);
                    free(pszFilenameResolved);
                    return rc;
                }
                else
                {
                    int iErr = errno;
                    supR3HardenedError(VERR_ACCESS_DENIED, false /*fFatal*/,
                                       "supR3HardenedVerifyFileFollowSymlinks: Failed to resolve the real path '%s': %s (%d)\n",
                                       pszFilename, strerror(iErr), iErr);
                    return supR3HardenedSetError4(VERR_ACCESS_DENIED, pErrInfo,
                                                  "realpath failed for '", pszFilename, "': ", strerror(iErr));
                }
#else
                /* Don't loop forever. */
                iLoops++;
                if (iLoops < 8)
                {
                    /*
                     * Construct new path by replacing the current component by the symlink value.
                     * Note! readlink() is a weird API that doesn't necessarily indicates if the
                     *       buffer is too small.
                     */
                    char   szPath[RTPATH_MAX];
                    size_t const cchBefore = Info.aoffComponents[iComponent]; /* includes slash */
                    size_t const cchAfter  = fFinal ? 0 : 1 /*slash*/ + Info.cch - Info.aoffComponents[iComponent + 1];
                    if (sizeof(szPath) > cchBefore + cchAfter + 2)
                    {
                        ssize_t cchTarget = readlink(Info.szPath, szPath, sizeof(szPath) - 1);
                        if (cchTarget > 0)
                        {
                            /* Some serious paranoia against embedded zero terminator and weird return values. */
                            szPath[cchTarget] = '\0';
                            size_t cchLink = strlen(szPath);

                            /* Strip trailing dirslashes of non-final link. */
                            if (!fFinal)
                                while (cchLink > 1 and szPath[cchLink - 1] == '/')
                                    cchLink--;

                            /* Check link value sanity and buffer size. */
                            if (cchLink == 0)
                                return supR3HardenedSetError3(VERR_ACCESS_DENIED, pErrInfo,
                                                              "Bad readlink return for '", Info.szPath, "'");
                            if (szPath[0] == '/')
                                return supR3HardenedSetError3(VERR_ACCESS_DENIED, pErrInfo,
                                                              "Absolute symbolic link not allowed: '", szPath, "'");
                            if (cchBefore + cchLink + cchAfter + 1 /*terminator*/ > sizeof(szPath))
                                return supR3HardenedSetError(VERR_SUPLIB_PATH_TOO_LONG, pErrInfo,
                                                             "Symlinks causing too long path!");

                            /* Construct the new path. */
                            if (cchBefore)
                                memmove(&szPath[cchBefore], &szPath[0], cchLink);
                            memcpy(&szPath[0], Info.szPath, cchBefore);
                            if (!cchAfter)
                                szPath[cchBefore + cchLink] = '\0';
                            else
                            {
                                szPath[cchBefore + cchLink] = RTPATH_SLASH;
                                memcpy(&szPath[cchBefore + cchLink + 1],
                                       &Info.szPath[Info.aoffComponents[iComponent + 1]],
                                       cchAfter); /* cchAfter includes a zero terminator */
                            }

                            /* Parse, copy and check the sanity (no '..' or '.') of the altered path. */
                            rc = supR3HardenedVerifyPathSanity(szPath, pErrInfo, &Info);
                            if (RT_FAILURE(rc))
                                return rc;
                            if (Info.fDirSlash)
                                return supR3HardenedSetError3(VERR_SUPLIB_IS_DIRECTORY, pErrInfo,
                                                              "The file path specifies a directory: '", szPath, "'");

                            /* Restart from the current component. */
                            continue;
                        }
                        int iErr = errno;
                        supR3HardenedError(VERR_ACCESS_DENIED, false /*fFatal*/,
                                           "supR3HardenedVerifyFileFollowSymlinks: Failed to readlink '%s': %s (%d)\n",
                                           Info.szPath, strerror(iErr), iErr);
                        return supR3HardenedSetError4(VERR_ACCESS_DENIED, pErrInfo,
                                                      "readlink failed for '", Info.szPath, "': ", strerror(iErr));
                    }
                    return supR3HardenedSetError(VERR_SUPLIB_PATH_TOO_LONG, pErrInfo, "Path too long for symlink replacing!");
                }
                else
                    return supR3HardenedSetError3(VERR_TOO_MANY_SYMLINKS, pErrInfo,
                                                  "Too many symbolic links: '", pszFilename, "'");
#endif
            }
        }
        if (RT_FAILURE(rc))
            return rc;
        Info.szPath[Info.aoffComponents[iComponent + 1] - 1] = !fFinal ? RTPATH_SLASH : '\0';
        iComponent++;
    }

    /*
     * Verify the file handle against the last component, if specified.
     */
    if (hNativeFile != RTHCUINTPTR_MAX)
    {
        rc = supR3HardenedVerifySameFsObject(hNativeFile, &FsObjState, Info.szPath, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}
#endif /* RT_OS_DARWIN || RT_OS_LINUX */


/**
 * Gets the pre-init data for the hand-over to the other version
 * of this code.
 *
 * The reason why we pass this information on is that it contains
 * open directories and files. Later it may include even more info
 * (int the verified arrays mostly).
 *
 * The receiver is supR3HardenedRecvPreInitData.
 *
 * @param   pPreInitData    Where to store it.
 */
DECLHIDDEN(void) supR3HardenedGetPreInitData(PSUPPREINITDATA pPreInitData)
{
    pPreInitData->cInstallFiles = RT_ELEMENTS(g_aSupInstallFiles);
    pPreInitData->paInstallFiles = &g_aSupInstallFiles[0];
    pPreInitData->paVerifiedFiles = &g_aSupVerifiedFiles[0];

    pPreInitData->cVerifiedDirs = RT_ELEMENTS(g_aSupVerifiedDirs);
    pPreInitData->paVerifiedDirs = &g_aSupVerifiedDirs[0];
}


/**
 * Receives the pre-init data from the static executable stub.
 *
 * @returns VBox status code. Will not bitch on failure since the
 *          runtime isn't ready for it, so that is left to the exe stub.
 *
 * @param   pPreInitData    The hand-over data.
 */
DECLHIDDEN(int) supR3HardenedRecvPreInitData(PCSUPPREINITDATA pPreInitData)
{
    /*
     * Compare the array lengths and the contents of g_aSupInstallFiles.
     */
    if (    pPreInitData->cInstallFiles != RT_ELEMENTS(g_aSupInstallFiles)
        ||  pPreInitData->cVerifiedDirs != RT_ELEMENTS(g_aSupVerifiedDirs))
        return VERR_VERSION_MISMATCH;
    SUPINSTFILE const *paInstallFiles = pPreInitData->paInstallFiles;
    for (unsigned iFile = 0; iFile < RT_ELEMENTS(g_aSupInstallFiles); iFile++)
        if (    g_aSupInstallFiles[iFile].enmDir    != paInstallFiles[iFile].enmDir
            ||  g_aSupInstallFiles[iFile].enmType   != paInstallFiles[iFile].enmType
            ||  g_aSupInstallFiles[iFile].fOptional != paInstallFiles[iFile].fOptional
            ||  suplibHardenedStrCmp(g_aSupInstallFiles[iFile].pszFile, paInstallFiles[iFile].pszFile))
            return VERR_VERSION_MISMATCH;

    /*
     * Check that we're not called out of order.
     * If dynamic linking it screwed up, we may end up here.
     */
    if (   !ASMMemIsZero(&g_aSupVerifiedFiles[0], sizeof(g_aSupVerifiedFiles))
        || !ASMMemIsZero(&g_aSupVerifiedDirs[0], sizeof(g_aSupVerifiedDirs)))
        return VERR_WRONG_ORDER;

    /*
     * Copy the verification data over.
     */
    suplibHardenedMemCopy(&g_aSupVerifiedFiles[0], pPreInitData->paVerifiedFiles, sizeof(g_aSupVerifiedFiles));
    suplibHardenedMemCopy(&g_aSupVerifiedDirs[0], pPreInitData->paVerifiedDirs, sizeof(g_aSupVerifiedDirs));
    return VINF_SUCCESS;
}
