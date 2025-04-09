/* $Id: wayland-helper-dcp.cpp 109143 2025-04-09 11:19:48Z vadim.galitsyn@oracle.com $ */
/** @file
 * Guest Additions - Data Control Protocol (DCP) helper for Wayland.
 *
 * This module implements Shared Clipboard support for Wayland guests
 * using Data Control Protocol interface.
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

#include <iprt/env.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <VBox/GuestHost/mime-type-converter.h>

#include "VBoxClient.h"
#include "clipboard.h"
#include "wayland-helper.h"
#include "wayland-helper-ipc.h"

#include "wayland-client-protocol.h"
#include "wlr-data-control-unstable-v1.h"

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

/* Node of mime-types list. */
typedef struct
{
    /** IPRT list node. */
    RTLISTNODE  Node;
    /** Data mime-type in string representation. */
    char        *pszMimeType;
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
        /** List of mime-types which are being advertised by guest. */
        vbox_wl_dcp_mime_t                      mimeTypesList;

        /** Bitmask which represents list of clipboard formats which
         *  are being advertised either by host or guest depending
         *  on session type. */
        vbcl::Waitable<volatile SHCLFORMATS>    fFmts;

        /** Clipboard format which either host or guest wants to
         *  obtain depending on session type. */
        vbcl::Waitable<volatile SHCLFORMAT>     uFmt;

        /** Clipboard buffer which contains requested data. */
        vbcl::Waitable<volatile uint64_t>       pvClipboardBuf;

        /** Size of clipboard buffer. */
        vbcl::Waitable<volatile uint32_t>       cbClipboardBuf;
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

    /** Wayland Data Device object. */
    struct zwlr_data_control_device_v1          *pDataDevice;

    /** Wayland Data Control Manager object. */
    struct zwlr_data_control_manager_v1         *pDataControlManager;
} vbox_wl_dcp_ctx_t;

/** Data required to write clipboard content to Wayland. */
struct vbcl_wl_dcp_write_ctx
{
    /** Content mime-type in string representation. */
    const char *sMimeType;
    /** Active file descriptor to write data into. */
    int32_t fd;
};

/** Helper context. */
static vbox_wl_dcp_ctx_t g_DcpCtx;

static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_hg_clip_report_join2_cb(vbcl_wl_session_type_t enmSessionType, void *pvUser);
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_hg_clip_report_join3_cb(vbcl_wl_session_type_t enmSessionType, void *pvUser);


/**********************************************************************************************************************************
 * Wayland low level operations.
 *********************************************************************************************************************************/


/**
 * A helper function which reallocates buffer to bigger size.
 *
 * This function will attempt to re-allocate specified buffer by cbChunk bytes.
 * If failed, caller is responsible for freeing input buffer. On success, output
 * buffer must be freed by caller.
 *
 * @returns IPRT status code.
 * @param   pvBufIn             Previously allocated buffer which size needs to be increased.
 * @param   cbBufIn             Size of input buffer.
 * @param   cbChunk             Amount of bytes by which caller wants to increase buffer size.
 * @param   cbMax               Maximum size of output buffer.
 * @param   ppBufOut            Output buffer (must be freed by caller).
 * @param   pcbBufOut           Size of output buffer.
 */
RTDECL(int) vbcl_wayland_hlp_dcp_grow_buffer(void *pvBufIn, size_t cbBufIn, size_t cbChunk, size_t cbMax,
                                             void **ppBufOut, size_t *pcbBufOut)
{
    int rc = VERR_NO_MEMORY;

    /* How many chunks were already added to the buffer. */
    int cChunks = cbBufIn / cbChunk;
    /* Size of a chunk to be added to already allocated buffer. */
    size_t cbCurrentChunk = 0;

    if (cbBufIn < cbMax)
    {
        void *pvBuf;

        /* Calculate size of a chunk which can be added to already allocated memory
         * in a way that resulting buffer size will not exceed cbMax. Always add
         * the extra '\0' byte to the end of allocated area for safety reasons. */
        cbCurrentChunk = RT_MIN(cbMax, cbChunk * (cChunks + 1)) - cbBufIn + 1;
        pvBuf = RTMemReallocZ(pvBufIn, cbBufIn, cbBufIn + cbCurrentChunk);
        if (RT_VALID_PTR(pvBuf))
        {
            LogRel(("Wayland: buffer size increased from %u to %u bytes\n", cbBufIn, cbBufIn + cbCurrentChunk));
            *ppBufOut = pvBuf;
            *pcbBufOut = cbBufIn + cbCurrentChunk;
            rc = VINF_SUCCESS;
        }
        else
        {
            LogRel(("Wayland: unable to allocate buffer of size of %u bytes: no memory\n", cbBufIn + cbCurrentChunk));
            rc = VERR_NO_MEMORY;
        }
    }
    else
    {
        LogRel(("Shared Clipboard: unable to re-allocate buffer: size of %u bytes exceeded\n", cbMax));
        rc = VERR_BUFFER_OVERFLOW;
    }

    return rc;
}

/**
 * A helper function for reading from file descriptor until EOF.
 *
 * Reads clipboard data from Wayland via file descriptor.
 *
 * @returns IPRT status code.
 * @param   fd                  A file descriptor to read data from.
 * @param   ppvBuf              Newly allocated output buffer (must be freed by caller).
 * @param   pcbBuf              Size of output buffer.
 */
RTDECL(int) vbcl_wayland_hlp_dcp_read_wl_fd(int fd, void **ppvBuf, size_t *pcbBuf)
{
    int rc = VERR_NO_MEMORY;

    struct timeval tv;
    fd_set rfds;

    /* Amount of payload actually read from Wayland fd in bytes. */
    size_t cbDst = 0;
    /* Dynamically growing buffer to store Wayland clipboard. */
    void *pvDst = NULL;
    /* Number of bytes currently allocated to read entire
     * Wayland buffer content (actual size of pvDst). */
    size_t cbGrowingBuffer = 0;
    /* Number of bytes read from Wayland fd per attempt. */
    size_t cbRead = 0;

    /* Start with allocating one chunk and grow buffer later if needed. */
    cbGrowingBuffer = VBOX_WAYLAND_BUFFER_CHUNK_INC_SIZE + 1 /* '\0' */;
    pvDst = RTMemAllocZ(cbGrowingBuffer);
    if (RT_VALID_PTR(pvDst))
    {
        /* Read everything from given fd. */
        while (1)
        {
            tv.tv_sec  = 0;
            tv.tv_usec = VBCL_WAYLAND_IO_TIMEOUT_MS * 1000;

            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);

            /* Wait until data is available. */
            if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0)
            {
                /* Check if backing buffer size is big enough to store one more data chunk
                 * read from fd. If not, try to increase buffer by size of chunk x 2. */
                if (cbDst + VBOX_WAYLAND_BUFFER_CHUNK_SIZE > cbGrowingBuffer)
                {
                    void *pBufTmp = NULL;

                    rc = vbcl_wayland_hlp_dcp_grow_buffer(
                        pvDst, cbGrowingBuffer, VBOX_WAYLAND_BUFFER_CHUNK_INC_SIZE,
                        VBOX_WAYLAND_BUFFER_MAX, &pBufTmp, &cbGrowingBuffer);

                    if (RT_FAILURE(rc))
                    {
                        RTMemFree(pvDst);
                        break;
                    }
                    else
                        pvDst = pBufTmp;
                }

                /* Read all data from fd until EOF. */
                cbRead = read(fd, (void *)((uint8_t *)pvDst + cbDst), VBOX_WAYLAND_BUFFER_CHUNK_SIZE);
                if (cbRead > 0)
                {
                    LogRel(("Wayland: read chunk of %u bytes from Wayland\n", cbRead));
                    cbDst += cbRead;
                }
                else
                {
                    /* EOF has been reached. */
                    LogRel(("Wayland: read %u bytes from Wayland\n", cbDst));

                    if (cbDst > 0)
                    {
                        rc = VINF_SUCCESS;
                        *ppvBuf = pvDst;
                        *pcbBuf = cbDst;
                    }
                    else
                    {
                        rc = VERR_NO_DATA;
                        RTMemFree(pvDst);
                    }

                    break;
                }
            }
            else
            {
                rc = VERR_TIMEOUT;
                break;
            }
        }
    }

    return rc;
}

/**
 * A helper function for writing to a file descriptor provided by Wayland.
 *
 * @returns IPRT status code.
 * @param   fd                  A file descriptor to write data to.
 * @param   pvBuf               Data buffer.
 * @param   cbBuf               Size of data buffer.
 */
RTDECL(int) vbcl_wayland_hlp_dcp_write_wl_fd(int fd, void *pvBuf, size_t cbBuf)
{
    struct timeval tv;
    fd_set wfds;

    int rc = VINF_SUCCESS;

    tv.tv_sec  = 0;
    tv.tv_usec = VBCL_WAYLAND_IO_TIMEOUT_MS * 1000;

    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);

    /* Wait until data is available. */
    if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0)
    {
        if (FD_ISSET(fd, &wfds))
        {
            ssize_t cbWritten = write(fd, pvBuf, cbBuf);
            if (cbWritten != (ssize_t)cbBuf)
            {
                VBClLogError("cannot write clipboard data, written %d out of %d bytes\n",
                             cbWritten, cbBuf);
                rc = VERR_PIPE_NOT_CONNECTED;
            }
            else
                VBClLogVerbose(5, "written %u bytes to Wayland clipboard\n", cbWritten);
        }
        else
        {
            VBClLogError("cannot write fd\n");
            rc = VERR_TIMEOUT;
        }
    }
    else
        rc = VERR_TIMEOUT;

    return rc;
}

/**
 * Read the next event from Wayland compositor.
 *
 * Implements custom reader function which can be interrupted
 * on service termination request.
 *
 * @returns IPRT status code.
 * @param   pCtx                Context data.
 */
static int vbcl_wayland_hlp_dcp_next_event(vbox_wl_dcp_ctx_t *pCtx)
{
    int rc = VINF_SUCCESS;

    struct timeval tv;
    fd_set rfds, efds;
    int fd;

    /* Instead of using wl_display_dispatch() directly, implement
     * custom event loop handling as recommended in Wayland documentation.
     * Thus, we can have a control over Wayland fd polling and in turn
     * can request event loop thread to shutdown when needed. */

    tv.tv_sec  = 0;
    tv.tv_usec = VBCL_WAYLAND_IO_TIMEOUT_MS * 1000;

    fd = wl_display_get_fd(pCtx->pDisplay);

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    FD_ZERO(&efds);
    FD_SET(fd, &efds);

    while (wl_display_prepare_read(pCtx->pDisplay) != 0)
        wl_display_dispatch(pCtx->pDisplay);

    wl_display_flush(pCtx->pDisplay);

    if (select(fd + 1, &rfds, NULL, &efds, &tv) > 0)
        wl_display_read_events(pCtx->pDisplay);
    else
    {
        wl_display_cancel_read(pCtx->pDisplay);
        rc = VERR_TIMEOUT;
    }

    wl_display_dispatch_pending(pCtx->pDisplay);

    return rc;
}


/**********************************************************************************************************************************
 * Host Clipboard service callbacks.
 *********************************************************************************************************************************/


/**
 * Release session resources.
 *
 * @param   pSession        Session data.
 */
static void vbcl_wayland_hlp_dcp_session_release(vbox_wl_dcp_session_t *pSession)
{
    void *pvData;

    if (!RTListIsEmpty(&pSession->clip.mimeTypesList.Node))
    {
        vbox_wl_dcp_mime_t *pEntry, *pNextEntry;

        RTListForEachSafe(&pSession->clip.mimeTypesList.Node, pEntry, pNextEntry, vbox_wl_dcp_mime_t, Node)
        {
            RTListNodeRemove(&pEntry->Node);
            RTStrFree(pEntry->pszMimeType);
            RTMemFree(pEntry);
        }
    }

    pvData = (void *)pSession->clip.pvClipboardBuf.reset();
    if (RT_VALID_PTR(pvData))
        RTMemFree(pvData);
}

/**
 * Initialize session.
 *
 * @param   pSession        Session data.
 */
static void vbcl_wayland_hlp_dcp_session_init(vbox_wl_dcp_session_t *pSession)
{
    RTListInit(&pSession->clip.mimeTypesList.Node);

    pSession->clip.fFmts.init(VBOX_SHCL_FMT_NONE, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
    pSession->clip.uFmt.init(VBOX_SHCL_FMT_NONE, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
    pSession->clip.pvClipboardBuf.init(0, VBCL_WAYLAND_DATA_WAIT_TIMEOUT_MS);
    pSession->clip.cbClipboardBuf.init(0, VBCL_WAYLAND_DATA_WAIT_TIMEOUT_MS);
}

/**
 * Reset previously initialized session.
 *
 * @param   pSession        Session data.
 */
static void vbcl_wayland_hlp_dcp_session_prepare(vbox_wl_dcp_session_t *pSession)
{
    vbcl_wayland_hlp_dcp_session_release(pSession);
    vbcl_wayland_hlp_dcp_session_init(pSession);
}

/**
 * Session callback: Generic session initializer.
 *
 * This callback starts new session.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type (unused).
 * @param   pvUser              User data (unused).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_session_start_generic_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    RT_NOREF(enmSessionType, pvUser);

    VBCL_LOG_CALLBACK;

    vbcl_wayland_hlp_dcp_session_prepare(&g_DcpCtx.Session);

    return VINF_SUCCESS;
}

/**
 * Wayland registry global handler.
 *
 * This callback is triggered when Wayland Registry listener is registered.
 * Wayland client library will trigger it individually for each available global
 * object.
 *
 * @param pvUser            Context data.
 * @param pRegistry         Wayland Registry object.
 * @param uName             Numeric name of the global object.
 * @param sIface            Name of interface implemented by the object.
 * @param uVersion          Interface version.
 */
static void vbcl_wayland_hlp_dcp_registry_global_handler(
    void *pvUser, struct wl_registry *pRegistry, uint32_t uName, const char *sIface, uint32_t uVersion)
{
    vbox_wl_dcp_ctx_t *pCtx = (vbox_wl_dcp_ctx_t *)pvUser;

    RT_NOREF(pRegistry);
    RT_NOREF(uVersion);

    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(sIface);

    /* Wrappers around 'if' statement. */
         VBCL_WAYLAND_REGISTRY_ADD_MATCH(pRegistry, sIface, uName, wl_seat_interface,                       pCtx->pSeat,                struct wl_seat *,                       VBCL_WAYLAND_SEAT_VERSION_MIN)
    else VBCL_WAYLAND_REGISTRY_ADD_MATCH(pRegistry, sIface, uName, zwlr_data_control_manager_v1_interface,  pCtx->pDataControlManager,  struct zwlr_data_control_manager_v1 *,  VBCL_WAYLAND_ZWLR_DATA_CONTROL_MANAGER_VERSION_MIN)
    else
        VBClLogVerbose(5, "ignoring Wayland interface %s\n", sIface);
}

/**
 * Wayland registry global remove handler.
 *
 * Triggered when global object is removed from Wayland registry.
 *
 * @param pvUser            Context data.
 * @param pRegistry         Wayland Registry object.
 * @param uName             Numeric name of the global object.
 */
static void vbcl_wayland_hlp_dcp_registry_global_remove_handler(
    void *pvUser, struct wl_registry *pRegistry, uint32_t uName)
{
    RT_NOREF(pvUser);
    RT_NOREF(pRegistry);
    RT_NOREF(uName);
}

/** Wayland global registry callbacks. */
static const struct wl_registry_listener g_vbcl_wayland_hlp_registry_cb =
{
    &vbcl_wayland_hlp_dcp_registry_global_handler,       /* .global */
    &vbcl_wayland_hlp_dcp_registry_global_remove_handler /* .global_remove */
};



/**********************************************************************************************************************************
 * Wayland Data Control Offer callbacks.
 *********************************************************************************************************************************/


/**
 * Session callback: Collect clipboard format advertised by guest.
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to access Wayland clipboard content.
 *
 * This callback adds mime-type just advertised by Wayland into a list
 * of mime-types which in turn later will be advertised to the host.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (Wayland mime-type).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_gh_add_fmt_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    const char *sMimeType = (const char *)pvUser;
    AssertPtrReturn(sMimeType, VERR_INVALID_PARAMETER);

    SHCLFORMAT uFmt = VBoxMimeConvGetIdByMime(sMimeType);

    int rc = (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
             ? VINF_SUCCESS : VERR_WRONG_ORDER;

    VBCL_LOG_CALLBACK;

    if (RT_SUCCESS(rc))
    {
        if (uFmt != VBOX_SHCL_FMT_NONE)
        {
            vbox_wl_dcp_mime_t *pNode = (vbox_wl_dcp_mime_t *)RTMemAllocZ(sizeof(vbox_wl_dcp_mime_t));
            if (RT_VALID_PTR(pNode))
            {
                pNode->pszMimeType = RTStrDup((char *)sMimeType);
                if (RT_VALID_PTR(pNode->pszMimeType))
                    RTListAppend(&g_DcpCtx.Session.clip.mimeTypesList.Node, &pNode->Node);
                else
                    RTMemFree(pNode);
            }

            if (   !RT_VALID_PTR(pNode)
                || !RT_VALID_PTR(pNode->pszMimeType))
            {
                rc = VERR_NO_MEMORY;
            }
        }
        else
            rc = VERR_NO_DATA;
    }

    return rc;
}


/**
 * Data Control Offer advertise callback.
 *
 * Triggered when other Wayland client advertises new clipboard content.
 *
 * @param pvUser            Context data.
 * @param pOffer            Wayland Data Control Offer object.
 * @param sMimeType         Mime-type of newly available clipboard data.
 */
static void vbcl_wayland_hlp_dcp_data_control_offer_offer(
    void *pvUser, struct zwlr_data_control_offer_v1 *pOffer, const char *sMimeType)
{
    vbox_wl_dcp_ctx_t *pCtx = (vbox_wl_dcp_ctx_t *)pvUser;
    int rc;

    RT_NOREF(pOffer);

    VBCL_LOG_CALLBACK;

    rc = vbcl_wayland_session_join(&pCtx->Session.Base,
                                   &vbcl_wayland_hlp_dcp_gh_add_fmt_cb,
                                   (void *)sMimeType);
    if (RT_FAILURE(rc))
        VBClLogError("cannot save formats announced by the guest, rc=%Rrc\n", rc);
}

/** Wayland Data Control Offer interface callbacks. */
static const struct zwlr_data_control_offer_v1_listener g_data_control_offer_listener =
{
    &vbcl_wayland_hlp_dcp_data_control_offer_offer,
};


/**********************************************************************************************************************************
 * Wayland Data Control Device callbacks.
 *********************************************************************************************************************************/


/**
 * Convert list of mime-types in string representation into bitmask of VBox formats.
 *
 * @returns Formats bitmask.
 * @param   pList   List of mime-types in string representation.
 */
static SHCLFORMATS vbcl_wayland_hlp_dcp_match_formats(vbox_wl_dcp_mime_t *pList)
{
    SHCLFORMATS fFmts = VBOX_SHCL_FMT_NONE;

    if (!RTListIsEmpty(&pList->Node))
    {
        vbox_wl_dcp_mime_t *pEntry;
        RTListForEach(&pList->Node, pEntry, vbox_wl_dcp_mime_t, Node)
        {
            AssertPtrReturn(pEntry, VBOX_SHCL_FMT_NONE);
            AssertPtrReturn(pEntry->pszMimeType, VBOX_SHCL_FMT_NONE);

            fFmts |= VBoxMimeConvGetIdByMime(pEntry->pszMimeType);
        }
    }

    return fFmts;
}

/**
 * Find first matching clipboard mime-type for given format ID.
 *
 * @returns Matching mime-type in string representation or NULL if not found.
 * @param   uFmt    Format in VBox representation to match.
 * @param   pList   List of Wayland mime-types in string representation.
 */
static char *vbcl_wayland_hlp_dcp_match_mime_type(SHCLFORMAT uFmt, vbox_wl_dcp_mime_t *pList)
{
    char *pszMimeType = NULL;

    if (!RTListIsEmpty(&pList->Node))
    {
        vbox_wl_dcp_mime_t *pEntry;
        RTListForEach(&pList->Node, pEntry, vbox_wl_dcp_mime_t, Node)
        {
            AssertPtrReturn(pEntry, NULL);
            AssertPtrReturn(pEntry->pszMimeType, NULL);

            if (uFmt == VBoxMimeConvGetIdByMime(pEntry->pszMimeType))
            {
                pszMimeType = pEntry->pszMimeType;
                break;
            }
        }
    }

    return pszMimeType;
}

/**
 * Read clipboard buffer from Wayland in specified format.
 *
 * @returns IPRT status code.
 * @param   pCtx            DCP context data.
 * @param   pOffer          Data offer object.
 * @param   uFmt            Clipboard format in VBox representation.
 * @param   pszMimeType     Requested mime-type in string representation.
 */
static int vbcl_wayland_hlp_dcp_receive_offer(
    vbox_wl_dcp_ctx_t *pCtx, zwlr_data_control_offer_v1 *pOffer, SHCLFORMAT uFmt, char *pszMimeType)
{
    int rc = VERR_PIPE_NOT_CONNECTED;

    int aFds[2];
    void *pvBuf = NULL;
    size_t cbBuf = 0;

    RT_NOREF(uFmt);

    if (pipe(aFds) == 0)
    {
        zwlr_data_control_offer_v1_receive(
            (struct zwlr_data_control_offer_v1 *)pOffer, pszMimeType, aFds[1]);

        close(aFds[1]);
        wl_display_flush(pCtx->pDisplay);

        rc = vbcl_wayland_hlp_dcp_read_wl_fd(aFds[0], &pvBuf, &cbBuf);
        if (RT_SUCCESS(rc))
        {
            void *pvBufOut = NULL;
            size_t cbBufOut = 0;

            rc = VBoxMimeConvNativeToVBox(pszMimeType, pvBuf, cbBuf, &pvBufOut, &cbBufOut);
            if (RT_SUCCESS(rc))
            {
                pCtx->Session.clip.pvClipboardBuf.set((uint64_t)pvBufOut);
                pCtx->Session.clip.cbClipboardBuf.set((uint64_t)cbBufOut);
            }

            RTMemFree(pvBuf);
        }
    }
    else
        VBClLogError("cannot read mime-type '%s' from Wayland, rc=%Rrc\n", pszMimeType, rc);

    return rc;
}

/**
 * Session callback: Advertise clipboard to the host.
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to access Wayland clipboard content.
 *
 * This callback (1) coverts Wayland clipboard formats into VBox
 * representation, (2) sets formats to the session, (3) waits for
 * host to request clipboard data in certain format, and (4)
 * receives Wayland clipboard in requested format.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (data offer object).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_gh_clip_report_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    struct zwlr_data_control_offer_v1 *pOffer = (struct zwlr_data_control_offer_v1 *)pvUser;
    SHCLFORMATS fFmts = VBOX_SHCL_FMT_NONE;

    int rc = (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
             ? VINF_SUCCESS : VERR_WRONG_ORDER;

    AssertPtrReturn(pOffer, VERR_INVALID_PARAMETER);

    VBCL_LOG_CALLBACK;

    if (RT_SUCCESS(rc))
    {
        fFmts = vbcl_wayland_hlp_dcp_match_formats(&g_DcpCtx.Session.clip.mimeTypesList);
        if (fFmts != VBOX_SHCL_FMT_NONE)
        {
            SHCLFORMAT uFmt;

            g_DcpCtx.Session.clip.fFmts.set(fFmts);

            if (RT_VALID_PTR(g_DcpCtx.pClipboardCtx))
            {
                rc = VbglR3ClipboardReportFormats(g_DcpCtx.pClipboardCtx->idClient, fFmts);
                if (RT_SUCCESS(rc))
                {
                    uFmt = g_DcpCtx.Session.clip.uFmt.wait();
                    if (uFmt != g_DcpCtx.Session.clip.uFmt.defaults())
                    {
                        char *pszMimeType =
                            vbcl_wayland_hlp_dcp_match_mime_type(uFmt, &g_DcpCtx.Session.clip.mimeTypesList);

                        if (RT_VALID_PTR(pszMimeType))
                        {
                            rc = vbcl_wayland_hlp_dcp_receive_offer(&g_DcpCtx, pOffer, uFmt, pszMimeType);

                            VBClLogVerbose(5, "will send fmt=0x%x (%s) to the host\n", uFmt, pszMimeType);
                        }
                        else
                            rc = VERR_NO_DATA;
                    }
                    else
                        rc = VERR_TIMEOUT;
                }
                else
                    VBClLogError("cannot report formats to host, rc=%Rrc\n", rc);
            }
            else
            {
                VBClLogVerbose(2, "cannot announce to guest, no host service connection yet\n");
                rc = VERR_TRY_AGAIN;
            }
        }
        else
            rc = VERR_NO_DATA;

        zwlr_data_control_offer_v1_destroy((struct zwlr_data_control_offer_v1 *)pOffer);

        VBClLogVerbose(5, "announcing fFmts=0x%x to host, rc=%Rrc\n", fFmts, rc);
    }

    return rc;
}

/**
 * Data Control Device offer callback.
 *
 * Triggered when other Wayland client advertises new clipboard content.
 * When this callback is triggered, a new zwlr_data_control_offer_v1 object
 * is created. This callback should setup listener callbacks for this object.
 *
 * @param pvUser            Context data.
 * @param pDevice           Wayland Data Control Device object.
 * @param pOffer            Wayland Data Control Offer object.
 */
static void vbcl_wayland_hlp_dcp_data_device_data_offer(
    void *pvUser, struct zwlr_data_control_device_v1 *pDevice, struct zwlr_data_control_offer_v1 *pOffer)
{
    vbox_wl_dcp_ctx_t *pCtx = (vbox_wl_dcp_ctx_t *)pvUser;
    int rc;

    RT_NOREF(pDevice);

    VBCL_LOG_CALLBACK;

    if (pCtx->fIngnoreWlClipIn)
    {
        VBClLogVerbose(5, "ignoring Wayland clipboard data offer, we advertising new clipboard ourselves\n");
        return;
    }

    rc = vbcl_wayland_session_end(&pCtx->Session.Base, NULL, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = vbcl_wayland_session_start(&pCtx->Session.Base,
                                        VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST,
                                        &vbcl_wayland_hlp_dcp_session_start_generic_cb,
                                        &pCtx->Session);
        if (RT_SUCCESS(rc))
        {
            zwlr_data_control_offer_v1_add_listener(pOffer, &g_data_control_offer_listener, pvUser);

            /* Receive all the advertised mime types. */
            wl_display_roundtrip(pCtx->pDisplay);

            /* Try to send an announcement to the host. */
            rc = vbcl_wayland_session_join(&pCtx->Session.Base,
                                           &vbcl_wayland_hlp_dcp_gh_clip_report_cb,
                                           pOffer);
        }
        else
            VBClLogError("unable to start session, rc=%Rrc\n", rc);
    }
    else
        VBClLogError("unable to start session, previous session is still running, rc=%Rrc\n", rc);
}

/**
 * Data Control Device selection callback.
 *
 * Triggered when Wayland client advertises new clipboard content.
 * In this callback, actual clipboard data is received from Wayland client.
 *
 * @param pvUser            Context data.
 * @param pDevice           Wayland Data Control Device object.
 * @param pOffer            Wayland Data Control Offer object.
 */
static void vbcl_wayland_hlp_dcp_data_device_selection(
    void *pvUser, struct zwlr_data_control_device_v1 *pDevice, struct zwlr_data_control_offer_v1 *pOffer)
{
    RT_NOREF(pDevice, pvUser, pOffer);

    VBCL_LOG_CALLBACK;
}

/**
 * Data Control Device finished callback.
 *
 * Triggered when Data Control Device object is no longer valid and
 * needs to be destroyed.
 *
 * @param pvUser            Context data.
 * @param pDevice           Wayland Data Control Device object.
 */
static void vbcl_wayland_hlp_dcp_data_device_finished(
    void *pvUser, struct zwlr_data_control_device_v1 *pDevice)
{
    RT_NOREF(pvUser);

    VBCL_LOG_CALLBACK;

    zwlr_data_control_device_v1_destroy(pDevice);
}

/**
 * Data Control Device primary selection callback.
 *
 * Same as shcl_wl_data_control_device_selection, but triggered for
 * primary selection case.
 *
 * @param pvUser            Context data.
 * @param pDevice           Wayland Data Control Device object.
 * @param pOffer            Wayland Data Control Offer object.
 */
static void vbcl_wayland_hlp_dcp_data_device_primary_selection(
    void *pvUser, struct zwlr_data_control_device_v1 *pDevice, struct zwlr_data_control_offer_v1 *pOffer)
{
    RT_NOREF(pDevice, pvUser, pOffer);

    VBCL_LOG_CALLBACK;
}


/** Data Control Device interface callbacks. */
static const struct zwlr_data_control_device_v1_listener g_data_device_listener =
{
    &vbcl_wayland_hlp_dcp_data_device_data_offer,
    &vbcl_wayland_hlp_dcp_data_device_selection,
    &vbcl_wayland_hlp_dcp_data_device_finished,
    &vbcl_wayland_hlp_dcp_data_device_primary_selection,
};


/**********************************************************************************************************************************
 * Wayland Data Control Source callbacks.
 *********************************************************************************************************************************/


/**
 * Wayland data send callback.
 *
 * Triggered when other Wayland client wants to read clipboard
 * data from us.
 *
 * @param pvUser            VBox private data.
 * @param pDataSource       Wayland Data Control Source object.
 * @param sMimeType         A mime-type of requested data.
 * @param fd                A file descriptor to write clipboard content into.
 */
static void vbcl_wayland_hlp_dcp_data_source_send(
    void *pvUser, struct zwlr_data_control_source_v1 *pDataSource,
    const char *sMimeType, int32_t fd)
{
    vbox_wl_dcp_ctx_t *pCtx = (vbox_wl_dcp_ctx_t *)pvUser;
    int rc;

    struct vbcl_wl_dcp_write_ctx priv;

    RT_NOREF(pDataSource);

    VBCL_LOG_CALLBACK;

    RT_ZERO(priv);

    priv.sMimeType = sMimeType;
    priv.fd = fd;

    rc = vbcl_wayland_session_join(&pCtx->Session.Base,
                                   &vbcl_wayland_hlp_dcp_hg_clip_report_join3_cb,
                                   &priv);

    VBClLogVerbose(5, "vbcl_wayland_hlp_dcp_data_source_send, rc=%Rrc\n", rc);
    close(fd);
}

/**
 * Wayland data canceled callback.
 *
 * Triggered when data source was replaced by another data source
 * and no longer valid.
 *
 * @param pvData            VBox private data.
 * @param pDataSource       Wayland Data Control Source object.
 */
static void vbcl_wayland_hlp_dcp_data_source_cancelled(
    void *pvData, struct zwlr_data_control_source_v1 *pDataSource)
{
    RT_NOREF(pvData);

    VBCL_LOG_CALLBACK;

    zwlr_data_control_source_v1_destroy(pDataSource);
}


/** Wayland Data Control Source interface callbacks. */
static const struct zwlr_data_control_source_v1_listener g_data_source_listener =
{
    &vbcl_wayland_hlp_dcp_data_source_send,
    &vbcl_wayland_hlp_dcp_data_source_cancelled,
};


/**********************************************************************************************************************************
 * Helper specific code and session callbacks.
 *********************************************************************************************************************************/


/**
 * Setup or reset helper context.
 *
 * This function is used on helper init and termination. In case of
 * init, memory is not initialized yet, so it needs to be zeroed.
 * In case of shutdown, memory is already initialized and previously
 * allocated resources must be freed.
 *
 * @param pCtx              Context data.
 * @param fShutdown         A flag to indicate if session resources
 *                          need to be deallocated.
 */
static void vbcl_wayland_hlp_dcp_reset_ctx(vbox_wl_dcp_ctx_t *pCtx, bool fShutdown)
{
    pCtx->Thread = NIL_RTTHREAD;
    pCtx->fShutdown = false;
    pCtx->fIngnoreWlClipIn = false;
    pCtx->fSendToGuest.init(false, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
    pCtx->pClipboardCtx = NULL;
    pCtx->pDisplay = NULL;
    pCtx->pRegistry = NULL;
    pCtx->pSeat = NULL;
    pCtx->pDataDevice = NULL;
    pCtx->pDataControlManager = NULL;

    if (fShutdown)
        vbcl_wayland_hlp_dcp_session_release(&pCtx->Session);

    vbcl_wayland_hlp_dcp_session_init(&pCtx->Session);
}

/**
 * Disconnect from Wayland compositor.
 *
 * Close connection, release resources and reset context data.
 *
 * @param pCtx              Context data.
 */
static void vbcl_wayland_hlp_dcp_disconnect(vbox_wl_dcp_ctx_t *pCtx)
{
    if (RT_VALID_PTR(pCtx->pDataControlManager))
        zwlr_data_control_manager_v1_destroy(pCtx->pDataControlManager);

    if (RT_VALID_PTR(pCtx->pDataDevice))
        zwlr_data_control_device_v1_destroy(pCtx->pDataDevice);

    if (RT_VALID_PTR(pCtx->pSeat))
        wl_seat_destroy(pCtx->pSeat);

    if (RT_VALID_PTR(pCtx->pRegistry))
        wl_registry_destroy(pCtx->pRegistry);

    if (RT_VALID_PTR(pCtx->pDisplay))
        wl_display_disconnect(pCtx->pDisplay);

    vbcl_wayland_hlp_dcp_reset_ctx(pCtx, true);
}

/**
 * Connect to Wayland compositor.
 *
 * Establish connection, bind to all required interfaces.
 *
 * @returns TRUE on success, FALSE otherwise.
 * @param   pCtx                Context data.
 */
static bool vbcl_wayland_hlp_dcp_connect(vbox_wl_dcp_ctx_t *pCtx)
{
    const char *csWaylandDisplay = RTEnvGet(VBCL_ENV_WAYLAND_DISPLAY);
    bool fConnected = false;

    if (RT_VALID_PTR(csWaylandDisplay))
        pCtx->pDisplay = wl_display_connect(csWaylandDisplay);
    else
        VBClLogError("cannot connect to Wayland compositor "
                     VBCL_ENV_WAYLAND_DISPLAY " environment variable not set\n");

    if (RT_VALID_PTR(pCtx->pDisplay))
    {
        pCtx->pRegistry = wl_display_get_registry(pCtx->pDisplay);
        if (RT_VALID_PTR(pCtx->pRegistry))
        {
            wl_registry_add_listener(pCtx->pRegistry, &g_vbcl_wayland_hlp_registry_cb, (void *)pCtx);
            wl_display_roundtrip(pCtx->pDisplay);

            if (RT_VALID_PTR(pCtx->pDataControlManager))
            {
                if (RT_VALID_PTR(pCtx->pSeat))
                {
                    pCtx->pDataDevice = zwlr_data_control_manager_v1_get_data_device(pCtx->pDataControlManager, pCtx->pSeat);
                    if (RT_VALID_PTR(pCtx->pDataDevice))
                    {
                        if (RT_VALID_PTR(pCtx->pDataControlManager))
                            fConnected = true;
                        else
                            VBClLogError("cannot get Wayland data control manager interface\n");
                    }
                    else
                        VBClLogError("cannot get Wayland data device interface\n");
                }
                else
                    VBClLogError("cannot get Wayland seat interface\n");
            }
            else
                VBClLogError("cannot get Wayland device manager interface\n");
        }
        else
            VBClLogError("cannot connect to Wayland registry\n");
    }
    else
        VBClLogError("cannot connect to Wayland compositor\n");

    if (!fConnected)
        vbcl_wayland_hlp_dcp_disconnect(pCtx);

    return fConnected;
}


/**
 * Main loop for Wayland compositor events.
 *
 * All requests to Wayland compositor must be performed in context
 * of this thread.
 *
 * @returns IPRT status code.
 * @param   hThreadSelf         IPRT thread object.
 * @param   pvUser              Context data.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_event_loop(RTTHREAD hThreadSelf, void *pvUser)
{
    vbox_wl_dcp_ctx_t *pCtx = (vbox_wl_dcp_ctx_t *)pvUser;
    int rc = VERR_TRY_AGAIN;

    if (vbcl_wayland_hlp_dcp_connect(pCtx))
    {
        /* Start listening Data Control Device interface. */
        if (zwlr_data_control_device_v1_add_listener(pCtx->pDataDevice, &g_data_device_listener, (void *)pCtx) == 0)
        {
            /* Tell parent thread we are ready. */
            RTThreadUserSignal(hThreadSelf);

            while (1)
            {
                rc = vbcl_wayland_hlp_dcp_next_event(pCtx);
                if (   rc != VERR_TIMEOUT
                    && RT_FAILURE(rc))
                {
                    VBClLogError("cannot read event from Wayland, rc=%Rrc\n", rc);
                }

                if (pCtx->fSendToGuest.reset())
                {
                    rc = vbcl_wayland_session_join(&pCtx->Session.Base,
                                                   &vbcl_wayland_hlp_dcp_hg_clip_report_join2_cb,
                                                   NULL);
                }

                /* Handle graceful thread termination. */
                if (pCtx->fShutdown)
                {
                    rc = VINF_SUCCESS;
                    break;
                }
            }
        }
        else
        {
            rc = VERR_NOT_SUPPORTED;
            VBClLogError("cannot subscribe to Data Control Device events\n");
        }

        vbcl_wayland_hlp_dcp_disconnect(pCtx);
    }

    /* Notify parent thread if we failed to start, so it won't be
     * waiting 30 sec to figure this out. */
    if (RT_FAILURE(rc))
        RTThreadUserSignal(hThreadSelf);

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnProbe}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_probe(void)
{
    vbox_wl_dcp_ctx_t probeCtx;
    int fCaps = VBOX_WAYLAND_HELPER_CAP_NONE;
    VBGHDISPLAYSERVERTYPE enmDisplayServerType = VBGHDisplayServerTypeDetect();

    vbcl_wayland_hlp_dcp_reset_ctx(&probeCtx, false /* fShutdown */);
    vbcl_wayland_session_init(&probeCtx.Session.Base);

    if (VBGHDisplayServerTypeIsWaylandAvailable(enmDisplayServerType))
    {
        if (vbcl_wayland_hlp_dcp_connect(&probeCtx))
        {
            fCaps |= VBOX_WAYLAND_HELPER_CAP_CLIPBOARD;
            vbcl_wayland_hlp_dcp_disconnect(&probeCtx);
        }
    }

    return fCaps;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnInit}
 */
RTDECL(int) vbcl_wayland_hlp_dcp_init(void)
{
    vbcl_wayland_hlp_dcp_reset_ctx(&g_DcpCtx, false /* fShutdown */);
    vbcl_wayland_session_init(&g_DcpCtx.Session.Base);

    return VBClClipboardThreadStart(&g_DcpCtx.Thread, vbcl_wayland_hlp_dcp_event_loop, "wl-dcp", &g_DcpCtx);
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnTerm}
 */
RTDECL(int) vbcl_wayland_hlp_dcp_term(void)
{
    int rc;
    int rcThread = 0;

    /* Set termination flag. Wayland event loop should pick it up
     * on the next iteration. */
    g_DcpCtx.fShutdown = true;

    /* Wait for Wayland event loop thread to shutdown. */
    rc = RTThreadWait(g_DcpCtx.Thread, RT_MS_30SEC, &rcThread);
    if (RT_SUCCESS(rc))
        VBClLogInfo("Wayland event thread exited with status, rc=%Rrc\n", rcThread);
    else
        VBClLogError("unable to stop Wayland event thread, rc=%Rrc\n", rc);

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnSetClipboardCtx}
 */
static DECLCALLBACK(void) vbcl_wayland_hlp_dcp_set_clipboard_ctx(PVBGLR3SHCLCMDCTX pCtx)
{
    g_DcpCtx.pClipboardCtx = pCtx;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnPopup}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_popup(void)
{
    return VINF_SUCCESS;
}

/**
 * Session callback: Copy clipboard to the guest.
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to inject clipboard content into Wayland. It is
 * triggered when Wayland client already decided data in which format
 * it wants to request.
 *
 * This callback (1) sets requested clipboard format to the session,
 * (2) waits for clipboard data to be copied from the host, (3) converts
 * host clipboard data into guest representation, and (4) sends clipboard
 * to the guest by writing given file descriptor.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (Wayland I/O context).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_hg_clip_report_join3_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    struct vbcl_wl_dcp_write_ctx *pPriv = (struct vbcl_wl_dcp_write_ctx *)pvUser;
    AssertPtrReturn(pPriv, VERR_INVALID_PARAMETER);

    void *pvBuf;
    uint32_t cbBuf;

    int rc = (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
             ? VINF_SUCCESS : VERR_WRONG_ORDER;

    VBCL_LOG_CALLBACK;

    if (RT_SUCCESS(rc))
    {
        if (RT_VALID_PTR(g_DcpCtx.pClipboardCtx))
        {
            /* Set requested format to the session. */
            g_DcpCtx.Session.clip.uFmt.set(VBoxMimeConvGetIdByMime(pPriv->sMimeType));

            /* Wait for data in requested format. */
            pvBuf = (void *)g_DcpCtx.Session.clip.pvClipboardBuf.wait();
            cbBuf = g_DcpCtx.Session.clip.cbClipboardBuf.wait();
            if (   cbBuf != g_DcpCtx.Session.clip.cbClipboardBuf.defaults()
                && pvBuf != (void *)g_DcpCtx.Session.clip.pvClipboardBuf.defaults())
            {
                void *pvBufOut;
                size_t cbOut;

                /* Convert clipboard data from VBox representation into guest format. */
                rc = VBoxMimeConvVBoxToNative(pPriv->sMimeType, pvBuf, cbBuf, &pvBufOut, &cbOut);
                if (RT_SUCCESS(rc))
                {
                    rc = vbcl_wayland_hlp_dcp_write_wl_fd(pPriv->fd, pvBufOut, cbOut);
                    RTMemFree(pvBufOut);
                }
                else
                    VBClLogError("cannot convert '%s' to native format, rc=%Rrc\n", pPriv->sMimeType, rc);
            }
            else
                rc = VERR_TIMEOUT;
        }
        else
        {
            VBClLogVerbose(2, "cannot send to guest, no host service connection yet\n");
            rc = VERR_TRY_AGAIN;
        }

        g_DcpCtx.fIngnoreWlClipIn = false;
    }

    return rc;
}

/**
 * Enumeration callback used for sending clipboard offers to Wayland client.
 *
 * When host announces its clipboard content, this call back is used in order
 * to send corresponding offers to other Wayland clients.
 *
 * Callback must be executed in context of Wayland event thread.
 *
 * @param   pcszMimeType    Mime-type to advertise.
 * @param   pvUser          User data (DCP data source object).
 */
static DECLCALLBACK(void) vbcl_wayland_hlp_dcp_send_offers(const char *pcszMimeType, void *pvUser)
{
    zwlr_data_control_source_v1 *pDataSource = (zwlr_data_control_source_v1 *)pvUser;
    zwlr_data_control_source_v1_offer(pDataSource, pcszMimeType);
}

/**
 * Session callback: Advertise clipboard to the guest.
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to inject clipboard content into Wayland.
 *
 * This callback (1) prevents Wayland event loop from processing
 * incoming clipboard advertisements before sending any data to
 * other Wayland clients (this is needed in order to avoid feedback
 * loop from our own advertisements), (2) waits for the list of clipboard
 * formats available on the host side (set by vbcl_wayland_hlp_dcp_hg_clip_report_join_cb),
 * and (3) sends data offers for available host clipboard to other clients.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (unused).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_hg_clip_report_join2_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    int rc = (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
             ? VINF_SUCCESS : VERR_WRONG_ORDER;

    RT_NOREF(pvUser);

    VBCL_LOG_CALLBACK;

    if (RT_SUCCESS(rc))
    {
        g_DcpCtx.fIngnoreWlClipIn = true;

        SHCLFORMATS fFmts = g_DcpCtx.Session.clip.fFmts.wait();
        if (fFmts != g_DcpCtx.Session.clip.fFmts.defaults())
        {
            zwlr_data_control_source_v1 *pDataSource =
            zwlr_data_control_manager_v1_create_data_source(g_DcpCtx.pDataControlManager);

            if (RT_VALID_PTR(pDataSource))
            {
                zwlr_data_control_source_v1_add_listener(
                    (struct zwlr_data_control_source_v1 *)pDataSource, &g_data_source_listener, &g_DcpCtx);

                VBoxMimeConvEnumerateMimeById(fFmts,
                                              vbcl_wayland_hlp_dcp_send_offers,
                                              pDataSource);

                zwlr_data_control_device_v1_set_selection(g_DcpCtx.pDataDevice, pDataSource);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_NO_DATA;
    }

    return rc;
}

/**
 * Session callback: Copy clipboard from the host.
 *
 * This callback (1) sets host clipboard formats list to the session,
 * (2) asks Wayland event thread to advertise these formats to the guest,
 * (3) waits for guest to request clipboard in specific format, (4) read
 * host clipboard in this format, and (5) sets clipboard data to the session,
 * so Wayland events thread can inject it into the guest.
 *
 * This callback should not return until clipboard data is read from
 * the host or error occurred. It must block host events loop until
 * current host event is fully processed.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (host clipboard formats).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_hg_clip_report_join_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    SHCLFORMATS *pfFmts = (SHCLFORMATS *)pvUser;
    AssertPtrReturn(pfFmts, VERR_INVALID_PARAMETER);

    int rc = (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
             ? VINF_SUCCESS : VERR_WRONG_ORDER;

    VBCL_LOG_CALLBACK;

    if (RT_SUCCESS(rc))
    {
        SHCLFORMAT uFmt;
        void *pvData;
        uint32_t cbData;

        /* Set list of host clipboard formats to the session. */
        g_DcpCtx.Session.clip.fFmts.set(*pfFmts);

        /* Ask Wayland event thread to advertise formats to the guest. */
        g_DcpCtx.fSendToGuest.set(true);
        RTThreadPoke(g_DcpCtx.Thread);

        /* Wait for the guest to request certain clipboard format. */
        uFmt = g_DcpCtx.Session.clip.uFmt.wait();
        if (uFmt != g_DcpCtx.Session.clip.uFmt.defaults())
        {
            /* Read host clipboard in specified format. */
            rc = VBClClipboardReadHostClipboard(g_DcpCtx.pClipboardCtx, uFmt, &pvData, &cbData);
            if (RT_SUCCESS(rc))
            {
                /* Set clipboard data to the session. */
                g_DcpCtx.Session.clip.pvClipboardBuf.set((uint64_t)pvData);
                g_DcpCtx.Session.clip.cbClipboardBuf.set((uint64_t)cbData);
            }
        }
        else
            rc = VERR_TIMEOUT;

    }

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnHGClipReport}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_hg_clip_report(SHCLFORMATS fFormats)
{
    int rc = VERR_NO_DATA;

    VBCL_LOG_CALLBACK;

    if (fFormats != VBOX_SHCL_FMT_NONE)
    {
        rc = vbcl_wayland_session_end(&g_DcpCtx.Session.Base, NULL, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = vbcl_wayland_session_start(&g_DcpCtx.Session.Base,
                                            VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                            &vbcl_wayland_hlp_dcp_session_start_generic_cb,
                                            NULL);

            if (RT_SUCCESS(rc))
                rc = vbcl_wayland_session_join(&g_DcpCtx.Session.Base,
                                               vbcl_wayland_hlp_dcp_hg_clip_report_join_cb,
                                               &fFormats);
        }
        else
            VBClLogError("unable to start session, previous session is still running rc=%Rrc\n", rc);
    }

    return rc;
}

/**
 * Session callback: Copy clipboard to the host.
 *
 * This callback sets clipboard format to the session as requested
 * by host, waits for guest clipboard data in requested format and
 * sends data to the host.
 *
 * This callback should not return until clipboard data is sent to
 * the host or error occurred. It must block host events loop until
 * current host event is fully processed.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (requested format).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_gh_clip_read_join_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    SHCLFORMAT *puFmt = (SHCLFORMAT *)pvUser;
    AssertPtrReturn(puFmt, VERR_INVALID_PARAMETER);

    int rc = (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
             ? VINF_SUCCESS : VERR_WRONG_ORDER;

    VBCL_LOG_CALLBACK;

    if (RT_SUCCESS(rc))
    {
        void *pvData;
        size_t cbData;

        /* Store requested clipboard format to the session. */
        g_DcpCtx.Session.clip.uFmt.set(*puFmt);

        /* Wait for data in requested format. */
        pvData = (void *)g_DcpCtx.Session.clip.pvClipboardBuf.wait();
        cbData = g_DcpCtx.Session.clip.cbClipboardBuf.wait();
        if (   cbData != g_DcpCtx.Session.clip.cbClipboardBuf.defaults()
            && pvData != (void *)g_DcpCtx.Session.clip.pvClipboardBuf.defaults())
        {
            /* Send clipboard data to the host. */
            rc = VbglR3ClipboardWriteDataEx(g_DcpCtx.pClipboardCtx, *puFmt, pvData, cbData);
        }
        else
            rc = VERR_TIMEOUT;
    }

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnGHClipRead}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_gh_clip_read(SHCLFORMAT uFmt)
{
    int rc;

    VBCL_LOG_CALLBACK;

    rc = vbcl_wayland_session_join(&g_DcpCtx.Session.Base,
                                   &vbcl_wayland_hlp_dcp_gh_clip_read_join_cb,
                                   &uFmt);
    return rc;
}

/* Helper callbacks. */
const VBCLWAYLANDHELPER g_WaylandHelperDcp =
{
    "wayland-dcp",                              /* .pszName */
    vbcl_wayland_hlp_dcp_probe,                 /* .pfnProbe */
    vbcl_wayland_hlp_dcp_init,                  /* .pfnInit */
    vbcl_wayland_hlp_dcp_term,                  /* .pfnTerm */
    vbcl_wayland_hlp_dcp_set_clipboard_ctx,     /* .pfnSetClipboardCtx */
    vbcl_wayland_hlp_dcp_popup,                 /* .pfnPopup */
    vbcl_wayland_hlp_dcp_hg_clip_report,        /* .pfnHGClipReport */
    vbcl_wayland_hlp_dcp_gh_clip_read,          /* .pfnGHClipRead */
};
