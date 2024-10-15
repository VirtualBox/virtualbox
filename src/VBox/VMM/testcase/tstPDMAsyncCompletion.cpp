/* $Id: tstPDMAsyncCompletion.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Asynchronous Completion Testcase.
 *
 * This testcase is for testing the async completion interface.
 * It implements a file copy program which uses the interface to copy the data.
 *
 * Use: ./tstPDMAsyncCompletion <source> <destination>
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
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <VBox/vmm/pdmapi.h>
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#define TESTCASE "tstPDMAsyncCompletion"

/*
 * Number of simultaneous active tasks.
 */
#define NR_TASKS      80
#define BUFFER_SIZE (64*_1K)

/* Buffers to store data in .*/
uint8_t                *g_AsyncCompletionTasksBuffer[NR_TASKS];
PPDMASYNCCOMPLETIONTASK g_AsyncCompletionTasks[NR_TASKS];
volatile uint32_t       g_cTasksLeft;
RTSEMEVENT              g_FinishedEventSem;

static DECLCALLBACK(void) AsyncTaskCompleted(PVM pVM, void *pvUser, void *pvUser2, int rc)
{
    RT_NOREF4(pVM, pvUser, pvUser2, rc);
    LogFlow((TESTCASE ": %s: pVM=%p pvUser=%p pvUser2=%p\n", __FUNCTION__, pVM, pvUser, pvUser2));

    uint32_t cTasksStillLeft = ASMAtomicDecU32(&g_cTasksLeft);

    if (!cTasksStillLeft)
    {
        /* All tasks processed. Wakeup main. */
        RTSemEventSignal(g_FinishedEventSem);
    }
}

/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF1(envp);
    int rcRet = 0; /* error count */
    PPDMASYNCCOMPLETIONENDPOINT pEndpointSrc, pEndpointDst;

    RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);

    if (argc != 3)
    {
        RTPrintf(TESTCASE ": Usage is ./tstPDMAsyncCompletion <source> <dest>\n");
        return 1;
    }

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

        /*
         * Create the template.
         */
        PPDMASYNCCOMPLETIONTEMPLATE pTemplate;
        rc = PDMR3AsyncCompletionTemplateCreateInternal(pVM, &pTemplate, AsyncTaskCompleted, NULL, "Test");
        if (RT_FAILURE(rc))
        {
            RTPrintf(TESTCASE ": Error while creating the template!! rc=%d\n", rc);
            return 1;
        }

        /*
         * Create event semaphore.
         */
        rc = RTSemEventCreate(&g_FinishedEventSem);
        AssertRC(rc);

        /*
         * Create the temporary buffers.
         */
        for (unsigned i=0; i < NR_TASKS; i++)
        {
            g_AsyncCompletionTasksBuffer[i] = (uint8_t *)RTMemAllocZ(BUFFER_SIZE);
            if (!g_AsyncCompletionTasksBuffer[i])
            {
                RTPrintf(TESTCASE ": out of memory!\n");
                return ++rcRet;
            }
        }

        /* Create the destination as the async completion API can't do this. */
        RTFILE FileTmp;
        rc = RTFileOpen(&FileTmp, argv[2], RTFILE_O_READWRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_DENY_NONE);
        if (RT_FAILURE(rc))
        {
            RTPrintf(TESTCASE ": Error while creating the destination!! rc=%d\n", rc);
            return ++rcRet;
        }
        RTFileClose(FileTmp);

        /* Create our file endpoint */
        rc = PDMR3AsyncCompletionEpCreateForFile(&pEndpointSrc, argv[1], 0, pTemplate);
        if (RT_SUCCESS(rc))
        {
            rc = PDMR3AsyncCompletionEpCreateForFile(&pEndpointDst, argv[2], 0, pTemplate);
            if (RT_SUCCESS(rc))
            {
                PDMR3PowerOn(pVM);

                /* Wait for all threads to finish initialization. */
                RTThreadSleep(100);

                int fReadPass = true;
                uint64_t cbSrc;
                size_t   offSrc = 0;
                size_t   offDst = 0;
                uint32_t cTasksUsed = 0;

                rc = PDMR3AsyncCompletionEpGetSize(pEndpointSrc, &cbSrc);
                if (RT_SUCCESS(rc))
                {
                    /* Copy the data. */
                    for (;;)
                    {
                        if (fReadPass)
                        {
                            cTasksUsed = (BUFFER_SIZE * NR_TASKS) <= (cbSrc - offSrc)
                                         ? NR_TASKS
                                         :   ((cbSrc - offSrc) / BUFFER_SIZE)
                                           + ((cbSrc - offSrc) % BUFFER_SIZE) > 0
                                         ? 1
                                         : 0;

                            g_cTasksLeft = cTasksUsed;

                            for (uint32_t i = 0; i < cTasksUsed; i++)
                            {
                                size_t cbRead = ((size_t)offSrc + BUFFER_SIZE) <= cbSrc ? BUFFER_SIZE : cbSrc - offSrc;
                                RTSGSEG DataSeg;

                                DataSeg.pvSeg = g_AsyncCompletionTasksBuffer[i];
                                DataSeg.cbSeg = cbRead;

                                rc = PDMR3AsyncCompletionEpRead(pEndpointSrc, offSrc, &DataSeg, 1, cbRead, NULL,
                                                                &g_AsyncCompletionTasks[i]);
                                AssertRC(rc);
                                offSrc += cbRead;
                                if (offSrc == cbSrc)
                                    break;
                            }
                        }
                        else
                        {
                            g_cTasksLeft = cTasksUsed;

                            for (uint32_t i = 0; i < cTasksUsed; i++)
                            {
                                size_t cbWrite = (offDst + BUFFER_SIZE) <= cbSrc ? BUFFER_SIZE : cbSrc - offDst;
                                RTSGSEG DataSeg;

                                DataSeg.pvSeg = g_AsyncCompletionTasksBuffer[i];
                                DataSeg.cbSeg = cbWrite;

                                rc = PDMR3AsyncCompletionEpWrite(pEndpointDst, offDst, &DataSeg, 1, cbWrite, NULL,
                                                                 &g_AsyncCompletionTasks[i]);
                                AssertRC(rc);
                                offDst += cbWrite;
                                if (offDst == cbSrc)
                                    break;
                            }
                        }

                        rc = RTSemEventWait(g_FinishedEventSem, RT_INDEFINITE_WAIT);
                        AssertRC(rc);

                        if (!fReadPass && (offDst == cbSrc))
                            break;
                        else if (fReadPass)
                            fReadPass = false;
                        else
                        {
                            cTasksUsed = 0;
                            fReadPass = true;
                        }
                    }
                }
                else
                {
                    RTPrintf(TESTCASE ": Error querying size of the endpoint!! rc=%d\n", rc);
                    rcRet++;
                }

                PDMR3PowerOff(pVM);
                PDMR3AsyncCompletionEpClose(pEndpointDst);
            }
            PDMR3AsyncCompletionEpClose(pEndpointSrc);
        }

        rc = VMR3Destroy(pUVM);
        AssertMsg(rc == VINF_SUCCESS, ("%s: Destroying VM failed rc=%Rrc!!\n", __FUNCTION__, rc));
        VMR3ReleaseUVM(pUVM);

        /*
         * Clean up.
         */
        for (uint32_t i = 0; i < NR_TASKS; i++)
        {
            RTMemFree(g_AsyncCompletionTasksBuffer[i]);
        }
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

