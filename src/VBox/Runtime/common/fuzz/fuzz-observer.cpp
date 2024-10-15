/* $Id: fuzz-observer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Fuzzing framework API, observer.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/fuzz.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/md5.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/poll.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/thread.h>


/** Poll ID for the reading end of the stdout pipe from the client process. */
#define RTFUZZOBS_EXEC_CTX_POLL_ID_STDOUT 0
/** Poll ID for the reading end of the stderr pipe from the client process. */
#define RTFUZZOBS_EXEC_CTX_POLL_ID_STDERR 1
/** Poll ID for the writing end of the stdin pipe to the client process. */
#define RTFUZZOBS_EXEC_CTX_POLL_ID_STDIN  2

/** Length of the input queue for an observer thread. */
# define RTFUZZOBS_THREAD_INPUT_QUEUE_MAX       UINT32_C(5)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the internal fuzzing observer state. */
typedef struct RTFUZZOBSINT *PRTFUZZOBSINT;


/**
 * Observer thread state for one process.
 */
typedef struct RTFUZZOBSTHRD
{
    /** The thread handle. */
    RTTHREAD                    hThread;
    /** The observer ID. */
    uint32_t                    idObs;
    /** Flag whether to shutdown. */
    volatile bool               fShutdown;
    /** Pointer to te global observer state. */
    PRTFUZZOBSINT               pFuzzObs;
    /** Number of inputs in the queue. */
    volatile uint32_t           cInputs;
    /** Where to insert the next input. */
    volatile uint32_t           offQueueInputW;
    /** Where to retrieve the next input from. */
    volatile uint32_t           offQueueInputR;
    /** The input queue for this thread. */
    RTFUZZINPUT                 ahQueueInput[RTFUZZOBS_THREAD_INPUT_QUEUE_MAX];
} RTFUZZOBSTHRD;
/** Pointer to an observer thread state. */
typedef RTFUZZOBSTHRD *PRTFUZZOBSTHRD;


/**
 * Internal fuzzing observer state.
 */
typedef struct RTFUZZOBSINT
{
    /** The fuzzing context used for this observer. */
    RTFUZZCTX                   hFuzzCtx;
    /** The target state recorder. */
    RTFUZZTGTREC                hTgtRec;
    /** Temp directory for input files. */
    char                       *pszTmpDir;
    /** Results directory. */
    char                       *pszResultsDir;
    /** The binary to run. */
    char                       *pszBinary;
    /** The filename path of the binary. */
    const char                 *pszBinaryFilename;
    /** Arguments to run the binary with, terminated by a NULL entry. */
    char                      **papszArgs;
    /** The environment to use for the target. */
    RTENV                       hEnv;
    /** Any configured sanitizers. */
    uint32_t                    fSanitizers;
    /** Sanitizer related options set in the environment block. */
    char                       *pszSanitizerOpts;
    /** Number of arguments. */
    uint32_t                    cArgs;
    /** Maximum time to wait for the client to terminate until it is considered hung and killed. */
    RTMSINTERVAL                msWaitMax;
    /** The channel the binary expects the input. */
    RTFUZZOBSINPUTCHAN          enmInputChan;
    /** Flag whether to shutdown the master and all workers. */
    volatile bool               fShutdown;
    /** Global observer thread handle. */
    RTTHREAD                    hThreadGlobal;
    /** The event semaphore handle for the global observer thread. */
    RTSEMEVENT                  hEvtGlobal;
    /** Notification event bitmap. */
    volatile uint64_t           bmEvt;
    /** Number of threads created - one for each process. */
    uint32_t                    cThreads;
    /** Pointer to the array of observer thread states. */
    PRTFUZZOBSTHRD              paObsThreads;
    /** Timestamp of the last stats query. */
    uint64_t                    tsLastStats;
    /** Last number of fuzzed inputs per second if we didn't gather enough data in between
     * statistic queries. */
    uint32_t                    cFuzzedInputsPerSecLast;
    /** Fuzzing statistics. */
    RTFUZZOBSSTATS              Stats;
} RTFUZZOBSINT;


/**
 * Worker execution context.
 */
typedef struct RTFUZZOBSEXECCTX
{
    /** The stdout pipe handle - reading end. */
    RTPIPE                      hPipeStdoutR;
    /** The stdout pipe handle - writing end. */
    RTPIPE                      hPipeStdoutW;
    /** The stderr pipe handle - reading end. */
    RTPIPE                      hPipeStderrR;
    /** The stderr pipe handle - writing end. */
    RTPIPE                      hPipeStderrW;
    /** The stdin pipe handle - reading end. */
    RTPIPE                      hPipeStdinR;
    /** The stind pipe handle - writing end. */
    RTPIPE                      hPipeStdinW;
    /** The stdout handle. */
    RTHANDLE                    StdoutHandle;
    /** The stderr handle. */
    RTHANDLE                    StderrHandle;
    /** The stdin handle. */
    RTHANDLE                    StdinHandle;
    /** The pollset to monitor. */
    RTPOLLSET                   hPollSet;
    /** The environment block to use. */
    RTENV                       hEnv;
    /** The process to monitor. */
    RTPROCESS                   hProc;
    /** Execution time of the process. */
    RTMSINTERVAL                msExec;
    /** The recording state handle. */
    RTFUZZTGTSTATE              hTgtState;
    /** Current input data pointer. */
    uint8_t                     *pbInputCur;
    /** Number of bytes left for the input. */
    size_t                      cbInputLeft;
    /** Modified arguments vector - variable in size. */
    char                        *apszArgs[1];
} RTFUZZOBSEXECCTX;
/** Pointer to an execution context. */
typedef RTFUZZOBSEXECCTX *PRTFUZZOBSEXECCTX;
/** Pointer to an execution context pointer. */
typedef PRTFUZZOBSEXECCTX *PPRTFUZZOBSEXECCTX;


/**
 * A variable descriptor.
 */
typedef struct RTFUZZOBSVARIABLE
{
    /** The variable. */
    const char                  *pszVar;
    /** Length of the variable in characters - excluding the terminator. */
    uint32_t                    cchVar;
    /** The replacement value. */
    const char                  *pszVal;
} RTFUZZOBSVARIABLE;
/** Pointer to a variable descriptor. */
typedef RTFUZZOBSVARIABLE *PRTFUZZOBSVARIABLE;



/**
 * Replaces a variable with its value.
 *
 * @returns VINF_SUCCESS or VERR_NO_STR_MEMORY.
 * @param   ppszNew             In/Out.
 * @param   pcchNew             In/Out. (Messed up on failure.)
 * @param   offVar              Variable offset.
 * @param   cchVar              Variable length.
 * @param   pszValue            The value.
 * @param   cchValue            Value length.
 */
static int rtFuzzObsReplaceStringVariable(char **ppszNew, size_t *pcchNew, size_t offVar, size_t cchVar,
                                          const char *pszValue, size_t cchValue)
{
    size_t const cchAfter = *pcchNew - offVar - cchVar;
    if (cchVar < cchValue)
    {
        *pcchNew += cchValue - cchVar;
        int rc = RTStrRealloc(ppszNew, *pcchNew + 1);
        if (RT_FAILURE(rc))
            return rc;
    }

    char *pszNew = *ppszNew;
    memmove(&pszNew[offVar + cchValue], &pszNew[offVar + cchVar], cchAfter + 1);
    memcpy(&pszNew[offVar], pszValue, cchValue);
    return VINF_SUCCESS;
}


/**
 * Replace the variables found in the source string, returning a new string that
 * lives on the string heap.
 *
 * @returns IPRT status code.
 * @param   pszSrc              The source string.
 * @param   paVars              Pointer to the array of known variables.
 * @param   ppszNew             Where to return the new string.
 */
static int rtFuzzObsReplaceStringVariables(const char *pszSrc, PRTFUZZOBSVARIABLE paVars, char **ppszNew)
{
    /* Lazy approach that employs memmove.  */
    int     rc        = VINF_SUCCESS;
    size_t  cchNew    = strlen(pszSrc);
    char   *pszNew    = RTStrDup(pszSrc);

    if (paVars)
    {
        char   *pszDollar = pszNew;
        while ((pszDollar = strchr(pszDollar, '$')) != NULL)
        {
            if (pszDollar[1] == '{')
            {
                const char *pszEnd = strchr(&pszDollar[2], '}');
                if (pszEnd)
                {
                    size_t const cchVar    = pszEnd - pszDollar + 1; /* includes "${}" */
                    size_t       offDollar = pszDollar - pszNew;
                    PRTFUZZOBSVARIABLE pVar = paVars;
                    while (pVar->pszVar != NULL)
                    {
                        if (   cchVar == pVar->cchVar
                            && !memcmp(pszDollar, pVar->pszVar, cchVar))
                        {
                            size_t const cchValue = strlen(pVar->pszVal);
                            rc = rtFuzzObsReplaceStringVariable(&pszNew, &cchNew, offDollar,
                                                                cchVar, pVar->pszVal, cchValue);
                            offDollar += cchValue;
                            break;
                        }

                        pVar++;
                    }

                    pszDollar = &pszNew[offDollar];

                    if (RT_FAILURE(rc))
                    {
                        RTStrFree(pszNew);
                        *ppszNew = NULL;
                        return rc;
                    }
                }
            }
        }
    }

    *ppszNew = pszNew;
    return rc;
}

/**
 * Prepares the argument vector for the child process.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal fuzzing observer state.
 * @param   pExecCtx            The execution context to prepare the argument vector for.
 * @param   paVars              Pointer to the array of known variables.
 */
static int rtFuzzObsExecCtxArgvPrepare(PRTFUZZOBSINT pThis, PRTFUZZOBSEXECCTX pExecCtx, PRTFUZZOBSVARIABLE paVars)
{
    int rc = VINF_SUCCESS;
    for (unsigned i = 0; i < pThis->cArgs && RT_SUCCESS(rc); i++)
        rc = rtFuzzObsReplaceStringVariables(pThis->papszArgs[i], paVars, &pExecCtx->apszArgs[i]);

    return rc;
}


/**
 * Creates a new execution context.
 *
 * @returns IPRT status code.
 * @param   ppExecCtx           Where to store the pointer to the execution context on success.
 * @param   pThis               The internal fuzzing observer state.
 */
static int rtFuzzObsExecCtxCreate(PPRTFUZZOBSEXECCTX ppExecCtx, PRTFUZZOBSINT pThis)
{
    int rc = VINF_SUCCESS;
    PRTFUZZOBSEXECCTX pExecCtx = (PRTFUZZOBSEXECCTX)RTMemAllocZ(RT_UOFFSETOF_DYN(RTFUZZOBSEXECCTX, apszArgs[pThis->cArgs + 1]));
    if (RT_LIKELY(pExecCtx))
    {
        pExecCtx->hPipeStdoutR     = NIL_RTPIPE;
        pExecCtx->hPipeStdoutW     = NIL_RTPIPE;
        pExecCtx->hPipeStderrR     = NIL_RTPIPE;
        pExecCtx->hPipeStderrW     = NIL_RTPIPE;
        pExecCtx->hPipeStdinR      = NIL_RTPIPE;
        pExecCtx->hPipeStdinW      = NIL_RTPIPE;
        pExecCtx->hPollSet         = NIL_RTPOLLSET;
        pExecCtx->hProc            = NIL_RTPROCESS;
        pExecCtx->msExec           = 0;

        rc = RTEnvClone(&pExecCtx->hEnv, pThis->hEnv);
        if (RT_SUCCESS(rc))
        {
            rc = RTFuzzTgtRecorderCreateNewState(pThis->hTgtRec, &pExecCtx->hTgtState);
            if (RT_SUCCESS(rc))
            {
                rc = RTPollSetCreate(&pExecCtx->hPollSet);
                if (RT_SUCCESS(rc))
                {
                    rc = RTPipeCreate(&pExecCtx->hPipeStdoutR, &pExecCtx->hPipeStdoutW, RTPIPE_C_INHERIT_WRITE);
                    if (RT_SUCCESS(rc))
                    {
                        RTHANDLE Handle;
                        Handle.enmType = RTHANDLETYPE_PIPE;
                        Handle.u.hPipe = pExecCtx->hPipeStdoutR;
                        rc = RTPollSetAdd(pExecCtx->hPollSet, &Handle, RTPOLL_EVT_READ, RTFUZZOBS_EXEC_CTX_POLL_ID_STDOUT);
                        AssertRC(rc);

                        rc = RTPipeCreate(&pExecCtx->hPipeStderrR, &pExecCtx->hPipeStderrW, RTPIPE_C_INHERIT_WRITE);
                        if (RT_SUCCESS(rc))
                        {
                            Handle.u.hPipe = pExecCtx->hPipeStderrR;
                            rc = RTPollSetAdd(pExecCtx->hPollSet, &Handle, RTPOLL_EVT_READ, RTFUZZOBS_EXEC_CTX_POLL_ID_STDERR);
                            AssertRC(rc);

                            /* Create the stdin pipe handles if not a file input. */
                            if (pThis->enmInputChan == RTFUZZOBSINPUTCHAN_STDIN || pThis->enmInputChan == RTFUZZOBSINPUTCHAN_FUZZING_AWARE_CLIENT)
                            {
                                rc = RTPipeCreate(&pExecCtx->hPipeStdinR, &pExecCtx->hPipeStdinW, RTPIPE_C_INHERIT_READ);
                                if (RT_SUCCESS(rc))
                                {
                                    pExecCtx->StdinHandle.enmType = RTHANDLETYPE_PIPE;
                                    pExecCtx->StdinHandle.u.hPipe = pExecCtx->hPipeStdinR;

                                    Handle.u.hPipe = pExecCtx->hPipeStdinW;
                                    rc = RTPollSetAdd(pExecCtx->hPollSet, &Handle, RTPOLL_EVT_WRITE, RTFUZZOBS_EXEC_CTX_POLL_ID_STDIN);
                                    AssertRC(rc);
                                }
                            }
                            else
                            {
                                pExecCtx->StdinHandle.enmType = RTHANDLETYPE_PIPE;
                                pExecCtx->StdinHandle.u.hPipe = NIL_RTPIPE;
                            }

                            if (RT_SUCCESS(rc))
                            {
                                pExecCtx->StdoutHandle.enmType = RTHANDLETYPE_PIPE;
                                pExecCtx->StdoutHandle.u.hPipe = pExecCtx->hPipeStdoutW;
                                pExecCtx->StderrHandle.enmType = RTHANDLETYPE_PIPE;
                                pExecCtx->StderrHandle.u.hPipe = pExecCtx->hPipeStderrW;
                                *ppExecCtx = pExecCtx;
                                return VINF_SUCCESS;
                            }

                            RTPipeClose(pExecCtx->hPipeStderrR);
                            RTPipeClose(pExecCtx->hPipeStderrW);
                        }

                        RTPipeClose(pExecCtx->hPipeStdoutR);
                        RTPipeClose(pExecCtx->hPipeStdoutW);
                    }

                    RTPollSetDestroy(pExecCtx->hPollSet);
                }

                RTFuzzTgtStateRelease(pExecCtx->hTgtState);
            }

            RTEnvDestroy(pExecCtx->hEnv);
        }

        RTMemFree(pExecCtx);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Destroys the given execution context.
 *
 * @param   pThis               The internal fuzzing observer state.
 * @param   pExecCtx            The execution context to destroy.
 */
static void rtFuzzObsExecCtxDestroy(PRTFUZZOBSINT pThis, PRTFUZZOBSEXECCTX pExecCtx)
{
    RTPipeClose(pExecCtx->hPipeStdoutR);
    RTPipeClose(pExecCtx->hPipeStdoutW);
    RTPipeClose(pExecCtx->hPipeStderrR);
    RTPipeClose(pExecCtx->hPipeStderrW);

    if (   pThis->enmInputChan == RTFUZZOBSINPUTCHAN_STDIN
        || pThis->enmInputChan == RTFUZZOBSINPUTCHAN_FUZZING_AWARE_CLIENT)
    {
        RTPipeClose(pExecCtx->hPipeStdinR);
        RTPipeClose(pExecCtx->hPipeStdinW);
    }

    RTPollSetDestroy(pExecCtx->hPollSet);
    char **ppszArg = &pExecCtx->apszArgs[0];
    while (*ppszArg != NULL)
    {
        RTStrFree(*ppszArg);
        ppszArg++;
    }

    if (pExecCtx->hTgtState != NIL_RTFUZZTGTSTATE)
        RTFuzzTgtStateRelease(pExecCtx->hTgtState);
    RTEnvDestroy(pExecCtx->hEnv);
    RTMemFree(pExecCtx);
}


/**
 * Runs the client binary pumping all data back and forth waiting for the client to finish.
 *
 * @returns IPRT status code.
 * @retval  VERR_TIMEOUT if the client didn't finish in the given deadline and was killed.
 * @param   pThis               The internal fuzzing observer state.
 * @param   pExecCtx            The execution context.
 * @param   pProcStat           Where to store the process exit status on success.
 */
static int rtFuzzObsExecCtxClientRun(PRTFUZZOBSINT pThis, PRTFUZZOBSEXECCTX pExecCtx, PRTPROCSTATUS pProcStat)
{
    int rc = RTProcCreateEx(pThis->pszBinary, &pExecCtx->apszArgs[0], pExecCtx->hEnv, 0 /*fFlags*/, &pExecCtx->StdinHandle,
                            &pExecCtx->StdoutHandle, &pExecCtx->StderrHandle, NULL, NULL, NULL, &pExecCtx->hProc);
    if (RT_SUCCESS(rc))
    {
        uint64_t tsMilliesStart = RTTimeSystemMilliTS();
        for (;;)
        {
            /* Wait a bit for something to happen on one of the pipes. */
            uint32_t fEvtsRecv = 0;
            uint32_t idEvt = 0;
            rc = RTPoll(pExecCtx->hPollSet, 10 /*cMillies*/, &fEvtsRecv, &idEvt);
            if (RT_SUCCESS(rc))
            {
                if (idEvt == RTFUZZOBS_EXEC_CTX_POLL_ID_STDOUT)
                {
                    Assert(fEvtsRecv & RTPOLL_EVT_READ);
                    rc = RTFuzzTgtStateAppendStdoutFromPipe(pExecCtx->hTgtState, pExecCtx->hPipeStdoutR);
                    AssertRC(rc);
                }
                else if (idEvt == RTFUZZOBS_EXEC_CTX_POLL_ID_STDERR)
                {
                    Assert(fEvtsRecv & RTPOLL_EVT_READ);

                    rc = RTFuzzTgtStateAppendStderrFromPipe(pExecCtx->hTgtState, pExecCtx->hPipeStderrR);
                    AssertRC(rc);
                }
                else if (idEvt == RTFUZZOBS_EXEC_CTX_POLL_ID_STDIN)
                {
                    /* Feed the next input. */
                    Assert(fEvtsRecv & RTPOLL_EVT_WRITE);
                    size_t cbWritten = 0;
                    rc = RTPipeWrite(pExecCtx->hPipeStdinW, pExecCtx->pbInputCur, pExecCtx->cbInputLeft, &cbWritten);
                    if (RT_SUCCESS(rc))
                    {
                        pExecCtx->cbInputLeft -= cbWritten;
                        if (!pExecCtx->cbInputLeft)
                        {
                            /* Close stdin pipe. */
                            rc = RTPollSetRemove(pExecCtx->hPollSet, RTFUZZOBS_EXEC_CTX_POLL_ID_STDIN);
                            AssertRC(rc);
                            RTPipeClose(pExecCtx->hPipeStdinW);
                        }
                    }
                }
                else
                    AssertMsgFailed(("Invalid poll ID returned: %u!\n", idEvt));
            }
            else
                Assert(rc == VERR_TIMEOUT);

            /* Check the process status. */
            rc = RTProcWait(pExecCtx->hProc, RTPROCWAIT_FLAGS_NOBLOCK, pProcStat);
            if (RT_SUCCESS(rc))
            {
                /* Add the coverage report to the sanitizer if enabled. */
                if (pThis->fSanitizers & RTFUZZOBS_SANITIZER_F_SANCOV)
                {
                    char szSanCovReport[RTPATH_MAX];
                    ssize_t cch = RTStrPrintf2(&szSanCovReport[0], sizeof(szSanCovReport),
                                               "%s%c%s.%u.sancov",
                                               pThis->pszTmpDir, RTPATH_SLASH,
                                               pThis->pszBinaryFilename, pExecCtx->hProc);
                    Assert(cch > 0); RT_NOREF(cch);
                    rc = RTFuzzTgtStateAddSanCovReportFromFile(pExecCtx->hTgtState, &szSanCovReport[0]);
                    RTFileDelete(&szSanCovReport[0]);
                }
                break;
            }
            else
            {
                Assert(rc == VERR_PROCESS_RUNNING);
                /* Check whether we reached the limit. */
                if (RTTimeSystemMilliTS() - tsMilliesStart > pThis->msWaitMax)
                {
                    rc = VERR_TIMEOUT;
                    break;
                }
            }
        } /* for (;;) */

        /* Kill the process on a timeout. */
        if (rc == VERR_TIMEOUT)
        {
            int rc2 = RTProcTerminate(pExecCtx->hProc);
            AssertRC(rc2);
        }
    }

    return rc;
}


/**
 * Runs the fuzzing aware client binary pumping all data back and forth waiting for the client to crash.
 *
 * @returns IPRT status code.
 * @retval  VERR_TIMEOUT if the client didn't finish in the given deadline and was killed.
 * @param   pThis               The internal fuzzing observer state.
 * @param   pExecCtx            The execution context.
 * @param   pProcStat           Where to store the process exit status on success.
 */
static int rtFuzzObsExecCtxClientRunFuzzingAware(PRTFUZZOBSINT pThis, PRTFUZZOBSEXECCTX pExecCtx, PRTPROCSTATUS pProcStat)
{
    int rc = RTProcCreateEx(pThis->pszBinary, &pExecCtx->apszArgs[0], pExecCtx->hEnv, 0 /*fFlags*/, &pExecCtx->StdinHandle,
                            &pExecCtx->StdoutHandle, &pExecCtx->StderrHandle, NULL, NULL, NULL, &pExecCtx->hProc);
    if (RT_SUCCESS(rc))
    {
        /* Send the initial fuzzing context state over to the client. */
        void *pvState = NULL;
        size_t cbState = 0;
        rc = RTFuzzCtxStateExportToMem(pThis->hFuzzCtx, &pvState, &cbState);
        if (RT_SUCCESS(rc))
        {
            uint32_t cbStateWr = (uint32_t)cbState;
            rc = RTPipeWriteBlocking(pExecCtx->hPipeStdinW, &cbStateWr, sizeof(cbStateWr), NULL);
            if (RT_SUCCESS(rc))
                rc = RTPipeWriteBlocking(pExecCtx->hPipeStdinW, pvState, cbState, NULL);
            if (RT_SUCCESS(rc))
            {
                rc = RTPollSetRemove(pExecCtx->hPollSet, RTFUZZOBS_EXEC_CTX_POLL_ID_STDIN);
                AssertRC(rc);

                uint64_t tsMilliesLastSignal = RTTimeSystemMilliTS();
                uint32_t cFuzzedInputs = 0;
                for (;;)
                {
                    /* Wait a bit for something to happen on one of the pipes. */
                    uint32_t fEvtsRecv = 0;
                    uint32_t idEvt = 0;
                    rc = RTPoll(pExecCtx->hPollSet, 10 /*cMillies*/, &fEvtsRecv, &idEvt);
                    if (RT_SUCCESS(rc))
                    {
                        if (idEvt == RTFUZZOBS_EXEC_CTX_POLL_ID_STDOUT)
                        {
                            Assert(fEvtsRecv & RTPOLL_EVT_READ);
                            for (;;)
                            {
                                char achBuf[512];
                                size_t cbRead = 0;
                                rc = RTPipeRead(pExecCtx->hPipeStdoutR, &achBuf[0], sizeof(achBuf), &cbRead);
                                if (RT_SUCCESS(rc))
                                {
                                    if (!cbRead)
                                        break;

                                    tsMilliesLastSignal = RTTimeMilliTS();
                                    for (unsigned i = 0; i < cbRead; i++)
                                    {
                                        ASMAtomicIncU32(&pThis->Stats.cFuzzedInputs);
                                        ASMAtomicIncU32(&pThis->Stats.cFuzzedInputsPerSec);

                                        if (achBuf[i] == '.')
                                            cFuzzedInputs++;
                                        else if (achBuf[i] == 'A')
                                        {
                                            while (cFuzzedInputs--)
                                            {
                                                RTFUZZINPUT hFuzzInput;
                                                rc = RTFuzzCtxInputGenerate(pThis->hFuzzCtx, &hFuzzInput);
                                                if (RT_SUCCESS(rc))
                                                    RTFuzzInputRelease(hFuzzInput);
                                            }

                                            /* We should be at the input generated by the client now. */
                                            RTFUZZINPUT hFuzzInput;
                                            rc = RTFuzzCtxInputGenerate(pThis->hFuzzCtx, &hFuzzInput);
                                            if (RT_SUCCESS(rc))
                                                rc = RTFuzzInputAddToCtxCorpus(hFuzzInput);
                                        }
                                    }
                                }
                                else
                                    break;
                            }
                            AssertRC(rc);
                        }
                        else if (idEvt == RTFUZZOBS_EXEC_CTX_POLL_ID_STDERR)
                        {
                            Assert(fEvtsRecv & RTPOLL_EVT_READ);
                            rc = RTFuzzTgtStateAppendStderrFromPipe(pExecCtx->hTgtState, pExecCtx->hPipeStderrR);
                            AssertRC(rc);
                        }
                        else
                            AssertMsgFailed(("Invalid poll ID returned: %u!\n", idEvt));
                    }
                    else
                        Assert(rc == VERR_TIMEOUT);

                    /* Check the process status. */
                    rc = RTProcWait(pExecCtx->hProc, RTPROCWAIT_FLAGS_NOBLOCK, pProcStat);
                    if (RT_SUCCESS(rc))
                        break;
                    else
                    {
                        Assert(rc == VERR_PROCESS_RUNNING);
                        /* Check when the last response from the client was. */
                        if (RTTimeSystemMilliTS() - tsMilliesLastSignal > pThis->msWaitMax)
                        {
                            rc = VERR_TIMEOUT;
                            break;
                        }
                    }
                } /* for (;;) */

                /* Kill the process on a timeout. */
                if (rc == VERR_TIMEOUT)
                {
                    int rc2 = RTProcTerminate(pExecCtx->hProc);
                    AssertRC(rc2);
                }
            }
        }
    }

    RTHANDLE Handle;
    Handle.enmType = RTHANDLETYPE_PIPE;
    Handle.u.hPipe = pExecCtx->hPipeStdinW;
    rc = RTPollSetAdd(pExecCtx->hPollSet, &Handle, RTPOLL_EVT_WRITE, RTFUZZOBS_EXEC_CTX_POLL_ID_STDIN);
    AssertRC(rc);

    return rc;
}


/**
 * Adds the input to the results directory.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal fuzzing observer state.
 * @param   hFuzzInput          Fuzzing input handle to write.
 * @param   pExecCtx            Execution context.
 */
static int rtFuzzObsAddInputToResults(PRTFUZZOBSINT pThis, RTFUZZINPUT hFuzzInput, PRTFUZZOBSEXECCTX pExecCtx)
{
    char aszDigest[RTMD5_STRING_LEN + 1];
    int rc = RTFuzzInputQueryDigestString(hFuzzInput, &aszDigest[0], sizeof(aszDigest));
    if (RT_SUCCESS(rc))
    {
        /* Create a directory. */
        char szPath[RTPATH_MAX];
        rc = RTPathJoin(szPath, sizeof(szPath), pThis->pszResultsDir, &aszDigest[0]);
        AssertRC(rc);

        rc = RTDirCreate(&szPath[0], 0700, 0 /*fCreate*/);
        if (RT_SUCCESS(rc))
        {
            /* Write the input. */
            char szTmp[RTPATH_MAX];
            rc = RTPathJoin(szTmp, sizeof(szTmp), &szPath[0], "input");
            AssertRC(rc);

            rc = RTFuzzInputWriteToFile(hFuzzInput, &szTmp[0]);
            if (RT_SUCCESS(rc))
                rc = RTFuzzTgtStateDumpToDir(pExecCtx->hTgtState, &szPath[0]);
        }
    }

    return rc;
}


/**
 * Fuzzing observer worker loop.
 *
 * @returns IPRT status code.
 * @param   hThrd               The thread handle.
 * @param   pvUser              Opaque user data.
 */
static DECLCALLBACK(int) rtFuzzObsWorkerLoop(RTTHREAD hThrd, void *pvUser)
{
    PRTFUZZOBSTHRD pObsThrd = (PRTFUZZOBSTHRD)pvUser;
    PRTFUZZOBSINT pThis = pObsThrd->pFuzzObs;
    PRTFUZZOBSEXECCTX pExecCtx = NULL;

    int rc = rtFuzzObsExecCtxCreate(&pExecCtx, pThis);
    if (RT_FAILURE(rc))
        return rc;

    char szInput[RTPATH_MAX]; RT_ZERO(szInput);
    if (pThis->enmInputChan == RTFUZZOBSINPUTCHAN_FILE)
    {
        char szFilename[32];

        ssize_t cbBuf = RTStrPrintf2(&szFilename[0], sizeof(szFilename), "%u", pObsThrd->idObs);
        Assert(cbBuf > 0); RT_NOREF(cbBuf);

        rc = RTPathJoin(szInput, sizeof(szInput), pThis->pszTmpDir, &szFilename[0]);
        AssertRC(rc);

        RTFUZZOBSVARIABLE aVar[2] =
        {
            { "${INPUT}", sizeof("${INPUT}") - 1, &szInput[0] },
            { NULL,       0,                      NULL        }
        };
        rc = rtFuzzObsExecCtxArgvPrepare(pThis, pExecCtx, &aVar[0]);
        if (RT_FAILURE(rc))
            return rc;
    }

    while (!pObsThrd->fShutdown)
    {
        /* Wait for work. */
        if (!ASMAtomicReadU32(&pObsThrd->cInputs))
        {
            rc = RTThreadUserWait(hThrd, RT_INDEFINITE_WAIT);
            AssertRC(rc);
        }

        if (pObsThrd->fShutdown)
            break;

        if (!ASMAtomicReadU32(&pObsThrd->cInputs))
            continue;

        uint32_t offRead = ASMAtomicReadU32(&pObsThrd->offQueueInputR);
        RTFUZZINPUT hFuzzInput = pObsThrd->ahQueueInput[offRead];

        ASMAtomicDecU32(&pObsThrd->cInputs);
        offRead = (offRead + 1) % RT_ELEMENTS(pObsThrd->ahQueueInput);
        ASMAtomicWriteU32(&pObsThrd->offQueueInputR, offRead);
        if (!ASMAtomicBitTestAndSet(&pThis->bmEvt, pObsThrd->idObs))
            RTSemEventSignal(pThis->hEvtGlobal);

        if (pThis->enmInputChan == RTFUZZOBSINPUTCHAN_FILE)
            rc = RTFuzzInputWriteToFile(hFuzzInput, &szInput[0]);
        else if (pThis->enmInputChan == RTFUZZOBSINPUTCHAN_STDIN)
        {
            rc = RTFuzzInputQueryBlobData(hFuzzInput, (void **)&pExecCtx->pbInputCur, &pExecCtx->cbInputLeft);
            if (RT_SUCCESS(rc))
                rc = rtFuzzObsExecCtxArgvPrepare(pThis, pExecCtx, NULL);
        }

        if (RT_SUCCESS(rc))
        {
            RTPROCSTATUS ProcSts;
            if (pThis->enmInputChan == RTFUZZOBSINPUTCHAN_FUZZING_AWARE_CLIENT)
                rc = rtFuzzObsExecCtxClientRunFuzzingAware(pThis, pExecCtx, &ProcSts);
            else
            {
                rc = rtFuzzObsExecCtxClientRun(pThis, pExecCtx, &ProcSts);
                ASMAtomicIncU32(&pThis->Stats.cFuzzedInputs);
                ASMAtomicIncU32(&pThis->Stats.cFuzzedInputsPerSec);
            }

            if (RT_SUCCESS(rc))
            {
                rc = RTFuzzTgtStateAddProcSts(pExecCtx->hTgtState, &ProcSts);
                AssertRC(rc);

                if (ProcSts.enmReason != RTPROCEXITREASON_NORMAL)
                {
                    ASMAtomicIncU32(&pThis->Stats.cFuzzedInputsCrash);
                    rc = rtFuzzObsAddInputToResults(pThis, hFuzzInput, pExecCtx);
                }
            }
            else if (rc == VERR_TIMEOUT)
            {
                ASMAtomicIncU32(&pThis->Stats.cFuzzedInputsHang);
                rc = rtFuzzObsAddInputToResults(pThis, hFuzzInput, pExecCtx);
            }
            else
                AssertFailed();

            if (RT_SUCCESS(rc))
            {
                /*
                 * Check whether we reached an unknown target state and add the input to the
                 * corpus in that case.
                 */
                rc = RTFuzzTgtStateAddToRecorder(pExecCtx->hTgtState);
                if (RT_SUCCESS(rc))
                {
                    /* Add to corpus and create a new target state for the next run. */
                    RTFuzzInputAddToCtxCorpus(hFuzzInput);
                    RTFuzzTgtStateRelease(pExecCtx->hTgtState);
                    pExecCtx->hTgtState = NIL_RTFUZZTGTSTATE;
                    rc = RTFuzzTgtRecorderCreateNewState(pThis->hTgtRec, &pExecCtx->hTgtState);
                    AssertRC(rc);
                }
                else
                {
                    Assert(rc == VERR_ALREADY_EXISTS);
                    /* Reset the state for the next run. */
                    rc = RTFuzzTgtStateReset(pExecCtx->hTgtState);
                    AssertRC(rc);
                }
            }
            RTFuzzInputRelease(hFuzzInput);

            if (pThis->enmInputChan == RTFUZZOBSINPUTCHAN_FILE)
                RTFileDelete(&szInput[0]);
        }
    }

    rtFuzzObsExecCtxDestroy(pThis, pExecCtx);
    return VINF_SUCCESS;
}


/**
 * Fills the input queue of the given observer thread until it is full.
 *
 * @returns IPRT status code.
 * @param   pThis               Pointer to the observer instance data.
 * @param   pObsThrd            The observer thread instance to fill.
 */
static int rtFuzzObsMasterInputQueueFill(PRTFUZZOBSINT pThis, PRTFUZZOBSTHRD pObsThrd)
{
    int rc = VINF_SUCCESS;
    uint32_t cInputsAdded = 0;
    uint32_t cInputsAdd = RTFUZZOBS_THREAD_INPUT_QUEUE_MAX - ASMAtomicReadU32(&pObsThrd->cInputs);
    uint32_t offW = ASMAtomicReadU32(&pObsThrd->offQueueInputW);

    while (   cInputsAdded < cInputsAdd
           && RT_SUCCESS(rc))
    {
        RTFUZZINPUT hFuzzInput = NIL_RTFUZZINPUT;
        rc = RTFuzzCtxInputGenerate(pThis->hFuzzCtx, &hFuzzInput);
        if (RT_SUCCESS(rc))
        {
            pObsThrd->ahQueueInput[offW] = hFuzzInput;
            offW = (offW + 1) % RTFUZZOBS_THREAD_INPUT_QUEUE_MAX;
            cInputsAdded++;
        }
    }

    ASMAtomicWriteU32(&pObsThrd->offQueueInputW, offW);
    ASMAtomicAddU32(&pObsThrd->cInputs, cInputsAdded);

    return rc;
}


/**
 * Fuzzing observer master worker loop.
 *
 * @returns IPRT status code.
 * @param   hThread             The thread handle.
 * @param   pvUser              Opaque user data.
 */
static DECLCALLBACK(int) rtFuzzObsMasterLoop(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    int rc = VINF_SUCCESS;
    PRTFUZZOBSINT pThis = (PRTFUZZOBSINT)pvUser;

    RTThreadUserSignal(hThread);

    while (   !pThis->fShutdown
           && RT_SUCCESS(rc))
    {
        uint64_t bmEvt = ASMAtomicXchgU64(&pThis->bmEvt, 0);
        uint32_t idxObs = 0;
        while (bmEvt != 0)
        {
            if (bmEvt & 0x1)
            {
                /* Create a new input for this observer and kick it. */
                PRTFUZZOBSTHRD pObsThrd = &pThis->paObsThreads[idxObs];

                rc = rtFuzzObsMasterInputQueueFill(pThis, pObsThrd);
                if (RT_SUCCESS(rc))
                    RTThreadUserSignal(pObsThrd->hThread);
            }

            idxObs++;
            bmEvt >>= 1;
        }

        rc = RTSemEventWait(pThis->hEvtGlobal, RT_INDEFINITE_WAIT);
    }

    return VINF_SUCCESS;
}


/**
 * Initializes the given worker thread structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal fuzzing observer state.
 * @param   iObs                Observer ID.
 * @param   pObsThrd            The observer thread structure.
 */
static int rtFuzzObsWorkerThreadInit(PRTFUZZOBSINT pThis, uint32_t idObs, PRTFUZZOBSTHRD pObsThrd)
{
    pObsThrd->pFuzzObs       = pThis;
    pObsThrd->idObs          = idObs;
    pObsThrd->fShutdown      = false;
    pObsThrd->cInputs        = 0;
    pObsThrd->offQueueInputW = 0;
    pObsThrd->offQueueInputR = 0;

    ASMAtomicBitSet(&pThis->bmEvt, idObs);
    return RTThreadCreate(&pObsThrd->hThread, rtFuzzObsWorkerLoop, pObsThrd, 0, RTTHREADTYPE_IO,
                          RTTHREADFLAGS_WAITABLE, "Fuzz-Worker");
}


/**
 * Creates the given amount of worker threads and puts them into waiting state.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal fuzzing observer state.
 * @param   cThreads            Number of worker threads to create.
 */
static int rtFuzzObsWorkersCreate(PRTFUZZOBSINT pThis, uint32_t cThreads)
{
    int rc = VINF_SUCCESS;
    PRTFUZZOBSTHRD paObsThreads = (PRTFUZZOBSTHRD)RTMemAllocZ(cThreads * sizeof(RTFUZZOBSTHRD));
    if (RT_LIKELY(paObsThreads))
    {
        for (unsigned i = 0; i < cThreads && RT_SUCCESS(rc); i++)
        {
            rc = rtFuzzObsWorkerThreadInit(pThis, i, &paObsThreads[i]);
            if (RT_FAILURE(rc))
            {
                /* Rollback. */

            }
        }

        if (RT_SUCCESS(rc))
        {
            pThis->paObsThreads = paObsThreads;
            pThis->cThreads     = cThreads;
        }
        else
            RTMemFree(paObsThreads);
    }

    return rc;
}


/**
 * Creates the global worker thread managing the input creation and other worker threads.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal fuzzing observer state.
 */
static int rtFuzzObsMasterCreate(PRTFUZZOBSINT pThis)
{
    pThis->fShutdown = false;

    int rc = RTSemEventCreate(&pThis->hEvtGlobal);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate(&pThis->hThreadGlobal, rtFuzzObsMasterLoop, pThis, 0, RTTHREADTYPE_IO,
                            RTTHREADFLAGS_WAITABLE, "Fuzz-Master");
        if (RT_SUCCESS(rc))
        {
            RTThreadUserWait(pThis->hThreadGlobal, RT_INDEFINITE_WAIT);
        }
        else
        {
            RTSemEventDestroy(pThis->hEvtGlobal);
            pThis->hEvtGlobal = NIL_RTSEMEVENT;
        }
    }

    return rc;
}


/**
 * Sets up any configured sanitizers to cooperate with the observer.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal fuzzing observer state.
 */
static int rtFuzzObsSetupSanitizerCfg(PRTFUZZOBSINT pThis)
{
    int rc = VINF_SUCCESS;
    bool fSep = false;

    if (pThis->fSanitizers & RTFUZZOBS_SANITIZER_F_ASAN)
    {
        /*
         * Need to set abort_on_error=1 in ASAN_OPTIONS or
         * the sanitizer will call exit() instead of abort() and we
         * don't catch invalid memory accesses.
         */
        rc = RTStrAAppend(&pThis->pszSanitizerOpts, "abort_on_error=1");
        fSep = true;
    }

    if (   RT_SUCCESS(rc)
        && (pThis->fSanitizers & RTFUZZOBS_SANITIZER_F_SANCOV))
    {
        /*
         * The coverage sanitizer will dump coverage information into a file
         * on process exit. Need to configure the directory where to dump it.
         */
        char aszSanCovCfg[_4K];
        ssize_t cch = RTStrPrintf2(&aszSanCovCfg[0], sizeof(aszSanCovCfg),
                                   "%scoverage=1:coverage_dir=%s",
                                   fSep ? ":" : "", pThis->pszTmpDir);
        if (cch > 0)
            rc = RTStrAAppend(&pThis->pszSanitizerOpts, &aszSanCovCfg[0]);
        else
            rc = VERR_BUFFER_OVERFLOW;
        /*fSep = true; Not used below currently. */
    }

    if (   RT_SUCCESS(rc)
        && pThis->pszSanitizerOpts)
    {
        /* Add it to the environment. */
        if (pThis->hEnv == RTENV_DEFAULT)
        {
            /* Clone the environment to keep the default one untouched. */
            rc = RTEnvClone(&pThis->hEnv, RTENV_DEFAULT);
        }
        if (RT_SUCCESS(rc))
            rc = RTEnvSetEx(pThis->hEnv, "ASAN_OPTIONS", pThis->pszSanitizerOpts);
    }

    return rc;
}


RTDECL(int) RTFuzzObsCreate(PRTFUZZOBS phFuzzObs, RTFUZZCTXTYPE enmType, uint32_t fTgtRecFlags)
{
    AssertPtrReturn(phFuzzObs, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    PRTFUZZOBSINT pThis = (PRTFUZZOBSINT)RTMemAllocZ(sizeof(*pThis));
    if (RT_LIKELY(pThis))
    {
        pThis->pszBinary         = NULL;
        pThis->pszBinaryFilename = NULL;
        pThis->papszArgs         = NULL;
        pThis->hEnv              = RTENV_DEFAULT;
        pThis->msWaitMax         = 1000;
        pThis->hThreadGlobal     = NIL_RTTHREAD;
        pThis->hEvtGlobal        = NIL_RTSEMEVENT;
        pThis->bmEvt             = 0;
        pThis->cThreads          = 0;
        pThis->paObsThreads      = NULL;
        pThis->tsLastStats       = RTTimeMilliTS();
        pThis->Stats.cFuzzedInputsPerSec = 0;
        pThis->Stats.cFuzzedInputs       = 0;
        pThis->Stats.cFuzzedInputsHang   = 0;
        pThis->Stats.cFuzzedInputsCrash  = 0;
        rc = RTFuzzCtxCreate(&pThis->hFuzzCtx, enmType);
        if (RT_SUCCESS(rc))
        {
            rc = RTFuzzTgtRecorderCreate(&pThis->hTgtRec, fTgtRecFlags);
            if (RT_SUCCESS(rc))
            {
                *phFuzzObs = pThis;
                return VINF_SUCCESS;
            }
            RTFuzzCtxRelease(pThis->hFuzzCtx);
        }

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int) RTFuzzObsDestroy(RTFUZZOBS hFuzzObs)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    RTFuzzObsExecStop(hFuzzObs);

    /* Clean up all acquired resources. */
    for (unsigned i = 0; i < pThis->cArgs; i++)
        RTStrFree(pThis->papszArgs[i]);

    RTMemFree(pThis->papszArgs);

    if (pThis->hEvtGlobal != NIL_RTSEMEVENT)
        RTSemEventDestroy(pThis->hEvtGlobal);

    if (pThis->pszResultsDir)
        RTStrFree(pThis->pszResultsDir);
    if (pThis->pszTmpDir)
        RTStrFree(pThis->pszTmpDir);
    if (pThis->pszBinary)
        RTStrFree(pThis->pszBinary);
    if (pThis->pszSanitizerOpts)
        RTStrFree(pThis->pszSanitizerOpts);
    if (pThis->hEnv != RTENV_DEFAULT)
    {
        RTEnvDestroy(pThis->hEnv);
        pThis->hEnv = RTENV_DEFAULT;
    }
    RTFuzzTgtRecorderRelease(pThis->hTgtRec);
    RTFuzzCtxRelease(pThis->hFuzzCtx);
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int) RTFuzzObsQueryCtx(RTFUZZOBS hFuzzObs, PRTFUZZCTX phFuzzCtx)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(phFuzzCtx, VERR_INVALID_POINTER);

    RTFuzzCtxRetain(pThis->hFuzzCtx);
    *phFuzzCtx = pThis->hFuzzCtx;
    return VINF_SUCCESS;
}


RTDECL(int) RTFuzzObsQueryStats(RTFUZZOBS hFuzzObs, PRTFUZZOBSSTATS pStats)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pStats, VERR_INVALID_POINTER);

    uint64_t tsStatsQuery = RTTimeMilliTS();
    uint32_t cFuzzedInputsPerSec = ASMAtomicXchgU32(&pThis->Stats.cFuzzedInputsPerSec, 0);

    pStats->cFuzzedInputsCrash  = ASMAtomicReadU32(&pThis->Stats.cFuzzedInputsCrash);
    pStats->cFuzzedInputsHang   = ASMAtomicReadU32(&pThis->Stats.cFuzzedInputsHang);
    pStats->cFuzzedInputs       = ASMAtomicReadU32(&pThis->Stats.cFuzzedInputs);
    uint64_t cPeriodSec = (tsStatsQuery - pThis->tsLastStats) / 1000;
    if (cPeriodSec)
    {
        pStats->cFuzzedInputsPerSec    = cFuzzedInputsPerSec / cPeriodSec;
        pThis->cFuzzedInputsPerSecLast = pStats->cFuzzedInputsPerSec;
        pThis->tsLastStats             = tsStatsQuery;
    }
    else
        pStats->cFuzzedInputsPerSec = pThis->cFuzzedInputsPerSecLast;
    return VINF_SUCCESS;
}


RTDECL(int) RTFuzzObsSetTmpDirectory(RTFUZZOBS hFuzzObs, const char *pszTmp)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszTmp, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    pThis->pszTmpDir = RTStrDup(pszTmp);
    if (!pThis->pszTmpDir)
        rc = VERR_NO_STR_MEMORY;
    return rc;
}


RTDECL(int) RTFuzzObsSetResultDirectory(RTFUZZOBS hFuzzObs, const char *pszResults)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszResults, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    pThis->pszResultsDir = RTStrDup(pszResults);
    if (!pThis->pszResultsDir)
        rc = VERR_NO_STR_MEMORY;
    return rc;
}


RTDECL(int) RTFuzzObsSetTestBinary(RTFUZZOBS hFuzzObs, const char *pszBinary, RTFUZZOBSINPUTCHAN enmInputChan)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszBinary, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    pThis->enmInputChan = enmInputChan;
    pThis->pszBinary    = RTStrDup(pszBinary);
    if (RT_UNLIKELY(!pThis->pszBinary))
        rc = VERR_NO_STR_MEMORY;
    else
        pThis->pszBinaryFilename = RTPathFilename(pThis->pszBinary);
    return rc;
}


RTDECL(int) RTFuzzObsSetTestBinaryArgs(RTFUZZOBS hFuzzObs, const char * const *papszArgs, unsigned cArgs)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    char **papszArgsOld = pThis->papszArgs;
    if (papszArgs)
    {
        pThis->papszArgs = (char **)RTMemAllocZ(sizeof(char **) * (cArgs + 1));
        if (RT_LIKELY(pThis->papszArgs))
        {
            for (unsigned i = 0; i < cArgs; i++)
            {
                pThis->papszArgs[i] = RTStrDup(papszArgs[i]);
                if (RT_UNLIKELY(!pThis->papszArgs[i]))
                {
                    while (i > 0)
                    {
                        i--;
                        RTStrFree(pThis->papszArgs[i]);
                    }
                    break;
                }
            }

            if (RT_FAILURE(rc))
                RTMemFree(pThis->papszArgs);
        }
        else
            rc = VERR_NO_MEMORY;

        if (RT_FAILURE(rc))
            pThis->papszArgs = papszArgsOld;
        else
            pThis->cArgs = cArgs;
    }
    else
    {
        pThis->papszArgs = NULL;
        pThis->cArgs = 0;
        if (papszArgsOld)
        {
            char **ppsz = papszArgsOld;
            while (*ppsz != NULL)
            {
                RTStrFree(*ppsz);
                ppsz++;
            }
            RTMemFree(papszArgsOld);
        }
    }

    return rc;
}


RTDECL(int) RTFuzzObsSetTestBinaryEnv(RTFUZZOBS hFuzzObs, RTENV hEnv)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    pThis->hEnv = hEnv;
    return VINF_SUCCESS;
}


RTDECL(int) RTFuzzObsSetTestBinarySanitizers(RTFUZZOBS hFuzzObs, uint32_t fSanitizers)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    pThis->fSanitizers = fSanitizers;
    return VINF_SUCCESS;
}


RTDECL(int) RTFuzzObsSetTestBinaryTimeout(RTFUZZOBS hFuzzObs, RTMSINTERVAL msTimeoutMax)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    pThis->msWaitMax = msTimeoutMax;
    return VINF_SUCCESS;
}


RTDECL(int) RTFuzzObsExecStart(RTFUZZOBS hFuzzObs, uint32_t cProcs)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(cProcs <= sizeof(uint64_t) * 8, VERR_INVALID_PARAMETER);
    AssertReturn(   pThis->enmInputChan == RTFUZZOBSINPUTCHAN_FILE
                 || pThis->pszTmpDir != NULL,
                 VERR_INVALID_STATE);

    int rc = VINF_SUCCESS;
    if (!cProcs)
    {
        cProcs = RTMpGetPresentCoreCount();
        cProcs = RT_MIN(cProcs, sizeof(uint64_t) * 8);
    }

    rc = rtFuzzObsSetupSanitizerCfg(pThis);
    if (RT_SUCCESS(rc))
    {
        /* Spin up the worker threads first. */
        rc = rtFuzzObsWorkersCreate(pThis, cProcs);
        if (RT_SUCCESS(rc))
        {
            /* Spin up the global thread. */
            rc = rtFuzzObsMasterCreate(pThis);
        }
    }

    return rc;
}


RTDECL(int) RTFuzzObsExecStop(RTFUZZOBS hFuzzObs)
{
    PRTFUZZOBSINT pThis = hFuzzObs;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    /* Wait for the master thread to terminate. */
    if (pThis->hThreadGlobal != NIL_RTTHREAD)
    {
        ASMAtomicXchgBool(&pThis->fShutdown, true);
        RTSemEventSignal(pThis->hEvtGlobal);
        RTThreadWait(pThis->hThreadGlobal, RT_INDEFINITE_WAIT, NULL);
        pThis->hThreadGlobal = NIL_RTTHREAD;
    }

    /* Destroy the workers. */
    if (pThis->paObsThreads)
    {
        for (unsigned i = 0; i < pThis->cThreads; i++)
        {
            PRTFUZZOBSTHRD pThrd = &pThis->paObsThreads[i];
            ASMAtomicXchgBool(&pThrd->fShutdown, true);
            RTThreadUserSignal(pThrd->hThread);
            RTThreadWait(pThrd->hThread, RT_INDEFINITE_WAIT, NULL);
        }

        RTMemFree(pThis->paObsThreads);
        pThis->paObsThreads = NULL;
        pThis->cThreads     = 0;
    }

    RTSemEventDestroy(pThis->hEvtGlobal);
    pThis->hEvtGlobal = NIL_RTSEMEVENT;
    return VINF_SUCCESS;
}

