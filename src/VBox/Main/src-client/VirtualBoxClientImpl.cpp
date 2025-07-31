/* $Id: VirtualBoxClientImpl.cpp 110372 2025-07-23 10:03:33Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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

#define LOG_GROUP LOG_GROUP_MAIN_VIRTUALBOXCLIENT
#include "LoggingNew.h"

#include "VirtualBoxClientImpl.h"

#include "AutoCaller.h"
#include "VBoxEvents.h"
#include "VBox/com/ErrorInfo.h"
#include "VBox/com/listeners.h"

#include <iprt/asm.h>
#include <iprt/thread.h>
#include <iprt/critsect.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/cpp/utils.h>
#include <iprt/utf16.h>
#include <iprt/err.h>
#ifdef RT_OS_WINDOWS
# include <iprt/ldr.h>
# include <msi.h>
# include <WbemIdl.h>
#endif

#include <new>


/** Waiting time between probing whether VBoxSVC is alive. */
#define VBOXCLIENT_DEFAULT_INTERVAL 30000


/** Initialize instance counter class variable */
uint32_t VirtualBoxClient::g_cInstances = 0;

LONG VirtualBoxClient::s_cUnnecessaryAtlModuleLocks = 0;

#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
extern TrackedObjectsCollector gTrackedObjectsCollector;
#endif

#ifdef VBOX_WITH_MAIN_NLS

/* listener class for language updates */
class VBoxEventListener
{
public:
    VBoxEventListener()
    {}


    HRESULT init(void *)
    {
        return S_OK;
    }

    HRESULT init()
    {
        return S_OK;
    }

    void uninit()
    {
    }

    virtual ~VBoxEventListener()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch(aType)
        {
            case VBoxEventType_OnLanguageChanged:
            {
                /*
                 * Proceed with uttmost care as we might be racing com::Shutdown()
                 * and have the ground open up beneath us.
                 */
                LogFunc(("VBoxEventType_OnLanguageChanged\n"));
                VirtualBoxTranslator *pTranslator = VirtualBoxTranslator::tryInstance();
                if (pTranslator)
                {
                    ComPtr<ILanguageChangedEvent> pEvent = aEvent;
                    Assert(pEvent);

                    /* This call may fail if we're racing COM shutdown. */
                    com::Bstr bstrLanguageId;
                    HRESULT hrc = pEvent->COMGETTER(LanguageId)(bstrLanguageId.asOutParam());
                    if (SUCCEEDED(hrc))
                    {
                        try
                        {
                            com::Utf8Str strLanguageId(bstrLanguageId);
                            LogFunc(("New language ID: %s\n", strLanguageId.c_str()));
                            pTranslator->i_loadLanguage(strLanguageId.c_str());
                        }
                        catch (std::bad_alloc &)
                        {
                            LogFunc(("Caught bad_alloc"));
                        }
                    }
                    else
                        LogFunc(("Failed to get new language ID: %Rhrc\n", hrc));

                    pTranslator->release();
                }
                break;
            }

            default:
              AssertFailed();
        }

        return S_OK;
    }
};

typedef ListenerImpl<VBoxEventListener> VBoxEventListenerImpl;

VBOX_LISTENER_DECLARE(VBoxEventListenerImpl)

#endif /* VBOX_WITH_MAIN_NLS */

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

/** @relates VirtualBoxClient::FinalConstruct() */
HRESULT VirtualBoxClient::FinalConstruct()
{
    HRESULT hrc = init();
    BaseFinalConstruct();
    return hrc;
}

void VirtualBoxClient::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the VirtualBoxClient object.
 *
 * @returns COM result indicator
 */
HRESULT VirtualBoxClient::init()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Important: DO NOT USE any kind of "early return" (except the single
     * one above, checking the init span success) in this method. It is vital
     * for correct error handling that it has only one point of return, which
     * does all the magic on COM to signal object creation success and
     * reporting the error later for every API method. COM translates any
     * unsuccessful object creation to REGDB_E_CLASSNOTREG errors or similar
     * unhelpful ones which cause us a lot of grief with troubleshooting. */

    HRESULT hrc = S_OK;
    try
    {
        if (ASMAtomicIncU32(&g_cInstances) != 1)
            AssertFailedStmt(throw setError(E_FAIL, "Attempted to create more than one VirtualBoxClient instance"));

        mData.m_ThreadWatcher = NIL_RTTHREAD;
        mData.m_SemEvWatcher = NIL_RTSEMEVENT;

        hrc = mData.m_pVirtualBox.createLocalObject(CLSID_VirtualBox);
        if (FAILED(hrc))
#ifdef RT_OS_WINDOWS
            throw i_investigateVirtualBoxObjectCreationFailure(hrc);
#else
            throw hrc;
#endif

#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
        /////////////// Try to start Object tracker thread as earlier as possible ///////////////
        {
            int vrc = VERR_GENERAL_FAILURE;
            if (gTrackedObjectsCollector.init())
            {
                try
                {
                    mData.m_objectTrackerTask = new ObjectTracker();
                    if (mData.m_objectTrackerTask->init()) // some init procedure
                        vrc = mData.m_objectTrackerTask->createThread(); // mData->m_objectTrackerTask1 is not consumed
                }
                catch (...)
                {
                    LogRel(("Exception during starting the Object tracker thread\n"));
                    if (mData.m_objectTrackerTask)
                    {
                        delete mData.m_objectTrackerTask;
                        mData.m_objectTrackerTask = NULL;
                    }
                    vrc = VERR_UNEXPECTED_EXCEPTION;
                }
            }

            if (RT_SUCCESS(vrc))
                LogRel(("Successfully started the Object tracker thread\n"));
            else
                LogRel(("Failed to start the Object tracker thread (%Rrc)\n", vrc));
        }
#endif

        /* VirtualBox error return is postponed to method calls, fetch it. */
        ULONG rev;
        hrc = mData.m_pVirtualBox->COMGETTER(Revision)(&rev);
        if (FAILED(hrc))
            throw hrc;

        hrc = unconst(mData.m_pEventSource).createObject();
        AssertComRCThrow(hrc, setError(hrc, "Could not create EventSource for VirtualBoxClient"));
        hrc = mData.m_pEventSource->init();
        AssertComRCThrow(hrc, setError(hrc, "Could not initialize EventSource for VirtualBoxClient"));

        /* HACK ALERT! This is for DllCanUnloadNow(). */
        s_cUnnecessaryAtlModuleLocks++;
        AssertMsg(s_cUnnecessaryAtlModuleLocks == 1, ("%d\n", s_cUnnecessaryAtlModuleLocks));

        int vrc;
#ifdef VBOX_WITH_MAIN_NLS
        /* Create the translator singelton (must work) and try load translations (non-fatal). */
        mData.m_pVBoxTranslator = VirtualBoxTranslator::instance();
        if (mData.m_pVBoxTranslator == NULL)
            throw setError(VBOX_E_IPRT_ERROR, "Failed to create translator instance");

        char szNlsPath[RTPATH_MAX];
        vrc = RTPathAppPrivateNoArch(szNlsPath, sizeof(szNlsPath));
        if (RT_SUCCESS(vrc))
            vrc = RTPathAppend(szNlsPath, sizeof(szNlsPath), "nls" RTPATH_SLASH_STR "VirtualBoxAPI");

        if (RT_SUCCESS(vrc))
        {
            vrc = mData.m_pVBoxTranslator->registerTranslation(szNlsPath, true, &mData.m_pTrComponent);
            if (RT_SUCCESS(vrc))
            {
                hrc = i_reloadApiLanguage();
                if (SUCCEEDED(hrc))
                    i_registerEventListener(); /* for updates */
                else
                    LogRelFunc(("i_reloadApiLanguage failed: %Rhrc\n", hrc));
            }
            else
                LogRelFunc(("Register translation failed: %Rrc\n", vrc));
        }
        else
            LogRelFunc(("Path constructing failed: %Rrc\n", vrc));
#endif
        /* Setting up the VBoxSVC watcher thread. If anything goes wrong here it
         * is not considered important enough to cause any sort of visible
         * failure. The monitoring will not be done, but that's all. */
        vrc = RTSemEventCreate(&mData.m_SemEvWatcher);
        if (RT_FAILURE(vrc))
        {
            mData.m_SemEvWatcher = NIL_RTSEMEVENT;
            AssertRCStmt(vrc, throw setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Failed to create semaphore (vrc=%Rrc)"), vrc));
        }

        vrc = RTThreadCreate(&mData.m_ThreadWatcher, SVCWatcherThread, this, 0,
                             RTTHREADTYPE_INFREQUENT_POLLER, RTTHREADFLAGS_WAITABLE, "VBoxSVCWatcher");
        if (RT_FAILURE(vrc))
        {
            RTSemEventDestroy(mData.m_SemEvWatcher);
            mData.m_SemEvWatcher = NIL_RTSEMEVENT;
            AssertRCStmt(vrc, throw setErrorBoth(VBOX_E_IPRT_ERROR, vrc,  tr("Failed to create watcher thread (vrc=%Rrc)"), vrc));
        }
    }
    catch (HRESULT err)
    {
        /* we assume that error info is set by the thrower */
        hrc = err;
    }
    catch (...)
    {
        hrc = VirtualBoxBase::handleUnexpectedExceptions(this, RT_SRC_POS);
    }

    /* Confirm a successful initialization when it's the case. Must be last,
     * as on failure it will uninitialize the object. */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed(hrc);

    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    /* Unconditionally return success, because the error return is delayed to
     * the attribute/method calls through the InitFailed object state. */
    return S_OK;
}

#ifdef RT_OS_WINDOWS

/**
 * Looks into why we failed to create the VirtualBox object.
 *
 * @returns hrcCaller thru setError.
 * @param   hrcCaller   The failure status code.
 */
HRESULT VirtualBoxClient::i_investigateVirtualBoxObjectCreationFailure(HRESULT hrcCaller)
{
    HRESULT hrc;

# ifdef VBOX_WITH_SDS
    /*
     * Check that the VBoxSDS service is configured to run as LocalSystem and is enabled.
     */
    WCHAR    wszBuffer[256];
    uint32_t uStartType;
    int vrc = i_getServiceAccountAndStartType(L"VBoxSDS", wszBuffer, RT_ELEMENTS(wszBuffer), &uStartType);
    if (RT_SUCCESS(vrc))
    {
        LogRelFunc(("VBoxSDS service is running under the '%ls' account with start type %u.\n", wszBuffer, uStartType));
        if (RTUtf16Cmp(wszBuffer, L"LocalSystem") != 0)
            return setError(hrcCaller,
                            tr("VBoxSDS is misconfigured to run under the '%ls' account instead of the SYSTEM one.\n"
                               "Reinstall VirtualBox to fix it.  Alternatively you can fix it using the Windows Service Control "
                               "Manager or by running 'sc config VBoxSDS obj=LocalSystem' on a command line."), wszBuffer);
        if (uStartType == SERVICE_DISABLED)
            return setError(hrcCaller,
                            tr("The VBoxSDS windows service is disabled.\n"
                               "Reinstall VirtualBox to fix it.  Alternatively try reenable the service by setting it to "
                               " 'Manual' startup type in the Windows Service management console, or by runing "
                               "'sc config VBoxSDS start=demand' on the command line."));
    }
    else if (vrc == VERR_NOT_FOUND)
        return setError(hrcCaller,
                        tr("The VBoxSDS windows service was not found.\n"
                           "Reinstall VirtualBox to fix it.  Alternatively you can try start VirtualBox as Administrator, this "
                           "should automatically reinstall the service, or you can run "
                           "'VBoxSDS.exe --regservice' command from an elevated Administrator command line."));
    else
        LogRelFunc(("VirtualBoxClient::i_getServiceAccount failed: %Rrc\n", vrc));
# endif

    /*
     * First step is to try get an IUnknown interface of the VirtualBox object.
     *
     * This will succeed even when oleaut32.msm (see @bugref{8016}, @ticketref{12087})
     * is accidentally installed and messes up COM.  It may also succeed when the COM
     * registration is partially broken (though that's unlikely to happen these days).
     */
    IUnknown *pUnknown = NULL;
    hrc = CoCreateInstance(CLSID_VirtualBox, NULL, CLSCTX_LOCAL_SERVER, IID_IUnknown, (void **)&pUnknown);
    if (FAILED(hrc))
    {
        if (hrc == hrcCaller)
            return setError(hrcCaller, tr("Completely failed to instantiate CLSID_VirtualBox: %Rhrc"), hrcCaller);
        return setError(hrcCaller, tr("Completely failed to instantiate CLSID_VirtualBox: %Rhrc & %Rhrc"), hrcCaller, hrc);
    }

    /*
     * Try query the IVirtualBox interface (should fail), if it succeed we return
     * straight away so we have more columns to spend on long messages below.
     */
    IVirtualBox *pVirtualBox;
    hrc = pUnknown->QueryInterface(IID_IVirtualBox, (void **)&pVirtualBox);
    if (SUCCEEDED(hrc))
    {
        pVirtualBox->Release();
        pUnknown->Release();
        return setError(hrcCaller,
                        tr("Failed to instantiate CLSID_VirtualBox the first time, but worked when checking out why ... weird"));
    }

    /*
     * Check for oleaut32.msm traces in the registry.
     */
    HKEY hKey;
    LSTATUS lrc = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\{00020420-0000-0000-C000-000000000046}\\InprocServer32",
                                0 /*fFlags*/, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | STANDARD_RIGHTS_READ, &hKey);
    if (lrc == ERROR_SUCCESS)
    {
        wchar_t wszBuf[8192];
        DWORD   cbBuf  = sizeof(wszBuf) - sizeof(wchar_t);
        DWORD   dwType = 0;
        lrc = RegQueryValueExW(hKey, L"InprocServer32", NULL /*pvReserved*/, &dwType, (BYTE *)&wszBuf[0], &cbBuf);
        if (lrc == ERROR_SUCCESS)
        {
            wszBuf[cbBuf / sizeof(wchar_t)] = '\0';
            bool fSetError = false;

            /*
             * Try decode the string and improve the message.
             */
            typedef UINT (WINAPI *PFNMSIDECOMPOSEDESCRIPTORW)(PCWSTR pwszDescriptor,
                                                              LPWSTR pwszProductCode /*[40]*/,
                                                              LPWSTR pwszFeatureId /*[40]*/,
                                                              LPWSTR pwszComponentCode /*[40]*/,
                                                              DWORD *poffArguments);
            PFNMSIDECOMPOSEDESCRIPTORW pfnMsiDecomposeDescriptorW;
            pfnMsiDecomposeDescriptorW = (PFNMSIDECOMPOSEDESCRIPTORW)RTLdrGetSystemSymbol("msi.dll", "MsiDecomposeDescriptorW");
            if (   pfnMsiDecomposeDescriptorW
                && (   dwType == REG_SZ
                    || dwType == REG_MULTI_SZ))
            {
                wchar_t wszProductCode[RTUUID_STR_LENGTH + 2 + 16]   = { 0 };
                wchar_t wszFeatureId[RTUUID_STR_LENGTH + 2 + 16]     = { 0 };
                wchar_t wszComponentCode[RTUUID_STR_LENGTH + 2 + 16] = { 0 };
                DWORD   offArguments = ~(DWORD)0;
                UINT uRc = pfnMsiDecomposeDescriptorW(wszBuf, wszProductCode, wszFeatureId, wszComponentCode, &offArguments);
                if (uRc == 0)
                {
                    /*
                     * Can we resolve the product code into a name?
                     */
                    typedef UINT (WINAPI *PFNMSIOPENPRODUCTW)(PCWSTR, MSIHANDLE *);
                    PFNMSIOPENPRODUCTW pfnMsiOpenProductW;
                    pfnMsiOpenProductW = (PFNMSIOPENPRODUCTW)RTLdrGetSystemSymbol("msi.dll", "MsiOpenProductW");

                    typedef UINT (WINAPI *PFNMSICLOSEHANDLE)(MSIHANDLE);
                    PFNMSICLOSEHANDLE pfnMsiCloseHandle;
                    pfnMsiCloseHandle = (PFNMSICLOSEHANDLE)RTLdrGetSystemSymbol("msi.dll", "MsiCloseHandle");

                    typedef UINT (WINAPI *PFNGETPRODUCTPROPERTYW)(MSIHANDLE, PCWSTR, PWSTR, PDWORD);
                    PFNGETPRODUCTPROPERTYW pfnMsiGetProductPropertyW;
                    pfnMsiGetProductPropertyW = (PFNGETPRODUCTPROPERTYW)RTLdrGetSystemSymbol("msi.dll", "MsiGetProductPropertyW");
                    if (   pfnMsiGetProductPropertyW
                        && pfnMsiCloseHandle
                        && pfnMsiOpenProductW)
                    {
                        MSIHANDLE hMsi = 0;
                        uRc = pfnMsiOpenProductW(wszProductCode, &hMsi);
                        if (uRc == 0)
                        {
                            static wchar_t const * const s_apwszProps[] =
                            {
                                INSTALLPROPERTY_INSTALLEDPRODUCTNAME,
                                INSTALLPROPERTY_PRODUCTNAME,
                                INSTALLPROPERTY_PACKAGENAME,
                            };

                            wchar_t  wszProductName[1024];
                            DWORD    cwcProductName;
                            unsigned i = 0;
                            do
                            {
                                cwcProductName = RT_ELEMENTS(wszProductName) - 1;
                                uRc = pfnMsiGetProductPropertyW(hMsi, s_apwszProps[i], wszProductName, &cwcProductName);
                            }
                            while (   ++i < RT_ELEMENTS(s_apwszProps)
                                   && (   uRc != 0
                                       || cwcProductName < 2
                                       || cwcProductName >= RT_ELEMENTS(wszProductName)) );
                            uRc = pfnMsiCloseHandle(hMsi);
                            if (uRc == 0 && cwcProductName >= 2)
                            {
                                wszProductName[RT_MIN(cwcProductName, RT_ELEMENTS(wszProductName) - 1)] = '\0';
                                setError(hrcCaller,
                                         tr("Failed to instantiate CLSID_VirtualBox w/ IVirtualBox, but CLSID_VirtualBox w/ IUnknown works.\n"
                                            "PSDispatch looks broken by the '%ls' (%ls) program, suspecting that it features the broken oleaut32.msm module as component %ls.\n"
                                            "\n"
                                            "We suggest you try uninstall '%ls'.\n"
                                            "\n"
                                            "See also https://support.microsoft.com/en-us/kb/316911 "),
                                         wszProductName, wszProductCode, wszComponentCode, wszProductName);
                                fSetError = true;
                            }
                        }
                    }

                    /* MSI uses COM and may mess up our stuff. So, we wait with the fallback till afterwards in this case. */
                    if (!fSetError)
                    {
                        setError(hrcCaller,
                                 tr("Failed to instantiate CLSID_VirtualBox w/ IVirtualBox, CLSID_VirtualBox w/ IUnknown works.\n"
                                    "PSDispatch looks broken by installer %ls featuring the broken oleaut32.msm module as component %ls.\n"
                                    "\n"
                                    "See also https://support.microsoft.com/en-us/kb/316911 "),
                                 wszProductCode, wszComponentCode);
                        fSetError = true;
                    }
                }
            }
            if (!fSetError)
                setError(hrcCaller, tr("Failed to instantiate CLSID_VirtualBox w/ IVirtualBox, CLSID_VirtualBox w/ IUnknown works.\n"
                                       "PSDispatch looks broken by some installer featuring the broken oleaut32.msm module as a component.\n"
                                       "\n"
                                       "See also https://support.microsoft.com/en-us/kb/316911 "));
        }
        else if (lrc == ERROR_FILE_NOT_FOUND)
            setError(hrcCaller, tr("Failed to instantiate CLSID_VirtualBox w/ IVirtualBox, but CLSID_VirtualBox w/ IUnknown works.\n"
                                   "PSDispatch looks fine. Weird"));
        else
            setError(hrcCaller, tr("Failed to instantiate CLSID_VirtualBox w/ IVirtualBox, but CLSID_VirtualBox w/ IUnknown works.\n"
                                   "Checking out PSDispatch registration ended with error: %u (%#x)"), lrc, lrc);
        RegCloseKey(hKey);
    }

    pUnknown->Release();
    return hrcCaller;
}

# ifdef VBOX_WITH_SDS
/**
 * Gets the service account name and start type for the given service.
 *
 * @returns IPRT status code (for some reason).
 * @param   pwszServiceName The name of the service.
 * @param   pwszAccountName Where to return the account name.
 * @param   cwcAccountName  The length of the account name buffer (in WCHARs).
 * @param   puStartType     Where to return the start type.
 */
int VirtualBoxClient::i_getServiceAccountAndStartType(const wchar_t *pwszServiceName,
                                                      wchar_t *pwszAccountName, size_t cwcAccountName, uint32_t *puStartType)
{
    AssertPtr(pwszServiceName);
    AssertPtr(pwszAccountName);
    Assert(cwcAccountName);
    *pwszAccountName = '\0';
    *puStartType     = SERVICE_DEMAND_START;

    int vrc;

    // Get a handle to the SCM database.
    SC_HANDLE hSCManager = OpenSCManagerW(NULL /*pwszMachineName*/, NULL /*pwszDatabaseName*/, SC_MANAGER_CONNECT);
    if (hSCManager != NULL)
    {
        SC_HANDLE hService = OpenServiceW(hSCManager, pwszServiceName, SERVICE_QUERY_CONFIG);
        if (hService != NULL)
        {
            DWORD cbNeeded = sizeof(QUERY_SERVICE_CONFIGW) + _1K;
            if (!QueryServiceConfigW(hService, NULL, 0, &cbNeeded))
            {
                Assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
                LPQUERY_SERVICE_CONFIGW pSc = (LPQUERY_SERVICE_CONFIGW)RTMemTmpAllocZ(cbNeeded + _1K);
                if (pSc)
                {
                    DWORD cbNeeded2 = 0;
                    if (QueryServiceConfigW(hService, pSc, cbNeeded + _1K, &cbNeeded2))
                    {
                        *puStartType = pSc->dwStartType;
                        vrc = RTUtf16Copy(pwszAccountName, cwcAccountName, pSc->lpServiceStartName);
                        if (RT_FAILURE(vrc))
                            LogRel(("Error: SDS service name is too long (%Rrc): %ls\n", vrc, pSc->lpServiceStartName));
                    }
                    else
                    {
                        int dwError = GetLastError();
                        vrc = RTErrConvertFromWin32(dwError);
                        LogRel(("Error: Failed querying '%ls' service config: %Rwc (%u) -> %Rrc; cbNeeded=%d cbNeeded2=%d\n",
                                pwszServiceName, dwError, dwError, vrc, cbNeeded, cbNeeded2));
                    }
                    RTMemTmpFree(pSc);
                }
                else
                {
                    LogRel(("Error: Failed allocating %#x bytes of memory for service config!\n", cbNeeded + _1K));
                    vrc = VERR_NO_TMP_MEMORY;
                }
            }
            else
            {
                AssertLogRelMsgFailed(("Error: QueryServiceConfigW returns success with zero buffer!\n"));
                vrc = VERR_IPE_UNEXPECTED_STATUS;
            }
            CloseServiceHandle(hService);
        }
        else
        {
            int dwError = GetLastError();
            vrc = RTErrConvertFromWin32(dwError);
            LogRel(("Error: Could not open service '%ls': %Rwc (%u) -> %Rrc\n", pwszServiceName, dwError, dwError, vrc));
        }
        CloseServiceHandle(hSCManager);
    }
    else
    {
        int dwError = GetLastError();
        vrc = RTErrConvertFromWin32(dwError);
        LogRel(("Error: Could not open SCM: %Rwc (%u) -> %Rrc\n", dwError, dwError, vrc));
    }
    return vrc;
}
# endif /* VBOX_WITH_SDS */

#endif /* RT_OS_WINDOWS */

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void VirtualBoxClient::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
    {
        LogFlowThisFunc(("already done\n"));
        return;
    }

#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
    if (mData.m_objectTrackerTask)
    {
        LogRel(("FRONTEND: Terminating the object tracker...\n"));

        mData.m_objectTrackerTask->finish();//set the termination flag in the thread
        delete mData.m_objectTrackerTask;//waiting thread termination is going in the m_objectTrackerTask destructor
        gTrackedObjectsCollector.uninit();
    }
#endif

#ifdef VBOX_WITH_MAIN_NLS
    i_unregisterEventListener();
#endif

    if (mData.m_ThreadWatcher != NIL_RTTHREAD)
    {
        /* Signal the event semaphore and wait for the thread to terminate.
         * if it hangs for some reason exit anyway, this can cause a crash
         * though as the object will no longer be available. */
        RTSemEventSignal(mData.m_SemEvWatcher);
        RTThreadWait(mData.m_ThreadWatcher, 30000, NULL);
        mData.m_ThreadWatcher = NIL_RTTHREAD;
        RTSemEventDestroy(mData.m_SemEvWatcher);
        mData.m_SemEvWatcher = NIL_RTSEMEVENT;
    }
#ifdef VBOX_WITH_MAIN_NLS
    if (mData.m_pVBoxTranslator != NULL)
    {
        mData.m_pVBoxTranslator->release();
        mData.m_pVBoxTranslator = NULL;
        mData.m_pTrComponent = NULL;
    }
#endif

    mData.m_pVirtualBox.setNull();

    ASMAtomicDecU32(&g_cInstances);

    LogFlowThisFunc(("returns\n"));
}

// IVirtualBoxClient properties
/////////////////////////////////////////////////////////////////////////////

/**
 * Returns a reference to the VirtualBox object.
 *
 * @returns COM status code
 * @param   aVirtualBox Address of result variable.
 */
HRESULT VirtualBoxClient::getVirtualBox(ComPtr<IVirtualBox> &aVirtualBox)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aVirtualBox = mData.m_pVirtualBox;
    return S_OK;
}

/**
 * Create a new Session object and return a reference to it.
 *
 * @returns COM status code
 * @param   aSession    Address of result variable.
 */
HRESULT VirtualBoxClient::getSession(ComPtr<ISession> &aSession)
{
    /* this is not stored in this object, no need to lock */
    ComPtr<ISession> pSession;
    HRESULT hrc = pSession.createInprocObject(CLSID_Session);
    if (SUCCEEDED(hrc))
        aSession = pSession;
    return hrc;
}

/**
 * Return reference to the EventSource associated with this object.
 *
 * @returns COM status code
 * @param   aEventSource    Address of result variable.
 */
HRESULT VirtualBoxClient::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    /* this is const, no need to lock */
    aEventSource = mData.m_pEventSource;
    return aEventSource.isNull() ? E_FAIL : S_OK;
}

// IVirtualBoxClient methods
/////////////////////////////////////////////////////////////////////////////

/**
 * Checks a Machine object for any pending errors.
 *
 * @returns COM status code
 * @param   aMachine    Machine object to check.
 */
HRESULT VirtualBoxClient::checkMachineError(const ComPtr<IMachine> &aMachine)
{
    BOOL fAccessible = FALSE;
    HRESULT hrc = aMachine->COMGETTER(Accessible)(&fAccessible);
    if (FAILED(hrc))
        return setError(hrc, tr("Could not check the accessibility status of the VM"));
    else if (!fAccessible)
    {
        ComPtr<IVirtualBoxErrorInfo> pAccessError;
        hrc = aMachine->COMGETTER(AccessError)(pAccessError.asOutParam());
        if (FAILED(hrc))
            return setError(hrc, tr("Could not get the access error message of the VM"));
        else
        {
            ErrorInfo info(pAccessError);
            ErrorInfoKeeper eik(info);
            return info.getResultCode();
        }
    }
    return S_OK;
}

// private methods
/////////////////////////////////////////////////////////////////////////////


/// @todo AM Add pinging of VBoxSDS
/*static*/
DECLCALLBACK(int) VirtualBoxClient::SVCWatcherThread(RTTHREAD ThreadSelf,
                                                     void *pvUser)
{
    NOREF(ThreadSelf);
    Assert(pvUser);
    VirtualBoxClient *pThis = (VirtualBoxClient *)pvUser;
    RTSEMEVENT sem = pThis->mData.m_SemEvWatcher;
    RTMSINTERVAL cMillies = VBOXCLIENT_DEFAULT_INTERVAL;

    /* The likelihood of early crashes are high, so start with a short wait. */
    int vrc = RTSemEventWait(sem, cMillies / 2);

    /* As long as the waiting times out keep retrying the wait. */
    while (RT_FAILURE(vrc))
    {
        {
            HRESULT hrc = S_OK;
            ComPtr<IVirtualBox> pV;
            {
                AutoReadLock alock(pThis COMMA_LOCKVAL_SRC_POS);
                pV = pThis->mData.m_pVirtualBox;
            }
            if (!pV.isNull())
            {
                ULONG rev;
                hrc = pV->COMGETTER(Revision)(&rev);
                if (FAILED_DEAD_INTERFACE(hrc))
                {
                    LogRel(("VirtualBoxClient: detected unresponsive VBoxSVC (hrc=%Rhrc)\n", hrc));
                    {
                        AutoWriteLock alock(pThis COMMA_LOCKVAL_SRC_POS);
                        /* Throw away the VirtualBox reference, it's no longer
                         * usable as VBoxSVC terminated in the mean time. */
                        pThis->mData.m_pVirtualBox.setNull();
                    }
                    ::FireVBoxSVCAvailabilityChangedEvent(pThis->mData.m_pEventSource, FALSE);
                }
            }
            else
            {
                /* Try to get a new VirtualBox reference straight away, and if
                 * this fails use an increased waiting time as very frequent
                 * restart attempts in some wedged config can cause high CPU
                 * and disk load. */
                ComPtr<IVirtualBox> pVirtualBox;
                hrc = pVirtualBox.createLocalObject(CLSID_VirtualBox);
                if (FAILED(hrc))
                    cMillies = 3 * VBOXCLIENT_DEFAULT_INTERVAL;
                else
                {
                    LogRel(("VirtualBoxClient: detected working VBoxSVC (hrc=%Rhrc)\n", hrc));
                    {
                        AutoWriteLock alock(pThis COMMA_LOCKVAL_SRC_POS);
                        /* Update the VirtualBox reference, there's a working
                         * VBoxSVC again from now on. */
                        pThis->mData.m_pVirtualBox = pVirtualBox;
#ifdef VBOX_WITH_MAIN_NLS
                        /* update language using new instance of IVirtualBox in case the language settings was changed */
                        pThis->i_reloadApiLanguage();
                        pThis->i_registerEventListener();
#endif
                    }
                    ::FireVBoxSVCAvailabilityChangedEvent(pThis->mData.m_pEventSource, TRUE);
                    cMillies = VBOXCLIENT_DEFAULT_INTERVAL;
                }
            }
        }
        vrc = RTSemEventWait(sem, cMillies);
    }
    return 0;
}

#ifdef VBOX_WITH_MAIN_NLS

HRESULT VirtualBoxClient::i_reloadApiLanguage()
{
    if (mData.m_pVBoxTranslator == NULL)
        return S_OK;

    HRESULT hrc = mData.m_pVBoxTranslator->loadLanguage(mData.m_pVirtualBox);
    if (FAILED(hrc))
        setError(hrc, tr("Failed to load user language instance"));
    return hrc;
}

HRESULT VirtualBoxClient::i_registerEventListener()
{
    HRESULT hrc = mData.m_pVirtualBox->COMGETTER(EventSource)(mData.m_pVBoxEventSource.asOutParam());
    if (SUCCEEDED(hrc))
    {
        ComObjPtr<VBoxEventListenerImpl> pVBoxListener;
        pVBoxListener.createObject();
        pVBoxListener->init(new VBoxEventListener());
        mData.m_pVBoxEventListener = pVBoxListener;
        com::SafeArray<VBoxEventType_T> eventTypes;
        eventTypes.push_back(VBoxEventType_OnLanguageChanged);
        hrc = mData.m_pVBoxEventSource->RegisterListener(pVBoxListener, ComSafeArrayAsInParam(eventTypes), true);
        if (FAILED(hrc))
        {
            hrc = setError(hrc, tr("Failed to register listener"));
            mData.m_pVBoxEventListener.setNull();
            mData.m_pVBoxEventSource.setNull();
        }
    }
    else
        hrc = setError(hrc, tr("Failed to get event source from VirtualBox"));
    return hrc;
}

void VirtualBoxClient::i_unregisterEventListener()
{
    if (mData.m_pVBoxEventListener.isNotNull())
    {
        if (mData.m_pVBoxEventSource.isNotNull())
            mData.m_pVBoxEventSource->UnregisterListener(mData.m_pVBoxEventListener);
        mData.m_pVBoxEventListener.setNull();
    }
    mData.m_pVBoxEventSource.setNull();
}

#endif /* VBOX_WITH_MAIN_NLS */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
