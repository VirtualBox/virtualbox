/* $Id: VBoxMPShgsmi.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <iprt/semaphore.h>

/* SHGSMI */
DECLINLINE(void) vboxSHGSMICommandRetain(VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    ASMAtomicIncU32(&pCmd->cRefs);
}

void vboxSHGSMICommandFree(PVBOXSHGSMI pHeap, VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    VBoxSHGSMIHeapFree(pHeap, pCmd);
}

DECLINLINE(void) vboxSHGSMICommandRelease(PVBOXSHGSMI pHeap, VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    uint32_t cRefs = ASMAtomicDecU32(&pCmd->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if(!cRefs)
        vboxSHGSMICommandFree(pHeap, pCmd);
}

static DECLCALLBACK(void) vboxSHGSMICompletionSetEvent(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvCmd, void *pvContext)
{
    RT_NOREF(pHeap, pvCmd);
    RTSemEventSignal((RTSEMEVENT)pvContext);
}

DECLCALLBACK(void) vboxSHGSMICompletionCommandRelease(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvCmd, void *pvContext)
{
    RT_NOREF(pvContext);
    vboxSHGSMICommandRelease(pHeap, VBoxSHGSMIBufferHeader(pvCmd));
}

/* do not wait for completion */
DECLINLINE(const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *)
vboxSHGSMICommandPrepAsynch(PVBOXSHGSMI pHeap, VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader)
{
    RT_NOREF(pHeap);
    /* ensure the command is not removed until we're processing it */
    vboxSHGSMICommandRetain(pHeader);
    return pHeader;
}

DECLINLINE(void) vboxSHGSMICommandDoneAsynch(PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader)
{
    if(!(ASMAtomicReadU32((volatile uint32_t *)&pHeader->fFlags) & VBOXSHGSMI_FLAG_HG_ASYNCH))
    {
        PFNVBOXSHGSMICMDCOMPLETION pfnCompletion = (PFNVBOXSHGSMICMDCOMPLETION)pHeader->u64Info1;
        if (pfnCompletion)
            pfnCompletion(pHeap, VBoxSHGSMIBufferData(pHeader), (PVOID)pHeader->u64Info2);
    }

    vboxSHGSMICommandRelease(pHeap, (PVBOXSHGSMIHEADER)pHeader);
}

const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *
VBoxSHGSMICommandPrepAsynchEvent(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuff, RTSEMEVENT hEventSem)
{
    VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader = VBoxSHGSMIBufferHeader(pvBuff);
    pHeader->u64Info1 = (uint64_t)vboxSHGSMICompletionSetEvent;
    pHeader->u64Info2 = (uintptr_t)hEventSem;
    pHeader->fFlags   = VBOXSHGSMI_FLAG_GH_ASYNCH_IRQ;

    return vboxSHGSMICommandPrepAsynch(pHeap, pHeader);
}

const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *VBoxSHGSMICommandPrepSynch(PVBOXSHGSMI pHeap,
                                                                              void  RT_UNTRUSTED_VOLATILE_HOST *pCmd)
{
    RTSEMEVENT hEventSem;
    int rc = RTSemEventCreate(&hEventSem);
    Assert(RT_SUCCESS(rc));
    if (RT_SUCCESS(rc))
    {
        return VBoxSHGSMICommandPrepAsynchEvent(pHeap, pCmd, hEventSem);
    }
    return NULL;
}

void VBoxSHGSMICommandDoneAsynch(PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader)
{
    vboxSHGSMICommandDoneAsynch(pHeap, pHeader);
}

int VBoxSHGSMICommandDoneSynch(PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader)
{
    VBoxSHGSMICommandDoneAsynch(pHeap, pHeader);
    RTSEMEVENT hEventSem = (RTSEMEVENT)pHeader->u64Info2;
    int rc = RTSemEventWait(hEventSem, RT_INDEFINITE_WAIT);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
        RTSemEventDestroy(hEventSem);
    return rc;
}

void VBoxSHGSMICommandCancelAsynch(PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader)
{
    vboxSHGSMICommandRelease(pHeap, (PVBOXSHGSMIHEADER)pHeader);
}

void VBoxSHGSMICommandCancelSynch(PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader)
{
    VBoxSHGSMICommandCancelAsynch(pHeap, pHeader);
    RTSEMEVENT hEventSem = (RTSEMEVENT)pHeader->u64Info2;
    RTSemEventDestroy(hEventSem);
}

const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *
VBoxSHGSMICommandPrepAsynch(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuff,
                            PFNVBOXSHGSMICMDCOMPLETION pfnCompletion,
                            void RT_UNTRUSTED_VOLATILE_HOST *pvCompletion, uint32_t fFlags)
{
    fFlags &= ~VBOXSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ;
    VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader = VBoxSHGSMIBufferHeader(pvBuff);
    pHeader->u64Info1 = (uintptr_t)pfnCompletion;
    pHeader->u64Info2 = (uintptr_t)pvCompletion;
    pHeader->fFlags = fFlags;

    return vboxSHGSMICommandPrepAsynch(pHeap, pHeader);
}

const VBOXSHGSMIHEADER  RT_UNTRUSTED_VOLATILE_HOST *
VBoxSHGSMICommandPrepAsynchIrq(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuff,
                               PFNVBOXSHGSMICMDCOMPLETION_IRQ pfnCompletion, PVOID pvCompletion, uint32_t fFlags)
{
    fFlags |= VBOXSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ | VBOXSHGSMI_FLAG_GH_ASYNCH_IRQ;
    VBOXSHGSMIHEADER  RT_UNTRUSTED_VOLATILE_HOST *pHeader = VBoxSHGSMIBufferHeader(pvBuff);
    pHeader->u64Info1 = (uintptr_t)pfnCompletion;
    pHeader->u64Info2 = (uintptr_t)pvCompletion;
    /* we must assign rather than or because flags field does not get zeroed on command creation */
    pHeader->fFlags = fFlags;

    return vboxSHGSMICommandPrepAsynch(pHeap, pHeader);
}

void RT_UNTRUSTED_VOLATILE_HOST *VBoxSHGSMIHeapAlloc(PVBOXSHGSMI pHeap, HGSMISIZE cbData, uint8_t u8Channel, uint16_t u16ChannelInfo)
{
    KIRQL OldIrql;
    void RT_UNTRUSTED_VOLATILE_HOST *pvData;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeAcquireSpinLock(&pHeap->HeapLock, &OldIrql);
    pvData = HGSMIHeapAlloc(&pHeap->Heap, cbData, u8Channel, u16ChannelInfo);
    KeReleaseSpinLock(&pHeap->HeapLock, OldIrql);
    if (!pvData)
        WARN(("HGSMIHeapAlloc failed!"));
    return pvData;
}

void VBoxSHGSMIHeapFree(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuffer)
{
    KIRQL OldIrql;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeAcquireSpinLock(&pHeap->HeapLock, &OldIrql);
    HGSMIHeapFree(&pHeap->Heap, pvBuffer);
    KeReleaseSpinLock(&pHeap->HeapLock, OldIrql);
}

void RT_UNTRUSTED_VOLATILE_HOST *VBoxSHGSMIHeapBufferAlloc(PVBOXSHGSMI pHeap, HGSMISIZE cbData)
{
    KIRQL OldIrql;
    void RT_UNTRUSTED_VOLATILE_HOST * pvData;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeAcquireSpinLock(&pHeap->HeapLock, &OldIrql);
    pvData = HGSMIHeapBufferAlloc(&pHeap->Heap, cbData);
    KeReleaseSpinLock(&pHeap->HeapLock, OldIrql);
    if (!pvData)
        WARN(("HGSMIHeapAlloc failed!"));
    return pvData;
}

void VBoxSHGSMIHeapBufferFree(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuffer)
{
    KIRQL OldIrql;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeAcquireSpinLock(&pHeap->HeapLock, &OldIrql);
    HGSMIHeapBufferFree(&pHeap->Heap, pvBuffer);
    KeReleaseSpinLock(&pHeap->HeapLock, OldIrql);
}

int VBoxSHGSMIInit(PVBOXSHGSMI pHeap, void *pvBase, HGSMISIZE cbArea, HGSMIOFFSET offBase,
                   const HGSMIENV *pEnv)
{
    KeInitializeSpinLock(&pHeap->HeapLock);
    return HGSMIHeapSetup(&pHeap->Heap, pvBase, cbArea, offBase, pEnv);
}

void VBoxSHGSMITerm(PVBOXSHGSMI pHeap)
{
    HGSMIHeapDestroy(&pHeap->Heap);
}

void RT_UNTRUSTED_VOLATILE_HOST *VBoxSHGSMICommandAlloc(PVBOXSHGSMI pHeap, HGSMISIZE cbData, uint8_t u8Channel,
                                                        uint16_t u16ChannelInfo)
{
    /* Issue the flush command. */
    VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader =
        (VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *)VBoxSHGSMIHeapAlloc(pHeap, cbData + sizeof(VBOXSHGSMIHEADER),
                                                                           u8Channel, u16ChannelInfo);
    Assert(pHeader);
    if (pHeader)
    {
        pHeader->cRefs = 1;
        return VBoxSHGSMIBufferData(pHeader);
    }
    return NULL;
}

void VBoxSHGSMICommandFree(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuffer)
{
    VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader = VBoxSHGSMIBufferHeader(pvBuffer);
    vboxSHGSMICommandRelease(pHeap, pHeader);
}

#define VBOXSHGSMI_CMD2LISTENTRY(_pCmd) ((PVBOXVTLIST_ENTRY)&(_pCmd)->pvNext)
#define VBOXSHGSMI_LISTENTRY2CMD(_pEntry) ( (PVBOXSHGSMIHEADER)((uint8_t *)(_pEntry) - RT_UOFFSETOF(VBOXSHGSMIHEADER, pvNext)) )

int VBoxSHGSMICommandProcessCompletion(PVBOXSHGSMI pHeap, VBOXSHGSMIHEADER *pCur, bool bIrq, PVBOXVTLIST pPostProcessList)
{
    int rc = VINF_SUCCESS;

    do
    {
        if (pCur->fFlags & VBOXSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ)
        {
            Assert(bIrq);

            PFNVBOXSHGSMICMDCOMPLETION pfnCompletion = NULL;
            void *pvCompletion;
            PFNVBOXSHGSMICMDCOMPLETION_IRQ pfnCallback = (PFNVBOXSHGSMICMDCOMPLETION_IRQ)pCur->u64Info1;
            void *pvCallback = (void*)pCur->u64Info2;

            pfnCompletion = pfnCallback(pHeap, VBoxSHGSMIBufferData(pCur), pvCallback, &pvCompletion);
            if (pfnCompletion)
            {
                pCur->u64Info1 = (uintptr_t)pfnCompletion;
                pCur->u64Info2 = (uintptr_t)pvCompletion;
                pCur->fFlags &= ~VBOXSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ;
            }
            else
            {
                /* nothing to do with this command */
                break;
            }
        }

        if (!bIrq)
        {
            PFNVBOXSHGSMICMDCOMPLETION pfnCallback = (PFNVBOXSHGSMICMDCOMPLETION)pCur->u64Info1;
            void *pvCallback = (void*)pCur->u64Info2;
            pfnCallback(pHeap, VBoxSHGSMIBufferData(pCur), pvCallback);
        }
        else
            vboxVtListPut(pPostProcessList, VBOXSHGSMI_CMD2LISTENTRY(pCur), VBOXSHGSMI_CMD2LISTENTRY(pCur));
    } while (0);


    return rc;
}

int VBoxSHGSMICommandPostprocessCompletion (PVBOXSHGSMI pHeap, PVBOXVTLIST pPostProcessList)
{
    PVBOXVTLIST_ENTRY pNext, pCur;
    for (pCur = pPostProcessList->pFirst; pCur; pCur = pNext)
    {
        /* need to save next since the command may be released in a pfnCallback and thus its data might be invalid */
        pNext = pCur->pNext;
        PVBOXSHGSMIHEADER pCmd = VBOXSHGSMI_LISTENTRY2CMD(pCur);
        PFNVBOXSHGSMICMDCOMPLETION pfnCallback = (PFNVBOXSHGSMICMDCOMPLETION)pCmd->u64Info1;
        void *pvCallback = (void*)pCmd->u64Info2;
        pfnCallback(pHeap, VBoxSHGSMIBufferData(pCmd), pvCallback);
    }

    return VINF_SUCCESS;
}
