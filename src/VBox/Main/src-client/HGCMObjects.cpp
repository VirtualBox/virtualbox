/* $Id: HGCMObjects.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HGCMObjects - Host-Guest Communication Manager objects
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

#define LOG_GROUP LOG_GROUP_HGCM
#include "LoggingNew.h"

#include "HGCMObjects.h"

#include <iprt/string.h>
#include <iprt/errcore.h>


static RTCRITSECT g_critsect;

/* There are internal handles, which are not saved,
 * and client handles, which are saved.
 * They use different range of values:
 *     1..7FFFFFFF for clients,
 *     0x80000001..0xFFFFFFFF for other handles.
 */
static uint32_t volatile g_u32InternalHandleCount;
static uint32_t volatile g_u32ClientHandleCount;

static PAVLU32NODECORE g_pTree;


DECLINLINE(int) hgcmObjEnter(void)
{
    return RTCritSectEnter(&g_critsect);
}

DECLINLINE(void) hgcmObjLeave(void)
{
    RTCritSectLeave(&g_critsect);
}

int hgcmObjInit(void)
{
    LogFlow(("MAIN::hgcmObjInit\n"));

    g_u32InternalHandleCount = 0x80000000;
    g_u32ClientHandleCount = 0;
    g_pTree = NULL;

    int vrc = RTCritSectInit(&g_critsect);

    LogFlow(("MAIN::hgcmObjInit: vrc = %Rrc\n", vrc));

    return vrc;
}

void hgcmObjUninit(void)
{
    if (RTCritSectIsInitialized(&g_critsect))
        RTCritSectDelete(&g_critsect);
}

uint32_t hgcmObjMake(HGCMObject *pObject, uint32_t u32HandleIn)
{
    int handle = 0;

    LogFlow(("MAIN::hgcmObjGenerateHandle: pObject %p\n", pObject));

    int vrc = hgcmObjEnter();

    if (RT_SUCCESS(vrc))
    {
        ObjectAVLCore *pCore = &pObject->m_core;

        /* Generate a new handle value. */

        uint32_t volatile *pu32HandleCountSource = pObject->Type () == HGCMOBJ_CLIENT?
                                                       &g_u32ClientHandleCount:
                                                       &g_u32InternalHandleCount;

        uint32_t u32Start = *pu32HandleCountSource;

        for (;;)
        {
            uint32_t Key;

            if (u32HandleIn == 0)
            {
                Key = ASMAtomicIncU32(pu32HandleCountSource);

                if (Key == u32Start)
                {
                    /* Rollover. Something is wrong. */
                    AssertReleaseFailed();
                    break;
                }

                /* 0 and 0x80000000 are not valid handles. */
                if ((Key & 0x7FFFFFFF) == 0)
                {
                    /* Over the invalid value, reinitialize the source. */
                    *pu32HandleCountSource = pObject->Type () == HGCMOBJ_CLIENT?
                                                 0:
                                                 UINT32_C(0x80000000);
                    continue;
                }
            }
            else
            {
                Key = u32HandleIn;
            }

            /* Insert object to AVL tree. */
            pCore->AvlCore.Key = Key;

            bool fRC = RTAvlU32Insert(&g_pTree, &pCore->AvlCore);

            /* Could not insert a handle. */
            if (!fRC)
            {
                if (u32HandleIn == 0)
                {
                    /* Try another generated handle. */
                    continue;
                }
                /* Could not use the specified handle. */
                break;
            }

            /* Initialize backlink. */
            pCore->pSelf = pObject;

            /* Reference the object for time while it resides in the tree. */
            pObject->Reference();

            /* Store returned handle. */
            handle = Key;

            Log(("Object key inserted 0x%08X\n", Key));

            break;
        }

        hgcmObjLeave();
    }
    else
    {
        AssertReleaseMsgFailed(("MAIN::hgcmObjGenerateHandle: Failed to acquire object pool semaphore"));
    }

    LogFlow(("MAIN::hgcmObjGenerateHandle: handle = 0x%08X, vrc = %Rrc, return void\n", handle, vrc));

    return handle;
}

uint32_t hgcmObjGenerateHandle(HGCMObject *pObject)
{
    return hgcmObjMake(pObject, 0);
}

uint32_t hgcmObjAssignHandle(HGCMObject *pObject, uint32_t u32Handle)
{
    return hgcmObjMake(pObject, u32Handle);
}

void hgcmObjDeleteHandle(uint32_t handle)
{
    int vrc = VINF_SUCCESS;

    LogFlow(("MAIN::hgcmObjDeleteHandle: handle 0x%08X\n", handle));

    if (handle)
    {
        vrc = hgcmObjEnter();

        if (RT_SUCCESS(vrc))
        {
            ObjectAVLCore *pCore = (ObjectAVLCore *)RTAvlU32Remove(&g_pTree, handle);

            if (pCore)
            {
                AssertRelease(pCore->pSelf);

                pCore->pSelf->Dereference();
            }

            hgcmObjLeave();
        }
        else
        {
            AssertReleaseMsgFailed(("Failed to acquire object pool semaphore, vrc = %Rrc", vrc));
        }
    }

    LogFlow(("MAIN::hgcmObjDeleteHandle: vrc = %Rrc, return void\n", vrc));
}

HGCMObject *hgcmObjReference (uint32_t handle, HGCMOBJ_TYPE enmObjType)
{
    LogFlow(("MAIN::hgcmObjReference: handle 0x%08X\n", handle));

    HGCMObject *pObject = NULL;

    if ((handle & UINT32_C(0x7FFFFFFF)) == 0)
    {
        return pObject;
    }

    int vrc = hgcmObjEnter();

    if (RT_SUCCESS(vrc))
    {
        ObjectAVLCore *pCore = (ObjectAVLCore *)RTAvlU32Get(&g_pTree, handle);

        Assert(!pCore || (pCore->pSelf && pCore->pSelf->Type() == enmObjType));
        if (    pCore
            &&  pCore->pSelf
            &&  pCore->pSelf->Type() == enmObjType)
        {
            pObject = pCore->pSelf;

            AssertRelease(pObject);

            pObject->Reference();
        }

        hgcmObjLeave();
    }
    else
    {
        AssertReleaseMsgFailed(("Failed to acquire object pool semaphore, vrc = %Rrc", vrc));
    }

    LogFlow(("MAIN::hgcmObjReference: return pObject %p\n", pObject));

    return pObject;
}

void hgcmObjDereference(HGCMObject *pObject)
{
    LogFlow(("MAIN::hgcmObjDereference: pObject %p\n", pObject));

    AssertRelease(pObject);

    pObject->Dereference();

    LogFlow(("MAIN::hgcmObjDereference: return\n"));
}

uint32_t hgcmObjQueryHandleCount()
{
    return g_u32ClientHandleCount;
}

void hgcmObjSetHandleCount(uint32_t u32ClientHandleCount)
{
    Assert(g_u32ClientHandleCount <= u32ClientHandleCount);

    int vrc = hgcmObjEnter();

    if (RT_SUCCESS(vrc))
    {
        if (g_u32ClientHandleCount <= u32ClientHandleCount)
            g_u32ClientHandleCount = u32ClientHandleCount;
        hgcmObjLeave();
   }
}
