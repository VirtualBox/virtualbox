/* $Id: RTCrStoreCreateSnapshotOfUserAndSystemTrustedCAsAndCerts.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Cryptographic (Certificate) Store, RTCrStoreCreateSnapshotOfUserAndSystemTrustedCAsAndCerts.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <iprt/crypto/store.h>

#include <iprt/errcore.h>




RTR3DECL(int) RTCrStoreCreateSnapshotOfUserAndSystemTrustedCAsAndCerts(PRTCRSTORE phStore, PRTERRINFO pErrInfo)
{
    /*
     * Start by a snapshot of the user store.
     */
    RTCRSTORE hDstStore;
    int rc = RTCrStoreCreateSnapshotById(&hDstStore, RTCRSTOREID_USER_TRUSTED_CAS_AND_CERTIFICATES, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Snapshot the system store and add that to the user snapshot.  Ignore errors.
         */
        RTCRSTORE hSrcStore;
        rc = RTCrStoreCreateSnapshotById(&hSrcStore, RTCRSTOREID_SYSTEM_TRUSTED_CAS_AND_CERTIFICATES, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            rc = RTCrStoreCertAddFromStore(hDstStore, RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR,
                                           hSrcStore);
            RTCrStoreRelease(hSrcStore);
        }
        if (RT_SUCCESS(rc))
        {
            *phStore = hDstStore;
            return rc;
        }

        /*
         * If we've got something in the store, return anyway despite of the error condition.
         */
        if (rc != VERR_NO_MEMORY && RTCrStoreCertCount(hDstStore) > 0)
        {
            *phStore = hDstStore;
            return -rc;
        }

        RTCrStoreRelease(hDstStore);
    }
    *phStore = NIL_RTCRSTORE;
    return rc;
}

