/* $Id: VBoxServiceControl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxServiceControl - Host-driven Guest Control.
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

/** @page pg_vgsvc_gstctrl VBoxService - Guest Control
 *
 * The Guest Control subservice helps implementing the IGuest APIs.
 *
 * The communication between this service (and its children) and IGuest goes
 * over the HGCM GuestControl service.
 *
 * The IGuest APIs provides means to manipulate (control) files, directories,
 * symbolic links and processes within the guest.  Most of these means requires
 * credentials of a guest OS user to operate, though some restricted ones
 * operates directly as the VBoxService user (root / system service account).
 *
 * The current design is that a subprocess is spawned for handling operations as
 * a given user.  This process is represented as IGuestSession in the API.  The
 * subprocess will be spawned as the given use, giving up the privileges the
 * parent subservice had.
 *
 * It will try handle as many of the operations directly from within the
 * subprocess, but for more complicated things (or things that haven't yet been
 * converted), it will spawn a helper process that does the actual work.
 *
 * These helpers are the typically modeled on similar unix core utilities, like
 * mkdir, rm, rmdir, cat and so on.  The helper tools can also be launched
 * directly from VBoxManage by the user by prepending the 'vbox_' prefix to the
 * unix command.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <VBox/err.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/HostServices/GuestControlSvc.h>
#include "VBoxServiceInternal.h"
#include "VBoxServiceControl.h"
#include "VBoxServiceUtils.h"

using namespace guestControl;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The control interval (milliseconds). */
static uint32_t             g_msControlInterval = 0;
/** The semaphore we're blocking our main control thread on. */
static RTSEMEVENTMULTI      g_hControlEvent = NIL_RTSEMEVENTMULTI;
/** The VM session ID. Changes whenever the VM is restored or reset. */
static uint64_t             g_idControlSession;
/** The guest control service client ID. */
uint32_t                    g_idControlSvcClient = 0;
/** VBOX_GUESTCTRL_HF_XXX */
uint64_t                    g_fControlHostFeatures0 = 0;
#if 0 /** @todo process limit */
/** How many started guest processes are kept into memory for supplying
 *  information to the host. Default is 256 processes. If 0 is specified,
 *  the maximum number of processes is unlimited. */
static uint32_t             g_uControlProcsMaxKept = 256;
#endif
/** List of guest control session threads (VBOXSERVICECTRLSESSIONTHREAD).
 *  A guest session thread represents a forked guest session process
 *  of VBoxService.  */
RTLISTANCHOR                g_lstControlSessionThreads;
/** The local session object used for handling all session-related stuff.
 *  When using the legacy guest control protocol (< 2), this session runs
 *  under behalf of the VBoxService main process. On newer protocol versions
 *  each session is a forked version of VBoxService using the appropriate
 *  user credentials for opening a guest session. These forked sessions then
 *  are kept in VBOXSERVICECTRLSESSIONTHREAD structures. */
VBOXSERVICECTRLSESSION      g_Session;
/** Copy of VbglR3GuestCtrlSupportsOptimizations().*/
bool                        g_fControlSupportsOptimizations = true;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  vgsvcGstCtrlHandleSessionOpen(PVBGLR3GUESTCTRLCMDCTX pHostCtx);
static int  vgsvcGstCtrlHandleSessionClose(PVBGLR3GUESTCTRLCMDCTX pHostCtx);
static int  vgsvcGstCtrlInvalidate(void);
static void vgsvcGstCtrlShutdown(void);


/**
 * @interface_method_impl{VBOXSERVICE,pfnPreInit}
 */
static DECLCALLBACK(int) vgsvcGstCtrlPreInit(void)
{
    int rc;
#ifdef VBOX_WITH_GUEST_PROPS
    /*
     * Read the service options from the VM's guest properties.
     * Note that these options can be overridden by the command line options later.
     */
    uint32_t uGuestPropSvcClientID;
    rc = VbglR3GuestPropConnect(&uGuestPropSvcClientID);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_HGCM_SERVICE_NOT_FOUND) /* Host service is not available. */
        {
            VGSvcVerbose(0, "Guest property service is not available, skipping\n");
            rc = VINF_SUCCESS;
        }
        else
            VGSvcError("Failed to connect to the guest property service, rc=%Rrc\n", rc);
    }
    else
        VbglR3GuestPropDisconnect(uGuestPropSvcClientID);

    if (rc == VERR_NOT_FOUND) /* If a value is not found, don't be sad! */
        rc = VINF_SUCCESS;
#else
    /* Nothing to do here yet. */
    rc = VINF_SUCCESS;
#endif

    if (RT_SUCCESS(rc))
    {
        /* Init session object. */
        rc = VGSvcGstCtrlSessionInit(&g_Session, 0 /* Flags */);
    }

    return rc;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnOption}
 */
static DECLCALLBACK(int) vgsvcGstCtrlOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    int rc = -1;
    if (ppszShort)
        /* no short options */;
    else if (!strcmp(argv[*pi], "--control-interval"))
        rc = VGSvcArgUInt32(argc, argv, "", pi,
                                  &g_msControlInterval, 1, UINT32_MAX - 1);
#ifdef DEBUG
    else if (!strcmp(argv[*pi], "--control-dump-stdout"))
    {
        g_Session.fFlags |= VBOXSERVICECTRLSESSION_FLAG_DUMPSTDOUT;
        rc = 0; /* Flag this command as parsed. */
    }
    else if (!strcmp(argv[*pi], "--control-dump-stderr"))
    {
        g_Session.fFlags |= VBOXSERVICECTRLSESSION_FLAG_DUMPSTDERR;
        rc = 0; /* Flag this command as parsed. */
    }
#endif
    return rc;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vgsvcGstCtrlInit(void)
{
    /*
     * If not specified, find the right interval default.
     * Then create the event sem to block on.
     */
    if (!g_msControlInterval)
        g_msControlInterval = 1000;

    int rc = RTSemEventMultiCreate(&g_hControlEvent);
    AssertRCReturn(rc, rc);

    VbglR3GetSessionId(&g_idControlSession); /* The status code is ignored as this information is not available with VBox < 3.2.10. */

    RTListInit(&g_lstControlSessionThreads);

    /*
     * Try connect to the host service and tell it we want to be master (if supported).
     */
    rc = VbglR3GuestCtrlConnect(&g_idControlSvcClient);
    if (RT_SUCCESS(rc))
    {
        rc = vgsvcGstCtrlInvalidate();
        if (RT_SUCCESS(rc))
            return rc;
    }
    else
    {
        /* If the service was not found, we disable this service without
           causing VBoxService to fail. */
        if (rc == VERR_HGCM_SERVICE_NOT_FOUND) /* Host service is not available. */
        {
            VGSvcVerbose(0, "Guest control service is not available\n");
            rc = VERR_SERVICE_DISABLED;
        }
        else
            VGSvcError("Failed to connect to the guest control service! Error: %Rrc\n", rc);
    }
    RTSemEventMultiDestroy(g_hControlEvent);
    g_hControlEvent      = NIL_RTSEMEVENTMULTI;
    g_idControlSvcClient = 0;
    return rc;
}

static int vgsvcGstCtrlInvalidate(void)
{
    VGSvcVerbose(1, "Invalidating configuration ...\n");

    int rc = VINF_SUCCESS;

    g_fControlSupportsOptimizations = VbglR3GuestCtrlSupportsOptimizations(g_idControlSvcClient);
    if (g_fControlSupportsOptimizations)
    {
        for (int i = 0; i < 3; i++)
        {
            rc = VbglR3GuestCtrlMakeMeMaster(g_idControlSvcClient);
            if (RT_SUCCESS(rc))
                break;
            RTThreadSleep(RTRandU32Ex(RT_MS_1SEC, RT_MS_5SEC));
            VGSvcError("Failed to become guest control master (#%d): %Rrc\n", i, rc);
        }
    }
    if (RT_SUCCESS(rc))
    {
        VGSvcVerbose(3, "Guest control service client ID=%RU32%s\n",
                     g_idControlSvcClient, g_fControlSupportsOptimizations ? " w/ optimizations" : "");

        /*
         * Report features to the host.
         */
        const uint64_t fGuestFeatures = VBOX_GUESTCTRL_GF_0_SET_SIZE
                                      | VBOX_GUESTCTRL_GF_0_PROCESS_ARGV0
                                      | VBOX_GUESTCTRL_GF_0_PROCESS_DYNAMIC_SIZES
                                      | VBOX_GUESTCTRL_GF_0_PROCESS_CWD
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
                                      | VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
                                      | VBOX_GUESTCTRL_GF_0_SHUTDOWN
                                      | VBOX_GUESTCTRL_GF_0_MOUNT_POINTS_ENUM;
        rc = VbglR3GuestCtrlReportFeatures(g_idControlSvcClient, fGuestFeatures, &g_fControlHostFeatures0);
        if (RT_SUCCESS(rc))
            VGSvcVerbose(3, "Host features: %#RX64\n", g_fControlHostFeatures0);
        else
            VGSvcVerbose(1, "Warning! Feature reporting failed: %Rrc\n", rc);

        return VINF_SUCCESS;
    }
    VGSvcError("Giving up to become guest control master, disconnecting\n");
    VbglR3GuestCtrlDisconnect(g_idControlSvcClient);

    return rc;
}

/**
 * @interface_method_impl{VBOXSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vgsvcGstCtrlWorker(bool volatile *pfShutdown)
{
    /*
     * Tell the control thread that it can continue spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());
    Assert(g_idControlSvcClient > 0);

    /* Allocate a scratch buffer for messages which also send
     * payload data with them. */
    uint32_t cbScratchBuf = _64K; /** @todo Make buffer size configurable via guest properties/argv! */
    AssertReturn(RT_IS_POWER_OF_TWO(cbScratchBuf), VERR_INVALID_PARAMETER);
    uint8_t *pvScratchBuf = (uint8_t*)RTMemAlloc(cbScratchBuf);
    AssertReturn(pvScratchBuf, VERR_NO_MEMORY);

    int rc = VINF_SUCCESS;      /* (shut up compiler warnings) */
    int cRetrievalFailed = 0;   /* Number of failed message retrievals in a row. */
    while (!*pfShutdown)
    {
        VGSvcVerbose(3, "GstCtrl: Waiting for host msg ...\n");
        VBGLR3GUESTCTRLCMDCTX ctxHost = { g_idControlSvcClient, 0 /*idContext*/, 2 /*uProtocol*/, 0  /*cParms*/ };
        uint32_t              idMsg   = 0;
        rc = VbglR3GuestCtrlMsgPeekWait(g_idControlSvcClient, &idMsg, &ctxHost.uNumParms, &g_idControlSession);
        if (RT_SUCCESS(rc))
        {
            cRetrievalFailed = 0; /* Reset failed retrieval count. */
            VGSvcVerbose(4, "idMsg=%RU32 (%s) (%RU32 parms) retrieved\n",
                         idMsg, GstCtrlHostMsgtoStr((eHostMsg)idMsg), ctxHost.uNumParms);

            /*
             * Handle the host message.
             */
            switch (idMsg)
            {
                case HOST_MSG_CANCEL_PENDING_WAITS:
                    VGSvcVerbose(1, "We were asked to quit ...\n");
                    break;

                case HOST_MSG_SESSION_CREATE:
                    rc = vgsvcGstCtrlHandleSessionOpen(&ctxHost);
                    break;

                /* This message is also sent to the child session process (by the host). */
                case HOST_MSG_SESSION_CLOSE:
                    rc = vgsvcGstCtrlHandleSessionClose(&ctxHost);
                    break;

                default:
                    if (VbglR3GuestCtrlSupportsOptimizations(g_idControlSvcClient))
                    {
                        rc = VbglR3GuestCtrlMsgSkip(g_idControlSvcClient, VERR_NOT_SUPPORTED, idMsg);
                        VGSvcVerbose(1, "Skipped unexpected message idMsg=%RU32 (%s), cParms=%RU32 (rc=%Rrc)\n",
                                     idMsg, GstCtrlHostMsgtoStr((eHostMsg)idMsg), ctxHost.uNumParms, rc);
                    }
                    else
                    {
                        rc = VbglR3GuestCtrlMsgSkipOld(g_idControlSvcClient);
                        VGSvcVerbose(3, "Skipped idMsg=%RU32, cParms=%RU32, rc=%Rrc\n", idMsg, ctxHost.uNumParms, rc);
                    }
                    break;
            }

            /* Do we need to shutdown? */
            if (idMsg == HOST_MSG_CANCEL_PENDING_WAITS)
                break;

            /* Let's sleep for a bit and let others run ... */
            RTThreadYield();
        }
        /*
         * Handle restore notification from host.  All the context IDs (sessions,
         * files, proceses, etc) are invalidated by a VM restore and must be closed.
         */
        else if (rc == VERR_VM_RESTORED)
        {
            VGSvcVerbose(1, "The VM session ID changed (i.e. restored), closing stale root session\n");

            /* Make sure that all other session threads are gone.
             * This is necessary, as the new VM session (NOT to be confused with guest session!) will re-use
             * the guest session IDs. */
            int rc2 = VGSvcGstCtrlSessionThreadDestroyAll(&g_lstControlSessionThreads, 0 /* Flags */);
            if (RT_FAILURE(rc2))
                VGSvcError("Closing session threads failed with rc=%Rrc\n", rc2);

            /* Make sure to also close the root session (session 0). */
            rc2 = VGSvcGstCtrlSessionClose(&g_Session);
            AssertRC(rc2);

            rc2 = VbglR3GuestCtrlSessionHasChanged(g_idControlSvcClient, g_idControlSession);
            AssertRC(rc2);

            /* Invalidate the internal state to match the current host we got restored from. */
            rc2 = vgsvcGstCtrlInvalidate();
            AssertRC(rc2);
        }
        else
        {
            /* Note: VERR_GEN_IO_FAILURE seems to be normal if ran into timeout. */
            /** @todo r=bird: Above comment makes no sense.  How can you get a timeout in a blocking HGCM call? */
            VGSvcError("GstCtrl: Getting host message failed with %Rrc\n", rc);

            /* Check for VM session change. */
            /** @todo  We don't need to check the host here.  */
            uint64_t idNewSession = g_idControlSession;
            int rc2 = VbglR3GetSessionId(&idNewSession);
            if (   RT_SUCCESS(rc2)
                && (idNewSession != g_idControlSession))
            {
                VGSvcVerbose(1, "GstCtrl: The VM session ID changed\n");
                g_idControlSession = idNewSession;

                /* Close all opened guest sessions -- all context IDs, sessions etc.
                 * are now invalid. */
                rc2 = VGSvcGstCtrlSessionClose(&g_Session);
                AssertRC(rc2);

                /* Do a reconnect. */
                VGSvcVerbose(1, "Reconnecting to HGCM service ...\n");
                rc2 = VbglR3GuestCtrlConnect(&g_idControlSvcClient);
                if (RT_SUCCESS(rc2))
                {
                    VGSvcVerbose(3, "Guest control service client ID=%RU32\n", g_idControlSvcClient);
                    cRetrievalFailed = 0;
                    continue; /* Skip waiting. */
                }
                VGSvcError("Unable to re-connect to HGCM service, rc=%Rrc, bailing out\n", rc);
                break;
            }

            if (rc == VERR_INTERRUPTED)
                RTThreadYield();        /* To be on the safe side... */
            else if (++cRetrievalFailed <= 16) /** @todo Make this configurable? */
                RTThreadSleep(1000);    /* Wait a bit before retrying. */
            else
            {
                VGSvcError("Too many failed attempts in a row to get next message, bailing out\n");
                break;
            }
        }
    }

    VGSvcVerbose(0, "Guest control service stopped\n");

    /* Delete scratch buffer. */
    if (pvScratchBuf)
        RTMemFree(pvScratchBuf);

    VGSvcVerbose(0, "Guest control worker returned with rc=%Rrc\n", rc);
    return rc;
}


static int vgsvcGstCtrlHandleSessionOpen(PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the message parameters.
     */
    PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pStartupInfo;
    int rc = VbglR3GuestCtrlSessionGetOpen(pHostCtx, &pStartupInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Flat out refuse to work with protocol v1 hosts.
         */
        if (pStartupInfo->uProtocol == 2)
        {
            pHostCtx->uProtocol = pStartupInfo->uProtocol;
            VGSvcVerbose(3, "Client ID=%RU32 now is using protocol %RU32\n", pHostCtx->uClientID, pHostCtx->uProtocol);

/** @todo Someone explain why this code isn't in this file too?  v1 support? */
            rc = VGSvcGstCtrlSessionThreadCreate(&g_lstControlSessionThreads, pStartupInfo, NULL /* ppSessionThread */);
            /* Report failures to the host (successes are taken care of by the session thread). */
        }
        else
        {
            VGSvcError("The host wants to use protocol v%u, we only support v2!\n", pStartupInfo->uProtocol);
            rc = VERR_VERSION_MISMATCH;
        }
        if (RT_FAILURE(rc))
        {
            int rc2 = VbglR3GuestCtrlSessionNotify(pHostCtx, GUEST_SESSION_NOTIFYTYPE_ERROR, rc);
            if (RT_FAILURE(rc2))
                VGSvcError("Reporting session error status on open failed with rc=%Rrc\n", rc2);
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for opening guest session: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }

    VbglR3GuestCtrlSessionStartupInfoFree(pStartupInfo);
    pStartupInfo = NULL;

    VGSvcVerbose(3, "Opening a new guest session returned rc=%Rrc\n", rc);
    return rc;
}


static int vgsvcGstCtrlHandleSessionClose(PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    uint32_t idSession;
    uint32_t fFlags;
    int rc = VbglR3GuestCtrlSessionGetClose(pHostCtx, &fFlags, &idSession);
    if (RT_SUCCESS(rc))
    {
        rc = VERR_NOT_FOUND;

        PVBOXSERVICECTRLSESSIONTHREAD pThread;
        RTListForEach(&g_lstControlSessionThreads, pThread, VBOXSERVICECTRLSESSIONTHREAD, Node)
        {
            if (   pThread->pStartupInfo
                && pThread->pStartupInfo->uSessionID == idSession)
            {
                rc = VGSvcGstCtrlSessionThreadDestroy(pThread, fFlags);
                break;
            }
        }

#if 0 /** @todo A bit of a mess here as this message goes to both to this process (master) and the session process. */
        if (RT_FAILURE(rc))
        {
            /* Report back on failure. On success this will be done
             * by the forked session thread. */
            int rc2 = VbglR3GuestCtrlSessionNotify(pHostCtx,
                                                   GUEST_SESSION_NOTIFYTYPE_ERROR, rc);
            if (RT_FAILURE(rc2))
            {
                VGSvcError("Reporting session error status on close failed with rc=%Rrc\n", rc2);
                if (RT_SUCCESS(rc))
                    rc = rc2;
            }
        }
#endif
        VGSvcVerbose(2, "Closing guest session %RU32 returned rc=%Rrc\n", idSession, rc);
    }
    else
    {
        VGSvcError("Error fetching parameters for closing guest session: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vgsvcGstCtrlStop(void)
{
    VGSvcVerbose(3, "Stopping ...\n");

    /** @todo Later, figure what to do if we're in RTProcWait(). It's a very
     *        annoying call since doesn't support timeouts in the posix world. */
    if (g_hControlEvent != NIL_RTSEMEVENTMULTI)
        RTSemEventMultiSignal(g_hControlEvent);

    /*
     * Ask the host service to cancel all pending requests for the main
     * control thread so that we can shutdown properly here.
     */
    if (g_idControlSvcClient)
    {
        VGSvcVerbose(3, "Cancelling pending waits (client ID=%u) ...\n",
                           g_idControlSvcClient);

        int rc = VbglR3GuestCtrlCancelPendingWaits(g_idControlSvcClient);
        if (RT_FAILURE(rc))
            VGSvcError("Cancelling pending waits failed; rc=%Rrc\n", rc);
    }
}


/**
 * Destroys all guest process threads which are still active.
 */
static void vgsvcGstCtrlShutdown(void)
{
    VGSvcVerbose(2, "Shutting down ...\n");

    int rc2 = VGSvcGstCtrlSessionThreadDestroyAll(&g_lstControlSessionThreads, 0 /* Flags */);
    if (RT_FAILURE(rc2))
        VGSvcError("Closing session threads failed with rc=%Rrc\n", rc2);

    rc2 = VGSvcGstCtrlSessionClose(&g_Session);
    if (RT_FAILURE(rc2))
        VGSvcError("Closing session failed with rc=%Rrc\n", rc2);

    VGSvcVerbose(2, "Shutting down complete\n");
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnTerm}
 */
static DECLCALLBACK(void) vgsvcGstCtrlTerm(void)
{
    VGSvcVerbose(3, "Terminating ...\n");

    vgsvcGstCtrlShutdown();

    VGSvcVerbose(3, "Disconnecting client ID=%u ...\n", g_idControlSvcClient);
    VbglR3GuestCtrlDisconnect(g_idControlSvcClient);
    g_idControlSvcClient = 0;

    if (g_hControlEvent != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiDestroy(g_hControlEvent);
        g_hControlEvent = NIL_RTSEMEVENTMULTI;
    }
}


/**
 * The 'vminfo' service description.
 */
VBOXSERVICE g_Control =
{
    /* pszName. */
    "control",
    /* pszDescription. */
    "Host-driven Guest Control",
    /* pszUsage. */
#ifdef DEBUG
    "           [--control-dump-stderr] [--control-dump-stdout]\n"
#endif
    "           [--control-interval <ms>]"
    ,
    /* pszOptions. */
#ifdef DEBUG
    "    --control-dump-stderr   Dumps all guest proccesses stderr data to the\n"
    "                            temporary directory.\n"
    "    --control-dump-stdout   Dumps all guest proccesses stdout data to the\n"
    "                            temporary directory.\n"
#endif
    "    --control-interval      Specifies the interval at which to check for\n"
    "                            new control messages. The default is 1000 ms.\n"
    ,
    /* methods */
    vgsvcGstCtrlPreInit,
    vgsvcGstCtrlOption,
    vgsvcGstCtrlInit,
    vgsvcGstCtrlWorker,
    vgsvcGstCtrlStop,
    vgsvcGstCtrlTerm
};

