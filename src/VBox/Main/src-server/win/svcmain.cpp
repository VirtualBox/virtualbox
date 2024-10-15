/* $Id: svcmain.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SVCMAIN - COM out-of-proc server main entry
 */

/*
 * Copyright (C) 2004-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_VBOXSVC
#include <iprt/win/windows.h>
#ifdef DEBUG_bird
# include <RpcAsync.h>
#endif

#include "VBox/com/defs.h"
#include "VBox/com/com.h"
#include "VBox/com/VirtualBox.h"

#include "VirtualBoxImpl.h"
#include "LoggingNew.h"

#include "svchlp.h"

#include <iprt/errcore.h>
#include <iprt/buildconfig.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/path.h>
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/asm.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MAIN_WND_CLASS L"VirtualBox Interface"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
class CExeModule : public ATL::CComModule
{
public:
    LONG Unlock() throw();
    DWORD dwThreadID;
    HANDLE hEventShutdown;
    void MonitorShutdown();
    bool StartMonitor();
    bool HasActiveConnection();
    bool bActivity;
    static bool isIdleLockCount(LONG cLocks);
};


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
BEGIN_OBJECT_MAP(ObjectMap)
    OBJECT_ENTRY(CLSID_VirtualBox, VirtualBox)
END_OBJECT_MAP()

CExeModule *g_pModule     = NULL;
HWND        g_hMainWindow = NULL;
HINSTANCE   g_hInstance   = NULL;
#ifdef VBOX_WITH_SDS
/** This is set if we're connected to SDS.
 *
 * It means that we should discount a server lock that it is holding when
 * deciding whether we're idle or not.
 *
 * Also, when set we deregister with SDS during class factory destruction.  We
 * exploit this to prevent attempts to deregister during or after COM shutdown.
 */
bool        g_fRegisteredWithVBoxSDS = false;
#endif

/* Normal timeout usually used in Shutdown Monitor */
const DWORD dwNormalTimeout = 5000;
volatile uint32_t dwTimeOut = dwNormalTimeout; /* time for EXE to be idle before shutting down. Can be decreased at system shutdown phase. */



/** Passed to CreateThread to monitor the shutdown event. */
static DWORD WINAPI MonitorProc(void *pv) RT_NOTHROW_DEF
{
    CExeModule *p = (CExeModule *)pv;
    p->MonitorShutdown();
    return 0;
}

LONG CExeModule::Unlock() throw()
{
    LONG cLocks = ATL::CComModule::Unlock();
    if (isIdleLockCount(cLocks))
    {
        bActivity = true;
        SetEvent(hEventShutdown); /* tell monitor that we transitioned to zero */
    }
    return cLocks;
}

bool CExeModule::HasActiveConnection()
{
    return bActivity || !isIdleLockCount(GetLockCount());
}

/**
 * Checks if @a cLocks signifies an IDLE server lock load.
 *
 * This takes VBoxSDS into account (i.e. ignores it).
 */
/*static*/ bool CExeModule::isIdleLockCount(LONG cLocks)
{
#ifdef VBOX_WITH_SDS
    if (g_fRegisteredWithVBoxSDS)
        return cLocks <= 1;
#endif
    return cLocks <= 0;
}

/* Monitors the shutdown event */
void CExeModule::MonitorShutdown()
{
    while (1)
    {
        WaitForSingleObject(hEventShutdown, INFINITE);
        DWORD dwWait;
        do
        {
            bActivity = false;
            dwWait = WaitForSingleObject(hEventShutdown, dwTimeOut);
        } while (dwWait == WAIT_OBJECT_0);
        /* timed out */
        if (!HasActiveConnection()) /* if no activity let's really bail */
        {
            /* Disable log rotation at this point, worst case a log file
             * becomes slightly bigger than it should. Avoids quirks with
             * log rotation: there might be another API service process
             * running at this point which would rotate the logs concurrently,
             * creating a mess. */
            PRTLOGGER pReleaseLogger = RTLogRelGetDefaultInstance();
            if (pReleaseLogger)
            {
                char szDest[1024];
                int vrc = RTLogQueryDestinations(pReleaseLogger, szDest, sizeof(szDest));
                if (RT_SUCCESS(vrc))
                {
                    vrc = RTStrCat(szDest, sizeof(szDest), " nohistory");
                    if (RT_SUCCESS(vrc))
                    {
                        vrc = RTLogDestinations(pReleaseLogger, szDest);
                        AssertRC(vrc);
                    }
                }
            }
#if _WIN32_WINNT >= 0x0400
            CoSuspendClassObjects();
            if (!HasActiveConnection())
#endif
                break;
        }
    }
    CloseHandle(hEventShutdown);
    PostThreadMessage(dwThreadID, WM_QUIT, 0, 0);
}

bool CExeModule::StartMonitor()
{
    hEventShutdown = CreateEvent(NULL, false, false, NULL);
    if (hEventShutdown == NULL)
        return false;
    DWORD idThreadIgnored;
    HANDLE h = CreateThread(NULL, 0, MonitorProc, this, 0, &idThreadIgnored);
    return (h != NULL);
}


#ifdef VBOX_WITH_SDS

class VBoxSVCRegistration;

/**
 * Custom class factory for the VirtualBox singleton.
 *
 * The implementation of CreateInstance is found in win/svcmain.cpp.
 */
class VirtualBoxClassFactory : public ATL::CComClassFactory
{
private:
    /** Tri state: 0=uninitialized or initializing; 1=success; -1=failure.
     * This will be updated after both m_hrcCreate and m_pObj have been set. */
    volatile int32_t       m_iState;
    /** The result of the instantiation attempt. */
    HRESULT                m_hrcCreate;
    /** The IUnknown of the VirtualBox object/interface we're working with. */
    IUnknown              *m_pObj;
    /** Pointer to the IVBoxSVCRegistration implementation that VBoxSDS works with. */
    VBoxSVCRegistration   *m_pVBoxSVC;
    /** The VBoxSDS interface. */
    ComPtr<IVirtualBoxSDS> m_ptrVirtualBoxSDS;

public:
    VirtualBoxClassFactory() : m_iState(0), m_hrcCreate(S_OK), m_pObj(NULL), m_pVBoxSVC(NULL)
    { }

    virtual ~VirtualBoxClassFactory()
    {
        if (m_pObj)
        {
            m_pObj->Release();
            m_pObj = NULL;
        }

        /* We usually get here during g_pModule->Term() via CoRevokeClassObjec, so COM
           probably working well enough to talk to SDS when we get here. */
        if (g_fRegisteredWithVBoxSDS)
            i_deregisterWithSds();
    }

    // IClassFactory
    STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, void **ppvObj);

    /** Worker for VBoxSVCRegistration::getVirtualBox. */
    HRESULT i_getVirtualBox(IUnknown **ppResult);

private:
    HRESULT i_registerWithSds(IUnknown **ppOtherVirtualBox);
    void    i_deregisterWithSds(void);

    friend VBoxSVCRegistration;
};


/**
 * The VBoxSVC class is handed to VBoxSDS so it can call us back and ask for the
 * VirtualBox object when the next VBoxSVC for this user registers itself.
 */
class VBoxSVCRegistration : public IVBoxSVCRegistration
{
private:
    /** Number of references. */
    uint32_t volatile m_cRefs;

public:
    /** Pointer to the factory. */
    VirtualBoxClassFactory *m_pFactory;

public:
    VBoxSVCRegistration(VirtualBoxClassFactory *pFactory)
        : m_cRefs(1), m_pFactory(pFactory)
    { }
    virtual ~VBoxSVCRegistration()
    {
        if (m_pFactory)
        {
            if (m_pFactory->m_pVBoxSVC)
                m_pFactory->m_pVBoxSVC = NULL;
            m_pFactory = NULL;
        }
    }
    RTMEMEF_NEW_AND_DELETE_OPERATORS();

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void **ppvObject)
    {
        if (riid == __uuidof(IUnknown))
            *ppvObject = (void *)(IUnknown *)this;
        else if (riid == __uuidof(IVBoxSVCRegistration))
            *ppvObject = (void *)(IVBoxSVCRegistration *)this;
        else
        {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;

    }

    STDMETHOD_(ULONG,AddRef)(void)
    {
        uint32_t cRefs = ASMAtomicIncU32(&m_cRefs);
        return cRefs;
    }

    STDMETHOD_(ULONG,Release)(void)
    {
        uint32_t cRefs = ASMAtomicDecU32(&m_cRefs);
        if (cRefs == 0)
            delete this;
        return cRefs;
    }

    // IVBoxSVCRegistration
    STDMETHOD(GetVirtualBox)(IUnknown **ppResult)
    {
        if (m_pFactory)
            return m_pFactory->i_getVirtualBox(ppResult);
        return E_FAIL;
    }
};


HRESULT VirtualBoxClassFactory::i_registerWithSds(IUnknown **ppOtherVirtualBox)
{
# ifdef DEBUG_bird
    RPC_CALL_ATTRIBUTES_V2_W CallAttribs = { RPC_CALL_ATTRIBUTES_VERSION, RPC_QUERY_CLIENT_PID | RPC_QUERY_IS_CLIENT_LOCAL };
    RPC_STATUS rcRpc = RpcServerInqCallAttributesW(NULL, &CallAttribs);
    LogRel(("i_registerWithSds: RpcServerInqCallAttributesW -> %#x ClientPID=%#x IsClientLocal=%d ProtocolSequence=%#x CallStatus=%#x CallType=%#x OpNum=%#x InterfaceUuid=%RTuuid\n",
            rcRpc, CallAttribs.ClientPID, CallAttribs.IsClientLocal, CallAttribs.ProtocolSequence, CallAttribs.CallStatus,
            CallAttribs.CallType, CallAttribs.OpNum, &CallAttribs.InterfaceUuid));
# endif

    /*
     * Connect to VBoxSDS.
     */
    HRESULT hrc = CoCreateInstance(CLSID_VirtualBoxSDS, NULL, CLSCTX_LOCAL_SERVER, IID_IVirtualBoxSDS,
                                   (void **)m_ptrVirtualBoxSDS.asOutParam());
    if (SUCCEEDED(hrc))
    {
        /* By default the RPC_C_IMP_LEVEL_IDENTIFY is used for impersonation the client. It allows
           ACL checking but restricts an access to system objects e.g. files. Call to CoSetProxyBlanket
           elevates the impersonation level up to RPC_C_IMP_LEVEL_IMPERSONATE allowing the VBoxSDS
           service to access the files. */
        hrc = CoSetProxyBlanket(m_ptrVirtualBoxSDS,
                                RPC_C_AUTHN_DEFAULT,
                                RPC_C_AUTHZ_DEFAULT,
                                COLE_DEFAULT_PRINCIPAL,
                                RPC_C_AUTHN_LEVEL_DEFAULT,
                                RPC_C_IMP_LEVEL_IMPERSONATE,
                                NULL,
                                EOAC_DEFAULT);
        if (SUCCEEDED(hrc))
        {
            /*
             * Create VBoxSVCRegistration object and hand that to VBoxSDS.
             */
            m_pVBoxSVC = new VBoxSVCRegistration(this);
            hrc = E_PENDING;
            /* we try to register IVirtualBox 10 times */
            for (int regTimes = 0; hrc == E_PENDING && regTimes < 10;  --regTimes)
            {
                hrc = m_ptrVirtualBoxSDS->RegisterVBoxSVC(m_pVBoxSVC, GetCurrentProcessId(), ppOtherVirtualBox);
                if (SUCCEEDED(hrc))
                {
                    g_fRegisteredWithVBoxSDS = !*ppOtherVirtualBox;
                    return hrc;
                }
                /* sleep to give a time for windows session 0 registration */
                if (hrc == E_PENDING)
                    RTThreadSleep(1000);
            }
            m_pVBoxSVC->Release();
        }
    }
    m_ptrVirtualBoxSDS.setNull();
    m_pVBoxSVC = NULL;
    *ppOtherVirtualBox = NULL;
    return hrc;
}


void VirtualBoxClassFactory::i_deregisterWithSds(void)
{
    Log(("VirtualBoxClassFactory::i_deregisterWithSds\n"));

    if (m_ptrVirtualBoxSDS.isNotNull())
    {
        if (m_pVBoxSVC)
        {
            HRESULT hrc = m_ptrVirtualBoxSDS->DeregisterVBoxSVC(m_pVBoxSVC, GetCurrentProcessId());
            NOREF(hrc);
        }
        m_ptrVirtualBoxSDS.setNull();
        g_fRegisteredWithVBoxSDS = false;
    }
    if (m_pVBoxSVC)
    {
        m_pVBoxSVC->m_pFactory = NULL;
        m_pVBoxSVC->Release();
        m_pVBoxSVC = NULL;
    }
}


HRESULT VirtualBoxClassFactory::i_getVirtualBox(IUnknown **ppResult)
{
# ifdef DEBUG_bird
    RPC_CALL_ATTRIBUTES_V2_W CallAttribs = { RPC_CALL_ATTRIBUTES_VERSION, RPC_QUERY_CLIENT_PID | RPC_QUERY_IS_CLIENT_LOCAL };
    RPC_STATUS rcRpc = RpcServerInqCallAttributesW(NULL, &CallAttribs);
    LogRel(("i_getVirtualBox: RpcServerInqCallAttributesW -> %#x ClientPID=%#x IsClientLocal=%d ProtocolSequence=%#x CallStatus=%#x CallType=%#x OpNum=%#x InterfaceUuid=%RTuuid\n",
            rcRpc, CallAttribs.ClientPID, CallAttribs.IsClientLocal, CallAttribs.ProtocolSequence, CallAttribs.CallStatus,
            CallAttribs.CallType, CallAttribs.OpNum, &CallAttribs.InterfaceUuid));
# endif
    IUnknown *pObj = m_pObj;
    if (pObj)
    {
        /** @todo Do we need to do something regarding server locking?  Hopefully COM
         *        deals with that........... */
        pObj->AddRef();
        *ppResult = pObj;
        Log(("VirtualBoxClassFactory::GetVirtualBox: S_OK - %p\n", pObj));
        return S_OK;
    }
    *ppResult = NULL;
    Log(("VirtualBoxClassFactory::GetVirtualBox: E_FAIL\n"));
    return E_FAIL;
}


/**
 * Custom instantiation of CComObjectCached.
 *
 * This catches certain QueryInterface callers for the purpose of watching for
 * abnormal client process termination (@bugref{3300}).
 *
 * @todo just merge this into class VirtualBox VirtualBoxImpl.h
 */
class VirtualBoxObjectCached : public VirtualBox
{
public:
    VirtualBoxObjectCached(void * = NULL)
        : VirtualBox()
    {
    }

    virtual ~VirtualBoxObjectCached()
    {
        m_iRef = LONG_MIN / 2; /* Catch refcount screwups by setting refcount something insane. */
        FinalRelease();
    }

    /** @name IUnknown implementation for VirtualBox
     * @{  */

    STDMETHOD_(ULONG, AddRef)() throw()
    {
        ULONG cRefs = InternalAddRef();
        if (cRefs == 2)
        {
            AssertMsg(ATL::_pAtlModule, ("ATL: referring to ATL module without having one declared in this linking namespace\n"));
            ATL::_pAtlModule->Lock();
        }
        return cRefs;
    }

    STDMETHOD_(ULONG, Release)() throw()
    {
        ULONG cRefs = InternalRelease();
        if (cRefs == 0)
            delete this;
        else if (cRefs == 1)
        {
            AssertMsg(ATL::_pAtlModule, ("ATL: referring to ATL module without having one declared in this linking namespace\n"));
            ATL::_pAtlModule->Unlock();
        }
        return cRefs;
    }

    STDMETHOD(QueryInterface)(REFIID iid, void **ppvObj) throw()
    {
        HRESULT hrc = _InternalQueryInterface(iid, ppvObj);
#ifdef VBOXSVC_WITH_CLIENT_WATCHER
        i_logCaller("QueryInterface %RTuuid -> %Rhrc %p", &iid, hrc, *ppvObj);
#endif
        return hrc;
    }

    /** @} */

    static HRESULT WINAPI CreateInstance(VirtualBoxObjectCached **ppObj) throw()
    {
        AssertReturn(ppObj, E_POINTER);
        *ppObj = NULL;

        HRESULT hrc = E_OUTOFMEMORY;
        VirtualBoxObjectCached *p = new (std::nothrow) VirtualBoxObjectCached();
        if (p)
        {
            p->SetVoid(NULL);
            p->InternalFinalConstructAddRef();
            hrc = p->_AtlInitialConstruct();
            if (SUCCEEDED(hrc))
                hrc = p->FinalConstruct();
            p->InternalFinalConstructRelease();
            if (FAILED(hrc))
                delete p;
            else
                *ppObj = p;
        }
        return hrc;
    }
};


/**
 * Custom class factory impl for the VirtualBox singleton.
 *
 * This will consult with VBoxSDS on whether this VBoxSVC instance should
 * provide the actual VirtualBox instance or just forward the instance from
 * some other SVC instance.
 *
 * @param   pUnkOuter       This must be NULL.
 * @param   riid            Reference to the interface ID to provide.
 * @param   ppvObj          Where to return the pointer to the riid instance.
 *
 * @return  COM status code.
 */
STDMETHODIMP VirtualBoxClassFactory::CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, void **ppvObj)
{
# ifdef VBOXSVC_WITH_CLIENT_WATCHER
    VirtualBox::i_logCaller("VirtualBoxClassFactory::CreateInstance: %RTuuid", riid);
# endif
    HRESULT hrc = E_POINTER;
    if (ppvObj != NULL)
    {
        *ppvObj = NULL;
        // no aggregation for singletons
        AssertReturn(pUnkOuter == NULL, CLASS_E_NOAGGREGATION);

        /*
         * We must make sure there is only one instance around.
         * So, we check without locking and then again after locking.
         */
        if (ASMAtomicReadS32(&m_iState) == 0)
        {
            Lock();
            __try
            {
                if (ASMAtomicReadS32(&m_iState) == 0)
                {
                    /*
                     * lock the module to indicate activity
                     * (necessary for the monitor shutdown thread to correctly
                     * terminate the module in case when CreateInstance() fails)
                     */
                    ATL::_pAtlModule->Lock();
                    __try
                    {
                        /*
                         * Now we need to connect to VBoxSDS to register ourselves.
                         */
                        IUnknown *pOtherVirtualBox = NULL;
                        m_hrcCreate = hrc = i_registerWithSds(&pOtherVirtualBox);
                        if (SUCCEEDED(hrc) && pOtherVirtualBox)
                            m_pObj = pOtherVirtualBox;
                        else if (SUCCEEDED(hrc))
                        {
                            ATL::_pAtlModule->Lock();
                            VirtualBoxObjectCached *p;
                            m_hrcCreate = hrc = VirtualBoxObjectCached::CreateInstance(&p);
                            if (SUCCEEDED(hrc))
                            {
                                m_hrcCreate = hrc = p->QueryInterface(IID_IUnknown, (void **)&m_pObj);
                                if (SUCCEEDED(hrc))
                                    RTLogClearFileDelayFlag(RTLogRelGetDefaultInstance(),  NULL);
                                else
                                {
                                    delete p;
                                    i_deregisterWithSds();
                                    m_pObj = NULL;
                                }
                            }
                        }
                        ASMAtomicWriteS32(&m_iState, SUCCEEDED(hrc) ? 1 : -1);
                    }
                    __finally
                    {
                        ATL::_pAtlModule->Unlock();
                    }
                }
            }
            __finally
            {
                if (ASMAtomicReadS32(&m_iState) == 0)
                {
                    ASMAtomicWriteS32(&m_iState, -1);
                    if (SUCCEEDED(m_hrcCreate))
                        m_hrcCreate = E_FAIL;
                }
                Unlock();
            }
        }

        /*
         * Query the requested interface from the IUnknown one we're keeping around.
         */
        if (m_hrcCreate == S_OK)
            hrc = m_pObj->QueryInterface(riid, ppvObj);
        else
            hrc = m_hrcCreate;
    }
    return hrc;
}

#endif // VBOX_WITH_SDS


/*
* Wrapper for Win API function ShutdownBlockReasonCreate
* This function defined starting from Vista only.
*/
static BOOL ShutdownBlockReasonCreateAPI(HWND hWnd, LPCWSTR pwszReason)
{
    typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNSHUTDOWNBLOCKREASONCREATE,(HWND hWnd, LPCWSTR pwszReason));

    PFNSHUTDOWNBLOCKREASONCREATE pfn
        = (PFNSHUTDOWNBLOCKREASONCREATE)GetProcAddress(GetModuleHandle(L"User32.dll"), "ShutdownBlockReasonCreate");
    AssertPtr(pfn);

    BOOL fResult = FALSE;
    if (pfn)
        fResult = pfn(hWnd, pwszReason);
    return fResult;
}

/*
* Wrapper for Win API function ShutdownBlockReasonDestroy
* This function defined starting from Vista only.
*/
static BOOL ShutdownBlockReasonDestroyAPI(HWND hWnd)
{
    typedef DECLCALLBACKPTR_EX(BOOL, WINAPI, PFNSHUTDOWNBLOCKREASONDESTROY,(HWND hWnd));
    PFNSHUTDOWNBLOCKREASONDESTROY pfn
        = (PFNSHUTDOWNBLOCKREASONDESTROY)GetProcAddress(GetModuleHandle(L"User32.dll"), "ShutdownBlockReasonDestroy");
    AssertPtr(pfn);

    BOOL fResult = FALSE;
    if (pfn)
        fResult = pfn(hWnd);
    return fResult;
}

static LRESULT CALLBACK WinMainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT lResult = 0;

    switch (msg)
    {
        case WM_QUERYENDSESSION:
        {
            LogRel(("WM_QUERYENDSESSION:%s%s%s%s (0x%08lx)\n",
                    lParam == 0                  ? " shutdown" : "",
                    lParam & ENDSESSION_CRITICAL ? " critical" : "",
                    lParam & ENDSESSION_LOGOFF   ? " logoff"   : "",
                    lParam & ENDSESSION_CLOSEAPP ? " close"    : "",
                    (unsigned long)lParam));
            if (g_pModule)
            {
                bool fActiveConnection = g_pModule->HasActiveConnection();
                if (fActiveConnection)
                {
                    lResult = FALSE;
                    LogRel(("VBoxSvc has active connections:"
                            " bActivity = %RTbool, lock count = %d\n",
                            g_pModule->bActivity, g_pModule->GetLockCount()));

                    /* place the VBoxSVC into system shutdown list */
                    ShutdownBlockReasonCreateAPI(hwnd, L"Has active connections.");
                    /* decrease a latency of MonitorShutdown loop */
                    ASMAtomicXchgU32(&dwTimeOut, 100);
                    Log(("VBoxSVCWinMain: WM_QUERYENDSESSION: VBoxSvc has active connections."
                         " bActivity = %d. Lock count = %d\n",
                         g_pModule->bActivity, g_pModule->GetLockCount()));
                }
                else
                {
                    LogRel(("No active connections:"
                            " bActivity = %RTbool, lock count = %d\n",
                            g_pModule->bActivity, g_pModule->GetLockCount()));
                    lResult = TRUE;
                }
            }
            else
                AssertMsgFailed(("VBoxSVCWinMain: WM_QUERYENDSESSION: Error: g_pModule is NULL"));
            break;
        }
        case WM_ENDSESSION:
        {
            LogRel(("WM_ENDSESSION:%s%s%s%s%s (%s/0x%08lx)\n",
                    lParam == 0                  ? " shutdown"  : "",
                    lParam & ENDSESSION_CRITICAL ? " critical"  : "",
                    lParam & ENDSESSION_LOGOFF   ? " logoff"    : "",
                    lParam & ENDSESSION_CLOSEAPP ? " close"     : "",
                    wParam == FALSE              ? " cancelled" : "",
                    wParam ? "TRUE" : "FALSE",
                    (unsigned long)lParam));

            /* Restore timeout of Monitor Shutdown if user canceled system shutdown */
            if (wParam == FALSE)
            {
                Log(("VBoxSVCWinMain: user canceled system shutdown.\n"));
                ASMAtomicXchgU32(&dwTimeOut, dwNormalTimeout);
                ShutdownBlockReasonDestroyAPI(hwnd);
            }
            break;
        }
        case WM_DESTROY:
        {
            ShutdownBlockReasonDestroyAPI(hwnd);
            PostQuitMessage(0);
            break;
        }

        default:
        {
            lResult = DefWindowProc(hwnd, msg, wParam, lParam);
            break;
        }
    }
    return lResult;
}

static int CreateMainWindow()
{
    int vrc = VINF_SUCCESS;
    Assert(g_hMainWindow == NULL);

    LogFlow(("CreateMainWindow\n"));

    g_hInstance = (HINSTANCE)GetModuleHandle(NULL);

    /* Register the Window Class. */
    WNDCLASS wc;
    RT_ZERO(wc);

    wc.style = CS_NOCLOSE;
    wc.lpfnWndProc = WinMainWndProc;
    wc.hInstance = g_hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
    wc.lpszClassName = MAIN_WND_CLASS;

    ATOM atomWindowClass = RegisterClass(&wc);
    if (atomWindowClass == 0)
    {
        LogRel(("Failed to register window class for session monitoring\n"));
        vrc = VERR_NOT_SUPPORTED;
    }
    else
    {
        /* Create the window. */
        g_hMainWindow = CreateWindowEx(0, MAIN_WND_CLASS, MAIN_WND_CLASS, 0,
                                       0, 0, 1, 1, NULL, NULL, g_hInstance, NULL);
        if (g_hMainWindow == NULL)
        {
            LogRel(("Failed to create window for session monitoring\n"));
            vrc = VERR_NOT_SUPPORTED;
        }
    }
    return vrc;
}


static void DestroyMainWindow()
{
    Assert(g_hMainWindow != NULL);
    Log(("SVCMain: DestroyMainWindow \n"));
    if (g_hMainWindow != NULL)
    {
        DestroyWindow(g_hMainWindow);
        g_hMainWindow = NULL;
        if (g_hInstance != NULL)
        {
            UnregisterClass(MAIN_WND_CLASS, g_hInstance);
            g_hInstance = NULL;
        }
    }
}


static const char * const ctrl_event_names[] = {
    "CTRL_C_EVENT",
    "CTRL_BREAK_EVENT",
    "CTRL_CLOSE_EVENT",
    /* reserved, not used */
    "<console control event 3>",
    "<console control event 4>",
    /* not sent to processes that load gdi32.dll or user32.dll */
    "CTRL_LOGOFF_EVENT",
    "CTRL_SHUTDOWN_EVENT",
};

/** @todo r=uwe placeholder */
BOOL WINAPI
ConsoleCtrlHandler(DWORD dwCtrlType) RT_NOTHROW_DEF
{
    const char *signame;
    char namebuf[48];
    // int vrc;

    if (dwCtrlType < RT_ELEMENTS(ctrl_event_names))
        signame = ctrl_event_names[dwCtrlType];
    else
    {
        /* should not happen, but be prepared */
        RTStrPrintf(namebuf, sizeof(namebuf),
                    "<console control event %lu>", (unsigned long)dwCtrlType);
        signame = namebuf;
    }
    LogRel(("Got %s\n", signame));

    if (RT_UNLIKELY(g_pModule == NULL))
    {
        LogRel(("%s: g_pModule == NULL\n", __FUNCTION__));
        return TRUE;
    }

    /* decrease latency of the MonitorShutdown loop */
    ASMAtomicXchgU32(&dwTimeOut, 100);

    bool fHasClients = g_pModule->HasActiveConnection();
    if (!fHasClients)
    {
        LogRel(("No clients, closing the shop.\n"));
        return TRUE;
    }

    LogRel(("VBoxSvc has clients: bActivity = %RTbool, lock count = %d\n",
            g_pModule->bActivity, g_pModule->GetLockCount()));

    /** @todo r=uwe wait for clients to disconnect */
    return TRUE;
}



/** Special export that make VBoxProxyStub not register this process as one that
 * VBoxSDS should be watching.
 */
extern "C" DECLEXPORT(void) VBOXCALL Is_VirtualBox_service_process_like_VBoxSDS_And_VBoxSDS(void)
{
    /* never called, just need to be here */
}


/* thread for registering the VBoxSVC started in session 0 */
static DWORD WINAPI threadRegisterVirtualBox(LPVOID lpParam) throw()
{
    HANDLE hEvent = (HANDLE)lpParam;
    HRESULT hrc = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hrc))
    {
        /* create IVirtualBox instance */
        ComPtr<IVirtualBox> pVirtualBox;
        hrc = CoCreateInstance(CLSID_VirtualBox, NULL, CLSCTX_INPROC_SERVER /*CLSCTX_LOCAL_SERVER */, IID_IVirtualBox,
                               (void **)pVirtualBox.asOutParam());
        if (SUCCEEDED(hrc))
        {
            /* wait a minute allowing clients to connect to the instance */
            WaitForSingleObject(hEvent, 60 * 1000);
            /* remove reference. If anybody connected to IVirtualBox it will stay alive. */
            pVirtualBox.setNull();
        }
        CoUninitialize();
    }
    return 0L;
}


/////////////////////////////////////////////////////////////////////////////
//
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int /*nShowCmd*/)
{
    int    argc = __argc;
    char **argv = __argv;

    /*
     * Need to parse the command line before initializing the VBox runtime so we can
     * change to the user home directory before logs are being created.
     */
    for (int i = 1; i < argc; i++)
        if (   (argv[i][0] == '/' || argv[i][0] == '-')
            && stricmp(&argv[i][1], "embedding") == 0) /* ANSI */
        {
            /* %HOMEDRIVE%%HOMEPATH% */
            wchar_t wszHome[RTPATH_MAX];
            DWORD cEnv = GetEnvironmentVariable(L"HOMEDRIVE", &wszHome[0], RTPATH_MAX);
            if (cEnv && cEnv < RTPATH_MAX)
            {
                DWORD cwc = cEnv; /* doesn't include NUL */
                cEnv = GetEnvironmentVariable(L"HOMEPATH", &wszHome[cEnv], RTPATH_MAX - cwc);
                if (cEnv && cEnv < RTPATH_MAX - cwc)
                {
                    /* If this fails there is nothing we can do. Ignore. */
                    SetCurrentDirectory(wszHome);
                }
            }
        }

    /*
     * Initialize the VBox runtime without loading
     * the support driver.
     */
    RTR3InitExe(argc, &argv, 0);

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--embedding",    'e',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "-embedding",     'e',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "/embedding",     'e',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "--unregserver",  'u',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "-unregserver",   'u',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "/unregserver",   'u',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "--regserver",    'r',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "-regserver",     'r',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "/regserver",     'r',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "--reregserver",  'f',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "-reregserver",   'f',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "/reregserver",   'f',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "--helper",       'H',    RTGETOPT_REQ_STRING | RTGETOPT_FLAG_ICASE },
        { "-helper",        'H',    RTGETOPT_REQ_STRING | RTGETOPT_FLAG_ICASE },
        { "/helper",        'H',    RTGETOPT_REQ_STRING | RTGETOPT_FLAG_ICASE },
        { "--logfile",      'F',    RTGETOPT_REQ_STRING | RTGETOPT_FLAG_ICASE },
        { "-logfile",       'F',    RTGETOPT_REQ_STRING | RTGETOPT_FLAG_ICASE },
        { "/logfile",       'F',    RTGETOPT_REQ_STRING | RTGETOPT_FLAG_ICASE },
        { "--logrotate",    'R',    RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_ICASE },
        { "-logrotate",     'R',    RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_ICASE },
        { "/logrotate",     'R',    RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_ICASE },
        { "--logsize",      'S',    RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_ICASE },
        { "-logsize",       'S',    RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_ICASE },
        { "/logsize",       'S',    RTGETOPT_REQ_UINT64 | RTGETOPT_FLAG_ICASE },
        { "--loginterval",  'I',    RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_ICASE },
        { "-loginterval",   'I',    RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_ICASE },
        { "/loginterval",   'I',    RTGETOPT_REQ_UINT32 | RTGETOPT_FLAG_ICASE },
        { "--registervbox", 'b',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "-registervbox",  'b',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
        { "/registervbox",  'b',    RTGETOPT_REQ_NOTHING | RTGETOPT_FLAG_ICASE },
    };

    bool            fRun = true;
    bool            fRegister = false;
    bool            fUnregister = false;
    const char      *pszPipeName = NULL;
    const char      *pszLogFile = NULL;
    uint32_t        cHistory = 10;                  // enable log rotation, 10 files
    uint32_t        uHistoryFileTime = RT_SEC_1DAY; // max 1 day per file
    uint64_t        uHistoryFileSize = 100 * _1M;   // max 100MB per file
    bool            fRegisterVBox = false;

    RTGETOPTSTATE   GetOptState;
    int vrc = RTGetOptInit(&GetOptState, argc, argv, &s_aOptions[0], RT_ELEMENTS(s_aOptions), 1, 0 /*fFlags*/);
    AssertRC(vrc);

    RTGETOPTUNION   ValueUnion;
    while ((vrc = RTGetOpt(&GetOptState, &ValueUnion)))
    {
        switch (vrc)
        {
            case 'e':
                /* already handled above */
                break;

            case 'u':
                fUnregister = true;
                fRun = false;
                break;

            case 'r':
                fRegister = true;
                fRun = false;
                break;

            case 'f':
                fUnregister = true;
                fRegister = true;
                fRun = false;
                break;

            case 'H':
                pszPipeName = ValueUnion.psz;
                if (!pszPipeName)
                    pszPipeName = "";
                fRun = false;
                break;

            case 'F':
                pszLogFile = ValueUnion.psz;
                break;

            case 'R':
                cHistory = ValueUnion.u32;
                break;

            case 'S':
                uHistoryFileSize = ValueUnion.u64;
                break;

            case 'I':
                uHistoryFileTime = ValueUnion.u32;
                break;

            case 'h':
            {
                static const WCHAR s_wszText[]  = L"Options:\n\n"
                                                  L"/RegServer:\tregister COM out-of-proc server\n"
                                                  L"/UnregServer:\tunregister COM out-of-proc server\n"
                                                  L"/ReregServer:\tunregister and register COM server\n"
                                                  L"no options:\trun the server";
                static const WCHAR s_wszTitle[] = L"Usage";
                fRun = false;
                MessageBoxW(NULL, s_wszText, s_wszTitle, MB_OK);
                return 0;
            }

            case 'V':
            {
                static const WCHAR s_wszTitle[] = L"Version";
                char         *pszText = NULL;
                RTStrAPrintf(&pszText, "%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                PRTUTF16     pwszText = NULL;
                RTStrToUtf16(pszText, &pwszText);
                RTStrFree(pszText);
                MessageBoxW(NULL, pwszText, s_wszTitle, MB_OK);
                RTUtf16Free(pwszText);
                fRun = false;
                return 0;
            }

            case 'b':
                fRegisterVBox = true;
                break;

            default:
                /** @todo this assumes that stderr is visible, which is not
                 * true for standard Windows applications. */
                /* continue on command line errors... */
                RTGetOptPrintError(vrc, &ValueUnion);
        }
    }

    /* Only create the log file when running VBoxSVC normally, but not when
     * registering/unregistering or calling the helper functionality. */
    if (fRun)
    {
        /** @todo Merge this code with server.cpp (use Logging.cpp?). */
        char szLogFile[RTPATH_MAX];
        if (!pszLogFile || !*pszLogFile)
        {
            vrc = com::GetVBoxUserHomeDirectory(szLogFile, sizeof(szLogFile));
            if (RT_SUCCESS(vrc))
                vrc = RTPathAppend(szLogFile, sizeof(szLogFile), "VBoxSVC.log");
            if (RT_FAILURE(vrc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to construct release log filename, vrc=%Rrc", vrc);
            pszLogFile = szLogFile;
        }

        RTERRINFOSTATIC ErrInfo;
        vrc = com::VBoxLogRelCreate("COM Server", pszLogFile,
                                    RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG,
                                    VBOXSVC_LOG_DEFAULT, "VBOXSVC_RELEASE_LOG",
#ifdef VBOX_WITH_SDS
                                    RTLOGDEST_FILE | RTLOGDEST_F_DELAY_FILE,
#else
                                    RTLOGDEST_FILE,
#endif
                                    UINT32_MAX /* cMaxEntriesPerGroup */, cHistory, uHistoryFileTime, uHistoryFileSize,
                                    RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(vrc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to open release log (%s, %Rrc)", ErrInfo.Core.pszMsg, vrc);
    }

    /* Set up a build identifier so that it can be seen from core dumps what
     * exact build was used to produce the core. Same as in Console::i_powerUpThread(). */
    static char saBuildID[48];
    RTStrPrintf(saBuildID, sizeof(saBuildID), "%s%s%s%s VirtualBox %s r%u %s%s%s%s",
                "BU", "IL", "DI", "D", RTBldCfgVersion(), RTBldCfgRevision(), "BU", "IL", "DI", "D");

    AssertCompile(VBOX_COM_INIT_F_DEFAULT == VBOX_COM_INIT_F_AUTO_REG_UPDATE);
    HRESULT hRes = com::Initialize(fRun ? VBOX_COM_INIT_F_AUTO_REG_UPDATE : 0);
    AssertLogRelMsg(SUCCEEDED(hRes), ("SVCMAIN: init failed: %Rhrc\n", hRes));

    g_pModule = new CExeModule();
    if(g_pModule == NULL)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "not enough memory to create ExeModule.");
    g_pModule->Init(ObjectMap, hInstance, &LIBID_VirtualBox);
    g_pModule->dwThreadID = GetCurrentThreadId();

    int nRet = 0;
    if (!fRun)
    {
#ifndef VBOX_WITH_MIDL_PROXY_STUB /* VBoxProxyStub.dll does all the registration work. */
        if (fUnregister)
        {
            g_pModule->UpdateRegistryFromResource(IDR_VIRTUALBOX, FALSE);
            nRet = g_pModule->UnregisterServer(TRUE);
        }
        if (fRegister)
        {
            g_pModule->UpdateRegistryFromResource(IDR_VIRTUALBOX, TRUE);
            nRet = g_pModule->RegisterServer(TRUE);
        }
#endif
        if (pszPipeName)
        {
            Log(("SVCMAIN: Processing Helper request (cmdline=\"%s\")...\n", pszPipeName));

            if (!*pszPipeName)
                vrc = VERR_INVALID_PARAMETER;

            if (RT_SUCCESS(vrc))
            {
                /* do the helper job */
                SVCHlpServer server;
                vrc = server.open(pszPipeName);
                if (RT_SUCCESS(vrc))
                    vrc = server.run();
            }
            if (RT_FAILURE(vrc))
            {
                Log(("SVCMAIN: Failed to process Helper request (%Rrc).\n", vrc));
                nRet = 1;
            }
        }
    }
    else
    {

        g_pModule->StartMonitor();
#if _WIN32_WINNT >= 0x0400
        hRes = g_pModule->RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE | REGCLS_SUSPENDED);
        _ASSERTE(SUCCEEDED(hRes));
        hRes = CoResumeClassObjects();
#else
        hRes = _Module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE);
#endif
        _ASSERTE(SUCCEEDED(hRes));

        /*
         * Register windows console signal handler to react to Ctrl-C,
         * Ctrl-Break, Close; but more importantly - to get notified
         * about shutdown when we are running in the context of the
         * autostart service - we won't get WM_ENDSESSION in that
         * case.
         */
        ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);


        if (RT_SUCCESS(CreateMainWindow()))
            Log(("SVCMain: Main window succesfully created\n"));
        else
            Log(("SVCMain: Failed to create main window\n"));

        /* create thread to register IVirtualBox in VBoxSDS
         * It is used for starting the VBoxSVC in the windows
         * session 0. */
        HANDLE hWaitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        HANDLE hRegisterVBoxThread = NULL;
        if (fRegisterVBox)
        {
            DWORD  dwThreadId = 0;
            hRegisterVBoxThread = CreateThread(NULL, 0, threadRegisterVirtualBox, (LPVOID)hWaitEvent,
                                               0, &dwThreadId);
        }

        MSG msg;
        while (GetMessage(&msg, 0, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DestroyMainWindow();

        if (fRegisterVBox)
        {
            SetEvent(hWaitEvent);
            WaitForSingleObject(hRegisterVBoxThread, INFINITE);
            CloseHandle(hRegisterVBoxThread);
            CloseHandle(hWaitEvent);
        }

        g_pModule->RevokeClassObjects();
    }

    g_pModule->Term();

#ifdef VBOX_WITH_SDS
    g_fRegisteredWithVBoxSDS = false; /* Don't trust COM LPC to work right from now on.  */
#endif
    com::Shutdown();

    if(g_pModule)
        delete g_pModule;
    g_pModule = NULL;

    Log(("SVCMAIN: Returning, COM server process ends.\n"));
    return nRet;
}
