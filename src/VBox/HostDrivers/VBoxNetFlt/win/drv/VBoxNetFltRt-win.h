/* $Id: VBoxNetFltRt-win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetFltRt-win.h - Bridged Networking Driver, Windows Specific Code.
 * NetFlt Runtime API
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

#ifndef VBOX_INCLUDED_SRC_VBoxNetFlt_win_drv_VBoxNetFltRt_win_h
#define VBOX_INCLUDED_SRC_VBoxNetFlt_win_drv_VBoxNetFltRt_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif
DECLHIDDEN(VOID) vboxNetFltWinUnload(IN PDRIVER_OBJECT DriverObject);

#ifndef VBOXNETADP
# if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)
DECLHIDDEN(bool) vboxNetFltWinMatchPackets(PNDIS_PACKET pPacket1, PNDIS_PACKET pPacket2, const INT cbMatch);
DECLHIDDEN(bool) vboxNetFltWinMatchPacketAndSG(PNDIS_PACKET pPacket, PINTNETSG pSG, const INT cbMatch);
# endif
#endif

/*************************
 * packet queue API      *
 *************************/


#define LIST_ENTRY_2_PACKET_INFO(pListEntry) \
    ( (PVBOXNETFLT_PACKET_INFO)((uint8_t *)(pListEntry) - RT_UOFFSETOF(VBOXNETFLT_PACKET_INFO, ListEntry)) )

#if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)

#define VBOX_SLE_2_PKTRSVD_PT(_pEntry) \
    ( (PVBOXNETFLT_PKTRSVD_PT)((uint8_t *)(_pEntry) - RT_UOFFSETOF(VBOXNETFLT_PKTRSVD_PT, ListEntry)) )

#define VBOX_SLE_2_SENDPACKET(_pEntry) \
    ( (PNDIS_PACKET)((uint8_t *)(VBOX_SLE_2_PKTRSVD_PT(_pEntry)) - RT_UOFFSETOF(NDIS_PACKET, ProtocolReserved)) )

#endif
/**
 * enqueus the packet info to the tail of the queue
 */
DECLINLINE(void) vboxNetFltWinQuEnqueueTail(PVBOXNETFLT_PACKET_QUEUE pQueue, PVBOXNETFLT_PACKET_INFO pPacketInfo)
{
    InsertTailList(pQueue, &pPacketInfo->ListEntry);
}

DECLINLINE(void) vboxNetFltWinQuEnqueueHead(PVBOXNETFLT_PACKET_QUEUE pQueue, PVBOXNETFLT_PACKET_INFO pPacketInfo)
{
    Assert(pPacketInfo->pPool);
    InsertHeadList(pQueue, &pPacketInfo->ListEntry);
}

/**
 * enqueus the packet info to the tail of the queue
 */
DECLINLINE(void) vboxNetFltWinQuInterlockedEnqueueTail(PVBOXNETFLT_INTERLOCKED_PACKET_QUEUE pQueue, PVBOXNETFLT_PACKET_INFO pPacketInfo)
{
    Assert(pPacketInfo->pPool);
    NdisAcquireSpinLock(&pQueue->Lock);
    vboxNetFltWinQuEnqueueTail(&pQueue->Queue, pPacketInfo);
    NdisReleaseSpinLock(&pQueue->Lock);
}

DECLINLINE(void) vboxNetFltWinQuInterlockedEnqueueHead(PVBOXNETFLT_INTERLOCKED_PACKET_QUEUE pQueue, PVBOXNETFLT_PACKET_INFO pPacketInfo)
{
    NdisAcquireSpinLock(&pQueue->Lock);
    vboxNetFltWinQuEnqueueHead(&pQueue->Queue, pPacketInfo);
    NdisReleaseSpinLock(&pQueue->Lock);
}

/**
 * dequeus the packet info from the head of the queue
 */
DECLINLINE(PVBOXNETFLT_PACKET_INFO) vboxNetFltWinQuDequeueHead(PVBOXNETFLT_PACKET_QUEUE pQueue)
{
    PLIST_ENTRY pListEntry = RemoveHeadList(pQueue);
    if (pListEntry != pQueue)
    {
        PVBOXNETFLT_PACKET_INFO pInfo = LIST_ENTRY_2_PACKET_INFO(pListEntry);
        Assert(pInfo->pPool);
        return pInfo;
    }
    return NULL;
}

DECLINLINE(PVBOXNETFLT_PACKET_INFO) vboxNetFltWinQuDequeueTail(PVBOXNETFLT_PACKET_QUEUE pQueue)
{
    PLIST_ENTRY pListEntry = RemoveTailList(pQueue);
    if (pListEntry != pQueue)
    {
        PVBOXNETFLT_PACKET_INFO pInfo = LIST_ENTRY_2_PACKET_INFO(pListEntry);
        Assert(pInfo->pPool);
        return pInfo;
    }
    return NULL;
}

DECLINLINE(PVBOXNETFLT_PACKET_INFO) vboxNetFltWinQuInterlockedDequeueHead(PVBOXNETFLT_INTERLOCKED_PACKET_QUEUE pInterlockedQueue)
{
    PVBOXNETFLT_PACKET_INFO pInfo;
    NdisAcquireSpinLock(&pInterlockedQueue->Lock);
    pInfo = vboxNetFltWinQuDequeueHead(&pInterlockedQueue->Queue);
    NdisReleaseSpinLock(&pInterlockedQueue->Lock);
    return pInfo;
}

DECLINLINE(PVBOXNETFLT_PACKET_INFO) vboxNetFltWinQuInterlockedDequeueTail(PVBOXNETFLT_INTERLOCKED_PACKET_QUEUE pInterlockedQueue)
{
    PVBOXNETFLT_PACKET_INFO pInfo;
    NdisAcquireSpinLock(&pInterlockedQueue->Lock);
    pInfo = vboxNetFltWinQuDequeueTail(&pInterlockedQueue->Queue);
    NdisReleaseSpinLock(&pInterlockedQueue->Lock);
    return pInfo;
}

DECLINLINE(void) vboxNetFltWinQuDequeue(PVBOXNETFLT_PACKET_INFO pInfo)
{
    RemoveEntryList(&pInfo->ListEntry);
}

DECLINLINE(void) vboxNetFltWinQuInterlockedDequeue(PVBOXNETFLT_INTERLOCKED_PACKET_QUEUE pInterlockedQueue, PVBOXNETFLT_PACKET_INFO pInfo)
{
    NdisAcquireSpinLock(&pInterlockedQueue->Lock);
    vboxNetFltWinQuDequeue(pInfo);
    NdisReleaseSpinLock(&pInterlockedQueue->Lock);
}

/**
 * allocates the packet info from the pool
 */
DECLINLINE(PVBOXNETFLT_PACKET_INFO) vboxNetFltWinPpAllocPacketInfo(PVBOXNETFLT_PACKET_INFO_POOL pPool)
{
    return vboxNetFltWinQuInterlockedDequeueHead(&pPool->Queue);
}

/**
 * returns the packet info to the pool
 */
DECLINLINE(void) vboxNetFltWinPpFreePacketInfo(PVBOXNETFLT_PACKET_INFO pInfo)
{
    PVBOXNETFLT_PACKET_INFO_POOL pPool = pInfo->pPool;
    vboxNetFltWinQuInterlockedEnqueueHead(&pPool->Queue, pInfo);
}

/** initializes the packet queue */
#define INIT_PACKET_QUEUE(_pQueue) InitializeListHead((_pQueue))

/** initializes the packet queue */
#define INIT_INTERLOCKED_PACKET_QUEUE(_pQueue) \
    { \
        INIT_PACKET_QUEUE(&(_pQueue)->Queue); \
        NdisAllocateSpinLock(&(_pQueue)->Lock); \
    }

/** delete the packet queue */
#define FINI_INTERLOCKED_PACKET_QUEUE(_pQueue) NdisFreeSpinLock(&(_pQueue)->Lock)

/** returns the packet the packet info contains */
#define GET_PACKET_FROM_INFO(_pPacketInfo) (ASMAtomicUoReadPtr((void * volatile *)&(_pPacketInfo)->pPacket))

/** assignes the packet to the packet info */
#define SET_PACKET_TO_INFO(_pPacketInfo, _pPacket) (ASMAtomicUoWritePtr(&(_pPacketInfo)->pPacket, (_pPacket)))

/** returns the flags the packet info contains */
#define GET_FLAGS_FROM_INFO(_pPacketInfo) (ASMAtomicUoReadU32((volatile uint32_t *)&(_pPacketInfo)->fFlags))

/** sets flags to the packet info */
#define SET_FLAGS_TO_INFO(_pPacketInfo, _fFlags) (ASMAtomicUoWriteU32((volatile uint32_t *)&(_pPacketInfo)->fFlags, (_fFlags)))

#ifdef VBOXNETFLT_NO_PACKET_QUEUE
DECLHIDDEN(bool) vboxNetFltWinPostIntnet(PVBOXNETFLTINS pInstance, PVOID pvPacket, const UINT fFlags);
#else
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinQuEnqueuePacket(PVBOXNETFLTINS pInstance, PVOID pPacket, const UINT fPacketFlags);
DECLHIDDEN(void) vboxNetFltWinQuFiniPacketQueue(PVBOXNETFLTINS pInstance);
DECLHIDDEN(NTSTATUS) vboxNetFltWinQuInitPacketQueue(PVBOXNETFLTINS pInstance);
#endif /* #ifndef VBOXNETFLT_NO_PACKET_QUEUE */


#ifndef VBOXNETADP
/**
 * searches the list entry in a single-linked list
 */
DECLINLINE(bool) vboxNetFltWinSearchListEntry(PVBOXNETFLT_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry2Search, bool bRemove)
{
    PSINGLE_LIST_ENTRY pHead = &pList->Head;
    PSINGLE_LIST_ENTRY pCur;
    PSINGLE_LIST_ENTRY pPrev;
    for (pCur = pHead->Next, pPrev = pHead; pCur; pPrev = pCur, pCur = pCur->Next)
    {
        if (pEntry2Search == pCur)
        {
            if (bRemove)
            {
                pPrev->Next = pCur->Next;
                if (pCur == pList->pTail)
                {
                    pList->pTail = pPrev;
                }
            }
            return true;
        }
    }
    return false;
}

#if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)

DECLINLINE(PNDIS_PACKET) vboxNetFltWinSearchPacket(PVBOXNETFLT_SINGLE_LIST pList, PNDIS_PACKET pPacket2Search, int cbMatch, bool bRemove)
{
    PSINGLE_LIST_ENTRY pHead = &pList->Head;
    PSINGLE_LIST_ENTRY pCur;
    PSINGLE_LIST_ENTRY pPrev;
    PNDIS_PACKET pCurPacket;
    for (pCur = pHead->Next, pPrev = pHead; pCur; pPrev = pCur, pCur = pCur->Next)
    {
        pCurPacket = VBOX_SLE_2_SENDPACKET(pCur);
        if (pCurPacket == pPacket2Search || vboxNetFltWinMatchPackets(pPacket2Search, pCurPacket, cbMatch))
        {
            if (bRemove)
            {
                pPrev->Next = pCur->Next;
                if (pCur == pList->pTail)
                {
                    pList->pTail = pPrev;
                }
            }
            return pCurPacket;
        }
    }
    return NULL;
}

DECLINLINE(PNDIS_PACKET) vboxNetFltWinSearchPacketBySG(PVBOXNETFLT_SINGLE_LIST pList, PINTNETSG pSG, int cbMatch, bool bRemove)
{
    PSINGLE_LIST_ENTRY pHead = &pList->Head;
    PSINGLE_LIST_ENTRY pCur;
    PSINGLE_LIST_ENTRY pPrev;
    PNDIS_PACKET pCurPacket;
    for (pCur = pHead->Next, pPrev = pHead; pCur; pPrev = pCur, pCur = pCur->Next)
    {
        pCurPacket = VBOX_SLE_2_SENDPACKET(pCur);
        if (vboxNetFltWinMatchPacketAndSG(pCurPacket, pSG, cbMatch))
        {
            if (bRemove)
            {
                pPrev->Next = pCur->Next;
                if (pCur == pList->pTail)
                {
                    pList->pTail = pPrev;
                }
            }
            return pCurPacket;
        }
    }
    return NULL;
}

#endif /* #if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS) */

DECLINLINE(bool) vboxNetFltWinSListIsEmpty(PVBOXNETFLT_SINGLE_LIST pList)
{
    return !pList->Head.Next;
}

DECLINLINE(void) vboxNetFltWinPutTail(PVBOXNETFLT_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    pList->pTail->Next = pEntry;
    pList->pTail = pEntry;
    pEntry->Next = NULL;
}

DECLINLINE(void) vboxNetFltWinPutHead(PVBOXNETFLT_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    pEntry->Next = pList->Head.Next;
    pList->Head.Next = pEntry;
    if (!pEntry->Next)
        pList->pTail = pEntry;
}

DECLINLINE(PSINGLE_LIST_ENTRY) vboxNetFltWinGetHead(PVBOXNETFLT_SINGLE_LIST pList)
{
    PSINGLE_LIST_ENTRY pEntry = pList->Head.Next;
    if (pEntry && pEntry == pList->pTail)
    {
        pList->Head.Next = NULL;
        pList->pTail = &pList->Head;
    }
    return pEntry;
}

DECLINLINE(bool) vboxNetFltWinInterlockedSearchListEntry(PVBOXNETFLT_INTERLOCKED_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry2Search, bool bRemove)
{
    bool bFound;
    NdisAcquireSpinLock(&pList->Lock);
    bFound = vboxNetFltWinSearchListEntry(&pList->List, pEntry2Search, bRemove);
    NdisReleaseSpinLock(&pList->Lock);
    return bFound;
}

#if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)

DECLINLINE(PNDIS_PACKET) vboxNetFltWinInterlockedSearchPacket(PVBOXNETFLT_INTERLOCKED_SINGLE_LIST pList, PNDIS_PACKET pPacket2Search, int cbMatch, bool bRemove)
{
    PNDIS_PACKET pFound;
    NdisAcquireSpinLock(&pList->Lock);
    pFound = vboxNetFltWinSearchPacket(&pList->List, pPacket2Search, cbMatch, bRemove);
    NdisReleaseSpinLock(&pList->Lock);
    return pFound;
}

DECLINLINE(PNDIS_PACKET) vboxNetFltWinInterlockedSearchPacketBySG(PVBOXNETFLT_INTERLOCKED_SINGLE_LIST pList, PINTNETSG pSG, int cbMatch, bool bRemove)
{
    PNDIS_PACKET pFound;
    NdisAcquireSpinLock(&pList->Lock);
    pFound = vboxNetFltWinSearchPacketBySG(&pList->List, pSG, cbMatch, bRemove);
    NdisReleaseSpinLock(&pList->Lock);
    return pFound;
}
#endif /* #if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS) */

DECLINLINE(void) vboxNetFltWinInterlockedPutTail(PVBOXNETFLT_INTERLOCKED_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    NdisAcquireSpinLock(&pList->Lock);
    vboxNetFltWinPutTail(&pList->List, pEntry);
    NdisReleaseSpinLock(&pList->Lock);
}

DECLINLINE(void) vboxNetFltWinInterlockedPutHead(PVBOXNETFLT_INTERLOCKED_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    NdisAcquireSpinLock(&pList->Lock);
    vboxNetFltWinPutHead(&pList->List, pEntry);
    NdisReleaseSpinLock(&pList->Lock);
}

DECLINLINE(PSINGLE_LIST_ENTRY) vboxNetFltWinInterlockedGetHead(PVBOXNETFLT_INTERLOCKED_SINGLE_LIST pList)
{
    PSINGLE_LIST_ENTRY pEntry;
    NdisAcquireSpinLock(&pList->Lock);
    pEntry = vboxNetFltWinGetHead(&pList->List);
    NdisReleaseSpinLock(&pList->Lock);
    return pEntry;
}

# if defined(DEBUG_NETFLT_PACKETS) || !defined(VBOX_LOOPBACK_USEFLAGS)
DECLINLINE(void) vboxNetFltWinLbPutSendPacket(PVBOXNETFLTINS pNetFlt, PNDIS_PACKET pPacket, bool bFromIntNet)
{
    PVBOXNETFLT_PKTRSVD_PT pSrv = (PVBOXNETFLT_PKTRSVD_PT)pPacket->ProtocolReserved;
    pSrv->bFromIntNet = bFromIntNet;
    vboxNetFltWinInterlockedPutHead(&pNetFlt->u.s.WinIf.SendPacketQueue, &pSrv->ListEntry);
}

DECLINLINE(bool) vboxNetFltWinLbIsFromIntNet(PNDIS_PACKET pPacket)
{
    PVBOXNETFLT_PKTRSVD_PT pSrv = (PVBOXNETFLT_PKTRSVD_PT)pPacket->ProtocolReserved;
    return pSrv->bFromIntNet;
}

DECLINLINE(PNDIS_PACKET) vboxNetFltWinLbSearchLoopBack(PVBOXNETFLTINS pNetFlt, PNDIS_PACKET pPacket, bool bRemove)
{
    return vboxNetFltWinInterlockedSearchPacket(&pNetFlt->u.s.WinIf.SendPacketQueue, pPacket, VBOXNETFLT_PACKETMATCH_LENGTH, bRemove);
}

DECLINLINE(PNDIS_PACKET) vboxNetFltWinLbSearchLoopBackBySG(PVBOXNETFLTINS pNetFlt, PINTNETSG pSG, bool bRemove)
{
    return vboxNetFltWinInterlockedSearchPacketBySG(&pNetFlt->u.s.WinIf.SendPacketQueue, pSG, VBOXNETFLT_PACKETMATCH_LENGTH, bRemove);
}

DECLINLINE(bool) vboxNetFltWinLbRemoveSendPacket(PVBOXNETFLTINS pNetFlt, PNDIS_PACKET pPacket)
{
    PVBOXNETFLT_PKTRSVD_PT pSrv = (PVBOXNETFLT_PKTRSVD_PT)pPacket->ProtocolReserved;
    bool bRet = vboxNetFltWinInterlockedSearchListEntry(&pNetFlt->u.s.WinIf.SendPacketQueue, &pSrv->ListEntry, true);
#ifdef DEBUG_misha
    Assert(bRet == (pNetFlt->enmTrunkState == INTNETTRUNKIFSTATE_ACTIVE));
#endif
    return bRet;
}

# endif

#endif

#ifdef DEBUG_misha
DECLHIDDEN(bool) vboxNetFltWinCheckMACs(PNDIS_PACKET pPacket, PRTMAC pDst, PRTMAC pSrc);
DECLHIDDEN(bool) vboxNetFltWinCheckMACsSG(PINTNETSG pSG, PRTMAC pDst, PRTMAC pSrc);
extern RTMAC g_vboxNetFltWinVerifyMACBroadcast;
extern RTMAC g_vboxNetFltWinVerifyMACGuest;

# define VBOXNETFLT_LBVERIFY(_pnf, _p) \
    do { \
        Assert(!vboxNetFltWinCheckMACs(_p, NULL, &g_vboxNetFltWinVerifyMACGuest)); \
        Assert(!vboxNetFltWinCheckMACs(_p, NULL, &(_pnf)->u.s.MacAddr)); \
    } while (0)

# define VBOXNETFLT_LBVERIFYSG(_pnf, _p) \
    do { \
        Assert(!vboxNetFltWinCheckMACsSG(_p, NULL, &g_vboxNetFltWinVerifyMACGuest)); \
        Assert(!vboxNetFltWinCheckMACsSG(_p, NULL, &(_pnf)->u.s.MacAddr)); \
    } while (0)

#else
# define VBOXNETFLT_LBVERIFY(_pnf, _p) do { } while (0)
# define VBOXNETFLT_LBVERIFYSG(_pnf, _p) do { } while (0)
#endif

/** initializes the list */
#define INIT_SINGLE_LIST(_pList) \
    { \
        (_pList)->Head.Next = NULL; \
        (_pList)->pTail = &(_pList)->Head; \
    }

/** initializes the list */
#define INIT_INTERLOCKED_SINGLE_LIST(_pList) \
    do { \
        INIT_SINGLE_LIST(&(_pList)->List); \
        NdisAllocateSpinLock(&(_pList)->Lock); \
    } while (0)

/** delete the packet queue */
#define FINI_INTERLOCKED_SINGLE_LIST(_pList) \
    do { \
        Assert(vboxNetFltWinSListIsEmpty(&(_pList)->List)); \
        NdisFreeSpinLock(&(_pList)->Lock); \
    } while (0)


/**************************************************************************
 * PVBOXNETFLTINS , WinIf reference/dereference (i.e. retain/release) API *
 **************************************************************************/


DECLHIDDEN(void) vboxNetFltWinWaitDereference(PVBOXNETFLT_WINIF_DEVICE pState);

DECLINLINE(void) vboxNetFltWinReferenceModeNetFlt(PVBOXNETFLTINS pIns)
{
    ASMAtomicIncU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs);
}

DECLINLINE(void) vboxNetFltWinReferenceModePassThru(PVBOXNETFLTINS pIns)
{
    ASMAtomicIncU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs);
}

DECLINLINE(void) vboxNetFltWinIncReferenceModeNetFlt(PVBOXNETFLTINS pIns, uint32_t v)
{
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs, v);
}

DECLINLINE(void) vboxNetFltWinIncReferenceModePassThru(PVBOXNETFLTINS pIns, uint32_t v)
{
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs, v);
}

DECLINLINE(void) vboxNetFltWinDereferenceModeNetFlt(PVBOXNETFLTINS pIns)
{
    ASMAtomicDecU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs);
}

DECLINLINE(void) vboxNetFltWinDereferenceModePassThru(PVBOXNETFLTINS pIns)
{
    ASMAtomicDecU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs);
}

DECLINLINE(void) vboxNetFltWinDecReferenceModeNetFlt(PVBOXNETFLTINS pIns, uint32_t v)
{
    Assert(v);
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs, (uint32_t)(-((int32_t)v)));
}

DECLINLINE(void) vboxNetFltWinDecReferenceModePassThru(PVBOXNETFLTINS pIns, uint32_t v)
{
    Assert(v);
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs, (uint32_t)(-((int32_t)v)));
}

DECLINLINE(void) vboxNetFltWinSetPowerState(PVBOXNETFLT_WINIF_DEVICE pState, NDIS_DEVICE_POWER_STATE State)
{
    ASMAtomicUoWriteU32((volatile uint32_t *)&pState->PowerState, State);
}

DECLINLINE(NDIS_DEVICE_POWER_STATE) vboxNetFltWinGetPowerState(PVBOXNETFLT_WINIF_DEVICE pState)
{
    return (NDIS_DEVICE_POWER_STATE)ASMAtomicUoReadU32((volatile uint32_t *)&pState->PowerState);
}

DECLINLINE(void) vboxNetFltWinSetOpState(PVBOXNETFLT_WINIF_DEVICE pState, VBOXNETDEVOPSTATE State)
{
    ASMAtomicUoWriteU32((volatile uint32_t *)&pState->OpState, State);
}

DECLINLINE(VBOXNETDEVOPSTATE) vboxNetFltWinGetOpState(PVBOXNETFLT_WINIF_DEVICE pState)
{
    return (VBOXNETDEVOPSTATE)ASMAtomicUoReadU32((volatile uint32_t *)&pState->OpState);
}

DECLINLINE(bool) vboxNetFltWinDoReferenceDevice(PVBOXNETFLT_WINIF_DEVICE pState)
{
    if (vboxNetFltWinGetPowerState(pState) == NdisDeviceStateD0 && vboxNetFltWinGetOpState(pState) == kVBoxNetDevOpState_Initialized)
    {
        /** @todo r=bird: Since this is a volatile member, why don't you declare it as
         *        such and save yourself all the casting? */
        ASMAtomicIncU32((uint32_t volatile *)&pState->cReferences);
        return true;
    }
    return false;
}

#ifndef VBOXNETADP
DECLINLINE(bool) vboxNetFltWinDoReferenceDevices(PVBOXNETFLT_WINIF_DEVICE pState1, PVBOXNETFLT_WINIF_DEVICE pState2)
{
    if (vboxNetFltWinGetPowerState(pState1) == NdisDeviceStateD0
            && vboxNetFltWinGetOpState(pState1) == kVBoxNetDevOpState_Initialized
            && vboxNetFltWinGetPowerState(pState2) == NdisDeviceStateD0
            && vboxNetFltWinGetOpState(pState2) == kVBoxNetDevOpState_Initialized)
    {
        ASMAtomicIncU32((uint32_t volatile *)&pState1->cReferences);
        ASMAtomicIncU32((uint32_t volatile *)&pState2->cReferences);
        return true;
    }
    return false;
}
#endif

DECLINLINE(void) vboxNetFltWinDereferenceDevice(PVBOXNETFLT_WINIF_DEVICE pState)
{
    ASMAtomicDecU32((uint32_t volatile *)&pState->cReferences);
    /** @todo r=bird: Add comment explaining why these cannot hit 0 or why
     *        reference are counted  */
}

#ifndef VBOXNETADP
DECLINLINE(void) vboxNetFltWinDereferenceDevices(PVBOXNETFLT_WINIF_DEVICE pState1, PVBOXNETFLT_WINIF_DEVICE pState2)
{
    ASMAtomicDecU32((uint32_t volatile *)&pState1->cReferences);
    ASMAtomicDecU32((uint32_t volatile *)&pState2->cReferences);
}
#endif

DECLINLINE(void) vboxNetFltWinDecReferenceDevice(PVBOXNETFLT_WINIF_DEVICE pState, uint32_t v)
{
    Assert(v);
    ASMAtomicAddU32((uint32_t volatile *)&pState->cReferences, (uint32_t)(-((int32_t)v)));
}

#ifndef VBOXNETADP
DECLINLINE(void) vboxNetFltWinDecReferenceDevices(PVBOXNETFLT_WINIF_DEVICE pState1, PVBOXNETFLT_WINIF_DEVICE pState2, uint32_t v)
{
    ASMAtomicAddU32((uint32_t volatile *)&pState1->cReferences, (uint32_t)(-((int32_t)v)));
    ASMAtomicAddU32((uint32_t volatile *)&pState2->cReferences, (uint32_t)(-((int32_t)v)));
}
#endif

DECLINLINE(bool) vboxNetFltWinDoIncReferenceDevice(PVBOXNETFLT_WINIF_DEVICE pState, uint32_t v)
{
    Assert(v);
    if (vboxNetFltWinGetPowerState(pState) == NdisDeviceStateD0 && vboxNetFltWinGetOpState(pState) == kVBoxNetDevOpState_Initialized)
    {
        ASMAtomicAddU32((uint32_t volatile *)&pState->cReferences, v);
        return true;
    }
    return false;
}

#ifndef VBOXNETADP
DECLINLINE(bool) vboxNetFltWinDoIncReferenceDevices(PVBOXNETFLT_WINIF_DEVICE pState1, PVBOXNETFLT_WINIF_DEVICE pState2, uint32_t v)
{
    if (vboxNetFltWinGetPowerState(pState1) == NdisDeviceStateD0
            && vboxNetFltWinGetOpState(pState1) == kVBoxNetDevOpState_Initialized
            && vboxNetFltWinGetPowerState(pState2) == NdisDeviceStateD0
            && vboxNetFltWinGetOpState(pState2) == kVBoxNetDevOpState_Initialized)
    {
        ASMAtomicAddU32((uint32_t volatile *)&pState1->cReferences, v);
        ASMAtomicAddU32((uint32_t volatile *)&pState2->cReferences, v);
        return true;
    }
    return false;
}
#endif


DECLINLINE(bool) vboxNetFltWinReferenceWinIfNetFlt(PVBOXNETFLTINS pNetFlt, bool * pbNetFltActive)
{
    RTSpinlockAcquire((pNetFlt)->hSpinlock);
#ifndef VBOXNETADP
    if (!vboxNetFltWinDoReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState))
#else
    if (!vboxNetFltWinDoReferenceDevice(&pNetFlt->u.s.WinIf.MpState))
#endif
    {
        RTSpinlockRelease((pNetFlt)->hSpinlock);
        *pbNetFltActive = false;
        return false;
    }

    if (pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE)
    {
        vboxNetFltWinReferenceModePassThru(pNetFlt);
        RTSpinlockRelease((pNetFlt)->hSpinlock);
        *pbNetFltActive = false;
        return true;
    }

    vboxNetFltRetain((pNetFlt), true /* fBusy */);
    vboxNetFltWinReferenceModeNetFlt(pNetFlt);
    RTSpinlockRelease((pNetFlt)->hSpinlock);

    *pbNetFltActive = true;
    return true;
}

DECLINLINE(bool) vboxNetFltWinIncReferenceWinIfNetFlt(PVBOXNETFLTINS pNetFlt, uint32_t v, bool *pbNetFltActive)
{
    uint32_t i;

    Assert(v);
    if (!v)
    {
        *pbNetFltActive = false;
        return false;
    }

    RTSpinlockAcquire((pNetFlt)->hSpinlock);
#ifndef VBOXNETADP
    if (!vboxNetFltWinDoIncReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState, v))
#else
    if (!vboxNetFltWinDoIncReferenceDevice(&pNetFlt->u.s.WinIf.MpState, v))
#endif
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        *pbNetFltActive = false;
        return false;
    }

    if (pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE)
    {
        vboxNetFltWinIncReferenceModePassThru(pNetFlt, v);

        RTSpinlockRelease((pNetFlt)->hSpinlock);
        *pbNetFltActive = false;
        return true;
    }

    vboxNetFltRetain(pNetFlt, true /* fBusy */);

    vboxNetFltWinIncReferenceModeNetFlt(pNetFlt, v);

    RTSpinlockRelease(pNetFlt->hSpinlock);

    /* we have marked it as busy, so can do the res references outside the lock */
    for (i = 0; i < v-1; i++)
    {
        vboxNetFltRetain(pNetFlt, true /* fBusy */);
    }

    *pbNetFltActive = true;

    return true;
}

DECLINLINE(void) vboxNetFltWinDecReferenceNetFlt(PVBOXNETFLTINS pNetFlt, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
    {
        vboxNetFltRelease(pNetFlt, true);
    }

    vboxNetFltWinDecReferenceModeNetFlt(pNetFlt, n);
}

DECLINLINE(void) vboxNetFltWinDereferenceNetFlt(PVBOXNETFLTINS pNetFlt)
{
    vboxNetFltRelease(pNetFlt, true);

    vboxNetFltWinDereferenceModeNetFlt(pNetFlt);
}

DECLINLINE(void) vboxNetFltWinDecReferenceWinIf(PVBOXNETFLTINS pNetFlt, uint32_t v)
{
#ifdef VBOXNETADP
    vboxNetFltWinDecReferenceDevice(&pNetFlt->u.s.WinIf.MpState, v);
#else
    vboxNetFltWinDecReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState, v);
#endif
}

DECLINLINE(void) vboxNetFltWinDereferenceWinIf(PVBOXNETFLTINS pNetFlt)
{
#ifdef VBOXNETADP
    vboxNetFltWinDereferenceDevice(&pNetFlt->u.s.WinIf.MpState);
#else
    vboxNetFltWinDereferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState);
#endif
}

DECLINLINE(bool) vboxNetFltWinIncReferenceWinIf(PVBOXNETFLTINS pNetFlt, uint32_t v)
{
    Assert(v);
    if (!v)
    {
        return false;
    }

    RTSpinlockAcquire(pNetFlt->hSpinlock);
#ifdef VBOXNETADP
    if (vboxNetFltWinDoIncReferenceDevice(&pNetFlt->u.s.WinIf.MpState, v))
#else
    if (vboxNetFltWinDoIncReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState, v))
#endif
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        return true;
    }

    RTSpinlockRelease(pNetFlt->hSpinlock);
    return false;
}

DECLINLINE(bool) vboxNetFltWinReferenceWinIf(PVBOXNETFLTINS pNetFlt)
{
    RTSpinlockAcquire(pNetFlt->hSpinlock);
#ifdef VBOXNETADP
    if (vboxNetFltWinDoReferenceDevice(&pNetFlt->u.s.WinIf.MpState))
#else
    if (vboxNetFltWinDoReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState))
#endif
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        return true;
    }

    RTSpinlockRelease(pNetFlt->hSpinlock);
    return false;
}

/***********************************************
 * methods for accessing the network card info *
 ***********************************************/

DECLHIDDEN(NDIS_STATUS) vboxNetFltWinGetMacAddress(PVBOXNETFLTINS pNetFlt, PRTMAC pMac);
DECLHIDDEN(bool) vboxNetFltWinIsPromiscuous(PVBOXNETFLTINS pNetFlt);
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinSetPromiscuous(PVBOXNETFLTINS pNetFlt, bool bYes);
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinQueryPhysicalMedium(PVBOXNETFLTINS pNetFlt, NDIS_PHYSICAL_MEDIUM * pMedium);

/*********************
 * mem alloc API     *
 *********************/

DECLHIDDEN(NDIS_STATUS) vboxNetFltWinMemAlloc(PVOID* ppMemBuf, UINT cbLength);

DECLHIDDEN(void) vboxNetFltWinMemFree(PVOID pMemBuf);

/* convenience method used which allocates and initializes the PINTNETSG containing one
 * segment referring the buffer of size cbBufSize
 * the allocated PINTNETSG should be freed with the vboxNetFltWinMemFree.
 *
 * This is used when our ProtocolReceive callback is called and we have to return the indicated NDIS_PACKET
 * on a callback exit. This is why we allocate the PINTNETSG and put the packet info there and enqueue it
 * for the packet queue */
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinAllocSG(UINT cbBufSize, PINTNETSG *ppSG);

/************************
 * WinIf init/fini API *
 ************************/
#if defined(VBOXNETADP)
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtInitBind(PVBOXNETFLTINS *ppNetFlt, NDIS_HANDLE hMiniportAdapter, PNDIS_STRING pBindToMiniportName /* actually this is our miniport name*/, NDIS_HANDLE hWrapperConfigurationContext);
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtInitWinIf(PVBOXNETFLTWIN pWinIf);
#else
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtInitBind(PVBOXNETFLTINS *ppNetFlt, PNDIS_STRING pOurMiniportName, PNDIS_STRING pBindToMiniportName);
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtInitWinIf(PVBOXNETFLTWIN pWinIf, PNDIS_STRING pOurDeviceName);
#endif

DECLHIDDEN(VOID) vboxNetFltWinPtFiniWinIf(PVBOXNETFLTWIN pWinIf);

/************************************
 * Execute Job at passive level API *
 ************************************/

typedef VOID (*PFNVBOXNETFLT_JOB_ROUTINE) (PVOID pContext);

DECLHIDDEN(VOID) vboxNetFltWinJobSynchExecAtPassive(PFNVBOXNETFLT_JOB_ROUTINE pfnRoutine, PVOID pContext);

/*******************************
 * Ndis Packets processing API *
 *******************************/
DECLHIDDEN(PNDIS_PACKET) vboxNetFltWinNdisPacketFromSG(PVBOXNETFLTINS pNetFlt, PINTNETSG pSG, PVOID pBufToFree, bool bToWire, bool bCopyMemory);

DECLHIDDEN(void) vboxNetFltWinFreeSGNdisPacket(PNDIS_PACKET pPacket, bool bFreeMem);

#ifdef DEBUG_NETFLT_PACKETS
#define DBG_CHECK_PACKETS(_p1, _p2) \
    {   \
        bool _b = vboxNetFltWinMatchPackets(_p1, _p2, -1);  \
        Assert(_b);  \
    }

#define DBG_CHECK_PACKET_AND_SG(_p, _sg) \
    {   \
        bool _b = vboxNetFltWinMatchPacketAndSG(_p, _sg, -1);  \
        Assert(_b);  \
    }

#define DBG_CHECK_SGS(_sg1, _sg2) \
    {   \
        bool _b = vboxNetFltWinMatchSGs(_sg1, _sg2, -1);  \
        Assert(_b);  \
    }

#else
#define DBG_CHECK_PACKETS(_p1, _p2)
#define DBG_CHECK_PACKET_AND_SG(_p, _sg)
#define DBG_CHECK_SGS(_sg1, _sg2)
#endif

/**
 * Ndis loops back broadcast packets posted to the wire by IntNet
 * This routine is used in the mechanism of preventing this looping
 *
 * @param pAdapt
 * @param pPacket
 * @param bOnRecv true is we are receiving the packet from the wire
 * false otherwise (i.e. the packet is from the host)
 *
 * @return true if the packet is a looped back one, false otherwise
 */
#ifdef VBOX_LOOPBACK_USEFLAGS
DECLINLINE(bool) vboxNetFltWinIsLoopedBackPacket(PNDIS_PACKET pPacket)
{
    return (NdisGetPacketFlags(pPacket) & g_fPacketIsLoopedBack) == g_fPacketIsLoopedBack;
}
#endif

/**************************************************************
 * utility methods for ndis packet creation/initialization    *
 **************************************************************/

#define VBOXNETFLT_OOB_INIT(_p) \
    { \
        NdisZeroMemory(NDIS_OOB_DATA_FROM_PACKET(_p), sizeof(NDIS_PACKET_OOB_DATA)); \
        NDIS_SET_PACKET_HEADER_SIZE(_p, VBOXNETFLT_PACKET_ETHEADER_SIZE); \
    }

#ifndef VBOXNETADP

DECLINLINE(NDIS_STATUS) vboxNetFltWinCopyPacketInfoOnRecv(PNDIS_PACKET pDstPacket, PNDIS_PACKET pSrcPacket, bool bForceStatusResources)
{
    NDIS_STATUS Status = bForceStatusResources ? NDIS_STATUS_RESOURCES : NDIS_GET_PACKET_STATUS(pSrcPacket);
    NDIS_SET_PACKET_STATUS(pDstPacket, Status);

    NDIS_PACKET_FIRST_NDIS_BUFFER(pDstPacket) = NDIS_PACKET_FIRST_NDIS_BUFFER(pSrcPacket);
    NDIS_PACKET_LAST_NDIS_BUFFER(pDstPacket) = NDIS_PACKET_LAST_NDIS_BUFFER(pSrcPacket);

    NdisGetPacketFlags(pDstPacket) = NdisGetPacketFlags(pSrcPacket);

    NDIS_SET_ORIGINAL_PACKET(pDstPacket, NDIS_GET_ORIGINAL_PACKET(pSrcPacket));
    NDIS_SET_PACKET_HEADER_SIZE(pDstPacket, NDIS_GET_PACKET_HEADER_SIZE(pSrcPacket));

    return Status;
}

DECLINLINE(void) vboxNetFltWinCopyPacketInfoOnSend(PNDIS_PACKET pDstPacket, PNDIS_PACKET pSrcPacket)
{
    NDIS_PACKET_FIRST_NDIS_BUFFER(pDstPacket) = NDIS_PACKET_FIRST_NDIS_BUFFER(pSrcPacket);
    NDIS_PACKET_LAST_NDIS_BUFFER(pDstPacket) = NDIS_PACKET_LAST_NDIS_BUFFER(pSrcPacket);

    NdisGetPacketFlags(pDstPacket) = NdisGetPacketFlags(pSrcPacket);

    NdisMoveMemory(NDIS_OOB_DATA_FROM_PACKET(pDstPacket),
                    NDIS_OOB_DATA_FROM_PACKET(pSrcPacket),
                    sizeof (NDIS_PACKET_OOB_DATA));

    NdisIMCopySendPerPacketInfo(pDstPacket, pSrcPacket);

    PVOID pMediaSpecificInfo = NULL;
    UINT fMediaSpecificInfoSize = 0;

    NDIS_GET_PACKET_MEDIA_SPECIFIC_INFO(pSrcPacket, &pMediaSpecificInfo, &fMediaSpecificInfoSize);

    if (pMediaSpecificInfo || fMediaSpecificInfoSize)
    {
        NDIS_SET_PACKET_MEDIA_SPECIFIC_INFO(pDstPacket, pMediaSpecificInfo, fMediaSpecificInfoSize);
    }
}

DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPrepareSendPacket(PVBOXNETFLTINS pNetFlt, PNDIS_PACKET pPacket, PNDIS_PACKET *ppMyPacket);
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPrepareRecvPacket(PVBOXNETFLTINS pNetFlt, PNDIS_PACKET pPacket, PNDIS_PACKET *ppMyPacket, bool bDpr);
#endif

DECLHIDDEN(void) vboxNetFltWinSleep(ULONG milis);

#define MACS_EQUAL(_m1, _m2) \
    ((_m1).au16[0] == (_m2).au16[0] \
        && (_m1).au16[1] == (_m2).au16[1] \
        && (_m1).au16[2] == (_m2).au16[2])


DECLHIDDEN(NDIS_STATUS) vboxNetFltWinDetachFromInterface(PVBOXNETFLTINS pNetFlt, bool bOnUnbind);
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinCopyString(PNDIS_STRING pDst, PNDIS_STRING pSrc);


/**
 * Sets the enmState member atomically.
 *
 * Used for all updates.
 *
 * @param   pThis           The instance.
 * @param   enmNewState     The new value.
 */
DECLINLINE(void) vboxNetFltWinSetWinIfState(PVBOXNETFLTINS pNetFlt, VBOXNETFLT_WINIFSTATE enmNewState)
{
    ASMAtomicWriteU32((uint32_t volatile *)&pNetFlt->u.s.WinIf.enmState, enmNewState);
}

/**
 * Gets the enmState member atomically.
 *
 * Used for all reads.
 *
 * @returns The enmState value.
 * @param   pThis           The instance.
 */
DECLINLINE(VBOXNETFLT_WINIFSTATE) vboxNetFltWinGetWinIfState(PVBOXNETFLTINS pNetFlt)
{
    return (VBOXNETFLT_WINIFSTATE)ASMAtomicUoReadU32((uint32_t volatile *)&pNetFlt->u.s.WinIf.enmState);
}

/* reference the driver module to prevent driver unload */
DECLHIDDEN(void) vboxNetFltWinDrvReference();
/* dereference the driver module to prevent driver unload */
DECLHIDDEN(void) vboxNetFltWinDrvDereference();


#ifndef VBOXNETADP
# define VBOXNETFLT_PROMISCUOUS_SUPPORTED(_pNetFlt) (!(_pNetFlt)->fDisablePromiscuous)
#else
# define STATISTIC_INCREASE(_s) ASMAtomicIncU32((uint32_t volatile *)&(_s));

DECLHIDDEN(void) vboxNetFltWinGenerateMACAddress(RTMAC *pMac);
DECLHIDDEN(int) vboxNetFltWinMAC2NdisString(RTMAC *pMac, PNDIS_STRING pNdisString);
DECLHIDDEN(int) vboxNetFltWinMACFromNdisString(RTMAC *pMac, PNDIS_STRING pNdisString);

#endif
#endif /* !VBOX_INCLUDED_SRC_VBoxNetFlt_win_drv_VBoxNetFltRt_win_h */
