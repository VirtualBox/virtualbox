/* $Id: tstRTHttp-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Testcase for the RTHttp API.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/http.h>

#include <iprt/test.h>
#include <iprt/message.h>
#include <iprt/string.h>
#include <iprt/crypto/key.h>


/* Test message:
 * POST /foo?param=value&pet=dog HTTP/1.1
 * Host: example.com
 * Date: Sun, 05 Jan 2014 21:31:40 GMT
 * Content-Type: application/json
 * Digest: SHA-256=X48E9qOokqqrvdts8nOJRJN3OWDUoyWxBf7kbu9DBPE=
 * Content-Length: 18
 *
 * {"hello": "world"}
 */

static void testHeaderSigning()
{
    static const char s_szPublicKey1[] =
        "-----BEGIN PUBLIC KEY-----\n"
        "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDCFENGw33yGihy92pDjZQhl0C3\n"
        "6rPJj+CvfSC8+q28hxA161QFNUd13wuCTUcq0Qd2qsBe/2hFyc2DCJJg0h1L78+6\n"
        "Z4UMR7EOcpfdUE9Hf3m/hs+FUR45uBJeDK1HSFHD8bHKD6kv8FPGfJTotc+2xjJw\n"
        "oYi+1hqp1fIekaxsyQIDAQAB\n"
        "-----END PUBLIC KEY-----\n";
    static const char s_szPrivateKey1[] =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MIICXgIBAAKBgQDCFENGw33yGihy92pDjZQhl0C36rPJj+CvfSC8+q28hxA161QF\n"
        "NUd13wuCTUcq0Qd2qsBe/2hFyc2DCJJg0h1L78+6Z4UMR7EOcpfdUE9Hf3m/hs+F\n"
        "UR45uBJeDK1HSFHD8bHKD6kv8FPGfJTotc+2xjJwoYi+1hqp1fIekaxsyQIDAQAB\n"
        "AoGBAJR8ZkCUvx5kzv+utdl7T5MnordT1TvoXXJGXK7ZZ+UuvMNUCdN2QPc4sBiA\n"
        "QWvLw1cSKt5DsKZ8UETpYPy8pPYnnDEz2dDYiaew9+xEpubyeW2oH4Zx71wqBtOK\n"
        "kqwrXa/pzdpiucRRjk6vE6YY7EBBs/g7uanVpGibOVAEsqH1AkEA7DkjVH28WDUg\n"
        "f1nqvfn2Kj6CT7nIcE3jGJsZZ7zlZmBmHFDONMLUrXR/Zm3pR5m0tCmBqa5RK95u\n"
        "412jt1dPIwJBANJT3v8pnkth48bQo/fKel6uEYyboRtA5/uHuHkZ6FQF7OUkGogc\n"
        "mSJluOdc5t6hI1VsLn0QZEjQZMEOWr+wKSMCQQCC4kXJEsHAve77oP6HtG/IiEn7\n"
        "kpyUXRNvFsDE0czpJJBvL/aRFUJxuRK91jhjC68sA7NsKMGg5OXb5I5Jj36xAkEA\n"
        "gIT7aFOYBFwGgQAQkWNKLvySgKbAZRTeLBacpHMuQdl1DfdntvAyqpAZ0lY0RKmW\n"
        "G6aFKaqQfOXKCyWoUiVknQJAXrlgySFci/2ueKlIE1QqIiLSZ8V8OlpFLRnb1pzI\n"
        "7U1yQXnTAEFYM560yJlzUpOb1V4cScGd365tiSMvxLOvTA==\n"
        "-----END RSA PRIVATE KEY-----\n";
    static const char s_szKeyId1[] = "Test";
    static const char s_szUrl1[]   = "https://example.com/foo?param=value&pet=dog";
    static const char s_szHost1[]  = "example.com";
    static const char s_szDate1[]  = "Sun, 05 Jan 2014 21:31:40 GMT";

    RTTestISub("RTHttpSignHeaders");


    /*
     * Load the key pair used in the reference examples.
     */
    RTCRKEY hPublicKey;
    RTTESTI_CHECK_RC_RETV(RTCrKeyCreateFromBuffer(&hPublicKey, 0, RT_STR_TUPLE(s_szPublicKey1), NULL /*pszPassword*/,
                                                  NULL /*pErrInfo*/, NULL /*pszErrorTag*/), VINF_SUCCESS);
    RTCRKEY hPrivateKey;
    RTTESTI_CHECK_RC_RETV(RTCrKeyCreateFromBuffer(&hPrivateKey, 0, RT_STR_TUPLE(s_szPrivateKey1), NULL /*pszPassword*/,
                                                  NULL /*pErrInfo*/, NULL /*pszErrorTag*/), VINF_SUCCESS);

    /*
     * C.2 Basic Test - tweaked a little with 'version="1"'.
     */
    RTHTTP hHttp;
    RTTESTI_CHECK_RC_RETV(RTHttpCreate(&hHttp), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTHttpAddHeader(hHttp, "Host", RT_STR_TUPLE(s_szHost1), RTHTTPADDHDR_F_BACK), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTHttpAddHeader(hHttp, "Date", RT_STR_TUPLE(s_szDate1), RTHTTPADDHDR_F_BACK), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTHttpSignHeaders(hHttp, RTHTTPMETHOD_POST, s_szUrl1, hPrivateKey, s_szKeyId1, 0), VINF_SUCCESS);
    const char *pszAuth = RTHttpGetHeader(hHttp, RT_STR_TUPLE("Authorization"));
    RTTESTI_CHECK_RETV(pszAuth);
    //const char *pszExpect = "Signature keyId=\"Test\",algorithm=\"rsa-sha256\",headers=\"(request-target) host date\",signature=\"qdx+H7PHHDZgy4y/Ahn9Tny9V3GP6YgBPyUXMmoxWtLbHpUnXS2mg2+SbrQDMCJypxBLSPQR2aAjn7ndmw2iicw3HMbe8VfEdKFYRqzic+efkb3nndiv/x1xSHDJWeSWkx3ButlYSuBskLu6kd9Fswtemr3lgdDEmn04swr2Os0=\"";
    const char *pszExpect = "Signature version=\"1\",keyId=\"Test\",algorithm=\"rsa-sha256\","
                            "headers=\"(request-target) host date\","
                            "signature=\"qdx+H7PHHDZgy4y/Ahn9Tny9V3GP6YgBPyUXMmoxWtLbHpUnXS2mg2+SbrQDMCJypxBLSPQR2aAjn7ndmw2iicw3HMbe8VfEdKFYRqzic+efkb3nndiv/x1xSHDJWeSWkx3ButlYSuBskLu6kd9Fswtemr3lgdDEmn04swr2Os0=\"";
    if (strcmp(pszAuth, pszExpect) != 0)
    {
        RTTestIFailed("Test C.2 failed");
        RTTestIFailureDetails("Got auth: %s\n", pszAuth);
        RTTestIFailureDetails("Expected: %s\n", pszExpect);
    }
    RTTESTI_CHECK_RC(RTHttpDestroy(hHttp), VINF_SUCCESS);

    /*
     * C.3 All Headers Test - tweaked a little with 'version="1"'.
     *
     * Note! Draft #10 has an incorrect signed digest.  The decrypting digest
     *       does not match the documented plaintext.
     *       Decrypted sha-256:  407954c106c7e9aa1644fc4764cbfb481cc178dec9142bf62e3cac97251e1953
     *       Plain text sha-256: 53cd4050ff72e3a6383091186168f3df4ca2e6b3a77cbed60a02ba00c9cd8078
     */
    hHttp = NIL_RTHTTP;
    RTTESTI_CHECK_RC_RETV(RTHttpCreate(&hHttp), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTHttpAddHeader(hHttp, "Host", RT_STR_TUPLE(s_szHost1), RTHTTPADDHDR_F_BACK), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTHttpAddHeader(hHttp, "Date", RT_STR_TUPLE(s_szDate1), RTHTTPADDHDR_F_BACK), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTHttpAddHeader(hHttp, "Content-Type", RT_STR_TUPLE("application/json"), RTHTTPADDHDR_F_BACK), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTHttpAddHeader(hHttp, "Digest", RT_STR_TUPLE("SHA-256=X48E9qOokqqrvdts8nOJRJN3OWDUoyWxBf7kbu9DBPE="), RTHTTPADDHDR_F_BACK), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTHttpAddHeader(hHttp, "Content-Length", RT_STR_TUPLE("18"), RTHTTPADDHDR_F_BACK), VINF_SUCCESS);

    RTTESTI_CHECK_RC_RETV(RTHttpSignHeaders(hHttp, RTHTTPMETHOD_POST, s_szUrl1, hPrivateKey, s_szKeyId1, 0), VINF_SUCCESS);
    pszAuth = RTHttpGetHeader(hHttp, RT_STR_TUPLE("Authorization"));
    RTTESTI_CHECK_RETV(pszAuth);
    //pszExpect = "Signature keyId=\"Test\",algorithm=\"rsa-sha256\",headers=\"(request-target) host date content-type digest content-length\",signature=\"jgSqYK0yKclIHfF9zdApVEbDp5eqj8C4i4X76pE+XHoxugXv7qnVrGR+30bmBgtpR39I4utq17s9ghz/2QFVxlnToYAvbSVZJ9ulLd1HQBugO0jOyn9sXOtcN7uNHBjqNCqUsnt0sw/cJA6B6nJZpyNqNyAXKdxZZItOuhIs78w=\"";
    pszExpect = "Signature version=\"1\",keyId=\"Test\",algorithm=\"rsa-sha256\","
                "headers=\"(request-target) host date content-type digest content-length\","
                // bad rfc draft #10? "signature=\"jgSqYK0yKclIHfF9zdApVEbDp5eqj8C4i4X76pE+XHoxugXv7qnVrGR+30bmBgtpR39I4utq17s9ghz/2QFVxlnToYAvbSVZJ9ulLd1HQBugO0jOyn9sXOtcN7uNHBjqNCqUsnt0sw/cJA6B6nJZpyNqNyAXKdxZZItOuhIs78w=\"";
                "signature=\"vSdrb+dS3EceC9bcwHSo4MlyKS59iFIrhgYkz8+oVLEEzmYZZvRs8rgOp+63LEM3v+MFHB32NfpB2bEKBIvB1q52LaEUHFv120V01IL+TAD48XaERZFukWgHoBTLMhYS2Gb51gWxpeIq8knRmPnYePbF5MOkR0Zkly4zKH7s1dE=\"";
    if (strcmp(pszAuth, pszExpect) != 0)
    {
        RTTestIFailed("Test C.3 failed");
        RTTestIFailureDetails("Got auth: %s\n", pszAuth);
        RTTestIFailureDetails("Expected: %s\n", pszExpect);
    }

    RTTESTI_CHECK_RC(RTHttpDestroy(hHttp), VINF_SUCCESS);
    RTCrKeyRelease(hPublicKey);
    RTCrKeyRelease(hPrivateKey);
}


int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTHttp-1", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    testHeaderSigning();

    return RTTestSummaryAndDestroy(hTest);
}

