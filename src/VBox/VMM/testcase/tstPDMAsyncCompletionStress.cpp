/* $Id: tstPDMAsyncCompletionStress.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Asynchronous Completion Stresstest.
 *
 * This testcase is for stress testing the async completion interface.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_PDM_ASYNC_COMPLETION

#include "VMInternal.h" /* UVM */
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/pdmasynccompletion.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmthread.h>
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/semaphore.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/thread.h>
#include <iprt/param.h>
#include <iprt/message.h>

#define TESTCASE "tstPDMAsyncCompletionStress"

#if 0
/** Number of simultaneous open endpoints for reading and writing. */
#define NR_OPEN_ENDPOINTS 10
/** Test pattern size. */
#define TEST_PATTERN_SIZE (100*_1M)
/** Minimum file size. */
#define FILE_SIZE_MIN (100 * _1M)
/** Maximum file size. */
#define FILE_SIZE_MAX (10000UL * _1M)
/** Minimum segment size. */
#define SEGMENT_SIZE_MIN (512)
/** Maximum segment size. */
#define SEGMENT_SIZE_MAX (TEST_PATTERN_SIZE)
/** Maximum number of active tasks. */
#define TASK_ACTIVE_MAX (1024)
/** Maximum size of a transfer. */
#define TASK_TRANSFER_SIZE_MAX (10*_1M)
#else
/** Number of simultaneous open endpoints for reading and writing. */
#define NR_OPEN_ENDPOINTS 5
/** Test pattern size. */
#define TEST_PATTERN_SIZE (10*_1M)
/** Minimum file size. */
#define FILE_SIZE_MIN (100 * _1M)
/** Maximum file size. */
#define FILE_SIZE_MAX (1000UL * _1M)
/** Minimum segment size. */
#define SEGMENT_SIZE_MIN (512)
/** Maximum segment size. */
#define SEGMENT_SIZE_MAX (TEST_PATTERN_SIZE)
/** Maximum number of active tasks. */
#define TASK_ACTIVE_MAX (1)
/** Maximum size of a transfer. */
#define TASK_TRANSFER_SIZE_MAX (_1M)
#endif

/**
 * Structure defining a file segment.
 */
typedef struct PDMACTESTFILESEG
{
    /** Start offset in the file. */
    RTFOFF                     off;
    /** Size of the segment. */
    size_t                     cbSegment;
    /** Pointer to the start of the data in the test pattern used for the segment. */
    uint8_t                   *pbData;
} PDMACTESTFILESEG, *PPDMACTESTFILESEG;

/**
 * Structure defining a I/O task.
 */
typedef struct PDMACTESTFILETASK
{
    /** Flag whether the task is currently active. */
    bool                        fActive;
    /** Flag whether this is a write. */
    bool                        fWrite;
    /** Start offset. */
    RTFOFF                      off;
    /** Data segment */
    RTSGSEG                     DataSeg;
    /** Task handle. */
    PPDMASYNCCOMPLETIONTASK     hTask;
} PDMACTESTFILETASK, *PPDMACTESTFILETASK;

/**
 * Structure defining a test file.
 */
typedef struct PDMACTESTFILE
{
    /** The PDM async completion endpoint handle. */
    PPDMASYNCCOMPLETIONENDPOINT hEndpoint;
    /** Template used for this file. */
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate;
    /** Maximum size of the file. */
    uint64_t                   cbFileMax;
    /** Current size of the file. */
    uint64_t                   cbFileCurr;
    /** Size of a file segment. */
    size_t                     cbFileSegment;
    /** Maximum number of segments. */
    size_t                     cSegments;
    /** Pointer to the array describing how the file is assembled
     * of the test pattern. Used for comparing read data to ensure
     * that no corruption occurred.
     */
    PPDMACTESTFILESEG          paSegs;
    /** Maximum number of active tasks for this endpoint. */
    uint32_t                   cTasksActiveMax;
    /** Number of current active tasks. */
    volatile uint32_t          cTasksActiveCurr;
    /** Pointer to the array of task. */
    PPDMACTESTFILETASK         paTasks;
    /** I/O thread handle. */
    PPDMTHREAD                 hThread;
    /** Flag whether the thread should terminate. */
    bool                       fRunning;
} PDMACTESTFILE, *PPDMACTESTFILE;

/** Buffer storing the random test pattern. */
uint8_t *g_pbTestPattern = NULL;
/** Size of the test pattern. */
size_t   g_cbTestPattern;
/** Array holding test files. */
PDMACTESTFILE g_aTestFiles[NR_OPEN_ENDPOINTS];

static DECLCALLBACK(void) tstPDMACStressTestFileTaskCompleted(PVM pVM, void *pvUser, void *pvUser2, int rcReq);

static void tstPDMACStressTestFileVerify(PPDMACTESTFILE pTestFile, PPDMACTESTFILETASK pTestTask)
{
    size_t   cbLeft = pTestTask->DataSeg.cbSeg;
    RTFOFF   off    = pTestTask->off;
    uint8_t *pbBuf  = (uint8_t *)pTestTask->DataSeg.pvSeg;

    while (cbLeft)
    {
        size_t cbCompare;
        size_t iSeg = off / pTestFile->cbFileSegment;
        PPDMACTESTFILESEG pSeg = &pTestFile->paSegs[iSeg];
        uint8_t *pbTestPattern;
        unsigned offSeg = off - pSeg->off;

        cbCompare = RT_MIN(cbLeft, pSeg->cbSegment - offSeg);
        pbTestPattern = pSeg->pbData + offSeg;

        if (memcmp(pbBuf, pbTestPattern, cbCompare))
        {
            unsigned idx = 0;

            while (   (idx < cbCompare)
                   && (pbBuf[idx] == pbTestPattern[idx]))
                idx++;

            RTMsgError("Unexpected data for off=%RTfoff size=%u\n"
                       "Expected %c got %c\n",
                       pTestTask->off + idx, pTestTask->DataSeg.cbSeg,
                       pbTestPattern[idx], pbBuf[idx]);
            RTAssertDebugBreak();
        }

        pbBuf  += cbCompare;
        off    += cbCompare;
        cbLeft -= cbCompare;
    }
}

static void tstPDMACStressTestFileFillBuffer(PPDMACTESTFILE pTestFile, PPDMACTESTFILETASK pTestTask)
{
    uint8_t *pbBuf = (uint8_t *)pTestTask->DataSeg.pvSeg;
    size_t  cbLeft = pTestTask->DataSeg.cbSeg;
    RTFOFF  off    = pTestTask->off;

    Assert(pTestTask->fWrite && pTestTask->fActive);

    while (cbLeft)
    {
        size_t cbFill;
        size_t iSeg = off / pTestFile->cbFileSegment;
        PPDMACTESTFILESEG pSeg = &pTestFile->paSegs[iSeg];
        uint8_t *pbTestPattern;
        unsigned offSeg = off - pSeg->off;

        cbFill = RT_MIN(cbLeft, pSeg->cbSegment - offSeg);
        pbTestPattern = pSeg->pbData + offSeg;

        memcpy(pbBuf, pbTestPattern, cbFill);

        pbBuf  += cbFill;
        off    += cbFill;
        cbLeft -= cbFill;
    }
}

static int tstPDMACStressTestFileWrite(PPDMACTESTFILE pTestFile, PPDMACTESTFILETASK pTestTask)
{
    int rc = VINF_SUCCESS;

    Assert(!pTestTask->fActive);

    pTestTask->fActive       = true;
    pTestTask->fWrite        = true;
    pTestTask->DataSeg.cbSeg = RTRandU32Ex(512, TASK_TRANSFER_SIZE_MAX) & ~511;

    uint64_t offMax;

    /* Did we reached the maximum file size */
    if (pTestFile->cbFileCurr < pTestFile->cbFileMax)
    {
        offMax =   (pTestFile->cbFileMax - pTestFile->cbFileCurr) < pTestTask->DataSeg.cbSeg
                 ? pTestFile->cbFileMax - pTestTask->DataSeg.cbSeg
                 : pTestFile->cbFileCurr;
    }
    else
        offMax = pTestFile->cbFileMax - pTestTask->DataSeg.cbSeg;

    uint64_t offMin;

    /*
     * If we reached the maximum file size write in the whole file
     * otherwise we will enforce the range for random offsets to let it grow
     * more quickly.
     */
    if (pTestFile->cbFileCurr == pTestFile->cbFileMax)
        offMin = 0;
    else
        offMin = RT_MIN(pTestFile->cbFileCurr, offMax);


    pTestTask->off = RTRandU64Ex(offMin, offMax) & ~511;

    /* Set new file size of required */
    if ((uint64_t)pTestTask->off + pTestTask->DataSeg.cbSeg > pTestFile->cbFileCurr)
        pTestFile->cbFileCurr = pTestTask->off + pTestTask->DataSeg.cbSeg;

    AssertMsg(pTestFile->cbFileCurr <= pTestFile->cbFileMax,
              ("Current file size (%llu) exceeds final size (%llu)\n",
              pTestFile->cbFileCurr, pTestFile->cbFileMax));

    /* Allocate data buffer. */
    pTestTask->DataSeg.pvSeg = RTMemAlloc(pTestTask->DataSeg.cbSeg);
    if (!pTestTask->DataSeg.pvSeg)
        return VERR_NO_MEMORY;

    /* Fill data into buffer. */
    tstPDMACStressTestFileFillBuffer(pTestFile, pTestTask);

    /* Engage */
    rc = PDMR3AsyncCompletionEpWrite(pTestFile->hEndpoint, pTestTask->off,
                                     &pTestTask->DataSeg, 1,
                                     pTestTask->DataSeg.cbSeg,
                                     pTestTask,
                                     &pTestTask->hTask);

    return rc;
}

static int tstPDMACStressTestFileRead(PPDMACTESTFILE pTestFile, PPDMACTESTFILETASK pTestTask)
{
    int rc = VINF_SUCCESS;

    Assert(!pTestTask->fActive);

    pTestTask->fActive       = true;
    pTestTask->fWrite        = false;
    pTestTask->DataSeg.cbSeg = RTRandU32Ex(1, RT_MIN(pTestFile->cbFileCurr, TASK_TRANSFER_SIZE_MAX));

    AssertMsg(pTestFile->cbFileCurr >= pTestTask->DataSeg.cbSeg, ("Impossible\n"));
    pTestTask->off = RTRandU64Ex(0, pTestFile->cbFileCurr - pTestTask->DataSeg.cbSeg);

    /* Allocate data buffer. */
    pTestTask->DataSeg.pvSeg = RTMemAlloc(pTestTask->DataSeg.cbSeg);
    if (!pTestTask->DataSeg.pvSeg)
        return VERR_NO_MEMORY;

    /* Engage */
    rc = PDMR3AsyncCompletionEpRead(pTestFile->hEndpoint, pTestTask->off,
                                     &pTestTask->DataSeg, 1,
                                     pTestTask->DataSeg.cbSeg,
                                     pTestTask,
                                     &pTestTask->hTask);

    return rc;
}

/**
 * Returns true with the given chance in percent.
 *
 * @returns true or false
 * @param   iPercentage   The percentage of the chance to return true.
 */
static bool tstPDMACTestIsTrue(int iPercentage)
{
    int uRnd = RTRandU32Ex(0, 100);

    return (uRnd <= iPercentage); /* This should be enough for our purpose */
}

static DECLCALLBACK(int) tstPDMACTestFileThread(PVM pVM, PPDMTHREAD pThread)
{
    PPDMACTESTFILE pTestFile = (PPDMACTESTFILE)pThread->pvUser;
    int iWriteChance = 100; /* Chance to get a write task in percent. */
    uint32_t cTasksStarted = 0;
    int rc = VINF_SUCCESS;

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pTestFile->fRunning)
    {
        unsigned iTaskCurr = 0;


        /* Fill all tasks */
        while (   (pTestFile->cTasksActiveCurr < pTestFile->cTasksActiveMax)
               && (iTaskCurr < pTestFile->cTasksActiveMax))
        {
            PPDMACTESTFILETASK pTask = &pTestFile->paTasks[iTaskCurr];

            if (!pTask->fActive)
            {
                /* Read or write task? */
                bool fWrite = tstPDMACTestIsTrue(iWriteChance);

                ASMAtomicIncU32(&pTestFile->cTasksActiveCurr);

                if (fWrite)
                    rc = tstPDMACStressTestFileWrite(pTestFile, pTask);
                else
                    rc = tstPDMACStressTestFileRead(pTestFile, pTask);

                if (rc != VINF_AIO_TASK_PENDING)
                    tstPDMACStressTestFileTaskCompleted(pVM, pTask, pTestFile, rc);

                cTasksStarted++;
            }

            iTaskCurr++;
        }

        /*
         * Recalc write chance. The bigger the file the lower the chance to have a write.
         * The minimum chance is 33 percent.
         */
        iWriteChance = 100 - (int)((100.0 / (double)pTestFile->cbFileMax) * (double)pTestFile->cbFileCurr);
        iWriteChance = RT_MAX(33, iWriteChance);

        /* Wait a random amount of time. (1ms - 100ms) */
        RTThreadSleep(RTRandU32Ex(1, 100));
    }

    /* Wait for the rest to complete. */
    while (pTestFile->cTasksActiveCurr)
        RTThreadSleep(250);

    RTPrintf("Thread exiting: processed %u tasks\n", cTasksStarted);
    return rc;
}

static DECLCALLBACK(void) tstPDMACStressTestFileTaskCompleted(PVM pVM, void *pvUser, void *pvUser2, int rcReq)
{
    PPDMACTESTFILE pTestFile = (PPDMACTESTFILE)pvUser2;
    PPDMACTESTFILETASK pTestTask = (PPDMACTESTFILETASK)pvUser;
    NOREF(pVM); NOREF(rcReq);

    if (pTestTask->fWrite)
    {
        /** @todo Do something sensible here. */
    }
    else
    {
        tstPDMACStressTestFileVerify(pTestFile, pTestTask); /* Will assert if it fails */
    }

    RTMemFree(pTestTask->DataSeg.pvSeg);
    pTestTask->fActive = false;
    AssertMsg(pTestFile->cTasksActiveCurr > 0, ("Trying to complete a non active task\n"));
    ASMAtomicDecU32(&pTestFile->cTasksActiveCurr);
}

/**
 * Sets up a test file creating the I/O thread.
 *
 * @returns VBox status code.
 * @param   pVM          Pointer to the shared VM instance structure.
 * @param   pTestFile    Pointer to the uninitialized test file structure.
 * @param   iTestId      Unique test id.
 */
static int tstPDMACStressTestFileOpen(PVM pVM, PPDMACTESTFILE pTestFile, unsigned iTestId)
{
    int rc = VERR_NO_MEMORY;

    /* Size is a multiple of 512 */
    pTestFile->cbFileMax     = RTRandU64Ex(FILE_SIZE_MIN, FILE_SIZE_MAX) & ~(511UL);
    pTestFile->cbFileCurr    = 0;
    pTestFile->cbFileSegment = RTRandU32Ex(SEGMENT_SIZE_MIN, RT_MIN(pTestFile->cbFileMax, SEGMENT_SIZE_MAX)) & ~((size_t)511);

    Assert(pTestFile->cbFileMax >= pTestFile->cbFileSegment);

    /* Set up the segments array. */
    pTestFile->cSegments  = pTestFile->cbFileMax / pTestFile->cbFileSegment;
    pTestFile->cSegments += ((pTestFile->cbFileMax % pTestFile->cbFileSegment) > 0) ? 1 : 0;

    pTestFile->paSegs = (PPDMACTESTFILESEG)RTMemAllocZ(pTestFile->cSegments * sizeof(PDMACTESTFILESEG));
    if (pTestFile->paSegs)
    {
        /* Init the segments */
        for (unsigned i = 0; i < pTestFile->cSegments; i++)
        {
            PPDMACTESTFILESEG pSeg = &pTestFile->paSegs[i];

            pSeg->off       = (RTFOFF)i * pTestFile->cbFileSegment;
            pSeg->cbSegment = pTestFile->cbFileSegment;

            /* Let the buffer point to a random position in the test pattern. */
            uint32_t offTestPattern = RTRandU64Ex(0, g_cbTestPattern - pSeg->cbSegment);

            pSeg->pbData = g_pbTestPattern + offTestPattern;
        }

        /* Init task array. */
        pTestFile->cTasksActiveMax = RTRandU32Ex(1, TASK_ACTIVE_MAX);
        pTestFile->paTasks         = (PPDMACTESTFILETASK)RTMemAllocZ(pTestFile->cTasksActiveMax * sizeof(PDMACTESTFILETASK));
        if (pTestFile->paTasks)
        {
            /* Create the template */
            char szDesc[256];

            RTStrPrintf(szDesc, sizeof(szDesc), "Template-%d", iTestId);
            rc = PDMR3AsyncCompletionTemplateCreateInternal(pVM, &pTestFile->pTemplate, tstPDMACStressTestFileTaskCompleted,
                                                            pTestFile, szDesc);
            if (RT_SUCCESS(rc))
            {
                /* Open the endpoint now. Because async completion endpoints cannot create files we have to do it before. */
                char szFile[RTPATH_MAX];

                RTStrPrintf(szFile, sizeof(szFile), "tstPDMAsyncCompletionStress-%d.tmp", iTestId);

                RTFILE FileTmp;
                rc = RTFileOpen(&FileTmp, szFile, RTFILE_O_READWRITE | RTFILE_O_CREATE | RTFILE_O_DENY_NONE);
                if (RT_SUCCESS(rc))
                {
                    RTFileClose(FileTmp);

                    rc = PDMR3AsyncCompletionEpCreateForFile(&pTestFile->hEndpoint, szFile, 0, pTestFile->pTemplate);
                    if (RT_SUCCESS(rc))
                    {
                        char szThreadDesc[256];

                        pTestFile->fRunning = true;

                        /* Create the thread creating the I/O for the given file. */
                        RTStrPrintf(szThreadDesc, sizeof(szThreadDesc), "PDMACThread-%d", iTestId);
                        rc = PDMR3ThreadCreate(pVM, &pTestFile->hThread, pTestFile, tstPDMACTestFileThread,
                                               NULL, 0, RTTHREADTYPE_IO, szThreadDesc);
                        if (RT_SUCCESS(rc))
                        {
                            rc = PDMR3ThreadResume(pTestFile->hThread);
                            AssertRC(rc);

                            RTPrintf(TESTCASE ": Created test file %s cbFileMax=%llu cbFileSegment=%u cSegments=%u cTasksActiveMax=%u\n",
                                     szFile, pTestFile->cbFileMax, pTestFile->cbFileSegment, pTestFile->cSegments, pTestFile->cTasksActiveMax);
                            return VINF_SUCCESS;
                        }

                        PDMR3AsyncCompletionEpClose(pTestFile->hEndpoint);
                    }

                    RTFileDelete(szFile);
                }

                PDMR3AsyncCompletionTemplateDestroy(pTestFile->pTemplate);
            }

            RTMemFree(pTestFile->paTasks);
        }
        else
            rc = VERR_NO_MEMORY;

        RTMemFree(pTestFile->paSegs);
    }
    else
        rc = VERR_NO_MEMORY;

    RTPrintf(TESTCASE ": Opening test file with id %d failed rc=%Rrc\n", iTestId, rc);

    return rc;
}

/**
 * Closes a test file.
 *
 * @param pTestFile    Pointer to the test file.
 */
static void tstPDMACStressTestFileClose(PPDMACTESTFILE pTestFile)
{
    int rcThread;
    int rc;

    RTPrintf("Terminating I/O thread, please wait...\n");

    /* Let the thread know that it should terminate. */
    pTestFile->fRunning = false;

    /* Wait for the thread to terminate. */
    rc = PDMR3ThreadDestroy(pTestFile->hThread, &rcThread);

    RTPrintf("Thread terminated with status code rc=%Rrc\n", rcThread);

    /* Free resources */
    RTMemFree(pTestFile->paTasks);
    RTMemFree(pTestFile->paSegs);
    PDMR3AsyncCompletionEpClose(pTestFile->hEndpoint);
    PDMR3AsyncCompletionTemplateDestroy(pTestFile->pTemplate);
}

/**
 * Inits the test pattern.
 *
 * @returns VBox status code.
 */
static int tstPDMACStressTestPatternInit(void)
{
    RTPrintf(TESTCASE ": Creating test pattern. Please wait...\n");
    g_cbTestPattern = TEST_PATTERN_SIZE;
    g_pbTestPattern = (uint8_t *)RTMemAlloc(g_cbTestPattern);
    if (!g_pbTestPattern)
        return VERR_NO_MEMORY;

    RTRandBytes(g_pbTestPattern, g_cbTestPattern);
    return VINF_SUCCESS;
}

static void tstPDMACStressTestPatternDestroy(void)
{
    RTPrintf(TESTCASE ": Destroying test pattern\n");
    RTMemFree(g_pbTestPattern);
}

/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF1(envp);
    int rcRet = 0; /* error count */

    RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);

    PVM pVM;
    PUVM pUVM;
    int rc = VMR3Create(1 /*cCpus*/, NULL, 0 /*fFlags*/, NULL, NULL, NULL, NULL, &pVM, &pUVM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Little hack to avoid the VM_ASSERT_EMT assertion.
         */
        RTTlsSet(pVM->pUVM->vm.s.idxTLS, &pVM->pUVM->aCpus[0]);
        pVM->pUVM->aCpus[0].pUVM = pVM->pUVM;
        pVM->pUVM->aCpus[0].vm.s.NativeThreadEMT = RTThreadNativeSelf();

        rc = tstPDMACStressTestPatternInit();
        if (RT_SUCCESS(rc))
        {
            unsigned cFilesOpened = 0;

            /* Open the endpoints. */
            for (cFilesOpened = 0; cFilesOpened < NR_OPEN_ENDPOINTS; cFilesOpened++)
            {
                rc = tstPDMACStressTestFileOpen(pVM, &g_aTestFiles[cFilesOpened], cFilesOpened);
                if (RT_FAILURE(rc))
                    break;
            }

            if (RT_SUCCESS(rc))
            {
                /* Tests are running now. */
                RTPrintf(TESTCASE ": Successfully opened all files. Running tests forever now or until an error is hit :)\n");
                RTThreadSleep(RT_INDEFINITE_WAIT);
            }

            /* Close opened endpoints. */
            for (unsigned i = 0; i < cFilesOpened; i++)
                tstPDMACStressTestFileClose(&g_aTestFiles[i]);

            tstPDMACStressTestPatternDestroy();
        }
        else
        {
            RTPrintf(TESTCASE ": failed to init test pattern!! rc=%Rrc\n", rc);
            rcRet++;
        }

        rc = VMR3Destroy(pUVM);
        AssertMsg(rc == VINF_SUCCESS, ("%s: Destroying VM failed rc=%Rrc!!\n", __FUNCTION__, rc));
    }
    else
    {
        RTPrintf(TESTCASE ": failed to create VM!! rc=%Rrc\n", rc);
        rcRet++;
    }

    return rcRet;
}


#if !defined(VBOX_WITH_HARDENING) || !defined(RT_OS_WINDOWS)
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    return TrustedMain(argc, argv, envp);
}
#endif

