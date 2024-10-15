/* $Id: VBoxGuest-haiku-stubs.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuest kernel module, Haiku Guest Additions, stubs.
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


/*
 * This file provides stubs for calling VBox runtime functions through the vboxguest module.
 * It should be linked into any driver or module that uses the VBox runtime, except vboxguest
 * itself (which contains the actual library and therefore doesn't need stubs to call it).
 */

#include "VBoxGuest-haiku.h"
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

// >>> file('/tmp/stubs.c', 'w').writelines([re.sub(r'^(?P<returntype>[^(]+) \(\*_(?P<functionname>[A-Za-z0-9_]+)\)\((?P<params>[^)]+)\);', lambda m: '%s %s(%s)\n{\n    %sg_VBoxGuest->_%s(%s);\n}\n' % (m.group(1), m.group(2), m.group(3), ('return ' if m.group(1) != 'void' else ''), m.group(2), (', '.join(a.split(' ')[-1].replace('*', '') for a in m.group(3).split(',')) if m.group(3) != 'void' else '')), f) for f in functions])

struct vboxguest_module_info *g_VBoxGuest;

RTDECL(size_t) RTLogBackdoorPrintf(const char *pszFormat, ...)
{
    va_list args;
    size_t cb;

    va_start(args, pszFormat);
    cb = g_VBoxGuest->_RTLogBackdoorPrintf(pszFormat, args);
    va_end(args);

    return cb;
}
RTDECL(size_t) RTLogBackdoorPrintfV(const char *pszFormat, va_list args)
{
    return g_VBoxGuest->_RTLogBackdoorPrintfV(pszFormat, args);
}
RTDECL(int) RTLogSetDefaultInstanceThread(PRTLOGGER pLogger, uintptr_t uKey)
{
    return g_VBoxGuest->_RTLogSetDefaultInstanceThread(pLogger, uKey);
}
RTDECL(int) RTMemAllocExTag(size_t cb, size_t cbAlignment, uint32_t fFlags, const char *pszTag, void **ppv)
{
    return g_VBoxGuest->_RTMemAllocExTag(cb, cbAlignment, fFlags, pszTag, ppv);
}
RTR0DECL(void*) RTMemContAlloc(PRTCCPHYS pPhys, size_t cb)
{
    return g_VBoxGuest->_RTMemContAlloc(pPhys, cb);
}
RTR0DECL(void) RTMemContFree(void *pv, size_t cb)
{
    g_VBoxGuest->_RTMemContFree(pv, cb);
}
RTDECL(void) RTMemFreeEx(void *pv, size_t cb)
{
    g_VBoxGuest->_RTMemFreeEx(pv, cb);
}
RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
    return g_VBoxGuest->_RTMpIsCpuPossible(idCpu);
}
RTDECL(int) RTMpNotificationDeregister(PFNRTMPNOTIFICATION pfnCallback, void *pvUser)
{
    return g_VBoxGuest->_RTMpNotificationDeregister(pfnCallback, pvUser);
}
RTDECL(int) RTMpNotificationRegister(PFNRTMPNOTIFICATION pfnCallback, void *pvUser)
{
    return g_VBoxGuest->_RTMpNotificationRegister(pfnCallback, pvUser);
}
RTDECL(int) RTMpOnAll(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    return g_VBoxGuest->_RTMpOnAll(pfnWorker, pvUser1, pvUser2);
}
RTDECL(int) RTMpOnOthers(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    return g_VBoxGuest->_RTMpOnOthers(pfnWorker, pvUser1, pvUser2);
}
RTDECL(int) RTMpOnSpecific(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    return g_VBoxGuest->_RTMpOnSpecific(idCpu, pfnWorker, pvUser1, pvUser2);
}
RTDECL(int) RTPowerNotificationDeregister(PFNRTPOWERNOTIFICATION pfnCallback, void *pvUser)
{
    return g_VBoxGuest->_RTPowerNotificationDeregister(pfnCallback, pvUser);
}
RTDECL(int) RTPowerNotificationRegister(PFNRTPOWERNOTIFICATION pfnCallback, void *pvUser)
{
    return g_VBoxGuest->_RTPowerNotificationRegister(pfnCallback, pvUser);
}
RTDECL(int) RTPowerSignalEvent(RTPOWEREVENT enmEvent)
{
    return g_VBoxGuest->_RTPowerSignalEvent(enmEvent);
}
RTR0DECL(void) RTR0AssertPanicSystem(void)
{
    g_VBoxGuest->_RTR0AssertPanicSystem();
}
RTR0DECL(int) RTR0Init(unsigned fReserved)
{
    return g_VBoxGuest->_RTR0Init(fReserved);
}
RTR0DECL(void*) RTR0MemObjAddress(RTR0MEMOBJ MemObj)
{
    return g_VBoxGuest->_RTR0MemObjAddress(MemObj);
}
RTR0DECL(RTR3PTR) RTR0MemObjAddressR3(RTR0MEMOBJ MemObj)
{
    return g_VBoxGuest->_RTR0MemObjAddressR3(MemObj);
}
RTR0DECL(int) RTR0MemObjAllocContTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, bool fExecutable, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjAllocContTag(pMemObj, cb, PhysHighest, fExecutable, pszTag);
}
RTR0DECL(int) RTR0MemObjAllocLowTag(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjAllocLowTag(pMemObj, cb, fExecutable, pszTag);
}
RTR0DECL(int) RTR0MemObjAllocPageTag(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjAllocPageTag(pMemObj, cb, fExecutable, pszTag);
}
RTR0DECL(int) RTR0MemObjAllocPhysExTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjAllocPhysExTag(pMemObj, cb, PhysHighest, uAlignment, pszTag);
}
RTR0DECL(int) RTR0MemObjAllocPhysNCTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjAllocPhysNCTag(pMemObj, cb, PhysHighest, pszTag);
}
RTR0DECL(int) RTR0MemObjAllocPhysTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjAllocPhysTag(pMemObj, cb, PhysHighest, pszTag);
}
RTR0DECL(int) RTR0MemObjEnterPhysTag(PRTR0MEMOBJ pMemObj, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjEnterPhysTag(pMemObj, Phys, cb, uCachePolicy, pszTag);
}
RTR0DECL(int) RTR0MemObjFree(RTR0MEMOBJ MemObj, bool fFreeMappings)
{
    return g_VBoxGuest->_RTR0MemObjFree(MemObj, fFreeMappings);
}
RTR0DECL(RTHCPHYS) RTR0MemObjGetPagePhysAddr(RTR0MEMOBJ MemObj, size_t iPage)
{
    return g_VBoxGuest->_RTR0MemObjGetPagePhysAddr(MemObj, iPage);
}
RTR0DECL(bool) RTR0MemObjIsMapping(RTR0MEMOBJ MemObj)
{
    return g_VBoxGuest->_RTR0MemObjIsMapping(MemObj);
}
RTR0DECL(int) RTR0MemObjLockKernelTag(PRTR0MEMOBJ pMemObj, void *pv, size_t cb, uint32_t fAccess, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjLockKernelTag(pMemObj, pv, cb, fAccess, pszTag);
}
RTR0DECL(int) RTR0MemObjLockUserTag(PRTR0MEMOBJ pMemObj, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess, RTR0PROCESS R0Process, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjLockUserTag(pMemObj, R3Ptr, cb, fAccess, R0Process, pszTag);
}
RTR0DECL(int) RTR0MemObjMapKernelExTag(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, void *pvFixed, size_t uAlignment, unsigned fProt, size_t offSub, size_t cbSub, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjMapKernelExTag(pMemObj, MemObjToMap, pvFixed, uAlignment, fProt, offSub, cbSub, pszTag);
}
RTR0DECL(int) RTR0MemObjMapKernelTag(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, void *pvFixed, size_t uAlignment, unsigned fProt, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjMapKernelTag(pMemObj, MemObjToMap, pvFixed, uAlignment, fProt, pszTag);
}
RTR0DECL(int) RTR0MemObjMapUserTag(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, RTR3PTR R3PtrFixed, size_t uAlignment, unsigned fProt, RTR0PROCESS R0Process, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjMapUserTag(pMemObj, MemObjToMap, R3PtrFixed, uAlignment, fProt, R0Process, pszTag);
}
RTR0DECL(int) RTR0MemObjProtect(RTR0MEMOBJ hMemObj, size_t offSub, size_t cbSub, uint32_t fProt)
{
    return g_VBoxGuest->_RTR0MemObjProtect(hMemObj, offSub, cbSub, fProt);
}
RTR0DECL(int) RTR0MemObjReserveKernelTag(PRTR0MEMOBJ pMemObj, void *pvFixed, size_t cb, size_t uAlignment, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjReserveKernelTag(pMemObj, pvFixed, cb, uAlignment, pszTag);
}
RTR0DECL(int) RTR0MemObjReserveUserTag(PRTR0MEMOBJ pMemObj, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment, RTR0PROCESS R0Process, const char *pszTag)
{
    return g_VBoxGuest->_RTR0MemObjReserveUserTag(pMemObj, R3PtrFixed, cb, uAlignment, R0Process, pszTag);
}
RTR0DECL(size_t) RTR0MemObjSize(RTR0MEMOBJ MemObj)
{
    return g_VBoxGuest->_RTR0MemObjSize(MemObj);
}
RTR0DECL(RTR0PROCESS) RTR0ProcHandleSelf(void)
{
    return g_VBoxGuest->_RTR0ProcHandleSelf();
}
RTR0DECL(void) RTR0Term(void)
{
    g_VBoxGuest->_RTR0Term();
}
RTR0DECL(void) RTR0TermForced(void)
{
    g_VBoxGuest->_RTR0TermForced();
}
RTDECL(RTPROCESS) RTProcSelf(void)
{
    return g_VBoxGuest->_RTProcSelf();
}
RTDECL(uint32_t) RTSemEventGetResolution(void)
{
    return g_VBoxGuest->_RTSemEventGetResolution();
}
RTDECL(uint32_t) RTSemEventMultiGetResolution(void)
{
    return g_VBoxGuest->_RTSemEventMultiGetResolution();
}
RTDECL(int) RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
    return g_VBoxGuest->_RTSemEventMultiWaitEx(hEventMultiSem, fFlags, uTimeout);
}
RTDECL(int) RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return g_VBoxGuest->_RTSemEventMultiWaitExDebug(hEventMultiSem, fFlags, uTimeout, uId, pszFile, iLine, pszFunction);
}
RTDECL(int) RTSemEventWaitEx(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout)
{
    return g_VBoxGuest->_RTSemEventWaitEx(hEventSem, fFlags, uTimeout);
}
RTDECL(int) RTSemEventWaitExDebug(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    return g_VBoxGuest->_RTSemEventWaitExDebug(hEventSem, fFlags, uTimeout, uId, pszFile, iLine, pszFunction);
}
RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread)
{
    return g_VBoxGuest->_RTThreadIsInInterrupt(hThread);
}
RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
    g_VBoxGuest->_RTThreadPreemptDisable(pState);
}
RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
    return g_VBoxGuest->_RTThreadPreemptIsEnabled(hThread);
}
RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    return g_VBoxGuest->_RTThreadPreemptIsPending(hThread);
}
RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    return g_VBoxGuest->_RTThreadPreemptIsPendingTrusty();
}
RTDECL(bool) RTThreadPreemptIsPossible(void)
{
    return g_VBoxGuest->_RTThreadPreemptIsPossible();
}
RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
    g_VBoxGuest->_RTThreadPreemptRestore(pState);
}
RTDECL(uint32_t) RTTimerGetSystemGranularity(void)
{
    return g_VBoxGuest->_RTTimerGetSystemGranularity();
}
RTDECL(int) RTTimerReleaseSystemGranularity(uint32_t u32Granted)
{
    return g_VBoxGuest->_RTTimerReleaseSystemGranularity(u32Granted);
}
RTDECL(int) RTTimerRequestSystemGranularity(uint32_t u32Request, uint32_t *pu32Granted)
{
    return g_VBoxGuest->_RTTimerRequestSystemGranularity(u32Request, pu32Granted);
}
RTDECL(void) RTSpinlockAcquire(RTSPINLOCK Spinlock)
{
    g_VBoxGuest->_RTSpinlockAcquire(Spinlock);
}
RTDECL(void) RTSpinlockRelease(RTSPINLOCK Spinlock)
{
    g_VBoxGuest->_RTSpinlockRelease(Spinlock);
}
RTDECL(void*) RTMemTmpAllocTag(size_t cb, const char *pszTag)
{
    return g_VBoxGuest->_RTMemTmpAllocTag(cb, pszTag);
}
RTDECL(void) RTMemTmpFree(void *pv)
{
    g_VBoxGuest->_RTMemTmpFree(pv);
}
RTDECL(PRTLOGGER) RTLogDefaultInstance(void)
{
    return g_VBoxGuest->_RTLogDefaultInstance();
}
RTDECL(PRTLOGGER) RTLogDefaultInstanceEx(uint32_t fFlagsAndGroup)
{
    return g_VBoxGuest->_RTLogDefaultInstanceEx(fFlagsAndGroup);
}
RTDECL(PRTLOGGER) RTLogRelGetDefaultInstance(void)
{
    return g_VBoxGuest->_RTLogRelGetDefaultInstance();
}
RTDECL(PRTLOGGER) RTLogRelGetDefaultInstance(uint32_t fFlags, uint32_t iGroup)
{
    return g_VBoxGuest->_RTLogRelGetDefaultInstanceEx(fFlags, iGroup);
}
RTDECL(int) RTErrConvertToErrno(int iErr)
{
    return g_VBoxGuest->_RTErrConvertToErrno(iErr);
}
int VGDrvCommonIoCtl(unsigned iFunction, PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, void *pvData, size_t cbData, size_t *pcbDataReturned)
{
    return g_VBoxGuest->_VGDrvCommonIoCtl(iFunction, pDevExt, pSession, pvData, cbData, pcbDataReturned);
}
int VGDrvCommonCreateUserSession(PVBOXGUESTDEVEXT pDevExt, uint32_t fRequestor, PVBOXGUESTSESSION *ppSession)
{
    return g_VBoxGuest->_VGDrvCommonCreateUserSession(pDevExt, fRequestor, ppSession);
}
void VGDrvCommonCloseSession(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession)
{
    g_VBoxGuest->_VGDrvCommonCloseSession(pDevExt, pSession);
}
void* VBoxGuestIDCOpen(uint32_t *pu32Version)
{
    return g_VBoxGuest->_VBoxGuestIDCOpen(pu32Version);
}
int VBoxGuestIDCClose(void *pvSession)
{
    return g_VBoxGuest->_VBoxGuestIDCClose(pvSession);
}
int VBoxGuestIDCCall(void *pvSession, unsigned iCmd, void *pvData, size_t cbData, size_t *pcbDataReturned)
{
    return g_VBoxGuest->_VBoxGuestIDCCall(pvSession, iCmd, pvData, cbData, pcbDataReturned);
}
RTDECL(void) RTAssertMsg1Weak(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    g_VBoxGuest->_RTAssertMsg1Weak(pszExpr, uLine, pszFile, pszFunction);
}
RTDECL(void) RTAssertMsg2Weak(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTAssertMsg2WeakV(pszFormat, va);
    va_end(va);
}
RTDECL(void) RTAssertMsg2WeakV(const char *pszFormat, va_list va)
{
    g_VBoxGuest->_RTAssertMsg2WeakV(pszFormat, va);
}
RTDECL(bool) RTAssertShouldPanic(void)
{
    return g_VBoxGuest->_RTAssertShouldPanic();
}
RTDECL(int) RTSemFastMutexCreate(PRTSEMFASTMUTEX phFastMtx)
{
    return g_VBoxGuest->_RTSemFastMutexCreate(phFastMtx);
}
RTDECL(int) RTSemFastMutexDestroy(RTSEMFASTMUTEX hFastMtx)
{
    return g_VBoxGuest->_RTSemFastMutexDestroy(hFastMtx);
}
RTDECL(int) RTSemFastMutexRelease(RTSEMFASTMUTEX hFastMtx)
{
    return g_VBoxGuest->_RTSemFastMutexRelease(hFastMtx);
}
RTDECL(int) RTSemFastMutexRequest(RTSEMFASTMUTEX hFastMtx)
{
    return g_VBoxGuest->_RTSemFastMutexRequest(hFastMtx);
}
RTDECL(int) RTSemMutexCreate(PRTSEMMUTEX phFastMtx)
{
    return g_VBoxGuest->_RTSemMutexCreate(phFastMtx);
}
RTDECL(int) RTSemMutexDestroy(RTSEMMUTEX hFastMtx)
{
    return g_VBoxGuest->_RTSemMutexDestroy(hFastMtx);
}
RTDECL(int) RTSemMutexRelease(RTSEMMUTEX hFastMtx)
{
    return g_VBoxGuest->_RTSemMutexRelease(hFastMtx);
}
RTDECL(int) RTSemMutexRequest(RTSEMMUTEX hFastMtx, RTMSINTERVAL cMillies)
{
    return g_VBoxGuest->_RTSemMutexRequest(hFastMtx, cMillies);
}
int RTHeapSimpleRelocate(RTHEAPSIMPLE hHeap, uintptr_t offDelta)
{
    return g_VBoxGuest->_RTHeapSimpleRelocate(hHeap, offDelta);
}
int RTHeapOffsetInit(PRTHEAPOFFSET phHeap, void *pvMemory, size_t cbMemory)
{
    return g_VBoxGuest->_RTHeapOffsetInit(phHeap, pvMemory, cbMemory);
}
int RTHeapSimpleInit(PRTHEAPSIMPLE pHeap, void *pvMemory, size_t cbMemory)
{
    return g_VBoxGuest->_RTHeapSimpleInit(pHeap, pvMemory, cbMemory);
}
void* RTHeapOffsetAlloc(RTHEAPOFFSET hHeap, size_t cb, size_t cbAlignment)
{
    return g_VBoxGuest->_RTHeapOffsetAlloc(hHeap, cb, cbAlignment);
}
void* RTHeapSimpleAlloc(RTHEAPSIMPLE Heap, size_t cb, size_t cbAlignment)
{
    return g_VBoxGuest->_RTHeapSimpleAlloc(Heap, cb, cbAlignment);
}
void RTHeapOffsetFree(RTHEAPOFFSET hHeap, void *pv)
{
    g_VBoxGuest->_RTHeapOffsetFree(hHeap, pv);
}
void RTHeapSimpleFree(RTHEAPSIMPLE Heap, void *pv)
{
    g_VBoxGuest->_RTHeapSimpleFree(Heap, pv);
}

