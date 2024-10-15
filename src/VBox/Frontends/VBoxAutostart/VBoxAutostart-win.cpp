/* $Id: VBoxAutostart-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Autostart Service - Windows Specific Code.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/process.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <iprt/win/windows.h>
#include <ntsecapi.h>

#define SECURITY_WIN32
#include <Security.h>

#include <VBox/com/array.h>
#include <VBox/com/com.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/Guid.h>
#include <VBox/com/listeners.h>
#include <VBox/com/NativeEventQueue.h>
#include <VBox/com/string.h>
#include <VBox/com/VirtualBox.h>

#include <VBox/log.h>

#include "VBoxAutostart.h"
#include "PasswordInput.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The service name. */
#define AUTOSTART_SERVICE_NAME             "VBoxAutostartSvc"
/** The service display name. */
#define AUTOSTART_SERVICE_DISPLAY_NAME     "VirtualBox Autostart Service"

/* just define it here instead of including
 * a bunch of nt headers */
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0)
#endif


ComPtr<IVirtualBoxClient> g_pVirtualBoxClient = NULL;
bool                      g_fVerbose    = false;
ComPtr<IVirtualBox>       g_pVirtualBox = NULL;
ComPtr<ISession>          g_pSession    = NULL;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The service control handler handle. */
static SERVICE_STATUS_HANDLE g_hSupSvcWinCtrlHandler = NULL;
/** The service status. */
static uint32_t volatile g_u32SupSvcWinStatus = SERVICE_STOPPED;
/** The semaphore the main service thread is waiting on in autostartSvcWinServiceMain. */
static RTSEMEVENTMULTI g_hSupSvcWinEvent = NIL_RTSEMEVENTMULTI;
/** The service name is used for send to service main. */
static com::Bstr g_bstrServiceName;

/** Verbosity level. */
unsigned             g_cVerbosity = 0;

/** Logging parameters. */
static uint32_t      g_cHistory = 10;                   /* Enable log rotation, 10 files. */
static uint32_t      g_uHistoryFileTime = 0;            /* No time limit, it's very low volume. */
static uint64_t      g_uHistoryFileSize = 100 * _1M;    /* Max 100MB per file. */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static SC_HANDLE autostartSvcWinOpenSCManager(const char *pszAction, DWORD dwAccess);
static RTEXITCODE autostartSvcWinShowHelp(void);


static int autostartGetProcessDomainUser(com::Utf8Str &aUser)
{
    int rc = VERR_NOT_SUPPORTED;

    RTUTF16 wszUsername[1024] = { 0 };
    ULONG   cwcUsername = RT_ELEMENTS(wszUsername);
    char *pszUser = NULL;
    if (!GetUserNameExW(NameSamCompatible, &wszUsername[0], &cwcUsername))
        return RTErrConvertFromWin32(GetLastError());
    rc = RTUtf16ToUtf8(wszUsername, &pszUser);
    aUser = pszUser;
    aUser.toLower();
    RTStrFree(pszUser);
    return rc;
}

static int autostartGetLocalDomain(com::Utf8Str &aDomain)
{
    RTUTF16 pwszDomain[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
    uint32_t cwcDomainSize =  MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(pwszDomain, (LPDWORD)&cwcDomainSize))
        return RTErrConvertFromWin32(GetLastError());
    char *pszDomain = NULL;
    int rc = RTUtf16ToUtf8(pwszDomain, &pszDomain);
    aDomain = pszDomain;
    aDomain.toLower();
    RTStrFree(pszDomain);
    return rc;
}

static int autostartGetDomainAndUser(const com::Utf8Str &aDomainAndUser, com::Utf8Str &aDomain, com::Utf8Str &aUser)
{
    size_t offDelim = aDomainAndUser.find("\\");
    if (offDelim != aDomainAndUser.npos)
    {
        // if only domain is specified
        if (aDomainAndUser.length() - offDelim == 1)
            return VERR_INVALID_PARAMETER;

        if (offDelim == 1 && aDomainAndUser[0] == '.')
        {
            int rc = autostartGetLocalDomain(aDomain);
            aUser = aDomainAndUser.substr(offDelim + 1);
            return rc;
        }
        aDomain = aDomainAndUser.substr(0, offDelim);
        aUser   = aDomainAndUser.substr(offDelim + 1);
        aDomain.toLower();
        aUser.toLower();
        return VINF_SUCCESS;
    }

    offDelim = aDomainAndUser.find("@");
    if (offDelim != aDomainAndUser.npos)
    {
        // if only domain is specified
        if (offDelim == 0)
            return VERR_INVALID_PARAMETER;

        // with '@' but without domain
        if (aDomainAndUser.length() - offDelim == 1)
        {
            int rc = autostartGetLocalDomain(aDomain);
            aUser = aDomainAndUser.substr(0, offDelim);
            return rc;
        }
        aDomain = aDomainAndUser.substr(offDelim + 1);
        aUser   = aDomainAndUser.substr(0, offDelim);
        aDomain.toLower();
        aUser.toLower();
        return VINF_SUCCESS;
    }

    // only user is specified
    int rc = autostartGetLocalDomain(aDomain);
    aUser = aDomainAndUser;
    aDomain.toLower();
    aUser.toLower();
    return rc;
}

/** Common helper for formatting the service name. */
static void autostartFormatServiceName(const com::Utf8Str &aDomain, const com::Utf8Str &aUser, com::Utf8Str &aServiceName)
{
    aServiceName.printf("%s%s%s", AUTOSTART_SERVICE_NAME, aDomain.c_str(), aUser.c_str());
}

/** Used by the delete service operation. */
static int autostartGetServiceName(const com::Utf8Str &aDomainAndUser, com::Utf8Str &aServiceName)
{
    com::Utf8Str sDomain;
    com::Utf8Str sUser;
    int rc = autostartGetDomainAndUser(aDomainAndUser, sDomain, sUser);
    if (RT_FAILURE(rc))
        return rc;
    autostartFormatServiceName(sDomain, sUser, aServiceName);
    return VINF_SUCCESS;
}

/**
 * Print out progress on the console.
 *
 * This runs the main event queue every now and then to prevent piling up
 * unhandled things (which doesn't cause real problems, just makes things
 * react a little slower than in the ideal case).
 */
DECLHIDDEN(HRESULT) showProgress(ComPtr<IProgress> progress)
{
    using namespace com;

    BOOL fCompleted = FALSE;
    ULONG uCurrentPercent = 0;
    Bstr bstrOperationDescription;

    NativeEventQueue::getMainEventQueue()->processEventQueue(0);

    ULONG cOperations = 1;
    HRESULT hrc = progress->COMGETTER(OperationCount)(&cOperations);
    if (FAILED(hrc))
        return hrc;

    /* setup signal handling if cancelable */
    bool fCanceledAlready = false;
    BOOL fCancelable;
    hrc = progress->COMGETTER(Cancelable)(&fCancelable);
    if (FAILED(hrc))
        fCancelable = FALSE;

    hrc = progress->COMGETTER(Completed(&fCompleted));
    while (SUCCEEDED(hrc))
    {
        progress->COMGETTER(Percent(&uCurrentPercent));

        if (fCompleted)
            break;

        /* process async cancelation */
        if (!fCanceledAlready)
        {
            hrc = progress->Cancel();
            if (SUCCEEDED(hrc))
                fCanceledAlready = true;
        }

        /* make sure the loop is not too tight */
        progress->WaitForCompletion(100);

        NativeEventQueue::getMainEventQueue()->processEventQueue(0);
        hrc = progress->COMGETTER(Completed(&fCompleted));
    }

    /* complete the line. */
    LONG iRc = E_FAIL;
    hrc = progress->COMGETTER(ResultCode)(&iRc);
    if (SUCCEEDED(hrc))
    {
        hrc = iRc;
    }

    return hrc;
}

DECLHIDDEN(void) autostartSvcOsLogStr(const char *pszMsg, AUTOSTARTLOGTYPE enmLogType)
{
    /* write it to the console + release log too (if configured). */
    LogRel(("%s", pszMsg));

    /** @todo r=andy Only (un)register source once? */
    HANDLE hEventLog = RegisterEventSourceA(NULL /* local computer */, "VBoxAutostartSvc");
    AssertReturnVoid(hEventLog != NULL);
    WORD wType = 0;
    const char *apsz[2];
    apsz[0] = "VBoxAutostartSvc";
    apsz[1] = pszMsg;

    switch (enmLogType)
    {
        case AUTOSTARTLOGTYPE_INFO:
            RTStrmPrintf(g_pStdOut, "%s", pszMsg);
            wType = 0;
            break;
        case AUTOSTARTLOGTYPE_ERROR:
            RTStrmPrintf(g_pStdErr, "Error: %s", pszMsg);
            wType = EVENTLOG_ERROR_TYPE;
            break;
        case AUTOSTARTLOGTYPE_WARNING:
            RTStrmPrintf(g_pStdOut, "Warning: %s", pszMsg);
            wType = EVENTLOG_WARNING_TYPE;
            break;
        case AUTOSTARTLOGTYPE_VERBOSE:
            RTStrmPrintf(g_pStdOut, "%s", pszMsg);
            wType = EVENTLOG_INFORMATION_TYPE;
            break;
        default:
            AssertMsgFailed(("Invalid log type %#x\n", enmLogType));
            break;
    }

    /** @todo r=andy Why ANSI and not Unicode (xxxW)? */
    BOOL fRc = ReportEventA(hEventLog,               /* hEventLog */
                            wType,                   /* wType */
                            0,                       /* wCategory */
                            0 /** @todo mc */,       /* dwEventID */
                            NULL,                    /* lpUserSid */
                            RT_ELEMENTS(apsz),       /* wNumStrings */
                            0,                       /* dwDataSize */
                            apsz,                    /* lpStrings */
                            NULL);                   /* lpRawData */
    AssertMsg(fRc, ("ReportEventA failed with %ld\n", GetLastError())); RT_NOREF(fRc);
    DeregisterEventSource(hEventLog);
}


/**
 * Adds "logon as service" policy to user rights
 *
 * When this fails, an error message will be displayed.
 *
 * @returns VBox status code.
 *
 * @param   sUser        The name of user whom the policy should be added.
 */
static int autostartUpdatePolicy(const com::Utf8Str &sUser)
{
    LSA_OBJECT_ATTRIBUTES objectAttributes;
    /* Object attributes are reserved, so initialize to zeros. */
    RT_ZERO(objectAttributes);

    int vrc;

    /* Get a handle to the Policy object. */
    LSA_HANDLE hPolicy;
    NTSTATUS ntRc = LsaOpenPolicy( NULL, &objectAttributes, POLICY_ALL_ACCESS, &hPolicy);
    if (ntRc != STATUS_SUCCESS)
    {
        DWORD dwErr = LsaNtStatusToWinError(ntRc);
        vrc = RTErrConvertFromWin32(dwErr);
        autostartSvcDisplayError("LsaOpenPolicy failed rc=%Rrc (%#x)\n", vrc, dwErr);
        return vrc;
    }
    /* Get user SID */
    DWORD cbDomain = 0;
    SID_NAME_USE enmSidUse = SidTypeUser;
    RTUTF16 *pwszUser = NULL;
    size_t cwUser = 0;
    vrc = RTStrToUtf16Ex(sUser.c_str(), sUser.length(), &pwszUser, 0, &cwUser);
    if (RT_SUCCESS(vrc))
    {
        PSID pSid = NULL;
        DWORD cbSid = 0;
        if (!LookupAccountNameW( NULL, pwszUser, pSid, &cbSid, NULL, &cbDomain, &enmSidUse))
        {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_INSUFFICIENT_BUFFER)
            {
                pSid = (PSID)RTMemAllocZ(cbSid);
                if (pSid != NULL)
                {
                    PRTUTF16 pwszDomain = (PRTUTF16)RTMemAllocZ(cbDomain * sizeof(RTUTF16));
                    if (pwszDomain != NULL)
                    {
                        if (LookupAccountNameW( NULL, pwszUser, pSid, &cbSid, pwszDomain, &cbDomain, &enmSidUse))
                        {
                            if (enmSidUse != SidTypeUser)
                            {
                                vrc = VERR_INVALID_PARAMETER;
                                autostartSvcDisplayError("The name %s is not the user\n", sUser.c_str());
                            }
                            else
                            {
                                /* Add privilege */
                                LSA_UNICODE_STRING lwsPrivilege;
                                // Create an LSA_UNICODE_STRING for the privilege names.
                                lwsPrivilege.Buffer = (PWSTR)L"SeServiceLogonRight";
                                size_t cwPrivilege = wcslen(lwsPrivilege.Buffer);
                                lwsPrivilege.Length = (USHORT)cwPrivilege * sizeof(WCHAR);
                                lwsPrivilege.MaximumLength = (USHORT)(cwPrivilege + 1) * sizeof(WCHAR);
                                ntRc = LsaAddAccountRights(hPolicy, pSid, &lwsPrivilege, 1);
                                if (ntRc != STATUS_SUCCESS)
                                {
                                    dwErr = LsaNtStatusToWinError(ntRc);
                                    vrc = RTErrConvertFromWin32(dwErr);
                                    autostartSvcDisplayError("LsaAddAccountRights failed rc=%Rrc (%#x)\n", vrc, dwErr);
                                }
                            }
                        }
                        else
                        {
                            dwErr = GetLastError();
                            vrc = RTErrConvertFromWin32(dwErr);
                            autostartSvcDisplayError("LookupAccountName failed rc=%Rrc (%#x)\n", vrc, dwErr);
                        }
                        RTMemFree(pwszDomain);
                    }
                    else
                    {
                        vrc = VERR_NO_MEMORY;
                        autostartSvcDisplayError("autostartUpdatePolicy failed rc=%Rrc\n", vrc);
                    }

                    RTMemFree(pSid);
                }
                else
                {
                    vrc = VERR_NO_MEMORY;
                    autostartSvcDisplayError("autostartUpdatePolicy failed rc=%Rrc\n", vrc);
                }
            }
            else
            {
                vrc = RTErrConvertFromWin32(dwErr);
                autostartSvcDisplayError("LookupAccountName failed rc=%Rrc (%#x)\n", vrc, dwErr);
            }
        }
    }
    else
        autostartSvcDisplayError("Failed to convert user name rc=%Rrc\n", vrc);

    if (pwszUser != NULL)
        RTUtf16Free(pwszUser);

    LsaClose(hPolicy);
    return vrc;
}


/**
 * Opens the service control manager.
 *
 * When this fails, an error message will be displayed.
 *
 * @returns Valid handle on success.
 *          NULL on failure, will display an error message.
 *
 * @param   pszAction       The action which is requesting access to SCM.
 * @param   dwAccess        The desired access.
 */
static SC_HANDLE autostartSvcWinOpenSCManager(const char *pszAction, DWORD dwAccess)
{
    SC_HANDLE hSCM = OpenSCManager(NULL /* lpMachineName*/, NULL /* lpDatabaseName */, dwAccess);
    if (hSCM == NULL)
    {
        DWORD err = GetLastError();
        switch (err)
        {
            case ERROR_ACCESS_DENIED:
                autostartSvcDisplayError("%s - OpenSCManager failure: access denied\n", pszAction);
                break;
            default:
                autostartSvcDisplayError("%s - OpenSCManager failure: %d\n", pszAction, err);
                break;
        }
    }
    return hSCM;
}


/**
 * Opens the service.
 *
 * Last error is preserved on failure and set to 0 on success.
 *
 * @returns Valid service handle on success.
 *          NULL on failure, will display an error message unless it's ignored.
 *          Use GetLastError() to find out what the last Windows error was.
 *
 * @param   pszAction           The action which is requesting access to the service.
 * @param   dwSCMAccess         The service control manager access.
 * @param   dwSVCAccess         The desired service access.
 * @param   cIgnoredErrors      The number of ignored errors.
 * @param   ...                 Errors codes that should not cause a message to be displayed.
 */
static SC_HANDLE autostartSvcWinOpenService(const PRTUTF16 pwszServiceName, const char *pszAction, DWORD dwSCMAccess, DWORD dwSVCAccess,
                                            unsigned cIgnoredErrors, ...)
{
    SC_HANDLE hSCM = autostartSvcWinOpenSCManager(pszAction, dwSCMAccess);
    if (!hSCM)
        return NULL;

    SC_HANDLE hSvc = OpenServiceW(hSCM, pwszServiceName, dwSVCAccess);
    if (hSvc)
    {
        CloseServiceHandle(hSCM);
        SetLastError(0);
    }
    else
    {
        DWORD const dwErr    = GetLastError();
        bool        fIgnored = false;
        va_list va;
        va_start(va, cIgnoredErrors);
        while (!fIgnored && cIgnoredErrors-- > 0)
            fIgnored = (DWORD)va_arg(va, int) == dwErr;
        va_end(va);
        if (!fIgnored)
        {
            switch (dwErr)
            {
                case ERROR_ACCESS_DENIED:
                    autostartSvcDisplayError("%s - OpenService failure: access denied\n", pszAction);
                    break;
                case ERROR_SERVICE_DOES_NOT_EXIST:
                    autostartSvcDisplayError("%s - OpenService failure: The service %ls does not exist. Reinstall it.\n",
                                             pszAction, pwszServiceName);
                    break;
                default:
                    autostartSvcDisplayError("%s - OpenService failure, rc=%Rrc (%#x)\n", RTErrConvertFromWin32(dwErr), dwErr);
                    break;
            }
        }

        CloseServiceHandle(hSCM);
        SetLastError(dwErr);
    }
    return hSvc;
}

static RTEXITCODE autostartSvcWinInterrogate(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTPrintf("VBoxAutostartSvc: The \"interrogate\" action is not implemented.\n");
    return RTEXITCODE_FAILURE;
}


static RTEXITCODE autostartSvcWinStop(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTPrintf("VBoxAutostartSvc: The \"stop\" action is not implemented.\n");
    return RTEXITCODE_FAILURE;
}


static RTEXITCODE autostartSvcWinContinue(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTPrintf("VBoxAutostartSvc: The \"continue\" action is not implemented.\n");
    return RTEXITCODE_FAILURE;
}


static RTEXITCODE autostartSvcWinPause(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTPrintf("VBoxAutostartSvc: The \"pause\" action is not implemented.\n");
    return RTEXITCODE_FAILURE;
}


static RTEXITCODE autostartSvcWinStart(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTPrintf("VBoxAutostartSvc: The \"start\" action is not implemented.\n");
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE autostartSvcWinQueryDescription(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTPrintf("VBoxAutostartSvc: The \"qdescription\" action is not implemented.\n");
    return RTEXITCODE_FAILURE;
}


static RTEXITCODE autostartSvcWinQueryConfig(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTPrintf("VBoxAutostartSvc: The \"qconfig\" action is not implemented.\n");
    return RTEXITCODE_FAILURE;
}


static RTEXITCODE autostartSvcWinDisable(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTPrintf("VBoxAutostartSvc: The \"disable\" action is not implemented.\n");
    return RTEXITCODE_FAILURE;
}

static RTEXITCODE autostartSvcWinEnable(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTPrintf("VBoxAutostartSvc: The \"enable\" action is not implemented.\n");
    return RTEXITCODE_FAILURE;
}


/**
 * Handle the 'delete' action.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE.
 * @param   argc    The action argument count.
 * @param   argv    The action argument vector.
 */
static RTEXITCODE autostartSvcWinDelete(int argc, char **argv)
{
    /*
     * Parse the arguments.
     */
    const char *pszUser = NULL;
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--verbose", 'v', RTGETOPT_REQ_NOTHING },
        { "--user",    'u', RTGETOPT_REQ_STRING  },
    };
    int ch;
    RTGETOPTUNION Value;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((ch = RTGetOpt(&GetState, &Value)))
    {
        switch (ch)
        {
            case 'v':
                g_cVerbosity++;
                break;
            case 'u':
                pszUser = Value.psz;
                break;
            default:
                return autostartSvcDisplayGetOptError("delete", ch, &Value);
        }
    }

    if (!pszUser)
        return autostartSvcDisplayError("delete - DeleteService failed, user name required.\n");

    com::Utf8Str sServiceName;
    int vrc = autostartGetServiceName(pszUser, sServiceName);
    if (RT_FAILURE(vrc))
        return autostartSvcDisplayError("delete - DeleteService failed, service name for user %s cannot be constructed.\n",
                                        pszUser);
    /*
     * Delete the service.
     */
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    SC_HANDLE hSvc = autostartSvcWinOpenService(com::Bstr(sServiceName).raw(), "delete", SERVICE_CHANGE_CONFIG, DELETE, 0);
    if (hSvc)
    {
        if (DeleteService(hSvc))
        {
            if (g_cVerbosity)
                RTPrintf("Successfully deleted the %s service.\n", sServiceName.c_str());
            rcExit = RTEXITCODE_SUCCESS;
        }
        else
        {
            DWORD const dwErr = GetLastError();
            autostartSvcDisplayError("delete - DeleteService failed, rc=%Rrc (%#x)\n", RTErrConvertFromWin32(dwErr), dwErr);
        }
        CloseServiceHandle(hSvc);
    }
    return rcExit;
}


/**
 * Handle the 'create' action.
 *
 * @returns 0 or 1.
 * @param   argc    The action argument count.
 * @param   argv    The action argument vector.
 */
static RTEXITCODE autostartSvcWinCreate(int argc, char **argv)
{
    /*
     * Parse the arguments.
     */
    const char *pszUser = NULL;
    com::Utf8Str strPwd;
    const char *pszPwdFile = NULL;
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* Common options first. */
        { "--verbose",       'v', RTGETOPT_REQ_NOTHING },
        { "--user",          'u', RTGETOPT_REQ_STRING  },
        { "--username",      'u', RTGETOPT_REQ_STRING  },
        { "--password-file", 'p', RTGETOPT_REQ_STRING  }
    };
    int ch;
    RTGETOPTUNION Value;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((ch = RTGetOpt(&GetState, &Value)))
    {
        switch (ch)
        {
            /* Common options first. */
            case 'v':
                g_cVerbosity++;
                break;
            case 'u':
                pszUser = Value.psz;
                break;
            case 'p':
                pszPwdFile = Value.psz;
                break;
            default:
                return autostartSvcDisplayGetOptError("create", ch, &Value);
        }
    }

    if (!pszUser)
        return autostartSvcDisplayError("Username is missing");

    if (pszPwdFile)
    {
        /* Get password from file. */
        RTEXITCODE rcExit = readPasswordFile(pszPwdFile, &strPwd);
        if (rcExit == RTEXITCODE_FAILURE)
            return rcExit;
    }
    else
    {
        /* Get password from console. */
        RTEXITCODE rcExit = readPasswordFromConsole(&strPwd, "Enter password:");
        if (rcExit == RTEXITCODE_FAILURE)
            return rcExit;
    }

    if (strPwd.isEmpty())
        return autostartSvcDisplayError("Password is missing");

    com::Utf8Str sDomain;
    com::Utf8Str sUserTmp;
    int vrc = autostartGetDomainAndUser(pszUser, sDomain, sUserTmp);
    if (RT_FAILURE(vrc))
        return autostartSvcDisplayError("create - Failed to get domain and user from string '%s' (%Rrc)\n",
                                        pszUser, vrc);
    com::Utf8StrFmt sUserFullName("%s\\%s", sDomain.c_str(), sUserTmp.c_str());
    com::Utf8StrFmt sDisplayName("%s %s@%s", AUTOSTART_SERVICE_DISPLAY_NAME, sUserTmp.c_str(), sDomain.c_str());
    com::Utf8Str    sServiceName;
    autostartFormatServiceName(sDomain, sUserTmp, sServiceName);

    vrc = autostartUpdatePolicy(sUserFullName);
    if (RT_FAILURE(vrc))
        return autostartSvcDisplayError("Failed to get/update \"logon as service\" policy for user %s (%Rrc)\n",
                                        sUserFullName.c_str(), vrc);
    /*
     * Create the service.
     */
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    SC_HANDLE hSCM = autostartSvcWinOpenSCManager("create", SC_MANAGER_CREATE_SERVICE); /*SC_MANAGER_ALL_ACCESS*/
    if (hSCM)
    {
        char szExecPath[RTPATH_MAX];
        if (RTProcGetExecutablePath(szExecPath, sizeof(szExecPath)))
        {
            if (g_cVerbosity)
                RTPrintf("Creating the %s service, binary \"%s\"...\n",
                         sServiceName.c_str(), szExecPath); /* yea, the binary name isn't UTF-8, but wtf. */

            /*
             * Add service name as command line parameter for the service
             */
            com::Utf8StrFmt sCmdLine("\"%s\" --service=%s", szExecPath, sServiceName.c_str());
            com::Bstr bstrServiceName(sServiceName);
            com::Bstr bstrDisplayName(sDisplayName);
            com::Bstr bstrCmdLine(sCmdLine);
            com::Bstr bstrUserFullName(sUserFullName);
            com::Bstr bstrPwd(strPwd);

            SC_HANDLE hSvc = CreateServiceW(hSCM,                            /* hSCManager */
                                            bstrServiceName.raw(),           /* lpServiceName */
                                            bstrDisplayName.raw(),           /* lpDisplayName */
                                            SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG, /* dwDesiredAccess */
                                            SERVICE_WIN32_OWN_PROCESS,       /* dwServiceType ( | SERVICE_INTERACTIVE_PROCESS? ) */
                                            SERVICE_AUTO_START,              /* dwStartType */
                                            SERVICE_ERROR_NORMAL,            /* dwErrorControl */
                                            bstrCmdLine.raw(),               /* lpBinaryPathName */
                                            NULL,                            /* lpLoadOrderGroup */
                                            NULL,                            /* lpdwTagId */
                                            L"Winmgmt\0RpcSs\0\0",           /* lpDependencies */
                                            bstrUserFullName.raw(),          /* lpServiceStartName (NULL => LocalSystem) */
                                            bstrPwd.raw());                  /* lpPassword */
            if (hSvc)
            {
                RTPrintf("Successfully created the %s service.\n", sServiceName.c_str());
                /** @todo Set the service description or it'll look weird in the vista service manager.
                 *  Anything else that should be configured? Start access or something? */
                rcExit = RTEXITCODE_SUCCESS;
                CloseServiceHandle(hSvc);
            }
            else
            {
                DWORD const dwErr = GetLastError();
                switch (dwErr)
                {
                    case ERROR_SERVICE_EXISTS:
                        autostartSvcDisplayError("create - The service already exists!\n");
                        break;
                    default:
                        autostartSvcDisplayError("create - CreateService failed, rc=%Rrc (%#x)\n",
                                                 RTErrConvertFromWin32(dwErr), dwErr);
                        break;
                }
            }
            CloseServiceHandle(hSvc);
        }
        else
            autostartSvcDisplayError("create - Failed to obtain the executable path\n");
    }
    return rcExit;
}


/**
 * Sets the service status, just a SetServiceStatus Wrapper.
 *
 * @returns See SetServiceStatus.
 * @param   dwStatus        The current status.
 * @param   iWaitHint       The wait hint, if < 0 then supply a default.
 * @param   dwExitCode      The service exit code.
 */
static bool autostartSvcWinSetServiceStatus(DWORD dwStatus, int iWaitHint, DWORD dwExitCode)
{
    SERVICE_STATUS SvcStatus;
    SvcStatus.dwServiceType         = SERVICE_WIN32_OWN_PROCESS;
    SvcStatus.dwWin32ExitCode       = dwExitCode;
    SvcStatus.dwServiceSpecificExitCode = 0;
    SvcStatus.dwWaitHint            = iWaitHint >= 0 ? iWaitHint : 3000;
    SvcStatus.dwCurrentState        = dwStatus;
    LogFlow(("autostartSvcWinSetServiceStatus: %d -> %d\n", g_u32SupSvcWinStatus, dwStatus));
    g_u32SupSvcWinStatus            = dwStatus;
    switch (dwStatus)
    {
        case SERVICE_START_PENDING:
            SvcStatus.dwControlsAccepted = 0;
            break;
        default:
            SvcStatus.dwControlsAccepted
                = SERVICE_ACCEPT_STOP
                | SERVICE_ACCEPT_SHUTDOWN;
            break;
    }

    static DWORD dwCheckPoint = 0;
    switch (dwStatus)
    {
        case SERVICE_RUNNING:
        case SERVICE_STOPPED:
            SvcStatus.dwCheckPoint       = 0;
        default:
            SvcStatus.dwCheckPoint       = ++dwCheckPoint;
            break;
    }
    return SetServiceStatus(g_hSupSvcWinCtrlHandler, &SvcStatus) != FALSE;
}


/**
 * Service control handler (extended).
 *
 * @returns Windows status (see HandlerEx).
 * @retval  NO_ERROR if handled.
 * @retval  ERROR_CALL_NOT_IMPLEMENTED if not handled.
 *
 * @param   dwControl       The control code.
 * @param   dwEventType     Event type. (specific to the control?)
 * @param   pvEventData     Event data, specific to the event.
 * @param   pvContext       The context pointer registered with the handler.
 *                          Currently not used.
 */
static DWORD WINAPI
autostartSvcWinServiceCtrlHandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID pvEventData, LPVOID pvContext) RT_NOTHROW_DEF
{
    RT_NOREF(dwEventType);
    RT_NOREF(pvEventData);
    RT_NOREF(pvContext);

    LogFlow(("autostartSvcWinServiceCtrlHandlerEx: dwControl=%#x dwEventType=%#x pvEventData=%p\n",
             dwControl, dwEventType, pvEventData));

    switch (dwControl)
    {
        /*
         * Interrogate the service about it's current status.
         * MSDN says that this should just return NO_ERROR and does
         * not need to set the status again.
         */
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;

        /*
         * Request to stop the service.
         */
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_STOP:
        {
            if (dwControl == SERVICE_CONTROL_SHUTDOWN)
                autostartSvcLogVerbose(1, "SERVICE_CONTROL_SHUTDOWN\n");
            else
                autostartSvcLogVerbose(1, "SERVICE_CONTROL_STOP\n");

            /*
             * Check if the real services can be stopped and then tell them to stop.
             */
            autostartSvcWinSetServiceStatus(SERVICE_STOP_PENDING, 3000, NO_ERROR);

            /*
             * Notify the main thread that we're done, it will wait for the
             * VMs to stop, and set the windows service status to SERVICE_STOPPED
             * and return.
             */
            int rc = RTSemEventMultiSignal(g_hSupSvcWinEvent);
            if (RT_FAILURE(rc)) /** @todo r=andy Don't we want to report back an error here to SCM? */
                autostartSvcLogErrorRc(rc, "SERVICE_CONTROL_STOP: RTSemEventMultiSignal failed, %Rrc\n", rc);

            return NO_ERROR;
        }

        default:
            /*
             * We only expect to receive controls we explicitly listed
             * in SERVICE_STATUS::dwControlsAccepted.  Logged in hex
             * b/c WinSvc.h defines them in hex
             */
            autostartSvcLogWarning("Unexpected service control message 0x%RX64\n", (uint64_t)dwControl);
            break;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

static int autostartStartVMs(void)
{
    int rc = autostartSetup();
    if (RT_FAILURE(rc))
        return rc;

    const char *pszConfigFile = RTEnvGet("VBOXAUTOSTART_CONFIG");
    if (!pszConfigFile)
        return autostartSvcLogErrorRc(VERR_ENV_VAR_NOT_FOUND,
                                      "Starting VMs failed. VBOXAUTOSTART_CONFIG environment variable is not defined.\n");
    bool fAllow = false;

    PCFGAST pCfgAst = NULL;
    rc = autostartParseConfig(pszConfigFile, &pCfgAst);
    if (RT_FAILURE(rc))
        return autostartSvcLogErrorRc(rc, "Starting VMs failed. Failed to parse the config file. Check the access permissions and file structure.\n");
    PCFGAST pCfgAstPolicy = autostartConfigAstGetByName(pCfgAst, "default_policy");
    /* Check default policy. */
    if (pCfgAstPolicy)
    {
        if (   pCfgAstPolicy->enmType == CFGASTNODETYPE_KEYVALUE
            && (   !RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "allow")
                || !RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "deny")))
        {
            if (!RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "allow"))
                fAllow = true;
        }
        else
        {
            autostartConfigAstDestroy(pCfgAst);
            return autostartSvcLogErrorRc(VERR_INVALID_PARAMETER, "'default_policy' must be either 'allow' or 'deny'.\n");
        }
    }

    com::Utf8Str sUser;
    rc = autostartGetProcessDomainUser(sUser);
    if (RT_FAILURE(rc))
    {
        autostartConfigAstDestroy(pCfgAst);
        return autostartSvcLogErrorRc(rc, "Failed to query username of the process (%Rrc).\n", rc);
    }

    PCFGAST pCfgAstUser = NULL;
    for (unsigned i = 0; i < pCfgAst->u.Compound.cAstNodes; i++)
    {
        PCFGAST pNode = pCfgAst->u.Compound.apAstNodes[i];
        com::Utf8Str sDomain;
        com::Utf8Str sUserTmp;
        rc = autostartGetDomainAndUser(pNode->pszKey, sDomain, sUserTmp);
        if (RT_FAILURE(rc))
            continue;
        com::Utf8StrFmt sDomainUser("%s\\%s", sDomain.c_str(), sUserTmp.c_str());
        if (sDomainUser == sUser)
        {
            pCfgAstUser = pNode;
            break;
        }
    }

    if (   pCfgAstUser
        && pCfgAstUser->enmType == CFGASTNODETYPE_COMPOUND)
    {
        pCfgAstPolicy = autostartConfigAstGetByName(pCfgAstUser, "allow");
        if (pCfgAstPolicy)
        {
            if (   pCfgAstPolicy->enmType == CFGASTNODETYPE_KEYVALUE
                && (   !RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "true")
                    || !RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "false")))
                fAllow = RTStrCmp(pCfgAstPolicy->u.KeyValue.aszValue, "true") == 0;
            else
            {
                autostartConfigAstDestroy(pCfgAst);
                return autostartSvcLogErrorRc(VERR_INVALID_PARAMETER, "'allow' must be either 'true' or 'false'.\n");
            }
        }
    }
    else if (pCfgAstUser)
    {
        autostartConfigAstDestroy(pCfgAst);
        return autostartSvcLogErrorRc(VERR_INVALID_PARAMETER, "Invalid config, user is not a compound node.\n");
    }

    if (!fAllow)
    {
        autostartConfigAstDestroy(pCfgAst);
        return autostartSvcLogErrorRc(VERR_INVALID_PARAMETER, "User is not allowed to autostart VMs.\n");
    }

    if (RT_SUCCESS(rc))
        rc = autostartStartMain(pCfgAstUser);

    autostartConfigAstDestroy(pCfgAst);

    return rc;
}

/**
 * Windows Service Main.
 *
 * This is invoked when the service is started and should not return until
 * the service has been stopped.
 *
 * @param   cArgs           Argument count.
 * @param   papwszArgs      Argument vector.
 */
static VOID WINAPI autostartSvcWinServiceMain(DWORD cArgs, LPWSTR *papwszArgs)
{
    RT_NOREF(cArgs, papwszArgs);
    LogFlowFuncEnter();

    /* Give this thread a name in the logs. */
    RTThreadAdopt(RTTHREADTYPE_DEFAULT, 0, "service", NULL);

#if 0
    for (size_t i = 0; i < cArgs; ++i)
        LogRel(("arg[%zu] = %ls\n", i, papwszArgs[i]));
#endif

    DWORD dwErr = ERROR_GEN_FAILURE;

    /*
     * Register the control handler function for the service and report to SCM.
     */
    Assert(g_u32SupSvcWinStatus == SERVICE_STOPPED);
    g_hSupSvcWinCtrlHandler = RegisterServiceCtrlHandlerExW(g_bstrServiceName.raw(), autostartSvcWinServiceCtrlHandlerEx, NULL);
    if (g_hSupSvcWinCtrlHandler)
    {
        if (autostartSvcWinSetServiceStatus(SERVICE_START_PENDING, 3000, NO_ERROR))
        {
            /*
             * Create the event semaphore we'll be waiting on and
             * then instantiate the actual services.
             */
            int rc = RTSemEventMultiCreate(&g_hSupSvcWinEvent);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Update the status and enter the work loop.
                 */
                if (autostartSvcWinSetServiceStatus(SERVICE_RUNNING, 0, 0))
                {
                    LogFlow(("autostartSvcWinServiceMain: calling autostartStartVMs\n"));

                    /* check if we should stopped already, e.g. windows shutdown */
                    rc = RTSemEventMultiWait(g_hSupSvcWinEvent, 1);
                    if (RT_FAILURE(rc))
                    {
                        /* No one signaled us to stop */
                        rc = autostartStartVMs();
                    }
                    autostartShutdown();
                }
                else
                {
                    dwErr = GetLastError();
                    autostartSvcLogError("SetServiceStatus failed, rc=%Rrc (%#x)\n", RTErrConvertFromWin32(dwErr), dwErr);
                }

                RTSemEventMultiDestroy(g_hSupSvcWinEvent);
                g_hSupSvcWinEvent = NIL_RTSEMEVENTMULTI;
            }
            else
                autostartSvcLogError("RTSemEventMultiCreate failed, rc=%Rrc\n", rc);
        }
        else
        {
            dwErr = GetLastError();
            autostartSvcLogError("SetServiceStatus failed, rc=%Rrc (%#x)\n", RTErrConvertFromWin32(dwErr), dwErr);
        }
        autostartSvcWinSetServiceStatus(SERVICE_STOPPED, 0, dwErr);
    }
    /* else error will be handled by the caller. */
}


/**
 * Handle the 'runit' action.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE.
 * @param   argc    The action argument count.
 * @param   argv    The action argument vector.
 */
static RTEXITCODE autostartSvcWinRunIt(int argc, char **argv)
{
    int vrc;

    LogFlowFuncEnter();

    /*
     * Init com here for first main thread initialization.
     * Service main function called in another thread
     * created by service manager.
     */
    HRESULT hrc = com::Initialize();
# ifdef VBOX_WITH_XPCOM
    if (hrc == NS_ERROR_FILE_ACCESS_DENIED)
    {
        char szHome[RTPATH_MAX] = "";
        com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome));
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
               "Failed to initialize COM because the global settings directory '%s' is not accessible!", szHome);
    }
# endif
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to initialize COM (%Rhrc)!", hrc);
    /*
     * Initialize release logging, do this early.  This means command
     * line options (like --logfile &c) can't be introduced to affect
     * the log file parameters, but the user can't change them easily
     * anyway and is better off using environment variables.
     */
    do
    {
        char szLogFile[RTPATH_MAX];
        vrc = com::GetVBoxUserHomeDirectory(szLogFile, sizeof(szLogFile),
                                           /* :fCreateDir */ false);
        if (RT_FAILURE(vrc))
        {
            autostartSvcLogError("Failed to get VirtualBox user home directory: %Rrc\n", vrc);
            break;
        }

        if (!RTDirExists(szLogFile)) /* vbox user home dir */
        {
            autostartSvcLogError("%s doesn't exist\n", szLogFile);
            break;
        }

        vrc = RTPathAppend(szLogFile, sizeof(szLogFile), "VBoxAutostart.log");
        if (RT_FAILURE(vrc))
        {
            autostartSvcLogError( "Failed to construct release log file name: %Rrc\n", vrc);
            break;
        }

        vrc = com::VBoxLogRelCreate(AUTOSTART_SERVICE_NAME,
                                   szLogFile,
                                     RTLOGFLAGS_PREFIX_THREAD
                                   | RTLOGFLAGS_PREFIX_TIME_PROG,
                                   "all",
                                   "VBOXAUTOSTART_RELEASE_LOG",
                                   RTLOGDEST_FILE,
                                   UINT32_MAX /* cMaxEntriesPerGroup */,
                                   g_cHistory,
                                   g_uHistoryFileTime,
                                   g_uHistoryFileSize,
                                   NULL);
        if (RT_FAILURE(vrc))
            autostartSvcLogError("Failed to create release log file: %Rrc\n", vrc);
    } while (0);

    /*
     * Parse the arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        /* Common options first. */
        { "--verbose", 'v', RTGETOPT_REQ_NOTHING },
        { "--service", 's', RTGETOPT_REQ_STRING },
    };

    const char *pszServiceName = NULL;
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            /* Common options first. */
            case 'v':
                g_cVerbosity++;
                break;
            case 's':
                pszServiceName = ValueUnion.psz;
                try
                {
                    g_bstrServiceName = com::Bstr(ValueUnion.psz);
                }
                catch (...)
                {
                    autostartSvcLogError("runit failed, service name is not valid UTF-8 string or out of memory\n");
                    return RTEXITCODE_FAILURE;
                }
                break;

            default:
                return autostartSvcDisplayGetOptError("runit", ch, &ValueUnion);
        }
    }

    if (!pszServiceName)
    {
        autostartSvcLogError("runit failed, service name is missing\n");
        return RTEXITCODE_SYNTAX;
    }

    autostartSvcLogInfo("Starting service %ls\n", g_bstrServiceName.raw());

    /*
     * Register the service with the service control manager
     * and start dispatching requests from it (all done by the API).
     */
    SERVICE_TABLE_ENTRYW const s_aServiceStartTable[] =
    {
        { g_bstrServiceName.raw(), autostartSvcWinServiceMain },
        { NULL, NULL}
    };

    if (StartServiceCtrlDispatcherW(&s_aServiceStartTable[0]))
    {
        LogFlowFuncLeave();
        return RTEXITCODE_SUCCESS; /* told to quit, so quit. */
    }

    DWORD const dwErr = GetLastError();
    switch (dwErr)
    {
        case ERROR_FAILED_SERVICE_CONTROLLER_CONNECT:
            autostartSvcLogWarning("Cannot run a service from the command line. Use the 'start' action to start it the right way.\n");
            autostartSvcWinServiceMain(0 /* cArgs */, NULL /* papwszArgs */);
            break;
        default:
            autostartSvcLogError("StartServiceCtrlDispatcher failed, rc=%Rrc (%#x)\n", RTErrConvertFromWin32(dwErr), dwErr);
            break;
    }

    com::Shutdown();

    return RTEXITCODE_FAILURE;
}


/**
 * Show the version info.
 *
 * @returns RTEXITCODE_SUCCESS.
 */
static RTEXITCODE autostartSvcWinShowVersion(int argc, char **argv)
{
    /*
     * Parse the arguments.
     */
    bool fBrief = false;
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--brief", 'b', RTGETOPT_REQ_NOTHING }
    };
    int ch;
    RTGETOPTUNION Value;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((ch = RTGetOpt(&GetState, &Value)))
        switch (ch)
        {
            case 'b':   fBrief = true;  break;
            default:    return autostartSvcDisplayGetOptError("version", ch, &Value);
        }

    /*
     * Do the printing.
     */
    autostartSvcShowVersion(fBrief);

    return RTEXITCODE_SUCCESS;
}


/**
 * Show the usage help screen.
 *
 * @returns RTEXITCODE_SUCCESS.
 */
static RTEXITCODE autostartSvcWinShowHelp(void)
{
    autostartSvcShowHeader();

    const char *pszExe = RTPathFilename(RTProcExecutablePath());

    RTPrintf("Usage:\n"
             "\n"
             "%s [global-options] [command] [command-options]\n"
             "\n"
             "Global options:\n"
             "  -v\n"
             "    Increases the verbosity. Can be specified multiple times."
             "\n\n"
             "No command given:\n"
             "  Runs the service.\n"
             "Options:\n"
             "  --service <name>\n"
             "    Specifies the service name to run.\n"
             "\n"
             "Command </help|help|-?|-h|--help> [...]\n"
             "    Displays this help screen.\n"
             "\n"
             "Command </version|version|-V|--version> [-brief]\n"
             "    Displays the version.\n"
             "\n"
             "Command </i|install|/RegServer> --user <username> --password-file <...>\n"
             "  Installs the service.\n"
             "Options:\n"
             "  --user <username>\n"
             "    Specifies the user name the service should be installed for.\n"
             "  --password-file <path/to/file>\n"
             "    Specifies the file for user password to use for installation.\n"
             "\n"
             "Command </u|uninstall|delete|/UnregServer>\n"
             "  Uninstalls the service.\n"
             "  --user <username>\n"
             "    Specifies the user name the service should will be deleted for.\n",
             pszExe);
    return RTEXITCODE_SUCCESS;
}


/**
 * VBoxAutostart main(), Windows edition.
 *
 *
 * @returns 0 on success.
 *
 * @param   argc    Number of arguments in argv.
 * @param   argv    Argument vector.
 */
int main(int argc, char **argv)
{
    /*
     * Initialize the IPRT first of all.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
    {
        autostartSvcLogError("RTR3InitExe failed with rc=%Rrc\n", rc);
        return RTEXITCODE_FAILURE;
    }

    /*
     * Parse the initial arguments to determine the desired action.
     */
    enum
    {
        kAutoSvcAction_RunIt, /* Default action, also called by SCM. */

        kAutoSvcAction_Create,
        kAutoSvcAction_Delete,

        kAutoSvcAction_Enable,
        kAutoSvcAction_Disable,
        kAutoSvcAction_QueryConfig,
        kAutoSvcAction_QueryDescription,

        kAutoSvcAction_Start,
        kAutoSvcAction_Pause,
        kAutoSvcAction_Continue,
        kAutoSvcAction_Stop,
        kAutoSvcAction_Interrogate,

        kAutoSvcAction_End
    } enmAction = kAutoSvcAction_RunIt;
    int iArg = 1;
    if (argc > 1)
    {
        if (    !stricmp(argv[iArg], "/RegServer")
            ||  !stricmp(argv[iArg], "install")
            ||  !stricmp(argv[iArg], "/i"))
            enmAction = kAutoSvcAction_Create;
        else if (   !stricmp(argv[iArg], "/UnregServer")
                 || !stricmp(argv[iArg], "/u")
                 || !stricmp(argv[iArg], "uninstall")
                 || !stricmp(argv[iArg], "delete"))
            enmAction = kAutoSvcAction_Delete;

        else if (!stricmp(argv[iArg], "enable"))
            enmAction = kAutoSvcAction_Enable;
        else if (!stricmp(argv[iArg], "disable"))
            enmAction = kAutoSvcAction_Disable;
        else if (!stricmp(argv[iArg], "qconfig"))
            enmAction = kAutoSvcAction_QueryConfig;
        else if (!stricmp(argv[iArg], "qdescription"))
            enmAction = kAutoSvcAction_QueryDescription;

        else if (   !stricmp(argv[iArg], "start")
                 || !stricmp(argv[iArg], "/t"))
            enmAction = kAutoSvcAction_Start;
        else if (!stricmp(argv[iArg], "pause"))
            enmAction = kAutoSvcAction_Start;
        else if (!stricmp(argv[iArg], "continue"))
            enmAction = kAutoSvcAction_Continue;
        else if (!stricmp(argv[iArg], "stop"))
            enmAction = kAutoSvcAction_Stop;
        else if (!stricmp(argv[iArg], "interrogate"))
            enmAction = kAutoSvcAction_Interrogate;
        else if (   !stricmp(argv[iArg], "help")
                 || !stricmp(argv[iArg], "?")
                 || !stricmp(argv[iArg], "/?")
                 || !stricmp(argv[iArg], "-?")
                 || !stricmp(argv[iArg], "/h")
                 || !stricmp(argv[iArg], "-h")
                 || !stricmp(argv[iArg], "/help")
                 || !stricmp(argv[iArg], "-help")
                 || !stricmp(argv[iArg], "--help"))
            return autostartSvcWinShowHelp();
        else if (   !stricmp(argv[iArg], "version")
                 || !stricmp(argv[iArg], "/ver")
                 || !stricmp(argv[iArg], "-V") /* Note: "-v" is used for specifying the verbosity. */
                 || !stricmp(argv[iArg], "/version")
                 || !stricmp(argv[iArg], "-version")
                 || !stricmp(argv[iArg], "--version"))
            return autostartSvcWinShowVersion(argc - iArg - 1, argv + iArg + 1);
        else
            iArg--;
        iArg++;
    }

    /*
     * Dispatch it.
     */
    switch (enmAction)
    {
        case kAutoSvcAction_RunIt:
        {
            RTEXITCODE const rcExit = autostartSvcWinRunIt(argc - iArg, argv + iArg);
            if (rcExit == RTEXITCODE_SYNTAX) /* When called by a user (e.g. w/o specifying any command, print our syntax help. */
                autostartSvcWinShowHelp();
            return rcExit;
        }

        case kAutoSvcAction_Create:
            return autostartSvcWinCreate(argc - iArg, argv + iArg);
        case kAutoSvcAction_Delete:
            return autostartSvcWinDelete(argc - iArg, argv + iArg);

        case kAutoSvcAction_Enable:
            return autostartSvcWinEnable(argc - iArg, argv + iArg);
        case kAutoSvcAction_Disable:
            return autostartSvcWinDisable(argc - iArg, argv + iArg);
        case kAutoSvcAction_QueryConfig:
            return autostartSvcWinQueryConfig(argc - iArg, argv + iArg);
        case kAutoSvcAction_QueryDescription:
            return autostartSvcWinQueryDescription(argc - iArg, argv + iArg);

        case kAutoSvcAction_Start:
            return autostartSvcWinStart(argc - iArg, argv + iArg);
        case kAutoSvcAction_Pause:
            return autostartSvcWinPause(argc - iArg, argv + iArg);
        case kAutoSvcAction_Continue:
            return autostartSvcWinContinue(argc - iArg, argv + iArg);
        case kAutoSvcAction_Stop:
            return autostartSvcWinStop(argc - iArg, argv + iArg);
        case kAutoSvcAction_Interrogate:
            return autostartSvcWinInterrogate(argc - iArg, argv + iArg);

        default:
            AssertMsgFailed(("enmAction=%d\n", enmAction));
    }

    return RTEXITCODE_FAILURE;
}
