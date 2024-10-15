/* $Id: RTCrStoreCreateSnapshotById-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Generic RTCrStoreCreateSnapshotById implementation.
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
#include <iprt/crypto/store.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/dir.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Unix root prefix. */
#ifdef RT_OS_OS2
# define UNIX_ROOT "/@unixroot@"
#elif defined(RT_OS_WINDOWS)
# define UNIX_ROOT "C:/cygwin"
#else
# define UNIX_ROOT
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** System PEM files worth looking at.
 * @remarks Several of these could be symlinks to one of the others.
 */
static const char *g_apszSystemPemFiles[] =
{
    UNIX_ROOT "/etc/ssl/certs/ca-certificates.crt",
    UNIX_ROOT "/etc/ssl/cert.pem",
    UNIX_ROOT "/etc/ca-certificates/extracted/tls-ca-bundle.pem",       /* Arch linux (ca 2015-08-xx) */
    UNIX_ROOT "/etc/ca-certificates/extracted/email-ca-bundle.pem",
    UNIX_ROOT "/etc/ca-certificates/extracted/objsign-ca-bundle.pem",
    UNIX_ROOT "/etc/ca-certificates/extracted/ca-bundle.trust.crt",
    UNIX_ROOT "/etc/ca-certificates/extracted/ca-bundle.trust.crt",
    UNIX_ROOT "/etc/pki/tls/certs/ca-bundle.crt",                       /* Oracle Linux 5 */
    UNIX_ROOT "/etc/pki/tls/cert.pem",
    UNIX_ROOT "/etc/certs/ca-certificates.crt",                         /* Solaris 11 */
    UNIX_ROOT "/etc/curl/curlCA",
};

/**
 * System directories containing lots of pem/crt files.
 */
static const char *g_apszSystemPemDirs[] =
{
    UNIX_ROOT "/etc/openssl/certs/",
    UNIX_ROOT "/etc/ssl/certs/",
    UNIX_ROOT "/etc/ca-certificates/extracted/cadir/",
    UNIX_ROOT "/etc/certs/CA/",                                         /* Solaris 11 */
};


RTDECL(int) RTCrStoreCreateSnapshotById(PRTCRSTORE phStore, RTCRSTOREID enmStoreId, PRTERRINFO pErrInfo)
{
    AssertReturn(enmStoreId > RTCRSTOREID_INVALID && enmStoreId < RTCRSTOREID_END, VERR_INVALID_PARAMETER);

    /*
     * Create an empty in-memory store.
     */
    RTCRSTORE hStore;
    uint32_t cExpected = enmStoreId == RTCRSTOREID_SYSTEM_TRUSTED_CAS_AND_CERTIFICATES ? 256 : 0;
    int rc = RTCrStoreCreateInMem(&hStore, cExpected);
    if (RT_SUCCESS(rc))
    {
        *phStore = hStore;

        /*
         * Add system certificates if part of the given store ID.
         */
        bool fFound = false;
        rc = VINF_SUCCESS;
        if (enmStoreId == RTCRSTOREID_SYSTEM_TRUSTED_CAS_AND_CERTIFICATES)
        {
            for (uint32_t i = 0; i < RT_ELEMENTS(g_apszSystemPemFiles); i++)
                if (RTFileExists(g_apszSystemPemFiles[i]))
                {
                    fFound = true;
                    int rc2 = RTCrStoreCertAddFromFile(hStore,
                                                       RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR,
                                                       g_apszSystemPemFiles[i], pErrInfo);
                    if (RT_FAILURE(rc2))
                        rc = -rc2;
                }

            /*
             * If we didn't find any of the certificate collection files, go hunting
             * for directories containing PEM/CRT files with single certificates.
             */
            if (!fFound)
                for (uint32_t i = 0; i < RT_ELEMENTS(g_apszSystemPemDirs); i++)
                    if (RTDirExists(g_apszSystemPemDirs[i]))
                    {
                        static RTSTRTUPLE const s_aSuffixes[] =
                        {
                            { RT_STR_TUPLE(".crt") },
                            { RT_STR_TUPLE(".pem") },
                            { RT_STR_TUPLE(".PEM") },
                            { RT_STR_TUPLE(".CRT") },
                        };
                        fFound = true;
                        int rc2 = RTCrStoreCertAddFromDir(hStore,
                                                          RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR,
                                                          g_apszSystemPemDirs[i], &s_aSuffixes[0], RT_ELEMENTS(s_aSuffixes),
                                                          pErrInfo);
                        if (RT_FAILURE(rc2))
                            rc = -rc2;
                    }
        }
    }
    else
        RTErrInfoAdd(pErrInfo, rc, "  RTCrStoreCreateInMem failed");
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCreateSnapshotById);

