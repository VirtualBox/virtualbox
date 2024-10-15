/* $Id: pipe-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Anonymous Pipes, Windows Implementation.
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
#include <iprt/nt/nt-and-windows.h>

#include <iprt/pipe.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/poll.h>
#include <iprt/process.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include "internal/pipe.h"
#include "internal/magics.h"
#include "internal-r3-win.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The pipe buffer size we prefer. */
#define RTPIPE_NT_SIZE      _64K


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTPIPEINTERNAL
{
    /** Magic value (RTPIPE_MAGIC). */
    uint32_t            u32Magic;
    /** The pipe handle. */
    HANDLE              hPipe;
    /** Set if this is the read end, clear if it's the write end. */
    bool                fRead;
    /** RTPipeFromNative: Leave native handle open on RTPipeClose. */
    bool                fLeaveOpen;
    /** Set if there is already pending I/O. */
    bool                fIOPending;
    /** Set if the zero byte read that the poll code using is pending. */
    bool                fZeroByteRead;
    /** Set if the pipe is broken. */
    bool                fBrokenPipe;
    /** Set if we've promised that the handle is writable. */
    bool                fPromisedWritable;
    /** Set if created inheritable. */
    bool                fCreatedInheritable;
    /** Usage counter. */
    uint32_t            cUsers;
    /** The overlapped I/O structure we use. */
    OVERLAPPED          Overlapped;
    /** Bounce buffer for writes. */
    uint8_t            *pbBounceBuf;
    /** Amount of used buffer space. */
    size_t              cbBounceBufUsed;
    /** Amount of allocated buffer space. */
    size_t              cbBounceBufAlloc;
    /** The handle of the poll set currently polling on this pipe.
     *  We can only have one poller at the time (lazy bird). */
    RTPOLLSET           hPollSet;
    /** Critical section protecting the above members.
     * (Taking the lazy/simple approach.) */
    RTCRITSECT          CritSect;
    /** Buffer for the zero byte read. */
    uint8_t             abBuf[8];
} RTPIPEINTERNAL;



/**
 * Wrapper for getting FILE_PIPE_LOCAL_INFORMATION via the NT API.
 *
 * @returns Success indicator (true/false).
 * @param   pThis               The pipe.
 * @param   pInfo               The info structure.
 */
static bool rtPipeQueryNtInfo(RTPIPEINTERNAL *pThis, FILE_PIPE_LOCAL_INFORMATION *pInfo)
{
    IO_STATUS_BLOCK Ios;
    RT_ZERO(Ios);
    RT_ZERO(*pInfo);
    NTSTATUS rcNt = NtQueryInformationFile(pThis->hPipe, &Ios, pInfo, sizeof(*pInfo), FilePipeLocalInformation);
    return rcNt >= 0;
}


RTDECL(int)  RTPipeCreate(PRTPIPE phPipeRead, PRTPIPE phPipeWrite, uint32_t fFlags)
{
    AssertPtrReturn(phPipeRead, VERR_INVALID_POINTER);
    AssertPtrReturn(phPipeWrite, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTPIPE_C_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Create the read end of the pipe.
     */
    DWORD   dwErr;
    HANDLE  hPipeR;
    HANDLE  hPipeW;
    int     rc;
    for (;;)
    {
        static volatile uint32_t    g_iNextPipe = 0;
        char                        szName[128];
        RTStrPrintf(szName, sizeof(szName), "\\\\.\\pipe\\iprt-pipe-%u-%u", RTProcSelf(), ASMAtomicIncU32(&g_iNextPipe));

        SECURITY_ATTRIBUTES  SecurityAttributes;
        PSECURITY_ATTRIBUTES pSecurityAttributes = NULL;
        if (fFlags & RTPIPE_C_INHERIT_READ)
        {
            SecurityAttributes.nLength              = sizeof(SecurityAttributes);
            SecurityAttributes.lpSecurityDescriptor = NULL;
            SecurityAttributes.bInheritHandle       = TRUE;
            pSecurityAttributes = &SecurityAttributes;
        }

        DWORD dwOpenMode = PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED;
#ifdef FILE_FLAG_FIRST_PIPE_INSTANCE
        dwOpenMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
#endif

        DWORD dwPipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT;
#ifdef PIPE_REJECT_REMOTE_CLIENTS
        dwPipeMode |= PIPE_REJECT_REMOTE_CLIENTS;
#endif

        hPipeR = CreateNamedPipeA(szName, dwOpenMode, dwPipeMode, 1 /*nMaxInstances*/, RTPIPE_NT_SIZE, RTPIPE_NT_SIZE,
                                  NMPWAIT_USE_DEFAULT_WAIT, pSecurityAttributes);
#ifdef PIPE_REJECT_REMOTE_CLIENTS
        if (hPipeR == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER)
        {
            dwPipeMode &= ~PIPE_REJECT_REMOTE_CLIENTS;
            hPipeR = CreateNamedPipeA(szName, dwOpenMode, dwPipeMode, 1 /*nMaxInstances*/, RTPIPE_NT_SIZE, RTPIPE_NT_SIZE,
                                      NMPWAIT_USE_DEFAULT_WAIT, pSecurityAttributes);
        }
#endif
#ifdef FILE_FLAG_FIRST_PIPE_INSTANCE
        if (hPipeR == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER)
        {
            dwOpenMode &= ~FILE_FLAG_FIRST_PIPE_INSTANCE;
            hPipeR = CreateNamedPipeA(szName, dwOpenMode, dwPipeMode, 1 /*nMaxInstances*/, RTPIPE_NT_SIZE, RTPIPE_NT_SIZE,
                                      NMPWAIT_USE_DEFAULT_WAIT, pSecurityAttributes);
        }
#endif
        if (hPipeR != INVALID_HANDLE_VALUE)
        {
            /*
             * Connect to the pipe (the write end).
             * We add FILE_READ_ATTRIBUTES here to make sure we can query the
             * pipe state later on.
             */
            pSecurityAttributes = NULL;
            if (fFlags & RTPIPE_C_INHERIT_WRITE)
            {
                SecurityAttributes.nLength              = sizeof(SecurityAttributes);
                SecurityAttributes.lpSecurityDescriptor = NULL;
                SecurityAttributes.bInheritHandle       = TRUE;
                pSecurityAttributes = &SecurityAttributes;
            }

            hPipeW = CreateFileA(szName,
                                 GENERIC_WRITE | FILE_READ_ATTRIBUTES /*dwDesiredAccess*/,
                                 0 /*dwShareMode*/,
                                 pSecurityAttributes,
                                 OPEN_EXISTING /* dwCreationDisposition */,
                                 FILE_FLAG_OVERLAPPED /*dwFlagsAndAttributes*/,
                                 NULL /*hTemplateFile*/);
            if (hPipeW != INVALID_HANDLE_VALUE)
                break;
            dwErr = GetLastError();
            CloseHandle(hPipeR);
        }
        else
            dwErr = GetLastError();
        if (   dwErr != ERROR_PIPE_BUSY     /* already exist - compatible */
            && dwErr != ERROR_ACCESS_DENIED /* already exist - incompatible */)
            return RTErrConvertFromWin32(dwErr);
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
            rc = RTCritSectInit(&pThisR->CritSect);
            if (RT_SUCCESS(rc))
            {
                rc = RTCritSectInit(&pThisW->CritSect);
                if (RT_SUCCESS(rc))
                {
                    pThisR->Overlapped.hEvent = CreateEvent(NULL, TRUE /*fManualReset*/,
                                                            TRUE /*fInitialState*/, NULL /*pName*/);
                    if (pThisR->Overlapped.hEvent != NULL)
                    {
                        pThisW->Overlapped.hEvent = CreateEvent(NULL, TRUE /*fManualReset*/,
                                                                TRUE /*fInitialState*/, NULL /*pName*/);
                        if (pThisW->Overlapped.hEvent != NULL)
                        {
                            pThisR->u32Magic            = RTPIPE_MAGIC;
                            pThisW->u32Magic            = RTPIPE_MAGIC;
                            pThisR->hPipe               = hPipeR;
                            pThisW->hPipe               = hPipeW;
                            pThisR->fRead               = true;
                            pThisW->fRead               = false;
                            pThisR->fLeaveOpen          = false;
                            pThisW->fLeaveOpen          = false;
                            //pThisR->fIOPending        = false;
                            //pThisW->fIOPending        = false;
                            //pThisR->fZeroByteRead     = false;
                            //pThisW->fZeroByteRead     = false;
                            //pThisR->fBrokenPipe       = false;
                            //pThisW->fBrokenPipe       = false;
                            //pThisW->fPromisedWritable = false;
                            //pThisR->fPromisedWritable = false;
                            pThisW->fCreatedInheritable = RT_BOOL(fFlags & RTPIPE_C_INHERIT_WRITE);
                            pThisR->fCreatedInheritable = RT_BOOL(fFlags & RTPIPE_C_INHERIT_READ);
                            //pThisR->cUsers            = 0;
                            //pThisW->cUsers            = 0;
                            //pThisR->pbBounceBuf       = NULL;
                            //pThisW->pbBounceBuf       = NULL;
                            //pThisR->cbBounceBufUsed   = 0;
                            //pThisW->cbBounceBufUsed   = 0;
                            //pThisR->cbBounceBufAlloc  = 0;
                            //pThisW->cbBounceBufAlloc  = 0;
                            pThisR->hPollSet            = NIL_RTPOLLSET;
                            pThisW->hPollSet            = NIL_RTPOLLSET;

                            *phPipeRead  = pThisR;
                            *phPipeWrite = pThisW;
                            return VINF_SUCCESS;
                        }
                        CloseHandle(pThisR->Overlapped.hEvent);
                    }
                    RTCritSectDelete(&pThisW->CritSect);
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

    CloseHandle(hPipeR);
    CloseHandle(hPipeW);
    return rc;
}


/**
 * Common worker for handling I/O completion.
 *
 * This is used by RTPipeClose, RTPipeWrite and RTPipeWriteBlocking.
 *
 * @returns IPRT status code.
 * @param   pThis               The pipe instance handle.
 */
static int rtPipeWriteCheckCompletion(RTPIPEINTERNAL *pThis)
{
    int rc;
    DWORD dwRc = WaitForSingleObject(pThis->Overlapped.hEvent, 0);
    if (dwRc == WAIT_OBJECT_0)
    {
        DWORD cbWritten = 0;
        if (GetOverlappedResult(pThis->hPipe, &pThis->Overlapped, &cbWritten, TRUE))
        {
            for (;;)
            {
                if (cbWritten >= pThis->cbBounceBufUsed)
                {
                    pThis->fIOPending = false;
                    rc = VINF_SUCCESS;
                    break;
                }

                /* resubmit the remainder of the buffer - can this actually happen? */
                pThis->cbBounceBufUsed -= cbWritten;
                memmove(&pThis->pbBounceBuf[0], &pThis->pbBounceBuf[cbWritten], pThis->cbBounceBufUsed);
                rc = ResetEvent(pThis->Overlapped.hEvent); Assert(rc == TRUE);
                if (!WriteFile(pThis->hPipe, pThis->pbBounceBuf, (DWORD)pThis->cbBounceBufUsed, &cbWritten, &pThis->Overlapped))
                {
                    DWORD const dwErr = GetLastError();
                    if (dwErr == ERROR_IO_PENDING)
                        rc = VINF_TRY_AGAIN;
                    else
                    {
                        pThis->fIOPending = false;
                        if (dwErr == ERROR_NO_DATA)
                            rc = VERR_BROKEN_PIPE;
                        else
                            rc = RTErrConvertFromWin32(GetLastError());
                        if (rc == VERR_BROKEN_PIPE)
                            pThis->fBrokenPipe = true;
                    }
                    break;
                }
                Assert(cbWritten > 0);
            }
        }
        else
        {
            pThis->fIOPending = false;
            rc = RTErrConvertFromWin32(GetLastError());
        }
    }
    else if (dwRc == WAIT_TIMEOUT)
        rc = VINF_TRY_AGAIN;
    else
    {
        pThis->fIOPending = false;
        if (dwRc == WAIT_ABANDONED)
            rc = VERR_INVALID_HANDLE;
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
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

    if (!pThis->fRead && pThis->fIOPending)
        rtPipeWriteCheckCompletion(pThis);

    if (!fLeaveOpen && !pThis->fLeaveOpen)
        CloseHandle(pThis->hPipe);
    pThis->hPipe = INVALID_HANDLE_VALUE;

    CloseHandle(pThis->Overlapped.hEvent);
    pThis->Overlapped.hEvent = NULL;

    RTMemFree(pThis->pbBounceBuf);
    pThis->pbBounceBuf = NULL;

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
    HANDLE hNative = (HANDLE)hNativePipe;
    AssertReturn(GetFileType(hNative) == FILE_TYPE_PIPE, VERR_INVALID_HANDLE);

    DWORD cMaxInstances;
    DWORD fInfo;
    if (!GetNamedPipeInfo(hNative, &fInfo, NULL, NULL, &cMaxInstances))
        return RTErrConvertFromWin32(GetLastError());
    /* Doesn't seem to matter to much if the pipe is message or byte type. Cygwin
       seems to hand us such pipes when capturing output (@bugref{9397}), so just
       ignore skip this check:
    AssertReturn(!(fInfo & PIPE_TYPE_MESSAGE), VERR_INVALID_HANDLE); */
    AssertReturn(cMaxInstances == 1, VERR_INVALID_HANDLE);

    DWORD cInstances;
    DWORD fState;
    if (!GetNamedPipeHandleState(hNative, &fState, &cInstances, NULL, NULL, NULL, 0))
        return RTErrConvertFromWin32(GetLastError());
    AssertReturn(!(fState & PIPE_NOWAIT), VERR_INVALID_HANDLE);
    AssertReturn(!(fState & PIPE_READMODE_MESSAGE), VERR_INVALID_HANDLE);
    AssertReturn(cInstances <= 1, VERR_INVALID_HANDLE);

    /*
     * Looks kind of OK, create a handle so we can try rtPipeQueryNtInfo on it
     * and see if we need to duplicate it to make that call work.
     */
    RTPIPEINTERNAL *pThis = (RTPIPEINTERNAL *)RTMemAllocZ(sizeof(RTPIPEINTERNAL));
    if (!pThis)
        return VERR_NO_MEMORY;
    int rc = RTCritSectInit(&pThis->CritSect);
    if (RT_SUCCESS(rc))
    {
        pThis->Overlapped.hEvent = CreateEvent(NULL, TRUE /*fManualReset*/,
                                               TRUE /*fInitialState*/, NULL /*pName*/);
        if (pThis->Overlapped.hEvent != NULL)
        {
            pThis->u32Magic             = RTPIPE_MAGIC;
            pThis->hPipe                = hNative;
            pThis->fRead                = RT_BOOL(fFlags & RTPIPE_N_READ);
            pThis->fLeaveOpen           = RT_BOOL(fFlags & RTPIPE_N_LEAVE_OPEN);
            //pThis->fIOPending         = false;
            //pThis->fZeroByteRead      = false;
            //pThis->fBrokenPipe        = false;
            //pThis->fPromisedWritable  = false;
            pThis->fCreatedInheritable  = RT_BOOL(fFlags & RTPIPE_N_INHERIT);
            //pThis->cUsers             = 0;
            //pThis->pbBounceBuf        = NULL;
            //pThis->cbBounceBufUsed    = 0;
            //pThis->cbBounceBufAlloc   = 0;
            pThis->hPollSet             = NIL_RTPOLLSET;

            HANDLE                      hNative2 = INVALID_HANDLE_VALUE;
            FILE_PIPE_LOCAL_INFORMATION Info;
            RT_ZERO(Info);
            if (   g_pfnSetHandleInformation
                && rtPipeQueryNtInfo(pThis, &Info))
                rc = VINF_SUCCESS;
            else
            {
                if (DuplicateHandle(GetCurrentProcess() /*hSrcProcess*/, hNative /*hSrcHandle*/,
                                    GetCurrentProcess() /*hDstProcess*/, &hNative2 /*phDstHandle*/,
                                    pThis->fRead ? GENERIC_READ : GENERIC_WRITE | FILE_READ_ATTRIBUTES /*dwDesiredAccess*/,
                                    !!(fFlags & RTPIPE_N_INHERIT) /*fInheritHandle*/,
                                    0 /*dwOptions*/))
                {
                    pThis->hPipe = hNative2;
                    if (rtPipeQueryNtInfo(pThis, &Info))
                    {
                        pThis->fLeaveOpen = false;
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        rc = VERR_ACCESS_DENIED;
                        CloseHandle(hNative2);
                    }
                }
                else
                    hNative2 = INVALID_HANDLE_VALUE;
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Verify the pipe state and correct the inheritability.
                 */
                AssertStmt(   Info.NamedPipeState == FILE_PIPE_CONNECTED_STATE
                           || Info.NamedPipeState == FILE_PIPE_CLOSING_STATE
                           || Info.NamedPipeState == FILE_PIPE_DISCONNECTED_STATE,
                           rc = VERR_INVALID_HANDLE);
                AssertStmt(      Info.NamedPipeConfiguration
                              == (   Info.NamedPipeEnd == FILE_PIPE_SERVER_END
                                  ? (pThis->fRead ? FILE_PIPE_INBOUND  : FILE_PIPE_OUTBOUND)
                                  : (pThis->fRead ? FILE_PIPE_OUTBOUND : FILE_PIPE_INBOUND) )
                           || Info.NamedPipeConfiguration == FILE_PIPE_FULL_DUPLEX,
                           rc = VERR_INVALID_HANDLE);
                if (   RT_SUCCESS(rc)
                    && hNative2 == INVALID_HANDLE_VALUE
                    && !g_pfnSetHandleInformation(hNative,
                                                  HANDLE_FLAG_INHERIT /*dwMask*/,
                                                  fFlags & RTPIPE_N_INHERIT ? HANDLE_FLAG_INHERIT : 0))
                {
                    rc = RTErrConvertFromWin32(GetLastError());
                    AssertMsgFailed(("%Rrc\n", rc));
                }
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Ok, we're good!  If we replaced the handle, make sure it's not a standard
                     * handle if we think we need to close it.
                     */
                    if (hNative2 != INVALID_HANDLE_VALUE)
                    {
                        if (   !(fFlags & RTPIPE_N_LEAVE_OPEN)
                            && hNative != GetStdHandle(STD_INPUT_HANDLE)
                            && hNative != GetStdHandle(STD_OUTPUT_HANDLE)
                            && hNative != GetStdHandle(STD_ERROR_HANDLE) )
                            CloseHandle(hNative);
                    }
                    *phPipe = pThis;
                    return VINF_SUCCESS;
                }
            }

            /* Bail out. */
            if (hNative2 != INVALID_HANDLE_VALUE)
                CloseHandle(hNative2);
            CloseHandle(pThis->Overlapped.hEvent);
        }
        RTCritSectDelete(&pThis->CritSect);
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


RTDECL(int) RTPipeGetCreationInheritability(RTPIPE hPipe)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, false);

    return pThis->fCreatedInheritable;
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
        /* No concurrent readers, sorry. */
        if (pThis->cUsers == 0)
        {
            pThis->cUsers++;

            /*
             * Kick off a an overlapped read.  It should return immediately if
             * there are bytes in the buffer.  If not, we'll cancel it and see
             * what we get back.
             */
            rc = ResetEvent(pThis->Overlapped.hEvent); Assert(rc == TRUE);
            DWORD cbRead = 0;
            if (   cbToRead == 0
                || ReadFile(pThis->hPipe, pvBuf,
                            cbToRead <= ~(DWORD)0 ? (DWORD)cbToRead : ~(DWORD)0,
                            &cbRead, &pThis->Overlapped))
            {
                *pcbRead = cbRead;
                rc = VINF_SUCCESS;
            }
            else if (GetLastError() == ERROR_IO_PENDING)
            {
                pThis->fIOPending = true;
                RTCritSectLeave(&pThis->CritSect);

                /* We use NtCancelIoFile here because the CancelIo API
                   providing access to it wasn't available till NT4.  This
                   code needs to work (or at least load) with NT 3.1 */
                IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
                NTSTATUS rcNt = NtCancelIoFile(pThis->hPipe, &Ios);
                if (!NT_SUCCESS(rcNt))
                    WaitForSingleObject(pThis->Overlapped.hEvent, INFINITE);

                if (GetOverlappedResult(pThis->hPipe, &pThis->Overlapped, &cbRead, TRUE /*fWait*/))
                {
                    *pcbRead = cbRead;
                    rc = VINF_SUCCESS;
                }
                else if (GetLastError() == ERROR_OPERATION_ABORTED)
                {
                    *pcbRead = 0;
                    rc = VINF_TRY_AGAIN;
                }
                else
                    rc = RTErrConvertFromWin32(GetLastError());

                RTCritSectEnter(&pThis->CritSect);
                pThis->fIOPending = false;
            }
            else
                rc = RTErrConvertFromWin32(GetLastError());
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
        /* No concurrent readers, sorry. */
        if (pThis->cUsers == 0)
        {
            pThis->cUsers++;

            size_t cbTotalRead = 0;
            while (cbToRead > 0)
            {
                /*
                 * Kick of a an overlapped read.  It should return immediately if
                 * there is bytes in the buffer.  If not, we'll cancel it and see
                 * what we get back.
                 */
                rc = ResetEvent(pThis->Overlapped.hEvent); Assert(rc == TRUE);
                DWORD cbRead = 0;
                pThis->fIOPending = true;
                RTCritSectLeave(&pThis->CritSect);

                if (ReadFile(pThis->hPipe, pvBuf,
                             cbToRead <= ~(DWORD)0 ? (DWORD)cbToRead : ~(DWORD)0,
                             &cbRead, &pThis->Overlapped))
                    rc = VINF_SUCCESS;
                else if (GetLastError() == ERROR_IO_PENDING)
                {
                    WaitForSingleObject(pThis->Overlapped.hEvent, INFINITE);
                    if (GetOverlappedResult(pThis->hPipe, &pThis->Overlapped, &cbRead, TRUE /*fWait*/))
                        rc = VINF_SUCCESS;
                    else
                        rc = RTErrConvertFromWin32(GetLastError());
                }
                else
                    rc = RTErrConvertFromWin32(GetLastError());

                RTCritSectEnter(&pThis->CritSect);
                pThis->fIOPending = false;
                if (RT_FAILURE(rc))
                    break;

                /* advance */
                cbToRead    -= cbRead;
                cbTotalRead += cbRead;
                pvBuf        = (uint8_t *)pvBuf + cbRead;
            }

            if (rc == VERR_BROKEN_PIPE)
                pThis->fBrokenPipe = true;

            if (pcbRead)
            {
                *pcbRead = cbTotalRead;
                if (   RT_FAILURE(rc)
                    && cbTotalRead
                    && rc != VERR_INVALID_POINTER)
                    rc = VINF_SUCCESS;
            }

            pThis->cUsers--;
        }
        else
            rc = VERR_WRONG_ORDER;
        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;
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
        /* No concurrent writers, sorry. */
        if (pThis->cUsers == 0)
        {
            pThis->cUsers++;

            /* If I/O is pending, check if it has completed. */
            if (pThis->fIOPending)
                rc = rtPipeWriteCheckCompletion(pThis);
            else
                rc = VINF_SUCCESS;
            if (rc == VINF_SUCCESS)
            {
                Assert(!pThis->fIOPending);

                /* Adjust the number of bytes to write to fit into the current
                   buffer quota, unless we've promised stuff in RTPipeSelectOne.
                   WriteQuotaAvailable better not be zero when it shouldn't!! */
                FILE_PIPE_LOCAL_INFORMATION Info;
                if (   !pThis->fPromisedWritable
                    && cbToWrite > 0
                    && rtPipeQueryNtInfo(pThis, &Info))
                {
                    if (Info.NamedPipeState == FILE_PIPE_CLOSING_STATE)
                        rc = VERR_BROKEN_PIPE;
                    /** @todo fixme: To get the pipe writing support to work the
                     *               block below needs to be commented out until a
                     *               way is found to address the problem of the incorrectly
                     *               set field Info.WriteQuotaAvailable.
                     * Update: We now just write up to RTPIPE_NT_SIZE more.  This is quite
                     *         possibely what lead to the misunderstanding here wrt to
                     *         WriteQuotaAvailable updating. */
#if 0
                    else if (   cbToWrite >= Info.WriteQuotaAvailable
                             && Info.OutboundQuota != 0
                             && (Info.WriteQuotaAvailable || pThis->cbBounceBufAlloc)
                            )
                    {
                        cbToWrite = Info.WriteQuotaAvailable;
                        if (!cbToWrite)
                            rc = VINF_TRY_AGAIN;
                    }
#endif
                }
                pThis->fPromisedWritable = false;

                /* Do the bounce buffering. */
                if (    pThis->cbBounceBufAlloc < cbToWrite
                    &&  pThis->cbBounceBufAlloc < RTPIPE_NT_SIZE)
                {
                    if (cbToWrite > RTPIPE_NT_SIZE)
                        cbToWrite = RTPIPE_NT_SIZE;
                    void *pv = RTMemRealloc(pThis->pbBounceBuf, RT_ALIGN_Z(cbToWrite, _1K));
                    if (pv)
                    {
                        pThis->pbBounceBuf = (uint8_t *)pv;
                        pThis->cbBounceBufAlloc = RT_ALIGN_Z(cbToWrite, _1K);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
                else if (cbToWrite > RTPIPE_NT_SIZE)
                    cbToWrite = RTPIPE_NT_SIZE;
                if (RT_SUCCESS(rc) && cbToWrite)
                {
                    memcpy(pThis->pbBounceBuf, pvBuf, cbToWrite);
                    pThis->cbBounceBufUsed = (uint32_t)cbToWrite;

                    /* Submit the write. */
                    rc = ResetEvent(pThis->Overlapped.hEvent); Assert(rc == TRUE);
                    DWORD cbWritten = 0;
                    if (WriteFile(pThis->hPipe, pThis->pbBounceBuf, (DWORD)pThis->cbBounceBufUsed,
                                  &cbWritten, &pThis->Overlapped))
                    {
                        *pcbWritten = RT_MIN(cbWritten, cbToWrite); /* paranoia^3 */
                        rc = VINF_SUCCESS;
                    }
                    else if (GetLastError() == ERROR_IO_PENDING)
                    {
                        *pcbWritten = cbToWrite;
                        pThis->fIOPending = true;
                        rc = VINF_SUCCESS;
                    }
                    else if (GetLastError() == ERROR_NO_DATA)
                        rc = VERR_BROKEN_PIPE;
                    else
                        rc = RTErrConvertFromWin32(GetLastError());
                }
                else if (RT_SUCCESS(rc))
                    *pcbWritten = 0;
            }
            else if (RT_SUCCESS(rc))
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
        /* No concurrent writers, sorry. */
        if (pThis->cUsers == 0)
        {
            pThis->cUsers++;

            /*
             * If I/O is pending, wait for it to complete.
             */
            if (pThis->fIOPending)
            {
                rc = rtPipeWriteCheckCompletion(pThis);
                while (rc == VINF_TRY_AGAIN)
                {
                    Assert(pThis->fIOPending);
                    HANDLE hEvent = pThis->Overlapped.hEvent;
                    RTCritSectLeave(&pThis->CritSect);
                    WaitForSingleObject(hEvent, INFINITE);
                    RTCritSectEnter(&pThis->CritSect);
                }
            }
            if (RT_SUCCESS(rc))
            {
                Assert(!pThis->fIOPending);
                pThis->fPromisedWritable = false;

                /*
                 * Try write everything.
                 * No bounce buffering, cUsers protects us.
                 */
                size_t cbTotalWritten = 0;
                while (cbToWrite > 0)
                {
                    rc = ResetEvent(pThis->Overlapped.hEvent); Assert(rc == TRUE);
                    pThis->fIOPending = true;
                    RTCritSectLeave(&pThis->CritSect);

                    DWORD cbWritten = 0;
                    DWORD const cbToWriteInThisIteration = cbToWrite <= ~(DWORD)0 ? (DWORD)cbToWrite : ~(DWORD)0;
                    if (WriteFile(pThis->hPipe, pvBuf, cbToWriteInThisIteration, &cbWritten, &pThis->Overlapped))
                        rc = VINF_SUCCESS;
                    else if (GetLastError() == ERROR_IO_PENDING)
                    {
                        WaitForSingleObject(pThis->Overlapped.hEvent, INFINITE);
                        if (GetOverlappedResult(pThis->hPipe, &pThis->Overlapped, &cbWritten, TRUE /*fWait*/))
                            rc = VINF_SUCCESS;
                        else
                            rc = RTErrConvertFromWin32(GetLastError());
                    }
                    else if (GetLastError() == ERROR_NO_DATA)
                        rc = VERR_BROKEN_PIPE;
                    else
                        rc = RTErrConvertFromWin32(GetLastError());

                    RTCritSectEnter(&pThis->CritSect);
                    pThis->fIOPending = false;
                    if (RT_FAILURE(rc))
                        break;

                    /* advance */
                    if (cbWritten > cbToWriteInThisIteration) /* paranoia^3 */
                        cbWritten = cbToWriteInThisIteration;
                    pvBuf           = (char const *)pvBuf + cbWritten;
                    cbTotalWritten += cbWritten;
                    cbToWrite      -= cbWritten;
                }

                if (pcbWritten)
                {
                    *pcbWritten = cbTotalWritten;
                    if (   RT_FAILURE(rc)
                        && cbTotalWritten
                        && rc != VERR_INVALID_POINTER)
                        rc = VINF_SUCCESS;
                }
            }

            if (rc == VERR_BROKEN_PIPE)
                pThis->fBrokenPipe = true;

            pThis->cUsers--;
        }
        else
            rc = VERR_WRONG_ORDER;
        RTCritSectLeave(&pThis->CritSect);
    }
    return rc;

#if 0 /** @todo r=bird: What's this? */
    int rc = rtPipeTryBlocking(pThis);
    if (RT_SUCCESS(rc))
    {
        size_t cbTotalWritten = 0;
        while (cbToWrite > 0)
        {
            ssize_t cbWritten = write(pThis->fd, pvBuf, RT_MIN(cbToWrite, SSIZE_MAX));
            if (cbWritten < 0)
            {
                rc = RTErrConvertFromErrno(errno);
                break;
            }

            /* advance */
            pvBuf           = (char const *)pvBuf + cbWritten;
            cbTotalWritten += cbWritten;
            cbToWrite      -= cbWritten;
        }

        if (pcbWritten)
        {
            *pcbWritten = cbTotalWritten;
            if (   RT_FAILURE(rc)
                && cbTotalWritten
                && rc != VERR_INVALID_POINTER)
                rc = VINF_SUCCESS;
        }

        ASMAtomicDecU32(&pThis->u32State);
    }
    return rc;
#endif
}


RTDECL(int) RTPipeFlush(RTPIPE hPipe)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fRead, VERR_ACCESS_DENIED);

    if (!FlushFileBuffers(pThis->hPipe))
    {
        int rc = RTErrConvertFromWin32(GetLastError());
        if (rc == VERR_BROKEN_PIPE)
            pThis->fBrokenPipe = true;
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
    for (unsigned iLoop = 0;; iLoop++)
    {
        HANDLE  hWait = INVALID_HANDLE_VALUE;
        if (pThis->fRead)
        {
            if (pThis->fIOPending)
                hWait = pThis->Overlapped.hEvent;
            else
            {
                /* Peek at the pipe buffer and see how many bytes it contains. */
                DWORD cbAvailable;
                if (   PeekNamedPipe(pThis->hPipe, NULL, 0, NULL, &cbAvailable, NULL)
                    && cbAvailable > 0)
                {
                    rc = VINF_SUCCESS;
                    break;
                }

                /* Start a zero byte read operation that we can wait on. */
                if (cMillies == 0)
                {
                    rc = VERR_TIMEOUT;
                    break;
                }
                AssertBreakStmt(pThis->cUsers == 0, rc = VERR_INTERNAL_ERROR_5);
                rc = ResetEvent(pThis->Overlapped.hEvent); Assert(rc == TRUE);
                DWORD cbRead = 0;
                if (ReadFile(pThis->hPipe, pThis->abBuf, 0, &cbRead, &pThis->Overlapped))
                {
                    rc = VINF_SUCCESS;
                    if (iLoop > 10)
                        RTThreadYield();
                }
                else if (GetLastError() == ERROR_IO_PENDING)
                {
                    pThis->cUsers++;
                    pThis->fIOPending = true;
                    pThis->fZeroByteRead = true;
                    hWait = pThis->Overlapped.hEvent;
                }
                else
                    rc = RTErrConvertFromWin32(GetLastError());
            }
        }
        else
        {
            if (pThis->fIOPending)
            {
                rc = rtPipeWriteCheckCompletion(pThis);
                if (RT_FAILURE(rc))
                    break;
            }
            if (pThis->fIOPending)
                hWait = pThis->Overlapped.hEvent;
            else
            {
                FILE_PIPE_LOCAL_INFORMATION Info;
#if 1
                /* We can always write one bounce buffer full of data regardless of
                   the pipe buffer state.  We must of course take this into account,
                   or code like "Full write buffer" test in tstRTPipe gets confused. */
                rc = VINF_SUCCESS;
                if (rtPipeQueryNtInfo(pThis, &Info))
                {
                    /* Check for broken pipe. */
                    if (Info.NamedPipeState != FILE_PIPE_CLOSING_STATE)
                        pThis->fPromisedWritable = true;
                    else
                        rc = VERR_BROKEN_PIPE;
                }
                else
                    pThis->fPromisedWritable = true;
                break;

#else /* old code: */
                if (rtPipeQueryNtInfo(pThis, &Info))
                {
                    /* Check for broken pipe. */
                    if (Info.NamedPipeState == FILE_PIPE_CLOSING_STATE)
                    {
                        rc = VERR_BROKEN_PIPE;
                        break;
                    }

                    /* Check for available write buffer space. */
                    if (Info.WriteQuotaAvailable > 0)
                    {
                        pThis->fPromisedWritable = false;
                        rc = VINF_SUCCESS;
                        break;
                    }

                    /* delayed buffer alloc or timeout: phony promise
                       later: See if we still can associate a semaphore with
                              the pipe, like on OS/2. */
                    if (Info.OutboundQuota == 0 || cMillies)
                    {
                        pThis->fPromisedWritable = true;
                        rc = VINF_SUCCESS;
                        break;
                    }
                }
                else
                {
                    pThis->fPromisedWritable = true;
                    rc = VINF_SUCCESS;
                    break;
                }
#endif
            }
        }
        if (RT_FAILURE(rc))
            break;

        /*
         * Check for timeout.
         */
        DWORD cMsMaxWait = INFINITE;
        if (   cMillies != RT_INDEFINITE_WAIT
            && (   hWait != INVALID_HANDLE_VALUE
                || iLoop > 10)
           )
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
        if (hWait != INVALID_HANDLE_VALUE)
        {
            RTCritSectLeave(&pThis->CritSect);

            DWORD dwRc = WaitForSingleObject(hWait, cMsMaxWait);
            if (dwRc == WAIT_OBJECT_0)
                rc = VINF_SUCCESS;
            else if (dwRc == WAIT_TIMEOUT)
                rc = VERR_TIMEOUT;
            else if (dwRc == WAIT_ABANDONED)
                rc = VERR_INVALID_HANDLE;
            else
                rc = RTErrConvertFromWin32(GetLastError());
            if (   RT_FAILURE(rc)
                && pThis->u32Magic != RTPIPE_MAGIC)
                return rc;

            RTCritSectEnter(&pThis->CritSect);
            if (pThis->fZeroByteRead)
            {
                pThis->cUsers--;
                pThis->fIOPending = false;
                if (rc != VINF_SUCCESS)
                {
                    IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
                    NtCancelIoFile(pThis->hPipe, &Ios);
                }
                DWORD cbRead = 0;
                GetOverlappedResult(pThis->hPipe, &pThis->Overlapped, &cbRead, TRUE /*fWait*/);
            }
            if (RT_FAILURE(rc))
                break;
        }
    }

    if (rc == VERR_BROKEN_PIPE)
        pThis->fBrokenPipe = true;

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

    /** @todo The file size should give the same info and be slightly faster... */
    DWORD cbAvailable = 0;
    if (PeekNamedPipe(pThis->hPipe, NULL, 0, NULL, &cbAvailable, NULL))
    {
#if ARCH_BITS == 32
        /*
         * Kludge!
         *
         * Prior to XP SP1 (?), the returned cbAvailable value was not adjusted
         * by the read position in the current message/buffer, so it could
         * potentially be too high.  This may cause the caller to try read more
         * data than what's actually available, which may cause the read to
         * block when the caller thought it wouldn't.
         *
         * To get an accurate readable size, we have to provide an output
         * buffer and see how much we actually get back in it, as the data
         * peeking works correctly (as you would expect).
         */
        if (cbAvailable == 0 || g_enmWinVer >= kRTWinOSType_XP64)
        { /* No data available or kernel shouldn't be affected. */ }
        else
        {
            for (unsigned i = 0; ; i++)
            {
                uint8_t abBufStack[_16K];
                void   *pvBufFree = NULL;
                void   *pvBuf;
                DWORD   cbBuf     = RT_ALIGN_32(cbAvailable + i * 256, 64);
                if (cbBuf <= sizeof(abBufStack))
                {
                    pvBuf = abBufStack;
                    /* No cbBuf = sizeof(abBufStack) here! PeekNamedPipe bounce buffers the request on the heap. */
                }
                else
                {
                    pvBufFree = pvBuf = RTMemTmpAlloc(cbBuf);
                    if (!pvBuf)
                    {
                        rc = VERR_NO_TMP_MEMORY;
                        cbAvailable = 1;
                        break;
                    }
                }

                DWORD cbAvailable2 = 0;
                DWORD cbRead       = 0;
                BOOL fRc = PeekNamedPipe(pThis->hPipe, pvBuf, cbBuf, &cbRead, &cbAvailable2, NULL);
                Log(("RTPipeQueryReadable: #%u: cbAvailable=%#x cbRead=%#x cbAvailable2=%#x (cbBuf=%#x)\n",
                     i, cbAvailable, cbRead, cbAvailable2, cbBuf));

                RTMemTmpFree(pvBufFree);

                if (fRc)
                {
                    if (cbAvailable2 <= cbBuf || i >= 10)
                        cbAvailable = cbRead;
                    else
                    {
                        cbAvailable = cbAvailable2;
                        continue;
                    }
                }
                else
                {
                    rc = RTErrConvertFromWin32(GetLastError());
                    cbAvailable = 1;
                }
                break;
            }
        }
#endif
        *pcbReadable = cbAvailable;
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());

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

    FILE_PIPE_LOCAL_INFORMATION Info;
    if (rtPipeQueryNtInfo(pThis, &Info))
    {
        pObjInfo->cbAllocated = pThis->fRead ? Info.InboundQuota : Info.OutboundQuota;
        pObjInfo->cbObject    = pThis->fRead ? Info.ReadDataAvailable : Info.WriteQuotaAvailable;
    }

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

    /* Later: Try register an event handle with the pipe like on OS/2, there is
       a file control for doing this obviously intended for the OS/2 subsys.
       The question is whether this still exists on Vista and W7. */
    *phNative = (RTHCINTPTR)pThis->Overlapped.hEvent;
    return VINF_SUCCESS;
}


/**
 * Checks for pending events.
 *
 * @returns Event mask or 0.
 * @param   pThis               The pipe handle.
 * @param   fEvents             The desired events.
 */
static uint32_t rtPipePollCheck(RTPIPEINTERNAL *pThis, uint32_t fEvents)
{
    uint32_t fRetEvents = 0;
    if (pThis->fBrokenPipe)
        fRetEvents |= RTPOLL_EVT_ERROR;
    else if (pThis->fRead)
    {
        if (!pThis->fIOPending)
        {
            DWORD cbAvailable;
            if (PeekNamedPipe(pThis->hPipe, NULL, 0, NULL, &cbAvailable, NULL))
            {
                if (   (fEvents & RTPOLL_EVT_READ)
                    && cbAvailable > 0)
                    fRetEvents |= RTPOLL_EVT_READ;
            }
            else
            {
                if (GetLastError() == ERROR_BROKEN_PIPE)
                    pThis->fBrokenPipe = true;
                fRetEvents |= RTPOLL_EVT_ERROR;
            }
        }
    }
    else
    {
        if (pThis->fIOPending)
        {
            rtPipeWriteCheckCompletion(pThis);
            if (pThis->fBrokenPipe)
                fRetEvents |= RTPOLL_EVT_ERROR;
        }
        if (   !pThis->fIOPending
            && !fRetEvents)
        {
            FILE_PIPE_LOCAL_INFORMATION Info;
            if (rtPipeQueryNtInfo(pThis, &Info))
            {
                /* Check for broken pipe. */
                if (Info.NamedPipeState == FILE_PIPE_CLOSING_STATE)
                {
                    fRetEvents = RTPOLL_EVT_ERROR;
                    pThis->fBrokenPipe = true;
                }

                /* Check if there is available buffer space. */
                if (   !fRetEvents
                    && (fEvents & RTPOLL_EVT_WRITE)
                    && (   Info.WriteQuotaAvailable > 0
                        || Info.OutboundQuota == 0)
                    )
                    fRetEvents |= RTPOLL_EVT_WRITE;
            }
            else if (fEvents & RTPOLL_EVT_WRITE)
                fRetEvents |= RTPOLL_EVT_WRITE;
        }
    }

    return fRetEvents;
}


/**
 * Internal RTPoll helper that polls the pipe handle and, if @a fNoWait is
 * clear, starts whatever actions we've got running during the poll call.
 *
 * @returns 0 if no pending events, actions initiated if @a fNoWait is clear.
 *          Event mask (in @a fEvents) and no actions if the handle is ready
 *          already.
 *          UINT32_MAX (asserted) if the pipe handle is busy in I/O or a
 *          different poll set.
 *
 * @param   hPipe               The pipe handle.
 * @param   hPollSet            The poll set handle (for access checks).
 * @param   fEvents             The events we're polling for.
 * @param   fFinalEntry         Set if this is the final entry for this handle
 *                              in this poll set.  This can be used for dealing
 *                              with duplicate entries.
 * @param   fNoWait             Set if it's a zero-wait poll call.  Clear if
 *                              we'll wait for an event to occur.
 */
uint32_t rtPipePollStart(RTPIPE hPipe, RTPOLLSET hPollSet, uint32_t fEvents, bool fFinalEntry, bool fNoWait)
{
    /** @todo All this polling code could be optimized to make fewer system
     *        calls; like for instance the ResetEvent calls. */
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, UINT32_MAX);
    RT_NOREF_PV(fFinalEntry);

    int rc = RTCritSectEnter(&pThis->CritSect);
    AssertRCReturn(rc, UINT32_MAX);

    /* Check that this is the only current use of this pipe. */
    uint32_t fRetEvents;
    if (   pThis->cUsers   == 0
        || pThis->hPollSet == hPollSet)
    {
        /* Check what the current events are. */
        fRetEvents = rtPipePollCheck(pThis, fEvents);
        if (   !fRetEvents
            && !fNoWait)
        {
            /* Make sure the event semaphore has been reset. */
            if (!pThis->fIOPending)
            {
                rc = ResetEvent(pThis->Overlapped.hEvent);
                Assert(rc == TRUE);
            }

            /* Kick off the zero byte read thing if applicable. */
            if (   !pThis->fIOPending
                && pThis->fRead
                && (fEvents & RTPOLL_EVT_READ)
               )
            {
                DWORD cbRead = 0;
                if (ReadFile(pThis->hPipe, pThis->abBuf, 0, &cbRead, &pThis->Overlapped))
                    fRetEvents = rtPipePollCheck(pThis, fEvents);
                else if (GetLastError() == ERROR_IO_PENDING)
                {
                    pThis->fIOPending    = true;
                    pThis->fZeroByteRead = true;
                }
                else
                    fRetEvents = RTPOLL_EVT_ERROR;
            }

            /* If we're still set for the waiting, record the poll set and
               mark the pipe used. */
            if (!fRetEvents)
            {
                pThis->cUsers++;
                pThis->hPollSet = hPollSet;
            }
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


/**
 * Called after a WaitForMultipleObjects returned in order to check for pending
 * events and stop whatever actions that rtPipePollStart() initiated.
 *
 * @returns Event mask or 0.
 *
 * @param   hPipe               The pipe handle.
 * @param   fEvents             The events we're polling for.
 * @param   fFinalEntry         Set if this is the final entry for this handle
 *                              in this poll set.  This can be used for dealing
 *                              with duplicate entries.  Only keep in mind that
 *                              this method is called in reverse order, so the
 *                              first call will have this set (when the entire
 *                              set was processed).
 * @param   fHarvestEvents      Set if we should check for pending events.
 */
uint32_t rtPipePollDone(RTPIPE hPipe, uint32_t fEvents, bool fFinalEntry, bool fHarvestEvents)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, 0);
    RT_NOREF_PV(fFinalEntry);
    RT_NOREF_PV(fHarvestEvents);

    int rc = RTCritSectEnter(&pThis->CritSect);
    AssertRCReturn(rc, 0);

    Assert(pThis->cUsers > 0);


    /* Cancel the zero byte read. */
    uint32_t fRetEvents = 0;
    if (pThis->fZeroByteRead)
    {
        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        NtCancelIoFile(pThis->hPipe, &Ios);

        DWORD cbRead = 0;
        if (   !GetOverlappedResult(pThis->hPipe, &pThis->Overlapped, &cbRead, TRUE /*fWait*/)
            && GetLastError() != ERROR_OPERATION_ABORTED)
            fRetEvents = RTPOLL_EVT_ERROR;

        pThis->fIOPending    = false;
        pThis->fZeroByteRead = false;
    }

    /* harvest events. */
    fRetEvents |= rtPipePollCheck(pThis, fEvents);

    /* update counters. */
    pThis->cUsers--;
    /** @todo This isn't sane, or is it? See OS/2 impl. */
    if (!pThis->cUsers)
        pThis->hPollSet = NIL_RTPOLLSET;

    RTCritSectLeave(&pThis->CritSect);
    return fRetEvents;
}

