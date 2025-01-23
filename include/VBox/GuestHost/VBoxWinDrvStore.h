/* $Id: VBoxWinDrvStore.h 108020 2025-01-23 08:29:22Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxWinDrvInst - Header for Windows driver store handling.
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

#ifndef VBOX_INCLUDED_GuestHost_VBoxWinDrvStore_h
#define VBOX_INCLUDED_GuestHost_VBoxWinDrvStore_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/list.h>
#include <iprt/path.h>
#include <iprt/utf16.h>

#include <VBox/GuestHost/VBoxWinDrvDefs.h>

/** Maximum model PnP ID length (in characters). */
#define VBOXWINDRVSTORE_MAX_PNP_ID          255
/** Maximum model name length (in characters). */
#define VBOXWINDRVSTORE_MAX_MODEL_NAME      255
/** Maximum driver name length (in characters). */
#define VBOXWINDRVSTORE_MAX_DRIVER_NAME     255

/**
 * Structure for keeping a generic Windows driver store list.
 */
typedef struct VBOXWINDRVSTORELIST
{
    /** List node. */
    RTLISTANCHOR List;
    /** Number of current entries of type VBOXWINDRVSTOREENTRY. */
    size_t       cEntries;
} VBOXWINDRVSTORELIST;
/** Pointer to a generic Windows driver store list. */
typedef VBOXWINDRVSTORELIST *PVBOXWINDRVSTORELIST;

/**
 * Structure for keeping a Windows driver store entry.
 */
typedef struct VBOXWINDRVSTOREENTRY
{
    RTLISTNODE Node;
    /** Version section information. */
    VBOXWINDRVINFSECVERSION  Ver;
    /** Full path to the oemXXX.inf file within the driver store. */
    RTUTF16                  wszInfFile[RTPATH_MAX];
    /** PnP ID of the driver.
     *  Only the first (valid) PnP ID is supported for now */
    RTUTF16                  wszPnpId[VBOXWINDRVSTORE_MAX_PNP_ID];
    /** Model name of the driver.
     *  Only the first (valid) model name is supported for now */
    RTUTF16                  wszModel[VBOXWINDRVSTORE_MAX_MODEL_NAME];
    /** Driver name (.sys).
     *  Only the first (valid) driver name is supported for now */
    RTUTF16                  wszDriverName[VBOXWINDRVSTORE_MAX_DRIVER_NAME];
} VBOXWINDRVSTOREENTRY;
/** Pointer to a Windows driver store entry. */
typedef VBOXWINDRVSTOREENTRY *PVBOXWINDRVSTOREENTRY;

struct VBOXWINDRVSTORE;
typedef struct VBOXWINDRVSTORE *PVBOXWINDRVSTORE;

/**
 * Interface for a Windows driver store implementation.
 */
typedef struct VBOXWINDRVSTOREIFACE
{
    /**
     * Adds a driver to the driver store.
     *
     * @returns VBox status code.
     * @param   pThis           Pointer to interface instance.
     * @param   pszInfFile      Path of INF file to add.
     *
     * Optional and can be NULL.
     */
    DECLCALLBACKMEMBER(int, pfnDriverAdd,(PVBOXWINDRVSTORE pThis, const char *pszInfFile));
    /**
     * Removes a driver from the driver store.
     *
     * @returns VBox status code.
     * @param   pThis           Pointer to interface instance.
     * @param   pszInfFile      Path of INF file to remove.
     *
     * Optional and can be NULL.
     */
    DECLCALLBACKMEMBER(int, pfnDriverRemove,(PVBOXWINDRVSTORE pThis, const char *pszInfFile, bool fForce));
    /**
     * Performs (re-)enumeration of the driver store entries.
     *
     * @returns VBox status code.
     * @param   pThis           Pointer to interface instance.
     */
    DECLCALLBACKMEMBER(int, pfnEnumerate,(PVBOXWINDRVSTORE pThis));
} VBOXWINDRVSTOREIFACE;
/** Pointer to a Windows driver store implementation. */
typedef VBOXWINDRVSTOREIFACE *PVBOXWINDRVSTOREIFACE;

/**
 * Enumeration for a driver store backend.
 */
typedef enum VBOXWINDRVSTOREBACKENDTYPE
{
    /** Invalid. */
    VBOXWINDRVSTOREBACKENDTYPE_INVALID = 0,
    /** Local file system. */
    VBOXWINDRVSTOREBACKENDTYPE_LOCAL_FS
} VBOXWINDRVSTOREBACKENDTYPE;

/**
 * Structure for keeping a Windows driver store backend.
 *
 * Currently only the (local) file system backend is supported.
 */
typedef struct VBOXWINDRVSTOREBACKEND
{
    VBOXWINDRVSTOREBACKENDTYPE enmType;
    /** The Windows driver store interface to use. */
    VBOXWINDRVSTOREIFACE       Iface;
    union
    {
        struct
        {
            char szPathAbs[RTPATH_MAX];
        } LocalFs;
    } u;
} VBOXWINDRVSTOREBACKEND;
/** Pointer to a Windows driver store backend. */
typedef VBOXWINDRVSTOREBACKEND *PVBOXWINDRVSTOREBACKEND;

/**
 * Structure for keeping Windows driver store instance data.
 */
typedef struct VBOXWINDRVSTORE
{
    /** The current list of drivers. */
    VBOXWINDRVSTORELIST    lstDrivers;
    /** The backend this driver store uses. */
    VBOXWINDRVSTOREBACKEND Backend;
} VBOXWINDRVSTORE;
/** Pointer to Windows driver store instance data. */
typedef VBOXWINDRVSTORE *PVBOXWINDRVSTORE;

int VBoxWinDrvStoreCreate(PVBOXWINDRVSTORE *ppDrvStore);
void VBoxWinDrvStoreDestroy(PVBOXWINDRVSTORE pDrvStore);
int VBoxWinDrvStoreQueryAny(PVBOXWINDRVSTORE pDrvStore, const char *pszPattern, PVBOXWINDRVSTORELIST *ppResults);
int VBoxWinDrvStoreQueryAll(PVBOXWINDRVSTORE pDrvStore, PVBOXWINDRVSTORELIST *ppResults);
int VBoxWinDrvStoreQueryByPnpId(PVBOXWINDRVSTORE pDrvStore, const char *pszPnpId, PVBOXWINDRVSTORELIST *ppResults);
int VBoxWinDrvStoreQueryByModelName(PVBOXWINDRVSTORE pDrvStore, const char *pszModelName, PVBOXWINDRVSTORELIST *ppResults);

const char *VBoxWinDrvStoreBackendGetLocation(PVBOXWINDRVSTORE pDrvStore);

void VBoxWinDrvStoreListFree(PVBOXWINDRVSTORELIST pList);

#endif /* !VBOX_INCLUDED_GuestHost_VBoxWinDrvStore_h */

