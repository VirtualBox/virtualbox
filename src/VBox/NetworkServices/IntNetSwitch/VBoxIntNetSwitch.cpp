/* $Id: VBoxIntNetSwitch.cpp 114959 2026-08-10 14:41:23Z andreas.loeffler@oracle.com $ */
/** @file
 * Internal networking - Wrapper for the R0 network service.
 *
 * This is a bit hackish as we're mixing context here, however it is
 * very useful when making changes to the internal networking service.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
#define IN_INTNET_TESTCASE
#define IN_INTNET_R3
#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
# if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3_LOCALIPC)
#  error "The local IPC R3 IntNet service implementation is not enabled!"
# endif
#endif
#include "IntNetSwitchInternal.h"

#include <VBox/err.h>
#include <VBox/intnetr3ipc.h>
#include <VBox/vmm/vmm.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/fs.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>

#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
# include <xpc/xpc.h>
# include <errno.h>
#else
# include <iprt/base64.h>
# include <iprt/env.h>
# include <iprt/localipc.h>
# include <iprt/process.h>
# include <iprt/shmem.h>
# include <iprt/uuid.h>
#endif

#ifndef RT_OS_WINDOWS
# include <unistd.h>
#endif

#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
static int intnetR3LocalIpcSendPoke(struct SUPDRVSESSION *pSession);
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** A client has registered an asynchronous receive wait. */
#define INTNETR3RECVSTATE_F_WAITING             RT_BIT_32(0)
/** Receive data arrived before a client registered a wait. */
#define INTNETR3RECVSTATE_F_AVAILABLE           RT_BIT_32(1)
/** The client aborted receive waits permanently for this interface session. */
#define INTNETR3RECVSTATE_F_NO_MORE_WAITS       RT_BIT_32(2)
/** Maximum combined IntNet shared-buffer allocation accepted over an R3 transport. */
#define INTNETR3_MAX_BUFFER_SIZE                 UINT64_C(134217728)
/** Maximum number of active transport connections. */
#define INTNETR3_MAX_CONNECTIONS                 UINT32_C(128)
/** Maximum number of local IPC request and notification worker threads. */
#define INTNETR3_MAX_THREADS                     UINT32_C(256)
/** Number of worker threads reserved by each local IPC session. */
#define INTNETR3_THREADS_PER_LOCALIPC_SESSION    UINT32_C(2)
/** Maximum aggregate shared memory allocated by the service. */
#define INTNETR3_MAX_AGGREGATE_SHMEM_SIZE        UINT64_C(1073741824)
/** Number of fresh UUID names tried if a shared-memory object already exists. */
#define INTNETR3_SHMEM_CREATE_RETRIES            16


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Registered object.
 * This takes care of reference counting and tracking data for access checks.
 */
typedef struct SUPDRVOBJ
{
    /** Pointer to the next in the global list. */
    struct SUPDRVOBJ * volatile     pNext;
    /** Pointer to the object destructor.
     * This may be set to NULL if the image containing the destructor get unloaded. */
    PFNSUPDRVDESTRUCTOR             pfnDestructor;
    /** User argument 1. */
    void                           *pvUser1;
    /** User argument 2. */
    void                           *pvUser2;
    /** The total sum of all per-session usage. */
    uint32_t volatile               cUsage;
} SUPDRVOBJ, *PSUPDRVOBJ;


/**
 * The per-session object usage record.
 */
typedef struct SUPDRVUSAGE
{
    /** Pointer to the next in the list. */
    struct SUPDRVUSAGE * volatile   pNext;
    /** Pointer to the object we're recording usage for. */
    PSUPDRVOBJ                      pObj;
    /** The usage count. */
    uint32_t volatile               cUsage;
} SUPDRVUSAGE, *PSUPDRVUSAGE;


/**
 * Device extension.
 */
typedef struct SUPDRVDEVEXT
{
    /** Number of active transport connections. */
    uint32_t volatile               cRefs;
    /** Number of active local IPC worker threads. */
    uint32_t volatile               cThreads;
    /** Aggregate size of all shared-memory allocations. */
    uint64_t                        cbShMem;
    /** Maximum number of active transport connections. */
    uint32_t                        cMaxConnections;
    /** Maximum number of active local IPC worker threads. */
    uint32_t                        cMaxThreads;
    /** Maximum aggregate size of all shared-memory allocations. */
    uint64_t                        cbMaxShMem;
    /** Maximum time allowed to receive the first frame or complete a started frame. */
    uint32_t                        cMsReadTimeout;
    /** Critical section to serialize the initialization, usage counting and objects. */
    RTCRITSECT                      CritSect;
    /** List of registered objects. Protected by the spinlock. */
    PSUPDRVOBJ volatile             pObjs;
} SUPDRVDEVEXT;
typedef SUPDRVDEVEXT *PSUPDRVDEVEXT;


typedef struct INTNETR3SHMEM
{
    struct INTNETR3SHMEM *pNext;
    /** Original mapping address used as the allocation lookup key. */
    void                *pvKey;
    /** Mapping address while it still needs to be unmapped. */
    void                *pv;
    size_t               cb;
#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
    RTSHMEM              hShMem;
    char                 szName[INTNET_R3_IPC_MAX_SHMEM_NAME];
#endif
} INTNETR3SHMEM;
typedef INTNETR3SHMEM *PINTNETR3SHMEM;


/**
 * Per session data.
 * This is mainly for memory tracking.
 */
typedef struct SUPDRVSESSION
{
    PSUPDRVDEVEXT                   pDevExt;
    /** List of generic usage records. (protected by SUPDRVDEVEXT::CritSect) */
    PSUPDRVUSAGE volatile           pUsage;
#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
    /** The XPC connection handle for this session. */
    xpc_connection_t                hXpcCon;
#else
    /** Local IPC session handle for this session. */
    RTLOCALIPCSESSION               hIpcSession;
    /** Request worker thread serving this session. */
    RTTHREAD                        hThread;
    /** Notification worker thread serving this session. */
    RTTHREAD                        hIpcPokeThread;
    /** Wakes the notification worker without blocking an IntNet delivery callback. */
    RTSEMEVENT                      hIpcPokeEvt;
    /** Whether the notification worker must stop. */
    bool volatile                   fIpcPokeStopping;
    /** Serializes complete outbound IPC frames. */
    RTSEMMUTEX                      hIpcIoMtx;
#endif
    /** Shared memory objects created for this session. */
    PINTNETR3SHMEM                  pShMemHead;
    /** The intnet interface handle to wait on. */
    INTNETIFHANDLE                  hIfWait;
    /** INTNETR3RECVSTATE_F_XXX state, updated as one atomic value to avoid lost wakeups. */
    uint32_t volatile               fRecvState;
} SUPDRVSESSION;


/** Request/reply scratch buffer large enough for all supported IntNet service requests. */
typedef union INTNETR3REQREPLY
{
    INTNETOPENREQ                   OpenReq;
    INTNETIFCLOSEREQ                IfCloseReq;
    INTNETIFGETBUFFERPTRSREQ        IfGetBufferPtrsReq;
    INTNETIFSETPROMISCUOUSMODEREQ   IfSetPromiscuousModeReq;
    INTNETIFSETMACADDRESSREQ        IfSetMacAddressReq;
    INTNETIFSETACTIVEREQ            IfSetActiveReq;
    INTNETIFSENDREQ                 IfSendReq;
    INTNETIFWAITREQ                 IfWaitReq;
    INTNETIFABORTWAITREQ            IfAbortWaitReq;
} INTNETR3REQREPLY;
typedef INTNETR3REQREPLY *PINTNETR3REQREPLY;


/** Transport-independent result of processing an IntNet service request. */
typedef struct INTNETR3REQRESULT
{
    /** Ring-3 buffer pointer for VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS, NULL otherwise. */
    PINTNETBUF                      pRing3Buf;
    /** Size of the request/reply payload to return for synchronous replies. */
    size_t                          cbReply;
    /** Whether the transport should send a synchronous reply. */
    bool                            fSendReply;
    /** Whether the transport should immediately emit a receive-available poke. */
    bool                            fSendPokeNow;
} INTNETR3REQRESULT;
typedef INTNETR3REQRESULT *PINTNETR3REQRESULT;


/*********************************************************************************************************************************
*   Internal Helpers                                                                                                             *
*********************************************************************************************************************************/
static DECLCALLBACK(void) intnetR3RecvAvail(INTNETIFHANDLE hIf, void *pvUser);


/** Atomically reserves @a cSlots slots without exceeding @a cMax. */
static bool intnetR3TryReserveSlots(uint32_t volatile *pcSlots, uint32_t cMax, uint32_t cSlots)
{
    AssertReturn(cSlots > 0, false);

    uint32_t cSlotsOld;
    do
    {
        cSlotsOld = ASMAtomicReadU32(pcSlots);
        if (   cSlotsOld > cMax
            || cSlots > cMax - cSlotsOld)
            return false;
    } while (!ASMAtomicCmpXchgU32(pcSlots, cSlotsOld + cSlots, cSlotsOld));
    return true;
}


/** Atomically reserves one slot without exceeding @a cMax. */
static bool intnetR3TryReserveSlot(uint32_t volatile *pcSlots, uint32_t cMax)
{
    return intnetR3TryReserveSlots(pcSlots, cMax, 1);
}


#ifdef VBOX_INTNET_TESTCASE_ONESHOT_SWITCH
/** Reads a positive testcase-only 32-bit limit override. */
static uint32_t intnetR3TestGetLimitU32(const char *pszVar, uint32_t uDefault)
{
    const char *pszValue = RTEnvGet(pszVar);
    if (pszValue && *pszValue)
    {
        uint32_t uValue = 0;
        if (   RT_SUCCESS(RTStrToUInt32Full(pszValue, 10, &uValue))
            && uValue > 0)
            return uValue;
    }
    return uDefault;
}


/** Reads a positive testcase-only 64-bit limit override. */
static uint64_t intnetR3TestGetLimitU64(const char *pszVar, uint64_t uDefault)
{
    const char *pszValue = RTEnvGet(pszVar);
    if (pszValue && *pszValue)
    {
        uint64_t uValue = 0;
        if (   RT_SUCCESS(RTStrToUInt64Full(pszValue, 10, &uValue))
            && uValue > 0)
            return uValue;
    }
    return uDefault;
}
#endif


/** Initializes service-wide resource accounting and limits. */
static void intnetR3InitLimits(PSUPDRVDEVEXT pDevExt)
{
    pDevExt->cRefs          = 0;
    pDevExt->cThreads       = 0;
    pDevExt->cbShMem        = 0;
    pDevExt->cMaxConnections = INTNETR3_MAX_CONNECTIONS;
    pDevExt->cMaxThreads    = INTNETR3_MAX_THREADS;
    pDevExt->cbMaxShMem     = INTNETR3_MAX_AGGREGATE_SHMEM_SIZE;
    pDevExt->cMsReadTimeout = INTNET_R3_IPC_FRAME_TIMEOUT_MS;
#ifdef VBOX_INTNET_TESTCASE_ONESHOT_SWITCH
    pDevExt->cMaxConnections = intnetR3TestGetLimitU32("VBOX_INTNET_R3_TEST_MAX_CONNECTIONS",
                                                       pDevExt->cMaxConnections);
    pDevExt->cMaxThreads = intnetR3TestGetLimitU32("VBOX_INTNET_R3_TEST_MAX_THREADS", pDevExt->cMaxThreads);
    pDevExt->cbMaxShMem = intnetR3TestGetLimitU64("VBOX_INTNET_R3_TEST_MAX_SHMEM", pDevExt->cbMaxShMem);
    pDevExt->cMsReadTimeout = intnetR3TestGetLimitU32("VBOX_INTNET_R3_TEST_READ_TIMEOUT_MS",
                                                      pDevExt->cMsReadTimeout);
#endif
}


/** Reserves service-wide shared-memory quota. */
static int intnetR3ReserveShMem(PSUPDRVDEVEXT pDevExt, size_t cb)
{
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);

    int rc = RTCritSectEnter(&pDevExt->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (   pDevExt->cbShMem <= pDevExt->cbMaxShMem
            && (uint64_t)cb <= pDevExt->cbMaxShMem - pDevExt->cbShMem)
            pDevExt->cbShMem += cb;
        else
            rc = VERR_OUT_OF_RESOURCES;
        int const rc2 = RTCritSectLeave(&pDevExt->CritSect);
        AssertStmt(RT_SUCCESS(rc2), rc = RT_SUCCESS(rc) ? rc2 : rc);
    }
    return rc;
}


/** Releases service-wide shared-memory quota. */
static void intnetR3ReleaseShMem(PSUPDRVDEVEXT pDevExt, size_t cb)
{
    int const rc = RTCritSectEnter(&pDevExt->CritSect);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        Assert(pDevExt->cbShMem >= cb);
        if (pDevExt->cbShMem >= cb)
            pDevExt->cbShMem -= cb;
        else
            pDevExt->cbShMem = pDevExt->cbMaxShMem;
        int const rc2 = RTCritSectLeave(&pDevExt->CritSect);
        AssertRC(rc2);
    }
}


/** Validates and bounds the allocation implied by an IntNet open request. */
static int intnetR3ValidateOpenBufferSizes(uint32_t cbSend, uint32_t cbRecv)
{
    uint64_t const cbMinRing     = sizeof(INTNETHDR) * UINT64_C(4);
    uint64_t const cbSendAligned = RT_ALIGN_64(RT_MAX((uint64_t)cbSend, cbMinRing), INTNETRINGBUF_ALIGNMENT);
    uint64_t const cbRecvAligned = RT_ALIGN_64(RT_MAX((uint64_t)cbRecv, cbMinRing), INTNETRINGBUF_ALIGNMENT);
    uint64_t const cbHdrAligned  = RT_ALIGN_64(sizeof(INTNETBUF), INTNETRINGBUF_ALIGNMENT);
    uint64_t const cbTotal       = cbHdrAligned + cbSendAligned + cbRecvAligned;
    return cbTotal <= INTNETR3_MAX_BUFFER_SIZE ? VINF_SUCCESS : VERR_OUT_OF_RANGE;
}


/**
 * Transport-agnostic IntNet request processor used by both XPC (Darwin) and local IPC (non-Darwin).
 *
 * This helper validates the header, dispatches the request to the existing IntNetR3/R0 handlers,
 * updates the in/out request buffer, and indicates whether a reply should be sent and/or a
 * transport-specific poke should be emitted immediately.
 *
 * @returns VBox status code for the request processing.
 * @param   pSession        The session context.
 * @param   uOperation      The VMMR0_DO_INTNET_* operation.
 * @param   pReqReply       In/out pointer to the request/reply union buffer.
 * @param   cbReqReply      Size of the request buffer.
 * @param   pResult         Transport-independent processing result.
 */
static int intnetR3ProcessRequestCore(PSUPDRVSESSION pSession, uint32_t uOperation,
                                      PINTNETR3REQREPLY pReqReply, size_t cbReqReply,
                                      PINTNETR3REQRESULT pResult)
{
    pResult->pRing3Buf   = NULL;
    pResult->cbReply     = 0;
    pResult->fSendReply  = true;
    pResult->fSendPokeNow = false;

    PSUPVMMR0REQHDR pReqHdr = (PSUPVMMR0REQHDR)pReqReply;
    AssertReturn(pReqHdr->u32Magic == SUPVMMR0REQHDR_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pReqHdr->cbReq == cbReqReply, VERR_INVALID_PARAMETER);

    int rc = VERR_INVALID_PARAMETER;

    switch (uOperation)
    {
        case VMMR0_DO_INTNET_OPEN:
            if (cbReqReply == sizeof(INTNETOPENREQ))
            {
                PINTNETOPENREQ p = &pReqReply->OpenReq;
                rc = intnetR3ValidateOpenBufferSizes(p->cbSend, p->cbRecv);
                if (RT_SUCCESS(rc))
                    rc = IntNetR3Open(pSession, &p->szNetwork[0], p->enmTrunkType, p->szTrunk,
                                      p->fFlags, p->cbSend, p->cbRecv, intnetR3RecvAvail, pSession, &p->hIf);
                pResult->cbReply = sizeof(*p);
            }
            break;

        case VMMR0_DO_INTNET_IF_CLOSE:
            if (cbReqReply == sizeof(INTNETIFCLOSEREQ))
            {
                PINTNETIFCLOSEREQ p = &pReqReply->IfCloseReq;
                rc = IntNetR0IfCloseReq(pSession, p);
                pResult->cbReply = sizeof(*p);
            }
            break;

        case VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS:
            if (cbReqReply == sizeof(INTNETIFGETBUFFERPTRSREQ))
            {
                PINTNETIFGETBUFFERPTRSREQ p = &pReqReply->IfGetBufferPtrsReq;
                rc = IntNetR0IfGetBufferPtrsReq(pSession, p);
                if (RT_SUCCESS(rc))
                    pResult->pRing3Buf = p->pRing3Buf;
                pResult->cbReply = sizeof(*p);
            }
            break;

        case VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE:
            if (cbReqReply == sizeof(INTNETIFSETPROMISCUOUSMODEREQ))
            {
                PINTNETIFSETPROMISCUOUSMODEREQ p = &pReqReply->IfSetPromiscuousModeReq;
                rc = IntNetR0IfSetPromiscuousModeReq(pSession, p);
                pResult->cbReply = sizeof(*p);
            }
            break;

        case VMMR0_DO_INTNET_IF_SET_MAC_ADDRESS:
            if (cbReqReply == sizeof(INTNETIFSETMACADDRESSREQ))
            {
                PINTNETIFSETMACADDRESSREQ p = &pReqReply->IfSetMacAddressReq;
                rc = IntNetR0IfSetMacAddressReq(pSession, p);
                pResult->cbReply = sizeof(*p);
            }
            break;

        case VMMR0_DO_INTNET_IF_SET_ACTIVE:
            if (cbReqReply == sizeof(INTNETIFSETACTIVEREQ))
            {
                PINTNETIFSETACTIVEREQ p = &pReqReply->IfSetActiveReq;
                rc = IntNetR0IfSetActiveReq(pSession, p);
                pResult->cbReply = sizeof(*p);
            }
            break;

        case VMMR0_DO_INTNET_IF_SEND:
            if (cbReqReply == sizeof(INTNETIFSENDREQ))
            {
                PINTNETIFSENDREQ p = &pReqReply->IfSendReq;
                rc = IntNetR0IfSendReq(pSession, p);
                pResult->cbReply = sizeof(*p);
            }
            break;

        case VMMR0_DO_INTNET_IF_WAIT:
            if (cbReqReply == sizeof(INTNETIFWAITREQ))
            {
                uint32_t fOld;
                uint32_t fNew;
                do
                {
                    fOld = ASMAtomicReadU32(&pSession->fRecvState);
                    if (fOld & INTNETR3RECVSTATE_F_NO_MORE_WAITS)
                        fNew = fOld & ~(INTNETR3RECVSTATE_F_WAITING | INTNETR3RECVSTATE_F_AVAILABLE);
                    else if (fOld & INTNETR3RECVSTATE_F_AVAILABLE)
                        fNew = fOld & ~(INTNETR3RECVSTATE_F_WAITING | INTNETR3RECVSTATE_F_AVAILABLE);
                    else
                        fNew = fOld | INTNETR3RECVSTATE_F_WAITING;
                } while (!ASMAtomicCmpXchgU32(&pSession->fRecvState, fNew, fOld));
                if (fOld & (INTNETR3RECVSTATE_F_AVAILABLE | INTNETR3RECVSTATE_F_NO_MORE_WAITS))
                    pResult->fSendPokeNow = true;
                pResult->fSendReply = false; /* async */
                rc = VINF_SUCCESS;
            }
            break;

        case VMMR0_DO_INTNET_IF_ABORT_WAIT:
            if (cbReqReply == sizeof(INTNETIFABORTWAITREQ))
            {
                PINTNETIFABORTWAITREQ p = &pReqReply->IfAbortWaitReq;
                RT_NOREF(p);
                uint32_t fOld;
                uint32_t fNew;
                do
                {
                    fOld = ASMAtomicReadU32(&pSession->fRecvState);
                    fNew = (fOld & ~(INTNETR3RECVSTATE_F_WAITING | INTNETR3RECVSTATE_F_AVAILABLE))
                         | INTNETR3RECVSTATE_F_NO_MORE_WAITS;
                } while (!ASMAtomicCmpXchgU32(&pSession->fRecvState, fNew, fOld));
                if (fOld & (INTNETR3RECVSTATE_F_WAITING | INTNETR3RECVSTATE_F_AVAILABLE))
                    pResult->fSendPokeNow = true;
                rc = VINF_SUCCESS;
                pResult->cbReply = sizeof(*p);
            }
            break;

        default:
            rc = VERR_INVALID_PARAMETER;
            break;
    }

    return rc;
}


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static SUPDRVDEVEXT g_DevExt;
#ifdef VBOX_INTNET_TESTCASE_ONESHOT_SWITCH
/** Local IPC listener cancelled when the testcase helper loses its last client. */
static RTLOCALIPCSERVER g_hTestIpcServer = NIL_RTLOCALIPCSERVER;
#endif


/** Releases one connection slot and wakes the one-shot listener after the last client. */
static uint32_t intnetR3ReleaseConnectionSlot(PSUPDRVDEVEXT pDevExt)
{
    uint32_t const cRefs = ASMAtomicDecU32(&pDevExt->cRefs);
#ifdef VBOX_INTNET_TESTCASE_ONESHOT_SWITCH
    if (!cRefs && g_hTestIpcServer != NIL_RTLOCALIPCSERVER)
        RTLocalIpcServerCancel(g_hTestIpcServer);
#endif
    return cRefs;
}


INTNETR3DECL(void *) SUPR0ObjRegister(PSUPDRVSESSION pSession, SUPDRVOBJTYPE enmType,
                                      PFNSUPDRVDESTRUCTOR pfnDestructor, void *pvUser1, void *pvUser2)
{
    RT_NOREF(enmType);

    PSUPDRVOBJ pObj = (PSUPDRVOBJ)RTMemAllocZ(sizeof(*pObj));
    if (!pObj)
        return NULL;
    pObj->cUsage = 1;
    pObj->pfnDestructor = pfnDestructor;
    pObj->pvUser1 = pvUser1;
    pObj->pvUser2 = pvUser2;

    /*
     * Insert the object and create the session usage record.
     */
    PSUPDRVUSAGE pUsage = (PSUPDRVUSAGE)RTMemAlloc(sizeof(*pUsage));
    if (!pUsage)
    {
        RTMemFree(pObj);
        return NULL;
    }

    PSUPDRVDEVEXT pDevExt = pSession->pDevExt;
    RTCritSectEnter(&pDevExt->CritSect);

    /* The object. */
    pObj->pNext         = pDevExt->pObjs;
    pDevExt->pObjs      = pObj;

    /* The session record. */
    pUsage->cUsage      = 1;
    pUsage->pObj        = pObj;
    pUsage->pNext       = pSession->pUsage;
    pSession->pUsage    = pUsage;

    RTCritSectLeave(&pDevExt->CritSect);
    return pObj;
}


INTNETR3DECL(int) SUPR0ObjAddRefEx(void *pvObj, PSUPDRVSESSION pSession, bool fNoBlocking)
{
    PSUPDRVDEVEXT   pDevExt     = pSession->pDevExt;
    PSUPDRVOBJ      pObj        = (PSUPDRVOBJ)pvObj;
    int             rc          = VINF_SUCCESS;
    PSUPDRVUSAGE    pUsage;

    RT_NOREF(fNoBlocking);

    RTCritSectEnter(&pDevExt->CritSect);

    /*
     * Reference the object.
     */
    ASMAtomicIncU32(&pObj->cUsage);

    /*
     * Look for the session record.
     */
    for (pUsage = pSession->pUsage; pUsage; pUsage = pUsage->pNext)
    {
        if (pUsage->pObj == pObj)
            break;
    }

    if (pUsage)
        pUsage->cUsage++;
    else
    {
        /* create a new session record. */
        pUsage = (PSUPDRVUSAGE)RTMemAlloc(sizeof(*pUsage));
        if (RT_LIKELY(pUsage))
        {
            pUsage->cUsage   = 1;
            pUsage->pObj     = pObj;
            pUsage->pNext    = pSession->pUsage;
            pSession->pUsage = pUsage;
        }
        else
        {
            ASMAtomicDecU32(&pObj->cUsage);
            rc = VERR_TRY_AGAIN;
        }
    }

    RTCritSectLeave(&pDevExt->CritSect);
    return rc;
}


INTNETR3DECL(int) SUPR0ObjAddRef(void *pvObj, PSUPDRVSESSION pSession)
{
    return SUPR0ObjAddRefEx(pvObj, pSession, false);
}


INTNETR3DECL(int) SUPR0ObjRelease(void *pvObj, PSUPDRVSESSION pSession)
{
    PSUPDRVDEVEXT       pDevExt     = pSession->pDevExt;
    PSUPDRVOBJ          pObj        = (PSUPDRVOBJ)pvObj;
    int                 rc          = VERR_INVALID_PARAMETER;
    PSUPDRVUSAGE        pUsage;
    PSUPDRVUSAGE        pUsagePrev;

    /*
     * Acquire the spinlock and look for the usage record.
     */
    RTCritSectEnter(&pDevExt->CritSect);

    for (pUsagePrev = NULL, pUsage = pSession->pUsage;
         pUsage;
         pUsagePrev = pUsage, pUsage = pUsage->pNext)
    {
        if (pUsage->pObj == pObj)
        {
            rc = VINF_SUCCESS;
            AssertMsg(pUsage->cUsage >= 1 && pObj->cUsage >= pUsage->cUsage, ("glob %d; sess %d\n", pObj->cUsage, pUsage->cUsage));
            if (pUsage->cUsage > 1)
            {
                pObj->cUsage--;
                pUsage->cUsage--;
            }
            else
            {
                /*
                 * Free the session record.
                 */
                if (pUsagePrev)
                    pUsagePrev->pNext = pUsage->pNext;
                else
                    pSession->pUsage = pUsage->pNext;
                RTMemFree(pUsage);

                /* What about the object? */
                if (pObj->cUsage > 1)
                    pObj->cUsage--;
                else
                {
                    /*
                     * Object is to be destroyed, unlink it.
                     */
                    rc = VINF_OBJECT_DESTROYED;
                    if (pDevExt->pObjs == pObj)
                        pDevExt->pObjs = pObj->pNext;
                    else
                    {
                        PSUPDRVOBJ pObjPrev;
                        for (pObjPrev = pDevExt->pObjs; pObjPrev; pObjPrev = pObjPrev->pNext)
                            if (pObjPrev->pNext == pObj)
                            {
                                pObjPrev->pNext = pObj->pNext;
                                break;
                            }
                        Assert(pObjPrev);
                    }
                }
            }
            break;
        }
    }

    RTCritSectLeave(&pDevExt->CritSect);

    /*
     * Call the destructor and free the object if required.
     */
    if (rc == VINF_OBJECT_DESTROYED)
    {
        if (pObj->pfnDestructor)
            pObj->pfnDestructor(pObj, pObj->pvUser1, pObj->pvUser2);
        RTMemFree(pObj);
    }

    return rc;
}


INTNETR3DECL(int) SUPR0ObjVerifyAccess(void *pvObj, PSUPDRVSESSION pSession, const char *pszObjName)
{
    RT_NOREF(pvObj, pSession, pszObjName);
    return VINF_SUCCESS;
}


/** Releases the backing resources of one tracked shared-memory allocation. */
static int intnetR3DestroyShMem(PINTNETR3SHMEM pShMem)
{
    int rc = VINF_SUCCESS;
#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
    if (pShMem->pv)
    {
        if (munmap(pShMem->pv, pShMem->cb) == 0)
            pShMem->pv = NULL;
        else
            rc = RTErrConvertFromErrno(errno);
    }
#else
    if (pShMem->pv)
    {
        int const rc2 = RTShMemUnmapRegion(pShMem->hShMem, pShMem->pv);
        if (RT_SUCCESS(rc2))
            pShMem->pv = NULL;
        else
            rc = rc2;
    }
    if (!pShMem->pv && pShMem->hShMem != NIL_RTSHMEM)
    {
        int const rc2 = RTShMemClose(pShMem->hShMem);
        if (RT_SUCCESS(rc2))
            pShMem->hShMem = NIL_RTSHMEM;
        else if (RT_SUCCESS(rc))
            rc = rc2;
    }
    if (!pShMem->pv && pShMem->hShMem == NIL_RTSHMEM && pShMem->szName[0])
    {
        int const rc2 = RTShMemDelete(pShMem->szName);
        if (   RT_SUCCESS(rc2)
            || rc2 == VERR_FILE_NOT_FOUND
            || rc2 == VERR_PATH_NOT_FOUND
            || rc2 == VERR_NOT_SUPPORTED)
            pShMem->szName[0] = '\0';
        else if (RT_SUCCESS(rc))
            rc = rc2;
    }
#endif
    return rc;
}


INTNETR3DECL(int) SUPR0MemAlloc(PSUPDRVSESSION pSession, uint32_t cb, PRTR0PTR ppvR0, PRTR3PTR ppvR3)
{
    AssertPtr(pSession);

    PINTNETR3SHMEM pShMem = (PINTNETR3SHMEM)RTMemAllocZ(sizeof(*pShMem));
    if (!pShMem)
        return VERR_NO_MEMORY;
    pShMem->cb = cb;

    int rc = intnetR3ReserveShMem(pSession->pDevExt, cb);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pShMem);
        return rc;
    }

#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
    /* The XPC transport requires a shareable mapping for the send/receive buffer. */
    pShMem->pv = mmap(NULL, cb, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if (pShMem->pv == MAP_FAILED)
    {
        pShMem->pv = NULL;
        rc = VERR_NO_MEMORY;
    }
#else
    pShMem->hShMem = NIL_RTSHMEM;

    rc = VERR_ALREADY_EXISTS;
    for (uint32_t iTry = 0; iTry < INTNETR3_SHMEM_CREATE_RETRIES && rc == VERR_ALREADY_EXISTS; iTry++)
    {
        RTUUID Uuid;
        char szUuid[32];
        rc = RTUuidCreate(&Uuid);
        if (RT_SUCCESS(rc))
        {
            size_t cchUuid = 0;
            rc = RTBase64EncodeEx(&Uuid, sizeof(Uuid), RTBASE64_FLAGS_NO_LINE_BREAKS,
                                  szUuid, sizeof(szUuid), &cchUuid);
            if (RT_SUCCESS(rc))
            {
                while (cchUuid > 0 && szUuid[cchUuid - 1] == '=')
                    szUuid[--cchUuid] = '\0';
                for (size_t off = 0; off < cchUuid; off++)
                    if (szUuid[off] == '+')
                        szUuid[off] = '-';
                    else if (szUuid[off] == '/')
                        szUuid[off] = '_';
            }
        }
        if (RT_SUCCESS(rc))
        {
            ssize_t const cch = RTStrPrintf2(pShMem->szName, sizeof(pShMem->szName), "vbi-%s", szUuid);
            if (cch <= 0 || (size_t)cch >= sizeof(pShMem->szName))
                rc = VERR_BUFFER_OVERFLOW;
        }
        if (RT_SUCCESS(rc))
            rc = RTShMemOpen(&pShMem->hShMem, pShMem->szName,
                             RTSHMEM_O_F_CREATE_EXCL | RTSHMEM_O_F_READWRITE, cb, 2 /*cMappingsHint*/);
    }
    if (RT_SUCCESS(rc))
        rc = RTShMemMapRegion(pShMem->hShMem, 0 /*off*/, cb, RTSHMEM_MAP_F_READ | RTSHMEM_MAP_F_WRITE, &pShMem->pv);
#endif
    if (RT_FAILURE(rc))
    {
#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
        if (pShMem->hShMem != NIL_RTSHMEM)
            intnetR3DestroyShMem(pShMem);
        else
            pShMem->szName[0] = '\0';
#endif
        if (   !pShMem->pv
#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
            && pShMem->hShMem == NIL_RTSHMEM
            && !pShMem->szName[0]
#endif
           )
        {
            intnetR3ReleaseShMem(pSession->pDevExt, cb);
            RTMemFree(pShMem);
        }
        else
        {
            pShMem->pNext = pSession->pShMemHead;
            pSession->pShMemHead = pShMem;
        }
        return rc;
    }

    pShMem->pvKey = pShMem->pv;
    pShMem->pNext = pSession->pShMemHead;
    pSession->pShMemHead = pShMem;
    *ppvR0 = (RTR0PTR)pShMem->pv;
    if (ppvR3)
        *ppvR3 = pShMem->pv;
    return VINF_SUCCESS;
}


INTNETR3DECL(int) SUPR0MemFree(PSUPDRVSESSION pSession, RTHCUINTPTR uPtr)
{
    AssertPtr(pSession);
    PINTNETR3SHMEM pPrev = NULL;
    PINTNETR3SHMEM pCur = pSession->pShMemHead;
    while (pCur)
    {
        if (pCur->pvKey == (void *)(uintptr_t)uPtr)
        {
            int const rc = intnetR3DestroyShMem(pCur);
            if (RT_FAILURE(rc))
                return rc;
            if (pPrev)
                pPrev->pNext = pCur->pNext;
            else
                pSession->pShMemHead = pCur->pNext;
            intnetR3ReleaseShMem(pSession->pDevExt, pCur->cb);
            RTMemFree(pCur);
            return VINF_SUCCESS;
        }
        pPrev = pCur;
        pCur = pCur->pNext;
    }
    return VERR_NOT_FOUND;
}


static PINTNETR3SHMEM intnetR3FindShMemByPtr(PSUPDRVSESSION pSession, void *pv)
{
    for (PINTNETR3SHMEM pCur = pSession->pShMemHead; pCur; pCur = pCur->pNext)
        if (pCur->pvKey == pv)
            return pCur;
    return NULL;
}


static void intnetR3FreeShMems(PSUPDRVSESSION pSession)
{
    PINTNETR3SHMEM pCur = pSession->pShMemHead;
    pSession->pShMemHead = NULL;
    while (pCur)
    {
        PINTNETR3SHMEM pNext = pCur->pNext;
        int const rc = intnetR3DestroyShMem(pCur);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            intnetR3ReleaseShMem(pSession->pDevExt, pCur->cb);
        RTMemFree(pCur);
        pCur = pNext;
    }
}


/**
 * Destroys the given internal network service session, freeing all allocated resources.
 *
 * @returns Reference count of the device extension.
 * @param   pSession        The session to destroy.
 */
static uint32_t intnetR3SessionDestroy(PSUPDRVSESSION pSession)
{
    PSUPDRVDEVEXT pDevExt = pSession->pDevExt;

    /* Prevent new notification work, but keep its synchronization objects and
       transport alive until interface destruction has quiesced callbacks. */
    ASMAtomicWriteU32(&pSession->fRecvState, INTNETR3RECVSTATE_F_NO_MORE_WAITS);
#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
    ASMAtomicWriteBool(&pSession->fIpcPokeStopping, true);
    if (pSession->hIpcSession != NIL_RTLOCALIPCSESSION)
        RTLocalIpcSessionCancel(pSession->hIpcSession);
#endif

    if (pSession->pUsage)
    {
        PSUPDRVUSAGE  pUsage;
        RTCritSectEnter(&pDevExt->CritSect);

        while ((pUsage = pSession->pUsage) != NULL)
        {
            PSUPDRVOBJ  pObj = pUsage->pObj;
            pSession->pUsage = pUsage->pNext;

            AssertMsg(pUsage->cUsage >= 1 && pObj->cUsage >= pUsage->cUsage, ("glob %d; sess %d\n", pObj->cUsage, pUsage->cUsage));
            if (pUsage->cUsage < pObj->cUsage)
            {
                pObj->cUsage -= pUsage->cUsage;
            }
            else
            {
                /* Destroy the object and free the record. */
                if (pDevExt->pObjs == pObj)
                    pDevExt->pObjs = pObj->pNext;
                else
                {
                    PSUPDRVOBJ pObjPrev;
                    for (pObjPrev = pDevExt->pObjs; pObjPrev; pObjPrev = pObjPrev->pNext)
                        if (pObjPrev->pNext == pObj)
                        {
                            pObjPrev->pNext = pObj->pNext;
                            break;
                        }
                    Assert(pObjPrev);
                }

                RTCritSectLeave(&pDevExt->CritSect);

                if (pObj->pfnDestructor)
                    pObj->pfnDestructor(pObj, pObj->pvUser1, pObj->pvUser2);
                RTMemFree(pObj);

                RTCritSectEnter(&pDevExt->CritSect);
            }

            /* free it and continue. */
            RTMemFree(pUsage);
        }

        RTCritSectLeave(&pDevExt->CritSect);
        AssertMsg(!pSession->pUsage, ("Some buster reregistered an object during destruction!\n"));
    }

    /* Interface destructors release their shared buffers via SUPR0MemFree. */
    intnetR3FreeShMems(pSession);
#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
    xpc_transaction_end();
    xpc_connection_set_context(pSession->hXpcCon, NULL);
    xpc_connection_cancel(pSession->hXpcCon);
    pSession->hXpcCon = NULL;
#else
    if (pSession->hIpcPokeEvt != NIL_RTSEMEVENT)
        RTSemEventSignal(pSession->hIpcPokeEvt);
    if (pSession->hIpcPokeThread != NIL_RTTHREAD)
    {
        int const rcThread = RTThreadWait(pSession->hIpcPokeThread, RT_INDEFINITE_WAIT, NULL /*prc*/);
        AssertRC(rcThread);
        pSession->hIpcPokeThread = NIL_RTTHREAD;
    }
    if (pSession->hIpcSession != NIL_RTLOCALIPCSESSION)
    {
        RTLocalIpcSessionClose(pSession->hIpcSession);
        pSession->hIpcSession = NIL_RTLOCALIPCSESSION;
    }
    if (pSession->hIpcPokeEvt != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pSession->hIpcPokeEvt);
        pSession->hIpcPokeEvt = NIL_RTSEMEVENT;
    }
    if (pSession->hIpcIoMtx != NIL_RTSEMMUTEX)
        RTSemMutexDestroy(pSession->hIpcIoMtx);
#endif
    uint32_t const cRefs = intnetR3ReleaseConnectionSlot(pDevExt);
    RTMemFree(pSession);
    return cRefs;
}


/**
 * Data available in th receive buffer callback.
 */
static DECLCALLBACK(void) intnetR3RecvAvail(INTNETIFHANDLE hIf, void *pvUser)
{
    RT_NOREF(hIf);
    PSUPDRVSESSION pSession = (PSUPDRVSESSION)pvUser;

    uint32_t fOld;
    uint32_t fNew;
    do
    {
        fOld = ASMAtomicReadU32(&pSession->fRecvState);
        if (fOld & INTNETR3RECVSTATE_F_NO_MORE_WAITS)
            fNew = fOld & ~(INTNETR3RECVSTATE_F_WAITING | INTNETR3RECVSTATE_F_AVAILABLE);
        else if (fOld & INTNETR3RECVSTATE_F_WAITING)
            fNew = fOld & ~(INTNETR3RECVSTATE_F_WAITING | INTNETR3RECVSTATE_F_AVAILABLE);
        else
            fNew = fOld | INTNETR3RECVSTATE_F_AVAILABLE;
    } while (!ASMAtomicCmpXchgU32(&pSession->fRecvState, fNew, fOld));

    if (fOld & INTNETR3RECVSTATE_F_WAITING)
    {
#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
        /* Send an empty message. */
        xpc_object_t hObjPoke = xpc_dictionary_create(NULL, NULL, 0);
        xpc_connection_send_message(pSession->hXpcCon, hObjPoke);
        xpc_release(hObjPoke);
#else
        int const rc = RTSemEventSignal(pSession->hIpcPokeEvt);
        AssertRC(rc);
#endif
    }
}


#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
static void intnetR3RequestProcess(xpc_connection_t hCon, xpc_object_t hObj, PSUPDRVSESSION pSession)
{
    uint64_t const uOperation = xpc_dictionary_get_uint64(hObj, "req-id");
    size_t cbReq = 0;
    const void *pvReq = xpc_dictionary_get_data(hObj, "req", &cbReq);
    if (cbReq > INTNET_R3_IPC_MAX_REQ)
    {
        xpc_connection_cancel(hCon);
        return;
    }

    xpc_object_t hObjReply = xpc_dictionary_create_reply(hObj);
    if (!hObjReply && uOperation != VMMR0_DO_INTNET_IF_WAIT)
    {
        xpc_connection_cancel(hCon);
        return;
    }

    INTNETR3REQREPLY ReqReply;
    INTNETR3REQRESULT Result;

    RT_ZERO(ReqReply);
    int rc = VERR_INVALID_PARAMETER;
    RT_ZERO(Result);
    Result.fSendReply = true;

    if (   pvReq
        && cbReq >= sizeof(SUPVMMR0REQHDR)
        && uOperation == (uint32_t)uOperation)
    {
        memcpy(&ReqReply, pvReq, RT_MIN(sizeof(ReqReply), cbReq));

        rc = intnetR3ProcessRequestCore(pSession, (uint32_t)uOperation, &ReqReply, cbReq, &Result);
    }

    if (Result.fSendPokeNow)
    {
        /* Send an empty message. */
        xpc_object_t hObjPoke = xpc_dictionary_create(NULL, NULL, 0);
        xpc_connection_send_message(pSession->hXpcCon, hObjPoke);
        xpc_release(hObjPoke);
    }

    if (!Result.fSendReply)
    {
        if (hObjReply)
            xpc_release(hObjReply);
        return;
    }

    if (!hObjReply)
    {
        xpc_connection_cancel(hCon);
        return;
    }
    if (RT_SUCCESS(rc) && Result.pRing3Buf)
    {
        /* This is special as we need to return a shared memory segment. */
        PINTNETR3SHMEM pShMem = intnetR3FindShMemByPtr(pSession, Result.pRing3Buf);
        if (pShMem)
        {
            xpc_object_t hObjShMem = xpc_shmem_create(pShMem->pv, pShMem->cb);
            if (hObjShMem)
            {
                xpc_dictionary_set_value(hObjReply, "buf-ptr", hObjShMem);
                xpc_release(hObjShMem);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_INTERNAL_ERROR;
    }

    xpc_dictionary_set_uint64(hObjReply, "rc", INTNET_R3_SVC_SET_RC(rc));
    size_t const cbXpcReply = uOperation == VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS ? 0 : Result.cbReply;
    xpc_dictionary_set_data(hObjReply, "reply", &ReqReply, cbXpcReply);
    xpc_connection_send_message(hCon, hObjReply);
    xpc_release(hObjReply);
}


static DECLCALLBACK(void) xpcConnHandler(xpc_connection_t hXpcCon)
{
    if (!intnetR3TryReserveSlot(&g_DevExt.cRefs, g_DevExt.cMaxConnections))
    {
        xpc_connection_cancel(hXpcCon);
        return;
    }

    PSUPDRVSESSION pSession = (PSUPDRVSESSION)RTMemAllocZ(sizeof(*pSession));
    if (!pSession)
    {
        intnetR3ReleaseConnectionSlot(&g_DevExt);
        xpc_connection_cancel(hXpcCon);
        return;
    }
    pSession->pDevExt = &g_DevExt;
    pSession->hXpcCon = hXpcCon;

    xpc_connection_set_event_handler(hXpcCon, ^(xpc_object_t hObj) {
        PSUPDRVSESSION pSessionCtx = (PSUPDRVSESSION)xpc_connection_get_context(hXpcCon);
        if (!pSessionCtx)
            return;

        xpc_type_t const hType = xpc_get_type(hObj);
        if (hType == XPC_TYPE_ERROR)
        {
            if (hObj == XPC_ERROR_CONNECTION_INVALID)
                intnetR3SessionDestroy(pSessionCtx);
            else if (hObj == XPC_ERROR_TERMINATION_IMMINENT)
            {
                PSUPDRVDEVEXT pDevExt = pSessionCtx->pDevExt;

                uint32_t cRefs = intnetR3SessionDestroy(pSessionCtx);
                if (!cRefs)
                {
                    /* Last one cleans up the global data. */
                    RTCritSectDelete(&pDevExt->CritSect);
                }
            }
        }
        else if (hType == XPC_TYPE_DICTIONARY)
            intnetR3RequestProcess(hXpcCon, hObj, pSessionCtx);
        else
            xpc_connection_cancel(hXpcCon);
    });

    xpc_connection_set_context(hXpcCon, pSession);
    xpc_transaction_begin();
    xpc_connection_resume(hXpcCon);
}

#else /* !RT_OS_DARWIN */

# if !defined(VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH)
static int intnetR3LocalIpcGetServiceName(char *pszService, size_t cbService)
{
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    const char *pszTestService = RTEnvGet("VBOX_INTNET_R3_SVC_NAME");
    if (pszTestService && *pszTestService)
        return RTStrCopy(pszService, cbService, pszTestService);
# endif

    char szUser[256];
    int rc = RTProcQueryUsername(RTProcSelf(), szUser, sizeof(szUser), NULL /*pcbUser*/);
    if (RT_SUCCESS(rc))
    {
        ssize_t const cch = RTStrPrintf2(pszService, cbService, "%s-%08RX32", INTNET_R3_SVC_NAME, RTStrHash1(szUser));
        if (cch < 0 || (size_t)cch >= cbService)
            rc = VERR_BUFFER_OVERFLOW;
    }
    return rc;
}
# endif


/** Verifies that a local IPC client belongs to the user running the switch. */
static int intnetR3LocalIpcVerifyPeer(RTLOCALIPCSESSION hSession)
{
    int rc = RTLocalIpcSessionVerifySameUser(hSession);
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    if (rc == VERR_NOT_SUPPORTED)
        rc = VINF_SUCCESS;
# endif
    return rc;
}


# if !defined(VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH)
static int intnetR3LocalIpcAcquireLock(PRTFILE phLock)
{
    *phLock = NIL_RTFILE;
#  ifdef RT_OS_WINDOWS
    /* The pipe namespace is scoped to the login session and the first pipe
       instance is created atomically, so a user-global file lock is wrong. */
    return VINF_SUCCESS;
#  else
    char szLock[RTPATH_MAX];
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    const char *pszTestLock = RTEnvGet("VBOX_INTNET_R3_SWITCH_LOCK_FILE");
    int rc;
    if (pszTestLock && *pszTestLock)
        rc = RTStrCopy(szLock, sizeof(szLock), pszTestLock);
    else
    {
        char szHome[RTPATH_MAX];
        rc = RTPathUserHome(szHome, sizeof(szHome));
        if (RT_SUCCESS(rc) && !RTPathStartsWithRoot(szHome))
            rc = VERR_INVALID_NAME;
        if (RT_SUCCESS(rc))
            rc = RTPathReal(szHome, szLock, sizeof(szLock));
    }
# else
    char szHome[RTPATH_MAX];
    int rc = RTPathUserHome(szHome, sizeof(szHome));
    if (RT_SUCCESS(rc) && !RTPathStartsWithRoot(szHome))
        rc = VERR_INVALID_NAME;
    if (RT_SUCCESS(rc))
        rc = RTPathReal(szHome, szLock, sizeof(szLock));
# endif
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    if (RT_SUCCESS(rc) && (!pszTestLock || !*pszTestLock))
# else
    if (RT_SUCCESS(rc))
# endif
        rc = RTPathAppend(szLock, sizeof(szLock), ".VirtualBox");
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    if (RT_SUCCESS(rc) && (!pszTestLock || !*pszTestLock))
# else
    if (RT_SUCCESS(rc))
# endif
        rc = RTDirCreateFullPath(szLock, 0700);
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    if (RT_SUCCESS(rc) && (!pszTestLock || !*pszTestLock))
# else
    if (RT_SUCCESS(rc))
# endif
    {
        RTFSOBJINFO ObjInfo;
        rc = RTPathQueryInfoEx(szLock, &ObjInfo, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
        if (RT_SUCCESS(rc))
        {
            if (   !RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode)
                || ObjInfo.Attr.u.Unix.uid != (RTUID)geteuid()
                || (ObjInfo.Attr.fMode & (RTFS_UNIX_IWUSR | RTFS_UNIX_IXUSR))
                   != (RTFS_UNIX_IWUSR | RTFS_UNIX_IXUSR)
                || (ObjInfo.Attr.fMode & (RTFS_UNIX_IWGRP | RTFS_UNIX_IWOTH)))
                rc = VERR_ACCESS_DENIED;
        }
    }
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    if (RT_SUCCESS(rc) && (!pszTestLock || !*pszTestLock))
# else
    if (RT_SUCCESS(rc))
# endif
        rc = RTPathAppend(szLock, sizeof(szLock), "VBoxIntNetSwitch.lock");
    if (RT_SUCCESS(rc))
        rc = RTFileOpen(phLock, szLock, RTFILE_O_READWRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_DENY_NONE
                                      | RTFILE_O_NO_SYMLINKS
                                      | (0600 << RTFILE_O_CREATE_MODE_SHIFT));
    if (RT_SUCCESS(rc))
    {
        rc = RTFileLock(*phLock, RTFILE_LOCK_WRITE | RTFILE_LOCK_IMMEDIATELY, 0 /*offLock*/, 1 /*cbLock*/);
        if (RT_FAILURE(rc))
        {
            RTFileClose(*phLock);
            *phLock = NIL_RTFILE;
        }
    }
    return rc;
#  endif
}
# endif

static int intnetR3LocalIpcSendHdrAndPayload(PSUPDRVSESSION pSession, PCINTNETR3IPCREPLYHDR pHdr,
                                             const void *pvReply, size_t cbReply,
                                             const char *pszShMemName, size_t cbShMemName)
{
    int rc = RTSemMutexRequest(pSession->hIpcIoMtx, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        rc = RTLocalIpcSessionWrite(pSession->hIpcSession, pHdr, sizeof(*pHdr));
        if (RT_SUCCESS(rc) && cbReply)
            rc = RTLocalIpcSessionWrite(pSession->hIpcSession, pvReply, cbReply);
        if (RT_SUCCESS(rc) && cbShMemName)
            rc = RTLocalIpcSessionWrite(pSession->hIpcSession, pszShMemName, cbShMemName);
        if (RT_SUCCESS(rc))
            rc = RTLocalIpcSessionFlush(pSession->hIpcSession);
        RTSemMutexRelease(pSession->hIpcIoMtx);
    }
    return rc;
}


static int intnetR3LocalIpcSendReply(PSUPDRVSESSION pSession, int rcReq, const void *pvReply, size_t cbReply,
                                     const char *pszShMemName, size_t cbShMem)
{
    size_t const cbShMemName = pszShMemName ? strlen(pszShMemName) + 1 : 0;
    AssertReturn(cbShMemName <= INTNET_R3_IPC_MAX_SHMEM_NAME, VERR_BUFFER_OVERFLOW);
    AssertReturn(cbReply <= INTNET_R3_IPC_MAX_REQ, VERR_BUFFER_OVERFLOW);
    INTNETR3IPCREPLYHDR Hdr;
    Hdr.u32Magic    = INTNET_R3_IPC_REPLY_MAGIC;
    Hdr.u16Version  = INTNET_R3_IPC_VERSION;
    Hdr.cbHdr       = sizeof(Hdr);
    Hdr.rc          = rcReq;
    Hdr.cbReply     = (uint32_t)cbReply;
    Hdr.cbShMemName = (uint32_t)cbShMemName;
    Hdr.cbShMem     = cbShMem;
    return intnetR3LocalIpcSendHdrAndPayload(pSession, &Hdr, pvReply, cbReply, pszShMemName, cbShMemName);
}


static int intnetR3LocalIpcSendPoke(PSUPDRVSESSION pSession)
{
# ifdef VBOX_INTNET_TESTCASE_ONESHOT_SWITCH
    /* Deterministically models a notification write blocked by a non-reading peer. */
    const char *pszTestBlockFile = RTEnvGet("VBOX_INTNET_R3_TEST_POKE_BLOCK_FILE");
    while (   pszTestBlockFile
           && *pszTestBlockFile
           && RTFileExists(pszTestBlockFile))
    {
        if (ASMAtomicReadBool(&pSession->fIpcPokeStopping))
            return VERR_CANCELLED;
        RTThreadSleep(1);
    }
# endif

    INTNETR3IPCREPLYHDR Hdr;
    Hdr.u32Magic    = INTNET_R3_IPC_POKE_MAGIC;
    Hdr.u16Version  = INTNET_R3_IPC_VERSION;
    Hdr.cbHdr       = sizeof(Hdr);
    Hdr.rc          = VINF_SUCCESS;
    Hdr.cbReply     = 0;
    Hdr.cbShMemName = 0;
    Hdr.cbShMem     = 0;
    return intnetR3LocalIpcSendHdrAndPayload(pSession, &Hdr, NULL, 0, NULL, 0);
}


/** Writes queued receive notifications outside the IntNet delivery callback. */
static DECLCALLBACK(int) intnetR3LocalIpcPokeThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    PSUPDRVSESSION pSession = (PSUPDRVSESSION)pvUser;
    PSUPDRVDEVEXT pDevExt = pSession->pDevExt;
    uint32_t const cMaxThreads = pDevExt->cMaxThreads;

    int rc = VINF_SUCCESS;
    for (;;)
    {
        rc = RTSemEventWait(pSession->hIpcPokeEvt, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
        {
            if (!ASMAtomicReadBool(&pSession->fIpcPokeStopping))
                RTLocalIpcSessionCancel(pSession->hIpcSession);
            break;
        }
        if (ASMAtomicReadBool(&pSession->fIpcPokeStopping))
        {
            rc = VINF_SUCCESS;
            break;
        }

        rc = intnetR3LocalIpcSendPoke(pSession);
        if (RT_FAILURE(rc))
        {
            RTLocalIpcSessionCancel(pSession->hIpcSession);
            break;
        }
    }

    if (ASMAtomicReadBool(&pSession->fIpcPokeStopping))
        rc = VINF_SUCCESS;
    uint32_t const cThreads = ASMAtomicDecU32(&pDevExt->cThreads);
    Assert(cThreads < cMaxThreads);
    RT_NOREF(cThreads);
    return rc;
}


/** Reads exactly @a cbToRead bytes without letting partial progress reset the deadline. */
static int intnetR3LocalIpcReadExact(RTLOCALIPCSESSION hSession, void *pvBuf, size_t cbToRead, uint64_t msDeadline)
{
    uint8_t *pbDst = (uint8_t *)pvBuf;
    while (cbToRead > 0)
    {
        size_t cbRead = 0;
        int rc = RTLocalIpcSessionReadNB(hSession, pbDst, cbToRead, &cbRead);
        if (rc == VINF_SUCCESS && cbRead > 0)
        {
            AssertReturn(cbRead <= cbToRead, VERR_INTERNAL_ERROR);
            pbDst    += cbRead;
            cbToRead -= cbRead;
            continue;
        }
        if (rc != VINF_SUCCESS && rc != VINF_TRY_AGAIN)
            return rc;

        uint64_t const msNow = RTTimeMilliTS();
        if (msNow >= msDeadline)
            return VERR_TIMEOUT;
        uint64_t const cMsLeft = msDeadline - msNow;
        rc = RTLocalIpcSessionWaitForData(hSession, (uint32_t)RT_MIN(cMsLeft, (uint64_t)UINT32_MAX));
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


static int intnetR3LocalIpcProcessRequest(PSUPDRVSESSION pSession, uint32_t uOperation, const void *pvReq, size_t cbReq,
                                          uint32_t *pcOpenIfs)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pvReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pcOpenIfs, VERR_INVALID_POINTER);
    AssertReturn(cbReq >= sizeof(SUPVMMR0REQHDR), VERR_INVALID_PARAMETER);

    INTNETR3REQREPLY ReqReply;
    INTNETR3REQRESULT Result;

    RT_ZERO(ReqReply);
    RT_ZERO(Result);
    Result.fSendReply = true;
    memcpy(&ReqReply, pvReq, RT_MIN(sizeof(ReqReply), cbReq));

    PSUPVMMR0REQHDR pReqHdr = (PSUPVMMR0REQHDR)&ReqReply;
    AssertReturn(pReqHdr->u32Magic == SUPVMMR0REQHDR_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pReqHdr->cbReq == cbReq, VERR_INVALID_PARAMETER);

    const char *pszShMemName = NULL;
    size_t cbShMem = 0;

    int rcReq = intnetR3ProcessRequestCore(pSession, uOperation, &ReqReply, cbReq, &Result);
    if (RT_SUCCESS(rcReq) && Result.pRing3Buf)
    {
        PINTNETR3SHMEM pShMem = intnetR3FindShMemByPtr(pSession, Result.pRing3Buf);
        if (pShMem)
        {
            pszShMemName = pShMem->szName;
            cbShMem = pShMem->cb;
        }
        else
            rcReq = VERR_INTERNAL_ERROR;
    }

    if (Result.fSendPokeNow)
    {
        int const rcPoke = intnetR3LocalIpcSendPoke(pSession);
        if (!Result.fSendReply)
            return rcPoke;
    }

    if (Result.fSendReply)
    {
        int const rc = intnetR3LocalIpcSendReply(pSession, rcReq, &ReqReply, Result.cbReply, pszShMemName, cbShMem);
        if (RT_SUCCESS(rc) && RT_SUCCESS(rcReq))
        {
            if (uOperation == VMMR0_DO_INTNET_OPEN)
                (*pcOpenIfs)++;
            else if (uOperation == VMMR0_DO_INTNET_IF_CLOSE && *pcOpenIfs > 0)
                (*pcOpenIfs)--;
        }
        return rc;
    }
    return rcReq;
}


static DECLCALLBACK(int) intnetR3LocalIpcSessionThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    PSUPDRVSESSION pSession = (PSUPDRVSESSION)pvUser;
    bool fPeerVerified = false;
    uint32_t cOpenIfs = 0;
    for (;;)
    {
        uint32_t const cMsFirstByte = cOpenIfs > 0 ? RT_INDEFINITE_WAIT : pSession->pDevExt->cMsReadTimeout;
        int rc = RTLocalIpcSessionWaitForData(pSession->hIpcSession, cMsFirstByte);
        if (RT_FAILURE(rc))
            break;

        uint64_t const msDeadline = RTTimeMilliTS() + pSession->pDevExt->cMsReadTimeout;
        INTNETR3IPCREQHDR Hdr;
        rc = intnetR3LocalIpcReadExact(pSession->hIpcSession, &Hdr, sizeof(Hdr), msDeadline);
        if (RT_FAILURE(rc))
            break;
        if (!fPeerVerified)
        {
            rc = intnetR3LocalIpcVerifyPeer(pSession->hIpcSession);
            if (RT_FAILURE(rc))
                break;
            fPeerVerified = true;
        }
        if (   Hdr.u32Magic != INTNET_R3_IPC_REQ_MAGIC
            || Hdr.u16Version != INTNET_R3_IPC_VERSION
            || Hdr.cbHdr != sizeof(Hdr)
            || Hdr.cbReq < sizeof(SUPVMMR0REQHDR)
            || Hdr.cbReq > INTNET_R3_IPC_MAX_REQ)
            break;

        void *pvReq = RTMemTmpAlloc(Hdr.cbReq);
        if (!pvReq)
            break;
        rc = intnetR3LocalIpcReadExact(pSession->hIpcSession, pvReq, Hdr.cbReq, msDeadline);
        if (RT_SUCCESS(rc))
            rc = intnetR3LocalIpcProcessRequest(pSession, Hdr.uOperation, pvReq, Hdr.cbReq, &cOpenIfs);
        RTMemTmpFree(pvReq);
        if (RT_FAILURE(rc))
            break;
    }
    PSUPDRVDEVEXT pDevExt = pSession->pDevExt;
    uint32_t const cMaxThreads = pDevExt->cMaxThreads;
    intnetR3SessionDestroy(pSession);
    uint32_t const cThreads = ASMAtomicDecU32(&pDevExt->cThreads);
    Assert(cThreads < cMaxThreads);
    RT_NOREF(cThreads);
    return VINF_SUCCESS;
}


# if !defined(VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH)
static int intnetR3LocalIpcRun(const char *pszService)
{
    RTFILE hLock = NIL_RTFILE;
    int rc = intnetR3LocalIpcAcquireLock(&hLock);
    if (RT_FAILURE(rc))
        return rc;

    RTLOCALIPCSERVER hServer = NIL_RTLOCALIPCSERVER;
    rc = RTLocalIpcServerCreate(&hServer, pszService, RTLOCALIPC_FLAGS_RESTRICT_TO_USER);
# ifndef RT_OS_WINDOWS
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcServerSetAccessMode(hServer, RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR);
# endif
    if (RT_FAILURE(rc))
    {
        if (hServer != NIL_RTLOCALIPCSERVER)
            RTLocalIpcServerDestroy(hServer);
        RTFileClose(hLock);
        return rc;
    }
# ifdef VBOX_INTNET_TESTCASE_ONESHOT_SWITCH
    g_hTestIpcServer = hServer;
# endif

    for (;;)
    {
        RTLOCALIPCSESSION hClient = NIL_RTLOCALIPCSESSION;
        rc = RTLocalIpcServerListen(hServer, &hClient);
# ifdef RT_OS_WINDOWS
        if (rc == VERR_TRY_AGAIN)
        {
            RTThreadSleep(10);
            continue;
        }
# endif
        if (RT_FAILURE(rc))
            break;
        if (!intnetR3TryReserveSlot(&g_DevExt.cRefs, g_DevExt.cMaxConnections))
        {
            RTLocalIpcSessionClose(hClient);
            RTThreadSleep(10);
            continue;
        }
        PSUPDRVSESSION pSession = (PSUPDRVSESSION)RTMemAllocZ(sizeof(*pSession));
        if (!pSession)
        {
            RTLocalIpcSessionClose(hClient);
            intnetR3ReleaseConnectionSlot(&g_DevExt);
            continue;
        }
        pSession->pDevExt     = &g_DevExt;
        pSession->hIpcSession = hClient;
        pSession->hThread     = NIL_RTTHREAD;
        pSession->hIpcPokeThread = NIL_RTTHREAD;
        pSession->hIpcPokeEvt = NIL_RTSEMEVENT;
        pSession->fIpcPokeStopping = false;
        pSession->hIpcIoMtx   = NIL_RTSEMMUTEX;
        rc = RTSemMutexCreate(&pSession->hIpcIoMtx);
        if (RT_SUCCESS(rc))
            rc = RTSemEventCreate(&pSession->hIpcPokeEvt);
        if (RT_FAILURE(rc))
        {
            intnetR3SessionDestroy(pSession);
            continue;
        }

        if (!intnetR3TryReserveSlots(&g_DevExt.cThreads, g_DevExt.cMaxThreads,
                                     INTNETR3_THREADS_PER_LOCALIPC_SESSION))
        {
            intnetR3SessionDestroy(pSession);
            RTThreadSleep(10);
            continue;
        }

        rc = RTThreadCreate(&pSession->hIpcPokeThread, intnetR3LocalIpcPokeThread, pSession, 0 /*cbStack*/,
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "IntNetPoke");
        if (RT_FAILURE(rc))
        {
            intnetR3SessionDestroy(pSession);
            ASMAtomicSubU32(&g_DevExt.cThreads, INTNETR3_THREADS_PER_LOCALIPC_SESSION);
            continue;
        }
        rc = RTThreadCreate(&pSession->hThread, intnetR3LocalIpcSessionThread, pSession, 0 /*cbStack*/,
                            RTTHREADTYPE_IO, 0 /*fFlags*/, "IntNetIpc");
        if (RT_FAILURE(rc))
        {
            intnetR3SessionDestroy(pSession);
            ASMAtomicDecU32(&g_DevExt.cThreads); /* Reserved request worker was never started. */
        }
    }
# ifdef VBOX_INTNET_TESTCASE_ONESHOT_SWITCH
    g_hTestIpcServer = NIL_RTLOCALIPCSERVER;
# endif
    RTLocalIpcServerDestroy(hServer);

    /*
     * The session workers are detached.  A fatal listener failure must not
     * let main tear down the IntNet globals while any worker still uses them.
     * Closing the listener prevents new sessions; existing clients can then
     * finish normally, however long that takes.
     */
    while (   ASMAtomicReadU32(&g_DevExt.cRefs) != 0
           || ASMAtomicReadU32(&g_DevExt.cThreads) != 0)
        RTThreadSleep(1);

    RTFileClose(hLock);
    return rc;
}
# endif /* !VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH */
#endif /* !RT_OS_DARWIN || VBOX_INTNET_TESTCASE_LOCALIPC */


#if !defined(VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH)
int main(int argc, char **argv)
{
#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_SUPLIB);
#else
    int rc = RTR3InitExe(argc, &argv, 0 /*fFlags*/);
#endif
    if (RT_SUCCESS(rc))
    {
        rc = IntNetR0Init();
        if (RT_SUCCESS(rc))
        {
            g_DevExt.pObjs = NULL;
            rc = RTCritSectInit(&g_DevExt.CritSect);
            if (RT_SUCCESS(rc))
            {
                intnetR3InitLimits(&g_DevExt);
#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
                xpc_main(xpcConnHandler); /* Never returns. */
                rc = VERR_INTERNAL_ERROR;
#else
                char szService[INTNET_R3_IPC_MAX_SERVICE_NAME];
                rc = intnetR3LocalIpcGetServiceName(szService, sizeof(szService));
                if (RT_SUCCESS(rc))
                    rc = intnetR3LocalIpcRun(szService); /* Normally never returns. */
# ifdef VBOX_INTNET_TESTCASE_ONESHOT_SWITCH
                if (rc == VERR_CANCELLED)
                    rc = VINF_SUCCESS;
# endif
#endif
                int const rc2 = RTCritSectDelete(&g_DevExt.CritSect);
                AssertRC(rc2);
                if (RT_SUCCESS(rc))
                    rc = rc2;
            }
            IntNetR0Term();
        }
    }

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTMsgInitFailure(rc);
}
#endif /* !VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH */
