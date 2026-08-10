/* $Id: Wayland.h 114738 2026-07-21 13:40:26Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest / Host common code - Wayland.
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

#ifndef VBOX_INCLUDED_GuestHost_Wayland_h
#define VBOX_INCLUDED_GuestHost_Wayland_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <iprt/types.h>
#include <iprt/list.h>


/* Forward declaration of Wayland structures: */
struct wl_display;
struct wl_registry;
struct wl_seat;

/* Forward declaration of our own structures: */
struct VBGHWAYLANDCORE;
/** Pointer to the core guest-host Wayland instance data. */
typedef struct VBGHWAYLANDCORE *PVBGHWAYLANDCORE;


/**
 * Register enumeration callback for use with VBClWaylandConnect.
 *
 * @param   pCore           The core VBox guest-host instance data.
 *                          (This is typically downcasted to a callback specific
 *                          structure pointer.)
 * @param   pRegistry       Registry entry handle.
 * @param   uObjName        The global object ordinal.
 * @param   pszIfaceName    The interface exposed by the object.
 * @param   uIfaceVersion   The interface version.
 * @param   pErrInfo        Error info. Optional.
 *
 * @note    Callbacks only happens during the VbghWaylandConnect() call for now.
 */
typedef DECLCALLBACKTYPE(void, FNVBGHWAYLANDREGENUM, (PVBGHWAYLANDCORE pCore, struct wl_registry *pRegistry,
                                                      uint32_t uObjName, const char *pszIfaceName, uint32_t uIfaceVersion,
                                                      PRTERRINFO pErrInfo));
/** Pointer to a FNVBGHWAYLANDREGENUM. */
typedef FNVBGHWAYLANDREGENUM *PFNVBGHWAYLANDREGENUM;

/** A Wayland seat. */
typedef struct VBGHWAYLANDSEAT
{
    /** List entry. */
    RTLISTNODE                                  ListEntry;
    /** Wayland Seat object. */
    struct wl_seat                             *pSeat;
    /** Capabilities (WL_SEAT_CAPABILITY_XXX). */
    uint32_t                                    fCaps;
    /** The seat name (or NULL). */
    char                                       *pszName;
} VBGHWAYLANDSEAT;
/** Pointer to a Wayland seat list entry. */
typedef VBGHWAYLANDSEAT *PVBGHWAYLANDSEAT;

/**
 * Core guest-host Wayland instance data.
 */
typedef struct VBGHWAYLANDCORE
{
    /** Wayland compositor connection object. */
    struct wl_display                          *pDisplay;
    /** Wayland registry object. */
    struct wl_registry                         *pRegistry;
    /** List of seats (VBGHWAYLANDSEAT). */
    RTLISTANCHOR                                SeatList;

    /** Private init time stuff.
     * @internal  */
    struct
    {
        PFNVBGHWAYLANDREGENUM                   pfnRegEnum;
        PRTERRINFO                              pErrInfo;
    } Int;
} VBGHWAYLANDCORE;

/**
 * A helper for the registry callback for binding and returning if the current
 * entry matches.
 */
#define VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(a_pRegistry, a_uObjName, a_pszIfaceName, a_uIfaceVersion, a_pErrInfo, \
                                                     a_IfaceToBindTo, a_CtxMemberType, a_CtxMember, a_uMinVersion) do { \
        if (RTStrCmp((a_pszIfaceName), (a_IfaceToBindTo).name) != 0) \
            break; \
        if (!(a_CtxMember)) \
        { \
            a_CtxMember = (a_CtxMemberType)wl_registry_bind((a_pRegistry), (a_uObjName), &(a_IfaceToBindTo), (a_uMinVersion)); \
            LogRel4(("%s: binding to Wayland object %u interface '%s' v%u(/%u/%u) -> %p\n", __func__, (a_uObjName), \
                     (a_IfaceToBindTo).name, wl_proxy_get_version((struct wl_proxy *)(a_CtxMember)), \
                     (a_uIfaceVersion), (a_uMinVersion), (a_CtxMember))); \
        } \
        AssertPtr(a_CtxMember); \
        return; \
    } while (0)

/**
 * Helper for cleaning up after VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET.
 */
#define VBGH_WAYLAND_DESTROY_PROXY(a_pMember, a_IfName) \
    if (a_pMember) \
    { \
        a_IfName##_destroy(a_pMember); \
        a_pMember = NULL; \
    } else do {} while (0)

/** Environment variable which points to which Wayland compositor we should connect.
 * Must always be checked. */
#define VBGH_WAYLAND_DISPLAY_ENV_VAR    "WAYLAND_DISPLAY"


RT_C_DECLS_BEGIN

VBGH_DECL(int)              VbghWaylandConnect(PVBGHWAYLANDCORE pThis, PFNVBGHWAYLANDREGENUM pfnRegEnum, PRTERRINFO pErrInfo);
VBGH_DECL(void)             VbghWaylandDisconnect(PVBGHWAYLANDCORE pThis);
VBGH_DECL(PVBGHWAYLANDSEAT) VbghWaylandGetBestSeatEntry(PVBGHWAYLANDCORE pThis);
VBGH_DECL(int)              VbghWaylandReadFdToBuffer(int fd, RTMSINTERVAL cMsTimeout, void **ppvBuf, size_t *pcbBuf);
VBGH_DECL(void)             VbghWaylandReadFdToBufferFree(void *pvBuf);
VBGH_DECL(int)              VbghWaylandWriteBufferToFd(void const *pvBuf, size_t cbBuf, int fdDst, RTMSINTERVAL cMsTimeout);

/**
 * Callback for servicing the VbghWaylandRunloopForDisplay wakeup pipe.
 *
 * @returns VBox status code.  Any failure status leads to the pipe being
 *          excluded from the poll.
 * @param   hPipe       The wakeup pipe (read).
 * @param   pfReturn    The return indicator, if given.
 * @param   pvUser      User argument.
 */
typedef DECLCALLBACKTYPE(int, FNVBGHWAYLANDRLWAKEUPPIPE,(RTPIPE hPipe, bool volatile *pfReturn, void *pvUser));
/** Pointer to a FNVBGHWAYLANDRLWAKEUPPIPE function. */
typedef FNVBGHWAYLANDRLWAKEUPPIPE *PFNVBGHWAYLANDRLWAKEUPPIPE;

VBGH_DECL(int)              VbghWaylandRunloopForDisplay(struct wl_display *pDisplay, RTPIPE hPipeWakeup,
                                                         PFNVBGHWAYLANDRLWAKEUPPIPE pfnWakeup, void *pvWakeupUser,
                                                         RTPIPE hPipeMonClose, RTMSINTERVAL cMsPollInterval,
                                                         bool volatile *pfReturn);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_GuestHost_Wayland_h */
