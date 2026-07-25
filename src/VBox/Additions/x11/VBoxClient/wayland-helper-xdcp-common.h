/* $Id: wayland-helper-xdcp-common.h 114771 2026-07-25 21:20:37Z knut.osmundsen@oracle.com $ */
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

/* A helper for matching interface and bind to it in registry callback.*/
#define VBCL_WAYLAND_REGISTRY_ADD_MATCH(_pRegistry, _sIfaceName, _uIface, _iface_to_bind_to, _ctx_member, _ctx_member_type, _uVersion) \
    if (RTStrNCmp(_sIfaceName, _iface_to_bind_to.name, VBCL_WAYLAND_INTERFACE_NAME_MAX) == 0) \
    { \
        if (! _ctx_member) \
        { \
            _ctx_member = \
                (_ctx_member_type)wl_registry_bind(_pRegistry, _uIface, &_iface_to_bind_to, _uVersion); \
            VBClLogVerbose(4, "binding to Wayland interface '%s' (%u) v%u\n", _iface_to_bind_to.name, _uIface, wl_proxy_get_version((struct wl_proxy *) _ctx_member)); \
        } \
        AssertPtrReturnVoid(_ctx_member); \
    }

/**
 * MIME type list entry.
 */
typedef struct
{
    /** IPRT list node. */
    RTLISTNODE  Node;
    /** Data MIME type in string representation. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char        szMimeType[RT_FLEXIBLE_ARRAY];
} vbox_wl_dcp_mime_t;

/**
 * DCP session data.
 *
 * A structure which accumulates all the necessary data required to
 * maintain session between host and Wayland for clipboard sharing. */
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

        /** Clipboard format which either host or guest wants to
         *  obtain depending on session type. */
        vbcl::Waitable<volatile SHCLFORMAT>     uFmt;

        /** Clipboard buffer which contains requested data. */
        vbcl::Waitable<volatile uint64_t>       pvDataBuf;

        /** Size of clipboard buffer. */
        vbcl::Waitable<volatile uint32_t>       cbDataBuf;
    } clip;
} vbox_wl_dcp_session_t;

/**
 * A set of objects required to handle clipboard sharing over
 * Data Control Protocol. */
typedef struct
{
    /** Wayland event loop thread. */
    RTTHREAD                                    Thread;

    /** A flag which indicates that Wayland event loop should terminate. */
    volatile bool                               fShutdown;

    /** Communication session between host event loop and Wayland. */
    vbox_wl_dcp_session_t                       Session;

    /** MIME types data cache. */
    VBGHMIMECONVCACHE                           hCache;

    /** When set, incoming clipboard announcements will
     *  be ignored. This flag is used in order to prevent a feedback
     *  loop when host advertises clipboard data to Wayland. In this case,
     *  Wayland will send the same advertisements back to us.  */
    bool                                        fIngnoreWlClipIn;

    /** A flag which indicates that host has announced new clipboard content
    *   and now Wayland event loop thread should pass this information to
    *   other Wayland clients. */
    vbcl::Waitable<volatile bool>               fSendToGuest;

    /** Connection handle to the host clipboard service. */
    PVBGLR3SHCLCMDCTX                           pClipboardCtx;

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
RTDECL(int) vbcl_wayland_xdcp_next_event(vbox_wl_xdcp_base_ctx_t *pCtx);

/**
 * Reset previously initialized session.
 *
 * @param   pCtx                Context data.
 */
RTDECL(void) vbcl_wayland_xdcp_session_prepare(vbox_wl_xdcp_base_ctx_t *pCtx);

/**
 * Collect clipboard format advertised by Wayland.
 *
 * This callback adds MIME type just advertised by Wayland into a list
 * of MIME types which in turn later will be advertised to the host.
 *
 * @returns IPRT status code.
 * @param   pEnmCtx             Format enumeration conext data.
 */
RTDECL(int) vbcl_wayland_xdcp_add_fmt(struct vbcl_wl_dcp_enumerate_ctx *pEnmCtx);

/**
 * Reset context.
 *
 * @param   pCtx                Context data.
 * @param   fShutdown           A flag to indicate if session resources
 *                              need to be deallocated.
 */
RTDECL(void) vbcl_wayland_xdcp_reset_ctx(vbox_wl_xdcp_base_ctx_t *pCtx, bool fShutdown);

/**
 * Read clipboard data from Wayland and cache it.
 *
 * Clipboard data is cached in internal VBox representation format.
 *
 * @returns IPRT status code.
 * @param   fd                  File descriptor provided by Wayland to read data from.
 * @param   pCtx                Context data.
 * @param   pcszMimeType        Clipboard data format in string representation.
 */
RTDECL(int) vbcl_wayland_xdcp_get_guest_clipboard(int fd, vbox_wl_xdcp_base_ctx_t *pCtx, const char *pcszMimeType);

/**
 * Write clipboard data to Wayland.
 *
 * @returns IPRT status code.
 * @param   fd                  File descriptor provided by Wayland to write data to.
 * @param   pCtx                Context data.
 * @param   pcszMimeType        Clipboard data format in string representation.
 */
RTDECL(int) vbcl_wayland_xdcp_set_guest_clipboard(int fd, vbox_wl_xdcp_base_ctx_t *pCtx, const char *pcszMimeType);

/**
 * Read clipboard data from host and cache it.
 *
 * Clipboard data is cached in internal VBox representation format.
 *
 * @returns IPRT status code.
 * @param   pCtx                Context data.
 * @param   fFmts               List of formats for Wayland to choose from in bitmask representation.
 */
RTDECL(int) vbcl_wayland_xdcp_get_host_clipboard(vbox_wl_xdcp_base_ctx_t *pCtx, SHCLFORMATS fFmts);

/**
 * Write clipboard data to host.
 *
 * @returns IPRT status code.
 * @param   pCtx                Context data.
 * @param   uFmt                Clipboard data format.
 */

RTDECL(int) vbcl_wayland_xdcp_set_host_clipboard(vbox_wl_xdcp_base_ctx_t *pCtx, SHCLFORMAT uFmt);

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_xdcp_common_h */

