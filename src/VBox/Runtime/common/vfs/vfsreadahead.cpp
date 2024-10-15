/* $Id: vfsreadahead.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, Read-Ahead Thread.
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
#define LOG_GROUP RTLOGGROUP_VFS
#include "internal/iprt.h"
#include <iprt/vfs.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/vfslowlevel.h>



/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/vfs.h>

#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Buffer descriptor.
 */
typedef struct RTVFSREADAHEADBUFDESC
{
    /** List entry. */
    RTLISTNODE          ListEntry;
    /** The offset of this extent within the file. */
    uint64_t            off;
    /** The amount of the buffer that has been filled.
     * (Buffer size is RTVFSREADAHEAD::cbBuffer.)  */
    uint32_t            cbFilled;
    /** */
    uint32_t volatile   fReserved;
    /** Pointer to the buffer. */
    uint8_t            *pbBuffer;
} RTVFSREADAHEADBUFDESC;
/** Pointer to a memory file extent. */
typedef RTVFSREADAHEADBUFDESC *PRTVFSREADAHEADBUFDESC;

/**
 * Read ahead file or I/O stream.
 */
typedef struct RTVFSREADAHEAD
{
    /** The I/O critical section (protects offActual).
     * The thread doing I/O or seeking always need to own this. */
    RTCRITSECT              IoCritSect;

    /** The critical section protecting the buffer lists and offConsumer.
     *
     * This can be taken while holding IoCritSect as that eliminates a race
     * condition between the read ahead thread inserting into ConsumerList and
     * a consumer thread deciding to do a direct read. */
    RTCRITSECT              BufferCritSect;
    /** List of buffers available for consumption.
     * The producer thread (hThread) puts buffers into this list once it's done
     * reading into them.   The consumer moves them to the FreeList once the
     * current position has passed beyond each buffer. */
    RTLISTANCHOR            ConsumerList;
    /** List of buffers available for the producer. */
    RTLISTANCHOR            FreeList;

    /** The current file position from the consumer point of view. */
    uint64_t                offConsumer;

    /** The end-of-file(/stream) offset.  This is initially UINT64_MAX and later
     *  set when reading past EOF.  */
    uint64_t                offEof;

    /** The read ahead thread. */
    RTTHREAD                hThread;
    /** Set when we want the thread to terminate. */
    bool volatile           fTerminateThread;
    /** Creation flags. */
    uint32_t                fFlags;

    /** The I/O stream we read from. */
    RTVFSIOSTREAM           hIos;
    /** The file face of hIos, if we're fronting for an actual file. */
    RTVFSFILE               hFile;
    /** The buffer size. */
    uint32_t                cbBuffer;
    /** The number of buffers. */
    uint32_t                cBuffers;
    /** Single big buffer allocation, cBuffers * cbBuffer in size.  */
    uint8_t                *pbAllBuffers;
    /** Array of buffer descriptors (cBuffers in size). */
    RTVFSREADAHEADBUFDESC   aBufDescs[1];
} RTVFSREADAHEAD;
/** Pointer to a memory file. */
typedef RTVFSREADAHEAD *PRTVFSREADAHEAD;



/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtVfsReadAhead_Close(void *pvThis)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    int rc;

    /*
     * Stop the read-ahead thread.
     */
    if (pThis->hThread != NIL_RTTHREAD)
    {
        ASMAtomicWriteBool(&pThis->fTerminateThread, true);
        rc = RTThreadUserSignal(pThis->hThread);
        AssertRC(rc);
        rc = RTThreadWait(pThis->hThread, RT_INDEFINITE_WAIT, NULL);
        AssertRCReturn(rc, rc);
        pThis->hThread = NIL_RTTHREAD;
    }

    /*
     * Release the upstream objects.
     */
    RTCritSectEnter(&pThis->IoCritSect);

    RTVfsIoStrmRelease(pThis->hIos);
    pThis->hIos  = NIL_RTVFSIOSTREAM;
    RTVfsFileRelease(pThis->hFile);
    pThis->hFile = NIL_RTVFSFILE;

    RTCritSectLeave(&pThis->IoCritSect);

    /*
     * Free the buffers.
     */
    RTCritSectEnter(&pThis->BufferCritSect);
    if (pThis->pbAllBuffers)
    {
        RTMemPageFree(pThis->pbAllBuffers, pThis->cBuffers * pThis->cbBuffer);
        pThis->pbAllBuffers = NULL;
    }
    RTCritSectLeave(&pThis->BufferCritSect);

    /*
     * Destroy the critical sections.
     */
    RTCritSectDelete(&pThis->BufferCritSect);
    RTCritSectDelete(&pThis->IoCritSect);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtVfsReadAhead_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    return RTVfsIoStrmQueryInfo(pThis->hIos, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtVfsReadAhead_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;

    Assert(pSgBuf->cSegs == 1); /* Caller deals with multiple SGs. */

    /*
     * We loop here to repeat the buffer search after entering the I/O critical
     * section, just in case a buffer got inserted while we were waiting for it.
     */
    int      rc = VINF_SUCCESS;
    uint8_t *pbDst           = (uint8_t *)pSgBuf->paSegs[0].pvSeg;
    size_t   cbDst           =            pSgBuf->paSegs[0].cbSeg;
    size_t   cbTotalRead     = 0;
    bool     fPokeReader     = false;
    bool     fOwnsIoCritSect = false;
    RTCritSectEnter(&pThis->BufferCritSect);
    for (;;)
    {
        /*
         * Try satisfy the read from the buffers.
         */
        uint64_t offCur = pThis->offConsumer;
        if (off != -1)
        {
            offCur = (uint64_t)off;
            if (pThis->offConsumer != offCur)
                fPokeReader = true; /* If the current position changed, poke it in case it stopped at EOF. */
            pThis->offConsumer = offCur;
        }

        PRTVFSREADAHEADBUFDESC pBufDesc, pNextBufDesc;
        RTListForEachSafe(&pThis->ConsumerList, pBufDesc, pNextBufDesc, RTVFSREADAHEADBUFDESC, ListEntry)
        {
            /* The buffers are sorted and reads must start in a buffer if
               anything should be taken from the buffer (at least for now). */
            if (offCur < pBufDesc->off)
                break;

            /* Anything we can read from this buffer? */
            uint64_t offCurBuf = offCur - pBufDesc->off;
            if (offCurBuf < pBufDesc->cbFilled)
            {
                size_t const cbFromCurBuf = RT_MIN(pBufDesc->cbFilled - offCurBuf, cbDst);
                memcpy(pbDst, pBufDesc->pbBuffer + offCurBuf, cbFromCurBuf);
                pbDst              += cbFromCurBuf;
                cbDst              -= cbFromCurBuf;
                cbTotalRead        += cbFromCurBuf;
                offCur             += cbFromCurBuf;
            }

            /* Discard buffers we've read past. */
            if (pBufDesc->off + pBufDesc->cbFilled <= offCur)
            {
                RTListNodeRemove(&pBufDesc->ListEntry);
                RTListAppend(&pThis->FreeList, &pBufDesc->ListEntry);
                fPokeReader = true; /* Poke it as there are now more buffers available. */
            }

            /* Stop if we're done. */
            if (!cbDst)
                break;
        }

        pThis->offConsumer = offCur;
        if (off != -1)
            off = offCur;

        if (!cbDst)
            break;

        /*
         * Check if we've reached the end of the file/stream.
         */
        if (offCur >= pThis->offEof)
        {
            rc = pcbRead ? VINF_EOF : VERR_EOF;
            Log(("rtVfsReadAhead_Read: ret %Rrc; offCur=%#llx offEof=%#llx\n", rc, offCur, pThis->offEof));
            break;
        }


        /*
         * First time around we don't own the I/O critsect and need to take it
         * and repeat the above buffer reading code.
         */
        if (!fOwnsIoCritSect)
        {
            RTCritSectLeave(&pThis->BufferCritSect);
            RTCritSectEnter(&pThis->IoCritSect);
            RTCritSectEnter(&pThis->BufferCritSect);
            fOwnsIoCritSect = true;
            continue;
        }


        /*
         * Do a direct read of the remaining data.
         */
        if (off == -1)
        {
            RTFOFF offActual = RTVfsIoStrmTell(pThis->hIos);
            if (offActual >= 0 && (uint64_t)offActual != offCur)
                off = offCur;
        }
        RTSGSEG TmpSeg = { pbDst, cbDst };
        RTSGBUF TmpSgBuf;
        RTSgBufInit(&TmpSgBuf, &TmpSeg, 1);
        size_t cbThisRead = cbDst;
        rc = RTVfsIoStrmSgRead(pThis->hIos, off, &TmpSgBuf, fBlocking, pcbRead ? &cbThisRead : NULL);
        if (RT_SUCCESS(rc))
        {
            cbTotalRead += cbThisRead;
            offCur      += cbThisRead;
            pThis->offConsumer = offCur;
            if (rc != VINF_EOF)
                fPokeReader = true;
            else
            {
                pThis->offEof = offCur;
                Log(("rtVfsReadAhead_Read: EOF %llu (%#llx)\n", pThis->offEof, pThis->offEof));
            }
        }
        /* else if (rc == VERR_EOF): hard to say where exactly the current position
           is here as cannot have had a non-NULL pcbRead.  Set offEof later. */
        break;
    }
    RTCritSectLeave(&pThis->BufferCritSect);
    if (fOwnsIoCritSect)
        RTCritSectLeave(&pThis->IoCritSect);
    if (fPokeReader && rc != VINF_EOF && rc != VERR_EOF)
        RTThreadUserSignal(pThis->hThread);

    if (pcbRead)
        *pcbRead = cbTotalRead;
    Assert(cbTotalRead <= pSgBuf->paSegs[0].cbSeg);
    RTSgBufAdvance(pSgBuf, cbTotalRead);

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtVfsReadAhead_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    RT_NOREF_PV(pvThis); RT_NOREF_PV(off); RT_NOREF_PV(pSgBuf); RT_NOREF_PV(fBlocking); RT_NOREF_PV(pcbWritten);
    AssertFailed();
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtVfsReadAhead_Flush(void *pvThis)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    return RTVfsIoStrmFlush(pThis->hIos);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtVfsReadAhead_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                                uint32_t *pfRetEvents)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    if (pThis->hThread != NIL_RTTHREAD)
    {
        /** @todo poll one with read-ahead thread. */
        return VERR_NOT_IMPLEMENTED;
    }
    return RTVfsIoStrmPoll(pThis->hIos, fEvents, cMillies, fIntr, pfRetEvents);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtVfsReadAhead_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;

    RTCritSectEnter(&pThis->BufferCritSect);
    *poffActual = pThis->offConsumer;
    RTCritSectLeave(&pThis->BufferCritSect);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtVfsReadAhead_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    AssertReturn(pThis->hFile != NIL_RTVFSFILE, VERR_NOT_SUPPORTED);

    RTCritSectEnter(&pThis->IoCritSect);
    RT_NOREF_PV(fMode); RT_NOREF_PV(fMask); /// @todo int rc = RTVfsFileSetMode(pThis->hFile, fMode, fMask);
    int rc = VERR_NOT_SUPPORTED;
    RTCritSectLeave(&pThis->IoCritSect);

    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtVfsReadAhead_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                 PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    AssertReturn(pThis->hFile != NIL_RTVFSFILE, VERR_NOT_SUPPORTED);

    RTCritSectEnter(&pThis->IoCritSect);
    RT_NOREF_PV(pAccessTime); RT_NOREF_PV(pModificationTime); RT_NOREF_PV(pChangeTime); RT_NOREF_PV(pBirthTime);
    /// @todo int rc = RTVfsFileSetTimes(pThis->hFile, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    int rc = VERR_NOT_SUPPORTED;
    RTCritSectLeave(&pThis->IoCritSect);

    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtVfsReadAhead_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    AssertReturn(pThis->hFile != NIL_RTVFSFILE, VERR_NOT_SUPPORTED);

    RTCritSectEnter(&pThis->IoCritSect);
    RT_NOREF_PV(uid); RT_NOREF_PV(gid);
    /// @todo int rc = RTVfsFileSetOwner(pThis->hFile, uid, gid);
    int rc = VERR_NOT_SUPPORTED;
    RTCritSectLeave(&pThis->IoCritSect);

    return rc;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtVfsReadAhead_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    AssertReturn(pThis->hFile != NIL_RTVFSFILE, VERR_NOT_SUPPORTED);

    RTCritSectEnter(&pThis->IoCritSect);        /* protects against concurrent I/O using the offset. */
    RTCritSectEnter(&pThis->BufferCritSect);    /* protects offConsumer */

    uint64_t offActual = UINT64_MAX;
    int rc = RTVfsFileSeek(pThis->hFile, offSeek, uMethod, &offActual);
    if (RT_SUCCESS(rc))
    {
        pThis->offConsumer = offActual;
        if (poffActual)
            *poffActual = offActual;
    }

    RTCritSectLeave(&pThis->BufferCritSect);
    RTCritSectLeave(&pThis->IoCritSect);

    return rc;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtVfsReadAhead_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    AssertReturn(pThis->hFile != NIL_RTVFSFILE, VERR_NOT_SUPPORTED);

    RTCritSectEnter(&pThis->IoCritSect); /* paranoia */
    int rc = RTVfsFileQuerySize(pThis->hFile, pcbFile);
    RTCritSectLeave(&pThis->IoCritSect);

    return rc;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSetSize}
 */
static DECLCALLBACK(int) rtVfsReadAhead_SetSize(void *pvThis, uint64_t cbFile, uint32_t fFlags)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    AssertReturn(pThis->hFile != NIL_RTVFSFILE, VERR_NOT_SUPPORTED);

    RTCritSectEnter(&pThis->IoCritSect); /* paranoia */
    int rc = RTVfsFileSetSize(pThis->hFile, cbFile, fFlags);
    RTCritSectLeave(&pThis->IoCritSect);

    return rc;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQueryMaxSize}
 */
static DECLCALLBACK(int) rtVfsReadAhead_QueryMaxSize(void *pvThis, uint64_t *pcbMax)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvThis;
    AssertReturn(pThis->hFile != NIL_RTVFSFILE, VERR_NOT_SUPPORTED);

    RTCritSectEnter(&pThis->IoCritSect); /* paranoia */
    int rc = RTVfsFileQueryMaxSize(pThis->hFile, pcbMax);
    RTCritSectLeave(&pThis->IoCritSect);

    return rc;
}


/**
 * Read ahead I/O stream operations.
 */
DECL_HIDDEN_CONST(const RTVFSIOSTREAMOPS) g_VfsReadAheadIosOps =
{ /* Stream */
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "Read ahead I/O stream",
        rtVfsReadAhead_Close,
        rtVfsReadAhead_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    RTVFSIOSTREAMOPS_FEAT_NO_SG,
    rtVfsReadAhead_Read,
    rtVfsReadAhead_Write,
    rtVfsReadAhead_Flush,
    rtVfsReadAhead_PollOne,
    rtVfsReadAhead_Tell,
    NULL /*Skip*/,
    NULL /*ZeroFill*/,
    RTVFSIOSTREAMOPS_VERSION,
};


/**
 * Read ahead file operations.
 */
DECL_HIDDEN_CONST(const RTVFSFILEOPS) g_VfsReadAheadFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "Read ahead file",
            rtVfsReadAhead_Close,
            rtVfsReadAhead_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtVfsReadAhead_Read,
        rtVfsReadAhead_Write,
        rtVfsReadAhead_Flush,
        rtVfsReadAhead_PollOne,
        rtVfsReadAhead_Tell,
        NULL /*Skip*/,
        NULL /*ZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    /*RTVFSIOFILEOPS_FEAT_NO_AT_OFFSET*/ 0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        rtVfsReadAhead_SetMode,
        rtVfsReadAhead_SetTimes,
        rtVfsReadAhead_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtVfsReadAhead_Seek,
    rtVfsReadAhead_QuerySize,
    rtVfsReadAhead_SetSize,
    rtVfsReadAhead_QueryMaxSize,
    RTVFSFILEOPS_VERSION
};


/**
 * @callback_method_impl{PFNRTTHREAD, Read ahead thread procedure}
 */
static DECLCALLBACK(int) rtVfsReadAheadThreadProc(RTTHREAD hThreadSelf, void *pvUser)
{
    PRTVFSREADAHEAD pThis = (PRTVFSREADAHEAD)pvUser;
    Assert(pThis);

    while (!pThis->fTerminateThread)
    {
        int rc;

        /*
         * Is there a buffer handy for reading ahead.
         */
        PRTVFSREADAHEADBUFDESC pBufDesc = NULL;
        RTCritSectEnter(&pThis->BufferCritSect);
        if (!pThis->fTerminateThread)
            pBufDesc = RTListRemoveFirst(&pThis->FreeList, RTVFSREADAHEADBUFDESC, ListEntry);
        RTCritSectLeave(&pThis->BufferCritSect);

        if (pBufDesc)
        {
            /*
             * Got a buffer, take the I/O lock and read into it.
             */
            rc = VERR_CALLBACK_RETURN;
            RTCritSectEnter(&pThis->IoCritSect);
            if (!pThis->fTerminateThread)
            {

                pBufDesc->off = RTVfsIoStrmTell(pThis->hIos);
                size_t cbRead = 0;
                rc = RTVfsIoStrmRead(pThis->hIos, pBufDesc->pbBuffer, pThis->cbBuffer, true /*fBlocking*/, &cbRead);
                if (RT_SUCCESS(rc))
                {
                    if (rc == VINF_EOF)
                    {
                        pThis->offEof = pBufDesc->off + cbRead;
                        Log(("rtVfsReadAheadThreadProc: EOF %llu (%#llx)\n", pThis->offEof, pThis->offEof));
                    }
                    pBufDesc->cbFilled = (uint32_t)cbRead;

                    /*
                     * Put back the buffer.  The consumer list is sorted by offset, but
                     * we should usually end up appending the buffer.
                     */
                    RTCritSectEnter(&pThis->BufferCritSect);
                    PRTVFSREADAHEADBUFDESC pAfter = RTListGetLast(&pThis->ConsumerList, RTVFSREADAHEADBUFDESC, ListEntry);
                    if (!pAfter || pAfter->off <= pBufDesc->off)
                        RTListAppend(&pThis->ConsumerList, &pBufDesc->ListEntry);
                    else
                    {
                        do
                            pAfter = RTListGetPrev(&pThis->ConsumerList, pAfter, RTVFSREADAHEADBUFDESC, ListEntry);
                        while (pAfter && pAfter->off > pBufDesc->off);
                        if (!pAfter)
                            RTListPrepend(&pThis->ConsumerList, &pBufDesc->ListEntry);
                        else
                        {
                            Assert(pAfter->off <= pBufDesc->off);
                            RTListNodeInsertAfter(&pAfter->ListEntry, &pBufDesc->ListEntry);
                        }
                    }
                    RTCritSectLeave(&pThis->BufferCritSect);
                    pBufDesc = NULL;

#ifdef RT_STRICT
                    /* Verify the list ordering.  */
                    unsigned                cAsserted = 0;
                    uint64_t                offAssert = 0;
                    PRTVFSREADAHEADBUFDESC  pAssertCur;
                    RTListForEach(&pThis->ConsumerList, pAssertCur, RTVFSREADAHEADBUFDESC, ListEntry)
                    {
                        Assert(offAssert <= pAssertCur->off);
                        offAssert = pAssertCur->off;
                        Assert(cAsserted < pThis->cBuffers);
                        cAsserted++;
                    }
#endif
                }
                else
                    Assert(rc != VERR_EOF);
            }
            RTCritSectLeave(&pThis->IoCritSect);

            /*
             * If we succeeded and we didn't yet reach the end of the stream,
             * loop without delay to start processing the next buffer.
             */
            if (RT_LIKELY(!pBufDesc && rc != VINF_EOF))
                continue;

            /* Put any unused buffer back in the free list (termination/failure, not EOF). */
            if (pBufDesc)
            {
                RTCritSectEnter(&pThis->BufferCritSect);
                RTListPrepend(&pThis->FreeList, &pBufDesc->ListEntry);
                RTCritSectLeave(&pThis->BufferCritSect);
            }
            if (pThis->fTerminateThread)
                break;
        }

        /*
         * Wait for more to do.
         */
        rc = RTThreadUserWait(hThreadSelf, RT_MS_1MIN);
        if (RT_SUCCESS(rc))
            RTThreadUserReset(hThreadSelf);
    }

    return VINF_SUCCESS;
}


static int rtVfsCreateReadAheadInstance(RTVFSIOSTREAM hVfsIosSrc, RTVFSFILE hVfsFileSrc, uint32_t fFlags,
                                        uint32_t cBuffers, uint32_t cbBuffer, PRTVFSIOSTREAM phVfsIos, PRTVFSFILE phVfsFile)
{
    /*
     * Validate input a little.
     */
    int rc = VINF_SUCCESS;
    AssertStmt(cBuffers < _4K, rc = VERR_OUT_OF_RANGE);
    if (cBuffers == 0)
        cBuffers = 4;
    AssertStmt(cbBuffer <= _4M, rc = VERR_OUT_OF_RANGE);
    if (cbBuffer == 0)
        cbBuffer = _256K / cBuffers;
    AssertStmt(cbBuffer * cBuffers < (ARCH_BITS < 64 ? _64M : _256M), rc = VERR_OUT_OF_RANGE);
    AssertStmt(!fFlags, rc = VERR_INVALID_FLAGS);

    if (RT_SUCCESS(rc))
    {
        /*
         * Create a file or I/O stream instance.
         */
        RTVFSFILE       hVfsFileReadAhead = NIL_RTVFSFILE;
        RTVFSIOSTREAM   hVfsIosReadAhead  = NIL_RTVFSIOSTREAM;
        PRTVFSREADAHEAD pThis;
        size_t          cbThis = RT_UOFFSETOF_DYN(RTVFSREADAHEAD, aBufDescs[cBuffers]);
        if (hVfsFileSrc != NIL_RTVFSFILE)
            rc = RTVfsNewFile(&g_VfsReadAheadFileOps, cbThis, RTFILE_O_READ, NIL_RTVFS, NIL_RTVFSLOCK,
                              &hVfsFileReadAhead, (void **)&pThis);
        else
            rc = RTVfsNewIoStream(&g_VfsReadAheadIosOps, cbThis, RTFILE_O_READ, NIL_RTVFS, NIL_RTVFSLOCK,
                                  &hVfsIosReadAhead, (void **)&pThis);
        if (RT_SUCCESS(rc))
        {
            RTListInit(&pThis->ConsumerList);
            RTListInit(&pThis->FreeList);
            pThis->hThread          = NIL_RTTHREAD;
            pThis->fTerminateThread = false;
            pThis->fFlags           = fFlags;
            pThis->hFile            = hVfsFileSrc;
            pThis->hIos             = hVfsIosSrc;
            pThis->cBuffers         = cBuffers;
            pThis->cbBuffer         = cbBuffer;
            pThis->offEof           = UINT64_MAX;
            pThis->offConsumer      = RTVfsIoStrmTell(hVfsIosSrc);
            if ((RTFOFF)pThis->offConsumer >= 0)
            {
                rc = RTCritSectInit(&pThis->IoCritSect);
                if (RT_SUCCESS(rc))
                    rc = RTCritSectInit(&pThis->BufferCritSect);
                if (RT_SUCCESS(rc))
                {
                    pThis->pbAllBuffers = (uint8_t *)RTMemPageAlloc(pThis->cbBuffer * pThis->cBuffers);
                    if (pThis->pbAllBuffers)
                    {
                        for (uint32_t i = 0; i < cBuffers; i++)
                        {
                            pThis->aBufDescs[i].cbFilled = 0;
                            pThis->aBufDescs[i].off      = UINT64_MAX / 2;
                            pThis->aBufDescs[i].pbBuffer = &pThis->pbAllBuffers[cbBuffer * i];
                            RTListAppend(&pThis->FreeList, &pThis->aBufDescs[i].ListEntry);
                        }

                        /*
                         * Create thread.
                         */
                        rc = RTThreadCreate(&pThis->hThread, rtVfsReadAheadThreadProc, pThis, 0, RTTHREADTYPE_DEFAULT,
                                            RTTHREADFLAGS_WAITABLE, "vfsreadahead");
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * We're good.
                             */
                            if (phVfsFile)
                                *phVfsFile = hVfsFileReadAhead;
                            else if (hVfsFileReadAhead == NIL_RTVFSFILE)
                                *phVfsIos = hVfsIosReadAhead;
                            else
                            {
                                *phVfsIos = RTVfsFileToIoStream(hVfsFileReadAhead);
                                RTVfsFileRelease(hVfsFileReadAhead);
                                AssertReturn(*phVfsIos != NIL_RTVFSIOSTREAM, VERR_INTERNAL_ERROR_5);
                            }
                            return VINF_SUCCESS;
                        }
                    }
                }
            }
            else
                rc = (int)pThis->offConsumer;
        }
    }

    RTVfsFileRelease(hVfsFileSrc);
    RTVfsIoStrmRelease(hVfsIosSrc);
    return rc;
}


RTDECL(int) RTVfsCreateReadAheadForIoStream(RTVFSIOSTREAM hVfsIos, uint32_t fFlags, uint32_t cBuffers, uint32_t cbBuffer,
                                            PRTVFSIOSTREAM phVfsIos)
{
    AssertPtrReturn(phVfsIos, VERR_INVALID_POINTER);
    *phVfsIos = NIL_RTVFSIOSTREAM;

    /*
     * Retain the input stream, trying to obtain a file handle too so we can
     * fully mirror it.
     */
    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIos);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);
    RTVFSFILE hVfsFile = RTVfsIoStrmToFile(hVfsIos);

    /*
     * Do the job. (This always consumes the above retained references.)
     */
    return rtVfsCreateReadAheadInstance(hVfsIos, hVfsFile, fFlags, cBuffers, cbBuffer, phVfsIos, NULL);
}


RTDECL(int) RTVfsCreateReadAheadForFile(RTVFSFILE hVfsFile, uint32_t fFlags, uint32_t cBuffers, uint32_t cbBuffer,
                                        PRTVFSFILE phVfsFile)
{
    AssertPtrReturn(phVfsFile, VERR_INVALID_POINTER);
    *phVfsFile = NIL_RTVFSFILE;

    /*
     * Retain the input file and cast it o an I/O stream.
     */
    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hVfsFile);
    AssertReturn(hVfsIos != NIL_RTVFSIOSTREAM, VERR_INVALID_HANDLE);
    uint32_t cRefs = RTVfsFileRetain(hVfsFile);
    AssertReturnStmt(cRefs != UINT32_MAX, RTVfsIoStrmRelease(hVfsIos), VERR_INVALID_HANDLE);

    /*
     * Do the job. (This always consumes the above retained references.)
     */
    return rtVfsCreateReadAheadInstance(hVfsIos, hVfsFile, fFlags, cBuffers, cbBuffer, NULL, phVfsFile);
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainReadAhead_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                                      PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, poffError, pErrInfo);

    /*
     * Basics.
     */
    if (   pElement->enmType != RTVFSOBJTYPE_FILE
        && pElement->enmType != RTVFSOBJTYPE_IO_STREAM)
        return VERR_VFS_CHAIN_ONLY_FILE_OR_IOS;
    if (pElement->enmTypeIn == RTVFSOBJTYPE_INVALID)
        return VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT;
    if (   pElement->enmTypeIn != RTVFSOBJTYPE_FILE
        && pElement->enmTypeIn != RTVFSOBJTYPE_IO_STREAM)
        return VERR_VFS_CHAIN_TAKES_FILE_OR_IOS;
    if (pSpec->fOpenFile & RTFILE_O_WRITE)
        return VERR_VFS_CHAIN_READ_ONLY_IOS;
    if (pElement->cArgs > 2)
        return VERR_VFS_CHAIN_AT_MOST_TWO_ARGS;

    /*
     * Parse the two optional arguments.
     */
    uint32_t cBuffers = 0;
    if (pElement->cArgs > 0)
    {
        const char *psz = pElement->paArgs[0].psz;
        if (*psz)
        {
            int rc = RTStrToUInt32Full(psz, 0, &cBuffers);
            if (RT_FAILURE(rc))
            {
                *poffError = pElement->paArgs[0].offSpec;
                return VERR_VFS_CHAIN_INVALID_ARGUMENT;
            }
        }
    }

    uint32_t cbBuffer = 0;
    if (pElement->cArgs > 1)
    {
        const char *psz = pElement->paArgs[1].psz;
        if (*psz)
        {
            int rc = RTStrToUInt32Full(psz, 0, &cbBuffer);
            if (RT_FAILURE(rc))
            {
                *poffError = pElement->paArgs[1].offSpec;
                return VERR_VFS_CHAIN_INVALID_ARGUMENT;
            }
        }
    }

    /*
     * Save the parsed arguments in the spec since their both optional.
     */
    pElement->uProvider = RT_MAKE_U64(cBuffers, cbBuffer);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) rtVfsChainReadAhead_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                         PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                         PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, pElement, poffError, pErrInfo);
    AssertReturn(hPrevVfsObj != NIL_RTVFSOBJ, VERR_VFS_CHAIN_IPE);

    /* Try for a file if we can. */
    int rc;
    RTVFSFILE hVfsFileIn = RTVfsObjToFile(hPrevVfsObj);
    if (hVfsFileIn != NIL_RTVFSFILE)
    {
        RTVFSFILE hVfsFile = NIL_RTVFSFILE;
        rc = RTVfsCreateReadAheadForFile(hVfsFileIn, 0 /*fFlags*/, RT_LO_U32(pElement->uProvider),
                                         RT_HI_U32(pElement->uProvider), &hVfsFile);
        RTVfsFileRelease(hVfsFileIn);
        if (RT_SUCCESS(rc))
        {
            *phVfsObj = RTVfsObjFromFile(hVfsFile);
            RTVfsFileRelease(hVfsFile);
            if (*phVfsObj != NIL_RTVFSOBJ)
                return VINF_SUCCESS;
            rc = VERR_VFS_CHAIN_CAST_FAILED;
        }
    }
    else if (pElement->enmType == RTVFSOBJTYPE_IO_STREAM)
    {
        RTVFSIOSTREAM hVfsIosIn = RTVfsObjToIoStream(hPrevVfsObj);
        if (hVfsIosIn != NIL_RTVFSIOSTREAM)
        {
            RTVFSIOSTREAM hVfsIos = NIL_RTVFSIOSTREAM;
            rc = RTVfsCreateReadAheadForIoStream(hVfsIosIn, 0 /*fFlags*/, RT_LO_U32(pElement->uProvider),
                                                 RT_HI_U32(pElement->uProvider), &hVfsIos);
            RTVfsIoStrmRelease(hVfsIosIn);
            if (RT_SUCCESS(rc))
            {
                *phVfsObj = RTVfsObjFromIoStream(hVfsIos);
                RTVfsIoStrmRelease(hVfsIos);
                if (*phVfsObj != NIL_RTVFSOBJ)
                    return VINF_SUCCESS;
                rc = VERR_VFS_CHAIN_CAST_FAILED;
            }
        }
        else
            rc = VERR_VFS_CHAIN_CAST_FAILED;
    }
    else
        rc = VERR_VFS_CHAIN_CAST_FAILED;
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) rtVfsChainReadAhead_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                              PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                              PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pElement, pReuseSpec, pReuseElement);
    return false;
}


/** VFS chain element 'pull'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainReadAheadReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "pull",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Takes an I/O stream or file and provides read-ahead caching.\n"
                                "Optional first argument specifies how many buffers to use, 0 indicating the default.\n"
                                "Optional second argument specifies the buffer size, 0 indicating the default.",
    /* pfnValidate = */         rtVfsChainReadAhead_Validate,
    /* pfnInstantiate = */      rtVfsChainReadAhead_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainReadAhead_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainReadAheadReg, rtVfsChainReadAheadReg);

