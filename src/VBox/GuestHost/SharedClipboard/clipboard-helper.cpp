/* $Id: clipboard-helper.cpp 114775 2026-07-26 00:14:04Z knut.osmundsen@oracle.com $ */
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

int ShClHlpUtf16LenUtf8(PCRTUTF16 pcwszSrc, size_t cwcSrc, size_t *pchLen)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pchLen, VERR_INVALID_POINTER);

    size_t chLen = 0;
    int rc = RTUtf16CalcUtf8LenEx(pcwszSrc, cwcSrc, &chLen);
    if (RT_SUCCESS(rc))
        *pchLen = chLen;
    return rc;
}

int ShClHlpConvUtf16CRLFToUtf8LF(PCRTUTF16 pcwszSrc, size_t cwcSrc,
                                  char *pszBuf, size_t cbBuf, size_t *pcbLen)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertReturn   (cwcSrc,   VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszBuf,   VERR_INVALID_POINTER);
    AssertPtrReturn(pcbLen,   VERR_INVALID_POINTER);

    int rc;

    PRTUTF16 pwszTmp = NULL;
    size_t   cchTmp  = 0;

    size_t   cbLen = 0;

    /* How long will the converted text be? */
    rc = ShClHlpUtf16CRLFLenUtf8(pcwszSrc, cwcSrc, &cchTmp);
    if (RT_SUCCESS(rc))
    {
        cchTmp++; /* Add space for terminator. */

        pwszTmp = (PRTUTF16)RTMemAllocZ(cchTmp * sizeof(RTUTF16));
        if (pwszTmp)
        {
            rc = ShClHlpConvUtf16CRLFToLF(pcwszSrc, cwcSrc, pwszTmp, cchTmp);
            if (RT_SUCCESS(rc))
                rc = RTUtf16ToUtf8Ex(pwszTmp + 1, cchTmp - 1, &pszBuf, cbBuf, &cbLen);

            RTMemFree(reinterpret_cast<void *>(pwszTmp));
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        *pcbLen = cbLen;
    }

    return rc;
}

int ShClHlpConvUtf16LFToCRLFA(PCRTUTF16 pcwszSrc, size_t cwcSrc,
                               PRTUTF16 *ppwszDst, size_t *pcwDst)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(ppwszDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcwDst,   VERR_INVALID_POINTER);

    PRTUTF16 pwszDst = NULL;
    size_t   cchDst;

    int rc = ShClHlpUtf16CalcNormalizedEolToCRLFLength(pcwszSrc, cwcSrc, &cchDst);
    if (RT_SUCCESS(rc))
    {
        pwszDst = (PRTUTF16)RTMemAllocZ((cchDst + 1 /* Leave space for terminator */) * sizeof(RTUTF16));
        if (pwszDst)
        {
            rc = ShClHlpConvUtf16LFToCRLF(pcwszSrc, cwcSrc, pwszDst, cchDst + 1 /* Include terminator */);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        *ppwszDst = pwszDst;
        *pcwDst   = cchDst;
    }
    else
        RTMemFree(pwszDst);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int ShClHlpConvUtf8LFToUtf16CRLF(const char *pcszSrc, size_t cbSrc,
                                  PRTUTF16 *ppwszDst, size_t *pcwDst)
{
    AssertPtrReturn(pcszSrc,  VERR_INVALID_POINTER);
    AssertReturn(cbSrc,       VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppwszDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcwDst,   VERR_INVALID_POINTER);

    /* Intermediate conversion to UTF-16. */
    size_t   cwcTmp;
    PRTUTF16 pwcTmp = NULL;
    int rc = RTStrToUtf16Ex(pcszSrc, cbSrc, &pwcTmp, 0, &cwcTmp);
    if (RT_SUCCESS(rc))
    {
        rc = ShClHlpConvUtf16LFToCRLFA(pwcTmp, cwcTmp, ppwszDst, pcwDst);
        RTUtf16Free(pwcTmp);
    }

    return rc;
}

/**
 * Converts a Latin-1 string with LF line endings into an UTF-16 string with CRLF endings.
 *
 * @returns VBox status code.
 * @param   pcszSrc             Latin-1 string to convert.
 * @param   cbSrc               Size (in bytes) of Latin-1 string to convert.
 * @param   ppwszDst            Where to return the converted UTF-16 string on success.
 * @param   pcwDst              Where to return the length (in UTF-16 characters) on success.
 *
 * @note    Only converts the source until the string terminator is found (or length limit is hit).
 */
int ShClHlpConvLatin1LFToUtf16CRLF(const char *pcszSrc, size_t cbSrc,
                                    PRTUTF16 *ppwszDst, size_t *pcwDst)
{
    AssertPtrReturn(pcszSrc,  VERR_INVALID_POINTER);
    AssertReturn(cbSrc,       VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppwszDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcwDst,   VERR_INVALID_POINTER);

    size_t chSrc = 0;

    PRTUTF16 pwszDst = NULL;

    /* Calculate the space needed. */
    size_t cwDst = 0;
    for (size_t i = 0; i < cbSrc && pcszSrc[i] != '\0'; ++i)
    {
        if (pcszSrc[i] == VBOX_SHCL_LINEFEED)
            cwDst += 2; /* Space for VBOX_SHCL_CARRIAGERETURN + VBOX_SHCL_LINEFEED. */
        else
            ++cwDst;
        chSrc++;
    }

    pwszDst = (PRTUTF16)RTMemAllocZ((cwDst + 1 /* Leave space for the terminator */) * sizeof(RTUTF16));
    AssertPtrReturn(pwszDst, VERR_NO_MEMORY);

    /* Do the conversion, bearing in mind that Latin-1 expands "naturally" to UTF-16. */
    for (size_t i = 0, j = 0; i < chSrc; ++i, ++j)
    {
        AssertMsg(j <= cwDst, ("cbSrc=%zu, j=%u vs. cwDst=%u\n", cbSrc, j, cwDst));
        if (pcszSrc[i] != VBOX_SHCL_LINEFEED)
            pwszDst[j] = pcszSrc[i];
        else
        {
            pwszDst[j]     = VBOX_SHCL_CARRIAGERETURN;
            pwszDst[j + 1] = VBOX_SHCL_LINEFEED;
            ++j;
        }
    }

    pwszDst[cwDst] = '\0';  /* Make sure we are zero-terminated. */

    *ppwszDst = pwszDst;
    *pcwDst   = cwDst;

    return VINF_SUCCESS;
}

int ShClHlpConvUtf16ToUtf8HTML(PCRTUTF16 pcwszSrc, size_t cwcSrc, char **ppszDst, size_t *pcbDst)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertReturn   (cwcSrc,   VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppszDst,  VERR_INVALID_POINTER);
    AssertPtrReturn(pcbDst,   VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    size_t    cwTmp = cwcSrc;
    PCRTUTF16 pwTmp = pcwszSrc;

    char  *pchDst = NULL;
    size_t cbDst  = 0;

    size_t i = 0;
    while (i < cwTmp)
    {
        /* Find  zero symbol (end of string). */
        for (; i < cwTmp && pcwszSrc[i] != 0; i++)
            ;

        /* Convert found string. */
        char  *psz = NULL;
        size_t cch = 0;
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

    AssertMsgReturn(pcwszSrc[0] != VBOX_SHCL_UTF16BEMARKER,
                    ("Big endian UTF-16 not supported yet\n"), VERR_NOT_SUPPORTED);

    size_t cLen = 0;

    /* Don't copy the endian marker. */
    size_t i = pcwszSrc[0] == VBOX_SHCL_UTF16LEMARKER ? 1 : 0;

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

int ShClHlpUtf16CRLFLenUtf8(PCRTUTF16 pcwszSrc, size_t cwSrc, size_t *pchLen)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertReturn(cwSrc, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pchLen, VERR_INVALID_POINTER);

    AssertMsgReturn(pcwszSrc[0] != VBOX_SHCL_UTF16BEMARKER,
                    ("Big endian UTF-16 not supported yet\n"), VERR_NOT_SUPPORTED);

    size_t cLen = 0;

    /* Calculate the size of the destination text string. */
    /* Is this Utf16 or Utf16-LE? */
    if (pcwszSrc[0] == VBOX_SHCL_UTF16LEMARKER)
        cLen = 0;
    else
        cLen = 1;

    for (size_t i = 0; i < cwSrc; ++i, ++cLen)
    {
        if (   (i + 1 < cwSrc)
            && (pcwszSrc[i]     == VBOX_SHCL_CARRIAGERETURN)
            && (pcwszSrc[i + 1] == VBOX_SHCL_LINEFEED))
        {
            ++i;
        }
        if (pcwszSrc[i] == 0)
            break;
    }

    *pchLen = cLen;

    return VINF_SUCCESS;
}

int ShClHlpConvUtf16LFToCRLF(PCRTUTF16 pcwszSrc, size_t cwcSrc, PRTUTF16 pu16Dst, size_t cwcDst)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pu16Dst, VERR_INVALID_POINTER);
    AssertReturn(cwcDst, VERR_INVALID_PARAMETER);

    AssertMsgReturn(pcwszSrc[0] != VBOX_SHCL_UTF16BEMARKER,
                    ("Big endian UTF-16 not supported yet\n"), VERR_NOT_SUPPORTED);

    /* Don't copy the endian marker. */
    size_t      offDst = 0;
    for (size_t offSrc = pcwszSrc[0] == VBOX_SHCL_UTF16LEMARKER ? 1 : 0; offSrc < cwcSrc; ++offSrc, ++offDst)
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

int ShClHlpConvUtf16CRLFToLF(PCRTUTF16 pcwszSrc, size_t cwcSrc, PRTUTF16 pu16Dst, size_t cwDst)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertReturn(cwcSrc,      VERR_INVALID_PARAMETER);
    AssertPtrReturn(pu16Dst,  VERR_INVALID_POINTER);
    AssertReturn(cwDst,       VERR_INVALID_PARAMETER);

    AssertMsgReturn(pcwszSrc[0] != VBOX_SHCL_UTF16BEMARKER,
                    ("Big endian UTF-16 not supported yet\n"), VERR_NOT_SUPPORTED);

    /* Prepend the Utf16 byte order marker if it is missing. */
    size_t cwDstPos;
    if (pcwszSrc[0] == VBOX_SHCL_UTF16LEMARKER)
    {
        cwDstPos = 0;
    }
    else
    {
        pu16Dst[0] = VBOX_SHCL_UTF16LEMARKER;
        cwDstPos = 1;
    }

    for (size_t i = 0; i < cwcSrc; ++i, ++cwDstPos)
    {
        if (pcwszSrc[i] == 0)
            break;

        if (cwDstPos == cwDst)
            return VERR_BUFFER_OVERFLOW;

        if (   (i + 1 < cwcSrc)
            && (pcwszSrc[i]     == VBOX_SHCL_CARRIAGERETURN)
            && (pcwszSrc[i + 1] == VBOX_SHCL_LINEFEED))
        {
            ++i;
        }

        pu16Dst[cwDstPos] = pcwszSrc[i];
    }

    if (cwDstPos == cwDst)
        return VERR_BUFFER_OVERFLOW;

    /* Add terminating zero. */
    pu16Dst[cwDstPos] = 0;

    return VINF_SUCCESS;
}

int ShClHlpDibToBmp(const void *pvSrc, size_t cbSrc, void **ppvDest, size_t *pcbDest)
{
    AssertPtrReturn(pvSrc,   VERR_INVALID_POINTER);
    AssertReturn(cbSrc,      VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppvDest, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbDest, VERR_INVALID_POINTER);

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

    void *pvDest = RTMemAllocZ(cbDst);
    if (!pvDest)
        return VERR_NO_MEMORY;

    PBMPFILEHDR fileHdr = (PBMPFILEHDR)pvDest;

    fileHdr->uType       = BMP_HDR_MAGIC;
    fileHdr->cbFileSize  = (uint32_t)RT_H2LE_U32(cbDst);
    fileHdr->Reserved1   = 0;
    fileHdr->Reserved2   = 0;
    fileHdr->offBits     = (uint32_t)RT_H2LE_U32(offPixel);

    memcpy((uint8_t *)pvDest + sizeof(BMPFILEHDR), pvSrc, cbSrc);

    *ppvDest = pvDest;
    *pcbDest = cbDst;

    return VINF_SUCCESS;
}

int ShClHlpBmpGetDib(const void *pvSrc, size_t cbSrc, const void **ppvDest, size_t *pcbDest)
{
    AssertPtrReturn(pvSrc,   VERR_INVALID_POINTER);
    AssertReturn(cbSrc,      VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppvDest, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbDest, VERR_INVALID_POINTER);

    PBMPFILEHDR pBmpHdr = (PBMPFILEHDR)pvSrc;
    if (   cbSrc < sizeof(BMPFILEHDR)
        || pBmpHdr->uType != BMP_HDR_MAGIC
        || RT_LE2H_U32(pBmpHdr->cbFileSize) != cbSrc)
    {
        return VERR_INVALID_PARAMETER;
    }

    *ppvDest = ((uint8_t *)pvSrc) + sizeof(BMPFILEHDR);
    *pcbDest = cbSrc - sizeof(BMPFILEHDR);

    return VINF_SUCCESS;
}

