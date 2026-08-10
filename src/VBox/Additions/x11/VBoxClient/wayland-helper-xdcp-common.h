/* $Id: wayland-helper-xdcp-common.h 114745 2026-07-21 18:40:35Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Definitions for Data Control protocols family helpers.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_xdcp_common_h
#define GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_xdcp_common_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** Environment variable which points to which Wayland compositor we should connect.
 * Must always be checked. */
#define VBCL_ENV_WAYLAND_DISPLAY                            "WAYLAND_DISPLAY"

/* Maximum length of Wayland interface name. */
#define VBCL_WAYLAND_INTERFACE_NAME_MAX                     (64)
/* Maximum waiting time interval for Wayland socket I/O to start. */
#define VBCL_WAYLAND_IO_TIMEOUT_MS                          (500)

/* Data chunk size when reading clipboard data from Wayland. */
#define VBOX_WAYLAND_BUFFER_CHUNK_SIZE                      (_1M)
/* Data chunk increment size to grow local buffer when it is not big enough. */
#define VBOX_WAYLAND_BUFFER_CHUNK_INC_SIZE                  (_4M)
/* Maximum length of clipboard buffer. */
#define VBOX_WAYLAND_BUFFER_MAX                             (_16M)

/** Minimum version numbers of Wayland interfaces we expect a compositor to provide. */
#define VBCL_WAYLAND_DATA_DEVICE_MANAGER_VERSION_MIN        (3)
#define VBCL_WAYLAND_SEAT_VERSION_MIN                       (5)
#define VBCL_WAYLAND_ZWLR_DATA_CONTROL_MANAGER_VERSION_MIN  (1)

/**
 * A helper for the registry callback for binding and returning if the current
 * entry matches.
 */
#define VBCL_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(a_pRegistry, a_pszIfaceName, a_uIfaceName, a_IfaceToBindTo, \
                                                     a_CtxMember, a_CtxMemberType, a_uVersion) do { \
        if (RTStrNCmp(a_pszIfaceName, a_IfaceToBindTo.name, VBCL_WAYLAND_INTERFACE_NAME_MAX) == 0) \
        { \
            if (!(a_CtxMember)) \
            { \
                a_CtxMember = (a_CtxMemberType)wl_registry_bind(a_pRegistry, a_uIfaceName, &a_IfaceToBindTo, a_uVersion); \
                VBClLogVerbose(4, "binding to Wayland interface '%s' (%u) v%u -> %p\n", a_IfaceToBindTo.name, a_uIfaceName, \
                               wl_proxy_get_version((struct wl_proxy *)(a_CtxMember)), (a_CtxMember)); \
            } \
            AssertPtr(a_CtxMember); \
            return; \
        } \
    } while (0)


/**
 * DCP session data.
 *
 * A structure which accumulates all the necessary data required to
 * maintain session between host and Wayland for clipboard sharing.
 */
typedef struct
{
    /** Generic VBoxClient Wayland session data (synchronization point). */
    vbcl_wl_session_t Base;

    /** Session data for clipboard sharing.
     *
     *  This data will be filled sequentially piece by piece by both
     *  sides - host event loop and Wayland event loop until clipboard
     *  buffer is obtained.
     */
    struct
    {
        /** List of MIME types which are being advertised by guest (vbox_wl_dcp_mime_t).
         * @todo r=bird: This is not how you use the list!!  This should be a
         *       RTLISTANCHOR instance, not a full node! */
        RTLISTANCHOR                            mimeTypesList;

        /** Bitmask which represents list of clipboard formats which
         *  are being advertised either by host or guest depending
         *  on session type. */
        vbcl::Waitable<volatile SHCLFORMATS>    fFmts;
    } clip;
} vbox_wl_dcp_session_t;

/**
 * A set of objects required to handle clipboard sharing over Ext or Legacy Data
 * Control Protocol.
 */
typedef struct
{
    /** Wayland event loop thread. */
    RTTHREAD                                    Thread;

    /** A flag which indicates that Wayland event loop should terminate. */
    volatile bool                               fShutdown;

    /** Communication session between host event loop and Wayland. */
    vbox_wl_dcp_session_t                       Session;

    /** When set, incoming clipboard announcements will be ignored.
     *
     * This flag is used in order to prevent a feedback loop when host advertises
     * clipboard data to Wayland. In this case, Wayland will send the same
     * advertisements back
     * to us. */
    bool                                        fIngnoreWlClipIn;

    /** A flag which indicates that host has announced new clipboard content
    *   and now Wayland event loop thread should pass this information to
    *   other Wayland clients. */
    vbcl::Waitable<volatile bool>               fSendToGuest;

    /** Pointer to the shared clipboard context (where this crap actually should've
     *  been living in the first place).  */
    PSHCLCONTEXT                                pShClCtx;

    /** Wayland compositor connection object. */
    struct wl_display                           *pDisplay;

    /** Wayland registry object. */
    struct wl_registry                          *pRegistry;

    /** Wayland Seat object. */
    struct wl_seat                              *pSeat;

} vbox_wl_xdcp_base_ctx_t;


/** Data required to write clipboard content to Wayland. */
struct vbcl_wl_dcp_write_ctx
{
    /** Content MIME type in string representation. */
    const char *pcszMimeType;
    /** Active file descriptor to write data into. */
    int32_t fd;
};

/** Data required to enumerate clipboard content by MIME type. */
struct vbcl_wl_dcp_enumerate_ctx
{
    /** Content MIME type in string representation. */
    const char *pcszMimeType;
    /** Active file descriptor to write data into. */
    vbox_wl_dcp_session_t *pSession;
};


/**
 * Read the next event from Wayland compositor.
 *
 * Implements custom reader function which can be interrupted
 * on service termination request.
 *
 * @returns IPRT status code.
 * @param   pCtx                Context data.
 */
int vbcl_wayland_xdcp_next_event(vbox_wl_xdcp_base_ctx_t *pCtx);

/**
 * Reset previously initialized session.
 *
 * @param   pCtx                Context data.
 */
void vbcl_wayland_xdcp_session_prepare(vbox_wl_xdcp_base_ctx_t *pCtx);

/**
 * Initializes the common context.
 *
 * @returns VBox status code.
 * @param   pCtx                Pointer to the uninitialized context.
 */
int VBClWaylandXdcpCtxInit(vbox_wl_xdcp_base_ctx_t *pCtx);

/**
 * Re-Initializes the common context.
 *
 * @param   pCtx                Pointer to the context to re-initialize.
 */
void VBClWaylandXdcpCtxReInit(vbox_wl_xdcp_base_ctx_t *pCtx);

/**
 * Terminates (uninitalizes) the common context.
 *
 * @param   pCtx                Pointer to the context to terminate.
 */
void VBClWaylandXdcpCtxTerm(vbox_wl_xdcp_base_ctx_t *pCtx);

int VBClWaylandXdcpSetGuestClipboard(int fd, vbox_wl_xdcp_base_ctx_t *pCtx, const char *pcszMimeType);

/** Parameters for vbclWaylandHlpEdcpGhClipFillCacheAndReport.   */
typedef struct VBCLWLHLP_XDCP_FILL_OUR_CACHE_FROM_OFFER_AND_REPORT_ARGS_T
{
    /** The VBoxClient shared clipboard context. */
    PSHCLCONTEXT                        pShClCtx;
    /** The revision this report is for.   */
    uint64_t                            uRevision;
    /** For some wl_display_flush call. */
    struct wl_display                  *pDisplay;
    /** struct zwlr_data_control_offer_v1 or ext_data_control_offer_v1 pointer
     *  used by pfnOfferReceive. */
    void                               *pvOffer;
    /** zwlr_data_control_offer_v1_receive / ext_data_control_offer_v1_receive
     *  wrapper. */
    DECLCALLBACKMEMBER(void, pfnOfferReceive,(struct VBCLWLHLP_XDCP_FILL_OUR_CACHE_FROM_OFFER_AND_REPORT_ARGS_T *pArgs,
                                              const char *pszMimeType, int fdWrite));
} VBCLWLHLP_XDCP_FILL_OUR_CACHE_FROM_OFFER_AND_REPORT_ARGS_T;
DECLCALLBACK(int) VBClWaylandXdcpFillOurCacheFromOfferAndReport(void *pvUser);


/**
 * Reports the host clipboard data to the guest.
 *
 * @returns IPRT status code.
 * @param   pCtx                Context data.
 * @param   fFmts               List of formats for Wayland to choose from in bitmask representation.
 */
int VBClWaylandXdcpReportHostClipboardFormats(vbox_wl_xdcp_base_ctx_t *pCtx, SHCLFORMATS fFmts);


#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_xdcp_common_h */

