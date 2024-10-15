/* $Id: HBDMgmt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: Host block device management API.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Storage_HBDMgmt_h
#define VBOX_INCLUDED_SRC_Storage_HBDMgmt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>

RT_C_DECLS_BEGIN

/**
 * Opaque host block device manager.
 */
typedef struct HBDMGRINT *HBDMGR;
/** Pointer to a block device manager. */
typedef HBDMGR *PHBDMGR;

/* NIL HBD manager handle. */
#define NIL_HBDMGR ((HBDMGR)0)

/**
 * Creates host block device manager.
 *
 * @returns VBox status code.
 * @param   phHbdMgr    Where to store the handle to the block device manager on success.
 */
DECLHIDDEN(int) HBDMgrCreate(PHBDMGR phHbdMgr);

/**
 * Destroys the given block device manager unclaiming all managed block devices.
 *
 * @param   hHbdMgr     The block device manager.
 */
DECLHIDDEN(void) HBDMgrDestroy(HBDMGR hHbdMgr);

/**
 * Returns whether a given filename resembles a block device which can
 * be managed by this API.
 *
 * @returns true if the given filename point to a block device manageable
 *                      by the given manager
 *              false otherwise.
 * @param   pszFilename The block device to check.
 */
DECLHIDDEN(bool) HBDMgrIsBlockDevice(const char *pszFilename);

/**
 * Prepares the given block device for use by unmounting and claiming it for exclusive use.
 *
 * @returns VBox status code.
 * @param   hHbdMgr     The block device manager.
 * @param   pszFilename The block device to claim.
 */
DECLHIDDEN(int) HBDMgrClaimBlockDevice(HBDMGR hHbdMgr, const char *pszFilename);

/**
 * Unclaims the given block device.
 *
 * @returns VBox status code.
 * @param   hHbdMgr     The block device manager.
 * @param   pszFilename The block device to unclaim.
 */
DECLHIDDEN(int) HBDMgrUnclaimBlockDevice(HBDMGR hHbdMgr, const char *pszFilename);

/**
 * Returns whether the given block device is claimed by the manager.
 *
 * @returns true if the block device is claimed, false otherwisw.
 * @param   hHbdMgr     The block device manager.
 * @param   pszFilename The block device to check.
 */
DECLHIDDEN(bool) HBDMgrIsBlockDeviceClaimed(HBDMGR hHbdMgr, const char *pszFilename);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Storage_HBDMgmt_h */
