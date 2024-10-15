/* $Id: VBoxUsbPwr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB Power state Handling
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

#include "VBoxUsbCmn.h"

#include <iprt/assert.h>

DECLHIDDEN(VOID) vboxUsbPwrStateInit(PVBOXUSBDEV_EXT pDevExt)
{
    POWER_STATE PowerState;
    PowerState.SystemState = PowerSystemWorking;
    PowerState.DeviceState = PowerDeviceD0;
    PoSetPowerState(pDevExt->pFDO, DevicePowerState, PowerState);
    pDevExt->DdiState.PwrState.PowerState = PowerState;
    pDevExt->DdiState.PwrState.PowerDownLevel = PowerDeviceUnspecified;
}

static NTSTATUS vboxUsbPwrMnDefault(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    NTSTATUS Status;
    PoStartNextPowerIrp(pIrp);
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbPwrMnPowerSequence(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    AssertFailed();
    return vboxUsbPwrMnDefault(pDevExt, pIrp);
}

typedef struct VBOXUSB_PWRDEV_CTX
{
    PVBOXUSBDEV_EXT pDevExt;
    PIRP pIrp;
} VBOXUSB_PWRDEV_CTX, *PVBOXUSB_PWRDEV_CTX;

static VOID vboxUsbPwrIoDeviceCompletion(IN PDEVICE_OBJECT pDeviceObject,
                                         IN UCHAR MinorFunction,
                                         IN POWER_STATE PowerState,
                                         IN PVOID pvContext,
                                         IN PIO_STATUS_BLOCK pIoStatus)
{
    RT_NOREF3(pDeviceObject, MinorFunction, PowerState);
    PVBOXUSB_PWRDEV_CTX pDevCtx = (PVBOXUSB_PWRDEV_CTX)pvContext;
    PVBOXUSBDEV_EXT pDevExt = pDevCtx->pDevExt;
    PIRP pIrp = pDevCtx->pIrp;
    pIrp->IoStatus.Status = pIoStatus->Status;
    pIrp->IoStatus.Information = 0;

    PoStartNextPowerIrp(pIrp);
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    vboxUsbDdiStateRelease(pDevExt);

    vboxUsbMemFree(pDevCtx);
}

static NTSTATUS vboxUsbPwrIoRequestDev(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    POWER_STATE PwrState;
    PwrState.SystemState = pSl->Parameters.Power.State.SystemState;
    PwrState.DeviceState = pDevExt->DdiState.DevCaps.DeviceState[PwrState.SystemState];

    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
    PVBOXUSB_PWRDEV_CTX pDevCtx = (PVBOXUSB_PWRDEV_CTX)vboxUsbMemAlloc(sizeof (*pDevCtx));
    Assert(pDevCtx);
    if (pDevCtx)
    {
        pDevCtx->pDevExt = pDevExt;
        pDevCtx->pIrp = pIrp;

        Status = PoRequestPowerIrp(pDevExt->pPDO, pSl->MinorFunction, PwrState,
                                   vboxUsbPwrIoDeviceCompletion, pDevCtx, NULL);
        Assert(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
            return STATUS_MORE_PROCESSING_REQUIRED;

        vboxUsbMemFree(pDevCtx);
    }

    PoStartNextPowerIrp(pIrp);
    pIrp->IoStatus.Status = Status;
    pIrp->IoStatus.Information = 0;
    vboxUsbDdiStateRelease(pDevExt);

    /* the "real" Status is stored in pIrp->IoStatus.Status,
     * return success here to complete the Io */
    return STATUS_SUCCESS;
}

static NTSTATUS vboxUsbPwrIoPostSysCompletion(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp, IN PVOID pvContext)
{
    RT_NOREF1(pDevObj);
    PVBOXUSBDEV_EXT pDevExt = (PVBOXUSBDEV_EXT)pvContext;
    NTSTATUS Status = pIrp->IoStatus.Status;
    Assert(Status == STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
        switch (pSl->MinorFunction)
        {
            case IRP_MN_SET_POWER:
                pDevExt->DdiState.PwrState.PowerState.SystemState = pSl->Parameters.Power.State.SystemState;
                break;

            default:
                break;
        }

        return vboxUsbPwrIoRequestDev(pDevExt, pIrp);
    }

    PoStartNextPowerIrp(pIrp);
    vboxUsbDdiStateRelease(pDevExt);
    return STATUS_SUCCESS;
}

static NTSTATUS vboxUsbPwrIoPostSys(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    IoMarkIrpPending(pIrp);
    IoCopyCurrentIrpStackLocationToNext(pIrp);
    IoSetCompletionRoutine(pIrp, vboxUsbPwrIoPostSysCompletion, pDevExt, TRUE, TRUE, TRUE);
    NTSTATUS Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status)); NOREF(Status);
    return STATUS_PENDING;
}

static NTSTATUS vboxUsbPwrQueryPowerSys(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    /*PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    SYSTEM_POWER_STATE enmSysPState = pSl->Parameters.Power.State.SystemState;*/

    return vboxUsbPwrIoPostSys(pDevExt, pIrp);
}

static NTSTATUS vboxUsbPwrIoPostDevCompletion(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp, IN PVOID pvContext)
{
    RT_NOREF1(pDevObj);
    PVBOXUSBDEV_EXT pDevExt = (PVBOXUSBDEV_EXT)pvContext;

    if (pIrp->PendingReturned)
        IoMarkIrpPending(pIrp);

    NTSTATUS Status = pIrp->IoStatus.Status;
    Assert(Status == STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
        switch (pSl->MinorFunction)
        {
            case IRP_MN_SET_POWER:
                pDevExt->DdiState.PwrState.PowerState.DeviceState = pSl->Parameters.Power.State.DeviceState;
                PoSetPowerState(pDevExt->pFDO, DevicePowerState, pSl->Parameters.Power.State);
                break;

            default:
                break;
        }
    }

    PoStartNextPowerIrp(pIrp);
    vboxUsbDdiStateRelease(pDevExt);
    return STATUS_SUCCESS;
}

static NTSTATUS vboxUsbPwrIoPostDev(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    IoMarkIrpPending(pIrp);
    IoCopyCurrentIrpStackLocationToNext(pIrp);
    IoSetCompletionRoutine(pIrp, vboxUsbPwrIoPostDevCompletion, pDevExt, TRUE, TRUE, TRUE);
    NTSTATUS Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status)); RT_NOREF_PV(Status);
    return STATUS_PENDING;
}

typedef struct VBOXUSB_IOASYNC_CTX
{
    PIO_WORKITEM pWrkItem;
    PIRP pIrp;
} VBOXUSB_IOASYNC_CTX, *PVBOXUSB_IOASYNC_CTX;

static VOID vboxUsbPwrIoWaitCompletionAndPostAsyncWorker(IN PDEVICE_OBJECT pDeviceObject, IN PVOID pvContext)
{
    PVBOXUSBDEV_EXT pDevExt = (PVBOXUSBDEV_EXT)pDeviceObject->DeviceExtension;
    PVBOXUSB_IOASYNC_CTX pCtx = (PVBOXUSB_IOASYNC_CTX)pvContext;
    PIRP pIrp = pCtx->pIrp;

    vboxUsbPwrIoPostDev(pDevExt, pIrp);

    IoFreeWorkItem(pCtx->pWrkItem);
    vboxUsbMemFree(pCtx);
}

static NTSTATUS vboxUsbPwrIoWaitCompletionAndPostAsync(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
    PVBOXUSB_IOASYNC_CTX pCtx = (PVBOXUSB_IOASYNC_CTX)vboxUsbMemAlloc(sizeof (*pCtx));
    Assert(pCtx);
    if (pCtx)
    {
        PIO_WORKITEM pWrkItem = IoAllocateWorkItem(pDevExt->pFDO);
        Assert(pWrkItem);
        if (pWrkItem)
        {
            pCtx->pWrkItem = pWrkItem;
            pCtx->pIrp = pIrp;
            IoMarkIrpPending(pIrp);
            IoQueueWorkItem(pWrkItem, vboxUsbPwrIoWaitCompletionAndPostAsyncWorker, DelayedWorkQueue, pCtx);
            return STATUS_PENDING;
        }
        vboxUsbMemFree(pCtx);
    }
    return Status;
}

static NTSTATUS vboxUsbPwrQueryPowerDev(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    DEVICE_POWER_STATE enmDevPState = pSl->Parameters.Power.State.DeviceState;
    NTSTATUS Status = STATUS_SUCCESS;

    if (enmDevPState >= pDevExt->DdiState.PwrState.PowerState.DeviceState)
    {
        Status = vboxUsbPwrIoWaitCompletionAndPostAsync(pDevExt, pIrp);
        Assert(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
            return Status;
    }

    pIrp->IoStatus.Status = Status;
    pIrp->IoStatus.Information = 0;

    PoStartNextPowerIrp(pIrp);

    if (NT_SUCCESS(Status))
    {
        IoSkipCurrentIrpStackLocation(pIrp);
        Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    }
    else
    {
        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    }

    vboxUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS vboxUsbPwrMnQueryPower(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    switch (pSl->Parameters.Power.Type)
    {
        case SystemPowerState:
            return vboxUsbPwrQueryPowerSys(pDevExt, pIrp);

        case DevicePowerState:
            return vboxUsbPwrQueryPowerDev(pDevExt, pIrp);

        default:
            AssertFailed();
            return vboxUsbPwrMnDefault(pDevExt, pIrp);

    }
}

static NTSTATUS vboxUsbPwrSetPowerSys(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    /*PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    SYSTEM_POWER_STATE enmSysPState = pSl->Parameters.Power.State.SystemState;*/

    return vboxUsbPwrIoPostSys(pDevExt, pIrp);
}

static NTSTATUS vboxUsbPwrSetPowerDev(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    DEVICE_POWER_STATE enmDevPState = pSl->Parameters.Power.State.DeviceState;
    DEVICE_POWER_STATE enmCurDevPState = pDevExt->DdiState.PwrState.PowerState.DeviceState;
    NTSTATUS Status = STATUS_SUCCESS;

    if (enmDevPState > enmCurDevPState && enmCurDevPState == PowerDeviceD0)
    {
        Status = vboxUsbPwrIoWaitCompletionAndPostAsync(pDevExt, pIrp);
        Assert(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
            return Status;
    }

    PoStartNextPowerIrp(pIrp);

    if (NT_SUCCESS(Status))
    {
        IoCopyCurrentIrpStackLocationToNext(pIrp);
        IoSetCompletionRoutine(pIrp, vboxUsbPwrIoPostDevCompletion, pDevExt, TRUE, TRUE, TRUE);
        Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    }
    else
    {
        pIrp->IoStatus.Status = Status;
        pIrp->IoStatus.Information = 0;

        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
        vboxUsbDdiStateRelease(pDevExt);
    }

    return Status;
}


static NTSTATUS vboxUsbPwrMnSetPower(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    switch (pSl->Parameters.Power.Type)
    {
        case SystemPowerState:
            return vboxUsbPwrSetPowerSys(pDevExt, pIrp);

        case DevicePowerState:
            return vboxUsbPwrSetPowerDev(pDevExt, pIrp);

        default:
            AssertFailed();
            return vboxUsbPwrMnDefault(pDevExt, pIrp);
    }
}

static NTSTATUS vboxUsbPwrMnWaitWake(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    AssertFailed();
    return vboxUsbPwrMnDefault(pDevExt, pIrp);
}


static NTSTATUS vboxUsbPwrDispatch(IN PVBOXUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);

    switch (pSl->MinorFunction)
    {
        case IRP_MN_POWER_SEQUENCE:
            return vboxUsbPwrMnPowerSequence(pDevExt, pIrp);

        case IRP_MN_QUERY_POWER:
            return vboxUsbPwrMnQueryPower(pDevExt, pIrp);

        case IRP_MN_SET_POWER:
            return vboxUsbPwrMnSetPower(pDevExt, pIrp);

        case IRP_MN_WAIT_WAKE:
            return vboxUsbPwrMnWaitWake(pDevExt, pIrp);

        default:
//            AssertFailed();
            return vboxUsbPwrMnDefault(pDevExt, pIrp);
    }
}

DECLHIDDEN(NTSTATUS) vboxUsbDispatchPower(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp)
{
    PVBOXUSBDEV_EXT pDevExt = (PVBOXUSBDEV_EXT)pDeviceObject->DeviceExtension;
    ENMVBOXUSB_PNPSTATE enmState = vboxUsbDdiStateRetainIfNotRemoved(pDevExt);
    switch (enmState)
    {
        case ENMVBOXUSB_PNPSTATE_REMOVED:
            PoStartNextPowerIrp(pIrp);

            pIrp->IoStatus.Status = STATUS_DELETE_PENDING;
            pIrp->IoStatus.Information = 0;

            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            vboxUsbDdiStateRelease(pDevExt);

            return STATUS_DELETE_PENDING;

        case ENMVBOXUSB_PNPSTATE_START_PENDING:
            PoStartNextPowerIrp(pIrp);
            IoSkipCurrentIrpStackLocation(pIrp);

            vboxUsbDdiStateRelease(pDevExt);

            return PoCallDriver(pDevExt->pLowerDO, pIrp);

        default:
            return vboxUsbPwrDispatch(pDevExt, pIrp);
    }
}

