/* $Id: WaylandPopup.h 114738 2026-07-21 13:40:26Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest / Host common code - Wayland Popup (for focus grabbing).
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

#ifndef VBOX_INCLUDED_GuestHost_WaylandPopup_h
#define VBOX_INCLUDED_GuestHost_WaylandPopup_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <iprt/types.h>

/* Forward declaration of Wayland structures: */
struct wl_compositor;
struct wl_buffer;
struct wl_keyboard;
struct wl_registry;
struct wl_shm;
struct wl_shm_pool;
struct wl_shell;
struct wl_shell_surface;
struct xdg_wm_base;
struct xdg_activation_v1;
struct xdg_surface;
struct xdg_toplevel;
struct gtk_shell1;
struct gtk_surface1;

/* Forward declaration of our own structures: */
struct VBGHWAYLANDCORE;
struct VBGHWAYLANDSEAT;
struct VBGHWAYLANDPOPUP;


/**
 * Notification callback for when the application get input focus.
 *
 * @param   pWlPopup    Pointer to the VBGHWAYLANDPOPUP structure.
 *                      Use RT_FROM_MEMBER to find your structure from it.
 * @param   uSerial     The focus event's serial number. Used as a password in
 *                      actions requiring focus.
 * @param   u64User     64-bit user value.
 */
typedef DECLCALLBACKTYPE(void, FNVBGHWAYLANDPOPUPONFOCUS,(struct VBGHWAYLANDPOPUP *pWlPopup, uint32_t uSerial, uint64_t u64User));
/** Pointer to a FNVBGHWAYLANDPOPUPONFOCUS function. */
typedef FNVBGHWAYLANDPOPUPONFOCUS *PFNVBGHWAYLANDPOPUPONFOCUS;

/**
 * Wayland popup surface state data.
 *
 * This is used for grabbing focus so we can get access the clipboard on Wayland
 * setups that doesn't implement any of the data control manager interfaces.
 */
typedef struct VBGHWAYLANDPOPUP
{
    /** @name Basic wayland stuff.
     * @{ */
    struct wl_compositor           *pWlCompositor;
    struct wl_shm                  *pWlShm;
    /** @} */

    /** @name Shell & WM related stuff.
     * @{ */
    struct wl_shell                *pWlShell;
    struct xdg_wm_base             *pXdgWmBase;
    struct xdg_activation_v1       *pXdgActivation;
    struct gtk_shell1              *pGtkShell1;
    /** @} */

    /** Pointer to the core Wayland guest host data. */
    struct VBGHWAYLANDCORE         *pGhCore;

    /** Pointer to the wayland keyboard object binding. */
    struct wl_keyboard             *pKeyboard;

    /** The surface. */
    struct wl_surface              *pSurface;
    /** The shell surface, if using pWlShell. */
    struct wl_shell_surface        *pWlShellSurface;
    /** The XDG surface, if using pxdgWmBase rather than pWlShell. */
    struct xdg_surface             *pXdgSurface;
    /** The XDG toplevel interface for pXdgSurface. */
    struct xdg_toplevel            *pXdgToplevel;
    /** The Gtk Shell 1 surface, if used. */
    struct gtk_surface1            *pGtkSurface1;

    /** Handle to temporary file used for the shared memory pool. */
    RTFILE                          hPoolTmpFile;
    /** The anonymouse shared memory pool file descriptor. */
    int                             fdPool;
    /** The shared memory pool. */
    struct wl_shm_pool             *pShmPool;
    /** The buffer for the surface. */
    struct wl_buffer               *pBuffer;

    /** On focus callback. */
    PFNVBGHWAYLANDPOPUPONFOCUS      pfnOnFocus;
    /** The 64-bit number associated with pfnOnFocus. */
    uint64_t                        u64OnFocusUser;

} VBGHWAYLANDPOPUP;
/** Pointer to a Wayland popup surface state. */
typedef VBGHWAYLANDPOPUP *PVBGHWAYLANDPOPUP;


RT_C_DECLS_BEGIN


VBGH_DECL(void) VbghWaylandPopupInit(PVBGHWAYLANDPOPUP pThis, struct VBGHWAYLANDCORE *pGhCore);
VBGH_DECL(void) VbghWaylandPopupTerm(PVBGHWAYLANDPOPUP pThis);
VBGH_DECL(void) VbghWaylandPopupRegEnum(PVBGHWAYLANDPOPUP pThis, struct wl_registry *pRegistry, uint32_t uObjName,
                                        const char *pszIfaceName, uint32_t uIfaceVersion);
VBGH_DECL(int)  VbghWaylandPopupShow(PVBGHWAYLANDPOPUP pThis, struct VBGHWAYLANDSEAT *pSeatEntry,
                                     const char *pszTitle, const char *pszClassOrId,
                                     PFNVBGHWAYLANDPOPUPONFOCUS pfnOnFocus, uint64_t u64OnFocusUser, PRTERRINFO pErrInfo);

struct RTPROCSTATUS;
VBGH_DECL(int)  VbghWaylandPopupTerminateAndWaitForChild(RTPROCESS hProcess, const char *pszProcName,
                                                         RTMSINTERVAL cMsWaitBeforeKill, RTMSINTERVAL cMsWaitBetweenKills,
                                                         unsigned cKills, struct RTPROCSTATUS *pProcStatus);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_GuestHost_WaylandPopup_h */

