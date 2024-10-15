/* $Id: shflhandle.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Folders Service - Handles helper functions.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_FOLDERS
#include "shflhandle.h"
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Very basic and primitive handle management. Should be sufficient for our needs.
 * Handle allocation can be rather slow, but at least lookup is fast.
 */
typedef struct
{
    uint32_t         uFlags;
    uintptr_t        pvUserData;
    PSHFLCLIENTDATA  pClient;
} SHFLINTHANDLE, *PSHFLINTHANDLE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static SHFLINTHANDLE *g_pHandles = NULL;
static int32_t        gLastHandleIndex = 0;
static RTCRITSECT     gLock;


int vbsfInitHandleTable()
{
    g_pHandles = (SHFLINTHANDLE *)RTMemAllocZ (sizeof (SHFLINTHANDLE) * SHFLHANDLE_MAX);
    if (!g_pHandles)
    {
        AssertFailed();
        return VERR_NO_MEMORY;
    }

    /* Never return handle 0 */
    g_pHandles[0].uFlags = SHFL_HF_TYPE_DONTUSE;
    gLastHandleIndex     = 1;

    return RTCritSectInit(&gLock);
}

int vbsfFreeHandleTable()
{
    if (g_pHandles)
        RTMemFree(g_pHandles);

    g_pHandles = NULL;

    if (RTCritSectIsInitialized(&gLock))
        RTCritSectDelete(&gLock);

    return VINF_SUCCESS;
}

SHFLHANDLE  vbsfAllocHandle(PSHFLCLIENTDATA pClient, uint32_t uType,
                            uintptr_t pvUserData)
{
    SHFLHANDLE handle;

    Assert((uType & SHFL_HF_TYPE_MASK) != 0 && pvUserData);

    RTCritSectEnter(&gLock);

    /* Find next free handle */
    if (gLastHandleIndex >= SHFLHANDLE_MAX-1)
        gLastHandleIndex = 1;

    /* Nice linear search */
    for(handle=gLastHandleIndex;handle<SHFLHANDLE_MAX;handle++)
    {
        if (g_pHandles[handle].pvUserData == 0)
        {
            gLastHandleIndex = handle;
            break;
        }
    }

    if (handle == SHFLHANDLE_MAX)
    {
        /* Try once more from the start */
        for(handle=1;handle<SHFLHANDLE_MAX;handle++)
        {
            if (g_pHandles[handle].pvUserData == 0)
            {
                gLastHandleIndex = handle;
                break;
            }
        }
        if (handle == SHFLHANDLE_MAX)
        {
            /* Out of handles */
            RTCritSectLeave(&gLock);
            AssertFailed();
            return SHFL_HANDLE_NIL;
        }
    }
    g_pHandles[handle].uFlags     = (uType & SHFL_HF_TYPE_MASK) | SHFL_HF_VALID;
    g_pHandles[handle].pvUserData = pvUserData;
    g_pHandles[handle].pClient    = pClient;

    gLastHandleIndex++;

    RTCritSectLeave(&gLock);

    return handle;
}

static int vbsfFreeHandle(PSHFLCLIENTDATA pClient, SHFLHANDLE handle)
{
    if (   handle < SHFLHANDLE_MAX
        && (g_pHandles[handle].uFlags & SHFL_HF_VALID)
        && g_pHandles[handle].pClient == pClient)
    {
        g_pHandles[handle].uFlags     = 0;
        g_pHandles[handle].pvUserData = 0;
        g_pHandles[handle].pClient    = 0;
        return VINF_SUCCESS;
    }
    return VERR_INVALID_HANDLE;
}

static uintptr_t vbsfQueryHandle(PSHFLCLIENTDATA pClient, SHFLHANDLE handle,
                                 uint32_t uType)
{
    if (   handle < SHFLHANDLE_MAX
        && (g_pHandles[handle].uFlags & SHFL_HF_VALID)
        && g_pHandles[handle].pClient == pClient)
    {
        Assert((uType & SHFL_HF_TYPE_MASK) != 0);

        if (g_pHandles[handle].uFlags & uType)
            return g_pHandles[handle].pvUserData;
    }
    return 0;
}

SHFLFILEHANDLE *vbsfQueryFileHandle(PSHFLCLIENTDATA pClient, SHFLHANDLE handle)
{
    return (SHFLFILEHANDLE *)vbsfQueryHandle(pClient, handle,
                                             SHFL_HF_TYPE_FILE);
}

SHFLFILEHANDLE *vbsfQueryDirHandle(PSHFLCLIENTDATA pClient, SHFLHANDLE handle)
{
    return (SHFLFILEHANDLE *)vbsfQueryHandle(pClient, handle,
                                             SHFL_HF_TYPE_DIR);
}

uint32_t vbsfQueryHandleType(PSHFLCLIENTDATA pClient, SHFLHANDLE handle)
{
    if (   handle < SHFLHANDLE_MAX
        && (g_pHandles[handle].uFlags & SHFL_HF_VALID)
        && g_pHandles[handle].pClient == pClient)
        return g_pHandles[handle].uFlags & SHFL_HF_TYPE_MASK;

    return 0;
}

SHFLHANDLE vbsfAllocDirHandle(PSHFLCLIENTDATA pClient)
{
    SHFLFILEHANDLE *pHandle = (SHFLFILEHANDLE *)RTMemAllocZ (sizeof (SHFLFILEHANDLE));

    if (pHandle)
    {
        pHandle->Header.u32Flags = SHFL_HF_TYPE_DIR;
        return vbsfAllocHandle(pClient, pHandle->Header.u32Flags,
                               (uintptr_t)pHandle);
    }

    return SHFL_HANDLE_NIL;
}

SHFLHANDLE vbsfAllocFileHandle(PSHFLCLIENTDATA pClient)
{
    SHFLFILEHANDLE *pHandle = (SHFLFILEHANDLE *)RTMemAllocZ (sizeof (SHFLFILEHANDLE));

    if (pHandle)
    {
        pHandle->Header.u32Flags = SHFL_HF_TYPE_FILE;
        return vbsfAllocHandle(pClient, pHandle->Header.u32Flags,
                               (uintptr_t)pHandle);
    }

    return SHFL_HANDLE_NIL;
}

void vbsfFreeFileHandle(PSHFLCLIENTDATA pClient, SHFLHANDLE hHandle)
{
    SHFLFILEHANDLE *pHandle = (SHFLFILEHANDLE *)vbsfQueryHandle(pClient,
               hHandle, SHFL_HF_TYPE_DIR|SHFL_HF_TYPE_FILE);

    if (pHandle)
    {
        vbsfFreeHandle(pClient, hHandle);
        RTMemFree (pHandle);
    }
    else
        AssertFailed();
}

