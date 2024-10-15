/* $Id: IntNetIf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IntNetIfCtx - Abstract API implementing an IntNet connection using the R0 support driver or some R3 IPC variant.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
# if defined(RT_OS_DARWIN)
#  include <xpc/xpc.h> /* This needs to be here because it drags PVM in and cdefs.h needs to undefine it... */
# else
#  error "R3 internal networking not implemented for this platform yet!"
# endif
#endif

#include <iprt/cdefs.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>

#include <VBox/err.h>
#include <VBox/sup.h>
#include <VBox/intnetinline.h>
#include <VBox/vmm/pdmnetinline.h>

#include "IntNetIf.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/**
 * Internal network interface context instance data.
 */
typedef struct INTNETIFCTXINT
{
    /** The support driver session handle. */
    PSUPDRVSESSION                  pSupDrvSession;
    /** Interface handle. */
    INTNETIFHANDLE                  hIf;
    /** The internal network buffer. */
    PINTNETBUF                      pBuf;
#if defined (VBOX_WITH_INTNET_SERVICE_IN_R3)
    /** Flag whether this interface is using the internal network switch in userspace path. */
    bool                            fIntNetR3Svc;
    /** Receive event semaphore. */
    RTSEMEVENT                      hEvtRecv;
# if defined(RT_OS_DARWIN)
    /** XPC connection handle to the R3 internal network switch service. */
    xpc_connection_t                hXpcCon;
    /** Size of the communication buffer in bytes. */
    size_t                          cbBuf;
# endif
#endif
} INTNETIFCTXINT;
/** Pointer to the internal network interface context instance data. */
typedef INTNETIFCTXINT *PINTNETIFCTXINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Calls the internal networking switch service living in either R0 or in another R3 process.
 *
 * @returns VBox status code.
 * @param   pThis           The internal network driver instance data.
 * @param   uOperation      The operation to execute.
 * @param   pReqHdr         Pointer to the request header.
 */
static int intnetR3IfCallSvc(PINTNETIFCTXINT pThis, uint32_t uOperation, PSUPVMMR0REQHDR pReqHdr)
{
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
# if defined(RT_OS_DARWIN)
        size_t cbReq = pReqHdr->cbReq;
        xpc_object_t hObj = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_uint64(hObj, "req-id", uOperation);
        xpc_dictionary_set_data(hObj, "req", pReqHdr, pReqHdr->cbReq);
        xpc_object_t hObjReply = xpc_connection_send_message_with_reply_sync(pThis->hXpcCon, hObj);
        xpc_release(hObj);

        int rc = (int)xpc_dictionary_get_int64(hObjReply, "rc");

        size_t cbReply = 0;
        const void *pvData = xpc_dictionary_get_data(hObjReply, "reply", &cbReply);
        AssertRelease(cbReply == cbReq);
        memcpy(pReqHdr, pvData, cbReq);
        xpc_release(hObjReply);

        return rc;
# endif
    }
    else
#else
        RT_NOREF(pThis);
#endif
        return SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, uOperation, 0, pReqHdr);
}


#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
/**
 * Calls the internal networking switch service living in either R0 or in another R3 process.
 *
 * @returns VBox status code.
 * @param   pThis           The internal network driver instance data.
 * @param   uOperation      The operation to execute.
 * @param   pReqHdr         Pointer to the request header.
 */
static int intnetR3IfCallSvcAsync(PINTNETIFCTXINT pThis, uint32_t uOperation, PSUPVMMR0REQHDR pReqHdr)
{
    if (pThis->fIntNetR3Svc)
    {
        xpc_object_t hObj = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_uint64(hObj, "req-id", uOperation);
        xpc_dictionary_set_data(hObj, "req", pReqHdr, pReqHdr->cbReq);
        xpc_connection_send_message(pThis->hXpcCon, hObj);
        return VINF_SUCCESS;
    }
    else
        return SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, uOperation, 0, pReqHdr);
}
#endif


/**
 * Map the ring buffer pointer into this process R3 address space.
 *
 * @returns VBox status code.
 * @param   pThis           The internal network driver instance data.
 */
static int intnetR3IfMapBufferPointers(PINTNETIFCTXINT pThis)
{
    int rc = VINF_SUCCESS;

    INTNETIFGETBUFFERPTRSREQ GetBufferPtrsReq;
    GetBufferPtrsReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    GetBufferPtrsReq.Hdr.cbReq    = sizeof(GetBufferPtrsReq);
    GetBufferPtrsReq.pSession     = pThis->pSupDrvSession;
    GetBufferPtrsReq.hIf          = pThis->hIf;
    GetBufferPtrsReq.pRing3Buf    = NULL;
    GetBufferPtrsReq.pRing0Buf    = NIL_RTR0PTR;

#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
#if defined(RT_OS_DARWIN)
        xpc_object_t hObj = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_uint64(hObj, "req-id", VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS);
        xpc_dictionary_set_data(hObj, "req", &GetBufferPtrsReq, sizeof(GetBufferPtrsReq));
        xpc_object_t hObjReply = xpc_connection_send_message_with_reply_sync(pThis->hXpcCon, hObj);
        xpc_release(hObj);

        rc = (int)xpc_dictionary_get_int64(hObjReply, "rc");
        if (RT_SUCCESS(rc))
        {
            /* Get the shared memory object. */
            xpc_object_t hObjShMem = xpc_dictionary_get_value(hObjReply, "buf-ptr");
            size_t cbMem = xpc_shmem_map(hObjShMem, (void **)&pThis->pBuf);
            if (!cbMem)
                rc = VERR_NO_MEMORY;
            else
                pThis->cbBuf = cbMem;
        }
        xpc_release(hObjReply);
#endif
    }
    else
#endif
    {
        rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS, 0 /*u64Arg*/, &GetBufferPtrsReq.Hdr);
        if (RT_SUCCESS(rc))
        {
            AssertRelease(RT_VALID_PTR(GetBufferPtrsReq.pRing3Buf));
            pThis->pBuf = GetBufferPtrsReq.pRing3Buf;
        }
    }

    return rc;
}


static void intnetR3IfClose(PINTNETIFCTXINT pThis)
{
    if (pThis->hIf != INTNET_HANDLE_INVALID)
    {
        INTNETIFCLOSEREQ CloseReq;
        CloseReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        CloseReq.Hdr.cbReq    = sizeof(CloseReq);
        CloseReq.pSession     = pThis->pSupDrvSession;
        CloseReq.hIf          = pThis->hIf;

        pThis->hIf = INTNET_HANDLE_INVALID;
        int rc = intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_CLOSE, &CloseReq.Hdr);
        AssertRC(rc);
    }
}


DECLHIDDEN(int) IntNetR3IfCreate(PINTNETIFCTX phIfCtx, const char *pszNetwork)
{
    return IntNetR3IfCreateEx(phIfCtx, pszNetwork, kIntNetTrunkType_WhateverNone, "",
                              _128K /*cbSend*/, _256K /*cbRecv*/, 0 /*fFlags*/);
}


DECLHIDDEN(int) IntNetR3IfCreateEx(PINTNETIFCTX phIfCtx, const char *pszNetwork, INTNETTRUNKTYPE enmTrunkType,
                                   const char *pszTrunk, uint32_t cbSend, uint32_t cbRecv, uint32_t fFlags)
{
    AssertPtrReturn(phIfCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pszNetwork, VERR_INVALID_POINTER);
    AssertPtrReturn(pszTrunk, VERR_INVALID_POINTER);

    PSUPDRVSESSION pSession = NIL_RTR0PTR;
    int rc = SUPR3Init(&pSession);
    if (RT_SUCCESS(rc))
    {
        PINTNETIFCTXINT pThis = (PINTNETIFCTXINT)RTMemAllocZ(sizeof(*pThis));
        if (RT_LIKELY(pThis))
        {
            pThis->pSupDrvSession = pSession;
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
            pThis->hEvtRecv       = NIL_RTSEMEVENT;
#endif

            /* Driverless operation needs support for running the internal network switch using IPC. */
            if (SUPR3IsDriverless())
            {
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
# if defined(RT_OS_DARWIN)
                xpc_connection_t hXpcCon = xpc_connection_create(INTNET_R3_SVC_NAME, NULL);
                xpc_connection_set_event_handler(hXpcCon, ^(xpc_object_t hObj) {
                    if (xpc_get_type(hObj) == XPC_TYPE_ERROR)
                    {
                        /** @todo Error handling - reconnecting. */
                    }
                    else
                    {
                        /* Out of band messages should only come when there is something to receive. */
                        RTSemEventSignal(pThis->hEvtRecv);
                    }
                });

                xpc_connection_resume(hXpcCon);
                pThis->hXpcCon      = hXpcCon;
# endif
                pThis->fIntNetR3Svc = true;
                rc = RTSemEventCreate(&pThis->hEvtRecv);
#else
                rc = VERR_SUP_DRIVERLESS;
#endif
            }
            else
            {
                /* Need to load VMMR0.r0 containing the network switching code. */
                char szPathVMMR0[RTPATH_MAX];

                rc = RTPathExecDir(szPathVMMR0, sizeof(szPathVMMR0));
                if (RT_SUCCESS(rc))
                {
                    rc = RTPathAppend(szPathVMMR0, sizeof(szPathVMMR0), "VMMR0.r0");
                    if (RT_SUCCESS(rc))
                        rc = SUPR3LoadVMM(szPathVMMR0, /* :pErrInfo */ NULL);
                }
            }

            if (RT_SUCCESS(rc))
            {
                /* Open the interface. */
                INTNETOPENREQ OpenReq;
                RT_ZERO(OpenReq);

                OpenReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
                OpenReq.Hdr.cbReq    = sizeof(OpenReq);
                OpenReq.pSession     = pThis->pSupDrvSession;
                OpenReq.enmTrunkType = enmTrunkType;
                OpenReq.fFlags       = fFlags;
                OpenReq.cbSend       = cbSend;
                OpenReq.cbRecv       = cbRecv;
                OpenReq.hIf          = INTNET_HANDLE_INVALID;

                rc = RTStrCopy(OpenReq.szNetwork, sizeof(OpenReq.szNetwork), pszNetwork);
                if (RT_SUCCESS(rc))
                    rc = RTStrCopy(OpenReq.szTrunk, sizeof(OpenReq.szTrunk), pszTrunk);
                if (RT_SUCCESS(rc))
                {
                    rc = intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_OPEN, &OpenReq.Hdr);
                    if (RT_SUCCESS(rc))
                    {
                        pThis->hIf = OpenReq.hIf;

                        rc = intnetR3IfMapBufferPointers(pThis);
                        if (RT_SUCCESS(rc))
                        {
                            *phIfCtx = pThis;
                            return VINF_SUCCESS;
                        }
                    }

                    intnetR3IfClose(pThis);
                }
            }

#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
            if (pThis->fIntNetR3Svc)
            {
# if defined(RT_OS_DARWIN)
                if (pThis->hXpcCon)
                    xpc_connection_cancel(pThis->hXpcCon);
                pThis->hXpcCon = NULL;
# endif

                if (pThis->hEvtRecv != NIL_RTSEMEVENT)
                    RTSemEventDestroy(pThis->hEvtRecv);
            }
#endif

            RTMemFree(pThis);
        }

        SUPR3Term();
    }

    return rc;
}


DECLHIDDEN(int) IntNetR3IfDestroy(INTNETIFCTX hIfCtx)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    intnetR3IfClose(pThis);

#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
# if defined(RT_OS_DARWIN)
        /* Unmap the shared buffer. */
        munmap(pThis->pBuf, pThis->cbBuf);
        xpc_connection_cancel(pThis->hXpcCon);
        pThis->hXpcCon      = NULL;
# endif
        RTSemEventDestroy(pThis->hEvtRecv);
        pThis->fIntNetR3Svc = false;
    }
#endif

    RTMemFree(pThis);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) IntNetR3IfQueryBufferPtr(INTNETIFCTX hIfCtx, PINTNETBUF *ppIfBuf)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(ppIfBuf, VERR_INVALID_POINTER);

    *ppIfBuf = pThis->pBuf;
    return VINF_SUCCESS;
}


DECLHIDDEN(int) IntNetR3IfSetActive(INTNETIFCTX hIfCtx, bool fActive)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    INTNETIFSETACTIVEREQ Req;
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq    = sizeof(Req);
    Req.pSession     = pThis->pSupDrvSession;
    Req.hIf          = pThis->hIf;
    Req.fActive      = fActive;
    return intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_SET_ACTIVE, &Req.Hdr);
}


DECLHIDDEN(int) IntNetR3IfSetPromiscuous(INTNETIFCTX hIfCtx, bool fPromiscuous)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    INTNETIFSETPROMISCUOUSMODEREQ Req;
    Req.Hdr.u32Magic    = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq       = sizeof(Req);
    Req.pSession        = pThis->pSupDrvSession;
    Req.hIf             = pThis->hIf;
    Req.fPromiscuous    = fPromiscuous;
    return intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE, &Req.Hdr);
}


DECLHIDDEN(int) IntNetR3IfSend(INTNETIFCTX hIfCtx)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    INTNETIFSENDREQ Req;
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq    = sizeof(Req);
    Req.pSession     = pThis->pSupDrvSession;
    Req.hIf          = pThis->hIf;
    return intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_SEND, &Req.Hdr);
}


DECLHIDDEN(int) IntNetR3IfWait(INTNETIFCTX hIfCtx, uint32_t cMillies)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    INTNETIFWAITREQ WaitReq;
    WaitReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    WaitReq.Hdr.cbReq    = sizeof(WaitReq);
    WaitReq.pSession     = pThis->pSupDrvSession;
    WaitReq.hIf          = pThis->hIf;
    WaitReq.cMillies     = cMillies;
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
        /* Send an asynchronous message. */
        rc = intnetR3IfCallSvcAsync(pThis, VMMR0_DO_INTNET_IF_WAIT, &WaitReq.Hdr);
        if (RT_SUCCESS(rc))
        {
            /* Wait on the receive semaphore. */
            rc = RTSemEventWait(pThis->hEvtRecv, cMillies);
        }
    }
    else
#endif
        rc = intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_WAIT, &WaitReq.Hdr);

    return rc;
}


DECLHIDDEN(int) IntNetR3IfWaitAbort(INTNETIFCTX hIfCtx)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    INTNETIFABORTWAITREQ AbortWaitReq;
    AbortWaitReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    AbortWaitReq.Hdr.cbReq    = sizeof(AbortWaitReq);
    AbortWaitReq.pSession     = pThis->pSupDrvSession;
    AbortWaitReq.hIf          = pThis->hIf;
    AbortWaitReq.fNoMoreWaits = true;
    return intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_ABORT_WAIT, &AbortWaitReq.Hdr);
}


DECLHIDDEN(int) IntNetR3IfPumpPkts(INTNETIFCTX hIfCtx, PFNINPUT pfnInput, void *pvUser,
                                      PFNINPUTGSO pfnInputGso, void *pvUserGso)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pfnInput, VERR_INVALID_POINTER);

    int rc;
    for (;;)
    {
        rc = IntNetR3IfWait(hIfCtx, RT_INDEFINITE_WAIT);
        if (RT_SUCCESS(rc) || rc == VERR_INTERRUPTED || rc == VERR_TIMEOUT)
        {
            PCINTNETHDR pHdr = IntNetRingGetNextFrameToRead(&pThis->pBuf->Recv);
            while (pHdr)
            {
                const uint8_t u8Type = pHdr->u8Type;
                void *pvSegFrame;
                uint32_t cbSegFrame;

                if (u8Type == INTNETHDR_TYPE_FRAME)
                {
                    pvSegFrame = IntNetHdrGetFramePtr(pHdr, pThis->pBuf);
                    cbSegFrame = pHdr->cbFrame;

                    /* pass the frame to the user callback */
                    pfnInput(pvUser, pvSegFrame, cbSegFrame);
                }
                else if (u8Type == INTNETHDR_TYPE_GSO)
                {
                    size_t cbGso = pHdr->cbFrame;
                    size_t cbFrame = cbGso - sizeof(PDMNETWORKGSO);

                    PCPDMNETWORKGSO pcGso = IntNetHdrGetGsoContext(pHdr, pThis->pBuf);
                    if (PDMNetGsoIsValid(pcGso, cbGso, cbFrame))
                    {
                        if (pfnInputGso != NULL)
                        {
                            /* pass the frame to the user GSO input callback if set */
                            pfnInputGso(pvUserGso, pcGso, (uint32_t)cbFrame);
                        }
                        else
                        {
                            const uint32_t cSegs = PDMNetGsoCalcSegmentCount(pcGso, cbFrame);
                            for (uint32_t i = 0; i < cSegs; ++i)
                            {
                                uint8_t abHdrScratch[256];
                                pvSegFrame = PDMNetGsoCarveSegmentQD(pcGso, (uint8_t *)(pcGso + 1), cbFrame,
                                                                     abHdrScratch,
                                                                     i, cSegs,
                                                                     &cbSegFrame);

                                /* pass carved frames to the user input callback */
                                pfnInput(pvUser, pvSegFrame, (uint32_t)cbSegFrame);
                            }
                        }
                    }
                }

                /* advance to the next input frame */
                IntNetRingSkipFrame(&pThis->pBuf->Recv);
                pHdr = IntNetRingGetNextFrameToRead(&pThis->pBuf->Recv);
            }
        }
        else
            break;
    }
    return rc;
}


DECLHIDDEN(int) IntNetR3IfQueryOutputFrame(INTNETIFCTX hIfCtx, uint32_t cbFrame, PINTNETFRAME pFrame)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    return IntNetRingAllocateFrame(&pThis->pBuf->Send, cbFrame, &pFrame->pHdr, &pFrame->pvFrame);
}


DECLHIDDEN(int) IntNetR3IfOutputFrameCommit(INTNETIFCTX hIfCtx, PCINTNETFRAME pFrame)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    IntNetRingCommitFrame(&pThis->pBuf->Send, pFrame->pHdr);
    return IntNetR3IfSend(hIfCtx);
}
