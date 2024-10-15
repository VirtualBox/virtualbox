/* $Id: MachineLaunchVMCommonWorker.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - VM process launcher helper for VBoxSVC & VBoxSDS.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include "MachineLaunchVMCommonWorker.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# define HOSTSUFF_EXE ".exe"
#else
# define HOSTSUFF_EXE ""
#endif


/**
 * Launch a VM process.
 *
 * The function starts the new VM process. It is a caller's responsibility
 * to make any checks before and after calling the function.
 * The function is a part of both VBoxSVC and VBoxSDS, so any calls to IVirtualBox
 * and IMachine interfaces are performed using the client API.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when new VM process started.
 * @retval  VERR_INVALID_PARAMETER when either aMachine is not Machine interface
 *          or invalid aFrontend is specified.  Hmm. Come to think of it, it
 *          could also be returned in some other cases, especially if the code
 *          is buggy, so I wouldn't rely on any exact meaning here!
 * @retval  VERR_INTERNAL_ERROR when something wrong.
 *
 * @param   aNameOrId            The Machine name or id interface the VM will start for.
 * @param   aComment             The comment for new VM process.
 * @param   aFrontend            The desired frontend for started VM.
 * @param   aEnvironmentChanges  Additional environment variables in the putenv style
 *                               (VAR=VAL for setting, VAR for unsetting) for new VM process.
 * @param   aExtraArg            Extra argument for the VM process.  Ignored if
 *                               empty string.
 * @param   aFilename            Start new VM using specified filename. Only filename
 *                               without path is allowed. Default filename is used if
 *                               empty.
 * @param   aFlags               Flags for RTProcCreateEx functions family if
 *                               required (RTPROC_FLAGS_XXX).
 * @param   aExtraData           Additional data for RTProcCreateX functions family
 *                               if required.  Content is defined by the flags.
 * @param   aPid                 The PID of created process is returned here
 */
int MachineLaunchVMCommonWorker(const Utf8Str &aNameOrId,
                                const Utf8Str &aComment,
                                const Utf8Str &aFrontend,
                                const std::vector<com::Utf8Str> &aEnvironmentChanges,
                                const Utf8Str &aExtraArg,
                                const Utf8Str &aFilename,
                                uint32_t      aFlags,
                                void         *aExtraData,
                                RTPROCESS     &aPid)
{
    NOREF(aNameOrId);
    NOREF(aComment);
    NOREF(aFlags);
    NOREF(aExtraData);
    NOREF(aExtraArg);
    NOREF(aFilename);

    /* Get the path to the executable directory w/ trailing slash: */
    char szPath[RTPATH_MAX];
    int vrc = RTPathAppPrivateArch(szPath, sizeof(szPath));
    AssertRCReturn(vrc, vrc);
    size_t cbBufLeft = RTPathEnsureTrailingSeparator(szPath, sizeof(szPath));
    AssertReturn(cbBufLeft > 0, VERR_FILENAME_TOO_LONG);
    char *pszNamePart = &szPath[cbBufLeft]; NOREF(pszNamePart);
    cbBufLeft = sizeof(szPath) - cbBufLeft;

    /* The process started when launching a VM with separate UI/VM processes is always
     * the UI process, i.e. needs special handling as it won't claim the session. */
    bool fSeparate = aFrontend.endsWith("separate", Utf8Str::CaseInsensitive); NOREF(fSeparate);

    aPid = NIL_RTPROCESS;

    RTENV hEnv = RTENV_DEFAULT;
    if (!aEnvironmentChanges.empty())
    {
#ifdef IN_VBOXSVC
        /* VBoxSVC: clone the current environment */
        vrc = RTEnvClone(&hEnv, RTENV_DEFAULT);
#else
        /* VBoxSDS: Create a change record environment since RTProcCreateEx has to
                    build the final environment from the profile of the VBoxSDS caller. */
        aFlags |= RTPROC_FLAGS_ENV_CHANGE_RECORD;
        vrc = RTEnvCreateChangeRecord(&hEnv);
#endif
        AssertRCReturn(vrc, vrc);

        /* Apply the specified environment changes. */
        for (std::vector<com::Utf8Str>::const_iterator itEnv = aEnvironmentChanges.begin();
             itEnv != aEnvironmentChanges.end();
             ++itEnv)
        {
            vrc = RTEnvPutEx(hEnv, itEnv->c_str());
            AssertRCReturnStmt(vrc, RTEnvDestroy(hEnv), vrc);
        }
    }

#ifdef VBOX_WITH_QTGUI
    if (   !aFrontend.compare("gui", Utf8Str::CaseInsensitive)
        || !aFrontend.compare("GUI/Qt", Utf8Str::CaseInsensitive)
        || !aFrontend.compare("separate", Utf8Str::CaseInsensitive)
        || !aFrontend.compare("gui/separate", Utf8Str::CaseInsensitive)
        || !aFrontend.compare("GUI/Qt/separate", Utf8Str::CaseInsensitive))
    {
# ifdef RT_OS_DARWIN /* Avoid Launch Services confusing this with the selector by using a helper app. */

#  define OSX_APP_NAME           "VirtualBoxVM"
#  define OSX_APP_PATH_FMT       "/Resources/%s.app/Contents/MacOS/VirtualBoxVM"
#  define OSX_APP_PATH_WITH_NAME "/Resources/VirtualBoxVM.app/Contents/MacOS/VirtualBoxVM"

        /* Modify the base path so that we don't need to use ".." below. */
        RTPathStripTrailingSlash(szPath);
        RTPathStripFilename(szPath);
        cbBufLeft = strlen(szPath);
        pszNamePart = &szPath[cbBufLeft]; Assert(!*pszNamePart);
        cbBufLeft = sizeof(szPath) - cbBufLeft;

        if (aFilename.isNotEmpty() && strpbrk(aFilename.c_str(), "./\\:") == NULL)
        {
            ssize_t cch = RTStrPrintf2(pszNamePart, cbBufLeft, OSX_APP_PATH_FMT, aFilename.c_str());
            AssertReturn(cch > 0, VERR_FILENAME_TOO_LONG);
            /* there is a race, but people using this deserve the failure */
            if (!RTFileExists(szPath))
                *pszNamePart = '\0';
        }
        if (!*pszNamePart)
        {
            vrc = RTStrCopy(pszNamePart, cbBufLeft, OSX_APP_PATH_WITH_NAME);
            AssertRCReturn(vrc, vrc);
        }
# else
        static const char s_szVirtualBox_exe[] = "VirtualBoxVM" HOSTSUFF_EXE;
        vrc = RTStrCopy(pszNamePart, cbBufLeft, s_szVirtualBox_exe);
        AssertRCReturn(vrc, vrc);
# endif

        const char *apszArgs[] =
        {
            szPath,
            "--comment", aComment.c_str(),
            "--startvm", aNameOrId.c_str(),
            "--no-startvm-errormsgbox",
            NULL, /* For "--separate". */
            NULL, /* For "--sup-startup-log". */
            NULL
        };
        unsigned iArg = 6;
        if (fSeparate)
            apszArgs[iArg++] = "--separate";
        if (aExtraArg.isNotEmpty())
            apszArgs[iArg++] = aExtraArg.c_str();

        vrc = RTProcCreateEx(szPath, apszArgs, hEnv, aFlags, NULL, NULL, NULL, NULL, NULL, aExtraData, &aPid);
    }
#else /* !VBOX_WITH_QTGUI */
    if (0)
        ;
#endif /* VBOX_WITH_QTGUI */

    else

#ifdef VBOX_WITH_VBOXSDL
    if (   !aFrontend.compare("sdl", Utf8Str::CaseInsensitive)
        || !aFrontend.compare("GUI/SDL", Utf8Str::CaseInsensitive)
        || !aFrontend.compare("sdl/separate", Utf8Str::CaseInsensitive)
        || !aFrontend.compare("GUI/SDL/separate", Utf8Str::CaseInsensitive))
    {
        static const char s_szVBoxSDL_exe[] = "VBoxSDL" HOSTSUFF_EXE;
        vrc = RTStrCopy(pszNamePart, cbBufLeft, s_szVBoxSDL_exe);
        AssertRCReturn(vrc, vrc);

        const char *apszArgs[] =
        {
            szPath,
            "--comment", aComment.c_str(),
            "--startvm", aNameOrId.c_str(),
            NULL, /* For "--separate". */
            NULL, /* For "--sup-startup-log". */
            NULL
        };
        unsigned iArg = 5;
        if (fSeparate)
            apszArgs[iArg++] = "--separate";
        if (aExtraArg.isNotEmpty())
            apszArgs[iArg++] = aExtraArg.c_str();

        vrc = RTProcCreateEx(szPath, apszArgs, hEnv, aFlags, NULL, NULL, NULL, NULL, NULL, aExtraData, &aPid);
    }
#else /* !VBOX_WITH_VBOXSDL */
    if (0)
        ;
#endif /* !VBOX_WITH_VBOXSDL */

    else

#ifdef VBOX_WITH_HEADLESS
    if (   !aFrontend.compare("headless", Utf8Str::CaseInsensitive)
        || !aFrontend.compare("capture", Utf8Str::CaseInsensitive)
        || !aFrontend.compare("vrdp", Utf8Str::CaseInsensitive) /* Deprecated. Same as headless. */
       )
    {
        /* On pre-4.0 the "headless" type was used for passing "--vrdp off" to VBoxHeadless to let it work in OSE,
         * which did not contain VRDP server. In VBox 4.0 the remote desktop server (VRDE) is optional,
         * and a VM works even if the server has not been installed.
         * So in 4.0 the "headless" behavior remains the same for default VBox installations.
         * Only if a VRDE has been installed and the VM enables it, the "headless" will work
         * differently in 4.0 and 3.x.
         */
        static const char s_szVBoxHeadless_exe[] = "VBoxHeadless" HOSTSUFF_EXE;
        vrc = RTStrCopy(pszNamePart, cbBufLeft, s_szVBoxHeadless_exe);
        AssertRCReturn(vrc, vrc);

        const char *apszArgs[] =
        {
            szPath,
            "--comment", aComment.c_str(),
            "--startvm", aNameOrId.c_str(),
            "--vrde", "config",
            NULL, /* For "--capture". */
            NULL, /* For "--sup-startup-log". */
            NULL
        };
        unsigned iArg = 7;
        if (!aFrontend.compare("capture", Utf8Str::CaseInsensitive))
            apszArgs[iArg++] = "--capture";
        if (aExtraArg.isNotEmpty())
            apszArgs[iArg++] = aExtraArg.c_str();

# ifdef RT_OS_WINDOWS
        aFlags |= RTPROC_FLAGS_NO_WINDOW;
# endif
        vrc = RTProcCreateEx(szPath, apszArgs, hEnv, aFlags, NULL, NULL, NULL, NULL, NULL, aExtraData, &aPid);
    }
#else /* !VBOX_WITH_HEADLESS */
    if (0)
        ;
#endif /* !VBOX_WITH_HEADLESS */
    else
        vrc = VERR_INVALID_PARAMETER;

    RTEnvDestroy(hEnv);

    if (RT_FAILURE(vrc))
        return vrc;

    return VINF_SUCCESS;
}
