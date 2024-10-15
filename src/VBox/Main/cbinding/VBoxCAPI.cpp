/* $Id: VBoxCAPI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file VBoxCAPI.cpp
 * Utility functions to use with the C API binding.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN

#include "VBoxCAPI.h"

#ifdef VBOX_WITH_XPCOM
# include <nsMemory.h>
# include <nsIServiceManager.h>
# include <nsEventQueueUtils.h>
# include <nsIExceptionService.h>
# include <stdlib.h>
#endif /* VBOX_WITH_XPCOM */

#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/uuid.h>
#include <VBox/log.h>
#include <VBox/version.h>

#include "VBox/com/com.h"
#include "VBox/com/NativeEventQueue.h"


#ifndef RT_OS_DARWIN /* Probably not used for xpcom, so clang gets upset: error: using directive refers to implicitly-defined namespace 'std' [-Werror]*/
using namespace std;
#endif

/* The following 2 object references should be eliminated once the legacy
 * way to initialize the COM/XPCOM C bindings is removed. */
static ISession            *g_Session           = NULL;
static IVirtualBox         *g_VirtualBox        = NULL;

#ifdef VBOX_WITH_XPCOM
/* This object reference should be eliminated once the legacy way of handling
 * the event queue (XPCOM specific) is removed. */
static nsIEventQueue       *g_EventQueue        = NULL;
#endif /* VBOX_WITH_XPCOM */

static void VBoxComUninitialize(void);
static void VBoxClientUninitialize(void);

static int
VBoxUtf16ToUtf8(CBSTR pwszString, char **ppszString)
{
    if (!pwszString)
    {
        *ppszString = NULL;
        return VINF_SUCCESS;
    }
    return RTUtf16ToUtf8(pwszString, ppszString);
}

static int
VBoxUtf8ToUtf16(const char *pszString, BSTR *ppwszString)
{
    *ppwszString = NULL;
    if (!pszString)
        return VINF_SUCCESS;
#ifdef VBOX_WITH_XPCOM
    return RTStrToUtf16(pszString, ppwszString);
#else /* !VBOX_WITH_XPCOM */
    PRTUTF16 pwsz;
    int vrc = RTStrToUtf16(pszString, &pwsz);
    if (RT_SUCCESS(vrc))
    {
        *ppwszString = ::SysAllocString(pwsz);
        if (!*ppwszString)
            vrc = VERR_NO_STR_MEMORY;
        RTUtf16Free(pwsz);
    }
    return vrc;
#endif /* !VBOX_WITH_XPCOM */
}

static void
VBoxUtf8Clear(char *pszString)
{
    RT_BZERO(pszString, strlen(pszString));
}

static void
VBoxUtf16Clear(BSTR pwszString)
{
    RT_BZERO(pwszString, RTUtf16Len(pwszString) * sizeof(RTUTF16));
}

static void
VBoxUtf16Free(BSTR pwszString)
{
#ifdef VBOX_WITH_XPCOM
    RTUtf16Free(pwszString);
#else
    ::SysFreeString(pwszString);
#endif
}

static void
VBoxUtf8Free(char *pszString)
{
    RTStrFree(pszString);
}

static void
VBoxComUnallocString(BSTR pwsz)
{
    if (pwsz)
    {
#ifdef VBOX_WITH_XPCOM
        nsMemory::Free(pwsz);
#else
        ::SysFreeString(pwsz);
#endif
    }
}

static void
VBoxComUnallocMem(void *pv)
{
    VBoxComUnallocString((BSTR)pv);
}

static ULONG
VBoxVTElemSize(VARTYPE vt)
{
    switch (vt)
    {
        case VT_BOOL:
        case VT_I1:
        case VT_UI1:
            return 1;
        case VT_I2:
        case VT_UI2:
            return 2;
        case VT_I4:
        case VT_UI4:
        case VT_HRESULT:
            return 4;
        case VT_I8:
        case VT_UI8:
            return 8;
        case VT_BSTR:
        case VT_DISPATCH:
        case VT_UNKNOWN:
            return sizeof(void *);
        default:
            return 0;
    }
}

static SAFEARRAY *
VBoxSafeArrayCreateVector(VARTYPE vt, LONG lLbound, ULONG cElements)
{
#ifdef VBOX_WITH_XPCOM
    NOREF(lLbound);
    ULONG cbElement = VBoxVTElemSize(vt);
    if (!cbElement)
        return NULL;
    SAFEARRAY *psa = (SAFEARRAY *)RTMemAllocZ(sizeof(SAFEARRAY));
    if (!psa)
        return psa;
    if (cElements)
    {
        void *pv = nsMemory::Alloc(cElements * cbElement);
        if (!pv)
        {
            RTMemFree(psa);
            return NULL;
        }
        psa->pv = pv;
        psa->c = cElements;
    }
    return psa;
#else /* !VBOX_WITH_XPCOM */
    return SafeArrayCreateVector(vt, lLbound, cElements);
#endif /* !VBOX_WITH_XPCOM */
}

static SAFEARRAY *
VBoxSafeArrayOutParamAlloc(void)
{
#ifdef VBOX_WITH_XPCOM
    return (SAFEARRAY *)RTMemAllocZ(sizeof(SAFEARRAY));
#else /* !VBOX_WITH_XPCOM */
    return NULL;
#endif /* !VBOX_WITH_XPCOM */
}

static HRESULT
VBoxSafeArrayDestroy(SAFEARRAY *psa)
{
#ifdef VBOX_WITH_XPCOM
    if (psa)
    {
        if (psa->pv)
            nsMemory::Free(psa->pv);
        RTMemFree(psa);
    }
    return S_OK;
#else /* !VBOX_WITH_XPCOM */
    VARTYPE vt = VT_UNKNOWN;
    HRESULT hrc = SafeArrayGetVartype(psa, &vt);
    if (FAILED(hrc))
        return hrc;
    if (vt == VT_BSTR)
    {
        /* Special treatment: strings are to be freed explicitly, see sample
         * C binding code, so zap it here. No way to reach compatible code
         * behavior between COM and XPCOM without this kind of trickery. */
        void *pData;
        hrc = SafeArrayAccessData(psa, &pData);
        if (FAILED(hrc))
            return hrc;
        ULONG cbElement = VBoxVTElemSize(vt);
        if (!cbElement)
            return E_INVALIDARG;
        Assert(cbElement = psa->cbElements);
        ULONG cElements = psa->rgsabound[0].cElements;
        memset(pData, '\0', cbElement * cElements);
        SafeArrayUnaccessData(psa);
    }
    return SafeArrayDestroy(psa);
#endif /* !VBOX_WITH_XPCOM */
}

static HRESULT
VBoxSafeArrayCopyInParamHelper(SAFEARRAY *psa, const void *pv, ULONG cb)
{
    if (!pv || !psa)
        return E_POINTER;
    if (!cb)
        return S_OK;

    void *pData;
#ifdef VBOX_WITH_XPCOM
    pData = psa->pv;
#else /* !VBOX_WITH_XPCOM */
    HRESULT hrc = SafeArrayAccessData(psa, &pData);
    if (FAILED(hrc))
        return hrc;
#endif /* !VBOX_WITH_XPCOM */
    memcpy(pData, pv, cb);
#ifndef VBOX_WITH_XPCOM
    SafeArrayUnaccessData(psa);
#endif
    return S_OK;
}

static HRESULT
VBoxSafeArrayCopyOutParamHelper(void **ppv, ULONG *pcb, VARTYPE vt, SAFEARRAY *psa)
{
    if (!ppv)
        return E_POINTER;
    ULONG cbElement = VBoxVTElemSize(vt);
    if (!cbElement)
    {
        *ppv = NULL;
        if (pcb)
            *pcb = 0;
        return E_INVALIDARG;
    }
#ifndef VBOX_WITH_XPCOM
    if (psa->cDims != 1)
    {
        *ppv = NULL;
        if (pcb)
            *pcb = 0;
        return E_INVALIDARG;
    }
    Assert(cbElement = psa->cbElements);
#endif /* !VBOX_WITH_XPCOM */
    void *pData;
    ULONG cElements;
#ifdef VBOX_WITH_XPCOM
    pData = psa->pv;
    cElements = psa->c;
#else /* !VBOX_WITH_XPCOM */
    HRESULT hrc = SafeArrayAccessData(psa, &pData);
    if (FAILED(hrc))
    {
        *ppv = NULL;
        if (pcb)
            *pcb = 0;
        return hrc;
    }
    cElements = psa->rgsabound[0].cElements;
#endif /* !VBOX_WITH_XPCOM */
    size_t cbTotal = cbElement * cElements;
    void *pv = NULL;
    if (cbTotal)
    {
        pv = malloc(cbTotal);
        if (!pv)
        {
            *ppv = NULL;
            if (pcb)
                *pcb = 0;
            return E_OUTOFMEMORY;
        }
        else
            memcpy(pv, pData, cbTotal);
    }
    *ppv = pv;
    if (pcb)
        *pcb = (ULONG)cbTotal;
#ifndef VBOX_WITH_XPCOM
    SafeArrayUnaccessData(psa);
#endif
    return S_OK;
}

static HRESULT
VBoxSafeArrayCopyOutIfaceParamHelper(IUnknown ***ppaObj, ULONG *pcObj, SAFEARRAY *psa)
{
    ULONG mypcb;
    HRESULT hrc = VBoxSafeArrayCopyOutParamHelper((void **)ppaObj, &mypcb, VT_UNKNOWN, psa);
    if (FAILED(hrc))
    {
        if (pcObj)
            *pcObj = 0;
        return hrc;
    }
    ULONG cElements = mypcb / sizeof(void *);
    if (pcObj)
        *pcObj = cElements;
#ifndef VBOX_WITH_XPCOM
    /* Do this only for COM, as there the SAFEARRAY destruction will release
     * the contained references automatically. XPCOM doesn't do that, which
     * means that copying implicitly transfers ownership. */
    IUnknown **paObj = *ppaObj;
    for (ULONG i = 0; i < cElements; i++)
    {
        IUnknown *pObj = paObj[i];
        if (pObj)
            pObj->AddRef();
    }
#endif /* VBOX_WITH_XPCOM */
    return S_OK;
}

static HRESULT
VBoxArrayOutFree(void *pv)
{
    free(pv);
    return S_OK;
}

static void
VBoxComInitialize(const char *pszVirtualBoxIID, IVirtualBox **ppVirtualBox,
                  const char *pszSessionIID, ISession **ppSession)
{
    int vrc;
    IID virtualBoxIID;
    IID sessionIID;

    *ppSession    = NULL;
    *ppVirtualBox = NULL;

    /* convert the string representation of the UUIDs (if provided) to IID */
    if (pszVirtualBoxIID && *pszVirtualBoxIID)
    {
        vrc = ::RTUuidFromStr((RTUUID *)&virtualBoxIID, pszVirtualBoxIID);
        if (RT_FAILURE(vrc))
            return;
    }
    else
        virtualBoxIID = IID_IVirtualBox;
    if (pszSessionIID && *pszSessionIID)
    {
        vrc = ::RTUuidFromStr((RTUUID *)&sessionIID, pszSessionIID);
        if (RT_FAILURE(vrc))
            return;
    }
    else
        sessionIID = IID_ISession;

    HRESULT hrc = com::Initialize(VBOX_COM_INIT_F_DEFAULT | VBOX_COM_INIT_F_NO_COM_PATCHING);
    if (FAILED(hrc))
    {
        Log(("Cbinding: COM/XPCOM could not be initialized! hrc=%Rhrc\n", hrc));
        VBoxComUninitialize();
        return;
    }

#ifdef VBOX_WITH_XPCOM
    hrc = NS_GetMainEventQ(&g_EventQueue);
    if (FAILED(hrc))
    {
        Log(("Cbinding: Could not get XPCOM event queue! hrc=%Rhrc\n", hrc));
        VBoxComUninitialize();
        return;
    }
#endif /* VBOX_WITH_XPCOM */

#ifdef VBOX_WITH_XPCOM
    nsIComponentManager *pManager = NULL;
    hrc = NS_GetComponentManager(&pManager);
    if (FAILED(hrc))
    {
        Log(("Cbinding: Could not get component manager! hrc=%Rhrc\n", hrc));
        VBoxComUninitialize();
        return;
    }

    hrc = pManager->CreateInstanceByContractID(NS_VIRTUALBOX_CONTRACTID,
                                               nsnull,
                                               virtualBoxIID,
                                               (void **)&g_VirtualBox);
#else /* !VBOX_WITH_XPCOM */
    IVirtualBoxClient *pVirtualBoxClient;
    hrc = CoCreateInstance(CLSID_VirtualBoxClient, NULL, CLSCTX_INPROC_SERVER, IID_IVirtualBoxClient, (void **)&pVirtualBoxClient);
    if (SUCCEEDED(hrc))
    {
        IVirtualBox *pVirtualBox;
        hrc = pVirtualBoxClient->get_VirtualBox(&pVirtualBox);
        if (SUCCEEDED(hrc))
        {
            hrc = pVirtualBox->QueryInterface(virtualBoxIID, (void **)&g_VirtualBox);
            pVirtualBox->Release();
        }
        pVirtualBoxClient->Release();
    }
#endif /* !VBOX_WITH_XPCOM */
    if (FAILED(hrc))
    {
        Log(("Cbinding: Could not instantiate VirtualBox object! hrc=%Rhrc\n",hrc));
#ifdef VBOX_WITH_XPCOM
        pManager->Release();
        pManager = NULL;
#endif /* VBOX_WITH_XPCOM */
        VBoxComUninitialize();
        return;
    }

    Log(("Cbinding: IVirtualBox object created.\n"));

#ifdef VBOX_WITH_XPCOM
    hrc = pManager->CreateInstanceByContractID(NS_SESSION_CONTRACTID, nsnull, sessionIID, (void **)&g_Session);
#else /* !VBOX_WITH_XPCOM */
    hrc = CoCreateInstance(CLSID_Session, NULL, CLSCTX_INPROC_SERVER, sessionIID, (void **)&g_Session);
#endif /* !VBOX_WITH_XPCOM */
    if (FAILED(hrc))
    {
        Log(("Cbinding: Could not instantiate Session object! hrc=%Rhrc\n",hrc));
#ifdef VBOX_WITH_XPCOM
        pManager->Release();
        pManager = NULL;
#endif /* VBOX_WITH_XPCOM */
        VBoxComUninitialize();
        return;
    }

    Log(("Cbinding: ISession object created.\n"));

#ifdef VBOX_WITH_XPCOM
    pManager->Release();
    pManager = NULL;
#endif /* VBOX_WITH_XPCOM */

    *ppSession = g_Session;
    *ppVirtualBox = g_VirtualBox;
}

static void
VBoxComInitializeV1(IVirtualBox **ppVirtualBox, ISession **ppSession)
{
    VBoxComInitialize(NULL, ppVirtualBox, NULL, ppSession);
}

static void
VBoxComUninitialize(void)
{
    if (g_Session)
    {
        g_Session->Release();
        g_Session = NULL;
    }
    if (g_VirtualBox)
    {
        g_VirtualBox->Release();
        g_VirtualBox = NULL;
    }
#ifdef VBOX_WITH_XPCOM
    if (g_EventQueue)
    {
        g_EventQueue->Release();
        g_EventQueue = NULL;
    }
#endif /* VBOX_WITH_XPCOM */
    com::Shutdown();
    Log(("Cbinding: Cleaned up the created objects.\n"));
}

#ifdef VBOX_WITH_XPCOM
static void
VBoxGetEventQueue(nsIEventQueue **ppEventQueue)
{
    *ppEventQueue = g_EventQueue;
}
#endif /* VBOX_WITH_XPCOM */

static int
VBoxProcessEventQueue(LONG64 iTimeoutMS)
{
    RTMSINTERVAL iTimeout;
    if (iTimeoutMS < 0 || iTimeoutMS > UINT32_MAX)
        iTimeout = RT_INDEFINITE_WAIT;
    else
        iTimeout = (RTMSINTERVAL)iTimeoutMS;
    int vrc = com::NativeEventQueue::getMainEventQueue()->processEventQueue(iTimeout);
    switch (vrc)
    {
        case VINF_SUCCESS:
            return 0;
        case VINF_INTERRUPTED:
            return 1;
        case VERR_INTERRUPTED:
            return 2;
        case VERR_TIMEOUT:
            return 3;
        case VERR_INVALID_CONTEXT:
            return 4;
        default:
            return 5;
    }
}

static int
VBoxInterruptEventQueueProcessing(void)
{
    com::NativeEventQueue::getMainEventQueue()->interruptEventQueueProcessing();
    return 0;
}

static HRESULT
VBoxGetException(IErrorInfo **ppException)
{
    HRESULT hrc;

    *ppException = NULL;

#ifdef VBOX_WITH_XPCOM
    nsIServiceManager *mgr = NULL;
    hrc = NS_GetServiceManager(&mgr);
    if (FAILED(hrc) || !mgr)
        return hrc;

    IID esid = NS_IEXCEPTIONSERVICE_IID;
    nsIExceptionService *es = NULL;
    hrc = mgr->GetServiceByContractID(NS_EXCEPTIONSERVICE_CONTRACTID, esid, (void **)&es);
    if (FAILED(hrc) || !es)
    {
        mgr->Release();
        return hrc;
    }

    nsIExceptionManager *em;
    hrc = es->GetCurrentExceptionManager(&em);
    if (FAILED(hrc) || !em)
    {
        es->Release();
        mgr->Release();
        return hrc;
    }

    nsIException *ex;
    hrc = em->GetCurrentException(&ex);
    if (FAILED(hrc))
    {
        em->Release();
        es->Release();
        mgr->Release();
        return hrc;
    }

    *ppException = ex;
    em->Release();
    es->Release();
    mgr->Release();
#else /* !VBOX_WITH_XPCOM */
    IErrorInfo *ex;
    hrc = ::GetErrorInfo(0, &ex);
    if (FAILED(hrc))
        return hrc;

    *ppException = ex;
#endif /* !VBOX_WITH_XPCOM */

    return hrc;
}

static HRESULT
VBoxClearException(void)
{
    HRESULT hrc;

#ifdef VBOX_WITH_XPCOM
    nsIServiceManager *mgr = NULL;
    hrc = NS_GetServiceManager(&mgr);
    if (FAILED(hrc) || !mgr)
        return hrc;

    IID esid = NS_IEXCEPTIONSERVICE_IID;
    nsIExceptionService *es = NULL;
    hrc = mgr->GetServiceByContractID(NS_EXCEPTIONSERVICE_CONTRACTID, esid, (void **)&es);
    if (FAILED(hrc) || !es)
    {
        mgr->Release();
        return hrc;
    }

    nsIExceptionManager *em;
    hrc = es->GetCurrentExceptionManager(&em);
    if (FAILED(hrc) || !em)
    {
        es->Release();
        mgr->Release();
        return hrc;
    }

    hrc = em->SetCurrentException(NULL);
    em->Release();
    es->Release();
    mgr->Release();
#else /* !VBOX_WITH_XPCOM */
    hrc = ::SetErrorInfo(0, NULL);
#endif /* !VBOX_WITH_XPCOM */

    return hrc;
}

static HRESULT
VBoxClientInitialize(const char *pszVirtualBoxClientIID, IVirtualBoxClient **ppVirtualBoxClient)
{
    IID virtualBoxClientIID;

    *ppVirtualBoxClient = NULL;

    /* convert the string representation of UUID to IID type */
    if (pszVirtualBoxClientIID && *pszVirtualBoxClientIID)
    {
        int vrc = ::RTUuidFromStr((RTUUID *)&virtualBoxClientIID, pszVirtualBoxClientIID);
        if (RT_FAILURE(vrc))
            return E_INVALIDARG;
    }
    else
        virtualBoxClientIID = IID_IVirtualBoxClient;

    HRESULT hrc = com::Initialize(VBOX_COM_INIT_F_DEFAULT | VBOX_COM_INIT_F_NO_COM_PATCHING);
    if (FAILED(hrc))
    {
        Log(("Cbinding: COM/XPCOM could not be initialized! hrc=%Rhrc\n", hrc));
        VBoxClientUninitialize();
        return hrc;
    }

#ifdef VBOX_WITH_XPCOM
    hrc = NS_GetMainEventQ(&g_EventQueue);
    if (NS_FAILED(hrc))
    {
        Log(("Cbinding: Could not get XPCOM event queue! hrc=%Rhrc\n", hrc));
        VBoxClientUninitialize();
        return hrc;
    }
#endif /* VBOX_WITH_XPCOM */

#ifdef VBOX_WITH_XPCOM
    nsIComponentManager *pManager = NULL;
    hrc = NS_GetComponentManager(&pManager);
    if (FAILED(hrc))
    {
        Log(("Cbinding: Could not get component manager! hrc=%Rhrc\n", hrc));
        VBoxClientUninitialize();
        return hrc;
    }

    hrc = pManager->CreateInstanceByContractID(NS_VIRTUALBOXCLIENT_CONTRACTID,
                                               nsnull,
                                               virtualBoxClientIID,
                                               (void **)ppVirtualBoxClient);
#else /* !VBOX_WITH_XPCOM */
    hrc = CoCreateInstance(CLSID_VirtualBoxClient, NULL, CLSCTX_INPROC_SERVER, virtualBoxClientIID, (void **)ppVirtualBoxClient);
#endif /* !VBOX_WITH_XPCOM */
    if (FAILED(hrc))
    {
        Log(("Cbinding: Could not instantiate VirtualBoxClient object! hrc=%Rhrc\n",hrc));
#ifdef VBOX_WITH_XPCOM
        pManager->Release();
        pManager = NULL;
#endif /* VBOX_WITH_XPCOM */
        VBoxClientUninitialize();
        return hrc;
    }

#ifdef VBOX_WITH_XPCOM
    pManager->Release();
    pManager = NULL;
#endif /* VBOX_WITH_XPCOM */

    Log(("Cbinding: IVirtualBoxClient object created.\n"));

    return S_OK;
}

static HRESULT
VBoxClientThreadInitialize(void)
{
    return com::Initialize(VBOX_COM_INIT_F_DEFAULT | VBOX_COM_INIT_F_NO_COM_PATCHING);
}

static HRESULT
VBoxClientThreadUninitialize(void)
{
    return com::Shutdown();
}

static void
VBoxClientUninitialize(void)
{
#ifdef VBOX_WITH_XPCOM
    if (g_EventQueue)
    {
        NS_RELEASE(g_EventQueue);
        g_EventQueue = NULL;
    }
#endif /* VBOX_WITH_XPCOM */
    com::Shutdown();
    Log(("Cbinding: Cleaned up the created objects.\n"));
}

static unsigned int
VBoxVersion(void)
{
    return VBOX_VERSION_MAJOR * 1000 * 1000 + VBOX_VERSION_MINOR * 1000 + VBOX_VERSION_BUILD;
}

static unsigned int
VBoxAPIVersion(void)
{
    return VBOX_VERSION_MAJOR * 1000 + VBOX_VERSION_MINOR + (VBOX_VERSION_BUILD > 50 ? 1 : 0);
}

VBOXCAPI_DECL(PCVBOXCAPI)
VBoxGetCAPIFunctions(unsigned uVersion)
{
    /* This is the first piece of code which knows that IPRT exists, so
     * initialize it properly. The limited initialization in VBoxC is not
     * sufficient, and causes trouble with com::Initialize() misbehaving. */
    RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);

    /*
     * The current interface version.
     */
    static const VBOXCAPI s_Functions =
    {
        sizeof(VBOXCAPI),
        VBOX_CAPI_VERSION,

        VBoxVersion,
        VBoxAPIVersion,

        VBoxClientInitialize,
        VBoxClientThreadInitialize,
        VBoxClientThreadUninitialize,
        VBoxClientUninitialize,

        VBoxComInitialize,
        VBoxComUninitialize,

        VBoxComUnallocString,

        VBoxUtf16ToUtf8,
        VBoxUtf8ToUtf16,
        VBoxUtf8Free,
        VBoxUtf16Free,

        VBoxSafeArrayCreateVector,
        VBoxSafeArrayOutParamAlloc,
        VBoxSafeArrayCopyInParamHelper,
        VBoxSafeArrayCopyOutParamHelper,
        VBoxSafeArrayCopyOutIfaceParamHelper,
        VBoxSafeArrayDestroy,
        VBoxArrayOutFree,

#ifdef VBOX_WITH_XPCOM
        VBoxGetEventQueue,
#endif /* VBOX_WITH_XPCOM */
        VBoxGetException,
        VBoxClearException,
        VBoxProcessEventQueue,
        VBoxInterruptEventQueueProcessing,

        VBoxUtf8Clear,
        VBoxUtf16Clear,

        VBOX_CAPI_VERSION
    };

    if ((uVersion & 0xffff0000U) == (VBOX_CAPI_VERSION & 0xffff0000U))
        return &s_Functions;

    /*
     * Legacy interface version 3.0.
     */
    static const struct VBOXCAPIV3
    {
        /** The size of the structure. */
        unsigned cb;
        /** The structure version. */
        unsigned uVersion;

        unsigned int (*pfnGetVersion)(void);

        unsigned int (*pfnGetAPIVersion)(void);

        HRESULT (*pfnClientInitialize)(const char *pszVirtualBoxClientIID,
                                       IVirtualBoxClient **ppVirtualBoxClient);
        void (*pfnClientUninitialize)(void);

        void (*pfnComInitialize)(const char *pszVirtualBoxIID,
                                 IVirtualBox **ppVirtualBox,
                                 const char *pszSessionIID,
                                 ISession **ppSession);

        void (*pfnComUninitialize)(void);

        void (*pfnComUnallocMem)(void *pv);

        int (*pfnUtf16ToUtf8)(CBSTR pwszString, char **ppszString);
        int (*pfnUtf8ToUtf16)(const char *pszString, BSTR *ppwszString);
        void (*pfnUtf8Free)(char *pszString);
        void (*pfnUtf16Free)(BSTR pwszString);

#ifdef VBOX_WITH_XPCOM
        void (*pfnGetEventQueue)(nsIEventQueue **ppEventQueue);
#endif /* VBOX_WITH_XPCOM */
        HRESULT (*pfnGetException)(IErrorInfo **ppException);
        HRESULT (*pfnClearException)(void);

        /** Tail version, same as uVersion. */
        unsigned uEndVersion;
    } s_Functions_v3_0 =
    {
        sizeof(s_Functions_v3_0),
        0x00030000U,

        VBoxVersion,
        VBoxAPIVersion,

        VBoxClientInitialize,
        VBoxClientUninitialize,

        VBoxComInitialize,
        VBoxComUninitialize,

        VBoxComUnallocMem,

        VBoxUtf16ToUtf8,
        VBoxUtf8ToUtf16,
        VBoxUtf8Free,
        VBoxUtf16Free,

#ifdef VBOX_WITH_XPCOM
        VBoxGetEventQueue,
#endif /* VBOX_WITH_XPCOM */
        VBoxGetException,
        VBoxClearException,

        0x00030000U
    };

    if ((uVersion & 0xffff0000U) == 0x00030000U)
        return (PCVBOXCAPI)&s_Functions_v3_0;

    /*
     * Legacy interface version 2.0.
     */
    static const struct VBOXCAPIV2
    {
        /** The size of the structure. */
        unsigned cb;
        /** The structure version. */
        unsigned uVersion;

        unsigned int (*pfnGetVersion)(void);

        void (*pfnComInitialize)(const char *pszVirtualBoxIID,
                                 IVirtualBox **ppVirtualBox,
                                 const char *pszSessionIID,
                                 ISession **ppSession);

        void (*pfnComUninitialize)(void);

        void (*pfnComUnallocMem)(void *pv);
        void (*pfnUtf16Free)(BSTR pwszString);
        void (*pfnUtf8Free)(char *pszString);

        int (*pfnUtf16ToUtf8)(CBSTR pwszString, char **ppszString);
        int (*pfnUtf8ToUtf16)(const char *pszString, BSTR *ppwszString);

#ifdef VBOX_WITH_XPCOM
        void (*pfnGetEventQueue)(nsIEventQueue **ppEventQueue);
#endif /* VBOX_WITH_XPCOM */

        /** Tail version, same as uVersion. */
        unsigned uEndVersion;
    } s_Functions_v2_0 =
    {
        sizeof(s_Functions_v2_0),
        0x00020000U,

        VBoxVersion,

        VBoxComInitialize,
        VBoxComUninitialize,

        VBoxComUnallocMem,
        VBoxUtf16Free,
        VBoxUtf8Free,

        VBoxUtf16ToUtf8,
        VBoxUtf8ToUtf16,

#ifdef VBOX_WITH_XPCOM
        VBoxGetEventQueue,
#endif /* VBOX_WITH_XPCOM */

        0x00020000U
    };

    if ((uVersion & 0xffff0000U) == 0x00020000U)
        return (PCVBOXCAPI)&s_Functions_v2_0;

    /*
     * Legacy interface version 1.0.
     */
    static const struct VBOXCAPIV1
    {
        /** The size of the structure. */
        unsigned cb;
        /** The structure version. */
        unsigned uVersion;

        unsigned int (*pfnGetVersion)(void);

        void (*pfnComInitialize)(IVirtualBox **virtualBox, ISession **session);
        void (*pfnComUninitialize)(void);

        void (*pfnComUnallocMem)(void *pv);
        void (*pfnUtf16Free)(BSTR pwszString);
        void (*pfnUtf8Free)(char *pszString);

        int (*pfnUtf16ToUtf8)(CBSTR pwszString, char **ppszString);
        int (*pfnUtf8ToUtf16)(const char *pszString, BSTR *ppwszString);

        /** Tail version, same as uVersion. */
        unsigned uEndVersion;
    } s_Functions_v1_0 =
    {
        sizeof(s_Functions_v1_0),
        0x00010000U,

        VBoxVersion,

        VBoxComInitializeV1,
        VBoxComUninitialize,

        VBoxComUnallocMem,
        VBoxUtf16Free,
        VBoxUtf8Free,

        VBoxUtf16ToUtf8,
        VBoxUtf8ToUtf16,

        0x00010000U
    };

    if ((uVersion & 0xffff0000U) == 0x00010000U)
        return (PCVBOXCAPI)&s_Functions_v1_0;

    /*
     * Unsupported interface version.
     */
    return NULL;
}

#ifdef VBOX_WITH_XPCOM
VBOXCAPI_DECL(PCVBOXCAPI)
VBoxGetXPCOMCFunctions(unsigned uVersion)
{
    return VBoxGetCAPIFunctions(uVersion);
}
#endif /* VBOX_WITH_XPCOM */
/* vim: set ts=4 sw=4 et: */
