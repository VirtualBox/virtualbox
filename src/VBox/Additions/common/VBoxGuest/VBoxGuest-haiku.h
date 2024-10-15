/* $Id: VBoxGuest-haiku.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuest kernel module, Haiku Guest Additions, header.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GA_INCLUDED_SRC_common_VBoxGuest_VBoxGuest_haiku_h
#define GA_INCLUDED_SRC_common_VBoxGuest_VBoxGuest_haiku_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <OS.h>
#include <Drivers.h>
#include <drivers/module.h>

#include "VBoxGuestInternal.h"
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/process.h>
#include <iprt/mem.h>
#include <iprt/asm.h>
#include <iprt/mp.h>
#include <iprt/power.h>
#include <iprt/thread.h>

/** The module name. */
#define VBOXGUEST_MODULE_NAME "generic/vboxguest"

struct VBoxGuestDeviceState
{
    /** Resource ID of the I/O port */
    int                iIOPortResId;
    /** Pointer to the I/O port resource. */
//    struct resource   *pIOPortRes;
    /** Start address of the IO Port. */
    uint16_t           uIOPortBase;
    /** Resource ID of the MMIO area */
    area_id            iVMMDevMemAreaId;
    /** Pointer to the MMIO resource. */
//    struct resource   *pVMMDevMemRes;
    /** Handle of the MMIO resource. */
//    bus_space_handle_t VMMDevMemHandle;
    /** Size of the memory area. */
    size_t             VMMDevMemSize;
    /** Mapping of the register space */
    void              *pMMIOBase;
    /** IRQ number */
    int                iIrqResId;
    /** IRQ resource handle. */
//    struct resource   *pIrqRes;
    /** Pointer to the IRQ handler. */
//    void              *pfnIrqHandler;
    /** VMMDev version */
    uint32_t           u32Version;

    /** The (only) select data we wait on. */
    //XXX: should leave in pSession ?
    uint8_t            selectEvent;
    uint32_t           selectRef;
    void              *selectSync;
};

struct vboxguest_module_info
{
    module_info module;

    VBOXGUESTDEVEXT devExt;
    struct VBoxGuestDeviceState _sState;
    volatile uint32_t _cUsers;

    size_t(*_RTLogBackdoorPrintf)(const char *pszFormat, ...);
    size_t(*_RTLogBackdoorPrintfV)(const char *pszFormat, va_list args);
    int (*_RTLogSetDefaultInstanceThread)(PRTLOGGER pLogger, uintptr_t uKey);
    int (*_RTMemAllocExTag)(size_t cb, size_t cbAlignment, uint32_t fFlags, const char *pszTag, void **ppv);
    void* (*_RTMemContAlloc)(PRTCCPHYS pPhys, size_t cb);
    void (*_RTMemContFree)(void *pv, size_t cb);
    void (*_RTMemFreeEx)(void *pv, size_t cb);
    bool (*_RTMpIsCpuPossible)(RTCPUID idCpu);
    int (*_RTMpNotificationDeregister)(PFNRTMPNOTIFICATION pfnCallback, void *pvUser);
    int (*_RTMpNotificationRegister)(PFNRTMPNOTIFICATION pfnCallback, void *pvUser);
    int (*_RTMpOnAll)(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2);
    int (*_RTMpOnOthers)(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2);
    int (*_RTMpOnSpecific)(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2);
    int (*_RTPowerNotificationDeregister)(PFNRTPOWERNOTIFICATION pfnCallback, void *pvUser);
    int (*_RTPowerNotificationRegister)(PFNRTPOWERNOTIFICATION pfnCallback, void *pvUser);
    int (*_RTPowerSignalEvent)(RTPOWEREVENT enmEvent);
    void (*_RTR0AssertPanicSystem)(void);
    int (*_RTR0Init)(unsigned fReserved);
    void* (*_RTR0MemObjAddress)(RTR0MEMOBJ MemObj);
    RTR3PTR(*_RTR0MemObjAddressR3)(RTR0MEMOBJ MemObj);
    int (*_RTR0MemObjAllocContTag)(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, bool fExecutable, const char *pszTag);
    int (*_RTR0MemObjAllocLowTag)(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag);
    int (*_RTR0MemObjAllocPageTag)(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag);
    int (*_RTR0MemObjAllocPhysExTag)(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment, const char *pszTag);
    int (*_RTR0MemObjAllocPhysNCTag)(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, const char *pszTag);
    int (*_RTR0MemObjAllocPhysTag)(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, const char *pszTag);
    int (*_RTR0MemObjEnterPhysTag)(PRTR0MEMOBJ pMemObj, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy, const char *pszTag);
    int (*_RTR0MemObjFree)(RTR0MEMOBJ MemObj, bool fFreeMappings);
    RTHCPHYS(*_RTR0MemObjGetPagePhysAddr)(RTR0MEMOBJ MemObj, size_t iPage);
    bool (*_RTR0MemObjIsMapping)(RTR0MEMOBJ MemObj);
    int (*_RTR0MemObjLockKernelTag)(PRTR0MEMOBJ pMemObj, void *pv, size_t cb, uint32_t fAccess, const char *pszTag);
    int (*_RTR0MemObjLockUserTag)(PRTR0MEMOBJ pMemObj, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess,
                                  RTR0PROCESS R0Process, const char *pszTag);
    int (*_RTR0MemObjMapKernelExTag)(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, void *pvFixed, size_t uAlignment,
                                     unsigned fProt, size_t offSub, size_t cbSub, const char *pszTag);
    int (*_RTR0MemObjMapKernelTag)(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, void *pvFixed,
                                   size_t uAlignment, unsigned fProt, const char *pszTag);
    int (*_RTR0MemObjMapUserTag)(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, RTR3PTR R3PtrFixed,
                                 size_t uAlignment, unsigned fProt, RTR0PROCESS R0Process, const char *pszTag);
    int (*_RTR0MemObjProtect)(RTR0MEMOBJ hMemObj, size_t offSub, size_t cbSub, uint32_t fProt);
    int (*_RTR0MemObjReserveKernelTag)(PRTR0MEMOBJ pMemObj, void *pvFixed, size_t cb, size_t uAlignment, const char *pszTag);
    int (*_RTR0MemObjReserveUserTag)(PRTR0MEMOBJ pMemObj, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment,
                                     RTR0PROCESS R0Process, const char *pszTag);
    size_t(*_RTR0MemObjSize)(RTR0MEMOBJ MemObj);
    RTR0PROCESS(*_RTR0ProcHandleSelf)(void);
    void (*_RTR0Term)(void);
    void (*_RTR0TermForced)(void);
    RTPROCESS(*_RTProcSelf)(void);
    uint32_t(*_RTSemEventGetResolution)(void);
    uint32_t(*_RTSemEventMultiGetResolution)(void);
    int (*_RTSemEventMultiWaitEx)(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout);
    int (*_RTSemEventMultiWaitExDebug)(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                       RTHCUINTPTR uId, RT_SRC_POS_DECL);
    int (*_RTSemEventWaitEx)(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout);
    int (*_RTSemEventWaitExDebug)(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout,
                                  RTHCUINTPTR uId, RT_SRC_POS_DECL);
    bool (*_RTThreadIsInInterrupt)(RTTHREAD hThread);
    void (*_RTThreadPreemptDisable)(PRTTHREADPREEMPTSTATE pState);
    bool (*_RTThreadPreemptIsEnabled)(RTTHREAD hThread);
    bool (*_RTThreadPreemptIsPending)(RTTHREAD hThread);
    bool (*_RTThreadPreemptIsPendingTrusty)(void);
    bool (*_RTThreadPreemptIsPossible)(void);
    void (*_RTThreadPreemptRestore)(PRTTHREADPREEMPTSTATE pState);
    uint32_t(*_RTTimerGetSystemGranularity)(void);
    int (*_RTTimerReleaseSystemGranularity)(uint32_t u32Granted);
    int (*_RTTimerRequestSystemGranularity)(uint32_t u32Request, uint32_t *pu32Granted);
    void (*_RTSpinlockAcquire)(RTSPINLOCK Spinlock);
    void (*_RTSpinlockRelease)(RTSPINLOCK Spinlock);
    void* (*_RTMemTmpAllocTag)(size_t cb, const char *pszTag);
    void (*_RTMemTmpFree)(void *pv);
    PRTLOGGER(*_RTLogDefaultInstance)(void);
    PRTLOGGER(*_RTLogDefaultInstanceEx)(uint32_t fFlagsAndGroup);
    PRTLOGGER(*_RTLogRelGetDefaultInstance)(void);
    PRTLOGGER(*_RTLogRelGetDefaultInstanceEx)(uint32_t fFlagsAndGroup);
    int (*_RTErrConvertToErrno)(int iErr);
    int (*_VGDrvCommonIoCtl)(unsigned iFunction, PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                             void *pvData, size_t cbData, size_t *pcbDataReturned);
    int (*_VGDrvCommonCreateUserSession)(PVBOXGUESTDEVEXT pDevExt, uint32_t fRequestor, PVBOXGUESTSESSION *ppSession);
    void (*_VGDrvCommonCloseSession)(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession);
    void* (*_VBoxGuestIDCOpen)(uint32_t *pu32Version);
    int (*_VBoxGuestIDCClose)(void *pvSession);
    int (*_VBoxGuestIDCCall)(void *pvSession, unsigned iCmd, void *pvData, size_t cbData, size_t *pcbDataReturned);
    void (*_RTAssertMsg1Weak)(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction);
    void (*_RTAssertMsg2Weak)(const char *pszFormat, ...);
    void (*_RTAssertMsg2WeakV)(const char *pszFormat, va_list va);
    bool (*_RTAssertShouldPanic)(void);
    int (*_RTSemFastMutexCreate)(PRTSEMFASTMUTEX phFastMtx);
    int (*_RTSemFastMutexDestroy)(RTSEMFASTMUTEX hFastMtx);
    int (*_RTSemFastMutexRelease)(RTSEMFASTMUTEX hFastMtx);
    int (*_RTSemFastMutexRequest)(RTSEMFASTMUTEX hFastMtx);
    int (*_RTSemMutexCreate)(PRTSEMMUTEX phFastMtx);
    int (*_RTSemMutexDestroy)(RTSEMMUTEX hFastMtx);
    int (*_RTSemMutexRelease)(RTSEMMUTEX hFastMtx);
    int (*_RTSemMutexRequest)(RTSEMMUTEX hFastMtx, RTMSINTERVAL cMillies);
    int (*_RTHeapSimpleRelocate)(RTHEAPSIMPLE hHeap, uintptr_t offDelta);
    int (*_RTHeapOffsetInit)(PRTHEAPOFFSET phHeap, void *pvMemory, size_t cbMemory);
    int (*_RTHeapSimpleInit)(PRTHEAPSIMPLE pHeap, void *pvMemory, size_t cbMemory);
    void* (*_RTHeapOffsetAlloc)(RTHEAPOFFSET hHeap, size_t cb, size_t cbAlignment);
    void* (*_RTHeapSimpleAlloc)(RTHEAPSIMPLE Heap, size_t cb, size_t cbAlignment);
    void (*_RTHeapOffsetFree)(RTHEAPOFFSET hHeap, void *pv);
    void (*_RTHeapSimpleFree)(RTHEAPSIMPLE Heap, void *pv);
};


#ifdef IN_VBOXGUEST
#define g_DevExt (g_VBoxGuest.devExt)
#define cUsers (g_VBoxGuest._cUsers)
#define sState (g_VBoxGuest._sState)
#else
#define g_DevExt (g_VBoxGuest->devExt)
#define cUsers (g_VBoxGuest->_cUsers)
#define sState (g_VBoxGuest->_sState)
extern struct vboxguest_module_info *g_VBoxGuest;
#endif

#endif /* !GA_INCLUDED_SRC_common_VBoxGuest_VBoxGuest_haiku_h */

