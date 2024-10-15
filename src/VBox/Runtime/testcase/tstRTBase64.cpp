/* $Id: tstRTBase64.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Base64.
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
#include <iprt/base64.h>

#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/stdarg.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# define MY_NL "\r\n"
#else
# define MY_NL "\n"
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/


static void tstBase64(const void *pvData, size_t cbData,
                      const char *pszEnc, size_t cchEnc,
                      int fTextData, int fNormalEnc)
{
    union
    {
        char    szOut[0x10000];
        RTUTF16 wszOut[0x10000];
    };
    size_t  cchOut = 0;

    /*
     * Test decoding.
     */
    int rc = RTBase64Decode(pszEnc, szOut, cbData, &cchOut, NULL);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTBase64Decode -> %Rrc", rc);
    else if (cchOut != cbData)
        RTTestIFailed("RTBase64Decode returned %zu bytes, expected %zu.",
                      cchOut, cbData);
    else if (memcmp(szOut, pvData, cchOut))
    {
        if (fTextData)
            RTTestIFailed("RTBase64Decode returned:\n%.*s\nexpected:\n%s\n",
                          (int)cchOut, szOut, pvData);
        else
            RTTestIFailed("RTBase64Decode return mismatching output\n");
    }

    cchOut = RTBase64DecodedSize(pszEnc, NULL);
    if (cchOut != cbData)
        RTTestIFailed("RTBase64DecodedSize returned %zu bytes, expected %zu.\n",
                      cchOut, cbData);

    /*
     * Test encoding.
     */
    rc = RTBase64Encode(pvData, cbData, szOut, cchEnc + 1, &cchOut);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTBase64Encode -> %Rrc\n", rc);
    else if (fNormalEnc && cchOut != cchEnc)
        RTTestIFailed("RTBase64Encode returned %zu bytes, expected %zu.\n",
                      cchOut, cchEnc);
    else if (fNormalEnc && memcmp(szOut, pszEnc, cchOut + 1))
        RTTestIFailed("RTBase64Encode returned:\n%.*s\nexpected:\n%s\n",
                      sizeof(szOut), szOut, pszEnc);

    size_t cchOut2 = RTBase64EncodedLength(cbData);
    if (cchOut != cchOut2)
        RTTestIFailed("RTBase64EncodedLength returned %zu bytes, expected %zu.\n",
                      cchOut2, cchOut);

    /** @todo negative testing. */

    /*
     * Same as above, but using the UTF-16 variant of the code.
     */

    /* Encoding UTF-16: */
    memset(wszOut, 0xaa, sizeof(wszOut));
    wszOut[sizeof(wszOut) / sizeof(wszOut[0]) - 1] = '\0';
    size_t cwcOut = 0;
    rc = RTBase64EncodeUtf16(pvData, cbData, wszOut, cchEnc + 1, &cwcOut);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTBase64EncodeUtf16 -> %Rrc\n", rc);
    else if (fNormalEnc && cwcOut != cchEnc)
        RTTestIFailed("RTBase64EncodeUtf16 returned %zu bytes, expected %zu.\n", cwcOut, cchEnc);
    else if (fNormalEnc && RTUtf16CmpUtf8(wszOut, pszEnc))
        RTTestIFailed("RTBase64EncodeUtf16 returned:\n%s\nexpected:\n%s\n", wszOut, pszEnc);

    size_t cwcOut2 = RTBase64EncodedUtf16Length(cbData);
    if (cwcOut != cwcOut2)
        RTTestIFailed("RTBase64EncodedLength returned %zu RTUTF16 units, expected %zu.\n", cwcOut2, cwcOut);

    /* Decoding UTF-16: */
    PRTUTF16 pwszEnc = NULL;
    RTTESTI_CHECK_RC_OK_RETV(RTStrToUtf16(pszEnc, &pwszEnc));

    rc = RTBase64DecodeUtf16(pwszEnc, szOut, cbData, &cchOut, NULL);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTBase64DecodeUtf16 -> %Rrc", rc);
    else if (cchOut != cbData)
        RTTestIFailed("RTBase64DecodeUtf16 returned %zu bytes, expected %zu.", cchOut, cbData);
    else if (memcmp(szOut, pvData, cchOut))
    {
        if (fTextData)
            RTTestIFailed("RTBase64Decode returned:\n%.*s\nexpected:\n%s\n", (int)cchOut, szOut, pvData);
        else
            RTTestIFailed("RTBase64Decode return mismatching output\n");
    }

    cchOut = RTBase64DecodedUtf16Size(pwszEnc, NULL);
    if (cchOut != cbData)
        RTTestIFailed("RTBase64DecodedUtf16Size returned %zu bytes, expected %zu.\n", cchOut, cbData);

    RTUtf16Free(pwszEnc);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTBase64", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Series of simple tests.
     */
    static const struct
    {
        const char *pszText;
        size_t      cchText;
        const char *pszEnc;
        size_t      cchEnc;
    } g_aTests[] =
    {
#define TEST_ENTRY(szText, szEnc) { szText, sizeof(szText) - 1, szEnc, sizeof(szEnc) - 1 }
        TEST_ENTRY("Hey", "SGV5"),
        TEST_ENTRY("Base64", "QmFzZTY0"),
        TEST_ENTRY("Call me Ishmael.", "Q2FsbCBtZSBJc2htYWVsLg=="),
        TEST_ENTRY(
        "Man is distinguished, not only by his reason, but by this singular passion "
        "from other animals, which is a lust of the mind, that by a perseverance of "
        "delight in the continued and indefatigable generation of knowledge, exceeds "
        "the short vehemence of any carnal pleasure." /* Thomas Hobbes's Leviathan */,
        "TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1" MY_NL
        "dCBieSB0aGlzIHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3" MY_NL
        "aGljaCBpcyBhIGx1c3Qgb2YgdGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFu" MY_NL
        "Y2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGludWVkIGFuZCBpbmRlZmF0aWdhYmxl" MY_NL
        "IGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRoZSBzaG9ydCB2ZWhl" MY_NL
        "bWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4="
        )
#undef TEST_ENTRY
    };

    for (unsigned i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        RTTestSubF(hTest, "Test 1-%u", i);
        tstBase64(g_aTests[i].pszText, g_aTests[i].cchText,
                  g_aTests[i].pszEnc, g_aTests[i].cchEnc,
                  1 /* fTextData */, 1 /* fNormalEnc */);
    }

    /*
     * Try with some more junk in the encoding and different line length.
     */
    RTTestSub(hTest, "Test 2");
    static const char s_szText2[] =
        "Man is distinguished, not only by his reason, but by this singular passion "
        "from other animals, which is a lust of the mind, that by a perseverance of "
        "delight in the continued and indefatigable generation of knowledge, exceeds "
        "the short vehemence of any carnal pleasure."; /* Thomas Hobbes's Leviathan */

    static const char s_szEnc2[] =
        "  TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB0aGlz\r\n"
        "  IHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIGx1c3Qgb2Yg\n\r\t\t\t\v"
          "dGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGlu\n"
        "\tdWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRo\n\r"
        "  ZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4=\n \n   \r   \n \t";

    tstBase64(s_szText2, sizeof(s_szText2) - 1,
              s_szEnc2,  sizeof(s_szEnc2) - 1,
              1 /* fTextData */, 0 /* fNormalEnc */);

    /*
     * Test for buffer overruns.
     */
    RTTestSubF(hTest, "Test 3");
    static uint8_t s_abData4[32768];
    for (size_t i = 0; i < sizeof(s_abData4); i++)
        s_abData4[i] = i % 256;
    for (size_t cbSrc = 1; cbSrc <= sizeof(s_abData4); cbSrc++)
    {
        union
        {
            char    szEnc[49152];
            RTUTF16 wszEnc[49152];
        };
        memset(szEnc, '\0', sizeof(szEnc));
        size_t cchEnc = RTBase64EncodedLength(cbSrc);
        if (cchEnc >= sizeof(szEnc))
            RTTestIFailed("RTBase64EncodedLength(%zu) returned %zu bytes - too big\n", cbSrc, cchEnc);
        size_t cchOut = 0;
        rc = RTBase64Encode(s_abData4, cbSrc, szEnc, cchEnc, &cchOut);
        if (rc != VERR_BUFFER_OVERFLOW)
            RTTestIFailed("RTBase64Encode(,%zu,) has no buffer overflow with too small buffer -> %Rrc\n", cbSrc, rc);
        rc = RTBase64Encode(s_abData4, cbSrc, szEnc, cchEnc + 1, &cchOut);
        if (RT_FAILURE(rc))
            RTTestIFailed("RTBase64Encode -> %Rrc\n", rc);
        if (cchOut != cchEnc)
            RTTestIFailed("RTBase64EncodedLength(%zu) returned %zu bytes, expected %zu.\n",
                          cbSrc, cchEnc, cchOut);
        if (szEnc[cchOut] != '\0')
            RTTestIFailed("RTBase64Encode(,%zu,) returned string which is not zero terminated\n", cbSrc);
        if (strlen(szEnc) != cchOut)
            RTTestIFailed("RTBase64Encode(,%zu,) returned incorrect string, length %lu\n", cbSrc, cchOut);

        /* Ditto for UTF-16: */
        memset(wszEnc, '\0', sizeof(wszEnc));
        size_t cwcEnc = RTBase64EncodedUtf16Length(cbSrc);
        if (cwcEnc >= RT_ELEMENTS(wszEnc))
            RTTestIFailed("RTBase64EncodedUtf16Length(%zu) returned %zu RTUTF16 units - too big\n", cbSrc, cwcEnc);
        size_t cwcOut = 0;
        rc = RTBase64EncodeUtf16(s_abData4, cbSrc, wszEnc, cwcEnc, &cwcOut);
        if (rc != VERR_BUFFER_OVERFLOW)
            RTTestIFailed("RTBase64EncodeUtf16(,%zu,) has no buffer overflow with too small buffer -> %Rrc\n", cbSrc, rc);
        cwcOut = ~(size_t)0;
        rc = RTBase64EncodeUtf16(s_abData4, cbSrc, wszEnc, cwcEnc + 1, &cwcOut);
        if (RT_FAILURE(rc))
            RTTestIFailed("RTBase64EncodeUtf16 -> %Rrc\n", rc);
        if (cchOut != cchEnc)
            RTTestIFailed("RTBase64EncodedUtf16Length(%zu) returned %zu bytes, expected %zu.\n",
                          cbSrc, cwcEnc, cwcOut);
        if (wszEnc[cwcOut] != '\0')
            RTTestIFailed("RTBase64EncodeUtf16(,%zu,) returned string which is not zero terminated\n", cbSrc);
        if (RTUtf16Len(wszEnc) != cwcOut)
            RTTestIFailed("RTBase64EncodeUtf16(,%zu,) returned incorrect string, length %lu\n", cbSrc, cwcOut);
    }

    /*
     * Finally, a more extensive test.
     */
    RTTestSub(hTest, "Test 4");
    static uint8_t s_abData3[12*256];
    for (unsigned i = 0; i < 256; i++)
    {
        unsigned j = i*12;
        s_abData3[j + 0] = i;      /* */
        s_abData3[j + 1] = 0xff;
        s_abData3[j + 2] = i;
        s_abData3[j + 3] = 0xff;   /* */
        s_abData3[j + 4] = i;
        s_abData3[j + 5] = 0xff;
        s_abData3[j + 6] = i;      /* */
        s_abData3[j + 7] = 0x00;
        s_abData3[j + 8] = i;
        s_abData3[j + 9] = 0x00;   /* */
        s_abData3[j + 10]= i;
        s_abData3[j + 11]= 0x00;
    }

    static const char s_szEnc3[] =
        "AP8A/wD/AAAAAAAAAf8B/wH/AQABAAEAAv8C/wL/AgACAAIAA/8D/wP/AwADAAMA" MY_NL
        "BP8E/wT/BAAEAAQABf8F/wX/BQAFAAUABv8G/wb/BgAGAAYAB/8H/wf/BwAHAAcA" MY_NL
        "CP8I/wj/CAAIAAgACf8J/wn/CQAJAAkACv8K/wr/CgAKAAoAC/8L/wv/CwALAAsA" MY_NL
        "DP8M/wz/DAAMAAwADf8N/w3/DQANAA0ADv8O/w7/DgAOAA4AD/8P/w//DwAPAA8A" MY_NL
        "EP8Q/xD/EAAQABAAEf8R/xH/EQARABEAEv8S/xL/EgASABIAE/8T/xP/EwATABMA" MY_NL
        "FP8U/xT/FAAUABQAFf8V/xX/FQAVABUAFv8W/xb/FgAWABYAF/8X/xf/FwAXABcA" MY_NL
        "GP8Y/xj/GAAYABgAGf8Z/xn/GQAZABkAGv8a/xr/GgAaABoAG/8b/xv/GwAbABsA" MY_NL
        "HP8c/xz/HAAcABwAHf8d/x3/HQAdAB0AHv8e/x7/HgAeAB4AH/8f/x//HwAfAB8A" MY_NL
        "IP8g/yD/IAAgACAAIf8h/yH/IQAhACEAIv8i/yL/IgAiACIAI/8j/yP/IwAjACMA" MY_NL
        "JP8k/yT/JAAkACQAJf8l/yX/JQAlACUAJv8m/yb/JgAmACYAJ/8n/yf/JwAnACcA" MY_NL
        "KP8o/yj/KAAoACgAKf8p/yn/KQApACkAKv8q/yr/KgAqACoAK/8r/yv/KwArACsA" MY_NL
        "LP8s/yz/LAAsACwALf8t/y3/LQAtAC0ALv8u/y7/LgAuAC4AL/8v/y//LwAvAC8A" MY_NL
        "MP8w/zD/MAAwADAAMf8x/zH/MQAxADEAMv8y/zL/MgAyADIAM/8z/zP/MwAzADMA" MY_NL
        "NP80/zT/NAA0ADQANf81/zX/NQA1ADUANv82/zb/NgA2ADYAN/83/zf/NwA3ADcA" MY_NL
        "OP84/zj/OAA4ADgAOf85/zn/OQA5ADkAOv86/zr/OgA6ADoAO/87/zv/OwA7ADsA" MY_NL
        "PP88/zz/PAA8ADwAPf89/z3/PQA9AD0APv8+/z7/PgA+AD4AP/8//z//PwA/AD8A" MY_NL
        "QP9A/0D/QABAAEAAQf9B/0H/QQBBAEEAQv9C/0L/QgBCAEIAQ/9D/0P/QwBDAEMA" MY_NL
        "RP9E/0T/RABEAEQARf9F/0X/RQBFAEUARv9G/0b/RgBGAEYAR/9H/0f/RwBHAEcA" MY_NL
        "SP9I/0j/SABIAEgASf9J/0n/SQBJAEkASv9K/0r/SgBKAEoAS/9L/0v/SwBLAEsA" MY_NL
        "TP9M/0z/TABMAEwATf9N/03/TQBNAE0ATv9O/07/TgBOAE4AT/9P/0//TwBPAE8A" MY_NL
        "UP9Q/1D/UABQAFAAUf9R/1H/UQBRAFEAUv9S/1L/UgBSAFIAU/9T/1P/UwBTAFMA" MY_NL
        "VP9U/1T/VABUAFQAVf9V/1X/VQBVAFUAVv9W/1b/VgBWAFYAV/9X/1f/VwBXAFcA" MY_NL
        "WP9Y/1j/WABYAFgAWf9Z/1n/WQBZAFkAWv9a/1r/WgBaAFoAW/9b/1v/WwBbAFsA" MY_NL
        "XP9c/1z/XABcAFwAXf9d/13/XQBdAF0AXv9e/17/XgBeAF4AX/9f/1//XwBfAF8A" MY_NL
        "YP9g/2D/YABgAGAAYf9h/2H/YQBhAGEAYv9i/2L/YgBiAGIAY/9j/2P/YwBjAGMA" MY_NL
        "ZP9k/2T/ZABkAGQAZf9l/2X/ZQBlAGUAZv9m/2b/ZgBmAGYAZ/9n/2f/ZwBnAGcA" MY_NL
        "aP9o/2j/aABoAGgAaf9p/2n/aQBpAGkAav9q/2r/agBqAGoAa/9r/2v/awBrAGsA" MY_NL
        "bP9s/2z/bABsAGwAbf9t/23/bQBtAG0Abv9u/27/bgBuAG4Ab/9v/2//bwBvAG8A" MY_NL
        "cP9w/3D/cABwAHAAcf9x/3H/cQBxAHEAcv9y/3L/cgByAHIAc/9z/3P/cwBzAHMA" MY_NL
        "dP90/3T/dAB0AHQAdf91/3X/dQB1AHUAdv92/3b/dgB2AHYAd/93/3f/dwB3AHcA" MY_NL
        "eP94/3j/eAB4AHgAef95/3n/eQB5AHkAev96/3r/egB6AHoAe/97/3v/ewB7AHsA" MY_NL
        "fP98/3z/fAB8AHwAff99/33/fQB9AH0Afv9+/37/fgB+AH4Af/9//3//fwB/AH8A" MY_NL
        "gP+A/4D/gACAAIAAgf+B/4H/gQCBAIEAgv+C/4L/ggCCAIIAg/+D/4P/gwCDAIMA" MY_NL
        "hP+E/4T/hACEAIQAhf+F/4X/hQCFAIUAhv+G/4b/hgCGAIYAh/+H/4f/hwCHAIcA" MY_NL
        "iP+I/4j/iACIAIgAif+J/4n/iQCJAIkAiv+K/4r/igCKAIoAi/+L/4v/iwCLAIsA" MY_NL
        "jP+M/4z/jACMAIwAjf+N/43/jQCNAI0Ajv+O/47/jgCOAI4Aj/+P/4//jwCPAI8A" MY_NL
        "kP+Q/5D/kACQAJAAkf+R/5H/kQCRAJEAkv+S/5L/kgCSAJIAk/+T/5P/kwCTAJMA" MY_NL
        "lP+U/5T/lACUAJQAlf+V/5X/lQCVAJUAlv+W/5b/lgCWAJYAl/+X/5f/lwCXAJcA" MY_NL
        "mP+Y/5j/mACYAJgAmf+Z/5n/mQCZAJkAmv+a/5r/mgCaAJoAm/+b/5v/mwCbAJsA" MY_NL
        "nP+c/5z/nACcAJwAnf+d/53/nQCdAJ0Anv+e/57/ngCeAJ4An/+f/5//nwCfAJ8A" MY_NL
        "oP+g/6D/oACgAKAAof+h/6H/oQChAKEAov+i/6L/ogCiAKIAo/+j/6P/owCjAKMA" MY_NL
        "pP+k/6T/pACkAKQApf+l/6X/pQClAKUApv+m/6b/pgCmAKYAp/+n/6f/pwCnAKcA" MY_NL
        "qP+o/6j/qACoAKgAqf+p/6n/qQCpAKkAqv+q/6r/qgCqAKoAq/+r/6v/qwCrAKsA" MY_NL
        "rP+s/6z/rACsAKwArf+t/63/rQCtAK0Arv+u/67/rgCuAK4Ar/+v/6//rwCvAK8A" MY_NL
        "sP+w/7D/sACwALAAsf+x/7H/sQCxALEAsv+y/7L/sgCyALIAs/+z/7P/swCzALMA" MY_NL
        "tP+0/7T/tAC0ALQAtf+1/7X/tQC1ALUAtv+2/7b/tgC2ALYAt/+3/7f/twC3ALcA" MY_NL
        "uP+4/7j/uAC4ALgAuf+5/7n/uQC5ALkAuv+6/7r/ugC6ALoAu/+7/7v/uwC7ALsA" MY_NL
        "vP+8/7z/vAC8ALwAvf+9/73/vQC9AL0Avv++/77/vgC+AL4Av/+//7//vwC/AL8A" MY_NL
        "wP/A/8D/wADAAMAAwf/B/8H/wQDBAMEAwv/C/8L/wgDCAMIAw//D/8P/wwDDAMMA" MY_NL
        "xP/E/8T/xADEAMQAxf/F/8X/xQDFAMUAxv/G/8b/xgDGAMYAx//H/8f/xwDHAMcA" MY_NL
        "yP/I/8j/yADIAMgAyf/J/8n/yQDJAMkAyv/K/8r/ygDKAMoAy//L/8v/ywDLAMsA" MY_NL
        "zP/M/8z/zADMAMwAzf/N/83/zQDNAM0Azv/O/87/zgDOAM4Az//P/8//zwDPAM8A" MY_NL
        "0P/Q/9D/0ADQANAA0f/R/9H/0QDRANEA0v/S/9L/0gDSANIA0//T/9P/0wDTANMA" MY_NL
        "1P/U/9T/1ADUANQA1f/V/9X/1QDVANUA1v/W/9b/1gDWANYA1//X/9f/1wDXANcA" MY_NL
        "2P/Y/9j/2ADYANgA2f/Z/9n/2QDZANkA2v/a/9r/2gDaANoA2//b/9v/2wDbANsA" MY_NL
        "3P/c/9z/3ADcANwA3f/d/93/3QDdAN0A3v/e/97/3gDeAN4A3//f/9//3wDfAN8A" MY_NL
        "4P/g/+D/4ADgAOAA4f/h/+H/4QDhAOEA4v/i/+L/4gDiAOIA4//j/+P/4wDjAOMA" MY_NL
        "5P/k/+T/5ADkAOQA5f/l/+X/5QDlAOUA5v/m/+b/5gDmAOYA5//n/+f/5wDnAOcA" MY_NL
        "6P/o/+j/6ADoAOgA6f/p/+n/6QDpAOkA6v/q/+r/6gDqAOoA6//r/+v/6wDrAOsA" MY_NL
        "7P/s/+z/7ADsAOwA7f/t/+3/7QDtAO0A7v/u/+7/7gDuAO4A7//v/+//7wDvAO8A" MY_NL
        "8P/w//D/8ADwAPAA8f/x//H/8QDxAPEA8v/y//L/8gDyAPIA8//z//P/8wDzAPMA" MY_NL
        "9P/0//T/9AD0APQA9f/1//X/9QD1APUA9v/2//b/9gD2APYA9//3//f/9wD3APcA" MY_NL
        "+P/4//j/+AD4APgA+f/5//n/+QD5APkA+v/6//r/+gD6APoA+//7//v/+wD7APsA" MY_NL
        "/P/8//z//AD8APwA/f/9//3//QD9AP0A/v/+//7//gD+AP4A/////////wD/AP8A";

    tstBase64(s_abData3, sizeof(s_abData3),
              s_szEnc3,  sizeof(s_szEnc3) - 1,
              0 /* fTextData */, 0 /* fNormalEnc */);

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

