/* $Id: VBoxClient.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VirtualBox additions user session daemon.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_VBoxClient_h
#define GA_INCLUDED_SRC_x11_VBoxClient_VBoxClient_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/log.h>
#include <iprt/cpp/utils.h>
#include <iprt/string.h>

#include <VBox/GuestHost/DisplayServerType.h>

/** A shortcut to log callback entering. */
#define VBCL_LOG_CALLBACK VBClLogVerbose(3, "%s\n", __func__)

int VBClShowNotify(const char *pszHeader, const char *pszBody);

void VBClLogInfo(const char *pszFormat, ...);
void VBClLogError(const char *pszFormat, ...);
void VBClLogFatalError(const char *pszFormat, ...);
void VBClLogVerbose(unsigned iLevel, const char *pszFormat, ...);

int VBClLogCreate(const char *pszLogFile);
int VBClLogCreateEx(const char *pszLogFile, bool fPrintHeader);
int VBClLogModify(const char *pszDest, unsigned uVerbosity);
void VBClLogSetLogPrefix(const char *pszPrefix);
void VBClLogDestroy(void);

/** Call clean-up for the current service and exit. */
extern void VBClShutdown(bool fExit = true);

extern VBGHDISPLAYSERVERTYPE VBClGetDisplayServerType(void);

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
extern VBCLSERVICE g_SvcClipboard;
extern VBCLSERVICE g_SvcDisplayDRM;
extern VBCLSERVICE g_SvcDisplaySVGA;
extern VBCLSERVICE g_SvcDisplayLegacy;
# ifdef RT_OS_LINUX
extern VBCLSERVICE g_SvcDisplaySVGASession;
# endif
extern VBCLSERVICE g_SvcDragAndDrop;
extern VBCLSERVICE g_SvcHostVersion;
extern VBCLSERVICE g_SvcSeamless;
# ifdef VBOX_WITH_WAYLAND_ADDITIONS
extern VBCLSERVICE g_SvcWayland;
# endif

extern unsigned    g_cVerbosity;
extern bool        g_fDaemonized;
RT_C_DECLS_END

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_VBoxClient_h */
