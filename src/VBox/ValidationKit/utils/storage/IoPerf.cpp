/* $Id: IoPerf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IoPerf - Storage I/O Performance Benchmark.
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
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/ioqueue.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/system.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/zero.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Size multiplier for the random data buffer to seek around. */
#define IOPERF_RAND_DATA_BUF_FACTOR 3


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Forward declaration of the master. */
typedef struct IOPERFMASTER *PIOPERFMASTER;

/**
 * I/O perf supported tests.
 */
typedef enum IOPERFTEST
{
    /** Invalid test handle. */
    IOPERFTEST_INVALID = 0,
    /** The test was disabled. */
    IOPERFTEST_DISABLED,
    IOPERFTEST_FIRST_WRITE,
    IOPERFTEST_SEQ_READ,
    IOPERFTEST_SEQ_WRITE,
    IOPERFTEST_REV_READ,
    IOPERFTEST_REV_WRITE,
    IOPERFTEST_RND_READ,
    IOPERFTEST_RND_WRITE,
    IOPERFTEST_SEQ_READWRITE,
    IOPERFTEST_RND_READWRITE,
    /** Special shutdown test which lets the workers exit, must be LAST. */
    IOPERFTEST_SHUTDOWN,
    IOPERFTEST_32BIT_HACK = 0x7fffffff
} IOPERFTEST;


/**
 * I/O perf test set preparation method.
 */
typedef enum IOPERFTESTSETPREP
{
    IOPERFTESTSETPREP_INVALID = 0,
    /** Just create the file and don't set any sizes. */
    IOPERFTESTSETPREP_JUST_CREATE,
    /** Standard RTFileSetSize() call which might create a sparse file. */
    IOPERFTESTSETPREP_SET_SZ,
    /** Uses RTFileSetAllocationSize() to ensure storage is allocated for the file. */
    IOPERFTESTSETPREP_SET_ALLOC_SZ,
    /** 32bit hack. */
    IOPERFTESTSETPREP_32BIT_HACK = 0x7fffffff
} IOPERFTESTSETPREP;


/**
 * Statistics values for a single request kept around until the
 * test completed for statistics collection.
 */
typedef struct IOPERFREQSTAT
{
    /** Start timestamp for the request. */
    uint64_t                    tsStart;
    /** Completion timestamp for the request. */
    uint64_t                    tsComplete;
} IOPERFREQSTAT;
/** Pointer to a request statistics record. */
typedef IOPERFREQSTAT *PIOPERFREQSTAT;


/**
 * I/O perf request.
 */
typedef struct IOPERFREQ
{
    /** Request operation code. */
    RTIOQUEUEOP                 enmOp;
    /** Start offset. */
    uint64_t                    offXfer;
    /** Transfer size for the request. */
    size_t                      cbXfer;
    /** The buffer used for the transfer. */
    void                        *pvXfer;
    /** This is the statically assigned destination buffer for read requests for this request. */
    void                        *pvXferRead;
    /** Size of the read buffer. */
    size_t                      cbXferRead;
    /** Pointer to statistics record. */
    PIOPERFREQSTAT              pStats;
} IOPERFREQ;
/** Pointer to an I/O perf request. */
typedef IOPERFREQ *PIOPERFREQ;
/** Pointer to a constant I/O perf request. */
typedef const IOPERFREQ *PCIOPERFREQ;


/**
 * I/O perf job data.
 */
typedef struct IOPERFJOB
{
    /** Pointer to the master if multiple jobs are running. */
    PIOPERFMASTER               pMaster;
    /** Job ID. */
    uint32_t                    idJob;
    /** The test this job is executing. */
    volatile IOPERFTEST         enmTest;
    /** The thread executing the job. */
    RTTHREAD                    hThread;
    /** The I/O queue for the job. */
    RTIOQUEUE                   hIoQueue;
    /** The file path used. */
    char                        *pszFilename;
    /** The handle to use for the I/O queue. */
    RTHANDLE                    Hnd;
    /** Multi event semaphore to synchronise with other jobs. */
    RTSEMEVENTMULTI             hSemEvtMultiRendezvous;
    /** The test set size. */
    uint64_t                    cbTestSet;
    /** Size of one I/O block. */
    size_t                      cbIoBlock;
    /** Maximum number of requests to queue. */
    uint32_t                    cReqsMax;
    /** Pointer to the array of request specific data. */
    PIOPERFREQ                  paIoReqs;
    /** Page aligned chunk of memory assigned as read buffers for the individual requests. */
    void                        *pvIoReqReadBuf;
    /** Size of the read memory buffer. */
    size_t                      cbIoReqReadBuf;
    /** Random number generator used. */
    RTRAND                      hRand;
    /** The random data buffer used for writes. */
    uint8_t                     *pbRandWrite;
    /** Size of the random write buffer in 512 byte blocks. */
    uint32_t                    cRandWriteBlocks512B;
    /** Chance in percent to get a write. */
    unsigned                    uWriteChance;
    /** Flag whether to verify read data. */
    bool                        fVerifyReads;
    /** Start timestamp. */
    uint64_t                    tsStart;
    /** End timestamp. for the job. */
    uint64_t                    tsFinish;
    /** Number of request statistic records. */
    uint32_t                    cReqStats;
    /** Index of the next free statistics record to use. */
    uint32_t                    idxReqStatNext;
    /** Array of request statistic records for the whole test. */
    PIOPERFREQSTAT              paReqStats;
    /** Test dependent data. */
    union
    {
        /** Sequential read write. */
        uint64_t                offNextSeq;
        /** Data for random acess. */
        struct
        {
            /** Number of valid entries in the bitmap. */
            uint32_t cBlocks;
            /** Pointer to the bitmap marking accessed blocks. */
            uint8_t *pbMapAccessed;
            /** Number of unaccessed blocks. */
            uint32_t cBlocksLeft;
        } Rnd;
    } Tst;
} IOPERFJOB;
/** Pointer to an I/O Perf job. */
typedef IOPERFJOB *PIOPERFJOB;


/**
 * I/O perf master instance coordinating the job execution.
 */
typedef struct IOPERFMASTER
{
    /** Event semaphore. */
    /** Number of jobs. */
    uint32_t                    cJobs;
    /** Job instances, variable in size. */
    IOPERFJOB                   aJobs[1];
} IOPERFMASTER;


enum
{
    kCmdOpt_First = 128,

    kCmdOpt_FirstWrite = kCmdOpt_First,
    kCmdOpt_NoFirstWrite,
    kCmdOpt_SeqRead,
    kCmdOpt_NoSeqRead,
    kCmdOpt_SeqWrite,
    kCmdOpt_NoSeqWrite,
    kCmdOpt_RndRead,
    kCmdOpt_NoRndRead,
    kCmdOpt_RndWrite,
    kCmdOpt_NoRndWrite,
    kCmdOpt_RevRead,
    kCmdOpt_NoRevRead,
    kCmdOpt_RevWrite,
    kCmdOpt_NoRevWrite,
    kCmdOpt_SeqReadWrite,
    kCmdOpt_NoSeqReadWrite,
    kCmdOpt_RndReadWrite,
    kCmdOpt_NoRndReadWrite,

    kCmdOpt_End
};


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Command line parameters */
static const RTGETOPTDEF g_aCmdOptions[] =
{
    { "--dir",                      'd',                            RTGETOPT_REQ_STRING  },
    { "--relative-dir",             'r',                            RTGETOPT_REQ_NOTHING },

    { "--jobs",                     'j',                            RTGETOPT_REQ_UINT32  },
    { "--io-engine",                'i',                            RTGETOPT_REQ_STRING  },
    { "--test-set-size",            's',                            RTGETOPT_REQ_UINT64  },
    { "--block-size",               'b',                            RTGETOPT_REQ_UINT32  },
    { "--maximum-requests",         'm',                            RTGETOPT_REQ_UINT32  },
    { "--verify-reads",             'y',                            RTGETOPT_REQ_BOOL    },
    { "--use-cache",                'c',                            RTGETOPT_REQ_BOOL    },
    { "--report-io-stats",          't',                            RTGETOPT_REQ_BOOL    },

    { "--first-write",              kCmdOpt_FirstWrite,             RTGETOPT_REQ_NOTHING },
    { "--no-first-write",           kCmdOpt_NoFirstWrite,           RTGETOPT_REQ_NOTHING },
    { "--seq-read",                 kCmdOpt_SeqRead,                RTGETOPT_REQ_NOTHING },
    { "--no-seq-read",              kCmdOpt_NoSeqRead,              RTGETOPT_REQ_NOTHING },
    { "--seq-write",                kCmdOpt_SeqWrite,               RTGETOPT_REQ_NOTHING },
    { "--no-seq-write",             kCmdOpt_NoSeqWrite,             RTGETOPT_REQ_NOTHING },
    { "--rnd-read",                 kCmdOpt_RndRead,                RTGETOPT_REQ_NOTHING },
    { "--no-rnd-read",              kCmdOpt_NoRndRead,              RTGETOPT_REQ_NOTHING },
    { "--rnd-write",                kCmdOpt_RndWrite,               RTGETOPT_REQ_NOTHING },
    { "--no-rnd-write",             kCmdOpt_NoRndWrite,             RTGETOPT_REQ_NOTHING },
    { "--rev-read",                 kCmdOpt_RevRead,                RTGETOPT_REQ_NOTHING },
    { "--no-rev-read",              kCmdOpt_NoRevRead,              RTGETOPT_REQ_NOTHING },
    { "--rev-write",                kCmdOpt_RevWrite,               RTGETOPT_REQ_NOTHING },
    { "--no-rev-write",             kCmdOpt_NoRevWrite,             RTGETOPT_REQ_NOTHING },
    { "--seq-read-write",           kCmdOpt_SeqReadWrite,           RTGETOPT_REQ_NOTHING },
    { "--no-seq-read-write",        kCmdOpt_NoSeqReadWrite,         RTGETOPT_REQ_NOTHING },
    { "--rnd-read-write",           kCmdOpt_RndReadWrite,           RTGETOPT_REQ_NOTHING },
    { "--no-rnd-read-write",        kCmdOpt_NoRndReadWrite,         RTGETOPT_REQ_NOTHING },

    { "--quiet",                    'q',                            RTGETOPT_REQ_NOTHING },
    { "--verbose",                  'v',                            RTGETOPT_REQ_NOTHING },
    { "--version",                  'V',                            RTGETOPT_REQ_NOTHING },
    { "--help",                     'h',                            RTGETOPT_REQ_NOTHING } /* for Usage() */
};

/** The test handle. */
static RTTEST       g_hTest;
/** Verbosity level. */
static uint32_t     g_uVerbosity   = 0;
/** Selected I/O engine for the tests, NULL means pick best default one. */
static const char   *g_pszIoEngine = NULL;
/** Number of jobs to run concurrently. */
static uint32_t     g_cJobs        = 1;
/** Size of each test set (file) in bytes. */
static uint64_t     g_cbTestSet    = _2G;
/** Block size for each request. */
static size_t       g_cbIoBlock    = _4K;
/** Maximum number of concurrent requests for each job. */
static uint32_t     g_cReqsMax     = 16;
/** Flag whether to open the file without caching enabled. */
static bool         g_fNoCache     = true;
/** Write chance for mixed read/write tests. */
static unsigned     g_uWriteChance = 50;
/** Flag whether to verify read data. */
static bool         g_fVerifyReads = true;
/** Flag whether to report I/O statistics after each test. */
static bool         g_fReportIoStats = true;

/** @name Configured tests, this must match the IOPERFTEST order.
 * @{ */
static IOPERFTEST   g_aenmTests[] =
{
    IOPERFTEST_DISABLED, /** @< The invalid test value is disabled of course. */
    IOPERFTEST_DISABLED,
    IOPERFTEST_FIRST_WRITE,
    IOPERFTEST_SEQ_READ,
    IOPERFTEST_SEQ_WRITE,
    IOPERFTEST_REV_READ,
    IOPERFTEST_REV_WRITE,
    IOPERFTEST_RND_READ,
    IOPERFTEST_RND_WRITE,
    IOPERFTEST_SEQ_READWRITE,
    IOPERFTEST_RND_READWRITE,
    IOPERFTEST_SHUTDOWN
};
/** The test index being selected next. */
static uint32_t     g_idxTest      = 2;
/** @} */

/** Set if g_szDir and friends are path relative to CWD rather than absolute. */
static bool         g_fRelativeDir = false;
/** The length of g_szDir. */
static size_t       g_cchDir;

/** The test directory (absolute).  This will always have a trailing slash. */
static char         g_szDir[RTPATH_BIG_MAX];


/*********************************************************************************************************************************
*   Tests                                                                                                                        *
*********************************************************************************************************************************/


/**
 * Selects the next test to run.
 *
 * @return Next test to run.
 */
static IOPERFTEST ioPerfJobTestSelectNext()
{
    AssertReturn(g_idxTest < RT_ELEMENTS(g_aenmTests), IOPERFTEST_SHUTDOWN);

    while (   g_idxTest < RT_ELEMENTS(g_aenmTests)
           && g_aenmTests[g_idxTest] == IOPERFTEST_DISABLED)
        g_idxTest++;

    AssertReturn(g_idxTest < RT_ELEMENTS(g_aenmTests), IOPERFTEST_SHUTDOWN);

    return g_aenmTests[g_idxTest++];
}


/**
 * Returns the I/O queue operation for the next request.
 *
 * @returns I/O queue operation enum.
 * @param   pJob                The job data for the current worker.
 */
static RTIOQUEUEOP ioPerfJobTestGetIoQOp(PIOPERFJOB pJob)
{
    switch (pJob->enmTest)
    {
        case IOPERFTEST_FIRST_WRITE:
        case IOPERFTEST_SEQ_WRITE:
        case IOPERFTEST_REV_WRITE:
        case IOPERFTEST_RND_WRITE:
            return RTIOQUEUEOP_WRITE;

        case IOPERFTEST_SEQ_READ:
        case IOPERFTEST_RND_READ:
        case IOPERFTEST_REV_READ:
            return RTIOQUEUEOP_READ;

        case IOPERFTEST_SEQ_READWRITE:
        case IOPERFTEST_RND_READWRITE:
        {
            uint32_t uRnd = RTRandAdvU32Ex(pJob->hRand, 0, 100);
            return (uRnd < pJob->uWriteChance) ? RTIOQUEUEOP_WRITE : RTIOQUEUEOP_READ;
        }

        default:
            AssertMsgFailed(("Invalid/unknown test selected: %d\n", pJob->enmTest));
            break;
    }

    return RTIOQUEUEOP_INVALID;
}


/**
 * Returns the offset to use for the next request.
 *
 * @returns Offset to use.
 * @param   pJob                The job data for the current worker.
 */
static uint64_t ioPerfJobTestGetOffsetNext(PIOPERFJOB pJob)
{
    uint64_t offNext = 0;

    switch (pJob->enmTest)
    {
        case IOPERFTEST_FIRST_WRITE:
        case IOPERFTEST_SEQ_WRITE:
        case IOPERFTEST_SEQ_READ:
        case IOPERFTEST_SEQ_READWRITE:
            offNext = pJob->Tst.offNextSeq;
            pJob->Tst.offNextSeq += pJob->cbIoBlock;
            break;
        case IOPERFTEST_REV_WRITE:
        case IOPERFTEST_REV_READ:
            offNext = pJob->Tst.offNextSeq;
            if (pJob->Tst.offNextSeq == 0)
                pJob->Tst.offNextSeq = pJob->cbTestSet;
            else
                pJob->Tst.offNextSeq -= pJob->cbIoBlock;
            break;
        case IOPERFTEST_RND_WRITE:
        case IOPERFTEST_RND_READ:
        case IOPERFTEST_RND_READWRITE:
        {
            int idx = -1;

            idx = ASMBitFirstClear(pJob->Tst.Rnd.pbMapAccessed, pJob->Tst.Rnd.cBlocks);

            /* In case this is the last request we don't need to search further. */
            if (pJob->Tst.Rnd.cBlocksLeft > 1)
            {
                int idxIo;
                idxIo = RTRandAdvU32Ex(pJob->hRand, idx, pJob->Tst.Rnd.cBlocks - 1);

                /*
                 * If the bit is marked free use it, otherwise search for the next free bit
                 * and if that doesn't work use the first free bit.
                 */
                if (ASMBitTest(pJob->Tst.Rnd.pbMapAccessed, idxIo))
                {
                    idxIo = ASMBitNextClear(pJob->Tst.Rnd.pbMapAccessed, pJob->Tst.Rnd.cBlocks, idxIo);
                    if (idxIo != -1)
                        idx = idxIo;
                }
                else
                    idx = idxIo;
            }

            Assert(idx != -1);
            offNext = (uint64_t)idx * pJob->cbIoBlock;
            pJob->Tst.Rnd.cBlocksLeft--;
            ASMBitSet(pJob->Tst.Rnd.pbMapAccessed, idx);
            break;
        }
        default:
            AssertMsgFailed(("Invalid/unknown test selected: %d\n", pJob->enmTest));
            break;
    }

    return offNext;
}


/**
 * Returns a pointer to the write buffer with random data for the given offset which
 * is predictable for data verification.
 *
 * @returns Pointer to I/O block sized data buffer with random data.
 * @param   pJob                The job data for the current worker.
 * @param   off                 The offset to get the buffer for.
 */
static void *ioPerfJobTestGetWriteBufForOffset(PIOPERFJOB pJob, uint64_t off)
{
    /*
     * Dividing the file into 512 byte blocks so buffer pointers are at least
     * 512 byte aligned to work with async I/O on some platforms (Linux and O_DIRECT for example).
     */
    uint64_t uBlock = off / 512;
    uint32_t idxBuf = uBlock % pJob->cRandWriteBlocks512B;
    return pJob->pbRandWrite + idxBuf * 512;
}


/**
 * Initialize the given request for submission.
 *
 * @param   pJob                The job data for the current worker.
 * @param   pIoReq              The request to initialize.
 */
static void ioPerfJobTestReqInit(PIOPERFJOB pJob, PIOPERFREQ pIoReq)
{
    pIoReq->enmOp   = ioPerfJobTestGetIoQOp(pJob);
    pIoReq->offXfer = ioPerfJobTestGetOffsetNext(pJob);
    pIoReq->cbXfer  = pJob->cbIoBlock;
    if (pIoReq->enmOp == RTIOQUEUEOP_READ)
        pIoReq->pvXfer = pIoReq->pvXferRead;
    else if (pIoReq->enmOp == RTIOQUEUEOP_WRITE)
        pIoReq->pvXfer = ioPerfJobTestGetWriteBufForOffset(pJob, pIoReq->offXfer);
    else /* Flush */
        pIoReq->pvXfer = NULL;

    Assert(pJob->idxReqStatNext < pJob->cReqStats);
    if (RT_LIKELY(pJob->idxReqStatNext < pJob->cReqStats))
    {
        pIoReq->pStats = &pJob->paReqStats[pJob->idxReqStatNext++];
        pIoReq->pStats->tsStart = RTTimeNanoTS();
    }
    else
        pIoReq->pStats = NULL;
}


/**
 * Returns a stringified version of the test given.
 *
 * @returns Pointer to string representation of the test.
 * @param   enmTest             The test to stringify.
 */
static const char *ioPerfJobTestStringify(IOPERFTEST enmTest)
{
    switch (enmTest)
    {
        case IOPERFTEST_FIRST_WRITE:
            return "FirstWrite";
        case IOPERFTEST_SEQ_WRITE:
            return "SequentialWrite";
        case IOPERFTEST_SEQ_READ:
            return "SequentialRead";
        case IOPERFTEST_REV_WRITE:
            return "ReverseWrite";
        case IOPERFTEST_REV_READ:
            return "ReverseRead";
        case IOPERFTEST_RND_WRITE:
            return "RandomWrite";
        case IOPERFTEST_RND_READ:
            return "RandomRead";
        case IOPERFTEST_SEQ_READWRITE:
            return "SequentialReadWrite";
        case IOPERFTEST_RND_READWRITE:
            return "RandomReadWrite";
        default:
            AssertMsgFailed(("Invalid/unknown test selected: %d\n", enmTest));
            break;
    }

    return "INVALID_TEST";
}


/**
 * Initializes the test state for the current test.
 *
 * @returns IPRT status code.
 * @param   pJob                The job data for the current worker.
 */
static int ioPerfJobTestInit(PIOPERFJOB pJob)
{
    int rc = VINF_SUCCESS;

    pJob->idxReqStatNext = 0;

    switch (pJob->enmTest)
    {
        case IOPERFTEST_FIRST_WRITE:
        case IOPERFTEST_SEQ_WRITE:
        case IOPERFTEST_SEQ_READ:
        case IOPERFTEST_SEQ_READWRITE:
            pJob->Tst.offNextSeq = 0;
            break;
        case IOPERFTEST_REV_WRITE:
        case IOPERFTEST_REV_READ:
            pJob->Tst.offNextSeq = pJob->cbTestSet - pJob->cbIoBlock;
            break;
        case IOPERFTEST_RND_WRITE:
        case IOPERFTEST_RND_READ:
        case IOPERFTEST_RND_READWRITE:
        {
            pJob->Tst.Rnd.cBlocks = (uint32_t)(  pJob->cbTestSet / pJob->cbIoBlock
                                               + (pJob->cbTestSet % pJob->cbIoBlock ? 1 : 0));
            pJob->Tst.Rnd.cBlocksLeft = pJob->Tst.Rnd.cBlocks;
            pJob->Tst.Rnd.pbMapAccessed = (uint8_t *)RTMemAllocZ(   pJob->Tst.Rnd.cBlocks / 8
                                                                 +    ((pJob->Tst.Rnd.cBlocks % 8)
                                                                    ? 1
                                                                    : 0));
            if (!pJob->Tst.Rnd.pbMapAccessed)
                rc = VERR_NO_MEMORY;
            break;
        }
        default:
            AssertMsgFailed(("Invalid/unknown test selected: %d\n", pJob->enmTest));
            break;
    }

    pJob->tsStart = RTTimeNanoTS();
    return rc;
}


/**
 * Frees allocated resources specific for the current test.
 *
 * @param   pJob                The job data for the current worker.
 */
static void ioPerfJobTestFinish(PIOPERFJOB pJob)
{
    pJob->tsFinish = RTTimeNanoTS();

    switch (pJob->enmTest)
    {
        case IOPERFTEST_FIRST_WRITE:
        case IOPERFTEST_SEQ_WRITE:
        case IOPERFTEST_SEQ_READ:
        case IOPERFTEST_REV_WRITE:
        case IOPERFTEST_REV_READ:
        case IOPERFTEST_SEQ_READWRITE:
            break; /* Nothing to do. */

        case IOPERFTEST_RND_WRITE:
        case IOPERFTEST_RND_READ:
        case IOPERFTEST_RND_READWRITE:
            RTMemFree(pJob->Tst.Rnd.pbMapAccessed);
            break;
        default:
            AssertMsgFailed(("Invalid/unknown test selected: %d\n", pJob->enmTest));
            break;
    }
}


/**
 * Returns whether the current test is done with submitting new requests (reached test set size).
 *
 * @returns True when the test has submitted all required requests, false if there are still requests required
 */
static bool ioPerfJobTestIsDone(PIOPERFJOB pJob)
{
    switch (pJob->enmTest)
    {
        case IOPERFTEST_FIRST_WRITE:
        case IOPERFTEST_SEQ_WRITE:
        case IOPERFTEST_SEQ_READ:
        case IOPERFTEST_REV_WRITE:
        case IOPERFTEST_REV_READ:
        case IOPERFTEST_SEQ_READWRITE:
            return pJob->Tst.offNextSeq == pJob->cbTestSet;
        case IOPERFTEST_RND_WRITE:
        case IOPERFTEST_RND_READ:
        case IOPERFTEST_RND_READWRITE:
            return pJob->Tst.Rnd.cBlocksLeft == 0;
            break;
        default:
            AssertMsgFailed(("Invalid/unknown test selected: %d\n", pJob->enmTest));
            break;
    }

    return true;
}


/**
 * The test I/O loop pumping I/O.
 *
 * @returns IPRT status code.
 * @param   pJob                The job data for the current worker.
 */
static int ioPerfJobTestIoLoop(PIOPERFJOB pJob)
{
    int rc = ioPerfJobTestInit(pJob);
    if (RT_SUCCESS(rc))
    {
        /* Allocate the completion event array. */
        uint32_t cReqsQueued = 0;
        PRTIOQUEUECEVT paIoQCEvt = (PRTIOQUEUECEVT)RTMemAllocZ(pJob->cReqsMax * sizeof(RTIOQUEUECEVT));
        if (RT_LIKELY(paIoQCEvt))
        {
            /* Queue requests up to the maximum. */
            while (   (cReqsQueued < pJob->cReqsMax)
                   && !ioPerfJobTestIsDone(pJob)
                   && RT_SUCCESS(rc))
            {
                PIOPERFREQ pReq = &pJob->paIoReqs[cReqsQueued];
                ioPerfJobTestReqInit(pJob, pReq);
                RTTESTI_CHECK_RC(RTIoQueueRequestPrepare(pJob->hIoQueue, &pJob->Hnd, pReq->enmOp,
                                                         pReq->offXfer, pReq->pvXfer, pReq->cbXfer, 0 /*fReqFlags*/,
                                                         pReq), VINF_SUCCESS);
                cReqsQueued++;
            }

            /* Commit the prepared requests. */
            if (   RT_SUCCESS(rc)
                && cReqsQueued)
            {
                RTTESTI_CHECK_RC(RTIoQueueCommit(pJob->hIoQueue), VINF_SUCCESS);
            }

            /* Enter wait loop and process completed requests. */
            while (   RT_SUCCESS(rc)
                   && cReqsQueued)
            {
                uint32_t cCEvtCompleted = 0;

                RTTESTI_CHECK_RC(RTIoQueueEvtWait(pJob->hIoQueue, paIoQCEvt, pJob->cReqsMax, 1 /*cMinWait*/,
                                                  &cCEvtCompleted, 0 /*fFlags*/), VINF_SUCCESS);
                if (RT_SUCCESS(rc))
                {
                    uint32_t cReqsThisQueued = 0;

                    /* Process any completed event and continue to fill the queue as long as there is stuff to do. */
                    for (uint32_t i = 0; i < cCEvtCompleted; i++)
                    {
                        PIOPERFREQ pReq = (PIOPERFREQ)paIoQCEvt[i].pvUser;

                        if (RT_SUCCESS(paIoQCEvt[i].rcReq))
                        {
                            Assert(paIoQCEvt[i].cbXfered == pReq->cbXfer);

                            if (pReq->pStats)
                                pReq->pStats->tsComplete = RTTimeNanoTS();

                            if (   pJob->fVerifyReads
                                && pReq->enmOp == RTIOQUEUEOP_READ)
                            {
                                const void *pvBuf = ioPerfJobTestGetWriteBufForOffset(pJob, pReq->offXfer);
                                if (memcmp(pReq->pvXferRead, pvBuf, pReq->cbXfer))
                                {
                                    if (g_uVerbosity > 1)
                                        RTTestIFailed("IoPerf: Corrupted data detected by read at offset %#llu (sz: %zu)", pReq->offXfer, pReq->cbXfer);
                                    else
                                        RTTestIErrorInc();
                                }
                            }

                            if (!ioPerfJobTestIsDone(pJob))
                            {
                                ioPerfJobTestReqInit(pJob, pReq);
                                RTTESTI_CHECK_RC(RTIoQueueRequestPrepare(pJob->hIoQueue, &pJob->Hnd, pReq->enmOp,
                                                                         pReq->offXfer, pReq->pvXfer, pReq->cbXfer, 0 /*fReqFlags*/,
                                                                         pReq), VINF_SUCCESS);
                                cReqsThisQueued++;
                            }
                            else
                                cReqsQueued--;
                        }
                        else
                            RTTestIErrorInc();
                    }

                    if (   cReqsThisQueued
                        && RT_SUCCESS(rc))
                    {
                        RTTESTI_CHECK_RC(RTIoQueueCommit(pJob->hIoQueue), VINF_SUCCESS);
                    }
                }
            }

            RTMemFree(paIoQCEvt);
        }

        ioPerfJobTestFinish(pJob);
    }

    return rc;
}


/**
 * Calculates the statistic values for the given job after a
 * test finished.
 *
 * @param   pJob                The job data.
 */
static void ioPerfJobStats(PIOPERFJOB pJob)
{
    const char *pszTest = ioPerfJobTestStringify(pJob->enmTest);
    uint64_t nsJobRuntime = pJob->tsFinish - pJob->tsStart;
    RTTestIValueF(nsJobRuntime, RTTESTUNIT_NS, "%s/Job/%RU32/Runtime", pszTest, pJob->idJob);

    uint64_t *paReqRuntimeNs = (uint64_t *)RTMemAllocZ(pJob->cReqStats * sizeof(uint64_t));
    if (RT_LIKELY(paReqRuntimeNs))
    {
        /* Calculate runtimes for each request first. */
        for (uint32_t i = 0; i < pJob->cReqStats; i++)
        {
            PIOPERFREQSTAT pStat = &pJob->paReqStats[i];
            paReqRuntimeNs[i] = pStat->tsComplete - pStat->tsStart;
        }

        /* Get average bandwidth for the job. */
        RTTestIValueF((uint64_t)((double)pJob->cbTestSet / ((double)nsJobRuntime / RT_NS_1SEC)),
                       RTTESTUNIT_BYTES_PER_SEC, "%s/Job/%RU32/AvgBandwidth", pszTest, pJob->idJob);

        RTTestIValueF((uint64_t)(pJob->cReqStats / ((double)nsJobRuntime / RT_NS_1SEC)),
                       RTTESTUNIT_OCCURRENCES_PER_SEC, "%s/Job/%RU32/AvgIops", pszTest, pJob->idJob);

        /* Calculate the average latency for the requests. */
        uint64_t uLatency = 0;
        for (uint32_t i = 0; i < pJob->cReqStats; i++)
            uLatency += paReqRuntimeNs[i];
        RTTestIValueF(uLatency / pJob->cReqStats, RTTESTUNIT_NS, "%s/Job/%RU32/AvgLatency", pszTest, pJob->idJob);

        RTMemFree(paReqRuntimeNs);
    }
    else
        RTTestIErrorInc();
}


/**
 * Synchronizes with the other jobs and waits for the current test to execute.
 *
 * @returns IPRT status.
 * @param   pJob                The job data for the current worker.
 */
static int ioPerfJobSync(PIOPERFJOB pJob)
{
    if (pJob->pMaster)
    {
        /* Enter the rendezvous semaphore. */
        int rc = VINF_SUCCESS;

        return rc;
    }

    /* Single threaded run, collect the results from our current test and select the next test. */
    /** @todo Results and statistics collection. */
    pJob->enmTest = ioPerfJobTestSelectNext();
    return VINF_SUCCESS;
}


/**
 * I/O perf job main work loop.
 *
 * @returns IPRT status code.
 * @param   pJob                The job data for the current worker.
 */
static int ioPerfJobWorkLoop(PIOPERFJOB pJob)
{
    int rc = VINF_SUCCESS;

    for (;;)
    {
        /* Synchronize with the other jobs and the master. */
        rc = ioPerfJobSync(pJob);
        if (RT_FAILURE(rc))
            break;

        if (pJob->enmTest == IOPERFTEST_SHUTDOWN)
            break;

        rc = ioPerfJobTestIoLoop(pJob);
        if (RT_FAILURE(rc))
            break;

        /*
         * Do the statistics here for a single job run,
         * the master will do this for each job and combined statistics
         * otherwise.
         */
        if (   !pJob->pMaster
            && g_fReportIoStats)
            ioPerfJobStats(pJob);
    }

    return rc;
}


/**
 * Job thread entry point.
 */
static DECLCALLBACK(int) ioPerfJobThread(RTTHREAD hThrdSelf, void *pvUser)
{
    RT_NOREF(hThrdSelf);

    PIOPERFJOB pJob = (PIOPERFJOB)pvUser;
    return ioPerfJobWorkLoop(pJob);
}


/**
 * Prepares the test set by laying out the files and filling them with data.
 *
 * @returns IPRT status code.
 * @param   pJob                The job to initialize.
 */
static int ioPerfJobTestSetPrep(PIOPERFJOB pJob)
{
    int rc = RTRandAdvCreateParkMiller(&pJob->hRand);
    if (RT_SUCCESS(rc))
    {
        rc = RTRandAdvSeed(pJob->hRand, RTTimeNanoTS());
        if (RT_SUCCESS(rc))
        {
            /*
             * Create a random data buffer for writes, we'll use multiple of the I/O block size to
             * be able to seek in the buffer quite a bit to make the file content as random as possible
             * to avoid mechanisms like compression or deduplication for now which can influence storage
             * benchmarking unpredictably.
             */
            pJob->cRandWriteBlocks512B = (uint32_t)(((IOPERF_RAND_DATA_BUF_FACTOR - 1) * pJob->cbIoBlock) / 512);
            pJob->pbRandWrite = (uint8_t *)RTMemPageAllocZ(IOPERF_RAND_DATA_BUF_FACTOR * pJob->cbIoBlock);
            if (RT_LIKELY(pJob->pbRandWrite))
            {
                RTRandAdvBytes(pJob->hRand, pJob->pbRandWrite, IOPERF_RAND_DATA_BUF_FACTOR * pJob->cbIoBlock);

                /* Write the content here if the first write test is disabled. */
                if (g_aenmTests[IOPERFTEST_FIRST_WRITE] == IOPERFTEST_DISABLED)
                {
                    for (uint64_t off = 0; off < pJob->cbTestSet && RT_SUCCESS(rc); off += pJob->cbIoBlock)
                    {
                        void *pvWrite = ioPerfJobTestGetWriteBufForOffset(pJob, off);
                        rc = RTFileWriteAt(pJob->Hnd.u.hFile, off, pvWrite, pJob->cbIoBlock, NULL);
                    }
                }

                if (RT_SUCCESS(rc))
                    return rc;

                RTMemPageFree(pJob->pbRandWrite, IOPERF_RAND_DATA_BUF_FACTOR * pJob->cbIoBlock);
            }
        }
        RTRandAdvDestroy(pJob->hRand);
    }

    return rc;
}


/**
 * Initializes the given job instance.
 *
 * @returns IPRT status code.
 * @param   pJob                The job to initialize.
 * @param   pMaster             The coordination master if any.
 * @param   idJob               ID of the job.
 * @param   pszIoEngine         I/O queue engine for this job, NULL for best default.
 * @param   pszTestDir          The test directory to create the file in - requires a slash a the end.
 * @param   enmPrepMethod       Test set preparation method to use.
 * @param   cbTestSet           Size of the test set ofr this job.
 * @param   cbIoBlock           I/O block size for the given job.
 * @param   cReqsMax            Maximum number of concurrent requests for this job.
 * @param   uWriteChance        The write chance for mixed read/write tests.
 * @param   fVerifyReads        Flag whether to verify read data.
 */
static int ioPerfJobInit(PIOPERFJOB pJob, PIOPERFMASTER pMaster, uint32_t idJob,
                         const char *pszIoEngine, const char *pszTestDir,
                         IOPERFTESTSETPREP enmPrepMethod,
                         uint64_t cbTestSet, size_t cbIoBlock, uint32_t cReqsMax,
                         unsigned uWriteChance, bool fVerifyReads)
{
    pJob->pMaster        = pMaster;
    pJob->idJob          = idJob;
    pJob->enmTest        = IOPERFTEST_INVALID;
    pJob->hThread        = NIL_RTTHREAD;
    pJob->Hnd.enmType    = RTHANDLETYPE_FILE;
    pJob->cbTestSet      = cbTestSet;
    pJob->cbIoBlock      = cbIoBlock;
    pJob->cReqsMax       = cReqsMax;
    pJob->cbIoReqReadBuf = cReqsMax * cbIoBlock;
    pJob->uWriteChance   = uWriteChance;
    pJob->fVerifyReads   = fVerifyReads;
    pJob->cReqStats      = (uint32_t)(pJob->cbTestSet / pJob->cbIoBlock + ((pJob->cbTestSet % pJob->cbIoBlock) ? 1 : 0));
    pJob->idxReqStatNext = 0;

    int rc = VINF_SUCCESS;
    pJob->paIoReqs = (PIOPERFREQ)RTMemAllocZ(cReqsMax * sizeof(IOPERFREQ));
    if (RT_LIKELY(pJob->paIoReqs))
    {
        pJob->paReqStats = (PIOPERFREQSTAT)RTMemAllocZ(pJob->cReqStats * sizeof(IOPERFREQSTAT));
        if (RT_LIKELY(pJob->paReqStats))
        {
            pJob->pvIoReqReadBuf = RTMemPageAlloc(pJob->cbIoReqReadBuf);
            if (RT_LIKELY(pJob->pvIoReqReadBuf))
            {
                uint8_t *pbReadBuf = (uint8_t *)pJob->pvIoReqReadBuf;

                for (uint32_t i = 0; i < cReqsMax; i++)
                {
                    pJob->paIoReqs[i].pvXferRead = pbReadBuf;
                    pJob->paIoReqs[i].cbXferRead = cbIoBlock;
                    pbReadBuf += cbIoBlock;
                }

                /* Create the file. */
                pJob->pszFilename = RTStrAPrintf2("%sioperf-%u.file", pszTestDir, idJob);
                if (RT_LIKELY(pJob->pszFilename))
                {
                    uint32_t fOpen = RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE | RTFILE_O_READWRITE | RTFILE_O_ASYNC_IO;
                    if (g_fNoCache)
                        fOpen |= RTFILE_O_NO_CACHE;
                    rc = RTFileOpen(&pJob->Hnd.u.hFile, pJob->pszFilename, fOpen);
                    if (RT_SUCCESS(rc))
                    {
                        switch (enmPrepMethod)
                        {
                            case IOPERFTESTSETPREP_JUST_CREATE:
                                break;
                            case IOPERFTESTSETPREP_SET_SZ:
                                rc = RTFileSetSize(pJob->Hnd.u.hFile, pJob->cbTestSet);
                                break;
                            case IOPERFTESTSETPREP_SET_ALLOC_SZ:
                                rc = RTFileSetAllocationSize(pJob->Hnd.u.hFile, pJob->cbTestSet, RTFILE_ALLOC_SIZE_F_DEFAULT);
                                break;
                            default:
                                AssertMsgFailed(("Invalid file preparation method: %d\n", enmPrepMethod));
                        }

                        if (RT_SUCCESS(rc))
                        {
                            rc = ioPerfJobTestSetPrep(pJob);
                            if (RT_SUCCESS(rc))
                            {
                                /* Create I/O queue. */
                                PCRTIOQUEUEPROVVTABLE pIoQProv = NULL;
                                if (!pszIoEngine)
                                    pIoQProv = RTIoQueueProviderGetBestForHndType(RTHANDLETYPE_FILE);
                                else
                                    pIoQProv = RTIoQueueProviderGetById(pszIoEngine);

                                if (RT_LIKELY(pIoQProv))
                                {
                                    rc = RTIoQueueCreate(&pJob->hIoQueue, pIoQProv, 0 /*fFlags*/, cReqsMax, cReqsMax);
                                    if (RT_SUCCESS(rc))
                                    {
                                        rc = RTIoQueueHandleRegister(pJob->hIoQueue, &pJob->Hnd);
                                        if (RT_SUCCESS(rc))
                                        {
                                            /* Spin up the worker thread. */
                                            if (pMaster)
                                                rc = RTThreadCreateF(&pJob->hThread, ioPerfJobThread, pJob, 0,
                                                                     RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "ioperf-%u", idJob);

                                            if (RT_SUCCESS(rc))
                                                return VINF_SUCCESS;
                                        }
                                    }
                                }
                                else
                                    rc = VERR_NOT_SUPPORTED;
                            }

                            RTRandAdvDestroy(pJob->hRand);
                        }

                        RTFileClose(pJob->Hnd.u.hFile);
                        RTFileDelete(pJob->pszFilename);
                    }

                    RTStrFree(pJob->pszFilename);
                }
                else
                    rc = VERR_NO_STR_MEMORY;

                RTMemPageFree(pJob->pvIoReqReadBuf, pJob->cbIoReqReadBuf);
            }
            else
                rc = VERR_NO_MEMORY;

            RTMemFree(pJob->paReqStats);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Teardown a job instance and free all associated resources.
 *
 * @returns IPRT status code.
 * @param   pJob                The job to teardown.
 */
static int ioPerfJobTeardown(PIOPERFJOB pJob)
{
    if (pJob->pMaster)
    {
        int rc = RTThreadWait(pJob->hThread, RT_INDEFINITE_WAIT, NULL);
        AssertRC(rc); RT_NOREF(rc);
    }

    RTIoQueueHandleDeregister(pJob->hIoQueue, &pJob->Hnd);
    RTIoQueueDestroy(pJob->hIoQueue);
    RTRandAdvDestroy(pJob->hRand);
    RTMemPageFree(pJob->pbRandWrite, IOPERF_RAND_DATA_BUF_FACTOR * pJob->cbIoBlock);
    RTFileClose(pJob->Hnd.u.hFile);
    RTFileDelete(pJob->pszFilename);
    RTStrFree(pJob->pszFilename);
    RTMemPageFree(pJob->pvIoReqReadBuf, pJob->cbIoReqReadBuf);
    RTMemFree(pJob->paIoReqs);
    RTMemFree(pJob->paReqStats);
    return VINF_SUCCESS;
}


/**
 * Single job testing entry point.
 *
 * @returns IPRT status code.
 */
static int ioPerfDoTestSingle(void)
{
    IOPERFJOB Job;

    int rc = ioPerfJobInit(&Job, NULL, 0, g_pszIoEngine,
                           g_szDir, IOPERFTESTSETPREP_SET_SZ,
                           g_cbTestSet, g_cbIoBlock, g_cReqsMax,
                           g_uWriteChance, g_fVerifyReads);
    if (RT_SUCCESS(rc))
    {
        rc = ioPerfJobWorkLoop(&Job);
        if (RT_SUCCESS(rc))
        {
            rc = ioPerfJobTeardown(&Job);
            AssertRC(rc); RT_NOREF(rc);
        }
    }

    return rc;
}


/**
 * Multi job testing entry point.
 *
 * @returns IPRT status code.
 */
static int ioPerfDoTestMulti(void)
{
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Display the usage to @a pStrm.
 */
static void Usage(PRTSTREAM pStrm)
{
    char szExec[RTPATH_MAX];
    RTStrmPrintf(pStrm, "usage: %s <-d <testdir>> [options]\n",
                 RTPathFilename(RTProcGetExecutablePath(szExec, sizeof(szExec))));
    RTStrmPrintf(pStrm, "\n");
    RTStrmPrintf(pStrm, "options: \n");

    for (unsigned i = 0; i < RT_ELEMENTS(g_aCmdOptions); i++)
    {
        char szHelp[80];
        const char *pszHelp;
        switch (g_aCmdOptions[i].iShort)
        {
            case 'd':                           pszHelp = "The directory to use for testing.            default: CWD/fstestdir"; break;
            case 'r':                           pszHelp = "Don't abspath test dir (good for deep dirs). default: disabled"; break;
            case 'y':                           pszHelp = "Flag whether to verify read data.            default: enabled"; break;
            case 'c':                           pszHelp = "Flag whether to use the filesystem cache.    default: disabled"; break;
            case 'v':                           pszHelp = "More verbose execution."; break;
            case 'q':                           pszHelp = "Quiet execution."; break;
            case 'h':                           pszHelp = "Displays this help and exit"; break;
            case 'V':                           pszHelp = "Displays the program revision"; break;
            default:
                if (g_aCmdOptions[i].iShort >= kCmdOpt_First)
                {
                    if (RTStrStartsWith(g_aCmdOptions[i].pszLong, "--no-"))
                        RTStrPrintf(szHelp, sizeof(szHelp), "Disables the '%s' test.", g_aCmdOptions[i].pszLong + 5);
                    else
                        RTStrPrintf(szHelp, sizeof(szHelp), "Enables  the '%s' test.", g_aCmdOptions[i].pszLong + 2);
                    pszHelp = szHelp;
                }
                else
                    pszHelp = "Option undocumented";
                break;
        }
        if ((unsigned)g_aCmdOptions[i].iShort < 127U)
        {
            char szOpt[64];
            RTStrPrintf(szOpt, sizeof(szOpt), "%s, -%c", g_aCmdOptions[i].pszLong, g_aCmdOptions[i].iShort);
            RTStrmPrintf(pStrm, "  %-19s %s\n", szOpt, pszHelp);
        }
        else
            RTStrmPrintf(pStrm, "  %-19s %s\n", g_aCmdOptions[i].pszLong, pszHelp);
    }
}


int main(int argc, char *argv[])
{
    /*
     * Init IPRT and globals.
     */
    int rc = RTTestInitAndCreate("IoPerf", &g_hTest);
    if (rc)
        return rc;

    /*
     * Default values.
     */
    char szDefaultDir[32];
    const char *pszDir = szDefaultDir;
    RTStrPrintf(szDefaultDir, sizeof(szDefaultDir), "ioperfdir-%u" RTPATH_SLASH_STR, RTProcSelf());

    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, g_aCmdOptions, RT_ELEMENTS(g_aCmdOptions), 1, 0 /* fFlags */);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'd':
                pszDir = ValueUnion.psz;
                break;

            case 'r':
                g_fRelativeDir = true;
                break;

            case 'i':
                g_pszIoEngine = ValueUnion.psz;
                break;

            case 's':
                g_cbTestSet = ValueUnion.u64;
                break;

            case 'b':
                g_cbIoBlock = ValueUnion.u32;
                break;

            case 'm':
                g_cReqsMax = ValueUnion.u32;
                break;

            case 'y':
                g_fVerifyReads = ValueUnion.f;
                break;

            case 'c':
                g_fNoCache = !ValueUnion.f;
                break;

            case 't':
                g_fReportIoStats = ValueUnion.f;
                break;

            case kCmdOpt_FirstWrite:
                g_aenmTests[IOPERFTEST_FIRST_WRITE] = IOPERFTEST_FIRST_WRITE;
                break;
            case kCmdOpt_NoFirstWrite:
                g_aenmTests[IOPERFTEST_FIRST_WRITE] = IOPERFTEST_DISABLED;
                break;
            case kCmdOpt_SeqRead:
                g_aenmTests[IOPERFTEST_SEQ_READ] = IOPERFTEST_SEQ_READ;
                break;
            case kCmdOpt_NoSeqRead:
                g_aenmTests[IOPERFTEST_SEQ_READ] = IOPERFTEST_DISABLED;
                break;
            case kCmdOpt_SeqWrite:
                g_aenmTests[IOPERFTEST_SEQ_WRITE] = IOPERFTEST_SEQ_WRITE;
                break;
            case kCmdOpt_NoSeqWrite:
                g_aenmTests[IOPERFTEST_SEQ_WRITE] = IOPERFTEST_DISABLED;
                break;
            case kCmdOpt_RndRead:
                g_aenmTests[IOPERFTEST_RND_READ] = IOPERFTEST_RND_READ;
                break;
            case kCmdOpt_NoRndRead:
                g_aenmTests[IOPERFTEST_RND_READ] = IOPERFTEST_DISABLED;
                break;
            case kCmdOpt_RndWrite:
                g_aenmTests[IOPERFTEST_RND_WRITE] = IOPERFTEST_RND_WRITE;
                break;
            case kCmdOpt_NoRndWrite:
                g_aenmTests[IOPERFTEST_RND_WRITE] = IOPERFTEST_DISABLED;
                break;
            case kCmdOpt_RevRead:
                g_aenmTests[IOPERFTEST_REV_READ] = IOPERFTEST_REV_READ;
                break;
            case kCmdOpt_NoRevRead:
                g_aenmTests[IOPERFTEST_REV_READ] = IOPERFTEST_DISABLED;
                break;
            case kCmdOpt_RevWrite:
                g_aenmTests[IOPERFTEST_REV_WRITE] = IOPERFTEST_REV_WRITE;
                break;
            case kCmdOpt_NoRevWrite:
                g_aenmTests[IOPERFTEST_REV_WRITE] = IOPERFTEST_DISABLED;
                break;
            case kCmdOpt_SeqReadWrite:
                g_aenmTests[IOPERFTEST_SEQ_READWRITE] = IOPERFTEST_SEQ_READWRITE;
                break;
            case kCmdOpt_NoSeqReadWrite:
                g_aenmTests[IOPERFTEST_SEQ_READWRITE] = IOPERFTEST_DISABLED;
                break;
            case kCmdOpt_RndReadWrite:
                g_aenmTests[IOPERFTEST_RND_READWRITE] = IOPERFTEST_RND_READWRITE;
                break;
            case kCmdOpt_NoRndReadWrite:
                g_aenmTests[IOPERFTEST_RND_READWRITE] = IOPERFTEST_DISABLED;
                break;

            case 'q':
                g_uVerbosity = 0;
                break;

            case 'v':
                g_uVerbosity++;
                break;

            case 'h':
                Usage(g_pStdOut);
                return RTEXITCODE_SUCCESS;

            case 'V':
            {
                char szRev[] = "$Revision: 106320 $";
                szRev[RT_ELEMENTS(szRev) - 2] = '\0';
                RTPrintf(RTStrStrip(strchr(szRev, ':') + 1));
                return RTEXITCODE_SUCCESS;
            }

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Populate g_szDir.
     */
    if (!g_fRelativeDir)
        rc = RTPathAbs(pszDir, g_szDir, sizeof(g_szDir));
    else
        rc = RTStrCopy(g_szDir, sizeof(g_szDir), pszDir);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "%s(%s) failed: %Rrc\n", g_fRelativeDir ? "RTStrCopy" : "RTAbsPath", pszDir, rc);
        return RTTestSummaryAndDestroy(g_hTest);
    }
    RTPathEnsureTrailingSeparator(g_szDir, sizeof(g_szDir));
    g_cchDir = strlen(g_szDir);

    /*
     * Create the test directory with an 'empty' subdirectory under it,
     * execute the tests, and remove directory when done.
     */
    RTTestBanner(g_hTest);
    if (!RTPathExists(g_szDir))
    {
        /* The base dir: */
        rc = RTDirCreate(g_szDir, 0755,
                         RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_DONT_SET | RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL);
        if (RT_SUCCESS(rc))
        {
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Test  dir: %s\n", g_szDir);

            if (g_cJobs == 1)
                rc = ioPerfDoTestSingle();
            else
                rc = ioPerfDoTestMulti();

            g_szDir[g_cchDir] = '\0';
            rc = RTDirRemoveRecursive(g_szDir, RTDIRRMREC_F_CONTENT_AND_DIR | (g_fRelativeDir ? RTDIRRMREC_F_NO_ABS_PATH : 0));
            if (RT_FAILURE(rc))
                RTTestFailed(g_hTest, "RTDirRemoveRecursive(%s,) -> %Rrc\n", g_szDir, rc);
        }
        else
            RTTestFailed(g_hTest, "RTDirCreate(%s) -> %Rrc\n", g_szDir, rc);
    }
    else
        RTTestFailed(g_hTest, "Test directory already exists: %s\n", g_szDir);

    return RTTestSummaryAndDestroy(g_hTest);
}

