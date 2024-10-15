/* $Id: UsbTestServicePlatform.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestServ - Remote USB test configuration and execution server, Platform specific helpers.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_usb_UsbTestServicePlatform_h
#define VBOX_INCLUDED_SRC_usb_UsbTestServicePlatform_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/**
 * Initializes the platform specific structures for UTS.
 *
 * @returns IPRT status code.
 */
DECLHIDDEN(int) utsPlatformInit(void);

/**
 * Frees all platform specific structures for UTS.
 */
DECLHIDDEN(void) utsPlatformTerm(void);

/**
 * Loads the specified kernel module on the platform.
 *
 * @returns IPRT status code.
 * @param   pszModule         The module to load.
 * @param   papszArgv         Array of arguments to pass to the module.
 * @param   cArgv             Number of argument array entries.
 */
DECLHIDDEN(int) utsPlatformModuleLoad(const char *pszModule, const char **papszArgv,
                                      unsigned cArgv);

/**
 * Unloads the specified kernel module on the platform.
 *
 * @returns IPRT status code.
 * @param   pszModule         The module to unload.
 */
DECLHIDDEN(int) utsPlatformModuleUnload(const char *pszModule);

#ifdef RT_OS_LINUX

/**
 * Acquires a free UDC to attach a gadget to.
 *
 * @returns IPRT status code.
 * @param   fSuperSpeed       Flag whether a super speed bus is required.
 * @param   ppszUdc           Where to store the pointer to the name of the UDC on success.
 *                            Free with RTStrFree().
 * @param   puBusId           Where to store the bus ID the UDC is attached to on the host side.
 */
DECLHIDDEN(int) utsPlatformLnxAcquireUDC(bool fSuperSpeed, char **ppszUdc, uint32_t *puBusId);

/**
 * Releases the given UDC for other use.
 *
 * @returns IPRT status code.
 * @param   pszUdc            The UDC to release.
 */
DECLHIDDEN(int) utsPlatformLnxReleaseUDC(const char *pszUdc);

#endif

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_usb_UsbTestServicePlatform_h */

