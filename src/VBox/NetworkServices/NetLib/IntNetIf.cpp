/* $Id: IntNetIf.cpp 114964 2026-08-10 15:18:23Z andreas.loeffler@oracle.com $ */
/** @file
 * IntNetIfCtx - Abstract API implementing an IntNet connection using the R0 support driver or some R3 IPC variant.
 */

/*
 * Copyright (C) 2022-2026 Oracle and/or its affiliates.
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
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
# if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
#  define INTNETIF_WITH_R3_SVC_XPC
#  include <xpc/xpc.h> /* This needs to be here because it drags PVM in and cdefs.h needs to undefine it... */
# elif defined(VBOX_WITH_INTNET_SERVICE_IN_R3_LOCALIPC) \
    && (defined(RT_OS_WINDOWS) || defined(RT_OS_LINUX) || defined(VBOX_INTNET_TESTCASE_LOCALIPC))
#  define INTNETIF_WITH_R3_SVC_LOCALIPC
# else
#  error "No enabled R3 internal networking transport for this platform!"
# endif
#endif

#include <iprt/cdefs.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#if defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
# include <VBox/intnetr3ipc.h>
# include <iprt/localipc.h>
# include <iprt/shmem.h>
# include <iprt/env.h>
#endif

#include <VBox/err.h>
#include <VBox/sup.h>
#include <VBox/intnetinline.h>
#include <VBox/vmm/pdmnetinline.h>

#include "IntNetIf.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/**
 * Internal network interface context instance data.
 */
typedef struct INTNETIFCTXINT
{
    /** The support driver session handle. */
    PSUPDRVSESSION                  pSupDrvSession;
    /** Interface handle. */
    INTNETIFHANDLE                  hIf;
    /** The internal network buffer. */
    PINTNETBUF                      pBuf;
    /** Whether this context owns one SUPR3Init reference. */
    bool                            fSupInited;
#if defined (VBOX_WITH_INTNET_SERVICE_IN_R3)
    /** Flag whether this interface is using the internal network switch in userspace path. */
    bool                            fIntNetR3Svc;
    /** Receive event semaphore. */
    RTSEMEVENT                      hEvtRecv;
    /** Whether the context created and owns hEvtRecv. */
    bool                            fOwnEvtRecv;
    /** Set by IntNetR3IfWaitAbort to prevent any subsequent waits. */
    bool volatile                   fNoMoreWaits;
# if defined(INTNETIF_WITH_R3_SVC_XPC)
    /** XPC connection handle to the R3 internal network switch service. */
    xpc_connection_t                hXpcCon;
    /** Signalled by XPC's final invalid event after connection cancellation has quiesced callbacks. */
    RTSEMEVENT                      hEvtXpcCancelled;
    /** Size of the communication buffer in bytes. */
    size_t                          cbBuf;
# elif defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
    /** Local IPC session to the R3 internal network switch service. */
    RTLOCALIPCSESSION               hIpcSession;
    /** Thread receiving and demultiplexing local IPC replies and notifications. */
    RTTHREAD                        hIpcRecvThread;
    /** Serializes local IPC request/reply calls. */
    RTSEMMUTEX                      hIpcCallMtx;
    /** Serializes short socket read and write operations; data waits happen outside it. */
    RTSEMMUTEX                      hIpcIoMtx;
    /** Signalled by the receiver thread when a synchronous reply arrives. */
    RTSEMEVENT                      hEvtReply;
    /** Shared memory handle backing the communication buffer. */
    RTSHMEM                         hShMemBuf;
    /** Size of the communication buffer in bytes. */
    size_t                          cbBuf;
    /** Most recently received synchronous reply header. */
    INTNETR3IPCREPLYHDR             ReplyHdr;
    /** Most recently received synchronous reply payload. */
    uint8_t                         abReply[INTNET_R3_IPC_MAX_REQ];
    /** Shared memory name accompanying the most recent reply. */
    char                            szReplyShMemName[INTNET_R3_IPC_MAX_SHMEM_NAME];
    /** Status carried by the most recently received notification. */
    int32_t volatile                rcRecvPoke;
    /** Receiver thread status, set to a failure when the stream terminates. */
    int32_t volatile                rcIpcRecv;
# endif
#endif
} INTNETIFCTXINT;
/** Pointer to the internal network interface context instance data. */
typedef INTNETIFCTXINT *PINTNETIFCTXINT;


#ifdef VBOX_INTNET_TESTCASE_LOCALIPC
/** Interface paused by the deterministic Wait/Abort race testcase. */
static PINTNETIFCTXINT g_pTestWaitRaceIf = NULL;
/** Signalled after the test wait has performed its initial no-more-waits check. */
static RTSEMEVENT g_hTestWaitRaceReached = NIL_RTSEMEVENT;
/** Releases the paused test wait so it performs the serialized check and send. */
static RTSEMEVENT g_hTestWaitRaceContinue = NIL_RTSEMEVENT;


/** Configures the deterministic Wait/Abort race hook used by tstVBoxIntNetR3Switch. */
DECLHIDDEN(void) intnetR3IfTestSetWaitRace(INTNETIFCTX hIfCtx, RTSEMEVENT hReached, RTSEMEVENT hContinue)
{
    g_hTestWaitRaceReached = hReached;
    g_hTestWaitRaceContinue = hContinue;
    g_pTestWaitRaceIf = (PINTNETIFCTXINT)hIfCtx;
}
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

#if defined(INTNETIF_WITH_R3_SVC_XPC)
/** Cancels the XPC connection and waits until its event handler cannot run again. */
static void intnetR3IfXpcDisconnect(PINTNETIFCTXINT pThis)
{
    if (pThis->hXpcCon)
    {
        Assert(pThis->hEvtXpcCancelled != NIL_RTSEMEVENT);
        xpc_connection_cancel(pThis->hXpcCon);
        int const rc = RTSemEventWait(pThis->hEvtXpcCancelled, RT_INDEFINITE_WAIT);
        AssertReleaseRC(rc);
        xpc_release(pThis->hXpcCon);
        pThis->hXpcCon = NULL;
    }
    if (pThis->hEvtXpcCancelled != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThis->hEvtXpcCancelled);
        pThis->hEvtXpcCancelled = NIL_RTSEMEVENT;
    }
}


/** Validates an XPC reply and decodes the VBox status code. */
static int intnetR3IfXpcGetReplyStatus(xpc_object_t hObjReply, int *prcReq)
{
    AssertPtrReturn(prcReq, VERR_INVALID_POINTER);
    *prcReq = VERR_INVALID_STATE;
    AssertReturn(hObjReply != NULL, VERR_INVALID_STATE);

    xpc_type_t hType = xpc_get_type(hObjReply);
    if (hType == XPC_TYPE_ERROR)
        return hObjReply == XPC_ERROR_CONNECTION_INTERRUPTED ? VERR_INTERRUPTED : VERR_NET_CONNECTION_REFUSED;
    AssertReturn(hType == XPC_TYPE_DICTIONARY, VERR_INVALID_STATE);

    uint64_t const u64Rc = xpc_dictionary_get_uint64(hObjReply, "rc");
    AssertReturn(INTNET_R3_SVC_IS_VALID_RC(u64Rc), VERR_INVALID_STATE);
    *prcReq = INTNET_R3_SVC_GET_RC(u64Rc);
    return VINF_SUCCESS;
}
#endif


#if defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
# ifndef VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH
static int intnetR3IfLocalIpcGetServiceName(char *pszService, size_t cbService)
{
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    const char *pszTestService = RTEnvGet("VBOX_INTNET_R3_SVC_NAME");
    if (pszTestService && *pszTestService)
        return RTStrCopy(pszService, cbService, pszTestService);
# endif

    char szUser[256];
    int rc = RTProcQueryUsername(RTProcSelf(), szUser, sizeof(szUser), NULL /*pcbUser*/);
    if (RT_SUCCESS(rc))
    {
        ssize_t const cch = RTStrPrintf2(pszService, cbService, "%s-%08RX32", INTNET_R3_SVC_NAME, RTStrHash1(szUser));
        if (cch < 0 || (size_t)cch >= cbService)
            rc = VERR_BUFFER_OVERFLOW;
    }
    return rc;
}
# endif


# ifndef VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH
static bool intnetR3IfLocalIpcIsServiceAbsent(int rc)
{
    return    rc == VERR_FILE_NOT_FOUND
           || rc == VERR_PATH_NOT_FOUND
           || rc == VERR_NET_CONNECTION_REFUSED
           || rc == VERR_PIPE_NOT_CONNECTED;
}


static int intnetR3IfLocalIpcStartService(void)
{
    char szExec[RTPATH_MAX];
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    const char *pszTestExec = RTEnvGet("VBOX_INTNET_R3_SWITCH_EXE");
    int rc = pszTestExec && *pszTestExec ? RTStrCopy(szExec, sizeof(szExec), pszTestExec)
                                        : RTPathExecDir(szExec, sizeof(szExec));
# else
    int rc = RTPathExecDir(szExec, sizeof(szExec));
# endif
    if (RT_FAILURE(rc))
        return rc;
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    if (!pszTestExec || !*pszTestExec)
# endif
    {
# ifdef RT_OS_WINDOWS
        rc = RTPathAppend(szExec, sizeof(szExec), "VBoxIntNetSwitch.exe");
# else
        rc = RTPathAppend(szExec, sizeof(szExec), "VBoxIntNetSwitch");
# endif
        if (RT_FAILURE(rc))
            return rc;
    }

    const char *apszArgs[] = { szExec, NULL };
    RTHANDLE hStdNil;
    hStdNil.enmType = RTHANDLETYPE_FILE;
    hStdNil.u.hFile = NIL_RTFILE;
    uint32_t fFlags = RTPROC_FLAGS_DETACHED;
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    const char *pszTestPidFile = RTEnvGet("VBOX_INTNET_R3_SWITCH_PID_FILE");
    bool const fTestWaitable = pszTestPidFile && *pszTestPidFile;
    RTPROCESS hTestProcess = NIL_RTPROCESS;
    if (fTestWaitable)
        fFlags &= ~RTPROC_FLAGS_DETACHED;
# endif
# ifdef RT_OS_WINDOWS
    fFlags |= RTPROC_FLAGS_NO_WINDOW;
# endif
    rc = RTProcCreateEx(szExec, apszArgs, RTENV_DEFAULT, fFlags,
                        &hStdNil, &hStdNil, &hStdNil, NULL /*pszAsUser*/, NULL /*pszPassword*/,
                        NULL /*pvExtraData*/,
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
                        fTestWaitable ? &hTestProcess : NULL /*phProcess*/);
    if (RT_SUCCESS(rc) && fTestWaitable)
    {
        RTFILE hPidFile = NIL_RTFILE;
        rc = RTFileOpen(&hPidFile, pszTestPidFile, RTFILE_O_WRITE | RTFILE_O_CREATE | RTFILE_O_DENY_ALL
                                                   | (0600 << RTFILE_O_CREATE_MODE_SHIFT));
        if (RT_SUCCESS(rc))
        {
            char szPid[32];
            ssize_t const cchPid = RTStrPrintf2(szPid, sizeof(szPid), "%RU32", hTestProcess);
            if (cchPid > 0 && (size_t)cchPid < sizeof(szPid))
                rc = RTFileWrite(hPidFile, szPid, (size_t)cchPid, NULL /*pcbWritten*/);
            else
                rc = VERR_BUFFER_OVERFLOW;
            int const rc2 = RTFileClose(hPidFile);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
        if (RT_FAILURE(rc))
        {
            RTProcTerminate(hTestProcess);
            RTProcWait(hTestProcess, RTPROCWAIT_FLAGS_BLOCK, NULL /*pProcStatus*/);
        }
    }
# else
                        NULL /*phProcess*/);
# endif
    return rc;
}
# endif


/** Returns the absolute frame-read timeout, shortened only by local IPC testcases. */
static uint32_t intnetR3IfLocalIpcGetFrameTimeout(void)
{
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    const char *pszTestTimeout = RTEnvGet("VBOX_INTNET_R3_TEST_READ_TIMEOUT_MS");
    if (pszTestTimeout && *pszTestTimeout)
    {
        uint32_t cMsTimeout = 0;
        if (   RT_SUCCESS(RTStrToUInt32Full(pszTestTimeout, 10, &cMsTimeout))
            && cMsTimeout > 0)
            return cMsTimeout;
    }
# endif
    return INTNET_R3_IPC_FRAME_TIMEOUT_MS;
}


/** Returns the synchronous reply wait timeout, overridden independently only by local IPC testcases. */
static uint32_t intnetR3IfLocalIpcGetReplyTimeout(void)
{
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    const char *pszTestTimeout = RTEnvGet("VBOX_INTNET_R3_TEST_REPLY_TIMEOUT_MS");
    if (pszTestTimeout && *pszTestTimeout)
    {
        uint32_t cMsTimeout = 0;
        if (   RT_SUCCESS(RTStrToUInt32Full(pszTestTimeout, 10, &cMsTimeout))
            && cMsTimeout > 0)
            return cMsTimeout;
    }
# endif
    return intnetR3IfLocalIpcGetFrameTimeout();
}


/** Reads exactly @a cbToRead bytes without letting partial progress reset the deadline. */
static int intnetR3IfLocalIpcReadExact(PINTNETIFCTXINT pThis, void *pvBuf, size_t cbToRead, uint64_t msDeadline)
{
    uint8_t *pbDst = (uint8_t *)pvBuf;
    while (cbToRead > 0)
    {
        size_t cbRead = 0;
        int rc = RTLocalIpcSessionReadNB(pThis->hIpcSession, pbDst, cbToRead, &cbRead);
        if (rc == VINF_SUCCESS && cbRead > 0)
        {
            AssertReturn(cbRead <= cbToRead, VERR_INTERNAL_ERROR);
            pbDst    += cbRead;
            cbToRead -= cbRead;
            continue;
        }
        if (rc != VINF_SUCCESS && rc != VINF_TRY_AGAIN)
            return rc;

        uint64_t const msNow = RTTimeMilliTS();
        if (msNow >= msDeadline)
            return VERR_TIMEOUT;
        uint64_t const cMsLeft = msDeadline - msNow;
        rc = RTLocalIpcSessionWaitForData(pThis->hIpcSession,
                                          (uint32_t)RT_MIN(cMsLeft, (uint64_t)UINT32_MAX));
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


/** Local IPC receiver thread. */
static DECLCALLBACK(int) intnetR3IfLocalIpcRecvThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    PINTNETIFCTXINT pThis = (PINTNETIFCTXINT)pvUser;
    uint32_t const cMsFrameTimeout = intnetR3IfLocalIpcGetFrameTimeout();
    int rc = VINF_SUCCESS;

    for (;;)
    {
        rc = RTLocalIpcSessionWaitForData(pThis->hIpcSession, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            break;
        rc = RTSemMutexRequest(pThis->hIpcIoMtx, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            break;

        uint64_t const msDeadline = RTTimeMilliTS() + cMsFrameTimeout;
        INTNETR3IPCREPLYHDR Hdr;
        rc = intnetR3IfLocalIpcReadExact(pThis, &Hdr, sizeof(Hdr), msDeadline);
        if (RT_FAILURE(rc))
        {
            RTSemMutexRelease(pThis->hIpcIoMtx);
            break;
        }
        if (   Hdr.u16Version != INTNET_R3_IPC_VERSION
            || Hdr.cbHdr != sizeof(Hdr))
        {
            rc = VERR_VERSION_MISMATCH;
            RTSemMutexRelease(pThis->hIpcIoMtx);
            break;
        }

        if (Hdr.u32Magic == INTNET_R3_IPC_POKE_MAGIC)
        {
            if (Hdr.cbReply != 0 || Hdr.cbShMemName != 0 || Hdr.cbShMem != 0)
            {
                rc = VERR_INVALID_PARAMETER;
                RTSemMutexRelease(pThis->hIpcIoMtx);
                break;
            }
            ASMAtomicWriteS32(&pThis->rcRecvPoke, Hdr.rc);
            RTSemEventSignal(pThis->hEvtRecv);
        }
        else if (Hdr.u32Magic == INTNET_R3_IPC_REPLY_MAGIC)
        {
            if (   Hdr.cbReply > sizeof(pThis->abReply)
                || Hdr.cbShMemName > sizeof(pThis->szReplyShMemName))
            {
                rc = VERR_BUFFER_OVERFLOW;
                RTSemMutexRelease(pThis->hIpcIoMtx);
                break;
            }

            if (Hdr.cbReply)
            {
                rc = intnetR3IfLocalIpcReadExact(pThis, pThis->abReply, Hdr.cbReply, msDeadline);
                if (RT_FAILURE(rc))
                {
                    RTSemMutexRelease(pThis->hIpcIoMtx);
                    break;
                }
            }
            pThis->szReplyShMemName[0] = '\0';
            if (Hdr.cbShMemName)
            {
                rc = intnetR3IfLocalIpcReadExact(pThis, pThis->szReplyShMemName, Hdr.cbShMemName, msDeadline);
                if (RT_FAILURE(rc))
                {
                    RTSemMutexRelease(pThis->hIpcIoMtx);
                    break;
                }
                if (pThis->szReplyShMemName[Hdr.cbShMemName - 1] != '\0')
                {
                    rc = VERR_INVALID_PARAMETER;
                    RTSemMutexRelease(pThis->hIpcIoMtx);
                    break;
                }
            }
            pThis->ReplyHdr = Hdr;
            RTSemEventSignal(pThis->hEvtReply);
        }
        else
        {
            rc = VERR_INVALID_MAGIC;
            RTSemMutexRelease(pThis->hIpcIoMtx);
            break;
        }
        RTSemMutexRelease(pThis->hIpcIoMtx);
    }

    ASMAtomicWriteS32(&pThis->rcIpcRecv, rc);
    RTSemEventSignal(pThis->hEvtRecv);
    RTSemEventSignal(pThis->hEvtReply);
    return rc;
}


/** Stops the receiver and closes a failed or no-longer-needed local IPC transport. */
static void intnetR3IfLocalIpcRetireSession(PINTNETIFCTXINT pThis)
{
    if (   pThis->hIpcSession != NIL_RTLOCALIPCSESSION
        && pThis->hIpcRecvThread != NIL_RTTHREAD)
        RTLocalIpcSessionCancel(pThis->hIpcSession);
    if (pThis->hIpcRecvThread != NIL_RTTHREAD)
    {
        int const rcThread = RTThreadWait(pThis->hIpcRecvThread, RT_INDEFINITE_WAIT, NULL);
        AssertRC(rcThread);
        pThis->hIpcRecvThread = NIL_RTTHREAD;
    }
    if (pThis->hIpcSession != NIL_RTLOCALIPCSESSION)
    {
        RTLocalIpcSessionClose(pThis->hIpcSession);
        pThis->hIpcSession = NIL_RTLOCALIPCSESSION;
    }
}


static int intnetR3IfLocalIpcSendReq(PINTNETIFCTXINT pThis, uint32_t uOperation, PSUPVMMR0REQHDR pReqHdr)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pReqHdr, VERR_INVALID_POINTER);
    int const rcRecv = ASMAtomicReadS32(&pThis->rcIpcRecv);
    if (RT_FAILURE(rcRecv))
    {
        intnetR3IfLocalIpcRetireSession(pThis);
        return rcRecv;
    }
    AssertReturn(pThis->hIpcSession != NIL_RTLOCALIPCSESSION, VERR_INVALID_HANDLE);
    AssertReturn(pReqHdr->cbReq >= sizeof(*pReqHdr), VERR_INVALID_PARAMETER);
    AssertReturn(pReqHdr->cbReq <= INTNET_R3_IPC_MAX_REQ, VERR_OUT_OF_RANGE);

    size_t const cbMsg = sizeof(INTNETR3IPCREQHDR) + pReqHdr->cbReq;
    uint8_t *pbMsg = (uint8_t *)RTMemTmpAlloc(cbMsg);
    AssertReturn(pbMsg, VERR_NO_TMP_MEMORY);

    PINTNETR3IPCREQHDR pHdr = (PINTNETR3IPCREQHDR)pbMsg;
    pHdr->u32Magic   = INTNET_R3_IPC_REQ_MAGIC;
    pHdr->u16Version = INTNET_R3_IPC_VERSION;
    pHdr->cbHdr      = sizeof(*pHdr);
    pHdr->cbReq      = pReqHdr->cbReq;
    pHdr->uOperation = uOperation;
    memcpy(pbMsg + sizeof(*pHdr), pReqHdr, pReqHdr->cbReq);

    int rc = RTSemMutexRequest(pThis->hIpcIoMtx, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        int const rcRecvNow = ASMAtomicReadS32(&pThis->rcIpcRecv);
        if (RT_FAILURE(rcRecvNow))
            rc = rcRecvNow;
        else
        {
            rc = RTLocalIpcSessionWrite(pThis->hIpcSession, pbMsg, cbMsg);
            if (RT_SUCCESS(rc))
                rc = RTLocalIpcSessionFlush(pThis->hIpcSession);
        }
        RTSemMutexRelease(pThis->hIpcIoMtx);
        if (RT_FAILURE(rcRecvNow))
            intnetR3IfLocalIpcRetireSession(pThis);
    }
    RTMemTmpFree(pbMsg);
    return rc;
}

static int intnetR3IfLocalIpcReadReply(PINTNETIFCTXINT pThis, void *pvReply, size_t cbReplyMax, size_t *pcbReply,
                                       char *pszShMemName, size_t cbShMemName, size_t *pcbShMem)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    if (pcbReply)
        *pcbReply = 0;
    if (pszShMemName && cbShMemName)
        *pszShMemName = '\0';
    if (pcbShMem)
        *pcbShMem = 0;

    int rcRecv = ASMAtomicReadS32(&pThis->rcIpcRecv);
    if (RT_FAILURE(rcRecv))
    {
        intnetR3IfLocalIpcRetireSession(pThis);
        return rcRecv;
    }

    int rc = RTSemEventWait(pThis->hEvtReply, intnetR3IfLocalIpcGetReplyTimeout());
    if (rc == VERR_TIMEOUT)
    {
        ASMAtomicCmpXchgS32(&pThis->rcIpcRecv, VERR_TIMEOUT, VINF_SUCCESS);
        intnetR3IfLocalIpcRetireSession(pThis);
        return rc;
    }
    if (RT_FAILURE(rc))
        return rc;

    rcRecv = ASMAtomicReadS32(&pThis->rcIpcRecv);
    if (RT_FAILURE(rcRecv))
    {
        intnetR3IfLocalIpcRetireSession(pThis);
        return rcRecv;
    }

    PCINTNETR3IPCREPLYHDR pHdr = &pThis->ReplyHdr;
    AssertReturn(pHdr->cbReply <= cbReplyMax, VERR_BUFFER_OVERFLOW);
    AssertReturn(!pHdr->cbShMemName || (pszShMemName && pHdr->cbShMemName <= cbShMemName), VERR_BUFFER_OVERFLOW);
    AssertReturn(pHdr->cbShMem == (size_t)pHdr->cbShMem, VERR_OUT_OF_RANGE);
    if (pHdr->cbReply)
    {
        AssertPtrReturn(pvReply, VERR_INVALID_POINTER);
        memcpy(pvReply, pThis->abReply, pHdr->cbReply);
    }
    if (pcbReply)
        *pcbReply = pHdr->cbReply;
    if (pHdr->cbShMemName)
        memcpy(pszShMemName, pThis->szReplyShMemName, pHdr->cbShMemName);
    if (pcbShMem)
        *pcbShMem = (size_t)pHdr->cbShMem;
    return pHdr->rc;
}

static int intnetR3IfLocalIpcReadPoke(PINTNETIFCTXINT pThis, uint32_t cMillies)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    int rcRecv = ASMAtomicReadS32(&pThis->rcIpcRecv);
    if (RT_FAILURE(rcRecv))
        return rcRecv;
    int rc = RTSemEventWait(pThis->hEvtRecv, cMillies);
    if (RT_SUCCESS(rc))
    {
        rcRecv = ASMAtomicReadS32(&pThis->rcIpcRecv);
        if (RT_FAILURE(rcRecv))
            return rcRecv;
        rc = ASMAtomicReadS32(&pThis->rcRecvPoke);
    }
    return rc;
}


/** Executes a synchronous local IPC call while the caller owns hIpcCallMtx. */
static int intnetR3IfLocalIpcCallLocked(PINTNETIFCTXINT pThis, uint32_t uOperation, PSUPVMMR0REQHDR pReqHdr)
{
    size_t const cbReq = pReqHdr->cbReq;
    int rc = intnetR3IfLocalIpcSendReq(pThis, uOperation, pReqHdr);
    if (RT_SUCCESS(rc))
    {
        size_t cbReply = 0;
        rc = intnetR3IfLocalIpcReadReply(pThis, pReqHdr, cbReq, &cbReply, NULL, 0, NULL);
        AssertStmt(RT_FAILURE(rc) || cbReply == cbReq, rc = VERR_INVALID_PARAMETER);
    }
    return rc;
}


static bool intnetR3IfLocalIpcIsRingValid(PCINTNETBUF pBuf, INTNETRINGBUF const *pRing,
                                          uint32_t offRing, uint32_t cbRing, uint32_t offExpected)
{
    uint64_t const offStart = (uint64_t)offRing + pRing->offStart;
    uint64_t const offEnd   = (uint64_t)offRing + pRing->offEnd;
    return    offStart == offExpected
           && offEnd == offStart + cbRing
           && offEnd <= pBuf->cbBuf
           && pRing->offReadX >= pRing->offStart
           && pRing->offReadX < pRing->offEnd
           && pRing->offWriteCom >= pRing->offStart
           && pRing->offWriteCom < pRing->offEnd
           && pRing->offWriteInt >= pRing->offStart
           && pRing->offWriteInt < pRing->offEnd
           && RT_ALIGN_32(pRing->offReadX, INTNETHDR_ALIGNMENT) == pRing->offReadX
           && RT_ALIGN_32(pRing->offWriteCom, INTNETHDR_ALIGNMENT) == pRing->offWriteCom
           && RT_ALIGN_32(pRing->offWriteInt, INTNETHDR_ALIGNMENT) == pRing->offWriteInt;
}


static bool intnetR3IfLocalIpcIsBufferValid(PCINTNETBUF pBuf, size_t cbMapped)
{
    if (   pBuf->u32Magic != INTNETBUF_MAGIC
        || pBuf->cbBuf != cbMapped
        || pBuf->cbBuf < sizeof(*pBuf)
        || pBuf->cbRecv < INTNETHDR_ALIGNMENT
        || pBuf->cbSend < INTNETHDR_ALIGNMENT
        || RT_ALIGN_32(pBuf->cbRecv, INTNETRINGBUF_ALIGNMENT) != pBuf->cbRecv
        || RT_ALIGN_32(pBuf->cbSend, INTNETRINGBUF_ALIGNMENT) != pBuf->cbSend)
        return false;

    uint32_t const offRecv = RT_UOFFSETOF(INTNETBUF, Recv);
    uint32_t const offSend = RT_UOFFSETOF(INTNETBUF, Send);
    uint32_t const offRecvStart = RT_ALIGN_32(sizeof(*pBuf), INTNETRINGBUF_ALIGNMENT);
    uint64_t const offSendStart = (uint64_t)offRecvStart + pBuf->cbRecv;
    if (offSendStart > UINT32_MAX)
        return false;
    return    intnetR3IfLocalIpcIsRingValid(pBuf, &pBuf->Recv, offRecv, pBuf->cbRecv, offRecvStart)
           && intnetR3IfLocalIpcIsRingValid(pBuf, &pBuf->Send, offSend, pBuf->cbSend, (uint32_t)offSendStart);
}


static void intnetR3IfLocalIpcDisconnect(PINTNETIFCTXINT pThis)
{
    intnetR3IfLocalIpcRetireSession(pThis);
    if (pThis->hEvtReply != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThis->hEvtReply);
        pThis->hEvtReply = NIL_RTSEMEVENT;
    }
    if (pThis->hIpcCallMtx != NIL_RTSEMMUTEX)
    {
        RTSemMutexDestroy(pThis->hIpcCallMtx);
        pThis->hIpcCallMtx = NIL_RTSEMMUTEX;
    }
    if (pThis->hIpcIoMtx != NIL_RTSEMMUTEX)
    {
        RTSemMutexDestroy(pThis->hIpcIoMtx);
        pThis->hIpcIoMtx = NIL_RTSEMMUTEX;
    }
}


static int intnetR3IfLocalIpcConnect(PINTNETIFCTXINT pThis, const char *pszService)
{
    int rc = RTSemEventCreate(&pThis->hEvtReply);
    if (RT_SUCCESS(rc))
        rc = RTSemMutexCreate(&pThis->hIpcCallMtx);
    if (RT_SUCCESS(rc))
        rc = RTSemMutexCreate(&pThis->hIpcIoMtx);
    if (RT_SUCCESS(rc))
    {
        rc = RTLocalIpcSessionConnect(&pThis->hIpcSession, pszService,
                                      RTLOCALIPC_C_FLAGS_ALLOW_IDENTIFICATION
                                    | RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER);
# ifndef VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH
        if (intnetR3IfLocalIpcIsServiceAbsent(rc))
        {
            int const rcStart = intnetR3IfLocalIpcStartService();
            if (RT_SUCCESS(rcStart))
            {
                uint64_t const msStart = RTTimeMilliTS();
                do
                {
                    RTThreadSleep(10);
                    rc = RTLocalIpcSessionConnect(&pThis->hIpcSession, pszService,
                                                  RTLOCALIPC_C_FLAGS_ALLOW_IDENTIFICATION
                                                | RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER);
                } while (   (   intnetR3IfLocalIpcIsServiceAbsent(rc)
                             || rc == VERR_ACCESS_DENIED /* Endpoint security setup may still be completing. */)
                         && RTTimeMilliTS() - msStart < RT_MS_5SEC);
            }
            else
                rc = rcStart;
        }
# endif
    }
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionVerifySameUser(pThis->hIpcSession);
# ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    if (rc == VERR_NOT_SUPPORTED)
        rc = VINF_SUCCESS;
# endif
    if (RT_SUCCESS(rc))
        rc = RTThreadCreate(&pThis->hIpcRecvThread, intnetR3IfLocalIpcRecvThread, pThis, 0 /*cbStack*/,
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "IntNetRx");
    if (RT_FAILURE(rc))
        intnetR3IfLocalIpcDisconnect(pThis);
    return rc;
}
#endif /* INTNETIF_WITH_R3_SVC_LOCALIPC */

/**
 * Calls the internal networking switch service living in either R0 or in another R3 process.
 *
 * @returns VBox status code.
 * @param   pThis           The internal network driver instance data.
 * @param   uOperation      The operation to execute.
 * @param   pReqHdr         Pointer to the request header.
 */
static int intnetR3IfCallSvc(PINTNETIFCTXINT pThis, uint32_t uOperation, PSUPVMMR0REQHDR pReqHdr)
{
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
# if defined(INTNETIF_WITH_R3_SVC_XPC)
        size_t const cbReq = pReqHdr->cbReq;
        xpc_object_t hObj = xpc_dictionary_create(NULL, NULL, 0);
        AssertReturn(hObj != NULL, VERR_NO_MEMORY);
        xpc_dictionary_set_uint64(hObj, "req-id", uOperation);
        xpc_dictionary_set_data(hObj, "req", pReqHdr, pReqHdr->cbReq);
        xpc_object_t hObjReply = xpc_connection_send_message_with_reply_sync(pThis->hXpcCon, hObj);
        xpc_release(hObj);

        int rcReq = VERR_INVALID_STATE;
        int rc = intnetR3IfXpcGetReplyStatus(hObjReply, &rcReq);
        if (RT_SUCCESS(rc))
        {
            size_t cbReply = 0;
            const void *pvData = xpc_dictionary_get_data(hObjReply, "reply", &cbReply);
            if (pvData && cbReply == cbReq)
            {
                memcpy(pReqHdr, pvData, cbReq);
                rc = rcReq;
            }
            else
                rc = VERR_INVALID_PARAMETER;
        }
        if (hObjReply)
            xpc_release(hObjReply);
        return rc;
# elif defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
        int rc = RTSemMutexRequest(pThis->hIpcCallMtx, RT_INDEFINITE_WAIT);
        if (RT_SUCCESS(rc))
        {
            rc = intnetR3IfLocalIpcCallLocked(pThis, uOperation, pReqHdr);
            RTSemMutexRelease(pThis->hIpcCallMtx);
        }
        return rc;
# else
        return VERR_SUP_DRIVERLESS;
# endif
    }
    else
#else
        RT_NOREF(pThis);
#endif
        return SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, uOperation, 0, pReqHdr);
}


#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
/**
 * Calls the internal networking switch service living in either R0 or in another R3 process.
 *
 * @returns VBox status code.
 * @param   pThis           The internal network driver instance data.
 * @param   uOperation      The operation to execute.
 * @param   pReqHdr         Pointer to the request header.
 */
static int intnetR3IfCallSvcAsync(PINTNETIFCTXINT pThis, uint32_t uOperation, PSUPVMMR0REQHDR pReqHdr)
{
    if (pThis->fIntNetR3Svc)
    {
# if defined(INTNETIF_WITH_R3_SVC_XPC)
        xpc_object_t hObj = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_uint64(hObj, "req-id", uOperation);
        xpc_dictionary_set_data(hObj, "req", pReqHdr, pReqHdr->cbReq);
        xpc_connection_send_message(pThis->hXpcCon, hObj);
        xpc_release(hObj);
        return VINF_SUCCESS;
# elif defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
        int rc = RTSemMutexRequest(pThis->hIpcCallMtx, RT_INDEFINITE_WAIT);
        if (RT_SUCCESS(rc))
        {
            if (   uOperation == VMMR0_DO_INTNET_IF_WAIT
                && ASMAtomicReadBool(&pThis->fNoMoreWaits))
                rc = VERR_SEM_DESTROYED;
            else
                rc = intnetR3IfLocalIpcSendReq(pThis, uOperation, pReqHdr);
            RTSemMutexRelease(pThis->hIpcCallMtx);
        }
        return rc;
# else
        return VERR_SUP_DRIVERLESS;
# endif
    }
    else
        return SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, uOperation, 0, pReqHdr);
}
#endif


/**
 * Map the ring buffer pointer into this process R3 address space.
 *
 * @returns VBox status code.
 * @param   pThis           The internal network driver instance data.
 */
static int intnetR3IfMapBufferPointers(PINTNETIFCTXINT pThis)
{
    int rc = VINF_SUCCESS;

    INTNETIFGETBUFFERPTRSREQ GetBufferPtrsReq;
    GetBufferPtrsReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    GetBufferPtrsReq.Hdr.cbReq    = sizeof(GetBufferPtrsReq);
    GetBufferPtrsReq.pSession     = pThis->pSupDrvSession;
    GetBufferPtrsReq.hIf          = pThis->hIf;
    GetBufferPtrsReq.pRing3Buf    = NULL;
    GetBufferPtrsReq.pRing0Buf    = NIL_RTR0PTR;

#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
# if defined(INTNETIF_WITH_R3_SVC_XPC)
        xpc_object_t hObj = xpc_dictionary_create(NULL, NULL, 0);
        AssertReturn(hObj != NULL, VERR_NO_MEMORY);
        xpc_dictionary_set_uint64(hObj, "req-id", VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS);
        xpc_dictionary_set_data(hObj, "req", &GetBufferPtrsReq, sizeof(GetBufferPtrsReq));
        xpc_object_t hObjReply = xpc_connection_send_message_with_reply_sync(pThis->hXpcCon, hObj);
        xpc_release(hObj);

        int rcReq = VERR_INVALID_STATE;
        rc = intnetR3IfXpcGetReplyStatus(hObjReply, &rcReq);
        if (RT_SUCCESS(rc))
        {
            rc = rcReq;
            if (RT_SUCCESS(rc))
            {
                /* Get the shared memory object. */
                xpc_object_t hObjShMem = xpc_dictionary_get_value(hObjReply, "buf-ptr");
                if (hObjShMem && xpc_get_type(hObjShMem) == XPC_TYPE_SHMEM)
                {
                    size_t const cbMem = xpc_shmem_map(hObjShMem, (void **)&pThis->pBuf);
                    if (cbMem)
                        pThis->cbBuf = cbMem;
                    else
                        rc = VERR_NO_MEMORY;
                }
                else
                    rc = VERR_INVALID_PARAMETER;
            }
        }
        if (hObjReply)
            xpc_release(hObjReply);
# elif defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
        char szShMemName[INTNET_R3_IPC_MAX_SHMEM_NAME];
        size_t cbReply = 0;
        size_t cbShMem = 0;
        rc = RTSemMutexRequest(pThis->hIpcCallMtx, RT_INDEFINITE_WAIT);
        if (RT_SUCCESS(rc))
        {
            rc = intnetR3IfLocalIpcSendReq(pThis, VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS, &GetBufferPtrsReq.Hdr);
            if (RT_SUCCESS(rc))
                rc = intnetR3IfLocalIpcReadReply(pThis, &GetBufferPtrsReq, sizeof(GetBufferPtrsReq), &cbReply,
                                                 szShMemName, sizeof(szShMemName), &cbShMem);
            RTSemMutexRelease(pThis->hIpcCallMtx);
        }
        if (RT_FAILURE(rc))
            return rc;
        AssertReturn(cbReply == sizeof(GetBufferPtrsReq), VERR_INVALID_PARAMETER);
        AssertReturn(cbShMem >= sizeof(INTNETBUF), VERR_INVALID_PARAMETER);
        AssertReturn(cbShMem <= UINT32_MAX, VERR_OUT_OF_RANGE);
        AssertReturn(szShMemName[0] != '\0', VERR_INVALID_PARAMETER);

        RTSHMEM hShMem = NIL_RTSHMEM;
        rc = RTShMemOpen(&hShMem, szShMemName, RTSHMEM_O_F_READWRITE, 0 /*cbMax*/, 1 /*cMappingsHint*/);
        if (RT_FAILURE(rc))
            return rc;

        size_t cbShMemActual = 0;
        rc = RTShMemQuerySize(hShMem, &cbShMemActual);
        if (RT_SUCCESS(rc) && cbShMemActual < cbShMem)
            rc = VERR_INVALID_PARAMETER;
        if (RT_FAILURE(rc))
        {
            RTShMemClose(hShMem);
            return rc;
        }

        void *pvBuf = NULL;
        rc = RTShMemMapRegion(hShMem, 0 /*off*/, cbShMem, RTSHMEM_MAP_F_READ | RTSHMEM_MAP_F_WRITE, &pvBuf);
        if (RT_FAILURE(rc))
        {
            RTShMemClose(hShMem);
            return rc;
        }

        PINTNETBUF pBuf = (PINTNETBUF)pvBuf;
        if (!intnetR3IfLocalIpcIsBufferValid(pBuf, cbShMem))
        {
            RTShMemUnmapRegion(hShMem, pvBuf);
            RTShMemClose(hShMem);
            return VERR_INVALID_PARAMETER;
        }

        pThis->hShMemBuf = hShMem;
        pThis->pBuf      = pBuf;
        pThis->cbBuf     = cbShMem;
# else
        rc = VERR_SUP_DRIVERLESS;
# endif
    }
    else
#endif
    {
        rc = SUPR3CallVMMR0Ex(NIL_RTR0PTR, NIL_VMCPUID, VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS, 0 /*u64Arg*/, &GetBufferPtrsReq.Hdr);
        if (RT_SUCCESS(rc))
        {
            AssertRelease(RT_VALID_PTR(GetBufferPtrsReq.pRing3Buf));
            pThis->pBuf = GetBufferPtrsReq.pRing3Buf;
        }
    }

    return rc;
}


static void intnetR3IfClose(PINTNETIFCTXINT pThis)
{
    if (pThis->hIf != INTNET_HANDLE_INVALID)
    {
        INTNETIFCLOSEREQ CloseReq;
        CloseReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        CloseReq.Hdr.cbReq    = sizeof(CloseReq);
        CloseReq.pSession     = pThis->pSupDrvSession;
        CloseReq.hIf          = pThis->hIf;

        pThis->hIf = INTNET_HANDLE_INVALID;
        int rc = intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_CLOSE, &CloseReq.Hdr);
#if defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
        AssertMsg(RT_SUCCESS(rc) || RT_FAILURE(ASMAtomicReadS32(&pThis->rcIpcRecv)), ("%Rrc\n", rc));
#else
        AssertRC(rc);
#endif
        RT_NOREF(rc);
    }
}


DECLHIDDEN(int) IntNetR3IfCreate(PINTNETIFCTX phIfCtx, const char *pszNetwork)
{
    return IntNetR3IfCreateEx(phIfCtx, pszNetwork, kIntNetTrunkType_WhateverNone, "",
                              _128K /*cbSend*/, _256K /*cbRecv*/, 0 /*fFlags*/);
}


/** Worker for IntNetR3IfCreateEx and IntNetR3IfCreateExWithRecvEvent. */
static int intnetR3IfCreateExWorker(PINTNETIFCTX phIfCtx, const char *pszNetwork, INTNETTRUNKTYPE enmTrunkType,
                                    const char *pszTrunk, uint32_t cbSend, uint32_t cbRecv, uint32_t fFlags,
                                    RTSEMEVENT hEvtRecv)
{
    AssertPtrReturn(phIfCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pszNetwork, VERR_INVALID_POINTER);
    AssertPtrReturn(pszTrunk, VERR_INVALID_POINTER);
    *phIfCtx = NULL;

#if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    RT_NOREF(hEvtRecv);
#endif

    PSUPDRVSESSION pSession = NIL_RTR0PTR;
    bool fSupInited = false;
#if defined(VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH) || defined(VBOX_INTNET_TESTCASE_FORCE_R3)
    /* For R3-only testcases, avoid SUPR3Init to prevent SCM operations
       and force the driverless R3 service path. */
    int rc = VINF_SUCCESS;
#else
    int rc = SUPR3Init(&pSession);
    if (RT_SUCCESS(rc))
        fSupInited = true;
#endif
    if (RT_SUCCESS(rc))
    {
        PINTNETIFCTXINT pThis = (PINTNETIFCTXINT)RTMemAllocZ(sizeof(*pThis));
        if (RT_LIKELY(pThis))
        {
            pThis->pSupDrvSession = pSession;
            pThis->hIf            = INTNET_HANDLE_INVALID;
            pThis->fSupInited     = fSupInited;
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
            pThis->hEvtRecv       = hEvtRecv;
# if defined(INTNETIF_WITH_R3_SVC_XPC)
            pThis->hEvtXpcCancelled = NIL_RTSEMEVENT;
# elif defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
            pThis->hIpcSession    = NIL_RTLOCALIPCSESSION;
            pThis->hIpcRecvThread = NIL_RTTHREAD;
            pThis->hIpcCallMtx    = NIL_RTSEMMUTEX;
            pThis->hIpcIoMtx      = NIL_RTSEMMUTEX;
            pThis->hEvtReply      = NIL_RTSEMEVENT;
            pThis->hShMemBuf      = NIL_RTSHMEM;
            pThis->rcRecvPoke     = VINF_SUCCESS;
            pThis->rcIpcRecv      = VINF_SUCCESS;
# endif
#endif

            /* Driverless operation needs support for running the internal network switch using IPC. */
#if defined(VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH) || defined(VBOX_INTNET_TESTCASE_FORCE_R3)
            bool const fDriverless = true;
#else
            bool const fDriverless = SUPR3IsDriverless();
#endif
            if (fDriverless)
            {
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
                if (pThis->hEvtRecv == NIL_RTSEMEVENT)
                {
                    rc = RTSemEventCreate(&pThis->hEvtRecv);
                    if (RT_SUCCESS(rc))
                        pThis->fOwnEvtRecv = true;
                }
                if (RT_SUCCESS(rc))
                {
# if defined(INTNETIF_WITH_R3_SVC_XPC)
                    rc = RTSemEventCreate(&pThis->hEvtXpcCancelled);
                    if (RT_SUCCESS(rc))
                    {
                        xpc_connection_t hXpcCon = xpc_connection_create(INTNET_R3_SVC_NAME, NULL);
                        if (hXpcCon)
                        {
                            pThis->hXpcCon = hXpcCon;
                            xpc_connection_set_event_handler(hXpcCon, ^(xpc_object_t hObj) {
                                if (xpc_get_type(hObj) == XPC_TYPE_ERROR)
                                {
                                    if (hObj == XPC_ERROR_CONNECTION_INVALID)
                                    {
                                        int const rc2 = RTSemEventSignal(pThis->hEvtXpcCancelled);
                                        AssertRC(rc2);
                                    }
                                    /** @todo Error handling - reconnecting. */
                                }
                                else
                                    RTSemEventSignal(pThis->hEvtRecv);
                            });
                            xpc_connection_resume(hXpcCon);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
# elif defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
#  ifdef VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH
                    const char *pszService = RTEnvGet("VBOX_INTNET_R3_SVC_NAME");
                    if (!pszService || !*pszService)
                        pszService = INTNET_R3_SVC_NAME;
                    rc = intnetR3IfLocalIpcConnect(pThis, pszService);
#  else
                    char szService[INTNET_R3_IPC_MAX_SERVICE_NAME];
                    rc = intnetR3IfLocalIpcGetServiceName(szService, sizeof(szService));
                    if (RT_SUCCESS(rc))
                        rc = intnetR3IfLocalIpcConnect(pThis, szService);
#  endif
# else
                    rc = VERR_SUP_DRIVERLESS;
# endif
                    if (RT_SUCCESS(rc))
                        pThis->fIntNetR3Svc = true;
                    else
                    {
                        if (pThis->fOwnEvtRecv)
                            RTSemEventDestroy(pThis->hEvtRecv);
                        pThis->hEvtRecv = NIL_RTSEMEVENT;
                        pThis->fOwnEvtRecv = false;
                    }
                }
#else
                rc = VERR_SUP_DRIVERLESS;
#endif
            }
            else
            {
                /* Need to load VMMR0.r0 containing the network switching code. */
                char szPathVMMR0[RTPATH_MAX];

                rc = RTPathExecDir(szPathVMMR0, sizeof(szPathVMMR0));
                if (RT_SUCCESS(rc))
                {
                    rc = RTPathAppend(szPathVMMR0, sizeof(szPathVMMR0), "VMMR0.r0");
                    if (RT_SUCCESS(rc))
                        rc = SUPR3LoadVMM(szPathVMMR0, /* :pErrInfo */ NULL);
                }
            }

            if (RT_SUCCESS(rc))
            {
                /* Open the interface. */
                INTNETOPENREQ OpenReq;
                RT_ZERO(OpenReq);

                OpenReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
                OpenReq.Hdr.cbReq    = sizeof(OpenReq);
                OpenReq.pSession     = pThis->pSupDrvSession;
                OpenReq.enmTrunkType = enmTrunkType;
                OpenReq.fFlags       = fFlags;
                OpenReq.cbSend       = cbSend;
                OpenReq.cbRecv       = cbRecv;
                OpenReq.hIf          = INTNET_HANDLE_INVALID;

                rc = RTStrCopy(OpenReq.szNetwork, sizeof(OpenReq.szNetwork), pszNetwork);
                if (RT_SUCCESS(rc))
                    rc = RTStrCopy(OpenReq.szTrunk, sizeof(OpenReq.szTrunk), pszTrunk);
                if (RT_SUCCESS(rc))
                {
                    rc = intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_OPEN, &OpenReq.Hdr);
                    if (RT_SUCCESS(rc))
                    {
                        pThis->hIf = OpenReq.hIf;

                        rc = intnetR3IfMapBufferPointers(pThis);
                        if (RT_SUCCESS(rc))
                        {
                            *phIfCtx = pThis;
                            return VINF_SUCCESS;
                        }
                    }

                    intnetR3IfClose(pThis);
                }
            }

#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
            if (pThis->fIntNetR3Svc)
            {
# if defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
                if (pThis->pBuf && pThis->hShMemBuf != NIL_RTSHMEM)
                {
                    RTShMemUnmapRegion(pThis->hShMemBuf, pThis->pBuf);
                    pThis->pBuf = NULL;
                    pThis->cbBuf = 0;
                }
                if (pThis->hShMemBuf != NIL_RTSHMEM)
                {
                    RTShMemClose(pThis->hShMemBuf);
                    pThis->hShMemBuf = NIL_RTSHMEM;
                }
                intnetR3IfLocalIpcDisconnect(pThis);
# endif
            }
# if defined(INTNETIF_WITH_R3_SVC_XPC)
            intnetR3IfXpcDisconnect(pThis);
# endif
            if (pThis->fOwnEvtRecv && pThis->hEvtRecv != NIL_RTSEMEVENT)
                RTSemEventDestroy(pThis->hEvtRecv);
#endif
            RTMemFree(pThis);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (fSupInited)
        SUPR3Term();

    return rc;
}


DECLHIDDEN(int) IntNetR3IfCreateEx(PINTNETIFCTX phIfCtx, const char *pszNetwork, INTNETTRUNKTYPE enmTrunkType,
                                   const char *pszTrunk, uint32_t cbSend, uint32_t cbRecv, uint32_t fFlags)
{
    return intnetR3IfCreateExWorker(phIfCtx, pszNetwork, enmTrunkType, pszTrunk, cbSend, cbRecv, fFlags,
                                    NIL_RTSEMEVENT);
}


DECLHIDDEN(int) IntNetR3IfCreateExWithRecvEvent(PINTNETIFCTX phIfCtx, const char *pszNetwork,
                                                INTNETTRUNKTYPE enmTrunkType, const char *pszTrunk,
                                                uint32_t cbSend, uint32_t cbRecv, uint32_t fFlags,
                                                RTSEMEVENT hEvtRecv)
{
    AssertReturn(hEvtRecv != NIL_RTSEMEVENT, VERR_INVALID_HANDLE);
    return intnetR3IfCreateExWorker(phIfCtx, pszNetwork, enmTrunkType, pszTrunk, cbSend, cbRecv, fFlags, hEvtRecv);
}


DECLHIDDEN(int) IntNetR3IfDestroy(INTNETIFCTX hIfCtx)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    intnetR3IfClose(pThis);

#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
# if defined(INTNETIF_WITH_R3_SVC_XPC)
        /* Unmap the shared buffer. */
        munmap(pThis->pBuf, pThis->cbBuf);
        pThis->pBuf = NULL;
        pThis->cbBuf = 0;
# elif defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
        if (pThis->pBuf && pThis->hShMemBuf != NIL_RTSHMEM)
        {
            RTShMemUnmapRegion(pThis->hShMemBuf, pThis->pBuf);
            pThis->pBuf = NULL;
            pThis->cbBuf = 0;
        }
        if (pThis->hShMemBuf != NIL_RTSHMEM)
        {
            RTShMemClose(pThis->hShMemBuf);
            pThis->hShMemBuf = NIL_RTSHMEM;
        }
        intnetR3IfLocalIpcDisconnect(pThis);
# endif
# if defined(INTNETIF_WITH_R3_SVC_XPC)
        intnetR3IfXpcDisconnect(pThis);
# endif
        if (pThis->fOwnEvtRecv)
            RTSemEventDestroy(pThis->hEvtRecv);
        pThis->hEvtRecv       = NIL_RTSEMEVENT;
        pThis->fOwnEvtRecv    = false;
        pThis->fIntNetR3Svc = false;
    }
#endif

    bool const fSupInited = pThis->fSupInited;
    RTMemFree(pThis);
    if (fSupInited)
        SUPR3Term();
    return VINF_SUCCESS;
}


DECLHIDDEN(int) IntNetR3IfQueryHandle(INTNETIFCTX hIfCtx, PINTNETIFHANDLE phIf)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(phIf, VERR_INVALID_POINTER);

    *phIf = pThis->hIf;
    return VINF_SUCCESS;
}


DECLHIDDEN(int) IntNetR3IfQueryBufferPtr(INTNETIFCTX hIfCtx, PINTNETBUF *ppIfBuf)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(ppIfBuf, VERR_INVALID_POINTER);

    *ppIfBuf = pThis->pBuf;
    return VINF_SUCCESS;
}


DECLHIDDEN(int) IntNetR3IfSetActive(INTNETIFCTX hIfCtx, bool fActive)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    INTNETIFSETACTIVEREQ Req;
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq    = sizeof(Req);
    Req.pSession     = pThis->pSupDrvSession;
    Req.hIf          = pThis->hIf;
    Req.fActive      = fActive;
    return intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_SET_ACTIVE, &Req.Hdr);
}


DECLHIDDEN(int) IntNetR3IfSetPromiscuous(INTNETIFCTX hIfCtx, bool fPromiscuous)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    INTNETIFSETPROMISCUOUSMODEREQ Req;
    Req.Hdr.u32Magic    = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq       = sizeof(Req);
    Req.pSession        = pThis->pSupDrvSession;
    Req.hIf             = pThis->hIf;
    Req.fPromiscuous    = fPromiscuous;
    return intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_SET_PROMISCUOUS_MODE, &Req.Hdr);
}


DECLHIDDEN(int) IntNetR3IfSetMacAddress(INTNETIFCTX hIfCtx, PCRTMAC pMac)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pMac, VERR_INVALID_POINTER);

    INTNETIFSETMACADDRESSREQ Req;
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq    = sizeof(Req);
    Req.pSession     = pThis->pSupDrvSession;
    Req.hIf          = pThis->hIf;
    Req.Mac          = *pMac;
    return intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_SET_MAC_ADDRESS, &Req.Hdr);
}


DECLHIDDEN(int) IntNetR3IfSend(INTNETIFCTX hIfCtx)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    INTNETIFSENDREQ Req;
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq    = sizeof(Req);
    Req.pSession     = pThis->pSupDrvSession;
    Req.hIf          = pThis->hIf;
    return intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_SEND, &Req.Hdr);
}


DECLHIDDEN(int) IntNetR3IfWait(INTNETIFCTX hIfCtx, uint32_t cMillies)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc && ASMAtomicReadBool(&pThis->fNoMoreWaits))
        return VERR_SEM_DESTROYED;
#endif

    int rc = VINF_SUCCESS;
#ifdef VBOX_INTNET_TESTCASE_LOCALIPC
    if (g_pTestWaitRaceIf == pThis)
    {
        rc = RTSemEventSignal(g_hTestWaitRaceReached);
        if (RT_SUCCESS(rc))
            rc = RTSemEventWait(g_hTestWaitRaceContinue, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            return rc;
    }
#endif
    INTNETIFWAITREQ WaitReq;
    WaitReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    WaitReq.Hdr.cbReq    = sizeof(WaitReq);
    WaitReq.pSession     = pThis->pSupDrvSession;
    WaitReq.hIf          = pThis->hIf;
    WaitReq.cMillies     = cMillies;
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
        /* Send an asynchronous message. */
        rc = intnetR3IfCallSvcAsync(pThis, VMMR0_DO_INTNET_IF_WAIT, &WaitReq.Hdr);
        if (RT_SUCCESS(rc))
        {
# if defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
            rc = RTSemEventWait(pThis->hEvtRecv, 0 /*cMillies*/);
            if (rc == VERR_TIMEOUT)
                rc = intnetR3IfLocalIpcReadPoke(pThis, cMillies);
# else
            /* Wait on the receive semaphore. */
            rc = RTSemEventWait(pThis->hEvtRecv, cMillies);
# endif
            if (ASMAtomicReadBool(&pThis->fNoMoreWaits))
                rc = VERR_SEM_DESTROYED;
        }
    }
    else
#endif
        rc = intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_WAIT, &WaitReq.Hdr);

    return rc;
}


DECLHIDDEN(int) IntNetR3IfWaitAbort(INTNETIFCTX hIfCtx)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    INTNETIFABORTWAITREQ AbortWaitReq;
    AbortWaitReq.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    AbortWaitReq.Hdr.cbReq    = sizeof(AbortWaitReq);
    AbortWaitReq.pSession     = pThis->pSupDrvSession;
    AbortWaitReq.hIf          = pThis->hIf;
    AbortWaitReq.fNoMoreWaits = true;
#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3)
    if (pThis->fIntNetR3Svc)
    {
# if defined(INTNETIF_WITH_R3_SVC_LOCALIPC)
        int rc = RTSemMutexRequest(pThis->hIpcCallMtx, RT_INDEFINITE_WAIT);
        if (RT_SUCCESS(rc))
        {
            ASMAtomicWriteBool(&pThis->fNoMoreWaits, true);
            RTSemEventSignal(pThis->hEvtRecv);
            rc = intnetR3IfLocalIpcCallLocked(pThis, VMMR0_DO_INTNET_IF_ABORT_WAIT, &AbortWaitReq.Hdr);
            RTSemMutexRelease(pThis->hIpcCallMtx);
        }
        return rc;
# else
        ASMAtomicWriteBool(&pThis->fNoMoreWaits, true);
        RTSemEventSignal(pThis->hEvtRecv);
# endif
    }
#endif
    return intnetR3IfCallSvc(pThis, VMMR0_DO_INTNET_IF_ABORT_WAIT, &AbortWaitReq.Hdr);
}


DECLHIDDEN(int) IntNetR3IfPumpPkts(INTNETIFCTX hIfCtx, PFNINPUT pfnInput, void *pvUser,
                                      PFNINPUTGSO pfnInputGso, void *pvUserGso)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pfnInput, VERR_INVALID_POINTER);

    int rc;
    for (;;)
    {
        rc = IntNetR3IfWait(hIfCtx, RT_INDEFINITE_WAIT);
        if (RT_SUCCESS(rc) || rc == VERR_INTERRUPTED || rc == VERR_TIMEOUT)
        {
            PCINTNETHDR pHdr = IntNetRingGetNextFrameToRead(&pThis->pBuf->Recv);
            while (pHdr)
            {
                const uint8_t u8Type = pHdr->u8Type;
                void *pvSegFrame;
                uint32_t cbSegFrame;

                if (u8Type == INTNETHDR_TYPE_FRAME)
                {
                    pvSegFrame = IntNetHdrGetFramePtr(pHdr, pThis->pBuf);
                    cbSegFrame = pHdr->cbFrame;

                    /* pass the frame to the user callback */
                    pfnInput(pvUser, pvSegFrame, cbSegFrame);
                }
                else if (u8Type == INTNETHDR_TYPE_GSO)
                {
                    size_t cbGso = pHdr->cbFrame;
                    size_t cbFrame = cbGso - sizeof(PDMNETWORKGSO);

                    PCPDMNETWORKGSO pcGso = IntNetHdrGetGsoContext(pHdr, pThis->pBuf);
                    if (PDMNetGsoIsValid(pcGso, cbGso, cbFrame))
                    {
                        if (pfnInputGso != NULL)
                        {
                            /* pass the frame to the user GSO input callback if set */
                            pfnInputGso(pvUserGso, pcGso, (uint32_t)cbFrame);
                        }
                        else
                        {
                            const uint32_t cSegs = PDMNetGsoCalcSegmentCount(pcGso, cbFrame);
                            for (uint32_t i = 0; i < cSegs; ++i)
                            {
                                uint8_t abHdrScratch[256];
                                pvSegFrame = PDMNetGsoCarveSegmentQD(pcGso, (uint8_t *)(pcGso + 1), cbFrame,
                                                                     abHdrScratch,
                                                                     i, cSegs,
                                                                     &cbSegFrame);

                                /* pass carved frames to the user input callback */
                                pfnInput(pvUser, pvSegFrame, (uint32_t)cbSegFrame);
                            }
                        }
                    }
                }

                /* advance to the next input frame */
                IntNetRingSkipFrame(&pThis->pBuf->Recv);
                pHdr = IntNetRingGetNextFrameToRead(&pThis->pBuf->Recv);
            }
        }
        else
            break;
    }
    return rc;
}


DECLHIDDEN(int) IntNetR3IfQueryOutputFrame(INTNETIFCTX hIfCtx, uint32_t cbFrame, PINTNETFRAME pFrame)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    return IntNetRingAllocateFrame(&pThis->pBuf->Send, cbFrame, &pFrame->pHdr, &pFrame->pvFrame);
}


DECLHIDDEN(int) IntNetR3IfOutputFrameCommit(INTNETIFCTX hIfCtx, PCINTNETFRAME pFrame)
{
    PINTNETIFCTXINT pThis = hIfCtx;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    IntNetRingCommitFrame(&pThis->pBuf->Send, pFrame->pHdr);
    return IntNetR3IfSend(hIfCtx);
}
