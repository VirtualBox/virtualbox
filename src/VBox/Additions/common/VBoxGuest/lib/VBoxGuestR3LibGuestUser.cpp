/* $Id: VBoxGuestR3LibGuestUser.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions,
 *                  guest user reporting / utility functions.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <iprt/assert.h>
#include <VBox/log.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "VBoxGuestR3LibInternal.h"


/**
 * Reports a state change of a specific guest user.
 *
 * @returns IPRT status value
 * @param   pszUser         Guest user name to report state for.
 * @param   pszDomain       Domain the guest user's account is bound to.
 * @param   enmState        Guest user state to report.
 * @param   puDetails       Pointer to state details. Optional.
 * @param   cbDetails       Size (in bytes) of state details. Pass 0
 *                          if puDetails is NULL.
 */
VBGLR3DECL(int) VbglR3GuestUserReportState(const char *pszUser, const char *pszDomain, VBoxGuestUserState enmState,
                                           uint8_t *puDetails, uint32_t cbDetails)
{
    AssertPtrReturn(pszUser, VERR_INVALID_POINTER);
    /* pszDomain is optional. */
    /* puDetails is optional. */
    AssertReturn(cbDetails == 0 || puDetails != NULL, VERR_INVALID_PARAMETER);
    AssertReturn(cbDetails < 16U*_1M, VERR_OUT_OF_RANGE);

    uint32_t cbBase   = sizeof(VMMDevReportGuestUserState);
    uint32_t cbUser   = (uint32_t)strlen(pszUser) + 1; /* Include terminating zero */
    uint32_t cbDomain = pszDomain ? (uint32_t)strlen(pszDomain) + 1 /* Ditto */ : 0;

    /* Allocate enough space for all fields. */
    uint32_t cbSize = cbBase
                    + cbUser
                    + cbDomain
                    + cbDetails;
    VMMDevReportGuestUserState *pReport = (VMMDevReportGuestUserState *)RTMemAllocZ(cbSize);
    if (!pReport)
        return VERR_NO_MEMORY;

    int rc = vmmdevInitRequest(&pReport->header, VMMDevReq_ReportGuestUserState);
    if (RT_SUCCESS(rc))
    {
        pReport->header.size      = cbSize;

        pReport->status.state     = enmState;
        pReport->status.cbUser    = cbUser;
        pReport->status.cbDomain  = cbDomain;
        pReport->status.cbDetails = cbDetails;

        /*
         * Note: cbOffDynamic contains the first dynamic array entry within
         *       VBoxGuestUserStatus.
         *       Therefore it's vital to *not* change the order of the struct members
         *       without altering this code. Don't try this at home.
         */
        uint32_t cbOffDynamic = RT_UOFFSETOF(VBoxGuestUserStatus, szUser);

        /* pDynamic marks the beginning for the dynamically allocated areas. */
        uint8_t *pDynamic = (uint8_t *)&pReport->status;
        pDynamic += cbOffDynamic;
        AssertPtr(pDynamic);

        memcpy(pDynamic, pszUser, cbUser);
        if (cbDomain)
            memcpy(pDynamic + cbUser, pszDomain, cbDomain);
        if (cbDetails)
            memcpy(pDynamic + cbUser + cbDomain, puDetails, cbDetails);

        rc = vbglR3GRPerform(&pReport->header);
    }

    RTMemFree(pReport);
    return rc;
}

