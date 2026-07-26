/** $Id: SharedClipboard-Wayland.h 114776 2026-07-26 00:19:33Z knut.osmundsen@oracle.com $ */
/** @file
 * Shared Clipboard - Common Wayland code.
 */

/*
 * Copyright (C) 2023-2026 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_GuestHost_SharedClipboard_Wayland_h
#define VBOX_INCLUDED_GuestHost_SharedClipboard_Wayland_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/Wayland.h>
#include <VBox/GuestHost/WaylandPopup.h>

/* Forward declaration of Wayland structures: */
struct ext_data_control_manager_v1;
struct zwlr_data_control_manager_v1;
struct wl_data_device_manager;
struct ext_data_control_device_v1;
struct zwlr_data_control_device_v1;
struct wl_data_device;
struct ext_data_control_source_v1;
struct zwlr_data_control_source_v1;
struct wl_data_source;


/** MIME type to VBOX_SHCL_FMT_XXX mapping table entry. */
typedef struct SHCLWLMIMEMAPENTRY
{
    /** The MIME type (from mime-type-converter.cpp). */
    char const     *pszMimeType;
    /** The priority and flags of this MIME type. */
    uint32_t        fFlagsAndPriority;
} SHCLWLMIMEMAPENTRY;

/**
 * Data offer collection slot.
 */
typedef struct SHCLWLOFFERSLOT
{
    /** MIME types for each VBOX_SHCL_FMT_XXX (indexed by its log2 value).*/
    SHCLWLMIMEMAPENTRY      aMimeTypes[VBOX_SHCL_FMT_LAST_BIT + 1];
    /** The VBox formats on offer (VBOX_SHCL_FMT_XXX) - summary of aMimeTypes. */
    SHCLFORMATS             fFormats;
    /** Set if VBOX_CLIPBOARD_MIME_TYPE_REVISION_NO is on offer.   */
    bool                    fHasRevisionNoMimeType;
    /** The revision at the start of the offer.   */
    uint64_t                uRevision;
    /** Pointer back to the wayland context. */
    struct SHCLWAYLANDCTX  *pCtx;
    /** The offer pointer. */
    void                   *pvOffer;
} SHCLWLOFFERSLOT;
/** Poitner to a Wayland data offer collection slot. */
typedef SHCLWLOFFERSLOT *PSHCLWLOFFERSLOT;

/** Reason why for a SHCLWAYLANDCTX::pfnDataOfferComplete callback. */
typedef enum SHCLWLOFFERCOMPLETE
{
    SHCLWLOFFERCOMPLETE_INVALID = 0,
    SHCLWLOFFERCOMPLETE_NEW_OFFER_IN_PROGRESS,
    SHCLWLOFFERCOMPLETE_REVISION_CHANGED,
    SHCLWLOFFERCOMPLETE_OUR_OWN,
    SHCLWLOFFERCOMPLETE_REPORTED,
    SHCLWLOFFERCOMPLETE_32BIT_HACK = 0x7fffffff
} SHCLWLOFFERCOMPLETE;

/** Indicates which Wayland Clipboard protocol is being used. */
typedef enum SHCLWAYLANDPROTO
{
    SHCLWAYLANDPROTO_INVALID = 0,
    SHCLWAYLANDPROTO_WL,
    SHCLWAYLANDPROTO_ZWLR,
    SHCLWAYLANDPROTO_EXT,
    SHCLWAYLANDPROTO_END,
    SHCLWAYLANDPROTO_32BIT_HACK = 0x7fffffff
} SHCLWAYLANDPROTO;

/**
 * Wayland context.
 */
typedef struct SHCLWAYLANDCTX
{
    /** Protects everything down to, but not including, aOurOfferSlots. */
    RTCRITSECT          CritSect;
    /** Revision number for the data protected by the critical section.
     *
     * This is increased by two every time there is a change in the clipboard data.
     * It is set to an ODD number when the guest is the clipboard provider and EVEN
     * when the host is. */
    uint64_t volatile   uRevision;

    /** Caching the other-side's clipboard data in VBox format.
     * This is filled as-needed.  */
    SHCLCACHE           OtherCache;
    /** The other side's VBox formats on offer (VBOX_SHCL_FMT_XXX). */
    SHCLFORMATS         fOtherFormats;

    /** Caching our side's clipboard data.
     * Upon receiving an announcement, we get all the relevant data from the
     * clipboard and puts it in the cache, in VBox format.  This simplifies IPC
     * interaction. */
    SHCLCACHE           OurCache;
    /** MIME types for each VBOX_SHCL_FMT_XXX (indexed by its log2 value).*/
    SHCLWLMIMEMAPENTRY  aOurMimeTypes[VBOX_SHCL_FMT_LAST_BIT + 1];
    /** Event that get's set when the cache has been filled.
     * @note Should've been a condition variable, as there is a potential race if
     *       a 2nd clipboard announcement is processed between someone leaving the
     *       critsect and starts waiting. Harmless, though, since they'll be
     *       waken up once that cache filling is completed. */
    RTSEMEVENTMULTI     hOurCacheFilledEvent;
    /** Our side's VBox formats on offer (VBOX_SHCL_FMT_XXX). */
    SHCLFORMATS         fOurFormats;

    /** Next slot in aOurOfferSlots. */
    uint32_t            idxOurOfferSlots;
    /** HACK: Circular callback context slots. */
    SHCLWLOFFERSLOT     aOurOfferSlots[8];

    /** Core guest-host Wayland data. */
    VBGHWAYLANDCORE     GhCore;

    /** Pointer to the extended data control manager interface, if available. */
    struct ext_data_control_manager_v1     *pExtDataControlManager;
    /** Pointer to the experimental data control manager interface, if available. */
    struct zwlr_data_control_manager_v1    *pZwlrDataControlManager;
    /** Pointer to the wayland data device mananager, if available. */
    struct wl_data_device_manager          *pWlDataDeviceManager;

    /** The Wayland clipboard protocol name. */
    const char                             *pszProtocol;
    /** The protocol we're using. */
    SHCLWAYLANDPROTO                        enmProtocol;
    /** The seat entry we're using.  */
    PVBGHWAYLANDSEAT                        pSeatEntry;
    /** Pointer to the extended data control device if we're using that protocol. */
    struct ext_data_control_device_v1      *pExtDataDevice;
    /** Pointer to the experimental data control device if we're using that protocol. */
    struct zwlr_data_control_device_v1     *pZwlrDataDevice;
    /** Pointer to the wayland data control device if we're using that protocol. */
    struct wl_data_device                  *pWlDataDevice;

    /** Pointer to the extended data control source of our offering of remote
     *  data, if we're using that protocol. */
    struct ext_data_control_source_v1      *pExtDataSource;
    /** Pointer to the experimental data control source of our offering of remote
     *  data, if we're using that protocol. */
    struct zwlr_data_control_source_v1     *pZwlrDataSource;
    /** Pointer to the wayland data source of our offering of remote data,
     *  if we're using that protocol. */
    struct wl_data_source                  *pWlDataSource;

    /** Process setting clipboard content for the wl-data-device-manager protocol. */
    RTPROCESS                               hProcClipboardSet;
    /** Pipe use to trigger gentle termination of hProcClipboardSet.
     * The other (read) end of the pipe is passed down to the child which will poll
     * it for HUP/ERR, which triggers when the parent closes the this pipe end. */
    RTPIPE                                  hPipeClipboardSet;

    /**
     * Optional callback for reporting our formats to the other side.
     *
     * @note Called from inside the critical section.
     */
    DECLCALLBACKMEMBER(void, pfnReportOurFormats,(struct SHCLWAYLANDCTX *pWlCtx, SHCLFORMATS fOurFormats));

    /**
     * Optional callback notification that a data offer completed.
     *
     * @note Called from inside the critical section.
     */
    DECLCALLBACKMEMBER(void, pfnDataOfferComplete,(struct SHCLWAYLANDCTX *pWlCtx, SHCLWLOFFERCOMPLETE enmReason));

    /**
     * Optional callback notification that our offering has been cancelled.
     *
     * This typically means that the local clipboard data has been replaced and our
     * clipboard entry is no longer needed and can be destroyed.
     *
     * @note Called from outside the critical section.
     */
    DECLCALLBACKMEMBER(void, pfnDataSourceCancelled,(struct SHCLWAYLANDCTX *pWlCtx));

    /**
     * Callback for requesting remote data.
     *
     * @returns VBox status code.
     * @retval  VERR_NO_DATA if no data available.
     * @param   pWlCtx          Pointer to this structure.
     * @param   uFmt            One VBOX_SHCL_FMT_XXX value indicate what to get.
     * @param   ppvData         Where to return the pointer to data buffer. Call
     *                          pfnQueryRemoteDataFree to free it.
     * @param   pcbData         Returns the amout of data returned on success.
     * @note Called from outside the critical section.
     */
    DECLCALLBACKMEMBER(int, pfnQueryRemoteData, (struct SHCLWAYLANDCTX *pWlCtx, SHCLFORMAT uFmt,
                                                 void **ppvData, uint32_t *pcbData));

    /**
     * Callback for freeing data returned by pfnQueryRemoteData.
     *
     * @param   pWlCtx          Pointer to this structure.
     * @param   pvData          Buffer returned by pfnQueryRemoteData.
     * @param   cbData          Size returned by pfnQueryRemoteData.
     * @note    May be called both inside and outside the critical section.
     */
    DECLCALLBACKMEMBER(int, pfnQueryRemoteDataFree, (struct SHCLWAYLANDCTX *pWlCtx, void *pvData, size_t cbData));

    /** The popup for use with the wl_data_device_manager protocol. */
    VBGHWAYLANDPOPUP                        Popup;
} SHCLWAYLANDCTX;
/** Pointer to a wayland clipboard context. */
typedef SHCLWAYLANDCTX *PSHCLWAYLANDCTX;

/** Checks if the revision indicates other side's clipboard ownership. */
#define SHCLWLCTX_REV_IS_OTHER(a_uRevision) (((a_uRevision) & 1) == 0)
/** Checks if the revision indicates our side's clipboard ownership. */
#define SHCLWLCTX_REV_IS_OUR(a_uRevision)   (((a_uRevision) & 1) == 1)

/** MIME type used for storing SHCLWAYLANDCTX:uRevision and flag the (wayland) clipboard as our. */
#define VBOX_CLIPBOARD_MIME_TYPE_REVISION_NO    "application/x.virtualbox.vboxclient.revno"

RT_C_DECLS_BEGIN

VBGH_DECL(int)      VbghWaylandClipboardInit(PSHCLWAYLANDCTX pThis, PRTERRINFO pErrInfo);
VBGH_DECL(void)     VbghWaylandClipboardTerm(PSHCLWAYLANDCTX pThis);
VBGH_DECL(uint64_t) VbghWaylandClipboardResetOurState(PSHCLWAYLANDCTX pThis, const char *pszCaller, SHCLWLOFFERSLOT *pOfferSlot);
VBGH_DECL(int)      VbghWaylandClipboardSetupListening(PSHCLWAYLANDCTX pThis, PRTERRINFO pErrInfo);
VBGH_DECL(int)      VbghWaylandClipboardSetupOffer(PSHCLWAYLANDCTX pThis, PRTERRINFO pErrInfo);

VBGH_DECL(int)      VbghWaylandClipboardPartialInit(PSHCLWAYLANDCTX pThis);
VBGH_DECL(void)     VbghWaylandClipboardPartialTerm(PSHCLWAYLANDCTX pThis);
VBGH_DECL(int)      VbghWaylandClipboardOfferAddMimeType(SHCLWLOFFERSLOT *pOfferSlot, const char *pszMimeType,
                                                         const char *pszCaller);

VBGH_DECL(int)      VbghlWaylandClipboardQueryRemoteData(PSHCLWAYLANDCTX pThis, const char *pszMimeType,
                                                         void **ppvOutData, size_t *pcbOutData);
VBGH_DECL(int)      VbghWaylandClipboardMakeDataOffering(PSHCLWAYLANDCTX pThis, const char *pszPopupTitle,
                                                         const char *pszPopupClass, PRTERRINFO pErrInfo);

RT_C_DECLS_END


#endif /* !VBOX_INCLUDED_GuestHost_SharedClipboard_Wayland_h */
