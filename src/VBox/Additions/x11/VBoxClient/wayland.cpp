/* $Id: wayland.cpp 114743 2026-07-21 18:31:58Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Wayland Desktop Environment assistant.
 *
 * @note Obsolete. Compiled with 'kmk VBOX_WITH_WAYLAND_ADDITIONS_LEGACY=1'.
 */

/*
 * Copyright (C) 2017-2026 Oracle and/or its affiliates.
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

#include <iprt/asm.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>

#include <VBox/VBoxGuestLibGuestProp.h>
#include <VBox/HostServices/GuestPropertySvc.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/mime-type-converter.h>

#include "VBoxClient.h"
#include "clipboard.h"
#include "wayland-helper.h"

/** Polling interval for input focus monitoring task. */
#define VBCL_WAYLAND_WAIT_HOST_FOCUS_TIMEOUT_MS     (250)
/** Relax interval for input focus monitoring task. */
#define VBCL_WAYLAND_WAIT_HOST_FOCUS_RELAX_MS       (100)

/** Array of Wayland Desktop Environment helpers.
 * Sorted in order of preference. */
static const VBCLWAYLANDHELPER *g_apWaylandHelpers[] =
{
    &g_WaylandHelperEdcp,   /* Ext Data Control Protocol helper. */
    &g_WaylandHelperDcp,    /* Data Control Protocol helper. */
    &g_WaylandHelperGtk,    /* GTK helper. */
};

/** Selected helpers for Clipboard and Drag-and-Drop. */
static const VBCLWAYLANDHELPER *g_pWaylandHelperClipboard = NULL;
static const VBCLWAYLANDHELPER *g_pWaylandHelperDnd       = NULL;

/** @name Corresponding threads for host events handling.
 * @{  */
static RTTHREAD g_hClipboardThread      = NIL_RTTHREAD;
static RTTHREAD g_hDndThread            = NIL_RTTHREAD;
static RTTHREAD g_hHostInputFocusThread = NIL_RTTHREAD;
/** @} */


/**
 * Queries data from the host.
 *
 * @returns VBox status code.
 * @retval  VERR_NO_DATA if there is no corresponding VBox translation of the
 *          desired MIME type.
 * @param   pCtx                The VBoxClient shared clibpoard context.
 * @param   pszMimeType         The MIME type to query data for.
 * @param   ppvOutData          Where to return pointer to the data. Output of
 *                              VbghMimeConvFromVBox, free accordingly.
 * @param   pcbOutData          Where to return the data size.
 *
 * @thread  wl-xxxx
 */
int VBClWaylandClipboardQueryHostData(PSHCLCONTEXT pCtx, const char *pszMimeType, void **ppvOutData, size_t *pcbOutData)
{
    *ppvOutData = NULL;
    *pcbOutData = 0;

    /*
     * Determine what VBox data we require.
     */
    SHCLFORMAT const uVBoxFmt = VbghMimeConvGetVBoxFormatByMime(pszMimeType, NULL /*pfFlagsAndPriority*/,
                                                                NULL /*ppszPersistentMimeType*/);
    if (uVBoxFmt == VBOX_SHCL_FMT_NONE)
    {
        VBClLogVerbose(2, "VBClWaylandClipboardQueryHostData: No VBox format for MIME type '%s'\n", pszMimeType);
        return VERR_NO_DATA;
    }

    /*
     * Look for the remote data in the cache first.
     */
    RTCritSectEnter(&pCtx->Wl.CritSect);
    PSHCLCACHEENTRY pEntry = ShClCacheGet(&pCtx->Wl.OtherCache, uVBoxFmt);
    if (pEntry)
    {
        int rc = VbghMimeConvFromVBox(pszMimeType, pEntry->pvData, pEntry->cbData, ppvOutData, pcbOutData);
        RTCritSectLeave(&pCtx->Wl.CritSect);
        VBClLogVerbose(3, "VBClWaylandClipboardQueryHostData: Cache hit for '%s': %#x bytes, rc=%Rrc\n",
                       pszMimeType, *pcbOutData, rc);
        return rc;
    }

    /*
     * Is the data actually available?
     */
    uint64_t const    uRevision      = pCtx->Wl.uRevision;
    SHCLFORMATS const fOtherFormats  = pCtx->Wl.fOtherFormats;
    if (!(fOtherFormats & uVBoxFmt))
    {
        RTCritSectLeave(&pCtx->Wl.CritSect);
        VBClLogVerbose(2, "VBClWaylandClipboardQueryHostData: No host for MIME type '%s'\n", pszMimeType);
        return VERR_NO_DATA;
    }
    RTCritSectLeave(&pCtx->Wl.CritSect);

    /*
     * Query the data from the host.
     */
    void    *pvVBoxData = NULL;
    uint32_t cbVBoxData = 0;
    int rc = VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, uVBoxFmt, &pvVBoxData, &cbVBoxData);
    if (RT_FAILURE(rc))
    {
        VBClLogError("VBClWaylandClipboardQueryHostData: Failed to read %#x (for %s) from the host: %Rrc\n",
                     uVBoxFmt, pszMimeType, rc);
        return rc;
    }

    /*
     * Convert it and add it to the cache iff nothing changed.
     */
    rc = VbghMimeConvFromVBox(pszMimeType, pvVBoxData, cbVBoxData, ppvOutData, pcbOutData);

    RTCritSectEnter(&pCtx->Wl.CritSect);
    if (   pCtx->Wl.uRevision     == uRevision
        && pCtx->Wl.fOtherFormats == fOtherFormats
        && ShClCacheGet(&pCtx->Wl.OtherCache, uVBoxFmt) == NULL
        && cbVBoxData > 0)
    {
        /** @todo one copy too many here, but it has the benefit of heap api
         * consistency (e.g. drop-in electric heap fencing in a source file). */
        ShClCacheSet(&pCtx->Wl.OtherCache, uVBoxFmt, pvVBoxData, cbVBoxData);
    }
    RTCritSectLeave(&pCtx->Wl.CritSect);

    RTMemFree(pvVBoxData);

    VBClLogVerbose(3, "VBClWaylandClipboardQueryHostData: Cache miss for '%s': %#x bytes, rc=%Rrc%s\n",
                   pszMimeType, *pcbOutData, rc, pvVBoxData ? "" : " (added VBox data to cached)");
    return rc;
}

/**
 * Resets the state for our side before, entering the cache-filling stage.
 *
 * @returns New uRevision value.
 * @param   pCtx        The VBoxClient shared clibpoard context.
 * @param   pszCaller   Caller name for logging.
 * @param   pOfferSlot  Offer slot to inherit from. Optional.
 * @thread  wl-gtk, wl-dcp, wl-edcp
 */
uint64_t VBClWaylandClipboardResetOurState(PSHCLCONTEXT pCtx, const char *pszCaller, SHCLWLOFFERSLOT *pOfferSlot)
{
    RTCritSectEnter(&pCtx->Wl.CritSect);

    ShClCacheInvalidate(&pCtx->Wl.OurCache);

    int rc = RTSemEventMultiReset(pCtx->Wl.hOurCacheFilledEvent);
    AssertRCStmt(rc, VBClLogError("%s: RTSemEventMultiReset failed: rc=%Rrc", pszCaller, rc));

    for (unsigned i = 0; i < RT_ELEMENTS(pCtx->Wl.aOurMimeTypes); i++)
    {
        pCtx->Wl.aOurMimeTypes[i].fFlagsAndPriority = pOfferSlot ? pOfferSlot->aMimeTypes[i].fFlagsAndPriority : 0;
        pCtx->Wl.aOurMimeTypes[i].pszMimeType       = pOfferSlot ? pOfferSlot->aMimeTypes[i].pszMimeType       : NULL;
    }

    pCtx->Wl.fOurFormats     = pOfferSlot ? pOfferSlot->fFormats : VBOX_SHCL_FMT_NONE;
    uint64_t const uRevision = (pCtx->Wl.uRevision + 2) | 1;
    pCtx->Wl.uRevision       = uRevision;

    RTCritSectLeave(&pCtx->Wl.CritSect);
    return uRevision;
}

/**
 * @callback_method_impl{FNHOSTCLIPREPORTFMTS}
 */
static DECLCALLBACK(int) vbclWaylandClipboardHgReportCommon(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    /*
     * Perform common tasks before calling backend code.
     */
    RTCritSectEnter(&pCtx->Wl.CritSect);
    pCtx->Wl.fOtherFormats = fFormats;
    ShClCacheInvalidate(&pCtx->Wl.OtherCache);
    pCtx->Wl.uRevision = (pCtx->Wl.uRevision + 2) & ~(uint64_t)1; /* even numbers for host ownership */
    RTCritSectLeave(&pCtx->Wl.CritSect);
    /** @todo switch session mode as well here, as that's common stuff. */

    /*
     * Call backend.
     */
    return g_pWaylandHelperClipboard->clip.pfnHGClipReport(pCtx, fFormats);
}

/**
 * Helper for vbclWaylandClipboardGhRead.
 *
 * Must be called while inside the CritSect. Will have left it upon return.
 */
static int vbclWaylandClipboardGhReadCommonCacheHit(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, PSHCLCACHEENTRY pEntry)
{
    size_t const cbData = pEntry->cbData;
    VBClLogVerbose(4, "vbclWaylandClipboardGhReadCommon: Cache hit for %#x: %#x bytes, transferring to host...\n", uFmt, cbData);
    int rc = VbglR3ClipboardWriteDataEx(&pCtx->CmdCtx, uFmt, pEntry->pvData, cbData);
    RTCritSectLeave(&pCtx->Wl.CritSect);
    VBClLogVerbose(4, "vbclWaylandClipboardGhReadCommon: Cache hit for %#x: %#x bytes, rc=%Rrc\n", uFmt, cbData, rc);
    return rc;
}


/**
 * @callback_method_impl{FNHOSTCLIPREAD}
 */
static DECLCALLBACK(int) vbclWaylandClipboardGhReadCommon(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt)
{
    /*
     * Try serve it from the cache first.
     */
    RTCritSectEnter(&pCtx->Wl.CritSect);
    PSHCLCACHEENTRY pEntry = ShClCacheGet(&pCtx->Wl.OurCache, uFmt);
    if (pEntry)
        return vbclWaylandClipboardGhReadCommonCacheHit(pCtx, uFmt, pEntry);

    /*
     * Is the cache filling still ongoing?
     */
    int rc = RTSemEventMultiWait(pCtx->Wl.hOurCacheFilledEvent, 0);
    if (rc == VERR_TIMEOUT)
    {
        RTCritSectLeave(&pCtx->Wl.CritSect);
        VBClLogVerbose(4, "vbclWaylandClipboardGhReadCommon: Waiting for cache to be filled...\n");

        rc = RTSemEventMultiWait(pCtx->Wl.hOurCacheFilledEvent, RT_MS_30SEC);
        if (RT_SUCCESS(rc))
        {
            RTCritSectEnter(&pCtx->Wl.CritSect);
            pEntry = ShClCacheGet(&pCtx->Wl.OurCache, uFmt);
            if (pEntry)
                return vbclWaylandClipboardGhReadCommonCacheHit(pCtx, uFmt, pEntry);
        }
    }
    RTCritSectLeave(&pCtx->Wl.CritSect);

    /*
     * No matching data.
     */
    VBClLogVerbose(2, "vbclWaylandClipboardGhReadCommon: No data for %#x!\n", uFmt);
    return VbglR3ClipboardWriteDataEx(&pCtx->CmdCtx, uFmt, NULL, 0);
}

/**
 * @callback_method_impl{FNRTTHREAD,
 *      Worker for Shared Clipboard events from host.}
 */
static DECLCALLBACK(int) vbclWaylandClipboardWorker(RTTHREAD ThreadSelf, void *pvUser)
{
    bool volatile * const pfShutdown = (bool volatile *)pvUser;

    /*
     * Initialize the clipboard context structure (defined in clipboard.cpp, shared with X11).
     */
    PSHCLCONTEXT const pCtx = &g_Ctx;
    RT_ZERO(*pCtx);
    int rc = VbghWaylandClipboardPartialInit(&pCtx->Wl);
    if (RT_SUCCESS(rc))
    {
        /*
         * Connect to the host service.
         */
        rc = VbglR3ClipboardConnectEx(&pCtx->CmdCtx, VBOX_SHCL_GF_0_CONTEXT_ID);
        if (RT_SUCCESS(rc))
        {
            /* Provide helper with host clipboard service connection handle. */
            g_pWaylandHelperClipboard->clip.pfnSetClipboardCtx(pCtx);

            /* Notify parent thread that we're up running. */
            RTThreadUserSignal(ThreadSelf);


            /*
             * The main event loop processing host events.
             */
            while (!ASMAtomicReadBool(pfShutdown))
            {
                rc = VBClClipboardReadHostEvent(pCtx, vbclWaylandClipboardHgReportCommon, vbclWaylandClipboardGhReadCommon);
                if (RT_FAILURE(rc) && rc != VERR_INTERRUPTED)
                {
                    VBClLogInfo("cannot process host clipboard event: %Rrc\n", rc);
                    if (!ASMAtomicReadBool(pfShutdown))
                        RTThreadSleep(RT_MS_1SEC / 2);
                }
            }


            /*
             * Done. Tear down the connection and context.
             */
            VbglR3ClipboardDisconnectEx(&pCtx->CmdCtx);
        }
        else
            VBClLogError("VbglR3ClipboardConnectEx failed: %Rrc\n", rc);

        VbghWaylandClipboardPartialTerm(&pCtx->Wl);
    }
    else
        VBClLogError("RTCritSectInit failed: %Rrc\n", rc);
    VBClLogVerbose(2, "clipboard thread exitting: %Rrc\n", rc);
    return rc;
}

#if 0 /** @todo implement DnD */
/**
 * @callback_method_impl{FNRTTHREAD,
 *      Worker for Drag-and-Drop events from host. }
 */
static DECLCALLBACK(int) vbclWaylandDndWorker(RTTHREAD ThreadSelf, void *pvUser)
{
    bool volatile * const pfShutdown = (bool volatile *)pvUser;
    RTThreadUserSignal(ThreadSelf);
    return VINF_SUCCESS;
}
#endif

/**
 * @callback_method_impl{FNRTTHREAD,
 *      Worker for VM window focus change polling thread.}
 *
 * Some Wayland helpers need to be notified about VM
 * window focus change events. This is needed in order to
 * ask about, for example, if guest clipboard content was
 * changed since last user interaction. Such guest are not
 * able to notify host about clipboard content change and
 * needed to be asked implicitly.
 */
static DECLCALLBACK(int) vbclWaylandHostInputFocusWorker(RTTHREAD ThreadSelf, void *pvUser)
{
    bool volatile * const pfShutdown = (bool volatile *)pvUser;

    VBGLGSTPROPCLIENT GuestPropClient;
    int rc = VbglGuestPropConnect(&GuestPropClient);
    if (RT_SUCCESS(rc))
    {
        RTThreadUserSignal(ThreadSelf);

        while (!ASMAtomicReadBool(pfShutdown))
        {
            char achBuf[GUEST_PROP_MAX_NAME_LEN + GUEST_PROP_MAX_VALUE_LEN + GUEST_PROP_MAX_FLAGS_LEN];
            char *pszName = NULL;
            char *pszValue = NULL;
            char *pszFlags = NULL;
            bool fWasDeleted = false;
            uint64_t u64Timestamp = 0;
            /** @todo r=bird: Please comment on u64Timestamp=0 instead of reusing the
             *        previous timestamp...  */

            rc = VbglGuestPropWait(&GuestPropClient, VBOX_GUI_FOCUS_CHANGE_GUEST_PROP_NAME, achBuf, sizeof(achBuf), u64Timestamp,
                                   VBCL_WAYLAND_WAIT_HOST_FOCUS_TIMEOUT_MS, &pszName, &pszValue, &u64Timestamp,
                                   &pszFlags, NULL, &fWasDeleted);
            if (RT_SUCCESS(rc))
            {
                VBClLogVerbose(1, "guest property change: name: %s, val: %s, flags: %s, fWasDeleted: %RTbool\n",
                               pszName, pszValue, pszFlags, fWasDeleted);

                uint32_t fFlags = 0;
                if (RT_SUCCESS(GuestPropValidateFlags(pszFlags, &fFlags)))
                {
                    if (RTStrCmp(pszName, VBOX_GUI_FOCUS_CHANGE_GUEST_PROP_NAME) == 0)
                    {
                        if (fFlags & GUEST_PROP_F_RDONLYGUEST)
                        {
                            if (RT_VALID_PTR(g_pWaylandHelperClipboard))
                            {
                                if (RTStrCmp(pszValue, "0") == 0)
                                {
                                    rc = g_pWaylandHelperClipboard->clip.pfnPopup();
                                    VBClLogVerbose(1, "trigger popup, rc=%Rrc\n", rc);
                                }
                            }
                            else
                                VBClLogVerbose(1, "will not trigger popup\n");
                        }
                        else
                            VBClLogError("property has invalid attributes\n");
                    }
                    else
                        VBClLogVerbose(1, "unknown property name '%s'\n", pszName);

                }
                else
                    VBClLogError("guest property change: name: %s, val: %s, flags: %s, fWasDeleted: %RTbool: bad flags\n",
                                 pszName, pszValue, pszFlags, fWasDeleted);

            }
            else if (   rc != VERR_TIMEOUT
                     && rc != VERR_INTERRUPTED)
            {
                VBClLogError("error on waiting guest property notification, rc=%Rrc\n", rc);
                RTThreadSleep(VBCL_WAYLAND_WAIT_HOST_FOCUS_RELAX_MS);
            }
        }

        VbglGuestPropDisconnect(&GuestPropClient);
    }

    return rc;
}


/**
 * @interface_method_impl{VBCLSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vbclWaylandInit(void)
{
    int rc = VERR_NOT_SUPPORTED;

    /* Custom log prefix to be used for logger instance of this process. */
    static const char *pszLogPrefix = "VBoxClient Wayland:";
    VBClLogSetLogPrefix(pszLogPrefix);

    /* Go through the array of available helpers and try to pick up one. */
    for (uint32_t idxHelper = 0; idxHelper < RT_ELEMENTS(g_apWaylandHelpers); idxHelper++)
    {
        if (RT_VALID_PTR(g_apWaylandHelpers[idxHelper]->pfnProbe))
        {
            VBClLogInfo("Probing Wayland helper '%s' ...\n", g_apWaylandHelpers[idxHelper]->pszName);

            int fCaps = g_apWaylandHelpers[idxHelper]->pfnProbe();

            /* Try Clipboard helper. */
            if (   fCaps & VBOX_WAYLAND_HELPER_CAP_CLIPBOARD
                && !RT_VALID_PTR(g_pWaylandHelperClipboard))
            {
                if (RT_VALID_PTR(g_apWaylandHelpers[idxHelper]->clip.pfnInit))
                {
/** @todo r=bird: For the EDCP and DPC backends, this will start the worker
 *        thread and whatnot. Iff there is something in the guest clipboard,
 *        it prematurely try to report this to the host before there is a
 *        connection to it.  This will lead to confusing VERR_TRY_AGAIN in
 *        a verbose log.  In the bi-directional and host-to-guest clipboard
 *        modes, this isn't much of a problem, as it just means the host content
 *        takes precedence.  However, in the guest-to-host mode, this probably
 *        means we won't get anything on the host clipboard till something new
 *        is copied inside the guest.
 *
 *        Actually, at this point, we don't even know if if there is a shared
 *        service on the host. :-) */
                    rc = g_apWaylandHelpers[idxHelper]->clip.pfnInit();
                    if (RT_SUCCESS(rc))
                        g_pWaylandHelperClipboard = g_apWaylandHelpers[idxHelper];
                    else
                        VBClLogError("Wayland helper '%s' cannot be initialized, skipping\n",
                                     g_apWaylandHelpers[idxHelper]->pszName);
                }
                else
                    VBClLogVerbose(1, "Wayland helper '%s' has no initializer, skipping\n",
                                   g_apWaylandHelpers[idxHelper]->pszName);
            }

            /* Try DnD helper. */
            if (   fCaps & VBOX_WAYLAND_HELPER_CAP_DND
                && !RT_VALID_PTR(g_pWaylandHelperDnd))
            {
                if (RT_VALID_PTR(g_apWaylandHelpers[idxHelper]->dnd.pfnInit))
                {
                    rc = g_apWaylandHelpers[idxHelper]->dnd.pfnInit();
                    if (RT_SUCCESS(rc))
                        g_pWaylandHelperDnd = g_apWaylandHelpers[idxHelper];
                    else
                        VBClLogError("Wayland helper '%s' cannot be initialized, skipping\n",
                                     g_apWaylandHelpers[idxHelper]->pszName);
                }
                else
                    VBClLogVerbose(1, "Wayland helper '%s' has no initializer, skipping\n",
                                   g_apWaylandHelpers[idxHelper]->pszName);
            }
        }

        /* See if we found all the needed helpers. */
        if (   RT_VALID_PTR(g_pWaylandHelperClipboard)
            && RT_VALID_PTR(g_pWaylandHelperDnd))
            break;
    }

    /* Check result. */
    if (RT_VALID_PTR(g_pWaylandHelperClipboard))
        VBClLogInfo("found Wayland Shared Clipboard helper '%s'\n", g_pWaylandHelperClipboard->pszName);
    else
        VBClLogError("Wayland Shared Clipboard helper not found, clipboard sharing not possible\n");

    /* Check result. */
    if (RT_VALID_PTR(g_pWaylandHelperDnd))
        VBClLogInfo("found Wayland Drag-and-Drop helper '%s'\n", g_pWaylandHelperDnd->pszName);
    else
        VBClLogError("Wayland Drag-and-Drop helper not found, drag-and-drop not possible\n");

    return rc;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vbclWaylandWorker(bool volatile *pfShutdown)
{
    RT_NOREF(pfShutdown);

    VBClLogVerbose(1, "starting wayland worker thread\n");

    /*
     * Start the worker threads.
     */
    /* Start event loop for clipboard events processing from host. */
    int rc = VINF_SUCCESS;
    if (RT_VALID_PTR(g_pWaylandHelperClipboard))
    {
        rc = VBClStartThread(&g_hClipboardThread, vbclWaylandClipboardWorker, "wl-clip", (void *)pfShutdown);
        VBClLogVerbose(1, "clipboard thread started, rc=%Rrc\n", rc);
    }

#if 0 /** @todo implement DnD */
    /* Start event loop for DnD events processing from host. */
    if (   RT_SUCCESS(rc)
        && RT_VALID_PTR(g_pWaylandHelperDnd))
    {
        rc = vbcl_wayland_thread_start(&g_hDndThread, vbclWaylandDndWorker, "wl-dnd", (void *)pfShutdown);
        VBClLogVerbose(1, "DnD thread started, rc=%Rrc\n", rc);
    }
#endif

    /* Start waiting host input focus events (only basic Wayland clipboard protocol
       requires this, thus the pfnPopup check). */
    if (   RT_SUCCESS(rc)
        && g_pWaylandHelperClipboard
        && g_pWaylandHelperClipboard->clip.pfnPopup != NULL)
    {
        rc = VBClStartThread(&g_hHostInputFocusThread, vbclWaylandHostInputFocusWorker, "wl-focus", (void *)pfShutdown);
        VBClLogVerbose(1, "host input focus event thread started, rc=%Rrc\n", rc);
    }

    /* Notify parent thread that we are successfully started. */
    RTThreadUserSignal(RTThreadSelf());

    /*
     * Wait for the worker threads to complete.
     * Note! The handling here in an error situation, might not be entirely optimal yet...
     */
    if (g_hClipboardThread != NIL_RTTHREAD)
    {
        int rcThread = VINF_SUCCESS;
        int rc2 = RTThreadWait(g_hClipboardThread, RT_INDEFINITE_WAIT, &rcThread);
        g_hClipboardThread = NIL_RTTHREAD;
        VBClLogVerbose(1, "clipboard thread finished, rc2=%Rrc, rcThread=%Rrc\n", rc2, rcThread);
    }

    if (g_hDndThread != NIL_RTTHREAD)
    {
        int rcThread = VINF_SUCCESS;
        int rc2 = RTThreadWait(g_hDndThread, RT_INDEFINITE_WAIT, &rcThread);
        g_hDndThread = NIL_RTTHREAD;
        VBClLogVerbose(1, "DnD thread finished, rc2=%Rrc, rcThread=%Rrc\n", rc2, rcThread);
    }

    if (g_hHostInputFocusThread != NIL_RTTHREAD)
    {
        int rcThread = VINF_SUCCESS;
        int rc2 = RTThreadWait(g_hHostInputFocusThread, RT_INDEFINITE_WAIT, &rcThread);
        g_hHostInputFocusThread = NIL_RTTHREAD;
        VBClLogVerbose(1, "host input focus polling thread finished, rc2=%Rrc, rcThread=%Rrc\n", rc2, rcThread);
    }

    VBClLogVerbose(1, "wayland worker thread finished, rc=%Rrc\n", rc);
    return rc;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vbclWaylandStop(void)
{
    VBClLogVerbose(1, "terminating wayland service: clipboard & DnD host event loops\n");

    if (g_hClipboardThread != NIL_RTTHREAD)
        RTThreadPoke(g_hClipboardThread);

    if (g_hDndThread != NIL_RTTHREAD)
        RTThreadPoke(g_hDndThread);

    if (g_hHostInputFocusThread != NIL_RTTHREAD)
        RTThreadPoke(g_hHostInputFocusThread);
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnTerm}
 */
static DECLCALLBACK(int) vbclWaylandTerm(void)
{
    int rc = VINF_SUCCESS;

    VBClLogVerbose(1, "shutting down wayland service: clipboard & DnD helpers\n");

    if (   RT_VALID_PTR(g_pWaylandHelperClipboard)
        && RT_VALID_PTR(g_pWaylandHelperClipboard->clip.pfnTerm))
    {
        rc = g_pWaylandHelperClipboard->clip.pfnTerm();
        AssertRC(rc);
    }

    if (   RT_VALID_PTR(g_pWaylandHelperDnd)
        && RT_VALID_PTR(g_pWaylandHelperDnd->dnd.pfnTerm))
    {
        int rc2 = g_pWaylandHelperDnd->dnd.pfnTerm();
        AssertRCStmt(rc2, rc = rc2);
    }

    return rc;
}

VBCLSERVICE const g_SvcWayland =
{
    "wayland",              /* szName */
    "Wayland assistant",    /* pszDescription */
    ".vboxclient-wayland",  /* pszPidFilePathTemplate */
    NULL,                   /* pszUsage */
    NULL,                   /* pszOptions */
    NULL,                   /* pfnOption */
    vbclWaylandInit,        /* pfnInit */
    vbclWaylandWorker,      /* pfnWorker */
    vbclWaylandStop,        /* pfnStop */
    vbclWaylandTerm,        /* pfnTerm */
};
