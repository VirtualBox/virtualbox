/* $Id: VUSBUrb.cpp 110377 2025-07-23 12:22:40Z michal.necasek@oracle.com $ */
/** @file
 * Virtual USB - URBs.
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
#define LOG_GROUP LOG_GROUP_DRV_VUSB
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/err.h>
#include <iprt/alloc.h>
#include <VBox/log.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/env.h>
#include "VUSBInternal.h"



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Strings for the CTLSTAGE enum values. */
const char * const g_apszCtlStates[4] =
{
    "SETUP",
    "DATA",
    "STATUS",
    "N/A"
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Complete a SETUP stage URB.
 *
 * This is used both for dev2host and host2dev kind of transfers.
 * It is used by both the sync and async control paths.
 */
static void vusbMsgSetupCompletion(PVUSBURB pUrb)
{
    PVUSBDEV        pDev   = pUrb->pVUsb->pDev;
    PVUSBPIPE       pPipe  = &pDev->aPipes[pUrb->EndPt];
    PVUSBCTRLEXTRA  pExtra = pPipe->pCtrl;
    PVUSBSETUP      pSetup = pExtra->pMsg;

    LogFlow(("%s: vusbMsgSetupCompletion: cbData=%d wLength=%#x cbLeft=%d pPipe=%p stage %s->DATA\n",
             pUrb->pszDesc, pUrb->cbData, pSetup->wLength, pExtra->cbLeft, pPipe, g_apszCtlStates[pExtra->enmStage])); NOREF(pSetup);
    pExtra->enmStage = CTLSTAGE_DATA;
    pUrb->enmStatus  = VUSBSTATUS_OK;
}

/**
 * Complete a DATA stage URB.
 *
 * This is used both for dev2host and host2dev kind of transfers.
 * It is used by both the sync and async control paths.
 */
static void vusbMsgDataCompletion(PVUSBURB pUrb)
{
    PVUSBDEV        pDev   = pUrb->pVUsb->pDev;
    PVUSBPIPE       pPipe  = &pDev->aPipes[pUrb->EndPt];
    PVUSBCTRLEXTRA  pExtra = pPipe->pCtrl;
    PVUSBSETUP      pSetup = pExtra->pMsg;

    LogFlow(("%s: vusbMsgDataCompletion: cbData=%d wLength=%#x cbLeft=%d pPipe=%p stage DATA\n",
             pUrb->pszDesc, pUrb->cbData, pSetup->wLength, pExtra->cbLeft, pPipe)); NOREF(pSetup);

    pUrb->enmStatus = VUSBSTATUS_OK;
}

/**
 * Complete a STATUS stage URB.
 *
 * This is used both for dev2host and host2dev kind of transfers.
 * It is used by both the sync and async control paths.
 */
static void vusbMsgStatusCompletion(PVUSBURB pUrb)
{
    PVUSBDEV        pDev = pUrb->pVUsb->pDev;
    PVUSBPIPE       pPipe = &pDev->aPipes[pUrb->EndPt];
    PVUSBCTRLEXTRA  pExtra = pPipe->pCtrl;

    if (pExtra->fOk)
    {
        /*
         * vusbDevStdReqSetAddress requests are deferred.
         */
        if (pDev->u8NewAddress != VUSB_INVALID_ADDRESS)
        {
            vusbDevSetAddress(pDev, pDev->u8NewAddress);
            pDev->u8NewAddress = VUSB_INVALID_ADDRESS;
        }

        LogFlow(("%s: vusbMsgStatusCompletion: pDev=%p[%s] pPipe=%p err=OK stage %s->SETUP\n",
                 pUrb->pszDesc, pDev, pDev->pUsbIns->pszName, pPipe, g_apszCtlStates[pExtra->enmStage]));
        pUrb->enmStatus = VUSBSTATUS_OK;
    }
    else
    {
        LogFlow(("%s: vusbMsgStatusCompletion: pDev=%p[%s] pPipe=%p err=STALL stage %s->SETUP\n",
                 pUrb->pszDesc, pDev, pDev->pUsbIns->pszName, pPipe, g_apszCtlStates[pExtra->enmStage]));
        pUrb->enmStatus = VUSBSTATUS_STALL;
    }

    /*
     * Done with this message sequence.
     */
    pExtra->pbCur    = NULL;
    pExtra->enmStage = CTLSTAGE_SETUP;
}

/**
 * This is a worker function for vusbMsgCompletion and
 * vusbMsgSubmitSynchronously used to complete the original URB.
 *
 * @param   pUrb    The URB originating from the HCI.
 */
static void vusbCtrlCompletion(PVUSBURB pUrb)
{
    PVUSBDEV        pDev = pUrb->pVUsb->pDev;
    PVUSBPIPE       pPipe = &pDev->aPipes[pUrb->EndPt];
    PVUSBCTRLEXTRA  pExtra = pPipe->pCtrl;
    LogFlow(("%s: vusbCtrlCompletion: pDev=%p[%s]\n", pUrb->pszDesc, pDev, pDev->pUsbIns->pszName));

    switch (pExtra->enmStage)
    {
        case CTLSTAGE_SETUP:
            vusbMsgSetupCompletion(pUrb);
            break;
        case CTLSTAGE_DATA:
            vusbMsgDataCompletion(pUrb);
            break;
        case CTLSTAGE_STATUS:
            vusbMsgStatusCompletion(pUrb);
            break;
    }
}

/**
 * Called from vusbUrbCompletionRh when it encounters a
 * message type URB.
 *
 * @param   pUrb    The URB within the control pipe extra state data.
 */
static void vusbMsgCompletion(PVUSBURB pUrb)
{
    PVUSBDEV        pDev   = pUrb->pVUsb->pDev;
    PVUSBPIPE       pPipe  = &pDev->aPipes[pUrb->EndPt];

    RTCritSectEnter(&pPipe->CritSectCtrl);
    PVUSBCTRLEXTRA  pExtra = pPipe->pCtrl;

#ifdef LOG_ENABLED
    LogFlow(("%s: vusbMsgCompletion: pDev=%p[%s]\n", pUrb->pszDesc, pDev, pDev->pUsbIns->pszName));
    vusbUrbTrace(pUrb, "vusbMsgCompletion", true);
#endif
    Assert(&pExtra->Urb == pUrb);


    if (pUrb->enmStatus == VUSBSTATUS_OK)
        pExtra->fOk = true;
    else
        pExtra->fOk = false;
    pExtra->cbLeft = pUrb->cbData - sizeof(VUSBSETUP);

    /*
     * Complete the original URB.
     */
    PVUSBURB pCtrlUrb = pUrb->pVUsb->pCtrlUrb;
    pCtrlUrb->enmState = VUSBURBSTATE_REAPED;
    vusbCtrlCompletion(pCtrlUrb);

    /*
     * 'Free' the message URB, i.e. put it back to the allocated state.
     */
    Assert(   pUrb->enmState == VUSBURBSTATE_REAPED
           || pUrb->enmState == VUSBURBSTATE_CANCELLED);
    if (pUrb->enmState != VUSBURBSTATE_CANCELLED)
    {
        pUrb->enmState = VUSBURBSTATE_ALLOCATED;
        pUrb->fCompleting = false;
    }
    RTCritSectLeave(&pPipe->CritSectCtrl);

    /* Complete the original control URB on the root hub now. */
    vusbUrbCompletionRh(pCtrlUrb);
}

/**
 * Deal with URB errors, talking thru the RH to the HCI.
 *
 * @returns true if it could be retried.
 * @returns false if it should be completed with failure.
 * @param   pRh     The roothub the URB originated from.
 * @param   pUrb    The URB in question.
 */
int vusbUrbErrorRhEx(PVUSBROOTHUB pRh, PVUSBURB pUrb)
{
    PVUSBDEV pDev = pUrb->pVUsb->pDev;
    LogFlow(("%s: vusbUrbErrorRh: pDev=%p[%s] rh=%p\n", pUrb->pszDesc, pDev, pDev && pDev->pUsbIns ? pDev->pUsbIns->pszName : "", pRh));
    RT_NOREF(pDev);
    return pRh->pIRhPort->pfnXferError(pRh->pIRhPort, pUrb);
}

/**
 * Does URB completion on roothub level.
 *
 * @param   pRh     The roothub the URB originated from.
 * @param   pUrb    The URB to complete.
 */
void vusbUrbCompletionRhEx(PVUSBROOTHUB pRh, PVUSBURB pUrb)
{
    LogFlow(("%s: vusbUrbCompletionRh: type=%s status=%s\n",
             pUrb->pszDesc, vusbUrbTypeName(pUrb->enmType), vusbUrbStatusName(pUrb->enmStatus)));
    AssertMsg(   pUrb->enmState == VUSBURBSTATE_REAPED
              || pUrb->enmState == VUSBURBSTATE_CANCELLED, ("%d\n", pUrb->enmState));

    if (   pUrb->pVUsb->pDev
        && pUrb->pVUsb->pDev->hSniffer)
    {
        int rc = VUSBSnifferRecordEvent(pUrb->pVUsb->pDev->hSniffer, pUrb,
                                          pUrb->enmStatus == VUSBSTATUS_OK
                                        ? VUSBSNIFFEREVENT_COMPLETE
                                        : VUSBSNIFFEREVENT_ERROR_COMPLETE);
        if (RT_FAILURE(rc))
            LogRel(("VUSB: Capturing URB completion event failed with %Rrc\n", rc));
    }

    /* If there is a sniffer on the roothub record the completed URB there too. */
    if (pRh->hSniffer != VUSBSNIFFER_NIL)
    {
        int rc = VUSBSnifferRecordEvent(pRh->hSniffer, pUrb,
                                          pUrb->enmStatus == VUSBSTATUS_OK
                                        ? VUSBSNIFFEREVENT_COMPLETE
                                        : VUSBSNIFFEREVENT_ERROR_COMPLETE);
        if (RT_FAILURE(rc))
            LogRel(("VUSB: Capturing URB completion event on the root hub failed with %Rrc\n", rc));
    }

#ifdef VBOX_WITH_STATISTICS
    /*
     * Total and per-type submit statistics.
     */
    if (pUrb->enmType != VUSBXFERTYPE_MSG)
    {
        Assert(pUrb->enmType >= 0 && pUrb->enmType < (int)RT_ELEMENTS(pRh->aTypes));

        if (    pUrb->enmStatus == VUSBSTATUS_OK
            ||  pUrb->enmStatus == VUSBSTATUS_DATA_UNDERRUN
            ||  pUrb->enmStatus == VUSBSTATUS_DATA_OVERRUN)
        {
            if (pUrb->enmType == VUSBXFERTYPE_ISOC)
            {
                for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
                {
                    const unsigned cb = pUrb->aIsocPkts[i].cb;
                    if (cb)
                    {
                        STAM_COUNTER_ADD(&pRh->Total.StatActBytes, cb);
                        STAM_COUNTER_ADD(&pRh->aTypes[VUSBXFERTYPE_ISOC].StatActBytes, cb);
                        STAM_COUNTER_ADD(&pRh->aStatIsocDetails[i].Bytes, cb);
                        if (pUrb->enmDir == VUSBDIRECTION_IN)
                        {
                            STAM_COUNTER_ADD(&pRh->Total.StatActReadBytes, cb);
                            STAM_COUNTER_ADD(&pRh->aTypes[VUSBXFERTYPE_ISOC].StatActReadBytes, cb);
                        }
                        else
                        {
                            STAM_COUNTER_ADD(&pRh->Total.StatActWriteBytes, cb);
                            STAM_COUNTER_ADD(&pRh->aTypes[VUSBXFERTYPE_ISOC].StatActWriteBytes, cb);
                        }
                        STAM_COUNTER_INC(&pRh->StatIsocActPkts);
                        STAM_COUNTER_INC(&pRh->StatIsocActReadPkts);
                    }
                    STAM_COUNTER_INC(&pRh->aStatIsocDetails[i].Pkts);
                    switch (pUrb->aIsocPkts[i].enmStatus)
                    {
                        case VUSBSTATUS_OK:
                            if (cb)                     STAM_COUNTER_INC(&pRh->aStatIsocDetails[i].Ok);
                            else                        STAM_COUNTER_INC(&pRh->aStatIsocDetails[i].Ok0);
                            break;
                        case VUSBSTATUS_DATA_UNDERRUN:
                            if (cb)                     STAM_COUNTER_INC(&pRh->aStatIsocDetails[i].DataUnderrun);
                            else                        STAM_COUNTER_INC(&pRh->aStatIsocDetails[i].DataUnderrun0);
                            break;
                        case VUSBSTATUS_DATA_OVERRUN:   STAM_COUNTER_INC(&pRh->aStatIsocDetails[i].DataOverrun); break;
                        case VUSBSTATUS_NOT_ACCESSED:   STAM_COUNTER_INC(&pRh->aStatIsocDetails[i].NotAccessed); break;
                        default:                        STAM_COUNTER_INC(&pRh->aStatIsocDetails[i].Misc); break;
                    }
                }
            }
            else
            {
                STAM_COUNTER_ADD(&pRh->Total.StatActBytes, pUrb->cbData);
                STAM_COUNTER_ADD(&pRh->aTypes[pUrb->enmType].StatActBytes, pUrb->cbData);
                if (pUrb->enmDir == VUSBDIRECTION_IN)
                {
                    STAM_COUNTER_ADD(&pRh->Total.StatActReadBytes, pUrb->cbData);
                    STAM_COUNTER_ADD(&pRh->aTypes[pUrb->enmType].StatActReadBytes, pUrb->cbData);
                }
                else
                {
                    STAM_COUNTER_ADD(&pRh->Total.StatActWriteBytes, pUrb->cbData);
                    STAM_COUNTER_ADD(&pRh->aTypes[pUrb->enmType].StatActWriteBytes, pUrb->cbData);
                }
            }
        }
        else
        {
            /* (Note. this also counts the cancelled packets) */
            STAM_COUNTER_INC(&pRh->Total.StatUrbsFailed);
            STAM_COUNTER_INC(&pRh->aTypes[pUrb->enmType].StatUrbsFailed);
        }
    }
#endif /* VBOX_WITH_STATISTICS */

    /*
     * Msg transfers are special virtual transfers associated with
     * vusb, not the roothub
     */
    switch (pUrb->enmType)
    {
        case VUSBXFERTYPE_MSG:
            vusbMsgCompletion(pUrb);
            return;
        case VUSBXFERTYPE_ISOC:
            /* Don't bother with error callback for isochronous URBs. */
            break;

#if 1   /** @todo r=bird: OHCI say ''If the Transfer Descriptor is being
         * retired because of an error, the Host Controller must update
         * the Halt bit of the Endpoint Descriptor.''
         *
         * So, I'll subject all transfertypes to the same halt stuff now. It could
         * just happen to fix the logitech disconnect trap in win2k.
         */
        default:
#endif
        case VUSBXFERTYPE_BULK:
            if (pUrb->enmStatus != VUSBSTATUS_OK)
                vusbUrbErrorRhEx(pRh, pUrb);
            break;
    }
#ifdef LOG_ENABLED
    vusbUrbTrace(pUrb, "vusbUrbCompletionRh", true);
#endif

    pRh->pIRhPort->pfnXferCompletion(pRh->pIRhPort, pUrb);
    if (pUrb->enmState == VUSBURBSTATE_REAPED)
    {
        LogFlow(("%s: vusbUrbCompletionRh: Freeing URB\n", pUrb->pszDesc));
        pUrb->pVUsb->pfnFree(pUrb);
    }

    vusbRhR3ProcessFrame(pRh, true /* fCallback */);
}


/**
 * Certain control requests must not ever be forwarded to the device because
 * they are required by the vusb core in order to maintain the vusb internal
 * data structures.
 */
DECLINLINE(bool) vusbUrbIsRequestSafe(PCVUSBSETUP pSetup, PVUSBURB pUrb)
{
    if ((pSetup->bmRequestType & VUSB_REQ_MASK) != VUSB_REQ_STANDARD)
        return true;

    switch (pSetup->bRequest)
    {
        case VUSB_REQ_CLEAR_FEATURE:
            return  pUrb->EndPt != 0                   /* not default control pipe */
                ||  pSetup->wValue != 0                /* not ENDPOINT_HALT */
                ||  !pUrb->pVUsb->pDev->pUsbIns->pReg->pfnUsbClearHaltedEndpoint; /* not special need for backend */
        case VUSB_REQ_SET_ADDRESS:
        case VUSB_REQ_SET_CONFIGURATION:
        case VUSB_REQ_GET_CONFIGURATION:
        case VUSB_REQ_SET_INTERFACE:
        case VUSB_REQ_GET_INTERFACE:
            return false;

        /*
         * If the device wishes it, we'll use the cached device and
         * configuration descriptors.  (We return false when we want to use the
         * cache. Yeah, it's a bit weird to read.)
         */
        case VUSB_REQ_GET_DESCRIPTOR:
            return !vusbDevIsDescriptorInCache(pUrb->pVUsb->pDev, pSetup);

        default:
            return true;
    }
}


/**
 * Queues an URB for asynchronous transfer.
 * A list of asynchronous URBs is kept by the roothub.
 *
 * @returns VBox status code (from pfnUrbQueue).
 * @param   pUrb    The URB.
 */
int vusbUrbQueueAsyncRh(PVUSBURB pUrb)
{
#ifdef LOG_ENABLED
    vusbUrbTrace(pUrb, "vusbUrbQueueAsyncRh", false);
#endif

    /* Immediately return in case of error.
     * XXX There is still a race: The Rh might vanish after this point! */
    PVUSBDEV pDev = pUrb->pVUsb->pDev;
    PVUSBROOTHUB pRh = vusbDevGetRh(pDev);
    if (!pRh)
    {
        Log(("vusbUrbQueueAsyncRh returning VERR_OBJECT_DESTROYED\n"));
        return VERR_OBJECT_DESTROYED;
    }

    RTCritSectEnter(&pDev->CritSectAsyncUrbs);
    int rc = pDev->pUsbIns->pReg->pfnUrbQueue(pDev->pUsbIns, pUrb);
    if (RT_FAILURE(rc))
    {
        LogFlow(("%s: vusbUrbQueueAsyncRh: returns %Rrc (queue_urb)\n", pUrb->pszDesc, rc));
        RTCritSectLeave(&pDev->CritSectAsyncUrbs);
        return rc;
    }

    ASMAtomicIncU32(&pDev->aPipes[pUrb->EndPt].async);

    /* Queue the Urb on the roothub */
    RTListAppend(&pDev->LstAsyncUrbs, &pUrb->pVUsb->NdLst);
    RTCritSectLeave(&pDev->CritSectAsyncUrbs);

    return VINF_SUCCESS;
}


/**
 * Send a control message *synchronously*.
 */
static void vusbMsgSubmitSynchronously(PVUSBURB pUrb, bool fSafeRequest)
{
    PVUSBDEV        pDev   = pUrb->pVUsb->pDev;
    Assert(pDev);
    PVUSBPIPE       pPipe  = &pDev->aPipes[pUrb->EndPt];
    PVUSBCTRLEXTRA  pExtra = pPipe->pCtrl;
    PVUSBSETUP      pSetup = pExtra->pMsg;
    LogFlow(("%s: vusbMsgSubmitSynchronously: pDev=%p[%s]\n", pUrb->pszDesc, pDev, pDev->pUsbIns ? pDev->pUsbIns->pszName : ""));

    uint8_t *pbData = (uint8_t *)pExtra->pMsg + sizeof(*pSetup);
    uint32_t cbData = pSetup->wLength;
    bool    fOk = false;
    if (!fSafeRequest)
        fOk = vusbDevStandardRequest(pDev, pUrb->EndPt, pSetup, pbData, &cbData);
    else
        AssertMsgFailed(("oops\n"));

    pUrb->enmState = VUSBURBSTATE_REAPED;
    if (fOk)
    {
        pSetup->wLength = cbData;
        pUrb->enmStatus = VUSBSTATUS_OK;
        pExtra->fOk = true;
    }
    else
    {
        pUrb->enmStatus = VUSBSTATUS_STALL;
        pExtra->fOk = false;
    }
    pExtra->cbLeft = cbData; /* used by IN only */

    vusbCtrlCompletion(pUrb);
    vusbUrbCompletionRh(pUrb);

    /*
     * 'Free' the message URB, i.e. put it back to the allocated state.
     */
    pExtra->Urb.enmState = VUSBURBSTATE_ALLOCATED;
    pExtra->Urb.fCompleting = false;
}

/**
 * Callback for dealing with device reset.
 */
void vusbMsgResetExtraData(PVUSBCTRLEXTRA pExtra)
{
    if (!pExtra)
        return;
    pExtra->enmStage = CTLSTAGE_SETUP;
    if (pExtra->Urb.enmState != VUSBURBSTATE_CANCELLED)
    {
        pExtra->Urb.enmState = VUSBURBSTATE_ALLOCATED;
        pExtra->Urb.fCompleting = false;
    }
}


/**
 * Callback to free a cancelled message URB.
 *
 * This is yet another place we're we have to performance acrobatics to
 * deal with cancelled URBs. sigh.
 *
 * The deal here is that we never free message URBs since they are integrated
 * into the message pipe state. But since cancel can leave URBs unreaped and in
 * a state which require them not to be freed, we'll have to do two things.
 * First, if a new message URB is processed we'll have to get a new message
 * pipe state. Second, we cannot just free the damn state structure because
 * that might lead to heap corruption since it might still be in-flight.
 *
 * The URB embedded into the message pipe control structure will start in an
 * ALLOCATED state. When submitted it will be go to the IN-FLIGHT state. When
 * reaped it will go from REAPED to ALLOCATED. When completed in the CANCELLED
 * state it will remain in that state (as does normal URBs).
 *
 * If a new message urb comes up while it's in the CANCELLED state, we will
 * orphan it and it will be freed here in vusbMsgFreeUrb. We indicate this
 * by setting pVUsb->pvFreeCtx to NULL.
 *
 * If we have to free the message state structure because of device destruction,
 * configuration changes, or similar, we will orphan the message pipe state in
 * the same way by setting pVUsb->pvFreeCtx to NULL and let this function free it.
 *
 * @param   pUrb
 */
static DECLCALLBACK(void) vusbMsgFreeUrb(PVUSBURB pUrb)
{
    vusbUrbAssert(pUrb);
    PVUSBCTRLEXTRA pExtra = (PVUSBCTRLEXTRA)((uint8_t *)pUrb - RT_UOFFSETOF(VUSBCTRLEXTRA, Urb));
    if (    pUrb->enmState == VUSBURBSTATE_CANCELLED
        &&  !pUrb->pVUsb->pvFreeCtx)
    {
        LogFlow(("vusbMsgFreeUrb: Freeing orphan: %p (pUrb=%p)\n", pExtra, pUrb));
        RTMemFree(pExtra);
    }
    else
    {
        Assert(pUrb->pVUsb->pvFreeCtx == &pExtra->Urb);
        pUrb->enmState = VUSBURBSTATE_ALLOCATED;
        pUrb->fCompleting = false;
    }
}

/**
 * Frees the extra state data associated with a message pipe.
 *
 * @param   pExtra      The data.
 */
void vusbMsgFreeExtraData(PVUSBCTRLEXTRA pExtra)
{
    if (!pExtra)
        return;
    if (pExtra->Urb.enmState != VUSBURBSTATE_CANCELLED)
    {
        pExtra->Urb.u32Magic = 0;
        pExtra->Urb.enmState = VUSBURBSTATE_FREE;
        if (pExtra->Urb.pszDesc)
            RTStrFree(pExtra->Urb.pszDesc);
        RTMemFree(pExtra);
    }
    else
        pExtra->Urb.pVUsb->pvFreeCtx = NULL; /* see vusbMsgFreeUrb */
}

/**
 * Allocates the extra state data required for a control pipe.
 *
 * @returns Pointer to the allocated and initialized state data.
 * @returns NULL on out of memory condition.
 * @param   pUrb    A URB we can copy default data from.
 */
static PVUSBCTRLEXTRA vusbMsgAllocExtraData(PVUSBURB pUrb)
{
/** @todo reuse these? */
    PVUSBCTRLEXTRA pExtra;
    /* The initial allocation tries to balance wasted memory versus the need to re-allocate
     * the message data. Experience shows that an 8K initial allocation in practice never needs
     * to be expanded but almost certainly wastes 4K or more memory.
     */
    const size_t cbMax = _2K + sizeof(VUSBSETUP);
    pExtra = (PVUSBCTRLEXTRA)RTMemAllocZ(RT_UOFFSETOF_DYN(VUSBCTRLEXTRA, Urb.abData[cbMax]));
    if (pExtra)
    {
        pExtra->enmStage = CTLSTAGE_SETUP;
        //pExtra->fOk = false;
        pExtra->pMsg = (PVUSBSETUP)pExtra->Urb.abData;
        pExtra->pbCur = (uint8_t *)(pExtra->pMsg + 1);
        //pExtra->cbLeft = 0;
        pExtra->cbMax = cbMax;

        //pExtra->Urb.Dev.pvProxyUrb = NULL;
        pExtra->Urb.u32Magic = VUSBURB_MAGIC;
        pExtra->Urb.enmState = VUSBURBSTATE_ALLOCATED;
        pExtra->Urb.fCompleting = false;
#ifdef LOG_ENABLED
        RTStrAPrintf(&pExtra->Urb.pszDesc, "URB %p msg->%p", &pExtra->Urb, pUrb);
#endif
        pExtra->Urb.pVUsb = &pExtra->VUsbExtra;
        //pExtra->Urb.pVUsb->pCtrlUrb = NULL;
        //pExtra->Urb.pVUsb->pNext = NULL;
        //pExtra->Urb.pVUsb->ppPrev = NULL;
        pExtra->Urb.pVUsb->pUrb = &pExtra->Urb;
        pExtra->Urb.pVUsb->pDev = pUrb->pVUsb->pDev;
        pExtra->Urb.pVUsb->pfnFree = vusbMsgFreeUrb;
        pExtra->Urb.pVUsb->pvFreeCtx = &pExtra->Urb;
        //pExtra->Urb.Hci = {0};
        //pExtra->Urb.Dev.pvProxyUrb = NULL;
        pExtra->Urb.DstAddress = pUrb->DstAddress;
        pExtra->Urb.EndPt = pUrb->EndPt;
        pExtra->Urb.enmType = VUSBXFERTYPE_MSG;
        pExtra->Urb.enmDir = VUSBDIRECTION_INVALID;
        //pExtra->Urb.fShortNotOk = false;
        pExtra->Urb.enmStatus = VUSBSTATUS_INVALID;
        //pExtra->Urb.cbData = 0;
        vusbUrbAssert(&pExtra->Urb);
    }
    return pExtra;
}

/**
 * Sets up the message.
 *
 * The message is associated with the pipe, in what's currently called
 * control pipe extra state data (pointed to by pPipe->pCtrl). If this
 * is a OUT message, we will no go on collecting data URB. If it's a
 * IN message, we'll send it and then queue any incoming data for the
 * URBs collecting it.
 *
 * @returns Success indicator.
 */
static bool vusbMsgSetup(PVUSBPIPE pPipe, const void *pvBuf, uint32_t cbBuf)
{
    PVUSBCTRLEXTRA  pExtra = pPipe->pCtrl;
    const VUSBSETUP *pSetupIn = (PVUSBSETUP)pvBuf;

    /*
     * Validate length.
     */
    if (cbBuf < sizeof(VUSBSETUP))
    {
        LogFlow(("vusbMsgSetup: pPipe=%p cbBuf=%u < %u (failure) !!!\n",
                 pPipe, cbBuf, sizeof(VUSBSETUP)));
        return false;
    }

    /* Paranoia: Clear data memory that was previously used
     * by the guest. See @bugref{10438}.
     */
    PVUSBSETUP pOldSetup = pExtra->pMsg;
    uint32_t   cbClean = sizeof(VUSBSETUP) + pOldSetup->wLength;
    cbClean = RT_MIN(cbClean, pExtra->cbMax);
    memset(pExtra->Urb.abData, 0, cbClean);

    /*
     * Check if we've got an cancelled message URB. Allocate a new one in that case.
     */
    if (pExtra->Urb.enmState == VUSBURBSTATE_CANCELLED)
    {
        void *pvNew = RTMemDup(pExtra, RT_UOFFSETOF_DYN(VUSBCTRLEXTRA, Urb.abData[pExtra->cbMax]));
        if (!pvNew)
        {
            Log(("vusbMsgSetup: out of memory!!! cbReq=%zu\n", RT_UOFFSETOF_DYN(VUSBCTRLEXTRA, Urb.abData[pExtra->cbMax])));
            return false;
        }
        pExtra->Urb.pVUsb->pvFreeCtx = NULL;
        LogFlow(("vusbMsgSetup: Replacing canceled pExtra=%p with %p.\n", pExtra, pvNew));
        pPipe->pCtrl = pExtra = (PVUSBCTRLEXTRA)pvNew;
        pExtra->Urb.pVUsb = &pExtra->VUsbExtra;
        pExtra->Urb.pVUsb->pUrb = &pExtra->Urb;
        pExtra->pMsg = (PVUSBSETUP)pExtra->Urb.abData;
        pExtra->Urb.enmState = VUSBURBSTATE_ALLOCATED;
        pExtra->Urb.fCompleting = false;
    }

    /*
     * Check that we've got sufficient space in the message URB.
     */
    if (pExtra->cbMax < cbBuf + pSetupIn->wLength)
    {
        uint32_t cbReq = RT_ALIGN_32(cbBuf + pSetupIn->wLength, 64);
        PVUSBCTRLEXTRA pNew = (PVUSBCTRLEXTRA)RTMemReallocZ(pExtra,
                                                            RT_UOFFSETOF_DYN(VUSBCTRLEXTRA, Urb.abData[pExtra->cbMax]),
                                                            RT_UOFFSETOF_DYN(VUSBCTRLEXTRA, Urb.abData[cbReq]));
        if (!pNew)
        {
            Log(("vusbMsgSetup: out of memory!!! cbReq=%u %zu\n",
                 cbReq, RT_UOFFSETOF_DYN(VUSBCTRLEXTRA, Urb.abData[cbReq])));
            return false;
        }
        if (pExtra != pNew) /* (parfait is wrong about pNew leak here) */
        {
            LogFunc(("Reallocated %u -> %u\n", pExtra->cbMax, cbReq));
            pNew->pMsg = (PVUSBSETUP)pNew->Urb.abData;
            pExtra = pNew;
            pPipe->pCtrl = pExtra;
            pExtra->Urb.pVUsb = &pExtra->VUsbExtra;
            pExtra->Urb.pVUsb->pUrb = &pExtra->Urb;
            pExtra->Urb.pVUsb->pvFreeCtx = &pExtra->Urb;
        }

        pExtra->cbMax = cbReq;
    }
    Assert(pExtra->Urb.enmState == VUSBURBSTATE_ALLOCATED);

    /*
     * Copy the setup data and prepare for data.
     */
    PVUSBSETUP pSetup = pExtra->pMsg;
    pExtra->fSubmitted      = false;
    pExtra->Urb.enmState    = VUSBURBSTATE_IN_FLIGHT;
    pExtra->pbCur           = (uint8_t *)(pSetup + 1);
    pSetup->bmRequestType   = pSetupIn->bmRequestType;
    pSetup->bRequest        = pSetupIn->bRequest;
    pSetup->wValue          = RT_LE2H_U16(pSetupIn->wValue);
    pSetup->wIndex          = RT_LE2H_U16(pSetupIn->wIndex);
    pSetup->wLength         = RT_LE2H_U16(pSetupIn->wLength);

    LogFlow(("vusbMsgSetup(%p,,%d): bmRequestType=%#04x bRequest=%#04x wValue=%#06x wIndex=%#06x wLength=0x%.4x\n",
             pPipe, cbBuf, pSetup->bmRequestType, pSetup->bRequest, pSetup->wValue, pSetup->wIndex, pSetup->wLength));
    return true;
}

/**
 * Build the message URB from the given control URB and accompanying message
 * pipe state which we grab from the device for the URB.
 *
 * @param   pUrb        The URB to submit.
 * @param   pSetup      The setup packet for the message transfer.
 * @param   pExtra      Pointer to the additional state requred for a control transfer.
 * @param   pPipe       The message pipe state.
 */
static void vusbMsgDoTransfer(PVUSBURB pUrb, PVUSBSETUP pSetup, PVUSBCTRLEXTRA pExtra, PVUSBPIPE pPipe)
{
    RT_NOREF(pPipe);

    /*
     * Mark this transfer as sent (cleared at setup time).
     */
    Assert(!pExtra->fSubmitted);
    pExtra->fSubmitted = true;

    /*
     * Do we have to do this synchronously?
     */
    bool fSafeRequest = vusbUrbIsRequestSafe(pSetup, pUrb);
    if (!fSafeRequest)
    {
        vusbMsgSubmitSynchronously(pUrb, fSafeRequest);
        return;
    }

    /*
     * Do it asynchronously.
     */
    LogFlow(("%s: vusbMsgDoTransfer: ep=%d pMsgUrb=%p pPipe=%p stage=%s\n",
             pUrb->pszDesc, pUrb->EndPt, &pExtra->Urb, pPipe, g_apszCtlStates[pExtra->enmStage]));
    Assert(pExtra->Urb.enmType == VUSBXFERTYPE_MSG);
    Assert(pExtra->Urb.EndPt == pUrb->EndPt);
    pExtra->Urb.enmDir  = (pSetup->bmRequestType & VUSB_DIR_TO_HOST) ? VUSBDIRECTION_IN : VUSBDIRECTION_OUT;
    pExtra->Urb.cbData  = pSetup->wLength + sizeof(*pSetup);
    pExtra->Urb.pVUsb->pCtrlUrb = pUrb;
    int rc = vusbUrbQueueAsyncRh(&pExtra->Urb);
    if (RT_FAILURE(rc))
    {
        /*
         * If we fail submitting it, will not retry but fail immediately.
         *
         * This keeps things simple. The host OS will have retried if
         * it's a proxied device, and if it's a virtual one it really means
         * it if it's failing a control message.
         */
        LogFlow(("%s: vusbMsgDoTransfer: failed submitting urb! failing it with %s (rc=%Rrc)!!!\n",
                 pUrb->pszDesc, rc == VERR_VUSB_DEVICE_NOT_ATTACHED ? "DNR" : "CRC", rc));
        pExtra->Urb.enmStatus = rc == VERR_VUSB_DEVICE_NOT_ATTACHED ? VUSBSTATUS_DNR : VUSBSTATUS_CRC;
        pExtra->Urb.enmState = VUSBURBSTATE_REAPED;
        vusbMsgCompletion(&pExtra->Urb);
    }
}

/**
 * Fails a URB request with a pipe STALL error.
 *
 * @returns VINF_SUCCESS indicating that we've completed the URB.
 * @param   pUrb    The URB in question.
 */
static int vusbMsgStall(PVUSBURB pUrb)
{
    PVUSBPIPE       pPipe = &pUrb->pVUsb->pDev->aPipes[pUrb->EndPt];
    PVUSBCTRLEXTRA  pExtra = pPipe->pCtrl;
    LogFlow(("%s: vusbMsgStall: pPipe=%p err=STALL stage %s->SETUP\n",
             pUrb->pszDesc, pPipe, g_apszCtlStates[pExtra->enmStage]));

    pExtra->pbCur    = NULL;
    pExtra->enmStage = CTLSTAGE_SETUP;
    pUrb->enmState = VUSBURBSTATE_REAPED;
    pUrb->enmStatus  = VUSBSTATUS_STALL;
    vusbUrbCompletionRh(pUrb);
    return VINF_SUCCESS;
}

/**
 * Submit a control message.
 *
 * Here we implement the USB defined traffic that occurs in message pipes
 * (aka control endpoints). We want to provide a single function for device
 * drivers so that they don't all have to reimplement the usb logic for
 * themselves. This means we need to keep a little bit of state information
 * because control transfers occur over multiple bus transactions. We may
 * also need to buffer data over multiple data stages.
 *
 * @returns VBox status code.
 * @param   pUrb        The URB to submit.
 */
static int vusbUrbSubmitCtrl(PVUSBURB pUrb)
{
#ifdef LOG_ENABLED
    vusbUrbTrace(pUrb, "vusbUrbSubmitCtrl", false);
#endif
    PVUSBDEV        pDev = pUrb->pVUsb->pDev;
    PVUSBPIPE       pPipe = &pDev->aPipes[pUrb->EndPt];

    RTCritSectEnter(&pPipe->CritSectCtrl);
    PVUSBCTRLEXTRA  pExtra = pPipe->pCtrl;

    if (!pExtra && !(pExtra = pPipe->pCtrl = vusbMsgAllocExtraData(pUrb)))
    {
        RTCritSectLeave(&pPipe->CritSectCtrl);
        return VERR_VUSB_NO_URB_MEMORY;
    }
    PVUSBSETUP      pSetup = pExtra->pMsg;

    if (pPipe->async)
    {
        AssertMsgFailed(("%u\n", pPipe->async));
        RTCritSectLeave(&pPipe->CritSectCtrl);
        return VERR_GENERAL_FAILURE;
    }

    /*
     * A setup packet always resets the transaction and the
     * end of data transmission is signified by change in
     * data direction.
     */
    if (pUrb->enmDir == VUSBDIRECTION_SETUP)
    {
        LogFlow(("%s: vusbUrbSubmitCtrl: pPipe=%p state %s->SETUP\n",
                 pUrb->pszDesc, pPipe, g_apszCtlStates[pExtra->enmStage]));
        pExtra->enmStage = CTLSTAGE_SETUP;
    }
    else if (   pExtra->enmStage == CTLSTAGE_DATA
                /* (the STATUS stage direction goes the other way) */
             && !!(pSetup->bmRequestType & VUSB_DIR_TO_HOST) != (pUrb->enmDir == VUSBDIRECTION_IN))
    {
        LogFlow(("%s: vusbUrbSubmitCtrl: pPipe=%p state %s->STATUS\n",
                 pUrb->pszDesc, pPipe, g_apszCtlStates[pExtra->enmStage]));
        pExtra->enmStage = CTLSTAGE_STATUS;
    }

    /*
     * Act according to the current message stage.
     */
    switch (pExtra->enmStage)
    {
        case CTLSTAGE_SETUP:
            /*
             * When stall handshake is returned, all subsequent packets
             * must generate stall until a setup packet arrives.
             */
            if (pUrb->enmDir != VUSBDIRECTION_SETUP)
            {
                Log(("%s: vusbUrbSubmitCtrl: Stall at setup stage (dir=%#x)!!\n", pUrb->pszDesc, pUrb->enmDir));
                vusbMsgStall(pUrb);
                break;
            }

            /* Store setup details, return DNR if corrupt */
            if (!vusbMsgSetup(pPipe, pUrb->abData, pUrb->cbData))
            {
                pUrb->enmState = VUSBURBSTATE_REAPED;
                pUrb->enmStatus = VUSBSTATUS_DNR;
                vusbUrbCompletionRh(pUrb);
                break;
            }
            if (pPipe->pCtrl != pExtra)
            {
                pExtra = pPipe->pCtrl;
                pSetup = pExtra->pMsg;
            }

            /* pre-buffer our output if it's device-to-host */
            if (pSetup->bmRequestType & VUSB_DIR_TO_HOST)
                vusbMsgDoTransfer(pUrb, pSetup, pExtra, pPipe);
            else if (pSetup->wLength)
            {
                LogFlow(("%s: vusbUrbSubmitCtrl: stage=SETUP - to dev: need data\n", pUrb->pszDesc));
                pUrb->enmState = VUSBURBSTATE_REAPED;
                vusbMsgSetupCompletion(pUrb);
                vusbUrbCompletionRh(pUrb);
            }
            /*
             * If there is no DATA stage, we must send it now since there are
             * no requirement of a STATUS stage.
             */
            else
            {
                LogFlow(("%s: vusbUrbSubmitCtrl: stage=SETUP - to dev: sending\n", pUrb->pszDesc));
                vusbMsgDoTransfer(pUrb, pSetup, pExtra, pPipe);
            }
            break;

        case CTLSTAGE_DATA:
        {
            /*
             * If a data stage exceeds the target buffer indicated in
             * setup return stall, if data stage returns stall there
             * will be no status stage.
             */
            uint8_t *pbData = (uint8_t *)(pExtra->pMsg + 1);
            if ((uintptr_t)&pExtra->pbCur[pUrb->cbData] > (uintptr_t)&pbData[pSetup->wLength])
            {
                /* In the device -> host direction, the device never returns more data than
                   what was requested (wLength).  So, we can just cap cbData. */
                ssize_t const cbLeft = &pbData[pSetup->wLength] - pExtra->pbCur;
                if (pSetup->bmRequestType & VUSB_DIR_TO_HOST)
                {
                    LogFlow(("%s: vusbUrbSubmitCtrl: Adjusting DATA request: %d -> %d\n", pUrb->pszDesc, pUrb->cbData, cbLeft));
                    pUrb->cbData = cbLeft >= 0 ? (uint32_t)cbLeft : 0;
                }
                /* In the host -> direction it's undefined what happens if the host provides
                   more data than what wLength inidicated.  However, in 2007, iPhone detection
                   via iTunes would issue wLength=0 but provide a data URB which we needed to
                   pass on to the device anyway, so we'll just quietly adjust wLength if it's
                   zero and get on with the work.

                   What confuses me (bird) here, though, is that we've already sent the SETUP
                   URB to the device when we received it, and all we end up doing is an
                   unnecessary memcpy and completing the URB, but never actually sending the
                   data to the device.  So, I guess this stuff is still a little iffy.

                   Note! We currently won't be doing any resizing, as we've disabled resizing
                         in general.
                   P.S.  We used to have a very strange (pUrb->cbData % pSetup->wLength) == 0
                         thing too that joined the pUrb->cbData adjusting above. */
                else if (   pSetup->wLength == 0
                         && pUrb->cbData <= pExtra->cbMax)
                {
                    Log(("%s: vusbUrbSubmitCtrl: pAdjusting wLength: %u -> %u (iPhone hack)\n",
                         pUrb->pszDesc, pSetup->wLength, pUrb->cbData));
                    pSetup->wLength = pUrb->cbData;
                    Assert(cbLeft >= (ssize_t)pUrb->cbData);
                }
                else
                {
                    Log(("%s: vusbUrbSubmitCtrl: Stall at data stage!! wLength=%u cbData=%d cbMax=%d cbLeft=%dz\n",
                         pUrb->pszDesc, pSetup->wLength, pUrb->cbData, pExtra->cbMax, cbLeft));
                    vusbMsgStall(pUrb);
                    break;
                }
            }

            if (pUrb->enmDir == VUSBDIRECTION_IN)
            {
                /* put data received from the device. */
                const uint32_t cbRead = RT_MIN(pUrb->cbData, pExtra->cbLeft);
                memcpy(pUrb->abData, pExtra->pbCur, cbRead);

                /* advance */
                pExtra->pbCur += cbRead;
                if (pUrb->cbData == cbRead)
                    pExtra->cbLeft -= pUrb->cbData;
                else
                {
                    /* adjust the pUrb->cbData to reflect the number of bytes containing actual data. */
                    LogFlow(("%s: vusbUrbSubmitCtrl: adjusting last DATA pUrb->cbData, %d -> %d\n",
                             pUrb->pszDesc, pUrb->cbData, pExtra->cbLeft));
                    pUrb->cbData = cbRead;
                    pExtra->cbLeft = 0;
                }
            }
            else
            {
                /* get data for sending when completed. */
                AssertStmt((ssize_t)pUrb->cbData <= pExtra->cbMax - (pExtra->pbCur - pbData), /* paranoia: checked above */
                           pUrb->cbData = pExtra->cbMax - (uint32_t)RT_MIN(pExtra->pbCur - pbData, pExtra->cbMax));
                memcpy(pExtra->pbCur, pUrb->abData, pUrb->cbData);

                /* advance */
                pExtra->pbCur += pUrb->cbData;

                /*
                 * If we've got the necessary data, we'll send it now since there are
                 * no requirement of a STATUS stage.
                 */
                if (    !pExtra->fSubmitted
                    &&  pExtra->pbCur - pbData >= pSetup->wLength)
                {
                    LogFlow(("%s: vusbUrbSubmitCtrl: stage=DATA - to dev: sending\n", pUrb->pszDesc));
                    vusbMsgDoTransfer(pUrb, pSetup, pExtra, pPipe);
                    break;
                }
            }

            pUrb->enmState = VUSBURBSTATE_REAPED;
            vusbMsgDataCompletion(pUrb);
            vusbUrbCompletionRh(pUrb);
            break;
        }

        case CTLSTAGE_STATUS:
            if (    (pSetup->bmRequestType & VUSB_DIR_TO_HOST)
                ||  pExtra->fSubmitted)
            {
                Assert(pExtra->fSubmitted);
                pUrb->enmState = VUSBURBSTATE_REAPED;
                vusbMsgStatusCompletion(pUrb);
                vusbUrbCompletionRh(pUrb);
            }
            else
            {
                LogFlow(("%s: vusbUrbSubmitCtrl: stage=STATUS - to dev: sending\n", pUrb->pszDesc));
                vusbMsgDoTransfer(pUrb, pSetup, pExtra, pPipe);
            }
            break;
    }

    RTCritSectLeave(&pPipe->CritSectCtrl);
    return VINF_SUCCESS;
}


/**
 * Submit a interrupt URB.
 *
 * @returns VBox status code.
 * @param   pUrb        The URB to submit.
 */
static int vusbUrbSubmitInterrupt(PVUSBURB pUrb)
{
    LogFlow(("%s: vusbUrbSubmitInterrupt: (sync)\n", pUrb->pszDesc));
    return vusbUrbQueueAsyncRh(pUrb);
}


/**
 * Submit a bulk URB.
 *
 * @returns VBox status code.
 * @param   pUrb        The URB to submit.
 */
static int vusbUrbSubmitBulk(PVUSBURB pUrb)
{
    LogFlow(("%s: vusbUrbSubmitBulk: (async)\n", pUrb->pszDesc));
    return vusbUrbQueueAsyncRh(pUrb);
}


/**
 * Submit an isochronous URB.
 *
 * @returns VBox status code.
 * @param   pUrb        The URB to submit.
 */
static int vusbUrbSubmitIsochronous(PVUSBURB pUrb)
{
    LogFlow(("%s: vusbUrbSubmitIsochronous: (async)\n", pUrb->pszDesc));
    return vusbUrbQueueAsyncRh(pUrb);
}


/**
 * Fail a URB with a 'hard-error' sort of error.
 *
 * @return VINF_SUCCESS (the Urb status indicates the error).
 * @param   pUrb    The URB.
 */
int vusbUrbSubmitHardError(PVUSBURB pUrb)
{
    /* FIXME: Find out the correct return code from the spec */
    pUrb->enmState = VUSBURBSTATE_REAPED;
    pUrb->enmStatus = VUSBSTATUS_DNR;
    vusbUrbCompletionRh(pUrb);
    return VINF_SUCCESS;
}


/**
 * Submit a URB.
 */
int vusbUrbSubmit(PVUSBURB pUrb)
{
    vusbUrbAssert(pUrb);
    Assert(pUrb->enmState == VUSBURBSTATE_ALLOCATED);
    PVUSBDEV pDev = pUrb->pVUsb->pDev;
    Assert(pDev);

    /*
     * Check that the device is in a valid state.
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    if (enmState == VUSB_DEVICE_STATE_RESET)
    {
        LogRel(("VUSB: %s: power off ignored, the device is resetting!\n", pDev->pUsbIns->pszName));
        pUrb->enmStatus = VUSBSTATUS_DNR;
        /* This will postpone the TDs until we're done with the resetting. */
        return VERR_VUSB_DEVICE_IS_RESETTING;
    }

#ifdef LOG_ENABLED
    /* stamp it */
    pUrb->pVUsb->u64SubmitTS = RTTimeNanoTS();
#endif

    /** @todo Check max packet size here too? */

    /*
     * Validate the pipe.
     */
    if (pUrb->EndPt >= VUSB_PIPE_MAX)
    {
        Log(("%s: pDev=%p[%s]: SUBMIT: ep %i >= %i!!!\n", pUrb->pszDesc, pDev, pDev->pUsbIns->pszName, pUrb->EndPt, VUSB_PIPE_MAX));
        return vusbUrbSubmitHardError(pUrb);
    }
    PCVUSBDESCENDPOINTEX pEndPtDesc;
    switch (pUrb->enmDir)
    {
        case VUSBDIRECTION_IN:
            pEndPtDesc = pDev->aPipes[pUrb->EndPt].in;
            break;
        case VUSBDIRECTION_SETUP:
        case VUSBDIRECTION_OUT:
        default:
            pEndPtDesc = pDev->aPipes[pUrb->EndPt].out;
            break;
    }
    if (!pEndPtDesc)
    {
        Log(("%s: pDev=%p[%s]: SUBMIT: no endpoint!!! dir=%s e=%i\n",
             pUrb->pszDesc, pDev, pDev->pUsbIns->pszName, vusbUrbDirName(pUrb->enmDir), pUrb->EndPt));
        return vusbUrbSubmitHardError(pUrb);
    }

    /*
     * Check for correct transfer types.
     * Our type codes are the same - what a coincidence.
     */
    if ((pEndPtDesc->Core.bmAttributes & 0x3) != pUrb->enmType)
    {
        /* Bulk and interrupt transfers are identical on the bus level (the only difference
         * is in how they are scheduled by the HCD/HC) and need an exemption.
         * Atheros AR9271 is a known offender; its configuration descriptors include
         * interrupt endpoints, but drivers (Win7/8, Linux kernel pre-3.05) treat them
         * as bulk endpoints.
         */
        if (   (pUrb->enmType == VUSBXFERTYPE_BULK && (pEndPtDesc->Core.bmAttributes & 0x3) == VUSBXFERTYPE_INTR)
            || (pUrb->enmType == VUSBXFERTYPE_INTR && (pEndPtDesc->Core.bmAttributes & 0x3) == VUSBXFERTYPE_BULK))
        {
            Log2(("%s: pDev=%p[%s]: SUBMIT: mixing bulk/interrupt transfers on DstAddress=%i ep=%i dir=%s\n",
                  pUrb->pszDesc, pDev, pDev->pUsbIns->pszName,
                  pUrb->DstAddress, pUrb->EndPt, vusbUrbDirName(pUrb->enmDir)));
        }
        else
        {
            Log(("%s: pDev=%p[%s]: SUBMIT: %s transfer requested for %#x endpoint on DstAddress=%i ep=%i dir=%s\n",
                 pUrb->pszDesc, pDev, pDev->pUsbIns->pszName, vusbUrbTypeName(pUrb->enmType), pEndPtDesc->Core.bmAttributes,
                 pUrb->DstAddress, pUrb->EndPt, vusbUrbDirName(pUrb->enmDir)));
            return vusbUrbSubmitHardError(pUrb);
        }
    }

    /*
     * If there's a URB in the read-ahead buffer, use it.
     */
    int rc;

    if (pDev->hSniffer)
    {
        rc = VUSBSnifferRecordEvent(pDev->hSniffer, pUrb, VUSBSNIFFEREVENT_SUBMIT);
        if (RT_FAILURE(rc))
            LogRel(("VUSB: Capturing URB submit event failed with %Rrc\n", rc));
    }

    /*
     * Take action based on type.
     */
    pUrb->enmState = VUSBURBSTATE_IN_FLIGHT;
    switch (pUrb->enmType)
    {
        case VUSBXFERTYPE_CTRL:
            rc = vusbUrbSubmitCtrl(pUrb);
            break;
        case VUSBXFERTYPE_BULK:
            rc = vusbUrbSubmitBulk(pUrb);
            break;
        case VUSBXFERTYPE_INTR:
            rc = vusbUrbSubmitInterrupt(pUrb);
            break;
        case VUSBXFERTYPE_ISOC:
            rc = vusbUrbSubmitIsochronous(pUrb);
            break;
        default:
            AssertMsgFailed(("Unexpected pUrb type %d\n", pUrb->enmType));
            return vusbUrbSubmitHardError(pUrb);
    }

    /*
     * The device was detached, so we fail everything.
     * (We should really detach and destroy the device, but we'll have to wait till Main reacts.)
     */
    if (rc == VERR_VUSB_DEVICE_NOT_ATTACHED)
        rc = vusbUrbSubmitHardError(pUrb);
    /*
     * We don't increment error count if async URBs are in flight, in
     * this case we just assume we need to throttle back, this also
     * makes sure we don't halt bulk endpoints at the wrong time.
     */
    else if (   RT_FAILURE(rc)
             && !ASMAtomicReadU32(&pDev->aPipes[pUrb->EndPt].async)
             /* && pUrb->enmType == VUSBXFERTYPE_BULK ?? */
             && !vusbUrbErrorRh(pUrb))
    {
        /* don't retry it anymore. */
        pUrb->enmState = VUSBURBSTATE_REAPED;
        pUrb->enmStatus = VUSBSTATUS_CRC;
        vusbUrbCompletionRh(pUrb);
        return VINF_SUCCESS;
    }

    return rc;
}


/**
 * Reap in-flight URBs.
 *
 * @param   pUrbLst     Pointer to the head of the URB list.
 * @param   cMillies    Number of milliseconds to block in each reap operation.
 *                      Use 0 to not block at all.
 */
void vusbUrbDoReapAsync(PRTLISTANCHOR pUrbLst, RTMSINTERVAL cMillies)
{
    PVUSBURBVUSB pVUsbUrb = RTListGetFirst(pUrbLst, VUSBURBVUSBINT, NdLst);
    while (pVUsbUrb)
    {
        vusbUrbAssert(pVUsbUrb->pUrb);
        PVUSBURBVUSB pVUsbUrbNext = RTListGetNext(pUrbLst, pVUsbUrb, VUSBURBVUSBINT, NdLst);
        PVUSBDEV pDev = pVUsbUrb->pDev;

        /* Don't touch resetting devices - paranoid safety precaution. */
        if (vusbDevGetState(pDev) != VUSB_DEVICE_STATE_RESET)
        {
            /*
             * Reap most URBs pending on a single device.
             */
            PVUSBURB pRipe;

            /**
             * This is workaround for race(should be fixed) detach on one EMT thread and frame boundary timer on other
             * and leaked URBs (shouldn't be affected by leaked URBs).
             */
            Assert(pDev->pUsbIns);
            while (   pDev->pUsbIns
                   && ((pRipe = pDev->pUsbIns->pReg->pfnUrbReap(pDev->pUsbIns, cMillies)) != NULL))
            {
                vusbUrbAssert(pRipe);
                if (pVUsbUrbNext && pRipe == pVUsbUrbNext->pUrb)
                    pVUsbUrbNext = RTListGetNext(pUrbLst, pVUsbUrbNext, VUSBURBVUSBINT, NdLst);
                vusbUrbRipe(pRipe);
            }
        }

        /* next */
        pVUsbUrb = pVUsbUrbNext;
    }
}

/**
 * Reap URBs on a per device level.
 *
 * @param   pDev        The device instance to reap URBs for.
 * @param   cMillies    Number of milliseconds to block in each reap operation.
 *                      Use 0 to not block at all.
 */
void vusbUrbDoReapAsyncDev(PVUSBDEV pDev, RTMSINTERVAL cMillies)
{
    Assert(pDev->enmState != VUSB_DEVICE_STATE_RESET);

    /*
     * Reap most URBs pending on a single device.
     */
    PVUSBURB pRipe;

    /**
     * This is workaround for race(should be fixed) detach on one EMT thread and frame boundary timer on other
     * and leaked URBs (shouldn't be affected by leaked URBs).
     */

    if (ASMAtomicXchgBool(&pDev->fWokenUp, false))
        return;

    Assert(pDev->pUsbIns);
    while (   pDev->pUsbIns
           && ((pRipe = pDev->pUsbIns->pReg->pfnUrbReap(pDev->pUsbIns, cMillies)) != NULL))
    {
        vusbUrbAssert(pRipe);
        vusbUrbRipe(pRipe);
        if (ASMAtomicXchgBool(&pDev->fWokenUp, false))
            break;
    }
}

/**
 * Completes the URB.
 */
static void vusbUrbCompletion(PVUSBURB pUrb)
{
    Assert(pUrb->pVUsb->pDev->aPipes);
    ASMAtomicDecU32(&pUrb->pVUsb->pDev->aPipes[pUrb->EndPt].async);

    if (pUrb->enmState == VUSBURBSTATE_REAPED)
        vusbUrbUnlink(pUrb);

    vusbUrbCompletionRh(pUrb);
}

/**
 * The worker for URB cancellation which is executed on the I/O thread.
 * Cancels a URB with CRC failure.
 *
 * Cancelling a URB is a tricky thing. The USBProxy backend can not
 * cancel it immediately and we must keep the URB around until it's ripe
 * and can be reaped the normal way. However, we must complete the URB
 * now, before leaving this function. This is not nice. sigh.
 *
 * This function will cancel the URB if it's in-flight and complete
 * it. The device will in its pfnCancel method be given the chance to
 * say that the URB doesn't need reaping and should be unlinked.
 *
 * An URB which is in the cancel state after pfnCancel will remain in that
 * state and in the async list until its reaped. When it's finally reaped
 * it will be unlinked and freed without doing any completion.
 *
 * There are different modes of canceling an URB. When devices are being
 * disconnected etc., they will be completed with an error (CRC). However,
 * when the HC needs to temporarily halt communication with a device, the
 * URB/TD must be left alone if possible.
 *
 * @returns IPRT status code.
 * @param   pUrb        The URB to cancel.
 * @param   enmMode     The way the URB should be canceled.
 */
DECLHIDDEN(int) vusbUrbCancelWorker(PVUSBURB pUrb, CANCELMODE enmMode)
{
    vusbUrbAssert(pUrb);
#ifdef VBOX_WITH_STATISTICS
    PVUSBROOTHUB pRh = vusbDevGetRh(pUrb->pVUsb->pDev);
#endif
    if (pUrb->enmState == VUSBURBSTATE_IN_FLIGHT)
    {
        LogFlow(("%s: vusbUrbCancel: Canceling in-flight\n", pUrb->pszDesc));
        STAM_COUNTER_INC(&pRh->Total.StatUrbsCancelled);
        if (pUrb->enmType != VUSBXFERTYPE_MSG)
        {
            STAM_STATS({Assert(pUrb->enmType >= 0 && pUrb->enmType < (int)RT_ELEMENTS(pRh->aTypes));});
            STAM_COUNTER_INC(&pRh->aTypes[pUrb->enmType].StatUrbsCancelled);
        }

        pUrb->enmState = VUSBURBSTATE_CANCELLED;
        PPDMUSBINS pUsbIns = pUrb->pVUsb->pDev->pUsbIns;
        pUsbIns->pReg->pfnUrbCancel(pUsbIns, pUrb);
        Assert(pUrb->enmState == VUSBURBSTATE_CANCELLED || pUrb->enmState == VUSBURBSTATE_REAPED);

        pUrb->enmStatus = VUSBSTATUS_CRC;
        vusbUrbCompletion(pUrb);
    }
    else if (pUrb->enmState == VUSBURBSTATE_REAPED)
    {
        LogFlow(("%s: vusbUrbCancel: Canceling reaped urb\n", pUrb->pszDesc));
        STAM_COUNTER_INC(&pRh->Total.StatUrbsCancelled);
        if (pUrb->enmType != VUSBXFERTYPE_MSG)
        {
            STAM_STATS({Assert(pUrb->enmType >= 0 && pUrb->enmType < (int)RT_ELEMENTS(pRh->aTypes));});
            STAM_COUNTER_INC(&pRh->aTypes[pUrb->enmType].StatUrbsCancelled);
        }

        pUrb->enmStatus = VUSBSTATUS_CRC;
        vusbUrbCompletion(pUrb);
    }
    else
    {
        AssertMsg(pUrb->enmState == VUSBURBSTATE_CANCELLED, ("Invalid state %d, pUrb=%p\n", pUrb->enmState, pUrb));
        switch (enmMode)
        {
            default:
                AssertMsgFailed(("Invalid cancel mode\n"));
                RT_FALL_THRU();
            case CANCELMODE_FAIL:
                pUrb->enmStatus = VUSBSTATUS_CRC;
                break;
            case CANCELMODE_UNDO:
                pUrb->enmStatus = VUSBSTATUS_UNDO;
                break;

        }
    }
    return VINF_SUCCESS;
}

/**
 * Deals with a ripe URB (i.e. after reaping it).
 *
 * If an URB is in the reaped or in-flight state, we'll
 * complete it. If it's cancelled, we'll simply free it.
 * Any other states should never get here.
 *
 * @param   pUrb    The URB.
 */
void vusbUrbRipe(PVUSBURB pUrb)
{
    if (    pUrb->enmState == VUSBURBSTATE_IN_FLIGHT
        ||  pUrb->enmState == VUSBURBSTATE_REAPED)
    {
        pUrb->enmState = VUSBURBSTATE_REAPED;
        if (!ASMAtomicXchgBool(&pUrb->fCompleting, true))
            vusbUrbCompletion(pUrb);
    }
    else if (pUrb->enmState == VUSBURBSTATE_CANCELLED)
    {
        vusbUrbUnlink(pUrb);
        LogFlow(("%s: vusbUrbRipe: Freeing cancelled URB\n", pUrb->pszDesc));
        pUrb->pVUsb->pfnFree(pUrb);
    }
    else
        AssertMsgFailed(("Invalid URB state %d; %s\n", pUrb->enmState, pUrb->pszDesc));
}


/*
 * Local Variables:
 *  mode: c
 *  c-file-style: "bsd"
 *  c-basic-offset: 4
 *  tab-width: 4
 *  indent-tabs-mode: s
 * End:
 */

