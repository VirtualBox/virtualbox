/* $Id: DrvIntNet.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvIntNet - Internal network transport driver.
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
#define LOG_GROUP LOG_GROUP_DRV_INTNET
#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
# include <xpc/xpc.h> /* This needs to be here because it drags PVM in and cdefs.h needs to undefine it... */
#endif
#include <iprt/cdefs.h>

#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/intnet.h>
#include <VBox/intnetinline.h>
#include <VBox/vmm/vmm.h>
#include <VBox/sup.h>
#include <VBox/err.h>

#include <VBox/param.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/memcache.h>
#include <iprt/net.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#if defined(RT_OS_DARWIN) && defined(IN_RING3)
# include <iprt/system.h>
#endif

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if 0
/** Enables the ring-0 part. */
#define VBOX_WITH_DRVINTNET_IN_R0
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The state of the asynchronous thread.
 */
typedef enum RECVSTATE
{
    /** The thread is suspended. */
    RECVSTATE_SUSPENDED = 1,
    /** The thread is running. */
    RECVSTATE_RUNNING,
    /** The thread must (/has) terminate. */
    RECVSTATE_TERMINATE,
    /** The usual 32-bit type blowup. */
    RECVSTATE_32BIT_HACK = 0x7fffffff
} RECVSTATE;

/**
 * Internal networking driver instance data.
 *
 * @implements  PDMINETWORKUP
 */
typedef struct DRVINTNET
{
    /** The network interface. */
    PDMINETWORKUP                   INetworkUpR3;
    /** The network interface. */
    R3PTRTYPE(PPDMINETWORKDOWN)     pIAboveNet;
    /** The network config interface.
     * Can (in theory at least) be NULL. */
    R3PTRTYPE(PPDMINETWORKCONFIG)   pIAboveConfigR3;
    /** Pointer to the driver instance (ring-3). */
    PPDMDRVINSR3                    pDrvInsR3;
    /** Pointer to the communication buffer (ring-3). */
    R3PTRTYPE(PINTNETBUF)           pBufR3;
#ifdef VBOX_WITH_DRVINTNET_IN_R0
    /** Ring-3 base interface for the ring-0 context. */
    PDMIBASER0                      IBaseR0;
    /** Ring-3 base interface for the raw-mode context. */
    PDMIBASERC                      IBaseRC;
    RTR3PTR                         R3PtrAlignment;

    /** The network interface for the ring-0 context. */
    PDMINETWORKUPR0                 INetworkUpR0;
    /** Pointer to the driver instance (ring-0). */
    PPDMDRVINSR0                    pDrvInsR0;
    /** Pointer to the communication buffer (ring-0). */
    R0PTRTYPE(PINTNETBUF)           pBufR0;

    /** The network interface for the raw-mode context. */
    PDMINETWORKUPRC                 INetworkUpRC;
    /** Pointer to the driver instance. */
    PPDMDRVINSRC                    pDrvInsRC;
    RTRCPTR                         RCPtrAlignment;
#endif

    /** The transmit lock. */
    PDMCRITSECT                     XmitLock;
    /** Interface handle. */
    INTNETIFHANDLE                  hIf;
    /** The receive thread state. */
    RECVSTATE volatile              enmRecvState;
    /** The receive thread. */
    RTTHREAD                        hRecvThread;
    /** The event semaphore that the receive thread waits on.  */
    RTSEMEVENT                      hRecvEvt;
    /** The transmit thread.  */
    PPDMTHREAD                      pXmitThread;
    /** The event semaphore that the transmit thread waits on.  */
    SUPSEMEVENT                     hXmitEvt;
    /** The support driver session handle. */
    PSUPDRVSESSION                  pSupDrvSession;
    /** Scatter/gather descriptor cache. */
    RTMEMCACHE                      hSgCache;
    /** Set if the link is down.
     * When the link is down all incoming packets will be dropped. */
    bool volatile                   fLinkDown;
    /** Set when the xmit thread has been signalled. (atomic) */
    bool volatile                   fXmitSignalled;
    /** Set if the transmit thread the one busy transmitting. */
    bool volatile                   fXmitOnXmitThread;
    /** The xmit thread should process the ring ASAP. */
    bool                            fXmitProcessRing;
    /** Set if data transmission should start immediately and deactivate
     * as late as possible. */
    bool                            fActivateEarlyDeactivateLate;
    /** Padding. */
    bool                            afReserved[HC_ARCH_BITS == 64 ? 3 : 3];
    /** Scratch space for holding the ring-0 scatter / gather descriptor.
     * The PDMSCATTERGATHER::fFlags member is used to indicate whether it is in
     * use or not.  Always accessed while owning the XmitLock. */
    union
    {
        PDMSCATTERGATHER            Sg;
        uint8_t                     padding[8 * sizeof(RTUINTPTR)];
    }                               u;
    /** The network name. */
    char                            szNetwork[INTNET_MAX_NETWORK_NAME];

    /** Number of GSO packets sent. */
    STAMCOUNTER                     StatSentGso;
    /** Number of GSO packets received. */
    STAMCOUNTER                     StatReceivedGso;
    /** Number of packets send from ring-0. */
    STAMCOUNTER                     StatSentR0;
    /** The number of times we've had to wake up the xmit thread to continue the
     *  ring-0 job. */
    STAMCOUNTER                     StatXmitWakeupR0;
    /** The number of times we've had to wake up the xmit thread to continue the
     *  ring-3 job. */
    STAMCOUNTER                     StatXmitWakeupR3;
    /** The times the xmit thread has been told to process the ring. */
    STAMCOUNTER                     StatXmitProcessRing;
#ifdef VBOX_WITH_STATISTICS
    /** Profiling packet transmit runs. */
    STAMPROFILE                     StatTransmit;
    /** Profiling packet receive runs. */
    STAMPROFILEADV                  StatReceive;
#endif /* VBOX_WITH_STATISTICS */
#ifdef LOG_ENABLED
    /** The nano ts of the last transfer. */
    uint64_t                        u64LastTransferTS;
    /** The nano ts of the last receive. */
    uint64_t                        u64LastReceiveTS;
#endif
#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    /** XPC connection handle to the R3 internal network switch service. */
    xpc_connection_t                hXpcCon;
    /** Flag whether the R3 internal network service is being used. */
    bool                            fIntNetR3Svc;
    /** Size of the communication buffer in bytes. */
    size_t                          cbBuf;
#endif
} DRVINTNET;
AssertCompileMemberAlignment(DRVINTNET, XmitLock, 8);
AssertCompileMemberAlignment(DRVINTNET, StatSentGso, 8);
/** Pointer to instance data of the internal networking driver. */
typedef DRVINTNET *PDRVINTNET;

/**
 * Config value to flag translation structure.
 */
typedef struct DRVINTNETFLAG
{
    /** The value. */
    const char *pszChoice;
    /** The corresponding flag. */
    uint32_t    fFlag;
} DRVINTNETFLAG;
/** Pointer to a const flag value translation. */
typedef DRVINTNETFLAG const *PCDRVINTNETFLAG;


#ifdef IN_RING3


/**
 * Calls the internal networking switch service living in either R0 or in another R3 process.
 *
 * @returns VBox status code.
 * @param   pThis           The internal network driver instance data.
 * @param   uOperation      The operation to execute.
 * @param   pvArg           Pointer to the argument data.
 * @param   cbArg           Size of the argument data in bytes.
 */
static int drvR3IntNetCallSvc(PDRVINTNET pThis, uint32_t uOperation, void *pvArg, unsigned cbArg)
{
#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
        xpc_object_t hObj = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_uint64(hObj, "req-id", uOperation);
        xpc_dictionary_set_data(hObj, "req", pvArg, cbArg);
        xpc_object_t hObjReply = xpc_connection_send_message_with_reply_sync(pThis->hXpcCon, hObj);
        xpc_release(hObj);

        uint64_t u64Rc = xpc_dictionary_get_uint64(hObjReply, "rc");
        if (INTNET_R3_SVC_IS_VALID_RC(u64Rc))
        {
            size_t cbReply = 0;
            const void *pvData = xpc_dictionary_get_data(hObjReply, "reply", &cbReply);
            AssertRelease(cbReply == cbArg);
            memcpy(pvArg, pvData, cbArg);
            xpc_release(hObjReply);

            return INTNET_R3_SVC_GET_RC(u64Rc);
        }

        xpc_release(hObjReply);
        return VERR_INVALID_STATE;
    }
    else
#endif
        return PDMDrvHlpSUPCallVMMR0Ex(pThis->pDrvInsR3, uOperation, pvArg, cbArg);
}


#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
/**
 * Calls the internal networking switch service living in either R0 or in another R3 process.
 *
 * @returns VBox status code.
 * @param   pThis           The internal network driver instance data.
 * @param   uOperation      The operation to execute.
 * @param   pvArg           Pointer to the argument data.
 * @param   cbArg           Size of the argument data in bytes.
 */
static int drvR3IntNetCallSvcAsync(PDRVINTNET pThis, uint32_t uOperation, void *pvArg, unsigned cbArg)
{
    if (pThis->fIntNetR3Svc)
    {
        xpc_object_t hObj = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_uint64(hObj, "req-id", uOperation);
        xpc_dictionary_set_data(hObj, "req", pvArg, cbArg);
        xpc_connection_send_message(pThis->hXpcCon, hObj);
        return VINF_SUCCESS;
    }
    else
        return PDMDrvHlpSUPCallVMMR0Ex(pThis->pDrvInsR3, uOperation, pvArg, cbArg);
}
#endif


/**
 * Map the ring buffer pointer into this process R3 address space.
 *
 * @returns VBox status code.
 * @param   pThis           The internal network driver instance data.
 */
static int drvR3IntNetMapBufferPointers(PDRVINTNET pThis)
{
    int rc = VINF_SUCCESS;

    INTNETIFGETBUFFERPTRSREQ GetBufferPtrsReq;
    GetBufferPtrsReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    GetBufferPtrsReq.Hdr.cbReq = sizeof(GetBufferPtrsReq);
    GetBufferPtrsReq.pSession = NIL_RTR0PTR;
    GetBufferPtrsReq.hIf = pThis->hIf;
    GetBufferPtrsReq.pRing3Buf = NULL;
    GetBufferPtrsReq.pRing0Buf = NIL_RTR0PTR;

#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
        xpc_object_t hObj = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_uint64(hObj, "req-id", VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS);
        xpc_dictionary_set_data(hObj, "req", &GetBufferPtrsReq, sizeof(GetBufferPtrsReq));
        xpc_object_t hObjReply = xpc_connection_send_message_with_reply_sync(pThis->hXpcCon, hObj);
        xpc_release(hObj);

        uint64_t u64Rc = xpc_dictionary_get_uint64(hObjReply, "rc");
        if (INTNET_R3_SVC_IS_VALID_RC(u64Rc))
            rc = INTNET_R3_SVC_GET_RC(u64Rc);
        else
            rc = VERR_INVALID_STATE;

        if (RT_SUCCESS(rc))
        {
            /* Get the shared memory object. */
            xpc_object_t hObjShMem = xpc_dictionary_get_value(hObjReply, "buf-ptr");
            size_t cbMem = xpc_shmem_map(hObjShMem, (void **)&pThis->pBufR3);
            if (!cbMem)
                rc = VERR_NO_MEMORY;
            else
                pThis->cbBuf = cbMem;
        }

        xpc_release(hObjReply);
    }
    else
#endif
    {
        rc = PDMDrvHlpSUPCallVMMR0Ex(pThis->pDrvInsR3, VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS, &GetBufferPtrsReq, sizeof(GetBufferPtrsReq));
        if (RT_SUCCESS(rc))
        {
            AssertRelease(RT_VALID_PTR(GetBufferPtrsReq.pRing3Buf));
            pThis->pBufR3 = GetBufferPtrsReq.pRing3Buf;
#ifdef VBOX_WITH_DRVINTNET_IN_R0
            pThis->pBufR0 = GetBufferPtrsReq.pRing0Buf;
#endif
        }
    }

    return rc;
}


/**
 * Updates the MAC address on the kernel side.
 *
 * @returns VBox status code.
 * @param   pThis       The driver instance.
 */
static int drvR3IntNetUpdateMacAddress(PDRVINTNET pThis)
{
    if (!pThis->pIAboveConfigR3)
        return VINF_SUCCESS;

    INTNETIFSETMACADDRESSREQ SetMacAddressReq;
    SetMacAddressReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    SetMacAddressReq.Hdr.cbReq = sizeof(SetMacAddressReq);
    SetMacAddressReq.pSession = NIL_RTR0PTR;
    SetMacAddressReq.hIf = pThis->hIf;
    int rc = pThis->pIAboveConfigR3->pfnGetMac(pThis->pIAboveConfigR3, &SetMacAddressReq.Mac);
    if (RT_SUCCESS(rc))
        rc = drvR3IntNetCallSvc(pThis, VMMR0_DO_INTNET_IF_SET_MAC_ADDRESS,
                                &SetMacAddressReq, sizeof(SetMacAddressReq));

    Log(("drvR3IntNetUpdateMacAddress: %.*Rhxs rc=%Rrc\n", sizeof(SetMacAddressReq.Mac), &SetMacAddressReq.Mac, rc));
    return rc;
}


/**
 * Sets the kernel interface active or inactive.
 *
 * Worker for poweron, poweroff, suspend and resume.
 *
 * @returns VBox status code.
 * @param   pThis       The driver instance.
 * @param   fActive     The new state.
 */
static int drvR3IntNetSetActive(PDRVINTNET pThis, bool fActive)
{
    if (!pThis->pIAboveConfigR3)
        return VINF_SUCCESS;

    INTNETIFSETACTIVEREQ SetActiveReq;
    SetActiveReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    SetActiveReq.Hdr.cbReq = sizeof(SetActiveReq);
    SetActiveReq.pSession = NIL_RTR0PTR;
    SetActiveReq.hIf = pThis->hIf;
    SetActiveReq.fActive = fActive;
    int rc = drvR3IntNetCallSvc(pThis, VMMR0_DO_INTNET_IF_SET_ACTIVE,
                                &SetActiveReq, sizeof(SetActiveReq));

    Log(("drvR3IntNetSetActive: fActive=%d rc=%Rrc\n", fActive, rc));
    AssertRC(rc);
    return rc;
}

#endif /* IN_RING3 */

/* -=-=-=-=- PDMINETWORKUP -=-=-=-=- */

#ifndef IN_RING3
/**
 * Helper for signalling the xmit thread.
 *
 * @returns VERR_TRY_AGAIN (convenience).
 * @param   pThis               The instance data..
 */
DECLINLINE(int) drvR0IntNetSignalXmit(PDRVINTNET pThis)
{
    /// @todo if (!ASMAtomicXchgBool(&pThis->fXmitSignalled, true)) - needs careful optimizing.
    {
        int rc = SUPSemEventSignal(pThis->pSupDrvSession, pThis->hXmitEvt);
        AssertRC(rc);
        STAM_REL_COUNTER_INC(&pThis->CTX_SUFF(StatXmitWakeup));
    }
    return VERR_TRY_AGAIN;
}
#endif /* !IN_RING3 */


/**
 * Helper for processing the ring-0 consumer side of the xmit ring.
 *
 * The caller MUST own the xmit lock.
 *
 * @returns Status code from IntNetR0IfSend, except for VERR_TRY_AGAIN.
 * @param   pThis               The instance data..
 */
DECLINLINE(int) drvIntNetProcessXmit(PDRVINTNET pThis)
{
    Assert(PDMDrvHlpCritSectIsOwner(pThis->CTX_SUFF(pDrvIns), &pThis->XmitLock));

#ifdef IN_RING3
    INTNETIFSENDREQ SendReq;
    SendReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    SendReq.Hdr.cbReq = sizeof(SendReq);
    SendReq.pSession = NIL_RTR0PTR;
    SendReq.hIf = pThis->hIf;
    int rc = drvR3IntNetCallSvc(pThis, VMMR0_DO_INTNET_IF_SEND, &SendReq, sizeof(SendReq));
#else
    int rc = IntNetR0IfSend(pThis->hIf, pThis->pSupDrvSession);
    if (rc == VERR_TRY_AGAIN)
    {
        ASMAtomicUoWriteBool(&pThis->fXmitProcessRing, true);
        drvR0IntNetSignalXmit(pThis);
        rc = VINF_SUCCESS;
    }
#endif
    return rc;
}



/**
 * @interface_method_impl{PDMINETWORKUP,pfnBeginXmit}
 */
PDMBOTHCBDECL(int) drvIntNetUp_BeginXmit(PPDMINETWORKUP pInterface, bool fOnWorkerThread)
{
    PDRVINTNET pThis = RT_FROM_MEMBER(pInterface, DRVINTNET, CTX_SUFF(INetworkUp));
#ifndef IN_RING3
    Assert(!fOnWorkerThread);
#endif

    int rc = PDMDrvHlpCritSectTryEnter(pThis->CTX_SUFF(pDrvIns), &pThis->XmitLock);
    if (RT_SUCCESS(rc))
    {
        if (fOnWorkerThread)
        {
            ASMAtomicUoWriteBool(&pThis->fXmitOnXmitThread, true);
            ASMAtomicWriteBool(&pThis->fXmitSignalled, false);
        }
    }
    else if (rc == VERR_SEM_BUSY)
    {
        /** @todo Does this actually make sense if the other dude is an EMT and so
         *        forth?  I seriously think this is ring-0 only...
         * We might end up waking up the xmit thread unnecessarily here, even when in
         * ring-0... This needs some more thought and optimizations when the ring-0 bits
         * are working. */
#ifdef IN_RING3
        if (    !fOnWorkerThread
            /*&&  !ASMAtomicUoReadBool(&pThis->fXmitOnXmitThread)
            &&  ASMAtomicCmpXchgBool(&pThis->fXmitSignalled, true, false)*/)
        {
            rc = SUPSemEventSignal(pThis->pSupDrvSession, pThis->hXmitEvt);
            AssertRC(rc);
        }
        rc = VERR_TRY_AGAIN;
#else  /* IN_RING0 */
        rc = drvR0IntNetSignalXmit(pThis);
#endif /* IN_RING0 */
    }
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnAllocBuf}
 */
PDMBOTHCBDECL(int) drvIntNetUp_AllocBuf(PPDMINETWORKUP pInterface, size_t cbMin,
                                                   PCPDMNETWORKGSO pGso,  PPPDMSCATTERGATHER ppSgBuf)
{
    PDRVINTNET  pThis = RT_FROM_MEMBER(pInterface, DRVINTNET, CTX_SUFF(INetworkUp));
    int         rc    = VINF_SUCCESS;
    Assert(cbMin < UINT32_MAX / 2);
    Assert(PDMDrvHlpCritSectIsOwner(pThis->CTX_SUFF(pDrvIns), &pThis->XmitLock));

    /*
     * Allocate a S/G descriptor.
     * This shouldn't normally fail as the NICs usually won't allocate more
     * than one buffer at a time and the SG gets freed on sending.
     */
#ifdef IN_RING3
    PPDMSCATTERGATHER pSgBuf = (PPDMSCATTERGATHER)RTMemCacheAlloc(pThis->hSgCache);
    if (!pSgBuf)
        return VERR_NO_MEMORY;
#else
    PPDMSCATTERGATHER pSgBuf = &pThis->u.Sg;
    if (RT_UNLIKELY(pSgBuf->fFlags != 0))
        return drvR0IntNetSignalXmit(pThis);
#endif

    /*
     * Allocate room in the ring buffer.
     *
     * In ring-3 we may have to process the xmit ring before there is
     * sufficient buffer space since we might have stacked up a few frames to the
     * trunk while in ring-0.  (There is not point of doing this in ring-0.)
     */
    PINTNETHDR pHdr = NULL;             /* gcc silliness */
    if (pGso)
        rc = IntNetRingAllocateGsoFrame(&pThis->CTX_SUFF(pBuf)->Send, (uint32_t)cbMin, pGso,
                                        &pHdr, &pSgBuf->aSegs[0].pvSeg);
    else
        rc = IntNetRingAllocateFrame(&pThis->CTX_SUFF(pBuf)->Send, (uint32_t)cbMin,
                                     &pHdr, &pSgBuf->aSegs[0].pvSeg);
#ifdef IN_RING3
    if (    RT_FAILURE(rc)
        &&  pThis->CTX_SUFF(pBuf)->cbSend >= cbMin * 2 + sizeof(INTNETHDR))
    {
        drvIntNetProcessXmit(pThis);
        if (pGso)
            rc = IntNetRingAllocateGsoFrame(&pThis->CTX_SUFF(pBuf)->Send, (uint32_t)cbMin, pGso,
                                            &pHdr, &pSgBuf->aSegs[0].pvSeg);
        else
            rc = IntNetRingAllocateFrame(&pThis->CTX_SUFF(pBuf)->Send, (uint32_t)cbMin,
                                         &pHdr, &pSgBuf->aSegs[0].pvSeg);
    }
#endif
    if (RT_SUCCESS(rc))
    {
        /*
         * Set up the S/G descriptor and return successfully.
         */
        pSgBuf->fFlags          = PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1;
        pSgBuf->cbUsed          = 0;
        pSgBuf->cbAvailable     = cbMin;
        pSgBuf->pvAllocator     = pHdr;
        pSgBuf->pvUser          = pGso ? (PPDMNETWORKGSO)pSgBuf->aSegs[0].pvSeg - 1 : NULL;
        pSgBuf->cSegs           = 1;
        pSgBuf->aSegs[0].cbSeg  = cbMin;

        *ppSgBuf = pSgBuf;
        return VINF_SUCCESS;
    }

#ifdef IN_RING3
    /*
     * If the above fails, then we're really out of space.  There are nobody
     * competing with us here because of the xmit lock.
     */
    rc = VERR_NO_MEMORY;
    RTMemCacheFree(pThis->hSgCache, pSgBuf);

#else  /* IN_RING0 */
    /*
     * If the request is reasonable, kick the xmit thread and tell it to
     * process the xmit ring ASAP.
     */
    if (pThis->CTX_SUFF(pBuf)->cbSend >= cbMin * 2 + sizeof(INTNETHDR))
    {
        pThis->fXmitProcessRing = true;
        rc = drvR0IntNetSignalXmit(pThis);
    }
    else
        rc = VERR_NO_MEMORY;
    pSgBuf->fFlags = 0;
#endif /* IN_RING0 */
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnFreeBuf}
 */
PDMBOTHCBDECL(int) drvIntNetUp_FreeBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf)
{
    PDRVINTNET  pThis = RT_FROM_MEMBER(pInterface, DRVINTNET, CTX_SUFF(INetworkUp));
    PINTNETHDR  pHdr  = (PINTNETHDR)pSgBuf->pvAllocator;
#ifdef IN_RING0
    Assert(pSgBuf == &pThis->u.Sg);
#endif
    Assert(pSgBuf->fFlags == (PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1));
    Assert(pSgBuf->cbUsed <= pSgBuf->cbAvailable);
    Assert(   pHdr->u8Type == INTNETHDR_TYPE_FRAME
           || pHdr->u8Type == INTNETHDR_TYPE_GSO);
    Assert(PDMDrvHlpCritSectIsOwner(pThis->CTX_SUFF(pDrvIns), &pThis->XmitLock));

    /** @todo LATER: try unalloc the frame. */
    pHdr->u8Type = INTNETHDR_TYPE_PADDING;
    IntNetRingCommitFrame(&pThis->CTX_SUFF(pBuf)->Send, pHdr);

#ifdef IN_RING3
    RTMemCacheFree(pThis->hSgCache, pSgBuf);
#else
    pSgBuf->fFlags = 0;
#endif
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSendBuf}
 */
PDMBOTHCBDECL(int) drvIntNetUp_SendBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    PDRVINTNET  pThis = RT_FROM_MEMBER(pInterface, DRVINTNET, CTX_SUFF(INetworkUp));
    STAM_PROFILE_START(&pThis->StatTransmit, a);
    RT_NOREF_PV(fOnWorkerThread);

    AssertPtr(pSgBuf);
    Assert(pSgBuf->fFlags == (PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1));
    Assert(pSgBuf->cbUsed <= pSgBuf->cbAvailable);
    Assert(PDMDrvHlpCritSectIsOwner(pThis->CTX_SUFF(pDrvIns), &pThis->XmitLock));

    if (pSgBuf->pvUser)
        STAM_COUNTER_INC(&pThis->StatSentGso);

    /*
     * Commit the frame and push it thru the switch.
     */
    PINTNETHDR pHdr = (PINTNETHDR)pSgBuf->pvAllocator;
    IntNetRingCommitFrameEx(&pThis->CTX_SUFF(pBuf)->Send, pHdr, pSgBuf->cbUsed);
    int rc = drvIntNetProcessXmit(pThis);
    STAM_PROFILE_STOP(&pThis->StatTransmit, a);

    /*
     * Free the descriptor and return.
     */
#ifdef IN_RING3
    RTMemCacheFree(pThis->hSgCache, pSgBuf);
#else
    STAM_REL_COUNTER_INC(&pThis->StatSentR0);
    pSgBuf->fFlags = 0;
#endif
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnEndXmit}
 */
PDMBOTHCBDECL(void) drvIntNetUp_EndXmit(PPDMINETWORKUP pInterface)
{
    PDRVINTNET pThis = RT_FROM_MEMBER(pInterface, DRVINTNET, CTX_SUFF(INetworkUp));
    ASMAtomicUoWriteBool(&pThis->fXmitOnXmitThread, false);
    PDMDrvHlpCritSectLeave(pThis->CTX_SUFF(pDrvIns), &pThis->XmitLock);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSetPromiscuousMode}
 */
PDMBOTHCBDECL(void) drvIntNetUp_SetPromiscuousMode(PPDMINETWORKUP pInterface, bool fPromiscuous)
{
    PDRVINTNET pThis = RT_FROM_MEMBER(pInterface, DRVINTNET, CTX_SUFF(INetworkUp));

#ifdef IN_RING3
    INTNETIFSETPROMISCUOUSMODEREQ Req;
    Req.Hdr.u32Magic    = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq       = sizeof(Req);
    Req.pSession        = NIL_RTR0PTR;
    Req.hIf             = pThis->hIf;
    Req.fPromiscuous    = fPromiscuous;
    int rc = drvR3IntNetCallSvc(pThis, VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE, &Req, sizeof(Req));
#else  /* IN_RING0 */
    int rc = IntNetR0IfSetPromiscuousMode(pThis->hIf, pThis->pSupDrvSession, fPromiscuous);
#endif /* IN_RING0 */

    LogFlow(("drvIntNetUp_SetPromiscuousMode: fPromiscuous=%RTbool\n", fPromiscuous));
    AssertRC(rc);
}

#ifdef IN_RING3

/**
 * @interface_method_impl{PDMINETWORKUP,pfnNotifyLinkChanged}
 */
static DECLCALLBACK(void) drvR3IntNetUp_NotifyLinkChanged(PPDMINETWORKUP pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    PDRVINTNET pThis = RT_FROM_MEMBER(pInterface, DRVINTNET, CTX_SUFF(INetworkUp));
    bool fLinkDown;
    switch (enmLinkState)
    {
        case PDMNETWORKLINKSTATE_DOWN:
        case PDMNETWORKLINKSTATE_DOWN_RESUME:
            fLinkDown = true;
            break;
        default:
            AssertMsgFailed(("enmLinkState=%d\n", enmLinkState));
            RT_FALL_THRU();
        case PDMNETWORKLINKSTATE_UP:
            fLinkDown = false;
            break;
    }
    LogFlow(("drvR3IntNetUp_NotifyLinkChanged: enmLinkState=%d %d->%d\n", enmLinkState, pThis->fLinkDown, fLinkDown));
    ASMAtomicXchgSize(&pThis->fLinkDown, fLinkDown);
}


/* -=-=-=-=- Transmit Thread -=-=-=-=- */

/**
 * Async I/O thread for deferred packet transmission.
 *
 * @returns VBox status code. Returning failure will naturally terminate the thread.
 * @param   pDrvIns     The internal networking driver instance.
 * @param   pThread     The thread.
 */
static DECLCALLBACK(int) drvR3IntNetXmitThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVINTNET pThis = PDMINS_2_DATA(pDrvIns, PDRVINTNET);

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        /*
         * Transmit any pending packets.
         */
        /** @todo Optimize this. We shouldn't call pfnXmitPending unless asked for.
         *        Also there is no need to call drvIntNetProcessXmit if we also
         *        called pfnXmitPending and send one or more frames. */
        if (ASMAtomicXchgBool(&pThis->fXmitProcessRing, false))
        {
            STAM_REL_COUNTER_INC(&pThis->StatXmitProcessRing);
            PDMDrvHlpCritSectEnter(pDrvIns, &pThis->XmitLock, VERR_IGNORED);
            drvIntNetProcessXmit(pThis);
            PDMDrvHlpCritSectLeave(pDrvIns, &pThis->XmitLock);
        }

        pThis->pIAboveNet->pfnXmitPending(pThis->pIAboveNet);

        if (ASMAtomicXchgBool(&pThis->fXmitProcessRing, false))
        {
            STAM_REL_COUNTER_INC(&pThis->StatXmitProcessRing);
            PDMDrvHlpCritSectEnter(pDrvIns, &pThis->XmitLock, VERR_IGNORED);
            drvIntNetProcessXmit(pThis);
            PDMDrvHlpCritSectLeave(pDrvIns, &pThis->XmitLock);
        }

        /*
         * Block until we've got something to send or is supposed
         * to leave the running state.
         */
        int rc = SUPSemEventWaitNoResume(pThis->pSupDrvSession, pThis->hXmitEvt, RT_INDEFINITE_WAIT);
        AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_INTERRUPTED, ("%Rrc\n", rc), rc);
        if (RT_UNLIKELY(pThread->enmState != PDMTHREADSTATE_RUNNING))
            break;

   }

    /* The thread is being initialized, suspended or terminated. */
    return VINF_SUCCESS;
}


/**
 * @copydoc FNPDMTHREADWAKEUPDRV
 */
static DECLCALLBACK(int) drvR3IntNetXmitWakeUp(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVINTNET pThis = PDMINS_2_DATA(pDrvIns, PDRVINTNET);
    return SUPSemEventSignal(pThis->pSupDrvSession, pThis->hXmitEvt);
}


/* -=-=-=-=- Receive Thread -=-=-=-=- */

/**
 * Wait for space to become available up the driver/device chain.
 *
 * @returns VINF_SUCCESS if space is available.
 * @returns VERR_STATE_CHANGED if the state changed.
 * @returns VBox status code on other errors.
 * @param   pThis       Pointer to the instance data.
 */
static int drvR3IntNetRecvWaitForSpace(PDRVINTNET pThis)
{
    LogFlow(("drvR3IntNetRecvWaitForSpace:\n"));
    STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
    int rc = pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, RT_INDEFINITE_WAIT);
    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);
    LogFlow(("drvR3IntNetRecvWaitForSpace: returns %Rrc\n", rc));
    return rc;
}


/**
 * Executes async I/O (RUNNING mode).
 *
 * @returns VERR_STATE_CHANGED if the state changed.
 * @returns Appropriate VBox status code (error) on fatal error.
 * @param   pThis       The driver instance data.
 */
static int drvR3IntNetRecvRun(PDRVINTNET pThis)
{
    LogFlow(("drvR3IntNetRecvRun: pThis=%p\n", pThis));

    /*
     * The running loop - processing received data and waiting for more to arrive.
     */
    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);
    PINTNETBUF      pBuf     = pThis->CTX_SUFF(pBuf);
    PINTNETRINGBUF  pRingBuf = &pBuf->Recv;
    for (;;)
    {
        /*
         * Process the receive buffer.
         */
        PINTNETHDR pHdr;
        while ((pHdr = IntNetRingGetNextFrameToRead(pRingBuf)) != NULL)
        {
            /*
             * Check the state and then inspect the packet.
             */
            if (pThis->enmRecvState != RECVSTATE_RUNNING)
            {
                STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
                LogFlow(("drvR3IntNetRecvRun: returns VERR_STATE_CHANGED (state changed - #0)\n"));
                return VERR_STATE_CHANGED;
            }

            Log2(("pHdr=%p offRead=%#x: %.8Rhxs\n", pHdr, pRingBuf->offReadX, pHdr));
            uint8_t u8Type = pHdr->u8Type;
            if (    (   u8Type == INTNETHDR_TYPE_FRAME
                     || u8Type == INTNETHDR_TYPE_GSO)
                &&  !pThis->fLinkDown)
            {
                /*
                 * Check if there is room for the frame and pass it up.
                 */
                size_t cbFrame = pHdr->cbFrame;
                int rc = pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, 0);
                if (rc == VINF_SUCCESS)
                {
                    if (u8Type == INTNETHDR_TYPE_FRAME)
                    {
                        /*
                         * Normal frame.
                         */
#ifdef LOG_ENABLED
                        if (LogIsEnabled())
                        {
                            uint64_t u64Now = RTTimeProgramNanoTS();
                            LogFlow(("drvR3IntNetRecvRun: %-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                                     cbFrame, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
                            pThis->u64LastReceiveTS = u64Now;
                            Log2(("drvR3IntNetRecvRun: cbFrame=%#x\n"
                                  "%.*Rhxd\n",
                                  cbFrame, cbFrame, IntNetHdrGetFramePtr(pHdr, pBuf)));
                        }
#endif
                        rc = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, IntNetHdrGetFramePtr(pHdr, pBuf), cbFrame);
                        AssertRC(rc);

                        /* skip to the next frame. */
                        IntNetRingSkipFrame(pRingBuf);
                    }
                    else
                    {
                        /*
                         * Generic segment offload frame (INTNETHDR_TYPE_GSO).
                         */
                        STAM_COUNTER_INC(&pThis->StatReceivedGso);
                        PCPDMNETWORKGSO pGso = IntNetHdrGetGsoContext(pHdr, pBuf);
                        if (PDMNetGsoIsValid(pGso, cbFrame, cbFrame - sizeof(PDMNETWORKGSO)))
                        {
                            if (   !pThis->pIAboveNet->pfnReceiveGso
                                || RT_FAILURE(pThis->pIAboveNet->pfnReceiveGso(pThis->pIAboveNet,
                                                                               (uint8_t *)(pGso + 1),
                                                                               pHdr->cbFrame - sizeof(PDMNETWORKGSO),
                                                                               pGso)))
                            {
                                /*
                                 * This is where we do the offloading since this NIC
                                 * does not support large receive offload (LRO).
                                 */
                                cbFrame -= sizeof(PDMNETWORKGSO);

                                uint8_t         abHdrScratch[256];
                                uint32_t const  cSegs = PDMNetGsoCalcSegmentCount(pGso, cbFrame);
#ifdef LOG_ENABLED
                                if (LogIsEnabled())
                                {
                                    uint64_t u64Now = RTTimeProgramNanoTS();
                                    LogFlow(("drvR3IntNetRecvRun: %-4d bytes at %llu ns  deltas: r=%llu t=%llu; GSO - %u segs\n",
                                             cbFrame, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS, cSegs));
                                    pThis->u64LastReceiveTS = u64Now;
                                    Log2(("drvR3IntNetRecvRun: cbFrame=%#x type=%d cbHdrsTotal=%#x cbHdrsSeg=%#x Hdr1=%#x Hdr2=%#x MMS=%#x\n"
                                          "%.*Rhxd\n",
                                          cbFrame, pGso->u8Type, pGso->cbHdrsTotal, pGso->cbHdrsSeg, pGso->offHdr1, pGso->offHdr2, pGso->cbMaxSeg,
                                          cbFrame - sizeof(*pGso), pGso + 1));
                                }
#endif
                                for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
                                {
                                    uint32_t cbSegFrame;
                                    void    *pvSegFrame = PDMNetGsoCarveSegmentQD(pGso, (uint8_t *)(pGso + 1), cbFrame,
                                                                                  abHdrScratch, iSeg, cSegs, &cbSegFrame);
                                    rc = drvR3IntNetRecvWaitForSpace(pThis);
                                    if (RT_FAILURE(rc))
                                    {
                                        Log(("drvR3IntNetRecvRun: drvR3IntNetRecvWaitForSpace -> %Rrc; iSeg=%u cSegs=%u\n", rc, iSeg, cSegs));
                                        break; /* we drop the rest. */
                                    }
                                    rc = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, pvSegFrame, cbSegFrame);
                                    AssertRC(rc);
                                }
                            }
                        }
                        else
                        {
                            AssertMsgFailed(("cbFrame=%#x type=%d cbHdrsTotal=%#x cbHdrsSeg=%#x Hdr1=%#x Hdr2=%#x MMS=%#x\n",
                                             cbFrame, pGso->u8Type, pGso->cbHdrsTotal, pGso->cbHdrsSeg, pGso->offHdr1, pGso->offHdr2, pGso->cbMaxSeg));
                            STAM_REL_COUNTER_INC(&pBuf->cStatBadFrames);
                        }

                        IntNetRingSkipFrame(pRingBuf);
                    }
                }
                else
                {
                    /*
                     * Wait for sufficient space to become available and then retry.
                     */
                    rc = drvR3IntNetRecvWaitForSpace(pThis);
                    if (RT_FAILURE(rc))
                    {
                        if (rc == VERR_INTERRUPTED)
                        {
                            /*
                             * NIC is going down, likely because the VM is being reset. Skip the frame.
                             */
                            AssertMsg(IntNetIsValidFrameType(pHdr->u8Type), ("Unknown frame type %RX16! offRead=%#x\n", pHdr->u8Type, pRingBuf->offReadX));
                            IntNetRingSkipFrame(pRingBuf);
                        }
                        else
                        {
                            STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
                            LogFlow(("drvR3IntNetRecvRun: returns %Rrc (wait-for-space)\n", rc));
                            return rc;
                        }
                    }
                }
            }
            else
            {
                /*
                 * Link down or unknown frame - skip to the next frame.
                 */
                AssertMsg(IntNetIsValidFrameType(pHdr->u8Type), ("Unknown frame type %RX16! offRead=%#x\n", pHdr->u8Type, pRingBuf->offReadX));
                IntNetRingSkipFrame(pRingBuf);
                STAM_REL_COUNTER_INC(&pBuf->cStatBadFrames);
            }
        } /* while more received data */

        /*
         * Wait for data, checking the state before we block.
         */
        if (pThis->enmRecvState != RECVSTATE_RUNNING)
        {
            STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
            LogFlow(("drvR3IntNetRecvRun: returns VINF_SUCCESS (state changed - #1)\n"));
            return VERR_STATE_CHANGED;
        }
        INTNETIFWAITREQ WaitReq;
        WaitReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        WaitReq.Hdr.cbReq    = sizeof(WaitReq);
        WaitReq.pSession     = NIL_RTR0PTR;
        WaitReq.hIf          = pThis->hIf;
        WaitReq.cMillies     = 30000; /* 30s - don't wait forever, timeout now and then. */
        STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);

#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
        if (pThis->fIntNetR3Svc)
        {
            /* Send an asynchronous message. */
            int rc = drvR3IntNetCallSvcAsync(pThis, VMMR0_DO_INTNET_IF_WAIT, &WaitReq, sizeof(WaitReq));
            if (RT_SUCCESS(rc))
            {
                /* Wait on the receive semaphore. */
                rc = RTSemEventWait(pThis->hRecvEvt, 30 * RT_MS_1SEC);
                if (    RT_FAILURE(rc)
                    &&  rc != VERR_TIMEOUT
                    &&  rc != VERR_INTERRUPTED)
                {
                    LogFlow(("drvR3IntNetRecvRun: returns %Rrc\n", rc));
                    return rc;
                }
            }
        }
        else
#endif
        {
            int rc = PDMDrvHlpSUPCallVMMR0Ex(pThis->pDrvInsR3, VMMR0_DO_INTNET_IF_WAIT, &WaitReq, sizeof(WaitReq));
            if (    RT_FAILURE(rc)
                &&  rc != VERR_TIMEOUT
                &&  rc != VERR_INTERRUPTED)
            {
                LogFlow(("drvR3IntNetRecvRun: returns %Rrc\n", rc));
                return rc;
            }
        }
        STAM_PROFILE_ADV_START(&pThis->StatReceive, a);
    }
}


/**
 * Asynchronous I/O thread for handling receive.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   hThreadSelf     Thread handle.
 * @param   pvUser          Pointer to a DRVINTNET structure.
 */
static DECLCALLBACK(int) drvR3IntNetRecvThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    PDRVINTNET pThis = (PDRVINTNET)pvUser;
    LogFlow(("drvR3IntNetRecvThread: pThis=%p\n", pThis));
    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

    /*
     * The main loop - acting on state.
     */
    for (;;)
    {
        RECVSTATE enmRecvState = pThis->enmRecvState;
        switch (enmRecvState)
        {
            case RECVSTATE_SUSPENDED:
            {
                int rc = RTSemEventWait(pThis->hRecvEvt, 30000);
                if (    RT_FAILURE(rc)
                    &&  rc != VERR_TIMEOUT)
                {
                    LogFlow(("drvR3IntNetRecvThread: returns %Rrc\n", rc));
                    return rc;
                }
                break;
            }

            case RECVSTATE_RUNNING:
            {
                int rc = drvR3IntNetRecvRun(pThis);
                if (    rc != VERR_STATE_CHANGED
                    &&  RT_FAILURE(rc))
                {
                    LogFlow(("drvR3IntNetRecvThread: returns %Rrc\n", rc));
                    return rc;
                }
                break;
            }

            default:
                AssertMsgFailed(("Invalid state %d\n", enmRecvState));
                RT_FALL_THRU();
            case RECVSTATE_TERMINATE:
                LogFlow(("drvR3IntNetRecvThread: returns VINF_SUCCESS\n"));
                return VINF_SUCCESS;
        }
    }
}


#ifdef VBOX_WITH_DRVINTNET_IN_R0

/* -=-=-=-=- PDMIBASERC -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASERC,pfnQueryInterface}
 */
static DECLCALLBACK(RTRCPTR) drvR3IntNetIBaseRC_QueryInterface(PPDMIBASERC pInterface, const char *pszIID)
{
    PDRVINTNET pThis = RT_FROM_MEMBER(pInterface, DRVINTNET, IBaseRC);
#if 0
    PDMIBASERC_RETURN_INTERFACE(pThis->pDrvInsR3, pszIID, PDMINETWORKUP, &pThis->INetworkUpRC);
#else
    RT_NOREF(pThis, pszIID);
#endif
    return NIL_RTRCPTR;
}


/* -=-=-=-=- PDMIBASER0 -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASER0,pfnQueryInterface}
 */
static DECLCALLBACK(RTR0PTR) drvR3IntNetIBaseR0_QueryInterface(PPDMIBASER0 pInterface, const char *pszIID)
{
    PDRVINTNET pThis = RT_FROM_MEMBER(pInterface, DRVINTNET, IBaseR0);
    PDMIBASER0_RETURN_INTERFACE(pThis->pDrvInsR3, pszIID, PDMINETWORKUP, &pThis->INetworkUpR0);
    return NIL_RTR0PTR;
}

#endif /* VBOX_WITH_DRVINTNET_IN_R0 */

/* -=-=-=-=- PDMIBASE -=-=-=-=- */


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvR3IntNetIBase_QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVINTNET pThis   = PDMINS_2_DATA(pDrvIns, PDRVINTNET);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
#ifdef VBOX_WITH_DRVINTNET_IN_R0
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASER0, &pThis->IBaseR0);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASERC, &pThis->IBaseRC);
#endif
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKUP, &pThis->INetworkUpR3);
    return NULL;
}


/* -=-=-=-=- PDMDRVREG -=-=-=-=- */

/**
 * Power Off notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvR3IntNetPowerOff(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3IntNetPowerOff\n"));
    PDRVINTNET pThis = PDMINS_2_DATA(pDrvIns, PDRVINTNET);
    if (!pThis->fActivateEarlyDeactivateLate)
    {
        ASMAtomicXchgSize(&pThis->enmRecvState, RECVSTATE_SUSPENDED);
        drvR3IntNetSetActive(pThis, false /* fActive */);
    }
}


/**
 * drvR3IntNetResume helper.
 */
static int drvR3IntNetResumeSend(PDRVINTNET pThis, const void *pvBuf, size_t cb)
{
    /*
     * Add the frame to the send buffer and push it onto the network.
     */
    int rc = IntNetRingWriteFrame(&pThis->pBufR3->Send, pvBuf, (uint32_t)cb);
    if (    rc == VERR_BUFFER_OVERFLOW
        &&  pThis->pBufR3->cbSend < cb)
    {
        INTNETIFSENDREQ SendReq;
        SendReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        SendReq.Hdr.cbReq = sizeof(SendReq);
        SendReq.pSession = NIL_RTR0PTR;
        SendReq.hIf = pThis->hIf;
        drvR3IntNetCallSvc(pThis, VMMR0_DO_INTNET_IF_SEND, &SendReq, sizeof(SendReq));

        rc = IntNetRingWriteFrame(&pThis->pBufR3->Send, pvBuf, (uint32_t)cb);
    }

    if (RT_SUCCESS(rc))
    {
        INTNETIFSENDREQ SendReq;
        SendReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        SendReq.Hdr.cbReq = sizeof(SendReq);
        SendReq.pSession = NIL_RTR0PTR;
        SendReq.hIf = pThis->hIf;
        rc = drvR3IntNetCallSvc(pThis, VMMR0_DO_INTNET_IF_SEND, &SendReq, sizeof(SendReq));
    }

    AssertRC(rc);
    return rc;
}


/**
 * Resume notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvR3IntNetResume(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3IntNetPowerResume\n"));
    PDRVINTNET pThis = PDMINS_2_DATA(pDrvIns, PDRVINTNET);
    VMRESUMEREASON enmReason = PDMDrvHlpVMGetResumeReason(pDrvIns);

    if (!pThis->fActivateEarlyDeactivateLate)
    {
        ASMAtomicXchgSize(&pThis->enmRecvState, RECVSTATE_RUNNING);
        RTSemEventSignal(pThis->hRecvEvt);
        drvR3IntNetUpdateMacAddress(pThis); /* (could be a state restore) */
        drvR3IntNetSetActive(pThis, true /* fActive */);
    }

    switch (enmReason)
    {
        case VMRESUMEREASON_HOST_RESUME:
        {
            uint32_t u32TrunkType;
            int rc = pDrvIns->pHlpR3->pfnCFGMQueryU32(pDrvIns->pCfg, "TrunkType", &u32TrunkType);
            AssertRC(rc);

            /*
             * Only do the disconnect for bridged networking. Host-only and
             * internal networks are not affected by a host resume.
             */
            if (   RT_SUCCESS(rc)
                && u32TrunkType == kIntNetTrunkType_NetFlt)
            {
                rc = pThis->pIAboveConfigR3->pfnSetLinkState(pThis->pIAboveConfigR3,
                                                             PDMNETWORKLINKSTATE_DOWN_RESUME);
                AssertRC(rc);
            }
            break;
        }
        case VMRESUMEREASON_TELEPORTED:
        case VMRESUMEREASON_TELEPORT_FAILED:
        {
            if (   PDMDrvHlpVMTeleportedAndNotFullyResumedYet(pDrvIns)
                   && pThis->pIAboveConfigR3)
            {
                /*
                 * We've just been teleported and need to drop a hint to the switch
                 * since we're likely to have changed to a different port.  We just
                 * push out some ethernet frame that doesn't mean anything to anyone.
                 * For this purpose ethertype 0x801e was chosen since it was registered
                 * to Sun (dunno what it is/was used for though).
                 */
                union
                {
                    RTNETETHERHDR   Hdr;
                    uint8_t         ab[128];
                } Frame;
                RT_ZERO(Frame);
                Frame.Hdr.DstMac.au16[0] = 0xffff;
                Frame.Hdr.DstMac.au16[1] = 0xffff;
                Frame.Hdr.DstMac.au16[2] = 0xffff;
                Frame.Hdr.EtherType      = RT_H2BE_U16_C(0x801e);
                int rc = pThis->pIAboveConfigR3->pfnGetMac(pThis->pIAboveConfigR3,
                                                           &Frame.Hdr.SrcMac);
                if (RT_SUCCESS(rc))
                    rc = drvR3IntNetResumeSend(pThis, &Frame, sizeof(Frame));
                if (RT_FAILURE(rc))
                    LogRel(("IntNet#%u: Sending dummy frame failed: %Rrc\n",
                            pDrvIns->iInstance, rc));
            }
            break;
        }
        default: /* ignore every other resume reason else */
            break;
    } /* end of switch(enmReason) */
}


/**
 * Suspend notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvR3IntNetSuspend(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3IntNetPowerSuspend\n"));
    PDRVINTNET pThis = PDMINS_2_DATA(pDrvIns, PDRVINTNET);
    if (!pThis->fActivateEarlyDeactivateLate)
    {
        ASMAtomicXchgSize(&pThis->enmRecvState, RECVSTATE_SUSPENDED);
        drvR3IntNetSetActive(pThis, false /* fActive */);
    }
}


/**
 * Power On notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvR3IntNetPowerOn(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3IntNetPowerOn\n"));
    PDRVINTNET pThis = PDMINS_2_DATA(pDrvIns, PDRVINTNET);
    if (!pThis->fActivateEarlyDeactivateLate)
    {
        ASMAtomicXchgSize(&pThis->enmRecvState, RECVSTATE_RUNNING);
        RTSemEventSignal(pThis->hRecvEvt);
        drvR3IntNetUpdateMacAddress(pThis);
        drvR3IntNetSetActive(pThis, true /* fActive */);
    }
}


/**
 * @interface_method_impl{PDMDRVREG,pfnRelocate}
 */
static DECLCALLBACK(void) drvR3IntNetRelocate(PPDMDRVINS pDrvIns, RTGCINTPTR offDelta)
{
    /* nothing to do here yet */
    RT_NOREF(pDrvIns, offDelta);
}


/**
 * Destruct a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvR3IntNetDestruct(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3IntNetDestruct\n"));
    PDRVINTNET pThis = PDMINS_2_DATA(pDrvIns, PDRVINTNET);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    /*
     * Indicate to the receive thread that it's time to quit.
     */
    ASMAtomicXchgSize(&pThis->enmRecvState, RECVSTATE_TERMINATE);
    ASMAtomicXchgSize(&pThis->fLinkDown, true);
    RTSEMEVENT hRecvEvt = pThis->hRecvEvt;
    pThis->hRecvEvt = NIL_RTSEMEVENT;

    if (hRecvEvt != NIL_RTSEMEVENT)
        RTSemEventSignal(hRecvEvt);

    if (pThis->hIf != INTNET_HANDLE_INVALID)
    {
#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
        if (!pThis->fIntNetR3Svc) /* The R3 service case is handled b the hRecEvt event semaphore. */
#endif
        {
            INTNETIFABORTWAITREQ AbortWaitReq;
            AbortWaitReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
            AbortWaitReq.Hdr.cbReq    = sizeof(AbortWaitReq);
            AbortWaitReq.pSession     = NIL_RTR0PTR;
            AbortWaitReq.hIf          = pThis->hIf;
            AbortWaitReq.fNoMoreWaits = true;
            int rc = drvR3IntNetCallSvc(pThis, VMMR0_DO_INTNET_IF_ABORT_WAIT, &AbortWaitReq, sizeof(AbortWaitReq));
            AssertMsg(RT_SUCCESS(rc) || rc == VERR_SEM_DESTROYED, ("%Rrc\n", rc)); RT_NOREF_PV(rc);
        }
    }

    /*
     * Wait for the threads to terminate.
     */
    if (pThis->pXmitThread)
    {
        int rc = PDMDrvHlpThreadDestroy(pDrvIns, pThis->pXmitThread, NULL);
        AssertRC(rc);
        pThis->pXmitThread = NULL;
    }

    if (pThis->hRecvThread != NIL_RTTHREAD)
    {
        int rc = RTThreadWait(pThis->hRecvThread, 5000, NULL);
        AssertRC(rc);
        pThis->hRecvThread = NIL_RTTHREAD;
    }

    /*
     * Deregister statistics in case we're being detached.
     */
    if (pThis->pBufR3)
    {
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->Recv.cStatFrames);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->Recv.cbStatWritten);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->Recv.cOverflows);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->Send.cStatFrames);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->Send.cbStatWritten);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->Send.cOverflows);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->cStatYieldsOk);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->cStatYieldsNok);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->cStatLost);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->cStatBadFrames);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->StatSend1);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->StatSend2);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->StatRecv1);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->StatRecv2);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->pBufR3->StatReserved);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReceivedGso);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatSentGso);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatSentR0);
#ifdef VBOX_WITH_STATISTICS
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReceive);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatTransmit);
#endif
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatXmitWakeupR0);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatXmitWakeupR3);
        PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatXmitProcessRing);
    }

    /*
     * Close the interface
     */
    if (pThis->hIf != INTNET_HANDLE_INVALID)
    {
        INTNETIFCLOSEREQ CloseReq;
        CloseReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        CloseReq.Hdr.cbReq = sizeof(CloseReq);
        CloseReq.pSession = NIL_RTR0PTR;
        CloseReq.hIf = pThis->hIf;
        pThis->hIf = INTNET_HANDLE_INVALID;
        int rc = drvR3IntNetCallSvc(pThis, VMMR0_DO_INTNET_IF_CLOSE, &CloseReq, sizeof(CloseReq));
        AssertRC(rc);
    }

#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
        /* Unmap the shared buffer. */
        munmap(pThis->pBufR3, pThis->cbBuf);
        xpc_connection_cancel(pThis->hXpcCon);
        pThis->fIntNetR3Svc = false;
        pThis->hXpcCon      = NULL;
    }
#endif

    /*
     * Destroy the semaphores, S/G cache and xmit lock.
     */
    if (hRecvEvt != NIL_RTSEMEVENT)
        RTSemEventDestroy(hRecvEvt);

    if (pThis->hXmitEvt != NIL_SUPSEMEVENT)
    {
        SUPSemEventClose(pThis->pSupDrvSession, pThis->hXmitEvt);
        pThis->hXmitEvt = NIL_SUPSEMEVENT;
    }

    RTMemCacheDestroy(pThis->hSgCache);
    pThis->hSgCache = NIL_RTMEMCACHE;

    if (PDMDrvHlpCritSectIsInitialized(pDrvIns, &pThis->XmitLock))
        PDMDrvHlpCritSectDelete(pDrvIns, &pThis->XmitLock);
}


/**
 * Queries a policy config value and translates it into open network flag.
 *
 * @returns VBox status code (error set on failure).
 * @param   pDrvIns             The driver instance.
 * @param   pszName             The value name.
 * @param   paFlags             The open network flag descriptors.
 * @param   cFlags              The number of descriptors.
 * @param   fFlags              The fixed flag.
 * @param   pfFlags             The flags variable to update.
 */
static int drvIntNetR3CfgGetPolicy(PPDMDRVINS pDrvIns, const char *pszName, PCDRVINTNETFLAG paFlags, size_t cFlags,
                                   uint32_t fFixedFlag, uint32_t *pfFlags)
{
    PCPDMDRVHLPR3 pHlp = pDrvIns->pHlpR3;

    char szValue[64];
    int rc = pHlp->pfnCFGMQueryString(pDrvIns->pCfg, pszName, szValue, sizeof(szValue));
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
            return VINF_SUCCESS;
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Failed to query value of \"%s\""), pszName);
    }

    /*
     * Check for +fixed first, so it can be stripped off.
     */
    char *pszSep = strpbrk(szValue, "+,;");
    if (pszSep)
    {
        *pszSep++ = '\0';
        const char *pszFixed = RTStrStripL(pszSep);
        if (strcmp(pszFixed, "fixed"))
        {
            *pszSep = '+';
            return PDMDrvHlpVMSetError(pDrvIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                       N_("Configuration error: The value of \"%s\" is unknown: \"%s\""), pszName, szValue);
        }
        *pfFlags |= fFixedFlag;
        RTStrStripR(szValue);
    }

    /*
     * Match against the flag values.
     */
    size_t i = cFlags;
    while (i-- > 0)
        if (!strcmp(paFlags[i].pszChoice, szValue))
        {
            *pfFlags |= paFlags[i].fFlag;
            return VINF_SUCCESS;
        }

    if (!strcmp(szValue, "none"))
        return VINF_SUCCESS;

    if (!strcmp(szValue, "fixed"))
    {
        *pfFlags |= fFixedFlag;
        return VINF_SUCCESS;
    }

    return PDMDrvHlpVMSetError(pDrvIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                               N_("Configuration error: The value of \"%s\" is unknown: \"%s\""), pszName, szValue);
}


/**
 * Construct a TAP network transport driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvR3IntNetConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVINTNET      pThis = PDMINS_2_DATA(pDrvIns, PDRVINTNET);
    PCPDMDRVHLPR3   pHlp = pDrvIns->pHlpR3;
    bool f;

    /*
     * Init the static parts.
     */
    pThis->pDrvInsR3                                = pDrvIns;
#ifdef VBOX_WITH_DRVINTNET_IN_R0
    pThis->pDrvInsR0                                = PDMDRVINS_2_R0PTR(pDrvIns);
#endif
    pThis->hIf                                      = INTNET_HANDLE_INVALID;
    pThis->hRecvThread                              = NIL_RTTHREAD;
    pThis->hRecvEvt                                 = NIL_RTSEMEVENT;
    pThis->pXmitThread                              = NULL;
    pThis->hXmitEvt                                 = NIL_SUPSEMEVENT;
    pThis->pSupDrvSession                           = PDMDrvHlpGetSupDrvSession(pDrvIns);
    pThis->hSgCache                                 = NIL_RTMEMCACHE;
    pThis->enmRecvState                             = RECVSTATE_SUSPENDED;
    pThis->fActivateEarlyDeactivateLate             = false;
    /* IBase* */
    pDrvIns->IBase.pfnQueryInterface                = drvR3IntNetIBase_QueryInterface;
#ifdef VBOX_WITH_DRVINTNET_IN_R0
    pThis->IBaseR0.pfnQueryInterface                = drvR3IntNetIBaseR0_QueryInterface;
    pThis->IBaseRC.pfnQueryInterface                = drvR3IntNetIBaseRC_QueryInterface;
#endif
    /* INetworkUp */
    pThis->INetworkUpR3.pfnBeginXmit                = drvIntNetUp_BeginXmit;
    pThis->INetworkUpR3.pfnAllocBuf                 = drvIntNetUp_AllocBuf;
    pThis->INetworkUpR3.pfnFreeBuf                  = drvIntNetUp_FreeBuf;
    pThis->INetworkUpR3.pfnSendBuf                  = drvIntNetUp_SendBuf;
    pThis->INetworkUpR3.pfnEndXmit                  = drvIntNetUp_EndXmit;
    pThis->INetworkUpR3.pfnSetPromiscuousMode       = drvIntNetUp_SetPromiscuousMode;
    pThis->INetworkUpR3.pfnNotifyLinkChanged        = drvR3IntNetUp_NotifyLinkChanged;

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns,
                                  "Network"
                                  "|Trunk"
                                  "|TrunkType"
                                  "|ReceiveBufferSize"
                                  "|SendBufferSize"
                                  "|SharedMacOnWire"
                                  "|RestrictAccess"
                                  "|RequireExactPolicyMatch"
                                  "|RequireAsRestrictivePolicy"
                                  "|AccessPolicy"
                                  "|PromiscPolicyClients"
                                  "|PromiscPolicyHost"
                                  "|PromiscPolicyWire"
                                  "|IfPolicyPromisc"
                                  "|TrunkPolicyHost"
                                  "|TrunkPolicyWire"
                                  "|IsService"
                                  "|IgnoreConnectFailure"
                                  "|Workaround1",
                                  "");

    /*
     * Check that no-one is attached to us.
     */
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Query the network port interface.
     */
    pThis->pIAboveNet = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKDOWN);
    if (!pThis->pIAboveNet)
    {
        AssertMsgFailed(("Configuration error: the above device/driver didn't export the network port interface!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }
    pThis->pIAboveConfigR3 = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKCONFIG);

    /*
     * Read the configuration.
     */
    INTNETOPENREQ OpenReq;
    RT_ZERO(OpenReq);
    OpenReq.Hdr.cbReq = sizeof(OpenReq);
    OpenReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    OpenReq.pSession = NIL_RTR0PTR;

    /** @cfgm{Network, string}
     * The name of the internal network to connect to.
     */
    int rc = pHlp->pfnCFGMQueryString(pCfg, "Network", OpenReq.szNetwork, sizeof(OpenReq.szNetwork));
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"Network\" value"));
    strcpy(pThis->szNetwork, OpenReq.szNetwork);

    /** @cfgm{TrunkType, uint32_t, kIntNetTrunkType_None}
     * The trunk connection type see INTNETTRUNKTYPE.
     */
    uint32_t u32TrunkType;
    rc = pHlp->pfnCFGMQueryU32(pCfg, "TrunkType", &u32TrunkType);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        u32TrunkType = kIntNetTrunkType_None;
    else if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"TrunkType\" value"));
    OpenReq.enmTrunkType = (INTNETTRUNKTYPE)u32TrunkType;

    /** @cfgm{Trunk, string, ""}
     * The name of the trunk connection.
     */
    rc = pHlp->pfnCFGMQueryString(pCfg, "Trunk", OpenReq.szTrunk, sizeof(OpenReq.szTrunk));
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        OpenReq.szTrunk[0] = '\0';
    else if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"Trunk\" value"));

    OpenReq.fFlags = 0;

    /** @cfgm{SharedMacOnWire, boolean, false}
     * Whether to shared the MAC address of the host interface when using the wire. When
     * attaching to a wireless NIC this option is usually a requirement.
     */
    bool fSharedMacOnWire;
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "SharedMacOnWire", &fSharedMacOnWire, false);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"SharedMacOnWire\" value"));
    if (fSharedMacOnWire)
        OpenReq.fFlags |= INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE;

    /** @cfgm{RestrictAccess, boolean, true}
     * Whether to restrict the access to the network or if it should be public.
     * Everyone on the computer can connect to a public network.
     * @deprecated Use AccessPolicy instead.
     */
    rc = pHlp->pfnCFGMQueryBool(pCfg, "RestrictAccess", &f);
    if (RT_SUCCESS(rc))
    {
        if (f)
            OpenReq.fFlags |= INTNET_OPEN_FLAGS_ACCESS_RESTRICTED;
        else
            OpenReq.fFlags |= INTNET_OPEN_FLAGS_ACCESS_PUBLIC;
        OpenReq.fFlags |= INTNET_OPEN_FLAGS_ACCESS_FIXED;
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND)
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"RestrictAccess\" value"));

    /** @cfgm{RequireExactPolicyMatch, boolean, false}
     * Whether to require that the current security and promiscuous policies of
     * the network is exactly as the ones specified in this open network
     * request.  Use this with RequireAsRestrictivePolicy to prevent
     * restrictions from being lifted.  If no further policy changes are
     * desired, apply the relevant fixed flags. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "RequireExactPolicyMatch", &f, false);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"RequireExactPolicyMatch\" value"));
    if (f)
        OpenReq.fFlags |= INTNET_OPEN_FLAGS_REQUIRE_EXACT;

    /** @cfgm{RequireAsRestrictivePolicy, boolean, false}
     * Whether to require that the security and promiscuous policies of the
     * network is at least as restrictive as specified this request specifies
     * and prevent them  being lifted later on.
     */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "RequireAsRestrictivePolicy", &f, false);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"RequireAsRestrictivePolicy\" value"));
    if (f)
        OpenReq.fFlags |= INTNET_OPEN_FLAGS_REQUIRE_AS_RESTRICTIVE_POLICIES;

    /** @cfgm{AccessPolicy, string, "none"}
     * The access policy of the network:
     *      public, public+fixed, restricted, restricted+fixed, none or fixed.
     *
     * A "public" network is accessible to everyone on the same host, while a
     * "restricted" one is only accessible to VMs & services started by the
     * same user.  The "none" policy, which is the default, means no policy
     * change or choice is made and that the current (existing network) or
     * default (new) policy should be used. */
    static const DRVINTNETFLAG s_aAccessPolicyFlags[] =
    {
        { "public",         INTNET_OPEN_FLAGS_ACCESS_PUBLIC             },
        { "restricted",     INTNET_OPEN_FLAGS_ACCESS_RESTRICTED         }
    };
    rc = drvIntNetR3CfgGetPolicy(pDrvIns, "AccessPolicy", &s_aAccessPolicyFlags[0], RT_ELEMENTS(s_aAccessPolicyFlags),
                                 INTNET_OPEN_FLAGS_ACCESS_FIXED, &OpenReq.fFlags);
    AssertRCReturn(rc, rc);

    /** @cfgm{PromiscPolicyClients, string, "none"}
     * The network wide promiscuous mode policy for client (non-trunk)
     * interfaces: allow, allow+fixed, deny, deny+fixed, none or fixed. */
    static const DRVINTNETFLAG s_aPromiscPolicyClient[] =
    {
        { "allow",         INTNET_OPEN_FLAGS_PROMISC_ALLOW_CLIENTS      },
        { "deny",          INTNET_OPEN_FLAGS_PROMISC_DENY_CLIENTS       }
    };
    rc = drvIntNetR3CfgGetPolicy(pDrvIns, "PromiscPolicyClients", &s_aPromiscPolicyClient[0], RT_ELEMENTS(s_aPromiscPolicyClient),
                                 INTNET_OPEN_FLAGS_PROMISC_FIXED, &OpenReq.fFlags);
    AssertRCReturn(rc, rc);
    /** @cfgm{PromiscPolicyHost, string, "none"}
     * The promiscuous mode policy for the trunk-host
     * connection: allow, allow+fixed, deny, deny+fixed, none or fixed. */
    static const DRVINTNETFLAG s_aPromiscPolicyHost[] =
    {
        { "allow",         INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_HOST   },
        { "deny",          INTNET_OPEN_FLAGS_PROMISC_DENY_TRUNK_HOST    }
    };
    rc = drvIntNetR3CfgGetPolicy(pDrvIns, "PromiscPolicyHost", &s_aPromiscPolicyHost[0], RT_ELEMENTS(s_aPromiscPolicyHost),
                                 INTNET_OPEN_FLAGS_PROMISC_FIXED, &OpenReq.fFlags);
    AssertRCReturn(rc, rc);
    /** @cfgm{PromiscPolicyWire, string, "none"}
     * The promiscuous mode policy for the trunk-host
     * connection: allow, allow+fixed, deny, deny+fixed, none or fixed. */
    static const DRVINTNETFLAG s_aPromiscPolicyWire[] =
    {
        { "allow",         INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_WIRE   },
        { "deny",          INTNET_OPEN_FLAGS_PROMISC_DENY_TRUNK_WIRE    }
    };
    rc = drvIntNetR3CfgGetPolicy(pDrvIns, "PromiscPolicyWire", &s_aPromiscPolicyWire[0], RT_ELEMENTS(s_aPromiscPolicyWire),
                                 INTNET_OPEN_FLAGS_PROMISC_FIXED, &OpenReq.fFlags);
    AssertRCReturn(rc, rc);


    /** @cfgm{IfPolicyPromisc, string, "none"}
     * The promiscuous mode policy for this
     * interface: deny, deny+fixed, allow-all, allow-all+fixed, allow-network,
     *      allow-network+fixed, none or fixed. */
    static const DRVINTNETFLAG s_aIfPolicyPromisc[] =
    {
        { "allow-all",     INTNET_OPEN_FLAGS_IF_PROMISC_ALLOW | INTNET_OPEN_FLAGS_IF_PROMISC_SEE_TRUNK },
        { "allow-network", INTNET_OPEN_FLAGS_IF_PROMISC_ALLOW | INTNET_OPEN_FLAGS_IF_PROMISC_NO_TRUNK  },
        { "deny",          INTNET_OPEN_FLAGS_IF_PROMISC_DENY }
    };
    rc = drvIntNetR3CfgGetPolicy(pDrvIns, "IfPolicyPromisc", &s_aIfPolicyPromisc[0], RT_ELEMENTS(s_aIfPolicyPromisc),
                                 INTNET_OPEN_FLAGS_IF_FIXED, &OpenReq.fFlags);
    AssertRCReturn(rc, rc);


    /** @cfgm{TrunkPolicyHost, string, "none"}
     * The trunk-host policy: promisc, promisc+fixed, enabled, enabled+fixed,
     *      disabled, disabled+fixed, none or fixed
     *
     * This can be used to prevent packages to be routed to the host. */
    static const DRVINTNETFLAG s_aTrunkPolicyHost[] =
    {
        { "promisc",        INTNET_OPEN_FLAGS_TRUNK_HOST_ENABLED | INTNET_OPEN_FLAGS_TRUNK_HOST_PROMISC_MODE },
        { "enabled",        INTNET_OPEN_FLAGS_TRUNK_HOST_ENABLED },
        { "disabled",       INTNET_OPEN_FLAGS_TRUNK_HOST_DISABLED }
    };
    rc = drvIntNetR3CfgGetPolicy(pDrvIns, "TrunkPolicyHost", &s_aTrunkPolicyHost[0], RT_ELEMENTS(s_aTrunkPolicyHost),
                                 INTNET_OPEN_FLAGS_TRUNK_FIXED, &OpenReq.fFlags);
    AssertRCReturn(rc, rc);
    /** @cfgm{TrunkPolicyWire, string, "none"}
     * The trunk-host policy: promisc, promisc+fixed, enabled, enabled+fixed,
     *      disabled, disabled+fixed, none or fixed.
     *
     * This can be used to prevent packages to be routed to the wire. */
    static const DRVINTNETFLAG s_aTrunkPolicyWire[] =
    {
        { "promisc",        INTNET_OPEN_FLAGS_TRUNK_WIRE_ENABLED | INTNET_OPEN_FLAGS_TRUNK_WIRE_PROMISC_MODE },
        { "enabled",        INTNET_OPEN_FLAGS_TRUNK_WIRE_ENABLED },
        { "disabled",       INTNET_OPEN_FLAGS_TRUNK_WIRE_DISABLED }
    };
    rc = drvIntNetR3CfgGetPolicy(pDrvIns, "TrunkPolicyWire", &s_aTrunkPolicyWire[0], RT_ELEMENTS(s_aTrunkPolicyWire),
                                 INTNET_OPEN_FLAGS_TRUNK_FIXED, &OpenReq.fFlags);
    AssertRCReturn(rc, rc);


    /** @cfgm{ReceiveBufferSize, uint32_t, 318 KB}
     * The size of the receive buffer.
     */
    rc = pHlp->pfnCFGMQueryU32(pCfg, "ReceiveBufferSize", &OpenReq.cbRecv);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        OpenReq.cbRecv = 318 * _1K ;
    else if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"ReceiveBufferSize\" value"));

    /** @cfgm{SendBufferSize, uint32_t, 196 KB}
     * The size of the send (transmit) buffer.
     * This should be more than twice the size of the larges frame size because
     * the ring buffer is very simple and doesn't support splitting up frames
     * nor inserting padding. So, if this is too close to the frame size the
     * header will fragment the buffer such that the frame won't fit on either
     * side of it and the code will get very upset about it all.
     */
    rc = pHlp->pfnCFGMQueryU32(pCfg, "SendBufferSize", &OpenReq.cbSend);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        OpenReq.cbSend = RT_ALIGN_Z(VBOX_MAX_GSO_SIZE * 3, _1K);
    else if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"SendBufferSize\" value"));
    if (OpenReq.cbSend < 128)
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: The \"SendBufferSize\" value is too small"));
    if (OpenReq.cbSend < VBOX_MAX_GSO_SIZE * 3)
        LogRel(("DrvIntNet: Warning! SendBufferSize=%u, Recommended minimum size %u butes.\n", OpenReq.cbSend, VBOX_MAX_GSO_SIZE * 4));

    /** @cfgm{IsService, boolean, true}
     * This alterns the way the thread is suspended and resumed. When it's being used by
     * a service such as LWIP/iSCSI it shouldn't suspend immediately like for a NIC.
     */
    rc = pHlp->pfnCFGMQueryBool(pCfg, "IsService", &pThis->fActivateEarlyDeactivateLate);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pThis->fActivateEarlyDeactivateLate = false;
    else if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"IsService\" value"));


    /** @cfgm{IgnoreConnectFailure, boolean, false}
     * When set only raise a runtime error if we cannot connect to the internal
     * network. */
    bool fIgnoreConnectFailure;
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "IgnoreConnectFailure", &fIgnoreConnectFailure, false);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"IgnoreConnectFailure\" value"));

    /** @cfgm{Workaround1, boolean, depends}
     * Enables host specific workarounds, the default is depends on the whether
     * we think the host requires it or not.
     */
    bool fWorkaround1 = false;
#ifdef RT_OS_DARWIN
    if (OpenReq.fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE)
    {
        char szKrnlVer[256];
        RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szKrnlVer, sizeof(szKrnlVer));
        if (strcmp(szKrnlVer, "10.7.0") >= 0)
        {
            LogRel(("IntNet#%u: Enables the workaround (ip_tos=0) for the little endian ip header checksum problem\n"));
            fWorkaround1 = true;
        }
    }
#endif
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Workaround1", &fWorkaround1, fWorkaround1);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"Workaround1\" value"));
    if (fWorkaround1)
        OpenReq.fFlags |= INTNET_OPEN_FLAGS_WORKAROUND_1;

    LogRel(("IntNet#%u: szNetwork={%s} enmTrunkType=%d szTrunk={%s} fFlags=%#x cbRecv=%u cbSend=%u fIgnoreConnectFailure=%RTbool\n",
            pDrvIns->iInstance, OpenReq.szNetwork, OpenReq.enmTrunkType, OpenReq.szTrunk, OpenReq.fFlags,
            OpenReq.cbRecv, OpenReq.cbSend, fIgnoreConnectFailure));

#ifdef RT_OS_DARWIN
    /* Temporary hack: attach to a network with the name 'if=en0' and you're hitting the wire. */
    if (    !OpenReq.szTrunk[0]
        &&   OpenReq.enmTrunkType == kIntNetTrunkType_None
        &&  !strncmp(pThis->szNetwork, RT_STR_TUPLE("if=en"))
        &&  RT_C_IS_DIGIT(pThis->szNetwork[sizeof("if=en") - 1])
        &&  !pThis->szNetwork[sizeof("if=en")])
    {
        OpenReq.enmTrunkType = kIntNetTrunkType_NetFlt;
        strcpy(OpenReq.szTrunk, &pThis->szNetwork[sizeof("if=") - 1]);
    }
    /* Temporary hack: attach to a network with the name 'wif=en0' and you're on the air. */
    if (    !OpenReq.szTrunk[0]
        &&   OpenReq.enmTrunkType == kIntNetTrunkType_None
        &&  !strncmp(pThis->szNetwork, RT_STR_TUPLE("wif=en"))
        &&  RT_C_IS_DIGIT(pThis->szNetwork[sizeof("wif=en") - 1])
        &&  !pThis->szNetwork[sizeof("wif=en")])
    {
        OpenReq.enmTrunkType = kIntNetTrunkType_NetFlt;
        OpenReq.fFlags |= INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE;
        strcpy(OpenReq.szTrunk, &pThis->szNetwork[sizeof("wif=") - 1]);
    }
#endif /* DARWIN */

    /*
     * Create the event semaphore, S/G cache and xmit critsect.
     */
    rc = RTSemEventCreate(&pThis->hRecvEvt);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTMemCacheCreate(&pThis->hSgCache, sizeof(PDMSCATTERGATHER), 0, UINT32_MAX, NULL, NULL, pThis, 0);
    if (RT_FAILURE(rc))
        return rc;
    rc = PDMDrvHlpCritSectInit(pDrvIns, &pThis->XmitLock, RT_SRC_POS, "IntNetXmit");
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Create the interface.
     */
    if (SUPR3IsDriverless())
    {
#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
        xpc_connection_t hXpcCon = xpc_connection_create(INTNET_R3_SVC_NAME, NULL);
        xpc_connection_set_event_handler(hXpcCon, ^(xpc_object_t hObj) {
            if (xpc_get_type(hObj) == XPC_TYPE_ERROR)
            {
                /** @todo Error handling - reconnecting. */
            }
            else
            {
                /* Out of band messages should only come when there is something to receive. */
                RTSemEventSignal(pThis->hRecvEvt);
            }
        });

        xpc_connection_resume(hXpcCon);
        pThis->hXpcCon      = hXpcCon;
        pThis->fIntNetR3Svc = true;
#else
        /** @todo This is probably not good enough for doing fuzz testing, but later... */
        return PDMDrvHlpVMSetError(pDrvIns, VERR_SUP_DRIVERLESS, RT_SRC_POS,
                                   N_("Cannot attach to '%s' in driverless mode"), pThis->szNetwork);
#endif
    }
    OpenReq.hIf = INTNET_HANDLE_INVALID;
    rc = drvR3IntNetCallSvc(pThis, VMMR0_DO_INTNET_OPEN, &OpenReq, sizeof(OpenReq));
    if (RT_FAILURE(rc))
    {
        if (fIgnoreConnectFailure)
        {
            /*
             * During VM restore it is fatal if the network is not available because the
             * VM settings are locked and the user has no chance to fix network settings.
             * Therefore don't abort but just raise a runtime warning.
             */
            PDMDrvHlpVMSetRuntimeError(pDrvIns, 0 /*fFlags*/, "HostIfNotConnecting",
                                       N_ ("Cannot connect to the network interface '%s'. The virtual "
                                           "network card will appear to work but the guest will not "
                                           "be able to connect. Please choose a different network in the "
                                           "network settings"), OpenReq.szTrunk);

            return VERR_PDM_NO_ATTACHED_DRIVER;
        }
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Failed to open/create the internal network '%s'"), pThis->szNetwork);
    }

    AssertRelease(OpenReq.hIf != INTNET_HANDLE_INVALID);
    pThis->hIf = OpenReq.hIf;
    Log(("IntNet%d: hIf=%RX32 '%s'\n", pDrvIns->iInstance, pThis->hIf, pThis->szNetwork));

    /*
     * Get default buffer.
     */
    rc = drvR3IntNetMapBufferPointers(pThis);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Failed to get ring-3 buffer for the newly created interface to '%s'"), pThis->szNetwork);

    /*
     * Register statistics.
     */
    PDMDrvHlpSTAMRegCounterEx(pDrvIns, &pThis->pBufR3->Recv.cbStatWritten, "Bytes/Received", STAMUNIT_BYTES, "Number of received bytes.");
    PDMDrvHlpSTAMRegCounterEx(pDrvIns, &pThis->pBufR3->Send.cbStatWritten, "Bytes/Sent",     STAMUNIT_BYTES, "Number of sent bytes.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->pBufR3->Recv.cOverflows,    "Overflows/Recv",       "Number overflows.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->pBufR3->Send.cOverflows,    "Overflows/Sent",       "Number overflows.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->pBufR3->Recv.cStatFrames,   "Packets/Received",     "Number of received packets.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->pBufR3->Send.cStatFrames,   "Packets/Sent",         "Number of sent packets.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->StatReceivedGso,            "Packets/Received-Gso", "The GSO portion of the received packets.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->StatSentGso,                "Packets/Sent-Gso",     "The GSO portion of the sent packets.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->StatSentR0,                 "Packets/Sent-R0",      "The ring-0 portion of the sent packets.");

    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->pBufR3->cStatLost,          "Packets/Lost",         "Number of lost packets.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->pBufR3->cStatYieldsNok,     "YieldOk",              "Number of times yielding helped fix an overflow.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->pBufR3->cStatYieldsOk,      "YieldNok",             "Number of times yielding didn't help fix an overflow.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->pBufR3->cStatBadFrames,     "BadFrames",            "Number of bad frames seed by the consumers.");
    PDMDrvHlpSTAMRegProfile(pDrvIns, &pThis->pBufR3->StatSend1,          "Send1",                "Profiling IntNetR0IfSend.");
    PDMDrvHlpSTAMRegProfile(pDrvIns, &pThis->pBufR3->StatSend2,          "Send2",                "Profiling sending to the trunk.");
    PDMDrvHlpSTAMRegProfile(pDrvIns, &pThis->pBufR3->StatRecv1,          "Recv1",                "Reserved for future receive profiling.");
    PDMDrvHlpSTAMRegProfile(pDrvIns, &pThis->pBufR3->StatRecv2,          "Recv2",                "Reserved for future receive profiling.");
    PDMDrvHlpSTAMRegProfile(pDrvIns, &pThis->pBufR3->StatReserved,       "Reserved",             "Reserved for future use.");
#ifdef VBOX_WITH_STATISTICS
    PDMDrvHlpSTAMRegProfileAdv(pDrvIns, &pThis->StatReceive,             "Receive",              "Profiling packet receive runs.");
    PDMDrvHlpSTAMRegProfile(pDrvIns, &pThis->StatTransmit,               "Transmit",             "Profiling packet transmit runs.");
#endif
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->StatXmitWakeupR0,           "XmitWakeup-R0",        "Xmit thread wakeups from ring-0.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->StatXmitWakeupR3,           "XmitWakeup-R3",        "Xmit thread wakeups from ring-3.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->StatXmitProcessRing,        "XmitProcessRing",      "Time xmit thread was told to process the ring.");

    /*
     * Create the async I/O threads.
     * Note! Using a PDM thread here doesn't fit with the IsService=true operation.
     */
    rc = RTThreadCreate(&pThis->hRecvThread, drvR3IntNetRecvThread, pThis, 0,
                        RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "INTNET-RECV");
    if (RT_FAILURE(rc))
    {
        AssertRC(rc);
        return rc;
    }

    rc = SUPSemEventCreate(pThis->pSupDrvSession, &pThis->hXmitEvt);
    AssertRCReturn(rc, rc);

    rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pXmitThread, pThis,
                               drvR3IntNetXmitThread, drvR3IntNetXmitWakeUp, 0, RTTHREADTYPE_IO, "INTNET-XMIT");
    AssertRCReturn(rc, rc);

#ifdef VBOX_WITH_DRVINTNET_IN_R0
    /*
     * Resolve the ring-0 context interface addresses.
     */
    rc = pDrvIns->pHlpR3->pfnLdrGetR0InterfaceSymbols(pDrvIns, &pThis->INetworkUpR0, sizeof(pThis->INetworkUpR0),
                                                      "drvIntNetUp_", PDMINETWORKUP_SYM_LIST);
    AssertLogRelRCReturn(rc, rc);
#endif

    /*
     * Activate data transmission as early as possible
     */
    if (pThis->fActivateEarlyDeactivateLate)
    {
        ASMAtomicXchgSize(&pThis->enmRecvState, RECVSTATE_RUNNING);
        RTSemEventSignal(pThis->hRecvEvt);

        drvR3IntNetUpdateMacAddress(pThis);
        drvR3IntNetSetActive(pThis, true /* fActive */);
    }

    return rc;
}



/**
 * Internal networking transport driver registration record.
 */
const PDMDRVREG g_DrvIntNet =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "IntNet",
    /* szRCMod */
    "VBoxDDRC.rc",
    /* szR0Mod */
    "VBoxDDR0.r0",
    /* pszDescription */
    "Internal Networking Transport Driver",
    /* fFlags */
#ifdef VBOX_WITH_DRVINTNET_IN_R0
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DRVREG_FLAGS_R0,
#else
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
#endif
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVINTNET),
    /* pfnConstruct */
    drvR3IntNetConstruct,
    /* pfnDestruct */
    drvR3IntNetDestruct,
    /* pfnRelocate */
    drvR3IntNetRelocate,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    drvR3IntNetPowerOn,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    drvR3IntNetSuspend,
    /* pfnResume */
    drvR3IntNetResume,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    drvR3IntNetPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

#endif /* IN_RING3 */

