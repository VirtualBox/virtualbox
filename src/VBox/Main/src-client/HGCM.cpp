/* $Id: HGCM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HGCM (Host-Guest Communication Manager)
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define LOG_GROUP LOG_GROUP_HGCM
#include "LoggingNew.h"

#include "HGCM.h"
#include "HGCMThread.h"

#include <VBox/err.h>
#include <VBox/hgcmsvc.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/sup.h>
#include <VBox/AssertGuest.h>

#include <iprt/alloc.h>
#include <iprt/avl.h>
#include <iprt/critsect.h>
#include <iprt/asm.h>
#include <iprt/ldr.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>

#include <VBox/VMMDev.h>
#include <new>

/**
 * A service gets one thread, which synchronously delivers messages to
 * the service. This is good for serialization.
 *
 * Some services may want to process messages asynchronously, and will want
 * a next message to be delivered, while a previous message is still being
 * processed.
 *
 * The dedicated service thread delivers a next message when service
 * returns after fetching a previous one. The service will call a message
 * completion callback when message is actually processed. So returning
 * from the service call means only that the service is processing message.
 *
 * 'Message processed' condition is indicated by service, which call the
 * callback, even if the callback is called synchronously in the dedicated
 * thread.
 *
 * This message completion callback is only valid for Call requests.
 * Connect and Disconnect are processed synchronously by the service.
 */


/* The maximum allowed size of a service name in bytes. */
#define VBOX_HGCM_SVC_NAME_MAX_BYTES 1024

struct _HGCMSVCEXTHANDLEDATA
{
    char *pszServiceName;
    /* The service name follows. */
};

class HGCMClient;

/** Internal helper service object. HGCM code would use it to
 *  hold information about services and communicate with services.
 *  The HGCMService is an (in future) abstract class that implements
 *  common functionality. There will be derived classes for specific
 *  service types.
 */

class HGCMService
{
    private:
        VBOXHGCMSVCHELPERS m_svcHelpers;

        static HGCMService *sm_pSvcListHead;
        static HGCMService *sm_pSvcListTail;

        static int sm_cServices;

        HGCMThread *m_pThread;
        friend DECLCALLBACK(void) hgcmServiceThread(HGCMThread *pThread, void *pvUser);

        uint32_t volatile m_u32RefCnt;

        HGCMService *m_pSvcNext;
        HGCMService *m_pSvcPrev;

        char *m_pszSvcName;
        char *m_pszSvcLibrary;

        RTLDRMOD m_hLdrMod;
        PFNVBOXHGCMSVCLOAD m_pfnLoad;

        VBOXHGCMSVCFNTABLE m_fntable;

        /** Set if servicing SVC_MSG_CONNECT or SVC_MSG_DISCONNECT.
         * Used for context checking pfnDisconnectClient calls, as it can only
         * safely be made when the main HGCM thread is waiting on the service to
         * process those messages. */
        bool m_fInConnectOrDisconnect;

        uint32_t m_acClients[HGCM_CLIENT_CATEGORY_MAX]; /**< Clients per category. */
        uint32_t m_cClients;
        uint32_t m_cClientsAllocated;

        uint32_t *m_paClientIds;

        HGCMSVCEXTHANDLE m_hExtension;

        PUVM m_pUVM;
        PCVMMR3VTABLE m_pVMM;
        PPDMIHGCMPORT m_pHgcmPort;

        /** @name Statistics
         * @{ */
        STAMPROFILE m_StatHandleMsg;
        STAMCOUNTER m_StatTooManyClients;
        STAMCOUNTER m_StatTooManyCalls;
        /** @} */

        int loadServiceDLL(void);
        void unloadServiceDLL(void);

        /*
         * Main HGCM thread methods.
         */
        int instanceCreate(const char *pszServiceLibrary, const char *pszServiceName,
                           PUVM pUVM, PCVMMR3VTABLE pVMM, PPDMIHGCMPORT pHgcmPort);
        void registerStatistics(const char *pszServiceName, PUVM pUVM, PCVMMR3VTABLE pVMM);
        void instanceDestroy(void);

        int saveClientState(uint32_t u32ClientId, PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM);
        int loadClientState(uint32_t u32ClientId, PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t uVersion);

        HGCMService();
        ~HGCMService() {};

        static DECLCALLBACK(int)  svcHlpCallComplete(VBOXHGCMCALLHANDLE callHandle, int32_t vrc);
        static DECLCALLBACK(int)  svcHlpDisconnectClient(void *pvInstance, uint32_t idClient);
        static DECLCALLBACK(bool) svcHlpIsCallRestored(VBOXHGCMCALLHANDLE callHandle);
        static DECLCALLBACK(bool) svcHlpIsCallCancelled(VBOXHGCMCALLHANDLE callHandle);
        static DECLCALLBACK(int)  svcHlpStamRegisterV(void *pvInstance, void *pvSample, STAMTYPE enmType,
                                                      STAMVISIBILITY enmVisibility, STAMUNIT enmUnit, const char *pszDesc,
                                                      const char *pszName, va_list va);
        static DECLCALLBACK(int)  svcHlpStamDeregisterV(void *pvInstance, const char *pszPatFmt, va_list va);
        static DECLCALLBACK(int)  svcHlpInfoRegister(void *pvInstance, const char *pszName, const char *pszDesc,
                                                     PFNDBGFHANDLEREXT pfnHandler, void *pvUser);
        static DECLCALLBACK(int)  svcHlpInfoDeregister(void *pvInstance, const char *pszName);
        static DECLCALLBACK(uint32_t) svcHlpGetRequestor(VBOXHGCMCALLHANDLE hCall);
        static DECLCALLBACK(uint64_t) svcHlpGetVMMDevSessionId(void *pvInstance);

    public:

        /*
         * Main HGCM thread methods.
         */
        static int LoadService(const char *pszServiceLibrary, const char *pszServiceName,
                               PUVM pUVM, PCVMMR3VTABLE pVMM, PPDMIHGCMPORT pHgcmPort);
        void UnloadService(bool fUvmIsInvalid);

        static void UnloadAll(bool fUvmIsInvalid);

        static int ResolveService(HGCMService **ppsvc, const char *pszServiceName);
        void ReferenceService(void);
        void ReleaseService(void);

        static void Reset(void);

        static int SaveState(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM);
        static int LoadState(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t uVersion);

        int CreateAndConnectClient(uint32_t *pu32ClientIdOut, uint32_t u32ClientIdIn, uint32_t fRequestor, bool fRestoring);
        int DisconnectClient(uint32_t u32ClientId, bool fFromService, HGCMClient *pClient);

        int HostCall(uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM *paParms);
        static void BroadcastNotify(HGCMNOTIFYEVENT enmEvent);
        void Notify(HGCMNOTIFYEVENT enmEvent);

        uint32_t SizeOfClient(void) { return m_fntable.cbClient; };

        int RegisterExtension(HGCMSVCEXTHANDLE handle, PFNHGCMSVCEXT pfnExtension, void *pvExtension);
        void UnregisterExtension(HGCMSVCEXTHANDLE handle);

        /*
         * The service thread methods.
         */

        int GuestCall(PPDMIHGCMPORT pHGCMPort, PVBOXHGCMCMD pCmd, uint32_t u32ClientId, HGCMClient *pClient,
                      uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM aParms[], uint64_t tsArrival);
        void GuestCancelled(PPDMIHGCMPORT pHGCMPort, PVBOXHGCMCMD pCmd, uint32_t idClient);
};


class HGCMClient: public HGCMObject
{
    public:
        HGCMClient(uint32_t a_fRequestor, uint32_t a_idxCategory)
            : HGCMObject(HGCMOBJ_CLIENT)
            , pService(NULL)
            , pvData(NULL)
            , fRequestor(a_fRequestor)
            , idxCategory(a_idxCategory)
            , cPendingCalls(0)
            , m_fGuestAccessible(false)
        {
            Assert(idxCategory < HGCM_CLIENT_CATEGORY_MAX);
        }
        ~HGCMClient();

        int Init(HGCMService *pSvc);

        /** Lookups a client object by its handle. */
        static HGCMClient *ReferenceByHandle(uint32_t idClient)
        {
            return (HGCMClient *)hgcmObjReference(idClient, HGCMOBJ_CLIENT);
        }

        /** Lookups a client object by its handle and makes sure that it's accessible to the guest. */
        static HGCMClient *ReferenceByHandleForGuest(uint32_t idClient)
        {
            HGCMClient *pClient = (HGCMClient *)hgcmObjReference(idClient, HGCMOBJ_CLIENT);
            if (pClient)
            {
                if (RT_LIKELY(pClient->m_fGuestAccessible))
                    return pClient;
                pClient->Dereference();
            }
            return NULL;
        }

        /** Make the client object accessible to the guest. */
        void makeAccessibleToGuest()
        {
            ASMAtomicWriteBool(&m_fGuestAccessible, true);
        }

        /** Service that the client is connected to. */
        HGCMService *pService;

        /** Client specific data. */
        void *pvData;

        /** The requestor flags this client was created with.
         * @sa VMMDevRequestHeader::fRequestor */
        uint32_t fRequestor;

        /** The client category (HGCM_CLIENT_CATEGORY_XXX). */
        uint32_t idxCategory;

        /** Number of pending calls. */
        uint32_t volatile cPendingCalls;

    protected:
        /** Set if the client is accessible to the guest, clear if not. */
        bool volatile m_fGuestAccessible;

    private: /* none of this: */
        HGCMClient();
        HGCMClient(HGCMClient const &);
        HGCMClient &operator=(HGCMClient const &);
};

HGCMClient::~HGCMClient()
{
    if (pService->SizeOfClient() > 0)
    {
        RTMemFree(pvData);
        pvData = NULL;
    }
}


int HGCMClient::Init(HGCMService *pSvc)
{
    pService = pSvc;

    if (pService->SizeOfClient() > 0)
    {
        pvData = RTMemAllocZ(pService->SizeOfClient());

        if (!pvData)
        {
           return VERR_NO_MEMORY;
        }
    }

    return VINF_SUCCESS;
}


#define HGCM_CLIENT_DATA(pService, pClient)(pClient->pvData)



HGCMService *HGCMService::sm_pSvcListHead = NULL;
HGCMService *HGCMService::sm_pSvcListTail = NULL;
int HGCMService::sm_cServices = 0;

HGCMService::HGCMService()
    :
    m_pThread    (NULL),
    m_u32RefCnt  (0),
    m_pSvcNext   (NULL),
    m_pSvcPrev   (NULL),
    m_pszSvcName (NULL),
    m_pszSvcLibrary (NULL),
    m_hLdrMod    (NIL_RTLDRMOD),
    m_pfnLoad    (NULL),
    m_fInConnectOrDisconnect(false),
    m_cClients   (0),
    m_cClientsAllocated (0),
    m_paClientIds (NULL),
    m_hExtension (NULL),
    m_pUVM       (NULL),
    m_pVMM       (NULL),
    m_pHgcmPort  (NULL)
{
    RT_ZERO(m_acClients);
    RT_ZERO(m_fntable);
}


static bool g_fResetting = false;
static bool g_fSaveState = false;


/** Helper function to load a local service DLL.
 *
 *  @return VBox code
 */
int HGCMService::loadServiceDLL(void)
{
    LogFlowFunc(("m_pszSvcLibrary = %s\n", m_pszSvcLibrary));

    if (m_pszSvcLibrary == NULL)
    {
        return VERR_INVALID_PARAMETER;
    }

    RTERRINFOSTATIC ErrInfo;
    RTErrInfoInitStatic(&ErrInfo);

    int vrc;

    if (RTPathHasPath(m_pszSvcLibrary))
        vrc = SUPR3HardenedLdrLoadPlugIn(m_pszSvcLibrary, &m_hLdrMod, &ErrInfo.Core);
    else
        vrc = SUPR3HardenedLdrLoadAppPriv(m_pszSvcLibrary, &m_hLdrMod, RTLDRLOAD_FLAGS_LOCAL, &ErrInfo.Core);

    if (RT_SUCCESS(vrc))
    {
        LogFlowFunc(("successfully loaded the library.\n"));

        m_pfnLoad = NULL;

        vrc = RTLdrGetSymbol(m_hLdrMod, VBOX_HGCM_SVCLOAD_NAME, (void**)&m_pfnLoad);

        if (RT_FAILURE(vrc) || !m_pfnLoad)
        {
            Log(("HGCMService::loadServiceDLL: Error resolving the service entry point %s, vrc = %Rrc, m_pfnLoad = %p\n",
                 VBOX_HGCM_SVCLOAD_NAME, vrc, m_pfnLoad));

            if (RT_SUCCESS(vrc))
            {
                /* m_pfnLoad was NULL */
                vrc = VERR_SYMBOL_NOT_FOUND;
            }
        }

        if (RT_SUCCESS(vrc))
        {
            RT_ZERO(m_fntable);

            m_fntable.cbSize     = sizeof(m_fntable);
            m_fntable.u32Version = VBOX_HGCM_SVC_VERSION;
            m_fntable.pHelpers   = &m_svcHelpers;

            /*  Total max calls: (2048 + 1024 + 1024) * 8192 = 33 554 432 */
            m_fntable.idxLegacyClientCategory = HGCM_CLIENT_CATEGORY_KERNEL;
            m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_KERNEL] = _2K;
            m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_ROOT]   = _1K;
            m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_USER]   = _1K;
            m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_KERNEL] = _8K;
            m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_ROOT]   = _4K;
            m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_USER]   = _2K;
            /** @todo provide way to configure different values via extra data.   */

            vrc = m_pfnLoad(&m_fntable);

            LogFlowFunc(("m_pfnLoad vrc = %Rrc\n", vrc));

            if (RT_SUCCESS(vrc))
            {
                if (   m_fntable.pfnUnload != NULL
                    && m_fntable.pfnConnect != NULL
                    && m_fntable.pfnDisconnect != NULL
                    && m_fntable.pfnCall != NULL
                   )
                {
                    Assert(m_fntable.idxLegacyClientCategory < RT_ELEMENTS(m_fntable.acMaxClients));
                    LogRel2(("HGCMService::loadServiceDLL: acMaxClients={%u,%u,%u} acMaxCallsPerClient={%u,%u,%u} => %RU64 calls; idxLegacyClientCategory=%d; %s\n",
                             m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_KERNEL],
                             m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_ROOT],
                             m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_USER],
                             m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_KERNEL],
                             m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_ROOT],
                             m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_USER],
                                 (uint64_t)m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_KERNEL]
                               *    m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_KERNEL]
                             +   (uint64_t)m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_ROOT]
                               *    m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_ROOT]
                             +   (uint64_t)m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_USER]
                               *    m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_USER],
                             m_fntable.idxLegacyClientCategory, m_pszSvcName));
                }
                else
                {
                    Log(("HGCMService::loadServiceDLL: at least one of function pointers is NULL\n"));

                    vrc = VERR_INVALID_PARAMETER;

                    if (m_fntable.pfnUnload)
                    {
                        m_fntable.pfnUnload(m_fntable.pvService);
                    }
                }
            }
        }
    }
    else
    {
        LogRel(("HGCM: Failed to load the service library: [%s], vrc = %Rrc - %s. The service will be not available.\n",
                m_pszSvcLibrary, vrc, ErrInfo.Core.pszMsg));
        m_hLdrMod = NIL_RTLDRMOD;
    }

    if (RT_FAILURE(vrc))
    {
        unloadServiceDLL();
    }

    return vrc;
}

/**
 * Helper function to free a local service DLL.
 */
void HGCMService::unloadServiceDLL(void)
{
    if (m_hLdrMod)
    {
        RTLdrClose(m_hLdrMod);
    }

    RT_ZERO(m_fntable);
    m_pfnLoad = NULL;
    m_hLdrMod = NIL_RTLDRMOD;
}

/*
 * Messages processed by service threads. These threads only call the service entry points.
 */

#define SVC_MSG_LOAD            (0)  /**< Load the service library and call VBOX_HGCM_SVCLOAD_NAME entry point. */
#define SVC_MSG_UNLOAD          (1)  /**< call pfnUnload and unload the service library. */
#define SVC_MSG_CONNECT         (2)  /**< pfnConnect */
#define SVC_MSG_DISCONNECT      (3)  /**< pfnDisconnect */
#define SVC_MSG_GUESTCALL       (4)  /**< pfnGuestCall */
#define SVC_MSG_HOSTCALL        (5)  /**< pfnHostCall */
#define SVC_MSG_LOADSTATE       (6)  /**< pfnLoadState. */
#define SVC_MSG_SAVESTATE       (7)  /**< pfnSaveState. */
#define SVC_MSG_QUIT            (8)  /**< Terminate the thread. */
#define SVC_MSG_REGEXT          (9)  /**< pfnRegisterExtension */
#define SVC_MSG_UNREGEXT        (10) /**< pfnRegisterExtension */
#define SVC_MSG_NOTIFY          (11) /**< pfnNotify */
#define SVC_MSG_GUESTCANCELLED  (12) /**< pfnCancelled */

class HGCMMsgSvcLoad: public HGCMMsgCore
{
    public:
        HGCMMsgSvcLoad() : HGCMMsgCore(), pUVM() {}

        /** The user mode VM handle (for statistics and such). */
        PUVM pUVM;
};

class HGCMMsgSvcUnload: public HGCMMsgCore
{
};

class HGCMMsgSvcConnect: public HGCMMsgCore
{
    public:
        /** client identifier */
        uint32_t u32ClientId;
        /** Requestor flags. */
        uint32_t fRequestor;
        /** Set if restoring. */
        bool fRestoring;
};

class HGCMMsgSvcDisconnect: public HGCMMsgCore
{
    public:
        /** client identifier */
        uint32_t u32ClientId;
        /** The client instance. */
        HGCMClient *pClient;
};

class HGCMMsgHeader: public HGCMMsgCore
{
    public:
        HGCMMsgHeader() : pCmd(NULL), pHGCMPort(NULL) {};

        /* Command pointer/identifier. */
        PVBOXHGCMCMD pCmd;

        /* Port to be informed on message completion. */
        PPDMIHGCMPORT pHGCMPort;
};

class HGCMMsgCall: public HGCMMsgHeader
{
    public:
        HGCMMsgCall() : pcCounter(NULL)
        { }

        HGCMMsgCall(HGCMThread *pThread)
            : pcCounter(NULL)
        {
            InitializeCore(SVC_MSG_GUESTCALL, pThread);
            Initialize();
        }
        ~HGCMMsgCall()
        {
            Log(("~HGCMMsgCall %p\n", this));
            Assert(!pcCounter);
        }

        /** Points to HGCMClient::cPendingCalls if it needs to be decremented. */
        uint32_t volatile *pcCounter;

        /* client identifier */
        uint32_t u32ClientId;

        /* function number */
        uint32_t u32Function;

        /* number of parameters */
        uint32_t cParms;

        VBOXHGCMSVCPARM *paParms;

        /** The STAM_GET_TS() value when the request arrived. */
        uint64_t tsArrival;
};

class HGCMMsgCancelled: public HGCMMsgHeader
{
    public:
        HGCMMsgCancelled() {}

        HGCMMsgCancelled(HGCMThread *pThread)
        {
            InitializeCore(SVC_MSG_GUESTCANCELLED, pThread);
            Initialize();
        }
        ~HGCMMsgCancelled() { Log(("~HGCMMsgCancelled %p\n", this)); }

        /** The client identifier. */
        uint32_t idClient;
};

class HGCMMsgLoadSaveStateClient: public HGCMMsgCore
{
    public:
        PSSMHANDLE      pSSM;
        PCVMMR3VTABLE   pVMM;
        uint32_t        uVersion;
        uint32_t        u32ClientId;
};

class HGCMMsgHostCallSvc: public HGCMMsgCore
{
    public:
        /* function number */
        uint32_t u32Function;

        /* number of parameters */
        uint32_t cParms;

        VBOXHGCMSVCPARM *paParms;
};

class HGCMMsgSvcRegisterExtension: public HGCMMsgCore
{
    public:
        /* Handle of the extension to be registered. */
        HGCMSVCEXTHANDLE handle;
        /* The extension entry point. */
        PFNHGCMSVCEXT pfnExtension;
        /* The extension pointer. */
        void *pvExtension;
};

class HGCMMsgSvcUnregisterExtension: public HGCMMsgCore
{
    public:
        /* Handle of the registered extension. */
        HGCMSVCEXTHANDLE handle;
};

class HGCMMsgNotify: public HGCMMsgCore
{
    public:
        /** The event. */
        HGCMNOTIFYEVENT enmEvent;
};

static HGCMMsgCore *hgcmMessageAllocSvc(uint32_t u32MsgId)
{
    switch (u32MsgId)
    {
        case SVC_MSG_LOAD:        return new HGCMMsgSvcLoad();
        case SVC_MSG_UNLOAD:      return new HGCMMsgSvcUnload();
        case SVC_MSG_CONNECT:     return new HGCMMsgSvcConnect();
        case SVC_MSG_DISCONNECT:  return new HGCMMsgSvcDisconnect();
        case SVC_MSG_HOSTCALL:    return new HGCMMsgHostCallSvc();
        case SVC_MSG_GUESTCALL:   return new HGCMMsgCall();
        case SVC_MSG_LOADSTATE:
        case SVC_MSG_SAVESTATE:   return new HGCMMsgLoadSaveStateClient();
        case SVC_MSG_REGEXT:      return new HGCMMsgSvcRegisterExtension();
        case SVC_MSG_UNREGEXT:    return new HGCMMsgSvcUnregisterExtension();
        case SVC_MSG_NOTIFY:      return new HGCMMsgNotify();
        case SVC_MSG_GUESTCANCELLED: return new HGCMMsgCancelled();
        default:
            AssertReleaseMsgFailed(("Msg id = %08X\n", u32MsgId));
    }

    return NULL;
}

/*
 * The service thread. Loads the service library and calls the service entry points.
 */
DECLCALLBACK(void) hgcmServiceThread(HGCMThread *pThread, void *pvUser)
{
    HGCMService *pSvc = (HGCMService *)pvUser;
    AssertRelease(pSvc != NULL);

    bool fQuit = false;

    while (!fQuit)
    {
        HGCMMsgCore *pMsgCore;
        int vrc = hgcmMsgGet(pThread, &pMsgCore);

        if (RT_FAILURE(vrc))
        {
            /* The error means some serious unrecoverable problem in the hgcmMsg/hgcmThread layer. */
            AssertMsgFailed(("%Rrc\n", vrc));
            break;
        }

        STAM_REL_PROFILE_START(&pSvc->m_StatHandleMsg, a);

        /* Cache required information to avoid unnecessary pMsgCore access. */
        uint32_t u32MsgId = pMsgCore->MsgId();

        switch (u32MsgId)
        {
            case SVC_MSG_LOAD:
            {
                LogFlowFunc(("SVC_MSG_LOAD\n"));
                vrc = pSvc->loadServiceDLL();
            } break;

            case SVC_MSG_UNLOAD:
            {
                LogFlowFunc(("SVC_MSG_UNLOAD\n"));
                if (pSvc->m_fntable.pfnUnload)
                {
                    pSvc->m_fntable.pfnUnload(pSvc->m_fntable.pvService);
                }

                pSvc->unloadServiceDLL();
                fQuit = true;
            } break;

            case SVC_MSG_CONNECT:
            {
                HGCMMsgSvcConnect *pMsg = (HGCMMsgSvcConnect *)pMsgCore;

                LogFlowFunc(("SVC_MSG_CONNECT u32ClientId = %d\n", pMsg->u32ClientId));

                HGCMClient *pClient = HGCMClient::ReferenceByHandle(pMsg->u32ClientId);

                if (pClient)
                {
                    pSvc->m_fInConnectOrDisconnect = true;
                    vrc = pSvc->m_fntable.pfnConnect(pSvc->m_fntable.pvService, pMsg->u32ClientId,
                                                     HGCM_CLIENT_DATA(pSvc, pClient),
                                                     pMsg->fRequestor, pMsg->fRestoring);
                    pSvc->m_fInConnectOrDisconnect = false;

                    hgcmObjDereference(pClient);
                }
                else
                {
                    vrc = VERR_HGCM_INVALID_CLIENT_ID;
                }
            } break;

            case SVC_MSG_DISCONNECT:
            {
                HGCMMsgSvcDisconnect *pMsg = (HGCMMsgSvcDisconnect *)pMsgCore;

                LogFlowFunc(("SVC_MSG_DISCONNECT u32ClientId = %d, pClient = %p\n", pMsg->u32ClientId, pMsg->pClient));

                if (pMsg->pClient)
                {
                    pSvc->m_fInConnectOrDisconnect = true;
                    vrc = pSvc->m_fntable.pfnDisconnect(pSvc->m_fntable.pvService, pMsg->u32ClientId,
                                                        HGCM_CLIENT_DATA(pSvc, pMsg->pClient));
                    pSvc->m_fInConnectOrDisconnect = false;
                }
                else
                {
                    vrc = VERR_HGCM_INVALID_CLIENT_ID;
                }
            } break;

            case SVC_MSG_GUESTCALL:
            {
                HGCMMsgCall *pMsg = (HGCMMsgCall *)pMsgCore;

                LogFlowFunc(("SVC_MSG_GUESTCALL u32ClientId = %d, u32Function = %d, cParms = %d, paParms = %p\n",
                             pMsg->u32ClientId, pMsg->u32Function, pMsg->cParms, pMsg->paParms));

                HGCMClient *pClient = HGCMClient::ReferenceByHandleForGuest(pMsg->u32ClientId);

                if (pClient)
                {
                    pSvc->m_fntable.pfnCall(pSvc->m_fntable.pvService, (VBOXHGCMCALLHANDLE)pMsg, pMsg->u32ClientId,
                                            HGCM_CLIENT_DATA(pSvc, pClient), pMsg->u32Function,
                                            pMsg->cParms, pMsg->paParms, pMsg->tsArrival);

                    hgcmObjDereference(pClient);
                }
                else
                {
                    vrc = VERR_HGCM_INVALID_CLIENT_ID;
                }
            } break;

            case SVC_MSG_GUESTCANCELLED:
            {
                HGCMMsgCancelled *pMsg = (HGCMMsgCancelled *)pMsgCore;

                LogFlowFunc(("SVC_MSG_GUESTCANCELLED idClient = %d\n", pMsg->idClient));

                HGCMClient *pClient = HGCMClient::ReferenceByHandleForGuest(pMsg->idClient);

                if (pClient)
                {
                    pSvc->m_fntable.pfnCancelled(pSvc->m_fntable.pvService, pMsg->idClient, HGCM_CLIENT_DATA(pSvc, pClient));

                    hgcmObjDereference(pClient);
                }
                else
                {
                    vrc = VERR_HGCM_INVALID_CLIENT_ID;
                }
            } break;

            case SVC_MSG_HOSTCALL:
            {
                HGCMMsgHostCallSvc *pMsg = (HGCMMsgHostCallSvc *)pMsgCore;

                LogFlowFunc(("SVC_MSG_HOSTCALL u32Function = %d, cParms = %d, paParms = %p\n",
                             pMsg->u32Function, pMsg->cParms, pMsg->paParms));

                vrc = pSvc->m_fntable.pfnHostCall(pSvc->m_fntable.pvService, pMsg->u32Function, pMsg->cParms, pMsg->paParms);
            } break;

            case SVC_MSG_LOADSTATE:
            {
                HGCMMsgLoadSaveStateClient *pMsg = (HGCMMsgLoadSaveStateClient *)pMsgCore;

                LogFlowFunc(("SVC_MSG_LOADSTATE\n"));

                HGCMClient *pClient = HGCMClient::ReferenceByHandle(pMsg->u32ClientId);

                if (pClient)
                {
                    /* fRequestor: Restored by the message sender already. */
                    bool fHaveClientState = pSvc->m_fntable.pfnLoadState != NULL;
                    if (pMsg->uVersion > HGCM_SAVED_STATE_VERSION_V2)
                        vrc = pMsg->pVMM->pfnSSMR3GetBool(pMsg->pSSM, &fHaveClientState);
                    else
                        vrc = VINF_SUCCESS;
                    if (RT_SUCCESS(vrc) )
                    {
                        if (pSvc->m_fntable.pfnLoadState)
                            vrc = pSvc->m_fntable.pfnLoadState(pSvc->m_fntable.pvService, pMsg->u32ClientId,
                                                               HGCM_CLIENT_DATA(pSvc, pClient), pMsg->pSSM, pMsg->pVMM,
                                                               fHaveClientState ? pMsg->uVersion : 0);
                        else
                            AssertLogRelStmt(!fHaveClientState, vrc = VERR_INTERNAL_ERROR_5);
                    }
                    hgcmObjDereference(pClient);
                }
                else
                {
                    vrc = VERR_HGCM_INVALID_CLIENT_ID;
                }
            } break;

            case SVC_MSG_SAVESTATE:
            {
                HGCMMsgLoadSaveStateClient *pMsg = (HGCMMsgLoadSaveStateClient *)pMsgCore;

                LogFlowFunc(("SVC_MSG_SAVESTATE\n"));

                HGCMClient *pClient = HGCMClient::ReferenceByHandle(pMsg->u32ClientId);

                vrc = VINF_SUCCESS;

                if (pClient)
                {
                    pMsg->pVMM->pfnSSMR3PutU32(pMsg->pSSM, pClient->fRequestor); /* Quicker to save this here than in the message sender. */
                    vrc = pMsg->pVMM->pfnSSMR3PutBool(pMsg->pSSM, pSvc->m_fntable.pfnSaveState != NULL);
                    if (RT_SUCCESS(vrc) && pSvc->m_fntable.pfnSaveState)
                    {
                        g_fSaveState = true;
                        vrc = pSvc->m_fntable.pfnSaveState(pSvc->m_fntable.pvService, pMsg->u32ClientId,
                                                           HGCM_CLIENT_DATA(pSvc, pClient), pMsg->pSSM, pMsg->pVMM);
                        g_fSaveState = false;
                    }

                    hgcmObjDereference(pClient);
                }
                else
                {
                    vrc = VERR_HGCM_INVALID_CLIENT_ID;
                }
            } break;

            case SVC_MSG_REGEXT:
            {
                HGCMMsgSvcRegisterExtension *pMsg = (HGCMMsgSvcRegisterExtension *)pMsgCore;

                LogFlowFunc(("SVC_MSG_REGEXT handle = %p, pfn = %p\n", pMsg->handle, pMsg->pfnExtension));

                if (pSvc->m_hExtension)
                {
                    vrc = VERR_NOT_SUPPORTED;
                }
                else
                {
                    if (pSvc->m_fntable.pfnRegisterExtension)
                    {
                        vrc = pSvc->m_fntable.pfnRegisterExtension(pSvc->m_fntable.pvService, pMsg->pfnExtension,
                                                                   pMsg->pvExtension);
                    }
                    else
                    {
                        vrc = VERR_NOT_SUPPORTED;
                    }

                    if (RT_SUCCESS(vrc))
                    {
                        pSvc->m_hExtension = pMsg->handle;
                    }
                }
            } break;

            case SVC_MSG_UNREGEXT:
            {
                HGCMMsgSvcUnregisterExtension *pMsg = (HGCMMsgSvcUnregisterExtension *)pMsgCore;

                LogFlowFunc(("SVC_MSG_UNREGEXT handle = %p\n", pMsg->handle));

                if (pSvc->m_hExtension != pMsg->handle)
                {
                    vrc = VERR_NOT_SUPPORTED;
                }
                else
                {
                    if (pSvc->m_fntable.pfnRegisterExtension)
                    {
                        vrc = pSvc->m_fntable.pfnRegisterExtension(pSvc->m_fntable.pvService, NULL, NULL);
                    }
                    else
                    {
                        vrc = VERR_NOT_SUPPORTED;
                    }

                    pSvc->m_hExtension = NULL;
                }
            } break;

            case SVC_MSG_NOTIFY:
            {
                HGCMMsgNotify *pMsg = (HGCMMsgNotify *)pMsgCore;

                LogFlowFunc(("SVC_MSG_NOTIFY enmEvent = %d\n", pMsg->enmEvent));

                pSvc->m_fntable.pfnNotify(pSvc->m_fntable.pvService, pMsg->enmEvent);
            } break;

            default:
            {
                AssertMsgFailed(("hgcmServiceThread::Unsupported message number %08X\n", u32MsgId));
                vrc = VERR_NOT_SUPPORTED;
            } break;
        }

        if (u32MsgId != SVC_MSG_GUESTCALL)
        {
            /* For SVC_MSG_GUESTCALL the service calls the completion helper.
             * Other messages have to be completed here.
             */
            hgcmMsgComplete (pMsgCore, vrc);
        }
        STAM_REL_PROFILE_STOP(&pSvc->m_StatHandleMsg, a);
    }
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnCallComplete}
 */
/* static */ DECLCALLBACK(int) HGCMService::svcHlpCallComplete(VBOXHGCMCALLHANDLE callHandle, int32_t vrc)
{
   HGCMMsgCore *pMsgCore = (HGCMMsgCore *)callHandle;

   /* Only call the completion for these messages. The helper
    * is called by the service, and the service does not get
    * any other messages.
    */
   AssertMsgReturn(pMsgCore->MsgId() == SVC_MSG_GUESTCALL, ("%d\n", pMsgCore->MsgId()), VERR_WRONG_TYPE);
   return hgcmMsgComplete(pMsgCore, vrc);
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnDisconnectClient}
 */
/* static */ DECLCALLBACK(int) HGCMService::svcHlpDisconnectClient(void *pvInstance, uint32_t idClient)
{
     HGCMService *pService = static_cast <HGCMService *> (pvInstance);
     AssertReturn(pService, VERR_INVALID_HANDLE);

     /* Only safe to call when the main HGCM thread is waiting on the service
        to handle a SVC_MSG_CONNECT or SVC_MSG_DISCONNECT message.  Otherwise
        we'd risk racing it and corrupt data structures. */
     AssertReturn(pService->m_fInConnectOrDisconnect, VERR_INVALID_CONTEXT);

     /* Resolve the client ID and verify that it belongs to this service before
        trying to disconnect it. */
     int vrc = VERR_NOT_FOUND;
     HGCMClient * const pClient = HGCMClient::ReferenceByHandle(idClient);
     if (pClient)
     {
         if (pClient->pService == pService)
             vrc = pService->DisconnectClient(idClient, true, pClient);
         hgcmObjDereference(pClient);
     }
     return vrc;
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnIsCallRestored}
 */
/* static */ DECLCALLBACK(bool) HGCMService::svcHlpIsCallRestored(VBOXHGCMCALLHANDLE callHandle)
{
    HGCMMsgHeader *pMsgHdr = (HGCMMsgHeader *)callHandle;
    AssertPtrReturn(pMsgHdr, false);

    PVBOXHGCMCMD pCmd = pMsgHdr->pCmd;
    AssertPtrReturn(pCmd, false);

    PPDMIHGCMPORT pHgcmPort = pMsgHdr->pHGCMPort;
    AssertPtrReturn(pHgcmPort, false);

    return pHgcmPort->pfnIsCmdRestored(pHgcmPort, pCmd);
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnIsCallCancelled}
 */
/* static */ DECLCALLBACK(bool) HGCMService::svcHlpIsCallCancelled(VBOXHGCMCALLHANDLE callHandle)
{
    HGCMMsgHeader *pMsgHdr = (HGCMMsgHeader *)callHandle;
    AssertPtrReturn(pMsgHdr, false);

    PVBOXHGCMCMD pCmd = pMsgHdr->pCmd;
    AssertPtrReturn(pCmd, false);

    PPDMIHGCMPORT pHgcmPort = pMsgHdr->pHGCMPort;
    AssertPtrReturn(pHgcmPort, false);

    return pHgcmPort->pfnIsCmdCancelled(pHgcmPort, pCmd);
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnStamRegisterV}
 */
/* static */ DECLCALLBACK(int)
HGCMService::svcHlpStamRegisterV(void *pvInstance, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility,
                                 STAMUNIT enmUnit, const char *pszDesc, const char *pszName, va_list va)
{
     HGCMService *pService = static_cast <HGCMService *>(pvInstance);
     AssertPtrReturn(pService, VERR_INVALID_PARAMETER);

     if (pService->m_pUVM)
         return pService->m_pVMM->pfnSTAMR3RegisterVU(pService->m_pUVM, pvSample, enmType, enmVisibility,
                                                      enmUnit, pszDesc, pszName, va);
     return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnStamDeregisterV}
 */
/* static */ DECLCALLBACK(int) HGCMService::svcHlpStamDeregisterV(void *pvInstance, const char *pszPatFmt, va_list va)
{
     HGCMService *pService = static_cast <HGCMService *>(pvInstance);
     AssertPtrReturn(pService, VERR_INVALID_PARAMETER);

     if (pService->m_pUVM)
         return pService->m_pVMM->pfnSTAMR3DeregisterV(pService->m_pUVM, pszPatFmt, va);
     return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnInfoRegister}
 */
/* static */ DECLCALLBACK(int) HGCMService::svcHlpInfoRegister(void *pvInstance, const char *pszName, const char *pszDesc,
                                                               PFNDBGFHANDLEREXT pfnHandler, void *pvUser)
{
     HGCMService *pService = static_cast <HGCMService *>(pvInstance);
     AssertPtrReturn(pService, VERR_INVALID_PARAMETER);

     if (pService->m_pUVM)
         return pService->m_pVMM->pfnDBGFR3InfoRegisterExternal(pService->m_pUVM, pszName, pszDesc, pfnHandler, pvUser);
     return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnInfoDeregister}
 */
/* static */ DECLCALLBACK(int) HGCMService::svcHlpInfoDeregister(void *pvInstance, const char *pszName)
{
     HGCMService *pService = static_cast <HGCMService *>(pvInstance);
     AssertPtrReturn(pService, VERR_INVALID_PARAMETER);
     if (pService->m_pUVM)
         return pService->m_pVMM->pfnDBGFR3InfoDeregisterExternal(pService->m_pUVM, pszName);
     return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnGetRequestor}
 */
/* static */ DECLCALLBACK(uint32_t) HGCMService::svcHlpGetRequestor(VBOXHGCMCALLHANDLE hCall)
{
    HGCMMsgHeader *pMsgHdr = (HGCMMsgHeader *)(hCall);
    AssertPtrReturn(pMsgHdr, VMMDEV_REQUESTOR_LOWEST);

    PVBOXHGCMCMD pCmd = pMsgHdr->pCmd;
    AssertPtrReturn(pCmd, VMMDEV_REQUESTOR_LOWEST);

    PPDMIHGCMPORT pHgcmPort = pMsgHdr->pHGCMPort;
    AssertPtrReturn(pHgcmPort, VMMDEV_REQUESTOR_LOWEST);

    return pHgcmPort->pfnGetRequestor(pHgcmPort, pCmd);
}

/**
 * @interface_method_impl{VBOXHGCMSVCHELPERS,pfnGetVMMDevSessionId}
 */
/* static */ DECLCALLBACK(uint64_t) HGCMService::svcHlpGetVMMDevSessionId(void *pvInstance)
{
     HGCMService *pService = static_cast <HGCMService *>(pvInstance);
     AssertPtrReturn(pService, UINT64_MAX);

     PPDMIHGCMPORT pHgcmPort = pService->m_pHgcmPort;
     AssertPtrReturn(pHgcmPort, UINT64_MAX);

     return pHgcmPort->pfnGetVMMDevSessionId(pHgcmPort);
}


static DECLCALLBACK(int) hgcmMsgCompletionCallback(int32_t result, HGCMMsgCore *pMsgCore)
{
    /* Call the VMMDev port interface to issue IRQ notification. */
    HGCMMsgHeader *pMsgHdr = (HGCMMsgHeader *)pMsgCore;

    LogFlow(("MAIN::hgcmMsgCompletionCallback: message %p\n", pMsgCore));

    if (pMsgHdr->pHGCMPort)
    {
        if (!g_fResetting)
            return pMsgHdr->pHGCMPort->pfnCompleted(pMsgHdr->pHGCMPort,
                                                    g_fSaveState ? VINF_HGCM_SAVE_STATE : result, pMsgHdr->pCmd);
        return VERR_ALREADY_RESET; /* best I could find. */
    }
    return VERR_NOT_AVAILABLE;
}

/*
 * The main HGCM methods of the service.
 */

int HGCMService::instanceCreate(const char *pszServiceLibrary, const char *pszServiceName,
                                PUVM pUVM, PCVMMR3VTABLE pVMM, PPDMIHGCMPORT pHgcmPort)
{
    LogFlowFunc(("name %s, lib %s\n", pszServiceName, pszServiceLibrary));

    /* The maximum length of the thread name, allowed by the RT is 15. */
    char szThreadName[16];
    if (!strncmp(pszServiceName, RT_STR_TUPLE("VBoxShared")))
        RTStrPrintf(szThreadName, sizeof(szThreadName), "Sh%s", pszServiceName + 10);
    else if (!strncmp(pszServiceName, RT_STR_TUPLE("VBox")))
        RTStrCopy(szThreadName, sizeof(szThreadName), pszServiceName + 4);
    else
        RTStrCopy(szThreadName, sizeof(szThreadName), pszServiceName);

    int vrc = hgcmThreadCreate(&m_pThread, szThreadName, hgcmServiceThread, this, pszServiceName, pUVM, pVMM);
    if (RT_SUCCESS(vrc))
    {
        m_pszSvcName    = RTStrDup(pszServiceName);
        m_pszSvcLibrary = RTStrDup(pszServiceLibrary);

        if (!m_pszSvcName || !m_pszSvcLibrary)
        {
            RTStrFree(m_pszSvcLibrary);
            m_pszSvcLibrary = NULL;

            RTStrFree(m_pszSvcName);
            m_pszSvcName = NULL;

            vrc = VERR_NO_MEMORY;
        }
        else
        {
            m_pUVM = pUVM;
            m_pVMM = pVMM;
            m_pHgcmPort = pHgcmPort;

            registerStatistics(pszServiceName, pUVM, pVMM);

            /* Initialize service helpers table. */
            m_svcHelpers.pfnCallComplete       = svcHlpCallComplete;
            m_svcHelpers.pvInstance            = this;
            m_svcHelpers.pfnDisconnectClient   = svcHlpDisconnectClient;
            m_svcHelpers.pfnIsCallRestored     = svcHlpIsCallRestored;
            m_svcHelpers.pfnIsCallCancelled    = svcHlpIsCallCancelled;
            m_svcHelpers.pfnStamRegisterV      = svcHlpStamRegisterV;
            m_svcHelpers.pfnStamDeregisterV    = svcHlpStamDeregisterV;
            m_svcHelpers.pfnInfoRegister       = svcHlpInfoRegister;
            m_svcHelpers.pfnInfoDeregister     = svcHlpInfoDeregister;
            m_svcHelpers.pfnGetRequestor       = svcHlpGetRequestor;
            m_svcHelpers.pfnGetVMMDevSessionId = svcHlpGetVMMDevSessionId;

            /* Execute the load request on the service thread. */
            HGCMMsgCore *pCoreMsg;
            vrc = hgcmMsgAlloc(m_pThread, &pCoreMsg, SVC_MSG_LOAD, hgcmMessageAllocSvc);

            if (RT_SUCCESS(vrc))
            {
                HGCMMsgSvcLoad *pMsg = (HGCMMsgSvcLoad *)pCoreMsg;

                pMsg->pUVM = pUVM;

                vrc = hgcmMsgSend(pMsg);
            }
        }
    }

    if (RT_FAILURE(vrc))
    {
        instanceDestroy();
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/** Called by HGCMService::instanceCreate to register statistics. */
void HGCMService::registerStatistics(const char *pszServiceName, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_StatHandleMsg, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_OCCURENCE,
                              "Message handling", "/HGCM/%s/Msg", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_StatTooManyCalls, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                              "Too many calls (per client)", "/HGCM/%s/TooManyCalls", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_StatTooManyClients, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                              "Too many clients", "/HGCM/%s/TooManyClients", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_cClients, STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                              "Number of clients", "/HGCM/%s/Clients", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_acClients[HGCM_CLIENT_CATEGORY_KERNEL], STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Number of kernel clients", "/HGCM/%s/Clients/Kernel", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_acClients[HGCM_CLIENT_CATEGORY_ROOT], STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Number of root/admin clients", "/HGCM/%s/Clients/Root", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_acClients[HGCM_CLIENT_CATEGORY_USER], STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Number of regular user clients", "/HGCM/%s/Clients/User", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_KERNEL], STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Max number of kernel clients", "/HGCM/%s/Clients/KernelMax", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_ROOT], STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Max number of root clients", "/HGCM/%s/Clients/RootMax", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_fntable.acMaxClients[HGCM_CLIENT_CATEGORY_USER], STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Max number of user clients", "/HGCM/%s/Clients/UserMax", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_fntable.idxLegacyClientCategory, STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Legacy client mapping", "/HGCM/%s/Clients/LegacyClientMapping", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_KERNEL], STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Max number of call per kernel client", "/HGCM/%s/MaxCallsKernelClient", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_ROOT], STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Max number of call per root client", "/HGCM/%s/MaxCallsRootClient", pszServiceName);
    pVMM->pfnSTAMR3RegisterFU(pUVM, &m_fntable.acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_USER], STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                              STAMUNIT_OCCURENCES, "Max number of call per user client", "/HGCM/%s/MaxCallsUserClient", pszServiceName);
}

void HGCMService::instanceDestroy(void)
{
    LogFlowFunc(("%s\n", m_pszSvcName));

    HGCMMsgCore *pMsg;
    int vrc = hgcmMsgAlloc(m_pThread, &pMsg, SVC_MSG_UNLOAD, hgcmMessageAllocSvc);

    if (RT_SUCCESS(vrc))
    {
        vrc = hgcmMsgSend(pMsg);

        if (RT_SUCCESS(vrc))
            hgcmThreadWait(m_pThread);
    }

    if (m_pszSvcName && m_pUVM)
        m_pVMM->pfnSTAMR3DeregisterF(m_pUVM, "/HGCM/%s/*", m_pszSvcName);
    m_pUVM = NULL;
    m_pHgcmPort = NULL;

    RTStrFree(m_pszSvcLibrary);
    m_pszSvcLibrary = NULL;

    RTStrFree(m_pszSvcName);
    m_pszSvcName = NULL;

    if (m_paClientIds)
    {
        RTMemFree(m_paClientIds);
        m_paClientIds = NULL;
    }
}

int HGCMService::saveClientState(uint32_t u32ClientId, PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM)
{
    LogFlowFunc(("%s\n", m_pszSvcName));

    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(m_pThread, &pCoreMsg, SVC_MSG_SAVESTATE, hgcmMessageAllocSvc);

    if (RT_SUCCESS(vrc))
    {
        HGCMMsgLoadSaveStateClient *pMsg = (HGCMMsgLoadSaveStateClient *)pCoreMsg;

        pMsg->u32ClientId = u32ClientId;
        pMsg->pSSM        = pSSM;
        pMsg->pVMM        = pVMM;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

int HGCMService::loadClientState(uint32_t u32ClientId, PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t uVersion)
{
    LogFlowFunc(("%s\n", m_pszSvcName));

    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(m_pThread, &pCoreMsg, SVC_MSG_LOADSTATE, hgcmMessageAllocSvc);

    if (RT_SUCCESS(vrc))
    {
        HGCMMsgLoadSaveStateClient *pMsg = (HGCMMsgLoadSaveStateClient *)pCoreMsg;

        pMsg->pSSM        = pSSM;
        pMsg->pVMM        = pVMM;
        pMsg->uVersion    = uVersion;
        pMsg->u32ClientId = u32ClientId;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}


/** The method creates a service and references it.
 *
 * @param   pszServiceLibrary  The library to be loaded.
 * @param   pszServiceName     The name of the service.
 * @param   pUVM               The user mode VM handle (for statistics and such).
 * @param   pVMM               The VMM vtable (for statistics and such).
 * @param   pHgcmPort          The VMMDev HGCM port interface.
 *
 * @return  VBox status code.
 * @thread  main HGCM
 */
/* static */ int HGCMService::LoadService(const char *pszServiceLibrary, const char *pszServiceName,
                                          PUVM pUVM, PCVMMR3VTABLE pVMM, PPDMIHGCMPORT pHgcmPort)
{
    LogFlowFunc(("lib %s, name = %s, pUVM = %p\n", pszServiceLibrary, pszServiceName, pUVM));

    /* Look at already loaded services to avoid double loading. */

    HGCMService *pSvc;
    int vrc = HGCMService::ResolveService(&pSvc, pszServiceName);

    if (RT_SUCCESS(vrc))
    {
        /* The service is already loaded. */
        pSvc->ReleaseService();
        vrc = VERR_HGCM_SERVICE_EXISTS;
    }
    else
    {
        /* Create the new service. */
        pSvc = new (std::nothrow) HGCMService();

        if (!pSvc)
        {
            vrc = VERR_NO_MEMORY;
        }
        else
        {
            /* Load the library and call the initialization entry point. */
            vrc = pSvc->instanceCreate(pszServiceLibrary, pszServiceName, pUVM, pVMM, pHgcmPort);
            if (RT_SUCCESS(vrc))
            {
                /* Insert the just created service to list for future references. */
                pSvc->m_pSvcNext = sm_pSvcListHead;
                pSvc->m_pSvcPrev = NULL;

                if (sm_pSvcListHead)
                    sm_pSvcListHead->m_pSvcPrev = pSvc;
                else
                    sm_pSvcListTail = pSvc;

                sm_pSvcListHead = pSvc;

                sm_cServices++;

                /* Reference the service (for first time) until it is unloaded on HGCM termination. */
                AssertRelease(pSvc->m_u32RefCnt == 0);
                pSvc->ReferenceService();

                LogFlowFunc(("service %p\n", pSvc));
            }
        }
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/** The method unloads a service.
 *
 * @thread main HGCM
 */
void HGCMService::UnloadService(bool fUvmIsInvalid)
{
    LogFlowFunc(("name = %s\n", m_pszSvcName));

    if (fUvmIsInvalid)
    {
        m_pUVM = NULL;
        m_pHgcmPort = NULL;
    }

    /* Remove the service from the list. */
    if (m_pSvcNext)
    {
        m_pSvcNext->m_pSvcPrev = m_pSvcPrev;
    }
    else
    {
        sm_pSvcListTail = m_pSvcPrev;
    }

    if (m_pSvcPrev)
    {
        m_pSvcPrev->m_pSvcNext = m_pSvcNext;
    }
    else
    {
        sm_pSvcListHead = m_pSvcNext;
    }

    sm_cServices--;

    /* The service must be unloaded only if all clients were disconnected. */
    LogFlowFunc(("m_u32RefCnt = %d\n", m_u32RefCnt));
    AssertRelease(m_u32RefCnt == 1);

    /* Now the service can be released. */
    ReleaseService();
}

/** The method unloads all services.
 *
 * @thread main HGCM
 */
/* static */ void HGCMService::UnloadAll(bool fUvmIsInvalid)
{
    while (sm_pSvcListHead)
    {
        sm_pSvcListHead->UnloadService(fUvmIsInvalid);
    }
}

/** The method obtains a referenced pointer to the service with
 *  specified name. The caller must call ReleaseService when
 *  the pointer is no longer needed.
 *
 * @param ppSvc          Where to store the pointer to the service.
 * @param pszServiceName The name of the service.
 * @return VBox status code.
 * @thread main HGCM
 */
/* static */ int HGCMService::ResolveService(HGCMService **ppSvc, const char *pszServiceName)
{
    LogFlowFunc(("ppSvc = %p name = %s\n",
                 ppSvc, pszServiceName));

    if (!ppSvc || !pszServiceName)
    {
        return VERR_INVALID_PARAMETER;
    }

    HGCMService *pSvc = sm_pSvcListHead;

    while (pSvc)
    {
        if (strcmp(pSvc->m_pszSvcName, pszServiceName) == 0)
        {
            break;
        }

        pSvc = pSvc->m_pSvcNext;
    }

    LogFlowFunc(("lookup in the list is %p\n", pSvc));

    if (pSvc == NULL)
    {
        *ppSvc = NULL;
        return VERR_HGCM_SERVICE_NOT_FOUND;
    }

    pSvc->ReferenceService();

    *ppSvc = pSvc;

    return VINF_SUCCESS;
}

/** The method increases reference counter.
 *
 * @thread main HGCM
 */
void HGCMService::ReferenceService(void)
{
    ASMAtomicIncU32(&m_u32RefCnt);
    LogFlowFunc(("[%s] m_u32RefCnt = %d\n", m_pszSvcName, m_u32RefCnt));
}

/** The method dereferences a service and deletes it when no more refs.
 *
 * @thread main HGCM
 */
void HGCMService::ReleaseService(void)
{
    LogFlowFunc(("m_u32RefCnt = %d\n", m_u32RefCnt));
    uint32_t u32RefCnt = ASMAtomicDecU32(&m_u32RefCnt);
    AssertRelease(u32RefCnt != ~0U);

    LogFlowFunc(("u32RefCnt = %d, name %s\n", u32RefCnt, m_pszSvcName));

    if (u32RefCnt == 0)
    {
        instanceDestroy();
        delete this;
    }
}

/** The method is called when the VM is being reset or terminated
 *  and disconnects all clients from all services.
 *
 * @thread main HGCM
 */
/* static */ void HGCMService::Reset(void)
{
    g_fResetting = true;

    HGCMService *pSvc = sm_pSvcListHead;

    while (pSvc)
    {
        while (pSvc->m_cClients && pSvc->m_paClientIds)
        {
            uint32_t const     idClient = pSvc->m_paClientIds[0];
            HGCMClient * const pClient  = HGCMClient::ReferenceByHandle(idClient);
            Assert(pClient);
            LogFlowFunc(("handle %d/%p\n", pSvc->m_paClientIds[0], pClient));

            pSvc->DisconnectClient(pSvc->m_paClientIds[0], false, pClient);

            hgcmObjDereference(pClient);
        }

        pSvc = pSvc->m_pSvcNext;
    }

    g_fResetting = false;
}

/** The method saves the HGCM state.
 *
 * @param pSSM  The saved state context.
 * @param pVMM  The VMM vtable.
 * @return VBox status code.
 * @thread main HGCM
 */
/* static */ int HGCMService::SaveState(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM)
{
    /* Save the current handle count and restore afterwards to avoid client id conflicts. */
    int vrc = pVMM->pfnSSMR3PutU32(pSSM, hgcmObjQueryHandleCount());
    AssertRCReturn(vrc, vrc);

    LogFlowFunc(("%d services to be saved:\n", sm_cServices));

    /* Save number of services. */
    vrc = pVMM->pfnSSMR3PutU32(pSSM, sm_cServices);
    AssertRCReturn(vrc, vrc);

    /* Save every service. */
    HGCMService *pSvc = sm_pSvcListHead;

    while (pSvc)
    {
        LogFlowFunc(("Saving service [%s]\n", pSvc->m_pszSvcName));

        /* Save the length of the service name. */
        vrc = pVMM->pfnSSMR3PutU32(pSSM, (uint32_t) strlen(pSvc->m_pszSvcName) + 1);
        AssertRCReturn(vrc, vrc);

        /* Save the name of the service. */
        vrc = pVMM->pfnSSMR3PutStrZ(pSSM, pSvc->m_pszSvcName);
        AssertRCReturn(vrc, vrc);

        /* Save the number of clients. */
        vrc = pVMM->pfnSSMR3PutU32(pSSM, pSvc->m_cClients);
        AssertRCReturn(vrc, vrc);

        /* Call the service for every client. Normally a service must not have
         * a global state to be saved: only per client info is relevant.
         * The global state of a service is configured during VM startup.
         */
        uint32_t i;

        for (i = 0; i < pSvc->m_cClients; i++)
        {
            uint32_t u32ClientId = pSvc->m_paClientIds[i];

            Log(("client id 0x%08X\n", u32ClientId));

            /* Save the client id. (fRequestor is saved via SVC_MSG_SAVESTATE for convenience.) */
            vrc = pVMM->pfnSSMR3PutU32(pSSM, u32ClientId);
            AssertRCReturn(vrc, vrc);

            /* Call the service, so the operation is executed by the service thread. */
            vrc = pSvc->saveClientState(u32ClientId, pSSM, pVMM);
            AssertRCReturn(vrc, vrc);
        }

        pSvc = pSvc->m_pSvcNext;
    }

    return VINF_SUCCESS;
}

/** The method loads saved HGCM state.
 *
 * @param pSSM      The saved state handle.
 * @param pVMM      The VMM vtable.
 * @param uVersion  The state version being loaded.
 * @return VBox status code.
 * @thread main HGCM
 */
/* static */ int HGCMService::LoadState(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t uVersion)
{
    /* Restore handle count to avoid client id conflicts. */
    uint32_t u32;

    int vrc = pVMM->pfnSSMR3GetU32(pSSM, &u32);
    AssertRCReturn(vrc, vrc);

    hgcmObjSetHandleCount(u32);

    /* Get the number of services. */
    uint32_t cServices;

    vrc = pVMM->pfnSSMR3GetU32(pSSM, &cServices);
    AssertRCReturn(vrc, vrc);

    LogFlowFunc(("%d services to be restored:\n", cServices));

    while (cServices--)
    {
        /* Get the length of the service name. */
        vrc = pVMM->pfnSSMR3GetU32(pSSM, &u32);
        AssertRCReturn(vrc, vrc);
        AssertReturn(u32 <= VBOX_HGCM_SVC_NAME_MAX_BYTES, VERR_SSM_UNEXPECTED_DATA);

        /* Get the service name. */
        char szServiceName[VBOX_HGCM_SVC_NAME_MAX_BYTES];
        vrc = pVMM->pfnSSMR3GetStrZ(pSSM, szServiceName, u32);
        AssertRCReturn(vrc, vrc);

        LogRel(("HGCM: Restoring [%s]\n", szServiceName));

        /* Resolve the service instance. */
        HGCMService *pSvc;
        vrc = ResolveService(&pSvc, szServiceName);
        AssertLogRelMsgReturn(pSvc, ("vrc=%Rrc, %s\n", vrc, szServiceName), VERR_SSM_UNEXPECTED_DATA);

        /* Get the number of clients. */
        uint32_t cClients;
        vrc = pVMM->pfnSSMR3GetU32(pSSM, &cClients);
        if (RT_FAILURE(vrc))
        {
            pSvc->ReleaseService();
            AssertFailed();
            return vrc;
        }

        while (cClients--)
        {
            /* Get the client ID and fRequest (convieniently save via SVC_MSG_SAVESTATE
               but restored here in time for calling CreateAndConnectClient). */
            uint32_t u32ClientId;
            vrc = pVMM->pfnSSMR3GetU32(pSSM, &u32ClientId);
            uint32_t fRequestor = VMMDEV_REQUESTOR_LEGACY;
            if (RT_SUCCESS(vrc) && uVersion > HGCM_SAVED_STATE_VERSION_V2)
                vrc = pVMM->pfnSSMR3GetU32(pSSM, &fRequestor);
            AssertLogRelMsgRCReturnStmt(vrc, ("vrc=%Rrc, %s\n", vrc, szServiceName), pSvc->ReleaseService(), vrc);

            /* Connect the client. */
            vrc = pSvc->CreateAndConnectClient(NULL, u32ClientId, fRequestor, true /*fRestoring*/);
            AssertLogRelMsgRCReturnStmt(vrc, ("vrc=%Rrc, %s\n", vrc, szServiceName), pSvc->ReleaseService(), vrc);

            /* Call the service, so the operation is executed by the service thread. */
            vrc = pSvc->loadClientState(u32ClientId, pSSM, pVMM, uVersion);
            AssertLogRelMsgRCReturnStmt(vrc, ("vrc=%Rrc, %s\n", vrc, szServiceName), pSvc->ReleaseService(), vrc);
        }

        pSvc->ReleaseService();
    }

    return VINF_SUCCESS;
}

/* Create a new client instance and connect it to the service.
 *
 * @param pu32ClientIdOut If not NULL, then the method must generate a new handle for the client.
 *                        If NULL, use the given 'u32ClientIdIn' handle.
 * @param u32ClientIdIn   The handle for the client, when 'pu32ClientIdOut' is NULL.
 * @param fRequestor      The requestor flags, VMMDEV_REQUESTOR_LEGACY if not available.
 * @param fRestoring      Set if we're restoring a saved state.
 * @return VBox status code.
 */
int HGCMService::CreateAndConnectClient(uint32_t *pu32ClientIdOut, uint32_t u32ClientIdIn, uint32_t fRequestor, bool fRestoring)
{
    LogFlowFunc(("pu32ClientIdOut = %p, u32ClientIdIn = %d, fRequestor = %#x, fRestoring = %d\n",
                 pu32ClientIdOut, u32ClientIdIn, fRequestor, fRestoring));

    /*
     * Categorize the client (compress VMMDEV_REQUESTOR_USR_MASK)
     * and check the respective client limit.
     */
    uint32_t idxClientCategory;
    if (fRequestor == VMMDEV_REQUESTOR_LEGACY)
    {
        idxClientCategory = m_fntable.idxLegacyClientCategory;
        AssertStmt(idxClientCategory < RT_ELEMENTS(m_acClients), idxClientCategory = HGCM_CLIENT_CATEGORY_KERNEL);
    }
    else
        switch (fRequestor & VMMDEV_REQUESTOR_USR_MASK)
        {
            case VMMDEV_REQUESTOR_USR_DRV:
            case VMMDEV_REQUESTOR_USR_DRV_OTHER:
                idxClientCategory = HGCM_CLIENT_CATEGORY_KERNEL;
                break;
            case VMMDEV_REQUESTOR_USR_ROOT:
            case VMMDEV_REQUESTOR_USR_SYSTEM:
                idxClientCategory = HGCM_CLIENT_CATEGORY_ROOT;
                break;
            default:
                idxClientCategory = HGCM_CLIENT_CATEGORY_USER;
                break;
        }

    if (   m_acClients[idxClientCategory] < m_fntable.acMaxClients[idxClientCategory]
        || fRestoring)
    { }
    else
    {
        LogRel2(("Too many concurrenct clients for HGCM service '%s': %u, max %u; category %u\n",
                 m_pszSvcName, m_cClients, m_fntable.acMaxClients[idxClientCategory], idxClientCategory));
        STAM_REL_COUNTER_INC(&m_StatTooManyClients);
        return VERR_HGCM_TOO_MANY_CLIENTS;
    }

    /* Allocate a client information structure. */
    HGCMClient *pClient = new (std::nothrow) HGCMClient(fRequestor, idxClientCategory);

    if (!pClient)
    {
        Log1WarningFunc(("Could not allocate HGCMClient!!!\n"));
        return VERR_NO_MEMORY;
    }

    uint32_t handle;

    if (pu32ClientIdOut != NULL)
    {
        handle = hgcmObjGenerateHandle(pClient);
    }
    else
    {
        handle = hgcmObjAssignHandle(pClient, u32ClientIdIn);
    }

    LogFlowFunc(("client id = %d\n", handle));

    AssertRelease(handle);

    /* Initialize the HGCM part of the client. */
    int vrc = pClient->Init(this);

    if (RT_SUCCESS(vrc))
    {
        /* Call the service. */
        HGCMMsgCore *pCoreMsg;

        vrc = hgcmMsgAlloc(m_pThread, &pCoreMsg, SVC_MSG_CONNECT, hgcmMessageAllocSvc);

        if (RT_SUCCESS(vrc))
        {
            HGCMMsgSvcConnect *pMsg = (HGCMMsgSvcConnect *)pCoreMsg;

            pMsg->u32ClientId = handle;
            pMsg->fRequestor = fRequestor;
            pMsg->fRestoring = fRestoring;

            vrc = hgcmMsgSend(pMsg);

            if (RT_SUCCESS(vrc))
            {
                /* Add the client Id to the array. */
                if (m_cClients == m_cClientsAllocated)
                {
                    const uint32_t cDelta = 64;

                    /* Guards against integer overflow on 32bit arch and also limits size of m_paClientIds array to 4GB*/
                    if (m_cClientsAllocated < UINT32_MAX / sizeof(m_paClientIds[0]) - cDelta)
                    {
                        uint32_t *paClientIdsNew;

                        paClientIdsNew = (uint32_t *)RTMemRealloc(m_paClientIds,
                                                                  (m_cClientsAllocated + cDelta) * sizeof(m_paClientIds[0]));
                        Assert(paClientIdsNew);

                        if (paClientIdsNew)
                        {
                            m_paClientIds = paClientIdsNew;
                            m_cClientsAllocated += cDelta;
                        }
                        else
                        {
                            vrc = VERR_NO_MEMORY;
                        }
                    }
                    else
                    {
                        vrc = VERR_NO_MEMORY;
                    }
                }

                if (RT_SUCCESS(vrc))
                {
                    m_paClientIds[m_cClients] = handle;
                    m_cClients++;
                    m_acClients[idxClientCategory]++;
                    LogFunc(("idClient=%u m_cClients=%u m_acClients[%u]=%u %s\n",
                             handle, m_cClients, idxClientCategory, m_acClients[idxClientCategory], m_pszSvcName));
                }
            }
        }
    }

    if (RT_SUCCESS(vrc))
    {
        if (pu32ClientIdOut != NULL)
        {
            *pu32ClientIdOut = handle;
        }

        ReferenceService();

        /* The guest may now use this client object. */
        pClient->makeAccessibleToGuest();
    }
    else
    {
        hgcmObjDeleteHandle(handle);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/**
 * Disconnect the client from the service and delete the client handle.
 *
 * @param   u32ClientId     The handle of the client.
 * @param   fFromService    Set if called by the service via
 *                          svcHlpDisconnectClient().
 * @param   pClient         The client disconnecting.
 * @return  VBox status code.
 */
int HGCMService::DisconnectClient(uint32_t u32ClientId, bool fFromService, HGCMClient *pClient)
{
    AssertPtr(pClient);
    LogFlowFunc(("client id = %d, fFromService = %d, pClient = %p\n", u32ClientId, fFromService, pClient));

    /*
     * Destroy the client handle prior to the disconnecting to avoid creating
     * a race with other messages from the same client.  See @bugref{10038}
     * for further details.
     */
    Assert(pClient->idxCategory < HGCM_CLIENT_CATEGORY_MAX);
    Assert(m_acClients[pClient->idxCategory] > 0);

    bool fReleaseService = false;
    int  vrc             = VERR_NOT_FOUND;
    for (uint32_t i = 0; i < m_cClients; i++)
    {
        if (m_paClientIds[i] == u32ClientId)
        {
            if (m_acClients[pClient->idxCategory] > 0)
                m_acClients[pClient->idxCategory]--;

            m_cClients--;

            if (m_cClients > i)
                memmove(&m_paClientIds[i], &m_paClientIds[i + 1], sizeof(m_paClientIds[0]) * (m_cClients - i));

            /* Delete the client handle. */
            hgcmObjDeleteHandle(u32ClientId);
            fReleaseService = true;

            vrc = VINF_SUCCESS;
            break;
        }
    }

    /* Some paranoia wrt to not trusting the client ID array. */
    Assert(vrc == VINF_SUCCESS || fFromService);
    if (vrc == VERR_NOT_FOUND && !fFromService)
    {
        if (m_acClients[pClient->idxCategory] > 0)
            m_acClients[pClient->idxCategory]--;

        hgcmObjDeleteHandle(u32ClientId);
        fReleaseService = true;
    }

    LogFunc(("idClient=%u m_cClients=%u m_acClients[%u]=%u %s (cPendingCalls=%u) vrc=%Rrc\n", u32ClientId, m_cClients,
             pClient->idxCategory, m_acClients[pClient->idxCategory], m_pszSvcName, pClient->cPendingCalls, vrc));

    /*
     * Call the service.
     */
    if (!fFromService)
    {
        /* Call the service. */
        HGCMMsgCore *pCoreMsg;

        vrc = hgcmMsgAlloc(m_pThread, &pCoreMsg, SVC_MSG_DISCONNECT, hgcmMessageAllocSvc);

        if (RT_SUCCESS(vrc))
        {
            HGCMMsgSvcDisconnect *pMsg = (HGCMMsgSvcDisconnect *)pCoreMsg;

            pMsg->u32ClientId = u32ClientId;
            pMsg->pClient = pClient;

            vrc = hgcmMsgSend(pMsg);
        }
        else
        {
            LogRel(("(%d, %d) [%s] hgcmMsgAlloc(%p, SVC_MSG_DISCONNECT) failed %Rrc\n",
                    u32ClientId, fFromService, RT_VALID_PTR(m_pszSvcName)? m_pszSvcName: "", m_pThread, vrc));
        }
    }


    /*
     * Release the pClient->pService reference.
     */
    if (fReleaseService)
        ReleaseService();

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

int HGCMService::RegisterExtension(HGCMSVCEXTHANDLE handle,
                                   PFNHGCMSVCEXT pfnExtension,
                                   void *pvExtension)
{
    LogFlowFunc(("%s\n", handle->pszServiceName));

    /* Forward the message to the service thread. */
    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(m_pThread, &pCoreMsg, SVC_MSG_REGEXT, hgcmMessageAllocSvc);

    if (RT_SUCCESS(vrc))
    {
        HGCMMsgSvcRegisterExtension *pMsg = (HGCMMsgSvcRegisterExtension *)pCoreMsg;

        pMsg->handle       = handle;
        pMsg->pfnExtension = pfnExtension;
        pMsg->pvExtension  = pvExtension;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

void HGCMService::UnregisterExtension(HGCMSVCEXTHANDLE handle)
{
    /* Forward the message to the service thread. */
    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(m_pThread, &pCoreMsg, SVC_MSG_UNREGEXT, hgcmMessageAllocSvc);

    if (RT_SUCCESS(vrc))
    {
        HGCMMsgSvcUnregisterExtension *pMsg = (HGCMMsgSvcUnregisterExtension *)pCoreMsg;

        pMsg->handle = handle;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
}

/** @callback_method_impl{FNHGCMMSGCALLBACK}   */
static DECLCALLBACK(int) hgcmMsgCallCompletionCallback(int32_t result, HGCMMsgCore *pMsgCore)
{
    /*
     * Do common message completion then decrement the call counter
     * for the client if necessary.
     */
    int vrc = hgcmMsgCompletionCallback(result, pMsgCore);

    HGCMMsgCall *pMsg = (HGCMMsgCall *)pMsgCore;
    if (pMsg->pcCounter)
    {
        uint32_t cCalls = ASMAtomicDecU32(pMsg->pcCounter);
        AssertStmt(cCalls < UINT32_MAX / 2, ASMAtomicWriteU32(pMsg->pcCounter, 0));
        pMsg->pcCounter = NULL;
        Log3Func(("pMsg=%p cPendingCalls=%u / %u (fun %u, %u parms)\n",
                  pMsg, cCalls, pMsg->u32ClientId, pMsg->u32Function, pMsg->cParms));
    }

    return vrc;
}

/** Perform a guest call to the service.
 *
 * @param pHGCMPort      The port to be used for completion confirmation.
 * @param pCmd           The VBox HGCM context.
 * @param u32ClientId    The client handle to be disconnected and deleted.
 * @param pClient        The client data.
 * @param u32Function    The function number.
 * @param cParms         Number of parameters.
 * @param paParms        Pointer to array of parameters.
 * @param tsArrival      The STAM_GET_TS() value when the request arrived.
 * @return VBox status code.
 * @retval VINF_HGCM_ASYNC_EXECUTE on success.
 */
int HGCMService::GuestCall(PPDMIHGCMPORT pHGCMPort, PVBOXHGCMCMD pCmd, uint32_t u32ClientId, HGCMClient *pClient,
                           uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[], uint64_t tsArrival)
{
    LogFlow(("MAIN::HGCMService::GuestCall\n"));

    int vrc;
    HGCMMsgCall *pMsg = new(std::nothrow) HGCMMsgCall(m_pThread);
    if (pMsg)
    {
        pMsg->Reference(); /** @todo starts out with zero references. */

        uint32_t cCalls = ASMAtomicIncU32(&pClient->cPendingCalls);
        Assert(pClient->idxCategory < RT_ELEMENTS(m_fntable.acMaxCallsPerClient));
        if (cCalls < m_fntable.acMaxCallsPerClient[pClient->idxCategory])
        {
            pMsg->pcCounter   = &pClient->cPendingCalls;
            Log3(("MAIN::HGCMService::GuestCall: pMsg=%p cPendingCalls=%u / %u / %s (fun %u, %u parms)\n",
                  pMsg, cCalls, u32ClientId, m_pszSvcName, u32Function, cParms));

            pMsg->pCmd        = pCmd;
            pMsg->pHGCMPort   = pHGCMPort;
            pMsg->u32ClientId = u32ClientId;
            pMsg->u32Function = u32Function;
            pMsg->cParms      = cParms;
            pMsg->paParms     = paParms;
            pMsg->tsArrival   = tsArrival;

            vrc = hgcmMsgPost(pMsg, hgcmMsgCallCompletionCallback);

            if (RT_SUCCESS(vrc))
            { /* Reference donated on success. */ }
            else
            {
                ASMAtomicDecU32(&pClient->cPendingCalls);
                pMsg->pcCounter = NULL;
                Log(("MAIN::HGCMService::GuestCall: hgcmMsgPost failed: %Rrc\n", vrc));
                pMsg->Dereference();
            }
        }
        else
        {
            ASMAtomicDecU32(&pClient->cPendingCalls);
            LogRel2(("HGCM: Too many calls to '%s' from client %u: %u, max %u; category %u\n", m_pszSvcName, u32ClientId,
                     cCalls, m_fntable.acMaxCallsPerClient[pClient->idxCategory], pClient->idxCategory));
            pMsg->Dereference();
            STAM_REL_COUNTER_INC(&m_StatTooManyCalls);
            vrc = VERR_HGCM_TOO_MANY_CLIENT_CALLS;
        }
    }
    else
    {
        Log(("MAIN::HGCMService::GuestCall: Message allocation failed\n"));
        vrc = VERR_NO_MEMORY;
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/** Guest cancelled a request (call, connection attempt, disconnect attempt).
 *
 * @param   pHGCMPort      The port to be used for completion confirmation
 * @param   pCmd           The VBox HGCM context.
 * @param   idClient       The client handle to be disconnected and deleted.
 */
void HGCMService::GuestCancelled(PPDMIHGCMPORT pHGCMPort, PVBOXHGCMCMD pCmd, uint32_t idClient)
{
    LogFlow(("MAIN::HGCMService::GuestCancelled\n"));

    if (m_fntable.pfnCancelled)
    {
        HGCMMsgCancelled *pMsg = new (std::nothrow) HGCMMsgCancelled(m_pThread);
        if (pMsg)
        {
            pMsg->Reference(); /** @todo starts out with zero references. */

            pMsg->pCmd      = pCmd;
            pMsg->pHGCMPort = pHGCMPort;
            pMsg->idClient  = idClient;

            hgcmMsgPost(pMsg, NULL);
        }
        else
            Log(("MAIN::HGCMService::GuestCancelled: Message allocation failed\n"));
    }
}

/** Perform a host call the service.
 *
 * @param u32Function    The function number.
 * @param cParms         Number of parameters.
 * @param paParms        Pointer to array of parameters.
 * @return VBox status code.
 */
int HGCMService::HostCall(uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM *paParms)
{
    LogFlowFunc(("%s u32Function = %d, cParms = %d, paParms = %p\n",
                 m_pszSvcName, u32Function, cParms, paParms));

    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(m_pThread, &pCoreMsg, SVC_MSG_HOSTCALL, hgcmMessageAllocSvc);

    if (RT_SUCCESS(vrc))
    {
        HGCMMsgHostCallSvc *pMsg = (HGCMMsgHostCallSvc *)pCoreMsg;

        pMsg->u32Function      = u32Function;
        pMsg->cParms           = cParms;
        pMsg->paParms          = paParms;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/** Posts a broadcast notification event to all interested services.
 *
 * @param   enmEvent    The notification event.
 */
/*static*/ void HGCMService::BroadcastNotify(HGCMNOTIFYEVENT enmEvent)
{
    for (HGCMService *pService = sm_pSvcListHead; pService != NULL; pService = pService->m_pSvcNext)
    {
        pService->Notify(enmEvent);
    }
}

/** Posts a broadcast notification event to the service.
 *
 * @param   enmEvent    The notification event.
 */
void HGCMService::Notify(HGCMNOTIFYEVENT enmEvent)
{
    LogFlowFunc(("%s enmEvent=%d pfnNotify=%p\n", m_pszSvcName, enmEvent, m_fntable.pfnNotify));
    if (m_fntable.pfnNotify)
    {
        HGCMMsgCore *pCoreMsg;
        int vrc = hgcmMsgAlloc(m_pThread, &pCoreMsg, SVC_MSG_NOTIFY, hgcmMessageAllocSvc);
        if (RT_SUCCESS(vrc))
        {
            HGCMMsgNotify *pMsg = (HGCMMsgNotify *)pCoreMsg;
            pMsg->enmEvent = enmEvent;

            vrc = hgcmMsgPost(pMsg, NULL);
            AssertRC(vrc);
        }
    }
}

/*
 * Main HGCM thread that manages services.
 */

/* Messages processed by the main HGCM thread. */
#define HGCM_MSG_CONNECT    (10)  /**< Connect a client to a service. */
#define HGCM_MSG_DISCONNECT (11)  /**< Disconnect the specified client id. */
#define HGCM_MSG_LOAD       (12)  /**< Load the service. */
#define HGCM_MSG_HOSTCALL   (13)  /**< Call the service. */
#define HGCM_MSG_LOADSTATE  (14)  /**< Load saved state for the specified service. */
#define HGCM_MSG_SAVESTATE  (15)  /**< Save state for the specified service. */
#define HGCM_MSG_RESET      (16)  /**< Disconnect all clients from the specified service. */
#define HGCM_MSG_QUIT       (17)  /**< Unload all services and terminate the thread. */
#define HGCM_MSG_REGEXT     (18)  /**< Register a service extension. */
#define HGCM_MSG_UNREGEXT   (19)  /**< Unregister a service extension. */
#define HGCM_MSG_BRD_NOTIFY (20)  /**< Broadcast notification event (VM state change). */

class HGCMMsgMainConnect: public HGCMMsgHeader
{
    public:
        /* Service name. */
        const char *pszServiceName;
        /* Where to store the client handle. */
        uint32_t *pu32ClientId;
};

class HGCMMsgMainDisconnect: public HGCMMsgHeader
{
    public:
        /* Handle of the client to be disconnected. */
        uint32_t u32ClientId;
};

class HGCMMsgMainLoad: public HGCMMsgCore
{
    public:
        /* Name of the library to be loaded. */
        const char *pszServiceLibrary;
        /* Name to be assigned to the service. */
        const char *pszServiceName;
        /** The user mode VM handle (for statistics and such). */
        PUVM pUVM;
        /** The VMM vtable (for statistics and such). */
        PCVMMR3VTABLE pVMM;
        /** The HGCM port on the VMMDev device (for session ID and such). */
        PPDMIHGCMPORT pHgcmPort;
};

class HGCMMsgMainHostCall: public HGCMMsgCore
{
    public:
        /* Which service to call. */
        const char *pszServiceName;
        /* Function number. */
        uint32_t u32Function;
        /* Number of the function parameters. */
        uint32_t cParms;
        /* Pointer to array of the function parameters. */
        VBOXHGCMSVCPARM *paParms;
};

class HGCMMsgMainLoadSaveState: public HGCMMsgCore
{
    public:
        /** Saved state handle. */
        PSSMHANDLE pSSM;
        /** The VMM vtable. */
        PCVMMR3VTABLE pVMM;
        /** The HGCM saved state version being loaded (ignore for save). */
        uint32_t uVersion;
};

class HGCMMsgMainReset: public HGCMMsgCore
{
    public:
        /** Set if this is actually a shutdown and not a VM reset. */
        bool fForShutdown;
};

class HGCMMsgMainQuit: public HGCMMsgCore
{
    public:
        /** Whether UVM has gone invalid already or not. */
        bool fUvmIsInvalid;
};

class HGCMMsgMainRegisterExtension: public HGCMMsgCore
{
    public:
        /** Returned handle to be used in HGCMMsgMainUnregisterExtension. */
        HGCMSVCEXTHANDLE *pHandle;
        /** Name of the service. */
        const char *pszServiceName;
        /** The extension entry point. */
        PFNHGCMSVCEXT pfnExtension;
        /** The extension pointer. */
        void *pvExtension;
};

class HGCMMsgMainUnregisterExtension: public HGCMMsgCore
{
    public:
        /* Handle of the registered extension. */
        HGCMSVCEXTHANDLE handle;
};

class HGCMMsgMainBroadcastNotify: public HGCMMsgCore
{
    public:
        /** The notification event. */
        HGCMNOTIFYEVENT enmEvent;
};


static HGCMMsgCore *hgcmMainMessageAlloc (uint32_t u32MsgId)
{
    switch (u32MsgId)
    {
        case HGCM_MSG_CONNECT:    return new HGCMMsgMainConnect();
        case HGCM_MSG_DISCONNECT: return new HGCMMsgMainDisconnect();
        case HGCM_MSG_LOAD:       return new HGCMMsgMainLoad();
        case HGCM_MSG_HOSTCALL:   return new HGCMMsgMainHostCall();
        case HGCM_MSG_LOADSTATE:
        case HGCM_MSG_SAVESTATE:  return new HGCMMsgMainLoadSaveState();
        case HGCM_MSG_RESET:      return new HGCMMsgMainReset();
        case HGCM_MSG_QUIT:       return new HGCMMsgMainQuit();
        case HGCM_MSG_REGEXT:     return new HGCMMsgMainRegisterExtension();
        case HGCM_MSG_UNREGEXT:   return new HGCMMsgMainUnregisterExtension();
        case HGCM_MSG_BRD_NOTIFY: return new HGCMMsgMainBroadcastNotify();

        default:
            AssertReleaseMsgFailed(("Msg id = %08X\n", u32MsgId));
    }

    return NULL;
}


/* The main HGCM thread handler. */
static DECLCALLBACK(void) hgcmThread(HGCMThread *pThread, void *pvUser)
{
    LogFlowFunc(("pThread = %p, pvUser = %p\n", pThread, pvUser));

    NOREF(pvUser);

    bool fQuit = false;

    while (!fQuit)
    {
        HGCMMsgCore *pMsgCore;
        int vrc = hgcmMsgGet(pThread, &pMsgCore);

        if (RT_FAILURE(vrc))
        {
            /* The error means some serious unrecoverable problem in the hgcmMsg/hgcmThread layer. */
            AssertMsgFailed(("%Rrc\n", vrc));
            break;
        }

        uint32_t u32MsgId = pMsgCore->MsgId();

        switch (u32MsgId)
        {
            case HGCM_MSG_CONNECT:
            {
                HGCMMsgMainConnect *pMsg = (HGCMMsgMainConnect *)pMsgCore;

                LogFlowFunc(("HGCM_MSG_CONNECT pszServiceName %s, pu32ClientId %p\n",
                             pMsg->pszServiceName, pMsg->pu32ClientId));

                /* Resolve the service name to the pointer to service instance.
                 */
                HGCMService *pService;
                vrc = HGCMService::ResolveService(&pService, pMsg->pszServiceName);

                if (RT_SUCCESS(vrc))
                {
                    /* Call the service instance method. */
                    vrc = pService->CreateAndConnectClient(pMsg->pu32ClientId,
                                                           0,
                                                           pMsg->pHGCMPort->pfnGetRequestor(pMsg->pHGCMPort, pMsg->pCmd),
                                                           pMsg->pHGCMPort->pfnIsCmdRestored(pMsg->pHGCMPort, pMsg->pCmd));

                    /* Release the service after resolve. */
                    pService->ReleaseService();
                }
            } break;

            case HGCM_MSG_DISCONNECT:
            {
                HGCMMsgMainDisconnect *pMsg = (HGCMMsgMainDisconnect *)pMsgCore;

                LogFlowFunc(("HGCM_MSG_DISCONNECT u32ClientId = %d\n",
                             pMsg->u32ClientId));

                HGCMClient *pClient = HGCMClient::ReferenceByHandle(pMsg->u32ClientId);

                if (!pClient)
                {
                    vrc = VERR_HGCM_INVALID_CLIENT_ID;
                    break;
                }

                /* The service the client belongs to. */
                HGCMService *pService = pClient->pService;

                /* Call the service instance to disconnect the client. */
                vrc = pService->DisconnectClient(pMsg->u32ClientId, false, pClient);

                hgcmObjDereference(pClient);
            } break;

            case HGCM_MSG_LOAD:
            {
                HGCMMsgMainLoad *pMsg = (HGCMMsgMainLoad *)pMsgCore;

                LogFlowFunc(("HGCM_MSG_LOAD pszServiceName = %s, pMsg->pszServiceLibrary = %s, pMsg->pUVM = %p\n",
                             pMsg->pszServiceName, pMsg->pszServiceLibrary, pMsg->pUVM));

                vrc = HGCMService::LoadService(pMsg->pszServiceLibrary, pMsg->pszServiceName,
                                               pMsg->pUVM, pMsg->pVMM, pMsg->pHgcmPort);
            } break;

            case HGCM_MSG_HOSTCALL:
            {
                HGCMMsgMainHostCall *pMsg = (HGCMMsgMainHostCall *)pMsgCore;

                LogFlowFunc(("HGCM_MSG_HOSTCALL pszServiceName %s, u32Function %d, cParms %d, paParms %p\n",
                             pMsg->pszServiceName, pMsg->u32Function, pMsg->cParms, pMsg->paParms));

                /* Resolve the service name to the pointer to service instance. */
                HGCMService *pService;
                vrc = HGCMService::ResolveService(&pService, pMsg->pszServiceName);

                if (RT_SUCCESS(vrc))
                {
                    vrc = pService->HostCall(pMsg->u32Function, pMsg->cParms, pMsg->paParms);

                    pService->ReleaseService();
                }
            } break;

            case HGCM_MSG_BRD_NOTIFY:
            {
                HGCMMsgMainBroadcastNotify *pMsg = (HGCMMsgMainBroadcastNotify *)pMsgCore;

                LogFlowFunc(("HGCM_MSG_BRD_NOTIFY enmEvent=%d\n", pMsg->enmEvent));

                HGCMService::BroadcastNotify(pMsg->enmEvent);
            } break;

            case HGCM_MSG_RESET:
            {
                LogFlowFunc(("HGCM_MSG_RESET\n"));

                HGCMService::Reset();

                HGCMMsgMainReset *pMsg = (HGCMMsgMainReset *)pMsgCore;
                if (!pMsg->fForShutdown)
                    HGCMService::BroadcastNotify(HGCMNOTIFYEVENT_RESET);
            } break;

            case HGCM_MSG_LOADSTATE:
            {
                HGCMMsgMainLoadSaveState *pMsg = (HGCMMsgMainLoadSaveState *)pMsgCore;

                LogFlowFunc(("HGCM_MSG_LOADSTATE\n"));

                vrc = HGCMService::LoadState(pMsg->pSSM, pMsg->pVMM, pMsg->uVersion);
            } break;

            case HGCM_MSG_SAVESTATE:
            {
                HGCMMsgMainLoadSaveState *pMsg = (HGCMMsgMainLoadSaveState *)pMsgCore;

                LogFlowFunc(("HGCM_MSG_SAVESTATE\n"));

                vrc = HGCMService::SaveState(pMsg->pSSM, pMsg->pVMM);
            } break;

            case HGCM_MSG_QUIT:
            {
                HGCMMsgMainQuit *pMsg = (HGCMMsgMainQuit *)pMsgCore;
                LogFlowFunc(("HGCM_MSG_QUIT\n"));

                HGCMService::UnloadAll(pMsg->fUvmIsInvalid);

                fQuit = true;
            } break;

            case HGCM_MSG_REGEXT:
            {
                HGCMMsgMainRegisterExtension *pMsg = (HGCMMsgMainRegisterExtension *)pMsgCore;

                LogFlowFunc(("HGCM_MSG_REGEXT\n"));

                /* Allocate the handle data. */
                HGCMSVCEXTHANDLE handle = (HGCMSVCEXTHANDLE)RTMemAllocZ(sizeof(struct _HGCMSVCEXTHANDLEDATA)
                                                                        + strlen(pMsg->pszServiceName)
                                                                        + sizeof(char));

                if (handle == NULL)
                {
                    vrc = VERR_NO_MEMORY;
                }
                else
                {
                    handle->pszServiceName = (char *)((uint8_t *)handle + sizeof(struct _HGCMSVCEXTHANDLEDATA));
                    strcpy(handle->pszServiceName, pMsg->pszServiceName);

                    HGCMService *pService;
                    vrc = HGCMService::ResolveService(&pService, handle->pszServiceName);

                    if (RT_SUCCESS(vrc))
                    {
                        pService->RegisterExtension(handle, pMsg->pfnExtension, pMsg->pvExtension);

                        pService->ReleaseService();
                    }

                    if (RT_FAILURE(vrc))
                    {
                        RTMemFree(handle);
                    }
                    else
                    {
                        *pMsg->pHandle = handle;
                    }
                }
            } break;

            case HGCM_MSG_UNREGEXT:
            {
                HGCMMsgMainUnregisterExtension *pMsg = (HGCMMsgMainUnregisterExtension *)pMsgCore;

                LogFlowFunc(("HGCM_MSG_UNREGEXT\n"));

                HGCMService *pService;
                vrc = HGCMService::ResolveService(&pService, pMsg->handle->pszServiceName);

                if (RT_SUCCESS(vrc))
                {
                    pService->UnregisterExtension(pMsg->handle);

                    pService->ReleaseService();
                }

                RTMemFree(pMsg->handle);
            } break;

            default:
            {
                AssertMsgFailed(("hgcmThread: Unsupported message number %08X!!!\n", u32MsgId));
                vrc = VERR_NOT_SUPPORTED;
            } break;
        }

        /* Complete the message processing. */
        hgcmMsgComplete(pMsgCore, vrc);

        LogFlowFunc(("message processed %Rrc\n", vrc));
    }
}


/*
 * The HGCM API.
 */

/** The main hgcm thread. */
static HGCMThread *g_pHgcmThread = 0;

/*
 * Public HGCM functions.
 *
 * hgcmGuest* - called as a result of the guest HGCM requests.
 * hgcmHost*  - called by the host.
 */

/* Load a HGCM service from the specified library.
 * Assign the specified name to the service.
 *
 * @param pszServiceLibrary  The library to be loaded.
 * @param pszServiceName     The name to be assigned to the service.
 * @param pUVM               The user mode VM handle (for statistics and such).
 * @param pVMM               The VMM vtable (for statistics and such).
 * @param pHgcmPort          The HGCM port on the VMMDev device (for session ID and such).
 * @return VBox status code.
 */
int HGCMHostLoad(const char *pszServiceLibrary,
                 const char *pszServiceName,
                 PUVM pUVM,
                 PCVMMR3VTABLE pVMM,
                 PPDMIHGCMPORT pHgcmPort)
{
    LogFlowFunc(("lib = %s, name = %s\n", pszServiceLibrary, pszServiceName));

    if (!pszServiceLibrary || !pszServiceName)
        return VERR_INVALID_PARAMETER;

    /* Forward the request to the main hgcm thread. */
    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(g_pHgcmThread, &pCoreMsg, HGCM_MSG_LOAD, hgcmMainMessageAlloc);
    if (RT_SUCCESS(vrc))
    {
        /* Initialize the message. Since the message is synchronous, use the supplied pointers. */
        HGCMMsgMainLoad *pMsg = (HGCMMsgMainLoad *)pCoreMsg;

        pMsg->pszServiceLibrary = pszServiceLibrary;
        pMsg->pszServiceName    = pszServiceName;
        pMsg->pUVM              = pUVM;
        pMsg->pVMM              = pVMM;
        pMsg->pHgcmPort         = pHgcmPort;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/* Register a HGCM service extension.
 *
 * @param pHandle            Returned handle for the registered extension.
 * @param pszServiceName     The name of the service.
 * @param pfnExtension       The extension entry point (callback).
 * @param pvExtension        The extension pointer.
 * @return VBox status code.
 */
int HGCMHostRegisterServiceExtension(HGCMSVCEXTHANDLE *pHandle,
                                     const char *pszServiceName,
                                     PFNHGCMSVCEXT pfnExtension,
                                     void *pvExtension)
{
    LogFlowFunc(("pHandle = %p, name = %s, pfn = %p, rv = %p\n", pHandle, pszServiceName, pfnExtension, pvExtension));

    if (!pHandle || !pszServiceName || !pfnExtension)
    {
        return VERR_INVALID_PARAMETER;
    }

    /* Forward the request to the main hgcm thread. */
    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(g_pHgcmThread, &pCoreMsg, HGCM_MSG_REGEXT, hgcmMainMessageAlloc);

    if (RT_SUCCESS(vrc))
    {
        /* Initialize the message. Since the message is synchronous, use the supplied pointers. */
        HGCMMsgMainRegisterExtension *pMsg = (HGCMMsgMainRegisterExtension *)pCoreMsg;

        pMsg->pHandle        = pHandle;
        pMsg->pszServiceName = pszServiceName;
        pMsg->pfnExtension   = pfnExtension;
        pMsg->pvExtension    = pvExtension;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("*pHandle = %p, vrc = %Rrc\n", *pHandle, vrc));
    return vrc;
}

void HGCMHostUnregisterServiceExtension(HGCMSVCEXTHANDLE handle)
{
    LogFlowFunc(("handle = %p\n", handle));

    /* Forward the request to the main hgcm thread. */
    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(g_pHgcmThread, &pCoreMsg, HGCM_MSG_UNREGEXT, hgcmMainMessageAlloc);

    if (RT_SUCCESS(vrc))
    {
        /* Initialize the message. */
        HGCMMsgMainUnregisterExtension *pMsg = (HGCMMsgMainUnregisterExtension *)pCoreMsg;

        pMsg->handle = handle;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return;
}

/* Find a service and inform it about a client connection, create a client handle.
 *
 * @param pHGCMPort      The port to be used for completion confirmation.
 * @param pCmd           The VBox HGCM context.
 * @param pszServiceName The name of the service to be connected to.
 * @param pu32ClientId   Where the store the created client handle.
 * @return VBox status code.
 */
int HGCMGuestConnect(PPDMIHGCMPORT pHGCMPort,
                     PVBOXHGCMCMD pCmd,
                     const char *pszServiceName,
                     uint32_t *pu32ClientId)
{
    LogFlowFunc(("pHGCMPort = %p, pCmd = %p, name = %s, pu32ClientId = %p\n",
                 pHGCMPort, pCmd, pszServiceName, pu32ClientId));

    if (pHGCMPort == NULL || pCmd == NULL || pszServiceName == NULL || pu32ClientId == NULL)
    {
        return VERR_INVALID_PARAMETER;
    }

    /* Forward the request to the main hgcm thread. */
    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(g_pHgcmThread, &pCoreMsg, HGCM_MSG_CONNECT, hgcmMainMessageAlloc);

    if (RT_SUCCESS(vrc))
    {
        /* Initialize the message. Since 'pszServiceName' and 'pu32ClientId'
         * will not be deallocated by the caller until the message is completed,
         * use the supplied pointers.
         */
        HGCMMsgMainConnect *pMsg = (HGCMMsgMainConnect *)pCoreMsg;

        pMsg->pHGCMPort      = pHGCMPort;
        pMsg->pCmd           = pCmd;
        pMsg->pszServiceName = pszServiceName;
        pMsg->pu32ClientId   = pu32ClientId;

        vrc = hgcmMsgPost(pMsg, hgcmMsgCompletionCallback);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/* Tell a service that the client is disconnecting, destroy the client handle.
 *
 * @param pHGCMPort      The port to be used for completion confirmation.
 * @param pCmd           The VBox HGCM context.
 * @param u32ClientId    The client handle to be disconnected and deleted.
 * @return VBox status code.
 */
int HGCMGuestDisconnect(PPDMIHGCMPORT pHGCMPort,
                        PVBOXHGCMCMD pCmd,
                        uint32_t u32ClientId)
{
    LogFlowFunc(("pHGCMPort = %p, pCmd = %p, u32ClientId = %d\n",
                  pHGCMPort, pCmd, u32ClientId));

    if (!pHGCMPort || !pCmd || !u32ClientId)
    {
        return VERR_INVALID_PARAMETER;
    }

    /* Forward the request to the main hgcm thread. */
    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(g_pHgcmThread, &pCoreMsg, HGCM_MSG_DISCONNECT, hgcmMainMessageAlloc);

    if (RT_SUCCESS(vrc))
    {
        /* Initialize the message. */
        HGCMMsgMainDisconnect *pMsg = (HGCMMsgMainDisconnect *)pCoreMsg;

        pMsg->pCmd        = pCmd;
        pMsg->pHGCMPort   = pHGCMPort;
        pMsg->u32ClientId = u32ClientId;

        vrc = hgcmMsgPost(pMsg, hgcmMsgCompletionCallback);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/** Helper to send either HGCM_MSG_SAVESTATE or HGCM_MSG_LOADSTATE messages to the main HGCM thread.
 *
 * @param pSSM     The SSM handle.
 * @param pVMM      The VMM vtable.
 * @param idMsg    The message to be sent: HGCM_MSG_SAVESTATE or HGCM_MSG_LOADSTATE.
 * @param uVersion The state version being loaded.
 * @return VBox status code.
 */
static int hgcmHostLoadSaveState(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t idMsg, uint32_t uVersion)
{
    LogFlowFunc(("pSSM = %p, pVMM = %p, idMsg = %d, uVersion = %#x\n", pSSM, pVMM, idMsg, uVersion));

    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(g_pHgcmThread, &pCoreMsg, idMsg, hgcmMainMessageAlloc);
    if (RT_SUCCESS(vrc))
    {
        HGCMMsgMainLoadSaveState *pMsg = (HGCMMsgMainLoadSaveState *)pCoreMsg;
        AssertRelease(pMsg);

        pMsg->pSSM = pSSM;
        pMsg->pVMM = pVMM;
        pMsg->uVersion = uVersion;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/** Save the state of services.
 *
 * @param pSSM      The SSM handle.
 * @param pVMM      The VMM vtable.
 * @return VBox status code.
 */
int HGCMHostSaveState(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM)
{
    return hgcmHostLoadSaveState(pSSM, pVMM, HGCM_MSG_SAVESTATE, HGCM_SAVED_STATE_VERSION);
}

/** Load the state of services.
 *
 * @param pSSM      The SSM handle.
 * @param pVMM      The VMM vtable.
 * @param uVersion  The state version being loaded.
 * @return VBox status code.
 */
int HGCMHostLoadState(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t uVersion)
{
    return hgcmHostLoadSaveState(pSSM, pVMM, HGCM_MSG_LOADSTATE, uVersion);
}

/** The guest calls the service.
 *
 * @param pHGCMPort      The port to be used for completion confirmation.
 * @param pCmd           The VBox HGCM context.
 * @param u32ClientId    The client handle.
 * @param u32Function    The function number.
 * @param cParms         Number of parameters.
 * @param paParms        Pointer to array of parameters.
 * @param tsArrival      The STAM_GET_TS() value when the request arrived.
 * @return VBox status code.
 */
int HGCMGuestCall(PPDMIHGCMPORT pHGCMPort,
                  PVBOXHGCMCMD pCmd,
                  uint32_t u32ClientId,
                  uint32_t u32Function,
                  uint32_t cParms,
                  VBOXHGCMSVCPARM *paParms,
                  uint64_t tsArrival)
{
    LogFlowFunc(("pHGCMPort = %p, pCmd = %p, u32ClientId = %d, u32Function = %d, cParms = %d, paParms = %p\n",
                  pHGCMPort, pCmd, u32ClientId, u32Function, cParms, paParms));

    if (!pHGCMPort || !pCmd || u32ClientId == 0)
    {
        return VERR_INVALID_PARAMETER;
    }

    int vrc = VERR_HGCM_INVALID_CLIENT_ID;

    /* Resolve the client handle to the client instance pointer. */
    HGCMClient *pClient = HGCMClient::ReferenceByHandleForGuest(u32ClientId);

    if (pClient)
    {
        AssertRelease(pClient->pService);

        /* Forward the message to the service thread. */
        vrc = pClient->pService->GuestCall(pHGCMPort, pCmd, u32ClientId, pClient, u32Function, cParms, paParms, tsArrival);

        hgcmObjDereference(pClient);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/** The guest cancelled a request (call, connect, disconnect)
 *
 * @param   pHGCMPort      The port to be used for completion confirmation.
 * @param   pCmd           The VBox HGCM context.
 * @param   idClient       The client handle.
 */
void HGCMGuestCancelled(PPDMIHGCMPORT pHGCMPort, PVBOXHGCMCMD pCmd, uint32_t idClient)
{
    LogFlowFunc(("pHGCMPort = %p, pCmd = %p, idClient = %d\n", pHGCMPort, pCmd, idClient));
    AssertReturnVoid(pHGCMPort);
    AssertReturnVoid(pCmd);
    AssertReturnVoid(idClient != 0);

    /* Resolve the client handle to the client instance pointer. */
    HGCMClient *pClient = HGCMClient::ReferenceByHandleForGuest(idClient);

    if (pClient)
    {
        AssertRelease(pClient->pService);

        /* Forward the message to the service thread. */
        pClient->pService->GuestCancelled(pHGCMPort, pCmd, idClient);

        hgcmObjDereference(pClient);
    }

    LogFlowFunc(("returns\n"));
}

/** The host calls the service.
 *
 * @param pszServiceName The service name to be called.
 * @param u32Function    The function number.
 * @param cParms         Number of parameters.
 * @param paParms        Pointer to array of parameters.
 * @return VBox status code.
 */
int HGCMHostCall(const char *pszServiceName,
                 uint32_t u32Function,
                 uint32_t cParms,
                 VBOXHGCMSVCPARM *paParms)
{
    LogFlowFunc(("name = %s, u32Function = %d, cParms = %d, paParms = %p\n",
                 pszServiceName, u32Function, cParms, paParms));

    if (!pszServiceName)
    {
        return VERR_INVALID_PARAMETER;
    }

    /* Host calls go to main HGCM thread that resolves the service name to the
     * service instance pointer and then, using the service pointer, forwards
     * the message to the service thread.
     * So it is slow but host calls are intended mostly for configuration and
     * other non-time-critical functions.
     */
    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(g_pHgcmThread, &pCoreMsg, HGCM_MSG_HOSTCALL, hgcmMainMessageAlloc);

    if (RT_SUCCESS(vrc))
    {
        HGCMMsgMainHostCall *pMsg = (HGCMMsgMainHostCall *)pCoreMsg;

        pMsg->pszServiceName = (char *)pszServiceName;
        pMsg->u32Function    = u32Function;
        pMsg->cParms         = cParms;
        pMsg->paParms        = paParms;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

/** Posts a notification event to all services.
 *
 * @param   enmEvent    The notification event.
 * @return  VBox status code.
 */
int HGCMBroadcastEvent(HGCMNOTIFYEVENT enmEvent)
{
    LogFlowFunc(("enmEvent=%d\n", enmEvent));

    HGCMMsgCore *pCoreMsg;
    int vrc = hgcmMsgAlloc(g_pHgcmThread, &pCoreMsg, HGCM_MSG_BRD_NOTIFY, hgcmMainMessageAlloc);

    if (RT_SUCCESS(vrc))
    {
        HGCMMsgMainBroadcastNotify *pMsg = (HGCMMsgMainBroadcastNotify *)pCoreMsg;

        pMsg->enmEvent = enmEvent;

        vrc = hgcmMsgPost(pMsg, NULL);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}


int HGCMHostReset(bool fForShutdown)
{
    LogFlowFunc(("\n"));

    /* Disconnect all clients.
     */

    HGCMMsgCore *pMsgCore;
    int vrc = hgcmMsgAlloc(g_pHgcmThread, &pMsgCore, HGCM_MSG_RESET, hgcmMainMessageAlloc);

    if (RT_SUCCESS(vrc))
    {
        HGCMMsgMainReset *pMsg = (HGCMMsgMainReset *)pMsgCore;

        pMsg->fForShutdown = fForShutdown;

        vrc = hgcmMsgSend(pMsg);
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

int HGCMHostInit(void)
{
    LogFlowFunc(("\n"));

    int vrc = hgcmThreadInit();

    if (RT_SUCCESS(vrc))
    {
        /*
         * Start main HGCM thread.
         */

        vrc = hgcmThreadCreate(&g_pHgcmThread, "MainHGCMthread", hgcmThread, NULL /*pvUser*/,
                               NULL /*pszStatsSubDir*/, NULL /*pUVM*/, NULL /*pVMM*/);

        if (RT_FAILURE(vrc))
            LogRel(("Failed to start HGCM thread. HGCM services will be unavailable!!! vrc = %Rrc\n", vrc));
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

int HGCMHostShutdown(bool fUvmIsInvalid /*= false*/)
{
    LogFlowFunc(("\n"));

    /*
     * Do HGCMReset and then unload all services.
     */

    int vrc = HGCMHostReset(true /*fForShutdown*/);

    if (RT_SUCCESS(vrc))
    {
        /* Send the quit message to the main hgcmThread. */
        HGCMMsgCore *pMsgCore;
        vrc = hgcmMsgAlloc(g_pHgcmThread, &pMsgCore, HGCM_MSG_QUIT, hgcmMainMessageAlloc);

        if (RT_SUCCESS(vrc))
        {
            HGCMMsgMainQuit *pMsg = (HGCMMsgMainQuit *)pMsgCore;
            pMsg->fUvmIsInvalid = fUvmIsInvalid;

            vrc = hgcmMsgSend(pMsg);

            if (RT_SUCCESS(vrc))
            {
                /* Wait for the thread termination. */
                hgcmThreadWait(g_pHgcmThread);
                g_pHgcmThread = NULL;

                hgcmThreadUninit();
            }
        }
    }

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    return vrc;
}

