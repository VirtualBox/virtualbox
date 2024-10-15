/* $Id: tstRTSystemQueryFirmware.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTSystemQuerFirmware*.
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
#include <iprt/system.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/test.h>


int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTSystemQueryFirmware", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * RTSystemQueryFirmwareType
     */
    RTTestSub(hTest, "RTSystemQueryFirmwareType");
    RTSYSFWTYPE enmType = (RTSYSFWTYPE)-42;
    int rc = RTSystemQueryFirmwareType(&enmType);
    if (RT_SUCCESS(rc))
    {
        switch (enmType)
        {
            case RTSYSFWTYPE_BIOS:
            case RTSYSFWTYPE_UEFI:
            case RTSYSFWTYPE_UNKNOWN: /* Do not fail on not-implemented platforms. */
                RTTestPrintf(hTest, RTTESTLVL_INFO, "  Firmware type: %s\n", RTSystemFirmwareTypeName(enmType));
                break;
            default:
                RTTestFailed(hTest, "RTSystemQueryFirmwareType return invalid type: %d (%#x)", enmType, enmType);
                break;
        }
    }
    else if (rc != VERR_NOT_SUPPORTED)
        RTTestFailed(hTest, "RTSystemQueryFirmwareType failed: %Rrc", rc);

    /*
     * RTSystemQueryFirmwareBoolean
     */
    RTTestSub(hTest, "RTSystemQueryFirmwareBoolean");
    bool fValue;
    rc = RTSystemQueryFirmwareBoolean(RTSYSFWBOOL_SECURE_BOOT, &fValue);
    if (RT_SUCCESS(rc))
        RTTestPrintf(hTest, RTTESTLVL_INFO, "  Secure Boot:   %s\n", fValue ? "enabled" : "disabled");
    else if (rc != VERR_NOT_SUPPORTED && rc != VERR_SYS_UNSUPPORTED_FIRMWARE_PROPERTY)
        RTTestIFailed("RTSystemQueryFirmwareBoolean/RTSYSFWBOOL_SECURE_BOOT failed: %Rrc", rc);

    return RTTestSummaryAndDestroy(hTest);
}

