/* $Id: VBoxTray.h 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
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
 * The environment information for a single VBoxTray service.
 */
typedef struct VBOXTRAYSVCENV
{
    /** hInstance of VBoxTray. */
    HINSTANCE hInstance;
    /* Display driver interface, XPDM - WDDM abstraction see VBOXDISPIF** definitions above */
    /** @todo r=andy Argh. Needed by the "display" + "seamless" services (which in turn get called
     *               by the VBoxCaps facility. See #8037. */
    VBOXDISPIF dispIf;
} VBOXTRAYSVCENV;
/** Pointer to a VBoxTray service env info structure.  */
typedef VBOXTRAYSVCENV *PVBOXTRAYSVCENV;
/** Pointer to a const VBoxTray service env info structure.  */
typedef VBOXTRAYSVCENV const *PCVBOXTRAYSVCENV;

/**
 * A VBoxTray service descriptor.
 */
typedef struct VBOXTRAYSVCDESC
{
    /** The service's name. RTTHREAD_NAME_LEN maximum characters. */
    const char *pszName;
    /** The service description. */
    const char *pszDesc;
    /** The usage options stuff for the --help screen. */
    const char *pszUsage;
    /** The option descriptions for the --help screen. */
    const char *pszOptions;

    /** Callbacks. */

    /**
     * Called before parsing arguments.
     * @returns VBox status code.
     */
    DECLCALLBACKMEMBER(int, pfnPreInit,(void));

    /**
     * Tries to parse the given command line option.
     *
     * @returns 0 if we parsed, -1 if it didn't and anything else means exit.
     * @param   ppszShort   If not NULL it points to the short option iterator. a short argument.
     *                      If NULL examine argv[*pi].
     * @param   argc        The argument count.
     * @param   argv        The argument vector.
     * @param   pi          The argument vector index. Update if any value(s) are eaten.
     */
    DECLCALLBACKMEMBER(int, pfnOption,(const char **ppszShort, int argc, char **argv, int *pi));

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
    DECLCALLBACKMEMBER(int, pfnInit,(const PVBOXTRAYSVCENV pEnv, void **ppInstance));

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
} VBOXTRAYSVCDESC;
/** Pointer to a VBoxTray service descriptor. */
typedef VBOXTRAYSVCDESC *PVBOXTRAYSVCDESC;

/**
 * VBoxTray service initialization info and runtime variables.
 */
typedef struct VBOXTRAYSVCINFO
{
    /** Pointer to the service descriptor. */
    PVBOXTRAYSVCDESC pDesc;
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
} VBOXTRAYSVCINFO;
/** Pointer to a VBoxTray service' initialization and runtime variables. */
typedef VBOXTRAYSVCINFO *PVBOXTRAYSVCINFO;

/**
 * Structure for keeping a globally registered Windows message.
 */
typedef struct VBOXTRAYGLOBALMSG
{
    /** Message name. */
    const char *pszName;
    /** Function pointer for handling the message. */
    int       (*pfnHandler)(WPARAM wParam, LPARAM lParam);

    /* Variables. */

    /** Message ID;
     *  to be filled in when registering the actual message. */
    UINT     uMsgID;
} VBOXTRAYGLOBALMSG;
/** Pointer to a globally registered Windows message. */
typedef VBOXTRAYGLOBALMSG *PVBOXTRAYGLOBALMSG;

/*********************************************************************************************************************************
*   Externals                                                                                                                    *
*********************************************************************************************************************************/
extern VBOXTRAYSVCDESC g_SvcDescDisplay;
#ifdef VBOX_WITH_SHARED_CLIPBOARD
extern VBOXTRAYSVCDESC g_SvcDescClipboard;
#endif
extern VBOXTRAYSVCDESC g_SvcDescSeamless;
extern VBOXTRAYSVCDESC g_SvcDescVRDP;
extern VBOXTRAYSVCDESC g_SvcDescIPC;
extern VBOXTRAYSVCDESC g_SvcDescLA;
#ifdef VBOX_WITH_DRAG_AND_DROP
extern VBOXTRAYSVCDESC g_SvcDescDnD;
#endif

extern bool         g_fHasConsole;
extern unsigned     g_cVerbosity;
extern HINSTANCE    g_hInstance;
extern HWND         g_hwndToolWindow;
extern uint32_t     g_fGuestDisplaysChanged;

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxTray_VBoxTray_h */

