/* $Id: VBoxUsbHook.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Driver Dispatch Table Hooking API
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
#include "VBoxUsbMon.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VBOXUSBHOOK_MEMTAG 'HUBV'


NTSTATUS VBoxUsbHookInstall(PVBOXUSBHOOK_ENTRY pHook)
{
    KIRQL Irql;
    KeAcquireSpinLock(&pHook->Lock, &Irql);
    if (pHook->fIsInstalled)
    {
        WARN(("hook is marked installed, returning failure"));
        KeReleaseSpinLock(&pHook->Lock, Irql);
        return STATUS_UNSUCCESSFUL;
    }

    pHook->pfnOldHandler = (PDRIVER_DISPATCH)InterlockedExchangePointer((PVOID*)&pHook->pDrvObj->MajorFunction[pHook->iMjFunction], pHook->pfnHook);
    Assert(pHook->pfnOldHandler);
    Assert(pHook->pfnHook != pHook->pfnOldHandler);
    pHook->fIsInstalled = TRUE;
    KeReleaseSpinLock(&pHook->Lock, Irql);
    return STATUS_SUCCESS;

}
NTSTATUS VBoxUsbHookUninstall(PVBOXUSBHOOK_ENTRY pHook)
{
    KIRQL Irql;
    KeAcquireSpinLock(&pHook->Lock, &Irql);
    if (!pHook->fIsInstalled)
    {
        KeReleaseSpinLock(&pHook->Lock, Irql);
        return STATUS_SUCCESS;
    }

    PDRIVER_DISPATCH pfnOldVal = (PDRIVER_DISPATCH)InterlockedCompareExchangePointer((PVOID*)&pHook->pDrvObj->MajorFunction[pHook->iMjFunction], pHook->pfnOldHandler, pHook->pfnHook);
    Assert(pfnOldVal == pHook->pfnHook);
    if (pfnOldVal != pHook->pfnHook)
    {
        AssertMsgFailed(("unhook failed!!!\n"));
        /* this is bad! this could happen if someone else has chained another hook,
         * or (which is even worse) restored the "initial" entry value it saved when doing a hooking before us
         * return the failure and don't do anything else
         * the best thing to do if this happens is to leave everything as is
         * and to prevent the driver from being unloaded to ensure no one references our unloaded hook routine */
        KeReleaseSpinLock(&pHook->Lock, Irql);
        return STATUS_UNSUCCESSFUL;
    }

    pHook->fIsInstalled = FALSE;
    KeReleaseSpinLock(&pHook->Lock, Irql);

    /* wait for the current handlers to exit */
    VBoxDrvToolRefWaitEqual(&pHook->HookRef, 1);

    return STATUS_SUCCESS;
}

BOOLEAN VBoxUsbHookIsInstalled(PVBOXUSBHOOK_ENTRY pHook)
{
    KIRQL Irql;
    BOOLEAN fIsInstalled;
    KeAcquireSpinLock(&pHook->Lock, &Irql);
    fIsInstalled = pHook->fIsInstalled;
    KeReleaseSpinLock(&pHook->Lock, Irql);
    return fIsInstalled;
}

VOID VBoxUsbHookInit(PVBOXUSBHOOK_ENTRY pHook, PDRIVER_OBJECT pDrvObj, UCHAR iMjFunction, PDRIVER_DISPATCH pfnHook)
{
    Assert(pDrvObj);
    Assert(iMjFunction <= IRP_MJ_MAXIMUM_FUNCTION);
    Assert(pfnHook);
    memset(pHook, 0, sizeof (*pHook));
    InitializeListHead(&pHook->RequestList);
    KeInitializeSpinLock(&pHook->Lock);
    VBoxDrvToolRefInit(&pHook->HookRef);
    pHook->pDrvObj = pDrvObj;
    pHook->iMjFunction = iMjFunction;
    pHook->pfnHook = pfnHook;
    Assert(!pHook->pfnOldHandler);
    Assert(!pHook->fIsInstalled);

}

static void vboxUsbHookRequestRegisterCompletion(PVBOXUSBHOOK_ENTRY pHook, PDEVICE_OBJECT pDevObj, PIRP pIrp, PIO_COMPLETION_ROUTINE pfnCompletion, PVBOXUSBHOOK_REQUEST pRequest)
{
    Assert(pfnCompletion);
    Assert(pRequest);
    Assert(pDevObj);
    Assert(pIrp);
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    memset(pRequest, 0, sizeof (*pRequest));
    pRequest->pHook = pHook;
    pRequest->OldLocation = *pSl;
    pRequest->pDevObj = pDevObj;
    pRequest->pIrp = pIrp;
    pRequest->bCompletionStopped = FALSE;
    pSl->CompletionRoutine = pfnCompletion;
    pSl->Context = pRequest;
    pSl->Control = SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR | SL_INVOKE_ON_CANCEL;

    KIRQL oldIrql;
    KeAcquireSpinLock(&pHook->Lock, &oldIrql);
    InsertTailList(&pHook->RequestList, &pRequest->ListEntry);
    KeReleaseSpinLock(&pHook->Lock, oldIrql);
}

NTSTATUS VBoxUsbHookRequestPassDownHookCompletion(PVBOXUSBHOOK_ENTRY pHook, PDEVICE_OBJECT pDevObj, PIRP pIrp, PIO_COMPLETION_ROUTINE pfnCompletion, PVBOXUSBHOOK_REQUEST pRequest)
{
    Assert(pfnCompletion);
    vboxUsbHookRequestRegisterCompletion(pHook, pDevObj, pIrp, pfnCompletion, pRequest);
    return pHook->pfnOldHandler(pDevObj, pIrp);
}

NTSTATUS VBoxUsbHookRequestPassDownHookSkip(PVBOXUSBHOOK_ENTRY pHook, PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    return pHook->pfnOldHandler(pDevObj, pIrp);
}

NTSTATUS VBoxUsbHookRequestMoreProcessingRequired(PVBOXUSBHOOK_ENTRY pHook, PDEVICE_OBJECT pDevObj, PIRP pIrp,
                                                  PVBOXUSBHOOK_REQUEST pRequest)
{
    RT_NOREF3(pHook, pDevObj, pIrp);
    Assert(!pRequest->bCompletionStopped);
    pRequest->bCompletionStopped = TRUE;
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS VBoxUsbHookRequestComplete(PVBOXUSBHOOK_ENTRY pHook, PDEVICE_OBJECT pDevObj, PIRP pIrp, PVBOXUSBHOOK_REQUEST pRequest)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (pRequest->OldLocation.CompletionRoutine && pRequest->OldLocation.Control)
    {
        Status = pRequest->OldLocation.CompletionRoutine(pDevObj, pIrp, pRequest->OldLocation.Context);
    }

    if (Status != STATUS_MORE_PROCESSING_REQUIRED)
    {
        if (pRequest->bCompletionStopped)
        {
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);
        }
    }
    /*
     * else - in case driver returned STATUS_MORE_PROCESSING_REQUIRED,
     * it will call IoCompleteRequest itself
     */

    KIRQL oldIrql;
    KeAcquireSpinLock(&pHook->Lock, &oldIrql);
    RemoveEntryList(&pRequest->ListEntry);
    KeReleaseSpinLock(&pHook->Lock, oldIrql);
    return Status;
}

#define PVBOXUSBHOOK_REQUEST_FROM_LE(_pLe) ( (PVBOXUSBHOOK_REQUEST)( ((uint8_t*)(_pLe)) - RT_OFFSETOF(VBOXUSBHOOK_REQUEST, ListEntry) ) )

VOID VBoxUsbHookVerifyCompletion(PVBOXUSBHOOK_ENTRY pHook, PVBOXUSBHOOK_REQUEST pRequest, PIRP pIrp)
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&pHook->Lock, &oldIrql);
    for (PLIST_ENTRY pLe = pHook->RequestList.Flink; pLe != &pHook->RequestList; pLe = pLe->Flink)
    {
        PVBOXUSBHOOK_REQUEST pCur = PVBOXUSBHOOK_REQUEST_FROM_LE(pLe);
        if (pCur != pRequest)
            continue;
        if (pCur->pIrp != pIrp)
            continue;
        WARN(("found pending IRP(0x%p) when it should not be", pIrp));
    }
    KeReleaseSpinLock(&pHook->Lock, oldIrql);

}
