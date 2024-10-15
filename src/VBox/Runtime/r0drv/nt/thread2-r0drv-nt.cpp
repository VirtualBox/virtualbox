/* $Id: thread2-r0drv-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads (Part 2), Ring-0 Driver, NT.
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
#include "the-nt-kernel.h"

#include <iprt/thread.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>

#include "internal/thread.h"


DECLHIDDEN(int) rtThreadNativeInit(void)
{
    /* No TLS in Ring-0. :-/ */
    return VINF_SUCCESS;
}


RTDECL(RTTHREAD) RTThreadSelf(void)
{
    return rtThreadGetByNative((RTNATIVETHREAD)PsGetCurrentThread());
}


DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
    /*
     * Convert the IPRT priority type to NT priority.
     *
     * The NT priority is in the range 0..32, with realtime starting
     * at 16 and the default for user processes at 8. (Should try find
     * the appropriate #defines for some of this...)
     */
    KPRIORITY Priority;
    switch (enmType)
    {
        case RTTHREADTYPE_INFREQUENT_POLLER:    Priority = 6; break;
        case RTTHREADTYPE_EMULATION:            Priority = 7; break;
        case RTTHREADTYPE_DEFAULT:              Priority = 8; break;
        case RTTHREADTYPE_MSG_PUMP:             Priority = 9; break;
        case RTTHREADTYPE_IO:                   Priority = LOW_REALTIME_PRIORITY; break;
        case RTTHREADTYPE_TIMER:                Priority = MAXIMUM_PRIORITY; break;

        default:
            AssertMsgFailed(("enmType=%d\n", enmType));
            return VERR_INVALID_PARAMETER;
    }

    /*
     * Do the actual modification.
     */
    /*KPRIORITY oldPririty = */KeSetPriorityThread((PKTHREAD)pThread->Core.Key, Priority);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtThreadNativeAdopt(PRTTHREADINT pThread)
{
    RT_NOREF1(pThread);
    return VERR_NOT_IMPLEMENTED;
}


DECLHIDDEN(void) rtThreadNativeWaitKludge(PRTTHREADINT pThread)
{
    PVOID pvThreadObj = pThread->Core.Key;
    NTSTATUS rcNt = KeWaitForSingleObject(pvThreadObj, Executive, KernelMode, FALSE, NULL);
    AssertMsg(rcNt == STATUS_SUCCESS, ("rcNt=%#x\n", rcNt)); RT_NOREF1(rcNt);
}


DECLHIDDEN(void) rtThreadNativeDestroy(PRTTHREADINT pThread)
{
    NOREF(pThread);
}


/**
 * Native kernel thread wrapper function.
 *
 * This will forward to rtThreadMain and do termination upon return.
 *
 * @param pvArg         Pointer to the argument package.
 */
static VOID rtThreadNativeMain(PVOID pvArg)
{
    PETHREAD Self = PsGetCurrentThread();
    PRTTHREADINT pThread = (PRTTHREADINT)pvArg;

    rtThreadMain(pThread, (RTNATIVETHREAD)Self, &pThread->szName[0]);

    ObDereferenceObject(Self); /* the rtThreadNativeCreate ref. */
}


DECLHIDDEN(int) rtThreadNativeCreate(PRTTHREADINT pThreadInt, PRTNATIVETHREAD pNativeThread)
{
    /*
     * PsCreateSysemThread create a thread an give us a handle in return.
     * We requests the object for that handle and then close it, so what
     * we keep around is the pointer to the thread object and not a handle.
     * The thread will dereference the object before returning.
     */
    HANDLE hThread = NULL;
    OBJECT_ATTRIBUTES ObjAttr;
    InitializeObjectAttributes(&ObjAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    NTSTATUS rc = PsCreateSystemThread(&hThread,
                                       THREAD_ALL_ACCESS,
                                       &ObjAttr,
                                       NULL /* ProcessHandle - kernel */,
                                       NULL /* ClientID - kernel */,
                                       rtThreadNativeMain,
                                       pThreadInt);
    if (NT_SUCCESS(rc))
    {
        PVOID pvThreadObj;
        rc = ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL /* object type */,
                                       KernelMode, &pvThreadObj, NULL /* handle info */);
        if (NT_SUCCESS(rc))
        {
            ZwClose(hThread);
            *pNativeThread = (RTNATIVETHREAD)pvThreadObj;
        }
        else
            AssertMsgFailed(("%#x\n", rc));
    }
    return RTErrConvertFromNtStatus(rc);
}

