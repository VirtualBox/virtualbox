/* $Id: HBDMgmt-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <VBox/cdefs.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>

#include "HBDMgmt.h"

DECLHIDDEN(int) HBDMgrCreate(PHBDMGR phHbdMgr)
{
    AssertPtrReturn(phHbdMgr, VERR_INVALID_POINTER);
    *phHbdMgr = NIL_HBDMGR;
    return VINF_SUCCESS;
}

DECLHIDDEN(void) HBDMgrDestroy(HBDMGR hHbdMgr)
{
    NOREF(hHbdMgr);
}

DECLHIDDEN(bool) HBDMgrIsBlockDevice(const char *pszFilename)
{
    NOREF(pszFilename);
    return false;
}

DECLHIDDEN(int) HBDMgrClaimBlockDevice(HBDMGR hHbdMgr, const char *pszFilename)
{
    NOREF(hHbdMgr);
    NOREF(pszFilename);
    return VINF_SUCCESS;
}

DECLHIDDEN(int) HBDMgrUnclaimBlockDevice(HBDMGR hHbdMgr, const char *pszFilename)
{
    NOREF(hHbdMgr);
    NOREF(pszFilename);
    return VINF_SUCCESS;
}

DECLHIDDEN(bool) HBDMgrIsBlockDeviceClaimed(HBDMGR hHbdMgr, const char *pszFilename)
{
    NOREF(hHbdMgr);
    NOREF(pszFilename);
    return false;
}
