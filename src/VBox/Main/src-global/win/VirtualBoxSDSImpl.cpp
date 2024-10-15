/* $Id: VirtualBoxSDSImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Global COM Class implementation.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_VIRTUALBOXSDS
#include <VBox/com/VirtualBox.h>
#include <VBox/com/utils.h>
#include "VirtualBoxSDSImpl.h"

#include "AutoCaller.h"
#include "LoggingNew.h"
#include "Wrapper.h"        /* for ArrayBSTRInConverter */

#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/system.h>

#include <rpcasync.h>
#include <rpcdcep.h>
#include <sddl.h>
#include <lmcons.h> /* UNLEN */

#include "MachineLaunchVMCommonWorker.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define INTERACTIVE_SID_FLAG 0x1
#define LOCAL_SID_FLAG       0x2
#define LOGON_SID_FLAG       0x4
#define IS_INTERACTIVE       (LOCAL_SID_FLAG|INTERACTIVE_SID_FLAG|LOGON_SID_FLAG)


/**
 * Per user data.
 *
 * @note We never delete instances of this class, except in case of an insertion
 *       race.  This allows us to separate the map lock from the user data lock
 *       and avoid DoS issues.
 */
class VBoxSDSPerUserData
{
public:
    /** The SID (secure identifier) for the user.  This is the key. */
    com::Utf8Str                    m_strUserSid;
    /** The user name (if we could get it). */
    com::Utf8Str                    m_strUsername;
    /** The VBoxSVC chosen to instantiate CLSID_VirtualBox.
     * This is NULL if not set. */
    ComPtr<IVBoxSVCRegistration>    m_ptrTheChosenOne;
    /** The PID of the chosen one. */
    RTPROCESS                       m_pidTheChosenOne;
    /** The tick count when the process in Windows session 0 started */
    uint32_t                        m_tickTheChosenOne;
    /** The current watcher thread index, UINT32_MAX if not watched. */
    uint32_t                        m_iWatcher;
    /** The chosen one revision number.
     * This is used to detect races while waiting for a full watcher queue.  */
    uint32_t volatile               m_iTheChosenOneRevision;
private:
    /** Reference count to make destruction safe wrt hung callers.
     * (References are retain while holding the map lock in some form, but
     * released while holding no locks.) */
    uint32_t volatile               m_cRefs;
    /** Critical section protecting everything here. */
    RTCRITSECT                      m_Lock;

public:
    VBoxSDSPerUserData(com::Utf8Str const &a_rStrUserSid, com::Utf8Str const &a_rStrUsername)
        : m_strUserSid(a_rStrUserSid)
        , m_strUsername(a_rStrUsername)
        , m_pidTheChosenOne(NIL_RTPROCESS)
        , m_tickTheChosenOne(0)
#ifdef WITH_WATCHER
        , m_iWatcher(UINT32_MAX)
        , m_iTheChosenOneRevision(0)
#endif
        , m_cRefs(1)
    {
        RTCritSectInit(&m_Lock);
    }

    ~VBoxSDSPerUserData()
    {
        RTCritSectDelete(&m_Lock);
        i_unchooseTheOne(true /*fIrregular*/);
    }

    uint32_t i_retain()
    {
        uint32_t cRefs = ASMAtomicIncU32(&m_cRefs);
        Assert(cRefs > 1);
        return cRefs;
    }

    uint32_t i_release()
    {
        uint32_t cRefs = ASMAtomicDecU32(&m_cRefs);
        Assert(cRefs < _1K);
        if (cRefs == 0)
            delete this;
        return cRefs;
    }

    void i_lock()
    {
        RTCritSectEnter(&m_Lock);
    }

    void i_unlock()
    {
        RTCritSectLeave(&m_Lock);
    }

    /** Reset the chosen one. */
    void i_unchooseTheOne(bool fIrregular)
    {
        if (m_ptrTheChosenOne.isNotNull())
        {
            if (!fIrregular)
                m_ptrTheChosenOne.setNull();
            else
            {
                LogRel(("i_unchooseTheOne: Irregular release ... (pid=%d (%#x) user=%s sid=%s)\n",
                        m_pidTheChosenOne, m_pidTheChosenOne, m_strUsername.c_str(), m_strUserSid.c_str()));
                m_ptrTheChosenOne.setNull();
                LogRel(("i_unchooseTheOne: ... done.\n"));
            }
        }
        m_pidTheChosenOne = NIL_RTPROCESS;
        m_tickTheChosenOne = 0;
    }

};



/*********************************************************************************************************************************
*   VirtualBoxSDS - constructor / destructor                                                                                     *
*********************************************************************************************************************************/

VirtualBoxSDS::VirtualBoxSDS()
    : m_cVBoxSvcProcesses(0)
#ifdef WITH_WATCHER
    , m_cWatchers(0)
    , m_papWatchers(NULL)
#endif
{
}


VirtualBoxSDS::~VirtualBoxSDS()
{
#ifdef WITH_WATCHER
    i_shutdownAllWatchers();
    RTMemFree(m_papWatchers);
    m_papWatchers = NULL;
    m_cWatchers   = 0;
#endif
}


HRESULT VirtualBoxSDS::FinalConstruct()
{
    LogRelFlowThisFuncEnter();

    int vrc = RTCritSectRwInit(&m_MapCritSect);
    AssertLogRelRCReturn(vrc, E_FAIL);

#ifdef WITH_WATCHER
    vrc = RTCritSectInit(&m_WatcherCritSect);
    AssertLogRelRCReturn(vrc, E_FAIL);
#endif

    LogRelFlowThisFuncLeave();
    return S_OK;
}


void VirtualBoxSDS::FinalRelease()
{
    LogRelFlowThisFuncEnter();

#ifdef WITH_WATCHER
    i_shutdownAllWatchers();
    RTCritSectDelete(&m_WatcherCritSect);
#endif

    RTCritSectRwDelete(&m_MapCritSect);

    for (UserDataMap_T::iterator it = m_UserDataMap.begin(); it != m_UserDataMap.end(); ++it)
    {
        VBoxSDSPerUserData *pUserData = it->second;
        if (pUserData)
        {
            it->second = NULL;
            pUserData->i_release();
        }
    }

    LogRelFlowThisFuncLeave();
}

/* static */
bool VirtualBoxSDS::i_isFeatureEnabled(wchar_t const *a_pwszFeature)
{
    HKEY    hKey;
    LSTATUS lrc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Oracle\\VirtualBox\\VBoxSDS", 0, KEY_READ, &hKey);
    /* Treat any errors as the feature is off. Because the actual error value doesn't matter. */
    if (lrc != ERROR_SUCCESS)
        return false;

    DWORD dwType  = 0;
    DWORD dwValue = 0;
    DWORD cbValue = sizeof(DWORD);
    lrc = RegQueryValueExW(hKey, a_pwszFeature, NULL, &dwType, (LPBYTE)&dwValue, &cbValue);

    bool const fEnabled = lrc     == ERROR_SUCCESS
                       && dwType  == REG_DWORD
                       && dwValue != 0;

    RegCloseKey(hKey);
    return fEnabled;
}


/*********************************************************************************************************************************
*   VirtualBoxSDS - IVirtualBoxSDS methods                                                                                       *
*********************************************************************************************************************************/

/* SDS plan B interfaces: */
STDMETHODIMP VirtualBoxSDS::RegisterVBoxSVC(IVBoxSVCRegistration *aVBoxSVC, LONG aPid, IUnknown **aExistingVirtualBox)
{
    LogRel(("registerVBoxSVC: aPid=%u (%#x)\n", aPid, aPid));

    /*
     * Get the caller PID so we can validate the aPid parameter with the other two.
     * The V2 structure requires Vista or later, so fake it if older.
     */
    RPC_CALL_ATTRIBUTES_V2_W CallAttribs = { RPC_CALL_ATTRIBUTES_VERSION, RPC_QUERY_CLIENT_PID | RPC_QUERY_IS_CLIENT_LOCAL };
    RPC_STATUS rcRpc;
    if (RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
        rcRpc = RpcServerInqCallAttributesW(NULL, &CallAttribs);
    else
    {
        CallAttribs.ClientPID = (HANDLE)(intptr_t)aPid;
        rcRpc = RPC_S_OK;
    }

    HRESULT hrc;
    if (   RT_VALID_PTR(aVBoxSVC)
        && RT_VALID_PTR(aExistingVirtualBox)
        && rcRpc == RPC_S_OK
        && (intptr_t)CallAttribs.ClientPID == aPid)
    {
        *aExistingVirtualBox = NULL;

        /*
         * Get the client user SID and name.
         */
        com::Utf8Str strSid;
        com::Utf8Str strUsername;
        if (i_getClientUserSid(&strSid, &strUsername))
        {
            VBoxSDSPerUserData *pUserData = i_lookupOrCreatePerUserData(strSid, strUsername); /* (returns holding the lock) */
            if (pUserData)
            {
                /*
                 * If there already is a chosen one, ask it for a IVirtualBox instance
                 * to return to the caller. Should it be dead or unresponsive, the caller
                 * takes its place.
                */
                if (pUserData->m_ptrTheChosenOne.isNotNull())
                {
                    try
                    {
                        hrc = pUserData->m_ptrTheChosenOne->GetVirtualBox(aExistingVirtualBox);
                        /* seems the VBoxSVC in windows session 0 is not yet finished object creation.
                         * Give it a time. */
                        if (FAILED(hrc) && GetTickCount() - pUserData->m_tickTheChosenOne < 60 * 1000)
                            hrc = E_PENDING;
                    }
                    catch (...)
                    {
                        LogRel(("registerVBoxSVC: Unexpected exception calling GetVirtualBox!!\n"));
                        hrc = E_FAIL;
                    }
                    if (FAILED_DEAD_INTERFACE(hrc))
                    {
                        LogRel(("registerVBoxSVC: Seems VBoxSVC instance died.  Dropping it and letting caller take over. (hrc=%Rhrc)\n", hrc));
#ifdef WITH_WATCHER
                        i_stopWatching(pUserData, pUserData->m_pidTheChosenOne);
#endif
                        pUserData->i_unchooseTheOne(true /*fIrregular*/);
                        hrc = S_OK;
                    }
                }
                else
                    hrc = S_OK;

                /* No chosen one?  Make the caller the new chosen one! */
                if (SUCCEEDED(hrc) && pUserData->m_ptrTheChosenOne.isNull())
                {
#ifdef VBOX_WITH_VBOXSVC_SESSION_0
                    DWORD dwSessionId = 0;
                    if (VirtualBoxSDS::i_isFeatureEnabled(L"ServerSession0"))
                    {
                        /* Get user token. */
                        HANDLE hThreadToken = NULL;
                        hrc = CoImpersonateClient();
                        if (SUCCEEDED(hrc))
                        {
                            hrc = E_FAIL;
                            if (OpenThreadToken(GetCurrentThread(),
                                                TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE
                                                | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_SESSIONID | TOKEN_READ | TOKEN_WRITE,
                                                TRUE /* OpenAsSelf - for impersonation at SecurityIdentification level */,
                                                &hThreadToken))
                            {
                                HANDLE hNewToken;
                                if (DuplicateTokenEx(hThreadToken, MAXIMUM_ALLOWED, NULL /*SecurityAttribs*/,
                                                     SecurityIdentification, TokenPrimary, &hNewToken))
                                {
                                    CloseHandle(hThreadToken);
                                    hThreadToken = hNewToken;
                                    hrc = S_OK;
                                }
                                else
                                    LogRel(("registerVBoxSVC: DuplicateTokenEx failed: %ld\n", GetLastError()));
                            }
                            else
                                LogRel(("registerVBoxSVC: OpenThreadToken failed: %ld\n", GetLastError()));

                            CoRevertToSelf();
                        }
                        else
                            LogRel(("registerVBoxSVC: CoImpersonateClient failed: %Rhrc\n", hrc));

                        /* check windows session */
                        if (SUCCEEDED(hrc) && hThreadToken != NULL)
                        {
                            hrc = E_FAIL;
                            DWORD cbSessionId = sizeof(DWORD);
                            if (GetTokenInformation(hThreadToken, TokenSessionId, (LPVOID)&dwSessionId, cbSessionId, &cbSessionId))
                            {
                                if (cbSessionId == sizeof(DWORD))
                                    hrc = S_OK;
                                else
                                    LogRel(("registerVBoxSVC: GetTokenInformation return value has invalid size\n"));
                            }
                            else
                                LogRel(("registerVBoxSVC: GetTokenInformation failed: %Rwc\n", GetLastError()));
                        }

                        /* Either the "VBoxSVC in windows session 0" feature is off or the request from VBoxSVC running
                         * in windows session 0. */
                        if (SUCCEEDED(hrc) && dwSessionId != 0)
                        {
                            /* if VBoxSVC in the Windows session 0 is not started or if it did not
                             * registered during a minute, start new one */
                            if (   pUserData->m_pidTheChosenOne == NIL_RTPROCESS
                                || GetTickCount() - pUserData->m_tickTheChosenOne > 60 * 1000)
                            {
                                uint32_t uSessionId = 0;
                                if (SetTokenInformation(hThreadToken, TokenSessionId, &uSessionId, sizeof(uint32_t)))
                                {
                                    /*
                                     * Start VBoxSVC process
                                     */
                                    char szPath[RTPATH_MAX];
                                    int vrc = RTPathAppPrivateArch(szPath, sizeof(szPath));
                                    AssertRCReturn(vrc, vrc);

                                    size_t cbBufLeft = RTPathEnsureTrailingSeparator(szPath, sizeof(szPath));
                                    AssertReturn(cbBufLeft > 0, VERR_FILENAME_TOO_LONG);

                                    char *pszNamePart = &szPath[cbBufLeft];
                                    cbBufLeft = sizeof(szPath) - cbBufLeft;

                                    static const char s_szVirtualBox_exe[] = "VBoxSVC.exe";
                                    vrc = RTStrCopy(pszNamePart, cbBufLeft, s_szVirtualBox_exe);
                                    AssertRCReturn(vrc, vrc);

                                    const char *apszArgs[] =
                                    {
                                        szPath,
                                        "--registervbox",
                                        NULL
                                    };

                                    RTPROCESS pid;
                                    vrc = RTProcCreateEx(szPath,
                                                        apszArgs,
                                                        RTENV_DEFAULT,
                                                        RTPROC_FLAGS_TOKEN_SUPPLIED,
                                                        NULL, NULL, NULL, NULL, NULL, &hThreadToken, &pid);

                                    if (RT_SUCCESS(vrc))
                                    {
                                        pUserData->m_pidTheChosenOne = pid;
                                        pUserData->m_tickTheChosenOne = GetTickCount();
                                        hrc = E_PENDING;
                                    }
                                    else
                                        LogRel(("registerVBoxSVC: Create VBoxSVC process failed: %Rrc\n", vrc));
                                }
                                else
                                {
                                    hrc = E_FAIL;
                                    LogRel(("registerVBoxSVC: SetTokenInformation failed: %ld\n", GetLastError()));
                                }
                            }
                            else /* the VBoxSVC in Windows session 0 already started */
                                hrc = E_PENDING;
                        }
                        CloseHandle(hThreadToken);
                    } /* Feature enabled */

                    if (SUCCEEDED(hrc) && dwSessionId == 0)
                    {
#endif
                        LogRel(("registerVBoxSVC: Making aPid=%u (%#x) the chosen one for user %s (%s)!\n",
                                aPid, aPid, pUserData->m_strUserSid.c_str(), pUserData->m_strUsername.c_str()));
#ifdef WITH_WATCHER
                        /* Open the process so we can watch it. */
                        HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE /*fInherit*/, aPid);
                        if (hProcess == NULL)
                            hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE /*fInherit*/, aPid);
                        if (hProcess == NULL)
                            hProcess = OpenProcess(SYNCHRONIZE, FALSE /*fInherit*/, aPid);
                        if (hProcess != NULL)
                        {
                            if (i_watchIt(pUserData, hProcess, aPid))
#endif
                            {
                                /* Make it official... */
                                pUserData->m_ptrTheChosenOne = aVBoxSVC;
                                pUserData->m_pidTheChosenOne = aPid;
                                hrc = S_OK;
                            }
#ifdef WITH_WATCHER
                            else
                            {

                                LogRel(("registerVBoxSVC: i_watchIt failed!\n"));
                                hrc = RPC_E_OUT_OF_RESOURCES;
                            }
                        }
                        else
                        {
                            LogRel(("registerVBoxSVC: OpenProcess() failed: %Rwc\n", GetLastError()));
                            hrc = E_ACCESSDENIED;
                        }
#endif
#ifdef VBOX_WITH_VBOXSVC_SESSION_0
                    }
#endif
                }
                pUserData->i_unlock();
                pUserData->i_release();
            }
            else
                hrc = E_OUTOFMEMORY;
        }
        else
            hrc = E_FAIL;
    }
    else if (   !RT_VALID_PTR(aVBoxSVC)
             || !RT_VALID_PTR(aExistingVirtualBox))
        hrc = E_INVALIDARG;
    else if (rcRpc != RPC_S_OK)
    {
        LogRel(("registerVBoxSVC: rcRpc=%d (%#x)!\n", rcRpc, rcRpc));
        hrc = E_UNEXPECTED;
    }
    else
    {
        LogRel(("registerVBoxSVC: Client PID mismatch: aPid=%d (%#x), RPC ClientPID=%zd (%#zx)\n",
                aPid, aPid, CallAttribs.ClientPID, CallAttribs.ClientPID));
        hrc = E_INVALIDARG;
    }
    LogRel2(("VirtualBoxSDS::registerVBoxSVC: returns %Rhrc\n", hrc));
    return hrc;
}


STDMETHODIMP VirtualBoxSDS::DeregisterVBoxSVC(IVBoxSVCRegistration *aVBoxSVC, LONG aPid)
{
    LogRel(("deregisterVBoxSVC: aPid=%u (%#x)\n", aPid, aPid));
    HRESULT hrc;
    if (RT_VALID_PTR(aVBoxSVC))
    {
        /* Get the client user SID and name. */
        com::Utf8Str strSid;
        com::Utf8Str strUsername;
        if (i_getClientUserSid(&strSid, &strUsername))
        {
            VBoxSDSPerUserData *pUserData = i_lookupPerUserData(strSid);
            if (pUserData)
            {
                if (aVBoxSVC == (IVBoxSVCRegistration *)pUserData->m_ptrTheChosenOne)
                {
                    LogRel(("deregisterVBoxSVC: It's the chosen one for %s (%s)!\n",
                            pUserData->m_strUserSid.c_str(), pUserData->m_strUsername.c_str()));
#ifdef WITH_WATCHER
                    i_stopWatching(pUserData, pUserData->m_pidTheChosenOne);
#endif
                    pUserData->i_unchooseTheOne(false /*fIrregular*/);
                }
                else
                    LogRel(("deregisterVBoxSVC: not the chosen one\n"));
                pUserData->i_unlock();
                pUserData->i_release();

                hrc = S_OK;
            }
            else
            {
                LogRel(("deregisterVBoxSVC: Found no user data for %s (%s) (pid %u)\n",
                        strSid.c_str(), strUsername.c_str(), aPid));
                hrc = S_OK;
            }
        }
        else
            hrc = E_FAIL;
    }
    else
        hrc = E_INVALIDARG;
    LogRel2(("VirtualBoxSDS::deregisterVBoxSVC: returns %Rhrc\n", hrc));
    return hrc;
}


STDMETHODIMP VirtualBoxSDS::LaunchVMProcess(IN_BSTR aMachine, IN_BSTR aComment, IN_BSTR aFrontend,
                                            ComSafeArrayIn(IN_BSTR, aEnvironmentChanges),
                                            IN_BSTR aCmdOptions, ULONG aSessionId, ULONG *aPid)
{
    /*
     * Convert parameters to UTF-8.
     */
    Utf8Str strMachine(aMachine);
    Utf8Str strComment(aComment);
    Utf8Str strFrontend(aFrontend);
    ArrayBSTRInConverter aStrEnvironmentChanges(ComSafeArrayInArg(aEnvironmentChanges));
    Utf8Str strCmdOptions(aCmdOptions);

    /*
     * Impersonate the caller.
     */
    HRESULT hrc = CoImpersonateClient();
    if (SUCCEEDED(hrc))
    {
        try
        {
            /*
             * Try launch the VM process as the client.
             */
            RTPROCESS pid;
            AssertCompile(sizeof(aSessionId) == sizeof(uint32_t));
            int vrc = ::MachineLaunchVMCommonWorker(strMachine, strComment, strFrontend, aStrEnvironmentChanges.array(),
                                                    strCmdOptions, Utf8Str(),
                                                    RTPROC_FLAGS_AS_IMPERSONATED_TOKEN | RTPROC_FLAGS_SERVICE
                                                    | RTPROC_FLAGS_PROFILE | RTPROC_FLAGS_DESIRED_SESSION_ID,
                                                    &aSessionId, pid);
            if (RT_SUCCESS(vrc))
            {
                *aPid = (ULONG)pid;
                LogRel(("VirtualBoxSDS::LaunchVMProcess: launchVM succeeded\n"));
            }
            else if (vrc == VERR_INVALID_PARAMETER)
            {
                hrc = E_INVALIDARG;
                LogRel(("VirtualBoxSDS::LaunchVMProcess: launchVM failed: %Rhrc\n", hrc));
            }
            else
            {
                hrc = VBOX_E_IPRT_ERROR;
                LogRel(("VirtualBoxSDS::LaunchVMProcess: launchVM failed: %Rhrc (%Rrc)\n", hrc, vrc));
            }
        }
        catch (...)
        {
            hrc = E_UNEXPECTED;
        }
        CoRevertToSelf();
    }
    else
        LogRel(("VirtualBoxSDS::LaunchVMProcess: CoImpersonateClient failed: %Rhrc\n", hrc));
    return hrc;
}


/*********************************************************************************************************************************
*   VirtualBoxSDS - Internal Methods                                                                                             *
*********************************************************************************************************************************/

/*static*/ bool VirtualBoxSDS::i_getClientUserSid(com::Utf8Str *a_pStrSid, com::Utf8Str *a_pStrUsername)
{
    bool fRet = false;
    a_pStrSid->setNull();
    a_pStrUsername->setNull();

    HRESULT hrc = CoImpersonateClient();
    if (SUCCEEDED(hrc))
    {
        HANDLE hToken = INVALID_HANDLE_VALUE;
        if (::OpenThreadToken(GetCurrentThread(), TOKEN_READ, TRUE /*OpenAsSelf*/, &hToken))
        {
            CoRevertToSelf();

            union
            {
                TOKEN_USER  TokenUser;
                uint8_t     abPadding[SECURITY_MAX_SID_SIZE + 256];
                WCHAR       wszUsername[UNLEN + 1];
            } uBuf;
            RT_ZERO(uBuf);
            DWORD cbActual = 0;
            if (::GetTokenInformation(hToken, TokenUser, &uBuf, sizeof(uBuf), &cbActual))
            {
                WCHAR *pwszString;
                if (ConvertSidToStringSidW(uBuf.TokenUser.User.Sid, &pwszString))
                {
                    try
                    {
                        *a_pStrSid = pwszString;
                        a_pStrSid->toUpper(); /* (just to be on the safe side) */
                        fRet = true;
                    }
                    catch (std::bad_alloc &)
                    {
                        LogRel(("i_GetClientUserSID: std::bad_alloc setting rstrSid.\n"));
                    }
                    LocalFree((HLOCAL)pwszString);

                    /*
                     * Get the username too.  We don't care if this step fails.
                     */
                    if (fRet)
                    {
                        WCHAR           wszUsername[UNLEN * 2 + 1];
                        DWORD           cwcUsername = RT_ELEMENTS(wszUsername);
                        WCHAR           wszDomain[UNLEN * 2 + 1];
                        DWORD           cwcDomain = RT_ELEMENTS(wszDomain);
                        SID_NAME_USE    enmNameUse;
                        if (LookupAccountSidW(NULL, uBuf.TokenUser.User.Sid, wszUsername, &cwcUsername,
                                              wszDomain, &cwcDomain, &enmNameUse))
                        {
                            wszUsername[RT_ELEMENTS(wszUsername) - 1] = '\0';
                            wszDomain[RT_ELEMENTS(wszDomain) - 1] = '\0';
                            try
                            {
                                *a_pStrUsername = wszDomain;
                                a_pStrUsername->append('/');
                                a_pStrUsername->append(Utf8Str(wszUsername));
                            }
                            catch (std::bad_alloc &)
                            {
                                LogRel(("i_GetClientUserSID: std::bad_alloc setting rStrUsername.\n"));
                                a_pStrUsername->setNull();
                            }
                        }
                        else
                            LogRel(("i_GetClientUserSID: LookupAccountSidW failed: %u/%x (cwcUsername=%u, cwcDomain=%u)\n",
                                   GetLastError(), cwcUsername, cwcDomain));
                    }
                }
                else
                    LogRel(("i_GetClientUserSID: ConvertSidToStringSidW failed: %u\n", GetLastError()));
            }
            else
                LogRel(("i_GetClientUserSID: GetTokenInformation/TokenUser failed: %u\n", GetLastError()));
            CloseHandle(hToken);
        }
        else
        {
            CoRevertToSelf();
            LogRel(("i_GetClientUserSID: OpenThreadToken failed: %u\n", GetLastError()));
        }
    }
    else
        LogRel(("i_GetClientUserSID: CoImpersonateClient failed: %Rhrc\n", hrc));
    return fRet;
}


/**
 * Looks up the given user.
 *
 * @returns Pointer to the LOCKED and RETAINED per user data.
 *          NULL if not found.
 * @param   a_rStrUserSid   The user SID.
 */
VBoxSDSPerUserData *VirtualBoxSDS::i_lookupPerUserData(com::Utf8Str const &a_rStrUserSid)
{
    int vrc = RTCritSectRwEnterShared(&m_MapCritSect);
    if (RT_SUCCESS(vrc))
    {

        UserDataMap_T::iterator it = m_UserDataMap.find(a_rStrUserSid);
        if (it != m_UserDataMap.end())
        {
            VBoxSDSPerUserData *pUserData = it->second;
            pUserData->i_retain();

            RTCritSectRwLeaveShared(&m_MapCritSect);

            pUserData->i_lock();
            return pUserData;
        }

        RTCritSectRwLeaveShared(&m_MapCritSect);
    }
    return NULL;
}


/**
 * Looks up the given user, creating it if not found
 *
 * @returns Pointer to the LOCKED and RETAINED per user data.
 *          NULL on allocation error.
 * @param   a_rStrUserSid   The user SID.
 * @param   a_rStrUsername  The user name if available.
 */
VBoxSDSPerUserData *VirtualBoxSDS::i_lookupOrCreatePerUserData(com::Utf8Str const &a_rStrUserSid,
                                                               com::Utf8Str const &a_rStrUsername)
{
    /*
     * Try do a simple lookup first.
     */
    VBoxSDSPerUserData *pUserData = i_lookupPerUserData(a_rStrUserSid);
    if (!pUserData)
    {
        /*
         * SID is not in map, create a new one.
         */
        try
        {
            pUserData = new VBoxSDSPerUserData(a_rStrUserSid, a_rStrUsername);
        }
        catch (std::bad_alloc &)
        {
            pUserData = NULL;
        }
        if (pUserData)
        {
            /*
             * Insert it.  We must check if someone raced us here.
             */
            VBoxSDSPerUserData *pUserDataFree = pUserData;
            pUserData->i_lock();

            int vrc = RTCritSectRwEnterExcl(&m_MapCritSect);
            if (RT_SUCCESS(vrc))
            {

                UserDataMap_T::iterator it = m_UserDataMap.find(a_rStrUserSid);
                if (it == m_UserDataMap.end())
                {
                    try
                    {
                        m_UserDataMap[a_rStrUserSid] = pUserData;
                        pUserData->i_retain();
                    }
                    catch (std::bad_alloc &)
                    {
                        pUserData = NULL;
                    }
                }
                else
                    pUserData = NULL;

                RTCritSectRwLeaveExcl(&m_MapCritSect);

                if (pUserData)
                    LogRel(("i_lookupOrCreatePerUserData: Created new entry for %s (%s)\n",
                            pUserData->m_strUserSid.c_str(), pUserData->m_strUsername.c_str() ));
                else
                {
                    pUserDataFree->i_unlock();
                    delete pUserDataFree;
                }
            }
        }
    }

    return pUserData;
}


#ifdef WITH_WATCHER
/**
 * Data about what's being watched.
 */
typedef struct VBoxSDSWatcherData
{
    /** The per-user data (referenced). */
    VBoxSDSPerUserData *pUserData;
    /** The chosen one revision number (for handling an almost impossible race
     * where a client terminates while making a deregistration call). */
    uint32_t            iRevision;
    /** The PID we're watching. */
    RTPROCESS           pid;

    /** Sets the members to NULL values. */
    void setNull()
    {
        pUserData = NULL;
        iRevision = UINT32_MAX;
        pid       = NIL_RTPROCESS;
    }
} VBoxSDSWatcherData;

/**
 * Per watcher data.
 */
typedef struct VBoxSDSWatcher
{
    /** Pointer to the VBoxSDS instance. */
    VirtualBoxSDS      *pVBoxSDS;
    /** The thread handle. */
    RTTHREAD            hThread;
    /** Number of references to this structure. */
    uint32_t volatile   cRefs;
    /** Set if the thread should shut down. */
    bool volatile       fShutdown;
    /** Number of pending items in the todo array. */
    uint32_t            cTodos;
    /** The watcher number. */
    uint32_t            iWatcher;
    /** The number of handles once TODOs have been taken into account. */
    uint32_t            cHandlesEffective;
    /** Number of handles / user data items being monitored. */
    uint32_t            cHandles;
    /** Array of handles.
     * The zero'th entry is the event semaphore use to signal the thread. */
    HANDLE              aHandles[MAXIMUM_WAIT_OBJECTS];
    /** Array the runs parallel to aHandles with the VBoxSVC data. */
    VBoxSDSWatcherData  aData[MAXIMUM_WAIT_OBJECTS];
    /** Pending changes. */
    struct
    {
        /** If NULL the data is being removed, otherwise it's being added and
         * this is the process handle to watch for termination. */
        HANDLE              hProcess;
        /** The data about what's being watched. */
        VBoxSDSWatcherData  Data;
    }                   aTodos[MAXIMUM_WAIT_OBJECTS * 4];


    /** Helper for removing a handle & data table entry. */
    uint32_t removeHandle(uint32_t a_iEntry, uint32_t a_cHandles)
    {
        uint32_t cToShift = a_cHandles - a_iEntry - 1;
        if (cToShift > 0)
        {
            memmove(&aData[a_iEntry], &aData[a_iEntry + 1], sizeof(aData[0]) * cToShift);
            memmove(&aHandles[a_iEntry], &aHandles[a_iEntry + 1], sizeof(aHandles[0]) * cToShift);
        }
        a_cHandles--;
        aHandles[a_cHandles] = NULL;
        aData[a_cHandles].setNull();

        return a_cHandles;
    }
} VBoxSDSWatcher;



/**
 * Watcher thread.
 */
/*static*/ DECLCALLBACK(int) VirtualBoxSDS::i_watcherThreadProc(RTTHREAD hSelf, void *pvUser)
{
    VBoxSDSWatcher *pThis    = (VBoxSDSWatcher *)pvUser;
    VirtualBoxSDS  *pVBoxSDS = pThis->pVBoxSDS;
    RT_NOREF(hSelf);

    /*
     * This thread may release references to IVBoxSVCRegistration objects.
     */
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    /*
     * The loop.
     */
    RTCritSectEnter(&pVBoxSDS->m_WatcherCritSect);
    while (!pThis->fShutdown)
    {
        /*
         * Deal with the todo list.
         */
        uint32_t cHandles = pThis->cHandles;
        uint32_t cTodos   = pThis->cTodos;

        for (uint32_t i = 0; i < cTodos; i++)
        {
            VBoxSDSPerUserData *pUserData = pThis->aTodos[i].Data.pUserData;
            AssertContinue(pUserData);
            if (pThis->aTodos[i].hProcess != NULL)
            {
                /* Add: */
                AssertLogRelMsgBreakStmt(cHandles < RT_ELEMENTS(pThis->aHandles),
                                         ("cHandles=%u cTodos=%u i=%u iWatcher=%u\n", cHandles, cTodos, i, pThis->iWatcher),
                                         pThis->fShutdown = true);
                pThis->aHandles[cHandles] = pThis->aTodos[i].hProcess;
                pThis->aData[cHandles]    = pThis->aTodos[i].Data;
                cHandles++;
            }
            else
            {
                /* Remove: */
                uint32_t cRemoved = 0;
                uint32_t j        = cHandles;
                while (j-- > 1)
                    if (pThis->aData[j].pUserData == pUserData)
                    {
                        cHandles = pThis->removeHandle(j, cHandles);
                        pUserData->i_release();
                        cRemoved++;
                    }
                if (cRemoved != 1)
                    LogRel(("i_watcherThreadProc/#%u: Warning! cRemoved=%u\n", pThis->iWatcher, cRemoved));
            }
            /* Zap the entry in case we assert and leave further up. */
            pThis->aTodos[i].Data.setNull();
            pThis->aTodos[i].hProcess = NULL;
        }

        Assert(cHandles > 0 && cHandles <= RT_ELEMENTS(pThis->aHandles));
        pThis->cHandles          = cHandles;
        pThis->cHandlesEffective = cHandles;
        pThis->cTodos            = 0;

        if (pThis->fShutdown)
            break;

        /*
         * Wait.
         */
        RTCritSectLeave(&pVBoxSDS->m_WatcherCritSect);

        LogRel(("i_watcherThreadProc/#%u: Waiting on %u handles...\n", pThis->iWatcher, cHandles));
        DWORD const dwWait = WaitForMultipleObjects(cHandles, pThis->aHandles, FALSE /*fWaitAll*/, INFINITE);
        LogRel(("i_watcherThreadProc/#%u: ... wait returned: %#x (%d)\n", pThis->iWatcher, dwWait, dwWait));

        uint32_t const iHandle = dwWait - WAIT_OBJECT_0;
        if (iHandle < cHandles && iHandle > 0)
        {
            /*
             * A VBoxSVC process has terminated.
             *
             * Note! We need to take the user data lock before the watcher one here.
             */
            VBoxSDSPerUserData * const pUserData = pThis->aData[iHandle].pUserData;
            uint32_t const             iRevision = pThis->aData[iHandle].iRevision;
            RTPROCESS const            pid       = pThis->aData[iHandle].pid;

            pUserData->i_lock();
            RTCritSectEnter(&pVBoxSDS->m_WatcherCritSect);

            DWORD dwExit = 0;
            GetExitCodeProcess(pThis->aHandles[iHandle], &dwExit);
            LogRel(("i_watcherThreadProc/#%u: %s: PID %u/%#x termination detected: %d (%#x)  [iRev=%u, cur %u]\n",
                    pThis->iWatcher, pUserData->m_strUsername.c_str(), pid, pid, dwExit, dwExit,
                    iRevision, pUserData->m_iTheChosenOneRevision));

            /* Remove it from the handle array. */
            CloseHandle(pThis->aHandles[iHandle]);
            pThis->cHandles = cHandles = pThis->removeHandle(iHandle, cHandles);
            pThis->cHandlesEffective -= 1;

            /* If the process we were watching is still the current chosen one,
               unchoose it and decrement the client count.  Otherwise we were subject
               to a deregistration/termination race (unlikely). */
            if (pUserData->m_iTheChosenOneRevision == iRevision)
            {
                pUserData->i_unchooseTheOne(true /*fIrregular*/);
                pUserData->i_unlock();
                pVBoxSDS->i_decrementClientCount();
            }
            else
                pUserData->i_unlock();
            pUserData->i_release();
        }
        else
        {
            RTCritSectEnter(&pThis->pVBoxSDS->m_WatcherCritSect);
            AssertLogRelMsgBreak(iHandle == 0 || dwWait == WAIT_TIMEOUT,
                                 ("dwWait=%u (%#x) cHandles=%u\n", dwWait, dwWait, cHandles));
        }
    }

    RTCritSectLeave(&pThis->pVBoxSDS->m_WatcherCritSect);

    /*
     * In case we quit w/o being told, signal i_watchIt that we're out of action.
     */
    pThis->fShutdown = true;

    /*
     * Release all our data on the way out.
     */
    uint32_t i = pThis->cHandles;
    while (i-- > 1)
    {
        if (pThis->aData[i].pUserData)
        {
            pThis->aData[i].pUserData->i_release();
            pThis->aData[i].pUserData = NULL;
        }
        if (pThis->aHandles[i])
        {
            CloseHandle(pThis->aHandles[i]);
            pThis->aHandles[i] = NULL;
        }
    }
    if (pThis->aHandles[0])
    {
        CloseHandle(pThis->aHandles[0]);
        pThis->aHandles[0] = NULL;
    }

    i = pThis->cTodos;
    pThis->cTodos = 0;
    while (i-- > 0)
    {
        if (pThis->aTodos[i].Data.pUserData)
        {
            pThis->aTodos[i].Data.pUserData->i_release();
            pThis->aTodos[i].Data.pUserData = NULL;
        }
        if (pThis->aTodos[i].hProcess)
        {
            CloseHandle(pThis->aTodos[i].hProcess);
            pThis->aTodos[i].hProcess = NULL;
        }
    }

    if (ASMAtomicDecU32(&pThis->cRefs) == 0)
        RTMemFree(pThis);

    return VINF_SUCCESS;
}


/**
 * Starts monitoring a VBoxSVC process.
 *
 * @param   pUserData   The user which chosen VBoxSVC should be watched.
 * @param   hProcess    Handle to the VBoxSVC process.  Consumed.
 * @param   pid         The VBoxSVC PID.
 * @returns Success indicator.
 */
bool VirtualBoxSDS::i_watchIt(VBoxSDSPerUserData *pUserData, HANDLE hProcess, RTPROCESS pid)
{
    RTCritSectEnter(&m_WatcherCritSect);

    /*
     * Find a watcher with capacity left over (we save 8 entries for removals).
     */
    for (uint32_t i = 0; i < m_cWatchers; i++)
    {
        VBoxSDSWatcher *pWatcher = m_papWatchers[i];
        if (   pWatcher->cHandlesEffective < RT_ELEMENTS(pWatcher->aHandles)
            && !pWatcher->fShutdown)
        {
            uint32_t iTodo = pWatcher->cTodos;
            if (iTodo + 8 < RT_ELEMENTS(pWatcher->aTodos))
            {
                pWatcher->aTodos[iTodo].hProcess       = hProcess;
                pWatcher->aTodos[iTodo].Data.pUserData = pUserData;
                pWatcher->aTodos[iTodo].Data.iRevision = ++pUserData->m_iTheChosenOneRevision;
                pWatcher->aTodos[iTodo].Data.pid       = pid;
                pWatcher->cTodos = iTodo + 1;

                pUserData->m_iWatcher = pWatcher->iWatcher;
                pUserData->i_retain();

                BOOL fRc = SetEvent(pWatcher->aHandles[0]);
                AssertLogRelMsg(fRc, ("SetEvent() failed: %u\n", GetLastError()));
                LogRel(("i_watchIt: Added process to watcher #%u: %RTbool\n", pWatcher->iWatcher, fRc));

                i_incrementClientCount();
                RTCritSectLeave(&m_WatcherCritSect);
                RTThreadYield();
                return true;
            }
        }
    }

    /*
     * No watcher with capacity was found, so create a new one with
     * the user/handle prequeued.
     */
    void *pvNew = RTMemRealloc(m_papWatchers, sizeof(m_papWatchers[0]) * (m_cWatchers + 1));
    if (pvNew)
    {
        m_papWatchers = (VBoxSDSWatcher **)pvNew;
        VBoxSDSWatcher *pWatcher = (VBoxSDSWatcher *)RTMemAllocZ(sizeof(*pWatcher));
        if (pWatcher)
        {
            for (uint32_t i = 0; i < RT_ELEMENTS(pWatcher->aData); i++)
                pWatcher->aData[i].setNull();
            for (uint32_t i = 0; i < RT_ELEMENTS(pWatcher->aTodos); i++)
                pWatcher->aTodos[i].Data.setNull();

            pWatcher->pVBoxSDS          = this;
            pWatcher->iWatcher          = m_cWatchers;
            pWatcher->cRefs             = 2;
            pWatcher->cHandlesEffective = 2;
            pWatcher->cHandles          = 2;
            pWatcher->aHandles[0]       = CreateEventW(NULL, FALSE /*fManualReset*/, FALSE /*fInitialState*/,  NULL);
            if (pWatcher->aHandles[0])
            {
                /* Add incoming VBoxSVC process in slot #1: */
                pWatcher->aHandles[1]        = hProcess;
                pWatcher->aData[1].pid       = pid;
                pWatcher->aData[1].pUserData = pUserData;
                pWatcher->aData[1].iRevision = ++pUserData->m_iTheChosenOneRevision;
                pUserData->i_retain();
                pUserData->m_iWatcher = pWatcher->iWatcher;

                /* Start the thread and we're good. */
                m_papWatchers[m_cWatchers++] = pWatcher;
                int vrc = RTThreadCreateF(&pWatcher->hThread, i_watcherThreadProc, pWatcher, 0, RTTHREADTYPE_MAIN_WORKER,
                                          RTTHREADFLAGS_WAITABLE, "watcher%u", pWatcher->iWatcher);
                if (RT_SUCCESS(vrc))
                {
                    LogRel(("i_watchIt: Created new watcher #%u\n", m_cWatchers));

                    i_incrementClientCount();
                    RTCritSectLeave(&m_WatcherCritSect);
                    return true;
                }

                LogRel(("i_watchIt: Error starting watcher thread: %Rrc\n", vrc));
                m_papWatchers[--m_cWatchers] = NULL;

                pUserData->m_iWatcher = UINT32_MAX;
                pUserData->i_release();
                CloseHandle(pWatcher->aHandles[0]);
            }
            else
                LogRel(("i_watchIt: CreateEventW failed: %u\n", GetLastError()));
            RTMemFree(pWatcher);
        }
        else
            LogRel(("i_watchIt: failed to allocate watcher structure!\n"));
    }
    else
        LogRel(("i_watchIt: Failed to grow watcher array to %u entries!\n", m_cWatchers + 1));

    RTCritSectLeave(&m_WatcherCritSect);
    CloseHandle(hProcess);
    return false;
}


/**
 * Stops monitoring a VBoxSVC process.
 *
 * @param   pUserData   The user which chosen VBoxSVC should be watched.
 * @param   pid         The VBoxSVC PID.
 */
void VirtualBoxSDS::i_stopWatching(VBoxSDSPerUserData *pUserData, RTPROCESS pid)
{
    /*
     * Add a remove order in the watcher's todo queue.
     */
    RTCritSectEnter(&m_WatcherCritSect);
    for (uint32_t iRound = 0; ; iRound++)
    {
        uint32_t const iWatcher = pUserData->m_iWatcher;
        if (iWatcher < m_cWatchers)
        {
            VBoxSDSWatcher *pWatcher = m_papWatchers[pUserData->m_iWatcher];
            if (!pWatcher->fShutdown)
            {
                /*
                 * Remove duplicate todo entries.
                 */
                bool fAddIt = true;
                uint32_t iTodo = pWatcher->cTodos;
                while (iTodo-- > 0)
                    if (pWatcher->aTodos[iTodo].Data.pUserData == pUserData)
                    {
                        if (pWatcher->aTodos[iTodo].hProcess == NULL)
                            fAddIt = true;
                        else
                        {
                            fAddIt = false;
                            CloseHandle(pWatcher->aTodos[iTodo].hProcess);
                        }
                        uint32_t const cTodos = --pWatcher->cTodos;
                        uint32_t const cToShift = cTodos - iTodo;
                        if (cToShift > 0)
                            memmove(&pWatcher->aTodos[iTodo], &pWatcher->aTodos[iTodo + 1], sizeof(pWatcher->aTodos[0]) * cToShift);
                        pWatcher->aTodos[cTodos].hProcess = NULL;
                        pWatcher->aTodos[cTodos].Data.setNull();
                    }

                /*
                 * Did we just eliminated the add and cancel out this operation?
                 */
                if (!fAddIt)
                {
                    pUserData->m_iWatcher = UINT32_MAX;
                    pUserData->m_iTheChosenOneRevision++;
                    i_decrementClientCount();

                    RTCritSectLeave(&m_WatcherCritSect);
                    RTThreadYield();
                    return;
                }

                /*
                 * No we didn't.  So, try append a removal item.
                 */
                iTodo = pWatcher->cTodos;
                if (iTodo < RT_ELEMENTS(pWatcher->aTodos))
                {
                    pWatcher->aTodos[iTodo].hProcess       = NULL;
                    pWatcher->aTodos[iTodo].Data.pUserData = pUserData;
                    pWatcher->aTodos[iTodo].Data.pid       = pid;
                    pWatcher->aTodos[iTodo].Data.iRevision = pUserData->m_iTheChosenOneRevision++;
                    pWatcher->cTodos = iTodo + 1;
                    SetEvent(pWatcher->aHandles[0]);

                    pUserData->m_iWatcher = UINT32_MAX;
                    i_decrementClientCount();

                    RTCritSectLeave(&m_WatcherCritSect);
                    RTThreadYield();
                    return;
                }
            }
            else
            {
                LogRel(("i_stopWatching: Watcher #%u has shut down.\n", iWatcher));
                break;
            }

            /*
             * Todo queue is full.  Sleep a little and let the watcher process it.
             */
            LogRel(("i_stopWatching: Watcher #%u todo queue is full! (round #%u)\n", iWatcher, iRound));

            uint32_t const iTheChosenOneRevision = pUserData->m_iTheChosenOneRevision;
            SetEvent(pWatcher->aHandles[0]);

            RTCritSectLeave(&m_WatcherCritSect);
            RTThreadSleep(1 + (iRound & 127));
            RTCritSectEnter(&m_WatcherCritSect);

            AssertLogRelMsgBreak(pUserData->m_iTheChosenOneRevision == iTheChosenOneRevision,
                                 ("Impossible! m_iTheChosenOneRevision changed %#x -> %#x!\n",
                                  iTheChosenOneRevision, pUserData->m_iTheChosenOneRevision));
        }
        else
        {
            AssertLogRelMsg(pUserData->m_iWatcher == UINT32_MAX,
                            ("Impossible! iWatcher=%d m_cWatcher=%u\n", iWatcher, m_cWatchers));
            break;
        }
    }
    RTCritSectLeave(&m_WatcherCritSect);
}


/**
 * Shutdowns all the watchers.
 */
void VirtualBoxSDS::i_shutdownAllWatchers(void)
{
    LogRel(("i_shutdownAllWatchers: %u watchers\n", m_cWatchers));

    /* Notify them all. */
    uint32_t i = m_cWatchers;
    while (i-- > 0)
    {
        ASMAtomicWriteBool(&m_papWatchers[i]->fShutdown, true);
        SetEvent(m_papWatchers[i]->aHandles[0]);
    }

    /* Wait for them to complete and destroy their data. */
    i = m_cWatchers;
    m_cWatchers = 0;
    while (i-- > 0)
    {
        VBoxSDSWatcher *pWatcher = m_papWatchers[i];
        if (pWatcher)
        {
            m_papWatchers[i] = NULL;

            int vrc = RTThreadWait(pWatcher->hThread, RT_MS_1MIN / 2, NULL);
            if (RT_SUCCESS(vrc))
                pWatcher->hThread = NIL_RTTHREAD;
            else
                LogRel(("i_shutdownAllWatchers: RTThreadWait failed on #%u: %Rrc\n", i, vrc));

            if (ASMAtomicDecU32(&pWatcher->cRefs) == 0)
                RTMemFree(pWatcher);
        }
    }
}


/**
 * Increments the VBoxSVC client count.
 */
void VirtualBoxSDS::i_incrementClientCount()
{
    Assert(RTCritSectIsOwner(&m_WatcherCritSect));
    uint32_t cClients = ++m_cVBoxSvcProcesses;
    Assert(cClients < 4096);
    VBoxSDSNotifyClientCount(cClients);
}


/**
 * Decrements the VBoxSVC client count.
 */
void VirtualBoxSDS::i_decrementClientCount()
{
    Assert(RTCritSectIsOwner(&m_WatcherCritSect));
    uint32_t cClients = --m_cVBoxSvcProcesses;
    Assert(cClients < 4096);
    VBoxSDSNotifyClientCount(cClients);
}


#endif /* WITH_WATCHER */


/* vi: set tabstop=4 shiftwidth=4 expandtab: */
