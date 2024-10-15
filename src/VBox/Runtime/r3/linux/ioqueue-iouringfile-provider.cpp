/* $Id: ioqueue-iouringfile-provider.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - I/O queue, Linux io_uring interface I/O file provider.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

/** @page pg_rtioqueue_linux     RTIoQueue - Linux io_uring implementation notes
 * @internal
 *
 * The io_uring interface is the most recent interface added to the Linux kernel
 * to deliver fast and efficient I/O. It was first added with kernel version 5.1 and is
 * thus not available on most systems as of writing this backend (July 2019).
 * It supersedes the old async I/O interface and cleans up with some restrictions like
 * having to disable caching for the file.
 * The interface is centered around a submission and completion queue to queue multiple new
 * requests for the kernel to process and get notified about completions to reduce the amount
 * of context switches to an absolute minimum. It also offers advanced features like
 * registering a fixed set of memory buffers for I/O upfront to reduce the processing overhead
 * even more.
 *
 * The first implementation will only make use of the basic features and more advanced features
 * will be added later.
 * The adept developer probably noticed that the public IPRT I/O queue API resembles the io_uring
 * interface in many aspects. This is not by accident but to reduce our own overhead as much as possible
 * while still keeping a consistent platform independent API which allows efficient implementations on
 * other hosts when they come up.
 *
 * The public kernel io_uring interface is completely defined in this file to avoid dragging in additional
 * dependencies and to avoid compile problems on older hosts missing the interface just like it is done
 * for the Linux RTFileAio* API  The necessary interface definitions and descriptions where retrieved from:
 *     * http://kernel.dk/io_uring.pdf
 *     * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/io_uring.h
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_IOQUEUE
#include <iprt/ioqueue.h>

#include <iprt/assertcompile.h>
#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include "internal/ioqueue.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The syscall number of io_uring_setup(). */
#define LNX_IOURING_SYSCALL_SETUP     425
/** The syscall number of io_uring_enter(). */
#define LNX_IOURING_SYSCALL_ENTER     426
/** The syscall number of io_uring_register(). */
#define LNX_IOURING_SYSCALL_REGISTER  427
/** eventfd2() syscall not associated with io_uring but used for kicking waiters. */
#define LNX_SYSCALL_EVENTFD2          290


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Linux io_uring completion event.
 */
typedef struct LNXIOURINGCQE
{
    /** Opaque user data associated with the completed request. */
    uint64_t                    u64User;
    /** The status code of the request. */
    int32_t                     rcLnx;
    /** Some flags which are not used as of now. */
    uint32_t                    fFlags;
} LNXIOURINGCQE;
AssertCompileSize(LNXIOURINGCQE, 16);
/** Pointer to a Linux io_uring completion event. */
typedef LNXIOURINGCQE *PLNXIOURINGCQE;
/** Pointer to a constant linux io_uring completion event. */
typedef const LNXIOURINGCQE *PCLNXIOURINGCQE;


/**
 * Linux io_uring submission queue entry.
 */
typedef struct LNXIOURINGSQE
{
    /** The opcode for the request. */
    uint8_t                     u8Opc;
    /** Common flags for the request. */
    uint8_t                     u8Flags;
    /** Assigned I/O priority. */
    uint16_t                    u16IoPrio;
    /** The file descriptor the request is for. */
    int32_t                     i32Fd;
    /** The start offset into the file for the request. */
    uint64_t                    u64OffStart;
    /** Buffer pointer or Pointer to io vector array depending on opcode. */
    uint64_t                    u64AddrBufIoVec;
    /** Size of the buffer in bytes or number of io vectors. */
    uint32_t                    u32BufIoVecSz;
    /** Opcode dependent data. */
    union
    {
        /** Flags for read/write requests. */
        uint32_t                u32KrnlRwFlags;
        /** Flags for fsync() like requests. */
        uint32_t                u32FsyncFlags;
        /** Flags for poll() like requests. */
        uint16_t                u16PollFlags;
        /** Flags for sync_file_range() like requests. */
        uint32_t                u32SyncFileRangeFlags;
        /** Flags for requests requiring a msg structure. */
        uint32_t                u32MsgFlags;
    } uOpc;
    /** Opaque user data associated with the request and returned durign completion. */
    uint64_t                    u64User;
    /** Request type dependent data. */
    union
    {
        /** Fixed buffer index if indicated by the request flags. */
        uint16_t                u16FixedBufIdx;
        /** Padding to align the structure to 64 bytes. */
        uint64_t                au64Padding[3];
    } uReq;
} LNXIOURINGSQE;
AssertCompileSize(LNXIOURINGSQE, 64);
/** Pointer to a Linux io_uring submission queue entry. */
typedef LNXIOURINGSQE *PLNXIOURINGSQE;
/** Pointer to a constant Linux io_uring submission queue entry. */
typedef const LNXIOURINGSQE *PCLNXIOURINGSQE;


/**
 * Linux u_ioring SQ ring header structure to maintain the queue.
 */
typedef struct LNXIOURINGSQ
{
    /** The current head position to fill in new requests. */
    uint32_t                    u32OffHead;
    /** The current tail position the kernel starts processing from. */
    uint32_t                    u32OffTail;
    /** The mask for the head and tail counters to apply to retrieve the index. */
    uint32_t                    u32OffRingMask;
    /** Number of entries in the SQ ring. */
    uint32_t                    u32OffRingEntries;
    /** Flags set asychronously by the kernel. */
    uint32_t                    u32OffFlags;
    /** Counter of dropped requests. */
    uint32_t                    u32OffDroppedReqs;
    /** Offset where to find the array of SQ entries. */
    uint32_t                    u32OffArray;
    /** Reserved. */
    uint32_t                    u32Rsvd0;
    /** Reserved. */
    uint64_t                    u64Rsvd1;
} LNXIOURINGSQ;
AssertCompileSize(LNXIOURINGSQ, 40);
/** Pointer to a Linux u_ioring SQ ring header. */
typedef LNXIOURINGSQ *PLNXIOURINGSQ;
/** Pointer to a constant Linux u_ioring SQ ring header. */
typedef const LNXIOURINGSQ *PCLNXIOURINGSQ;


/**
 * Linux io_uring CQ ring header structure to maintain the queue.
 */
typedef struct LNXIOURINGCQ
{
    /** The current head position the kernel modifies when completion events happen. */
    uint32_t                    u32OffHead;
    /** The current tail position to read completion events from. */
    uint32_t                    u32OffTail;
    /** The mask for the head and tail counters to apply to retrieve the index. */
    uint32_t                    u32OffRingMask;
    /** Number of entries in the CQ ring. */
    uint32_t                    u32OffRingEntries;
    /** Number of CQ overflows happened. */
    uint32_t                    u32OffOverflowCnt;
    /** */
    uint32_t                    u32OffCqes;
    /** Reserved. */
    uint64_t                    au64Rsvd0[2];
} LNXIOURINGCQ;
AssertCompileSize(LNXIOURINGCQ, 40);
/** Pointer to a Linux u_ioring CQ ring header. */
typedef LNXIOURINGCQ *PLNXIOURINGCQ;
/** Pointer to a constant Linux u_ioring CQ ring header. */
typedef const LNXIOURINGCQ *PCLNXIOURINGCQ;


/**
 * Linux io_uring parameters passed to io_uring_setup().
 */
typedef struct LNXIOURINGPARAMS
{
    /** Number of SQ entries requested, must be power of 2. */
    uint32_t                    u32SqEntriesCnt;
    /** Number of CQ entries requested, must be power of 2. */
    uint32_t                    u32CqEntriesCnt;
    /** Flags for the ring, , see LNX_IOURING_SETUP_F_*. */
    uint32_t                    u32Flags;
    /** Affinity of the kernel side SQ polling thread if enabled. */
    uint32_t                    u32SqPollCpu;
    /** Milliseconds after the kernel side SQ polling thread goes to sleep
     * if there is are no requests to process. */
    uint32_t                    u32SqPollIdleMs;
    /** Reserved. */
    uint32_t                    au32Rsvd0[5];
    /** Offsets returned for the submission queue. */
    LNXIOURINGSQ                SqOffsets;
    /** Offsets returned for the completion queue. */
    LNXIOURINGCQ                CqOffsets;
} LNXIOURINGPARAMS;
/** Pointer to Linux io_uring parameters. */
typedef LNXIOURINGPARAMS *PLNXIOURINGPARAMS;
/** Pointer to constant Linux io_uring parameters. */
typedef const LNXIOURINGPARAMS *PCLNXIOURINGPARAMS;


/** @name LNXIOURINGSQE::u8Opc defined opcodes.
 * @{ */
/** Opcode to profile the interface, does nothing. */
#define LNX_IOURING_OPC_NOP             0
/** preadv() like request. */
#define LNX_IOURING_OPC_READV           1
/** pwritev() like request. */
#define LNX_IOURING_OPC_WRITEV          2
/** fsync() like request. */
#define LNX_IOURING_OPC_FSYNC           3
/** Read request using a fixed preset buffer. */
#define LNX_IOURING_OPC_READ_FIXED      4
/** Write request using a fixed preset buffer. */
#define LNX_IOURING_OPC_WRITE_FIXED     5
/** Add file descriptor to pollset. */
#define LNX_IOURING_OPC_POLL_ADD        6
/** Remove file descriptor from pollset. */
#define LNX_IOURING_OPC_POLL_REMOVE     7
/** sync_file_range() like request. */
#define LNX_IOURING_OPC_SYNC_FILE_RANGE 8
/** sendmsg() like request. */
#define LNX_IOURING_OPC_SENDMSG         9
/** recvmsg() like request. */
#define LNX_IOURING_OPC_RECVMSG         10
/** @} */


/** @name Additional flags for LNX_IOURING_OPC_FSYNC requests.
 * @{ */
/** Sync userdata as well instead of metadata only. */
#define LNX_IOURING_OPC_FSYNC_DATASYNC  RT_BIT_32(0)
/** @} */


/** @name Flags for the LNX_IOURING_SYSCALL_SETUP syscall.
 * @{ */
/** The I/O context is polled. */
#define LNX_IOURING_SETUP_F_IOPOLL      RT_BIT_32(0)
/** The kernel should poll the submission queue. */
#define LNX_IOURING_SETUP_F_SQPOLL      RT_BIT_32(1)
/** Sets the CPU affinity of the kernel thread polling the submission queue. */
#define LNX_IOURING_SETUP_F_SQAFF       RT_BIT_32(2)
/** @} */


/** @name Flags for LNXIOURINGSQE::u8Flags.
 * @{ */
/** The file descriptor was registered before use. */
#define LNX_IOURING_SQE_F_FIXED_FILE    RT_BIT(0)
/** Complete all active requests before issuing the request with the flag set. */
#define LNX_IOURING_SQE_F_IO_DRAIN      RT_BIT(1)
/** Links the request with the flag set to the next one. */
#define LNX_IOURING_SQE_F_IO_LINK       RT_BIT(2)
/** @} */


/** @name Magic mmap offsets to map submission and completion queues.
 * @{ */
/** Used to map the submission queue. */
#define LNX_IOURING_MMAP_OFF_SQ         UINT64_C(0)
/** Used to map the completion queue. */
#define LNX_IOURING_MMAP_OFF_CQ         UINT64_C(0x8000000)
/** Used to map the submission queue entries array. */
#define LNX_IOURING_MMAP_OFF_SQES       UINT64_C(0x10000000)
/** @} */


/** @name Flags used for the SQ ring structure.
 * @{ */
/** The kernel thread needs a io_uring_enter() wakeup to continue processing requests. */
#define LNX_IOURING_SQ_RING_F_NEED_WAKEUP           RT_BIT_32(0)
/** @} */


/** @name Flags for the LNX_IOURING_SYSCALL_ENTER syscall.
 * @{ */
/** Retrieve completion events for the completion queue. */
#define LNX_IOURING_ENTER_F_GETEVENTS               RT_BIT_32(0)
/** Wakes the suspended kernel thread processing the requests. */
#define LNX_IOURING_ENTER_F_SQ_WAKEUP               RT_BIT_32(1)
/** @} */


/** @name Opcodes for the LNX_IOURING_SYSCALL_REGISTER syscall.
 * @{ */
/** Register a fixed set of buffers. */
#define LNX_IOURING_REGISTER_OPC_BUFFERS_REGISTER   0
/** Unregisters a fixed set of buffers registered previously. */
#define LNX_IOURING_REGISTER_OPC_BUFFERS_UNREGISTER 1
/** Register a fixed set of files. */
#define LNX_IOURING_REGISTER_OPC_FILES_REGISTER     2
/** Unregisters a fixed set of files registered previously. */
#define LNX_IOURING_REGISTER_OPC_FILES_UNREGISTER   3
/** Register an eventfd associated with the I/O ring. */
#define LNX_IOURING_REGISTER_OPC_EVENTFD_REGISTER   4
/** Unregisters an eventfd registered previously. */
#define LNX_IOURING_REGISTER_OPC_EVENTFD_UNREGISTER 5
/** @} */


/**
 * SQ ring structure.
 *
 * @note Some members of this structure point to memory shared with the kernel,
 *       hence the volatile keyword.
 */
typedef struct RTIOQUEUESQ
{
    /** Pointer to the head counter. */
    volatile uint32_t           *pidxHead;
    /** Pointer to the tail counter. */
    volatile uint32_t           *pidxTail;
    /** Mask to apply for the counters to get to the index. */
    uint32_t                    fRingMask;
    /** Number of entries in the ring. */
    uint32_t                    cEntries;
    /** Pointer to the global flags. */
    volatile uint32_t           *pfFlags;
    /** Pointer to the indirection array used for indexing the real SQ entries. */
    volatile uint32_t           *paidxSqes;
} RTIOQUEUESQ;


/**
 * CQ ring structure.
 *
 * @note Some members of this structure point to memory shared with the kernel,
 *       hence the volatile keyword.
 */
typedef struct RTIOQUEUECQ
{
    /** Pointer to the head counter. */
    volatile uint32_t           *pidxHead;
    /** Pointer to the tail counter. */
    volatile uint32_t           *pidxTail;
    /** Mask to apply for the counters to get to the index. */
    uint32_t                    fRingMask;
    /** Number of entries in the ring. */
    uint32_t                    cEntries;
    /** Pointer to the completion entry ring. */
    volatile LNXIOURINGCQE      *paCqes;
} RTIOQUEUECQ;


/**
 * Internal I/O queue provider instance data.
 */
typedef struct RTIOQUEUEPROVINT
{
    /** The io_uring file descriptor. */
    int                         iFdIoCtx;
    /** The eventfd file descriptor registered with the ring. */
    int                         iFdEvt;
    /** The submission queue. */
    RTIOQUEUESQ                 Sq;
    /** The currently uncommitted tail for the SQ. */
    uint32_t                    idxSqTail;
    /** Numbere of uncommitted SQEs. */
    uint32_t                    cSqesToCommit;
    /** The completion queue. */
    RTIOQUEUECQ                 Cq;
    /** Pointer to the mapped SQES entries. */
    PLNXIOURINGSQE              paSqes;
    /** Pointer to the iovec structure used for non S/G requests. */
    struct iovec                *paIoVecs;
    /** Pointer returned by mmap() for the SQ ring, used for unmapping. */
    void                        *pvMMapSqRing;
    /** Pointer returned by mmap() for the CQ ring, used for unmapping. */
    void                        *pvMMapCqRing;
    /** Pointer returned by mmap() for the SQ entries array, used for unmapping. */
    void                        *pvMMapSqes;
    /** Size of the mapped SQ ring, used for unmapping. */
    size_t                      cbMMapSqRing;
    /** Size of the mapped CQ ring, used for unmapping. */
    size_t                      cbMMapCqRing;
    /** Size of the mapped SQ entries array, used for unmapping. */
    size_t                      cbMMapSqes;
    /** Flag whether the waiter was woken up externally. */
    volatile bool               fExtIntr;
} RTIOQUEUEPROVINT;
/** Pointer to the internal I/O queue provider instance data. */
typedef RTIOQUEUEPROVINT *PRTIOQUEUEPROVINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Syscall wrapper for io_uring_setup().
 *
 * @returns IPRT status code.
 * @param   cEntries            Number of entries for submission and completion queues.
 * @param   pParams             Additional parameters for the I/O ring and updated return values
 *                              on success.
 * @param   piFdIoCtx           Where to store the file descriptor of the I/O ring on success.
 */
DECLINLINE(int) rtIoQueueLnxIoURingSetup(uint32_t cEntries, PLNXIOURINGPARAMS pParams, int32_t *piFdIoCtx)
{
    int rcLnx = syscall(LNX_IOURING_SYSCALL_SETUP, cEntries, pParams);
    if (RT_UNLIKELY(rcLnx == -1))
        return RTErrConvertFromErrno(errno);

    *piFdIoCtx = rcLnx;
    return VINF_SUCCESS;
}


/**
 * Syscall wrapper for io_uring_enter().
 *
 * @returns IPRT status code.
 * @param   iFdIoCtx            The I/O ring file descriptor.
 * @param   cToSubmit           Maximum number of requests waiting for processing.
 * @param   cMinComplete        Minimum number of completion events to accumulate before returning.
 * @param   fFlags              Flags for io_uring_enter(), see LNX_IOURING_ENTER_F_*.
 */
DECLINLINE(int) rtIoQueueLnxIoURingEnter(int32_t iFdIoCtx, uint32_t cToSubmit, uint32_t cMinComplete,
                                         uint32_t fFlags)
{
    int rcLnx = syscall(LNX_IOURING_SYSCALL_ENTER, iFdIoCtx, cToSubmit, cMinComplete, fFlags,
                        NULL, 0);
    if (RT_UNLIKELY(rcLnx == -1))
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


/**
 * Syscall wrapper for io_uring_register().
 *
 * @returns IPRT status code.
 * @param   iFdIoCtx            The I/O ring file descriptor.
 * @param   uOpc                Operation to perform, see LNX_IOURING_REGISTER_OPC_*.
 * @param   pvArg               Opaque arguments.
 * @param   cArgs               Number of arguments.
 */
DECLINLINE(int) rtIoQueueLnxIoURingRegister(int32_t iFdIoCtx, uint32_t uOpc, void *pvArg,
                                            uint32_t cArgs)
{
    int rcLnx = syscall(LNX_IOURING_SYSCALL_REGISTER, iFdIoCtx, uOpc, pvArg, cArgs);
    if (RT_UNLIKELY(rcLnx == -1))
        return RTErrConvertFromErrno(errno);

    return VINF_SUCCESS;
}


/**
 * mmap() wrapper for the common bits and returning an IPRT status code.
 *
 * @returns IPRT status code.
 * @param   iFdIoCtx            The I/O ring file descriptor.
 * @param   offMmap             The mmap() offset.
 * @param   cbMmap              How much to map.
 * @param   ppv                 Where to store the pointer to the mapping on success.
 */
DECLINLINE(int) rtIoQueueLnxIoURingMmap(int iFdIoCtx, off_t offMmap, size_t cbMmap, void **ppv)
{
    void *pv = mmap(0, cbMmap, PROT_READ | PROT_WRITE , MAP_SHARED | MAP_POPULATE, iFdIoCtx, offMmap);
    if (pv != MAP_FAILED)
    {
        *ppv = pv;
        return VINF_SUCCESS;
    }

    return RTErrConvertFromErrno(errno);
}


/**
 * eventfd2() syscall wrapper.
 *
 * @returns IPRT status code.
 * @param   uValInit            The initial value of the maintained counter.
 * @param   fFlags              Flags controlling the eventfd behavior.
 * @param   piFdEvt             Where to store the file descriptor of the eventfd object on success.
 */
DECLINLINE(int) rtIoQueueLnxEventfd2(uint32_t uValInit, uint32_t fFlags, int *piFdEvt)
{
    int rcLnx = syscall(LNX_SYSCALL_EVENTFD2, uValInit, fFlags);
    if (RT_UNLIKELY(rcLnx == -1))
        return RTErrConvertFromErrno(errno);

    *piFdEvt = rcLnx;
    return VINF_SUCCESS;
}


/**
 * Checks the completion event queue for pending events.
 *
 * @param   pThis               The provider instance.
 * @param   paCEvt              Pointer to the array of completion events.
 * @param   cCEvt               Maximum number of completion events the array can hold.
 * @param   pcCEvtSeen          Where to store the number of completion events processed.
 */
static void rtIoQueueLnxIoURingFileProvCqCheck(PRTIOQUEUEPROVINT pThis, PRTIOQUEUECEVT paCEvt,
                                               uint32_t cCEvt, uint32_t *pcCEvtSeen)
{
    /* The fencing and atomic accesses are kind of overkill and probably not required (dev paranoia). */
    ASMReadFence();
    uint32_t idxCqHead = ASMAtomicReadU32(pThis->Cq.pidxHead);
    uint32_t idxCqTail = ASMAtomicReadU32(pThis->Cq.pidxTail);
    ASMReadFence();

    uint32_t cCEvtSeen = 0;

    while (   idxCqTail != idxCqHead
           && cCEvtSeen < cCEvt)
    {
        /* Get the index. */
        uint32_t idxCqe = idxCqHead & pThis->Cq.fRingMask;
        volatile LNXIOURINGCQE *pCqe = &pThis->Cq.paCqes[idxCqe];

        paCEvt->pvUser = (void *)(uintptr_t)pCqe->u64User;
        if (pCqe->rcLnx >= 0)
        {
            paCEvt->rcReq    = VINF_SUCCESS;
            paCEvt->cbXfered = (size_t)pCqe->rcLnx;
        }
        else
            paCEvt->rcReq = RTErrConvertFromErrno(-pCqe->rcLnx);

#ifdef RT_STRICT /* poison */
        memset((void *)pCqe, 0xff, sizeof(*pCqe));
#endif

        paCEvt++;
        cCEvtSeen++;
        idxCqHead++;
    }

    *pcCEvtSeen = cCEvtSeen;

    /* Paranoia strikes again. */
    ASMWriteFence();
    ASMAtomicWriteU32(pThis->Cq.pidxHead, idxCqHead);
    ASMWriteFence();
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnIsSupported} */
static DECLCALLBACK(bool) rtIoQueueLnxIoURingFileProv_IsSupported(void)
{
    /*
     * Try to create a simple I/O ring and close it again.
     * The common code/public API already checked for the proper handle type.
     */
    int iFdIoCtx = 0;
    bool fSupp = false;
    LNXIOURINGPARAMS Params;
    RT_ZERO(Params);

    int rc = rtIoQueueLnxIoURingSetup(16, &Params, &iFdIoCtx);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check that we can register an eventfd descriptor to get notified about
         * completion events while being able to kick the waiter externally out of the wait.
         */
        int iFdEvt = 0;
        rc = rtIoQueueLnxEventfd2(0 /*uValInit*/, 0 /*fFlags*/, &iFdEvt);
        if (RT_SUCCESS(rc))
        {
            rc = rtIoQueueLnxIoURingRegister(iFdIoCtx, LNX_IOURING_REGISTER_OPC_EVENTFD_REGISTER,
                                             &iFdEvt, 1 /*cArgs*/);
            if (RT_SUCCESS(rc))
                fSupp = true;

            int rcLnx = close(iFdEvt); Assert(!rcLnx); RT_NOREF(rcLnx);
        }
        int rcLnx = close(iFdIoCtx); Assert(!rcLnx); RT_NOREF(rcLnx);
    }

    return fSupp;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnQueueInit} */
static DECLCALLBACK(int) rtIoQueueLnxIoURingFileProv_QueueInit(RTIOQUEUEPROV hIoQueueProv, uint32_t fFlags,
                                                               uint32_t cSqEntries, uint32_t cCqEntries)
{
    RT_NOREF(fFlags, cCqEntries);

    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    LNXIOURINGPARAMS Params;
    RT_ZERO(Params);

    pThis->cSqesToCommit = 0;
    pThis->fExtIntr      = false;

    int rc = rtIoQueueLnxIoURingSetup(cSqEntries, &Params, &pThis->iFdIoCtx);
    if (RT_SUCCESS(rc))
    {
        /* Map the rings into userspace. */
        pThis->cbMMapSqRing = Params.SqOffsets.u32OffArray + Params.u32SqEntriesCnt * sizeof(uint32_t);
        pThis->cbMMapCqRing = Params.CqOffsets.u32OffCqes + Params.u32CqEntriesCnt * sizeof(LNXIOURINGCQE);
        pThis->cbMMapSqes   = Params.u32SqEntriesCnt * sizeof(LNXIOURINGSQE);

        pThis->paIoVecs = (struct iovec *)RTMemAllocZ(Params.u32SqEntriesCnt * sizeof(struct iovec));
        if (RT_LIKELY(pThis->paIoVecs))
        {
            rc = rtIoQueueLnxEventfd2(0 /*uValInit*/, 0 /*fFlags*/, &pThis->iFdEvt);
            if (RT_SUCCESS(rc))
            {
                rc = rtIoQueueLnxIoURingRegister(pThis->iFdIoCtx, LNX_IOURING_REGISTER_OPC_EVENTFD_REGISTER, &pThis->iFdEvt, 1 /*cArgs*/);
                if (RT_SUCCESS(rc))
                {
                    rc = rtIoQueueLnxIoURingMmap(pThis->iFdIoCtx, LNX_IOURING_MMAP_OFF_SQ, pThis->cbMMapSqRing, &pThis->pvMMapSqRing);
                    if (RT_SUCCESS(rc))
                    {
                        rc = rtIoQueueLnxIoURingMmap(pThis->iFdIoCtx, LNX_IOURING_MMAP_OFF_CQ, pThis->cbMMapCqRing, &pThis->pvMMapCqRing);
                        if (RT_SUCCESS(rc))
                        {
                            rc = rtIoQueueLnxIoURingMmap(pThis->iFdIoCtx, LNX_IOURING_MMAP_OFF_SQES, pThis->cbMMapSqes, &pThis->pvMMapSqes);
                            if (RT_SUCCESS(rc))
                            {
                                uint8_t *pbTmp = (uint8_t *)pThis->pvMMapSqRing;

                                pThis->Sq.pidxHead  = (uint32_t *)(pbTmp + Params.SqOffsets.u32OffHead);
                                pThis->Sq.pidxTail  = (uint32_t *)(pbTmp + Params.SqOffsets.u32OffTail);
                                pThis->Sq.fRingMask = *(uint32_t *)(pbTmp + Params.SqOffsets.u32OffRingMask);
                                pThis->Sq.cEntries  = *(uint32_t *)(pbTmp + Params.SqOffsets.u32OffRingEntries);
                                pThis->Sq.pfFlags   = (uint32_t *)(pbTmp + Params.SqOffsets.u32OffFlags);
                                pThis->Sq.paidxSqes = (uint32_t *)(pbTmp + Params.SqOffsets.u32OffArray);
                                pThis->idxSqTail    = *pThis->Sq.pidxTail;

                                pThis->paSqes       = (PLNXIOURINGSQE)pThis->pvMMapSqes;

                                pbTmp = (uint8_t *)pThis->pvMMapCqRing;

                                pThis->Cq.pidxHead  = (uint32_t *)(pbTmp + Params.CqOffsets.u32OffHead);
                                pThis->Cq.pidxTail  = (uint32_t *)(pbTmp + Params.CqOffsets.u32OffTail);
                                pThis->Cq.fRingMask = *(uint32_t *)(pbTmp + Params.CqOffsets.u32OffRingMask);
                                pThis->Cq.cEntries  = *(uint32_t *)(pbTmp + Params.CqOffsets.u32OffRingEntries);
                                pThis->Cq.paCqes    = (PLNXIOURINGCQE)(pbTmp + Params.CqOffsets.u32OffCqes);
                                return VINF_SUCCESS;
                            }

                            munmap(pThis->pvMMapCqRing, pThis->cbMMapCqRing);
                        }

                        munmap(pThis->pvMMapSqRing, pThis->cbMMapSqRing);
                    }

                    rc = rtIoQueueLnxIoURingRegister(pThis->iFdIoCtx, LNX_IOURING_REGISTER_OPC_EVENTFD_UNREGISTER, NULL, 0);
                    AssertRC(rc);
                }

                close(pThis->iFdEvt);
            }

            RTMemFree(pThis->paIoVecs);
        }

        int rcLnx = close(pThis->iFdIoCtx); Assert(!rcLnx); RT_NOREF(rcLnx);
    }

    return rc;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnQueueDestroy} */
static DECLCALLBACK(void) rtIoQueueLnxIoURingFileProv_QueueDestroy(RTIOQUEUEPROV hIoQueueProv)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    int rcLnx = munmap(pThis->pvMMapSqRing, pThis->cbMMapSqRing); Assert(!rcLnx); RT_NOREF(rcLnx);
    rcLnx = munmap(pThis->pvMMapCqRing, pThis->cbMMapCqRing); Assert(!rcLnx); RT_NOREF(rcLnx);
    rcLnx = munmap(pThis->pvMMapSqes, pThis->cbMMapSqes); Assert(!rcLnx); RT_NOREF(rcLnx);

    int rc = rtIoQueueLnxIoURingRegister(pThis->iFdIoCtx, LNX_IOURING_REGISTER_OPC_EVENTFD_UNREGISTER, NULL, 0);
    AssertRC(rc);

    close(pThis->iFdEvt);
    close(pThis->iFdIoCtx);
    RTMemFree(pThis->paIoVecs);

    RT_ZERO(pThis);
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnHandleRegister} */
static DECLCALLBACK(int) rtIoQueueLnxIoURingFileProv_HandleRegister(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle)
{
    RT_NOREF(hIoQueueProv, pHandle);
    /** @todo Add support for fixed file sets later. */
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnHandleDeregister} */
static DECLCALLBACK(int) rtIoQueueLnxIoURingFileProv_HandleDeregister(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle)
{
    RT_NOREF(hIoQueueProv, pHandle);
    /** @todo Add support for fixed file sets later. */
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnReqPrepare} */
static DECLCALLBACK(int) rtIoQueueLnxIoURingFileProv_ReqPrepare(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle, RTIOQUEUEOP enmOp,
                                                                uint64_t off, void *pvBuf, size_t cbBuf, uint32_t fReqFlags,
                                                                void *pvUser)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    RT_NOREF(fReqFlags);

    uint32_t idx = pThis->idxSqTail & pThis->Sq.fRingMask;
    PLNXIOURINGSQE pSqe = &pThis->paSqes[idx];
    struct iovec *pIoVec = &pThis->paIoVecs[idx];

    pIoVec->iov_base = pvBuf;
    pIoVec->iov_len  = cbBuf;

    pSqe->u8Flags         = 0;
    pSqe->u16IoPrio       = 0;
    pSqe->i32Fd           = (int32_t)RTFileToNative(pHandle->u.hFile);
    pSqe->u64OffStart     = off;
    pSqe->u64AddrBufIoVec = (uint64_t)(uintptr_t)pIoVec;
    pSqe->u32BufIoVecSz   = 1;
    pSqe->u64User         = (uint64_t)(uintptr_t)pvUser;

    switch (enmOp)
    {
        case RTIOQUEUEOP_READ:
            pSqe->u8Opc               = LNX_IOURING_OPC_READV;
            pSqe->uOpc.u32KrnlRwFlags = 0;
            break;
        case RTIOQUEUEOP_WRITE:
            pSqe->u8Opc               = LNX_IOURING_OPC_WRITEV;
            pSqe->uOpc.u32KrnlRwFlags = 0;
            break;
        case RTIOQUEUEOP_SYNC:
            pSqe->u8Opc              = LNX_IOURING_OPC_FSYNC;
            pSqe->uOpc.u32FsyncFlags = 0;
            break;
        default:
            AssertMsgFailedReturn(("Invalid I/O queue operation: %d\n", enmOp),
                                  VERR_INVALID_PARAMETER);
    }

    pThis->Sq.paidxSqes[idx] = idx;
    pThis->idxSqTail++;
    pThis->cSqesToCommit++;
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnCommit} */
static DECLCALLBACK(int) rtIoQueueLnxIoURingFileProv_Commit(RTIOQUEUEPROV hIoQueueProv, uint32_t *pcReqsCommitted)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    ASMWriteFence();
    ASMAtomicWriteU32(pThis->Sq.pidxTail, pThis->idxSqTail);
    ASMWriteFence();

    int rc = rtIoQueueLnxIoURingEnter(pThis->iFdIoCtx, pThis->cSqesToCommit, 0, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        *pcReqsCommitted = pThis->cSqesToCommit;
        pThis->cSqesToCommit = 0;
    }

    return rc;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnEvtWait} */
static DECLCALLBACK(int) rtIoQueueLnxIoURingFileProv_EvtWait(RTIOQUEUEPROV hIoQueueProv, PRTIOQUEUECEVT paCEvt, uint32_t cCEvt,
                                                             uint32_t cMinWait, uint32_t *pcCEvt, uint32_t fFlags)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    int rc = VINF_SUCCESS;
    uint32_t cCEvtSeen = 0;

    RT_NOREF(fFlags);

    /*
     * Check the completion queue first for any completed events which might save us a
     * context switch later on.
     */
    rtIoQueueLnxIoURingFileProvCqCheck(pThis, paCEvt, cCEvt, &cCEvtSeen);

    while (   cCEvtSeen < cMinWait
           && RT_SUCCESS(rc))
    {
        /*
         * We can employ a blocking read on the event file descriptor, it will return
         * either when woken up externally or when there are completion events pending.
         */
        uint64_t uCnt = 0; /**< The counter value returned upon a successful read(). */
        ssize_t rcLnx = read(pThis->iFdEvt, &uCnt, sizeof(uCnt));
        if (rcLnx == sizeof(uCnt))
        {
            uint32_t cCEvtThisSeen = 0;
            rtIoQueueLnxIoURingFileProvCqCheck(pThis, &paCEvt[cCEvtSeen], cCEvt - cCEvtSeen, &cCEvtThisSeen);
            cCEvtSeen += cCEvtThisSeen;

            /* Whether we got woken up externally. */
            if (ASMAtomicXchgBool(&pThis->fExtIntr, false))
                rc = VERR_INTERRUPTED;
        }
        else if (rcLnx == -1)
            rc = RTErrConvertFromErrno(errno);
        else
            AssertMsgFailed(("Unexpected read() -> 0\n"));
    }

    *pcCEvt = cCEvtSeen;
    return rc;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnEvtWaitWakeup} */
static DECLCALLBACK(int) rtIoQueueLnxIoURingFileProv_EvtWaitWakeup(RTIOQUEUEPROV hIoQueueProv)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    int rc = VINF_SUCCESS;

    if (!ASMAtomicXchgBool(&pThis->fExtIntr, true))
    {
        const uint64_t uValAdd = 1;
        ssize_t rcLnx = write(pThis->iFdEvt, &uValAdd, sizeof(uValAdd));

        Assert(rcLnx == -1 || rcLnx == sizeof(uValAdd));
        if (rcLnx == -1)
            rc = RTErrConvertFromErrno(errno);
    }

    return rc;
}


/**
 * Async file I/O queue provider virtual method table.
 */
RT_DECL_DATA_CONST(RTIOQUEUEPROVVTABLE const) g_RTIoQueueLnxIoURingProv =
{
    /** uVersion */
    RTIOQUEUEPROVVTABLE_VERSION,
    /** pszId */
    "LnxIoURingFile",
    /** cbIoQueueProv */
    sizeof(RTIOQUEUEPROVINT),
    /** enmHnd */
    RTHANDLETYPE_FILE,
    /** fFlags */
    0,
    /** pfnIsSupported */
    rtIoQueueLnxIoURingFileProv_IsSupported,
    /** pfnQueueInit  */
    rtIoQueueLnxIoURingFileProv_QueueInit,
    /** pfnQueueDestroy */
    rtIoQueueLnxIoURingFileProv_QueueDestroy,
    /** pfnHandleRegister */
    rtIoQueueLnxIoURingFileProv_HandleRegister,
    /** pfnHandleDeregister */
    rtIoQueueLnxIoURingFileProv_HandleDeregister,
    /** pfnReqPrepare */
    rtIoQueueLnxIoURingFileProv_ReqPrepare,
    /** pfnReqPrepareSg */
    NULL,
    /** pfnCommit */
    rtIoQueueLnxIoURingFileProv_Commit,
    /** pfnEvtWait */
    rtIoQueueLnxIoURingFileProv_EvtWait,
    /** pfnEvtWaitWakeup */
    rtIoQueueLnxIoURingFileProv_EvtWaitWakeup,
    /** uEndMarker */
    RTIOQUEUEPROVVTABLE_VERSION
};

