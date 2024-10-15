/* $Id: VBoxMFInternal.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Mouse Filter Driver - Internal functions.
 *
 * @todo r=bird: Would be better to merge this file into VBoxMFDriver.cpp...
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#undef WIN9X_COMPAT_SPINLOCK
#define WIN9X_COMPAT_SPINLOCK /* Avoid duplicate _KeInitializeSpinLock@4 error on x86. */
#include <iprt/asm.h>
#include "VBoxMF.h"
#include <VBox/VBoxGuestLib.h>
#include <iprt/assert.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct _VBoxGlobalContext
{
    volatile LONG cDevicesStarted;
    volatile LONG fVBGLInited;
    volatile LONG fVBGLInitFailed;
    volatile LONG fHostInformed;
    volatile LONG fHostMouseFound;
    VBGLIDCHANDLE IdcHandle;
    KSPIN_LOCK SyncLock;
    volatile PVBOXMOUSE_DEVEXT pCurrentDevExt;
    LIST_ENTRY DevExtList;
    bool fIsNewProtEnabled;
    MOUSE_INPUT_DATA LastReportedData;
} VBoxGlobalContext;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static VBoxGlobalContext g_ctx = {};


/**
 * Called from DriverEntry to initialize g_ctx.
 */
void VBoxMouFltInitGlobals(void)
{
    RT_ZERO(g_ctx);
    KeInitializeSpinLock(&g_ctx.SyncLock);
    InitializeListHead(&g_ctx.DevExtList);
}


/**
 * Called on driver unload to clean up g_ctx.
 */
void VBoxMouFltDeleteGlobals(void)
{
    Assert(IsListEmpty(&g_ctx.DevExtList));
}


/**
 * @callback_method_impl{FNVBOXGUESTMOUSENOTIFY}
 */
static DECLCALLBACK(void) vboxNewProtMouseEventCb(void *pvUser)
{
    RT_NOREF(pvUser);
    PVBOXMOUSE_DEVEXT pDevExt = (PVBOXMOUSE_DEVEXT)ASMAtomicUoReadPtr((void * volatile *)&g_ctx.pCurrentDevExt);
    if (pDevExt)
    {
        NTSTATUS Status = IoAcquireRemoveLock(&pDevExt->RemoveLock, pDevExt);
        if (NT_SUCCESS(Status))
        {
            ULONG InputDataConsumed = 0;
            VBoxDrvNotifyServiceCB(pDevExt, &g_ctx.LastReportedData, &g_ctx.LastReportedData + 1, &InputDataConsumed);
            IoReleaseRemoveLock(&pDevExt->RemoveLock, pDevExt);
        }
        else
            WARN(("IoAcquireRemoveLock failed, Status (0x%x)", Status));
    }
    else
        WARN(("no current pDevExt specified"));
}

/**
 * Lazy init callback.
 *
 * We don't have control over when VBoxGuest.sys is loaded and therefore cannot
 * be sure it is already around when we are started or our devices instantiated.
 * So, we try lazily attaching to the device when we have a chance.
 *
 * @returns true on success, false on failure.
 */
static bool vboxNewProtLazyRegister(void)
{
    if (g_ctx.fIsNewProtEnabled)
        return true;
    int rc = VbglR0SetMouseNotifyCallback(vboxNewProtMouseEventCb, NULL);
    if (RT_SUCCESS(rc))
    {
        g_ctx.fIsNewProtEnabled = true;
        LOG(("Successfully register mouse event callback with VBoxGuest."));
        return true;
    }
    WARN(("VbglR0SetMouseNotifyCallback failed: %Rrc", rc));
    return false;
}

/**
 * This is called when the last device instance is destroyed.
 */
static void vboxNewProtTerm(void)
{
    Assert(IsListEmpty(&g_ctx.DevExtList));
    if (g_ctx.fIsNewProtEnabled)
    {
        g_ctx.fIsNewProtEnabled = false;
        int rc = VbglR0SetMouseNotifyCallback(NULL, NULL);
        if (RT_FAILURE(rc))
            WARN(("VbglR0SetMouseNotifyCallback failed: %Rrc", rc));
    }
}

/**
 * Worker for VBoxDeviceAdded that enables callback processing of pDevExt.
 *
 * @param   pDevExt             The device instance that was added.
 */
static void vboxNewProtDeviceAdded(PVBOXMOUSE_DEVEXT pDevExt)
{
    /*
     * Always add the device to the list.
     */
    KIRQL Irql;
    KeAcquireSpinLock(&g_ctx.SyncLock, &Irql);

    InsertHeadList(&g_ctx.DevExtList, &pDevExt->ListEntry);

    /* g_ctx.pCurrentDevExt must be associated with the i8042prt device. */
    if (   pDevExt->bHostMouse
        && ASMAtomicCmpXchgPtr(&g_ctx.pCurrentDevExt, pDevExt, NULL))
    {
        /* ensure the object is not deleted while it is being used by a poller thread */
        ObReferenceObject(pDevExt->pdoSelf);
    }

    KeReleaseSpinLock(&g_ctx.SyncLock, Irql);

    /*
     * Do lazy callback registration.
     */
    vboxNewProtLazyRegister();
}

/**
 * Worker for VBoxDeviceRemoved that disables callback processing of pDevExt.
 *
 * @param   pDevExt             The device instance that is being removed.
 */
static void vboxNewProtDeviceRemoved(PVBOXMOUSE_DEVEXT pDevExt)
{
    /*
     * Remove the device from the list.
     */
    KIRQL Irql;
    KeAcquireSpinLock(&g_ctx.SyncLock, &Irql);

    /* Do not try to remove the device from the list if it was never added. */
    if (pDevExt->ListEntry.Flink && pDevExt->ListEntry.Blink)
        RemoveEntryList(&pDevExt->ListEntry);

    /* Check if the PS/2 mouse is being removed. Usually never happens. */
    if (ASMAtomicCmpXchgPtr(&g_ctx.pCurrentDevExt, NULL, pDevExt))
        ObDereferenceObject(pDevExt->pdoSelf);

    KeReleaseSpinLock(&g_ctx.SyncLock, Irql);
}

VOID VBoxDrvNotifyServiceCB(PVBOXMOUSE_DEVEXT pDevExt, PMOUSE_INPUT_DATA InputDataStart, PMOUSE_INPUT_DATA InputDataEnd,
                            PULONG InputDataConsumed)
{
    /* we need to avoid concurrency between the poller thread and our ServiceCB.
     * this is perhaps not the best way of doing things, but the most easiest to avoid concurrency
     * and to ensure the pfnServiceCB is invoked at DISPATCH_LEVEL */
    KIRQL Irql;
    KeAcquireSpinLock(&g_ctx.SyncLock, &Irql);
    if (pDevExt->pSCReq)
    {
        int rc = VbglR0GRPerform(&pDevExt->pSCReq->header);
        if (RT_SUCCESS(rc))
        {
            if (pDevExt->pSCReq->mouseFeatures & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE)
            {
                PMOUSE_INPUT_DATA pData = InputDataStart;
                while (pData < InputDataEnd)
                {
                    pData->LastX = pDevExt->pSCReq->pointerXPos;
                    pData->LastY = pDevExt->pSCReq->pointerYPos;
                    pData->Flags = MOUSE_MOVE_ABSOLUTE;
                    if (g_ctx.fIsNewProtEnabled)
                        pData->Flags |= MOUSE_VIRTUAL_DESKTOP;
                    pData++;
                }

                /* get the last data & cache it */
                --pData;
                g_ctx.LastReportedData.UnitId = pData->UnitId;
            }
        }
        else
        {
            WARN(("VbglR0GRPerform failed with rc=%Rrc", rc));
        }
    }

    /* Call original callback */
    pDevExt->OriginalConnectData.pfnServiceCB(pDevExt->OriginalConnectData.pDO, InputDataStart, InputDataEnd, InputDataConsumed);
    KeReleaseSpinLock(&g_ctx.SyncLock, Irql);
}

static BOOLEAN vboxIsVBGLInited(void)
{
   return InterlockedCompareExchange(&g_ctx.fVBGLInited, TRUE, TRUE) == TRUE;
}

static BOOLEAN vboxIsVBGLInitFailed(void)
{
   return InterlockedCompareExchange(&g_ctx.fVBGLInitFailed, TRUE, TRUE) == TRUE;
}

static BOOLEAN vboxIsHostInformed(void)
{
   return InterlockedCompareExchange(&g_ctx.fHostInformed, TRUE, TRUE) == TRUE;
}

static BOOLEAN vboxIsHostMouseFound(void)
{
   return InterlockedCompareExchange(&g_ctx.fHostMouseFound, TRUE, TRUE) == TRUE;
}

void VBoxDeviceAdded(PVBOXMOUSE_DEVEXT pDevExt)
{
    LOGF_ENTER();
    LONG cCalls = InterlockedIncrement(&g_ctx.cDevicesStarted);

    /* One time Vbgl initialization */
    if (cCalls == 1)
    {
        KeInitializeSpinLock(&g_ctx.SyncLock);
        InitializeListHead(&g_ctx.DevExtList);

        if (!vboxIsVBGLInited() && !vboxIsVBGLInitFailed())
        {
            int rc = VbglR0InitClient();
            if (RT_SUCCESS(rc))
            {
                InterlockedExchange(&g_ctx.fVBGLInited, TRUE);
                LOG(("VBGL init OK"));
                vboxNewProtLazyRegister();
            }
            else
            {
                InterlockedExchange(&g_ctx.fVBGLInitFailed, TRUE);
                WARN(("VBGL init failed with rc=%Rrc", rc));
            }
        }
    }

    if (!vboxIsHostMouseFound())
    {
        NTSTATUS rc;
        UCHAR buffer[512];
        CM_RESOURCE_LIST *pResourceList = (CM_RESOURCE_LIST *)&buffer[0];
        ULONG cbWritten=0;
        BOOLEAN bDetected = FALSE;

        rc = IoGetDeviceProperty(pDevExt->pdoMain, DevicePropertyBootConfiguration,
                                 sizeof(buffer), &buffer[0], &cbWritten);
        if (!NT_SUCCESS(rc))
        {
            if (rc == STATUS_OBJECT_NAME_NOT_FOUND) /* This happen when loading on a running system, don't want the assertion. */
                LOG(("IoGetDeviceProperty failed with STATUS_OBJECT_NAME_NOT_FOUND"));
            else
                WARN(("IoGetDeviceProperty failed with rc=%#x", rc));
            return;
        }

        LOG(("Number of descriptors: %d", pResourceList->Count));

        /* Check if device claims IO port 0x60 or int12 */
        for (ULONG i=0; i<pResourceList->Count; ++i)
        {
            CM_FULL_RESOURCE_DESCRIPTOR *pFullDescriptor = &pResourceList->List[i];

            LOG(("FullDescriptor[%i]: IfType %d, Bus %d, Ver %d, Rev %d, Count %d",
                 i, pFullDescriptor->InterfaceType, pFullDescriptor->BusNumber,
                 pFullDescriptor->PartialResourceList.Version, pFullDescriptor->PartialResourceList.Revision,
                 pFullDescriptor->PartialResourceList.Count));

            for (ULONG j=0; j<pFullDescriptor->PartialResourceList.Count; ++j)
            {
                CM_PARTIAL_RESOURCE_DESCRIPTOR *pPartialDescriptor = &pFullDescriptor->PartialResourceList.PartialDescriptors[j];
                LOG(("PartialDescriptor[%d]: type %d, ShareDisposition %d, Flags 0x%04X, Start 0x%llx, length 0x%x",
                     j, pPartialDescriptor->Type, pPartialDescriptor->ShareDisposition, pPartialDescriptor->Flags,
                     pPartialDescriptor->u.Generic.Start.QuadPart, pPartialDescriptor->u.Generic.Length));

                switch(pPartialDescriptor->Type)
                {
                    case CmResourceTypePort:
                    {
                        LOG(("CmResourceTypePort %#x", pPartialDescriptor->u.Port.Start.QuadPart));
                        if (pPartialDescriptor->u.Port.Start.QuadPart == 0x60)
                        {
                            bDetected = TRUE;
                        }
                        break;
                    }
                    case CmResourceTypeInterrupt:
                    {
                        LOG(("CmResourceTypeInterrupt %ld", pPartialDescriptor->u.Interrupt.Vector));
                        if (pPartialDescriptor->u.Interrupt.Vector == 0xC)
                        {
                            bDetected = TRUE;
                        }
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
        }

        if (bDetected)
        {
            /* It's the emulated 8042 PS/2 mouse/kbd device, so mark it as the Host one.
             * For this device the filter will query absolute mouse coords from the host.
             */
            /** @todo r=bird: The g_ctx.fHostMouseFound needs to be cleared
             *        when the device is removed... */
            InterlockedExchange(&g_ctx.fHostMouseFound, TRUE);

            pDevExt->bHostMouse = TRUE;
            LOG(("Host mouse found"));
        }
    }

    /* Finally call the handler, which needs a correct pDevExt->bHostMouse value. */
    vboxNewProtDeviceAdded(pDevExt);

    LOGF_LEAVE();
}

void VBoxInformHost(PVBOXMOUSE_DEVEXT pDevExt)
{
    LOGF_ENTER();
    if (vboxIsVBGLInited())
    {
        /* Do lazy callback installation. */
        vboxNewProtLazyRegister();

        /* Inform host we support absolute coordinates */
        if (pDevExt->bHostMouse && !vboxIsHostInformed())
        {
            VMMDevReqMouseStatus *req = NULL;
            int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevReqMouseStatus), VMMDevReq_SetMouseStatus);
            if (RT_SUCCESS(rc))
            {
                req->mouseFeatures = VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE;
                if (g_ctx.fIsNewProtEnabled)
                    req->mouseFeatures |= VMMDEV_MOUSE_NEW_PROTOCOL;

                req->pointerXPos = 0;
                req->pointerYPos = 0;

                rc = VbglR0GRPerform(&req->header);
                if (RT_SUCCESS(rc))
                    InterlockedExchange(&g_ctx.fHostInformed, TRUE);
                else
                    WARN(("VbglR0GRPerform failed with rc=%Rrc", rc));

                VbglR0GRFree(&req->header);
            }
            else
                WARN(("VbglR0GRAlloc failed with rc=%Rrc", rc));
        }

        /* Preallocate request to be used in VBoxServiceCB*/
        if (pDevExt->bHostMouse && !pDevExt->pSCReq)
        {
            VMMDevReqMouseStatus *req = NULL;
            int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevReqMouseStatus), VMMDevReq_GetMouseStatus);
            if (RT_SUCCESS(rc))
                InterlockedExchangePointer((PVOID volatile *)&pDevExt->pSCReq, req);
            else
            {
                WARN(("VbglR0GRAlloc for service callback failed with rc=%Rrc", rc));
            }
        }
    }
    else
        WARN(("!vboxIsVBGLInited"));
    LOGF_LEAVE();
}

VOID VBoxDeviceRemoved(PVBOXMOUSE_DEVEXT pDevExt)
{
    LOGF_ENTER();

    /*
     * Tell the host that from now on we can't handle absolute coordinates anymore.
     */
    if (pDevExt->bHostMouse && vboxIsHostInformed())
    {
        VMMDevReqMouseStatus *req = NULL;
        int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevReqMouseStatus), VMMDevReq_SetMouseStatus);
        if (RT_SUCCESS(rc))
        {
            req->mouseFeatures = 0;
            req->pointerXPos = 0;
            req->pointerYPos = 0;

            rc = VbglR0GRPerform(&req->header);
            if (RT_FAILURE(rc))
                WARN(("VbglR0GRPerform failed with rc=%Rrc", rc));

            VbglR0GRFree(&req->header);
        }
        else
            WARN(("VbglR0GRAlloc failed with rc=%Rrc", rc));

        InterlockedExchange(&g_ctx.fHostInformed, FALSE);
    }

    /*
     * Remove the device from the list so we won't get callouts any more.
     */
    vboxNewProtDeviceRemoved(pDevExt);

    /*
     * Free the preallocated request.
     * Note! This could benefit from merging with vboxNewProtDeviceRemoved to
     *       avoid taking the spinlock twice in a row.
     */
    KIRQL Irql;
    KeAcquireSpinLock(&g_ctx.SyncLock, &Irql);
    VMMDevReqMouseStatus *pSCReq = ASMAtomicXchgPtrT(&pDevExt->pSCReq, NULL, VMMDevReqMouseStatus *);
    KeReleaseSpinLock(&g_ctx.SyncLock, Irql);
    if (pSCReq)
        VbglR0GRFree(&pSCReq->header);

    /*
     * Do init ref count handling.
     * Note! This sequence could potentially be racing VBoxDeviceAdded, depending
     *       on what the OS allows to run in parallel...
     */
    LONG cCalls = InterlockedDecrement(&g_ctx.cDevicesStarted);
    if (cCalls == 0)
    {
        if (vboxIsVBGLInited())
        {
            /* Set the flag to prevent reinitializing of the VBGL. */
            InterlockedExchange(&g_ctx.fVBGLInitFailed, TRUE);

            vboxNewProtTerm();
            VbglR0TerminateClient();

            /* The VBGL is now in the not initialized state. */
            InterlockedExchange(&g_ctx.fVBGLInited, FALSE);
            InterlockedExchange(&g_ctx.fVBGLInitFailed, FALSE);
        }
    }

    LOGF_LEAVE();
}

