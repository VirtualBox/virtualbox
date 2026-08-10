/* $Id: VbghWaylandPopup.cpp 114738 2026-07-21 13:40:26Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest / Host common code - Wayland Popup Surface.
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
#include <VBox/GuestHost/WaylandPopup.h>
#include <VBox/GuestHost/Wayland.h>

#include <VBox/log.h>
#include <iprt/file.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include <wayland-client-protocol.h>
#include <gtk-shell.h>
#include <xdg-shell.h>
#include <xdg-activation-v1.h>

#include <errno.h>
#include <unistd.h>
#ifdef RT_OS_LINUX /* Since 3.17 */
# include <sys/mman.h>
# include <fcntl.h>
# include <sys/syscall.h>
# ifndef SYS_memfd_create
#  if defined(RT_ARCH_X86)
#   define SYS_memfd_create 356
#  elif defined(RT_ARCH_AMD64)
#   define SYS_memfd_create 319
#  elif defined(RT_ARCH_ARM32)
#   define SYS_memfd_create 385
#  elif defined(RT_ARCH_ARM64)
#   define SYS_memfd_create 279
#  else
#   error "Missing SYS_memfd_create for your architecture"
#  endif
# endif
#endif


/**
 * Initialize the state structure.
 */
VBGH_DECL(void) VbghWaylandPopupInit(PVBGHWAYLANDPOPUP pThis, PVBGHWAYLANDCORE pGhCore)
{
    RT_ZERO(*pThis);
    pThis->pGhCore = pGhCore;
    pThis->fdPool  = -1;
}


/**
 * Cleanup an initialized popup state.
 */
VBGH_DECL(void) VbghWaylandPopupTerm(PVBGHWAYLANDPOPUP pThis)
{
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pXdgToplevel,      xdg_toplevel);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pXdgSurface,       xdg_surface);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pWlShellSurface,   wl_shell_surface);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pSurface,          wl_surface);

    VBGH_WAYLAND_DESTROY_PROXY(pThis->pBuffer,           wl_buffer);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pShmPool,          wl_shm_pool);

    if (pThis->hPoolTmpFile != NIL_RTFILE)
    {
        RTFileClose(pThis->hPoolTmpFile);
        pThis->hPoolTmpFile = NIL_RTFILE;
        pThis->fdPool = -1;
    }
    else if (pThis->fdPool >= 0)
    {
        close(pThis->fdPool);
        pThis->fdPool = -1;
    }

    VBGH_WAYLAND_DESTROY_PROXY(pThis->pKeyboard,         wl_keyboard);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pGtkShell1,        gtk_shell1);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pXdgActivation,    xdg_activation_v1);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pXdgWmBase,        xdg_wm_base);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pWlShell,          wl_shell);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pWlShm,            wl_shm);
    VBGH_WAYLAND_DESTROY_PROXY(pThis->pWlCompositor,     wl_compositor);
    pThis->pGhCore = NULL;
}


/**
 * @callback_method_impl{FNVBGHWAYLANDREGENUM,
 *      Wayland global registry object enumeration callback.}
 */
VBGH_DECL(void) VbghWaylandPopupRegEnum(PVBGHWAYLANDPOPUP pThis, struct wl_registry *pRegistry, uint32_t uObjName,
                                        const char *pszIfaceName, uint32_t uIfaceVersion)
{
    /*
     * Call wl_registry_bind & return if matching anything we're interested in.
     */
    VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, uObjName, pszIfaceName, uIfaceVersion, NULL,
                                                 wl_compositor_interface, struct wl_compositor *,
                                                 pThis->pWlCompositor, 2 /*uMinVersion*/);
    VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, uObjName, pszIfaceName, uIfaceVersion, NULL,
                                                 wl_shm_interface, struct wl_shm *,
                                                 pThis->pWlShm, 1 /*uMinVersion*/);

    VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, uObjName, pszIfaceName, uIfaceVersion, NULL,
                                                 wl_shell_interface, struct wl_shell *,
                                                 pThis->pWlShell, 1 /*uMinVersion*/);
    VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, uObjName, pszIfaceName, uIfaceVersion, NULL,
                                                 xdg_wm_base_interface, struct xdg_wm_base *,
                                                 pThis->pXdgWmBase, 1 /*uMinVersion*/);
    VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, uObjName, pszIfaceName, uIfaceVersion, NULL,
                                                 xdg_activation_v1_interface, struct xdg_activation_v1 *,
                                                 pThis->pXdgActivation, 1 /*uMinVersion*/);
    VBGH_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, uObjName, pszIfaceName, uIfaceVersion, NULL,
                                                 gtk_shell1_interface, struct gtk_shell1 *,
                                                 pThis->pGtkShell1, 4 /*uMinVersion*/);
}


/**
 * Helper for setting up the buffer of the surface.
 */
static int vbghWaylandPopupCreateAndAttachBuffer(PVBGHWAYLANDPOPUP pThis, PRTERRINFO pErrInfo)
{
    /*
     * The buffer size config.
     */
    static unsigned const s_cx = 1;
    static unsigned const s_cy = 1;
    static unsigned const s_cbScanline = s_cx * 4 /*ARGB*/;
    static unsigned const s_cbBuffer   = s_cy * s_cbScanline;

    /*
     * Create an anonymous file with s_cbBuffer of zeros (transparent when ARGB).
     */
    int fd;
#ifdef RT_OS_LINUX
# ifndef MFD_CLOEXEC /* Since 3.17 & glibc 2.27 (way too new for typical addition build targets) */
    fd = syscall(SYS_memfd_create, __func__, 0 /*fFlags*/);
# else
    fd = memfd_create(__func__, 0 /*fFlags*/);
# endif
    if (fd < 0)
#endif
    {
#if defined(RT_OS_LINUX) && defined(SHM_ANON) /** @todo POSIX 2008, so probably available elsewhere... */
        fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
        if (fd < 0)
#endif
        {
            char szIgnore[RTPATH_MAX];
            int rc = RTFileOpenTemp(&pThis->hPoolTmpFile, szIgnore, sizeof(szIgnore),
                                    RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_CREATE | RTFILE_O_TEMP_AUTO_DELETE);
            if (RT_FAILURE(rc))
                return RTErrInfoSetF(pErrInfo, rc, "RTFileOpenTemp failed: %Rrc", rc);
            fd = RTFileToNative(pThis->hPoolTmpFile);
            Assert(fd >= 0);
        }
    }
    pThis->fdPool = fd;

    int rc = ftruncate(fd, s_cbBuffer);
    AssertLogRelMsg(rc == 0, ("ftruncate(%d,%u) failed: rc=%d, errno=%d\n", fd, s_cbBuffer, rc, errno));

    /*
     * Create a shared memory pool for fd and allocate a buffer from it.
     */
    struct wl_shm_pool * const pShmPool = wl_shm_create_pool(pThis->pWlShm, fd, s_cbBuffer);
    AssertPtrReturn(pShmPool, RTERRINFO_LOG_REL_SET(pErrInfo, VERR_GENERAL_FAILURE, "wl_shm_create_pool failed"));
    pThis->pShmPool = pShmPool;

    struct wl_buffer * const pWlBuffer = wl_shm_pool_create_buffer(pShmPool, 0 /*offset*/, s_cx, s_cy, s_cbScanline,
                                                                   WL_SHM_FORMAT_ARGB8888);
    AssertPtrReturn(pWlBuffer, RTERRINFO_LOG_REL_SET(pErrInfo, VERR_GENERAL_FAILURE, "wl_shm_pool_create_buffer failed"));

    /*
     * Attach the buffer to the surface.
     */
    wl_surface_attach(pThis->pSurface, pWlBuffer, 0, 0);
    wl_surface_damage(pThis->pSurface, 0, 0, s_cx, s_cy);

    return VINF_SUCCESS;
}


/** Ping/pong for checking that we're alive.   */
static void vbghWlPopupShellSurfaceListener_Ping(void *pvUser, struct wl_shell_surface *pSurface, uint32_t uSerial)
{
    wl_shell_surface_pong(pSurface, uSerial);
    LogRel6(("%s: pSurface=%p uSerial=%#x\n", __func__, pSurface, uSerial));
    RT_NOREF(pvUser);
}

/** Surface resize suggestion. */
static void vbghWlPopupShellSurfaceListener_Configure(void *pvUser, struct wl_shell_surface *pSurface, uint32_t uEdges,
                                                      int32_t cx, int32_t cy)
{
    RT_NOREF(pvUser, pSurface, uEdges, cx, cy);
    LogRel6(("%s: pSurface=%p uEdges=%u %dx%d\n", __func__, pSurface, uEdges, cx, cy));
}

/** Done. */
static void vbghWlPopupShellSurfaceListener_PopupDone(void *pvUser, struct wl_shell_surface *pSurface)
{
    RT_NOREF(pvUser, pSurface);
    LogRel6(("%s: pSurface=%p\n", __func__, pSurface));
}

/** wl_shell_surface notification callbacks for the popup surface. */
static struct wl_shell_surface_listener const g_vbghWlPopupShellSurfaceListener =
{
    /* .ping = */       vbghWlPopupShellSurfaceListener_Ping,
    /* .configure = */  vbghWlPopupShellSurfaceListener_Configure,
    /* .popup_done = */ vbghWlPopupShellSurfaceListener_PopupDone,
};


/** Ping/pong for checking that we're alive.   */
static void g_vbghWlPopupXdgWmBaseListener_Ping(void *pvUser, struct xdg_wm_base *pXdgWmBase, uint32_t uSerial)
{
    xdg_wm_base_pong(pXdgWmBase, uSerial);
    LogRel6(("%s: pXdgWmBase=%p uSerial=%#x\n", __func__, pXdgWmBase, uSerial));
    RT_NOREF(pvUser);
}

/** xdg_wm_base notification callback for the popup surface. */
static struct xdg_wm_base_listener const g_vbghWlPopupXdgWmBaseListener =
{
    /* .ping = */ g_vbghWlPopupXdgWmBaseListener_Ping
};


/** Suggests a XDG surface reconfiguration. */
static void vbghWlPopupXdgSurfaceListener_Configure(void *pvUser, struct xdg_surface *pXdgSurface, uint32_t uSerial)
{
    RT_NOREF(pvUser);
    xdg_surface_ack_configure(pXdgSurface, uSerial);
}

/** xdg_surface_listener notification callbacks for the popup surface. */
static struct xdg_surface_listener const g_vbghWlPopupXdgSurfaceListener =
{
    /* .configure = */ vbghWlPopupXdgSurfaceListener_Configure,
};


/** Suggest a change to the surface. */
static void g_vbghWlPopupXdgToplevelListener_Configure(void *pvUser, struct xdg_toplevel *pToplevel, int32_t cx, int32_t cy,
                                                       struct wl_array *pStates)
{
    RT_NOREF(pvUser, pToplevel, cx, cy, pStates);
}

/** The surface wants to be closed. */
static void g_vbghWlPopupXdgToplevelListener_Close(void *pvUser, struct xdg_toplevel *pToplevel)
{
    RT_NOREF(pvUser, pToplevel);
}

/** Window size recommendations.   */
static void g_vbghWlPopupXdgToplevelListener_ConfigureBounds(void *pvUser, struct xdg_toplevel *pToplevel, int32_t cx, int32_t cy)
{
    RT_NOREF(pvUser, pToplevel, cx, cy);
}

/** Compositor capabilities. */
static void g_vbghWlPopupXdgToplevelListener_WmCapabilities(void *pvUser, struct xdg_toplevel *pToplevel, struct
                                                            wl_array *pCapabilities)
{
    RT_NOREF(pvUser, pToplevel, pCapabilities);
}

/** xdg_toplevel_listener notification callbacks for the popup surface. */
static struct xdg_toplevel_listener const g_vbghWlPopupXdgToplevelListener =
{
    /* .configure = */          g_vbghWlPopupXdgToplevelListener_Configure,
    /* .close = */              g_vbghWlPopupXdgToplevelListener_Close,
    /* .configure_bounds = */   g_vbghWlPopupXdgToplevelListener_ConfigureBounds,
    /* .wm_capabilities = */    g_vbghWlPopupXdgToplevelListener_WmCapabilities,
};


/**
 * Helper that creates the surface w/o buffer content.
 */
static int vbghWaylandPopupCreateSurface(PVBGHWAYLANDPOPUP pThis, const char *pszTitle,
                                         const char *pszClassOrId, PRTERRINFO pErrInfo)
{
    pThis->pSurface = wl_compositor_create_surface(pThis->pWlCompositor);
    AssertPtrReturn(pThis->pSurface, RTERRINFO_LOG_REL_SET(pErrInfo, VERR_GENERAL_FAILURE, "wl_compositor_create_surface failed"));

    if (pThis->pWlShell)
    {
        LogRel3(("%s: Using wl-shell\n", __func__));
        struct wl_shell_surface *pShellSurface = wl_shell_get_shell_surface(pThis->pWlShell, pThis->pSurface);
        AssertPtrReturn(pShellSurface, RTERRINFO_LOG_REL_SET(pErrInfo, VERR_GENERAL_FAILURE, "wl_shell_get_shell_surface failed"));
        pThis->pWlShellSurface = pShellSurface;

        int rc = wl_shell_surface_add_listener(pShellSurface, &g_vbghWlPopupShellSurfaceListener, pThis);
        AssertLogRel(!rc);
        wl_shell_surface_set_toplevel(pShellSurface);
        wl_shell_surface_set_class(pShellSurface, pszClassOrId);
        wl_shell_surface_set_title(pShellSurface, pszTitle);
    }
    else
    {
        LogRel3(("%s: Using xdg-wm-base\n", __func__));
        xdg_wm_base_add_listener(pThis->pXdgWmBase, &g_vbghWlPopupXdgWmBaseListener, pThis);
        struct xdg_surface * const pXdgSurface = xdg_wm_base_get_xdg_surface(pThis->pXdgWmBase, pThis->pSurface);
        AssertPtrReturn(pXdgSurface, RTERRINFO_LOG_REL_SET(pErrInfo, VERR_GENERAL_FAILURE, "xdg_wm_base_get_xdg_surface failed"));
        pThis->pXdgSurface = pXdgSurface;

        int rc = xdg_surface_add_listener(pXdgSurface, &g_vbghWlPopupXdgSurfaceListener, pThis);
        AssertLogRel(!rc);

        struct xdg_toplevel * const pXdgToplevel = xdg_surface_get_toplevel(pXdgSurface);
        AssertPtrReturn(pXdgSurface, RTERRINFO_LOG_REL_SET(pErrInfo, VERR_GENERAL_FAILURE, "xdg_surface_get_toplevel failed"));
        pThis->pXdgToplevel = pXdgToplevel;

        rc = xdg_toplevel_add_listener(pXdgToplevel, &g_vbghWlPopupXdgToplevelListener, pThis);
        AssertLogRel(!rc);
        xdg_toplevel_set_app_id(pXdgToplevel, pszTitle); /* shows up in alt-tab, so same as title for now. */
        xdg_toplevel_set_title(pXdgToplevel, pszTitle);
    }

    /* Associate with Gtk Shell too, if available. */
    if (pThis->pGtkShell1)
    {
        pThis->pGtkSurface1 = gtk_shell1_get_gtk_surface(pThis->pGtkShell1, pThis->pSurface);
        AssertLogRel(RT_VALID_PTR(pThis->pGtkSurface1));
    }

    return VINF_SUCCESS;
}


/** Keyboard keymap notification callback. */
static void vbghWlPopupKeyboardListener_Keymap(void *pvUser, struct wl_keyboard *pKeyboard, uint32_t uFormat, int32_t fd, uint32_t cb)
{
    RT_NOREF(pvUser, pKeyboard, uFormat, cb);
    close(fd);
}

/** Keyboard enter (focus) notification callback. */
static void vbghWlPopupKeyboardListener_Enter(void *pvUser, struct wl_keyboard *pKeyboard, uint32_t uSerial,
                                              struct wl_surface *pSurface, struct wl_array *pKeys)
{
    PVBGHWAYLANDPOPUP const pThis = (PVBGHWAYLANDPOPUP)pvUser;
    LogRel3(("%s: uSerial=%#x\n", __func__, uSerial));
    if (pThis->pfnOnFocus)
        pThis->pfnOnFocus(pThis, uSerial, pThis->u64OnFocusUser);
    RT_NOREF(pKeyboard, pSurface, pKeys);
}

/** Keyboard leave (focus) notification callback. */
static void vbghWlPopupKeyboardListener_Leave(void *pvUser, struct wl_keyboard *pKeyboard, uint32_t uSerial,
                                              struct wl_surface *pSurface)
{
    LogRel3(("%s: uSerial=%#x\n", __func__, uSerial));
    RT_NOREF(pvUser, pKeyboard, uSerial, pSurface);
}

/** Keyboard key event notification callback. */
static void vbghWlPopupKeyboardListener_Key(void *pvUser, struct wl_keyboard *pKeyboard, uint32_t uSerial,
                                            uint32_t msTime, uint32_t uKey, uint32_t uKeyState)
{
    LogRel3(("%s: uSerial=%#x msTime=%u uKey=%#x uKeyState=%#x\n", __func__, uSerial, msTime, uKey, uKeyState));
    RT_NOREF(pvUser, pKeyboard, uSerial, msTime, uKey, uKeyState);
}

/** Keyboard key event notification callback. */
static void vbghWlPopupKeyboardListener_Modifiers(void *pvUser, struct wl_keyboard *pKeyboard, uint32_t uSerial,
                                                  uint32_t fModsDepressed, uint32_t fModsLatched, uint32_t fModsLocked,
                                                  uint32_t uGroup)
{
    LogRel3(("%s: uSerial=%#x fModsDepressed=%#x fModsLatched=%#x fModsLocked=%#x uGroup=%#x\n",
             __func__, uSerial, fModsDepressed, fModsLatched, fModsLocked, uGroup));
    RT_NOREF(pvUser, pKeyboard, uSerial, fModsDepressed, fModsLatched, fModsLocked, uGroup);
}

/** Keyboard repeat rate & delay notification callback. */
static void vbghWlPopupKeyboardListener_RepeatInfo(void *pvUser, struct wl_keyboard *pKeyboard, int32_t uHzRate, int32_t cMsDelay)
{
    RT_NOREF(pvUser, pKeyboard, uHzRate, cMsDelay);
}

/** Keyboard notification callbacks for the popup surface. */
static struct wl_keyboard_listener const g_vbghWlPopupKeyboardListener =
{
    /* .keymap = */         vbghWlPopupKeyboardListener_Keymap,
    /* .enter = */          vbghWlPopupKeyboardListener_Enter,
    /* .leave = */          vbghWlPopupKeyboardListener_Leave,
    /* .key = */            vbghWlPopupKeyboardListener_Key,
    /* .modifiers = */      vbghWlPopupKeyboardListener_Modifiers,
    /* .repeat_info = */    vbghWlPopupKeyboardListener_RepeatInfo,
};


/**
 * Creates and shows the invisible popup, doing the focus grabbing.
 */
VBGH_DECL(int) VbghWaylandPopupShow(PVBGHWAYLANDPOPUP pThis, PVBGHWAYLANDSEAT pSeatEntry,
                                    const char *pszTitle, const char *pszClassOrId,
                                    PFNVBGHWAYLANDPOPUPONFOCUS pfnOnFocus, uint64_t u64OnFocusUser, PRTERRINFO pErrInfo)
{
    /*
     * Check requirements before we start...
     */
    AssertPtrReturn(pThis->pWlCompositor,
                    RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_NOT_FOUND, "pWlCompositor=%p", pThis->pWlCompositor));
    AssertPtrReturn(pThis->pGhCore, RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_INVALID_STATE, "pGhCore=%p", pThis->pGhCore));
    AssertPtrReturn(pSeatEntry, RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_NOT_FOUND, "pSeatEntry=%p", pSeatEntry));
    AssertReturn(pSeatEntry->fCaps & WL_SEAT_CAPABILITY_KEYBOARD,
                 RTERRINFO_LOG_REL_SET(pErrInfo, VERR_INVALID_FLAGS, "Missing WL_SEAT_CAPABILITY_KEYBOARD on seat"));
    AssertReturn(RT_VALID_PTR(pThis->pWlShell) || RT_VALID_PTR(pThis->pXdgWmBase),
                 RTERRINFO_LOG_REL_SET(pErrInfo, VERR_NOT_FOUND, "No wl_shell or xdg_wm_base found!"));
    AssertPtrNullReturn(pfnOnFocus, RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_INVALID_POINTER, "pfnOnFoucs=%p", pfnOnFocus));

    /*
     * First get the keyboard associated with the seat and register a focus
     * listener for it.
     */
    pThis->u64OnFocusUser = u64OnFocusUser;
    pThis->pfnOnFocus     = pfnOnFocus;
    if (pfnOnFocus)
    {
        pThis->pKeyboard = wl_seat_get_keyboard(pSeatEntry->pSeat);
        AssertPtrReturn(pThis->pKeyboard, RTERRINFO_LOG_REL_SET(pErrInfo, VERR_NOT_FOUND, "wl_seat_get_keyboard failed"));

        int rc = wl_keyboard_add_listener(pThis->pKeyboard, &g_vbghWlPopupKeyboardListener, pThis);
        AssertReturn(!rc, RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_GENERAL_FAILURE, "wl_keyboard_add_listener failed: %d", rc));

        wl_display_dispatch(pThis->pGhCore->pDisplay);
    }

    /*
     * Create a surface associated with the shell.
     */
    int rc = vbghWaylandPopupCreateSurface(pThis, pszTitle, pszClassOrId, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * We're ready to configure the surface.
         */
        wl_surface_commit(pThis->pSurface);
        wl_display_roundtrip(pThis->pGhCore->pDisplay);

        /*
         * Before continuing, check that we haven't finished whatever we
         * needed to do already and destroyed the surface during the
         * wl_display_roundtrip call already.
         */
        rc = VINF_SUCCESS;
        if (pThis->pSurface)
        {
            rc = vbghWaylandPopupCreateAndAttachBuffer(pThis, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Try our best to get focus.
                 */
                if (pThis->pGtkSurface1)
                    gtk_surface1_present(pThis->pGtkSurface1, 0 /*time*/);

                if (pThis->pXdgActivation)
                {
                    /* We need a token for this to work, which is hopefully present
                       in the environment... */
                    const char *pszToken = RTEnvGet("XDG_ACTIVATION_TOKEN");
                    if (!pszToken)
                        pszToken = RTEnvGet("DESKTOP_STARTUP_ID");
                    if (pszToken)
                        xdg_activation_v1_activate(pThis->pXdgActivation, pszToken, pThis->pSurface);
                    else
                        LogRel2(("warning: %s: No XDG activation token found...\n", __func__));
                }

                wl_surface_commit(pThis->pSurface);
            }
        }
    }

    return rc;
}


/**
 * Utility function for zombie management of popup helper child processes.
 *
 * If the child process doesn't quit reasonably (@a cMsWaitBeforeKill) fast on
 * its own, `kill -9` will be issued and some further waiting ensues (@a
 * cMsWaitBetweenKills).  The killing is repeated @a cKills times.
 */
VBGH_DECL(int) VbghWaylandPopupTerminateAndWaitForChild(RTPROCESS hProcess, const char *pszProcName,
                                                        RTMSINTERVAL cMsWaitBeforeKill, RTMSINTERVAL cMsWaitBetweenKills,
                                                        unsigned cKills, PRTPROCSTATUS pProcStatus)
{
    AssertStmt(cKills < ~(unsigned)0, cKills -= 1);
    RTPROCSTATUS ProcStatusTmp;
    if (!pProcStatus)
        pProcStatus = &ProcStatusTmp;

    /*
     * Since we cannot wait on children with a generic timeout, we first poll
     * for 1 second, then do kill -9 and poll for another 5 seconds, and repeat
     * that once again before giving up.  A total of ~11 seconds.
     */
    pProcStatus->enmReason = RTPROCEXITREASON_ABEND;
    pProcStatus->iStatus   = -1;
    int rc = RTProcWait(hProcess, RTPROCWAIT_FLAGS_NOBLOCK, pProcStatus);
    unsigned iLoop;
    for (iLoop = 0; rc == VERR_PROCESS_RUNNING && iLoop <= cKills; iLoop++)
    {
        uint64_t const msStart = RTTimeMilliTS();
        do
        {
            RTThreadSleep(32);
            pProcStatus->enmReason = RTPROCEXITREASON_ABEND;
            pProcStatus->iStatus   = -1;
            rc = RTProcWait(hProcess, RTPROCWAIT_FLAGS_NOBLOCK, pProcStatus);
        } while (   rc == VERR_PROCESS_RUNNING
                 && RTTimeMilliTS() - msStart < (!iLoop ? cMsWaitBeforeKill : cMsWaitBetweenKills));
        if (rc == VERR_PROCESS_RUNNING)
            RTProcTerminate(hProcess);
    }
    if (RT_SUCCESS(rc))
        LogRel2(("%s process %u (%#x) exited: iStatus=%d enmReason=%d (rc=%Rrc iLoop=%u)\n",
                 pszProcName, hProcess, hProcess, pProcStatus->iStatus, pProcStatus->enmReason, rc, iLoop));
    else
        LogRel(("error: Failed to terminate %s process %u (%#x): %Rrc (iLoop=%u)\n", pszProcName, hProcess, hProcess, rc, iLoop));
    return rc;
}

