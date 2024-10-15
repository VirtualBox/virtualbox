/** $Id: VBoxClientClipboardGuestToHost.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxClient - Shared Clipboard Guest -> Host copying, Darwin.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <Carbon/Carbon.h>
#include <signal.h>
#include <stdlib.h>

#include <iprt/thread.h>
#include <iprt/mem.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/utf16.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include "VBoxClientInternal.h"

RT_GCC_NO_WARN_DEPRECATED_BEGIN /* Much here is deprecated since 12.0 */

/**
 * Walk through pasteboard items and report currently available item types.
 *
 * @param   pPasteboard         Reference to guest Pasteboard.
 * @returns Available formats bit field.
 */
uint32_t vbclClipboardGetAvailableFormats(PasteboardRef pPasteboard)
{
    uint32_t  fFormats = 0;
    ItemCount cItems   = 0;
    ItemCount iItem;
    OSStatus  rc;

#define VBOXCL_ADD_FORMAT_IF_PRESENT(a_kDarwinFmt, a_fVBoxFmt) \
    if (PasteboardCopyItemFlavorData(pPasteboard, iItemID, a_kDarwinFmt, &flavorData) == noErr) \
    { \
        fFormats |= (uint32_t)a_fVBoxFmt; \
        CFRelease(flavorData); \
    }

    rc = PasteboardGetItemCount(pPasteboard, &cItems);
    AssertReturn((rc == noErr) && (cItems > 0), fFormats);

    for (iItem = 1; iItem <= cItems; iItem++)
    {
        PasteboardItemID iItemID;
        CFDataRef        flavorData;

        rc = PasteboardGetItemIdentifier(pPasteboard, iItem, &iItemID);
        if (rc == noErr)
        {
            VBOXCL_ADD_FORMAT_IF_PRESENT(kUTTypeUTF16PlainText, VBOX_SHCL_FMT_UNICODETEXT);
            VBOXCL_ADD_FORMAT_IF_PRESENT(kUTTypeUTF8PlainText,  VBOX_SHCL_FMT_UNICODETEXT);
            VBOXCL_ADD_FORMAT_IF_PRESENT(kUTTypeBMP,            VBOX_SHCL_FMT_BITMAP     );
            VBOXCL_ADD_FORMAT_IF_PRESENT(kUTTypeHTML,           VBOX_SHCL_FMT_HTML       );

#ifdef CLIPBOARD_DUMP_CONTENT_FORMATS
            CFArrayRef  flavorTypeArray;
            CFIndex     flavorCount;
            CFStringRef flavorType;

            rc = PasteboardCopyItemFlavors(pPasteboard, iItemID, &flavorTypeArray);
            if (rc == noErr)
            {
                VBoxClientVerbose(3, "SCAN..\n");
                flavorCount = CFArrayGetCount(flavorTypeArray);
                VBoxClientVerbose(3, "SCAN (%d)..\n", (int)flavorCount);
                for(CFIndex flavorIndex = 0; flavorIndex < flavorCount; flavorIndex++)
                {
                    VBoxClientVerbose(3, "SCAN #%d..\n", (int)flavorIndex);
                    flavorType = (CFStringRef)CFArrayGetValueAtIndex(flavorTypeArray, flavorIndex);

                    CFDataRef flavorData1;
                    rc = PasteboardCopyItemFlavorData(pPasteboard, iItemID, flavorType, &flavorData1);
                    if (rc == noErr)
                    {
                        VBoxClientVerbose(3, "Found: %s, size: %d\n", (char *)CFStringGetCStringPtr(flavorType, kCFStringEncodingMacRoman), (int)CFDataGetLength(flavorData1));
                        CFRelease(flavorData1);
                    }
                }
                VBoxClientVerbose(3, "SCAN COMPLETE\n");
                CFRelease(flavorTypeArray);
            }
#endif /* CLIPBOARD_DUMP_CONTENT_FORMATS */
        }
    }

#undef VBOXCL_ADD_FORMAT_IF_PRESENT

    return fFormats;
}


/**
 * Search for content of specified type in guest clipboard buffer and put
 * it into newly allocated buffer.
 *
 * @param   pPasteboard     Guest PasteBoard reference.
 * @param   fFormat         Data formats we are looking for.
 * @param   ppvData         Where to return pointer to the received data. M
 * @param   pcbData         Where to return the size of the data.
 * @param   pcbAlloc        Where to return the size of the memory block
 *                          *ppvData pointes to. (Usually greater than *cbData
 *                           because the allocation is page aligned.)
 * @returns IPRT status code.
 */
static int vbclClipboardReadGuestData(PasteboardRef pPasteboard, CFStringRef sFormat, void **ppvData, uint32_t *pcbData,
                                      uint32_t *pcbAlloc)
{
    ItemCount cItems, iItem;
    OSStatus  rc;

    void     *pvData  = NULL;
    uint32_t  cbData  = 0;
    uint32_t  cbAlloc = 0;

    AssertPtrReturn(ppvData, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbData, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbAlloc, VERR_INVALID_POINTER);

    rc = PasteboardGetItemCount(pPasteboard, &cItems);
    AssertReturn(rc == noErr, VERR_INVALID_PARAMETER);
    AssertReturn(cItems > 0, VERR_INVALID_PARAMETER);

    /* Walk through all the items in PasteBoard in order to find
       that one that correcponds to requested data format. */
    for (iItem = 1; iItem <= cItems; iItem++)
    {
        PasteboardItemID iItemID;
        CFDataRef        flavorData;

        /* Now, get the item's flavors that corresponds to requested type. */
        rc = PasteboardGetItemIdentifier(pPasteboard, iItem, &iItemID);
        AssertReturn(rc == noErr, VERR_INVALID_PARAMETER);
        rc = PasteboardCopyItemFlavorData(pPasteboard, iItemID, sFormat, &flavorData);
        if (rc == noErr)
        {
            void *flavorDataPtr = (void *)CFDataGetBytePtr(flavorData);
            cbData = CFDataGetLength(flavorData);
            if (flavorDataPtr && cbData > 0)
            {
                cbAlloc = RT_ALIGN_32(cbData, PAGE_SIZE);
                pvData = RTMemPageAllocZ(cbAlloc);
                if (pvData)
                    memcpy(pvData, flavorDataPtr, cbData);
            }

            CFRelease(flavorData);

            /* Found first matching item, no more search. */
            break;
        }

    }

    /* Found match */
    if (pvData)
    {
        *ppvData  = pvData;
        *pcbData  = cbData;
        *pcbAlloc = cbAlloc;

        return VINF_SUCCESS;
    }

    return VERR_INVALID_PARAMETER;
}


/**
 * Release resources occupied by vbclClipboardReadGuestData().
 */
static void vbclClipboardReleaseGuestData(void **ppvData, uint32_t cbAlloc)
{
    AssertReturnVoid(ppvData);
    RTMemPageFree(*ppvData, cbAlloc);
    *ppvData = NULL;
}

/**
 * Pass data to host.
 */
static int vbclClipboardHostPasteData(uint32_t u32ClientId, uint32_t u32Format, const void *pvData, uint32_t cbData)
{
    /* Allow empty buffers */
    if (cbData == 0)
        return VbglR3ClipboardWriteData(u32ClientId, u32Format, NULL, 0);

    AssertReturn(pvData, VERR_INVALID_PARAMETER);
    return VbglR3ClipboardWriteData(u32ClientId, u32Format, (void *)pvData, cbData); /** @todo r=bird: Why on earth does a write function like VbglR3ClipboardWriteData take a non-const parameter? */
}

/**
 * Paste text data into host clipboard.
 *
 * @param   u32ClientId     Host clipboard connection.
 * @param   pwszData        UTF-16 encoded string.
 * @param   cbData          The length of the string, in bytes, probably
 *                          including a terminating zero.
 */
static int vbclClipboardHostPasteText(uint32_t u32ClientId, PRTUTF16 pwszData, uint32_t cbData)
{
    AssertReturn(cbData > 0,  VERR_INVALID_PARAMETER);
    AssertPtrReturn(pwszData, VERR_INVALID_POINTER);

    size_t cwcTmp; /* (includes a schwarzenegger character) */
    int rc = ShClUtf16CalcNormalizedEolToCRLFLength(pwszData, cbData / sizeof(RTUTF16), &cwcTmp);
    AssertRCReturn(rc, rc);

    cwcTmp++; /* Add space for terminator. */

    PRTUTF16 pwszTmp = (PRTUTF16)RTMemAlloc(cwcTmp * sizeof(RTUTF16));
    AssertReturn(pwszTmp, VERR_NO_MEMORY);

    rc = ShClConvUtf16LFToCRLF(pwszData, cbData / sizeof(RTUTF16), pwszTmp, cwcTmp);
    if (RT_SUCCESS(rc))
        rc = vbclClipboardHostPasteData(u32ClientId, VBOX_SHCL_FMT_UNICODETEXT,
                                        pwszTmp, cwcTmp * sizeof(RTUTF16));

    RTMemFree(pwszTmp);

    return rc;
}


/**
 * Paste a bitmap onto the host clipboard.
 *
 * @param   u32ClientId     Host clipboard connection.
 * @param   pvData          The bitmap data.
 * @param   cbData          The size of the bitmap.
 */
static int vbclClipboardHostPasteBitmap(uint32_t u32ClientId, void *pvData, uint32_t cbData)
{
    const void   *pvDib;
    size_t        cbDib;
    int rc = ShClBmpGetDib(pvData, cbData, &pvDib, &cbDib);
    AssertRCReturn(rc, rc);

    rc = vbclClipboardHostPasteData(u32ClientId, VBOX_SHCL_FMT_BITMAP, pvDib, cbDib);

    return rc;
}


/**
 * Read guest's clipboard buffer and forward its content to host.
 *
 * @param   u32ClientId    Host clipboard connection.
 * @param   pPasteboard    Guest PasteBoard reference.
 * @param   fFormats       List of data formats (bit field) received from host.
 *
 * @returns IPRT status code.
 */
int vbclClipboardForwardToHost(uint32_t u32ClientId, PasteboardRef pPasteboard, uint32_t fFormats)
{
    int       rc = VINF_SUCCESS;

    void     *pvData  = NULL;
    uint32_t  cbData  = 0;
    uint32_t  cbAlloc = 0;

    VBoxClientVerbose(3, "vbclClipboardForwardToHost: %d\n", fFormats);

    /* Walk across all item(s) formats */
    uint32_t  fFormatsLeft = fFormats;
    while (fFormatsLeft)
    {
        if (fFormatsLeft & VBOX_SHCL_FMT_UNICODETEXT)
        {
            VBoxClientVerbose(3, "requested VBOX_SHCL_FMT_UNICODETEXT: %d\n", fFormats);

            RTUTF16 *pUtf16Str = NULL;

            /* First, try to get UTF16 encoded buffer */
            rc = vbclClipboardReadGuestData(pPasteboard, kUTTypeUTF16PlainText, &pvData, &cbData, &cbAlloc);
            if (RT_SUCCESS(rc))
            {
                rc = RTUtf16DupEx(&pUtf16Str, (PRTUTF16)pvData, 0);
                if (RT_FAILURE(rc))
                    pUtf16Str = NULL;
            }
            else /* Failed to get UTF16 buffer */
            {
                /* Then, try to get UTF8 encoded buffer */
                rc = vbclClipboardReadGuestData(pPasteboard, kUTTypeUTF8PlainText, &pvData, &cbData, &cbAlloc);
                if (RT_SUCCESS(rc))
                {
                    rc = RTStrToUtf16((const char *)pvData, &pUtf16Str);
                    if (RT_FAILURE(rc))
                        pUtf16Str = NULL;
                }
            }

            /* Finally, we got UTF16 encoded buffer */
            if (RT_SUCCESS(rc))
            {
                rc = vbclClipboardHostPasteText(u32ClientId, (PRTUTF16)pvData, cbData);

                if (pUtf16Str)
                {
                    RTUtf16Free(pUtf16Str);
                    pUtf16Str = NULL;
                }

                vbclClipboardReleaseGuestData(&pvData, cbAlloc);
            }
            else
            {
                /* No data found or error occurred: send empty buffer */
                rc = vbclClipboardHostPasteData(u32ClientId, VBOX_SHCL_FMT_UNICODETEXT, NULL, 0);
            }

            fFormatsLeft &= ~(uint32_t)VBOX_SHCL_FMT_UNICODETEXT;
        }

        else if (fFormatsLeft & VBOX_SHCL_FMT_BITMAP)
        {
            VBoxClientVerbose(3, "requested VBOX_SHCL_FMT_BITMAP: %d\n", fFormats);

            rc = vbclClipboardReadGuestData(pPasteboard, kUTTypeBMP, &pvData, &cbData, &cbAlloc);
            if (RT_SUCCESS(rc))
            {
                rc = vbclClipboardHostPasteBitmap(u32ClientId, pvData, cbData);
                vbclClipboardReleaseGuestData(&pvData, cbAlloc);
            }
            else
            {
                /* No data found or error occurred: send empty buffer */
                rc = vbclClipboardHostPasteData(u32ClientId, VBOX_SHCL_FMT_BITMAP, NULL, 0);
            }

            fFormatsLeft &= ~(uint32_t)VBOX_SHCL_FMT_BITMAP;
        }

        else if (fFormatsLeft & VBOX_SHCL_FMT_HTML)
        {
            VBoxClientVerbose(3, "requested VBOX_SHCL_FMT_HTML: %d\n", fFormats);

            rc = vbclClipboardReadGuestData(pPasteboard, kUTTypeHTML, &pvData, &cbData, &cbAlloc);
            if (RT_SUCCESS(rc))
            {
                rc = vbclClipboardHostPasteData(u32ClientId, VBOX_SHCL_FMT_HTML, pvData, cbData);
                vbclClipboardReleaseGuestData(&pvData, cbAlloc);
            }
            else
            {
                /* No data found or error occurred: send empty buffer */
                rc = vbclClipboardHostPasteData(u32ClientId, VBOX_SHCL_FMT_HTML, NULL, 0);
            }

            fFormatsLeft &= ~(uint32_t)VBOX_SHCL_FMT_HTML;
        }

        else
        {
            VBoxClientVerbose(3, "requested data in unsupported format: %#x\n", fFormatsLeft);
            break;
        }
    }

    return rc; /** @todo r=bird: If there are multiple formats available, which rc is returned here? Does it matter? */
}

RT_GCC_NO_WARN_DEPRECATED_END
