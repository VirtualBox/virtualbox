/* $Id: tstClipboardMimeConv.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard MIME converter testcase.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/GuestHost/mime-type-converter.h>

#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>
#include <iprt/zero.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#if 0 /** @todo r=bird: file transfers require a very different approach... */

/** MIME enumeration test state. */
typedef struct TSTMIMECONVENUMCTX
{
    /** Whether text/uri-list was seen. */
    bool fFoundUriList;
    /** Flags and priority reported for text/uri-list. */
    uint32_t fFlagsAndPriority;
} TSTMIMECONVENUMCTX;
/** Pointer to a MIME enumeration test state. */
typedef TSTMIMECONVENUMCTX *PTSTMIMECONVENUMCTX;


/**
 * MIME enumeration callback used by testUriListMapping().
 *
 * @param   pcszMimeType        MIME type being enumerated.
 * @param   fFlagsAndPriority   MIME converter flags and priority.
 * @param   pvUser              Pointer to TSTMIMECONVENUMCTX.
 */
static DECLCALLBACK(void) tstMimeConvEnumCallback(const char *pcszMimeType, uint32_t fFlagsAndPriority, void *pvUser)
{
    PTSTMIMECONVENUMCTX pCtx = (PTSTMIMECONVENUMCTX)pvUser;
    if (RTStrCmp(pcszMimeType, "text/uri-list") == 0)
    {
        pCtx->fFoundUriList = true;
        pCtx->fFlagsAndPriority = fFlagsAndPriority;
    }
}


/**
 * Tests the transfer URI-list MIME mapping and copy conversion.
 */
static void testUriListMapping(void)
{
    RTTestISub("Testing text/uri-list mapping");

    uint32_t fFlagsAndPriority = 0;
    const char *pcszPersistentMimeType = NULL;
    SHCLFORMAT const uFmt = VbghMimeConvGetVBoxFormatByMime("text/uri-list", &fFlagsAndPriority,
                                                            &pcszPersistentMimeType);
    RTTESTI_CHECK_MSG(uFmt == VBOX_SHCL_FMT_URI_LIST, ("uFmt=%#x\n", uFmt));
    RTTESTI_CHECK_MSG(pcszPersistentMimeType != NULL, ("pcszPersistentMimeType=NULL\n"));
    if (pcszPersistentMimeType)
        RTTESTI_CHECK_MSG(RTStrCmp(pcszPersistentMimeType, "text/uri-list") == 0,
                          ("pcszPersistentMimeType=%s\n", pcszPersistentMimeType));
    RTTESTI_CHECK_MSG((fFlagsAndPriority & VBGH_MIME_CONV_F_PRIORITY_MASK) != 0,
                      ("fFlagsAndPriority=%#x\n", fFlagsAndPriority));

    TSTMIMECONVENUMCTX EnumCtx;
    RT_ZERO(EnumCtx);
    VbghMimeConvEnumerateByVBoxFormats(VBOX_SHCL_FMT_URI_LIST, tstMimeConvEnumCallback, &EnumCtx);
    RTTESTI_CHECK_MSG(EnumCtx.fFoundUriList, ("text/uri-list was not enumerated\n"));

    char szUriList[] = "file:///tmp/vbox-shcl-a\r\nfile:///tmp/vbox-shcl-b\r\n";
    void *pvOut = NULL;
    size_t cbOut = 0;
    int rc = VbghMimeConvToVBox("text/uri-list", szUriList, strlen(szUriList), &pvOut, &cbOut);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK_MSG(cbOut == strlen(szUriList), ("cbOut=%zu expected=%zu\n", cbOut, strlen(szUriList)));
        RTTESTI_CHECK_MSG(memcmp(pvOut, szUriList, cbOut) == 0, ("URI-list ToVBox data changed\n"));
        VbghMimeConvFreeBuf(pvOut, cbOut);
    }

    pvOut = NULL;
    cbOut = 0;
    rc = VbghMimeConvFromVBox("text/uri-list", szUriList, strlen(szUriList), &pvOut, &cbOut);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK_MSG(cbOut == strlen(szUriList), ("cbOut=%zu expected=%zu\n", cbOut, strlen(szUriList)));
        RTTESTI_CHECK_MSG(memcmp(pvOut, szUriList, cbOut) == 0, ("URI-list FromVBox data changed\n"));
        VbghMimeConvFreeBuf(pvOut, cbOut);
    }

    uint8_t abInvalid[] = { 'f', 'i', 'l', 'e', ':', '/', '/', 0xff, 0x00 };
    pvOut = NULL;
    cbOut = 0;
    rc = VbghMimeConvToVBox("text/uri-list", abInvalid, sizeof(abInvalid), &pvOut, &cbOut);
    RTTESTI_CHECK_MSG(RT_FAILURE(rc), ("invalid UTF-8 URI-list accepted by ToVBox: rc=%Rrc\n", rc));
    VbghMimeConvFreeBuf(pvOut, cbOut);

    pvOut = NULL;
    cbOut = 0;
    rc = VbghMimeConvFromVBox("text/uri-list", abInvalid, sizeof(abInvalid), &pvOut, &cbOut);
    RTTESTI_CHECK_MSG(RT_FAILURE(rc), ("invalid UTF-8 URI-list accepted by FromVBox: rc=%Rrc\n", rc));
    VbghMimeConvFreeBuf(pvOut, cbOut);
}

#endif


/**
 * Tests escaped output containing multibyte UTF-8 on a text-mode stream.
 */
static void testEscapedString(void)
{
    RTTestISub("escaped UTF-8 output");

    static const char s_szInput[] =
        "A\"\\\n\t"
        "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
    static const char s_szExpected[] =
        "A\\\"\\\\\\n\\t"
        "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";

    char szFilename[RTPATH_MAX];
    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpenTemp(&hFile, szFilename, sizeof(szFilename),
                            RTFILE_O_CREATE | RTFILE_O_READWRITE | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
    {
        RTTestIFailed("RTFileOpenTemp failed: %Rrc", rc);
        return;
    }
    rc = RTFileClose(hFile);
    if (RT_FAILURE(rc))
    {
        RTTestIFailed("RTFileClose failed: %Rrc", rc);
        RTFileDelete(szFilename);
        return;
    }

    PRTSTREAM pStrm = NULL;
    rc = RTStrmOpen(szFilename, "w", &pStrm);
    if (RT_SUCCESS(rc))
        rc = RTStrmSetMode(pStrm, false /* fBinary */, true /* fCurrentCodeSet */);
    if (RT_SUCCESS(rc))
    {
        ShClHlpPrintEscapedString(pStrm, s_szInput, sizeof(s_szInput) - 1);
        rc = RTStrmError(pStrm);
    }
    if (RT_FAILURE(rc))
        RTTestIFailed("Writing escaped UTF-8 failed: %Rrc", rc);
    int const rcClose = RTStrmClose(pStrm);
    if (RT_FAILURE(rcClose))
        RTTestIFailed("RTStrmClose failed: %Rrc", rcClose);

    void *pvOutput = NULL;
    size_t cbOutput = 0;
    rc = RTFileReadAll(szFilename, &pvOutput, &cbOutput);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTFileReadAll failed: %Rrc", rc);
    else
    {
        RTTESTI_CHECK_MSG(cbOutput == sizeof(s_szExpected) - 1,
                          ("cbOutput=%zu expected=%zu\n", cbOutput, sizeof(s_szExpected) - 1));
        if (cbOutput == sizeof(s_szExpected) - 1)
            RTTESTI_CHECK_MSG(memcmp(pvOutput, s_szExpected, cbOutput) == 0,
                              ("output='%.*Rhxs' expected='%.*Rhxs'\n",
                               cbOutput, pvOutput, sizeof(s_szExpected) - 1, s_szExpected));
        RTFileReadAllFree(pvOutput, cbOutput);
    }

    rc = RTFileDelete(szFilename);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}


static void testText(RTTEST hTest)
{
    RTTestISub("text");
    static const char s_szHtml1[]  = "<h1>It works!</h1>\n";
    static const char s_wszHtml1LE[] =
        "\xFF\xFE\x3C\x00\x68\x00\x31\x00\x3E\x00\x49\x00\x74\x00\x20\x00"  // ..<.h.1.>.I.t. .
        "\x77\x00\x6F\x00\x72\x00\x6B\x00\x73\x00\x21\x00\x3C\x00\x2F\x00"  // w.o.r.k.s.!.<./.
        "\x68\x00\x31\x00\x3E\x00\x0A\x00\x00";                             // h.1.>..  + '\0\0'
    AssertCompile((sizeof(s_wszHtml1LE) & 1) == 0);
    static const char s_wszHtml1BE[] =
        "\xFE\xFF\x00\x3C\x00\x68\x00\x31\x00\x3E\x00\x49\x00\x74\x00\x20"
        "\x00\x77\x00\x6F\x00\x72\x00\x6B\x00\x73\x00\x21\x00\x3C\x00\x2F"
        "\x00\x68\x00\x31\x00\x3E\x00\x0A\x00";
    AssertCompile((sizeof(s_wszHtml1BE) & 1) == 0);

    static const char s_szHtml2[]  = "<h1>\xE6\x9C\x89\xE7\x94\xA8!</h1>\n";
    static const char s_wszHtml2[] =
        "\xFF\xFE\x3C\x00\x68\x00\x31\x00\x3E\x00"                          // ..<.h.1.>
        "\x09\x67\x28\x75"                                                  // chinese for "It works"
        "\x21\x00\x3C\x00\x2F\x00\x68\x00\x31\x00\x3E\x00\x0A\x00\x00";     // !.<./.h.1.>..  + '\0\0'
    AssertCompile((sizeof(s_wszHtml2) & 1) == 0);
#define F_VB_UTF8   1
#define F_NV_UTF16  2
    static struct
    {
        const char *pszMimeType;    /**< The MIME type */
        unsigned    fFlags;         /**< 0, F_VB_UTF8*/
        const char *pszVBox;        /**< The UTF-8 version of the VBox output. */
        const char *pszNativeSrc;   /**< The native source text. */
        size_t      cchNativeSrc;   /**< Length of the native source text. */
        const char *pszNativeOut;   /**< The native output text, optional. */
        size_t      cchNativeOut;   /**< Length of the native output text, optional. */
        int         rcToVBox;
        int         rcFromVBox;
    } const s_aTexts[] =
    {
        /* utf-8 */
        { "text/plain;charset=utf-8",   0, "",                          },
        { "text/plain;charset=utf-8",   0, "VirtualBox",                },
        { "text/plain;charset=utf-8",   0, "\r\n",                      RT_STR_TUPLE("\n") },
        { "text/plain;charset=utf-8",   0, "1\r\n2",                    RT_STR_TUPLE("1\n2") },
        { "text/plain;charset=utf-8",   0, "1\r\n2\r\n",                RT_STR_TUPLE("1\n2\n") },
        { "teXt/plAin;chaRseT=utF-8",   0, "1\r\n2\r\n",                RT_STR_TUPLE("1\n2\n") },
        { "TEXT/PLAIN;CHARSET=UTF-8",   0, "1\r\n2\r\n3\r\n",           RT_STR_TUPLE("1\r\n2\n3\r\n"), RT_STR_TUPLE("1\n2\n3\n")},

        /* latin-1 */
        { "text/plain",                 0, "",                          },
        { "text/plain",                 0, "VirtualBox",                },
        { "teXT/PLain",                 0, "\r\n",                      RT_STR_TUPLE("\n") },
        { "TEXT/plain",                 0, "1\r\n2",                    RT_STR_TUPLE("1\n2") },
        { "text/PLAIN",                 0, "1\r\n2\r\n",                RT_STR_TUPLE("1\n2\n") },
        { "text/plain",                 0, "aa=\xC3\xA5",               RT_STR_TUPLE("aa=\xE5") },
        { "text/plain",                 0, "1,\r\n\xE4\xBA\x8C,\r\n3",  RT_STR_TUPLE("1,\n\\u4e8c,\n3"),
          NULL, 0, VINF_SUCCESS, VWRN_NO_TRANSLATION, },
        { "TEXT/PLAIN",                 0, "1\r\n2\r\n3\r\n",           RT_STR_TUPLE("1\r\n2\n3\r\n"), RT_STR_TUPLE("1\n2\n3\n")},

        /* html (w/o charset spec) */
        { "text/html",               F_VB_UTF8, "", },
        { "text/html",               F_VB_UTF8, "<!DOCTYPE html><html><head><title>hello</title></head></html>", },
        { "TexT/hTml",               F_VB_UTF8, "<!DOCTYPE html>\n<html>\n\t<head><title>hello</title></head>\n</html>", },
        { "text/html",               F_VB_UTF8, "<!DOCTYPE html>\r\n<html>\r\n\t<head><title>hello</title></head>\r\n</html>\r\n", },
        { "text/html",  F_NV_UTF16 | F_VB_UTF8, s_szHtml1, s_wszHtml1LE, sizeof(s_wszHtml1LE) - 2, RT_STR_TUPLE(s_szHtml1) },
        { "text/html",  F_NV_UTF16 | F_VB_UTF8, s_szHtml1, s_wszHtml1BE, sizeof(s_wszHtml1BE) - 2, RT_STR_TUPLE(s_szHtml1) },
        { "text/html",  F_NV_UTF16 | F_VB_UTF8, s_szHtml2, s_wszHtml2,   sizeof(s_wszHtml2) - 2,   RT_STR_TUPLE(s_szHtml2) },

        /* html charset=utf-8*/
        { "text/html;charset=utf-8", F_VB_UTF8, "", },
        { "text/html;charset=utf-8", F_VB_UTF8, "<!DOCTYPE html><html><head><title>hello</title></head></html>", },
        { "text/htmL;chaRset=utf-8", F_VB_UTF8, "<!DOCTYPE html>\n<html>\n\t<head><title>hello</title></head>\n</html>", },
        { "text/html;charset=utf-8", F_VB_UTF8, "<!DOCTYPE html>\r\n<html>\r\n\t<head><title>hello</title></head>\r\n</html>\r\n", },
        { "tExT/HTml;cHarseT=uTf-8", F_VB_UTF8, "<!DOCTYPE html>\r\n<html>\r\n\t<head><title>hello</title></head>\r\n</html>\r\n", },

#if 0 /* busted (probably useless) */
        /* uri-list */
        { "text/uri-list",              1, "file:///tmp\r\n",           },
#endif
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aTexts); i++)
    {
        void               *pvOut;
        size_t              cbOut;
        int                 rc;

        /* Copy & expand the test data. */
        const char * const  pszMimeType      = s_aTexts[i].pszMimeType;
        const char * const  pszNativeSrc     = s_aTexts[i].pszNativeSrc ? s_aTexts[i].pszNativeSrc : s_aTexts[i].pszVBox;
        size_t const        cchNativeSrc     = s_aTexts[i].pszNativeSrc ? s_aTexts[i].cchNativeSrc : strlen(s_aTexts[i].pszVBox);
        size_t const        cchNativeInZero  = s_aTexts[i].fFlags & F_NV_UTF16 ? 2 : 1;
        const char * const  pszNativeOut     = s_aTexts[i].pszNativeOut ? s_aTexts[i].pszNativeOut : pszNativeSrc;
        size_t const        cchNativeOut     = s_aTexts[i].pszNativeOut ? s_aTexts[i].cchNativeOut : cchNativeSrc;
        size_t const        cchNativeOutZero = 1;
        int const           rcToVBox         = s_aTexts[i].rcToVBox;
        int const           rcFromVBox       = s_aTexts[i].rcFromVBox;

        /* Produce the VBox string. This is UTF-8 for html and URI-lists. */
        size_t const        cbVBox   = s_aTexts[i].fFlags & F_VB_UTF8
                                     ? strlen(s_aTexts[i].pszVBox) + 1
                                     : (RTStrCalcUtf16Len(s_aTexts[i].pszVBox) + 1) * sizeof(RTUTF16);
        void * const        pvVBox   = RTTestGuardedAllocTail(hTest, cbVBox);
        if (s_aTexts[i].fFlags & F_VB_UTF8)
            memcpy(pvVBox, s_aTexts[i].pszVBox, cbVBox);
        else
        {
            PRTUTF16 pwszVBox = (PRTUTF16)pvVBox;
            rc = RTStrToUtf16Ex(s_aTexts[i].pszVBox, RTSTR_MAX, &pwszVBox, cbVBox / sizeof(RTUTF16), NULL);
            if (RT_FAILURE(rc))
            {
                RTTestIFailed("Failed to convert vbox string #%u(%s) to UTF-16: %Rrc", i, pszMimeType, rc);
                RTTestGuardedFree(hTest, pvVBox);
                continue;
            }
            RTTESTI_CHECK((void *)pwszVBox == pvVBox);
            RTTESTI_CHECK(RTUtf16Len(pwszVBox) == cbVBox / sizeof(RTUTF16) - 1);
        }

        /* Translate To VBox and check the output. */
        if (rcFromVBox != VWRN_NO_TRANSLATION)
        {
            pvOut = NULL;
            cbOut = 0;
            rc = VbghMimeConvToVBox(pszMimeType, pszNativeSrc, cchNativeSrc, &pvOut, &cbOut);
            if (RT_FAILURE(rc))
                RTTestIFailed("string #%u(%s): VbghMimeConvToVBox failed: %Rrc", i, pszMimeType, rc);
            else if (cbOut != cbVBox || memcmp(pvOut, pvVBox, cbVBox))
                RTTestIFailed("string #%u(%s): Wrong VbghMimeConvToVBox output: %#zx bytes, expected %#zx\n'%.*Rhxs', expected\n'%.*Rhxs'",
                              i, pszMimeType, cbOut, cbVBox, cbOut, pvOut, cbVBox, pvVBox);
            else if (rc != rcToVBox)
                RTTestIFailed("string #%u(%s): VbghMimeConvToVBox returned %Rrc instead of %Rrc", i, pszMimeType, rc, rcToVBox);
            VbghMimeConvFreeBuf(pvOut, cbOut);

            /* Translate To VBox, but supply buffer including the zero terminator in the count. */
            pvOut = NULL;
            cbOut = 0;
            rc = VbghMimeConvToVBox(pszMimeType, pszNativeSrc, cchNativeSrc + cchNativeInZero, &pvOut, &cbOut);
            if (RT_FAILURE(rc))
                RTTestIFailed("string #%u(%s): VbghMimeConvToVBox w/zero failed: %Rrc", i, pszMimeType, rc);
            else if (cbOut != cbVBox || memcmp(pvOut, pvVBox, cbVBox))
                RTTestIFailed("string #%u(%s): Wrong VbghMimeConvToVBox w/zero output: %#zx bytes, expected %#zx\n'%.*Rhxs', expected\n'%.*Rhxs'",
                              i, pszMimeType, cbOut, cbVBox, cbOut, pvOut, cbVBox, pvVBox);
            else if (rc != rcToVBox)
                RTTestIFailed("string #%u(%s): VbghMimeConvToVBox w/zero returned %Rrc instead of %Rrc", i, pszMimeType, rc, rcToVBox);
            VbghMimeConvFreeBuf(pvOut, cbOut);

            /* Translate to VBox, but supply an untermianted input string with an electric tail guard. */
            char * const pachGuardedIn = (char *)RTTestGuardedAllocTail(hTest, cchNativeSrc);
            if (pachGuardedIn)
            {
                memcpy(pachGuardedIn, pszNativeSrc, cchNativeSrc);
                pvOut = NULL;
                cbOut = 0;
                rc = VbghMimeConvToVBox(pszMimeType, pszNativeSrc, cchNativeSrc, &pvOut, &cbOut);
                if (RT_FAILURE(rc))
                    RTTestIFailed("string #%u(%s): VbghMimeConvToVBox w/o zero failed: %Rrc", i, pszMimeType, rc);
                else if (cbOut != cbVBox || memcmp(pvOut, pvVBox, cbVBox))
                    RTTestIFailed("string #%u(%s): Wrong VbghMimeConvToVBox w/o zero output: %#zx bytes, expected %#zx\n'%.*Rhxs', expected\n'%.*Rhxs'",
                                  i, pszMimeType, cbOut, cbVBox, cbOut, pvOut, cbVBox, pvVBox);
                else if (rc != rcToVBox)
                    RTTestIFailed("string #%u(%s): VbghMimeConvToVBox w/o zero returned %Rrc instead of %Rrc", i, pszMimeType, rc, rcToVBox);
                VbghMimeConvFreeBuf(pvOut, cbOut);
                RTTestGuardedFree(hTest, pachGuardedIn);
            }
        }

        /* Translate the other way. */
        pvOut = NULL;
        cbOut = 0;
        rc = VbghMimeConvFromVBox(pszMimeType, pvVBox, cbVBox, &pvOut, &cbOut);
        if (RT_FAILURE(rc))
            RTTestIFailed("string #%u(%s): VbghMimeConvFromVBox failed: %Rrc", i, pszMimeType, rc);
        else if (cbOut != cchNativeOut || memcmp(pvOut, pszNativeOut, cchNativeOut))
            RTTestIFailed("string #%u(%s): Wrong VbghMimeConvFromVBox output: %#zx bytes, expected %#zx\n'%.*Rhxs', expected\n'%.*Rhxs'",
                          i, pszMimeType, cbOut, cchNativeOut, cbOut, pvOut, cchNativeOut, pszNativeOut);
        else if (memcmp((char *)pvOut + cbOut, g_abRTZero4K, cchNativeOutZero))
            RTTestIFailed("string #%u(%s): Incorrectly terminated VbghMimeConvToVBox output: %.*Rhxs",
                          i, pszMimeType, cchNativeOutZero, (char *)pvOut + cbOut);
        else if (rc != rcFromVBox)
            RTTestIFailed("string #%u(%s): VbghMimeConvFromVBox returned %Rrc instead of %Rrc", i, pszMimeType, rc, rcFromVBox);
        VbghMimeConvFreeBuf(pvOut, cbOut);

        /* Cleanup VBox buffer. */
        RTTestGuardedFree(hTest, pvVBox);
    }
}


/**
 * Testcase entry point.
 */
int main(int argc, char **argv)
{
    RT_NOREF(argc, argv);

    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardMimeConv", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    RTTestBanner(hTest);
    testEscapedString();
    testText(hTest);
#if 0 /** @todo r=bird: file transfers require a very different approach... */
    testUriListMapping();
#endif

    return RTTestSummaryAndDestroy(hTest);
}
