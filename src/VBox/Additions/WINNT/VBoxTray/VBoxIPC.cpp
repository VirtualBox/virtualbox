/* $Id: VBoxIPC.cpp 110099 2025-07-03 08:29:36Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxIPC - IPC thread, acts as a (purely) local IPC server.
 *           Multiple sessions are supported, whereas every session
 *           has its own thread for processing requests.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <iprt/list.h>
#include <iprt/localipc.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/win/windows.h>

#include "VBoxTray.h"
#include "VBoxTrayInternal.h"
#include "VBoxTrayMsg.h"
#include "VBoxHelpers.h"
#include "VBoxIPC.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * IPC context data.
 */
typedef struct VBOXIPCCONTEXT
{
    /** Pointer to the service environment. */
    const VBOXTRAYSVCENV      *pEnv;
    /** Handle for the local IPC server. */
    RTLOCALIPCSERVER           hServer;
    /** Critical section serializing access to the session list, the state,
     * the response event, the session event, and the thread event. */
    RTCRITSECT                 CritSect;
    /** List of all active IPC sessions. */
    RTLISTANCHOR               SessionList;

} VBOXIPCCONTEXT, *PVBOXIPCCONTEXT;

/** Function pointer for GetLastInputInfo(). */
typedef BOOL (WINAPI *PFNGETLASTINPUTINFO)(PLASTINPUTINFO);

/**
 * IPC per-session thread data.
 */
typedef struct VBOXIPCSESSION
{
    /** The list node required to be part of the
     *  IPC session list. */
    RTLISTNODE                          Node;
    /** Pointer to the IPC context data. */
    PVBOXIPCCONTEXT volatile            pCtx;
    /** The local ipc client handle. */
    RTLOCALIPCSESSION volatile          hSession;
    /** Indicate that the thread should terminate ASAP. */
    bool volatile                       fTerminate;
    /** The thread handle. */
    RTTHREAD                            hThread;

} VBOXIPCSESSION, *PVBOXIPCSESSION;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static VBOXIPCCONTEXT       g_Ctx = { NULL, NIL_RTLOCALIPCSERVER };
static PFNGETLASTINPUTINFO  g_pfnGetLastInputInfo = NULL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int vboxIPCSessionStop(PVBOXIPCSESSION pSession);



/**
 * Handles VBOXTRAYIPCMSGTYPE_RESTART.
 */
static int vboxIPCHandleVBoxTrayRestart(PVBOXIPCSESSION pSession, PVBOXTRAYIPCHEADER pHdr)
{
    RT_NOREF(pSession, pHdr);

    /** @todo Not implemented yet; don't return an error here. */
    return VINF_SUCCESS;
}

/**
 * Handles VBOXTRAYIPCMSGTYPE_SHOW_BALLOON_MSG.
 */
static int vboxIPCHandleShowBalloonMsg(PVBOXIPCSESSION pSession, PVBOXTRAYIPCHEADER pHdr)
{
    /*
     * Unmarshal and validate the data.
     */
    union
    {
        uint8_t                             abBuf[_4K];
        VBOXTRAYIPCMSG_SHOW_BALLOON_MSG_T   s;
    } Payload;
    AssertReturn(pHdr->cbPayload >= RT_UOFFSETOF_DYN(VBOXTRAYIPCMSG_SHOW_BALLOON_MSG_T, szzStrings[2]), VERR_INVALID_PARAMETER);
    AssertReturn(pHdr->cbPayload < sizeof(Payload), VERR_BUFFER_OVERFLOW);

    int rc = RTLocalIpcSessionRead(pSession->hSession, &Payload, pHdr->cbPayload, NULL /*pcbRead - exact, blocking*/);
    if (RT_FAILURE(rc))
        return rc;

    /* String lengths: */
    AssertReturn(   Payload.s.cchMsg + 1 + Payload.s.cchTitle + 1 + RT_UOFFSETOF(VBOXTRAYIPCMSG_SHOW_BALLOON_MSG_T, szzStrings)
                 <= pHdr->cbPayload, VERR_INVALID_PARAMETER);

    /* Message text: */
    const char *pszMsg   = Payload.s.szzStrings;
    rc = RTStrValidateEncodingEx(pszMsg, Payload.s.cchMsg + 1,
                                 RTSTR_VALIDATE_ENCODING_EXACT_LENGTH | RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
    AssertRCReturn(rc, rc);

    /* Title text: */
    const char *pszTitle = &Payload.s.szzStrings[Payload.s.cchMsg + 1];
    rc = RTStrValidateEncodingEx(pszMsg, Payload.s.cchTitle + 1,
                                 RTSTR_VALIDATE_ENCODING_EXACT_LENGTH | RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
    AssertRCReturn(rc, rc);

    /* Type/dwInfoFlags: */
    AssertReturn(   Payload.s.uType == NIIF_NONE
                 || Payload.s.uType == NIIF_INFO
                 || Payload.s.uType == NIIF_WARNING
                 || Payload.s.uType == NIIF_ERROR,
                 VERR_WRONG_TYPE);

    /* Timeout: */
    if (!Payload.s.cMsTimeout)
        Payload.s.cMsTimeout = RT_MS_5SEC;
    AssertStmt(Payload.s.cMsTimeout >= RT_MS_1SEC, Payload.s.cMsTimeout = RT_MS_1SEC);
    AssertStmt(Payload.s.cMsTimeout <= RT_MS_1MIN, Payload.s.cMsTimeout = RT_MS_1MIN);

    /*
     * Showing the balloon tooltip is not critical.
     */
    VBoxTrayHlpShowBalloonTipEx(g_hInstance, g_hwndToolWindow, ID_TRAYICON,
                                pszMsg, pszTitle, Payload.s.cMsTimeout, Payload.s.uType);

    return VINF_SUCCESS;
}

/**
 * Handles VBOXTRAYIPCMSGTYPE_USER_LAST_INPUT.
 */
static int vboxIPCHandleUserLastInput(PVBOXIPCSESSION pSession, PVBOXTRAYIPCHEADER pHdr)
{
    RT_NOREF(pHdr);

    int rc = VINF_SUCCESS;
    VBOXTRAYIPCREPLY_USER_LAST_INPUT_T Reply = { UINT32_MAX };
    if (g_pfnGetLastInputInfo)
    {
        /* Note: This only works up to 49.7 days (= 2^32, 32-bit counter) since Windows was started. */
        LASTINPUTINFO LastInput;
        LastInput.cbSize = sizeof(LastInput);
        if (g_pfnGetLastInputInfo(&LastInput))
            Reply.cSecSinceLastInput = (GetTickCount() - LastInput.dwTime) / 1000;
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }

    int rc2 = RTLocalIpcSessionWrite(pSession->hSession, &Reply, sizeof(Reply));
    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnPreInit}
 */
static DECLCALLBACK(int) vbtrIPCPreInit(void)
{
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnOption}
 */
static DECLCALLBACK(int) vbtrIPCOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    RT_NOREF(ppszShort, argc, argv, pi);

    return -1;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnInit}
 */
DECLCALLBACK(int) vbtrIPCInit(const PVBOXTRAYSVCENV pEnv, void **ppvInstance)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);
    AssertPtrReturn(ppvInstance, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    PVBOXIPCCONTEXT pCtx = &g_Ctx; /* Only one instance at the moment. */
    AssertPtr(pCtx);

    int rc = RTCritSectInit(&pCtx->CritSect);
    if (RT_SUCCESS(rc))
    {
        char szPipeName[512 + sizeof(VBOXTRAY_IPC_PIPE_PREFIX)];
        memcpy(szPipeName, VBOXTRAY_IPC_PIPE_PREFIX, sizeof(VBOXTRAY_IPC_PIPE_PREFIX));
        rc = RTProcQueryUsername(NIL_RTPROCESS,
                                 &szPipeName[sizeof(VBOXTRAY_IPC_PIPE_PREFIX) - 1],
                                 sizeof(szPipeName) - sizeof(VBOXTRAY_IPC_PIPE_PREFIX) + 1,
                                 NULL /*pcbUser*/);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = RTLocalIpcServerCreate(&pCtx->hServer, szPipeName, RTLOCALIPC_FLAGS_NATIVE_NAME);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                pCtx->pEnv = pEnv;
                RTListInit(&pCtx->SessionList);

                *ppvInstance = pCtx;

                /* GetLastInputInfo only is available starting at Windows 2000 -- might fail. */
                g_pfnGetLastInputInfo = (PFNGETLASTINPUTINFO)RTLdrGetSystemSymbol("User32.dll", "GetLastInputInfo");

                LogRelFunc(("Local IPC server now running at \"%s\"\n", szPipeName));
                return VINF_SUCCESS;
            }

        }

        RTCritSectDelete(&pCtx->CritSect);
    }

    LogRelFunc(("Creating local IPC server failed with rc=%Rrc\n", rc));
    return rc;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnStop}
 */
DECLCALLBACK(int) vbtrIPCStop(void *pvInstance)
{
    /* Can be NULL if VBoxIPCInit failed. */
    if (!pvInstance)
        return VINF_SUCCESS;
    AssertPtrReturn(pvInstance, VERR_INVALID_POINTER);

     VBoxTrayInfo("IPC: Stopping worker thread ...\n");

    /* Shut down local IPC server. */
    PVBOXIPCCONTEXT pCtx = (PVBOXIPCCONTEXT)pvInstance;
    AssertPtr(pCtx);

    if (pCtx->hServer != NIL_RTLOCALIPCSERVER)
    {
        int rc2 = RTLocalIpcServerCancel(pCtx->hServer);
        if (RT_FAILURE(rc2))
            LogFlowFunc(("Cancelling current listening call failed with rc=%Rrc\n", rc2));
    }

    /* Stop all remaining session threads. */
    int rc = RTCritSectEnter(&pCtx->CritSect);
    if (RT_SUCCESS(rc))
    {
        PVBOXIPCSESSION pSession;
        RTListForEach(&pCtx->SessionList, pSession, VBOXIPCSESSION, Node)
        {
            int rc2 = vboxIPCSessionStop(pSession);
            if (RT_FAILURE(rc2))
            {
                LogFlowFunc(("Stopping IPC session %p failed with rc=%Rrc\n",
                         pSession, rc2));
                /* Keep going. */
            }
        }

        RTCritSectLeave(&pCtx->CritSect);
    }

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnDestroy}
 */
DECLCALLBACK(void) vbtrIPCDestroy(void *pvInstance)
{
    AssertPtrReturnVoid(pvInstance);

    LogFlowFunc(("Destroying pvInstance=%p\n", pvInstance));

    PVBOXIPCCONTEXT pCtx = (PVBOXIPCCONTEXT)pvInstance;
    AssertPtr(pCtx);

    /* Shut down local IPC server. */
    int rc = RTCritSectEnter(&pCtx->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = RTLocalIpcServerDestroy(pCtx->hServer);
        if (RT_FAILURE(rc))
            VBoxTrayError("IPC: Unable to destroy IPC server, rc=%Rrc\n", rc);

        int rc2 = RTCritSectLeave(&pCtx->CritSect);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    VBoxTrayInfo("IPC: Waiting for remaining IPC sessions to shut down ...\n");

    /* Wait for all IPC session threads to shut down. */
    bool fListIsEmpty = true;
    do
    {
        int rc2 = RTCritSectEnter(&pCtx->CritSect);
        if (RT_SUCCESS(rc2))
        {
            fListIsEmpty = RTListIsEmpty(&pCtx->SessionList);
            rc2 = RTCritSectLeave(&pCtx->CritSect);

            if (!fListIsEmpty) /* Don't hog CPU while waiting. */
                RTThreadSleep(100);
        }

        if (RT_FAILURE(rc2))
            break;

    } while (!fListIsEmpty);

    AssertMsg(fListIsEmpty, ("Session thread list is not empty when it should\n"));
    VBoxTrayInfo("IPC: All remaining IPC sessions shut down\n");

    int rc2 = RTCritSectDelete(&pCtx->CritSect);
    if (RT_SUCCESS(rc))
        rc = rc2;

    LogFlowFunc(("Destroyed pvInstance=%p, rc=%Rrc\n", pvInstance, rc));
}

/**
 * Services a client session.
 *
 * @returns VINF_SUCCESS.
 * @param   hThreadSelf     The thread handle.
 * @param   pvSession       Pointer to the session instance data.
 */
static DECLCALLBACK(int) vboxIPCSessionThread(RTTHREAD hThreadSelf, void *pvSession)
{
    RT_NOREF(hThreadSelf);
    PVBOXIPCSESSION pThis = (PVBOXIPCSESSION)pvSession;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    RTLOCALIPCSESSION hSession = pThis->hSession;
    AssertReturn(hSession != NIL_RTLOCALIPCSESSION, VERR_INVALID_PARAMETER);

    VBoxTrayVerbose(1, "IPC: Session thread %#x started\n", hSession);

    int rc = VINF_SUCCESS;

    /*
     * Process client requests until it quits or we're cancelled on termination.
     */
    while (   !ASMAtomicUoReadBool(&pThis->fTerminate)
           && RT_SUCCESS(rc))
    {
        /* The next call will be cancelled via VBoxIPCStop if needed. */
        rc = RTLocalIpcSessionWaitForData(hSession, RT_INDEFINITE_WAIT);
        if (RT_SUCCESS(rc))
        {
            /*
             * Read the message header.
             */
            VBOXTRAYIPCHEADER Hdr = {0};
            rc = RTLocalIpcSessionRead(hSession, &Hdr, sizeof(Hdr), NULL /*pcbRead - exact, blocking*/);
            if (RT_FAILURE(rc))
                break;

            /*
             * Validate the message header.
             *
             * Disconnecting the client if invalid or something we don't grok.
             * Currently all clients are one-shots, so there is no need to get
             * in complicated recovery code if we don't understand one another.
             */
            if (   Hdr.uMagic   != VBOXTRAY_IPC_HDR_MAGIC
                || Hdr.uVersion != VBOXTRAY_IPC_HDR_VERSION)
            {
                VBoxTrayVerbose(1, "IPC: Session %#x: Invalid header magic/version: %#x, %#x, %#x, %#x\n",
                                hSession, Hdr.uMagic, Hdr.uVersion, Hdr.enmMsgType, Hdr.cbPayload);
                rc = VERR_INVALID_MAGIC;
                break;
            }
            if (Hdr.cbPayload > VBOXTRAY_IPC_MAX_PAYLOAD)
            {
                VBoxTrayVerbose(1, "IPC: Session %#x: Payload too big: %#x, %#x, %#x, %#x - max %#x\n",
                                hSession, Hdr.uMagic, Hdr.uVersion, Hdr.enmMsgType, Hdr.cbPayload, VBOXTRAY_IPC_MAX_PAYLOAD);
                rc = VERR_TOO_MUCH_DATA;
                break;
            }
            if (   Hdr.enmMsgType <= VBOXTRAYIPCMSGTYPE_INVALID
                || Hdr.enmMsgType >= VBOXTRAYIPCMSGTYPE_END)
            {
                VBoxTrayVerbose(1, "IPC: Session %#x: Unknown message: %#x, %#x, %#x, %#x\n",
                                hSession, Hdr.uMagic, Hdr.uVersion, Hdr.enmMsgType, Hdr.cbPayload);
                rc = VERR_INVALID_FUNCTION;
                break;
            }

            /*
             * Handle the message.
             */
            switch (Hdr.enmMsgType)
            {
                case VBOXTRAYIPCMSGTYPE_RESTART:
                    rc = vboxIPCHandleVBoxTrayRestart(pThis, &Hdr);
                    break;

                case VBOXTRAYIPCMSGTYPE_SHOW_BALLOON_MSG:
                    rc = vboxIPCHandleShowBalloonMsg(pThis, &Hdr);
                    break;

                case VBOXTRAYIPCMSGTYPE_USER_LAST_INPUT:
                    rc = vboxIPCHandleUserLastInput(pThis, &Hdr);
                    break;

                default:
                    AssertFailedBreakStmt(rc = VERR_IPE_NOT_REACHED_DEFAULT_CASE);
            }
            if (RT_FAILURE(rc))
                VBoxTrayVerbose(1, "IPC: Session %#x: Handling command %RU32 failed with rc=%Rrc\n",
                                hSession, Hdr.enmMsgType, rc);
        }
        else if (rc == VERR_CANCELLED)
        {
            VBoxTrayVerbose(1, "IPC: Session %#x: Waiting for data cancelled\n", hSession);
            rc = VINF_SUCCESS;
            break;
        }
        else
            VBoxTrayError("IPC: Session %#x: Waiting for session data failed with rc=%Rrc\n", hSession, rc);
    }

    VBoxTrayVerbose(1, "IPC: Session thread %#x ended with rc=%Rrc\n", hSession, rc);

    /*
     * Close the session.
     */
    int rc2 = RTLocalIpcSessionClose(hSession);
    if (RT_FAILURE(rc2))
        VBoxTrayError("IPC: Failed closing session %#x, rc=%Rrc\n", hSession, rc);

    /*
     * Clean up the session.
     */
    PVBOXIPCCONTEXT pCtx = ASMAtomicReadPtrT(&pThis->pCtx, PVBOXIPCCONTEXT);
    AssertMsg(pCtx, ("Session %#x: No context found\n", hSession));
    rc2 = RTCritSectEnter(&pCtx->CritSect);
    if (RT_SUCCESS(rc2))
    {
        /* Remove this session from the session list. */
        RTListNodeRemove(&pThis->Node);

        rc2 = RTCritSectLeave(&pCtx->CritSect);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    VBoxTrayVerbose(1, "IPC: Session thread %#x terminated with rc=%Rrc\n", hSession, rc);

    RTMemFree(pThis);
    pThis = NULL;

    return rc;
}

static int vboxIPCSessionCreate(PVBOXIPCCONTEXT pCtx, RTLOCALIPCSESSION hSession)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(hSession != NIL_RTLOCALIPCSESSION, VERR_INVALID_PARAMETER);

    int rc = RTCritSectEnter(&pCtx->CritSect);
    if (RT_SUCCESS(rc))
    {
        PVBOXIPCSESSION pSession = (PVBOXIPCSESSION)RTMemAllocZ(sizeof(VBOXIPCSESSION));
        if (pSession)
        {
            pSession->pCtx       = pCtx;
            pSession->hSession   = hSession;
            pSession->fTerminate = false;
            pSession->hThread    = NIL_RTTHREAD;

            /* Start IPC session thread. */
            LogFlowFunc(("Creating thread for session %p ...\n", pSession));
            rc = RTThreadCreate(&pSession->hThread, vboxIPCSessionThread,
                                pSession /* pvUser */, 0 /* Default stack size */,
                                RTTHREADTYPE_DEFAULT, 0 /* Flags */, "IPCSESSION");
            if (RT_SUCCESS(rc))
            {
                /* Add session thread to session IPC list. */
                RTListAppend(&pCtx->SessionList, &pSession->Node);
            }
            else
            {
                int rc2 = RTLocalIpcSessionClose(hSession);
                if (RT_FAILURE(rc2))
                    VBoxTrayError("IPC: Failed closing client session %#x with rc=%Rrc\n", hSession, rc2);
                RTMemFree(pSession);
            }
        }
        else
            rc = VERR_NO_MEMORY;

        int rc2 = RTCritSectLeave(&pCtx->CritSect);
        AssertRC(rc2);
    }

    return rc;
}

static int vboxIPCSessionStop(PVBOXIPCSESSION pSession)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    ASMAtomicWriteBool(&pSession->fTerminate, true);

    VBoxTrayInfo("IPC: Stopping worker thread ...\n");

    RTLOCALIPCSESSION hSession;
    ASMAtomicXchgHandle(&pSession->hSession, NIL_RTLOCALIPCSESSION, &hSession);
    if (hSession)
        return RTLocalIpcSessionClose(hSession);

    return VINF_SUCCESS;
}

/**
 * Thread worker for the IPC handling code.
 */
DECLCALLBACK(int) vbtrIPCWorker(void *pvInstance, bool volatile *pfShutdown)
{
    AssertPtr(pvInstance);
    LogFlowFunc(("pvInstance=%p\n", pvInstance));

    /*
     * Tell the control thread that it can continue
     * spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    VBoxTrayInfo("IPC: Worker thread started\n");

    PVBOXIPCCONTEXT pCtx = (PVBOXIPCCONTEXT)pvInstance;
    AssertPtr(pCtx);

    int rc;

    for (;;)
    {
        RTLOCALIPCSESSION hClientSession = NIL_RTLOCALIPCSESSION;
        rc = RTLocalIpcServerListen(pCtx->hServer, &hClientSession);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_CANCELLED)
            {
                VBoxTrayInfo("IPC: Worker thread cancelled\n");
                rc = VINF_SUCCESS;
                break;
            }
            else
                VBoxTrayError("IPC: Listening failed with rc=%Rrc\n", rc);
        }
        else
        {
            VBoxTrayVerbose(1, "IPC: New client connected with session %#x\n", hClientSession);

            rc = vboxIPCSessionCreate(pCtx, hClientSession);
            if (RT_FAILURE(rc))
            {
                VBoxTrayError("IPC: Creating session %#x failed with rc=%Rrc\n", hClientSession, rc);
                /* Keep going. */
            }
        }

        if (*pfShutdown)
            break;

        /* Back off a little to not hog the CPU too much. */
        RTThreadSleep(100 /* ms */);
    }

    VBoxTrayInfo("IPC: Worker thread stopped with %Rrc\n", rc);
    return rc;
}

/**
 * The service description.
 */
VBOXTRAYSVCDESC g_SvcDescIPC =
{
    /* pszName. */
    "IPC",
    /* pszDescription. */
    "Inter-Process Communication",
    /* pszUsage. */
    NULL,
    /* pszOptions. */
    NULL,
    /* methods */
    vbtrIPCPreInit,
    vbtrIPCOption,
    vbtrIPCInit,
    vbtrIPCWorker,
    vbtrIPCStop,
    vbtrIPCDestroy
};
