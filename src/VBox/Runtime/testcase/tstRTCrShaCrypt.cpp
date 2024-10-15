/* $Id: tstRTCrShaCrypt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - SHA-crypt 256 / 512.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#include <iprt/crypto/shacrypt.h>

#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Test data                                                                                                                    *
*********************************************************************************************************************************/

/** Digest type. */
typedef enum TSTDIGESTTYPE
{
    TSTDIGESTTYPE_RANDOM = 0,
    TSTDIGESTTYPE_SHA256,
    TSTDIGESTTYPE_SHA512,
    TSTDIGESTTYPE_END
} TSTDIGESTTYPE;

static struct
{
    /** Cleartext password. */
    const char     *pszPassword;
    /** Salt to use. If NULL, a random salt will be used. */
    const char     *pszSalt;
    /** Number of rounds to use. If set to UINT32_MAX, random rounds will be used. */
    uint32_t        cRounds;
    /** Digest type to use. If set to 0, a random digest type will be used. */
    TSTDIGESTTYPE   enmType;
    /** Overall test outcome. */
    int             rc;
    /** Expected result as a string. Can be NULL to skip testing this. */
    const char     *pszResult;
} g_aTests[] =
{
    /*
     * Invalid stuff.
     */
    {   /* No salt */
        /* .pszPassword = */    "changeme",
        /* .pszSalt = */        "",
        /* .cRounds = */        RT_SHACRYPT_ROUNDS_DEFAULT,
        /* .enmType = */        TSTDIGESTTYPE_RANDOM,
        /* .rc = */             VERR_BUFFER_UNDERFLOW,
        /* .pszResult = */      ""
    },
    {   /* Salt too short */
        /* .pszPassword = */    "changeme",
        /* .pszSalt = */        "1234",
        /* .cRounds = */        RT_SHACRYPT_ROUNDS_DEFAULT,
        /* .enmType = */        TSTDIGESTTYPE_RANDOM,
        /* .rc = */             VERR_BUFFER_UNDERFLOW,
        /* .pszResult = */      ""
    },
    {   /* Salt too long */
        /* .pszPassword = */    "changeme",
        /* .pszSalt = */        "12341234123412341234123412341234",
        /* .cRounds = */        RT_SHACRYPT_ROUNDS_DEFAULT,
        /* .enmType = */        TSTDIGESTTYPE_RANDOM,
        /* .rc = */             VERR_TOO_MUCH_DATA,
        /* .pszResult = */      ""
    },
    {   /* Invalid rounds */
        /* .pszPassword = */    "changeme",
        /* .pszSalt = */        "0123456789abcdef",
        /* .cRounds = */        42,
        /* .enmType = */        TSTDIGESTTYPE_RANDOM,
        /* .rc = */             VERR_OUT_OF_RANGE,
        /* .pszResult = */      ""
    },
    /*
     * Valid stuff.
     */
    {
        /* .pszPassword = */    "changeme",
        /* .pszSalt = */        "foo12345",
        /* .cRounds = */        RT_SHACRYPT_ROUNDS_DEFAULT,
        /* .enmType = */        TSTDIGESTTYPE_SHA256,
        /* .rc = */             VINF_SUCCESS,
        /* .pszResult = */      "$5$foo12345$KnOIYJmTgZ744xCqNLl1I9qF.Xq47vHTH.yVStiAMZD"
    },
    {
        /* .pszPassword = */    "really-secure-semi-long-password",
        /* .pszSalt = */        "5288d4774fd14289",
        /* .cRounds = */        999663,
        /* .enmType = */        TSTDIGESTTYPE_SHA256,
        /* .rc = */             VINF_SUCCESS,
        /* .pszResult = */      "$5$rounds=999663$5288d4774fd14289$KkMAAPiAFgo9bFHnd79MCnBmMeJXlx02ra4e/20WoC4"
    },
    {
        /* .pszPassword = */    "changeme",
        /* .pszSalt = */        "foo12345",
        /* .cRounds = */        RT_SHACRYPT_ROUNDS_DEFAULT,
        /* .enmType = */        TSTDIGESTTYPE_SHA512,
        /* .rc = */             VINF_SUCCESS,
        /* .pszResult = */      "$6$foo12345$cb11CtCP6YgoZr8SyNoD2TAdOY4OmTzA6kfDgju5JrNVzgeCBU1ALbJHVlEuSImPKAoSnT53N7k7BqzjYRRPk/"
    },
    {
        /* .pszPassword = */    "really-really-really-really-really-long-and-still-insecure-password",
        /* .pszSalt = */        "$6$rounds=384836$AbCdEfGhijKLM",
        /* .cRounds = */        RT_SHACRYPT_ROUNDS_DEFAULT,
        /* .enmType = */        TSTDIGESTTYPE_SHA512,
        /* .rc = */             VINF_SUCCESS,
        /* .pszResult = */      "$6$rounds=384836$AbCdEfGhijKLM$pJM6Ugvo4IiVCd8KTmDNIvShHX.G6p0SC/FnBNBAf9TBm1Td/s9HsVu.iWiEBxnEDWiB5zn/NBi6VTqhCP7Ii0"
    },
    {
        /* .pszPassword = */    "이것은 테스트입니다", /* "This is a test" in Korean */
        /* .pszSalt = */        "foo12345",
        /* .cRounds = */        RT_SHACRYPT_ROUNDS_DEFAULT,
        /* .enmType = */        TSTDIGESTTYPE_SHA256,
        /* .rc = */             VINF_SUCCESS,
        /* .pszResult = */      "$5$foo12345$7fumMsJKgCGipks2nNPi185ANXwfTf9Ilz70J4wKqe1"
    },
    {
        /* .pszPassword = */    "이것은 테스트입니다", /* "This is a test" in Korean */
        /* .pszSalt = */        "foo12345",
        /* .cRounds = */        RT_SHACRYPT_ROUNDS_DEFAULT,
        /* .enmType = */        TSTDIGESTTYPE_SHA512,
        /* .rc = */             VINF_SUCCESS,
        /* .pszResult = */      "$6$foo12345$IWlIz4tyl39ETRpKlQ.R42tdeB2Ax9gz9sazAynilHDFm0zXUdsrm4nXzdlSd5jJhvwV7EPSc./2pBNoL1PIw1"
    },
    {
        /* .pszPassword = */    "changeme",
        /* .pszSalt = */        "foo12345",
        /* .cRounds = */        40000,
        /* .enmType = */        TSTDIGESTTYPE_RANDOM,
        /* .rc = */             VINF_SUCCESS,
        /* .pszResult = */      NULL
    },
    {
        /* .pszPassword = */    "changeme",
        /* .pszSalt = */        NULL,
        /* .cRounds = */        UINT32_MAX,
        /* .enmType = */        TSTDIGESTTYPE_RANDOM,
        /* .rc = */             VINF_SUCCESS,
        /* .pszResult = */      NULL
    },
    {
        /* .pszPassword = */    "changeme",
        /* .pszSalt = */        NULL,
        /* .cRounds = */        RT_SHACRYPT_ROUNDS_DEFAULT,
        /* .enmType = */        TSTDIGESTTYPE_RANDOM,
        /* .rc = */             VINF_SUCCESS,
        /* .pszResult = */      NULL
    }
};


static void test1(RTTEST hTest)
{
    RTTestDisableAssertions(hTest);

    char *pszGuardedSalt = NULL;
    int rc = RTTestGuardedAlloc(hTest, RT_SHACRYPT_SALT_MAX_LEN + 1, 1, false, (void **)&pszGuardedSalt);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    char *pszGuardedResult = NULL;
    rc = RTTestGuardedAlloc(hTest, RT_SHACRYPT_512_MAX_SIZE, 1, false, (void **)&pszGuardedResult);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    for (uint32_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        const char *pszSalt = g_aTests[i].pszSalt;
        if (!pszSalt)
        {
            uint32_t const cchSalt = RTRandU32Ex(RT_SHACRYPT_SALT_MIN_LEN, RT_SHACRYPT_SALT_MAX_LEN);
            rc = RTCrShaCryptGenerateSalt(&pszGuardedSalt[RT_SHACRYPT_SALT_MAX_LEN - cchSalt], cchSalt);
            RTTEST_CHECK_RC_OK(hTest, rc);
            pszSalt = &pszGuardedSalt[RT_SHACRYPT_SALT_MAX_LEN - cchSalt];
        }

        uint32_t cRounds = g_aTests[i].cRounds;
        if (cRounds == UINT32_MAX)
            cRounds = RTRandU32Ex(RT_SHACRYPT_ROUNDS_MIN, _512K /* Save a bit of time on the testboxes */);

        TSTDIGESTTYPE enmType = g_aTests[i].enmType;
        if (enmType == TSTDIGESTTYPE_RANDOM)
            enmType = (TSTDIGESTTYPE)RTRandU32Ex(TSTDIGESTTYPE_RANDOM + 1, TSTDIGESTTYPE_END - 1);

        uint8_t abDigest[RTSHA512_HASH_SIZE];
        switch (enmType)
        {
            case TSTDIGESTTYPE_SHA256:
                rc = RTCrShaCrypt256Ex(g_aTests[i].pszPassword, pszSalt, cRounds, abDigest);
                break;

            case TSTDIGESTTYPE_SHA512:
                rc = RTCrShaCrypt512Ex(g_aTests[i].pszPassword, pszSalt, cRounds, abDigest);
                break;

            default:
                AssertFailedStmt(rc = VERR_INTERNAL_ERROR);
                break;
        }
        if (rc != g_aTests[i].rc)
            RTTestIFailed("#%u: RTCrShaCryptXxxEx(,%s,%#x,) returns %Rrc, expected %Rrc",
                          i, pszSalt, cRounds, rc, g_aTests[i].rc);

        if (RT_SUCCESS(rc))
        {
            RT_BZERO(pszGuardedResult, RT_SHACRYPT_512_MAX_SIZE);
            switch (enmType)
            {
                case TSTDIGESTTYPE_SHA256:
                    rc = RTCrShaCrypt256ToString(abDigest, pszSalt, cRounds, pszGuardedResult, RT_SHACRYPT_512_MAX_SIZE);
                    break;

                case TSTDIGESTTYPE_SHA512:
                    rc = RTCrShaCrypt512ToString(abDigest, pszSalt, cRounds, pszGuardedResult, RT_SHACRYPT_512_MAX_SIZE);
                    break;

                default:
                    AssertFailedStmt(rc = VERR_INTERNAL_ERROR);
                    break;
            }
            if (RT_SUCCESS(rc))
            {
                if (g_aTests[i].pszResult && strcmp(pszGuardedResult, g_aTests[i].pszResult))
                    RTTestIFailed("#%u: RTCrShaCryptXxxString returns '%s', expected '%s'",
                                  i, pszGuardedResult, g_aTests[i].pszResult);

                /*
                 * Do a verification round, where we pass the above result in as the salt.
                 */
                char szResult2[RT_SHACRYPT_512_MAX_SIZE] = {0};
                switch (enmType)
                {
                    case TSTDIGESTTYPE_SHA256:
                        rc = RTCrShaCrypt256(g_aTests[i].pszPassword, pszGuardedResult, cRounds, szResult2, sizeof(szResult2));
                        break;

                    case TSTDIGESTTYPE_SHA512:
                        rc = RTCrShaCrypt512(g_aTests[i].pszPassword, pszGuardedResult, cRounds, szResult2, sizeof(szResult2));
                        break;

                    default:
                        AssertFailed();
                        break;
                }

                if (strcmp(szResult2, pszGuardedResult))
                    RTTestIFailed("#%u (result as salt): Returns '%s', expected '%s'", i, szResult2, pszGuardedResult);

                /*
                 * Push the buffer limit on the string formatter.
                 */
                size_t const cchNeeded = strlen(szResult2);
                size_t const cbBufMax  = RT_MIN(RT_SHACRYPT_512_MAX_SIZE, cchNeeded + 32);
                for (size_t cbBuf = 0; cbBuf <= cbBufMax; cbBuf++)
                {
                    char * const pszBuf = &pszGuardedResult[RT_SHACRYPT_512_MAX_SIZE - cbBuf];
                    switch (enmType)
                    {
                        case TSTDIGESTTYPE_SHA256:
                            rc = RTCrShaCrypt256ToString(abDigest, pszSalt, cRounds, pszBuf, cbBuf);
                            break;
                        case TSTDIGESTTYPE_SHA512:
                            rc = RTCrShaCrypt512ToString(abDigest, pszSalt, cRounds, pszBuf, cbBuf);
                            break;
                        default:
                            AssertFailedStmt(rc = VERR_INTERNAL_ERROR);
                            break;
                    }
                    int rcExpect = cbBuf <= cchNeeded ? VERR_BUFFER_OVERFLOW : VINF_SUCCESS;
                    if (rc != rcExpect)
                        RTTestIFailed("#%u: cbBuf=%#zx cchNeeded=%#zx: %Rrc, expected %Rrc", i, cbBuf, cchNeeded, rc, rcExpect);
                    if (cbBuf > cchNeeded && memcmp(pszBuf, szResult2, cchNeeded + 1))
                        RTTestIFailed("#%u: cbBuf=%#zx cchNeeded=%#zx: '%s', expected '%s'",
                                      i, cbBuf, cchNeeded, pszBuf, szResult2);
                }
            }
            else
                RTTestIFailed("#%u: RTCrShaCryptXxxString returns %Rrc", i, rc);
        }
    }

    RTTestRestoreAssertions(hTest);
}


int main()
{
    /*
     * Init.
     */
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTCrShaCrypt", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    test1(hTest);

    /*
     * Done.
     */
    return RTTestSummaryAndDestroy(hTest);
}

