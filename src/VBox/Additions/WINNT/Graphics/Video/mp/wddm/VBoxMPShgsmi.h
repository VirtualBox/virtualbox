/* $Id: VBoxMPShgsmi.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPShgsmi_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPShgsmi_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <VBoxVideo.h>

#include "common/VBoxMPUtils.h"

typedef struct VBOXSHGSMI
{
    KSPIN_LOCK HeapLock;
    HGSMIHEAP Heap;
} VBOXSHGSMI, *PVBOXSHGSMI;

typedef DECLCALLBACKTYPE(void, FNVBOXSHGSMICMDCOMPLETION,(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvCmd, void *pvContext));
typedef FNVBOXSHGSMICMDCOMPLETION *PFNVBOXSHGSMICMDCOMPLETION;

typedef DECLCALLBACKTYPE(PFNVBOXSHGSMICMDCOMPLETION, FNVBOXSHGSMICMDCOMPLETION_IRQ,(PVBOXSHGSMI pHeap,
                                                                                    void RT_UNTRUSTED_VOLATILE_HOST *pvCmd,
                                                                                    void *pvContext, void **ppvCompletion));
typedef FNVBOXSHGSMICMDCOMPLETION_IRQ *PFNVBOXSHGSMICMDCOMPLETION_IRQ;


const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *
    VBoxSHGSMICommandPrepAsynchEvent(PVBOXSHGSMI pHeap, PVOID pvBuff, RTSEMEVENT hEventSem);
const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *
    VBoxSHGSMICommandPrepSynch(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvCmd);
const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *
    VBoxSHGSMICommandPrepAsynch(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuff,
                                PFNVBOXSHGSMICMDCOMPLETION pfnCompletion, void RT_UNTRUSTED_VOLATILE_HOST *pvCompletion,
                                uint32_t fFlags);
const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *
    VBoxSHGSMICommandPrepAsynchIrq(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuff,
                                   PFNVBOXSHGSMICMDCOMPLETION_IRQ pfnCompletion, PVOID pvCompletion, uint32_t fFlags);

void VBoxSHGSMICommandDoneAsynch(PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader);
int  VBoxSHGSMICommandDoneSynch(PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader);
void VBoxSHGSMICommandCancelAsynch(PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader);
void VBoxSHGSMICommandCancelSynch(PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader);

DECLINLINE(HGSMIOFFSET) VBoxSHGSMICommandOffset(const PVBOXSHGSMI pHeap, const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_HOST *pHeader)
{
    return HGSMIHeapBufferOffset(&pHeap->Heap, (void*)pHeader);
}

/* allows getting VRAM offset of arbitrary pointer within the SHGSMI command
 * if invalid pointer is passed in, behavior is undefined */
DECLINLINE(HGSMIOFFSET) VBoxSHGSMICommandPtrOffset(const PVBOXSHGSMI pHeap, const void RT_UNTRUSTED_VOLATILE_HOST *pvPtr)
{
    return HGSMIPointerToOffset(&pHeap->Heap.area, (const HGSMIBUFFERHEADER RT_UNTRUSTED_VOLATILE_HOST *)pvPtr);
}

int VBoxSHGSMIInit(PVBOXSHGSMI pHeap, void *pvBase, HGSMISIZE cbArea, HGSMIOFFSET offBase, const HGSMIENV *pEnv);
void VBoxSHGSMITerm(PVBOXSHGSMI pHeap);
void RT_UNTRUSTED_VOLATILE_HOST *VBoxSHGSMIHeapAlloc(PVBOXSHGSMI pHeap, HGSMISIZE cbData, uint8_t u8Channel, uint16_t u16ChannelInfo);
void VBoxSHGSMIHeapFree(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuffer);
void RT_UNTRUSTED_VOLATILE_HOST *VBoxSHGSMIHeapBufferAlloc(PVBOXSHGSMI pHeap, HGSMISIZE cbData);
void VBoxSHGSMIHeapBufferFree(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuffer);
void RT_UNTRUSTED_VOLATILE_HOST *VBoxSHGSMICommandAlloc(PVBOXSHGSMI pHeap, HGSMISIZE cbData, uint8_t u8Channel, uint16_t u16ChannelInfo);
void VBoxSHGSMICommandFree(PVBOXSHGSMI pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvBuffer);
int VBoxSHGSMICommandProcessCompletion(PVBOXSHGSMI pHeap, VBOXSHGSMIHEADER* pCmd, bool bIrq, struct VBOXVTLIST * pPostProcessList);
int VBoxSHGSMICommandPostprocessCompletion(PVBOXSHGSMI pHeap, struct VBOXVTLIST * pPostProcessList);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPShgsmi_h */
