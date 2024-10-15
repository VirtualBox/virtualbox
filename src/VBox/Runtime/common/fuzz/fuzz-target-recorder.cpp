/* $Id: fuzz-target-recorder.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Fuzzing framework API, target state recorder.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/fuzz.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/crc.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the internal fuzzed target recorder state. */
typedef struct RTFUZZTGTRECINT *PRTFUZZTGTRECINT;


/**
 * Stdout/Stderr buffer.
 */
typedef struct RTFUZZTGTSTDOUTERRBUF
{
    /** Current amount buffered. */
    size_t                      cbBuf;
    /** Maxmium amount to buffer. */
    size_t                      cbBufMax;
    /** Base pointer to the data buffer. */
    uint8_t                     *pbBase;
} RTFUZZTGTSTDOUTERRBUF;
/** Pointer to a stdout/stderr buffer. */
typedef RTFUZZTGTSTDOUTERRBUF *PRTFUZZTGTSTDOUTERRBUF;


/**
 * Internal fuzzed target state.
 */
typedef struct RTFUZZTGTSTATEINT
{
    /** Node for the list of states. */
    RTLISTNODE                  NdStates;
    /** Checksum for the state. */
    uint64_t                    uChkSum;
    /** Magic identifying the structure. */
    uint32_t                    u32Magic;
    /** Reference counter. */
    volatile uint32_t           cRefs;
    /** The owning recorder instance. */
    PRTFUZZTGTRECINT            pTgtRec;
    /** Flag whether the state is finalized. */
    bool                        fFinalized;
    /** Flag whether the state is contained in the recorded set. */
    bool                        fInRecSet;
    /** The stdout data buffer. */
    RTFUZZTGTSTDOUTERRBUF       StdOutBuf;
    /** The stderr data buffer. */
    RTFUZZTGTSTDOUTERRBUF       StdErrBuf;
    /** Process status. */
    RTPROCSTATUS                ProcSts;
    /** Coverage report buffer. */
    void                        *pvCovReport;
    /** Size of the coverage report in bytes. */
    size_t                      cbCovReport;
    /** Number of traced edges. */
    size_t                      cEdges;
} RTFUZZTGTSTATEINT;
/** Pointer to an internal fuzzed target state. */
typedef RTFUZZTGTSTATEINT *PRTFUZZTGTSTATEINT;


/**
 * Recorder states node in the AVL tree.
 */
typedef struct RTFUZZTGTRECNODE
{
    /** The AVL tree core (keyed by checksum). */
    AVLU64NODECORE              Core;
    /** The list anchor for the individual states. */
    RTLISTANCHOR                LstStates;
} RTFUZZTGTRECNODE;
/** Pointer to a recorder states node. */
typedef RTFUZZTGTRECNODE *PRTFUZZTGTRECNODE;


/**
 * Edge information node.
 */
typedef struct RTFUZZTGTEDGE
{
    /** The AVL tree core (keyed by offset). */
    AVLU64NODECORE              Core;
    /** Number of times the edge was hit. */
    volatile uint64_t           cHits;
} RTFUZZTGTEDGE;
/** Pointer to a edge information node. */
typedef RTFUZZTGTEDGE *PRTFUZZTGTEDGE;


/**
 * Internal fuzzed target recorder state.
 */
typedef struct RTFUZZTGTRECINT
{
    /** Magic value for identification. */
    uint32_t                    u32Magic;
    /** Reference counter. */
    volatile uint32_t           cRefs;
    /** Flags passed when the recorder was created. */
    uint32_t                    fRecFlags;
    /** Semaphore protecting the states tree. */
    RTSEMRW                     hSemRwStates;
    /** The AVL tree for indexing the recorded state (keyed by stdout/stderr buffer size). */
    AVLU64TREE                  TreeStates;
    /** Semaphore protecting the edges tree. */
    RTSEMRW                     hSemRwEdges;
    /** The AVL tree for discovered edges when coverage reports are collected. */
    AVLU64TREE                  TreeEdges;
    /** Number of edges discovered so far. */
    volatile uint64_t           cEdges;
    /** The discovered offset width. */
    volatile uint32_t           cbCovOff;
} RTFUZZTGTRECINT;


/** SanCov magic for 64bit offsets. */
#define SANCOV_MAGIC_64         UINT64_C(0xc0bfffffffffff64)
/** SanCov magic for 32bit offsets. */
#define SANCOV_MAGIC_32         UINT64_C(0xc0bfffffffffff32)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Initializes the given stdout/stderr buffer.
 *
 * @param   pBuf                The buffer to initialize.
 */
static void rtFuzzTgtStdOutErrBufInit(PRTFUZZTGTSTDOUTERRBUF pBuf)
{
    pBuf->cbBuf    = 0;
    pBuf->cbBufMax = 0;
    pBuf->pbBase   = NULL;
}


/**
 * Frees all allocated resources in the given stdout/stderr buffer.
 *
 * @param   pBuf                The buffer to free.
 */
static void rtFuzzTgtStdOutErrBufFree(PRTFUZZTGTSTDOUTERRBUF pBuf)
{
    if (pBuf->pbBase)
        RTMemFree(pBuf->pbBase);
}


/**
 * Fills the given stdout/stderr buffer from the given pipe.
 *
 * @returns IPRT status code.
 * @param   pBuf                The buffer to fill.
 * @param   hPipeRead           The pipe to read from.
 */
static int rtFuzzTgtStdOutErrBufFillFromPipe(PRTFUZZTGTSTDOUTERRBUF pBuf, RTPIPE hPipeRead)
{
    int rc = VINF_SUCCESS;

    size_t cbRead = 0;
    size_t cbThisRead = 0;
    do
    {
        cbThisRead = pBuf->cbBufMax - pBuf->cbBuf;
        if (!cbThisRead)
        {
            /* Try to increase the buffer. */
            uint8_t *pbNew = (uint8_t *)RTMemRealloc(pBuf->pbBase, pBuf->cbBufMax + _4K);
            if (RT_LIKELY(pbNew))
            {
                pBuf->cbBufMax += _4K;
                pBuf->pbBase   = pbNew;
            }
            cbThisRead = pBuf->cbBufMax - pBuf->cbBuf;
        }

        if (cbThisRead)
        {
            rc = RTPipeRead(hPipeRead, pBuf->pbBase + pBuf->cbBuf, cbThisRead, &cbRead);
            if (RT_SUCCESS(rc))
                pBuf->cbBuf += cbRead;
        }
        else
            rc = VERR_NO_MEMORY;
    } while (   RT_SUCCESS(rc)
             && cbRead == cbThisRead);

    return rc;
}


/**
 * Writes the given buffer to the given file.
 *
 * @returns IPRT status code.
 * @param   pBuf                The buffer to write.
 * @param   pszFilename         Where to write the buffer.
 */
static int rtFuzzTgtStateStdOutErrBufWriteToFile(PRTFUZZTGTSTDOUTERRBUF pBuf, const char *pszFilename)
{
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(rc))
    {
        rc = RTFileWrite(hFile, pBuf->pbBase, pBuf->cbBuf, NULL);
        AssertRC(rc);
        RTFileClose(hFile);

        if (RT_FAILURE(rc))
            RTFileDelete(pszFilename);
    }

    return rc;
}


/**
 * Scans the given target state for newly discovered edges in the coverage report.
 *
 * @returns IPRT status code.
 * @param   pThis               The fuzzer target recorder instance.
 * @param   pTgtState           The target state to check.
 */
static int rtFuzzTgtRecScanStateForNewEdges(PRTFUZZTGTRECINT pThis, PRTFUZZTGTSTATEINT pTgtState)
{
    int rc = VINF_SUCCESS;

    if (pTgtState->pvCovReport)
    {
        rc = RTSemRWRequestRead(pThis->hSemRwEdges, RT_INDEFINITE_WAIT); AssertRC(rc);

        uint32_t cbCovOff = ASMAtomicReadU32(&pThis->cbCovOff);
        Assert(cbCovOff != 0);

        uint8_t *pbCovCur = (uint8_t *)pTgtState->pvCovReport;
        size_t cEdgesLeft = pTgtState->cbCovReport / cbCovOff;
        while (cEdgesLeft)
        {
            uint64_t offCur =   cbCovOff == sizeof(uint64_t)
                              ? *(uint64_t *)pbCovCur
                              : *(uint32_t *)pbCovCur;

            PRTFUZZTGTEDGE pEdge = (PRTFUZZTGTEDGE)RTAvlU64Get(&pThis->TreeEdges, offCur);
            if (!pEdge)
            {
                /* New edge discovered, allocate and add. */
                rc = RTSemRWReleaseRead(pThis->hSemRwEdges); AssertRC(rc);

                pEdge = (PRTFUZZTGTEDGE)RTMemAllocZ(sizeof(RTFUZZTGTEDGE));
                if (RT_LIKELY(pEdge))
                {
                    pEdge->Core.Key = offCur;
                    pEdge->cHits    = 1;
                    rc = RTSemRWRequestWrite(pThis->hSemRwEdges, RT_INDEFINITE_WAIT); AssertRC(rc);

                    bool fIns = RTAvlU64Insert(&pThis->TreeEdges, &pEdge->Core);
                    if (!fIns)
                    {
                        /* Someone raced us, free and query again. */
                        RTMemFree(pEdge);
                        pEdge = (PRTFUZZTGTEDGE)RTAvlU64Get(&pThis->TreeEdges, offCur);
                        AssertPtr(pEdge);

                        ASMAtomicIncU64(&pEdge->cHits);
                    }
                    else
                        ASMAtomicIncU64(&pThis->cEdges);

                    rc = RTSemRWReleaseWrite(pThis->hSemRwEdges); AssertRC(rc);
                    rc = RTSemRWRequestRead(pThis->hSemRwEdges, RT_INDEFINITE_WAIT); AssertRC(rc);
                }
                else
                {
                    rc = RTSemRWRequestRead(pThis->hSemRwEdges, RT_INDEFINITE_WAIT);
                    AssertRC(rc);

                    rc = VERR_NO_MEMORY;
                    break;
                }
            }
            else
                ASMAtomicIncU64(&pEdge->cHits);

            pbCovCur += cbCovOff;
            cEdgesLeft--;
        }

        int rc2 = RTSemRWReleaseRead(pThis->hSemRwEdges); AssertRC(rc2);
        if (   RT_FAILURE(rc2)
            && RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


/**
 * Destorys the given fuzzer target recorder freeing all allocated resources.
 *
 * @param   pThis               The fuzzer target recorder instance.
 */
static void rtFuzzTgtRecDestroy(PRTFUZZTGTRECINT pThis)
{
    RT_NOREF(pThis);
}


/**
 * Destroys the given fuzzer target state freeing all allocated resources.
 *
 * @param   pThis               The fuzzed target state instance.
 */
static void rtFuzzTgtStateDestroy(PRTFUZZTGTSTATEINT pThis)
{
    pThis->u32Magic = ~(uint32_t)0; /** @todo Dead magic */
    rtFuzzTgtStdOutErrBufFree(&pThis->StdOutBuf);
    rtFuzzTgtStdOutErrBufFree(&pThis->StdErrBuf);
    RTMemFree(pThis);
}


/**
 * Compares two given target states, checking whether they match.
 *
 * @returns Flag whether the states are identical.
 * @param   pThis               Target state 1.
 * @param   pThat               Target state 2.
 */
static bool rtFuzzTgtStateDoMatch(PRTFUZZTGTSTATEINT pThis, PRTFUZZTGTSTATEINT pThat)
{
    PRTFUZZTGTRECINT pTgtRec = pThis->pTgtRec;
    Assert(pTgtRec == pThat->pTgtRec);

    if (   (pTgtRec->fRecFlags & RTFUZZTGT_REC_STATE_F_STDOUT)
        && (   pThis->StdOutBuf.cbBuf != pThat->StdOutBuf.cbBuf
            || (   pThis->StdOutBuf.cbBuf > 0
                && memcmp(pThis->StdOutBuf.pbBase, pThat->StdOutBuf.pbBase, pThis->StdOutBuf.cbBuf))))
        return false;

    if (   (pTgtRec->fRecFlags & RTFUZZTGT_REC_STATE_F_STDERR)
        && (   pThis->StdErrBuf.cbBuf != pThat->StdErrBuf.cbBuf
            || (   pThis->StdErrBuf.cbBuf > 0
                && memcmp(pThis->StdErrBuf.pbBase, pThat->StdErrBuf.pbBase, pThis->StdErrBuf.cbBuf))))
        return false;

    if (   (pTgtRec->fRecFlags & RTFUZZTGT_REC_STATE_F_PROCSTATUS)
        && memcmp(&pThis->ProcSts, &pThat->ProcSts, sizeof(RTPROCSTATUS)))
        return false;

    if (   (pTgtRec->fRecFlags & RTFUZZTGT_REC_STATE_F_SANCOV)
        && (   pThis->cbCovReport != pThat->cbCovReport
            || (   pThis->cbCovReport > 0
                && memcmp(pThis->pvCovReport, pThat->pvCovReport, pThis->cbCovReport))))
        return false;

    return true;
}


RTDECL(int) RTFuzzTgtRecorderCreate(PRTFUZZTGTREC phFuzzTgtRec, uint32_t fRecFlags)
{
    AssertPtrReturn(phFuzzTgtRec, VERR_INVALID_POINTER);
    AssertReturn(!(fRecFlags & ~RTFUZZTGT_REC_STATE_F_VALID), VERR_INVALID_PARAMETER);

    int rc;
    PRTFUZZTGTRECINT pThis = (PRTFUZZTGTRECINT)RTMemAllocZ(sizeof(*pThis));
    if (RT_LIKELY(pThis))
    {
        pThis->u32Magic         = 0; /** @todo */
        pThis->cRefs            = 1;
        pThis->TreeStates       = NULL;
        pThis->TreeEdges        = NULL;
        pThis->cbCovOff         = 0;
        pThis->fRecFlags        = fRecFlags;

        rc = RTSemRWCreate(&pThis->hSemRwStates);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemRWCreate(&pThis->hSemRwEdges);
            if (RT_SUCCESS(rc))
            {
                *phFuzzTgtRec = pThis;
                return VINF_SUCCESS;
            }

            RTSemRWDestroy(pThis->hSemRwStates);
        }

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(uint32_t) RTFuzzTgtRecorderRetain(RTFUZZTGTREC hFuzzTgtRec)
{
    PRTFUZZTGTRECINT pThis = hFuzzTgtRec;

    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    return cRefs;
}


RTDECL(uint32_t) RTFuzzTgtRecorderRelease(RTFUZZTGTREC hFuzzTgtRec)
{
    PRTFUZZTGTRECINT pThis = hFuzzTgtRec;
    if (pThis == NIL_RTFUZZTGTREC)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 0)
        rtFuzzTgtRecDestroy(pThis);
    return cRefs;
}


RTDECL(int) RTFuzzTgtRecorderCreateNewState(RTFUZZTGTREC hFuzzTgtRec, PRTFUZZTGTSTATE phFuzzTgtState)
{
    PRTFUZZTGTRECINT pThis = hFuzzTgtRec;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(phFuzzTgtState, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    PRTFUZZTGTSTATEINT pState = (PRTFUZZTGTSTATEINT)RTMemAllocZ(sizeof(*pState));
    if (RT_LIKELY(pState))
    {
        pState->u32Magic   = 0; /** @todo */
        pState->cRefs      = 1;
        pState->pTgtRec    = pThis;
        pState->fFinalized = false;
        rtFuzzTgtStdOutErrBufInit(&pState->StdOutBuf);
        rtFuzzTgtStdOutErrBufInit(&pState->StdErrBuf);
        *phFuzzTgtState = pState;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(uint32_t) RTFuzzTgtStateRetain(RTFUZZTGTSTATE hFuzzTgtState)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;

    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    return cRefs;
}


RTDECL(uint32_t) RTFuzzTgtStateRelease(RTFUZZTGTSTATE hFuzzTgtState)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    if (pThis == NIL_RTFUZZTGTSTATE)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 0 && !pThis->fInRecSet)
        rtFuzzTgtStateDestroy(pThis);
    return cRefs;
}


RTDECL(int) RTFuzzTgtStateReset(RTFUZZTGTSTATE hFuzzTgtState)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    /* Clear the buffers. */
    pThis->StdOutBuf.cbBuf = 0;
    pThis->StdErrBuf.cbBuf = 0;
    RT_ZERO(pThis->ProcSts);
    if (pThis->pvCovReport)
        RTMemFree(pThis->pvCovReport);
    pThis->pvCovReport     = NULL;
    pThis->fFinalized      = false;
    return VINF_SUCCESS;
}


RTDECL(int) RTFuzzTgtStateFinalize(RTFUZZTGTSTATE hFuzzTgtState)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    /* Create the checksum. */
    PRTFUZZTGTRECINT pTgtRec = pThis->pTgtRec;
    uint64_t uChkSum = RTCrc64Start();
    if (   (pTgtRec->fRecFlags & RTFUZZTGT_REC_STATE_F_STDOUT)
        && pThis->StdOutBuf.cbBuf)
        uChkSum = RTCrc64Process(uChkSum, pThis->StdOutBuf.pbBase, pThis->StdOutBuf.cbBuf);
    if (   (pTgtRec->fRecFlags & RTFUZZTGT_REC_STATE_F_STDERR)
        && pThis->StdErrBuf.cbBuf)
        uChkSum = RTCrc64Process(uChkSum, pThis->StdErrBuf.pbBase, pThis->StdErrBuf.cbBuf);
    if (pTgtRec->fRecFlags & RTFUZZTGT_REC_STATE_F_PROCSTATUS)
        uChkSum = RTCrc64Process(uChkSum, &pThis->ProcSts, sizeof(RTPROCSTATUS));
    if (   (pTgtRec->fRecFlags & RTFUZZTGT_REC_STATE_F_SANCOV)
        && pThis->pvCovReport)
        uChkSum = RTCrc64Process(uChkSum, pThis->pvCovReport, pThis->cbCovReport);

    pThis->uChkSum = RTCrc64Finish(uChkSum);
    pThis->fFinalized = true;
    return VINF_SUCCESS;
}


RTDECL(int) RTFuzzTgtStateAddToRecorder(RTFUZZTGTSTATE hFuzzTgtState)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    if (!pThis->fFinalized)
    {
        int rc = RTFuzzTgtStateFinalize(pThis);
        if (RT_FAILURE(rc))
            return rc;
    }

    PRTFUZZTGTRECINT pTgtRec = pThis->pTgtRec;

    /* Try to find a node matching the stdout and sterr sizes first. */
    int rc = RTSemRWRequestRead(pTgtRec->hSemRwStates, RT_INDEFINITE_WAIT); AssertRC(rc);
    PRTFUZZTGTRECNODE pNode = (PRTFUZZTGTRECNODE)RTAvlU64Get(&pTgtRec->TreeStates, pThis->uChkSum);
    if (pNode)
    {
        /* Traverse the states and check if any matches the stdout and stderr buffers exactly. */
        PRTFUZZTGTSTATEINT pIt;
        bool fMatchFound = false;
        RTListForEach(&pNode->LstStates, pIt, RTFUZZTGTSTATEINT, NdStates)
        {
            if (rtFuzzTgtStateDoMatch(pThis, pIt))
            {
                fMatchFound = true;
                break;
            }
        }

        rc = RTSemRWReleaseRead(pTgtRec->hSemRwStates); AssertRC(rc);
        if (!fMatchFound)
        {
            rc = RTSemRWRequestWrite(pTgtRec->hSemRwStates, RT_INDEFINITE_WAIT); AssertRC(rc);
            RTListAppend(&pNode->LstStates, &pThis->NdStates);
            rc = RTSemRWReleaseWrite(pTgtRec->hSemRwStates); AssertRC(rc);
            pThis->fInRecSet = true;
        }
        else
            rc = VERR_ALREADY_EXISTS;
    }
    else
    {
        rc = RTSemRWReleaseRead(pTgtRec->hSemRwStates); AssertRC(rc);

        /* No node found, create new one and insert in to the tree right away. */
        pNode = (PRTFUZZTGTRECNODE)RTMemAllocZ(sizeof(*pNode));
        if (RT_LIKELY(pNode))
        {
            pNode->Core.Key = pThis->uChkSum;
            RTListInit(&pNode->LstStates);
            RTListAppend(&pNode->LstStates, &pThis->NdStates);
            rc = RTSemRWRequestWrite(pTgtRec->hSemRwStates, RT_INDEFINITE_WAIT); AssertRC(rc);
            bool fIns = RTAvlU64Insert(&pTgtRec->TreeStates, &pNode->Core);
            if (!fIns)
            {
                /* Someone raced us, get the new node and append there. */
                RTMemFree(pNode);
                pNode = (PRTFUZZTGTRECNODE)RTAvlU64Get(&pTgtRec->TreeStates, pThis->uChkSum);
                AssertPtr(pNode);
                RTListAppend(&pNode->LstStates, &pThis->NdStates);
            }
            rc = RTSemRWReleaseWrite(pTgtRec->hSemRwStates); AssertRC(rc);
            pThis->fInRecSet = true;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (   RT_SUCCESS(rc)
        && pThis->fInRecSet)
        rc = rtFuzzTgtRecScanStateForNewEdges(pTgtRec, pThis);

    return rc;
}


RTDECL(int) RTFuzzTgtStateAppendStdoutFromBuf(RTFUZZTGTSTATE hFuzzTgtState, const void *pvStdOut, size_t cbStdOut)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    RT_NOREF(pvStdOut, cbStdOut);
    return VERR_NOT_IMPLEMENTED;
}


RTDECL(int) RTFuzzTgtStateAppendStderrFromBuf(RTFUZZTGTSTATE hFuzzTgtState, const void *pvStdErr, size_t cbStdErr)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    RT_NOREF(pvStdErr, cbStdErr);
    return VERR_NOT_IMPLEMENTED;
}


RTDECL(int) RTFuzzTgtStateAppendStdoutFromPipe(RTFUZZTGTSTATE hFuzzTgtState, RTPIPE hPipe)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    return rtFuzzTgtStdOutErrBufFillFromPipe(&pThis->StdOutBuf, hPipe);
}


RTDECL(int) RTFuzzTgtStateAppendStderrFromPipe(RTFUZZTGTSTATE hFuzzTgtState, RTPIPE hPipe)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    return rtFuzzTgtStdOutErrBufFillFromPipe(&pThis->StdErrBuf, hPipe);
}


RTDECL(int) RTFuzzTgtStateAddSanCovReportFromFile(RTFUZZTGTSTATE hFuzzTgtState, const char *pszFilename)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    uint8_t *pbSanCov = NULL;
    size_t cbSanCov = 0;
    int rc = RTFileReadAll(pszFilename, (void **)&pbSanCov, &cbSanCov);
    if (RT_SUCCESS(rc))
    {
        /* Check for the magic identifying whether the offsets are 32bit or 64bit. */
        if (   cbSanCov >= sizeof(uint64_t)
            && (   *(uint64_t *)pbSanCov == SANCOV_MAGIC_64
                || *(uint64_t *)pbSanCov == SANCOV_MAGIC_32))
        {
            uint32_t cbCovOff = sizeof(uint32_t);
            if (*(uint64_t *)pbSanCov == SANCOV_MAGIC_64)
                cbCovOff = sizeof(uint64_t);

            uint32_t cbCovDet = ASMAtomicReadU32(&pThis->pTgtRec->cbCovOff);
            if (!cbCovDet)
            {
                /* Set the detected offset width. */
                if (!ASMAtomicCmpXchgU32(&pThis->pTgtRec->cbCovOff, cbCovOff, 0))
                {
                    /* Someone raced us, check again. */
                    cbCovDet = ASMAtomicReadU32(&pThis->pTgtRec->cbCovOff);
                    Assert(cbCovDet != 0);
                }
                else
                    cbCovDet = cbCovOff;
            }

            if (cbCovDet == cbCovOff)
            {
                /*
                 * Just copy the offsets into the state for now. Now further analysis
                 * is happening right now, just checking whether the content changed for
                 * the states.to spot newly discovered edges.
                 */
                pThis->cbCovReport = cbSanCov - sizeof(uint64_t);
                pThis->pvCovReport = RTMemDup(pbSanCov + sizeof(uint64_t), pThis->cbCovReport);
                if (!pThis->pvCovReport)
                {
                    pThis->cbCovReport = 0;
                    rc = VERR_NO_MEMORY;
                }
            }
            else
                rc = VERR_INVALID_STATE; /* Mixing 32bit and 64bit offsets shouldn't happen, is not supported. */
        }
        else
            rc = VERR_INVALID_STATE;
        RTFileReadAllFree(pbSanCov, cbSanCov);
    }
    return rc;
}


RTDECL(int) RTFuzzTgtStateAddProcSts(RTFUZZTGTSTATE hFuzzTgtState, PCRTPROCSTATUS pProcSts)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pProcSts, VERR_INVALID_POINTER);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    pThis->ProcSts = *pProcSts;
    return VINF_SUCCESS;
}


RTDECL(int) RTFuzzTgtStateDumpToDir(RTFUZZTGTSTATE hFuzzTgtState, const char *pszDirPath)
{
    PRTFUZZTGTSTATEINT pThis = hFuzzTgtState;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pszDirPath, VERR_INVALID_POINTER);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    int rc = VINF_SUCCESS;
    char szPath[RTPATH_MAX];
    if (pThis->StdOutBuf.cbBuf)
    {
        rc = RTPathJoin(szPath, sizeof(szPath), pszDirPath, "stdout"); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = rtFuzzTgtStateStdOutErrBufWriteToFile(&pThis->StdOutBuf, &szPath[0]);
    }

    if (   RT_SUCCESS(rc)
        && pThis->StdErrBuf.cbBuf)
    {
        rc = RTPathJoin(szPath, sizeof(szPath), pszDirPath, "stderr"); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = rtFuzzTgtStateStdOutErrBufWriteToFile(&pThis->StdErrBuf, &szPath[0]);
    }

    return rc;
}

