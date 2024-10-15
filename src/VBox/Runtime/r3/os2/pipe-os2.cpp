/* $Id: pipe-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Anonymous Pipes, OS/2 Implementation.
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
#define INCL_ERRORS
#define INCL_DOSSEMAPHORES
#include <os2.h>

#include <iprt/pipe.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/poll.h>
#include <iprt/process.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include "internal/pipe.h"
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The pipe buffer size we prefer. */
#define RTPIPE_OS2_SIZE     _32K


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTPIPEINTERNAL
{
    /** Magic value (RTPIPE_MAGIC). */
    uint32_t            u32Magic;
    /** The pipe handle. */
    HPIPE               hPipe;
    /** Set if this is the read end, clear if it's the write end. */
    bool                fRead;
    /** RTPipeFromNative: Leave open. */
    bool                fLeaveOpen;
    /** Whether the pipe is in blocking or non-blocking mode. */
    bool                fBlocking;
    /** Set if the pipe is broken. */
    bool                fBrokenPipe;
    /** Usage counter. */
    uint32_t            cUsers;

    /** The event semaphore associated with the pipe. */
    HEV                 hev;
    /** The handle of the poll set currently polling on this pipe.
     *  We can only have one poller at the time (lazy bird). */
    RTPOLLSET           hPollSet;
    /** Critical section protecting the above members.
     * (Taking the lazy/simple approach.) */
    RTCRITSECT          CritSect;

} RTPIPEINTERNAL;


/**
 * Ensures that the pipe has a semaphore associated with it.
 *
 * @returns VBox status code.
 * @param   pThis               The pipe.
 */
static int rtPipeOs2EnsureSem(RTPIPEINTERNAL *pThis)
{
    if (pThis->hev != NULLHANDLE)
        return VINF_SUCCESS;

    HEV     hev;
    APIRET  orc = DosCreateEventSem(NULL, &hev, DC_SEM_SHARED, FALSE);
    if (orc == NO_ERROR)
    {
        orc = DosSetNPipeSem(pThis->hPipe, (HSEM)hev, 1);
        if (orc == NO_ERROR)
        {
            pThis->hev = hev;
            return VINF_SUCCESS;
        }

        DosCloseEventSem(hev);
    }
    return RTErrConvertFromOS2(orc);
}


RTDECL(int)  RTPipeCreate(PRTPIPE phPipeRead, PRTPIPE phPipeWrite, uint32_t fFlags)
{
    AssertPtrReturn(phPipeRead, VERR_INVALID_POINTER);
    AssertPtrReturn(phPipeWrite, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTPIPE_C_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Try create and connect a pipe pair.
     */
    APIRET  orc;
    HPIPE   hPipeR;
    HFILE   hPipeW;
    int     rc;
    for (;;)
    {
        static volatile uint32_t    g_iNextPipe = 0;
        char                        szName[128];
        RTStrPrintf(szName, sizeof(szName), "\\pipe\\iprt-pipe-%u-%u", RTProcSelf(), ASMAtomicIncU32(&g_iNextPipe));

        /*
         * Create the read end of the pipe.
         */
        ULONG fPipeMode = 1 /*instance*/ | NP_TYPE_BYTE | NP_READMODE_BYTE | NP_NOWAIT;
        ULONG fOpenMode = NP_ACCESS_DUPLEX | NP_WRITEBEHIND;
        if (fFlags & RTPIPE_C_INHERIT_READ)
            fOpenMode |= NP_INHERIT;
        else
            fOpenMode |= NP_NOINHERIT;
        orc = DosCreateNPipe((PSZ)szName, &hPipeR, fOpenMode, fPipeMode, RTPIPE_OS2_SIZE, RTPIPE_OS2_SIZE, NP_DEFAULT_WAIT);
        if (orc == NO_ERROR)
        {
            orc = DosConnectNPipe(hPipeR);
            if (orc == ERROR_PIPE_NOT_CONNECTED || orc == NO_ERROR)
            {
                /*
                 * Connect to the pipe (the write end), attach sem below.
                 */
                ULONG ulAction = 0;
                ULONG fOpenW   = OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS;
                ULONG fModeW   = OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYNONE | OPEN_FLAGS_FAIL_ON_ERROR;
                if (!(fFlags & RTPIPE_C_INHERIT_WRITE))
                    fModeW |= OPEN_FLAGS_NOINHERIT;
                orc = DosOpen((PSZ)szName, &hPipeW, &ulAction, 0 /*cbFile*/, FILE_NORMAL,
                              fOpenW, fModeW, NULL /*peaop2*/);
                if (orc == NO_ERROR)
                    break;
            }
            DosClose(hPipeR);
        }
        if (   orc != ERROR_PIPE_BUSY     /* already exist - compatible */
            && orc != ERROR_ACCESS_DENIED /* already exist - incompatible (?) */)
            return RTErrConvertFromOS2(orc);
        /* else: try again with a new name */
    }

    /*
     * Create the two handles.
     */
    RTPIPEINTERNAL *pThisR = (RTPIPEINTERNAL *)RTMemAllocZ(sizeof(RTPIPEINTERNAL));
    if (pThisR)
    {
        RTPIPEINTERNAL *pThisW = (RTPIPEINTERNAL *)RTMemAllocZ(sizeof(RTPIPEINTERNAL));
        if (pThisW)
        {
            /* Crit sects. */
            rc = RTCritSectInit(&pThisR->CritSect);
            if (RT_SUCCESS(rc))
            {
                rc = RTCritSectInit(&pThisW->CritSect);
                if (RT_SUCCESS(rc))
                {
                    /* Initialize the structures. */
                    pThisR->u32Magic        = RTPIPE_MAGIC;
                    pThisW->u32Magic        = RTPIPE_MAGIC;
                    pThisR->hPipe           = hPipeR;
                    pThisW->hPipe           = hPipeW;
                    pThisR->hev             = NULLHANDLE;
                    pThisW->hev             = NULLHANDLE;
                    pThisR->fRead           = true;
                    pThisW->fRead           = false;
                    pThisR->fLeaveOpen      = false;
                    pThisW->fLeaveOpen      = false;
                    pThisR->fBlocking       = false;
                    pThisW->fBlocking       = true;
                    //pThisR->fBrokenPipe     = false;
                    //pThisW->fBrokenPipe     = false;
                    //pThisR->cUsers          = 0;
                    //pThisW->cUsers          = 0;
                    pThisR->hPollSet        = NIL_RTPOLLSET;
                    pThisW->hPollSet        = NIL_RTPOLLSET;

                    *phPipeRead  = pThisR;
                    *phPipeWrite = pThisW;
                    return VINF_SUCCESS;
                }

                RTCritSectDelete(&pThisR->CritSect);
            }
            RTMemFree(pThisW);
        }
        else
            rc = VERR_NO_MEMORY;
        RTMemFree(pThisR);
    }
    else
        rc = VERR_NO_MEMORY;

    /* Don't call DosDisConnectNPipe! */
    DosClose(hPipeW);
    DosClose(hPipeR);
    return rc;
}


RTDECL(int)  RTPipeCloseEx(RTPIPE hPipe, bool fLeaveOpen)
{
    RTPIPEINTERNAL *pThis = hPipe;
    if (pThis == NIL_RTPIPE)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Do the cleanup.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, ~RTPIPE_MAGIC, RTPIPE_MAGIC), VERR_INVALID_HANDLE);
    RTCritSectEnter(&pThis->CritSect);
    Assert(pThis->cUsers == 0);

    /* Don't call DosDisConnectNPipe! */
    if (!fLeaveOpen && !pThis->fLeaveOpen)
        DosClose(pThis->hPipe);
    pThis->hPipe = (HPIPE)-1;

    if (pThis->hev != NULLHANDLE)
    {
        DosCloseEventSem(pThis->hev);
        pThis->hev = NULLHANDLE;
    }

    RTCritSectLeave(&pThis->CritSect);
    RTCritSectDelete(&pThis->CritSect);

    RTMemFree(pThis);

    return VINF_SUCCESS;
}


RTDECL(int)  RTPipeClose(RTPIPE hPipe)
{
    return RTPipeCloseEx(hPipe, false /*fLeaveOpen*/);
}


RTDECL(int)  RTPipeFromNative(PRTPIPE phPipe, RTHCINTPTR hNativePipe, uint32_t fFlags)
{
    AssertPtrReturn(phPipe, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTPIPE_N_VALID_MASK_FN), VERR_INVALID_PARAMETER);
    AssertReturn(!!(fFlags & RTPIPE_N_READ) != !!(fFlags & RTPIPE_N_WRITE), VERR_INVALID_PARAMETER);

    /*
     * Get and validate the pipe handle info.
     */
    HPIPE hNative = (HPIPE)hNativePipe;
    ULONG ulType  = 0;
    ULONG ulAttr  = 0;
    APIRET orc = DosQueryHType(hNative, &ulType, &ulAttr);
    AssertMsgReturn(orc == NO_ERROR, ("%d\n", orc), RTErrConvertFromOS2(orc));
    AssertReturn((ulType & 0x7) == HANDTYPE_PIPE, VERR_INVALID_HANDLE);

#if 0
    union
    {
        PIPEINFO    PipeInfo;
        uint8_t     abPadding[sizeof(PIPEINFO) + 127];
    } Buf;
    orc = DosQueryNPipeInfo(hNative, 1, &Buf, sizeof(Buf));
    if (orc != NO_ERROR)
    {
        /* Sorry, anonymous pips are not supported. */
        AssertMsgFailed(("%d\n", orc));
        return VERR_INVALID_HANDLE;
    }
    AssertReturn(Buf.PipeInfo.cbMaxInst == 1, VERR_INVALID_HANDLE);
#endif

    ULONG fPipeState = 0;
    orc = DosQueryNPHState(hNative, &fPipeState);
    if (orc != NO_ERROR)
    {
        /* Sorry, anonymous pips are not supported. */
        AssertMsgFailed(("%d\n", orc));
        return VERR_INVALID_HANDLE;
    }
    AssertReturn(!(fPipeState & NP_TYPE_MESSAGE), VERR_INVALID_HANDLE);
    AssertReturn(!(fPipeState & NP_READMODE_MESSAGE), VERR_INVALID_HANDLE);
    AssertReturn((fPipeState & 0xff) == 1, VERR_INVALID_HANDLE);

    ULONG fFileState = 0;
    orc = DosQueryFHState(hNative, &fFileState);
    AssertMsgReturn(orc == NO_ERROR, ("%d\n", orc), VERR_INVALID_HANDLE);
    AssertMsgReturn(   (fFileState & 0x3) == (fFlags & RTPIPE_N_READ ? OPEN_ACCESS_READONLY : OPEN_ACCESS_WRITEONLY)
                    || (fFileState & 0x3) == OPEN_ACCESS_READWRITE
                    , ("%#x\n", fFileState), VERR_INVALID_HANDLE);

    /*
     * Looks kind of OK. Fix the inherit flag.
     */
    orc = DosSetFHState(hNative,   (fFileState & (OPEN_FLAGS_WRITE_THROUGH | OPEN_FLAGS_FAIL_ON_ERROR | OPEN_FLAGS_NO_CACHE))
                                 | (fFlags & RTPIPE_N_INHERIT ? 0 : OPEN_FLAGS_NOINHERIT));
    AssertMsgReturn(orc == NO_ERROR, ("%d\n", orc), RTErrConvertFromOS2(orc));


    /*
     * Create a handle so we can try rtPipeQueryInfo on it
     * and see if we need to duplicate it to make that call work.
     */
    RTPIPEINTERNAL *pThis = (RTPIPEINTERNAL *)RTMemAllocZ(sizeof(RTPIPEINTERNAL));
    if (!pThis)
        return VERR_NO_MEMORY;
    int rc = RTCritSectInit(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        pThis->u32Magic        = RTPIPE_MAGIC;
        pThis->hPipe           = hNative;
        pThis->hev             = NULLHANDLE;
        pThis->fRead           = RT_BOOL(fFlags & RTPIPE_N_READ);
        pThis->fLeaveOpen      = RT_BOOL(fFlags & RTPIPE_N_LEAVE_OPEN);
        pThis->fBlocking       = !(fPipeState & NP_NOWAIT);
        //pThis->fBrokenPipe     = false;
        //pThis->cUsers          = 0;
        pThis->hPollSet        = NIL_RTPOLLSET;

        *phPipe = pThis;
        return VINF_SUCCESS;

        //RTCritSectDelete(&pThis->CritSect);
    }
    RTMemFree(pThis);
    return rc;
}

RTDECL(RTHCINTPTR) RTPipeToNative(RTPIPE hPipe)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, -1);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, -1);

    return (RTHCINTPTR)pThis->hPipe;
}

/**
 * Prepare blocking mode.
 *
 * @returns IPRT status code.
 * @retval  VERR_WRONG_ORDER if simultaneous non-blocking and blocking access is
 *          attempted.
 *
 * @param   pThis               The pipe handle.
 *
 * @remarks Caller owns the critical section.
 */
static int rtPipeTryBlocking(RTPIPEINTERNAL *pThis)
{
    if (!pThis->fBlocking)
    {
        if (pThis->cUsers != 0)
            return VERR_WRONG_ORDER;

        APIRET orc = DosSetNPHState(pThis->hPipe, NP_WAIT | NP_READMODE_BYTE);
        if (orc != NO_ERROR)
        {
            if (orc != ERROR_BROKEN_PIPE && orc != ERROR_PIPE_NOT_CONNECTED)
                return RTErrConvertFromOS2(orc);
            pThis->fBrokenPipe = true;
        }
        pThis->fBlocking = true;
    }

    pThis->cUsers++;
    return VINF_SUCCESS;
}


/**
 * Prepare non-blocking mode.
 *
 * @returns IPRT status code.
 * @retval  VERR_WRONG_ORDER if simultaneous non-blocking and blocking access is
 *          attempted.
 *
 * @param   pThis               The pipe handle.
 */
static int rtPipeTryNonBlocking(RTPIPEINTERNAL *pThis)
{
    if (pThis->fBlocking)
    {
        if (pThis->cUsers != 0)
            return VERR_WRONG_ORDER;

        APIRET orc = DosSetNPHState(pThis->hPipe, NP_NOWAIT | NP_READMODE_BYTE);
        if (orc != NO_ERROR)
        {
            if (orc != ERROR_BROKEN_PIPE && orc != ERROR_PIPE_NOT_CONNECTED)
                return RTErrConvertFromOS2(orc);
            pThis->fBrokenPipe = true;
        }
        pThis->fBlocking = false;
    }

    pThis->cUsers++;
    return VINF_SUCCESS;
}


/**
 * Checks if the read pipe has been broken.
 *
 * @returns true if broken, false if no.
 * @param   pThis               The pipe handle (read).
 */
static bool rtPipeOs2IsBroken(RTPIPEINTERNAL *pThis)
{
    Assert(pThis->fRead);

#if 0
    /*
     * Query it via the semaphore. Not sure how fast this is...
     */
    PIPESEMSTATE aStates[3]; RT_ZERO(aStates);
    APIRET orc = DosQueryNPipeSemState(pThis->hev, &aStates[0], sizeof(aStates));
    if (orc == NO_ERROR)
    {
        if (aStates[0].fStatus == NPSS_CLOSE)
            return true;
        if (aStates[0].fStatus == NPSS_RDATA)
            return false;
    }
    AssertMsgFailed(("%d / %d\n", orc, aStates[0].fStatus));

    /*
     * Fall back / alternative method.
     */
#endif
    ULONG       cbActual = 0;
    ULONG       ulState  = 0;
    AVAILDATA   Avail    = { 0, 0 };
    APIRET orc = DosPeekNPipe(pThis->hPipe, NULL, 0, &cbActual, &Avail, &ulState);
    if (orc != NO_ERROR)
    {
        if (orc != ERROR_PIPE_BUSY)
            AssertMsgFailed(("%d\n", orc));
        return false;
    }

    return ulState != NP_STATE_CONNECTED;
}


RTDECL(int) RTPipeRead(RTPIPE hPipe, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fRead, VERR_ACCESS_DENIED);
    AssertPtr(pcbRead);
    AssertPtr(pvBuf);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = rtPipeTryNonBlocking(pThis);
        if (RT_SUCCESS(rc))
        {
            RTCritSectLeave(&pThis->CritSect);

            ULONG cbActual = 0;
            APIRET orc = DosRead(pThis->hPipe, pvBuf, cbToRead, &cbActual);
            if (orc == NO_ERROR)
            {
                if (cbActual || !cbToRead || !rtPipeOs2IsBroken(pThis))
                    *pcbRead = cbActual;
                else
                    rc = VERR_BROKEN_PIPE;
            }
            else if (orc == ERROR_NO_DATA)
            {
                *pcbRead = 0;
                rc = VINF_TRY_AGAIN;
            }
            else
                rc = RTErrConvertFromOS2(orc);

            RTCritSectEnter(&pThis->CritSect);
            if (rc == VERR_BROKEN_PIPE)
                pThis->fBrokenPipe = true;
            pThis->cUsers--;
        }
        else
            rc = VERR_WRONG_ORDER;
        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;
}


RTDECL(int) RTPipeReadBlocking(RTPIPE hPipe, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fRead, VERR_ACCESS_DENIED);
    AssertPtr(pvBuf);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = rtPipeTryBlocking(pThis);
        if (RT_SUCCESS(rc))
        {
            RTCritSectLeave(&pThis->CritSect);

            size_t cbTotalRead = 0;
            while (cbToRead > 0)
            {
                ULONG  cbActual = 0;
                APIRET orc = DosRead(pThis->hPipe, pvBuf, cbToRead, &cbActual);
                if (orc != NO_ERROR)
                {
                    rc = RTErrConvertFromOS2(orc);
                    break;
                }
                if (!cbActual && rtPipeOs2IsBroken(pThis))
                {
                    rc = VERR_BROKEN_PIPE;
                    break;
                }

                /* advance */
                pvBuf        = (char *)pvBuf + cbActual;
                cbTotalRead += cbActual;
                cbToRead    -= cbActual;
            }

            if (pcbRead)
            {
                *pcbRead = cbTotalRead;
                if (   RT_FAILURE(rc)
                    && cbTotalRead)
                    rc = VINF_SUCCESS;
            }

            RTCritSectEnter(&pThis->CritSect);
            if (rc == VERR_BROKEN_PIPE)
                pThis->fBrokenPipe = true;
            pThis->cUsers--;
        }
        else
            rc = VERR_WRONG_ORDER;
        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;
}


/**
 * Gets the available write buffer size of the pipe.
 *
 * @returns Number of bytes, 1 on failure.
 * @param   pThis               The pipe handle.
 */
static ULONG rtPipeOs2GetSpace(RTPIPEINTERNAL *pThis)
{
    Assert(!pThis->fRead);

#if 0 /* Not sure which is more efficient, neither are really optimal, I fear. */
    /*
     * Query via semaphore state.
     * This will walk the list of active named pipes...
     */
    /** @todo Check how hev and hpipe are associated, if complicated, use the
     *        alternative method below. */
    PIPESEMSTATE aStates[3]; RT_ZERO(aStates);
    APIRET orc = DosQueryNPipeSemState((HSEM)pThis->hev, &aStates[0], sizeof(aStates));
    if (orc == NO_ERROR)
    {
        if (aStates[0].fStatus == NPSS_WSPACE)
            return aStates[0].usAvail;
        if (aStates[1].fStatus == NPSS_WSPACE)
            return aStates[1].usAvail;
        return 0;
    }
    AssertMsgFailed(("%d / %d\n", orc, aStates[0].fStatus));

#else
    /*
     * Query via the pipe info.
     * This will have to lookup and store the pipe name.
     */
    union
    {
        PIPEINFO    PipeInfo;
        uint8_t     abPadding[sizeof(PIPEINFO) + 127];
    } Buf;
    APIRET orc = DosQueryNPipeInfo(pThis->hPipe, 1, &Buf, sizeof(Buf));
    if (orc == NO_ERROR)
        return Buf.PipeInfo.cbOut;
    AssertMsgFailed(("%d\n", orc));
#endif

    return 1;
}


RTDECL(int) RTPipeWrite(RTPIPE hPipe, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fRead, VERR_ACCESS_DENIED);
    AssertPtr(pcbWritten);
    AssertPtr(pvBuf);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = rtPipeTryNonBlocking(pThis);
        if (RT_SUCCESS(rc))
        {
            if (cbToWrite > 0)
            {
                ULONG  cbActual = 0;
                APIRET orc = DosWrite(pThis->hPipe, pvBuf, cbToWrite, &cbActual);
                if (orc == NO_ERROR && cbActual == 0)
                {
                    /* Retry with the request adjusted to the available buffer space. */
                    ULONG cbAvail = rtPipeOs2GetSpace(pThis);
                    orc = DosWrite(pThis->hPipe, pvBuf, RT_MIN(cbAvail, cbToWrite), &cbActual);
                }

                if (orc == NO_ERROR)
                {
                    *pcbWritten = cbActual;
                    if (cbActual == 0)
                        rc = VINF_TRY_AGAIN;
                }
                else
                {
                    rc = RTErrConvertFromOS2(orc);
                    if (rc == VERR_PIPE_NOT_CONNECTED)
                        rc = VERR_BROKEN_PIPE;
                }
            }
            else
                *pcbWritten = 0;

            if (rc == VERR_BROKEN_PIPE)
                pThis->fBrokenPipe = true;
            pThis->cUsers--;
        }
        else
            rc = VERR_WRONG_ORDER;
        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;
}


RTDECL(int) RTPipeWriteBlocking(RTPIPE hPipe, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fRead, VERR_ACCESS_DENIED);
    AssertPtr(pvBuf);
    AssertPtrNull(pcbWritten);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = rtPipeTryBlocking(pThis);
        if (RT_SUCCESS(rc))
        {
            RTCritSectLeave(&pThis->CritSect);

            size_t cbTotalWritten = 0;
            while (cbToWrite > 0)
            {
                ULONG cbActual = 0;
                APIRET orc = DosWrite(pThis->hPipe, pvBuf, cbToWrite, &cbActual);
                if (orc != NO_ERROR)
                {
                    rc = RTErrConvertFromOS2(orc);
                    if (rc == VERR_PIPE_NOT_CONNECTED)
                        rc = VERR_BROKEN_PIPE;
                    break;
                }
                pvBuf           = (char const *)pvBuf + cbActual;
                cbToWrite      -= cbActual;
                cbTotalWritten += cbActual;
            }

            if (pcbWritten)
            {
                *pcbWritten = cbTotalWritten;
                if (   RT_FAILURE(rc)
                    && cbTotalWritten)
                    rc = VINF_SUCCESS;
            }

            RTCritSectEnter(&pThis->CritSect);
            if (rc == VERR_BROKEN_PIPE)
                pThis->fBrokenPipe = true;
            pThis->cUsers--;
        }
        else
            rc = VERR_WRONG_ORDER;
        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;
}


RTDECL(int) RTPipeFlush(RTPIPE hPipe)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fRead, VERR_ACCESS_DENIED);

    APIRET orc = DosResetBuffer(pThis->hPipe);
    if (orc != NO_ERROR)
    {
        int rc = RTErrConvertFromOS2(orc);
        if (rc == VERR_BROKEN_PIPE)
        {
            RTCritSectEnter(&pThis->CritSect);
            pThis->fBrokenPipe = true;
            RTCritSectLeave(&pThis->CritSect);
        }
        return rc;
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTPipeSelectOne(RTPIPE hPipe, RTMSINTERVAL cMillies)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);

    uint64_t const StartMsTS = RTTimeMilliTS();

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    rc = rtPipeOs2EnsureSem(pThis);
    if (RT_SUCCESS(rc) && cMillies > 0)
    {
        /* Stop polling attempts if we might block. */
        if (pThis->hPollSet == NIL_RTPOLLSET)
            pThis->hPollSet = (RTPOLLSET)(uintptr_t)0xbeef0042;
        else
            rc = VERR_WRONG_ORDER;
    }
    if (RT_SUCCESS(rc))
    {
        for (unsigned iLoop = 0;; iLoop++)
        {
            /*
             * Check the handle state.
             */
            APIRET orc;
            if (cMillies > 0)
            {
                ULONG ulIgnore;
                orc = DosResetEventSem(pThis->hev, &ulIgnore);
                AssertMsg(orc == NO_ERROR || orc == ERROR_ALREADY_RESET, ("%d\n", orc));
            }

            PIPESEMSTATE aStates[4]; RT_ZERO(aStates);
            orc = DosQueryNPipeSemState((HSEM)pThis->hev, &aStates[0], sizeof(aStates));
            if (orc != NO_ERROR)
            {
                rc = RTErrConvertFromOS2(orc);
                break;
            }
            int i = 0;
            if (pThis->fRead)
                while (aStates[i].fStatus == NPSS_WSPACE)
                    i++;
            else
                while (aStates[i].fStatus == NPSS_RDATA)
                    i++;
            if (aStates[i].fStatus == NPSS_CLOSE)
                break;
            Assert(aStates[i].fStatus == NPSS_WSPACE || aStates[i].fStatus == NPSS_RDATA || aStates[i].fStatus == NPSS_EOI);
            if (   aStates[i].fStatus != NPSS_EOI
                && aStates[i].usAvail > 0)
                break;

            /*
             * Check for timeout.
             */
            ULONG cMsMaxWait = SEM_INDEFINITE_WAIT;
            if (cMillies != RT_INDEFINITE_WAIT)
            {
                uint64_t cElapsed = RTTimeMilliTS() - StartMsTS;
                if (cElapsed >= cMillies)
                {
                    rc = VERR_TIMEOUT;
                    break;
                }
                cMsMaxWait = cMillies - (uint32_t)cElapsed;
            }

            /*
             * Wait.
             */
            RTCritSectLeave(&pThis->CritSect);
            orc = DosWaitEventSem(pThis->hev, cMsMaxWait);
            RTCritSectEnter(&pThis->CritSect);
            if (orc != NO_ERROR && orc != ERROR_TIMEOUT && orc != ERROR_SEM_TIMEOUT )
            {
                rc = RTErrConvertFromOS2(orc);
                break;
            }
        }

        if (rc == VERR_BROKEN_PIPE)
            pThis->fBrokenPipe = true;
        if (cMillies > 0)
            pThis->hPollSet = NIL_RTPOLLSET;
    }

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


RTDECL(int) RTPipeQueryReadable(RTPIPE hPipe, size_t *pcbReadable)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fRead, VERR_PIPE_NOT_READ);
    AssertPtrReturn(pcbReadable, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    ULONG       cbActual = 0;
    ULONG       ulState  = 0;
    AVAILDATA   Avail    = { 0, 0 };
    APIRET orc = DosPeekNPipe(pThis->hPipe, NULL, 0, &cbActual, &Avail, &ulState);
    if (orc == NO_ERROR)
    {
        if (Avail.cbpipe > 0 || ulState == NP_STATE_CONNECTED)
            *pcbReadable = Avail.cbpipe;
        else
            rc = VERR_PIPE_NOT_CONNECTED; /*??*/
    }
    else
        rc = RTErrConvertFromOS2(orc);

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


RTDECL(int) RTPipeQueryInfo(RTPIPE hPipe, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, 0);

    int rc = RTCritSectEnter(&pThis->CritSect);
    AssertRCReturn(rc, 0);

    rtPipeFakeQueryInfo(pObjInfo, enmAddAttr, pThis->fRead);

    if (pThis->fRead)
    {
        ULONG       cbActual = 0;
        ULONG       ulState  = 0;
        AVAILDATA   Avail    = { 0, 0 };
        APIRET orc = DosPeekNPipe(pThis->hPipe, NULL, 0, &cbActual, &Avail, &ulState);
        if (orc == NO_ERROR && (Avail.cbpipe > 0 || ulState == NP_STATE_CONNECTED))
            pObjInfo->cbObject = Avail.cbpipe;
    }
    else
        pObjInfo->cbObject = rtPipeOs2GetSpace(pThis);
    pObjInfo->cbAllocated = RTPIPE_OS2_SIZE; /** @todo this isn't necessarily true if we didn't create it... but, whatever */

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


int rtPipePollGetHandle(RTPIPE hPipe, uint32_t fEvents, PRTHCINTPTR phNative)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);

    AssertReturn(!(fEvents & RTPOLL_EVT_READ)  || pThis->fRead,  VERR_INVALID_PARAMETER);
    AssertReturn(!(fEvents & RTPOLL_EVT_WRITE) || !pThis->fRead, VERR_INVALID_PARAMETER);

    int rc = RTCritSectEnter(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = rtPipeOs2EnsureSem(pThis);
        if (RT_SUCCESS(rc))
            *phNative = (RTHCINTPTR)pThis->hev;
        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;
}


/**
 * Checks for pending events.
 *
 * @returns Event mask or 0.
 * @param   pThis               The pipe handle.
 * @param   fEvents             The desired events.
 * @param   fResetEvtSem        Whether to reset the event semaphore.
 */
static uint32_t rtPipePollCheck(RTPIPEINTERNAL *pThis, uint32_t fEvents, bool fResetEvtSem)
{
    /*
     * Reset the event semaphore if we're gonna wait.
     */
    APIRET orc;
    ULONG ulIgnore;
    if (fResetEvtSem)
    {
        orc = DosResetEventSem(pThis->hev, &ulIgnore);
        AssertMsg(orc == NO_ERROR || orc == ERROR_ALREADY_RESET, ("%d\n", orc));
    }

    /*
     * Check for events.
     */
    uint32_t fRetEvents = 0;
    if (pThis->fBrokenPipe)
        fRetEvents |= RTPOLL_EVT_ERROR;
    else if (pThis->fRead)
    {
        ULONG       cbActual = 0;
        ULONG       ulState  = 0;
        AVAILDATA   Avail    = { 0, 0 };
        orc = DosPeekNPipe(pThis->hPipe, NULL, 0, &cbActual, &Avail, &ulState);
        if (orc != NO_ERROR)
        {
            fRetEvents |= RTPOLL_EVT_ERROR;
            if (orc == ERROR_BROKEN_PIPE || orc == ERROR_PIPE_NOT_CONNECTED)
                pThis->fBrokenPipe = true;
        }
        else if (Avail.cbpipe > 0)
            fRetEvents |= RTPOLL_EVT_READ;
        else if (ulState != NP_STATE_CONNECTED)
        {
            fRetEvents |= RTPOLL_EVT_ERROR;
            pThis->fBrokenPipe = true;
        }
    }
    else
    {
        PIPESEMSTATE aStates[4]; RT_ZERO(aStates);
        orc = DosQueryNPipeSemState((HSEM)pThis->hev, &aStates[0], sizeof(aStates));
        if (orc == NO_ERROR)
        {
            int i = 0;
            while (aStates[i].fStatus == NPSS_RDATA)
                i++;
            if (aStates[i].fStatus == NPSS_CLOSE)
            {
                fRetEvents |= RTPOLL_EVT_ERROR;
                pThis->fBrokenPipe = true;
            }
            else if (   aStates[i].fStatus == NPSS_WSPACE
                     && aStates[i].usAvail > 0)
                fRetEvents |= RTPOLL_EVT_WRITE;
        }
        else
        {
            fRetEvents |= RTPOLL_EVT_ERROR;
            if (orc == ERROR_BROKEN_PIPE || orc == ERROR_PIPE_NOT_CONNECTED)
                pThis->fBrokenPipe = true;
        }
    }

    return fRetEvents & (fEvents | RTPOLL_EVT_ERROR);
}


uint32_t rtPipePollStart(RTPIPE hPipe, RTPOLLSET hPollSet, uint32_t fEvents, bool fFinalEntry, bool fNoWait)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, UINT32_MAX);

    int rc = RTCritSectEnter(&pThis->CritSect);
    AssertRCReturn(rc, UINT32_MAX);

    /* Check that this is the only current use of this pipe. */
    uint32_t fRetEvents;
    if (   pThis->cUsers   == 0
        || pThis->hPollSet == NIL_RTPOLLSET)
    {
        fRetEvents = rtPipePollCheck(pThis, fEvents, fNoWait);
        if (!fRetEvents && !fNoWait)
        {
            /* Mark the set busy while waiting. */
            pThis->cUsers++;
            pThis->hPollSet = hPollSet;
        }
    }
    else
    {
        AssertFailed();
        fRetEvents = UINT32_MAX;
    }

    RTCritSectLeave(&pThis->CritSect);
    return fRetEvents;
}


uint32_t rtPipePollDone(RTPIPE hPipe, uint32_t fEvents, bool fFinalEntry, bool fHarvestEvents)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, 0);

    int rc = RTCritSectEnter(&pThis->CritSect);
    AssertRCReturn(rc, 0);

    Assert(pThis->cUsers > 0);

    /* harvest events. */
    uint32_t fRetEvents = rtPipePollCheck(pThis, fEvents, false);

    /* update counters. */
    pThis->cUsers--;
    pThis->hPollSet = NIL_RTPOLLSET;

    RTCritSectLeave(&pThis->CritSect);
    return fRetEvents;
}
