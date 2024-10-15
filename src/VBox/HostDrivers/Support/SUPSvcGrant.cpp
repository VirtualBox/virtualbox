/* $Id: SUPSvcGrant.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Service - The Grant Service.
 */

/*
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP   LOG_GROUP_SUP
#include "SUPSvcInternal.h"

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/localipc.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a client instance. */
typedef struct SUPSVCGRANTSESSION *PSUPSVCGRANTSESSION;
/** Pointer to a Grant service instance. */
typedef struct SUPSVCGRANT *PSUPSVCGRANT;


/**
 * Grant service session data.
 */
typedef struct SUPSVCGRANTSESSION
{
    /** Pointer to the next client in the list. */
    PSUPSVCGRANTSESSION                 pNext;
    /** Pointer to the previous client in the list. */
    PSUPSVCGRANTSESSION                 pPrev;
    /** Pointer to the parent (the service instance). */
    PSUPSVCGRANT volatile               pParent;
    /** The local ipc client handle. */
    RTLOCALIPCSESSION volatile          hSession;
    /** Indicate that the thread should terminate ASAP. */
    bool volatile                       fTerminate;
    /** The thread handle. */
    RTTHREAD                            hThread;

} SUPSVCGRANTSESSION;


/**
 * State grant service machine.
 */
typedef enum SUPSVCGRANTSTATE
{
    /** The invalid zero entry. */
    kSupSvcGrantState_Invalid = 0,
    /** Creating - the thread is being started.
     * Next: Paused or Butchered.  */
    kSupSvcGrantState_Creating,
    /** Paused - the thread is blocked on it's user event semaphore.
     * Next: Resuming, Terminating or Butchered.
     * Prev: Creating, Pausing */
    kSupSvcGrantState_Paused,
    /** Resuming - the thread is being unblocked and ushered into RTLocalIpcServiceListen.
     * Next: Listen or Butchered.
     * Prev: Paused */
    kSupSvcGrantState_Resuming,
    /** Listen - the thread is in RTLocalIpcServerListen or setting up an incoming session.
     * Next: Pausing or Butchered.
     * Prev: Resuming */
    kSupSvcGrantState_Listen,
    /** Pausing - Cancelling the listen and dropping any incoming sessions.
     * Next: Paused or Butchered.
     * Prev: Listen */
    kSupSvcGrantState_Pausing,
    /** Butchered - The thread has quit because something when terribly wrong.
     * Next: Destroyed
     * Prev: Any. */
    kSupSvcGrantState_Butchered,
    /** Pausing - Cancelling the listen and dropping any incoming sessions.
     * Next: Destroyed
     * Prev: Paused */
    kSupSvcGrantState_Terminating,
    /** Destroyed - the instance is invalid.
     * Prev: Butchered or Terminating */
    kSupSvcGrantState_Destroyed,
    /** The end of valid state values. */
    kSupSvcGrantState_End,
    /** The usual 32-bit blowup hack. */
    kSupSvcGrantState_32BitHack = 0x7fffffff
} SUPSVCGRANTSTATE;


/**
 * Grant service instance data.
 */
typedef struct SUPSVCGRANT
{
    /** The local ipc server handle. */
    RTLOCALIPCSERVER    hServer;

    /** Critical section serializing access to the session list, the state,
     * the response event, the session event, and the thread event. */
    RTCRITSECT          CritSect;
    /** The service thread will signal this event when it has changed to
     * the 'paused' or 'running' state. */
    RTSEMEVENT          hResponseEvent;
    /** Event that's signaled on session termination. */
    RTSEMEVENT          hSessionEvent;
    /** The handle to the service thread. */
    RTTHREAD            hThread;
    /** Head of the session list. */
    PSUPSVCGRANTSESSION volatile pSessionHead;
    /** The service state. */
    SUPSVCGRANTSTATE volatile enmState;

    /** Critical section serializing access to the SUPR3HardenedVerify APIs. */
    RTCRITSECT          VerifyCritSect;
} SUPSVCGRANT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static const char *supSvcGrantStateName(SUPSVCGRANTSTATE enmState);




/**
 * Services a client session.
 *
 * @returns VINF_SUCCESS.
 *
 * @param   hThread         The thread handle.
 * @param   pvSession       Pointer to the session instance data.
 */
static DECLCALLBACK(int) supSvcGrantSessionThread(RTTHREAD hThread, void *pvSession)
{
    PSUPSVCGRANTSESSION pThis = (PSUPSVCGRANTSESSION)pvSession;
    RTLOCALIPCSESSION   hSession = pThis->hSession;
    Log(("supSvcGrantSessionThread(%p):\n", pThis));

    /*
     * Process client requests until it quits or we're cancelled on termination.
     */
    while (!ASMAtomicUoReadBool(&pThis->fTerminate))
    {
        RTThreadSleep(1000);
        /** @todo */
    }

    /*
     * Clean up the session.
     */
    PSUPSVCGRANT pParent = ASMAtomicReadPtrT(&pThis->pParent, PSUPSVCGRANT);
    if (pParent)
        RTCritSectEnter(&pParent->CritSect);
    else
        Log(("supSvcGrantSessionThread(%p): No parent\n", pThis));

    ASMAtomicXchgHandle(&pThis->hSession, NIL_RTLOCALIPCSESSION, &hSession);
    if (hSession != NIL_RTLOCALIPCSESSION)
        RTLocalIpcSessionClose(hSession);
    else
        Log(("supSvcGrantSessionThread(%p): No session handle\n", pThis));

    if (pParent)
    {
        RTSemEventSignal(pParent->hSessionEvent);
        RTCritSectLeave(&pParent->CritSect);
    }
    Log(("supSvcGrantSessionThread(%p): exits\n"));
    return VINF_SUCCESS;
}


/**
 * Cleans up a session.
 *
 * This is called while inside the grant service critical section.
 *
 * @param   pThis           The session to destroy.
 * @param   pParent         The parent.
 */
static void supSvcGrantSessionDestroy(PSUPSVCGRANTSESSION pThis, PSUPSVCGRANT pParent)
{
    /*
     * Unlink it.
     */
    if (pThis->pNext)
    {
        Assert(pThis->pNext->pPrev == pThis);
        pThis->pNext->pPrev = pThis->pPrev;
    }

    if (pThis->pPrev)
    {
        Assert(pThis->pPrev->pNext == pThis);
        pThis->pPrev->pNext = pThis->pNext;
    }
    else if (pParent->pSessionHead == pThis)
        pParent->pSessionHead = pThis->pNext;

    /*
     * Free the resources associated with it.
     */
    pThis->hThread = NIL_RTTHREAD;
    pThis->pNext = NULL;
    pThis->pPrev = NULL;

    RTLOCALIPCSESSION hSession;
    ASMAtomicXchgHandle(&pThis->hSession, NIL_RTLOCALIPCSESSION, &hSession);
    if (hSession != NIL_RTLOCALIPCSESSION)
        RTLocalIpcSessionClose(hSession);

    RTMemFree(pThis);
}


/**
 * Cleans up zombie sessions, locked.
 *
 * @param   pThis           Pointer to the grant service instance data.
 */
static void supSvcGrantCleanUpSessionsLocked(PSUPSVCGRANT pThis)
{
    /*
     * Iterate until be make it all the way thru the list.
     *
     * Only use the thread state as and indicator on whether we can destroy
     * the session or not.
     */
    PSUPSVCGRANTSESSION pCur;
    do
    {
        for (pCur = pThis->pSessionHead; pCur; pCur = pCur->pNext)
        {
            int rc = RTThreadWait(pCur->hThread, 0, NULL);
            if (RT_SUCCESS(rc))
            {
                supSvcGrantSessionDestroy(pCur, pThis);
                break;
            }

            Assert(rc == VERR_TIMEOUT);
            Assert(pCur->hThread != NIL_RTTHREAD);
            Assert(pCur->pNext != pThis->pSessionHead);
        }
    } while (pCur);
}


/**
 * Cleans up zombie sessions.
 *
 * @returns VINF_SUCCESS, VBox error code on internal error.
 *
 * @param   pThis           Pointer to the grant service instance data.
 * @param   fOwnCritSect    Whether we own the crit sect already. The state is preserved.
 */
static int supSvcGrantCleanUpSessions(PSUPSVCGRANT pThis, bool fOwnCritSect)
{
    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_FAILURE(rc))
    {
        supSvcLogError("supSvcGrantCleanUpSessions: RTCritSectEnter returns %Rrc", rc);
        return rc;
    }

    supSvcGrantCleanUpSessionsLocked(pThis);

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * Gets the state name.
 *
 * @returns The state name string (read only).
 * @param   enmState        The state.
 */
static const char *supSvcGrantStateName(SUPSVCGRANTSTATE enmState)
{
    switch (enmState)
    {
        case kSupSvcGrantState_Invalid:         return "Invalid";
        case kSupSvcGrantState_Creating:        return "Creating";
        case kSupSvcGrantState_Paused:          return "Paused";
        case kSupSvcGrantState_Resuming:        return "Resuming";
        case kSupSvcGrantState_Listen:          return "Listen";
        case kSupSvcGrantState_Pausing:         return "Pausing";
        case kSupSvcGrantState_Butchered:       return "Butchered";
        case kSupSvcGrantState_Terminating:     return "Terminating";
        case kSupSvcGrantState_Destroyed:       return "Destroyed";
        default:                                return "?Unknown?";
    }
}


/**
 * Attempts to flip into the butchered state.
 *
 * @returns rc.
 * @param   pThis           The instance data.
 * @param   fOwnCritSect    Whether we own the crit sect already.
 * @param   pszFailed       What failed.
 * @param   rc              What to return (lazy bird).
 */
static int supSvcGrantThreadButchered(PSUPSVCGRANT pThis, bool fOwnCritSect, const char *pszFailed, int rc)
{
    int rc2 = VINF_SUCCESS;
    if (!fOwnCritSect)
        rc2 = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc2))
    {
        supSvcLogError("supSvcGrantThread(%s): Butchered; %Rrc: %s",
                       supSvcGrantStateName(pThis->enmState), rc, pszFailed);
        pThis->enmState = kSupSvcGrantState_Butchered;

        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;
}


/**
 * Creates a new session.
 *
 * @returns VINF_SUCCESS on success, VBox error code on internal error.
 *
 * @param   pThis           Pointer to the grant service instance data.
 * @param   hSession        The client session handle.
 */
static int supSvcGrantThreadCreateSession(PSUPSVCGRANT pThis, RTLOCALIPCSESSION hSession)
{
    /*
     * Allocate and initialize a new session instance before entering the critsect.
     */
    PSUPSVCGRANTSESSION pSession = (PSUPSVCGRANTSESSION)RTMemAlloc(sizeof(*pSession));
    if (!pSession)
    {
        supSvcLogError("supSvcGrantThreadListen: failed to allocate session");
        return VINF_SUCCESS; /* not fatal? */
    }
    pSession->pPrev = NULL;
    pSession->pNext = NULL;
    pSession->pParent = pThis;
    pSession->hSession = hSession;
    pSession->fTerminate = false;
    pSession->hThread = NIL_RTTHREAD;

    /*
     * Enter the critsect, check the state, link it and fire off the session thread.
     */
    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        /* check the state */
        SUPSVCGRANTSTATE enmState = pThis->enmState;
        if (enmState == kSupSvcGrantState_Listen)
        {
            /* link it */
            pSession->pNext = pThis->pSessionHead;
            if (pThis->pSessionHead)
                pThis->pSessionHead->pPrev = pSession;
            pThis->pSessionHead = pSession;

            /* fire up the thread */
            Log(("supSvcGrantThreadListen: starting session %p\n", pSession));
            rc = RTThreadCreate(&pSession->hThread, supSvcGrantSessionThread, pSession, 0,
                                RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "SESSION");
            if (RT_SUCCESS(rc))
            {
                rc = RTCritSectLeave(&pThis->CritSect);
                if (RT_FAILURE(rc))
                    return supSvcGrantThreadButchered(pThis, false /* fOwnCritSect */, "RTCritSectLeave", rc);

                /*
                 * Successfully handled the client.
                 */
                return VINF_SUCCESS;
            }

            /* bail out */
            supSvcLogError("supSvcGrantThreadListen: RTThreadCreate returns %Rrc", rc);
        }
        else
            Log(("supSvcGrantThreadListen: dropping connection, state %s\n", supSvcGrantStateName(enmState)));

        RTCritSectLeave(&pThis->CritSect);
        rc = VINF_SUCCESS;
    }
    else
        supSvcGrantThreadButchered(pThis, false /* fOwnCritSect */, "RTCritSectEnter", rc);
    RTLocalIpcSessionClose(hSession);
    RTMemFree(pSession);
    return rc;
}


/**
 * Listen for a client session and kicks off the service thread for it.
 *
 * @returns VINF_SUCCESS on normal state change, failure if something gets screwed up.
 *
 * @param   pThis           Pointer to the grant service instance data.
 */
static int supSvcGrantThreadListen(PSUPSVCGRANT pThis)
{
    /*
     * Wait for a client to connect and create a new session.
     */
    RTLOCALIPCSESSION hClientSession = NIL_RTLOCALIPCSESSION;
    int rc = RTLocalIpcServerListen(pThis->hServer, &hClientSession);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CANCELLED)
            LogFlow(("supSvcGrantThreadListen: cancelled\n"));
        else if (rc == VERR_TRY_AGAIN)
            /* for testing */;
        else
            return supSvcGrantThreadButchered(pThis, false /* fOwnCritSect */, "RTLocalIpcServerListen", rc);
        return VINF_SUCCESS;
    }

    return supSvcGrantThreadCreateSession(pThis, hClientSession);
}


/**
 * Grant service thread.
 *
 * This thread is the one listening for clients and kicks off
 * the session threads and stuff.
 *
 * @returns VINF_SUCCESS on normal exit, VBox error status on failure.
 * @param   hThread         The thread handle.
 * @param   pvThis          Pointer to the grant service instance data.
 */
static DECLCALLBACK(int) supSvcGrantThread(RTTHREAD hThread, void *pvThis)
{
    PSUPSVCGRANT pThis = (PSUPSVCGRANT)pvThis;

    /*
     * The state loop.
     */
    for (;;)
    {
        /*
         * Switch on the current state (requires critsect).
         */
        int rc = RTCritSectEnter(&pThis->CritSect);
        if (RT_FAILURE(rc))
        {
            supSvcLogError("supSvcGrantThread - RTCritSectEnter returns %Rrc", rc);
            return rc;
        }

        SUPSVCGRANTSTATE enmState = pThis->enmState;
        LogFlow(("supSvcGrantThread: switching %s\n", supSvcGrantStateName(enmState)));
        switch (enmState)
        {
            case kSupSvcGrantState_Creating:
            case kSupSvcGrantState_Pausing:
                pThis->enmState = kSupSvcGrantState_Paused;
                rc = RTSemEventSignal(pThis->hResponseEvent);
                if (RT_FAILURE(rc))
                    return supSvcGrantThreadButchered(pThis, true /* fOwnCritSect*/, "RTSemEventSignal", rc);
                RT_FALL_THRU();

            case kSupSvcGrantState_Paused:
                RTCritSectLeave(&pThis->CritSect);

                rc = RTThreadUserWait(hThread, 60*1000); /* wake up once in a while (paranoia) */
                if (RT_FAILURE(rc) && rc != VERR_TIMEOUT)
                    return supSvcGrantThreadButchered(pThis, false /* fOwnCritSect*/, "RTThreadUserWait", rc);
                break;

            case kSupSvcGrantState_Resuming:
                pThis->enmState = kSupSvcGrantState_Listen;
                rc = RTSemEventSignal(pThis->hResponseEvent);
                if (RT_FAILURE(rc))
                    return supSvcGrantThreadButchered(pThis, true /* fOwnCritSect*/, "RTSemEventSignal", rc);
                RT_FALL_THRU();

            case kSupSvcGrantState_Listen:
                RTCritSectLeave(&pThis->CritSect);
                rc = supSvcGrantThreadListen(pThis);
                if (RT_FAILURE(rc))
                {
                    Log(("supSvcGrantThread: supSvcGrantDoListening returns %Rrc, exiting\n", rc));
                    return rc;
                }
                break;

            case kSupSvcGrantState_Terminating:
                RTCritSectLeave(&pThis->CritSect);
                Log(("supSvcGrantThread: Done\n"));
                return VINF_SUCCESS;

            case kSupSvcGrantState_Butchered:
            default:
                return supSvcGrantThreadButchered(pThis, true /* fOwnCritSect*/, "Bad state", VERR_INTERNAL_ERROR);
        }

        /*
         * Massage the session list between clients and states.
         */
        rc = supSvcGrantCleanUpSessions(pThis, false /* fOwnCritSect */);
        if (RT_FAILURE(rc))
            return supSvcGrantThreadButchered(pThis, false /* fOwnCritSect */, "supSvcGrantCleanUpSessions", rc);
    }
}


/**
 * Waits for the service thread to respond to a state change.
 *
 * @returns VINF_SUCCESS on success, VERR_TIMEOUT if it doesn't respond in time, other error code on internal error.
 *
 * @param   pThis           Pointer to the grant service instance data.
 * @param   enmCurState     The current state.
 * @param   enmNewState     The new state we're waiting for it to enter.
 */
static int supSvcGrantWait(PSUPSVCGRANT pThis, SUPSVCGRANTSTATE enmCurState, SUPSVCGRANTSTATE enmNewState)
{
    LogFlow(("supSvcGrantWait(,%s,%s): enter\n",
             supSvcGrantStateName(enmCurState), supSvcGrantStateName(enmNewState)));

    /*
     * Wait a short while for the response event to be set.
     */
    RTSemEventWait(pThis->hResponseEvent, 1000);
    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pThis->enmState == enmNewState)
        {
            RTCritSectLeave(&pThis->CritSect);
            rc = VINF_SUCCESS;
        }
        else if (pThis->enmState == enmCurState)
        {
            /*
             * Wait good while longer.
             */
            RTCritSectLeave(&pThis->CritSect);
            rc = RTSemEventWait(pThis->hResponseEvent, 59*1000); /* 59 sec */
            if (RT_SUCCESS(rc) || rc == VERR_TIMEOUT)
            {
                rc = RTCritSectEnter(&pThis->CritSect);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Check the state whether we've succeeded.
                     */
                    SUPSVCGRANTSTATE enmState = pThis->enmState;
                    if (enmState == enmNewState)
                        rc = VINF_SUCCESS;
                    else if (enmState == enmCurState)
                    {
                        supSvcLogError("supSvcGrantWait(,%s,%s) - the thread doesn't respond in a timely manner, failing.",
                                       supSvcGrantStateName(enmCurState), supSvcGrantStateName(enmNewState));
                        rc = VERR_TIMEOUT;
                    }
                    else
                    {
                        supSvcLogError("supSvcGrantWait(,%s,%s) - wrong state %s!",  supSvcGrantStateName(enmCurState),
                                       supSvcGrantStateName(enmNewState), supSvcGrantStateName(enmState));
                        AssertMsgFailed(("%s\n", supSvcGrantStateName(enmState)));
                        rc = VERR_INTERNAL_ERROR;
                    }

                    RTCritSectLeave(&pThis->CritSect);
                }
                else
                    supSvcLogError("supSvcGrantWait(,%s,%s) - RTCritSectEnter returns %Rrc",
                                   supSvcGrantStateName(enmCurState), supSvcGrantStateName(enmNewState));
            }
            else
                supSvcLogError("supSvcGrantWait(,%s,%s) - RTSemEventWait returns %Rrc",
                               supSvcGrantStateName(enmCurState), supSvcGrantStateName(enmNewState));
        }
        else
        {
            supSvcLogError("supSvcGrantWait(,%s,%s) - wrong state %s!",  supSvcGrantStateName(enmCurState),
                           supSvcGrantStateName(enmNewState), supSvcGrantStateName(pThis->enmState));
            AssertMsgFailed(("%s\n", supSvcGrantStateName(pThis->enmState)));
            RTCritSectLeave(&pThis->CritSect);
            rc = VERR_INTERNAL_ERROR;
        }
    }
    else
        supSvcLogError("supSvcGrantWait(,%s,%s) - RTCritSectEnter returns %Rrc",
                       supSvcGrantStateName(enmCurState), supSvcGrantStateName(enmNewState));

    Log(("supSvcGrantWait(,%s,%s): returns %Rrc\n",
         supSvcGrantStateName(enmCurState), supSvcGrantStateName(enmNewState), rc));
    return rc;
}


/** @copydoc SUPSVCSERVICE::pfnCreate */
DECLCALLBACK(int)  supSvcGrantCreate(void **ppvInstance)
{
    LogFlowFuncEnter();

    /*
     * Allocate and initialize the session data.
     */
    PSUPSVCGRANT pThis = (PSUPSVCGRANT)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
    {
        supSvcLogError("supSvcGrantCreate - no memory");
        return VERR_NO_MEMORY;
    }
    bool fFreeIt = true;
    pThis->pSessionHead = NULL;
    pThis->enmState = kSupSvcGrantState_Creating;
    int rc = RTCritSectInit(&pThis->VerifyCritSect);
    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectInit(&pThis->CritSect);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemEventCreate(&pThis->hResponseEvent);
            if (RT_SUCCESS(rc))
            {
                rc = RTSemEventCreate(&pThis->hSessionEvent);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Create the local IPC instance and then finally fire up the thread.
                     */
                    rc = RTLocalIpcServerCreate(&pThis->hServer, SUPSVC_GRANT_SERVICE_NAME, RTLOCALIPC_FLAGS_MULTI_SESSION);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTThreadCreate(&pThis->hThread, supSvcGrantThread, pThis, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "GRANT");
                        if (RT_SUCCESS(rc))
                        {
                            rc = supSvcGrantWait(pThis, kSupSvcGrantState_Creating, kSupSvcGrantState_Paused);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * Successfully created the grant service!
                                 */
                                Log(("supSvcGrantCreate: returns VINF_SUCCESS (pThis=%p)\n", pThis));
                                *ppvInstance = pThis;
                                return VINF_SUCCESS;
                            }

                            /*
                             * The thread FAILED to start in a timely manner!
                             */
                            RTCritSectEnter(&pThis->CritSect);
                            pThis->enmState = kSupSvcGrantState_Terminating;
                            RTCritSectLeave(&pThis->CritSect);

                            RTThreadUserSignal(pThis->hThread);

                            int cTries = 10;
                            int rc2 = RTThreadWait(pThis->hThread, 20000, NULL);
                            if (RT_FAILURE(rc2))
                            {
                                /* poke it a few more times before giving up. */
                                while (--cTries > 0)
                                {
                                    RTThreadUserSignal(pThis->hThread);
                                    RTLocalIpcServerCancel(pThis->hServer);
                                    if (RTThreadWait(pThis->hThread, 1000, NULL) != VERR_TIMEOUT)
                                        break;
                                }
                            }
                            fFreeIt = cTries <= 0;
                        }
                        else
                            supSvcLogError("supSvcGrantCreate - RTThreadCreate returns %Rrc", rc);
                        RTLocalIpcServerDestroy(pThis->hServer);
                        pThis->hServer = NIL_RTLOCALIPCSERVER;
                    }
                    else
                        supSvcLogError("supSvcGrantCreate - RTLocalIpcServiceCreate returns %Rrc", rc);
                    RTSemEventDestroy(pThis->hSessionEvent);
                    pThis->hSessionEvent = NIL_RTSEMEVENT;
                }
                else
                    supSvcLogError("supSvcGrantCreate - RTSemEventCreate returns %Rrc", rc);
                RTSemEventDestroy(pThis->hResponseEvent);
                pThis->hResponseEvent = NIL_RTSEMEVENT;
            }
            else
                supSvcLogError("supSvcGrantCreate - RTSemEventCreate returns %Rrc", rc);
            RTCritSectDelete(&pThis->CritSect);
        }
        else
            supSvcLogError("supSvcGrantCreate - RTCritSectInit returns %Rrc", rc);
        RTCritSectDelete(&pThis->VerifyCritSect);
    }
    else
        supSvcLogError("supSvcGrantCreate - RTCritSectInit returns %Rrc", rc);
    if (fFreeIt)
        RTMemFree(pThis);
    Log(("supSvcGrantCreate: returns %Rrc\n", rc));
    return rc;
}


/** @copydoc SUPSVCSERVICE::pfnStart */
DECLCALLBACK(void) supSvcGrantStart(void *pvInstance)
{
    PSUPSVCGRANT pThis = (PSUPSVCGRANT)pvInstance;

    /*
     * Change the state and signal the thread.
     */
    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        bool fInCritSect = true;
        SUPSVCGRANTSTATE enmState = pThis->enmState;
        if (enmState == kSupSvcGrantState_Paused)
        {
            pThis->enmState = kSupSvcGrantState_Resuming;
            rc = RTThreadUserSignal(pThis->hThread);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Wait for the bugger to respond (no need to bitch here).
                 */
                RTCritSectLeave(&pThis->CritSect);
                supSvcGrantWait(pThis, kSupSvcGrantState_Resuming, kSupSvcGrantState_Listen);
                fInCritSect = false;
            }
        }
        else
            supSvcLogError("supSvcGrantStart - Incorrect state %s!", supSvcGrantStateName(enmState));
        if (fInCritSect)
            RTCritSectLeave(&pThis->CritSect);
    }
    else
    {
        supSvcLogError("supSvcGrantStart - RTCritSectEnter returns %Rrc!", rc);
        AssertRCReturnVoid(rc);
    }
}


/** @copydoc SUPSVCSERVICE::pfnTryStop */
DECLCALLBACK(int)  supSvcGrantTryStop(void *pvInstance)
{
    PSUPSVCGRANT pThis = (PSUPSVCGRANT)pvInstance;

    /*
     * Don't give up immediately.
     */
    uint64_t u64StartTS = RTTimeMilliTS();
    int rc;
    for (;;)
    {
        /*
         * First check the state to make sure the thing is actually running.
         * If the critsect is butchered, just pretend success.
         */
        rc = RTCritSectEnter(&pThis->CritSect);
        if (RT_FAILURE(rc))
        {
            supSvcLogError("supSvcGrantTryStop - RTCritSectEnter returns %Rrc", rc);
            AssertRC(rc);
            return VINF_SUCCESS;
        }
        SUPSVCGRANTSTATE enmState = pThis->enmState;
        if (enmState != kSupSvcGrantState_Listen)
        {
            supSvcLogError("supSvcGrantTryStop - Not running, state: %s", supSvcGrantStateName(enmState));
            RTCritSectLeave(&pThis->CritSect);
            return VINF_SUCCESS;
        }

        /*
         * If there are no clients, usher the thread into the paused state.
         */
        supSvcGrantCleanUpSessionsLocked(pThis);
        if (!pThis->pSessionHead)
        {
            rc = RTThreadUserReset(pThis->hThread);
            pThis->enmState = kSupSvcGrantState_Pausing;
            int rc2 = RTLocalIpcServerCancel(pThis->hServer);
            int rc3 = RTCritSectLeave(&pThis->CritSect);
            if (RT_SUCCESS(rc) && RT_SUCCESS(rc2) && RT_SUCCESS(rc3))
                supSvcGrantWait(pThis, kSupSvcGrantState_Pausing, kSupSvcGrantState_Paused);
            else
            {
                if (RT_FAILURE(rc))
                    supSvcLogError("supSvcGrantTryStop - RTThreadUserReset returns %Rrc", rc);
                if (RT_FAILURE(rc2))
                    supSvcLogError("supSvcGrantTryStop - RTLocalIpcServerCancel returns %Rrc", rc);
                if (RT_FAILURE(rc3))
                    supSvcLogError("supSvcGrantTryStop - RTCritSectLeave returns %Rrc", rc);
            }
            return VINF_SUCCESS;
        }

        /*
         * Check the time limit, otherwise wait for a client event.
         */
        uint64_t u64Elapsed = RTTimeMilliTS() - u64StartTS;
        if (u64Elapsed >= 60*1000) /* 1 min */
        {
            unsigned cSessions = 0;
            for (PSUPSVCGRANTSESSION pCur = pThis->pSessionHead; pCur; pCur = pCur->pNext)
                cSessions++;
            RTCritSectLeave(&pThis->CritSect);

            supSvcLogError("supSvcGrantTryStop - %u active sessions after waiting %u ms", cSessions, (unsigned)u64Elapsed);
            return VERR_TRY_AGAIN;
        }

        rc = RTCritSectLeave(&pThis->CritSect);
        if (RT_FAILURE(rc))
        {
            supSvcLogError("supSvcGrantTryStop - RTCritSectLeave returns %Rrc", rc);
            return VINF_SUCCESS;
        }

        rc = RTSemEventWait(pThis->hSessionEvent, 60*1000 - u64Elapsed);
        if (RT_FAILURE(rc) && rc != VERR_TIMEOUT)
        {
            supSvcLogError("supSvcGrantTryStop - RTSemEventWait returns %Rrc", rc);
            return VINF_SUCCESS;
        }
    }
}


/** @copydoc SUPSVCSERVICE::pfnStopAndDestroy */
DECLCALLBACK(void) supSvcGrantStopAndDestroy(void *pvInstance, bool fRunning)
{
    PSUPSVCGRANT pThis = (PSUPSVCGRANT)pvInstance;
    int rc;

    /*
     * Attempt to stop the service, cancelling blocked server and client calls.
     */
    RTCritSectEnter(&pThis->CritSect);

    SUPSVCGRANTSTATE enmState = pThis->enmState;
    AssertMsg(fRunning == (pThis->enmState == kSupSvcGrantState_Listen),
              ("%RTbool %s\n", fRunning, supSvcGrantStateName(enmState)));

    if (enmState == kSupSvcGrantState_Listen)
    {
        RTThreadUserReset(pThis->hThread);
        pThis->enmState = kSupSvcGrantState_Paused;
        for (PSUPSVCGRANTSESSION pCur = pThis->pSessionHead; pCur; pCur = pCur->pNext)
            ASMAtomicWriteBool(&pCur->fTerminate, true);

        /* try cancel local ipc operations that might be pending */
        RTLocalIpcServerCancel(pThis->hServer);
        for (PSUPSVCGRANTSESSION pCur = pThis->pSessionHead; pCur; pCur = pCur->pNext)
        {
            RTLOCALIPCSESSION hSession;
            ASMAtomicReadHandle(&pCur->hSession, &hSession);
            if (hSession != NIL_RTLOCALIPCSESSION)
                RTLocalIpcSessionCancel(hSession);
        }

        /*
         * Wait for the thread to respond (outside the crit sect).
         */
        RTCritSectLeave(&pThis->CritSect);
        supSvcGrantWait(pThis, kSupSvcGrantState_Pausing, kSupSvcGrantState_Paused);
        RTCritSectEnter(&pThis->CritSect);

        /*
         * Wait for any lingering sessions to exit.
         */
        supSvcGrantCleanUpSessionsLocked(pThis);
        if (pThis->pSessionHead)
        {
            uint64_t u64StartTS = RTTimeMilliTS();
            do
            {
                /* Destroy the sessions since cancelling didn't do the trick. */
                for (PSUPSVCGRANTSESSION pCur = pThis->pSessionHead; pCur; pCur = pCur->pNext)
                {
                    RTLOCALIPCSESSION hSession;
                    ASMAtomicXchgHandle(&pCur->hSession, NIL_RTLOCALIPCSESSION, &hSession);
                    if (hSession != NIL_RTLOCALIPCSESSION)
                    {
                        rc = RTLocalIpcSessionClose(hSession);
                        AssertRC(rc);
                        if (RT_FAILURE(rc))
                            supSvcLogError("supSvcGrantStopAndDestroy: RTLocalIpcSessionClose(%p) returns %Rrc",
                                           (uintptr_t)hSession, rc);
                    }
                }

                /* Check the time. */
                uint64_t u64Elapsed = RTTimeMilliTS() - u64StartTS;
                if (u64Elapsed >= 60*1000) /* 1 min */
                    break;

                /* wait */
                RTCritSectLeave(&pThis->CritSect);
                rc = RTSemEventWait(pThis->hSessionEvent, 60*1000 - u64Elapsed);
                RTCritSectEnter(&pThis->CritSect);
                if (RT_FAILURE(rc) && rc != VERR_TIMEOUT)
                    break;

                /* cleanup and check again */
                supSvcGrantCleanUpSessionsLocked(pThis);
            } while (pThis->pSessionHead);
        }
    }

    /*
     * Tell the service thread to terminate and wait for it to do so.
     */
    pThis->enmState = kSupSvcGrantState_Terminating;
    RTLOCALIPCSERVER hServer;
    ASMAtomicXchgHandle(&pThis->hServer, NIL_RTLOCALIPCSERVER, &hServer);
    RTThreadUserSignal(pThis->hThread);

    RTCritSectLeave(&pThis->CritSect);

    rc = RTThreadWait(pThis->hThread, 20*1000, NULL);
    if (RT_FAILURE(rc) && rc == VERR_TIMEOUT)
    {
        RTThreadUserSignal(pThis->hThread);
        RTLocalIpcServerDestroy(hServer);
        hServer = NIL_RTLOCALIPCSERVER;

        rc = RTThreadWait(pThis->hThread, 40*1000, NULL);
        if (RT_FAILURE(rc))
            supSvcLogError("supSvcGrantStopAndDestroy - RTThreadWait(40 sec) returns %Rrc", rc);
    }
    else if (RT_FAILURE(rc))
        supSvcLogError("supSvcGrantStopAndDestroy - RTThreadWait(20 sec) returns %Rrc", rc);
    pThis->hThread = NIL_RTTHREAD;

    /*
     * Kill the parent pointers of any lingering sessions.
     */
    RTCritSectEnter(&pThis->CritSect);
    pThis->enmState = kSupSvcGrantState_Destroyed;

    supSvcGrantCleanUpSessionsLocked(pThis);
    unsigned cSessions = 0;
    for (PSUPSVCGRANTSESSION pCur = pThis->pSessionHead; pCur; pCur = pCur->pNext)
        ASMAtomicWriteNullPtr(&pCur->pParent);

    RTCritSectLeave(&pThis->CritSect);
    if (cSessions)
        supSvcLogError("supSvcGrantStopAndDestroy: %d session failed to terminate!", cSessions);

    /*
     * Free the resource.
     */
    RTLocalIpcServerDestroy(hServer);

    RTSemEventDestroy(pThis->hResponseEvent);
    pThis->hResponseEvent = NIL_RTSEMEVENT;

    RTSemEventDestroy(pThis->hSessionEvent);
    pThis->hSessionEvent = NIL_RTSEMEVENT;

    RTCritSectDelete(&pThis->VerifyCritSect);
    RTCritSectDelete(&pThis->CritSect);

    RTMemFree(pThis);

    Log(("supSvcGrantStopAndDestroy: done (rc=%Rrc)\n", rc));
}

