/* $Id: RTCrStoreCreateSnapshotById-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTCrStoreCreateSnapshotById, Windows.
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
#include <iprt/once.h>
#include <iprt/ldr.h>

#include <iprt/win/windows.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef HCERTSTORE (WINAPI *PFNCERTOPENSTORE)(PCSTR pszStoreProvider, DWORD dwEncodingType, HCRYPTPROV_LEGACY hCryptProv,
                                              DWORD dwFlags, const void *pvParam);
typedef BOOL (WINAPI *PFNCERTCLOSESTORE)(HCERTSTORE hCertStore, DWORD dwFlags);
typedef PCCERT_CONTEXT (WINAPI *PFNCERTENUMCERTIFICATESINSTORE)(HCERTSTORE hCertStore, PCCERT_CONTEXT pPrevCertContext);



static int rtCrStoreAddCertsFromNative(RTCRSTORE hStore, DWORD fStore, PCRTUTF16 pwszStoreName,
                                       PFNCERTOPENSTORE pfnOpenStore, PFNCERTCLOSESTORE pfnCloseStore,
                                       PFNCERTENUMCERTIFICATESINSTORE pfnEnumCerts, int rc, PRTERRINFO pErrInfo)
{
    DWORD fOpenStore = CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG;
    HCERTSTORE hNativeStore = pfnOpenStore(CERT_STORE_PROV_SYSTEM_W, PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
                                           NULL /* hCryptProv = default */, fStore | fOpenStore, pwszStoreName);
    if (hNativeStore)
    {
        PCCERT_CONTEXT pCurCtx = NULL;
        while ((pCurCtx = pfnEnumCerts(hNativeStore, pCurCtx)) != NULL)
        {
            if (pCurCtx->dwCertEncodingType & X509_ASN_ENCODING)
            {
                RTERRINFOSTATIC StaticErrInfo;
                RTASN1CURSORPRIMARY PrimaryCursor;
                RTAsn1CursorInitPrimary(&PrimaryCursor, pCurCtx->pbCertEncoded, pCurCtx->cbCertEncoded,
                                        RTErrInfoInitStatic(&StaticErrInfo),
                                        &g_RTAsn1DefaultAllocator, RTASN1CURSOR_FLAGS_DER, "CurCtx");
                RTCRX509CERTIFICATE MyCert;
                int rc2 = RTCrX509Certificate_DecodeAsn1(&PrimaryCursor.Cursor, 0, &MyCert, "Cert");
                if (RT_SUCCESS(rc2))
                {
                    rc2 = RTCrStoreCertAddEncoded(hStore, RTCRCERTCTX_F_ENC_X509_DER | RTCRCERTCTX_F_ADD_IF_NOT_FOUND,
                                                  pCurCtx->pbCertEncoded, pCurCtx->cbCertEncoded,
                                                  RTErrInfoInitStatic(&StaticErrInfo));
                    RTCrX509Certificate_Delete(&MyCert);
                }
                if (RT_FAILURE(rc2))
                {
                    if (RTErrInfoIsSet(&StaticErrInfo.Core))
                        RTErrInfoAddF(pErrInfo, -rc2, "  %s", StaticErrInfo.Core.pszMsg);
                    else
                        RTErrInfoAddF(pErrInfo, -rc2, "  %Rrc adding cert", rc2);
                    rc = -rc2;
                }
            }
        }
        pfnCloseStore(hNativeStore, CERT_CLOSE_STORE_CHECK_FLAG);
    }
    else
    {
        DWORD uLastErr = GetLastError();
        if (uLastErr != ERROR_FILE_NOT_FOUND)
            rc = RTErrInfoAddF(pErrInfo, -RTErrConvertFromWin32(uLastErr),
                               " CertOpenStore(%#x,'%ls') failed: %u", fStore, pwszStoreName);
    }
    return rc;
}



RTDECL(int) RTCrStoreCreateSnapshotById(PRTCRSTORE phStore, RTCRSTOREID enmStoreId, PRTERRINFO pErrInfo)
{
    AssertReturn(enmStoreId > RTCRSTOREID_INVALID && enmStoreId < RTCRSTOREID_END, VERR_INVALID_PARAMETER);

    /*
     * Create an empty in-memory store.
     */
    RTCRSTORE hStore;
    int rc = RTCrStoreCreateInMem(&hStore, 128);
    if (RT_SUCCESS(rc))
    {
        *phStore = hStore;

        /*
         * Resolve the APIs we need to do this job.
         */
        RTLDRMOD hLdrMod;
        int rc2 = RTLdrLoadSystem("crypt32.dll", false /*NoUnload*/, &hLdrMod);
        if (RT_SUCCESS(rc2))
        {
            PFNCERTOPENSTORE                pfnOpenStore  = NULL;
            rc2 = RTLdrGetSymbol(hLdrMod, "CertOpenStore", (void **)&pfnOpenStore);

            PFNCERTCLOSESTORE               pfnCloseStore = NULL;
            if (RT_SUCCESS(rc2))
                rc2 = RTLdrGetSymbol(hLdrMod, "CertCloseStore", (void **)&pfnCloseStore);

            PFNCERTENUMCERTIFICATESINSTORE  pfnEnumCerts  = NULL;
            if (RT_SUCCESS(rc2))
                rc2 = RTLdrGetSymbol(hLdrMod, "CertEnumCertificatesInStore", (void **)&pfnEnumCerts);
            if (RT_SUCCESS(rc2))
            {
                /*
                 * Do the work.
                 */
                DWORD fStore = CERT_SYSTEM_STORE_CURRENT_USER;
                switch (enmStoreId)
                {
                    case RTCRSTOREID_SYSTEM_TRUSTED_CAS_AND_CERTIFICATES:
                    case RTCRSTOREID_SYSTEM_INTERMEDIATE_CAS:
                        fStore = CERT_SYSTEM_STORE_LOCAL_MACHINE;
                        RT_FALL_THRU();
                    case RTCRSTOREID_USER_TRUSTED_CAS_AND_CERTIFICATES:
                    case RTCRSTOREID_USER_INTERMEDIATE_CAS:
                    {
                        /** @todo CA and MY in s_apwszRootStores are _very_ questionable!!! However,
                         * curl may need them  to work correct and it doesn't seem to have any
                         * intermediate ca file. :/ */
                        static PCRTUTF16 const s_apwszRootStores[] = { L"AuthRoot", L"CA", L"MY", L"Root" };
                        static PCRTUTF16 const s_apwszIntermediateStores[] = { L"CA", L"MY" };
                        PCRTUTF16 const *papwszStores = s_apwszRootStores;
                        uint32_t         cStores      = RT_ELEMENTS(s_apwszRootStores);
                        if (enmStoreId == RTCRSTOREID_USER_INTERMEDIATE_CAS || enmStoreId == RTCRSTOREID_SYSTEM_INTERMEDIATE_CAS)
                        {
                            papwszStores = s_apwszIntermediateStores;
                            cStores      = RT_ELEMENTS(s_apwszIntermediateStores);
                        }

                        for (uint32_t i = 0; i < cStores; i++)
                            rc = rtCrStoreAddCertsFromNative(hStore, fStore, papwszStores[i], pfnOpenStore, pfnCloseStore,
                                                             pfnEnumCerts, rc, pErrInfo);
                        break;
                    }

                    default:
                        AssertFailed(); /* implement me */
                }
            }
            else
                rc = RTErrInfoSetF(pErrInfo, -rc2, "Error resolving crypt32.dll APIs");
            RTLdrClose(hLdrMod);
        }
        else
            rc = RTErrInfoSetF(pErrInfo, -rc2, "Error loading crypt32.dll");
    }
    else
        RTErrInfoSet(pErrInfo, rc, "RTCrStoreCreateInMem failed");
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCreateSnapshotById);

