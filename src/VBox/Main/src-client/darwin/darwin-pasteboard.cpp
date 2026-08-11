/* $Id: darwin-pasteboard.cpp 114987 2026-08-11 13:50:56Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Mac OS X host implementation.
 */

/*
 * Includes contributions from François Revol
 *
 * Copyright (C) 2008-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <Carbon/Carbon.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <iprt/path.h>
# include <iprt/uri.h>
#endif

#include <VBox/log.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/clipboard-helper.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif

#include "darwin-pasteboard.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define WITH_HTML_H2G 1
#define WITH_HTML_G2H 1

RT_GCC_NO_WARN_DEPRECATED_BEGIN /* Much here is deprecated since 12.0 */

/* For debugging */
//#define SHOW_CLIPBOARD_CONTENT


/**
 * Initialize the global pasteboard and return a reference to it.
 *
 * @returns VBox status code.
 * @param   pPasteboardRef          Reference to the global pasteboard.
 */
DECLHIDDEN(int) initPasteboard(PasteboardRef *pPasteboardRef)
{
    AssertPtrReturn(pPasteboardRef, VERR_INVALID_POINTER);

    *pPasteboardRef = NULL;

    OSStatus orc = PasteboardCreate(kPasteboardClipboard, pPasteboardRef);
    if (   orc == 0
        && *pPasteboardRef != NULL)
        return VINF_SUCCESS;

    destroyPasteboard(pPasteboardRef);
    return VERR_NOT_SUPPORTED;
}

/**
 * Release the reference to the global pasteboard.
 *
 * @param pPasteboardRef        Reference to the global pasteboard.
 */
DECLHIDDEN(void) destroyPasteboard(PasteboardRef *pPasteboardRef)
{
    if (*pPasteboardRef != NULL)
    {
        CFRelease(*pPasteboardRef);
        *pPasteboardRef = NULL;
    }
}

/**
 * Inspect the global pasteboard for new content. Check if there is some type
 * that is supported by vbox and return it.
 *
 * @param   hPasteboard         Reference to the global pasteboard.
 * @param   idOwnership         Our ownership ID.
 * @param   hStrOwnershipFlavor The ownership flavor string reference returned
 *                              by takePasteboardOwnership().
 * @param   fForce              Whether to inspect the current content even if
 *                              the pasteboard change was already observed.
 * @param   pfFormats           Pointer for the bit combination of the
 *                              supported types.
 * @param   pfChanged           True if something has changed after the
 *                              last call.
 *
 * @returns VINF_SUCCESS.
 */
DECLHIDDEN(int) queryNewPasteboardFormats(PasteboardRef hPasteboard, uint64_t idOwnership, void *hStrOwnershipFlavor,
                                          bool fForce, uint32_t *pfFormats, bool *pfChanged)
{
    AssertPtrReturn(hPasteboard, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFormats,   VERR_INVALID_POINTER);
    AssertPtrReturn(pfChanged,   VERR_INVALID_POINTER);

    *pfFormats = 0;
    *pfChanged = true;

    /* Make sure all is in sync */
    PasteboardSyncFlags const syncFlags = PasteboardSynchronize(hPasteboard);
    /* If nothing changed return */
    if (   !(syncFlags & kPasteboardModified)
        && !fForce)
    {
        *pfChanged = false;
        Log2(("queryNewPasteboardFormats: no change\n"));
        return VINF_SUCCESS;
    }

    /* Are some items in the pasteboard? */
    ItemCount cItems = 0;
    OSStatus const orcItems = PasteboardGetItemCount(hPasteboard, &cItems);
    if (orcItems == noErr)
    {
        if (cItems < 1)
            Log(("queryNewPasteboardFormats: changed: No items on the pasteboard\n"));
        else
        {
            bool fOwnClipboard = false;
            for (ItemCount idxItem = 0; idxItem < cItems; idxItem++)
            {
                PasteboardItemID idItem = 0;
                OSStatus const orcItem = PasteboardGetItemIdentifier(hPasteboard, idxItem + 1, &idItem);
                if (orcItem == noErr)
                {
                    /*
                     * Retrieve all flavors on the pasteboard, maybe there
                     * is something we can use.  Or maybe we're the owner.
                     */
                    CFArrayRef hFlavors = NULL;
                    OSStatus const orcFlavors = PasteboardCopyItemFlavors(hPasteboard, idItem, &hFlavors);
                    if (   orcFlavors == noErr
                        && hFlavors)
                    {
                        CFIndex const cFlavors = CFArrayGetCount(hFlavors);
                        for (CFIndex idxFlavor = 0; idxFlavor < cFlavors; idxFlavor++)
                        {
                            CFStringRef hStrFlavor = (CFStringRef)CFArrayGetValueAtIndex(hFlavors, idxFlavor);
                            if (   idItem == (PasteboardItemID)idOwnership
                                && hStrOwnershipFlavor
                                && CFStringCompare(hStrFlavor, (CFStringRef)hStrOwnershipFlavor, 0) == kCFCompareEqualTo)
                            {
                                /* We made the changes ourselves. */
                                Log2(("queryNewPasteboardFormats: no-changed: our clipboard!\n"));
                                fOwnClipboard = true;
                                break;
                            }

                            if (UTTypeConformsTo(hStrFlavor, kUTTypeBMP))
                            {
                                Log(("queryNewPasteboardFormats: BMP flavor detected.\n"));
                                *pfFormats |= VBOX_SHCL_FMT_BITMAP;
                            }
                            else if (   UTTypeConformsTo(hStrFlavor, kUTTypeUTF8PlainText)
                                     || UTTypeConformsTo(hStrFlavor, kUTTypeUTF16PlainText))
                            {
                                Log(("queryNewPasteboardFormats: Unicode flavor detected.\n"));
                                *pfFormats |= VBOX_SHCL_FMT_UNICODETEXT;
                            }
#ifdef WITH_HTML_H2G
                            else if (UTTypeConformsTo(hStrFlavor, kUTTypeHTML))
                            {
                                Log(("queryNewPasteboardFormats: HTML flavor detected.\n"));
                                *pfFormats |= VBOX_SHCL_FMT_HTML;
                            }
#endif
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                            else if (UTTypeConformsTo(hStrFlavor, kUTTypeFileURL))
                            {
                                Log(("queryNewPasteboardFormats: File URL flavor detected.\n"));
                                *pfFormats |= VBOX_SHCL_FMT_URI_LIST;
                            }
#endif
#ifdef LOG_ENABLED
                            else if (LogIs2Enabled())
                            {
                                if (CFStringGetCharactersPtr(hStrFlavor))
                                    Log2(("queryNewPasteboardFormats: Unknown flavor: %ls.\n",
                                          CFStringGetCharactersPtr(hStrFlavor)));
                                else if (CFStringGetCStringPtr(hStrFlavor, kCFStringEncodingUTF8))
                                    Log2(("queryNewPasteboardFormats: Unknown flavor: %s.\n",
                                          CFStringGetCStringPtr(hStrFlavor, kCFStringEncodingUTF8)));
                                else
                                    Log2(("queryNewPasteboardFormats: Unknown flavor: ???\n"));
                            }
#endif
                        }
                    }
                    else
                        Log(("queryNewPasteboardFormats: PasteboardCopyItemFlavors failed - %d (%#x)\n",
                             orcFlavors, orcFlavors));
                    if (hFlavors)
                        CFRelease(hFlavors);
                    if (fOwnClipboard)
                        break;
                }
                else
                    Log(("queryNewPasteboardFormats: PasteboardGetItemIdentifier failed - %d (%#x)\n", orcItem, orcItem));
            }

            if (fOwnClipboard)
            {
                *pfChanged = false;
                *pfFormats = 0;
            }
            else
                Log(("queryNewPasteboardFormats: changed: *pfFormats=%#x\n", *pfFormats));
        }
    }
    else
        Log(("queryNewPasteboardFormats: PasteboardGetItemCount failed - %d (%#x)\n", orcItems, orcItems));
    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Converts one macOS file URL pasteboard value to a canonical file URI.
 *
 * @returns VBox status code.
 * @param   hData               File URL pasteboard data.
 * @param   ppszURI             Where to return the allocated URI.  Must be
 *                              freed with RTStrFree().
 * @param   pcchURI             Where to return the URI length without the
 *                              terminator.
 */
static int darwinPasteboardFileURLToURI(CFDataRef hData, char **ppszURI, size_t *pcchURI)
{
    AssertPtrReturn(hData,   VERR_INVALID_POINTER);
    AssertPtrReturn(ppszURI, VERR_INVALID_POINTER);
    AssertPtrReturn(pcchURI, VERR_INVALID_POINTER);

    *ppszURI = NULL;
    *pcchURI = 0;

    CFIndex const cbData = CFDataGetLength(hData);
    if (cbData <= 0)
        return VERR_INVALID_PARAMETER;
    if ((uint64_t)cbData > (uint64_t)RTPATH_MAX * 3 + 32)
        return VERR_TOO_MUCH_DATA;

    const UInt8 *pbData = CFDataGetBytePtr(hData);
    AssertPtrReturn(pbData, VERR_INVALID_POINTER);

    size_t cchData = 0;
    int vrc = ShClHlpUtf8ValidateExact((const char *)pbData, (size_t)cbData, &cchData);
    if (RT_FAILURE(vrc))
        return vrc;
    if (!cchData)
        return VERR_INVALID_PARAMETER;
    for (size_t off = 0; off + 2 < cchData; off++)
        if (   pbData[off] == '%'
            && pbData[off + 1] == '0'
            && pbData[off + 2] == '0')
            return VERR_INVALID_PARAMETER;

    CFURLRef hURL = CFURLCreateWithBytes(kCFAllocatorDefault, pbData, (CFIndex)cchData,
                                         kCFStringEncodingUTF8, NULL /* baseURL */);
    if (!hURL)
        return VERR_INVALID_PARAMETER;

    CFStringRef hScheme   = CFURLCopyScheme(hURL);
    CFStringRef hLocation = CFURLCopyNetLocation(hURL);
    CFStringRef hQuery    = CFURLCopyQueryString(hURL, NULL /* charactersToLeaveEscaped */);
    CFStringRef hFragment = CFURLCopyFragment(hURL, NULL /* charactersToLeaveEscaped */);
    if (   !hScheme
        || CFStringCompare(hScheme, CFSTR("file"), kCFCompareCaseInsensitive) != kCFCompareEqualTo
        || (   hLocation
            && CFStringGetLength(hLocation)
            && CFStringCompare(hLocation, CFSTR("localhost"), kCFCompareCaseInsensitive) != kCFCompareEqualTo)
        || hQuery
        || hFragment)
        vrc = VERR_INVALID_PARAMETER;

    char szPath[RTPATH_MAX];
    if (RT_SUCCESS(vrc))
    {
        if (!CFURLGetFileSystemRepresentation(hURL, true /* resolveAgainstBase */, (UInt8 *)szPath, sizeof(szPath)))
            vrc = VERR_INVALID_PARAMETER;
        else if (!RTPathStartsWithRoot(szPath))
            vrc = VERR_PATH_IS_RELATIVE;
        else
            vrc = RTStrValidateEncoding(szPath);
    }

    if (RT_SUCCESS(vrc))
    {
        vrc = RTUriFileCreateEx(szPath, RTPATH_STR_F_STYLE_UNIX, ppszURI, 0 /* cbUri */, NULL /* pcchUri */);
        if (RT_SUCCESS(vrc))
            *pcchURI = strlen(*ppszURI);
    }

    if (hFragment)
        CFRelease(hFragment);
    if (hQuery)
        CFRelease(hQuery);
    if (hLocation)
        CFRelease(hLocation);
    if (hScheme)
        CFRelease(hScheme);
    CFRelease(hURL);
    return vrc;
}


/**
 * Reads local file URLs from the macOS pasteboard as a transfer root list.
 *
 * @returns VBox status code.
 * @param   hPasteboard         Reference to the pasteboard to read.
 * @param   ppszRoots           Where to return the allocated CRLF-separated
 *                              file URI list.  Must be freed with RTStrFree().
 * @param   pcbRoots            Where to return the list size, including the
 *                              terminator.
 */
DECLHIDDEN(int) readFileURLsFromPasteboard(PasteboardRef hPasteboard, char **ppszRoots, size_t *pcbRoots)
{
    AssertPtrReturn(hPasteboard, VERR_INVALID_POINTER);
    AssertPtrReturn(ppszRoots,   VERR_INVALID_POINTER);
    AssertPtrReturn(pcbRoots,    VERR_INVALID_POINTER);

    *ppszRoots = NULL;
    *pcbRoots = 0;

    PasteboardSynchronize(hPasteboard);

    ItemCount cItems = 0;
    OSStatus const orcItems = PasteboardGetItemCount(hPasteboard, &cItems);
    if (orcItems != noErr)
        return VERR_GENERAL_FAILURE;

    char  *pszRoots = NULL;
    size_t cbRoots = 0;
    size_t cRoots = 0;
    int vrc = VINF_SUCCESS;
    for (ItemCount idxItem = 0; idxItem < cItems; idxItem++)
    {
        PasteboardItemID idItem = 0;
        OSStatus const orcItem = PasteboardGetItemIdentifier(hPasteboard, idxItem + 1, &idItem);
        if (orcItem != noErr)
        {
            vrc = VERR_GENERAL_FAILURE;
            break;
        }

        CFArrayRef hFlavors = NULL;
        OSStatus const orcFlavors = PasteboardCopyItemFlavors(hPasteboard, idItem, &hFlavors);
        if (   orcFlavors != noErr
            || !hFlavors)
        {
            if (hFlavors)
                CFRelease(hFlavors);
            vrc = VERR_GENERAL_FAILURE;
            break;
        }

        CFStringRef hFileURLFlavor = NULL;
        CFIndex const cFlavors = CFArrayGetCount(hFlavors);
        for (CFIndex idxFlavor = 0; idxFlavor < cFlavors; idxFlavor++)
        {
            CFStringRef hFlavor = (CFStringRef)CFArrayGetValueAtIndex(hFlavors, idxFlavor);
            if (UTTypeConformsTo(hFlavor, kUTTypeFileURL))
            {
                hFileURLFlavor = hFlavor;
                break;
            }
        }

        if (hFileURLFlavor)
        {
            CFDataRef hData = NULL;
            OSStatus const orcData = PasteboardCopyItemFlavorData(hPasteboard, idItem, hFileURLFlavor, &hData);
            if (   orcData == noErr
                && hData)
            {
                char *pszURI = NULL;
                size_t cchURI = 0;
                vrc = darwinPasteboardFileURLToURI(hData, &pszURI, &cchURI);
                if (RT_SUCCESS(vrc))
                {
                    size_t const cchSep = sizeof(SHCL_TRANSFER_URI_LIST_SEP_STR) - 1;
                    if (   cbRoots > ~(size_t)0 - cchSep - 1
                        || cchURI > ~(size_t)0 - cbRoots - cchSep - 1)
                        vrc = VERR_TOO_MUCH_DATA;
                    else
                    {
                        vrc = RTStrAAppendExN(&pszRoots, 2 /* cPairs */, pszURI, cchURI,
                                             SHCL_TRANSFER_URI_LIST_SEP_STR, cchSep);
                        if (RT_SUCCESS(vrc))
                        {
                            cbRoots += cchURI + cchSep;
                            cRoots++;
                        }
                    }
                }
                RTStrFree(pszURI);
            }
            else
                vrc = VERR_GENERAL_FAILURE;
            if (hData)
                CFRelease(hData);
        }

        CFRelease(hFlavors);
        if (RT_FAILURE(vrc))
            break;
    }

    if (   RT_SUCCESS(vrc)
        && !cRoots)
        vrc = VERR_NOT_FOUND;
    if (RT_SUCCESS(vrc))
    {
        *ppszRoots = pszRoots;
        *pcbRoots = cbRoots + 1;
        LogRel2(("Shared Clipboard: macOS reported %zu root entries for transfer to guest\n", cRoots));
    }
    else
        RTStrFree(pszRoots);
    return vrc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/**
 * Read content from the host clipboard and write it to the internal clipboard
 * structure for further processing.
 *
 * @param   pPasteboard    Reference to the global pasteboard.
 * @param   fFormat        The format type which should be read.
 * @param   pv             The destination buffer.
 * @param   cb             The size of the destination buffer.
 * @param   pcbActual      The size which is needed to transfer the content.
 *
 * @returns IPRT status code.
 */
DECLHIDDEN(int) readFromPasteboard(PasteboardRef pPasteboard, uint32_t fFormat, void *pv, uint32_t cb, uint32_t *pcbActual)
{
    Log(("readFromPasteboard: fFormat = %02X\n", fFormat));

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (fFormat & VBOX_SHCL_FMT_URI_LIST)
    {
        char  *pszRoots = NULL;
        size_t cbRoots = 0;
        int vrc = readFileURLsFromPasteboard(pPasteboard, &pszRoots, &cbRoots);
        if (RT_SUCCESS(vrc))
        {
            if (cbRoots > UINT32_MAX)
                vrc = VERR_TOO_MUCH_DATA;
            else
            {
                *pcbActual = (uint32_t)cbRoots;
                if (cbRoots <= cb)
                    memcpy(pv, pszRoots, cbRoots);
                else
                    vrc = VINF_BUFFER_OVERFLOW;
            }
        }
        RTStrFree(pszRoots);
        return vrc;
    }
#endif

    /* Make sure all is in sync */
    PasteboardSynchronize(pPasteboard);

    /* Are some items in the pasteboard? */
    ItemCount cItems;
    OSStatus orc = PasteboardGetItemCount(pPasteboard, &cItems);
    if (cItems < 1)
        return VINF_SUCCESS;

    /*
     * Our default response...
     */
    int vrc = VERR_NOT_SUPPORTED;

    /*
     * The id of the first element in the pasteboard
     */
    PasteboardItemID idItem;
    orc = PasteboardGetItemIdentifier(pPasteboard, 1, &idItem);
    if (orc == 0)
    {
        CFDataRef hDataCopy  = 0;
        size_t    cbDataCopy = 0;

        /*
         * The guest request unicode
         */
        if (fFormat & VBOX_SHCL_FMT_UNICODETEXT)
        {
            PRTUTF16  pwszSrcFree = NULL;
            PCRTUTF16 pwszSrc     = NULL;
            size_t    cwcSrc      = 0;

            /* First preference is plain UTF-16 text: */
            orc = PasteboardCopyItemFlavorData(pPasteboard, idItem, kUTTypeUTF16PlainText, &hDataCopy);
            if (orc == 0)
            {
                cbDataCopy = CFDataGetLength(hDataCopy);
                Log(("Clipboard content is utf-16 (%zu bytes)\n", cbDataCopy));
                pwszSrc = (PCRTUTF16)CFDataGetBytePtr(hDataCopy);
                if (pwszSrc)
                {
                    cwcSrc = RTUtf16NLen(pwszSrc, cbDataCopy / sizeof(RTUTF16));
                    if (cwcSrc >= cbDataCopy / sizeof(RTUTF16))
                    {
                        pwszSrcFree = RTUtf16Alloc((cwcSrc + 1) * sizeof(RTUTF16));
                        if (pwszSrcFree)
                        {
                            memcpy(pwszSrcFree, pwszSrc, cwcSrc * sizeof(RTUTF16));
                            pwszSrcFree[cwcSrc] = '\0';
                            pwszSrc = pwszSrcFree;
                        }
                        else
                        {
                            vrc = VERR_NO_UTF16_MEMORY;
                            pwszSrc = NULL;
                        }
                    }
                }
                else
                    vrc = VERR_GENERAL_FAILURE;
            }
            /* Second preference is plain UTF-8 text: */
            else
            {
                orc = PasteboardCopyItemFlavorData(pPasteboard, idItem, kUTTypeUTF8PlainText, &hDataCopy);
                if (orc == 0)
                {
                    cbDataCopy = CFDataGetLength(hDataCopy);
                    Log(("readFromPasteboard: clipboard content is utf-8 (%zu bytes)\n", cbDataCopy));
                    const char *pszSrc = (const char *)CFDataGetBytePtr(hDataCopy);
                    if (pszSrc)
                    {
                        size_t cchSrc = RTStrNLen(pszSrc, cbDataCopy);
                        vrc = RTStrToUtf16Ex(pszSrc, cchSrc, &pwszSrcFree, 0, &cwcSrc);
                        if (RT_SUCCESS(vrc))
                            pwszSrc = pwszSrcFree;
                    }
                    else
                        vrc = VERR_GENERAL_FAILURE;
                }
            }
            if (pwszSrc)
            {
                /*
                 * Convert to windows UTF-16.
                 */
                Assert(cwcSrc == RTUtf16Len(pwszSrc));
                size_t cwcDst = 0;
                vrc = ShClHlpUtf16CalcNormalizedEolToCRLFLength(pwszSrc, cwcSrc, &cwcDst);
                if (RT_SUCCESS(vrc))
                {
                    cwcDst++; /* Add space for terminator. */

                    *pcbActual = cwcDst * sizeof(RTUTF16);
                    if (*pcbActual <= cb)
                    {
                        vrc = ShClHlpConvUtf16LFToCRLF(pwszSrc, cwcSrc, (PRTUTF16)pv, cb / sizeof(RTUTF16));
                        if (RT_SUCCESS(vrc))
                        {
#ifdef SHOW_CLIPBOARD_CONTENT
                            Log(("readFromPasteboard: clipboard content: %ls\n", (PCRTUTF16)pv));
#endif
                        }
                        else
                        {
                            Log(("readFromPasteboard: ShClHlpConvUtf16LFToCRLF failed - %Rrc!\n", vrc));
                            AssertRC(vrc);
                        }
                    }
                    else
                    {
                        Log(("readFromPasteboard: Insufficient (text) buffer space: %#zx, need %#zx\n", cb, *pcbActual));
                        vrc = VINF_SUCCESS;
                    }
                }
                else
                {
                    Log(("readFromPasteboard: ShClHlpUtf16CalcNormalizedEolToCRLFLength failed - %Rrc!\n", vrc));
                    AssertRC(vrc);
                }
                RTUtf16Free(pwszSrcFree);
            }
        }
        /*
         * The guest request BITMAP
         */
        else if (fFormat & VBOX_SHCL_FMT_BITMAP)
        {
            /* Get the BMP data from the pasteboard */
            orc = PasteboardCopyItemFlavorData(pPasteboard, idItem, kUTTypeBMP, &hDataCopy);
            if (orc == 0)
            {
                cbDataCopy = CFDataGetLength(hDataCopy);
                Log(("Clipboard content is BMP (%zu bytes)\n", cbDataCopy));
                const void *pvSrc = CFDataGetBytePtr(hDataCopy);
                if (pvSrc)
                {
                    /*
                     * Try get the device independent bitmap (DIB) bit from it.
                     */
                    const void *pvDib;
                    size_t      cbDib;
                    vrc = ShClHlpBmpGetDib(pvSrc, cbDataCopy, &pvDib, &cbDib);
                    if (RT_SUCCESS(vrc))
                    {
                        *pcbActual = cbDib;
                        if (*pcbActual <= cb)
                        {
                            memcpy(pv, pvDib, cbDib);
#ifdef SHOW_CLIPBOARD_CONTENT
                            Log(("readFromPasteboard: clipboard content bitmap %zx bytes\n", cbDib));
#endif
                        }
                        else
                            Log(("readFromPasteboard: Insufficient (bitmap) buffer space: %#zx, need %#zx\n", cb, cbDib));
                        vrc = VINF_SUCCESS;
                    }
                    else
                    {
                        AssertRC(vrc);
                        Log(("readFromPasteboard: ShClHlpBmpGetDib failed - %Rrc - unknown bitmap format??\n", vrc));
                        vrc = VERR_NOT_SUPPORTED;
                    }
                }
                else
                    vrc = VERR_GENERAL_FAILURE;
            }
            else
                LogFlow(("readFromPasteboard: PasteboardCopyItemFlavorData/kUTTypeBMP -> %d (%#x)\n", orc, orc));
        }
#ifdef WITH_HTML_H2G
        /*
         * The guest request HTML.  It expects a UTF-8 reply and we assume
         * that's what's on the pasteboard too.
         */
        else if (fFormat & VBOX_SHCL_FMT_HTML)
        {
            orc = PasteboardCopyItemFlavorData(pPasteboard, idItem, kUTTypeHTML, &hDataCopy);
            if (orc == 0)
            {
                cbDataCopy = CFDataGetLength(hDataCopy);
                Log(("Clipboard content is HTML (%zu bytes):\n", cbDataCopy));
                const char *pszSrc = (const char *)CFDataGetBytePtr(hDataCopy);
                if (pszSrc)
                {
                    Log3(("%.*Rhxd\n", cbDataCopy, pszSrc));
                    vrc = RTStrValidateEncodingEx(pszSrc, cbDataCopy, 0 /*fFlags*/);
                    if (RT_SUCCESS(vrc))
                    {
                        size_t cchSrc = RTStrNLen(pszSrc, cbDataCopy);
                        *pcbActual = cchSrc;
                        if (cchSrc <= cb)
                            memcpy(pv, pszSrc, cchSrc);
                        else
                            Log(("readFromPasteboard: Insufficient (HTML) buffer space: %#zx, need %#zx\n", cb, cchSrc));
                        vrc = VINF_SUCCESS;
                    }
                    else
                    {
                        Log(("readFromPasteboard: Invalid UTF-8 encoding on pasteboard: %Rrc\n", vrc));
                        vrc = VERR_NOT_SUPPORTED;
                    }
                }
                else
                    vrc = VERR_GENERAL_FAILURE;
            }
            else
                LogFlow(("readFromPasteboard: PasteboardCopyItemFlavorData/kUTTypeHTML -> %d (%#x)\n", orc, orc));
        }
#endif
        else
        {
            Log2(("readFromPasteboard: Unsupported format: %#x\n", fFormat));
            vrc = VERR_NOT_SUPPORTED;
        }

        /*
         * Release the data copy, if we got one.  There are no returns above!
         */
        if (hDataCopy)
            CFRelease(hDataCopy);
    }
    else
    {
        Log(("readFromPasteboard: PasteboardGetItemIdentifier failed: %u (%#x)\n", orc, orc));
        vrc = VERR_NOT_SUPPORTED;
    }

    Log(("readFromPasteboard: vrc=%Rrc *pcbActual=%#zx\n", vrc, *pcbActual));
    return vrc;
}

/**
 * Takes the ownership of the pasteboard.
 *
 * This is called when the other end reports available formats.
 *
 * @returns VBox status code.
 * @param   hPasteboard             The pastboard handle (reference).
 * @param   idOwnership             The ownership ID to use now.
 * @param   pszOwnershipFlavor      The ownership indicator flavor
 * @param   pszOwnershipValue       The ownership value (stringified format mask).
 * @param   phStrOwnershipFlavor    Pointer to a CFStringRef variable holding
 *                                  the current ownership flavor string.  This
 *                                  will always be released, and set again on
 *                                  success.
 *
 * @todo    Add fFormats so we can make promises about available formats at once
 *          without needing to request any data first.  That might help on
 *          flavor priority.
 */
DECLHIDDEN(int) takePasteboardOwnership(PasteboardRef hPasteboard, uint64_t idOwnership, const char *pszOwnershipFlavor,
                                        const char *pszOwnershipValue, void **phStrOwnershipFlavor)
{
    int vrc = clearPasteboard(hPasteboard, phStrOwnershipFlavor);
    if (RT_SUCCESS(vrc))
    {
        OSStatus orc = 0;

        /* For good measure. */
        PasteboardSynchronize(hPasteboard);

        /*
         * Put the ownership flavor and value onto the clipboard.
         */
        CFDataRef hData = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)pszOwnershipValue, strlen(pszOwnershipValue));
        if (hData)
        {
            CFStringRef hFlavor = CFStringCreateWithCString(kCFAllocatorDefault, pszOwnershipFlavor, kCFStringEncodingUTF8);
            if (hFlavor)
            {
                orc = PasteboardPutItemFlavor(hPasteboard, (PasteboardItemID)idOwnership,
                                              hFlavor, hData, kPasteboardFlavorNoFlags);
                if (orc == 0)
                {
                    *phStrOwnershipFlavor = (void *)hFlavor;
                    Log(("takePasteboardOwnership: idOwnership=%RX64 flavor=%s value=%s\n",
                         idOwnership, pszOwnershipFlavor, pszOwnershipValue));
                }
                else
                {
                    Log(("takePasteboardOwnership: PasteboardPutItemFlavor -> %d (%#x)!\n", orc, orc));
                    CFRelease(hFlavor);
                }
            }
            else
                Log(("takePasteboardOwnership: CFStringCreateWithCString failed!\n"));
            CFRelease(hData);
        }
        else
            Log(("takePasteboardOwnership: CFDataCreate failed!\n"));
        if (orc != 0)
            vrc = VERR_GENERAL_FAILURE;
    }
    return vrc;
}

/**
 * Clears the pasteboard and releases any current ownership flavor.
 *
 * @returns VBox status code.
 * @param   hPasteboard             The pasteboard handle.
 * @param   phStrOwnershipFlavor    Pointer to the current ownership flavor string.
 */
DECLHIDDEN(int) clearPasteboard(PasteboardRef hPasteboard, void **phStrOwnershipFlavor)
{
    if (*phStrOwnershipFlavor)
    {
        CFStringRef hOldFlavor = (CFStringRef)*phStrOwnershipFlavor;
        CFRelease(hOldFlavor);
        *phStrOwnershipFlavor = NULL;
    }

    OSStatus orc = PasteboardClear(hPasteboard);
    if (orc == 0)
    {
        PasteboardSynchronize(hPasteboard);
        Log(("clearPasteboard: cleared pasteboard\n"));
    }
    else
        Log(("clearPasteboard: PasteboardClear failed -> %d (%#x)\n", orc, orc));
    return orc == 0 ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
}

/**
 * Write clipboard content to the host clipboard from the internal clipboard
 * structure.
 *
 * @param   hPasteboard    Reference to the global pasteboard.
 * @param   idOwnership    The ownership ID.
 * @param   pv             The source buffer.
 * @param   cb             The size of the source buffer.
 * @param   fFormat        The format type which should be written.
 *
 * @returns IPRT status code.
 */
DECLHIDDEN(int) writeToPasteboard(PasteboardRef hPasteboard, uint64_t idOwnership, const void *pv, uint32_t cb, uint32_t fFormat)
{
    int       vrc;
    OSStatus  orc;
    CFDataRef hData;
    Log(("writeToPasteboard: fFormat=%#x\n", fFormat));

    /* Make sure all is in sync */
    PasteboardSynchronize(hPasteboard);

    /*
     * Handle the unicode text
     */
    if (fFormat & VBOX_SHCL_FMT_UNICODETEXT)
    {
        PCRTUTF16 const pwszSrc = (PCRTUTF16)pv;
        size_t const    cwcSrc  = cb / sizeof(RTUTF16);

        /*
         * If the other side is windows or OS/2, we may have to convert
         * '\r\n' -> '\n' and the drop ending marker.
         */

        /* How long will the converted text be? */
        size_t cwcDst = 0;
        vrc = ShClHlpUtf16CRLFToLFLen(pwszSrc, cwcSrc, &cwcDst);
        AssertMsgRCReturn(vrc, ("ShClHlpUtf16CRLFToLFLen failed: %Rrc\n", vrc), vrc);

        /* Ignore empty strings? */ /** @todo r=andy Really? Why? */
        if (cwcDst == 0)
        {
            Log(("writeToPasteboard: received empty string from the guest; ignoreing it.\n"));
            return VINF_SUCCESS;
        }

        cwcDst++; /* Add space for terminator. */

        /* Allocate the necessary memory and do the conversion. */
        PRTUTF16 pwszDst = (PRTUTF16)RTMemAlloc(cwcDst * sizeof(RTUTF16));
        AssertMsgReturn(pwszDst, ("cwcDst=%#zx\n", cwcDst), VERR_NO_UTF16_MEMORY);

        vrc = ShClHlpConvUtf16CRLFToLF(pwszSrc, cwcSrc, pwszDst, cwcDst);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Create an immutable CFData object that we can place on the clipboard.
             */
            hData = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)pwszDst, cwcDst * sizeof(RTUTF16));
            if (hData)
            {
                orc = PasteboardPutItemFlavor(hPasteboard, (PasteboardItemID)idOwnership,
                                              kUTTypeUTF16PlainText, hData, kPasteboardFlavorNoFlags);
                if (orc == 0)
                    vrc = VINF_SUCCESS;
                else
                {
                    Log(("writeToPasteboard: PasteboardPutItemFlavor/kUTTypeUTF16PlainText failed: %d (%#x)\n", orc, orc));
                    vrc = VERR_GENERAL_FAILURE;
                }
                CFRelease(hData);
            }
            else
            {
                Log(("writeToPasteboard: CFDataCreate/UTF16 failed!\n"));
                vrc = VERR_NO_MEMORY;
            }

            /*
             * Now for the UTF-8 version.
             */
            PCRTUTF16 const pwszUtf8 = pwszDst[0] == VBOX_SHCL_UTF16_BOM ? pwszDst + 1 : pwszDst;
            char *pszDst;
            int vrc2 = RTUtf16ToUtf8(pwszUtf8, &pszDst);
            if (RT_SUCCESS(vrc2))
            {
                hData = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)pszDst, strlen(pszDst));
                if (hData)
                {
                    orc = PasteboardPutItemFlavor(hPasteboard, (PasteboardItemID)idOwnership,
                                                  kUTTypeUTF8PlainText, hData, kPasteboardFlavorNoFlags);
                    if (orc != 0)
                    {
                        Log(("writeToPasteboard: PasteboardPutItemFlavor/kUTTypeUTF8PlainText failed: %d (%#x)\n", orc, orc));
                        vrc = VERR_GENERAL_FAILURE;
                    }
                    CFRelease(hData);
                }
                else
                {
                    Log(("writeToPasteboard: CFDataCreate/UTF8 failed!\n"));
                    vrc = VERR_NO_MEMORY;
                }
                RTStrFree(pszDst);
            }
            else
                vrc = vrc2;
        }
        else
            Log(("writeToPasteboard: clipboard conversion failed.  vboxClipboardUtf16WinToLin() returned %Rrc.  Abandoning.\n", vrc));

        RTMemFree(pwszDst);
    }
    /*
     * Handle the bitmap.  We convert the DIB to a bitmap and put it on
     * the pasteboard using the BMP flavor.
     */
    else if (fFormat & VBOX_SHCL_FMT_BITMAP)
    {
        /* Create a full BMP from it */
        void  *pvBmp;
        size_t cbBmp;
        vrc = ShClHlpDibToBmp(pv, cb, &pvBmp, &cbBmp);
        if (RT_SUCCESS(vrc))
        {
            hData = CFDataCreate(kCFAllocatorDefault, (UInt8 const *)pvBmp, cbBmp);
            if (hData)
            {
                orc = PasteboardPutItemFlavor(hPasteboard, (PasteboardItemID)idOwnership,
                                              kUTTypeBMP, hData, kPasteboardFlavorNoFlags);
                if (orc != 0)
                {
                    Log(("writeToPasteboard: PasteboardPutItemFlavor/kUTTypeBMP failed: %d (%#x)\n", orc, orc));
                    vrc = VERR_GENERAL_FAILURE;
                }
                CFRelease(hData);
            }
            else
            {
                Log(("writeToPasteboard: CFDataCreate/UTF8 failed!\n"));
                vrc = VERR_NO_MEMORY;
            }
            RTMemFree(pvBmp);
        }
    }
#ifdef WITH_HTML_G2H
    /*
     * Handle HTML.  Expect UTF-8, ignore line endings and just put it
     * straigh up on the pasteboard for now.
     */
    else if (fFormat & VBOX_SHCL_FMT_HTML)
    {
        const char   *pszSrc = (const char *)pv;
        size_t const  cchSrc = RTStrNLen(pszSrc, cb);
        vrc = RTStrValidateEncodingEx(pszSrc, cchSrc, 0);
        if (RT_SUCCESS(vrc))
        {
            hData = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)pszSrc, cchSrc);
            if (hData)
            {
                orc = PasteboardPutItemFlavor(hPasteboard, (PasteboardItemID)idOwnership, kUTTypeHTML,
                                              hData, kPasteboardFlavorNoFlags);
                if (orc == 0)
                    vrc = VINF_SUCCESS;
                else
                {
                    Log(("writeToPasteboard: PasteboardPutItemFlavor/kUTTypeHTML failed: %d (%#x)\n", orc, orc));
                    vrc = VERR_GENERAL_FAILURE;
                }
                CFRelease(hData);
            }
            else
            {
                Log(("writeToPasteboard: CFDataCreate/HTML failed!\n"));
                vrc = VERR_NO_MEMORY;
            }
        }
        else
            Log(("writeToPasteboard: HTML: Invalid UTF-8 encoding: %Rrc\n", vrc));
    }
#endif
    else
        vrc = VERR_NOT_IMPLEMENTED;

    Log(("writeToPasteboard: vrc=%Rrc\n", vrc));
    return vrc;
}

RT_GCC_NO_WARN_DEPRECATED_END
