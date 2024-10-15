/* $Id: file.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders - File System Driver file routines.
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
#include "vbsf.h"
#include <iprt/fs.h>
#include <iprt/mem.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** How many pages we should try transfer in one I/O request (read/write). */
#define VBSF_MAX_IO_PAGES   RT_MIN(_16K / sizeof(RTGCPHYS64) /* => 8MB buffer */, VMMDEV_MAX_HGCM_DATA_SIZE >> PAGE_SHIFT)




/** @name HACK ALERT! Using the better CcCoherencyFlushAndPurgeCache when
 *        available (>= Windows 7) and avoid flushing+purging cache twice.
 *
 * We change the cache flushing and purging related imports from the write.obj
 * and read.obj files in the rdbsslib.lib to import so these gets redirected
 * here instead of going directly to ntoskrnl.  We will use
 * CcCoherencyFlushAndPurgeCache when present, and on older systems there will
 * be no change.  This does however save us from doing double flushing and
 * purging on newer systems.
 *
 * If we don't use CcCoherencyFlushAndPurgeCache we end up not seeing newly
 * written data in memory mappings, and similarlly not seeing data from freshly
 * dirtied (but as yet unflushed) memory mapping pages when reading.  (Both
 * these scenarios are tested by FsPerf --mmap.)
 *
 * See VBoxEditCoffLib and the Makefile.kmk for the rest of the puzzle.
 *
 * @todo investigate whether we could do it the same way as we do on linux,
 *       where we iterrogate the cache and use cached data when memory mappings
 *       are active.  Only troubles are:
 *
 *          1. Don't know how to find out whether we've got memory mappings.
 *
 *          2. Don't know how to detect dirty pages (we should only read
 *             from dirty ones).
 *
 *       To really explore this, it would be best to introduce a caching mode
 *       mount parameter (or something) analogous to what we have on linux.  In
 *       the relaxed mode, we could get away with more as users could always
 *       disable caching...
 * @{
 */

/** For reads. */
static VOID NTAPI vbsfNtReadCcFlushCache(PSECTION_OBJECT_POINTERS pSectObjPtrs, PLARGE_INTEGER poffFlush, ULONG cbFlush,
                                         PIO_STATUS_BLOCK pIos)
{
    if (g_pfnCcCoherencyFlushAndPurgeCache)
        g_pfnCcCoherencyFlushAndPurgeCache(pSectObjPtrs, poffFlush, cbFlush, pIos, CC_FLUSH_AND_PURGE_NO_PURGE);
    else
        CcFlushCache(pSectObjPtrs, poffFlush, cbFlush, pIos);
}


/**
 * For writes with mmapping/caching section, called before the purging.
 *
 * This does both flushing and puring when CcCoherencyFlushAndPurgeCache is
 * available.
 */
static VOID NTAPI vbsfNtWriteCcFlushCache(PSECTION_OBJECT_POINTERS pSectObjPtrs, PLARGE_INTEGER poffFlush, ULONG cbFlush,
                                          PIO_STATUS_BLOCK pIos)
{
    if (g_pfnCcCoherencyFlushAndPurgeCache)
        g_pfnCcCoherencyFlushAndPurgeCache(pSectObjPtrs, poffFlush, cbFlush, pIos, 0 /*fFlags*/);
    else
        CcFlushCache(pSectObjPtrs, poffFlush, cbFlush, pIos);
}


/**
 * For writes with mmapping/caching section, called to purge after flushing.
 *
 * We translate this to a no-op when CcCoherencyFlushAndPurgeCache is available.
 */
static BOOLEAN NTAPI vbsfNtWriteCcPurgeCacheSection(PSECTION_OBJECT_POINTERS pSectObjPtrs, PLARGE_INTEGER poffPurge,ULONG cbPurge,
#if (NTDDI_VERSION >= NTDDI_VISTA)
                                                    ULONG fUninitializeCacheMaps)
#else
                                                    BOOLEAN fUninitializeCacheMaps)
#endif
{
#if (NTDDI_VERSION >= NTDDI_VISTA)
    fUninitializeCacheMaps &= 0xff; /* Used to be BOOLEAN before Vista. */
#endif
    Assert(fUninitializeCacheMaps == 0);
    BOOLEAN fRet;
    if (g_pfnCcCoherencyFlushAndPurgeCache)
        fRet = TRUE;
    else
        fRet = CcPurgeCacheSection(pSectObjPtrs, poffPurge, cbPurge, fUninitializeCacheMaps);
    return fRet;
}

extern "C" {
/** This is what read.obj gets instead of __imp_CcFlushCache. */
decltype(CcFlushCache)        *g_pfnRdFlushCache        = vbsfNtReadCcFlushCache;
/** This is what write.obj gets instead of __imp_CcFlushCache. */
decltype(CcFlushCache)        *g_pfnWrFlushCache        = vbsfNtWriteCcFlushCache;
/** This is what write.obj gets instead of __imp_CcPurgeCacheSection. */
decltype(CcPurgeCacheSection) *g_pfnWrPurgeCacheSection = vbsfNtWriteCcPurgeCacheSection;
}

/** @} */



/**
 * Performs a read.
 *
 * @note Almost identical to vbsfNtWriteWorker.
 */
static NTSTATUS vbsfNtReadWorker(PRX_CONTEXT RxContext)
{
    RxCaptureFcb;
    RxCaptureFobx;
    PMRX_VBOX_NETROOT_EXTENSION pNetRootX  = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PVBSFNTFCBEXT               pVBoxFcbX  = VBoxMRxGetFcbExtension(capFcb);
    PMRX_VBOX_FOBX              pVBoxFobX  = VBoxMRxGetFileObjectExtension(capFobx);
    PMDL                        pBufferMdl = RxContext->LowIoContext.ParamsFor.ReadWrite.Buffer;

    LogFlow(("vbsfNtReadWorker: hFile=%#RX64 offFile=%#RX64 cbToRead=%#x %s\n", pVBoxFobX->hFile,
             RxContext->LowIoContext.ParamsFor.ReadWrite.ByteOffset, RxContext->LowIoContext.ParamsFor.ReadWrite.ByteCount,
             RxContext->Flags & RX_CONTEXT_FLAG_ASYNC_OPERATION ? " async" : "sync"));

    AssertReturn(pBufferMdl,  STATUS_INTERNAL_ERROR);


    /*
     * We should never get a zero byte request (RDBSS checks), but in case we
     * do, it should succeed.
     */
    uint32_t cbRet  = 0;
    uint32_t cbLeft = RxContext->LowIoContext.ParamsFor.ReadWrite.ByteCount;
    AssertReturnStmt(cbLeft > 0, RxContext->InformationToReturn = 0, STATUS_SUCCESS);

    Assert(cbLeft <= MmGetMdlByteCount(pBufferMdl));

    /*
     * Allocate a request buffer.
     */
    uint32_t            cPagesLeft = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(pBufferMdl), cbLeft);
    uint32_t            cMaxPages  = RT_MIN(cPagesLeft, VBSF_MAX_IO_PAGES);
    VBOXSFREADPGLSTREQ *pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ,
                                                                                          PgLst.aPages[cMaxPages]));
    while (!pReq && cMaxPages > 4)
    {
        cMaxPages /= 2;
        pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ, PgLst.aPages[cMaxPages]));
    }
    NTSTATUS rcNt = STATUS_SUCCESS;
    if (pReq)
    {
        /*
         * The read loop.
         */
        RTFOFF      offFile = RxContext->LowIoContext.ParamsFor.ReadWrite.ByteOffset;
        PPFN_NUMBER paPfns  = MmGetMdlPfnArray(pBufferMdl);
        uint32_t    offPage = MmGetMdlByteOffset(pBufferMdl);
        if (offPage < PAGE_SIZE)
        { /* likely */ }
        else
        {
            paPfns  += offPage >> PAGE_SHIFT;
            offPage &= PAGE_OFFSET_MASK;
        }

        for (;;)
        {
            /*
             * Figure out how much to process now and set up the page list for it.
             */
            uint32_t cPagesInChunk;
            uint32_t cbChunk;
            if (cPagesLeft <= cMaxPages)
            {
                cPagesInChunk = cPagesLeft;
                cbChunk       = cbLeft;
            }
            else
            {
                cPagesInChunk = cMaxPages;
                cbChunk       = (cMaxPages << PAGE_SHIFT) - offPage;
            }

            size_t iPage = cPagesInChunk;
            while (iPage-- > 0)
                pReq->PgLst.aPages[iPage] = (RTGCPHYS)paPfns[iPage] << PAGE_SHIFT;
            pReq->PgLst.offFirstPage = offPage;

#if 0 /* Instead we hook into read.obj's import function pointers to do this more efficiently. */
            /*
             * Flush dirty cache content before we try read it from the host.  RDBSS calls
             * CcFlushCache before it calls us, I think, but CcCoherencyFlushAndPurgeCache
             * does the right thing whereas CcFlushCache clearly does (FsPerf mmap+read
             * coherency test fails consistently on W10, XP, ++).
             */
            if (   g_pfnCcCoherencyFlushAndPurgeCache
                && !(RxContext->CurrentIrp && (RxContext->CurrentIrp->Flags & IRP_PAGING_IO))
                && RxContext->NonPagedFcb != NULL
                && RxContext->NonPagedFcb->SectionObjectPointers.DataSectionObject != NULL)
            {
                LARGE_INTEGER offFlush;
                offFlush.QuadPart = offFile;
                Assert(!RxContext->FcbPagingIoResourceAcquired);
                BOOLEAN AcquiredFile = RxAcquirePagingIoResourceShared(NULL, capFcb, 1 /*fWait*/);
                g_pfnCcCoherencyFlushAndPurgeCache(&RxContext->NonPagedFcb->SectionObjectPointers, &offFlush, cbChunk,
                                                   &RxContext->CurrentIrp->IoStatus, CC_FLUSH_AND_PURGE_NO_PURGE);
                if (AcquiredFile)
                {   RxReleasePagingIoResource(NULL, capFcb); /* requires {} */ }
            }
#endif

            /*
             * Issue the request and unlock the pages.
             */
            int vrc = VbglR0SfHostReqReadPgLst(pNetRootX->map.root, pReq, pVBoxFobX->hFile, offFile, cbChunk, cPagesInChunk);
            if (RT_SUCCESS(vrc))
            {
                /*
                 * Success, advance position and buffer.
                 */
                uint32_t cbActual = pReq->Parms.cb32Read.u.value32;
                AssertStmt(cbActual <= cbChunk, cbActual = cbChunk);
                cbRet   += cbActual;
                offFile += cbActual;
                cbLeft  -= cbActual;

                /*
                 * Update timestamp state (FCB is shared).
                 */
                pVBoxFobX->fTimestampsImplicitlyUpdated |= VBOX_FOBX_F_INFO_LASTACCESS_TIME;
                if (pVBoxFcbX->pFobxLastAccessTime != pVBoxFobX)
                    pVBoxFcbX->pFobxLastAccessTime = NULL;

                /*
                 * Are we done already?
                 */
                if (!cbLeft || cbActual < cbChunk)
                {
                    /*
                     * Flag EOF.
                     */
                    if (cbActual != 0 || cbRet != 0)
                    { /* typical */ }
                    else
                        rcNt = STATUS_END_OF_FILE;

                    /*
                     * See if we've reached the EOF early or read beyond what we thought were the EOF.
                     *
                     * Note! We don't dare do this (yet) if we're in paging I/O as we then hold the
                     *       PagingIoResource in shared mode and would probably deadlock in the
                     *       updating code when taking the lock in exclusive mode.
                     */
                    if (RxContext->LowIoContext.Resource != capFcb->Header.PagingIoResource)
                    {
                        LONGLONG cbFileRdbss;
                        RxGetFileSizeWithLock((PFCB)capFcb, &cbFileRdbss);
                        if (   offFile < cbFileRdbss
                            && cbActual < cbChunk /* hit EOF */)
                            vbsfNtUpdateFcbSize(RxContext->pFobx->AssociatedFileObject, capFcb, pVBoxFobX, offFile, cbFileRdbss, -1);
                        else if (offFile > cbFileRdbss)
                            vbsfNtQueryAndUpdateFcbSize(pNetRootX, RxContext->pFobx->AssociatedFileObject,
                                                        pVBoxFobX, capFcb, pVBoxFcbX);
                    }
                    break;
                }

                /*
                 * More to read, advance page related variables and loop.
                 */
                paPfns     += cPagesInChunk;
                cPagesLeft -= cPagesInChunk;
                offPage     = 0;
            }
            else if (vrc == VERR_NO_MEMORY && cMaxPages > 4)
            {
                /*
                 * The host probably doesn't have enough heap to handle the
                 * request, reduce the page count and retry.
                 */
                cMaxPages /= 4;
                Assert(cMaxPages > 0);
            }
            else
            {
                /*
                 * If we've successfully read stuff, return it rather than
                 * the error.  (Not sure if this is such a great idea...)
                 */
                if (cbRet > 0)
                    Log(("vbsfNtReadWorker: read at %#RX64 -> %Rrc; got cbRet=%#zx already\n", offFile, vrc, cbRet));
                else
                {
                    rcNt = vbsfNtVBoxStatusToNt(vrc);
                    Log(("vbsfNtReadWorker: read at %#RX64 -> %Rrc (rcNt=%#x)\n", offFile, vrc, rcNt));
                }
                break;
            }

        }

        VbglR0PhysHeapFree(pReq);
    }
    else
        rcNt = STATUS_INSUFFICIENT_RESOURCES;
    RxContext->InformationToReturn = cbRet;
    LogFlow(("vbsfNtReadWorker: returns %#x cbRet=%#x @ %#RX64\n",
             rcNt, cbRet, RxContext->LowIoContext.ParamsFor.ReadWrite.ByteOffset));
    return rcNt;
}

/**
 * Wrapper for RxDispatchToWorkerThread().
 */
static VOID vbsfNtReadThreadWorker(VOID *pv)
{
    PRX_CONTEXT RxContext = (PRX_CONTEXT)pv;

    Log(("VBOXSF: vbsfNtReadThreadWorker: calling the worker\n"));

    RxContext->IoStatusBlock.Status = vbsfNtReadWorker(RxContext);

    Log(("VBOXSF: vbsfNtReadThreadWorker: Status 0x%08X\n",
         RxContext->IoStatusBlock.Status));

    RxLowIoCompletion(RxContext);
}

/**
 * Read stuff from a file.
 *
 * Prior to calling us, RDBSS will have:
 *  - Called CcFlushCache() for uncached accesses.
 *  - For non-paging access the Fcb.Header.Resource lock in shared mode in one
 *    way or another (ExAcquireResourceSharedLite,
 *    ExAcquireSharedWaitForExclusive).
 *  - For paging the FCB isn't, but the Fcb.Header.PagingResource is taken
 *    in shared mode (ExAcquireResourceSharedLite).
 *
 * Upon completion, it will update the file pointer if applicable.  There are no
 * EOF checks and corresponding file size updating like in the write case, so
 * that's something we have to do ourselves it seems since the library relies on
 * the size information to be accurate in a few places (set EOF, cached reads).
 */
NTSTATUS VBoxMRxRead(IN PRX_CONTEXT RxContext)
{
    NTSTATUS Status;

    /* If synchronous operation, keep it on this thread (RDBSS already checked
       if we've got enough stack before calling us).   */
    if (!(RxContext->Flags & RX_CONTEXT_FLAG_ASYNC_OPERATION))
    {
        RxContext->IoStatusBlock.Status = Status = vbsfNtReadWorker(RxContext);
        Assert(Status != STATUS_PENDING);

        Log(("VBOXSF: VBoxMRxRead: vbsfNtReadWorker: Status %#08X\n", Status));
    }
    else
    {
        Status = RxDispatchToWorkerThread(VBoxMRxDeviceObject, DelayedWorkQueue, vbsfNtReadThreadWorker, RxContext);

        Log(("VBOXSF: VBoxMRxRead: RxDispatchToWorkerThread: Status 0x%08X\n", Status));

        if (Status == STATUS_SUCCESS)
            Status = STATUS_PENDING;
    }

    return Status;
}

/**
 * Performs a write.
 *
 * @note Almost identical to vbsfNtReadWorker.
 */
static NTSTATUS vbsfNtWriteWorker(PRX_CONTEXT RxContext)
{
    RxCaptureFcb;
    RxCaptureFobx;
    PMRX_VBOX_NETROOT_EXTENSION pNetRootX  = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PVBSFNTFCBEXT               pVBoxFcbX  = VBoxMRxGetFcbExtension(capFcb);
    PMRX_VBOX_FOBX              pVBoxFobX  = VBoxMRxGetFileObjectExtension(capFobx);
    PMDL                        pBufferMdl = RxContext->LowIoContext.ParamsFor.ReadWrite.Buffer;

    LogFlow(("vbsfNtWriteWorker: hFile=%#RX64 offFile=%#RX64 cbToWrite=%#x %s\n", pVBoxFobX->hFile,
             RxContext->LowIoContext.ParamsFor.ReadWrite.ByteOffset, RxContext->LowIoContext.ParamsFor.ReadWrite.ByteCount,
             RxContext->Flags & RX_CONTEXT_FLAG_ASYNC_OPERATION ? " async" : "sync"));

    AssertReturn(pBufferMdl,  STATUS_INTERNAL_ERROR);

    /*
     * We should never get a zero byte request (RDBSS checks), but in case we
     * do, it should succeed.
     */
    uint32_t cbRet  = 0;
    uint32_t cbLeft = RxContext->LowIoContext.ParamsFor.ReadWrite.ByteCount;
    AssertReturnStmt(cbLeft > 0, RxContext->InformationToReturn = 0, STATUS_SUCCESS);

    Assert(cbLeft <= MmGetMdlByteCount(pBufferMdl));

    /*
     * Allocate a request buffer.
     */
    uint32_t             cPagesLeft = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(pBufferMdl), cbLeft);
    uint32_t             cMaxPages  = RT_MIN(cPagesLeft, VBSF_MAX_IO_PAGES);
    VBOXSFWRITEPGLSTREQ *pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFWRITEPGLSTREQ,
                                                                                            PgLst.aPages[cMaxPages]));
    while (!pReq && cMaxPages > 4)
    {
        cMaxPages /= 2;
        pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFWRITEPGLSTREQ, PgLst.aPages[cMaxPages]));
    }
    NTSTATUS rcNt = STATUS_SUCCESS;
    if (pReq)
    {
        /*
         * The write loop.
         */
        RTFOFF      offFile = RxContext->LowIoContext.ParamsFor.ReadWrite.ByteOffset;
        PPFN_NUMBER paPfns  = MmGetMdlPfnArray(pBufferMdl);
        uint32_t    offPage = MmGetMdlByteOffset(pBufferMdl);
        if (offPage < PAGE_SIZE)
        { /* likely */ }
        else
        {
            paPfns  += offPage >> PAGE_SHIFT;
            offPage &= PAGE_OFFSET_MASK;
        }

        for (;;)
        {
            /*
             * Figure out how much to process now and set up the page list for it.
             */
            uint32_t cPagesInChunk;
            uint32_t cbChunk;
            if (cPagesLeft <= cMaxPages)
            {
                cPagesInChunk = cPagesLeft;
                cbChunk       = cbLeft;
            }
            else
            {
                cPagesInChunk = cMaxPages;
                cbChunk       = (cMaxPages << PAGE_SHIFT) - offPage;
            }

            size_t iPage = cPagesInChunk;
            while (iPage-- > 0)
                pReq->PgLst.aPages[iPage] = (RTGCPHYS)paPfns[iPage] << PAGE_SHIFT;
            pReq->PgLst.offFirstPage = offPage;

#if 0 /* Instead we hook into write.obj's import function pointers to do this more efficiently. */
            /*
             * Flush and purge the cache range we're touching upon now, provided we can and
             * really needs to.  The CcCoherencyFlushAndPurgeCache API seems to work better
             * than the CcFlushCache + CcPurgeCacheSection that RDBSS does before calling us.
             */
            if (   g_pfnCcCoherencyFlushAndPurgeCache
                && !(RxContext->CurrentIrp && (RxContext->CurrentIrp->Flags & IRP_PAGING_IO))
                && RxContext->NonPagedFcb != NULL
                && RxContext->NonPagedFcb->SectionObjectPointers.DataSectionObject != NULL)
            {
                LARGE_INTEGER offFlush;
                offFlush.QuadPart = offFile;
                BOOLEAN fAcquiredLock = RxAcquirePagingIoResource(NULL, capFcb);
                g_pfnCcCoherencyFlushAndPurgeCache(&RxContext->NonPagedFcb->SectionObjectPointers, &offFlush, cbChunk,
                                                   &RxContext->CurrentIrp->IoStatus, 0 /*fFlags*/);
                if (fAcquiredLock)
                {   RxReleasePagingIoResource(NULL, capFcb); /* requires {} */ }
            }
#endif

            /*
             * Issue the request and unlock the pages.
             */
            int vrc = VbglR0SfHostReqWritePgLst(pNetRootX->map.root, pReq, pVBoxFobX->hFile, offFile, cbChunk, cPagesInChunk);
            if (RT_SUCCESS(vrc))
            {
                /*
                 * Success, advance position and buffer.
                 */
                uint32_t cbActual = pReq->Parms.cb32Write.u.value32;
                AssertStmt(cbActual <= cbChunk, cbActual = cbChunk);
                cbRet   += cbActual;
                offFile += cbActual;
                cbLeft  -= cbActual;

                /*
                 * Update timestamp state (FCB is shared).
                 */
                pVBoxFobX->fTimestampsImplicitlyUpdated |= VBOX_FOBX_F_INFO_LASTWRITE_TIME;
                if (pVBoxFcbX->pFobxLastWriteTime != pVBoxFobX)
                    pVBoxFcbX->pFobxLastWriteTime = NULL;

                /*
                 * Are we done already?
                 */
                if (!cbLeft || cbActual < cbChunk)
                {
                    /*
                     * Make sure our cached file size value is up to date (RDBSS takes care
                     * of the ones in the FCB as well as the cache manager).
                     */
                    if (cbRet > 0)
                    {
                        if (pVBoxFobX->Info.cbObject < offFile)
                            pVBoxFobX->Info.cbObject = offFile;

                        if (pVBoxFobX->Info.cbAllocated < offFile)
                        {
                            pVBoxFobX->Info.cbAllocated = offFile;
                            pVBoxFobX->nsUpToDate       = 0;
                        }
                    }
                    break;
                }

                /*
                 * More to write, advance page related variables and loop.
                 */
                paPfns     += cPagesInChunk;
                cPagesLeft -= cPagesInChunk;
                offPage     = 0;
            }
            else if (vrc == VERR_NO_MEMORY && cMaxPages > 4)
            {
                /*
                 * The host probably doesn't have enough heap to handle the
                 * request, reduce the page count and retry.
                 */
                cMaxPages /= 4;
                Assert(cMaxPages > 0);
            }
            else
            {
                /*
                 * If we've successfully written stuff, return it rather than
                 * the error.  (Not sure if this is such a great idea...)
                 */
                if (cbRet > 0)
                    Log(("vbsfNtWriteWorker: write at %#RX64 -> %Rrc; got cbRet=%#zx already\n", offFile, vrc, cbRet));
                else
                {
                    rcNt = vbsfNtVBoxStatusToNt(vrc);
                    Log(("vbsfNtWriteWorker: write at %#RX64 -> %Rrc (rcNt=%#x)\n", offFile, vrc, rcNt));
                }
                break;
            }

        }

        VbglR0PhysHeapFree(pReq);
    }
    else
        rcNt = STATUS_INSUFFICIENT_RESOURCES;
    RxContext->InformationToReturn = cbRet;
    LogFlow(("vbsfNtWriteWorker: returns %#x cbRet=%#x @ %#RX64\n",
             rcNt, cbRet, RxContext->LowIoContext.ParamsFor.ReadWrite.ByteOffset));
    return rcNt;
}

/**
 * Wrapper for RxDispatchToWorkerThread().
 */
static VOID vbsfNtWriteThreadWorker(VOID *pv)
{
    PRX_CONTEXT RxContext = (PRX_CONTEXT)pv;

    Log(("VBOXSF: vbsfNtWriteThreadWorker: calling the worker\n"));

    RxContext->IoStatusBlock.Status = vbsfNtWriteWorker(RxContext);

    Log(("VBOXSF: vbsfNtWriteThreadWorker: Status 0x%08X\n",
         RxContext->IoStatusBlock.Status));

    RxLowIoCompletion(RxContext);
}

NTSTATUS VBoxMRxWrite(IN PRX_CONTEXT RxContext)
{
    NTSTATUS Status;

    /* If synchronous operation, keep it on this thread (RDBSS already checked
       if we've got enough stack before calling us).   */
    if (!(RxContext->Flags & RX_CONTEXT_FLAG_ASYNC_OPERATION))
    {
        RxContext->IoStatusBlock.Status = Status = vbsfNtWriteWorker(RxContext);
        Assert(Status != STATUS_PENDING);

        Log(("VBOXSF: VBoxMRxWrite: vbsfNtWriteWorker: Status %#08X\n", Status));
    }
    else
    {
        Status = RxDispatchToWorkerThread(VBoxMRxDeviceObject, DelayedWorkQueue, vbsfNtWriteThreadWorker, RxContext);

        Log(("VBOXSF: VBoxMRxWrite: RxDispatchToWorkerThread: Status 0x%08X\n", Status));

        if (Status == STATUS_SUCCESS)
            Status = STATUS_PENDING;
    }

    return Status;
}


NTSTATUS VBoxMRxLocks(IN PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_VBOX_FOBX pVBoxFobx = VBoxMRxGetFileObjectExtension(capFobx);

    PLOWIO_CONTEXT LowIoContext = &RxContext->LowIoContext;
    uint32_t fu32Lock = 0;
    int vrc;

    Log(("VBOXSF: MRxLocks: Operation %d\n",
         LowIoContext->Operation));

    switch (LowIoContext->Operation)
    {
        default:
            AssertMsgFailed(("VBOXSF: MRxLocks: Unsupported lock/unlock type %d detected!\n",
                             LowIoContext->Operation));
            return STATUS_NOT_IMPLEMENTED;

        case LOWIO_OP_UNLOCK_MULTIPLE:
            /** @todo Remove multiple locks listed in LowIoContext.ParamsFor.Locks.LockList. */
            Log(("VBOXSF: MRxLocks: Unsupported LOWIO_OP_UNLOCK_MULTIPLE!\n",
                 LowIoContext->Operation));
            return STATUS_NOT_IMPLEMENTED;

        case LOWIO_OP_SHAREDLOCK:
            fu32Lock = SHFL_LOCK_SHARED | SHFL_LOCK_PARTIAL;
            break;

        case LOWIO_OP_EXCLUSIVELOCK:
            fu32Lock = SHFL_LOCK_EXCLUSIVE | SHFL_LOCK_PARTIAL;
            break;

        case LOWIO_OP_UNLOCK:
            fu32Lock = SHFL_LOCK_CANCEL | SHFL_LOCK_PARTIAL;
            break;
    }

    if (LowIoContext->ParamsFor.Locks.Flags & LOWIO_LOCKSFLAG_FAIL_IMMEDIATELY)
        fu32Lock |= SHFL_LOCK_NOWAIT;
    else
        fu32Lock |= SHFL_LOCK_WAIT;

    vrc = VbglR0SfLock(&g_SfClient, &pNetRootExtension->map, pVBoxFobx->hFile,
                       LowIoContext->ParamsFor.Locks.ByteOffset, LowIoContext->ParamsFor.Locks.Length, fu32Lock);

    Status = vbsfNtVBoxStatusToNt(vrc);

    Log(("VBOXSF: MRxLocks: Returned 0x%08X\n", Status));
    return Status;
}

NTSTATUS VBoxMRxCompleteBufferingStateChangeRequest(IN OUT PRX_CONTEXT RxContext, IN OUT PMRX_SRV_OPEN SrvOpen,
                                                    IN PVOID pvContext)
{
    RT_NOREF(RxContext, SrvOpen, pvContext);
    Log(("VBOXSF: MRxCompleteBufferingStateChangeRequest: not implemented\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS VBoxMRxFlush (IN PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_VBOX_FOBX pVBoxFobx = VBoxMRxGetFileObjectExtension(capFobx);

    int vrc;

    Log(("VBOXSF: MRxFlush\n"));

    /* Do the actual flushing of file buffers */
    vrc = VbglR0SfFlush(&g_SfClient, &pNetRootExtension->map, pVBoxFobx->hFile);

    Status = vbsfNtVBoxStatusToNt(vrc);

    Log(("VBOXSF: MRxFlush: Returned 0x%08X\n", Status));
    return Status;
}

/** See PMRX_EXTENDFILE_CALLDOWN in ddk/mrx.h
 *
 * Documentation says it returns STATUS_SUCCESS on success and an error
 * status on failure, so the ULONG return type is probably just a typo that
 * stuck.
 */
ULONG NTAPI VBoxMRxExtendStub(IN OUT struct _RX_CONTEXT * RxContext, IN OUT PLARGE_INTEGER pNewFileSize,
                              OUT PLARGE_INTEGER pNewAllocationSize)
{
    RT_NOREF(RxContext);

    /* Note: On Windows hosts vbsfNtSetEndOfFile returns ACCESS_DENIED if the file has been
     *       opened in APPEND mode. Writes to a file will extend it anyway, therefore it is
     *       better to not call the host at all and tell the caller that the file was extended.
     */
    Log(("VBOXSF: MRxExtendStub: new size = %RX64\n",
         pNewFileSize->QuadPart));

    pNewAllocationSize->QuadPart = pNewFileSize->QuadPart;

    return STATUS_SUCCESS;
}

