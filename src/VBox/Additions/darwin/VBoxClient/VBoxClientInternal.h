/** $Id: VBoxClientInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxClient - common definitions, Darwin.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_darwin_VBoxClient_VBoxClientInternal_h
#define GA_INCLUDED_SRC_darwin_VBoxClient_VBoxClientInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/VBoxGuestLib.h>
#include <Carbon/Carbon.h>

/* Service description */
typedef struct
{
    /** The service name. */
    const char *pszName;

    /**
     * Start service.
     * @returns VBox status code.
     */
    DECLCALLBACKMEMBER(int, pfnStart,(void));

    /**
     * Stop service.
     * @returns VBox status code.
     */
    DECLCALLBACKMEMBER(int, pfnStop,(void));

} VBOXCLIENTSERVICE;


/*
 * Services
 */

RT_C_DECLS_BEGIN

extern VBOXCLIENTSERVICE g_ClipboardService;

RT_C_DECLS_END


/*
 * Functions
 */

/**
 * Displays a verbose message.
 *
 * @param   iLevel      Minimum log level required to display this message.
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
extern void VBoxClientVerbose(int iLevel, const char *pszFormat, ...);

/**
 * Walk through pasteboard items and report currently available item types.
 *
 * @param   pPasteboard       Reference to guest Pasteboard.
 # @returns Available formats bit field.
 */
extern uint32_t vbclClipboardGetAvailableFormats(PasteboardRef pPasteboard);

/**
 * Read host's clipboard buffer and put its content to guest clipboard.
 *
 * @param   u32ClientId    Host connection.
 * @param   pPasteboard    Guest PasteBoard reference.
 * @param   fFormats       List of data formats (bit field) received from host.
 *
 * @returns IPRT status code.
 */
extern int vbclClipboardForwardToGuest(uint32_t u32ClientId, PasteboardRef pPasteboard, uint32_t fFormats);

/**
 * Read guest's clipboard buffer and forward its content to host.
 *
 * @param   u32ClientId    Host clipboard connection.
 * @param   pPasteboard    Guest PasteBoard reference.
 * @param   fFormats       List of data formats (bit field) received from host.
 *
 * @returns IPRT status code.
 */
extern int vbclClipboardForwardToHost(uint32_t u32ClientId, PasteboardRef pPasteboard, uint32_t fFormats);

#endif /* !GA_INCLUDED_SRC_darwin_VBoxClient_VBoxClientInternal_h */
