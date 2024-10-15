/* $Id: once.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Execute Once.
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
#include <iprt/once.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#ifdef IN_RING3
# include <iprt/critsect.h>
# define RTONCE_USE_CRITSECT_FOR_TERM
#elif defined(IN_RING0)
# include <iprt/spinlock.h>
# define RTONCE_USE_SPINLOCK_FOR_TERM
#else
# define RTONCE_NO_TERM
#endif
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifndef RTONCE_NO_TERM
/** For initializing the clean-up list code. */
static RTONCE           g_OnceCleanUp = RTONCE_INITIALIZER;
/** Lock protecting the clean-up list. */
#ifdef RTONCE_USE_CRITSECT_FOR_TERM
static RTCRITSECT       g_CleanUpCritSect;
#else
static RTSEMFASTMUTEX   g_hCleanUpLock;
#endif
/** The clean-up list. */
static RTLISTANCHOR     g_CleanUpList;

/** Locks the clean-up list. */
#ifdef RTONCE_USE_CRITSECT_FOR_TERM
# define RTONCE_CLEANUP_LOCK()      RTCritSectEnter(&g_CleanUpCritSect)
#else
# define RTONCE_CLEANUP_LOCK()      RTSemFastMutexRequest(g_hCleanUpLock);
#endif

/** Unlocks the clean-up list. */
#ifdef RTONCE_USE_CRITSECT_FOR_TERM
# define RTONCE_CLEANUP_UNLOCK()    RTCritSectLeave(&g_CleanUpCritSect);
#else
# define RTONCE_CLEANUP_UNLOCK()    RTSemFastMutexRelease(g_hCleanUpLock);
#endif



/** @callback_method_impl{FNRTTERMCALLBACK} */
static DECLCALLBACK(void) rtOnceTermCallback(RTTERMREASON enmReason, int32_t iStatus, void *pvUser)
{
    bool const fLazyCleanUpOk = RTTERMREASON_IS_LAZY_CLEANUP_OK(enmReason);
    RTONCE_CLEANUP_LOCK();      /* Potentially dangerous. */

    PRTONCE pCur, pPrev;
    RTListForEachReverseSafe(&g_CleanUpList, pCur, pPrev, RTONCE, CleanUpNode)
    {
        /*
         * Mostly reset it before doing the callback.
         *
         * Should probably introduce some new states here, but I'm not sure
         * it's really worth it at this point.
         */
        PFNRTONCECLEANUP    pfnCleanUp    = pCur->pfnCleanUp;
        void               *pvUserCleanUp = pCur->pvUser;
        pCur->pvUser        = NULL;
        pCur->pfnCleanUp    = NULL;
        ASMAtomicWriteS32(&pCur->rc, VERR_WRONG_ORDER);

        pfnCleanUp(pvUserCleanUp, fLazyCleanUpOk);

        /*
         * Reset the reset of the state if we're being unloaded or smth.
         */
        if (!fLazyCleanUpOk)
        {
            ASMAtomicWriteS32(&pCur->rc, VERR_INTERNAL_ERROR);
            ASMAtomicWriteS32(&pCur->iState, RTONCESTATE_UNINITIALIZED);
        }
    }

    RTONCE_CLEANUP_UNLOCK();

    /*
     * Reset our own structure and the critsect / mutex.
     */
    if (!fLazyCleanUpOk)
    {
# ifdef RTONCE_USE_CRITSECT_FOR_TERM
        RTCritSectDelete(&g_CleanUpCritSect);
# else
        RTSemFastMutexDestroy(g_hCleanUpLock);
        g_hCleanUpLock = NIL_RTSEMFASTMUTEX;
# endif

        ASMAtomicWriteS32(&g_OnceCleanUp.rc, VERR_INTERNAL_ERROR);
        ASMAtomicWriteS32(&g_OnceCleanUp.iState, RTONCESTATE_UNINITIALIZED);
    }

    NOREF(pvUser); NOREF(iStatus);
}



/**
 * Initializes the globals (using RTOnce).
 *
 * @returns IPRT status code
 * @param   pvUser              Unused.
 */
static DECLCALLBACK(int32_t) rtOnceInitCleanUp(void *pvUser)
{
    NOREF(pvUser);
    RTListInit(&g_CleanUpList);
# ifdef RTONCE_USE_CRITSECT_FOR_TERM
    int rc = RTCritSectInit(&g_CleanUpCritSect);
# else
    int rc = RTSemFastMutexCreate(&g_hCleanUpLock);
# endif
    if (RT_SUCCESS(rc))
    {
        rc = RTTermRegisterCallback(rtOnceTermCallback, NULL);
        if (RT_SUCCESS(rc))
            return rc;

# ifdef RTONCE_USE_CRITSECT_FOR_TERM
        RTCritSectDelete(&g_CleanUpCritSect);
# else
        RTSemFastMutexDestroy(g_hCleanUpLock);
        g_hCleanUpLock = NIL_RTSEMFASTMUTEX;
# endif
    }
    return rc;
}

#endif /* !RTONCE_NO_TERM */

/**
 * The state loop of the other threads.
 *
 * @returns VINF_SUCCESS when everything went smoothly. IPRT status code if we
 *          encountered trouble.
 * @param   pOnce           The execute once structure.
 * @param   phEvtM          Where to store the semaphore handle so the caller
 *                          can do the cleaning up for us.
 */
static int rtOnceOtherThread(PRTONCE pOnce, PRTSEMEVENTMULTI phEvtM)
{
    uint32_t cYields = 0;
    for (;;)
    {
        int32_t iState = ASMAtomicReadS32(&pOnce->iState);
        switch (iState)
        {
            /*
             * No semaphore, try create one.
             */
            case RTONCESTATE_BUSY_NO_SEM:
                if (ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_BUSY_CREATING_SEM, RTONCESTATE_BUSY_NO_SEM))
                {
                    int rc = RTSemEventMultiCreate(phEvtM);
                    if (RT_SUCCESS(rc))
                    {
                        ASMAtomicWriteHandle(&pOnce->hEventMulti, *phEvtM);
                        int32_t cRefs = ASMAtomicIncS32(&pOnce->cEventRefs); Assert(cRefs == 1); NOREF(cRefs);

                        if (!ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_BUSY_HAVE_SEM, RTONCESTATE_BUSY_CREATING_SEM))
                        {
                            /* Too slow. */
                            AssertReturn(ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_DONE, RTONCESTATE_DONE_CREATING_SEM)
                                         , VERR_INTERNAL_ERROR_5);

                            ASMAtomicWriteHandle(&pOnce->hEventMulti, NIL_RTSEMEVENTMULTI);
                            cRefs = ASMAtomicDecS32(&pOnce->cEventRefs); Assert(cRefs == 0);

                            RTSemEventMultiDestroy(*phEvtM);
                            *phEvtM = NIL_RTSEMEVENTMULTI;
                        }
                    }
                    else
                    {
                        AssertReturn(   ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_BUSY_SPIN, RTONCESTATE_BUSY_CREATING_SEM)
                                     || ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_DONE,      RTONCESTATE_DONE_CREATING_SEM)
                                     , VERR_INTERNAL_ERROR_4);
                        *phEvtM = NIL_RTSEMEVENTMULTI;
                    }
                }
                break;

            /*
             * This isn't nice, but it's the easy way out.
             */
            case RTONCESTATE_BUSY_CREATING_SEM:
            case RTONCESTATE_BUSY_SPIN:
                cYields++;
                if (!(++cYields % 8))
                    RTThreadSleep(1);
                else
                    RTThreadYield();
                break;

            /*
             * There is a semaphore, try wait on it.
             *
             * We continue waiting after reaching DONE_HAVE_SEM if we
             * already got the semaphore to avoid racing the first thread.
             */
            case RTONCESTATE_DONE_HAVE_SEM:
                if (*phEvtM == NIL_RTSEMEVENTMULTI)
                    return VINF_SUCCESS;
                RT_FALL_THRU();
            case RTONCESTATE_BUSY_HAVE_SEM:
            {
                /*
                 * Grab the semaphore if we haven't got it yet.
                 * We must take care not to increment the counter if it
                 * is 0.  This may happen if we're racing a state change.
                 */
                if (*phEvtM == NIL_RTSEMEVENTMULTI)
                {
                    int32_t cEventRefs = ASMAtomicUoReadS32(&pOnce->cEventRefs);
                    while (   cEventRefs > 0
                           && ASMAtomicUoReadS32(&pOnce->iState) == RTONCESTATE_BUSY_HAVE_SEM)
                    {
                        if (ASMAtomicCmpXchgExS32(&pOnce->cEventRefs, cEventRefs + 1, cEventRefs, &cEventRefs))
                            break;
                        ASMNopPause();
                    }
                    if (cEventRefs <= 0)
                        break;

                    ASMAtomicReadHandle(&pOnce->hEventMulti, phEvtM);
                    AssertReturn(*phEvtM != NIL_RTSEMEVENTMULTI, VERR_INTERNAL_ERROR_2);
                }

                /*
                 * We've got a sempahore, do the actual waiting.
                 */
                do
                    RTSemEventMultiWaitNoResume(*phEvtM, RT_INDEFINITE_WAIT);
                while (ASMAtomicReadS32(&pOnce->iState) == RTONCESTATE_BUSY_HAVE_SEM);
                break;
            }

            case RTONCESTATE_DONE_CREATING_SEM:
            case RTONCESTATE_DONE:
                return VINF_SUCCESS;

            default:
                AssertMsgFailedReturn(("%d\n", iState), VERR_INTERNAL_ERROR_3);
        }
    }
}


RTDECL(int) RTOnceSlow(PRTONCE pOnce, PFNRTONCE pfnOnce, PFNRTONCECLEANUP pfnCleanUp, void *pvUser)
{
    /*
     * Validate input (strict builds only).
     */
    AssertPtr(pOnce);
    AssertPtr(pfnOnce);

    /*
     * Deal with the 'initialized' case first
     */
    int32_t iState = ASMAtomicUoReadS32(&pOnce->iState);
    if (RT_LIKELY(   iState == RTONCESTATE_DONE
                  || iState == RTONCESTATE_DONE_CREATING_SEM
                  || iState == RTONCESTATE_DONE_HAVE_SEM
                 ))
        return ASMAtomicUoReadS32(&pOnce->rc);

    AssertReturn(   iState == RTONCESTATE_UNINITIALIZED
                 || iState == RTONCESTATE_BUSY_NO_SEM
                 || iState == RTONCESTATE_BUSY_SPIN
                 || iState == RTONCESTATE_BUSY_CREATING_SEM
                 || iState == RTONCESTATE_BUSY_HAVE_SEM
                 , VERR_INTERNAL_ERROR);

#ifdef RTONCE_NO_TERM
    AssertReturn(!pfnCleanUp, VERR_NOT_SUPPORTED);
#else /* !RTONCE_NO_TERM */

    /*
     * Make sure our clean-up bits are working if needed later.
     */
    if (pfnCleanUp)
    {
        int rc = RTOnce(&g_OnceCleanUp, rtOnceInitCleanUp, NULL);
        if (RT_FAILURE(rc))
            return rc;
    }
#endif /* !RTONCE_NO_TERM */

    /*
     * Do we initialize it?
     */
    int32_t rcOnce;
    if (   iState == RTONCESTATE_UNINITIALIZED
        && ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_BUSY_NO_SEM, RTONCESTATE_UNINITIALIZED))
    {
        /*
         * Yes, so do the execute once stuff.
         */
        rcOnce = pfnOnce(pvUser);
        ASMAtomicWriteS32(&pOnce->rc, rcOnce);

#ifndef RTONCE_NO_TERM
        /*
         * Register clean-up if requested and we were successful.
         */
        if (pfnCleanUp && RT_SUCCESS(rcOnce))
        {
            RTONCE_CLEANUP_LOCK();

            pOnce->pfnCleanUp = pfnCleanUp;
            pOnce->pvUser     = pvUser;
            RTListAppend(&g_CleanUpList, &pOnce->CleanUpNode);

            RTONCE_CLEANUP_UNLOCK();
        }
#endif /* !RTONCE_NO_TERM */

        /*
         * If there is a sempahore to signal, we're in for some extra work here.
         */
        if (   !ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_DONE,              RTONCESTATE_BUSY_NO_SEM)
            && !ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_DONE,              RTONCESTATE_BUSY_SPIN)
            && !ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_DONE_CREATING_SEM, RTONCESTATE_BUSY_CREATING_SEM)
           )
        {
            /* Grab the sempahore by switching to 'DONE_HAVE_SEM' before reaching 'DONE'. */
            AssertReturn(ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_DONE_HAVE_SEM, RTONCESTATE_BUSY_HAVE_SEM),
                         VERR_INTERNAL_ERROR_2);

            int32_t cRefs = ASMAtomicIncS32(&pOnce->cEventRefs);
            Assert(cRefs > 1); NOREF(cRefs);

            RTSEMEVENTMULTI hEvtM;
            ASMAtomicReadHandle(&pOnce->hEventMulti, &hEvtM);
            Assert(hEvtM != NIL_RTSEMEVENTMULTI);

            ASMAtomicWriteS32(&pOnce->iState, RTONCESTATE_DONE);

            /* Signal it and return. */
            RTSemEventMultiSignal(hEvtM);
        }
    }
    else
    {
        /*
         * Wait for the first thread to complete.  Delegate this to a helper
         * function to simplify cleanup and keep things a bit shorter.
         */
        RTSEMEVENTMULTI hEvtM = NIL_RTSEMEVENTMULTI;
        rcOnce = rtOnceOtherThread(pOnce, &hEvtM);
        if (hEvtM != NIL_RTSEMEVENTMULTI)
        {
            if (ASMAtomicDecS32(&pOnce->cEventRefs) == 0)
            {
                bool fRc;
                ASMAtomicCmpXchgHandle(&pOnce->hEventMulti, NIL_RTSEMEVENTMULTI, hEvtM, fRc);           Assert(fRc);
                fRc = ASMAtomicCmpXchgS32(&pOnce->iState, RTONCESTATE_DONE, RTONCESTATE_DONE_HAVE_SEM); Assert(fRc);
                RTSemEventMultiDestroy(hEvtM);
            }
        }
        if (RT_SUCCESS(rcOnce))
            rcOnce = ASMAtomicUoReadS32(&pOnce->rc);
    }

    return rcOnce;
}
RT_EXPORT_SYMBOL(RTOnceSlow);


RTDECL(void) RTOnceReset(PRTONCE pOnce)
{
    /* Cannot be done while busy! */
    AssertPtr(pOnce);
    Assert(pOnce->hEventMulti == NIL_RTSEMEVENTMULTI);
    int32_t iState = ASMAtomicUoReadS32(&pOnce->iState);
    AssertMsg(   iState == RTONCESTATE_DONE
              || iState == RTONCESTATE_UNINITIALIZED,
              ("%d\n", iState));
    NOREF(iState);

#ifndef RTONCE_NO_TERM
    /* Unregister clean-up. */
    if (pOnce->pfnCleanUp)
    {
        RTONCE_CLEANUP_LOCK();

        RTListNodeRemove(&pOnce->CleanUpNode);
        pOnce->pfnCleanUp = NULL;
        pOnce->pvUser     = NULL;

        RTONCE_CLEANUP_UNLOCK();
    }
#endif /* !RTONCE_NO_TERM */

    /* Do the same as RTONCE_INITIALIZER does. */
    ASMAtomicWriteS32(&pOnce->rc, VERR_INTERNAL_ERROR);
    ASMAtomicWriteS32(&pOnce->iState, RTONCESTATE_UNINITIALIZED);
}
RT_EXPORT_SYMBOL(RTOnceReset);

