/* $Id: HashedPw.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - Password Hashing
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "HashedPw.h"

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/sha.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * The prefix of a hashed password.
 */
static const char s_szHashedPwPrefix[] = "#SHA-512#";


/**
 * Checks if the password is a hashed one or not.
 *
 * Empty password are not considered hashed.
 *
 * @returns true if hashed, false if not.
 * @param   a_pstrPassword  Password to inspect.
 */
bool VBoxIsPasswordHashed(RTCString const *a_pstrPassword)
{
    /* prefix */
    if (!a_pstrPassword->startsWith(s_szHashedPwPrefix))
        return false;

    /* salt (optional) */
    const char *pszSalt    = a_pstrPassword->c_str() + sizeof(s_szHashedPwPrefix) - 1;
    const char *pszSaltEnd = strchr(pszSalt, '#');
    if (!pszSaltEnd)
        return false;
    while (pszSalt != pszSaltEnd)
    {
        if (!RT_C_IS_XDIGIT(*pszSalt))
            return false;
        pszSalt++;
    }

    /* hash */
    uint8_t abHash[RTSHA512_HASH_SIZE];
    int vrc = RTSha512FromString(pszSaltEnd + 1, abHash);
    return RT_SUCCESS(vrc);
}


/**
 * Hashes a plain text password.
 *
 * @param   a_pstrPassword      Plain text password to hash.  This is both
 *                              input and output.
 */
void VBoxHashPassword(RTCString *a_pstrPassword)
{
    AssertReturnVoid(!VBoxIsPasswordHashed(a_pstrPassword));

    char szHashedPw[sizeof(s_szHashedPwPrefix) + 1 + RTSHA512_DIGEST_LEN];
    if (a_pstrPassword->isEmpty())
        szHashedPw[0] = '\0';
    else
    {
        /* prefix */
        char *pszHashedPw = szHashedPw;
        strcpy(pszHashedPw, s_szHashedPwPrefix);
        pszHashedPw += sizeof(s_szHashedPwPrefix) - 1;

        /* salt */
        *pszHashedPw++ = '#'; /* no salt yet */

        /* hash */
        uint8_t abHash[RTSHA512_HASH_SIZE];
        RTSha512(a_pstrPassword->c_str(), a_pstrPassword->length(), abHash);
        int vrc = RTSha512ToString(abHash, pszHashedPw, sizeof(szHashedPw) - (size_t)(pszHashedPw - &szHashedPw[0]));
        AssertReleaseRC(vrc);
    }

    *a_pstrPassword = szHashedPw;
}

