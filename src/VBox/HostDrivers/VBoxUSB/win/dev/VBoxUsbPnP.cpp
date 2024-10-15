/* $Id: VBoxUsbPnP.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB PnP Handling
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

static NTSTATUS vboxUsbPnPMnStartDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    IoCopyCurrentIrpStackLocationToNext(pIrp);
    NTSTATUS Status = VBoxDrvToolIoPostSync(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED);
    if (NT_SUCCESS(Status))
    {
        Status = vboxUsbRtStart(pDevExt);
        Assert(Status == STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            vboxUsbPnPStateSet(pDevExt, ENMVBOXUSB_PNPSTATE_STARTED);
        }
    }

    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbPnPMnQueryStopDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    vboxUsbPnPStateSet(pDevExt, ENMVBOXUSB_PNPSTATE_STOP_PENDING);

    vboxUsbDdiStateReleaseAndWaitCompleted(pDevExt);

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    return IoCallDriver(pDevExt->pLowerDO, pIrp);
}

static NTSTATUS vboxUsbPnPMnStopDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    vboxUsbPnPStateSet(pDevExt, ENMVBOXUSB_PNPSTATE_STOPPED);

    vboxUsbRtClear(pDevExt);

    NTSTATUS Status = VBoxUsbToolDevUnconfigure(pDevExt->pLowerDO);
    Assert(NT_SUCCESS(Status));

    pIrp->IoStatus.Status = Status;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = IoCallDriver(pDevExt->pLowerDO, pIrp);

    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS vboxUsbPnPMnCancelStopDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    ENMVBOXUSB_PNPSTATE enmState = vboxUsbPnPStateGet(pDevExt);
    NTSTATUS Status = STATUS_SUCCESS;

    IoCopyCurrentIrpStackLocationToNext(pIrp);
    Status = VBoxDrvToolIoPostSync(pDevExt->pLowerDO, pIrp);
    if (NT_SUCCESS(Status) && enmState == ENMVBOXUSB_PNPSTATE_STOP_PENDING)
    {
        vboxUsbPnPStateRestore(pDevExt);
    }

    Status = STATUS_SUCCESS;
    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS vboxUsbPnPMnQueryRemoveDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    vboxUsbPnPStateSet(pDevExt, ENMVBOXUSB_PNPSTATE_REMOVE_PENDING);

    vboxUsbDdiStateReleaseAndWaitCompleted(pDevExt);

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    return IoCallDriver(pDevExt->pLowerDO, pIrp);
}

static NTSTATUS vboxUsbPnPRmDev(PVBOXUSBDEV_EXT pDevExt)
{
    NTSTATUS Status = vboxUsbRtRm(pDevExt);
    Assert(Status == STATUS_SUCCESS);

    return Status;
}

static NTSTATUS vboxUsbPnPMnRemoveDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    ENMVBOXUSB_PNPSTATE enmState = vboxUsbPnPStateGet(pDevExt);
    NTSTATUS Status = STATUS_SUCCESS;
    if (enmState != ENMVBOXUSB_PNPSTATE_SURPRISE_REMOVED)
    {
        Status = vboxUsbPnPRmDev(pDevExt);
        Assert(Status == STATUS_SUCCESS);
    }

    vboxUsbPnPStateSet(pDevExt, ENMVBOXUSB_PNPSTATE_REMOVED);

    vboxUsbDdiStateRelease(pDevExt);

    vboxUsbDdiStateReleaseAndWaitRemoved(pDevExt);

    vboxUsbRtClear(pDevExt);

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = IoCallDriver(pDevExt->pLowerDO, pIrp);

    IoDetachDevice(pDevExt->pLowerDO);
    IoDeleteDevice(pDevExt->pFDO);

    return Status;
}

static NTSTATUS vboxUsbPnPMnCancelRemoveDevice(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    ENMVBOXUSB_PNPSTATE enmState = vboxUsbPnPStateGet(pDevExt);
    NTSTATUS Status = STATUS_SUCCESS;
    IoCopyCurrentIrpStackLocationToNext(pIrp);

    Status = VBoxDrvToolIoPostSync(pDevExt->pLowerDO, pIrp);

    if (NT_SUCCESS(Status) &&
        enmState == ENMVBOXUSB_PNPSTATE_REMOVE_PENDING)
    {
        vboxUsbPnPStateRestore(pDevExt);
    }

    Status = STATUS_SUCCESS;
    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS vboxUsbPnPMnSurpriseRemoval(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    vboxUsbPnPStateSet(pDevExt, ENMVBOXUSB_PNPSTATE_SURPRISE_REMOVED);

    NTSTATUS Status = vboxUsbPnPRmDev(pDevExt);
    Assert(Status == STATUS_SUCCESS);

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = IoCallDriver(pDevExt->pLowerDO, pIrp);

    vboxUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS vboxUsbPnPMnQueryCapabilities(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PDEVICE_CAPABILITIES pDevCaps = pSl->Parameters.DeviceCapabilities.Capabilities;

    if (pDevCaps->Version < 1 || pDevCaps->Size < sizeof (*pDevCaps))
    {
        AssertFailed();
        /** @todo return more appropriate status ?? */
        return STATUS_UNSUCCESSFUL;
    }

    pDevCaps->SurpriseRemovalOK = TRUE;
    pIrp->IoStatus.Status = STATUS_SUCCESS;

    IoCopyCurrentIrpStackLocationToNext(pIrp);
    NTSTATUS Status = VBoxDrvToolIoPostSync(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        pDevCaps->SurpriseRemovalOK = 1;
        pDevExt->DdiState.DevCaps = *pDevCaps;
    }

    VBoxDrvToolIoComplete(pIrp, Status, 0);
    vboxUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS vboxUsbPnPMnDefault(PVBOXUSBDEV_EXT pDevExt, PIRP pIrp)
{
    NTSTATUS Status;
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = IoCallDriver(pDevExt->pLowerDO, pIrp);
    vboxUsbDdiStateRelease(pDevExt);
    return Status;
}

DECLHIDDEN(NTSTATUS) vboxUsbDispatchPnP(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp)
{
    PVBOXUSBDEV_EXT pDevExt = (PVBOXUSBDEV_EXT)pDeviceObject->DeviceExtension;
    if (!vboxUsbDdiStateRetainIfNotRemoved(pDevExt))
        return VBoxDrvToolIoComplete(pIrp, STATUS_DELETE_PENDING, 0);

    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    switch (pSl->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            return vboxUsbPnPMnStartDevice(pDevExt, pIrp);

        case IRP_MN_QUERY_STOP_DEVICE:
            return vboxUsbPnPMnQueryStopDevice(pDevExt, pIrp);

        case IRP_MN_STOP_DEVICE:
            return vboxUsbPnPMnStopDevice(pDevExt, pIrp);

        case IRP_MN_CANCEL_STOP_DEVICE:
            return vboxUsbPnPMnCancelStopDevice(pDevExt, pIrp);

        case IRP_MN_QUERY_REMOVE_DEVICE:
            return vboxUsbPnPMnQueryRemoveDevice(pDevExt, pIrp);

        case IRP_MN_REMOVE_DEVICE:
            return vboxUsbPnPMnRemoveDevice(pDevExt, pIrp);

        case IRP_MN_CANCEL_REMOVE_DEVICE:
            return vboxUsbPnPMnCancelRemoveDevice(pDevExt, pIrp);

        case IRP_MN_SURPRISE_REMOVAL:
            return vboxUsbPnPMnSurpriseRemoval(pDevExt, pIrp);

        case IRP_MN_QUERY_CAPABILITIES:
            return vboxUsbPnPMnQueryCapabilities(pDevExt, pIrp);

        default:
            return vboxUsbPnPMnDefault(pDevExt, pIrp);
    }
}

