/* $Id: tstIntNetR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Internal networking - Usermode testcase for the kernel mode bits.
 *
 * This is a bit hackish as we're mixing context here, however it is
 * very useful when making changes to the internal networking service.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#define IN_INTNET_TESTCASE
#define IN_INTNET_R3
#include <VBox/cdefs.h>
#undef INTNETR0DECL
#define INTNETR0DECL INTNETR3DECL
#undef DECLR0CALLBACKMEMBER
#define DECLR0CALLBACKMEMBER(type, name, args) DECLR3CALLBACKMEMBER(type, name, args)
#include <VBox/types.h>
typedef void *MYPSUPDRVSESSION;
#define PSUPDRVSESSION  MYPSUPDRVSESSION

#include <VBox/intnet.h>
#include <VBox/sup.h>
#include <VBox/err.h>
#include <iprt/asm.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/stream.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Security objectype.
 */
typedef enum SUPDRVOBJTYPE
{
    /** The usual invalid object. */
    SUPDRVOBJTYPE_INVALID = 0,
    /** Internal network. */
    SUPDRVOBJTYPE_INTERNAL_NETWORK,
    /** Internal network interface. */
    SUPDRVOBJTYPE_INTERNAL_NETWORK_INTERFACE,
    /** The first invalid object type in this end. */
    SUPDRVOBJTYPE_END,
    /** The usual 32-bit type size hack. */
    SUPDRVOBJTYPE_32_BIT_HACK = 0x7ffffff
} SUPDRVOBJTYPE;

/**
 * Object destructor callback.
 * This is called for reference counted objectes when the count reaches 0.
 *
 * @param   pvObj       The object pointer.
 * @param   pvUser1     The first user argument.
 * @param   pvUser2     The second user argument.
 */
typedef DECLCALLBACKTYPE(void, FNSUPDRVDESTRUCTOR,(void *pvObj, void *pvUser1, void *pvUser2));
/** Pointer to a FNSUPDRVDESTRUCTOR(). */
typedef FNSUPDRVDESTRUCTOR *PFNSUPDRVDESTRUCTOR;


/**
 * Dummy
 */
typedef struct OBJREF
{
    PFNSUPDRVDESTRUCTOR pfnDestructor;
    void *pvUser1;
    void *pvUser2;
    uint32_t volatile cRefs;
} OBJREF, *POBJREF;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test handle.*/
static RTTEST           g_hTest      = NIL_RTTEST;
/** The size (in bytes) of the large transfer tests. */
static uint32_t         g_cbTransfer = _1M * 384;
/** Fake session handle. */
const PSUPDRVSESSION    g_pSession   = (PSUPDRVSESSION)(uintptr_t)0xdeadface;


INTNETR3DECL(void *) SUPR0ObjRegister(PSUPDRVSESSION pSession, SUPDRVOBJTYPE enmType,
                                      PFNSUPDRVDESTRUCTOR pfnDestructor, void *pvUser1, void *pvUser2)
{
    RTTEST_CHECK_RET(g_hTest, pSession == g_pSession, NULL);
    POBJREF pRef = (POBJREF)RTTestGuardedAllocTail(g_hTest, sizeof(OBJREF));
    if (!pRef)
        return NULL;
    pRef->cRefs = 1;
    pRef->pfnDestructor = pfnDestructor;
    pRef->pvUser1 = pvUser1;
    pRef->pvUser2 = pvUser2;
    NOREF(enmType);
    return pRef;
}

INTNETR3DECL(int) SUPR0ObjAddRefEx(void *pvObj, PSUPDRVSESSION pSession, bool fNoBlocking)
{
    RTTEST_CHECK_RET(g_hTest, pSession == g_pSession, VERR_INVALID_PARAMETER);
    POBJREF pRef = (POBJREF)pvObj;
    ASMAtomicIncU32(&pRef->cRefs);
    NOREF(fNoBlocking);
    return VINF_SUCCESS;
}

INTNETR3DECL(int) SUPR0ObjAddRef(void *pvObj, PSUPDRVSESSION pSession)
{
    return SUPR0ObjAddRefEx(pvObj, pSession, false);
}

INTNETR3DECL(int) SUPR0ObjRelease(void *pvObj, PSUPDRVSESSION pSession)
{
    RTTEST_CHECK_RET(g_hTest, pSession == g_pSession, VERR_INVALID_PARAMETER);
    POBJREF pRef = (POBJREF)pvObj;
    if (!ASMAtomicDecU32(&pRef->cRefs))
    {
        pRef->pfnDestructor(pRef, pRef->pvUser1, pRef->pvUser2);
        RTTestGuardedFree(g_hTest, pRef);
        return VINF_OBJECT_DESTROYED;
    }
    return VINF_SUCCESS;
}

INTNETR3DECL(int) SUPR0ObjVerifyAccess(void *pvObj, PSUPDRVSESSION pSession, const char *pszObjName)
{
    RTTEST_CHECK_RET(g_hTest, pSession == g_pSession, VERR_INVALID_PARAMETER);
    NOREF(pvObj); NOREF(pszObjName);
    return VINF_SUCCESS;
}

INTNETR3DECL(int) SUPR0MemAlloc(PSUPDRVSESSION pSession, uint32_t cb, PRTR0PTR ppvR0, PRTR3PTR ppvR3)
{
    RTTEST_CHECK_RET(g_hTest, pSession == g_pSession, VERR_INVALID_PARAMETER);
    void *pv = RTTestGuardedAllocTail(g_hTest, cb);
    if (!pv)
        return VERR_NO_MEMORY;
    *ppvR0 = (RTR0PTR)pv;
    if (ppvR3)
        *ppvR3 = pv;
    return VINF_SUCCESS;
}

INTNETR3DECL(int) SUPR0MemFree(PSUPDRVSESSION pSession, RTHCUINTPTR uPtr)
{
    RTTEST_CHECK_RET(g_hTest, pSession == g_pSession, VERR_INVALID_PARAMETER);
    RTTestGuardedFree(g_hTest, (void *)uPtr);
    return VINF_SUCCESS;
}

/* Fake non-existing ring-0 APIs. */
#define RTThreadIsInInterrupt(hThread)      false
#define RTThreadPreemptIsEnabled(hThread)   true
#define RTMpCpuId()                         0

/* No CLI/POPF, please. */
#include <iprt/spinlock.h>
#undef  RTSPINLOCK_FLAGS_INTERRUPT_SAFE
#define RTSPINLOCK_FLAGS_INTERRUPT_SAFE     RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE


/* ugly but necessary for making R0 code compilable for R3. */
#undef LOG_GROUP
#include "../SrvIntNetR0.cpp"


/**
 * Sends the data @a pvBuf points to.
 */
static int tstIntNetSendBuf(PINTNETRINGBUF pRingBuf, INTNETIFHANDLE hIf,
                            PSUPDRVSESSION pSession, void const *pvBuf, size_t cbBuf)
{
    union
    {
        uint8_t abBuf[sizeof(INTNETSG) + sizeof(INTNETSEG)];
        INTNETSG SG;
    } u;
    IntNetSgInitTemp(&u.SG, (void *)pvBuf, (uint32_t)cbBuf);
    int rc = intnetR0RingWriteFrame(pRingBuf, &u.SG, NULL);
    if (RT_SUCCESS(rc))
        rc = IntNetR0IfSend(hIf, pSession);
    return rc;
}


typedef struct MYARGS
{
    PINTNETBUF      pBuf;
    INTNETIFHANDLE  hIf;
    RTMAC           Mac;
    uint32_t        cbFrame;
    uint64_t        u64Start;
    uint64_t        u64End;
    uint32_t        cbSent;
    uint32_t        cFramesSent;
} MYARGS, *PMYARGS;


/**
 * Frame header used when testing.
 */
#pragma pack(1)
typedef struct MYFRAMEHDR
{
    RTMAC       SrcMac;
    RTMAC       DstMac;
    uint32_t    iFrame;
    uint32_t    auEos[3];
} MYFRAMEHDR;
#pragma pack()

/**
 * Send thread.
 * This is constantly sending frames to the other interface.
 */
static DECLCALLBACK(int) SendThread(RTTHREAD hThreadSelf, void *pvArg)
{
    PMYARGS pArgs = (PMYARGS)pvArg;
    int rc;
    NOREF(hThreadSelf);

    /*
     * Send g_cbTransfer of data.
     */
    uint8_t         abBuf[16384] = {0};
    MYFRAMEHDR     *pHdr    = (MYFRAMEHDR *)&abBuf[0];
    uint32_t        iFrame  = 0;
    uint32_t        cbSent  = 0;
    uint32_t        cErrors = 0;

    pHdr->SrcMac            = pArgs->Mac;
    pHdr->DstMac            = pArgs->Mac;
    pHdr->DstMac.au16[2]    = (pArgs->Mac.au16[2] + 1) % 2;

    pArgs->u64Start = RTTimeNanoTS();
    for (; cbSent < g_cbTransfer; iFrame++)
    {
        const unsigned cb = pArgs->cbFrame
                          ? pArgs->cbFrame
                          : iFrame % 1519 + sizeof(RTMAC) * 2 + sizeof(unsigned);
        pHdr->iFrame = iFrame;

        union
        {
            uint8_t abBuf[sizeof(INTNETSG) + sizeof(INTNETSEG)];
            INTNETSG SG;
        } u;
        IntNetSgInitTemp(&u.SG, abBuf, cb);
        RTTEST_CHECK_RC_OK(g_hTest, rc = intnetR0RingWriteFrame(&pArgs->pBuf->Send, &u.SG, NULL));
        if (RT_SUCCESS(rc))
            RTTEST_CHECK_RC_OK(g_hTest, rc = IntNetR0IfSend(pArgs->hIf, g_pSession));
        if (RT_FAILURE(rc) && ++cErrors > 64)
        {
            RTTestFailed(g_hTest, "Aborting xmit after >64 errors");
            break;
        }

        cbSent += cb;
    }
    pArgs->cbSent      = cbSent;
    pArgs->cFramesSent = iFrame;

    /*
     * Termination frames.
     */
    pHdr->iFrame   = 0xffffdead;
    pHdr->auEos[0] = 0xffffdead;
    pHdr->auEos[1] = 0xffffdead;
    pHdr->auEos[2] = 0xffffdead;
    for (unsigned c = 0; c < 20; c++)
    {
        RTTEST_CHECK_RC_OK(g_hTest, rc = tstIntNetSendBuf(&pArgs->pBuf->Send, pArgs->hIf, g_pSession,
                                                          abBuf, sizeof(RTMAC) * 2 + sizeof(unsigned) * 4));
        RTThreadSleep(1);
    }

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "sender   thread %.6Rhxs terminating.\n"
                 "iFrame=%u  cb=%'u\n",
                 &pArgs->Mac, iFrame, cbSent);
    return 0;
}


/** Ignore lost frames. It only makes things worse to bitch about it. */
#define IGNORE_LOST_FRAMES

/**
 * Receive thread.
 * This is reading stuff from the network.
 */
static DECLCALLBACK(int) ReceiveThread(RTTHREAD hThreadSelf, void *pvArg)
{
    uint32_t    cbReceived  = 0;
    uint32_t    cLostFrames = 0;
    uint32_t    iFrame      = UINT32_MAX;
    PMYARGS     pArgs       = (PMYARGS)pvArg;
    NOREF(hThreadSelf);

    for (;;)
    {
        /*
         * Read data.
         */
        while (IntNetRingHasMoreToRead(&pArgs->pBuf->Recv))
        {
            uint8_t     abBuf[16384 + 1024];
            MYFRAMEHDR *pHdr = (MYFRAMEHDR *)&abBuf[0];
            uint32_t    cb   = IntNetRingReadAndSkipFrame(&pArgs->pBuf->Recv, abBuf);

            /* check for termination frame. */
            if (    pHdr->iFrame   == 0xffffdead
                &&  pHdr->auEos[0] == 0xffffdead
                &&  pHdr->auEos[1] == 0xffffdead
                &&  pHdr->auEos[2] == 0xffffdead)
            {
                pArgs->u64End = RTTimeNanoTS();
                RTThreadSleep(10);
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                             "receiver thread %.6Rhxs terminating.\n"
                             "  iFrame=%u  cb=%'u  c=%'u  %'uKB/s  %'ufps  cLost=%'u \n",
                             &pArgs->Mac, iFrame, cbReceived, iFrame - cLostFrames,
                             (unsigned)((double)cbReceived * 1000000000.0 / 1024 / (double)(pArgs->u64End - pArgs->u64Start)),
                             (unsigned)((double)(iFrame - cLostFrames) * 1000000000.0 / (double)(pArgs->u64End - pArgs->u64Start)),
                             cLostFrames);
                return VINF_SUCCESS;
            }

            /* validate frame header */
            if (    pHdr->DstMac.au16[0] != pArgs->Mac.au16[0]
                ||  pHdr->DstMac.au16[1] != pArgs->Mac.au16[1]
                ||  pHdr->DstMac.au16[2] != pArgs->Mac.au16[2]
                ||  pHdr->SrcMac.au16[0] != pArgs->Mac.au16[0]
                ||  pHdr->SrcMac.au16[1] != pArgs->Mac.au16[1]
                ||  pHdr->SrcMac.au16[2] != (pArgs->Mac.au16[2] + 1) % 2)
            {
                RTTestFailed(g_hTest, "receiver thread %.6Rhxs received frame header: %.16Rhxs\n", &pArgs->Mac, abBuf);
            }

            /* frame stuff and stats. */
            int32_t off = pHdr->iFrame - (iFrame + 1);
            if (off)
            {
                if (off > 0)
                {
#ifndef IGNORE_LOST_FRAMES
                    RTTestFailed(g_hTest, "receiver thread %.6Rhxs: iFrame=%#x *puFrame=%#x off=%d\n",
                                 &pArgs->Mac, iFrame, pHdr->iFrame, off);
#endif
                    cLostFrames += off;
                }
                else
                {
                    cLostFrames++;
                    RTTestFailed(g_hTest, "receiver thread %.6Rhxs: iFrame=%#x *puFrame=%#x off=%d\n",
                                 &pArgs->Mac, iFrame, pHdr->iFrame, off);
                }
            }
            iFrame = pHdr->iFrame;
            cbReceived += cb;
        }

        /*
         * Wait for data.
         */
        int rc = IntNetR0IfWait(pArgs->hIf, g_pSession, RT_INDEFINITE_WAIT);
        switch (rc)
        {
            case VERR_INTERRUPTED:
            case VINF_SUCCESS:
                break;
            case VERR_SEM_DESTROYED:
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                             "receiver thread %.6Rhxs terminating. iFrame=%u cb=%'u c=%'u cLost=%'u\n",
                             &pArgs->Mac, iFrame, cbReceived, iFrame - cLostFrames, cLostFrames);
                return VINF_SUCCESS;

            default:
                RTTestFailed(g_hTest, "receiver thread %.6Rhxs got odd return value %Rrc! iFrame=%u cb=%'u c=%'u cLost=%'u\n",
                             &pArgs->Mac, rc, iFrame, cbReceived, iFrame - cLostFrames, cLostFrames);
                return rc;
        }

    }
}


/**
 * Drains the interface buffer before starting a new bi-directional run.
 *
 * We may have termination frames from previous runs pending in the buffer.
 */
static void tstDrainInterfaceBuffer(PMYARGS pArgs)
{
    uint8_t abBuf[16384 + 1024];
    while (IntNetRingHasMoreToRead(&pArgs->pBuf->Recv))
        IntNetRingReadAndSkipFrame(&pArgs->pBuf->Recv, abBuf);
}


/**
 * Test state.
 */
typedef struct TSTSTATE
{
    PINTNETBUF      pBuf0;
    INTNETIFHANDLE  hIf0;

    PINTNETBUF      pBuf1;
    INTNETIFHANDLE  hIf1;
} TSTSTATE;
typedef TSTSTATE *PTSTSTATE;


/**
 * Open two internal network interfaces.
 *
 * @returns IPRT status of the first failure.
 * @param   pThis               The test instance.
 */
static int tstOpenInterfaces(PTSTSTATE pThis, const char *pszNetwork, uint32_t cbSend, uint32_t cbRecv)
{
    pThis->hIf0 = INTNET_HANDLE_INVALID;
    RTTESTI_CHECK_RC_OK_RET(IntNetR0Open(g_pSession, pszNetwork, kIntNetTrunkType_None, "", 0/*fFlags*/, cbSend, cbRecv,
                                         NULL /*pfnRecvAvail*/, NULL /*pvUser*/, &pThis->hIf0), rcCheck);
    RTTESTI_CHECK_RET(pThis->hIf0 != INTNET_HANDLE_INVALID, VERR_INTERNAL_ERROR);
    RTTESTI_CHECK_RC_RET(IntNetR0IfGetBufferPtrs(pThis->hIf0, g_pSession, &pThis->pBuf0, NULL), VINF_SUCCESS, rcCheck);
    RTTESTI_CHECK_RET(pThis->pBuf0, VERR_INTERNAL_ERROR);


    pThis->hIf1 = INTNET_HANDLE_INVALID;
    RTTESTI_CHECK_RC_OK_RET(IntNetR0Open(g_pSession, pszNetwork, kIntNetTrunkType_None, "", 0/*fFlags*/, cbSend, cbRecv,
                                         NULL /*pfnRecvAvail*/, NULL /*pvUser*/, &pThis->hIf1), rcCheck);
    RTTESTI_CHECK_RET(pThis->hIf1 != INTNET_HANDLE_INVALID, VERR_INTERNAL_ERROR);
    RTTESTI_CHECK_RC_RET(IntNetR0IfGetBufferPtrs(pThis->hIf1, g_pSession, &pThis->pBuf1, NULL), VINF_SUCCESS, rcCheck);
    RTTESTI_CHECK_RET(pThis->pBuf1, VERR_INTERNAL_ERROR);

    return VINF_SUCCESS;
}

/**
 * Close the interfaces.
 *
 * @param   pThis               The test instance.
 */
static void tstCloseInterfaces(PTSTSTATE pThis)
{
    int rc;
    RTTESTI_CHECK_RC_OK(rc = IntNetR0IfClose(pThis->hIf0, g_pSession));
    if (RT_SUCCESS(rc))
    {
        pThis->hIf0  = INTNET_HANDLE_INVALID;
        pThis->pBuf0 = NULL;
    }

    RTTESTI_CHECK_RC_OK(rc = IntNetR0IfClose(pThis->hIf1, g_pSession));
    if (RT_SUCCESS(rc))
    {
        pThis->hIf1  = INTNET_HANDLE_INVALID;
        pThis->pBuf1 = NULL;
    }

    /* The network should be dead now. */
    RTTESTI_CHECK(IntNetR0GetNetworkCount() == 0);
}

/**
 * Do the bi-directional transfer test.
 */
static void tstBidirectionalTransfer(PTSTSTATE pThis, uint32_t cbFrame)
{
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "-------------------------------------------------------------\n");

    /*
     * Reset statistics.
     */
    pThis->pBuf0->cStatYieldsOk.c       = 0;
    pThis->pBuf0->cStatYieldsNok.c      = 0;
    pThis->pBuf0->cStatLost.c           = 0;
    pThis->pBuf0->cStatBadFrames.c      = 0;
    pThis->pBuf0->Recv.cStatFrames.c    = 0;
    pThis->pBuf0->Recv.cbStatWritten.c  = 0;
    pThis->pBuf0->Recv.cOverflows.c     = 0;
    pThis->pBuf0->Send.cStatFrames.c    = 0;
    pThis->pBuf0->Send.cbStatWritten.c  = 0;
    pThis->pBuf0->Send.cOverflows.c     = 0;
    pThis->pBuf1->cStatYieldsOk.c       = 0;
    pThis->pBuf1->cStatYieldsNok.c      = 0;
    pThis->pBuf1->cStatLost.c           = 0;
    pThis->pBuf1->cStatBadFrames.c      = 0;
    pThis->pBuf1->Recv.cStatFrames.c    = 0;
    pThis->pBuf1->Recv.cbStatWritten.c  = 0;
    pThis->pBuf1->Recv.cOverflows.c     = 0;
    pThis->pBuf1->Send.cStatFrames.c    = 0;
    pThis->pBuf1->Send.cbStatWritten.c  = 0;
    pThis->pBuf1->Send.cOverflows.c     = 0;

    /*
     * Do the benchmarking.
     */
    MYARGS Args0;
    RT_ZERO(Args0);
    Args0.hIf         = pThis->hIf0;
    Args0.pBuf        = pThis->pBuf0;
    Args0.Mac.au16[0] = 0x8086;
    Args0.Mac.au16[1] = 0;
    Args0.Mac.au16[2] = 0;
    Args0.cbFrame     = cbFrame;
    tstDrainInterfaceBuffer(&Args0);
    //RTTESTI_CHECK_RC_OK(IntNetR0IfSetMacAddress(pThis->hIf0, g_pSession, &Args0.Mac));

    MYARGS Args1;
    RT_ZERO(Args1);
    Args1.hIf         = pThis->hIf1;
    Args1.pBuf        = pThis->pBuf1;
    Args1.Mac.au16[0] = 0x8086;
    Args1.Mac.au16[1] = 0;
    Args1.Mac.au16[2] = 1;
    Args1.cbFrame     = cbFrame;
    tstDrainInterfaceBuffer(&Args1);
    //RTTESTI_CHECK_RC_OK(IntNetR0IfSetMacAddress(pThis->hIf1, g_pSession, &Args1.Mac));

    RTTHREAD ThreadRecv0 = NIL_RTTHREAD;
    RTTHREAD ThreadRecv1 = NIL_RTTHREAD;
    RTTHREAD ThreadSend0 = NIL_RTTHREAD;
    RTTHREAD ThreadSend1 = NIL_RTTHREAD;
    RTTESTI_CHECK_RC_OK_RETV(RTThreadCreate(&ThreadRecv0, ReceiveThread, &Args0, 0, RTTHREADTYPE_IO,        RTTHREADFLAGS_WAITABLE, "RECV0"));
    RTTESTI_CHECK_RC_OK_RETV(RTThreadCreate(&ThreadRecv1, ReceiveThread, &Args1, 0, RTTHREADTYPE_IO,        RTTHREADFLAGS_WAITABLE, "RECV1"));
    RTTESTI_CHECK_RC_OK_RETV(RTThreadCreate(&ThreadSend0, SendThread,    &Args0, 0, RTTHREADTYPE_EMULATION, RTTHREADFLAGS_WAITABLE, "SEND0"));
    RTTESTI_CHECK_RC_OK_RETV(RTThreadCreate(&ThreadSend1, SendThread,    &Args1, 0, RTTHREADTYPE_EMULATION, RTTHREADFLAGS_WAITABLE, "SEND1"));

    int rc2 = VINF_SUCCESS;
    int rc;
    RTTESTI_CHECK_RC_OK(rc = RTThreadWait(ThreadSend0, RT_MS_5MIN, &rc2));
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK_RC_OK(rc2);
        ThreadSend0 = NIL_RTTHREAD;
        RTTESTI_CHECK_RC_OK(rc = RTThreadWait(ThreadSend1, RT_MS_5MIN, RT_SUCCESS(rc2) ? &rc2 : NULL));
        if (RT_SUCCESS(rc))
        {
            ThreadSend1 = NIL_RTTHREAD;
            RTTESTI_CHECK_RC_OK(rc2);
        }
    }
    if (RTTestErrorCount(g_hTest) == 0)
    {
        /*
         * Wait a bit for the receivers to finish up.
         */
        unsigned cYields = 100000;
        while (     (  IntNetRingHasMoreToRead(&pThis->pBuf0->Recv)
                    || IntNetRingHasMoreToRead(&pThis->pBuf1->Recv))
               &&   cYields-- > 0)
            RTThreadYield();

        /*
         * Wait for the threads to finish up...
         */
        RTTESTI_CHECK_RC_OK(rc = RTThreadWait(ThreadRecv0, RT_MS_5SEC, &rc2));
        if (RT_SUCCESS(rc))
        {
            RTTESTI_CHECK_RC_OK(rc2);
            ThreadRecv0 = NIL_RTTHREAD;
        }

        RTTESTI_CHECK_RC_OK(rc = RTThreadWait(ThreadRecv1, RT_MS_5MIN, &rc2));
        if (RT_SUCCESS(rc))
        {
            RTTESTI_CHECK_RC_OK(rc2);
            ThreadRecv1 = NIL_RTTHREAD;
        }

        /*
         * Report the results.
         */
        uint64_t cNsElapsed = RT_MAX(Args0.u64End, Args1.u64End) - RT_MIN(Args0.u64Start, Args1.u64Start);
        uint64_t cbSent     = (uint64_t)Args0.cbSent      + Args1.cbSent;
        uint64_t cKbps      = (uint64_t)((double)(cbSent / 1024) / ((double)cNsElapsed / 1000000000.0));
        uint64_t cFrames    = (uint64_t)Args0.cFramesSent + Args1.cFramesSent;
        uint64_t cFps       = (uint64_t)((double)cFrames / ((double)cNsElapsed / 1000000000.0));
        RTTestValue(g_hTest, "frame size",  cbFrame,    RTTESTUNIT_BYTES);
        RTTestValue(g_hTest, "xmit time",   cNsElapsed, RTTESTUNIT_NS);
        RTTestValue(g_hTest, "bytes sent",  cbSent,     RTTESTUNIT_BYTES);
        RTTestValue(g_hTest, "speed",       cKbps,      RTTESTUNIT_KILOBYTES_PER_SEC);
        RTTestValue(g_hTest, "frames sent", cFrames,    RTTESTUNIT_FRAMES);
        RTTestValue(g_hTest, "fps",         cFps,       RTTESTUNIT_FRAMES_PER_SEC);
        RTTestValue(g_hTest, "overflows",
                    pThis->pBuf0->Send.cOverflows.c + pThis->pBuf1->Send.cOverflows.c, RTTESTUNIT_OCCURRENCES);

    }

    /*
     * Give them a chance to complete...
     */
    RTThreadWait(ThreadRecv0, RT_MS_5MIN, NULL);
    RTThreadWait(ThreadRecv1, RT_MS_5MIN, NULL);
    RTThreadWait(ThreadSend0, RT_MS_5MIN, NULL);
    RTThreadWait(ThreadSend1, RT_MS_5MIN, NULL);


    /*
     * Display statistics.
     */
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "Buf0: Yields-OK=%llu Yields-NOK=%llu Lost=%llu Bad=%llu\n",
                 pThis->pBuf0->cStatYieldsOk.c,
                 pThis->pBuf0->cStatYieldsNok.c,
                 pThis->pBuf0->cStatLost.c,
                 pThis->pBuf0->cStatBadFrames.c);
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "Buf0.Recv: Frames=%llu Bytes=%llu Overflows=%llu\n",
                 pThis->pBuf0->Recv.cStatFrames.c,
                 pThis->pBuf0->Recv.cbStatWritten.c,
                 pThis->pBuf0->Recv.cOverflows.c);
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "Buf0.Send: Frames=%llu Bytes=%llu Overflows=%llu\n",
                 pThis->pBuf0->Send.cStatFrames.c,
                 pThis->pBuf0->Send.cbStatWritten.c,
                 pThis->pBuf0->Send.cOverflows.c);

    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "Buf1: Yields-OK=%llu Yields-NOK=%llu Lost=%llu Bad=%llu\n",
                 pThis->pBuf1->cStatYieldsOk.c,
                 pThis->pBuf1->cStatYieldsNok.c,
                 pThis->pBuf1->cStatLost.c,
                 pThis->pBuf1->cStatBadFrames.c);
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "Buf1.Recv: Frames=%llu Bytes=%llu Overflows=%llu\n",
                 pThis->pBuf1->Recv.cStatFrames.c,
                 pThis->pBuf1->Recv.cbStatWritten.c,
                 pThis->pBuf1->Recv.cOverflows.c);
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                 "Buf1.Send: Frames=%llu Bytes=%llu Overflows=%llu\n",
                 pThis->pBuf1->Send.cStatFrames.c,
                 pThis->pBuf1->Send.cbStatWritten.c,
                 pThis->pBuf1->Send.cOverflows.c);

}

/**
 * Performs a simple broadcast test.
 *
 * @param   pThis               The test instance.
 * @param   fHeadGuard          Whether to use a head or tail guard.
 */
static void doBroadcastTest(PTSTSTATE pThis, bool fHeadGuard)
{
    static uint16_t const s_au16Frame[7] = { /* dst:*/ 0xffff, 0xffff, 0xffff, /*src:*/0x8086, 0, 0, 0x0800 };

    RTTESTI_CHECK_RC_RETV(tstIntNetSendBuf(&pThis->pBuf0->Send, pThis->hIf0,
                                           g_pSession, &s_au16Frame, sizeof(s_au16Frame)),
                          VINF_SUCCESS);

    /* No echo, please */
    RTTESTI_CHECK_RC_RETV(IntNetR0IfWait(pThis->hIf0, g_pSession, 1), VERR_TIMEOUT);

    /* The other interface should see it though.  But Wait should only return once, thank you. */
    RTTESTI_CHECK_RC_RETV(IntNetR0IfWait(pThis->hIf1, g_pSession, 1), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(IntNetR0IfWait(pThis->hIf1, g_pSession, 0), VERR_TIMEOUT);

    /* Receive the data. */
    const unsigned cbExpect = RT_ALIGN(sizeof(s_au16Frame) + sizeof(INTNETHDR), sizeof(INTNETHDR));
    RTTESTI_CHECK_MSG(IntNetRingGetReadable(&pThis->pBuf1->Recv) == cbExpect,
                      ("%#x vs. %#x\n", IntNetRingGetReadable(&pThis->pBuf1->Recv), cbExpect));

    void *pvBuf;
    RTTESTI_CHECK_RC_OK_RETV(RTTestGuardedAlloc(g_hTest, sizeof(s_au16Frame), 1, fHeadGuard, &pvBuf));
    uint32_t cb;
    RTTESTI_CHECK_MSG_RETV((cb = IntNetRingReadAndSkipFrame(&pThis->pBuf1->Recv, pvBuf)) == sizeof(s_au16Frame),
                           ("%#x vs. %#x\n", cb, sizeof(s_au16Frame)));

    if (memcmp(pvBuf, &s_au16Frame, sizeof(s_au16Frame)))
        RTTestIFailed("Got invalid data!\n"
                      "received: %.*Rhxs\n"
                      "expected: %.*Rhxs\n",
                      cb, pvBuf, sizeof(s_au16Frame), &s_au16Frame);
}

/**
 * Performs a simple unicast test.
 *
 * @param   pThis               The test instance.
 * @param   fHeadGuard          Whether to use a head or tail guard.
 */
static void doUnicastTest(PTSTSTATE pThis, bool fHeadGuard)
{
    static uint16_t const s_au16Frame[7] = { /* dst:*/ 0x8086, 0, 0,      /*src:*/0x8086, 0, 1, 0x0800 };

    RTTESTI_CHECK_RC_RETV(tstIntNetSendBuf(&pThis->pBuf1->Send, pThis->hIf1,
                                           g_pSession, s_au16Frame, sizeof(s_au16Frame)),
                          VINF_SUCCESS);

    /* No echo, please */
    RTTESTI_CHECK_RC_RETV(IntNetR0IfWait(pThis->hIf1, g_pSession, 1), VERR_TIMEOUT);

    /* The other interface should see it though.  But Wait should only return once, thank you. */
    RTTESTI_CHECK_RC_RETV(IntNetR0IfWait(pThis->hIf0, g_pSession, 1), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(IntNetR0IfWait(pThis->hIf0, g_pSession, 0), VERR_TIMEOUT);

    /* Receive the data. */
    const unsigned cbExpect = RT_ALIGN(sizeof(s_au16Frame) + sizeof(INTNETHDR), sizeof(INTNETHDR));
    RTTESTI_CHECK_MSG(IntNetRingGetReadable(&pThis->pBuf0->Recv) == cbExpect,
                      ("%#x vs. %#x\n", IntNetRingGetReadable(&pThis->pBuf0->Recv), cbExpect));

    void *pvBuf;
    RTTESTI_CHECK_RC_OK_RETV(RTTestGuardedAlloc(g_hTest, sizeof(s_au16Frame), 1, fHeadGuard, &pvBuf));
    uint32_t cb;
    RTTESTI_CHECK_MSG_RETV((cb = IntNetRingReadAndSkipFrame(&pThis->pBuf0->Recv, pvBuf)) == sizeof(s_au16Frame),
                           ("%#x vs. %#x\n", cb, sizeof(s_au16Frame)));

    if (memcmp(pvBuf, &s_au16Frame, sizeof(s_au16Frame)))
        RTTestIFailed("Got invalid data!\n"
                      "received: %.*Rhxs\n"
                      "expected: %.*Rhxs\n",
                      cb, pvBuf, sizeof(s_au16Frame), s_au16Frame);
}

static void doTest(PTSTSTATE pThis, uint32_t cbRecv, uint32_t cbSend)
{

    /*
     * Create an INTNET instance.
     */
    RTTestISub("IntNetR0Init");
    RTTESTI_CHECK_RC_RETV(IntNetR0Init(), VINF_SUCCESS);

    /*
     * Create two interfaces and activate them.
     */
    RTTestISub("Network creation");
    int rc = tstOpenInterfaces(pThis, "test", cbSend, cbRecv);
    if (RT_FAILURE(rc))
        return;
    RTTESTI_CHECK_RC(IntNetR0IfSetActive(pThis->hIf0, g_pSession, true), VINF_SUCCESS);
    RTTESTI_CHECK_RC(IntNetR0IfSetActive(pThis->hIf1, g_pSession, true), VINF_SUCCESS);

    /*
     * Test basic waiting.
     */
    RTTestISub("IntNetR0IfWait");
    RTTESTI_CHECK_RC(IntNetR0IfWait(pThis->hIf0, g_pSession, 1), VERR_TIMEOUT);
    RTTESTI_CHECK_RC(IntNetR0IfWait(pThis->hIf0, g_pSession, 0), VERR_TIMEOUT);
    RTTESTI_CHECK_RC(IntNetR0IfWait(pThis->hIf1, g_pSession, 1), VERR_TIMEOUT);
    RTTESTI_CHECK_RC(IntNetR0IfWait(pThis->hIf1, g_pSession, 0), VERR_TIMEOUT);

    /*
     * Broadcast send and receive.
     * (This establishes the MAC address of the 1st interface.)
     */
    RTTestISub("Broadcast");
    doBroadcastTest(pThis, false /*fHeadGuard*/);
    doBroadcastTest(pThis, true /*fHeadGuard*/);

    /*
     * Unicast send and receive.
     * (This establishes the MAC address of the 2nd interface.)
     */
    RTTestISub("Unicast");
    doUnicastTest(pThis, false /*fHeadGuard*/);
    doUnicastTest(pThis, true /*fHeadGuard*/);

    /*
     * Do the big bi-directional transfer test if the basics worked out.
     */
    if (!RTTestIErrorCount())
    {
        RTTestISubF("bi-dir benchmark, xbuf=%u rbuf=%u xfer=%u",
                    pThis->pBuf0->cbSend, pThis->pBuf0->cbRecv, g_cbTransfer);
        tstBidirectionalTransfer(pThis, 256);

        /* Only doing up to half the xmit buffer size as it is easy to get into a
           bad frame position from a previous run and run into overflow issues. */
        /** @todo fix the code so it skips to a more optimal buffer position? */
        for (uint32_t cbFrame = 64; cbFrame < cbSend / 2 - 64; cbFrame += 16)
        {
            RTTestISubF("bi-dir benchmark, xbuf=%u rbuf=%u xmit=%u frm=%u",
                        pThis->pBuf0->cbSend, pThis->pBuf0->cbRecv, g_cbTransfer, cbFrame);
            tstBidirectionalTransfer(pThis, cbFrame);
        }
    }

    /*
     * Destroy the service.
     */
    tstCloseInterfaces(pThis);
    IntNetR0Term();
}


int main(int argc, char **argv)
{
    int rc = RTTestInitAndCreate("tstIntNetR0", &g_hTest);
    if (rc)
        return rc;

    /*
     * Parse the arguments.
     */
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--recv-buffer",   'r', RTGETOPT_REQ_UINT32 },
        { "--send-buffer",   's', RTGETOPT_REQ_UINT32 },
        { "--transfer-size", 'l', RTGETOPT_REQ_UINT32 },
    };

    uint32_t cbSend = 1536*2 + 4;
    uint32_t cbRecv = 0x8000;

    int ch;
    RTGETOPTUNION Value;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((ch = RTGetOpt(&GetState, &Value)))
        switch (ch)
        {
            case 'l':
                g_cbTransfer = Value.u32;
                break;

            case 'r':
                cbRecv = Value.u32;
                break;

            case 's':
                cbSend = Value.u32;
                break;

            default:
                return RTGetOptPrintError(ch, &Value);
        }

    /*
     * Do the testing and report summary.
     */
    TSTSTATE This;
    RT_ZERO(This);
    doTest(&This, cbRecv, cbSend);

    return RTTestSummaryAndDestroy(g_hTest);
}

