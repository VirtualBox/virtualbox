/* $Id: VbghWaylandClipboard.cpp 114774 2026-07-25 23:32:01Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest / Host common code - Wayland Clipboard.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include <VBox/GuestHost/SharedClipboard-Wayland.h>
#include <VBox/GuestHost/mime-type-converter.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>

#include <wayland-client-protocol.h>
#include <ext-data-control-v1.h>
#include <wlr-data-control-unstable-v1.h>

#include <unistd.h>
#include <errno.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/** Function used by vbghWaylandClipboardFinishDataOffer to get offered data. */
typedef DECLCALLBACKTYPE(void, FNVBGHWLCLOFFERRECIVE,(void *pvOffer, const char *pszMimeType, int fdWrite));
/** Pointer to FNVBGHWLCLOFFERRECIVE. */
typedef FNVBGHWLCLOFFERRECIVE *PFNVBGHWLCLOFFERRECIVE;


/**
 * @callback_method_impl{FNVBGHWAYLANDREGENUM,
 *      Wayland global registry object enumeration callback.}
 */
static DECLCALLBACK(void)
vbghWaylandClipboardRegEnum(PVBGHWAYLANDCORE pCore, struct wl_registry *pRegistry, uint32_t uObjName,
                            const char *pszIfaceName, uint32_t uIfaceVersion, PRTERRINFO pErrInfo)
{
    LogRel6(("vbghWaylandClipboardRegEnum: #%u: %s v%u\n", uObjName, pszIfaceName, uIfaceVersion));
    PSHCLWAYLANDCTX const pThis = RT_FROM_MEMBER(pCore, SHCLWAYLANDCTX, GhCore);
    RT_NOREF(pErrInfo);

    /*
     * Gather stuff for the popup first.
     */
    VbghWaylandPopupRegEnum(&pThis->Popup, pRegistry, uObjName, pszIfaceName, uIfaceVersion);

    /*
     * Call wl_registry_bind & return if matching anything we're interested in.
     */
    /* Objects providing access to the clipboard: */
    VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, uObjName, pszIfaceName, uIfaceVersion, pErrInfo,
                                                 ext_data_control_manager_v1_interface, struct ext_data_control_manager_v1 *,
                                                 pThis->pExtDataControlManager, 1 /*uMinVersion*/);

    VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, uObjName, pszIfaceName, uIfaceVersion, pErrInfo,
                                                 zwlr_data_control_manager_v1_interface, struct zwlr_data_control_manager_v1 *,
                                                 pThis->pZwlrDataControlManager, RT_MIN(2, uIfaceVersion));

    VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, uObjName, pszIfaceName, uIfaceVersion, pErrInfo,
                                                 wl_data_device_manager_interface, struct wl_data_device_manager *,
                                                 pThis->pWlDataDeviceManager, 1 /*uMinVersion*/);

}


/**
 * Legacy interface: Initializes the VBoxClient clipboard context for Wayland use.
 */
VBGH_DECL(int) VbghWaylandClipboardPartialInit(PSHCLWAYLANDCTX pThis)
{
    pThis->hOurCacheFilledEvent = NIL_RTSEMEVENTMULTI;

    /* common */
    int rc = RTCritSectInit(&pThis->CritSect);
    AssertRCReturn(rc, rc);

    /* Other side: */
    ShClCacheInit(&pThis->OtherCache);
    pThis->fOtherFormats = VBOX_SHCL_FMT_NONE;

    /* Our side: */
    rc = RTSemEventMultiCreate(&pThis->hOurCacheFilledEvent);
    if (RT_SUCCESS(rc))
    {
        RTSemEventMultiSignal(pThis->hOurCacheFilledEvent);
        ShClCacheInit(&pThis->OurCache);
        pThis->fOurFormats = VBOX_SHCL_FMT_NONE;
        return VINF_SUCCESS;
    }

    /* bail */
    pThis->hOurCacheFilledEvent = NIL_RTSEMEVENTMULTI;
    ShClCacheTerm(&pThis->OtherCache);
    RTCritSectDelete(&pThis->CritSect);
    return rc;
}


VBGH_DECL(int) VbghWaylandClipboardInit(PSHCLWAYLANDCTX pThis, PRTERRINFO pErrInfo)
{
    /*
     * Initialize the basic wayland bits.
     */
    int rc = VbghWaylandClipboardPartialInit(pThis);
    if (RT_FAILURE(rc))
        return RTErrInfoSetF(pErrInfo, rc, "VbghWaylandClipboardPartialInit failed: %Rrc", rc);

    VbghWaylandPopupInit(&pThis->Popup, &pThis->GhCore);
    rc = VbghWaylandConnect(&pThis->GhCore, vbghWaylandClipboardRegEnum, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        /* We need a seat ... */
        PVBGHWAYLANDSEAT const pSeatEntry = VbghWaylandGetBestSeatEntry(&pThis->GhCore);
        if (pSeatEntry)
        {
            /* ... and one of the data control/device managers. */
            if (   pThis->pExtDataControlManager
                || pThis->pZwlrDataControlManager
                || pThis->pWlDataDeviceManager)
            {
                /*
                 * Prefer the Ext and Zwlr over Wl.
                 */
                void *pv;
                if (pThis->pExtDataControlManager)
                {
                    pThis->pszProtocol = "ext-data-control-v1";
                    pThis->enmProtocol = SHCLWAYLANDPROTO_EXT;
                    pv = pThis->pExtDataDevice = ext_data_control_manager_v1_get_data_device(pThis->pExtDataControlManager,
                                                                                             pSeatEntry->pSeat);
                }
                else if (pThis->pZwlrDataControlManager)
                {
                    pThis->pszProtocol = "wlr-data-control-unstable-v1";
                    pThis->enmProtocol = SHCLWAYLANDPROTO_ZWLR;
                    pv = pThis->pZwlrDataDevice = zwlr_data_control_manager_v1_get_data_device(pThis->pZwlrDataControlManager,
                                                                                               pSeatEntry->pSeat);
                }
                else
                {
                    pThis->pszProtocol = "wl-data-device-manager";
                    pThis->enmProtocol = SHCLWAYLANDPROTO_WL;
                    pv = pThis->pWlDataDevice = wl_data_device_manager_get_data_device(pThis->pWlDataDeviceManager,
                                                                                       pSeatEntry->pSeat);
                }
                if (pv)
                {
                    pThis->pSeatEntry = pSeatEntry;
                    LogRel3(("VbghWaylandClipboardInit: VINF_SUCCESS - %s (seat: %p/%#x/%s)\n", pThis->pszProtocol,
                             pSeatEntry->pSeat, pSeatEntry->fCaps, pSeatEntry->pszName));
                    return VINF_SUCCESS;
                }

                rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                   "Failed to get data device for the %s protocol!", pThis->pszProtocol);
            }
            else
                rc = RTErrInfoSet(pErrInfo, VERR_NOT_FOUND, "No Wayland data control manager or data device manager found");
        }
        else
            rc = RTErrInfoSet(pErrInfo, VERR_NOT_FOUND, "No wl_seat found");
        VbghWaylandDisconnect(&pThis->GhCore);
    }
    VbghWaylandPopupTerm(&pThis->Popup);
    VbghWaylandClipboardPartialTerm(pThis);
    return rc;
}


/**
 * Legacy interface: Cleans up the VBoxClient clipboard context after Wayland use.
 */
VBGH_DECL(void) VbghWaylandClipboardPartialTerm(PSHCLWAYLANDCTX pThis)
{
    ShClCacheTerm(&pThis->OurCache);
    RTSemEventMultiDestroy(pThis->hOurCacheFilledEvent);
    pThis->hOurCacheFilledEvent = NIL_RTSEMEVENTMULTI;

    RTCritSectDelete(&pThis->CritSect);
    ShClCacheTerm(&pThis->OtherCache);
}


VBGH_DECL(void) VbghWaylandClipboardTerm(PSHCLWAYLANDCTX pThis)
{
    AssertPtrReturnVoid(pThis);

    /* Data device: */
    if (pThis->pExtDataDevice)
    {
        ext_data_control_device_v1_destroy(pThis->pExtDataDevice);
        pThis->pExtDataDevice = NULL;
    }
    if (pThis->pZwlrDataDevice)
    {
        zwlr_data_control_device_v1_destroy(pThis->pZwlrDataDevice);
        pThis->pZwlrDataDevice = NULL;
    }
    if (pThis->pWlDataDevice)
    {
        wl_data_device_destroy(pThis->pWlDataDevice);
        pThis->pWlDataDevice = NULL;
    }

    /* Data Manage: */
    if (pThis->pExtDataControlManager)
    {
        ext_data_control_manager_v1_destroy(pThis->pExtDataControlManager);
        pThis->pExtDataControlManager = NULL;
    }
    if (pThis->pZwlrDataControlManager)
    {
        zwlr_data_control_manager_v1_destroy(pThis->pZwlrDataControlManager);
        pThis->pZwlrDataControlManager = NULL;
    }
    if (pThis->pWlDataDeviceManager)
    {
        wl_data_device_manager_destroy(pThis->pWlDataDeviceManager);
        pThis->pWlDataDeviceManager = NULL;
    }

    VbghWaylandPopupTerm(&pThis->Popup);
    VbghWaylandClipboardPartialTerm(pThis); /** @todo merge in. */
    VbghWaylandDisconnect(&pThis->GhCore);
}


/**
 * Resets the state for our side before, entering the cache-filling stage.
 *
 * @returns New uRevision value.
 * @param   pThis       The Wayland clipboard state.
 * @param   pszCaller   Caller name for logging.
 * @param   pOfferSlot  Offer slot to incorporate. Optional.
 */
VBGH_DECL(uint64_t) VbghWaylandClipboardResetOurState(PSHCLWAYLANDCTX pThis, const char *pszCaller, SHCLWLOFFERSLOT *pOfferSlot)
{
    RTCritSectEnter(&pThis->CritSect);

    ShClCacheInvalidate(&pThis->OurCache);

    int rc = RTSemEventMultiReset(pThis->hOurCacheFilledEvent);
    AssertRCStmt(rc, LogRel(("error: %s: RTSemEventMultiReset failed: rc=%Rrc", pszCaller, rc)));

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aOurMimeTypes); i++)
    {
        pThis->aOurMimeTypes[i].fFlagsAndPriority = pOfferSlot ? pOfferSlot->aMimeTypes[i].fFlagsAndPriority : 0;
        pThis->aOurMimeTypes[i].pszMimeType       = pOfferSlot ? pOfferSlot->aMimeTypes[i].pszMimeType       : NULL;
    }

    pThis->fOurFormats     = pOfferSlot ? pOfferSlot->fFormats : VBOX_SHCL_FMT_NONE;
    uint64_t const uRevision = (pThis->uRevision + 2) | 1;
    pThis->uRevision       = uRevision;

    RTCritSectLeave(&pThis->CritSect);
    return uRevision;
}


/**
 * Reads guest clipboard data into our cache entry for @a uVBoxFmt.
 *
 * @returns VBox status code.
 * @param   pThis               The Wayland clipboard state.
 * @param   fdSrc               The source file descriptor (pipe).
 * @param   idxVBoxFmt          The log2 of the VBox format / cache index.
 * @param   uRevision           The state/cache revision.
 * @param   pszMimeType         The MIME type being read (for logging).
 */
static int vbghWaylandClipboardReadGuestDataIntoCache(PSHCLWAYLANDCTX pThis, int fdSrc, unsigned idxVBoxFmt,
                                                      uint64_t uRevision, const char *pszMimeType)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertReturn(idxVBoxFmt < RT_ELEMENTS(pThis->aOurMimeTypes), VERR_INVALID_PARAMETER);
    SHCLFORMAT const fVBoxFmt = RT_BIT_32(idxVBoxFmt);
    AssertReturn(fdSrc >= 0, VERR_INVALID_HANDLE);

    /*
     * Read the stuff into a temporary buffer (allocated by worker).
     */
    void  *pvSrcData = NULL;
    size_t cbSrcData = 0;
    int rc = VbghWaylandReadFdToBuffer(fdSrc, RT_MS_5SEC, &pvSrcData, &cbSrcData);
    if (RT_SUCCESS(rc))
    {
        /*
         * Convert it into VBox format.
         */
        void  *pvVBoxData = NULL;
        size_t cbVBoxData = 0;
        rc = VbghMimeConvToVBox(pszMimeType, pvSrcData, cbSrcData, &pvVBoxData, &cbVBoxData);
        if (RT_SUCCESS(rc))
        {
            /*
             * Enter it into the cache.
             */
            RTCritSectEnter(&pThis->CritSect);
            if (pThis->uRevision == uRevision)
            {
                /** @todo one copy too many here, but it has the benefit of heap api
                 * consistency (e.g. drop-in electric heap fencing in a source file). */
                rc = ShClCacheSet(&pThis->OurCache, fVBoxFmt, pvVBoxData, cbVBoxData);
                RTCritSectLeave(&pThis->CritSect);
                if (RT_SUCCESS(rc))
                    LogRel5(("Put %zu bytes into cache for %#x (from %s)\n", cbVBoxData, fVBoxFmt, pszMimeType));
                else
                    LogRel2(("Failed to put %zu bytes into cache for %#x (from %s): %Rrc\n",
                             cbVBoxData, fVBoxFmt, pszMimeType, rc));
            }
            else
            {
                rc = VERR_VERSION_MISMATCH;
                RTCritSectLeave(&pThis->CritSect);
                LogRel2(("Failed to put %zu bytes into cache for %#x (from %s): version changed\n",
                         cbVBoxData, fVBoxFmt, pszMimeType));
            }
            RTMemFree(pvVBoxData);
        }
        else
            LogRel(("error: Failed to convert %zu bytes of clipboard data from %s to VBox format %#x: %Rrc\n",
                    cbSrcData, pszMimeType, fVBoxFmt, rc));
        RTMemFree(pvSrcData);
    }
    return rc;
}


/**
 * Common worker for starting a data offer sequence.
 */
static PSHCLWLOFFERSLOT vbghWaylandClipboardBeginDataOffer(PSHCLWAYLANDCTX pThis, void *pvOffer)
{
    AssertPtrReturn(pThis, NULL);

    /* Get a slot for taking down this offer. */
    RTCritSectEnter(&pThis->CritSect);
    SHCLWLOFFERSLOT * const pOfferSlot = &pThis->aOurOfferSlots[pThis->idxOurOfferSlots++ % RT_ELEMENTS(pThis->aOurOfferSlots)];
    for (unsigned i = 0; i < RT_ELEMENTS(pOfferSlot->aMimeTypes); i++)
    {
        pOfferSlot->aMimeTypes[i].fFlagsAndPriority = 0;
        pOfferSlot->aMimeTypes[i].pszMimeType       = NULL;
    }
    pOfferSlot->fFormats               = VBOX_SHCL_FMT_NONE;
    pOfferSlot->fHasRevisionNoMimeType = false;
    pOfferSlot->uRevision              = pThis->uRevision;
    pOfferSlot->pCtx                   = pThis;
    pOfferSlot->pvOffer                = pvOffer;

    RTCritSectLeave(&pThis->CritSect);

    return pOfferSlot;
}


/**
 * Common worker for completing a data offer.
 */
static void vbghWaylandClipboardFinishDataOffer(PSHCLWAYLANDCTX pThis, void *pvOffer, PFNVBGHWLCLOFFERRECIVE pfnOfferReceive)
{
    /*
     * Get the last offer slot and see if it matches up to the offer,
     * that our clipboard state hasn't changed, and that it isn't our offer.
     */
    RTCritSectEnter(&pThis->CritSect);

    PSHCLWLOFFERSLOT const pOfferSlot = &pThis->aOurOfferSlots[(pThis->idxOurOfferSlots - 1) % RT_ELEMENTS(pThis->aOurOfferSlots)];
    if (pOfferSlot->pvOffer != pvOffer)
        LogRel4(("%s: Ignore - pOffer %p, expected %p!\n", __func__, pvOffer, pOfferSlot->pvOffer));
    else if (pOfferSlot->uRevision != pThis->uRevision)
        LogRel4(("%s: Ignore - uRevision %RX64 -> %#RX64!\n", __func__, pOfferSlot->uRevision, pThis->uRevision));
    else if (pOfferSlot->fHasRevisionNoMimeType)
        LogRel4(("%s: Ignore our own offer.\n", __func__));
    else
    {
        /*
         * Reset the state with the new offer.
         */
        uint64_t const uRevision = VbghWaylandClipboardResetOurState(pThis, __func__, pOfferSlot);

        /*
         * Cache the data for each VBox format.
         */
        SHCLFORMATS fOurFormats  = pThis->fOurFormats;
        SHCLFORMATS fFormatsLeft = fOurFormats;
        while (fFormatsLeft && uRevision == pThis->uRevision)
        {
            /* Get the index of the first format in the mask. */
            AssertCompile(sizeof(fFormatsLeft) == sizeof(uint32_t));
            unsigned const idxFmt = ASMBitFirstSetU32(fFormatsLeft) - 1;
            AssertBreak(idxFmt < 32); /* paranoia */
            SHCLFORMAT const fThisFmt = RT_BIT_32(idxFmt);

            /* It simplifies error handling if we remove it early. */
            fFormatsLeft &= ~fThisFmt;

            /* Get the mime type before leaving the critsect. */
            const char * const pszMimeType = pThis->aOurMimeTypes[idxFmt].pszMimeType;
            AssertContinue(pszMimeType);

            RTCritSectLeave(&pThis->CritSect);

            /* Read the data for this MIME type. */
            int rc;
            int aFds[2] = {-1,-1};
            if (pipe(aFds) == 0)
            {
                pfnOfferReceive(pvOffer, pszMimeType, aFds[1]);
                close(aFds[1]);

                /* ?? */
                wl_display_flush(pThis->GhCore.pDisplay);

                rc = vbghWaylandClipboardReadGuestDataIntoCache(pThis, aFds[0], idxFmt, uRevision, pszMimeType);
                close(aFds[0]);
            }
            else
            {
                rc = RTErrConvertFromErrno(errno);
                LogRel(("error: %s: pipe failed: %Rrc\n", __func__, rc));
            }

            if (RT_FAILURE(rc))
            {
                fOurFormats &= ~fThisFmt;
                LogRel(("warning: %s: dropping %#x due to %Rrc\n", __func__, fThisFmt, rc));
            }

            RTCritSectEnter(&pThis->CritSect);
        }

        /*
         * Now that we've cached all the data, report it to the host if
         * everything is still fine.
         */
        if (uRevision == pThis->uRevision)
        {
            pThis->fOurFormats = fOurFormats;
            int rc = RTSemEventMultiSignal(pThis->hOurCacheFilledEvent);
            if (RT_FAILURE(rc))
                LogRel(("error: %s: RTSemEventMultiSignal failed: %Rrc\n", __func__, rc));

            if (pThis->pfnReportOurFormats)
                pThis->pfnReportOurFormats(pThis, fOurFormats);
        }
    }

    /* This is mainly for quitting the clipboard-get command. */
    if (pThis->pfnDataOfferComplete)
        pThis->pfnDataOfferComplete(pThis);

    RTCritSectLeave(&pThis->CritSect);
}

/**
 * Indicates that the data offer is being ignored.
 */
static void vbghWaylandClipboardDiscardDataOffer(PSHCLWAYLANDCTX pThis, void *pvOffer)
{
    RT_NOREF(pThis, pvOffer);
    /** @todo invalidate the current offer slot if still valid. */
}


/*
 * WL - wl-data-device-manager - Listening for offers.
 */

/** @callback_method_impl{FNVBGHWLCLOFFERRECIVE} */
static DECLCALLBACK(void) vbghWaylandClipboardWlOfferDataWrapper(void *pvOffer, const char *pszMimeType, int fdWrite)
{
    wl_data_offer_receive((struct wl_data_offer *)pvOffer, pszMimeType, fdWrite);
}


/**
 * Wayland callback: Data Control Offer advertise.
 *
 * Triggered when other Wayland client advertises new clipboard content.
 *
 * @param pvUser            Context data.
 * @param pOffer            Wayland Data Control Offer object.
 * @param pcszMimeType      MIME type of newly available clipboard data.
 */
static void vbghWaylandClipboardWlOfferListener_Offer(void *pvUser, struct wl_data_offer *pOffer,
                                                      const char *pcszMimeType)
{
    LogRel3(("%s: %s\n", __func__, pcszMimeType));
    SHCLWLOFFERSLOT * const pOfferSlot = (SHCLWLOFFERSLOT *)pvUser;
    AssertPtrReturnVoid(pOfferSlot);
    VbghWaylandClipboardOfferAddMimeType(pOfferSlot, pcszMimeType, __func__);
    RT_NOREF(pOffer);
}

/** Irrelevant DnD notification. */
static void vbghWaylandClipboardWlOfferListener_SourceActions(void *pvUser, struct wl_data_offer *pOffer, uint32_t fSourceActions)
{
    RT_NOREF(pvUser, pOffer, fSourceActions);
}

/** Irrelevant DnD notification. */
static void vbghWaylandClipboardWlOfferListener_Action(void *pvUser, struct wl_data_offer *pOffer, uint32_t fDnDAction)
{
    RT_NOREF(pvUser, pOffer, fDnDAction);
}


/** Wayland Data Control Offer interface callbacks. */
static const struct wl_data_offer_listener g_vbghWaylandClipboardWlOfferListener =
{
    /* .offer = */          vbghWaylandClipboardWlOfferListener_Offer,
    /* .source_actions = */ vbghWaylandClipboardWlOfferListener_SourceActions,
    /* .action = */         vbghWaylandClipboardWlOfferListener_Action,
};

/**
 * Data is being offered.
 *
 * What is is going to be used for, isn't yet given that will be determined by
 * the following selection or drop callout.
 */
static void vbglWaylandClipboardWlDeviceListener_DataOffer(void *pvUser, struct wl_data_device *pDevice,
                                                           struct wl_data_offer *pOffer)
{
    LogRel3(("%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer));
    PSHCLWAYLANDCTX const  pThis      = (PSHCLWAYLANDCTX)pvUser;
    PSHCLWLOFFERSLOT const pOfferSlot = vbghWaylandClipboardBeginDataOffer(pThis, pOffer);
    if (pOfferSlot)
    {
        wl_data_offer_add_listener(pOffer, &g_vbghWaylandClipboardWlOfferListener, pOfferSlot);
        wl_display_roundtrip(pThis->GhCore.pDisplay); /** @todo probably not required */
    }
}

/** Irrelevant DnD notification. */
static void vbglWaylandClipboardWlDeviceListener_Enter(void *pvUser, struct wl_data_device *pDevice, uint32_t uSerial,
                                                       struct wl_surface *pSurface, wl_fixed_t x, wl_fixed_t y,
                                                       struct wl_data_offer *pOffer)
{
    RT_NOREF(pvUser, pDevice, uSerial, pSurface, x, y, pOffer);
    /** @todo do we need to remember pOffer so it can be destroyed in Leave? */
}

/** Irrelevant DnD notification. */
static void vbglWaylandClipboardWlDeviceListener_Leave(void *pvUser, struct wl_data_device *pDevice)
{
    RT_NOREF(pvUser, pDevice);
}

/** Irrelevant DnD notification. */
static void vbglWaylandClipboardWlDeviceListener_Motion(void *pvUser, struct wl_data_device *pDevice, uint32_t uTime,
                                                        wl_fixed_t x, wl_fixed_t y)
{
    RT_NOREF(pvUser, pDevice, uTime, x, y);
}

/**
 * Drop notification (DnD).
 */
static void vbglWaylandClipboardWlDeviceListener_Drop(void *pvUser, struct wl_data_device *pDevice)
{
    RT_NOREF(pvUser, pDevice);
}

/**
 * Clipboard selection notification.
 */
static void vbglWaylandClipboardWlDeviceListener_Selection(void *pvUser, struct wl_data_device *pDevice,
                                                           struct wl_data_offer *pOffer)
{
    LogRel(("%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer));
    vbghWaylandClipboardFinishDataOffer((PSHCLWAYLANDCTX)pvUser, pOffer, vbghWaylandClipboardWlOfferDataWrapper);
    /** @todo Do we need wl_data_offer_destroy(pOffer) here? */
}

/** Clipboard data notifications for wl-data-device-manager. */
static struct wl_data_device_listener const g_vbglWlDataDeviceListener =
{
    /* .data_offer = */             vbglWaylandClipboardWlDeviceListener_DataOffer,
    /* .enter = */                  vbglWaylandClipboardWlDeviceListener_Enter,
    /* .leave = */                  vbglWaylandClipboardWlDeviceListener_Leave,
    /* .motion = */                 vbglWaylandClipboardWlDeviceListener_Motion,
    /* .drop = */                   vbglWaylandClipboardWlDeviceListener_Drop,
    /* .selection = */              vbglWaylandClipboardWlDeviceListener_Selection,
};


/*
 * ZWLR
 */

/** @callback_method_impl{FNVBGHWLCLOFFERRECIVE} */
static DECLCALLBACK(void) vbghWaylandClipboardZwlrOfferDataWrapper(void *pvOffer, const char *pszMimeType, int fdWrite)
{
    zwlr_data_control_offer_v1_receive((struct zwlr_data_control_offer_v1 *)pvOffer, pszMimeType, fdWrite);
}


/**
 * Wayland callback: Data Control Offer advertise.
 *
 * Triggered when other Wayland client advertises new clipboard content.
 *
 * @param pvUser            Context data.
 * @param pOffer            Wayland Data Control Offer object.
 * @param pcszMimeType      MIME type of newly available clipboard data.
 */
static void vbghWaylandClipboardZwlrOfferListener_Offer(void *pvUser, struct zwlr_data_control_offer_v1 *pOffer,
                                                        const char *pcszMimeType)
{
    LogRel3(("%s: %s\n", __func__, pcszMimeType));
    SHCLWLOFFERSLOT * const pOfferSlot = (SHCLWLOFFERSLOT *)pvUser;
    AssertPtrReturnVoid(pOfferSlot);
    VbghWaylandClipboardOfferAddMimeType(pOfferSlot, pcszMimeType, __func__);
    RT_NOREF(pOffer);
}

/** Wayland Data Control Offer interface callbacks. */
static const struct zwlr_data_control_offer_v1_listener g_vbghWaylandClipboardZwlrOfferListener =
{
    /* .offer = */      vbghWaylandClipboardZwlrOfferListener_Offer,
};

/**
 * Data is being offered.
 *
 * What is is going to be used for, isn't yet given that will be determined by
 * the following selection or primary_selection callout.
 */
static void vbglWaylandClipboardZwlrDeviceListener_DataOffer(void *pvUser, struct zwlr_data_control_device_v1 *pDevice,
                                                             struct zwlr_data_control_offer_v1 *pOffer)
{
    LogRel3(("%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer));
    PSHCLWAYLANDCTX const  pThis      = (PSHCLWAYLANDCTX)pvUser;
    PSHCLWLOFFERSLOT const pOfferSlot = vbghWaylandClipboardBeginDataOffer(pThis, pOffer);
    if (pOfferSlot)
    {
        zwlr_data_control_offer_v1_add_listener(pOffer, &g_vbghWaylandClipboardZwlrOfferListener, pOfferSlot);
        wl_display_roundtrip(pThis->GhCore.pDisplay); /** @todo probably not required */
    }
}

/**
 * Clipboard selection notification.
 */
static void vbglWaylandClipboardZwlrDeviceListener_Selection(void *pvUser, struct zwlr_data_control_device_v1 *pDevice,
                                                             struct zwlr_data_control_offer_v1 *pOffer)
{
    LogRel(("%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer));
    vbghWaylandClipboardFinishDataOffer((PSHCLWAYLANDCTX)pvUser, pOffer, vbghWaylandClipboardZwlrOfferDataWrapper);
    /** @todo Do we need zwlr_data_control_offer_v1_destroy(pOffer) here? */
}

/**
 * Data Control Device is being destroyed.
 */
static void vbglWaylandClipboardZwlrDeviceListener_Finished(void *pvUser, struct zwlr_data_control_device_v1 *pDevice)
{
    LogRel3(("%s: pDevice=%p\n", __func__, pDevice));
    PSHCLWAYLANDCTX const pThis = (PSHCLWAYLANDCTX)pvUser;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pDevice == pThis->pZwlrDataDevice && RT_VALID_PTR(pDevice));

    zwlr_data_control_device_v1_destroy(pDevice);
    pThis->pZwlrDataDevice = NULL;
}

/**
 * Primary selection notification.
 */
static void vbglWaylandClipboardZwlrDeviceListener_PrimarySelection(void *pvUser, struct zwlr_data_control_device_v1 *pDevice,
                                                                    struct zwlr_data_control_offer_v1 *pOffer)
{
    LogRel(("%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer));
    vbghWaylandClipboardDiscardDataOffer((PSHCLWAYLANDCTX)pvUser, pOffer);
    /** @todo Do we need zwlr_data_control_offer_v1_destroy(pOffer) here? */
}

/** Clipboard data notifications for wlr-data-control-unstable-v1. */
static struct zwlr_data_control_device_v1_listener const g_vbglZwlrDataDeviceListener =
{
    /* .data_offer = */             vbglWaylandClipboardZwlrDeviceListener_DataOffer,
    /* .selection = */              vbglWaylandClipboardZwlrDeviceListener_Selection,
    /* .finished = */               vbglWaylandClipboardZwlrDeviceListener_Finished,
    /* .primary_selection = */      vbglWaylandClipboardZwlrDeviceListener_PrimarySelection,
};


/*
 * EDCP
 */

/** @callback_method_impl{FNVBGHWLCLOFFERRECIVE} */
static DECLCALLBACK(void) vbghWaylandClipboardEdcpOfferDataWrapper(void *pvOffer, const char *pszMimeType, int fdWrite)
{
    ext_data_control_offer_v1_receive((struct ext_data_control_offer_v1 *)pvOffer, pszMimeType, fdWrite);
}


/**
 * Wayland callback: Data Control Offer advertise.
 *
 * Triggered when other Wayland client advertises new clipboard content.
 *
 * @param pvUser            Context data.
 * @param pOffer            Wayland Data Control Offer object.
 * @param pcszMimeType      MIME type of newly available clipboard data.
 */
static void vbghWaylandClipboardEdcpOfferListener_Offer(void *pvUser, struct ext_data_control_offer_v1 *pOffer,
                                                        const char *pcszMimeType)
{
    LogRel3(("%s: %s\n", __func__, pcszMimeType));
    SHCLWLOFFERSLOT * const pOfferSlot = (SHCLWLOFFERSLOT *)pvUser;
    AssertPtrReturnVoid(pOfferSlot);
    VbghWaylandClipboardOfferAddMimeType(pOfferSlot, pcszMimeType, __func__);
    RT_NOREF(pOffer);
}

/** Wayland Data Control Offer interface callbacks. */
static const struct ext_data_control_offer_v1_listener g_vbghWaylandClipboardEdcpOfferListener =
{
    /* .offer = */      vbghWaylandClipboardEdcpOfferListener_Offer,
};


/**
 * Data is being offered.
 *
 * What is is going to be used for, isn't yet given that will be determined by
 * the following selection or primary_selection callout.
 */
static void vbglWaylandClipboardEdcpDeviceListener_DataOffer(void *pvUser, struct ext_data_control_device_v1 *pDevice,
                                                             struct ext_data_control_offer_v1 *pOffer)
{
    LogRel3(("%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer));
    PSHCLWAYLANDCTX const  pThis      = (PSHCLWAYLANDCTX)pvUser;
    PSHCLWLOFFERSLOT const pOfferSlot = vbghWaylandClipboardBeginDataOffer(pThis, pOffer);
    if (pOfferSlot)
    {
        ext_data_control_offer_v1_add_listener(pOffer, &g_vbghWaylandClipboardEdcpOfferListener, pOfferSlot);
        wl_display_roundtrip(pThis->GhCore.pDisplay); /** @todo probably not required */
    }
}

/**
 * Clipboard selection notification.
 */
static void vbglWaylandClipboardEdcpDeviceListener_Selection(void *pvUser, struct ext_data_control_device_v1 *pDevice,
                                                             struct ext_data_control_offer_v1 *pOffer)
{
    LogRel(("%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer));
    vbghWaylandClipboardFinishDataOffer((PSHCLWAYLANDCTX)pvUser, pOffer, vbghWaylandClipboardEdcpOfferDataWrapper);
    /** @todo Do we need ext_data_control_offer_v1_destroy(pOffer) here? */
}

/**
 * Data Control Device is being destroyed.
 */
static void vbglWaylandClipboardEdcpDeviceListener_Finished(void *pvUser, struct ext_data_control_device_v1 *pDevice)
{
    LogRel3(("%s: pDevice=%p\n", __func__, pDevice));
    PSHCLWAYLANDCTX const pThis = (PSHCLWAYLANDCTX)pvUser;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pDevice == pThis->pExtDataDevice && RT_VALID_PTR(pDevice));

    ext_data_control_device_v1_destroy(pDevice);
    pThis->pExtDataDevice = NULL;
}

/**
 * Primary selection notification.
 */
static void vbglWaylandClipboardEdcpDeviceListener_PrimarySelection(void *pvUser, struct ext_data_control_device_v1 *pDevice,
                                                                    struct ext_data_control_offer_v1 *pOffer)
{
    LogRel(("%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer));
    vbghWaylandClipboardDiscardDataOffer((PSHCLWAYLANDCTX)pvUser, pOffer);
    /** @todo Do we need ext_data_control_offer_v1_destroy(pOffer) here? */
}

/** Clipboard data notifications for ext-data-control-v1. */
static struct ext_data_control_device_v1_listener const g_vbglExtDataDeviceListener =
{
    /* .data_offer = */             vbglWaylandClipboardEdcpDeviceListener_DataOffer,
    /* .selection = */              vbglWaylandClipboardEdcpDeviceListener_Selection,
    /* .finished = */               vbglWaylandClipboardEdcpDeviceListener_Finished,
    /* .primary_selection = */      vbglWaylandClipboardEdcpDeviceListener_PrimarySelection,
};


VBGH_DECL(int) VbghWaylandClipboardSetupListening(PSHCLWAYLANDCTX pThis, PRTERRINFO pErrInfo)
{
    int rc;
    switch (pThis->enmProtocol)
    {
        case SHCLWAYLANDPROTO_WL:
            rc = wl_data_device_add_listener(pThis->pWlDataDevice, &g_vbglWlDataDeviceListener, pThis);
            break;
        case SHCLWAYLANDPROTO_ZWLR:
            rc = zwlr_data_control_device_v1_add_listener(pThis->pZwlrDataDevice, &g_vbglZwlrDataDeviceListener, pThis);
            break;
        case SHCLWAYLANDPROTO_EXT:
            rc = ext_data_control_device_v1_add_listener(pThis->pExtDataDevice, &g_vbglExtDataDeviceListener, pThis);
            break;
        default:
            AssertFailedReturn(RTErrInfoSetF(pErrInfo, VERR_INVALID_STATE, "enmProtocol=%d\n", pThis->enmProtocol));
    }
    if (rc == 0)
        return VINF_SUCCESS;
    return RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                         "Failed to register data device listener (%s protocol): %d", pThis->pszProtocol, rc);
}


/**
 * Adds a MIME type prior to reporting a potential new clipboard offer.
 *
 * This will try map the MIME type to a VBox format type and add it to the
 * reverse mapping table, iff it has higher priority than the current type for
 * the format.
 *
 * @returns VBox status code (can be ignored).
 * @param   pOfferSlot      The offset slot to update.
 * @param   pszMimeType     The MIME type being offered.
 * @param   pszCaller       Caller name for logging.
 */
VBGH_DECL(int) VbghWaylandClipboardOfferAddMimeType(SHCLWLOFFERSLOT *pOfferSlot, const char *pszMimeType, const char *pszCaller)
{
    LogRel5(("Wayland announces MIME type: %s\n", pszMimeType));
    AssertPtrReturn(pOfferSlot, VERR_INVALID_POINTER);
    PSHCLWAYLANDCTX const pThis = pOfferSlot->pCtx;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    RTCritSectEnter(&pThis->CritSect);

    if (pThis->uRevision == pOfferSlot->uRevision)
    {
        /* Map the MIME type to a VBox format. */
        uint32_t    fFlagsAndPriority = 0;
        const char *pszPersistentMimeType = NULL;
        SHCLFORMAT uFmt = VbghMimeConvGetVBoxFormatByMime(pszMimeType, &fFlagsAndPriority, &pszPersistentMimeType);
        if (uFmt != VBOX_SHCL_FMT_NONE)
        {
            AssertPtr(pszPersistentMimeType);
            int const idxFmt = ShClFormatToBitNo(uFmt);
            if ((unsigned)idxFmt < RT_ELEMENTS(pOfferSlot->aMimeTypes))
            {
                /* Does it have higher priority than any existing mapping? */
                if (  (fFlagsAndPriority & VBGH_MIME_CONV_F_PRIORITY_MASK)
                    > (pOfferSlot->aMimeTypes[idxFmt].fFlagsAndPriority & VBGH_MIME_CONV_F_PRIORITY_MASK))
                {
                    /* Okay, use this MIME type for the VBox format then. */
                    pOfferSlot->aMimeTypes[idxFmt].pszMimeType       = pszPersistentMimeType;
                    pOfferSlot->aMimeTypes[idxFmt].fFlagsAndPriority = fFlagsAndPriority;
                    pOfferSlot->fFormats |= uFmt;
                    LogRel4(("%s: %s -> VBoxFmt %#x/%u prio %#x\n", pszCaller, pszMimeType, uFmt, idxFmt, fFlagsAndPriority));
                    rc = VINF_SUCCESS;
                }
                else
                {
                    LogRel6(("%s: %s -> VBoxFmt %#x/%u prio %#x - have better (%#x)\n", pszCaller, pszMimeType,
                             uFmt, idxFmt, fFlagsAndPriority, pOfferSlot->aMimeTypes[idxFmt].fFlagsAndPriority));
                    rc = VINF_SUCCESS;
                }
            }
            else
                AssertFailedStmt(rc = VERR_INTERNAL_ERROR_2);
        }
        else
        {
            LogRel6(("%s: %s - no VBox format mapping\n", pszCaller, pszMimeType));
            rc = VERR_NO_DATA;
        }
    }
    else
    {
        LogRel2(("%s: outdated callback (%#RX64 vs %#RX64)\n", pszCaller, pOfferSlot->uRevision, pThis->uRevision));
        rc = VERR_VERSION_MISMATCH;
    }

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}
