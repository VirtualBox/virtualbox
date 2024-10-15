/* $Id: pipe-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Anonymous Pipes, POSIX Implementation.
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
#include <iprt/pipe.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include "internal/magics.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <signal.h>
#ifdef RT_OS_LINUX
# include <sys/syscall.h>
#endif
#ifdef RT_OS_SOLARIS
# include <sys/filio.h>
#endif

#include "internal/pipe.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTPIPEINTERNAL
{
    /** Magic value (RTPIPE_MAGIC). */
    uint32_t            u32Magic;
    /** The file descriptor. */
    int                 fd;
    /** Set if this is the read end, clear if it's the write end. */
    bool                fRead;
    /** RTPipeFromNative: Leave it open on RTPipeClose. */
    bool                fLeaveOpen;
    /** Atomically operated state variable.
     *
     *  - Bits 0 thru 29 - Users of the new mode.
     *  - Bit 30 - The pipe mode, set indicates blocking.
     *  - Bit 31 - Set when we're switching the mode.
     */
    uint32_t volatile   u32State;
} RTPIPEINTERNAL;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name RTPIPEINTERNAL::u32State defines
 * @{ */
#define RTPIPE_POSIX_BLOCKING           UINT32_C(0x40000000)
#define RTPIPE_POSIX_SWITCHING          UINT32_C(0x80000000)
#define RTPIPE_POSIX_SWITCHING_BIT      31
#define RTPIPE_POSIX_USERS_MASK         UINT32_C(0x3fffffff)
/** @} */



/**
 * Wrapper for calling pipe2() or pipe().
 *
 * When using pipe2() the returned handles are marked close-on-exec and does
 * not risk racing process creation calls on other threads.
 *
 * @returns See pipe().
 * @param   paFds               See pipe().
 * @param   piNewPipeSyscall    Where to cache which call we should used. -1 if
 *                              pipe(), 1 if pipe2(), 0 if not yet decided.
 */
static int my_pipe_wrapper(int *paFds, int *piNewPipeSyscall)
{
    if (*piNewPipeSyscall >= 0)
    {
#if defined(RT_OS_LINUX) && defined(__NR_pipe2) && defined(O_CLOEXEC)
        long rc = syscall(__NR_pipe2, paFds, O_CLOEXEC);
        if (rc >= 0)
        {
            if (*piNewPipeSyscall == 0)
                *piNewPipeSyscall = 1;
            return (int)rc;
        }
#endif
        *piNewPipeSyscall = -1;
    }

    return pipe(paFds);
}


RTDECL(int)  RTPipeCreate(PRTPIPE phPipeRead, PRTPIPE phPipeWrite, uint32_t fFlags)
{
    AssertPtrReturn(phPipeRead, VERR_INVALID_POINTER);
    AssertPtrReturn(phPipeWrite, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTPIPE_C_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Create the pipe and clear/set the close-on-exec flag as required.
     */
    int aFds[2] = {-1, -1};
    static int s_iNewPipeSyscall = 0;
    if (my_pipe_wrapper(aFds, &s_iNewPipeSyscall))
        return RTErrConvertFromErrno(errno);

    int rc = VINF_SUCCESS;
    if (s_iNewPipeSyscall > 0)
    {
        /* created with close-on-exec set. */
        if (fFlags & RTPIPE_C_INHERIT_READ)
        {
            if (fcntl(aFds[0], F_SETFD, 0))
                rc = RTErrConvertFromErrno(errno);
        }

        if (fFlags & RTPIPE_C_INHERIT_WRITE)
        {
            if (fcntl(aFds[1], F_SETFD, 0))
                rc = RTErrConvertFromErrno(errno);
        }
    }
    else
    {
        /* created with close-on-exec cleared. */
        if (!(fFlags & RTPIPE_C_INHERIT_READ))
        {
            if (fcntl(aFds[0], F_SETFD, FD_CLOEXEC))
                rc = RTErrConvertFromErrno(errno);
        }

        if (!(fFlags & RTPIPE_C_INHERIT_WRITE))
        {
            if (fcntl(aFds[1], F_SETFD, FD_CLOEXEC))
                rc = RTErrConvertFromErrno(errno);
        }
    }

    if (RT_SUCCESS(rc))
    {
        /*
         * Create the two handles.
         */
        RTPIPEINTERNAL *pThisR = (RTPIPEINTERNAL *)RTMemAlloc(sizeof(RTPIPEINTERNAL));
        if (pThisR)
        {
            RTPIPEINTERNAL *pThisW = (RTPIPEINTERNAL *)RTMemAlloc(sizeof(RTPIPEINTERNAL));
            if (pThisW)
            {
                pThisR->u32Magic    = RTPIPE_MAGIC;
                pThisW->u32Magic    = RTPIPE_MAGIC;
                pThisR->fd          = aFds[0];
                pThisW->fd          = aFds[1];
                pThisR->fRead       = true;
                pThisW->fRead       = false;
                pThisR->fLeaveOpen  = false;
                pThisW->fLeaveOpen  = false;
                pThisR->u32State    = RTPIPE_POSIX_BLOCKING;
                pThisW->u32State    = RTPIPE_POSIX_BLOCKING;

                *phPipeRead  = pThisR;
                *phPipeWrite = pThisW;

                /*
                 * Before we leave, make sure to shut up SIGPIPE.
                 */
                signal(SIGPIPE, SIG_IGN);
                return VINF_SUCCESS;
            }

            RTMemFree(pThisR);
            rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    close(aFds[0]);
    close(aFds[1]);
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

    int fd = pThis->fd;
    pThis->fd = -1;
    if (!fLeaveOpen && !pThis->fLeaveOpen)
        close(fd);

    if (ASMAtomicReadU32(&pThis->u32State) & RTPIPE_POSIX_USERS_MASK)
    {
        AssertFailed();
        RTThreadSleep(1);
    }

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
    int hNative = (int)hNativePipe;
    struct stat st;
    AssertReturn(fstat(hNative, &st) == 0, RTErrConvertFromErrno(errno));
    AssertMsgReturn(S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode), ("%#x (%o)\n", st.st_mode, st.st_mode), VERR_INVALID_HANDLE);

    int fFd = fcntl(hNative, F_GETFL, 0);
    AssertReturn(fFd != -1, VERR_INVALID_HANDLE);
    AssertMsgReturn(   (fFd & O_ACCMODE) == (fFlags & RTPIPE_N_READ ? O_RDONLY : O_WRONLY)
                    || (fFd & O_ACCMODE) == O_RDWR /* Solaris creates bi-directional pipes. */
                    , ("%#x\n", fFd), VERR_INVALID_HANDLE);

    /*
     * Create the handle.
     */
    RTPIPEINTERNAL *pThis = (RTPIPEINTERNAL *)RTMemAlloc(sizeof(RTPIPEINTERNAL));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic   = RTPIPE_MAGIC;
    pThis->fd         = hNative;
    pThis->fRead      = RT_BOOL(fFlags & RTPIPE_N_READ);
    pThis->fLeaveOpen = RT_BOOL(fFlags & RTPIPE_N_LEAVE_OPEN);
    pThis->u32State   = fFd & O_NONBLOCK ? 0 : RTPIPE_POSIX_BLOCKING;

    /*
     * Fix up inheritability and shut up SIGPIPE and we're done.
     */
    if (fcntl(hNative, F_SETFD, fFlags & RTPIPE_N_INHERIT ? 0 : FD_CLOEXEC) == 0)
    {
        signal(SIGPIPE, SIG_IGN);
        *phPipe = pThis;
        return VINF_SUCCESS;
    }

    int rc = RTErrConvertFromErrno(errno);
    RTMemFree(pThis);
    return rc;
}


RTDECL(RTHCINTPTR) RTPipeToNative(RTPIPE hPipe)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, -1);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, -1);

    return pThis->fd;
}


/**
 * Prepare blocking mode.
 *
 * @returns VINF_SUCCESS
 * @retval  VERR_WRONG_ORDER
 * @retval  VERR_INTERNAL_ERROR_4
 *
 * @param   pThis               The pipe handle.
 */
static int rtPipeTryBlocking(RTPIPEINTERNAL *pThis)
{
    /*
     * Update the state.
     */
    for (;;)
    {
        uint32_t        u32State    = ASMAtomicReadU32(&pThis->u32State);
        uint32_t const  u32StateOld = u32State;
        uint32_t const  cUsers      = (u32State & RTPIPE_POSIX_USERS_MASK);

        if (u32State & RTPIPE_POSIX_BLOCKING)
        {
            AssertReturn(cUsers < RTPIPE_POSIX_USERS_MASK / 2, VERR_INTERNAL_ERROR_4);
            u32State &= ~RTPIPE_POSIX_USERS_MASK;
            u32State |= cUsers + 1;
            if (ASMAtomicCmpXchgU32(&pThis->u32State, u32State, u32StateOld))
            {
                if (u32State & RTPIPE_POSIX_SWITCHING)
                    break;
                return VINF_SUCCESS;
            }
        }
        else if (cUsers == 0)
        {
            u32State = 1 | RTPIPE_POSIX_SWITCHING | RTPIPE_POSIX_BLOCKING;
            if (ASMAtomicCmpXchgU32(&pThis->u32State, u32State, u32StateOld))
                break;
        }
        else
            return VERR_WRONG_ORDER;
        ASMNopPause();
    }

    /*
     * Do the switching.
     */
    int fFlags = fcntl(pThis->fd, F_GETFL, 0);
    if (fFlags != -1)
    {
        if (    !(fFlags & O_NONBLOCK)
            ||  fcntl(pThis->fd, F_SETFL, fFlags & ~O_NONBLOCK) != -1)
        {
            ASMAtomicBitClear(&pThis->u32State, RTPIPE_POSIX_SWITCHING_BIT);
            return VINF_SUCCESS;
        }
    }

    ASMAtomicDecU32(&pThis->u32State);
    return RTErrConvertFromErrno(errno);
}


/**
 * Prepare non-blocking mode.
 *
 * @returns VINF_SUCCESS
 * @retval  VERR_WRONG_ORDER
 * @retval  VERR_INTERNAL_ERROR_4
 *
 * @param   pThis               The pipe handle.
 */
static int rtPipeTryNonBlocking(RTPIPEINTERNAL *pThis)
{
    /*
     * Update the state.
     */
    for (;;)
    {
        uint32_t        u32State    = ASMAtomicReadU32(&pThis->u32State);
        uint32_t const  u32StateOld = u32State;
        uint32_t const  cUsers      = (u32State & RTPIPE_POSIX_USERS_MASK);

        if (!(u32State & RTPIPE_POSIX_BLOCKING))
        {
            AssertReturn(cUsers < RTPIPE_POSIX_USERS_MASK / 2, VERR_INTERNAL_ERROR_4);
            u32State &= ~RTPIPE_POSIX_USERS_MASK;
            u32State |= cUsers + 1;
            if (ASMAtomicCmpXchgU32(&pThis->u32State, u32State, u32StateOld))
            {
                if (u32State & RTPIPE_POSIX_SWITCHING)
                    break;
                return VINF_SUCCESS;
            }
        }
        else if (cUsers == 0)
        {
            u32State = 1 | RTPIPE_POSIX_SWITCHING;
            if (ASMAtomicCmpXchgU32(&pThis->u32State, u32State, u32StateOld))
                break;
        }
        else
            return VERR_WRONG_ORDER;
        ASMNopPause();
    }

    /*
     * Do the switching.
     */
    int fFlags = fcntl(pThis->fd, F_GETFL, 0);
    if (fFlags != -1)
    {
        if (    (fFlags & O_NONBLOCK)
            ||  fcntl(pThis->fd, F_SETFL, fFlags | O_NONBLOCK) != -1)
        {
            ASMAtomicBitClear(&pThis->u32State, RTPIPE_POSIX_SWITCHING_BIT);
            return VINF_SUCCESS;
        }
    }

    ASMAtomicDecU32(&pThis->u32State);
    return RTErrConvertFromErrno(errno);
}


/**
 * Checks if the read pipe has a HUP condition.
 *
 * @returns true if HUP, false if no.
 * @param   pThis               The pipe handle (read).
 */
static bool rtPipePosixHasHup(RTPIPEINTERNAL *pThis)
{
    Assert(pThis->fRead);

    struct pollfd PollFd;
    RT_ZERO(PollFd);
    PollFd.fd      = pThis->fd;
    PollFd.events  = POLLHUP;
    return poll(&PollFd, 1, 0) >= 1
       && (PollFd.revents & POLLHUP);
}


RTDECL(int) RTPipeRead(RTPIPE hPipe, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fRead, VERR_ACCESS_DENIED);
    AssertPtr(pcbRead);
    AssertPtr(pvBuf);

    int rc = rtPipeTryNonBlocking(pThis);
    if (RT_SUCCESS(rc))
    {
        ssize_t cbRead = read(pThis->fd, pvBuf, RT_MIN(cbToRead, SSIZE_MAX));
        if (cbRead >= 0)
        {
            if (cbRead || !cbToRead || !rtPipePosixHasHup(pThis))
                *pcbRead = cbRead;
            else
                rc = VERR_BROKEN_PIPE;
        }
        else if (errno == EAGAIN)
        {
            *pcbRead = 0;
            rc = VINF_TRY_AGAIN;
        }
        else
            rc = RTErrConvertFromErrno(errno);

        ASMAtomicDecU32(&pThis->u32State);
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

    int rc = rtPipeTryBlocking(pThis);
    if (RT_SUCCESS(rc))
    {
        size_t cbTotalRead = 0;
        while (cbToRead > 0)
        {
            ssize_t cbRead = read(pThis->fd, pvBuf, RT_MIN(cbToRead, SSIZE_MAX));
            if (cbRead < 0)
            {
                rc = RTErrConvertFromErrno(errno);
                break;
            }
            if (!cbRead && rtPipePosixHasHup(pThis))
            {
                rc = VERR_BROKEN_PIPE;
                break;
            }

            /* advance */
            pvBuf        = (char *)pvBuf + cbRead;
            cbTotalRead += cbRead;
            cbToRead    -= cbRead;
        }

        if (pcbRead)
        {
            *pcbRead = cbTotalRead;
            if (   RT_FAILURE(rc)
                && cbTotalRead
                && rc != VERR_INVALID_POINTER)
                rc = VINF_SUCCESS;
        }

        ASMAtomicDecU32(&pThis->u32State);
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

    int rc = rtPipeTryNonBlocking(pThis);
    if (RT_SUCCESS(rc))
    {
        if (cbToWrite)
        {
            ssize_t cbWritten = write(pThis->fd, pvBuf, RT_MIN(cbToWrite, SSIZE_MAX));
            if (cbWritten >= 0)
                *pcbWritten = cbWritten;
            else if (errno == EAGAIN)
            {
                *pcbWritten = 0;
                rc = VINF_TRY_AGAIN;
            }
            else
                rc = RTErrConvertFromErrno(errno);
        }
        else
            *pcbWritten = 0;

        ASMAtomicDecU32(&pThis->u32State);
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
}


RTDECL(int) RTPipeFlush(RTPIPE hPipe)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fRead, VERR_ACCESS_DENIED);

    if (fsync(pThis->fd))
    {
        if (errno == EINVAL || errno == ENOTSUP)
            return VERR_NOT_SUPPORTED;
        return RTErrConvertFromErrno(errno);
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTPipeSelectOne(RTPIPE hPipe, RTMSINTERVAL cMillies)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);

    struct pollfd PollFd;
    RT_ZERO(PollFd);
    PollFd.fd      = pThis->fd;
    PollFd.events  = POLLHUP | POLLERR;
    if (pThis->fRead)
        PollFd.events |= POLLIN | POLLPRI;
    else
        PollFd.events |= POLLOUT;

    int timeout;
    if (   cMillies == RT_INDEFINITE_WAIT
        || cMillies >= INT_MAX /* lazy bird */)
        timeout = -1;
    else
        timeout = cMillies;

    int rc = poll(&PollFd, 1, timeout);
    if (rc == -1)
        return RTErrConvertFromErrno(errno);
    return rc > 0 ? VINF_SUCCESS : VERR_TIMEOUT;
}


RTDECL(int) RTPipeQueryReadable(RTPIPE hPipe, size_t *pcbReadable)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->fRead, VERR_PIPE_NOT_READ);
    AssertPtrReturn(pcbReadable, VERR_INVALID_POINTER);

    int cb = 0;
    int rc = ioctl(pThis->fd, FIONREAD, &cb);
    if (rc != -1)
    {
        AssertStmt(cb >= 0, cb = 0);
        *pcbReadable = cb;
        return VINF_SUCCESS;
    }

    rc = errno;
    if (rc == ENOTTY)
        rc = VERR_NOT_SUPPORTED;
    else
        rc = RTErrConvertFromErrno(rc);
    return rc;
}


RTDECL(int) RTPipeQueryInfo(RTPIPE hPipe, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, 0);

    rtPipeFakeQueryInfo(pObjInfo, enmAddAttr, pThis->fRead);

    if (pThis->fRead)
    {
        int cb = 0;
        int rc = ioctl(pThis->fd, FIONREAD, &cb);
        if (rc >= 0)
            pObjInfo->cbObject = cb;
    }
#ifdef FIONSPACE
    else
    {
        int cb = 0;
        int rc = ioctl(pThis->fd, FIONSPACE, &cb);
        if (rc >= 0)
            pObjInfo->cbObject = cb;
    }
#endif

    /** @todo Check this out on linux, solaris and darwin... (Currently going by a
     *        FreeBSD manpage.) */
    struct stat St;
    if (fstat(pThis->fd, &St))
    {
        pObjInfo->cbAllocated = St.st_blksize;
        if (   enmAddAttr == RTFSOBJATTRADD_NOTHING
            || enmAddAttr == RTFSOBJATTRADD_UNIX)
        {
            pObjInfo->Attr.enmAdditional = RTFSOBJATTRADD_UNIX;
            pObjInfo->Attr.u.Unix.INodeId       = St.st_ino;
            pObjInfo->Attr.u.Unix.INodeIdDevice = St.st_dev;
        }
    }
    /** @todo error handling?  */

    return VINF_SUCCESS;
}


int rtPipePollGetHandle(RTPIPE hPipe, uint32_t fEvents, PRTHCINTPTR phNative)
{
    RTPIPEINTERNAL *pThis = hPipe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTPIPE_MAGIC, VERR_INVALID_HANDLE);

    AssertReturn(!(fEvents & RTPOLL_EVT_READ)  || pThis->fRead,  VERR_INVALID_PARAMETER);
    AssertReturn(!(fEvents & RTPOLL_EVT_WRITE) || !pThis->fRead, VERR_INVALID_PARAMETER);

    *phNative = pThis->fd;
    return VINF_SUCCESS;
}

