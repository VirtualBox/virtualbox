/* $Id: clipboard-helper.cpp 114779 2026-07-26 01:05:00Z knut.osmundsen@oracle.com $ */
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
#include <iprt/errcore.h>
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
    AssertPtrReturn(pvSrc,  VERR_INVALID_POINTER);
    AssertReturn(cbSrc,     VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppvDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbDst, VERR_INVALID_POINTER);

    PBMPWIN3XINFOHDR coreHdr = (PBMPWIN3XINFOHDR)pvSrc;
    /** @todo Support all the many versions of the DIB headers. */
    if (   cbSrc < sizeof(BMPWIN3XINFOHDR)
        || RT_LE2H_U32(coreHdr->cbSize) < sizeof(BMPWIN3XINFOHDR)
        || RT_LE2H_U32(coreHdr->cbSize) != sizeof(BMPWIN3XINFOHDR))
    {
        return VERR_INVALID_PARAMETER;
    }

    size_t offPixel = sizeof(BMPFILEHDR)
                    + RT_LE2H_U32(coreHdr->cbSize)
                    + RT_LE2H_U32(coreHdr->cClrUsed) * sizeof(uint32_t);
    if (cbSrc < offPixel)
        return VERR_INVALID_PARAMETER;

    size_t cbDst = sizeof(BMPFILEHDR) + cbSrc;

    void *pvDst = RTMemAllocZ(cbDst);
    if (!pvDst)
        return VERR_NO_MEMORY;

    PBMPFILEHDR fileHdr = (PBMPFILEHDR)pvDst;

    fileHdr->uType       = BMP_HDR_MAGIC;
    fileHdr->cbFileSize  = (uint32_t)RT_H2LE_U32(cbDst);
    fileHdr->Reserved1   = 0;
    fileHdr->Reserved2   = 0;
    fileHdr->offBits     = (uint32_t)RT_H2LE_U32(offPixel);

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

