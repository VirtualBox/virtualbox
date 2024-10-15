/* $Id: VBoxAddInstallNt3x.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxAddInstallNt3x - Install Guest Additions on NT3.51, 3.5 and 3.1.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include <iprt/nt/nt-and-windows.h>
#include <iprt/ctype.h>
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include <VBox/version.h>
#include <revision-generated.h> /* VBOX_SVN_REV. */



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Components (also indexes into g_aComponents). */
typedef enum { kComp_VBoxGuest = 0, kComp_VBoxService = 1, kComp_VBoxMouse = 2} VBOXGACOMP;
/** File status. */
typedef enum { kFile_NotFound, kFile_LongName, kFile_8Dot3, kFile_Both, kFile_Mismatch } VBOXGAFILE;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The components. */
struct
{
    const char     *pszName;
    VBOXGACOMP      enmComp;
    bool            fSelected;

    bool            fDriverFile;
    const wchar_t  *pwszFilename;
    const wchar_t  *pwsz8Dot3;
    const wchar_t  *pwszServiceName;
    const wchar_t  *pwszServiceDesc;
    const wchar_t  *pwszServiceLoadOrderGroup;

    /** @name Status
     * @{ */
    VBOXGAFILE      enmFileStatus;
    bool            fServiceInstalled;
    bool            fMisconfigured;
    bool            fActive;
    VBOXGAFILE      enmServiceFile;
    wchar_t         wszServiceImagePath[MAX_PATH];
    /** @} */

} g_aComponents[] =
{
    {
       "VBoxGuest",     kComp_VBoxGuest,    true, true,  L"VBoxGuest.sys",   L"VBoxGst.sys",
        L"VBoxGuest",   L"VirtualBox Guest Additions Driver",   L"System",
        kFile_NotFound, false, false, false, kFile_NotFound, {0},
    },
    {
        "VBoxService",  kComp_VBoxService,  true, false, L"VBoxService.exe", L"VBoxGaSv.exe",
        L"VBoxService", L"VirtualBox Guest Additions Service",  L"Base",
        kFile_NotFound, false, false, false, kFile_NotFound, {0},
    },
    {
        "VBoxMouse",    kComp_VBoxMouse,    true, true,  L"VBoxMouseNT.sys", L"VBoxMou.sys",
        L"i8042prt",    L"i8042prt",                            L"Pointer Port",
        kFile_NotFound, true,  false, false, kFile_NotFound, {0},
    },
};

/** The source path where the files are. */
static WCHAR  g_wszSrc[MAX_PATH];
static size_t g_cwcSrc = 0;

#define MAKE_SANE_VERSION(a_uMajor, a_uMinor) RT_MAKE_U32(a_uMinor, a_uMajor)
static uint32_t g_uSaneVersion = MAKE_SANE_VERSION(3,51);
static DWORD    g_dwVersion    = 3 | (51 << 8);


RTDECL(PRTUTF16) RTUtf16ToLowerAscii(PRTUTF16 pwsz)
{
    for (PRTUTF16 pwc = pwsz; *pwc; pwc++)
        if (*pwc < 0x7f)
            *pwc = RT_C_TO_LOWER(*pwc);
    return pwsz;
}


/**
 * Composes the service binary path for component.
 */
static WCHAR *ComposeServicePath(WCHAR *pwszPath, size_t cwcPath, size_t iComponent, bool f8Dot3)
{
    static wchar_t const s_wszPrefixDrv[] = L"\\SystemRoot\\System32\\drivers\\";
    static wchar_t const s_wszPrefixExe[] = L"%SystemRoot%\\System32\\";
    size_t cwcDst;
    if (g_aComponents[iComponent].fDriverFile)
    {
        RTUtf16Copy(pwszPath, cwcPath, s_wszPrefixDrv);
        cwcDst = RT_ELEMENTS(s_wszPrefixDrv) - 1;
    }
    else
    {
        RTUtf16Copy(pwszPath, cwcPath, s_wszPrefixExe);
        cwcDst = RT_ELEMENTS(s_wszPrefixExe) - 1;
    }

    RTUtf16Copy(&pwszPath[cwcDst], cwcPath - cwcDst,
                f8Dot3 ? g_aComponents[iComponent].pwsz8Dot3 : g_aComponents[iComponent].pwszFilename);
    return pwszPath;
}


/**
 * Composes the installed filename for a component.
 */
static int ComposeFilename(WCHAR *pwszPath, size_t cwcPath, size_t iComponent, bool f8Dot3)
{
    UINT cwcDst = GetSystemDirectoryW(pwszPath, cwcPath - 30);
    if (cwcDst > 0)
    {
        pwszPath[cwcDst++] = '\\';
        if (g_aComponents[iComponent].fDriverFile)
        {
            pwszPath[cwcDst++] = 'd';
            pwszPath[cwcDst++] = 'r';
            pwszPath[cwcDst++] = 'i';
            pwszPath[cwcDst++] = 'v';
            pwszPath[cwcDst++] = 'e';
            pwszPath[cwcDst++] = 'r';
            pwszPath[cwcDst++] = 's';
            pwszPath[cwcDst++] = '\\';
        }
        const wchar_t *pwszSrc = f8Dot3 ? g_aComponents[iComponent].pwsz8Dot3 : g_aComponents[iComponent].pwszFilename;
        do
            pwszPath[cwcDst++] = *pwszSrc;
        while (*pwszSrc++);
        return VINF_SUCCESS;
    }
    RTMsgError("GetSystemDirectoryW failed: %u\n", GetLastError());
    return VERR_GENERAL_FAILURE;
}


/**
 * Composes the source filename for a component.
 */
static int ComposeSourceFilename(WCHAR *pwszPath, size_t cwcPath, size_t iComponent)
{
    int rc = RTUtf16Copy(pwszPath, cwcPath, g_wszSrc);
    if (RT_SUCCESS(rc))
        rc = RTUtf16Copy(&pwszPath[g_cwcSrc], cwcPath - g_cwcSrc, g_aComponents[iComponent].pwszFilename);
    if (RT_FAILURE(rc))
        RTMsgError("Failed to compose source filename path for '%ls': %Rrc\n", g_aComponents[iComponent].pwszFilename, rc);
    return rc;
}


static DWORD DeterminServiceType(size_t iComponent)
{
    if (g_aComponents[iComponent].fDriverFile)
        return SERVICE_KERNEL_DRIVER;
    /* SERVICE_INTERACTIVE_PROCESS was added in 3.50: */
    if (g_uSaneVersion >= MAKE_SANE_VERSION(3, 50))
        return SERVICE_INTERACTIVE_PROCESS | SERVICE_WIN32_OWN_PROCESS;
    return SERVICE_WIN32_OWN_PROCESS;
}


static DWORD DeterminServiceStartType(size_t iComponent)
{
    if (g_aComponents[iComponent].fDriverFile)
    {
        if (g_aComponents[iComponent].enmComp == kComp_VBoxMouse)
            return SERVICE_SYSTEM_START;
        return SERVICE_BOOT_START;
    }
    return SERVICE_AUTO_START;
}


static DWORD DeterminServiceErrorControl(size_t iComponent)
{
    if (   g_aComponents[iComponent].enmComp == kComp_VBoxMouse
        && g_uSaneVersion != MAKE_SANE_VERSION(3, 10))
        return SERVICE_ERROR_IGNORE;
    return SERVICE_ERROR_NORMAL;
}


static WCHAR const *DeterminServiceLoadOrderGroup(size_t iComponent)
{
    if (   g_aComponents[iComponent].enmComp == kComp_VBoxMouse
        && g_uSaneVersion == MAKE_SANE_VERSION(3, 10))
        return L"Keyboard Port";
    return g_aComponents[iComponent].pwszServiceLoadOrderGroup;
}


static DWORD *DeterminServiceTag(size_t iComponent, DWORD *pidTag)
{
    if (g_aComponents[iComponent].enmComp != kComp_VBoxMouse)
        return NULL;
    *pidTag = 1;
    return pidTag;
}


/**
 * Updates the status portion of g_aComponents.
 */
void UpdateStatus(void)
{
    /*
     * File precense.
     */
    WCHAR wszPath[MAX_PATH];
    for (size_t i = 0; i < RT_ELEMENTS(g_aComponents); i++)
    {
        ComposeFilename(wszPath, RT_ELEMENTS(wszPath), i, false /*f8Dot3*/);
        DWORD fLongAttribs = GetFileAttributesW(wszPath);

        ComposeFilename(wszPath, RT_ELEMENTS(wszPath), i, true /*f8Dot3*/);
        DWORD f8Dot3Attribs = GetFileAttributesW(wszPath);

        if (f8Dot3Attribs == INVALID_FILE_ATTRIBUTES && fLongAttribs == INVALID_FILE_ATTRIBUTES)
            g_aComponents[i].enmFileStatus = kFile_NotFound;
        else if (f8Dot3Attribs != INVALID_FILE_ATTRIBUTES && fLongAttribs == INVALID_FILE_ATTRIBUTES)
            g_aComponents[i].enmFileStatus = kFile_8Dot3;
        else if (f8Dot3Attribs == INVALID_FILE_ATTRIBUTES && fLongAttribs != INVALID_FILE_ATTRIBUTES)
            g_aComponents[i].enmFileStatus = kFile_LongName;
        else
            g_aComponents[i].enmFileStatus = kFile_Both;
    }

    /*
     * Service config.
     */
    SC_HANDLE hServiceMgr = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hServiceMgr)
    {
        RTMsgError("Failed to open service manager (for status queries): %u\n", GetLastError());
        return;
    }

    for (size_t i = 0; i < RT_ELEMENTS(g_aComponents); i++)
    {
        g_aComponents[i].fActive        = false;
        g_aComponents[i].fMisconfigured = false;

        SetLastError(NO_ERROR);
        SC_HANDLE hService = OpenServiceW(hServiceMgr, g_aComponents[i].pwszServiceName,
                                          SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
        if (hService)
        {
            DWORD const dwExpectedType      = DeterminServiceType(i);
            DWORD const dwExpectedStartType = DeterminServiceStartType(i);

            g_aComponents[i].fServiceInstalled = true;

            union
            {
                QUERY_SERVICE_CONFIGW   Config;
                SERVICE_STATUS          Status;
                uint8_t                 abPadding[_8K];
            } u;

            /* Status: */
            RT_ZERO(u);
            if (QueryServiceStatus(hService, &u.Status))
            {
                g_aComponents[i].fMisconfigured = false;

                if (u.Status.dwServiceType != dwExpectedType)
                {
                    RTMsgWarning("Unexpected dwServiceType for '%ls': %#x, expected %#x\n",
                                 g_aComponents[i].pwszServiceName, u.Status.dwServiceType, dwExpectedType);
                    g_aComponents[i].fMisconfigured = true;
                }

                g_aComponents[i].fActive = u.Status.dwCurrentState == SERVICE_RUNNING
                                        || u.Status.dwCurrentState == SERVICE_START_PENDING;
            }
            else
                RTMsgWarning("QueryServiceStatus failed on '%ls': %u\n", g_aComponents[i].pwszServiceName, GetLastError());

            /* Configuration: */
            RT_ZERO(u);
            DWORD cbNeeded = 0;
            if (QueryServiceConfigW(hService, &u.Config, sizeof(u), &cbNeeded))
            {
                if (u.Config.dwServiceType != dwExpectedType)
                    g_aComponents[i].fMisconfigured = true;

                if (u.Config.dwStartType != dwExpectedStartType)
                {
                    RTMsgWarning("Unexpected dwStartType for '%ls': %#x, expected %#x\n",
                                 g_aComponents[i].pwszServiceName, u.Config.dwStartType, dwExpectedStartType);
                    g_aComponents[i].fMisconfigured = true;
                }

                if (u.Config.lpBinaryPathName)
                {
                    RTUtf16Copy(g_aComponents[i].wszServiceImagePath, RT_ELEMENTS(g_aComponents[i].wszServiceImagePath),
                                u.Config.lpBinaryPathName);

                    PRTUTF16 const pwszCfg = RTUtf16ToLowerAscii(u.Config.lpBinaryPathName);
                    if (RTUtf16Cmp(RTUtf16ToLowerAscii(ComposeServicePath(wszPath, RT_ELEMENTS(wszPath), i, false /*f8Dot3*/)),
                                   pwszCfg) == 0)
                        g_aComponents[i].enmServiceFile = kFile_LongName;
                    else if (RTUtf16Cmp(RTUtf16ToLowerAscii(ComposeServicePath(wszPath, RT_ELEMENTS(wszPath), i, true /*f8Dot3*/)),
                                        pwszCfg) == 0)
                        g_aComponents[i].enmServiceFile = kFile_8Dot3;
                    else
                    {
                        g_aComponents[i].enmServiceFile = kFile_Mismatch;
                        g_aComponents[i].fMisconfigured = true;
                    }
                }
                else
                    g_aComponents[i].fMisconfigured = true;

                if (   !u.Config.lpLoadOrderGroup
                    || RTUtf16Cmp(u.Config.lpLoadOrderGroup, DeterminServiceLoadOrderGroup(i)) != 0)
                {
                    RTMsgWarning("Unexpected load group for '%ls': '%ls', expected '%ls'\n",
                                 g_aComponents[i].pwszServiceName, u.Config.lpLoadOrderGroup, DeterminServiceLoadOrderGroup(i));
                    g_aComponents[i].fMisconfigured = true;
                }
            }
            else
                RTMsgWarning("QueryServiceConfigW failed on '%ls': %u\n", g_aComponents[i].pwszServiceName, GetLastError());

            CloseServiceHandle(hService);
        }
        else
        {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_SERVICE_DOES_NOT_EXIST)
                g_aComponents[i].fServiceInstalled = false;
            else
                RTMsgWarning("Failed to open '%ls' for status query: %u\n", g_aComponents[i].pwszServiceName, dwErr);
        }
    }

    CloseServiceHandle(hServiceMgr);
}


/**
 * Reports the device statuses.
 */
int DoStatus(void)
{
    RTPrintf("NT Version: %#x = %u.%u build %u\n", g_dwVersion,
             g_dwVersion & 0xff, (g_dwVersion >> 8) & 0xff, g_dwVersion >> 16);

    WCHAR wszPath[MAX_PATH];
    for (size_t i = 0; i < RT_ELEMENTS(g_aComponents); i++)
        if (g_aComponents[i].fSelected)
        {
            RTPrintf("%ls:\n", g_aComponents[i].pwszServiceName);
            RTPrintf("    %s%s\n", g_aComponents[i].fServiceInstalled ? "service installed" : "service not installed",
                     g_aComponents[i].fMisconfigured ? " - misconfigured" : "");
            if (   g_aComponents[i].enmFileStatus == kFile_LongName
                || g_aComponents[i].enmFileStatus == kFile_Both)
            {
                ComposeFilename(wszPath, RT_ELEMENTS(wszPath), i, false /*f8Dot3*/);
                RTPrintf("    File:         %ls\n", wszPath);
            }
            if (   g_aComponents[i].enmFileStatus == kFile_8Dot3
                || g_aComponents[i].enmFileStatus == kFile_Both)
            {
                ComposeFilename(wszPath, RT_ELEMENTS(wszPath), i, true /*f8Dot3*/);
                RTPrintf("    File 8.3:     %ls\n", wszPath);
            }
            if (g_aComponents[i].wszServiceImagePath[0])
                RTPrintf("    ServiceImage: %ls (%s)\n", g_aComponents[i].wszServiceImagePath,
                         g_aComponents[i].enmServiceFile == kFile_Mismatch   ? "mismatch"
                         : g_aComponents[i].enmServiceFile == kFile_LongName ? "long"
                         : g_aComponents[i].enmServiceFile == kFile_8Dot3    ? "8.3" : "whut!?!");
        }
    return RTEXITCODE_SUCCESS;
}


/**
 * Does the installation.
 */
int DoInstall(bool f8Dot3)
{
    /*
     * Validate the request.  We cannot install either VBoxService
     * or VBoxMouse w/o the VBoxGuest driver (being) installed.
     */
    if (   !g_aComponents[kComp_VBoxGuest].fSelected
        && !(   g_aComponents[kComp_VBoxGuest].fActive
             || (g_aComponents[kComp_VBoxGuest].fServiceInstalled && !g_aComponents[kComp_VBoxGuest].fMisconfigured)))
    {
        RTMsgError("VBoxGuest is required by all other components!\n"
                   "It is not selected nor installed in any working state!\n");
        return RTEXITCODE_FAILURE;
    }

    /*
     * We may need the service manager for stopping VBoxService, so open it
     * before doing the copying.
     */
    SC_HANDLE hServiceMgr = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hServiceMgr)
        return RTMsgErrorExitFailure("Failed to open service manager (for all access): %u\n", GetLastError());

    /*
     * First step, copy over the files.
     */
    WCHAR wszSrc[MAX_PATH];
    WCHAR wszDst[MAX_PATH];
    for (size_t i = 0; i < RT_ELEMENTS(g_aComponents); i++)
    {
        if (!g_aComponents[i].fSelected)
            continue;
        int rc = ComposeSourceFilename(wszSrc, RT_ELEMENTS(wszSrc), i);
        if (RT_SUCCESS(rc))
            rc = ComposeFilename(wszDst, RT_ELEMENTS(wszDst), i, f8Dot3);
        if (RT_FAILURE(rc))
            return RTEXITCODE_FAILURE;

        /* If service active and it isn't a driver, we must stop it or we
           cannot copy the file. */
        if (g_aComponents[i].fActive && !g_aComponents[i].fDriverFile)
        {
            SC_HANDLE hService = OpenServiceW(hServiceMgr, g_aComponents[i].pwszServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
            if (!hService)
                return RTMsgErrorExitFailure("Failed to open service '%ls' for stopping: %u\n",
                                             g_aComponents[i].pwszServiceName, GetLastError());
            uint32_t const uStartTick = GetTickCount();
            uint32_t cStopsSent = 0;
            for (;;)
            {
                SERVICE_STATUS Status;
                RT_ZERO(Status);
                if (!QueryServiceStatus(hService, &Status))
                    return RTMsgErrorExitFailure("Failed to query status of service '%ls': %u\n",
                                                 g_aComponents[i].pwszServiceName, GetLastError());
                if (Status.dwCurrentState == SERVICE_STOPPED)
                    break;

                if (GetTickCount() - uStartTick > 30000)
                    return RTMsgErrorExitFailure("Giving up trying to stop service '%ls': %u\n",
                                                 g_aComponents[i].pwszServiceName, GetLastError());

                if (Status.dwCurrentState != SERVICE_STOP_PENDING)
                {
                    if (cStopsSent > 5)
                        return RTMsgErrorExitFailure("Giving up trying to stop service '%ls': %u\n",
                                                     g_aComponents[i].pwszServiceName, GetLastError());
                    if (cStopsSent)
                        Sleep(128);
                    if (!ControlService(hService, SERVICE_CONTROL_STOP, &Status))
                        return RTMsgErrorExitFailure("Failed to stop service '%ls': %u\n",
                                                     g_aComponents[i].pwszServiceName, GetLastError());
                    cStopsSent++;
                    if (Status.dwCurrentState == SERVICE_STOPPED)
                        break;
                }
                Sleep(256);
            }
            CloseServiceHandle(hService);
        }

        /* Before copying, make sure the destination doesn't have the
           readonly bit set. */
        DWORD fAttribs = GetFileAttributesW(wszDst);
        if (   (fAttribs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN))
            && fAttribs != INVALID_FILE_ATTRIBUTES)
        {
            fAttribs &= ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
            if (!fAttribs)
                fAttribs |= FILE_ATTRIBUTE_NORMAL;
            SetFileAttributesW(wszDst, fAttribs);
        }

        if (CopyFileW(wszSrc, wszDst, FALSE /*fFailIfExists*/))
            RTMsgInfo("Copied '%ls' to '%ls'\n", wszSrc, wszDst);
        else
            return RTMsgErrorExitFailure("Failed to copy '%ls' to '%ls': %u\n", wszSrc, wszDst, GetLastError());
    }

    /*
     * Second step, do the installing / reconfiguring of services.
     */
    for (size_t i = 0; i < RT_ELEMENTS(g_aComponents); i++)
    {
        if (!g_aComponents[i].fSelected)
            continue;
        DWORD const     dwType             = DeterminServiceType(i);
        DWORD const     dwStartType        = DeterminServiceStartType(i);
        DWORD const     dwErrorCtrl        = DeterminServiceErrorControl(i);
        wchar_t const  *pwszLoadOrderGroup = DeterminServiceLoadOrderGroup(i);
        DWORD           idTag              = 0;
        DWORD * const   pidTag             = DeterminServiceTag(i, &idTag);

        ComposeServicePath(wszDst, RT_ELEMENTS(wszDst), i, f8Dot3);

        SC_HANDLE hService;
        if (!g_aComponents[i].fServiceInstalled)
        {
            hService = CreateServiceW(hServiceMgr,
                                      g_aComponents[i].pwszServiceName,
                                      g_aComponents[i].pwszServiceDesc,
                                      SERVICE_ALL_ACCESS,
                                      dwType,
                                      dwStartType,
                                      dwErrorCtrl,
                                      wszDst,
                                      pwszLoadOrderGroup,
                                      pidTag,
                                      NULL /*pwszDependencies*/,
                                      NULL /*pwszServiceStartName*/,
                                      NULL /*pwszPassword*/);
            if (!hService)
                return RTMsgErrorExitFailure("Failed to create service '%ls': %u\n",
                                             g_aComponents[i].pwszServiceName, GetLastError());
            RTMsgInfo("Created service '%ls'.\n", g_aComponents[i].pwszServiceName);
        }
        else if (   g_aComponents[i].fMisconfigured
                 || RTUtf16Cmp(g_aComponents[i].wszServiceImagePath, wszDst) != 0)
        {
            hService = OpenServiceW(hServiceMgr, g_aComponents[i].pwszServiceName, SERVICE_ALL_ACCESS);
            if (!hService)
                return RTMsgErrorExitFailure("Failed to open service '%ls': %u\n",
                                             g_aComponents[i].pwszServiceName, GetLastError());
            if (!ChangeServiceConfigW(hService,
                                      dwType,
                                      dwStartType,
                                      dwErrorCtrl,
                                      wszDst,
                                      pwszLoadOrderGroup,
                                      pidTag,
                                      NULL /*pwszDependencies*/,
                                      NULL /*pwszServiceStartName*/,
                                      NULL /*pwszPassword*/,
                                      g_aComponents[i].enmComp != kComp_VBoxMouse ? g_aComponents[i].pwszServiceDesc : NULL))
                return RTMsgErrorExitFailure("Failed to change configuration of service '%ls': %u\n",
                                             g_aComponents[i].pwszServiceName, GetLastError());
            RTMsgInfo("Reconfigured service '%ls'.\n", g_aComponents[i].pwszServiceName);
        }
        else
        {
            RTMsgInfo("No changes to service '%ls'.\n", g_aComponents[i].pwszServiceName);
            continue;
        }
        CloseServiceHandle(hService);
    }

    CloseServiceHandle(hServiceMgr);

    RTMsgInfo("Done.  Please reboot.\n");
    return RTEXITCODE_SUCCESS;
}


int DoUninstall(void)
{
    return RTMsgError("Not implemented. Sorry.\n");
}


int usage(const char *argv0)
{
    RTPrintf("Usage: %Rbn [--status]  [--select <component> [..]]\n"
             "   or  %Rbn --install   [--select <component> [..]] [--8-dot-3]\n"
             "   or  %Rbn --uninstall [--select <component> [..]]\n"
             "   or  %Rbn --help\n"
             "   or  %Rbn --version\n"
             "\n"
             "VirtualBox Guest Additions installer for NT 3.x.\n"
             "\n"
             "Options:\n"
             "  --status\n"
             "      Checks the installation status of the components.\n"
             "  --install\n"
             "      Installs the selected components.\n"
             "  --uninstall\n"
             "      Uninstalls the selected components.\n"
             "  --selected <component>\n"
             "      Select a component.  By default all components are selected. However,\n"
             "      when this option is first used all are unselected before processing it.\n"
             "      Components:",
             argv0, argv0, argv0, argv0, argv0);
    for (size_t i = 0; i < RT_ELEMENTS(g_aComponents); i++)
        RTPrintf(" %s", g_aComponents[i].pszName);
    RTPrintf("\n"
             "  --8-dot-3, -8\n"
             "      Install files in 8.3 compatible manner (for FAT system volume).\n"
             "  --long-names, -l\n"
             "      Install files with long filenames (NTFS system volume). The default.\n"
             "  --help, -h, -?\n"
             "      Display this help text.\n"
             "  --version, -V\n"
             "      Display the version number.\n"
             );
    return RTEXITCODE_SUCCESS;
}


int main(int argc, char **argv)
{
    /*
     * Init version.
     */
    g_dwVersion    = GetVersion();
    g_uSaneVersion = MAKE_SANE_VERSION(g_dwVersion & 0xff, (g_dwVersion >> 8) & 0xff);

    /*
     * Parse arguments.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--status",     1000 + 's',   RTGETOPT_REQ_NOTHING },
        { "--install",    1000 + 'i',   RTGETOPT_REQ_NOTHING },
        { "--uninstall",  1000 + 'u',   RTGETOPT_REQ_NOTHING },
        { "--select",     's',          RTGETOPT_REQ_STRING  },
        { "--8-dot-3",    '8',          RTGETOPT_REQ_NOTHING },
        { "--long-names", 'l',          RTGETOPT_REQ_NOTHING },
        { "--src",        'S',          RTGETOPT_REQ_STRING  },
        { "--source",     'S',          RTGETOPT_REQ_STRING  },
    };

    bool    fFirstSelect = true;
    bool    f8Dot3 = false;
    enum { kMode_Status, kMode_Install, kMode_Uninstall }
            enmMode = kMode_Status;

    g_cwcSrc = GetModuleFileNameW(NULL, g_wszSrc, RT_ELEMENTS(g_wszSrc));
    if (g_cwcSrc == 0)
        return RTMsgErrorExitFailure("GetModuleFileNameW failed: %u\n", GetLastError());
    while (g_cwcSrc > 0 && !RTPATH_IS_SEP(g_wszSrc[g_cwcSrc - 1]))
        g_cwcSrc--;
    g_wszSrc[g_cwcSrc] = '\0';

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State,argc,argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTGetOptInit failed: %Rrc\n", rc);

    RTGETOPTUNION ValueUnion;
    int chOpt;
    while ((chOpt = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (chOpt)
        {
            case 1000 + 's':
                enmMode = kMode_Status;
                break;

            case 1000 + 'i':
                enmMode = kMode_Install;
                break;

            case 1000 + 'u':
                enmMode = kMode_Uninstall;
                break;

            case '8':
                f8Dot3 = true;
                break;

            case 'l':
                f8Dot3 = false;
                break;

            case 's':
            {
                size_t i;
                if (fFirstSelect)
                {
                    for (i = 0; i < RT_ELEMENTS(g_aComponents); i++)
                        g_aComponents[i].fSelected = false;
                    fFirstSelect = false;
                }
                for (i = 0; i < RT_ELEMENTS(g_aComponents); i++)
                    if (RTStrICmpAscii(ValueUnion.psz, g_aComponents[i].pszName) == 0)
                    {
                        g_aComponents[i].fSelected = true;
                        break;
                    }
                if (i >= RT_ELEMENTS(g_aComponents))
                    return RTMsgErrorExitFailure("Unknown component: %s\n", ValueUnion.psz);
                break;
            }

            case 'S':
            {
                PRTUTF16 pwszDst = g_wszSrc;
                rc = RTStrToUtf16Ex(ValueUnion.psz, RTSTR_MAX, &pwszDst, RT_ELEMENTS(g_wszSrc) - 16, &g_cwcSrc);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExitFailure("Error converting source to UTF-16: %Rrc\n", rc);
                if (!g_cwcSrc)
                    return RTMsgErrorExitFailure("Empty source argument!\n");
                if (!RTPATH_IS_SEP(g_wszSrc[g_cwcSrc - 1]))
                {
                    g_wszSrc[g_cwcSrc++] = '\\';
                    g_wszSrc[g_cwcSrc]   = '\0';
                }
                break;
            }

            case 'h':
                return usage(argv[0]);

            case 'V':
                RTPrintf("%sr%u\n", VBOX_VERSION_STRING, VBOX_SVN_REV);
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }

    /*
     * Before we do anything gather status info on the components.
     */
    UpdateStatus();

    /*
     * Take action.
     */
    if (enmMode == kMode_Status)
        return DoStatus();
    if (enmMode == kMode_Install)
        return DoInstall(f8Dot3);
    return DoUninstall();
}

