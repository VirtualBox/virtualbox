/* $Id: DrvDedicatedNic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvDedicatedNic - Experimental network driver for using a dedicated (V)NIC.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEFAULT
#include <VBox/log.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/intnet.h>
#include <VBox/intnetinline.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Instance data for the dedicated (V)NIC driver.
 *
 * @implements PDMINETWORKUP
 */
typedef struct DRVDEDICATEDNIC
{
    /** The network interface. */
    PDMINETWORKUP                   INetworkUpR3;
    /** The network interface. */
    R3PTRTYPE(PPDMINETWORKDOWN)     pIAboveNet;
    /** The network config interface.
     * Can (in theory at least) be NULL. */
    R3PTRTYPE(PPDMINETWORKCONFIG)   pIAboveConfigR3;
    /** Pointer to the driver instance. */
    PPDMDRVINSR3                    pDrvInsR3;
    /** Ring-3 base interface for the ring-0 context. */
    PDMIBASER0                      IBaseR0;
    /** Ring-3 base interface for the raw-mode context. */
    PDMIBASERC                      IBaseRC;
    RTR3PTR                         R3PtrAlignment;


    /** The network interface for the ring-0 context. */
    PDMINETWORKUPR0                 INetworkUpR0;
    /** Pointer to the driver instance. */
    PPDMDRVINSR0                    pDrvInsR0;
    RTR0PTR                         R0PtrAlignment;

    /** The interface we're talking to. */
    R0PTRTYPE(PINTNETTRUNKIFPORT)   pIfPortR0;
    /** Set if the link is up, clear if its down. */
    bool                            fLinkDown;
    /** Set if the current transmit operation is done the XMIT thread.  If clear,
     *  we assume its an EMT. */
    bool                            fXmitOnXmitThread;
    /** The name of the interface that we're connected to. */
    char                            szIfName[128 + 8 - 2];

    /** Critical section serializing transmission. */
    PDMCRITSECT                     XmitLock;
    /** The transmit scatter gather buffer (ring-3 -> ring-0). */
    PDMSCATTERGATHER                XmitSg;
    /** The transmit GSO context (when applicable). */
    PDMNETWORKGSO                   XmitGso;
    /** The transmit buffer (ring-3 -> ring-0). */
    uint8_t                         abXmitBuf[_64K];

    /** The receive scatter gather buffer. */
    PDMSCATTERGATHER                RecvSg;
    /** The receive buffer (ring-0 -> ring-3). */
    uint8_t                         abRecvBuf[_64K];

} DRVDEDICATEDNIC;
/** Pointer to the instance data for the dedicated (V)NIC driver. */
typedef DRVDEDICATEDNIC *PDRVDEDICATEDNIC;

/**
 * Ring-0 operations.
 */
typedef enum DRVDEDICATEDNICR0OP
{
    /** Invalid zero value.. */
    DRVDEDICATEDNICR0OP_INVALID = 0,
    /** Initialize the connection to the NIC. */
    DRVDEDICATEDNICR0OP_INIT,
    /** Terminate the connection to the NIC. */
    DRVDEDICATEDNICR0OP_TERM,
    /** Suspend the operation. */
    DRVDEDICATEDNICR0OP_SUSPEND,
    /** Resume the operation. */
    DRVDEDICATEDNICR0OP_RESUME,
    /** Wait for and do receive work.
     * We do this in ring-0 instead of ring-3 to save 1-2 buffer copies and
     * unnecessary context switching. */
    DRVDEDICATEDNICR0OP_RECV,
    /** Wait for and do transmit work.
     * We do this in ring-0 instead of ring-3 to save 1-2 buffer copies and
     * unnecessary context switching. */
    DRVDEDICATEDNICR0OP_SEND,
    /** Changes the promiscuousness of the interface (guest point of view). */
    DRVDEDICATEDNICR0OP_PROMISC,
    /** End of the valid operations. */
    DRVDEDICATEDNICR0OP_END,
    /** The usual 32-bit hack. */
    DRVDEDICATEDNICR0OP_32BIT_HACK = 0x7fffffff
} DRVDEDICATEDNICR0OP;



#ifdef IN_RING0

/**
 * @interface_method_impl{FNPDMDRVREQHANDLERR0}
 */
PDMBOTHCBDECL(int) drvR0DedicatedNicReqHandler(PPDMDRVINS pDrvIns, uint32_t uOperation, uint64_t u64Arg)
{
    RT_NOREF_PV(pDrvIns); RT_NOREF_PV(u64Arg);
    switch ((DRVDEDICATEDNICR0OP)uOperation)
    {
        case DRVDEDICATEDNICR0OP_INIT:
            return VERR_NOT_IMPLEMENTED;//drvR0DedicatedNicReqInit(pDrvIns, u64Arg);

        case DRVDEDICATEDNICR0OP_TERM:
            return VERR_NOT_IMPLEMENTED;//drvR0DedicatedNicReqTerm(pDrvIns);

        case DRVDEDICATEDNICR0OP_SUSPEND:
            return VERR_NOT_IMPLEMENTED;//drvR0DedicatedNicReqSuspend(pDrvIns);

        case DRVDEDICATEDNICR0OP_RESUME:
            return VERR_NOT_IMPLEMENTED;//drvR0DedicatedNicReqResume(pDrvIns);

        case DRVDEDICATEDNICR0OP_RECV:
            return VERR_NOT_IMPLEMENTED;//drvR0DedicatedNicReqRecv(pDrvIns);

        case DRVDEDICATEDNICR0OP_SEND:
            return VERR_NOT_IMPLEMENTED;//drvR0DedicatedNicReqSend(pDrvIns);

        case DRVDEDICATEDNICR0OP_PROMISC:
            return VERR_NOT_IMPLEMENTED;//drvR0DedicatedNicReqPromisc(pDrvIns, !!u64Arg);

        case DRVDEDICATEDNICR0OP_END:
        default:
            return VERR_INVALID_FUNCTION;
    }
}

#endif /* IN_RING0 */



#if 0 /* currently unused */

/* -=-=-=-=- PDMINETWORKUP -=-=-=-=- */

/**
 * @interface_method_impl{PDMINETWORKUP,pfnBeginXmit}
 */
PDMBOTHCBDECL(int) drvDedicatedNicUp_BeginXmit(PPDMINETWORKUP pInterface, bool fOnWorkerThread)
{
    PDRVDEDICATEDNIC pThis = RT_FROM_MEMBER(pInterface, DRVDEDICATEDNIC, CTX_SUFF(INetworkUp));
    int rc = PDMCritSectTryEnter(&pThis->XmitLock);
    if (RT_SUCCESS(rc))
        ASMAtomicUoWriteBool(&pThis->fXmitOnXmitThread, fOnWorkerThread);
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnAllocBuf}
 */
PDMBOTHCBDECL(int) drvDedicatedNicUp_AllocBuf(PPDMINETWORKUP pInterface, size_t cbMin,
                                              PCPDMNETWORKGSO pGso,  PPPDMSCATTERGATHER ppSgBuf)
{
    PDRVDEDICATEDNIC    pThis = RT_FROM_MEMBER(pInterface, DRVDEDICATEDNIC, CTX_SUFF(INetworkUp));
    Assert(PDMCritSectIsOwner(&pThis->XmitLock));

    /*
     * If the net is down, we can return immediately.
     */
    if (pThis->fLinkDown)
        return VERR_NET_DOWN;

#ifdef IN_RING0
    /** @todo Ask the driver for a buffer, atomically if we're called on EMT.  */
    RT_NOREF_PV(cbMin); RT_NOREF_PV(pGso); RT_NOREF_PV(ppSgBuf);
    return VERR_TRY_AGAIN;

#else  /* IN_RING3 */
    /*
     * Are we busy or is the request too big?
     */
    if (RT_UNLIKELY((pThis->XmitSg.fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC))
        return VERR_TRY_AGAIN;
    if (cbMin > sizeof(pThis->abXmitBuf))
        return VERR_NO_MEMORY;

    /*
     * Initialize the S/G buffer and return.
     */
    pThis->XmitSg.fFlags         = PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1;
    pThis->XmitSg.cbUsed         = 0;
    pThis->XmitSg.cbAvailable    = sizeof(pThis->abXmitBuf);
    pThis->XmitSg.pvAllocator    = NULL;
    if (!pGso)
    {
        pThis->XmitSg.pvUser     = NULL;
        pThis->XmitGso.u8Type    = PDMNETWORKGSOTYPE_INVALID;
    }
    else
    {
        pThis->XmitSg.pvUser     = &pThis->XmitGso;
        pThis->XmitGso           = *pGso;
    }
    pThis->XmitSg.cSegs          = 1;
    pThis->XmitSg.aSegs[0].cbSeg = pThis->XmitSg.cbAvailable;
    pThis->XmitSg.aSegs[0].pvSeg = &pThis->abXmitBuf[0];

# if 0 /* poison */
    memset(pThis->XmitSg.aSegs[0].pvSeg, 'F', pThis->XmitSg.aSegs[0].cbSeg);
# endif

    *ppSgBuf = &pThis->XmitSg;
    return VINF_SUCCESS;
#endif /* IN_RING3 */
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnFreeBuf}
 */
PDMBOTHCBDECL(int) drvDedicatedNicUp_FreeBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf)
{
#ifdef VBOX_STRICT
    PDRVDEDICATEDNIC  pThis = RT_FROM_MEMBER(pInterface, DRVDEDICATEDNIC, CTX_SUFF(INetworkUp));
    Assert(pSgBuf->fFlags == (PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1));
    Assert(pSgBuf->cbUsed <= pSgBuf->cbAvailable);
    Assert(PDMCritSectIsOwner(&pThis->XmitLock));
#else
    RT_NOREF1(pInterface);
#endif

    if (pSgBuf)
    {
#ifdef IN_RING0
        // ...
#else
        Assert(pSgBuf == &pThis->XmitSg);
        Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
        pSgBuf->fFlags = 0;
#endif
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSendBuf}
 */
PDMBOTHCBDECL(int) drvDedicatedNicUp_SendBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    PDRVDEDICATEDNIC  pThis = RT_FROM_MEMBER(pInterface, DRVDEDICATEDNIC, CTX_SUFF(INetworkUp));
    STAM_PROFILE_START(&pThis->StatTransmit, a);

    AssertPtr(pSgBuf);
    Assert(pSgBuf->fFlags == (PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1));
    Assert(pSgBuf->cbUsed <= pSgBuf->cbAvailable);
#ifdef IN_RING0
    Assert(pSgBuf == &pThis->XmitSg);
#endif
    Assert(PDMCritSectIsOwner(&pThis->XmitLock));

#ifdef IN_RING0
    /*
     * Tell the driver to send the packet.
     */
    RT_NOREF_PV(pThis); RT_NOREF_PV(pSgBuf); RT_NOREF_PV(fOnWorkerThread);
    return VERR_INTERNAL_ERROR_4;

#else  /* IN_RING3 */
    NOREF(fOnWorkerThread);

    /*
     * Call ring-0 to start the transfer.
     */
    int rc = PDMDrvHlpCallR0(pThis->pDrvInsR3, DRVDEDICATEDNICR0OP_SEND, pSgBuf->cbUsed);
    if (RT_FAILURE(rc) && rc != VERR_NET_DOWN)
        rc = VERR_NET_NO_BUFFER_SPACE;
    pSgBuf->fFlags = 0;
    return rc;
#endif /* IN_RING3 */
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnEndXmit}
 */
PDMBOTHCBDECL(void) drvDedicatedNicUp_EndXmit(PPDMINETWORKUP pInterface)
{
    PDRVDEDICATEDNIC pThis = RT_FROM_MEMBER(pInterface, DRVDEDICATEDNIC, CTX_SUFF(INetworkUp));
    ASMAtomicUoWriteBool(&pThis->fXmitOnXmitThread, false);
    PDMCritSectLeave(&pThis->XmitLock);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSetPromiscuousMode}
 */
PDMBOTHCBDECL(void) drvDedicatedNicUp_SetPromiscuousMode(PPDMINETWORKUP pInterface, bool fPromiscuous)
{
    PDRVDEDICATEDNIC pThis = RT_FROM_MEMBER(pInterface, DRVDEDICATEDNIC, CTX_SUFF(INetworkUp));
    /** @todo enable/disable promiscuous mode (should be easy) */
    NOREF(pThis); RT_NOREF_PV(fPromiscuous);
}

#endif /* unused */
#ifdef IN_RING3
# if 0 /* currently unused */

/**
 * @interface_method_impl{PDMINETWORKUP,pfnNotifyLinkChanged}
 */
static DECLCALLBACK(void) drvR3DedicatedNicUp_NotifyLinkChanged(PPDMINETWORKUP pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    PDRVDEDICATEDNIC pThis = RT_FROM_MEMBER(pInterface, DRVDEDICATEDNIC, CTX_SUFF(INetworkUp));
    bool fLinkDown;
    switch (enmLinkState)
    {
        case PDMNETWORKLINKSTATE_DOWN:
        case PDMNETWORKLINKSTATE_DOWN_RESUME:
            fLinkDown = true;
            break;
        default:
            AssertMsgFailed(("enmLinkState=%d\n", enmLinkState));
        case PDMNETWORKLINKSTATE_UP:
            fLinkDown = false;
            break;
    }
    LogFlow(("drvR3DedicatedNicUp_NotifyLinkChanged: enmLinkState=%d %d->%d\n", enmLinkState, pThis->fLinkDown, fLinkDown));
    ASMAtomicWriteBool(&pThis->fLinkDown, fLinkDown);
}


/* -=-=-=-=- PDMIBASER0 -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASER0,pfnQueryInterface}
 */
static DECLCALLBACK(RTR0PTR) drvR3DedicatedNicIBaseR0_QueryInterface(PPDMIBASER0 pInterface, const char *pszIID)
{
    PDRVDEDICATEDNIC pThis = RT_FROM_MEMBER(pInterface, DRVDEDICATEDNIC, IBaseR0);
    PDMIBASER0_RETURN_INTERFACE(pThis->pDrvInsR3, pszIID, PDMINETWORKUP, &pThis->INetworkUpR0);
    return NIL_RTR0PTR;
}


/* -=-=-=-=- PDMIBASE -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvR3DedicatedNicIBase_QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVDEDICATEDNIC pThis   = PDMINS_2_DATA(pDrvIns, PDRVDEDICATEDNIC);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASER0, &pThis->IBaseR0);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKUP, &pThis->INetworkUpR3);
    return NULL;
}

# endif /* Currently unused */


/* -=-=-=-=- PDMDRVREG -=-=-=-=- */

/**
 * @interface_method_impl{PDMDRVREG,pfnPowerOff}
 */
static DECLCALLBACK(void) drvR3DedicatedNicPowerOff(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3DedicatedNicPowerOff\n"));
    int rc = PDMDrvHlpCallR0(pDrvIns, DRVDEDICATEDNICR0OP_SUSPEND, 0);
    AssertRC(rc);
}


/**
 * @interface_method_impl{PDMDRVREG,pfnResume}
 */
static DECLCALLBACK(void) drvR3DedicatedNicResume(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3DedicatedNicPowerResume\n"));
    int rc = PDMDrvHlpCallR0(pDrvIns, DRVDEDICATEDNICR0OP_RESUME, 0);
    AssertRC(rc);
}


/**
 * @interface_method_impl{PDMDRVREG,pfnSuspend}
 */
static DECLCALLBACK(void) drvR3DedicatedNicSuspend(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3DedicatedNicPowerSuspend\n"));
    int rc = PDMDrvHlpCallR0(pDrvIns, DRVDEDICATEDNICR0OP_SUSPEND, 0);
    AssertRC(rc);
}


/**
 * @interface_method_impl{PDMDRVREG,pfnPowerOn}
 */
static DECLCALLBACK(void) drvR3DedicatedNicPowerOn(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3DedicatedNicPowerOn\n"));
    int rc = PDMDrvHlpCallR0(pDrvIns, DRVDEDICATEDNICR0OP_RESUME, 0);
    AssertRC(rc);
}


/**
 * @interface_method_impl{PDMDRVREG,pfnDestruct}
 */
static DECLCALLBACK(void) drvR3DedicatedNicDestruct(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvR3DedicatedNicDestruct\n"));
    PDRVDEDICATEDNIC pThis = PDMINS_2_DATA(pDrvIns, PDRVDEDICATEDNIC);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    if (pThis->pIfPortR0)
    {
        int rc = PDMDrvHlpCallR0(pDrvIns, DRVDEDICATEDNICR0OP_TERM, 0);
        AssertRC(rc);;
    }
}


/**
 * @interface_method_impl{PDMDRVREG,pfnConstruct}
 */
static DECLCALLBACK(int) drvR3DedicatedNicConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(pCfg, fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVDEDICATEDNIC pThis = PDMINS_2_DATA(pDrvIns, PDRVDEDICATEDNIC);

    /*
     * Init the static parts.
     */
    pThis->pDrvInsR3                                = pDrvIns;
    pThis->pDrvInsR0                                = PDMDRVINS_2_R0PTR(pDrvIns);
#if 0
    pThis->hRecvThread                              = NIL_RTTHREAD;
    pThis->hRecvEvt                                 = NIL_RTSEMEVENT;
    pThis->pXmitThread                              = NULL;
    pThis->hXmitEvt                                 = NIL_SUPSEMEVENT;
    pThis->pSupDrvSession                           = PDMDrvHlpGetSupDrvSession(pDrvIns);
    pThis->hSgCache                                 = NIL_RTMEMCACHE;
    pThis->enmRecvState                             = RECVSTATE_SUSPENDED;
    pThis->fActivateEarlyDeactivateLate             = false;
    /* IBase* */
    pDrvIns->IBase.pfnQueryInterface                = drvR3DedicatedNicIBase_QueryInterface;
    pThis->IBaseR0.pfnQueryInterface                = drvR3DedicatedNicIBaseR0_QueryInterface;
    pThis->IBaseRC.pfnQueryInterface                = drvR3DedicatedNicIBaseRC_QueryInterface;
    /* INetworkUp */
    pThis->INetworkUpR3.pfnBeginXmit                = drvDedicatedNic_BeginXmit;
    pThis->INetworkUpR3.pfnAllocBuf                 = drvDedicatedNic_AllocBuf;
    pThis->INetworkUpR3.pfnFreeBuf                  = drvDedicatedNic_FreeBuf;
    pThis->INetworkUpR3.pfnSendBuf                  = drvDedicatedNic_SendBuf;
    pThis->INetworkUpR3.pfnEndXmit                  = drvDedicatedNic_EndXmit;
    pThis->INetworkUpR3.pfnSetPromiscuousMode       = drvDedicatedNic_SetPromiscuousMode;
    pThis->INetworkUpR3.pfnNotifyLinkChanged        = drvR3DedicatedNicUp_NotifyLinkChanged;
#endif

    /** @todo
     * Need to create a generic way of calling into the ring-0 side of the driver so
     * we can initialize the thing as well as send and receive.  Hmm ... the
     * sending could be done more efficiently from a ring-0 kernel thread actually
     * (saves context switching and 1-2 copy operations).  Ditto for receive, except
     * we need to tie the thread to the process or we cannot access the guest ram so
     * easily.
     */

    return VERR_NOT_IMPLEMENTED;
}



/**
 * Internal networking transport driver registration record.
 */
const PDMDRVREG g_DrvDedicatedNic =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "DedicatedNic",
    /* szRCMod */
    "",
    /* szR0Mod */
    "VBoxDDR0.r0",
    /* pszDescription */
    "Dedicated (V)NIC Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DRVREG_FLAGS_R0,
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVDEDICATEDNIC),
    /* pfnConstruct */
    drvR3DedicatedNicConstruct,
    /* pfnDestruct */
    drvR3DedicatedNicDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    drvR3DedicatedNicPowerOn,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    drvR3DedicatedNicSuspend,
    /* pfnResume */
    drvR3DedicatedNicResume,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    drvR3DedicatedNicPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

#endif /* IN_RING3 */

