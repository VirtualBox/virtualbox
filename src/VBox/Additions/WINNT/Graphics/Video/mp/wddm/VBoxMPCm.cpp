/* $Id: VBoxMPCm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox WDDM Miniport driver
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include "VBoxMPWddm.h"

typedef struct VBOXVIDEOCM_CMD_DR
{
    LIST_ENTRY QueueList;
    PVBOXVIDEOCM_CTX pContext;
    uint32_t cbMaxCmdSize;
    volatile uint32_t cRefs;

    VBOXVIDEOCM_CMD_HDR CmdHdr;
} VBOXVIDEOCM_CMD_DR, *PVBOXVIDEOCM_CMD_DR;

typedef enum
{
    VBOXVIDEOCM_CMD_CTL_KM_TYPE_POST_INVOKE = 1,
    VBOXVIDEOCM_CMD_CTL_KM_TYPE_PRE_INVOKE,
    VBOXVIDEOCM_CMD_CTL_KM_TYPE_DUMMY_32BIT = 0x7fffffff
} VBOXVIDEOCM_CMD_CTL_KM_TYPE;

typedef DECLCALLBACKTYPE(VOID, FNVBOXVIDEOCM_CMD_CB,(PVBOXVIDEOCM_CTX pContext, struct VBOXVIDEOCM_CMD_CTL_KM *pCmd,
                                                     PVOID pvContext));
typedef FNVBOXVIDEOCM_CMD_CB *PFNVBOXVIDEOCM_CMD_CB;

typedef struct VBOXVIDEOCM_CMD_CTL_KM
{
    VBOXVIDEOCM_CMD_CTL_KM_TYPE enmType;
    uint32_t u32Reserved;
    PFNVBOXVIDEOCM_CMD_CB pfnCb;
    PVOID pvCb;
} VBOXVIDEOCM_CMD_CTL_KM, *PVBOXVIDEOCM_CMD_CTL_KM;

AssertCompile(VBOXWDDM_ROUNDBOUND(RT_OFFSETOF(VBOXVIDEOCM_CMD_DR, CmdHdr), 8) == RT_OFFSETOF(VBOXVIDEOCM_CMD_DR, CmdHdr));

#define VBOXVIDEOCM_HEADER_SIZE() (VBOXWDDM_ROUNDBOUND(sizeof (VBOXVIDEOCM_CMD_DR), 8))
#define VBOXVIDEOCM_SIZE_FROMBODYSIZE(_s) (VBOXVIDEOCM_HEADER_SIZE() + (_s))
//#define VBOXVIDEOCM_SIZE(_t) (VBOXVIDEOCM_SIZE_FROMBODYSIZE(sizeof (_t)))
#define VBOXVIDEOCM_BODY(_pCmd, _t) ( (_t*)(((uint8_t*)(_pCmd)) + VBOXVIDEOCM_HEADER_SIZE()) )
#define VBOXVIDEOCM_HEAD(_pCmd) ( (PVBOXVIDEOCM_CMD_DR)(((uint8_t*)(_pCmd)) - VBOXVIDEOCM_HEADER_SIZE()) )

#define VBOXVIDEOCM_SENDSIZE_FROMBODYSIZE(_s) ( VBOXVIDEOCM_SIZE_FROMBODYSIZE(_s) - RT_OFFSETOF(VBOXVIDEOCM_CMD_DR, CmdHdr))

//#define VBOXVIDEOCM_BODY_FIELD_OFFSET(_ot, _t, _f) ( (_ot)( VBOXVIDEOCM_BODY(0, uint8_t) + RT_OFFSETOF(_t, _f) ) )

typedef struct VBOXVIDEOCM_SESSION
{
    /* contexts in this session */
    LIST_ENTRY QueueEntry;
    /* contexts in this session */
    LIST_ENTRY ContextList;
    /* commands list  */
    LIST_ENTRY CommandsList;
    /* post process commands list  */
    LIST_ENTRY PpCommandsList;
    /* event used to notify UMD about pending commands */
    PKEVENT pUmEvent;
    /* sync lock */
    KSPIN_LOCK SynchLock;
    /* indicates whether event signaling is needed on cmd add */
    bool bEventNeeded;
} VBOXVIDEOCM_SESSION, *PVBOXVIDEOCM_SESSION;

#define VBOXCMENTRY_2_CMD(_pE) ((PVBOXVIDEOCM_CMD_DR)((uint8_t*)(_pE) - RT_UOFFSETOF(VBOXVIDEOCM_CMD_DR, QueueList)))

void* vboxVideoCmCmdReinitForContext(void *pvCmd, PVBOXVIDEOCM_CTX pContext)
{
    PVBOXVIDEOCM_CMD_DR pHdr = VBOXVIDEOCM_HEAD(pvCmd);
    pHdr->pContext = pContext;
    pHdr->CmdHdr.u64UmData = pContext->u64UmData;
    return pvCmd;
}

void* vboxVideoCmCmdCreate(PVBOXVIDEOCM_CTX pContext, uint32_t cbSize)
{
    Assert(cbSize);
    if (!cbSize)
        return NULL;

    Assert(VBOXWDDM_ROUNDBOUND(cbSize, 8) == cbSize);
    cbSize = VBOXWDDM_ROUNDBOUND(cbSize, 8);

    Assert(pContext->pSession);
    if (!pContext->pSession)
        return NULL;

    uint32_t cbCmd = VBOXVIDEOCM_SIZE_FROMBODYSIZE(cbSize);
    PVBOXVIDEOCM_CMD_DR pCmd = (PVBOXVIDEOCM_CMD_DR)vboxWddmMemAllocZero(cbCmd);
    Assert(pCmd);
    if (pCmd)
    {
        InitializeListHead(&pCmd->QueueList);
        pCmd->pContext = pContext;
        pCmd->cbMaxCmdSize = VBOXVIDEOCM_SENDSIZE_FROMBODYSIZE(cbSize);
        pCmd->cRefs = 1;
        pCmd->CmdHdr.u64UmData = pContext->u64UmData;
        pCmd->CmdHdr.cbCmd = pCmd->cbMaxCmdSize;
    }
    return VBOXVIDEOCM_BODY(pCmd, void);
}

static PVBOXVIDEOCM_CMD_CTL_KM vboxVideoCmCmdCreateKm(PVBOXVIDEOCM_CTX pContext, VBOXVIDEOCM_CMD_CTL_KM_TYPE enmType,
        PFNVBOXVIDEOCM_CMD_CB pfnCb, PVOID pvCb,
        uint32_t cbSize)
{
    PVBOXVIDEOCM_CMD_CTL_KM pCmd = (PVBOXVIDEOCM_CMD_CTL_KM)vboxVideoCmCmdCreate(pContext, cbSize + sizeof (*pCmd));
    pCmd->enmType = enmType;
    pCmd->pfnCb = pfnCb;
    pCmd->pvCb = pvCb;
    PVBOXVIDEOCM_CMD_DR pHdr = VBOXVIDEOCM_HEAD(pCmd);
    pHdr->CmdHdr.enmType = VBOXVIDEOCM_CMD_TYPE_CTL_KM;
    return pCmd;
}

static DECLCALLBACK(VOID) vboxVideoCmCmdCbSetEventAndDereference(PVBOXVIDEOCM_CTX pContext, PVBOXVIDEOCM_CMD_CTL_KM pCmd,
                                                                 PVOID pvContext)
{
    RT_NOREF(pContext);
    PKEVENT pEvent = (PKEVENT)pvContext;
    KeSetEvent(pEvent, 0, FALSE);
    ObDereferenceObject(pEvent);
    vboxVideoCmCmdRelease(pCmd);
}

NTSTATUS vboxVideoCmCmdSubmitCompleteEvent(PVBOXVIDEOCM_CTX pContext, PKEVENT pEvent)
{
    Assert(pEvent);
    PVBOXVIDEOCM_CMD_CTL_KM pCmd = vboxVideoCmCmdCreateKm(pContext, VBOXVIDEOCM_CMD_CTL_KM_TYPE_POST_INVOKE,
            vboxVideoCmCmdCbSetEventAndDereference, pEvent, 0);
    if (!pCmd)
    {
        WARN(("vboxVideoCmCmdCreateKm failed"));
        return STATUS_NO_MEMORY;
    }

    vboxVideoCmCmdSubmit(pCmd, VBOXVIDEOCM_SUBMITSIZE_DEFAULT);

    return STATUS_SUCCESS;
}

DECLINLINE(void) vboxVideoCmCmdRetainByHdr(PVBOXVIDEOCM_CMD_DR pHdr)
{
    ASMAtomicIncU32(&pHdr->cRefs);
}

DECLINLINE(void) vboxVideoCmCmdReleaseByHdr(PVBOXVIDEOCM_CMD_DR pHdr)
{
    uint32_t cRefs = ASMAtomicDecU32(&pHdr->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    if (!cRefs)
        vboxWddmMemFree(pHdr);
}

static void vboxVideoCmCmdCancel(PVBOXVIDEOCM_CMD_DR pHdr)
{
    InitializeListHead(&pHdr->QueueList);
    vboxVideoCmCmdReleaseByHdr(pHdr);
}

static void vboxVideoCmCmdPostByHdr(PVBOXVIDEOCM_SESSION pSession, PVBOXVIDEOCM_CMD_DR pHdr, uint32_t cbSize)
{
    bool bSignalEvent = false;
    if (cbSize != VBOXVIDEOCM_SUBMITSIZE_DEFAULT)
    {
        cbSize = VBOXVIDEOCM_SENDSIZE_FROMBODYSIZE(cbSize);
        Assert(cbSize <= pHdr->cbMaxCmdSize);
        pHdr->CmdHdr.cbCmd = cbSize;
    }

    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    InsertHeadList(&pSession->CommandsList, &pHdr->QueueList);
    if (pSession->bEventNeeded)
    {
        pSession->bEventNeeded = false;
        bSignalEvent = true;
    }

    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);

    if (bSignalEvent)
        KeSetEvent(pSession->pUmEvent, 0, FALSE);
}

void vboxVideoCmCmdRetain(void *pvCmd)
{
    PVBOXVIDEOCM_CMD_DR pHdr = VBOXVIDEOCM_HEAD(pvCmd);
    vboxVideoCmCmdRetainByHdr(pHdr);
}

void vboxVideoCmCmdRelease(void *pvCmd)
{
    PVBOXVIDEOCM_CMD_DR pHdr = VBOXVIDEOCM_HEAD(pvCmd);
    vboxVideoCmCmdReleaseByHdr(pHdr);
}

/**
 * @param pvCmd memory buffer returned by vboxVideoCmCmdCreate
 * @param cbSize should be <= cbSize posted to vboxVideoCmCmdCreate on command creation
 */
void vboxVideoCmCmdSubmit(void *pvCmd, uint32_t cbSize)
{
    PVBOXVIDEOCM_CMD_DR pHdr = VBOXVIDEOCM_HEAD(pvCmd);
    vboxVideoCmCmdPostByHdr(pHdr->pContext->pSession, pHdr, cbSize);
}

NTSTATUS vboxVideoCmCmdVisit(PVBOXVIDEOCM_CTX pContext, BOOLEAN bEntireSession, PFNVBOXVIDEOCMCMDVISITOR pfnVisitor,
                             PVOID pvVisitor)
{
    PVBOXVIDEOCM_SESSION pSession = pContext->pSession;
    PLIST_ENTRY pCurEntry = NULL;
    PVBOXVIDEOCM_CMD_DR pHdr;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    pCurEntry = pSession->CommandsList.Blink;
    do
    {
        if (pCurEntry != &pSession->CommandsList)
        {
            pHdr = VBOXCMENTRY_2_CMD(pCurEntry);
            pCurEntry = pHdr->QueueList.Blink;
            if (bEntireSession || pHdr->pContext == pContext)
            {
                if (pHdr->CmdHdr.enmType == VBOXVIDEOCM_CMD_TYPE_UM)
                {
                    void * pvBody = VBOXVIDEOCM_BODY(pHdr, void);
                    UINT fRet = pfnVisitor(pHdr->pContext, pvBody, pHdr->CmdHdr.cbCmd, pvVisitor);
                    if (fRet & VBOXVIDEOCMCMDVISITOR_RETURN_RMCMD)
                    {
                        RemoveEntryList(&pHdr->QueueList);
                    }
                    if ((fRet & VBOXVIDEOCMCMDVISITOR_RETURN_BREAK))
                        break;
                }
                else
                {
                    WARN(("non-um cmd on visit, skipping"));
                }
            }
        }
        else
        {
            break;
        }
    } while (1);


    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);

    return STATUS_SUCCESS;
}

void vboxVideoCmCtxInitEmpty(PVBOXVIDEOCM_CTX pContext)
{
    InitializeListHead(&pContext->SessionEntry);
    pContext->pSession = NULL;
    pContext->u64UmData = 0ULL;
}

static void vboxVideoCmSessionCtxAddLocked(PVBOXVIDEOCM_SESSION pSession, PVBOXVIDEOCM_CTX pContext)
{
    InsertHeadList(&pSession->ContextList, &pContext->SessionEntry);
    pContext->pSession = pSession;
}

void vboxVideoCmSessionCtxAdd(PVBOXVIDEOCM_SESSION pSession, PVBOXVIDEOCM_CTX pContext)
{
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    vboxVideoCmSessionCtxAddLocked(pSession, pContext);

    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);
}

void vboxVideoCmSessionSignalEvent(PVBOXVIDEOCM_SESSION pSession)
{
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    if (pSession->pUmEvent)
        KeSetEvent(pSession->pUmEvent, 0, FALSE);
}

static void vboxVideoCmSessionDestroyLocked(PVBOXVIDEOCM_SESSION pSession)
{
    /* signal event so that user-space client can figure out the context is destroyed
     * in case the context destroyal is caused by Graphics device reset or miniport driver update */
    KeSetEvent(pSession->pUmEvent, 0, FALSE);
    ObDereferenceObject(pSession->pUmEvent);
    Assert(IsListEmpty(&pSession->ContextList));
    Assert(IsListEmpty(&pSession->CommandsList));
    Assert(IsListEmpty(&pSession->PpCommandsList));
    RemoveEntryList(&pSession->QueueEntry);
    vboxWddmMemFree(pSession);
}

static void vboxVideoCmSessionCtxPpList(PVBOXVIDEOCM_CTX pContext, PLIST_ENTRY pHead)
{
    LIST_ENTRY *pCur;
    for (pCur = pHead->Flink; pCur != pHead; pCur = pHead->Flink)
    {
        RemoveEntryList(pCur);
        PVBOXVIDEOCM_CMD_DR pHdr = VBOXCMENTRY_2_CMD(pCur);
        PVBOXVIDEOCM_CMD_CTL_KM pCmd = VBOXVIDEOCM_BODY(pHdr, VBOXVIDEOCM_CMD_CTL_KM);
        pCmd->pfnCb(pContext, pCmd, pCmd->pvCb);
    }
}

static void vboxVideoCmSessionCtxDetachCmdsLocked(PLIST_ENTRY pEntriesHead, PVBOXVIDEOCM_CTX pContext, PLIST_ENTRY pDstHead)
{
    LIST_ENTRY *pCur;
    LIST_ENTRY *pPrev;
    pCur = pEntriesHead->Flink;
    pPrev = pEntriesHead;
    while (pCur != pEntriesHead)
    {
        PVBOXVIDEOCM_CMD_DR pCmd = VBOXCMENTRY_2_CMD(pCur);
        if (pCmd->pContext == pContext)
        {
            RemoveEntryList(pCur);
            InsertTailList(pDstHead, pCur);
            pCur = pPrev;
            /* pPrev - remains unchanged */
        }
        else
        {
            pPrev = pCur;
        }
        pCur = pCur->Flink;
    }
}
/**
 * @return true iff the given session is destroyed
 */
bool vboxVideoCmSessionCtxRemoveLocked(PVBOXVIDEOCM_SESSION pSession, PVBOXVIDEOCM_CTX pContext)
{
    bool bDestroy;
    LIST_ENTRY RemainedList;
    LIST_ENTRY RemainedPpList;
    LIST_ENTRY *pCur;
    InitializeListHead(&RemainedList);
    InitializeListHead(&RemainedPpList);
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    pContext->pSession = NULL;
    RemoveEntryList(&pContext->SessionEntry);
    bDestroy = !!(IsListEmpty(&pSession->ContextList));
    /* ensure there are no commands left for the given context */
    if (bDestroy)
    {
        vboxVideoLeDetach(&pSession->CommandsList, &RemainedList);
        vboxVideoLeDetach(&pSession->PpCommandsList, &RemainedPpList);
    }
    else
    {
        vboxVideoCmSessionCtxDetachCmdsLocked(&pSession->CommandsList, pContext, &RemainedList);
        vboxVideoCmSessionCtxDetachCmdsLocked(&pSession->PpCommandsList, pContext, &RemainedPpList);
    }

    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);

    for (pCur = RemainedList.Flink; pCur != &RemainedList; pCur = RemainedList.Flink)
    {
        RemoveEntryList(pCur);
        PVBOXVIDEOCM_CMD_DR pCmd = VBOXCMENTRY_2_CMD(pCur);
        vboxVideoCmCmdCancel(pCmd);
    }

    vboxVideoCmSessionCtxPpList(pContext, &RemainedPpList);

    if (bDestroy)
    {
        vboxVideoCmSessionDestroyLocked(pSession);
    }

    return bDestroy;
}

/* the session gets destroyed once the last context is removed from it */
NTSTATUS vboxVideoCmSessionCreateLocked(PVBOXVIDEOCM_MGR pMgr, PVBOXVIDEOCM_SESSION *ppSession, PKEVENT pUmEvent,
                                        PVBOXVIDEOCM_CTX pContext)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PVBOXVIDEOCM_SESSION pSession = (PVBOXVIDEOCM_SESSION)vboxWddmMemAllocZero(sizeof (VBOXVIDEOCM_SESSION));
    Assert(pSession);
    if (pSession)
    {
        InitializeListHead(&pSession->ContextList);
        InitializeListHead(&pSession->CommandsList);
        InitializeListHead(&pSession->PpCommandsList);
        pSession->pUmEvent = pUmEvent;
        Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
        KeInitializeSpinLock(&pSession->SynchLock);
        pSession->bEventNeeded = true;
        vboxVideoCmSessionCtxAddLocked(pSession, pContext);
        InsertHeadList(&pMgr->SessionList, &pSession->QueueEntry);
        *ppSession = pSession;
        return STATUS_SUCCESS;
//        vboxWddmMemFree(pSession);
    }
    else
    {
        Status = STATUS_NO_MEMORY;
    }
    return Status;
}

#define VBOXCMENTRY_2_SESSION(_pE) ((PVBOXVIDEOCM_SESSION)((uint8_t*)(_pE) - RT_UOFFSETOF(VBOXVIDEOCM_SESSION, QueueEntry)))

NTSTATUS vboxVideoCmCtxAdd(PVBOXVIDEOCM_MGR pMgr, PVBOXVIDEOCM_CTX pContext, HANDLE hUmEvent, uint64_t u64UmData)
{
    PKEVENT pUmEvent = NULL;
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);
    NTSTATUS Status = ObReferenceObjectByHandle(hUmEvent, EVENT_MODIFY_STATE, *ExEventObjectType, UserMode,
        (PVOID*)&pUmEvent,
        NULL);
    AssertNtStatusSuccess(Status);
    if (Status == STATUS_SUCCESS)
    {
        KIRQL OldIrql;
        KeAcquireSpinLock(&pMgr->SynchLock, &OldIrql);

        bool bFound = false;
        PVBOXVIDEOCM_SESSION pSession = NULL;
        for (PLIST_ENTRY pEntry = pMgr->SessionList.Flink; pEntry != &pMgr->SessionList; pEntry = pEntry->Flink)
        {
            pSession = VBOXCMENTRY_2_SESSION(pEntry);
            if (pSession->pUmEvent == pUmEvent)
            {
                bFound = true;
                break;
            }
        }

        pContext->u64UmData = u64UmData;

        if (!bFound)
        {
            Status = vboxVideoCmSessionCreateLocked(pMgr, &pSession, pUmEvent, pContext);
            AssertNtStatusSuccess(Status);
        }
        else
        {
            /* Status = */vboxVideoCmSessionCtxAdd(pSession, pContext);
            /*AssertNtStatusSuccess(Status);*/
        }

        KeReleaseSpinLock(&pMgr->SynchLock, OldIrql);

        if (Status == STATUS_SUCCESS)
        {
            return STATUS_SUCCESS;
        }

        ObDereferenceObject(pUmEvent);
    }
    return Status;
}

NTSTATUS vboxVideoCmCtxRemove(PVBOXVIDEOCM_MGR pMgr, PVBOXVIDEOCM_CTX pContext)
{
    PVBOXVIDEOCM_SESSION pSession = pContext->pSession;
    if (!pSession)
        return STATUS_SUCCESS;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pMgr->SynchLock, &OldIrql);

    vboxVideoCmSessionCtxRemoveLocked(pSession, pContext);

    KeReleaseSpinLock(&pMgr->SynchLock, OldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS vboxVideoCmInit(PVBOXVIDEOCM_MGR pMgr)
{
    KeInitializeSpinLock(&pMgr->SynchLock);
    InitializeListHead(&pMgr->SessionList);
    return STATUS_SUCCESS;
}

NTSTATUS vboxVideoCmTerm(PVBOXVIDEOCM_MGR pMgr)
{
    RT_NOREF(pMgr);
    Assert(IsListEmpty(&pMgr->SessionList));
    return STATUS_SUCCESS;
}

NTSTATUS vboxVideoCmSignalEvents(PVBOXVIDEOCM_MGR pMgr)
{
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    PVBOXVIDEOCM_SESSION pSession = NULL;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pMgr->SynchLock, &OldIrql);

    for (PLIST_ENTRY pEntry = pMgr->SessionList.Flink; pEntry != &pMgr->SessionList; pEntry = pEntry->Flink)
    {
        pSession = VBOXCMENTRY_2_SESSION(pEntry);
        vboxVideoCmSessionSignalEvent(pSession);
    }

    KeReleaseSpinLock(&pMgr->SynchLock, OldIrql);

    return STATUS_SUCCESS;
}

VOID vboxVideoCmProcessKm(PVBOXVIDEOCM_CTX pContext, PVBOXVIDEOCM_CMD_CTL_KM pCmd)
{
    PVBOXVIDEOCM_SESSION pSession = pContext->pSession;

    switch (pCmd->enmType)
    {
        case VBOXVIDEOCM_CMD_CTL_KM_TYPE_PRE_INVOKE:
        {
            pCmd->pfnCb(pContext, pCmd, pCmd->pvCb);
            break;
        }

        case VBOXVIDEOCM_CMD_CTL_KM_TYPE_POST_INVOKE:
        {
            PVBOXVIDEOCM_CMD_DR pHdr = VBOXVIDEOCM_HEAD(pCmd);
            KIRQL OldIrql;
            KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);
            InsertTailList(&pSession->PpCommandsList, &pHdr->QueueList);
            KeReleaseSpinLock(&pSession->SynchLock, OldIrql);
            break;
        }

        default:
        {
            WARN(("unsupported cmd type %d", pCmd->enmType));
            break;
        }
    }
}

NTSTATUS vboxVideoCmEscape(PVBOXVIDEOCM_CTX pContext, PVBOXDISPIFESCAPE_GETVBOXVIDEOCMCMD pCmd, uint32_t cbCmd)
{
    Assert(cbCmd >= sizeof (VBOXDISPIFESCAPE_GETVBOXVIDEOCMCMD));
    if (cbCmd < sizeof (VBOXDISPIFESCAPE_GETVBOXVIDEOCMCMD))
        return STATUS_BUFFER_TOO_SMALL;

    PVBOXVIDEOCM_SESSION pSession = pContext->pSession;
    PVBOXVIDEOCM_CMD_DR pHdr;
    LIST_ENTRY DetachedList;
    LIST_ENTRY DetachedPpList;
    PLIST_ENTRY pCurEntry = NULL;
    uint32_t cbRemainingCmds = 0;
    uint32_t cbRemainingFirstCmd = 0;
    uint32_t cbData = cbCmd - sizeof (VBOXDISPIFESCAPE_GETVBOXVIDEOCMCMD);
    uint8_t * pvData = ((uint8_t *)pCmd) + sizeof (VBOXDISPIFESCAPE_GETVBOXVIDEOCMCMD);
    bool bDetachMode = true;
    InitializeListHead(&DetachedList);
    InitializeListHead(&DetachedPpList);
//    PVBOXWDDM_GETVBOXVIDEOCMCMD_HDR *pvCmd

    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    vboxVideoCmSessionCtxDetachCmdsLocked(&pSession->PpCommandsList, pContext, &DetachedPpList);

    do
    {
        if (bDetachMode)
        {
            if (!IsListEmpty(&pSession->CommandsList))
            {
                Assert(!pCurEntry);
                pHdr = VBOXCMENTRY_2_CMD(pSession->CommandsList.Blink);
                Assert(pHdr->CmdHdr.cbCmd);
                uint32_t cbUserCmd = pHdr->CmdHdr.enmType == VBOXVIDEOCM_CMD_TYPE_UM ? pHdr->CmdHdr.cbCmd : 0;
                if (cbData >= cbUserCmd)
                {
                    RemoveEntryList(&pHdr->QueueList);
                    InsertHeadList(&DetachedList, &pHdr->QueueList);
                    cbData -= cbUserCmd;
                }
                else
                {
                    Assert(cbUserCmd);
                    cbRemainingFirstCmd = cbUserCmd;
                    cbRemainingCmds = cbUserCmd;
                    pCurEntry = pHdr->QueueList.Blink;
                    bDetachMode = false;
                }
            }
            else
            {
                pSession->bEventNeeded = true;
                break;
            }
        }
        else
        {
            Assert(pCurEntry);
            if (pCurEntry != &pSession->CommandsList)
            {
                pHdr = VBOXCMENTRY_2_CMD(pCurEntry);
                uint32_t cbUserCmd = pHdr->CmdHdr.enmType == VBOXVIDEOCM_CMD_TYPE_UM ? pHdr->CmdHdr.cbCmd : 0;
                Assert(cbRemainingFirstCmd);
                cbRemainingCmds += cbUserCmd;
                pCurEntry = pHdr->QueueList.Blink;
            }
            else
            {
                Assert(cbRemainingFirstCmd);
                Assert(cbRemainingCmds);
                break;
            }
        }
    } while (1);

    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);

    vboxVideoCmSessionCtxPpList(pContext, &DetachedPpList);

    pCmd->Hdr.cbCmdsReturned = 0;
    for (pCurEntry = DetachedList.Blink; pCurEntry != &DetachedList; pCurEntry = DetachedList.Blink)
    {
        pHdr = VBOXCMENTRY_2_CMD(pCurEntry);
        RemoveEntryList(pCurEntry);
        switch (pHdr->CmdHdr.enmType)
        {
            case VBOXVIDEOCM_CMD_TYPE_UM:
            {
                memcpy(pvData, &pHdr->CmdHdr, pHdr->CmdHdr.cbCmd);
                pvData += pHdr->CmdHdr.cbCmd;
                pCmd->Hdr.cbCmdsReturned += pHdr->CmdHdr.cbCmd;
                vboxVideoCmCmdReleaseByHdr(pHdr);
                break;
            }

            case VBOXVIDEOCM_CMD_TYPE_CTL_KM:
            {
                vboxVideoCmProcessKm(pContext, VBOXVIDEOCM_BODY(pHdr, VBOXVIDEOCM_CMD_CTL_KM));
                break;
            }

            default:
            {
                WARN(("unsupported cmd type %d", pHdr->CmdHdr.enmType));
                break;
            }
        }
    }

    pCmd->Hdr.cbRemainingCmds = cbRemainingCmds;
    pCmd->Hdr.cbRemainingFirstCmd = cbRemainingFirstCmd;
    pCmd->Hdr.u32Reserved = 0;

    return STATUS_SUCCESS;
}

static BOOLEAN vboxVideoCmHasUncompletedCmdsLocked(PVBOXVIDEOCM_MGR pMgr)
{
    PVBOXVIDEOCM_SESSION pSession = NULL;
    for (PLIST_ENTRY pEntry = pMgr->SessionList.Flink; pEntry != &pMgr->SessionList; pEntry = pEntry->Flink)
    {
        pSession = VBOXCMENTRY_2_SESSION(pEntry);
        KIRQL OldIrql;
        KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

        if (pSession->bEventNeeded)
        {
            /* commands still being processed */
            KeReleaseSpinLock(&pSession->SynchLock, OldIrql);
            return TRUE;
        }
        KeReleaseSpinLock(&pSession->SynchLock, OldIrql);
    }
    return FALSE;
}
