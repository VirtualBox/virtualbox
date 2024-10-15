/* $Id: poll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Polling I/O Handles, Windows+Posix Implementation.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/cdefs.h>
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>

#elif defined(RT_OS_OS2)
# define INCL_BASE
# include <os2.h>
# include <limits.h>
# include <sys/socket.h>

#else
# include <limits.h>
# include <errno.h>
# include <sys/poll.h>
# if defined(RT_OS_SOLARIS)
#  include <sys/socket.h>
# endif
#endif

#include <iprt/poll.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/pipe.h>
#include <iprt/socket.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include "internal/pipe.h"
#define IPRT_INTERNAL_SOCKET_POLLING_ONLY
#include "internal/socket.h"
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The maximum poll set size.
 * @remarks To help portability, we set this to the Windows limit. We can lift
 *          this restriction later if it becomes necessary. */
#define RTPOLL_SET_MAX     64



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Handle entry in a poll set.
 */
typedef struct RTPOLLSETHNDENT
{
    /** The handle type. */
    RTHANDLETYPE    enmType;
    /** The handle ID. */
    uint32_t        id;
    /** The events we're waiting for here. */
    uint32_t        fEvents;
    /** Set if this is the final entry for this handle.
     * If the handle is entered more than once, this will be clear for all but
     * the last entry. */
    bool            fFinalEntry;
    /** The handle union. */
    RTHANDLEUNION   u;
} RTPOLLSETHNDENT;
/** Pointer to a handle entry. */
typedef RTPOLLSETHNDENT *PRTPOLLSETHNDENT;


/**
 * Poll set data.
 */
typedef struct RTPOLLSETINTERNAL
{
    /** The magic value (RTPOLLSET_MAGIC). */
    uint32_t            u32Magic;
    /** Set when someone is polling or making changes. */
    bool volatile       fBusy;

    /** The number of allocated handles. */
    uint16_t            cHandlesAllocated;
    /** The number of valid handles in the set. */
    uint16_t            cHandles;

#ifdef RT_OS_WINDOWS
    /** Pointer to an array of native handles. */
    HANDLE             *pahNative;
#elif defined(RT_OS_OS2)
    /** The semaphore records. */
    PSEMRECORD          paSemRecs;
    /** The multiple wait semaphore used for non-socket waits. */
    HMUX                hmux;
    /** os2_select template. */
    int                *pafdSelect;
    /** The number of sockets to monitor for read. */
    uint16_t            cReadSockets;
    /** The number of sockets to monitor for write. */
    uint16_t            cWriteSockets;
    /** The number of sockets to monitor for exceptions. */
    uint16_t            cXcptSockets;
    /** The number of pipes. */
    uint16_t            cPipes;
    /** Pointer to an array of native handles. */
    PRTHCINTPTR         pahNative;
#else
    /** Pointer to an array of pollfd structures. */
    struct pollfd      *paPollFds;
#endif
    /** Pointer to an array of handles and IDs. */
    PRTPOLLSETHNDENT    paHandles;
} RTPOLLSETINTERNAL;



/**
 * Common worker for RTPoll and RTPollNoResume
 */
static int rtPollNoResumeWorker(RTPOLLSETINTERNAL *pThis, uint64_t MsStart, RTMSINTERVAL cMillies,
                                uint32_t *pfEvents, uint32_t *pid)
{
    int rc;

    /*
     * Check for special case, RTThreadSleep...
     */
    uint32_t const  cHandles = pThis->cHandles;
    if (cHandles == 0)
    {
        if (RT_LIKELY(cMillies != RT_INDEFINITE_WAIT))
        {
            rc = RTThreadSleep(cMillies);
            if (RT_SUCCESS(rc))
                rc = VERR_TIMEOUT;
        }
        else
            rc = VERR_DEADLOCK;
        return rc;
    }

#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    /*
     * Check + prepare the handles before waiting.
     */
    uint32_t        fEvents  = 0;
    bool const      fNoWait  = cMillies == 0;
    uint32_t        i;
    for (i = 0; i < cHandles; i++)
    {
        switch (pThis->paHandles[i].enmType)
        {
            case RTHANDLETYPE_PIPE:
                fEvents = rtPipePollStart(pThis->paHandles[i].u.hPipe, pThis, pThis->paHandles[i].fEvents,
                                          pThis->paHandles[i].fFinalEntry, fNoWait);
                break;

            case RTHANDLETYPE_SOCKET:
                fEvents = rtSocketPollStart(pThis->paHandles[i].u.hSocket, pThis, pThis->paHandles[i].fEvents,
                                            pThis->paHandles[i].fFinalEntry, fNoWait);
                break;

            default:
                AssertFailed();
                fEvents = UINT32_MAX;
                break;
        }
        if (fEvents)
            break;
    }
    if (   fEvents
        || fNoWait)
    {

        if (pid)
            *pid = pThis->paHandles[i].id;
        if (pfEvents)
            *pfEvents = fEvents;
        rc = !fEvents
           ? VERR_TIMEOUT
           : fEvents != UINT32_MAX
           ? VINF_SUCCESS
           : VERR_INTERNAL_ERROR_4;

        /* clean up */
        if (!fNoWait)
            while (i-- > 0)
            {
                switch (pThis->paHandles[i].enmType)
                {
                    case RTHANDLETYPE_PIPE:
                        rtPipePollDone(pThis->paHandles[i].u.hPipe, pThis->paHandles[i].fEvents,
                                       pThis->paHandles[i].fFinalEntry, false);
                        break;

                    case RTHANDLETYPE_SOCKET:
                        rtSocketPollDone(pThis->paHandles[i].u.hSocket, pThis->paHandles[i].fEvents,
                                         pThis->paHandles[i].fFinalEntry, false);
                        break;

                    default:
                        AssertFailed();
                        break;
                }
            }

        return rc;
    }


    /*
     * Wait.
     */
# ifdef RT_OS_WINDOWS
    RT_NOREF_PV(MsStart);

    DWORD dwRc = WaitForMultipleObjectsEx(cHandles, pThis->pahNative,
                                          FALSE /*fWaitAll */,
                                          cMillies == RT_INDEFINITE_WAIT ? INFINITE : cMillies,
                                          TRUE /*fAlertable*/);
    AssertCompile(WAIT_OBJECT_0 == 0);
    if (dwRc < WAIT_OBJECT_0 + cHandles)
        rc = VERR_INTERRUPTED;
    else if (dwRc == WAIT_TIMEOUT)
        rc = VERR_TIMEOUT;
    else if (dwRc == WAIT_IO_COMPLETION)
        rc = VERR_INTERRUPTED;
    else if (dwRc == WAIT_FAILED)
        rc = RTErrConvertFromWin32(GetLastError());
    else
    {
        AssertMsgFailed(("%u (%#x)\n", dwRc, dwRc));
        rc = VERR_INTERNAL_ERROR_5;
    }

# else  /* RT_OS_OS2 */
    APIRET      orc;
    ULONG       ulUser   = 0;
    uint16_t    cSockets = pThis->cReadSockets + pThis->cWriteSockets + pThis->cXcptSockets;
    if (cSockets == 0)
    {
        /* Only pipes. */
        AssertReturn(pThis->cPipes > 0, VERR_INTERNAL_ERROR_2);
        orc = DosWaitMuxWaitSem(pThis->hmux,
                                cMillies == RT_INDEFINITE_WAIT ? SEM_INDEFINITE_WAIT : RT_MIN(cMillies, SEM_INDEFINITE_WAIT - 1),
                                &ulUser);
        rc = RTErrConvertFromOS2(orc);
    }
    else
    {
        int *pafdSelect = (int *)alloca(cSockets + 1);
        if (pThis->cPipes == 0)
        {
            /* Only sockets. */
            memcpy(pafdSelect, pThis->pafdSelect, sizeof(pThis->pafdSelect[0]) * (cSockets + 1));
            rc = os2_select(pafdSelect, pThis->cReadSockets, pThis->cWriteSockets, pThis->cXcptSockets,
                            cMillies == RT_INDEFINITE_WAIT ? -1 : (long)RT_MIN(cMillies, LONG_MAX));
            if (rc > 0)
                rc = VINF_SUCCESS;
            else if (rc == 0)
                rc = VERR_TIMEOUT;
            else
                rc = RTErrConvertFromErrno(sock_errno());
        }
        else
        {
            /* Mix of both - taking the easy way out, not optimal, but whatever... */
            do
            {
                orc = DosWaitMuxWaitSem(pThis->hmux, 8, &ulUser);
                if (orc != ERROR_TIMEOUT && orc != ERROR_SEM_TIMEOUT)
                {
                    rc = RTErrConvertFromOS2(orc);
                    break;
                }

                memcpy(pafdSelect, pThis->pafdSelect, sizeof(pThis->pafdSelect[0]) * (cSockets + 1));
                rc = os2_select(pafdSelect, pThis->cReadSockets, pThis->cWriteSockets, pThis->cXcptSockets, 8);
                if (rc != 0)
                {
                    if (rc > 0)
                        rc = VINF_SUCCESS;
                    else
                        rc = RTErrConvertFromErrno(sock_errno());
                    break;
                }
            } while (cMillies == RT_INDEFINITE_WAIT || RTTimeMilliTS() - MsStart < cMillies);
        }
    }
# endif /* RT_OS_OS2 */

    /*
     * Get event (if pending) and do wait cleanup.
     */
    bool fHarvestEvents = true;
    for (i = 0; i < cHandles; i++)
    {
        fEvents = 0;
        switch (pThis->paHandles[i].enmType)
        {
            case RTHANDLETYPE_PIPE:
                fEvents = rtPipePollDone(pThis->paHandles[i].u.hPipe, pThis->paHandles[i].fEvents,
                                         pThis->paHandles[i].fFinalEntry, fHarvestEvents);
                break;

            case RTHANDLETYPE_SOCKET:
                fEvents = rtSocketPollDone(pThis->paHandles[i].u.hSocket, pThis->paHandles[i].fEvents,
                                           pThis->paHandles[i].fFinalEntry, fHarvestEvents);
                break;

            default:
                AssertFailed();
                break;
        }
        if (   fEvents
            && fHarvestEvents)
        {
            Assert(fEvents != UINT32_MAX);
            fHarvestEvents = false;
            if (pfEvents)
                *pfEvents = fEvents;
            if (pid)
                *pid = pThis->paHandles[i].id;
            rc = VINF_SUCCESS;
        }
    }

#else  /* POSIX */

    RT_NOREF_PV(MsStart);

    /* clear the revents. */
    uint32_t i = pThis->cHandles;
    while (i-- > 0)
        pThis->paPollFds[i].revents = 0;

    rc = poll(&pThis->paPollFds[0], pThis->cHandles,
              cMillies == RT_INDEFINITE_WAIT || cMillies >= INT_MAX
              ? -1
              : (int)cMillies);
    if (rc == 0)
        return VERR_TIMEOUT;
    if (rc < 0)
        return RTErrConvertFromErrno(errno);
    for (i = 0; i < pThis->cHandles; i++)
        if (pThis->paPollFds[i].revents)
        {
            if (pfEvents)
            {
                *pfEvents = 0;
                if (pThis->paPollFds[i].revents & (POLLIN
# ifdef POLLRDNORM
                                                   | POLLRDNORM     /* just in case */
# endif
# ifdef POLLRDBAND
                                                   | POLLRDBAND     /* ditto */
# endif
# ifdef POLLPRI
                                                   | POLLPRI        /* ditto */
# endif
# ifdef POLLMSG
                                                   | POLLMSG        /* ditto */
# endif
# ifdef POLLWRITE
                                                   | POLLWRITE       /* ditto */
# endif
# ifdef POLLEXTEND
                                                   | POLLEXTEND      /* ditto */
# endif
                                                   )
                   )
                    *pfEvents |= RTPOLL_EVT_READ;

                if (pThis->paPollFds[i].revents & (POLLOUT
# ifdef POLLWRNORM
                                                   | POLLWRNORM     /* just in case */
# endif
# ifdef POLLWRBAND
                                                   | POLLWRBAND     /* ditto */
# endif
                                                   )
                   )
                    *pfEvents |= RTPOLL_EVT_WRITE;

                if (pThis->paPollFds[i].revents & (POLLERR | POLLHUP | POLLNVAL
# ifdef POLLRDHUP
                                                   | POLLRDHUP
# endif
                                                   )
                   )
                    *pfEvents |= RTPOLL_EVT_ERROR;

# if defined(RT_OS_SOLARIS)
                /* Solaris does not return POLLHUP for sockets, just POLLIN.  Check if a
                   POLLIN should also have RTPOLL_EVT_ERROR set or not, so we present a
                   behaviour more in line with linux and BSDs.  Note that this will not
                   help is only RTPOLL_EVT_ERROR was requested, that will require
                   extending this hack quite a bit further (restart poll):  */
                if (   *pfEvents == RTPOLL_EVT_READ
                    && pThis->paHandles[i].enmType == RTHANDLETYPE_SOCKET)
                {
                    uint8_t abBuf[64];
                    ssize_t rcRecv = recv(pThis->paPollFds[i].fd, abBuf, sizeof(abBuf), MSG_PEEK | MSG_DONTWAIT);
                    if (rcRecv == 0)
                        *pfEvents |= RTPOLL_EVT_ERROR;
                }
# endif
            }
            if (pid)
                *pid = pThis->paHandles[i].id;
            return VINF_SUCCESS;
        }

    AssertFailed();
    RTThreadYield();
    rc = VERR_INTERRUPTED;

#endif /* POSIX */

    return rc;
}


RTDECL(int) RTPoll(RTPOLLSET hPollSet, RTMSINTERVAL cMillies, uint32_t *pfEvents, uint32_t *pid)
{
    RTPOLLSETINTERNAL *pThis = hPollSet;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPOLLSET_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNull(pfEvents);
    AssertPtrNull(pid);

    /*
     * Set the busy flag and do the job.
     */
    AssertReturn(ASMAtomicCmpXchgBool(&pThis->fBusy, true,  false), VERR_CONCURRENT_ACCESS);

    int rc;
    if (cMillies == RT_INDEFINITE_WAIT || cMillies == 0)
    {
        do rc = rtPollNoResumeWorker(pThis, 0, cMillies, pfEvents, pid);
        while (rc == VERR_INTERRUPTED);
    }
    else
    {
        uint64_t MsStart = RTTimeMilliTS();
        rc = rtPollNoResumeWorker(pThis, MsStart, cMillies, pfEvents, pid);
        while (RT_UNLIKELY(rc == VERR_INTERRUPTED))
        {
            if (RTTimeMilliTS() - MsStart >= cMillies)
            {
                rc = VERR_TIMEOUT;
                break;
            }
            rc = rtPollNoResumeWorker(pThis, MsStart, cMillies, pfEvents, pid);
        }
    }

    ASMAtomicWriteBool(&pThis->fBusy, false);

    return rc;
}


RTDECL(int) RTPollNoResume(RTPOLLSET hPollSet, RTMSINTERVAL cMillies, uint32_t *pfEvents, uint32_t *pid)
{
    RTPOLLSETINTERNAL *pThis = hPollSet;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPOLLSET_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNull(pfEvents);
    AssertPtrNull(pid);

    /*
     * Set the busy flag and do the job.
     */
    AssertReturn(ASMAtomicCmpXchgBool(&pThis->fBusy, true,  false), VERR_CONCURRENT_ACCESS);

    int rc;
    if (cMillies == RT_INDEFINITE_WAIT || cMillies == 0)
        rc = rtPollNoResumeWorker(pThis, 0, cMillies, pfEvents, pid);
    else
        rc = rtPollNoResumeWorker(pThis, RTTimeMilliTS(), cMillies, pfEvents, pid);

    ASMAtomicWriteBool(&pThis->fBusy, false);

    return rc;
}


RTDECL(int) RTPollSetCreate(PRTPOLLSET phPollSet)
{
    AssertPtrReturn(phPollSet, VERR_INVALID_POINTER);
    RTPOLLSETINTERNAL *pThis = (RTPOLLSETINTERNAL *)RTMemAlloc(sizeof(RTPOLLSETINTERNAL));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->fBusy                = false;
    pThis->cHandles             = 0;
    pThis->cHandlesAllocated    = 0;
#ifdef RT_OS_WINDOWS
    pThis->pahNative            = NULL;
#elif defined(RT_OS_OS2)
    pThis->hmux                 = NULLHANDLE;
    APIRET orc = DosCreateMuxWaitSem(NULL, &pThis->hmux, 0, NULL, DCMW_WAIT_ANY);
    if (orc != NO_ERROR)
    {
        RTMemFree(pThis);
        return RTErrConvertFromOS2(orc);
    }
    pThis->pafdSelect           = NULL;
    pThis->cReadSockets           = 0;
    pThis->cWriteSockets          = 0;
    pThis->cXcptSockets           = 0;
    pThis->cPipes               = 0;
    pThis->pahNative            = NULL;
#else
    pThis->paPollFds            = NULL;
#endif
    pThis->paHandles            = NULL;
    pThis->u32Magic             = RTPOLLSET_MAGIC;

    *phPollSet = pThis;
    return VINF_SUCCESS;
}


RTDECL(int) RTPollSetDestroy(RTPOLLSET hPollSet)
{
    RTPOLLSETINTERNAL *pThis = hPollSet;
    if (pThis == NIL_RTPOLLSET)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPOLLSET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(ASMAtomicCmpXchgBool(&pThis->fBusy, true,  false), VERR_CONCURRENT_ACCESS);

    ASMAtomicWriteU32(&pThis->u32Magic, ~RTPOLLSET_MAGIC);
#ifdef RT_OS_WINDOWS
    RTMemFree(pThis->pahNative);
    pThis->pahNative = NULL;
#elif defined(RT_OS_OS2)
    DosCloseMuxWaitSem(pThis->hmux);
    pThis->hmux = NULLHANDLE;
    RTMemFree(pThis->pafdSelect);
    pThis->pafdSelect = NULL;
    RTMemFree(pThis->pahNative);
    pThis->pahNative = NULL;
#else
    RTMemFree(pThis->paPollFds);
    pThis->paPollFds = NULL;
#endif
    RTMemFree(pThis->paHandles);
    pThis->paHandles = NULL;
    RTMemFree(pThis);

    return VINF_SUCCESS;
}

#ifdef RT_OS_OS2

/**
 * Checks if @a fd is in the specific socket subset.
 *
 * @returns true / false.
 * @param   pThis       The poll set instance.
 * @param   iStart      The index to start at.
 * @param   cFds        The number of sockets to check.
 * @param   fd          The socket to look for.
 */
static bool rtPollSetOs2IsSocketInSet(RTPOLLSETINTERNAL *pThis, uint16_t iStart, uint16_t cFds, int fd)
{
    int const *pfd = pThis->pafdSelect + iStart;
    while (cFds-- > 0)
    {
        if (*pfd == fd)
            return true;
        pfd++;
    }
    return false;
}


/**
 * Removes a socket from a select template subset.
 *
 * @param   pThis       The poll set instance.
 * @param   iStart      The index to start at.
 * @param   pcSubSet    The subset counter to decrement.
 * @param   fd          The socket to remove.
 */
static void rtPollSetOs2RemoveSocket(RTPOLLSETINTERNAL *pThis, uint16_t iStart, uint16_t *pcFds, int fd)
{
    uint16_t cFds = *pcFds;
    while (cFds-- > 0)
    {
        if (pThis->pafdSelect[iStart] == fd)
            break;
        iStart++;
    }
    AssertReturnVoid(iStart != UINT16_MAX);

    /* Note! We keep a -1 entry at the end of the set, thus the + 1. */
    memmove(&pThis->pafdSelect[iStart],
            &pThis->pafdSelect[iStart + 1],
            pThis->cReadSockets + pThis->cWriteSockets + pThis->cXcptSockets + 1 - 1 - iStart);
    *pcFds -= 1;

    Assert(pThis->pafdSelect[pThis->cReadSockets + pThis->cWriteSockets + pThis->cXcptSockets] == -1);
}


/**
 * Adds a socket to a select template subset.
 *
 * @param   pThis       The poll set instance.
 * @param   iInsert     The insertion point.
 *                      ASSUMED to be at the end of the subset.
 * @param   pcSubSet    The subset counter to increment.
 * @param   fd          The socket to add.
 */
static void rtPollSetOs2AddSocket(RTPOLLSETINTERNAL *pThis, uint16_t iInsert, uint16_t *pcFds, int fd)
{
    Assert(!rtPollSetOs2IsSocketInSet(pThis, iInsert - *pcFds, *pcFds, fd));

    /* Note! We keep a -1 entry at the end of the set, thus the + 1. */
    memmove(&pThis->pafdSelect[iInsert + 1],
            &pThis->pafdSelect[iInsert],
            pThis->cReadSockets + pThis->cWriteSockets + pThis->cXcptSockets + 1 - iInsert);
    pThis->pafdSelect[iInsert] = fd;
    *pcFds += 1;

    Assert(pThis->pafdSelect[pThis->cReadSockets + pThis->cWriteSockets + pThis->cXcptSockets] == -1);
}


/**
 * OS/2 specific RTPollSetAdd worker.
 *
 * @returns IPRT status code.
 * @param   pThis       The poll set instance.
 * @param   i           The index of the new handle (not committed).
 * @param   fEvents     The events to poll for.
 */
static int rtPollSetOs2Add(RTPOLLSETINTERNAL *pThis, unsigned i, uint32_t fEvents)
{
    if (pThis->paHandles[i].enmType == RTHANDLETYPE_SOCKET)
    {
        int const fdSocket = pThis->pahNative[i];
        if (   (fEvents & RTPOLL_EVT_READ)
            && rtPollSetOs2IsSocketInSet(pThis, 0, pThis->cReadSockets, fdSocket))
            rtPollSetOs2AddSocket(pThis, pThis->cReadSockets, &pThis->cReadSockets, fdSocket);

        if (   (fEvents & RTPOLL_EVT_WRITE)
            && rtPollSetOs2IsSocketInSet(pThis, pThis->cReadSockets, pThis->cWriteSockets, fdSocket))
            rtPollSetOs2AddSocket(pThis, pThis->cReadSockets + pThis->cWriteSockets, &pThis->cWriteSockets, fdSocket);

        if (   (fEvents & RTPOLL_EVT_ERROR)
            && rtPollSetOs2IsSocketInSet(pThis, pThis->cReadSockets + pThis->cWriteSockets, pThis->cXcptSockets, fdSocket))
            rtPollSetOs2AddSocket(pThis, pThis->cReadSockets + pThis->cWriteSockets + pThis->cXcptSockets,
                                  &pThis->cXcptSockets, fdSocket);
    }
    else if (pThis->paHandles[i].enmType == RTHANDLETYPE_PIPE)
    {
        SEMRECORD Rec = { (HSEM)pThis->pahNative[i], pThis->paHandles[i].id };
        APIRET orc = DosAddMuxWaitSem(pThis->hmux, &Rec);
        if (orc != NO_ERROR && orc != ERROR_DUPLICATE_HANDLE)
            return RTErrConvertFromOS2(orc);
        pThis->cPipes++;
    }
    else
        AssertFailedReturn(VERR_INTERNAL_ERROR_2);
    return VINF_SUCCESS;
}

#endif /* RT_OS_OS2 */

/**
 * Grows the poll set.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   pThis               The poll set instance.
 * @param   cHandlesNew         The new poll set size.
 */
static int rtPollSetGrow(RTPOLLSETINTERNAL *pThis, uint32_t cHandlesNew)
{
    Assert(cHandlesNew > pThis->cHandlesAllocated);

    /* The common array. */
    void *pvNew = RTMemRealloc(pThis->paHandles, cHandlesNew * sizeof(pThis->paHandles[0]));
    if (!pvNew)
        return VERR_NO_MEMORY;
    pThis->paHandles = (PRTPOLLSETHNDENT)pvNew;


    /* OS specific handles */
#if defined(RT_OS_WINDOWS)
    pvNew = RTMemRealloc(pThis->pahNative, cHandlesNew * sizeof(pThis->pahNative[0]));
    if (!pvNew)
        return VERR_NO_MEMORY;
    pThis->pahNative  = (HANDLE *)pvNew;

#elif defined(RT_OS_OS2)
    pvNew = RTMemRealloc(pThis->pahNative, cHandlesNew * sizeof(pThis->pahNative[0]));
    if (!pvNew)
        return VERR_NO_MEMORY;
    pThis->pahNative  = (PRTHCINTPTR)pvNew;

    pvNew = RTMemRealloc(pThis->pafdSelect, (cHandlesNew * 3 + 1) * sizeof(pThis->pafdSelect[0]));
    if (!pvNew)
        return VERR_NO_MEMORY;
    pThis->pafdSelect  = (int *)pvNew;
    if (pThis->cHandlesAllocated == 0)
        pThis->pafdSelect[0] = -1;

#else
    pvNew = RTMemRealloc(pThis->paPollFds, cHandlesNew * sizeof(pThis->paPollFds[0]));
    if (!pvNew)
        return VERR_NO_MEMORY;
    pThis->paPollFds  = (struct pollfd *)pvNew;

#endif

    pThis->cHandlesAllocated = (uint16_t)cHandlesNew;
    return VINF_SUCCESS;
}


RTDECL(int) RTPollSetAdd(RTPOLLSET hPollSet, PCRTHANDLE pHandle, uint32_t fEvents, uint32_t id)
{
    /*
     * Validate the input (tedious).
     */
    RTPOLLSETINTERNAL *pThis = hPollSet;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPOLLSET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!(fEvents & ~RTPOLL_EVT_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(fEvents, VERR_INVALID_PARAMETER);
    AssertReturn(id != UINT32_MAX, VERR_INVALID_PARAMETER);

    if (!pHandle)
        return VINF_SUCCESS;
    AssertPtrReturn(pHandle, VERR_INVALID_POINTER);
    AssertReturn(pHandle->enmType > RTHANDLETYPE_INVALID && pHandle->enmType < RTHANDLETYPE_END, VERR_INVALID_PARAMETER);

    /*
     * Set the busy flag and do the job.
     */

    int             rc       = VINF_SUCCESS;
    RTHCINTPTR      hNative  = -1;
    RTHANDLEUNION   uh;
    uh.uInt = 0;
    switch (pHandle->enmType)
    {
        case RTHANDLETYPE_PIPE:
            uh.hPipe = pHandle->u.hPipe;
            if (uh.hPipe == NIL_RTPIPE)
                return VINF_SUCCESS;
            rc = rtPipePollGetHandle(uh.hPipe, fEvents, &hNative);
            break;

        case RTHANDLETYPE_SOCKET:
            uh.hSocket = pHandle->u.hSocket;
            if (uh.hSocket == NIL_RTSOCKET)
                return VINF_SUCCESS;
            rc = rtSocketPollGetHandle(uh.hSocket, fEvents, &hNative);
            break;

        case RTHANDLETYPE_FILE:
            AssertMsgFailed(("Files are always ready for reading/writing and thus not pollable. Use native APIs for special devices.\n"));
            rc = VERR_POLL_HANDLE_NOT_POLLABLE;
            break;

        case RTHANDLETYPE_THREAD:
            AssertMsgFailed(("Thread handles are currently not pollable\n"));
            rc = VERR_POLL_HANDLE_NOT_POLLABLE;
            break;

        default:
            AssertMsgFailed(("\n"));
            rc = VERR_POLL_HANDLE_NOT_POLLABLE;
            break;
    }
    if (RT_SUCCESS(rc))
    {
        AssertReturn(ASMAtomicCmpXchgBool(&pThis->fBusy, true,  false), VERR_CONCURRENT_ACCESS);

        uint32_t const i = pThis->cHandles;

        /* Check that the handle ID doesn't exist already. */
        uint32_t iPrev = UINT32_MAX;
        uint32_t j     = i;
        while (j-- > 0)
        {
            if (pThis->paHandles[j].id == id)
            {
                rc = VERR_POLL_HANDLE_ID_EXISTS;
                break;
            }
            if (   pThis->paHandles[j].enmType == pHandle->enmType
                && pThis->paHandles[j].u.uInt  == uh.uInt)
                iPrev = j;
        }

        /* Check that we won't overflow the poll set now. */
        if (    RT_SUCCESS(rc)
            &&  i + 1 > RTPOLL_SET_MAX)
            rc = VERR_POLL_SET_IS_FULL;

        /* Grow the tables if necessary. */
        if (RT_SUCCESS(rc) && i + 1 > pThis->cHandlesAllocated)
            rc = rtPollSetGrow(pThis, pThis->cHandlesAllocated + 32);
        if (RT_SUCCESS(rc))
        {
            /*
             * Add the handles to the two parallel arrays.
             */
#ifdef RT_OS_WINDOWS
            pThis->pahNative[i]             = (HANDLE)hNative;
#elif defined(RT_OS_OS2)
            pThis->pahNative[i]             = hNative;
#else
            pThis->paPollFds[i].fd          = (int)hNative;
            pThis->paPollFds[i].revents     = 0;
            pThis->paPollFds[i].events      = 0;
            if (fEvents & RTPOLL_EVT_READ)
                pThis->paPollFds[i].events |= POLLIN;
            if (fEvents & RTPOLL_EVT_WRITE)
                pThis->paPollFds[i].events |= POLLOUT;
            if (fEvents & RTPOLL_EVT_ERROR)
# ifdef RT_OS_DARWIN
                pThis->paPollFds[i].events |= POLLERR | POLLHUP;
# else
                pThis->paPollFds[i].events |= POLLERR;
# endif
#endif
            pThis->paHandles[i].enmType     = pHandle->enmType;
            pThis->paHandles[i].u           = uh;
            pThis->paHandles[i].id          = id;
            pThis->paHandles[i].fEvents     = fEvents;
            pThis->paHandles[i].fFinalEntry = true;

            if (iPrev != UINT32_MAX)
            {
                Assert(pThis->paHandles[iPrev].fFinalEntry);
                pThis->paHandles[iPrev].fFinalEntry = false;
            }

            /*
             * Validations and OS specific updates.
             */
#ifdef RT_OS_WINDOWS
            /* none */
#elif defined(RT_OS_OS2)
            rc = rtPollSetOs2Add(pThis, i, fEvents);
#else  /* POSIX */
            if (poll(&pThis->paPollFds[i], 1, 0) < 0)
            {
                rc = RTErrConvertFromErrno(errno);
                pThis->paPollFds[i].fd = -1;
            }
#endif /* POSIX */

            if (RT_SUCCESS(rc))
            {
                /*
                 * Commit it to the set.
                 */
                pThis->cHandles++; Assert(pThis->cHandles == i + 1);
                rc = VINF_SUCCESS;
            }
        }
    }

    ASMAtomicWriteBool(&pThis->fBusy, false);
    return rc;
}


RTDECL(int) RTPollSetRemove(RTPOLLSET hPollSet, uint32_t id)
{
    /*
     * Validate the input.
     */
    RTPOLLSETINTERNAL *pThis = hPollSet;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPOLLSET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(id != UINT32_MAX, VERR_INVALID_PARAMETER);

    /*
     * Set the busy flag and do the job.
     */
    AssertReturn(ASMAtomicCmpXchgBool(&pThis->fBusy, true,  false), VERR_CONCURRENT_ACCESS);

    int         rc = VERR_POLL_HANDLE_ID_NOT_FOUND;
    uint32_t    i  = pThis->cHandles;
    while (i-- > 0)
        if (pThis->paHandles[i].id == id)
        {
            /* Save some details for the duplicate searching. */
            bool const          fFinalEntry     = pThis->paHandles[i].fFinalEntry;
            RTHANDLETYPE const  enmType         = pThis->paHandles[i].enmType;
            RTHANDLEUNION const uh              = pThis->paHandles[i].u;
#ifdef RT_OS_OS2
            uint32_t            fRemovedEvents  = pThis->paHandles[i].fEvents;
            RTHCINTPTR const    hNative         = pThis->pahNative[i];
#endif

            /* Remove the entry. */
            pThis->cHandles--;
            size_t const cToMove = pThis->cHandles - i;
            if (cToMove)
            {
                memmove(&pThis->paHandles[i], &pThis->paHandles[i + 1], cToMove * sizeof(pThis->paHandles[i]));
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
                memmove(&pThis->pahNative[i], &pThis->pahNative[i + 1], cToMove * sizeof(pThis->pahNative[i]));
#else
                memmove(&pThis->paPollFds[i], &pThis->paPollFds[i + 1], cToMove * sizeof(pThis->paPollFds[i]));
#endif
            }

            /* Check for duplicate and set the fFinalEntry flag. */
            if (fFinalEntry)
                while (i-- > 0)
                    if (   pThis->paHandles[i].u.uInt  == uh.uInt
                        && pThis->paHandles[i].enmType == enmType)
                    {
                        Assert(!pThis->paHandles[i].fFinalEntry);
                        pThis->paHandles[i].fFinalEntry = true;
                        break;
                    }

#ifdef RT_OS_OS2
            /*
             * Update OS/2 wait structures.
             */
            uint32_t fNewEvents = 0;
            i = pThis->cHandles;
            while (i-- > 0)
                if (   pThis->paHandles[i].u.uInt  == uh.uInt
                    && pThis->paHandles[i].enmType == enmType)
                    fNewEvents |= pThis->paHandles[i].fEvents;
            if (enmType == RTHANDLETYPE_PIPE)
            {
                pThis->cPipes--;
                if (fNewEvents == 0)
                {
                    APIRET orc = DosDeleteMuxWaitSem(pThis->hmux, (HSEM)hNative);
                    AssertMsg(orc == NO_ERROR, ("%d\n", orc));
                }
            }
            else if (   fNewEvents != (fNewEvents | fRemovedEvents)
                     && enmType == RTHANDLETYPE_SOCKET)
            {
                fRemovedEvents = fNewEvents ^ (fNewEvents | fRemovedEvents);
                if (fRemovedEvents & RTPOLL_EVT_ERROR)
                    rtPollSetOs2RemoveSocket(pThis, pThis->cReadSockets + pThis->cWriteSockets, &pThis->cXcptSockets, (int)hNative);
                if (fRemovedEvents & RTPOLL_EVT_WRITE)
                    rtPollSetOs2RemoveSocket(pThis, pThis->cReadSockets, &pThis->cWriteSockets, (int)hNative);
                if (fRemovedEvents & RTPOLL_EVT_READ)
                    rtPollSetOs2RemoveSocket(pThis, 0, &pThis->cReadSockets, (int)hNative);
            }
#endif /* RT_OS_OS2 */
            rc = VINF_SUCCESS;
            break;
        }

    ASMAtomicWriteBool(&pThis->fBusy, false);
    return rc;
}


RTDECL(int) RTPollSetQueryHandle(RTPOLLSET hPollSet, uint32_t id, PRTHANDLE pHandle)
{
    /*
     * Validate the input.
     */
    RTPOLLSETINTERNAL *pThis = hPollSet;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPOLLSET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(id != UINT32_MAX, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pHandle, VERR_INVALID_POINTER);

    /*
     * Set the busy flag and do the job.
     */
    AssertReturn(ASMAtomicCmpXchgBool(&pThis->fBusy, true,  false), VERR_CONCURRENT_ACCESS);

    int         rc = VERR_POLL_HANDLE_ID_NOT_FOUND;
    uint32_t    i  = pThis->cHandles;
    while (i-- > 0)
        if (pThis->paHandles[i].id == id)
        {
            if (pHandle)
            {
                pHandle->enmType = pThis->paHandles[i].enmType;
                pHandle->u       = pThis->paHandles[i].u;
            }
            rc = VINF_SUCCESS;
            break;
        }

    ASMAtomicWriteBool(&pThis->fBusy, false);
    return rc;
}


RTDECL(uint32_t) RTPollSetGetCount(RTPOLLSET hPollSet)
{
    /*
     * Validate the input.
     */
    RTPOLLSETINTERNAL *pThis = hPollSet;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTPOLLSET_MAGIC, UINT32_MAX);

    /*
     * Set the busy flag and do the job.
     */
    AssertReturn(ASMAtomicCmpXchgBool(&pThis->fBusy, true,  false), UINT32_MAX);
    uint32_t cHandles = pThis->cHandles;
    ASMAtomicWriteBool(&pThis->fBusy, false);

    return cHandles;
}

RTDECL(int) RTPollSetEventsChange(RTPOLLSET hPollSet, uint32_t id, uint32_t fEvents)
{
    /*
     * Validate the input.
     */
    RTPOLLSETINTERNAL *pThis = hPollSet;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPOLLSET_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(id != UINT32_MAX, VERR_INVALID_PARAMETER);
    AssertReturn(!(fEvents & ~RTPOLL_EVT_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(fEvents, VERR_INVALID_PARAMETER);

    /*
     * Set the busy flag and do the job.
     */
    AssertReturn(ASMAtomicCmpXchgBool(&pThis->fBusy, true,  false), VERR_CONCURRENT_ACCESS);

    int         rc = VERR_POLL_HANDLE_ID_NOT_FOUND;
    uint32_t    i  = pThis->cHandles;
    while (i-- > 0)
        if (pThis->paHandles[i].id == id)
        {
            if (pThis->paHandles[i].fEvents != fEvents)
            {
#if defined(RT_OS_WINDOWS)
                /*nothing*/
#elif defined(RT_OS_OS2)
                if (pThis->paHandles[i].enmType == RTHANDLETYPE_SOCKET)
                {
                    uint32_t fOldEvents = 0;
                    uint32_t j          = pThis->cHandles;
                    while (j-- > 0)
                        if (   pThis->paHandles[j].enmType == RTHANDLETYPE_SOCKET
                            && pThis->paHandles[j].u.uInt  == pThis->paHandles[i].u.uInt
                            && j != i)
                            fOldEvents |= pThis->paHandles[j].fEvents;
                    uint32_t fNewEvents = fOldEvents | fEvents;
                    fOldEvents |= pThis->paHandles[i].fEvents;
                    if (fOldEvents != fEvents)
                    {
                        int const       fdSocket = pThis->pahNative[i];
                        uint32_t const  fChangedEvents = fOldEvents ^ fNewEvents;

                        if ((fChangedEvents & RTPOLL_EVT_READ) && (fNewEvents & RTPOLL_EVT_READ))
                            rtPollSetOs2AddSocket(pThis, pThis->cReadSockets, &pThis->cReadSockets, fdSocket);
                        else if (fChangedEvents & RTPOLL_EVT_READ)
                            rtPollSetOs2RemoveSocket(pThis, 0, &pThis->cReadSockets, fdSocket);

                        if ((fChangedEvents & RTPOLL_EVT_WRITE) && (fNewEvents & RTPOLL_EVT_WRITE))
                            rtPollSetOs2AddSocket(pThis, pThis->cReadSockets + pThis->cWriteSockets,
                                                  &pThis->cWriteSockets, fdSocket);
                        else if (fChangedEvents & RTPOLL_EVT_WRITE)
                            rtPollSetOs2RemoveSocket(pThis, pThis->cReadSockets, &pThis->cWriteSockets, fdSocket);

                        if ((fChangedEvents & RTPOLL_EVT_ERROR) && (fNewEvents & RTPOLL_EVT_ERROR))
                            rtPollSetOs2AddSocket(pThis, pThis->cReadSockets + pThis->cWriteSockets + pThis->cXcptSockets,
                                                  &pThis->cXcptSockets, fdSocket);
                        else if (fChangedEvents & RTPOLL_EVT_ERROR)
                            rtPollSetOs2RemoveSocket(pThis, pThis->cReadSockets + pThis->cWriteSockets, &pThis->cXcptSockets,
                                                     fdSocket);
                    }
                }
#else
                pThis->paPollFds[i].events  = 0;
                if (fEvents & RTPOLL_EVT_READ)
                    pThis->paPollFds[i].events |= POLLIN;
                if (fEvents & RTPOLL_EVT_WRITE)
                    pThis->paPollFds[i].events |= POLLOUT;
                if (fEvents & RTPOLL_EVT_ERROR)
                    pThis->paPollFds[i].events |= POLLERR;
#endif
                pThis->paHandles[i].fEvents = fEvents;
            }
            rc = VINF_SUCCESS;
            break;
        }

    ASMAtomicWriteBool(&pThis->fBusy, false);
    return rc;
}

