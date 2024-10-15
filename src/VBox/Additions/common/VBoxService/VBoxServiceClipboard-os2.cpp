/** $Id: VBoxServiceClipboard-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxService - Guest Additions Clipboard Service, OS/2.
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


/** @page pg_vgsvc_clipboard VBoxService - Clipboard (OS/2)
 *
 * The Clipboard subservice provides clipboard sharing for OS/2 guests only.
 *
 * This was the second subservice that was added to VBoxService.  OS/2 is a
 * single user system and we don't provide any VBoxTray or VBoxClient like
 * processes. Because it's kind of simple system, it became natural to put the
 * clipboard sharing here in VBoxService for OS/2.
 *
 * In addition to integrating with the native OS/2 PM clipboard formats, we also
 * try provide the Odin32, a windows API layer for OS/2 (developed by Sander van
 * Leeuwen and friends, later mainly InnoTek), with additional formats.
 *
 * Bitmaps are currently not supported, but that can easily be added should the
 * need ever arrise.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define INCL_BASE
#define INCL_PM
#define INCL_ERRORS
#include <os2.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/utf16.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include "VBoxServiceInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Header for Odin32 specific clipboard entries.
 * (Used to get the correct size of the data.)
 */
typedef struct _Odin32ClipboardHeader
{
    /** magic number */
    char        achMagic[8];
    /** Size of the following data.
     * (The interpretation depends on the type.) */
    unsigned    cbData;
    /** Odin32 format number. */
    unsigned    uFormat;
} CLIPHEADER, *PCLIPHEADER;

#define CLIPHEADER_MAGIC "Odin\1\0\1"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** The control thread (main) handle.
 * Only used to avoid some queue creation trouble. */
static RTTHREAD g_ThreadCtrl = NIL_RTTHREAD;
/** The HAB of the control thread (main). */
static HAB g_habCtrl = NULLHANDLE;
/** The HMQ of the control thread (main). */
static HMQ g_hmqCtrl = NULLHANDLE;

/** The Listener thread handle. */
static RTTHREAD g_ThreadListener = NIL_RTTHREAD;
/** The HAB of the listener thread. */
static HAB g_habListener = NULLHANDLE;
/** The HMQ of the listener thread. */
static HMQ g_hmqListener = NULLHANDLE;
/** Indicator that gets set if the listener thread is successfully initialized. */
static bool volatile g_fListenerOkay = false;

/** The HAB of the worker thread. */
static HAB g_habWorker = NULLHANDLE;
/** The HMQ of the worker thread. */
static HMQ g_hmqWorker = NULLHANDLE;
/** The object window handle. */
static HWND g_hwndWorker = NULLHANDLE;
/** The timer id returned by WinStartTimer. */
static ULONG g_idWorkerTimer = ~0UL;
/** The state of the clipboard.
 * @remark I'm trying out the 'k' prefix from the mac here, bear with me.  */
static enum
{
    /** The clipboard hasn't been initialized yet. */
    kClipboardState_Uninitialized = 0,
    /** WinSetClipbrdViewer call in progress, ignore WM_DRAWCLIPBOARD. */
    kClipboardState_SettingViewer,
    /** We're monitoring the clipboard as a viewer. */
    kClipboardState_Viewer,
    /** We're monitoring the clipboard using polling.
     * This usually means something is wrong... */
    kClipboardState_Polling,
    /** We're destroying the clipboard content, ignore WM_DESTROYCLIPBOARD. */
    kClipboardState_Destroying,
    /** We're owning the clipboard (i.e. we have data on it). */
    kClipboardState_Owner
} g_enmState = kClipboardState_Uninitialized;
/** Set if the clipboard was empty the last time we polled it. */
static bool g_fEmptyClipboard = false;

/** A clipboard format atom for the dummy clipboard data we insert
 * watching for clipboard changes. If this format is found on the
 * clipboard, the empty clipboard function has not been called
 * since we last polled it. */
static ATOM g_atomNothingChanged = 0;

/** The clipboard connection client ID. */
static uint32_t g_u32ClientId;
/** Odin32 CF_UNICODETEXT. See user32.cpp. */
static ATOM g_atomOdin32UnicodeText = 0;
/** Odin32 CF_UNICODETEXT. See user32.cpp. */
#define SZFMT_ODIN32_UNICODETEXT    (PCSZ)"Odin32 UnicodeText"




/**
 * @interface_method_impl{VBOXSERVICE,pfnPreInit}
 */
static DECLCALLBACK(int) vgsvcClipboardOs2PreInit(void)
{
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnOption}
 */
static DECLCALLBACK(int) vgsvcClipboardOs2Option(const char **ppszShort, int argc, char **argv, int *pi)
{
    NOREF(ppszShort);
    NOREF(argc);
    NOREF(argv);
    NOREF(pi);

    return -1;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vgsvcClipboardOs2Init(void)
{
    int rc = VERR_GENERAL_FAILURE;
    g_ThreadCtrl = RTThreadSelf();

    /*
     * Make PM happy.
     */
    PPIB pPib;
    PTIB pTib;
    DosGetInfoBlocks(&pTib, &pPib);
    pPib->pib_ultype = 3; /* PM session type */

    /*
     * Since we have to send shutdown messages and such from the
     * service controller (main) thread, create a HAB and HMQ for it.
     */
    g_habCtrl = WinInitialize(0);
    if (g_habCtrl  == NULLHANDLE)
    {
        VGSvcError("WinInitialize(0) failed, lasterr=%lx\n", WinGetLastError(NULLHANDLE));
        return VERR_GENERAL_FAILURE;
    }
    g_hmqCtrl = WinCreateMsgQueue(g_habCtrl, 0);
    if (g_hmqCtrl != NULLHANDLE)
    {
        WinCancelShutdown(g_hmqCtrl, TRUE); /* We don't care about shutdown */

        /*
         * Create the 'nothing-changed' format.
         */
        g_atomNothingChanged = WinAddAtom(WinQuerySystemAtomTable(), (PCSZ)"VirtualBox Clipboard Service");
        LONG lLastError = WinGetLastError(g_habCtrl);
        if (g_atomNothingChanged == 0)
            g_atomNothingChanged = WinFindAtom(WinQuerySystemAtomTable(), (PCSZ)"VirtualBox Clipboard Service");
        if (g_atomNothingChanged)
        {
            /*
             * Connect to the clipboard service.
             */
            VGSvcVerbose(4, "clipboard: connecting\n");
            rc = VbglR3ClipboardConnect(&g_u32ClientId);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create any extra clipboard type atoms, like the odin unicode text.
                 */
                g_atomOdin32UnicodeText = WinAddAtom(WinQuerySystemAtomTable(), SZFMT_ODIN32_UNICODETEXT);
                lLastError = WinGetLastError(g_habCtrl);
                if (g_atomOdin32UnicodeText == 0)
                    g_atomOdin32UnicodeText = WinFindAtom(WinQuerySystemAtomTable(), SZFMT_ODIN32_UNICODETEXT);
                if (g_atomOdin32UnicodeText == 0)
                    VGSvcError("WinAddAtom() failed, lasterr=%lx; WinFindAtom() failed, lasterror=%lx\n",
                               lLastError, WinGetLastError(g_habCtrl));

                VGSvcVerbose(2, "g_u32ClientId=%RX32 g_atomNothingChanged=%#x g_atomOdin32UnicodeText=%#x\n",
                             g_u32ClientId, g_atomNothingChanged, g_atomOdin32UnicodeText);
                return VINF_SUCCESS;
            }

            VGSvcError("Failed to connect to the clipboard service, rc=%Rrc!\n", rc);
        }
        else
            VGSvcError("WinAddAtom() failed, lasterr=%lx; WinFindAtom() failed, lasterror=%lx\n",
                       lLastError, WinGetLastError(g_habCtrl));
    }
    else
        VGSvcError("WinCreateMsgQueue(,0) failed, lasterr=%lx\n", WinGetLastError(g_habCtrl));
    WinTerminate(g_habCtrl);
    return rc;
}


/**
 * Check that we're still the view / try make us the viewer.
 */
static void vgsvcClipboardOs2PollViewer(void)
{
    const int iOrgState = g_enmState;

    HWND hwndClipboardViewer = WinQueryClipbrdViewer(g_habWorker);
    if (hwndClipboardViewer == g_hwndWorker)
        return;

    if (hwndClipboardViewer == NULLHANDLE)
    {
        /* The API will send a WM_DRAWCLIPBOARD message before returning. */
        g_enmState = kClipboardState_SettingViewer;
        if (WinSetClipbrdViewer(g_habWorker, g_hwndWorker))
            g_enmState = kClipboardState_Viewer;
        else
            g_enmState = kClipboardState_Polling;
    }
    else
        g_enmState = kClipboardState_Polling;
    if ((int)g_enmState != iOrgState)
    {
        if (g_enmState == kClipboardState_Viewer)
            VGSvcVerbose(3, "clipboard: viewer\n");
        else
            VGSvcVerbose(3, "clipboard: poller\n");
    }
}


/**
 * Advertise the formats available from the host.
 *
 * @param   fFormats        The formats available on the host.
 */
static void vgsvcClipboardOs2AdvertiseHostFormats(uint32_t fFormats)
{
    /*
     * Open the clipboard and switch to 'destruction' mode.
     * Make sure we stop being viewer. Temporarily also make sure we're
     * not the owner so that PM won't send us any WM_DESTROYCLIPBOARD message.
     */
    if (WinOpenClipbrd(g_habWorker))
    {
        if (g_enmState == kClipboardState_Viewer)
            WinSetClipbrdViewer(g_habWorker, NULLHANDLE);
        if (g_enmState == kClipboardState_Owner)
            WinSetClipbrdOwner(g_habWorker, NULLHANDLE);

        g_enmState = kClipboardState_Destroying;
        if (WinEmptyClipbrd(g_habWorker))
        {
            /*
             * Take clipboard ownership.
             */
            if (WinSetClipbrdOwner(g_habWorker, g_hwndWorker))
            {
                g_enmState = kClipboardState_Owner;

                /*
                 * Do the format advertising.
                 */
                if (fFormats & (VBOX_SHCL_FMT_UNICODETEXT/* | VBOX_SHCL_FMT_HTML ?? */))
                {
                    if (!WinSetClipbrdData(g_habWorker, 0, CF_TEXT, CFI_POINTER))
                        VGSvcError("WinSetClipbrdData(,,CF_TEXT,) failed, lasterr=%lx\n", WinGetLastError(g_habWorker));
                    if (   g_atomOdin32UnicodeText
                        && !WinSetClipbrdData(g_habWorker, 0, g_atomOdin32UnicodeText, CFI_POINTER))
                        VGSvcError("WinSetClipbrdData(,,g_atomOdin32UnicodeText,) failed, lasterr=%lx\n", WinGetLastError(g_habWorker));
                }
                if (fFormats & VBOX_SHCL_FMT_BITMAP)
                {
                    /** @todo bitmaps */
                }
            }
            else
            {
                VGSvcError("WinSetClipbrdOwner failed, lasterr=%lx\n", WinGetLastError(g_habWorker));
                g_enmState = kClipboardState_Polling;
            }
        }
        else
        {
            VGSvcError("WinEmptyClipbrd failed, lasterr=%lx\n", WinGetLastError(g_habWorker));
            g_enmState = kClipboardState_Polling;
        }

        if (g_enmState == kClipboardState_Polling)
        {
            g_fEmptyClipboard = true;
            vgsvcClipboardOs2PollViewer();
        }

        WinCloseClipbrd(g_habWorker);
    }
    else
        VGSvcError("vgsvcClipboardOs2AdvertiseHostFormats: WinOpenClipbrd failed, lasterr=%lx\n", WinGetLastError(g_habWorker));
}


/**
 * Converts (render) to an Odin32 clipboard format.
 *
 * We ASSUME we get windows data from the host and all we've got to do here is
 * slapping an Odin32 header on it.
 *
 * @returns Pointer to the data (DosFreeMem).
 * @param   fFormat             The host format.
 * @param   usFmt               The PM/Odin32 format.
 * @param   pv                  The data in host formatting.
 * @param   cb                  The size of the data.
 */
static void *vgsvcClipboardOs2ConvertToOdin32(uint32_t fFormat, USHORT usFmt, void *pv, uint32_t cb)
{
    PVOID pvPM = NULL;
    APIRET rc = DosAllocSharedMem(&pvPM, NULL, cb + sizeof(CLIPHEADER), OBJ_GIVEABLE | OBJ_GETTABLE | OBJ_TILE | PAG_READ | PAG_WRITE | PAG_COMMIT);
    if (rc)
    {
        PCLIPHEADER pHdr = (PCLIPHEADER)pvPM;
        memcpy(pHdr->achMagic, CLIPHEADER_MAGIC, sizeof(pHdr->achMagic));
        pHdr->cbData = cb;
        if (usFmt == g_atomOdin32UnicodeText)
            pHdr->uFormat = usFmt;
        else
            AssertFailed();
        memcpy(pHdr + 1, pv, cb);
    }
    else
    {
        VGSvcError("DosAllocSharedMem(,,%#x,,) -> %ld\n", cb + sizeof(CLIPHEADER), rc);
        pvPM = NULL;
    }
    return pvPM;
}


/**
 * Converts (render) to a PM clipboard format.
 *
 * @returns Pointer to the data (DosFreeMem).
 * @param   fFormat             The host format.
 * @param   usFmt               The PM/Odin32 format.
 * @param   pv                  The data in host formatting.
 * @param   cb                  The size of the data.
 */
static void *vgsvcClipboardOs2ConvertToPM(uint32_t fFormat, USHORT usFmt, void *pv, uint32_t cb)
{
    void *pvPM = NULL;

    /*
     * The Odin32 stuff is simple, we just assume windows data from the host
     * and all we need to do is add the header.
     */
    if (    usFmt
        &&  (   usFmt == g_atomOdin32UnicodeText
             /* || usFmt == ...*/
            )
       )
        pvPM = vgsvcClipboardOs2ConvertToOdin32(fFormat, usFmt, pv, cb);
    else if (usFmt == CF_TEXT)
    {
        /*
         * Convert the unicode text to the current ctype locale.
         *
         * Note that we probably should be using the current PM or DOS codepage
         * here instead of the LC_CTYPE one which iconv uses by default.
         * -lazybird
         */
        Assert(fFormat & VBOX_SHCL_FMT_UNICODETEXT);
        char *pszUtf8;
        int rc = RTUtf16ToUtf8((PCRTUTF16)pv, &pszUtf8);
        if (RT_SUCCESS(rc))
        {
            char *pszLocale;
            rc = RTStrUtf8ToCurrentCP(&pszLocale, pszUtf8);
            if (RT_SUCCESS(rc))
            {
                size_t cbPM = strlen(pszLocale) + 1;
                APIRET orc = DosAllocSharedMem(&pvPM, NULL, cbPM, OBJ_GIVEABLE | OBJ_GETTABLE | OBJ_TILE | PAG_READ | PAG_WRITE | PAG_COMMIT);
                if (orc == NO_ERROR)
                    memcpy(pvPM, pszLocale, cbPM);
                else
                {
                    VGSvcError("DosAllocSharedMem(,,%#x,,) -> %ld\n", cb + sizeof(CLIPHEADER), orc);
                    pvPM = NULL;
                }
                RTStrFree(pszLocale);
            }
            else
                VGSvcError("RTStrUtf8ToCurrentCP() -> %Rrc\n", rc);
            RTStrFree(pszUtf8);
        }
        else
            VGSvcError("RTUtf16ToUtf8() -> %Rrc\n", rc);
    }

    return pvPM;
}


/**
 * Tries to deliver an advertised host format.
 *
 * @param   usFmt       The PM format name.
 *
 * @remark  We must not try open the clipboard here because WM_RENDERFMT is a
 *          request send synchronously by someone who has already opened the
 *          clipboard. We would enter a deadlock trying to open it here.
 */
static void vgsvcClipboardOs2RenderFormat(USHORT usFmt)
{
    bool fSucceeded = false;

    /*
     * Determine which format.
     */
    uint32_t fFormat;
    if (    usFmt == CF_TEXT
        ||  usFmt == g_atomOdin32UnicodeText)
        fFormat = VBOX_SHCL_FMT_UNICODETEXT;
    else /** @todo bitmaps */
        fFormat = 0;
    if (fFormat)
    {
        /*
         * Query the data from the host.
         * This might require two iterations because of buffer guessing.
         */
        uint32_t cb = _4K;
        uint32_t cbAllocated = cb;
        int      rc = VERR_NO_MEMORY;
        void    *pv = RTMemPageAllocZ(cbAllocated);
        if (pv)
        {
            VGSvcVerbose(4, "clipboard: reading host data (%#x)\n", fFormat);
            rc = VbglR3ClipboardReadData(g_u32ClientId, fFormat, pv, cb, &cb);
            if (rc == VINF_BUFFER_OVERFLOW)
            {
                RTMemPageFree(pv, cbAllocated);
                cbAllocated = cb = RT_ALIGN_32(cb, PAGE_SIZE);
                pv = RTMemPageAllocZ(cbAllocated);
                rc = VbglR3ClipboardReadData(g_u32ClientId, fFormat, pv, cb, &cb);
            }
            if (RT_FAILURE(rc))
                RTMemPageFree(pv, cbAllocated);
        }
        if (RT_SUCCESS(rc))
        {
            VGSvcVerbose(4, "clipboard: read %u bytes\n", cb);

            /*
             * Convert the host clipboard data to PM clipboard data and set it.
             */
            PVOID pvPM = vgsvcClipboardOs2ConvertToPM(fFormat, usFmt, pv, cb);
            if (pvPM)
            {
                if (WinSetClipbrdData(g_habWorker, (ULONG)pvPM, usFmt, CFI_POINTER))
                    fSucceeded = true;
                else
                {
                    VGSvcError("vgsvcClipboardOs2RenderFormat: WinSetClipbrdData(,%p,%#x, CF_POINTER) failed, lasterror=%lx\n",
                               pvPM, usFmt, WinGetLastError(g_habWorker));
                    DosFreeMem(pvPM);
                }
            }
            RTMemPageFree(pv, cbAllocated);
        }
        else
            VGSvcError("vgsvcClipboardOs2RenderFormat: Failed to query / allocate data. rc=%Rrc cb=%#RX32\n", rc, cb);
    }

    /*
     * Empty the clipboard on failure so we don't end up in any loops.
     */
    if (!fSucceeded)
    {
        WinSetClipbrdOwner(g_habWorker, NULLHANDLE);
        g_enmState = kClipboardState_Destroying;
        WinEmptyClipbrd(g_habWorker);
        g_enmState = kClipboardState_Polling;
        g_fEmptyClipboard = true;
        vgsvcClipboardOs2PollViewer();
    }
}


/**
 * Sends data to the host.
 *
 * @param   fFormat     The data format the host is requesting.
 */
static void vgsvcClipboardOs2SendDataToHost(uint32_t fFormat)
{
    if (WinOpenClipbrd(g_habWorker))
    {
        PRTUTF16 pwszFree = NULL;
        void *pv = NULL;
        uint32_t cb = 0;

        if (fFormat & VBOX_SHCL_FMT_UNICODETEXT)
        {
            /* Got any odin32 unicode text? */
            PVOID pvPM;
            PCLIPHEADER pHdr = (PCLIPHEADER)WinQueryClipbrdData(g_habWorker, g_atomOdin32UnicodeText);
            if (    pHdr
                &&  !memcmp(pHdr->achMagic, CLIPHEADER_MAGIC, sizeof(pHdr->achMagic)))
            {
                pv = pHdr + 1;
                cb = pHdr->cbData;
            }

            /* Got any CF_TEXT? */
            if (    !pv
                &&  (pvPM = (PVOID)WinQueryClipbrdData(g_habWorker, CF_TEXT)) != NULL)
            {
                char *pszUtf8;
                int rc = RTStrCurrentCPToUtf8(&pszUtf8, (const char *)pvPM);
                if (RT_SUCCESS(rc))
                {
                    PRTUTF16 pwsz;
                    rc = RTStrToUtf16(pszUtf8, &pwsz);
                    if (RT_SUCCESS(rc))
                    {
                        pv = pwszFree = pwsz;
                        cb = (RTUtf16Len(pwsz) + 1) * sizeof(RTUTF16);
                    }
                    RTStrFree(pszUtf8);
                }
            }
        }
        if (!pv)
            VGSvcError("vgsvcClipboardOs2SendDataToHost: couldn't find data for %#x\n", fFormat);

        /*
         * Now, sent whatever we've got to the host (it's waiting).
         */
        VGSvcVerbose(4, "clipboard: writing %pv/%#d (fFormat=%#x)\n", pv, cb, fFormat);
        VbglR3ClipboardWriteData(g_u32ClientId, fFormat, pv, cb);
        RTUtf16Free(pwszFree);

        WinCloseClipbrd(g_habWorker);
    }
    else
    {
        VGSvcError("vgsvcClipboardOs2SendDataToHost: WinOpenClipbrd failed, lasterr=%lx\n", WinGetLastError(g_habWorker));
        VGSvcVerbose(4, "clipboard: writing NULL/0 (fFormat=%x)\n", fFormat);
        VbglR3ClipboardWriteData(g_u32ClientId, fFormat, NULL, 0);
    }
}


/**
 * Figure out what's on the clipboard and report it to the host.
 */
static void vgsvcClipboardOs2ReportFormats(void)
{
    uint32_t fFormats = 0;
    ULONG ulFormat = 0;
    while ((ulFormat = WinEnumClipbrdFmts(g_habWorker, ulFormat)) != 0)
    {
        if (    ulFormat == CF_TEXT
            ||  ulFormat == g_atomOdin32UnicodeText)
            fFormats |= VBOX_SHCL_FMT_UNICODETEXT;
        /** @todo else bitmaps and stuff. */
    }
    VGSvcVerbose(4, "clipboard: reporting fFormats=%#x\n", fFormats);
    VbglR3ClipboardReportFormats(g_u32ClientId, fFormats);
}


/**
 * Poll the clipboard for changes.
 *
 * This is called both when we're the viewer and when we're
 * falling back to polling. If something has changed it will
 * notify the host.
 */
static void vgsvcClipboardOs2Poll(void)
{
    if (WinOpenClipbrd(g_habWorker))
    {
        /*
         * If our dummy is no longer there, something has actually changed,
         * unless the clipboard is really empty.
         */
        ULONG fFmtInfo;
        if (!WinQueryClipbrdFmtInfo(g_habWorker, g_atomNothingChanged, &fFmtInfo))
        {
            if (WinEnumClipbrdFmts(g_habWorker, 0) != 0)
            {
                g_fEmptyClipboard = false;
                vgsvcClipboardOs2ReportFormats();

                /* inject the dummy */
                PVOID pv;
                APIRET rc = DosAllocSharedMem(&pv, NULL, 1, OBJ_GIVEABLE | OBJ_GETTABLE | PAG_READ | PAG_WRITE | PAG_COMMIT);
                if (rc == NO_ERROR)
                {
                    if (WinSetClipbrdData(g_habWorker, (ULONG)pv, g_atomNothingChanged, CFI_POINTER))
                        VGSvcVerbose(4, "clipboard: Added dummy item.\n");
                    else
                    {
                        VGSvcError("vgsvcClipboardOs2Poll: WinSetClipbrdData failed, lasterr=%#lx\n", WinGetLastError(g_habWorker));
                        DosFreeMem(pv);
                    }
                }
                else
                    VGSvcError("vgsvcClipboardOs2Poll: DosAllocSharedMem(,,1,) -> %ld\n", rc);
            }
            else if (!g_fEmptyClipboard)
            {
                g_fEmptyClipboard = true;
                VGSvcVerbose(3, "Reporting empty clipboard\n");
                VbglR3ClipboardReportFormats(g_u32ClientId, 0);
            }
        }
        WinCloseClipbrd(g_habWorker);
    }
    else
        VGSvcError("vgsvcClipboardOs2Poll: WinOpenClipbrd failed, lasterr=%lx\n", WinGetLastError(g_habWorker));
}


/**
 * The clipboard we owned was destroyed by someone else.
 */
static void vgsvcClipboardOs2Destroyed(void)
{
    /* make sure we're no longer the owner. */
    if (WinQueryClipbrdOwner(g_habWorker) == g_hwndWorker)
        WinSetClipbrdOwner(g_habWorker, NULLHANDLE);

    /* switch to polling state and notify the host. */
    g_enmState = kClipboardState_Polling;
    g_fEmptyClipboard = true;
    VGSvcVerbose(3, "Reporting empty clipboard\n");
    VbglR3ClipboardReportFormats(g_u32ClientId, 0);

    vgsvcClipboardOs2PollViewer();
}


/**
 * The window procedure for the object window.
 *
 * @returns Message result.
 *
 * @param   hwnd    The window handle.
 * @param   msg     The message.
 * @param   mp1     Message parameter 1.
 * @param   mp2     Message parameter 2.
 */
static MRESULT EXPENTRY vgsvcClipboardOs2WinProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    if (msg != WM_TIMER)
        VGSvcVerbose(6, "vgsvcClipboardOs2WinProc: hwnd=%#lx msg=%#lx mp1=%#lx mp2=%#lx\n", hwnd, msg, mp1, mp2);

    switch (msg)
    {
        /*
         * Handle the two system defined messages for object windows.
         *
         * We'll just use the CREATE/DESTROY message to create that timer we're
         * using for the viewer checks and polling fallback.
         */
        case WM_CREATE:
            g_idWorkerTimer = WinStartTimer(g_habWorker, hwnd, 1 /* id */, 1000 /* 1 second */);
            g_fEmptyClipboard = true;
            g_enmState = kClipboardState_Polling;
            return NULL; /* FALSE(/NULL) == Continue*/

        case WM_DESTROY:
            WinStopTimer(g_habWorker, hwnd, g_idWorkerTimer);
            g_idWorkerTimer = ~0UL;
            g_hwndWorker = NULLHANDLE;
            break;

        /*
         * Clipboard viewer message - the content has been changed.
         * This is sent *after* releasing the clipboard sem
         * and during the WinSetClipbrdViewer call.
         */
        case WM_DRAWCLIPBOARD:
            if (g_enmState == kClipboardState_SettingViewer)
                break;
            AssertMsgBreak(g_enmState == kClipboardState_Viewer, ("g_enmState=%d\n", g_enmState));
            vgsvcClipboardOs2Poll();
            break;

        /*
         * Clipboard owner message - the content was replaced.
         * This is sent by someone with an open clipboard, so don't try open it now.
         */
        case WM_DESTROYCLIPBOARD:
            if (g_enmState == kClipboardState_Destroying)
                break; /* it's us doing the replacing, ignore. */
            AssertMsgBreak(g_enmState == kClipboardState_Owner, ("g_enmState=%d\n", g_enmState));
            vgsvcClipboardOs2Destroyed();
            break;

        /*
         * Clipboard owner message - somebody is requesting us to render a format.
         * This is called by someone which owns the clipboard, but that's fine.
         */
        case WM_RENDERFMT:
            AssertMsgBreak(g_enmState == kClipboardState_Owner, ("g_enmState=%d\n", g_enmState));
            vgsvcClipboardOs2RenderFormat(SHORT1FROMMP(mp1));
            break;

        /*
         * Clipboard owner message - we're about to quit and should render all formats.
         *
         * However, because we're lazy, we'll just ASSUME that since we're quitting
         * we're probably about to shutdown or something and there is no point in
         * doing anything here except for emptying the clipboard and removing
         * ourselves as owner. Any failures at this point are silently ignored.
         */
        case WM_RENDERALLFMTS:
            WinOpenClipbrd(g_habWorker);
            WinSetClipbrdOwner(g_habWorker, NULLHANDLE);
            g_enmState = kClipboardState_Destroying;
            WinEmptyClipbrd(g_habWorker);
            g_enmState = kClipboardState_Polling;
            g_fEmptyClipboard = true;
            WinCloseClipbrd(g_habWorker);
            break;

        /*
         * Listener message - the host has new formats to offer.
         */
        case WM_USER + VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
            vgsvcClipboardOs2AdvertiseHostFormats(LONGFROMMP(mp1));
            break;

        /*
         * Listener message - the host wish to read our clipboard data.
         */
        case WM_USER + VBOX_SHCL_HOST_MSG_READ_DATA:
            vgsvcClipboardOs2SendDataToHost(LONGFROMMP(mp1));
            break;

        /*
         * This is just a fallback polling strategy in case some other
         * app is trying to view the clipboard too. We also use this
         * to try recover from errors.
         *
         * Because the way the clipboard service works, we have to monitor
         * it all the time and cannot get away with simpler solutions like
         * synergy is employing (basically checking upon entering and leaving
         * a desktop).
         */
        case WM_TIMER:
            if (    g_enmState != kClipboardState_Viewer
                &&  g_enmState != kClipboardState_Polling)
                break;

            /* Lost the position as clipboard viewer?*/
            if (g_enmState == kClipboardState_Viewer)
            {
                if (WinQueryClipbrdViewer(g_habWorker) == hwnd)
                    break;
                g_enmState = kClipboardState_Polling;
            }

            /* poll for changes */
            vgsvcClipboardOs2Poll();
            vgsvcClipboardOs2PollViewer();
            break;


        /*
         * Clipboard owner messages dealing with owner drawn content.
         * We shouldn't be seeing any of these.
         */
        case WM_PAINTCLIPBOARD:
        case WM_SIZECLIPBOARD:
        case WM_HSCROLLCLIPBOARD:
        case WM_VSCROLLCLIPBOARD:
            AssertMsgFailed(("msg=%lx (%ld)\n", msg, msg));
            break;

        /*
         * We shouldn't be seeing any other messages according to the docs.
         * But for whatever reason, PM sends us a WM_ADJUSTWINDOWPOS message
         * during WinCreateWindow. So, ignore that and assert on anything else.
         */
        default:
            AssertMsgFailed(("msg=%lx (%ld)\n", msg, msg));
        case WM_ADJUSTWINDOWPOS:
            break;
    }
    return NULL;
}


/**
 * The listener thread.
 *
 * This thread is dedicated to listening for host messages and forwarding
 * these to the worker thread (using PM).
 *
 * The thread will set g_fListenerOkay and signal its user event when it has
 * completed initialization. In the case of init failure g_fListenerOkay will
 * not be set.
 *
 * @returns Init error code or VINF_SUCCESS.
 * @param   ThreadSelf  Our thread handle.
 * @param   pvUser      Pointer to the clipboard service shutdown indicator.
 */
static DECLCALLBACK(int) vgsvcClipboardOs2Listener(RTTHREAD ThreadSelf, void *pvUser)
{
    bool volatile *pfShutdown = (bool volatile *)pvUser;
    int rc = VERR_GENERAL_FAILURE;
    VGSvcVerbose(3, "vgsvcClipboardOs2Listener: ThreadSelf=%RTthrd\n", ThreadSelf);

    g_habListener = WinInitialize(0);
    if (g_habListener != NULLHANDLE)
    {
        g_hmqListener = WinCreateMsgQueue(g_habListener, 0);
        if (g_hmqListener != NULLHANDLE)
        {
            WinCancelShutdown(g_hmqListener, TRUE); /* We don't care about shutdown */

            /*
             * Tell the worker thread that we're good.
             */
            rc = VINF_SUCCESS;
            ASMAtomicXchgBool(&g_fListenerOkay, true);
            RTThreadUserSignal(ThreadSelf);
            VGSvcVerbose(3, "vgsvcClipboardOs2Listener: Started successfully\n");

            /*
             * Loop until termination is requested.
             */
            bool fQuit = false;
            while (!*pfShutdown && !fQuit)
            {
                uint32_t Msg;
                uint32_t fFormats;
                rc = VbglR3ClipboardGetHostMsgOld(g_u32ClientId, &Msg, &fFormats);
                if (RT_SUCCESS(rc))
                {
                    VGSvcVerbose(3, "vgsvcClipboardOs2Listener: Msg=%#x  fFormats=%#x\n", Msg, fFormats);
                    switch (Msg)
                    {
                        /*
                         * The host has announced available clipboard formats.
                         * Forward the information to the window, so it can later
                         * respond do WM_RENDERFORMAT message.
                         */
                        case VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
                            if (!WinPostMsg(g_hwndWorker, WM_USER + VBOX_SHCL_HOST_MSG_FORMATS_REPORT,
                                            MPFROMLONG(fFormats), 0))
                                VGSvcError("WinPostMsg(%lx, FORMATS,,) failed, lasterr=%#lx\n",
                                           g_hwndWorker, WinGetLastError(g_habListener));
                            break;

                        /*
                         * The host needs data in the specified format.
                         */
                        case VBOX_SHCL_HOST_MSG_READ_DATA:
                            if (!WinPostMsg(g_hwndWorker, WM_USER + VBOX_SHCL_HOST_MSG_READ_DATA,
                                            MPFROMLONG(fFormats), 0))
                                VGSvcError("WinPostMsg(%lx, READ_DATA,,) failed, lasterr=%#lx\n",
                                           g_hwndWorker, WinGetLastError(g_habListener));
                            break;

                        /*
                         * The host is terminating.
                         */
                        case VBOX_SHCL_HOST_MSG_QUIT:
                            fQuit = true;
                            break;

                        default:
                            VGSvcVerbose(1, "vgsvcClipboardOs2Listener: Unknown message %RU32\n", Msg);
                            break;
                    }
                }
                else
                {
                    if (*pfShutdown)
                        break;
                    VGSvcError("VbglR3ClipboardGetHostMsg failed, rc=%Rrc\n", rc);
                    RTThreadSleep(1000);
                }
            } /* the loop */

            WinDestroyMsgQueue(g_hmqListener);
        }
        WinTerminate(g_habListener);
        g_habListener = NULLHANDLE;
    }

    /* Signal our semaphore to make the worker catch on. */
    RTThreadUserSignal(ThreadSelf);
    VGSvcVerbose(3, "vgsvcClipboardOs2Listener: terminating, rc=%Rrc\n", rc);
    return rc;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vgsvcClipboardOs2Worker(bool volatile *pfShutdown)
{
    int rc = VERR_GENERAL_FAILURE;

    /*
     * Standard PM init.
     */
    g_habWorker = RTThreadSelf() != g_ThreadCtrl ? WinInitialize(0) : g_habCtrl;
    if (g_habWorker != NULLHANDLE)
    {
        g_hmqWorker = RTThreadSelf() != g_ThreadCtrl ? WinCreateMsgQueue(g_habWorker, 0) : g_hmqCtrl;
        if (g_hmqWorker != NULLHANDLE)
        {
            if (g_hmqWorker != g_hmqCtrl)
                WinCancelShutdown(g_hmqWorker, TRUE); /* We don't care about shutdown */

            /*
             * Create the object window.
             */
            if (WinRegisterClass(g_habWorker, (PCSZ)"VBoxServiceClipboardClass", vgsvcClipboardOs2WinProc, 0, 0))
            {
                g_hwndWorker = WinCreateWindow(HWND_OBJECT,                             /* hwndParent */
                                               (PCSZ)"VBoxServiceClipboardClass",       /* pszClass */
                                               (PCSZ)"VirtualBox Clipboard Service",    /* pszName */
                                               0,                                       /* flStyle */
                                               0, 0, 0, 0,                              /* x, y, cx, cy */
                                               NULLHANDLE,                              /* hwndOwner */
                                               HWND_BOTTOM,                             /* hwndInsertBehind */
                                               42,                                      /* id */
                                               NULL,                                    /* pCtlData */
                                               NULL);                                   /* pPresParams */
                if (g_hwndWorker != NULLHANDLE)
                {
                    VGSvcVerbose(3, "g_hwndWorker=%#lx g_habWorker=%#lx g_hmqWorker=%#lx\n", g_hwndWorker, g_habWorker, g_hmqWorker);

                    /*
                     * Create the listener thread.
                     */
                    g_fListenerOkay = false;
                    rc = RTThreadCreate(&g_ThreadListener, vgsvcClipboardOs2Listener, (void *)pfShutdown, 0,
                                        RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "CLIPLISTEN");
                    if (RT_SUCCESS(rc))
                    {
                        RTThreadUserWait(g_ThreadListener, 30*1000);
                        RTThreadUserReset(g_ThreadListener);
                        if (!g_fListenerOkay)
                            RTThreadWait(g_ThreadListener, 60*1000, NULL);
                        if (g_fListenerOkay)
                        {
                            /*
                             * Tell the control thread that it can continue
                             * spawning services.
                             */
                            RTThreadUserSignal(RTThreadSelf());

                            /*
                             * The PM event pump.
                             */
                            VGSvcVerbose(2, "clipboard: Entering PM message loop.\n");
                            rc = VINF_SUCCESS;
                            QMSG qmsg;
                            while (WinGetMsg(g_habWorker, &qmsg, NULLHANDLE, NULLHANDLE, 0))
                            {
                                if (qmsg.msg != WM_TIMER)
                                    VGSvcVerbose(6, "WinGetMsg -> hwnd=%p msg=%#x mp1=%p mp2=%p time=%#x ptl=%d,%d rsrv=%#x\n",
                                                 qmsg.hwnd, qmsg.msg, qmsg.mp1, qmsg.mp2, qmsg.time, qmsg.ptl.x, qmsg.ptl.y, qmsg.reserved);
                                WinDispatchMsg(g_habWorker, &qmsg);
                            }
                            VGSvcVerbose(2, "clipboard: Exited PM message loop. *pfShutdown=%RTbool\n", *pfShutdown);

                            RTThreadWait(g_ThreadListener, 60*1000, NULL);
                        }
                        g_ThreadListener = NIL_RTTHREAD;
                    }

                    /*
                     * Got a WM_QUIT, clean up.
                     */
                    if (g_hwndWorker != NULLHANDLE)
                    {
                        WinDestroyWindow(g_hwndWorker);
                        g_hwndWorker = NULLHANDLE;
                    }
                }
                else
                    VGSvcError("WinCreateWindow() failed, lasterr=%lx\n", WinGetLastError(g_habWorker));
                /* no class deregistration in PM.  */
            }
            else
                VGSvcError("WinRegisterClass() failed, lasterr=%lx\n", WinGetLastError(g_habWorker));

            if (g_hmqCtrl != g_hmqWorker)
                WinDestroyMsgQueue(g_hmqWorker);
            g_hmqWorker = NULLHANDLE;
        }
        else
            VGSvcError("WinCreateMsgQueue(,0) failed, lasterr=%lx\n", WinGetLastError(g_habWorker));

        if (g_habCtrl != g_habWorker)
            WinTerminate(g_habWorker);
        g_habWorker = NULLHANDLE;
    }
    else
        VGSvcError("WinInitialize(0) failed, lasterr=%lx\n", WinGetLastError(NULLHANDLE));

    return rc;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vgsvcClipboardOs2Stop(void)
{
    if (    g_hmqWorker != NULLHANDLE
        &&  !WinPostQueueMsg(g_hmqWorker, WM_QUIT, NULL, NULL))
        VGSvcError("WinPostQueueMsg(g_hmqWorker, WM_QUIT, 0,0) failed, lasterr=%lx\n", WinGetLastError(g_habCtrl));

    /* Must disconnect the clipboard here otherwise the listner won't quit and
       the service shutdown will not stop. */
    if (g_u32ClientId != 0)
    {
        if (g_hmqWorker != NULLHANDLE)
            RTThreadSleep(32);      /* fudge */

        VGSvcVerbose(4, "clipboard: disconnecting %#x\n", g_u32ClientId);
        int rc = VbglR3ClipboardDisconnect(g_u32ClientId);
        if (RT_SUCCESS(rc))
            g_u32ClientId = 0;
        else
            VGSvcError("clipboard: VbglR3ClipboardDisconnect(%#x) -> %Rrc\n", g_u32ClientId, rc);
    }
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnTerm}
 */
static DECLCALLBACK(void) vgsvcClipboardOs2Term(void)
{
    if (g_u32ClientId != 0)
    {
        VGSvcVerbose(4, "clipboard: disconnecting %#x\n", g_u32ClientId);
        int rc = VbglR3ClipboardDisconnect(g_u32ClientId);
        if (RT_SUCCESS(rc))
            g_u32ClientId = 0;
        else
            VGSvcError("clipboard: VbglR3ClipboardDisconnect(%#x) -> %Rrc\n", g_u32ClientId, rc);
    }
    WinDestroyMsgQueue(g_hmqCtrl);
    g_hmqCtrl = NULLHANDLE;
    WinTerminate(g_habCtrl);
    g_habCtrl = NULLHANDLE;
}


/**
 * The OS/2 'clipboard' service description.
 */
VBOXSERVICE g_Clipboard =
{
    /* pszName. */
    "clipboard",
    /* pszDescription. */
    "Shared Clipboard",
    /* pszUsage. */
    ""
    ,
    /* pszOptions. */
    ""
    ,
    /* methods */
    vgsvcClipboardOs2PreInit,
    vgsvcClipboardOs2Option,
    vgsvcClipboardOs2Init,
    vgsvcClipboardOs2Worker,
    vgsvcClipboardOs2Stop,
    vgsvcClipboardOs2Term
};

