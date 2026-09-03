/* $Id: tstClipboardDataObjectWin.cpp 115147 2026-09-03 07:10:22Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Windows IDataObject testcase.
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

#include <iprt/win/windows.h>
#include <iprt/win/shlobj.h>

#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-transfers.h>
#include <VBox/GuestHost/SharedClipboard-win.h>

#include <string.h>


typedef struct TESTFILE
{
    const char  *pszNameUtf8;
    const WCHAR *pwszName;
} TESTFILE;


/*
 * Keep all filenames escaped so the source file itself does not depend on the
 * editor/source encoding.  The narrow strings are UTF-8.  The wide strings are
 * UTF-16 code units expected in FILEDESCRIPTORW::cFileName.
 */
static const TESTFILE g_aFiles[] =
{
    {
        /*
         * plain-ascii.txt
         *
         * Pure ASCII baseline.
         */
        "\x70\x6c\x61\x69\x6e\x2d\x61\x73\x63\x69\x69\x2e\x74\x78\x74",
        L"\x0070\x006c\x0061\x0069\x006e\x002d\x0061\x0073\x0063\x0069\x0069\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * cafe-with-accents:
         *
         *   c a f U+00E9 - U+00FC b e r - U+00C5 n g s t r U+00F6 m .txt
         *
         * Latin characters:
         *   U+00E9 LATIN SMALL LETTER E WITH ACUTE
         *   U+00FC LATIN SMALL LETTER U WITH DIAERESIS
         *   U+00C5 LATIN CAPITAL LETTER A WITH RING ABOVE
         *   U+00F6 LATIN SMALL LETTER O WITH DIAERESIS
         */
        "\x63\x61\x66\xc3\xa9\x2d\xc3\xbc\x62\x65\x72\x2d\xc3\x85\x6e\x67\x73\x74\x72\xc3\xb6\x6d\x2e\x74\x78\x74",
        L"\x0063\x0061\x0066\x00e9\x002d\x00fc\x0062\x0065\x0072\x002d\x00c5\x006e\x0067\x0073\x0074\x0072\x00f6\x006d\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * cafe + combining acute accent:
         *
         *   c a f e U+0301 - n f d .txt
         *
         * This visually resembles precomposed "cafe with acute", but contains:
         *   U+0065 LATIN SMALL LETTER E
         *   U+0301 COMBINING ACUTE ACCENT
         *
         * The IDataObject path must preserve this exact sequence and must not
         * normalize it to precomposed U+00E9.
         */
        "\x63\x61\x66\x65\xcc\x81\x2d\x6e\x66\x64\x2e\x74\x78\x74",
        L"\x0063\x0061\x0066\x0065\x0301\x002d\x006e\x0066\x0064\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Russian Cyrillic:
         *
         *   Privet-mir.txt, roughly "hello-world.txt"
         *
         * Unicode:
         *   U+041F U+0440 U+0438 U+0432 U+0435 U+0442
         *   U+002D
         *   U+043C U+0438 U+0440
         *
         * This is the main mojibake regression case.  If UTF-8 bytes are
         * exposed through FILEDESCRIPTORA instead of FILEDESCRIPTORW, this
         * often turns into text beginning with U+00D0 / U+00D1 mojibake.
         */
        "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82\x2d\xd0\xbc\xd0\xb8\xd1\x80\x2e\x74\x78\x74",
        L"\x041f\x0440\x0438\x0432\x0435\x0442\x002d\x043c\x0438\x0440\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Greek:
         *
         *   dokimi-kosmos.txt, roughly "test-world.txt"
         *
         * Includes:
         *   U+03AE GREEK SMALL LETTER ETA WITH TONOS
         *   U+03CC GREEK SMALL LETTER OMICRON WITH TONOS
         *   U+03C2 GREEK SMALL LETTER FINAL SIGMA
         */
        "\xce\xb4\xce\xbf\xce\xba\xce\xb9\xce\xbc\xce\xae\x2d\xce\xba\xcf\x8c\xcf\x83\xce\xbc\xce\xbf\xcf\x82\x2e\x74\x78\x74",
        L"\x03b4\x03bf\x03ba\x03b9\x03bc\x03ae\x002d\x03ba\x03cc\x03c3\x03bc\x03bf\x03c2\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Polish / Central European Latin:
         *
         *   za + U+017C U+00F3 U+0142 U+0107
         *   ge + U+0119 U+015B l U+0105
         *   ja + U+017A U+0144
         *
         * Useful for catching accidental host ANSI code page assumptions.
         */
        "\x7a\x61\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87\x2d\x67\xc4\x99\xc5\x9b\x6c\xc4\x85\x2d\x6a\x61\xc5\xba\xc5\x84\x2e\x74\x78\x74",
        L"\x007a\x0061\x017c\x00f3\x0142\x0107\x002d\x0067\x0119\x015b\x006c\x0105\x002d\x006a\x0061\x017a\x0144\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Hebrew, right-to-left:
         *
         *   shalom-olam.txt, roughly "hello-world" / "peace-world"
         *
         * The test compares logical Unicode order, not visual display order.
         */
        "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d\x2d\xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d\x2e\x74\x78\x74",
        L"\x05e9\x05dc\x05d5\x05dd\x002d\x05e2\x05d5\x05dc\x05dd\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Arabic, right-to-left:
         *
         *   marhaba-alam.txt, "hello-world.txt"
         *
         * The test compares logical Unicode order, not visual display order.
         */
        "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7\x2d\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85\x2e\x74\x78\x74",
        L"\x0645\x0631\x062d\x0628\x0627\x002d\x0639\x0627\x0644\x0645\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Simplified Chinese:
         *
         *   U+4E2D U+6587 - U+6D4B U+8BD5 .txt
         *
         * Meaning:
         *   first word: Chinese language/text
         *   second word: test
         */
        "\xe4\xb8\xad\xe6\x96\x87\x2d\xe6\xb5\x8b\xe8\xaf\x95\x2e\x74\x78\x74",
        L"\x4e2d\x6587\x002d\x6d4b\x8bd5\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Japanese:
         *
         *   U+65E5 U+672C U+8A9E - U+30C6 U+30B9 U+30C8 .txt
         *
         * Meaning:
         *   first word: Japanese language
         *   second word: test, written in katakana
         */
        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\x2d\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88\x2e\x74\x78\x74",
        L"\x65e5\x672c\x8a9e\x002d\x30c6\x30b9\x30c8\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Korean Hangul:
         *
         *   U+D55C U+AE00 - U+D14C U+C2A4 U+D2B8 .txt
         *
         * Meaning:
         *   first word: Hangul
         *   second word: test
         */
        "\xed\x95\x9c\xea\xb8\x80\x2d\xed\x85\x8c\xec\x8a\xa4\xed\x8a\xb8\x2e\x74\x78\x74",
        L"\xd55c\xae00\x002d\xd14c\xc2a4\xd2b8\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Emoji / non-BMP:
         *
         *   emoji - U+1F600 GRINNING FACE - file.txt
         *
         * UTF-8:
         *   F0 9F 98 80
         *
         * UTF-16:
         *   D83D DE00
         */
        "\x65\x6d\x6f\x6a\x69\x2d\xf0\x9f\x98\x80\x2d\x66\x69\x6c\x65\x2e\x74\x78\x74",
        L"\x0065\x006d\x006f\x006a\x0069\x002d\xd83d\xde00\x002d\x0066\x0069\x006c\x0065\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Non-BMP CJK Extension B:
         *
         *   cjk-ext - U+2000B CJK UNIFIED IDEOGRAPH-2000B .txt
         *
         * UTF-8:
         *   F0 A0 80 8B
         *
         * UTF-16:
         *   D840 DC0B
         */
        "\x63\x6a\x6b\x2d\x65\x78\x74\x2d\xf0\xa0\x80\x8b\x2e\x74\x78\x74",
        L"\x0063\x006a\x006b\x002d\x0065\x0078\x0074\x002d\xd840\xdc0b\x002e\x0074\x0078\x0074"
    },
    {
        /*
         * Deliberate mojibake sentinel:
         *
         *   mojibake - U+00D0 U+0178 U+00D1 U+20AC
         *              U+00D0 U+00B8 U+00D0 U+00B2 U+00D0 U+00B5
         *              U+00D1 U+201A .txt
         *
         * These are the literal Unicode characters often produced when UTF-8
         * Cyrillic bytes are interpreted as Windows-1252-ish text.
         *
         * This testcase verifies that the IDataObject path preserves the
         * literal filename and does not try to heuristically repair mojibake.
         */
        "\x6d\x6f\x6a\x69\x62\x61\x6b\x65\x2d\xc3\x90\xc5\xb8\xc3\x91\xe2\x82\xac\xc3\x90\xc2\xb8\xc3\x90\xc2\xb2\xc3\x90\xc2\xb5\xc3\x91\xe2\x80\x9a\x2e\x74\x78\x74",
        L"\x006d\x006f\x006a\x0069\x0062\x0061\x006b\x0065\x002d\x00d0\x0178\x00d1\x20ac\x00d0\x00b8\x00d0\x00b2\x00d0\x00b5\x00d1\x201a\x002e\x0074\x0078\x0074"
    }
};


typedef struct TESTCTX
{
    PSHCLTRANSFER pTransfer;
} TESTCTX;


static DECLCALLBACK(int) testTransferBegin(ShClWinDataObject::PCALLBACKCTX pCbCtx)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCbCtx->pThis, VERR_INVALID_POINTER);

    TESTCTX *pThis = (TESTCTX *)pCbCtx->pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pThis->pTransfer, VERR_INVALID_POINTER);

    int rc = pCbCtx->pThis->SetTransfer(pThis->pTransfer);
    if (RT_SUCCESS(rc))
        rc = pCbCtx->pThis->SetStatus(ShClWinDataObject::Running);
    return rc;
}


static DECLCALLBACK(int) testTransferEnd(ShClWinDataObject::PCALLBACKCTX pCbCtx,
                                         PSHCLTRANSFER pTransfer, int rcTransfer)
{
    RT_NOREF(pCbCtx, pTransfer, rcTransfer);
    return VINF_SUCCESS;
}


static size_t testGetExpectedFileContent(unsigned i, char *pszBuf, size_t cbBuf)
{
    AssertReturn(i < RT_ELEMENTS(g_aFiles), 0);
    AssertReturn(cbBuf > 0, 0);

    pszBuf[0] = '\0';

    RTStrPrintf(pszBuf, cbBuf,
                "tstClipboardDataObjectWin payload #%u: %s\n",
                i, g_aFiles[i].pszNameUtf8);

    pszBuf[cbBuf - 1] = '\0';
    return RTStrNLen(pszBuf, cbBuf);
}


static void testReleaseStgMedium(STGMEDIUM *pMedium)
{
    if (!pMedium)
        return;

    if (pMedium->pUnkForRelease)
        pMedium->pUnkForRelease->Release();
    else if (pMedium->tymed == TYMED_HGLOBAL && pMedium->hGlobal)
        GlobalFree(pMedium->hGlobal);
    else if (pMedium->tymed == TYMED_ISTREAM && pMedium->pstm)
        pMedium->pstm->Release();

    RT_ZERO(*pMedium);
}


static int testCreateTempDir(char *pszTempDir, size_t cbTempDir)
{
    char szTempDir[RTPATH_MAX];

    int rc = RTPathTemp(szTempDir, sizeof(szTempDir));
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathAppend(szTempDir, sizeof(szTempDir), "tstClipboardDataObjectWin");
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTDirCreate(szTempDir, 0700, 0);
    if (rc == VERR_ALREADY_EXISTS)
        rc = VINF_SUCCESS;
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathAppend(szTempDir, sizeof(szTempDir), "XXXXXX");
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTDirCreateTemp(szTempDir, 0700);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTStrCopy(pszTempDir, cbTempDir, szTempDir);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    return VINF_SUCCESS;
}


static int testCreateFile(const char *pszTempDir, unsigned i,
                          char *pszPath, size_t cbPath)
{
    AssertReturn(i < RT_ELEMENTS(g_aFiles), VERR_INVALID_PARAMETER);

    int rc = RTPathJoin(pszPath, cbPath, pszTempDir, g_aFiles[i].pszNameUtf8);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    RTFILE hFile;
    rc = RTFileOpen(&hFile, pszPath, RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    char szPayload[1024];
    size_t const cbPayload = testGetExpectedFileContent(i, szPayload, sizeof(szPayload));

    rc = RTFileWrite(hFile, szPayload, cbPayload, NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    int rc2 = RTFileClose(hFile);
    RTTESTI_CHECK_RC(rc2, VINF_SUCCESS);

    return RT_SUCCESS(rc) ? rc2 : rc;
}


static int testCreateTransfer(const char * const *papszRoots, unsigned cRoots,
                              PSHCLTRANSFER *ppTransfer)
{
    *ppTransfer = NULL;

    PSHCLTRANSFER pTransfer = NULL;
    char *pszRoots = NULL;

    int rc = ShClTransferCreate(SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL,
                                NULL /* pCallbacks */, &pTransfer);
    if (RT_FAILURE(rc))
    {
        RTTESTI_CHECK_RC_OK(rc);
        return rc;
    }

    for (;;)
    {
        rc = ShClWinTransferCreate(NULL /* pCtx */, pTransfer);
        if (RT_FAILURE(rc))
        {
            RTTESTI_CHECK_RC_OK(rc);
            break;
        }

        SHCLTXPROVIDER Provider;
        RT_ZERO(Provider);
        if (!ShClTransferProviderLocalQueryInterface(&Provider))
        {
            RTTestIFailed("ShClTransferProviderLocalQueryInterface failed");
            rc = VERR_NOT_SUPPORTED;
            break;
        }

        rc = ShClTransferSetProvider(pTransfer, &Provider);
        if (RT_FAILURE(rc))
        {
            RTTESTI_CHECK_RC_OK(rc);
            break;
        }

        for (unsigned i = 0; i < cRoots; i++)
        {
            rc = RTStrAAppend(&pszRoots, papszRoots[i]);
            if (RT_FAILURE(rc))
            {
                RTTESTI_CHECK_RC_OK(rc);
                break;
            }

            rc = RTStrAAppend(&pszRoots, SHCL_TRANSFER_URI_LIST_SEP_STR);
            if (RT_FAILURE(rc))
            {
                RTTESTI_CHECK_RC_OK(rc);
                break;
            }
        }

        if (RT_FAILURE(rc))
            break;

        rc = ShClTransferRootsSetFromStringList(pTransfer, pszRoots, strlen(pszRoots) + 1);
        if (RT_FAILURE(rc))
        {
            RTTESTI_CHECK_RC_OK(rc);
            break;
        }

        rc = ShClTransferInit(pTransfer);
        if (RT_FAILURE(rc))
        {
            RTTESTI_CHECK_RC_OK(rc);
            break;
        }

        *ppTransfer = pTransfer;
        pTransfer = NULL;
        break;
    }

    RTStrFree(pszRoots);

    if (pTransfer)
    {
        ShClWinTransferDestroy(NULL /* pCtx */, pTransfer);

        int rc2 = ShClTransferDestroy(pTransfer);
        RTTESTI_CHECK_RC_OK(rc2);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


static void testPrintWideNameN(const char *pszWhat, unsigned i,
                               WCHAR const *pwszName, size_t cwcMax)
{
    WCHAR wszTmp[MAX_PATH + 1];

    size_t cwcName = 0;
    while (cwcName < cwcMax && pwszName[cwcName] != L'\0')
        cwcName++;

    size_t const cwcCopy = cwcName < RT_ELEMENTS(wszTmp) - 1
                         ? cwcName
                         : RT_ELEMENTS(wszTmp) - 1;

    memcpy(wszTmp, pwszName, cwcCopy * sizeof(wszTmp[0]));
    wszTmp[cwcCopy] = L'\0';

    char *pszName = NULL;
    int rc = RTUtf16ToUtf8((PCRTUTF16)wszTmp, &pszName);
    if (RT_SUCCESS(rc))
    {
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%s #%u: \"%s\"%s\n",
                      pszWhat, i, pszName,
                      cwcName == cwcMax ? " <not terminated>" : "");
        RTStrFree(pszName);
    }
    else
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%s #%u: <RTUtf16ToUtf8 failed: %Rrc>%s\n",
                      pszWhat, i, rc,
                      cwcName == cwcMax ? " <not terminated>" : "");
}


static void testPrintAnsiNameN(const char *pszWhat, unsigned i,
                               const char *pszName, size_t cchMax)
{
    char szTmp[MAX_PATH + 1];

    size_t const cchName = RTStrNLen(pszName, cchMax);
    size_t const cchCopy = cchName < RT_ELEMENTS(szTmp) - 1
                         ? cchName
                         : RT_ELEMENTS(szTmp) - 1;

    memcpy(szTmp, pszName, cchCopy);
    szTmp[cchCopy] = '\0';

    RTTestIPrintf(RTTESTLVL_ALWAYS, "%s #%u: \"%s\"%s\n",
                  pszWhat, i, szTmp,
                  cchName == cchMax ? " <not terminated>" : "");
}


static void testPrintFileUnderTest(unsigned i)
{
    RTTestIPrintf(RTTESTLVL_ALWAYS, "Preparing file #%u, input UTF-8 name: \"%s\"\n",
                  i, g_aFiles[i].pszNameUtf8);

    testPrintWideNameN("Preparing file expected UTF-16 name",
                       i, g_aFiles[i].pwszName, MAX_PATH);
}


static void testCheckFileGroupDescriptorW(HGLOBAL hGlobal)
{
    SIZE_T const cbActual = GlobalSize(hGlobal);
    SIZE_T const cbExpected = sizeof(FILEGROUPDESCRIPTORW)
                            + (RT_ELEMENTS(g_aFiles) - 1) * sizeof(FILEDESCRIPTORW);

    if (cbActual < cbExpected)
    {
        RTTestIFailed("FILEGROUPDESCRIPTORW HGLOBAL too small: got %zu bytes, expected at least %zu bytes",
                      cbActual, cbExpected);
        return;
    }

    FILEGROUPDESCRIPTORW *pFGD = (FILEGROUPDESCRIPTORW *)GlobalLock(hGlobal);
    if (!pFGD)
    {
        RTTestIFailed("GlobalLock(FILEGROUPDESCRIPTORW) failed, lasterr=%u", GetLastError());
        return;
    }

    if (pFGD->cItems != RT_ELEMENTS(g_aFiles))
    {
        RTTestIFailed("FILEGROUPDESCRIPTORW cItems=%u, expected %u",
                      (unsigned)pFGD->cItems, (unsigned)RT_ELEMENTS(g_aFiles));
        GlobalUnlock(hGlobal);
        return;
    }

    for (unsigned i = 0; i < RT_ELEMENTS(g_aFiles); i++)
    {
        FILEDESCRIPTORW const *pFD = &pFGD->fgd[i];

        RTTestIPrintf(RTTESTLVL_ALWAYS, "Verifying FILEDESCRIPTORW item #%u\n", i);
        RTTestIPrintf(RTTESTLVL_ALWAYS, "Verifying file #%u, original UTF-8 name: \"%s\"\n",
                      i, g_aFiles[i].pszNameUtf8);

        testPrintWideNameN("Verifying file expected UTF-16 name",
                           i, g_aFiles[i].pwszName, MAX_PATH);
        testPrintWideNameN("Verifying file actual FILEDESCRIPTORW name",
                           i, pFD->cFileName, RT_ELEMENTS(pFD->cFileName));

        if (!(pFD->dwFlags & FD_ATTRIBUTES))
            RTTestIFailed("file #%u: FD_ATTRIBUTES is not set", i);

        if (!(pFD->dwFlags & FD_FILESIZE))
            RTTestIFailed("file #%u: FD_FILESIZE is not set", i);

        if (pFD->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            RTTestIFailed("file #%u: unexpectedly marked as directory", i);

#ifdef FD_UNICODE
        if (!(pFD->dwFlags & FD_UNICODE))
            RTTestIFailed("file #%u: FD_UNICODE is not set", i);
#endif

        if (RTUtf16NCmp(pFD->cFileName,
                         g_aFiles[i].pwszName,
                         RT_ELEMENTS(pFD->cFileName)) != 0)
            RTTestIFailed("file #%u: FILEDESCRIPTORW cFileName mismatch", i);

        char szExpected[1024];
        uint64_t const cbExpectedFile = testGetExpectedFileContent(i, szExpected, sizeof(szExpected));
        uint64_t const cbActualFile   = ((uint64_t)pFD->nFileSizeHigh << 32) | pFD->nFileSizeLow;

        if (cbActualFile != cbExpectedFile)
            RTTestIFailed("file #%u: FILEDESCRIPTORW file size %RU64, expected %RU64",
                          i, cbActualFile, cbExpectedFile);
    }

    GlobalUnlock(hGlobal);
}


static void testCheckFileGroupDescriptorA(HGLOBAL hGlobal)
{
    SIZE_T const cbActual = GlobalSize(hGlobal);
    SIZE_T const cbExpected = sizeof(FILEGROUPDESCRIPTORA)
                            + (RT_ELEMENTS(g_aFiles) - 1) * sizeof(FILEDESCRIPTORA);

    if (cbActual < cbExpected)
    {
        RTTestIFailed("FILEGROUPDESCRIPTORA HGLOBAL too small: got %zu bytes, expected at least %zu bytes",
                      cbActual, cbExpected);
        return;
    }

    FILEGROUPDESCRIPTORA *pFGD = (FILEGROUPDESCRIPTORA *)GlobalLock(hGlobal);
    if (!pFGD)
    {
        RTTestIFailed("GlobalLock(FILEGROUPDESCRIPTORA) failed, lasterr=%u", GetLastError());
        return;
    }

    if (pFGD->cItems != RT_ELEMENTS(g_aFiles))
    {
        RTTestIFailed("FILEGROUPDESCRIPTORA cItems=%u, expected %u",
                      (unsigned)pFGD->cItems, (unsigned)RT_ELEMENTS(g_aFiles));
        GlobalUnlock(hGlobal);
        return;
    }

    for (unsigned i = 0; i < RT_ELEMENTS(g_aFiles); i++)
    {
        FILEDESCRIPTORA const *pFD = &pFGD->fgd[i];

        RTTestIPrintf(RTTESTLVL_ALWAYS, "Verifying FILEDESCRIPTORA item #%u\n", i);
        RTTestIPrintf(RTTESTLVL_ALWAYS, "Verifying file #%u, original UTF-8 name: \"%s\"\n",
                      i, g_aFiles[i].pszNameUtf8);

        testPrintAnsiNameN("Verifying file actual FILEDESCRIPTORA name",
                           i, pFD->cFileName, RT_ELEMENTS(pFD->cFileName));

        if (RTStrNLen(pFD->cFileName, RT_ELEMENTS(pFD->cFileName)) == RT_ELEMENTS(pFD->cFileName))
            RTTestIFailed("file #%u: FILEDESCRIPTORA cFileName is not NUL-terminated", i);

        /*
         * Only assert exact A-name equality for the pure ASCII case.  For
         * non-ASCII names, the A format is inherently codepage-dependent and
         * should not be the Unicode correctness oracle.
         */
        if (i == 0 && strcmp(pFD->cFileName, g_aFiles[i].pszNameUtf8) != 0)
            RTTestIFailed("file #%u: FILEDESCRIPTORA ASCII cFileName mismatch: got \"%s\", expected \"%s\"",
                          i, pFD->cFileName, g_aFiles[i].pszNameUtf8);

        if (!(pFD->dwFlags & FD_ATTRIBUTES))
            RTTestIFailed("file #%u: FILEDESCRIPTORA FD_ATTRIBUTES is not set", i);

        if (!(pFD->dwFlags & FD_FILESIZE))
            RTTestIFailed("file #%u: FILEDESCRIPTORA FD_FILESIZE is not set", i);

#ifdef FD_UNICODE
        if (pFD->dwFlags & FD_UNICODE)
            RTTestIFailed("file #%u: FILEDESCRIPTORA unexpectedly has FD_UNICODE set", i);
#endif

        if (pFD->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            RTTestIFailed("file #%u: FILEDESCRIPTORA unexpectedly marked as directory", i);

        char szExpected[1024];
        uint64_t const cbExpectedFile = testGetExpectedFileContent(i, szExpected, sizeof(szExpected));
        uint64_t const cbActualFile   = ((uint64_t)pFD->nFileSizeHigh << 32) | pFD->nFileSizeLow;

        if (cbActualFile != cbExpectedFile)
            RTTestIFailed("file #%u: FILEDESCRIPTORA file size %RU64, expected %RU64",
                          i, cbActualFile, cbExpectedFile);
    }

    GlobalUnlock(hGlobal);
}


static void testCheckFileDescriptorA(ShClWinDataObject *pDataObj)
{
    AssertPtrReturnVoid(pDataObj);

    RTTestISub("IDataObject / CFSTR_FILEDESCRIPTORA");

    CLIPFORMAT const cfFileDescriptorA = (CLIPFORMAT)RegisterClipboardFormat(CFSTR_FILEDESCRIPTORA);
    if (!cfFileDescriptorA)
    {
        RTTestIFailed("RegisterClipboardFormat(CFSTR_FILEDESCRIPTORA) failed, lasterr=%u",
                      GetLastError());
        return;
    }

    FORMATETC FormatEtc;
    RT_ZERO(FormatEtc);
    FormatEtc.cfFormat = cfFileDescriptorA;
    FormatEtc.dwAspect = DVASPECT_CONTENT;
    FormatEtc.lindex   = -1;
    FormatEtc.tymed    = TYMED_HGLOBAL;

    HRESULT hrc = pDataObj->QueryGetData(&FormatEtc);
    if (hrc != S_OK)
    {
        RTTestIFailed("QueryGetData(CFSTR_FILEDESCRIPTORA) returned %Rhrc, expected S_OK",
                      hrc);
        return;
    }

    STGMEDIUM Medium;
    RT_ZERO(Medium);

    hrc = pDataObj->GetData(&FormatEtc, &Medium);
    if (hrc != S_OK)
    {
        RTTestIFailed("GetData(CFSTR_FILEDESCRIPTORA) returned %Rhrc, expected S_OK",
                      hrc);
        testReleaseStgMedium(&Medium);
        return;
    }

    if (Medium.tymed != TYMED_HGLOBAL)
        RTTestIFailed("CFSTR_FILEDESCRIPTORA tymed %#x, expected TYMED_HGLOBAL",
                      Medium.tymed);

    if (!Medium.hGlobal)
        RTTestIFailed("CFSTR_FILEDESCRIPTORA returned NULL HGLOBAL");
    else
        testCheckFileGroupDescriptorA(Medium.hGlobal);

    testReleaseStgMedium(&Medium);
}


static void testCheckFileContentsStream(unsigned i, IStream *pStream)
{
    AssertPtrReturnVoid(pStream);
    AssertReturnVoid(i < RT_ELEMENTS(g_aFiles));

    char szExpected[1024];
    size_t const cbExpected = testGetExpectedFileContent(i, szExpected, sizeof(szExpected));

    STATSTG StatStg;
    RT_ZERO(StatStg);

    HRESULT hrc = pStream->Stat(&StatStg, STATFLAG_NONAME);
    if (hrc != S_OK)
        RTTestIFailed("file #%u: IStream::Stat returned %Rhrc, expected S_OK", i, hrc);
    else if ((uint64_t)StatStg.cbSize.QuadPart != cbExpected)
        RTTestIFailed("file #%u: IStream size %RU64, expected %zu",
                      i, (uint64_t)StatStg.cbSize.QuadPart, cbExpected);

    char abBuf[2048];
    RT_ZERO(abBuf);

    if (cbExpected > sizeof(abBuf))
    {
        RTTestIFailed("file #%u: expected payload too large for test buffer: %zu > %zu",
                      i, cbExpected, sizeof(abBuf));
        return;
    }

    ULONG cbRead = 0;
    hrc = pStream->Read(abBuf, sizeof(abBuf), &cbRead);
    if (hrc != S_OK && hrc != S_FALSE)
        RTTestIFailed("file #%u: IStream::Read returned %Rhrc, expected S_OK or S_FALSE",
                      i, hrc);

    RTTestIPrintf(RTTESTLVL_ALWAYS, "CFSTR_FILECONTENTS item #%u read %RU32 bytes: \"%.*s\"\n",
                  i, (uint32_t)cbRead, (int)cbRead, abBuf);

    if (cbRead != cbExpected)
        RTTestIFailed("file #%u: IStream::Read read %RU32 bytes, expected %zu",
                      i, (uint32_t)cbRead, cbExpected);
    else if (memcmp(abBuf, szExpected, cbExpected) != 0)
        RTTestIFailed("file #%u: IStream payload mismatch", i);

    ULONG cbReadAgain = ~0U;
    hrc = pStream->Read(abBuf, sizeof(abBuf), &cbReadAgain);
    if (hrc != S_OK && hrc != S_FALSE)
        RTTestIFailed("file #%u: second EOF IStream::Read returned %Rhrc, expected S_OK or S_FALSE",
                      i, hrc);

    if (cbReadAgain != 0)
        RTTestIFailed("file #%u: second EOF IStream::Read returned %RU32 bytes, expected 0",
                      i, (uint32_t)cbReadAgain);
}


static void testCheckFileContents(ShClWinDataObject *pDataObj)
{
    AssertPtrReturnVoid(pDataObj);

    RTTestISub("IDataObject / CFSTR_FILECONTENTS");

    CLIPFORMAT const cfFileContents = (CLIPFORMAT)RegisterClipboardFormat(CFSTR_FILECONTENTS);
    if (!cfFileContents)
    {
        RTTestIFailed("RegisterClipboardFormat(CFSTR_FILECONTENTS) failed, lasterr=%u",
                      GetLastError());
        return;
    }

    for (unsigned i = 0; i < RT_ELEMENTS(g_aFiles); i++)
    {
        RTTestIPrintf(RTTESTLVL_ALWAYS,
                      "Requesting CFSTR_FILECONTENTS item #%u: \"%s\"\n",
                      i, g_aFiles[i].pszNameUtf8);

        FORMATETC FormatEtc;
        RT_ZERO(FormatEtc);
        FormatEtc.cfFormat = cfFileContents;
        FormatEtc.dwAspect = DVASPECT_CONTENT;
        FormatEtc.lindex   = (LONG)i;
        FormatEtc.tymed    = TYMED_ISTREAM;

        HRESULT hrc = pDataObj->QueryGetData(&FormatEtc);
        if (hrc != S_OK)
        {
            RTTestIFailed("file #%u: QueryGetData(CFSTR_FILECONTENTS) returned %Rhrc, expected S_OK",
                          i, hrc);
            continue;
        }

        STGMEDIUM Medium;
        RT_ZERO(Medium);

        hrc = pDataObj->GetData(&FormatEtc, &Medium);
        if (hrc != S_OK)
        {
            RTTestIFailed("file #%u: GetData(CFSTR_FILECONTENTS) returned %Rhrc, expected S_OK",
                          i, hrc);
            testReleaseStgMedium(&Medium);
            continue;
        }

        if (Medium.tymed != TYMED_ISTREAM)
            RTTestIFailed("file #%u: CFSTR_FILECONTENTS tymed %#x, expected TYMED_ISTREAM",
                          i, Medium.tymed);

        if (!Medium.pstm)
            RTTestIFailed("file #%u: CFSTR_FILECONTENTS returned NULL IStream", i);
        else
            testCheckFileContentsStream(i, Medium.pstm);

        testReleaseStgMedium(&Medium);
    }
}


static void testFileDescriptorW(void)
{
    RTTestISub("IDataObject / CFSTR_FILEDESCRIPTORW");

    /*
     * Keep Ctx alive until after pDataObj has been released.  The data object
     * stores the callback context pointer passed to Init().
     */
    TESTCTX Ctx;
    RT_ZERO(Ctx);

    char szTempDir[RTPATH_MAX] = "";
    char aaszFilePaths[RT_ELEMENTS(g_aFiles)][RTPATH_MAX];
    const char *apszRoots[RT_ELEMENTS(g_aFiles)];
    RT_ZERO(aaszFilePaths);
    RT_ZERO(apszRoots);

    PSHCLTRANSFER pTransfer = NULL;
    ShClWinDataObject *pDataObj = NULL;

    STGMEDIUM StgMedium;
    RT_ZERO(StgMedium);

    int rc = VINF_SUCCESS;

    for (;;)
    {
        rc = testCreateTempDir(szTempDir, sizeof(szTempDir));
        if (RT_FAILURE(rc))
            break;

        RTTestIPrintf(RTTESTLVL_ALWAYS, "Temporary test directory: %s\n", szTempDir);

        for (unsigned i = 0; i < RT_ELEMENTS(g_aFiles); i++)
        {
            testPrintFileUnderTest(i);

            rc = testCreateFile(szTempDir,
                                i,
                                aaszFilePaths[i],
                                sizeof(aaszFilePaths[i]));
            if (RT_FAILURE(rc))
                break;

            apszRoots[i] = aaszFilePaths[i];

            RTTestIPrintf(RTTESTLVL_ALWAYS, "Created test file #%u: %s\n",
                          i, aaszFilePaths[i]);
        }

        if (RT_FAILURE(rc))
            break;

        rc = testCreateTransfer(apszRoots, RT_ELEMENTS(g_aFiles), &pTransfer);
        if (RT_FAILURE(rc))
            break;

        Ctx.pTransfer = pTransfer;

        ShClWinDataObject::CALLBACKS Callbacks;
        RT_ZERO(Callbacks);
        Callbacks.pfnTransferBegin = testTransferBegin;
        Callbacks.pfnTransferEnd   = testTransferEnd;

        pDataObj = new ShClWinDataObject();
        if (!pDataObj)
        {
            RTTestIFailed("new ShClWinDataObject failed");
            rc = VERR_NO_MEMORY;
            break;
        }

        pDataObj->AddRef();

        rc = pDataObj->Init((PSHCLCONTEXT)&Ctx, &Callbacks);
        if (RT_FAILURE(rc))
        {
            RTTESTI_CHECK_RC_OK(rc);
            break;
        }

        CLIPFORMAT const cfFileDescriptorW = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
        if (!cfFileDescriptorW)
        {
            RTTestIFailed("RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW) failed, lasterr=%u",
                          GetLastError());
            rc = VERR_INTERNAL_ERROR;
            break;
        }

        FORMATETC FormatEtc;
        RT_ZERO(FormatEtc);
        FormatEtc.cfFormat = cfFileDescriptorW;
        FormatEtc.dwAspect = DVASPECT_CONTENT;
        FormatEtc.lindex   = -1;
        FormatEtc.tymed    = TYMED_HGLOBAL;

        HRESULT hrc = pDataObj->QueryGetData(&FormatEtc);
        if (hrc != S_OK)
        {
            RTTestIFailed("QueryGetData(CFSTR_FILEDESCRIPTORW) returned %Rhrc, expected S_OK", hrc);
            break;
        }

        hrc = pDataObj->GetData(&FormatEtc, &StgMedium);
        if (hrc != S_OK)
        {
            RTTestIFailed("GetData(CFSTR_FILEDESCRIPTORW) returned %Rhrc, expected S_OK", hrc);
            break;
        }

        if (StgMedium.tymed != TYMED_HGLOBAL)
            RTTestIFailed("GetData returned tymed %#x, expected TYMED_HGLOBAL",
                          StgMedium.tymed);

        if (!StgMedium.hGlobal)
            RTTestIFailed("GetData returned a NULL HGLOBAL");

        if (StgMedium.tymed == TYMED_HGLOBAL && StgMedium.hGlobal)
            testCheckFileGroupDescriptorW(StgMedium.hGlobal);

        testCheckFileDescriptorA(pDataObj);

        /*
         * Keep CFSTR_FILECONTENTS last.  Consuming the final stream can
         * transition the transfer/data-object state to Completed.
         */
        testCheckFileContents(pDataObj);

        break;
    }

    /*
     * Cleanup.
     */
    testReleaseStgMedium(&StgMedium);

    if (pDataObj)
    {
        pDataObj->Release();
        pDataObj = NULL;
    }

    if (pTransfer)
    {
        ShClWinTransferDestroy(NULL /* pCtx */, pTransfer);

        int rc2 = ShClTransferDestroy(pTransfer);
        RTTESTI_CHECK_RC_OK(rc2);

        pTransfer = NULL;
        Ctx.pTransfer = NULL;
    }

    if (szTempDir[0])
    {
        int rc2 = RTDirRemoveRecursive(szTempDir, RTDIRRMREC_F_CONTENT_AND_DIR);
        RTTESTI_CHECK_RC_OK(rc2);
    }
}


int main(int argc, char **argv)
{
    RT_NOREF(argc, argv);

    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardDataObjectWin", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    RTTestBanner(hTest);

    bool const fMayPanic = RTAssertSetMayPanic(false);
    bool const fQuiet    = RTAssertSetQuiet(true);

    testFileDescriptorW();

    RTAssertSetQuiet(fQuiet);
    RTAssertSetMayPanic(fMayPanic);

    return RTTestSummaryAndDestroy(hTest);
}
