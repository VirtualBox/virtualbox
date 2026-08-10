/* $Id: VBoxClient.h 114744 2026-07-21 18:37:21Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * VirtualBox additions user session daemon.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_VBoxClient_h
#define GA_INCLUDED_SRC_x11_VBoxClient_VBoxClient_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/log.h>
#include <iprt/cpp/utils.h>
#include <iprt/string.h>

#include <VBox/GuestHost/DisplayServerType.h>

/** Macros to separate selectable logging levels. */
#define VBCL_INFO(fmt, ...)     VBClLogVerbose(1, fmt, __VA_ARGS__)
#define VBCL_WARN(fmt, ...)     VBClLogVerbose(2, fmt, __VA_ARGS__)
#define VBCL_WARN_NO_ARGS(msg)  VBClLogVerbose(2, msg)
#define VBCL_DEBUG(fmt, ...)    VBClLogVerbose(3, fmt, __VA_ARGS__)
#define VBCL_DEBUG_NO_ARGS(msg) VBClLogVerbose(3, msg)

/** A shortcut to log callback entering. */
#define VBCL_LOG_CALLBACK       VBClLogVerbose(3, "%s\n", __func__)

int VBClShowNotify(const char *pszHeader, const char *pszBody);

RTEXITCODE VBClLogError(const char *pszFormat, ...);
RTEXITCODE VBClLogFatalError(const char *pszFormat, ...);
void VBClLogInfo(const char *pszFormat, ...);
void VBClLogVerbose(unsigned iLevel, const char *pszFormat, ...);

int VBClLogCreate(const char *pszLogFile);
int VBClLogCreateEx(const char *pszLogFile, bool fPrintHeader, bool fNoStdOut);
int VBClLogCreateNoStdOut(void);
int VBClLogModify(const char *pszDest, unsigned uVerbosity);
void VBClLogSetLogPrefix(const char *pszPrefix);
void VBClLogDestroy(void);

/** Call clean-up for the current service and exit. */
extern void VBClShutdown(bool fExit = true);

extern VBGHDISPLAYSERVERTYPE VBClGetDisplayServerType(void);
extern VBGHDISPLAYSERVERTYPE VBClGetDisplayServerTypeResolveAuto(void);
extern int VBClExplicitLoadClientLibrariesForDisplayServer(VBGHDISPLAYSERVERTYPE enmType, bool fXWaylandAsPureWayland);

#if defined(IPRT_INCLUDED_thread_h) || defined(DOXYGEN_RUNNING)
extern int VBClStartThread(PRTTHREAD phThread, PFNRTTHREAD pfnThread, const char *pszName, void *pvUser);
#endif

#if defined(VBOX_INCLUDED_VBoxGuestLibGuestProp_h) || defined(DOXYGEN_RUNNING)
/** Host input focus monitor state. */
typedef struct VBCLHOSTINPUTFOCUSSTATE
{
    /** @name User Settable Properties.
     * @{  */
    /**
     * Called when the host VM process receives input focus.
     *
     * @return Quit indicator - true to quit, false to keep going.
     * @param  pThis    Pointer to this structure.
     */
    DECLCALLBACKMEMBER(bool, pfnFocusEnter,(struct VBCLHOSTINPUTFOCUSSTATE *pThis));
    /**
     * Called when the host VM process loses input focus.
     *
     * @return Quit indicator - true to quit, false to keep going.
     * @param  pThis    Pointer to this structure.
     */
    DECLCALLBACKMEMBER(bool, pfnFocusExit,(struct VBCLHOSTINPUTFOCUSSTATE *pThis));
    /** Pointer to the shutdown indicator (will be set to fShutdownInternal if NULL). */
    bool volatile      *pfShutdown;
    /** Where to store user data. */
    void               *pvUser;
    /** @} */

    /** @name Internal
     *  @{ */
    /** Handle of the monitoring thread (don't touch). */
    RTTHREAD            hThread;
    /** Guest property client handle (don't touch). */
    VBGLGSTPROPCLIENT   GuestPropClient;
    /** Internal shutdown indicator (don't touch). */
    bool volatile       fShutdownInternal;
    /** @} */
} VBCLHOSTINPUTFOCUSSTATE;
#else
struct VBCLHOSTINPUTFOCUSSTATE;
#endif
/** Pointer to host input focus monitor state. */
typedef struct VBCLHOSTINPUTFOCUSSTATE *PVBCLHOSTINPUTFOCUSSTATE;

void VBClHostInputFocusMonitorInit(PVBCLHOSTINPUTFOCUSSTATE pState);
int  VBClHostInputFocusMonitorStart(PVBCLHOSTINPUTFOCUSSTATE pState, const char *pszThreadName);
void VBClHostInputFocusMonitorStop(PVBCLHOSTINPUTFOCUSSTATE pState);
int  VBClHostInputFocusMonitorTerm(PVBCLHOSTINPUTFOCUSSTATE pState);

struct RTGETOPTSTATE;

/**
 * Tries to parse the given command line option.
 *
 * This is for a service or command.
 *
 * @returns IPRT status code (RTGetOpt style).
 * @retval  VERR_GENERAL_FAILURE to return RTEXITCODE_FAILURE.
 * @param   pGetOptState    For use with RTGetOptEx.
 */
typedef DECLCALLBACKTYPE(int, FNVBCLOPTPARSE,(struct RTGETOPTSTATE *pGetOptState));
/** Pointer to FNVBCLOPTPARSE. */
typedef FNVBCLOPTPARSE *PFNVBCLOPTPARSE;


/**
 * A service descriptor.
 */
typedef struct
{
    /** The short service name. 16 chars maximum (RTTHREAD_NAME_LEN). */
    const char *pszName;
    /** The longer service name. */
    const char *pszDesc;
    /** Get the services default path to pidfile, relative to $HOME */
    /** @todo Should this also have a component relative to the X server number?
     */
    const char *pszPidFilePathTemplate;

    /** The usage options stuff for the --help screen. */
    const char *pszUsage;
    /** The option descriptions for the --help screen. */
    const char *pszOptions;
    /**
     * Try parse extra service options. Optional.
     */
    PFNVBCLOPTPARSE pfnOption;

    /**
     * Called before parsing arguments.
     * @returns VBox status code, or
     *          VERR_NOT_AVAILABLE if service is supported on this platform in general but not available at the moment.
     *          VERR_NOT_SUPPORTED if service is not supported on this platform. */
    DECLCALLBACKMEMBER(int, pfnInit,(void));

    /** Called from the worker thread.
     *
     * @returns VBox status code.
     * @retval  VINF_SUCCESS if exitting because *pfShutdown was set.
     * @param   pfShutdown      Pointer to a per service termination flag to check
     *                          before and after blocking.
     */
    DECLCALLBACKMEMBER(int, pfnWorker,(bool volatile *pfShutdown));

    /**
     * Asks the service to stop.
     *
     * @remarks Will be called from the signal handler.
     */
    DECLCALLBACKMEMBER(void, pfnStop,(void));

    /**
     * Does termination cleanups.
     *
     * @remarks This will be called even if pfnInit hasn't been called or pfnStop failed!
     */
    DECLCALLBACKMEMBER(int, pfnTerm,(void));
} VBCLSERVICE;
/** Pointer to a VBCLSERVICE. */
typedef VBCLSERVICE *PVBCLSERVICE;
/** Pointer to a const VBCLSERVICE. */
typedef VBCLSERVICE const *PCVBCLSERVICE;

RT_C_DECLS_BEGIN
extern VBCLSERVICE const g_SvcClipboard;
extern VBCLSERVICE const g_SvcDisplaySVGA;
extern VBCLSERVICE const g_SvcDisplayLegacy;
# ifdef RT_OS_LINUX
extern VBCLSERVICE const g_SvcDisplaySVGASession;
# endif
extern VBCLSERVICE const g_SvcDragAndDrop;
extern VBCLSERVICE const g_SvcHostVersion;
extern VBCLSERVICE const g_SvcSeamless;
# ifdef VBOX_WITH_WAYLAND_ADDITIONS_LEGACY
extern VBCLSERVICE const g_SvcWayland;
# endif

/**
 * A command descriptor.
 */
typedef struct
{
    /** The long option name. */
    const char *pszName;
    /** The option description. */
    const char *pszDesc;
    /** The log prefix to use.   */
    const char *pszLogPrefix;

    /** The option descriptions for the --help screen. */
    const char *pszOptions;
    /** Try parse extra command options. Optional. */
    PFNVBCLOPTPARSE pfnOption;

    /**
     * Executes the command.
     * @returns Application exit code.
     */
    DECLCALLBACKMEMBER(RTEXITCODE, pfnExecute,(void));
} VBCLCOMMAND;
/** Pointer to a const VBCLCOMMAND. */
typedef VBCLCOMMAND const *PCVBCLCOMMAND;

#ifdef VBOX_WITH_WAYLAND_ADDITIONS
extern VBCLCOMMAND const        g_CmdClipboardGet;
extern VBCLCOMMAND const        g_CmdClipboardSet;
#endif

extern unsigned                 g_cVerbosity;
extern bool                     g_fDaemonized;

DECLASM(int) ExplicitlyLoadlibwayland_client(bool fResolveAllImports, PRTERRINFO pErrInfo);

RT_C_DECLS_END

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_VBoxClient_h */
