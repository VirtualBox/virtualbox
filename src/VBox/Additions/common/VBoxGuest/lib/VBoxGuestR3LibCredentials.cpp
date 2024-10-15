/* $Id: VBoxGuestR3LibCredentials.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, user credentials.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <VBox/log.h>

#include "VBoxGuestR3LibInternal.h"


/**
 * Checks whether user credentials are available to the guest or not.
 *
 * @returns IPRT status value; VINF_SUCCESS if credentials are available,
 *          VERR_NOT_FOUND if not. Otherwise an error is occurred.
 */
VBGLR3DECL(int) VbglR3CredentialsQueryAvailability(void)
{
    VMMDevCredentials Req;
    RT_ZERO(Req);
    vmmdevInitRequest((VMMDevRequestHeader*)&Req, VMMDevReq_QueryCredentials);
    Req.u32Flags |= VMMDEV_CREDENTIALS_QUERYPRESENCE;

    int rc = vbglR3GRPerform(&Req.header);
    if (RT_SUCCESS(rc))
    {
        if ((Req.u32Flags & VMMDEV_CREDENTIALS_PRESENT) == 0)
            rc = VERR_NOT_FOUND;
    }
    return rc;
}


/**
 * Retrieves and clears the user credentials for logging into the guest OS.
 *
 * @returns IPRT status value
 * @param   ppszUser        Receives pointer of allocated user name string.
 *                          The returned pointer must be freed using VbglR3CredentialsDestroy().
 * @param   ppszPassword    Receives pointer of allocated user password string.
 *                          The returned pointer must be freed using VbglR3CredentialsDestroy().
 * @param   ppszDomain      Receives pointer of allocated domain name string.
 *                          The returned pointer must be freed using VbglR3CredentialsDestroy().
 */
VBGLR3DECL(int) VbglR3CredentialsRetrieve(char **ppszUser, char **ppszPassword, char **ppszDomain)
{
    AssertPtrReturn(ppszUser, VERR_INVALID_POINTER);
    AssertPtrReturn(ppszPassword, VERR_INVALID_POINTER);
    AssertPtrReturn(ppszDomain, VERR_INVALID_POINTER);

    VMMDevCredentials Req;
    RT_ZERO(Req);
    vmmdevInitRequest((VMMDevRequestHeader*)&Req, VMMDevReq_QueryCredentials);
    Req.u32Flags |= VMMDEV_CREDENTIALS_READ | VMMDEV_CREDENTIALS_CLEAR;

    int rc = vbglR3GRPerform(&Req.header);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrDupEx(ppszUser, Req.szUserName);
        if (RT_SUCCESS(rc))
        {
            rc = RTStrDupEx(ppszPassword, Req.szPassword);
            if (RT_SUCCESS(rc))
            {
                rc = RTStrDupEx(ppszDomain, Req.szDomain);
                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;

                RTStrFree(*ppszPassword);
            }
            RTStrFree(*ppszUser);
        }
    }
    return rc;
}


/**
 * Retrieves and clears the user credentials for logging into the guest OS.
 * UTF-16 version.
 *
 * @returns IPRT status value
 * @param   ppwszUser       Receives pointer of allocated user name string.
 *                          The returned pointer must be freed using VbglR3CredentialsDestroyUtf16().
 * @param   ppwszPassword   Receives pointer of allocated user password string.
 *                          The returned pointer must be freed using VbglR3CredentialsDestroyUtf16().
 * @param   ppwszDomain     Receives pointer of allocated domain name string.
 *                          The returned pointer must be freed using VbglR3CredentialsDestroyUtf16().
 */
VBGLR3DECL(int) VbglR3CredentialsRetrieveUtf16(PRTUTF16 *ppwszUser, PRTUTF16 *ppwszPassword, PRTUTF16 *ppwszDomain)
{
    AssertPtrReturn(ppwszUser, VERR_INVALID_POINTER);
    AssertPtrReturn(ppwszPassword, VERR_INVALID_POINTER);
    AssertPtrReturn(ppwszDomain, VERR_INVALID_POINTER);

    char *pszUser, *pszPassword, *pszDomain;
    int rc = VbglR3CredentialsRetrieve(&pszUser, &pszPassword, &pszDomain);
    if (RT_SUCCESS(rc))
    {
        PRTUTF16 pwszUser = NULL;
        PRTUTF16 pwszPassword = NULL;
        PRTUTF16 pwszDomain = NULL;

        rc = RTStrToUtf16(pszUser, &pwszUser);
        if (RT_SUCCESS(rc))
        {
            rc = RTStrToUtf16(pszPassword, &pwszPassword);
            if (RT_SUCCESS(rc))
                rc = RTStrToUtf16(pszDomain, &pwszDomain);
        }

        if (RT_SUCCESS(rc))
        {
            *ppwszUser     = pwszUser;
            *ppwszPassword = pwszPassword;
            *ppwszDomain   = pwszDomain;
        }
        else
            VbglR3CredentialsDestroyUtf16(pwszUser, pwszPassword, pwszDomain, 3 /* Passes */);
        VbglR3CredentialsDestroy(pszUser, pszPassword, pszDomain, 3 /* Passes */);
    }

    return rc;
}


/**
 * Clears and frees the three strings.
 *
 * @param   pszUser        Receives pointer of the user name string to destroy.
 *                         Optional.
 * @param   pszPassword    Receives pointer of the password string to destroy.
 *                         Optional.
 * @param   pszDomain      Receives pointer of allocated domain name string.
 *                         Optional.
 * @param   cPasses        Number of wipe passes.  The more the better + slower.
 */
VBGLR3DECL(void) VbglR3CredentialsDestroy(char *pszUser, char *pszPassword, char *pszDomain, uint32_t cPasses)
{
    /* wipe first */
    if (pszUser)
        RTMemWipeThoroughly(pszUser,     strlen(pszUser) + 1,     cPasses);
    if (pszPassword)
        RTMemWipeThoroughly(pszPassword, strlen(pszPassword) + 1, cPasses);
    if (pszDomain)
        RTMemWipeThoroughly(pszDomain,   strlen(pszDomain) + 1,   cPasses);

    /* then free. */
    RTStrFree(pszUser);
    RTStrFree(pszPassword);
    RTStrFree(pszDomain);
}


/**
 * Clears and frees the three strings. UTF-16 version.
 *
 * @param   pwszUser       Receives pointer of the user name string to destroy.
 *                         Optional.
 * @param   pwszPassword   Receives pointer of the password string to destroy.
 *                         Optional.
 * @param   pwszDomain     Receives pointer of allocated domain name string.
 *                         Optional.
 * @param   cPasses        Number of wipe passes.  The more the better + slower.
 */
VBGLR3DECL(void) VbglR3CredentialsDestroyUtf16(PRTUTF16 pwszUser, PRTUTF16 pwszPassword, PRTUTF16 pwszDomain,
                                               uint32_t cPasses)
{
    /* wipe first */
    if (pwszUser)
        RTMemWipeThoroughly(pwszUser,     (RTUtf16Len(pwszUser)     + 1) * sizeof(RTUTF16), cPasses);
    if (pwszPassword)
        RTMemWipeThoroughly(pwszPassword, (RTUtf16Len(pwszPassword) + 1) * sizeof(RTUTF16), cPasses);
    if (pwszDomain)
        RTMemWipeThoroughly(pwszDomain,   (RTUtf16Len(pwszDomain)   + 1) * sizeof(RTUTF16), cPasses);

    /* then free. */
    RTUtf16Free(pwszUser);
    RTUtf16Free(pwszPassword);
    RTUtf16Free(pwszDomain);
}

