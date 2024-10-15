/* $Id: RTCrStoreCertAddWantedFromFishingExpedition.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Cryptographic (Certificate) Store, RTCrStoreCertAddFromFishingExpedition.
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

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/path.h>

#include "x509-internal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef RT_OS_WINDOWS
# define PREFIX_UNIXROOT  "${SystemDrive}/cygwin"
#elif defined(RT_OS_OS2)
# define PREFIX_UNIXROOT  "/@unixroot@"
#else
# define PREFIX_UNIXROOT
#endif


/**
 * Count the number of found certificates.
 *
 * @returns Number found.
 * @param   afFound             Indicator array.
 * @param   cWanted             Number of wanted certificates.
 */
DECLINLINE(size_t) rtCrStoreCountFound(bool const *afFound, size_t cWanted)
{
    size_t cFound = 0;
    while (cWanted-- > 0)
        if (afFound[cWanted])
            cFound++;
    return cFound;
}


RTDECL(int) RTCrStoreCertAddWantedFromFishingExpedition(RTCRSTORE hStore, uint32_t fFlags,
                                                        PCRTCRCERTWANTED paWanted, size_t cWanted,
                                                        bool *pafFound, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;
    int rc2;

    /*
     * Validate input.
     */
    AssertReturn(!(fFlags & ~(RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)), VERR_INVALID_FLAGS);
    fFlags |= RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR; /* force these! */
    AssertReturn(cWanted, VERR_NOT_FOUND);
    for (uint32_t i = 0; i < cWanted; i++)
    {
        AssertReturn(!paWanted[i].pszSubject || *paWanted[i].pszSubject, VERR_INVALID_PARAMETER);
        AssertReturn(   paWanted[i].pszSubject
                     || paWanted[i].fSha1Fingerprint
                     || paWanted[i].fSha512Fingerprint,
                     VERR_INVALID_PARAMETER);
    }

    /*
     * Make sure we've got a result array.
     */
    bool *pafFoundFree = NULL;
    if (!pafFound)
    {
        pafFound = pafFoundFree = (bool *)RTMemTmpAllocZ(sizeof(bool) * cWanted);
        AssertReturn(pafFound, VERR_NO_TMP_MEMORY);
    }

    /*
     * Search the user and system stores first.
     */
    bool fAllFound = false;
    RTCRSTORE hTmpStore;
    for (int iStoreId = RTCRSTOREID_INVALID + 1; iStoreId < RTCRSTOREID_END; iStoreId++)
    {
        rc2 = RTCrStoreCreateSnapshotById(&hTmpStore, (RTCRSTOREID)iStoreId, NULL);
        if (RT_SUCCESS(rc2))
        {
            rc2 = RTCrStoreCertAddWantedFromStore(hStore, fFlags, hTmpStore, paWanted, cWanted, pafFound);
            RTCrStoreRelease(hTmpStore);
            fAllFound = rc2 == VINF_SUCCESS;
            if (fAllFound)
                break;
        }
    }

    /*
     * Search alternative file based stores.
     */
    if (!fAllFound)
    {
        static const char * const s_apszFiles[] =
        {
            PREFIX_UNIXROOT "/usr/share/ca-certificates/trust-source/mozilla.neutral-trust.crt",
            PREFIX_UNIXROOT "/usr/share/ca-certificates/trust-source/mozilla.trust.crt",
            PREFIX_UNIXROOT "/usr/share/doc/mutt/samples/ca-bundle.crt",
            PREFIX_UNIXROOT "/usr/jdk/latest/jre/lib/security/cacerts",
            PREFIX_UNIXROOT "/usr/share/curl/curl-ca-bundle.crt",
#ifdef RT_OS_DARWIN
            "/opt/local/share/curl/curl-ca-bundle.crt",
            "/Library/Internet Plug-Ins/JavaAppletPlugin.plugin/Contents/Home/lib/security/cacerts",
            "/System/Library/Java/Support/CoreDeploy.bundle/Contents/Home/lib/security/cacerts",
            "/System/Library/Java/Support/CoreDeploy.bundle/Contents/JavaAppletPlugin.plugin/Contents/Home/lib/security/cacerts",
            "/System/Library/Java/Support/Deploy.bundle/Contents/Home/lib/security/cacerts",
            "/Applications/Xcode.app/Contents/Applications/Application Loader.app/Contents/MacOS/itms/java/lib/security/cacerts",
            "/Applications/Xcode.app/Contents/Applications/Application Loader.app/Contents/itms/java/lib/security/cacerts",
            "/Applications/Xcode-beta.app/Contents/Applications/Application Loader.app/Contents/itms/java/lib/security/cacerts",
            "/System/Library/Java/JavaVirtualMachines/*/Contents/Home/lib/security/cacerts",
#endif
#ifdef RT_OS_LINUX
            PREFIX_UNIXROOT "/etc/ssl/certs/java/cacerts",
            PREFIX_UNIXROOT "/usr/lib/j*/*/jre/lib/security/cacerts",
            PREFIX_UNIXROOT "/opt/*/jre/lib/security/cacerts",
#endif
#ifdef RT_OS_SOLARIS
            PREFIX_UNIXROOT "/usr/java/jre/lib/security/cacerts",
            PREFIX_UNIXROOT "/usr/jdk/instances/*/jre/lib/security/cacerts",
#endif
#ifdef RT_OS_WINDOWS
            "${AllProgramFiles}/Git/bin/curl-ca-bundle.crt",
            "${AllProgramFiles}/Mercurial/hgrc.d/cacert.pem",
            "${AllProgramFiles}/Java/jre*/lib/security/cacerts",
            "${AllProgramFiles}/Java/jdk*/jre/lib/security/cacerts",
            "${AllProgramFiles}/HexChat/cert.pem",
            "${SystemDrive}/BitNami/*/git/bin/curl-ca-bundle.crt",
            "${SystemDrive}/BitNami/*/heroku/data/cacert.pem",
            "${SystemDrive}/BitNami/*/heroku/vendor/gems/excon*/data/cacert.pem",
            "${SystemDrive}/BitNami/*/php/PEAR/AWSSDKforPHP/lib/requstcore/cacert.pem",
#endif
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_apszFiles) && !fAllFound; i++)
        {
            PCRTPATHGLOBENTRY pResultHead;
            rc2 = RTPathGlob(s_apszFiles[i], RTPATHGLOB_F_NO_DIRS, &pResultHead, NULL);
            if (RT_SUCCESS(rc2))
            {
                for (PCRTPATHGLOBENTRY pCur = pResultHead; pCur; pCur = pCur->pNext)
                {
                    rc2 = RTCrStoreCertAddWantedFromFile(hStore, fFlags, pCur->szPath, paWanted, cWanted, pafFound, pErrInfo);
                    fAllFound = rc2 == VINF_SUCCESS;
                    if (fAllFound)
                        break;
                }
                RTPathGlobFree(pResultHead);
            }
        }
    }

    /*
     * Search alternative directory based stores.
     */
    if (!fAllFound)
    {
        static const char * const s_apszFiles[] =
        {
            PREFIX_UNIXROOT "/usr/share/ca-certificates/mozilla/",
#ifdef RT_OS_DARWIN
            "/System/Library/Frameworks/Ruby.framework/Versions/2.0/usr/lib/ruby/2.0.0/rubygems/ssl_certs/",
#endif
#ifdef RT_OS_SOLARIS
            "/etc/certs/",
            "/etc/crypto/certs/",
#endif
#ifdef RT_OS_WINDOWS
            "${AllProgramFiles}/Git/ssl/certs/",
            "${AllProgramFiles}/Git/ssl/certs/expired/",
            "${AllProgramFiles}/Common Files/Apple/Internet Services/security.resources/roots/",
            "${AllProgramFiles}/Raptr/ca-certs/",
            "${SystemDrive}/Bitname/*/git/ssl/certs/",
            "${SystemDrive}/Bitnami/*/git/ssl/certs/expired/",
#endif
        };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_apszFiles) && !fAllFound; i++)
        {
            PCRTPATHGLOBENTRY pResultHead;
            rc2 = RTPathGlob(s_apszFiles[i], RTPATHGLOB_F_ONLY_DIRS, &pResultHead, NULL);
            if (RT_SUCCESS(rc2))
            {
                for (PCRTPATHGLOBENTRY pCur = pResultHead; pCur; pCur = pCur->pNext)
                {
                    rc2 = RTCrStoreCertAddWantedFromDir(hStore, fFlags, pCur->szPath, NULL /*paSuffixes*/, 0 /*cSuffixes*/,
                                                        paWanted, cWanted, pafFound, pErrInfo);
                    fAllFound = rc2 == VINF_SUCCESS;
                    if (fAllFound)
                        break;
                }
                RTPathGlobFree(pResultHead);
            }
        }
    }

    /*
     * If all found, return VINF_SUCCESS, otherwise warn that we didn't find everything.
     */
    if (RT_SUCCESS(rc))
    {
        size_t cFound = rtCrStoreCountFound(pafFound, cWanted);
        Assert(cFound == cWanted || !fAllFound);
        if (cFound == cWanted)
            rc = VINF_SUCCESS;
        else if (cFound > 0)
            rc = VWRN_NOT_FOUND;
        else
            rc = VERR_NOT_FOUND;
    }

    if (pafFoundFree)
        RTMemTmpFree(pafFoundFree);
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCertAddWantedFromFishingExpedition);

