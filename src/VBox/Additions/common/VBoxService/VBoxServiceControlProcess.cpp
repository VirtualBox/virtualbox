/* $Id: VBoxServiceControlProcess.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxServiceControlThread - Guest process handling.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/handle.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/poll.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/thread.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/HostServices/GuestControlSvc.h>

#include "VBoxServiceInternal.h"
#include "VBoxServiceControl.h"
#include "VBoxServiceToolBox.h"

using namespace guestControl;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int                  vgsvcGstCtrlProcessAssignPID(PVBOXSERVICECTRLPROCESS pThread, uint32_t uPID);
static int                  vgsvcGstCtrlProcessLock(PVBOXSERVICECTRLPROCESS pProcess);
static int                  vgsvcGstCtrlProcessSetupPipe(const char *pszHowTo, int fd, PRTHANDLE ph, PRTHANDLE *pph,
                                                         PRTPIPE phPipe);
static int                  vgsvcGstCtrlProcessUnlock(PVBOXSERVICECTRLPROCESS pProcess);
/* Request handlers. */
static DECLCALLBACK(int)    vgsvcGstCtrlProcessOnInput(PVBOXSERVICECTRLPROCESS pThis, const PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                                       bool fPendingClose, void *pvBuf, uint32_t cbBuf);
static DECLCALLBACK(int)    vgsvcGstCtrlProcessOnOutput(PVBOXSERVICECTRLPROCESS pThis, const PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                                        uint32_t uHandle, uint32_t cbToRead, uint32_t uFlags);
static int                  vgsvcGstCtrlProcessRequestExV(PVBOXSERVICECTRLPROCESS pProcess, bool fAsync, RTMSINTERVAL cMsTimeout,
                                                          PFNRT pfnFunction, unsigned cArgs,
                                                          va_list Args) RT_IPRT_CALLREQ_ATTR(4, 5, 0); /* attrib req prototype */
static int                  vgsvcGstCtrlProcessRequestAsync(PVBOXSERVICECTRLPROCESS pProcess, PFNRT pfnFunction,
                                                            unsigned cArgs, ...) RT_IPRT_CALLREQ_ATTR(2, 3, 4); /* ditto */
#if 0 /* unused */
static int                  vgsvcGstCtrlProcessRequestWait(PVBOXSERVICECTRLPROCESS pProcess, RTMSINTERVAL cMsTimeout,
                                                           PFNRT pfnFunction, unsigned cArgs, ...) RT_IPRT_CALLREQ_ATTR(3, 4, 5);
#endif



/**
 * Initialies the passed in thread data structure with the parameters given.
 *
 * @return  IPRT status code.
 * @param   pProcess                    Process to initialize.
 * @param   pSession                    Guest session the process is bound to.
 * @param   pStartupInfo                Startup information.
 * @param   u32ContextID                The context ID bound to this request / command.
 */
static int vgsvcGstCtrlProcessInit(PVBOXSERVICECTRLPROCESS pProcess,
                                   const PVBOXSERVICECTRLSESSION pSession,
                                   const PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo,
                                   uint32_t u32ContextID)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pStartupInfo, VERR_INVALID_POINTER);

    /* General stuff. */
    pProcess->hProcess   = NIL_RTPROCESS;
    pProcess->pSession   = pSession;
    pProcess->Node.pPrev = NULL;
    pProcess->Node.pNext = NULL;

    pProcess->fShutdown  = false;
    pProcess->fStarted   = false;
    pProcess->fStopped   = false;

    pProcess->uPID       = 0; /* Don't have a PID yet. */
    pProcess->cRefs      = 0;
    /*
     * Use the initial context ID we got for starting
     * the process to report back its status with the
     * same context ID.
     */
    pProcess->uContextID = u32ContextID;
    /*
     * Note: pProcess->ClientID will be assigned when thread is started;
     * every guest process has its own client ID to detect crashes on
     * a per-guest-process level.
     */

    int rc = RTCritSectInit(&pProcess->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    pProcess->hPollSet = NIL_RTPOLLSET;
    pProcess->hPipeStdInW = NIL_RTPIPE;
    pProcess->hPipeStdOutR = NIL_RTPIPE;
    pProcess->hPipeStdErrR = NIL_RTPIPE;
    pProcess->hNotificationPipeW = NIL_RTPIPE;
    pProcess->hNotificationPipeR = NIL_RTPIPE;

    rc = RTReqQueueCreate(&pProcess->hReqQueue);
    AssertReleaseRC(rc);

    /* Duplicate startup info. */
    pProcess->pStartupInfo = VbglR3GuestCtrlProcStartupInfoDup(pStartupInfo);
    AssertPtrReturn(pProcess->pStartupInfo, VERR_NO_MEMORY);

    /* Adjust timeout value. */
    if (   pProcess->pStartupInfo->uTimeLimitMS == UINT32_MAX
        || pProcess->pStartupInfo->uTimeLimitMS == 0)
        pProcess->pStartupInfo->uTimeLimitMS = RT_INDEFINITE_WAIT;

    if (RT_FAILURE(rc)) /* Clean up on failure. */
        VGSvcGstCtrlProcessFree(pProcess);
    return rc;
}


/**
 * Frees a guest process. On success, pProcess will be
 * free'd and thus won't be available anymore.
 *
 * @return  IPRT status code.
 * @param   pProcess                Guest process to free.
 *                                  The pointer will not be valid anymore after return.
 */
int VGSvcGstCtrlProcessFree(PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pProcess->CritSect);
    if (RT_SUCCESS(rc))
    {
        VGSvcVerbose(3, "[PID %RU32]: Freeing (cRefs=%RU32)...\n", pProcess->uPID, pProcess->cRefs);

        AssertReturn(pProcess->cRefs == 0, VERR_WRONG_ORDER);
        AssertReturn(pProcess->fStopped, VERR_WRONG_ORDER);
        AssertReturn(pProcess->fShutdown, VERR_WRONG_ORDER);

        VbglR3GuestCtrlProcStartupInfoFree(pProcess->pStartupInfo);
        pProcess->pStartupInfo = NULL;

        /*
         * Destroy other thread data.
         */
        rc = RTPollSetDestroy(pProcess->hPollSet);
        AssertRC(rc);

        rc = RTReqQueueDestroy(pProcess->hReqQueue);
        AssertRC(rc);

        rc = RTPipeClose(pProcess->hNotificationPipeR);
        AssertRC(rc);
        rc = RTPipeClose(pProcess->hNotificationPipeW);
        AssertRC(rc);

        rc = RTPipeClose(pProcess->hPipeStdInW);
        AssertRC(rc);
        rc = RTPipeClose(pProcess->hPipeStdErrR);
        AssertRC(rc);
        rc = RTPipeClose(pProcess->hPipeStdOutR);
        AssertRC(rc);

        rc = RTCritSectLeave(&pProcess->CritSect);
        AssertRC(rc);

        RTCritSectDelete(&pProcess->CritSect);

        /*
         * Destroy thread structure as final step.
         */
        RTMemFree(pProcess);
        pProcess = NULL;
    }

    return rc;
}


/**
 * Signals a guest process thread that we want it to shut down in
 * a gentle way.
 *
 * @return  IPRT status code.
 * @param   pProcess            Process to stop.
 */
int VGSvcGstCtrlProcessStop(PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

    VGSvcVerbose(3, "[PID %RU32]: Stopping ...\n", pProcess->uPID);

    /* Do *not* set pThread->fShutdown or other stuff here!
     * The guest thread loop will clean up itself. */

    return VGSvcGstCtrlProcessHandleTerm(pProcess);
}


/**
 * Releases a previously acquired guest process (decreases the refcount).
 *
 * @param   pProcess            Process to release.
 */
void VGSvcGstCtrlProcessRelease(PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturnVoid(pProcess);

    int rc2 = RTCritSectEnter(&pProcess->CritSect);
    if (RT_SUCCESS(rc2))
    {
        AssertReturnVoid(pProcess->cRefs);
        pProcess->cRefs--;

        VGSvcVerbose(3, "[PID %RU32]: cRefs=%RU32, fShutdown=%RTbool, fStopped=%RTbool\n",
                     pProcess->uPID, pProcess->cRefs, pProcess->fShutdown, pProcess->fStopped);

        rc2 = RTCritSectLeave(&pProcess->CritSect);
        AssertRC(rc2);
    }
}


/**
 * Wait for a guest process thread to shut down.
 *
 * @return  IPRT status code.
 * @param   pProcess            Process to wait shutting down for.
 * @param   msTimeout           Timeout in ms to wait for shutdown.
 * @param   prc                 Where to store the thread's return code.
 *                              Optional.
 */
int VGSvcGstCtrlProcessWait(const PVBOXSERVICECTRLPROCESS pProcess, RTMSINTERVAL msTimeout, int *prc)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    AssertPtrNullReturn(prc, VERR_INVALID_POINTER);

    int rc = vgsvcGstCtrlProcessLock(pProcess);
    if (RT_SUCCESS(rc))
    {
        if (RTThreadGetState(pProcess->Thread) != RTTHREADSTATE_INVALID) /* Is there a thread we can wait for? */
        {
            VGSvcVerbose(2, "[PID %RU32]: Waiting for shutdown (%RU32ms) ...\n", pProcess->uPID, msTimeout);

            AssertMsgReturn(pProcess->fStarted,
                            ("Tried to wait on guest process=%p (PID %RU32) which has not been started yet\n",
                             pProcess, pProcess->uPID), VERR_INVALID_PARAMETER);

            /* Unlock process before waiting. */
            rc = vgsvcGstCtrlProcessUnlock(pProcess);
            AssertRC(rc);

            /* Do the actual waiting. */
            int rcThread;
            Assert(pProcess->Thread != NIL_RTTHREAD);
            rc = RTThreadWait(pProcess->Thread, msTimeout, &rcThread);

            int rc2 = vgsvcGstCtrlProcessLock(pProcess);
            AssertRC(rc2);

            if (RT_SUCCESS(rc))
            {
                pProcess->Thread = NIL_RTTHREAD;
                VGSvcVerbose(3, "[PID %RU32]: Thread shutdown complete, thread rc=%Rrc\n", pProcess->uPID, rcThread);
                if (prc)
                    *prc = rcThread;
            }
        }

        int rc2 = vgsvcGstCtrlProcessUnlock(pProcess);
        AssertRC(rc2);
    }

    if (RT_FAILURE(rc))
        VGSvcError("[PID %RU32]: Waiting for shutting down thread returned error rc=%Rrc\n", pProcess->uPID, rc);

    VGSvcVerbose(3, "[PID %RU32]: Waiting resulted in rc=%Rrc\n", pProcess->uPID, rc);
    return rc;
}


/**
 * Closes the stdin pipe of a guest process.
 *
 * @return  IPRT status code.
 * @param   pProcess            The process which input pipe we close.
 * @param   phStdInW            The standard input pipe handle.
 */
static int vgsvcGstCtrlProcessPollsetCloseInput(PVBOXSERVICECTRLPROCESS pProcess, PRTPIPE phStdInW)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    AssertPtrReturn(phStdInW, VERR_INVALID_POINTER);

    int rc = RTPollSetRemove(pProcess->hPollSet, VBOXSERVICECTRLPIPEID_STDIN);
    if (rc != VERR_POLL_HANDLE_ID_NOT_FOUND)
        AssertRC(rc);

    if (*phStdInW != NIL_RTPIPE)
    {
        rc = RTPipeClose(*phStdInW);
        AssertRC(rc);
        *phStdInW = NIL_RTPIPE;
    }

    return rc;
}


#ifdef DEBUG
/**
 * Names a poll handle ID.
 *
 * @returns Pointer to read-only string.
 * @param   idPollHnd           What to name.
 */
static const char *vgsvcGstCtrlProcessPollHandleToString(uint32_t idPollHnd)
{
    switch (idPollHnd)
    {
        case VBOXSERVICECTRLPIPEID_UNKNOWN:
            return "unknown";
        case VBOXSERVICECTRLPIPEID_STDIN:
            return "stdin";
        case VBOXSERVICECTRLPIPEID_STDIN_WRITABLE:
            return "stdin_writable";
        case VBOXSERVICECTRLPIPEID_STDOUT:
            return "stdout";
        case VBOXSERVICECTRLPIPEID_STDERR:
            return "stderr";
        case VBOXSERVICECTRLPIPEID_IPC_NOTIFY:
            return "ipc_notify";
        default:
            return "unknown";
    }
}
#endif /* DEBUG */


/**
 * Handle an error event on standard input.
 *
 * @return  IPRT status code.
 * @param   pProcess            Process to handle pollset for.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   phStdInW            The standard input pipe handle.
 */
static int vgsvcGstCtrlProcessPollsetOnInput(PVBOXSERVICECTRLPROCESS pProcess, uint32_t fPollEvt, PRTPIPE phStdInW)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

    NOREF(fPollEvt);

    return vgsvcGstCtrlProcessPollsetCloseInput(pProcess, phStdInW);
}


/**
 * Handle pending output data or error on standard out or standard error.
 *
 * @returns IPRT status code from client send.
 * @param   pProcess            Process to handle pollset for.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   phPipeR             The pipe handle.
 * @param   idPollHnd           The pipe ID to handle.
 */
static int vgsvcGstCtrlProcessHandleOutputError(PVBOXSERVICECTRLPROCESS pProcess,
                                                uint32_t fPollEvt, PRTPIPE phPipeR, uint32_t idPollHnd)
{
    RT_NOREF1(fPollEvt);
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

    if (!phPipeR)
        return VINF_SUCCESS;

#ifdef DEBUG
    VGSvcVerbose(4, "[PID %RU32]: Output error: idPollHnd=%s, fPollEvt=0x%x\n",
                 pProcess->uPID, vgsvcGstCtrlProcessPollHandleToString(idPollHnd), fPollEvt);
#endif

    /* Remove pipe from poll set. */
    int rc2 = RTPollSetRemove(pProcess->hPollSet, idPollHnd);
    AssertMsg(RT_SUCCESS(rc2) || rc2 == VERR_POLL_HANDLE_ID_NOT_FOUND, ("%Rrc\n", rc2));

    bool fClosePipe = true; /* By default close the pipe. */

    /* Check if there's remaining data to read from the pipe. */
    if (*phPipeR != NIL_RTPIPE)
    {
        size_t cbReadable;
        rc2 = RTPipeQueryReadable(*phPipeR, &cbReadable);
        if (   RT_SUCCESS(rc2)
            && cbReadable)
        {
#ifdef DEBUG
            VGSvcVerbose(3, "[PID %RU32]: idPollHnd=%s has %zu bytes left, vetoing close\n",
                         pProcess->uPID, vgsvcGstCtrlProcessPollHandleToString(idPollHnd), cbReadable);
#endif
            /* Veto closing the pipe yet because there's still stuff to read
             * from the pipe. This can happen on UNIX-y systems where on
             * error/hangup there still can be data to be read out. */
            fClosePipe = false;
        }
    }
#ifdef DEBUG
    else
        VGSvcVerbose(3, "[PID %RU32]: idPollHnd=%s will be closed\n",
                     pProcess->uPID, vgsvcGstCtrlProcessPollHandleToString(idPollHnd));
#endif

    if (   *phPipeR != NIL_RTPIPE
        && fClosePipe)
    {
        rc2 = RTPipeClose(*phPipeR);
        AssertRC(rc2);
        *phPipeR = NIL_RTPIPE;
    }

    return VINF_SUCCESS;
}


/**
 * Handle pending output data or error on standard out or standard error.
 *
 * @returns IPRT status code from client send.
 * @param   pProcess            Process to handle pollset for.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   phPipeR             The pipe handle.
 * @param   idPollHnd           The pipe ID to handle.
 *
 */
static int vgsvcGstCtrlProcessPollsetOnOutput(PVBOXSERVICECTRLPROCESS pProcess,
                                              uint32_t fPollEvt, PRTPIPE phPipeR, uint32_t idPollHnd)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

#ifdef DEBUG
    VGSvcVerbose(4, "[PID %RU32]: Output event phPipeR=%p, idPollHnd=%s, fPollEvt=0x%x\n",
                 pProcess->uPID, phPipeR, vgsvcGstCtrlProcessPollHandleToString(idPollHnd), fPollEvt);
#endif

    if (!phPipeR)
        return VINF_SUCCESS;

    int rc = VINF_SUCCESS;

#ifdef DEBUG
    if (*phPipeR != NIL_RTPIPE)
    {
        size_t cbReadable;
        rc = RTPipeQueryReadable(*phPipeR, &cbReadable);
        if (   RT_SUCCESS(rc)
            && cbReadable)
        {
            VGSvcVerbose(4, "[PID %RU32]: Output event cbReadable=%zu\n", pProcess->uPID, cbReadable);
        }
    }
#endif

#if 0
    /* Push output to the host. */
    if (fPollEvt & RTPOLL_EVT_READ)
    {
        size_t cbRead = 0;
        uint8_t byData[_64K];
        rc = RTPipeRead(*phPipeR, byData, sizeof(byData), &cbRead);
        VGSvcVerbose(4, "VGSvcGstCtrlProcessHandleOutputEvent cbRead=%u, rc=%Rrc\n", cbRead, rc);

        /* Make sure we go another poll round in case there was too much data
           for the buffer to hold. */
        fPollEvt &= RTPOLL_EVT_ERROR;
    }
#endif

    if (fPollEvt & RTPOLL_EVT_ERROR)
        rc = vgsvcGstCtrlProcessHandleOutputError(pProcess, fPollEvt, phPipeR, idPollHnd);
    return rc;
}


/**
 * Execution loop which runs in a dedicated per-started-process thread and
 * handles all pipe input/output and signalling stuff.
 *
 * @return  IPRT status code.
 * @param   pProcess                    The guest process to handle.
 */
static int vgsvcGstCtrlProcessProcLoop(PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

    int                         rc;
    int                         rc2;
    uint64_t const              uMsStart            = RTTimeMilliTS();
    RTPROCSTATUS                ProcessStatus       = { 254, RTPROCEXITREASON_ABEND };
    bool                        fProcessAlive       = true;
    bool                        fProcessTimedOut    = false;
    uint64_t                    MsProcessKilled     = UINT64_MAX;
    RTMSINTERVAL const          cMsPollBase         = pProcess->hPipeStdInW != NIL_RTPIPE
                                                      ? 100   /* Need to poll for input. */
                                                      : 1000; /* Need only poll for process exit and aborts. */
    RTMSINTERVAL                cMsPollCur          = 0;

    /*
     * Assign PID to thread data.
     * Also check if there already was a thread with the same PID and shut it down -- otherwise
     * the first (stale) entry will be found and we get really weird results!
     */
    rc = vgsvcGstCtrlProcessAssignPID(pProcess, pProcess->hProcess /* Opaque PID handle */);
    if (RT_FAILURE(rc))
    {
        VGSvcError("Unable to assign PID=%u, to new thread, rc=%Rrc\n", pProcess->hProcess, rc);
        return rc;
    }

    /*
     * Before entering the loop, tell the host that we've started the guest
     * and that it's now OK to send input to the process.
     */
    VGSvcVerbose(2, "[PID %RU32]: Process '%s' started, CID=%u, User=%s, cMsTimeout=%RU32\n",
                       pProcess->uPID, pProcess->pStartupInfo->pszCmd, pProcess->uContextID,
                       pProcess->pStartupInfo->pszUser, pProcess->pStartupInfo->uTimeLimitMS);
    VBGLR3GUESTCTRLCMDCTX ctxStart = { g_idControlSvcClient, pProcess->uContextID, 0 /* uProtocol */, 0 /* uNumParms */ };
    rc = VbglR3GuestCtrlProcCbStatus(&ctxStart,
                                     pProcess->uPID, PROC_STS_STARTED, 0 /* u32Flags */,
                                     NULL /* pvData */, 0 /* cbData */);
    if (rc == VERR_INTERRUPTED)
        rc = VINF_SUCCESS; /* SIGCHLD send by quick childs! */
    if (RT_FAILURE(rc))
        VGSvcError("[PID %RU32]: Error reporting starting status to host, rc=%Rrc\n", pProcess->uPID, rc);

    /*
     * Process input, output, the test pipe and client requests.
     */
    while (   RT_SUCCESS(rc)
           && RT_UNLIKELY(!pProcess->fShutdown))
    {
        /*
         * Wait/Process all pending events.
         */
        uint32_t idPollHnd;
        uint32_t fPollEvt;
        rc2 = RTPollNoResume(pProcess->hPollSet, cMsPollCur, &fPollEvt, &idPollHnd);
        if (pProcess->fShutdown)
            continue;

        cMsPollCur = 0; /* No rest until we've checked everything. */

        if (RT_SUCCESS(rc2))
        {
            switch (idPollHnd)
            {
                case VBOXSERVICECTRLPIPEID_STDIN:
                    rc = vgsvcGstCtrlProcessPollsetOnInput(pProcess, fPollEvt, &pProcess->hPipeStdInW);
                    break;

                case VBOXSERVICECTRLPIPEID_STDOUT:
                    rc = vgsvcGstCtrlProcessPollsetOnOutput(pProcess, fPollEvt, &pProcess->hPipeStdOutR, idPollHnd);
                    break;

                case VBOXSERVICECTRLPIPEID_STDERR:
                    rc = vgsvcGstCtrlProcessPollsetOnOutput(pProcess, fPollEvt, &pProcess->hPipeStdErrR, idPollHnd);
                    break;

                case VBOXSERVICECTRLPIPEID_IPC_NOTIFY:
#ifdef DEBUG_andy
                    VGSvcVerbose(4, "[PID %RU32]: IPC notify\n", pProcess->uPID);
#endif
                    rc2 = vgsvcGstCtrlProcessLock(pProcess);
                    if (RT_SUCCESS(rc2))
                    {
                        /* Drain the notification pipe. */
                        uint8_t abBuf[8];
                        size_t cbIgnore;
                        rc2 = RTPipeRead(pProcess->hNotificationPipeR, abBuf, sizeof(abBuf), &cbIgnore);
                        if (RT_FAILURE(rc2))
                            VGSvcError("Draining IPC notification pipe failed with rc=%Rrc\n", rc2);

                        /* Process all pending requests. */
                        VGSvcVerbose(4, "[PID %RU32]: Processing pending requests ...\n", pProcess->uPID);
                        Assert(pProcess->hReqQueue != NIL_RTREQQUEUE);
                        rc2 = RTReqQueueProcess(pProcess->hReqQueue,
                                                0 /* Only process all pending requests, don't wait for new ones */);
                        if (   RT_FAILURE(rc2)
                            && rc2 != VERR_TIMEOUT)
                            VGSvcError("Processing requests failed with with rc=%Rrc\n", rc2);

                        int rc3 = vgsvcGstCtrlProcessUnlock(pProcess);
                        AssertRC(rc3);
#ifdef DEBUG
                        VGSvcVerbose(4, "[PID %RU32]: Processing pending requests done, rc=%Rrc\n", pProcess->uPID, rc2);
#endif
                    }

                    break;

                default:
                    AssertMsgFailed(("Unknown idPollHnd=%RU32\n", idPollHnd));
                    break;
            }

            if (RT_FAILURE(rc) || rc == VINF_EOF)
                break; /* Abort command, or client dead or something. */
        }
#if 0
        VGSvcVerbose(4, "[PID %RU32]: Polling done, pollRc=%Rrc, pollCnt=%RU32, idPollHnd=%s, rc=%Rrc, fProcessAlive=%RTbool, fShutdown=%RTbool\n",
                     pProcess->uPID, rc2, RTPollSetGetCount(hPollSet), vgsvcGstCtrlProcessPollHandleToString(idPollHnd), rc, fProcessAlive, pProcess->fShutdown);
        VGSvcVerbose(4, "[PID %RU32]: stdOut=%s, stdErrR=%s\n",
                     pProcess->uPID,
                     *phStdOutR == NIL_RTPIPE ? "closed" : "open",
                     *phStdErrR == NIL_RTPIPE ? "closed" : "open");
#endif
        if (RT_UNLIKELY(pProcess->fShutdown))
            break; /* We were asked to shutdown. */

        /*
         * Check for process death.
         */
        if (fProcessAlive)
        {
            rc2 = RTProcWaitNoResume(pProcess->hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &ProcessStatus);
            if (RT_SUCCESS_NP(rc2))
            {
                fProcessAlive = false;
                /* Note: Don't bail out here yet. First check in the next block below
                 *       if all needed pipe outputs have been consumed. */
            }
            else
            {
                if (RT_UNLIKELY(rc2 == VERR_INTERRUPTED))
                    continue;
                if (RT_UNLIKELY(rc2 == VERR_PROCESS_NOT_FOUND))
                {
                    fProcessAlive = false;
                    ProcessStatus.enmReason = RTPROCEXITREASON_ABEND;
                    ProcessStatus.iStatus   = 255;
                    AssertFailed();
                }
                else
                    AssertMsg(rc2 == VERR_PROCESS_RUNNING, ("%Rrc\n", rc2));
            }
        }

        /*
         * If the process has terminated and all output has been consumed,
         * we should be heading out.
         */
        if (!fProcessAlive)
        {
            if (   fProcessTimedOut
                || (   pProcess->hPipeStdOutR == NIL_RTPIPE
                    && pProcess->hPipeStdErrR == NIL_RTPIPE)
               )
            {
                VGSvcVerbose(3, "[PID %RU32]: RTProcWaitNoResume=%Rrc\n", pProcess->uPID, rc2);
                break;
            }
        }

        /*
         * Check for timed out, killing the process.
         */
        uint32_t cMilliesLeft = RT_INDEFINITE_WAIT;
        if (   pProcess->pStartupInfo->uTimeLimitMS != RT_INDEFINITE_WAIT
            && pProcess->pStartupInfo->uTimeLimitMS != 0)
        {
            uint64_t u64Now = RTTimeMilliTS();
            uint64_t cMsElapsed = u64Now - uMsStart;
            if (cMsElapsed >= pProcess->pStartupInfo->uTimeLimitMS)
            {
                fProcessTimedOut = true;
                if (    MsProcessKilled == UINT64_MAX
                    ||  u64Now - MsProcessKilled > 1000)
                {
                    if (u64Now - MsProcessKilled > 20*60*1000)
                        break; /* Give up after 20 mins. */

                    VGSvcVerbose(3, "[PID %RU32]: Timed out (%RU64ms elapsed > %RU32ms timeout), killing ...\n",
                                 pProcess->uPID, cMsElapsed, pProcess->pStartupInfo->uTimeLimitMS);

                    rc2 = RTProcTerminate(pProcess->hProcess);
                    VGSvcVerbose(3, "[PID %RU32]: Killing process resulted in rc=%Rrc\n",
                                 pProcess->uPID, rc2);
                    MsProcessKilled = u64Now;
                    continue;
                }
                cMilliesLeft = 10000;
            }
            else
                cMilliesLeft = pProcess->pStartupInfo->uTimeLimitMS - (uint32_t)cMsElapsed;
        }

        /* Reset the polling interval since we've done all pending work. */
        cMsPollCur = fProcessAlive
                   ? cMsPollBase
                   : RT_MS_1MIN;
        if (cMilliesLeft < cMsPollCur)
            cMsPollCur = cMilliesLeft;
    }

    VGSvcVerbose(3, "[PID %RU32]: Loop ended: rc=%Rrc, fShutdown=%RTbool, fProcessAlive=%RTbool, fProcessTimedOut=%RTbool, MsProcessKilled=%RU64 (%RX64)\n",
                 pProcess->uPID, rc, pProcess->fShutdown, fProcessAlive, fProcessTimedOut, MsProcessKilled, MsProcessKilled);
    VGSvcVerbose(3, "[PID %RU32]: *phStdOutR=%s, *phStdErrR=%s\n",
                 pProcess->uPID,
                 pProcess->hPipeStdOutR == NIL_RTPIPE ? "closed" : "open",
                 pProcess->hPipeStdErrR == NIL_RTPIPE ? "closed" : "open");

    /* Signal that this thread is in progress of shutting down. */
    ASMAtomicWriteBool(&pProcess->fShutdown, true);

    /*
     * Try killing the process if it's still alive at this point.
     */
    if (fProcessAlive)
    {
        if (MsProcessKilled == UINT64_MAX)
        {
            VGSvcVerbose(2, "[PID %RU32]: Is still alive and not killed yet\n", pProcess->uPID);

            MsProcessKilled = RTTimeMilliTS();
            rc2 = RTProcTerminate(pProcess->hProcess);
            if (rc2 == VERR_NOT_FOUND)
            {
                fProcessAlive = false;
            }
            else if (RT_FAILURE(rc2))
                VGSvcError("[PID %RU32]: Killing process failed with rc=%Rrc\n", pProcess->uPID, rc2);
            RTThreadSleep(500);
        }

        for (int i = 0; i < 10 && fProcessAlive; i++)
        {
            VGSvcVerbose(4, "[PID %RU32]: Kill attempt %d/10: Waiting to exit ...\n", pProcess->uPID, i + 1);
            rc2 = RTProcWait(pProcess->hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &ProcessStatus);
            if (RT_SUCCESS(rc2))
            {
                VGSvcVerbose(4, "[PID %RU32]: Kill attempt %d/10: Exited\n", pProcess->uPID, i + 1);
                fProcessAlive = false;
                break;
            }
            if (i >= 5)
            {
                VGSvcVerbose(4, "[PID %RU32]: Kill attempt %d/10: Trying to terminate ...\n", pProcess->uPID, i + 1);
                rc2 = RTProcTerminate(pProcess->hProcess);
                if (   RT_FAILURE(rc)
                    && rc2 != VERR_NOT_FOUND)
                    VGSvcError("PID %RU32]: Killing process failed with rc=%Rrc\n",
                                     pProcess->uPID, rc2);
            }
            RTThreadSleep(i >= 5 ? 2000 : 500);
        }

        if (fProcessAlive)
            VGSvcError("[PID %RU32]: Could not be killed\n", pProcess->uPID);
    }

    /*
     * Shutdown procedure:
     * - Set the pProcess->fShutdown indicator to let others know we're
     *   not accepting any new requests anymore.
     * - After setting the indicator, try to process all outstanding
     *   requests to make sure they're getting delivered.
     *
     * Note: After removing the process from the session's list it's not
     *       even possible for the session anymore to control what's
     *       happening to this thread, so be careful and don't mess it up.
     */

    rc2 = vgsvcGstCtrlProcessLock(pProcess);
    if (RT_SUCCESS(rc2))
    {
        VGSvcVerbose(3, "[PID %RU32]: Processing outstanding requests ...\n", pProcess->uPID);

        /* Process all pending requests (but don't wait for new ones). */
        Assert(pProcess->hReqQueue != NIL_RTREQQUEUE);
        rc2 = RTReqQueueProcess(pProcess->hReqQueue, 0 /* No timeout */);
        if (   RT_FAILURE(rc2)
            && rc2 != VERR_TIMEOUT)
            VGSvcError("[PID %RU32]: Processing outstanding requests failed with with rc=%Rrc\n", pProcess->uPID, rc2);

        VGSvcVerbose(3, "[PID %RU32]: Processing outstanding requests done, rc=%Rrc\n", pProcess->uPID, rc2);

        rc2 = vgsvcGstCtrlProcessUnlock(pProcess);
        AssertRC(rc2);
    }

    /*
     * If we don't have a client problem (RT_FAILURE(rc)) we'll reply to the
     * clients exec packet now.
     */
    if (RT_SUCCESS(rc))
    {
        uint32_t uStatus = PROC_STS_UNDEFINED;
        uint32_t fFlags = 0;

        if (     fProcessTimedOut  && !fProcessAlive && MsProcessKilled != UINT64_MAX)
        {
            VGSvcVerbose(3, "[PID %RU32]: Timed out and got killed\n", pProcess->uPID);
            uStatus = PROC_STS_TOK;
        }
        else if (fProcessTimedOut  &&  fProcessAlive && MsProcessKilled != UINT64_MAX)
        {
            VGSvcVerbose(3, "[PID %RU32]: Timed out and did *not* get killed\n", pProcess->uPID);
            uStatus = PROC_STS_TOA;
        }
        else if (pProcess->fShutdown && (fProcessAlive || MsProcessKilled != UINT64_MAX))
        {
            VGSvcVerbose(3, "[PID %RU32]: Got terminated because system/service is about to shutdown\n", pProcess->uPID);
            uStatus = PROC_STS_DWN; /* Service is stopping, process was killed. */
            fFlags  = pProcess->pStartupInfo->fFlags; /* Return handed-in execution flags back to the host. */
        }
        else if (fProcessAlive)
            VGSvcError("[PID %RU32]: Is alive when it should not!\n", pProcess->uPID);
        else if (MsProcessKilled != UINT64_MAX)
            VGSvcError("[PID %RU32]: Has been killed when it should not!\n", pProcess->uPID);
        else if (ProcessStatus.enmReason == RTPROCEXITREASON_NORMAL)
        {
            VGSvcVerbose(3, "[PID %RU32]: Ended with RTPROCEXITREASON_NORMAL (Exit code: %d)\n",
                         pProcess->uPID, ProcessStatus.iStatus);
            uStatus = PROC_STS_TEN;
            fFlags  = ProcessStatus.iStatus;
        }
        else if (ProcessStatus.enmReason == RTPROCEXITREASON_SIGNAL)
        {
            VGSvcVerbose(3, "[PID %RU32]: Ended with RTPROCEXITREASON_SIGNAL (Signal: %u)\n",
                         pProcess->uPID, ProcessStatus.iStatus);
            uStatus = PROC_STS_TES;
            fFlags  = ProcessStatus.iStatus;
        }
        else if (ProcessStatus.enmReason == RTPROCEXITREASON_ABEND)
        {
            /* ProcessStatus.iStatus will be undefined. */
            VGSvcVerbose(3, "[PID %RU32]: Ended with RTPROCEXITREASON_ABEND\n", pProcess->uPID);
            uStatus = PROC_STS_TEA;
            fFlags  = ProcessStatus.iStatus;
        }
        else
            VGSvcVerbose(1, "[PID %RU32]: Handling process status %u not implemented\n", pProcess->uPID, ProcessStatus.enmReason);
        VBGLR3GUESTCTRLCMDCTX ctxEnd = { g_idControlSvcClient, pProcess->uContextID, 0 /* uProtocol */, 0 /* uNumParms */ };
        VGSvcVerbose(2, "[PID %RU32]: Ended, ClientID=%u, CID=%u, Status=%u, Flags=0x%x\n",
                     pProcess->uPID, ctxEnd.uClientID, pProcess->uContextID, uStatus, fFlags);

        rc2 = VbglR3GuestCtrlProcCbStatus(&ctxEnd, pProcess->uPID, uStatus, fFlags, NULL /* pvData */, 0 /* cbData */);
        if (   RT_FAILURE(rc2)
            && rc2 == VERR_NOT_FOUND)
            VGSvcError("[PID %RU32]: Error reporting final status to host; rc=%Rrc\n", pProcess->uPID, rc2);
    }

    VGSvcVerbose(3, "[PID %RU32]: Process loop returned with rc=%Rrc\n", pProcess->uPID, rc);
    return rc;
}


#if 0 /* unused */
/**
 * Initializes a pipe's handle and pipe object.
 *
 * @return  IPRT status code.
 * @param   ph                      The pipe's handle to initialize.
 * @param   phPipe                  The pipe's object to initialize.
 */
static int vgsvcGstCtrlProcessInitPipe(PRTHANDLE ph, PRTPIPE phPipe)
{
    AssertPtrReturn(ph, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phPipe, VERR_INVALID_PARAMETER);

    ph->enmType = RTHANDLETYPE_PIPE;
    ph->u.hPipe = NIL_RTPIPE;
    *phPipe     = NIL_RTPIPE;

    return VINF_SUCCESS;
}
#endif


/**
 * Sets up the redirection / pipe / nothing for one of the standard handles.
 *
 * @returns IPRT status code.  No client replies made.
 * @param   pszHowTo            How to set up this standard handle.
 * @param   fd                  Which standard handle it is (0 == stdin, 1 ==
 *                              stdout, 2 == stderr).
 * @param   ph                  The generic handle that @a pph may be set
 *                              pointing to.  Always set.
 * @param   pph                 Pointer to the RTProcCreateExec argument.
 *                              Always set.
 * @param   phPipe              Where to return the end of the pipe that we
 *                              should service.
 */
static int vgsvcGstCtrlProcessSetupPipe(const char *pszHowTo, int fd, PRTHANDLE ph, PRTHANDLE *pph, PRTPIPE phPipe)
{
    AssertPtrReturn(ph, VERR_INVALID_POINTER);
    AssertPtrReturn(pph, VERR_INVALID_POINTER);
    AssertPtrReturn(phPipe, VERR_INVALID_POINTER);

    int rc;

    ph->enmType = RTHANDLETYPE_PIPE;
    ph->u.hPipe = NIL_RTPIPE;
    *pph        = NULL;
    *phPipe     = NIL_RTPIPE;

    if (!strcmp(pszHowTo, "|"))
    {
        /*
         * Setup a pipe for forwarding to/from the client.
         * The ph union struct will be filled with a pipe read/write handle
         * to represent the "other" end to phPipe.
         */
        if (fd == 0) /* stdin? */
        {
            /* Connect a wrtie pipe specified by phPipe to stdin. */
            rc = RTPipeCreate(&ph->u.hPipe, phPipe, RTPIPE_C_INHERIT_READ);
        }
        else /* stdout or stderr. */
        {
            /* Connect a read pipe specified by phPipe to stdout or stderr. */
            rc = RTPipeCreate(phPipe, &ph->u.hPipe, RTPIPE_C_INHERIT_WRITE);
        }

        if (RT_FAILURE(rc))
            return rc;

        ph->enmType = RTHANDLETYPE_PIPE;
        *pph = ph;
    }
    else if (!strcmp(pszHowTo, "/dev/null"))
    {
        /*
         * Redirect to/from /dev/null.
         */
        RTFILE hFile;
        rc = RTFileOpenBitBucket(&hFile, fd == 0 ? RTFILE_O_READ : RTFILE_O_WRITE);
        if (RT_FAILURE(rc))
            return rc;

        ph->enmType = RTHANDLETYPE_FILE;
        ph->u.hFile = hFile;
        *pph = ph;
    }
    else /* Add other piping stuff here. */
        rc = VINF_SUCCESS; /* Same as parent (us). */

    return rc;
}


/**
 * Expands a file name / path to its real content.
 *
 * ~~This only works on Windows for now (e.g. translating "%TEMP%\foo.exe" to
 * "C:\Windows\Temp" when starting with system / administrative rights).~~ See
 * todo in code.
 *
 * @return  IPRT status code.
 * @param   pszPath                     Path to resolve.
 * @param   pszExpanded                 Pointer to string to store the resolved path in.
 * @param   cbExpanded                  Size (in bytes) of string to store the resolved path.
 */
static int vgsvcGstCtrlProcessMakeFullPath(const char *pszPath, char *pszExpanded, size_t cbExpanded)
{
/** @todo r=bird: This feature shall be made optional, i.e. require a
 *        flag to be passed down.  Further, it shall work on the environment
 *        block of the new process (i.e. include env changes passed down from
 *        the caller).  I would also suggest using the unix variable expansion
 *        syntax, not the DOS one.
 *
 *        Since this currently not available on non-windows guests, I suggest
 *        we disable it until such a time as it is implemented correctly. */
#if 0 /*def RT_OS_WINDOWS - see above. Don't know why this wasn't disabled before 7.0, didn't see the @todo yet? */
    int rc = VINF_SUCCESS;
    if (!ExpandEnvironmentStrings(pszPath, pszExpanded, (DWORD)cbExpanded))
        rc = RTErrConvertFromWin32(GetLastError());
#else
    /* There is no expansion anywhere yet, see above @todo. */
    int rc = RTStrCopy(pszExpanded, cbExpanded, pszPath);
#endif
#ifdef DEBUG
    VGSvcVerbose(3, "vgsvcGstCtrlProcessMakeFullPath: %s -> %s\n", pszPath, pszExpanded);
#endif
    return rc;
}


/**
 * Resolves the full path of a specified executable name.
 *
 * This function also resolves internal VBoxService tools to its appropriate
 * executable path + name if VBOXSERVICE_NAME is specified as pszFilename.
 *
 * @return  IPRT status code.
 * @param   pszFilename                 File name to resolve.
 * @param   pszResolved                 Pointer to a string where the resolved file name will be stored.
 * @param   cbResolved                  Size (in bytes) of resolved file name string.
 */
static int vgsvcGstCtrlProcessResolveExecutable(const char *pszFilename, char *pszResolved, size_t cbResolved)
{
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(pszResolved, VERR_INVALID_POINTER);
    AssertReturn(cbResolved, VERR_INVALID_PARAMETER);

    const char * const pszOrgFilename = pszFilename;
    if (   RTStrICmp(pszFilename, g_pszProgName) == 0
        || RTStrICmp(pszFilename, VBOXSERVICE_NAME) == 0)
        pszFilename = RTProcExecutablePath();

    int rc = vgsvcGstCtrlProcessMakeFullPath(pszFilename, pszResolved, cbResolved);
    if (RT_SUCCESS(rc))
        VGSvcVerbose(3, "Looked up executable: %s -> %s\n", pszOrgFilename, pszResolved);
    return rc;
}


/**
 * Constructs the argv command line by resolving environment variables
 * and relative paths.
 *
 * @return IPRT status code.
 * @param  pszArgv0         First argument (argv0), either original or modified version.
 * @param  papszArgs        Original argv command line from the host, starting at argv[1].
 * @param  fFlags           The process creation flags pass to us from the host.
 * @param  fExecutingSelf   Set if we're executing the VBoxService executable
 *                          and should inject the --utf8-argv trick.
 * @param  ppapszArgv       Pointer to a pointer with the new argv command line.
 *                          Needs to be freed with RTGetOptArgvFree.
 */
static int vgsvcGstCtrlProcessAllocateArgv(const char *pszArgv0, const char * const *papszArgs, uint32_t fFlags,
                                           bool fExecutingSelf, char ***ppapszArgv)
{
    VGSvcVerbose(3, "VGSvcGstCtrlProcessPrepareArgv: pszArgv0=%p, papszArgs=%p, fFlags=%#x, fExecutingSelf=%d, ppapszArgv=%p\n",
                 pszArgv0, papszArgs, fFlags, fExecutingSelf, ppapszArgv);

    AssertPtrReturn(pszArgv0,   VERR_INVALID_POINTER);
    AssertPtrReturn(ppapszArgv, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & GUEST_PROC_CREATE_FLAG_EXPAND_ARGUMENTS), VERR_INVALID_FLAGS); /** @todo implement me */

#ifndef VBOXSERVICE_ARG1_UTF8_ARGV
    fExecutingSelf = false;
#endif

    /* Count arguments: */
    int rc = VINF_SUCCESS;
    uint32_t cArgs;
    for (cArgs = 0; papszArgs[cArgs]; cArgs++)
    {
        if (cArgs >= UINT32_MAX - 2)
            return VERR_BUFFER_OVERFLOW;
    }

    /* Allocate new argv vector (adding + 2 for argv0 + termination). */
    size_t cbSize = (fExecutingSelf + cArgs + 2) * sizeof(char *);
    char **papszNewArgv = (char **)RTMemAlloc(cbSize);
    if (!papszNewArgv)
        return VERR_NO_MEMORY;

    VGSvcVerbose(3, "VGSvcGstCtrlProcessAllocateArgv: pszArgv0 = '%s', cArgs=%RU32, cbSize=%zu\n", pszArgv0, cArgs, cbSize);
#ifdef DEBUG /* Never log this stuff in release mode! */
    if (cArgs)
    {
        for (uint32_t i = 0; i < cArgs; i++)
            VGSvcVerbose(3, "VGSvcGstCtrlProcessAllocateArgv: papszArgs[%RU32] = '%s'\n", i, papszArgs[i]);
    }
#endif

    /* HACK ALERT! Older hosts (< VBox 6.1.x) did not allow the user to really specify
                   the first argument separately from the executable image, so we have
                   to fudge a little in the unquoted argument case to deal with executables
                   containing spaces.  Windows only, as RTPROC_FLAGS_UNQUOTED_ARGS is
                   ignored on all other hosts. */
#ifdef RT_OS_WINDOWS
    if (   (fFlags & GUEST_PROC_CREATE_FLAG_UNQUOTED_ARGS)
        && strpbrk(pszArgv0, " \t\n\r")
        && pszArgv0[0] == '"')
    {
        size_t cchArgv0 = strlen(pszArgv0);
        AssertReturn(cchArgv0, VERR_INVALID_PARAMETER); /* Paranoia. */
        rc = RTStrAllocEx(&papszNewArgv[0], 1 + cchArgv0 + 1 + 1);
        if (RT_SUCCESS(rc))
        {
            char *pszDst = papszNewArgv[0];
            *pszDst++ = '"';
            memcpy(pszDst, pszArgv0, cchArgv0);
            pszDst += cchArgv0;
            *pszDst++ = '"';
            *pszDst   = '\0';
        }
    }
    else
#endif
        rc = RTStrDupEx(&papszNewArgv[0], pszArgv0);
    if (RT_SUCCESS(rc))
    {
        size_t iDst = 1;

#ifdef VBOXSERVICE_ARG1_UTF8_ARGV
        /* Insert --utf8-argv as the first argument if executing the VBoxService binary. */
        if (fExecutingSelf)
        {
            rc = RTStrDupEx(&papszNewArgv[iDst], VBOXSERVICE_ARG1_UTF8_ARGV);
            if (RT_SUCCESS(rc))
                iDst++;
        }
#endif
        /* Copy over the other arguments. */
        if (RT_SUCCESS(rc))
            for (size_t iSrc = 0; iSrc < cArgs; iSrc++)
            {
#if 0 /* Arguments expansion -- untested. */
                if (fFlags & GUEST_PROC_CREATE_FLAG_EXPAND_ARGUMENTS)
                {
/** @todo r=bird: If you want this, we need a generic implementation, preferably in RTEnv or somewhere like that.  The marking
 * up of the variables must be the same on all platforms.  */
                    /* According to MSDN the limit on older Windows version is 32K, whereas
                     * Vista+ there are no limits anymore. We still stick to 4K. */
                    char szExpanded[_4K];
# ifdef RT_OS_WINDOWS
                    if (!ExpandEnvironmentStrings(papszArgs[i], szExpanded, sizeof(szExpanded)))
                        rc = RTErrConvertFromWin32(GetLastError());
# else
                    /* No expansion for non-Windows yet. */
                    rc = RTStrCopy(papszArgs[i], sizeof(szExpanded), szExpanded);
# endif
                    if (RT_SUCCESS(rc))
                        rc = RTStrDupEx(&pszArg, szExpanded);
                }
                else
#endif
                rc = RTStrDupEx(&papszNewArgv[iDst], papszArgs[iSrc]);
                if (RT_SUCCESS(rc))
                    iDst++;
                else
                    break;
            }

        if (RT_SUCCESS(rc))
        {
            /* Terminate array. */
            papszNewArgv[iDst] = NULL;

            *ppapszArgv = papszNewArgv;
            return VINF_SUCCESS;
        }

        /* Failed, bail out. */
        while (iDst-- > 0)
            RTStrFree(papszNewArgv[iDst]);
    }
    RTMemFree(papszNewArgv);
    return rc;
}


/**
 * Assigns a valid PID to a guest control thread and also checks if there already was
 * another (stale) guest process which was using that PID before and destroys it.
 *
 * @return  IPRT status code.
 * @param   pProcess       Process to assign PID to.
 * @param   uPID           PID to assign to the specified guest control execution thread.
 */
static int vgsvcGstCtrlProcessAssignPID(PVBOXSERVICECTRLPROCESS pProcess, uint32_t uPID)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    AssertReturn(uPID, VERR_INVALID_PARAMETER);

    AssertPtr(pProcess->pSession);
    int rc = RTCritSectEnter(&pProcess->pSession->CritSect);
    if (RT_SUCCESS(rc))
    {
        /* Search old threads using the desired PID and shut them down completely -- it's
         * not used anymore. */
        bool fTryAgain;
        do
        {
            fTryAgain = false;
            PVBOXSERVICECTRLPROCESS pProcessCur;
            RTListForEach(&pProcess->pSession->lstProcesses, pProcessCur, VBOXSERVICECTRLPROCESS, Node)
            {
                if (pProcessCur->uPID == uPID)
                {
                    Assert(pProcessCur != pProcess); /* can't happen */
                    uint32_t uTriedPID = uPID;
                    uPID += 391939;
                    VGSvcVerbose(2, "PID %RU32 was used before (process %p), trying again with %RU32 ...\n",
                                       uTriedPID, pProcessCur, uPID);
                    fTryAgain = true;
                    break;
                }
            }
        } while (fTryAgain);

        /* Assign PID to current thread. */
        pProcess->uPID = uPID;

        rc = RTCritSectLeave(&pProcess->pSession->CritSect);
        AssertRC(rc);
    }

    return rc;
}


static void vgsvcGstCtrlProcessFreeArgv(char **papszArgv)
{
    if (papszArgv)
    {
        size_t i = 0;
        while (papszArgv[i])
            RTStrFree(papszArgv[i++]);
        RTMemFree(papszArgv);
    }
}


/**
 * Helper function to create/start a process on the guest.
 *
 * @return  IPRT status code.
 * @param   pszExec                     Full qualified path of process to start (without arguments).
 * @param   papszArgs                   Pointer to array of command line arguments.
 * @param   hEnv                        Handle to environment block to use.
 * @param   fFlags                      Process execution flags.
 * @param   phStdIn                     Handle for the process' stdin pipe.
 * @param   phStdOut                    Handle for the process' stdout pipe.
 * @param   phStdErr                    Handle for the process' stderr pipe.
 * @param   pszAsUser                   User name (account) to start the process under.
 * @param   pszPassword                 Password of the specified user.
 * @param   pszDomain                   Domain to use for authentication.
 * @param   pszCwd                      Current working directory to use for the created process.
 *                                      Set to NULL if not being used.
 * @param   phProcess                   Pointer which will receive the process handle after
 *                                      successful process start.
 */
static int vgsvcGstCtrlProcessCreateProcess(const char *pszExec, const char * const *papszArgs, RTENV hEnv, uint32_t fFlags,
                                            PCRTHANDLE phStdIn, PCRTHANDLE phStdOut, PCRTHANDLE phStdErr,
                                            const char *pszAsUser, const char *pszPassword, const char *pszDomain,
                                            const char *pszCwd, PRTPROCESS phProcess)
{
#ifndef RT_OS_WINDOWS
    RT_NOREF1(pszDomain);
#endif
    AssertPtrReturn(pszExec, VERR_INVALID_PARAMETER);
    AssertPtrReturn(papszArgs, VERR_INVALID_PARAMETER);
    /* phStdIn is optional. */
    /* phStdOut is optional. */
    /* phStdErr is optional. */
    /* pszPassword is optional. */
    /* pszDomain is optional. */
    /* pszCwd is optional. */
    AssertPtrReturn(phProcess, VERR_INVALID_PARAMETER);

    int  rc = VINF_SUCCESS;
    char szExecExp[RTPATH_MAX];

#ifdef DEBUG
    /* Never log this in release mode! */
    VGSvcVerbose(4, "pszUser=%s, pszPassword=%s, pszDomain=%s\n", pszAsUser, pszPassword, pszDomain);
#endif

#ifdef RT_OS_WINDOWS
    /*
     * If sysprep should be executed do this in the context of VBoxService, which
     * (usually, if started by SCM) has administrator rights. Because of that a UI
     * won't be shown (doesn't have a desktop).
     */
    if (!RTStrICmp(pszExec, "sysprep"))
    {
        /* Use a predefined sysprep path as default. */
        char szSysprepCmd[RTPATH_MAX] = "C:\\sysprep\\sysprep.exe";
        /** @todo Check digital signature of file above before executing it? */

        /*
         * On Windows Vista (and up) sysprep is located in "system32\\Sysprep\\sysprep.exe",
         * so detect the OS and use a different path.
         */
        if (RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(6,0,0) /* Vista and later */)
        {
            rc = RTEnvGetEx(RTENV_DEFAULT, "windir", szSysprepCmd, sizeof(szSysprepCmd), NULL);
#ifndef RT_ARCH_AMD64
            /* Don't execute 64-bit sysprep from a 32-bit service host! */
            char szSysWow64[RTPATH_MAX];
            if (RTStrPrintf(szSysWow64, sizeof(szSysWow64), "%s", szSysprepCmd))
            {
                rc = RTPathAppend(szSysWow64, sizeof(szSysWow64), "SysWow64");
                AssertRC(rc);
            }
            if (   RT_SUCCESS(rc)
                && RTPathExists(szSysWow64))
                VGSvcVerbose(0, "Warning: This service is 32-bit; could not execute sysprep on 64-bit OS!\n");
#endif
            if (RT_SUCCESS(rc))
                rc = RTPathAppend(szSysprepCmd, sizeof(szSysprepCmd), "system32\\Sysprep\\sysprep.exe");
            if (RT_SUCCESS(rc))
                RTPathChangeToDosSlashes(szSysprepCmd, false /* No forcing necessary */);

            if (RT_FAILURE(rc))
                VGSvcError("Failed to detect sysrep location, rc=%Rrc\n", rc);
        }

        VGSvcVerbose(3, "Sysprep executable is: %s\n", szSysprepCmd);

        if (RT_SUCCESS(rc))
        {
            char **papszArgsExp;
            rc = vgsvcGstCtrlProcessAllocateArgv(szSysprepCmd /* argv0 */, papszArgs, fFlags,
                                                 false /*fExecutingSelf*/, &papszArgsExp);
            if (RT_SUCCESS(rc))
            {
                /* As we don't specify credentials for the sysprep process, it will
                 * run under behalf of the account VBoxService was started under, most
                 * likely local system. */
                rc = RTProcCreateEx(szSysprepCmd, papszArgsExp, hEnv, 0 /* fFlags */,
                                    phStdIn, phStdOut, phStdErr, NULL /* pszAsUser */,
                                    NULL /* pszPassword */, NULL, phProcess);
                vgsvcGstCtrlProcessFreeArgv(papszArgsExp);
            }
        }

        if (RT_FAILURE(rc))
            VGSvcVerbose(3, "Starting sysprep returned rc=%Rrc\n", rc);

        return rc;
    }
#endif /* RT_OS_WINDOWS */

    bool fExecutingSelf = false;
#ifdef VBOX_WITH_VBOXSERVICE_TOOLBOX
    /* The "vbox_" prefix is reserved for the toolbox (vbox_cat, vbox_mkdir,
       et al.) and we will replace pszExec with the full VBoxService path instead.  */
    if (RTStrStartsWith(pszExec, "vbox_"))
    {
        fExecutingSelf = true;
        rc = vgsvcGstCtrlProcessResolveExecutable(VBOXSERVICE_NAME, szExecExp, sizeof(szExecExp));
    }
    else
    {
#endif
        /*
         * Do the environment variables expansion on executable and arguments.
         */
        rc = vgsvcGstCtrlProcessResolveExecutable(pszExec, szExecExp, sizeof(szExecExp));
#ifdef VBOX_WITH_VBOXSERVICE_TOOLBOX
    }
#endif
    if (RT_SUCCESS(rc))
    {
        /*
         * This one is a bit tricky to also support older hosts:
         *
         * - If the host does not provide a dedicated argv[0] (< VBox 6.1.x), we use the
         *   unmodified executable name (pszExec) as the (default) argv[0]. This is wrong, but we can't do
         *   much about it. The rest (argv[1,2,n]) then gets set starting at papszArgs[0].
         *
         * - Newer hosts (>= VBox 6.1.x) provide a correct argv[0] independently of the actual
         *   executable name though, so actually use argv[0] *and* argv[1,2,n] as intended.
         */
        const bool fHasArgv0 = RT_BOOL(g_fControlHostFeatures0 & VBOX_GUESTCTRL_HF_0_PROCESS_ARGV0);

        const char *pcszArgv0 = (fHasArgv0 && papszArgs[0]) ? papszArgs[0] : pszExec;
        AssertPtrReturn(pcszArgv0, VERR_INVALID_POINTER); /* Paranoia. */

        const uint32_t uArgvIdx = pcszArgv0 == papszArgs[0] ? 1 : 0;

        VGSvcVerbose(3, "vgsvcGstCtrlProcessCreateProcess: fHasArgv0=%RTbool, pcszArgv0=%p, uArgvIdx=%RU32, "
                        "g_fControlHostFeatures0=%#x\n",
                     fHasArgv0, pcszArgv0, uArgvIdx, g_fControlHostFeatures0);

        char **papszArgsExp;
        rc = vgsvcGstCtrlProcessAllocateArgv(pcszArgv0, &papszArgs[uArgvIdx], fFlags, fExecutingSelf, &papszArgsExp);
        if (RT_FAILURE(rc))
        {
            /* Don't print any arguments -- may contain passwords or other sensible data! */
            VGSvcError("Could not prepare arguments, rc=%Rrc\n", rc);
        }
        else
        {
            uint32_t fProcCreateFlags = 0;
            if (fExecutingSelf)
                fProcCreateFlags |= VBOXSERVICE_PROC_F_UTF8_ARGV;
            if (fFlags)
            {
                if (fFlags & GUEST_PROC_CREATE_FLAG_HIDDEN)
                    fProcCreateFlags |= RTPROC_FLAGS_HIDDEN;
                if (fFlags & GUEST_PROC_CREATE_FLAG_PROFILE)
                    fProcCreateFlags |= RTPROC_FLAGS_PROFILE;
                if (fFlags & GUEST_PROC_CREATE_FLAG_UNQUOTED_ARGS)
                    fProcCreateFlags |= RTPROC_FLAGS_UNQUOTED_ARGS;
            }
            if (pszCwd && *pszCwd)
                fProcCreateFlags |= RTPROC_FLAGS_CWD;
            else
                pszCwd = NULL;

            /* If no user name specified run with current credentials (e.g.
             * full service/system rights). This is prohibited via official Main API!
             *
             * Otherwise use the RTPROC_FLAGS_SERVICE to use some special authentication
             * code (at least on Windows) for running processes as different users
             * started from our system service. */
            if (pszAsUser && *pszAsUser)
                fProcCreateFlags |= RTPROC_FLAGS_SERVICE;
#ifdef DEBUG
            VGSvcVerbose(3, "Command: %s\n", szExecExp);
            for (size_t i = 0; papszArgsExp[i]; i++)
                VGSvcVerbose(3, "  argv[%zu]: %s\n", i, papszArgsExp[i]);
#endif
            VGSvcVerbose(3, "Starting process '%s' ...\n", szExecExp);

#ifdef RT_OS_WINDOWS
            /* If a domain name is given, construct an UPN (User Principle Name) with
             * the domain name built-in, e.g. "joedoe@example.com". */
            char *pszUserUPN = NULL;
            if (pszDomain && *pszDomain != '\0')
            {
                pszAsUser = pszUserUPN = RTStrAPrintf2("%s@%s", pszAsUser, pszDomain);
                if (pszAsUser)
                    VGSvcVerbose(3, "Using UPN: %s\n", pszAsUser);
                else
                    rc = VERR_NO_STR_MEMORY;
            }
            if (RT_SUCCESS(rc))
#endif
            {
                /* Do normal execution. */
                rc = RTProcCreateEx(szExecExp, papszArgsExp, hEnv, fProcCreateFlags,
                                    phStdIn, phStdOut, phStdErr,
                                    pszAsUser,
                                    pszPassword && *pszPassword ? pszPassword : NULL,
                                    (void *)pszCwd /*pvExtraData*/,
                                    phProcess);

#ifdef RT_OS_WINDOWS
                RTStrFree(pszUserUPN);
#endif
                VGSvcVerbose(3, "Starting process '%s' returned rc=%Rrc\n", szExecExp, rc);
            }
            vgsvcGstCtrlProcessFreeArgv(papszArgsExp);
        }
    }
    return rc;
}


#ifdef DEBUG
/**
 * Dumps content to a file in the OS temporary directory.
 *
 * @returns VBox status code.
 * @param   pvBuf               Buffer of content to dump.
 * @param   cbBuf               Size (in bytes) of content to dump.
 * @param   pszFileNmFmt        Pointer to the file name format string, @see pg_rt_str_format.
 * @param   ...                 The format argument.
 */
static int vgsvcGstCtrlProcessDbgDumpToFileF(const void *pvBuf, size_t cbBuf, const char *pszFileNmFmt, ...)
{
    AssertPtrReturn(pszFileNmFmt, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);

    if (!cbBuf)
        return VINF_SUCCESS;

    va_list va;
    va_start(va, pszFileNmFmt);

    char *pszFileName = NULL;
    const int cchFileName = RTStrAPrintfV(&pszFileName, pszFileNmFmt, va);

    va_end(va);

    if (!cchFileName)
        return VERR_NO_MEMORY;

    char szPathFileAbs[RTPATH_MAX];
    int rc = RTPathTemp(szPathFileAbs, sizeof(szPathFileAbs));
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szPathFileAbs, sizeof(szPathFileAbs), pszFileName);

    RTStrFree(pszFileName);

    if (RT_SUCCESS(rc))
    {
        VGSvcVerbose(4, "Dumping %zu bytes to '%s'\n", cbBuf, szPathFileAbs);

        RTFILE fh;
        rc = RTFileOpen(&fh, szPathFileAbs, RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileWrite(fh, pvBuf, cbBuf, NULL /* pcbWritten */);
            RTFileClose(fh);
        }
    }

    return rc;
}
#endif /* DEBUG */


/**
 * The actual worker routine (loop) for a started guest process.
 *
 * @return  IPRT status code.
 * @param   pProcess        The process we're servicing and monitoring.
 */
static int vgsvcGstCtrlProcessProcessWorker(PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    VGSvcVerbose(3, "Thread of process pThread=0x%p = '%s' started\n", pProcess, pProcess->pStartupInfo->pszCmd);

    VGSvcVerbose(3, "Guest process '%s', flags=0x%x\n", pProcess->pStartupInfo->pszCmd, pProcess->pStartupInfo->fFlags);

    int rc = VGSvcGstCtrlSessionProcessAdd(pProcess->pSession, pProcess);
    if (RT_FAILURE(rc))
    {
        VGSvcError("Error while adding guest process '%s' (%p) to session process list, rc=%Rrc\n",
                   pProcess->pStartupInfo->pszCmd, pProcess, rc);
        RTThreadUserSignal(RTThreadSelf());
        return rc;
    }

    bool fSignalled = false; /* Indicator whether we signalled the thread user event already. */

    /*
     * Prepare argument list.
     */
    VGSvcVerbose(3, "vgsvcGstCtrlProcessProcessWorker: fHostFeatures0       = %#x\n",     g_fControlHostFeatures0);
    VGSvcVerbose(3, "vgsvcGstCtrlProcessProcessWorker: StartupInfo.szCmd    = '%s'\n",    pProcess->pStartupInfo->pszCmd);
    VGSvcVerbose(3, "vgsvcGstCtrlProcessProcessWorker: StartupInfo.szCwd    = '%s'\n",    pProcess->pStartupInfo->pszCwd
                                                                                        ? pProcess->pStartupInfo->pszCwd : "<None>");
    VGSvcVerbose(3, "vgsvcGstCtrlProcessProcessWorker: StartupInfo.uNumArgs = '%RU32'\n", pProcess->pStartupInfo->cArgs);
#ifdef DEBUG /* Never log this stuff in release mode! */
    VGSvcVerbose(3, "vgsvcGstCtrlProcessProcessWorker: StartupInfo.szArgs   = '%s'\n",    pProcess->pStartupInfo->pszArgs);
#endif

    char **papszArgs;
    int cArgs = 0; /* Initialize in case of RTGetOptArgvFromString() is failing ... */
    rc = RTGetOptArgvFromString(&papszArgs, &cArgs,
                                pProcess->pStartupInfo->cArgs > 0 ? pProcess->pStartupInfo->pszArgs : "",
                                RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, NULL);

    VGSvcVerbose(3, "vgsvcGstCtrlProcessProcessWorker: cArgs = %d\n", cArgs);
#ifdef VBOX_STRICT
    for (int i = 0; i < cArgs; i++)
        VGSvcVerbose(3, "vgsvcGstCtrlProcessProcessWorker: papszArgs[%d] = '%s'\n", i, papszArgs[i] ? papszArgs[i] : "<NULL>");

    const bool fHasArgv0    = RT_BOOL(g_fControlHostFeatures0 & VBOX_GUESTCTRL_HF_0_PROCESS_ARGV0); RT_NOREF(fHasArgv0);
    const int  cArgsToCheck = cArgs + (fHasArgv0 ? 0 : 1);

    /* Did we get the same result?
     * Take into account that we might not have supplied a (correct) argv[0] from the host. */
    AssertMsg((int)pProcess->pStartupInfo->cArgs == cArgsToCheck,
              ("rc=%Rrc, StartupInfo.uNumArgs=%RU32 != cArgsToCheck=%d, cArgs=%d, fHostFeatures0=%#x\n",
               rc, pProcess->pStartupInfo->cArgs, cArgsToCheck, cArgs, g_fControlHostFeatures0));
#endif

    /*
     * Create the environment.
     */
    uint32_t const cbEnv = pProcess->pStartupInfo->cbEnv;
    if (RT_SUCCESS(rc))
        AssertStmt(   cbEnv <= GUEST_PROC_MAX_ENV_LEN
                   || pProcess->pStartupInfo->cEnvVars == 0,
                   rc = VERR_INVALID_PARAMETER);
    if (RT_SUCCESS(rc))
    {
        RTENV hEnv;
        rc = RTEnvClone(&hEnv, RTENV_DEFAULT);
        if (RT_SUCCESS(rc))
        {
            VGSvcVerbose(3, "Additional environment variables: %RU32 (%RU32 bytes)\n",
                         pProcess->pStartupInfo->cEnvVars, cbEnv);

            if (   pProcess->pStartupInfo->cEnvVars
                && cbEnv > 0)
            {
                size_t offCur = 0;
                while (offCur < cbEnv)
                {
                    const char * const pszCur = &pProcess->pStartupInfo->pszEnv[offCur];
                    size_t const       cchCur = RTStrNLen(pszCur, cbEnv - offCur);
                    AssertBreakStmt(cchCur < cbEnv - offCur, rc = VERR_INVALID_PARAMETER);
                    VGSvcVerbose(3, "Setting environment variable: '%s'\n", pszCur);
                    rc = RTEnvPutEx(hEnv, pszCur);
                    if (RT_SUCCESS(rc))
                        offCur += cchCur + 1;
                    else
                    {
                        VGSvcError("Setting environment variable '%s' failed: %Rrc\n", pszCur, rc);
                        break;
                    }
                }
            }

            if (RT_SUCCESS(rc))
            {
                /*
                 * Setup the redirection of the standard stuff.
                 */
                /** @todo consider supporting: gcc stuff.c >file 2>&1.  */
                RTHANDLE    hStdIn;
                PRTHANDLE   phStdIn;
                rc = vgsvcGstCtrlProcessSetupPipe("|", 0 /*STDIN_FILENO*/,
                                             &hStdIn, &phStdIn, &pProcess->hPipeStdInW);
                if (RT_SUCCESS(rc))
                {
                    RTHANDLE    hStdOut;
                    PRTHANDLE   phStdOut;
                    rc = vgsvcGstCtrlProcessSetupPipe(  (pProcess->pStartupInfo->fFlags & GUEST_PROC_CREATE_FLAG_WAIT_STDOUT)
                                                 ? "|" : "/dev/null",
                                                 1 /*STDOUT_FILENO*/,
                                                 &hStdOut, &phStdOut, &pProcess->hPipeStdOutR);
                    if (RT_SUCCESS(rc))
                    {
                        RTHANDLE    hStdErr;
                        PRTHANDLE   phStdErr;
                        rc = vgsvcGstCtrlProcessSetupPipe(  (pProcess->pStartupInfo->fFlags & GUEST_PROC_CREATE_FLAG_WAIT_STDERR)
                                                     ? "|" : "/dev/null",
                                                     2 /*STDERR_FILENO*/,
                                                     &hStdErr, &phStdErr, &pProcess->hPipeStdErrR);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Create a poll set for the pipes and let the
                             * transport layer add stuff to it as well.
                             */
                            rc = RTPollSetCreate(&pProcess->hPollSet);
                            if (RT_SUCCESS(rc))
                            {
                                uint32_t uFlags = RTPOLL_EVT_ERROR;
#if 0
                                /* Add reading event to pollset to get some more information. */
                                uFlags |= RTPOLL_EVT_READ;
#endif
                                /* Stdin. */
                                if (RT_SUCCESS(rc))
                                    rc = RTPollSetAddPipe(pProcess->hPollSet,
                                                          pProcess->hPipeStdInW, RTPOLL_EVT_ERROR, VBOXSERVICECTRLPIPEID_STDIN);
                                /* Stdout. */
                                if (RT_SUCCESS(rc))
                                    rc = RTPollSetAddPipe(pProcess->hPollSet,
                                                          pProcess->hPipeStdOutR, uFlags, VBOXSERVICECTRLPIPEID_STDOUT);
                                /* Stderr. */
                                if (RT_SUCCESS(rc))
                                    rc = RTPollSetAddPipe(pProcess->hPollSet,
                                                          pProcess->hPipeStdErrR, uFlags, VBOXSERVICECTRLPIPEID_STDERR);
                                /* IPC notification pipe. */
                                if (RT_SUCCESS(rc))
                                    rc = RTPipeCreate(&pProcess->hNotificationPipeR, &pProcess->hNotificationPipeW, 0 /* Flags */);
                                if (RT_SUCCESS(rc))
                                    rc = RTPollSetAddPipe(pProcess->hPollSet,
                                                          pProcess->hNotificationPipeR, RTPOLL_EVT_READ, VBOXSERVICECTRLPIPEID_IPC_NOTIFY);
                                if (RT_SUCCESS(rc))
                                {
                                    AssertPtr(pProcess->pSession);
                                    bool fNeedsImpersonation = !(pProcess->pSession->fFlags & VBOXSERVICECTRLSESSION_FLAG_SPAWN);

                                    rc = vgsvcGstCtrlProcessCreateProcess(pProcess->pStartupInfo->pszCmd, papszArgs, hEnv,
                                                                     pProcess->pStartupInfo->fFlags,
                                                                     phStdIn, phStdOut, phStdErr,
                                                                     fNeedsImpersonation ? pProcess->pStartupInfo->pszUser     : NULL,
                                                                     fNeedsImpersonation ? pProcess->pStartupInfo->pszPassword : NULL,
                                                                     fNeedsImpersonation ? pProcess->pStartupInfo->pszDomain   : NULL,
                                                                     pProcess->pStartupInfo->pszCwd, &pProcess->hProcess);
                                    if (RT_FAILURE(rc))
                                        VGSvcError("Error starting process, rc=%Rrc\n", rc);
                                    /*
                                     * Tell the session thread that it can continue
                                     * spawning guest processes. This needs to be done after the new
                                     * process has been started because otherwise signal handling
                                     * on (Open) Solaris does not work correctly (see @bugref{5068}).
                                     */
                                    int rc2 = RTThreadUserSignal(RTThreadSelf());
                                    if (RT_SUCCESS(rc))
                                        rc = rc2;
                                    fSignalled = true;

                                    if (RT_SUCCESS(rc))
                                    {
                                        /*
                                         * Close the child ends of any pipes and redirected files.
                                         */
                                        rc2 = RTHandleClose(phStdIn);   AssertRC(rc2);
                                        phStdIn    = NULL;
                                        rc2 = RTHandleClose(phStdOut);  AssertRC(rc2);
                                        phStdOut   = NULL;
                                        rc2 = RTHandleClose(phStdErr);  AssertRC(rc2);
                                        phStdErr   = NULL;

                                        /* Enter the process main loop. */
                                        rc = vgsvcGstCtrlProcessProcLoop(pProcess);

                                        /*
                                         * The handles that are no longer in the set have
                                         * been closed by the above call in order to prevent
                                         * the guest from getting stuck accessing them.
                                         * So, NIL the handles to avoid closing them again.
                                         */
                                        if (RT_FAILURE(RTPollSetQueryHandle(pProcess->hPollSet,
                                                                            VBOXSERVICECTRLPIPEID_IPC_NOTIFY, NULL)))
                                            pProcess->hNotificationPipeW = NIL_RTPIPE;
                                        if (RT_FAILURE(RTPollSetQueryHandle(pProcess->hPollSet,
                                                                            VBOXSERVICECTRLPIPEID_STDERR, NULL)))
                                            pProcess->hPipeStdErrR = NIL_RTPIPE;
                                        if (RT_FAILURE(RTPollSetQueryHandle(pProcess->hPollSet,
                                                                            VBOXSERVICECTRLPIPEID_STDOUT, NULL)))
                                            pProcess->hPipeStdOutR = NIL_RTPIPE;
                                        if (RT_FAILURE(RTPollSetQueryHandle(pProcess->hPollSet,
                                                                            VBOXSERVICECTRLPIPEID_STDIN, NULL)))
                                            pProcess->hPipeStdInW = NIL_RTPIPE;
                                    }
                                }
                                RTPollSetDestroy(pProcess->hPollSet);
                                pProcess->hPollSet = NIL_RTPOLLSET;

                                RTPipeClose(pProcess->hNotificationPipeR);
                                pProcess->hNotificationPipeR = NIL_RTPIPE;
                                RTPipeClose(pProcess->hNotificationPipeW);
                                pProcess->hNotificationPipeW = NIL_RTPIPE;
                            }
                            RTPipeClose(pProcess->hPipeStdErrR);
                            pProcess->hPipeStdErrR = NIL_RTPIPE;
                            RTHandleClose(&hStdErr);
                            if (phStdErr)
                                RTHandleClose(phStdErr);
                        }
                        RTPipeClose(pProcess->hPipeStdOutR);
                        pProcess->hPipeStdOutR = NIL_RTPIPE;
                        RTHandleClose(&hStdOut);
                        if (phStdOut)
                            RTHandleClose(phStdOut);
                    }
                    RTPipeClose(pProcess->hPipeStdInW);
                    pProcess->hPipeStdInW = NIL_RTPIPE;
                    RTHandleClose(&hStdIn);
                    if (phStdIn)
                        RTHandleClose(phStdIn);
                }
            }
            RTEnvDestroy(hEnv);
        }
    }

    if (RT_FAILURE(rc))
    {
        VBGLR3GUESTCTRLCMDCTX ctx = { g_idControlSvcClient, pProcess->uContextID, 0 /* uProtocol */, 0 /* uNumParms */ };
        int rc2 = VbglR3GuestCtrlProcCbStatus(&ctx,
                                              pProcess->uPID, PROC_STS_ERROR, rc,
                                              NULL /* pvData */, 0 /* cbData */);
        if (   RT_FAILURE(rc2)
            && rc2 != VERR_NOT_FOUND)
            VGSvcError("[PID %RU32]: Could not report process failure error; rc=%Rrc (process error %Rrc)\n",
                       pProcess->uPID, rc2, rc);
    }

    /* Update stopped status. */
    ASMAtomicWriteBool(&pProcess->fStopped, true);

    if (cArgs)
        RTGetOptArgvFree(papszArgs);

    /*
     * If something went wrong signal the user event so that others don't wait
     * forever on this thread.
     */
    if (   RT_FAILURE(rc)
        && !fSignalled)
    {
        RTThreadUserSignal(RTThreadSelf());
    }

    /* Set shut down flag in case we've forgotten it. */
    ASMAtomicWriteBool(&pProcess->fShutdown, true);

    VGSvcVerbose(3, "[PID %RU32]: Thread of process '%s' ended with rc=%Rrc (fSignalled=%RTbool)\n",
                 pProcess->uPID, pProcess->pStartupInfo->pszCmd, rc, fSignalled);

    return rc;
}


static int vgsvcGstCtrlProcessLock(PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    int rc = RTCritSectEnter(&pProcess->CritSect);
    AssertRC(rc);
    return rc;
}


/**
 * Thread main routine for a started process.
 *
 * @return IPRT status code.
 * @param  hThreadSelf      The thread handle.
 * @param  pvUser           Pointer to a VBOXSERVICECTRLPROCESS structure.
 *
 */
static DECLCALLBACK(int) vgsvcGstCtrlProcessThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF1(hThreadSelf);
    PVBOXSERVICECTRLPROCESS pProcess = (PVBOXSERVICECTRLPROCESS)pvUser;
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    return vgsvcGstCtrlProcessProcessWorker(pProcess);
}


static int vgsvcGstCtrlProcessUnlock(PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    int rc = RTCritSectLeave(&pProcess->CritSect);
    AssertRC(rc);
    return rc;
}


/**
 * Executes (starts) a process on the guest. This causes a new thread to be created
 * so that this function will not block the overall program execution.
 *
 * @return  IPRT status code.
 * @param   pSession                    Guest session.
 * @param   pStartupInfo                Startup info.
 * @param   uContextID                  Context ID to associate the process to start with.
 */
int VGSvcGstCtrlProcessStart(const PVBOXSERVICECTRLSESSION pSession,
                             const PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo, uint32_t uContextID)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pStartupInfo, VERR_INVALID_POINTER);

    /*
     * Allocate new thread data and assign it to our thread list.
     */
    PVBOXSERVICECTRLPROCESS pProcess = (PVBOXSERVICECTRLPROCESS)RTMemAlloc(sizeof(VBOXSERVICECTRLPROCESS));
    if (!pProcess)
        return VERR_NO_MEMORY;

    int rc = vgsvcGstCtrlProcessInit(pProcess, pSession, pStartupInfo, uContextID);
    if (RT_SUCCESS(rc))
    {
        static uint32_t s_uCtrlExecThread = 0;
        rc = RTThreadCreateF(&pProcess->Thread, vgsvcGstCtrlProcessThread,
                             pProcess /*pvUser*/, 0 /*cbStack*/,
                             RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "gctl%RU32", s_uCtrlExecThread++);
        if (RT_FAILURE(rc))
        {
            VGSvcError("Creating thread for guest process '%s' failed: rc=%Rrc, pProcess=%p\n",
                       pStartupInfo->pszCmd, rc, pProcess);

            /* Process has not been added to the session's process list yet, so skip VGSvcGstCtrlSessionProcessRemove() here. */
            VGSvcGstCtrlProcessFree(pProcess);
        }
        else
        {
            VGSvcVerbose(4, "Waiting for thread to initialize ...\n");

            /* Wait for the thread to initialize. */
            rc = RTThreadUserWait(pProcess->Thread, 60 * 1000 /* 60 seconds max. */);
            AssertRC(rc);
            if (   ASMAtomicReadBool(&pProcess->fShutdown)
                || ASMAtomicReadBool(&pProcess->fStopped)
                || RT_FAILURE(rc))
            {
                VGSvcError("Thread for process '%s' failed to start, rc=%Rrc\n", pStartupInfo->pszCmd, rc);
                int rc2 = RTThreadWait(pProcess->Thread, RT_MS_1SEC * 30, NULL);
                if (RT_SUCCESS(rc2))
                    pProcess->Thread = NIL_RTTHREAD;

                VGSvcGstCtrlSessionProcessRemove(pSession, pProcess);
                VGSvcGstCtrlProcessFree(pProcess);
            }
            else
            {
                ASMAtomicXchgBool(&pProcess->fStarted, true);
            }
        }
    }

    return rc;
}


static DECLCALLBACK(int) vgsvcGstCtrlProcessOnInput(PVBOXSERVICECTRLPROCESS pThis,
                                                    const PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                                    bool fPendingClose, void *pvBuf, uint32_t cbBuf)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    int rc;

    size_t cbWritten = 0;
    if (pvBuf && cbBuf)
    {
        if (pThis->hPipeStdInW != NIL_RTPIPE)
            rc = RTPipeWrite(pThis->hPipeStdInW, pvBuf, cbBuf, &cbWritten);
        else
            rc = VINF_EOF;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    /*
     * If this is the last write + we have really have written all data
     * we need to close the stdin pipe on our end and remove it from
     * the poll set.
     */
    if (   fPendingClose
        && cbBuf == cbWritten)
    {
        int rc2 = vgsvcGstCtrlProcessPollsetCloseInput(pThis, &pThis->hPipeStdInW);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    uint32_t uStatus = INPUT_STS_UNDEFINED; /* Status to send back to the host. */
    uint32_t fFlags = 0; /* No flags at the moment. */
    if (RT_SUCCESS(rc))
    {
        VGSvcVerbose(4, "[PID %RU32]: Written %RU32 bytes input, CID=%RU32, fPendingClose=%RTbool\n",
                     pThis->uPID, cbWritten, pHostCtx->uContextID, fPendingClose);
        uStatus = INPUT_STS_WRITTEN;
    }
    else
    {
        if (rc == VERR_BAD_PIPE)
            uStatus = INPUT_STS_TERMINATED;
        else if (rc == VERR_BUFFER_OVERFLOW)
            uStatus = INPUT_STS_OVERFLOW;
        /* else undefined */
    }

    /*
     * If there was an error and we did not set the host status
     * yet, then do it now.
     */
    if (   RT_FAILURE(rc)
        && uStatus == INPUT_STS_UNDEFINED)
    {
        uStatus = INPUT_STS_ERROR;
        fFlags = rc; /* funny thing to call a "flag"... */
    }
    Assert(uStatus > INPUT_STS_UNDEFINED);

    int rc2 = VbglR3GuestCtrlProcCbStatusInput(pHostCtx, pThis->uPID, uStatus, fFlags, (uint32_t)cbWritten);
    if (RT_SUCCESS(rc))
        rc = rc2;

#ifdef DEBUG
    VGSvcVerbose(3, "[PID %RU32]: vgsvcGstCtrlProcessOnInput returned with rc=%Rrc\n", pThis->uPID, rc);
#endif
    return rc;
}


static DECLCALLBACK(int) vgsvcGstCtrlProcessOnOutput(PVBOXSERVICECTRLPROCESS pThis,
                                                     const PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                                     uint32_t uHandle, uint32_t cbToRead, uint32_t fFlags)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    const PVBOXSERVICECTRLSESSION pSession = pThis->pSession;
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    int rc;

    uint32_t cbBuf = cbToRead;
    uint8_t *pvBuf = (uint8_t *)RTMemAlloc(cbBuf);
    if (pvBuf)
    {
        PRTPIPE phPipe = uHandle == GUEST_PROC_OUT_H_STDOUT
                       ? &pThis->hPipeStdOutR
                       : &pThis->hPipeStdErrR;
        AssertPtr(phPipe);

        size_t cbRead = 0;
        if (*phPipe != NIL_RTPIPE)
        {
            rc = RTPipeRead(*phPipe, pvBuf, cbBuf, &cbRead);
            if (RT_FAILURE(rc))
            {
                RTPollSetRemove(pThis->hPollSet,   uHandle == GUEST_PROC_OUT_H_STDERR
                                                 ? VBOXSERVICECTRLPIPEID_STDERR : VBOXSERVICECTRLPIPEID_STDOUT);
                RTPipeClose(*phPipe);
                *phPipe = NIL_RTPIPE;
                if (rc == VERR_BROKEN_PIPE)
                    rc = VINF_EOF;
            }
        }
        else
            rc = VINF_EOF;

#ifdef DEBUG
        if (RT_SUCCESS(rc))
        {
            if (   pSession->fFlags & VBOXSERVICECTRLSESSION_FLAG_DUMPSTDOUT
                && (   uHandle == GUEST_PROC_OUT_H_STDOUT
                    || uHandle == GUEST_PROC_OUT_H_STDOUT_DEPRECATED)
               )
            {
                rc = vgsvcGstCtrlProcessDbgDumpToFileF(pvBuf, cbRead, "VBoxService_Session%RU32_PID%RU32_StdOut.txt",
                                                pSession->StartupInfo.uSessionID, pThis->uPID);
                AssertRC(rc);
            }
            else if (   pSession->fFlags & VBOXSERVICECTRLSESSION_FLAG_DUMPSTDERR
                     && uHandle == GUEST_PROC_OUT_H_STDERR)
            {
                rc = vgsvcGstCtrlProcessDbgDumpToFileF(pvBuf, cbRead, "VBoxService_Session%RU32_PID%RU32_StdErr.txt",
                                                pSession->StartupInfo.uSessionID, pThis->uPID);
                AssertRC(rc);
            }
        }
#endif

        if (RT_SUCCESS(rc))
        {
#ifdef DEBUG
            VGSvcVerbose(3, "[PID %RU32]: Read %RU32 bytes output: uHandle=%RU32, CID=%RU32, fFlags=%x\n",
                         pThis->uPID, cbRead, uHandle, pHostCtx->uContextID, fFlags);
#endif
            /** Note: Don't convert/touch/modify/whatever the output data here! This might be binary
             *        data which the host needs to work with -- so just pass through all data unfiltered! */

            /* Note: Since the context ID is unique the request *has* to be completed here,
             *       regardless whether we got data or not! Otherwise the waiting events
             *       on the host never will get completed! */
            Assert((uint32_t)cbRead == cbRead);
            rc = VbglR3GuestCtrlProcCbOutput(pHostCtx, pThis->uPID, uHandle, fFlags, pvBuf, (uint32_t)cbRead);
            if (   RT_FAILURE(rc)
                && rc == VERR_NOT_FOUND) /* Not critical if guest PID is not found on the host (anymore). */
                rc = VINF_SUCCESS;
        }

        RTMemFree(pvBuf);
    }
    else
        rc = VERR_NO_MEMORY;

#ifdef DEBUG
    VGSvcVerbose(3, "[PID %RU32]: Reading output returned with rc=%Rrc\n", pThis->uPID, rc);
#endif
    return rc;
}


static DECLCALLBACK(int) vgsvcGstCtrlProcessOnTerm(PVBOXSERVICECTRLPROCESS pThis)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    if (!ASMAtomicXchgBool(&pThis->fShutdown, true))
        VGSvcVerbose(3, "[PID %RU32]: Setting shutdown flag ...\n", pThis->uPID);

    return VINF_SUCCESS;
}


static int vgsvcGstCtrlProcessRequestExV(PVBOXSERVICECTRLPROCESS pProcess, bool fAsync, RTMSINTERVAL cMsTimeout,
                                         PFNRT pfnFunction, unsigned cArgs, va_list Args)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnFunction, VERR_INVALID_POINTER);
    if (!fAsync)
        AssertPtrReturn(pfnFunction, VERR_INVALID_POINTER);

    int rc = vgsvcGstCtrlProcessLock(pProcess);
    if (RT_SUCCESS(rc))
    {
#ifdef DEBUG
        VGSvcVerbose(3, "[PID %RU32]: vgsvcGstCtrlProcessRequestExV fAsync=%RTbool, cMsTimeout=%RU32, cArgs=%u\n",
                     pProcess->uPID, fAsync, cMsTimeout, cArgs);
#endif
        uint32_t fFlags = RTREQFLAGS_IPRT_STATUS;
        if (fAsync)
        {
            Assert(cMsTimeout == 0);
            fFlags |= RTREQFLAGS_NO_WAIT;
        }

        PRTREQ hReq = NIL_RTREQ;
        rc = RTReqQueueCallV(pProcess->hReqQueue, &hReq, cMsTimeout, fFlags, pfnFunction, cArgs, Args);
        RTReqRelease(hReq);
        if (RT_SUCCESS(rc))
        {
            /* Wake up the process' notification pipe to get
             * the request being processed. */
            Assert(pProcess->hNotificationPipeW != NIL_RTPIPE || pProcess->fShutdown /* latter in case of race */);
            size_t cbWritten = 0;
            rc = RTPipeWrite(pProcess->hNotificationPipeW, "i", 1, &cbWritten);
            if (   RT_SUCCESS(rc)
                && cbWritten != 1)
            {
                VGSvcError("[PID %RU32]: Notification pipe got %zu bytes instead of 1\n",
                                 pProcess->uPID, cbWritten);
            }
            else if (RT_UNLIKELY(RT_FAILURE(rc)))
                VGSvcError("[PID %RU32]: Writing to notification pipe failed, rc=%Rrc\n",
                                 pProcess->uPID, rc);
        }
        else
            VGSvcError("[PID %RU32]: RTReqQueueCallV failed, rc=%Rrc\n",
                             pProcess->uPID, rc);

        int rc2 = vgsvcGstCtrlProcessUnlock(pProcess);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

#ifdef DEBUG
    VGSvcVerbose(3, "[PID %RU32]: vgsvcGstCtrlProcessRequestExV returned rc=%Rrc\n", pProcess->uPID, rc);
#endif
    return rc;
}


static int vgsvcGstCtrlProcessRequestAsync(PVBOXSERVICECTRLPROCESS pProcess, PFNRT pfnFunction, unsigned cArgs, ...)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnFunction, VERR_INVALID_POINTER);

    va_list va;
    va_start(va, cArgs);
    int rc = vgsvcGstCtrlProcessRequestExV(pProcess, true /* fAsync */, 0 /* cMsTimeout */,
                                           pfnFunction, cArgs, va);
    va_end(va);

    return rc;
}


#if 0 /* unused */
static int vgsvcGstCtrlProcessRequestWait(PVBOXSERVICECTRLPROCESS pProcess, RTMSINTERVAL cMsTimeout,
                                          PFNRT pfnFunction, unsigned cArgs, ...)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    /* pHostCtx is optional. */
    AssertPtrReturn(pfnFunction, VERR_INVALID_POINTER);

    va_list va;
    va_start(va, cArgs);
    int rc = vgsvcGstCtrlProcessRequestExV(pProcess, false /* fAsync */, cMsTimeout,
                                           pfnFunction, cArgs, va);
    va_end(va);

    return rc;
}
#endif


int VGSvcGstCtrlProcessHandleInput(PVBOXSERVICECTRLPROCESS pProcess, PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                   bool fPendingClose, void *pvBuf, uint32_t cbBuf)
{
    if (!ASMAtomicReadBool(&pProcess->fShutdown) && !ASMAtomicReadBool(&pProcess->fStopped))
        return vgsvcGstCtrlProcessRequestAsync(pProcess, (PFNRT)vgsvcGstCtrlProcessOnInput, 5 /*cArgs*/,
                                               pProcess, pHostCtx, fPendingClose, pvBuf, cbBuf);

    return vgsvcGstCtrlProcessOnInput(pProcess, pHostCtx, fPendingClose, pvBuf, cbBuf);
}


int VGSvcGstCtrlProcessHandleOutput(PVBOXSERVICECTRLPROCESS pProcess, PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                    uint32_t uHandle, uint32_t cbToRead, uint32_t fFlags)
{
    if (!ASMAtomicReadBool(&pProcess->fShutdown) && !ASMAtomicReadBool(&pProcess->fStopped))
        return vgsvcGstCtrlProcessRequestAsync(pProcess, (PFNRT)vgsvcGstCtrlProcessOnOutput, 5 /*cArgs*/,
                                               pProcess, pHostCtx, uHandle, cbToRead, fFlags);

    return vgsvcGstCtrlProcessOnOutput(pProcess, pHostCtx, uHandle, cbToRead, fFlags);
}


int VGSvcGstCtrlProcessHandleTerm(PVBOXSERVICECTRLPROCESS pProcess)
{
    if (!ASMAtomicReadBool(&pProcess->fShutdown) && !ASMAtomicReadBool(&pProcess->fStopped))
        return vgsvcGstCtrlProcessRequestAsync(pProcess, (PFNRT)vgsvcGstCtrlProcessOnTerm, 1 /*cArgs*/, pProcess);

    return vgsvcGstCtrlProcessOnTerm(pProcess);
}

