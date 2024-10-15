/* $Id: tstPDMQueue.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Queue Testcase.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_PDM_QUEUE
#define VBOX_IN_VMM

#include <VBox/vmm/pdmqueue.h>

#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vmm.h>

#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;


/*********************************************************************************************************************************
*   Test #2 - Threading                                                                                                          *
*********************************************************************************************************************************/
typedef struct TEST2ITEM
{
    PDMQUEUEITEMCORE    Core;
    uint32_t            iSeqNo;
    uint32_t            iThreadNo;
    /* Pad it up to two cachelines to reduce noise. */
    uint8_t             abPadding[128 - sizeof(PDMQUEUEITEMCORE) - sizeof(uint32_t) * 2];
} TEST2ITEM;
typedef TEST2ITEM *PTEST2ITEM;

static struct TEST2THREAD
{
    RTTHREAD            hThread;
    uint32_t            iThreadNo;
    uint32_t            cMaxPending;
    /* Pad one cache line. */
    uint8_t             abPadding1[64];
    uint32_t volatile   cPending;
    uint32_t volatile   iReceiveSeqNo;
    /* Pad structure size to three cache lines. */
    uint8_t             abPadding2[64 * 2 - sizeof(uint32_t) * 4 - sizeof(RTTHREAD)];
} g_aTest2Threads[16];

static bool volatile            g_fTest2Terminate = false;
static uint32_t volatile        g_cTest2Threads   = 0;
static uint32_t                 g_cTest2Received  = 0;
static bool volatile            g_fTest2PushBack  = false;
static PVM volatile             g_pTest2VM        = NULL;               /**< Volatile to force local copy in thread function. */
static PDMQUEUEHANDLE volatile  g_hTest2Queue     = NIL_PDMQUEUEHANDLE; /**< Ditto. */



/**
 * @callback_method_impl{FNPDMQUEUEEXT, Consumer callback}
 */
static DECLCALLBACK(bool) Test2ConsumerCallback(void *pvUser, PPDMQUEUEITEMCORE pItem)
{
    PTEST2ITEM   pMyItem = (PTEST2ITEM)pItem;
    size_t const iThread = pMyItem->iThreadNo;
    RTTEST_CHECK_RET(g_hTest, iThread < RT_ELEMENTS(g_aTest2Threads), true);

    /*
     * Start pushing back after the first million or when the
     * control thread decide it's time for it:
     */
    uint32_t cReceived = ++g_cTest2Received;
    if (g_fTest2PushBack)
    {
        if ((cReceived & 3) == 3 && cReceived > _1M)
            return false;
    }
    else if (cReceived < _1M )
    { /* likely */ }
    else
        g_fTest2PushBack = true;

    /*
     * Process the item:
     */
    uint32_t iCallbackNo = ASMAtomicIncU32(&g_aTest2Threads[iThread].iReceiveSeqNo);
    if (pMyItem->iSeqNo != iCallbackNo)
        RTTestFailed(g_hTest, "iThread=%#x: iSeqNo=%#x, expected %#x\n", iThread, pMyItem->iSeqNo, iCallbackNo);

    ASMAtomicDecU32(&g_aTest2Threads[iThread].cPending);

    RT_NOREF(pvUser);
    return true;
}


/**
 * @callback_method_impl{FNRTTHREAD, Producer thread}
 */
static DECLCALLBACK(int) Test2Thread(RTTHREAD hThreadSelf, void *pvUser)
{
    PVM const               pVM     = g_pTest2VM;
    PDMQUEUEHANDLE const    hQueue  = g_hTest2Queue;
    size_t const            iThread = (size_t)pvUser;
    RTTEST_CHECK_RET(g_hTest, iThread < RT_ELEMENTS(g_aTest2Threads), VERR_INVALID_PARAMETER);

    uint32_t iSendSeqNo = 0;
    uint32_t cSpinLoops = 0;
    while (!g_fTest2Terminate && iSendSeqNo < _64M)
    {
        if (g_aTest2Threads[iThread].cPending < g_aTest2Threads[iThread].cMaxPending)
        {
            PTEST2ITEM pMyItem = (PTEST2ITEM)PDMQueueAlloc(pVM, hQueue, pVM);
            if (pMyItem)
            {
                pMyItem->iSeqNo    = ++iSendSeqNo;
                pMyItem->iThreadNo = (uint32_t)iThread;
                RTTEST_CHECK_RC(g_hTest, PDMQueueInsert(pVM, hQueue, pVM, &pMyItem->Core), VINF_SUCCESS);
                ASMAtomicIncU32(&g_aTest2Threads[iThread].cPending);
            }
            else
            {
                RTTestFailed(g_hTest, "iThread=%u: PDMQueueAlloc failed: cPending=%u cMaxPending=%u iSendSeqNo=%u",
                             iThread, g_aTest2Threads[iThread].cPending, g_aTest2Threads[iThread].cMaxPending, iSendSeqNo);
                ASMAtomicWriteBool(&g_fTest2Terminate, true);
                break;
            }

            cSpinLoops = 0;
        }
        else if (cSpinLoops++ < 1024)
            ASMNopPause();
        else
        {
            RTThreadYield();
            cSpinLoops = 0;
        }
    }

    ASMAtomicDecU32(&g_cTest2Threads);

    RT_NOREF(hThreadSelf);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTTHREAD, Control thread}
 */
static DECLCALLBACK(int) Test2ControlThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf, pvUser);

    RTThreadSleep(RT_MS_5SEC);
    ASMAtomicWriteBool(&g_fTest2PushBack, true);

    RTThreadSleep(RT_MS_30SEC);
    ASMAtomicWriteBool(&g_fTest2Terminate, true);

    ASMAtomicDecU32(&g_cTest2Threads);
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) Test2Emt(PVM pVM, PUVM pUVM)
{
    uint32_t cThreads = 2;
    RTTestSubF(g_hTest, "%u Threads", cThreads);
    RTTEST_CHECK_RET(g_hTest, cThreads < RT_ELEMENTS(g_aTest2Threads) /* last entry is control thread*/, VERR_OUT_OF_RANGE);

    PDMQUEUEHANDLE hQueue;
    RTTEST_CHECK_RC_RET(g_hTest, PDMR3QueueCreateExternal(pVM, sizeof(TEST2ITEM), cThreads * 128 + 16, 0 /*cMilliesInterval*/,
                                                          Test2ConsumerCallback, pVM /*pvUser*/, "Test2", &hQueue),
                        VINF_SUCCESS, VINF_SUCCESS);

    /* Init the thread data: */
    g_fTest2Terminate = false;
    g_pTest2VM        = pVM;
    g_hTest2Queue     = hQueue;
    g_fTest2PushBack  = false;
    g_cTest2Received  = 0;
    for (uint32_t i = 0; i < cThreads; i++)
    {
        g_aTest2Threads[i].hThread       = NIL_RTTHREAD;
        g_aTest2Threads[i].iThreadNo     = i;
        g_aTest2Threads[i].cMaxPending   = 64 + i % 16;
        g_aTest2Threads[i].cPending      = 0;
        g_aTest2Threads[i].iReceiveSeqNo = 0;
    }

    /* Start the threads: */
    for (uint32_t i = 0; i < cThreads; i++)
    {
        RTTEST_CHECK_RC_BREAK(g_hTest, RTThreadCreateF(&g_aTest2Threads[i].hThread, Test2Thread, (void *)(uintptr_t)i, 0,
                                                       RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "test2-t%u", i),
                              VINF_SUCCESS);
        ASMAtomicIncU32(&g_cTest2Threads);
    }

    int rc;
    RTTEST_CHECK_RC(g_hTest, rc = RTThreadCreate(&g_aTest2Threads[cThreads].hThread, Test2ControlThread, NULL, 0,
                                                 RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "test2-ctl"),
                    VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        ASMAtomicIncU32(&g_cTest2Threads);

    /* Process the queue till all threads have quit or termination is triggered: */
    while (   ASMAtomicUoReadU32(&g_cTest2Threads) != 0
           && !g_fTest2Terminate)
    {
        PDMR3QueueFlushAll(pVM);
    }

    /* Wait for the threads. */
    ASMAtomicWriteBool(&g_fTest2Terminate, true);
    for (uint32_t i = 0; i <= cThreads; i++)
    {
        if (g_aTest2Threads[i].hThread != NIL_RTTHREAD)
        {
            int rcThread = VERR_GENERAL_FAILURE;
            RTTEST_CHECK_RC(g_hTest, RTThreadWait(g_aTest2Threads[i].hThread, RT_MS_30SEC, &rcThread), VINF_SUCCESS);
            RTTEST_CHECK_RC(g_hTest, rcThread, VINF_SUCCESS);
        }
    }

    STAMR3Print(pUVM, "/PDM/Queue/Test2/*");

    /* Cleanup: */
    RTTEST_CHECK_RC(g_hTest, PDMR3QueueDestroy(pVM, hQueue, pVM), VINF_SUCCESS);
    RTTestSubDone(g_hTest);
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Test #1 - Basics                                                                                                             *
*********************************************************************************************************************************/
static uint32_t volatile g_cTest1Callbacks = 0;
static int32_t volatile  g_cTest1Pushback  = INT32_MAX;

typedef struct TEST1ITEM
{
    PDMQUEUEITEMCORE    Core;
    uint32_t            iSeqNo;
} TEST1ITEM;
typedef TEST1ITEM *PTEST1ITEM;


/** @callback_method_impl{FNPDMQUEUEEXT} */
static DECLCALLBACK(bool) Test1ConsumerCallback(void *pvUser, PPDMQUEUEITEMCORE pItem)
{
    if (ASMAtomicDecS32(&g_cTest1Pushback) < 0)
        return false;

    PTEST1ITEM pMyItem = (PTEST1ITEM)pItem;
    uint32_t iCallbackNo = ASMAtomicIncU32(&g_cTest1Callbacks);
    if (pMyItem->iSeqNo != iCallbackNo)
        RTTestFailed(g_hTest, "iSeqNo=%#x, expected %#x\n", pMyItem->iSeqNo, iCallbackNo);

    RT_NOREF(pvUser);
    return true;
}


static DECLCALLBACK(int) Test1Emt(PVM pVM)
{
    RTTestSub(g_hTest, "Basics");

    PDMQUEUEHANDLE hQueue;
    RTTEST_CHECK_RC_RET(g_hTest, PDMR3QueueCreateExternal(pVM, sizeof(TEST1ITEM), 16, 0 /*cMilliesInterval*/,
                                                          Test1ConsumerCallback, pVM /*pvUser*/, "Test1", &hQueue),
                        VINF_SUCCESS, VINF_SUCCESS);

    PDMQUEUEHANDLE const hQueueFirst = hQueue; /* Save the handle value so we can check that it's correctly reused. */

    /*
     * Single item:
     */
    PTEST1ITEM pMyItem = (PTEST1ITEM)PDMQueueAlloc(pVM, hQueue, pVM);
    RTTEST_CHECK(g_hTest, pMyItem);
    if (pMyItem)
    {
        pMyItem->iSeqNo = 1;
        RTTEST_CHECK_RC(g_hTest, PDMQueueInsert(pVM, hQueue, pVM, &pMyItem->Core), VINF_SUCCESS);

        PDMR3QueueFlushAll(pVM);
        RTTEST_CHECK(g_hTest, g_cTest1Callbacks == 1);
    }

    /*
     * All items:
     */
    for (uint32_t i = 0; i < 16; i++)
    {
        pMyItem = (PTEST1ITEM)PDMQueueAlloc(pVM, hQueue, pVM);
        RTTEST_CHECK_BREAK(g_hTest, pMyItem);
        pMyItem->iSeqNo = i + 2;
        RTTEST_CHECK_RC(g_hTest, PDMQueueInsert(pVM, hQueue, pVM, &pMyItem->Core), VINF_SUCCESS);
    }

    pMyItem = (PTEST1ITEM)PDMQueueAlloc(pVM, hQueue, pVM);
    RTTEST_CHECK(g_hTest, pMyItem == NULL);

    PDMR3QueueFlushAll(pVM);
    RTTEST_CHECK(g_hTest, g_cTest1Callbacks == 17);

    /*
     * Push back.
     *  1. First queue all items.
     *  2. Process half of them.
     *  3. The process one by one.
     */
    g_cTest1Callbacks = 0;
    g_cTest1Pushback  = 8;

    for (uint32_t i = 0; i < 16; i++)
    {
        pMyItem = (PTEST1ITEM)PDMQueueAlloc(pVM, hQueue, pVM);
        RTTEST_CHECK_BREAK(g_hTest, pMyItem);
        pMyItem->iSeqNo = i + 1;
        RTTEST_CHECK_RC(g_hTest, PDMQueueInsert(pVM, hQueue, pVM, &pMyItem->Core), VINF_SUCCESS);
    }

    pMyItem = (PTEST1ITEM)PDMQueueAlloc(pVM, hQueue, pVM);
    RTTEST_CHECK(g_hTest, pMyItem == NULL);

    PDMR3QueueFlushAll(pVM);
    RTTEST_CHECK(g_hTest, g_cTest1Callbacks == 8);

    for (uint32_t i = 0; i < 8; i++)
    {
        g_cTest1Pushback = 1;
        PDMR3QueueFlushAll(pVM);
        RTTEST_CHECK(g_hTest, g_cTest1Callbacks == 8 + 1 + i);
    }

    /*
     * Cleanup.
     */
    RTTEST_CHECK_RC(g_hTest, PDMR3QueueDestroy(pVM, hQueue, pVM), VINF_SUCCESS);

    /*
     * Do some creation/deletion ordering checks.
     */
    RTTestSub(g_hTest, "Cleanup & handle reuse");
    PDMQUEUEHANDLE ahQueues[168];
    for (size_t i = 0; i < RT_ELEMENTS(ahQueues); i++)
        ahQueues[i] = NIL_PDMQUEUEHANDLE;
    for (uint32_t i = 0; i < RT_ELEMENTS(ahQueues); i++)
    {
        char szQueueNm[32];
        RTStrPrintf(szQueueNm, sizeof(szQueueNm), "Test1b-%u", i);
        RTTEST_CHECK_RC(g_hTest, PDMR3QueueCreateExternal(pVM, sizeof(TEST1ITEM), i + 1, 0 /*cMilliesInterval*/,
                                                          Test1ConsumerCallback, pVM /*pvUser*/, szQueueNm, &ahQueues[i]),
                        VINF_SUCCESS);
        if (i == 0 && ahQueues[0] != hQueueFirst)
            RTTestFailed(g_hTest, "Queue handle value not reused: %#RX64, expected %#RX64", ahQueues[0], hQueueFirst);
    }

    /* Delete them in random order. */
    for (uint32_t i = 0; i < RT_ELEMENTS(ahQueues); i++)
    {
        uint32_t iDelete = RTRandU32Ex(0, RT_ELEMENTS(ahQueues) - 1);
        if (ahQueues[iDelete] != NIL_PDMQUEUEHANDLE)
        {
            RTTEST_CHECK_RC(g_hTest, PDMR3QueueDestroy(pVM, ahQueues[iDelete], pVM), VINF_SUCCESS);
            ahQueues[iDelete] = NIL_PDMQUEUEHANDLE;
        }
    }

    /* Delete remainder in ascending order, creating a array shrinking at the end. */
    for (uint32_t i = 0; i < RT_ELEMENTS(ahQueues); i++)
        if (ahQueues[i] != NIL_PDMQUEUEHANDLE)
        {
            RTTEST_CHECK_RC(g_hTest, PDMR3QueueDestroy(pVM, ahQueues[i], pVM), VINF_SUCCESS);
            ahQueues[i] = NIL_PDMQUEUEHANDLE;
        }

    /* Create one more queue and check that we get the first queue handle again. */
    RTTEST_CHECK_RC(g_hTest, PDMR3QueueCreateExternal(pVM, sizeof(TEST1ITEM), 1, 0 /*cMilliesInterval*/,
                                                      Test1ConsumerCallback, pVM /*pvUser*/, "Test1c", &hQueue), VINF_SUCCESS);
    if (hQueue != hQueueFirst)
        RTTestFailed(g_hTest, "Queue handle value not reused: %#RX64, expected %#RX64", hQueue, hQueueFirst);
    RTTEST_CHECK_RC(g_hTest, PDMR3QueueDestroy(pVM, hQueue, pVM), VINF_SUCCESS);

    RTTestSubDone(g_hTest);
    return VINF_SUCCESS;
}


static void DoTests(void)
{
    PVM  pVM;
    PUVM pUVM;
    RTTESTI_CHECK_RC_OK_RETV(VMR3Create(1 /*cCpus*/, NULL, VMCREATE_F_DRIVERLESS, NULL, NULL, NULL, NULL, &pVM, &pUVM));

    /*
     * Do the tests.
     */
    RTTESTI_CHECK_RC(VMR3ReqCallWaitU(pUVM, 0, (PFNRT)Test1Emt, 1, pVM), VINF_SUCCESS);
    if (RTTestErrorCount(g_hTest) == 0)
    {
        RTTESTI_CHECK_RC(VMR3ReqCallWaitU(pUVM, 0, (PFNRT)Test2Emt, 2, pVM, pUVM), VINF_SUCCESS);
    }

    /*
     * Clean up.
     */
    RTTESTI_CHECK_RC_OK_RETV(VMR3PowerOff(pUVM));
    RTTESTI_CHECK_RC_OK_RETV(VMR3Destroy(pUVM));
    VMR3ReleaseUVM(pUVM);
}


int main(int argc, char **argv)
{
    /*
     * We run the VMM in driverless mode to avoid needing to hardened the testcase
     */
    RTEXITCODE rcExit;
    int rc = RTR3InitExe(argc, &argv, SUPR3INIT_F_DRIVERLESS << RTR3INIT_FLAGS_SUPLIB_SHIFT);
    if (RT_SUCCESS(rc))
    {
        rc = RTTestCreate("tstPDMQueue", &g_hTest);
        if (RT_SUCCESS(rc))
        {
            RTTestBanner(g_hTest);
            DoTests();
            rcExit = RTTestSummaryAndDestroy(g_hTest);
        }
        else
            rcExit = RTMsgErrorExitFailure("RTTestCreate failed: %Rrc", rc);
    }
    else
        rcExit = RTMsgInitFailure(rc);
    return rcExit;
}

