/* $Id: Helper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VBox frontends: VBoxSDL (simple frontend based on SDL):
 * Miscellaneous helpers
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_GUI
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include "VBoxSDL.h"
#include "Helper.h"


/**
 * Globals
 */


#ifdef USE_XPCOM_QUEUE_THREAD

/** global flag indicating that the event queue thread should terminate */
static bool volatile   g_fTerminateXPCOMQueueThread = false;

/** How many XPCOM user events are on air. Only allow one pending event to
 *  prevent an overflow of the SDL event queue. */
static volatile int32_t g_s32XPCOMEventsPending;

/** Semaphore the XPCOM event thread will sleep on while it waits for the main thread to process pending requests. */
RTSEMEVENT    g_EventSemXPCOMQueueThread = NULL;

/**
 * Thread method to wait for XPCOM events and notify the SDL thread.
 *
 * @returns Error code
 * @param   thread  Thread ID
 * @param   pvUser  User specific parameter, the file descriptor
 *                  of the event queue socket
 */
DECLCALLBACK(int) xpcomEventThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    int eqFD = (intptr_t)pvUser;
    unsigned cErrors = 0;
    int rc;

    /* Wait with the processing till the main thread needs it. */
    RTSemEventWait(g_EventSemXPCOMQueueThread, 2500);

    do
    {
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(eqFD, &fdset);
        int n = select(eqFD + 1, &fdset, NULL, NULL, NULL);

        /* are there any events to process? */
        if ((n > 0) && !g_fTerminateXPCOMQueueThread)
        {
            /*
             * Wait until all XPCOM events are processed. 1s just for sanity.
             */
            int iWait = 1000;
            /*
             * Don't post an event if there is a pending XPCOM event to prevent an
             * overflow of the SDL event queue.
             */
            if (g_s32XPCOMEventsPending < 1)
            {
                /*
                 * Post the event and wait for it to be processed. If we don't wait,
                 * we'll flood the queue on SMP systems and when the main thread is busy.
                 * In the event of a push error, we'll yield the timeslice and retry.
                 */
                SDL_Event event = {0};
                event.type = SDL_USEREVENT;
                event.user.type = SDL_USER_EVENT_XPCOM_EVENTQUEUE;
                rc = SDL_PushEvent(&event);
                if (!rc)
                {
                    /* success */
                    ASMAtomicIncS32(&g_s32XPCOMEventsPending);
                    cErrors = 0;
                }
                else
                {
                    /* failure */
                    cErrors++;
                    if (!RTThreadYield())
                        RTThreadSleep(2);
                    iWait = (cErrors >= 10) ? RT_MIN(cErrors - 8, 50) : 0;
                }
            }
            else
                Log2(("not enqueueing SDL XPCOM event (%d)\n", g_s32XPCOMEventsPending));

            if (iWait)
                RTSemEventWait(g_EventSemXPCOMQueueThread, iWait);
        }
    } while (!g_fTerminateXPCOMQueueThread);
    return VINF_SUCCESS;
}

/**
 * Creates the XPCOM event thread
 *
 * @returns VBOX status code
 * @param   eqFD XPCOM event queue file descriptor
 */
int startXPCOMEventQueueThread(int eqFD)
{
    int rc = RTSemEventCreate(&g_EventSemXPCOMQueueThread);
    if (RT_SUCCESS(rc))
    {
        RTTHREAD Thread;
        rc = RTThreadCreate(&Thread, xpcomEventThread, (void *)(intptr_t)eqFD,
                            0, RTTHREADTYPE_MSG_PUMP, 0, "XPCOMEvent");
    }
    AssertRC(rc);
    return rc;
}

/**
 * Notify the XPCOM thread that we consumed an XPCOM event.
 */
void consumedXPCOMUserEvent(void)
{
    ASMAtomicDecS32(&g_s32XPCOMEventsPending);
}

/**
 * Signal to the XPCOM even queue thread that it should select for more events.
 */
void signalXPCOMEventQueueThread(void)
{
    int rc = RTSemEventSignal(g_EventSemXPCOMQueueThread);
    AssertRC(rc);
}

/**
 * Indicates to the XPCOM thread that it should terminate now.
 */
void terminateXPCOMQueueThread(void)
{
    g_fTerminateXPCOMQueueThread = true;
    if (g_EventSemXPCOMQueueThread)
    {
        RTSemEventSignal(g_EventSemXPCOMQueueThread);
        RTThreadYield();
    }
}

#endif /* USE_XPCOM_QUEUE_THREAD */
