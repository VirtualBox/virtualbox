/* $Id: VBoxMPVideoPortAPI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Miniport video port api
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

#include "VBoxMPInternal.h"

/*Empty stubs*/
static VP_STATUS
vboxWaitForSingleObjectVoid(IN PVOID  HwDeviceExtension, IN PVOID  Object, IN PLARGE_INTEGER  Timeout  OPTIONAL)
{
    RT_NOREF(HwDeviceExtension, Object, Timeout);
    WARN(("stub called"));
    return ERROR_INVALID_FUNCTION;
}

static LONG vboxSetEventVoid(IN PVOID  HwDeviceExtension, IN PEVENT  pEvent)
{
    RT_NOREF(HwDeviceExtension, pEvent);
    WARN(("stub called"));
    return 0;
}

static VOID vboxClearEventVoid(IN PVOID  HwDeviceExtension, IN PEVENT  pEvent)
{
    RT_NOREF(HwDeviceExtension, pEvent);
    WARN(("stub called"));
}

static VP_STATUS
vboxCreateEventVoid(IN PVOID  HwDeviceExtension, IN ULONG  EventFlag, IN PVOID  Unused, OUT PEVENT  *ppEvent)
{
    RT_NOREF(HwDeviceExtension, EventFlag, Unused, ppEvent);
    WARN(("stub called"));
    return ERROR_INVALID_FUNCTION;
}

static VP_STATUS
vboxDeleteEventVoid(IN PVOID  HwDeviceExtension, IN PEVENT  pEvent)
{
    RT_NOREF(HwDeviceExtension, pEvent);
    WARN(("stub called"));
    return ERROR_INVALID_FUNCTION;
}

static PVOID
vboxAllocatePoolVoid(IN PVOID  HwDeviceExtension, IN VBOXVP_POOL_TYPE  PoolType, IN size_t  NumberOfBytes, IN ULONG  Tag)
{
    RT_NOREF(HwDeviceExtension, PoolType, NumberOfBytes, Tag);
    WARN(("stub called"));
    return NULL;
}

static VOID vboxFreePoolVoid(IN PVOID  HwDeviceExtension, IN PVOID  Ptr)
{
    RT_NOREF(HwDeviceExtension, Ptr);
    WARN(("stub called"));
}

static BOOLEAN
vboxQueueDpcVoid(IN PVOID  HwDeviceExtension, IN PMINIPORT_DPC_ROUTINE  CallbackRoutine, IN PVOID  Context)
{
    RT_NOREF(HwDeviceExtension, CallbackRoutine, Context);
    WARN(("stub called"));
    return FALSE;
}

static VBOXVP_STATUS
vboxCreateSecondaryDisplayVoid(IN PVOID HwDeviceExtension, IN OUT PVOID SecondaryDeviceExtension, IN ULONG fFlag)
{
    RT_NOREF(HwDeviceExtension, SecondaryDeviceExtension, fFlag);
    WARN(("stub called"));
    return ERROR_INVALID_FUNCTION;
}

#define VP_GETPROC(dst, type, name) \
{                                                                                   \
    pAPI->dst = (type)(pConfigInfo->VideoPortGetProcAddress)(pExt, (PUCHAR)(name)); \
}

/*Query video port for api functions or fill with stubs if those are not supported*/
void VBoxSetupVideoPortAPI(PVBOXMP_DEVEXT pExt, PVIDEO_PORT_CONFIG_INFO pConfigInfo)
{
    VBOXVIDEOPORTPROCS *pAPI = &pExt->u.primary.VideoPortProcs;
    VideoPortZeroMemory(pAPI, sizeof(VBOXVIDEOPORTPROCS));

    if (VBoxQueryWinVersion(NULL) <= WINVERSION_NT4)
    {
        /* VideoPortGetProcAddress is available for >= win2k */
        pAPI->pfnWaitForSingleObject = vboxWaitForSingleObjectVoid;
        pAPI->pfnSetEvent = vboxSetEventVoid;
        pAPI->pfnClearEvent = vboxClearEventVoid;
        pAPI->pfnCreateEvent = vboxCreateEventVoid;
        pAPI->pfnDeleteEvent = vboxDeleteEventVoid;
        pAPI->pfnAllocatePool = vboxAllocatePoolVoid;
        pAPI->pfnFreePool = vboxFreePoolVoid;
        pAPI->pfnQueueDpc = vboxQueueDpcVoid;
        pAPI->pfnCreateSecondaryDisplay = vboxCreateSecondaryDisplayVoid;
        return;
    }

    VP_GETPROC(pfnWaitForSingleObject, PFNWAITFORSINGLEOBJECT, "VideoPortWaitForSingleObject");
    VP_GETPROC(pfnSetEvent, PFNSETEVENT, "VideoPortSetEvent");
    VP_GETPROC(pfnClearEvent, PFNCLEAREVENT, "VideoPortClearEvent");
    VP_GETPROC(pfnCreateEvent, PFNCREATEEVENT, "VideoPortCreateEvent");
    VP_GETPROC(pfnDeleteEvent, PFNDELETEEVENT, "VideoPortDeleteEvent");

    if(pAPI->pfnWaitForSingleObject
       && pAPI->pfnSetEvent
       && pAPI->pfnClearEvent
       && pAPI->pfnCreateEvent
       && pAPI->pfnDeleteEvent)
    {
        pAPI->fSupportedTypes |= VBOXVIDEOPORTPROCS_EVENT;
    }
    else
    {
        pAPI->pfnWaitForSingleObject = vboxWaitForSingleObjectVoid;
        pAPI->pfnSetEvent = vboxSetEventVoid;
        pAPI->pfnClearEvent = vboxClearEventVoid;
        pAPI->pfnCreateEvent = vboxCreateEventVoid;
        pAPI->pfnDeleteEvent = vboxDeleteEventVoid;
    }

    VP_GETPROC(pfnAllocatePool, PFNALLOCATEPOOL, "VideoPortAllocatePool");
    VP_GETPROC(pfnFreePool, PFNFREEPOOL, "VideoPortFreePool");

    if(pAPI->pfnAllocatePool
       && pAPI->pfnFreePool)
    {
        pAPI->fSupportedTypes |= VBOXVIDEOPORTPROCS_POOL;
    }
    else
    {
        pAPI->pfnAllocatePool = vboxAllocatePoolVoid;
        pAPI->pfnFreePool = vboxFreePoolVoid;
    }

    VP_GETPROC(pfnQueueDpc, PFNQUEUEDPC, "VideoPortQueueDpc");

    if(pAPI->pfnQueueDpc)
    {
        pAPI->fSupportedTypes |= VBOXVIDEOPORTPROCS_DPC;
    }
    else
    {
        pAPI->pfnQueueDpc = vboxQueueDpcVoid;
    }

    VP_GETPROC(pfnCreateSecondaryDisplay, PFNCREATESECONDARYDISPLAY, "VideoPortCreateSecondaryDisplay");

    if (pAPI->pfnCreateSecondaryDisplay)
    {
        pAPI->fSupportedTypes |= VBOXVIDEOPORTPROCS_CSD;
    }
    else
    {
        pAPI->pfnCreateSecondaryDisplay = vboxCreateSecondaryDisplayVoid;
    }
}

#undef VP_GETPROC
