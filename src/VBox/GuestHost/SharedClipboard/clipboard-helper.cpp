/* $Id: clipboard-helper.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard: Helper functions.
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

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD

#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include <iprt/formats/bmp.h>

#include <VBox/log.h>
#include <VBox/GuestHost/clipboard-helper.h>


/*********************************************************************************************************************************
*   Implementation                                                                                                               *
*********************************************************************************************************************************/

int ShClHlpUtf16LenUtf8(PCRTUTF16 pcwszSrc, size_t cwcSrc, size_t *pcbLenSansTerm)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbLenSansTerm, VERR_INVALID_POINTER);

    size_t cbLenSansTerm = 0;
    int rc = RTUtf16CalcUtf8LenEx(pcwszSrc, cwcSrc, &cbLenSansTerm);
    if (RT_SUCCESS(rc))
        *pcbLenSansTerm = cbLenSansTerm;
    return rc;
}

int ShClHlpUtf8ValidateExact(const char *pchSrc, size_t cbSrc, size_t *pcchText)
{
    AssertPtrReturn(pchSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pcchText, VERR_INVALID_POINTER);
    *pcchText = 0;

    bool const fTerminated = cbSrc > 0 && pchSrc[cbSrc - 1] == '\0';
    uint32_t fFlags = RTSTR_VALIDATE_ENCODING_EXACT_LENGTH;
    if (fTerminated)
        fFlags |= RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED;

    int const rc = RTStrValidateEncodingEx(pchSrc, cbSrc, fFlags);
    if (RT_SUCCESS(rc))
        *pcchText = cbSrc - (fTerminated ? 1 : 0);
    return rc;
}

int ShClHlpUtf16DupExact(PCRTUTF16 pwszSrc, size_t cwcSrc, PRTUTF16 *ppwszDst)
{
    AssertPtrReturn(pwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(ppwszDst, VERR_INVALID_POINTER);
    *ppwszDst = NULL;
    AssertReturn(cwcSrc, VERR_INVALID_PARAMETER);

    size_t cwcText = 0;
    int rc = RTUtf16LenAndValidateEncoding(pwszSrc, cwcSrc, 0 /* fFlags */, NULL /* pcuc */, &cwcText);
    if (RT_FAILURE(rc))
        return rc;
    if (cwcText < cwcSrc - 1)
        return VERR_BUFFER_UNDERFLOW;
    if (cwcText >= RTSTR_MAX / sizeof(RTUTF16))
        return VERR_TOO_MUCH_DATA;

    PRTUTF16 pwszDst = RTUtf16Alloc((cwcText + 1) * sizeof(RTUTF16));
    if (!pwszDst)
        return VERR_NO_UTF16_MEMORY;
    memcpy(pwszDst, pwszSrc, cwcText * sizeof(RTUTF16));
    pwszDst[cwcText] = '\0';
    *ppwszDst = pwszDst;
    return VINF_SUCCESS;
}

int ShClHlpConvUtf16CRLFToUtf8LF(PCRTUTF16 pwszSrc, size_t cwcSrc, char *pszBuf, size_t cbBuf, size_t *pcbLen)
{
    AssertPtrReturn(pwszSrc,  VERR_INVALID_POINTER);
    AssertPtrReturn(pszBuf,   VERR_INVALID_POINTER);
    AssertReturn(   cbBuf,    VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbLen,   VERR_INVALID_POINTER);

#if 0
    /*
     * Do a two step conversion.  First, do the CRLF -> LF bit,
     * then do the UTF-16 to UTF-8.
     */
    /* Step 1: */
    size_t   cchTmp  = 0;
    int rc = ShClHlpUtf16CRLFToLFLen(pwszSrc, cwcSrc, &cchTmp);
    if (RT_SUCCESS(rc))
    {
        cchTmp++; /* Add space for terminator. */

        /** @todo r=bird: cchTmp is the entirely wrong temporary buffer length! */
        PRTUTF16 pwszTmp = (PRTUTF16)RTMemTmpAllocZ(cchTmp * sizeof(RTUTF16));
        if (pwszTmp)
        {
            rc = ShClHlpConvUtf16CRLFToLF(pwszSrc, cwcSrc, pwszTmp, cchTmp);
            if (RT_SUCCESS(rc))
            {
                /* Step 2: (we must skip the BOM in the temporary string here) */
                size_t cbLenSansTerm = 0;
                rc = RTUtf16ToUtf8Ex(pwszTmp + 1, cchTmp - 1, &pszBuf, cbBuf, &cbLenSansTerm);
                if (RT_SUCCESS(rc))
                    *pcbLen = cbLenSansTerm;
            }

            RTMemTmpFree(reinterpret_cast<void *>(pwszTmp));
        }
        else
            rc = VERR_NO_MEMORY;
    }
    return rc;
#else
    /*
     * Just do the converting.
     */
    PCRTUTF16 const pwszEnd = &pwszSrc[cwcSrc];
    size_t          offDst  = 0;
    while ((uintptr_t)pwszSrc < (uintptr_t)pwszEnd)
    {
        RTUNICP uc;
        int rc = RTUtf16GetCpEx(&pwszSrc, &uc);
        AssertRCReturn(rc, rc);
        if (!uc)
            break;

        if (   uc != VBOX_SHCL_CARRIAGERETURN
            || (uintptr_t)pwszSrc >= (uintptr_t)pwszEnd
            || *pwszSrc != VBOX_SHCL_LINEFEED)
        {
            size_t const cbCp = RTStrCpSize(uc);
            AssertReturn(offDst + cbCp < cbBuf, VERR_BUFFER_OVERFLOW);
            size_t const cbPut = RTStrPutCpRetLen(&pszBuf[offDst], uc);
            Assert(cbPut == cbCp); RT_NOREF(cbPut);
            offDst += cbCp;
        }
        else
        {
            AssertReturn(offDst + 1 < cbBuf, VERR_BUFFER_OVERFLOW);
            pszBuf[offDst++] = VBOX_SHCL_LINEFEED;
            pwszSrc++;
        }
    }

    AssertStmt(offDst < cbBuf, offDst = cbBuf - 1);
    pszBuf[offDst] = '\0';
    *pcbLen = offDst;
    return VINF_SUCCESS;
#endif
}

int ShClHlpConvUtf16CRLFToUtf8LFA(PCRTUTF16 pwszSrc, size_t cwcSrc, char **ppszDst, size_t *pcbLenSansTerm)
{
    AssertPtrReturn(pwszSrc, VERR_INVALID_POINTER);
    AssertPtr(ppszDst);
    AssertPtr(pcbLenSansTerm);

    /*
     * Calculate the UTF-8 length of the whole string, then rescan it looking
     * for CRLF pairs and reduce it accordingly.
     */
    size_t cchDst;
    int rc = RTUtf16CalcUtf8LenEx(pwszSrc, cwcSrc, &cchDst); /* (Will validate the UTF-16 encoding.) */
    if (RT_SUCCESS(rc))
    {
        /* Scan for CRLF pairs. */
        size_t cDosEols = 0;
        for (size_t i = 0; i < cwcSrc; i++)
        {
            RTUTF16 const wc = pwszSrc[i];
            if (wc != 0)
            {
                if (   wc == VBOX_SHCL_CARRIAGERETURN
                    && i + 1 < cwcSrc
                    && pwszSrc[i + 1] == VBOX_SHCL_LINEFEED)
                    cDosEols += 1;
            }
            else
            {
                cwcSrc = i;
                break;
            }
        }

        AssertReturn(cDosEols * 2 <= cchDst, VERR_BUFFER_UNDERFLOW); /* string must've been modified between the two scans... */
        cchDst -= cDosEols;

        /* Allocate output buffer. */
        char * const pszDst = (char *)RTMemAlloc(cchDst + 1 + 6); /* 6 is the max UTF-8 encoding length. */
        if (pszDst)
        {
            /*
             * Do the converting.
             */
            PCRTUTF16 const pwszEnd = &pwszSrc[cwcSrc];
            size_t          offDst  = 0;
            while ((uintptr_t)pwszSrc < (uintptr_t)pwszEnd)
            {
                RTUNICP uc;
                rc = RTUtf16GetCpEx(&pwszSrc, &uc);
                AssertRCBreak(rc);

                AssertBreakStmt(uc != 0, rc = VINF_BUFFER_UNDERFLOW);

                if (   uc != VBOX_SHCL_CARRIAGERETURN
                    || (uintptr_t)pwszSrc >= (uintptr_t)pwszEnd
                    || *pwszSrc != VBOX_SHCL_LINEFEED)
                {
                    offDst += RTStrPutCpRetLen(&pszDst[offDst], uc);
                    AssertBreakStmt(offDst <= cchDst, rc = VERR_BUFFER_OVERFLOW);
                }
                else
                {
                    pszDst[offDst++] = VBOX_SHCL_LINEFEED;
                    pwszSrc++;
                }
            }
            if (RT_SUCCESS(rc))
            {
                Assert(offDst <= cchDst + 6);
                pszDst[offDst] = '\0';

                *ppszDst        = pszDst;
                *pcbLenSansTerm = offDst;
            }
            else
                RTMemFree(pszDst);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    return rc;
}

int ShClHlpConvUtf16LFToCRLFA(PCRTUTF16 pcwszSrc, size_t cwcSrc,
                              PRTUTF16 *ppwszDst, size_t *pcwcDst)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(ppwszDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcwcDst,  VERR_INVALID_POINTER);

    size_t cwcDst;
    int rc = ShClHlpUtf16CalcNormalizedEolToCRLFLength(pcwszSrc, cwcSrc, &cwcDst);
    if (RT_SUCCESS(rc))
    {
        PRTUTF16 const pwszDst = (PRTUTF16)RTMemAllocZ((cwcDst + 1 /* Leave space for terminator */) * sizeof(RTUTF16));
        if (pwszDst)
        {
            rc = ShClHlpConvUtf16LFToCRLF(pcwszSrc, cwcSrc, pwszDst, cwcDst + 1);
            if (RT_SUCCESS(rc))
            {
                *ppwszDst = pwszDst;
                *pcwcDst  = cwcDst;
            }
            else
                RTMemFree(pwszDst);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int ShClHlpConvUtf8LFToUtf16CRLF(const char *pcszSrc, size_t cbSrc, PRTUTF16 *ppwszDst, size_t *pcwcDst)
{
    AssertPtrReturn(pcszSrc,  VERR_INVALID_POINTER);
    AssertPtrReturn(ppwszDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcwcDst,  VERR_INVALID_POINTER);

    /* Intermediate conversion to UTF-16. */
    size_t   cwcTmp;
    PRTUTF16 pwcTmp = NULL;
    int rc = RTStrToUtf16Ex(pcszSrc, cbSrc, &pwcTmp, 0, &cwcTmp);
    if (RT_SUCCESS(rc))
    {
        rc = ShClHlpConvUtf16LFToCRLFA(pwcTmp, cwcTmp, ppwszDst, pcwcDst);
        RTUtf16Free(pwcTmp);
    }

    return rc;
}

int ShClHlpConvLatin1LFToUtf16CRLF(const char *pszSrc, size_t cbSrc,
                                    PRTUTF16 *ppwszDst, size_t *pcwcDst)
{
    AssertPtrReturn(pszSrc,   VERR_INVALID_POINTER);
    AssertPtrReturn(ppwszDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcwcDst,  VERR_INVALID_POINTER);

    /*
     * Count the LF (& CR) codepoints that will be converted to CRLF.
     * We follow same logic as ShClHlpUtf16CalcNormalizedEolToCRLFLength.
     */
    size_t cToExpand = 0;
    for (size_t iSrc = 0; iSrc < cbSrc; ++iSrc)
    {
        char const ch = pszSrc[iSrc];
        if (ch != '\0')
        {
            /* Check for a single line feed: */
            if (   ch == VBOX_SHCL_LINEFEED
                && (iSrc == 0 || pszSrc[iSrc - 1] != VBOX_SHCL_CARRIAGERETURN))
                cToExpand += 1;
#ifdef RT_OS_DARWIN
            /* Check for a single carriage return (MacOS): */
            else if (   ch == VBOX_SHCL_CARRIAGERETURN
                     && (iSrc + 1 >= cbSrc || pszSrc[iSrc + 1] != VBOX_SHCL_LINEFEED))
                cToExpand += 1;
#endif
        }
        else
        {
            cbSrc = iSrc;
            break;
        }
    }

    /* Allocate output buffer. */
    size_t const   cwcDst  = cbSrc + cToExpand;
    PRTUTF16 const pwszDst = (PRTUTF16)RTMemAllocZ((cwcDst + 1 /* Leave space for the terminator */) * sizeof(RTUTF16));
    AssertPtrReturn(pwszDst, VERR_NO_MEMORY);

    /*
     * Do the conversion, bearing in mind that Latin-1 expands "naturally" to UTF-16.
     */
    size_t iDst = 0;
    for (size_t iSrc = 0; iSrc < cbSrc; ++iSrc)
    {
        Assert(iDst < cwcDst);
        unsigned char const uch = (unsigned char)pszSrc[iSrc];
        if (!(    (    uch == VBOX_SHCL_LINEFEED
                   && (iSrc == 0 || pszSrc[iSrc - 1] != VBOX_SHCL_CARRIAGERETURN))
#ifdef RT_OS_DARWIN
              ||  (    uch == VBOX_SHCL_CARRIAGERETURN
                   && (iSrc + 1 >= cbSrc || pszSrc[iSrc + 1] != VBOX_SHCL_LINEFEED))
#endif
             ))
            pwszDst[iDst++] = uch;
        else if (RT_LIKELY(iDst - iSrc < cToExpand))
        {
            pwszDst[iDst++] = VBOX_SHCL_CARRIAGERETURN;
            pwszDst[iDst++] = VBOX_SHCL_LINEFEED;
        }
        else
        {
            AssertMsgFailed(("EOL conversion count increased! iSrc=%#zx iDst=%#zx cToExpand=%#zx cbSrc=%#zx\n",
                             iSrc, iDst, cToExpand, cbSrc));
            pwszDst[iDst++] = uch;
        }
    }
    AssertMsg(iDst == cwcDst, ("EOL conversion count decreased! iDst=%#zx cwcDst=%#zx cbSrc=%#zx cToExpand=%#zx\n",
                               iDst, cwcDst, cbSrc, cToExpand));
    AssertStmt(iDst <= cwcDst, pwszDst[iDst = cwcDst] = '\0');  /* impossible, but be paranoid about it... */

    *ppwszDst = pwszDst;
    *pcwcDst  = iDst;
    return VINF_SUCCESS;
}

int ShClHlpConvUtf16ToUtf8HTML(PCRTUTF16 pcwszSrc, size_t cwcSrc, char **ppszDst, size_t *pcbDst)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertReturn   (cwcSrc,   VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppszDst,  VERR_INVALID_POINTER);
    AssertPtrReturn(pcbDst,   VERR_INVALID_POINTER);

/** @todo r=bird: This has no useful documentation or testcase,
 * so it's anyone's guess what input it is doing all that empty string
 * skipping for.  Yes, older firefox will put UTF-16 formatted text/html on
 * the X11 clipboard, but it seems to do that without any embedded zeros... */
    int rc = VINF_SUCCESS;

    size_t    cwTmp = cwcSrc;
    PCRTUTF16 pwTmp = pcwszSrc;

    char  *pchDst = NULL;
    size_t cbDst  = 0;

    size_t i = 0;
    while (i < cwTmp)
    {
        /* Find  zero symbol (end of string). */
/** @todo Use RTUtf16NLen? */
        for (; i < cwTmp && pcwszSrc[i] != 0; i++)
            ;

        /* Convert found string. */
        char  *psz = NULL;
        size_t cch = 0;
/** @todo r=bird: What on earth is going on with the output buffer size calculation here?!?
 * It ends up as zero for the first loop, but it will the UTF-16 units
 * preceeding the current string for the following loops... Doubt this ever
 * worked, though, I cannot understand who would put anything with multiple
 * strings onto clipboard in the first place... */
        rc = RTUtf16ToUtf8Ex(pwTmp, cwTmp, &psz, pwTmp - pcwszSrc, &cch);
        if (RT_FAILURE(rc))
            break;

        /* Append new substring. */
        char *pchNew = (char *)RTMemRealloc(pchDst, cbDst + cch + 1);
        if (!pchNew)
        {
            RTStrFree(psz);
            rc = VERR_NO_MEMORY;
            break;
        }

        pchDst = pchNew;
        memcpy(pchDst + cbDst, psz, cch + 1);

        RTStrFree(psz);

        cbDst += cch + 1;

        /* Skip zero symbols. */
        for (; i < cwTmp && pcwszSrc[i] == 0; i++)
            ;

        /* Remember start of string. */
        pwTmp += i;
    }

    if (RT_SUCCESS(rc))
    {
/** @todo r=bird: pchDst may be NULL here if if the input is one or more empty
 * strings. */
        *ppszDst = pchDst;
        *pcbDst  = cbDst;

        return VINF_SUCCESS;
    }

    RTMemFree(pchDst);

    return rc;
}

int ShClHlpUtf16CalcNormalizedEolToCRLFLength(PCRTUTF16 pcwszSrc, size_t cwSrc, size_t *pchLen)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pchLen, VERR_INVALID_POINTER);

    AssertMsgReturn(pcwszSrc[0] != VBOX_SHCL_UTF16_BOM_INVERSE,
                    ("Other endian UTF-16 not supported yet\n"), VERR_NOT_SUPPORTED);

    size_t cLen = 0;

    /* Don't copy the endian marker. */
    size_t i = pcwszSrc[0] == VBOX_SHCL_UTF16_BOM ? 1 : 0;

    /* Calculate the size of the destination text string. */
    /* Is this Utf16 or Utf16-LE? */
    for (; i < cwSrc; ++i, ++cLen)
    {
        /* Check for a single line feed */
        if (   pcwszSrc[i] == VBOX_SHCL_LINEFEED
            && (i == 0 || pcwszSrc[i - 1] != VBOX_SHCL_CARRIAGERETURN))
        {
            ++cLen;
        }
#ifdef RT_OS_DARWIN
        /* Check for a single carriage return (MacOS) */
        if (   pcwszSrc[i] == VBOX_SHCL_CARRIAGERETURN
            && (i + 1 >= cwSrc || pcwszSrc[i + 1] != VBOX_SHCL_LINEFEED))
        {
            ++cLen;
        }
#endif
        if (pcwszSrc[i] == 0)
        {
            /* Don't count this, as we do so below. */
            break;
        }
    }

    *pchLen = cLen;

    return VINF_SUCCESS;
}

int ShClHlpUtf16CRLFToLFLen(PCRTUTF16 pcwszSrc, size_t cwSrc, size_t *pcwcConverted)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtr(pcwcConverted);
    AssertMsgReturn(pcwszSrc[0] != VBOX_SHCL_UTF16_BOM_INVERSE,
                    ("Other endian UTF-16 not supported yet\n"), VERR_NOT_SUPPORTED);

    /* Do we need to add a BOM? */
    size_t cwcConverted = 0;
    if (pcwszSrc[0] == VBOX_SHCL_UTF16_BOM)
        cwcConverted = 0;
    else
        cwcConverted = 1;

    for (size_t iSrc = 0; iSrc < cwSrc; ++iSrc, ++cwcConverted)
    {
        if (   (iSrc + 1 < cwSrc)
            && (pcwszSrc[iSrc]     == VBOX_SHCL_CARRIAGERETURN)
            && (pcwszSrc[iSrc + 1] == VBOX_SHCL_LINEFEED))
            ++iSrc;
        else if (pcwszSrc[iSrc] == 0)
            break;
    }

    *pcwcConverted = cwcConverted;
    return VINF_SUCCESS;
}

int ShClHlpConvUtf16LFToCRLF(PCRTUTF16 pcwszSrc, size_t cwcSrc, PRTUTF16 pu16Dst, size_t cwcDst)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pu16Dst, VERR_INVALID_POINTER);
    AssertReturn(cwcDst, VERR_INVALID_PARAMETER);

    AssertMsgReturn(pcwszSrc[0] != VBOX_SHCL_UTF16_BOM_INVERSE,
                    ("Other endian UTF-16 not supported yet\n"), VERR_NOT_SUPPORTED);

    /* Don't copy the endian marker. */
    size_t      offDst = 0;
    for (size_t offSrc = pcwszSrc[0] == VBOX_SHCL_UTF16_BOM ? 1 : 0; offSrc < cwcSrc; ++offSrc, ++offDst)
    {
        /* Ensure more output space: */
        if (offDst < cwcDst) { /* likely */ }
        else return VERR_BUFFER_OVERFLOW;

        /* Don't copy the null byte, as we add it below. */
        if (pcwszSrc[offSrc] == 0)
            break;

        /* Check for newlines not preceded by carriage return: "\n" -> "\r\n";  but not "\r\n" to "\r\r\n"! */
        if (   pcwszSrc[offSrc] == VBOX_SHCL_LINEFEED
            && (offSrc == 0 || pcwszSrc[offSrc - 1] != VBOX_SHCL_CARRIAGERETURN))
        {
            pu16Dst[offDst++] = VBOX_SHCL_CARRIAGERETURN;

            /* Ensure sufficient output space: */
            if (offDst < cwcDst) { /* likely */ }
            else return VERR_BUFFER_OVERFLOW;
        }
#ifdef RT_OS_DARWIN
        /* Check for a carriage return not followed by newline (MacOS): "\r" -> "\n\r";  but not "\r\n" to "\r\n\n"! */
        else if (   pcwszSrc[offSrc] == VBOX_SHCL_CARRIAGERETURN
                 && (offSrc + 1 >= cwcSrc || pcwszSrc[offSrc + 1] != VBOX_SHCL_LINEFEED))
        {
            pu16Dst[offDst++] = VBOX_SHCL_CARRIAGERETURN;

            /* Ensure more output space: */
            if (offDst < cwcDst) { /* likely */ }
            else return VERR_BUFFER_OVERFLOW;

            /* Add line feed. */
            pu16Dst[offDst] = VBOX_SHCL_LINEFEED;
            continue;
        }
#endif
        pu16Dst[offDst] = pcwszSrc[offSrc];
    }

    /* Add terminator. */
    if (offDst < cwcDst)
    {
        pu16Dst[offDst] = 0;
        return VINF_SUCCESS;
    }
    return VERR_BUFFER_OVERFLOW;
}

int ShClHlpConvUtf16CRLFToLF(PCRTUTF16 pcwszSrc, size_t cwcSrc, PRTUTF16 pwszDst, size_t cwcDst)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pwszDst,  VERR_INVALID_POINTER);
    AssertReturn(cwcDst,      VERR_INVALID_PARAMETER);

    AssertMsgReturn(pcwszSrc[0] != VBOX_SHCL_UTF16_BOM_INVERSE,
                    ("Other endian UTF-16 not supported yet\n"), VERR_NOT_SUPPORTED);

    /* Prepend the Utf16 byte order marker if it is missing. */
    size_t offDst;
    if (pcwszSrc[0] == VBOX_SHCL_UTF16_BOM)
        offDst = 0;
    else
    {
        pwszDst[0] = VBOX_SHCL_UTF16_BOM;
        offDst = 1;
    }

    for (size_t i = 0; i < cwcSrc; ++i, ++offDst)
    {
        if (pcwszSrc[i] == 0)
            break;

        if (offDst == cwcDst)
            return VERR_BUFFER_OVERFLOW;

        if (   i + 1 < cwcSrc
            && pcwszSrc[i]     == VBOX_SHCL_CARRIAGERETURN
            && pcwszSrc[i + 1] == VBOX_SHCL_LINEFEED)
            ++i;
        pwszDst[offDst] = pcwszSrc[i];
    }

    /* Add terminating zero. */
    if (offDst == cwcDst)
        return VERR_BUFFER_OVERFLOW;
    pwszDst[offDst] = 0;

    return VINF_SUCCESS;
}

int ShClHlpDibToBmp(const void *pvSrc, size_t cbSrc, void **ppvDst, size_t *pcbDst)
{
    AssertPtrReturn(ppvDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbDst, VERR_INVALID_POINTER);

    *ppvDst = NULL;
    *pcbDst = 0;

    AssertPtrReturn(pvSrc,  VERR_INVALID_POINTER);
    AssertReturn(cbSrc,     VERR_INVALID_PARAMETER);

    PBMPWIN3XINFOHDR coreHdr = (PBMPWIN3XINFOHDR)pvSrc;
    /** @todo Support all the many versions of the DIB headers. */
    if (   cbSrc < sizeof(BMPWIN3XINFOHDR)
        || RT_LE2H_U32(coreHdr->cbSize) < sizeof(BMPWIN3XINFOHDR)
        || RT_LE2H_U32(coreHdr->cbSize) != sizeof(BMPWIN3XINFOHDR))
        return VERR_INVALID_PARAMETER;

    uint32_t const cx            = RT_LE2H_U32(coreHdr->uWidth);
    uint32_t const uHeight      = RT_LE2H_U32(coreHdr->uHeight);
    uint16_t const cPlanes      = RT_LE2H_U16(coreHdr->cPlanes);
    uint16_t const cBits        = RT_LE2H_U16(coreHdr->cBits);
    uint32_t const uCompression = RT_LE2H_U32(coreHdr->enmCompression);
    uint32_t const cbSizeImage  = RT_LE2H_U32(coreHdr->cbSizeImage);
    uint32_t const cClrUsed     = RT_LE2H_U32(coreHdr->cClrUsed);

    if (   !cx
        || cx & RT_BIT_32(31)
        || !uHeight
        || uHeight == RT_BIT_32(31)
        || cPlanes != 1)
        return VERR_INVALID_PARAMETER;

    uint32_t const cy = uHeight & RT_BIT_32(31) ? (~uHeight) + 1 : uHeight;
    uint32_t cMasks;
    switch (uCompression)
    {
        case BMP_COMPRESSION_TYPE_NONE:
            if (   cBits != 1
                && cBits != 4
                && cBits != 8
                && cBits != 16
                && cBits != 24
                && cBits != 32)
                return VERR_INVALID_PARAMETER;
            cMasks = 0;
            break;

        case BMP_COMPRESSION_TYPE_BITFIELDS:
            if (cBits != 16 && cBits != 32)
                return VERR_INVALID_PARAMETER;
            cMasks = 3;
            break;

        case BMP_COMPRESSION_TYPE_ALPHABITFIELDS:
            if (cBits != 16 && cBits != 32)
                return VERR_INVALID_PARAMETER;
            cMasks = 4;
            break;

        default:
            /* Compressed DIBs require format-specific validation before exposing them to clipboard consumers. */
            return VERR_NOT_SUPPORTED;
    }

    uint32_t cColors = cClrUsed;
    if (cBits <= 8)
    {
        uint32_t const cMaxColors = RT_BIT_32(cBits);
        if (!cColors)
            cColors = cMaxColors;
        else if (cColors > cMaxColors)
            return VERR_INVALID_PARAMETER;
    }

    uint64_t const offPixelDib = (uint64_t)sizeof(BMPWIN3XINFOHDR)
                               + (uint64_t)cMasks  * sizeof(uint32_t)
                               + (uint64_t)cColors * sizeof(uint32_t);
    if (offPixelDib > cbSrc)
        return VERR_INVALID_PARAMETER;

    uint64_t const cbRow = (((uint64_t)cx * cBits + 31) / 32) * 4;
    if (cy > UINT64_MAX / cbRow)
        return VERR_TOO_MUCH_DATA;
    uint64_t const cbPixels = cbRow * cy;
    if (   cbPixels > cbSrc - (size_t)offPixelDib
        || (cbSizeImage && cbSizeImage != cbPixels))
        return VERR_INVALID_PARAMETER;

    if (cbSrc > UINT32_MAX - sizeof(BMPFILEHDR))
        return VERR_TOO_MUCH_DATA;
    size_t const cbDst = sizeof(BMPFILEHDR) + cbSrc;

    void *pvDst = RTMemAllocZ(cbDst);
    if (!pvDst)
        return VERR_NO_MEMORY;

    PBMPFILEHDR fileHdr = (PBMPFILEHDR)pvDst;

    fileHdr->uType       = BMP_HDR_MAGIC;
    fileHdr->cbFileSize  = (uint32_t)RT_H2LE_U32(cbDst);
    fileHdr->Reserved1   = 0;
    fileHdr->Reserved2   = 0;
    fileHdr->offBits     = (uint32_t)RT_H2LE_U32(sizeof(BMPFILEHDR) + offPixelDib);

    memcpy((uint8_t *)pvDst + sizeof(BMPFILEHDR), pvSrc, cbSrc);

    *ppvDst = pvDst;
    *pcbDst = cbDst;

    return VINF_SUCCESS;
}

int ShClHlpBmpGetDib(const void *pvSrc, size_t cbSrc, const void **ppvDst, size_t *pcbDst)
{
    AssertPtrReturn(pvSrc,  VERR_INVALID_POINTER);
    AssertReturn(cbSrc,     VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppvDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbDst, VERR_INVALID_POINTER);

    PBMPFILEHDR pBmpHdr = (PBMPFILEHDR)pvSrc;
    if (   cbSrc < sizeof(BMPFILEHDR)
        || pBmpHdr->uType != BMP_HDR_MAGIC
        || RT_LE2H_U32(pBmpHdr->cbFileSize) != cbSrc)
    {
        return VERR_INVALID_PARAMETER;
    }

    *ppvDst = ((uint8_t *)pvSrc) + sizeof(BMPFILEHDR);
    *pcbDst = cbSrc - sizeof(BMPFILEHDR);

    return VINF_SUCCESS;
}


/**
 * Frees a buffer retrned by a ShClHlpConvXxxx or ShClHlpDibToBmp function.
 *
 * @returns VBox status code.
 * @param   pvBuf The buffer to free. Must have been returned by some
 *                ShClhlpConvXxxx function.
 * @param   cbBuf The buffer size returned by the ShClhlpConvXxxx function.
 */
VBGH_DECL(void) ShClHlpFreeBuf(void *pvBuf, size_t cbBuf)
{
    RTMemFree(pvBuf);
    RT_NOREF(cbBuf);
}


/**
 * Converts a clipboard source value to a printable string.
 *
 * @returns Printable source name.
 * @param   uSource             Clipboard source value.
 */
const char *ShClHlpSourceToString(uint32_t uSource)
{
    switch (uSource)
    {
        case VBOX_SHCL_CLIPBOARD_SOURCE_HOST:   return "host";
        case VBOX_SHCL_CLIPBOARD_SOURCE_GUEST:  return "guest";
        case VBOX_SHCL_CLIPBOARD_SOURCE_REMOTE: return "remote";
        case VBOX_SHCL_CLIPBOARD_SOURCE_CUSTOM: return "custom";
        default:                                break;
    }

    AssertFailedReturn("unknown");
}


/**
 * Converts a clipboard mode value to a printable string.
 *
 * @returns Printable mode name.
 * @param   uMode               Clipboard mode value.
 */
const char *ShClHlpModeToString(uint32_t uMode)
{
    switch (uMode)
    {
        case VBOX_SHCL_CLIPBOARD_MODE_DISABLED:      return "disabled";
        case VBOX_SHCL_CLIPBOARD_MODE_HOST_TO_GUEST: return "host-to-guest";
        case VBOX_SHCL_CLIPBOARD_MODE_GUEST_TO_HOST: return "guest-to-host";
        case VBOX_SHCL_CLIPBOARD_MODE_BIDIRECTIONAL: return "bidirectional";
        default:                                     break;
    }

    AssertFailedReturn("unknown");
}


/**
 * Converts a clipboard transfer state value to a printable string.
 *
 * @returns Printable transfer state name.
 * @param   uState              Clipboard transfer state value.
 */
const char *ShClHlpTransferStateToString(uint32_t uState)
{
    switch (uState)
    {
        case VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_ADDED:       return "added";
        case VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_REMOVED:     return "removed";
        case VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_IN_PROGRESS: return "in-progress";
        case VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_INTERACTION: return "interaction";
        case VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_COMPLETED:   return "completed";
        case VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_CANCELED:    return "canceled";
        case VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_FAILED:      return "failed";
        default:                                            break;
    }

    AssertFailedReturn("unknown");
}


/**
 * Converts a Main API clipboard event type value to a printable string.
 *
 * @returns Printable event type name.
 * @param   uEventType          Main API VBoxEventType value.
 */
const char *ShClHlpVBoxEventTypeToString(uint32_t uEventType)
{
    switch (uEventType)
    {
        case 72:  return "OnClipboardModeChanged";
        case 104: return "OnClipboardFileTransferModeChanged";
        case 122: return "OnClipboardError";
        case 126: return "OnClipboardSourceChanged";
        case 127: return "OnClipboardFormatChanged";
        case 128: return "OnClipboardDataChanged";
        case 129: return "OnClipboardTransfer";
        case 130: return "OnClipboardDataRequested";
        default:  break;
    }

    AssertFailedReturn("unknown");
}


/**
 * Parses a clipboard sharing mode value.
 *
 * @returns true if the value was parsed successfully, false otherwise.
 * @param   pszMode             String value to parse.
 * @param   puMode              Where to return the parsed mode.
 */
bool ShClHlpModeFromString(const char *pszMode, uint32_t *puMode)
{
    AssertPtrReturn(pszMode, false);
    AssertPtrReturn(puMode, false);

    if (!RTStrICmp(pszMode, "disabled"))
        *puMode = VBOX_SHCL_CLIPBOARD_MODE_DISABLED;
    else if (!RTStrICmp(pszMode, "hosttoguest"))
        *puMode = VBOX_SHCL_CLIPBOARD_MODE_HOST_TO_GUEST;
    else if (!RTStrICmp(pszMode, "guesttohost"))
        *puMode = VBOX_SHCL_CLIPBOARD_MODE_GUEST_TO_HOST;
    else if (!RTStrICmp(pszMode, "bidirectional"))
        *puMode = VBOX_SHCL_CLIPBOARD_MODE_BIDIRECTIONAL;
    else
        return false;
    return true;
}


/**
 * Checks whether a MIME type represents text.
 *
 * @returns true if the MIME type is a text type, false otherwise.
 * @param   pszMimeType         MIME type to check.
 */
bool ShClHlpIsTextMimeType(const char *pszMimeType)
{
    AssertPtrReturn(pszMimeType, false);

    return RTStrNICmp(pszMimeType, RT_STR_TUPLE("text/")) == 0;
}


/**
 * Checks whether a MIME type is UTF-8 encoded text.
 *
 * @returns true if the MIME type is UTF-8 text, false otherwise.
 * @param   pszMimeType         MIME type to check.
 */
bool ShClHlpIsUtf8TextMimeType(const char *pszMimeType)
{
    AssertPtrReturn(pszMimeType, false);

    return    !RTStrICmp(pszMimeType, "UTF8_STRING")
           || (   ShClHlpIsTextMimeType(pszMimeType)
               && RTStrIStr(pszMimeType, "charset=utf-8"));
}


/**
 * Checks whether a MIME type is UTF-16 encoded text.
 *
 * @returns true if the MIME type is UTF-16 text, false otherwise.
 * @param   pszMimeType         MIME type to check.
 */
bool ShClHlpIsUtf16TextMimeType(const char *pszMimeType)
{
    AssertPtrReturn(pszMimeType, false);

    return    ShClHlpIsTextMimeType(pszMimeType)
           && RTStrIStr(pszMimeType, "charset=utf-16");
}


/**
 * Checks whether clipboard data is multiline text.
 *
 * @returns true if the payload is text and contains more than one line.
 * @param   pszMimeType         MIME type of the payload.
 * @param   pbData              Payload bytes.
 * @param   cbData              Number of payload bytes.
 */
bool ShClHlpIsMultilineText(const char *pszMimeType, const uint8_t *pbData, size_t cbData)
{
    if (!ShClHlpIsTextMimeType(pszMimeType))
        return false;
    if (!cbData)
        return false;
    AssertPtrReturn(pbData, false);

    for (size_t i = 0; i < cbData; i++)
    {
        if (pbData[i] == '\r' || pbData[i] == '\n')
        {
            if (pbData[i] == '\r' && i + 1 < cbData && pbData[i + 1] == '\n')
                i++;
            if (i + 1 < cbData)
                return true;
        }
    }
    return false;
}


/**
 * Prints an escaped UTF-8 string to an output stream.
 *
 * @param   pStrm               Output stream.
 * @param   pszText             UTF-8 text bytes.
 * @param   cchText             Number of bytes to print.
 */
void ShClHlpPrintEscapedString(PRTSTREAM pStrm, const char *pszText, size_t cchText)
{
    AssertPtrReturnVoid(pStrm);
    if (!cchText)
        return;
    AssertPtrReturnVoid(pszText);

    size_t offPending = 0;
    for (size_t i = 0; i < cchText; i++)
    {
        unsigned char const ch = (unsigned char)pszText[i];
        const char *pszEscape = NULL;
        size_t cchEscape = 0;
        switch (ch)
        {
            case '\n': pszEscape = "\\n";  cchEscape = 2; break;
            case '\r': pszEscape = "\\r";  cchEscape = 2; break;
            case '\t': pszEscape = "\\t";  cchEscape = 2; break;
            case '\\': pszEscape = "\\\\"; cchEscape = 2; break;
            case '"':  pszEscape = "\\\""; cchEscape = 2; break;
            default:
                if (ch < 0x20)
                {
                    if (i > offPending)
                        RTStrmWrite(pStrm, &pszText[offPending], i - offPending);
                    RTStrmPrintf(pStrm, "\\x%02x", ch);
                    offPending = i + 1;
                }
                break;
        }
        if (pszEscape)
        {
            if (i > offPending)
                RTStrmWrite(pStrm, &pszText[offPending], i - offPending);
            RTStrmWrite(pStrm, pszEscape, cchEscape);
            offPending = i + 1;
        }
    }
    if (offPending < cchText)
        RTStrmWrite(pStrm, &pszText[offPending], cchText - offPending);
}
