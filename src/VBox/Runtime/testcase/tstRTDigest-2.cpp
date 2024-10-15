/* $Id: tstRTDigest-2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Checksums and Digests.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include <iprt/crypto/digest.h>
#include <iprt/md2.h>
#include <iprt/md4.h>
#include <iprt/md5.h>
#include <iprt/sha.h>

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct TESTRTDIGEST
{
    /** Pointer to the input. */
    void const *pvInput;
    /** The size of the input. */
    size_t      cbInput;
    /** The expected digest. */
    const char *pszDigest;
    /** Clue what this is. Can be NULL. */
    const char *pszTest;
} TESTRTDIGEST;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#include "72kb-random.h"


#define CHECK_STRING(a_pszActual, a_pszExpected) \
    do { \
        if (strcmp(a_pszActual, a_pszExpected)) \
            RTTestIFailed("line %u: Expected %s, got %s.", __LINE__, a_pszExpected, a_pszActual); \
    } while (0)


/**
 * Worker for testGeneric that finalizes the digest and compares it with what is
 * expected.
 *
 * @returns true on success, false on failure.
 * @param   hDigest             The digest to finalize and check.
 * @param   iTest               The test number (for reporting).
 * @param   pTest               The test.
 * @param   rcSuccessDigest The digest success informational status code.
 */
static bool testGenericCheckResult(RTCRDIGEST hDigest, uint32_t iTest, TESTRTDIGEST const *pTest, int rcSuccessDigest)
{
    RTTESTI_CHECK_RC_RET(RTCrDigestFinal(hDigest, NULL, 0), rcSuccessDigest, false);

    char szDigest[_4K * 2];
    RTTESTI_CHECK_RC_RET(RTStrPrintHexBytes(szDigest, sizeof(szDigest),
                                            RTCrDigestGetHash(hDigest), RTCrDigestGetHashSize(hDigest), 0 /*fFlags*/),
                         VINF_SUCCESS, false);

    if (strcmp(szDigest, pTest->pszDigest))
    {
        RTTestIFailed("sub-test %#u (%s) failed: Expected %s, got %s.", iTest, pTest->pszTest, pTest->pszDigest, szDigest);
        return false;
    }

    return true;
}


/**
 * Table driven generic digest test case.
 *
 * @param   pszDigestObjId  The object ID of the digest algorithm to test.
 * @param   paTests         The test table.
 * @param   cTests          The number of tests in the table.
 * @param   pszDigestName   The name of the digest.
 * @param   enmDigestType   The digest enum type value.
 * @param   rcSuccessDigest The digest success informational status code.
 */
static void testGeneric(const char *pszDigestObjId, TESTRTDIGEST const *paTests, size_t cTests, const char *pszDigestName,
                        RTDIGESTTYPE enmDigestType, int rcSuccessDigest)
{
    /*
     * Execute the tests.
     */
    RTCRDIGEST hDigest;
    for (uint32_t iTest = 0; iTest < cTests; iTest++)
    {
        /*
         * The whole thing in one go.
         */
        RTTESTI_CHECK_RC_RETV(RTCrDigestCreateByObjIdString(&hDigest, pszDigestObjId), rcSuccessDigest);
        uint32_t const cbHash = RTCrDigestGetHashSize(hDigest);
        RTTESTI_CHECK_RETV(cbHash > 0);
        RTTESTI_CHECK_RETV(cbHash < _1K);
        RTTESTI_CHECK_RC_RETV(RTCrDigestUpdate(hDigest, paTests[iTest].pvInput, paTests[iTest].cbInput), VINF_SUCCESS);
        if (!testGenericCheckResult(hDigest, iTest, &paTests[iTest], rcSuccessDigest))
            continue; /* skip the remaining tests if this failed. */

        /*
         * Repeat the test feeding the input in smaller chunks.
         */
        static const uint32_t s_acbChunks[] = { 1, 2, 3, 7, 11, 16, 19, 31, 61, 127, 255, 511, 1023, 1024 };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_acbChunks); i++)
        {
            uint32_t const cbChunk = s_acbChunks[i];

            RTTESTI_CHECK_RC_RETV(RTCrDigestReset(hDigest), VINF_SUCCESS);

            uint8_t const *pbInput = (uint8_t const *)paTests[iTest].pvInput;
            size_t         cbInput = paTests[iTest].cbInput;
            while (cbInput > 0)
            {
                size_t cbUpdate = RT_MIN(cbInput, cbChunk);
                RTTESTI_CHECK_RC_RETV(RTCrDigestUpdate(hDigest, pbInput, cbUpdate), VINF_SUCCESS);
                pbInput += cbUpdate;
                cbInput -= cbUpdate;
            }

            if (!testGenericCheckResult(hDigest, iTest, &paTests[iTest], rcSuccessDigest))
                break; /* No need to test the other permutations if this fails. */
        }

        RTTESTI_CHECK_RC(RTCrDigestRelease(hDigest), 0);
    }

    /*
     * Do a quick benchmark.
     */
    RTTESTI_CHECK_RC_RETV(RTCrDigestCreateByObjIdString(&hDigest, pszDigestObjId), rcSuccessDigest);

    /* Warmup. */
    uint32_t cChunks  = enmDigestType == RTDIGESTTYPE_MD2 ? 12 : 128;
    uint32_t cLeft    = cChunks;
    int      rc       = VINF_SUCCESS;
    RTThreadYield();
    uint64_t uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
        rc |= RTCrDigestUpdate(hDigest, g_abRandom72KB, sizeof(g_abRandom72KB));
    uint64_t cNsPerChunk = (RTTimeNanoTS() - uStartTS) / cChunks;
    if (!cNsPerChunk)
        cNsPerChunk = 16000000 / cChunks; /* Time resolution kludge: 16ms. */
    RTTESTI_CHECK_RETV(rc == VINF_SUCCESS);

    /* Do it for real for about 2 seconds. */
    RTTESTI_CHECK_RC(RTCrDigestReset(hDigest), VINF_SUCCESS);
    cChunks = _2G32 / cNsPerChunk;
    cLeft   = cChunks;
    RTThreadYield();
    uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
        rc |= RTCrDigestUpdate(hDigest, g_abRandom72KB, sizeof(g_abRandom72KB));
    uint64_t cNsElapsed = RTTimeNanoTS() - uStartTS;
    RTTESTI_CHECK(rc == VINF_SUCCESS);

    RTTestIValueF((uint64_t)((long double)(cChunks * sizeof(g_abRandom72KB)) / _1K / (0.000000001 * (long double)cNsElapsed)),
                  RTTESTUNIT_KILOBYTES_PER_SEC, "%s throughput", pszDigestName);
    RTTESTI_CHECK_RC(RTCrDigestRelease(hDigest), 0);
}


/**
 * Tests MD2.
 */
static void testMd2(void)
{
    RTTestISub("MD2");

    /*
     * Some quick direct API tests.
     */
    uint8_t     abHash[RTMD2_HASH_SIZE];
    char        szDigest[RTMD2_DIGEST_LEN + 1];
    const char *pszString;

    pszString = "";
    RTMd2(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd2ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "8350e5a3e24c153df2275c9f80692773");

    pszString = "The quick brown fox jumps over the lazy dog";
    RTMd2(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd2ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "03d85a0d629d2c442e987525319fc471");

    pszString = "a";
    RTMd2(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd2ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "32ec01ec4a6dac72c0ab96fb34c0b5d1");

    pszString = "abc";
    RTMd2(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd2ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "da853b0d3f88d99b30283a69e6ded6bb");

    pszString = "message digest";
    RTMd2(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd2ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "ab4f496bfb2a530b219ff33031fe06b0");

    pszString = "abcdefghijklmnopqrstuvwxyz";
    RTMd2(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd2ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "4e8ddff3650292ab5a4108c3aa47940b");

    pszString = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    RTMd2(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd2ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "da33def2a42df13975352846c30338cd");

    pszString = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    RTMd2(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd2ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "d5976f79d83d3a0dc9806c3c66f3efd8");


    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],          0, "8350e5a3e24c153df2275c9f80692773", "MD2 0 bytes" },
        { &g_abRandom72KB[0],          1, "142ca23308e886f4249d147914889156", "MD2 1 byte" },
        { &g_abRandom72KB[0],          2, "5e962a3590849aff8038032cb55e4491", "MD2 2 bytes" },
        { &g_abRandom72KB[0],          3, "0d866c59a8b5e1c305570be5ade02d5e", "MD2 3 bytes" },
        { &g_abRandom72KB[0],          4, "dfd68bdf6826b5c8bf55675a0f0b8703", "MD2 4 bytes" },
        { &g_abRandom72KB[0],          5, "33f83255dcceaf8abbafdca5327f9886", "MD2 5 bytes" },
        { &g_abRandom72KB[0],          6, "586a0dac9c469e938adca3e405ba0ac8", "MD2 6 bytes" },
        { &g_abRandom72KB[0],          7, "88427c5c074552dfd2ced41d4fe5e853", "MD2 7 bytes" },
        { &g_abRandom72KB[0],          8, "927ac332618d50e49aae2d5f64c025b4", "MD2 8 bytes" },
        { &g_abRandom72KB[0],          9, "9fcae6b2526d3f089a3cccc31aa8ebf1", "MD2 9 bytes" },
        { &g_abRandom72KB[0],         10, "3ad05f9fd70ad323a0bbe77d2c320456", "MD2 10 bytes" },
        { &g_abRandom72KB[0],         11, "d9bdb0ccacdd65694d19482521256e5d", "MD2 11 bytes" },
        { &g_abRandom72KB[0],         12, "b79cb3a39d25750fa380f6e82c3cc239", "MD2 12 bytes" },
        { &g_abRandom72KB[0],         13, "b7db14f7057edd00e8c769060a3a8cb3", "MD2 13 bytes" },
        { &g_abRandom72KB[0],         14, "c3d3e05a1cb241890cbb164c800c4576", "MD2 14 bytes" },
        { &g_abRandom72KB[0],         15, "08a7b38a5e0891b07a0ed1a69f6037b2", "MD2 15 bytes" },
        { &g_abRandom72KB[0],         16, "7131c0f3b904678579fe4a64b79095a4", "MD2 16 bytes" },
        { &g_abRandom72KB[0],         17, "f91ba6efd6069ff46d604ecfdec15ac9", "MD2 17 bytes" },
        { &g_abRandom72KB[0],         18, "bea228d4e8358268071b440f41e68d21", "MD2 18 bytes" },
        { &g_abRandom72KB[0],         19, "ca4c5c896ed5dc6ac52fb8b5aedcb044", "MD2 19 bytes" },
        { &g_abRandom72KB[0],         20, "9e66429cd582564328b86f72208460f3", "MD2 20 bytes" },
        { &g_abRandom72KB[0],         21, "a366dd52bcd8baf3578e5ccff819074b", "MD2 21 bytes" },
        { &g_abRandom72KB[0],         22, "4f909f0f5101e87bb8652036fcd54fea", "MD2 22 bytes" },
        { &g_abRandom72KB[0],         23, "f74233c43a71736be4ae4d0259658106", "MD2 23 bytes" },
        { &g_abRandom72KB[0],         24, "3a245315355acb62a402646f3eb789e7", "MD2 24 bytes" },
        { &g_abRandom72KB[0],         25, "f9bbc496a41f2f377b9e7a665a503fc4", "MD2 25 bytes" },
        { &g_abRandom72KB[0],         26, "b90cc3e523c58b2ddee36dabf6544961", "MD2 26 bytes" },
        { &g_abRandom72KB[0],         27, "99a035865fbd3fe595afc01140fa8c34", "MD2 27 bytes" },
        { &g_abRandom72KB[0],         28, "8196f2d6989d824b828b483c8bf21390", "MD2 28 bytes" },
        { &g_abRandom72KB[0],         29, "957754cec1095bb3eed25827282c00db", "MD2 29 bytes" },
        { &g_abRandom72KB[0],         30, "8c392b65a3824de6edf94d4c4df064cd", "MD2 30 bytes" },
        { &g_abRandom72KB[0],         31, "cb7276510e070c82e5391e682c262e1d", "MD2 31 bytes" },
        { &g_abRandom72KB[0],         32, "cb6ba252ddd841c483095a58f2c2c78c", "MD2 32 bytes" },
        { &g_abRandom72KB[0],         33, "a74fdcd7798bb3d9153d486e0c2049bf", "MD2 33 bytes" },
        { &g_abRandom72KB[0],         34, "30f1d91b79c01fe15dda2d018e05ce8c", "MD2 34 bytes" },
        { &g_abRandom72KB[0],         35, "d24c79922d3168fc5ea3896cf6517067", "MD2 35 bytes" },
        { &g_abRandom72KB[0],         36, "eea7cb5dc0716cda6a1b97db655db057", "MD2 36 bytes" },
        { &g_abRandom72KB[0],         37, "85223ec3f4b266bb9ef9546dd4c6fbe0", "MD2 37 bytes" },
        { &g_abRandom72KB[0],         38, "4f45bdd2390452ab7ea46448f3e08d72", "MD2 38 bytes" },
        { &g_abRandom72KB[0],         39, "c2e0e4b3fbaab93342e952ff1f45bb3e", "MD2 39 bytes" },
        { &g_abRandom72KB[0],         40, "08967efe427009b4b4f1d6d444ae2897", "MD2 40 bytes" },
        { &g_abRandom72KB[0],         41, "ef66a72fb184b64d07b525dc1a6e0376", "MD2 41 bytes" },
        { &g_abRandom72KB[0],         42, "8e08b6337537406770c27584293ea515", "MD2 42 bytes" },
        { &g_abRandom72KB[0],         43, "08fe64ee6d9a811154d9312c586c0a08", "MD2 43 bytes" },
        { &g_abRandom72KB[0],         44, "4f29656bf1fe579357240d1a364415c5", "MD2 44 bytes" },
        { &g_abRandom72KB[0],         45, "7998eb00e5774b175db8fa91de555aab", "MD2 45 bytes" },
        { &g_abRandom72KB[0],         46, "a7692c36d342ba2b750f1f26fecdac56", "MD2 46 bytes" },
        { &g_abRandom72KB[0],         47, "038f0bb2828f03fcabde6aa9dcb10e6d", "MD2 47 bytes" },
        { &g_abRandom72KB[0],         48, "b178ffa99a7c5caefef81b8601fbd992", "MD2 48 bytes" },
        { &g_abRandom72KB[0],         49, "f139777906b3ca8785a893f4aacd2489", "MD2 49 bytes" },
        { &g_abRandom72KB[0],         50, "e1a57dcc846d8c0ad3853fde8278094e", "MD2 50 bytes" },
        { &g_abRandom72KB[0],         51, "e754995fa0fd61ba9e10a54e12afde4f", "MD2 51 bytes" },
        { &g_abRandom72KB[0],         52, "ddf17522138d919678f44eaeeb1c0901", "MD2 52 bytes" },
        { &g_abRandom72KB[0],         53, "7b588ece72a7dca79fcabfda083b7ac0", "MD2 53 bytes" },
        { &g_abRandom72KB[0],         54, "8b23c8a9261faeb9ebb2853a03f3f2eb", "MD2 54 bytes" },
        { &g_abRandom72KB[0],         55, "89d700920d22d2f4a655add9fce30295", "MD2 55 bytes" },
        { &g_abRandom72KB[0],         56, "c3ece99f88613724c8e6d17af9e728a9", "MD2 56 bytes" },
        { &g_abRandom72KB[0],         57, "271a0f4e6e270fb0264348cff440f36d", "MD2 57 bytes" },
        { &g_abRandom72KB[0],         58, "4b01b8d1f7912dea17b5c8f5aa0654f2", "MD2 58 bytes" },
        { &g_abRandom72KB[0],         59, "ed399fc4cab3cbcc1d65dda49f9496b7", "MD2 59 bytes" },
        { &g_abRandom72KB[0],         60, "59eab0ff60a2da2935cdd548082c3cf9", "MD2 60 bytes" },
        { &g_abRandom72KB[0],         61, "88c5169745df6ef88088bd5d162ef2f9", "MD2 61 bytes" },
        { &g_abRandom72KB[0],         62, "c4711f410d141f7e3fc40464cf117a98", "MD2 62 bytes" },
        { &g_abRandom72KB[0],         63, "d9bf4e2e3692082cf712466c90c7548b", "MD2 63 bytes" },
        { &g_abRandom72KB[0],         64, "cd8e18bede03bbc4620539b3d41091a0", "MD2 64 bytes" },
        { &g_abRandom72KB[0],         65, "4ccbf9b18f0a47a958efd71f4bcff1d5", "MD2 65 bytes" },
        { &g_abRandom72KB[0],         66, "5f0356e384a3ace42ddaa0053494e932", "MD2 66 bytes" },
        { &g_abRandom72KB[0],         67, "c90f40c059da2d14c45c9a6f72fbc06a", "MD2 67 bytes" },
        { &g_abRandom72KB[0],         68, "29d000bccfc0df57d79da8d275c5766d", "MD2 68 bytes" },
        { &g_abRandom72KB[0],         69, "2fb908536b6e0e4ead7aa5e8552a10cb", "MD2 69 bytes" },
        { &g_abRandom72KB[0],         70, "df1e18111f62b07aae8e3f81d74385d1", "MD2 70 bytes" },
        { &g_abRandom72KB[0],         71, "1213eca29ce71f0037527f8347da4c1c", "MD2 71 bytes" },
        { &g_abRandom72KB[0],         72, "bd6686795f936446534646f7d7f35e03", "MD2 72 bytes" },
        { &g_abRandom72KB[0],         73, "b5181c0b712b3aa024e67430edbd816a", "MD2 73 bytes" },
        { &g_abRandom72KB[0],         74, "3b05c33b18ac294e35cd18337d31a842", "MD2 74 bytes" },
        { &g_abRandom72KB[0],         75, "1d6e83621f88667536242ff538872758", "MD2 75 bytes" },
        { &g_abRandom72KB[0],         76, "d830d59fe1cba1da97f19c8750a130d6", "MD2 76 bytes" },
        { &g_abRandom72KB[0],         77, "2d290278d7c502b59a68ca47f22e2ffd", "MD2 77 bytes" },
        { &g_abRandom72KB[0],         78, "7fcf7fa7e54c5189c76c3724811fe105", "MD2 78 bytes" },
        { &g_abRandom72KB[0],         79, "f41c1697d5cb26734108aace37d2f8ee", "MD2 79 bytes" },
        { &g_abRandom72KB[0],         80, "ff814e4823539973251969c88f0aabd2", "MD2 80 bytes" },
        { &g_abRandom72KB[0],         81, "eaf947ace4636dd9fdf5f540d48afa18", "MD2 81 bytes" },
        { &g_abRandom72KB[0],         82, "89b69633a8f41d2f318a59157f7b1135", "MD2 82 bytes" },
        { &g_abRandom72KB[0],         83, "22b6a01cf22dda9aecd29903eb551b96", "MD2 83 bytes" },
        { &g_abRandom72KB[0],         84, "2ab03e9c9674df73c9316ae471275b9e", "MD2 84 bytes" },
        { &g_abRandom72KB[0],         85, "f3c3b113995ad4bb6ac011bce4c8aaeb", "MD2 85 bytes" },
        { &g_abRandom72KB[0],         86, "c47408bcce2ad6d88c27644381829d17", "MD2 86 bytes" },
        { &g_abRandom72KB[0],         87, "6a7eb6a1d8d6e2d32fbfb5e18b4a7d78", "MD2 87 bytes" },
        { &g_abRandom72KB[0],         88, "573d2746448f32b9fb2e7e96c902df5d", "MD2 88 bytes" },
        { &g_abRandom72KB[0],         89, "88043ead96eb9ad170a2a5c04b31473e", "MD2 89 bytes" },
        { &g_abRandom72KB[0],         90, "e08bf4255a9aceec98195d98fe23ca3a", "MD2 90 bytes" },
        { &g_abRandom72KB[0],         91, "8c5d08ff1e6cd07b13ea5c8f2679be54", "MD2 91 bytes" },
        { &g_abRandom72KB[0],         92, "229e41d0dea6c9fc50385a04107d34e1", "MD2 92 bytes" },
        { &g_abRandom72KB[0],         93, "9ba3f41db14506d0d72d2477086fc3b0", "MD2 93 bytes" },
        { &g_abRandom72KB[0],         94, "6838e101797e4af9db93cce7b7f08497", "MD2 94 bytes" },
        { &g_abRandom72KB[0],         95, "a936e8484a013da8fb2f76b1a5e0e577", "MD2 95 bytes" },
        { &g_abRandom72KB[0],         96, "34140248d6f7c3cfe08c52460bd5ae85", "MD2 96 bytes" },
        { &g_abRandom72KB[0],         97, "65022916cd54ab0dfddd5b6a5d88daf6", "MD2 97 bytes" },
        { &g_abRandom72KB[0],         98, "929a96dfeaca781892e1ef0114429d07", "MD2 98 bytes" },
        { &g_abRandom72KB[0],         99, "f3c117eff8a7693a34f60d8364b32fb4", "MD2 99 bytes" },
        { &g_abRandom72KB[0],        100, "b20414388581bc1f001bad02d34db98f", "MD2 100 bytes" },
        { &g_abRandom72KB[0],        101, "2e649b9250edc6a717f7a72ba5ad245b", "MD2 101 bytes" },
        { &g_abRandom72KB[0],        102, "84c30f8dfb3555f24320d1da1fe41845", "MD2 102 bytes" },
        { &g_abRandom72KB[0],        103, "ce647df6e37517be82a63eb5bb06c010", "MD2 103 bytes" },
        { &g_abRandom72KB[0],        104, "e58b4587f8d8654446b495054ca9a3c8", "MD2 104 bytes" },
        { &g_abRandom72KB[0],        105, "d0e5067a2fcdd00cbb058228b9f23a09", "MD2 105 bytes" },
        { &g_abRandom72KB[0],        106, "2666a7d835c27bf8e2b88e5260fc4b08", "MD2 106 bytes" },
        { &g_abRandom72KB[0],        107, "ba5f7980c93a5f63a921d11f74aa0c0b", "MD2 107 bytes" },
        { &g_abRandom72KB[0],        108, "c03ca631eed8a88ddd6aadb6b138da41", "MD2 108 bytes" },
        { &g_abRandom72KB[0],        109, "84c273d3e262c7d0d18f8d32ce6c87ab", "MD2 109 bytes" },
        { &g_abRandom72KB[0],        110, "8abc96f5ef0dd75a64ed4aebdb529e45", "MD2 110 bytes" },
        { &g_abRandom72KB[0],        111, "85d93a90b19e9dcaf2794d61eaf6095f", "MD2 111 bytes" },
        { &g_abRandom72KB[0],        112, "d14a45c32e48b9deee617a4682a6e47a", "MD2 112 bytes" },
        { &g_abRandom72KB[0],        113, "171a52a9b9df939ce634a530e1fdb571", "MD2 113 bytes" },
        { &g_abRandom72KB[0],        114, "bcfd33c67597ea544a54266c5b971ad5", "MD2 114 bytes" },
        { &g_abRandom72KB[0],        115, "76799540a3dace08d09dc1c23e33d7db", "MD2 115 bytes" },
        { &g_abRandom72KB[0],        116, "83ef334faedec13d17f5df823d0f98a1", "MD2 116 bytes" },
        { &g_abRandom72KB[0],        117, "cafcb95eac55ba3a07d8a216dc89bf3c", "MD2 117 bytes" },
        { &g_abRandom72KB[0],        118, "042c1f9c066204e9cb43a8ba6a73195a", "MD2 118 bytes" },
        { &g_abRandom72KB[0],        119, "829e12d96e1a3ce4c51dd70e9774da42", "MD2 119 bytes" },
        { &g_abRandom72KB[0],        120, "162c221e43624b03dee871e2ddf9275a", "MD2 120 bytes" },
        { &g_abRandom72KB[0],        121, "272eba7f65e4704a8200039351f7b7ed", "MD2 121 bytes" },
        { &g_abRandom72KB[0],        122, "4418e4c4ef6b6c9f5d6e35f934c54dc2", "MD2 122 bytes" },
        { &g_abRandom72KB[0],        123, "cbb78508b71600c067923bf200abd2d2", "MD2 123 bytes" },
        { &g_abRandom72KB[0],        124, "133e55a0236279fe2c3c8a616e6a4ec1", "MD2 124 bytes" },
        { &g_abRandom72KB[0],        125, "5aba7624888166ecfcf88607e7bca4ff", "MD2 125 bytes" },
        { &g_abRandom72KB[0],        126, "99fe0c4302b79f84b236eb125af38c9f", "MD2 126 bytes" },
        { &g_abRandom72KB[0],        127, "1c6aacae3441ab225a7aec2deefa5f8a", "MD2 127 bytes" },
        { &g_abRandom72KB[0],        128, "a971703ab10735c6df4e19ff5052da40", "MD2 128 bytes" },
        { &g_abRandom72KB[0],        129, "e7fd473d1f062183a18af88e947d0096", "MD2 129 bytes" },
        { &g_abRandom72KB[0],       1024, "bdccc17a55ced048a6937a2735b09cc6", "MD2 1024 bytes" },
        { &g_abRandom72KB[0],      73001, "54e982ce469bc379a71e2ca755eabe31", "MD2 73001 bytes" },
        { &g_abRandom72KB[0],      73728, "2cf3570a90117130c8879cca30dafb39", "MD2 73728 bytes" },
        { &g_abRandom72KB[0x20c9],  9991, "bbba194efa81238e5b613e20e937144e", "MD2 8393 bytes @9991" },
    };
    testGeneric("1.2.840.113549.2.2", s_abTests, RT_ELEMENTS(s_abTests), "MD2", RTDIGESTTYPE_MD2, VINF_CR_DIGEST_DEPRECATED);
}


/**
 * Tests MD4.
 */
static void testMd4(void)
{
    RTTestISub("MD4");

    /*
     * Some quick direct API tests.
     */
    uint8_t     abHash[RTMD4_HASH_SIZE];
    char        szDigest[RTMD4_DIGEST_LEN + 1];
    const char *pszString;

    pszString = "";
    RTMd4(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd4ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "31d6cfe0d16ae931b73c59d7e0c089c0");

    pszString = "The quick brown fox jumps over the lazy dog";
    RTMd4(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd4ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "1bee69a46ba811185c194762abaeae90");

    pszString = "a";
    RTMd4(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd4ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "bde52cb31de33e46245e05fbdbd6fb24");

    pszString = "abc";
    RTMd4(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd4ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "a448017aaf21d8525fc10ae87aa6729d");

    pszString = "message digest";
    RTMd4(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd4ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "d9130a8164549fe818874806e1c7014b");

    pszString = "abcdefghijklmnopqrstuvwxyz";
    RTMd4(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd4ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "d79e1c308aa5bbcdeea8ed63df412da9");

    pszString = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    RTMd4(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd4ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "043f8582f241db351ce627e153e7f0e4");

    pszString = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    RTMd4(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd4ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "e33b4ddc9c38f2199c3e7b164fcc0536");


    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],          0, "31d6cfe0d16ae931b73c59d7e0c089c0", "MD4 0 bytes" },
        { &g_abRandom72KB[0],          1, "0014d6b190d365d071ea01701055f180", "MD4 1 byte" },
        { &g_abRandom72KB[0],          2, "2be9806c2efb7648dc5c3ec5a502cf10", "MD4 2 bytes" },
        { &g_abRandom72KB[0],          3, "4d5268b74418be41d0f2ed6806048897", "MD4 3 bytes" },
        { &g_abRandom72KB[0],          4, "62f2e58f059dea40a92eba280f3062b4", "MD4 4 bytes" },
        { &g_abRandom72KB[0],          5, "b3f93386d95f56dd3e95b9bd03c6e944", "MD4 5 bytes" },
        { &g_abRandom72KB[0],          6, "21faedc10d9ec3943d047518b6fd5238", "MD4 6 bytes" },
        { &g_abRandom72KB[0],          7, "fba0c1028ffb0714cc44a9572713a900", "MD4 7 bytes" },
        { &g_abRandom72KB[0],          8, "1ba88ad5bc00076e753213a2e5b8ae15", "MD4 8 bytes" },
        { &g_abRandom72KB[0],          9, "e86ba9ff9cdd821dedc54fc7e752dda4", "MD4 9 bytes" },
        { &g_abRandom72KB[0],         10, "4ca3d69542505eaebd6a55dccb1d7413", "MD4 10 bytes" },
        { &g_abRandom72KB[0],         11, "3efc80988668aa176ec0cee9edd8122f", "MD4 11 bytes" },
        { &g_abRandom72KB[0],         12, "084cd1ce1d35e8e1908c9700f372790a", "MD4 12 bytes" },
        { &g_abRandom72KB[0],         13, "91114dc2de94a27707045f261ce616e0", "MD4 13 bytes" },
        { &g_abRandom72KB[0],         14, "637000da1e66c73d400baa653470bdb1", "MD4 14 bytes" },
        { &g_abRandom72KB[0],         15, "02af37485efd590a086dac3f30389592", "MD4 15 bytes" },
        { &g_abRandom72KB[0],         16, "2a6f6d15a99fc5a467f60bb78733780e", "MD4 16 bytes" },
        { &g_abRandom72KB[0],         17, "866b211da26762208316b930dacdeaaa", "MD4 17 bytes" },
        { &g_abRandom72KB[0],         18, "15ebc4114d9e5ca0cdff23f84531a64d", "MD4 18 bytes" },
        { &g_abRandom72KB[0],         19, "56927e67e2f6a99c8327c6ff14404d61", "MD4 19 bytes" },
        { &g_abRandom72KB[0],         20, "70fc8943ee6233e1f7a349a6dc68d0f6", "MD4 20 bytes" },
        { &g_abRandom72KB[0],         21, "4927cf2f4ec8ede18328abc0507cb1aa", "MD4 21 bytes" },
        { &g_abRandom72KB[0],         22, "dccda1a1c95d36c4ab6f593c3ab81b9e", "MD4 22 bytes" },
        { &g_abRandom72KB[0],         23, "9a45e0e9bfee3f362592ccc8aea9207c", "MD4 23 bytes" },
        { &g_abRandom72KB[0],         24, "0f4dee14ef3ec085ee80810066095600", "MD4 24 bytes" },
        { &g_abRandom72KB[0],         25, "986534cc3fd5c50a8be5c66b170bd67b", "MD4 25 bytes" },
        { &g_abRandom72KB[0],         26, "fb84beb50239e80d38631571fc02bd37", "MD4 26 bytes" },
        { &g_abRandom72KB[0],         27, "416675187789def281c48d402d3db6d8", "MD4 27 bytes" },
        { &g_abRandom72KB[0],         28, "a7e5379e670b33f3d9e3499955561cd8", "MD4 28 bytes" },
        { &g_abRandom72KB[0],         29, "5b3b41e2c4d91056c58280468952d755", "MD4 29 bytes" },
        { &g_abRandom72KB[0],         30, "8f0389eb6c9a5574341ec707b7496fdb", "MD4 30 bytes" },
        { &g_abRandom72KB[0],         31, "8fb16cc1fd5ea43d6ae52b852dbecf3f", "MD4 31 bytes" },
        { &g_abRandom72KB[0],         32, "c5ce62339fa63c3eb8386ee024e157ec", "MD4 32 bytes" },
        { &g_abRandom72KB[0],         33, "fc03d11586207fa12b505354f6ed8fa0", "MD4 33 bytes" },
        { &g_abRandom72KB[0],         34, "41923f367cd620969f1ee087295d0a40", "MD4 34 bytes" },
        { &g_abRandom72KB[0],         35, "07dbc9ad8d993e0c7a8febd2c506b87b", "MD4 35 bytes" },
        { &g_abRandom72KB[0],         36, "19d8e4f515748b03ad6b000ccce51101", "MD4 36 bytes" },
        { &g_abRandom72KB[0],         37, "54d2c85331ca4e7236629620268527c4", "MD4 37 bytes" },
        { &g_abRandom72KB[0],         38, "352148c6cc975c6b67704c52be5e280a", "MD4 38 bytes" },
        { &g_abRandom72KB[0],         39, "31c52890373437a2041a759fdd782ee2", "MD4 39 bytes" },
        { &g_abRandom72KB[0],         40, "1aebb166f3576845dc79b442885ea46c", "MD4 40 bytes" },
        { &g_abRandom72KB[0],         41, "0ca6c4388a7faa9304681261bc155676", "MD4 41 bytes" },
        { &g_abRandom72KB[0],         42, "69cd5768697348aceac47c2c119efb60", "MD4 42 bytes" },
        { &g_abRandom72KB[0],         43, "0912c29c5fd6daa187f4c22426854792", "MD4 43 bytes" },
        { &g_abRandom72KB[0],         44, "a7db92786a2703ff4b06cd3be31dabb7", "MD4 44 bytes" },
        { &g_abRandom72KB[0],         45, "d6584bb2131258613f779e26e72c6d72", "MD4 45 bytes" },
        { &g_abRandom72KB[0],         46, "19c49d89942e737d592618e45c413480", "MD4 46 bytes" },
        { &g_abRandom72KB[0],         47, "25034803d9d5b5add9969416147b9db4", "MD4 47 bytes" },
        { &g_abRandom72KB[0],         48, "ce20d232be7507c888591db5b3803086", "MD4 48 bytes" },
        { &g_abRandom72KB[0],         49, "fba1b3a38e353f06cac6a85d90f17733", "MD4 49 bytes" },
        { &g_abRandom72KB[0],         50, "9b72cd65b42e377c3d3660b07474c617", "MD4 50 bytes" },
        { &g_abRandom72KB[0],         51, "9c7177c712c4124cf1e7fe153fa2dbcb", "MD4 51 bytes" },
        { &g_abRandom72KB[0],         52, "6f3428a83cba0c3ee050ad43441c80ac", "MD4 52 bytes" },
        { &g_abRandom72KB[0],         53, "e4fff75da9a2797949101eeccd71cda9", "MD4 53 bytes" },
        { &g_abRandom72KB[0],         54, "840d32fc7b779ceb6b8d1b0b2cd0c9fc", "MD4 54 bytes" },
        { &g_abRandom72KB[0],         55, "0742c60033fe94ade3a0b1627e2daca1", "MD4 55 bytes" },
        { &g_abRandom72KB[0],         56, "6e258014688e63ad1a20ebe81d285141", "MD4 56 bytes" },
        { &g_abRandom72KB[0],         57, "f8c6d8879fcc3f99b10c680ca7b96566", "MD4 57 bytes" },
        { &g_abRandom72KB[0],         58, "5b0882e886b454247403184daaa5088f", "MD4 58 bytes" },
        { &g_abRandom72KB[0],         59, "0a2944704b29793e9b8e86950bc06eb7", "MD4 59 bytes" },
        { &g_abRandom72KB[0],         60, "84e03ff08c2fd101b157bb6799339dd8", "MD4 60 bytes" },
        { &g_abRandom72KB[0],         61, "66fc3200bd43dc88fbdf2ecfc86210c0", "MD4 61 bytes" },
        { &g_abRandom72KB[0],         62, "19ea83da725826d1fa4a2816eca5363d", "MD4 62 bytes" },
        { &g_abRandom72KB[0],         63, "a274d6f859c5e2a3aa055e2f25a5f695", "MD4 63 bytes" },
        { &g_abRandom72KB[0],         64, "dcd51f9400a8864157ef380eebbef60d", "MD4 64 bytes" },
        { &g_abRandom72KB[0],         65, "f8fe3a0be1c9ec0ee1a9f1a8bfe7701c", "MD4 65 bytes" },
        { &g_abRandom72KB[0],         66, "e20f96625019092cc70d706bbd113aec", "MD4 66 bytes" },
        { &g_abRandom72KB[0],         67, "16dd4f76d2d1845bfb9804516ce1a509", "MD4 67 bytes" },
        { &g_abRandom72KB[0],         68, "cc30a7b83a4a54e592435e1185ea4812", "MD4 68 bytes" },
        { &g_abRandom72KB[0],         69, "a64b84a86b0bf5b93604e2151d1dab05", "MD4 69 bytes" },
        { &g_abRandom72KB[0],         70, "523fb6da8d86b6d21d59e79150ec9749", "MD4 70 bytes" },
        { &g_abRandom72KB[0],         71, "ba92cad24dae5ff8b09e078c7b80df1d", "MD4 71 bytes" },
        { &g_abRandom72KB[0],         72, "510636391607ac06f1701b6420d90bf6", "MD4 72 bytes" },
        { &g_abRandom72KB[0],         73, "642f99a448cfebcb6502a3d51d84561e", "MD4 73 bytes" },
        { &g_abRandom72KB[0],         74, "0594cbfff1dcd956168f9780b4bf5fd7", "MD4 74 bytes" },
        { &g_abRandom72KB[0],         75, "bcd149fa1824199ce20723c455c6a799", "MD4 75 bytes" },
        { &g_abRandom72KB[0],         76, "2b31bf720e1adff2f9b3229ad680eea0", "MD4 76 bytes" },
        { &g_abRandom72KB[0],         77, "8498a6b10c06fd14f88fa4fbd957c1ec", "MD4 77 bytes" },
        { &g_abRandom72KB[0],         78, "03366c519d243d914e13024803ac6fdb", "MD4 78 bytes" },
        { &g_abRandom72KB[0],         79, "4795ab150f194b2e8e5c0f40f26f09eb", "MD4 79 bytes" },
        { &g_abRandom72KB[0],         80, "ca2b9808e7083ff0be1c0b5567861123", "MD4 80 bytes" },
        { &g_abRandom72KB[0],         81, "002a0aa238fc962ab76313413111fc34", "MD4 81 bytes" },
        { &g_abRandom72KB[0],         82, "289fb6a311a5603fe47c65c571ee3938", "MD4 82 bytes" },
        { &g_abRandom72KB[0],         83, "cacd59c0bc6fc31849c0f4552e3b253f", "MD4 83 bytes" },
        { &g_abRandom72KB[0],         84, "b6ebc1eb2ed472ab95696752aafeab24", "MD4 84 bytes" },
        { &g_abRandom72KB[0],         85, "98c289ef93f887fa9eeed86e655f5890", "MD4 85 bytes" },
        { &g_abRandom72KB[0],         86, "22c3bf5be2798a9ec04e1c459ff65544", "MD4 86 bytes" },
        { &g_abRandom72KB[0],         87, "43cd7e64a8d0b5a2ee4221786876bb0f", "MD4 87 bytes" },
        { &g_abRandom72KB[0],         88, "ec95c85bb82c26e17ef7083a6732c1e5", "MD4 88 bytes" },
        { &g_abRandom72KB[0],         89, "1b9a6299f80996cd9a01ffd4299d4b2a", "MD4 89 bytes" },
        { &g_abRandom72KB[0],         90, "9ad1ec0845c287fa2c3400946f3b74c4", "MD4 90 bytes" },
        { &g_abRandom72KB[0],         91, "eff2124cd403f523ddefce349d913d33", "MD4 91 bytes" },
        { &g_abRandom72KB[0],         92, "5253b33449b543eadc7de0061c35cdf0", "MD4 92 bytes" },
        { &g_abRandom72KB[0],         93, "170f9e4cda211d9e2c48a3636d29120e", "MD4 93 bytes" },
        { &g_abRandom72KB[0],         94, "7c738164e285ec838c2736eb1396db34", "MD4 94 bytes" },
        { &g_abRandom72KB[0],         95, "cfedfee3e1ac2414d9083a8ffcb1d6c6", "MD4 95 bytes" },
        { &g_abRandom72KB[0],         96, "41ff6a9f247ba6ba7cbc16bc19101b0a", "MD4 96 bytes" },
        { &g_abRandom72KB[0],         97, "871ba92280eb7a1d838376840bd3eedb", "MD4 97 bytes" },
        { &g_abRandom72KB[0],         98, "1663e51bcea3a86fb19ed2753c3234ba", "MD4 98 bytes" },
        { &g_abRandom72KB[0],         99, "70d79f6be8da0a5ac1e05f9a3f72385d", "MD4 99 bytes" },
        { &g_abRandom72KB[0],        100, "1b2eeec426c1e5d8b2459b9308eec29e", "MD4 100 bytes" },
        { &g_abRandom72KB[0],        101, "1d7eac9ef23954d8c2e9115ca8ae43c2", "MD4 101 bytes" },
        { &g_abRandom72KB[0],        102, "f1dd48951a67dddecfca3572a6d1e41b", "MD4 102 bytes" },
        { &g_abRandom72KB[0],        103, "97c4443adf9896437112a826d3041f08", "MD4 103 bytes" },
        { &g_abRandom72KB[0],        104, "8834a131a88b9cf837389cf9cbf00a16", "MD4 104 bytes" },
        { &g_abRandom72KB[0],        105, "54c072c5b32150a631d584212be70918", "MD4 105 bytes" },
        { &g_abRandom72KB[0],        106, "28312975c6f5eeab493208ae2fd36401", "MD4 106 bytes" },
        { &g_abRandom72KB[0],        107, "921f5392f4ebd739647db883067e33a9", "MD4 107 bytes" },
        { &g_abRandom72KB[0],        108, "f60480eb631d321f8fba8d6f8b302bd7", "MD4 108 bytes" },
        { &g_abRandom72KB[0],        109, "ba9ec7166e3b8764d7110b097b0183a8", "MD4 109 bytes" },
        { &g_abRandom72KB[0],        110, "319757aba1d89a4a0e675ab08408956b", "MD4 110 bytes" },
        { &g_abRandom72KB[0],        111, "9a85d1eb0fb988c0d83aad8353e7e29e", "MD4 111 bytes" },
        { &g_abRandom72KB[0],        112, "fac16e7a4c33a301a0f626c5d291e27e", "MD4 112 bytes" },
        { &g_abRandom72KB[0],        113, "654d8ebe888100f4c484d22b68af9185", "MD4 113 bytes" },
        { &g_abRandom72KB[0],        114, "766df4a0681e87523146cc2c5084bc5b", "MD4 114 bytes" },
        { &g_abRandom72KB[0],        115, "c73d46e4e0039821f29eb595e63a6445", "MD4 115 bytes" },
        { &g_abRandom72KB[0],        116, "ed3544002078b116451827c45b8ce782", "MD4 116 bytes" },
        { &g_abRandom72KB[0],        117, "0db043d6e4006523679ad807d1b8b25d", "MD4 117 bytes" },
        { &g_abRandom72KB[0],        118, "59fd1acab527423854deb0b5cc8aa7fd", "MD4 118 bytes" },
        { &g_abRandom72KB[0],        119, "ca5d2aa85a493741e4e6bce6f6d23e1d", "MD4 119 bytes" },
        { &g_abRandom72KB[0],        120, "47f646776ef01e3c8e4e38c0beae7333", "MD4 120 bytes" },
        { &g_abRandom72KB[0],        121, "1905c60dc8f51c3b7c326b5cad9f6fd2", "MD4 121 bytes" },
        { &g_abRandom72KB[0],        122, "ce435994d14df744b26d5dfc80b95398", "MD4 122 bytes" },
        { &g_abRandom72KB[0],        123, "e3c102dcf64d29f2c386413c0483c3a6", "MD4 123 bytes" },
        { &g_abRandom72KB[0],        124, "aa7ed5d353dc66b7f23639ac5cf18636", "MD4 124 bytes" },
        { &g_abRandom72KB[0],        125, "37f30e534f4661a989e9c9367ded7536", "MD4 125 bytes" },
        { &g_abRandom72KB[0],        126, "da722a846bcdd3b5b8acf5100b44a9b4", "MD4 126 bytes" },
        { &g_abRandom72KB[0],        127, "b93bc385e0c6d2b31213bbec50111359", "MD4 127 bytes" },
        { &g_abRandom72KB[0],        128, "762bbb84570d3fb82ea0cd2b468d1ac1", "MD4 128 bytes" },
        { &g_abRandom72KB[0],        129, "0f7b751552acc853f88684616d21a737", "MD4 129 bytes" },
        { &g_abRandom72KB[0],       1024, "3722350e9c6c7e7ddf39f112cdd6b454", "MD4 1024 bytes" },
        { &g_abRandom72KB[0],      73001, "ee4fec16230aff2fd3f9075d781de22d", "MD4 73001 bytes" },
        { &g_abRandom72KB[0],      73728, "120c5ba7971af1dcc004c2a54e0afb44", "MD4 73728 bytes" },
        { &g_abRandom72KB[0x20c9],  9991, "7d1e0e69dfec5dd0ae84aeecd53516bd", "MD4 8393 bytes @9991" },
    };
    testGeneric("1.2.840.113549.2.4", s_abTests, RT_ELEMENTS(s_abTests), "MD4", RTDIGESTTYPE_MD4,
                VINF_CR_DIGEST_SEVERELY_COMPROMISED);
}


/**
 * Tests MD5.
 */
static void testMd5(void)
{
    RTTestISub("MD5");

    /*
     * Some quick direct API tests.
     */
    uint8_t     abHash[RTMD5_HASH_SIZE];
    char        szDigest[RTMD5_DIGEST_LEN + 1];
    const char *pszString;

    pszString = "";
    RTMd5(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd5ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "d41d8cd98f00b204e9800998ecf8427e");

    pszString = "The quick brown fox jumps over the lazy dog";
    RTMd5(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd5ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "9e107d9d372bb6826bd81d3542a419d6");

    pszString = "a";
    RTMd5(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd5ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "0cc175b9c0f1b6a831c399e269772661");

    pszString = "abc";
    RTMd5(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd5ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "900150983cd24fb0d6963f7d28e17f72");

    pszString = "message digest";
    RTMd5(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd5ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "f96b697d7cb7938d525a2f31aaf161d0");

    pszString = "abcdefghijklmnopqrstuvwxyz";
    RTMd5(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd5ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "c3fcd3d76192e4007dfb496cca67e13b");

    pszString = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    RTMd5(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd5ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "d174ab98d277d9f5a5611c2c9f419d9f");

    pszString = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    RTMd5(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTMd5ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "57edf4a22be3c955ac49da2e2107b67a");


    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],         0, "d41d8cd98f00b204e9800998ecf8427e", "MD5 0 bytes" },
        { &g_abRandom72KB[0],         1, "785d512be4316d578e6650613b45e934", "MD5 1 bytes" },
        { &g_abRandom72KB[0],         2, "87ed1a6013d72148da0b0294e763048d", "MD5 2 bytes" },
        { &g_abRandom72KB[0],         3, "822de939633727229045cb7789af7df5", "MD5 3 bytes" },
        { &g_abRandom72KB[0],         4, "86287bc09cb9260cf25c132979051304", "MD5 4 bytes" },
        { &g_abRandom72KB[0],         5, "d3d1ce75037d39cf5de6c00f0ed3ca78", "MD5 5 bytes" },
        { &g_abRandom72KB[0],         6, "3dd3a9b4fdf9afecac4ff3a81fa42487", "MD5 6 bytes" },
        { &g_abRandom72KB[0],         7, "15644ca265e05e9c6cf19f297fd061d4", "MD5 7 bytes" },
        { &g_abRandom72KB[0],         8, "da96c8b71cd59cb6af75358ca6c85d7f", "MD5 8 bytes" },
        { &g_abRandom72KB[0],         9, "c2ad718a365c3b3ea6452d18911c9731", "MD5 9 bytes" },
        { &g_abRandom72KB[0],        10, "b5075641b6951182c10596c1f2e4f77a", "MD5 10 bytes" },
        { &g_abRandom72KB[0],        11, "ceea2cb4481c655b7057e1b8dd5a0bd1", "MD5 11 bytes" },
        { &g_abRandom72KB[0],        12, "006e60df0db98aaa0c7a920ea3b663e3", "MD5 12 bytes" },
        { &g_abRandom72KB[0],        13, "3a2cfd399b70235b276203b03dd6eb0e", "MD5 13 bytes" },
        { &g_abRandom72KB[0],        14, "e7829cff3b8c15a6771b9ee8e9dd2084", "MD5 14 bytes" },
        { &g_abRandom72KB[0],        15, "4277413716da82ffee61f36fc25fd5b1", "MD5 15 bytes" },
        { &g_abRandom72KB[0],        16, "823753a79e0677c9b7e41b5725dbce8f", "MD5 16 bytes" },
        { &g_abRandom72KB[0],        17, "3af2c16a115519a00fa8c38f99bdd531", "MD5 17 bytes" },
        { &g_abRandom72KB[0],        18, "e0d640f347fd089338434c4ddc826538", "MD5 18 bytes" },
        { &g_abRandom72KB[0],        19, "d07554497ee83534affa84a5af86c04d", "MD5 19 bytes" },
        { &g_abRandom72KB[0],        20, "b3156aab604ab535832243ff1ce0d3ea", "MD5 20 bytes" },
        { &g_abRandom72KB[0],        21, "a4fb150aab01d4ac4859ecaecf221c63", "MD5 21 bytes" },
        { &g_abRandom72KB[0],        22, "fe6aa2d9c68e100ebaca1bede6be7025", "MD5 22 bytes" },
        { &g_abRandom72KB[0],        23, "db6c3662d9a8950ddcd1b8603a5ab040", "MD5 23 bytes" },
        { &g_abRandom72KB[0],        24, "c56df7b41f9b1420e9e114e82484bcdf", "MD5 24 bytes" },
        { &g_abRandom72KB[0],        25, "7194fb3612d4f53d8eeaa80d60618e49", "MD5 25 bytes" },
        { &g_abRandom72KB[0],        26, "3355f00eac0c85ea8e84c713f55bca3a", "MD5 26 bytes" },
        { &g_abRandom72KB[0],        27, "127b104d22b2cb437ba3b34f6c977e05", "MD5 27 bytes" },
        { &g_abRandom72KB[0],        28, "ce4911fab274a55254cc8989252b47a9", "MD5 28 bytes" },
        { &g_abRandom72KB[0],        29, "f9dd6d60777fee2d654835d5b239979b", "MD5 29 bytes" },
        { &g_abRandom72KB[0],        30, "213779ae9dd7ce0cf199c42ad719da5d", "MD5 30 bytes" },
        { &g_abRandom72KB[0],        31, "5e561c81405437e2205aa64eff81fad3", "MD5 31 bytes" },
        { &g_abRandom72KB[0],        32, "e41238f661fa29883be2f387e56621fb", "MD5 32 bytes" },
        { &g_abRandom72KB[0],        33, "d474457edf9bb331935109b905b2ac9c", "MD5 33 bytes" },
        { &g_abRandom72KB[0],        34, "9c60aa7bb5380442a96fcc960c54f4cd", "MD5 34 bytes" },
        { &g_abRandom72KB[0],        35, "6c27d085e88368d951d0be70bcb83daa", "MD5 35 bytes" },
        { &g_abRandom72KB[0],        36, "e743d29943ddee43e2d3b20373868ace", "MD5 36 bytes" },
        { &g_abRandom72KB[0],        37, "7917ff3a754410f5f3e6a1e34543ad3b", "MD5 37 bytes" },
        { &g_abRandom72KB[0],        38, "d9f6b9d5188e836fa851a5900ac20f3a", "MD5 38 bytes" },
        { &g_abRandom72KB[0],        39, "cef18b503ba9beb5ddf8a70112aaad88", "MD5 39 bytes" },
        { &g_abRandom72KB[0],        40, "39be72035e1058aee305b984373a6b16", "MD5 40 bytes" },
        { &g_abRandom72KB[0],        41, "5f8eda0e0084622bf6233594f06af754", "MD5 41 bytes" },
        { &g_abRandom72KB[0],        42, "c30e55ff2004e7b90009dee503964bbf", "MD5 42 bytes" },
        { &g_abRandom72KB[0],        43, "7a7f33277aa22a9199951022ab96c383", "MD5 43 bytes" },
        { &g_abRandom72KB[0],        44, "4f02d1a4e1ab98f7aed56ba12964af62", "MD5 44 bytes" },
        { &g_abRandom72KB[0],        45, "e0802f8b1739e5c284184d595624392e", "MD5 45 bytes" },
        { &g_abRandom72KB[0],        46, "50ea8f8f8a2bc14f7c07a9ec42826daa", "MD5 46 bytes" },
        { &g_abRandom72KB[0],        47, "365f247ba76a2024d2ff234b4e99bc48", "MD5 47 bytes" },
        { &g_abRandom72KB[0],        48, "cea411e3c67e77b48170699fe259e1c6", "MD5 48 bytes" },
        { &g_abRandom72KB[0],        49, "8453cb1977439f97279e39cbd8408ced", "MD5 49 bytes" },
        { &g_abRandom72KB[0],        50, "e6288244d7ae6f30dafa113146044f1b", "MD5 50 bytes" },
        { &g_abRandom72KB[0],        51, "f0c19a27d2474b5a2076ad0013ce966d", "MD5 51 bytes" },
        { &g_abRandom72KB[0],        52, "9265e6aea5bb3e16b4771dc0e15e9b23", "MD5 52 bytes" },
        { &g_abRandom72KB[0],        53, "3efcf10c3fd84a1999a11a8f2474fddd", "MD5 53 bytes" },
        { &g_abRandom72KB[0],        54, "cd6004b92196226fc754794bb185c09e", "MD5 54 bytes" },
        { &g_abRandom72KB[0],        55, "3ad247a37a75767374e3808930ff1240", "MD5 55 bytes" },
        { &g_abRandom72KB[0],        56, "11e8564e3db197beeaa8b9665b2ee2aa", "MD5 56 bytes" },
        { &g_abRandom72KB[0],        57, "790119e207ba5bf9b95768bd4acec278", "MD5 57 bytes" },
        { &g_abRandom72KB[0],        58, "2ff7a27055eb975c8d36f7105b905422", "MD5 58 bytes" },
        { &g_abRandom72KB[0],        59, "d1d5be70e576e9db7145b68bfaf4b8f7", "MD5 59 bytes" },
        { &g_abRandom72KB[0],        60, "d4383ffab62bda08cf2222186954abc8", "MD5 60 bytes" },
        { &g_abRandom72KB[0],        61, "69454edb58ddb72d7a715125ac5eefec", "MD5 61 bytes" },
        { &g_abRandom72KB[0],        62, "c2ea1754ec455e15e83c79630e726295", "MD5 62 bytes" },
        { &g_abRandom72KB[0],        63, "5c05c6ca2dc4ddbd52e447c2683aed47", "MD5 63 bytes" },
        { &g_abRandom72KB[0],        64, "a9f2d51e24b01bef5c96d4ab09b00f7b", "MD5 64 bytes" },
        { &g_abRandom72KB[0],        65, "f9ac6f5e2f43481d5966d7b9f946df6e", "MD5 65 bytes" },
        { &g_abRandom72KB[0],        66, "004d20523ee8c581da762700d4856e95", "MD5 66 bytes" },
        { &g_abRandom72KB[0],        67, "54180f89561ec585155c83cdb332eda7", "MD5 67 bytes" },
        { &g_abRandom72KB[0],        68, "e6e884c81250372d2f6d1e297a58fd3d", "MD5 68 bytes" },
        { &g_abRandom72KB[0],        69, "1ece0e157d215cec78ac5e7fc6096899", "MD5 69 bytes" },
        { &g_abRandom72KB[0],        70, "998fc35762eb99548110129d81873b4b", "MD5 70 bytes" },
        { &g_abRandom72KB[0],        71, "54e6dd0d33bc39c7bae536d20a070074", "MD5 71 bytes" },
        { &g_abRandom72KB[0],        72, "0eef8b9cf8f1e008cec190ab021e42d2", "MD5 72 bytes" },
        { &g_abRandom72KB[0],        73, "5519c0cdf891efe0bd9ead66cd20d9b4", "MD5 73 bytes" },
        { &g_abRandom72KB[0],        74, "14deef0ce4d7e2105875532f21da2212", "MD5 74 bytes" },
        { &g_abRandom72KB[0],        75, "938fdf08e55106e588fec9659ecc3f4b", "MD5 75 bytes" },
        { &g_abRandom72KB[0],        76, "7f4f6a2114cd8552aa948b20d7cfd725", "MD5 76 bytes" },
        { &g_abRandom72KB[0],        77, "5c34a0fe473b856d9665d789f7107146", "MD5 77 bytes" },
        { &g_abRandom72KB[0],        78, "c0fe660ac18254a65efdc8f71da77635", "MD5 78 bytes" },
        { &g_abRandom72KB[0],        79, "2c2670f20850aaa3f5e0d8a8fc07ae6e", "MD5 79 bytes" },
        { &g_abRandom72KB[0],        80, "8e74b2fb1edfd2135fd7240c62906fce", "MD5 80 bytes" },
        { &g_abRandom72KB[0],        81, "724214ccd4c5f1608cc577d80f1c1d63", "MD5 81 bytes" },
        { &g_abRandom72KB[0],        82, "2215de010fdcc3fe82a4bda76fc4c00c", "MD5 82 bytes" },
        { &g_abRandom72KB[0],        83, "f022e4fd762db1e713deff528eb8ab15", "MD5 83 bytes" },
        { &g_abRandom72KB[0],        84, "262c82ee993d73543fb86f60d43849bc", "MD5 84 bytes" },
        { &g_abRandom72KB[0],        85, "dac379497414e4135ea8e42ccbe39a11", "MD5 85 bytes" },
        { &g_abRandom72KB[0],        86, "c01cb2483bbdf778b536b07d7b12d31b", "MD5 86 bytes" },
        { &g_abRandom72KB[0],        87, "8f4753f5e64fa725f1a2a9a638e97686", "MD5 87 bytes" },
        { &g_abRandom72KB[0],        88, "6f53a21288b8a107a237df50b99fbc63", "MD5 88 bytes" },
        { &g_abRandom72KB[0],        89, "1df7274ecf95aecf4cef76070b4bc703", "MD5 89 bytes" },
        { &g_abRandom72KB[0],        90, "ebde27beebf8649892818f2c1b94dbac", "MD5 90 bytes" },
        { &g_abRandom72KB[0],        91, "c259c1aa0277ef8f3fda149d657f2958", "MD5 91 bytes" },
        { &g_abRandom72KB[0],        92, "47654004a1761a853d3a052cf3207e04", "MD5 92 bytes" },
        { &g_abRandom72KB[0],        93, "4e3011d42a53c359dfb7ed0cdd9fca3c", "MD5 93 bytes" },
        { &g_abRandom72KB[0],        94, "eab81b49b0efad606e623cad773f9bad", "MD5 94 bytes" },
        { &g_abRandom72KB[0],        95, "77a15147669b80b13cebf7f944865f7a", "MD5 95 bytes" },
        { &g_abRandom72KB[0],        96, "6975f970c6c7f8d11a52b8465df6f752", "MD5 96 bytes" },
        { &g_abRandom72KB[0],        97, "a5628bb324d1bd34bc41f81501c73c6d", "MD5 97 bytes" },
        { &g_abRandom72KB[0],        98, "c1b67f871130569c8dcd56b13997011b", "MD5 98 bytes" },
        { &g_abRandom72KB[0],        99, "4225c0cda6c16259a74c0733f3fa3025", "MD5 99 bytes" },
        { &g_abRandom72KB[0],       100, "39d1a9671f5eee3fd63f571e5700fb18", "MD5 100 bytes" },
        { &g_abRandom72KB[0],       101, "743bed2b5485505ebcd9c2dcaf61afd3", "MD5 101 bytes" },
        { &g_abRandom72KB[0],       102, "7a0d60739b87793168113a695257de4b", "MD5 102 bytes" },
        { &g_abRandom72KB[0],       103, "d483ae1c829dc3244ede1f46488c0f0c", "MD5 103 bytes" },
        { &g_abRandom72KB[0],       104, "1502b082b28c1b60decad1c3ec8d637b", "MD5 104 bytes" },
        { &g_abRandom72KB[0],       105, "df0d769bad97093d00560e4221023dbf", "MD5 105 bytes" },
        { &g_abRandom72KB[0],       106, "bd3641699ffe5adb1c8a3c8917abb1ff", "MD5 106 bytes" },
        { &g_abRandom72KB[0],       107, "b5e585c6da2c40a7e5aab059e7bd15ee", "MD5 107 bytes" },
        { &g_abRandom72KB[0],       108, "e3f8ec4a683a3512f73639cd6ef69638", "MD5 108 bytes" },
        { &g_abRandom72KB[0],       109, "a4a4b065603644c6d50dc0a4426badf6", "MD5 109 bytes" },
        { &g_abRandom72KB[0],       110, "7b22226bdb2504211fa8b99d2860b2c0", "MD5 110 bytes" },
        { &g_abRandom72KB[0],       111, "ab89296851f6ffc435431b82a3247b15", "MD5 111 bytes" },
        { &g_abRandom72KB[0],       112, "20249b22ba14b007555e54c9366ddabd", "MD5 112 bytes" },
        { &g_abRandom72KB[0],       113, "6bf5d9a3a30b5eb7af2a4092eda69ecd", "MD5 113 bytes" },
        { &g_abRandom72KB[0],       114, "f7d97a06da8494176e0fe01934e31e1f", "MD5 114 bytes" },
        { &g_abRandom72KB[0],       115, "2616a77b387bb99afdf8bb1f54f7a000", "MD5 115 bytes" },
        { &g_abRandom72KB[0],       116, "bf81d63f172e2ce9b958f0da5c92c344", "MD5 116 bytes" },
        { &g_abRandom72KB[0],       117, "8633ee1f631841cb1887a487f22a9f9f", "MD5 117 bytes" },
        { &g_abRandom72KB[0],       118, "0f3b0311c0cf9d9eba3240404cae137d", "MD5 118 bytes" },
        { &g_abRandom72KB[0],       119, "f79c00a97df8d45a211e6d01409c119b", "MD5 119 bytes" },
        { &g_abRandom72KB[0],       120, "4d88da4ff44c801f692b46869f2fbb98", "MD5 120 bytes" },
        { &g_abRandom72KB[0],       121, "15ca57bd78833831f54dfdcfbd5e4d29", "MD5 121 bytes" },
        { &g_abRandom72KB[0],       122, "0877ba42c2b57ab3d0041ddbc4bd930b", "MD5 122 bytes" },
        { &g_abRandom72KB[0],       123, "66086909e3740dd20004b0968e3316b5", "MD5 123 bytes" },
        { &g_abRandom72KB[0],       124, "bafe834041396ce465f0995677ea6fba", "MD5 124 bytes" },
        { &g_abRandom72KB[0],       125, "634eff105e618d75ab738512aff11048", "MD5 125 bytes" },
        { &g_abRandom72KB[0],       126, "850257fc17096cce1e2d6de664e7ec04", "MD5 126 bytes" },
        { &g_abRandom72KB[0],       127, "f12f54a3ecee91de840103aaa0d3726a", "MD5 127 bytes" },
        { &g_abRandom72KB[0],       128, "1a266528b8119279e9639418a2d85e77", "MD5 128 bytes" },
        { &g_abRandom72KB[0],       129, "e4791e35863addd6fa74ff1662d6a908", "MD5 129 bytes" },
        { &g_abRandom72KB[0],      1024, "310e55220bd80529e76a544209f8e532", "MD5 1024 bytes" },
        { &g_abRandom72KB[0],     73001, "f3d05b52be86f1db66a9ebf5ababaaa8", "MD5 73001 bytes" },
        { &g_abRandom72KB[0],     73728, "aef57c3b2ec6e560b51b8094fe34def7", "MD5 73728 bytes" },
        { &g_abRandom72KB[0x20c9], 9991, "6461339c6615d23c704298a313e07cf5", "MD5 8393 bytes @9991" },
    };
    testGeneric("1.2.840.113549.2.5", s_abTests, RT_ELEMENTS(s_abTests), "MD5", RTDIGESTTYPE_MD5,
                VINF_CR_DIGEST_COMPROMISED);
}


/**
 * Tests SHA-1
 */
static void testSha1(void)
{
    RTTestISub("SHA-1");

    /*
     * Some quick direct API tests.
     */
    uint8_t     abHash[RTSHA1_HASH_SIZE];
    char        szDigest[RTSHA1_DIGEST_LEN + 1];
    const char *pszString;

    pszString = "";
    RTSha1(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha1ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    pszString = "The quick brown fox jumps over the lazy dog";
    RTSha1(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha1ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

    pszString = "a";
    RTSha1(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha1ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8");

    pszString = "abc";
    RTSha1(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha1ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "a9993e364706816aba3e25717850c26c9cd0d89d");


    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],         0, "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1 0 bytes" },
        { &g_abRandom72KB[0],         1, "768aab37c292010133979e821ad5ac081ade388a", "SHA-1 1 bytes" },
        { &g_abRandom72KB[0],         2, "e1d72a3d75ec967fe2826670ed35ce5242204ad2", "SHA-1 2 bytes" },
        { &g_abRandom72KB[0],         3, "d76ef947caa78f5e786d4ea417edec1d8f5a399b", "SHA-1 3 bytes" },
        { &g_abRandom72KB[0],         4, "392932bb1bd20acce81b1c9df604cb149ca3089e", "SHA-1 4 bytes" },
        { &g_abRandom72KB[0],         5, "f13362588bcfe78afd6f2b4f264bfa10f9e18bcf", "SHA-1 5 bytes" },
        { &g_abRandom72KB[0],         6, "9f582f1afe75e3769d6afc95f266b114c1f42349", "SHA-1 6 bytes" },
        { &g_abRandom72KB[0],         7, "e9326429c1709dd2278220e72eab0de2eebcfa67", "SHA-1 7 bytes" },
        { &g_abRandom72KB[0],         8, "7882a61d6bd15b1377810d6a2083c20306b56b2d", "SHA-1 8 bytes" },
        { &g_abRandom72KB[0],         9, "71df6819243e6ff29be17628fd9e051d732bf26e", "SHA-1 9 bytes" },
        { &g_abRandom72KB[0],        10, "76f9029c260c99700fe14341d39067bcc746b766", "SHA-1 10 bytes" },
        { &g_abRandom72KB[0],        11, "b11df4de9916a760282f046f86d7e65ebc276daa", "SHA-1 11 bytes" },
        { &g_abRandom72KB[0],        12, "67af3a95329eb9d23b4db48e372befe02f93860d", "SHA-1 12 bytes" },
        { &g_abRandom72KB[0],        13, "add021eaaa1c315110708e307ff6cf838fac4764", "SHA-1 13 bytes" },
        { &g_abRandom72KB[0],        14, "b66297ac3211b967b27010316a4feeb87af377b1", "SHA-1 14 bytes" },
        { &g_abRandom72KB[0],        15, "8350ccd151d4fbc8fcaa76486854c82ce28bf1c2", "SHA-1 15 bytes" },
        { &g_abRandom72KB[0],        16, "e005dbbc5f955cf88e06ad92eb2fecb8d7abc829", "SHA-1 16 bytes" },
        { &g_abRandom72KB[0],        17, "d9b9101f8026e9c9e164370e11b6b14877642339", "SHA-1 17 bytes" },
        { &g_abRandom72KB[0],        18, "f999dfca047ee12be46b4141d9387d8694a8c03f", "SHA-1 18 bytes" },
        { &g_abRandom72KB[0],        19, "366324b23a2ef746fe5bedce961482c0e8816fd1", "SHA-1 19 bytes" },
        { &g_abRandom72KB[0],        20, "6f987e9f030a9b25790ec79b0c9c0a5d8085a565", "SHA-1 20 bytes" },
        { &g_abRandom72KB[0],        21, "799a3aa036d56809e459f757a4086a9b83549fd8", "SHA-1 21 bytes" },
        { &g_abRandom72KB[0],        22, "1fd879155acdd70d26b64533283b033db95cb264", "SHA-1 22 bytes" },
        { &g_abRandom72KB[0],        23, "888db854b475c3923f73090ad855d68c8708ecf4", "SHA-1 23 bytes" },
        { &g_abRandom72KB[0],        24, "c81e41a34ec7ae112a59bdcc1e31c9dd9f75cafc", "SHA-1 24 bytes" },
        { &g_abRandom72KB[0],        25, "a19edeb182e13e11c7371b6795f9f973544e25e2", "SHA-1 25 bytes" },
        { &g_abRandom72KB[0],        26, "7a98f96aa921d6932dd966d4f41f7afafef50992", "SHA-1 26 bytes" },
        { &g_abRandom72KB[0],        27, "847272961c5a5a1b10d2fefab9d95475bb246e6b", "SHA-1 27 bytes" },
        { &g_abRandom72KB[0],        28, "eb2b1dbc0ea2a23e308e1bf9d6e78167f62b9a5c", "SHA-1 28 bytes" },
        { &g_abRandom72KB[0],        29, "5050998e561c0887e3365d8c87f63ce00cc4084e", "SHA-1 29 bytes" },
        { &g_abRandom72KB[0],        30, "42278d4e8a70aeaa64f9194f1c90d827bba85a45", "SHA-1 30 bytes" },
        { &g_abRandom72KB[0],        31, "f87ad36842c6e36fbbe7b3edd6514d4670367b79", "SHA-1 31 bytes" },
        { &g_abRandom72KB[0],        32, "9faa3230afd8279a4b006d37b9f243b2f6430616", "SHA-1 32 bytes" },
        { &g_abRandom72KB[0],        33, "5ca875e82e84cfa9aee1adc5a54fdb8b276f9c9a", "SHA-1 33 bytes" },
        { &g_abRandom72KB[0],        34, "833d2bb01ec548b204750e0a3b9226059377aa66", "SHA-1 34 bytes" },
        { &g_abRandom72KB[0],        35, "aeb0843fc2978addc14cc42a184887a57678722b", "SHA-1 35 bytes" },
        { &g_abRandom72KB[0],        36, "77d4e5da16564461a0a44ce0ac4d0e885c46c127", "SHA-1 36 bytes" },
        { &g_abRandom72KB[0],        37, "dfe064bcfed5f8fa2fe8efca6a75a9ca51d2ef43", "SHA-1 37 bytes" },
        { &g_abRandom72KB[0],        38, "a89cf8c2af3a1ede699eb7421d0038030035b081", "SHA-1 38 bytes" },
        { &g_abRandom72KB[0],        39, "5f4eb0893c2ea7445fea51508573f9f8eab98862", "SHA-1 39 bytes" },
        { &g_abRandom72KB[0],        40, "2cf921e5e564b947c6ae98e709b49e9e7c7f9c2f", "SHA-1 40 bytes" },
        { &g_abRandom72KB[0],        41, "7788ea70ee17e475b360e4e59f3f7792137e836d", "SHA-1 41 bytes" },
        { &g_abRandom72KB[0],        42, "6506ab486671f2a2b7e8a19626da6ae531299f39", "SHA-1 42 bytes" },
        { &g_abRandom72KB[0],        43, "950096584b7a7aabd610ef466be73e544d9d0809", "SHA-1 43 bytes" },
        { &g_abRandom72KB[0],        44, "ff99e44ed9e8f30f81a9b50f61c376681ee16a91", "SHA-1 44 bytes" },
        { &g_abRandom72KB[0],        45, "f043b267447eaf18c89528e8f52fa0c8f33c0022", "SHA-1 45 bytes" },
        { &g_abRandom72KB[0],        46, "a9527c0a351fe0b4606eba0a8900b78c0f311f1a", "SHA-1 46 bytes" },
        { &g_abRandom72KB[0],        47, "579f1fea36188490d80bfe4f04bd5a0e625006d2", "SHA-1 47 bytes" },
        { &g_abRandom72KB[0],        48, "c43765e7b3565e7a78ccf96011a4afad18f7f781", "SHA-1 48 bytes" },
        { &g_abRandom72KB[0],        49, "a3130b7c92c6441e8c319c6768e611d74e419ce7", "SHA-1 49 bytes" },
        { &g_abRandom72KB[0],        50, "ef8fedfa73384d6c68c7e956ddc1c45239d40be0", "SHA-1 50 bytes" },
        { &g_abRandom72KB[0],        51, "a4dea447ae78523fec06a7e65d798f1a61c3b96b", "SHA-1 51 bytes" },
        { &g_abRandom72KB[0],        52, "86b4aa02ab3cc63badf544131087689ecaf25f24", "SHA-1 52 bytes" },
        { &g_abRandom72KB[0],        53, "a0faf581ceaa922d0530f90d76fbd93e93810d58", "SHA-1 53 bytes" },
        { &g_abRandom72KB[0],        54, "80ab7368ad003e5083d9804579a565de6a278c40", "SHA-1 54 bytes" },
        { &g_abRandom72KB[0],        55, "8b1bec05d4a56b1beb500f2d639b3f7887d68e81", "SHA-1 55 bytes" },
        { &g_abRandom72KB[0],        56, "66ba1322833fd92af27ae37e4382e95b525969b4", "SHA-1 56 bytes" },
        { &g_abRandom72KB[0],        57, "ecab320750baead21b14d31450512ec0584ccd8d", "SHA-1 57 bytes" },
        { &g_abRandom72KB[0],        58, "a4f13253e34d4dc4f65c4b4b3da694fdf8af3bf9", "SHA-1 58 bytes" },
        { &g_abRandom72KB[0],        59, "73f6b88c28af42df6a0ab207e33176078082f67d", "SHA-1 59 bytes" },
        { &g_abRandom72KB[0],        60, "d19f291905289ded6ed13b3dc8c47f94b999c100", "SHA-1 60 bytes" },
        { &g_abRandom72KB[0],        61, "bd0bef35c45892c13c27825de9920a8b9fc91d73", "SHA-1 61 bytes" },
        { &g_abRandom72KB[0],        62, "f62f8426087fea0bf3733f2b2f6d37fd155d01f6", "SHA-1 62 bytes" },
        { &g_abRandom72KB[0],        63, "22891a14ef0e27fabef93f768fd645e042254011", "SHA-1 63 bytes" },
        { &g_abRandom72KB[0],        64, "364a08a4a85f6fe61272125ae2e549a1c4cc3fe4", "SHA-1 64 bytes" },
        { &g_abRandom72KB[0],        65, "8bf19ebbe263845cd3c35b853f1892d959dc8bd4", "SHA-1 65 bytes" },
        { &g_abRandom72KB[0],        66, "ed3a0f2dc02fcc12e134bfd2f4e23a6294122e57", "SHA-1 66 bytes" },
        { &g_abRandom72KB[0],        67, "596308eb7eabf39431ec953d2c6bacb9bb7c6c70", "SHA-1 67 bytes" },
        { &g_abRandom72KB[0],        68, "d1d736e7d1a7ffe5613f6337248d67d7ebf8be01", "SHA-1 68 bytes" },
        { &g_abRandom72KB[0],        69, "6ebad117db6e9cdfedae52f44df3ba51eb95efa2", "SHA-1 69 bytes" },
        { &g_abRandom72KB[0],        70, "f5a05b4b156c07c23e8779f776a7f894922c148f", "SHA-1 70 bytes" },
        { &g_abRandom72KB[0],        71, "01aef4f832ad68e6708f31a97bc363d9da20b6ba", "SHA-1 71 bytes" },
        { &g_abRandom72KB[0],        72, "1bbf9dc55aa008a5e0c7fc4d34cb9e283f785b4e", "SHA-1 72 bytes" },
        { &g_abRandom72KB[0],        73, "1504867acdbe4b1db5015d272182d17a4620a8b9", "SHA-1 73 bytes" },
        { &g_abRandom72KB[0],        74, "7c96021e92a9bf98e5fb34ac17a3db487f1c7ac7", "SHA-1 74 bytes" },
        { &g_abRandom72KB[0],        75, "d53acd002a6fc89120bc1ae7698aa0996cfacf00", "SHA-1 75 bytes" },
        { &g_abRandom72KB[0],        76, "f8036f1ffdcaaadc1beb1542264f43060bf7c0c3", "SHA-1 76 bytes" },
        { &g_abRandom72KB[0],        77, "6bec2adc19d2229054499d035f9904b6a69ea89a", "SHA-1 77 bytes" },
        { &g_abRandom72KB[0],        78, "ad09365576f77c608afe0242caaae3604ce0fd17", "SHA-1 78 bytes" },
        { &g_abRandom72KB[0],        79, "8ef16b886a12c3ceadf67d6bf8ee016c72b4c02a", "SHA-1 79 bytes" },
        { &g_abRandom72KB[0],        80, "bd60ca67ea01e456de55bfbf0cc1095eadeda98a", "SHA-1 80 bytes" },
        { &g_abRandom72KB[0],        81, "6cb9bb8e6447fff7a04e46de9e8410bffe7185f9", "SHA-1 81 bytes" },
        { &g_abRandom72KB[0],        82, "2365ced0aa582dfdfe0b7ba0607e6953d64e1029", "SHA-1 82 bytes" },
        { &g_abRandom72KB[0],        83, "4597ee3f912cce76b81d05eb0d4941af76688995", "SHA-1 83 bytes" },
        { &g_abRandom72KB[0],        84, "287a99bcb83b395e9e0b1f36d492417f4bd25d6f", "SHA-1 84 bytes" },
        { &g_abRandom72KB[0],        85, "689caf3dd39ec1cf99baba13c549d9a25797e3e1", "SHA-1 85 bytes" },
        { &g_abRandom72KB[0],        86, "1fc2a6da9ba7a623f5eac73b56134772de374d1c", "SHA-1 86 bytes" },
        { &g_abRandom72KB[0],        87, "a2c3cb4c5b1dc6dfdf553614bf846ed794874f16", "SHA-1 87 bytes" },
        { &g_abRandom72KB[0],        88, "3e9b451561c66d9eb9e9052c3027d4d91f495771", "SHA-1 88 bytes" },
        { &g_abRandom72KB[0],        89, "a47b63438fdaaab0bb4ea5e31b84aaae118bc9a9", "SHA-1 89 bytes" },
        { &g_abRandom72KB[0],        90, "5c1b3fbc228add14796ed049b00924ed7140340d", "SHA-1 90 bytes" },
        { &g_abRandom72KB[0],        91, "b873c08cb5329f82bd6adcd134f81e29597c4964", "SHA-1 91 bytes" },
        { &g_abRandom72KB[0],        92, "f77066f9a0908d50e6018300cb82f436df9c8045", "SHA-1 92 bytes" },
        { &g_abRandom72KB[0],        93, "b4c97048c643c9e5b6355683d36a1faee6d023ac", "SHA-1 93 bytes" },
        { &g_abRandom72KB[0],        94, "d6b48d223bd97d3b53c0868c0528f18a3d5ebc88", "SHA-1 94 bytes" },
        { &g_abRandom72KB[0],        95, "9562095eccd53bf5a968bc3eda65f3e327326b8e", "SHA-1 95 bytes" },
        { &g_abRandom72KB[0],        96, "b394b8e175238834daf9c4d3b5fc8dbcfd982ae9", "SHA-1 96 bytes" },
        { &g_abRandom72KB[0],        97, "c0127c82cb667da52892e462b5e9cdafb4f0deaa", "SHA-1 97 bytes" },
        { &g_abRandom72KB[0],        98, "f51474c9995eb341748dea0f07f60cac46d5fa87", "SHA-1 98 bytes" },
        { &g_abRandom72KB[0],        99, "6ae3406d41332cfe86c40d275b5e1e394893361b", "SHA-1 99 bytes" },
        { &g_abRandom72KB[0],       100, "0d5823c081f69ad4e7fbd7ee0ed12092f6e2ed75", "SHA-1 100 bytes" },
        { &g_abRandom72KB[0],       101, "fc8527f9c789abf67e2bcc78e2048f4eda8f7d7d", "SHA-1 101 bytes" },
        { &g_abRandom72KB[0],       102, "a322eb5d5e65833310953a3a7bb1081e05b69318", "SHA-1 102 bytes" },
        { &g_abRandom72KB[0],       103, "5a4fac64b273263043c771a5f9bae1fb243cd7d6", "SHA-1 103 bytes" },
        { &g_abRandom72KB[0],       104, "bad4d3f3091817273dbac07b8712eec27b16cb6b", "SHA-1 104 bytes" },
        { &g_abRandom72KB[0],       105, "ca0721a40610ea6290b97b541806b195e659af19", "SHA-1 105 bytes" },
        { &g_abRandom72KB[0],       106, "cf3fb01cc0d95898e3b698ebdbd3c1e2624eacd2", "SHA-1 106 bytes" },
        { &g_abRandom72KB[0],       107, "e3509ada3e264733cd36e2dab301650797fa8351", "SHA-1 107 bytes" },
        { &g_abRandom72KB[0],       108, "045ca9690e7ac3336fd8907c28650c3ad489cbfe", "SHA-1 108 bytes" },
        { &g_abRandom72KB[0],       109, "44a6c85d7d9053679fde01139ee0cf6176754227", "SHA-1 109 bytes" },
        { &g_abRandom72KB[0],       110, "2ea6df640202b8799a885887a7c62a05247e60da", "SHA-1 110 bytes" },
        { &g_abRandom72KB[0],       111, "38b0a91b3b2949cf752d3273157b9fa911972ad3", "SHA-1 111 bytes" },
        { &g_abRandom72KB[0],       112, "962b950eab71062e7bc619e33cee36ceded923ee", "SHA-1 112 bytes" },
        { &g_abRandom72KB[0],       113, "ccdecb735b377f1023ac2ad1e5ef0cb264bccf63", "SHA-1 113 bytes" },
        { &g_abRandom72KB[0],       114, "4491b4e057d62e9c875b9153e9d76860657ab1a7", "SHA-1 114 bytes" },
        { &g_abRandom72KB[0],       115, "f2d6c27c001222592fe06ba3e8c408b7033d14e1", "SHA-1 115 bytes" },
        { &g_abRandom72KB[0],       116, "c9651c478f36ddacbca4cd680b37db6869473ed4", "SHA-1 116 bytes" },
        { &g_abRandom72KB[0],       117, "b69442fcaa3e3bd617d3d3a0d436eb170e580083", "SHA-1 117 bytes" },
        { &g_abRandom72KB[0],       118, "a48a7f361bf44d4e055a7e6aaa51f5b2f8de4419", "SHA-1 118 bytes" },
        { &g_abRandom72KB[0],       119, "257d756bebc29cd408394b23f1891739cfd00755", "SHA-1 119 bytes" },
        { &g_abRandom72KB[0],       120, "1ef9f2becc7217db45b97d7a0f47da9ed23f8957", "SHA-1 120 bytes" },
        { &g_abRandom72KB[0],       121, "06ac75dc3ffee76eeefa11bb10dae6f4fb94ece8", "SHA-1 121 bytes" },
        { &g_abRandom72KB[0],       122, "883c7780a8926ac22cb6dff4dbb8889c579ebb99", "SHA-1 122 bytes" },
        { &g_abRandom72KB[0],       123, "004e08413617c1bb262d95f66a5ed5fff82b718b", "SHA-1 123 bytes" },
        { &g_abRandom72KB[0],       124, "f68155afc2ee6292c881db5721f3e0f6e77a0bca", "SHA-1 124 bytes" },
        { &g_abRandom72KB[0],       125, "e9ec6e4b4b1123adceda9d0f7db99c10712af649", "SHA-1 125 bytes" },
        { &g_abRandom72KB[0],       126, "bcf570708b73c20d9805172f50935ab44d122e6b", "SHA-1 126 bytes" },
        { &g_abRandom72KB[0],       127, "4b5f949b1b8a0a2b3716aba8cad91f45ae0f9408", "SHA-1 127 bytes" },
        { &g_abRandom72KB[0],       128, "fd78703c7d7ce4a29bd79230898b7ac382f117cc", "SHA-1 128 bytes" },
        { &g_abRandom72KB[0],       129, "461d0063059b9d4b8255f621bd329dfecdfaedef", "SHA-1 129 bytes" },
        { &g_abRandom72KB[0],      1024, "2abd8ddb6c13b55596f31c74d96a04411ad39e8e", "SHA-1 1024 bytes" },
        { &g_abRandom72KB[0],     73001, "e8c68bf7f6bd3b5c2a482c8a2ca9849b1e5afff1", "SHA-1 73001 bytes" },
        { &g_abRandom72KB[0],     73728, "e89f36633ad2745ab2aac50ea7b2fe23e49ba69f", "SHA-1 73728 bytes" },
        { &g_abRandom72KB[0x20c9],  9991, "62001184bacacce3774566d916055d425a85eba5", "SHA-1 8393 bytes @9991" },
    };
    testGeneric("1.3.14.3.2.26", s_abTests, RT_ELEMENTS(s_abTests), "SHA-1", RTDIGESTTYPE_SHA1, VINF_CR_DIGEST_DEPRECATED);
}


/**
 * Tests SHA-256
 */
static void testSha256(void)
{
    RTTestISub("SHA-256");

    /*
     * Some quick direct API tests.
     */
    uint8_t     abHash[RTSHA256_HASH_SIZE];
    char        szDigest[RTSHA256_DIGEST_LEN + 1];
    const char *pszString;

    pszString = "";
    RTSha256(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha256ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    pszString = "The quick brown fox jumps over the lazy dog";
    RTSha256(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha256ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],         0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 0 bytes" },
        { &g_abRandom72KB[0],         1, "e6f207509afa3908da116ce61a7576954248d9fe64a3c652b493cca57ce36e2e", "SHA-256 1 bytes" },
        { &g_abRandom72KB[0],         2, "51f3435c5f25f051c7e9209728c37a062d7d93c1d7c4075a5e383f2600cd1422", "SHA-256 2 bytes" },
        { &g_abRandom72KB[0],         3, "37cd6662639ecea8c1c26ae10cf307f6884b5ab6ddba99885a9fd92349044de1", "SHA-256 3 bytes" },
        { &g_abRandom72KB[0],         4, "a31bb0a349218352bb721a901b7bf8b9290d1b81eec4ac7e6005a2cfd5b3baa9", "SHA-256 4 bytes" },
        { &g_abRandom72KB[0],         5, "00a42e462920c577ae608ddcaedf9478b53d25541b696e454dd8d7fb14f61c35", "SHA-256 5 bytes" },
        { &g_abRandom72KB[0],         6, "6c80d81f45f18cfaaf6c3de33dc505936f7faa41f714cdc7b0c8a3bdba15ade4", "SHA-256 6 bytes" },
        { &g_abRandom72KB[0],         7, "540ea7cc49b1aa11cfd4004b0faebc1b72cfa4aa0bcd940c4341bbd9ac4f6913", "SHA-256 7 bytes" },
        { &g_abRandom72KB[0],         8, "b5822d2199e3b3d7e6260d083dbf8bd79aa5ea7e27c3b879997c90098cbf1f06", "SHA-256 8 bytes" },
        { &g_abRandom72KB[0],         9, "6d983a270c13e529d6a7b1d86f702c588884e6e109cfc975dfebce3b65afa538", "SHA-256 9 bytes" },
        { &g_abRandom72KB[0],        10, "66da7649b7bd8f814d8af92347c8fca9102f27449dacd72b7dfe05bb1937ff84", "SHA-256 10 bytes" },
        { &g_abRandom72KB[0],        11, "1fcec1b28467ffe6e80422a5150bf885ab58452b335a525e363aa06d5ae70c20", "SHA-256 11 bytes" },
        { &g_abRandom72KB[0],        12, "22ae8615a76d6e274a819cf2ee4a8af8dc3b6b985b59136218013e309c94a5d8", "SHA-256 12 bytes" },
        { &g_abRandom72KB[0],        13, "e4b439cdd6ed214dbc53d4312e65dea93129e4093e9dedea6c19f5d8961ee86c", "SHA-256 13 bytes" },
        { &g_abRandom72KB[0],        14, "8eee4ceba622ee53987910b3f8491e38094a569b9e9caafd857fd71626cc1066", "SHA-256 14 bytes" },
        { &g_abRandom72KB[0],        15, "c819dadb248aca10440334ba0d271705f83640c0a8a718679e6faaa48debdbd2", "SHA-256 15 bytes" },
        { &g_abRandom72KB[0],        16, "4019ab801bf80475e963c9d1458785cd4a24c9b00adda273fa1aa4f0c345ee8b", "SHA-256 16 bytes" },
        { &g_abRandom72KB[0],        17, "9fc7b643bfa14072f47ad05f96e6760dd78c15baee67c91c759cd49031d33138", "SHA-256 17 bytes" },
        { &g_abRandom72KB[0],        18, "9985cf02258c07b7134061bfde8434599e9e5e4d8342d8df659637236394d093", "SHA-256 18 bytes" },
        { &g_abRandom72KB[0],        19, "e345757094c316b6b5f0959da146586687fb282c1e415645c15f898932f47d9c", "SHA-256 19 bytes" },
        { &g_abRandom72KB[0],        20, "ae0d944c727221bc51da45956d40f3586ce69d76c8853c3a1ddf97cf64b562b4", "SHA-256 20 bytes" },
        { &g_abRandom72KB[0],        21, "95cc050b256d723d38357896c12fe6008e34f9dde4be67dbd057e34007956d33", "SHA-256 21 bytes" },
        { &g_abRandom72KB[0],        22, "f1aecdc3bca51bb2a9c4904d0c71d7acb61ddae166cfa748df1fc89b151844d8", "SHA-256 22 bytes" },
        { &g_abRandom72KB[0],        23, "269189cca3f6bb406782e7ed40beab1934b01af599daeb9bfdd68b102b11224d", "SHA-256 23 bytes" },
        { &g_abRandom72KB[0],        24, "70600ca44cdd0b8c81e20127768e8a42d4546c408681d7681854fa06a2953319", "SHA-256 24 bytes" },
        { &g_abRandom72KB[0],        25, "dc26c41fcbe49afbe6b05fb581484caad8782a4350959277cd4070fb46c798ac", "SHA-256 25 bytes" },
        { &g_abRandom72KB[0],        26, "ed78ad5525b97311df46d02db582bc951cc2671250912f045f5d29be87063552", "SHA-256 26 bytes" },
        { &g_abRandom72KB[0],        27, "6cc53426c84a5c449367fb9b1a9bb50411a1c4de858da1153fa1ccb7cf6646e3", "SHA-256 27 bytes" },
        { &g_abRandom72KB[0],        28, "ecb2ed45ceb2e0ce2415eb8dccfadd7353410cdffd3641541eb84f8f225ef748", "SHA-256 28 bytes" },
        { &g_abRandom72KB[0],        29, "335eacfe3cf7d86c4b4a83e0a7810371cc1f4773240790b56edc5e804fa5c3f8", "SHA-256 29 bytes" },
        { &g_abRandom72KB[0],        30, "070f657fa091df733f56565338bd9e2bce13246426ff2daba1c9c11c32f502f3", "SHA-256 30 bytes" },
        { &g_abRandom72KB[0],        31, "dfb7b82fd9524de158614fb1c0f232df6903a4247313d7e52891ea1a274bcad8", "SHA-256 31 bytes" },
        { &g_abRandom72KB[0],        32, "c21553469f65bc2bb449fa366467fd0ae16e5fc82c87bbba3ee59e9ccacf881d", "SHA-256 32 bytes" },
        { &g_abRandom72KB[0],        33, "a3532bf13ab6c55b001b32a3dd71204fcc5543c1e3c6afa4ec9d68b6d67557ae", "SHA-256 33 bytes" },
        { &g_abRandom72KB[0],        34, "7ea71b61c7419219d4fdafbff4dcc181b2db6a9cfa0bfd8389e1c679031f2458", "SHA-256 34 bytes" },
        { &g_abRandom72KB[0],        35, "07cbd9a6996f1f98d2734d818c2b833707fc11ef157517efd30dcffd33409b65", "SHA-256 35 bytes" },
        { &g_abRandom72KB[0],        36, "9347e18a41e55e4b199274583ad8ac8a6f72712b12ff08454dc9a94ed28f2405", "SHA-256 36 bytes" },
        { &g_abRandom72KB[0],        37, "b4d8a36d40ed9def9cc08fc64e0bb5792d4b2f7e6bdbb3003bae503f2ef2afd4", "SHA-256 37 bytes" },
        { &g_abRandom72KB[0],        38, "829c2b0c1785ebff89f5caf2f7f5b7529e1ccbbefb171e23b054058327c2c478", "SHA-256 38 bytes" },
        { &g_abRandom72KB[0],        39, "11d3c0ec52501e78f668a241957939c113c08b0a83420924397f97869b3d018a", "SHA-256 39 bytes" },
        { &g_abRandom72KB[0],        40, "476271f9371bf76d4aa627bafb5924959c033b0298e0b9ea4b5eb3012e095b4e", "SHA-256 40 bytes" },
        { &g_abRandom72KB[0],        41, "b818241e2f5c0b415ed49095abbfe465653946ddf67b78d1b0ebc9c2fa70371f", "SHA-256 41 bytes" },
        { &g_abRandom72KB[0],        42, "a02f6bc7f79a7b96dd16fa4f7ecbc0dfcc9719c5d41c51c4df9504c81b10cd56", "SHA-256 42 bytes" },
        { &g_abRandom72KB[0],        43, "f6f32fb00cdec042f70d38f20f92b73a6534d84b1af1fb4386a22cb1419f2f33", "SHA-256 43 bytes" },
        { &g_abRandom72KB[0],        44, "946d66e920b1f034186876cba8509b8e92dd6ddb41f29a48c9a9fb9a40ed27e6", "SHA-256 44 bytes" },
        { &g_abRandom72KB[0],        45, "b9e5f490e4505ce834759d0239e6b91499eafaedfe2e20f53b649ed719226f09", "SHA-256 45 bytes" },
        { &g_abRandom72KB[0],        46, "3dee1256de4dabbd8ae8eb463f294aaa187c6eb2a8b158e89bd01d24cc0e5ea6", "SHA-256 46 bytes" },
        { &g_abRandom72KB[0],        47, "f32ad8377e24bca4b664069e23d7e306d0ed0bec04b86834d245bea3b25e03b9", "SHA-256 47 bytes" },
        { &g_abRandom72KB[0],        48, "bd539ac8985f6f251d4ed3486dd7d45a3a316eecb872815873cf75858bbe90fc", "SHA-256 48 bytes" },
        { &g_abRandom72KB[0],        49, "685e1aaa3ca611b74c4bcfe62d6597f54c8f16236e1d990f21c61b5952a205f0", "SHA-256 49 bytes" },
        { &g_abRandom72KB[0],        50, "32d6fc76312316bf96cb57bb5f0f9c6a799ffcdc29571de437d5b2dd15ec788a", "SHA-256 50 bytes" },
        { &g_abRandom72KB[0],        51, "921be210954a8de9563c2dd652be14e1c9abf659b8485c5c7ac70fd381291ac6", "SHA-256 51 bytes" },
        { &g_abRandom72KB[0],        52, "848d5c2eafb58011f5f513735405c43e55fc6d6c23d1792cd891a921f69a74e3", "SHA-256 52 bytes" },
        { &g_abRandom72KB[0],        53, "052edfc879cb6a63ce33ef2a28da5ef418dd7ad20209ccdeb8247ca7325bbb97", "SHA-256 53 bytes" },
        { &g_abRandom72KB[0],        54, "862af02ee839897dde32d564b18316889176eac0e62b7a982cd79d5d3f9800d4", "SHA-256 54 bytes" },
        { &g_abRandom72KB[0],        55, "82042a5fcaa02dd245583b4fa198ddad31052a687979f76f0085d14c8ed22221", "SHA-256 55 bytes" },
        { &g_abRandom72KB[0],        56, "49869627a2ee03d8374d6fe5557dabb5211d59cac1186fe5502064eefe52e3e5", "SHA-256 56 bytes" },
        { &g_abRandom72KB[0],        57, "12f63c4012024e962996eb341be18c41f098e9b69739fe5262cb124567cb26ac", "SHA-256 57 bytes" },
        { &g_abRandom72KB[0],        58, "c8830941fdb38ccd160036d18e4969154361e615de37d8ac9edcf9b601727acd", "SHA-256 58 bytes" },
        { &g_abRandom72KB[0],        59, "9b953c0e044a932bd90c256dfc1c6fe1e49aaf15d3f6fe07b1b524da29c96d3e", "SHA-256 59 bytes" },
        { &g_abRandom72KB[0],        60, "590f234c6c5ab3ea01981e01468be82c13da5b07370991d92b8ecfd0e3d36030", "SHA-256 60 bytes" },
        { &g_abRandom72KB[0],        61, "6490bdb3fc554899f53705a6729d67008b20b802359fcb944fed95fe7e05caf5", "SHA-256 61 bytes" },
        { &g_abRandom72KB[0],        62, "c85c5c3d945b2c728388cb9af0913e28f6c74d907a01df3756748c4ef82635ea", "SHA-256 62 bytes" },
        { &g_abRandom72KB[0],        63, "46dcc81342ef03e4a313827e0bcdc72f5b97145483fd9fc280f2a39b9f2e6a0f", "SHA-256 63 bytes" },
        { &g_abRandom72KB[0],        64, "89eda27523b81a333fccd4be824a84a60f602a4bfe774ae7aa63c98a9b12ebf6", "SHA-256 64 bytes" },
        { &g_abRandom72KB[0],        65, "10ce93270e7fca1e7bdc0b7475845eeb3adcf39c47867aa2b36b41456b7627b0", "SHA-256 65 bytes" },
        { &g_abRandom72KB[0],        66, "3c4a3f92e8954ad710296d49227d85092249376b7e80f6c14056e961002a1811", "SHA-256 66 bytes" },
        { &g_abRandom72KB[0],        67, "64979dd99b6da8172ae79474bad1ccc8e91adfe803a47b2bb497f466a78cf95d", "SHA-256 67 bytes" },
        { &g_abRandom72KB[0],        68, "479f6c701cabd84516f25a45a3759e17a3b6ee56a439e08e03a682316651645c", "SHA-256 68 bytes" },
        { &g_abRandom72KB[0],        69, "7b401aba8fbcff05cdeb0ad35e66ba5d608a39c4f6542d46df439b2225e39a1e", "SHA-256 69 bytes" },
        { &g_abRandom72KB[0],        70, "4b397707574f2196e8023e86d2c1d060cbb0ab3bd9ce78d6ae971452f6b2cd36", "SHA-256 70 bytes" },
        { &g_abRandom72KB[0],        71, "ca6ec101132f05647f4aad51983dfbafc7b9044aafab1fa8dcfb395b767c2595", "SHA-256 71 bytes" },
        { &g_abRandom72KB[0],        72, "78605447fcbe1adecf6807c4a81ab0a756b09c777d3156f9993ad7b22f788ed6", "SHA-256 72 bytes" },
        { &g_abRandom72KB[0],        73, "ee529f31a4e0b71bf4bd182a45f800a5abb0e42169e8d875d725712306ad0fba", "SHA-256 73 bytes" },
        { &g_abRandom72KB[0],        74, "582bb8ec1c431e2468065a7d2b2dab2ed10b2a23e650cf2c295eb0d90bc4c6d5", "SHA-256 74 bytes" },
        { &g_abRandom72KB[0],        75, "faa6b7ec0cd4e13f8b53f9116675f3d91c90244eb8c84dadc81883c9421421e0", "SHA-256 75 bytes" },
        { &g_abRandom72KB[0],        76, "69e989716af62d1a255e53260e8bff7d680d507fdc432955dea7e616dc3a222a", "SHA-256 76 bytes" },
        { &g_abRandom72KB[0],        77, "619a27ee4575109a9880b2a7ff8f52f0b66346fe7281805e227390d24dc7f3e4", "SHA-256 77 bytes" },
        { &g_abRandom72KB[0],        78, "79efe7395bd9fbeb8964558c0a88be8a7293f75bf4513e0efa4cda0efb1621b6", "SHA-256 78 bytes" },
        { &g_abRandom72KB[0],        79, "361a1c3874a0145324c3ce6330e3321eef84fd46d2127e68c1e2596872d74983", "SHA-256 79 bytes" },
        { &g_abRandom72KB[0],        80, "42b3ec061a230faec1af95f685408d61c8025a30d8a9b04f7c9b2626f94d85e3", "SHA-256 80 bytes" },
        { &g_abRandom72KB[0],        81, "97aa260a9979f20997731c258dee85bc0936812cacae8325030b9df4e5c4762f", "SHA-256 81 bytes" },
        { &g_abRandom72KB[0],        82, "022d15b91d9e3849ca9e284bef29d5a2567c0bdd5af6145945705102165c3107", "SHA-256 82 bytes" },
        { &g_abRandom72KB[0],        83, "e45a484833a59bd0834dc2da045eac9747c1441f4318b1d535eb6e4c0bd869a3", "SHA-256 83 bytes" },
        { &g_abRandom72KB[0],        84, "ba0782193d5792d36b4a9cc5b1a47de9b661a7a05cbbcc1abd9334ee3778f6bd", "SHA-256 84 bytes" },
        { &g_abRandom72KB[0],        85, "f528b11135dc44642573857dbffcb361cb3fdeaefef8bb36eff4bdee1670fe59", "SHA-256 85 bytes" },
        { &g_abRandom72KB[0],        86, "0ba567b67c054bd794462540ca2049a008857b112d2fbd591ba2c56415d40924", "SHA-256 86 bytes" },
        { &g_abRandom72KB[0],        87, "21b09abfd9c2b78d081cd5d07aae172df9aea3c52b572fa96dbe107d5db02817", "SHA-256 87 bytes" },
        { &g_abRandom72KB[0],        88, "6cec2966f175b9bc5a15037a84cb2368b69837168368f316def4c75378ab5294", "SHA-256 88 bytes" },
        { &g_abRandom72KB[0],        89, "2d9628847f4638972646eb3265f45ebd8d4db4586a39cbf62e772ad2e0868436", "SHA-256 89 bytes" },
        { &g_abRandom72KB[0],        90, "5652de4228d477a5425dfde8d9652655d8c761480a57976dfa78bd88e4b11ff0", "SHA-256 90 bytes" },
        { &g_abRandom72KB[0],        91, "7a50a27207be3066ad1b349cf9c82e50d8610d0d95ec53d0fa0b677e0ef198c4", "SHA-256 91 bytes" },
        { &g_abRandom72KB[0],        92, "edcd28c7c6777bec4f9ff863554098055abcbc4ee6f777f669acf4c17e9b939e", "SHA-256 92 bytes" },
        { &g_abRandom72KB[0],        93, "f8e353f5033856bf1b3e29b1cf95acc977473e6e84c6aa7f467ff3a214a311f8", "SHA-256 93 bytes" },
        { &g_abRandom72KB[0],        94, "ff964737aaf19c5968393aa37d5a133bd42d26d49a1d342bc625cc23fbfcd3df", "SHA-256 94 bytes" },
        { &g_abRandom72KB[0],        95, "7b975c510c8d7eba8ba3688cd96452d18b3544bb5aed540845b8ed320862e6cb", "SHA-256 95 bytes" },
        { &g_abRandom72KB[0],        96, "39af3d95f2784e671171b02217344d41a50ca063db118940d940b103aa8f88df", "SHA-256 96 bytes" },
        { &g_abRandom72KB[0],        97, "a7f84a55605007267ab6b22478d82fc045b9ccdeb7bc29e2368b6d36ba5302ee", "SHA-256 97 bytes" },
        { &g_abRandom72KB[0],        98, "393755a20e107455a7d961494a23433b3aed585b6173231922ba83cd7980baba", "SHA-256 98 bytes" },
        { &g_abRandom72KB[0],        99, "555400c5ea1b138cf58c0941a4edd7733698ef35d9b7b599da1d27a4a1da9b56", "SHA-256 99 bytes" },
        { &g_abRandom72KB[0],       100, "f27524b39dff76ca3870c765955a86272f8136801a367638ab788a3ba9f57c04", "SHA-256 100 bytes" },
        { &g_abRandom72KB[0],       101, "4857c87e9c6477e57475b8ceff80a02de75a9c6a2c21cfa2a3ac35ef199b132d", "SHA-256 101 bytes" },
        { &g_abRandom72KB[0],       102, "9c41626db68168c7c3a1065fc507932ea87f6fe7f490343d2ed532ae244c756a", "SHA-256 102 bytes" },
        { &g_abRandom72KB[0],       103, "13c331f42ad5bb2216ccbfe8e9254111e97291da1ba242a21d99f62547720ab7", "SHA-256 103 bytes" },
        { &g_abRandom72KB[0],       104, "259dd0b292cac0f4bb6a26e5e8dce7cfde908561edda176b3d826228c7ec4836", "SHA-256 104 bytes" },
        { &g_abRandom72KB[0],       105, "5f90dce6a68f0ccf50e0ffbfbc1e7831ebe619ab053d59625d75a5805d1cfc91", "SHA-256 105 bytes" },
        { &g_abRandom72KB[0],       106, "9eedf570854b0e9b47fb9bddccdd7c02529b0ce1394f83fa968f8bd10061dc82", "SHA-256 106 bytes" },
        { &g_abRandom72KB[0],       107, "4dce09d513d26be436d43ab935164156441bfbe2a9ce39341b3c902071f2fb75", "SHA-256 107 bytes" },
        { &g_abRandom72KB[0],       108, "f1ba290596fedeabca8079a8fd8eafa0599751d677034e4d8c4c86f3542e8828", "SHA-256 108 bytes" },
        { &g_abRandom72KB[0],       109, "e7dfc612ca26e6fb1dbc7b75dff65bff2c6b134e8283a24b9434ad5d469cea09", "SHA-256 109 bytes" },
        { &g_abRandom72KB[0],       110, "7d3f395a5da0dd86fb14e29074c7e4f05cb32ae28ddf1bd4327a535df9f809fb", "SHA-256 110 bytes" },
        { &g_abRandom72KB[0],       111, "7d74c406261ff04c3c32498b3534ef70c6199adba0d1d91989a54a9f606ebeb5", "SHA-256 111 bytes" },
        { &g_abRandom72KB[0],       112, "ccc4acce4d2e89c2fe5cc5cc0e1e1b380de17095dee93516ee87f1d1fc6f5e01", "SHA-256 112 bytes" },
        { &g_abRandom72KB[0],       113, "301bb78408884937aed4eb3ff069a79c2b714ee18519339ccac4afb10bfb3421", "SHA-256 113 bytes" },
        { &g_abRandom72KB[0],       114, "2afe58676676f4a44a422cafd1c3ca187a7cf7dd54d4901448e59b683a7ce025", "SHA-256 114 bytes" },
        { &g_abRandom72KB[0],       115, "5b88845e99afb754ce84cc42d99ddfc9022b55175d3cda8c56d304450d403ff3", "SHA-256 115 bytes" },
        { &g_abRandom72KB[0],       116, "4255156c12f13ba85659d5d0b8872ae63e0c98075c06c64673ab6b1e253cab71", "SHA-256 116 bytes" },
        { &g_abRandom72KB[0],       117, "c5e0fe1632cd39d9bec9b7047fbdc66f6db3cb2b60d4eef863a4c5f43649f71d", "SHA-256 117 bytes" },
        { &g_abRandom72KB[0],       118, "e7be5a747eaf858c56ab45d1efd8317dddea74df01e188d2d899aeb00a747873", "SHA-256 118 bytes" },
        { &g_abRandom72KB[0],       119, "e4642107146d4b94dcede9a4fdcd4f13ab88cf49605e8a7cfe303306288bb685", "SHA-256 119 bytes" },
        { &g_abRandom72KB[0],       120, "39261dcb052d46f1f811f6edd9f90805e0a2ff0b86afbdc59da4632b5817e512", "SHA-256 120 bytes" },
        { &g_abRandom72KB[0],       121, "b1e2988090ddd589757939f2b0912515998b7ac9ec8534655f586764b350fe78", "SHA-256 121 bytes" },
        { &g_abRandom72KB[0],       122, "c21143977ad672e9458d403b1da4da2553ac113eb0d1bb905c781badca957c30", "SHA-256 122 bytes" },
        { &g_abRandom72KB[0],       123, "173a78a19a11875f87163c5f111be2ec7a39d1358051fd80141b12576f4a17c2", "SHA-256 123 bytes" },
        { &g_abRandom72KB[0],       124, "e499062db198b79950bb482e99c0a5001bf76a339b3f0da5cec09e3ec3f11599", "SHA-256 124 bytes" },
        { &g_abRandom72KB[0],       125, "fbdfd0d05db20c67fda9f2d86f686e29f9fac48c57a7decf38c379eb217768b1", "SHA-256 125 bytes" },
        { &g_abRandom72KB[0],       126, "d8435c9cf281b3fd3d77b3f0dcab2d3e752c78884b76d13c710999731e753e3b", "SHA-256 126 bytes" },
        { &g_abRandom72KB[0],       127, "038900394e6f2fd78bf015df757862fb7b2da9bde1fbde97976d99156e6e5f3c", "SHA-256 127 bytes" },
        { &g_abRandom72KB[0],       128, "37b31b204988fc35aacec89dad4c3308e1db3f337a55d0ce51ed551d8605c047", "SHA-256 128 bytes" },
        { &g_abRandom72KB[0],       129, "069753c44ea75cddc3f41c720e3a99b455796c376a6f7454328fad79d25c5ea8", "SHA-256 129 bytes" },
        { &g_abRandom72KB[0],      1024, "c4bce478ad241b8d66bb71cf68ab71b2dc6f2eb39ac5203db944f20a52cf66f4", "SHA-256 1024 bytes" },
        { &g_abRandom72KB[0],     73001, "92a185a3bfadca11eab70e5ccbad2d40f06bb9a3f0471d021f2cab2f5c00657b", "SHA-256 73001 bytes" },
        { &g_abRandom72KB[0],     73728, "930de9a012e41bcb650a12328a45e3b25f010c2e1b531376ffce4247b3b16faf", "SHA-256 73728 bytes" },
        { &g_abRandom72KB[0x20c9],  9991, "8bd4c6142e36f15385769ebdeb855dcdf542f72d067315472a52ff626946310e", "SHA-256 8393 bytes @9991" },
    };
    testGeneric("2.16.840.1.101.3.4.2.1", s_abTests, RT_ELEMENTS(s_abTests), "SHA-256", RTDIGESTTYPE_SHA256, VINF_SUCCESS);
}


/**
 * Tests SHA-224
 */
static void testSha224(void)
{
    RTTestISub("SHA-224");

    /*
     * Some quick direct API tests.
     */
    uint8_t    *pabHash   = (uint8_t *)RTTestGuardedAllocTail(NIL_RTTEST, RTSHA224_HASH_SIZE);
    char       *pszDigest = (char    *)RTTestGuardedAllocTail(NIL_RTTEST, RTSHA224_DIGEST_LEN + 1);
    const char *pszString;

    pszString = "abc";
    RTSha224(pszString, strlen(pszString), pabHash);
    RTTESTI_CHECK_RC_RETV(RTSha224ToString(pabHash, pszDigest, RTSHA224_DIGEST_LEN + 1), VINF_SUCCESS);
    CHECK_STRING(pszDigest, "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7");


    pszString = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    RTSha224(pszString, strlen(pszString), pabHash);
    RTTESTI_CHECK_RC_RETV(RTSha224ToString(pabHash, pszDigest, RTSHA224_DIGEST_LEN + 1), VINF_SUCCESS);
    CHECK_STRING(pszDigest, "75388b16512776cc5dba5da1fd890150b0c6455cb4f58b1952522525");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { RT_STR_TUPLE("abc"), "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7", "SHA-224 abc" },
        { RT_STR_TUPLE("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
          "75388b16512776cc5dba5da1fd890150b0c6455cb4f58b1952522525", "SHA-224 abcdbc..." },
    };
    testGeneric("2.16.840.1.101.3.4.2.4", s_abTests, RT_ELEMENTS(s_abTests), "SHA-224", RTDIGESTTYPE_SHA224, VINF_SUCCESS);
}


/**
 * Tests SHA-512
 */
static void testSha512(void)
{
    RTTestISub("SHA-512");

    /*
     * Some quick direct API tests.
     */
    uint8_t     abHash[RTSHA512_HASH_SIZE];
    char        szDigest[RTSHA512_DIGEST_LEN + 1];
    const char *pszString;

    pszString = "";
    RTSha512(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha512ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");

    pszString = "The quick brown fox jumps over the lazy dog";
    RTSha512(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha512ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "07e547d9586f6a73f73fbac0435ed76951218fb7d0c8d788a309d785436bbb642e93a252a954f23912547d1e8a3b5ed6e1bfd7097821233fa0538f3db854fee6");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],         0, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e", "SHA-512 0 bytes" },
        { &g_abRandom72KB[0],         1, "a9e51cac9ab1a98a599a13d05bfefae0559fd8c46abae79bc15c830f0153ba5f05a7d8eb97578fc71594d872b12483a366125b2c71f27a9e3fb91af9c76e7606", "SHA-512 1 bytes" },
        { &g_abRandom72KB[0],         2, "0f69ca22d38f562e2221953cdf3ee0b360b034c5b644ee04e0f58567b462a488578b4b75b15ffab8622f591c5e74a314cc4539929f14941969bbd3bc6770f499", "SHA-512 2 bytes" },
        { &g_abRandom72KB[0],         3, "58bebd4e3f1e5e724a4b158db50c415ede9d870b52b86aa9f4e04eadc27881e8e0f0c48da1f2dc56bbf5885801267d10bdf29c68afabae29d43f72e005bcb113", "SHA-512 3 bytes" },
        { &g_abRandom72KB[0],         4, "e444e041d0f14e75f2cb073c3d6d75a5a5f805e7292b30dfa902a0df4191422a6ff7d7644d28715a9daf01c4580c4b5ea94523e11d858ffc3f4d9cbade1effb2", "SHA-512 4 bytes" },
        { &g_abRandom72KB[0],         5, "f950ebe051723a765edfa6653e9ebb034c62c93fcd059ba85b250236626bda33408204ff9dccc7a991ab4c29cedf02a995b8a418b65dd9f1b45a42d46240fdf0", "SHA-512 5 bytes" },
        { &g_abRandom72KB[0],         6, "c39979d4b972741bf90072ca4ce084693221370abcbfc12a07dbc248d195ad77d77d1fca2f4f4c98c07963d8a65d05a799456f98a9d3fc38f7bf19b913beccbb", "SHA-512 6 bytes" },
        { &g_abRandom72KB[0],         7, "ac5653cb0455277c9e3294a2ec5ad7f2ef8f9790a01e7cfabd9360d419a4c101d4567cf32b35d291e3cc73747e6f3b2d89a95c96421468db5eeae72845db236b", "SHA-512 7 bytes" },
        { &g_abRandom72KB[0],         8, "454a56d5f9df24303c163430d8ed19b624072e2cb082991e17abe46bb89c209fd53beecb5182f820feb13d0404ab862bb3abf219c7f32841f35930e2fdf6c858", "SHA-512 8 bytes" },
        { &g_abRandom72KB[0],         9, "e9841c44180cbe5130147161767d477d1a118b12fdf3791a896fdaeb02c9d547b4613a6a3d34bb1311ff68f6860f16f7bb341a2381a143daac3d3b803c25469d", "SHA-512 9 bytes" },
        { &g_abRandom72KB[0],        10, "450915d9d69e85ff87488f3c976962c32db18225ae5241a59e4c44739499dc073e1503f121065964c8b892c1aef4e98caf2a5eff10422ced179e1c8c58aeadf7", "SHA-512 10 bytes" },
        { &g_abRandom72KB[0],        11, "059d9629d61a58bede1ae2903aef1bcf1ef6b1175680a6e795def167163d05202167e22dba232750bc58a150764c13c048b2d83c0b077d819a1bd3a92432d97f", "SHA-512 11 bytes" },
        { &g_abRandom72KB[0],        12, "00e3ec8f76e1bbdeae3f051d20369b64759b9f45e15add04e4bb7590b90ce9c37706e36b68dfdbfaef8b8e8d41e42732de80314a1c58f89a8a26179d4ba144f2", "SHA-512 12 bytes" },
        { &g_abRandom72KB[0],        13, "dfde69e66f61bbd8c2d5c56493670d9c902df8b5fde0bdb61dbcc902390290c159ef1dfd988813a14c424c6bee49e62c17140918a4ad805edf9ac67ffe6af6ed", "SHA-512 13 bytes" },
        { &g_abRandom72KB[0],        14, "5d7908aada1445e9edecef34cbedd032baa646a88aafe4ebf3141008485b640539ee78e56b18d34bfcd298178412384d993d948e5711c61500a53085e63e3bf1", "SHA-512 14 bytes" },
        { &g_abRandom72KB[0],        15, "ab6102021cbf19cae1b0633ecb0757f4a280b8055fa353aa628ba8d21188af3daf08323367c315dd0eb9ba8b58f415decd231f319ad7cc9a88156229693c2061", "SHA-512 15 bytes" },
        { &g_abRandom72KB[0],        16, "75e2d97633b22af732caa606becf798945eb91be1d04f66b2ec86b8df98d12f7de8baa6db48b1294a713f17d792c89cc03726a7b03ae0d16837de1b4bfa5f249", "SHA-512 16 bytes" },
        { &g_abRandom72KB[0],        17, "e6bbf8ab9c68e99f569f6c68d93726eddab383e3695ab84383425617e73b6062970045fca422b393d479f3ba821ce205461ff76d0acf67fd4a83e5eeb5ccf594", "SHA-512 17 bytes" },
        { &g_abRandom72KB[0],        18, "68cfb17e1fe58ce145c3ce44efdb12b65a029d19ca684a410e0243166e86985b92f8e815de17f36f2b0f54e08c6a49c5446be14ebce6166c124d8605189adc24", "SHA-512 18 bytes" },
        { &g_abRandom72KB[0],        19, "fcaeefcae96683fb1337170f5f72247252843322029ee9edc7333f14386d619f2ecc12707ed4f1aea5f7c052c682d8c9e518a47c4fc9f347299688818baf0468", "SHA-512 19 bytes" },
        { &g_abRandom72KB[0],        20, "22dfd1b7af7118d93645f9b7baf861a317b4318a8881e4cb6a7669298560849c9169d86b57803c0de0c82f1a5e853ee571cae3e101cd8aad5d2045aec0805879", "SHA-512 20 bytes" },
        { &g_abRandom72KB[0],        21, "87de7e747b5ac2401e8540498ff8222e80b28bcb570c7f873332b01f5c6c0b672b8ac65d4986bd4463fc5f6f7f16656cc300bc9a5be9873295b1a0a13ce2aa28", "SHA-512 21 bytes" },
        { &g_abRandom72KB[0],        22, "deb01211010e06351e5215006bd0b09b353e6af5903a5afe9236f7418be7bd804119942be2be2cef9dd86b7d441300d8861ae564cad3dd5ae3eaea21de141f22", "SHA-512 22 bytes" },
        { &g_abRandom72KB[0],        23, "c7c62e3033755889d9b690f9b91288ea79e9225068b85981d4c3f2035ecde58a794fa59938b3206e0b9edea380fad3d9534bf40e889b59d8db52261f1e06e7cb", "SHA-512 23 bytes" },
        { &g_abRandom72KB[0],        24, "ae825238a7ac961c33b7b4e3faa131bca880bbd799657ab89e8e260fc5e81a160490137587c13b89af96cc494c884bac3e3c53a5e3838ffba7922c62f946fa77", "SHA-512 24 bytes" },
        { &g_abRandom72KB[0],        25, "a8bd5532033300fc15292726e8179c382735650e9c2430fedd45c6e0ec393f87e04a53901e76bf9ea089c41b9c73e34440f7c7503ed872ca61b61301aaacd71e", "SHA-512 25 bytes" },
        { &g_abRandom72KB[0],        26, "a2439151940845db0bfd7f4a955e0a7f19d594759917530fd2afa1f41ab30afa1122cc00875bbccf3cbe4c3afb94deaf6abcb23ccf379c6ee31ac1615a16b12a", "SHA-512 26 bytes" },
        { &g_abRandom72KB[0],        27, "5a0d49012aa539aef64a3f4e0933efdba3610b7aee6e8e2ca26a5550799141a3174001eff9669c8194eb2b60b7e264e02b73fb4bdeda61076423b26f50b210f1", "SHA-512 27 bytes" },
        { &g_abRandom72KB[0],        28, "09847f1c906c9f5507b499ae6a8543ee3dc04b81eee072148b0c55668ce363a139e54f74da9feb5fadddfa0c920bdf595ba68bf7ba21c6e131e6c4b598d444d9", "SHA-512 28 bytes" },
        { &g_abRandom72KB[0],        29, "80efb6be55a081fac36fc927326b5b6e53c91aadb9106d8b3861ecab9b08b7b6ebdea386b5132d6e6cd7d096ebe912577bccde6b709d5f2f2e8f2ce4cf8834cc", "SHA-512 29 bytes" },
        { &g_abRandom72KB[0],        30, "a052774739540e095f4e54ebc77b6b4a60253c20231d077224a9bfd69a882b5bb4db7ae52a8c475ef15a95d8fc2c33064fdaf356f151a429cd0625b218708eee", "SHA-512 30 bytes" },
        { &g_abRandom72KB[0],        31, "6501f635b4690ccd280443d2eeee36cc6d626df9f287a8e46e237a0528b82aafbce5a23d75a71ac611cee62c1c0423d5e4dcec14b5c31907fbe2f60406486bee", "SHA-512 31 bytes" },
        { &g_abRandom72KB[0],        32, "a7b4e870a1a4585439f62da7e99998f60daecc90849c16ce1f5b4bc15e68e567b8fb0c44d1294eea925bc14161bb9f9e724933bfe88d8aab9ab2dbf4e00f7b88", "SHA-512 32 bytes" },
        { &g_abRandom72KB[0],        33, "ce465a93568976550943c160907615974b5c7fbbeb05506905d516a56dbf6dd7f2c8f45cbcea10d1c85316595559e3ff8193629eadbcd941359e166874ae4058", "SHA-512 33 bytes" },
        { &g_abRandom72KB[0],        34, "b5f60488576dccbeca6924483f1732055af0383e63acefc6dfc13e4e6b77655a97f10eb5778a28d939ca3f49311f4c0d2920764f10aa2fdff6d60a24e8d68d04", "SHA-512 34 bytes" },
        { &g_abRandom72KB[0],        35, "9509b9052efcff757291296a58fbba6835ccfc1b4692307fbf683a22379242a4d6c8324dcea6a870e50c40960cd56e0f52c41102f25f357328d8abf1b9499441", "SHA-512 35 bytes" },
        { &g_abRandom72KB[0],        36, "dcaf59cf1dc3c5dfd74595e53bda617c313ab65dd4d04821e17bb65330458827d89efac7d64556bc439c12d326d4312d90789f39801d0d9a0faba59d23a57f40", "SHA-512 36 bytes" },
        { &g_abRandom72KB[0],        37, "fcd30053a81adf7a9e5135e55fed6f389b6e516b04a0e9972b18df387a3dcb064f2903ea58d4b3e2f51c30a2ff0d7353b9d744e13e215a3a63e486130acca9fe", "SHA-512 37 bytes" },
        { &g_abRandom72KB[0],        38, "36cc41b6dc5602f8d71a0ea7f7187a648c7b78d9386c3f4cb952153b57fe6c307957d2a39752a5f682dee98e68a4b4e99a6e681f76ad6b561be3ba2bb7409e88", "SHA-512 38 bytes" },
        { &g_abRandom72KB[0],        39, "09d0e8487f329c2b66618e8734e2a5e7a68654cd744e5941ceba04e2f7dcf92af1d3ac5ad3920abc4b3b0e121a68a4665e0b6ed6eaa3dbef06cd3c21217f0445", "SHA-512 39 bytes" },
        { &g_abRandom72KB[0],        40, "d24e246470f0fa260976b1063f5a46a73c54614881173f3793c70a47f925bb0d05feb376858bd838f550cd82f7122b02b438ad3facfc9049be3efd7e5f3b03f3", "SHA-512 40 bytes" },
        { &g_abRandom72KB[0],        41, "6e56434ce4c50a1989df0de44485697a53ca6b8560ea84290fbb54b4627a980412033f4ed2aa795cd653dbd0134d4e4e2f25b57a20d55ee1ae8aa47d6e8af7d4", "SHA-512 41 bytes" },
        { &g_abRandom72KB[0],        42, "f9cce63deb2f4291ef74c7b50cf573bd99500b6aeee2959fe3500a7b55b3693df4f5b4eb5c242271e5b9ada3049b2a601e85f32d0501b870fa1593ea9cfa6fb1", "SHA-512 42 bytes" },
        { &g_abRandom72KB[0],        43, "0781bf3344189b3c10dfc34e6bf3f822a78533f0a29b066c67f49753412c40075b6d988c2c0a338fb8372be0b66a97a73c382509613e7fb908bed5cce9804676", "SHA-512 43 bytes" },
        { &g_abRandom72KB[0],        44, "4af952c3a51e5b932b21ce3b335213235ed7d3a2a567d647d6faa7e32e647038d0dbde3b6638453a79badb9c882d75d5201c5b172989d3e8b1632a15773fd2aa", "SHA-512 44 bytes" },
        { &g_abRandom72KB[0],        45, "98e0b281d765b90360338afaa7cf1ec90d841dfa07c05e7db74dc96dc7bf53465eb78d6e8c915c53d273fb569593d51a81331b56bce69eb506a3a400c73b170e", "SHA-512 45 bytes" },
        { &g_abRandom72KB[0],        46, "50c7c00f254a13b2288c580ebb165be5669aa88ae247aaddc8f20750564b945849f5126a3cc0fdf7926a59b7c7b7866fe49174bf1dfbbe5734a2fd570ff26183", "SHA-512 46 bytes" },
        { &g_abRandom72KB[0],        47, "e5bcecb9ce32ccfd4916a267f64b9d478461d10066375f0088f7ffc2e43b393d09613e8f60c623268c863b91ef08059b0af491d15d040fff14ef3b2c89e84d46", "SHA-512 47 bytes" },
        { &g_abRandom72KB[0],        48, "f30c0d42f15fc467f454cc8b7d3fb3453150a57f28eacd9870fecc98cae43dc937b4e45cb3443e6c09eb8d82bfdd1c501870f827d5d792dbbaf3beb9dc7bfaba", "SHA-512 48 bytes" },
        { &g_abRandom72KB[0],        49, "12bb4030e966cf51ca3ead169bfdf399e1564b2c7a7636e8625d288473967643a85e932ed45d9b9ecf9a05001e34d259a8d4733faf7ee9093e1a64b57fe357a7", "SHA-512 49 bytes" },
        { &g_abRandom72KB[0],        50, "e21a552fe775cc337c4bccb934f4093d03c4c6c5e2e50ba6cdf0cd5ddd74d7b3de6690e8d3b2ff195b9015e3d137a6f4b6e683148e4a8c7914272767cb3fa68f", "SHA-512 50 bytes" },
        { &g_abRandom72KB[0],        51, "dd262372035812a20304a313866cbd9c6c9099d4c434630d3904618bee471967e4b715f7e3bc4f5d3283b4131b8885af5af645289faba8cfb095960883eb5d31", "SHA-512 51 bytes" },
        { &g_abRandom72KB[0],        52, "0541767fd127bd553915f6fc4e11336c734bee7409c972f014952128152658339bb641989b19eba9d73f7cbe2561fdf5c00e7ac6d7a3b4dedd3249cb11357a0c", "SHA-512 52 bytes" },
        { &g_abRandom72KB[0],        53, "6446470e96304a1e2ca9946de70d8c61c0c39e23900274db6262042e5555663af6b25d7d7dcd1f1f890e14e1a588b498e8cc26e8aba117bcd61ef74b25603645", "SHA-512 53 bytes" },
        { &g_abRandom72KB[0],        54, "946bb0ced082b07b965d57c3634011f13142b9dd84146103262483b7fb7c3413bd481ad1a3e01005e974792a6523a2c22eb2211ce7912c2e88378ac6d962f809", "SHA-512 54 bytes" },
        { &g_abRandom72KB[0],        55, "7d742669be7bef2a4df67e5853146cbd8e8ab9afb1b3429d5eac7e1fe66e050d7c08eac3e90b596eaad00ddb92ac8876a50eebb4fddc17b39abd79c83dc2523b", "SHA-512 55 bytes" },
        { &g_abRandom72KB[0],        56, "5b25df3a85495f600cc1cd92d53e969adbb3329a87cf8f32361fc2aea3331bedfed63c25a5e74a38790b2d4a96bf1e0e4df9f0ac8e8d07813197a575939cf444", "SHA-512 56 bytes" },
        { &g_abRandom72KB[0],        57, "9aefb636d982bf842ec37bf91f5c01f65796f954e26eb3cdd35515aef312b93f72ffc6fbbe3eb168fb07973471ebdd33b302e098c8b5d949d29afe129761137f", "SHA-512 57 bytes" },
        { &g_abRandom72KB[0],        58, "7deca25f1ca788598002368c2e07c5b65766bc28e66b1fe1b1a16dd38134a64f2a51be3a9c8930da939971e48ce5d9f25141a619386639eaaee11786c9c4df6f", "SHA-512 58 bytes" },
        { &g_abRandom72KB[0],        59, "27e4c254f1284dbece4f63aba4f61ff6cc23abe4ff2e86d24ccfd582a2cf65145849de27d8292a66391466f965e61b06772b1f8a7b5a5e69f8b0f0a6d4974d40", "SHA-512 59 bytes" },
        { &g_abRandom72KB[0],        60, "39e3a628a14f307028e3f690057ef24a02533d2141e0cf7070d4f2ba5d58e92d5c27e9d6dd8e07747915ea5535f8963ef350c424d6f3b3dc3850256a216b4ae0", "SHA-512 60 bytes" },
        { &g_abRandom72KB[0],        61, "8877a71d1e412e4fe0d60bbcc3ac61c458a221311b1425defbfa28096b74e18b2661ae03026da38d6ed7a42b49850fe235145bb177a7e0e99b12977bd5eb4ce5", "SHA-512 61 bytes" },
        { &g_abRandom72KB[0],        62, "a8623e4a43560950427020b64bd70f37ce354698d926c0292614b89100f6e30947db498bb88b165dc50da54439321a564739be36de02d134da893a4e239bdd01", "SHA-512 62 bytes" },
        { &g_abRandom72KB[0],        63, "62d7f6e428f8252723a265ce2311aa11fed41dfd07705cc50a24d744cf8ade4817cd3c5a22deae04bfdebe9292022ccd87e5fbe8fac717ee1ef01d2a336dcef3", "SHA-512 63 bytes" },
        { &g_abRandom72KB[0],        64, "1d356bccb9d9d089bec81a241818434ab1157bbafc7fa1fad78f17d085430bd6efffa409efdd8bf4306927407272e14f70f5344ee5085ccbc17aac16a9d40ae7", "SHA-512 64 bytes" },
        { &g_abRandom72KB[0],        65, "9c500cc115fbd8f609890b332ed6b933c2e1b60664ac57939776348b394a3af6ef55b740351cad611bed175bc932971d7803caa3802e765d14e795e83ef81727", "SHA-512 65 bytes" },
        { &g_abRandom72KB[0],        66, "862197dff8c67ea30846b25adeb191546c165cefa7c2fd3bebfdcb038884dac5bf0f5274417c2834a63751bfbc744193bd8bbfc2f261a01a9a3c2914d5069913", "SHA-512 66 bytes" },
        { &g_abRandom72KB[0],        67, "fadb5858a9071e696371c37287e0d7ee476fca005de6d1049162bded431bc9659c1f981c110fb8fade5495a6207af819260512d0160d11dea295856f4fb55d4c", "SHA-512 67 bytes" },
        { &g_abRandom72KB[0],        68, "029b208137092c9f38084ace884d371c34434e3268802aa7b6276697eca683d23082a9c4d81f8b871adb99cb2a4f73d1064c16feec7c3df282594045250eb594", "SHA-512 68 bytes" },
        { &g_abRandom72KB[0],        69, "a6f78eba388c4b96ae3b30a6204f0d4bc7ee6c3dadcaf18a938dba69a0a66fef1a6a2d6589da0e8990bea7bbe46cdbdae700a464f394c8f1c13821f49b9ce08b", "SHA-512 69 bytes" },
        { &g_abRandom72KB[0],        70, "f39c31993dd96a07cb0168c4d72a216ab957d56d2e5a73840ed7ea170659434c0ca6d5c4d70eae040ab0f488cd1b93453c85c398b3fb2cbece762e5eebeae3ae", "SHA-512 70 bytes" },
        { &g_abRandom72KB[0],        71, "90d60c76601deeb2f3c9c597a04a333724fa4efbca2fdbb09163bff812615841495b79225627b6da100aaec0ac7f532a782075308aed7c6760e530430f77c063", "SHA-512 71 bytes" },
        { &g_abRandom72KB[0],        72, "604e315e1db8ddf64b5d11151113c25b61b66d690046c1830bccfb0b92cee65ade2ed75691bb65be9df84d4d83bb9b0a3311453c9f7e30a04889882e74383d75", "SHA-512 72 bytes" },
        { &g_abRandom72KB[0],        73, "bfd93659349d3ee86f88ba312087c97cfb6989d33eef7f59ef9fd1ed650c4e10b5172b9cd90bf4982c85397c0de2fa691f5b49617e0bc168f45c084093cf3b41", "SHA-512 73 bytes" },
        { &g_abRandom72KB[0],        74, "6084b22bfbc488cc5c61ff382137fac5dea084d32e49aafcf2c9eec3d1cbdcff8f093df63913743b55a16304b0d8adb663dcc37e6d933a6212c1aa13e4acd2f5", "SHA-512 74 bytes" },
        { &g_abRandom72KB[0],        75, "2a9a4625797f6137b4200be28f07bc8b183e098e139d427c6f0b7b5ab19f5f0b4bad407424414e2475f05cf0a8055deefa0c8503bf1c09d545634c6cf4d4337a", "SHA-512 75 bytes" },
        { &g_abRandom72KB[0],        76, "88e7dd70a561537bb978e295cefd641a4bc653cc1dee9f8cafe653c934f99d2b7c18caf26abb803acbc8172eab34cc603aaa35aeca9444c5759016415f076430", "SHA-512 76 bytes" },
        { &g_abRandom72KB[0],        77, "78fbe62ea044c5601099b5605486bc911bb88e325077651722fd4790554f6a01c7cceaddd38d850925b05852616712118d356b7b023fdc7facae720a2b3008fb", "SHA-512 77 bytes" },
        { &g_abRandom72KB[0],        78, "1e35da455a47b0acc9ee2c6970de8376ee3ad22b53c39a6613651de23fb323aff796d9b9ee7c3f56684bca29bc16df9e2e4846d66ee4f6e720eb8c01b305f166", "SHA-512 78 bytes" },
        { &g_abRandom72KB[0],        79, "a1d26f27fc25892a1329434d6045384b62b32d61a645c06645493b32aaf0d6be7761828c04424d778214863db047fbc34865d0c4271f1d22206b60eb16cf92be", "SHA-512 79 bytes" },
        { &g_abRandom72KB[0],        80, "8ad44d72bb9a3a7436f26577275b97eef79a27a5de9eda9f1c5ebf740cb2e1198acb0ce774f395bba1962c570f19278eba8a5928f740a5cc3113cc6b6627d8e1", "SHA-512 80 bytes" },
        { &g_abRandom72KB[0],        81, "54556b5b7a14e090491256ab964cbb819d57e0f3fd8acf7fc9ff87ccb0a89f388e8083fe6e86eb3da3bb21c6dcaa291bb6e48e304628e9d1e2c13a3e907645e5", "SHA-512 81 bytes" },
        { &g_abRandom72KB[0],        82, "9997f6934072546a9ca5a1824589db692ac9b25b9be49f82b769efe094b8909e16037644ea88ca86501ff3ea533fdc5b81ce8e3456e07559b218aee682c151d5", "SHA-512 82 bytes" },
        { &g_abRandom72KB[0],        83, "5bc87e566b62eea1af01808cf4499dc7d0e06ced3b1903da2a807a87b9cd4c8ffd4d46e546a8a18e815efae0df5eb70191c8afbe44aefae02e2e2886593618f7", "SHA-512 83 bytes" },
        { &g_abRandom72KB[0],        84, "a3231d21aea1381fd85b475db976164dcae3cd9ce210285007b260b63797ca8d024becdd6b5b41ad1772170f915ecb03785e21224a574e118c5552e5689fcd47", "SHA-512 84 bytes" },
        { &g_abRandom72KB[0],        85, "d673935769d0ac3fe6f522b6bb537869c234828139a1a39e844c35592f361b4e39e55c7e49bd0dd7588ce8e30f7b6aeed6cf3ccfd951589a27dda37b1a2fadeb", "SHA-512 85 bytes" },
        { &g_abRandom72KB[0],        86, "a0fca2a6e0126fa9a9b84947167ea3a42aeaa69d8918a175e0bd0be20f8beba333e59dd3905029da37cb54740f94bc1bf688be4756e6c6769de9658566d07c90", "SHA-512 86 bytes" },
        { &g_abRandom72KB[0],        87, "a8730b6e7d1c850c40c797ec8d69dc4b26473e692cb0e3fe8781be355574bb921c4454a8d54fa2e607a0bc36be9a5ab324e0f9f439bf1cce93d0ebfd019154ed", "SHA-512 87 bytes" },
        { &g_abRandom72KB[0],        88, "77857fad35e564a8d5dcea9b981bcb074dc6aaed7626db8132e08555dae7f5445c0378a6dbfb24bf5f2c4c6afac09cb19ac1ab76ed4f41fe3a72d32ca1f39f00", "SHA-512 88 bytes" },
        { &g_abRandom72KB[0],        89, "6ac4c2044f4d9a983c1e41ae22daf47ef05a4ee2f013a174a3068d38c59994a19b8788541a29f47f3000b4c0491dacbf98e98dc0588a3a30fee42524697ab996", "SHA-512 89 bytes" },
        { &g_abRandom72KB[0],        90, "eb894f383342fed8371ec72403c636b1bcc7f6f39d6db1dbfc58f9fac8f41552af6dc0a5a968146c028c4db113b21831d80eb0ae166d68616ae3696832f4e563", "SHA-512 90 bytes" },
        { &g_abRandom72KB[0],        91, "8be91181345c9511f2c74df93db07ca739a586a2a84f38d5f063244d2e39fc4254c5747787e08b74a2a59916c9751aa30d0fe57a18d858d3346186facc86365d", "SHA-512 91 bytes" },
        { &g_abRandom72KB[0],        92, "43e007dc454a4303724a2baa860307083a3a1e1d52b587cc3b7a4b6bc252c8dccad0e9cf9f2b16ec178371e57033ac3c4e71dbcb2eb69cf7ef7f617c7ada8f76", "SHA-512 92 bytes" },
        { &g_abRandom72KB[0],        93, "2e2d6686c4e7a7919879ee03a4fd37ddc71a7e2ffa17a3cfe6eef24b29d8a1e7cc0fa5d8e9dab3c6c3190652ceb9e1ebe67a0ac7d92c6c205a9e8add91bbd2a7", "SHA-512 93 bytes" },
        { &g_abRandom72KB[0],        94, "c8b9e3825c4a81c63f79b88d91f61af15c714ccc611dc77635c1bf343e2c185caa2e0ef8eb76505f1544c8f78377c26d3a6f79c2b77abadba9906e583fb2e5c3", "SHA-512 94 bytes" },
        { &g_abRandom72KB[0],        95, "0bc63075ec108a5ee1f69a4e39ae03333da8c1f2d61a94fbe6357c143fa2dfeee44fb8cad0ee016fd42697f6848b7a174b4a77a268401f7cac4c4df1623cfc52", "SHA-512 95 bytes" },
        { &g_abRandom72KB[0],        96, "b4aa1d4a849840413e830e27c388baed9e8d4a0941048aad9a4b8751497d86e002e3e50b0197a9164ea440ec4324771229d0c5de04b9922c992d97ea736ff477", "SHA-512 96 bytes" },
        { &g_abRandom72KB[0],        97, "1c5142f15476f56abed2a0c5d9450e14f56a30af2d5d81f16e05ff0184d695fd1923488da707b570307370c4f1669a522230617f347c264465c12d82fa2018b8", "SHA-512 97 bytes" },
        { &g_abRandom72KB[0],        98, "7146b68a0670d5d901e312c78a728111975c312b8365a3cb9618cbe9c124c30d65cdf668902bd9ec76493caa0ff40e3c1f03ccb06e3b2380c69c154fd065d137", "SHA-512 98 bytes" },
        { &g_abRandom72KB[0],        99, "e591fc00af6733a6e308577e0043d5c12b81051848d8123e4350b82037350f27828ed6cdc0b1daf9ef57b30edd72b58370cd1851545d9e39ddeb00fbc66c8582", "SHA-512 99 bytes" },
        { &g_abRandom72KB[0],       100, "6328c48376ab29fbebd732db8b4073a96b2358de13c8a2a5a5ddc2502d8e0356822da65551bb079d4a90f3fefe5b8d00cd186696706900471348784a55009a0c", "SHA-512 100 bytes" },
        { &g_abRandom72KB[0],       101, "c90b01b14fd712be70ac318b21974181418365cd94a33d4121260c260d6f72e0819195d5c3f83b516e7e9aaf667957bc67c0c0a44a35d7756b41e33c3677d017", "SHA-512 101 bytes" },
        { &g_abRandom72KB[0],       102, "d23e5ca2c0f460d6831abd112b6a6f6d3aebdc500f7af96d887aebbaceae2e33f61f5a423deacee85276e796478d5002c0b94e85c0b2d55c75752a8a717785e2", "SHA-512 102 bytes" },
        { &g_abRandom72KB[0],       103, "e20df98010b277e188ddf6d3e953722287cffcf43c910e80ab9dceec32aff3c2059d0268bbbae414bc08919adb92d409035ab1a970a175aefeaced8df2395e13", "SHA-512 103 bytes" },
        { &g_abRandom72KB[0],       104, "774154db3993f0b4d05f03e187249cc9db94e736807691de26f734277149597ac68acdb412f8fdf3a71093cabcd0257d39f3c5e8eb7d3545474914a5b4ebc623", "SHA-512 104 bytes" },
        { &g_abRandom72KB[0],       105, "30df8e2d774fa706d8ffce6009237122540d20d4d92e5c9e2f19c4b9fc91ef9454254575e1a94cde8930439b679f2702ad22145c983418fc5b48d1b0d46e7677", "SHA-512 105 bytes" },
        { &g_abRandom72KB[0],       106, "091b5252043b7d47aab184c82c3317f2674ff8b0b76679de6c4e27ac15fc653597d635c8a0d3a0c0f271e86b7c9db86c622449bbb1044f6b26ebf7f681ab108f", "SHA-512 106 bytes" },
        { &g_abRandom72KB[0],       107, "152e06c5313eb9c58345878c73180ab82c0a66ab853653a54f1195f351eca82ebcbbb6c7705bd20440eb69e9b73df9a4696421205f9b2244e5765be5493b0fee", "SHA-512 107 bytes" },
        { &g_abRandom72KB[0],       108, "cdc5a8269c1aa916af26a6fefd395f338fd9ba7b7ad578fb5e7d92be956a83c43cc7cc8f590450878d45e28882d204bd244e2e5bf6309e425170024d8d307a87", "SHA-512 108 bytes" },
        { &g_abRandom72KB[0],       109, "6c120d70dda091f8de847241e6921e8540fcf0cca9927ac673bc9d2aa29bc5a782134a79250d9fa1bb436ebf73378ab0619a05140473ff0c2be7292f34e6d6a7", "SHA-512 109 bytes" },
        { &g_abRandom72KB[0],       110, "7094f3e32b2075db6290da8a3379b138675943f647d14444bc9299c01a5ea9c58706386030970f4d670132d9d7a2485064c901443f9f1050679ad09e576ae2df", "SHA-512 110 bytes" },
        { &g_abRandom72KB[0],       111, "a56e148f8cf9a20c96453ca2cd29aea8ec9b76c213f72d609a1052f81efba24b7de365214b21dd09447b8df272e1458f566f3af827d96ea866c921155ed5b85a", "SHA-512 111 bytes" },
        { &g_abRandom72KB[0],       112, "68ab4ac095d2aaa05c61ea622804c3164f27f8fb0adbd070906e75093cb09e2b283aeda64584cd856de8ef369f64a0900e0e191f08a7f729ac9b9077efd41c43", "SHA-512 112 bytes" },
        { &g_abRandom72KB[0],       113, "1ca365ab12d42b4ccd1561a2000b885fd38df9a60b0f8580c85e2547f40be8fcee465b24b7efd09fbac92c4aa74fe403fe7f00aacb7aec5d187403d34da4b6c2", "SHA-512 113 bytes" },
        { &g_abRandom72KB[0],       114, "17652cc2481a32c42f9cdc447bbbc3a95c76d7f7b2cd6ca93c29e0beeef219baf00f1d3514c3f9fba7a6c70177d3371a0e95638f1c210135f9bcb7322e513749", "SHA-512 114 bytes" },
        { &g_abRandom72KB[0],       115, "0d42ab4691599cdb280b61fd170a8dd7970639ab074109cdc91bbc94714ea257cb1c35a3c5c471d8853c02e66d90de0b66d9c04adeccbaed7e48d517d7b763b4", "SHA-512 115 bytes" },
        { &g_abRandom72KB[0],       116, "d410d63adf1dadec0eb6712f30ef2f3f56aef3e5e0ddd4fa3088516bdefe1af3d031869b9d4642013bd1d907a8687db86363a9e94d9e08bac6576b0cf0a3f877", "SHA-512 116 bytes" },
        { &g_abRandom72KB[0],       117, "adfa29d3e85a68a153a470cd271293a6fcf27b67b45b232cc6f1944375ae6254760bdc700ae33bfaaa26490a6a216982379e3973081d210034791f60a1e90259", "SHA-512 117 bytes" },
        { &g_abRandom72KB[0],       118, "35f41b486bae7cc4c6b2609dd5314deb8a5151da7e6ab1b0bfb1d7310dae8435152db8b75ebee106a583f16a4b0a3408492e15fba9f9f90c1fda7daae3b3dd71", "SHA-512 118 bytes" },
        { &g_abRandom72KB[0],       119, "dd9cc5aa2ee23e9bc94e80795d19f40adef09384365a805ee0c9b6c946734ca55e8c68c0baea51fb0693efbf04722f5236b9ebc8aed222ea4c47c2d2f20af1ef", "SHA-512 119 bytes" },
        { &g_abRandom72KB[0],       120, "6988719e25df98b8f6b779fe48f61d81f3e7f88ea4c3fed89b28f49b8113686dc5788e66da419c6dcb38a95b2fbd9b29259e7bb8754923913e3f8528ce884f59", "SHA-512 120 bytes" },
        { &g_abRandom72KB[0],       121, "b096a715acd6611dd57d66b547093c937ce73456e18f5d969c2bb1581f33b6ad89142d19850ae29648368adf53d0ff804ed661fb106f81dac514b75a7200d0df", "SHA-512 121 bytes" },
        { &g_abRandom72KB[0],       122, "3691d1ab30f41684f320a6161743b2113e3b452c23867248a8741a383410bb4c83f116404c649cd6983f79a156bf4933d1264d75662d93011a7764f5f962d26a", "SHA-512 122 bytes" },
        { &g_abRandom72KB[0],       123, "2b953227e77ad7415d5363a8f5eed23529b573e20dd6925fd357814daa184f4c4b69ef14a3599c476f589fcfd027c609ccb2fb247acd83f4812d8b72dd0800ff", "SHA-512 123 bytes" },
        { &g_abRandom72KB[0],       124, "639323ed76ab3ec433446d67b41a9f8b6c917627795caf266a58fe396c141988b73490a78d6e053d88ce8c2252051ca711ad2de4fa4fcfca9c26a6a8ea5f16b9", "SHA-512 124 bytes" },
        { &g_abRandom72KB[0],       125, "243b6196df1e1e5c6018644e71471d51e4320303f18749a2e7888430cf43e4f71598c394fbcb2f31b76acf5349233ee9614ab86b455364e54bd2013ee9e1bcbd", "SHA-512 125 bytes" },
        { &g_abRandom72KB[0],       126, "5dc477dcf848e23ba3a66c2ff9bccefdf83bc1134ff7f7a20fe7393269c7987939fbc264535a1ff0d3aba7201ee15a448d5545429a48c681ed5a8859857614f6", "SHA-512 126 bytes" },
        { &g_abRandom72KB[0],       127, "7ea868053923ca7112afb72d6d184ea4fa41191b5a2cfc30c4555ae4bc6223c7c6c834f9f34433947308838abee9d068cd18cd3021ca677141440fc03d5daddd", "SHA-512 127 bytes" },
        { &g_abRandom72KB[0],       128, "a2d7ee08394542488ee7c76954dc027826836161de10795eab31877dc0b56321ca0239a324985a5826a59ef60f70d591e543f56a5fa147d53f85d15ffc7f7791", "SHA-512 128 bytes" },
        { &g_abRandom72KB[0],       129, "e5df5295d00b085665aab5208d17d2c5b152984ca952a2f28599943fe613b38590e24b5552b9614ad38de16197599ac2464ba7a85b66b087b4162dbd8f1038e3", "SHA-512 129 bytes" },
        { &g_abRandom72KB[0],      1024, "213117257eedf07e76d9bd57f7b9b5fad2fbdccd8c9bf60a70e8b2feac5a30ccf83ca9041a07bb15727c81777d94ba75535f29a0bd92471b8899f5cd096e326a", "SHA-512 1024 bytes" },
        { &g_abRandom72KB[0],     73001, "73e458b0479638d5e0b89ed55ca34933fbca66dc2b8ec81490e4b0ee465d7045736a001bc37388e6f73b3acd5655a210092dd5533a88ba1679a6513fe0c70a74", "SHA-512 73001 bytes" },
        { &g_abRandom72KB[0],     73728, "80bd83278c0a26e0f2f952b44ff31057a33e971ea4d6d2f45097e1ff289c9b3c927152ec8ef972929b9b3222abecc3ed64bebc31779c6178b60b91e00a71f542", "SHA-512 73728 bytes" },
        { &g_abRandom72KB[0x20c9],  9991, "d6ac7c68664df2e34dc6be233b33f8dad196348350b70a4c2c5a78eb54d6e297c819771313d798de7552b7a3cb85370aab25087e189f3be8560d49406ebb6280", "SHA-512 8393 bytes @9991" },
    };
    testGeneric("2.16.840.1.101.3.4.2.3", s_abTests, RT_ELEMENTS(s_abTests), "SHA-512", RTDIGESTTYPE_SHA512, VINF_SUCCESS);
}


#ifndef IPRT_WITHOUT_SHA512T224
/**
 * Tests SHA-512/224
 */
static void testSha512t224(void)
{
    RTTestISub("SHA-512/224");

    /*
     * Some quick direct API tests.
     */
    uint8_t     abHash[RTSHA512T224_HASH_SIZE];
    char        szDigest[RTSHA512T224_DIGEST_LEN + 1];
    const char *pszString;

    pszString = "abc";
    RTSha512t224(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha512t224ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "4634270f707b6a54daae7530460842e20e37ed265ceee9a43e8924aa");

    pszString = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    RTSha512t224(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha512t224ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "23fec5bb94d60b23308192640b0c453335d664734fe40e7268674af9");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { RT_STR_TUPLE("abc"), "4634270f707b6a54daae7530460842e20e37ed265ceee9a43e8924aa", "SHA-512/224 abc" },
        { RT_STR_TUPLE("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
          "23fec5bb94d60b23308192640b0c453335d664734fe40e7268674af9", "SHA-512/256 abcdef..." },
    };
    testGeneric("2.16.840.1.101.3.4.2.5", s_abTests, RT_ELEMENTS(s_abTests), "SHA-512/224", RTDIGESTTYPE_SHA512T224, VINF_SUCCESS);
}
#endif /* IPRT_WITHOUT_SHA512T224 */

#ifndef IPRT_WITHOUT_SHA512T256
/**
 * Tests SHA-512/256
 */
static void testSha512t256(void)
{
    RTTestISub("SHA-512/256");

    /*
     * Some quick direct API tests.
     */
    uint8_t     abHash[RTSHA512T256_HASH_SIZE];
    char        szDigest[RTSHA512T256_DIGEST_LEN + 1];
    const char *pszString;

    pszString = "abc";
    RTSha512t256(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha512t256ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23");

    pszString = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    RTSha512t256(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha512t256ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "3928e184fb8690f840da3988121d31be65cb9d3ef83ee6146feac861e19b563a");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { RT_STR_TUPLE("abc"), "53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23", "SHA-512/256 abc" },
        { RT_STR_TUPLE("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
          "3928e184fb8690f840da3988121d31be65cb9d3ef83ee6146feac861e19b563a", "SHA-512/256 abcdef..." },
    };
    testGeneric("2.16.840.1.101.3.4.2.6", s_abTests, RT_ELEMENTS(s_abTests), "SHA-512/256", RTDIGESTTYPE_SHA512T256, VINF_SUCCESS);
}
#endif /* !IPRT_WITHOUT_SHA512T256 */

/**
 * Tests SHA-384
 */
static void testSha384(void)
{
    RTTestISub("SHA-384");

    /*
     * Some quick direct API tests.
     */
    uint8_t     abHash[RTSHA384_HASH_SIZE];
    char        szDigest[RTSHA384_DIGEST_LEN + 1];
    const char *pszString;

    pszString = "abc";
    RTSha384(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha384ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");

    pszString = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    RTSha384(pszString, strlen(pszString), abHash);
    RTTESTI_CHECK_RC_RETV(RTSha384ToString(abHash, szDigest, sizeof(szDigest)), VINF_SUCCESS);
    CHECK_STRING(szDigest, "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { RT_STR_TUPLE("abc"), "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7", "SHA-384 abc" },
        { RT_STR_TUPLE("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
          "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039", "SHA-384 abcdef..." },
    };
    testGeneric("2.16.840.1.101.3.4.2.2", s_abTests, RT_ELEMENTS(s_abTests), "SHA-384", RTDIGESTTYPE_SHA384, VINF_SUCCESS);
}


static void testSha3_224(void)
{
    RTTestISub("SHA3-224");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],         0, "6b4e03423667dbb73b6e15454f0eb1abd4597f9a1b078e3f5b5a6bc7", "SHA3-224 0 bytes" },
        { &g_abRandom72KB[0],         1, "c16ddd47002314077565c004c8a6a335622fb9eebd93a207b3df86f1", "SHA3-224 1 bytes" },
        { &g_abRandom72KB[0],         2, "806c36dcdfa26341648dfcd6dffed6bd17b1bfae7e6cff0ddade2dcd", "SHA3-224 2 bytes" },
        { &g_abRandom72KB[0],         3, "850900b680b3645a715bf4f9a24c939936d5d400cea7e48e27a5a82c", "SHA3-224 3 bytes" },
        { &g_abRandom72KB[0],         4, "03f71d684cd5f38a1508a4a313fa2f099afcf306a9943d8b497b793f", "SHA3-224 4 bytes" },
        { &g_abRandom72KB[0],         5, "188ca424af350902fc377035db1f26e46ebe304ede5ac700ed4262d3", "SHA3-224 5 bytes" },
        { &g_abRandom72KB[0],         6, "6310deb0fd800c59d4116ae8f98137200377e637902904e21f8b1912", "SHA3-224 6 bytes" },
        { &g_abRandom72KB[0],         7, "4aeccabab1c5af03ab038edbf7f293fcc95a5d362c51bad6208ffb2f", "SHA3-224 7 bytes" },
        { &g_abRandom72KB[0],         8, "9349cae62e9198c47ecbd288dc2f92918b8fca2ba9121633d82a4d4a", "SHA3-224 8 bytes" },
        { &g_abRandom72KB[0],         9, "36c1119e49b45c72ab64f81281204f25cda7dcba348347d441f7392e", "SHA3-224 9 bytes" },
        { &g_abRandom72KB[0],        10, "2bdfddbd1ff52d1a55a15e862e1f6740bc09ca2dfb195d756402899b", "SHA3-224 10 bytes" },
        { &g_abRandom72KB[0],        11, "00d28c1f6d6d68b18c102d0c79727b62aff23b534efb2397f628d739", "SHA3-224 11 bytes" },
        { &g_abRandom72KB[0],        12, "037550e18635864203a6aaf2c732ed76dc94f6dd838cd14f0d317dbd", "SHA3-224 12 bytes" },
        { &g_abRandom72KB[0],        13, "d989f92017047e08b8212483a2ce9288aa97b1f07bda29c938d9bc8e", "SHA3-224 13 bytes" },
        { &g_abRandom72KB[0],        14, "ab23e6d5a674c471f2842e81593f42f728056fc2adf4b78b7f91edc2", "SHA3-224 14 bytes" },
        { &g_abRandom72KB[0],        15, "d8470eb158ce88069f057d2c96db04f9bb8d480e4edc239be4d074a5", "SHA3-224 15 bytes" },
        { &g_abRandom72KB[0],        16, "9c446f8b9a4d70ff2aa174ed49910db205622f4735f78c8e756b69d9", "SHA3-224 16 bytes" },
        { &g_abRandom72KB[0],        17, "c873455ce6b16f24d3a437a6397a06215500111f448627929db94ffb", "SHA3-224 17 bytes" },
        { &g_abRandom72KB[0],        18, "1cf8336176b6747bcd1e6bacda5b87d9d299cae74c4eacfc25241523", "SHA3-224 18 bytes" },
        { &g_abRandom72KB[0],        19, "97bea48929cc724f9fb75d132101b8f00c82c8d5790c5e06dc41393f", "SHA3-224 19 bytes" },
        { &g_abRandom72KB[0],        20, "f549bcda80edcaeef5e01047a6429c55babc33407502d332d1a4285d", "SHA3-224 20 bytes" },
        { &g_abRandom72KB[0],        21, "752a317c4527605d6882cf40d833de22cb43eb208f2f265a16795ff6", "SHA3-224 21 bytes" },
        { &g_abRandom72KB[0],        22, "379bc7846ec4736dbe7a69f94e4622cfccba5687694e1c73ed6a1c57", "SHA3-224 22 bytes" },
        { &g_abRandom72KB[0],        23, "f4d7161e2e4dd576f9c67e8212e94f8f6a0348b1760e6009d4d82b8c", "SHA3-224 23 bytes" },
        { &g_abRandom72KB[0],        24, "188b729072e18f920f869809d90270c26991566bbf943c080ee944c7", "SHA3-224 24 bytes" },
        { &g_abRandom72KB[0],        25, "3b65d20cf2717de484574d39656a8684a71e9bc9ed6640e075ccb018", "SHA3-224 25 bytes" },
        { &g_abRandom72KB[0],        26, "0219518dc74ea5829974af838fada6581e611b2909365269bba0b2d6", "SHA3-224 26 bytes" },
        { &g_abRandom72KB[0],        27, "2f4d4afa8dcc6d2bdcbcf148a328b63392c5a6cca40939c243e20832", "SHA3-224 27 bytes" },
        { &g_abRandom72KB[0],        28, "c4fd8e78042ef4a366a7d9bc81c089e7391c93fafa85c48fedad100c", "SHA3-224 28 bytes" },
        { &g_abRandom72KB[0],        29, "ab364ae73034dba102a0c477c3acee20d9ea6a967c49a0361f60e241", "SHA3-224 29 bytes" },
        { &g_abRandom72KB[0],        30, "dfe98cb78941401a9c50fb87659cd6413a2cb13c7ff8ef0823553cde", "SHA3-224 30 bytes" },
        { &g_abRandom72KB[0],        31, "d40afd915348f9b2fd9e6ada40d27859ca1efed9d795eef714f65809", "SHA3-224 31 bytes" },
        { &g_abRandom72KB[0],        32, "2503528850f2f418c7bb029f507766d77fb5be193b66ed74edf24d6b", "SHA3-224 32 bytes" },
        { &g_abRandom72KB[0],        33, "2c13b17427c015049e746cbece772caba6937adac3a5255166b032c0", "SHA3-224 33 bytes" },
        { &g_abRandom72KB[0],        34, "d36fc360863705e3d1c22d1dfe7c3d369ae5445aa835e06046b06236", "SHA3-224 34 bytes" },
        { &g_abRandom72KB[0],        35, "688909a2d656bdc79d9607628c13458668b4a68f418eee4ad40e3797", "SHA3-224 35 bytes" },
        { &g_abRandom72KB[0],        36, "8564656691fb972b8d8462944c4fc95dfb815ead67402dd150310ad9", "SHA3-224 36 bytes" },
        { &g_abRandom72KB[0],        37, "000e42152df4306c46fd86b64abc64061861b535e73c1db755873c92", "SHA3-224 37 bytes" },
        { &g_abRandom72KB[0],        38, "5797e1b6e7738bcbea346d58ed7e82cdf4921c8c7061ad273a652fad", "SHA3-224 38 bytes" },
        { &g_abRandom72KB[0],        39, "d9f70e175a6ec2b87fa53dccd8fe2fe662619d4227e79b020354a8ef", "SHA3-224 39 bytes" },
        { &g_abRandom72KB[0],        40, "a567898565c10d9d233aabacc661b3b534f823ee9144a78b3538c29c", "SHA3-224 40 bytes" },
        { &g_abRandom72KB[0],        41, "f9879abc1aeaa3b93465bf012bb0bca619994d1e38c5d9704a4c4737", "SHA3-224 41 bytes" },
        { &g_abRandom72KB[0],        42, "a947c58c199a49cd27837c7df25bba6d4832eab3f6ad7825481439ef", "SHA3-224 42 bytes" },
        { &g_abRandom72KB[0],        43, "496485839fdd5cc8dc0f0ce1973c2bde663723dfe5eafe539bd96599", "SHA3-224 43 bytes" },
        { &g_abRandom72KB[0],        44, "fcea784f1428967cae93ae58716a1a7cde35ce3d295c65439a4f24ec", "SHA3-224 44 bytes" },
        { &g_abRandom72KB[0],        45, "3a2e32058f0553e706fc1ac804931c809994598d86d74c3a00e88bf9", "SHA3-224 45 bytes" },
        { &g_abRandom72KB[0],        46, "d964782b0fc1743a590efe7f42de8c3c44e8ddcd5a1b7bb343223d56", "SHA3-224 46 bytes" },
        { &g_abRandom72KB[0],        47, "7e4c82f47d77ac4539d25e6fa122d70d2d017f96a971b108c190fa78", "SHA3-224 47 bytes" },
        { &g_abRandom72KB[0],        48, "66393d6e347cbd37e81b3d0c43175ab2a874196bb051391d8ed75353", "SHA3-224 48 bytes" },
        { &g_abRandom72KB[0],        49, "865e7ab710d415c2c291491891f0a076c6064863c49e483fa4e77b80", "SHA3-224 49 bytes" },
        { &g_abRandom72KB[0],        50, "b495da95e14b544f0229788ee6b88dfc7c1b0e809b1c9f89884db075", "SHA3-224 50 bytes" },
        { &g_abRandom72KB[0],        51, "9ce32908772904b4863513044646a89e69493cc6d5d43c3b07afa48b", "SHA3-224 51 bytes" },
        { &g_abRandom72KB[0],        52, "da457f9a263e9b1b73130419c9be8eaa91d9c742b8965c4721fb3a1c", "SHA3-224 52 bytes" },
        { &g_abRandom72KB[0],        53, "86707eb2d9e32b3241740d7c915cfa0216928eeca1c66e4bf61304ae", "SHA3-224 53 bytes" },
        { &g_abRandom72KB[0],        54, "2ed6f9b614341b440107ac9743f202d56698757ec704d051b6b98c09", "SHA3-224 54 bytes" },
        { &g_abRandom72KB[0],        55, "8c6c71dcd399cb83506b6cdf0d58bc07301e674e643fbd28c8510044", "SHA3-224 55 bytes" },
        { &g_abRandom72KB[0],        56, "ee58b4581f368a8530d8c97f568d22d25710279044450d9616b3642e", "SHA3-224 56 bytes" },
        { &g_abRandom72KB[0],        57, "bcfc035caa5b5604221a2eb4fb52ea14f362e719cde6de940409fe36", "SHA3-224 57 bytes" },
        { &g_abRandom72KB[0],        58, "83e3508129d6891269e6e71f25a72143ea106ba6a03fa86fb394a7a5", "SHA3-224 58 bytes" },
        { &g_abRandom72KB[0],        59, "7ac6956c9966581a9fb892c56e798e9fb0f5ef32f417daf015e50826", "SHA3-224 59 bytes" },
        { &g_abRandom72KB[0],        60, "ae689e3347198b48aebc96178dddc52e35e6c54c3cee365e49e0ea69", "SHA3-224 60 bytes" },
        { &g_abRandom72KB[0],        61, "0279f79ba33d32b8214c1133097b9f89fa659b77c44eaf166ed2529e", "SHA3-224 61 bytes" },
        { &g_abRandom72KB[0],        62, "9b27f0538ac8fce31e123ae6ab2ca9569d3fc8e018b7e6fc1eb7c73d", "SHA3-224 62 bytes" },
        { &g_abRandom72KB[0],        63, "7961cc0f6091100760f0b1e2d4db62969320fc1780092576d1dee5be", "SHA3-224 63 bytes" },
        { &g_abRandom72KB[0],        64, "e5740eb1c2c611e2a75fbaf4375be8bb73fe5718d9fddc3bca889c5a", "SHA3-224 64 bytes" },
        { &g_abRandom72KB[0],        65, "54699c4f043f96b065d04af21b8fc0ff21880a4f82c3c32d6a11c1af", "SHA3-224 65 bytes" },
        { &g_abRandom72KB[0],        66, "fcd22829d13a6d7cec11876a1a376afd521c98d29363fd683a58e907", "SHA3-224 66 bytes" },
        { &g_abRandom72KB[0],        67, "02dc82ad08e364cf2571c047de6ceb9cbfa4928e907484497d66fb3b", "SHA3-224 67 bytes" },
        { &g_abRandom72KB[0],        68, "17156cb0b69423dac7e749db311abf4a706b16fee64698c6565e7869", "SHA3-224 68 bytes" },
        { &g_abRandom72KB[0],        69, "42a22855ab3be53983f392f7cbd038164b0eb0d482bfcb4b9f2c3e70", "SHA3-224 69 bytes" },
        { &g_abRandom72KB[0],        70, "0758abc06dcdf81cd6923f1d97246bf24e0bd50f43c4215fde41d25f", "SHA3-224 70 bytes" },
        { &g_abRandom72KB[0],        71, "8fdc4d4d0ee58239eba5bbee83b9e06a3d35ecc45e7ef94428ac5766", "SHA3-224 71 bytes" },
        { &g_abRandom72KB[0],        72, "831289ea7a65354937575adeba26668776640421c44f3619421054b8", "SHA3-224 72 bytes" },
        { &g_abRandom72KB[0],        73, "17e7a1aa6bc9b5a4e6964801c119ceed987406d328902ee37accf277", "SHA3-224 73 bytes" },
        { &g_abRandom72KB[0],        74, "0c79a923e2a3178808f6bb4215357990f5488fbb3793247af84f9bf9", "SHA3-224 74 bytes" },
        { &g_abRandom72KB[0],        75, "c6e157824d65e3880eb35d6b97c26affaf49e323009ccbdf23ec91de", "SHA3-224 75 bytes" },
        { &g_abRandom72KB[0],        76, "a4a2f31afe00a3e7ed111809ca4b36d24aff59fe9515908187c28f2b", "SHA3-224 76 bytes" },
        { &g_abRandom72KB[0],        77, "a427e8644082b21109519306030dd62b48a3913f996b21039b46fb50", "SHA3-224 77 bytes" },
        { &g_abRandom72KB[0],        78, "7d930aca6fef38522af10a132d146325cc30937ae9abb301605c350a", "SHA3-224 78 bytes" },
        { &g_abRandom72KB[0],        79, "da75e1be010a756971d76da39015c34978c6295f77d01dbed4f39028", "SHA3-224 79 bytes" },
        { &g_abRandom72KB[0],        80, "3541d7bce47da0d906fb90fb1d98610dacca9adf79d156337aa22683", "SHA3-224 80 bytes" },
        { &g_abRandom72KB[0],        81, "f79c13e00d5836f6d047d84eebbeb0edf296f8aa72fcfa08b49efc5b", "SHA3-224 81 bytes" },
        { &g_abRandom72KB[0],        82, "63ed5e252ca90d5f83c1f27ec8f658c488e4671a1b18c070d954e2f3", "SHA3-224 82 bytes" },
        { &g_abRandom72KB[0],        83, "653a98bf1b4aed149dd9552aa9e9b5598d52fe6bf777f92e61df850c", "SHA3-224 83 bytes" },
        { &g_abRandom72KB[0],        84, "c685ef068ea79653eb9deb7eb19616421a1a32ad11bd7ac4a3e76bde", "SHA3-224 84 bytes" },
        { &g_abRandom72KB[0],        85, "53d01a556bce8d05e6ead02cd8376dea9abc73051d08165afa4332cb", "SHA3-224 85 bytes" },
        { &g_abRandom72KB[0],        86, "b245082e59d780d00b2f26d99848808b467c8ca00aa9e9be7b6015e1", "SHA3-224 86 bytes" },
        { &g_abRandom72KB[0],        87, "a43477d9ad4c176a2efb21b970e273270856d5fc212060142c03abaf", "SHA3-224 87 bytes" },
        { &g_abRandom72KB[0],        88, "50d777f861dec127b5a1033f53c18ec3b42c3b7506cac20714f1b840", "SHA3-224 88 bytes" },
        { &g_abRandom72KB[0],        89, "367c94e4553e21f893e50100eef0c73a9c4ece979f15feecffd7ae6e", "SHA3-224 89 bytes" },
        { &g_abRandom72KB[0],        90, "f6b9bb09f5518ec01cc8f1dfa8a15f3956a40699eb306cbd8ee3fd2f", "SHA3-224 90 bytes" },
        { &g_abRandom72KB[0],        91, "87e0ff97b08b82abf918775d9bfae231224046c94d17803ce2891708", "SHA3-224 91 bytes" },
        { &g_abRandom72KB[0],        92, "8ff74e71ac81da91b9333d0da134573616180972656a5c78671eee1f", "SHA3-224 92 bytes" },
        { &g_abRandom72KB[0],        93, "cabd5597a76890d584d639652b3987c7ca0a66508b00d011cc439f8b", "SHA3-224 93 bytes" },
        { &g_abRandom72KB[0],        94, "686774c4d9124325b9643a575964e09c51bd3bc24cb2cc42a9c5061a", "SHA3-224 94 bytes" },
        { &g_abRandom72KB[0],        95, "9cbf2c98dad0ae73735f2cac071129e41ae2585776ded9e2d43d57cf", "SHA3-224 95 bytes" },
        { &g_abRandom72KB[0],        96, "f53faed02f1dfccc0547af35da372774218e2504298a3bf80d05ed97", "SHA3-224 96 bytes" },
        { &g_abRandom72KB[0],        97, "72cd564a08ba595c375197df0158c90b7358a047d6b8f6cf98315b2f", "SHA3-224 97 bytes" },
        { &g_abRandom72KB[0],        98, "afa5d854d139ea9bb8d9ebb58cc593c4b4bea8aa0046427c77ffd5e6", "SHA3-224 98 bytes" },
        { &g_abRandom72KB[0],        99, "fc81822a0155f881431c2432ec4caa9fd4c1ac739a03c30f8b320d53", "SHA3-224 99 bytes" },
        { &g_abRandom72KB[0],       100, "25f2f31b98e6001329a8a5008d3f20479afea8a38a857b4c04c30090", "SHA3-224 100 bytes" },
        { &g_abRandom72KB[0],       101, "7f86d0e097e022b180899413c99360872b733544cc3337172c85b2f9", "SHA3-224 101 bytes" },
        { &g_abRandom72KB[0],       102, "d35db0d601a2e096cb6788a966dfd161ed8f5056ad0e0a7e1e9e717f", "SHA3-224 102 bytes" },
        { &g_abRandom72KB[0],       103, "c5934041c642050bcff5ecc477cd03465a504497dd93f80be9013e68", "SHA3-224 103 bytes" },
        { &g_abRandom72KB[0],       104, "518317aef82651992e5142570beffcdbc74ed18f4b8d594f0541231a", "SHA3-224 104 bytes" },
        { &g_abRandom72KB[0],       105, "dc01a97914e9fa82adc76e1cb544e90d28a746a8e4922bdc3d8c45a9", "SHA3-224 105 bytes" },
        { &g_abRandom72KB[0],       106, "4f38ae5d26757a2c945134106b29302286d252d97c8412e104698364", "SHA3-224 106 bytes" },
        { &g_abRandom72KB[0],       107, "228bac7ab9519c2840485cd89abb6a8ce7168a6c7bada34aa67549e3", "SHA3-224 107 bytes" },
        { &g_abRandom72KB[0],       108, "b4066bbd7b2e50f945bdd8975077ee1ad0bd5dd43ee6de168f5eaf26", "SHA3-224 108 bytes" },
        { &g_abRandom72KB[0],       109, "848724ab14b37effa4f2de9eb7d509c4a67420655a8202aafc2245bb", "SHA3-224 109 bytes" },
        { &g_abRandom72KB[0],       110, "9217bd598153cd6bbf121034d7fe57e461be564dc27096760932c9c5", "SHA3-224 110 bytes" },
        { &g_abRandom72KB[0],       111, "c97bbaf7e7d2c884f8c11cefa049fbeda46385abb3cc3cfe60ee7025", "SHA3-224 111 bytes" },
        { &g_abRandom72KB[0],       112, "e0157bab5712c11296e8497ebaa461eb1117434d849aac9af578259a", "SHA3-224 112 bytes" },
        { &g_abRandom72KB[0],       113, "afec901dce2536c1b4d986816edfbff81ab5ab677cb1c5852de14947", "SHA3-224 113 bytes" },
        { &g_abRandom72KB[0],       114, "c7ffc02bf2c055ee7c6d72deeb19a70538a4ec61845b8d5df16655e8", "SHA3-224 114 bytes" },
        { &g_abRandom72KB[0],       115, "774d505a09932de66f66d20f2ba355cadf36fa143715c747834ebb8b", "SHA3-224 115 bytes" },
        { &g_abRandom72KB[0],       116, "32bd91adeaa38f017696ccadda32445e65f76bc7abc73ebcd35a324d", "SHA3-224 116 bytes" },
        { &g_abRandom72KB[0],       117, "27712acf5577badb6e87fbc519234b795ffa7ce509bc99665b0bc06a", "SHA3-224 117 bytes" },
        { &g_abRandom72KB[0],       118, "56b83dbf7aa7d0b3162a6f0a667ba469370e9a648b59db643172649c", "SHA3-224 118 bytes" },
        { &g_abRandom72KB[0],       119, "e6f35f53bf2df44eeff1dcb3549afa02e6dbec8febe17cd4417ba141", "SHA3-224 119 bytes" },
        { &g_abRandom72KB[0],       120, "8c44941f21d2e863daffc5ad7faee2b5a79475496e95753491345a4d", "SHA3-224 120 bytes" },
        { &g_abRandom72KB[0],       121, "1cb79d12f6eb093469aba8f0f3455389b09f8dd965d05a6cc06fdb21", "SHA3-224 121 bytes" },
        { &g_abRandom72KB[0],       122, "6080d39958b1472f5f4e9bdfcf36df30e074603464a9a242857fbe3d", "SHA3-224 122 bytes" },
        { &g_abRandom72KB[0],       123, "614920d5cffdc929efb38768ab8f6d940dc9488211c550d0526f3839", "SHA3-224 123 bytes" },
        { &g_abRandom72KB[0],       124, "5dd2081e84d1946245e8a58f27be169d26c017fa9ed583c0f29555fc", "SHA3-224 124 bytes" },
        { &g_abRandom72KB[0],       125, "cfe8e9b33a46812f82c6cd72908287afc40e9e77bb3a6cae670b4315", "SHA3-224 125 bytes" },
        { &g_abRandom72KB[0],       126, "7d8d13b5e533456fd67e4b3a674a3c1d83218a77bc47cc3019cd4df1", "SHA3-224 126 bytes" },
        { &g_abRandom72KB[0],       127, "b29f872062dfb9f40b1ae7b40d9eca735c5ec1e2de10f0eb393e638b", "SHA3-224 127 bytes" },
        { &g_abRandom72KB[0],       128, "edd905ef5427ac92cbed887eedee28193cc6ccad967769f112663031", "SHA3-224 128 bytes" },
        { &g_abRandom72KB[0],       129, "eddbb15b21c9edf478ff14c6cf99f5b1d82a92a91de38eaf8756f44c", "SHA3-224 129 bytes" },
        { &g_abRandom72KB[0],      1024, "c200848e295665570e56cd995f51e58289e1e6398c9c9f326d757bab", "SHA3-224 1024 bytes" },
        { &g_abRandom72KB[0],     73001, "95a8479d32047490841a03c0c2152554c40e4b470cac18777b96e245", "SHA3-224 73001 bytes" },
        { &g_abRandom72KB[0],     73728, "1acd8a512e1ea38402b16ae7fb796d2a195106078b65a77f28d517a1", "SHA3-224 73728 bytes" },
        { &g_abRandom72KB[0x20c9],  9991, "e2d5e6c023bb4a663c0958eef8b4ef4de953fe94d1b45c3cdc51fe8e", "SHA3-224 8393 bytes @9991" },
    };
    testGeneric("2.16.840.1.101.3.4.2.7", s_abTests, RT_ELEMENTS(s_abTests), "SHA3-224", RTDIGESTTYPE_SHA3_512, VINF_SUCCESS);
}


static void testSha3_256(void)
{
    RTTestISub("SHA3-256");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],         0, "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a", "SHA3-256 0 bytes" },
        { &g_abRandom72KB[0],         1, "afcd9d4977b545e10872d720975b66368e9388866420da61b967d84d6791819b", "SHA3-256 1 bytes" },
        { &g_abRandom72KB[0],         2, "b390b8f8ab7d1dd1570d9583c5536a09896836f78bfa6e146b2caa217fb03282", "SHA3-256 2 bytes" },
        { &g_abRandom72KB[0],         3, "6714921795665b34839a95593f20981a85f0bd84d079baff97419d6803303b05", "SHA3-256 3 bytes" },
        { &g_abRandom72KB[0],         4, "430bfb151ddae9b7c16e2ad9cbd33b6722ae1eda83f2b6bb6933e8fd3da56755", "SHA3-256 4 bytes" },
        { &g_abRandom72KB[0],         5, "9cf2b6d2627900be1174bebf23e705664eb4c08904430daefe537c6493a29d8e", "SHA3-256 5 bytes" },
        { &g_abRandom72KB[0],         6, "871549e51b212f4d63d0c7a55b13555ade1d73cd4002eb79144bcc4b97937cb4", "SHA3-256 6 bytes" },
        { &g_abRandom72KB[0],         7, "ae27b315124cc7b003ab1aaf2c108591fe2f31f807ea1b970799c3598e101204", "SHA3-256 7 bytes" },
        { &g_abRandom72KB[0],         8, "181674cc927edc7466c9408055d43a554448175edb5f3a3484a92c9d7344d81c", "SHA3-256 8 bytes" },
        { &g_abRandom72KB[0],         9, "a75aae88c58895fdb97ca7e26121d73ee2ad76bf084bcb3d5d7663efed5d918c", "SHA3-256 9 bytes" },
        { &g_abRandom72KB[0],        10, "1e7cc10a8cd646a063284960e443a83b36ca50d6c9429d0e4f2d098fbf20a850", "SHA3-256 10 bytes" },
        { &g_abRandom72KB[0],        11, "e05c7bf58b23cd04f390fd805ebd11d5e4f373382dd3f4fc065e3b5d36986f37", "SHA3-256 11 bytes" },
        { &g_abRandom72KB[0],        12, "362e87c0856768db0cde2c6a9edfc3ce513a021420d93b15669c18972ffbaa01", "SHA3-256 12 bytes" },
        { &g_abRandom72KB[0],        13, "c7a99d13b7da44be0565b3b726ed8948e425ae1c43adbf7145c715c7f725f60a", "SHA3-256 13 bytes" },
        { &g_abRandom72KB[0],        14, "0ffa1ed9b160cd0adc964f42834f6b18f65c6a5d12883594ad1b3ee53562585a", "SHA3-256 14 bytes" },
        { &g_abRandom72KB[0],        15, "dfc04b284a6e4db24ab307700269b25b4400242c0dd4773b851d7c24106f9e3d", "SHA3-256 15 bytes" },
        { &g_abRandom72KB[0],        16, "f567d888fec188b3a85600a4f31e3842e4b46fa3efad941b2e720f186a080e93", "SHA3-256 16 bytes" },
        { &g_abRandom72KB[0],        17, "a6d07d11cb3d6de6976ba487892c50c9bb73fe234827b224c92c70a950eb955c", "SHA3-256 17 bytes" },
        { &g_abRandom72KB[0],        18, "a494c5307e6210c405e990992ded702460a967e82e680d3c07db2a61a64d9565", "SHA3-256 18 bytes" },
        { &g_abRandom72KB[0],        19, "5c6eadddcd686959bffd39d1311c4e9ea71ec63d532d87cba38136fb9c156db6", "SHA3-256 19 bytes" },
        { &g_abRandom72KB[0],        20, "eef8fd8e01b47dbb4650f09e5306eb48a477e0f38170c3242e755f0bcc44d574", "SHA3-256 20 bytes" },
        { &g_abRandom72KB[0],        21, "c93e8811b3e7a2409c96d4a738c2c20658cc7740d971e9c7d12044bdd9abad81", "SHA3-256 21 bytes" },
        { &g_abRandom72KB[0],        22, "6c78e40e3f570e4e7ff22b47f4cadee3f0007aafefff83e42d04cec20b4d9de5", "SHA3-256 22 bytes" },
        { &g_abRandom72KB[0],        23, "ef35d85cdd3b7e0cf364c3d19d28a3c6e187052a92a4085e014b5b89b1a94e18", "SHA3-256 23 bytes" },
        { &g_abRandom72KB[0],        24, "1e6d74bfd46200dbb0ba76c536004118da36350a8fabdacdf33405e969964d76", "SHA3-256 24 bytes" },
        { &g_abRandom72KB[0],        25, "a03c0e30ec83406b0a9987cf9d4355b45f26a9f97190df193886d576e335cd0f", "SHA3-256 25 bytes" },
        { &g_abRandom72KB[0],        26, "8df121000eb2c38c9cab237e8e098b6e021ba6ad514da55ca1c1a62f8caaaa9f", "SHA3-256 26 bytes" },
        { &g_abRandom72KB[0],        27, "568b200871d9586bad5912369703c0e89954e063246ca7a778e1ce5e33794302", "SHA3-256 27 bytes" },
        { &g_abRandom72KB[0],        28, "e84cb03accae1b85e27bf7ffcf25c88902a911478823c37713ab2b9aedc79a67", "SHA3-256 28 bytes" },
        { &g_abRandom72KB[0],        29, "94df7a85582b1ed59724d0390851e7b2bf62b99dbb2b03ce0edb660003469a31", "SHA3-256 29 bytes" },
        { &g_abRandom72KB[0],        30, "d8b23f6aecc5824335c5982aa9e02157d97ce719e416184bcc86e101b9c196b8", "SHA3-256 30 bytes" },
        { &g_abRandom72KB[0],        31, "93a281f2175a8da699e0822f8b5ff64035a40d1d3f7de4d52eebca43bc793f87", "SHA3-256 31 bytes" },
        { &g_abRandom72KB[0],        32, "f61d85fc0c2bb5df6992d3c06ee0104be88d8ad9ed4a3d76dd2dd1e8600bbe78", "SHA3-256 32 bytes" },
        { &g_abRandom72KB[0],        33, "eeea3fc3344187d0fc85a2e92d4bb30ac60ab71bd04ef8dd26b6de2821af3e5f", "SHA3-256 33 bytes" },
        { &g_abRandom72KB[0],        34, "5606fae500a3d14548ae4d87ac76cc5b039b430db2c52e85838f3752f90899ab", "SHA3-256 34 bytes" },
        { &g_abRandom72KB[0],        35, "698fc06075518e6bc6ae19518fef105536693ae66e449ae1654737f74d7be978", "SHA3-256 35 bytes" },
        { &g_abRandom72KB[0],        36, "d14d4e67cca9eecda54e575ac70189fcba7ff6a192baec5ed1b49cb31f0798c7", "SHA3-256 36 bytes" },
        { &g_abRandom72KB[0],        37, "26e139c6ee4441251e88fa9140494630dc1330e5986638fd358a1cc43222a1cd", "SHA3-256 37 bytes" },
        { &g_abRandom72KB[0],        38, "22264dc5858425ef089803019321a6e892f7d65e8290ec9d1d14950c69e57ff9", "SHA3-256 38 bytes" },
        { &g_abRandom72KB[0],        39, "a5a7f8b33538b0fd66efa56656801a61f97c12d6fdf145c6a121eb03417e9e69", "SHA3-256 39 bytes" },
        { &g_abRandom72KB[0],        40, "32f01debb886b3792de6be774be4ad24fd29aa023aa73a79912ce2dbe58213ee", "SHA3-256 40 bytes" },
        { &g_abRandom72KB[0],        41, "6db83652a8ace783bf009cc45699e13eaee316554ea746c4ed75628e69698b54", "SHA3-256 41 bytes" },
        { &g_abRandom72KB[0],        42, "af81c002afaa1ce4c06728cdacff430598c434ca7e019b5cc783965d881b257c", "SHA3-256 42 bytes" },
        { &g_abRandom72KB[0],        43, "c3d030e8dcc15924f60982e353c1c59079fae57a42fb064b979c5da78ff1fc5b", "SHA3-256 43 bytes" },
        { &g_abRandom72KB[0],        44, "d33b4edbf75dddcacaf2a547714a934cd001eb9ecd5a2a7f4633ba2868ad5d04", "SHA3-256 44 bytes" },
        { &g_abRandom72KB[0],        45, "a7fc1974dd18dad78c5fe4b649a3e634106f0ea2e8fd1a5488cc9e395618e5cf", "SHA3-256 45 bytes" },
        { &g_abRandom72KB[0],        46, "269fafc4016c68275994c6a48411fa06e97e754374f79084f81a6a3b2afbe2a9", "SHA3-256 46 bytes" },
        { &g_abRandom72KB[0],        47, "d2e95c6b46381bf506af9845af4ad5c34b0a046c745e80a1eac9dbd20ce7464a", "SHA3-256 47 bytes" },
        { &g_abRandom72KB[0],        48, "35024a13cf9b8487e0a1c76815cf085beef986509a07ecb179ebb73db727e999", "SHA3-256 48 bytes" },
        { &g_abRandom72KB[0],        49, "d4012893df1482cdf74dabc4b3394395e9eb11d638e69818c14bb6783575112d", "SHA3-256 49 bytes" },
        { &g_abRandom72KB[0],        50, "b8e4231b19c6ceb78c07dca93131d9dcae75dc263ca5bb00fa857b3863b21ee9", "SHA3-256 50 bytes" },
        { &g_abRandom72KB[0],        51, "f831e375922ceafd5478a277e2c35cef17210a941dc82266e3e0b29a08ece983", "SHA3-256 51 bytes" },
        { &g_abRandom72KB[0],        52, "d54c127b8fd298247c6d31afb17b2849e63b8931527d687e3fc71dd22cd4463d", "SHA3-256 52 bytes" },
        { &g_abRandom72KB[0],        53, "54ac308411c45e3a3b921e027bf4a64d738bcae1aef174cc28fedc5d9e10807f", "SHA3-256 53 bytes" },
        { &g_abRandom72KB[0],        54, "17dd8c4ebba743005f76e97ceab0632ee39b85e4fcf143b343e2ecd159bd9284", "SHA3-256 54 bytes" },
        { &g_abRandom72KB[0],        55, "258e0ff160dee3cfb01c9db942b7cc43388ca4459873251a8f1963ee0d85b4dd", "SHA3-256 55 bytes" },
        { &g_abRandom72KB[0],        56, "0d5dddf6e30c93f508b34987c2747b32d5dad3ea222bf03147fd483a9c65329f", "SHA3-256 56 bytes" },
        { &g_abRandom72KB[0],        57, "01d6132d90233f2b6ae40d4881ef2cb5d908283fcb413482759aa89a14d45374", "SHA3-256 57 bytes" },
        { &g_abRandom72KB[0],        58, "1416bf31ac8140f47135c78ad9ceb01c84ce8c31330c63846cc4234f982d1335", "SHA3-256 58 bytes" },
        { &g_abRandom72KB[0],        59, "5605bb41089bccd61bfa9b4dbbb87043f501a5417f3d93f515693f29ab0701cc", "SHA3-256 59 bytes" },
        { &g_abRandom72KB[0],        60, "108bcf872c5b226c70b4323d1ad61f60a94b2fd1e002c96c4f592cf5f0cc95a9", "SHA3-256 60 bytes" },
        { &g_abRandom72KB[0],        61, "1b0339f2b871802a7a7038cf8825192f84346ee55c04deea5db87f98648c19e8", "SHA3-256 61 bytes" },
        { &g_abRandom72KB[0],        62, "52a82b911a628797ed8071c41a349e09be92b49320b360b2a220dab8fb923d55", "SHA3-256 62 bytes" },
        { &g_abRandom72KB[0],        63, "b043822070550ab5a18340c950bf5676274b4442c770ef5c4301d25b5307cfb4", "SHA3-256 63 bytes" },
        { &g_abRandom72KB[0],        64, "d57eea9e407e20c5a5af4bef5319330c2c06a936bf9d6943ca3cadf3e276c30c", "SHA3-256 64 bytes" },
        { &g_abRandom72KB[0],        65, "e6a1a67b5c72926a91316c81f78e60cff33d90d78f5772b7324a99926d01721f", "SHA3-256 65 bytes" },
        { &g_abRandom72KB[0],        66, "6eb286ad53f1b84d24eb69b4350368ca6630fb381c9f9915442b1fcf0dbad04c", "SHA3-256 66 bytes" },
        { &g_abRandom72KB[0],        67, "ac9f8eae18862b36df72387d31b713fa0c38040e13496f1743081ebd36847cfc", "SHA3-256 67 bytes" },
        { &g_abRandom72KB[0],        68, "1b083e91d0d67d491fe71f87429834606758602a1f3b3a8c6ff9d554fac19d03", "SHA3-256 68 bytes" },
        { &g_abRandom72KB[0],        69, "c3dbdcbad247ae031b7dc1f5800b8857d2a7038f582cd530a8dfbb4634c32322", "SHA3-256 69 bytes" },
        { &g_abRandom72KB[0],        70, "604dbd1b9f6c3a9fcb598ddc9113ef1cc4e38f9f711f1ed6444f71b286a507b3", "SHA3-256 70 bytes" },
        { &g_abRandom72KB[0],        71, "bf2372cbc8fb128c2a4ac54e9acc51c37f73a8d64994f62924cae9c60a9724e9", "SHA3-256 71 bytes" },
        { &g_abRandom72KB[0],        72, "b27584e3b982cd937b73442e9b1626cd7c5e305a2f6d0a5aee11d6a2f1d49f20", "SHA3-256 72 bytes" },
        { &g_abRandom72KB[0],        73, "40caa6117a773cd12bb121a6fcd15015548a7273c0b02bdbf411cab4fc1004d3", "SHA3-256 73 bytes" },
        { &g_abRandom72KB[0],        74, "4e92df398fd4372832411aad4cc97607b56437c41454381a1448052fa951cda9", "SHA3-256 74 bytes" },
        { &g_abRandom72KB[0],        75, "47bbe0d5cfb501764956def15b83c8a8a29f12c20d1e0f50a47226046fd5e2e1", "SHA3-256 75 bytes" },
        { &g_abRandom72KB[0],        76, "905bce96097d868e7b52c77df101afb9003ccd158603cc87c2e73d3aa3e7089b", "SHA3-256 76 bytes" },
        { &g_abRandom72KB[0],        77, "fc69a2b6c2640d657bd9bfcf4d578078dcb13521be073eca0f4c3690573198ea", "SHA3-256 77 bytes" },
        { &g_abRandom72KB[0],        78, "4f6a2ac1ef0ad1a065f48aebd20a13fa1e531f5f0d413fbdf95affe066186eb9", "SHA3-256 78 bytes" },
        { &g_abRandom72KB[0],        79, "57ac8ba842322f02b01d8f0e59a8ffaee945d8f2959712c9a3756b84a40a4a74", "SHA3-256 79 bytes" },
        { &g_abRandom72KB[0],        80, "57870b5575b8849dbcdb16f69c8ad47161f84052c92267b5f24b07a25681aecd", "SHA3-256 80 bytes" },
        { &g_abRandom72KB[0],        81, "ae26a9496fec4d6caa862ffc8948f8e70bd5c59f23b6ce7594a0a2d43a554665", "SHA3-256 81 bytes" },
        { &g_abRandom72KB[0],        82, "21f2317c9c6e10396b03f98696a6c060289323afde658c44e1943c154d4b0e4c", "SHA3-256 82 bytes" },
        { &g_abRandom72KB[0],        83, "d3a8a809aafc9852e44acd88f87a66b4376fa226e2c084ebcddd5a89b59aaa53", "SHA3-256 83 bytes" },
        { &g_abRandom72KB[0],        84, "7222c4a3dc927db4f8b10c2d4b7c4a14315a73f344146f83781e5679e0a4386d", "SHA3-256 84 bytes" },
        { &g_abRandom72KB[0],        85, "5e2bed3edf023680143d4c2ad0fa361e6d1f41842094cf4df908311b54529041", "SHA3-256 85 bytes" },
        { &g_abRandom72KB[0],        86, "13adbdd8f6b753f3c32c896240f043834a15ded83931418f163b5c8201ecaccc", "SHA3-256 86 bytes" },
        { &g_abRandom72KB[0],        87, "fd9c24db36b84a0054bb9f752663b5d5ca77bfff5441c6ceaa65948fc521826f", "SHA3-256 87 bytes" },
        { &g_abRandom72KB[0],        88, "d05fc925bea2dc42d0b10a8100dc746dc7590bc4d15b6f4c0b4ab364d69a81d0", "SHA3-256 88 bytes" },
        { &g_abRandom72KB[0],        89, "256f7f16eabc63b3a080c702737518060e83bf62169ec4f2d84febb1122031cd", "SHA3-256 89 bytes" },
        { &g_abRandom72KB[0],        90, "cdc601dedb0627e8ba5b5a9e53230b8421c4776ffec7e53d0fee0adec104418d", "SHA3-256 90 bytes" },
        { &g_abRandom72KB[0],        91, "c9cc24d55dccaf7b9e5b0de3116d8a25d9f564aab207a07b26c0191502bc1e94", "SHA3-256 91 bytes" },
        { &g_abRandom72KB[0],        92, "f3994256c1745b761b929da74cdf987109b29ee5c3bd6436786bcfdb5358a32e", "SHA3-256 92 bytes" },
        { &g_abRandom72KB[0],        93, "d2881b2c298c424e4e1a478f20f36ab8c55a24c79b9329104bafd024b12f51ee", "SHA3-256 93 bytes" },
        { &g_abRandom72KB[0],        94, "5d2d97b27a328df73a1528d6d955bbae6498f6ef9f3364acdf1f7b83e4a6dee4", "SHA3-256 94 bytes" },
        { &g_abRandom72KB[0],        95, "cfa103153e3746324df951b2d89ca1ccdee67e8e3d97a8196b2581ee3060668f", "SHA3-256 95 bytes" },
        { &g_abRandom72KB[0],        96, "344e10910c51c9486caaaf7550f0687c4b6d5fdbaf66dfe67d6383844104e21e", "SHA3-256 96 bytes" },
        { &g_abRandom72KB[0],        97, "e37ce426d69ed6c9b7acb78bda6a59c08ae1243d5a6cfceaa6adb78f3eca2b0d", "SHA3-256 97 bytes" },
        { &g_abRandom72KB[0],        98, "5c9e2a53a2dd0098e62c47b4a1b22fb64004d65aeb0828030e33be77e828b273", "SHA3-256 98 bytes" },
        { &g_abRandom72KB[0],        99, "5cd4c97198591bf91c6565429ed28f89e53815f7e4dad39be6a215a1c2c4cc69", "SHA3-256 99 bytes" },
        { &g_abRandom72KB[0],       100, "ca908c8aa585538640742c2baa10785e4a5daaf128cc94087bd9f880344660f4", "SHA3-256 100 bytes" },
        { &g_abRandom72KB[0],       101, "6011ab054563f201cdb70229cab8bf19372c47321c3d567af535315c08b6f341", "SHA3-256 101 bytes" },
        { &g_abRandom72KB[0],       102, "99e6a8b95d22e796368f48752b49e2c1760629369e8c1d2fd273bf2b81b7b1c4", "SHA3-256 102 bytes" },
        { &g_abRandom72KB[0],       103, "f708c03570a45f25b9abc9764d7b974fe9fe97b0a418194eaa5003eadaa6d428", "SHA3-256 103 bytes" },
        { &g_abRandom72KB[0],       104, "84619f8a92cbbedeaf50712874f228ad3cd9c9045aa60732a3555901ccdafe2d", "SHA3-256 104 bytes" },
        { &g_abRandom72KB[0],       105, "31af9794fa3ad4614d34eca3727f19814db9edd74bdee043482a2d3ae5590368", "SHA3-256 105 bytes" },
        { &g_abRandom72KB[0],       106, "0bf3834a655045ba00e2b0967a643bce3fa89fc2e5f201dcd5b000b52fad4cce", "SHA3-256 106 bytes" },
        { &g_abRandom72KB[0],       107, "784acc66db73d12a8e49f0319db17e10a0667bafd58576076bd009a629dddf12", "SHA3-256 107 bytes" },
        { &g_abRandom72KB[0],       108, "02f7c426f430b3d082ee899ca85112d3e5286f88bfe0e90f0c623634c7e03c98", "SHA3-256 108 bytes" },
        { &g_abRandom72KB[0],       109, "b438b0781a78ef42a40f680f117b65a592128a197536d115e8b9ed9037f08134", "SHA3-256 109 bytes" },
        { &g_abRandom72KB[0],       110, "6380cd9e9e93dbf931efdbf84ca60560b9d2d18b517485a0ae0199db55e830b2", "SHA3-256 110 bytes" },
        { &g_abRandom72KB[0],       111, "de89367fb625762274b37f205a9318730168616c153400f2d3e6460e49b56dba", "SHA3-256 111 bytes" },
        { &g_abRandom72KB[0],       112, "514a48d1734e4a4ba0d555a72fe48125efd7f0adba027ffa52c722a7911fa806", "SHA3-256 112 bytes" },
        { &g_abRandom72KB[0],       113, "4ae2bf547fcd3610199b96431d634696400c9c139ce813a567da50a71037ebf1", "SHA3-256 113 bytes" },
        { &g_abRandom72KB[0],       114, "84bc4a36d34c6637c470872be38154d31d493d9470b075a0383ddd28cdbe9eb2", "SHA3-256 114 bytes" },
        { &g_abRandom72KB[0],       115, "b863d5691e4c89063e2c2851d992c7f5324ad62e79f68350bbaa8b0025e15f61", "SHA3-256 115 bytes" },
        { &g_abRandom72KB[0],       116, "29a8b23a03b1ddba9a7bf2e95cbd8414ffe8dd20bef6c4895989bc5b98fe409c", "SHA3-256 116 bytes" },
        { &g_abRandom72KB[0],       117, "1d89fe8bad953887b7e9b1d0f356f34b8c6f4961dcc27b634843ff7702635a92", "SHA3-256 117 bytes" },
        { &g_abRandom72KB[0],       118, "60f5bc20e290c189677f4c91ce97a3334c21cda5196c670cd84c076518536f29", "SHA3-256 118 bytes" },
        { &g_abRandom72KB[0],       119, "9ef5150c77dc16e8bbd04386ec076065dd5fb6d77af39e41740d861ad52f9bf1", "SHA3-256 119 bytes" },
        { &g_abRandom72KB[0],       120, "235ebf0c8b52c19986e1a63ca0ef63e88e83343ccd286a09a8e893c9d2530707", "SHA3-256 120 bytes" },
        { &g_abRandom72KB[0],       121, "27666c10e02ac3d3ab7a84a46b6e82edd7610403fdb4fb69527057d05531cfb1", "SHA3-256 121 bytes" },
        { &g_abRandom72KB[0],       122, "5200ffb7dfb207ae1fd3dfa76ca47368f2f72895a4ae91eda4c226d059a28b5e", "SHA3-256 122 bytes" },
        { &g_abRandom72KB[0],       123, "b04bf4eae46d4372ea481637a110c716aa2fe077adba5652ad195c58fe9bdb30", "SHA3-256 123 bytes" },
        { &g_abRandom72KB[0],       124, "dc84dfefe157486e2f705a572af10f73a840f92a6eb55261a776966b046f7645", "SHA3-256 124 bytes" },
        { &g_abRandom72KB[0],       125, "c7fc5d07ae481a3a2c70d7ac1df5973301f416ec427919a1cb1552a07e31ceb6", "SHA3-256 125 bytes" },
        { &g_abRandom72KB[0],       126, "f155c1dd830afeff3964fae4a0944017b8557bbc3304a33e629accf0eb66dbde", "SHA3-256 126 bytes" },
        { &g_abRandom72KB[0],       127, "d31cad30dffc49ae149d3cd369df776a58ac2f5d1deee03a869eb2e8ffcbb1c5", "SHA3-256 127 bytes" },
        { &g_abRandom72KB[0],       128, "6c42f5ff3377b65bceefb5b16bd5048fd7a605b3c538b7215b1c7b6e0126041c", "SHA3-256 128 bytes" },
        { &g_abRandom72KB[0],       129, "b7f894e617320ec30587f0b678f5cc6c238b6f94ef16f1e90935c4f41755acb1", "SHA3-256 129 bytes" },
        { &g_abRandom72KB[0],      1024, "753ba264db6e163510a9f464b231a3eb9284d077f3785e527aaa4e33049f9e5c", "SHA3-256 1024 bytes" },
        { &g_abRandom72KB[0],     73001, "7411763136254c7c9846ba64be8df5797da17d8412ab064f7a2b3ef669e138ea", "SHA3-256 73001 bytes" },
        { &g_abRandom72KB[0],     73728, "8736966ca1364256f5db9cfc1803f3989f5e0a2517cead959788cfb34053ccbd", "SHA3-256 73728 bytes" },
        { &g_abRandom72KB[0x20c9],  9991, "4f343c7135ba2023b9bc918e8926fd55386c9f7d33138cb31f30b89c00b22e5e", "SHA3-256 8393 bytes @9991" },
    };
    testGeneric("2.16.840.1.101.3.4.2.8", s_abTests, RT_ELEMENTS(s_abTests), "SHA3-256", RTDIGESTTYPE_SHA3_512, VINF_SUCCESS);
}


static void testSha3_384(void)
{
    RTTestISub("SHA3-384");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],         0, "0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2ac3713831264adb47fb6bd1e058d5f004", "SHA3-384 0 bytes" },
        { &g_abRandom72KB[0],         1, "351a6f908eae0c58a7bc6a9baff7778ffe794b2056dec37b5564fcdf5e5aba3fa7739c5032ad7e1fecb7b8764928f002", "SHA3-384 1 bytes" },
        { &g_abRandom72KB[0],         2, "52805f0b7fdf522da3f138d3a1bef821b31383721eeb10de12d503d3359eeb491693cbb9cee4cdb0d3d21122b7e5c582", "SHA3-384 2 bytes" },
        { &g_abRandom72KB[0],         3, "73c573da6e0fe95bf52c74ca90a840f2c7313e1551fea63ce4d52dcb3a1de4d6eaf93bd00e632646bcd073165b37e56d", "SHA3-384 3 bytes" },
        { &g_abRandom72KB[0],         4, "f225dd6b5ca7551c90dd02e8b6cb2897de0d94e51f82122386cee7c9dcf626bd51fe221624a0e1c6f8670ca033ca211d", "SHA3-384 4 bytes" },
        { &g_abRandom72KB[0],         5, "70e82b29ef43e63d66aa750afc62c9a7a50fafb43de8ad25ee2ec48d310a71fe36996cf01ec4a7d0b6eba74a2c9a7814", "SHA3-384 5 bytes" },
        { &g_abRandom72KB[0],         6, "a987f80ad4dd0fd315529233a33233b34af4030e4a3be6ef27992d5e04ee1669a1c7799075a7cc514243f6a05e7b6303", "SHA3-384 6 bytes" },
        { &g_abRandom72KB[0],         7, "b80eb8e14d5a0547c7296376d08af26b3b9bdb4366b355408ad2e8f869a312006ef5489208cad08374c36879be01e54e", "SHA3-384 7 bytes" },
        { &g_abRandom72KB[0],         8, "ab74e8ee36754243b7261d68f82956a70c0e4b3e2a98579b92ebd1c9123c37907760012a8bca8da41ed6bc60c1dcdd5e", "SHA3-384 8 bytes" },
        { &g_abRandom72KB[0],         9, "2d48b90f6b595fb44cccd5926d4877f0a4a31a9d3055d57435ded8fdf97e2b235db849a73406aa8a02708f3fafd94670", "SHA3-384 9 bytes" },
        { &g_abRandom72KB[0],        10, "7540111d23ff8305deba8c60a739da70ca0e78e57a9fe07075f9bca229a13fbec6329916ff4697e7bcac9dc148738791", "SHA3-384 10 bytes" },
        { &g_abRandom72KB[0],        11, "31a3fdafc1a057e487d40cbd5e65d8152d90cfb7362707a2f374851beae1e42f32755001d96848e728afe3cf88a5dde7", "SHA3-384 11 bytes" },
        { &g_abRandom72KB[0],        12, "3f581f6a75f2cf5702a879a3beb2b424e40c066c1bac767d8cfdcd2d4b967f52cfa3d0f7e47b7bc03a50c75ce56080a6", "SHA3-384 12 bytes" },
        { &g_abRandom72KB[0],        13, "ec49f22fc85a6b813f7222c9799e9fc220838462e8948ff46ededd73b888c874f0ddeb2a85dd0c9c3a848f822be06522", "SHA3-384 13 bytes" },
        { &g_abRandom72KB[0],        14, "bae3eb3815b0eec7f834292a21d5de705657a9345e7029599758e509ef2c0fbfc35c13a2c88605d505373a213fc4e37a", "SHA3-384 14 bytes" },
        { &g_abRandom72KB[0],        15, "75bf52c9a95f795a97571e8734c368d023b850dbcae9e33f38b7135a941675914be511e8a3d49292e2073ea7acd1ccb8", "SHA3-384 15 bytes" },
        { &g_abRandom72KB[0],        16, "ec0d76053f71ff3c78f3a35161cc50e90515398597d265f80f0519beec5829783874453661d6c5387fe6b92b63948060", "SHA3-384 16 bytes" },
        { &g_abRandom72KB[0],        17, "75bff0a4214cfc36fdcbf1dc4a156a1ff014c8337b63a97cf4a95edf0ff940e31820e5b618261fb93ad506eec600518b", "SHA3-384 17 bytes" },
        { &g_abRandom72KB[0],        18, "08eec95e0109eaeb5a152e43a96dc4568c02580b0da6f4772dfbb6b93a6fec38db201597ea5dc9cc64c39c190dddb671", "SHA3-384 18 bytes" },
        { &g_abRandom72KB[0],        19, "0797dd6277689061f5590cfb6fc2331c0f0e6d197301b1ffea2b1d87d5bb146cbed54de3dd907c03bf36c2d66b09188f", "SHA3-384 19 bytes" },
        { &g_abRandom72KB[0],        20, "0976f1e66c6e22ac5c42bce96f6fcd0f80ca2c1360ac55d22be5b65cb042a0b226b9c69c38a8221ff9ad15468af3d114", "SHA3-384 20 bytes" },
        { &g_abRandom72KB[0],        21, "efa2a237fa24bfa349f026b49e38243b18dc9a67ab812c802115bf4be4f4d2d38f8982ae17fe2b95e429ead2617b81e2", "SHA3-384 21 bytes" },
        { &g_abRandom72KB[0],        22, "c8fd4c651d40550fa11b25b2e5beb4c82e0b8deb35cbf15774a60ccb59c1e49663ce003d9b9b716ef55e7e9f9698c4a5", "SHA3-384 22 bytes" },
        { &g_abRandom72KB[0],        23, "a3ff1d39e935e7ad9db2006f407aae652101684046d05933a6625e9b41f11365754f79bdeb8c448aa186994360fb5d8c", "SHA3-384 23 bytes" },
        { &g_abRandom72KB[0],        24, "ef3a883129fbe6a3a484c1cf402d6ac4bfa403d9976940643a4e3d3d0aa08dad6f2f1b129b95d9509db392ac1783db4a", "SHA3-384 24 bytes" },
        { &g_abRandom72KB[0],        25, "c38974c59f2d39826f277358aaffd08d96e9a17057b7040ffa996dc0ae9b3ee2c8f99a40de8b14da6668cd48bd290ae4", "SHA3-384 25 bytes" },
        { &g_abRandom72KB[0],        26, "e4342539692f426b15dba992704745e685b1df1442b63c2e23efd725e3fe2580982106af450d640f8fd53dc6e21cd666", "SHA3-384 26 bytes" },
        { &g_abRandom72KB[0],        27, "c0f7bddf4ce882387d733ee4bb61c55f7e85066c13e68bde2d457b6734dde13c1d2970aa24ab1fd9d755f2a4534ad72c", "SHA3-384 27 bytes" },
        { &g_abRandom72KB[0],        28, "66d1e453556618070cdf1d27364296ea8f12b64986250b4dc3491da2c9908c15d3eb731e48d77c9a2ff5ff985312e750", "SHA3-384 28 bytes" },
        { &g_abRandom72KB[0],        29, "e1ececffae0e3e58d2dd4e834edb6365830e28c1fa37046c2368fc3c7a1adf5da4ec9437a9b526db1834e53e409764bf", "SHA3-384 29 bytes" },
        { &g_abRandom72KB[0],        30, "b86f6e5485cd773ac37ed59a06e44d2f68553f2235695988024b84d9ea311db1634124a23021d9879d70f3104ba92bd5", "SHA3-384 30 bytes" },
        { &g_abRandom72KB[0],        31, "853e6213578f7324056b05eec706c976346667051b50ea4f7dbfe8a3752a40c93af76e517b4ae3e849d50b2c41d06b42", "SHA3-384 31 bytes" },
        { &g_abRandom72KB[0],        32, "8d12a1665736d1f197de900387597965701029b3c898e3b4cbe81fe3838b457c9116eec6fd3ede7baa5aa88509af42e1", "SHA3-384 32 bytes" },
        { &g_abRandom72KB[0],        33, "c014fcfa6c0c02f83dd88a2be34f57ef5ba711319f974468a3835b36dc65c8c2a1a6a9f7a58c068d57807228989c64d8", "SHA3-384 33 bytes" },
        { &g_abRandom72KB[0],        34, "d82b000e22fd142f9bbefe67514f05b8b3a59fac3c393a335f12899ad8d60445dfe644c7638e164d08acfe6ebc1477d7", "SHA3-384 34 bytes" },
        { &g_abRandom72KB[0],        35, "6fe7de0e226cbfed362b33f3f2281ec3943a7caec2e824829c24f23a31e3fca9d9e375f55d6a1f64d7b495a4be652bd0", "SHA3-384 35 bytes" },
        { &g_abRandom72KB[0],        36, "b5c28e85b4ce16b6fa15e08290910941e7a3e6db49debaaaea4ac1616e38362daa73974a88b7e058738608f8f7e850f2", "SHA3-384 36 bytes" },
        { &g_abRandom72KB[0],        37, "3a1f8c9a52272e8b167d7df37a40b470820b5ae455b89290740fc02085212aa6e56f874edec5c9026a933a21c4e5b249", "SHA3-384 37 bytes" },
        { &g_abRandom72KB[0],        38, "d1c1b8aa38cb26a57cc31f9e4409a9fbde4f345b85c1c5d0b5237f61aeb9e19aa676b3a58abf5a484d66b83cc7635220", "SHA3-384 38 bytes" },
        { &g_abRandom72KB[0],        39, "81cad2561d80d5278635f1ecccf7e60550fb1276041c18e386fa78392106c000c5f946b0963f176b920f8ed5b2d51e88", "SHA3-384 39 bytes" },
        { &g_abRandom72KB[0],        40, "18d87d1e74d4eedf4ba7816dbc4d0249f69869d4d2a73af5558b7678179344ec89dd3afe4b71171ffe43fcaeed6f6780", "SHA3-384 40 bytes" },
        { &g_abRandom72KB[0],        41, "be04c4f1b70fdbb8741b9f1fe3712e60b56ac1cc3d157856e7dbaa47d0dab9c9ed3677bbd5b38deeb89c805befb00b2f", "SHA3-384 41 bytes" },
        { &g_abRandom72KB[0],        42, "945c8c93784f3bbdb625866bdbc506e20bea661d87de978005a97bf68178c55a4faac033f38d0231cf35af57c3b13c23", "SHA3-384 42 bytes" },
        { &g_abRandom72KB[0],        43, "3c2fff20cc9405f022d4571d30c9cbf2768dc48e95d8eefdf16bd1f5a6d07027baa0a898245dda935efd01226f239fcd", "SHA3-384 43 bytes" },
        { &g_abRandom72KB[0],        44, "9fa1bae02937e2799e4c053814203391991e9492217166c5d525c245f1f898d86ac0c93a6aab4c26159004be867e108b", "SHA3-384 44 bytes" },
        { &g_abRandom72KB[0],        45, "3d05fc09ec76cad99b43b9576d44d4852dbf26a58c7ae6af2d4f211493ff8ce3b6da71e05e60096a8f7d23b9acc795f2", "SHA3-384 45 bytes" },
        { &g_abRandom72KB[0],        46, "54650f8665809f950b92cd1449ed63633d96dff84b349cbca281dc7547d2c2ae554decf114fe32c066608c27553778b1", "SHA3-384 46 bytes" },
        { &g_abRandom72KB[0],        47, "6122ab61add284f058ee45f0aaa209813e0b6503f907259a968e0b0fd706b6bcdd376a37a62e3dcb31e2e5a45e940522", "SHA3-384 47 bytes" },
        { &g_abRandom72KB[0],        48, "5e2aa542591d5148e2b975c41e606de6877ebfcab832a89d4781a91688e76b5a2f30d6d289521d06363c1321975d1cf2", "SHA3-384 48 bytes" },
        { &g_abRandom72KB[0],        49, "6dd1d9bbe2cab7d4ba8756a18a5978a3ab54eae07c068be07d6eb0ebf61f67a810281acff3df609821eb998699156503", "SHA3-384 49 bytes" },
        { &g_abRandom72KB[0],        50, "dafa71cf50e1f80e6200b2280ec2a70fbd820783817ec40d811fa0d7dcd3ca80242d374b11b6c43497f943e610135b97", "SHA3-384 50 bytes" },
        { &g_abRandom72KB[0],        51, "2839c75d8751796c680b9be0029eb3395835f87e653625c9954d161cea78c18d35fb96545ed71f8793cdc5153f3462d2", "SHA3-384 51 bytes" },
        { &g_abRandom72KB[0],        52, "899c196d598401a2167ac88e452f76f1d78f039b4edee7e3f0d55bb6c877afa280667d0c6fbb74f6e4138aa6acd417aa", "SHA3-384 52 bytes" },
        { &g_abRandom72KB[0],        53, "03c3d35a19b5f93121851a3aaeac2047c66cd128f55f7cd81eb6f55c79244571f10e0660e9c8f84408b93e3fb7212b55", "SHA3-384 53 bytes" },
        { &g_abRandom72KB[0],        54, "016841b49625dd79f166ed9e9d0ab6628dc20a45ec41edecf8aae506db8059b51a81379ead0347e02f752839d947e8b3", "SHA3-384 54 bytes" },
        { &g_abRandom72KB[0],        55, "b0873afe8b3b4cb37f34b9afd56bc9c879b58438f2dfaab09c6dc7490285db4a9f8261dc2dec9fb7a9b0ec79a663dfe9", "SHA3-384 55 bytes" },
        { &g_abRandom72KB[0],        56, "bdbbac0fe4dd5cfc6a7b7f4bd8d7d4dfea2963ffa7fc5e3d87d71943b9140b65b36db9b91d512ef37031be3f173e45d5", "SHA3-384 56 bytes" },
        { &g_abRandom72KB[0],        57, "c5205b3182b49ef264f97e1fb04dabe0f6c83409799f77d29713771f4335083e0ebee42b66694913c62131149dbfd34b", "SHA3-384 57 bytes" },
        { &g_abRandom72KB[0],        58, "9324d23c2d96c321e9844808afdf82aa9dd2a13d6f850fa20abc942c77e4628f3632dedb25bdc2a7de81e34d0a683781", "SHA3-384 58 bytes" },
        { &g_abRandom72KB[0],        59, "99017701633107f973e51c25d11a5fa3a05138b079901ba9a3ab2150658bb37ddffedbbd66a9788c87e652c0a16ff963", "SHA3-384 59 bytes" },
        { &g_abRandom72KB[0],        60, "a336e96a7e06f6acc63c9f39a89689e71db54a28e69ffb58b88f97b5422be0b95bfc75105a0baf0d0c88ddb5b9b42b27", "SHA3-384 60 bytes" },
        { &g_abRandom72KB[0],        61, "da0bc2c07c2e5d7aa461f335be8df794e13a696a53dd1be654ab3b15edf4289a36b7cd53a811b7366250ff3437b5b011", "SHA3-384 61 bytes" },
        { &g_abRandom72KB[0],        62, "1e7bc6ca22c38de4b3fd17cde00cd98273a04a3cebad3375544954ce226cf7fe49bfe9edbbb3d611a622ed7057049b68", "SHA3-384 62 bytes" },
        { &g_abRandom72KB[0],        63, "9f12eeb4b46c504538d30ca7ee816ad44d2b2ec9287076ed5fba572663100a14223177545c97f172792cc5dcc121734f", "SHA3-384 63 bytes" },
        { &g_abRandom72KB[0],        64, "d5c58d56e3bec6e7c3b99c2bcb99435ea523eb7b2aac01859d0ba496874a3fec6d7ab9b2c1581543ea19f3363e04e161", "SHA3-384 64 bytes" },
        { &g_abRandom72KB[0],        65, "0261d01bb6498f01ad42cd7a624b4f7fbc4dc9bcb1ba4bcaf94084a4b9bd357819c4cf41a60a2e7d90414e3a10b3dfaf", "SHA3-384 65 bytes" },
        { &g_abRandom72KB[0],        66, "b94a286367a68d69d4c77eeaa5b03cafc1209f5ce04b98f6218a71150420da2fa786d8b91ae4f20c320d380830466eab", "SHA3-384 66 bytes" },
        { &g_abRandom72KB[0],        67, "83355d1f784c7c5f5e97cce9427006706720191ed729f576b989a136aed99eebd835d5c934df5094db8e01d3534bc349", "SHA3-384 67 bytes" },
        { &g_abRandom72KB[0],        68, "26895ff45f1d409fca25c169528d880e811b0e2aa57891b783d9606695e040bcbe9b1ec5bfe697bf80711afb075849f3", "SHA3-384 68 bytes" },
        { &g_abRandom72KB[0],        69, "44744ffeee86553383e311a2f3af39e6a6c0e699794af43be2fb69f07ea843b67cbef422ac14fcf23be79e2150e55e53", "SHA3-384 69 bytes" },
        { &g_abRandom72KB[0],        70, "6d8e393588ee37529e989ef4250c5d4d00effcbbe089ac3ed44ee3212f03162823f264ed1ddb2241302f9e3addd438f7", "SHA3-384 70 bytes" },
        { &g_abRandom72KB[0],        71, "1717d8e9793a0b624875dc9e68c36cf3abc0164bfa3e3d291e36663ca144d3298dffe668726e59984b12ff2c28c9c394", "SHA3-384 71 bytes" },
        { &g_abRandom72KB[0],        72, "b67fd24f2684a0353f94e97041ae439edeabf9e8dcb8c2eacd01821085fc31ed9d2d46f678aaec2f12211ee8118d17e7", "SHA3-384 72 bytes" },
        { &g_abRandom72KB[0],        73, "08fbad7996bacab84baaa84783c57be6144fa1e9306865f3157f59e60986c831c50d24ac67a8db1d957db7e98812809d", "SHA3-384 73 bytes" },
        { &g_abRandom72KB[0],        74, "f7dbeb3e8eec8dc93c8f43e7531208a2bc6d6b18bd6bed67aaa81490bf1e334b926c750f254c9c0b1675d69f93612a4b", "SHA3-384 74 bytes" },
        { &g_abRandom72KB[0],        75, "277012f450a023c70e75ef7bac78ce8a6241bda7f4d9f41261aa77625062654c949fa1642867be894eef4c967e4a3f5f", "SHA3-384 75 bytes" },
        { &g_abRandom72KB[0],        76, "f21f4c2110e24f2df77d6d97e8dd2acacb172f8929b78358a9cd1c4826cac04f11b37b4af205b08c5be16188e4ba4e4b", "SHA3-384 76 bytes" },
        { &g_abRandom72KB[0],        77, "d2ff0145b121a78743806a1af5f8fd054d5a0cf86272179754e05e498535fa05733c70b7f70d872429df35c3441c64ac", "SHA3-384 77 bytes" },
        { &g_abRandom72KB[0],        78, "a728f1b1e30a286d31e8736b95284053f76b68367645bbe30f14838732c6b6d24aaef815a9bdc76fb86409c72ebeee2b", "SHA3-384 78 bytes" },
        { &g_abRandom72KB[0],        79, "d509adc96cf2844b9c4df2008205c3a7b1ce8c2c071c225dc34c724eb79f961f29824f20d1bdc2ed9904822b9e7bcf5d", "SHA3-384 79 bytes" },
        { &g_abRandom72KB[0],        80, "92872f5dd6f71ae3aadc4b717679893069ef2e9712d5b1ddb13d8ae70af58d5be0aed753b6e8a5264dbe37f424e848dc", "SHA3-384 80 bytes" },
        { &g_abRandom72KB[0],        81, "00a8805d752e1e7a8c2eee08b7aad773e18e97bb22814189471c5faceaa0be093315c8ca74a5f2e3b847cae1cc521fc6", "SHA3-384 81 bytes" },
        { &g_abRandom72KB[0],        82, "1c91d79d007353e898080e24ccb64d997b83590cc875ddc4e3e264ca160efeb731f04391197e9b3f135fc116461c0a1b", "SHA3-384 82 bytes" },
        { &g_abRandom72KB[0],        83, "669bcf3fa978752a5b7b4199ce6cf5d876a3073519f3a73330f08b392b9aec6f0305ab52f99f634ffa293dbd852cb7c1", "SHA3-384 83 bytes" },
        { &g_abRandom72KB[0],        84, "ac885cfbf0544756a4e6cd77270cfd66edf74dfd25a4f19181ee762a77895d93257193ca1a60000946b682b635ab6044", "SHA3-384 84 bytes" },
        { &g_abRandom72KB[0],        85, "c32073b1b2cf925ba6ebd130a12b98e34826ef5669a5dc1fa8d3320d2533b27b9065e3565a8c0f8dff044421a9bc585f", "SHA3-384 85 bytes" },
        { &g_abRandom72KB[0],        86, "793462351767807335b0844e6d4878b3ccfd4f594ea86ee111c642d19ef08427071caa3ea2d52a53293dd897c4ce9646", "SHA3-384 86 bytes" },
        { &g_abRandom72KB[0],        87, "2287c449b17f68b63412faaf1d74148b86412ab77643dbe227a06327416b51e9b94116df822a52e0839c856c0cbb1ee6", "SHA3-384 87 bytes" },
        { &g_abRandom72KB[0],        88, "6bc32f874a67eac1366456b704a4d0bd2fc611392f6ff339e8a5feaab217b101b827f89cc66647a1ccea4d739552e3d0", "SHA3-384 88 bytes" },
        { &g_abRandom72KB[0],        89, "e322a1eb4c9d65cd25be405228cefabc757c49f1a1e954e6e2c218108449d5ebe5c444144e3d43c96d5736c4162a061a", "SHA3-384 89 bytes" },
        { &g_abRandom72KB[0],        90, "678df2cffd333044f1dd056e257e2bbf99f0daa0f9ee5edcbe8e0c3bde8c9d5b3338939b8cd7de9039d24e873e3a34f6", "SHA3-384 90 bytes" },
        { &g_abRandom72KB[0],        91, "f5487ff2ec0b0db09bffb7994749a76cff97424e5304ddfe650a19145a667e3ea5128b7455015be20b6fa950803299f8", "SHA3-384 91 bytes" },
        { &g_abRandom72KB[0],        92, "a54f6fe4262eef9b24daf0cd9dcd5f49696f4b0b3fdfa7976c24faf04ea7242d65246844bdf9e774b2db9d134176fe98", "SHA3-384 92 bytes" },
        { &g_abRandom72KB[0],        93, "025aebd069a29ff9145831843819875491a7833ecf2cbb9271f8b9075bc7a5601576706f47c45dabf1383aaf267e6406", "SHA3-384 93 bytes" },
        { &g_abRandom72KB[0],        94, "6b3bee378866a5a886241343219459ca25c4092c47e8498632043f6b20a91d7d51a0fbfc92b75ff8d83ed81d1db8517f", "SHA3-384 94 bytes" },
        { &g_abRandom72KB[0],        95, "02adf1f5edebd714c6b4717b212cad4ad80b5ebc0ea9cbee28b2eb2c16142001f829a42e789b56b05a39347afa962bce", "SHA3-384 95 bytes" },
        { &g_abRandom72KB[0],        96, "2f2257518e832826e833619221b721af3940e053b3fdb493143372289aa9687ffa1f0ede725ebde6a46921f7fa25d0e6", "SHA3-384 96 bytes" },
        { &g_abRandom72KB[0],        97, "1638dd76882713a5da153c466b2c8721432b9b1241cc10ea0e2c4f908ea562ef7db4257c7013e85c63b3451467f68f86", "SHA3-384 97 bytes" },
        { &g_abRandom72KB[0],        98, "dc19989c29044e28ec9dc1a15c8a3f9b3ed428f1e24d7d311e399f42e6b9707e8d26e0b83f426944c92f3ecd9fc145fc", "SHA3-384 98 bytes" },
        { &g_abRandom72KB[0],        99, "98fe06676c51ac2daebebc2541366bbaa44d7681b2606282d18ca62432adfba14bf3025657f45487a5e467d62a907923", "SHA3-384 99 bytes" },
        { &g_abRandom72KB[0],       100, "633e3ca7c740c42deb29cdce84b146d403af6467240f414a01d70a01dec4ffefbeedb4fd7f87ea10ef17fa8e2f95839e", "SHA3-384 100 bytes" },
        { &g_abRandom72KB[0],       101, "265fc450e8fe8a939c3d1c3c3adb57f0c2fab198419743249462b9587f963c5b15ebe917ee2870cd5b88b4a22892b2f9", "SHA3-384 101 bytes" },
        { &g_abRandom72KB[0],       102, "c13be9e4720cbd2dec27b15e39cb348fd36278f09d4a809142256ff943c91400c911ddd685b029798103a07e27d13ea1", "SHA3-384 102 bytes" },
        { &g_abRandom72KB[0],       103, "df4b85fac28b97c0a9530f3658b43861426ce9b7178e6d684a7a19e62527e65c7e02e6a21f70bda08cc7d77891cc1e7d", "SHA3-384 103 bytes" },
        { &g_abRandom72KB[0],       104, "dc5d36d735b0db6ceadb6a89ee9fd71348fa92c6c8b6ff5122364e507f0850a8bca5de046dbaeb072ade79ab90ef6554", "SHA3-384 104 bytes" },
        { &g_abRandom72KB[0],       105, "4405f031f7dc2bff4d4edcc19225ca48f87a6d26863293505a4750f08d37b8acb345e81eb7f3720da34badde724eb8a9", "SHA3-384 105 bytes" },
        { &g_abRandom72KB[0],       106, "fc77e20166999b025d11dbfda2ada5ac55e79a13f5a28834c802af316d8d4dc1daabc884e9f4ac28ef71598c44632818", "SHA3-384 106 bytes" },
        { &g_abRandom72KB[0],       107, "c738f15bed5c991816fece164170da1a13623d1ce841a36d03825f52da5b667b359efaea48d98146acfec23e2292c19d", "SHA3-384 107 bytes" },
        { &g_abRandom72KB[0],       108, "05e12cfe915c8cacb57bf65a298f5257d5991af5276eb21d1a71e5209143fabd52ab18488c50d919dff840fb58f9f4ad", "SHA3-384 108 bytes" },
        { &g_abRandom72KB[0],       109, "30d27e43a3e5c8ea5d246e93325768f578b00059b0c83ede760d591543512c58cd879283ec68907c6cf2786fcc51e422", "SHA3-384 109 bytes" },
        { &g_abRandom72KB[0],       110, "ca8020c153045996c1db89b8a9d8715b87725af59d467d4647140cae82a976b911ca5be702119a8006f3c695dbc53133", "SHA3-384 110 bytes" },
        { &g_abRandom72KB[0],       111, "b902694b31ae535f488ee74202348d3c2a69664f416b1751a009831b660a42584c9a13818da8223b4a597776d507becf", "SHA3-384 111 bytes" },
        { &g_abRandom72KB[0],       112, "7a5b4a263964173138ac85fab23e69e3ee43754bc06a92d712773d78bd182642a0d7640bd1efef9f16dd5a3a4c11742f", "SHA3-384 112 bytes" },
        { &g_abRandom72KB[0],       113, "04fe25a667631b12a421c0d059fd632c9f91050fa219307571c95f927f58838a67f92563a256737f0d0667964d6d044f", "SHA3-384 113 bytes" },
        { &g_abRandom72KB[0],       114, "9253e185ad4ccbd6ad51bd4f9b15b972d187be7b13bec3af0830d4f38865c77726a52b0316b6d8b2fc90ace13b456325", "SHA3-384 114 bytes" },
        { &g_abRandom72KB[0],       115, "d4379a4a6f2c5e13e945089d4d02b19d59e4b5f4566e74f491b5dacf6a3b10539daea4d13f53bfa3506ce6ad0436585c", "SHA3-384 115 bytes" },
        { &g_abRandom72KB[0],       116, "a95251f8b89a5b45e61040ce0b6f9705b863f177ba7fec27f02cead8a7e1684ab3f9736fb4d6ac688937a40a1be791fb", "SHA3-384 116 bytes" },
        { &g_abRandom72KB[0],       117, "61ce9c3bc8fd2d2500ae8f6694c66c918facd2cae77b50dc0247cc25e8f1bbdfe6b6d1e92a429dc9f844216e3b8acf17", "SHA3-384 117 bytes" },
        { &g_abRandom72KB[0],       118, "763a1c60a5356ea751ff166e6e9f9a981af3833fedf127cf9db3df2cd4038ebbdbfce2ec6eb1e3a6825a8810d9706952", "SHA3-384 118 bytes" },
        { &g_abRandom72KB[0],       119, "c93f6d4ffa7fca49127f0b3a85555e048bb8984f62f31303614d582757829917292da620a248cf482f18ab61d299e0fc", "SHA3-384 119 bytes" },
        { &g_abRandom72KB[0],       120, "ea255bd5294daf26e46c36333d677a445269e0a5e1cef27a4123c120afb19a6c5ff9273e849c4e0ff41be657ce484293", "SHA3-384 120 bytes" },
        { &g_abRandom72KB[0],       121, "4830fe904db9b4b17766a5fe346720259805e7e02798910c0134377224e4632af0e451e01f1e878554325e203d1650b6", "SHA3-384 121 bytes" },
        { &g_abRandom72KB[0],       122, "99781b361e73d5eacb0dd02f2f87fe4a440b14942756aec6f8afbec276781315782bd20fee1fff2a4f2a77057d797748", "SHA3-384 122 bytes" },
        { &g_abRandom72KB[0],       123, "470d3d508fc216e1a1e6b7d98646a01144175758421b0945e934d7b3fd43963fe61517d99757565868d4e94dbff56cfc", "SHA3-384 123 bytes" },
        { &g_abRandom72KB[0],       124, "8a9c1715fe95519a8f5b3978c581840d0dd1feda450f28163b5c0497111e04771ce90b31d3c786075092d89c167e2007", "SHA3-384 124 bytes" },
        { &g_abRandom72KB[0],       125, "8cfc174c8567f4a7cdc02b11db37dd915a28b3da8099f803a8440b6bf6b11bfd7afbc26b0c0edee50500ba26bd41eae7", "SHA3-384 125 bytes" },
        { &g_abRandom72KB[0],       126, "fb89ee296bb8ce1ff1f787dafaf066aa0543bc154a7b2b5f395150e16d30dd9411fa9a455106e478119f7bf4f3445f04", "SHA3-384 126 bytes" },
        { &g_abRandom72KB[0],       127, "02021f9486fdfedba2b51ea503922f2e4bbb5a0c01b4e0f4c5a7c4a22193c69333eb6f1b4496dc23fe0cdc3f09125cc3", "SHA3-384 127 bytes" },
        { &g_abRandom72KB[0],       128, "bd0b400dd2869c5332a041e8d8264f445f2d8ffc727f5efa38745c5a81981af6acfec6b8287543bf8100aad7cdd0b375", "SHA3-384 128 bytes" },
        { &g_abRandom72KB[0],       129, "9a5282d4d4993ac395462551adc75feb644fb97bf4ea9d5b582fdf98dc0e7b70f9b075d150d11b3c8960629ad46a4de2", "SHA3-384 129 bytes" },
        { &g_abRandom72KB[0],      1024, "f1a46e19369d97b0d3e50a23690d25d95592647d118cf0433d309024d7ffc11a69e172bee516f4e69b4a855c05a0a69c", "SHA3-384 1024 bytes" },
        { &g_abRandom72KB[0],     73001, "6ba059a8af7d72660261347867e5308a80ce4c41159237cf6aabdd03cd86fa9e38a9c93b37ea86c63afb7e3a61348355", "SHA3-384 73001 bytes" },
        { &g_abRandom72KB[0],     73728, "e69e5ab83d5cfc56bd36fc256229344e74d50f04de6c0c6a94adc7ccf674fbbbf8f9694dc1dd958cb41f0a65a78fd8af", "SHA3-384 73728 bytes" },
        { &g_abRandom72KB[0x20c9],  9991, "0a2f284b452e440815778749ce156a666178e771f27b06b44bf55dd10a7ddbdbd0e149dd1112fb61666812ac9a11b213", "SHA3-384 8393 bytes @9991" },
    };
    testGeneric("2.16.840.1.101.3.4.2.9", s_abTests, RT_ELEMENTS(s_abTests), "SHA3-384", RTDIGESTTYPE_SHA3_512, VINF_SUCCESS);
}


static void testSha3_512(void)
{
    RTTestISub("SHA3-512");

    /*
     * Generic API tests.
     */
    static TESTRTDIGEST const s_abTests[] =
    {
        { &g_abRandom72KB[0],         0, "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26", "SHA3-512 0 bytes" },
        { &g_abRandom72KB[0],         1, "4ee7f324651207f39d805a7316fa5fea29f4b220f0dab62c1b1543e9bf8bcca99f23717cc1a760ef1aa899b81457bbdcc03735dcb77826d888247349454d1db9", "SHA3-512 1 bytes" },
        { &g_abRandom72KB[0],         2, "a37abcdae939bea2fca0098ed2605ce1ee435323afc3b25ce0cd3591e91ed0d14f8b5b472d7b24c6660fd56af8a738848b472f26582fbda90a76c8050d6c2a12", "SHA3-512 2 bytes" },
        { &g_abRandom72KB[0],         3, "990d4cbd76529cbc6eb12e4da80ddafbd3400c4914aa6123aec0afea5620e1bee60b200125219911696abc77d62916c39755247a198084e3fb75252c093bb5c2", "SHA3-512 3 bytes" },
        { &g_abRandom72KB[0],         4, "a4406495c343b9c3b313cba6013294afd81fe23374149906074dfb4b596269e3735c98d7e2b1499a05f6596ea0a7a7e1a82cc85032e94bea71a0e6a775bcd18c", "SHA3-512 4 bytes" },
        { &g_abRandom72KB[0],         5, "f84d7503d3a0ca641b3e095c27afde5a273a1e6e838529d872215b0429a7bf27707b943780957441904395912fafe60b429448759f5dfb3e5ff32fb38dccb099", "SHA3-512 5 bytes" },
        { &g_abRandom72KB[0],         6, "525662195aa6f59c6df3f289f2806d5ae273a173bb1d0c3ea1f81073f75ae79b4771907641e6bf8672c0e84f1b978142fb416ca65c3bba0de351f2b8d848ff53", "SHA3-512 6 bytes" },
        { &g_abRandom72KB[0],         7, "821cef685d761af82b4306e1ae57f94de06c1e447b68eb0f928b50d48efedf36a6d98f0e6af966d94632bf135fccba4afcecce2fb7d96bd556877f8b35826fba", "SHA3-512 7 bytes" },
        { &g_abRandom72KB[0],         8, "7e01e3c096f03241c590f228863ee9c412cc209d0234452f28f5e52d238133fd91795b7a19ba0e6a6187383eb22749b5ea0fdefa327f824bcfe5ded0128abdcd", "SHA3-512 8 bytes" },
        { &g_abRandom72KB[0],         9, "1cb4b2dc7e3bc947c8b99a52e538491e7f05a86877895917db1a24dcfc68822997f2bf1cad9ab63d656b4ef7cacb32109bc2e4befd87755645cdf44e4b177405", "SHA3-512 9 bytes" },
        { &g_abRandom72KB[0],        10, "b5db597d408db3e664422e8723a3f03e42e6302ed6765fded67e35406e73f9edf3610f63287059f1850c57a6d460fda1eead440e67c65ff60f28dbc9fb886225", "SHA3-512 10 bytes" },
        { &g_abRandom72KB[0],        11, "b43ed646049b4d7067af01ea26c922f877cf1bcb83fd42ad7ee02ccd4f79c9bd7fb5c5c9e6538c67ba46f3ef7537f8b7c1465843191c17ced4d59d1a534091be", "SHA3-512 11 bytes" },
        { &g_abRandom72KB[0],        12, "844eb1bdc2bda7a7a142c6769ea7f7c482457e6e025789125b43be6d2a10639ce939c4ec4bb80cf5113e7e3f608b3c3587c262fdba05ffd9c56354e64085d052", "SHA3-512 12 bytes" },
        { &g_abRandom72KB[0],        13, "cfad914dc4738c54fede2b0bc0ba5f0d657d1a120f38c2d4dfa79528c210236a03cbcd277852413ea64d2e5e54659772e69d9b8af6c0df4aece7da71097c17bd", "SHA3-512 13 bytes" },
        { &g_abRandom72KB[0],        14, "d7fadc895847ca55425da98b51cc12c38dac301475a0517cd7ac7b40fa328809e0edc07a3314409df18759dc03f30bbbdd5d78ed6755a761401b19b4c3426a99", "SHA3-512 14 bytes" },
        { &g_abRandom72KB[0],        15, "46c539de6bfe7da9d6e145c8e73f9ad4a117b55a59fd0a913e3f56349f02f1af81736293e55f3439da7d8f7e6799fbbb0d76fd77d2b0acb0acf78ce08f775245", "SHA3-512 15 bytes" },
        { &g_abRandom72KB[0],        16, "81cf0210eb63ec45bbbdb3105ed8ae148452e4989f0910e6ea857ad6beb4f879429ef521d649153c435210aa4edcf9509941460c10a2b9c26173dfb862e8f4ef", "SHA3-512 16 bytes" },
        { &g_abRandom72KB[0],        17, "9cdd116f054627a52a1b323c42d168855520291d91a3dc7fe52ea3d4119d44e7a650b6a27e9041d37057e51a48c183ec0c653d21d5435c6d8cda6de300644b69", "SHA3-512 17 bytes" },
        { &g_abRandom72KB[0],        18, "dc4be84a998b02b19c95d06486fbf02128742a3499adb8b720c4b63483c64ba96b11ac8769138561991f90211b7c0698691a852f949b843db968e481cce9a12a", "SHA3-512 18 bytes" },
        { &g_abRandom72KB[0],        19, "8c156e9abe6337b23af7d82c15b7aef03118faa0b539f0eadef72ab0258f78e6708741d172c4185b420fa8a21b885c32ccdca1543eb9ec35d188b30254e0702e", "SHA3-512 19 bytes" },
        { &g_abRandom72KB[0],        20, "b038a2cea0052814f2c29cf4815211d3d8f006a388c9c166ff20c9b214ffa264f68fde4842bbc773254f8dd4df10e98287cee82c85fdd170ac7ed2999cda0095", "SHA3-512 20 bytes" },
        { &g_abRandom72KB[0],        21, "35ac6d2727b4af76144ac58cdb84f662d78855d76baf22411c75057ca31a86fc11e7c06248ef87661467d394428dfe75f5b88121b7e762920c3cf32a5ba0d6b1", "SHA3-512 21 bytes" },
        { &g_abRandom72KB[0],        22, "7add7d739c6f31fe25b30367853f90c0fd9d7416c20ca2778e527b3d4b876b5676ea1610db6df508e1b6230c67c6e9f5eeb60de65b2f7cea85064eb7ab21771d", "SHA3-512 22 bytes" },
        { &g_abRandom72KB[0],        23, "6dbf5a243571b4c3e03786a1978074d06fe78ba01ee73de50e165bcf461018f96c6eff1d69c44dadae3df2e1d406a207208c684cb04cd38e679bc883bf95e4d4", "SHA3-512 23 bytes" },
        { &g_abRandom72KB[0],        24, "c2d88cd3da3db0883c01e622b40f93060ba176710e1f9988e94748a77a8c5c64cd461b4ad5190ac5a0a66294713d097478b0ea0f49da2f737d9208f4f844a509", "SHA3-512 24 bytes" },
        { &g_abRandom72KB[0],        25, "3a7146eda5a22fdd53535da6706399c74ee9848cd2cd11944575270b994c7713c556d96b38c0b92b8592e8fb48fa79816cca982b56c673f7edc919d4f50b888a", "SHA3-512 25 bytes" },
        { &g_abRandom72KB[0],        26, "23a48b562fa85ad25d08daa2c14bf47f1dab9edb5fca761586b3418c1672aa9297e5681c24822ec29a2e42c54a019d949c2464a52c3456aa5e435d452510730b", "SHA3-512 26 bytes" },
        { &g_abRandom72KB[0],        27, "8aabb979e65d5c332cafb4ae5edb58437cbadeb13d8f7d985cc8dc0108060bca616add6c6be22eecf3059822a9f154471f690f3e1dfebce88c4c42533597ade9", "SHA3-512 27 bytes" },
        { &g_abRandom72KB[0],        28, "0e3bcaae03e672848cb3bbad4f21cae7043a0a0c51c6f6bb0870b9e33f7f8fde86200212d0e5f25a2c5eed1cfcafc76f477a395b712b55071a139ff7726d6815", "SHA3-512 28 bytes" },
        { &g_abRandom72KB[0],        29, "fb5a3094c42e4917388edd335c8c32eb8f1e2f5789329b89a9775ca84cd43f8f4187d8ab334bad952f21e426dca97a9f577a13d21cdbfb92cfb2a53d99c0c530", "SHA3-512 29 bytes" },
        { &g_abRandom72KB[0],        30, "0be32867a3a919e9a2ba2ce700f801e7531e35c38897ca99c915b2796d215817ccdc64efe4a8aeee354ec0c68c1dfb6cdf27922d5340e89d86dc096df3c68dc9", "SHA3-512 30 bytes" },
        { &g_abRandom72KB[0],        31, "33b0b014160167c2e1b893275f6c053156471cabfc20c13456302053aa8d96ba517db68322dabe41d3b990cffb6ed9ec7dc2a9365b92c94094fdc0e4376e4c80", "SHA3-512 31 bytes" },
        { &g_abRandom72KB[0],        32, "0b532c07abc6739767601ddb0798b22bd0c7687a14ccd79bfaaf94276bd9d158c589d99ce845b218fc044ba3aba3f402f21aeec67d1b71fdd962df481701d89e", "SHA3-512 32 bytes" },
        { &g_abRandom72KB[0],        33, "4e4babce8b0e0163388fcd1d3c81375949e9ff42c3ae97452b6c54875a8c5bbd684536991ee68fcc0422c3d892c3fd49ac1ec8938e416db3eac69f3c038709dd", "SHA3-512 33 bytes" },
        { &g_abRandom72KB[0],        34, "bc67eb2b32f4cafbb395cef79781069cca2f15384896f9bce8b82af43073c6446675878c1c8b7bb2a5fbadd5e8296d3f35c02b9135ed4313454ecad603d1d8c8", "SHA3-512 34 bytes" },
        { &g_abRandom72KB[0],        35, "2fb234bcebd0589cdf5cf30fa8d593d21e9734428f166d1c4dcd44c938f70ebbbe7f9d8f7089edfb3925c0d1c45c1b0db1244e2de97646282236f5e8cb517504", "SHA3-512 35 bytes" },
        { &g_abRandom72KB[0],        36, "0d5069bfed5f8c77cff581cbb340239624a55d34123d138e110fe783d648190da2c3a4b2cd5e72d98223670a4435bfd8005b9305d6526fda164a5a7dc115cf8b", "SHA3-512 36 bytes" },
        { &g_abRandom72KB[0],        37, "2c14f8534e40a03e8b4882c2751f818d2a4ff5862d0489c8111ce002f507ed215fa7efaa68293f5b50e8bf73c665bfb8a257a18f82b14809abc6cb03c4190547", "SHA3-512 37 bytes" },
        { &g_abRandom72KB[0],        38, "0351c1c8507264c7a63bf148e7fa7d475f147c148ac9926ad6fd00f92815fc0d0cff79b2a57246757af6de325b186115c0152f89605edc9ed3fb8e74b0bf3c5a", "SHA3-512 38 bytes" },
        { &g_abRandom72KB[0],        39, "2c25e88aad094ca1c65e8b4f4cafd03719a2af9e470e540f454297c245a7d1dc4aa6ddd8daad23ea682d520fc25368f8c58eefe2a977a0856b2fbe69bcfc6802", "SHA3-512 39 bytes" },
        { &g_abRandom72KB[0],        40, "94e315fd9972515fcb737a4aa3131ec6c329d628f5845401d12b34f0b47d767b57aa5e1e1321324d301a18f0a678b9619819548e87d5f5eb7043505ffaa9ea56", "SHA3-512 40 bytes" },
        { &g_abRandom72KB[0],        41, "d3d7fe9f5cab99b9b3d6dae377e6467eb8330ac54248e281a44c2c42877de5e2b63a2c3923d0d2760bbf27837e0646df797f9687cd4a9a7a99047be9681bec13", "SHA3-512 41 bytes" },
        { &g_abRandom72KB[0],        42, "0fb6d3488f202dfa56e37f20269d17e84556c9cb9f28d2c7c5e66ff64f4557d17ca5f67874a97c0f73fdb71698cfa576eeeffc8cee7b499ea1335257102dc5ac", "SHA3-512 42 bytes" },
        { &g_abRandom72KB[0],        43, "6dd66ccbb5c5e95f92d6e55b4bd159a96045d39a87c2c8e3db42abbf01cfe36e92b326aab17d42441a5717da4518884669d0f43d43453dba7a744167cda8f41e", "SHA3-512 43 bytes" },
        { &g_abRandom72KB[0],        44, "afdb2b1b74767e6b26b96d174a89fe5efb3205c3aaceb6e067631a4e5834a067d9d74248556e32661ee50fc7aaaf49482a33a6fba3a24e19a7977c14af092643", "SHA3-512 44 bytes" },
        { &g_abRandom72KB[0],        45, "d68f05c24ec746168af39ea279987643189c239f981173691f462296b682b320bccadc463fc4829668736ba007e47118cbf35b4d957d1758d5a1d3b0b5e3f6aa", "SHA3-512 45 bytes" },
        { &g_abRandom72KB[0],        46, "03651133ed68ee78daae564e84d6749ceb7ebfed83b1db48dd19e37447f3efa0e2e18d844a3ca017c47fbf027460f9a8f37d6014ffdf6aac48f030ba1abc23f3", "SHA3-512 46 bytes" },
        { &g_abRandom72KB[0],        47, "5629f7274311cd562e91ef70dbfdec9f09911ab9808407bb66cb4da61520dbc33f1a4aa1c4e8697e8c78b69d99eccfe1ed85e84420e1c7da492392959a5f3f2b", "SHA3-512 47 bytes" },
        { &g_abRandom72KB[0],        48, "12e3c5cef70c5a6dabc95eee03b892787872f6d32e0d9371d89fced570b4e40d97ee6eb143eb109425ed56f6ee213b86de1ab2a028999fac519c77c3f26b3276", "SHA3-512 48 bytes" },
        { &g_abRandom72KB[0],        49, "7e3941a41eadd11873df6e587275e9023626a3f5f21feac2691c7ffedf3be953bf8e72640d812ff3c286c0c59b1d635af071eceecf40a2501183dd32982e60da", "SHA3-512 49 bytes" },
        { &g_abRandom72KB[0],        50, "e22da00a967377bdef1a65449c30df64847af91aabe4cb9b847066bc2453f8b3feb20614440b1718253ff8121ae0e385dc29c07b096dbb6a9c128660cf2d201d", "SHA3-512 50 bytes" },
        { &g_abRandom72KB[0],        51, "915467a10f3e111ac7c7fde607aba3c9147be30d36516b43d4486344ba459eb20b9aca443b2d0173a0cf2230e9045589369fefab7d6d053048208508ea86ed5b", "SHA3-512 51 bytes" },
        { &g_abRandom72KB[0],        52, "e6f45a0d9346e02b21e4ddef0ec776c65dea326af0b763dffc31eac2449506d723b1425e9b675463c63d4cd8c28533b76d50fbda2a431f8de6de62c1faf98352", "SHA3-512 52 bytes" },
        { &g_abRandom72KB[0],        53, "bf1e3f13b1f26ce8fbb8dc38d4f061765f90845e2fc0527f0f010fd71925a0bef2e31e96b9b2c379a27089c5dad504da161741556c6a6570a0303e71146beb2e", "SHA3-512 53 bytes" },
        { &g_abRandom72KB[0],        54, "5c57863608bc36390c697b82f30ee2c8f2d6f65d024b46b7c74f21203cf720e1a58e1bc8c42e203db4458e323ac3208ee6892ef106f5e2ec04cf89c5a6d04c93", "SHA3-512 54 bytes" },
        { &g_abRandom72KB[0],        55, "65c1543ab3f5354882aa55050bd55f7e9d317e405352dc7ded30ae3dfd92f9b3191ccf0e616c963d4e5ebb45a5de5c880345d890fe3fa5f0695efb749e2b64f0", "SHA3-512 55 bytes" },
        { &g_abRandom72KB[0],        56, "19c31282083102e9ccb901d617d7f676c10e10d93e718732bd0de93a42417cc7b9d30c3c59ff5ae1b39fe3ceff46d4a38be6f8276435c3183a4e2d6f4d062a15", "SHA3-512 56 bytes" },
        { &g_abRandom72KB[0],        57, "9d1cfb089e112b391c20a649827f4e33be7ef24f902f7d7a9e51523df2f77ec80ef29364ac34f74199ff1ab96d276cf1d4f9d39e755221c14f993d1f84b3e5ad", "SHA3-512 57 bytes" },
        { &g_abRandom72KB[0],        58, "2905d9b5f8e24bbee15b576084842ed5d81570369a4fca15f1a588e093d84949edf374e2b25bd845482f93f8d03e92f52d8179c4ac356fbf12bc0591cb8a6217", "SHA3-512 58 bytes" },
        { &g_abRandom72KB[0],        59, "30bbd30b7b90c8c2ef65310a4d0ae0918c8e8f4c3b116305cf5960e5baa2c7d98e763d18993784e277ce6d51345046724626291c76b0293139074c31ad3f60bf", "SHA3-512 59 bytes" },
        { &g_abRandom72KB[0],        60, "50f508336ec07572733d1e725218916d1ecd578891d49ae514e0de3d6175b50c4f5556d8b93cda8743cf37acaf69ff278e2944913d05bd9a13ee959d2616279e", "SHA3-512 60 bytes" },
        { &g_abRandom72KB[0],        61, "e9d0c5b876151523c3cb2364d6c4788f37bfcfde0196c68b626418035929ff6c2a4c470d1d04b47d445b23144a529fc78f4cbc77aa4d04a0ee4b6aa2a699166f", "SHA3-512 61 bytes" },
        { &g_abRandom72KB[0],        62, "b7e3fc3d74d63179b8832383c202e35b2f701e5457729652f7eff37bedc5df0f94548765d0b16d95d0b37c3cac1ecfdba5ca8cbabf2a72bbcb2f3313782976b6", "SHA3-512 62 bytes" },
        { &g_abRandom72KB[0],        63, "0b47cb66418e1c6d41389c2191eadee2376db5db7808a6e2e3245ed7820652da63361c2e65c3801fefa81bcf580477a91b11d2377dfb0c3e5b5f08e49d46cc40", "SHA3-512 63 bytes" },
        { &g_abRandom72KB[0],        64, "cf6365be34524c4bff524832f2d909e442428c90294f71bc6b7b38f26d53e215bfef34b89530e9ab0089c589dde9938bc97d43f9d429aabfee3af95ae1b4cfb3", "SHA3-512 64 bytes" },
        { &g_abRandom72KB[0],        65, "3e4722b8bc5b8e4ed1e10896949cb29fbf6985eec29b4d9e6e4b0c9411996a3c34e9fe5a08bd7c962736d38bdb5aa65d432bc1e35d2f5a66709e9b0cb8cf786b", "SHA3-512 65 bytes" },
        { &g_abRandom72KB[0],        66, "73520db68703a4704ec6a521d5e5aeff2d5dc26963e8ba64db5d2c890ae5cb20a206e4c887659af80436ba592c7451b24c9739ff444831692a89fbbc5663c410", "SHA3-512 66 bytes" },
        { &g_abRandom72KB[0],        67, "83a6eb52acddffb2a4b01e65eb300811bacddc8d0b303cd103ddad85431cc7731b19dfa65404e0012a6776ed904831a273d2a530c181f66e6f1591c10d293916", "SHA3-512 67 bytes" },
        { &g_abRandom72KB[0],        68, "9931cccc3c7e7ef3713b5fdc3c03fadf9c95ca94d03ea36da20d060df7af0e1c1ce985d147547052b2410d582d21552d864a7869f7b25eecccdbd6c044a5112b", "SHA3-512 68 bytes" },
        { &g_abRandom72KB[0],        69, "492d08d8a38c0fa5524979765335585189c113a922481a468e782b2dad23d24415881a2998f46373f1c163cd1ca8a5db6856520e61b1dae443bb2fe95e54e189", "SHA3-512 69 bytes" },
        { &g_abRandom72KB[0],        70, "2b16f2c83901434ca4bb10565b8370e408a7766e7df26ad7c7b6c304bb299654e8e5cc3c4c8fa4a7039f94762dc24f1dfbec782d590780042f5afc6c807da93b", "SHA3-512 70 bytes" },
        { &g_abRandom72KB[0],        71, "923e4055b04dccc21ec30f1234fbd5ca4a917f2a38218119ebf14d664207b31ff706d1979f32e81fb0f9fdbaca8e7f10c255e107bc3f14bf353d9f09d1820ad3", "SHA3-512 71 bytes" },
        { &g_abRandom72KB[0],        72, "4721edf451ffb8c7f367b9c5aef47d32932ae03e497786adcc51fba635148d2e4a9a2ef779700eca36273ac33c7f66ed5ad59090601b4d98f27958b90b195074", "SHA3-512 72 bytes" },
        { &g_abRandom72KB[0],        73, "bb21eb1d4887ea6ed89f1cad785c88dab259567bc3988d85ad1a6cc0bd906d41862f7bc748176e691a79eefa2c679978bbcf7463e1dae772432985e01b713b31", "SHA3-512 73 bytes" },
        { &g_abRandom72KB[0],        74, "9dcdd22af755313131b64b72408bea54b4aca212d2ea7f62763c60f21860bf68bdad60fb80e31dea30a291ec74e8e98db27886b96dee09c0f3a2c555de283cc9", "SHA3-512 74 bytes" },
        { &g_abRandom72KB[0],        75, "73aff791c665b8af2606fb23418cce72cf6093179d7025fc29ea3592122dd9a27fdf85c1179241c28b9b945a5597ba17600342c8025e212b1ff95e571e3fdb63", "SHA3-512 75 bytes" },
        { &g_abRandom72KB[0],        76, "6b81bb06ecd0c10c7ecbb20c303941cc04c2ab56294b1dc22ad0bc0c9dd039ef31aebb10fe9bb13499991bd5742a4887fa220b2e74d91af40998649b24470821", "SHA3-512 76 bytes" },
        { &g_abRandom72KB[0],        77, "8cfacb727fd2ece3601ea7da001ab1c62dd6412c78a79b6960927d90a4daafefca7ceb1f025d05d7fef11ce36402c73c6d41a1fe917ccda47a119f8816a58d6b", "SHA3-512 77 bytes" },
        { &g_abRandom72KB[0],        78, "1f02f1c2eecb7cc1247e09efcc528a151170caa7b45e426dac0c7f7d57201b078c0b2bc461a2585575b0f704e2c048ddc3db19ddf48df7811e6bdccd0d3e5f8b", "SHA3-512 78 bytes" },
        { &g_abRandom72KB[0],        79, "e1a4f3f7d00c2a4b18ccf13ab5fb4ff073db381f6d31e8b82a3d476608e9e4685f0df71e4c59cc44557caae72f32591d5aa49829ad2c2a868c819f71240619a4", "SHA3-512 79 bytes" },
        { &g_abRandom72KB[0],        80, "bc865da84d88fddd29e45b9bbddaded9e9011fb2e5277b3be1bb09b70d90d66e7ea1c57360b8d9defc40538e960adb356308efde38d22ab5eb74db91325c7bb1", "SHA3-512 80 bytes" },
        { &g_abRandom72KB[0],        81, "a505111a146ad2713f6aa57a67a3e73c8f458ed5d943e1f081a9ecc484ab1da2f0157143bfe0e9a0a402e5514d165c76ef2879a772823c5f841805f09840116b", "SHA3-512 81 bytes" },
        { &g_abRandom72KB[0],        82, "409131dd1171d05d96e2013a80ef9d44ffe39b39bb4b630f73d43fa9e0da4b9719add8cacf14406e00e5a14bb181ca7e45ee0264d81e5977dc5a0ede25c5a6c1", "SHA3-512 82 bytes" },
        { &g_abRandom72KB[0],        83, "2930270075ecfdb960ba6696e7c158f56142fc969ffa1b77e89e36a1694b26aec687a12f865cc5c1ea75a0f318d8e7180715848d77d24959d0b428cd50b8db10", "SHA3-512 83 bytes" },
        { &g_abRandom72KB[0],        84, "6a271f6d548c3ea27cdeca450e665c7370617f5309f629c18d1756b26d5546b18dde3af83ef79c1aafa70b0574f4d3b32e6d5d8fae359b810f37a6ed41013990", "SHA3-512 84 bytes" },
        { &g_abRandom72KB[0],        85, "0484e9ddd706b1c13f595824f63ff24df87e61aeb029da69046118f269b247a682ec0106d774e1708c1c6f810bb10be8f6844b10f1f82e03ddcd8634aa72e693", "SHA3-512 85 bytes" },
        { &g_abRandom72KB[0],        86, "414e13451dcc5cd997d384e7fcef8a6b0cb95a5eb75748a97529c424104bf4810fb7618150cff96940e41e1480c47be67d331450d8393760ec07302a674f7fed", "SHA3-512 86 bytes" },
        { &g_abRandom72KB[0],        87, "a7653579f7740919644a229ff3e812be4d52afa49035800e42969d3fcf16f915c1cef0664ea59edbba8643aa84a96040bd2659992733c6c78877b5212164718b", "SHA3-512 87 bytes" },
        { &g_abRandom72KB[0],        88, "f13ed79fe8185f13f3eab7f47ef5f17d1368c86c558ec76f4653c10db4d4eacac6a1adab465d8f8170e37fb1038d34c4238330f2878087116b0011e812e5f13f", "SHA3-512 88 bytes" },
        { &g_abRandom72KB[0],        89, "477f9fd5978cc2b56793dfeedf2001a24c8813c052bc391bfa0991378bddea4d50953083309d5fef8b150a06f2869200858fb0b045d88e94cadb569b74eb4ade", "SHA3-512 89 bytes" },
        { &g_abRandom72KB[0],        90, "c41eff28b115c8121b76cfd9d8410d292e9d8a7e1cfc954334e32ebd1fa1166f0f425e21ff4d5898ef824ce6617ca0509e7515d23e64cc2f4e88e376cf25c8cd", "SHA3-512 90 bytes" },
        { &g_abRandom72KB[0],        91, "710cb43ac749bf771d9c86a4f10c2a137ada8e1ccf18df635149c769954c7dabcfbc37c650fb5e568169f34d5bd4790457d4f8550bb8f0bf2f54a74e1473cdcd", "SHA3-512 91 bytes" },
        { &g_abRandom72KB[0],        92, "32aab1bdeb8d90becf600790aca16f4e7255923ad78e95e1733d3b4b11f51eb7198dedd4368c15647e5748fed5e0d9ada2af098b0b3a7b22fb3075ee48e4dfab", "SHA3-512 92 bytes" },
        { &g_abRandom72KB[0],        93, "cf97dc9c3f51a0ffa92379d33e123e0a2e1849b0107d69ed346d5ff841d7bb39c289ff7f8941200e0a23bb636180eeb9b04942bbf344d877f5306e498fc6ec73", "SHA3-512 93 bytes" },
        { &g_abRandom72KB[0],        94, "3b3dfebb00a8731103b54ce0e04903ab020c857dadd08642e8757c44afaab4b7d94cab2ccb9787f2a874004677d22780cf41c12df1fa0fe1752bdf672261327f", "SHA3-512 94 bytes" },
        { &g_abRandom72KB[0],        95, "12c68fec6ee5748c31e16138a6a2fe6ca84ba55b52abb17b91b1ad40e735a5fe486fcc9033fcd5e306d0da7a1382cb375950de6f05956f7f98465c34f55ea921", "SHA3-512 95 bytes" },
        { &g_abRandom72KB[0],        96, "0729b797cb8bb28274bd83258ccd5fb70ae3b50fff1a02137f8620ed7ed97807094e9735415b9288847eb0652d76bc2225f8c933016ae3e7a6c59283b67427b4", "SHA3-512 96 bytes" },
        { &g_abRandom72KB[0],        97, "e553596d7464f147e7b728eb4cfd176fe68b6a01d1e5c9d436f3ba4f35fc5183dfc40af52cb22682b9e388bda9f5ccacbf3d28fb2751f1840bee01e84c336baf", "SHA3-512 97 bytes" },
        { &g_abRandom72KB[0],        98, "5ff10285538450aac83075504612f99a97ef7d3b3d271af80d089fcb6cf0ba65d3b8627659dce567434f011241dd956fdaa603d3a6a95b183bc21a9e541c5c0b", "SHA3-512 98 bytes" },
        { &g_abRandom72KB[0],        99, "8264ce198a9cf2c0df56ac0e66c4d28f43a538c77b54080a0d623226c360878a7bae213e1e05aad4933e1ec8f50646f3f1abfe2d26b97e879463ec20b1ec7398", "SHA3-512 99 bytes" },
        { &g_abRandom72KB[0],       100, "ec1b9a77eef08fa9f30bfda857a404cea4ad382bef4d6c4468c6561e057396697f1a8d8f1981f369186c0d5302d7ba94dd21bef988934f4cce624e3727880c1d", "SHA3-512 100 bytes" },
        { &g_abRandom72KB[0],       101, "52500498571ed5ff4d274c593017bf958676ce0bd9ad4dc512a79615fa753a9a74f753e29eb230b810b544bb27f6d978f10b10ece4ca20483f0648dc55ef5dec", "SHA3-512 101 bytes" },
        { &g_abRandom72KB[0],       102, "b7e7a4fc48e5ee13a5fb6cb3d6369f16e73499ed689e843db5ef0f4113f5934ec88bbc1d88f644fc940eabe563962ddd253458fe353dd94b84fc0b68f79351cf", "SHA3-512 102 bytes" },
        { &g_abRandom72KB[0],       103, "a36bdca11677f6d00c41480cedb84a61f50badfe139d8e3a614ab4a6dda92b814f4448cc8449fafc48b643eb650acef6a66ae4e1e8c9931e64120f2fca8df3fb", "SHA3-512 103 bytes" },
        { &g_abRandom72KB[0],       104, "a251f1502eea3fd43aae904ee1aff0b710221bb21b934b1e2c8a2cabe865dcc34a1a35e81fd123c62bf007486332e6b33a15b621e94264f056d2388f7f37bd5b", "SHA3-512 104 bytes" },
        { &g_abRandom72KB[0],       105, "ee284dac8cc77a76e2c5da4c51d50e536bd76ce532b2e879904a5dcdb53b8f19da80953728724af49a434d4f55ee22efca95fb64efa6d40b3ef7f2ab9e4e2f2b", "SHA3-512 105 bytes" },
        { &g_abRandom72KB[0],       106, "91e36f3c8dc599d36ba96815b9f7aff30657f9a30554d4b3badf7d664b3f0364d934eb633fa314e71eb9d453ccac4f66b9497fbc329b1ed6a48b416a10e84673", "SHA3-512 106 bytes" },
        { &g_abRandom72KB[0],       107, "2762a46b22f6259e864bb03324d313460e0daa74219787e0596fafc6fef8bb7f723d8375cf5a3097e6ac377c4b13588e978483c21240af30226b6cba043eca53", "SHA3-512 107 bytes" },
        { &g_abRandom72KB[0],       108, "5971c80376d85e61caf25336b9af83aa90e5ae9facee9a36a5a89a889aaf589bf1e03aeb16d6cbfb167d61981b27f40731803385077ec9f427e2e16146afaa11", "SHA3-512 108 bytes" },
        { &g_abRandom72KB[0],       109, "c40906c8ed4101ba4da184eecb09ac96b9196becd39922f859c5fe898aaed15c940c2a662fb5aea1efaa3ec8301a4baa3b44fa37281a06f5480836efc26f0ba8", "SHA3-512 109 bytes" },
        { &g_abRandom72KB[0],       110, "29536834295248ddbbe1c366cb4e391eb0574ebf36fb0e3cbafd285de6d2568944b89a8108b644b48b60bd6e5e766d6472767654c3c36e373341fd5efe8baddb", "SHA3-512 110 bytes" },
        { &g_abRandom72KB[0],       111, "7a6f98d03200f77661686b6d69d5bb48a37c618dff07c0e25fda73a1fb08c1f04312c33971878de46acbb338e4a20dfd09acf1b1133206af57732123f36c3f0a", "SHA3-512 111 bytes" },
        { &g_abRandom72KB[0],       112, "6d46c8fe0f7645029f9a4af5d8a7a0abb2fddfbb1afc5235966df4d26cc4301d26cbf09bef044f264cf80a67a52861e9ddfe88b4a7f3224812750ffacf927449", "SHA3-512 112 bytes" },
        { &g_abRandom72KB[0],       113, "654c3d3b664da1a419443566e8e58be49c1c8d55137a32bdc1e67ca93ce0dca5058c46e4f1c7be260790ee2c58cd9710e1ff0a3b3ff0faccbf66f7e3279830e9", "SHA3-512 113 bytes" },
        { &g_abRandom72KB[0],       114, "232d3a6bb57a2dbf2b287698e7446876b68be7934afb7701553f2eec05b999aaf895d993d6643b1b90ac646698f1fa8b4a8a2c9aca209feee4bcf3ea865b94fe", "SHA3-512 114 bytes" },
        { &g_abRandom72KB[0],       115, "2d8ef7d8cfbf11c02064a1c34df41eb7ae652f076d9608639ad4aaa6949c86819a3752e509ec55d2291561ad5bcbf149b01bf497dd5bf0dbcdfc237536e8f567", "SHA3-512 115 bytes" },
        { &g_abRandom72KB[0],       116, "b1f8dc3aa2f6222c8cef637be1e3c27c6f718cc7747776f5adea6d9e73e8c0f522a7bacefa0dc40c75be982de3ac525eaf5d23d5fc1db5e61d33646211fab42f", "SHA3-512 116 bytes" },
        { &g_abRandom72KB[0],       117, "9d942fb3a3b4b16b42125e5680384541d840729796eb24b2132eea8e4614fe1483191ef33543fe5438d66c2ebd3fbf3baee2886e3665e2e7bbdb3d744fb98a45", "SHA3-512 117 bytes" },
        { &g_abRandom72KB[0],       118, "c9768aa0cc49ec005b17e3977055eb78e979404b5138209adc45a7c293bfd52dfbf442320b2f955fd90b360741be7e1c94f5ed844ac09caf1fa089bc6182ba47", "SHA3-512 118 bytes" },
        { &g_abRandom72KB[0],       119, "29dfb602406ff7c35d6980a31c9c9e4cedb070cdd8fd19427b10daeb7509dce0f52123ddbb4a57f970ed346f338c59af917b8f90d32b3927bf4a2a10efabc647", "SHA3-512 119 bytes" },
        { &g_abRandom72KB[0],       120, "b299a46e17c12dd35d49374c11f91e47d1f8b804c7aaeedf1fb2b84f77fc6247f7203bca813f812a6d062e505db43d03cdfc633fa23b6101dee42856c0485b8c", "SHA3-512 120 bytes" },
        { &g_abRandom72KB[0],       121, "3459aad57f27f2689728b828a8e4eccc991af2e87b2db7b0743d432ecb24c13ddcb2d6bedd09a1c0ae1b178222c0695802e0c14461a123e49fb8ce79d1e2a601", "SHA3-512 121 bytes" },
        { &g_abRandom72KB[0],       122, "2acd3bc4a351fa2c640bfcb73c4ee4c4f078e4621f5c424bfbbb3427b2396a5432ed2adca24bd21c0b9aa70d1d4ce847afe1f40e37c9bb75668994705dcd29ef", "SHA3-512 122 bytes" },
        { &g_abRandom72KB[0],       123, "633166065ebba100f264090981783e223bed71325ca87a609de02b8e23226fee55a6a389a1978df01eaa5cd497ffb3c6d72f6e60033f217fd0cfd6c6ceba468c", "SHA3-512 123 bytes" },
        { &g_abRandom72KB[0],       124, "976c3f3a52a2a13b1437aaad29230f6a64b66caf3fb450c8dea915bb5d3cc9fafc5ba1297cfef6dca1211fa77a737026adc1426cfe84f6bce8cfc071859208a3", "SHA3-512 124 bytes" },
        { &g_abRandom72KB[0],       125, "cc68da46995cc0becdf21d1a6777efef4734ca81f5f079b5de2a394b18c0068e36af49e12ea6e916e954520b72edb15beff51b6b2cad64270099d020758ab05a", "SHA3-512 125 bytes" },
        { &g_abRandom72KB[0],       126, "1c504c2b97f8b1351b32b70836d52d6991188b48b1e714cda5b44ddb51f16d12d644a9703078dff59164acb2b1d5e8d9fe74057e1e54a38e23933c37bb873f63", "SHA3-512 126 bytes" },
        { &g_abRandom72KB[0],       127, "73534fffcd2647f7657cbbfd7b1487e260d79aa6d54cfde74c9cfb042618d20405fb43a2adb2acabf707ee676fd9b2f94d6173fab57b3b4e99e12f34a3396564", "SHA3-512 127 bytes" },
        { &g_abRandom72KB[0],       128, "7fba8d3e8e2bba64fce80aef2010fec041129e2ad559ccc5064544e1bf95a6c51d9dad5c27d4deabc2e103672fdfbe2df25ebffc3b4b4c0c94c7e9320237d97f", "SHA3-512 128 bytes" },
        { &g_abRandom72KB[0],       129, "2f65d51640daf193d089e61afd4fe5094765b75ca6c6d212fd5b18001154b421cb8dab0932aef69b85407fa1efdb0d0dcc638e7e9c2268ac8a5e453984544261", "SHA3-512 129 bytes" },
        { &g_abRandom72KB[0],      1024, "951e0f318804de414f479be226b733e65ca3a686a5c53afbfb7894fa6278bea1d92f9fa5273f542fd892cdae5dae115c5ac62898da49a7f12433cd0462b6df54", "SHA3-512 1024 bytes" },
        { &g_abRandom72KB[0],     73001, "f1275b4fcf0650888f60fee8b1557e5406eafe166d6a7771a4f3b865a5e051edfdc534de29e0773c9cd418c40cf6c9bff18c06a68f0f02d3a2e7caf3c3353f4d", "SHA3-512 73001 bytes" },
        { &g_abRandom72KB[0],     73728, "031700e2323b8d5b7d87eb2e00714ecd0aa9408de3b4ea3328040292f23ff0aa4e29b0b9c3125516e80e522ef228551e2481bd8c0560c12be148306f5379595e", "SHA3-512 73728 bytes" },
        { &g_abRandom72KB[0x20c9],  9991, "c570fa09ebc4157ae9836d09a3c6b41bd1ef028f76c2a72e1a10b6b09deb67bc484c436b51c1338a245ef244be13cd6efc034a66f70c685c49f400830806ebc8", "SHA3-512 8393 bytes @9991" },
    };
    testGeneric("2.16.840.1.101.3.4.2.10", s_abTests, RT_ELEMENTS(s_abTests), "SHA3-512", RTDIGESTTYPE_SHA3_512, VINF_SUCCESS);
}


static unsigned checkArgs(int cArgs, char **papszArgs, const char *pszName, const char *pszFamily)
{
    if (cArgs <= 1)
        return 1;
    size_t const cchName   = strlen(pszName);
    size_t const cchFamily = strlen(pszFamily);
    for (int i = 1; i < cArgs; i++)
    {
        const char  *pszArg = papszArgs[i];
        const char  *pszSep = strpbrk(pszArg, ":=");
        size_t const cchCur = pszSep ? (size_t)(pszSep - pszArg) : strlen(pszArg);
        if (   (cchCur == cchName   && RTStrNICmp(pszArg, pszName, cchCur) == 0)
            || (cchCur == cchFamily && RTStrNICmp(pszArg, pszFamily, cchCur) == 0) )
        {
            if (!pszSep || pszSep[1] == '\0')
                return 1;
            return RTStrToUInt32(pszSep + 1);
        }
    }
    return 0;
}


int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitExAndCreate(argc, &argv, 0, "tstRTDigest-2", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

#define DO(a_szName, a_szFamily, a_fnTestExpr) do { \
            unsigned const cTimes = checkArgs(argc, argv, a_szName, a_szFamily); \
            for (unsigned i = 0; i < cTimes; i++) { a_fnTestExpr; } \
        } while (0)
    DO("MD2",    "MD",   testMd2());
    DO("MD4",    "MD",   testMd4());
    DO("MD5",    "MD",   testMd5());
    DO("SHA1",   "SHA",  testSha1());
    DO("SHA256", "SHA2", testSha256());
    DO("SHA224", "SHA2", testSha224());
    DO("SHA512", "SHA2", testSha512());
    DO("SHA384", "SHA2", testSha384());
#ifndef IPRT_WITHOUT_SHA512T224
    DO("SHA512T224", "SHA2", testSha512t224());
#endif
#ifndef IPRT_WITHOUT_SHA512T256
    DO("SHA512T256", "SHA2", testSha512t256());
#endif
    DO("SHA3-224", "SHA3", testSha3_224());
    DO("SHA3-256", "SHA3", testSha3_256());
    DO("SHA3-384", "SHA3", testSha3_384());
    DO("SHA3-512", "SHA3", testSha3_512());

    return RTTestSummaryAndDestroy(hTest);
}

