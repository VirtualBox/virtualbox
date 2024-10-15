/* $Id: VBoxTray.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxTray - Guest Additions Tray, Internal Header.
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

#ifndef GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTray_h
#define GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTray_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/windows.h>

#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <VBox/version.h>
#include <VBox/VBoxGuestLib.h>
#include <VBoxDisplay.h>

#include "VBoxDispIf.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Title of the program to show.
 *  Also shown as part of message boxes. */
#define VBOX_VBOXTRAY_TITLE                     "VBoxTray"

/*
 * Windows messsages.
 */

/**
 * General VBoxTray messages.
 */
#define WM_VBOXTRAY_TRAY_ICON                   WM_APP + 40

/* The tray icon's ID. */
#define ID_TRAYICON                             2000

/*
 * Timer IDs.
 */
#define TIMERID_VBOXTRAY_CHECK_HOSTVERSION      1000
#define TIMERID_VBOXTRAY_CAPS_TIMER             1001
#define TIMERID_VBOXTRAY_DT_TIMER               1002
#define TIMERID_VBOXTRAY_ST_DELAYED_INIT_TIMER  1003


/*********************************************************************************************************************************
*   Common structures                                                                                                            *
*********************************************************************************************************************************/

/**
 * The environment information for services.
 */
typedef struct VBOXSERVICEENV
{
    /** hInstance of VBoxTray. */
    HINSTANCE hInstance;
    /* Display driver interface, XPDM - WDDM abstraction see VBOXDISPIF** definitions above */
    /** @todo r=andy Argh. Needed by the "display" + "seamless" services (which in turn get called
     *               by the VBoxCaps facility. See #8037. */
    VBOXDISPIF dispIf;
} VBOXSERVICEENV;
/** Pointer to a VBoxTray service env info structure.  */
typedef VBOXSERVICEENV *PVBOXSERVICEENV;
/** Pointer to a const VBoxTray service env info structure.  */
typedef VBOXSERVICEENV const *PCVBOXSERVICEENV;

/**
 * A service descriptor.
 */
typedef struct VBOXSERVICEDESC
{
    /** The service's name. RTTHREAD_NAME_LEN maximum characters. */
    const char *pszName;
    /** The service description. */
    const char *pszDesc;

    /** Callbacks. */

    /**
     * Initializes a service.
     * @returns VBox status code.
     *          VERR_NOT_SUPPORTED if the service is not supported on this guest system. Logged.
     *          VERR_HGCM_SERVICE_NOT_FOUND if the service is not available on the host system. Logged.
     *          Returning any other error will be considered as a fatal error.
     * @param   pEnv
     * @param   ppInstance      Where to return the thread-specific instance data.
     * @todo r=bird: The pEnv type is WRONG!  Please check all your const pointers.
     */
    DECLCALLBACKMEMBER(int, pfnInit,(const PVBOXSERVICEENV pEnv, void **ppInstance));

    /** Called from the worker thread.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS if exitting because *pfShutdown was set.
     * @param   pInstance       Pointer to thread-specific instance data.
     * @param   pfShutdown      Pointer to a per service termination flag to check
     *                          before and after blocking.
     */
    DECLCALLBACKMEMBER(int, pfnWorker,(void *pInstance, bool volatile *pfShutdown));

    /**
     * Stops a service.
     */
    DECLCALLBACKMEMBER(int, pfnStop,(void *pInstance));

    /**
     * Does termination cleanups.
     *
     * @remarks This may be called even if pfnInit hasn't been called!
     */
    DECLCALLBACKMEMBER(void, pfnDestroy,(void *pInstance));
} VBOXSERVICEDESC, *PVBOXSERVICEDESC;


/**
 * The service initialization info and runtime variables.
 */
typedef struct VBOXSERVICEINFO
{
    /** Pointer to the service descriptor. */
    PVBOXSERVICEDESC pDesc;
    /** Thread handle. */
    RTTHREAD         hThread;
    /** Pointer to service-specific instance data.
     *  Must be free'd by the service itself. */
    void            *pInstance;
    /** Whether Pre-init was called. */
    bool             fPreInited;
    /** Shutdown indicator. */
    bool volatile    fShutdown;
    /** Indicator set by the service thread exiting. */
    bool volatile    fStopped;
    /** Whether the service was started or not. */
    bool             fStarted;
    /** Whether the service is enabled or not. */
    bool             fEnabled;
} VBOXSERVICEINFO, *PVBOXSERVICEINFO;

/**
 * Globally unique (system wide) message registration.
 */
typedef struct VBOXGLOBALMESSAGE
{
    /** Message name. */
    const char *pszName;
    /** Function pointer for handling the message. */
    int       (*pfnHandler)(WPARAM wParam, LPARAM lParam);

    /* Variables. */

    /** Message ID;
     *  to be filled in when registering the actual message. */
    UINT     uMsgID;
} VBOXGLOBALMESSAGE, *PVBOXGLOBALMESSAGE;


/*********************************************************************************************************************************
*   Externals                                                                                                                    *
*********************************************************************************************************************************/
extern VBOXSERVICEDESC g_SvcDescDisplay;
#ifdef VBOX_WITH_SHARED_CLIPBOARD
extern VBOXSERVICEDESC g_SvcDescClipboard;
#endif
extern VBOXSERVICEDESC g_SvcDescSeamless;
extern VBOXSERVICEDESC g_SvcDescVRDP;
extern VBOXSERVICEDESC g_SvcDescIPC;
extern VBOXSERVICEDESC g_SvcDescLA;
#ifdef VBOX_WITH_DRAG_AND_DROP
extern VBOXSERVICEDESC g_SvcDescDnD;
#endif

extern int          g_cVerbosity;
extern HINSTANCE    g_hInstance;
extern HWND         g_hwndToolWindow;
extern uint32_t     g_fGuestDisplaysChanged;

RTEXITCODE VBoxTrayShowError(const char *pszFormat, ...);

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTray_h */

