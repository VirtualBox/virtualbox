/* $Id: VBoxMPRegistry.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Miniport registry related functions
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include "common/VBoxMPCommon.h"

static VP_STATUS
VBoxMPQueryNamedValueCB(PVOID HwDeviceExtension, PVOID Context, PWSTR ValueName, PVOID ValueData, ULONG ValueLength)
{
    RT_NOREF(HwDeviceExtension, ValueName);
    PAGED_CODE();

    if (!ValueLength || !Context)
    {
        WARN(("failed due to invalid parameters"));
        return ERROR_INVALID_PARAMETER;
    }

    *(uint32_t *)Context = *(uint32_t *)ValueData;

    return NO_ERROR;
}


VP_STATUS VBoxMPCmnRegInit(IN PVBOXMP_DEVEXT pExt, OUT VBOXMPCMNREGISTRY *pReg)
{
    *pReg = pExt->pPrimary;
    return NO_ERROR;
}

VP_STATUS VBoxMPCmnRegFini(IN VBOXMPCMNREGISTRY Reg)
{
    RT_NOREF(Reg);
    return NO_ERROR;
}

VP_STATUS VBoxMPCmnRegSetDword(IN VBOXMPCMNREGISTRY Reg, PCWSTR pName, uint32_t Val)
{
    return VideoPortSetRegistryParameters(Reg, (PWSTR)pName, &Val, sizeof(Val));
}

VP_STATUS VBoxMPCmnRegQueryDword(IN VBOXMPCMNREGISTRY Reg, PCWSTR pName, uint32_t *pVal)
{
    VP_STATUS rc = VideoPortGetRegistryParameters(Reg, (PWSTR)pName, FALSE, VBoxMPQueryNamedValueCB, pVal);
    if (rc != NO_ERROR && pVal)
        *pVal = 0;
    return rc;
}
