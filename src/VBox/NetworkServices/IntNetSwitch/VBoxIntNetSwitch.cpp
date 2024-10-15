/* $Id: VBoxIntNetSwitch.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Internal networking - Wrapper for the R0 network service.
 *
 * This is a bit hackish as we're mixing context here, however it is
 * very useful when making changes to the internal networking service.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define IN_INTNET_TESTCASE
#define IN_INTNET_R3
#include "IntNetSwitchInternal.h"

#include <VBox/err.h>
#include <VBox/vmm/vmm.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>

#include <xpc/xpc.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


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
    /** Number of references to this service. */
    uint32_t volatile               cRefs;
    /** Critical section to serialize the initialization, usage counting and objects. */
    RTCRITSECT                      CritSect;
    /** List of registered objects. Protected by the spinlock. */
    PSUPDRVOBJ volatile             pObjs;
} SUPDRVDEVEXT;
typedef SUPDRVDEVEXT *PSUPDRVDEVEXT;


/**
 * Per session data.
 * This is mainly for memory tracking.
 */
typedef struct SUPDRVSESSION
{
    PSUPDRVDEVEXT                   pDevExt;
    /** List of generic usage records. (protected by SUPDRVDEVEXT::CritSect) */
    PSUPDRVUSAGE volatile           pUsage;
    /** The XPC connection handle for this session. */
    xpc_connection_t                hXpcCon;
    /** The intnet interface handle to wait on. */
    INTNETIFHANDLE                  hIfWait;
    /** Flag whether a receive wait was initiated. */
    bool volatile                   fRecvWait;
    /** Flag whether there is something to receive. */
    bool volatile                   fRecvAvail;
} SUPDRVSESSION;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static SUPDRVDEVEXT g_DevExt;


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


INTNETR3DECL(int) SUPR0MemAlloc(PSUPDRVSESSION pSession, uint32_t cb, PRTR0PTR ppvR0, PRTR3PTR ppvR3)
{
    RT_NOREF(pSession);

    /*
     * This is used to allocate and map the send/receive buffers into the callers process space, meaning
     * we have to mmap it with the shareable attribute.
     */
    void *pv = mmap(NULL, cb, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if (pv == MAP_FAILED)
        return VERR_NO_MEMORY;

    *ppvR0 = (RTR0PTR)pv;
    if (ppvR3)
        *ppvR3 = pv;
    return VINF_SUCCESS;
}


INTNETR3DECL(int) SUPR0MemFree(PSUPDRVSESSION pSession, RTHCUINTPTR uPtr)
{
    RT_NOREF(pSession);

    PINTNETBUF pBuf = (PINTNETBUF)uPtr; /// @todo Hack hack hack!
    munmap((void *)uPtr, pBuf->cbBuf);
    return VINF_SUCCESS;
}


/**
 * Destroys the given internal network XPC connection session freeing all allocated resources.
 *
 * @returns Reference count of the device extension..
 * @param   pSession        The ession to destroy.
 */
static uint32_t intnetR3SessionDestroy(PSUPDRVSESSION pSession)
{
    PSUPDRVDEVEXT pDevExt = pSession->pDevExt;
    uint32_t cRefs = ASMAtomicDecU32(&pDevExt->cRefs);
    xpc_transaction_end();
    xpc_connection_set_context(pSession->hXpcCon, NULL);
    xpc_connection_cancel(pSession->hXpcCon);
    pSession->hXpcCon = NULL;

    ASMAtomicXchgBool(&pSession->fRecvAvail, true);

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

    if (ASMAtomicXchgBool(&pSession->fRecvWait, false))
    {
        /* Send an empty message. */
        xpc_object_t hObjPoke = xpc_dictionary_create(NULL, NULL, 0);
        xpc_connection_send_message(pSession->hXpcCon, hObjPoke);
        xpc_release(hObjPoke);
    }
    else
        ASMAtomicXchgBool(&pSession->fRecvAvail, true);
}


static void intnetR3RequestProcess(xpc_connection_t hCon, xpc_object_t hObj, PSUPDRVSESSION pSession)
{
    int rc = VINF_SUCCESS;
    uint64_t iReq = xpc_dictionary_get_uint64(hObj, "req-id");
    size_t cbReq = 0;
    const void *pvReq = xpc_dictionary_get_data(hObj, "req", &cbReq);
    union
    {
        INTNETOPENREQ                 OpenReq;
        INTNETIFCLOSEREQ              IfCloseReq;
        INTNETIFGETBUFFERPTRSREQ      IfGetBufferPtrsReq;
        INTNETIFSETPROMISCUOUSMODEREQ IfSetPromiscuousModeReq;
        INTNETIFSETMACADDRESSREQ      IfSetMacAddressReq;
        INTNETIFSETACTIVEREQ          IfSetActiveReq;
        INTNETIFSENDREQ               IfSendReq;
        INTNETIFWAITREQ               IfWaitReq;
        INTNETIFABORTWAITREQ          IfAbortWaitReq;
    } ReqReply;

    memcpy(&ReqReply, pvReq, RT_MIN(sizeof(ReqReply), cbReq));
    size_t cbReply = 0;

    if (pvReq)
    {
        switch (iReq)
        {
            case VMMR0_DO_INTNET_OPEN:
            {
                if (cbReq == sizeof(INTNETOPENREQ))
                {
                    rc = IntNetR3Open(pSession, &ReqReply.OpenReq.szNetwork[0], ReqReply.OpenReq.enmTrunkType, ReqReply.OpenReq.szTrunk,
                                      ReqReply.OpenReq.fFlags, ReqReply.OpenReq.cbSend, ReqReply.OpenReq.cbRecv,
                                      intnetR3RecvAvail, pSession, &ReqReply.OpenReq.hIf);
                    cbReply = sizeof(INTNETOPENREQ);
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            case VMMR0_DO_INTNET_IF_CLOSE:
            {
                if (cbReq == sizeof(INTNETIFCLOSEREQ))
                {
                    rc = IntNetR0IfCloseReq(pSession, &ReqReply.IfCloseReq);
                    cbReply = sizeof(INTNETIFCLOSEREQ);
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            case VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS:
            {
                if (cbReq == sizeof(INTNETIFGETBUFFERPTRSREQ))
                {
                    rc = IntNetR0IfGetBufferPtrsReq(pSession, &ReqReply.IfGetBufferPtrsReq);
                    /* This is special as we need to return a shared memory segment. */
                    xpc_object_t hObjReply = xpc_dictionary_create_reply(hObj);
                    xpc_object_t hObjShMem = xpc_shmem_create(ReqReply.IfGetBufferPtrsReq.pRing3Buf, ReqReply.IfGetBufferPtrsReq.pRing3Buf->cbBuf);
                    if (hObjShMem)
                    {
                        xpc_dictionary_set_value(hObjReply, "buf-ptr", hObjShMem);
                        xpc_release(hObjShMem);
                    }
                    else
                        rc = VERR_NO_MEMORY;

                    xpc_dictionary_set_uint64(hObjReply, "rc", INTNET_R3_SVC_SET_RC(rc));
                    xpc_connection_send_message(hCon, hObjReply);
                    xpc_release(hObjReply);
                    return;
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            case VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE:
            {
                if (cbReq == sizeof(INTNETIFSETPROMISCUOUSMODEREQ))
                {
                    rc = IntNetR0IfSetPromiscuousModeReq(pSession, &ReqReply.IfSetPromiscuousModeReq);
                    cbReply = sizeof(INTNETIFSETPROMISCUOUSMODEREQ);
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            case VMMR0_DO_INTNET_IF_SET_MAC_ADDRESS:
            {
                if (cbReq == sizeof(INTNETIFSETMACADDRESSREQ))
                {
                    rc = IntNetR0IfSetMacAddressReq(pSession, &ReqReply.IfSetMacAddressReq);
                    cbReply = sizeof(INTNETIFSETMACADDRESSREQ);
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            case VMMR0_DO_INTNET_IF_SET_ACTIVE:
            {
                if (cbReq == sizeof(INTNETIFSETACTIVEREQ))
                {
                    rc = IntNetR0IfSetActiveReq(pSession, &ReqReply.IfSetActiveReq);
                    cbReply = sizeof(INTNETIFSETACTIVEREQ);
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            case VMMR0_DO_INTNET_IF_SEND:
            {
                if (cbReq == sizeof(INTNETIFSENDREQ))
                {
                    rc = IntNetR0IfSendReq(pSession, &ReqReply.IfSendReq);
                    cbReply = sizeof(INTNETIFSENDREQ);
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            case VMMR0_DO_INTNET_IF_WAIT:
            {
                if (cbReq == sizeof(INTNETIFWAITREQ))
                {
                    ASMAtomicXchgBool(&pSession->fRecvWait, true);
                    if (ASMAtomicXchgBool(&pSession->fRecvAvail, false))
                    {
                        ASMAtomicXchgBool(&pSession->fRecvWait, false);

                        /* Send an empty message. */
                        xpc_object_t hObjPoke = xpc_dictionary_create(NULL, NULL, 0);
                        xpc_connection_send_message(pSession->hXpcCon, hObjPoke);
                        xpc_release(hObjPoke);
                    }
                    return;
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            case VMMR0_DO_INTNET_IF_ABORT_WAIT:
            {
                if (cbReq == sizeof(INTNETIFABORTWAITREQ))
                {
                    ASMAtomicXchgBool(&pSession->fRecvWait, false);
                    if (ASMAtomicXchgBool(&pSession->fRecvAvail, false))
                    {
                        /* Send an empty message. */
                        xpc_object_t hObjPoke = xpc_dictionary_create(NULL, NULL, 0);
                        xpc_connection_send_message(pSession->hXpcCon, hObjPoke);
                        xpc_release(hObjPoke);
                    }
                    cbReply = sizeof(INTNETIFABORTWAITREQ);
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            default:
                rc = VERR_INVALID_PARAMETER;
        }
    }

    xpc_object_t hObjReply = xpc_dictionary_create_reply(hObj);
    xpc_dictionary_set_uint64(hObjReply, "rc", INTNET_R3_SVC_SET_RC(rc));
    xpc_dictionary_set_data(hObjReply, "reply", &ReqReply, cbReply);
    xpc_connection_send_message(hCon, hObjReply);
    xpc_release(hObjReply);
}


static DECLCALLBACK(void) xpcConnHandler(xpc_connection_t hXpcCon)
{
    xpc_connection_set_event_handler(hXpcCon, ^(xpc_object_t hObj) {
        PSUPDRVSESSION pSession = (PSUPDRVSESSION)xpc_connection_get_context(hXpcCon);

        if (xpc_get_type(hObj) == XPC_TYPE_ERROR)
        {
            if (hObj == XPC_ERROR_CONNECTION_INVALID)
                intnetR3SessionDestroy(pSession);
            else if (hObj == XPC_ERROR_TERMINATION_IMMINENT)
            {
                PSUPDRVDEVEXT pDevExt = pSession->pDevExt;

                uint32_t cRefs = intnetR3SessionDestroy(pSession);
                if (!cRefs)
                {
                    /* Last one cleans up the global data. */
                    RTCritSectDelete(&pDevExt->CritSect);
                }
            }
        }
        else
            intnetR3RequestProcess(hXpcCon, hObj, pSession);
    });

    PSUPDRVSESSION pSession = (PSUPDRVSESSION)RTMemAllocZ(sizeof(*pSession));
    if (pSession)
    {
        pSession->pDevExt = &g_DevExt;
        pSession->hXpcCon = hXpcCon;

        xpc_connection_set_context(hXpcCon, pSession);
        xpc_connection_resume(hXpcCon);
        xpc_transaction_begin();
        ASMAtomicIncU32(&g_DevExt.cRefs);
    }
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_SUPLIB);
    if (RT_SUCCESS(rc))
    {
        IntNetR0Init();

        g_DevExt.pObjs = NULL;
        rc = RTCritSectInit(&g_DevExt.CritSect);
        if (RT_SUCCESS(rc))
            xpc_main(xpcConnHandler); /* Never returns. */

        exit(EXIT_FAILURE);
    }

    return RTMsgInitFailure(rc);
}

