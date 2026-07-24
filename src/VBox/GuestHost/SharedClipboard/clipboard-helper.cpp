/* $Id: clipboard-helper.cpp 114762 2026-07-24 00:25:38Z knut.osmundsen@oracle.com $ */
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

int ShClHlpConvUtf16CRLFToUtf8LF(PCRTUTF16 pcwszSrc, size_t cwcSrc, char *pszBuf, size_t cbBuf, size_t *pcbLen)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pszBuf,   VERR_INVALID_POINTER);
    AssertReturn(   cbBuf,    VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbLen,   VERR_INVALID_POINTER);

    /*
     * Do a two step conversion.  First, do the CRLF -> LF bit,
     * then do the UTF-16 to UTF-8.
     */
    /* Step 1: */
    size_t   cchTmp  = 0;
    int rc = ShClHlpUtf16CRLFLenUtf8(pcwszSrc, cwcSrc, &cchTmp);
    if (RT_SUCCESS(rc))
    {
        cchTmp++; /* Add space for terminator. */

        PRTUTF16 pwszTmp = (PRTUTF16)RTMemTmpAllocZ(cchTmp * sizeof(RTUTF16));
        if (pwszTmp)
        {
            rc = ShClHlpConvUtf16CRLFToLF(pcwszSrc, cwcSrc, pwszTmp, cchTmp);
            if (RT_SUCCESS(rc))
            {
                /* Step 2: */
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

int ShClHlpConvLatin1LFToUtf16CRLF(const char *pcszSrc, size_t cbSrc,
                                    PRTUTF16 *ppwszDst, size_t *pcwcDst)
{
    AssertPtrReturn(pcszSrc,  VERR_INVALID_POINTER);
    AssertPtrReturn(ppwszDst, VERR_INVALID_POINTER);
    AssertPtrReturn(pcwcDst,  VERR_INVALID_POINTER);

    /* Count the LF codepoints that will double in size when prefixed by CR. */
    size_t cLinefeeds = 0;
    for (size_t iSrc = 0; iSrc < cbSrc; ++iSrc)
    {
        char const ch = pcszSrc[iSrc];
        if (ch != '\0')
        {
            if (ch == VBOX_SHCL_LINEFEED)
                cLinefeeds += 1;
        }
        else
        {
            cbSrc = iSrc;
            break;
        }
    }

    /* Allocate output buffer. */
    size_t const   cwcDst  = cbSrc + cLinefeeds;
    PRTUTF16 const pwszDst = (PRTUTF16)RTMemAllocZ((cwcDst + 1 /* Leave space for the terminator */) * sizeof(RTUTF16));
    AssertPtrReturn(pwszDst, VERR_NO_MEMORY);

    /* Do the conversion, bearing in mind that Latin-1 expands "naturally" to UTF-16. */
    size_t iDst = 0;
    for (size_t iSrc = 0; iSrc < cbSrc; ++iSrc)
    {
        Assert(iDst < cwcDst);
        if (pcszSrc[iSrc] != VBOX_SHCL_LINEFEED)
            pwszDst[iDst++] = pcszSrc[iSrc];
        else
        {
            if (iDst - iSrc < cLinefeeds)
                pwszDst[iDst++] = VBOX_SHCL_CARRIAGERETURN;
            else
                AssertMsgFailed(("line feed count increased! iSrc=%#zx iDst=%#zx cLinefeeds=%#zx cbSrc=%#zx\n",
                                 iSrc, iDst, cLinefeeds, cbSrc));
            pwszDst[iDst++] = VBOX_SHCL_LINEFEED;
        }
    }
    AssertMsg(iDst == cwcDst, ("line feed count decreased! iDst=%#zx cwcDst=%#zx cbSrc=%#zx cLinefeeds=%#zx\n",
                               iDst, cwcDst, cbSrc, cLinefeeds));
    Assert(iDst <= cwcDst);  /* impossible ...  */
    pwszDst[cwcDst] = '\0';  /* ... but make sure we are zero-terminated. */

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

int ShClHlpConvUtf16CRLFToLF(PCRTUTF16 pcwszSrc, size_t cwcSrc, PRTUTF16 pwszDst, size_t cwcDst)
{
    AssertPtrReturn(pcwszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pwszDst,  VERR_INVALID_POINTER);
    AssertReturn(cwcDst,      VERR_INVALID_PARAMETER);

    AssertMsgReturn(pcwszSrc[0] != VBOX_SHCL_UTF16BEMARKER,
                    ("Big endian UTF-16 not supported yet\n"), VERR_NOT_SUPPORTED);

    /* Prepend the Utf16 byte order marker if it is missing. */
    size_t offDst;
    if (pcwszSrc[0] == VBOX_SHCL_UTF16LEMARKER)
        offDst = 0;
    else
    {
        pwszDst[0] = VBOX_SHCL_UTF16LEMARKER;
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

    for (size_t i = 0; i < cchText; i++)
    {
        unsigned char const ch = (unsigned char)pszText[i];
        switch (ch)
        {
            case '\n': RTStrmWrite(pStrm, RT_STR_TUPLE("\\n")); break;
            case '\r': RTStrmWrite(pStrm, RT_STR_TUPLE("\\r")); break;
            case '\t': RTStrmWrite(pStrm, RT_STR_TUPLE("\\t")); break;
            case '\\': RTStrmWrite(pStrm, RT_STR_TUPLE("\\\\")); break;
            case '"':  RTStrmWrite(pStrm, RT_STR_TUPLE("\\\"")); break;
            default:
                if (ch >= 0x20)
                    RTStrmPutCh(pStrm, ch);
                else
                    RTStrmPrintf(pStrm, "\\x%02x", ch);
                break;
        }
    }
}
