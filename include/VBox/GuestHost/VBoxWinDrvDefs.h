/* $Id: VBoxWinDrvDefs.h 108020 2025-01-23 08:29:22Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxWinDrvDefs - Common definitions for Windows driver installation functions.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_GuestHost_VBoxWinDrvDefs_h
#define VBOX_INCLUDED_GuestHost_VBoxWinDrvDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/utf16.h>

/* If not defined explicitly, use the given / current target architecture. */
#ifndef VBOXWINDRVINF_NATIVE_ARCH_STR /* For cross testing and testcases. */
# if defined(RT_ARCH_AMD64)
#  define VBOXWINDRVINF_NATIVE_ARCH_STR     "AMD64"
# elif defined(RT_ARCH_X86)
#  define VBOXWINDRVINF_NATIVE_ARCH_STR     "X86"
# elif defined(RT_ARCH_ARM64)
#  define VBOXWINDRVINF_NATIVE_ARCH_STR     "ARM64"
# else
#  error "Port me!"
# endif
#endif

/** Defines a string which emits the decoration separator. */
#define VBOXWINDRVINF_DECORATION_SEP_STR "."

/** Defines a string which emits the decoration separator (UTF-16 version). */
#define VBOXWINDRVINF_DECORATION_SEP_UTF16_STR L"."

/** Defines a string which emits the bulld target's native architeture, e.g. "NTAMD64". */
#define VBOXWINDRVINF_NT_NATIVE_ARCH_STR "NT" VBOXWINDRVINF_NATIVE_ARCH_STR

/** Defines a string which emits the bulld target's native architeture, e.g. ".NTAMD64". */
#define VBOXWINDRVINF_DOT_NT_NATIVE_ARCH_STR VBOXWINDRVINF_DECORATION_SEP_STR VBOXWINDRVINF_NT_NATIVE_ARCH_STR

/** Maximum INF catalog file (.cat) length (in characters). */
#define VBOXWINDRVINF_MAX_CATALOG_FILE_LEN     255
/** Maximum INF driver version length (in characters). */
#define VBOXWINDRVINF_MAX_DRIVER_VER_LEN       255
/** Maximum INF provider name length (in characters). */
#define VBOXWINDRVINF_MAX_PROVIDER_NAME_LEN    255
/** Maximum INF section name length (in characters). */
#define VBOXWINDRVINF_MAX_SECTION_NAME_LEN     255
/** Maximum INF model name length (in characters). */
#define VBOXWINDRVINF_MAX_MODEL_NAME_LEN       255
/** Maximum INF PnP ID length (in characters). */
#define VBOXWINDRVINF_MAX_PNP_ID_LEN           255


/**
 * Structure for keeping INF Version section information.
 */
typedef struct VBOXWINDRVINFSECVERSION
{
    /** Catalog (.cat) file. */
    RTUTF16 wszCatalogFile[VBOXWINDRVINF_MAX_CATALOG_FILE_LEN];
    /** Driver version. */
    RTUTF16 wszDriverVer[VBOXWINDRVINF_MAX_DRIVER_VER_LEN];
    /** Provider name. */
    RTUTF16 wszProvider[VBOXWINDRVINF_MAX_PROVIDER_NAME_LEN];
} VBOXWINDRVINFSECVERSION;
/** Pointer to structure for keeping INF Version section information. */
typedef VBOXWINDRVINFSECVERSION *PVBOXWINDRVINFSECVERSION;

#endif /* !VBOX_INCLUDED_GuestHost_VBoxWinDrvDefs_h */
