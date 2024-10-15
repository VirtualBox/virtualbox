/* $Id: RTSystemFirmware-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - System firmware information, linux.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/system.h>

#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/linux/sysfs.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Defines the UEFI Globals UUID that is used here as filename suffix (case sensitive). */
#define VBOX_UEFI_UUID_GLOBALS "8be4df61-93ca-11d2-aa0d-00e098032b8c"


RTDECL(int) RTSystemQueryFirmwareType(PRTSYSFWTYPE penmFirmwareType)
{
    if (RTLinuxSysFsExists("firmware/efi/"))
        *penmFirmwareType = RTSYSFWTYPE_UEFI;
    else if (RTLinuxSysFsExists(""))
        *penmFirmwareType = RTSYSFWTYPE_BIOS;
    else
    {
        *penmFirmwareType = RTSYSFWTYPE_INVALID;
        return VERR_NOT_SUPPORTED;
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSystemQueryFirmwareType);


RTDECL(int) RTSystemQueryFirmwareBoolean(RTSYSFWBOOL enmBoolean, bool *pfValue)
{
    *pfValue = false;

    /*
     * Translate the property to variable base filename.
     */
    const char *pszName;
    switch (enmBoolean)
    {
        case RTSYSFWBOOL_SECURE_BOOT:
            pszName = "firmware/efi/efivars/SecureBoot";
            break;

        default:
            AssertReturn(enmBoolean > RTSYSFWBOOL_INVALID && enmBoolean < RTSYSFWBOOL_END, VERR_INVALID_PARAMETER);
            return VERR_SYS_UNSUPPORTED_FIRMWARE_PROPERTY;

    }

    /*
     * Try open and read the variable value.
     */
    RTFILE hFile;
    int rc = RTLinuxSysFsOpen(&hFile, "%s-" VBOX_UEFI_UUID_GLOBALS, pszName);
    /** @todo try other suffixes if file-not-found. */
    if (RT_SUCCESS(rc))
    {
        uint8_t abBuf[16];
        size_t  cbRead = 0;
        rc = RTLinuxSysFsReadFile(hFile, abBuf, sizeof(abBuf), &cbRead);
        *pfValue = cbRead > 1 && abBuf[cbRead - 1] != 0;
        RTFileClose(hFile);
    }
    else if (rc == VERR_FILE_NOT_FOUND || rc == VERR_PATH_NOT_FOUND)
        rc = VINF_SUCCESS;
    else if (rc == VERR_PERMISSION_DENIED)
        rc = VERR_NOT_SUPPORTED;

    return rc;
}
RT_EXPORT_SYMBOL(RTSystemQueryFirmwareBoolean);

