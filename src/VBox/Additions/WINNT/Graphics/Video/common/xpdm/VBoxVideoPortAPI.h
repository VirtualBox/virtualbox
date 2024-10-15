/* $Id: VBoxVideoPortAPI.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox video port functions header
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_common_xpdm_VBoxVideoPortAPI_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_common_xpdm_VBoxVideoPortAPI_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* To maintain binary backward compatibility with older windows versions
 * we query at runtime for video port functions which are not present in NT 4.0
 * Those could used in the display driver also.
 */

/*Basic datatypes*/
typedef long VBOXVP_STATUS;
#ifndef VBOX_USING_W2K3DDK
typedef struct _ENG_EVENT *VBOXPEVENT;
#else
typedef struct _VIDEO_PORT_EVENT *VBOXPEVENT;
#endif
typedef struct _VIDEO_PORT_SPIN_LOCK *VBOXPSPIN_LOCK;
typedef union _LARGE_INTEGER *VBOXPLARGE_INTEGER;

typedef enum VBOXVP_POOL_TYPE
{
    VBoxVpNonPagedPool,
    VBoxVpPagedPool,
    VBoxVpNonPagedPoolCacheAligned = 4,
    VBoxVpPagedPoolCacheAligned
} VBOXVP_POOL_TYPE;

#define VBOXNOTIFICATION_EVENT 0x00000001UL
#define VBOXNO_ERROR           0x00000000UL

/*VideoPort API functions*/
typedef VBOXVP_STATUS (*PFNWAITFORSINGLEOBJECT) (void*  HwDeviceExtension, void*  Object, VBOXPLARGE_INTEGER  Timeout);
typedef long (*PFNSETEVENT) (void* HwDeviceExtension, VBOXPEVENT  pEvent);
typedef void (*PFNCLEAREVENT) (void*  HwDeviceExtension, VBOXPEVENT  pEvent);
typedef VBOXVP_STATUS (*PFNCREATEEVENT) (void*  HwDeviceExtension, unsigned long  EventFlag, void*  Unused, VBOXPEVENT  *ppEvent);
typedef VBOXVP_STATUS (*PFNDELETEEVENT) (void*  HwDeviceExtension, VBOXPEVENT  pEvent);
typedef void* (*PFNALLOCATEPOOL) (void*  HwDeviceExtension, VBOXVP_POOL_TYPE PoolType, size_t NumberOfBytes, unsigned long Tag);
typedef void (*PFNFREEPOOL) (void*  HwDeviceExtension, void*  Ptr);
typedef unsigned char (*PFNQUEUEDPC) (void* HwDeviceExtension, void (*CallbackRoutine)(void* HwDeviceExtension, void *Context), void *Context);
typedef VBOXVP_STATUS (*PFNCREATESECONDARYDISPLAY)(void* HwDeviceExtension, void* SecondaryDeviceExtension, unsigned long ulFlag);

/* pfn*Event and pfnWaitForSingleObject functions are available */
#define VBOXVIDEOPORTPROCS_EVENT    0x00000002
/* pfn*Pool functions are available */
#define VBOXVIDEOPORTPROCS_POOL     0x00000004
/* pfnQueueDpc function is available */
#define VBOXVIDEOPORTPROCS_DPC      0x00000008
/* pfnCreateSecondaryDisplay function is available */
#define VBOXVIDEOPORTPROCS_CSD      0x00000010

typedef struct VBOXVIDEOPORTPROCS
{
    /* ored VBOXVIDEOPORTPROCS_xxx constants describing the supported functionality */
    uint32_t fSupportedTypes;

    PFNWAITFORSINGLEOBJECT pfnWaitForSingleObject;

    PFNSETEVENT pfnSetEvent;
    PFNCLEAREVENT pfnClearEvent;
    PFNCREATEEVENT pfnCreateEvent;
    PFNDELETEEVENT pfnDeleteEvent;

    PFNALLOCATEPOOL pfnAllocatePool;
    PFNFREEPOOL pfnFreePool;

    PFNQUEUEDPC pfnQueueDpc;

    PFNCREATESECONDARYDISPLAY pfnCreateSecondaryDisplay;
} VBOXVIDEOPORTPROCS;

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_common_xpdm_VBoxVideoPortAPI_h */
