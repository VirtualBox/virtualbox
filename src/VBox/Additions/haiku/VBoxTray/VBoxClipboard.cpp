/* $Id: VBoxClipboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxClipboard; Haiku Guest Additions, implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    Fran�ois Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Clipboard.h>
#include <DataIO.h>
#include <Message.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <String.h>

#include "VBoxGuestApplication.h"
#include "VBoxClipboard.h"
#include <VBoxGuestInternal.h>

#include <iprt/mem.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/log.h>

/** @todo r=ramshankar: this hack should go eventually. */
#ifdef DEBUG_ramshankar
# undef Log
# define Log(x) printf x
# undef LogRel
# define LogRel(x) printf x
# undef LogRelFlowFunc
# define LogRelFlowFunc(x) printf x
#endif


VBoxShClService::VBoxShClService()
     : BHandler("VBoxShClService"),
       fClientId(-1),
       fServiceThreadID(-1),
       fExiting(false)
{
}


VBoxShClService::~VBoxShClService()
{
}


status_t VBoxShClService::Connect()
{
    status_t err;
    LogFlowFunc(("Connect\n"));

    int rc = VbglR3ClipboardConnect(&fClientId);
    if (RT_SUCCESS(rc))
    {
        err = fServiceThreadID = spawn_thread(_ServiceThreadNub, "VBoxShClService", B_NORMAL_PRIORITY, this);
        if (err >= B_OK)
        {
            resume_thread(fServiceThreadID);
            err = be_clipboard->StartWatching(BMessenger(this));
            LogFlow(("be_clipboard->StartWatching: %ld\n", err));
            if (err == B_OK)
                return B_OK;
            else
                LogRel(("VBoxShClService: Error watching the system clipboard: %ld\n", err));
        }
        else
            LogRel(("VBoxShClService: Error starting service thread: %ld\n", err));

        //rc = RTErrConvertFromErrno(err);
        VbglR3ClipboardDisconnect(fClientId);
    }
    else
        LogRel(("VBoxShClService: Error starting service thread: %d\n", rc));
    return B_ERROR;
}


status_t VBoxShClService::Disconnect()
{
    status_t status;

    be_clipboard->StopWatching(BMessenger(this));

    fExiting = true;

    VbglR3ClipboardDisconnect(fClientId);

    wait_for_thread(fServiceThreadID, &status);
    return B_OK;
}


void VBoxShClService::MessageReceived(BMessage *message)
{
    uint32_t formats = 0;
    message->PrintToStream();
    switch (message->what)
    {
        case VBOX_GUEST_CLIPBOARD_HOST_MSG_FORMATS:
        {
            int rc;
            uint32_t cb;
            void *pv;
            bool commit = false;

            if (message->FindInt32("Formats", (int32 *)&formats) != B_OK)
                break;

            if (!formats)
                break;

            if (!be_clipboard->Lock())
                break;

            be_clipboard->Clear();
            BMessage *clip = be_clipboard->Data();
            if (!clip)
            {
                be_clipboard->Unlock();
                break;
            }

            if (formats & VBOX_SHCL_FMT_UNICODETEXT)
            {
                pv = _VBoxReadHostClipboard(VBOX_SHCL_FMT_UNICODETEXT, &cb);
                if (pv)
                {
                    char *text;
                    rc = RTUtf16ToUtf8((PCRTUTF16)pv, &text);
                    if (RT_SUCCESS(rc))
                    {
                        BString str(text);
                        /** @todo user vboxClipboardUtf16WinToLin() */
                        // convert Windows CRLF to LF
                        str.ReplaceAll("\r\n", "\n");
                        // don't include the \0
                        clip->AddData("text/plain", B_MIME_TYPE, str.String(), str.Length());
                        RTStrFree(text);
                        commit = true;
                    }
                    free(pv);
                }
            }

            if (formats & VBOX_SHCL_FMT_BITMAP)
            {
                pv = _VBoxReadHostClipboard(VBOX_SHCL_FMT_BITMAP, &cb);
                if (pv)
                {
                    void  *pBmp  = NULL;
                    size_t cbBmp = 0;
                    rc = ShClDibToBmp(pv, cb, &pBmp, &cbBmp);
                    if (RT_SUCCESS(rc))
                    {
                        BMemoryIO mio(pBmp, cbBmp);
                        BBitmap *bitmap = BTranslationUtils::GetBitmap(&mio);
                        if (bitmap)
                        {
                            BMessage bitmapArchive;

                            /** @todo r=ramshankar: split this into functions with error checking as
                             *        neccessary. */
                            if (   bitmap->IsValid()
                                && bitmap->Archive(&bitmapArchive) == B_OK
                                && clip->AddMessage("image/bitmap", &bitmapArchive) == B_OK)
                            {
                                commit = true;
                            }
                            delete bitmap;
                        }
                        RTMemFree(pBmp);
                    }
                    free(pv);
                }
            }

            /*
             * Make sure we don't bounce this data back to the host, it's impolite. It can also
             * be used as a hint to applications probably.
             */
            clip->AddBool("FromVirtualBoxHost", true);
            if (commit)
                be_clipboard->Commit();
            be_clipboard->Unlock();
            break;
        }

        case VBOX_GUEST_CLIPBOARD_HOST_MSG_READ_DATA:
        {
            int rc;

            if (message->FindInt32("Formats", (int32 *)&formats) != B_OK)
                break;

            if (!formats)
                break;
            if (!be_clipboard->Lock())
                break;

            BMessage *clip = be_clipboard->Data();
            if (!clip)
            {
                be_clipboard->Unlock();
                break;
            }
            clip->PrintToStream();

            if (formats & VBOX_SHCL_FMT_UNICODETEXT)
            {
                const char *text;
                int32 textLen;
                if (clip->FindData("text/plain", B_MIME_TYPE, (const void **)&text, &textLen) == B_OK)
                {
                    // usually doesn't include the \0 so be safe
                    BString str(text, textLen);
                    // convert from LF to Windows CRLF
                    str.ReplaceAll("\n", "\r\n");
                    PRTUTF16 pwsz;
                    rc = RTStrToUtf16(str.String(), &pwsz);
                    if (RT_SUCCESS(rc))
                    {
                        uint32_t cb = (RTUtf16Len(pwsz) + 1) * sizeof(RTUTF16);

                        rc = VbglR3ClipboardWriteData(fClientId, VBOX_SHCL_FMT_UNICODETEXT, pwsz, cb);
                        //printf("VbglR3ClipboardWriteData: %d\n", rc);
                        RTUtf16Free(pwsz);
                    }
                }
            }
            else if (formats & VBOX_SHCL_FMT_BITMAP)
            {
                BMessage archivedBitmap;
                if (clip->FindMessage("image/bitmap", &archivedBitmap) == B_OK ||
                    clip->FindMessage("image/x-be-bitmap", &archivedBitmap) == B_OK)
                {
                    BBitmap *bitmap = new(std::nothrow) BBitmap(&archivedBitmap);
                    if (bitmap)
                    {
                        // Don't delete bitmap, BBitmapStream will.
                        BBitmapStream stream(bitmap);
                        BTranslatorRoster *roster = BTranslatorRoster::Default();
                        if (roster && bitmap->IsValid())
                        {
                            BMallocIO bmpStream;
                            if (roster->Translate(&stream, NULL, NULL, &bmpStream, B_BMP_FORMAT) == B_OK)
                            {
                                const void *pDib;
                                size_t cbDibSize;
                                /* Strip out the BM header */
                                rc = ShClBmpGetDib(bmpStream.Buffer(), bmpStream.BufferLength(), &pDib, &cbDibSize);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = VbglR3ClipboardWriteData(fClientId, VBOX_SHCL_FMT_BITMAP, (void *)pDib,
                                                                  cbDibSize);
                                }
                            }
                        }
                    }
                }
            }

            be_clipboard->Unlock();
            break;
        }

        case B_CLIPBOARD_CHANGED:
        {
            printf("B_CLIPBOARD_CHANGED\n");
            const void *data;
            int32 dataLen;
            if (!be_clipboard->Lock())
                break;

            BMessage *clip = be_clipboard->Data();
            if (!clip)
            {
                be_clipboard->Unlock();
                break;
            }

            bool fromVBox;
            if (clip->FindBool("FromVirtualBoxHost", &fromVBox) == B_OK && fromVBox)
            {
                // It already comes from the host, discard.
                be_clipboard->Unlock();
                break;
            }

            if (clip->FindData("text/plain", B_MIME_TYPE, &data, &dataLen) == B_OK)
                formats |= VBOX_SHCL_FMT_UNICODETEXT;

            if (   clip->HasMessage("image/bitmap")
                || clip->HasMessage("image/x-be-bitmap"))
            {
                formats |= VBOX_SHCL_FMT_BITMAP;
            }

            be_clipboard->Unlock();

            VbglR3ClipboardReportFormats(fClientId, formats);
            break;
        }

        case B_QUIT_REQUESTED:
            fExiting = true;
            break;

        default:
            BHandler::MessageReceived(message);
    }
}


status_t VBoxShClService::_ServiceThreadNub(void *_this)
{
    VBoxShClService *service = (VBoxShClService *)_this;
    return service->_ServiceThread();
}


status_t VBoxShClService::_ServiceThread()
{
    printf("VBoxShClService::%s()\n", __FUNCTION__);

    /* The thread waits for incoming messages from the host. */
    for (;;)
    {
        uint32_t u32Msg;
        uint32_t u32Formats;
        int rc = VbglR3ClipboardGetHostMsgOld(fClientId, &u32Msg, &u32Formats);
        if (RT_SUCCESS(rc))
        {
            switch (u32Msg)
            {
                case VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
                {
                    /*
                     * The host has announced available clipboard formats. Forward
                     * the information to the handler.
                     */
                    LogRelFlowFunc(("VBOX_SHCL_HOST_MSG_REPORT_FORMATS u32Formats=%x\n", u32Formats));
                    BMessage msg(VBOX_GUEST_CLIPBOARD_HOST_MSG_FORMATS);
                    msg.AddInt32("Formats", (uint32)u32Formats);
                    Looper()->PostMessage(&msg, this);
                    break;
                }

                case VBOX_SHCL_HOST_MSG_READ_DATA:
                {
                    /* The host needs data in the specified format. */
                    LogRelFlowFunc(("VBOX_SHCL_HOST_MSG_READ_DATA u32Formats=%x\n", u32Formats));
                    BMessage msg(VBOX_GUEST_CLIPBOARD_HOST_MSG_READ_DATA);
                    msg.AddInt32("Formats", (uint32)u32Formats);
                    Looper()->PostMessage(&msg, this);
                    break;
                }

                case VBOX_SHCL_HOST_MSG_QUIT:
                {
                    /* The host is terminating. */
                    LogRelFlowFunc(("VBOX_SHCL_HOST_MSG_QUIT\n"));
                    fExiting = true;
                    return VERR_INTERRUPTED;
                }

                default:
                    Log(("VBoxShClService::%s: Unsupported message from host! Message = %u\n", __FUNCTION__, u32Msg));
            }
        }
        else
            fExiting = true;

        LogRelFlow(("processed host event rc = %d\n", rc));

        if (fExiting)
            break;
    }
    return 0;
}


void* VBoxShClService::_VBoxReadHostClipboard(uint32_t format, uint32_t *pcb)
{
    uint32_t cb = 1024;
    void *pv;
    int rc;

    pv = malloc(cb);
    if (pv == NULL)
        return NULL;

    rc = VbglR3ClipboardReadData(fClientId, format, pv, cb, pcb);
    if (RT_SUCCESS(rc) && (rc != VINF_BUFFER_OVERFLOW))
        return pv;
    if (rc == VINF_BUFFER_OVERFLOW)
    {
        free(pv);
        cb = *pcb;
        pv = malloc(cb);
        if (pv == NULL)
            return NULL;

        rc = VbglR3ClipboardReadData(fClientId, format, pv, cb, pcb);
        if (RT_SUCCESS(rc) && (rc != VINF_BUFFER_OVERFLOW))
            return pv;

        free(pv);
    }
    return NULL;
}

