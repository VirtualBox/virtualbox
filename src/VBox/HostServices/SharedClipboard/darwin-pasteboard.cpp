/* $Id: darwin-pasteboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard Service - Mac OS X host implementation.
 */

/*
 * Includes contributions from François Revol
 *
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <iprt/mem.h>
#include <iprt/errcore.h>
#include <iprt/utf16.h>

#include <VBox/log.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/clipboard-helper.h>

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
 * @param pPasteboardRef Reference to the global pasteboard.
 *
 * @returns IPRT status code.
 */
DECLHIDDEN(int) initPasteboard(PasteboardRef *pPasteboardRef)
{
    int rc = VINF_SUCCESS;

    if (PasteboardCreate(kPasteboardClipboard, pPasteboardRef))
        rc = VERR_NOT_SUPPORTED;

    return rc;
}

/**
 * Release the reference to the global pasteboard.
 *
 * @param pPasteboardRef Reference to the global pasteboard.
 */
DECLHIDDEN(void) destroyPasteboard(PasteboardRef *pPasteboardRef)
{
    CFRelease(*pPasteboardRef);
    *pPasteboardRef = NULL;
}

/**
 * Inspect the global pasteboard for new content. Check if there is some type
 * that is supported by vbox and return it.
 *
 * @param   hPasteboard         Reference to the global pasteboard.
 * @param   idOwnership         Our ownership ID.
 * @param   hStrOwnershipFlavor The ownership flavor string reference returned
 *                              by takePasteboardOwnership().
 * @param   pfFormats           Pointer for the bit combination of the
 *                              supported types.
 * @param   pfChanged           True if something has changed after the
 *                              last call.
 *
 * @returns VINF_SUCCESS.
 */
DECLHIDDEN(int) queryNewPasteboardFormats(PasteboardRef hPasteboard, uint64_t idOwnership, void *hStrOwnershipFlavor,
                                          uint32_t *pfFormats, bool *pfChanged)
{
    OSStatus orc;

    *pfFormats = 0;
    *pfChanged = true;

    PasteboardSyncFlags syncFlags;
    /* Make sure all is in sync */
    syncFlags = PasteboardSynchronize(hPasteboard);
    /* If nothing changed return */
    if (!(syncFlags & kPasteboardModified))
    {
        *pfChanged = false;
        Log2(("queryNewPasteboardFormats: no change\n"));
        return VINF_SUCCESS;
    }

    /* Are some items in the pasteboard? */
    ItemCount cItems = 0;
    orc = PasteboardGetItemCount(hPasteboard, &cItems);
    if (orc == 0)
    {
        if (cItems < 1)
            Log(("queryNewPasteboardFormats: changed: No items on the pasteboard\n"));
        else
        {
            /* The id of the first element in the pasteboard */
            PasteboardItemID idItem = 0;
            orc = PasteboardGetItemIdentifier(hPasteboard, 1, &idItem);
            if (orc == 0)
            {
                /*
                 * Retrieve all flavors on the pasteboard, maybe there
                 * is something we can use.  Or maybe we're the owner.
                 */
                CFArrayRef hFlavors = 0;
                orc = PasteboardCopyItemFlavors(hPasteboard, idItem, &hFlavors);
                if (orc == 0)
                {
                    CFIndex cFlavors = CFArrayGetCount(hFlavors);
                    for (CFIndex idxFlavor = 0; idxFlavor < cFlavors; idxFlavor++)
                    {
                        CFStringRef hStrFlavor = (CFStringRef)CFArrayGetValueAtIndex(hFlavors, idxFlavor);
                        if (   idItem == (PasteboardItemID)idOwnership
                            && hStrOwnershipFlavor
                            && CFStringCompare(hStrFlavor, (CFStringRef)hStrOwnershipFlavor, 0) == kCFCompareEqualTo)
                        {
                            /* We made the changes ourselves. */
                            Log2(("queryNewPasteboardFormats: no-changed: our clipboard!\n"));
                            *pfChanged = false;
                            *pfFormats = 0;
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
#ifdef LOG_ENABLED
                        else if (LogIs2Enabled())
                        {
                            if (CFStringGetCharactersPtr(hStrFlavor))
                                Log2(("queryNewPasteboardFormats: Unknown flavor: %ls.\n", CFStringGetCharactersPtr(hStrFlavor)));
                            else if (CFStringGetCStringPtr(hStrFlavor, kCFStringEncodingUTF8))
                                Log2(("queryNewPasteboardFormats: Unknown flavor: %s.\n",
                                      CFStringGetCStringPtr(hStrFlavor, kCFStringEncodingUTF8)));
                            else
                                Log2(("queryNewPasteboardFormats: Unknown flavor: ???\n"));
                        }
#endif
                    }

                    CFRelease(hFlavors);
                }
                else
                    Log(("queryNewPasteboardFormats: PasteboardCopyItemFlavors failed - %d (%#x)\n", orc, orc));
            }
            else
                Log(("queryNewPasteboardFormats: PasteboardGetItemIdentifier failed - %d (%#x)\n", orc, orc));

            if (*pfChanged)
                Log(("queryNewPasteboardFormats: changed: *pfFormats=%#x\n", *pfFormats));
        }
    }
    else
        Log(("queryNewPasteboardFormats: PasteboardGetItemCount failed - %d (%#x)\n", orc, orc));
    return VINF_SUCCESS;
}

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
    int rc = VERR_NOT_SUPPORTED;

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
                            rc = VERR_NO_UTF16_MEMORY;
                            pwszSrc = NULL;
                        }
                    }
                }
                else
                    rc = VERR_GENERAL_FAILURE;
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
                        rc = RTStrToUtf16Ex(pszSrc, cchSrc, &pwszSrcFree, 0, &cwcSrc);
                        if (RT_SUCCESS(rc))
                            pwszSrc = pwszSrcFree;
                    }
                    else
                        rc = VERR_GENERAL_FAILURE;
                }
            }
            if (pwszSrc)
            {
                /*
                 * Convert to windows UTF-16.
                 */
                Assert(cwcSrc == RTUtf16Len(pwszSrc));
                size_t cwcDst = 0;
                rc = ShClUtf16CalcNormalizedEolToCRLFLength(pwszSrc, cwcSrc, &cwcDst);
                if (RT_SUCCESS(rc))
                {
                    cwcDst++; /* Add space for terminator. */

                    *pcbActual = cwcDst * sizeof(RTUTF16);
                    if (*pcbActual <= cb)
                    {
                        rc = ShClConvUtf16LFToCRLF(pwszSrc, cwcSrc, (PRTUTF16)pv, cb / sizeof(RTUTF16));
                        if (RT_SUCCESS(rc))
                        {
#ifdef SHOW_CLIPBOARD_CONTENT
                            Log(("readFromPasteboard: clipboard content: %ls\n", (PCRTUTF16)pv));
#endif
                        }
                        else
                        {
                            Log(("readFromPasteboard: ShClUtf16LinToWin failed - %Rrc!\n", rc));
                            AssertRC(rc);
                        }
                    }
                    else
                    {
                        Log(("readFromPasteboard: Insufficient (text) buffer space: %#zx, need %#zx\n", cb, *pcbActual));
                        rc = VINF_SUCCESS;
                    }
                }
                else
                {
                    Log(("readFromPasteboard: ShClUtf16GetWinSize failed - %Rrc!\n", rc));
                    AssertRC(rc);
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
                    rc = ShClBmpGetDib(pvSrc, cbDataCopy, &pvDib, &cbDib);
                    if (RT_SUCCESS(rc))
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
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        AssertRC(rc);
                        Log(("readFromPasteboard: ShClBmpGetDib failed - %Rrc - unknown bitmap format??\n", rc));
                        rc = VERR_NOT_SUPPORTED;
                    }
                }
                else
                    rc = VERR_GENERAL_FAILURE;
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
                    rc = RTStrValidateEncodingEx(pszSrc, cbDataCopy, 0 /*fFlags*/);
                    if (RT_SUCCESS(rc))
                    {
                        size_t cchSrc = RTStrNLen(pszSrc, cbDataCopy);
                        *pcbActual = cchSrc;
                        if (cchSrc <= cb)
                            memcpy(pv, pszSrc, cchSrc);
                        else
                            Log(("readFromPasteboard: Insufficient (HTML) buffer space: %#zx, need %#zx\n", cb, cchSrc));
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        Log(("readFromPasteboard: Invalid UTF-8 encoding on pasteboard: %Rrc\n", rc));
                        rc = VERR_NOT_SUPPORTED;
                    }
                }
                else
                    rc = VERR_GENERAL_FAILURE;
            }
            else
                LogFlow(("readFromPasteboard: PasteboardCopyItemFlavorData/kUTTypeHTML -> %d (%#x)\n", orc, orc));
        }
#endif
        else
        {
            Log2(("readFromPasteboard: Unsupported format: %#x\n", fFormat));
            rc = VERR_NOT_SUPPORTED;
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
        rc = VERR_NOT_SUPPORTED;
    }

    Log(("readFromPasteboard: rc=%Rrc *pcbActual=%#zx\n", rc, *pcbActual));
    return rc;
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
    /*
     * Release the old string.
     */
    if (*phStrOwnershipFlavor)
    {
        CFStringRef hOldFlavor = (CFStringRef)*phStrOwnershipFlavor;
        CFRelease(hOldFlavor);
        *phStrOwnershipFlavor = NULL;
    }

    /*
     * Clear the pasteboard and take ownership over it.
     */
    OSStatus orc = PasteboardClear(hPasteboard);
    if (orc == 0)
    {
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
    }
    else
        Log(("takePasteboardOwnership: PasteboardClear failed -> %d (%#x)\n", orc, orc));
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
    int       rc;
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
        rc = ShClUtf16CRLFLenUtf8(pwszSrc, cwcSrc, &cwcDst);
        AssertMsgRCReturn(rc, ("ShClUtf16GetLinSize failed: %Rrc\n", rc), rc);

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

        rc = ShClConvUtf16CRLFToLF(pwszSrc, cwcSrc, pwszDst, cwcDst);
        if (RT_SUCCESS(rc))
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
                    rc = VINF_SUCCESS;
                else
                {
                    Log(("writeToPasteboard: PasteboardPutItemFlavor/kUTTypeUTF16PlainText failed: %d (%#x)\n", orc, orc));
                    rc = VERR_GENERAL_FAILURE;
                }
                CFRelease(hData);
            }
            else
            {
                Log(("writeToPasteboard: CFDataCreate/UTF16 failed!\n"));
                rc = VERR_NO_MEMORY;
            }

            /*
             * Now for the UTF-8 version.
             */
            char *pszDst;
            int rc2 = RTUtf16ToUtf8(pwszDst, &pszDst);
            if (RT_SUCCESS(rc2))
            {
                hData = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)pszDst, strlen(pszDst));
                if (hData)
                {
                    orc = PasteboardPutItemFlavor(hPasteboard, (PasteboardItemID)idOwnership,
                                                  kUTTypeUTF8PlainText, hData, kPasteboardFlavorNoFlags);
                    if (orc != 0)
                    {
                        Log(("writeToPasteboard: PasteboardPutItemFlavor/kUTTypeUTF8PlainText failed: %d (%#x)\n", orc, orc));
                        rc = VERR_GENERAL_FAILURE;
                    }
                    CFRelease(hData);
                }
                else
                {
                    Log(("writeToPasteboard: CFDataCreate/UTF8 failed!\n"));
                    rc = VERR_NO_MEMORY;
                }
                RTStrFree(pszDst);
            }
            else
                rc = rc2;
        }
        else
            Log(("writeToPasteboard: clipboard conversion failed.  vboxClipboardUtf16WinToLin() returned %Rrc.  Abandoning.\n", rc));

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
        rc = ShClDibToBmp(pv, cb, &pvBmp, &cbBmp);
        if (RT_SUCCESS(rc))
        {
            hData = CFDataCreate(kCFAllocatorDefault, (UInt8 const *)pvBmp, cbBmp);
            if (hData)
            {
                orc = PasteboardPutItemFlavor(hPasteboard, (PasteboardItemID)idOwnership,
                                              kUTTypeBMP, hData, kPasteboardFlavorNoFlags);
                if (orc != 0)
                {
                    Log(("writeToPasteboard: PasteboardPutItemFlavor/kUTTypeBMP failed: %d (%#x)\n", orc, orc));
                    rc = VERR_GENERAL_FAILURE;
                }
                CFRelease(hData);
            }
            else
            {
                Log(("writeToPasteboard: CFDataCreate/UTF8 failed!\n"));
                rc = VERR_NO_MEMORY;
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
        rc = RTStrValidateEncodingEx(pszSrc, cchSrc, 0);
        if (RT_SUCCESS(rc))
        {
            hData = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)pszSrc, cchSrc);
            if (hData)
            {
                orc = PasteboardPutItemFlavor(hPasteboard, (PasteboardItemID)idOwnership, kUTTypeHTML,
                                              hData, kPasteboardFlavorNoFlags);
                if (orc == 0)
                    rc = VINF_SUCCESS;
                else
                {
                    Log(("writeToPasteboard: PasteboardPutItemFlavor/kUTTypeHTML failed: %d (%#x)\n", orc, orc));
                    rc = VERR_GENERAL_FAILURE;
                }
                CFRelease(hData);
            }
            else
            {
                Log(("writeToPasteboard: CFDataCreate/HTML failed!\n"));
                rc = VERR_NO_MEMORY;
            }
        }
        else
            Log(("writeToPasteboard: HTML: Invalid UTF-8 encoding: %Rrc\n", rc));
    }
#endif
    else
        rc = VERR_NOT_IMPLEMENTED;

    Log(("writeToPasteboard: rc=%Rrc\n", rc));
    return rc;
}

RT_GCC_NO_WARN_DEPRECATED_END
