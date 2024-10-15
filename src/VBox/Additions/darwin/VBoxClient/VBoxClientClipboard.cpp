/** $Id: VBoxClientClipboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxClient - Shared Slipboard Dispatcher, Darwin.
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

#include <iprt/asm.h>
#include <iprt/stream.h>
#include <iprt/thread.h>
#include <iprt/critsect.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include "VBoxClientInternal.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** Host clipboard connection client ID */
static uint32_t         g_u32ClientId;
/* Guest clipboard reference */
static PasteboardRef    g_PasteboardRef;
/* Dispatcher tharead handle */
RTTHREAD                g_DispatcherThread;
/* Pasteboard polling tharead handle */
RTTHREAD                g_GuestPasteboardThread;
/* Flag that indicates whether or not dispatcher and Pasteboard polling threada should stop */
static bool volatile    g_fShouldStop;
/* Barrier for Pasteboard */
static RTCRITSECT       g_critsect;


/*********************************************************************************************************************************
*   Local Macros                                                                                                                 *
*********************************************************************************************************************************/

#define VBOXCLIENT_SERVICE_NAME     "clipboard"


/*********************************************************************************************************************************
*   Local Function Prototypes                                                                                                    *
*********************************************************************************************************************************/
static DECLCALLBACK(int) vbclClipboardStop(void);


/**
 * Clipboard dispatcher function.
 *
 * Forwards cliproard content between host and guest.
 *
 * @param   ThreadSelf  Unused parameter.
 * @param   pvUser      Unused parameter.
 *
 * @return  IPRT status code.
 */
static DECLCALLBACK(int) vbclClipboardDispatcher(RTTHREAD ThreadSelf, void *pvUser)
{
    bool fQuit = false;
    NOREF(ThreadSelf);
    NOREF(pvUser);

    VBoxClientVerbose(2, "starting host clipboard polling thread\n");

    /*
     * Block all signals for this thread. Only the main thread will handle signals.
     */
    sigset_t signalMask;
    sigfillset(&signalMask);
    pthread_sigmask(SIG_BLOCK, &signalMask, NULL);

    while (!fQuit && !ASMAtomicReadBool(&g_fShouldStop))
    {
        int rc;
        uint32_t Msg;
        uint32_t fFormats;

        VBoxClientVerbose(2, "waiting for new host request\n");

        rc = VbglR3ClipboardGetHostMsgOld(g_u32ClientId, &Msg, &fFormats);
        if (RT_SUCCESS(rc))
        {
            RTCritSectEnter(&g_critsect);
            switch (Msg)
            {
                /* The host is terminating */
                case VBOX_SHCL_HOST_MSG_QUIT:
                    VBoxClientVerbose(2, "host requested quit\n");
                    fQuit = true;
                    break;

                /* The host needs data in the specified format */
                case VBOX_SHCL_HOST_MSG_READ_DATA:
                    VBoxClientVerbose(2, "host requested guest's clipboard read\n");
                    rc = vbclClipboardForwardToHost(g_u32ClientId, g_PasteboardRef, fFormats);
                    AssertMsg(RT_SUCCESS(rc), ("paste to host failed\n"));
                    break;

                /* The host has announced available clipboard formats */
                case VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
                    VBoxClientVerbose(2, "host requested guest's clipboard write\n");
                    rc = vbclClipboardForwardToGuest(g_u32ClientId, g_PasteboardRef, fFormats);
                    AssertMsg(RT_SUCCESS(rc), ("paste to guest failed\n"));
                    break;

                default:
                    VBoxClientVerbose(2, "received unknow command from host service\n");
                    RTThreadSleep(1000);
            }

            RTCritSectLeave(&g_critsect);
        }
        else
        {
            RTThreadSleep(1000);
        }
    }

    VBoxClientVerbose(2, "host clipboard polling thread stopped\n");

    return VINF_SUCCESS;
}


/**
 * Clipboard dispatcher function.
 *
 * Forwards cliproard content between host and guest.
 *
 * @param   hThreadSelf  Unused parameter.
 * @param   pvUser      Unused parameter.
 *
 * @return  IPRT status code.
 */
static DECLCALLBACK(int) vbclGuestPasteboardPoll(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf, pvUser);

    /*
     * Block all signals for this thread. Only the main thread will handle signals.
     */
    sigset_t signalMask;
    sigfillset(&signalMask);
    pthread_sigmask(SIG_BLOCK, &signalMask, NULL);

    VBoxClientVerbose(2, "starting guest clipboard polling thread\n");

    while (!ASMAtomicReadBool(&g_fShouldStop))
    {
        PasteboardSyncFlags fSyncFlags;
        uint32_t            fFormats;
        int                 rc;

        RTCritSectEnter(&g_critsect);

        fSyncFlags = PasteboardSynchronize(g_PasteboardRef);
        if (fSyncFlags & kPasteboardModified)
        {
            fFormats = vbclClipboardGetAvailableFormats(g_PasteboardRef);
            rc = VbglR3ClipboardReportFormats(g_u32ClientId, fFormats);
            if (RT_FAILURE(rc))
            {
                VBoxClientVerbose(2, "failed to report pasteboard update (%Rrc)\n", rc);
            }
            else
            {
                VBoxClientVerbose(2, "guest clipboard update reported: %d\n", (int)fFormats);
            }
        }

        RTCritSectLeave(&g_critsect);

        /* Check pasteboard every 200 ms */
        RTThreadSleep(200);
    }

    VBoxClientVerbose(2, "guest clipboard polling thread stopped\n");

    return VINF_SUCCESS;
}


/**
 * Initialize host and guest clipboards, start clipboard dispatcher loop.
 *
 * @return  IPRT status code.
 */
static DECLCALLBACK(int) vbclClipboardStart(void)
{
    int rc;

    VBoxClientVerbose(2, "starting clipboard\n");

    rc = RTCritSectInit(&g_critsect);
    if (RT_FAILURE(rc))
        return VERR_GENERAL_FAILURE;

    rc = VbglR3ClipboardConnect(&g_u32ClientId);
    if (RT_SUCCESS(rc))
    {
        rc = PasteboardCreate(kPasteboardClipboard, &g_PasteboardRef);
        if (rc == noErr)
        {
            /* Start dispatcher loop */
            ASMAtomicWriteBool(&g_fShouldStop, false);
            rc = RTThreadCreate(&g_DispatcherThread,
                                vbclClipboardDispatcher,
                                (void *)NULL,
                                0,
                                RTTHREADTYPE_DEFAULT,
                                RTTHREADFLAGS_WAITABLE,
                                VBOXCLIENT_SERVICE_NAME);
            if (RT_SUCCESS(rc))
            {
                /* Start dispatcher loop */
                ASMAtomicWriteBool(&g_fShouldStop, false);
                rc = RTThreadCreate(&g_GuestPasteboardThread,
                                    vbclGuestPasteboardPoll,
                                    (void *)NULL,
                                    0,
                                    RTTHREADTYPE_DEFAULT,
                                    RTTHREADFLAGS_WAITABLE,
                                    VBOXCLIENT_SERVICE_NAME);
                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;

                /* Stop dispatcher thread */
                ASMAtomicWriteBool(&g_fShouldStop, true);
                RTThreadWait(g_DispatcherThread, 10 * 1000 /* Wait 10 seconds */, NULL);

            }
            VBoxClientVerbose(2, "unable create dispatcher thread\n");
            CFRelease(g_PasteboardRef);
            g_PasteboardRef = NULL;

        }
        else
        {
            rc = VERR_GENERAL_FAILURE;
            VBoxClientVerbose(2, "unable access guest clipboard\n");
        }

        vbclClipboardStop();

    }
    else
    {
        VBoxClientVerbose(2, "unable to establish connection to clipboard service: %Rrc\n", rc);
    }

    RTCritSectDelete(&g_critsect);

    return rc;
}


/**
 * Release host and guest clipboards, stop clipboard dispatcher loop.
 *
 * @return  IPRT status code.
 */
static DECLCALLBACK(int) vbclClipboardStop(void)
{
    int rc;

    VBoxClientVerbose(2, "stopping clipboard\n");

    AssertReturn(g_u32ClientId != 0, VERR_GENERAL_FAILURE);

    VbglR3ClipboardReportFormats(g_u32ClientId, 0);

    rc = VbglR3ClipboardDisconnect(g_u32ClientId);
    if (RT_SUCCESS(rc))
        g_u32ClientId = 0;
    else
        VBoxClientVerbose(2, "unable to close clipboard service connection: %Rrc\n", rc);

    if (g_PasteboardRef)
    {
        CFRelease(g_PasteboardRef);
        g_PasteboardRef = NULL;
    }

    /* Stop dispatcher thread */
    ASMAtomicWriteBool(&g_fShouldStop, true);
    rc = RTThreadWait(g_DispatcherThread, 10 * 1000 /* Wait 10 seconds */, NULL);
    if (RT_FAILURE(rc))
        VBoxClientVerbose(2, "failed to stop dispatcher thread");

    /* Stop Pasteboard polling thread */
    rc = RTThreadWait(g_GuestPasteboardThread, 10 * 1000 /* Wait 10 seconds */, NULL);
    if (RT_FAILURE(rc))
        VBoxClientVerbose(2, "failed to stop pasteboard polling thread");

    RTCritSectDelete(&g_critsect);

    return rc;
}


/* Clipboard service struct */
VBOXCLIENTSERVICE g_ClipboardService =
{
    /* pszName */
    VBOXCLIENT_SERVICE_NAME,

    /* pfnStart */
    vbclClipboardStart,

    /* pfnStop */
    vbclClipboardStop,
};
